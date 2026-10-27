# 任务资源限额与优先级

实现位于 `src/kernel/task/resource/resource.cc`，声明和状态位于 `include/kernel/task.h`，命名空间为 `kernel::task`。nice 权重的修改由 `sched/cfs.cc` 执行；此处处理目标选择、权限、限额和查询。

## 资源限额

Task 保存 core 与 nice 的软限额、硬限额。创建和 fork 继承父进程值，exec 保留。当前 core 默认软、硬限额均为 8 MiB，nice 默认软、硬限额均为 0。

`resourceLimit(caller, resource, set, soft, hard)` 在目标 Task 上执行。caller 是经过认证的真实调用者；resource 选择资源；set 为 false 时只查询，为 true 时修改；soft、hard 输入新限额，并在成功时返回修改前的值。

| 资源编号 | 含义 | 实际作用 |
| --- | --- | --- |
| 4 / RLIMIT_CORE | core 文件大小限额 | 快照上限取进程软限额与全局 core 策略上限的较小值；零禁止转储。 |
| 13 / RLIMIT_NICE | 允许提升调度优先级的范围 | 非特权线程降低 nice 时受软限额约束。 |
| 其他合法编号 | 尚未实现 | 返回 95，不能当作已生效。 |
| 范围外编号 | 参数错误 | 返回 22。 |

soft 不能大于 hard。提高硬限额需要 caller 的有效 UID 为 0。普通跨进程访问要求 caller 的真实 UID/GID 与目标的真实、有效及保存 UID/GID 全部匹配；操作自身不受这项跨进程匹配限制。

`setCoreLimit(soft, hard)` 是当前进程 core 专用入口，执行软硬关系和硬限额提升检查。相关服务请求见 [任务机制服务](service.md)，实际采集见 [core](core.md)。

## 优先级请求

`applyTaskPriority(caller, current, operation, which, who, value, result)` 实现查询、绝对设置和 nice 相对调整。

| 参数 | 数据 |
| --- | --- |
| caller | 实际调用进程，用于权限判断。 |
| current | 当前线程，用于 who 为 0 的默认目标和 nice 相对操作。 |
| operation | 0 查询，1 绝对设置，2 相对 nice；相对操作只接受 which=0、who=0。 |
| which | 0 按线程编号，1 按进程组，2 按真实 UID。 |
| who | 对应编号；0 采用当前线程编号、当前进程组或当前真实 UID。 |
| value | 要设置的 nice，或相对增量。 |
| result | 查询值或修改后的 nice。 |

按线程编号操作不等同于修改目标进程全部线程；主线程编号等于进程编号。按进程组或 UID 查询返回匹配线程中最小的 nice。组修改逐线程执行，部分目标失败不会回滚已成功修改的线程。

返回值使用正的 errno：0 成功，1 无权操作目标，3 没有匹配目标，13 不允许提高优先级，22 参数不合法。IPC 服务将它放入结果字段，区别于通信层的负错误码。

## nice 权限与记账

普通进程可以增大自己的 nice，即降低调度优先级。修改其他进程的线程要求有效 UID 匹配目标真实或有效 UID，或调用者有效 UID 为 0。

减小 nice 表示提高优先级。特权调用者可调整到 -20；普通调用者受目标 RLIMIT_NICE 软限额约束。限额为 0 时不允许提升；非零限额对应最低 nice 为 `20 - limit`，limit 大于等于 40 时最低为 -20。最终 nice 钳制到 -20～19。

`changeNice` 在目标运行队列锁下修改，先按旧权重结算已经运行的时间，再更新 nice 与 weight，并通知相应 CPU 重新调度，不能通过反复改权重抹掉已经消耗的 CPU 时间。

## 线程和进程统计

| 成员函数 | 功能与参数 |
| --- | --- |
| `mainNice()` | 取得主线程 nice；没有主线程时为 0。 |
| `mainThreadId()` | 取得主线程编号；没有主线程时为 0。 |
| `threadCount()` | 遍历线程链，统计未终止线程。 |
| `threadInfo(minimumTid, info)` | 选择编号不小于 minimumTid 的最小存活线程，复制线程编号、所属 Task、运行纳秒数、nice 和状态。 |

Task 的 CPU 纳秒数由调度记账累加，表示进程线程消耗的 CPU 时间，不是墙上时间。任务信息中的 nice 只代表主线程，不能替代逐线程查询。

线程和任务枚举在各次调用之间可能变化，不提供冻结快照。遍历应以上次编号加一继续，而不是假设固定链表位置一直对应同一对象。
