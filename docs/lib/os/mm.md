# 内存

## 对象

### os::Mem

进程内存对象，查询内存占用和可用容量。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Mem()` | 构造Mem对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int refresh(uint64_t taskId = 0)` | 更新对象属性 | `taskId`：进程编号 |
| `int64_t reclaimCaches()` | 请求回收可丢弃缓存 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getTotal() const` | 读取系统可管理的内存总字节数 | — |
| `uint64_t getFree() const` | 读取当前可用内存字节数 | — |
| `uint64_t getOwned() const` | 读取资源是否由本对象负责释放 | — |
| `uint64_t getGeneration() const` | 读取查询结果或连接的代次 | — |
| `uint32_t getNodeCount() const` | 读取内存节点数量 | — |
| `bool getSnapshotValid() const` | 读取是否已有完整属性快照 | — |

### os::Mem::Page

页对象，保存内核页面句柄和物理内存属性。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Page()` | 构造Page对象 | — |
| `~Page()` | 析构Page对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int create(uint32_t order, uint16_t node, uint64_t owner = 0)` | 分配物理内存页 | `order`：内存页分配阶数<br>`node`：NUMA 节点编号<br>`owner`：所属进程编号 |
| `int open(Ipc::Handle&& handle)` | 打开对象连接 | `handle`：资源句柄 |
| `int close()` | 关闭对象连接 | — |
| `int refresh()` | 更新对象属性 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `uint64_t getOwner() const` | 读取页对象所属任务编号 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint32_t getReferences() const` | 读取资源引用数量 | — |
| `uint16_t getNode() const` | 读取内存节点编号 | — |
| `uint16_t getOrder() const` | 读取页分配阶数 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

### os::Mem::Map

地址映射对象，管理映射范围、访问权限和后备存储。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Map()` | 构造Map对象 | — |
| `~Map()` | 析构Map对象 | — |
| `Map(Map&& other) noexcept` | 构造Map对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Map& operator=(Map&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int create(uint64_t size, uint32_t protection, bool shared = false)` | 建立地址映射 | `size`：数据大小，单位为字节<br>`protection`：内存访问权限<br>`shared`：使用共享映射 |
| `int open(File& file, uint64_t size, uint64_t offset, uint32_t protection, bool shared)` | 打开对象连接 | `file`：文件对象<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限<br>`shared`：使用共享映射 |
| `int open(Share& memory, uint64_t size, uint64_t offset, uint32_t protection)` | 打开对象连接 | `memory`：共享内存对象<br>`size`：数据大小，单位为字节<br>`offset`：数据起始偏移量<br>`protection`：内存访问权限 |
| `int close()` | 关闭对象连接 | — |
| `uint64_t release()` | 交出对象持有的资源 | — |
| `int sync(int mode)` | 向内存服务提交本映射的同步请求 | `mode`：访问方式或权限位 |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `uint64_t getAddress() const` | 读取映射开始的虚拟地址 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint32_t getProtection() const` | 读取读、写、执行保护位 | — |
| `uint64_t getOffset() const` | 读取当前偏移位置 | — |
| `bool getShared() const` | 读取是否与其他映射共享内容 | — |
| `bool getMapped() const` | 读取当前是否持有有效映射 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |

#### Setter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int setProtection(uint32_t protection)` | 设置读、写、执行保护位 | `protection`：内存访问权限 |

### os::Mem::Share

共享内存对象，保存名称、句柄和容量。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Share()` | 构造Share对象 | — |
| `~Share()` | 析构Share对象 | — |
| `Share(Share&& other) noexcept` | 构造Share对象 | `other`：提供资源或属性的原对象 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Share& operator=(Share&& other) noexcept` | 赋值对象属性 | `other`：提供资源或属性的原对象 |
| `int open(const char* name, int flags)` | 打开对象连接 | `name`：对象名称<br>`flags`：操作标志位 |
| `int close()` | 关闭对象连接 | — |
| `int unlink()` | 删除资源的名称 | — |
| `int resize(uint64_t size)` | 改变共享内存对象长度 | `size`：数据大小，单位为字节 |
| `int refresh()` | 更新对象属性 | — |

#### Getter

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `const char* getName() const` | 读取名称 | — |
| `uint64_t getSize() const` | 读取长度 | — |
| `uint64_t getHandle() const` | 读取原始资源句柄 | — |
| `bool getOpened() const` | 读取是否已经打开 | — |
| `bool getValid() const` | 读取对象是否可用 | — |
| `int getError() const` | 读取最近一次申请或操作的错误码 | — |
