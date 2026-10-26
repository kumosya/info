# ISO 9660

源码位于 `src/fs/iso/`，声明位于 `include/service/fs/iso/filesystem.h`。

`vfs::iso::Probe::detect()` 检查卷描述符，`Probe::blockOffset(block)` 把逻辑块号转换为字节偏移。`Iso9660FileSystem::mount()` 读取主卷描述符并保存根目录位置。

`lookup()` 沿目录记录查找文件，`readDirectory()` 枚举目录项，`read()` 按记录中的位置读取文件。该驱动按只读文件系统使用；不能因为块设备允许写入而把 ISO 卷作为可写目录处理。
