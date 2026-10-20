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

MapFile 必须由 FS 验证并兑换文件映射委托；不能凭调用者给出的文件描述符数字读取其他进程文件。Backing 持有 FS 连接、描述符和页引用，引用归零时回收。

## 机制边界

实际 map/unmap/protect 复用 syscall 8–10；目标地址空间操作只允许内核认可的 Memory 身份。memory.kernel 提供页对象、共享对象、pager 绑定、页状态等机制，没有第二套 Map/Unmap/Protect RPC。详见 [memory.kernel](memory.kernel/README.md)。

主线程串行处理有界 IPC，每 50 ms 扫描死亡任务和可回收状态；RegionTable、backing/page 固定容量来自 `mm.h`。请求/回复中的句柄有明确关闭路径；服务自己读文件不能向自己发 FS 请求，FS 与 mm 为独立进程。

## 故障边界

MM 退出会丢失策略状态；重新注册暂时不恢复旧 VMA、COW 与文件委托。基础引导 mm 暂不支持 systemctl 停止/重启。

普通 kill 即使 root 也被内核拒绝。
