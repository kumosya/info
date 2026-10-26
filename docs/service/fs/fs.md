# 文件系统服务（src/fs）

## 系统元数据配置（v0.1.1）

根卷确定后、服务发布前，FS 通过 `FileSystem::lookup/read` 直接加载 `/lib/version.1` 与 `/etc/hostname`，不向自己发 RPC。`/sys/kernel/osrelease` 和 `/sys/kernel/hostname` 使用这一启动快照；sysinfo 读取相同节点。修改文件后重启生效。系统类型仍为 osinfo；uptime、CPU 和内存来自实时机制查询，配置节点不依赖统计服务成功。

配置为单行、1–63 字节，允许结尾 LF/CRLF；版本允许字母、数字、点、连字符、下划线和加号，hostname 允许字母、数字、点与连字符。缺失、空、超长、非法内容或读取失败会记录明确错误并使 FS 初始化失败，不回退到写死的版本或 hostname。sysfs 读取保留偏移与短读语义。

fs 枚举 storage 的全部块设备，探测格式、卷标和 UUID，再按根卷的 /etc/fstab 挂载。main.cc 只负责服务启动与 IPC 分发，不指定磁盘路径或设备序号。

InfoIDE 的可选光盘模式把完整根卷放入 ISO 的 /boot/rootfs.ext3，经 GRUB 载入内存后由 storage 发布 /dev/ram0。FS 仍按卷内 LABEL=OSINFO_ROOT 的 fstab 确定根卷，运行时可写，关机不保存。这里不依赖 ATAPI 驱动，也不会将 ram0 冒充 /dev/cdrom；仅该可选模式只连接 ISO 光驱。OS 本体和 InfoIDE 项目默认使用 VirtIO 硬盘。

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

## 虚拟目录与任务快照

sysfs 与 procfs 的目录允许 O_RDONLY/O_DIRECTORY 打开；大小零不表示没有条目。FS 统一折叠路径中的重复斜线，不依赖 ls 特判。目录协议返回 inode 与 nextOffset，libc 对所有文件系统统一采用返回游标；proc 根用单调 PID 下界应对创建和退出竞争。普通进程文件在 open 时保存内容快照，self 使用真实请求 PID。格式与权限详见 [procfs/sysfs](../procfs.md)。

## 监护、并发和诊断

MM 文件映射使用稳定租约，不把引用寿命绑定到旧 MM PID。ClaimMapping 返回租约 ID 与描述符，原 FD 关闭或文件 rename 后仍由同一打开文件对象提供页。MM 在检查点提交后发送 ReleaseMapping；新实例通过 ListMappings 回收未提交请求留下的孤儿租约。CancelMapping 只按发行者和稳定 token 取消尚未兑换的委托，避免按复用的描述符数字误关闭其他文件。以上不等于 FS 自身重启已支持完整状态恢复。

ext3/ext4 按打开文件对象保留 inode。unlink 最后一个名字、rmdir 空目录或 rename 覆盖旧目标时，仍有打开引用或 MM 租约的对象只把链接数降到零；数据块和 inode 留到最后一个对象关闭之后回收。同名重建使用独立 inode，旧 FD 与映射不会读到替代文件。fstat 直接查询 inode，正确返回零链接数，不依赖旧路径是否还存在。dup/fork/映射租约共享 OpenFileDescription 引用，不重复计算底层保留。

引用归零的描述对象先进入待回收状态，按“文件系统锁→描述符表锁”回收，避免关闭路径反向取锁。主 worker 每秒回收退出任务的描述符。底层回收报告失败时隔离该 inode 与描述对象，保留槽位且禁止 inode 再分配；没有完整磁盘事务时不能自动重做可能已释放部分块的操作，以免二次释放。此类介质/元数据错误仍需文件系统修复，不宣称具备完整写入事务恢复。当前保留表属于 FS 实例内存，没有实现跨 FS 重启或整机崩溃的持久 orphan 恢复；FAT32 的 unlink 寿命保证尚未迁移。`tests/fs-inode-host.sh` 对 ext3/ext4 执行内容、同名重建、rename 覆盖、打开目录和 128 轮回收，并用 e2fsck 检查正常路径磁盘；MM 故障测试覆盖 unlink 后的未触页文件映射。

init 创建并监护 fs；角色 Fs，没有任意设备 I/O 能力。块访问通过 storage IPC；挂载和格式化验证实际请求凭据。打开文件描述符归 PID，OpenFileDescription 可由 fork/dup 共享，引用归零才释放；每挂载独立锁，描述符表锁与描述符偏移锁分开。
