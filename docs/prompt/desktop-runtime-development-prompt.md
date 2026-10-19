# 桌面稳定性、用户目录、动态链接与独立开发环境实施 Prompt

你正在维护 OSInfo 混合内核及其用户空间。先阅读本文，再从 docs 阅读相关设计与接口介绍，核对实际源码后实施；不能把写计划或文档当作实现完成。保留工作区已有修改，不自行提交、切分支或重置 Git。

## 已确认的架构约束

- 所有服务进程运行在 Ring3，包括 guiw、fs、终端、storage、pci、device、MM、pipe、msgq。Ring0 仅保留基础机制与 task.kernel、pci.kernel、device.kernel、memory.kernel、system.kernel 等机制对象。
- 不新增 syscall，不引入用户态端口访问；现有 syscall 仅面向 Ring3。通过现有 IPC、句柄和受限机制接口实现扩展。
- OS 库业务 API 与服务逻辑使用类方法。本文要求的“函数”是可调用的实际接口，优先采用类方法，不用宏代替路径接口。
- 内存管理头文件只允许 mm.h、page.h；PageObject 使用构造函数。
- 继续完成此前 Task 回收、远端 CPU exec/kill 和 MM 地址空间 generation 修复的最终验证，保留证据。

## 先阅读的文档与源码入口

先阅读 README.md、docs/x86-64/architecture.md、docs/service/overview.md，再按下列主题阅读并整理一份简明现状介绍，写入实施进度文件：

1. GUI 和输入：docs/service/window.md、device.md、tty.md、console.md，及 docs/lib/os/clients.md。
2. 启动、登录、进程参数：docs/service/users 相关实现、docs/service/task.md、init.md、docs/x86-64/user.md、task/task.md、docs/lib/os/runtime.md。
3. 文件系统和用户目录：docs/service/fs.md、docs/lib/os/overview.md、docs/lib/libc/libc.md。
4. ELF 与共享库：docs/x86-64/user.md、docs/x86-64/boot.md、docs/lib/os/runtime.md、docs/lib/libc++/libcxx.md。
5. 软件包与构建：docs/app/inpkg.md、项目 Makefile、user/Makefile、script/packages.py 及现有包配置。
6. 检查 D:/osinfo/guiw、D:/osinfo/inpkg、D:/osinfo/infoide 的实际内容和适用 AGENTS.md，复用已有工作，不覆盖用户的独立项目。

文档与源码不一致时先核实，记录实际状态并修正文档。不要仅从旧设计推断问题原因。

## 第一阶段：复现并修复两个故障

### guiw 点击几次后卡死

- 在实际 GUI 中复现，记录具体点击、输入、窗口、线程/服务状态及必要的串口证据。
- 区分整机停止调度、服务退出、IPC 互等、事件循环阻塞、输入队列或共享帧缓冲问题。
- 检查鼠标消息、窗口对象生命周期、同步 RPC、锁顺序、共享内存和绘制路径；依据复现证据修复根因。
- 回归连续点击、移动、切换窗口、关闭窗口和终端输入，验证服务及系统持续响应。

### 新用户登录提示 invalid main(argc, argv) state

- 追踪 users/login → task/spawn 或 exec → 用户栈/运行时 → shell 的 argc、argv、环境和身份切换。
- 检查栈对齐、参数/环境字符串所有权、跨地址空间复制、动态链接器入口约定，不能通过删除检查掩盖错误。
- 验证 root、已有用户和新用户登录、退出及再次登录，参数、HOME、USER、LOGNAME、SHELL、工作目录和权限正确。

## 第二阶段：目录布局与 XDG Base Directory

### 文件系统布局

- 创建并在镜像安装、首次启动和新用户创建路径一致维护 /media、/proc、/run、/lib、/var、/run/lock。
- 维护 /var/log、/var/tmp、/var/lib、/var/cache；/var/tmp 用于跨重启保留的临时文件，/run 为本次启动的运行状态。
- 用户 app 目录迁为 .local，创建 ~/.local/share、~/.local/state、~/.config、~/.cache；用户写法中的 ~./cache、~./local 按 ~/.cache、~/.local 处理。
- 已有用户目录迁移需保留已有文件，处理同名冲突，不覆盖数据。新用户也使用同一规则。
- /proc 的目录存在不等同于已实现 procfs；文档明确实际支持范围。
- shell 提示符将当前用户的 HOME 显示为 ~，子目录显示 ~/子目录；按目录边界匹配，不能把 /root-other 等误缩写。实际工作目录仍使用绝对路径。

### OS 库接口与环境

提供类方法返回具体绝对路径，区分用户和系统用途，并支持附加合法应用名称：

| 用途 | 系统默认 | 用户默认 |
|---|---|---|
| 只读数据 | /usr/share/xxx/ | ~/.local/share/xxx/ |
| 可变数据、状态 | /var/lib/xxx/ | ~/.local/state/xxx/ |
| 配置 | XDG_CONFIG_DIRS 搜索目录，默认 /etc/xdg | ~/.config/xxx/ |
| 缓存 | /var/cache/xxx/ | ~/.cache/xxx/ |
| 日志 | /var/log/xxx/ | 用户状态目录中的日志 |
| 运行时 | /run/，锁位于 /run/lock | /run/user/<uid>/ |

- 完整支持 XDG_DATA_HOME、XDG_CONFIG_HOME、XDG_STATE_HOME、XDG_CACHE_HOME、XDG_RUNTIME_DIR、XDG_DATA_DIRS、XDG_CONFIG_DIRS。
- DATA_DIRS 默认 /usr/local/share:/usr/share，CONFIG_DIRS 默认 /etc/xdg；保留搜索优先级，忽略无效相对路径，妥善处理空值与空列表项。
- 登录、exec、spawn、fork 和程序环境继承规则一致；跨用户登录重建该用户环境，不能继续使用前一用户的 HOME/XDG 路径。
- XDG_RUNTIME_DIR 必须归目标用户所有、权限 0700，与登录/启动生命周期协调，不能回退到所有用户共享的可写目录。
- 返回值生命周期、缓冲区容量、错误和路径拼接边界清晰；拒绝应用名称中的路径穿越。查询路径不隐式创建文件，创建所需目录使用明确接口。

## 第三阶段：真正的用户态动态链接器

- 支持 ELF PT_INTERP 与 DT_NEEDED，提供独立 /lib/ld-osinfo-x86-64.so。
- 应用可选择依赖库，按明确定义的库搜索规则处理依赖图、重复依赖、符号查找和必要 x86-64 重定位。
- 明确主程序与解释器的映射、入口、argc/argv/envp/auxv 约定，动态链接器在用户态完成链接；不得以仍由内核预先链接所有库来冒充独立解释器。
- 结合现有实现支持必要的 DT_RPATH/DT_RUNPATH、LD_LIBRARY_PATH 和 $ORIGIN；特权程序不得信任不安全环境搜索路径。
- 依赖缺失、解释器缺失、格式或 ABI 错误、未解析符号必须报告具体路径/名称与失败原因，不能继续运行部分链接的程序。
- 验证普通程序、带传递依赖的程序、自选库、库缺失、符号缺失、exec、fork 及服务启动；不破坏 boot 身份与授权边界。

## 第四阶段：独立项目与 Info IDE

- Info IDE 项目位于 D:/osinfo/infoide。用户已确认运行于 Windows/WSL 宿主，面向 OSInfo 应用进行交叉编译和打包。提供可运行的工程创建/打开、源码编辑、构建诊断、打包及安装/运行流程，先检查现有技术栈和构建工具。
- guiw 项目位于 D:/osinfo/guiw；inpkg 项目位于 D:/osinfo/inpkg，分别具备独立源码、构建入口和开发说明，使用明确版本的 OS SDK/ABI。
- OSInfo 仓库侧仅安装 guiw/inpkg 软件包，解除对其应用源码直接编译的依赖；保留内核/服务启动需要的包资源、清单及授权校验。
- 包内组件、服务和动态依赖明确声明，安装时验证包完整性及所需 ABI；更新、构建失败和依赖缺失必须可诊断。
- 先使外部项目能够独立构建并生成可安装包，验证安装和启动后，再清理被替代的仓库内构建路径；不能丢失已有改动。
- 文档说明各项目边界、SDK 获取、构建/调试/包安装命令；不要把界面原型或空工程称为已完成 IDE。

## 验证和交付

- 对故障修复提供实际复现及修复后证据；GUI 必须进行连续交互回归。
- 运行适当的构建、对象化/头文件/ABI 检查，以及单核/多核启动与任务生命周期测试。
- 覆盖新用户登录、HOME 缩写、XDG 覆盖/回退/权限、动态链接成功与错误、外部包构建和安装。
- 更新 docs 中的架构图、目录布局、运行时 ABI、服务与包开发说明，并在 docs/prompt/desktop-runtime-development-progress.md 记录事实、验证命令和明确未完成项。
- 不新增 syscall，不掩盖错误、不宣称未实际验证的功能已完成。
