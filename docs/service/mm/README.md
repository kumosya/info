# mm：Ring3 内存策略与缺页处理

实现为 `src/mm/`，入口 `main.cc`，角色 Memory、能力 Memory，注册名 `mm`。init 在 fs 启动后创建内置 mm；先通过 memory.kernel 绑定 pager 端点，再发布 mm。代码不位于 user/，页表机制仍属于 Ring0。

## 职责与请求

mm 维护 Region/VMA、匿名/文件/共享 backing、延迟分配、COW、脏页与回收策略。协议位于 `os/mm.h::MmOperation`，客户端库通过 `os::Memory` 和 libc mmap 适配调用。

| 操作组 | 数据与约束 |
| --- | --- |
| MapAnonymous / MapShared / MapFile | 页对齐大小与 hint、权限和标志；单映射不超过 32 MiB；拒绝 W+X |
| Fault | 真实发送者 PID、地址、缺页原因；检验区域与访问权限 |
| Fork / Exec | 同步父子区域、COW 和地址空间代次 |
| Unmap / Protect | 先检查并暂存 RegionTable，再调用既有目标映射 syscall；失败尝试回滚 |
| 共享对象 | 能力标识和命名对象状态，不信任普通整数当句柄 |

MapFile 必须由 FS 验证并兑换文件映射委托；不能凭调用者给出的文件描述符数字读取其他进程文件。兑换后形成稳定映射租约，其寿命独立于 MM PID。原 FD 关闭、文件重命名后仍引用原文件对象；ext3/ext4 最后一个名字 unlink 后也由打开对象保留 inode 和数据块，直到最后租约释放才回收。无引用租约在 MM 检查点提交后释放；失败请求按委托 token 取消，不按可能复用的描述符数字取消。

## 机制边界

实际 map/unmap/protect 复用 syscall 8–10；目标地址空间操作只允许内核认可的 Memory 身份。memory.kernel 提供页对象、共享对象、pager 绑定、页状态等机制，没有第二套 Map/Unmap/Protect RPC。详见 [memory.kernel](memory.kernel/README.md)。

主线程串行处理有界 IPC，每 50 ms 扫描死亡任务和可回收状态；RegionTable、backing/page 固定容量来自 `mm.h`。请求/回复中的句柄有明确关闭路径；服务自己读文件不能向自己发 FS 请求，FS 与 mm 为独立进程。

## 故障边界

MM 已接入受保护恢复域。快照包含完整 RegionTable、地址空间 generation、页/backing 元数据和请求结果；页与共享对象通过能力 token 托管。快照不是普通进程内存的第二份副本，MM 退出后其内容及引用继续存在。

变更先预留提交空间，暂停相关普通任务，再执行现有映射 syscall。完成后提交元数据与请求结果、解除暂停、回复客户端。服务在修改中途退出时，新 MM 对暂停任务的 pager 映射对账并还原最后提交状态；已经提交的请求按原 ID 返回结果，避免重复创建 VMA。页表扫描只用于对账，未触页 VMA 从检查点恢复。

恢复保留匿名数据、PROT_NONE 页、COW、共享别名和文件 backing。新实例过滤已退出/exec 的旧地址空间，再验证区间、权限及引用；完成后绑定 pager、发布 ready。缺页与 fork 桥接在恢复窗口内等待新实例。暂时停用 MM 仍不开放；systemctl 的受控重启按运行时管理文档逐项开放。

暂停普通地址空间有 20 秒上限。无法完成恢复时，内核终止仍处于暂停的受影响任务，避免无限挂起或恢复不确定的页表。恢复控制代码与服务内存使用驻留的机制映射；当前事务暂停接口拒绝暂停受管服务自身，防止恢复依赖环。

当前验证覆盖 MM 单服务故障和 ext3/ext4 已 unlink 文件的映射寿命；FS 同时退出、并发多线程 fork 与页表复制的协调、低内存/损坏记录及所有服务的统一事务恢复仍属于后续组合验收，不能据此声称 FS/设备写入也已完整恢复。

测试 `tests/mm-recovery.sh` 在修改前、PTE 修改后提交前、提交后唤醒前、唤醒后回复前四处强制退出 MM，检查旧 VMA、数据、懒分配、PROT_NONE、共享对象、文件租约、请求去重及跨四次重启的父子 COW。

普通 kill 即使 root 也被内核拒绝。
