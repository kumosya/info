# libc（lib/libc/）

libc 是 POSIX C 库，构建在 os 运行时的 `os_*` C ABI 之上。本文档按源文件组织。

## 1. 内部层（internal.h / internal.c）

常量：`LIBC_DESCRIPTOR_CAPACITY = 32`（即 OPEN_MAX）、`LIBC_PATH_CAPACITY = 96`、`LIBC_EXEC_STATE_MAGIC = 0x4f53455845435354`（"OSEXECST"）。

数据结构：

```c
enum DescriptorKind { DESCRIPTOR_FILE, DESCRIPTOR_PIPE_READ, DESCRIPTOR_PIPE_WRITE,
                      DESCRIPTOR_TERMINAL_READ, DESCRIPTOR_TERMINAL_WRITE, DESCRIPTOR_SHARED_MEMORY };
struct OpenDescription { uint64_t handle, offset; int flags; DescriptorKind kind; char path[96]; uint32_t referenceCount; };
struct FileDescriptor { OpenDescription* description; int closeOnExec; };
struct LibcExecState { uint64_t magic, size; uint32_t count; char currentDirectory[96]; LibcExecDescriptorState descriptors[32]; };
```

全局描述符表 `libcDescriptors[32]`。关键逻辑：

- `libcFsService()`：缓存 fs 服务端点。
- `libcCopyPath`：相对路径拼接当前目录（默认 "/"），处理 "./" 前缀；绝对路径直接拷贝。
- `libcDescriptor(fd)`：对 0/1/2 惰性初始化——未打开过且未被关闭过时用 `os_get_standard_handle` 获取；句柄为 0 则建终端描述符，否则建管道描述符。关闭 0-2 时置 `standardClosed` 防止再创建。
- `libcAllocateDescriptor`：从 fd=3 找空槽；耗尽置 `errno=EMFILE`。
- `libcDuplicateDescriptor`：dup/dup3/F_DUPFD 公共实现，增加引用计数。
- `libcLockFileIo/libcUnlockFileIo`：全局文件 IO 自旋锁。
- `libcAfterForkChild(parentPid)`：fork 子进程中 `os_clone_file_descriptors` 认领父进程句柄并替换本地文件描述符的 handle；复位 fileIoLock。
- `libcExportExecState` / `__libc_import_exec_state`：execve 跨映像迁移描述符表与当前目录（保持 dup 共享关系；跳过 closeOnExec 描述符；关闭遗留句柄）。

## 2. 文件描述符（fd.c）

- `open(path, flags, ...)`：支持 `O_CREAT`（先创建再 chmod）、`O_TRUNC`、`O_APPEND`（打开后 seek 到文件尾）；目录返回 `EISDIR`。
- `creat` = `open(O_WRONLY|O_CREAT|O_TRUNC)`。
- `read`/`write`：按描述符类型分发到 `os_read_pipe/os_read_terminal/os_read_file`；检查读写方向；文件类型加全局 IO 锁；成功累加 `offset`。`write` 对 `O_APPEND` 每次先 stat+seek 到文件尾。
- `close`：引用计数归零才真正关闭。
- `dup`、`dup2`、`dup3`（支持 `O_CLOEXEC`）。
- `fcntl`：支持 `F_DUPFD/F_DUPFD_CLOEXEC/F_GETFD/F_SETFD/F_GETFL/F_SETFL`；`F_SETFL` 只允许改 `O_APPEND`，`O_NONBLOCK` 返回 `ENOSYS`。
- `pipe`/`pipe2`（支持 `O_CLOEXEC`）。
- `lseek`：仅文件类型（其它 `ESPIPE`），支持 SEEK_SET/CUR/END。
- `pread`/`pwrite`：加锁保存 offset → seek → 读/写 → 恢复。
- `readv`/`writev`：逐块累加，短读/短写即停。

## 3. 文件系统（filesystem.c）

- `stat`/`fstat`：填充 `struct stat`（st_mode 合并 `S_IFDIR/S_IFREG`，st_blksize=4096，时间秒级）。
- `mkdir`（创建后 chmod）、`unlink`、`rmdir`（=unlink）、`link`、`linkat`（仅 AT_FDCWD）。
- `symlink`/`readlink`：不支持，返回 `ENOSYS`。
- `chdir`、`getcwd`（缓冲不足 `ERANGE`）。
- `access`：按 stat mode 位粗检。
- `truncate`：只支持截断到 0；`ftruncate`：共享内存描述符走 `os_resize_shared_memory`。
- `fsync`/`fdatasync`：`ENOSYS`。
- `chmod`/`fchmod`、`chown`/`fchown`/`lchown`（-1 保持原值；uid/gid ≤0xFFFF）、`fstatat`（仅 AT_FDCWD）。
- `utimensat`：支持 `UTIME_NOW/UTIME_OMIT`。
- `sysconf`：`_SC_VERSION/_SC_OPEN_MAX/_SC_PAGESIZE(4096)/_SC_CLK_TCK(100)`；`_SC_NPROCESSORS_*` 返回 `ENOSYS`。
- `pathconf`、`confstr`（`_CS_PATH` = "/bin:/usr/bin"）。

## 4. 目录（directory.c）

`struct DirectoryStream`：path、index、批量缓存（8 条 `OsDirectoryEntry`）、`struct dirent`。

- `opendir`（stat 校验目录）、`readdir`（批缓存耗尽时 `os_read_directory_batch`；填充 d_ino/d_type/d_name）、`closedir`、`rewinddir`、`seekdir`、`telldir`；`dirfd` 返回 `ENOSYS`。

## 5. 进程（process_runtime.c）

- 子进程登记表：`ChildProcess {handle, id}` 容量 32。
- `getpid`、`getppid`/`getpgrp`/`getsid`（`os_get_process_identifiers`）、`setsid`。
- `kill(pid, sig)`；pid==-1 不支持（`ENOSYS`）。
- **`fork()`**：`os_fork_process`；子进程中重置子进程表、`libcAfterForkChild(parent)` 重映射文件句柄，失败则 `_exit(127)`。
- **`execve(path, argv, envp)`**：最多 8 参数、16 环境变量（超出 `E2BIG`）；映像 ≤8MB；完整读入目标文件与 3 个共享库（`libos-c++.so`、`libc.so`、`libc++.so`）；`libcExportExecState` 生成 runtimeState；`os_replace_process_image`；成功不返回。
- `wait`/`waitpid`：校验 options（WNOHANG|WUNTRACED|WCONTINUED），`os_wait_child`；成功后关闭子进程句柄。
- `isatty`：终端描述符返回 1。

## 6. pthread（pthread.c）

完整 pthread 实现，底层基于 `os_create_thread`/`os_wait_thread`/`os_exit_thread` + futex 式 `os_wait_address`/`os_wake_address` + `os_yield`/`os_sleep` + `__atomic_*`。

常量：`LIBC_THREAD_CAPACITY = 64`，默认栈 64 KB。

- **线程生命周期**：`pthread_create`（先回收已结束 detached 线程；malloc 栈；`os_create_thread`）、`pthread_join`（`join_claimed` 防并发；阻塞等待后取 result）、`pthread_detach`、`pthread_exit`（运行 TLS 析构器）、`pthread_self`、`pthread_equal`。
- **互斥锁**（futex 三态：0 空闲、1 持有、2 有等待者）：支持 NORMAL/RECURSIVE/ERRORCHECK；`trylock` 返回 `EBUSY`；`timedlock` 用 trylock + `os_sleep(1)` 轮询至绝对截止时间。
- **条件变量**（基于 sequence 序号）：`wait` = 读序号 → 解锁 → `os_wait_address(&sequence, seq)` → 重新加锁；`signal` = 序号+1 唤醒一个；`broadcast` 唤醒全部；`timedwait` 轮询。
- **`pthread_once`**：三态（0/1/2），CAS 抢占执行权，等待者 `os_wait_address`。
- **TLS**：`pthread_key_create/delete`、`pthread_getspecific/setspecific`；线程退出按 `PTHREAD_DESTRUCTOR_ITERATIONS` 轮次调用析构器。
- **读写锁**：mutex+cond 构建，写者优先。
- **屏障**：mutex+cond + 代际计数；最后到达者返回 `PTHREAD_BARRIER_SERIAL_THREAD`。
- **自旋锁**：`__atomic_exchange` + `os_yield`；仅 PROCESS_PRIVATE。
- `pthread_cancel` 等：返回 `ENOSYS`。

## 7. C11 线程（threads.c）

构建在 pthread 之上：`thrd_create/current/equal/join/detach/exit/sleep/yield`、`mtx_*`（支持 recursive|timed 组合）、`cnd_*`、`call_once`、`tss_*`。errno→thrd 错误码映射。

## 8. 信号量（semaphore.c）

- **匿名信号量**：`sem_t {value, initialized}`；`sem_wait` = trywait 失败则 `os_wait_address(&value, 0)` 阻塞；`sem_post` = CAS 递增后 `os_wake_address`；`sem_timedwait` 轮询。
- **命名信号量**：基于共享内存。名字映射 `/name` → `/__posix_sem__name`；`sem_open` = `shm_open(O_RDWR|O_CREAT|O_EXCL)` + `ftruncate(4096)` + `mmap(MAP_SHARED)`；创建者写初值，打开者自旋等待；`sem_unlink` 转 `shm_unlink`。

## 9. 信号（signal_runtime.c）

- `sigset_t` 为 64 位位图（位 `1ULL << (signo-1)`）；`sigemptyset/sigfillset/sigaddset/sigdelset/sigismember`。
- `sigaction`：拒绝 SIGKILL/SIGSTOP；接受 `SA_SIGINFO|SA_RESTART|SA_NODEFER|SA_RESETHAND`；向内核注册 `signal_trampoline`（handler）与 `signal_restorer`（naked，`movq %rsp,%rdi; call os_signal_return`）；用户 `struct sigaction` 存本地表。
- `signal_trampoline`：取本地 action、递增 `signal_generation`、按 SA_SIGINFO 分发。
- `signal()` 用 sigaction 实现；`raise` = `kill(getpid(), sig)`。
- `pthread_sigmask`/`sigprocmask`、`sigpending`。
- `sigwait`：循环 `os_wait_signal`，WouldBlock 时 yield 重试；`sigwaitinfo`/`sigtimedwait` 加 siginfo 填充与截止轮询。
- `pause`/`sigsuspend`：轮询 `signal_generation` 变化。
- `pthread_kill`：`os_send_thread_signal`。

## 10. spawn（spawn.c）

`posix_spawn`/`posix_spawnp` 实现为 **fork + 子进程执行文件动作 + execve**。参数上限 8；支持文件动作 close/dup2/open；属性支持 `POSIX_SPAWN_SETSIGMASK`/`POSIX_SPAWN_SETSIGDEF`；`POSIX_SPAWN_SETPGROUP` 返回 `ENOTSUP`。`posix_spawnp` 非绝对路径前缀 `/bin/`。

## 11. 内存映射（mman.c）

常量：页 4096，映射区基址 `0x0000000040000000`，容量 64。

- `mmap`：校验页对齐、保护位、MAP_PRIVATE/MAP_SHARED 二选一；**不支持** `MAP_FIXED` 与 `PROT_NONE`；**禁止** W+X。
  - 匿名私有：`os_map_memory`。
  - 共享内存描述符（须 MAP_SHARED）：`os_map_shared_memory`。
  - 文件私有（MAP_SHARED 文件返回 `ENOTSUP`）：只读映射 → pread 读入 → `os_protect_memory` 改保护（copy-on-read）。
- `munmap`：地址/大小精确匹配。
- `mprotect`：范围须在单个映射内；禁止 W+X。
- `msync`：no-op。
- `shm_open`：校验名字与 flags；`os_open_shared_memory`；O_TRUNC 时 resize 到 0；描述符强制 `closeOnExec=1`。
- `shm_unlink`：`os_unlink_shared_memory`。

## 12. 消息队列（mqueue.c）

直接映射到内核消息队列系统调用。`mq_open`（flags 限 `O_ACCMODE|O_CREAT|O_EXCL|O_NONBLOCK`；`mq_maxmsg ≤ 16`、`mq_msgsize ≤ 256`）、`mq_close`、`mq_unlink`、`mq_send`（priority < MQ_PRIO_MAX）、`mq_receive`、`mq_getattr`/`mq_setattr`（只允许改 O_NONBLOCK）、`mq_timedsend`/`mq_timedreceive`（临时非阻塞 + 轮询 + 截止时间）、`mq_notify`（SIGEV_NONE/SIGEV_SIGNAL；SIGEV_THREAD 返回 `ENOTSUP`）。

## 13. 异步 IO（aio.c）

基于 fs 服务异步 IPC + 工作线程。状态 `AIO_STATE_IDLE/PROGRESS/DONE/RETURNED`；单次传输上限 4088 字节；`aio_reqprio` 必须为 0；仅普通文件。

- `aio_read/aio_write`：创建完成端点 → `os_read_file_async/os_write_file_async` → `pthread_create` 工作线程收结果（失败则当前线程同步执行）。
- `aio_error`、`aio_return`（CAS DONE→RETURNED，只能取一次，join 工作线程）、`aio_cancel`（不能真正取消）、`aio_suspend`（轮询 + 相对超时）、`aio_fsync`、`lio_listio`（LIO_WAIT/LIO_NOWAIT）。

## 14. stdio（stdio_runtime.c）

- `FILE {fileDescriptor, error, endOfFile, owned}`；静态 `stdin/stdout/stderr` 绑定 fd 0/1/2。
- 自实现 mini printf：`vsnprintf`/`snprintf` 支持 `%s %c %d %i %u %x %X %p %%`，标志 `-`/`0`、宽度、长度 `l`；不支持浮点/精度/`%o`。
- `fprintf`/`printf`（512 字节栈缓冲）、`fputc/putchar/fputs/puts`、`fgetc/getchar`、`fgets`、`fread/fwrite`、`fopen`（仅 "r"/"w"/"a"）、`fclose`（仅 owned）、`fflush`（no-op）、`fileno/feof/ferror/clearerr`、`perror`。

## 15. stdlib（stdlib_runtime.c）

- `strtoul`/`strtol`/`atoi`。
- `rand`/`srand`：线性同余。
- `atexit`：静态表容量 16；`__libc_run_exit_handlers` 逆序执行。
- `exit`：exit handlers → `os_runtime_fini` → 关闭全部描述符 → `os_exit_process`。
- `_exit`：关闭描述符后直接退出；`abort`：退出码 134。

## 16. string / memory / time / errno / environment / ctype

- **string_runtime.c**：`memcpy`、`memmove`（重叠安全）、`memset`、`memcmp`、`strlen`、`strnlen`、`strcpy`、`strncpy`、`strcat`、`strcmp`、`strncmp`、`strchr`、`strrchr`、`strstr`、`strdup`。
- **memory_runtime.c**：`malloc`（加 `AllocationHeader` 头后 `os_allocate_memory`）、`free`、`calloc`、`realloc`。底层每次分配页映射 + 保护页，实际粒度 4 KB。
- **time_runtime.c**：`time`、`clock_gettime`（MONOTONIC/REALTIME）、`clock_getres`、`nanosleep`（换算毫秒）、`clock_nanosleep`（支持 TIMER_ABSTIME）、`sleep`、`usleep`、`gettimeofday`。
- **errno.c**：`int errno;` —— **全局变量，非线程局部**。
- **errors.c**：`strerror`，16 种常见错误的静态字符串。
- **environment.c**：`environ`、`getenv`、`setenv`（首次修改拷贝到静态表）、`unsetenv`、`clearenv`。
- **ctype_runtime.c**：`isalpha`、`isdigit`、`isalnum`、`isspace`、`isprint`、`tolower`、`toupper`。
- **posix_stubs.c**：`__libc_posix_not_implemented(operation)` —— 调用 `os_posix_unsupported` 上报，置 `errno = ENOSYS` 返回 -1。

## 17. 已知限制

- `errno` 是全局非 TLS；
- 描述符上限 32；管道单次传输 512 字节；消息队列 ≤16 条 ×256 字节；AIO 单次 ≤4088 字节且不可取消；
- 不支持 `MAP_FIXED`、`PROT_NONE`、W+X 映射、符号链接、`fsync`、`O_NONBLOCK` fcntl、`pthread_cancel`、`dirfd` 等。
