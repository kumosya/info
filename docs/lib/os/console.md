# os/console.h


## 内容与边界

### Console

openCurrent 或 open(id) 绑定控制台，close 返回释放连接的结果。read、write 传输文本；activate 激活自身；refresh 更新状态。getter 读取编号、活动状态、回显和前台进程组；setEcho、setForegroundGroup 提交真实修改。

### Console::EscapeParser

EscapeParser 是终端 ANSI 转义序列的增量解析器。例如 ESC[31m 表示设置红色，ESC[2J 表示清屏；解析器识别命令及参数，由终端执行相应显示操作，不由解析器直接绘图。

一次 write 可能只收到 ESC[，下一次才收到 31m，因此对象必须保存未完成的解析进度。feed 输入字节，reset 清除解析状态；getCommand、getParameterCount、getParameter 读取结果。

当前 console 服务和 guiw terminal 都有实际调用，所以保留可复用解析能力。它不是键盘 Esc 键处理，也不是窗口事件解析器。

### Console::Style 与颜色转换

Style 保存前景色索引、背景色索引和文本属性，apply 应用 SGR 参数，reset 恢复默认，getter、setter 分组读写样式字段。

不保留 Palette 类。颜色索引转换作为 Console 的静态成员声明在 console.h：

```cpp
static uint32_t terminalColor(uint8_t index, bool bright = false);
```

实现在 console.cc，通过 Console::terminalColor(index, bright) 调用，不需要创建 Console 对象。输出格式在接口中固定，供 console 与图形终端复用。启动进度和 TTY 交接协议仍放在 include/service/console/。

## 枚举

### Console::EscapeParser::Result

底层类型：`int`。feed 返回的解析状态。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Text` | `0` | 当前字节作为普通文字处理 |
| `Pending` | `1` | 转义序列未完整，等待后续字节 |
| `Command` | `2` | 已收齐命令和参数 |

### 当前实现中的进程级终端选择

| 当前实现名字 | 初始值 | 保存什么 | 推荐可见性 |
| --- | --- | --- | --- |
| processConsoleId | 0 | 当前进程默认使用的控制台编号，新 Console 对象据此选择终端 | 留在运行时内部，不在 console.h 导出可写变量 |

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Console
这个类表示一个控制台的输入、输出、激活和终端设置。

#### 构造与析构

```cpp
~Console();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Console();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Console& operator=(Console&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Console&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int64_t read(void* buffer, size_t size);
```

用途：从本对象绑定的控制台读取输入字节，复制到 buffer；是否等待由控制台模式决定。

参数：

- `void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
int64_t write(const void* buffer, size_t size);
```

用途：向本对象绑定的控制台输出 size 个字节，返回实际写入数量。

参数：

- `const void* buffer`：存放读入数据或提供待写数据的内存；读操作要求可写，写操作只读取。
- `size_t size`：本次数据的字节数。

结果：数据操作返回实际长度或条目数；负值表示错误，零可能表示 EOF 或暂时没有结果，以所属接口为准。

```cpp
int activate();
```

用途：把本对象绑定的控制台切换到显示前台。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
static uint32_t terminalColor(uint8_t index, bool bright = false);
```

用途：Console::terminalColor 把终端颜色索引转换成固定 RGB 格式的像素颜色，供 console 服务和 GUI 终端共同使用。

参数：

- `index`：终端基础颜色索引；范围与支持的 ANSI 颜色表一致，越界行为明确返回默认色。
- `bright`：true 使用明亮颜色组，false 使用普通颜色组。

结果：返回约定格式的颜色值。不访问服务，也不依赖内核专用 graphic 库。

```cpp
int open(uint32_t id);
```

用途：绑定给定控制台并取得连接。

参数：

- `uint32_t id`：要绑定的资源编号，含义由所属类确定。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int openCurrent();
```

用途：绑定当前进程使用的控制台，连接或权限失败直接返回错误。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放控制台连接，不关闭整个控制台服务。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint32_t getId() const;
```

数据：编号。

```cpp
bool getOpened() const;
```

数据：是否已经打开。

```cpp
bool getActive() const;
```

数据：是否是当前正在显示的控制台。

```cpp
bool getEcho() const;
```

数据：输入字符是否回显。

```cpp
uint64_t getForegroundGroup() const;
```

数据：控制终端的前台进程组编号。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

#### setter

```cpp
int setEcho(bool enabled);
```

数据：输入字符是否回显。

```cpp
int setForegroundGroup(uint64_t group);
```

数据：控制终端前台进程组。

## 实现文件

`lib/os/console.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
