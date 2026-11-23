# 伪终端

## 对象

### os::Pty

伪终端对象，连接主从端并管理终端属性。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Pty()` | 构造Pty对象 | — |
| `~Pty()` | 析构Pty对象 | — |
| `Pty(Pty&&) noexcept` | 构造Pty对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int adopt(uint64_t master, uint64_t slave)` | 接管资源句柄 | `master`：伪终端主端句柄<br>`slave`：伪终端从端句柄 |
| `int borrow(uint64_t master)` | 借用资源句柄 | `master`：伪终端主端句柄 |
| `int open(Ipc::Handle&& master)` | 打开对象连接 | `master`：伪终端主端句柄 |
| `int close()` | 关闭对象连接 | — |
| `int queryForegroundGroup(uint64_t& group)` | 即时查询终端当前前台进程组 | `group`：进程组编号 |
| `Pty& operator=(Pty&&) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int create()` | 创建伪终端主从通道 | — |
| `uint64_t releaseMaster()` | 交出PTY主端 | — |
| `uint64_t releaseSlave()` | 交出PTY从端 | — |
| `bool refreshAttributes()` | 读取终端当前行模式、回显和控制字符 | — |
| `bool applyAttributes()` | 把本地修改后的终端模式和控制字符一次提交给tty服务 | — |
| `bool refreshWindowSize()` | 读取终端字符行列数和可选像素尺寸 | — |
| `bool applyWindowSize()` | 把本地尺寸设置提交给tty服务 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getMasterHandle() const` | 读取PTY主端的借用句柄 | — |
| `uint64_t getSlaveHandle() const` | 读取PTY从端的借用句柄 | — |
| `uint32_t getNumber() const` | 读取PTY编号 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
| `bool getCanonicalMode() const` | 读取是否按行提交输入 | — |
| `bool getEcho() const` | 读取输入字符是否回显 | — |
| `bool getSignalMode() const` | 读取是否把终端控制字符转换成信号 | — |
| `uint8_t getEraseCharacter() const` | 读取删除前一个输入字符的控制字节 | — |
| `uint8_t getInterruptCharacter() const` | 读取产生中断信号的控制字节 | — |
| `uint8_t getEofCharacter() const` | 读取表示输入结束的控制字节 | — |
| `uint8_t getSuspendCharacter() const` | 读取产生挂起信号的控制字节 | — |
| `uint8_t getQuitCharacter() const` | 读取产生退出信号的控制字节 | — |
| `uint32_t getRows() const` | 读取终端字符行数 | — |
| `uint32_t getColumns() const` | 读取终端字符列数 | — |
| `uint32_t getPixelWidth() const` | 读取终端可选的像素宽度 | — |
| `uint32_t getPixelHeight() const` | 读取终端可选的像素高度 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool setNonblocking(bool enabled)` | 设置读写是否立即返回而不等待 | `enabled`：功能开关 |
| `bool setForegroundGroup(uint64_t group)` | 设置控制终端的前台进程组编号 | `group`：进程组编号 |
| `void setCanonicalMode(bool value)` | 设置是否按行提交输入 | `value`：是否按行提交输入 |
| `void setEcho(bool value)` | 设置输入字符是否回显 | `value`：输入字符是否回显 |
| `void setSignalMode(bool value)` | 设置是否把终端控制字符转换成信号 | `value`：是否把终端控制字符转换成信号 |
| `void setEraseCharacter(uint8_t value)` | 设置删除前一个输入字符的控制字节 | `value`：删除前一个输入字符的控制字节 |
| `void setInterruptCharacter(uint8_t value)` | 设置产生中断信号的控制字节 | `value`：产生中断信号的控制字节 |
| `void setEofCharacter(uint8_t value)` | 设置表示输入结束的控制字节 | `value`：表示输入结束的控制字节 |
| `void setSuspendCharacter(uint8_t value)` | 设置产生挂起信号的控制字节 | `value`：产生挂起信号的控制字节 |
| `void setQuitCharacter(uint8_t value)` | 设置产生退出信号的控制字节 | `value`：产生退出信号的控制字节 |
| `void setRows(uint32_t value)` | 设置终端字符行数 | `value`：终端字符行数 |
| `void setColumns(uint32_t value)` | 设置终端字符列数 | `value`：终端字符列数 |
| `void setPixelWidth(uint32_t value)` | 设置终端可选的像素宽度 | `value`：终端可选的像素宽度 |
| `void setPixelHeight(uint32_t value)` | 设置终端可选的像素高度 | `value`：终端可选的像素高度 |
