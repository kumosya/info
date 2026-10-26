# os/mm.h


## 内容与边界

Mem 负责页对象分配、统计和回收；页对象由 Mem::Page::create 显式申请，refresh 更新统计，getter 读取总量、空闲、已拥有量、代次和节点数。

Mem::Page::create 返回分配结果，close 返回释放结果，refresh 读取属性；getter 读取句柄、所有者、大小、引用数、节点和阶数；需要映射时使用 Mem::Map::create/open。

Mem::Map 合并原 Mapping 和 VirtualMemory 的映射能力，不保留公共 VirtualMemory 入口。create 建立匿名映射，open 建立文件或共享内存映射，close 解除并返回结果，release 显式交接；getter 只读缓存，setProtection 向服务提交保护变更。

匿名共享映射使用未链接到名称空间的共享内存对象作为后备，fork 后继续引用同一对象；不依靠复制普通匿名页来模拟共享。当前内核共享对象容量上限为 64 个基础页，即 256 KiB。

文件私有映射已经接入；文件共享映射及其脏页写回尚未实现，open(file, ..., true) 返回 NotSupported。sync 接受 MS_SYNC 或 MS_ASYNC；当前支持的匿名、共享内存及文件私有映射没有共享文件脏页需要写回，服务确认范围仍有效后返回。MS_INVALIDATE 尚不支持，不宣称已经完成文件写回。

运行时堆分配器保持内部职责，不将 malloc/free 的堆块管理塞入映射对象。每个 Map 保存服务在创建时返回的后备身份；这个身份随映射结果一起记入恢复记录。close 只解除原范围内仍匹配该身份的片段，因此局部 munmap 后同地址上的新映射不会被旧对象关闭。setProtection 和 sync 要求整个范围仍属于原身份，否则返回 NotFound。MAP_FIXED 地址替换本身仍待后端实现。

Mem::Share 管理共享内存句柄和名称，由 open 显式打开或按标志创建，close 返回关闭结果，unlink 移除名称，resize 改长度，refresh 更新属性，getter 读取结果。unlink 只移除名称，已有引用和映射继续有效。

共享内存的打开结果按原权限进入恢复记录。只读句柄仍按只读权限复制和恢复，不扩大成读写管理权限；恢复记录保持原有固定大小。

Map 面向普通应用的地址空间。管理服务的地址空间不参与这套冻结恢复事务，服务内部使用自己的受权内存控制接口，不由应用库绕过服务隔离。

## 枚举

### MmOperation

底层类型：`uint64_t`。内部协议枚举放在现有 include/service/common/requests.h，不进入 os/mm.h。应用使用 Mem 的资源类；内核控制请求的声明留在服务头文件，实现仍在 lib/os/mm.cc。

| 成员 | 数值 | 含义 |
| --- | --- | --- |
| `MapAnonymous` | `1` | 建立匿名映射 |
| `MapFile` | `2` | 建立文件映射 |
| `MapShared` | `3` | 建立共享对象映射 |
| `Unmap` | `4` | 解除映射 |
| `Protect` | `5` | 修改保护 |
| `Fault` | `6` | 处理缺页请求 |
| `Usage` | `7` | 查询使用量 |
| `Reclaim` | `8` | 请求回收 |
| `SharedOpen` | `9` | 打开共享对象 |
| `SharedUnlink` | `10` | 删除共享对象名称 |
| `Fork` | `11` | 协调地址空间复制 |
| `Sync` | `12` | 同步脏页 |
| `UnmapOwned` | `13` | 只解除匹配映射身份的片段 |
| `ProtectOwned` | `14` | 验证范围身份后修改保护 |
| `SyncOwned` | `15` | 验证范围身份后处理同步请求 |

## 常量与标志

表中列出当前声明和值，并明确它在推荐接口中是否保留。一般不把 namespace constexpr 变量当成新的共享状态；稳定选项优先用所属类型的枚举，容量等通过相关对象的 getter 查询。

| 当前名字 | 当前值或表达式 | 数据含义 | 推荐归属 |
| --- | --- | --- | --- |
| `mmFlagEager` | `1` | 内存服务请求中的立即分配标志 | 内存服务协议内部常量 |
| `mmFlagShared` | `2` | 内存服务请求中的共享标志 | 内存服务协议内部常量，不与 POSIX MAP_SHARED 混用 |

## 全局变量

当前公共头文件没有导出可写全局变量。页、共享对象和映射引用属于实例；共享堆的锁与分配表留在运行时内部。

### 当前共享堆内部数据

| 当前实现名字 | 初始值或容量 | 保存什么 |
| --- | --- | --- |
| RuntimeHeap::allocations_ | 4096 个对象，初始清零 | 运行时立即分配堆的地址和长度记录 |
| RuntimeHeap::nextAddress_ | 0x10000000 | 当前立即分配堆继续申请地址的位置 |
| RuntimeHeap::lock_ | 0 | 保护共享堆记录的锁状态 |

这些名字只用于说明当前实现，不在 mm.h 导出。解除映射失败时分配记录保留，成功后才清除；Mem::Map 不接管 malloc 的分配表。

## 其他类型与固定布局

除本页列出的类、嵌套类及必要参数类型外，不增加整份内部状态记录；服务 wire 记录留在服务头文件。

## 类与函数

### Mem

这个类表示当前系统或任务的内存统计和回收请求。

#### 构造与析构

```cpp
Mem();
```

用途：建立空的内存统计对象，不查询服务；调用 refresh 后才有完整统计缓存。

参数：无。

结果：构造完成后可使用本类查询或参数设置函数；不返回数值。

#### 操作

```cpp
int refresh(uint64_t taskId = 0);
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：

- `uint64_t taskId = 0`：目标进程编号。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int64_t reclaimCaches();
```

用途：请求回收可丢弃缓存，返回实际回收量或错误。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

#### getter

```cpp
uint64_t getTotal() const;
```

数据：系统可管理的内存总字节数。

```cpp
uint64_t getFree() const;
```

数据：当前可用内存字节数。

```cpp
uint64_t getOwned() const;
```

数据：被查询任务拥有的物理内存字节数。

```cpp
uint64_t getGeneration() const;
```

数据：查询结果或连接的代次。

```cpp
uint32_t getNodeCount() const;
```

数据：内存节点数量。

```cpp
bool getSnapshotValid() const;
```

数据：是否已有完整属性快照。

### Mem::Page
这个类表示一份物理页对象引用。

#### 构造与析构

```cpp
~Page();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Page();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


#### 操作

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int create(uint32_t order, uint16_t node, uint64_t owner = 0);
```

用途：向内核内存服务申请物理页对象；成功后保存引用及属性。阶数必须在当前后端支持的 0 至 12 范围内。

参数：

- `uint32_t order`：分配阶数；阶数为 n 时申请 2 的 n 次幂个基础页。
- `uint16_t node`：首选内存节点编号；约定的任意节点值允许系统选择。
- `uint64_t owner = 0`：页对象的所有者任务编号；0 表示当前任务，指定其他任务需要权限。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int open(Ipc::Handle&& handle);
```

用途：先查询句柄，确认它是页对象且属性完整，再接管引用；验证失败时 handle 仍由原调用者持有。

参数：

- `Ipc::Handle&& handle`：现有资源句柄或待转移的句柄对象。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：释放本对象拥有的页引用；其他引用或映射仍然存在时，不强制回收它们使用的物理页。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
uint64_t getOwner() const;
```

数据：页对象所属任务编号。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint32_t getReferences() const;
```

数据：资源引用数量。

```cpp
uint16_t getNode() const;
```

数据：内存节点编号。

```cpp
uint16_t getOrder() const;
```

数据：页分配阶数，一次包含 2 的该次幂个基础页。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

### Mem::Map
这个类表示一段由本对象负责解除的用户虚拟地址映射。

#### 构造与析构

```cpp
~Map();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Map();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Mem::Map& operator=(Mem::Map&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Mem::Map&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
uint64_t release();
```

用途：把资源管理责任交给调用者，本对象不再负责释放。

参数：无。

结果：返回用途所述字段或被交接的原始引用；空引用使用约定的无效值。

```cpp
int sync(int mode);
```

用途：向内存服务提交本映射的同步请求，并验证范围仍属于本对象。当前不支持共享文件脏页写回。

参数：

- `int mode`：MS_SYNC 或 MS_ASYNC，二者不能同时设置；当前 MS_INVALIDATE 返回 NotSupported。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int create(uint64_t size, uint32_t protection, bool shared = false);
```

用途：建立匿名映射，size 向上取整到 4096 字节。shared 为 true 时使用匿名共享后备对象，fork 后的写入继续可见；构造函数本身不申请地址或页。

参数：

- `uint64_t size`：本次数据的字节数。
- `uint32_t protection`：读、写、执行权限位。
- `bool shared = false`：是否让该映射与同一对象的其他映射共享修改。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int open(File& file, uint64_t size, uint64_t offset, uint32_t protection, bool shared);
```

用途：从已打开的文件建立私有映射，并由文件服务委派独立引用，因此关闭传入的 File 不会使映射失效。当前 shared 为 true 时返回 NotSupported。

参数：

- `File& file`：文件对象；输出参数时接收打开结果或查询到的属性，输入参数时提供已打开引用。
- `uint64_t size`：本次数据的字节数。
- `uint64_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。
- `uint32_t protection`：读、写、执行权限位。
- `bool shared`：是否让该映射与同一对象的其他映射共享修改。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int open(Mem::Share& memory, uint64_t size, uint64_t offset, uint32_t protection);
```

用途：从已打开的共享内存对象建立共享映射；offset 必须按基础页对齐，映射有效期独立于传入 Share 的引用。

参数：

- `Mem::Share& memory`：已经打开的共享内存对象。
- `uint64_t size`：本次数据的字节数。
- `uint64_t offset`：起始字节偏移；目录相关接口中是服务返回的继续枚举位置。
- `uint32_t protection`：读、写、执行权限位。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：解除本对象仍拥有的地址范围，成功后清除地址和长度；失败时保留实际仍存在的映射范围。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
uint64_t getAddress() const;
```

数据：映射开始的虚拟地址。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint32_t getProtection() const;
```

数据：读、写、执行保护位。

```cpp
uint64_t getOffset() const;
```

数据：当前偏移位置。

```cpp
bool getShared() const;
```

数据：是否与其他映射共享内容。

```cpp
bool getMapped() const;
```

数据：当前是否持有有效映射。

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
int setProtection(uint32_t protection);
```

数据：映射的读写执行权限。

### Mem::Share
这个类表示一个有名称或匿名打开的共享内存对象引用。

#### 构造与析构

```cpp
~Share();
```

用途：如果调用者没有显式关闭，最后尝试清理仍由本对象拥有的引用或映射。析构不能返回清理错误，也不能当作业务操作成功的证明。

参数：无。

结果：析构无返回值，不抛出异常。需要确认清理成功时，先显式 close。


```cpp
Share();
```

用途：建立空对象并初始化本地字段，不查找服务、不创建资源。

参数：无。

结果：构造没有返回值；需要资源时显式调用 open 或 create，并检查它的返回值。


```cpp
Mem::Share& operator=(Mem::Share&& other) noexcept;
```

用途：仅在本对象为空时转移 other 的资源及释放责任，转移后 other 为空。本对象已有资源时，必须先显式 close 并确认成功；移动赋值不隐式关闭旧资源。

参数：

- `Mem::Share&& other`：需要转移或复制的另一个同类对象。

结果：返回本对象的引用。

#### 操作

```cpp
int unlink();
```

用途：按本对象的句柄移除所对应共享内存对象的名称，已有引用和映射继续有效。原对象已被 unlink 时返回 NotFound，不会删除之后使用同名创建的新对象。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int resize(uint64_t size);
```

用途：改变共享内存对象长度，不自动扩大所有已有映射。

参数：

- `uint64_t size`：本次数据的字节数。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int refresh();
```

用途：向对应服务查询当前属性；响应完整有效后更新本对象缓存，失败保留上一次完整结果。

参数：无。

结果：0 表示成功，负 ErrorCode 表示失败；输出参数仅按本函数说明在成功或部分完成时有效。

```cpp
int open(const char* name, int flags);
```

用途：打开或按标志创建共享内存对象。

参数：

- `const char* name`：资源、服务或账户名称，以零字节结束。
- `int flags`：本次操作的标志位；只接受该操作声明支持的组合。

结果：成功返回 0，失败返回负 ErrorCode。申请失败时保留原有有效资源；不把失败后的空对象当作申请成功。

```cpp
int close();
```

用途：释放本对象持有的共享内存引用，不删除名称，也不解除其他对象持有的映射。

参数：无。

结果：成功返回 0，失败返回负 ErrorCode。对象状态按服务确认的实际结果更新，已释放的引用不再重复释放。


#### getter

```cpp
const char* getName() const;
```

数据：名称。

```cpp
uint64_t getSize() const;
```

数据：长度，单位为字节。

```cpp
uint64_t getHandle() const;
```

数据：原始资源句柄。

```cpp
bool getOpened() const;
```

数据：是否已经打开。

```cpp
bool getValid() const;
```

数据：对象是否可用。

```cpp
int getError() const;
```

数据：最近一次申请或操作的错误码。

## 实现文件

`lib/os/mm.cc` 对应本头文件。相关普通实现不另起与头文件无关的根目录文件名。
