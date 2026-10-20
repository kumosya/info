# libc 与 POSIX 接口

标准接口以 [POSIX.1-2024 清单](posix-c-functions.md) 为参考，包含 70 个接口头文件、1225 个函数及函数式接口。入口齐全不代表所有可选系统设施都已具备运行后端；暂缺后端的调用遵循各接口的失败形式，返回 ENOSYS 等明确错误，不报告成功。

SDK 分别安装到 `/usr/include/stdc`、`/usr/include/stdc++`、`/usr/include/posix`。libc、libc++ 和这些头文件由 libc++ 软件包提供。可移植数学、复数、正则、编码、字符和部分 C 库代码来自 musl 1.2.5，保留源码许可证及 `/usr/lib/libc.COPYRIGHT`。

文件描述符表保存引用计数的 OpenDescription，每个描述拥有一个 os::File 对象。dup 共享对象及文件偏移；fork 重新绑定服务句柄；exec 保留非 CLOEXEC 描述符的共享关系。open 接受标准 flags/mode，支持目录描述符和 openat；创建模式应用 umask。fstat 从描述符对应的 File 查询；pread/pwrite 使用独立偏移 RPC。File 的私有元数据通过 getter 提供给 C++。

目录流使用真实 fd，支持 dirfd、fdopendir、scandir、readdir_r 和 posix_getdents。glob/fnmatch 按路径分量展开、排序并遵守隐藏文件规则，递归缓冲位于堆上。

目录记录包含 inode 与不透明 nextOffset，readdir/telldir/getdents 不再对 /proc 做路径特判。C++ 的 os::File::fsync 与 C 的 fsync/fdatasync 经 FS Sync 请求调用实际文件系统同步；非法描述符和非文件后端明确失败。服务配置持久化使用该同步路径后替换临时文件，接口存在不再等同于返回 ENOSYS。

公共 C++ 任务类为 os::Task；libc 保持 POSIX C 函数名，OsTaskInfo 与 SDK TaskInfo 同步布局。进程工具从 /proc 获取列表，C 适配不保留旧 TaskListEntry 列表包装。C++ 源和专用头采用 cstdio/cstdint/cstring 等包装头；C 源、C/C++ 共用头及包装头自身保留正确 C 头。

POSIX 进程接口使用与 OS 库统一的系统调用号；文件服务操作通过 libc 私有 C++ 适配连接 File。公开 OS SDK 不含 C 适配头。errno 是按内核线程 id 分配的可修改左值宏，线程退出时回收槽位。pthread 提供线程、同步、TLS 与清理栈；缺少后端的操作通过标准返回值指示错误。

数学、复数、浮点环境、字符串、排序搜索、正则表达式、整数/浮点解析、printf/scanf、C/UTF-8 locale 和编码转换在用户态实现。日历转换当前以 UTC 为本地时区。文件系统时间戳精度由相应驱动决定。

sh 支持未引用的 `~`、`~/...`、`~user` 和 `*`、`?`、字符组；引号或反斜杠保护的字符保留原义。无匹配时保留原模式，重定向必须展开到单个路径。`sh -c` 在继承的工作目录与环境中执行命令，不重新运行登录 profile。
