# 用户程序加载（src/kernel/user/）

本文档描述 `src/kernel/user/` 的实现：静态 ELF 加载器（`elf.cc`）、内核内动态链接器（`dynamic_linker.cc`）、用户程序加载与用户栈构建（`loader.cc`）、POSIX 系统调用代理层（`posix.cc`）。对应头文件 `include/kernel/elf.h`、`task.h`（`UserProgramLoader` 类）、`posix.h`。

## 1. 静态 ELF 加载器（elf.cc）

### 支持范围

仅支持 **ELF64、小端、ET_EXEC（静态可执行文件）、EM_X86_64**。拒绝 PT_DYNAMIC 与 PT_INTERP（动态文件由动态链接器路径处理）。

### 错误码

```cpp
enum class ElfLoadError : uint8_t {
    None, InvalidArgument, InvalidMagic, UnsupportedClass, UnsupportedEndian,
    UnsupportedVersion, UnsupportedMachine, UnsupportedType, InvalidHeader,
    InvalidProgramHeader, UnsupportedProgramHeader, InvalidSegment,
    InvalidAddress, OverlappingSegments, InvalidPermissions, OutOfMemory,
    MappingFailed, InvalidEntryPoint, UndefinedSymbol
};
struct ElfLoadResult { uint64_t entryPoint, imageBase, imageEnd; };
struct ElfSharedImage { uint64_t sourceAddress, size; const char* name; };
```

### 校验规则

**头部校验**：魔数 `\x7fELF`；ELF64；小端；版本 1；ET_EXEC；EM_X86_64；头尺寸与程序头表范围合法；入口点在 `[0x10000, 2^47)`。

**段校验**（每个 PT_LOAD）：

- `fileSize <= memorySize`，文件区间在镜像内；
- 无未知标志位；**禁止可写+可执行**（W^X）；
- alignment > 1 时须为 2 的幂且 `vaddr & (align-1) == offset & (align-1)`；
- vaddr 区间在用户半区内；累计映射量 ≤ 256 MiB；
- 与其他可加载段不重叠；
- 入口点必须落在某个可执行段内；
- 第二遍逐页检查：任何页聚合出可写+可执行 → 拒绝（跨段共享页的 W^X）。

### 接口

```cpp
class ElfLoader {
   public:
    static const char* errorName(ElfLoadError error);
    static bool runValidationSelfTest(Task& task);

    static ElfLoadError load(Task& task, const void* image, size_t size,
                             ElfLoadResult& result);
    static ElfLoadError loadFromAddressSpace(Task& task, uint64_t sourceRoot,
                                             uint64_t sourceAddress,
                                             size_t size,
                                             ElfLoadResult& result);
    static ElfLoadError loadFromAddressSpace(uint64_t destinationRoot,
                                             uint64_t sourceRoot,
                                             uint64_t sourceAddress,
                                             size_t size,
                                             ElfLoadResult& result);
    static ElfLoadError loadDynamicSetFromAddressSpace(
        uint64_t destinationRoot, uint64_t sourceRoot,
        uint64_t executableAddress, size_t executableSize,
        const ElfSharedImage* sharedImages, uint32_t sharedImageCount,
        ElfLoadResult& result);
};
```

加载流程：校验 → 持地址空间锁预检目标页空闲 → 逐页 `memory::allocateUserPageInAddressSpace`（按段权限设置 `pageUser`/`pageWritable`/`pageNoExecute`）→ 复制 `fileSize` 内容 → 失败完整回滚。

自检（内核启动时运行）：用合成镜像验证 InvalidMagic、InvalidPermissions（W+X）、OverlappingSegments、InvalidEntryPoint、跨页 W^X 等用例，最后验证一个合法镜像加载成功且页表标志正确。

## 2. 动态链接器（dynamic_linker.cc）

内核内一次性完成**加载时链接**（无惰性绑定、无 dlopen）。实现 `ElfLoader::loadDynamicSetFromAddressSpace`。

### 加载布局

| 镜像 | 基址 |
|---|---|
| 可执行文件 | `0x400000`（ET_EXEC 固定链接地址时 bias=0） |
| 共享库 1 | `0x0000001000000000` |
| 共享库 2 | `0x0000001400000000` |
| 共享库 3 | `0x0000001800000000` |
| 共享库 4 | `0x0000001c00000000` |

最多 1 个可执行文件 + 4 个共享库（`maximumImages = 5`）。共享库必须为 `ET_SHARED`，按 `bias = preferredBase - minimum` 平移重定位。

### 动态节解析

处理的标签：`DT_HASH`、`DT_STRTAB`、`DT_STRSZ`、`DT_SYMTAB`、`DT_SYMENT`、`DT_RELA`、`DT_RELASZ`、`DT_RELAENT`、`DT_JMPREL`、`DT_PLTRELSZ`、`DT_PLTREL`。一致性要求：`DT_SYMENT == 24`、`DT_RELAENT == 24`、PLT 重定位格式必须为 RELA、strtab/symtab/hash 必须存在。**符号数量从经典 DT_HASH 表的 nchain 读取**（≤65536），不支持 DT_GNU_HASH。

### 符号解析

按镜像顺序（可执行文件在前）计算请求名称的经典 ELF 哈希，查询 DT_HASH 的 bucket/chain。候选条件仍为非 `SHN_UNDEF`、绑定为全局或弱、名字一致；名称按小块读取，避免逐字节反复遍历页表。校验完整哈希表范围、bucket 数、符号索引，并以 nchain 限制链遍历次数，损坏或环形链不能无限循环。命中值 = `SHN_ABS ? value : bias + value`。未命中但请求方为弱绑定 → 值 0；否则 `UndefinedSymbol`。

### 重定位

支持 4 种类型（全部加载时立即处理，含 JMPREL）：

| 类型 | 值 | 写入值 |
|---|---|---|
| `R_X86_64_RELATIVE` | 8 | `bias + addend` |
| `R_X86_64_64` | 1 | `symbolValue + addend` |
| `R_X86_64_GLOB_DAT` | 6 | `symbolValue` |
| `R_X86_64_JUMP_SLOT` | 7 | `symbolValue` |

不支持符号版本控制与惰性 PLT。

## 3. 用户程序加载器（loader.cc）

### 内置程序表

与 `os::ProgramId` 一一对应，镜像来自 Multiboot2 引导模块：

| ProgramId | 引导模块名 | 进程名 | 授予能力 | SystemService |
|---|---|---|---|---|
| Init (0) | `init` | `init` | Spawn | 否 |
| TaskServer (1) | `task` | `task-server` | Spawn | 否 |
| FsServer (2) | `fs` | `fs-server` | 无 | 否 |
| DeviceServer (3) | `device` | `device-server` | Device | 是 |
| StorageServer (4) | `storage` | `storage-server` | Device | 是 |

另有引导模块 `libos-c++.so` 作为 C++ 运行时共享库（`bootRuntimeImage`）。**内置程序总是与 `libos-c++.so` 一起动态链接加载**。

DeviceServer 和 StorageServer 被标记为 `systemService`，其线程使用 `ThreadType::SystemService`，通过 0x80 中断门进行快速系统调用（避免 SYSCALL/SYSRET 开销）。

### 用户栈构建 `UserProgramLoader::buildStack`

```cpp
class UserProgramLoader {
   public:
    static ElfLoadError buildStack(
        uint64_t rootPhysicalAddress, const char* programName,
        uint32_t argumentCount, const char* const* arguments,
        uint32_t environmentCount, const char* const* environment,
        uint64_t runtimeStateAddress, uint64_t& stackPointer);
    // ...
};
```

- 栈顶 `0x00007ffffff00000`，64 页（256 KiB），页标志 `pageUser | pageWritable | pageNoExecute`。
- 参数 ≤64、环境变量 ≤16，每个字符串 ≤383 字节。
- 从栈顶向下：先逆序压入环境变量字符串，再逆序压入参数字符串；16 字节对齐后布置启动字：

```text
[argc][argv[0..argc-1]][NULL][envp[0..envc-1]][NULL][runtimeStateAddress]
```

- 无参数时默认 `argv = {programName}`。

### 加载流程

**`UserProgramLoader::load(program, parent, bootstrapHandle, argc, argv, loadError)`**（内置程序）：

1. 校验索引、父进程、引导模块就绪；
2. `Task::create`；授予能力；DeviceServer/StorageServer 设置 `systemService`；
3. `bootstrapHandle != 0` 时复制端点句柄到子进程（`Endpoint::duplicateHandle`）；
4. `ElfLoader::loadDynamicSetFromAddressSpace`（与 `libos-c++.so`）；
5. `buildStack`；`Thread::createUser`（传入 `initialStackSize` 供 SystemService 复制栈到内核侧）；失败销毁进程。

**`UserProgramLoader::loadImage(...)`**（SpawnImage 通用路径）：

1. 创建进程、授予调用方指定能力；复制引导句柄（`Endpoint::duplicateHandle`）；
2. 三个标准句柄：通过 `Pipe::duplicateHandle` 复制 Pipe 或 Channel；任一标准句柄为 Channel 时 `createSession()`；
3. `sharedImageCount == 0` 用静态加载，否则动态加载；
4. 构建用户栈、创建用户线程。

### 用户态启动 `UserProgramLoader::startInitial(bootInfo)`

1. `initializeBootPrograms`：定位 5 个内置模块与 `libos-c++.so`（记录直接映射虚拟地址与大小），任一缺失失败；
2. 以内核进程（`task::getIdle()`）为父加载 `ProgramId::Init`；
3. 返回 init 进程。失败打印错误名并返回 nullptr（内核停机）。

## 4. POSIX 库与系统调用

POSIX 库与 OS 库共用 `include/os-c++/os/syscall.h` 定义的系统调用号。编号从 1 开始，0 无效。参数布局相同的 POSIX `SYS_*` 宏在 `include/posix/sys/syscall.h` 中直接引用对应 OS 编号；其他 POSIX 函数由 libc 按 OS ABI 适配，不再存在独立的内核 POSIX 分发入口。