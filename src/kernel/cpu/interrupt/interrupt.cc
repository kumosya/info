/*
 * 建立中断入口表，分发异常、设备中断及调度请求。
 */

#include "kernel/interrupt.h"

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace cpu {

    /* 与硬件格式一致的一条中断门，保存入口地址、代码段和独立栈编号。 */
    struct [[gnu::packed]] IdtEntry {
            uint16_t offsetLow;
            uint16_t selector;
            uint8_t ist;
            uint8_t attributes;
            uint16_t offsetMiddle;
            uint32_t offsetHigh;
            uint32_t reserved;
    };

    /* LIDT 所需的表上限与地址，按硬件要求紧凑排列。 */
    struct [[gnu::packed]] IdtPointer {
            uint16_t limit;
            uint64_t base;
    };

    alignas(16) static IdtEntry bootstrapIdt[256];
    static Handler handlers[256];
    static task::SpinLock handlerLock;
    extern "C" void* isrStubTable[];

    /*
     * 向指定 I/O 端口写一个字节，供初始化旧中断控制器使用。
     */
    static void writePort(uint16_t port, uint8_t value) {
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    }

    /*
     * 屏蔽两片旧式 PIC 的全部输入，避免与 APIC 中断路由同时生效。
     */
    static void maskLegacyPic() {
        writePort(0x21, 0xff);
        writePort(0xa1, 0xff);
    }

    /*
     * 按硬件布局填写一条 IDT
     * 门，指定汇编入口、独立栈编号以及允许的软件触发特权级。
     */
    static void setIdtEntry(uint8_t vector, void* entry, uint8_t ist,
                            uint8_t privilege) {
        const uint64_t address  = reinterpret_cast<uint64_t>(entry);
        IdtEntry& descriptor    = bootstrapIdt[vector];
        descriptor.offsetLow    = address & 0xffff;
        descriptor.selector     = 0x08;
        descriptor.ist          = ist & 0x07;
        descriptor.attributes   = 0x8e | ((privilege & 0x03) << 5);
        descriptor.offsetMiddle = (address >> 16) & 0xffff;
        descriptor.offsetHigh   = address >> 32;
        descriptor.reserved     = 0;
    }

    /*
     * 使用不依赖普通输出锁的紧急串口路径打印异常向量、错误码和寄存器线索。
     */
    static void printException(const Context* context) {
        static constexpr const char* names[32] = {
            "#DE",         "#DB",
            "NMI",         "#BP",
            "#OF",         "#BR",
            "#UD",         "#NM",
            "#DF",         "legacy coprocessor overrun",
            "#TS",         "#NP",
            "#SS",         "#GP",
            "#PF",         "reserved 15",
            "#MF",         "#AC",
            "#MC",         "#XM",
            "#VE",         "#CP",
            "reserved 22", "reserved 23",
            "reserved 24", "reserved 25",
            "reserved 26", "reserved 27",
            "#HV",         "#VC",
            "#SX",         "reserved 31"};
        device::emergencyWrite("[INTERRUPT] ");
        device::emergencyWrite(context->vector < 32 ? names[context->vector]
                                                    : "unexpected vector");
        device::emergencyWrite(" exception vector=");
        device::emergencyHex(context->vector);
        device::emergencyWrite(" error=");
        device::emergencyHex(context->errorCode);
        device::emergencyWrite(" rip=");
        device::emergencyHex(context->rip);
        device::emergencyWrite(" rsp=");
        device::emergencyHex(context->rsp);
        device::emergencyWrite("\n");
    }

    /*
     * 建立 256
     * 个中断入口，为严重异常配置独立栈，并只对断点入口开放用户态软件触发。
     */
    void initIdt() {
        maskLegacyPic();

        for (uint16_t vector = 0; vector < 256; ++vector) {
            // 双重故障、不可屏蔽中断和机器检查各用一个独立栈，其余入口沿用普通中断栈规则。
            uint8_t ist = 0;

            if (vector == 8) {
                ist = 1;
            } else if (vector == 2) {
                ist = 2;
            } else if (vector == 18) {
                ist = 3;
            }

            // 只有断点允许用户态用软件指令触发；其他向量仍限制为内核特权。
            const uint8_t privilege = vector == 3 ? 3 : 0;
            setIdtEntry(static_cast<uint8_t>(vector), isrStubTable[vector], ist,
                        privilege);
            handlers[vector] = nullptr;
        }

        cpu::getCurrent()->setIdt(bootstrapIdt);
        loadIdt();
        device::serialWrite("[INTERRUPT] IDT initialized with 256 vectors\n");
    }

    /*
     * 为当前 CPU 准备或复用 IDT 副本，然后用 LIDT 装入硬件寄存器。
     */
    void loadIdt() {
        LocalData* cpu = cpu::getCurrent();

        // 新 CPU 复制已经准备好的入口表，使用自己的表存储；引导 CPU
        // 可直接复用启动表。
        if (cpu->getIdt() == nullptr) {
            IdtEntry* idt =
                static_cast<IdtEntry*>(mm::allocateHeap(sizeof(bootstrapIdt)));

            if (idt == nullptr) {
                device::serialWrite(
                    "[INTERRUPT] unable to allocate per-CPU IDT\n");
                cpu::halt();
            }

            for (uint32_t index = 0; index < 256; ++index) {
                idt[index] = bootstrapIdt[index];
            }

            cpu->setIdt(idt);
        }

        // 硬件需要的是表的字节上限和地址，LIDT 把它们装入当前 CPU。
        const IdtPointer pointer{sizeof(bootstrapIdt) - 1,
                                 reinterpret_cast<uint64_t>(cpu->getIdt())};
        asm volatile("lidt %0" : : "m"(pointer));
    }

    /*
     * 在登记锁内安装尚未占用的中断处理函数，并以原子发布让中断分发看到完整指针。
     */
    bool registerHandler(uint8_t vector, Handler handler) {
        ::task::InterruptSpinLockGuard guard(handlerLock);

        if (handler == nullptr || handlers[vector] != nullptr) {
            return false;
        }

        __atomic_store_n(&handlers[vector], handler, __ATOMIC_RELEASE);

        return true;
    }

    /*
     * 撤销指定向量的处理函数，后续分发将按未登记向量处理。
     */
    void unregisterHandler(uint8_t vector) {
        ::task::InterruptSpinLockGuard guard(handlerLock);
        __atomic_store_n(&handlers[vector], nullptr, __ATOMIC_RELEASE);
    }

    /*
     * 区分严重异常、缺页和可转为用户信号的异常，无法恢复时进入 panic。
     */
    void handleException(Context* context) {
        if (context->vector == 2 || context->vector == 8 ||
            context->vector == 18) {
            panic(context);
        }

        // 缺页有单独的修复和权限检查路径；其他受支持异常再尝试转换成用户信号。
        if (context->vector == 14) {
            handlePageFault(context);

            return;
        }

        if (dispatchUserException(context)) {
            return;
        }

        panic(context);
    }

    /*
     * 关闭中断并确保只有一个 CPU 输出主要故障信息，再尽力停住其他 CPU
     * 并永久停机。
     */
    [[noreturn]] void panic(Context* context) {
        cpu::disableInterrupts();

        // 多个 CPU 可能同时看到故障；原子认领输出权，避免重复打印互相覆盖，其他
        // CPU 直接停机。
        static bool panicking = false;

        if (__atomic_exchange_n(&panicking, true, __ATOMIC_ACQ_REL)) {
            cpu::halt();
        }

        cpu::stopOtherCpus();
        printException(context);
        device::emergencyWrite("[INTERRUPT] unrecoverable kernel exception\n");
        cpu::halt();
    }

    /*
     * 中断汇编入口的公共 C++ 分发函数。
     * 先区分异常和外部中断，完成处理与 APIC
     * 确认后，在允许抢占时处理调度并检查用户信号。
     */
    extern "C" void interruptDispatch(Context* context) {
        // CPU 异常不走设备处理函数表，处理后仍需要执行返回用户态前的信号检查。
        if (context->vector < 32) {
            handleException(context);

            // 返回前由统一入口检查待处理信号；该入口也会判断当前现场是否真的要回用户态。
            task::returnToUser(context);

            return;
        }

        // 保存可能被调度或信号使用的用户现场，再调用登记的设备或跨核处理函数。
        task::captureUserContext(context);
        Handler handler =
            __atomic_load_n(&handlers[context->vector], __ATOMIC_ACQUIRE);

        if (handler != nullptr) {
            handler(context);
        } else if (context->vector != 0xff) {
            // 未登记向量只报告一次；用原子位图避免多个 CPU 同时输出相同告警。

            // 普通外部中断处理完要向 APIC 确认；伪中断 0xff 不做这一步。
            static uint64_t reported[4]{};
            const uint64_t index         = context->vector / 64;
            const unsigned long long bit = 1ULL << (context->vector % 64);

            if (!(__atomic_fetch_or(&reported[index], bit, __ATOMIC_RELAXED) &
                  bit)) {
                device::serialWrite(
                    "[INTERRUPT] unregistered vector acknowledged: ");
                device::serialWriteDecimal(context->vector);
                device::serialWrite("\n");
            }
        }

        if (context->vector != 0xff) {
            sendEndOfInterrupt();
        }

        // 处理函数只提出调度请求；等控制器已确认、且当前允许抢占时，才真正切换线程。
        if (task::isSchedulerInitialized() && isPreemptionEnabled() &&
            getCurrent()->isRescheduleRequested()) {
            task::schedule();
        }

        task::returnToUser(context);
    }

}  // namespace cpu
