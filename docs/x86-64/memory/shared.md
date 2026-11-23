# 共享内存：对象、名称、句柄与映射

本文覆盖 `src/kernel/memory/shared/shared.cc` 和 `include/kernel/mm.h` 中的共享内存函数，命名空间为 `mm`。按创建、调整大小、映射、部分解除和回收的顺序阅读，即可把函数表对应到真实使用过程。

## 1. 两个进程怎样共用数据

进程 A 的 0x400000 和进程 B 的 0x800000 可以映射到同一物理页。A 写入后 B 读取的是同一份内容，不需要复制整页；并发读写仍需额外同步协议。

| 名称 | 通俗解释 |
| --- | --- |
| 共享对象 | 保存逻辑大小和物理页数组的存储 |
| 名称 | 查找对象的名字，例如 /demo；匿名对象没有名字 |
| 句柄 | 当前任务的访问凭证，包含只读或读写等权利 |
| 打开包装 | 一次 open 产生的内部对象，可由多个复制句柄共用 |
| view | 对象在某个任务某段虚拟地址上的映射记录 |
| offset | 从对象第几个字节开始映射，要求页对齐 |
| external PTE | 借用对象物理页的页表项，通用 unmap 不能直接释放 backing |

## 2. 共享内存：名字、句柄、映射各有寿命

共享对象可以被多个任务映射。同一物理页可以出现在不同虚拟地址，写入共享内容后，其他映射看到的是同一份数据。

对象必须同时满足以下条件才回收：没有名称链接、没有打开包装、没有存活映射。`unlink` 仅移除名字，close 仅结束句柄使用关系，unmap 仅解除地址映射，三者不能互相替代。

### 2.1 完整函数表

位置：`shared/shared.cc`、`mm.h`。以下无前缀的成员函数均属于 SharedMemory。

| 函数 | 输入、作用和结果 |
| --- | --- |
| `namesEqual(left, right)` | 内部比较共享名称是否相同 |
| `copyName(destination, source)` | 内部按固定名称容量复制并保证终止 |
| `sharedMemory()` | 返回全局共享对象登记表 |
| `releaseObject(object)` | 检查三种使用关系均已结束，才释放 backing 并清空对象槽；类型化分配只退池占用位 |
| `resolve(task, handle, rights)` | 按句柄类型和权利取得内部包装；失败 nullptr，不新建句柄 |
| `open(task, name, flags)` | 查找/创建对象并安装任务句柄；name=nullptr 表示匿名；返回句柄或 0 |
| `duplicateHandle(source, destination, handle, destinationHandle)` | 复制共享句柄引用，可指定目标句柄编号，返回句柄或 0 |
| `unlink(name)` | 按名称移除链接，现有句柄和映射继续有效，成功 true |
| `unlink(task, handle)` | 从固定的共享句柄找到对象并移除名称，成功 true |
| `resize(task, handle, size)` | 要求 manage 权利，调整普通对象逻辑字节数；有映射时拒绝缩小，失败 false |
| `querySize(task, handle, size)` | 查询逻辑大小，通过输出引用返回，成功 true |
| `map(task, handle, address, size, offset, protection)` | 将对象页映射到任务，校验句柄和权限；成功返回 address，失败 0 |
| `prepareUnmap(task, address, size, reservation)` | 中段删除需要新记录时先预留槽，成功 true |
| `cancelUnmap(reservation)` | 取消尚未提交的拆分预留，不删除现有映射 |
| `releaseMapping(task, address, size, reservation)` | PTE 移除后删除、裁剪或拆分共享映射记录，并维护 mappings 计数 |
| `protectionAllowed(task, address, size, protection)` | 按重叠共享记录的原句柄写权限上限决定能否改权限 |
| `releaseTaskMappings(task)` | 删除该任务所有共享 view，减少计数并检查对象能否回收 |
| `cloneTaskMappings(source, destination)` | 预检槽数后复制映射记录并增加对象 mappings，不重新分配共享页 |
| `closeHandleObject(object)` | 最后一个句柄引用结束时清理打开包装、减少 references，再尝试回收对象 |
| `nextManagedMapping(task, cursor, address, size)` | 枚举可信 MM 建立的下一段共享 view，找到 true |
| `openTyped(task, flags, policy)` | 打开类型化池描述句柄，按需建立专用池；返回正句柄或负错误码 |
| `typedInfo(task, handle, available)` | 按池策略查询可用字节，输出 available；成功 0，失败负错误码 |
| `allocateTyped(task, handle, size)` | 按源池策略分配，返回新的匿名结果句柄或负错误码，不把池句柄改成分配结果 |
| `SharedMemoryObject::isUsed()` | 查询对象槽占用位 |
| `SharedMemoryHandle::isUsed()` | 查询打开包装槽占用位 |
| `SharedMemoryMapping::isUsed()` | 查询映射记录槽占用位 |

当前上限：16 个对象、64 个打开包装、64 个映射记录、每普通对象最多 64 页、名称缓冲 64 字节。扩容不会自动突破这些边界。

### 2.2 open → resize → map 流程

`open`：检查只读/读写和名称格式，命名对象要求以 `/` 开头且不为空；匿名对象要求创建标志。随后持登记表锁查找名字，处理创建加排他冲突，必要时建立零大小对象。再分配打开包装，增加 references，按打开模式安装句柄。句柄安装失败则撤销本次包装和引用。

复制句柄共享已有的句柄 Reference，不等于重新 open 一次，因此 references 统计的是打开包装，不是每个任务表中所有句柄数字的总和。

`resize`：固定句柄并检查 manage 权限；拒绝类型化对象和超容量申请；有映射时拒绝缩小。增长时逐页申请并清零，全部成功才提交逻辑大小；失败只释放本次新增页，保留原内容和大小。允许逻辑大小不是整页，但 backing 按整页计算。

`map`：固定句柄 → 检查读写权利、对齐、用户范围和对象偏移 → 锁目标空间和共享表 → 先找空映射记录 → 试算锁页额度 → 按对象页数组逐页建立 external PTE → 成功后发布 view 并增加 mappings。中途失败只撤销本次 PTE，物理页继续归对象。

记录中的 writable 保存原句柄的写授权上限，而不只是当前 PTE 状态。因此只读句柄建立的映射不能通过 protect 变成可写。

### 2.3 部分 unmap 和释放流程

删除整段就退记录；删前缀或后缀就缩短；删中段用预留槽拆成两条 view，并增加一份映射计数。任务退出时 `releaseTaskMappings` 逐条结束 view，最后通过 releaseObject 判断整个对象是否还能被名称或其他任务使用。

普通共享页最终交回物理分配器；类型化分配页只退回专用池，不能逐页释放到通用 buddy。

### 2.4 类型化内存池流程

当前支持 `/memory/ram` 专用池。`openTyped` 第一次成功时申请 8 阶，即 256 页、1 MiB 连续物理块，后续用 `typedUsed_` 位图划分；整个池在当前实现中保留复用。

- policy=1：允许挑选分散空页，typedInfo 返回空闲页总字节数。
- policy=2：要求连续空页，typedInfo 返回最长连续空闲段字节数。

`allocateTyped` 检查大小非零、页对齐且不超过 64 页；先创建匿名结果对象，再在池中找齐全部页，找到后一次标占用、清零并登记页数组。失败关闭未发布的结果句柄。源句柄只读时结果也降为只读，避免通过分配操作提升权利。

## 3. 主要执行流程举例

### 3.1 创建到最终释放

1. A open 创建 /demo，初始逻辑大小为 0。
2. resize 到 8192 字节，申请并清零两页。
3. A 映射两页，B 按名称打开后映射同样两页；虚拟地址可以不同。
4. unlink 删除名字，但已有句柄和映射仍有效。
5. 双方关闭句柄，映射仍可继续访问。
6. 最后所有映射都解除，名称、打开包装、view 都消失，releaseObject 才归还物理页。

保留名称时，没有句柄或映射的对象仍不会回收，因为以后还能按名打开。

### 3.2 resize 失败怎样保留旧内容

原对象一页，准备扩为三页。第二页成功、第三页失败时，只归还本次新分配的第二页，原页和原逻辑大小不变。有映射时不允许逻辑缩小，即使缩小前后都落在同一末页。

### 3.3 解除中段怎样分两步完成

prepareUnmap 在删除严格中段时先占住一个新记录槽；其他上层准备失败则 cancelUnmap 归还预留，不改旧映射。准备完成后上层删 PTE 并失效 TLB，再 releaseMapping 更新记录：整段删除减一份 mappings，边缘删除只缩短，中段拆分增加一份 mappings，未用的预留清空。

releaseMapping 返回是否实际修改过记录，不表示释放了物理页。共享记录没有 Map 那样的 offset 字段，裁剪 view 不重新建立 PTE；实际物理对应关系已在页表中。

### 3.4 cloneTaskMappings 与 closeHandleObject

cloneTaskMappings 先统计源记录和空槽，足够才逐条复制、改目标、增加 mappings；它不复制页面内容，也不建立子页表，必须与地址空间克隆配合。

closeHandleObject 在最后一份句柄 Reference 结束时调用：核对包装 → 持锁清包装占用和指针 → 减少对象 references → releaseObject 检查最终回收。duplicateHandle 不等于重新 open，因此不会按复制出的每个句柄数字直接增加打开包装计数。

### 3.5 类型化池的连续和分散策略

如果只有池页编号 0、2、4 空闲，policy=1 可用空间是 3 页；policy=2 最长连续空间只有 1 页，无法申请连续两页。typedInfo 只是查询，allocateTyped 仍需在锁内重新找齐；找齐才标占用并清零，失败不占半份资源。结果释放时页退回专用池，整个池保留复用。

## 4. 返回值和大小边界

open/map/duplicateHandle 以 0 表示失败；普通管理函数多为 bool。typedInfo 成功为 0，openTyped/allocateTyped 成功为正句柄，类型化接口失败为负错误码。querySize 和 typedInfo 经输出引用返回数据。

对象、打开包装、view 和页面都有固定容量。逻辑末页可以不足整页，map 可按 backing 的整页大小映射末页，但 offset 仍必须落在实际对象内容范围内。共享内存不自动提供文件写回策略。

## 5. 继续阅读

页表与拆分事务见 [虚拟内存](virtual.md)，请求协议见 [内存机制服务](service.md)，整体概念见 [内存总览](memory.md)。
