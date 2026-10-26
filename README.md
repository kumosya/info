# OS Info

## 介绍

osinfo 运行在 x86-64 架构，采用混合内核架构：

- Ring0 提供页表、内存分配器、Task/Thread、调度、中断、Endpoint、句柄和受限硬件机制；所有服务进程及 pipe/msgq 服务状态运行于 Ring3。
- init 服务管理器和 MM 内存策略运行于 Ring 3。

## 配置

```sh
./configure
```

`./configure` 检查环境，构建 host 辅助程序，扫描并校验包，然后统一生成配置。

更新包或构建配置后要重新执行 `./configure`。make 不补建配置，失败配置不能继续构建。

关于配置参数，`CC`、`CPP`、`AR`、`HOST_CPP` 可选择编译器；仅编译时可使用 `--disable-image-tools`。

## 编译与安装

### 安装到本体镜像

```sh
make install
```

该命令会编译系统，并部署到已有 `build/osinfo.img` 的第一分区。

`install` 不会分区或格式化。osinfo.img 必须事先准备好，可通过 `make mkimg` 来生成 img 文件。

### 安装到指定目录

```sh
make install DESTDIR=/target
```

该方式将系统部署到指定目录。如果目标位于其他卷上，需要提前挂载好目标卷。

### 重新安装 GRUB

默认不安装 GRUB，因为 `mkimg` 已经完成引导安装。只有显式设置 `INSTALL_GRUB=1`，才会重新安装：

- **镜像模式：** 使用安装过程自行创建的 loop 设备。
- **目录模式：** 还必须通过 `GRUB_DISK=/dev/设备` 指定目标磁盘。

### 运行与镜像导出

| 命令 | 用途 |
| --- | --- |
| `make run` / `make debug` | 用 qemu 从本体硬盘镜像启动；修改系统后，需要先执行 `make install` 更新镜像 |
| `make image` | 独立生成 `build/export/osinfo.img` |
| `make iso` | 生成 `build/export/osinfo.iso` |
| `make export-images sdk` | 导出硬盘镜像、光盘镜像和公共 SDK |

`make image` 生成的是可直接引导的 MBR/ext3 硬盘镜像。生成过程不依赖本体的 `build/osinfo.img`，也不需要挂载或 root 权限。

`make export-images sdk` 可生成测试镜像 build/export/osinfo.img、build/export/osinfo.iso 与 SDK 目录 build/sdk/。

IMG/ISO 默认 development profile，提供空密码 root 账户。可加入参数 `EXPORT_ACCOUNT_PROFILE=system` 导出普通账户模板。

## 更新日志

- [v0.1-beta](docs/update/v0.1-beta.md)
