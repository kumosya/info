# inpk 软件包与 tar

`.inpk` 是带有 `package.conf` 的标准 ustar tar.gz。OS 构建自身运行库；edit、guiw、inpkg、osinfo-base-tools 等应用和外部服务以发行包形式放入 `packages/`，由 configure 统一扫描。

```sh
./configure
make -j
tools/inpkg check packages/coreutils.inpk
```

sysinfo、mount/umount、mkfs、coreutils 由 osinfo-base-tools 发行包提供。ZIP 复合包直接展开到包目录，再逐个扫描 `.inpk`，不把 ZIP 当作单个 inpk 安装。具体映射以各包 manifest 为准。

配置示例：

```ini
name=hello
version=0.1.0
bin=/usr/bin/hello
install=hello -> /usr/bin/hello
```

`bin` 是程序的系统绝对路径；`install` 左侧为归档相对文件名，右侧为绝对安装位置。数据、共享库或头文件用 file/include 声明。配置在归档和数据库中原样保存，路径不得越界或重复。

ld-osinfo 与 `/lib/version.1` 由系统基础镜像直接部署，不属于普通软件包；开发机包检查/安装器与目标系统 inpkg 均拒绝包映射占用这两个路径。系统组件由对应 OS 构建更新。

Linux 二进制为 `tools/inpkg`，其独立项目与 OSInfo 版本共享包核心。Linux 操作已安装包时必须指定明确的 staging 或挂载根目录：

```sh
tools/inpkg install packages/coreutils.inpk --sysroot /path/to/staging-root
tools/inpkg upgrade packages/coreutils.inpk --sysroot /path/to/staging-root --install-missing -y
```

`install` 只接受未安装包，`upgrade` 要求包已安装；`upgrade --install-missing` 仅在确实未安装时首次安装，不掩盖损坏、冲突或恢复失败。同版本允许显式升级。Linux 拒绝宿主根目录及符号链接逃逸；操作挂载卷前应停止使用该卷的 OSInfo 实例，两端锁不协调跨系统并发。

`make install` 使用 configure 生成的 `build/packages.conf`，以 upgrade --install-missing 同步程序和运行库，将原始配置写入 `/var/lib/inpkg/包名.conf`，并以同名 `.state` 保存路径、SHA-256 与模式。镜像构建不进入相邻源码项目，缺包在 configure 阶段报错。

更新与卸载拒绝覆盖其他包、无归属现存文件及本地修改。旧数据库没有 `.state` 时，必须先人工核对，再显式运行 `inpkg adopt 包名 --sysroot 根目录 -y` 建立当前文件基线。中断后用 `inpkg recover --sysroot 根目录` 恢复；损坏的事务日志或备份会阻止自动恢复。事务按包执行，整批预检不代表整批原子提交，也未承诺断电持久性。

系统内使用 `inpkg install /path/hello.inpk --system -y` 按绝对路径安装；普通用户使用 `--local` 安装到 `~/.local/`，数据库为 `~/.local/state/inpkg`。`inpkg remove hello --system -y` 和 `--local` 对应卸载两个范围。sudo 安装保留系统与 root 私有目录的范围选择。

`tar` 命令由 osinfo-base-tools 提供，支持普通文件、目录、ustar，以及 gzip 的存储、固定和动态 Huffman 数据块：

```sh
tar -czf backup.tar.gz folder
tar -tzf backup.tar.gz
tar -xzf backup.tar.gz -C destination
tar -cf backup.tar folder
```

`-v` 显示文件名，`-f -` 使用标准输入或输出。解包拒绝绝对成员路径、`..`、链接和设备成员；gzip 解压检查 CRC 与原始长度。当前不实现 PAX、GNU 长文件名扩展和链接归档。
