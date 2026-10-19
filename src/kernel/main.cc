#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/elf.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/core_service.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/time.h"


namespace {

constexpr uint64_t directMapBase      = 0xffff800000000000ULL;
constexpr uint64_t userProbeCode      = 0x0000000000400000ULL;
constexpr uint64_t userProbeStackBase = 0x0000000000800000ULL;
constexpr uint64_t pageSize           = 4096;

uint32_t schedulerProbeCount;
uint32_t ipcProbeResult;
uint32_t smpProbeCount;
kernel::Endpoint* ipcProbeEndpoint;

extern "C" uint8_t userSyscallProbeStart;
extern "C" uint8_t userSyscallProbeEnd;

kernel::Task* createSyscallProbe() {
    kernel::Task* task =
        kernel::Task::create("syscall-probe", kernel::task::getCurrent());
    if (task == nullptr) {
        kernel::serial::serialWrite("[SYSCALL] probe task creation failed\n");
        return nullptr;
    }
    const uint64_t codePhysical  = kernel::memory::allocatePhysicalPage();
    const uint64_t stackPhysical = kernel::memory::allocatePhysicalPage();
    const uint64_t codeSize =
        reinterpret_cast<uint64_t>(&userSyscallProbeEnd) -
        reinterpret_cast<uint64_t>(&userSyscallProbeStart);
    if (codePhysical == 0 || stackPhysical == 0) {
        kernel::serial::serialWrite("[SYSCALL] probe page allocation failed\n");
        return nullptr;
    }
    if (codeSize > pageSize) {
        kernel::serial::serialWrite("[SYSCALL] probe code exceeds one page\n");
        return nullptr;
    }
    if (!kernel::memory::mapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), userProbeCode,
            codePhysical, kernel::pageUser)) {
        kernel::serial::serialWrite("[SYSCALL] probe code mapping failed\n");
        return nullptr;
    }
    if (!kernel::memory::mapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), userProbeStackBase,
            stackPhysical,
            kernel::pageUser | kernel::pageWritable | kernel::pageNoExecute)) {
        kernel::serial::serialWrite("[SYSCALL] probe stack mapping failed\n");
        return nullptr;
    }
    uint8_t* code = reinterpret_cast<uint8_t*>(directMapBase + codePhysical);
    const uint8_t* source = &userSyscallProbeStart;
    for (uint64_t index = 0; index < pageSize; ++index) {
        code[index] = index < codeSize ? source[index] : 0;
    }
    uint8_t* stack = reinterpret_cast<uint8_t*>(directMapBase + stackPhysical);
    for (uint64_t index = 0; index < pageSize; ++index) {
        stack[index] = 0;
    }
    kernel::Thread* thread = kernel::Thread::createUser(
        task, userProbeCode, userProbeStackBase + pageSize);
    if (thread == nullptr) {
        kernel::serial::serialWrite("[SYSCALL] probe thread creation failed\n");
        return nullptr;
    }
    if (kernel::smp::getOnlineCpuCount() > 1 &&
        !thread->setAffinity(1ULL << 1)) {
        kernel::serial::serialWrite("[SYSCALL] probe thread affinity failed\n");
        return nullptr;
    }
    return task;
}

void schedulerProbe(void*) {
    // kernel::serial::serialWrite("[THREAD] probe ");
    // kernel::serial::serialWriteDecimal(reinterpret_cast<uint64_t>(argument));
    // kernel::serial::serialWrite(" running\n");
    kernel::schedule::yieldThread();
    __atomic_add_fetch(&schedulerProbeCount, 1U, __ATOMIC_RELAXED);
    // kernel::serial::serialWrite("[THREAD] probe completed\n");
}

void smpSchedulerProbe(void*) {
    kernel::serial::serialWrite("[SMP] scheduled probe on CPU ");
    kernel::serial::serialWriteDecimal(kernel::cpu::getCurrentId());
    kernel::serial::serialWrite("\n");
    __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
}

void ipcServerProbe(void*) {
    kernel::IpcMessage request{};
    request.length = 0xffffffffU;
    if (ipcProbeEndpoint->receive(&request) != 0 || request.type != 0x41 ||
        request.length != 1 || request.data[0] != 0x1234 ||
        request.senderTid == 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
        return;
    }
    kernel::IpcMessage reply{};
    reply.replyToken = request.replyToken;
    reply.type    = 0x42;
    reply.length  = 1;
    reply.data[0] = request.data[0] + 1;
    if (ipcProbeEndpoint->reply(&reply) != 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }
    // kernel::serial::serialWrite("[IPC] server probe completed\n");
}

void ipcClientProbe(void*) {
    kernel::IpcMessage request{};
    request.type    = 0x41;
    request.length  = 1;
    request.data[0] = 0x1234;
    kernel::IpcMessage reply{};
    if (ipcProbeEndpoint->call(&request, &reply) == 0 && reply.type == 0x42 &&
        reply.length == 1 && reply.data[0] == 0x1235) {
        __atomic_store_n(&ipcProbeResult, 1U, __ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }
    // kernel::serial::serialWrite("[IPC] client probe completed\n");
}

}  // namespace

namespace kernel {

void kernelMain(const BootInfo* bootInfo) {
    // serial::serialWrite("[KERNEL] kernelMain()\n");
    // serial::serialWrite("[KERNEL] arch initialized\n");
    memory::initPhy(bootInfo);
    memory::initVir(bootInfo);
    heap::init();
    volatile uint64_t* heapProbe =
        static_cast<volatile uint64_t*>(heap::kernelAllocate(sizeof(uint64_t)));
    if (heapProbe == nullptr) {
        serial::serialWrite("[MEMORY] kernel heap initialization failed\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    *heapProbe = 0x6f73696e666fULL;
    cpu::init();
    memory::MemoryTopology::enableCpuLocal();
    memory::PerCpuObjectCache::enableCpuLocal();
    time::init();
    interrupt::initIdt();
    interrupt::initControllers(bootInfo);
    device::init(bootInfo);
    smp::initSmp(bootInfo);
    Thread::init();
    schedule::init();
    task::init();
    sync::runSynchronizationSelfTest();
    syscall::init();
    smp::startApplicationTaskors();
    if (smp::getOnlineCpuCount() > 1) {
        Thread* smpProbe = Thread::create(nullptr, smpSchedulerProbe, nullptr);
        if (smpProbe == nullptr || !(smpProbe)->setAffinity(1ULL << 1)) {
            serial::serialWrite("[SMP] unable to create CPU affinity probe\n");
            cpu::halt();
        }
    } else {
        __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
    }
    Task* taskProbe = Task::create("address-space-probe", task::getCurrent());
    if (taskProbe == nullptr ||
        taskProbe->addressSpace().rootPhysicalAddress() ==
            memory::getKernelAddressSpaceRoot() ||
        !ElfLoader::runValidationSelfTest(*taskProbe)) {
        serial::serialWrite("[TASK] address-space self-test failed\n");
        cpu::halt();
    }

    taskProbe->destroy();
    Task* syscallProbe = createSyscallProbe();
    if (syscallProbe == nullptr) {
        serial::serialWrite("[SYSCALL] unable to create Ring 3 probe\n");
        cpu::halt();
    }
    Endpoint::init();
    if (!CoreServices::initialize()) {
        serial::serialWrite("[SERVICE] unable to create core service objects\n");
        cpu::halt();
    }
    ipcProbeEndpoint = Endpoint::create();
    if (ipcProbeEndpoint == nullptr ||
        Thread::create(task::getCurrent(), ipcServerProbe, nullptr) ==
            nullptr ||
        Thread::create(task::getCurrent(), ipcClientProbe, nullptr) ==
            nullptr) {
        serial::serialWrite("[IPC] unable to create IPC probes\n");
        cpu::halt();
    }
    if (Thread::create(nullptr, schedulerProbe, reinterpret_cast<void*>(1)) ==
            nullptr ||
        Thread::create(nullptr, schedulerProbe, reinterpret_cast<void*>(2)) ==
            nullptr) {
        serial::serialWrite("[THREAD] unable to create schedule probes\n");
        cpu::halt();
    }
    interrupt::enable();

#ifdef OS_TASK_LIFECYCLE_TEST
    TaskLifecycleTest::run();
#endif
    if (UserProgramLoader::startInitial(bootInfo) == nullptr) {
        cpu::halt();
    }
    // serial::serialWrite("[KERNEL] entering idle loop\n");

    // The per-CPU idle thread owns idle time. Keeping this bootstrap thread
    // runnable makes every I/O wake compete with an otherwise empty timeslice.
    Thread::exitCurrent(0);
}

}  // namespace kernel

extern "C" void kernelEntry(uint64_t multibootAddress, uint64_t pageTableRoot,
                            uint64_t bootstrapStart, uint64_t bootstrapEnd) {
    kernel::serial::initSerial();
    // kernel::serial::serialWrite("[BOOT] osinfo starting\n");
    // kernel::serial::serialWrite("[BOOT] Entered x86-64 long mode\n");

    kernel::BootInfo bootInfo{};
    if (!kernel::parseMultibootInfo(multibootAddress, &bootInfo)) {
        kernel::serial::serialWrite("[BOOT] invalid Multiboot2 information\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    bootInfo.pageTableRoot  = pageTableRoot;
    bootInfo.bootstrapStart = bootstrapStart;
    bootInfo.bootstrapEnd   = bootstrapEnd;

    // kernel::serial::serialWrite("[BOOT] Multiboot2 information received\n");
    // kernel::serial::serialWrite("[BOOT] memory map found\n");
    kernel::kernelMain(&bootInfo);
}
