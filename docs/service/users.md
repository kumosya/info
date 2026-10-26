# users：账户、认证与凭据

实现 `src/users/main.cc`，Ring3、Users 角色、Users 能力，注册名 `users`。init 按 `/etc/services` 从 `/sbin/sys/users.elf` 加载，依赖 fs、mm；账户文件读取成功、端点注册完成才就绪。

运行时输入为 `/etc/passwd`、`/etc/shadow`、`/etc/sudoers`，均无 conf 后缀。shadow 的临时文件为 `/etc/.shadow.tmp`，写入后替换目标并保持 0600 权限。账户解析、口令验证和 sudo 策略在本服务；真正的任务凭据由 task.kernel 保存并验证修改权限。

## 协议和认证

应用接口位于 `os/user.h`，`UserClient`以lookup、login、graphicalLogin、authorizeSudo、changePassword、querySession、beginSession、autoLogin、list分别表示具体操作。`UserProfile`仅提供逐字段getter。服务消息布局位于 `include/service/users/messages.h`，不要求应用了解操作编号或打包结构。账号、口令和替换字段必须 NUL 终结。服务按 request.senderPid 查询实际调用者，不接受负载伪造 UID。

客户端使用 `os::UserClient` 与 libc 账户适配。querySession按PID查询；全系统枚举由 `/proc` 完成。客户端区分服务拒绝与传输或格式错误，未知用户、认证失败和权限拒绝不能当作成功。增加账户的消息编号保留兼容，但服务当前未实现该操作，公共客户端不提供对应函数。

Users 能力用于受控修改任务凭据；同名普通程序、root 自行加载 ELF 都不会获得能力。配置和口令不出现在日志里。普通程序不能通过请求改变不属于自己的任务权限；服务重启也不能跳过恢复会话认证。

## 生命周期、资源和排查

请求主循环串行使用账户缓存和有界缓冲，写 shadow 采用临时文件替换；所有 FILE、端点和接收能力按所有权释放。停止需先停止依赖的 tty/console/guiw，systemctl 默认拒绝仍有活动依赖的 stop。

退出后账户缓存从文件严格重读，活跃会话从内核认证标记与 `/proc` 恢复。服务不在启动时清空 `/run`，保留仍有认证任务的 runtime 目录与文件；扫描未完成时不执行清理。登录中的凭据不会因重启回退或提权，普通进程不能建立内核认证标记。现在允许受控 `systemctl restart users` 保留会话，永久停止仍受依赖检查；普通 kill/terminate 保护保持。排查顺序为文件权限、账号格式、服务角色、`[USERS]` 日志和 `systemctl status users`。登录与开发账户规则见 [登录](../login.md)。

会话入口见 [init](../init/README.md)，任务凭据机制见 [task.kernel](../task/task.kernel/README.md)。
