# OS 库的服务实现

此目录位于 `lib/os/service/`，实现服务运行时和 `os/service.h` 中的服务对象，不放普通应用库的零散实现。

- `runtime.cc`：`Runtime::prepare()` 解析运行时入口，`runtimeInit()` 初始化运行环境，`runtimeFini()` 倒序执行结束处理。
- `connection.cc`：`Sys::Connection::connect()` 查找服务并保存端点，`call()` 发送请求，`disconnect()` 释放本对象持有的引用。已经断开的请求不会自动重发。
- `recovery.cc`：`Sys::Recovery` 向任务服务提交检查点和日志请求；`acquire()` 取得恢复记录，`prepare()` 检查容量，`commit()` 和 `append()` 发布数据。
- `checkpoint.cc`：`Sys::Checkpoint::open()` 恢复状态，`prepare()` 准备完整快照，`commit()` 发布快照；日志通过 `prepareLog()`、`append()`、`readLog()` 处理。`close()` 先解除暂存映射，再释放页对象。
- `control.cc`：`Sys::Manager` 查询服务状态，提交启动、停止、重启、启用和禁用请求；`Sys::Status` 只保存已查询的属性。
- `boot.cc`：向 console 发送启动状态和启动完成消息。
- `session.cc`：实现 console 与图形会话控制器之间的服务端通信约定，声明属于 `include/service/console/`。

应用入口源码为 `lib/os/syscall.S`，服务入口源码为 `src/service/start.S`；构建时分别生成 `start.o` 和 `service_start.o`，调用运行时后再进入程序的 `main()`。
