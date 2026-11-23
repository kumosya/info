/*
 * 将硬件 IRQ 绑定到消息端点，管理确认和解除绑定。
 */

#include <new>

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace device {

    constexpr uint8_t irqVectorBase = 64;
    constexpr uint8_t maximumIrq    = 15;
    static task::SpinLock bindingLock;
    static InterruptBinding* bindings[16];

    /*
     * 把硬件中断向量换算成绑定记录；电平触发先屏蔽线路，再向服务端点投递无阻塞通知。
     */
    static void irqHandler(cpu::Context* context) {
        const uint64_t vector = context->vector;

        if (vector < irqVectorBase || vector > irqVectorBase + maximumIrq) {
            return;
        }

        task::InterruptSpinLockGuard guard(bindingLock);
        InterruptBinding* binding = bindings[vector - irqVectorBase];

        if (binding != nullptr) {
            // 电平中断的设备条件可能仍然有效，先屏蔽再通知服务，等服务处理并确认后才重新开放。
            if (binding->requiresAcknowledgement()) {
                cpu::maskIrq(binding->irq());
            }

            (binding->endpoint())
                ->notifyFromInterrupt(0x495251, binding->irq());
        }
    }

    /*
     * 独占绑定一个 IRQ
     * 到任务端点，安装中断入口和控制器路由后才公布记录；失败时按准备顺序撤销。
     */
    InterruptBinding* InterruptBinding::create(::task::Task& task, uint8_t irq,
                                               ::task::Endpoint* endpoint,
                                               bool levelTriggered) {
        if (irq > maximumIrq || endpoint == nullptr) {
            return nullptr;
        }

        ::task::InterruptSpinLockGuard guard(bindingLock);

        // 每条传统 IRQ
        // 只绑定一个接收者；持锁完成安装，避免两个任务同时抢到同一条线。
        if (bindings[irq] != nullptr) {
            return nullptr;
        }

        void* storage = mm::allocateHeap(sizeof(InterruptBinding));
        InterruptBinding* binding =
            storage == nullptr ? nullptr : new (storage) InterruptBinding;

        if (binding == nullptr ||
            !cpu::registerHandler(irqVectorBase + irq, irqHandler)) {
            mm::freeHeap(binding);

            return nullptr;
        }

        // 先登记处理函数再配置硬件路由；配置失败要撤掉函数登记并释放对象。
        const uint32_t apicId = cpu::getCurrent()->getApicId();

        if (!cpu::setIrq(irq, irqVectorBase + irq, apicId, levelTriggered)) {
            cpu::unregisterHandler(irqVectorBase + irq);
            mm::freeHeap(binding);

            return nullptr;
        }

        // 中断仍受本地关中断与绑定锁保护，填好全部字段并保留端点后，最后才把对象放进可见表。
        binding->irq_      = irq;
        binding->endpoint_ = endpoint;
        endpoint->retain();
        binding->owner_          = &task;
        binding->apicId_         = apicId;
        binding->levelTriggered_ = levelTriggered;
        bindings[irq]            = binding;

        return binding;
    }

    /*
     * 返回绑定的传统 IRQ 编号。
     */
    uint8_t InterruptBinding::irq() const { return irq_; }

    /*
     * 返回绑定保留的端点指针，调用方不因此取得额外引用。
     */
    ::task::Endpoint* InterruptBinding::endpoint() const { return endpoint_; }

    /*
     * 只允许绑定所属任务确认；电平触发确认后重新启用线路，边沿触发无需重新布线。
     */
    bool InterruptBinding::acknowledge(::task::Task& task) const {
        return owner_ == &task &&
               (!levelTriggered_ ||
                cpu::setIrq(irq_, irqVectorBase + irq_, apicId_, true));
    }

    /*
     * 在绑定锁内屏蔽线路并撤销入口，之后在锁外归还端点引用和对象存储。
     */
    void InterruptBinding::destroy() {
        {
            ::task::InterruptSpinLockGuard guard(bindingLock);

            if (bindings[irq_] == this) {
                // 先阻止新的硬件中断，再从绑定表和处理函数表移除，避免后续中断访问已释放对象。
                cpu::maskIrq(irq_);
                bindings[irq_] = nullptr;
                cpu::unregisterHandler(irqVectorBase + irq_);
            }
        }

        // 退出绑定锁后释放端点和对象，避免释放端点时进入其他锁导致顺序冲突。
        endpoint_->release();
        mm::freeHeap(this);
    }

}  // namespace device
