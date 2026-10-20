# InfoIDE Linux 项目工具

InfoIDE 是独立的 Linux C++ 命令行工具。创建项目后，在项目目录内编译、打包；run/debug 将项目安装进专属 OSInfo 镜像，再由 QEMU 运行。程序不会把 OSInfo ELF 当作 Linux 本地程序执行，也不调用相邻 OS 源码目录。

OS 仓库只提供系统镜像和公共 SDK：执行 `./configure && make -j export-images sdk`，输出 build/export/osinfo.img、build/export/osinfo.iso 和 build/sdk/；也可用 image 或 iso 单独导出一种镜像。OS 不构建或安装 InfoIDE。

在 InfoIDE 项目中准备自己的资源目录，然后生成安装包：

```text
resources/
  sdk/            # 系统导出的 SDK，含 sdk.json
  osinfo.img      # 系统导出的硬盘镜像
  osinfo.iso      # 可选光盘镜像
```

```sh
make installer                 # 默认仅携带硬盘模板
make check check-install       # CLI 与安装器验证
sudo make install              # 安装 InfoIDE；所有操作均在 InfoIDE 项目执行
```

resources/ 已加入忽略规则。也可用 `make installer SDK=/path/to/sdk DISK=/path/to/osinfo.img` 显式指定输入。发行包为 InfoIDE 项目自己的 build/infoide-linux.tar.gz；复制到其他 Linux 主机后，解压并运行 `sudo sh infoide-linux/install.sh`。`make install DESTDIR=/暂存目录` 支持隔离安装验证。安装位置只有两处：

```text
/usr/local/bin/infoide
/usr/local/lib/osinfo/
  sdk/
  osinfo.img
  run-qemu.sh
  uninstall.sh
  osinfo.iso       # 可选
```

安装器支持 `--destdir DIR` 暂存打包，不改默认安装路径。宿主需有 gcc/g++、debugfs、flock、QEMU；选用光盘时另需 xorriso 和 grub-mkrescue。开发、更新项目镜像和运行 QEMU 均无需 root。包核心内置于 InfoIDE，无需另带 inpkg 程序。

卸载在 InfoIDE 项目执行 `sudo make uninstall`，无需重新编译或准备 SDK/镜像。也可使用已安装的 `sudo sh /usr/local/lib/osinfo/uninstall.sh`，或解压发行包后运行 `sudo sh infoide-linux/uninstall.sh`。暂存安装对应 `make uninstall DESTDIR=/暂存目录` 或卸载脚本的 `--destdir DIR`。卸载仅清理 /usr/local/bin/infoide 与专用的 /usr/local/lib/osinfo/，保留各项目源码、配置和 IMG/ISO；重复卸载可安全执行。

```sh
infoide create hello
cd hello
infoide build
infoide pkg
infoide install     # 编译、打包并安装到当前项目的 osinfo.img
infoide run         # 完成上述步骤，再从项目硬盘启动 QEMU
infoide debug       # 同上，启用 GDB，默认等待 127.0.0.1:1234
```

新项目包含 src/main.cc、infoide-project.json、infoide-debug.json 和 .gitignore。首次 install/run/debug 从已安装模板复制出项目根目录的 osinfo.img，后续使用 upgrade --install-missing 更新包，保留镜像内其他数据；不会每次覆盖模板重建镜像。镜像、锁文件和 build/ 均忽略。旧项目首次使用调试命令时补建缺失的调试配置，已有配置不覆盖。重置镜像需要用户显式操作。

硬盘更新在临时副本中完成：提取 MBR 第一分区、离线安装包、读回校验，再原子发布整盘。保留原有引导区、分区表和其他分区。当前支持第一分区为 OSInfo Linux 根卷的 MBR 镜像。InfoIDE 项目锁和 QEMU 原生文件锁阻止并发写入；编译或安装失败不发布镜像。

## 项目配置

infoide-project.json 决定源码、编译器、头文件、库、目标与包映射。cppflags 是公共参数，cflags/cxxflags/asflags 分别作用于 C/C++/.S，ldflags/ldlibs 控制链接。字段为字符串数组，每项一个参数；先应用全局参数，再追加目标参数。目标 libraries 覆盖全局选择。生成模板明确写入初始参数。

SDK 优先级为 --sdk、项目 sdk、OSINFO_SDK、安装资源；编译器可用 --cxx/--cc 覆盖。相对配置路径以项目根目录为基准。资源默认读取 /usr/local/lib/osinfo，也支持 INFOIDE_HOME 显式指定资源目录；暂存安装通过本体所在前缀定位 lib/osinfo。

infoide-debug.json 集中保存测试设置：

| 字段 | 默认值 | 用途 |
| --- | --- | --- |
| boot | disk | disk 或 cdrom |
| disk_template / iso_template | 空 | 空表示使用安装资源；显式路径相对项目根目录 |
| disk_image / iso_image | osinfo.img / osinfo.iso | 项目根目录的镜像，拒绝项目外路径 |
| qemu | qemu-system-x86_64 | QEMU 程序 |
| memory_mb / cpus | 512 / 4 | 虚拟机资源 |
| display / serial | gtk / stdio | 显示与串口 |
| gdb_port / wait_for_gdb | 1234 / true | debug 的本地 GDB 端口及启动暂停 |

--debug-config 选择另一份调试配置，--boot、--disk、--iso、--output、--qemu、--headless 覆盖对应设置。输出仍须位于项目根目录；相对镜像路径以项目为基准。run 正常启动 QEMU；debug 开启 GDB，是否暂停由 wait_for_gdb 决定。

## 可选光盘

在 InfoIDE 项目中显式使用 `make installer WITH_CDROM=1` 附带 resources/osinfo.iso，也可通过 ISO=/path/to/osinfo.iso 指定。项目使用 `infoide run --boot cdrom` 或在调试配置中设置 boot=cdrom；InfoIDE 将应用装入新 ISO，再从光驱启动 QEMU。光盘模式不挂硬盘，运行时写入关机丢弃。--iso 可选择基础光盘，输出为项目根目录的 osinfo.iso，禁止覆盖基础光盘。保留此路径不会影响 OS 本体 make run/debug 的硬盘启动。

配套 run-qemu.sh 可在项目目录直接启动已有镜像，支持 --image、--boot、--headless、--gdb、--gdb-port；它不代替 InfoIDE 的构建与安装步骤，也不直接运行安装目录里的模板。

`infoide bundle --output FILE.zip 包1.inpk 包2.inpk` 校验并汇总复合包。base-tools 的五个子项目（含独立 systemctl）各自构建 .inpk，再由顶层 Makefile 汇总 ZIP。OS 构建只消费 packages/ 内的发行包。

## 开发验证

InfoIDE 自身的 tests/ 保存 CLI、安装器、项目硬盘和可选光盘回归。`make check check-install` 使用 resources/ 验证编译参数、包内容、安装布局和启动脚本。`make check-install WITH_CDROM=1` 额外验证携带 ISO 的发行包。

`make check-project OSINFO_TEST_HOST=/path/to/osinfo-host` 和 `make check-optical WITH_CDROM=1 OSINFO_TEST_HOST=/path/to/osinfo-host` 使用显式提供的开发测试驱动向真实 QEMU 输入命令；发行包和安装流程无需该驱动。测试临时目录通过 TMPDIR 指定，需位于支持 Unix socket 的 Linux 文件系统；WSL 下不要选择 /mnt/d 等 Windows 挂载目录。
