# OS 服务客户端

lib/os 的客户端面向Ring3 服务进程及 Ring0 .kernel 机制对象；业务接口使用类方法。

## 服务发现

os::ServiceRegistry::registerService、lookupService、unregisterService 传递端点及 generation。serviceRegistry 返回已配置的引导端点。保护名由内核 ServiceRole 授权，lookup 只获得 Write 权限。os::ServiceConnection 缓存连接，失效后清缓存，不重放有副作用请求。

## 任务

os::Process::execute / executeWithIo 请求 task 服务读取并加载磁盘 ELF，最多 64 个参数，支持重定向标准流。任务查询、等待、终止、进程组、凭据及信号经驻留 task.kernel 内联执行。fork/exec 的完整上下文机制保留直接 syscall。

终端身份属于 os::Terminal 类；端点工厂和取消属于 os::Endpoint 类。内存策略通过 os::VirtualMemory 访问 Ring 3 mm 服务，底层页对象通过 os::Memory 访问驻留 memory 服务。

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

内部事件订阅缓存：`EventSubscription {service, window, endpoint}`，容量 16，`std::mutex` 保护。`eventEndpoint()` 先查缓存，未命中则 os::Endpoint::create() 并发送 `SubscribeEvents` 注册；窗口销毁时关闭端点。

窗口客户端由 os::Window 类的静态方法提供，包括 createWindow、destroyWindow、showWindow、hideWindow、resizeWindow、moveWindow、drawPixel、drawLine、fillRectangle、drawText、presentWindow、getNextWindowEvent、setWindowSessionActive、setWindowLayer 和 raiseWindow。具体参数以 include/osgui-c++/osgui/gui.h 为准。

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
