# 文件系统

## 枚举

### os::File::Type

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `None` | `0` | 空值 |
| `File` | `1` | 普通文件 |
| `Directory` | `2` | 目录 |

### os::File::Backend

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Filesystem` | `0` | 文件系统文件 |
| `Pipe` | `1` | 管道端点 |
| `Terminal` | `2` | 控制台或终端后端 |
| `SharedMemory` | `3` | 共享内存后端 |

## 对象

### os::Mount

挂载对象，保存设备、挂载点和文件系统属性。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Mount()` | 构造Mount对象 | — |
| `~Mount()` | 析构Mount对象 | — |
| `Mount(Mount&& other) noexcept` | 构造Mount对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Mount& operator=(Mount&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int create(const char* source, const char* target, const char* type, unsigned long flags = 0, const void* data = nullptr)` | 建立文件系统挂载 | `source`：待复制的数据<br>`target`：目标路径<br>`type`：操作或对象类型<br>`flags`：操作标志位<br>`data`：数据缓冲区 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const char* getDevice() const` | 读取PCI设备号 | — |
| `const char* getTarget() const` | 读取挂载点路径 | — |
| `const char* getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `bool getReadOnly() const` | 读取是否只读 | — |
| `bool getMounted() const` | 读取挂载是否仍有效 | — |
| `bool getOwned() const` | 读取资源是否由本对象负责释放 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::File

打开文件对象，持有文件连接、访问方式和位置。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `File()` | 构造File对象 | — |
| `~File()` | 析构File对象 | — |
| `File(File&& other) noexcept` | 构造File对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `File& operator=(File&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int open(const char* path, int flags, mode_t mode = 0666)` | 打开对象连接 | `path`：文件或目录路径<br>`flags`：操作标志位<br>`mode`：访问方式或权限位 |
| `int close()` | 关闭对象连接 | — |
| `int adopt(uint64_t service, uint64_t handle, const char* path, int flags)` | 接管资源句柄 | `service`：服务端点<br>`handle`：资源句柄<br>`path`：文件或目录路径<br>`flags`：操作标志位 |
| `int borrow(uint64_t service, uint64_t handle, const char* path, int flags)` | 借用资源句柄 | `service`：服务端点<br>`handle`：资源句柄<br>`path`：文件或目录路径<br>`flags`：操作标志位 |
| `int adoptHandle(uint64_t handle, Backend backend, int flags)` | 接管指定后端的已有引用 | `handle`：资源句柄<br>`backend`：文件访问后端<br>`flags`：操作标志位 |
| `uint64_t releaseHandle()` | 交出文件引用 | — |
| `ssize_t read(void* buffer, size_t size)` | 读取文件数据 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `ssize_t write(const void* buffer, size_t size)` | 写入文件数据 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `ssize_t pread(void* buffer, size_t size, off_t offset)` | 从指定文件偏移读取 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `ssize_t pwrite(const void* buffer, size_t size, off_t offset)` | 向指定文件偏移写入 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `off_t seek(off_t offset, int whence)` | 按起点规则调整文件读写位置 | `offset`：数据起始偏移量<br>`whence`：偏移量的计算起点 |
| `int truncate(off_t length)` | 请求修改已打开文件的长度 | `length`：数据长度 |
| `int flush()` | 要求服务把已提交的修改写回设备 | — |
| `int refresh(const char* path = nullptr)` | 更新对象属性 | `path`：文件或目录路径 |
| `int readAsync(uint64_t offset, size_t size, Opt& operation)` | 向已打开文件提交指定范围的异步读取 | `offset`：数据起始偏移量<br>`size`：数据大小，单位为字节<br>`operation`：文件操作对象 |
| `int writeAsync(uint64_t offset, const void* buffer, size_t size, Opt& operation)` | 提交指定范围的异步写入 | `offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`operation`：文件操作对象 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `int getFlags() const` | 读取当前标志位 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
| `const char* getPath() const` | 读取指定种类的系统或用户约定目录路径 | — |
| `Type getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint32_t getMode() const` | 读取文件类型和权限位 | — |
| `uint64_t getInode() const` | 读取文件系统内的文件编号 | — |
| `uint32_t getLinkCount() const` | 读取指向该文件的硬链接数量 | — |
| `uint32_t getUid() const` | 读取真实用户编号 | — |
| `uint32_t getGid() const` | 读取真实用户组编号 | — |
| `uint64_t getAccessTime() const` | 读取最近读取时间 | — |
| `uint64_t getModificationTime() const` | 读取最近修改内容的时间 | — |
| `uint64_t getChangeTime() const` | 读取最近修改文件属性的时间 | — |
| `uint64_t getDevice() const` | 读取PCI设备号 | — |
| `uint64_t getSpecialDevice() const` | 读取设备文件代表的设备号 | — |
| `bool getOpened() const` | 读取是否已经打开 | — |
| `bool getSnapshotValid() const` | 读取是否已有完整属性快照 | — |
| `Backend getBackend() const` | 读取文件采用的读写后端 | — |
| `size_t getMaximumAsyncTransfer() const` | 读取当前内联异步传输允许的数据字节数 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setMode(mode_t mode)` | 设置文件类型和权限位 | `mode`：访问方式或权限位 |
| `int setOwner(uid_t uid, gid_t gid)` | 设置页对象所属任务编号 | `uid`：用户编号<br>`gid`：用户组编号 |
| `int setTimes(const struct timespec times[2])` | 设置文件访问与修改时间 | `times`：文件访问时间与修改时间 |
| `void setStatusFlags(int flags)` | 设置非阻塞等打开状态标志 | `flags`：操作标志位 |

### os::File::Opt

文件打开选项对象，保存访问方式、创建属性和权限。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Opt()` | 构造Opt对象 | — |
| `~Opt()` | 析构Opt对象 | — |
| `Opt(Opt&& other) noexcept` | 构造Opt对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Opt& operator=(Opt&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int poll()` | 不等待地检查一次异步文件完成状态 | — |
| `int wait(uint64_t deadline)` | 等待本对象对应的文件请求完成 | `deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getId() const` | 读取编号 | — |
| `int64_t getStatus() const` | 读取异步文件操作的完成状态 | — |
| `uint64_t getTransferred() const` | 读取异步文件操作实际完成的字节数 | — |
| `bool getCompleted() const` | 读取异步文件操作是否已经完成 | — |
| `const void* getData() const` | 读取应用数据目录路径 | — |

### os::Fs

文件系统连接对象，提供路径操作和挂载查询。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Fs()` | 构造Fs对象 | — |
| `~Fs()` | 析构Fs对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int open(const char* path, int flags, mode_t mode, File& file)` | 打开对象连接 | `path`：文件或目录路径<br>`flags`：操作标志位<br>`mode`：访问方式或权限位<br>`file`：文件对象 |
| `int query(const char* path, File& file)` | 查询文件系统属性 | `path`：文件或目录路径<br>`file`：文件对象 |
| `int removeFile(const char* path)` | 删除一个文件名称 | `path`：文件或目录路径 |
| `int rename(const char* existing, const char* target)` | 把已有目录改为目标路径 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `int link(const char* existing, const char* target)` | 为已有文件创建另一个硬链接名称 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `int queryMount(uint32_t index, Mount& mount)` | 按序号读取一个已有挂载的信息 | `index`：项目索引<br>`mount`：挂载对象 |
| `int format(const char* device, const char* type)` | 请求将指定设备初始化为给定文件系统格式 | `device`：块设备路径<br>`type`：文件系统类型名称 |
