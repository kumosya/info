# device 服务（src/device/）

device 服务（注册名 `"device"`）由内核内置镜像加载，授予 `processCapabilityDevice`。同一进程内还承载 **input 服务**（注册名 `"input"`）。

## 1. PCI 设备管理

```cpp
enum class DeviceCategory : uint8_t { Pci, Storage, Keyboard, Mouse, Display, Network, Timer, Serial };
enum class DriverState : uint8_t { Created, Initialized, Running, Stopping, Stopped };
```

- 分类规则：classCode 0x01 = Storage、0x02 = Network、0x03 = Display，其余 Pci。
- `os::enumeratePci` 最多枚举 64 个功能；每个设备创建 `PciDriver` 并走 Created → Initialized → Running 生命周期。
- `PciDriver` 提供 `readConfiguration` / `writeConfiguration` / `mapBar`。

## 2. IPC 请求（`os::DeviceRequest`）

| 请求 | 值 | 语义 |
|---|---|---|
| List | 1 | 回复设备数、驱动数、Storage/Network/Display 分类计数 |
| ListNode | 2 | `data[0]`=索引，枚举固定 12 个节点：`zero`、`cdrom`、`hda1`、`tty1`..`tty7`、`ptmx`、`pts`；`data[1]`=是否目录（仅 `pts`）；`data[2..7]`=名字 |
| ReadNode | 3 | 仅支持 `zero` 节点；`data[6]`=请求大小（≤4104 字节）；回复全零数据 |

其他类型回复 2。`/dev` 虚拟目录由 fs 服务基于这些接口合成（见 [fs.md](fs.md)）。

## 3. 输入子系统（InputService）

### IRQ 与 PS/2 控制器

- 绑定 **IRQ 1（键盘）与 IRQ 12（鼠标）**到专用端点；IRQ 线程收到 `0x495251`（"IRQ"）通知后最多排空 32 字节（状态寄存器 0x64 位 5 区分鼠标/键盘，位 0 有数据）。
- `Ps2Controller`（8042）初始化：清空输出缓冲 → 禁用两端口（0xAD/0xA7）→ 读配置字节（0x20）→ 置 IRQ1/IRQ2 并清位 4/5 → 写回（0x60）→ 启用（0xAE/0xA8）→ 鼠标滚轮探测（采样率 200/100/80 序列 + 0xF2 读设备 ID，ID=3 则 4 字节包）→ 键盘/鼠标发 0xF4 开启扫描。

### 键盘驱动（KeyboardDriver）

- Scan code set 1 翻译，含 0xE0 扩展前缀（右 Ctrl/右 Alt、方向键、Insert/Delete/Home/End/PageUp/PageDown 等）。
- 维护修饰键状态：Shift/Ctrl/Alt 按下态，CapsLock/NumLock/ScrollLock 切换态。
- 产生 `KeyDown`/`KeyUp` 事件。

### 鼠标驱动（MouseDriver）

- 3/4 字节包组装；首字节必须置位 0x08 否则丢弃（重新同步）。
- `deltaX = 字节2`、`deltaY = -字节3`（有符号）；滚轮 = 第 4 字节低 4 位符号扩展。
- 加速度：|delta| ≥ 8 → ×4、≥ 3 → ×3、否则 ×1。
- 按钮位 0-2 产生 Move/Wheel/ButtonDown/ButtonUp 事件。

### 事件队列（InputQueue）

- 容量 256 的环形事件队列 + 最多 4 个订阅者（`Subscriber { processId, nextSequence, wakeEndpoint, notified }`）。
- 连续 MouseMove/MouseWheel 在无订阅者追平时合并增量；队列满时丢弃最旧事件。

### IPC 请求（`os::InputRequest`，基值 0x200）

| 请求 | 值 | 语义 |
|---|---|---|
| GetEvent | 0x200 | 按 `senderPid` 出队；无事件回复 `WouldBlock`；有事件则把 `InputEvent` 拷入 `data` |
| GetState | 0x201 | 回复该订阅者积压事件数 |
| Subscribe | 0x202 | `handles[0]`=唤醒端点；有新事件时发送 `0x494e505554`（"INPUT"）通知；发送失败自动退订 |

### 输入事件结构（`include/os/input.h`）

```cpp
enum class InputEventType : uint32_t { None, KeyDown, KeyUp, MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel };
struct KeyEvent { KeyCode keyCode; uint32_t modifiers; bool pressed; uint8_t reserved[3]; };
struct MouseEvent { int32_t deltaX, deltaY, wheel; uint32_t buttons; };
struct InputEvent { InputEventType type; uint32_t reserved; uint64_t timestamp; union { KeyEvent key; MouseEvent mouse; }; };
```

`KeyCode` 共 98 个键码（US 布局）；修饰键位：Shift=1<<0、Control=1<<1、Alt=1<<2、CapsLock=1<<3、NumLock=1<<4、ScrollLock=1<<5。

客户端 `InputClient`：`connect(service)` / `wait()` / `receive(event)`，RAII 持有唤醒端点。

## 4. 线程模型

主线程处理 DeviceRequest；`inputServiceMain`（请求循环）与 `inputIrqMain`（IRQ 循环）各占 64 KiB 栈（基址 `0x60000000` 与 `0x60010000`）。
