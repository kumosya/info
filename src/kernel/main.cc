#include "kernel/boot.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/elf.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace kernel {
namespace {

constexpr uint64_t directMapBase      = 0xffff800000000000ULL;
constexpr uint64_t userProbeCode      = 0x0000000000400000ULL;
constexpr uint64_t userProbeStackBase = 0x0000000000800000ULL;
constexpr uint64_t pageSize           = 4096;

uint32_t schedulerProbeCount;
uint32_t ipcProbeResult;
uint32_t smpProbeCount;
kernel::task::Endpoint* ipcProbeEndpoint;

extern "C" uint8_t userSyscallProbeStart;
extern "C" uint8_t userSyscallProbeEnd;

kernel::task::Task* createSyscallProbe() {
    kernel::task::Task* task =
        kernel::task::Task::create("syscall-probe", kernel::task::getCurrent());
    if (task == nullptr) {
        kernel::device::serialWrite("[SYSCALL] probe task creation failed\n");
        return nullptr;
    }
    const uint64_t codePhysical  = kernel::mm::allocatePhysicalPage();
    const uint64_t stackPhysical = kernel::mm::allocatePhysicalPage();
    const uint64_t codeSize =
        reinterpret_cast<uint64_t>(&userSyscallProbeEnd) -
        reinterpret_cast<uint64_t>(&userSyscallProbeStart);
    if (codePhysical == 0 || stackPhysical == 0) {
        kernel::device::serialWrite("[SYSCALL] probe page allocation failed\n");
        return nullptr;
    }
    if (codeSize > pageSize) {
        kernel::device::serialWrite("[SYSCALL] probe code exceeds one page\n");
        return nullptr;
    }
    if (!kernel::mm::mapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), userProbeCode,
            codePhysical, kernel::mm::pageUser)) {
        kernel::device::serialWrite("[SYSCALL] probe code mapping failed\n");
        return nullptr;
    }
    if (!kernel::mm::mapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), userProbeStackBase,
            stackPhysical,
            kernel::mm::pageUser | kernel::mm::pageWritable |
                kernel::mm::pageNoExecute)) {
        kernel::device::serialWrite("[SYSCALL] probe stack mapping failed\n");
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
    for (uint64_t i = 1; i < 4; ++i) {
        if (kernel::mm::allocateUserPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(),
                userProbeStackBase + i * pageSize,
                kernel::mm::pageUser | kernel::mm::pageWritable |
                    kernel::mm::pageNoExecute) !=
            kernel::mm::UserPageMapResult::Success)
            return nullptr;
    }
    kernel::task::Thread* thread = kernel::task::Thread::createUser(
        task, userProbeCode, userProbeStackBase + 4 * pageSize,
        kernel::task::coreServices().getHandle(*task,
                                               os::CoreServiceKind::Task),
        0, kernel::cpu::getOnlineCpuCount() > 1 ? 1ULL << 1 : 1ULL);
    if (thread == nullptr) {
        kernel::device::serialWrite("[SYSCALL] probe thread creation failed\n");
        return nullptr;
    }
    return task;
}

void schedulerProbe(void*) {
    // kernel::device::serialWrite("[THREAD] probe ");
    // kernel::device::serialWriteDecimal(reinterpret_cast<uint64_t>(argument));
    // kernel::device::serialWrite(" running\n");
    kernel::task::yieldThread();
    __atomic_add_fetch(&schedulerProbeCount, 1U, __ATOMIC_RELAXED);
    // kernel::device::serialWrite("[THREAD] probe completed\n");
}

void smpSchedulerProbe(void*) {
    // Creating the probe detached lets affinity be configured before any CPU
    // can run it. Verify actual execution here, not just queue admission.
    if (kernel::cpu::getCurrentId() != 1) {
        kernel::device::serialWrite("[SMP] affinity probe ran on wrong CPU\n");
        kernel::cpu::halt();
    }
    kernel::device::serialWrite("[SMP] scheduled probe on CPU ");
    kernel::device::serialWriteDecimal(kernel::cpu::getCurrentId());
    kernel::device::serialWrite("\n");
    __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
}

void ipcServerProbe(void*) {
    kernel::task::IpcMessage request{};
    request.length = 0xffffffffU;
    if (ipcProbeEndpoint->receive(&request) != 0 || request.type != 0x41 ||
        request.length != 1 || request.data[0] != 0x1234 ||
        request.senderTid == 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
        return;
    }
    kernel::task::IpcMessage reply{};
    reply.replyToken = request.replyToken;
    reply.type       = 0x42;
    reply.length     = 1;
    reply.data[0]    = request.data[0] + 1;
    if (ipcProbeEndpoint->reply(&reply) != 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }
    // kernel::device::serialWrite("[IPC] server probe completed\n");
}

void ipcClientProbe(void*) {
    kernel::task::IpcMessage request{};
    request.type    = 0x41;
    request.length  = 1;
    request.data[0] = 0x1234;
    kernel::task::IpcMessage reply{};
    if (ipcProbeEndpoint->call(&request, &reply) == 0 && reply.type == 0x42 &&
        reply.length == 1 && reply.data[0] == 0x1235) {
        __atomic_store_n(&ipcProbeResult, 1U, __ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }
    // kernel::device::serialWrite("[IPC] client probe completed\n");
}

}  // namespace
}  // namespace kernel

namespace kernel {

void kernelMain(const BootInfo* bootInfo) {
    // device::serialWrite("[KERNEL] kernelMain()\n");
    // device::serialWrite("[KERNEL] arch initialized\n");
    mm::initPhy(bootInfo);
    mm::initVir(bootInfo);
    mm::initHeap();
    volatile uint64_t* heapProbe =
        static_cast<volatile uint64_t*>(mm::allocateHeap(sizeof(uint64_t)));
    if (heapProbe == nullptr) {
        device::serialWrite("[MEMORY] kernel heap initialization failed\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    *heapProbe = 0x6f73696e666fULL;
    cpu::init();
    mm::memoryTopology().enableCpuLocal();
    mm::enableCpuObjectCaches();
    time::initClock();
    cpu::initIdt();
    cpu::initControllers(bootInfo);
    device::initDevices(bootInfo);
    device::bootImage().init(*bootInfo);
    cpu::initSmp(bootInfo);
    ::kernel::task::initThreads();
    task::initScheduler();
    task::init();
    task::runSynchronizationSelfTest();
    task::initSyscall();
    cpu::startApplicationTaskors();
    if (cpu::getOnlineCpuCount() > 1) {
        // Do not publish the thread with the default unrestricted mask. It
        // could run or terminate before setAffinity sees a Ready thread.
        // Moving this detached Ready thread to CPU 1 performs its admission.
        ::kernel::task::Thread* smpProbe = ::kernel::task::Thread::create(
            nullptr, smpSchedulerProbe, nullptr,
            ::kernel::task::ThreadType::Kernel, false);
        if (smpProbe == nullptr || !(smpProbe)->setAffinity(1ULL << 1)) {
            device::serialWrite("[SMP] unable to create CPU affinity probe\n");
            cpu::halt();
        }
    } else {
        __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
    }
    ::kernel::task::Task* taskProbe =
        ::kernel::task::Task::create("address-space-probe", task::getCurrent());
    if (taskProbe == nullptr ||
        taskProbe->addressSpace().rootPhysicalAddress() ==
            mm::getKernelAddressSpaceRoot() ||
        !::kernel::elf::runElfValidationSelfTest(*taskProbe)) {
        device::serialWrite("[TASK] address-space self-test failed\n");
        cpu::halt();
    }

    taskProbe->destroy();
    ::kernel::task::Endpoint::init();
    if (!::kernel::task::coreServices().init()) {
        device::serialWrite(
            "[SERVICE] unable to create core service objects\n");
        cpu::halt();
    }
    ::kernel::task::Task* syscallProbe = createSyscallProbe();
    if (syscallProbe == nullptr) {
        device::serialWrite("[SYSCALL] unable to create Ring 3 probe\n");
        cpu::halt();
    }
    ipcProbeEndpoint = ::kernel::task::Endpoint::create();
    if (ipcProbeEndpoint == nullptr ||
        ::kernel::task::Thread::create(task::getCurrent(), ipcServerProbe,
                                       nullptr) == nullptr ||
        ::kernel::task::Thread::create(task::getCurrent(), ipcClientProbe,
                                       nullptr) == nullptr) {
        device::serialWrite("[IPC] unable to create IPC probes\n");
        cpu::halt();
    }
    if (::kernel::task::Thread::create(nullptr, schedulerProbe,
                                       reinterpret_cast<void*>(1)) == nullptr ||
        ::kernel::task::Thread::create(nullptr, schedulerProbe,
                                       reinterpret_cast<void*>(2)) == nullptr) {
        device::serialWrite("[THREAD] unable to create schedule probes\n");
        cpu::halt();
    }
    cpu::enableInterrupts();

    if (::kernel::task::startInitialProgram(bootInfo) == nullptr) {
        cpu::halt();
    }

    // device::serialWrite("[KERNEL] entering idle loop\n");

    // The per-CPU idle thread owns idle time. Keeping this bootstrap thread
    // runnable makes every I/O wake compete with an otherwise empty timeslice.
    ::kernel::task::exitCurrentThread(0);
}

}  // namespace kernel

extern "C" void kernelEntry(uint64_t multibootAddress, uint64_t pageTableRoot,
                            uint64_t bootstrapStart, uint64_t bootstrapEnd) {
    kernel::device::initSerial();
    // kernel::device::serialWrite("[BOOT] osinfo starting\n");
    // kernel::device::serialWrite("[BOOT] Entered x86-64 long mode\n");

    ::BootInfo bootInfo{};
    if (!parseMultibootInfo(multibootAddress, &bootInfo)) {
        kernel::device::serialWrite("[BOOT] invalid Multiboot2 information\n");
        while (true) {
            asm volatile("cli; hlt");
        }
    }
    bootInfo.setPageTableRoot(pageTableRoot);
    bootInfo.setBootstrapStart(bootstrapStart);
    bootInfo.setBootstrapEnd(bootstrapEnd);

    // kernel::device::serialWrite("[BOOT] Multiboot2 information received\n");
    // kernel::device::serialWrite("[BOOT] memory map found\n");
    kernel::kernelMain(&bootInfo);
}
