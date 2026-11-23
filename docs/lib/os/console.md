# 控制台

## 枚举

### os::Console::EscapeParser::Result

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Text` | `0` | 当前字节作为普通文字处理 |
| `Pending` | `1` | 转义序列未完整 |
| `Command` | `2` | 已收齐命令和参数 |

### os::GraphicalDisplayRequest

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `SetActive` | `781` | 设置图形显示活动状态 |
| `GetActive` | `1196622848` | 查询图形显示活动状态 |

### os::GraphicalPhase

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Starting` | `0` | 启动中 |
| `Greeter` | `1` | 登录界面 |
| `Authenticating` | `2` | 认证中 |
| `Desktop` | `3` | 桌面 |
| `Suspended` | `4` | 已挂起 |

### os::GraphicalRequest

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Bind` | `1196622592` | 绑定会话控制进程 |
| `State` | `1196622593` | 查询会话状态 |
| `GreeterReady` | `1196622594` | 登录界面就绪 |
| `Authenticated` | `1196622595` | 身份认证完成 |
| `DesktopReady` | `1196622596` | 桌面就绪 |
| `Reset` | `1196622597` | 重置会话 |
| `Suspend` | `1196622598` | 挂起会话 |
| `Resume` | `1196622599` | 恢复会话 |

## 常量

| 名称 | 值 | 说明 |
| --- | --- | --- |
| `os::textTerminalCount` | `6` | os::textTerminalCount |
| `os::graphicalTerminalId` | `6` | os::graphicalTerminalId |

## 对象

### os::Console

控制台对象，提供终端输入、输出和显示设置。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Console()` | 构造Console对象 | — |
| `~Console()` | 析构Console对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Console& operator=(Console&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int open(uint32_t id)` | 打开对象连接 | `id`：终端编号 |
| `int openCurrent()` | 绑定当前进程使用的控制台 | — |
| `int close()` | 关闭对象连接 | — |
| `int64_t read(void* buffer, size_t size)` | 读取控制台输入 | `buffer`：接收读取结果的缓冲区<br>`size`：数据大小，单位为字节 |
| `int64_t write(const void* buffer, size_t size)` | 输出控制台文字 | `buffer`：待写入的数据缓冲区<br>`size`：数据大小，单位为字节 |
| `int activate()` | 把本对象绑定的控制台切换到显示前台 | — |
| `int refresh()` | 更新对象属性 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint32_t getId() const` | 读取编号 | — |
| `bool getOpened() const` | 读取是否已经打开 | — |
| `bool getActive() const` | 读取是否是当前正在显示的控制台 | — |
| `bool getEcho() const` | 读取输入字符是否回显 | — |
| `uint64_t getForegroundGroup() const` | 读取控制终端的前台进程组编号 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setEcho(bool enabled)` | 设置输入字符是否回显 | `enabled`：功能开关 |
| `int setForegroundGroup(uint64_t group)` | 设置控制终端的前台进程组编号 | `group`：进程组编号 |

#### 静态成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `static uint32_t terminalColor(uint8_t index, bool bright = false)` | 将终端颜色索引转换为 RGB 像素颜色 | `index`：终端颜色索引<br>`bright`：使用高亮颜色 |

### os::Console::EscapeParser

终端转义序列解析对象，保存解析进度和参数。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Result feed(uint8_t value)` | 解析输入的转义序列字节 | `value`：待解析的输入字节 |
| `void reset()` | 重置对象状态 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `char getCommand() const` | 读取命令 | — |
| `uint32_t getParameterCount() const` | 读取转义序列参数数量 | — |
| `uint16_t getParameter(uint32_t index) const` | 读取转义序列参数 | `index`：转义序列参数的索引 |

### os::Console::Style

终端文字样式对象，保存前景色、背景色和显示属性。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void apply(uint16_t value)` | 应用终端文字样式 | `value`：编码颜色和文字属性的样式值 |
| `void reset()` | 重置对象状态 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint8_t getForeground() const` | 读取前景色 | — |
| `uint8_t getBackground() const` | 读取背景色 | — |
| `uint8_t getAttributes() const` | 读取属性 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void setForeground(uint8_t value)` | 设置前景色 | `value`：前景色 |
| `void setBackground(uint8_t value)` | 设置背景色 | `value`：背景色 |
| `void setAttributes(uint8_t value)` | 设置属性 | `value`：属性 |

### os::BootConsole

启动终端连接对象，持有进度报告和显示交接使用的端点。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit BootConsole(uint64_t endpoint, bool owned = false)` | 构造BootConsole对象 | `endpoint`：消息端点句柄<br>`owned`：由对象管理句柄的释放 |
| `~BootConsole() = default` | 析构BootConsole对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool report(const char* name, bool started, int32_t error = 0)` | 输出启动状态 | `name`：对象名称<br>`started`：服务启动状态或启动时间<br>`error`：错误码 |
| `bool finish(uint32_t terminal)` | 完成启动终端交接 | `terminal`：目标终端编号 |
| `bool state(bool& booting, uint32_t& preferredTerminal)` | 读取会话状态 | `booting`：接收系统启动状态<br>`preferredTerminal`：接收首选终端编号 |

### os::DisplayHandoff

显示交接连接对象，持有窗口服务端点。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit DisplayHandoff(uint64_t windowService, bool owned = false)` | 构造DisplayHandoff对象 | `windowService`：窗口服务端点<br>`owned`：由对象管理句柄的释放 |
| `~DisplayHandoff() = default` | 析构DisplayHandoff对象 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool setGraphicalActive(bool active)` | 设置图形界面活动状态 | `active`：图形界面活动状态 |

### os::GraphicalSession

图形会话连接对象，持有会话查询和控制使用的端点。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit GraphicalSession(uint64_t endpoint, bool owned = false)` | 构造GraphicalSession对象 | `endpoint`：消息端点句柄<br>`owned`：由对象管理句柄的释放 |
| `~GraphicalSession() = default` | 析构GraphicalSession对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool bind(uint64_t task, uint64_t started)` | 绑定对象资源 | `task`：目标进程<br>`started`：服务启动状态或启动时间 |
| `bool state(GraphicalPhase& phase, uint64_t& controller)` | 读取会话状态 | `phase`：接收图形会话阶段<br>`controller`：接收会话控制端点 |
| `bool command(GraphicalRequest operation, uint64_t window = 0)` | 执行图形会话命令 | `operation`：文件操作对象<br>`window`：窗口服务端点 |
