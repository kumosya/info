# 桌面与开发环境实施记录

规格：[desktop-runtime-development-prompt.md](desktop-runtime-development-prompt.md)。保留混合内核、Task 生命周期与内存重构成果。所有服务仍为 Ring3，19 个有效 syscall；未增加端口访问接口。

## 文档导览

- [架构](../x86-64/architecture.md)、[服务总览](../service/overview.md)：Ring3 服务与 Ring0 机制边界、启动依赖及架构图。
- [窗口](../service/window.md)、[终端](../service/console.md)、[TTY](../service/tty.md)：合成、输入、事件通知与 PTY。
- [用户程序](../x86-64/user.md)、[动态链接](../x86-64/dynamic-linker.md)、[运行时](../lib/os/runtime.md)：ELF 映射、解释器、辅助向量与 libc 状态。
- [目录 API](../lib/os/directories.md)、[软件包](../app/inpkg.md)、[Info IDE](../app/infoide.md)：XDG、迁移、SDK 与独立构建。

开工时普通应用由内核链接固定四库，guiw/inpkg 尚在 OS 仓库构建，用户目录使用 app。下面记录实际变更和证据。

## 已实现

- guiw 持全局锁无限发送通知的问题已复现。改为非阻塞通知，合成线程重试；事件领取后清等待标志。旧版饱和复现见 `build/desktop-notification-repro.log`，修复后的队列饱和和恢复已运行通过。
- shell 接受合法的 sh、/bin/sh、-sh argv[0]，仍检查参数结构。HOME 和子目录按边界显示 ~ 与 ~/...。真实 PTY 用户密码登录、环境、提示符、最后会话运行目录清理已通过。
- 修正 libc mkdir 对既有目录误报 EIO；终端 File 使用实际 PTY 句柄；read/write 按打开模式支持 O_RDWR PTY；密码输入使用当前描述符；TTY 和图形终端读取线程正常结束时调用 exitThread。
- Directories 类提供用户/系统数据、状态、配置、缓存、日志、运行目录及搜索路径。支持全部七个 XDG 变量，实时观察环境变更，校验 runtime 的 UID 与 0700，拒绝应用名穿越。
- 统一布局加入 /media、/proc、/run/lock、/lib、/var/{lib,cache,log,tmp}；用户 app 合并迁到 .local，冲突保留。users 服务负责会话及 /run 清理。系统包数据库迁到 /var/lib/inpkg，用户包状态为 .local/state/inpkg。/proc 目前只是目录，未实现 procfs。
- 独立 `/lib/ld-osinfo-x86-64.so` 负责 DT_NEEDED、依赖搜索、符号、RELA、W^X/RELRO 与库构造析构；内核映射 PT_INTERP 并传递辅助向量。普通应用不再由内核固定链接四库。boot 模块保留封闭引导链接路径，仍为 Ring3。
- guiw/inpkg 源码迁至 D:/osinfo/guiw、D:/osinfo/inpkg，各有 Makefile 与项目配置；OS 仓库只导入和安装 .inpk。SDK 导出公共头文件、库、CRT、链接脚本与构建工具，不导出内核私有头文件。
- D:/osinfo/infoide 为 Windows/WSL 宿主基础 IDE，提供文件列表、编辑、新建、保存、编译和打包。实际从 Windows 调 WSL 编译模板应用成功。尚无 LSP、补全或断点调试。当前没有可连接的浏览器自动化表面，未完成 IDE 页面截图验收；HTTP 边界测试与真实构建已通过。
- 包缓存改为比较实际内容；归档、独立项目产物经临时文件替换。root Makefile 改用单一依赖图，避免递归 make 重复链接同一产物。

## 验证证据

- `build/desktop-final-build.log`：完整构建、内核/OS 头文件与对象接口审计通过。
- `build/desktop-final-runtime.log`：包边界下的新镜像四核运行通过 XDG、窗口通知饱和、三种 argv[0]、真实 user 登录、~ 提示符、会话清理、传递依赖/构造函数与缺库/缺符号错误。`build/ld-session-runtime.log` 是随后修正构造前初始化顺序的最终回归，额外检查构造函数中的分配/环境修改与库析构。
- `build/external-guiw-build.log`、`build/external-inpkg-build.log`：两个独立项目仅使用 SDK 编译打包成功。
- `build/infoide-windows-build.log`：宿主 IDE 编译 hello.inpk 成功；`build/infoide-security-tests.log` 验证令牌、跨源写入、越界路径和保存冲突。
- `build/package-cache-tests.log`：更新但负载错误的缓存重建、损坏归档安装前拒绝已通过。
- `build/service-object-audit.log`：OS 服务及外部 guiw 服务没有游离业务函数。
- 先前 TaskReclaimer 32 轮清理发起者被杀、64 轮远端 exec/kill 与 MM generation 测试已通过；证据为 `build/hybrid-lifecycle-result.log`。

## 最终回归

1. 实际 GUI 完成 100 次点击、新建第二标签、键盘输入及应用执行。`build/gui-input-after.png` 确认 Info IDE 在 Windows/WSL 编译并打包的自定义库应用在目标 OS 输出 answer=42；`build/gui-input-result.log`、`build/gui-input-runtime.log` 无运行故障。
2. 最终加载器与目录改动下，32 轮清理发起者被杀、64 轮远端 exec/kill、MM 映射/COW/共享/文件/信号、IPC deadline、Ring3 pipe/msgq 及 exec generation 均通过，见 `build/desktop-final-lifecycle-result.log` 与 `build/desktop-final-lifecycle.log`。
3. SDK 自定义 shared 目标、应用选择项目内库、打包并安装执行已验证，宿主构建证据为 `build/infoide-custom-library.log`。没有提交、建分支或修改用户原有 LICENSE。

首次测试曾在链接期间打包，得到损坏 libc 缓存；这些启动失败不作为 GUI 缺陷证据。之后已在稳定产物重现通知死锁，并用内容校验修正包缓存。测试只使用 build 下隔离镜像和 QEMU snapshot，不修改用户原 osinfo.img。
