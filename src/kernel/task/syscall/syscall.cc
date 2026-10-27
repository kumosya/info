/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/syscall.cc
 * System call validation, message copies, and user-context completion.
 */

#include "kernel/syscall.h"

#include <new>

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/device.h>

namespace kernel::task {

namespace {

// Native x86-64 syscall MSRs; these configure the hardware entry rather than
// define additional service operations. User address checks exclude the null
// guard area and the noncanonical/high half.
constexpr uint32_t ia32Efer           = 0xc0000080;
constexpr uint32_t ia32Star           = 0xc0000081;
constexpr uint32_t ia32Lstar          = 0xc0000082;
constexpr uint32_t ia32Fmask          = 0xc0000084;
constexpr uint64_t minimumUserAddress = 0x10000;
constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
constexpr uint64_t directMapBase      = 0xffff800000000000ULL;

// These layouts are consumed by message copies and entry.S. In particular, the
// first fifteen words are GPRs and the final three are the user IRET
// continuation; changing member order requires matching assembly changes.
static_assert(sizeof(SignalAction) == sizeof(os::wire::SignalAction));
static_assert(__is_standard_layout(SignalAction));
static_assert(sizeof(SyscallContext) == 18 * sizeof(uint64_t));
static_assert(__builtin_offsetof(SyscallContext, rax) == 14 * 8);
static_assert(__builtin_offsetof(SyscallContext, userRip) == 15 * 8);

// Preserve negative native errno bits in the unsigned RAX result.
uint64_t error(SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}

// Reject zero length, low guard addresses, arithmetic wrap and high-half
// ranges before attempting pager resolution or direct memory access.
bool validUserRange(uint64_t address, uint64_t size) {
    if (size == 0 || address < minimumUserAddress) {
        return false;
    }
    const uint64_t end = address + size;
    return end > address && end <= maximumUserAddress;
}

// Pin under the calling task's handle-table lock. The returned guard holds the
// endpoint alive across message copies, paging and a blocking call.
HandleTable::Pin resolveEndpoint(uint64_t handle, uint32_t rights) {
    return HandleTable::Pin(getCurrent()->handles(), handle,
                            HandleType::Endpoint, rights);
}

}  // namespace

// Configure this CPU's syscall entry and masked flags. Return uses IRET, not
// SYSRET, because complete restored user state includes RCX/R11 and
// signal-editable flag combinations. APs configure their own MSRs too.
void initSyscall() {
    constexpr uint64_t kernelCodeSelector = 0x08;
    constexpr uint64_t userSelectorBase   = 0x10;
    cpu::writeMsr(ia32Efer, cpu::readMsr(ia32Efer) | 1);
    cpu::writeMsr(ia32Star,
                  (userSelectorBase << 48) | (kernelCodeSelector << 32));
    cpu::writeMsr(ia32Lstar, reinterpret_cast<uint64_t>(syscallEntry));
    cpu::writeMsr(ia32Fmask,
                  (1ULL << 8) | (1ULL << 9) | (1ULL << 10) | (1ULL << 18));
    device::serialWrite("[SYSCALL] SYSCALL/IRET initialized\n");
}

// Validate the caller range and prepare legitimate lazy pages before taking
// the address-space lock. Read validation under that lock prevents copying
// from unmapped or supervisor-only pages. The legacy Sys direct-map branch is
// privileged compatibility behavior, not ordinary Ring3 authorization.
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
        !mm::pagerBridge().ensureUserMemory(address, size, false))
        return false;
    PreemptionSpinLockGuard guard(task->addressSpace().lock());
    if (!mm::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
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

// Resolve writable/COW backing before locking the root, then revalidate and
// copy. Service replies and signal frames must obey the same write checks as
// user stores rather than bypassing COW through physical memory.
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
        !mm::pagerBridge().ensureUserMemory(address, size, true))
        return false;
    PreemptionSpinLockGuard guard(task->addressSpace().lock());
    if (!mm::validateUserMemory(task->addressSpace().rootPhysicalAddress(),
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

// A null message with a nonzero thread ID selects same-process cancellation.
// Otherwise copy the whole message into kernel storage before enqueueing; no
// endpoint may later dereference the sender's mutable message buffer.
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

// Prepare the output mapping before consuming a queue entry. If final user
// copy still fails, close every just-installed reply handle rather than
// leaking capabilities into a caller that never received their numbers.
uint64_t handleReceiveMessage(uint64_t endpointHandle, uint64_t userMessage,
                              uint64_t deadline, uint64_t, uint64_t, uint64_t) {
    auto endpointPin   = resolveEndpoint(endpointHandle, handleRightRead);
    Endpoint* endpoint = endpointPin.as<Endpoint>();
    IpcMessage message{};
    Task* task = task::getCurrent();
    if (endpoint == nullptr) {
        return error(SyscallError::InvalidHandle);
    }
    if (task == nullptr || !mm::pagerBridge().ensureUserMemory(
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

// Own the operation observed before choosing the dispatch path. Re-read the
// complete request and require the same operation, preventing a concurrently
// modified message header from switching between normal and context paths.
class ServiceCall {
    uint64_t operation_;
    // Copy both message and output permission state before entering the
    // service. A successful exec/signal return invalidates the old reply
    // destination; contextRestored therefore takes precedence over ordinary
    // reply copying.
    uint64_t complete(Endpoint& endpoint, uint64_t userRequest,
                      uint64_t userReply, uint64_t deadline,
                      IpcMessage& request, IpcMessage& reply, bool immediate) {
        Task* task = task::getCurrent();
        if (task == nullptr ||
            !copyFromUser(&request, reinterpret_cast<const void*>(userRequest),
                          sizeof(request)) ||
            !mm::pagerBridge().ensureUserMemory(userReply, sizeof(reply),
                                                true)) {
            return error(SyscallError::BadAddress);
        }
        if (request.type != operation_)
            return error(SyscallError::InvalidArgument);
        const int result =
            immediate ? endpoint.callImmediate(request, reply, deadline)
                      : endpoint.call(&request, &reply, deadline);
        if (currentThread()->contextRestored()) return 0;
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
    // Keep ordinary stack message buffers out of the large-context path. These
    // entry frames stay separate so deep fork/exec work does not also inherit
    // two full stack-sized IpcMessage objects.
    __attribute__((noinline)) uint64_t normal(Endpoint& endpoint,
                                              uint64_t requestAddress,
                                              uint64_t replyAddress,
                                              uint64_t deadline,
                                              bool immediate) {
        IpcMessage request{}, reply{};
        return complete(endpoint, requestAddress, replyAddress, deadline,
                        request, reply, immediate);
    }
    // Allocate the two fixed messages on the heap for deep context operations.
    // Register allocation cleanup before re-enabling preemption; a killed
    // caller must not strand this memory even when no destructor is reached.
    __attribute__((noinline)) uint64_t context(Endpoint& endpoint,
                                               uint64_t requestAddress,
                                               uint64_t replyAddress,
                                               uint64_t deadline) {
        class Buffers {
           public:
            IpcMessage& request() { return request_; }
            IpcMessage& reply() { return reply_; }

           private:
            IpcMessage request_{}, reply_{};
        };
        ThreadCleanup cleanup;
        cpu::disablePreemption();
        void* storage = mm::allocateHeap(sizeof(Buffers));
        auto* buffers = storage ? new (storage) Buffers : nullptr;
        if (buffers)
            cleanup.attach(buffers, [](void* value) { mm::freeHeap(value); });
        cpu::enablePreemption();
        if (!buffers) return error(SyscallError::OutOfMemory);
        return complete(endpoint, requestAddress, replyAddress, deadline,
                        buffers->request(), buffers->reply(), true);
    }
};
// Resolve the real endpoint before classifying the operation. Only a resident
// task endpoint can request caller-context completion; a user endpoint using
// the same numeric type remains an ordinary RPC.
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

// The explicit replyToken comes from the copied message. Receive rights
// identify the server side; Endpoint validates the claiming task and token.
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

// Require a naturally aligned 32-bit synchronization word and translate the
// internal compare result into native syscall errors. There is no deadline
// argument or kernel-side priority-inheritance operation in this entry.
uint64_t handleWaitAddress(uint64_t userAddress, uint64_t expected, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    const int result = task::waitOnUserAddress(
        reinterpret_cast<volatile uint32_t*>(userAddress),
        static_cast<uint32_t>(expected));
    if (result < 0) return error(SyscallError::InvalidArgument);
    if (result > 0) return error(SyscallError::WouldBlock);
    return 0;
}

// Return how many physical-key waiters were removed, not a boolean success.
uint64_t handleWakeAddress(uint64_t userAddress, uint64_t count, uint64_t,
                           uint64_t, uint64_t, uint64_t) {
    if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
        return error(SyscallError::InvalidArgument);
    }
    return task::wakeUserAddress(
        reinterpret_cast<volatile uint32_t*>(userAddress),
        static_cast<uint32_t>(count));
}

}  // namespace

// The ABI is sparse: only 1..4 and 18..19 exist. Count is the number of
// supported calls, not an array bound for their largest numeric identifier.
// Business operations are messages to core/user services, not extra syscalls.
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

// Bind the saved user frame to its actual current thread for inline task
// mechanisms. Scope exit clears that binding on ordinary return. Context
// restoration suppresses both the normal RAX result and pending-signal pass,
// which would otherwise overwrite a newly installed exec/signal frame.
extern "C" void syscallDispatchEntry(SyscallContext* context) {
    Thread* thread = currentThread();
    if (context == nullptr || thread == nullptr ||
        thread->getType() != ThreadType::User ||
        context->userRip >= maximumUserAddress ||
        context->userRsp >= maximumUserAddress) {
        task::fatal(static_cast<uint32_t>(Signal::SegmentationViolation));
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
    // Only the ordinary completion path writes the syscall result, sanitizes
    // user flags, then allows one pending handler to replace the continuation.
    context->rax                    = result;
    constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
    context->userRflags = (context->userRflags & allowedFlags) | 0x202;
    task::deliverPendingSignal(context);
}

}  // namespace kernel::task
