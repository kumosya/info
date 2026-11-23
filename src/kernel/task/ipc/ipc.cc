/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/ipc.cc
 * 端点消息排队、同步请求回复、超时与线程退出取消。
 */

#include "kernel/ipc.h"

#include <new>

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/mm.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "kernel/time.h"

namespace task {

    // 每个端点保存八条普通先进先出消息；IRQ
    // 另有待处理位图，普通队列满不能遮蔽设备通知。
    constexpr uint32_t queueCapacity = 8;
    constexpr int waiting            = 1;

    /*
     * 把错误枚举转换成 IPC 接口约定的负错误码。
     */
    static constexpr int failure(os::ErrorCode error) {
        return static_cast<int>(error);
    }

    // 等待记录位于调用线程栈上，同时挂入端点和可选定时链；完成结果只能确定一次，栈释放前必须摘除所有链接。
    struct Waiter {
            Thread* thread;
            Waiter* next      = nullptr;
            Waiter* timerNext = nullptr;
            uint64_t deadline;
            int result = waiting;
            bool timed = false;
    };

    // 同步调用记录由调用者持有，令牌标识请求；服务任务用于回复授权，服务线程用于退出检测；回复目标始终是调用者的内核缓冲区。
    struct PendingCall {
            Thread* caller;
            Task* callerTask;
            Task* serverTask     = nullptr;
            Thread* serverThread = nullptr;
            uint64_t token;
            IpcMessage* replyTarget;
            uint64_t deadline;
            PendingCall* next = nullptr;
            int result        = waiting;
    };

    // 队列先持有消息捕获的引用，接收时才安装句柄；同步调用指针指向调用者记录，取消时必须在栈消失前移除。
    struct QueuedMessage {
            IpcMessage message{};
            HandleTable::Transfer handles;
            PendingCall* call = nullptr;
    };

    // 显式授权只供可信内核调用者使用，普通进程通过受限句柄证明访问资格。
    struct AccessGrant {
            Task* task;
            uint32_t rights;
            AccessGrant* next;
    };

    static SpinLock timerLock;
    static Waiter* timers;
    static SpinLock endpointListLock;
    static EndpointImplementation* endpoints;
    static Atomic<uint64_t> nextEndpointId{1};
    static Atomic<uint64_t> nextReplyToken{1};

    // 操作期间固定活动端点并登记线程清理；补页调用可能嵌套，所以正常返回要恢复原端点，强制退出也要归还引用。
    class Operation {
        public:
            /*
             * 登记当前线程正在使用的端点并保留引用，记住之前的活动端点，支持调用嵌套和强制退出清理。
             */
            explicit Operation(Endpoint* endpoint)
                : endpoint_(endpoint),
                  thread_(currentThread()),
                  previous_(thread_->getActiveEndpoint()) {
                endpoint_->retain();
                cleanup_.attach(endpoint_, [](void* object) {
                    static_cast<Endpoint*>(object)->release();
                });
                thread_->setActiveIpcCall(true);
                thread_->setActiveEndpoint(endpoint_);
            }

            /*
             * 恢复线程原来的活动端点，并由清理记录归还本次端点引用。
             */
            ~Operation() {
                thread_->setActiveEndpoint(previous_);
                thread_->setActiveIpcCall(previous_ != nullptr);
            }

        private:
            Endpoint* endpoint_;
            Thread* thread_;
            Endpoint* previous_;
            ThreadCleanup cleanup_;
    };

    /*
     * 给等待者写入完成结果并唤醒线程。
     * 调用者负责先把等待者从所属队列摘下。
     */
    static void complete(Waiter& waiter, int result) {
        int expected = waiting;

        if (__atomic_compare_exchange_n(&waiter.result, &expected, result,
                                        false, __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            wakeThread(waiter.thread);
        }
    }

    /*
     * 为有截止时间的等待登记计时器，使没有消息到达时也能及时结束等待。
     */
    static void armTimer(Waiter& waiter) {
        if (waiter.deadline == os::ipcInfiniteDeadline) {
            return;
        }

        InterruptSpinLockGuard guard(timerLock);
        Waiter** link = &timers;

        while (*link && (*link)->deadline <= waiter.deadline) {
            link = &(*link)->timerNext;
        }

        waiter.timerNext = *link;
        *link            = &waiter;
        waiter.timed     = true;
    }

    /*
     * 撤销等待计时器，防止已经完成的请求后来又被超时路径处理。
     */
    static void disarmTimer(Waiter& waiter) {
        InterruptSpinLockGuard guard(timerLock);

        if (!waiter.timed) {
            return;
        }

        Waiter** link = &timers;

        while (*link && *link != &waiter) {
            link = &(*link)->timerNext;
        }

        if (*link) {
            *link = waiter.timerNext;
        }

        waiter.timed     = false;
        waiter.timerNext = nullptr;
    }

    /*
     * 从单向等待链表摘下指定节点，保持其余等待顺序。
     */
    static void removeWaiter(Waiter*& head, Waiter& waiter) {
        Waiter** link = &head;

        while (*link && *link != &waiter) {
            link = &(*link)->next;
        }

        if (*link) {
            *link = waiter.next;
        }

        waiter.next = nullptr;
    }

    /*
     * 把当前等待节点登记到队列和线程清理状态中，使取消路径能找到它。
     */
    static void prepareWaiter(Waiter*& head, Waiter& waiter) {
        prepareCurrentWait();
        Waiter** tail = &head;

        while (*tail) {
            tail = &(*tail)->next;
        }

        *tail = &waiter;
        armTimer(waiter);
    }

    /*
     * 从等待链表取出一个可唤醒节点，完成后让对应线程继续运行。
     */
    static void wakeOne(Waiter*& head) {
        while (head) {
            Waiter* waiter = head;
            head           = waiter->next;
            waiter->next   = nullptr;

            if (__atomic_load_n(&waiter->result, __ATOMIC_ACQUIRE) != waiting) {
                continue;
            }

            complete(*waiter, 0);

            return;
        }
    }

    /*
     * 检查消息指针和长度等基本条件，拒绝超出固定消息容量的数据。
     */
    static bool validateMessage(const IpcMessage* message) {
        return message && message->length <= os::ipcDataWordCount &&
               message->handleCount <= 4;
    }

    /*
     * 由内核写入发送者身份，避免用户消息伪造来源任务和用户编号。
     */
    static void stampSender(IpcMessage& message) {
        Task* task        = task::getCurrent();
        Thread* thread    = currentThread();
        message.senderPid = task ? task->id() : 0;
        message.senderTid = thread ? thread->getId() : 0;
    }

    /*
     * 检查截止时间是否已经过去，并转换成接口约定的超时结果。
     */
    static int deadlineError(uint64_t deadline) {
        if (!deadline) {
            return failure(os::ErrorCode::WouldBlock);
        }

        return deadline != os::ipcInfiniteDeadline &&
                       time::getMonotonicTime() >= deadline
                   ? failure(os::ErrorCode::Timeout)
                   : 0;
    }

    bool hasRightLocked(const Endpoint* endpoint, Task* task, uint32_t right);
    void unlinkCall(EndpointImplementation& state, PendingCall& call);
    void closeLocked(EndpointImplementation& state, os::ErrorCode error,
                     bool permanent = true);
    void discardCallMessage(EndpointImplementation& state, PendingCall& call);
    int enqueue(Endpoint* endpoint, IpcMessage& message, PendingCall* call,
                uint64_t deadline, bool kernelOrigin = false);

    // 内部状态由一把锁保护消息槽、关闭状态、授权和未完成请求；发送、接收和调用等待各有队列。
    class EndpointImplementation {
        public:
            /*
             * 返回端点内部状态锁，供公共操作保护消息和等待队列。
             */
            SpinLock& getLock() { return lock; }

        private:
            friend class Endpoint;
            friend bool hasRightLocked(const Endpoint*, Task*, uint32_t);
            friend void unlinkCall(EndpointImplementation&, PendingCall&);
            friend void closeLocked(EndpointImplementation&, os::ErrorCode,
                                    bool);
            friend void discardCallMessage(EndpointImplementation&,
                                           PendingCall&);
            friend int enqueue(Endpoint*, IpcMessage&, PendingCall*, uint64_t,
                               bool);
            SpinLock lock;
            Endpoint* endpoint;
            EndpointService* service = nullptr;
            EndpointImplementation* next;
            QueuedMessage queue[queueCapacity];
            uint32_t head        = 0;
            uint32_t count       = 0;
            Waiter* receivers    = nullptr;
            Waiter* senders      = nullptr;
            Waiter* callers      = nullptr;
            PendingCall* calls   = nullptr;
            AccessGrant* grants  = nullptr;
            bool destroyed       = false;
            uint16_t pendingIrqs = 0;
    };

    /*
     * 在端点锁内检查任务是否拥有指定访问权，结合句柄授权和端点归属判断。
     */
    bool hasRightLocked(const Endpoint* endpoint, Task* task, uint32_t right) {
        if (!task) {
            return false;
        }

        const uint32_t rights =
            ((right & endpointRightSend) ? handleRightWrite : 0) |
            ((right & endpointRightReceive) ? handleRightRead : 0) |
            ((right & endpointRightManage) ? handleRightManage : 0);

        if (task->handles().hasObjectRights(const_cast<Endpoint*>(endpoint),
                                            HandleType::Endpoint, rights)) {
            return true;
        }

        // 显式授权仅用于可信的内核调用者，包括内部检查路径。
        for (AccessGrant* grant = endpoint->getImplementation()->grants; grant;
             grant              = grant->next) {
            if (grant->task == task && (grant->rights & right) == right) {
                return true;
            }
        }

        return task == endpoint->getOwner();
    }

    /*
     * 从端点的未完成调用链表中摘下请求，防止回复、取消和超时再次找到同一请求。
     */
    void unlinkCall(EndpointImplementation& state, PendingCall& call) {
        PendingCall** link = &state.calls;

        while (*link && *link != &call) {
            link = &(*link)->next;
        }

        if (*link) {
            *link = call.next;
        }

        call.next = nullptr;
    }

    /*
     * 把端点改为关闭或暂时失联状态，结束排队请求并唤醒等待者。
     * 是否允许恢复由 permanent 参数决定。
     */
    void closeLocked(EndpointImplementation& state, os::ErrorCode error,
                     bool permanent) {
        if (state.destroyed) {
            return;
        }

        state.destroyed = permanent;
        Waiter* lists[] = {state.receivers, state.senders, state.callers};

        for (Waiter* list : lists) {
            for (Waiter* waiter = list; waiter; waiter = waiter->next) {
                complete(*waiter, failure(error));
            }
        }

        for (PendingCall* call = state.calls; call; call = call->next) {
            call->result = failure(error);
        }

        for (uint32_t i = 0; i < state.count; ++i) {
            QueuedMessage& queued =
                state.queue[(state.head + i) % queueCapacity];
            queued.handles.clear();
            queued = {};
        }

        state.count       = 0;
        state.pendingIrqs = 0;
    }

    /*
     * 删除已经入队但不应再交付的调用消息，同时清理该消息持有的对象引用。
     */
    void discardCallMessage(EndpointImplementation& state, PendingCall& call) {
        for (uint32_t i = 0; i < state.count; ++i) {
            const uint32_t index = (state.head + i) % queueCapacity;

            if (state.queue[index].call != &call) {
                continue;
            }

            state.queue[index].handles.clear();

            for (uint32_t j = i; j + 1 < state.count; ++j) {
                state.queue[(state.head + j) % queueCapacity] =
                    state.queue[(state.head + j + 1) % queueCapacity];
            }

            QueuedMessage& last =
                state.queue[(state.head + state.count - 1) % queueCapacity];
            last = {};
            --state.count;
            wakeOne(state.senders);

            return;
        }
    }

    /*
     * 将消息及其句柄引用加入端点队列。
     * 队列满时按截止时间等待，醒来后重新检查端点和容量，避免把唤醒当成成功保证。
     */
    int enqueue(Endpoint* endpoint, IpcMessage& message, PendingCall* call,
                uint64_t deadline, bool kernelOrigin) {
        EndpointImplementation& state = *endpoint->getImplementation();
        Task* sender                  = task::getCurrent();

        while (true) {
            // 每轮使用栈上等待记录；离开这一轮前必须摘掉端点链和计时链，不能把悬空栈指针留在队列。
            Waiter waiter{currentThread(), nullptr, nullptr, deadline};
            {
                InterruptSpinLockGuard guard(state.lock);

                if (state.destroyed) {
                    return failure(os::ErrorCode::EndpointClosed);
                }

                if (!endpoint->getOwner()) {
                    return failure(os::ErrorCode::PeerDied);
                }

                if (!kernelOrigin &&
                    !hasRightLocked(endpoint, sender, endpointRightSend)) {
                    return failure(os::ErrorCode::PermissionDenied);
                }

                // 有空槽时先捕获消息附带对象引用，再提交消息内容，队列因此独立于发送方句柄的后续关闭。
                if (state.count < queueCapacity) {
                    QueuedMessage& queued =
                        state.queue[(state.head + state.count) % queueCapacity];

                    if (!queued.handles.capture(sender->handles(), message)) {
                        return failure(os::ErrorCode::InvalidHandle);
                    }

                    queued.message = message;
                    queued.call    = call;
                    ++state.count;

                    // 同步调用还要挂入未完成调用表，后续服务回复、取消和超时都通过该记录找到调用者。
                    if (call) {
                        call->next  = state.calls;
                        state.calls = call;
                    }

                    wakeOne(state.receivers);

                    return 0;
                }

                const int error = deadlineError(deadline);

                if (error) {
                    return error;
                }

                // 队列满时在同一把锁下登记等待，避免接收者刚腾出位置却错过发送者的睡眠登记。
                prepareWaiter(state.senders, waiter);
            }

            // 放开端点锁后才睡眠；醒来先拆除旧等待登记，再回到循环重新检查容量。
            blockWaitingThread();
            {
                InterruptSpinLockGuard guard(state.lock);
                removeWaiter(state.senders, waiter);
                disarmTimer(waiter);
            }

            const int result =
                __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);

            if (result < 0) {
                return result;
            }
        }
    }

    /*
     * 初始化端点的对象字段和内部状态，供创建路径发布使用。
     */
    void Endpoint::init() {
        timerLock.init();
        endpointListLock.init();
        device::serialWrite("[IPC] endpoint IPC initialized\n");
    }

    /*
     * 增加端点引用，保证通信期间端点对象不会被释放。
     */
    void Endpoint::retain() {
        __atomic_add_fetch(&references, 1U, __ATOMIC_RELAXED);
    }

    /*
     * 归还端点引用，最后一份引用触发端点销毁。
     */
    void Endpoint::release() {
        __atomic_sub_fetch(&references, 1U, __ATOMIC_RELEASE);
    }

    /*
     * 持端点锁查询是否已经关闭，避免读到并发关闭的中间状态。
     */
    bool Endpoint::isClosed() {
        InterruptSpinLockGuard guard(implementation->getLock());

        return implementation->destroyed;
    }

    /*
     * 处理任务失去接收端句柄的情况，按接收者归属决定是否关闭或交给恢复机制。
     */
    void Endpoint::receiverClosed(Task* task) {
        InterruptSpinLockGuard guard(implementation->getLock());

        if (getOwner() == task) {
            closeLocked(*implementation, os::ErrorCode::EndpointClosed);
        }
    }

    /*
     * 分配端点和实现状态，初始化服务入口；失败时按分配顺序释放已取得的存储。
     */
    Endpoint* Endpoint::create(EndpointService* service) {
        Task* owner = task::getCurrent();

        if (!owner) {
            return nullptr;
        }

        void* storage        = mm::allocateHeap(sizeof(Endpoint));
        void* implementation = mm::allocateHeap(sizeof(EndpointImplementation));

        if (!storage || !implementation) {
            mm::freeHeap(storage);
            mm::freeHeap(implementation);

            return nullptr;
        }

        // 先分别取得对象和内部状态存储，任一步失败都应回收尚未发布的部分。
        Endpoint* endpoint = new (storage) Endpoint;
        EndpointImplementation* state =
            new (implementation) EndpointImplementation{};

        // 初始化完成后才安装服务入口与拥有者，避免其他路径看见未准备好的队列状态。
        state->lock.init();
        state->endpoint = endpoint;
        state->service  = service;
        endpoint->setId(nextEndpointId.fetchAdd(1, MemoryOrder::Relaxed));
        endpoint->setOwner(owner);
        endpoint->setImplementation(state);
        InterruptSpinLockGuard guard(endpointListLock);
        state->next = endpoints;
        endpoints   = state;

        return endpoint;
    }

    /*
     * 结束端点尚未完成的通信并释放内部存储。
     * 只有对象不再被引用时才进入最终销毁。
     */
    void Endpoint::destroy() {
        EndpointImplementation& state = *getImplementation();
        InterruptSpinLockGuard guard(state.lock);

        if (state.destroyed ||
            !hasRightLocked(this, task::getCurrent(), endpointRightManage)) {
            return;
        }

        closeLocked(state, os::ErrorCode::EndpointClosed);

        if (getOwner()) {
            setOwner(nullptr);
            release();
        }
    }

    /*
     * 通过请求令牌检查同步调用是否仍在等待，供服务避免向已经取消的调用提交结果。
     */
    bool Endpoint::requestActive(uint64_t token) {
        EndpointImplementation& state = *getImplementation();
        InterruptSpinLockGuard guard(state.lock);

        for (PendingCall* call = state.calls; call; call = call->next) {
            if (call->token == token &&
                call->serverTask == task::getCurrent()) {
                return call->result == waiting &&
                       !deadlineError(call->deadline);
            }
        }

        return false;
    }

    /*
     * 关闭属于某任务的接收端。
     * 恢复保护的端点保留必要身份，普通端点则结束等待和消息。
     */
    void Endpoint::closeOwner(Task* owner) {
        InterruptSpinLockGuard listGuard(endpointListLock);

        // 逐个端点处理所属任务退出；每次在该端点锁内关闭通信并清除该任务的显式授权。
        for (EndpointImplementation* state = endpoints; state;
             state                         = state->next) {
            InterruptSpinLockGuard guard(state->lock);

            if (state->endpoint->getOwner() == owner) {
                closeLocked(*state, os::ErrorCode::PeerDied,
                            state->endpoint->recoveryRole == 0);
                state->endpoint->setOwner(nullptr);
                state->endpoint->release();
            }

            AccessGrant** link = &state->grants;

            while (*link) {
                AccessGrant* grant = *link;

                if (grant->task != owner) {
                    link = &grant->next;
                    continue;
                }

                *link = grant->next;
                mm::freeHeap(grant);
            }
        }
    }

    /*
     * 将端点置于恢复保护之下，为服务重启保留接收者身份和通信对象。
     */
    bool Endpoint::protectRecovery(Task& task) {
        InterruptSpinLockGuard guard(implementation->getLock());
        const uint32_t role = static_cast<uint32_t>(task.serviceRole());

        if (implementation->destroyed || implementation->service ||
            getOwner() != &task || !task.managedService() || !role ||
            (recoveryRole && recoveryRole != role)) {
            return false;
        }

        recoveryRole = role;

        return true;
    }

    /*
     * 让重启后的服务接管原受保护端点，重新建立接收者归属。
     */
    bool Endpoint::adoptRecovery(Task& task) {
        InterruptSpinLockGuard guard(implementation->getLock());

        if (implementation->destroyed || !recoveryRole ||
            recoveryRole != static_cast<uint32_t>(task.serviceRole()) ||
            (getOwner() && getOwner() != &task)) {
            return false;
        }

        if (!getOwner()) {
            retain();
            setOwner(&task);
        }

        return true;
    }

    /*
     * 解除端点的恢复保护并归还代管引用，之后按普通引用规则回收。
     */
    void Endpoint::releaseRecovery(Task& task) {
        InterruptSpinLockGuard guard(implementation->getLock());

        if (recoveryRole == static_cast<uint32_t>(task.serviceRole())) {
            recoveryRole = 0;
        }
    }

    /*
     * 为任务登记端点访问权限。
     * 已经存在的授权按规则合并，新授权需要空闲记录。
     */
    bool Endpoint::grantAccess(Task* task, uint32_t rights) {
        if (!task || (rights & ~(endpointRightSend | endpointRightReceive |
                                 endpointRightManage))) {
            return false;
        }

        EndpointImplementation& state = *getImplementation();
        InterruptSpinLockGuard guard(state.lock);

        if (state.destroyed ||
            !hasRightLocked(this, task::getCurrent(), endpointRightManage)) {
            return false;
        }

        AccessGrant* grant =
            static_cast<AccessGrant*>(mm::allocateHeap(sizeof(AccessGrant)));

        if (!grant) {
            return false;
        }

        *grant       = {task, rights, state.grants};
        state.grants = grant;

        return true;
    }

    /*
     * 复制端点句柄时同步处理接收者身份和权限，避免仅复制槽位却遗漏端点层面的归属关系。
     */
    uint64_t Endpoint::duplicateHandle(Task* source, Task* destination,
                                       uint64_t handle,
                                       uint64_t destinationHandle) {
        if (!source || !destination) {
            return 0;
        }

        // fork 和引导继承可以要求保持原句柄编号。
        return source->handles().copyTo(destination->handles(), handle,
                                        destinationHandle);
    }

    /*
     * 发送单向消息。
     * 校验消息和发送权限，补写发送者身份后加入端点队列。
     */
    int Endpoint::send(IpcMessage* message, uint64_t deadline) {
        Operation operation(this);

        if (getImplementation()->service) {
            return failure(os::ErrorCode::NotSupported);
        }

        if (!validateMessage(message) || !getImplementation()) {
            return failure(os::ErrorCode::InvalidArgument);
        }

        IpcMessage copy = *message;
        copy.replyToken = 0;
        stampSender(copy);

        return enqueue(this, copy, nullptr, deadline);
    }

    /*
     * 接收一条消息并把附带引用安装成当前任务的句柄。
     * 没有消息时等待；同步调用还要维护供后续回复使用的令牌。
     */
    int Endpoint::receive(IpcMessage* message, uint64_t deadline) {
        Operation operation(this);

        if (getImplementation()->service) {
            return failure(os::ErrorCode::NotSupported);
        }

        if (!message || !getImplementation()) {
            return failure(os::ErrorCode::InvalidArgument);
        }

        EndpointImplementation& state = *getImplementation();

        while (true) {
            Waiter waiter{currentThread(), nullptr, nullptr, deadline};
            {
                InterruptSpinLockGuard guard(state.lock);

                if (state.destroyed) {
                    return failure(os::ErrorCode::EndpointClosed);
                }

                if (!hasRightLocked(this, task::getCurrent(),
                                    endpointRightReceive)) {
                    return failure(os::ErrorCode::PermissionDenied);
                }

                if (recoveryRole && getOwner() != task::getCurrent()) {
                    return failure(os::ErrorCode::PermissionDenied);
                }

                // 中断通知有独立位图，优先取出一个待处理
                // IRQ，普通消息队列满也不能挡住它。
                if (state.pendingIrqs) {
                    const uint32_t irq = __builtin_ctz(state.pendingIrqs);
                    state.pendingIrqs &= static_cast<uint16_t>(~(1U << irq));
                    *message         = {};
                    message->type    = 0x495251;
                    message->length  = 1;
                    message->data[0] = irq;

                    return 0;
                }

                // 先把附带引用安装成接收者自己的句柄，安装成功才允许把消息从队列取走。
                if (state.count) {
                    QueuedMessage& queued = state.queue[state.head];
                    // 目标句柄安装失败时保留队列消息，不向接收者公开半份句柄数组，允许稍后重试。
                    if (!queued.handles.install(task::getCurrent()->handles(),
                                                queued.message)) {
                        return failure(os::ErrorCode::OutOfMemory);
                    }

                    *message = queued.message;

                    // 同步请求首次交给服务时记录领取任务和线程，回复授权与服务线程退出检测都需要它们。
                    if (queued.call) {
                        queued.call->serverTask   = task::getCurrent();
                        queued.call->serverThread = currentThread();
                    }

                    // 消息内容交付完再推进环形队首并唤醒发送者，腾出的槽位此时才能复用。
                    queued     = {};
                    state.head = (state.head + 1) % queueCapacity;
                    --state.count;
                    wakeOne(state.senders);

                    return 0;
                }

                const int error = deadlineError(deadline);

                if (error) {
                    return error;
                }

                // 没有消息时登记接收等待，醒来仍回到循环检查，普通唤醒本身不保证已有消息。
                prepareWaiter(state.receivers, waiter);
            }

            blockWaitingThread();
            {
                InterruptSpinLockGuard guard(state.lock);
                removeWaiter(state.receivers, waiter);
                disarmTimer(waiter);
            }

            const int result =
                __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);

            if (result < 0) {
                return result;
            }
        }
    }

    /*
     * 按令牌查找等待回复的调用，复制回复和句柄引用，再唤醒调用者。
     * 失效或不属于本端点的令牌被拒绝。
     */
    int Endpoint::reply(IpcMessage* message) {
        Operation operation(this);

        if (!validateMessage(message) || !message->replyToken ||
            !getImplementation()) {
            return failure(os::ErrorCode::InvalidArgument);
        }

        EndpointImplementation& state = *getImplementation();
        InterruptSpinLockGuard guard(state.lock);

        if (state.destroyed) {
            return failure(os::ErrorCode::EndpointClosed);
        }

        // 令牌标识一次具体调用，不能按调用者线程的当前状态猜测应该回复哪一个请求。
        PendingCall* call = state.calls;

        while (call && call->token != message->replyToken) {
            call = call->next;
        }

        // 令牌存在还不够，必须属于已领取请求的服务任务、仍在等待且没有超时。
        if (!call || call->serverTask != task::getCurrent() ||
            call->result != waiting || deadlineError(call->deadline) ||
            !hasRightLocked(this, task::getCurrent(), endpointRightReceive)) {
            return failure(os::ErrorCode::InvalidArgument);
        }

        IpcMessage reply = *message;
        stampSender(reply);

        // 回复中带出的句柄也要转成调用者本地句柄；目标安装失败时归还这次临时引用。
        HandleTable::Transfer transfer;

        if (!transfer.capture(task::getCurrent()->handles(), reply, false)) {
            return failure(os::ErrorCode::InvalidHandle);
        }

        if (!transfer.install(call->callerTask->handles(), reply)) {
            transfer.clear();

            return failure(os::ErrorCode::OutOfMemory);
        }

        // 先写完整内核回复，再改完成状态和唤醒调用者，防止被唤醒线程读到半份结果。
        *call->replyTarget = reply;
        call->result       = 0;

        for (Waiter* waiter = state.callers; waiter; waiter = waiter->next) {
            if (waiter->thread == call->caller) {
                complete(*waiter, 0);
            }
        }

        return 0;
    }

    /*
     * 判断服务操作是否需要系统调用现场，例如 fork、exec
     * 需要读取或修改返回用户态的寄存器。
     */
    bool Endpoint::handlesContext(uint64_t operation) const {
        return implementation &&
               implementation->service == &taskKernelService() &&
               ((operation >=
                     static_cast<uint64_t>(os::TaskOperation::ExitTask) &&
                 operation <= static_cast<uint64_t>(
                                  os::TaskOperation::StandardHandle)) ||
                operation ==
                    static_cast<uint64_t>(os::TaskOperation::SignalReturn));
    }

    /*
     * 直接调用内核服务入口，省去用户服务的排队过程；仍执行请求校验和调用上下文处理。
     */
    int Endpoint::callImmediate(IpcMessage& request, IpcMessage& reply,
                                uint64_t deadline, bool kernelOrigin) {
        Operation operation(this);

        if (!validateMessage(&request) || request.handleCount ||
            !handlesContext(request.type)) {
            return failure(os::ErrorCode::InvalidArgument);
        }

        EndpointImplementation& state = *getImplementation();
        {
            InterruptSpinLockGuard guard(state.lock);

            if (state.destroyed) {
                return failure(os::ErrorCode::EndpointClosed);
            }

            if (!kernelOrigin &&
                !hasRightLocked(this, task::getCurrent(), endpointRightSend)) {
                return failure(os::ErrorCode::PermissionDenied);
            }

            if (deadline && deadlineError(deadline)) {
                return deadlineError(deadline);
            }
        }

        stampSender(request);
        reply = {};

        return state.service->call(*task::getCurrent(), request, reply);
    }

    /*
     * 发起普通同步调用，等待服务回复或截止时间到达。
     */
    int Endpoint::call(IpcMessage* request, IpcMessage* reply,
                       uint64_t deadline) {
        return callImpl(request, reply, deadline, false);
    }

    /*
     * 从内核发起同步调用，显式标记请求来源，使内部补页等请求不冒用用户消息内容。
     */
    int Endpoint::callFromKernel(IpcMessage* request, IpcMessage* reply,
                                 uint64_t deadline) {
        return callImpl(request, reply, deadline, true);
    }

    /*
     * 同步调用的公共实现。
     * 登记未完成请求、传递消息并等待回复，统一处理取消、超时、句柄传递和清理。
     */
    int Endpoint::callImpl(IpcMessage* request, IpcMessage* reply,
                           uint64_t deadline, bool kernelOrigin) {
        Operation operation(this);

        if (!validateMessage(request) || !reply || !getImplementation()) {
            return failure(os::ErrorCode::InvalidArgument);
        }

        // 内核服务可以在当前线程直接执行；用户服务则必须经过排队、领取和令牌回复。
        if (EndpointService* service = getImplementation()->service) {
            if (request->handleCount) {
                return failure(os::ErrorCode::InvalidArgument);
            }

            EndpointImplementation& state = *getImplementation();

            // 会修改用户返回现场的操作使用专用直接路径，不能让普通回复写入覆盖新寄存器现场。
            if (handlesContext(request->type)) {
                return callImmediate(*request, *reply, deadline, kernelOrigin);
            }

            Waiter waiter{currentThread(), nullptr, nullptr,
                          deadline ? deadline : os::ipcInfiniteDeadline};
            IpcMessage copy = *request;
            stampSender(copy);
            copy.replyToken = nextReplyToken.fetchAdd(1, MemoryOrder::Relaxed);
            {
                InterruptSpinLockGuard guard(state.lock);

                if (state.destroyed) {
                    return failure(os::ErrorCode::EndpointClosed);
                }

                if (!kernelOrigin && !hasRightLocked(this, task::getCurrent(),
                                                     endpointRightSend)) {
                    return failure(os::ErrorCode::PermissionDenied);
                }

                if (deadline && deadlineError(deadline)) {
                    return deadlineError(deadline);
                }

                prepareWaiter(state.callers, waiter);
                waiter.thread->setIpcWaitResult(&waiter.result, deadline == 0);
            }

            *reply = {};

            // 等待登记完成后才执行服务，信号和取消路径因此能找到这个正在进行的调用。
            int result = service->call(*task::getCurrent(), copy, *reply);
            {
                InterruptSpinLockGuard guard(state.lock);
                complete(waiter, 0);
                removeWaiter(state.callers, waiter);
                disarmTimer(waiter);
                waiter.thread->setIpcWaitResult(nullptr);
                const int aborted =
                    __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
                // 内核服务可能已经完成不可重复操作，例如收走子任务退出状态；与信号同时发生时，不能丢弃已经完成的回复。
                if (aborted < 0 && result != 0) {
                    result = aborted;
                }
            }

            reply->replyToken = copy.replyToken;

            // 调用失败时，回复里新建的句柄还没有成功交给用户，需要在当前任务表中关闭。
            if (result) {
                for (uint32_t i = 0; i < reply->handleCount; ++i) {
                    task::getCurrent()->handles().close(reply->handles[i]);
                }

                reply->handleCount = 0;
            }

            return result;
        }

        // 记录要等完成或取消摘链后才能销毁；服务使用另一套页表时，回复目标仍是公共内核地址，之后由系统调用层写回用户空间。
        PendingCall call{currentThread(),
                         task::getCurrent(),
                         nullptr,
                         nullptr,
                         nextReplyToken.fetchAdd(1, MemoryOrder::Relaxed),
                         reply,
                         deadline};
        IpcMessage copy = *request;
        copy.replyToken = call.token;
        stampSender(copy);

        // 用户服务路径先把请求入队，队列持有它需要的对象引用，之后调用者才开始等令牌回复。
        int result = enqueue(this, copy, &call, deadline, kernelOrigin);

        if (result) {
            return result;
        }

        EndpointImplementation& state = *getImplementation();

        while (true) {
            Waiter waiter{call.caller, nullptr, nullptr, deadline};
            {
                InterruptSpinLockGuard guard(state.lock);

                // 检查完成结果与登记睡眠使用同一端点锁，避免回复先到却又把调用者睡下。
                if (call.result != waiting) {
                    result = call.result;
                    break;
                }

                result = deadlineError(deadline);

                if (result) {
                    // 超时或取消时连同尚未领取的消息一起撤走，防止服务以后读到已离开作用域的调用记录。
                    discardCallMessage(state, call);
                    break;
                }

                prepareWaiter(state.callers, waiter);
            }

            blockWaitingThread();
            {
                InterruptSpinLockGuard guard(state.lock);
                removeWaiter(state.callers, waiter);
                disarmTimer(waiter);

                if (call.result != waiting) {
                    result = call.result;
                } else {
                    result = __atomic_load_n(&waiter.result, __ATOMIC_ACQUIRE);
                }

                if (result < 0) {
                    discardCallMessage(state, call);
                    break;
                }
            }
        }

        {
            InterruptSpinLockGuard guard(state.lock);

            // 返回调用方之前最后摘除未完成记录，栈上的 PendingCall
            // 到这里才允许销毁。
            unlinkCall(state, call);
        }

        return result;
    }

    /*
     * 取消指定线程的未完成调用，摘除请求及排队消息，再唤醒等待线程返回取消结果。
     */
    int Endpoint::cancel(uint64_t threadId) {
        EndpointImplementation& state = *getImplementation();
        InterruptSpinLockGuard guard(state.lock);
        int result = failure(os::ErrorCode::NotFound);

        for (PendingCall* call = state.calls; call; call = call->next) {
            if (call->caller->getId() == threadId &&
                call->callerTask == task::getCurrent() &&
                call->result == waiting) {
                call->result = failure(os::ErrorCode::Cancelled);
                discardCallMessage(state, *call);
                result = 0;
            }
        }

        Waiter* lists[] = {state.senders, state.receivers, state.callers};

        for (Waiter* list : lists) {
            for (Waiter* waiter = list; waiter; waiter = waiter->next) {
                if (waiter->thread->getId() != threadId ||
                    waiter->thread->getTask() != task::getCurrent()) {
                    continue;
                }

                complete(*waiter, failure(os::ErrorCode::Cancelled));
                result = 0;
            }
        }

        return result;
    }

    /*
     * 扫描过期等待并结束它们，清除请求和计时状态，避免超时与正常回复重复完成同一请求。
     */
    void Endpoint::expireWaits(uint64_t now) {
        {
            InterruptSpinLockGuard guard(timerLock);

            // 超时扫描先在计时链上找到到期等待，再按所属端点锁完成取消，不能直接释放仍挂在端点上的栈节点。
            while (timers && timers->deadline <= now) {
                Waiter* waiter    = timers;
                timers            = waiter->timerNext;
                waiter->timerNext = nullptr;
                waiter->timed     = false;
                complete(*waiter, failure(os::ErrorCode::Timeout));
            }
        }

        InterruptSpinLockGuard guard(endpointListLock);
        EndpointImplementation** link = &endpoints;

        while (*link) {
            EndpointImplementation* state = *link;
            Endpoint* endpoint            = state->endpoint;

            if (__atomic_load_n(&endpoint->references, __ATOMIC_ACQUIRE) != 0) {
                link = &state->next;
                continue;
            }

            *link = state->next;
            while (state->grants) {
                AccessGrant* grant = state->grants;
                state->grants      = grant->next;
                mm::freeHeap(grant);
            }

            mm::freeHeap(state);
            mm::freeHeap(endpoint);
        }
    }

    /*
     * 在线程退出时取消它仍挂在端点上的发送、接收或同步调用，解除栈上等待节点的登记。
     */
    void Endpoint::cancelThread(Thread* thread) {
        {
            InterruptSpinLockGuard listGuard(endpointListLock);

            for (EndpointImplementation* state = endpoints; state;
                 state                         = state->next) {
                InterruptSpinLockGuard guard(state->lock);
                Waiter** heads[] = {&state->senders, &state->receivers,
                                    &state->callers};

                for (Waiter** head : heads) {
                    Waiter** link = head;

                    while (*link) {
                        Waiter* waiter = *link;

                        if (waiter->thread != thread) {
                            link = &waiter->next;
                            continue;
                        }

                        *link = waiter->next;
                        disarmTimer(*waiter);
                        complete(*waiter, failure(os::ErrorCode::Interrupted));
                    }
                }

                // 线程退出时既可能是请求方，也可能是领取同步调用的服务线程，两类关系都必须清理。
                PendingCall** link = &state->calls;

                while (*link) {
                    PendingCall* call = *link;

                    if (call->serverThread == thread &&
                        call->result == waiting) {
                        call->result       = failure(os::ErrorCode::PeerDied);
                        call->serverThread = nullptr;

                        for (Waiter* waiter = state->callers; waiter;
                             waiter         = waiter->next) {
                            if (waiter->thread == call->caller) {
                                complete(*waiter,
                                         failure(os::ErrorCode::PeerDied));
                            }
                        }
                    }

                    if (call->caller != thread) {
                        link = &call->next;
                        continue;
                    }

                    discardCallMessage(*state, *call);
                    *link = call->next;
                }
            }
        }

        // 活动操作引用包含嵌套补页调用，统一由 ThreadCleanup 归还。
        thread->setActiveEndpoint(nullptr);
        thread->setActiveIpcCall(false);
        thread->setIpcWaitResult(nullptr);
    }

    /*
     * 用信号打断线程的可中断 IPC 等待，使线程能够返回用户态处理信号。
     */
    void Endpoint::interruptThread(Thread* thread) {
        InterruptSpinLockGuard listGuard(endpointListLock);

        for (EndpointImplementation* state = endpoints; state;
             state                         = state->next) {
            InterruptSpinLockGuard guard(state->lock);

            for (PendingCall* call = state->calls; call; call = call->next) {
                if (call->caller != thread || call->result != waiting) {
                    continue;
                }

                call->result = failure(os::ErrorCode::Interrupted);
                discardCallMessage(*state, *call);
            }

            Waiter* lists[] = {state->senders, state->receivers,
                               state->callers};

            for (Waiter* list : lists) {
                for (Waiter* waiter = list; waiter; waiter = waiter->next) {
                    if (waiter->thread == thread) {
                        complete(*waiter, failure(os::ErrorCode::Interrupted));
                    }
                }
            }
        }
    }

    /*
     * 从中断上下文投递简短通知。
     * 只使用不会阻塞的路径，队列无法容纳时返回失败。
     */
    bool Endpoint::notifyFromInterrupt(uint64_t type, uint64_t value) {
        EndpointImplementation& state = *getImplementation();
        InterruptSpinLockGuard guard(state.lock);

        if (state.destroyed) {
            return false;
        }

        if (type == 0x495251 && value < 16) {
            state.pendingIrqs |= static_cast<uint16_t>(1U << value);
            wakeOne(state.receivers);

            return true;
        }

        if (state.count == queueCapacity) {
            return false;
        }

        QueuedMessage& queued =
            state.queue[(state.head + state.count) % queueCapacity];
        queued.message         = {};
        queued.message.type    = type;
        queued.message.length  = 1;
        queued.message.data[0] = value;
        ++state.count;
        wakeOne(state.receivers);

        return true;
    }

}  // namespace task
