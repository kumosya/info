# 同步原语（src/kernel/task/sync/）

本文档描述 `src/kernel/task/sync/` 的实现：可睡眠内核同步原语（`sync.cc`）与用户态 futex 式等待/唤醒（`user_wait.cc`）。对应头文件 `include/kernel/sync.h`、`user_wait.h`。

## 1. 内核同步原语（sync.cc）

全部基于 `SpinLock` + 线程等待队列实现，等待者用 `thread->waitNext` 串成 FIFO。

### WaitQueue

```cpp
class WaitQueue {
    void initialize();
    void wait();          // 关中断、挂队尾、blockThread
    bool wakeOne();       // 取队首唤醒，返回是否唤醒了人
    uint32_t wakeAll();   // 循环 wakeOne，返回唤醒数
};
```

### Mutex

```cpp
class Mutex {
    void initialize();
    void lock();      // owner 为空则获取；否则挂等待队列并阻塞，循环重试
    bool tryLock();
    void unlock();    // 仅持有者可释放；释放并唤醒一个等待者
};
```

记录 `owner_`（持有线程），等待者队列 `waiters_/waitersTail_`。

### Semaphore

```cpp
class Semaphore {
    void initialize(uint32_t count);
    void wait();      // count > 0 则减一；否则阻塞
    bool tryWait();
    void signal();    // 有等待者唤醒一个；否则 count++
};
```

### Once

```cpp
class Once {
    using Initializer = void (*)(void* argument);
    void initialize();
    void run(Initializer initializer, void* argument = nullptr);
};
```

状态机：0=未开始、1=执行中、2=完成。`run`：状态 2 直接返回；状态 0 → 置 1，**锁外**执行初始化函数，完成后置 2 并唤醒全部等待者；状态 1 → 挂队列阻塞。

### 自检

```cpp
void runSynchronizationSelfTest();
```

内核启动时执行：验证 SpinLock、Mutex、Semaphore（初值 1 时第二次 tryWait 必须失败）、Once（执行两次后初始化恰好发生一次，魔数 `0x6f6e6365`）。任一失败打印串口日志并停机。

## 2. 用户态等待（user_wait.cc）

futex 式等待/唤醒，是用户态 pthread 互斥锁、条件变量等的内核基础。等待键为**物理地址**，使不同进程映射同一物理页时等待/唤醒可以互相匹配。

```cpp
int waitOnUserAddress(volatile uint32_t* address, uint32_t expected);
uint32_t wakeUserAddress(volatile uint32_t* address, uint32_t count);
```

### 实现

- 64 个哈希桶（`bucketCount = 64`），每桶一个 `SpinLock` + 等待者链表。
- **等待键**：持地址空间锁把用户虚拟地址翻译为物理地址（要求该页带 `pageUser` 标志），存入 `thread->waitKey`。
- **`waitOnUserAddress`**：
  1. 取键 → 桶加锁；
  2. `copyFromUser` 读当前值；**值 ≠ expected → 返回 1**（不阻塞）；
  3. 否则记录 `thread->waitAddress/waitKey`、挂入桶队列、`blockThread()`；
  4. 被唤醒后清空两字段，返回 0；非法参数返回 -1。
- **`wakeUserAddress`**：取键 → 遍历桶链表，摘除最多 `count` 个 `waitKey` 匹配的线程（保留不匹配者），解锁后逐个 `wakeThread`；返回实际唤醒数。

### 对应系统调用

- `WaitAddress`（50）：4 字节对齐校验后调用 `waitOnUserAddress`；返回 1 映射为 `WouldBlock`。
- `WakeAddress`（51）：调用 `wakeUserAddress`，返回唤醒数。

详见 [syscall.md](syscall.md)。
