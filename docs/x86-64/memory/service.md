# 内存机制服务与缺页：请求怎样完成

本文覆盖 `src/kernel/memory/service/service.cc`、`mapping.cc`、`pager.cc` 和 `fault.cc`。MemoryService、PagerBridge 属于 `mm`，端点工厂属于 `task`，缺页入口属于 `cpu`。声明主要位于 `include/kernel/mm.h`，操作码位于 `include/service/common/kernel-requests.h`。

## 1. 分清机制服务和用户态 MM

用户态 MM 决定区域、文件来源和补页策略；memory.kernel 在内核中分配对象、修改页表、查询和锁页。前者请求后者执行机制，后者遇到用户缺页又通过 PagerBridge 请求前者提供策略。

| 术语 | 通俗解释 |
| --- | --- |
| endpoint | 发送和接收消息的通信对象 |
| RPC | 发请求并等待对应回复 |
| caller | 真正发起调用的任务，身份由内核提供 |
| target | 被操作任务；接口中 0 通常表示自己 |
| Pin / TaskReference | 操作期间固定对象，避免对象中途消失 |
| capability | 内核检查的能力，知道 PID 不等于获得权利 |
| generation | 地址空间代数，用来识别 exec 后已更换的映像 |
| 冻结 | 暂停目标内存活动，便于服务恢复时稳定枚举 |

## 2. memory.kernel：授权、分派和返回结果

用户通过 CallService 请求内存机制，Map/Unmap/Protect 没有对应的独立 syscall 8～10。拿到服务端点只代表可以发请求，不代表可以操作任意任务。

### 2.1 完整函数表

| 函数 | 输入、作用和结果 |
| --- | --- |
| `service/mapping.cc::error(value)` | 将系统错误码转换为 uint64_t；与 operations.cc 的同名静态函数分别作用于各自文件 |
| `isMemoryManager(caller)` | 同时检查 Memory 角色、Memory 能力和当前恢复实例身份 |
| `mappingAuthorized(caller, target)` | 目标为 0/自己时允许，否则要求当前 Memory 管理者 |
| `MemoryService(mappings, pager)` | 保存映射表和 PagerBridge 引用，不新建它们 |
| `MemoryService::map(address, size, flags, handle, offset, target)` | 授权并固定目标，按匿名/页对象/共享对象分派；flags 低两位是权限 |
| `MemoryService::unmap(address, size, target, preserveLocks)` | 授权后转 unmapUserPages；preserveLocks 仅当前 Memory 管理者可用 |
| `MemoryService::protect(address, size, protection, target)` | 普通请求转 protectUserPages，特殊 COW 请求仅当前 MM 可用，转 Maps::markCopyOnWrite |
| `readRequestName(request, word, name)` | 检查指定数据位置后是否有界且包含非空、终止的名称，成功 true |
| `copyRequestBytes(destination, source, size)` | 按字节复制固定结果结构，不依赖用户运行库 |
| `requestAuthorized(caller, target)` | 对查询、锁页等请求执行自己/当前 Memory 管理者授权检查 |
| `MemoryService::call(caller, request, reply)` | 校验公共消息长度，再按 MemoryOperation 检查参数、执行操作、填写回复；传输处理错误用负返回值 |
| `task::dispatchMemory(caller, request, reply)` | 内部创建静态 MemoryService 实例，将端点调用转给 call |
| `task::memoryKernelService()` | 返回绑定 dispatchMemory 的静态内核端点服务描述 |

### 2.2 map 和 call 的主要流程

`MemoryService::map` 先确认当前调用者有权操作目标，用 TaskReference 固定目标生命期，再读取 flags 的后备类型：

1. 匿名：要求 handle/offset 为 0，转 mapUserPages。
2. 页对象：固定调用者的 PageObject 句柄，检查所需读写权利，从真实句柄权利计算最大权限，再转 Maps::map。
3. 共享对象：同任务直接转 SharedMemory::map；跨任务先复制临时目标句柄，映射后关闭临时句柄，映射自己保留生命期。
4. 不认识的类型返回参数错误。

`call` 先检查公共最小长度 5 个数据字，再按请求种类补充长度、目标、角色和句柄检查。操作完成统一填写回复长度。涉及自解除映射或降为不可写时，还检查不能破坏当前同步调用的回复缓冲，否则结果将无法写回。

### 2.3 全部 MemoryOperation 分支

这些是 call 的协议分支，不是额外 C++ 函数。列出它们是为了能从用户请求一路对应到上述实现。

| 请求 | 执行内容和主要结果 |
| --- | --- |
| `Map/Unmap/Protect` | 转对应成员函数，业务结果写 reply.data[0] |
| `Open/Unlink/Resize` | 命名共享对象操作；Open 成功同时公布新句柄 |
| `SharedInfo` | 查询共享对象逻辑大小 |
| `UnlinkSharedHandle` | 通过固定句柄移除名称 |
| `AnonymousShared` | 创建匿名对象并 resize，失败关闭临时句柄，成功公布句柄 |
| `TypedOpen` | 只接受 `/memory/ram` 名称，调用 openTyped |
| `TypedAllocate` | 从指定池句柄分配，公布新结果句柄 |
| `TypedInfo` | 查询策略对应的可用字节 |
| `AllocatePages` | 校验最多 12 阶和节点，申请包装并构造 PageObject，valid 后安装句柄；任一步失败 release |
| `PageInfo` | 固定页对象句柄，返回 owner、大小、物理引用、节点、阶数 |
| `Usage` | 返回管理地址跨度、空闲物理字节、目标 owner 字节、地址空间代数、节点数；分别采样，不是全局瞬时快照 |
| `ReclaimCaches` | 要求 Memory 能力，清 CPU 暂存并回收全空缓存页 |
| `BindPager` | 固定有读/接收权利的端点，再交 bind 检查所有者和当前 MM 身份 |
| `Freeze/Thaw` | 仅当前 MM 可冻结/恢复目标，返回地址空间 generation，帮助识别 exec 更换 |
| `FrozenNext` | 按 PID 游标查找下一个仍冻结的存活任务 |
| `MappingNext` | 要求目标已冻结，合并 Maps 与 SharedMemory 的下一段 managed 映射结果 |
| `PageState` | 返回存在、权限、external、locked、accessed/dirty 等选定标志，不暴露物理帧地址 |
| `RangeAvailable` | 检查对齐用户范围内是否已有 PTE；只是查询，不预留虚拟地址 |
| `LockRange` | 调 lockUserPages；允许缺页或只预检的内部选项限制为 Memory 角色使用 |
| `LockAll` | 调 lockAllUserPages；额外延迟区间表检查角色、条数和消息长度 |
| `LockState` | 返回锁页字节数、future 状态和 memlock 限额 |

## 3. PagerBridge 与缺页异常

### 3.1 完整函数表

位置：`service/pager.cc` 和 `fault.cc`。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `pagerBridge()` | 返回全局内核到 MM 的通信桥 |
| `PagerBridge::bind(owner, endpoint)` | 检查 Memory 身份、能力、当前实例和端点归属；增加新端点引用后替换旧绑定，成功 true |
| `PagerBridge::exchange(request, reply, optional)` | 固定当前端点后发 RPC；处理端点死亡/关闭和更换，在本次绝对 20 秒预算内重试，返回通信结果 |
| `PagerBridge::resolve(address, faultFlags, failure)` | 将当前任务的缺页请求交 MM；失败可输出原因，成功后重新核验真实 PTE 和访问权限 |
| `PagerBridge::clone(parent, child)` | 子任务开始运行前通知 MM 复制 VMA/COW 管理状态；拒绝 MM 自身递归克隆，成功 true |
| `PagerBridge::ensureUserMemory(address, size, writable)` | 逐页检查当前任务用户缓冲，必要时 resolve；零长度成功，任一页无法准备则 false |
| `cpu::handlePageFault(context)` | 读取 CR2 和错误码，尝试补页；无法恢复的用户故障转信号，内核故障或保留位错误进入 panic |

### 3.2 一次用户缺页怎样恢复

例如进程第一次读一个已登记、但还没实际分配页面的区域：

1. CPU 查页表失败，进入 handlePageFault；CR2 给出出错虚拟地址，异常错误码给出读/写/执行以及权限信息。
2. 若为可尝试恢复的用户故障且没有保留位错误，调用 resolve。
3. resolve 拒绝越界地址和 MM 自身故障，避免“MM 等待自己处理缺页”。
4. 构造 Fault 请求，exchange 固定端点并释放绑定锁，再向用户态 MM 发请求。等待时允许中断。
5. MM 根据自己的区域和后备策略准备页面，并通过 memory.kernel 建立映射。
6. 收到成功回复后，resolve 再锁当前地址空间，确认真实页表已有用户可访问页；写访问查可写，执行访问查 NX。
7. 验证通过，异常处理返回，CPU 重新执行刚才失败的指令。
8. 不能恢复时，用户错误按原因转为 SIGSEGV 或 SIGBUS；内核故障和页表保留位损坏输出应急信息并 panic。

exchange 的 20 秒截止时间在重试循环外计算，重连不会刷新预算。未观察到端点且 optional=true 时可允许早期启动继续；一旦本次调用已经见过端点，再失联就按恢复和超时处理。

### 3.3 ensureUserMemory 为什么先检查、解锁，再请求

系统调用准备用户缓冲时，范围可能跨多页。ensureUserMemory 对每页短暂取得地址空间锁并检查；已有权限就继续，不满足则先解锁再 resolve。

原因是 MM 补页也要取得同一个目标地址空间锁。如果内核持锁等待 MM，而 MM 等锁才能完成请求，双方就会一直互相等待。

## 4. 一次 AllocatePages 请求怎样走完

1. 用户通过 CallService 发 AllocatePages 消息。
2. memoryKernelService 使用 dispatchMemory 将真实 caller 和消息交给 call。
3. 检查消息长度、最多 12 阶、节点范围和目标 owner 授权。
4. 从堆申请包装，构造 PageObject，申请物理 backing 并清零。
5. 检查 valid，失败 release 包装并返回错误。
6. 安装带权利的句柄，失败同样 release。
7. 成功写 reply.data[0] 并将新句柄放入 reply.handles。

包装存储、物理块、句柄安装是三次不同的成功条件，不能只检查第一步。

## 5. 一次跨进程页对象映射

当前 MM 持页对象句柄 H，计划映射到目标 T 的地址 V：先验证真实 MM 身份，固定 T，再从 caller 的表固定 H 并检查类型和读写权利，从真实权利计算 maximumProtection，最后调用 Maps::map。它先取记录再建 external PTE，成功后保留对象引用；H 后续关闭不影响映射生命期。

共享对象跨目标路径会临时复制目标本地句柄，映射后立即关闭，最终由 view 保留使用关系。

## 6. 端点绑定和恢复等待的执行细节

### 6.1 bind 怎样替换端点

检查 owner 的 Memory 能力、角色、当前恢复身份和端点归属；加锁后拒绝抢占另一任务的活动绑定。先 retain 新端点再 release 旧端点，即使新旧相同也不会中途销毁。

### 6.2 exchange 怎样重连

保存原中断状态、允许中断，计算本次绝对 20 秒截止时间。每轮锁内取端点并 retain，放锁后 RPC，结束该轮后 release。端点死亡或关闭时重新读取绑定并短暂睡眠；其他通信结果直接返回，到期超时。所有返回路径恢复原中断状态，重连不会延长截止时间。

optional 只允许本次未见端点时的早期缺省成功，不代表忽略所有失败。

### 6.3 clone 怎样同步 MM 状态

拒绝父任务就是 MM，构造携带子任务 ID 的 Fork 请求，使用 optional exchange。调用方需在子线程运行前完成同步，它不代替内核页表克隆、句柄继承和共享对象引用继承。

## 7. 服务恢复查询如何配合

FrozenNext 按 PID 找冻结任务；MappingNext 合并页对象和共享 view 范围；PageState 查实际标志；Usage 获取 owner 占用和地址空间代数；完成恢复后 Thaw。

MappingNext 只认可按真实 Memory 调用身份登记的 pagerManaged 映射。RangeAvailable 不保留地址，Usage 分项采样也不是全局同时快照。

## 8. 故障与返回结果怎么判断

用户故障可恢复则交 Pager，成功后核验页表再重试；无效地址或权限问题按原因转 SIGSEGV，文件末尾和后端问题可转 SIGBUS。MM 自身不递归请求自己；内核故障或页表保留位错误进入应急输出和 panic。

handlePageFault 的用户信号分类还兼顾用户地址执行的系统线程，实际 Pager 恢复分支要求用户特权级故障。CR2 是地址，errorCode 是原因和访问方式。

call 的负返回值表示消息处理错误；业务数据由具体分支规定。锁页结果 0/22/12 位于 reply.data[1]，不能只查 data[0]。自操作不能删除当前同步回复区或撤掉其写权限，否则内核无法返回结果。

## 9. 继续阅读

映射细节见 [虚拟内存](virtual.md)，对象接口见 [共享内存](shared.md) 和 [对象与堆](object.md)，用户态策略见 [MM 服务](../../service/mm.md)，信号与转储见 [core](../task/core.md)。
