# 服务配置与 systemctl

生产默认文件为 `script/etc/services`、`sessions`、`console`，安装到 `/etc/`。`./configure` 生成构建元数据，`make install` 部署系统文件。已有运行时配置保留用户内容，首次安装才复制默认值；修改默认模板不等于改动现有镜像的配置。升级时应先比较新模板再按本文格式迁移，不能直接覆盖用户文件。

## `/etc/services`

每行十列，使用 `|` 分隔，无空白裁剪、引号转义或 shell 展开；整行 `#` 注释，允许空行及 LF/CRLF。文件最大 8191 字节，名称最长 23 字节，只允许小写字母、数字、点、下划线、连字符。拒绝 NUL、非法控制字节、非规范绝对路径、重复名字、未知 owner、无效依赖、重复 order 与依赖逆序。完整解析和验证后才启动文件服务。

```text
name|owner|enabled|executable|arguments|requires|order|ready|timeout-ms|restart
guiw|init|1|/sbin/sys/guiw.elf|-|console|40|guiw|30000|1
```

| 字段 | 当前语义 |
| --- | --- |
| owner | 文件服务当前仅支持 init；boot 子服务归属固定为 task/device |
| enabled | 0 或 1，决定下次启动是否启动 |
| executable | 绝对路径，拒绝空段、点段、反斜线与尾斜线 |
| arguments | `-` 表示没有额外参数；否则逗号分隔，最多八项，总长 127 字节；argv[0] 为服务名 |
| requires | `-` 或逗号分隔的已知服务；文件依赖必须具有更早 order |
| order | 非负整数，文件服务间唯一 |
| ready | `-` 或注册名；等待实际子 PID 发布，不能接收其他进程代替就绪 |
| timeout-ms | 1–30000 |
| restart | 0 或 1，控制异常退出后的自动重启 |

所有列均必需，解析器不会 eval 参数，也不支持参数内的逗号。启动计划只在系统启动读取，没有热重载。固定身份仍由受控加载链授权，配置里没有任意 capabilities/role 字段。

## 会话与其他配置

`/etc/sessions` 恰好包含 terminal、shell、recovery 三条 `kind|absolute-path|argv0`；缺失、重复和非法输入使会话启动失败。默认 GUI 终端为 `/usr/bin/terminal`，初次 shell 为 `/bin/sh`，故障恢复为 `/bin/login`。`/etc/console` 保存默认 TTY 与串口选项；旧 console.conf 需迁移到此路径。版本是 `/lib/version.1`，动态链接器是系统程序，不是 `/etc` 配置或 inpk。

## systemctl 独立项目

源码在 osinfo-base-tools 的 `systemctl/`，项目和调试配置、源码、build 目录均独立。顶层 Makefile 依次构建 coreutils、sysinfo、mount、mkfs、systemctl，各自生成 inpk 后汇总 ZIP。

支持 `list`、`status [name]`、`start/stop/restart/enable/disable name`。list/status 向所有用户开放；变更要求实际发送者有效 UID 为 0。status 显示 owner、状态、PID、enabled、desired、内置标记、重启次数和最近退出值；非运行状态返回 3，命令错误返回 2，操作失败返回 1。

控制消息 type 为 `0x535643`；五个 data 字：operation、索引、24 字节名称。回复首字为 ErrorCode，随后 ServiceStatus。控制端点为 `control.init/task/device`，按固定角色保护。init 路由请求到实际 owner，客户端不直接 kill PID。

start/stop 改当前 desired；enable/disable 只改下次启动设置，互不隐含。持久化先比较当前 `/etc/services` 与已读取版本，发现外部编辑则拒绝写入；写临时文件、fsync 后 rename。不同服务的 enabledOffset 随整条记录排序移动，修改仍落在原文件的对应字节。

停止被活动服务依赖的目标返回 Busy 和依赖名；基础 boot 服务当前拒绝停止、重启和禁用，避免孤儿子服务及不可恢复业务状态。文件服务先 SIGTERM，再有界强制终止；普通 kill/killall/pkill、句柄 terminate 和进程组信号均不能替代该流程。保护由内核 managedService 标记执行，与名称、UID 或固定硬件能力分开。

配置错误、端点失效、启动超时、权限失败应保留原错误；不要以普通 install 替代失败 upgrade，也不要自动重试有副作用请求。实际验证入口为 `tests/service-runtime.sh`，对文件升级保留策略另见 `tests/check-install.sh`。

无效文件计划整体拒绝，保留引导服务及 control.init；合法计划中的单个启动失败表现为 Failed，并由 owner 在有界退避后重试。会话缺席不会使 init 退出。`tests/service-configuration.sh` 覆盖六类配置故障，检查文件服务均未提前启动。
