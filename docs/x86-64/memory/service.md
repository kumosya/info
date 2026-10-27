# memory.kernel 与 PagerBridge

实现位于 `src/kernel/memory/service/service.cc`、`mapping.cc` 和 `pager.cc`，主要声明在 `include/kernel/mm.h`。MemoryService 和 PagerBridge 位于 `kernel::mm`；端点工厂 `memoryKernelService()` 位于 `kernel::task`。

MemoryService 保存 Maps、PagerBridge 的引用，在经过认证的当前调用线程内执行。用户态 mm 进程通过 PagerBridge 接收策略请求；二者不是同一个服务执行上下文。

## 映射成员函数

| 成员函数 | 功能与参数 |
| --- | --- |
| `map(address, size, flags, handle, offset, target)` | flags 低两位是 write/execute，其余选择匿名、页对象或共享对象；handle 在调用者表中解释，offset 是对象字节偏移，target 为 0 时自身。 |
| `unmap(address, size, target)` | 授权并保留目标，进入范围解除及拆分准备。 |
| `protect(address, size, protection, target)` | 执行受对象上限约束的保护；特殊 COW 标记仅当前 MM 可以设置。 |
| `call(caller, request, reply)` | 校验消息格式并分派全部 MemoryOperation，caller 是实际当前 Task。 |

跨任务 map/unmap/protect 及特权查询要求 Memory 角色、Memory 能力、当前恢复域实例同时满足。不能从客户端传入角色或 PID 就取得授权。

PageObject 映射固定调用者的 backing 句柄，并将原始句柄权利记录为最大保护；共享对象跨目标映射通过临时目标本地句柄完成，成功后映射独立持有生命期，再关闭临时槽。

## 请求分工

| 请求 | 行为 |
| --- | --- |
| Map / Unmap / Protect | 上述映射机制；自操作不能删除当前同步回复区或撤掉其写权限。 |
| BindPager | 固定接收端点，再验证 owner、角色、能力和恢复所有权后绑定。 |
| Freeze / Thaw | 停止或恢复普通目标，返回地址空间 generation。 |
| FrozenNext | 按 PID 查找仍被冻结的存活进程，供 MM 重启恢复。 |
| MappingNext | 要求目标冻结，合并枚举 PageObject 和共享对象的下一个 managed 区间。 |
| PageState | 返回 selected PTE 权限及 accessed/dirty 状态，不返回物理帧地址。 |
| RangeAvailable | 校验并查询整段地址是否已有 PTE；结果不是解锁后的地址保留。 |
| Open / Unlink / Resize / SharedInfo / UnlinkSharedHandle | 命名共享对象操作和大小查询。 |
| AnonymousShared | 创建匿名共享对象，设置大小，失败关闭未发布句柄。 |
| AllocatePages / PageInfo | 分配、清零、返回 PageObject 句柄，或查询其 owner、大小、物理引用、节点与阶数。 |
| Usage | 总地址跨度、空闲物理字节、目标 owner 字节、代数和 NUMA 数量；不是同一瞬间冻结快照。 |
| ReclaimCaches | 要求 Memory 能力，drain 后回收空 slab，返回字节数。 |

请求至少有公共最小长度，变长操作另外核对 payload。传输错误用负返回值，映射等业务结果在 reply.data[0]；新对象同时放入 reply.handles。创建包装成功但 backing 或句柄安装失败时，必须 release 对象。

## PagerBridge 的绑定和请求

| 成员函数 | 功能与参数 |
| --- | --- |
| `bind(owner, endpoint)` | 仅当前获授权 Memory 实例可绑定自己拥有的接收端点，保存独立引用。 |
| `resolve(address, faultFlags, failure)` | 请求当前进程缺页恢复；failure 可接收失败原因，成功后重新验证实际页权限。 |
| `clone(parent, child)` | 在子线程运行前同步用户态 VMA/COW 记录；禁止 pager 递归克隆自身。 |
| `ensureUserMemory(address, size, writable)` | 逐页确认合法 backing，必要时 resolve，供 syscall 缓冲区和线程入口准备使用。 |
| `exchange(request, reply, optional)` | 在一个绝对 20 秒预算内发送并应对端点死亡、关闭或替换，返回原通信结果或超时。 |

exchange 在 pagerLock 内固定端点引用，释放锁后才 RPC；阻塞期间允许中断，结束后恢复原中断状态。optional 允许尚未出现过 Pager 的早期启动，不表示已经观察到服务后失联也能静默成功。

ensureUserMemory 在短地址空间锁内检查，必须解锁后才向 Pager 请求；Pager 需要取得同一个目标锁建立 PTE，持锁等待会构成死锁。resolve 还拒绝用户范围外、保留位页表损坏和 Memory 服务自身递归故障，成功回复后核验 user/write/execute。

## 缺页与失败边界

fault.cc 的 `kernel::cpu::handlePageFault(context)` 首先读取 CR2。可恢复的 Ring3 缺页交 Pager，成功便重试原指令；未映射、权限错误、文件 EOF 和后端错误分别形成适当 SIGSEGV/SIGBUS。内核故障和页表保留位错误进入应急输出与 panic，不能当成普通缺页继续重试。

本服务维护机制与内存引用，不直接读取用户文件路径，也不在异常入口写磁盘。具体用户态策略见 [MM 服务](../../service/mm.md)，信号与诊断见 [core](../task/core.md)。
