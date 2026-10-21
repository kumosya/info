# 配置

`./configure` 从 host 工具内置默认值创建配置。

| 配置 | 职责 |
| --- | --- |
| services.conf | 服务源码、Ring、固定身份、启动依赖、就绪与链接库 |
| `/etc/services`、`/etc/sessions` | 运行时服务及 GUI/文本会话入口，默认模板在 script/etc；不生成入口 C++ 数组 |
| layout.conf | 目录、权限、系统文件、boot 运行库、SDK 与旧布局迁移 |
| checks.conf | 审计策略与本仓库检查范围 |

`./configure` 使用 `tools/inpkg`、`tools/infoide` 二进制，并将 host 辅助程序编译为 `tools/osinfo-host`。环境检查、ZIP 展开、包扫描和配置生成均在 configure 完成。make 使用现有配置，缺失或过期时要求重新 configure。

生成 `build/config.mk`、`packages.conf`、`package-inputs.mk`、`packages.mk` 和 `build/generated/` 下的 services.mk、boot-images.inc、boot-abi.inc、startup.inc、modules.conf、grub.cfg。startup.inc 仅包含建立根文件系统所需的内置服务及 owner。更改输出目录可用 configure 的 `--build-dir`、`--object-dir`、`--package-dir`；随后 make 使用 `CONFIGURE_BUILD=配置输出目录`。

services.conf 的 17 列为：名称、Ring、相对 src 的目录、ProgramId 符号、编号、ServiceRole 符号、编号、能力、源码来源、就绪端点、依赖列表、顺序、进程名、链接库、遍历方式、加载来源、owner。源码来源保留 local/disk/external/resident 分类，加载来源单独为 boot/file/resident，根据源码是否在 OS 仓库推断是否内置。旧 15 列配置需补齐最后两列；新配置目录可由 configure 独立创建。owner 为 init/task/device/kernel；文件服务当前归 init。依赖使用逗号分隔且先启动；flat 只选本层源码，recursive 再纳入一层子目录。生成代码以 static_assert 校验固定 ABI。system.kernel 位于 `src/kernel/task/sys/`。

layout 的 copy/sdk-file 来源支持 `{build}`、`{packages}`、`{objects}`。系统版本和 hostname/fstab 使用各自文件，layout 登记部署位置。

包集由扫描生成，不保留手写 packages.conf：manifest 行登记 OS 运行库，archive 行引用 `packages/` 中的 `.inpk`。只使用复数目录。ZIP 作为复合包容器直接展开到包目录，再逐包校验。osinfo-base-tools.zip 内为 coreutils、sysinfo、mount、mkfs、systemctl 五个独立包，各子项目在自己的 build/packages/ 中生成归档。引导服务和会话入口必须由发现的包提供，缺少时 configure 失败。各应用的映射由包内 manifest 或独立项目 `infoide-project.json` 维护。

`make install` 编译并部署到已有 build/osinfo.img 的第一分区，默认不安装 GRUB，因为 mkimg 已提前完成引导安装；原有复制入口已移除。镜像安装自动申请权限、锁定镜像、挂载根分区，并在退出时卸载和释放 loop。`make install DESTDIR=/target` 仍可部署到指定目录，目标卷应事先挂载好。只有设置 INSTALL_GRUB=1 才重新安装引导：镜像模式使用自己的 loop，目录模式需另传 GRUB_DISK=/dev/设备。install 不分区、不格式化。

OS 本体的 `make run/debug` 使用硬盘启动，依赖 install 更新本体镜像。`make image` 单独生成 build/export/osinfo.img；该镜像为可直接引导的 MBR/ext3 硬盘，不依赖本体 build/osinfo.img，也不需要挂载或 root。`make iso` 输出 build/export/osinfo.iso；`make export-images sdk` 导出两种镜像和公共 SDK。OS 不承担 InfoIDE 的安装或发行包生成。

InfoIDE 项目从自己的 resources/sdk/、resources/osinfo.img 读取资源，执行 `make installer` 生成自己的 build/infoide-linux.tar.gz，执行 `make install` 安装工具；也支持 SDK、DISK、ISO 指定输入。安装位置只有 /usr/local/bin/infoide 与 /usr/local/lib/osinfo/。InfoIDE 的安装、CLI 和项目调试测试均在它自己的 tests/ 中。

光盘是可选的项目测试路径。在 InfoIDE 项目执行 `make installer WITH_CDROM=1` 才附带 resources/osinfo.iso；InfoIDE 通过 --boot cdrom 安装项目并生成 ISO，再启动光驱。该模式使用 /boot/rootfs.ext3 内存模块，运行时写入关机丢弃，不改变 OS 本体默认行为。
