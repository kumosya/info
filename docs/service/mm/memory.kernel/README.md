# memory.kernel：页与共享对象机制

实现 `src/kernel/memory/service/service.cc`，Ring0 驻留 EndpointService，init 发布 `memory.kernel`。Ring3 [mm](../README.md)持有区域和回收策略，本对象只暴露完成策略所需的受限机制。

## 协议与边界

| MemoryOperation | 作用 |
| --- | --- |
| BindPager | 绑定经过认证的 MM pager 端点 |
| PageState / RangeAvailable | 查询自身或授权目标地址空间的页标志、区域是否空闲 |
| Open / Unlink / Resize | 有界命名共享对象与能力句柄 |
| AllocatePages / PageInfo | 页对象分配及所有者、大小、引用数、节点信息 |
| Usage | 总内存、可用内存、目标占用和地址空间代次 |
| ReclaimCaches | Memory 能力授权的缓存回收 |
| Freeze / Thaw | 当前 MM 实例受控暂停/恢复普通任务，保留原有 SIGSTOP 状态 |
| FrozenNext / MappingNext | 枚举暂停任务和 pager 管理的既存映射，用于恢复对账 |

**没有 Map/MapPages/MapSharedTarget/UnmapTarget/ProtectTarget RPC。** map/unmap/protect 统一复用 syscall 8–10，不能因 MM 服务需要映射而在此复现一份映射业务。

## 参数、权限与对象寿命

机制验证消息长度、NUL 名称、页对齐、用户地址范围、分配阶数与 NUMA 节点。跨进程查询要求 Memory 角色和能力；普通请求只允许自身。页对象由 HandleTable::Pin 固定，引用释放后回收物理页；共享对象 unlink 和已有打开引用的寿命分开。

页表查询持目标地址空间锁，物理页和拓扑使用各自已有同步机制。对象没有独立服务线程、PID 或 runtime 文件；通过现有 IPC 入口同步执行。页对象权限不能由裸物理地址或普通整数句柄替代。

## 失败与验证

非法参数、无权限、死亡目标、分配失败分别返回原生 ErrorCode。pager 故障期间缺页/fork 有界等待新实例，Ring3 MM 负责恢复 VMA 和业务事务；本驻留对象不解释检查点里的策略结构。暂停超时由独立回收监督线程终止受影响普通任务，服务保护不阻断内核异常清理。systemctl 不可停止该驻留机制对象。

参考 `tests/mm-runtime.cc`、`tests/kernel-lifecycle.h` 和本轮执行记录；诊断同时检查 `/sys/memory`、地址空间 generation、页引用和 MM 日志，不只看全局 free 值。
