# libc++（lib/libc++/）

libc++ 是 C++ 标准库的极简实现，构建在 libc 之上，以共享库 `libc++.so` 提供。仅覆盖运行时支持、内存分配、IO 流与线程的最小必要子集。

## 1. ABI 支持（abi.cc）

- `__dso_handle`：指向自身。
- `__cxa_atexit(destructor, object, dso)`：静态表容量 32，满则返回 -1（忽略 dso 参数）。
- `__cxa_finalize(void*)`：逆序调用全部登记的析构器（忽略 dso 参数）。由 `os::runtimeFini` 的 fini_array 机制间接驱动。
- `__cxa_deleted_virtual()`：调用 `abort()`。

## 2. 内存分配（new.cc）

- `operator new` / `operator new[]`（含 nothrow 版）全部 `noexcept`，直接 `malloc`（不抛异常）。
- `operator delete` / `operator delete[]`（含 sized 与 nothrow 版）直接 `free`。
- 定义 `std::nothrow` 常量。
- `__cxa_pure_virtual()`：调用 `abort()`。

## 3. 输出流（ostream.cc）

`std::ostream` 绑定文件描述符成员 `fileDescriptor_`：

- `put(char)` / `write(data, size)`：直接 `::write(fd, ...)`；`flush()` 为 no-op。
- `operator<<` 重载：`const char*`、`char`、`int`/`unsigned`/`long`/`unsigned long`（经 `snprintf` 到 32 字节栈缓冲）、`const void*`（%p）、函数操纵子。
- 全局操纵子：`std::endl`（put '\n' + flush）、`std::flush`。

## 4. 输入流（istream.cc）

`std::istream` 绑定 fd，状态位 `good_`：

- `get(char&)`：读 1 字节。
- `getline(buffer, size)`：逐字节读到换行（换行被消耗但不存储）或满。
- `read(buffer, size)`：一次性读 size 字节。
- `operator>>(int&)`：先 `getline` 32 字节再 `atoi`（按行读取整数）。

## 5. 全局流对象（iostream_globals.cc）

```cpp
std::cin{STDIN_FILENO};
std::cout{STDOUT_FILENO};
std::cerr{STDERR_FILENO};
```

## 6. 线程支撑（thread.cc）

`std::thread` 的底层实现（`namespace std::detail`）：

```cpp
struct ThreadState {
    ThreadInvoker invoker; void* object; void* stack;
    uint64_t id; bool completed, detached, used;
};
```

- 静态表容量 8，栈大小 64 KB，test-and-set 自旋锁。
- `createStandardThread(invoker, object)`：回收已完成的 detached 线程 → 找空槽 → `os::allocateMemory(64KB)` 分配栈 → `os::createThread(run, stack_top, &state)`；`run` 执行 `invoker(object)` 后置 completed 并 `os::exitThread(0)`。
- `joinStandardThread`：`os::waitThread` 阻塞等待后释放栈与槽位。
- `detachStandardThread`：仅置 detached 标志（实际回收发生在下次创建时）。
- `getStandardThreadId`：返回内核线程 id。

## 7. 范围与限制

- 无异常机制（new 不抛异常）；无 RTTI；
- 流不支持格式化标志、宽度、精度控制；
- `std::thread` 最多 8 个并发实例；
- 容器（`vector`/`string` 等）由 `include/` 的头文件模板实现，不在本库编译单元内。
