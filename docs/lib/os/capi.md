# os/capi.h



当前来源：`lib/libc/os_adapter.h`；推荐头文件为 `os/capi.h`。

## 内容与边界

原 lib/libc/os_adapter.h 归入 include/os-c++/os/capi.h，使用方统一包含 os/capi.h，不保留旧头文件。可同时由 C 和 C++ 包含，通过 extern "C" 固定链接名，不包含 C++ 类、模板和引用。

函数名统一为 os + 对应类或服务 + 功能，例如 osFileRead、osDirCreate、osTaskSetNice、osMemMapAnonymous；完整逐项说明见[C API](capi.md)。不公开 operation 参数或 bool set 参数来切换不同业务。

C ABI 确实需要的固定参数和结果可用 struct，明确大小、对齐和所有权。实际改名时同步所有调用方、导出符号和 SDK，旧名字不作为长期兼容接口保留。

C 接口能直接发服务请求时优先直接走 IPC，不先构造或调用对应 C++ 业务对象。本地做参数检查、路径解析、消息编码和结果验证；需要多步操作时组织准备和失败回滚，不能只把复杂度推给调用者。消息和地址等待仍使用保留的 syscall。

独立于 libc 私有状态的系统桥接实现归 capi.cc，并从 libos 正确导出；libc 自己的描述符表、FILE、errno 及退出清理仍由 libc 管理。不能只搬头文件，却让声明的函数全部隐藏在另一个共享库。动态解释器只链接所需底层实现，不能因此强依赖完整 libc。

## 枚举

### OS_FILE_TYPE_*

底层类型：`int`。C 文件类型数值，与 File::Type 保持一致。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `OS_FILE_TYPE_NONE` | `0` | 无有效文件类型 |
| `OS_FILE_TYPE_FILE` | `1` | 普通文件 |
| `OS_FILE_TYPE_DIRECTORY` | `2` | 目录 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

当前公共头文件没有声明需要公开的独立数值或字符串常量。

## 全局变量

当前公共头文件没有导出可写全局变量。需要共享的初始化或缓存状态放在实现中，不通过应用头文件开放可写变量。

environ 由 C 运行库和 POSIX 头文件管理，不在 os/capi.h 重复导出；描述符表、环境槽、fork 协调状态也不成为公共全局变量。

## 其他类型与固定布局

- OsFileStat、OsDirectoryEntry、OsTaskInfo、OsCredentialInfo、OsFileOperationResult、OsExecImageInfo、OsSignalAction、OsSignalStack、OsMountStat、OsMessageQueueAttributes 和 OsIpcMessage 是 C 边界的明确布局记录。
- OsTaskInfo 已含父进程、进程组和会话标识，不增加 OsTaskIdentifiers；管道创建用两个输出引用，不新增 OsPipeHandles。

## 类与函数

### 命名与调用方式

名字统一为 os + 类或服务名 + 功能，例如 osFileRead、osTaskSetNice、osMemMapAnonymous。没有 __libc_os_ 公开名字，也没有 operation 参数或 bool set 参数选择不同业务。

C 没有构造和析构，故 C API 明确保留 Open/Create/Close，调用者负责配对。C++ 的 open/create/close 使用同一底层机制报告错误，析构仅作兜底清理，不改变 C 调用者的引用责任。

能直接发送请求的接口优先直接使用 IPC。参数检查、路径解析、字符串处理、批量分段及结果校验在本地进行；多个调用需要准备或回滚时，在 C 适配实现中组织步骤，不为省事创建对应的 C++ 业务对象。地址等待和消息发送接收使用保留的 syscall。

OsFileStat、OsTaskInfo、OsCredentialInfo、OsDirectoryEntry、OsFileOperationResult、OsExecImageInfo、OsSignalAction、OsSignalStack、OsMountStat、OsMessageQueueAttributes、OsIpcMessage 是 C 边界的明确布局记录，大小和字段定义单独维护。它们不与 C++ 类内存互相强转；OsTaskInfo 已包含父进程、进程组、会话字段，不再新增 OsTaskIdentifiers。

状态函数 0 成功、负 ErrorCode 失败；数据传输返回实际字节数或负错误码；创建函数通过输出参数交接引用。参数长度、空指针是否允许、单位和部分成功的处理按下列说明固定。

### 文件

#### osFileOpen

```c
int osFileOpen(uint64_t fs, const char* path, int flags, uint32_t mode, uint64_t* handle);
```

用途：打开路径，成功时输出文件引用。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。
- `int flags`：文件打开方式：O_RDONLY、O_WRONLY 或 O_RDWR，加上支持的 O_CREAT、O_TRUNC、O_APPEND、O_NONBLOCK 等；不允许填映射标志。
- `uint32_t mode`：创建时的权限位，例如 0644 或 0755；打开已存在对象时不借此修改它的权限。
- `uint64_t* handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileClose

```c
int osFileClose(uint64_t fs, uint64_t handle);
```

用途：关闭一份文件引用，不删除文件名称。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileRead

```c
int64_t osFileRead(uint64_t fs, uint64_t handle, void* buffer, size_t size);
```

用途：从当前文件偏移读取，成功后推进共享偏移。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osFileWrite

```c
int64_t osFileWrite(uint64_t fs, uint64_t handle, const void* buffer, size_t size);
```

用途：从当前文件偏移写入，保留部分完成结果。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `const void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osFileReadAt

```c
int64_t osFileReadAt(uint64_t fs, uint64_t handle, void* buffer, size_t size, uint64_t offset);
```

用途：从给定位置读取，不改变顺序读写位置。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osFileWriteAt

```c
int64_t osFileWriteAt(uint64_t fs, uint64_t handle, const void* buffer, size_t size, uint64_t offset);
```

用途：从给定位置写入，不改变顺序读写位置。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `const void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osFileSeek

```c
int64_t osFileSeek(uint64_t fs, uint64_t handle, int64_t offset, int whence);
```

用途：修改文件读写位置，返回修改后的位置。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `int64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。
- `int whence`：SEEK_SET、SEEK_CUR 或 SEEK_END，分别从开头、当前位置或末尾定位。

结果：返回修改后的文件偏移，单位为字节；负 ErrorCode 表示失败。

#### osFileTruncate

```c
int osFileTruncate(uint64_t fs, uint64_t handle, uint64_t length);
```

用途：修改打开文件的长度。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t length`：文件修改后的字节长度。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileFlush

```c
int osFileFlush(uint64_t fs, uint64_t handle);
```

用途：等待文件的已提交修改完成写回。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileQuery

```c
int osFileQuery(uint64_t fs, uint64_t handle, OsFileStat* result);
```

用途：一次读取打开文件的类型、长度、权限、时间和所有者。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `OsFileStat* result`：成功时填写的 OsFileStat 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileSetMode

```c
int osFileSetMode(uint64_t fs, uint64_t handle, uint32_t mode);
```

数据：文件权限位。

#### osFileSetOwner

```c
int osFileSetOwner(uint64_t fs, uint64_t handle, uint32_t uid, uint32_t gid);
```

数据：文件所有者用户编号和用户组编号。

#### osFileSetTimes

```c
int osFileSetTimes(uint64_t fs, uint64_t handle, uint64_t accessTime, uint64_t modificationTime);
```

数据：文件访问时间和内容修改时间。

#### osFileReadAsync

```c
int osFileReadAsync(uint64_t fs, uint64_t handle, uint64_t offset, size_t size, uint64_t completionEndpoint, uint64_t requestId);
```

用途：提交异步读取，完成通知发送到指定端点。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint64_t completionEndpoint`：接收异步文件完成消息的端点，等待完成前必须保持有效。
- `uint64_t requestId`：调用者分配的请求编号，用来对应提交和完成通知。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileWriteAsync

```c
int osFileWriteAsync(uint64_t fs, uint64_t handle, uint64_t offset, const void* buffer, size_t size, uint64_t completionEndpoint, uint64_t requestId);
```

用途：提交异步写入；说明服务何时复制输入，避免过早释放缓冲区。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t fs`：要使用的文件服务端点；由服务发现取得，不是 POSIX 文件描述符。
- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。
- `const void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint64_t completionEndpoint`：接收异步文件完成消息的端点，等待完成前必须保持有效。
- `uint64_t requestId`：调用者分配的请求编号，用来对应提交和完成通知。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFileWaitOperation

```c
int osFileWaitOperation(uint64_t completionEndpoint, OsFileOperationResult* result, void* buffer, size_t capacity, uint64_t deadline);
```

用途：等待异步完成，取回结果以及异步读取的数据。

实现方式：直接从完成端点接收消息并校验请求编号和长度，不创建 File::Opt。

参数：

- `uint64_t completionEndpoint`：接收异步文件完成消息的端点，等待完成前必须保持有效。
- `OsFileOperationResult* result`：成功时填写的 OsFileOperationResult 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。
- `void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t capacity`：输出缓冲区可容纳的字节数；目录条目数组中表示可容纳多少项。
- `uint64_t deadline`：单调时钟纳秒绝对截止时间；使用约定无限值表示不设截止时间。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

### 路径、目录和挂载

#### osFsQuery

```c
int osFsQuery(const char* path, OsFileStat* result);
```

用途：查询路径属性，不打开读写引用。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。
- `OsFileStat* result`：成功时填写的 OsFileStat 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFsRemove

```c
int osFsRemove(const char* path);
```

用途：删除一个文件名称，不用于递归删除目录。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFsRename

```c
int osFsRename(const char* existing, const char* target);
```

用途：将文件改名或移动到目标路径。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* existing`：已经存在的文件或目录路径。
- `const char* target`：改名、链接或挂载后的目标路径；挂载时必须是合法目录。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFsLink

```c
int osFsLink(const char* existing, const char* target);
```

用途：为已有文件建立硬链接名称。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* existing`：已经存在的文件或目录路径。
- `const char* target`：改名、链接或挂载后的目标路径；挂载时必须是合法目录。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osFsFormat

```c
int osFsFormat(const char* device, const char* type);
```

用途：请求格式化指定块设备，需通过权限和占用检查。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* device`：需要格式化的块设备路径，服务检查权限与占用。
- `const char* type`：文件系统名称，例如 ext3；消息通知中为通知方式。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osDirOpen

```c
int osDirOpen(const char* path, uint64_t* handle);
```

用途：打开目录，输出目录引用。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。
- `uint64_t* handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osDirClose

```c
int osDirClose(uint64_t handle);
```

用途：关闭目录引用，不删除目录。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osDirCreate

```c
int osDirCreate(const char* path, uint32_t mode);
```

用途：创建一个目录。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。
- `uint32_t mode`：创建时的权限位，例如 0644 或 0755；打开已存在对象时不借此修改它的权限。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osDirRemove

```c
int osDirRemove(const char* path);
```

用途：删除指定空目录，非空时返回错误。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osDirRename

```c
int osDirRename(const char* existing, const char* target);
```

用途：将目录改名或移动。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* existing`：已经存在的文件或目录路径。
- `const char* target`：改名、链接或挂载后的目标路径；挂载时必须是合法目录。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osDirRead

```c
int osDirRead(uint64_t handle, uint64_t offset, OsDirectoryEntry* entries, size_t capacity, size_t* count);
```

用途：批量读取目录项，输出数量；每项的 nextOffset 用于继续读取。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。
- `OsDirectoryEntry* entries`：输出目录项数组，至少有 capacity 个元素位置。
- `size_t capacity`：输出缓冲区可容纳的字节数；目录条目数组中表示可容纳多少项。
- `size_t* count`：输入时为组列表长度或最大唤醒线程数；指针输出时接收实际目录项数量。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMountCreate

```c
int osMountCreate(const char* source, const char* target, const char* type, unsigned long flags, const void* data, uint64_t* mountId);
```

用途：建立挂载，返回用于跟踪本次挂载的身份。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `const char* source`：挂载源设备路径；无设备文件系统使用其规定的空值或源名称。
- `const char* target`：改名、链接或挂载后的目标路径；挂载时必须是合法目录。
- `const char* type`：文件系统名称，例如 ext3；消息通知中为通知方式。
- `unsigned long flags`：挂载选项，例如只读；只接受目标文件系统及 fs 服务支持的位。
- `const void* data`：该文件系统定义的附加挂载参数，不需要时传 NULL。
- `uint64_t* mountId`：代表本次挂载的稳定身份，输出后用于卸载，不能用可能复用的目标路径冒充同一挂载。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMountClose

```c
int osMountClose(uint64_t mountId);
```

用途：卸载该身份对应的挂载，忙或无权限时返回错误。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint64_t mountId`：代表本次挂载的稳定身份，输出后用于卸载，不能用可能复用的目标路径冒充同一挂载。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMountQuery

```c
int osMountQuery(uint32_t index, OsMountStat* result);
```

用途：读取已有挂载的设备、目标路径和类型，不授予卸载责任。

实现方式：直接向 fs 服务发送请求；只在本地检查参数和组装消息，不创建 File、Fs 或 Dir 对象。

参数：

- `uint32_t index`：从 0 开始的查询序号。
- `OsMountStat* result`：成功时填写的 OsMountStat 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

### IPC、管道和消息队列

#### osIpcCreate

```c
int osIpcCreate(uint64_t* endpoint);
```

用途：创建消息端点。

实现方式：直接使用保留的消息 syscall 发送内核 IPC 服务请求，不创建 Ipc::Handle 对象。

参数：

- `uint64_t* endpoint`：消息或设备通知使用的端点；指针输出时接收新建或查找到的端点。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcSend

```c
int osIpcSend(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline);
```

用途：向端点发送消息，按约定处理携带句柄。

实现方式：直接调用对应的四个消息 syscall，不经 C++ 对象。

参数：

- `uint64_t endpoint`：消息或设备通知使用的端点；指针输出时接收新建或查找到的端点。
- `OsIpcMessage* message`：固定 C 消息记录；接收时填写负载和由内核认证的发送者身份。
- `uint64_t deadline`：单调时钟纳秒绝对截止时间；使用约定无限值表示不设截止时间。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcReceive

```c
int osIpcReceive(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline);
```

用途：从端点接收消息，发送者身份由内核填写。

实现方式：直接调用对应的四个消息 syscall，不经 C++ 对象。

参数：

- `uint64_t endpoint`：消息或设备通知使用的端点；指针输出时接收新建或查找到的端点。
- `OsIpcMessage* message`：固定 C 消息记录；接收时填写负载和由内核认证的发送者身份。
- `uint64_t deadline`：单调时钟纳秒绝对截止时间；使用约定无限值表示不设截止时间。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcCall

```c
int osIpcCall(uint64_t endpoint, OsIpcMessage* request, OsIpcMessage* reply, uint64_t deadline);
```

用途：发送请求并等待对应回复。

实现方式：直接调用对应的四个消息 syscall，不经 C++ 对象。

参数：

- `uint64_t endpoint`：消息或设备通知使用的端点；指针输出时接收新建或查找到的端点。
- `OsIpcMessage* request`：待发送请求记录；包含有效的负载长度和携带句柄。
- `OsIpcMessage* reply`：用于接收或发送回复的固定 C 消息记录。
- `uint64_t deadline`：单调时钟纳秒绝对截止时间；使用约定无限值表示不设截止时间。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcReply

```c
int osIpcReply(uint64_t endpoint, uint64_t replyToken, OsIpcMessage* reply);
```

用途：回复此前收到的请求。

实现方式：直接调用对应的四个消息 syscall，不经 C++ 对象。

参数：

- `uint64_t endpoint`：消息或设备通知使用的端点；指针输出时接收新建或查找到的端点。
- `uint64_t replyToken`：从此前收到的请求中取得的回复凭据，不能自行编造。
- `OsIpcMessage* reply`：用于接收或发送回复的固定 C 消息记录。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcHandleClose

```c
int osIpcHandleClose(uint64_t handle);
```

用途：释放一份通用资源引用。

实现方式：直接使用保留的消息 syscall 发送内核 IPC 服务请求，不创建 Ipc::Handle 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcHandleDuplicate

```c
int osIpcHandleDuplicate(uint64_t handle, uint32_t rights, uint32_t flags, uint64_t* result);
```

用途：复制资源引用，输出权限不超过原引用的新句柄。

实现方式：直接使用保留的消息 syscall 发送内核 IPC 服务请求，不创建 Ipc::Handle 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint32_t rights`：新引用允许的权限集合，不得超过原有引用。
- `uint32_t flags`：句柄继承和 exec 关闭标志，例如禁止继承、exec 时关闭；不表示文件权限。
- `uint64_t* result`：成功时填写的 uint64_t 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcHandleRestrict

```c
int osIpcHandleRestrict(uint64_t handle, uint32_t rights);
```

用途：缩减句柄权限。

实现方式：直接使用保留的消息 syscall 发送内核 IPC 服务请求，不创建 Ipc::Handle 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint32_t rights`：新引用允许的权限集合，不得超过原有引用。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcHandleSetFlags

```c
int osIpcHandleSetFlags(uint64_t handle, uint32_t flags);
```

数据：句柄继承和 exec 关闭标志。

#### osIpcPipeCreate

```c
int osIpcPipeCreate(uint64_t* readHandle, uint64_t* writeHandle);
```

用途：创建单向管道，分别输出读端和写端。

实现方式：直接向 pipe 服务请求，不创建 Ipc::Pipe。

参数：

- `uint64_t* readHandle`：管道读端；指针输出时接收新读端引用。
- `uint64_t* writeHandle`：管道写端；指针输出时接收新写端引用。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcPipeRead

```c
int64_t osIpcPipeRead(uint64_t readHandle, void* buffer, size_t size);
```

用途：从管道读端读取数据。

实现方式：直接向 pipe 服务请求，不创建 Ipc::Pipe。

参数：

- `uint64_t readHandle`：管道读端；指针输出时接收新读端引用。
- `void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osIpcPipeWrite

```c
int64_t osIpcPipeWrite(uint64_t writeHandle, const void* buffer, size_t size);
```

用途：向管道写端写入数据。

实现方式：直接向 pipe 服务请求，不创建 Ipc::Pipe。

参数：

- `uint64_t writeHandle`：管道写端；指针输出时接收新写端引用。
- `const void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osIpcMsgQueueOpen

```c
int osIpcMsgQueueOpen(const char* name, int flags, uint32_t maximumMessages, uint32_t messageSize, uint64_t* handle);
```

用途：打开或创建命名消息队列。

实现方式：直接向消息队列服务发送请求，不创建 Ipc::MsgQueue 对象。

参数：

- `const char* name`：队列、共享内存或服务的零结尾名称。
- `int flags`：消息队列打开方式，包括只读、只写、读写、创建及非阻塞；创建参数只在需要创建队列时使用。
- `uint32_t maximumMessages`：新建队列允许保存的最大消息数，已有队列按服务规则验证。
- `uint32_t messageSize`：新建队列每条消息允许的最大字节数。
- `uint64_t* handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcMsgQueueUnlink

```c
int osIpcMsgQueueUnlink(const char* name);
```

用途：移除队列名称，已有引用继续有效。

实现方式：直接向消息队列服务发送请求，不创建 Ipc::MsgQueue 对象。

参数：

- `const char* name`：队列、共享内存或服务的零结尾名称。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcMsgQueueSend

```c
int osIpcMsgQueueSend(uint64_t handle, const void* buffer, size_t size, uint32_t priority);
```

用途：发送带优先级的一条队列消息。

实现方式：直接向消息队列服务发送请求，不创建 Ipc::MsgQueue 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `const void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint32_t priority`：发送时为本条消息的优先级；指针输出时接收消息携带的优先级。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcMsgQueueReceive

```c
int64_t osIpcMsgQueueReceive(uint64_t handle, void* buffer, size_t capacity, uint32_t* priority);
```

用途：接收队列消息，并输出它的优先级。

实现方式：直接向消息队列服务发送请求，不创建 Ipc::MsgQueue 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t capacity`：输出缓冲区可容纳的字节数；目录条目数组中表示可容纳多少项。
- `uint32_t* priority`：发送时为本条消息的优先级；指针输出时接收消息携带的优先级。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osIpcMsgQueueQuery

```c
int osIpcMsgQueueQuery(uint64_t handle, OsMessageQueueAttributes* result);
```

用途：读取队列标志、容量、单条大小及当前消息数。

实现方式：直接向消息队列服务发送请求，不创建 Ipc::MsgQueue 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `OsMessageQueueAttributes* result`：成功时填写的 OsMessageQueueAttributes 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osIpcMsgQueueSetFlags

```c
int osIpcMsgQueueSetFlags(uint64_t handle, uint64_t flags, uint64_t* previous);
```

数据：消息队列标志及修改前的值。

#### osIpcMsgQueueSetNotification

```c
int osIpcMsgQueueSetNotification(uint64_t handle, int type, uint32_t signal);
```

数据：队列通知方式和信号编号。

### 控制台和 PTY

#### osConsoleRead

```c
int64_t osConsoleRead(void* buffer, size_t size);
```

用途：读取当前控制台输入。

实现方式：直接向 console 或 tty 服务发送请求，不创建 Console 或 Pty 对象。

参数：

- `void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osConsoleWrite

```c
int64_t osConsoleWrite(const void* buffer, size_t size);
```

用途：向当前控制台输出字节。

实现方式：直接向 console 或 tty 服务发送请求，不创建 Console 或 Pty 对象。

参数：

- `const void* buffer`：读操作中的可写输出内存，或写操作中的只读输入内存。
- `size_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：返回实际传输的字节数或负 ErrorCode；写入允许部分完成，返回值小于请求长度时调用者需处理剩余部分。

#### osConsoleGetId

```c
int osConsoleGetId(uint32_t* id);
```

数据：当前进程绑定的控制台编号。

#### osConsoleSetId

```c
int osConsoleSetId(uint32_t id);
```

数据：当前进程绑定的控制台编号。

#### osPtyCreate

```c
int osPtyCreate(uint64_t* master, uint64_t* slave);
```

用途：创建 PTY 主从端点并分别交接。

实现方式：直接向 console 或 tty 服务发送请求，不创建 Console 或 Pty 对象。

参数：

- `uint64_t* master`：PTY 主端引用；指针输出时接收新创建主端。
- `uint64_t* slave`：PTY 从端引用；指针输出时接收新创建从端。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osPtyGetNumber

```c
int osPtyGetNumber(uint64_t master, uint32_t* number);
```

数据：PTY 编号。

#### osPtySetNonblocking

```c
int osPtySetNonblocking(uint64_t master, int enabled);
```

数据：PTY 非阻塞设置。

#### osPtySetForegroundGroup

```c
int osPtySetForegroundGroup(uint64_t master, uint64_t group);
```

数据：PTY 前台进程组编号。

### 进程、线程、身份与调度

#### osTaskGetId

```c
int osTaskGetId(uint64_t* id);
```

数据：当前进程编号。

#### osTaskQuery

```c
int osTaskQuery(uint64_t id, OsTaskInfo* result);
```

用途：一次查询进程状态、身份、父进程、进程组和会话等字段，不再另设 Ids 结果。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。
- `OsTaskInfo* result`：成功时填写的 OsTaskInfo 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskGetParentId

```c
int osTaskGetParentId(uint64_t id, uint64_t* parentId);
```

数据：父进程编号。

#### osTaskGetGroupId

```c
int osTaskGetGroupId(uint64_t id, uint64_t* groupId);
```

数据：进程组编号。

#### osTaskGetSessionId

```c
int osTaskGetSessionId(uint64_t id, uint64_t* sessionId);
```

数据：会话编号。

#### osTaskExecute

```c
int osTaskExecute(const char* path, uint32_t argumentCount, const char* const* arguments, uint64_t input, uint64_t output, uint64_t error, uint64_t* handle);
```

用途：启动程序并交接标准输入输出引用；本地完成路径和 ELF 参数准备。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `const char* path`：以零字节结束的路径；相对路径按当前工作目录解析。
- `uint32_t argumentCount`：arguments 中有效参数字符串的数量。
- `const char* const* arguments`：参数字符串数组，启动请求完成前保持有效。
- `uint64_t input`：交给新程序的标准输入资源引用。
- `uint64_t output`：交给新程序的标准输出资源引用。
- `uint64_t error`：交给新程序的标准错误资源引用。
- `uint64_t* handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskFork

```c
int64_t osTaskFork(void);
```

用途：复制当前进程；父进程得到子进程编号，子进程得到 0。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：无。

结果：父进程返回子进程编号，子进程返回 0；失败返回负 ErrorCode。

#### osTaskReplaceImage

```c
int osTaskReplaceImage(const OsExecImageInfo* image);
```

用途：替换当前进程映像；成功后不返回旧程序。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `const OsExecImageInfo* image`：包含映像地址、长度、参数和环境的 C 参数记录，调用完成前保持有效。

结果：成功后不返回旧程序；返回时表示失败，并给出负 ErrorCode。

#### osTaskWaitChild

```c
int64_t osTaskWaitChild(int64_t id, int* status, int options);
```

用途：等待匹配的子进程，返回实际匹配的编号。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `int64_t id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。
- `int* status`：输入时为希望使用的退出码，输出指针时接收等待结果。
- `int options`：等待选项，例如只检查而不阻塞；只能使用服务支持的选项。

结果：返回实际取得状态的子进程编号；不阻塞选项下尚无结果时返回 0，失败返回负 ErrorCode。

#### osTaskSendSignal

```c
int osTaskSendSignal(uint64_t id, uint32_t signal);
```

用途：向进程发送信号。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。
- `uint32_t signal`：信号编号。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskExit

```c
void osTaskExit(int status);
```

用途：结束当前进程，成功后不返回。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `int status`：输入时为希望使用的退出码，输出指针时接收等待结果。

结果：不返回数值；Exit、ThreadExit、SignalReturn 成功后不返回调用位置。

#### osTaskThreadGetId

```c
int osTaskThreadGetId(uint64_t* id);
```

数据：当前线程编号。

#### osTaskThreadCreate

```c
int osTaskThreadCreate(void (*entry)(void*), void* argument, void* stack, size_t stackSize, uint64_t* id);
```

用途：创建线程并输出线程编号，不接管借用的栈缓冲区。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `void (*entry)(void*)`：新线程入口函数，线程开始时调用 entry(argument)。
- `void* argument`：传给新线程入口的指针，库不接管其中数据。
- `void* stack`：线程栈或信号备用栈；线程创建时是借用内存，线程结束前不能释放。
- `size_t stackSize`：线程栈内存的字节数。
- `uint64_t* id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskThreadWait

```c
int osTaskThreadWait(uint64_t id, int* exitCode, int noHang);
```

用途：等待线程结束，或在 noHang 时立即检查。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。
- `int* exitCode`：输出线程退出码。
- `int noHang`：非 0 时只检查一次；线程未结束时不等待。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskThreadSendSignal

```c
int osTaskThreadSendSignal(uint64_t id, uint32_t signal);
```

用途：向指定线程发送信号。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。
- `uint32_t signal`：信号编号。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskThreadExit

```c
void osTaskThreadExit(int status);
```

用途：结束当前线程，不结束同进程其他线程。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `int status`：输入时为希望使用的退出码，输出指针时接收等待结果。

结果：不返回数值；Exit、ThreadExit、SignalReturn 成功后不返回调用位置。

#### osTaskSessionCreate

```c
int osTaskSessionCreate(uint64_t* id);
```

用途：为当前进程建立新会话并输出编号。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t* id`：目标进程、线程或控制台编号；指针输出时接收当前或新建对象编号。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osTaskSetGroup

```c
int osTaskSetGroup(uint64_t id, uint64_t group);
```

数据：进程组归属。

#### osTaskGetNice

```c
int osTaskGetNice(uint64_t id, int* value);
```

数据：目标进程的 nice 值。

#### osTaskSetNice

```c
int osTaskSetNice(uint64_t id, int value);
```

数据：目标进程的 nice 值。

#### osTaskGetResourceLimit

```c
int osTaskGetResourceLimit(uint64_t id, int resource, uint64_t* soft, uint64_t* hard);
```

数据：资源软限制和硬限制。

#### osTaskSetResourceLimit

```c
int osTaskSetResourceLimit(uint64_t id, int resource, uint64_t soft, uint64_t hard);
```

数据：资源软限制和硬限制。

#### osTaskGetCredentials

```c
int osTaskGetCredentials(uint64_t id, OsCredentialInfo* result);
```

数据：真实、有效、保存的用户和用户组编号及补充组。

#### osTaskSetUserIds

```c
int osTaskSetUserIds(uint32_t real, uint32_t effective, uint32_t saved);
```

数据：真实、有效和保存的用户编号。

#### osTaskSetGroupIds

```c
int osTaskSetGroupIds(uint32_t real, uint32_t effective, uint32_t saved);
```

数据：真实、有效和保存的用户组编号。

#### osTaskSetGroups

```c
int osTaskSetGroups(const uint32_t* groups, uint32_t count);
```

数据：补充用户组列表。

#### osTaskYield

```c
void osTaskYield(void);
```

用途：让出当前 CPU 时间，之后继续执行。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：无。

结果：不返回数值；Exit、ThreadExit、SignalReturn 成功后不返回调用位置。

#### osTaskSleep

```c
int osTaskSleep(uint64_t milliseconds);
```

用途：休眠指定毫秒数；提前被信号中断时返回对应状态。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t milliseconds`：希望休眠的毫秒数。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

### 信号

#### osTaskGetSignalAction

```c
int osTaskGetSignalAction(uint32_t signal, OsSignalAction* result);
```

数据：指定信号的处理配置。

#### osTaskSetSignalAction

```c
int osTaskSetSignalAction(uint32_t signal, const OsSignalAction* action, OsSignalAction* previous);
```

数据：指定信号的新处理配置及旧配置。

#### osTaskGetSignalMask

```c
int osTaskGetSignalMask(uint64_t* mask);
```

数据：当前线程信号屏蔽集。

#### osTaskSetSignalMask

```c
int osTaskSetSignalMask(int how, uint64_t mask, uint64_t* previous);
```

数据：信号屏蔽集及修改方式和旧值。

#### osTaskGetPendingSignals

```c
int osTaskGetPendingSignals(uint64_t* mask);
```

数据：待处理信号集合。

#### osTaskWaitSignal

```c
int osTaskWaitSignal(uint64_t mask);
```

用途：等待指定集合中的信号。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `uint64_t mask`：信号位集合；指针输出时接收查询到的集合。

结果：返回实际取得的信号编号，失败返回负 ErrorCode。

#### osTaskGetSignalStack

```c
int osTaskGetSignalStack(OsSignalStack* result);
```

数据：备用信号栈配置。

#### osTaskSetSignalStack

```c
int osTaskSetSignalStack(const OsSignalStack* stack, OsSignalStack* previous);
```

数据：备用信号栈的新配置及旧配置。

#### osTaskGetCoreLimit

```c
int osTaskGetCoreLimit(uint64_t* soft, uint64_t* hard);
```

数据：core 文件大小软限制和硬限制。

#### osTaskSetCoreLimit

```c
int osTaskSetCoreLimit(uint64_t soft, uint64_t hard);
```

数据：core 文件大小软限制和硬限制。

#### osTaskSignalReturn

```c
void osTaskSignalReturn(void* frame);
```

用途：供信号返回跳板恢复执行上下文，成功后不返回该函数；不是普通应用随意恢复线程的接口。

实现方式：直接向对应任务服务发送请求，由服务检查实际调用者身份和权限，不创建 Task 对象。

参数：

- `void* frame`：内核认可的信号返回上下文，供返回跳板使用，不能由普通函数随意构造。

结果：不返回数值；Exit、ThreadExit、SignalReturn 成功后不返回调用位置。

### 内存

#### osMemMapAnonymous

```c
int osMemMapAnonymous(uint64_t address, uint64_t size, uint32_t protection, uint32_t flags, uint64_t* result);
```

用途：创建匿名映射；本地检查地址和标志，实际页和映射由服务建立。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `uint64_t address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint32_t protection`：读取、写入、执行访问权限位。
- `uint32_t flags`：映射方式标志：私有或共享、是否固定地址等；组合必须符合匿名或文件映射规则，地址替换失败不能留下半完成范围。
- `uint64_t* result`：成功时填写的 uint64_t 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemMapFile

```c
int osMemMapFile(uint64_t handle, uint64_t address, uint64_t size, uint64_t offset, uint32_t protection, uint32_t flags, uint64_t* result);
```

用途：建立私有或共享文件映射。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。
- `uint32_t protection`：读取、写入、执行访问权限位。
- `uint32_t flags`：映射方式标志：私有或共享、是否固定地址等；组合必须符合匿名或文件映射规则，地址替换失败不能留下半完成范围。
- `uint64_t* result`：成功时填写的 uint64_t 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemMapShared

```c
int osMemMapShared(uint64_t handle, uint64_t address, uint64_t size, uint64_t offset, uint32_t protection, uint64_t* result);
```

用途：把共享内存对象映射到当前地址空间。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `uint64_t offset`：文件或映射的起始字节偏移；目录读取中是上次返回的 nextOffset。
- `uint32_t protection`：读取、写入、执行访问权限位。
- `uint64_t* result`：成功时填写的 uint64_t 结果；调用者提供可写内存，失败不把未完成结果当作有效数据。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemMapUnmap

```c
int osMemMapUnmap(uint64_t address, uint64_t size);
```

用途：解除指定地址范围的映射。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `uint64_t address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemMapSetProtection

```c
int osMemMapSetProtection(uint64_t address, uint64_t size, uint32_t protection);
```

数据：映射地址范围的读、写、执行权限。

#### osMemMapSync

```c
int osMemMapSync(uint64_t address, uint64_t size, int mode);
```

用途：请求同步共享文件映射修改，按 mode 决定同步等待语义。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `uint64_t address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `int mode`：同步方式，例如要求等待脏页写回完成；不能同时给出互相矛盾的同步和异步选项。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemShareOpen

```c
int osMemShareOpen(const char* name, int flags, uint64_t* handle);
```

用途：打开或创建命名共享内存对象。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `const char* name`：队列、共享内存或服务的零结尾名称。
- `int flags`：共享对象打开方式，包括访问权限、是否创建和是否要求名称不存在。
- `uint64_t* handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemShareUnlink

```c
int osMemShareUnlink(const char* name);
```

用途：删除共享内存名称，不撤销已有引用。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `const char* name`：队列、共享内存或服务的零结尾名称。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemShareResize

```c
int osMemShareResize(uint64_t handle, uint64_t size);
```

用途：调整共享内存对象长度。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `uint64_t handle`：资源引用；作为指针输出时接收新引用，调用者之后负责关闭。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osMemCheckRange

```c
int osMemCheckRange(const void* address, uint64_t size, int writable);
```

用途：检查地址范围当前是否允许读或写；实际访问仍由内核检查。

实现方式：直接向 mm 或内核内存服务发送请求，由服务完成映射和引用维护，不创建 Mem 或 Mem::Map 对象。

参数：

- `const void* address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint64_t size`：本次处理的字节数，不能大于调用者实际提供的缓冲区。
- `int writable`：0 仅检查可读，非 0 还要求可写。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

### 同步

#### osSyncWaitAddress

```c
int osSyncWaitAddress(volatile uint32_t* address, uint32_t expected);
```

用途：地址值仍等于 expected 时进入等待；调用者醒来后必须重新检查条件。

实现方式：直接调用保留的 WaitAddress syscall，不使用服务 IPC。

参数：

- `volatile uint32_t* address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint32_t expected`：仅在地址当前值仍等于此值时进入等待。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osSyncWakeAddress

```c
int osSyncWakeAddress(volatile uint32_t* address, uint32_t count);
```

用途：唤醒等待同一地址的线程。

实现方式：直接调用保留的 WakeAddress syscall，不使用服务 IPC。

参数：

- `volatile uint32_t* address`：映射起始或待检查地址；建立映射时 0 允许服务选择，固定地址模式按 flags 验证。
- `uint32_t count`：输入时为组列表长度或最大唤醒线程数；指针输出时接收实际目录项数量。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

### 系统与服务发现

#### osSysReadMonotonicTime

```c
int osSysReadMonotonicTime(uint64_t* nanoseconds);
```

用途：读取适合计算间隔和超时的单调时钟。

实现方式：直接向系统信息服务发送请求，不创建 Sys 对象。

参数：

- `uint64_t* nanoseconds`：输出纳秒数；墙上时钟从约定的日期纪元计算，单调时钟从系统约定起点计算。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osSysReadWallClockTime

```c
int osSysReadWallClockTime(uint64_t* nanoseconds);
```

用途：读取日历时间对应的系统时钟。

实现方式：直接向系统信息服务发送请求，不创建 Sys 对象。

参数：

- `uint64_t* nanoseconds`：输出纳秒数；墙上时钟从约定的日期纪元计算，单调时钟从系统约定起点计算。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osSysReadClockResolution

```c
int osSysReadClockResolution(uint64_t* nanoseconds);
```

用途：读取时钟最小可表示时间步长。

实现方式：直接向系统信息服务发送请求，不创建 Sys 对象。

参数：

- `uint64_t* nanoseconds`：输出纳秒数；墙上时钟从约定的日期纪元计算，单调时钟从系统约定起点计算。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

#### osSysConnectionLookup

```c
int osSysConnectionLookup(const char* name, uint64_t* endpoint, uint64_t* generation);
```

用途：发现服务端点及其登记代次。

实现方式：直接请求名称注册中心，不创建 Sys::Connection 对象。

参数：

- `const char* name`：队列、共享内存或服务的零结尾名称。
- `uint64_t* endpoint`：消息或设备通知使用的端点；指针输出时接收新建或查找到的端点。
- `uint64_t* generation`：输出服务名称登记代次，供连接失效和重启识别。

结果：成功返回 0，失败返回负 ErrorCode；输出引用仅在成功时交给调用者。

### libc 私有适配不直接公开

POSIX 描述符表克隆、fork 协调端点、exec 描述符导入、environ 槽绑定和 libc 退出清理仍属于库间内部约定，不把内部表地址当成 os/capi.h 的应用接口。需要系统请求的步骤调用上述 C IPC 接口；仅处理 libc 内存状态的步骤留在 libc。堆分配是运行时分配器操作，不为 malloc/free 人为发送一条 IPC；扩展地址范围时才访问内存服务。


## 实现文件

`lib/os/capi.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
