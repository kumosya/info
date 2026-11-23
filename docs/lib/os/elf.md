# ELF 映像

## 对象

### os::Executable

ELF 映像对象，保存文件头、程序段和加载信息。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Executable()` | 构造Executable对象 | — |
| `~Executable()` | 析构Executable对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int open(const void* image, size_t size)` | 打开对象连接 | `image`：程序映像或执行选项<br>`size`：数据大小，单位为字节 |
| `void close()` | 关闭对象连接 | — |
| `int validate()` | 检查ELF标识、体系结构、各表范围和数值溢出 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `bool getValid() const` | 读取对象是否可用 | — |
| `uint16_t getArchitecture() const` | 读取映像要求的处理器体系结构 | — |
| `uint16_t getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `uint64_t getEntry() const` | 读取程序入口虚拟地址 | — |
| `uint32_t getProgramCount() const` | 读取ELF程序头数量 | — |
| `int getProgram(uint32_t index, ElfProgramHeader& program) const` | 读取指定位置的ELF程序头 | `index`：项目索引<br>`program`：接收程序段信息的对象 |
| `int getInterpreter(char* buffer, size_t capacity) const` | 读取ELF指定的动态解释器路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：接收缓冲区容量 |
