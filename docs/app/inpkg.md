# inpk 软件包与 tar

应用程序直接编译到仓库的 `packages/包名/`，例如 `packages/sysinfo/sysinfo`、`packages/coreutils/ls`。`make all` 同时生成 `packages/包名.inpk`；`.inpk` 的内容是标准 tar.gz，可用 Linux 的 `tar -tzf packages/sysinfo.inpk` 检查。

源码中的 `package.conf` 同时用于构建和安装，包内和系统中的副本都原样复制，不改写字段：

```ini
name=sysinfo
version=0.2
bin=/usr/bin/sysinfo
install=sysinfo -> /usr/bin/sysinfo
```

`bin` 是程序在系统里的绝对路径。每条 `install` 左侧是压缩包内的相对文件名，右侧是解包后安装的绝对路径；例如 SDK 头文件可以写为 `install=include/os-c++/os/abi.h -> /usr/include/os-c++/os/abi.h`。

Linux 开发环境下安装到已经挂载的分区：

```sh
python3 script/packages.py --install packages/sysinfo.inpk --install-root /mnt/osinfo
```

`make cpfiles` 使用同一个安装器，从 `.inpk` 解包安装程序和库，并将包配置原样保存到 `/etc/inpkg/system/包名.conf`。压缩包留在开发机，不复制到系统的 `/usr/share`。

系统内也可以运行 `inpkg install /path/sysinfo.inpk --system -y`，按照配置的绝对路径安装；普通用户使用 `--local` 时，安装到自身 `app/` 中，并保留原始包配置。`inpkg remove sysinfo --system -y` 和 `--local` 分别卸载对应范围的文件。sudo 安装时仍提供系统与 root 私有目录的选择。

`tar` 支持普通文件、目录与 ustar 格式，以及 gzip 的存储、固定和动态 Huffman 数据块：

```sh
tar -czf backup.tar.gz folder
tar -tzf backup.tar.gz
tar -xzf backup.tar.gz -C destination
tar -cf backup.tar folder
```

`-v` 显示文件名，`-f -` 使用标准输入或输出。解包拒绝绝对成员路径、`..`、链接和设备成员；gzip 解压检查 CRC 与原始长度。当前未实现 PAX、GNU 长文件名扩展和链接归档。
