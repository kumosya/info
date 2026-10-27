# 共享内存对象

实现位于 `src/kernel/memory/shared/shared.cc`，声明在 `include/kernel/mm.h`，命名空间为 `kernel::mm`。SharedMemory 保存有界的对象、打开包装和映射表，以及 registryLock。

## 三种独立生命期

SharedMemoryObject 保存 used、linked、名称、逻辑大小、物理页数组、references 和 mappings。名称允许再次查找；references 计算打开包装；mappings 计算仍存活的映射记录。

进程句柄通过 HandleTable::Reference 共享 SharedMemoryHandle 包装，因此复制一个句柄不等于又打开了一次对象。只有该包装的最后一个 Reference 释放时，closeHandleObject 才减少对象的打开包装计数。

`releaseObject(object)` 要求对象未链接、打开包装为零、映射数为零，才释放全部物理页和对象槽。unlink 不使现有句柄失效，close 不解除现有映射，unmap 也不自动移除名称。

## 打开、命名与大小

| 成员函数 | 功能与参数 |
| --- | --- |
| `open(task, name, flags)` | 为 task 创建本地句柄；name 为空时匿名，否则要求以 / 开头且非空；flags 控制只读/读写、创建和排他。 |
| `duplicateHandle(source, destination, handle, destinationHandle)` | 复制已有共享 Reference，可为 fork 保持句柄数值，不新建名称或打开包装。 |
| `unlink(name)`、`unlink(task, handle)` | 通过名字或已固定的对象移除链接。 |
| `resize(task, handle, size)` | 要求 manage 权限，size 为逻辑字节数；有映射时拒绝缩小。 |
| `querySize(task, handle, size)` | 在锁内读取逻辑大小。 |
| `closeHandleObject(object)` | 最后打开包装引用释放时退回包装槽，再检查对象能否回收。 |

当前最多 16 个对象、64 个打开包装、64 个映射记录，每对象最多 64 页，名字缓冲 64 字节。耗尽明确失败，不自动扩表。

resize 增长时逐页申请并清零，全部成功后更新逻辑大小。失败只回收新增后缀并保留原大小。末页允许逻辑长度不足整页；可映射范围仍受按页取整后的 backing 大小约束。

## 建立映射

`map(task, handle, address, size, offset, protection)` 固定句柄引用，再按目标地址空间、注册表顺序加锁。地址、大小、对象偏移按页对齐；检查对象范围和读写授权，禁止 write+execute。

逐页安装 pageExternal PTE，不把每个叶子当作独立拥有的普通物理页。全部成功后才发布 SharedMemoryMapping 并增加 mappings；失败撤销本次已安装叶子，物理页仍归共享对象。

记录保存目标、对象、虚拟范围、原始 write 授权和 pagerManaged。writable 反映源句柄的权限上限，不只是当前 PTE；普通 protect 不能为只读来源增加写权限。

## 部分解除与继承

`prepareUnmap(task, address, size, reservation)` 在严格中段删除时预留第二个映射槽；`cancelUnmap(reservation)` 取消未提交的准备。`releaseMapping` 在 PTE 删除后完成整段删除、边界裁剪或中段拆分，中段拆分增加一条存活 view 计数。

`protectionAllowed` 检查所有重叠记录的 write 上限。`releaseTaskMappings(task)` 在目标停稳后释放该 Task 的 view，最终物理释放仍受名称和其他进程引用约束。

`cloneTaskMappings(source, destination)` 先确认全部所需槽位，再复制记录并增加对象 mappings。父进程 close/unmap 后，子进程保留的 view 仍拥有 backing 生命期。

`nextManagedMapping(task, cursor, address, size)` 只枚举可信 MM 建立的 view，供冻结恢复使用。这套内核对象机制不能被描述为所有 POSIX 文件共享映射与写回语义都已实现。
