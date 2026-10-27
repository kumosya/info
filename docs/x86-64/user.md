# ELF 程序加载与用户入口

实现位于 `src/kernel/user/elf.cc`、`dynamic_linker.cc` 和 `loader.cc`。ELF 类型声明位于 `include/kernel/elf.h` 的 `kernel::elf`；Task 启动和栈构造声明位于 `kernel/task.h` 的 `kernel::task`。本目录处理程序映像，不是账户或 UID 数据库。

## 文件和类型

| 文件或类型 | 作用 |
| --- | --- |
| `elf.cc` | 静态 ELF 验证、分段权限、普通加载及错误名称。 |
| `dynamic_linker.cc` | 受支持的动态映像集合、符号与 RELA 重定位，或解释器入口准备。 |
| `loader.cc` | 固定模块识别、角色能力、初始句柄、用户栈、子 Task 启动与 init。 |
| Loader | 保存目标 root 或 Task，提供不同来源映像的加载方法。 |
| ElfLoadResult | 入口、映像区间、程序头、应用入口和 bias、解释器基址等结果数据。 |
| ElfSharedImage | 源地址、字节数、名称及可选源 root。 |
| ElfLoadError | 格式、范围、权限、分配、映射、入口和符号错误分类。 |

ELF 头与程序头使用固定格式，内核对象不能按自身内存布局代替它们。加载错误返回分类，不直接把文件字节当作已验证代码。

## 静态映像

| 成员函数 | 功能与参数 |
| --- | --- |
| `Loader(task)` | 以 Task 作为目标，调用时按其地址空间锁稳定 root。 |
| `Loader(destinationRoot)` | 面向已准备的新根，由上层负责生命期。 |
| `load(image, size, result)` | 从内核可访问的映像字节加载，result 接收区间与入口。 |
| `loadFromAddressSpace(sourceRoot, sourceAddress, size, result)` | 从指定源地址空间取字节，避免用当前 CR3 误解另一个进程的地址。 |
| `loadDynamicSetFromAddressSpace(sourceRoot, executableAddress, executableSize, sharedImages, sharedImageCount, result, trustedImports)` | 加载主程序与提供的运行时映像，或准备解释器启动；trustedImports 只用于受控内核导入兼容路径。 |

普通静态入口要求 ELF64、小端、当前版本、x86-64、ET_EXEC，程序头最多 128 个，段内存预算有界。验证文件区间、内存区间、对齐、溢出、段重叠和入口确实落在可执行段。

权限还需要按页合并检查：两个不同段即使字节不重叠，若共享同一页且合并后需要 write+execute，也不能仅按段各自合法就接受。映射先取得清零页，复制文件部分，剩余 BSS 保持零；最终用户权限由段要求生成。

准备失败不能让半成品 Task 运行。原始 root 形式的调用者负责回收部分目标根，启动层统一通过 Task 销毁清理已建立资源。

## 动态集合和解释器

内核动态集合路径最多保存主程序加 4 个附加映像，验证各自 ELF 和动态表。支持的内核 RELA 类型为 R_X86_64_64、GLOB_DAT、JUMP_SLOT、RELATIVE；符号解析处理已定义、绝对和受支持的 weak 情况，找不到必需符号返回 UndefinedSymbol，未知重定位明确失败。

这是有界的加载机制，不等同于把用户动态链接器的全部策略搬到内核。trustedImports 只有显式受控入口才可向 serviceResolve 白名单查询，普通程序不能据符号名取得任意内核地址。

主映像存在 PT_INTERP 时，校验解释器路径字符串，并要求提供一个解释器映像。内核映射应用和自包含解释器，准备程序头元数据，将实际启动入口设为解释器，同时单独保留应用入口、bias 和解释器基址。它不自行按路径调用 FS 打开解释器；所需字节由上层提供。后续运行时链接由 `lib/ld-x86-64/` 完成。

## 用户栈与辅助向量

`buildUserStack(root, programName, argumentCount, arguments, environmentCount, environment, runtimeStateAddress, stackPointer, image, owner)` 在新根中准备用户栈。参数与环境均最多 64 项，每字符串最多 383 字节；固定用户栈为 64 页，栈顶 0x00007ffffff00000，16 字节对齐。

先复制字符串并记录其新地址，再写 argc、argv、envp、结束零和 auxv。旧空间中的字符串指针不能原样带进新空间。参数为空时使用 programName 作为默认 argv[0]。

辅助信息包括程序头、条目大小和数量、页大小、解释器基址、应用入口、执行名、运行时状态、应用 bias，以及 OS_AUX_KERNEL_HANDLE、OS_AUX_REGISTRY_HANDLE。前者指内核 IPC 端点，后者指用户注册中心；buildUserStack 会为 owner 确认真实初始内核句柄。失败时不能把空句柄包装为可启动结果。

## 固定服务与普通映像启动

`loadProgram(program, parent, bootstrapHandle, argumentCount, arguments, loadError)` 按受控 ProgramId 查找引导模块，建立 Task、角色与能力，复制并限制注册中心句柄，加载运行时和栈，最后创建 User 线程。固定服务身份不使线程进入 Ring0，加载路径显式保持普通用户执行。

`loadProgramImage` 区分提供映像及句柄的 source 与逻辑 parent，可转交三条标准端点、终端关联和新会话。设置服务角色前的身份检查在 task.kernel 请求层执行，不能把这个内核内部函数当成应用自由授予能力的入口。

`serviceCapabilities(role, capabilities)` 查受控角色配置。`startInitialProgram(boot)` 初始化模块集合，加载 init 并登记为孤儿进程接收者。

所有路径都应在映像、参数、句柄、凭据和首线程现场准备完成后才开放执行。失败返回的 loadError 与 TaskReference 分别负责错误说明和对象生命期；已建 Task 的根、句柄和线程由正常回收路径处理。exec 的旧根提交及兄弟线程停止另见 [任务管理](task/task.md)。
