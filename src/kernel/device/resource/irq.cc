#include <new>

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/interrupt.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace kernel::device {

namespace {
constexpr uint8_t irqVectorBase = 64;
constexpr uint8_t maximumIrq    = 15;
kernel::task::SpinLock bindingLock;
InterruptBinding* bindings[16];

void irqHandler(kernel::cpu::Context* context) {
    const uint64_t vector = context->vector;
    if (vector < irqVectorBase || vector > irqVectorBase + maximumIrq) {
        return;
    }
    kernel::task::InterruptSpinLockGuard guard(bindingLock);
    InterruptBinding* binding = bindings[vector - irqVectorBase];
    if (binding != nullptr) {
        if (binding->requiresAcknowledgement())
            kernel::cpu::maskIrq(binding->irq());
        (binding->endpoint())->notifyFromInterrupt(0x495251, binding->irq());
    }
}
}  // namespace

InterruptBinding* InterruptBinding::create(::kernel::task::Task& task,
                                           uint8_t irq,
                                           ::kernel::task::Endpoint* endpoint,
                                           bool levelTriggered) {
    if (irq > maximumIrq || endpoint == nullptr) {
        return nullptr;
    }
    ::kernel::task::InterruptSpinLockGuard guard(bindingLock);
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
    const uint32_t apicId = cpu::getCurrent()->getApicId();
    if (!cpu::setIrq(irq, irqVectorBase + irq, apicId, levelTriggered)) {
        cpu::unregisterHandler(irqVectorBase + irq);
        mm::freeHeap(binding);
        return nullptr;
    }
    binding->irq_      = irq;
    binding->endpoint_ = endpoint;
    endpoint->retain();
    binding->owner_          = &task;
    binding->apicId_         = apicId;
    binding->levelTriggered_ = levelTriggered;
    bindings[irq]            = binding;
    return binding;
}

uint8_t InterruptBinding::irq() const { return irq_; }

::kernel::task::Endpoint* InterruptBinding::endpoint() const {
    return endpoint_;
}

bool InterruptBinding::acknowledge(::kernel::task::Task& task) const {
    return owner_ == &task &&
           (!levelTriggered_ ||
            cpu::setIrq(irq_, irqVectorBase + irq_, apicId_, true));
}

void InterruptBinding::destroy() {
    {
        ::kernel::task::InterruptSpinLockGuard guard(bindingLock);
        if (bindings[irq_] == this) {
            cpu::maskIrq(irq_);
            bindings[irq_] = nullptr;
            cpu::unregisterHandler(irqVectorBase + irq_);
        }
    }
    endpoint_->release();
    mm::freeHeap(this);
}

}  // namespace kernel::device
