# TTY 登录与开发镜像账户

文本 TTY 首次启动、logout 和异常恢复统一经过 `/bin/login`；GUI 新终端使用相同入口。启动器建立 SID/PGID 和控制终端，login 检查这些关系，认证后保留同一会话执行账户 shell，不次 setsid 丢弃控制终端。

## 自动登录条件

只有完整 passwd/shadow 数据库恰好一个 root 账户、UID/GID 为 0、对应 shadow 字段明确为空密码时，users 才允许自动登录。多账户、有密码、锁定哈希、缺失 shadow、损坏/重复/超长记录都不能触发。空密码交互认证仅接受空输入，`!` 与 `*` 始终为锁定状态。

自动登录由 users 在请求执行时判断，要求调用者为 root、普通会话首领且具有控制终端。命令行 terminal 参数不授予 root 身份。普通进程即使 root 也不能调用内核认证操作伪造 users 的认证标记。

## 会话和恢复

users 先准备 runtime 目录，再通过受保护机制原子建立凭据与认证标记。失败不切换身份；成功后 login 应用 HOME、USER、LOGNAME、SHELL、PATH、TERM、XDG 目录，进入 home 并执行账户 shell。

密码输入关闭回显，返回时恢复终端属性并清除口令缓冲区。认证失败在 login 内退避重试，不消耗 init 的服务崩溃预算。数据库异常或服务不可用时不能回退到裸 shell。

认证标记由内核保存，派生任务继承。users 新实例从 `/proc` 与认证标记重建活跃用户引用，不通过任务名称猜测身份，也不在启动时清空 `/run`。仍有认证任务的 `/run/user/<uid>` 及文件保留，最后一个任务退出后回收。

## 开发资源与安装

`make export-images` 默认使用 `EXPORT_ACCOUNT_PROFILE=development`，导出 `build/export/osinfo.img` 与 `osinfo.iso`，两者均只有无密码 root，直接登录。模板位于 `script/profiles/development/`，shadow 安装权限为 0600。

需要普通系统默认账户模板时，可执行 `make export-images EXPORT_ACCOUNT_PROFILE=system`。普通 `make install` 不应用开发账户 profile，既有 passwd/shadow 按原安装保留策略处理。

开发账户只写入新建镜像 staging root；不会主动覆盖真实安装、已有 InfoIDE 项目 IMG 或自定义模板。InfoIDE 的打包、安装和卸载继续由 InfoIDE 项目完成，OS 只提供通用导出资源。系统本体 run/debug 为硬盘启动，项目光盘调试仍需选择。

## 验证

`tests/accounts-host.sh` 验证严格账户解析；`tests/login-runtime.sh` 验证自动/密码/多账户、错误口令、UID/GID、SID/PGID、前台组、环境、非 root 管理拒绝及 users 重启保留会话/runtime 文件。
