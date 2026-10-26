# procfs

源码位于 `src/fs/procfs/`，声明位于 `include/service/fs/procfs/`。这些文件由 fs 服务动态提供，不存放在磁盘卷中。

`ProcessFiles::lookup(path, caller, status)` 检查路径并查询对象属性，`directory()` 根据目录游标返回下一项，`snapshot()` 把进程属性转换为可读取的文本，`nextCursor()` 生成后续枚举位置。`caller` 是访问进程编号，用于区分调用者和目标进程。

`ProcessObjectPath` 处理进程对象路径。fs 根据描述符保留读入快照，普通应用仍通过文件和目录接口访问，不在 libc 中为 `/proc` 再建立一套目录读取分支。
