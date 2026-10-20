# guiw 服务（独立项目 D:/osinfo/guiw/server/）

guiw 服务（注册名 `"guiw"`）是窗口服务器/合成器：软件合成、窗口管理、输入分发与内置桌面演示控件。它与 `/usr/bin/terminal` 一同由 guiw 软件包交付，程序本体安装在 `/sbin/sys/guiw.elf`，授予 `taskCapabilityDisplay`。

## 1. 常量

| 常量 | 值 | 说明 |
|---|---|---|
| `windowCapacity` | 8 | 最大窗口数 |
| `eventCapacity` | 32 | 每窗口事件队列容量 |
| `maximumSurfaceWidth/Height` | 704 / 448 | 表面尺寸上限（静态数组存储） |
| `titleBarHeight` | 22 | 标题栏高度 |
| 帧缓冲映射 | `0x50000000` | |
| `frameIntervalNanoseconds` | 16,666,667 | ≈60 Hz 帧率限制 |

## 2. 核心结构

- `Rectangle { int32_t x, y, width, height; }` + `intersects/intersection`（脏区裁剪）。
- `Window`：handle、ownerPid、x/y、active、visible、layer、zOrder、title[48]、Surface、EventQueue、eventEndpoint、eventWaiting。
- `DisplayDriver`：`mapFramebuffer` 获取 `DisplayInfo`（24/32 bpp）；`drawPixel` 按 RGB 位置打包写帧缓冲。
- `Surface`（实现 `window::WidgetCanvas`）：固定 stride=704 的 XRGB 像素缓冲；`resize` 填充背景 `0xf4f4f4`；提供 `drawPixel/fillRectangle/drawText`（8×16 `consoleFont`）/`drawLine`（Bresenham）。
- `EventQueue`：FIFO 32 项；满时若新事件与末事件均为 MouseMove 则合并替换。

## 3. IPC 请求（`os::WindowRequest`，基值 0x300）

| 请求 | 值 | 语义 |
|---|---|---|
| Create | 0x300 | `data[0]`=宽、`data[1]`=高、`data[2..]`=标题（≤47）；初始位置 `(40+28i, 70+24i)`、不可见、Normal 层；回复句柄 |
| Destroy | 0x301 | 关闭事件端点、置非活动、清除焦点 |
| Show / Hide | 0x302 / 0x303 | Show 同时获取焦点 |
| Resize | 0x304 | 成功后推 `WindowResize` 事件并钳制窗口位置 |
| Move | 0x305 | 设置位置并钳制（x≥0、y≥22、不超出屏幕） |
| DrawPixel | 0x306 | x, y, color |
| DrawLine | 0x307 | x0, y0, x1, y1, color |
| FillRectangle | 0x308 | x, y, w, h, color |
| DrawText | 0x309 | x, y, color@data[3], 文本@data[4..] |
| Present | 0x30a | 标记脏区，触发合成 |
| GetEvent | 0x30b | 弹出事件；空且已订阅则置 eventWaiting 并回复 WouldBlock |
| GetDisplayInfo | 0x30c | 回复宽、高、bpp、PixelFormat |
| SetSessionActive | 0x30d | console 切换 tty7/离开时调用；激活时整屏标脏并立即出帧 |
| SetLayer | 0x30e | Background=0 / Normal=1 / Overlay=2，刷新 zOrder |
| Raise | 0x30f | 获取焦点并置顶 |
| SubscribeEvents | 0x310 | `handles[0]`=唤醒端点；有事件时发送 `0x57494e4556454e54`（"WINEVENT"） |

绘制类请求（DrawPixel/DrawLine/FillRectangle/DrawText）都绘制到**离屏表面**，需 `Present` 才上屏。所有按句柄的操作都经 `findOwned(senderPid, handle)` 校验所有权。

### 窗口事件（`include/osgui-c++/osgui/gui.h`）

```cpp
enum class WindowEventType : uint32_t {
    None, KeyDown, KeyUp, MouseMove, MouseButtonDown, MouseButtonUp,
    MouseWheel, WindowResize, WindowClose, WindowFocus
};
struct WindowEvent {   // 40 字节
    WindowEventType type; uint32_t modifiers; uint64_t timestamp;
    int32_t x, y, delta; KeyCode keyCode; uint32_t buttons; uint32_t reserved;
};
```

## 4. 合成（`GraphicsServer::compose`）

1. 脏区与屏幕求交 → 填充桌面背景色 `0x272a30`；
2. 按 `(layer << 56) | zOrder` 键做选择排序式逐层绘制；
3. 最后画光标。

`drawWindow`：窗口边框为表面外扩 2 像素 + 22 像素标题栏；焦点窗口边框 `0x3aa675`/标题 `0x1c704d`（绿色调），非焦点 `0x6a7079`/`0x464b52`（灰色调）；标题文字用内置 5×7 迷你字体（仅 O/S/I/N/F/W/D/E/M 字形 + 回退矩形），最多 20 字符；内容区逐像素从表面 blit。光标为 13×13 十字（白臂黑心）。

## 5. 输入分发（`handleInput`）

- 仅在 `sessionActive_`（tty7 激活）时处理。
- MouseMove：更新光标（钳制在屏内）、命中测试。
- MouseButtonDown：命中窗口获取焦点。
- 命中测试覆盖标题栏区域（y 从 `window.y - 22` 起），按 layer + zOrder 取最上层。
- **系统窗口**（ownerPid == 0，即内置桌面窗口）：直接由控件处理；**客户窗口**：把 `InputEvent` 翻译为 `WindowEvent`（坐标转为窗口局部坐标）推入该窗口事件队列并唤醒等待者。

## 6. 内置桌面

`initializeDesktopLocked`：

- 窗口 0："Welcome to osinfo"（80,105，360×220），含 Label "OSINFO DESKTOP"、TextBox "Type here"、Button "OPEN"。
- 窗口 1："System status"（485,175，260×160），含 Label "SYSTEM ONLINE"、"4 CPU  64-BIT" 及进度条。

## 7. 控件库（widget.h/widget.cc，`namespace window`）

```cpp
struct WidgetBounds { int32_t x, y; uint32_t width, height; };
class WidgetCanvas { virtual void fillRectangle(...); virtual void drawText(...); };
class Widget { init/render/handlePointer/handleKey/contains; };
class Label;    // 文本 ≤47 字节 + 颜色
class Button;   // 文本 ≤31 字节；按下态 0x1f6f50、常态 0x2b8a65、顶部高光 0x6fd0aa
class TextBox;  // 文本 ≤31 字节；焦点态白底/绿边，非焦点灰底/灰边；支持退格与字符输入
```

## 8. 线程模型

主线程跑 IPC；`inputMain` 订阅 input 服务（栈 `0x61000000`）；`compositorMain` 每 1 ms 调 `refresh()`（脏区存在且距上帧 ≥16.67 ms 时合成，栈 `0x62000000`）。全部状态由自旋锁保护；脏区管理为单一合并矩形（`markDirtyLocked` 做并集）。

窗口服务由 init 根据 /etc/services 从 /sbin/sys/guiw.elf 受控加载，角色 Window、Display 能力；独立项目 `src/main.cc` 为普通 Ring3 终端客户端。普通应用不能通过磁盘路径申请 Display 能力。OS 仓库安装 `packages/guiw.inpk`，不编译 guiw 源码。

事件通知采用 deadline=0 的非阻塞发送。队列满时保留等待状态，由合成线程在后续 refresh 重试，禁止在全局锁内等待客户端消费通知。成功领取事件会清除等待状态；不可恢复的端点错误关闭订阅。`tests/desktop-runtime.cc` 覆盖通知队列填满、窗口 RPC 继续响应及清空后补发。
