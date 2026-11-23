/*
 * 组织内核初始化、自检与首个用户程序启动。
 */

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

constexpr uint64_t directMapBase      = 0xffff800000000000ULL;
constexpr uint64_t userProbeCode      = 0x0000000000400000ULL;
constexpr uint64_t userProbeStackBase = 0x0000000000800000ULL;
constexpr uint64_t pageSize           = 4096;

static uint32_t schedulerProbeCount;
static uint32_t ipcProbeResult;
static uint32_t smpProbeCount;
static task::Endpoint* ipcProbeEndpoint;

extern "C" uint8_t userSyscallProbeStart;
extern "C" uint8_t userSyscallProbeEnd;

/*
 * 建立一个用户态系统调用测试任务，准备代码与栈并启动到指定
 * CPU，用来检查用户态到内核服务的调用边界。
 */
static task::Task* createSyscallProbe() {
    task::Task* task = task::Task::create("syscall-probe", task::getCurrent());

    if (task == nullptr) {
        device::serialWrite("[SYSCALL] probe task creation failed\n");

        return nullptr;
    }

    // 探针用独立的代码页和栈页，先确认汇编探针能装进一页，再映射到测试任务的用户地址。
    const uint64_t codePhysical  = mm::allocatePhysicalPage();
    const uint64_t stackPhysical = mm::allocatePhysicalPage();
    const uint64_t codeSize =
        reinterpret_cast<uint64_t>(&userSyscallProbeEnd) -
        reinterpret_cast<uint64_t>(&userSyscallProbeStart);

    if (codePhysical == 0 || stackPhysical == 0) {
        device::serialWrite("[SYSCALL] probe page allocation failed\n");

        return nullptr;
    }

    if (codeSize > pageSize) {
        device::serialWrite("[SYSCALL] probe code exceeds one page\n");

        return nullptr;
    }

    if (!mm::mapPageInAddressSpace(task->addressSpace().rootPhysicalAddress(),
                                   userProbeCode, codePhysical, mm::pageUser)) {
        device::serialWrite("[SYSCALL] probe code mapping failed\n");

        return nullptr;
    }

    if (!mm::mapPageInAddressSpace(
            task->addressSpace().rootPhysicalAddress(), userProbeStackBase,
            stackPhysical,
            mm::pageUser | mm::pageWritable | mm::pageNoExecute)) {
        device::serialWrite("[SYSCALL] probe stack mapping failed\n");

        return nullptr;
    }

    // 经内核直接映射写入测试代码，并把代码剩余空间和栈页清零，避免用户探针看到旧数据。
    uint8_t* code = reinterpret_cast<uint8_t*>(directMapBase + codePhysical);
    const uint8_t* source = &userSyscallProbeStart;

    for (uint64_t index = 0; index < pageSize; ++index) {
        code[index] = index < codeSize ? source[index] : 0;
    }

    uint8_t* stack = reinterpret_cast<uint8_t*>(directMapBase + stackPhysical);

    for (uint64_t index = 0; index < pageSize; ++index) {
        stack[index] = 0;
    }

    // 再补齐四页用户栈；栈可写但不可执行，代码页不授予写权限。
    for (uint64_t i = 1; i < 4; ++i) {
        if (mm::allocateUserPageInAddressSpace(
                task->addressSpace().rootPhysicalAddress(),
                userProbeStackBase + i * pageSize,
                mm::pageUser | mm::pageWritable | mm::pageNoExecute) !=
            mm::UserPageMapResult::Success) {
            return nullptr;
        }
    }

    // 创建真正的用户线程验证系统调用入口；有第二个 CPU 时将探针安排到该
    // CPU，兼顾多核路径。
    task::Thread* thread = task::Thread::createUser(
        task, userProbeCode, userProbeStackBase + 4 * pageSize,
        task::coreServices().getHandle(*task, os::CoreServiceKind::Task), 0,
        cpu::getOnlineCpuCount() > 1 ? 1ULL << 1 : 1ULL);

    if (thread == nullptr) {
        device::serialWrite("[SYSCALL] probe thread creation failed\n");

        return nullptr;
    }

    return task;
}

/*
 * 让出一次 CPU 后增加完成计数，启动主线据此检查线程确实经历了调度。
 */
static void schedulerProbe(void*) {
    // device::serialWrite("[THREAD] probe ");
    // device::serialWriteDecimal(reinterpret_cast<uint64_t>(argument));
    // device::serialWrite(" running\n");

    // 主动让出后还能继续执行才算调度恢复成功，完成次数以原子计数供其他 CPU
    // 观察。
    task::yieldThread();
    __atomic_add_fetch(&schedulerProbeCount, 1U, __ATOMIC_RELAXED);
    // device::serialWrite("[THREAD] probe completed\n");
}

/*
 * 检查绑定 CPU
 * 的测试线程实际运行位置，再发布完成标志；仅入队成功不能证明亲和性已经正确生效。
 */
static void smpSchedulerProbe(void*) {
    // 先创建未入队的探针才能在执行前设置 CPU 亲和性；这里检查真正运行的
    // CPU，而不只检查入队成功。
    if (cpu::getCurrentId() != 1) {
        device::serialWrite("[SMP] affinity probe ran on wrong CPU\n");
        cpu::halt();
    }

    device::serialWrite("[SMP] scheduled probe on CPU ");
    device::serialWriteDecimal(cpu::getCurrentId());
    device::serialWrite("\n");
    __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
}

/*
 * 接收并校验测试请求，沿原回复令牌返回修改后的值，验证同步 IPC
 * 的来源和回复关联。
 */
static void ipcServerProbe(void*) {
    task::IpcMessage request{};
    request.length = 0xffffffffU;

    // 接收时核对消息长度、载荷和发送者身份，不能只凭接收返回成功就判定 IPC
    // 正常。
    if (ipcProbeEndpoint->receive(&request) != 0 || request.type != 0x41 ||
        request.length != 1 || request.data[0] != 0x1234 ||
        request.senderTid == 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);

        return;
    }

    // 回复沿用本次请求的回复凭据，修改载荷使客户端能确认确实经过服务端处理。
    task::IpcMessage reply{};
    reply.replyToken = request.replyToken;
    reply.type       = 0x42;
    reply.length     = 1;
    reply.data[0]    = request.data[0] + 1;

    if (ipcProbeEndpoint->reply(&reply) != 0) {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }

    // device::serialWrite("[IPC] server probe completed\n");
}

/*
 * 发起同步测试调用并检查回复字段，用原子结果标志向启动线程报告测试成功或失败。
 */
static void ipcClientProbe(void*) {
    task::IpcMessage request{};
    request.type    = 0x41;
    request.length  = 1;
    request.data[0] = 0x1234;
    task::IpcMessage reply{};

    // 同步调用需要请求和回复都匹配，最终结果用 release 发布给其他 CPU。
    if (ipcProbeEndpoint->call(&request, &reply) == 0 && reply.type == 0x42 &&
        reply.length == 1 && reply.data[0] == 0x1235) {
        __atomic_store_n(&ipcProbeResult, 1U, __ATOMIC_RELEASE);
    } else {
        __atomic_store_n(&ipcProbeResult, 2U, __ATOMIC_RELEASE);
    }

    // device::serialWrite("[IPC] client probe completed\n");
}


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

    // 有可用内存后再建立 CPU 局部数据；随后才能打开每 CPU
    // 缓存和依赖局部状态的子系统。
    cpu::init();
    mm::memoryTopology().enableCpuLocal();
    mm::enableCpuObjectCaches();

    // 时钟先于调度定时器标定和设备超时逻辑；中断表与外部路由则先于设备真正收发中断。
    time::initClock();
    cpu::initIdt();
    cpu::initControllers(bootInfo);
    device::initDevices(bootInfo);
    device::bootImage().init(*bootInfo);
    cpu::initSmp(bootInfo);

    // 引导 CPU 先具备线程、调度与系统调用能力，再启动其他
    // CPU，保证它们能够接入共同的运行环境。
    ::task::initThreads();
    task::initScheduler();
    task::init();
    task::runSynchronizationSelfTest();
    task::initSyscall();
    cpu::startApplicationTaskors();

    if (cpu::getOnlineCpuCount() > 1) {
        // 先不让线程入队，避免设置亲和性前就被其他 CPU 执行甚至退出。
        // 把这个尚未入队的就绪线程迁到 CPU 1 时才完成入队。
        ::task::Thread* smpProbe =
            ::task::Thread::create(nullptr, smpSchedulerProbe, nullptr,
                                   ::task::ThreadType::Kernel, false);

        if (smpProbe == nullptr || !(smpProbe)->setAffinity(1ULL << 1)) {
            device::serialWrite("[SMP] unable to create CPU affinity probe\n");
            cpu::halt();
        }
    } else {
        __atomic_store_n(&smpProbeCount, 1U, __ATOMIC_RELEASE);
    }

    // 用独立地址空间验证 ELF
    // 检查和页权限，测试任务结束后销毁，避免自检映射混入实际服务。
    ::task::Task* taskProbe =
        ::task::Task::create("address-space-probe", task::getCurrent());

    if (taskProbe == nullptr ||
        taskProbe->addressSpace().rootPhysicalAddress() ==
            mm::getKernelAddressSpaceRoot() ||
        !::elf::runElfValidationSelfTest(*taskProbe)) {
        device::serialWrite("[TASK] address-space self-test failed\n");
        cpu::halt();
    }

    taskProbe->destroy();

    // 端点与核心服务就绪后，才创建依赖服务句柄的用户态系统调用探针。
    ::task::Endpoint::init();

    if (!::task::coreServices().init()) {
        device::serialWrite(
            "[SERVICE] unable to create core service objects\n");
        cpu::halt();
    }

    ::task::Task* syscallProbe = createSyscallProbe();

    if (syscallProbe == nullptr) {
        device::serialWrite("[SYSCALL] unable to create Ring 3 probe\n");
        cpu::halt();
    }

    // 同时准备 IPC 双端和主动让出探针，让中断开放后能检查通信与调度的实际执行。
    ipcProbeEndpoint = ::task::Endpoint::create();

    if (ipcProbeEndpoint == nullptr ||
        ::task::Thread::create(task::getCurrent(), ipcServerProbe, nullptr) ==
            nullptr ||
        ::task::Thread::create(task::getCurrent(), ipcClientProbe, nullptr) ==
            nullptr) {
        device::serialWrite("[IPC] unable to create IPC probes\n");
        cpu::halt();
    }

    if (::task::Thread::create(nullptr, schedulerProbe,
                               reinterpret_cast<void*>(1)) == nullptr ||
        ::task::Thread::create(nullptr, schedulerProbe,
                               reinterpret_cast<void*>(2)) == nullptr) {
        device::serialWrite("[THREAD] unable to create schedule probes\n");
        cpu::halt();
    }

    // 所有中断入口与调度基础准备好后才开中断，随后加载第一个用户程序 Init。
    cpu::enableInterrupts();

    if (::task::startInitialProgram(bootInfo) == nullptr) {
        cpu::halt();
    }

    // device::serialWrite("[KERNEL] entering idle loop\n");

    // 空闲时间交给各 CPU
    // 的空闲线程；启动线程完成后退出，避免无事可做却继续占用时间片。
    ::task::exitCurrentThread(0);
}

/*
 * 从汇编启动入口接收页表和引导信息地址，初始化串口、解析
 * BootInfo，再交给主内核初始化流程。
 */
extern "C" void kernelEntry(uint64_t multibootAddress, uint64_t pageTableRoot,
                            uint64_t bootstrapStart, uint64_t bootstrapEnd) {
    // 最早打开串口，之后引导信息或内存初始化失败也能报告原因。
    device::initSerial();
    // device::serialWrite("[BOOT] osinfo starting\n");
    // device::serialWrite("[BOOT] Entered x86-64 long mode\n");

    // 把引导器的标签整理为内核统一结构；解析失败时不能继续使用不完整的内存地图。
    ::BootInfo bootInfo{};

    if (!bootInfo.parseMultibootInfo(multibootAddress)) {
        device::serialWrite("[BOOT] invalid Multiboot2 information\n");

        while (true) {
            asm volatile("cli; hlt");
        }
    }

    // 补上汇编入口传来的页表根和启动保留区，内存初始化据此避开仍在使用的启动页面。
    bootInfo.setPageTableRoot(pageTableRoot);
    bootInfo.setBootstrapStart(bootstrapStart);
    bootInfo.setBootstrapEnd(bootstrapEnd);

    // device::serialWrite("[BOOT] Multiboot2 information received\n");
    // device::serialWrite("[BOOT] memory map found\n");
    kernelMain(&bootInfo);
}
