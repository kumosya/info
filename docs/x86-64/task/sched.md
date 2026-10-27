# 线程调度

调度器位于 `src/kernel/task/sched/`，声明在 `include/kernel/task.h`，命名空间为 `kernel::task`。每个 CPU 有自己的 RunQueue，按 CFS 风格的虚拟运行时间选择线程。

## 四个文件的边界

| 文件 | 内容 |
| --- | --- |
| `rbtree.cc` | 只维护红黑树、最小节点和队列基础状态，不负责切换 CPU 现场。 |
| `cfs.cc` | nice 权重、运行时间记账、入队出队、亲和性和迁移入口。 |
| `schedule.cc` | 每 CPU 初始化、选取与切换、阻塞唤醒、睡眠、定时处理、退役栈和远端停稳。 |
| `switch.S` | 实际保存寄存器和切换栈，以及进入或恢复用户现场。 |

## RunQueue 与红黑树

RunQueue 私有数据包括 root、leftmost、当前线程、idle 线程、睡眠链表、就绪节点数、minVruntime、时钟基准和锁。线程本身保存树节点指针、颜色和 inRunQueue 标记，不另行分配树节点。

| 成员函数 | 功能与参数 |
| --- | --- |
| `clear()` | 清空新分配队列的初始状态。 |
| `insertNode(thread)` | 按 vruntime、再按线程编号插入并修复红黑树，同时维护 leftmost 和节点计数。 |
| `removeNode(thread)` | 摘除节点，修复树和最小节点；节点必须属于该队列。 |
| `rotateLeft(node)`、`rotateRight(node)` | 调整局部树结构，维护父子指针。 |
| `insertFixup(node)` | 修复插入造成的红色相邻和黑高问题。 |
| `transplant(target, replacement)` | 将 target 所在位置交给 replacement。 |
| `deleteFixup(node, parent)` | 修复删除后的黑高；空节点也需要 parent 定位。 |
| `minimum(node)` | 查找该子树最小节点。 |
| `comesBefore(left, right)`、`isRed(node)` | 比较排序键和查询节点颜色，供树操作使用。 |

操作已有队列时必须持有其锁。运行线程已经从红黑树摘下；不能将树节点数量直接当作包含当前线程的总可运行线程数。

## 权重和时间记账

nice 范围是 -20 到 19，数值越小权重越大。nice 0 的权重为 1024，边界权重分别是 88761 和 15。虚拟时间增量按下式向上取整：

```text
deltaVruntime = (elapsedNanoseconds * 1024 + weight - 1) / weight
```

同样的实际运行时间，高权重线程增加较少的虚拟时间，因此较容易再次被选中。默认线程时间片为 4 ms；这不是实时调度保证。

| 函数 | 功能与参数 |
| --- | --- |
| `getSchedulerWeight(nice)` | 将 nice 钳制到支持范围，查询权重。 |
| `RunQueue::accountTick(current, elapsed)` | 累加当前非 idle 运行线程的实际时间、时间片时间、vruntime 和进程 CPU 时间。时间片耗尽或有更小 vruntime 的就绪线程时返回 true；调用者持队列锁。 |
| `changeNice(thread, value, relative, privileged, limit, result)` | 对目标线程改变 nice。relative 控制相对增加或绝对设置，privileged 表示可绕过提升限制，limit 是 RLIMIT_NICE 软限额，result 返回调整后的值。 |

changeNice 先按旧权重结算队列当前运行线程的时间，再修改目标权重并请求调度。普通调用者可以增大 nice；减小 nice 必须有特权，或满足资源限额允许的最低 nice。完整权限和进程组操作见 [资源管理](resource.md)。

## 入队、出队和 CPU 选择

`enqueueThread(thread)` 优先使用线程原 CPU；若该 CPU 不可用或不在亲和掩码内，则扫描允许且在线的 CPU。入队前将过小的 vruntime 提升到目标队列的 minVruntime。目标进程处于内存恢复冻结时，线程保留为 Stopped，不插入运行树。远端入队后发送重新调度请求。

`dequeueThread(thread)` 持目标队列锁摘下节点；`pickNextThread()` 只查询当前队列的最小节点或 idle，不执行摘除和切换。

`Thread::setAffinity(cpuMask)` 保存非零掩码；当前 CPU 不再允许时尝试迁移。`Thread::migrate(cpu_id)` 只接受 Ready 线程，检查目标 CPU 在线且位于新掩码内，从原队列摘下线程，先保存目标 CPU 编号，再交给 enqueueThread 完成接纳。返回成功表示接纳成功；目标 CPU 可能已经运行、阻塞甚至结束该线程，不能再用返回瞬间的 Ready 状态判断迁移成败。

启动时的亲和性探针先创建为未入队的 Ready 线程，再设置 CPU 1 掩码，使首次接纳与目标 CPU 一致，避免默认掩码下提前执行。setAffinity 迁移失败时仍保留新掩码，返回 false 不代表回滚；当前也没有周期性的跨 CPU 负载均衡。

## 调度与实际切换

`initScheduler()` 为当前 CPU 建队列，登记引导线程、创建 idle、注册向量 32 并配置本地 APIC 定时器。idle 在没有就绪线程时执行 `sti; hlt`。

`schedule()` 关中断并持本 CPU 队列锁，将仍为 Running 的旧线程放回树，摘下最小节点或选 idle，更新当前线程、所属 Task 和时间片。释放队列锁后，设置目标内核栈并切换页表根，再调用 `Thread::switchTo(next)`。

`switchThreadContext(previousStackPointer, nextStackPointer)` 将 RFLAGS 和 15 个通用寄存器压到旧内核栈，保存 RSP 后换栈，按相反顺序恢复并返回。previousStackPointer 是旧线程保存 RSP 的位置，nextStackPointer 是目标栈上已保存的帧。

`finishContextSwitch()` 在旧栈不再执行后处理退役栈；只有没有活动 IPC 或清理登记时才释放它，再通知 Task 线程栈已退役。随后发布 executingTasks，表示该 CPU 已完成进入新 Task。

`enterUserMode` 构造 Ring3 IRET 帧。`resumeUserContext` 复制保存的 SyscallContext，再进入统一 userContextReturn；用户返回使用 IRET，能够恢复 RCX、R11 和信号上下文需要的标志。

## 阻塞和唤醒

等待者必须在保护等待条件的锁内调用 `prepareCurrentWait()` 并登记等待，释放条件锁后再调用 `blockWaitingThread()`。

`prepareCurrentWait()` 在运行队列锁下清除 wakePending。`blockCurrentThread()` 在同一队列锁下检查该标志：若已经有唤醒则消费标志直接返回，否则设 Blocked 并调度。这样，登记等待后、真正阻塞前发生的唤醒不会丢失。

`wakeThread(thread)` 对 Running 或 Ready 线程设置 wakePending，对 Blocked 或 Sleeping 线程将其变为 Ready 并入队，对 Stopped 线程只更新恢复后的状态。唤醒睡眠线程时先摘下睡眠链；远端线程就绪后通知其 CPU。

`requestReschedule()` 只设当前 CPU 标志。`yieldThread()` 设置标志后立即调用 schedule。唤醒不是直接切入被唤醒线程，其执行顺序仍由运行队列决定。

## 时钟、睡眠和终止

`schedulerTick()` 计算距离队列上次时钟的实际纳秒数，调用 accountTick；再将睡眠链上到期线程移入就绪树，最后调用 `Endpoint::expireWaits(now)` 处理 IPC 超时。睡眠链按截止时间排列。

`sleepCurrentThread(milliseconds)` 在值为 0 时让出 CPU；否则将当前线程置 Sleeping，计算饱和截止时间并挂入睡眠链，然后调度。

`terminateThread(thread)` 从运行树或睡眠链摘下线程，标记 Terminated，通知目标 CPU 重调度。它不等于该线程已经离开 CPU，也不释放栈和页表。

`quiesceTask(task)` 同时检查远端 CPU 已选择的线程和已完成切换的 executingTasks，等待两者都离开目标 Task。调用时不能持 Task 锁、地址空间锁或运行队列锁；等待期间保持中断可用。exec 和回收器必须在释放旧页表、线程栈之前完成这一步。
