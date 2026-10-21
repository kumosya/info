# OS Info

## 介绍

osinfo 运行在 x86-64 架构，采用混合内核架构：

- Ring0 提供页表、内存分配器、Task/Thread、调度、中断、Endpoint、句柄和受限硬件机制；所有服务进程及 pipe/msgq 服务状态运行于 Ring3。
- init 服务管理器和 MM 内存策略运行于 Ring 3。

## 编译

```sh
./configure
make -j
make check sdk
```

`./configure` 检查环境，构建 host 辅助程序，扫描并校验包，然后统一生成构建、包清单和引导/init 配置。

更新包或构建配置后重新执行 `./configure`。`CC`、`CXX`、`AR`、`HOST_CXX` 可选择编译器；仅编译时可使用 `--disable-image-tools`。make 不补建配置，失败配置不能继续构建。下载链接需要能访问对应 Release，私有仓库可手动准备发行包。

## 安装

`make install` 先编译再安装到 build/osinfo.img，run/debug 也通过该目标更新镜像，默认 INSTALL_GRUB=0，也就是不安装 GRUB。

`make install DESTDIR=/已挂载目录` 改为安装到指定目录；需要重新安装引导时设置 INSTALL_GRUB=1，目录模式还需 GRUB_DISK=/dev/设备。

## 测试镜像与 SDK

`make export-images sdk` 可生成测试镜像 build/export/osinfo.img、build/export/osinfo.iso 与 SDK 目录 build/sdk/。

IMG/ISO 默认 development profile，提供空密码 root 账户。可加入参数 `EXPORT_ACCOUNT_PROFILE=system` 导出普通账户模板。

InfoIDE 的安装与发行包生成全部在 InfoIDE 项目中执行：将资源放入它自己的 resources/，执行 `make installer` 或 `make install`，发行包位于 InfoIDE 项目的 build/infoide-linux.tar.gz。详见 [InfoIDE](docs/app/infoide.md)。

## 在 QEMU 上运行

```
make mkimg
make run
```

## 更新日志

- [v0.1-beta](docs/update/v0.1-beta.md)
