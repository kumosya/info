# 同步与用户地址等待

声明位于 `include/kernel/sync.h`，实现位于 `src/kernel/task/sync/`，命名空间统一为 `kernel::task`。`sync.cc` 实现可阻塞同步和线程清理登记，`wait.cc` 实现用户地址等待；它与 task 根目录中处理进程和 join 的 wait.cc 职责不同。

## 自旋锁及作用域锁

`SpinLock` 私有保存一个锁字节，lock 使用 acquire 原子置位并在竞争时 pause，unlock 使用 release 清零。它不负责保存中断或抢占状态。

| 类或成员函数 | 功能与参数 |
| --- | --- |
| `SpinLock::init()` | 初始化未使用的锁；不能用来重置正在被持有的锁。 |
| `lock()`、`tryLock()`、`unlock()` | 自旋获取、尝试获取、释放。 |
| `InterruptSpinLockGuard(lock)` | 保存并关闭当前 CPU 中断后加锁，析构时解锁并恢复原中断状态。 |
| `PreemptionSpinLockGuard(lock)` | 禁止当前 CPU 抢占后加锁，析构时解锁并恢复抢占；中断仍可到达。 |

中断处理可能使用的锁应阻止同 CPU 的中断重入。地址空间等需要保持中断可用的路径按已有锁约束使用 PreemptionSpinLockGuard。禁止持有自旋锁进入会等待调度的操作；需阻塞时先登记等待，再释放锁。

## 等待队列、互斥锁和信号量

这些对象保存自己的 SpinLock 和等待线程头尾；链表通过 Thread 的 waitNext 连接。唤醒只使线程具备运行条件，不保证马上执行或优先取得资源。

| 类与成员函数 | 功能与参数 |
| --- | --- |
| `WaitQueue::init()` | 清空等待链并初始化锁。 |
| `WaitQueue::wait()` | 将当前线程挂到队尾，然后阻塞。它本身不保存业务条件。 |
| `wakeOne()` | 摘下一个等待者，解除登记并唤醒；返回是否取到线程。 |
| `wakeAll()` | 依次唤醒全部等待者，返回数量。 |
| `Mutex::init()` | 清空所有者和等待链。 |
| `Mutex::lock()` | 无所有者时取得锁，否则登记等待，唤醒后循环竞争。 |
| `Mutex::tryLock()` | 只尝试一次，成功返回 true。 |
| `Mutex::unlock()` | 只有当前所有者能够释放；清空 owner 后唤醒一个等待者。 |
| `Semaphore::init(count)` | 设置初始许可数量。 |
| `Semaphore::wait()` | 有许可时减一，否则登记并等待 signal 交付。 |
| `Semaphore::tryWait()` | 有许可才减一，返回是否成功。 |
| `Semaphore::signal()` | 有等待者时直接唤醒一个，否则增加许可数量。 |

Mutex 不支持递归，也没有优先级继承。WaitQueue 不是带条件锁交接的条件变量；业务层不能先无锁检查条件、随后 wait，并期待它自动消除这两步间的竞态。

## 一次初始化

Once 保存 state_ 和等待链。state_ 为 0 时未执行，1 时正在执行，2 时已经完成。

`Once::init()` 初始化对象。`run(initializer, argument)` 将第一位调用者设为执行者，在锁外运行 initializer(argument)，完成后发布状态并唤醒等待者。已完成时直接返回；执行中时登记并等待。

当前实现没有初始化线程死亡后的接管或失败重试协议，不应把会被强制终止的任意业务操作放进 initializer。

## 登记等待与取消等待

`Thread::registerWait(lock, head, tail)` 记录等待链的锁、头指针及可选尾指针；它本身不把线程挂进链。`clearWaitRegistration()` 清除这些指针，供正常摘链使用。`cancelWait()` 在对应锁下查找并移除该线程，修复头尾，供退出或 exec 清理使用。

建立等待时的顺序是：

1. 持保护条件的锁，检查条件。
2. 调用 prepareCurrentWait，登记等待位置并挂链。
3. 释放条件锁，再调用 blockWaitingThread。

prepareCurrentWait 和 wakeThread 使用运行队列锁配合 wakePending，覆盖已经登记但尚未真正阻塞的窗口。blockWaitingThread 还检查 IPC 取消或中断结果，已有错误时取消等待登记。

强制取消另一个线程的等待前，调用者必须已停止它并确认 CPU 完成切换；否则线程可能仍在读写链表或栈。一个 Thread 的通用 waitNext 不能同时挂入多个独立等待链。

## ThreadCleanup

ThreadCleanup 保存所属线程、前后链接、对象指针和释放回调。它让内核临时引用既能在正常返回时释放，也能在线程没有执行 C++ 栈展开便被终止时释放。

| 成员函数 | 功能与参数 |
| --- | --- |
| `attach(object, release)` | 将对象及回调登记到当前线程清理链；release 接收 object。 |
| `detach()` | 摘下登记并清空内容，不执行释放回调，供转移所有权使用。 |
| `reset()` | 摘下登记，再执行一次释放回调。 |
| 析构函数 | 调用 reset。 |
| `releaseAll(thread)` | 依次清理指定线程的全部登记；由退出、exec 或回收路径调用。 |

TaskReference、HandleTable::Pin、端点活动引用和较大的系统调用缓冲区使用该机制。含清理登记的线程栈不能提前释放；接管引用时必须同步摘下旧登记，避免重复释放。

## 用户地址等待

`waitOnUserAddress(address, expected)` 和 `wakeUserAddress(address, count)` 是 WaitAddress、WakeAddress 的实现基础。address 指向对齐的 32 位用户字，expected 为比较值，count 为最多唤醒数量。

实现使用 64 个哈希桶。waitKey 在地址空间锁内将虚拟地址转换为物理地址，以物理地址作为键，因此映射到同一物理位置的进程可以匹配等待和唤醒。调用时该地址必须已经能够解析为合法映射。

等待时持桶锁再次读取该 32 位值；不等于 expected 就返回 1，不入队；相等才登记并阻塞。无效地址返回 -1，正常等待返回 0。系统调用入口分别转换为 WouldBlock、InvalidArgument 和成功。

唤醒按键查找最多 count 个线程，在桶锁下摘链并解除登记，再在锁外唤醒，返回实际数量。count 为 0 或键无效时返回 0。

当前接口不提供等待期限、重排队、优先级继承或对映射生命周期的固定。等待期间解除映射或更换对应物理页不由此机制自动协调；使用者应保持同步字和映射有效，并在返回后重新检查共享状态。
