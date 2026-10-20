> 后续已修正：OS 本体恢复硬盘启动；InfoIDE 默认使用项目内硬盘及独立调试配置，光盘仅显式可选。安装位置固定为 /usr/local/bin/infoide 与 /usr/local/lib/osinfo/。当前实现见 [项目调试修正记录](infoide-project-debug-progress.md)。以下保留历史记录。

# Linux InfoIDE 与纯光盘构建补充

本轮以最新要求为准，覆盖此前“单体 base-tools 包、单数 package/、Python 格式化例外、光盘加硬盘调试”的安排。架构文件不修改。

## 实现要求

1. 应用、服务、运行库与根卷全部放进 ISO；QEMU 只连接光盘，严格从光盘启动。
2. ISO 内 /boot/rootfs.ext3 由 GRUB 加载为 rootfs 模块。Ring0 仅提供保留模块块访问机制，Ring3 storage 发布 ram0；FS 继续从卷内 fstab 选择根卷。运行时修改保存在内存，关机丢弃。
3. InfoIDE 的 install/run/debug 先编译打包，再使用内置 C++ inpkg 核心执行 upgrade --install-missing，将应用安装进新 ISO，读回校验后发布。安装和运行无需相邻源码或外部 inpkg 程序。
4. Linux 安装包只有 InfoIDE 本体、SDK、测试 ISO、启动 sh。安装器默认 ~/.local，支持 --prefix 与隔离 --destdir；安装后自动定位资源，自定义前缀的项目副本可使用 INFOIDE_HOME。
5. OS make install DESTDIR=目录 先构建再安装，默认不装 GRUB；显式 INSTALL_GRUB=1 GRUB_DISK=设备 才执行。验证不使用真实磁盘。
6. 仅保留 packages/，ZIP 直接展开后扫描。base-tools 拆成 coreutils、sysinfo、mount、mkfs 四个子项目，各自 build/packages/ 出 inpk，顶层 make 再汇总 ZIP。
7. config/ 初始可不存在，configure 从内置数据生成缺失文件，保留用户已有配置；目录加入 .gitignore。检查和冒烟脚本全部放 tests/，无冒烟配置文件。
8. 全部项目不保留 Python 文件，格式化入口为 clang-format.sh；guiw、inpkg、base-tools 根目录复制并忽略 InfoIDE 二进制。

## 已通过的验证

- OS 编译、SDK 导出与 make check：build/cd-build.log。
- QEMU 仅挂 ISO，ram0 根卷、GUI 与 tty1–tty6 启动：build/cd-only-boot-fast.log。
- config/ 不存在时生成全部配置，二次 configure 保留编辑：build/config-bootstrap-test.log。
- 离线包扫描、ZIP 展开、模拟 Release 下载与错误隔离：build/packages-configure-test.log。
- InfoIDE 编译参数、模板、打包、失败处理回归：build/infoide-cli-regression.log。
- inpkg 升级、修改检测、SHA256 与中断事务恢复：build/inpkg-regression.log。
- 隔离安装、自动 SDK/ISO 发现、包内容升级、光盘内真实应用执行，以及 run/debug/启动脚本仅光驱参数：build/infoide-optical-test.log。
- OS make install 临时目录安装成功，默认 GRUB=0：build/os-install-test.log。
- 内核分配器/VMA、IPC 与系统配置检查：build/memory-regression.log、ipc-regression.log、system-config-regression.log。
- 纯光盘内分区发现、ext4 挂载卸载与四个独立基础工具包运行：build/cd-base-tools-runtime.log。
- 纯光盘匿名映射、保护、部分 unmap、COW、共享/文件映射、信号、服务 RPC、exec 和 Ring3 pipe/msgq：build/cd-memory-runtime.log。
- 纯光盘系统内 inpkg 安装、升级、缺失包处理、本地范围与卸载：build/cd-inpkg-runtime.log。
- shell 语法、安装包内容及本体摘要一致性、无 Python 扫描通过；git diff --check 通过，docs/architecture.md 无差异。

此处为历史光盘方案，现已由 [项目硬盘调试与安装方案](infoide-project-debug-progress.md) 替代：发行包由 InfoIDE 项目生成至其 build/infoide-linux.tar.gz；固定安装 /usr/local/bin/infoide 和 /usr/local/lib/osinfo/，默认硬盘、显式选择光盘。启动脚本位于 /usr/local/lib/osinfo/run-qemu.sh。构建器、ZIP 容器、ISO 更新和内核保留块访问各自独立；包格式、升级策略复用 inpkg 核心，参数继续由项目 JSON 决定。
