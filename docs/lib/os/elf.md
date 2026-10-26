# os/elf.h


## 内容与边界

Executable 绑定借用的 ELF 映像视图，open 检查并绑定视图，close 解除借用，validate 验证格式、体系结构、边界和溢出；getter 读取入口、程序头和解释器路径。映像缓冲区在使用期间必须有效且不可被其他代码修改。支持小端 ELF64、x86-64、ET_EXEC 和 ET_DYN；其他体系结构或扩展编号返回 NotSupported。

标准 ElfHeader、ElfProgramHeader、ElfDynamic、ElfSymbol、ElfRela 固定格式可保留 struct。实际装载、重定位写入及 exec 上下文替换由加载器和服务完成。

## 枚举

本头文件不公开枚举。功能操作使用各自的成员函数，不通过公开操作编号切换。

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

当前公共头文件没有声明需要公开的独立数值或字符串常量。

## 全局变量

当前公共头文件没有导出可写全局变量。需要共享的初始化或缓存状态放在实现中，不通过应用头文件开放可写变量。

## 其他类型与固定布局

- ElfHeader：ELF64 文件头，64 字节。
- ElfProgramHeader：ELF64 程序头，56 字节。
- ElfDynamic：ELF64 动态项，16 字节。
- ElfSymbol：ELF64 符号表项，24 字节。
- ElfRela：带显式加数的 ELF64 重定位项，24 字节。

## 类与函数

### Executable
这个类表示一段可执行映像的借用视图和已检查的 ELF 属性。

#### 构造与析构

```cpp
~Executable();
```

用途：解除本地映像视图，不释放借用的映像字节；此清理不访问服务，不需要错误返回值。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Executable();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
int validate();
```

用途：检查 ELF 标识、体系结构、各表范围和数值溢出，不执行其中代码。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(const void* image, size_t size);
```

用途：绑定借用的 ELF 字节并检查格式，损坏或不支持的映像通过返回值报告。

参数：

- `const void* image`：可执行映像的借用字节或已整理的启动参数，不由函数自动释放。
- `size_t size`：本次数据的字节数。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
void close();
```

用途：解除本地映像视图，清除检查结果；不释放借用的字节缓冲区，也不访问服务。

参数：无。

结果：无返回值；对象变为空，借用的字节保持不变。


#### getter

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
uint16_t getArchitecture() const;
```

数据：映像要求的处理器体系结构。

```cpp
uint16_t getType() const;
```

数据：种类。

```cpp
uint64_t getEntry() const;
```

数据：程序入口虚拟地址。

```cpp
uint32_t getProgramCount() const;
```

数据：ELF 程序头数量。

```cpp
int getProgram(uint32_t index, ElfProgramHeader& program) const;
```

数据：指定位置的 ELF 程序头。

```cpp
int getInterpreter(char* buffer, size_t capacity) const;
```

数据：ELF 指定的动态解释器路径；返回 1 表示存在解释器，0 表示没有解释器，负 ErrorCode 表示视图无效或缓冲区不足。

## 实现文件

`lib/os/elf.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
