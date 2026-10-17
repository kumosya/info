# 文件系统服务（src/fs）

fs 枚举 storage 的全部块设备，探测格式、卷标和 UUID，再按根卷的 /etc/fstab 挂载。main.cc 只负责服务启动与 IPC 分发，不指定磁盘路径或设备序号。

## 文件划分

| 文件 | 职责 |
|---|---|
| main.cc | 服务入口、请求分发 |
| devices.h/cc | 格式探测、设备标识、块节点及别名 |
| fstab.h/cc | 六列解析、八进制转义、路径规范化与选项 |
| mount.cc | 根卷发现、挂载表、挂载/卸载、最长路径匹配 |
| file.cc、dir.cc | 描述符、文件操作、目录与虚拟 /dev |
| drivers.cc | 为每个挂载创建独立文件系统实例 |

ext4 与 ext3 对齐：super.cc 为格式探测，superblock.cc 为超级块/组描述符，block.cc 为块 I/O，balloc.cc 为块分配，ialloc.cc 为 inode 分配，inode.cc 为 inode I/O 和数据块/extent 映射，dir.cc 为目录与路径，file.cc 为文件操作，sync.cc 为同步/只读状态。共享编解码位于 internal.h。

## 设备标识

VirtIO 整盘为 /dev/vda、/dev/vdb，分区为 /dev/vda1 等。主号是本系统分配的 252，次号为“磁盘序号 × 256 + 分区号”，整盘分区号为 0。设备号只保证当前启动期间唯一。

探测到 ISO9660 后生成 /dev/sr0、/dev/sr1，/dev/cdrom 绑定第一份 ISO。不会把第二块盘或只读盘直接当作光盘；多光盘特指使用稳定标识。

| 来源 | 示例 |
|---|---|
| 卷 UUID | UUID=... 或 /dev/disk/by-uuid/... |
| 卷标 | LABEL=OSINFO_EXT4 或 /dev/disk/by-label/OSINFO_EXT4 |
| 分区 UUID | PARTUUID=... 或 /dev/disk/by-partuuid/... |
| PCI 位置 | /dev/disk/by-path/pci-0000:00:04.0-part2 |
| 当前节点/别名 | /dev/vda2、/dev/cdrom |

GPT PARTUUID 来自分区唯一 GUID，MBR 为磁盘签名-分区号。重复 UUID/PARTUUID/LABEL 报歧义，不选择其中第一块；有歧义的稳定别名不出现在目录列表中。发现日志输出卷标、UUID、PARTUUID。

别名由 fs 解析，不依赖磁盘中的占位文件。同一设备的别名共享 st_rdev；普通文件的 st_dev 是所在卷设备号，sys/sysmacros.h 提供 major/minor/makedev。块节点支持只读打开/读取；裸写和格式化未开放。虚拟别名目前不是完整 POSIX 符号链接。

## 根卷和 fstab

读取候选卷的 /etc/fstab，仅当根挂载行能唯一解析回该卷且类型匹配时，才选为根卷。多个根候选或歧义标识导致失败，不按枚举顺序猜测。

默认配置使用 LABEL=OSINFO_ROOT、LABEL=OSINFO_EXT4、LABEL=FAT32，挂载点分别是 /、/mnt/ext4、/mnt/fat32；/dev/cdrom 以 auto 类型、ro,nofail 挂在 /mnt/cdrom。

- 支持六列，兼容省略 dump/pass 的四列；支持 \040、\011 等转义和行尾注释。
- auto 类型使用探测结果；支持多个同类型卷，各有独立实例。
- 支持 defaults、ro/rw、auto/noauto、nofail/fail；未知选项报错。
- noauto 只登记，等待手工挂载；nofail 允许设备缺席或挂载失败。
- dump/pass 会校验并保留，目前没有启动阶段的 dump/fsck 调度。
- 根卷必须自动、必需挂载，目标须为现存目录；/dev 保留。
- 同一设备只允许挂载一次；打开文件或子挂载会阻止卸载，卸载前同步。
- 当前最多 32 个块节点、16 个挂载槽位，仅启动时枚举，无热插拔管理。
- ext4 原有写入限制不变：64 位组描述符、metadata_csum 等不支持写入的布局仍只读，没有新增日志重放。

## ISO9660 与验证

ls 支持 -l、-a、-la 和 --color=auto/always/never；普通列表以两个空格分隔并按行宽换行，-l 显示类型、rwx/特殊权限位、链接数、数字 UID/GID、大小或主次设备号、UTC 修改时间。没有时间元数据的条目显示 -。默认只给终端输出 ANSI 颜色，图形 PTY 也由 isatty 识别；管道或重定向默认不带颜色。LS_COLORS 可覆盖 di、fi、ex、ln、pi、so、bd、cd 类型颜色，例如 export LS_COLORS='di=01;35:fi=0:ex=01;32'。

目录读取区分记录、正常结束和介质错误。末尾零填充属于正常结束，块间填充跳到下一块，批量枚举能返回已有条目；真实 I/O 错误和越界记录仍报错。查找与枚举一致处理 ;1 和末尾点。

在 WSL Debian 中可用 `make clean`、`make all`、`make check`、`make mkimg`、`make cpfiles run` 完成构建与启动检查。
