# sysfs

源码位于 `src/fs/sysfs/filesystem.cc`，声明位于 `include/service/fs/sysfs/filesystem.h`。

`SystemFiles::init(root)` 保存系统文件所需的根文件系统入口，`lookup(path, status)` 查找系统属性文件，`directory(path, index, entry)` 枚举目录项，`read(inode, offset, buffer, size)` 读取属性内容。

属性由系统状态生成，不作为普通磁盘文件修改。路径解析和文件读取仍经过 fs 服务，应用不直接依赖 `SystemFiles`。
