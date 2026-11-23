# OS 库源文件

## lib/os/capi.cc

C 接口及对象访问入口。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `CapiEncoding::rawOutput` | 转换系统调用结果 | `value`：系统调用的原始返回值<br>`result`：接收成功时返回的数值 |
| `CapiEncoding::rawCall` | 发送服务请求并接收回复 | `service`：服务端点<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `CapiEncoding::fsResult` | 读取文件系统请求结果 | `service`：服务端点<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `CapiEncoding::pathRequest` | 编码路径请求 | `request`：待发送的请求消息<br>`type`：操作或对象类型<br>`path`：文件或目录路径 |
| `CapiEncoding::fileSimple` | 提交文件句柄操作 | `fs`：文件系统服务端点<br>`type`：操作或对象类型<br>`handle`：资源句柄<br>`a`：请求的第一个数据字段<br>`b`：请求的第二个数据字段 |
| `CapiEncoding::decodeStat` | 解码文件属性 | `r`：服务回复消息<br>`out`：接收解码后的文件属性 |
| `osFileOpen` | 文件：打开连接 | `fs`：文件系统服务端点<br>`path`：文件或目录路径<br>`flags`：操作标志位<br>`mode`：访问方式或权限位<br>`handle`：资源句柄 |
| `osFileClose` | 文件：关闭连接 | `fs`：文件系统服务端点<br>`handle`：资源句柄 |
| `osFileRead` | 文件：读取数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `osFileWrite` | 文件：写入数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `osFileReadAt` | 文件：读取指定位置的数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `osFileWriteAt` | 文件：写入指定位置的数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `osFileSeek` | 文件：设置读写位置 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`offset`：数据起始偏移量<br>`whence`：偏移量的计算起点 |
| `osFileTruncate` | 文件：调整文件长度 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`length`：数据长度 |
| `osFileFlush` | 文件：刷新数据 | `fs`：文件系统服务端点<br>`handle`：资源句柄 |
| `osFileQuery` | 文件：查询属性 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`result`：接收文件属性 |
| `osFileSetMode` | 文件：设置权限 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`mode`：访问方式或权限位 |
| `osFileSetOwner` | 文件：设置所属用户与组 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`uid`：用户编号<br>`gid`：用户组编号 |
| `osFileSetTimes` | 文件：设置时间属性 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`accessTime`：文件访问时间<br>`modificationTime`：文件修改时间 |
| `osFileReadAsync` | 文件：提交异步读取 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`offset`：数据起始偏移量<br>`size`：数据大小，单位为字节<br>`completionEndpoint`：异步操作完成通知端点<br>`requestId`：异步请求编号 |
| `osFileWriteAsync` | 文件：提交异步写入 | `fs`：文件系统服务端点<br>`handle`：资源句柄<br>`offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`completionEndpoint`：异步操作完成通知端点<br>`requestId`：异步请求编号 |
| `osFileWaitOperation` | 文件：等待异步操作完成 | `completionEndpoint`：异步操作完成通知端点<br>`result`：接收异步文件操作结果<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `osFsQuery` | 文件系统：查询属性 | `path`：文件或目录路径<br>`result`：接收文件属性 |
| `osFsRemove` | 文件系统：删除对象 | `path`：文件或目录路径 |
| `osFsRename` | 文件系统：重命名对象 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `osFsLink` | 文件系统：创建链接 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `osFsFormat` | 文件系统：创建文件系统 | `device`：块设备路径<br>`type`：文件系统类型名称 |
| `osDirOpen` | 目录：打开连接 | `path`：文件或目录路径<br>`handle`：资源句柄 |
| `osDirClose` | 目录：关闭连接 | `handle`：资源句柄 |
| `osDirCreate` | 目录：创建对象 | `path`：文件或目录路径<br>`mode`：访问方式或权限位 |
| `osDirRemove` | 目录：删除对象 | `path`：文件或目录路径 |
| `osDirRename` | 目录：重命名对象 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `osDirRead` | 目录：读取数据 | `handle`：资源句柄<br>`offset`：数据起始偏移量<br>`entries`：接收目录项的数组<br>`capacity`：目录项数组可容纳的项目数<br>`count`：接收实际读取的目录项数量 |
| `osMountCreate` | 挂载：创建对象 | `source`：待复制的数据<br>`target`：目标路径<br>`type`：操作或对象类型<br>`flags`：操作标志位<br>`data`：数据缓冲区<br>`mountId`：挂载编号 |
| `osMountClose` | 挂载：关闭连接 | `mountId`：挂载编号 |
| `osMountQuery` | 挂载：查询属性 | `index`：项目索引<br>`result`：接收挂载属性 |
| `osIpcCreate` | 进程通信：创建对象 | `endpoint`：消息端点句柄 |
| `osIpcSend` | 进程通信：发送消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `osIpcReceive` | 进程通信：接收消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `osIpcCall` | 进程通信：发送请求并等待回复 | `endpoint`：消息端点句柄<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `osIpcReply` | 进程通信：回复消息 | `endpoint`：消息端点句柄<br>`replyToken`：请求回复令牌<br>`reply`：接收回复消息的对象 |
| `osIpcHandleClose` | 进程通信：关闭句柄 | `handle`：资源句柄 |
| `osIpcHandleDuplicate` | 进程通信：复制句柄 | `handle`：资源句柄<br>`rights`：句柄访问权限<br>`flags`：操作标志位<br>`result`：接收进程通信：复制句柄结果 |
| `osIpcHandleRestrict` | 进程通信：设置句柄权限 | `handle`：资源句柄<br>`rights`：句柄访问权限 |
| `osIpcHandleSetFlags` | 进程通信：设置句柄标志 | `handle`：资源句柄<br>`flags`：操作标志位 |
| `osIpcPipeCreate` | 进程通信：创建管道 | `readHandle`：管道读取端句柄<br>`writeHandle`：管道写入端句柄 |
| `osIpcPipeRead` | 进程通信：读取管道 | `readHandle`：管道读取端句柄<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `osIpcPipeWrite` | 进程通信：写入管道 | `writeHandle`：管道写入端句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `osIpcMsgQueueOpen` | 进程通信：打开消息队列 | `name`：对象名称<br>`flags`：操作标志位<br>`maximumMessages`：队列可容纳的消息数量<br>`messageSize`：单条消息的最大字节数<br>`handle`：资源句柄 |
| `osIpcMsgQueueUnlink` | 进程通信：移除消息队列名称 | `name`：对象名称 |
| `osIpcMsgQueueSend` | 进程通信：发送队列消息 | `handle`：资源句柄<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`priority`：消息优先级 |
| `osIpcMsgQueueReceive` | 进程通信：接收队列消息 | `handle`：资源句柄<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量<br>`priority`：消息优先级 |
| `osIpcMsgQueueQuery` | 进程通信：查询消息队列属性 | `handle`：资源句柄<br>`result`：接收消息队列属性 |
| `osIpcMsgQueueSetFlags` | 进程通信：设置消息队列标志 | `handle`：资源句柄<br>`flags`：操作标志位<br>`previous`：接收修改前的设置 |
| `osIpcMsgQueueSetNotification` | 进程通信：设置消息到达通知 | `handle`：资源句柄<br>`type`：操作或对象类型<br>`signal`：信号编号 |
| `osConsoleRead` | 控制台：读取数据 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `osConsoleWrite` | 控制台：写入数据 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `osConsoleGetId` | 控制台：读取编号 | `id`：接收终端编号 |
| `osConsoleSetId` | 控制台：设置编号 | `id`：终端编号 |
| `osPtyCreate` | 伪终端：创建对象 | `master`：伪终端主端句柄<br>`slave`：伪终端从端句柄 |
| `osPtyGetNumber` | 伪终端：读取伪终端编号 | `master`：伪终端主端句柄<br>`number`：系统调用编号 |
| `osPtySetNonblocking` | 伪终端：设置非阻塞方式 | `master`：伪终端主端句柄<br>`enabled`：功能开关 |
| `osPtySetForegroundGroup` | 伪终端：设置前台进程组 | `master`：伪终端主端句柄<br>`group`：进程组编号 |
| `osTaskGetId` | 进程：读取编号 | `id`：接收进程编号 |
| `osTaskQuery` | 进程：查询属性 | `id`：进程编号<br>`result`：接收进程属性 |
| `osTaskGetParentId` | 进程：读取父进程编号 | `id`：进程编号<br>`parentId`：接收父进程编号 |
| `osTaskGetGroupId` | 进程：读取进程组编号 | `id`：进程编号<br>`groupId`：接收进程组编号 |
| `osTaskGetSessionId` | 进程：读取会话编号 | `id`：进程编号<br>`sessionId`：接收会话编号 |
| `osTaskExecute` | 进程：启动程序 | `path`：文件或目录路径<br>`argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组<br>`input`：标准输入句柄<br>`output`：标准输出句柄<br>`error`：标准错误句柄<br>`handle`：资源句柄 |
| `osTaskFork` | 进程：派生进程 | — |
| `osTaskReplaceImage` | 进程：替换执行映像 | `image`：程序映像或执行选项 |
| `osTaskWaitChild` | 进程：等待子进程状态变化 | `id`：进程编号<br>`status`：操作状态<br>`options`：等待选项 |
| `osTaskSendSignal` | 进程：发送信号 | `id`：进程编号<br>`signal`：信号编号 |
| `osTaskExit` | 进程：退出进程 | `status`：操作状态 |
| `osTaskThreadGetId` | 进程：读取线程编号 | `id`：接收线程编号 |
| `osTaskThreadCreate` | 进程：创建线程 | `entry`：线程入口函数<br>`argument`：传给线程入口的上下文<br>`stack`：线程栈或备用信号栈<br>`stackSize`：线程栈大小，单位为字节<br>`id`：接收线程编号 |
| `osTaskThreadWait` | 进程：等待线程退出 | `id`：线程编号<br>`exitCode`：进程或线程退出码<br>`noHang`：启用立即查询退出状态 |
| `osTaskThreadSendSignal` | 进程：向线程发送信号 | `id`：线程编号<br>`signal`：信号编号 |
| `osTaskThreadExit` | 进程：退出线程 | `status`：操作状态 |
| `osTaskSessionCreate` | 进程：创建会话 | `id`：接收新建会话编号 |
| `osTaskSetGroup` | 进程：设置进程组 | `id`：进程编号<br>`group`：进程组编号 |
| `osTaskGetNice` | 进程：读取调度优先级 | `id`：进程编号<br>`value`：调度优先级数值 |
| `osTaskSetNice` | 进程：设置调度优先级 | `id`：进程编号<br>`value`：调度优先级数值 |
| `osTaskGetResourceLimit` | 进程：读取资源限制 | `id`：进程编号<br>`resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `osTaskSetResourceLimit` | 进程：设置资源限制 | `id`：进程编号<br>`resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `osTaskGetCredentials` | 进程：读取身份信息 | `id`：进程编号<br>`result`：接收进程身份 |
| `osTaskSetUserIds` | 进程：设置用户身份 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `osTaskSetGroupIds` | 进程：设置用户组身份 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `osTaskSetGroups` | 进程：设置附加用户组 | `groups`：附加用户组编号数组<br>`count`：附加用户组数量 |
| `osTaskYield` | 进程：让出处理器 | — |
| `osTaskSleep` | 进程：等待指定时间 | `milliseconds`：等待时长，单位为毫秒 |
| `osTaskGetSignalAction` | 进程：读取信号处理设置 | `signal`：信号编号<br>`result`：接收信号处理设置 |
| `osTaskSetSignalAction` | 进程：设置信号处理方式 | `signal`：信号编号<br>`action`：信号处理设置<br>`previous`：接收修改前的设置 |
| `osTaskGetSignalMask` | 进程：读取信号屏蔽集合 | `mask`：信号集合 |
| `osTaskSetSignalMask` | 进程：设置信号屏蔽集合 | `how`：信号屏蔽集合的修改方式<br>`mask`：信号集合<br>`previous`：接收修改前的设置 |
| `osTaskGetPendingSignals` | 进程：读取待处理信号 | `mask`：信号集合 |
| `osTaskWaitSignal` | 进程：等待信号 | `mask`：信号集合 |
| `osTaskGetSignalStack` | 进程：读取备用信号栈 | `result`：接收备用信号栈设置 |
| `osTaskSetSignalStack` | 进程：设置备用信号栈 | `stack`：线程栈或备用信号栈<br>`previous`：接收修改前的设置 |
| `osTaskGetCoreLimit` | 进程：读取核心转储大小限制 | `soft`：资源软限制<br>`hard`：资源硬限制 |
| `osTaskSetCoreLimit` | 进程：设置核心转储大小限制 | `soft`：资源软限制<br>`hard`：资源硬限制 |
| `osTaskSignalReturn` | 进程：恢复信号处理前的状态 | `frame`：显示缓冲区地址 |
| `osMemMapAnonymous` | 内存：建立匿名映射 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`flags`：操作标志位<br>`result`：接收内存：建立匿名映射结果 |
| `osMemMapFile` | 内存：建立文件映射 | `handle`：资源句柄<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`flags`：操作标志位<br>`result`：接收内存：建立文件映射结果 |
| `osMemMapShared` | 内存：建立共享映射 | `handle`：资源句柄<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`result`：接收内存：建立共享映射结果 |
| `osMemMapUnmap` | 内存：解除映射 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `osMemMapSetProtection` | 内存：设置映射权限 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `osMemMapSync` | 内存：同步映射数据 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`mode`：访问方式或权限位 |
| `osMemShareOpen` | 内存：打开共享内存 | `name`：对象名称<br>`flags`：操作标志位<br>`handle`：资源句柄 |
| `osMemShareUnlink` | 内存：移除共享内存名称 | `name`：对象名称 |
| `osMemShareResize` | 内存：调整共享内存大小 | `handle`：资源句柄<br>`size`：数据大小，单位为字节 |
| `osMemCheckRange` | 内存：检查地址范围 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`writable`：允许写入 |
| `osSyncWaitAddress` | 同步：等待地址值变化 | `address`：内存地址<br>`expected`：等待地址的预期值 |
| `osSyncWakeAddress` | 同步：唤醒地址等待者 | `address`：内存地址<br>`count`：最多唤醒的线程数量 |
| `osSysReadMonotonicTime` | 系统：读取单调时钟 | `nanoseconds`：接收时钟读数，单位为纳秒 |
| `osSysReadWallClockTime` | 系统：读取系统时间 | `nanoseconds`：接收时钟读数，单位为纳秒 |
| `osSysReadClockResolution` | 系统：读取时钟分辨率 | `nanoseconds`：接收时钟读数，单位为纳秒 |
| `osSysConnectionLookup` | 系统：查询服务连接 | `name`：对象名称<br>`endpoint`：消息端点句柄<br>`generation`：检查点或就绪状态的版本号 |

## lib/os/console.cc

控制台输入输出、转义序列和文字样式。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Console::Console` | 构造Console对象 | — |
| `os::Console::~Console` | 析构Console对象 | — |
| `os::Console::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Console::open` | 打开对象连接 | `id`：终端编号 |
| `os::Console::openCurrent` | 绑定当前进程使用的控制台 | — |
| `os::Console::close` | 关闭对象连接 | — |
| `os::Console::refresh` | 更新对象属性 | — |
| `os::Console::read` | 读取控制台输入 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Console::write` | 输出控制台文字 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Console::activate` | 把本对象绑定的控制台切换到显示前台 | — |
| `os::Console::getId` | 读取编号 | — |
| `os::Console::getOpened` | 读取是否已经打开 | — |
| `os::Console::getActive` | 读取是否是当前正在显示的控制台 | — |
| `os::Console::getEcho` | 读取输入字符是否回显 | — |
| `os::Console::getForegroundGroup` | 读取控制终端的前台进程组编号 | — |
| `os::Console::getValid` | 读取对象是否可用 | — |
| `os::Console::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Console::setEcho` | 设置输入字符是否回显 | `enabled`：功能开关 |
| `os::Console::setForegroundGroup` | 设置控制终端的前台进程组编号 | `group`：进程组编号 |
| `os::Console::EscapeParser::feed` | 解析输入的转义序列字节 | `value`：待解析的输入字节 |
| `os::Console::EscapeParser::reset` | 重置对象状态 | — |
| `os::Console::EscapeParser::getCommand` | 读取命令 | — |
| `os::Console::EscapeParser::getParameterCount` | 读取转义序列参数数量 | — |
| `os::Console::EscapeParser::getParameter` | 读取转义序列参数 | `index`：转义序列参数的索引 |
| `os::Console::Style::reset` | 重置对象状态 | — |
| `os::Console::Style::apply` | 应用终端文字样式 | `value`：编码颜色和文字属性的样式值 |
| `os::Console::Style::getForeground` | 读取前景色 | — |
| `os::Console::Style::getBackground` | 读取背景色 | — |
| `os::Console::Style::getAttributes` | 读取属性 | — |
| `os::Console::Style::setForeground` | 设置前景色 | `value`：前景色 |
| `os::Console::Style::setBackground` | 设置背景色 | `value`：背景色 |
| `os::Console::Style::setAttributes` | 设置属性 | `value`：属性 |
| `os::Console::terminalColor` | 将终端颜色索引转换为 RGB 像素颜色 | `index`：终端颜色索引<br>`bright`：使用高亮颜色 |

## lib/os/device.cc

设备连接、PCI 配置、DMA、寄存器映射、显示和中断资源。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::PciConnection::~PciConnection` | 析构PciConnection对象 | — |
| `os::PciConnection::call` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `os::Device::Pci::Pci` | 构造Pci对象 | — |
| `os::Device::Pci::decode` | 读取对象属性 | `bytes`：数据字节数 |
| `os::Device::Pci::encode` | 编码对象属性 | `bytes`：数据字节数 |
| `os::Device::Pci::enumerate` | 一次查询多个PCI功能 | `functions`：接收 PCI 功能信息的数组<br>`capacity`：接收缓冲区容量 |
| `os::Device::Pci::query` | 查询指定PCI地址 | `segment`：PCI 段号<br>`bus`：PCI 总线号<br>`device`：PCI 设备号<br>`function`：PCI 功能号<br>`result`：接收PCI 功能信息 |
| `os::Device::Pci::open` | 打开对象连接 | `segment`：PCI 段号<br>`bus`：PCI 总线号<br>`device`：PCI 设备号<br>`function`：PCI 功能号 |
| `os::Device::Pci::close` | 关闭对象连接 | — |
| `os::Device::Pci::readConfig` | 读取绑定PCI功能的配置空间寄存器 | `offset`：数据起始偏移量<br>`value`：PCI 配置寄存器数值 |
| `os::Device::Pci::writeConfig` | 向绑定PCI功能的配置空间寄存器写入数值 | `offset`：数据起始偏移量<br>`value`：PCI 配置寄存器数值 |
| `os::Device::portIo` | 提交端口输入输出请求 | `port`：硬件端口编号<br>`width`：端口访问宽度，单位为字节<br>`data`：数据缓冲区<br>`count`：端口读写次数<br>`write`：执行端口写入 |
| `os::Device::readPort` | 读取硬件端口 | `port`：硬件端口编号<br>`width`：端口访问宽度，单位为字节<br>`data`：数据缓冲区<br>`count`：端口读写次数 |
| `os::Device::writePort` | 写入硬件端口 | `port`：硬件端口编号<br>`width`：端口访问宽度，单位为字节<br>`data`：数据缓冲区<br>`count`：端口读写次数 |
| `os::Device::Device` | 构造Device对象 | — |
| `os::Device::~Device` | 析构Device对象 | — |
| `os::Device::open` | 打开对象连接 | `function`：PCI 功能对象 |
| `os::Device::stop` | 请求设备停止工作 | — |
| `os::Device::Dma::create` | 分配设备 DMA 缓冲区 | `device`：设备连接对象<br>`size`：数据大小，单位为字节<br>`alignment`：地址对齐字节数 |
| `os::Device::Mmio::open` | 打开对象连接 | `device`：设备连接对象<br>`bar`：PCI BAR 寄存器编号<br>`offset`：数据起始偏移量<br>`size`：数据大小，单位为字节 |
| `os::Device::Frame::open` | 打开对象连接 | `address`：内存地址 |
| `os::Device::Frame::refresh` | 更新对象属性 | — |
| `os::Device::Frame::decode` | 读取对象属性 | `bytes`：数据字节数 |
| `os::Device::Irq::create` | 绑定设备中断与消息端点 | `device`：设备连接对象<br>`irq`：硬件中断号<br>`endpoint`：接收中断通知的消息端点对象<br>`levelTriggered`：使用电平触发中断 |
| `os::Device::Irq::acknowledge` | 通知内核本轮设备中断已处理 | — |
| `os::Device::close` | 关闭对象连接 | — |
| `os::Device::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Device::getValid` | 读取对象是否可用 | — |
| `os::Device::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Device::Dma::Dma` | 构造Dma对象 | — |
| `os::Device::Dma::~Dma` | 析构Dma对象 | — |
| `os::Device::Dma::close` | 关闭对象连接 | — |
| `os::Device::Dma::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Device::Dma::getValid` | 读取对象是否可用 | — |
| `os::Device::Dma::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Device::Mmio::Mmio` | 构造Mmio对象 | — |
| `os::Device::Mmio::~Mmio` | 析构Mmio对象 | — |
| `os::Device::Mmio::close` | 关闭对象连接 | — |
| `os::Device::Mmio::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Device::Mmio::getValid` | 读取对象是否可用 | — |
| `os::Device::Mmio::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Device::Frame::Frame` | 构造Frame对象 | — |
| `os::Device::Frame::~Frame` | 析构Frame对象 | — |
| `os::Device::Frame::close` | 关闭对象连接 | — |
| `os::Device::Frame::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Device::Frame::getValid` | 读取对象是否可用 | — |
| `os::Device::Frame::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Device::Irq::Irq` | 构造Irq对象 | — |
| `os::Device::Irq::~Irq` | 析构Irq对象 | — |
| `os::Device::Irq::close` | 关闭对象连接 | — |
| `os::Device::Irq::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Device::Irq::getValid` | 读取对象是否可用 | — |
| `os::Device::Irq::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Device::getHandle` | 读取原始资源句柄 | — |
| `os::Device::Pci::getSegment` | 读取PCI段号 | — |
| `os::Device::Pci::getBus` | 读取PCI总线号 | — |
| `os::Device::Pci::getDevice` | 读取PCI设备号 | — |
| `os::Device::Pci::getFunction` | 读取PCI功能号 | — |
| `os::Device::Pci::getClassCode` | 读取PCI设备大类 | — |
| `os::Device::Pci::getSubclass` | 读取PCI设备子类 | — |
| `os::Device::Pci::getProgrammingInterface` | 读取PCI编程接口编号 | — |
| `os::Device::Pci::getVendorId` | 读取硬件厂商编号 | — |
| `os::Device::Pci::getDeviceId` | 读取厂商定义的设备编号 | — |
| `os::Device::Pci::getHeaderType` | 读取PCI配置头类型 | — |
| `os::Device::Dma::getHandle` | 读取原始资源句柄 | — |
| `os::Device::Dma::getVirtualAddress` | 读取CPU可访问的虚拟地址 | — |
| `os::Device::Dma::getDeviceAddress` | 读取设备进行DMA时使用的地址 | — |
| `os::Device::Dma::getSize` | 读取长度 | — |
| `os::Device::Mmio::getAddress` | 读取映射开始的虚拟地址 | — |
| `os::Device::Mmio::getSize` | 读取长度 | — |
| `os::Device::Mmio::getBarIndex` | 读取PCIBAR序号 | — |
| `os::Device::Frame::getAddress` | 读取映射开始的虚拟地址 | — |
| `os::Device::Frame::getSize` | 读取长度 | — |
| `os::Device::Frame::getWidth` | 读取帧缓冲宽度 | — |
| `os::Device::Frame::getHeight` | 读取帧缓冲高度 | — |
| `os::Device::Frame::getPitch` | 读取相邻两行像素起点之间的字节数 | — |
| `os::Device::Frame::getBitsPerPixel` | 读取每个像素占用的位数 | — |
| `os::Device::Frame::getFormat` | 读取像素中颜色通道的排列方式 | — |
| `os::Device::Frame::getRedPosition` | 读取红色通道的位位置 | — |
| `os::Device::Frame::getGreenPosition` | 读取绿色通道的位位置 | — |
| `os::Device::Frame::getBluePosition` | 读取蓝色通道的位位置 | — |
| `os::Device::Irq::getHandle` | 读取原始资源句柄 | — |
| `os::Device::Irq::getIrq` | 读取中断线路或向量编号 | — |
| `os::detail::PciQuery::PciQuery` | 构造PciQuery对象 | — |
| `os::detail::PciQuery::enumerate` | 一次查询多个PCI功能 | `functions`：接收 PCI 功能信息的数组<br>`capacity`：接收缓冲区容量 |
| `os::detail::PciQuery::read` | 读取 PCI 配置寄存器 | `function`：PCI 功能号<br>`offset`：数据起始偏移量<br>`value`：PCI 配置寄存器数值 |
| `os::detail::PciQuery::write` | 写入 PCI 配置寄存器 | `function`：PCI 功能号<br>`offset`：数据起始偏移量<br>`value`：PCI 配置寄存器数值<br>`requester`：发起请求的进程编号 |
| `os::detail::PciQuery::query` | 查询指定PCI地址 | `segment`：PCI 段号<br>`bus`：PCI 总线号<br>`device`：PCI 设备号<br>`function`：PCI 功能号<br>`result`：接收PCI 功能信息 |

## lib/os/dir.cc

目录访问和约定目录解析。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Dir::Dir` | 构造Dir对象 | — |
| `os::Dir::~Dir` | 析构Dir对象 | — |
| `os::Dir::open` | 打开对象连接 | `scope`：目录所属范围<br>`application`：应用名称 |
| `os::Dir::userId` | 读取目录所属用户编号 | `uid`：用户编号 |
| `os::Dir::validApplication` | 检查应用名称 | `application`：应用名称 |
| `os::Dir::append` | 追加一条已经定义格式的增量记录 | `base`：基础目录路径<br>`application`：应用名称<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getHome` | 读取账户主目录 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getPath` | 读取指定种类的系统或用户约定目录路径 | `kind`：服务类型<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::search` | 查找目录项 | `configuration`：查找应用配置目录<br>`index`：项目索引<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getDataSearch` | 读取指定序号的系统数据搜索目录 | `index`：项目索引<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getConfigSearch` | 读取指定序号的系统配置搜索目录 | `index`：项目索引<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getData` | 读取应用数据目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getState` | 读取系统或用户的持久状态目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getConfig` | 读取配置目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getCache` | 读取可删除缓存目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getRuntime` | 读取当前登录会话的运行目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::getLog` | 读取日志目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `os::Dir::Dir` | 构造Dir对象 | `other`：提供资源或属性的原对象 |
| `os::Dir::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Dir::service` | 取得文件系统服务连接 | — |
| `os::Dir::open` | 打开对象连接 | `path`：文件或目录路径 |
| `os::Dir::close` | 关闭对象连接 | — |
| `os::Dir::rewind` | 把已打开目录的读取位置恢复到开头 | — |
| `os::Dir::create` | 创建目录 | `path`：文件或目录路径<br>`mode`：访问方式或权限位 |
| `os::Dir::remove` | 删除指定目录 | `path`：文件或目录路径 |
| `os::Dir::rename` | 把已有目录改为目标路径 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `os::Dir::readBatch` | 一次读取多个目录项并推进位置 | `entries`：接收目录项的数组<br>`capacity`：目录项数组可容纳的项目数<br>`count`：接收实际读取的目录项数量 |
| `os::Dir::readNext` | 从已打开目录读取下一项 | `entry`：目录项或线程入口 |
| `os::Dir::getPath` | 读取指定种类的系统或用户约定目录路径 | — |
| `os::Dir::getOpened` | 读取是否已经打开 | — |
| `os::Dir::getOffset` | 读取当前偏移位置 | — |
| `os::Dir::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Dir::Entry::getName` | 读取名称 | — |
| `os::Dir::Entry::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Dir::Entry::getInode` | 读取文件系统内的文件编号 | — |
| `os::Dir::Entry::getNextOffset` | 读取继续读取目录时使用的位置 | — |

## lib/os/elf.cc

ELF 映像检查和加载信息读取。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::range` | 检查内存范围 | `offset`：数据起始偏移量<br>`length`：数据长度<br>`size`：数据大小，单位为字节 |
| `os::alignment` | 计算内存对齐量 | `value`：待检查对齐量的地址或大小 |
| `os::invalid` | 生成参数错误结果 | — |
| `os::Executable::Executable` | 构造Executable对象 | — |
| `os::Executable::~Executable` | 析构Executable对象 | — |
| `os::Executable::open` | 打开对象连接 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `os::Executable::close` | 关闭对象连接 | — |
| `os::Executable::validate` | 检查ELF标识、体系结构、各表范围和数值溢出 | — |
| `os::Executable::getValid` | 读取对象是否可用 | — |
| `os::Executable::getArchitecture` | 读取映像要求的处理器体系结构 | — |
| `os::Executable::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Executable::getEntry` | 读取程序入口虚拟地址 | — |
| `os::Executable::getProgramCount` | 读取ELF程序头数量 | — |
| `os::Executable::getProgram` | 读取指定位置的ELF程序头 | `index`：项目索引<br>`program`：接收程序段信息的对象 |
| `os::Executable::getInterpreter` | 读取ELF指定的动态解释器路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量 |

## lib/os/file.cc

文件访问、文件系统操作和挂载管理。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::bufferedRead` | 读取终端缓冲数据 | `endpoint`：消息端点句柄<br>`descriptor`：文件描述符或文件服务句柄<br>`output`：接收数据的缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `os::Mapping::~Mapping` | 析构Mapping对象 | — |
| `os::error` | 转换错误码 | `value`：待转换的服务错误码 |
| `os::result` | 发送服务请求并解释标准回复中的状态值 | `endpoint`：消息端点句柄<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `os::terminated` | 检查字符串结束字符 | `text`：待检查的文本<br>`size`：数据大小，单位为字节 |
| `os::openError` | 打开错误码 | `value`：待转换的服务错误码 |
| `os::gone` | 查询资源失效状态 | `status`：操作状态 |
| `os::closeEndpoint` | 关闭消息端点 | `endpoint`：消息端点句柄 |
| `os::pathRequest` | 编码路径请求 | `endpoint`：消息端点句柄<br>`operation`：文件操作对象<br>`path`：文件或目录路径<br>`target`：目标路径 |
| `os::File::File` | 构造File对象 | — |
| `os::File::~File` | 析构File对象 | — |
| `os::File::File` | 构造File对象 | `other`：提供资源或属性的原对象 |
| `os::File::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::File::service` | 取得文件系统服务连接 | — |
| `os::File::borrow` | 借用资源句柄 | `endpoint`：消息端点句柄<br>`handle`：资源句柄<br>`path`：文件或目录路径<br>`flags`：操作标志位 |
| `os::File::adopt` | 接管资源句柄 | `endpoint`：消息端点句柄<br>`handle`：资源句柄<br>`path`：文件或目录路径<br>`flags`：操作标志位 |
| `os::File::adoptHandle` | 接管指定后端的已有引用 | `handle`：资源句柄<br>`backend`：文件访问后端<br>`flags`：操作标志位 |
| `os::File::open` | 打开对象连接 | `path`：文件或目录路径<br>`flags`：操作标志位<br>`mode`：访问方式或权限位 |
| `os::File::close` | 关闭对象连接 | — |
| `os::File::releaseHandle` | 交出文件引用 | — |
| `os::File::read` | 读取文件数据 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `os::File::write` | 写入文件数据 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `os::File::pread` | 从指定文件偏移读取 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `os::File::pwrite` | 向指定文件偏移写入 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量 |
| `os::File::transfer` | 执行数据传输 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`writing`：执行数据写入 |
| `os::File::seek` | 按起点规则调整文件读写位置 | `offset`：数据起始偏移量<br>`whence`：偏移量的计算起点 |
| `os::File::truncate` | 请求修改已打开文件的长度 | `length`：数据长度 |
| `os::File::flush` | 要求服务把已提交的修改写回设备 | — |
| `os::File::refresh` | 更新对象属性 | `path`：文件或目录路径 |
| `os::File::setMode` | 设置文件类型和权限位 | `mode`：访问方式或权限位 |
| `os::File::setOwner` | 设置页对象所属任务编号 | `uid`：用户编号<br>`gid`：用户组编号 |
| `os::File::setTimes` | 设置文件访问与修改时间 | `times`：文件访问时间与修改时间 |
| `os::File::setStatusFlags` | 设置非阻塞等打开状态标志 | `flags`：操作标志位 |
| `os::File::getHandle` | 读取原始资源句柄 | — |
| `os::File::getFlags` | 读取当前标志位 | — |
| `os::File::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::File::getPath` | 读取指定种类的系统或用户约定目录路径 | — |
| `os::File::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::File::getSize` | 读取长度 | — |
| `os::File::getMode` | 读取文件类型和权限位 | — |
| `os::File::getInode` | 读取文件系统内的文件编号 | — |
| `os::File::getLinkCount` | 读取指向该文件的硬链接数量 | — |
| `os::File::getUid` | 读取真实用户编号 | — |
| `os::File::getGid` | 读取真实用户组编号 | — |
| `os::File::getAccessTime` | 读取最近读取时间 | — |
| `os::File::getModificationTime` | 读取最近修改内容的时间 | — |
| `os::File::getChangeTime` | 读取最近修改文件属性的时间 | — |
| `os::File::getDevice` | 读取PCI设备号 | — |
| `os::File::getSpecialDevice` | 读取设备文件代表的设备号 | — |
| `os::File::getOpened` | 读取是否已经打开 | — |
| `os::File::getSnapshotValid` | 读取是否已有完整属性快照 | — |
| `os::File::getBackend` | 读取文件采用的读写后端 | — |
| `os::File::getMaximumAsyncTransfer` | 读取当前内联异步传输允许的数据字节数 | — |
| `os::File::Opt::Opt` | 构造Opt对象 | — |
| `os::File::Opt::~Opt` | 析构Opt对象 | — |
| `os::File::Opt::Opt` | 构造Opt对象 | `other`：提供资源或属性的原对象 |
| `os::File::Opt::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::File::submit` | 提交异步文件操作 | `offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`operation`：文件操作对象<br>`writing`：执行数据写入 |
| `os::File::readAsync` | 向已打开文件提交指定范围的异步读取 | `offset`：数据起始偏移量<br>`size`：数据大小，单位为字节<br>`operation`：文件操作对象 |
| `os::File::writeAsync` | 提交指定范围的异步写入 | `offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`operation`：文件操作对象 |
| `os::File::Opt::poll` | 不等待地检查一次异步文件完成状态 | — |
| `os::File::Opt::wait` | 等待本对象对应的文件请求完成 | `deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::File::Opt::close` | 关闭对象连接 | — |
| `os::File::Opt::getId` | 读取编号 | — |
| `os::File::Opt::getStatus` | 读取异步文件操作的完成状态 | — |
| `os::File::Opt::getTransferred` | 读取异步文件操作实际完成的字节数 | — |
| `os::File::Opt::getCompleted` | 读取异步文件操作是否已经完成 | — |
| `os::File::Opt::getData` | 读取应用数据目录路径 | — |
| `os::Fs::Fs` | 构造Fs对象 | — |
| `os::Fs::~Fs` | 析构Fs对象 | — |
| `os::Fs::service` | 取得文件系统服务连接 | — |
| `os::Fs::open` | 打开对象连接 | `path`：文件或目录路径<br>`flags`：操作标志位<br>`mode`：访问方式或权限位<br>`file`：文件对象 |
| `os::Fs::query` | 查询文件系统属性 | `path`：文件或目录路径<br>`file`：文件对象 |
| `os::Fs::removeFile` | 删除一个文件名称 | `path`：文件或目录路径 |
| `os::Fs::rename` | 把已有目录改为目标路径 | `from`：原路径<br>`to`：目标路径 |
| `os::Fs::link` | 为已有文件创建另一个硬链接名称 | `from`：原路径<br>`to`：目标路径 |
| `os::Fs::format` | 请求将指定设备初始化为给定文件系统格式 | `device`：块设备路径<br>`type`：文件系统类型名称 |
| `os::Fs::queryMount` | 按序号读取一个已有挂载的信息 | `index`：项目索引<br>`mount`：挂载对象 |
| `os::Mount::Mount` | 构造Mount对象 | — |
| `os::Mount::~Mount` | 析构Mount对象 | — |
| `os::Mount::Mount` | 构造Mount对象 | `other`：提供资源或属性的原对象 |
| `os::Mount::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Mount::create` | 建立文件系统挂载 | `source`：待复制的数据<br>`target`：目标路径<br>`type`：操作或对象类型<br>`flags`：操作标志位<br>`data`：数据缓冲区 |
| `os::Mount::close` | 关闭对象连接 | — |
| `os::Mount::getDevice` | 读取PCI设备号 | — |
| `os::Mount::getTarget` | 读取挂载点路径 | — |
| `os::Mount::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Mount::getReadOnly` | 读取是否只读 | — |
| `os::Mount::getMounted` | 读取挂载是否仍有效 | — |
| `os::Mount::getOwned` | 读取资源是否由本对象负责释放 | — |
| `os::Mount::getError` | 读取最近一次申请或操作的错误码 | — |

## lib/os/input.cc

键盘与鼠标事件读取。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::inputError` | 转换输入错误码 | `value`：待转换的服务错误码 |
| `os::disconnected` | 查询连接断开状态 | `status`：操作状态 |
| `os::releaseInputEndpoint` | 释放输入服务端点 | `endpoint`：消息端点句柄 |
| `os::inputResult` | 读取输入请求结果 | `endpoint`：消息端点句柄<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `os::Input::Input` | 构造Input对象 | — |
| `os::Input::~Input` | 析构Input对象 | — |
| `os::Input::open` | 打开对象连接 | — |
| `os::Input::close` | 关闭对象连接 | — |
| `os::Input::refresh` | 更新对象属性 | — |
| `os::Input::wait` | 等待本对象对应的文件请求完成 | `deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Input::receive` | 从输入服务取出一个输入事件并填写event | `event`：接收输入事件的对象 |
| `os::Input::getConnected` | 读取是否已经连接到服务 | — |
| `os::Input::getKeyPressed` | 读取指定按键当前是否按下 | `key`：按键编号 |
| `os::Input::getModifiers` | 读取Shift、Ctrl等修饰键状态 | — |
| `os::Input::getButtons` | 读取鼠标按钮状态 | — |
| `os::Input::getValid` | 读取对象是否可用 | — |
| `os::Input::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Input::Event::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Input::Event::getTimestamp` | 读取事件发生时间 | — |
| `os::Input::Event::getKeyCode` | 读取按键编号 | — |
| `os::Input::Event::getModifiers` | 读取Shift、Ctrl等修饰键状态 | — |
| `os::Input::Event::getPressed` | 读取该按键事件是否表示按下 | — |
| `os::Input::Event::getDeltaX` | 读取鼠标水平移动量 | — |
| `os::Input::Event::getDeltaY` | 读取鼠标垂直移动量 | — |
| `os::Input::Event::getWheel` | 读取滚轮移动量 | — |
| `os::Input::Event::getButtons` | 读取鼠标按钮状态 | — |

## lib/os/ipc.cc

句柄、消息、端点、管道和消息队列。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Ipc::MsgQueue::open` | 打开对象连接 | `name`：对象名称<br>`flags`：操作标志位<br>`maximumMessages`：队列可容纳的消息数量<br>`messageSize`：单条消息的最大字节数 |
| `os::Ipc::MsgQueue::unlink` | 删除资源的名称 | `name`：对象名称 |
| `os::Ipc::MsgQueue::send` | 发送消息或向队列加入数据 | `data`：数据缓冲区<br>`size`：数据大小，单位为字节<br>`priority`：消息优先级 |
| `os::Ipc::MsgQueue::receive` | 接收队列消息 | `data`：数据缓冲区<br>`capacity`：接收缓冲区容量<br>`priority`：消息优先级 |
| `os::Ipc::MsgQueue::refresh` | 更新对象属性 | — |
| `os::Ipc::MsgQueue::setFlags` | 设置当前标志位 | `flags`：操作标志位<br>`previous`：接收修改前的设置 |
| `os::Ipc::MsgQueue::setNotification` | 设置消息队列通知方式 | `type`：操作或对象类型<br>`signal`：信号编号 |
| `os::detail::EndpointControl::create` | 创建消息端点 | — |
| `os::detail::EndpointControl::cancel` | 取消端点等待 | `endpoint`：消息端点句柄<br>`threadId`：线程编号 |
| `os::Ipc::Pipe::Pipe` | 构造Pipe对象 | — |
| `os::Ipc::Pipe::Pipe` | 构造Pipe对象 | `handle`：资源句柄 |
| `os::Ipc::Pipe::~Pipe` | 析构Pipe对象 | — |
| `os::Ipc::Pipe::Pipe` | 构造Pipe对象 | `other`：提供资源或属性的原对象 |
| `os::Ipc::Pipe::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Ipc::Pipe::create` | 创建管道读写端点 | `duplex`：启用双向管道 |
| `os::Ipc::Pipe::close` | 关闭对象连接 | — |
| `os::Ipc::Pipe::releaseReadHandle` | 交出管道读端 | — |
| `os::Ipc::Pipe::releaseWriteHandle` | 交出管道写端 | — |
| `os::Ipc::Pipe::getReadHandle` | 读取管道读端的借用句柄 | — |
| `os::Ipc::Pipe::getWriteHandle` | 读取管道写端的借用句柄 | — |
| `os::Ipc::Pipe::getValid` | 读取对象是否可用 | — |
| `os::Ipc::Pipe::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::detail::PipeClient::PipeClient` | 构造PipeClient对象 | `handle`：资源句柄 |
| `os::detail::QueueClient::QueueClient` | 构造QueueClient对象 | `handle`：资源句柄 |
| `os::Ipc::MsgQueue::MsgQueue` | 构造MsgQueue对象 | — |
| `os::Ipc::MsgQueue::MsgQueue` | 构造MsgQueue对象 | `handle`：资源句柄 |
| `os::Ipc::MsgQueue::~MsgQueue` | 析构MsgQueue对象 | — |
| `os::Ipc::MsgQueue::close` | 关闭对象连接 | — |
| `os::Ipc::MsgQueue::release` | 交出对象持有的资源 | — |
| `os::Ipc::MsgQueue::getHandle` | 读取原始资源句柄 | — |
| `os::Ipc::MsgQueue::getFlags` | 读取当前标志位 | — |
| `os::Ipc::MsgQueue::getMaximumMessages` | 读取队列最多容纳的消息数 | — |
| `os::Ipc::MsgQueue::getMessageSize` | 读取单条消息最大字节数 | — |
| `os::Ipc::MsgQueue::getCurrentMessages` | 读取队列中的消息数量 | — |
| `os::Ipc::MsgQueue::getValid` | 读取对象是否可用 | — |
| `os::Ipc::MsgQueue::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Ipc::Pipe::getIdentity` | 读取底层对象的稳定身份 | — |
| `os::detail::PipeClient::getIdentity` | 读取底层对象的稳定身份 | — |
| `os::Ipc::Pipe::queryIdentity` | 请求服务返回底层对象稳定身份 | — |
| `os::Ipc::Pipe::setNonblocking` | 设置读写是否立即返回而不等待 | `enabled`：功能开关 |
| `os::Ipc::Pipe::signalEof` | 关闭管道继续写入的能力 | — |
| `os::Ipc::Pipe::read` | 读取管道数据 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Ipc::Pipe::write` | 写入管道数据 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `os::sendMessage` | 发送端点消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::receiveMessage` | 接收端点消息 | `endpoint`：消息端点句柄<br>`message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::callService` | 调用服务端点 | `endpoint`：消息端点句柄<br>`request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::replyMessage` | 回复端点消息 | `endpoint`：消息端点句柄<br>`replyToken`：请求回复令牌<br>`reply`：接收回复消息的对象 |
| `os::closeHandle` | 关闭内核句柄 | `handle`：资源句柄 |
| `os::Ipc::Handle::Handle` | 构造Handle对象 | `value`：内核资源句柄值<br>`owned`：由对象管理句柄的释放 |
| `os::Ipc::Handle::~Handle` | 析构Handle对象 | — |
| `os::Ipc::Handle::Handle` | 构造Handle对象 | `other`：提供资源或属性的原对象 |
| `os::Ipc::Handle::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Ipc::Handle::release` | 交出对象持有的资源 | — |
| `os::Ipc::Handle::reset` | 重置对象状态 | `value`：内核资源句柄值<br>`owned`：由对象管理句柄的释放 |
| `os::Ipc::Handle::invoke` | 提交对象请求 | `operation`：文件操作对象<br>`rights`：句柄访问权限<br>`flags`：操作标志位 |
| `os::Ipc::Handle::close` | 关闭对象连接 | — |
| `os::Ipc::Handle::restrictRights` | 缩减当前引用的权限 | `rights`：句柄访问权限 |
| `os::Ipc::Handle::destroyEndpoint` | 明确请求销毁此句柄指向的端点 | — |
| `os::Ipc::Handle::requestActive` | 询问给定回复令牌对应的请求是否仍有效 | `token`：请求或资源令牌 |
| `os::Ipc::Handle::queryState` | 请求服务返回底层资源当前状态 | — |
| `os::Ipc::Handle::queryIdentity` | 请求服务返回底层对象稳定身份 | — |
| `os::Ipc::Handle::queryReferences` | 请求服务返回底层资源引用数量 | — |
| `os::Ipc::Handle::adopt` | 接管资源句柄 | `handle`：资源句柄 |
| `os::Ipc::Handle::borrow` | 借用资源句柄 | `handle`：资源句柄 |
| `os::Ipc::Handle::duplicate` | 为同一底层对象创建另一份引用 | `rights`：句柄访问权限<br>`flags`：操作标志位<br>`result`：接收资源句柄 |
| `os::Ipc::Handle::getValue` | 读取原始句柄值 | — |
| `os::Ipc::Handle::getValid` | 读取对象是否可用 | — |
| `os::Ipc::Handle::getOwned` | 读取资源是否由本对象负责释放 | — |
| `os::Ipc::Handle::setFlags` | 设置当前标志位 | `flags`：操作标志位 |
| `os::Ipc::Msg::Msg` | 构造Msg对象 | — |
| `os::Ipc::Msg::~Msg` | 析构Msg对象 | — |
| `os::Ipc::Msg::Msg` | 构造Msg对象 | `other`：提供资源或属性的原对象 |
| `os::Ipc::Msg::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Ipc::Msg::clear` | 清除消息负载和回复状态 | — |
| `os::Ipc::Msg::read` | 读取消息字段 | `offset`：数据起始偏移量<br>`buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Ipc::Msg::write` | 写入消息字段 | `offset`：数据起始偏移量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Ipc::Msg::attachHandle` | 向消息附加一个句柄 | `handle`：资源句柄<br>`transfer`：移交句柄所有权 |
| `os::Ipc::Msg::takeHandle` | 取走消息中的一个接收引用 | `index`：项目索引<br>`handle`：资源句柄 |
| `os::Ipc::Msg::sent` | 读取消息发送状态 | `success`：消息发送结果 |
| `os::Ipc::Msg::accepted` | 读取消息接收状态 | — |
| `os::Ipc::Msg::getCapacity` | 读取消息负载容量 | — |
| `os::Ipc::Msg::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Ipc::Msg::getSize` | 读取长度 | — |
| `os::Ipc::Msg::getHandleCount` | 读取消息携带的句柄数量 | — |
| `os::Ipc::Msg::getSenderId` | 读取由内核填写的发送进程编号 | — |
| `os::Ipc::Msg::getSenderThreadId` | 读取由内核填写的发送线程编号 | — |
| `os::Ipc::Msg::setType` | 设置目录项是文件、目录还是其他类型 | `type`：操作或对象类型 |
| `os::Ipc::Ipc` | 构造Ipc对象 | — |
| `os::Ipc::~Ipc` | 析构Ipc对象 | — |
| `os::Ipc::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Ipc::create` | 创建消息端点 | — |
| `os::Ipc::open` | 打开对象连接 | `handle`：资源句柄 |
| `os::Ipc::close` | 关闭对象连接 | — |
| `os::Ipc::destroy` | 明确销毁端点对象 | — |
| `os::Ipc::send` | 发送消息或向队列加入数据 | `message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Ipc::receive` | 接收端点消息 | `message`：消息内容<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Ipc::call` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Ipc::reply` | 回复收到的请求 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `os::Ipc::cancel` | 取消端点等待 | `threadId`：线程编号 |
| `os::Ipc::getHandle` | 读取原始资源句柄 | — |
| `os::Ipc::getValid` | 读取对象是否可用 | — |
| `os::Ipc::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Ipc::getInfiniteDeadline` | 读取不设置截止时间的特殊值UINT64_MAX | — |

## lib/os/mm.cc

进程堆、内存对象和地址映射。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::detail::MemoryQuery::MemoryQuery` | 构造MemoryQuery对象 | — |
| `os::RuntimeHeap::Allocation::getAddress` | 读取映射开始的虚拟地址 | — |
| `os::RuntimeHeap::Allocation::getSize` | 读取长度 | — |
| `os::RuntimeHeap::Allocation::capacity` | 读取内存块容量 | — |
| `os::RuntimeHeap::Allocation::cached` | 查询内存块缓存状态 | — |
| `os::RuntimeHeap::Allocation::cache` | 缓存空闲内存块 | — |
| `os::RuntimeHeap::Allocation::activate` | 把本对象绑定的控制台切换到显示前台 | — |
| `os::RuntimeHeap::Allocation::discardCache` | 释放缓存内存块 | — |
| `os::RuntimeHeap::Allocation::assign` | 记录分配结果 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `os::RuntimeHeap::Allocation::clear` | 清除消息负载和回复状态 | — |
| `os::RuntimeHeap::Allocation::released` | 查询内存块释放状态 | — |
| `os::RuntimeHeap::lock` | 取得互斥访问权 | — |
| `os::RuntimeHeap::unlock` | 释放互斥访问权 | — |
| `os::RuntimeHeap::forkPrepare` | 锁定进程派生所需状态 | — |
| `os::RuntimeHeap::forkParent` | 释放父进程派生锁 | — |
| `os::RuntimeHeap::forkChild` | 恢复子进程锁状态 | — |
| `os::RuntimeHeap::initialize` | 初始化堆管理数据 | — |
| `os::RuntimeHeap::allocate` | 分配堆内存 | `size`：数据大小，单位为字节 |
| `os::RuntimeHeap::free` | 释放堆内存 | `pointer`：待释放的内存地址 |
| `os::Runtime::heap` | 取得进程堆对象 | — |
| `os::Runtime::prepareHeapFork` | 准备进程堆的派生操作 | — |
| `os::Runtime::finishHeapFork` | 恢复父进程堆的访问 | — |
| `os::Runtime::resetHeapAfterFork` | 设置子进程堆状态 | — |
| `os::VirtualMemoryClient::forkPrepare` | 锁定进程派生所需状态 | — |
| `os::VirtualMemoryClient::forkParent` | 释放父进程派生锁 | — |
| `os::VirtualMemoryClient::forkChild` | 恢复子进程锁状态 | — |
| `os::VirtualMemoryClient::call` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`identity`：内存对象标识<br>`resultReply`：接收服务回复的消息 |
| `os::__osinfo_runtime_fork_prepare` | 准备运行库的进程派生状态 | — |
| `os::__osinfo_runtime_fork_parent` | 恢复父进程运行库状态 | — |
| `os::__osinfo_runtime_fork_child` | 设置子进程运行库状态 | — |
| `os::detail::MemoryControl::allocatePages` | 分配物理页 | `order`：内存页分配阶数<br>`node`：NUMA 节点编号<br>`owner`：所属进程编号 |
| `os::detail::MemoryControl::mapPages` | 映射物理页 | `handle`：资源句柄<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`target`：目标路径 |
| `os::detail::MemoryQuery::pageInfo` | 读取物理页属性 | `handle`：资源句柄<br>`info`：接收或提供操作所需的对象信息 |
| `os::detail::MemoryQuery::usage` | 读取内存使用量 | `info`：接收或提供操作所需的对象信息<br>`target`：目标路径 |
| `os::detail::MemoryControl::reclaimCaches` | 请求回收可丢弃缓存 | — |
| `os::detail::MemoryControl::unmapTarget` | 解除目标地址映射 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`target`：目标路径<br>`preserveLocks`：保留地址范围的锁定状态 |
| `os::detail::MemoryControl::protectTarget` | 设置目标映射权限 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`target`：目标路径 |
| `os::detail::MemoryControl::openSharedMemory` | 打开共享内存对象 | `name`：对象名称<br>`flags`：操作标志位 |
| `os::detail::MemoryControl::unlinkSharedMemory` | 移除共享内存名称 | `name`：对象名称 |
| `os::detail::MemoryControl::resizeSharedMemory` | 调整共享内存大小 | `handle`：资源句柄<br>`size`：数据大小，单位为字节 |
| `os::detail::MemoryControl::mapSharedMemory` | 映射共享内存 | `handle`：资源句柄<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`target`：目标路径 |
| `os::Runtime::allocateMemory` | 分配进程堆内存 | `size`：数据大小，单位为字节 |
| `os::Runtime::freeMemory` | 释放进程堆内存 | `pointer`：待释放的内存地址 |
| `os::detail::MappingClient::openTyped` | 打开指定类型的内存对象 | `name`：对象名称<br>`flags`：操作标志位<br>`policy`：核心转储策略 |
| `os::detail::MappingClient::allocateTyped` | 分配指定类型的内存对象 | `handle`：资源句柄<br>`size`：数据大小，单位为字节 |
| `os::detail::MappingClient::typedInfo` | 读取内存对象属性 | `handle`：资源句柄<br>`available`：接收可用字节数 |
| `os::detail::MappingClient::memoryOffset` | 计算映射对应的内存位置 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`contiguous`：接收连续可用的字节数<br>`rights`：句柄访问权限 |
| `os::detail::MappingClient::policy` | 读取映射策略 | `operation`：文件操作对象<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`argument`：传给线程入口的上下文 |
| `os::detail::MappingClient::anonymous` | 建立匿名内存映射 | `size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`lazy`：使用按需分配的内存映射<br>`hint`：期望的映射地址<br>`identity`：内存对象标识 |
| `os::detail::MappingClient::shared` | 建立共享内存映射 | `handle`：资源句柄<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`offset`：数据起始偏移量<br>`hint`：期望的映射地址<br>`identity`：内存对象标识 |
| `os::detail::MappingClient::file` | 建立文件内存映射 | `descriptor`：文件描述符或文件服务句柄<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`offset`：数据起始偏移量<br>`hint`：期望的映射地址<br>`identity`：内存对象标识 |
| `os::detail::MappingClient::unmap` | 解除地址映射 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `os::detail::MappingClient::protect` | 设置映射访问权限 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `os::detail::MappingClient::sync` | 向内存服务提交本映射的同步请求 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `os::detail::MappingClient::openShared` | 打开命名共享内存 | `name`：对象名称<br>`flags`：操作标志位 |
| `os::detail::MappingClient::unlinkShared` | 移除共享内存名称 | `name`：对象名称 |
| `os::detail::MemoryControl::MemoryControl` | 构造MemoryControl对象 | — |
| `os::Runtime::initializeHeap` | 初始化进程堆 | — |
| `os::mapMemory` | 建立内存映射 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `os::unmapMemory` | 解除内存映射 | `address`：内存地址<br>`size`：数据大小，单位为字节 |
| `os::protectMemory` | 设置内存访问权限 | `address`：内存地址<br>`size`：数据大小，单位为字节<br>`protection`：内存访问权限 |
| `__osinfo_runtime_allocate` | 分配运行库内存 | `size`：数据大小，单位为字节 |
| `__osinfo_runtime_free` | 释放运行库内存 | `pointer`：待释放的内存地址 |
| `os::memoryError` | 转换内存操作错误码 | `value`：待转换的服务错误码 |
| `os::mappingSize` | 计算内存映射大小 | `size`：数据大小，单位为字节<br>`aligned`：接收按页对齐后的大小 |
| `os::mappingProtection` | 转换内存映射权限 | `protection`：内存访问权限 |
| `os::memoryRequest` | 构造内存服务请求 | `operation`：文件操作对象<br>`first`：起始块编号或请求的第一个字段<br>`reply`：接收回复消息的对象<br>`second`：请求的第二个数据字段 |
| `os::Mem::Mem` | 构造Mem对象 | — |
| `os::Mem::refresh` | 更新对象属性 | `task`：目标进程 |
| `os::Mem::reclaimCaches` | 请求回收可丢弃缓存 | — |
| `os::Mem::getTotal` | 读取系统可管理的内存总字节数 | — |
| `os::Mem::getFree` | 读取当前可用内存字节数 | — |
| `os::Mem::getOwned` | 读取资源是否由本对象负责释放 | — |
| `os::Mem::getGeneration` | 读取查询结果或连接的代次 | — |
| `os::Mem::getNodeCount` | 读取内存节点数量 | — |
| `os::Mem::getSnapshotValid` | 读取是否已有完整属性快照 | — |
| `os::Mem::Page::Page` | 构造Page对象 | — |
| `os::Mem::Page::~Page` | 析构Page对象 | — |
| `os::Mem::Page::create` | 分配物理内存页 | `order`：内存页分配阶数<br>`node`：NUMA 节点编号<br>`owner`：所属进程编号 |
| `os::Mem::Page::open` | 打开对象连接 | `handle`：资源句柄 |
| `os::Mem::Page::refresh` | 更新对象属性 | — |
| `os::Mem::Page::close` | 关闭对象连接 | — |
| `os::Mem::Page::getHandle` | 读取原始资源句柄 | — |
| `os::Mem::Page::getOwner` | 读取页对象所属任务编号 | — |
| `os::Mem::Page::getSize` | 读取长度 | — |
| `os::Mem::Page::getReferences` | 读取资源引用数量 | — |
| `os::Mem::Page::getNode` | 读取内存节点编号 | — |
| `os::Mem::Page::getOrder` | 读取页分配阶数 | — |
| `os::Mem::Page::getValid` | 读取对象是否可用 | — |
| `os::Mem::Page::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::detail::MappingClient::owned` | 查询内存对象持有状态 | `operation`：文件操作对象<br>`address`：内存地址<br>`size`：数据大小，单位为字节<br>`identity`：内存对象标识<br>`argument`：传给线程入口的上下文 |
| `os::Mem::Map::Map` | 构造Map对象 | — |
| `os::Mem::Map::~Map` | 析构Map对象 | — |
| `os::Mem::Map::Map` | 构造Map对象 | `other`：提供资源或属性的原对象 |
| `os::Mem::Map::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Mem::Map::bind` | 绑定对象资源 | `address`：内存地址<br>`identity`：内存对象标识<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`shared`：使用共享映射 |
| `os::Mem::Map::create` | 建立地址映射 | `size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`shared`：使用共享映射 |
| `os::Mem::Map::open` | 打开对象连接 | `file`：文件对象<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`shared`：使用共享映射 |
| `os::Mem::Map::open` | 打开对象连接 | `memory`：共享内存对象<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限 |
| `os::Mem::Map::close` | 关闭对象连接 | — |
| `os::Mem::Map::release` | 交出对象持有的资源 | — |
| `os::Mem::Map::sync` | 向内存服务提交本映射的同步请求 | `mode`：访问方式或权限位 |
| `os::Mem::Map::setProtection` | 设置读、写、执行保护位 | `protection`：内存访问权限 |
| `os::Mem::Map::getAddress` | 读取映射开始的虚拟地址 | — |
| `os::Mem::Map::getSize` | 读取长度 | — |
| `os::Mem::Map::getProtection` | 读取读、写、执行保护位 | — |
| `os::Mem::Map::getOffset` | 读取当前偏移位置 | — |
| `os::Mem::Map::getShared` | 读取是否与其他映射共享内容 | — |
| `os::Mem::Map::getMapped` | 读取当前是否持有有效映射 | — |
| `os::Mem::Map::getValid` | 读取对象是否可用 | — |
| `os::Mem::Map::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Mem::Share::Share` | 构造Share对象 | — |
| `os::Mem::Share::~Share` | 析构Share对象 | — |
| `os::Mem::Share::Share` | 构造Share对象 | `other`：提供资源或属性的原对象 |
| `os::Mem::Share::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Mem::Share::open` | 打开对象连接 | `name`：对象名称<br>`flags`：操作标志位 |
| `os::Mem::Share::close` | 关闭对象连接 | — |
| `os::Mem::Share::refresh` | 更新对象属性 | — |
| `os::Mem::Share::resize` | 改变共享内存对象长度 | `size`：数据大小，单位为字节 |
| `os::Mem::Share::unlink` | 删除资源的名称 | — |
| `os::Mem::Share::getName` | 读取名称 | — |
| `os::Mem::Share::getSize` | 读取长度 | — |
| `os::Mem::Share::getHandle` | 读取原始资源句柄 | — |
| `os::Mem::Share::getOpened` | 读取是否已经打开 | — |
| `os::Mem::Share::getValid` | 读取对象是否可用 | — |
| `os::Mem::Share::getError` | 读取最近一次申请或操作的错误码 | — |

## lib/os/pty.cc

伪终端连接及终端设置。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Pty::Pty` | 构造Pty对象 | — |
| `os::Pty::Pty` | 构造Pty对象 | `handle`：资源句柄 |
| `os::Pty::~Pty` | 析构Pty对象 | — |
| `os::Pty::Pty` | 构造Pty对象 | `other`：提供资源或属性的原对象 |
| `os::Pty::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Pty::request` | 提交伪终端请求 | `operation`：文件操作对象<br>`input`：待发送的数据缓冲区<br>`inputSize`：输入数据字节数<br>`output`：接收数据的缓冲区<br>`outputSize`：输出缓冲区字节数 |
| `os::Pty::create` | 创建伪终端主从通道 | — |
| `os::Pty::getMasterHandle` | 读取PTY主端的借用句柄 | — |
| `os::Pty::getSlaveHandle` | 读取PTY从端的借用句柄 | — |
| `os::Pty::refreshNumber` | 更新伪终端编号 | — |
| `os::Pty::getCanonicalMode` | 读取是否按行提交输入 | — |
| `os::Pty::getEcho` | 读取输入字符是否回显 | — |
| `os::Pty::getSignalMode` | 读取是否把终端控制字符转换成信号 | — |
| `os::Pty::getEraseCharacter` | 读取删除前一个输入字符的控制字节 | — |
| `os::Pty::getInterruptCharacter` | 读取产生中断信号的控制字节 | — |
| `os::Pty::getEofCharacter` | 读取表示输入结束的控制字节 | — |
| `os::Pty::getSuspendCharacter` | 读取产生挂起信号的控制字节 | — |
| `os::Pty::getQuitCharacter` | 读取产生退出信号的控制字节 | — |
| `os::Pty::getRows` | 读取终端字符行数 | — |
| `os::Pty::getColumns` | 读取终端字符列数 | — |
| `os::Pty::getPixelWidth` | 读取终端可选的像素宽度 | — |
| `os::Pty::getPixelHeight` | 读取终端可选的像素高度 | — |
| `os::Pty::setNonblocking` | 设置读写是否立即返回而不等待 | `enabled`：功能开关 |
| `os::Pty::setForegroundGroup` | 设置控制终端的前台进程组编号 | `group`：进程组编号 |
| `os::Pty::setCanonicalMode` | 设置是否按行提交输入 | `value`：是否按行提交输入 |
| `os::Pty::setEcho` | 设置输入字符是否回显 | `value`：输入字符是否回显 |
| `os::Pty::setSignalMode` | 设置是否把终端控制字符转换成信号 | `value`：是否把终端控制字符转换成信号 |
| `os::Pty::setEraseCharacter` | 设置删除前一个输入字符的控制字节 | `value`：删除前一个输入字符的控制字节 |
| `os::Pty::setInterruptCharacter` | 设置产生中断信号的控制字节 | `value`：产生中断信号的控制字节 |
| `os::Pty::setEofCharacter` | 设置表示输入结束的控制字节 | `value`：表示输入结束的控制字节 |
| `os::Pty::setSuspendCharacter` | 设置产生挂起信号的控制字节 | `value`：产生挂起信号的控制字节 |
| `os::Pty::setQuitCharacter` | 设置产生退出信号的控制字节 | `value`：产生退出信号的控制字节 |
| `os::Pty::setRows` | 设置终端字符行数 | `value`：终端字符行数 |
| `os::Pty::setColumns` | 设置终端字符列数 | `value`：终端字符列数 |
| `os::Pty::setPixelWidth` | 设置终端可选的像素宽度 | `value`：终端可选的像素宽度 |
| `os::Pty::setPixelHeight` | 设置终端可选的像素高度 | `value`：终端可选的像素高度 |
| `os::Pty::releaseMaster` | 交出PTY主端 | — |
| `os::Pty::releaseSlave` | 交出PTY从端 | — |
| `os::Pty::refreshAttributes` | 读取终端当前行模式、回显和控制字符 | — |
| `os::Pty::applyAttributes` | 把本地修改后的终端模式和控制字符一次提交给tty服务 | — |
| `os::Pty::refreshWindowSize` | 读取终端字符行列数和可选像素尺寸 | — |
| `os::Pty::applyWindowSize` | 把本地尺寸设置提交给tty服务 | — |
| `os::Pty::adopt` | 接管资源句柄 | `master`：伪终端主端句柄<br>`slave`：伪终端从端句柄 |
| `os::Pty::borrow` | 借用资源句柄 | `master`：伪终端主端句柄 |
| `os::Pty::open` | 打开对象连接 | `master`：伪终端主端句柄 |
| `os::Pty::close` | 关闭对象连接 | — |
| `os::Pty::getNumber` | 读取PTY编号 | — |
| `os::Pty::getValid` | 读取对象是否可用 | — |
| `os::Pty::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Pty::queryForegroundGroup` | 即时查询终端当前前台进程组 | `group`：进程组编号 |
| `os::detail::PtyClient::PtyClient` | 构造PtyClient对象 | `handle`：资源句柄 |
| `os::detail::PtyClient::getNumber` | 读取PTY编号 | `number`：系统调用编号 |

## lib/os/service/boot.cc

启动终端报告及图形显示交接。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::BootConsole::BootConsole` | 构造BootConsole对象 | `endpoint`：消息端点句柄<br>`owned`：由对象管理句柄的释放 |
| `os::BootConsole::report` | 输出启动状态 | `name`：对象名称<br>`started`：服务启动状态或启动时间<br>`error`：错误码 |
| `os::BootConsole::finish` | 完成启动终端交接 | `terminal`：目标终端编号 |
| `os::BootConsole::state` | 读取会话状态 | `booting`：接收系统启动状态<br>`preferredTerminal`：接收首选终端编号 |
| `os::DisplayHandoff::DisplayHandoff` | 构造DisplayHandoff对象 | `windowService`：窗口服务端点<br>`owned`：由对象管理句柄的释放 |
| `os::DisplayHandoff::setGraphicalActive` | 设置图形界面活动状态 | `active`：图形界面活动状态 |

## lib/os/service/checkpoint.cc

服务检查点数据与资源令牌。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Sys::Checkpoint::openSnapshot` | 打开检查点快照 | `state`：状态数据<br>`bytes`：数据字节数<br>`schema`：检查点数据格式编号 |
| `os::Sys::Checkpoint::prepare` | 设置进程启动环境 | — |
| `os::Sys::Checkpoint::commit` | 提交一份完整状态快照 | `state`：状态数据 |
| `os::Sys::Checkpoint::prepareLog` | 为下一条增量记录预留空间 | `bytes`：数据字节数 |
| `os::Sys::Checkpoint::append` | 追加一条已经定义格式的增量记录 | `record`：记录数据<br>`bytes`：数据字节数 |
| `os::Sys::Checkpoint::readLog` | 读取指定位置的已保存增量记录 | `index`：项目索引<br>`record`：记录数据<br>`bytes`：数据字节数 |
| `os::Sys::Checkpoint::put` | 把一个资源引用登记到恢复域 | `handle`：资源句柄 |
| `os::Sys::Checkpoint::get` | 读取根据保存的恢复令牌取回资源引用 | `token`：请求或资源令牌 |
| `os::Sys::Checkpoint::drop` | 撤销一个恢复令牌并释放其保存的引用 | `token`：请求或资源令牌 |
| `os::Sys::Checkpoint::next` | 按游标枚举任务或恢复域中的资源 | `cursor`：枚举位置 |
| `os::Sys::Checkpoint::getSequence` | 读取检查点当前序列号 | — |
| `os::Sys::Checkpoint::getEpoch` | 读取恢复域当前代次 | — |
| `os::Sys::Checkpoint::getRecordCount` | 读取检查点之后保存的增量记录数量 | — |
| `os::Sys::Checkpoint::queryGeneration` | 读取指定任务的代次 | `task`：目标进程<br>`value`：接收检查点版本号 |
| `os::Sys::Checkpoint::ensureStaging` | 准备检查点暂存内存 | `total`：所需总字节数 |
| `os::Sys::Checkpoint::publish` | 发布检查点 | `state`：状态数据<br>`bytes`：数据字节数<br>`append`：追加检查点记录 |
| `os::Sys::Checkpoint::checksum` | 计算检查点校验值 | `data`：数据缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Sys::Checkpoint::call` | 发送请求并等待对应回复 | `op`：服务操作编号<br>`a`：请求的第一个数据字段<br>`b`：请求的第二个数据字段<br>`c`：请求的第三个数据字段<br>`reply`：接收回复消息的对象 |
| `os::Sys::Checkpoint::Checkpoint` | 构造Checkpoint对象 | — |
| `os::Sys::Checkpoint::~Checkpoint` | 析构Checkpoint对象 | — |
| `os::Sys::Checkpoint::open` | 打开对象连接 | `state`：状态数据<br>`bytes`：数据字节数<br>`schema`：检查点数据格式编号 |
| `os::Sys::Checkpoint::close` | 关闭对象连接 | — |
| `os::Sys::Checkpoint::getValid` | 读取对象是否可用 | — |
| `os::Sys::Checkpoint::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Sys::Checkpoint::getRestored` | 读取最近一次open是否从已有检查点恢复了状态 | — |

## lib/os/service/configuration.cc

配置文本对象的构造与格式检查。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::ConfigurationText::ConfigurationText` | 构造ConfigurationText对象 | `text`：待检查的文本 |
| `os::ConfigurationText::ConfigurationText` | 构造ConfigurationText对象 | `text`：待检查的文本<br>`size`：数据大小，单位为字节 |
| `os::ConfigurationText::valid` | 检查配置文本格式 | — |
| `os::ConfigurationText::absolutePath` | 解析绝对路径 | — |

## lib/os/service/connection.cc

服务连接的建立与消息调用。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Sys::Connection::connect` | 按名称查找服务 | `name`：对象名称<br>`registry`：服务注册中心端点 |
| `os::Sys::Connection::call` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Sys::Connection::disconnect` | 释放已保存的服务引用 | — |
| `os::Sys::Connection::getConnected` | 读取是否已经连接到服务 | — |
| `os::Sys::Connection::getName` | 读取名称 | — |
| `os::Sys::Connection::getGeneration` | 读取查询结果或连接的代次 | — |

## lib/os/service/control.cc

服务管理操作和服务状态查询。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Sys::Manager::Manager` | 构造Manager对象 | `name`：对象名称 |
| `os::Sys::Manager::call` | 发送请求并等待对应回复 | `operation`：文件操作对象<br>`name`：对象名称<br>`index`：项目索引<br>`status`：操作状态 |
| `os::detail::ManagerClient::ManagerClient` | 构造ManagerClient对象 | `name`：对象名称 |
| `os::Sys::Manager::list` | 按序号读取一个由该管理者负责的服务及其当前状态 | `index`：项目索引<br>`status`：操作状态 |
| `os::Sys::Manager::query` | 查询服务状态 | `name`：对象名称<br>`status`：操作状态 |
| `os::Sys::Manager::queryResult` | 查询操作结果 | `operation`：文件操作对象<br>`name`：对象名称<br>`index`：项目索引<br>`status`：操作状态 |
| `os::Sys::Manager::update` | 更新服务管理状态 | `operation`：文件操作对象<br>`name`：对象名称 |
| `os::Sys::Manager::start` | 请求管理者启动给定服务并处理依赖 | `name`：对象名称 |
| `os::Sys::Manager::stop` | 请求设备停止工作 | `name`：对象名称 |
| `os::Sys::Manager::restart` | 请求管理者按权限和依赖规则重启指定服务 | `name`：对象名称 |
| `os::Sys::Manager::enable` | 允许管理者按配置自动启动指定服务 | `name`：对象名称 |
| `os::Sys::Manager::disable` | 取消指定服务自动启动配置 | `name`：对象名称 |
| `os::Sys::Status::getName` | 读取名称 | — |
| `os::Sys::Status::getOwner` | 读取页对象所属任务编号 | — |
| `os::Sys::Status::getExecutable` | 读取服务可执行文件路径 | — |
| `os::Sys::Status::getBlockedBy` | 读取阻止服务启动的依赖名称 | — |
| `os::Sys::Status::getTaskId` | 读取所属或对应的进程编号 | — |
| `os::Sys::Status::getState` | 读取系统或用户的持久状态目录路径 | — |
| `os::Sys::Status::getEnabled` | 读取是否允许该服务自动启动 | — |
| `os::Sys::Status::getDesired` | 读取管理者是否希望该服务运行 | — |
| `os::Sys::Status::getBuiltin` | 读取服务是否由系统内置 | — |
| `os::Sys::Status::getRestartCount` | 读取服务已重启次数 | — |
| `os::Sys::Status::getExitCode` | 读取进程或线程的退出码 | — |

## lib/os/service/identity.cc

登录会话身份查询与设置。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::SessionIdentity::SessionIdentity` | 构造SessionIdentity对象 | `task`：目标进程 |
| `os::SessionIdentity::query` | 查询目标任务的认证身份 | `identity`：内存对象标识 |
| `os::SessionIdentity::establish` | 登记登录会话身份 | `uid`：用户编号<br>`gid`：用户组编号<br>`preserveCredentials`：保留进程当前身份<br>`groupCount`：附加用户组数量<br>`groups`：附加用户组编号数组 |

## lib/os/service/lifecycle.cc

服务启动、依赖、监护和配置管理。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `services::Lifecycle::validName` | 检查服务名称 | `value`：待检查的服务名称 |
| `services::Lifecycle::number` | 解析配置中的整数 | `text`：待检查的文本<br>`result`：接收解析配置中的整数结果 |
| `services::Lifecycle::copy` | 复制配置字段 | `output`：接收数据的缓冲区<br>`capacity`：接收缓冲区容量<br>`input`：待发送的数据缓冲区 |
| `services::Lifecycle::validArguments` | 检查服务启动参数 | `text`：待检查的文本 |
| `services::Lifecycle::waitReady` | 等待服务登记就绪端点 | `unit`：服务管理记录 |
| `services::Lifecycle::find` | 按名称查找服务记录 | `name`：对象名称 |
| `services::Lifecycle::bootName` | 识别启动阶段服务名称 | `name`：对象名称 |
| `services::Lifecycle::dependencies` | 检查服务依赖状态 | `unit`：服务管理记录<br>`runtime`：检查运行阶段依赖 |
| `services::Lifecycle::start` | 请求管理者启动给定服务并处理依赖 | `unit`：服务管理记录 |
| `services::Lifecycle::failed` | 记录服务启动或退出失败 | `unit`：服务管理记录<br>`code`：进程或线程退出码 |
| `services::Lifecycle::restoresState` | 查询服务状态恢复方式 | `unit`：服务管理记录 |
| `services::Lifecycle::release` | 交出对象持有的资源 | `unit`：服务管理记录 |
| `services::Lifecycle::terminate` | 请求强制结束绑定进程 | `unit`：服务管理记录<br>`force`：要求终止目标进程 |
| `services::Lifecycle::tick` | 检查退出状态并重启到期服务 | — |
| `services::Lifecycle::persist` | 保存服务启用状态 | `unit`：服务管理记录<br>`enabled`：功能开关 |
| `services::Lifecycle::control` | 处理服务管理命令 | `operation`：文件操作对象<br>`name`：对象名称<br>`index`：项目索引<br>`status`：操作状态 |
| `services::Lifecycle::run` | 运行线程入口函数 | — |
| `services::Lifecycle::init` | 初始化服务管理对象 | `owner`：所属进程编号<br>`registry`：服务注册中心端点 |
| `services::Lifecycle::bootstrap` | 启动基础服务 | — |
| `services::Lifecycle::load` | 加载服务配置 | — |
| `services::Lifecycle::readConfiguration` | 读取服务配置文件 | — |
| `services::Lifecycle::configError` | 报告配置行错误 | `line`：配置文件行号 |
| `services::Lifecycle::supervise` | 启动服务监护线程 | — |

## lib/os/service/recovery-cache.cc

恢复令牌与已完成请求的缓存。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `services::RecoveryCapabilities::keep` | 记录本轮使用的恢复令牌 | `token`：请求或资源令牌 |
| `services::RecoveryCapabilities::encode` | 编码对象属性 | `handle`：资源句柄 |
| `services::RecoveryCapabilities::decode` | 读取对象属性 | `token`：请求或资源令牌 |
| `services::RecoveryCapabilities::collect` | 回收本轮使用范围之外的恢复令牌 | — |
| `services::RecoveryCapabilities::discardOrphans` | 回收孤立的恢复令牌 | — |
| `services::ReplayCache::begin` | 识别重试并准备请求记录 | `message`：消息内容<br>`request`：待发送的请求消息<br>`checkpoint`：服务检查点对象 |
| `services::ReplayCache::remember` | 保存请求回复 | `slot`：缓存记录位置<br>`reply`：接收回复消息的对象<br>`capabilities`：恢复令牌管理对象 |
| `services::ReplayCache::restoreReply` | 恢复已保存的回复 | `slot`：缓存记录位置<br>`checkpoint`：服务检查点对象<br>`reply`：接收回复消息的对象 |
| `services::ReplayCache::acknowledge` | 通知内核本轮设备中断已处理 | `message`：消息内容<br>`checkpoint`：服务检查点对象 |
| `services::ReplayCache::retain` | 保留回复中的资源令牌 | `capabilities`：恢复令牌管理对象 |
| `services::ReplayCache::references` | 查询记录引用的恢复令牌 | `token`：请求或资源令牌 |
| `services::ReplayCache::valid` | 检查配置文本格式 | — |
| `services::ReplayCache::digest` | 计算请求内容摘要 | `request`：待发送的请求消息 |

## lib/os/service/recovery.cc

服务恢复数据与恢复状态。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Sys::Recovery::acquire` | 向恢复服务取得本服务的恢复域和现有快照信息 | `snapshot`：检查点快照消息 |
| `os::Sys::Recovery::invoke` | 提交对象请求 | `operation`：文件操作对象<br>`a`：请求的第一个数据字段<br>`b`：请求的第二个数据字段<br>`c`：请求的第三个数据字段<br>`reply`：接收回复消息的对象 |
| `os::Sys::Recovery::prepare` | 设置进程启动环境 | `bytes`：数据字节数<br>`logRecord`：将记录写入系统日志 |
| `os::Sys::Recovery::commit` | 提交一份完整状态快照 | `handle`：资源句柄<br>`bytes`：数据字节数<br>`sequence`：记录序号<br>`reply`：接收回复消息的对象 |
| `os::Sys::Recovery::append` | 追加一条已经定义格式的增量记录 | `handle`：资源句柄<br>`bytes`：数据字节数<br>`sequence`：记录序号<br>`reply`：接收回复消息的对象 |
| `os::Sys::Recovery::readLog` | 读取指定位置的已保存增量记录 | `index`：项目索引<br>`reply`：接收回复消息的对象 |
| `os::Sys::Recovery::getEpoch` | 读取恢复域当前代次 | — |

## lib/os/service/runtime.cc

进程运行环境、环境变量、终端选择和退出处理。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Runtime::current` | 取得进程运行环境对象 | — |
| `os::Runtime::addFinalizer` | 登记退出处理函数 | `function`：程序退出时执行的函数 |
| `os::Runtime::runFinalizers` | 执行退出处理函数 | — |
| `os::Runtime::lockFinalizers` | 取得退出处理表的锁 | — |
| `os::Runtime::unlockFinalizers` | 释放退出处理表的锁 | — |
| `os::Runtime::printString` | 向进程终端输出字符串 | `string`：待输出的字符串 |
| `os::Runtime::environmentValue` | 读取环境变量值 | `name`：对象名称 |
| `os::Runtime::bindEnvironment` | 绑定进程环境变量 | `environment`：进程环境变量数组 |
| `os::Runtime::prepare` | 设置进程启动环境 | `environment`：进程环境变量数组 |
| `os::Runtime::runtimeInit` | 初始化进程运行环境 | `argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组<br>`environment`：进程环境变量数组 |
| `os::Runtime::runtimeFini` | 执行进程退出处理 | — |
| `os::Runtime::stringLength` | 计算字符串长度 | `value`：待计算长度的字符串 |
| `os::Runtime::stringsEqual` | 比较两个字符串 | `left`：参与比较的左侧字符串<br>`right`：参与比较的右侧字符串 |
| `os::Runtime::copyString` | 复制字符串 | `destination`：接收复制结果的缓冲区<br>`source`：待复制的数据<br>`capacity`：接收缓冲区容量 |
| `os::Runtime::clearMemory` | 将内存内容置零 | `destination`：接收复制结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `os::Runtime::copyBytes` | 复制字节数据 | `destination`：接收复制结果的缓冲区<br>`source`：待复制的数据<br>`size`：数据大小，单位为字节 |
| `os::Runtime::kernelBootstrapHandle` | 读取内核引导端点 | — |
| `os::Runtime::registryBootstrapHandle` | 读取服务注册表端点 | — |
| `os::getBootstrapHandle` | 读取服务注册表端点 | — |
| `osRuntimeInit` | 初始化程序运行环境 | `argc`：程序启动参数数量<br>`argv`：程序启动参数数组<br>`environment`：进程环境变量数组 |
| `osRuntimeFini` | 执行程序退出处理 | — |
| `os::Runtime::getConsoleId` | 读取进程终端编号 | — |
| `os::Runtime::setConsoleId` | 设置进程终端编号 | `id`：终端编号 |
| `os::Runtime::bindWorkingDirectory` | 绑定工作目录查询函数 | `reader`：读取当前工作目录的回调函数 |
| `os::Runtime::absolutePath` | 解析绝对路径 | `path`：文件或目录路径<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量 |

## lib/os/service/session.cc

进程组、会话和控制终端。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::GraphicalSession::GraphicalSession` | 构造GraphicalSession对象 | `endpoint`：消息端点句柄<br>`owned`：由对象管理句柄的释放 |
| `os::GraphicalSession::bind` | 绑定对象资源 | `task`：目标进程<br>`started`：服务启动状态或启动时间 |
| `os::GraphicalSession::state` | 读取会话状态 | `phase`：接收图形会话阶段<br>`controller`：接收会话控制端点 |
| `os::GraphicalSession::command` | 执行图形会话命令 | `operation`：文件操作对象<br>`window`：窗口服务端点 |

## lib/os/service/session_cleanup.cc

认证会话所属用户进程的退出清理。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::SessionCleanup::SessionCleanup` | 构造SessionCleanup对象 | `session`：认证会话编号 |
| `os::SessionCleanup::terminate` | 结束认证会话所属的用户进程 | — |

## lib/os/service.cc

系统信息、时间、日志和核心服务通信。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::Sys::Registry::Registry` | 构造Registry对象 | `endpoint`：消息端点句柄 |
| `os::Sys::Registry::putName` | 编码服务名称 | `message`：消息内容<br>`name`：对象名称 |
| `os::Sys::Registry::setDefault` | 设置后续服务查找使用的注册中心端点 | `endpoint`：消息端点句柄 |
| `os::Sys::Registry::getEndpoint` | 读取注册中心端点的借用句柄 | — |
| `os::Sys::Registry::registerRaw` | 登记服务端点 | `name`：对象名称<br>`endpoint`：消息端点句柄<br>`access`：服务访问权限<br>`generation`：检查点或就绪状态的版本号 |
| `os::Sys::Registry::lookupRaw` | 查询服务注册记录 | `name`：对象名称<br>`attempts`：服务查询尝试次数<br>`generation`：检查点或就绪状态的版本号 |
| `os::Sys::Registry::unregisterRaw` | 撤销服务端点登记 | `name`：对象名称<br>`generation`：检查点或就绪状态的版本号 |
| `os::Sys::Transport::openHandle` | 获取该传输对象对应的服务端点 | — |
| `os::Sys::Transport::callOnce` | 执行一次服务消息调用 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Sys::Transport::call` | 发送请求并等待对应回复 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象<br>`deadline`：等待截止时间，单位为单调时钟纳秒 |
| `os::Sys::Transport::result` | 发送服务请求并解释标准回复中的状态值 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `os::Sys::log` | 把文本交给系统日志 | `message`：消息内容 |
| `os::Sys::readClockResolution` | 读取系统时钟能表示的最小时间步长 | — |
| `os::detail::SystemQuery::getSystemInfo` | 读取系统信息 | `info`：接收或提供操作所需的对象信息 |
| `os::Sys::readMonotonicTime` | 读取只随运行时间前进的时钟 | — |
| `os::Sys::readWallClockTime` | 读取当前日期时间对应的系统时钟 | — |
| `os::detail::SystemQuery::SystemQuery` | 构造SystemQuery对象 | — |
| `os::Sys::getUptimeNanoseconds` | 读取系统启动后经过的纳秒数 | — |
| `os::Sys::getTotalMemoryBytes` | 读取系统物理内存总字节数 | — |
| `os::Sys::getFreeMemoryBytes` | 读取空闲物理内存字节数 | — |
| `os::Sys::getOnlineCpus` | 读取当前在线处理器数量 | — |
| `os::Sys::refresh` | 更新对象属性 | — |
| `os::Sys::Transport::Transport` | 构造Transport对象 | `kind`：服务类型 |
| `os::Sys::Sys` | 构造Sys对象 | — |
| `os::Sys::Recovery::Recovery` | 构造Recovery对象 | — |
| `os::Sys::result` | 发送服务请求并解释标准回复中的状态值 | `request`：待发送的请求消息<br>`reply`：接收回复消息的对象 |
| `os::Sys::Registry::lookup` | 按服务名称查找已登记端点 | `name`：对象名称<br>`endpoint`：接收中断通知的消息端点对象 |
| `os::Sys::Registry::registerName` | 向注册中心登记名称与端点 | `name`：对象名称<br>`endpoint`：接收中断通知的消息端点对象<br>`generation`：检查点或就绪状态的版本号 |
| `os::Sys::Registry::unregisterName` | 只撤销与给定代次相符的名称登记 | `name`：对象名称<br>`generation`：检查点或就绪状态的版本号 |
| `os::detail::RegistryClient::RegistryClient` | 构造RegistryClient对象 | `endpoint`：消息端点句柄 |
| `os::detail::RegistryClient::lookup` | 按服务名称查找已登记端点 | `name`：对象名称<br>`attempts`：服务查询尝试次数<br>`generation`：检查点或就绪状态的版本号 |
| `os::detail::RegistryClient::registerName` | 向注册中心登记名称与端点 | `name`：对象名称<br>`endpoint`：消息端点句柄<br>`access`：服务访问权限<br>`generation`：检查点或就绪状态的版本号 |
| `os::detail::RegistryClient::unregister` | 撤销服务登记 | `name`：对象名称<br>`generation`：检查点或就绪状态的版本号 |
| `os::detail::RegistryClient::endpoint` | 读取服务端点 | — |

## lib/os/storage.cc

块设备枚举、分区属性、块传输和统计。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::storageDeadline` | 计算块传输的截止时间 | — |
| `os::Storage::Part::getStartBlock` | 读取分区在父设备中的起始块 | — |
| `os::Storage::Part::getBlockCount` | 读取设备或分区包含的块数 | — |
| `os::Storage::Part::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Storage::Part::getNumber` | 读取PTY编号 | — |
| `os::Storage::Part::getUuid` | 读取分区UUID | — |
| `os::Storage::Part::setStartBlock` | 设置分区在父设备中的起始块 | `value`：分区在父设备中的起始块 |
| `os::Storage::Part::setBlockCount` | 设置设备或分区包含的块数 | `value`：设备或分区包含的块数 |
| `os::Storage::Part::setType` | 设置目录项是文件、目录还是其他类型 | `value`：目录项是文件、目录还是其他类型 |
| `os::Storage::Part::setNumber` | 设置PTY编号 | `value`：PTY编号 |
| `os::Storage::Part::setUuid` | 设置分区UUID | `value`：分区UUID |
| `os::Storage::Device::getBlockCount` | 读取设备或分区包含的块数 | — |
| `os::Storage::Device::getBlockSize` | 读取每块字节数 | — |
| `os::Storage::Device::getMaxTransferBlocks` | 读取服务一次请求允许的最大块数 | — |
| `os::Storage::Device::getType` | 读取目录项是文件、目录还是其他类型 | — |
| `os::Storage::Device::getReadOnly` | 读取是否只读 | — |
| `os::Storage::Device::getId` | 读取编号 | — |
| `os::Storage::Device::getMajor` | 读取设备主编号 | — |
| `os::Storage::Device::getMinor` | 读取设备次编号 | — |
| `os::Storage::Device::getParentId` | 读取父块设备编号 | — |
| `os::Storage::Device::getPartitionNumber` | 读取分区编号 | — |
| `os::Storage::Device::getStartBlock` | 读取分区在父设备中的起始块 | — |
| `os::Storage::Device::getName` | 读取名称 | — |
| `os::Storage::Device::getPartitionUuid` | 读取分区UUID | — |
| `os::Storage::Device::getHardwarePath` | 读取设备硬件路径 | — |
| `os::Storage::Number::Number` | 构造Number对象 | `major`：主设备号<br>`minor`：次设备号 |
| `os::Storage::Number::getValue` | 读取原始句柄值 | — |
| `os::Storage::Number::getMajor` | 读取设备主编号 | — |
| `os::Storage::Number::getMinor` | 读取设备次编号 | — |
| `os::Storage::Number::setMajor` | 设置设备主编号 | `value`：设备主编号 |
| `os::Storage::Number::setMinor` | 设置设备次编号 | `value`：设备次编号 |
| `os::Storage::Device::Device` | 构造Device对象 | — |
| `os::Storage::Device::lock` | 取得互斥访问权 | — |
| `os::Storage::Device::unlock` | 释放互斥访问权 | — |
| `os::Storage::Device::Guard::Guard` | 构造Guard对象 | `device`：接收块设备连接的对象 |
| `os::Storage::Device::Guard::~Guard` | 析构Guard对象 | — |
| `os::Storage::Device::openService` | 建立存储服务连接 | `device`：接收块设备连接的对象<br>`service`：服务端点 |
| `os::Storage::Device::refresh` | 更新对象属性 | — |
| `os::Storage::Device::read` | 读取指定范围的磁盘块 | `first`：起始块编号或请求的第一个字段<br>`count`：本次传输的磁盘块数量<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量<br>`completed`：接收实际完成的磁盘块数量 |
| `os::Storage::Device::write` | 写入指定范围的磁盘块 | `first`：起始块编号或请求的第一个字段<br>`count`：本次传输的磁盘块数量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`completed`：接收实际完成的磁盘块数量 |
| `os::Storage::Device::transfer` | 执行数据传输 | `first`：起始块编号或请求的第一个字段<br>`count`：本次传输的磁盘块数量<br>`output`：接收数据的缓冲区<br>`input`：待发送的数据缓冲区<br>`completed`：接收实际完成的磁盘块数量 |
| `os::Storage::Device::flush` | 要求服务把已提交的修改写回设备 | — |
| `os::Storage::Device::flushStatus` | 读取写缓存刷新能力 | — |
| `os::Storage::Device::statistics` | 读取块设备统计 | `result`：接收块设备统计 |
| `os::detail::StorageClient::deviceCount` | 读取块设备数量 | — |
| `os::Storage::Storage` | 构造Storage对象 | — |
| `os::Storage::refresh` | 更新对象属性 | — |
| `os::Storage::getDeviceCount` | 读取发现的块设备数量 | — |
| `os::Storage::queryDevice` | 读取给定块设备并填写绑定对象 | `id`：块设备编号<br>`device`：接收块设备连接的对象 |
| `os::Storage::nextDevice` | 从编号不小于minimumId的设备开始查找下一项 | `minimumId`：枚举的起始编号<br>`device`：接收块设备连接的对象 |
| `os::Storage::Device::~Device` | 析构Device对象 | — |
| `os::Storage::Device::open` | 打开对象连接 | `id`：块设备编号 |
| `os::Storage::Device::close` | 关闭对象连接 | — |
| `os::detail::StorageClient::open` | 打开对象连接 | `device`：接收块设备连接的对象<br>`id`：块设备编号<br>`service`：服务端点 |

## lib/os/sync.cc

自旋锁、互斥锁和地址等待。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::SpinLock::lock` | 取得互斥访问权 | — |
| `os::SpinLock::tryLock` | 尝试取得互斥访问权 | — |
| `os::SpinLock::unlock` | 释放互斥访问权 | — |
| `os::SpinLock::Guard::Guard` | 构造Guard对象 | `lock`：作用域内持有的自旋锁 |
| `os::SpinLock::Guard::~Guard` | 析构Guard对象 | — |
| `os::Mutex::Mutex` | 构造Mutex对象 | — |
| `os::Mutex::lock` | 取得互斥访问权 | — |
| `os::Mutex::tryLock` | 尝试取得互斥访问权 | — |
| `os::Mutex::unlock` | 释放互斥访问权 | — |
| `os::Mutex::Guard::Guard` | 构造Guard对象 | `mutex`：作用域内持有的互斥锁 |
| `os::Mutex::Guard::~Guard` | 析构Guard对象 | — |
| `os::AddressWait::AddressWait` | 构造AddressWait对象 | `address`：内存地址 |
| `os::AddressWait::wait` | 等待地址值变化 | `expected`：等待地址的预期值 |
| `os::AddressWait::wakeOne` | 唤醒等待同一地址的一个线程 | — |
| `os::AddressWait::wakeAll` | 唤醒等待同一地址的全部线程 | — |

## lib/os/syscall.cc

系统调用桥接及就绪通知。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::invokeSyscall` | 执行系统调用 | `number`：系统调用编号<br>`arg1`：第1个系统调用参数<br>`arg2`：第2个系统调用参数<br>`arg3`：第3个系统调用参数<br>`arg4`：第4个系统调用参数<br>`arg5`：第5个系统调用参数<br>`arg6`：第6个系统调用参数 |
| `os::notifyReadiness` | 通知就绪状态变化 | — |
| `os::waitAddress` | 等待地址 | `address`：内存地址<br>`expected`：等待地址的预期值 |
| `os::wakeAddress` | 唤醒地址等待者 | `address`：内存地址<br>`count`：最多唤醒的线程数量 |

## lib/os/task.cc

进程、线程、身份、信号和执行映像。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::detail::TaskQuery::credentials` | 读取进程身份 | `taskId`：进程编号<br>`info`：接收或提供操作所需的对象信息 |
| `os::detail::TaskQuery::threadInfo` | 读取线程信息 | `taskId`：进程编号<br>`minimumTid`：线程枚举的起始编号<br>`info`：接收或提供操作所需的对象信息 |
| `os::EndpointReference::EndpointReference` | 构造EndpointReference对象 | `handle`：资源句柄 |
| `os::EndpointReference::~EndpointReference` | 析构EndpointReference对象 | — |
| `os::Task::execute` | 启动程序 | `path`：文件或目录路径 |
| `os::Task::execute` | 启动程序 | `path`：文件或目录路径<br>`argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组 |
| `os::Task::executeWithIo` | 使用指定输入输出启动程序 | `path`：文件或目录路径<br>`argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组<br>`standardInputHandle`：标准输入句柄<br>`standardOutputHandle`：标准输出句柄<br>`standardErrorHandle`：标准错误句柄 |
| `os::Task::executeService` | 取得程序执行服务 | `path`：文件或目录路径<br>`name`：对象名称 |
| `os::Task::executeService` | 取得程序执行服务 | `path`：文件或目录路径<br>`count`：项目数量<br>`arguments`：程序启动参数数组 |
| `os::Task::executeRequest` | 构造程序执行请求 | `path`：文件或目录路径<br>`argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组<br>`standardInputHandle`：标准输入句柄<br>`standardOutputHandle`：标准输出句柄<br>`standardErrorHandle`：标准错误句柄<br>`service`：服务端点 |
| `os::Task::wait` | 等待本对象对应的文件请求完成 | `task`：目标进程<br>`exitCode`：进程或线程退出码 |
| `os::Task::kill` | 发送进程信号 | `taskId`：进程编号 |
| `os::detail::TaskQuery::query` | 查询进程属性 | `taskId`：进程编号<br>`info`：接收或提供操作所需的对象信息 |
| `os::Task::setCredentials` | 设置进程身份 | `taskId`：进程编号<br>`uid`：用户编号<br>`gid`：用户组编号<br>`effectiveOnly`：更新有效身份 |
| `os::Task::Task` | 构造Task对象 | `id`：进程编号 |
| `os::Task::id` | 读取进程编号 | — |
| `os::Task::sendSignal` | 向绑定的进程或线程发送信号 | `signal`：信号编号 |
| `os::Task::waitThread` | 等待线程退出 | `threadId`：线程编号<br>`exitCode`：进程或线程退出码<br>`noHang`：启用立即查询退出状态 |
| `os::Task::setSignalAction` | 设置指定信号的处理方式 | `signal`：信号编号<br>`action`：信号处理设置<br>`oldAction`：接收原信号处理设置 |
| `os::Task::setSignalMask` | 设置当前信号屏蔽集合 | `how`：信号屏蔽集合的修改方式<br>`mask`：信号集合<br>`oldMask`：接收原信号屏蔽集合 |
| `os::Task::getPendingSignals` | 读取待处理信号 | `mask`：信号集合 |
| `os::Task::signalStack` | 读取备用信号栈 | `stack`：线程栈或备用信号栈<br>`oldStack`：接收原备用信号栈 |
| `os::Task::coreLimit` | 读取核心转储大小限制 | `set`：执行限制值设置<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `os::detail::TaskQuery::exitInfo` | 读取进程退出信息 | `id`：进程编号<br>`info`：接收或提供操作所需的对象信息 |
| `os::Task::recordImage` | 记录执行映像信息 | `path`：文件或目录路径<br>`begin`：映像起始地址<br>`end`：映像结束地址<br>`offset`：数据起始偏移量 |
| `os::Task::accessible` | 检查进程访问权限 | `address`：内存地址<br>`bytes`：数据字节数<br>`writable`：允许写入 |
| `os::Task::recordAuxiliary` | 记录程序辅助向量 | `headers`：程序头表地址<br>`count`：项目数量<br>`entry`：目录项或线程入口<br>`loader`：动态加载器地址 |
| `os::Task::waitSignal` | 等待屏蔽集合中的一个信号 | `mask`：信号集合 |
| `os::Task::signalReturn` | 恢复信号处理前的执行状态 | `frame`：显示缓冲区地址 |
| `os::Task::sendThreadSignal` | 向指定线程发送信号 | `threadId`：线程编号<br>`signal`：信号编号 |
| `os::Task::waitChild` | 等待子进程状态变化 | `taskId`：进程编号<br>`status`：操作状态<br>`options`：等待选项 |
| `os::Task::spawnProgram` | 按程序路径创建进程 | `program`：接收程序段信息的对象<br>`bootstrapHandle`：进程启动端点<br>`argumentCount`：程序启动参数数量<br>`arguments`：程序启动参数数组 |
| `os::detail::TaskQuery::spawnImage` | 从执行映像创建进程 | `info`：接收或提供操作所需的对象信息 |
| `os::Task::getState` | 读取系统或用户的持久状态目录路径 | `taskHandle`：进程句柄<br>`exitCode`：进程或线程退出码 |
| `os::detail::TaskQuery::getInfo` | 读取属性信息 | `taskHandle`：进程句柄<br>`info`：接收或提供操作所需的对象信息 |
| `os::Task::terminate` | 请求强制结束绑定进程 | `taskHandle`：进程句柄<br>`exitCode`：进程或线程退出码 |
| `os::Task::sendSignal` | 向绑定的进程或线程发送信号 | `taskId`：进程编号<br>`signal`：信号编号 |
| `os::Task::setGroupTarget` | 设置进程组目标 | `taskGroupId`：进程组编号<br>`taskId`：进程编号 |
| `os::Task::createSession` | 创建进程会话 | — |
| `os::Task::getIdentifiers` | 读取父进程、进程组和会话编号 | `identifiers`：接收进程、进程组和会话编号<br>`taskId`：进程编号 |
| `os::Task::readId` | 读取编号 | — |
| `os::Task::getThreadId` | 读取当前线程编号 | — |
| `os::detail::TaskQuery::TaskQuery` | 构造TaskQuery对象 | — |
| `os::Task::Credential::Credential` | 构造Credential对象 | `task`：目标进程 |
| `os::Task::Credential::refresh` | 更新对象属性 | — |
| `os::Task::Credential::getUid` | 读取真实用户编号 | — |
| `os::Task::Credential::getEffectiveUid` | 读取实际用于权限检查的用户编号 | — |
| `os::Task::Credential::getSavedUid` | 读取保存的用户编号 | — |
| `os::Task::Credential::getGid` | 读取真实用户组编号 | — |
| `os::Task::Credential::getEffectiveGid` | 读取实际用于权限检查的用户组编号 | — |
| `os::Task::Credential::getSavedGid` | 读取保存的用户组编号 | — |
| `os::Task::Credential::getGroupCount` | 读取补充组数量 | — |
| `os::Task::Credential::getGroup` | 读取指定位置的补充组编号 | `index`：项目索引 |
| `os::Task::getState` | 读取系统或用户的持久状态目录路径 | — |
| `os::Task::getExitCode` | 读取进程或线程的退出码 | — |
| `os::Task::getName` | 读取名称 | — |
| `os::Task::getUid` | 读取真实用户编号 | — |
| `os::Task::getGid` | 读取真实用户组编号 | — |
| `os::Task::getEffectiveUid` | 读取实际用于权限检查的用户编号 | — |
| `os::Task::getEffectiveGid` | 读取实际用于权限检查的用户组编号 | — |
| `os::Task::getParentId` | 读取父块设备编号 | — |
| `os::Task::getTrustedDeviceService` | 读取目标是否具有受控设备服务身份 | — |
| `os::Task::getServiceRole` | 读取目标的系统服务角色 | — |
| `os::Task::getGroupId` | 读取进程组编号 | — |
| `os::Task::getSessionId` | 读取会话编号 | — |
| `os::Task::getTerminalForegroundGroup` | 读取控制终端的前台进程组编号 | — |
| `os::Task::getStartedNanoseconds` | 读取启动时间 | — |
| `os::Task::getCpuNanoseconds` | 读取已消耗的CPU时间 | — |
| `os::Task::getTerminalKind` | 读取控制终端的种类 | — |
| `os::Task::getTerminalIndex` | 读取控制终端的编号 | — |
| `os::Task::getCommandLineSize` | 读取命令行有效字节数 | — |
| `os::Task::getManagedService` | 读取是否是init管理的服务 | — |
| `os::Task::getCommandLine` | 读取启动命令行 | — |
| `os::Task::getMainThreadId` | 读取主线程编号 | — |
| `os::Task::getNice` | 读取调度优先级调整值 | — |
| `os::Task::getThreadCount` | 读取当前线程数量 | — |
| `os::Task::Thread::getId` | 读取编号 | — |
| `os::Task::Thread::getTaskId` | 读取所属或对应的进程编号 | — |
| `os::Task::Thread::getCpuNanoseconds` | 读取已消耗的CPU时间 | — |
| `os::Task::Thread::getNice` | 读取调度优先级调整值 | — |
| `os::Task::Thread::getState` | 读取系统或用户的持久状态目录路径 | — |
| `os::Task::decode` | 读取对象属性 | `bytes`：数据字节数 |
| `os::Task::refreshTarget` | 更新进程访问端点 | `id`：进程编号 |
| `os::Task::enumerate` | 一次查询多个PCI功能 | `minimumId`：枚举的起始编号 |
| `os::Task::refreshHandle` | 更新资源句柄 | `handle`：资源句柄 |
| `os::Task::Thread::refresh` | 更新对象属性 | — |
| `os::Task::Task` | 构造Task对象 | — |
| `os::Task::Image::getImage` | 读取映像字节的借用地址 | — |
| `os::Task::Image::getImageSize` | 读取映像字节数 | — |
| `os::Task::Image::getName` | 读取名称 | — |
| `os::Task::Image::setImage` | 设置映像字节的借用地址 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `os::Task::Image::setName` | 设置名称 | `value`：名称 |
| `os::Task::Spawn::getImage` | 读取映像字节的借用地址 | — |
| `os::Task::Spawn::getImageSize` | 读取映像字节数 | — |
| `os::Task::Spawn::getName` | 读取名称 | — |
| `os::Task::Spawn::getBootstrapHandle` | 读取服务注册表端点 | — |
| `os::Task::Spawn::getStandardInputHandle` | 读取新程序标准输入句柄 | — |
| `os::Task::Spawn::getStandardOutputHandle` | 读取新程序标准输出句柄 | — |
| `os::Task::Spawn::getStandardErrorHandle` | 读取新程序标准错误句柄 | — |
| `os::Task::Spawn::getCapabilities` | 读取请求授予新程序的能力位 | — |
| `os::Task::Spawn::getParentTaskId` | 读取受控启动请求中的父任务编号 | — |
| `os::Task::Spawn::getArgumentCount` | 读取程序参数数量 | — |
| `os::Task::Spawn::getServiceRole` | 读取目标的系统服务角色 | — |
| `os::Task::Spawn::getArguments` | 读取程序参数字符串数组 | — |
| `os::Task::Spawn::getSharedImageCount` | 读取共享映像数量 | — |
| `os::Task::Spawn::getFlags` | 读取当前标志位 | — |
| `os::Task::Spawn::getTerminalKind` | 读取控制终端的种类 | — |
| `os::Task::Spawn::getTerminalIndex` | 读取控制终端的编号 | — |
| `os::Task::Spawn::getSharedImages` | 读取共享映像数组 | — |
| `os::Task::Spawn::setImage` | 设置映像字节的借用地址 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `os::Task::Spawn::setName` | 设置名称 | `value`：名称 |
| `os::Task::Spawn::setBootstrapHandle` | 设置服务注册表端点 | `value`：服务注册表端点 |
| `os::Task::Spawn::setStandardInputHandle` | 设置新程序标准输入句柄 | `value`：新程序标准输入句柄 |
| `os::Task::Spawn::setStandardOutputHandle` | 设置新程序标准输出句柄 | `value`：新程序标准输出句柄 |
| `os::Task::Spawn::setStandardErrorHandle` | 设置新程序标准错误句柄 | `value`：新程序标准错误句柄 |
| `os::Task::Spawn::setCapabilities` | 设置请求授予新程序的能力位 | `value`：请求授予新程序的能力位 |
| `os::Task::Spawn::setParentTaskId` | 设置受控启动请求中的父任务编号 | `value`：受控启动请求中的父任务编号 |
| `os::Task::Spawn::setServiceRole` | 设置目标的系统服务角色 | `value`：目标的系统服务角色 |
| `os::Task::Spawn::setArguments` | 设置程序参数字符串数组 | `value`：程序参数字符串数组<br>`count`：程序启动参数数量 |
| `os::Task::Spawn::setFlags` | 设置当前标志位 | `value`：当前标志位 |
| `os::Task::Spawn::setTerminalKind` | 设置控制终端的种类 | `value`：控制终端的种类 |
| `os::Task::Spawn::setTerminalIndex` | 设置控制终端的编号 | `value`：控制终端的编号 |
| `os::Task::Spawn::setSharedImages` | 设置共享映像数组 | `value`：共享映像数组<br>`count`：共享映像数量 |
| `os::Task::Exec::getImage` | 读取映像字节的借用地址 | — |
| `os::Task::Exec::getImageSize` | 读取映像字节数 | — |
| `os::Task::Exec::getName` | 读取名称 | — |
| `os::Task::Exec::getArgumentCount` | 读取程序参数数量 | — |
| `os::Task::Exec::getEnvironmentCount` | 读取环境字符串数量 | — |
| `os::Task::Exec::getArguments` | 读取程序参数字符串数组 | — |
| `os::Task::Exec::getEnvironment` | 读取环境字符串数组 | — |
| `os::Task::Exec::getRuntimeState` | 读取exec时交接的运行时数据地址 | — |
| `os::Task::Exec::getRuntimeStateSize` | 读取运行时交接数据字节数 | — |
| `os::Task::Exec::getSharedImageCount` | 读取共享映像数量 | — |
| `os::Task::Exec::getSharedImages` | 读取共享映像数组 | — |
| `os::Task::Exec::setImage` | 设置映像字节的借用地址 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `os::Task::Exec::setName` | 设置名称 | `value`：名称 |
| `os::Task::Exec::setArguments` | 设置程序参数字符串数组 | `value`：程序参数字符串数组<br>`count`：程序启动参数数量 |
| `os::Task::Exec::setEnvironment` | 设置环境字符串数组 | `value`：环境字符串数组<br>`count`：项目数量 |
| `os::Task::Exec::setRuntimeState` | 设置exec时交接的运行时数据地址 | `value`：exec时交接的运行时数据地址 |
| `os::Task::Exec::setRuntimeStateSize` | 设置运行时交接数据字节数 | `value`：运行时交接数据字节数 |
| `os::Task::Exec::setSharedImages` | 设置共享映像数组 | `value`：共享映像数组<br>`count`：共享映像数量 |
| `os::Task::getExitSignal` | 读取导致进程退出的信号 | — |
| `os::Task::getExitCoreState` | 读取退出时的core转储状态 | — |
| `os::Task::getExitFaultThread` | 读取发生异常的线程编号 | — |
| `os::Task::getExitVector` | 读取处理器异常向量号 | — |
| `os::Task::getExitError` | 读取处理器异常错误码 | — |
| `os::Task::getExitRip` | 读取异常时的指令地址 | — |
| `os::Task::getExitRsp` | 读取异常时的栈地址 | — |
| `os::Task::getExitAddress` | 读取异常访问的地址 | — |
| `os::Task::getExitStarted` | 读取发生异常的时间 | — |
| `os::Task::getExitAddressValid` | 读取异常地址是否有效 | — |
| `os::Task::getExitSignalCode` | 读取退出信号的具体原因 | — |
| `os::Task::refreshExitTarget` | 更新进程退出端点 | `id`：进程编号 |
| `os::Task::spawnImage` | 从执行映像创建进程 | `image`：程序映像或执行选项 |
| `os::Task::Core::getEnabled` | 读取是否允许该服务自动启动 | — |
| `os::Task::Core::getMaximumBytes` | 读取单份core转储的最大字节数 | — |
| `os::Task::Core::getMaximumConcurrent` | 读取允许同时进行的core转储数量 | — |
| `os::Task::Core::getTimeoutMilliseconds` | 读取core操作超时毫秒数 | — |
| `os::Task::Core::getId` | 读取编号 | — |
| `os::Task::Core::getTaskId` | 读取所属或对应的进程编号 | — |
| `os::Task::Core::getStarted` | 读取core操作开始时间 | — |
| `os::Task::Core::getBytes` | 读取已生成的core字节数 | — |
| `os::Task::Core::getDeadline` | 读取操作截止时间 | — |
| `os::Task::Core::getSignal` | 读取相关信号编号 | — |
| `os::Task::Core::getTruncated` | 读取core内容是否被截断 | — |
| `os::Task::Core::getState` | 读取系统或用户的持久状态目录路径 | — |
| `os::Task::Core::setEnabled` | 设置是否允许该服务自动启动 | `value`：是否允许该服务自动启动 |
| `os::Task::Core::setMaximumBytes` | 设置单份core转储的最大字节数 | `value`：单份core转储的最大字节数 |
| `os::Task::Core::setMaximumConcurrent` | 设置允许同时进行的core转储数量 | `value`：允许同时进行的core转储数量 |
| `os::Task::Core::setTimeoutMilliseconds` | 设置core操作超时毫秒数 | `value`：core操作超时毫秒数 |
| `os::Task::Signal::getHandler` | 读取信号处理入口地址 | — |
| `os::Task::Signal::getMask` | 读取信号屏蔽位集合 | — |
| `os::Task::Signal::getFlags` | 读取当前标志位 | — |
| `os::Task::Signal::getRestorer` | 读取信号处理结束时使用的返回入口 | — |
| `os::Task::Signal::setHandler` | 设置信号处理入口地址 | `value`：信号处理入口地址 |
| `os::Task::Signal::setMask` | 设置信号屏蔽位集合 | `value`：信号屏蔽位集合 |
| `os::Task::Signal::setFlags` | 设置当前标志位 | `value`：当前标志位 |
| `os::Task::Signal::setRestorer` | 设置信号处理结束时使用的返回入口 | `value`：信号处理结束时使用的返回入口 |
| `os::Task::querySignalAction` | 读取指定信号当前的处理方式 | `signal`：信号编号<br>`action`：信号处理设置 |
| `os::Task::setSignalAction` | 设置指定信号的处理方式 | `signal`：信号编号<br>`action`：信号处理设置 |
| `os::Task::Credential::getMaximumGroups` | 读取当前允许的最多补充组数量 | — |
| `os::Task::Credential::setUserIds` | 设置真实、有效和保存的用户编号 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `os::Task::Credential::setGroupIds` | 设置真实、有效和保存的用户组编号 | `real`：真实身份编号<br>`effective`：有效身份编号<br>`saved`：保存的身份编号 |
| `os::Task::Credential::setGroups` | 设置补充用户组列表 | `groups`：附加用户组编号数组<br>`count`：附加用户组数量 |
| `os::createThread` | 创建线程 | `entry`：线程入口函数<br>`stackPointer`：线程栈顶地址 |
| `os::createThread` | 创建线程 | `entry`：线程入口函数<br>`stackPointer`：线程栈顶地址<br>`argument`：传给线程入口的上下文 |
| `os::forkTask` | 派生进程 | — |
| `os::yield` | 让出处理器 | — |
| `os::sleep` | 等待指定时间 | `milliseconds`：等待时长，单位为毫秒 |
| `os::getStandardHandle` | 读取标准输入输出句柄 | `index`：项目索引 |
| `os::replaceTaskImage` | 替换进程映像 | `image`：程序映像或执行选项 |
| `os::ThreadLaunch::ThreadLaunch` | 构造ThreadLaunch对象 | `entry`：线程入口函数<br>`argument`：传给线程入口的上下文 |
| `os::ThreadLaunch::run` | 运行线程入口函数 | — |
| `os::startLibraryThread` | 启动运行库线程 | `storage`：存放线程启动对象的内存 |
| `os::taskAttribute` | 转换进程属性 | `operation`：文件操作对象<br>`id`：目标编号<br>`a`：请求的第一个数据字段<br>`b`：请求的第二个数据字段<br>`c`：请求的第三个数据字段<br>`d`：请求的第四个数据字段<br>`reply`：接收回复消息的对象 |
| `os::posixAttribute` | 转换文件属性 | `status`：操作状态<br>`reply`：接收回复消息的对象<br>`length`：数据长度 |
| `os::detail::TaskControl::TaskControl` | 构造TaskControl对象 | — |
| `os::detail::TaskControl::TaskControl` | 构造TaskControl对象 | `id`：进程编号 |
| `os::detail::TaskControl::getId` | 读取编号 | — |
| `os::detail::TaskControl::refresh` | 更新对象属性 | `id`：进程编号 |
| `os::detail::TaskControl::next` | 按游标枚举任务或恢复域中的资源 | `id`：进程编号 |
| `os::detail::TaskControl::refreshExit` | 读取目标进程的退出信号、异常位置和core状态 | `id`：进程编号 |
| `os::detail::TaskControl::setGroup` | 设置指定位置的补充组编号 | `group`：进程组编号<br>`id`：进程编号 |
| `os::Task::~Task` | 析构Task对象 | — |
| `os::Task::getId` | 读取编号 | — |
| `os::Task::getValid` | 读取对象是否可用 | — |
| `os::Task::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Task::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Task::open` | 打开对象连接 | `id`：进程编号 |
| `os::Task::openCurrent` | 绑定当前进程使用的控制台 | — |
| `os::Task::create` | 创建进程 | `image`：程序映像或执行选项 |
| `os::Task::close` | 关闭对象连接 | — |
| `os::Task::refresh` | 更新对象属性 | — |
| `os::Task::refreshExit` | 读取目标进程的退出信号、异常位置和core状态 | — |
| `os::Task::terminate` | 请求强制结束绑定进程 | `code`：进程或线程退出码 |
| `os::Task::wait` | 等待本对象对应的文件请求完成 | `code`：进程或线程退出码 |
| `os::Task::next` | 按游标枚举任务或恢复域中的资源 | `minimum`：枚举的起始进程编号<br>`target`：目标路径 |
| `os::Task::querySignalMask` | 读取目标支持查询的当前信号屏蔽集合 | `mask`：信号集合 |
| `os::Task::queryPendingSignals` | 查询待处理信号集合 | `mask`：信号集合 |
| `os::Task::setSignalMask` | 设置当前信号屏蔽集合 | `how`：信号屏蔽集合的修改方式<br>`mask`：信号集合 |
| `os::Task::setGroup` | 设置指定位置的补充组编号 | `group`：进程组编号 |
| `os::Task::setNice` | 设置调度优先级调整值 | `value`：调度优先级调整值 |
| `os::Task::queryResourceLimit` | 读取指定资源的软限制和硬限制 | `resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `os::Task::setResourceLimit` | 设置资源软限制和硬限制 | `resource`：资源限制类型<br>`soft`：资源软限制<br>`hard`：资源硬限制 |
| `os::Task::querySignalStack` | 读取当前配置的备用信号栈 | `stack`：线程栈或备用信号栈 |
| `os::Task::setSignalStack` | 设置备用信号栈 | `stack`：线程栈或备用信号栈 |
| `os::Task::queryCorePolicy` | 读取core转储是否启用、大小及并发等限制 | `policy`：核心转储策略 |
| `os::Task::setCorePolicy` | 设置core转储配置 | `policy`：核心转储策略 |
| `os::Task::SignalStack::getAddress` | 读取映射开始的虚拟地址 | — |
| `os::Task::SignalStack::getSize` | 读取长度 | — |
| `os::Task::SignalStack::getFlags` | 读取当前标志位 | — |
| `os::Task::SignalStack::setAddress` | 设置映射开始的虚拟地址 | `value`：映射开始的虚拟地址 |
| `os::Task::SignalStack::setSize` | 设置长度 | `value`：长度 |
| `os::Task::SignalStack::setFlags` | 设置当前标志位 | `value`：当前标志位 |
| `os::Task::Child::getId` | 读取编号 | — |
| `os::Task::Child::getUid` | 读取真实用户编号 | — |
| `os::Task::Child::getCode` | 读取子进程事件原因 | — |
| `os::Task::Child::getStatus` | 读取异步文件操作的完成状态 | — |
| `os::Task::ImageMapping::getBegin` | 读取映像映射开始地址 | — |
| `os::Task::ImageMapping::getEnd` | 读取映像映射结束地址 | — |
| `os::Task::ImageMapping::getOffset` | 读取当前偏移位置 | — |
| `os::Task::ImageMapping::getPath` | 读取指定种类的系统或用户约定目录路径 | — |
| `os::Task::Thread::Thread` | 构造Thread对象 | — |
| `os::Task::Thread::~Thread` | 析构Thread对象 | — |
| `os::Task::Thread::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Task::Thread::open` | 打开对象连接 | `task`：目标进程<br>`thread`：线程编号 |
| `os::Task::Thread::openCurrent` | 绑定当前进程使用的控制台 | — |
| `os::Task::Thread::create` | 创建线程 | `entry`：线程入口函数<br>`argument`：传给线程入口的上下文<br>`stack`：线程栈或备用信号栈<br>`size`：数据大小，单位为字节 |
| `os::Task::Thread::close` | 关闭对象连接 | — |
| `os::Task::Thread::wait` | 等待本对象对应的文件请求完成 | `code`：进程或线程退出码 |
| `os::Task::Thread::sendSignal` | 向绑定的进程或线程发送信号 | `signal`：信号编号 |
| `os::Task::Thread::getValid` | 读取对象是否可用 | — |
| `os::Task::Thread::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Task::Thread::setNice` | 设置调度优先级调整值 | `value`：调度优先级调整值 |
| `os::Task::Session::Session` | 构造Session对象 | — |
| `os::Task::Session::~Session` | 析构Session对象 | — |
| `os::Task::Session::operator=` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `os::Task::Session::refresh` | 更新对象属性 | — |
| `os::Task::Session::openCurrent` | 绑定当前进程使用的控制台 | — |
| `os::Task::Session::create` | 创建进程会话 | — |
| `os::Task::Session::close` | 关闭对象连接 | — |
| `os::Task::Session::getId` | 读取编号 | — |
| `os::Task::Session::getLeaderId` | 读取会话首进程编号 | — |
| `os::Task::Session::getForegroundGroup` | 读取控制终端的前台进程组编号 | — |
| `os::Task::Session::getTerminalKind` | 读取控制终端的种类 | — |
| `os::Task::Session::getTerminalIndex` | 读取控制终端的编号 | — |
| `os::Task::Session::getValid` | 读取对象是否可用 | — |
| `os::Task::Session::getError` | 读取最近一次申请或操作的错误码 | — |
| `os::Task::Session::setForegroundGroup` | 设置控制终端的前台进程组编号 | `group`：进程组编号 |

## lib/os/user.cc

用户账户、认证和登录会话。

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `os::UserEncoding::erase` | 清除认证数据 | `pointer`：待释放的内存地址<br>`count`：项目数量 |
| `os::UserEncoding::terminated` | 检查字符串结束字符 | `text`：待检查的文本<br>`size`：数据大小，单位为字节 |
| `os::UserEncoding::copyInput` | 复制用户请求文本 | `target`：目标路径<br>`source`：待复制的数据<br>`size`：数据大小，单位为字节 |
| `os::SecretMessage::SecretMessage` | 构造SecretMessage对象 | `message`：消息内容 |
| `os::SecretMessage::~SecretMessage` | 析构SecretMessage对象 | — |
| `os::User::Profile::getUid` | 读取真实用户编号 | — |
| `os::User::Profile::getGid` | 读取真实用户组编号 | — |
| `os::User::Profile::getName` | 读取名称 | — |
| `os::User::Profile::getHome` | 读取账户主目录 | — |
| `os::User::Profile::getShell` | 读取账户登录shell路径 | — |
| `os::User::User` | 构造User对象 | — |
| `os::User::open` | 打开对象连接 | — |
| `os::User::close` | 关闭对象连接 | — |
| `os::User::exchange` | 交换用户服务消息 | `operation`：文件操作对象<br>`id`：目标编号<br>`name`：对象名称<br>`password`：认证密码<br>`replacement`：新密码<br>`profile`：接收账户资料的对象<br>`sessionTask`：接收登录会话进程编号 |
| `os::User::lookup` | 按用户编号读取账户资料 | `uid`：用户编号<br>`profile`：接收账户资料的对象 |
| `os::User::lookupName` | 按用户名读取账户资料 | `name`：对象名称<br>`profile`：接收账户资料的对象 |
| `os::User::login` | 验证用户名和密码 | `name`：对象名称<br>`password`：认证密码<br>`profile`：接收账户资料的对象 |
| `os::User::authorizeSudo` | 根据真实调用者、密码和sudoers请求提权 | `password`：认证密码<br>`profile`：接收账户资料的对象 |
| `os::User::changePassword` | 修改账户密码 | `name`：对象名称<br>`password`：认证密码<br>`replacement`：新密码 |
| `os::User::querySession` | 读取给定登录任务的账户和服务确认的会话任务编号 | `taskId`：进程编号<br>`profile`：接收账户资料的对象<br>`sessionTask`：接收登录会话进程编号 |
| `os::User::beginSession` | 为已认证账户准备会话目录等资源 | `uid`：用户编号<br>`profile`：接收账户资料的对象 |
| `os::User::autoLogin` | 请求服务按系统自动登录策略认证 | `profile`：接收账户资料的对象 |
| `os::User::list` | 按序号读取账户资料 | `index`：项目索引<br>`profile`：接收账户资料的对象 |
| `os::User::graphicalLogin` | 验证图形登录 | `name`：对象名称<br>`password`：认证密码<br>`profile`：接收账户资料的对象 |
