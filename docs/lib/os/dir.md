# os/dir.h



当前来源：`os/direct.h`；推荐头文件为 `os/dir.h`。


## 内容与边界

原 direct.h 改为 dir.h，Direct 改为 Dir，原 Fs::Dir 和目录项都移到这里。

Dir() 创建空对象，open(path) 显式打开目录，close 显式关闭并返回状态。create 创建目录，remove 删除空目录，rename 改目录名；readNext、readBatch 枚举已打开目录，rewind 回到开头。析构只关闭引用，不删除目录。

Dir::Entry 保存一个目录项的名称、类型、inode 和继续位置，供文件管理器、文件选择器、ls 和递归工具使用，不暴露磁盘目录或 VFS 内部结构。

同一类保留原约定目录能力。open(scope, application) 设置使用系统还是用户目录及可选应用名；getHome、getData、getState、getConfig、getCache、getRuntime、getLog 解析约定路径，getDataSearch、getConfigSearch 逐个读取搜索路径。这些查询不打开、创建或改变当前已打开目录。

无参数 getPath 返回已打开目录的路径；带 kind、buffer、capacity 的 getPath 解析指定种类的约定目录。getState 在这里表示持久状态目录，不表示任务状态。
### 约定目录的默认值

| 数据 | 系统目录 | 用户目录 |
| --- | --- | --- |
| Data | /usr/share | $HOME/.local/share |
| State | /var/lib | $HOME/.local/state |
| Config | /etc/xdg | $HOME/.config |
| Cache | /var/cache | $HOME/.cache |
| Runtime | /run | /run/user/<uid> |
| Log | /var/log | 应用的持久状态目录下的 log |

查询按进程当前的 XDG_DATA_HOME、XDG_STATE_HOME、XDG_CONFIG_HOME、XDG_CACHE_HOME、XDG_RUNTIME_DIR 解析；空值或相对路径按规则回退。XDG_DATA_DIRS、XDG_CONFIG_DIRS 表示搜索目录列表，不是库导出的全局变量。

应用名是单个目录分量，拒绝路径穿越。用户运行目录必须已由 users 服务建立、属于当前有效用户且权限为 0700；查询不会代替服务创建它。持久数据不能放在会随会话结束清理的运行目录中。

## 枚举

### Dir::Scope

底层类型：`int`。约定目录使用系统范围还是当前用户范围。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `User` | `0` | 当前用户范围 |
| `System` | `1` | 整个系统范围 |

### Dir::Kind

底层类型：`int`。需要解析的约定目录类别。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `Data` | `0` | 应用数据目录 |
| `State` | `1` | 持久状态目录 |
| `Config` | `2` | 配置目录 |
| `Cache` | `3` | 可以清理重建的缓存目录 |
| `Runtime` | `4` | 当前登录会话运行目录 |
| `Log` | `5` | 日志目录 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

当前公共头文件没有声明需要公开的独立数值或字符串常量。

## 全局变量

当前公共头文件没有导出可写全局变量。已打开目录、枚举游标和应用目录配置属于 Dir 实例；环境变量由进程环境管理。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Dir
这个类表示目录打开、创建、删除、改名、枚举，以及主目录和配置等约定路径解析。

#### 构造与析构

```cpp
Dir();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
~Dir();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Dir(Dir&& other) noexcept;
```

用途：将 other 的资源及释放责任转移给本对象，之后 other 为空；不得把这当作复制一份系统资源。

参数：

- `Dir&& other`：需要转移或复制的另一个同类对象。

结果：构造函数没有返回值；通过 getValid/getError 查询资源结果，File、Dir 使用 getOpened/getError。

```cpp
Dir& operator=(Dir&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Dir&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int open(const char* path);
```

用途：打开指定路径；成功后对象保存访问引用，后续读写或枚举使用这个对象。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int close();
```

用途：关闭对象拥有的文件或目录引用；成功后对象变为空，不删除磁盘上的内容。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int rewind();
```

用途：把已打开目录的读取位置恢复到开头。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int create(const char* path, uint32_t mode = 0755);
```

用途：在指定路径创建目录，不自动打开它。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。
- `uint32_t mode = 0755`：创建时的权限位；已有文件的权限不因再次打开而改变。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int remove(const char* path);
```

用途：删除指定目录；目录非空时返回错误。

参数：

- `const char* path`：要操作的路径，以零字节结束；相对路径从进程当前工作目录开始解释。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int rename(const char* existing, const char* target);
```

用途：把已有目录改为目标路径；源路径必须是目录，实际替换规则由文件系统服务执行。

参数：

- `const char* existing`：已有文件或目录的路径。
- `const char* target`：操作后的目标路径；挂载时是用于接入文件系统的目录。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int readNext(Dir::Entry& entry);
```

用途：从已打开目录读取下一项，并推进枚举位置；返回值区分取得条目、已读完和失败。

参数：

- `Dir::Entry& entry`：输出下一项的名称、类型、inode 和继续位置。

结果：1 表示得到一项，0 表示读完，负值表示错误。

```cpp
int readBatch(Dir::Entry* entries, size_t capacity, size_t& count);
```

用途：一次读取多个目录项并推进位置，减少重复 IPC。

参数：

- `Dir::Entry* entries`：输出目录项数组，至少预留 capacity 项。
- `size_t capacity`：输出缓冲区可容纳的字节数；条目数组接口中表示可容纳的条目个数。
- `size_t& count`：输出实际得到的条目个数；输入时表示数组中有效元素个数。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(Dir::Scope scope, const char* application = nullptr);
```

用途：设置约定目录的范围和应用名，不打开目录，不创建目录；已经打开目录时返回 Busy。

参数：

- `Dir::Scope scope`：使用系统级还是当前用户的约定目录。
- `const char* application = nullptr`：可选应用名，用作约定目录的下级目录，不允许包含路径穿越分量。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

#### getter

```cpp
char* getPath(Dir::Kind kind, char* buffer, size_t capacity);
```

数据：指定种类的系统或用户约定目录路径。

```cpp
char* getHome(char* buffer, size_t capacity);
```

数据：账户主目录。

```cpp
char* getData(char* buffer, size_t capacity);
```

数据：应用数据目录路径。

```cpp
char* getState(char* buffer, size_t capacity);
```

数据：系统或用户的持久状态目录路径。

```cpp
char* getConfig(char* buffer, size_t capacity);
```

数据：配置目录路径。

```cpp
char* getCache(char* buffer, size_t capacity);
```

数据：可删除缓存目录路径。

```cpp
char* getRuntime(char* buffer, size_t capacity);
```

数据：当前登录会话的运行目录路径。

```cpp
char* getLog(char* buffer, size_t capacity);
```

数据：日志目录路径。

```cpp
char* getDataSearch(uint32_t index, char* buffer, size_t capacity);
```

数据：指定序号的系统数据搜索目录。

```cpp
char* getConfigSearch(uint32_t index, char* buffer, size_t capacity);
```

数据：指定序号的系统配置搜索目录。

```cpp
const char* getPath() const;
```

数据：已打开目录的路径。

```cpp
bool getOpened() const;
```

数据：是否已经打开。

```cpp
uint64_t getOffset() const;
```

数据：当前偏移位置。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Dir::Entry

这个类表示一次目录枚举得到的名称、类型、文件编号和继续位置。

#### getter

```cpp
const char* getName() const;
```

数据：名称。

```cpp
File::Type getType() const;
```

数据：目录项是文件、目录还是其他类型。

```cpp
uint64_t getInode() const;
```

数据：文件系统内的文件编号。

```cpp
uint64_t getNextOffset() const;
```

数据：继续读取目录时使用的位置。

## 实现文件

`lib/os/dir.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
