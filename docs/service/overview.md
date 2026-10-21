# Ring3 服务与 Ring0 机制

所有服务程序运行在 Ring3。固定 boot 角色只决定其可调用的内核机制权限。

| 服务 | 源码 | 注册名 |
|---|---|---|
| init | src/init/ | services |
| task | src/task/ | task |
| fs | src/fs/ | fs |
| pci | src/device/pci/ | pci |
| device | src/device/ | device、input |
| storage | src/device/storage/ | storage |
| tty | src/tty/ | tty |
| console | src/console/ | console、terminal |
| guiw | 独立项目 D:/osinfo/guiw/server/ | guiw |
| users | src/users/ | users |
| MM | src/mm/ | mm |
| pipe | src/task/pipe/ | pipe |
| msgq | src/task/msgq/ | msgq |

Ring0 驻留的是 task.kernel、pci.kernel、device.kernel、memory.kernel、system.kernel 等机制对象，服务进程通过已有 IPC syscall 请求它们。PCI 配置和 PS/2 端口访问只存在于内核硬件实现；没有端口 syscall 或任意端口代理。

| Ring0 对象 | 独立实现 |
| --- | --- |
| task.kernel | `src/kernel/task/service/service.cc` |
| pci.kernel | `src/kernel/device/pci/service.cc`；硬件访问为同目录 `access.cc` |
| device.kernel | `src/kernel/device/service/service.cc` |
| memory.kernel | `src/kernel/memory/service/service.cc` |
| system.kernel | `src/kernel/task/sys/service.cc` |

`src/kernel/task/core_service.cc` 仅组装端点并提供发现。映射统一使用 syscall 8–10，包括页对象、共享对象以及固定 MM 角色授权的目标地址空间操作；memory.kernel 不提供 Map/MapPages/MapSharedTarget/UnmapTarget/ProtectTarget RPC。服务构建分别选择各自源码，task/device 不递归链接子服务入口。`user/` 是程序目录。

pipe/msgq 的缓冲区、命名对象、优先级、EOF、通知和等待请求在各自 Ring3 服务中。它们使用 Endpoint 能力标签、句柄引用统计及 Reply Token 状态来处理关闭、取消和对象寿命，不新增 syscall。

ServiceManager 容量 64，名字容量 24。保护名必须匹配内核赋予的 ServiceRole；查询只发布 Write 句柄并返回发布者 PID。管理器即时检查死亡发布者并周期清理失效记录。每个服务只有一个 owner；客户端清除失效连接后重新发现。MM/pipe 使用稳定请求 ID 续接已提交结果，其他有副作用请求不能据此盲目重放。

实际引导链为 init → task → pipe/msgq，随后 init → device → pci/storage，再启动 fs、mm。users、tty、console、guiw 由 init 读取 `/etc/services` 后从磁盘 ELF 受控加载，不在 GRUB 模块中。GUI 终端与文本会话读取 `/etc/sessions`。普通信号不能终止受监护服务，管理由独立 systemctl 程序通过 control.init/task/device 执行。

| 主文档 | 派生服务与机制 |
| --- | --- |
| [init](init/README.md) | 注册表、运行时加载、会话监护 |
| [task](task/README.md) | [pipe](task/pipe/README.md)、[msgq](task/msgq/README.md)、[task.kernel](task/task.kernel/README.md)、[system.kernel](task/system.kernel/README.md) |
| [device](device/README.md) | [pci](device/pci/README.md)、[pci.kernel](device/pci/pci.kernel/README.md)、[storage](device/storage/README.md)、[device.kernel](device/device.kernel/README.md)；input 同进程承载 |
| [fs](fs/README.md) | [procfs/sysfs 与 ps](procfs.md) |
| [mm](mm/README.md) | [memory.kernel](mm/memory.kernel/README.md) |
| [users](users/README.md) | 账户与认证 |
| [tty](tty/README.md)、[console](console/README.md)、[guiw](guiw/README.md) | PTY、文本终端和窗口 |

配置格式、状态、持久化和命令见 [运行时管理](runtime.md)，认证见 [登录](login.md)。MM 已有 VMA/页/backing 与事务恢复，users 从内核认证标记恢复会话，pipe 恢复旧字节流、游标与消费结果；三者支持受控 restart。设备资源域已处理 DMA 停止、回收和失败隔离。消息队列、FS 自身描述符、窗口及设备在途业务的完整恢复仍在实施，不能用这些部分进展代替全服务恢复。契约与当前边界见 [恢复](recovery.md)。历史平铺 md 保留导航入口，主文档各自位于服务目录。

结构图、启动依赖与故障边界见[架构](../architecture.md)。
