# os/file.h


## 内容与边界

### File

File 直接保存文件引用、路径、类型、长度、权限、inode、所有者、时间及设备号，不再拆出 MetaData。refresh 一次读取属性，getter 只读取缓存，不发 IPC。open 成功后需要属性时显式 refresh；写入或修改属性后，getSnapshotValid 返回 false。

File() 只创建空对象；open(path, flags, mode) 返回打开结果，close 返回关闭结果，允许显式重新打开。析构关闭仍由对象拥有的引用，不删除文件。read、write、pread、pwrite、seek、truncate、flush 操作已打开引用；查询属性不要求伪造一个打开引用。

Fs::query(path, file) 可以把路径属性填进一个空 File，getOpened 为 false，此时只能读属性。后续需要读写时调用 open；不能把仅有属性的对象误当作已打开文件，也不能覆盖另一个已打开对象的引用。

### File::Opt

保存一次异步读写请求、完成端点、读取数据和完成结果。poll 立即检查，wait 等待完成；getData 返回对象内部的读取数据，getTransferred 返回有效字节数。指针有效期到本对象 close、移动或析构为止；写请求的 getData 返回 nullptr。Opt 表示 operation，不是打开选项。

### Fs

只负责按路径执行文件查询、删除文件、改名、创建硬链接、格式化及已有挂载查询等跨文件操作。目录创建、删除和枚举移到 Dir。文件打开也可由 File::open 完成，不依赖必须先创建 Fs。

### Mount

Mount::create(source, target, type, flags, data) 请求挂载，返回结果；成功后直接保存设备、挂载点、类型和只读状态。Mount::close 显式卸载并返回结果，析构仅作最后尝试。getter 直接读这些字段，不再有 Entry。

Mount() 只初始化空对象，Fs::queryMount(index, mount) 只填写已有挂载信息，getOwned 为 false；析构不能卸载这种仅查询到的挂载。禁止复制。移动构造可以直接转移引用；如果覆盖旧资源可能失败，则先显式 close，再移动，不能让移动赋值吞掉清理错误。

挂载可能因设备、路径或权限失败，所以提供 getMounted、getError。卸载可能因为仍有文件使用而失败，析构不能返回错误，因此使用 close() 让调用者取得卸载结果；失败时挂载仍存在，不宣称作用域退出就一定卸载成功。

## 枚举

### File::Backend

底层类型：`int`。已有引用实际采用的读写后端。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Filesystem` | `0` | 文件系统文件 |
| `Pipe` | `1` | 管道端点 |
| `Terminal` | `2` | 控制台或终端后端 |
| `SharedMemory` | `3` | 共享内存后端 |

### File::Type

底层类型：`uint64_t`。原先错误放在 ipc.h 的文件种类，归 file.h。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `None` | `0` | 没有有效对象或事件 |
| `File` | `1` | 普通文件 |
| `Directory` | `2` | 目录 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `maximumAsyncFileTransfer` | `4088` | 当前内联异步文件传输的最大数据字节数 | 私有常量；通过 File::getMaximumAsyncTransfer() 查询 |

```cpp
size_t File::getMaximumAsyncTransfer() const;
```

数据：当前内联异步传输允许的数据字节数，现值为 4088。

## 全局变量

当前公共头文件没有导出可写全局变量。文件引用、偏移、属性缓存和异步结果属于实例，不导出全局文件对象。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### File
这个类表示一个打开的文件，或由 Fs::query 填入的只读属性快照。

#### 构造与析构

```cpp
~File();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
File();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
File(File&& other) noexcept;
```

用途：把 other 的文件引用、连接和缓存交给新对象，other 随后变为空；不复制系统引用。

参数：`other` 是需要交接的文件对象。

结果：无返回值，不执行可能失败的关闭操作。

```cpp
File& operator=(File&&) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `File&&`：另一个同类对象；移动时交接资源，禁止复制的声明不能调用。

结果：返回本对象的引用。

#### 操作

```cpp
int open(const char* path, int flags, mode_t mode = 0666);
```

用途：打开指定路径；成功后对象保存访问引用，后续读写或枚举使用这个对象。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。
- `int flags`：本次打开方式及创建、追加、截断、非阻塞等标志，不是权限位。
- `mode_t mode = 0666`：创建时的权限位；已有文件的权限不因再次打开而改变。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int close();
```

用途：关闭对象拥有的文件或目录引用；成功后对象变为空，不删除磁盘上的内容。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
ssize_t read(void* buffer, size_t size);
```

用途：从文件、管道或消息负载读取字节到调用者缓冲区。

参数：

- `void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
ssize_t write(const void* buffer, size_t size);
```

用途：把调用者缓冲区中的字节写入文件、管道或消息负载。

参数：

- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
ssize_t pread(void* buffer, size_t size, off_t offset);
```

用途：从指定文件偏移读取，不改变共享的顺序读写位置。

参数：

- `void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。
- `off_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
ssize_t pwrite(const void* buffer, size_t size, off_t offset);
```

用途：向指定文件偏移写入，不改变共享的顺序读写位置。

参数：

- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。
- `off_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
off_t seek(off_t offset, int whence);
```

用途：按起点规则调整文件读写位置，并返回新位置。

参数：

- `off_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。
- `int whence`：定位起点：文件开头、当前位置或文件末尾。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int truncate(off_t length);
```

用途：请求修改已打开文件的长度。当前后端仅支持截断到 0，其他非负长度返回 NotSupported；非零长度的调整和扩展补零仍待文件系统后端实现。操作使用打开引用，文件改名后仍可执行。

参数：

- `off_t length`：文件修改后的目标字节数。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int flush();
```

用途：要求服务把已提交的修改写回设备，等待同步结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int refresh(const char* path = nullptr);
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：

- `const char* path = nullptr`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int adopt(uint64_t service, uint64_t handle, const char* path, int flags);
```

用途：接管已有引用，之后由本对象负责释放；接管失败时原所有者继续负责。

参数：

- `uint64_t service`：显式使用的服务端点；省略或传约定空值时由库发现服务。
- `uint64_t handle`：现有资源句柄或待转移的句柄对象。
- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。
- `int flags`：本次操作的标志位；只接受该操作声明支持的组合。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int borrow(uint64_t service, uint64_t handle, const char* path, int flags);
```

用途：临时使用已有引用，close 和析构只解除本对象的借用，不关闭原所有者的文件引用。

参数：

- `uint64_t service`：显式使用的服务端点；省略或传约定空值时由库发现服务。
- `uint64_t handle`：现有资源句柄或待转移的句柄对象。
- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。
- `int flags`：本次操作的标志位；只接受该操作声明支持的组合。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int adoptHandle(uint64_t handle, Backend backend, int flags);
```

用途：接管指定后端的已有引用，保存后端及打开标志。

参数：

- `uint64_t handle`：现有资源句柄或待转移的句柄对象。
- `Backend backend`：句柄属于文件系统、管道、终端还是共享内存，用于选择正确的读写方式。
- `int flags`：本次操作的标志位；只接受该操作声明支持的组合。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
uint64_t releaseHandle();
```

用途：交出文件引用，本对象变为空。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
int readAsync(uint64_t offset, size_t size, File::Opt& operation);
```

用途：向已打开文件提交指定范围的异步读取，File::Opt 保存请求编号、完成端点和读回的数据。提交成功不表示读取完成；poll 或 wait 成功后，用 getData 和 getTransferred 取得数据。

参数：

- `uint64_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。
- `size_t size`：本次数据的字节数。
- `File::Opt& operation`：接收异步请求编号及完成资源的 File::Opt 对象，操作完成前必须保持有效。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int writeAsync(uint64_t offset, const void* buffer, size_t size, File::Opt& operation);
```

用途：提交指定范围的异步写入，把完成端点和结果保存在 File::Opt。函数返回前已把输入数据复制到请求消息，调用者之后可以复用输入缓冲区；提交成功不表示写入完成。

参数：

- `uint64_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。
- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。
- `File::Opt& operation`：接收异步请求编号及完成资源的 File::Opt 对象，操作完成前必须保持有效。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
int getFlags() const;
```

数据：当前标志位。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

```cpp
const char* getPath() const;
```

数据：对象对应的路径。

```cpp
File::Type getType() const;
```

数据：文件种类。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint32_t getMode() const;
```

数据：文件类型和权限位。

```cpp
uint64_t getInode() const;
```

数据：文件系统内的文件编号。

```cpp
uint32_t getLinkCount() const;
```

数据：指向该文件的硬链接数量。

```cpp
uint32_t getUid() const;
```

数据：真实用户编号。

```cpp
uint32_t getGid() const;
```

数据：真实用户组编号。

```cpp
uint64_t getAccessTime() const;
```

数据：最近读取时间。

```cpp
uint64_t getModificationTime() const;
```

数据：最近修改内容的时间。

```cpp
uint64_t getChangeTime() const;
```

数据：最近修改文件属性的时间。

```cpp
uint64_t getDevice() const;
```

数据：文件所在设备号。

```cpp
uint64_t getSpecialDevice() const;
```

数据：设备文件代表的设备号。

```cpp
bool getOpened() const;
```

数据：是否已经打开。

```cpp
bool getSnapshotValid() const;
```

数据：是否已有完整属性快照。

```cpp
Backend getBackend() const;
```

数据：文件采用的读写后端。

#### setter

```cpp
int setMode(mode_t mode);
```

数据：文件权限位。

```cpp
int setOwner(uid_t uid, gid_t gid);
```

数据：文件所有者和用户组。

```cpp
int setTimes(const struct timespec times[2]);
```

数据：文件访问与修改时间。

```cpp
void setStatusFlags(int flags);
```

数据：非阻塞等打开状态标志。

### File::Opt
这个类表示一次文件异步读写请求及其完成通知。

#### 构造与析构

```cpp
Opt();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
~Opt();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Opt(File::Opt&& other) noexcept;
```

用途：将 other 的资源及释放责任转移给本对象，之后 other 为空；不得把这当作复制一份系统资源。

参数：

- `File::Opt&& other`：需要转移或复制的另一个同类对象。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

```cpp
File::Opt& operator=(File::Opt&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `File::Opt&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int poll();
```

用途：不等待地检查一次异步文件完成状态；完成后保存状态和数据，之后调用不会重复接收同一通知。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int wait(uint64_t deadline);
```

用途：等待本对象对应的文件请求完成，并保存完成状态、实际字节数和读取数据。超时或中断不释放端点，调用者可以继续等待。

参数：

- `uint64_t deadline`：本次等待的单调时钟绝对截止时间；约定的无限值表示不设截止时间。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int close();
```

用途：释放已完成操作的通知资源并清除结果；操作尚未完成时返回 Busy。析构会关闭本地完成端点，丢弃未读取的通知，但不会撤销文件服务已经收到的读写请求。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getId() const;
```

数据：编号。

```cpp
int64_t getStatus() const;
```

数据：异步文件操作的完成状态。

```cpp
uint64_t getTransferred() const;
```

数据：异步文件操作实际完成的字节数。

```cpp
bool getCompleted() const;
```

数据：异步文件操作是否已经完成。

```cpp
const void* getData() const;
```

数据：已成功完成的异步读取数据；有效长度为 getTransferred，未完成、读取失败或写操作时为 nullptr。

### Fs

这个类表示按路径执行的文件查询、删除、改名、硬链接和格式化操作。

#### 构造与析构

```cpp
Fs();
```

用途：建立空对象，首次路径操作时再查找文件系统服务。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

```cpp
~Fs();
```

用途：释放本对象查找到的文件系统服务连接，不关闭交给 File 的文件引用，不删除文件或卸载文件系统。

参数：无。

结果：无返回值。

#### 操作

```cpp
int open(const char* path, int flags, mode_t mode, File& file);
```

用途：打开指定路径；成功后对象保存访问引用，后续读写或枚举使用这个对象。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。
- `int flags`：交给文件服务的只读、只写或读写方式，以及创建、截断、追加等打开标志。
- `mode_t mode`：创建时的权限位；已有文件的权限不因再次打开而改变。
- `File& file`：文件对象；输出参数时接收打开结果或查询到的属性，输入参数时提供已打开引用。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int query(const char* path, File& file);
```

用途：查询给定路径或硬件地址，填写结果对象；查询不意味着打开、挂载或授权资源。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。
- `File& file`：接收路径及文件属性的空 File 对象；本次仅查询，不打开读写引用，getOpened 返回 false。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int removeFile(const char* path);
```

用途：删除一个文件名称；其他已打开引用按服务规则继续使用。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int rename(const char* existing, const char* target);
```

用途：把已有文件或目录的名称改为目标路径，实际替换规则由文件系统服务执行。

参数：

- `const char* existing`：已有文件或目录的路径。
- `const char* target`：操作后的目标路径；挂载时是用于接入文件系统的目录。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int link(const char* existing, const char* target);
```

用途：为已有文件创建另一个硬链接名称。

参数：

- `const char* existing`：已有文件或目录的路径。
- `const char* target`：操作后的目标路径；挂载时是用于接入文件系统的目录。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryMount(uint32_t index, Mount& mount);
```

用途：按序号读取一个已有挂载的信息；填写的 Mount 只作查询，不拥有卸载责任。

参数：

- `uint32_t index`：从 0 开始的条目或参数序号。
- `Mount& mount`：输出已有挂载的信息，不授予自动卸载责任。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int format(const char* device, const char* type);
```

用途：请求将指定设备初始化为给定文件系统格式；权限和设备占用由服务检查。

参数：

- `const char* device`：已经授权的设备对象；格式化接口中为块设备路径。
- `const char* type`：文件系统名称，例如 ext3；通知操作中则表示通知方式。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

### Mount
这个类表示一次挂载及其设备、挂载点、文件系统类型和卸载责任。

#### 构造与析构

```cpp
Mount();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
~Mount();
```

用途：仅对本对象成功创建且仍拥有的挂载尝试卸载；查询得到的挂载不卸载。析构无法报告 Busy 等错误，调用者应提前 close 并检查结果。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Mount(Mount&& other) noexcept;
```

用途：将 other 的资源及释放责任转移给本对象，之后 other 为空；不得把这当作复制一份系统资源。

参数：

- `Mount&& other`：需要转移或复制的另一个同类对象。

结果：通过 getMounted 判断挂载是否成功，通过 getError 取得错误原因；默认空对象的 getMounted 为 false。

```cpp
Mount& operator=(Mount&& other) noexcept;
```

用途：仅在本对象没有待释放连接或挂载时，接管 other 的连接、挂载信息和卸载责任。目标仍拥有资源时保留双方，并通过 getError 返回 Busy。

参数：`other` 是需要交接的挂载对象。

结果：返回本对象的引用；移动成功后 other 变为空。

#### 操作

```cpp
int close();
```

用途：卸载本对象创建且仍拥有的挂载。卸载失败时保持挂载信息和卸载责任；成功后清除拥有状态。查询得到的对象只清除本地查询结果，不卸载系统挂载。当前文件服务尚未区分所有卸载失败原因，可能返回 IoError。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int create(const char* source, const char* target, const char* type, unsigned long flags = 0, const void* data = nullptr);
```

用途：请求把 source 挂载到 target，成功后记录挂载身份、设备、路径、类型和卸载责任。

参数：

- `const char* source`：要挂载的设备路径；无设备文件系统按其类型要求传空值或约定名称。
- `const char* target`：操作后的目标路径；挂载时是用于接入文件系统的目录。
- `const char* type`：文件系统名称，例如 ext3、ext4 或 fat，用于选择对应文件系统实现。
- `unsigned long flags = 0`：挂载选项，例如只读；只能使用 fs 服务与目标文件系统支持的选项。
- `const void* data = nullptr`：文件系统专用挂载参数；不需要时传 nullptr，内容必须在create 请求完成前有效。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

#### getter

```cpp
const char* getDevice() const;
```

数据：挂载使用的设备路径。

```cpp
const char* getTarget() const;
```

数据：挂载点路径。

```cpp
const char* getType() const;
```

数据：种类。

```cpp
bool getReadOnly() const;
```

数据：是否只读。

```cpp
bool getMounted() const;
```

数据：挂载是否仍有效。

```cpp
bool getOwned() const;
```

数据：资源是否由本对象负责释放。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

## 实现文件

`lib/os/file.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
