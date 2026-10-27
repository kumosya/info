# 源码目录

## 头文件目录 include

OS C++ 库头文件集中在`include/os-c++/os/`。除此之外，C++ 标准头文件在`include/stdc++/`，C 标准的头文件在`include/stdc/`，posix 标准的头文件在`include/posix/`。

用户态服务自身的声明放在`include/service/<服务名>/`，通用的服务实现声明放在`include/service/common/`。

内核声明集中在`include/kernel/`。

## 库文件目录 lib

- [graphic](lib/graphic.md): 为内核文字屏幕输出提供基础图形处理。
- [ld-x86-64](lib/ld-x86-64.md): x86-64 动态链接器，负责加载共享库和处理符号重定位。
- [libc](lib/libc/libc.md): C 标准库及 POSIX 接口实现。
  - [fallback](lib/libc/fallback.md): 尚未完整实现的 POSIX 和标准库函数的占位或兼容实现。
- [libc++](lib/libc++.md): C++ 标准库支持。
- [os](lib/os/libosc++.md): OS C++ 库支持
  - service

## 源文件目录 src

- [kernel](x86-64/overview.md): osinfo x86-64 内核本体。
  - [boot](x86-64/boot.md): 内核启动。
  - [cpu](x86-64/cpu/cpu.md): 处理器功能。
    - [interrupt](x86-64/cpu/interrupt.md): 中断与异常处理。
    - [smp](x86-64/cpu/smp.md): 多处理器处理。
  - [device](x86-64/device/device.md): 内核设备管理。
    - [service](x86-64/device/service.md): `device.kernel` 服务。
    - [pci](x86-64/device/pci.md): PCI 处理与 `pci.kernel` 服务。
    - [resource](x86-64/device/resource.md): 设备资源域管理。
    - [time](x86-64/device/time.md): 硬件时钟与定时器。
    - [dmabuffer](x86-64/device/dmabuffer.md): DMA 缓冲区。
    - [storage](x86-64/device/storage.md): 存储设备管理。
    - [ps2](x86-64/device/ps2.md): PS2 接口管理。
    - [acpi](x86-64/device/acpi.md): ACPI 电源管理。
  - [memory](x86-64/memory/memory.md): 内核内存管理。
    - [service](x86-64/memory/service.md): `memory.kernel` 服务。
    - [object](x86-64/memory/object.md): 内存对象及其生命周期管理。
    - [shared](x86-64/memory/shared.md): 共享内存管理。
    - [physical](x86-64/memory/physical.md): 物理内存与物理页分配。
    - [virtual](x86-64/memory/virtual.md): 虚拟地址空间、页表与内存映射。
  - [task](x86-64/task/task.md): 内核任务管理与执行控制。
    - [sys](x86-64/task/sys.md): `system.kernel` 的实现。
    - [service](x86-64/task/service.md): `task.kernel` 服务。
    - [sched](x86-64/task/sched.md): 任务调度。
    - [core](x86-64/task/core.md): 任务的崩溃转储。
    - [credentials](x86-64/task/credentials.md): 进程用户与组凭据管理。
    - [ipc](x86-64/task/ipc.md): 进程间通信。
    - [resource](x86-64/task/resource.md): 进程资源限额、线程信息查询与任务优先级管理。
    - [sync](x86-64/task/sync.md): 任务同步。
    - [syscall](x86-64/task/syscall.md): 系统调用。
  - [user](x86-64/user.md): 内核层的用户管理。

- [init](service/init.md): init 服务。

- [service](service/service.md): 服务框架与公共服务管理。

- [task](service/task/task.md): 任务管理服务。
  - [msgq](service/task/msgq.md): 消息队列通信服务。
  - [pipe](service/task/pipe.md): 管道通信服务。

- [mm](service/mm.md): 内存管理服务。

- [fs](service/fs/fs.md): 文件系统服务。
  - [exfat](service/fs/exfat.md)
  - [fat](service/fs/fat.md)
  - [ext3](service/fs/ext3.md)
  - [ext4](service/fs/ext4.md)
  - [iso](service/fs/iso.md)
  - [procfs](service/fs/procfs.md)
  - [sysfs](service/fs/sysfs.md)

- [device](service/device/device.md): 设备管理服务。
  - [pci](service/device/pci.md): PCI 设备管理。
  - [storage](service/device/storage.md): 存储设备管理。

- [console](service/console.md): 控制台服务。

- [tty](service/tty.md): 终端管理。

- [users](service/users.md): 用户与身份管理服务。

## 编译工具目录 tools

- host
- license
