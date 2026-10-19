# OS Info

## 介绍

osinfo 运行在 x86-64 架构，采用混合内核架构：

- Ring0 提供页表、内存分配器、Task/Thread、调度、中断、Endpoint、句柄和受限硬件机制；所有服务进程及 pipe/msgq 服务状态运行于 Ring3。
- init 服务管理器和 MM 内存策略运行于 Ring 3。
- 用户态程序通过 19 个系统调用（`SYSCALL`/`SYSRET`）进入内核。

## 编译

guiw、inpkg 源码分别位于相邻 `D:/osinfo/guiw`、`D:/osinfo/inpkg`。首次构建先导出 SDK，再在独立项目编译并导入软件包：

```sh
make sdk
make -C ../guiw
make -C ../inpkg
python3 script/import-package.py ../guiw/build/packages/guiw.inpk
python3 script/import-package.py ../inpkg/build/packages/inpkg.inpk
```

```
make all
```

## 编译、建盘并运行

```
make all mkimg cpfiles run
```

`mkimg` 只创建含 ext3 根分区、ext4 分区和 FAT32 分区的磁盘镜像；
首次切换到可写 ext4 格式时需运行一次 `make mkimg`（会重建镜像并清除旧数据）；
之后修改系统文件时只需运行 `make all cpfiles run`，无需重新格式化镜像。
启动时，FS 服务按 `/etc/fstab` 将 ext4 与 FAT32 数据分区挂载到
`/mnt/ext4`、`/mnt/fat32`。FAT32 和使用受支持特性集创建的 ext4 镜像支持
文件写入、创建和截断；带日志、元数据/GDT 校验和或 64 位块寻址的 ext4 卷保持只读。



内核架构、服务启动依赖、RPC 状态与 SMP 回收时序见[混合内核架构](docs/x86-64/architecture.md)。

用户空间独立构建：make -C user。内存管理头文件仅使用 mm.h、page.h，机制与策略说明见[内存管理](docs/x86-64/memory/memory.md)。

应用使用独立的 [ELF 动态链接器](docs/x86-64/dynamic-linker.md)，支持 PT_INTERP 与 DT_NEEDED。用户数据与配置路径见 [XDG 目录接口](docs/lib/os/directories.md)。Windows/WSL 上的 [Info IDE](docs/app/infoide.md) 位于 `D:/osinfo/infoide`，负责编辑、交叉编译和 `.inpk` 打包。
