/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/task.cc: 创建任务、维护父子关系、管理对象寿命、
 * 提交 fork/exec 的部分状态，以及进程组、会话和任务查找。
 */

#include "kernel/task.h"

#include <new>

#include "kernel/atomic.h"
#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/protocol.h"
#include "service/common/records.h"

namespace task {

    constexpr uint32_t initialHandleCapacity = 64;
    static ::Atomic<uint64_t> nextTaskId{
        1};  // 	原子递增的任务/线程编号序列
    static Task* kernelTask;
    static Task* initTask;
    static Task* taskList;         // 	全局任务链表
    static SpinLock taskListLock;  //  保护全局链表的自旋锁
    static uint64_t terminalForegroundGroups[2]
                                            [8]{};  // 	各终端对应的前台进程组

    /*
     * 把任务名拷贝到定长缓冲区
     */
    static void copyName(char* destination, const char* source) {
        uint32_t index = 0;

        if (source != nullptr) {
            while (index < 63 && source[index] != '\0') {
                destination[index] = source[index];
                ++index;
            }
        }

        destination[index] = '\0';
    }

    /* TaskReference 是一个“带引用的智能指针”（RAII：对象构造时获取资源，
       析构时自动归还）。构造时给任务加一份引用，析构时自动减掉，于是中途

       return 或出错也不会忘记 release()。

       需要登记到 ThreadCleanup 是因为线程被强制杀死时 C++ 栈不会正常展开，
       只有 ThreadCleanup 会被执行。
     */
    TaskReference::TaskReference(Task* task) : task_(task) {
        const uint64_t flags = cpu::disableInterrupts();

        if (task_) {
            task_->retain();
            cleanup_.attach(task_, [](void* value) {
                static_cast<Task*>(value)->release();
            });
        }

        cpu::restoreInterrupts(flags);
        // 为什么关中断：加引用和向当前线程登记清理动作必须作为一个整体完成，
        // 否则线程可能恰好在两步之间被杀死，导致这份引用永远没人释放（引用计数
        // 泄漏）。
    }

    /* 移动构造：把源对象的清理登记“搬”过来，而不是再加一份引用。
       若先加新引用、再减旧引用，中间会有一个引用计数虚高的窗口；直接转移所有权
       更精确。detach() 只解除登记、不触发 release()，否则对象可能在所有权转移
       途中就被释放。转移完把源对象的 task_ 置空，保证一份引用只有一个拥有者。
     */
    TaskReference::TaskReference(TaskReference&& other) : task_(other.task_) {
        const uint64_t flags = cpu::disableInterrupts();
        other.cleanup_.detach();

        if (task_) {
            cleanup_.attach(task_, [](void* value) {
                static_cast<Task*>(value)->release();
            });
        }

        other.task_ = nullptr;
        cpu::restoreInterrupts(flags);
    }

    /* 赋值：先释放自己原有的登记，再接管源对象的登记。
     */
    TaskReference& TaskReference::operator=(TaskReference&& other) {
        if (this != &other) {  // 排除自赋值
            const uint64_t flags = cpu::disableInterrupts();
            cleanup_.reset();
            task_ = other.task_;
            other.cleanup_.detach();

            if (task_) {
                cleanup_.attach(task_, [](void* value) {
                    static_cast<Task*>(value)->release();
                });
            }

            other.task_ = nullptr;
            cpu::restoreInterrupts(
                flags);  // 避免两个包装器同时认为自己拥有同一份引用而重复
                         // release()。
        }

        return *this;  // 以支持链式赋值
    }

    /*
     * 析构时由 cleanup_ 成员触发 release()，与构造时的 retain() 配对。
     * 正常作用域退出走这里就够了，所以写成 = default。
     */
    TaskReference::~TaskReference() = default;

    /*
     * 增加一份“对象生命周期”引用（原子操作：多核并发加减也不会算错）。
     * 它只保证对象不会被释放，并不阻止任务被终止，因为终止由状态机决定。
     */
    void Task::retain() {
        __atomic_add_fetch(&references_, 1U, __ATOMIC_RELAXED);
    }

    /*
     * 减少一份引用；只有把计数减到 0 的那个调用者负责真正释放对象。
     * 顺序：先把 parent_ 存到局部变量，再 freeHeap(this)。因为 freeHeap
     * 之后 this 指向的内存已失效，不能再读 this->parent_。
     * 父任务的引用是随本对象一起释放的：子任务存活期间 parent_ 一直有效
     * （对应 Task::init 里对 parent 的 retain）。
     */
    void Task::release() {
        if (__atomic_sub_fetch(&references_, 1U, __ATOMIC_ACQ_REL) != 0) {
            return;
        }

        Task* parent = parent_;
        mm::freeHeap(this);

        if (parent) {
            parent->release();
        }
    }

    /*
     * 记录init：父任务退出时，它的子任务会交给 init 任务收养，
     * 以免变成孤儿进程（orphan，父进程先于自己退出的进程）。
     * initTask 是本文件内的全局指针，这里额外持有一份引用，使它独立于普通父子
     * 关系，不会因为某个子任务被回收而失效。启动阶段只会安装一次。
     */
    void Task::setInitTask(Task* task) {
        if (task) {
            task->retain();
        }

        initTask = task;
    }

    /*
     * 初始化页表根和非零映像代号，清空锁页记录并初始化地址空间锁。
     */
    void AddressSpace::init(uint64_t rootPhysicalAddress) {
        rootPhysicalAddress_ = rootPhysicalAddress;
        generation_          = 1;

        residency_  = {};
        lockFuture_ = false;

        lock_.init();
    }

    /* 更换页表根，并返回换下来的旧根。调用前提：调用方已持有该地址空间的锁
     （addressSpace().lock()）。
    */
    uint64_t AddressSpace::replaceRoot(uint64_t rootPhysicalAddress) {
        const uint64_t previous = rootPhysicalAddress_;
        rootPhysicalAddress_    = rootPhysicalAddress;

        // 若根确实变了，就清空驻留统计（residency_，记录本地址空间驻留了哪些内存
        // 区间）并重置 lockFuture_；同时把 generation_ 加一，并跳过 0。
        if (previous != rootPhysicalAddress) {
            residency_  = {};
            lockFuture_ = false;

            // 旧的映射记录可能在换根之后才被回收，若新映像恰好复用了相同的虚拟地址，旧记录就会错误地撤销新映射。
            // 递增代号让旧记录自动失效。
            if (++generation_ == 0) {
                ++generation_;
            }
        }

        return previous;
    }

    /*
     * 返回页表根物理地址，调用者负责在需要时持锁并保证根仍然有效。
     */
    uint64_t AddressSpace::rootPhysicalAddress() const {
        return rootPhysicalAddress_;
    }

    /*
     * 返回地址空间锁，供调用者把换根或映射相关操作组成一个受保护的步骤。
     */
    SpinLock& AddressSpace::lock() { return lock_; }

    /*
     * 分配全局唯一的任务号（PID）。任务和线程共用同一个递增序列，保证两者编号
     * 不会冲突。原子操作（atomic）用于多核并发：fetchAdd 返回加一之前的旧值，
     * 所以第一个任务拿到 1。
     * 约定：任务的第一个线程会沿用该任务的编号（见 attachThread），于是主线程的
     * TID 与 PID 相同，方便用户态理解。
     */
    uint64_t allocateId() {
        return nextTaskId.fetchAdd(1, MemoryOrder::Relaxed);
    }

    /*
        本成员函数用于创建任务。

        为什么先有根再 init：页表根由 Task::create
       统一分配和回滚，本函数不拥有它， 失败时也由 create 负责销毁。

        返回值：初始句柄表分配失败时返回 false；此时对象还没发布到任何链表，
        调用方（Task::create）负责回滚根和对象存储。

        调用前提：对象此刻尚未被其它 CPU 看到，所以外部无需加锁；父任务的字段在
        本函数内部持 parent->lock_ 读取，避免读到正在被修改的凭据或限制。
    */
    bool Task::init(const char* name, uint64_t rootPhysicalAddress,
                    Task* parent) {
        id_ = task::allocateId();
        copyName(name_, name);
        state_ = TaskState::Created;
        addressSpace_.init(
            rootPhysicalAddress);  // 用给定的页表根初始化任务的地址空间

        // 保证句柄表初始化成功，否则任务对象就没法使用
        if (!handles_.init(initialHandleCapacity, this)) {
            return false;
        }

        mainThread_  = nullptr;
        threads_     = nullptr;
        threadCount_ = 0;
        parent_      = parent;

        // 父任务可能先于子任务退出并释放资源；这里持有一份引用，保证只要子任务
        // 还活着，parent_ 指针就一直指向有效对象。
        if (parent_) {
            parent_->retain();
        }

        terminalId_         = parent ? parent->terminalId() : 0;
        terminalKind_       = parent ? parent->terminalKind() : 0;
        startedNanoseconds_ = time::getMonotonicTime();
        uid_ = effectiveUid_ = parent ? parent->effectiveUid() : 0;
        gid_ = effectiveGid_ = parent ? parent->effectiveGid() : 0;

        if (parent) {
            InterruptSpinLockGuard guard(parent->lock_);
            memlockLimit_       = parent->memlockLimit_;
            memlockHardLimit_   = parent->memlockHardLimit_;
            coreLimit_          = parent->coreLimit_;
            coreHardLimit_      = parent->coreHardLimit_;
            uid_                = parent->uid_;
            effectiveUid_       = parent->effectiveUid_;
            savedUid_           = parent->savedUid_;
            gid_                = parent->gid_;
            effectiveGid_       = parent->effectiveGid_;
            savedGid_           = parent->savedGid_;
            supplementaryCount_ = parent->supplementaryCount_;

            for (uint32_t i = 0; i < supplementaryCount_; ++i) {
                supplementaryGroups_[i] = parent->supplementaryGroups_[i];
            }

            niceLimit_      = parent->niceLimit_;
            niceHardLimit_  = parent->niceHardLimit_;
            Thread* creator = currentThread();
            initialNice_ =
                creator && creator->getTask() == parent ? creator->getNice()
                : parent->mainThread_ ? parent->mainThread_->getNice()
                                      : 0;
            // 已持有父任务锁，直接复制同一份凭据快照；authentication()
            // 会再次取锁，
            // 在不可重入的自旋锁上调用它会让创建线程永远等待自己释放锁。
            authenticationSession_ = parent->authenticationSession_;
            authenticationUid_     = parent->authenticationUid_;
            authenticationGid_     = parent->authenticationGid_;
        }

        firstChild_      = nullptr;
        nextSibling_     = nullptr;
        childWaiters_    = nullptr;
        exitCode_        = 0;
        acceptsThreads_  = true;
        capabilities_    = 0;
        bootstrapHandle_ = 0;

        for (uint64_t& handle : standardHandles_) {
            handle = 0;
        }

        taskGroupId_      = parent == nullptr ? id_ : parent->taskGroupId();
        sessionId_        = parent == nullptr ? id_ : parent->sessionId();
        stopped_          = false;
        stopSignal_       = 0;
        stopReported_     = false;
        continuedPending_ = false;

        for (SignalAction& action : signalActions_) {
            action = {};
        }

        pendingSignals_ = 0;
        nextGlobal_     = nullptr;

        lock_.init();

        return true;
    }

    /*
     * 创建一个新任务：先分配私有的页表根和对象存储，全部成功后才发布到父任务
     * 的子链表和全局任务链表（taskList）中。
     *
     * 为什么是这个顺序：一旦发布，对象就可能被其它 CPU 查到，那时就不能再用
     * “直接回滚”的方式清理了。所以早期失败时两个链表都还没见过这个对象，
     * 可以安全地销毁根、释放存储。
     *
     * 失败路径：根分配失败或 init 失败时返回 nullptr，且不留下半成品。
     * 成功路径：对象自带一份创建者引用（references_ 从 1 开始），并持有父任务
     * 引用直到对象最终释放——包括它作为僵尸进程（zombie：已退出、但退出状态
     * 还没被父任务用 wait 取走的进程）等待的那段时间。
     * 另外把页表根的物理页归属登记为新任务的 PID，便于内存统计和回收归因。
     */
    Task* Task::create(const char* name, Task* parent) {
        const uint64_t root = mm::createAddressSpaceRoot();

        if (root == 0) {
            return nullptr;
        }

        // 页表根和 Task
        // 存储分别可能失败，成功发布到任何链表之前都由本创建路径独占并负责回滚。
        void* storage = mm::allocateHeap(sizeof(Task));
        Task* task    = storage == nullptr ? nullptr : new (storage) Task;

        if (task == nullptr || !task->init(name, root, parent)) {
            mm::destroyAddressSpaceRoot(root);
            mm::freeHeap(storage);

            return nullptr;
        }

        mm::assignPhysicalPageOwner(root, task->id());

        // 对象完整初始化后才加入父子链，其他线程从此可能找到它，之后应走正常生命周期清理。
        if (parent != nullptr) {
            InterruptSpinLockGuard guard(parent->lock_);
            task->nextSibling_  = parent->firstChild_;
            parent->firstChild_ = task;
        }

        {
            InterruptSpinLockGuard guard(taskListLock);

            // 全局链表发布是对外可查的最后一步，链表锁保护头插与并发查找。
            task->nextGlobal_ = taskList;
            taskList          = task;
        }

        return task;
    }

    /*
     * 返回任务号（PID）。这是任务的对外身份标识，任务存活期间不会改变。
     */
    uint64_t Task::id() const { return id_; }

    /*
     * 返回任务名。名字存在定长缓冲区里（最多 63 字节 + '\0'），缓冲区归任务
     * 对象所有，调用方不要释放，也不要假定它能一直有效。
     */
    const char* Task::name() const { return name_; }

    /*
     * 返回任务对外的状态视图。
     *
     * 为什么叫“视图”：作业控制（job control，例如终端里的 Ctrl+Z）只是把任务
     * 挂起，底层 state_ 仍然写着 Running；这里借 stopped_ 把它映射成
     * TaskState::Stopped，让调用方看到一致的状态。
     * 注意：为回收内存而做的冻结（memoryFrozen_）是另一套状态，恢复规则也不同，
     * 不能混进这里。
     * 加锁原因：这些字段会被其它 CPU 改写，必须持锁读取。
     */
    TaskState Task::state() {
        InterruptSpinLockGuard guard(lock_);

        return stopped_ && state_ == TaskState::Running ? TaskState::Stopped
                                                        : state_;
    }

    /*
     * 返回任务私有的地址空间对象。多核下要操作它，需自行持有
     * addressSpace().lock()。
     */
    AddressSpace& Task::addressSpace() { return addressSpace_; }

    /*
     * 返回任务私有的句柄表（handle table：每个进程一张、彼此隔离的编号表，
     * 进程通过表中的句柄访问内核对象）。
     */
    HandleTable& Task::handles() { return handles_; }

    /*
     * 返回父任务指针（借用，不额外持有引用）：只在当前父子关系仍然成立的期间
     * 有效。解锁之后还要继续使用父任务的话，请改用 parentReference()。
     */
    Task* Task::parent() {
        InterruptSpinLockGuard guard(lock_);

        return parent_;
    }

    /*
     * 在关系锁保护下读取父任务的 PID；没有父任务时（例如内核任务）返回 0。
     */
    uint64_t Task::parentId() {
        InterruptSpinLockGuard guard(lock_);

        return parent_ ? parent_->id() : 0;
    }

    /*
     * 返回一个带引用的父任务引用（TaskReference），可以安全地在解锁之后使用。
     * 必须在放掉关系锁之前完成 retain：否则父任务可能刚好被重新收养（reparent）
     * 或释放，调用方就会拿着一个失去引用保护的旧父指针。
     */
    TaskReference Task::parentReference() {
        InterruptSpinLockGuard guard(lock_);

        return TaskReference(parent_);
    }

    /*
     * 当前任务退出时，把自己的子任务转交给“收养者”，避免它们变成孤儿进程
     * （orphan：父进程先退出、没有父任务来 wait 的进程）。
     *
     * 收养者优先选 init 任务（第一个用户态程序，由 loader 启动）；如果 init
     * 尚未建立、就是自己、或已经终止，则退回到内核任务。这样能保证任何时刻都有
     * 一个活着的收养者。
     *
     * 步骤与顺序：
     *   1. 先在自身锁内把整条子链表摘下（firstChild_ 置空），避免遍历时被并发
     *      修改；
     *   2. 逐个孩子在“收养者锁 + 孩子锁”下重新挂链，并给收养者加一份引用——
     *      这份引用正好对应该孩子 parent_ 指针的所有权；
     *   3. 挂好新链之后，才释放旧父任务（this）为这个孩子保留的那份引用。
     *      顺序反过来先释放旧引用的话，中间就会出现孩子指向无效父任务的窗口；
     *   4. 最后通知收养者：可能有僵尸子任务需要处理。
     */
    void Task::adoptChildren() {
        Task* adopter =
            initTask && initTask != this && !initTask->isTerminated()
                ? initTask
                : kernelTask;
        Task* children;
        {
            InterruptSpinLockGuard guard(lock_);

            // 先在旧父锁内整批摘下孩子，再逐个交接；遍历期间不让其他路径继续操作旧子链。
            children    = firstChild_;
            firstChild_ = nullptr;
        }

        if (!children) {
            return;
        }

        while (children) {
            Task* child = children;
            children    = child->nextSibling_;
            {
                InterruptSpinLockGuard parentGuard(adopter->lock_);
                InterruptSpinLockGuard childGuard(child->lock_);

                // 先取得新父引用再改
                // parent_，之后才归还旧父引用，保证孩子的父指针始终有生命周期保护。
                adopter->retain();
                child->parent_       = adopter;
                child->nextSibling_  = adopter->firstChild_;
                adopter->firstChild_ = child;
            }

            release();
        }

        if (adopter) {
            adopter->notifyChildStateChange();
        }
    }

    /*
     * 判断任务是否已在逻辑上终止。这里只反映状态机，不代表物理资源已经回收完毕
     * ——资源回收由回收器（reclaimer）异步完成，两者刻意分开。
     */
    bool Task::isTerminated() { return state() == TaskState::Terminated; }

    /*
     * fork（复制出一个子进程）时，把父任务中应当被继承的状态复制到本对象。
     *
     * 做什么：
     *   1. 凭据、资源限制、dumpable、映像映射表、auxv、标准句柄、进程组/会话、
     *      认证会话、命令行、信号动作等元数据，都在“父锁 + 子锁”下一次性拷贝；
     *   2. 句柄表按编号逐个继承：带 NoInherit 标志的槽位跳过；端点（endpoint）
     *      和共享内存要分别走各模块的 duplicateHandle()，因为它们有额外的所有权
     *      和引用规则；其它类型用通用 copyTo()。
     *
     * 为什么元数据要先拷贝完：子任务一旦可运行就可能被调度，调度时看到的状态
     * 必须已经完整。
     *
     * 失败路径：任何一步返回 false，已经建立的子任务资源都留给回收器统一清理，
     * 不在这里回滚——因为句柄复制可能已经产生了副作用（例如端点接收者身份）。
     * 注意 pendingSignals_ 故意清零：父任务待处理的信号不应由子任务继承。
     */
    bool Task::inheritForFork(Task& parent) {
        {
            InterruptSpinLockGuard parentGuard(parent.lock_);
            InterruptSpinLockGuard childGuard(lock_);

            // 在父子状态锁内复制进程属性，这里只准备元数据，不发布可运行线程。
            capabilities_       = parent.capabilities_;
            uid_                = parent.uid_;
            gid_                = parent.gid_;
            effectiveUid_       = parent.effectiveUid_;
            effectiveGid_       = parent.effectiveGid_;
            savedUid_           = parent.savedUid_;
            savedGid_           = parent.savedGid_;
            supplementaryCount_ = parent.supplementaryCount_;

            for (uint32_t i = 0; i < supplementaryCount_; ++i) {
                supplementaryGroups_[i] = parent.supplementaryGroups_[i];
            }

            memlockLimit_      = parent.memlockLimit_;
            memlockHardLimit_  = parent.memlockHardLimit_;
            coreLimit_         = parent.coreLimit_;
            coreHardLimit_     = parent.coreHardLimit_;
            niceLimit_         = parent.niceLimit_;
            niceHardLimit_     = parent.niceHardLimit_;
            dumpable_          = parent.dumpable_;
            imageMappingCount_ = parent.imageMappingCount_;

            for (uint32_t i = 0; i < imageMappingCount_; ++i) {
                imageMappings_[i] = parent.imageMappings_[i];
            }

            for (unsigned i = 0; i < 14; ++i) {
                auxiliary_[i] = parent.auxiliary_[i];
            }

            bootstrapHandle_ = parent.bootstrapHandle_;
            kernelHandle_    = parent.kernelHandle_;

            for (uint32_t index = 0; index < 3; ++index) {
                standardHandles_[index] = parent.standardHandles_[index];
            }

            taskGroupId_           = parent.taskGroupId_;
            sessionId_             = parent.sessionId_;
            authenticationSession_ = parent.authenticationSession_;
            authenticationUid_     = parent.authenticationUid_;
            authenticationGid_     = parent.authenticationGid_;
            commandLineSize_       = parent.commandLineSize_;

            for (size_t i = 0; i < sizeof(commandLine_); ++i) {
                commandLine_[i] = parent.commandLine_[i];
            }

            for (uint32_t signal = 0; signal <= maximumSignal; ++signal) {
                signalActions_[signal] = parent.signalActions_[signal];
            }

            pendingSignals_ = 0;
        }

        // 元数据复制结束后逐槽继承对象；端点与共享内存使用专用复制路径，保留各自额外的归属规则。
        const uint32_t inheritedCapacity = parent.handles_.capacity();

        for (uint32_t index = 0; index < inheritedCapacity; ++index) {
            uint64_t handle = 0;
            void* object    = nullptr;
            HandleType type = HandleType::None;
            uint32_t rights = 0;

            if (!parent.handles_.inspectAt(index, &handle, &object, &type,
                                           &rights)) {
                continue;
            }

            uint32_t flags = 0;

            if (!parent.handles_.getFlags(handle, flags)) {
                return false;
            }

            // 明确禁止继承的句柄直接跳过，不能因为编号存在就全部复制给孩子。
            if (flags & services::handleFlagNoInherit) {
                continue;
            }

            uint64_t inherited = 0;

            if (type == HandleType::Endpoint) {
                inherited =
                    Endpoint::duplicateHandle(&parent, this, handle, handle);
            } else if (type == HandleType::SharedMemory) {
                inherited = ::mm::sharedMemory().duplicateHandle(
                    parent, *this, handle, handle);
            } else {
                inherited = parent.handles_.copyTo(handles_, handle, handle);
            }

            if (inherited != handle) {
                // 正常情况下复制出的句柄号必须与父任务一致，不一致就说明中途
                // 出错，直接失败返回。回收器会先关闭端点、再处置句柄表，其中
                // 也包括那些“身份需要在本服务实例之后继续存在”的可恢复接收者，
                // 所以这里不需要自己回滚已复制的句柄。
                return false;
            }

            handles_.setFlags(inherited, flags);
        }

        return true;
    }

    /*
     * exec（用新程序替换当前进程映像）的提交阶段：只为“发起调用的这个线程”
     * 提交已准备好的新页表根。
     *
     * 这里已经不可回头：兄弟线程一旦被摘除，即使另一个 CPU 并发杀死本任务，
     * 也必须把线程所有权和新根推进到已提交状态，否则资源会漏掉。
     *
     * 并发安排：整个不可逆区间内禁止本地抢占（PreemptionGuard），但保持中断
     * 开启，让其它 CPU 能收到 shootdown（跨核页表失效通知）并尽快离开旧栈；
     * 本区间内不允许阻塞，也不允许调度。
     *
     * 输入：newRootPhysicalAddress 是已准备好的新根（不能为 0）；currentThread
     * 必须是本任务的线程，且本任务不能是内核任务。不满足前置条件时返回 false。
     * 调用前提：调用方已把新映像准备好并传入它的根；被换下的旧根由本函数销毁。
     */
    bool Task::commitExec(uint64_t newRootPhysicalAddress, const char* name,
                          Thread* currentThread) {
        if (newRootPhysicalAddress == 0 || currentThread == nullptr ||
            currentThread->getTask() != this || this == kernelTask) {
            return false;
        }

        // 再强调一次这里的危险点：即使另一个 CPU 正在杀死本任务，兄弟线程的归属
        // 和新根也必须落到已提交状态。因此保持中断开启（shootdown 需要它），
        // 但禁止阻塞和调度。
        cpu::PreemptionGuard commitGuard;
        uint64_t oldRoot       = 0;
        Thread* retiredThreads = nullptr;
        {
            InterruptSpinLockGuard guard(lock_);

            if (!acceptsThreads_ || state_ == TaskState::Terminated ||
                currentThread->getState() == ThreadState::Terminated) {
                return false;
            }

            // 在任务锁内终止并摘下其余线程，之后这个进程只保留执行 exec
            // 的当前线程。
            for (Thread* thread = threads_; thread != nullptr;) {
                Thread* next = thread->getTaskNext();

                if (thread != currentThread &&
                    thread->getState() != ThreadState::Terminated) {
                    task::terminateThread(thread);
                    cpu::requestRemoteReschedule(
                        static_cast<uint32_t>(thread->getCpuId()));
                }

                if (thread != currentThread) {
                    thread->setExitAccounted(true);
                    thread->setTaskNext(retiredThreads);
                    retiredThreads = thread;
                }

                thread = next;
            }

            // 清理当前线程里属于旧程序的信号现场和等待关系，旧用户代码的栈地址不能带进新程序。
            currentThread->setTaskNext(nullptr);
            currentThread->setPendingSignals(0);
            currentThread->signalStack       = {};
            currentThread->signalDepth       = 0;
            currentThread->activeSignalFrame = 0;
            currentThread->lastFault         = {};
            imageMappingCount_               = 0;

            for (uint64_t& value : auxiliary_) {
                value = 0;
            }

            currentThread->setWaitNext(nullptr);
            threads_    = currentThread;
            mainThread_ = currentThread;
            currentThread->setId(id_);
            savedUid_         = effectiveUid_;
            savedGid_         = effectiveGid_;
            dumpable_         = uid_ == effectiveUid_ && gid_ == effectiveGid_;
            threadCount_      = 1;
            acceptsThreads_   = false;
            state_            = TaskState::Running;
            stopped_          = false;
            stopSignal_       = 0;
            stopReported_     = false;
            continuedPending_ = false;
            pendingSignals_   = 0;
            dumpable_         = uid_ == effectiveUid_ && gid_ == effectiveGid_;
            serviceRole_      = services::ServiceRole::None;

            // exec
            // 保留明确忽略的信号动作，用户自定义处理入口则重置，因为它们指向旧映像地址。
            for (uint32_t signal = 1; signal <= maximumSignal; ++signal) {
                if (signalActions_[signal].getHandler() != signalIgnore) {
                    signalActions_[signal] = {};
                }
            }

            copyName(name_, name);
        }

        // 兄弟线程的状态已经在任务锁内改完，但远端 CPU 可能仍在它们的栈上执行。
        // 所以必须先放掉任务锁，等这些 CPU
        // 真正完成上下文切换（quiesce，停稳），
        // 然后才能取消它们挂起的等待、释放内核栈、销毁旧根——否则就是在释放一份
        // 仍在使用中的栈。
        task::quiesceTask(this);
        device::resourceDomains().ownerExiting(*this);

        for (Thread* thread = retiredThreads; thread;
             thread         = thread->getTaskNext()) {
            Endpoint::cancelThread(thread);
            thread->cancelWait();
            ThreadCleanup::releaseAll(*thread);
        }

        while (retiredThreads) {
            Thread* thread = retiredThreads;
            retiredThreads = thread->getTaskNext();

            if (thread->getKernelStack()) {
                mm::freeHeap(thread->getKernelStack());
            }

            mm::freeHeap(thread);
        }

        // 先撤销旧的映射登记：它们描述的是旧一代（generation）的映像。
        // 换根之后，再对“已提交的新映像”执行 close-on-exec 关闭句柄；换下来的
        // 旧根此时仍由本地持有，由后面统一销毁。
        mm::maps().releaseTask(*this);
        ::mm::sharedMemory().releaseTaskMappings(*this);
        {
            PreemptionSpinLockGuard guard(addressSpace_.lock());
            oldRoot = addressSpace_.replaceRoot(newRootPhysicalAddress);
            mm::switchAddressSpace(newRootPhysicalAddress);
        }

        // 新根已经切换后才处理执行关闭标记，相关关闭动作看到的任务应处于已提交的新映像。
        handles_.closeOnExec();
        mm::destroyAddressSpaceRoot(oldRoot);
        device::resourceDomains().addressSpaceReleased(*this);
        {
            InterruptSpinLockGuard guard(lock_);
            acceptsThreads_ = state_ != TaskState::Terminated;
        }

        return true;
    }

    /*
     * 返回进程组号（PGID）。进程组把若干相关任务当作一个“作业”统一管理，
     * 例如终端上的 Ctrl+C 会发给整个前台进程组。
     */
    uint64_t Task::taskGroupId() const { return taskGroupId_; }

    /*
     * 返回会话号（SID）。会话通常对应一次登录或一个终端，进程组隶属于会话。
     */
    uint64_t Task::sessionId() const { return sessionId_; }

    /*
     * 保存命令行参数（各参数以 '\0' 分隔，总长受限），供任务信息查询和核心转储
     * （core dump）的注释使用。
     *
     * 两个细节：
     *   1. 缓冲区定长，放不下就截断，但绝不越过 sizeof(commandLine_)；
     *   2. 内部用于选择控制台的那个参数 "--os-terminal=N"（N 为 1..7，且必须
     *      正好占满整个参数）不会出现在对外公开的命令行列表里。
     * 输入：count 为参数个数，arguments 可以为空指针。调用前提：通常在创建或
     * exec 阶段调用，此时任务尚未对外可见。
     */
    void Task::setCommandLine(uint32_t count, const char* const* arguments) {
        size_t used = 0;

        for (uint32_t i = 0; i < count && used + 1 < sizeof(commandLine_);
             ++i) {
            const char* text = arguments ? arguments[i] : nullptr;

            if (!text) {
                continue;
            }

            constexpr char prefix[] = "--os-terminal=";
            size_t p                = 0;

            while (prefix[p] && prefix[p] == text[p]) {
                ++p;
            }

            if (!prefix[p] && text[p] >= '1' && text[p] <= '7' &&
                !text[p + 1]) {
                continue;
            }

            while (*text && used + 1 < sizeof(commandLine_)) {
                commandLine_[used++] = *text++;
            }

            commandLine_[used++] = 0;
        }

        commandLineSize_ = used;
    }

    /*
     * 把本任务加入指定的进程组。
     *
     * 规则：
     *   1. 参数为 0 表示“拿我自己的 PID 当组号”，即新建一个以自己为首的组；
     *   2. 目标组的组长必须存在，并且与本任务属于同一个会话——本机制不允许
     *      跨会话加入进程组；
     *   3. 拿到组长引用（带引用计数，因此期间不会被释放）后再复核一次会话号，
     *      防止两次检查之间组长被别人换掉（所以代码里做了两次检查）。
     * 失败返回 false：组长不存在、跨会话，或加锁后复核不通过。
     */
    bool Task::setTaskGroup(uint64_t taskGroupId) {
        if (taskGroupId == 0) {
            taskGroupId = id_;
        }

        ::task::TaskReference leader = task::find(taskGroupId);

        if (leader == nullptr || leader->sessionId() != sessionId_) {
            return false;
        }

        InterruptSpinLockGuard guard(lock_);

        if (leader->sessionId() != sessionId_) {
            return false;
        }

        taskGroupId_ = taskGroupId;

        return true;
    }

    /*
     * 创建一个新会话：会话号和进程组号都取本任务的 PID，同时脱离原来的终端。
     *
     * 拒绝条件：如果本任务已经是进程组组长（taskGroupId_ == id_），说明它已经
     * 是某个会话/进程组的首进程，直接返回 false。
     * 细节：若已有认证记录，authenticationSession_ 要跟着改成新的会话号，而不是
     * 继续沿用旧会话号，否则认证模块会认为它仍属于旧会话。
     * 终端归属（terminalId_/terminalKind_）清零表示不再有控制终端。
     */
    bool Task::createSession() {
        InterruptSpinLockGuard guard(lock_);

        if (taskGroupId_ == id_) {
            return false;
        }

        sessionId_ = id_;

        if (authenticationSession_) {
            authenticationSession_ = id_;
        }

        taskGroupId_  = id_;
        terminalKind_ = terminalId_ = 0;

        return true;
    }

    /*
     * 任务子系统的全局初始化。
     *
     * 步骤：
     *   1. 清空全局任务链表，建立内核任务：它使用内核自己的页表根、没有父任务，
     *      代表内核本身，由 allocateId() 分到第一个编号；
     *   2. 把引导线程（bootstrap thread）挂到内核任务上，并让当前 CPU 记录它；
     *   3. 初始化回收器、核心转储、设备资源域、服务恢复等基础设施。
     *
     * 为什么顺序不能反：这些后台设施负责在任务退出后异步清理资源，必须先于任何
     * 用户进程可用；否则第一批用户进程退出时就没人回收了。
     * 失败路径：连内核任务都建立不起来（内存不足）时，打印错误并停机（halt），
     * 因为此时系统无法继续运行。
     */
    void init() {
        taskListLock.init();
        taskList      = nullptr;
        void* storage = mm::allocateHeap(sizeof(Task));
        Task* task    = storage == nullptr ? nullptr : new (storage) Task;

        if (task == nullptr ||
            !task->init("kernel", mm::getKernelAddressSpaceRoot(), nullptr)) {
            device::serialWrite("[TASK] unable to init kernel task\n");
            cpu::halt();
        }

        kernelTask = task;
        taskList   = task;
        kernelTask->attachThread(currentThread());
        cpu::getCurrent()->setCurrentTask(kernelTask);
        taskReclaimer().init();
        coreDump().init();
        device::resourceDomains().init();
        serviceRecovery().init();
        device::serialWrite("[TASK] kernel task initialized\n");
    }

    /*
     * 返回当前 CPU 正在执行的任务；内核工作线程可能没有归属任务，此时返回
     * nullptr。
     */
    Task* getCurrent() { return cpu::getCurrent()->getCurrentTask(); }

    /*
     * 返回内核任务对象。每个 CPU 的空闲线程（idle thread）存放在各自的运行队列
     * （run queue）里，不通过这里返回。
     */
    Task* getIdle() { return kernelTask; }

    /*
     * 按 PID 查找任务。
     *
     * 为什么返回 TaskReference 而不是裸指针：查找在全局链表锁内完成，但返回时
     * 锁就释放了；若直接返回裸指针，任务可能立刻被摘链并释放。返回值带一份引用，
     * 保证调用方使用期间对象有效。
     * 找不到时返回空引用（可隐式转换为 nullptr）。
     */
    TaskReference find(uint64_t taskId) {
        InterruptSpinLockGuard guard(taskListLock);

        for (Task* task = taskList; task != nullptr; task = task->nextGlobal_) {
            if (task->id() == taskId) {
                return task;
            }
        }

        return nullptr;
    }

    /*
     * 按序号枚举任务（跳过内核任务），供任务服务等场景逐个列出任务。
     * 注意：序号不是稳定身份——两次调用之间若有任务创建或退出，同一序号可能对应
     * 不同的任务。需要稳定遍历时请用 next()，按 PID 前进。
     */
    TaskReference at(uint32_t index) {
        InterruptSpinLockGuard guard(taskListLock);

        for (Task* task = taskList; task; task = task->nextGlobal_) {
            if (task == kernelTask) {
                continue;
            }

            if (!index--) {
                return task;
            }
        }

        return nullptr;
    }

    /*
     * 返回 PID 不小于 minimumId 的最小非内核任务，用作“按 PID 递增”的遍历游标。
     * 为什么按 PID 前进：链表位置在多次调用之间不稳定，而 PID 由递增序列分配，
     * 用它递增游标可以避免因链表位置变化而重复返回；这不是快照，已退出并摘链的任务仍可能看不到。
     * 找不到时返回空引用，调用方据此结束遍历。
     */
    TaskReference next(uint64_t minimumId) {
        InterruptSpinLockGuard guard(taskListLock);
        Task* selected = nullptr;

        for (Task* task = taskList; task; task = task->nextGlobal_) {
            if (task != kernelTask && task->id() >= minimumId &&
                (!selected || task->id() < selected->id())) {
                selected = task;
            }
        }

        return selected;
    }

    /*
     * 读取某个终端槽位当前的前台进程组号（前台进程组负责接收键盘信号，例如
     * Ctrl+C 产生的 SIGINT）。
     * kind 是终端类别（1 或 2），index 是槽位号（0..7）；越界时返回 0。
     * 用原子读是为了避免读到被写侧撕裂的值：另一个 CPU 可能正在更新同一个槽位。
     */
    uint64_t terminalForeground(uint32_t kind, uint32_t index) {
        return kind >= 1 && kind <= 2 && index < 8
                   ? __atomic_load_n(&terminalForegroundGroups[kind - 1][index],
                                     __ATOMIC_ACQUIRE)
                   : 0;
    }

    /*
     * 设置终端的前台进程组号，成功返回 true。
     *
     * 边界检查：kind 只能是 1 或 2；槽位上限按类别不同——kind 1 只有 6 个槽位，
     * kind 2 有 8 个。
     * 权限判断不在这里做：调用者是否有权修改某个终端，由任务服务（task
     * service） 负责校验；本函数只检查类别和槽位是否合法。 使用原子写（release
     * 语义）与读侧的原子读配对。
     */
    bool setTerminalForeground(uint32_t kind, uint32_t index, uint64_t group) {
        if (kind < 1 || kind > 2 || index >= (kind == 1 ? 6U : 8U)) {
            return false;
        }

        __atomic_store_n(&terminalForegroundGroups[kind - 1][index], group,
                         __ATOMIC_RELEASE);

        return true;
    }

    /*
     * 向整个进程组发送信号；signal 为 0 时只做“存在性检查”，不真正投递信号
     * （类似 kill(pid, 0) 的语义）。
     *
     * 为什么要分批：全局任务链表锁是自旋锁，持锁期间不能做可能阻塞或终止调用者
     * 的操作（投递信号就可能如此）。所以每轮只在锁内抓取“最多 32 个带引用的
     * 任务”，放锁后再逐个投递，并用 before 游标继续下一批。
     * 安全性：抓的是 TaskReference，锁释放后任务对象仍然有效；绝不能把链表的裸
     * 指针或链表锁带进投递过程。
     *
     * 权限规则：优先使用显式传入的调用者，否则取当前任务；实际调用者为空、或调用者
     * euid 为 0、或调用者 uid 与目标 任务 uid 相同、或调用者 euid 等于目标任务
     * uid 时，才允许投递。 对于真正的信号（signal 非
     * 0），托管服务任务会被跳过；signal 为 0 时则只做
     * 上述权限检查、组成员判断和存活检查。
     *
     * 返回值：只要有一个任务成功投递，或 signal 为 0 时存在性检查为真，就返回
     * true。
     */
    bool sendSignalToTaskGroup(uint64_t taskGroupId, uint32_t signal,
                               Task* authenticatedCaller) {
        if (taskGroupId == 0 || (signal != 0 && !task::isValidSignal(signal))) {
            return false;
        }

        bool delivered  = false;
        uint64_t before = UINT64_MAX;

        while (true) {
            TaskReference batch[32];
            uint32_t count = 0;
            {
                InterruptSpinLockGuard guard(taskListLock);

                for (Task* task = taskList; task && count < 32;
                     task       = task->nextGlobal_) {
                    if (task->id() >= before) {
                        continue;
                    }

                    batch[count++] = TaskReference(task);
                }
            }

            if (!count) {
                break;
            }

            before = batch[count - 1]->id();
            // 投递可能阻塞，甚至导致调用者被终止。绝不能把全局任务链表锁带进
            // 投递过程；任务对象本身的生存期由 TaskReference 保证，比它的地址
            // 空间活得更久。
            for (uint32_t i = 0; i < count; ++i) {
                Task* task           = batch[i];
                Task* caller         = authenticatedCaller ? authenticatedCaller
                                                           : task::getCurrent();
                const bool permitted = !caller || caller->effectiveUid() == 0 ||
                                       caller->uid() == task->uid() ||
                                       caller->effectiveUid() == task->uid();

                if (permitted && (!signal || !task->managedService()) &&
                    task->taskGroupId() == taskGroupId) {
                    delivered = (signal == 0 ? !task->isTerminated()
                                             : task->deliverSignal(signal)) ||
                                delivered;
                }
            }
        }

        return delivered;
    }

    /*
     * 只把本任务从全局任务链表中摘掉，不释放任何引用。
     * 调用前提：由 wait 路径（父任务收走退出状态之后）或回收器调用。
     * 为什么分开：链表中的对象由创建时那份引用保活，摘链只表示“不再对外可见”；
     * 真正的 release() 由调用方在合适的时机单独完成，以免出现重复释放。
     */
    void Task::unlinkGlobal() {
        InterruptSpinLockGuard guard(taskListLock);
        Task** link = &taskList;

        while (*link && *link != this) {
            link = &(*link)->nextGlobal_;
        }

        if (*link == this) {
            *link = nextGlobal_;
        }

        nextGlobal_ = nullptr;
    }

}  // namespace task
