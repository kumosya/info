# FAT

源码目录使用 `src/fs/fat/`，头文件使用 `include/service/fs/fat/`。当前实际驱动类为 `Fat32FileSystem`，磁盘格式名称仍为 `fat32`。

`vfs::fat::Probe::detect()` 检查 FAT32 卷信息，`Probe::blockOffset()` 根据簇号、每簇扇区数、数据区起点和每扇区字节数计算偏移。`Fat32FileSystem::mount()` 绑定设备并读取卷参数。

`lookup()` 查找路径，`read()`、`write()` 遍历簇链读写文件，`create()`、`remove()`、`rename()` 修改目录项，`readDirectory()` 枚举名称，`sync()` 完成设备刷新。短文件名和长文件名处理放在 `name.h` 的 `NameCodec` 中。
