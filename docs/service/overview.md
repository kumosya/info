# Ring3 服务与 Ring0 机制

所有服务程序运行在 Ring3。固定 boot 角色只决定其可调用的内核机制权限。

| 服务 | 源码 | 注册名 |
|---|---|---|
| init | src/init/ | services |
| task | src/task/ | task |
| fs | src/fs/ | fs |
| pci | src/pci/ | pci |
| device | src/device/ | device、input |
| storage | src/storage/ | storage |
| tty | src/tty/ | tty |
| console | src/console/ | console、terminal |
| guiw | 独立项目 D:/osinfo/guiw/server/ | guiw |
| users | src/users/ | users |
| MM | user/mm/ | mm |
| pipe | user/pipe/ | pipe |
| msgq | user/msgq/ | msgq |

Ring0 驻留的是 task.kernel、pci.kernel、device.kernel、memory.kernel、system.kernel 等机制对象，服务进程通过已有 IPC syscall 请求它们。PCI 配置和 PS/2 端口访问只存在于内核硬件实现；没有端口 syscall 或任意端口代理。

pipe/msgq 的缓冲区、命名对象、优先级、EOF、通知和等待请求在各自 Ring3 服务中。它们使用 Endpoint 能力标签、句柄引用统计及 Reply Token 状态来处理关闭、取消和对象寿命，不新增 syscall。

ServiceManager 容量 64，名字容量 24。保护名必须匹配内核赋予的 ServiceRole；查询只发布 Write 句柄。管理器在处理请求时即时检查死亡发布者，并周期清理失效记录。init 监护器负责重启 boot 服务；客户端清除失效连接后重新发现，不重放有副作用请求。

结构图、启动依赖与故障边界见[架构](../x86-64/architecture.md)。
