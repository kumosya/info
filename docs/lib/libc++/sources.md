# libc++ 运行时源文件

| 文件 | 功能 | 函数 |
| --- | --- | --- |
| `lib/libc++/abi.cc` | 对象析构登记与运行时调用入口。 | `__cxa_atexit`：登记析构函数；`__cxa_finalize`：执行析构函数；`__cxa_deleted_virtual`：处理已删除虚函数调用。 |
| `lib/libc++/new.cc` | 分配、释放对象内存及虚函数调用入口。 | `__cxa_pure_virtual`：处理纯虚函数调用。 |
