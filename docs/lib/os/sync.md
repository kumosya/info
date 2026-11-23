# 线程同步

## 对象

### os::SpinLock

自旋锁对象，管理短临界区的互斥访问。

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void lock()` | 取得互斥访问权 | — |
| `bool tryLock()` | 尝试取得互斥访问权 | — |
| `void unlock()` | 释放互斥访问权 | — |

### os::SpinLock::Guard

自旋锁作用域对象，管理作用域内的持锁时间。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit Guard(SpinLock& lock)` | 构造Guard对象 | `lock`：作用域内持有的自旋锁 |
| `~Guard()` | 析构Guard对象 | — |

### os::Mutex

互斥锁对象，管理线程间的互斥访问。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `Mutex()` | 构造Mutex对象 | — |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `void lock()` | 取得互斥访问权 | — |
| `bool tryLock()` | 尝试取得互斥访问权 | — |
| `void unlock()` | 释放互斥访问权 | — |

### os::Mutex::Guard

互斥锁作用域对象，管理作用域内的持锁时间。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit Guard(Mutex& mutex)` | 构造Guard对象 | `mutex`：作用域内持有的互斥锁 |
| `~Guard()` | 析构Guard对象 | — |

### os::AddressWait

地址等待对象，保存等待序号并协调线程唤醒。

#### 构造与析构

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `explicit AddressWait(volatile uint32_t& address)` | 构造AddressWait对象 | `address`：内存地址 |

#### 成员函数

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int wait(uint32_t expected)` | 等待地址值变化 | `expected`：等待地址的预期值 |
| `int wakeOne()` | 唤醒等待同一地址的一个线程 | — |
| `int wakeAll()` | 唤醒等待同一地址的全部线程 | — |
