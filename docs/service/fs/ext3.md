# ext3

源码位于 `src/fs/ext3/`，声明位于 `include/service/fs/ext3/`。

`vfs::ext3::Probe::detect()` 识别超级块；`fs::ext3::Filesystem::mount()` 读取超级块和块组信息并绑定块设备。`lookup()` 逐级查找路径，`read()` 和 `write()` 处理文件数据，`readDirectory()` 返回目录项，`sync()` 把缓存修改写回设备。

`superblock.cc` 处理卷信息，`block.cc` 和 `balloc.cc` 处理块读写与分配，`inode.cc` 和 `ialloc.cc` 处理 inode，`file.cc` 处理文件内容，`dir.cc` 处理目录。内部编码和磁盘布局留在本目录的 `internal.h`、`filesystem.h`，公共块设备接口使用上层的 `block.h`。
