# C ABI 桥（c_api.cc）与启动流程（start.S / service_start.S）

## 1. C ABI 桥（c_api.cc）

把 C++ `os::` API 暴露为纯 C 的 `os_*` 接口，供 libc 使用。文件顶部用 `static_assert` 保证 C/C++ 两侧结构体大小一致：`OsFileStat`、`OsDirectoryEntry`、`OsPipeHandles`、`OsFileOperationResult`、`OsProcessIdentifiers`、`OsProcessInfo`、`OsSignalAction`、`OsMessageQueueAttributes`、`OsExecImageInfo`，以及 `OS_POSIX_TERMINAL_JOB_CONTROL` 常量值一致。

`extern "C"` 导出约 60 个函数，绝大多数是一对一转发。值得注意的：

- `os_get_fs_service()`：从引导句柄查找 `fsServiceName` 服务（重试 1000 次）。
- `os_stat_file`：把 `os::FileStat` 逐字段转成 C 结构 `OsFileStat`，成功返回 1。
- `os_read_directory_batch`：直接 reinterpret_cast C 数组为 `os::DirectoryEntry*`。
- `os_create_pipe`、`os_get_process_identifiers`、`os_get_process_state/info`：结构体字段搬运，空指针返回 -1/0。
- `os_wait_thread(thread_id, exit_code*, no_hang)`、`os_set_signal_mask(how, mask, old_mask*)`：把 C 指针参数适配为 C++ 引用。
- `os_unmap_memory`/`os_protect_memory`：bool 转 0/-1。
- `os_execute_process(path, argc, argv, stdin, stdout, stderr)`：转发 `executeWithIo`。
- `os_runtime_fini()`：暴露运行时收尾（供 libc 的 `exit()` 调用）。

C 接口头为 `include/os/c_api.h`；`os_exit_thread`/`os_exit_process`/`os_signal_return` 带 `__attribute__((noreturn))`。

## 2. 普通程序启动（start.S）

`_start` 入口（`.section .text.start`，`.code64`）。内核在进程栈上布置的初始布局：

```text
[argc][argv[0..argc-1]][NULL][envp[0..envc-1]][NULL][可选的 exec 状态指针]
```

流程：

1. `xorq %rbp, %rbp` 清帧指针；`%r12 = %rsp`（保存原始栈顶）。
2. `%rdi = *(%rsp)` = argc；`%rsi = %rsp+8` = argv；`%rdx = %rsp+16+argc*8` = envp；`%r14 = %rdx`（保存 envp）。
3. 调用 `os::runtimeInit(argc, argv, envp)`，返回值（可能被 `--os-terminal=` 修正过的 argc）存入 `%r13`。
4. 调用 `__libc_set_environ(envp)`，让 libc 拿到环境变量。
5. 从 envp 向后扫描到 NULL，取 NULL 之后的第一个字：若非空，作为 `const LibcExecState*` 调用 `__libc_import_exec_state`（execve 后恢复文件描述符表/当前目录）。
6. 恢复 `%rsp=%r12`，准备 `main(argc=%r13, argv, envp=%r14)`，`andq $-16, %rsp` 按 SysV ABI 对齐后 `call main`。
7. main 返回值存 `%r12`；依次调用 `__libc_run_exit_handlers`（atexit）和 `os::runtimeFini`（fini_array）。
8. `movq %r12, %rdi`、`xorq %rax, %rax`（系统调用号 0 = ExitProcess）、`syscall` 退出；之后 `ud2` 兜底。

## 3. 服务程序启动（service_start.S）

简化版 `_start`：

- 同样解析 `[argc][argv][envp]`，调用 `os::runtimeInit`；
- 不处理 environ / exec 状态，直接 16 字节对齐后调用 **`userMain(argc, argv, envp)`**（注意入口名是 `userMain` 而非 `main`）；
- 返回后调用 `os::runtimeFini`，再以 `rax=0`（ExitProcess）`syscall` 退出。

内置服务（init、task、fs、device、storage）链接 `service_start.S`；磁盘程序（shell、terminal、console、window 等）链接 `start.S`。
