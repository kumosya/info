# 运行环境

## 对象

### os::Runtime

进程运行环境对象，保存启动端点、环境变量、终端选择和退出处理。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool absolutePath(const char* path, char* buffer, size_t capacity)` | 解析绝对路径 | `path`：文件或目录路径<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量 |
| `void bindWorkingDirectory(const char* (*reader)())` | 绑定工作目录查询函数 | `reader`：读取当前工作目录的回调函数 |
| `void initializeHeap()` | 初始化进程堆 | — |
| `void* allocateMemory(size_t size)` | 分配进程堆内存 | `size`：数据大小，单位为字节 |
| `bool freeMemory(void* pointer)` | 释放进程堆内存 | `pointer`：待释放的内存地址 |
| `void prepareHeapFork()` | 准备进程堆的派生操作 | — |
| `void finishHeapFork()` | 恢复父进程堆的访问 | — |
| `void resetHeapAfterFork()` | 设置子进程堆状态 | — |
| `uint64_t kernelBootstrapHandle() const` | 读取内核引导端点 | — |
| `uint64_t registryBootstrapHandle() const` | 读取服务注册表端点 | — |
| `const char* environmentValue(const char* name) const` | 读取环境变量值 | `name`：对象名称 |
| `void bindEnvironment(char*** environment)` | 绑定进程环境变量 | `environment`：进程环境变量数组 |
| `int printString(const char* string)` | 向进程终端输出字符串 | `string`：待输出的字符串 |
| `int addFinalizer(void (*function)())` | 登记退出处理函数 | `function`：程序退出时执行的函数 |
| `void runtimeFini()` | 执行进程退出处理 | — |
| `void prepare(char** environment)` | 设置进程启动环境 | `environment`：进程环境变量数组 |
| `int runtimeInit(int argumentCount, char** arguments, char** environment)` | 初始化进程运行环境 | `argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组<br>`environment`：进程环境变量数组 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint32_t getConsoleId() const` | 读取进程终端编号 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setConsoleId(uint32_t id)` | 设置进程终端编号 | `id`：终端编号 |

#### 静态成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `static Runtime& current()` | 取得进程运行环境对象 | — |
| `static void copyBytes(void* destination, const void* source, size_t size)` | 复制字节数据 | `destination`：接收复制结果的缓冲区<br>`source`：待复制的数据<br>`size`：数据大小，单位为字节 |
| `static void clearMemory(void* destination, size_t size)` | 将内存内容置零 | `destination`：接收复制结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `static void copyString(char* destination, const char* source, size_t capacity)` | 复制字符串 | `destination`：接收复制结果的缓冲区<br>`source`：待复制的数据<br>`capacity`：接收缓冲区容量 |
| `static bool stringsEqual(const char* left, const char* right)` | 比较两个字符串 | `left`：参与比较的左侧字符串<br>`right`：参与比较的右侧字符串 |
| `static size_t stringLength(const char* value)` | 计算字符串长度 | `value`：待计算长度的字符串 |

### os::ConfigurationText

配置文本对象，保存文本范围并检查字符和路径格式。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit ConfigurationText(const char* text)` | 构造ConfigurationText对象 | `text`：待检查的文本 |
| `ConfigurationText(const char* text, size_t size)` | 构造ConfigurationText对象 | `text`：待检查的文本<br>`size`：数据大小，单位为字节 |
| `~ConfigurationText() = default` | 析构ConfigurationText对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool valid() const` | 检查配置文本格式 | — |
| `bool absolutePath() const` | 解析绝对路径 | — |

## 函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `[[noreturn]] void exitTask(int exitCode)` | 结束进程 | `exitCode`：进程或线程退出码 |
| `[[noreturn]] void exitThread(int exitCode)` | 结束线程 | `exitCode`：进程或线程退出码 |
| `uint64_t createThread(void (*entry)(), uint64_t stackPointer)` | 创建线程 | `entry`：线程入口函数<br>`stackPointer`：线程栈顶地址 |
| `uint64_t createThread(void (*entry)(void*), uint64_t stackPointer, void* argument)` | 创建线程 | `entry`：线程入口函数<br>`stackPointer`：线程栈顶地址<br>`argument`：传给线程入口的上下文 |
| `int waitAddress(volatile uint32_t* address, uint32_t expected)` | 等待地址 | `address`：内存地址<br>`expected`：等待地址的预期值 |
| `int wakeAddress(volatile uint32_t* address, uint32_t count)` | 唤醒地址等待者 | `address`：内存地址<br>`count`：最多唤醒的线程数量 |
| `int64_t forkTask()` | 派生进程 | — |
| `int replaceTaskImage(const Task::Exec& image)` | 替换进程映像 | `image`：程序映像或执行选项 |
| `void yield()` | 让出处理器 | — |
| `void sleep(uint64_t milliseconds)` | 等待指定时间 | `milliseconds`：等待时长，单位为毫秒 |
| `uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection)` | 建立内存映射 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `bool unmapMemory(uint64_t address, uint64_t size)` | 解除内存映射 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `bool protectMemory(uint64_t address, uint64_t size, uint64_t protection)` | 设置内存访问权限 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `uint64_t getStandardHandle(uint32_t index)` | 读取标准输入输出句柄 | `index`：项目索引 |
| `uint64_t getBootstrapHandle()` | 读取服务注册表端点 | — |
| `bool closeHandle(uint64_t handle)` | 关闭内核句柄 | `handle`：资源句柄 |
