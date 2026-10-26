# os/device.h



当前来源：`os/pci.h`；推荐头文件为 `os/device.h`。


## 内容与边界

pci.h 改为 device.h，因为这里不只涉及 PCI，也包含 DMA、中断、MMIO 和线性帧缓冲。根目录实现对应 device.cc。

Device 表示经过授权的设备引用。Device::open(pci) 请求授权；Dma::create、Mmio::open、Irq::create 申请对应资源，close 显式报告释放结果。stop 明确请求停止硬件；析构只作最后清理，不代表设备已停止。

| 嵌套类 | 功能 |
| --- | --- |
| Device::Pci | 直接表示一个 PCI 功能，保存地址与硬件属性，提供枚举、查询和配置空间读写 |
| Device::Dma | DMA 分配及映射引用，getter 读取虚拟地址、设备地址、长度和句柄 |
| Device::Mmio | 管理一段 MMIO 映射，getter 读取地址、长度及 BAR 编号 |
| Device::Frame | 打开和管理线性帧缓冲映射，getter 读取宽高、pitch、格式及颜色通道 |
| Device::Irq | 管理设备中断到 IPC 接收端点的绑定 |

不再保留 DeviceService 类：设备授权归 Device::open，DMA、MMIO、IRQ、帧缓冲分别由各自的 create/open 和 close 处理。

Device::Irq 不是应用的普通窗口事件。内核收到硬件中断后向已绑定端点通知，驱动处理设备状态，并在需要时 acknowledge；close 请求解绑并释放资源，防止驱动退出后仍向旧端点投递。普通应用通常不需要这个类，用户态驱动会使用，所有操作受设备权限限制。

释放 DMA、解除 IRQ 等必须有实际服务支持，不能仅在析构时丢弃编号。硬件身份不提供普通 setter。

## 枚举

### Device::Frame::Format

底层类型：`uint8_t`。像素的颜色通道排列，实际位位置和 pitch 仍以帧缓冲属性为准。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Unknown` | `0` | 未知或尚未识别 |
| `Rgb` | `1` | RGB 颜色顺序 |
| `Bgr` | `2` | BGR 颜色顺序 |
| `Argb` | `3` | ARGB 颜色顺序 |
| `Rgba` | `4` | RGBA 颜色顺序 |
| `Bgra` | `5` | BGRA 颜色顺序 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

当前公共头文件没有声明需要公开的独立数值或字符串常量。

## 全局变量

当前公共头文件没有导出可写全局变量。设备授权、DMA、IRQ 和映射引用属于对应对象，不导出全局硬件对象。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Device
这个类表示一份经过权限验证的设备访问引用。

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


```cpp
Device& operator=(Device&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Device&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int stop();
```

用途：请求设备停止工作；这与释放一个设备引用不同。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(const Device::Pci& function);
```

用途：根据已查询的 PCI 功能请求设备授权，拒绝授权时保留错误结果。

参数：

- `const Device::Pci& function`：已经枚举或查询到的 PCI 功能对象。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：释放本对象持有的设备授权引用，不请求停止硬件；停止设备需先调用 stop。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Device::Pci

这个对象表示一个 PCI 功能：保存段、总线、设备、功能号及硬件属性，直接提供配置空间读写；枚举和 query 填写同一种 Pci 对象，不再另设 Func。

#### 构造与析构

```cpp
Pci();
```

用途：初始化空的 PCI 功能对象，不查找设备、不请求授权。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int enumerate(Device::Pci* functions, uint32_t capacity);
```

用途：一次查询多个 PCI 功能，填写数组并返回实际数量。

参数：

- `Device::Pci* functions`：用于接收 PCI 功能结果的数组。
- `uint32_t capacity`：functions 数组可容纳的 PCI 功能对象个数。

结果：成功返回实际填入的对象个数，最多为 capacity；0 表示没有匹配设备，负 ErrorCode 表示失败。

```cpp
int query(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, Device::Pci& result);
```

用途：查询指定 PCI 地址，把地址和硬件属性写入 result；不授予设备访问权限。

参数：

- `uint16_t segment`：PCI 段号。
- `uint8_t bus`：PCI 总线号。
- `uint8_t device`：PCI 总线内的设备号。
- `uint8_t function`：PCI 设备中的功能号。
- `Device::Pci& result`：接收查询或复制结果的输出对象。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int readConfig(uint16_t offset, uint32_t& value);
```

用途：读取绑定 PCI 功能的配置空间寄存器。

参数：

- `uint16_t offset`：PCI 配置空间中的寄存器字节偏移。
- `uint32_t& value`：输出读取到的配置寄存器数值。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int writeConfig(uint16_t offset, uint32_t value);
```

用途：向绑定 PCI 功能的配置空间寄存器写入数值，权限由服务验证。

参数：

- `uint16_t offset`：PCI 配置空间中的寄存器字节偏移。
- `uint32_t value`：要写入配置寄存器的数值。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function);
```

用途：按地址查询一个 PCI 功能，成功后把该地址和硬件属性绑定到本对象。

参数：

- `uint16_t segment`：PCI 段号。
- `uint8_t bus`：PCI 总线号。
- `uint8_t device`：总线中的 PCI 设备号，不是授权 Device 对象。
- `uint8_t function`：该 PCI 设备中的功能号，不是另一个对象。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：清除绑定地址与属性，并释放本对象拥有的查询连接；不停止硬件，也不撤销其他 Device 的授权引用。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint16_t getSegment() const;
```

数据：PCI 段号。

```cpp
uint8_t getBus() const;
```

数据：PCI 总线号。

```cpp
uint8_t getDevice() const;
```

数据：PCI 设备号。

```cpp
uint8_t getFunction() const;
```

数据：PCI 功能号。

```cpp
uint8_t getClassCode() const;
```

数据：PCI 设备大类。

```cpp
uint8_t getSubclass() const;
```

数据：PCI 设备子类。

```cpp
uint8_t getProgrammingInterface() const;
```

数据：PCI 编程接口编号。

```cpp
uint16_t getVendorId() const;
```

数据：硬件厂商编号。

```cpp
uint16_t getDeviceId() const;
```

数据：厂商定义的设备编号。

```cpp
uint8_t getHeaderType() const;
```

数据：PCI 配置头类型。

### Device::Dma
这个类表示一份可供设备直接读写的内存分配及其映射。

#### 构造与析构

```cpp
~Dma();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Dma();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Device::Dma& operator=(Device::Dma&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Device::Dma&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int create(Device& device, uint64_t size, uint64_t alignment = 4096);
```

用途：根据设备要求申请 DMA 缓冲区和必要映射。

参数：

- `Device& device`：已经授权的设备对象；格式化接口中为块设备路径。
- `uint64_t size`：本次数据的字节数。
- `uint64_t alignment = 4096`：要求的对齐字节数。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：请求释放 DMA 分配及其映射。调用前必须停止设备对该缓冲区的访问；释放失败时保留仍有效的分配信息。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
uint64_t getVirtualAddress() const;
```

数据：CPU 可访问的虚拟地址。

```cpp
uint64_t getDeviceAddress() const;
```

数据：设备进行 DMA 时使用的地址。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Device::Mmio
这个类表示一段设备寄存器的用户地址映射。

#### 构造与析构

```cpp
~Mmio();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Mmio();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Device::Mmio& operator=(Device::Mmio&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Device::Mmio&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int open(Device& device, uint8_t bar, uint64_t offset, uint64_t size);
```

用途：为授权设备的指定 BAR 建立 MMIO 映射。

参数：

- `Device& device`：已经授权的设备对象；格式化接口中为块设备路径。
- `uint8_t bar`：需要映射的 PCI BAR 序号。
- `uint64_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。
- `uint64_t size`：本次数据的字节数。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：解除本对象拥有的 MMIO 映射并释放映射引用，不改变设备本身的运行状态。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getAddress() const;
```

数据：映射开始的虚拟地址。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint8_t getBarIndex() const;
```

数据：PCI BAR 序号。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Device::Frame
这个类表示当前显示设备的线性帧缓冲映射。

#### 构造与析构

```cpp
~Frame();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Frame();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Device::Frame& operator=(Device::Frame&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Device::Frame&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(uint64_t address = 0);
```

用途：取得主显示设备的帧缓冲映射；不支持或映射失败时返回错误。

参数：

- `uint64_t address`：希望映射帧缓冲的用户虚拟地址；0 让内存服务选择合适地址。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：解除帧缓冲映射并释放显示访问引用，不关闭显示设备。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getAddress() const;
```

数据：映射开始的虚拟地址。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint32_t getWidth() const;
```

数据：帧缓冲宽度，单位为像素。

```cpp
uint32_t getHeight() const;
```

数据：帧缓冲高度，单位为像素。

```cpp
uint32_t getPitch() const;
```

数据：相邻两行像素起点之间的字节数。

```cpp
uint8_t getBitsPerPixel() const;
```

数据：每个像素占用的位数。

```cpp
Device::Frame::Format getFormat() const;
```

数据：像素中颜色通道的排列方式。

```cpp
uint8_t getRedPosition() const;
```

数据：红色通道的位位置。

```cpp
uint8_t getGreenPosition() const;
```

数据：绿色通道的位位置。

```cpp
uint8_t getBluePosition() const;
```

数据：蓝色通道的位位置。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Device::Irq
这个类表示设备中断到接收端点的绑定。

#### 构造与析构

```cpp
~Irq();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Irq();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Device::Irq& operator=(Device::Irq&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Device::Irq&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int acknowledge();
```

用途：通知内核本轮设备中断已处理，允许需要确认的中断继续投递。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int create(Device& device, uint8_t irq, Ipc& endpoint, bool levelTriggered = false);
```

用途：将授权设备的中断绑定到指定接收端点。

参数：

- `Device& device`：已经授权的设备对象；格式化接口中为块设备路径。
- `uint8_t irq`：要绑定的中断编号，必须属于调用者获准使用的设备。
- `Ipc& endpoint`：接收设备中断或服务消息的 IPC 端点。
- `bool levelTriggered = false`：是否为需要保持确认流程的电平触发中断，实际类型还由内核验证。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：请求解除中断与接收端点的绑定并释放绑定引用；失败时保留服务确认仍存在的绑定信息。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint8_t getIrq() const;
```

数据：中断线路或向量编号。

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

## 实现文件

`lib/os/device.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
