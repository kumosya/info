# device 服务（src/device/）

device 服务（注册名 `"device"`）由内核内置镜像加载，授予 `taskCapabilityDevice`，以固定 boot 角色在 Ring3 运行。同一进程内还承载 **input 服务**（注册名 `"input"`）。

## 1. PCI 设备管理

独立的 `pci` 服务由固定 boot 镜像在 Ring3 启动，注册名为 `"pci"`。device/storage 通过 IPC 请求枚举和配置访问；只有 pci 服务持有 `taskCapabilityPci`，可通过已有 IPC syscall 向 `pci.kernel` 提交请求。PCI 配置端口操作由内核 `PciAccess` 完成。

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
| ListNode | 2 | 枚举 zero、null、tty1..tty7、ptmx、pts；块节点由 fs 根据 storage 的探测结果生成 |
| ReadNode | 3 | 仅支持 `zero` 节点；`data[6]`=请求大小（≤4104 字节）；回复全零数据 |

其他类型回复 2。`/dev` 虚拟目录由 fs 服务基于这些接口合成（见 [fs.md](../fs/README.md)）。

## 3. 输入子系统（InputService）

### IRQ 与 PS/2 控制器

- 绑定 **IRQ 1（键盘）与 IRQ 12（鼠标）**到专用端点；IRQ 线程收到 `0x495251`（"IRQ"）通知后持续排空已缓存字节，每 64 字节让出一次 CPU，避免边沿触发中断不再到达而遗留输入。
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

- 容量 256 的环形事件队列 + 最多 4 个订阅者（`Subscriber { taskId, nextSequence, wakeEndpoint, notified }`）。
- 连续 MouseMove/MouseWheel 在无订阅者追平时合并增量；队列满时丢弃最旧事件。

### IPC 请求（`os::InputRequest`，基值 0x200）

| 请求 | 值 | 语义 |
|---|---|---|
| GetEvent | 0x200 | 按 `senderPid` 出队；无事件回复 `WouldBlock`；有事件则把 `InputEvent` 拷入 `data` |
| GetState | 0x201 | 回复该订阅者积压事件数 |
| Subscribe | 0x202 | `handles[0]`=唤醒端点；有新事件时发送 `0x494e505554`（"INPUT"）通知；发送失败自动退订 |

### 输入事件结构（`include/os-c++/os/input.h`）

```cpp
enum class InputEventType : uint32_t { None, KeyDown, KeyUp, MouseMove, MouseButtonDown, MouseButtonUp, MouseWheel };
struct KeyEvent { KeyCode keyCode; uint32_t modifiers; bool pressed; uint8_t reserved[3]; };
struct MouseEvent { int32_t deltaX, deltaY, wheel; uint32_t buttons; };
struct InputEvent { InputEventType type; uint32_t reserved; uint64_t timestamp; union { KeyEvent key; MouseEvent mouse; }; };
```

`KeyCode` 共 98 个键码（US 布局）；修饰键位：Shift=1<<0、Control=1<<1、Alt=1<<2、CapsLock=1<<3、NumLock=1<<4、ScrollLock=1<<5。

客户端 `InputClient`：`connect(service)` / `wait()` / `receive(event)`，RAII 持有唤醒端点。

## 4. 线程模型

主线程处理 DeviceRequest；`inputServiceMain`（请求循环）与 `inputIrqMain`（IRQ 循环）各占 64 KiB 栈（基址 `0x60000000` 与 `0x60010000`）。另有独立生命周期线程监护派生服务。

块设备发现、稳定标识及别名见 [fs 服务](../fs/README.md)。

## 服务执行级别与内核机制授权

所有服务进程都在 Ring3 运行。固定 boot 模块的内核 ServiceRole 和 capabilities 决定可调用的机制；磁盘路径、root 身份或普通请求参数不能自行获得这些权限。

PCI 服务通过 pci.kernel 进行受限配置空间操作；device 服务通过 device.kernel 初始化 PS/2 并读取输入字节。存储驱动的 MMIO、DMA 与 IRQ 同样由 device.kernel 验证并提供。OS 库不再提供原始端口读写 API，没有端口 syscall。

纯光盘根卷由 GRUB 加载为 rootfs 内存模块。device.kernel 的 BootImageInfo/Read/Write 仅允许带设备能力的固定 Storage 身份访问这个模块，按块校验边界；实现放在 `src/kernel/device/storage/boot_image.cc`。Ring3 storage 提供标准块设备协议，普通程序无法通过该接口获得任意物理内存访问，也没有新增通用 map 服务。

硬件端口实现仅存在于内核；设备枚举、驱动匹配、键鼠解析和输入事件队列都在 Ring3 服务中。

源码归属：Ring3 设备服务在 `src/device/`，PCI 在 `src/device/pci/`，存储在 `src/device/storage/`；Ring0 通用设备服务在 `src/kernel/device/service/`，PCI 在 `src/kernel/device/pci/`。CS 级别检查仅在内部服务入口 `src/service/start.S`，公共 os/ 头文件不包含检查或硬件 I/O 实现。

## 启动、背压和生命周期

init 创建 device；device 实际创建并持有 pci、storage 的任务句柄，先等 pci，再等 storage，就绪后完成自身枚举、驱动匹配和 input 初始化。控制端点为 control.device；init 不重复监护两个派生进程。默认依赖 task 子服务已经就绪，父子归属不等同于循环 ready 依赖。

input 是 device 同进程承载的子系统，不虚构独立进程。队列锁只保护序号和订阅状态；通知使用 deadline=0 非阻塞发送。WouldBlock/Timeout/Interrupted 保留订阅并清通知标记，由请求循环最多 10 ms 后重试；永久端点错误才退订并关闭持有句柄。慢消费者不能阻塞 IRQ 线程，也不能让键盘和 TTY 切换随 GUI 一起停住。

旧实现在订阅通知队列填满后阻塞 IRQ 线程。`tests/input-backpressure.cc` 故意保留不收消息的活动订阅者；`tests/input-backpressure.sh` 再通过 QEMU 注入鼠标和 Ctrl+Shift+F1/F7，验证其他客户端继续工作。该场景的修复前后证据见 [进度](../../prompt/v0.1-service-task-runtime-progress.md)。

device 是基础引导服务，目前 systemctl 拒绝停止/重启/禁用，防止留下孤儿子服务和无法恢复的设备状态。端点重建不恢复 IRQ、DMA 或驱动队列；客户端必须区分失效和无权限，不能无限重放写入。机制详见 [device.kernel](device.kernel/README.md)。
