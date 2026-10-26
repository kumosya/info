# 服务入口与公共框架

`src/service/start.S` 提供用户态服务的启动入口。进入服务程序前，入口建立调用约定要求的栈状态，把加载器提供的参数交给运行时，再调用服务的 `main()`。

服务共用的声明放在 `include/service/common/`，不放回应用公开头文件：`identity.h` 定义固定服务身份，`requests.h` 和 `kernel-requests.h` 定义请求编号，`records.h` 定义固定消息记录。

`Lifecycle::supervise()` 按所有权和依赖启动受管服务，检查就绪端点并处理退出；`RequestChannel` 在服务实现中提供受保护传输接口的调用入口。具体服务自身的声明放在 `include/service/<服务目录>/`。
