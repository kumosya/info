/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/core/core_dump.cc
 * Bounded ELF core snapshots, writer tickets, and timeout cleanup.
 */

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include <os/elf.h>

namespace kernel::task {

namespace {
// Bound snapshot memory, thread notes and load headers before any user fault
// can request work. noteOffset/dataOffset reserve fixed metadata space; the
// configured byte budget must also fit that overhead and at least a page.
constexpr uint64_t maximumSnapshot = 16 * 1024 * 1024;
constexpr unsigned maximumSlots = 2, maximumLoads = 256, maximumThreads = 64;
constexpr uint64_t noteOffset = 16384, dataOffset = 81920;
// Byte copy used to assemble the on-disk image without runtime-library calls.
void copy(void* destination, const void* source, size_t size) {
    for (size_t i = 0; i < size; ++i)
        static_cast<uint8_t*>(destination)[i] =
            static_cast<const uint8_t*>(source)[i];
}
// Round note lengths to their ABI alignment; callers supply a power of two.
uint64_t align(uint64_t n, uint64_t a) { return (n + a - 1) & ~(a - 1); }
// Linux x86-64 NT_PRSTATUS byte layout, including explicit padding. These
// records are written verbatim for debuggers, not serialized as C++ objects.
struct PrStatus {
    int32_t signo, code, error;
    int16_t currentSignal;
    uint16_t padding;
    uint64_t pending, held;
    int32_t pid, ppid, pgrp, sid;
    int64_t times[8];
    uint64_t registers[27];
    int32_t fpValid, tail;
};
static_assert(sizeof(PrStatus) == 336);
// Linux x86-64 process-note layout; fixed widths and padding are intentional.
struct PrPsInfo {
    char state, stateName, zombie, nice;
    uint32_t padding;
    uint64_t flags;
    uint32_t uid, gid;
    int32_t pid, ppid, pgrp, sid;
    char name[16], arguments[80];
};
static_assert(sizeof(PrPsInfo) == 136);
// 128-byte ELF signal note. Preserve the standard placement of signo, errno,
// code and fault address independently of our richer FaultRecord.
struct LinuxSignalInfo {
    int32_t signo, error, code, padding;
    uint64_t address;
    uint8_t rest[104];
};
// Append-only writer for the reserved PT_NOTE region. used is an output-file
// offset, not a virtual address in the crashed process.
struct Notes {
    uint8_t* data;
    uint64_t used = noteOffset;
    // ELF note headers store unpadded lengths, but name/descriptor storage is
    // four-byte aligned. Check total space first so an omitted note cannot
    // partially overwrite another region of the snapshot.
    bool add(uint32_t type, const void* descriptor, uint32_t bytes,
             const char* name = "CORE", uint32_t namesz = 5) {
        const uint64_t total = 12 + align(namesz, 4) + align(bytes, 4);
        if (used + total > dataOffset) return false;
        const uint32_t header[3] = {namesz, bytes, type};
        copy(data + used, header, sizeof(header));
        copy(data + used + 12, name, namesz);
        copy(data + used + 12 + align(namesz, 4), descriptor, bytes);
        used += total;
        return true;
    }
};
// Map the native saved continuation into Linux gregset ordering. Segment
// selectors are known user values; orig_rax is unavailable and marked -1.
void registers(const SyscallContext& s, uint64_t* r) {
    r[0]  = s.r15;
    r[1]  = s.r14;
    r[2]  = s.r13;
    r[3]  = s.r12;
    r[4]  = s.rbp;
    r[5]  = s.rbx;
    r[6]  = s.r11;
    r[7]  = s.r10;
    r[8]  = s.r9;
    r[9]  = s.r8;
    r[10] = s.rax;
    r[11] = s.rcx;
    r[12] = s.rdx;
    r[13] = s.rsi;
    r[14] = s.rdi;
    r[15] = ~0ULL;
    r[16] = s.userRip;
    r[17] = 0x23;
    r[18] = s.userRflags;
    r[19] = s.userRsp;
    r[20] = 0x1b;
}
// Core tickets are restricted to the fixed Task service role, not any process
// that happens to possess a task-kernel endpoint.
bool writerAllowed(Task& caller) {
    return caller.serviceRole() == os::ServiceRole::Task;
}
// These states can still change the eventual WCOREDUMP result. Waiters must
// not report final child status while one remains within its deadline.
bool busy(os::wire::CoreState state) {
    return state == os::wire::CoreState::Requested ||
           state == os::wire::CoreState::Capturing ||
           state == os::wire::CoreState::Ready ||
           state == os::wire::CoreState::Writing;
}
}  // namespace

// Shared bounded snapshot manager; no filesystem object is stored here.
CoreDump& coreDump() {
    static CoreDump instance;
    return instance;
}
// Create an independent expiry worker so a dead or blocked user writer cannot
// keep snapshots and parent waits alive indefinitely.
void CoreDump::init() {
    watchdogReady = Thread::create(
                        nullptr,
                        [](void* context) {
                            auto& instance = *static_cast<CoreDump*>(context);
                            while (true) {
                                instance.expire();
                                sleepCurrentThread(100);
                            }
                        },
                        this) != nullptr;
}
// Fault-time path records policy/deadline only. Do not allocate/copy process
// memory here: other threads may still run and the fault context may not
// permit deep allocation or filesystem work.
void CoreDump::request(Task& task) {
    if (!task::hasCoreDefault(task.exitSignal_)) return;
    // Fault entry only records a bounded request. Allocation and copying
    // happen after quiescence.
    if (!__atomic_load_n(&policy.enabled, __ATOMIC_ACQUIRE) ||
        !task.dumpable() || !task.coreLimit_) {
        task.coreState_ = os::wire::CoreState::Skipped;
        return;
    }
    task.coreDeadline_ =
        time::getMonotonicTime() +
        __atomic_load_n(&policy.timeoutMilliseconds, __ATOMIC_RELAXED) *
            1000000;
    __atomic_store_n(&task.coreState_, os::wire::CoreState::Requested,
                     __ATOMIC_RELEASE);
}
// Read the in-progress state and turn expired work into TimedOut. This
// predicate can advance status, but freeing occupied slots is expire's job.
bool CoreDump::pending(Task& task) {
    const auto state = __atomic_load_n(&task.coreState_, __ATOMIC_ACQUIRE);
    if (busy(state) && time::getMonotonicTime() >= task.coreDeadline_) {
        __atomic_store_n(&task.coreState_, os::wire::CoreState::TimedOut,
                         __ATOMIC_RELEASE);
        return false;
    }
    return busy(state);
}
// Reclaimer entry after CPU quiescence and device shutdown notification, while
// the old root and thread objects still exist. Reserve one bounded slot and
// retain Task before copying. The resulting independent ELF buffer allows
// original mappings to be released without waiting for disk I/O.
void CoreDump::capture(Task& task) {
    if (!pending(task) || task.coreState_ != os::wire::CoreState::Requested)
        return;
    Slot* slot = nullptr;
    uint64_t limit;
    {
        InterruptSpinLockGuard guard(lock);
        unsigned used = 0;
        for (auto& s : slots)
            if (s.occupied()) ++used;
        if (used < policy.maximumConcurrent)
            for (auto& s : slots)
                if (!s.task) {
                    slot = &s;
                    break;
                }
        if (!slot) {
            task.coreState_ = os::wire::CoreState::Skipped;
            return;
        }
        limit = task.coreLimit_ < policy.maximumBytes ? task.coreLimit_
                                                      : policy.maximumBytes;
        if (limit < dataOffset + 4096) {
            task.coreState_ = os::wire::CoreState::Skipped;
            return;
        }
        // Publish capturing=true with the retained task so expiry cannot free
        // a buffer still being assembled outside the manager lock.
        task.retain();
        slot->task      = &task;
        slot->capturing = true;
        slot->ticket    = {nextTicket++,
                           task.id(),
                           task.startedNanoseconds(),
                           0,
                           task.coreDeadline_,
                           task.exitSignal_,
                           0};
        task.coreState_ = os::wire::CoreState::Capturing;
    }
    auto* data = static_cast<uint8_t*>(mm::allocateHeap(limit));
    if (data) {
        for (uint64_t i = 0; i < dataOffset; ++i) data[i] = 0;
        auto* header           = reinterpret_cast<os::ElfHeader*>(data);
        const uint8_t ident[8] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
        copy(header->identity, ident, sizeof(ident));
        header->type          = 4;
        header->machine       = 62;
        header->version       = 1;
        header->size          = sizeof(*header);
        header->programOffset = sizeof(*header);
        header->programSize   = sizeof(os::ElfProgramHeader);
        header->programCount  = 1;
        auto* ph =
            reinterpret_cast<os::ElfProgramHeader*>(data + sizeof(*header));
        Notes notes{data};
        LinuxSignalInfo info{
            static_cast<int32_t>(task.exitSignal_),
            0,
            task.exitFault_.code,
            0,
            task.exitFault_.addressValid ? task.exitFault_.address : 0,
            {}};
        notes.add(0x53494749, &info, sizeof(info));
        PrPsInfo process{};
        process.stateName = 'Z';
        process.zombie    = 1;
        process.uid       = task.uid();
        process.gid       = task.gid();
        process.pid       = task.id();
        process.ppid      = task.parentId();
        process.pgrp      = task.taskGroupId();
        process.sid       = task.sessionId();
        for (unsigned i = 0; i < 15 && task.name()[i]; ++i)
            process.name[i] = task.name()[i];
        for (unsigned i = 0; i < 79 && i < task.commandLineSize(); ++i)
            process.arguments[i] =
                task.commandLine()[i] ? task.commandLine()[i] : ' ';
        notes.add(3, &process, sizeof(process));
        // NT_FILE places mapping triples immediately before packed
        // NUL-terminated paths. Paths start after the actual count, not after
        // all 64 reserved slots, so the file layout does not expose the
        // in-memory struct reservation.
        struct FileNotes {
            uint64_t count, pageSize, ranges[64][3];
            char paths[64 * 192];
        } files{};
        files.count    = task.imageMappingCount_;
        files.pageSize = 4096;
        // NT_FILE stores triples followed immediately by the NUL-terminated
        // paths.
        auto* paths =
            reinterpret_cast<char*>(&files.ranges[task.imageMappingCount_]);
        uint32_t pathBytes = 0;
        for (uint32_t i = 0; i < task.imageMappingCount_; ++i) {
            const auto& image  = task.imageMappings_[i];
            files.ranges[i][0] = image.begin;
            files.ranges[i][1] = image.end;
            files.ranges[i][2] = image.offset / 4096;
            for (unsigned c = 0; c < sizeof(image.path); ++c) {
                paths[pathBytes++] = image.path[c];
                if (!image.path[c]) break;
            }
        }
        if (files.count)
            notes.add(0x46494c45, &files, 16 + files.count * 24 + pathBytes);
        unsigned threads = 0;
        // GDB selects the first PRSTATUS as the faulting thread. Emit the
        // faulting thread first because debuggers select the first status note
        // as the initial thread. Other threads contribute their most recently
        // saved user context rather than a kernel wait instruction pointer.
        for (unsigned pass = 0; pass < 2; ++pass)
            for (Thread* t = task.threads_; t; t = t->getTaskNext()) {
                if ((t->getId() == task.exitFault_.tid) != (pass == 0))
                    continue;
                if (threads++ >= maximumThreads) {
                    slot->ticket.truncated = 1;
                    break;
                }
                PrStatus status{};
                status.signo                  = task.exitSignal_;
                status.code                   = task.exitFault_.code;
                status.currentSignal          = task.exitSignal_;
                status.pid                    = t->getId();
                status.ppid                   = task.parentId();
                status.pgrp                   = task.taskGroupId();
                status.sid                    = task.sessionId();
                status.held                   = t->getSignalMask();
                status.fpValid                = 1;
                const SyscallContext& context = t->syscallContext()
                                                    ? *t->syscallContext()
                                                    : t->lastUserContext;
                registers(context, status.registers);
                if (!notes.add(1, &status, sizeof(status)) ||
                    !notes.add(2, t->fpState(), 512)) {
                    slot->ticket.truncated = 1;
                    break;
                }
            }
        uint64_t size = dataOffset, cursor = 0, address = 0, flags = 0;
        {
            PreemptionSpinLockGuard guard(task.addressSpace().lock());
            const uint64_t root = task.addressSpace().rootPhysicalAddress();
            while (mm::nextDumpPage(root, cursor, address, flags)) {
                // Never read hardware mappings merely because a page table
                // contains them. Only pager-managed external RAM explicitly
                // admitted by Maps is dumpable; MMIO/framebuffer/DMA reads
                // could cause device effects or leak unrelated data.
                if ((flags & ::kernel::mm::pageExternal) &&
                    !mm::maps().dumpAllowed(task, address)) {
                    cursor = address + 4096;
                    continue;
                }
                if (size + 4096 > limit ||
                    header->programCount >= maximumLoads ||
                    time::getMonotonicTime() >= task.coreDeadline_) {
                    slot->ticket.truncated = 1;
                    break;
                }
                if (!mm::copyFromAddressSpace(data + size, root, address,
                                              4096)) {
                    slot->ticket.truncated = 1;
                    break;
                }
                // Convert page permissions to ELF flags and merge only
                // adjacent pages with the same flags. File offsets remain
                // contiguous even when virtual regions have holes, so each new
                // region gets its own PT_LOAD.
                const uint32_t elfFlags =
                    4 | ((flags & ::kernel::mm::pageWritable) ? 2 : 0) |
                    ((flags & ::kernel::mm::pageNoExecute) ? 0 : 1);
                auto& last = ph[header->programCount - 1];
                if (last.type == 1 &&
                    last.address + last.memorySize == address &&
                    last.flags == elfFlags) {
                    last.fileSize += 4096;
                    last.memorySize += 4096;
                } else
                    ph[header->programCount++] = {1, elfFlags, size, address,
                                                  0, 4096,     4096, 4096};
                size += 4096;
                cursor = address + 4096;
            }
        }
        // Native fault detail supplements standard notes without changing
        // their Linux layouts.
        notes.add(0x4f530001, &task.exitFault_, sizeof(task.exitFault_),
                  "OSINFO", 7);
        const uint64_t bounds[] = {1,    slot->ticket.truncated, limit,
                                   size, maximumThreads,         maximumLoads};
        notes.add(0x4f530002, bounds, sizeof(bounds), "OSINFO", 7);
        const uint64_t auxv[] = {6, 4096, 0, 0};
        if (task.auxiliary_[0])
            notes.add(6, task.auxiliary_, sizeof(task.auxiliary_));
        else
            notes.add(6, auxv, sizeof(auxv));
        ph[0] = {4, 0, noteOffset, 0, 0, notes.used - noteOffset, 0, 4};
        slot->ticket.bytes = size;
    }
    {
        InterruptSpinLockGuard guard(lock);
        slot->data      = data;
        slot->capturing = false;
        task.coreState_ = time::getMonotonicTime() >= task.coreDeadline_
                              ? os::wire::CoreState::TimedOut
                          : data ? os::wire::CoreState::Ready
                                 : os::wire::CoreState::Failed;
    }
    expire();
}
// Global core policy read consistently under the manager lock.
void CoreDump::query(os::wire::CorePolicy& result) {
    InterruptSpinLockGuard guard(lock);
    result = policy;
}

// Only the Task writer role may set bounded policy, and expiry infrastructure
// must exist before enabling snapshots. Publish timing before enabled so a new
// request observes an initialized policy rather than a partial update.
int CoreDump::configure(Task& caller, const os::wire::CorePolicy& value) {
    if (!writerAllowed(caller)) return -3;
    if (!watchdogReady) return -5;
    if (value.enabled > 1 || value.maximumBytes > maximumSnapshot ||
        value.maximumConcurrent > maximumSlots || !value.maximumConcurrent ||
        value.timeoutMilliseconds < 100 || value.timeoutMilliseconds > 60000)
        return -1;
    InterruptSpinLockGuard guard(lock);
    policy.maximumBytes      = value.maximumBytes;
    policy.maximumConcurrent = value.maximumConcurrent;
    __atomic_store_n(&policy.timeoutMilliseconds, value.timeoutMilliseconds,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&policy.enabled, value.enabled, __ATOMIC_RELEASE);
    return 0;
}
// Claim a Ready snapshot for one writer PID and return its unique ticket.
// Another writer cannot read/finish it merely by guessing that ticket ID.
int CoreDump::acquire(Task& caller, os::wire::CoreTicket& ticket) {
    if (!writerAllowed(caller)) return -3;
    expire();
    InterruptSpinLockGuard guard(lock);
    for (auto& s : slots)
        if (s.task && s.data &&
            s.task->coreState_ == os::wire::CoreState::Ready) {
            s.writer           = caller.id();
            s.task->coreState_ = os::wire::CoreState::Writing;
            ticket             = s.ticket;
            return 0;
        }
    return -6;
}
// Copy only within the ticket-owned immutable snapshot and before deadline.
// Return bytes copied or a negative error. The public service bounds a chunk
// to 4096 bytes and performs the final copy into the writer's user mapping.
int CoreDump::read(Task& caller, uint64_t id, uint64_t offset,
                   void* destination, uint64_t size) {
    if (!writerAllowed(caller)) return -3;
    InterruptSpinLockGuard guard(lock);
    for (auto& s : slots)
        if (s.task && s.data && s.ticket.id == id && s.writer == caller.id() &&
            s.task->coreState_ == os::wire::CoreState::Writing) {
            if (offset > s.ticket.bytes || size > s.ticket.bytes - offset ||
                time::getMonotonicTime() >= s.ticket.deadline)
                return -1;
            copy(destination, s.data + offset, size);
            return size;
        }
    return -4;
}
// Validate writer/ticket and deadline, then distinguish actual write success
// from snapshot truncation or failure. Release memory under the manager lock,
// notify the parent after unlocking, and finally drop the slot's Task ref. A
// requested core alone must never become CLD_DUMPED.
int CoreDump::finish(Task& caller, uint64_t id, bool success) {
    if (!writerAllowed(caller)) return -3;
    Task* task    = nullptr;
    bool accepted = false;
    {
        InterruptSpinLockGuard guard(lock);
        for (auto& s : slots)
            if (s.task && s.ticket.id == id && s.writer == caller.id()) {
                task = s.task;
                const bool valid =
                    time::getMonotonicTime() < s.ticket.deadline &&
                    task->coreState_ == os::wire::CoreState::Writing;
                accepted         = valid;
                task->coreState_ = !valid     ? os::wire::CoreState::TimedOut
                                   : !success ? os::wire::CoreState::Failed
                                   : s.ticket.truncated
                                       ? os::wire::CoreState::Truncated
                                       : os::wire::CoreState::Complete;
                mm::freeHeap(s.data);
                s = {};
                break;
            }
    }
    if (!task) return -4;
    auto parent = task->parentReference();
    if (parent) parent->notifyChildExit(*task);
    task->release();
    return accepted ? 0 : -9;
}
// Skip active capture to avoid freeing its scratch buffer. For finished or
// expired work, detach each slot under lock, then notify and release Task
// outside it; callbacks can themselves query task/core state.
void CoreDump::expire() {
    for (unsigned i = 0; i < maximumSlots; ++i) {
        Task* task = nullptr;
        {
            InterruptSpinLockGuard guard(lock);
            auto& s = slots[i];
            if (!s.task || s.capturing) continue;
            if (pending(*s.task) &&
                s.task->coreState_ != os::wire::CoreState::Failed)
                continue;
            task = s.task;
            mm::freeHeap(s.data);
            s = {};
        }
        auto parent = task->parentReference();
        if (parent) parent->notifyChildExit(*task);
        task->release();
    }
}

// Keep the first fatal signal and original fault even if other threads fail
// during shutdown. Without explicit fault data, capture the current saved user
// continuation and FP state before requesting bounded core work.
void Task::recordSignalExit(uint32_t signo, const FaultRecord* fault) {
    InterruptSpinLockGuard guard(lock_);
    if (exitSignal_) return;
    exitSignal_ = signo;
    exitCode_   = -static_cast<int>(signo);
    if (fault)
        exitFault_ = *fault;
    else {
        exitFault_.pid       = id_;
        exitFault_.signal    = signo;
        exitFault_.started   = startedNanoseconds_;
        exitFault_.timestamp = time::getMonotonicTime();
        Thread* current      = currentThread();
        if (current && current->getTask() == this) {
            current->saveFloatingPointState();
            exitFault_.tid = current->getId();
            if (current->syscallContext())
                current->lastUserContext = *current->syscallContext();
            exitFault_.rip = current->lastUserContext.userRip;
            exitFault_.rsp = current->lastUserContext.userRsp;
        }
    }
    coreDump().request(*this);
}

// Copy diagnostics and refreshed timeout state under the Task lock. Signal
// identity is separate from the compatibility negative exitCode value.
void Task::getExitInfo(os::wire::ExitInfo& info) {
    InterruptSpinLockGuard guard(lock_);
    coreDump().pending(*this);
    info = {exitSignal_,         static_cast<uint32_t>(coreState_),
            exitFault_.tid,      exitFault_.vector,
            exitFault_.error,    exitFault_.rip,
            exitFault_.rsp,      exitFault_.address,
            startedNanoseconds_, exitFault_.addressValid,
            exitFault_.code};
}

// Append a bounded loader-provided file mapping for NT_FILE. The service
// validates range/alignment/path before this internal bookkeeping method.
bool Task::recordImage(const os::wire::ImageMapping& mapping) {
    InterruptSpinLockGuard guard(lock_);
    if (imageMappingCount_ == 64) return false;
    imageMappings_[imageMappingCount_++] = mapping;
    return true;
}

// Store the ELF auxiliary-vector pairs needed by debuggers: program headers,
// header size/count, loader base, entry and page size, followed by AT_NULL.
void Task::recordAuxiliary(uint64_t headers, uint64_t count, uint64_t entry,
                           uint64_t loader) {
    InterruptSpinLockGuard guard(lock_);
    const uint64_t values[14] = {3,      headers, 4,     56, 5,    count, 7,
                                 loader, 9,       entry, 6,  4096, 0,     0};
    for (unsigned i = 0; i < 14; ++i) auxiliary_[i] = values[i];
}

}  // namespace kernel::task
