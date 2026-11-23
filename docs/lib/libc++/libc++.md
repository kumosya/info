# libc++ 运行时

## 常量

| 名称 | 值 | 说明 |
| --- | --- | --- |
| `destructorCapacity` | `32` | 对象析构记录数量 |

## 全局变量

| 名称 | 说明 |
| --- | --- |
| `__dso_handle` | 当前动态库的对象析构标识 |

## 对象

| 名称 | 说明 |
| --- | --- |
| `DestructorEntry` | 析构记录，保存析构函数与关联对象。 |

## 函数

### 对象生命周期

| 函数 | 用途 | 参数 |
| --- | --- | --- |
| `int __cxa_atexit(void (*destructor)(void*), void* object, void* dso)` | 登记对象析构函数 | `destructor`：对象析构回调函数<br>`object`：运行库持有的文件对象<br>`dso`：动态库标识 |
| `void __cxa_finalize(void* dso)` | 执行已登记的对象析构函数 | `dso`：动态库标识 |
| `void __cxa_deleted_virtual()` | 处理已删除虚函数调用 | — |
| `void __cxa_pure_virtual()` | 处理纯虚函数调用 | — |
