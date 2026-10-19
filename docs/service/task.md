# task 服务（src/task/）

task 服务（注册名 `"task"`）是进程服务器：负责从文件系统读取 ELF、判定动态链接、组装 `SpawnImageInfo` 并经 Process::spawnImage 对驻留 task.kernel 对象发起请求创建进程，同时维护进程表。授予能力：`taskCapabilitySpawn`。

## 1. 常量

| 常量 | 值 | 说明 |
|---|---|---|
| `maximumArgumentCount` | 64 | 最大参数个数 |
| `executePayloadSize` | 56 字节 | Execute 请求参数区（7 个 uint64） |
| `maximumExecutableSize` | 8 MiB | 可执行文件上限 |
| `taskRecordCapacity` | 64 | 进程表容量 |
| `executableReadWindow` | 8 | 异步读流水线深度 |

普通动态程序读取 PT_INTERP 指定的解释器（通常 `/lib/ld-osinfo-x86-64.so`），`sharedImageCount = 1`。DT_NEEDED 依赖由 Ring3 解释器加载，task 不再固定读取四个库；静态程序不附带共享镜像。

## 2. IPC 请求（`os::TaskRequest`）

| 请求 | 值 | 语义 |
|---|---|---|
| Spawn | 1 | `data[0]` = `ProgramId`，`handles[0]` = bootstrap 句柄；`spawnProgram` 启动内置镜像；成功回传进程句柄 |
| Execute | 2 | 从磁盘加载并启动程序（核心路径，见下） |
| Wait | 3 | `handles[0]` = 进程句柄；`getTaskState` 查询；回复 `data[0]`=state（<0 时为 2）、`data[1]`=exitCode |
| List | 4 | `data[0]` = 索引；回复 pid、state、48 字节进程名；越界 `data[0]=1` |
| Kill | 5 | `data[0]` = pid；`terminateTask(handle, -15)`（SIGTERM） |

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
3. `Executable::interpreter()` 校验 ELF 头及 PT_INTERP 的单一、绝对、NUL 结尾路径；
4. 有解释器时读入该文件；缺少解释器明确失败；依赖库搜索交给独立 Ring3 链接器；
5. 组装 `SpawnImageInfo`（bootstrap 句柄、三个标准流句柄、能力、`parentTaskId = request.senderPid`、参数、共享镜像数组）；
6. `os::Process::spawnImage()` 创建进程；内核映射主程序与解释器，并构建 ELF 辅助向量；
7. 关闭请求携带的全部句柄。

**能力边界**：磁盘 SpawnImage 的 capabilities 和 serviceRole 必须为 0；特权服务仅按固定 boot ProgramId 启动。

## 5. 进程表维护

`TaskRecord { uint64_t handle; uint64_t id; char name[48]; }`，容量 64。

- `trackTask()`：登记新创建的进程；
- `reapTaskRecords()`：回收 `state == 2`（已退出）或句柄失效的记录。

## 6. 客户端接口

用户态经 `include/os-c++/os/task.h` 访问（实现见 os 运行时库）：

```cpp
TaskHandle execute(const char* path);
TaskHandle execute(const char* path, int argumentCount, const char* const* arguments);
TaskHandle executeWithIo(const char* path, int argumentCount, const char* const* arguments,
                            uint64_t standardInputHandle, uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
int waitTask(TaskHandle task, int* exitCode);
bool listTask(uint32_t index, TaskListEntry& entry);
bool killTask(uint64_t taskId);
```

当前 task 服务运行于 Ring3，固定 boot 身份提供 Spawn 权限。磁盘 SpawnImage 的 capabilities 和 serviceRole 必须为 0；Display、Users、Memory 等权限不再根据路径授予。普通进程管理由驻留 TaskService 内联执行，磁盘加载由 TaskServer 执行。
