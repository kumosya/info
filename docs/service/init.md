# init 服务（src/init/）

init 是第一个用户进程（由内核 `startInitialUserland` 加载），职责：

1. 运行**服务注册表**（service registry）；
2. 按顺序启动全部内置服务与磁盘服务；
3. 启动终端会话。

授予能力：`processCapabilitySpawn`。

## 1. 服务注册表

注册表是 init 进程内的一个专用线程（`registryMain`），监听独立端点。容量 12 条记录：

```cpp
struct ServiceRecord { char name[24]; uint64_t endpoint; };
```

请求类型（`os::RegistryRequest`）：

| 请求 | 值 | 语义 |
|---|---|---|
| Register | 1 | `handleCount == 1`；把 `handles[0]`（服务端点）与 `data` 中的名字关联；同名重复注册替换端点；容量满则失败。回复 `data[0]=0` |
| Lookup | 2 | 按名字查找；成功 `data[0]=0` 且 `handles[0]` 回传端点；失败 `data[0]=1` |
| 其他 | — | 回复 `data[0]=2` |

启动后调用 `os::setServiceRegistry` 公布注册表端点；其他服务通过引导句柄（bootstrap handle）找到它。

## 2. 启动流程（`userMain`）

1. `createEndpoint` 创建注册表端点；在 `0x70000000` 映射 4 页栈，`createThread(registryMain)` 启动注册表线程。
2. 依次 `spawnProgram`（内核内置镜像，传入注册表端点作为引导句柄）：
   - `ProgramId::TaskServer`
   - `ProgramId::DeviceServer`
   - `ProgramId::StorageServer`
   - `ProgramId::FsServer`
3. `lookupService("fs", 30000)` 等待 fs 服务注册（最多 30000 次重试）。
4. 从磁盘启动（`os::execute`）：
   - `/system/services/console.elf`
   - `/system/services/window.elf`
   - `/usr/bin/terminal`（GUI 终端）
5. `switchTerminal(os::graphicalTerminalId)`（=6，即 tty7 图形会话）。
6. 为 tty1-6（`os::textTerminalCount = 6`）各启动 `/bin/shell`，参数 `--os-terminal=N`（N=1..6）。
7. 进入 `sleep(1000)` 无限循环保活。

## 3. 失败码

| 返回码 | 含义 |
|---|---|
| 1 | 注册表端点/线程创建失败 |
| 2 | 核心内置服务启动失败 |
| 3 | fs 服务发现失败 |
| 4 / 5 / 6 | console / window / terminal 启动失败 |
| 7 | 图形终端切换失败 |
| 8 | TTY shell 启动失败 |
