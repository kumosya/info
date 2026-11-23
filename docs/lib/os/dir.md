# 目录

## 枚举

### os::Dir::Scope

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `User` | `0` | 当前用户范围 |
| `System` | `1` | 整个系统范围 |

### os::Dir::Kind

| 成员 | 值 | 说明 |
| --- | --- | --- |
| `Data` | `0` | $HOME/.local/share |
| `State` | `1` | 查询会话状态 |
| `Config` | `2` | $HOME/.config |
| `Cache` | `3` | $HOME/.cache |
| `Runtime` | `4` | /run/user/<uid> |
| `Log` | `5` | 应用的持久状态目录下的log |

## 对象

### os::Dir

目录对象，提供目录访问和约定路径解析。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Dir()` | 构造Dir对象 | — |
| `~Dir()` | 析构Dir对象 | — |
| `Dir(Dir&& other) noexcept` | 构造Dir对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Dir& operator=(Dir&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int open(const char* path)` | 打开对象连接 | `path`：文件或目录路径 |
| `int close()` | 关闭对象连接 | — |
| `int rewind()` | 把已打开目录的读取位置恢复到开头 | — |
| `int create(const char* path, uint32_t mode = 0755)` | 创建目录 | `path`：文件或目录路径<br>`mode`：访问方式或权限位 |
| `int remove(const char* path)` | 删除指定目录 | `path`：文件或目录路径 |
| `int rename(const char* existing, const char* target)` | 把已有目录改为目标路径 | `existing`：现有文件或目录的路径<br>`target`：目标路径 |
| `int readNext(Entry& entry)` | 从已打开目录读取下一项 | `entry`：目录项或线程入口 |
| `int readBatch(Entry* entries, size_t capacity, size_t& count)` | 一次读取多个目录项并推进位置 | `entries`：接收目录项的数组<br>`capacity`：目录项数组可容纳的项目数<br>`count`：接收实际读取的目录项数量 |
| `int open(Scope scope, const char* application = nullptr)` | 打开对象连接 | `scope`：目录所属范围<br>`application`：应用名称 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `char* getPath(Kind kind, char* buffer, size_t capacity)` | 读取指定种类的系统或用户约定目录路径 | `kind`：服务类型<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getHome(char* buffer, size_t capacity)` | 读取账户主目录 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getData(char* buffer, size_t capacity)` | 读取应用数据目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getState(char* buffer, size_t capacity)` | 读取系统或用户的持久状态目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getConfig(char* buffer, size_t capacity)` | 读取配置目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getCache(char* buffer, size_t capacity)` | 读取可删除缓存目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getRuntime(char* buffer, size_t capacity)` | 读取当前登录会话的运行目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getLog(char* buffer, size_t capacity)` | 读取日志目录路径 | `buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getDataSearch(uint32_t index, char* buffer, size_t capacity)` | 读取指定序号的系统数据搜索目录 | `index`：项目索引<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `char* getConfigSearch(uint32_t index, char* buffer, size_t capacity)` | 读取指定序号的系统配置搜索目录 | `index`：项目索引<br>`buffer`：接收读取结果的缓冲区<br>`capacity`：目录项数组可容纳的项目数 |
| `const char* getPath() const` | 读取指定种类的系统或用户约定目录路径 | — |
| `bool getOpened() const` | 读取是否已经打开 | — |
| `uint64_t getOffset() const` | 读取当前偏移位置 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Dir::Entry

目录项对象，保存名称、类型、文件编号和枚举位置。

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const char* getName() const` | 读取名称 | — |
| `File::Type getType() const` | 读取目录项是文件、目录还是其他类型 | — |
| `uint64_t getInode() const` | 读取文件系统内的文件编号 | — |
| `uint64_t getNextOffset() const` | 读取继续读取目录时使用的位置 | — |
