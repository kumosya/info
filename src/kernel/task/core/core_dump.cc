/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/core/core_dump.cc
 * 捕获 ELF 核心转储，管理写入票据、大小限制和超时清理。
 */

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include <os/elf.h>

namespace task {

    // 预先限制快照、线程说明和程序段头数量；转储字节预算还要容纳固定元数据和至少一个数据页。
    constexpr uint64_t maximumSnapshot = 16 * 1024 * 1024;
    constexpr unsigned maximumSlots = 2, maximumLoads = 256,
                       maximumThreads = 64;
    constexpr uint64_t noteOffset = 16384, dataOffset = 81920;

    /*
     * 复制固定大小的转储数据，供构造文件头和记录使用。
     */
    static void copy(void* destination, const void* source, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            static_cast<uint8_t*>(destination)[i] =
                static_cast<const uint8_t*>(source)[i];
        }
    }

    /*
     * 把长度向上补齐到指定边界，保证转储文件中的记录满足格式对齐要求。
     */
    static uint64_t align(uint64_t n, uint64_t a) {
        return (n + a - 1) & ~(a - 1);
    }

    // 布局与 Linux x86-64
    // 的线程状态说明一致，包括明确的填充；数据直接写给调试器读取，不能随意调整字段。
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

    // 进程说明使用固定宽度及填充，保持 Linux x86-64 调试格式兼容。
    struct PrPsInfo {
            char state, stateName, zombie, nice;
            uint32_t padding;
            uint64_t flags;
            uint32_t uid, gid;
            int32_t pid, ppid, pgrp, sid;
            char name[16], arguments[80];
    };

    static_assert(sizeof(PrPsInfo) == 136);

    // ELF 信号说明固定为 128
    // 字节，信号号、错误号、代码和故障地址位置不能随内部故障记录变化。
    struct LinuxSignalInfo {
            int32_t signo, error, code, padding;
            uint64_t address;
            uint8_t rest[104];
    };

    // 向预留说明区顺序追加数据，used 表示输出文件偏移，不是故障进程的虚拟地址。
    struct Notes {
            uint8_t* data;
            uint64_t used = noteOffset;

            /*
             * 向说明区追加一条带名称和类型的数据记录。
             * 先检查容量，再写入记录头和按要求对齐的数据。
             */
            bool add(uint32_t type, const void* descriptor, uint32_t bytes,
                     const char* name = "CORE", uint32_t namesz = 5) {
                const uint64_t total = 12 + align(namesz, 4) + align(bytes, 4);

                if (used + total > dataOffset) {
                    return false;
                }

                const uint32_t header[3] = {namesz, bytes, type};
                copy(data + used, header, sizeof(header));
                copy(data + used + 12, name, namesz);
                copy(data + used + 12 + align(namesz, 4), descriptor, bytes);
                used += total;

                return true;
            }
    };

    /*
     * 把系统调用现场中的寄存器放进转储约定的顺序，使调试器能还原故障线程状态。
     */
    static void registers(const SyscallContext& s, uint64_t* r) {
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

    /*
     * 检查调用者是否有资格作为转储写入服务读取任务内存。
     */
    static bool writerAllowed(Task& caller) {
        return caller.serviceRole() == os::ServiceRole::Task;
    }

    /*
     * 判断转储是否仍在准备或写出中，用来决定退出通知能否继续以及请求是否超时。
     */
    static bool busy(os::wire::CoreState state) {
        return state == os::wire::CoreState::Requested ||
               state == os::wire::CoreState::Capturing ||
               state == os::wire::CoreState::Ready ||
               state == os::wire::CoreState::Writing;
    }

    /*
     * 返回全局核心转储管理器。
     */
    CoreDump& coreDump() {
        static CoreDump instance;

        return instance;
    }

    /*
     * 初始化转储锁和后台超时检查线程，避免写入服务失联后任务永久等待。
     */
    void CoreDump::init() {
        watchdogReady = Thread::create(
                            nullptr,
                            [](void* context) {
                                CoreDump& instance =
                                    *static_cast<CoreDump*>(context);
                                while (true) {
                                    instance.expire();
                                    sleepCurrentThread(100);
                                }
                            },
                            this) != nullptr;
    }

    /*
     * 登记任务的转储请求和截止时间。
     * 先检查策略及任务是否允许转储，再进入等待捕获状态。
     */
    void CoreDump::request(Task& task) {
        if (!task::hasCoreDefault(task.exitSignal_)) {
            return;
        }

        // 异常入口只登记有界请求，等任务停止执行后再分配缓冲区和复制内存。
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

    /*
     * 查询任务转储是否尚未完成，超过截止时间时把请求改为超时，使退出流程能够继续。
     */
    bool CoreDump::pending(Task& task) {
        const os::wire::CoreState state =
            __atomic_load_n(&task.coreState_, __ATOMIC_ACQUIRE);

        if (busy(state) && time::getMonotonicTime() >= task.coreDeadline_) {
            __atomic_store_n(&task.coreState_, os::wire::CoreState::TimedOut,
                             __ATOMIC_RELEASE);

            return false;
        }

        return busy(state);
    }

    /*
     * 捕获任务的线程现场和可导出的用户页，组织成 ELF 核心转储数据。
     * 受大小上限约束，无法全部收集时记录截断状态。
     */
    void CoreDump::capture(Task& task) {
        if (!pending(task) ||
            task.coreState_ != os::wire::CoreState::Requested) {
            return;
        }

        Slot* slot = nullptr;
        uint64_t limit;
        {
            InterruptSpinLockGuard guard(lock);

            // 先计算当前并发转储数量，只有策略允许且存在空槽时才认领本次任务。
            unsigned used = 0;

            for (Slot& s : slots) {
                if (s.occupied()) {
                    ++used;
                }
            }

            if (used < policy.maximumConcurrent) {
                for (Slot& s : slots) {
                    if (!s.task) {
                        slot = &s;
                        break;
                    }
                }
            }

            if (!slot) {
                task.coreState_ = os::wire::CoreState::Skipped;

                return;
            }

            // 同时受任务限额和系统策略限制，最终预算取较小值，并须容纳固定头部与至少一页数据。
            limit = task.coreLimit_ < policy.maximumBytes ? task.coreLimit_
                                                          : policy.maximumBytes;

            if (limit < dataOffset + 4096) {
                task.coreState_ = os::wire::CoreState::Skipped;

                return;
            }

            // 在锁内把保留的任务与正在捕获标志一起发布，防止超时路径释放锁外仍在组装的缓冲区。
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

        // 大缓冲区分配放在管理锁外，槽位已标记正在捕获，超时扫描不会误释放正在组装的内容。
        uint8_t* data = static_cast<uint8_t*>(mm::allocateHeap(limit));

        if (data) {
            for (uint64_t i = 0; i < dataOffset; ++i) {
                data[i] = 0;
            }

            // 先构造 ELF
            // 核心文件头和说明段，调试器据此找到寄存器信息与后续内存数据。
            os::ElfHeader* header  = reinterpret_cast<os::ElfHeader*>(data);
            const uint8_t ident[8] = {0x7f, 'E', 'L', 'F', 2, 1, 1, 0};
            copy(header->identity, ident, sizeof(ident));
            header->type          = 4;
            header->machine       = 62;
            header->version       = 1;
            header->size          = sizeof(*header);
            header->programOffset = sizeof(*header);
            header->programSize   = sizeof(os::ElfProgramHeader);
            header->programCount  = 1;
            os::ElfProgramHeader* ph =
                reinterpret_cast<os::ElfProgramHeader*>(data + sizeof(*header));
            Notes notes{data};

            // 标准信号说明与本机故障详情分别保存，既兼容调试器，也保留内核收集的额外线索。
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

            for (unsigned i = 0; i < 15 && task.name()[i]; ++i) {
                process.name[i] = task.name()[i];
            }

            for (unsigned i = 0; i < 79 && i < task.commandLineSize(); ++i) {
                process.arguments[i] =
                    task.commandLine()[i] ? task.commandLine()[i] : ' ';
            }

            notes.add(3, &process, sizeof(process));

            // 文件映射三元组之后紧接实际路径字符串；路径起点按实际记录数计算，不能把未使用的
            // 64 个预留槽也写入文件布局。
            struct FileNotes {
                    uint64_t count, pageSize, ranges[64][3];
                    char paths[64 * 192];
            } files{};

            files.count    = task.imageMappingCount_;
            files.pageSize = 4096;
            // NT_FILE 的三元组之后立即保存以空字符结尾的路径。
            char* paths =
                reinterpret_cast<char*>(&files.ranges[task.imageMappingCount_]);
            uint32_t pathBytes = 0;

            for (uint32_t i = 0; i < task.imageMappingCount_; ++i) {
                const os::wire::ImageMapping& image = task.imageMappings_[i];
                files.ranges[i][0]                  = image.begin;
                files.ranges[i][1]                  = image.end;
                files.ranges[i][2]                  = image.offset / 4096;

                for (unsigned c = 0; c < sizeof(image.path); ++c) {
                    paths[pathBytes++] = image.path[c];

                    if (!image.path[c]) {
                        break;
                    }
                }
            }

            if (files.count) {
                notes.add(0x46494c45, &files,
                          16 + files.count * 24 + pathBytes);
            }

            unsigned threads = 0;
            // 调试器通常选第一条线程状态作为故障线程，因此先写故障线程；其他线程使用最近保存的用户现场，不能写成内核等待地址。
            for (unsigned pass = 0; pass < 2; ++pass) {
                // 各线程使用最近保存的用户现场，不拿内核等待函数地址冒充用户故障位置。
                for (Thread* t = task.threads_; t; t = t->getTaskNext()) {
                    if ((t->getId() == task.exitFault_.tid) != (pass == 0)) {
                        continue;
                    }

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
            }

            uint64_t size = dataOffset, cursor = 0, address = 0, flags = 0;
            {
                PreemptionSpinLockGuard guard(task.addressSpace().lock());
                const uint64_t root = task.addressSpace().rootPhysicalAddress();

                // 按地址逐页收集可导出的内存，并在容量限制内组织程序段，不能无限扩展转储缓冲区。
                while (mm::nextDumpPage(root, cursor, address, flags)) {
                    // 不能因页表存在就读取硬件映射；只允许登记表明确认可的内存服务页面，读取设备寄存器或
                    // DMA 区可能产生副作用或泄露无关数据。
                    if ((flags & ::mm::pageExternal) &&
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

                    // 把页面权限转成 ELF
                    // 标志，仅合并虚拟地址相邻且权限一致的页；虚拟地址存在空洞时另建程序段，文件数据仍连续保存。
                    const uint32_t elfFlags =
                        4 | ((flags & ::mm::pageWritable) ? 2 : 0) |
                        ((flags & ::mm::pageNoExecute) ? 0 : 1);
                    os::ElfProgramHeader& last = ph[header->programCount - 1];

                    if (last.type == 1 &&
                        last.address + last.memorySize == address &&
                        last.flags == elfFlags) {
                        last.fileSize += 4096;
                        last.memorySize += 4096;
                    } else {
                        ph[header->programCount++] = {
                            1, elfFlags, size, address, 0, 4096, 4096, 4096};
                    }

                    size += 4096;
                    cursor = address + 4096;
                }
            }

            // 本机故障详情作为额外说明追加，不改变标准 Linux 说明记录的布局。
            notes.add(0x4f530001, &task.exitFault_, sizeof(task.exitFault_),
                      "OSINFO", 7);
            const uint64_t bounds[] = {
                1,    slot->ticket.truncated, limit,
                size, maximumThreads,         maximumLoads};
            notes.add(0x4f530002, bounds, sizeof(bounds), "OSINFO", 7);
            const uint64_t auxv[] = {6, 4096, 0, 0};

            if (task.auxiliary_[0]) {
                notes.add(6, task.auxiliary_, sizeof(task.auxiliary_));
            } else {
                notes.add(6, auxv, sizeof(auxv));
            }

            ph[0] = {4, 0, noteOffset, 0, 0, notes.used - noteOffset, 0, 4};
            slot->ticket.bytes = size;
        }

        {
            InterruptSpinLockGuard guard(lock);

            // 捕获内容准备完成后才发布缓冲区和可领取状态，写入服务不能读到尚在修改中的数据。
            slot->data      = data;
            slot->capturing = false;
            task.coreState_ = time::getMonotonicTime() >= task.coreDeadline_
                                  ? os::wire::CoreState::TimedOut
                              : data ? os::wire::CoreState::Ready
                                     : os::wire::CoreState::Failed;
        }

        expire();
    }

    /*
     * 在锁内复制当前转储策略，供管理服务查询。
     */
    void CoreDump::query(os::wire::CorePolicy& result) {
        InterruptSpinLockGuard guard(lock);
        result = policy;
    }

    /*
     * 校验管理权限和策略参数，再整体更新转储配置，拒绝不合理的大小和时间上限。
     */
    int CoreDump::configure(Task& caller, const os::wire::CorePolicy& value) {
        if (!writerAllowed(caller)) {
            return -3;
        }

        if (!watchdogReady) {
            return -5;
        }

        if (value.enabled > 1 || value.maximumBytes > maximumSnapshot ||
            value.maximumConcurrent > maximumSlots ||
            !value.maximumConcurrent || value.timeoutMilliseconds < 100 ||
            value.timeoutMilliseconds > 60000) {
            return -1;
        }

        InterruptSpinLockGuard guard(lock);
        policy.maximumBytes      = value.maximumBytes;
        policy.maximumConcurrent = value.maximumConcurrent;
        __atomic_store_n(&policy.timeoutMilliseconds, value.timeoutMilliseconds,
                         __ATOMIC_RELEASE);
        __atomic_store_n(&policy.enabled, value.enabled, __ATOMIC_RELEASE);

        return 0;
    }

    /*
     * 为有权限的写入者领取一份待写转储，记录领取者身份并返回票据，避免多个写入者重复处理同一份数据。
     */
    int CoreDump::acquire(Task& caller, os::wire::CoreTicket& ticket) {
        if (!writerAllowed(caller)) {
            return -3;
        }

        // 领取之前先处理过期槽，释放不再有效的占位，避免旧失败请求阻挡新转储。
        expire();
        InterruptSpinLockGuard guard(lock);

        for (Slot& s : slots) {
            if (s.task && s.data &&
                s.task->coreState_ == os::wire::CoreState::Ready) {
                // 在同一管理锁下把槽标成正在写并登记领取任务，其他写入者不能再领取同一份内容。
                s.writer           = caller.id();
                s.task->coreState_ = os::wire::CoreState::Writing;
                ticket             = s.ticket;

                return 0;
            }
        }

        return -6;
    }

    /*
     * 按票据和偏移读取已捕获数据。
     * 检查领取者、范围和票据有效性，避免跨任务读取或越界。
     */
    int CoreDump::read(Task& caller, uint64_t id, uint64_t offset,
                       void* destination, uint64_t size) {
        if (!writerAllowed(caller)) {
            return -3;
        }

        InterruptSpinLockGuard guard(lock);

        for (Slot& s : slots) {
            if (s.task && s.data && s.ticket.id == id &&
                s.writer == caller.id() &&
                s.task->coreState_ == os::wire::CoreState::Writing) {
                // 用总长减偏移检查长度，避免偏移加长度溢出，同时拒绝超过票据有效期的读取。
                if (offset > s.ticket.bytes || size > s.ticket.bytes - offset ||
                    time::getMonotonicTime() >= s.ticket.deadline) {
                    return -1;
                }

                // 验证票据、领取者和范围都通过以后，只复制捕获缓冲区中的指定片段。
                copy(destination, s.data + offset, size);

                return size;
            }
        }

        return -4;
    }

    /*
     * 提交转储写出结果，释放缓冲区并更新任务转储状态，再推动之前延后的退出通知。
     */
    int CoreDump::finish(Task& caller, uint64_t id, bool success) {
        if (!writerAllowed(caller)) {
            return -3;
        }

        Task* task    = nullptr;
        bool accepted = false;
        {
            InterruptSpinLockGuard guard(lock);

            for (Slot& s : slots) {
                if (s.task && s.ticket.id == id && s.writer == caller.id()) {
                    task = s.task;

                    // 写入成功报告仍要核对截止时间和当前阶段，不能让迟到结果把已超时任务改回成功。
                    const bool valid =
                        time::getMonotonicTime() < s.ticket.deadline &&
                        task->coreState_ == os::wire::CoreState::Writing;
                    accepted         = valid;
                    task->coreState_ = !valid ? os::wire::CoreState::TimedOut
                                       : !success ? os::wire::CoreState::Failed
                                       : s.ticket.truncated
                                           ? os::wire::CoreState::Truncated
                                           : os::wire::CoreState::Complete;

                    // 在锁内撤销槽的所有权并释放数据，后续相同票据将无法再次命中。
                    mm::freeHeap(s.data);
                    s = {};
                    break;
                }
            }
        }

        if (!task) {
            return -4;
        }

        // 任务引用仍由本次局部变量保留，放管理锁后再通知父任务，避免跨模块持锁调用。
        ::task::TaskReference parent = task->parentReference();

        if (parent) {
            parent->notifyChildExit(*task);
        }

        // 父任务通知完成后才归还转储槽原本保留的引用，防止通知过程中任务对象失效。
        task->release();

        return accepted ? 0 : -9;
    }

    /*
     * 清理超时或无法继续的转储槽位，归还任务引用和缓冲区，防止写入端故障导致资源长期占用。
     */
    void CoreDump::expire() {
        for (unsigned i = 0; i < maximumSlots; ++i) {
            Task* task = nullptr;
            {
                InterruptSpinLockGuard guard(lock);
                Slot& s = slots[i];

                // 空槽和仍在锁外构造数据的槽都跳过；捕获者还在使用缓冲区时不能由超时线程抢先释放。
                if (!s.task || s.capturing) {
                    continue;
                }

                if (pending(*s.task) &&
                    s.task->coreState_ != os::wire::CoreState::Failed) {
                    continue;
                }

                // 在管理锁内摘下到期任务和缓冲区，留下局部引用承接后续父任务通知。
                task = s.task;
                mm::freeHeap(s.data);
                s = {};
            }

            ::task::TaskReference parent = task->parentReference();

            if (parent) {
                parent->notifyChildExit(*task);
            }

            task->release();
        }
    }

    /*
     * 记录导致任务退出的信号及可选故障现场，使 wait
     * 和核心转储看到同一个退出原因。
     */
    void Task::recordSignalExit(uint32_t signo, const FaultRecord* fault) {
        InterruptSpinLockGuard guard(lock_);

        if (exitSignal_) {
            return;
        }

        exitSignal_ = signo;
        exitCode_   = -static_cast<int>(signo);

        if (fault) {
            exitFault_ = *fault;
        } else {
            exitFault_.pid       = id_;
            exitFault_.signal    = signo;
            exitFault_.started   = startedNanoseconds_;
            exitFault_.timestamp = time::getMonotonicTime();
            Thread* current      = currentThread();

            if (current && current->getTask() == this) {
                current->saveFloatingPointState();
                exitFault_.tid = current->getId();

                if (current->syscallContext()) {
                    current->lastUserContext = *current->syscallContext();
                }

                exitFault_.rip = current->lastUserContext.userRip;
                exitFault_.rsp = current->lastUserContext.userRsp;
            }
        }

        coreDump().request(*this);
    }

    /*
     * 在任务锁内读取退出信号、转储状态和故障信息，填充对外的退出详情。
     */
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

    /*
     * 登记一个程序映像的地址范围和文件信息，供转储描述使用；记录表满时返回失败。
     */
    bool Task::recordImage(const os::wire::ImageMapping& mapping) {
        InterruptSpinLockGuard guard(lock_);

        if (imageMappingCount_ == 64) {
            return false;
        }

        imageMappings_[imageMappingCount_++] = mapping;

        return true;
    }

    /*
     * 保存程序头、入口和加载器等辅助信息，供生成转储中的辅助向量。
     */
    void Task::recordAuxiliary(uint64_t headers, uint64_t count, uint64_t entry,
                               uint64_t loader) {
        InterruptSpinLockGuard guard(lock_);
        const uint64_t values[14] = {3, headers, 4, 56,   5, count, 7, loader,
                                     9, entry,   6, 4096, 0, 0};

        for (unsigned i = 0; i < 14; ++i) {
            auxiliary_[i] = values[i];
        }
    }

}  // namespace task
