# exFAT 格式识别

源码位于 `src/fs/exfat/`，专用声明位于 `include/service/fs/exfat/filesystem.h`。

`vfs::exfat::Probe::detect(data, size)` 检查引导扇区长度、末尾的 `55 AA` 标记和偏移 3 开始的 `EXFAT` 文件系统名称。`data` 是读入的扇区，`size` 是可访问的字节数；不满足条件返回 false。

目前这里提供格式识别，尚没有完整的 exFAT 文件、目录和分配位图实现。`block.cc` 也尚未实现独立的块读写逻辑。探测到 exFAT 不代表可以挂载或写入。
