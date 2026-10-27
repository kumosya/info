# 快速开始

## 配置流程

首次构建前，先运行：

```sh
./configure
```

`configure` 会根据 host 工具内置的默认值创建配置，并完成以下准备工作：

1. 检查构建环境。
2. 使用 `inpkg` 和 `infoide`，将 host 辅助程序编译为 `tools/osinfo-host`。
3. 将 ZIP 包容器展开到包目录，扫描并校验各个包。
4. 生成构建配置和所需文件。

之后运行 `make` 时，会直接使用已有配置。如果配置缺失或过期，`make` 会提示重新运行 `configure`。

新配置目录可以由 `configure` 独立创建，无需预先准备配置文件。

## 生成文件与输出目录

默认情况下，`configure` 会在 `build/` 下生成：

- `config.mk`
- `packages.conf`
- `package-inputs.mk`
- `packages.mk`

并在 `build/generated/` 下生成：

- `services.mk`
- `boot-images.inc`
- `boot-abi.inc`
- `startup.inc`
- `modules.conf`
- `grub.cfg`

其中，`startup.inc` 只包含建立根文件系统所必需的内置服务及其 owner。

如需更改输出位置，可使用：

```sh
./configure --build-dir 配置输出目录 --object-dir 对象文件目录 --package-dir 包目录
```

后续构建时，通过 `CONFIGURE_BUILD` 指定配置输出目录：

```sh
make CONFIGURE_BUILD=配置输出目录
```

## 配置文件

配置源文件统一放在 `script/config/`，使用 UTF-8 JSON。`tools/osinfo-host defaults --build build` 读取下面三个文件，校验后转换为 `build/config/services.conf`、`build/config/layout.conf` 和 `build/config/checks.conf`。`configure` 会调用这个命令；使用 `--build-dir out` 时，生成位置相应为 `out/config/`。

这三个 `.conf` 是以 `|` 分隔字段的生成文件，供服务清单、安装布局和源码检查读取。需要修改配置时编辑 JSON，再运行 `configure`，不要直接修改 `.conf`。生成内容没有变化时保留原文件的修改时间。三个 JSON 全部解析成功后才开始写入生成文件；错误信息包含源文件路径。

每个 JSON 顶层都有整数 `version`，当前为 `1`。不接受未知字段、重复字段、缺少的必填字段或错误的数据类型。配置字段不能包含 `|`、换行、制表符或零字节；没有内容的服务字段使用 `"-"`。

### script/config/services.json

顶层 `services` 是服务对象数组，每个对象转换为 `services.conf` 的一行。普通服务和内核驻留服务共用这张表。

```json
{
  "version": 1,
  "services": [
    {
      "name": "init", "ring": 3, "source": "init",
      "program": "Init", "id": 0, "role": "Init", "roleId": 1,
      "capability": "taskCapabilitySpawn", "origin": "local",
      "ready": "services", "dependencies": "-", "order": 0,
      "process": "init", "libraries": "-", "traversal": "flat",
      "loading": "boot", "owner": "kernel"
    }
  ]
}
```

此片段只展示一条记录；实际文件需要保留完整的服务目录。

| 字段 | 含义 |
| --- | --- |
| `name` | 服务名称，也是构建和安装时的标识。 |
| `ring` | 执行级别，`0` 为内核驻留服务，`3` 为用户态服务。 |
| `source` | 相对 `src/` 的源码目录；外部包提供的服务使用 `"-"`。 |
| `program`、`id` | 引导程序枚举名和固定编号。 |
| `role`、`roleId` | 服务身份枚举名和固定编号。 |
| `capability` | 允许的能力名称；多个能力用 `+` 连接，不需要能力时使用 `"0"`。 |
| `origin` | 源码来源：`local`、`disk`、`external` 或 `resident`。 |
| `ready` | 启动后需要等待注册的端点名称。 |
| `dependencies` | 依赖的服务名称，用逗号分隔；无依赖使用 `"-"`。 |
| `order` | 启动顺序，依赖项必须排在当前服务之前。 |
| `process` | 启动时使用的进程名称。 |
| `libraries` | 附加链接库；没有时使用 `"-"`。 |
| `traversal` | `flat` 读取当前源码目录，`recursive` 额外读取下一层子目录。 |
| `loading` | `boot` 表示引导加载，`file` 表示从文件加载，`resident` 表示驻留内核。 |
| `owner` | 负责创建和监护该服务的主体：`kernel`、`init`、`task` 或 `device`。 |

`ring`、`id`、`roleId`、`order` 接受非负整数；驻留服务中没有意义的编号可以写 `"-"`。`generate()` 在生成引导代码时继续检查目录是否存在、编号是否唯一，以及依赖和加载方式是否一致。

### script/config/layout.json

顶层 `entries` 是按顺序执行的布局记录数组。每条记录通过 `type` 选择操作，其余字段如下。

| `type` | 必填字段 | 操作 |
| --- | --- | --- |
| `directory` | `path`、`mode` | 在系统根目录下建立目录。 |
| `user-directory` | `path`、`mode` | 在用户主目录下建立目录。 |
| `copy` | `source`、`target`、`mode` | 复制一个文件并设置权限。 |
| `tree` | `source`、`target` | 复制整个目录的内容。 |
| `mode` | `path`、`mode` | 设置已有文件的权限。 |
| `runtime` | `package`、`library` | 从本体运行库包安装共享库。 |
| `external-runtime` | `package`、`library` | 要求外部软件包提供共享库。 |
| `sdk-tree` | `source`、`target` | 把头文件目录复制到 SDK 的 include 目录。 |
| `sdk-file` | `source`、`target` | 向 SDK 复制单个文件。 |
| `legacy-database` | `source`、`target` | 迁移旧包数据库目录。 |
| `legacy-home` | `source`、`target` | 迁移用户主目录中的旧应用目录。 |

权限 `mode` 必须写成字符串，例如 `"0755"` 或 `"1777"`，以保留八进制表示。源码路径相对仓库根目录；目标路径相对安装根目录或对应的 SDK 目录。`source` 支持 `{build}`、`{packages}`、`{objects}`，分别替换为本次构建、软件包和对象文件目录。

```json
{
  "version": 1,
  "entries": [
    {"type": "directory", "path": "usr/bin", "mode": "0755"},
    {"type": "copy", "source": "{build}/kernel.elf", "target": "boot/kernel.elf", "mode": "0755"},
    {"type": "sdk-tree", "source": "include/os-c++", "target": "os-c++"}
  ]
}
```

### script/config/checks.json

这个文件配置源码检查的范围和允许出现的底层入口，不登记第二份服务目录。数组中的每个字符串转换为 `checks.conf` 的一行。

| 字段 | 类型 | 含义 |
| --- | --- | --- |
| `project` | 字符串 | 检查的项目根目录，默认 `"."`。 |
| `memoryRoots` | 字符串数组 | 内存实现的检查目录。 |
| `memoryHeaders` | 字符串数组 | 允许的内存头文件名。 |
| `forbiddenMemoryHeaders` | 字符串数组 | 不允许重新引入的旧内存头文件名。 |
| `auditRoots` | 字符串数组 | 扫描库入口的目录。 |
| `syscalls` | 字符串数组 | 允许的直接系统调用及其底层桥接函数。 |
| `runtimeEntries` | 字符串数组 | 通过运行时或服务 IPC 实现的内部入口。 |

`WaitAddress`、`WakeAddress` 和四个端点消息入口仍是直接系统调用。线程退出、进程管理、句柄管理和内存管理的库辅助函数列在 `runtimeEntries`，不再因为函数名称看起来像系统调用就登记为直接入口。

### 配置规则

- **源码来源与加载来源分别配置。** 源码来源保留原有的四种分类；加载来源单独指定为 `boot`、`file` 或 `resident`。服务是否内置，则根据源码是否位于 OS 仓库中判断。
- **依赖服务先启动。** 依赖列表使用逗号分隔。
- **源码遍历深度有限。** `flat` 只选择当前目录的源码；`recursive` 还会纳入下一层子目录。
- **文件服务当前归 `init` 所有。**
- **固定 ABI 会在编译时校验。** 生成代码使用 `static_assert` 检查固定 ABI。
- `system.kernel` 的源码目录是 `src/kernel/task/sys/`。

## 包管理

包集由 `configure` 扫描生成，不再维护手写的 `packages.conf`：

- `manifest` 行登记 OS 运行库。
- `archive` 行引用 `packages/` 中的 `.inpk` 包。
- 包目录统一使用复数形式 `packages/`。

ZIP 文件作为包含多个包的容器使用：先直接展开到包目录，再逐个校验其中的包。

例如，`osinfo-base-tools.zip` 包含五个独立包：

- `coreutils`
- `sysinfo`
- `mount`
- `mkfs`
- `systemctl`

各子项目在自己的 `build/packages/` 中生成归档。应用映射由包内 manifest 或独立项目的 `infoide-project.json` 维护。

引导服务和会话入口必须由扫描发现的包提供；如果缺少，`configure` 会失败并要求补齐。
