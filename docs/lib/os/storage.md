# 块存储

## 枚举

### os::Storage::Error

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Success` | `0` | 成功 |
| `DeviceNotFound` | `-1` | 块设备查找失败 |
| `DeviceOffline` | `-2` | 设备已经离线 |
| `InvalidBlock` | `-3` | 块编号或范围无效 |
| `Io` | `-4` | 设备读写失败 |
| `Timeout` | `-5` | 等待超时 |
| `ReadOnly` | `-6` | 设备只读 |
| `NoSpace` | `-7` | 可用空间不足 |
| `Dma` | `-8` | DMA分配或访问失败 |
| `Corrupted` | `-9` | 检测到损坏的数据或元信息 |
| `InvalidArgument` | `-10` | 参数无效 |
| `NotSupported` | `-11` | 功能支持错误 |
| `CapabilityUnknown` | `-12` | 设备能力未知 |

### os::Storage::Device::Type

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `VirtioBlock` | `1` | virtio块设备 |
| `Ahci` | `2` | AHCI SATA 磁盘 |
| `Nvme` | `3` | NVMe存储设备 |
| `UsbStorage` | `4` | USB存储设备 |
| `BootImage` | `5` | 启动映像提供的块设备 |
| `Ide` | `6` | IDE ATA 磁盘 |

## 对象

### os::Storage

存储设备目录对象，枚举和查询块设备。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Storage()` | 构造Storage对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int refresh()` | 更新对象属性 | — |
| `int queryDevice(uint32_t id, Device& device)` | 读取给定块设备并填写绑定对象 | `id`：块设备编号<br>`device`：接收块设备连接的对象 |
| `int nextDevice(uint32_t minimumId, Device& device)` | 从编号不小于minimumId的设备开始查找下一项 | `minimumId`：枚举的起始编号<br>`device`：接收块设备连接的对象 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint32_t getDeviceCount() const` | 读取发现的块设备数量 | — |

### os::Storage::Statistics

存储统计对象，保存请求数、传输字节数和错误计数。

### os::Storage::Number

设备编号对象，保存主设备号和次设备号。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Number(uint32_t major = 0, uint32_t minor = 0)` | 构造Number对象 | `major`：主设备号<br>`minor`：次设备号 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getValue() const` | 读取原始句柄值 | — |
| `uint32_t getMajor() const` | 读取设备主编号 | — |
| `uint32_t getMinor() const` | 读取设备次编号 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setMajor(uint32_t value)` | 设置设备主编号 | `value`：设备主编号 |
| `void setMinor(uint32_t value)` | 设置设备次编号 | `value`：设备次编号 |

### os::Storage::Part

分区对象，保存起始块、容量、类型和标识。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getStartBlock() const` | 读取分区在父设备中的起始块 | — |
| `uint64_t getBlockCount() const` | 读取设备或分区包含的块数 | — |
| `uint32_t getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `uint32_t getNumber() const` | 读取PTY编号 | — |
| `const char* getUuid() const` | 读取分区UUID | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setStartBlock(uint64_t value)` | 设置分区在父设备中的起始块 | `value`：分区在父设备中的起始块 |
| `void setBlockCount(uint64_t value)` | 设置设备或分区包含的块数 | `value`：设备或分区包含的块数 |
| `void setType(uint32_t value)` | 设置目录项是文件、目录还是其他类型 | `value`：目录项是文件、目录还是其他类型 |
| `void setNumber(uint32_t value)` | 设置PTY编号 | `value`：PTY编号 |
| `void setUuid(const char* value)` | 设置分区UUID | `value`：分区UUID |

### os::Storage::Device

块设备连接对象，提供块读写、缓存刷新和属性查询。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Device()` | 构造Device对象 | — |
| `~Device()` | 析构Device对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int open(uint32_t id)` | 打开对象连接 | `id`：块设备编号 |
| `int close()` | 关闭对象连接 | — |
| `bool refresh()` | 更新对象属性 | — |
| `int read(uint64_t firstBlock, uint32_t count, void* buffer, size_t capacity, uint32_t& completed)` | 读取指定范围的磁盘块 | `firstBlock`：起始磁盘块编号<br>`count`：本次传输的磁盘块数量<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量<br>`completed`：接收实际完成的磁盘块数量 |
| `int write(uint64_t firstBlock, uint32_t count, const void* buffer, size_t size, uint32_t& completed)` | 写入指定范围的磁盘块 | `firstBlock`：起始磁盘块编号<br>`count`：本次传输的磁盘块数量<br>`buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节<br>`completed`：接收实际完成的磁盘块数量 |
| `bool flush()` | 要求服务把已提交的修改写回设备 | — |
| `Error flushStatus()` | 读取写缓存刷新能力 | — |
| `Error statistics(Statistics& result)` | 读取块设备统计 | `result`：接收块设备统计 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getBlockCount() const` | 读取设备或分区包含的块数 | — |
| `uint32_t getBlockSize() const` | 读取每块字节数 | — |
| `uint32_t getMaxTransferBlocks() const` | 读取服务一次请求允许的最大块数 | — |
| `Type getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `bool getReadOnly() const` | 读取是否只读 | — |
| `uint32_t getId() const` | 读取编号 | — |
| `uint32_t getMajor() const` | 读取设备主编号 | — |
| `uint32_t getMinor() const` | 读取设备次编号 | — |
| `uint32_t getParentId() const` | 读取父块设备编号 | — |
| `uint32_t getPartitionNumber() const` | 读取分区编号 | — |
| `uint64_t getStartBlock() const` | 读取分区在父设备中的起始块 | — |
| `const char* getName() const` | 读取名称 | — |
| `const char* getPartitionUuid() const` | 读取分区UUID | — |
| `const char* getHardwarePath() const` | 读取设备硬件路径 | — |
