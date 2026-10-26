# os/pty.h


## 内容与边界

Pty 保存主从端点及终端配置，不增加只用于包一层类型的外壳类。create 创建主从端点、close 显式释放，adopt、borrow、releaseMaster、releaseSlave 明确交接引用。

refreshAttributes、applyAttributes 读取和提交终端模式；refreshWindowSize、applyWindowSize 处理字符网格及可选像素尺寸。getter 逐个读取端点、编号、模式、控制字符和尺寸；setter 分组修改待提交配置。setNonblocking、setForegroundGroup 是即时服务操作，声明中说明区别。

终端尺寸与图形 Window 对象无关，这里不包含窗口标题栏和拖动实现。

## 枚举

本头文件不公开枚举。功能操作使用各自的成员函数，不通过公开操作编号切换。

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

当前公共头文件没有声明需要公开的独立数值或字符串常量。

## 全局变量

当前公共头文件没有导出可写全局变量。需要共享的初始化或缓存状态放在实现中，不通过应用头文件开放可写变量。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Pty
这个类表示一对伪终端端点及终端设置。

#### 构造与析构

```cpp
~Pty();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Pty();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Pty& operator=(Pty&&) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Pty&&`：另一个同类对象；移动时交接资源，禁止复制的声明不能调用。

结果：返回本对象的引用。

#### 操作

```cpp
uint64_t releaseMaster();
```

用途：交出 PTY 主端，调用者接管释放责任。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
uint64_t releaseSlave();
```

用途：交出 PTY 从端，调用者接管释放责任。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
bool refreshAttributes();
```

用途：读取终端当前行模式、回显和控制字符，保存在本地待编辑配置中。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
bool applyAttributes();
```

用途：把本地修改后的终端模式和控制字符一次提交给 tty 服务。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
bool refreshWindowSize();
```

用途：读取终端字符行列数和可选像素尺寸。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
bool applyWindowSize();
```

用途：把本地尺寸设置提交给 tty 服务，由服务通知相关进程。

参数：无。

结果：返回 true 或 false，具体含义见用途。

```cpp
int adopt(uint64_t master, uint64_t slave);
```

用途：接管已有引用，之后由本对象负责释放；接管失败时原所有者继续负责。

参数：

- `uint64_t master`：PTY 主端引用，通常由终端模拟器用于与从端程序交换数据。
- `uint64_t slave`：PTY 从端引用，通常作为 shell 的标准输入输出。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int borrow(uint64_t master);
```

用途：临时使用已有引用，析构时不关闭借用资源。

参数：

- `uint64_t master`：PTY 主端引用，通常由终端模拟器用于与从端程序交换数据。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int queryForegroundGroup(uint64_t& group);
```

用途：即时查询终端当前前台进程组。

参数：

- `uint64_t& group`：进程组编号，或指定索引处的用户组；以函数签名为准。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(Ipc::Handle&& master);
```

用途：检查并接管已有 PTY 主端。

参数：

- `Ipc::Handle&& master`：PTY 主端引用，通常由终端模拟器用于与从端程序交换数据。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int create();
```

用途：申请一对 PTY 主从端点；失败时回收本次已申请的临时引用。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


```cpp
int close();
```

用途：释放本对象仍拥有的主从端引用，不释放已经交给其他所有者的端。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getMasterHandle() const;
```

数据：PTY 主端的借用句柄。

```cpp
uint64_t getSlaveHandle() const;
```

数据：PTY 从端的借用句柄。

```cpp
bool getCanonicalMode() const;
```

数据：是否按行提交输入。

```cpp
bool getEcho() const;
```

数据：输入字符是否回显。

```cpp
bool getSignalMode() const;
```

数据：是否把终端控制字符转换成信号。

```cpp
uint8_t getEraseCharacter() const;
```

数据：删除前一个输入字符的控制字节。

```cpp
uint8_t getInterruptCharacter() const;
```

数据：产生中断信号的控制字节。

```cpp
uint8_t getEofCharacter() const;
```

数据：表示输入结束的控制字节。

```cpp
uint8_t getSuspendCharacter() const;
```

数据：产生挂起信号的控制字节。

```cpp
uint8_t getQuitCharacter() const;
```

数据：产生退出信号的控制字节。

```cpp
uint32_t getRows() const;
```

数据：终端字符行数。

```cpp
uint32_t getColumns() const;
```

数据：终端字符列数。

```cpp
uint32_t getPixelWidth() const;
```

数据：终端可选的像素宽度。

```cpp
uint32_t getPixelHeight() const;
```

数据：终端可选的像素高度。

```cpp
uint32_t getNumber() const;
```

数据：PTY 编号。

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
bool setNonblocking(bool enabled);
```

数据：读写是否立即返回而不等待。

```cpp
bool setForegroundGroup(uint64_t group);
```

数据：控制终端前台进程组。

```cpp
void setCanonicalMode(bool value);
```

数据：是否按行提交输入。

```cpp
void setEcho(bool value);
```

数据：输入字符是否回显。

```cpp
void setSignalMode(bool value);
```

数据：是否把终端控制字符转换成信号。

```cpp
void setEraseCharacter(uint8_t value);
```

数据：删除前一个输入字符的控制字节。

```cpp
void setInterruptCharacter(uint8_t value);
```

数据：产生中断信号的控制字节。

```cpp
void setEofCharacter(uint8_t value);
```

数据：表示输入结束的控制字节。

```cpp
void setSuspendCharacter(uint8_t value);
```

数据：产生挂起信号的控制字节。

```cpp
void setQuitCharacter(uint8_t value);
```

数据：产生退出信号的控制字节。

```cpp
void setRows(uint32_t value);
```

数据：终端字符行数。

```cpp
void setColumns(uint32_t value);
```

数据：终端字符列数。

```cpp
void setPixelWidth(uint32_t value);
```

数据：终端可选的像素宽度。

```cpp
void setPixelHeight(uint32_t value);
```

数据：终端可选的像素高度。

## 实现文件

`lib/os/pty.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
