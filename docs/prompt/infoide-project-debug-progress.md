# InfoIDE 项目内硬盘调试与安装路径修正

本轮按用户确认的大纲执行，替代上一轮把 OS 本体默认改为光盘启动的安排。架构文件不修改。

## 已实现

1. OS Makefile 的 run/debug 恢复 install、VirtIO 硬盘启动；debug 保留 GDB 等待。OS 冒烟脚本恢复隔离硬盘。光盘生成不再是本体运行的前置操作。
2. InfoIDE create 同时生成 infoide-project.json 和 infoide-debug.json；前者决定全部编译参数，后者集中管理镜像、启动模式、QEMU、内存、CPU、显示、串口和 GDB。新模板在编译配置中写入 -g，便于源码调试，不对已有配置强加参数。
3. install/run/debug 默认在项目根目录生成 osinfo.img。首次复制模板，以后离线升级包并保留卷内数据；镜像更新经过读回校验，在同目录原子发布。run 正常启动 QEMU，debug 开启本地 GDB，可配置是否暂停。
4. 每个项目使用独立镜像和配置。镜像不能输出到项目外；项目锁与 QEMU 原生 OFD/记录锁阻止占用中更新，构建失败保留原镜像。
5. 本体安装到 /usr/local/bin/infoide；SDK、硬盘模板与启动 sh 全部安装到 /usr/local/lib/osinfo/。安装器仅提供 --destdir 暂存选项，无相邻源码依赖。普通用户可以编译、更新镜像和启动 QEMU。
6. OS 通过通用 image 目标生成 build/export/osinfo.img，不复用或覆盖本体 build/osinfo.img；iso 目标导出 build/export/osinfo.iso，export-images 汇总两种导出。镜像包含 MBR、嵌入式 GRUB 和 ext3 根分区；仅处理普通镜像文件，不挂载宿主磁盘。GRUB BIOS core 的连续扇区块表由 C++ host 工具写入预留空间。
7. 保留可选光盘更新/运行代码。--boot cdrom 显式选择，镜像仍在项目根目录；在 InfoIDE 项目打包时 WITH_CDROM=1 才附带 ISO。可选 rootfs 内存块机制不参与正常硬盘启动。
8. guiw、inpkg、四个 base-tools 子项目及 edit 补齐独立调试配置；同步项目内忽略规则和 InfoIDE 二进制。既有 packages/、去 Python、服务布局及 make install 默认不装 GRUB的安排保留。

## 验证记录

- build/project-cli-test.log：模板、C/C++/汇编参数、项目共享库、打包及失败处理。
- build/project-disk-boot.log：独立测试模板由硬盘直接启动，图形桌面及 tty1–tty6 就绪。
- build/project-infoide-integration.log：暂存安装到固定路径、自动发现 SDK、两个项目独立硬盘真实执行、应用升级与数据保留、调试配置覆盖、QEMU 占用拒绝、构建失败及项目外路径拒绝。
- build/project-infoide-optical.log：显式光盘模式生成、升级、run/debug 参数及光盘内真实应用执行。
- build/project-os-disk-runtime.log：OS 本体隔离硬盘上的映射、保护、COW、文件/共享内存、信号、exec、RPC、pipe/msgq 回归。
- build/project-final-build.log：默认硬盘发行包、OS 构建与静态检查。
- build/project-distribution-test.log：实际发行包本体摘要一致、固定安装布局、启动 sh 的默认硬盘和显式光盘选项、OS make run/debug 均选择硬盘。
- build/project-python-inventory.log：关联项目没有 Python 文件；git diff --check 通过，架构文件无差异。

默认交付物为 InfoIDE 项目中的 build/infoide-linux.tar.gz。安装后从项目目录使用 infoide create/build/pkg/install/run/debug；不自动重建已有项目镜像，不隐式切换光盘模式。

## 安装入口统一

OS 现在仅使用 make install 更新系统：不传 DESTDIR 时安装到已有 build/osinfo.img，传 DESTDIR 时安装到指定目录，默认均不重装 GRUB。run/debug 依赖 install；disk 串行完成 mkimg 后再调用 install，避免并行建盘与写入。镜像挂载、清理及目录部署共用 script/install.sh。

tests/check-install.sh 已使用隔离镜像验证 mkimg→make install、引导区逐字节保持、GRUB 调用拦截、目录安装、镜像锁、失败后的 loop/挂载清理及实际硬盘启动。结果见 build/install-unify-test.log；构建和静态检查见 build/install-unify-build.log，架构文件无差异。

## InfoIDE 安装职责归属

OS 已移除 InfoIDE 专用的模板和安装器目标，仅提供系统 IMG/ISO 与公共 SDK。InfoIDE 使用自身忽略的 resources/ 或 SDK、DISK、ISO 显式输入，在自身项目执行 make installer、make install。安装、项目硬盘及可选光盘测试迁至 InfoIDE 的 tests/distribution.sh、tests/project.sh、tests/optical.sh；OS 不再调用 InfoIDE 生成安装器。上方 build/project-* 日志是此前实现的历史验证记录。

本次职责拆分验证已完成：

- OS 的 build/infoide-separation-build.log：export-images、sdk、check 成功；build/infoide-separation-final-check.log：工具同步后的重新配置及静态检查成功。
- InfoIDE 的 build/separation-check.log：CLI 与默认安装器验证成功；build/separation-cd-installer.log：携带 ISO 的安装器验证成功。
- InfoIDE 的 build/separation-install.log：make install DESTDIR 安装到隔离目录，固定 bin/lib 布局及本体摘要一致；未执行全局安装。
- InfoIDE 的 build/separation-project-retry.log：安装后的工具自动发现 SDK，两个独立项目在真实 QEMU 中执行，升级保留数据，run/debug 配置、镜像占用拒绝和失败保护通过。
- InfoIDE 的 build/separation-optical.log：可选光盘安装、升级、run/debug 参数和光盘内应用真实执行通过。

首次项目回归曾遇内核在服务启动前报告 probe thread affinity failed，见 InfoIDE 的 build/separation-project.log；完整复跑通过，本轮未修改内核。另一次测试临时目录位于 WSL Windows 挂载目录，QMP Unix socket 不受支持，改用 Linux /var/tmp 后继续验证。测试脚本支持 TMPDIR，文档注明文件系统要求。

当前交付物为 D:/osinfo/infoide/build/infoide-linux.tar.gz，默认不含 ISO。OS 目录原有的 InfoIDE 安装包和旧镜像已迁至 InfoIDE 的 build/legacy-os-artifacts/ 留存；新资源位于自身忽略的 resources/，与系统导出逐字节一致。相关项目中的 InfoIDE 二进制已同步。脚本语法、无 Python、git diff --check 和架构文件无差异检查通过。

## InfoIDE 卸载入口

InfoIDE 的 make uninstall 调用自身 script/uninstall.sh，无编译或资源准备依赖；该脚本在构建时嵌入 C++ 工具，发行包生成 uninstall.sh，安装时同时放入 /usr/local/lib/osinfo/uninstall.sh，避免维护多份卸载逻辑。支持 --destdir 和 Makefile 的 DESTDIR，清理本体与专用资源目录，保留项目源码、配置、镜像及其他软件。拒绝安装前缀中的符号链接，支持重复卸载。

InfoIDE 的 build/uninstall-check.log 和 build/uninstall-cd-check.log 已验证默认版及携带 ISO 版的安装/卸载、实际安装脚本的自删除、重复卸载、未知参数拒绝、前缀重定向拒绝，以及项目镜像和相邻软件的保留。仅卸载隔离测试目录，未执行全局卸载。
