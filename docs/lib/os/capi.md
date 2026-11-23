# C 接口

## 函数

### 文件

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osFileOpen(uint64_t fs, const char* path, int flags, uint32_t mode, uint64_t* handle)` | 文件：打开连接 | `fs`：文件系统服务端点<br>`path`：文件或目录路径<br>`flags`：操作标志位<br>`mode`：访问方式或权限位<br>`handle`：资源句柄 |
| `int osFileClose(uint64_t fs, uint64_t handle)` | 文件：关闭连接 | `fs`：文件系统服务端点<br>`handle`：资源句柄 |
| `int64_t osFileRead(uint64_t fs, uint64_t handle, void* buffer, size_t size)` | 文件：读取数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `int64_t osFileWrite(uint64_t fs, uint64_t handle, const void* buffer, size_t size)` | 文件：写入数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `int64_t osFileReadAt(uint64_t fs, uint64_t handle, void* buffer, size_t size, uint64_t offset)` | 文件：读取指定位置的数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `int64_t osFileWriteAt(uint64_t fs, uint64_t handle, const void* buffer, size_t size, uint64_t offset)` | 文件：写入指定位置的数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `int64_t osFileSeek(uint64_t fs, uint64_t handle, int64_t offset, int whence)` | 文件：设置读写位置 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`offset`：数据起始偏移量<br>`whence`：偏移量的计算起点 |
| `int osFileTruncate(uint64_t fs, uint64_t handle, uint64_t length)` | 文件：调整文件长度 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`length`：数据长度 |
| `int osFileFlush(uint64_t fs, uint64_t handle)` | 文件：刷新数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄 |
| `int osFileQuery(uint64_t fs, uint64_t handle, OsFileStat* result)` | 文件：查询属性 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`result`：接收文件属性 |
| `int osFileSetMode(uint64_t fs, uint64_t handle, uint32_t mode)` | 文件：设置权限 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`mode`：访问方式或权限位 |
| `int osFileSetOwner(uint64_t fs, uint64_t handle, uint32_t uid, uint32_t gid)` | 文件：设置所属用户与组 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`uid`：用户编号<br>`gid`：用户组编号 |
| `int osFileSetTimes(uint64_t fs, uint64_t handle, uint64_t accessTime, uint64_t modificationTime)` | 文件：设置时间属性 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`accessTime`：文件访问时间<br>`modificationTime`：文件修改时间 |
| `int osFileReadAsync(uint64_t fs, uint64_t handle, uint64_t offset, size_t size, uint64_t completionEndpoint, uint64_t requestId)` | 文件：提交异步读取 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`offset`：数据起始偏移量<br>`size`：数据大小，单位为字节<br>`completionEndpoint`：异步操作完成通知端点<br>`requestId`：异步请求编号 |
| `int osFileWriteAsync(uint64_t fs, uint64_t handle, uint64_t offset, const void* buffer, size_t size, uint64_t completionEndpoint, uint64_t requestId)` | 文件：提交异步写入 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`completionEndpoint`：异步操作完成通知端点<br>`requestId`：异步请求编号 |
| `int osFileWaitOperation(uint64_t completionEndpoint, OsFileOperationResult* result, void* buffer, size_t capacity, uint64_t deadline)` | 文件：等待异步操作完成 | `completionEndpoint`：异步操作完成通知端点<br>`result`：接收异步文件操作结果<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |

### 文件系统

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osFsQuery(const char* path, OsFileStat* result)` | 文件系统：查询属性 | `path`：文件或目录路径<br>`result`：接收文件属性 |
| `int osFsRemove(const char* path)` | 文件系统：删除对象 | `path`：文件或目录路径 |
| `int osFsRename(const char* existing, const char* target)` | 文件系统：重命名对象 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `int osFsLink(const char* existing, const char* target)` | 文件系统：创建链接 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `int osFsFormat(const char* device, const char* type)` | 文件系统：创建文件系统 | `device`：块设备路径<br>`type`：文件系统类型名称 |

### 目录

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osDirOpen(const char* path, uint64_t* handle)` | 目录：打开连接 | `path`：文件或目录路径<br>`handle`：资源句柄 |
| `int osDirClose(uint64_t handle)` | 目录：关闭连接 | `handle`：资源句柄 |
| `int osDirCreate(const char* path, uint32_t mode)` | 目录：创建对象 | `path`：文件或目录路径<br>`mode`：访问方式或权限位 |
| `int osDirRemove(const char* path)` | 目录：删除对象 | `path`：文件或目录路径 |
| `int osDirRename(const char* existing, const char* target)` | 目录：重命名对象 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `int osDirRead(uint64_t handle, uint64_t offset, OsDirectoryEntry* entries, size_t capacity, size_t* count)` | 目录：读取数据 | `handle`：资源句柄<br>`offset`：数据起始偏移量<br>`entries`：接收目录项的数组<br>`capacity`：目录项数组可容纳的项目数<br>`count`：接收实际读取的目录项数量 |

### 挂载

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osMountCreate(const char* source, const char* target, const char* type, unsigned long flags, const void* data, uint64_t* mountId)` | 挂载：创建对象 | `source`：待复制的数据<br>`target`：目标路径<br>`type`：操作或对象类型<br>`flags`：操作标志位<br>`data`：数据缓冲区<br>`mountId`：挂载编号 |
| `int osMountClose(uint64_t mountId)` | 挂载：关闭连接 | `mountId`：挂载编号 |
| `int osMountQuery(uint32_t index, OsMountStat* result)` | 挂载：查询属性 | `index`：项目索引<br>`result`：接收挂载属性 |

### 进程通信

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osIpcCreate(uint64_t* endpoint)` | 进程通信：创建对象 | `endpoint`：消息端点句柄 |
| `int osIpcSend(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline)` | 进程通信：发送消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int osIpcReceive(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline)` | 进程通信：接收消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int osIpcCall(uint64_t endpoint, OsIpcMessage* request, OsIpcMessage* reply, uint64_t deadline)` | 进程通信：发送请求并等待回复 | `endpoint`：消息端点句柄<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `int osIpcReply(uint64_t endpoint, uint64_t replyToken, OsIpcMessage* reply)` | 进程通信：回复消息 | `endpoint`：消息端点句柄<br>`replyToken`：请求回复令牌<br>`reply`：接收回复消息的对象 |
| `int osIpcHandleClose(uint64_t handle)` | 进程通信：关闭句柄 | `handle`：资源句柄 |
| `int osIpcHandleDuplicate(uint64_t handle, uint32_t rights, uint32_t flags, uint64_t* result)` | 进程通信：复制句柄 | `handle`：资源句柄<br>`rights`：句柄访问权限<br>`flags`：操作标志位<br>`result`：接收进程通信：复制句柄结果 |
| `int osIpcHandleRestrict(uint64_t handle, uint32_t rights)` | 进程通信：设置句柄权限 | `handle`：资源句柄<br>`rights`：句柄访问权限 |
| `int osIpcHandleSetFlags(uint64_t handle, uint32_t flags)` | 进程通信：设置句柄标志 | `handle`：资源句柄<br>`flags`：操作标志位 |
| `int osIpcPipeCreate(uint64_t* readHandle, uint64_t* writeHandle)` | 进程通信：创建管道 | `readHandle`：管道读取端句柄<br>`writeHandle`：管道写入端句柄 |
| `int64_t osIpcPipeRead(uint64_t readHandle, void* buffer, size_t size)` | 进程通信：读取管道 | `readHandle`：管道读取端句柄<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `int64_t osIpcPipeWrite(uint64_t writeHandle, const void* buffer, size_t size)` | 进程通信：写入管道 | `writeHandle`：管道写入端句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `int osIpcMsgQueueOpen(const char* name, int flags, uint32_t maximumMessages, uint32_t messageSize, uint64_t* handle)` | 进程通信：打开消息队列 | `name`：对象名称<br>`flags`：操作标志位<br>`maximumMessages`：队列可容纳的消息数量<br>`messageSize`：单条消息的最大字节数<br>`handle`：资源句柄 |
| `int osIpcMsgQueueUnlink(const char* name)` | 进程通信：移除消息队列名称 | `name`：对象名称 |
| `int osIpcMsgQueueSend(uint64_t handle, const void* buffer, size_t size, uint32_t priority)` | 进程通信：发送队列消息 | `handle`：资源句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`priority`：消息优先级 |
| `int64_t osIpcMsgQueueReceive(uint64_t handle, void* buffer, size_t capacity, uint32_t* priority)` | 进程通信：接收队列消息 | `handle`：资源句柄<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量<br>`priority`：消息优先级 |
| `int osIpcMsgQueueQuery(uint64_t handle, OsMessageQueueAttributes* result)` | 进程通信：查询消息队列属性 | `handle`：资源句柄<br>`result`：接收消息队列属性 |
| `int osIpcMsgQueueSetFlags(uint64_t handle, uint64_t flags, uint64_t* previous)` | 进程通信：设置消息队列标志 | `handle`：资源句柄<br>`flags`：操作标志位<br>`previous`：接收修改前的设置 |
| `int osIpcMsgQueueSetNotification(uint64_t handle, int type, uint32_t signal)` | 进程通信：设置消息到达通知 | `handle`：资源句柄<br>`type`：操作或对象类型<br>`signal`：信号编号 |

### 控制台

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int64_t osConsoleRead(void* buffer, size_t size)` | 控制台：读取数据 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `int64_t osConsoleWrite(const void* buffer, size_t size)` | 控制台：写入数据 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `int osConsoleGetId(uint32_t* id)` | 控制台：读取编号 | `id`：接收终端编号 |
| `int osConsoleSetId(uint32_t id)` | 控制台：设置编号 | `id`：终端编号 |

### 伪终端

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osPtyCreate(uint64_t* master, uint64_t* slave)` | 伪终端：创建对象 | `master`：伪终端主端句柄<br>`slave`：伪终端从端句柄 |
| `int osPtyGetNumber(uint64_t master, uint32_t* number)` | 伪终端：读取伪终端编号 | `master`：伪终端主端句柄<br>`number`：系统调用编号 |
| `int osPtySetNonblocking(uint64_t master, int enabled)` | 伪终端：设置非阻塞方式 | `master`：伪终端主端句柄<br>`enabled`：功能开关 |
| `int osPtySetForegroundGroup(uint64_t master, uint64_t group)` | 伪终端：设置前台进程组 | `master`：伪终端主端句柄<br>`group`：进程组编号 |

### 进程

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osTaskGetId(uint64_t* id)` | 进程：读取编号 | `id`：接收进程编号 |
| `int osTaskQuery(uint64_t id, OsTaskInfo* result)` | 进程：查询属性 | `id`：进程编号<br>`result`：接收进程属性 |
| `int osTaskGetParentId(uint64_t id, uint64_t* parentId)` | 进程：读取父进程编号 | `id`：进程编号<br>`parentId`：接收父进程编号 |
| `int osTaskGetGroupId(uint64_t id, uint64_t* groupId)` | 进程：读取进程组编号 | `id`：进程编号<br>`groupId`：接收进程组编号 |
| `int osTaskGetSessionId(uint64_t id, uint64_t* sessionId)` | 进程：读取会话编号 | `id`：进程编号<br>`sessionId`：接收会话编号 |
| `int osTaskExecute(const char* path, uint32_t argumentCount, const char* const* arguments, uint64_t input, uint64_t output, uint64_t error, uint64_t* handle)` | 进程：启动程序 | `path`：文件或目录路径<br>`argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组<br>`input`：标准输入句柄<br>`output`：标准输出句柄<br>`error`：标准错误句柄<br>`handle`：资源句柄 |
| `int64_t osTaskFork(void)` | 进程：派生进程 | — |
| `int osTaskReplaceImage(const OsExecImageInfo* image)` | 进程：替换执行映像 | `image`：程序映像或执行选项 |
| `int64_t osTaskWaitChild(int64_t id, int* status, int options)` | 进程：等待子进程状态变化 | `id`：进程编号<br>`status`：操作状态<br>`options`：等待选项 |
| `int osTaskSendSignal(uint64_t id, uint32_t signal)` | 进程：发送信号 | `id`：进程编号<br>`signal`：信号编号 |
| `void osTaskExit(int status)` | 进程：退出进程 | `status`：操作状态 |
| `int osTaskThreadGetId(uint64_t* id)` | 进程：读取线程编号 | `id`：接收线程编号 |
| `int osTaskThreadCreate(void (*entry)(void*), void* argument, void* stack, size_t stackSize, uint64_t* id)` | 进程：创建线程 | `entry`：线程入口函数<br>`argument`：传给线程入口的上下文<br>`stack`：线程栈或备用信号栈<br>`stackSize`：线程栈大小，单位为字节<br>`id`：接收线程编号 |
| `int osTaskThreadWait(uint64_t id, int* exitCode, int noHang)` | 进程：等待线程退出 | `id`：线程编号<br>`exitCode`：进程或线程退出码<br>`noHang`：启用立即查询退出状态 |
| `int osTaskThreadSendSignal(uint64_t id, uint32_t signal)` | 进程：向线程发送信号 | `id`：线程编号<br>`signal`：信号编号 |
| `void osTaskThreadExit(int status)` | 进程：退出线程 | `status`：操作状态 |
| `int osTaskSessionCreate(uint64_t* id)` | 进程：创建会话 | `id`：接收新建会话编号 |
| `int osTaskSetGroup(uint64_t id, uint64_t group)` | 进程：设置进程组 | `id`：进程编号<br>`group`：进程组编号 |
| `int osTaskGetNice(uint64_t id, int* value)` | 进程：读取调度优先级 | `id`：进程编号<br>`value`：调度优先级数值 |
| `int osTaskSetNice(uint64_t id, int value)` | 进程：设置调度优先级 | `id`：进程编号<br>`value`：调度优先级数值 |
| `int osTaskGetResourceLimit(uint64_t id, int resource, uint64_t* soft, uint64_t* hard)` | 进程：读取资源限制 | `id`：进程编号<br>`resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `int osTaskSetResourceLimit(uint64_t id, int resource, uint64_t soft, uint64_t hard)` | 进程：设置资源限制 | `id`：进程编号<br>`resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `int osTaskGetCredentials(uint64_t id, OsCredentialInfo* result)` | 进程：读取身份信息 | `id`：进程编号<br>`result`：接收进程身份 |
| `int osTaskSetUserIds(uint32_t real, uint32_t effective, uint32_t saved)` | 进程：设置用户身份 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `int osTaskSetGroupIds(uint32_t real, uint32_t effective, uint32_t saved)` | 进程：设置用户组身份 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `int osTaskSetGroups(const uint32_t* groups, uint32_t count)` | 进程：设置附加用户组 | `groups`：附加用户组编号数组<br>`count`：附加用户组数量 |
| `void osTaskYield(void)` | 进程：让出处理器 | — |
| `int osTaskSleep(uint64_t milliseconds)` | 进程：等待指定时间 | `milliseconds`：等待时长，单位为毫秒 |
| `int osTaskGetSignalAction(uint32_t signal, OsSignalAction* result)` | 进程：读取信号处理设置 | `signal`：信号编号<br>`result`：接收信号处理设置 |
| `int osTaskSetSignalAction(uint32_t signal, const OsSignalAction* action, OsSignalAction* previous)` | 进程：设置信号处理方式 | `signal`：信号编号<br>`action`：信号处理设置<br>`previous`：接收修改前的设置 |
| `int osTaskGetSignalMask(uint64_t* mask)` | 进程：读取信号屏蔽集合 | `mask`：信号集合 |
| `int osTaskSetSignalMask(int how, uint64_t mask, uint64_t* previous)` | 进程：设置信号屏蔽集合 | `how`：信号屏蔽集合的修改方式<br>`mask`：信号集合<br>`previous`：接收修改前的设置 |
| `int osTaskGetPendingSignals(uint64_t* mask)` | 进程：读取待处理信号 | `mask`：信号集合 |
| `int osTaskWaitSignal(uint64_t mask)` | 进程：等待信号 | `mask`：信号集合 |
| `int osTaskGetSignalStack(OsSignalStack* result)` | 进程：读取备用信号栈 | `result`：接收备用信号栈设置 |
| `int osTaskSetSignalStack(const OsSignalStack* stack, OsSignalStack* previous)` | 进程：设置备用信号栈 | `stack`：线程栈或备用信号栈<br>`previous`：接收修改前的设置 |
| `int osTaskGetCoreLimit(uint64_t* soft, uint64_t* hard)` | 进程：读取核心转储大小限制 | `soft`：资源软限制<br>`hard`：资源硬限制 |
| `int osTaskSetCoreLimit(uint64_t soft, uint64_t hard)` | 进程：设置核心转储大小限制 | `soft`：资源软限制<br>`hard`：资源硬限制 |
| `void osTaskSignalReturn(void* frame)` | 进程：恢复信号处理前的状态 | `frame`：显示缓冲区地址 |

### 内存

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osMemMapAnonymous(uint64_t address, uint64_t size, uint32_t protection, uint32_t flags, uint64_t* result)` | 内存：建立匿名映射 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`flags`：操作标志位<br>`result`：接收内存：建立匿名映射结果 |
| `int osMemMapFile(uint64_t handle, uint64_t address, uint64_t size, uint64_t offset, uint32_t protection, uint32_t flags, uint64_t* result)` | 内存：建立文件映射 | `handle`：资源句柄<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`flags`：操作标志位<br>`result`：接收内存：建立文件映射结果 |
| `int osMemMapShared(uint64_t handle, uint64_t address, uint64_t size, uint64_t offset, uint32_t protection, uint64_t* result)` | 内存：建立共享映射 | `handle`：资源句柄<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`result`：接收内存：建立共享映射结果 |
| `int osMemMapUnmap(uint64_t address, uint64_t size)` | 内存：解除映射 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `int osMemMapSetProtection(uint64_t address, uint64_t size, uint32_t protection)` | 内存：设置映射权限 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `int osMemMapSync(uint64_t address, uint64_t size, int mode)` | 内存：同步映射数据 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`mode`：访问方式或权限位 |
| `int osMemShareOpen(const char* name, int flags, uint64_t* handle)` | 内存：打开共享内存 | `name`：对象名称<br>`flags`：操作标志位<br>`handle`：资源句柄 |
| `int osMemShareUnlink(const char* name)` | 内存：移除共享内存名称 | `name`：对象名称 |
| `int osMemShareResize(uint64_t handle, uint64_t size)` | 内存：调整共享内存大小 | `handle`：资源句柄<br>`size`：数据大小，单位为字节 |
| `int osMemCheckRange(const void* address, uint64_t size, int writable)` | 内存：检查地址范围 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`writable`：允许写入 |

### 同步

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osSyncWaitAddress(volatile uint32_t* address, uint32_t expected)` | 同步：等待地址值变化 | `address`：内存地址<br>`expected`：等待地址的预期值 |
| `int osSyncWakeAddress(volatile uint32_t* address, uint32_t count)` | 同步：唤醒地址等待者 | `address`：内存地址<br>`count`：最多唤醒的线程数量 |

### 系统

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int osSysReadMonotonicTime(uint64_t* nanoseconds)` | 系统：读取单调时钟 | `nanoseconds`：接收时钟读数，单位为纳秒 |
| `int osSysReadWallClockTime(uint64_t* nanoseconds)` | 系统：读取系统时间 | `nanoseconds`：接收时钟读数，单位为纳秒 |
| `int osSysReadClockResolution(uint64_t* nanoseconds)` | 系统：读取时钟分辨率 | `nanoseconds`：接收时钟读数，单位为纳秒 |
| `int osSysConnectionLookup(const char* name, uint64_t* endpoint, uint64_t* generation)` | 系统：查询服务连接 | `name`：对象名称<br>`endpoint`：消息端点句柄<br>`generation`：检查点或就绪状态的版本号 |
