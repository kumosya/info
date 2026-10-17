# os 服务客户端（service.cc / task.cc / file.cc / gui.cc / input.cc）

本文档描述 os 运行时库中访问各用户态服务的 IPC 客户端。

## 1. 服务发现（service.cc）

```cpp
void setServiceRegistry(uint64_t endpoint);
uint64_t serviceRegistry();
bool registerService(uint64_t registry, const char* name, uint64_t endpoint);
uint64_t lookupService(uint64_t registry, const char* name, uint32_t attempts = 100);
```

- `serviceRegistry()` 返回手动配置值，未配置时退回 `getBootstrapHandle()`。
- `registerService`：发送 `RegistryRequest::Register`，名字拷入 `message.data`，endpoint 作为 `handles[0]` 传递（句柄传递式 IPC）。
- `lookupService`：发送 `RegistryRequest::Lookup`，最多重试 `attempts` 次，每次失败 `sleep(1)`；成功返回回复携带的句柄；失败返回 0。

## 2. 进程客户端（task.cc，task 服务）

常量：`maximumArgumentCount = 64`，`executePayloadSize = 56` 字节。

```cpp
TaskHandle execute(const char* path);
TaskHandle execute(const char* path, int argumentCount, const char* const* arguments);
TaskHandle executeWithIo(const char* path, int argumentCount, const char* const* arguments,
                            uint64_t standardInputHandle, uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
int waitTask(TaskHandle task, int* exitCode);
bool listTask(uint32_t index, TaskListEntry& entry);
bool killTask(uint64_t taskId);
```

- `execute` 默认继承三个标准句柄（`getStandardHandle(0/1/2)`）。
- `executeWithIo`：查找 registry 与 task 服务（各重试 1000 次）；构造 `TaskRequest::Execute`：`data[0] = argumentCount | ((terminalId+1) << 32)`；`data[1..]` 为 56 字节 payload（path + 各参数，NUL 结尾）；`handles[0] = registry`；stdin/stdout/stderr 非 0 时追加到 handles 并置 `taskExecuteHasStdin/Stdout/Stderr` 标志。成功时回复携带子进程句柄。
- `waitTask`：循环发送 `TaskRequest::Wait`；回复 `data[0]==1` 表示退出（取 `data[1]` 为退出码，关闭句柄）；`==2` 返回 `ErrorCode::Interrupted`；其它 `sleep(1)` 重试。
- `listTask`：`TaskRequest::List`，回复 id、state、48 字节名字。
- `killTask`：即 `sendSignal(taskId, 15)`（SIGTERM）。

## 3. 文件客户端（os::File，fs 服务）

`include/os-c++/os/file.h` 提供可移动、不可复制的 RAII File 类。服务端点属于对象状态，方法不再重复接受 fs 参数。

```cpp
os::File file;
if (file.open("/tmp/example", O_RDWR | O_CREAT, 0640) == 0) {
    file.write("hello", 5);
    file.lseek(0, SEEK_SET);
    const auto size = file.getSize();
    const auto mode = file.getMode();
}
```

open 接受 POSIX flags/mode，read/write/pread/pwrite 使用字节数和 off_t 偏移，lseek 接受 SEEK_SET/CUR/END。失败返回负 errno。pread/pwrite 使用定位读写 RPC，保持共享文件偏移不变。

元数据保存在私有成员中，通过 getType/getSize/getMode/getInode/getLinks/getUid/getGid/getAccessTime/getModificationTime/getChangeTime/getDevice/getSpecialDevice 查询；refresh 显式更新缓存。没有公开的 FileStat 结构。

目录、挂载、权限、所有权和异步请求也由 File 方法承载。borrow 创建不拥有句柄的临时视图；adopt 接管所有权；releaseNativeHandle 释放对象的所有权。File 为 pipe、终端、共享内存描述符提供后端视图。toDescriptor 移交所有权，fromDescriptor 访问 fd 引用的对象。

## 4. 窗口客户端（gui.cc，guiw 服务）

内部事件订阅缓存：`EventSubscription {service, window, endpoint}`，容量 16，`std::mutex` 保护。`eventEndpoint()` 先查缓存，未命中则 `createEndpoint()` 并发送 `SubscribeEvents` 注册；窗口销毁时关闭端点。

窗口操作（均为 `callService` + 检查 `reply.data[0]==0`）：

```cpp
WindowHandle createWindow(uint64_t service, uint32_t width, uint32_t height, const char* title);
bool destroyWindow(uint64_t service, WindowHandle window);
bool showWindow / hideWindow / resizeWindow / moveWindow(...);
bool drawPixel / drawLine / fillRectangle / drawText(...);
bool presentWindow(uint64_t service, WindowHandle window);
bool getNextWindowEvent(uint64_t service, WindowHandle window, WindowEvent& event);
bool setWindowSessionActive(uint64_t service, bool active);
bool setWindowLayer(uint64_t service, WindowHandle window, WindowLayer layer);
bool raiseWindow(uint64_t service, WindowHandle window);
```

- 标题最多 48 字节；文本绘制最多 48 字节。
- `getNextWindowEvent`：循环发送 `GetEvent`；回复 `WouldBlock` 时在订阅端点上 `receiveMessage` 等待 `windowEventNotification`（"WINEVENT"）后重试；成功按字节拷入 `WindowEvent`。

## 5. 输入客户端（input.cc）

```cpp
class InputClient {
    bool connect(uint64_t service);   // 创建唤醒端点，发送 Subscribe（端点句柄传递）
    bool wait();                      // 在唤醒端点接收，直到收到 inputEventNotification（"INPUT"）
    bool receive(InputEvent& event);  // 发送 GetEvent，校验长度后拷入
    ~InputClient();                   // 关闭两个句柄
};
```
