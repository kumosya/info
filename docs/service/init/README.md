# init：引导、发现和运行时服务管理

实现：`src/init/main.cc`、`service_manager.h`、`sessions.h`；公共监护实现为 `src/service/lifecycle.h`。init 是内核创建的 Ring3 进程，角色 Init，持有 Spawn 能力；不是 Ring0 服务。注册名为 `services`、`control.init`。

## 引导顺序与所有权

1. 建立注册表线程，发布 task.kernel、system.kernel、memory.kernel、device.kernel、pci.kernel。
2. 创建 task，等待它完成 pipe、msgq 的创建与就绪。
3. 创建 device，等待它完成 pci、storage 和驱动发现。
4. 创建 fs、mm；文件请求经 fs 等待根卷初始化完成。
5. 读取并校验 `/etc/services`，按 order 启动文件服务；默认 users → tty → console → guiw。
6. 读取 `/etc/sessions`，建立 GUI 终端与 tty1–tty6 会话。

只有 init 监护 task、device、fs、mm 和它读取的文件服务；pipe/msgq 与 pci/storage 的创建句柄分别归 task、device。基础引导清单来自生成的 startup.inc，其中没有文件服务；会话入口没有编译进 init。

## 注册表协议

协议声明为 `os/protocol.h::RegistryRequest`。请求 data 前 24 字节为 NUL 结尾名称，允许小写字母、数字、点、连字符、下划线；容量 64。

| 请求 | 输入 | 输出 |
| --- | --- | --- |
| Register=1 | endpoint 句柄，data[3]=访问标志 | data[0]=状态，data[1]=generation |
| Lookup=2 | 名称 | 状态、generation、发布者 PID、一个仅 Write 的端点句柄 |
| Unregister=3 | 名称、data[4]=generation | 状态、新 generation |

保护名必须匹配内核赋予的 ServiceRole。活动记录不能被其他 PID 覆盖；普通名称还绑定发布者有效 UID。OwnerOnly 限制不同有效 UID 查询。注销必须匹配发布者及代次，防止旧实例注销新实例。每 50 ms 清理死亡发布者和失效端点；失效会增加 generation。

就绪等待同时验证名称和发布者 PID。进程退出、超时、错误实例抢占名称均不能当作启动成功。注册成功只代表服务实现已到达它自己的发布点。

## 管理与线程

注册表线程与生命周期线程各使用独立 64 KiB 栈。生命周期线程串行处理状态变更，每轮检查退出任务，再接收最多等待 250 ms 的控制请求；主线程每秒监护会话。

控制协议及配置格式见 [运行时管理](../runtime.md)。非查询操作验证真实 IPC 发送者有效 UID 为 0。子管理请求路由到 `control.task`、`control.device`，客户端不持有服务 PID 的杀进程权限。

停止先清 desired，再发受控 SIGTERM，最多等待两秒，然后使用受控终止。活动依赖阻止单独停止，错误返回依赖名。基础引导服务拒绝永久 stop/disable；MM、pipe 已有事务恢复，users 可从内核认证身份重建会话，因此允许受控 restart，并在实际新实例发布 ready 后返回成功。其他服务仍保持现有依赖限制，不能以重启进程代替业务恢复。

退出或启动失败采用 2–32 秒退避；连续失败五次停止自动尝试，运行稳定一分钟后清连续失败计数。手工 start 可重置失败预算。自动重建进程不意味着 FS 描述符、MM 区域、DMA、管道或消息队列内容恢复。

## 会话恢复与限制

文本会话首次启动及 shell 结束后，清空该 TTY 输入、行编辑、历史、前台组和回显状态，统一启动 `/etc/sessions` 的 login 程序。兼容旧格式时统一使用 recovery，旧 shell 项不提供首次绕过认证。login 与认证后的 shell 是普通程序，允许接收信号；只有唯一无密码 root 模式自动登录。详见 [登录](../login.md)。

文本会话正常 logout 清除连续失败计数；异常退出采用 2、4、8 秒退避，之后维持 8 秒，持续尝试恢复，不因五次退出永久丢失 TTY。GUI 终端的退出与失败预算独立保留。`tests/service-runtime.sh` 覆盖六个 TTY 经 killall sh 后连续六轮重建，排除已退出的 /proc 记录再判断新 shell 是否就绪。

GUI 终端正常退出不自动重开；异常退出可恢复。guiw 新实例出现后允许重新创建终端。PTY 内 shell 的恢复由终端应用负责，见 [tty](../tty/README.md)。当前没有依赖服务业务状态的透明重建，也没有配置热重载。默认文件配置 owner 仅支持 init。

