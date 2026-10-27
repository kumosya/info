# PS/2 输入入口

`src/kernel/device/ps2/ps2.cc` 在 `kernel::device` 中提供 `initPs2()`、`readPs2(value, mouse)`，声明在 `include/kernel/device.h`。内部 Controller 保存控制配置 configuration_ 和鼠标包长度 mousePacketSize_，全局 controllerLock 串行保护控制器访问。

内核处理硬件端口和初始化握手；键盘扫描码、鼠标包解析、输入路由由用户态 device 服务处理。这里没有内核软件输入队列，也不决定 GUI 焦点。

## 控制器初始化

`initPs2()` 持控制器锁调用 `Controller::init()`，成功返回鼠标包长度，失败返回 -11。

初始化先排出残留输出字节，再禁用接口、读取和修改控制配置，随后重新开放接口并配置键盘、鼠标。这样可减少启动残留字节与命令回复混淆的机会。键盘和鼠标命令均检查 0xfa ACK，不把任意返回字节当作成功。

| 内部成员函数 | 作用 |
| --- | --- |
| `inputReady()` | 等待控制器输入缓冲区空闲，最多轮询 100000 次；供命令与数据写入前使用。 |
| `outputReady()` | 查询输出缓冲区是否有数据。 |
| `readData(value)` | 有界等待输出，再把字节写入 value；超时或端口读取失败返回 false。 |
| `writeCommand(value)` | 等待输入空闲后向 0x64 写命令。 |
| `writeData(value)` | 等待输入空闲后向 0x60 写数据。 |
| `sendKeyboard(value)` | 发送键盘命令并检查 ACK。 |
| `sendMouse(value)` | 先发送控制器命令 0xd4，再发送鼠标命令并检查 ACK。 |
| `setMouseSampleRate(rate)` | 发送设置采样率命令及 rate，逐步检查回复。 |
| `enableMouseWheel()` | 尝试 200、100、80 的采样率序列，再查询鼠标 ID。 |
| `mousePacketSize()` | 鼠标包长度。 |

滚轮协商读到 ID 3 且随后设置 200 Hz 采样率成功时采用四字节包，否则维持三字节包；用户态必须按初始化返回的长度组包。端口辅助函数 `readPs2Port(port, value)`、`writePs2Port(port, value)` 只封装实际 I/O 指令。

## 中断后的读取

`readPs2(value, mouse)` 持同一把锁调用 `Controller::readInterruptByte(value, mouse)`。它先检查 0x64 的输出状态，没有数据时返回 false；有数据则读 0x60，并按状态位 5 填写 mouse。

IRQ 只表示可能有输入，不保证本次读取一定成功。用户态通过 device.kernel 的 ReadInput 取字节，不能把 IRQ 通知本身当作完整按键或鼠标包。

键盘 IRQ 1 和鼠标 IRQ 12 的绑定通过 InterruptBinding 完成；控制器读写锁与 IRQ 绑定锁解决不同问题，不能相互替代。路由与通知过程见[资源和中断](resource.md)，服务授权见[设备机制服务](service.md)。
