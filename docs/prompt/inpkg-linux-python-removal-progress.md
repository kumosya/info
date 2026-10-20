> 后续要求已更新：仅使用 packages/，base-tools 拆分四包，删除全部 Python（含格式化例外），InfoIDE 改用纯光盘调试和独立 Linux 安装包。当前实现与验证见 [补充记录](cd-only-infoide-progress.md)。以下保留当时记录。

# 双平台 inpkg、集中配置与去 Python 实施记录

> 后续本地二进制工具、configure、包目录、InfoIDE 命名/参数和标准头迁移见 [最新实施记录](configure-tools-progress.md)。本记录保留上一阶段流程语境。

日期：2026-10-20。执行依据为 [实施 Prompt](inpkg-linux-python-removal-prompt.md) 与 [补充大纲](inpkg-linux-python-removal-outline.md)。本记录承接 [v0.1.1 实施记录](v0.1.1-refinement-progress.md)，其中的旧 Python 命令仅保留历史语境，当前入口以本文及应用文档为准。

## 已实现

- `system.kernel` 从 `src/kernel/system/service/` 迁入 `src/kernel/task/sys/service.cc`。端点、权限和 ABI 保持原值，旧目录不保留副本。
- `D:/osinfo/inpkg` 提供 Linux 与 OSInfo 两个 C++ 入口，共享 `src/common/` 的 manifest、ustar/gzip、SHA-256、归属校验与安装事务。Linux 适配在 `src/linux/`，目标系统适配在 `src/osinfo/`。不调用外部 tar 或 Python 后端。
- `install` 仅首次安装；`upgrade` 要求已安装；`upgrade --install-missing` 仅在包确实未安装时安装。损坏、冲突、锁或恢复错误不会触发首次安装。同版本允许显式更新，支持增删包内文件。
- Linux 修改、查询已安装状态必须指定 `--sysroot`，逐层通过根目录描述符访问文件，拒绝宿主根、符号链接逃逸及特殊成员。OSInfo 保留 system/local/root 范围，不接受 Linux sysroot 选项。
- 保留原始 `NAME.conf`，新增 `NAME.state` 保存相对路径、SHA-256 与模式。更新和卸载保护其他包、无归属现存文件及本地修改。旧记录需要显式 `adopt` 建立当前文件基线。
- 事务保存新文件、备份、日志及摘要，变更前发布 journal，完成后写 committed。恢复先校验完整日志与全部备份，再回滚；损坏时保留事务并报错。提供显式 `recover`，查询遇到未清理事务也报错。
- Linux `catalog` 读取包集，生成 Make 清单、打包、检查及整批预检，然后逐包 upgrade --install-missing。`osinfo pkg` 使用 C++ inpkg；查找顺序为 INPKG、PATH、相邻 inpkg 项目的 Linux 产物。
- 根构建自举宿主 C++ 工具。`config/services.conf`、`applications.conf`、`layout.conf`、`packages.conf` 分别维护服务、应用、布局和包集；审计与 smoke 策略使用 `checks.conf`、`smoke.conf`。字段与生成产物见 [配置说明](../../config/README.md)。
- `tools/host/` 负责服务/引导/init 配置生成、SDK 导出、布局及权限、系统文件部署、源码和导入审计、隔离镜像处理、QMP 输入。shell 只编排外部系统工具，配置不通过 source/eval 执行。
- 引导表显式生成 ServiceRole，使用 static_assert 检查固定 ABI；task/device 不递归纳入子服务入口。服务运行库、会话路径和启动依赖从配置生成。
- 原 Python 脚本、SDK 副本、构建辅助文件与缓存已按清单迁移、清理，唯一例外是 OS 仓库根目录 `clang-format.py`。清单保存在 `build/native-python-removal-files.log`；`make check` 持续扫描全部关联项目。

## 正常开发流程

在 Linux 下从 OS 仓库根目录执行：

```sh
make -C ../inpkg host
make -C ../infoide
make sdk
make -C ../guiw
make -C ../inpkg
make -C ../osinfo-base-tools package
../inpkg/build/linux/inpkg import ../guiw/build/packages/guiw.inpk --package-dir packages/external
../inpkg/build/linux/inpkg import ../inpkg/build/packages/inpkg.inpk --package-dir packages/external
../inpkg/build/linux/inpkg import ../osinfo-base-tools/build/packages/osinfo-base-tools.inpk --package-dir packages/external
make all check
```

镜像同步继续使用 `make install`，内部以 catalog 全量预检和 upgrade --install-missing 同步；缺少外部包会失败，不隐式构建外部项目。离线单包操作示例：

```sh
../inpkg/build/linux/inpkg upgrade hello.inpk --sysroot /mnt/osinfo --install-missing -y
../inpkg/build/linux/inpkg info hello --sysroot /mnt/osinfo
../inpkg/build/linux/inpkg recover --sysroot /mnt/osinfo
```

只有旧数据库缺少摘要且已经人工核对当前文件时才使用 `adopt NAME --sysroot ROOT -y`；它记录当前基线，不能证明旧文件此前未修改。旧布局中冲突的废弃包归属需要先处理，不会自动抹掉旧记录。

## 验证与保证边界

所有 QEMU 测试使用 `/tmp/osinfo-smoke.*` 内的隔离镜像及 snapshot，原 `build/osinfo.img` 只用于读取分区输入。本轮没有重新格式化或同步用户的原始磁盘。

| 验证 | 日志与范围 |
| --- | --- |
| Linux inpkg | `build/native-inpkg-tests.log`：归档、SHA-256、sysroot、upgrade/missing、本地修改、路径冲突、损坏日志与进程中断恢复 |
| C++ CLI | `build/native-cli-tests.log`：项目创建、自选 SDK/编译器、空格路径、C/C++、共享库与打包 |
| 常规构建与审计 | `build/native-final-build.log`：all/check/sdk、服务对象与可信导入、内存头边界、Python 全量扫描 |
| 独立输出目录 | `build/native-clean-build.log`：独立 BUILD_DIR、OBJECT_DIR、PACKAGE_DIR 的 all/check/sdk，自举宿主生成工具 |
| 宿主内存、IPC、配置 | `build/native-host-memory.log`、`native-host-ipc.log`、`native-host-system.log` |
| 单核内存 | `build/native-memory-smp1.log`：MM、IPC、配置以及自定义版本和 hostname |
| 四核内存 | `build/native-memory-smp4-check.log`：匿名/文件/共享映射、保护、部分解除、COW、信号、服务期限、exec 代次及 Ring3 pipe/msgq；带 shell 退出路径诊断，返回成功 |
| 目标 inpkg | `build/native-inpkg-cross-runtime.log`：OSInfo 打包、安装、升级、缺包分支、卸载、root 私有范围，以及 Linux 数据库的目标系统升级和卸载 |
| 桌面动态链接 | `build/native-desktop.log`：通知队列、shell argv、XDG、真实登录、传递依赖、构造/析构、缺库及未定义符号 |
| 真实桌面输入 | `build/native-desktop-input.log`：QMP 注入 100 次点击、新标签与键盘；`build/native-input-after.bmp` 已目视确认第二标签执行 echo gui alive 并显示输出 |
| 四核任务生命周期 | `build/native-lifecycle.log`：清理请求者被杀 32 轮，远端 CR3 exec 与并发 kill 64 轮 |
| 基础工具 | `build/native-base-tools-runtime.log`：配置驱动的 sysinfo、mount/umount、mkfs 拒绝路径及 coreutils |
| 最终静态核对 | `build/native-final-inventory.log`：关联项目只剩根目录 clang-format.py；shell 语法与 Git diff --check 通过，架构文件无差异 |

目标集成测试暴露并修复了两项实际问题：清单重复检查创建大量临时字符串，触发目标堆的频繁页映射；OSInfo 的 fsync 返回 ENOSYS，不能把宿主同步实现直接搬入目标适配。前者改为直接字符串比较，后者使用已确认完成的文件写入和关闭。

独立输出目录构建完成后，最新目标 inpkg 归档已再次导入并通过七包全量校验，记录为 `build/native-clean-package-check.log`。桌面回归曾因 `/tmp` 容量不足而中止；清理已结束测试的临时镜像后重跑通过，串口日志和输入截图保留。

Linux 使用 flock，OSInfo 使用带进程身份的目录锁，两者不协调跨系统并发；挂载修改前必须停止使用同一卷的运行中 OSInfo。保证范围为逐包事务及经过测试的进程中断恢复；整批预检不代表整批原子提交，OSInfo 当前没有 fsync，不承诺断电持久性或断电原子性。

架构文件不修改，Git 提交、分支与推送不执行。
