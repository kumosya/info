#include "kernel/syscall.h"

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/new.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include <os/device.h>
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"

namespace {

constexpr uint32_t ia32Efer           = 0xc0000080;
constexpr uint32_t ia32Star           = 0xc0000081;
constexpr uint32_t ia32Lstar          = 0xc0000082;
constexpr uint32_t ia32Fmask          = 0xc0000084;
constexpr uint64_t minimumUserAddress = 0x10000;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
constexpr uint64_t directMapBase      = 0xffff800000000000ULL;

static_assert(sizeof(kernel::SignalAction) == sizeof(os::wire::SignalAction));
static_assert(__is_standard_layout(kernel::SignalAction));
static_assert(sizeof(kernel::SyscallContext) == 18 * sizeof(uint64_t));
static_assert(__builtin_offsetof(kernel::SyscallContext, rax) == 14 * 8);
static_assert(__builtin_offsetof(kernel::SyscallContext, userRip) == 15 * 8);

uint64_t error(kernel::SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}

bool validUserRange(uint64_t address, uint64_t size) {
    if (size == 0 || address < minimumUserAddress) {
        return false;
    }
    const uint64_t end = address + size;
    return end > address && end <= maximumUserAddress;
}

kernel::HandleTable::Pin resolveEndpoint(uint64_t handle, uint32_t rights) {
    return kernel::HandleTable::Pin(kernel::task::getCurrent()->handles(),
                                    handle, kernel::HandleType::Endpoint,
                                    rights);
}

}  // namespace

namespace kernel::syscall {

void init() {
    constexpr uint64_t kernelCodeSelector = 0x08;
    constexpr uint64_t userSelectorBase   = 0x10;
    cpu::writeMsr(ia32Efer, cpu::readMsr(ia32Efer) | 1);
    cpu::writeMsr(ia32Star,
                  (userSelectorBase << 48) | (kernelCodeSelector << 32));
    cpu::writeMsr(ia32Lstar, reinterpret_cast<uint64_t>(syscallEntry));
    cpu::writeMsr(ia32Fmask,
                  (1ULL << 8) | (1ULL << 9) | (1ULL << 10) | (1ULL << 18));
    serial::serialWrite("[SYSCALL] SYSCALL/IRET initialized\n");
}

bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size) {
    Task* task             = task::getCurrent();
    const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);
    if (kernelBuffer == nullptr || task == nullptr ||
        !(validUserRange(address, size) ||
          (task->isSystemService() && address >= directMapBase &&
           address + size > address))) {
        return false;
    }
    if (address < maximumUserAddress &&
        !memory::PagerBridge::ensureUserMemory(address, size, false))
        return false;
    PreemptionSpinLockGuard guard(task->addressSpace().lock());
    if (!memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    address, size, false)) {
        return false;
    }
    uint8_t* destination  = static_cast<uint8_t*>(kernelBuffer);
    const uint8_t* source = static_cast<const uint8_t*>(userBuffer);
    for (size_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
    return true;
}

bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size) {
    Task* task             = task::getCurrent();
    const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);
    if (kernelBuffer == nullptr || task == nullptr ||
        !(validUserRange(address, size) ||
          (task->isSystemService() && address >= directMapBase &&
           address + size > address))) {
        return false;
    }
    if (address < maximumUserAddress &&
        !memory::PagerBridge::ensureUserMemory(address, size, true))
        return false;
    PreemptionSpinLockGuard guard(task->addressSpace().lock());
    if (!memory::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
                                    address, size, true)) {
        return false;
    }
    uint8_t* destination  = static_cast<uint8_t*>(userBuffer);
    const uint8_t* source = static_cast<const uint8_t*>(kernelBuffer);
    for (size_t index = 0; index < size; ++index) {
        destination[index] = source[index];
    }
    return true;
}

namespace {

uint64_t handleSendMessage(uint64_t endpointHandle, uint64_t userMessage,
                           uint64_t deadline, uint64_t cancelThreadId, uint64_t,
                           uint64_t) {
    auto endpointPin = resolveEndpoint(
        endpointHandle, !userMessage && cancelThreadId ? 0 : handleRightWrite);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage message{};
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (!userMessage && cancelThreadId) return endpoint->cancel(cancelThreadId);
    if (!copyFromUser(&message, reinterpret_cast<const void*>(userMessage),
                      sizeof(message))) {
        return error(SyscallError::BadAddress);
    }
    return static_cast<uint64_t>(endpoint->send(&message, deadline));
}

uint64_t handleReceiveMessage(uint64_t endpointHandle, uint64_t userMessage,
                              uint64_t deadline, uint64_t, uint64_t, uint64_t) {
    auto endpointPin   = resolveEndpoint(endpointHandle, handleRightRead);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage message{};
    Task* task = task::getCurrent();
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (task == nullptr || !memory::PagerBridge::ensureUserMemory(
                               userMessage, sizeof(message), true)) {
        return error(SyscallError::BadAddress);
    }
    const int result = endpoint->receive(&message, deadline);
    if (result) return static_cast<uint64_t>(result);
    if (copyToUser(reinterpret_cast<void*>(userMessage), &message,
                   sizeof(message)))
        return 0;
    for (uint32_t i = 0; i < message.handleCount; ++i)
        task->handles().close(message.handles[i]);
    return error(SyscallError::BadAddress);
}

class ServiceCall {
    uint64_t operation_;
    uint64_t complete(Endpoint& endpoint, uint64_t userRequest,
                      uint64_t userReply, uint64_t deadline,
                      IpcMessage& request, IpcMessage& reply, bool immediate) {
        Task* task = task::getCurrent();
        if (task == nullptr ||
            !copyFromUser(&request, reinterpret_cast<const void*>(userRequest),
                          sizeof(request)) ||
            !memory::PagerBridge::ensureUserMemory(userReply, sizeof(reply),
                                                   true)) {
            return error(SyscallError::BadAddress);
        }
        if (request.type != operation_)
            return error(SyscallError::InvalidArgument);
        const int result =
            immediate ? endpoint.callImmediate(request, reply, deadline)
                      : endpoint.call(&request, &reply, deadline);
        if (Thread::getCurrent()->contextRestored()) return 0;
        if (result) return static_cast<uint64_t>(result);
        if (copyToUser(reinterpret_cast<void*>(userReply), &reply,
                       sizeof(reply)))
            return 0;
        for (uint32_t i = 0; i < reply.handleCount; ++i)
            task->handles().close(reply.handles[i]);
        return error(SyscallError::BadAddress);
    }

   public:
    explicit ServiceCall(uint64_t operation) : operation_(operation) {}
    __attribute__((noinline)) uint64_t normal(Endpoint& endpoint,
                                              uint64_t requestAddress,
                                              uint64_t replyAddress,
                                              uint64_t deadline,
                                              bool immediate) {
        IpcMessage request{}, reply{};
        return complete(endpoint, requestAddress, replyAddress, deadline,
                        request, reply, immediate);
    }
    __attribute__((noinline)) uint64_t context(Endpoint& endpoint,
                                               uint64_t requestAddress,
                                               uint64_t replyAddress,
                                               uint64_t deadline) {
        class Buffers {
           public:
            IpcMessage request{}, reply{};
        };
        ThreadCleanup cleanup;
        cpu::disablePreemption();
        void* storage = heap::kernelAllocate(sizeof(Buffers));
        auto* buffers = storage ? new (storage) Buffers : nullptr;
        if (buffers)
            cleanup.attach(buffers,
                           [](void* value) { heap::kernelFree(value); });
        cpu::enablePreemption();
        if (!buffers) return error(SyscallError::OutOfMemory);
        return complete(endpoint, requestAddress, replyAddress, deadline,
                        buffers->request, buffers->reply, true);
    }
};
uint64_t handleCallService(uint64_t endpointHandle, uint64_t userRequest,
                           uint64_t userReply, uint64_t deadline, uint64_t,
                           uint64_t) {
    auto pin       = resolveEndpoint(endpointHandle, handleRightWrite);
    auto* endpoint = pin.as<Endpoint>();
    if (!endpoint) return error(SyscallError::InvalidHandle);
    uint64_t operation = 0;
    if (!copyFromUser(&operation, reinterpret_cast<void*>(userRequest),
                      sizeof(operation)))
        return error(SyscallError::BadAddress);
    ServiceCall call(operation);
    const bool context = endpoint->handlesContext(operation);
    const bool large =
        operation == static_cast<uint64_t>(os::TaskOperation::Fork) ||
        operation == static_cast<uint64_t>(os::TaskOperation::ReplaceImage) ||
        operation == static_cast<uint64_t>(os::TaskOperation::SignalReturn);
    return context && large
               ? call.context(*endpoint, userRequest, userReply, deadline)
               : call.normal(*endpoint, userRequest, userReply, deadline,
                             context);
}

uint64_t handleReplyMessage(uint64_t endpointHandle, uint64_t userReply,
                            uint64_t, uint64_t, uint64_t, uint64_t) {
    auto endpointPin   = resolveEndpoint(endpointHandle, handleRightRead);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage reply{};
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (!copyFromUser(&reply, reinterpret_cast<const void*>(userReply),
                      sizeof(reply))) {
        return error(SyscallError::BadAddress);
    }
    return static_cast<uint64_t>(endpoint->reply(&reply));
}

uint64_t handleWaitAddress(uint64_t userAddress, uint64_t expected, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = sync::waitOnUserAddress(
        reinterpret_cast<volatile uint32_t*>(userAddress),
        static_cast<uint32_t>(expected));
    if (result < 0) return error(SyscallError::InvalidArgument);
    if (result > 0) return error(SyscallError::WouldBlock);
    return 0;
}

uint64_t handleWakeAddress(uint64_t userAddress, uint64_t count, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    return sync::wakeUserAddress(
        reinterpret_cast<volatile uint32_t*>(userAddress),
        static_cast<uint32_t>(count));
}

}  // namespace

uint64_t dispatchSyscall(uint64_t number, uint64_t a, uint64_t b, uint64_t c,
                         uint64_t d, uint64_t e, uint64_t f) {
    switch (static_cast<SyscallNumber>(number)) {
        case SyscallNumber::SendMessage:
            return handleSendMessage(a, b, c, d, e, f);
        case SyscallNumber::ReceiveMessage:
            return handleReceiveMessage(a, b, c, d, e, f);
        case SyscallNumber::CallService:
            return handleCallService(a, b, c, d, e, f);
        case SyscallNumber::ReplyMessage:
            return handleReplyMessage(a, b, c, d, e, f);
        case SyscallNumber::WaitAddress:
            return handleWaitAddress(a, b, c, d, e, f);
        case SyscallNumber::WakeAddress:
            return handleWakeAddress(a, b, c, d, e, f);
        default:
            return error(SyscallError::NotSupported);
    }
}

extern "C" void syscallDispatchEntry(SyscallContext* context) {
    Thread* thread = Thread::getCurrent();
    if (context == nullptr || thread == nullptr ||
        thread->getType() != ThreadType::User ||
        context->userRip >= maximumUserAddress ||
        context->userRsp >= maximumUserAddress) {
        signal::fatal(static_cast<uint32_t>(Signal::SegmentationViolation));
    }
    class ContextScope {
        Thread& owner_;

       public:
        ContextScope(Thread& owner, SyscallContext* context) : owner_(owner) {
            owner_.bindSyscallContext(context);
        }
        ~ContextScope() { owner_.clearSyscallContext(); }
    } scope(*thread, context);
    thread->lastUserContext = *context;
    thread->hasUserContext  = true;
    const uint64_t result =
        dispatchSyscall(context->rax, context->rdi, context->rsi, context->rdx,
                        context->r10, context->r8, context->r9);
    if (thread->contextRestored()) return;
    context->rax                    = result;
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    context->userRflags = (context->userRflags & allowedFlags) | 0x202;
    signal::deliverPendingSignal(context);
}

}  // namespace kernel::syscall
