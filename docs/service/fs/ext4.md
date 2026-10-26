# ext4

源码位于 `src/fs/ext4/`，声明位于 `include/service/fs/ext4/`。

`vfs::ext4::Probe::detect()` 根据超级块特征识别格式，`fs::ext4::Filesystem::mount()` 校验受支持的卷特性。路径查询、文件读写和目录枚举分别由 `lookup()`、`read()`、`write()`、`readDirectory()` 完成；`sync()` 提交缓存修改。

超级块、块分配、inode 分配、文件、目录和同步分别放在 `superblock.cc`、`balloc.cc`、`ialloc.cc`、`file.cc`、`dir.cc`、`sync.cc`。ext4 自身的磁盘编码声明放在本目录中，不在文件系统公共目录新增 `ext4.h`。
