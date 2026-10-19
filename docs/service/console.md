# console 服务（src/console/）

console 服务（注册名同时为 `"console"` 与 `"terminal"`，同一端点）实现 6 个文本终端（tty1-tty6）与图形会话（tty7）切换、VGA 字体软件渲染、ANSI 转义序列与行编辑。由固定 boot ConsoleServer 模块在 Ring3 加载并授予 Display 能力，磁盘副本不获得特权。

## 1. 常量与结构

| 常量 | 值 | 说明 |
|---|---|---|
| `framebufferAddress` | `0x51000000` | 帧缓冲映射地址 |
| `inputStackAddress` | `0x62000000` | 输入线程栈（64 KiB） |
| 字符单元 | 8×16 | 使用 `os::consoleFont` 位图 |
| `maximumColumns` / `maximumRows` | 128 / 48 | 文本网格上限 |
| `inputCapacity` | 512 | 输入环形缓冲 |
| `lineCapacity` | 256 | 行编辑缓冲 |
| `historyCapacity` | 16 | 输入历史条数 |

```cpp
struct Cell { char character; uint8_t foreground, background, attributes; };
// attributes: bit 1 = 高亮，bit 2 = 下划线
```

## 2. 显示层（DisplayConsole）

- `mapFramebuffer` 获取 `DisplayInfo`（要求 24/32 bpp）；列数 = width/8，行数 = height/16。
- `render()` 脏单元增量重绘：与 `previousCells_` 逐格比较，光标位置强制重绘。
- `drawCell()` 用 8×16 位图逐像素绘制，光标处反色。
- `drawPixel()` 按 `redPosition/greenPosition/bluePosition` 打包像素。
- 16 色调色板（8 普通 + 8 高亮），如普通白 `0xd9dde3`、高亮白 `0xffffff`。

## 3. 终端（Tty，每个文本终端一个）

- **输出**：处理 `\r \n \b \t`（制表位 4 列）与可打印字符 32-126；越界整屏上移一行。
- **ANSI 状态机**：TTY 和图形终端共用 os/ansi.h 的 CSI/SGR 解析器，转义序列可跨 IPC/PTY 读取边界。支持光标移动、定位、清屏、清行尾，以及多参数 SGR：复位、高亮、下划线、反色、普通/高亮前景和背景、默认色恢复。单条 CSI 最多 8 个参数，非法或过长序列会丢弃。
- **行编辑**：插入/覆盖模式（Insert 切换）、光标左右移动、Home/End、Backspace/Delete、Tab=4 空格、上下方向键历史回溯（16 条）。
- **控制键**：Ctrl+C → SIGINT、Ctrl+Z → SIGTSTP（发往前台进程）、Ctrl+D → EOF、Ctrl+L → 清屏。
- **键盘映射**：US 布局，含移位符号、小键盘（NumLock 区分）、大小写锁定。

## 4. IPC 请求（`os::TerminalRequest`，基值 0x300）

| 请求 | 值 | 语义 |
|---|---|---|
| Write | 0x300 | `data[0]`=终端号、`data[1]`=长度（≤512）、`data[2..]`=数据；校验实际消息长度，活动终端按批重绘 |
| Read | 0x301 | `data[0]`=终端号、`data[1]`=容量（≤48）；回复数据在 `data[2..]` |
| GetActive | 0x302 | 回复活动终端号 |
| Switch | 0x303 | `data[0]`=目标终端（0..6） |
| GetInfo | 0x304 | 回复终端数、图形终端 id、列数、行数 |
| SetForegroundTask | 0x305 | `data[0]`=终端、`data[1]`=进程 ID（Ctrl+C 信号投递目标） |

## 5. 会话切换

`switchLocked()`：目标 ≤6；切换前调用 guiw 服务的 `setWindowSessionActive`（切到 tty7 激活 GUI 会话，切走时停用）；切到文本终端则失效显示缓存并重绘。快捷键 **Ctrl+Shift+F1..F7** 切换 tty1..tty7。

## 6. 配置与线程

- 启动时读 `/etc/console.conf`（≤256 字节），解析 `defaultTerminal=ttyN`（1-7）与 `serialConsole=on`。
- 线程模型：主线程跑 IPC 循环；`inputMain` 线程经 `os::InputClient` 订阅 input 服务，把 `KeyDown` 事件送入活动文本终端。全部状态由 `os::SpinLock` 保护。
