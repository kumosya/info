# task 服务（src/task/）

task 服务（注册名 `"task"`）是进程服务器：负责从文件系统读取 ELF、判定动态链接、组装 `SpawnImageInfo` 并经 `spawnImage` 创建进程，同时维护进程表。授予能力：`processCapabilitySpawn`。

## 1. 常量

| 常量 | 值 | 说明 |
|---|---|---|
| `maximumArgumentCount` | 8 | 最大参数个数 |
| `executePayloadSize` | 56 字节 | Execute 请求参数区（7 个 uint64） |
| `maximumExecutableSize` | 8 MiB | 可执行文件上限 |
| `processRecordCapacity` | 64 | 进程表容量 |
| `executableReadWindow` | 8 | 异步读流水线深度 |

动态可执行文件一并加载的共享库（`sharedImageCount = 3`）：

```text
/lib/x86-64-osinfo/libos-c++.so
/lib/x86-64-osinfo/libc.so
/lib/x86-64-osinfo/libc++.so
```

## 2. IPC 请求（`os::TaskRequest`）

| 请求 | 值 | 语义 |
|---|---|---|
| Spawn | 1 | `data[0]` = `ProgramId`，`handles[0]` = bootstrap 句柄；`spawnProgram` 启动内置镜像；成功回传进程句柄 |
| Execute | 2 | 从磁盘加载并启动程序（核心路径，见下） |
| Wait | 3 | `handles[0]` = 进程句柄；`getProcessState` 查询；回复 `data[0]`=state（<0 时为 2）、`data[1]`=exitCode |
| List | 4 | `data[0]` = 索引；回复 pid、state、48 字节进程名；越界 `data[0]=1` |
| Kill | 5 | `data[0]` = pid；`terminateProcess(handle, -15)`（SIGTERM） |

未知类型回复 `data[0]=3`。

## 3. Execute 协议细节

请求 `data[0]` 编码：

- 低 32 位 = 参数个数；
- 位 40/41/42 = `taskExecuteHasStdin/HasStdout/HasStderr`（有则按序从 `handles[1..]` 取标准流句柄）；
- 位 32-39 = 终端号（1..7）；若参数中未显式含 `--os-terminal=` 则自动追加。

`data[1..]` 为 56 字节参数负载：`argv[0]`（路径，必须以 `/` 开头）+ 至多 8 个 NUL 结尾参数。

## 4. 加载流程（`loadAndSpawn`）

1. `statFile` 校验：必须是普通文件、非空、≤ 8 MiB；
2. `allocateMemory` 分配缓冲；`openFile` + `readExecutable()`：用独立完成端点做窗口为 8 的 `readFileAsync` 流水线（每块 ≤ 4088 字节）；
3. `usesDynamicLinking()`：扫描程序头判断是否含非空 `PT_DYNAMIC`；
4. 动态程序则同样读入 3 个共享库；
5. 组装 `SpawnImageInfo`（bootstrap 句柄、三个标准流句柄、能力、`parentProcessId = request.senderPid`、参数、共享镜像数组）；
6. `os::spawnImage()` 创建进程；
7. 关闭请求携带的全部句柄。

**能力授予**：路径为 `/system/services/console.elf` 或 `window.elf` 时 `capabilities = processCapabilityDisplay`，否则 0。

## 5. 进程表维护

`ProcessRecord { uint64_t handle; uint64_t id; char name[48]; }`，容量 64。

- `trackProcess()`：登记新创建的进程；
- `reapProcessRecords()`：回收 `state == 2`（已退出）或句柄失效的记录。

## 6. 客户端接口

用户态经 `include/os/process.h` 访问（实现见 os 运行时库）：

```cpp
ProcessHandle execute(const char* path);
ProcessHandle execute(const char* path, int argumentCount, const char* const* arguments);
ProcessHandle executeWithIo(const char* path, int argumentCount, const char* const* arguments,
                            uint64_t standardInputHandle, uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
int waitProcess(ProcessHandle process, int* exitCode);
bool listProcess(uint32_t index, ProcessListEntry& entry);
bool killProcess(uint64_t processId);
```
