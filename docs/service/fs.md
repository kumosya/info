# fs 服务（src/fs/）

fs 服务（注册名 `"fs"`）是文件系统服务器：挂载根文件系统（ext3 on `/dev/hda1`）与可选光盘（iso9660 on `/mnt/cdrom`），提供全部文件操作，并合成 `/dev` 虚拟目录。由内核内置镜像加载。

## 1. 服务结构

- `descriptorCapacity = 64`：描述符表与打开文件描述表各 64 项。
- 4 个线程处理请求（主线程 + 3 个工作线程，栈基址 `0x50000000`）；全部状态由 `std::mutex` 保护。

```cpp
enum class DescriptorBackend : uint8_t { None, Ext3, Iso9660, Zero };
struct OpenFileDescription {   // 打开文件描述，引用计数
    uint32_t inode; uint64_t offset, size;
    uint32_t isoExtent; uint32_t referenceCount;
    DescriptorBackend backend;
};
struct FileDescriptor { uint64_t ownerPid; OpenFileDescription* description; };
```

## 2. 块设备客户端（BlockDeviceClient）

实现 `fs::SectorDevice` 接口（`src/fs/block.h`）：

```cpp
class SectorDevice {
    virtual bool readSector(uint64_t sector, void* buffer) = 0;
    virtual bool writeSector(uint64_t sector, const void* buffer) = 0;
    virtual bool readSectors(uint64_t firstSector, uint32_t count, void* buffer);
    virtual bool writeSectors(uint64_t firstSector, uint32_t count, const void* buffer);
    virtual bool flush() = 0;
    virtual uint64_t sectorCount() const = 0;
    virtual bool readOnly() const = 0;
};
```

- 初始化时向 storage 发 `GetInfo`，要求扇区大小恰为 512；
- 批量异步传输：`maximumBatch = 8` 扇区/批，经 `ReadAsync/WriteAsync` + 完成端点收发，按 `0x53544f52414745`（"STORAGE"）通知收集。

## 3. 挂载流程

1. 设备 0 初始化 + 探测 + `ext3::Volume::mount`；
2. 读取 `/etc/fstab`（≤1023 字节），文本匹配：
   - 含 `/dev/hda1` + `ext3` → `rootMounted_`；
   - 含 `/dev/cdrom` + `/mnt/cdrom` + `iso9660` 且设备 1 探测成功并挂载 → `cdromMounted_`。

## 4. IPC 请求（`os::FsRequest`）

路径内联在 `data` 起始 256 字节。

| 请求 | 值 | 语义 |
|---|---|---|
| Open | 1 | 路径→描述符。`/dev/zero` → Zero 后端；`/mnt/cdrom/*` → Iso9660（仅普通文件）；其余 → ext3（仅 `modeRegular`） |
| Close | 2 | `data[0]`=描述符；引用计数归零时释放描述 |
| Read | 3 | `data[0]`=fd、`data[1]`=字节数（≤4112）；回复计数 + 数据；推进偏移。Zero 后端转发 device 服务 ReadNode |
| Write | 4 | 仅 Ext3；`data[1]`=大小（≤4104）、`data[2..]`=数据；推进偏移并扩展 size |
| Seek | 5 | `data[1]`=int64 偏移、`data[2]`=whence（0=SET/1=CUR/2=END）；回复新位置 |
| Stat | 6 | 回复 11 字：状态、类型、size、mode&07777、inode、links、uid、gid、atime、mtime、ctime。`/dev`、`/dev/pts` 报目录 0755；`/dev/*` 报文件 0666；cdrom 目录 0555/文件 0444 |
| Create | 7 | 虚拟路径（/dev、/mnt/cdrom 前缀）拒绝 |
| MakeDirectory | 8 | 同上 |
| Remove | 9 | 同上 |
| ReadDirectory | 10 | `data[7]`=索引；回复状态、类型、48 字节名字。`/dev` 转发 device 服务 ListNode |
| Truncate | 11 | 仅普通文件 |
| Mount | 12 | 负载：设备名(64B)+挂载点(64B)+类型；仅接受 `/dev/hda1→/ ext3` 与 `/dev/cdrom→/mnt/cdrom iso9660` |
| Unmount | 13 | 仅 `/mnt/cdrom`，且无 Iso9660 打开描述时才可卸载 |
| ListMounts | 14 | `data[0]`=索引（0/1）；回复设备/挂载点/类型/只读标志 |
| Format | 15 | 当前恒失败 |
| ReadAtAsync | 16 | `data[0]`=fd、`data[1]`=绝对偏移、`data[2]`=大小（≤4088）、`data[3]`=requestId、`handles[0]`=完成端点；仅 Ext3 |
| WriteAtAsync | 17 | 同上 |
| ReadDirectoryBatch | 18 | `data[7]`=起始索引、`data[8]`=请求条数；上限约 73 项；回复条数 + 条目数组 |
| Link | 19 | `data`+0=existing、+256=newPath |
| ChangeMode | 20 | `data[32]`=mode |
| ChangeOwner | 21 | `data[32]`=uid、`data[33]`=gid（≤0xFFFF） |
| ChangeTimes | 22 | `data[32]`=atime、`data[33]`=mtime（≤UINT32_MAX） |
| CloneDescriptors | 23 | `data[0]`=父进程 pid（fork 语义）；把父进程全部描述符克隆给 `senderPid`，引用计数 +1；回复 (旧号,新号) 对 |

异步完成消息类型 `0x4653434f4d504c`（"FSCMPL"）：`data[0]`=requestId、`data[1]`=状态、`data[2]`=传输数、读数据在 `data[3..]`。

## 5. 格式探测框架（format.h/format.cc）

```cpp
enum class Format : uint8_t { Unknown, Ext2, Ext3, Fat32, ExFat, Iso9660 };
Format detectFormat(bootData, bootSize, isoDescriptor, isoSize);
```

探测优先级：**ext3 → ext2 → fat32 → exfat → iso9660**（ISO 主卷描述符位于 LBA16，单独传入）。`mkfs` 工具链以此为基础。

## 6. ext3 文件系统（完整读写驱动）

源码：`src/fs/ext3/`（`super.cc`、`block.cc`、`volume.cc`），头文件 `src/fs/ext3.h`。

### 磁盘结构（小端，`#pragma pack(1)`）

- `Superblock`（1024 字节，位于字节偏移 1024-2047）：inodesCount、blocksCount、freeBlocksCount、freeInodesCount、firstDataBlock、logBlockSize、blocksPerGroup、inodesPerGroup、magic（`0xef53`）、state、compatibleFeatures、incompatibleFeatures、inodeSize 等。
- `GroupDescriptor`（32 字节）：blockBitmap、inodeBitmap、inodeTable、freeBlocksCount、freeInodesCount、usedDirectoriesCount。
- `Inode`（128 字节）：mode、uid、size、atime/ctime/mtime/dtime、gid、linksCount、sectorsCount、blocks[15]（12 直接 + 单/双/三间接）等。
- `RawDirectoryEntry`：inode(4)、recordLength(2)、nameLength(1)、fileType(1) + 名字，记录 4 字节对齐。

### 挂载校验

- 魔数匹配；`compatibleFeatures & 0x4`（has_journal）必须置位；
- `incompatibleFeatures & ~0x2` 必须为 0（仅允许 filetype 特性）；
- 块大小 = `1024 << logBlockSize`，限 1024-4096、512 的倍数、2 的幂；
- inodeSize ≥128 且 ≤块大小且为 2 的幂；
- 读根 inode 2 并验证为目录。

### Volume 接口

```cpp
mount(SectorDevice&); lookup(path, FileStatus&);
read/write(inode, offset, buffer, size); truncate(inode);
create(path, directory); remove(path); link(existing, newPath);
changeMode/changeOwner/changeTimes;
readDirectory(path, index, entry); readDirectoryBatch(path, index, entries, capacity);
sync();
```

### 关键实现

- **块缓存**：16 槽 LRU（4096 字节/槽），写穿透。
- **块映射**：12 直接 + 单间接 + 双间接 + 三间接；`ensureDataBlock` 按需分配，优先与前一逻辑块同组。
- **分配**：`allocateBlock`/`allocateInode` 扫描位图，更新位图块、组描述符、超级块并清零新块。
- **读**：空洞块读作零；距上次 atime 超过 3600 秒则更新。
- **写**：仅普通文件；`offset+size ≤ UINT32_MAX`；写后更新 size/mtime/ctime 并 `sync()`（超级块 state=1 + 设备 flush）。
- **目录项**：添加优先复用空槽，其次拆分记录剩余空间，否则追加新块；删除合并到前驱记录或置 inode=0；名字 ≤255 字节。
- **create**：目录 0755/链接数 2（初始化 `.`/`..`），文件 0644/链接数 1；时间戳取墙钟。
- **remove**：目录必须为空；文件链接数 >1 仅减链接；否则释放全部数据块与间接块、清 inode、释放 inode 位。
- **不做 journal replay**：日志重放不在本实现范围。

## 7. ext2（探测级）

`probeExt2`：超级块偏移 1024，魔数 `0xef53`，特性位 `has_journal`（0x4）**必须为 0**。无独立卷驱动——纯 ext2 卷只能被探测识别，不能被挂载读写。块寻址 `extBlockOffset(block, blockSize) = block * blockSize`（与 ext3 共用）。

## 8. FAT32（探测级）

`probeFat32`：引导签名 `0x55AA`（偏移 510-511）；偏移 82-89 的 OEM 串必须为 `"FAT32   "`。簇地址换算 `fatBlockOffset(cluster, sectorsPerCluster, firstDataSector, bytesPerSector)`。无卷驱动。

## 9. exFAT（探测级）

`probeExFat`：引导签名 `0x55AA`；偏移 3-10 的 OEM 名必须为 `"EXFAT   "`。簇寻址复用 `fatBlockOffset`。无卷驱动。

## 10. ISO 9660（只读驱动）

源码：`src/fs/iso/`，头文件 `src/fs/iso.h`。

- `isoBlockSize = 2048`；一个 ISO 块 = 4 个 512 扇区。
- **挂载**：要求设备只读；读 LBA 16 主卷描述符：类型 1、签名 `"CD001"`、版本 1；根目录记录在偏移 156。
- **目录记录**：首字节=长度；长度 0 跳到下一块边界；最小 34 字节不跨块；名字在偏移 33；单字节 0/1 名字为 `.`/`..`，跳过。
- **名字比较**：截断 `;` 版本号后缀，大小写不敏感。
- **读文件**：拒绝目录；按 2048 块边界分段拷贝。
- **读目录**：输出名小写、去版本后缀、截断 47 字节。
- `probeIso9660`：对主卷描述符首 7 字节探测（类型 1 + "CD001" + 版本 1）。

## 11. 能力矩阵

| 文件系统 | 探测 | 挂载 | 读写 |
|---|---|---|---|
| ext3 | ✓ | ✓（根） | ✓ 完整（目录/链接/权限/时间/截断/批量目录读） |
| ISO 9660 | ✓ | ✓（/mnt/cdrom） | 只读 |
| ext2 | ✓ | ✗ | ✗ |
| FAT32 | ✓ | ✗ | ✗ |
| exFAT | ✓ | ✗ | ✗ |

## 12. 虚拟节点

`/dev` 目录由 fs 服务合成，内容来自 device 服务的固定 12 节点表；`/dev/zero` 读经 device 服务 ReadNode；`/dev/pts` 报为目录。
