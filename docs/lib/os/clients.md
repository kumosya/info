# os 服务客户端（service.cc / process.cc / file.cc / gui.cc / input.cc）

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

## 2. 进程客户端（process.cc，task 服务）

常量：`maximumArgumentCount = 8`，`executePayloadSize = 56` 字节。

```cpp
ProcessHandle execute(const char* path);
ProcessHandle execute(const char* path, int argumentCount, const char* const* arguments);
ProcessHandle executeWithIo(const char* path, int argumentCount, const char* const* arguments,
                            uint64_t standardInputHandle, uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
int waitProcess(ProcessHandle process, int* exitCode);
bool listProcess(uint32_t index, ProcessListEntry& entry);
bool killProcess(uint64_t processId);
```

- `execute` 默认继承三个标准句柄（`getStandardHandle(0/1/2)`）。
- `executeWithIo`：查找 registry 与 task 服务（各重试 1000 次）；构造 `TaskRequest::Execute`：`data[0] = argumentCount | ((terminalId+1) << 32)`；`data[1..]` 为 56 字节 payload（path + 各参数，NUL 结尾）；`handles[0] = registry`；stdin/stdout/stderr 非 0 时追加到 handles 并置 `taskExecuteHasStdin/Stdout/Stderr` 标志。成功时回复携带子进程句柄。
- `waitProcess`：循环发送 `TaskRequest::Wait`；回复 `data[0]==1` 表示退出（取 `data[1]` 为退出码，关闭句柄）；`==2` 返回 `ErrorCode::Interrupted`；其它 `sleep(1)` 重试。
- `listProcess`：`TaskRequest::List`，回复 id、state、48 字节名字。
- `killProcess`：即 `sendSignal(processId, 15)`（SIGTERM）。

## 3. 文件客户端（file.cc，fs 服务）

所有函数第一参数 `fs` 是文件系统服务端点。

### 同步操作

```cpp
uint64_t openFile(uint64_t fs, const char* path);
bool closeFile(uint64_t fs, uint64_t descriptor);
int64_t readFile(uint64_t fs, uint64_t descriptor, void* buffer, size_t size);
int64_t writeFile(uint64_t fs, uint64_t descriptor, const void* buffer, size_t size);
bool seekFile(uint64_t fs, uint64_t descriptor, uint64_t offset);          // 绝对偏移
bool seekFilePosition(uint64_t fs, uint64_t descriptor, int64_t offset, int whence, uint64_t& position);
bool createFile(uint64_t fs, const char* path);
bool makeDirectory(uint64_t fs, const char* path);
bool removePath(uint64_t fs, const char* path);
bool truncateFile(uint64_t fs, const char* path);
bool statFile(uint64_t fs, const char* path, FileStat& stat);
bool linkFile(uint64_t fs, const char* existing, const char* newPath);
bool changeFileMode(uint64_t fs, const char* path, uint32_t mode);
bool changeFileOwner(uint64_t fs, const char* path, uint32_t uid, uint32_t gid);
bool changeFileTimes(uint64_t fs, const char* path, uint64_t atime, uint64_t mtime);
bool readDirectory(uint64_t fs, const char* path, uint64_t index, DirectoryEntry& entry);
int64_t readDirectoryBatch(uint64_t fs, const char* path, uint64_t index, DirectoryEntry* entries, size_t capacity);
bool mountFileSystem(uint64_t fs, const char* device, const char* target, const char* type);
bool unmountFileSystem(uint64_t fs, const char* target);
bool listMount(uint64_t fs, uint32_t index, MountInfo& info);
bool formatFileSystem(uint64_t fs, const char* device, const char* type);
```

- 读/写分块：`readFile` 每块最多 4112 字节（`(515-1)*8`）；`writeFile` 每块最多 4104 字节（`(515-2)*8`）。循环直到完成或短读/写失败。
- `FileStat`：type、size、mode、inode、links、uid、gid、atime、mtime、ctime。

### 异步操作

```cpp
bool readFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset, size_t size,
                   uint64_t completionEndpoint, uint64_t requestId);
bool writeFileAsync(uint64_t fs, uint64_t descriptor, uint64_t offset, const void* buffer, size_t size,
                    uint64_t completionEndpoint, uint64_t requestId);
bool waitFileOperation(uint64_t completionEndpoint, FileOperationResult& result,
                       void* buffer = nullptr, size_t capacity = 0);
```

- 异步请求用 `sendMessage` 单向发送，完成端点作为传递句柄 `handles[0]`。
- `waitFileOperation`：阻塞接收，过滤 `type == fsCompletionNotification`（"FSCMPL"）的消息，取 requestId、status、transferred，读数据在 `data[3..]`。
- 单次异步传输上限 `maximumAsyncFileTransfer = 4088` 字节。

### fork 支持

```cpp
bool cloneFileDescriptors(uint64_t fs, uint64_t parentPid, uint64_t old[], uint64_t new[], size_t capacity);
```

`FsRequest::CloneDescriptors`，fork 后子进程认领父进程的文件句柄（回复为 old/new 句柄对数组）。

## 4. 窗口客户端（gui.cc，window 服务）

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
