# os/storage.h


## 内容与边界

Storage 负责设备发现、枚举和数量快照。Storage::Device 管理已绑定块设备，open 绑定设备、close 显式释放访问引用，refresh、read、write、flush 操作自身目标；getter 读取身份、容量、块尺寸、只读状态、分区与硬件路径。

分批 I/O 中途失败保留完成块数。稳定身份或代次用于避免编号复用后旧对象误操作新设备。关闭设备引用不使设备离线。

Storage::Number 表示主次设备号，可设置本地数值；Storage::Part 表示解析后的分区属性，通过 getter/setter 读写结果。修改分区值对象不代表分区表已经写盘。

## 枚举

### Storage::Device::Type

底层类型：`uint32_t`。块设备后端种类。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `VirtioBlock` | `1` | virtio 块设备 |
| `Ahci` | `2` | AHCI 存储设备 |
| `Nvme` | `3` | NVMe 存储设备 |
| `UsbStorage` | `4` | USB 存储设备 |
| `BootImage` | `5` | 启动映像提供的块设备 |

### Storage::Error

底层类型：`int64_t`。storage 服务错误，与通用 ErrorCode 不是同一种枚举，不能只凭数值互相解释。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Success` | `0` | 成功 |
| `DeviceNotFound` | `-1` | 没有找到块设备 |
| `DeviceOffline` | `-2` | 设备已经离线 |
| `InvalidBlock` | `-3` | 块编号或范围无效 |
| `Io` | `-4` | 设备读写失败 |
| `Timeout` | `-5` | 等待超时 |
| `ReadOnly` | `-6` | 设备只读 |
| `NoSpace` | `-7` | 可用空间不足 |
| `Dma` | `-8` | DMA 分配或访问失败 |
| `Corrupted` | `-9` | 检测到损坏的数据或元信息 |
| `InvalidArgument` | `-10` | 参数无效 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `maximumStorageDevices` | `32` | 当前存储服务设备槽位上限 | 服务内部限制；已发现数量通过 Storage::getDeviceCount() 查询 |
| `maximumStorageTransferBlocks` | `7` | 当前客户端一次内联传输的块数上限 | 实现内部限制；设备可用批量大小通过 getMaxTransferBlocks() 查询 |
| `storageIdentityWord` | `6` | 存储回复中身份记录的起始字位置 | 存储服务协议内部偏移 |

## 全局变量

当前公共头文件没有导出可写全局变量。设备身份、传输缓冲区和锁属于 Storage::Device，不导出共享的全局设备对象。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Storage

这个类表示storage 服务发现、块设备枚举和设备数量查询。

#### 构造与析构

```cpp
Storage();
```

用途：建立storage 服务发现、块设备枚举和设备数量查询；只保存借用数据或设置时，不接管调用者缓冲区。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryDevice(uint32_t id, Storage::Device& device);
```

用途：读取给定块设备并填写绑定对象。

参数：

- `uint32_t id`：要绑定的资源编号，含义由所属类确定。
- `Storage::Device& device`：已经授权的设备对象；格式化接口中为块设备路径。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int nextDevice(uint32_t minimumId, Storage::Device& device);
```

用途：从编号不小于 minimumId 的设备开始查找下一项。

参数：

- `uint32_t minimumId`：从此编号开始查找，包括该编号本身。
- `Storage::Device& device`：已经授权的设备对象；格式化接口中为块设备路径。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint32_t getDeviceCount() const;
```

数据：发现的块设备数量。

### Storage::Device
这个类表示一台已经发现的块设备访问引用。

#### 构造与析构

```cpp
~Device();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Device();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
bool refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
bool flush();
```

用途：要求服务把已提交的修改写回设备，等待同步结果。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
int read(uint64_t firstBlock, uint32_t count, void* buffer, size_t capacity, uint32_t& completed);
```

用途：从绑定块设备的 firstBlock 开始读取 count 个逻辑块；completed 接收实际完成块数，失败也保留部分结果。

参数：

- `uint64_t firstBlock`：本次读写的第一个逻辑块编号。
- `uint32_t count`：输出实际得到的条目个数；输入时表示数组中有效元素个数。
- `void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t capacity`：输出缓冲区可容纳的字节数；条目数组接口中表示可容纳的条目个数。
- `uint32_t& completed`：输出实际完成的块数，即使操作中途失败也要填写。

结果：0 表示请求块全部完成，负值表示失败；completed 始终指出此前已完成的块数。

```cpp
int write(uint64_t firstBlock, uint32_t count, const void* buffer, size_t size, uint32_t& completed);
```

用途：向绑定块设备从 firstBlock 开始写入 count 个逻辑块；只读设备拒绝写入，completed 保留实际完成块数。

参数：

- `uint64_t firstBlock`：本次读写的第一个逻辑块编号。
- `uint32_t count`：输出实际得到的条目个数；输入时表示数组中有效元素个数。
- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。
- `uint32_t& completed`：输出实际完成的块数，即使操作中途失败也要填写。

结果：0 表示请求块全部完成，负值表示失败；completed 始终指出此前已完成的块数。

```cpp
int open(uint32_t id);
```

用途：连接并绑定指定块设备，不创建硬件设备。

参数：

- `uint32_t id`：要绑定的资源编号，含义由所属类确定。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：释放块设备访问引用，不删除设备或分区；调用前应等待本对象提交的读写完成。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getBlockCount() const;
```

数据：设备或分区包含的块数。

```cpp
uint32_t getBlockSize() const;
```

数据：每块字节数。

```cpp
uint32_t getMaxTransferBlocks() const;
```

数据：服务一次请求允许的最大块数。

```cpp
Storage::Device::Type getType() const;
```

数据：块设备种类。

```cpp
bool getReadOnly() const;
```

数据：是否只读。

```cpp
uint32_t getId() const;
```

数据：编号。

```cpp
uint32_t getMajor() const;
```

数据：设备主编号。

```cpp
uint32_t getMinor() const;
```

数据：设备次编号。

```cpp
uint32_t getParentId() const;
```

数据：父块设备编号。

```cpp
uint32_t getPartitionNumber() const;
```

数据：分区编号。

```cpp
uint64_t getStartBlock() const;
```

数据：分区在父设备中的起始块。

```cpp
const char* getName() const;
```

数据：名称。

```cpp
const char* getPartitionUuid() const;
```

数据：分区 UUID。

```cpp
const char* getHardwarePath() const;
```

数据：设备硬件路径。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Storage::Number

这个类表示设备主编号与次编号。

#### 构造与析构

```cpp
Number(uint32_t major = 0, uint32_t minor = 0);
```

用途：建立设备主编号与次编号；只保存借用数据或设置时，不接管调用者缓冲区。

参数：

- `uint32_t major = 0`：设备主编号，标识设备类别或驱动分组。
- `uint32_t minor = 0`：同一主编号下的具体设备编号。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### getter

```cpp
uint64_t getValue() const;
```

数据：原始句柄值。

```cpp
uint32_t getMajor() const;
```

数据：设备主编号。

```cpp
uint32_t getMinor() const;
```

数据：设备次编号。

#### setter

```cpp
void setMajor(uint32_t value);
```

数据：设备主编号。

```cpp
void setMinor(uint32_t value);
```

数据：设备次编号。

### Storage::Part

这个类表示解析得到的一项磁盘分区属性。

#### getter

```cpp
uint64_t getStartBlock() const;
```

数据：分区在父设备中的起始块。

```cpp
uint64_t getBlockCount() const;
```

数据：设备或分区包含的块数。

```cpp
uint32_t getType() const;
```

数据：种类。

```cpp
uint32_t getNumber() const;
```

数据：PTY 编号。

```cpp
const char* getUuid() const;
```

数据：分区 UUID。

#### setter

```cpp
void setStartBlock(uint64_t value);
```

数据：分区在父设备中的起始块。

```cpp
void setBlockCount(uint64_t value);
```

数据：设备或分区包含的块数。

```cpp
void setType(uint32_t value);
```

数据：待发送消息的类型。

```cpp
void setNumber(uint32_t value);
```

数据：PTY 编号。

```cpp
void setUuid(const char* value);
```

数据：分区 UUID。

## 实现文件

`lib/os/storage.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
