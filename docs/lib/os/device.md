# 设备

## 枚举

### os::Device::Frame::Format

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Unknown` | `0` | 未知类型 |
| `Rgb` | `1` | RGB颜色顺序 |
| `Bgr` | `2` | BGR颜色顺序 |
| `Argb` | `3` | ARGB颜色顺序 |
| `Rgba` | `4` | RGBA颜色顺序 |
| `Bgra` | `5` | BGRA颜色顺序 |

## 对象

### os::Device

设备访问对象，持有设备连接。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `~Device()` | 析构Device对象 | — |
| `Device()` | 构造Device对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Device& operator=(Device&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int stop()` | 请求设备停止工作 | — |
| `int readPort(uint16_t port, uint32_t width, void* data, uint32_t count = 1)` | 读取硬件端口 | `port`：硬件端口编号<br>`width`：端口访问宽度，单位为字节<br>`data`：数据缓冲区<br>`count`：端口读写次数 |
| `int writePort(uint16_t port, uint32_t width, const void* data, uint32_t count = 1)` | 写入硬件端口 | `port`：硬件端口编号<br>`width`：端口访问宽度，单位为字节<br>`data`：数据缓冲区<br>`count`：端口读写次数 |
| `int open(const Pci& function)` | 打开对象连接 | `function`：PCI 功能对象 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Device::Pci

PCI 功能对象，保存总线地址和设备属性。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Pci()` | 构造Pci对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int enumerate(Device::Pci* functions, uint32_t capacity)` | 一次查询多个PCI功能 | `functions`：接收 PCI 功能信息的数组<br>`capacity`：接收缓冲区容量 |
| `int query(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function, Device::Pci& result)` | 查询指定PCI地址 | `segment`：PCI 段号<br>`bus`：PCI 总线号<br>`device`：PCI 设备号<br>`function`：PCI 功能号<br>`result`：接收PCI 功能信息 |
| `int readConfig(uint16_t offset, uint32_t& value)` | 读取绑定PCI功能的配置空间寄存器 | `offset`：数据起始偏移量<br>`value`：PCI 配置寄存器数值 |
| `int writeConfig(uint16_t offset, uint32_t value)` | 向绑定PCI功能的配置空间寄存器写入数值 | `offset`：数据起始偏移量<br>`value`：PCI 配置寄存器数值 |
| `int open(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function)` | 打开对象连接 | `segment`：PCI 段号<br>`bus`：PCI 总线号<br>`device`：PCI 设备号<br>`function`：PCI 功能号 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint16_t getSegment() const` | 读取PCI段号 | — |
| `uint8_t getBus() const` | 读取PCI总线号 | — |
| `uint8_t getDevice() const` | 读取PCI设备号 | — |
| `uint8_t getFunction() const` | 读取PCI功能号 | — |
| `uint8_t getClassCode() const` | 读取PCI设备大类 | — |
| `uint8_t getSubclass() const` | 读取PCI设备子类 | — |
| `uint8_t getProgrammingInterface() const` | 读取PCI编程接口编号 | — |
| `uint16_t getVendorId() const` | 读取硬件厂商编号 | — |
| `uint16_t getDeviceId() const` | 读取厂商定义的设备编号 | — |
| `uint8_t getHeaderType() const` | 读取PCI配置头类型 | — |

### os::Device::Dma

DMA 缓冲区对象，保存内存映射和设备访问地址。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `~Dma()` | 析构Dma对象 | — |
| `Dma()` | 构造Dma对象 | — |
| `Device::Dma& operator=(Device::Dma&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int create(Device& device, uint64_t size, uint64_t alignment = 4096)` | 分配设备 DMA 缓冲区 | `device`：设备连接对象<br>`size`：数据大小，单位为字节<br>`alignment`：地址对齐字节数 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `uint64_t getVirtualAddress() const` | 读取CPU可访问的虚拟地址 | — |
| `uint64_t getDeviceAddress() const` | 读取设备进行DMA时使用的地址 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Device::Mmio

设备寄存器映射对象，保存 BAR 区域和访问地址。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `~Mmio()` | 析构Mmio对象 | — |
| `Mmio()` | 构造Mmio对象 | — |
| `Device::Mmio& operator=(Device::Mmio&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int open(Device& device, uint8_t bar, uint64_t offset, uint64_t size)` | 打开对象连接 | `device`：设备连接对象<br>`bar`：PCI BAR 寄存器编号<br>`offset`：数据起始偏移量<br>`size`：数据大小，单位为字节 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getAddress() const` | 读取映射开始的虚拟地址 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint8_t getBarIndex() const` | 读取PCIBAR序号 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Device::Frame

显示缓冲区对象，保存像素映射和显示参数。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `~Frame()` | 析构Frame对象 | — |
| `Frame()` | 构造Frame对象 | — |
| `Device::Frame& operator=(Device::Frame&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int refresh()` | 更新对象属性 | — |
| `int open(uint64_t address = 0)` | 打开对象连接 | `address`：内存地址 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getAddress() const` | 读取映射开始的虚拟地址 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint32_t getWidth() const` | 读取帧缓冲宽度 | — |
| `uint32_t getHeight() const` | 读取帧缓冲高度 | — |
| `uint32_t getPitch() const` | 读取相邻两行像素起点之间的字节数 | — |
| `uint8_t getBitsPerPixel() const` | 读取每个像素占用的位数 | — |
| `Format getFormat() const` | 读取像素中颜色通道的排列方式 | — |
| `uint8_t getRedPosition() const` | 读取红色通道的位位置 | — |
| `uint8_t getGreenPosition() const` | 读取绿色通道的位位置 | — |
| `uint8_t getBluePosition() const` | 读取蓝色通道的位位置 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Device::Irq

硬件中断绑定对象，连接中断线路和消息端点。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `~Irq()` | 析构Irq对象 | — |
| `Irq()` | 构造Irq对象 | — |
| `Device::Irq& operator=(Device::Irq&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int acknowledge()` | 通知内核本轮设备中断已处理 | — |
| `int create(Device& device, uint8_t irq, Ipc& endpoint, bool levelTriggered = false)` | 绑定设备中断与消息端点 | `device`：设备连接对象<br>`irq`：硬件中断号<br>`endpoint`：接收中断通知的消息端点对象<br>`levelTriggered`：使用电平触发中断 |
| `int close()` | 关闭对象连接 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint8_t getIrq() const` | 读取中断线路或向量编号 | — |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
