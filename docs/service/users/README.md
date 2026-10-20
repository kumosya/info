# users：账户、认证与凭据

实现 `src/users/main.cc`，Ring3、Users 角色、Users 能力，注册名 `users`。init 按 `/etc/services` 从 `/sbin/sys/users.elf` 加载，依赖 fs、mm；账户文件读取成功、端点注册完成才就绪。

运行时输入为 `/etc/passwd`、`/etc/shadow`、`/etc/sudoers`，均无 conf 后缀。shadow 的临时文件为 `/etc/.shadow.tmp`，写入后替换目标并保持 0600 权限。账户解析、口令验证和 sudo 策略在本服务；真正的任务凭据由 task.kernel 保存并验证修改权限。

## 协议和认证

公共协议位于 `os/users.h`，请求为 UserOperation 与固定长度 UserRequest；账号、口令和替换字段必须 NUL 终结。服务按 request.senderPid 查询实际调用者，不接受负载伪造 UID。操作覆盖身份查询、认证、账户和口令管理、sudo 授权、Who 查询。

客户端使用 `os::Users` 与 libc 账户适配。Who 现在按 PID 查询，而非旧 task 私有表索引；全系统枚举由 `/proc` 完成。失败返回值在客户端统一映射，未知用户、认证失败和权限拒绝不能当作成功。

Users 能力用于受控修改任务凭据；同名普通程序、root 自行加载 ELF 都不会获得能力。配置和口令不出现在日志里。普通程序不能通过请求改变不属于自己的任务权限；服务重启也不能跳过恢复会话认证。

## 生命周期、资源和排查

请求主循环串行使用账户缓存和有界缓冲，写 shadow 采用临时文件替换；所有 FILE、端点和接收能力按所有权释放。停止需先停止依赖的 tty/console/guiw，systemctl 默认拒绝仍有活动依赖的 stop。

退出后账户内存缓存丢失，下次实例从文件重读；登录中的内核凭据不会因为重启自动回退或提权。普通 kill/terminate 受服务标记保护，配置管理走 systemctl。排查顺序为文件权限、账号格式、服务角色、`[USERS]` 启动日志、`systemctl status users`。

会话入口见 [init](../init/README.md)，任务凭据机制见 [task.kernel](../task/task.kernel/README.md)。
