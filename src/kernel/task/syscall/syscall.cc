/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/syscall.cc
 * 安全复制用户消息，分发系统调用并处理返回用户态的寄存器现场。
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

namespace task {

    // 这些 MSR 配置 x86-64
    // 硬件系统调用入口，不增加服务操作；用户范围排除低地址保护区及高半区。
    constexpr uint32_t ia32Efer           = 0xc0000080;
    constexpr uint32_t ia32Star           = 0xc0000081;
    constexpr uint32_t ia32Lstar          = 0xc0000082;
    constexpr uint32_t ia32Fmask          = 0xc0000084;
    constexpr uint64_t minimumUserAddress = 0x10000;
    constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
    constexpr uint64_t directMapBase      = 0xffff800000000000ULL;

    // 消息复制和 entry.S
    // 都依赖这些布局；前十五个字保存通用寄存器，最后三个保存返回用户态的信息，改顺序必须同步修改汇编。
    static_assert(sizeof(SignalAction) == sizeof(os::wire::SignalAction));
    static_assert(__is_standard_layout(SignalAction));
    static_assert(sizeof(SyscallContext) == 18 * sizeof(uint64_t));
    static_assert(__builtin_offsetof(SyscallContext, rax) == 14 * 8);
    static_assert(__builtin_offsetof(SyscallContext, userRip) == 15 * 8);

    /*
     * 把系统调用错误枚举转换为统一返回表示。
     */
    static uint64_t error(SyscallError value) {
        return static_cast<uint64_t>(static_cast<int64_t>(value));
    }

    /*
     * 检查用户指针范围是否落在合法空间，并防止地址加长度发生溢出。
     */
    static bool validUserRange(uint64_t address, uint64_t size) {
        if (size == 0 || address < minimumUserAddress) {
            return false;
        }

        const uint64_t end = address + size;

        return end > address && end <= maximumUserAddress;
    }

    /*
     * 按所需权限解析端点句柄并保留引用，避免调用期间端点被释放。
     */
    static HandleTable::Pin resolveEndpoint(uint64_t handle, uint32_t rights) {
        return HandleTable::Pin(getCurrent()->handles(), handle,
                                HandleType::Endpoint, rights);
    }

    /*
     * 配置 x86-64 系统调用入口寄存器和入口地址，使用户态 syscall
     * 指令进入内核公共入口。
     */
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

    /*
     * 安全读取用户缓冲区。
     * 先准备缺失页面，再持地址空间锁核对整段读取权限并复制；无效范围在实际访问前返回失败。
     */
    bool copyFromUser(void* kernelBuffer, const void* userBuffer, size_t size) {
        Task* task             = task::getCurrent();
        const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);

        if (kernelBuffer == nullptr || task == nullptr ||
            !(validUserRange(address, size) ||
              (task->isSystemService() && address >= directMapBase &&
               address + size > address))) {
            return false;
        }

        // 先请求内存服务准备尚未驻留的用户页，避免持地址空间锁执行拷贝时再进入补页服务。
        if (address < maximumUserAddress &&
            !mm::pagerBridge().ensureUserMemory(address, size, false)) {
            return false;
        }

        // 持地址空间锁重新核对整段权限，再逐字节读取；检查与读取期间不允许另一条映射修改穿插。
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

    /*
     * 安全写入用户缓冲区。先准备可写页面，再持地址空间锁检查整段权限并复制，保留写时复制处理。
     */
    bool copyToUser(void* userBuffer, const void* kernelBuffer, size_t size) {
        Task* task             = task::getCurrent();
        const uint64_t address = reinterpret_cast<uint64_t>(userBuffer);

        if (kernelBuffer == nullptr || task == nullptr ||
            !(validUserRange(address, size) ||
              (task->isSystemService() && address >= directMapBase &&
               address + size > address))) {
            return false;
        }

        // 写入前以可写要求准备页面，让写时复制先生成私有页；随后持地址空间锁核对并复制。
        if (address < maximumUserAddress &&
            !mm::pagerBridge().ensureUserMemory(address, size, true)) {
            return false;
        }

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

    /*
     * 从用户空间读取发送消息，检查端点权限后发送；带取消参数时交给相应取消路径处理。
     */
    static uint64_t handleSendMessage(uint64_t endpointHandle,
                                      uint64_t userMessage, uint64_t deadline,
                                      uint64_t cancelThreadId, uint64_t,
                                      uint64_t) {
        HandleTable::Pin endpointPin = resolveEndpoint(
            endpointHandle,
            !userMessage && cancelThreadId ? 0 : handleRightWrite);
        Endpoint* endpoint = endpointPin.as<Endpoint>();
        IpcMessage message{};

        if (endpoint == nullptr) {
            return error(SyscallError::InvalidHandle);
        }

        if (!userMessage && cancelThreadId) {
            return endpoint->cancel(cancelThreadId);
        }

        if (!copyFromUser(&message, reinterpret_cast<const void*>(userMessage),
                          sizeof(message))) {
            return error(SyscallError::BadAddress);
        }

        return static_cast<uint64_t>(endpoint->send(&message, deadline));
    }

    /*
     * 在内核缓冲区接收消息，再安全写回用户空间；写回失败时清理本次接收带来的句柄资源。
     */
    static uint64_t handleReceiveMessage(uint64_t endpointHandle,
                                         uint64_t userMessage,
                                         uint64_t deadline, uint64_t, uint64_t,
                                         uint64_t) {
        HandleTable::Pin endpointPin =
            resolveEndpoint(endpointHandle, handleRightRead);
        Endpoint* endpoint = endpointPin.as<Endpoint>();

        // 先把完整消息接收到内核临时对象，确认接收完成后再安全拷贝到用户提供的地址。
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

        if (result) {
            return static_cast<uint64_t>(result);
        }

        if (copyToUser(reinterpret_cast<void*>(userMessage), &message,
                       sizeof(message))) {
            return 0;
        }

        // 用户缓冲区写入失败时，已安装到接收者表里的附带句柄也需要关闭，避免只因坏指针造成资源泄漏。
        for (uint32_t i = 0; i < message.handleCount; ++i) {
            task->handles().close(message.handles[i]);
        }

        return error(SyscallError::BadAddress);
    }

    // 先记住分派时看到的操作号，完整重读请求后要求操作号一致，避免并发修改消息把普通路径换成现场操作路径。
    class ServiceCall {
            uint64_t operation_;

            /*
             * 完成一次服务调用并把回复写回用户地址。
             * 根据服务类别选择直接调用或普通调用，统一处理错误和回复句柄。
             */
            uint64_t complete(Endpoint& endpoint, uint64_t userRequest,
                              uint64_t userReply, uint64_t deadline,
                              IpcMessage& request, IpcMessage& reply,
                              bool immediate) {
                Task* task = task::getCurrent();

                if (task == nullptr ||
                    !copyFromUser(&request,
                                  reinterpret_cast<const void*>(userRequest),
                                  sizeof(request)) ||
                    !mm::pagerBridge().ensureUserMemory(userReply,
                                                        sizeof(reply), true)) {
                    return error(SyscallError::BadAddress);
                }

                if (request.type != operation_) {
                    return error(SyscallError::InvalidArgument);
                }

                const int result =
                    immediate ? endpoint.callImmediate(request, reply, deadline)
                              : endpoint.call(&request, &reply, deadline);

                if (currentThread()->contextRestored()) {
                    return 0;
                }

                if (result) {
                    return static_cast<uint64_t>(result);
                }

                if (copyToUser(reinterpret_cast<void*>(userReply), &reply,
                               sizeof(reply))) {
                    return 0;
                }

                for (uint32_t i = 0; i < reply.handleCount; ++i) {
                    task->handles().close(reply.handles[i]);
                }

                return error(SyscallError::BadAddress);
            }

        public:
            /*
             * 记录服务操作编号，供后续判断是否需要完整寄存器上下文。
             */
            explicit ServiceCall(uint64_t operation) : operation_(operation) {}

            /*
             * 处理普通服务调用，使用本地请求和回复缓冲区完成安全拷贝及端点调用。
             */
            __attribute__((noinline)) uint64_t normal(Endpoint& endpoint,
                                                      uint64_t requestAddress,
                                                      uint64_t replyAddress,
                                                      uint64_t deadline,
                                                      bool immediate) {
                IpcMessage request{}, reply{};

                return complete(endpoint, requestAddress, replyAddress,
                                deadline, request, reply, immediate);
            }

            /*
             * 处理需要修改返回现场的服务调用，保留当前系统调用上下文，供 fork
             * 和 exec 等操作使用。
             */
            __attribute__((noinline)) uint64_t context(Endpoint& endpoint,
                                                       uint64_t requestAddress,
                                                       uint64_t replyAddress,
                                                       uint64_t deadline) {
                class Buffers {
                    public:
                        /*
                         * 返回内部请求缓冲区引用，供公共调用路径填充。
                         */
                        IpcMessage& request() { return request_; }

                        /*
                         * 返回内部回复缓冲区引用，供服务结果写回使用。
                         */
                        IpcMessage& reply() { return reply_; }

                    private:
                        IpcMessage request_{}, reply_{};
                };

                ThreadCleanup cleanup;
                cpu::disablePreemption();
                void* storage    = mm::allocateHeap(sizeof(Buffers));
                Buffers* buffers = storage ? new (storage) Buffers : nullptr;

                if (buffers) {
                    cleanup.attach(buffers,
                                   [](void* value) { mm::freeHeap(value); });
                }

                cpu::enablePreemption();

                if (!buffers) {
                    return error(SyscallError::OutOfMemory);
                }

                return complete(endpoint, requestAddress, replyAddress,
                                deadline, buffers->request(), buffers->reply(),
                                true);
            }
    };

    /*
     * 安全读取服务请求，检查端点权限及操作性质，再选择普通或需要寄存器现场的调用路径。
     */
    static uint64_t handleCallService(uint64_t endpointHandle,
                                      uint64_t userRequest, uint64_t userReply,
                                      uint64_t deadline, uint64_t, uint64_t) {
        HandleTable::Pin pin =
            resolveEndpoint(endpointHandle, handleRightWrite);
        task::Endpoint* endpoint = pin.as<Endpoint>();

        if (!endpoint) {
            return error(SyscallError::InvalidHandle);
        }

        // 先读取操作编号来选择是否需要返回现场，随后完整读取时仍要核对编号没有被并发修改。
        uint64_t operation = 0;

        if (!copyFromUser(&operation, reinterpret_cast<void*>(userRequest),
                          sizeof(operation))) {
            return error(SyscallError::BadAddress);
        }

        ServiceCall call(operation);
        const bool context = endpoint->handlesContext(operation);
        const bool large =
            operation == static_cast<uint64_t>(os::TaskOperation::Fork) ||
            operation ==
                static_cast<uint64_t>(os::TaskOperation::ReplaceImage) ||
            operation == static_cast<uint64_t>(os::TaskOperation::SignalReturn);

        return context && large
                   ? call.context(*endpoint, userRequest, userReply, deadline)
                   : call.normal(*endpoint, userRequest, userReply, deadline,
                                 context);
    }

    /*
     * 读取用户回复消息并提交给端点，令牌和权限检查由端点层完成。
     */
    static uint64_t handleReplyMessage(uint64_t endpointHandle,
                                       uint64_t userReply, uint64_t, uint64_t,
                                       uint64_t, uint64_t) {
        HandleTable::Pin endpointPin =
            resolveEndpoint(endpointHandle, handleRightRead);
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

    /*
     * 校验用户等待地址和期望值，再进入地址等待机制。
     */
    static uint64_t handleWaitAddress(uint64_t userAddress, uint64_t expected,
                                      uint64_t deadline, uint64_t userMask,
                                      uint64_t, uint64_t) {
        if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
            return error(SyscallError::InvalidArgument);
        }

        if (userMask) {
            uint64_t mask = 0;
            if (!copyFromUser(&mask, reinterpret_cast<const void*>(userMask),
                              sizeof(mask)))
                return error(SyscallError::BadAddress);
            currentThread()->beginTemporarySignalMask(mask);
        }

        const int result = task::waitOnUserAddress(
            reinterpret_cast<volatile uint32_t*>(userAddress),
            static_cast<uint32_t>(expected), deadline);

        if (result == 2) return static_cast<uint64_t>(-9LL);
        if (result == -10) return static_cast<uint64_t>(-10LL);

        if (result < 0) {
            return error(SyscallError::InvalidArgument);
        }

        if (result > 0) {
            return error(SyscallError::WouldBlock);
        }

        return 0;
    }

    /*
     * 校验用户唤醒地址和数量，再唤醒同地址上的等待线程。
     */
    static uint64_t handleWakeAddress(uint64_t userAddress, uint64_t count,
                                      uint64_t, uint64_t, uint64_t, uint64_t) {
        if ((userAddress & (alignof(uint32_t) - 1)) != 0) {
            return error(SyscallError::InvalidArgument);
        }

        return task::wakeUserAddress(
            reinterpret_cast<volatile uint32_t*>(userAddress),
            static_cast<uint32_t>(count));
    }

    /* 就绪快照同时记录已处理信号，防止服务查询期间处理过信号后再次睡眠。 */
    static uint64_t handleReadiness(uint64_t operation, uint64_t userState,
                                    uint64_t deadline, uint64_t userMask) {
        Thread* thread = currentThread();
        if (!thread) return error(SyscallError::InvalidArgument);
        if (operation == 4) return task::readinessGeneration();
        if (operation == 2) {
            task::notifyReadiness();
            return 0;
        }
        if (operation == 3) {
            return thread->endReadinessMask(userState)
                       ? 0
                       : error(SyscallError::InvalidArgument);
        }
        OsReadinessState state{};
        if (operation == 0) {
            uint64_t mask = 0;
            if (userMask &&
                !copyFromUser(&mask, reinterpret_cast<const void*>(userMask),
                              sizeof(mask)))
                return error(SyscallError::BadAddress);
            state.generation       = task::readinessGeneration();
            state.signalGeneration = thread->signalGeneration;
            if (userMask) {
                state.token = thread->beginReadinessMask(mask);
                if (!state.token) return error(SyscallError::Busy);
            }
            if (!copyToUser(reinterpret_cast<void*>(userState), &state,
                            sizeof(state))) {
                thread->endReadinessMask(state.token);
                return error(SyscallError::BadAddress);
            }
            return 0;
        }
        if (operation != 1) return error(SyscallError::InvalidArgument);
        if (!copyFromUser(&state, reinterpret_cast<const void*>(userState),
                          sizeof(state)))
            return error(SyscallError::BadAddress);
        const int result = task::waitOnReadiness(state.generation, deadline,
                                                 state.signalGeneration);
        if (result == 2) return static_cast<uint64_t>(-9LL);
        if (result == -10) return static_cast<uint64_t>(-10LL);
        if (result < 0) return error(SyscallError::InvalidArgument);
        return result ? error(SyscallError::WouldBlock) : 0;
    }

    /*
     * 按系统调用号选择处理入口，未知编号返回明确错误，避免跳转到任意地址。
     */
    uint64_t dispatchSyscall(uint64_t number, uint64_t a, uint64_t b,
                             uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
        switch (static_cast<SyscallNumber>(number)) {
            case SyscallNumber::SendMessage:
                return handleSendMessage(a, b, c, d, e, f);
            case SyscallNumber::ReceiveMessage:
                return handleReceiveMessage(a, b, c, d, e, f);
            case SyscallNumber::CallService:
                return handleCallService(a, b, c, d, e, f);
            case SyscallNumber::ReplyMessage:
                return handleReplyMessage(a, b, c, d, e, f);
            case SyscallNumber::Readiness:
                return handleReadiness(a, b, c, d);
            case SyscallNumber::WaitAddress:
                return handleWaitAddress(a, b, c, d, e, f);
            case SyscallNumber::WakeAddress:
                return handleWakeAddress(a, b, c, d, e, f);
            default:
                return error(SyscallError::NotSupported);
        }
    }

    /*
     * 把入口保存的寄存器解释为系统调用参数，执行分发后写回返回值，并处理返回用户态之前的信号和调度。
     */
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
                /*
                 * 暂时登记当前系统调用现场，保存原记录以支持嵌套使用。
                 */
                ContextScope(Thread& owner, SyscallContext* context)
                    : owner_(owner) {
                    owner_.bindSyscallContext(context);
                }

                /*
                 * 恢复线程原来的系统调用现场指针，避免后续代码使用已离开作用域的现场。
                 */
                ~ContextScope() { owner_.clearSyscallContext(); }
        } scope(*thread, context);

        thread->lastUserContext = *context;
        thread->hasUserContext  = true;

        // 保存当前调用现场后再分发，fork、exec
        // 和信号返回才能找到本次完整寄存器。
        const uint64_t result = dispatchSyscall(
            context->rax, context->rdi, context->rsi, context->rdx,
            context->r10, context->r8, context->r9);

        // 若处理路径已经恢复了新用户现场，就不能用普通返回值和旧回复再次覆盖它。
        if (thread->contextRestored()) {
            return;
        }

        // 只有普通完成路径写返回值、清理用户标志，然后才允许待处理信号替换返回执行位置。
        context->rax                    = result;
        constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
        context->userRflags = (context->userRflags & allowedFlags) | 0x202;
        task::deliverPendingSignal(context);
        thread->finishTemporarySignalMask();
    }

}  // namespace task
