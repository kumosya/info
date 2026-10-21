# POSIX.1-2024 C 语言函数与接口完整清单

本文以 **IEEE Std 1003.1-2024 / The Open Group Base Specifications Issue 8** 为基准，核对日期为 **2026-10-17**。覆盖标准 System Interfaces 卷收录的 C 函数，以及与函数一起列出的函数式宏、原子泛型接口和特殊调用接口，包含标准所载的可选、XSI 及 OB 接口。

按官方索引和头文件逐项核对后，本清单列出 **1225 个当前接口名称**，分布在 **70 个头文件**下：普通函数 1174 个、函数式宏 26 个、原子泛型接口 22 个、特殊接口 3 个。同名接口在主头文件下列一次。另将 **16 个标准对象或对象式宏**和 **2 个已移除的历史索引项**单独列出。

## 1. 范围与阅读方法

- **POSIX 与 ISO C 有交集**：`printf`、`malloc`、`memcpy`、数学函数、ISO C 线程等同样属于本清单。POSIX 的 C 接口范围包含这些函数。
- **可选不等于非标准**：XSI、实时、设备控制等选项中的接口仍属于标准所描述的接口；是否要求某个实现提供，取决于其声明支持的选项和配置。每行的接口名称链接到官方定义，可查询具体约束、参数、返回值和错误码。
- **OB 表示过时但仍保留**：例如 `asctime`、`ctime`、`pthread_atfork`。已从当前版本移除的接口不计入当前接口总数。
- **函数不等于系统调用**：这些是 C 语言绑定。实现可以位于 libc、其他库、用户态服务或内核，不要求每个函数对应一个系统调用。
- **声明是标准接口形式**：多数参数名被省略；宏的声明形式用于说明参数和结果，并不要求存在同名可链接函数。`restrict`、可变参数和回调类型按标准保留。
- **项目实现情况**：这份文档描述标准范围，不是已实现清单。项目已有实现和限制参见 [libc 实现文档](libc.md)；仓库中存在声明或占位实现也不能单独证明已满足完整标准语义。

普通函数也可能同时由宏提供，表中的“函数”不排除这种实现方式。`pthread_cleanup_push/pop` 允许通过配对宏实现，`sigsetjmp` 允许为宏，因此另标为“特殊接口”。原子操作中的 `A`、`C`、`M` 是标准对泛型类型的占位记号；`type`、`scalar`、`real-floating` 同样是说明记号，不是需要自行定义的 typedef。

### 编译时选择标准版本

特性测试宏应在应用包含任何头文件之前定义，或通过编译器 `-D` 选项定义。请求 POSIX.1-2024 接口可使用：

```c
#define _POSIX_C_SOURCE 202405L
#include <unistd.h>
```

需要 Issue 8 的 XSI 接口时使用 `_XOPEN_SOURCE=800`。宏值只是在请求声明相应接口；实际支持仍应依据实现文档、选项宏，以及需要时的 `sysconf`、`pathconf`、`confstr` 查询。

## 2. 头文件导航

| 头文件 | 当前接口数 | 用途 |
|---|---:|---|
| [`<aio.h>`](#header-aio-h) | 8 | 异步文件 I/O：提交、取消、等待和查询请求。 |
| [`<arpa/inet.h>`](#header-arpa-inet-h) | 8 | IPv4/IPv6 地址文本转换及网络字节序转换。 |
| [`<assert.h>`](#header-assert-h) | 1 | 运行期断言；assert 为函数式宏。 |
| [`<complex.h>`](#header-complex-h) | 69 | 复数运算、复数初等函数及复数构造宏。 |
| [`<ctype.h>`](#header-ctype-h) | 28 | 字符分类、大小写转换及 locale 版本。 |
| [`<devctl.h>`](#header-devctl-h) | 1 | 设备控制接口；受 Device Control（DC）选项约束。 |
| [`<dirent.h>`](#header-dirent-h) | 12 | 目录流、目录遍历、排序及批量获取目录项。 |
| [`<dlfcn.h>`](#header-dlfcn-h) | 5 | 动态加载对象、符号查找和地址信息查询。 |
| [`<endian.h>`](#header-endian-h) | 12 | 主机、大端和小端整数表示之间的转换。 |
| [`<fcntl.h>`](#header-fcntl-h) | 6 | 打开和创建文件、文件描述符控制及文件访问建议。 |
| [`<fenv.h>`](#header-fenv-h) | 11 | 浮点异常状态、舍入方式和浮点环境。 |
| [`<fmtmsg.h>`](#header-fmtmsg-h) | 1 | 格式化诊断消息；XSI 接口。 |
| [`<fnmatch.h>`](#header-fnmatch-h) | 1 | 文件名通配模式匹配。 |
| [`<ftw.h>`](#header-ftw-h) | 1 | 递归遍历文件树；XSI 接口。 |
| [`<glob.h>`](#header-glob-h) | 2 | 路径名通配展开及结果释放。 |
| [`<grp.h>`](#header-grp-h) | 7 | 用户组数据库查询和遍历。 |
| [`<iconv.h>`](#header-iconv-h) | 3 | 字符编码转换及转换描述符管理。 |
| [`<inttypes.h>`](#header-inttypes-h) | 6 | 最大宽度整数的转换、绝对值、除法及格式宏。 |
| [`<langinfo.h>`](#header-langinfo-h) | 2 | 查询 locale 的语言、编码和格式信息。 |
| [`<libgen.h>`](#header-libgen-h) | 2 | 提取路径的目录部分和文件名部分。 |
| [`<libintl.h>`](#header-libintl-h) | 15 | gettext 消息翻译、消息域及字符集设置。 |
| [`<locale.h>`](#header-locale-h) | 7 | 全局及线程 locale 对象的创建、设置和查询。 |
| [`<math.h>`](#header-math-h) | 189 | 实数数学函数、浮点分类和比较宏；f/l 后缀版本分别面向 float/long double。 |
| [`<monetary.h>`](#header-monetary-h) | 2 | 按 locale 格式化货币金额。 |
| [`<mqueue.h>`](#header-mqueue-h) | 10 | POSIX 消息队列及消息通知、定时收发。 |
| [`<ndbm.h>`](#header-ndbm-h) | 9 | 键值数据库的打开、查询、遍历和更新；XSI 接口。 |
| [`<net/if.h>`](#header-net-if-h) | 4 | 网络接口名称与索引的转换和枚举。 |
| [`<netdb.h>`](#header-netdb-h) | 22 | 主机、地址、网络、协议和服务数据库。 |
| [`<nl_types.h>`](#header-nl_types-h) | 3 | 消息目录的打开、读取和关闭。 |
| [`<poll.h>`](#header-poll-h) | 2 | 等待多个文件描述符上的 I/O 事件。 |
| [`<pthread.h>`](#header-pthread-h) | 102 | POSIX 线程、线程属性、取消、线程局部存储及同步原语。 |
| [`<pwd.h>`](#header-pwd-h) | 7 | 用户账户数据库查询和遍历。 |
| [`<regex.h>`](#header-regex-h) | 4 | 正则表达式的编译、匹配、错误格式化和释放。 |
| [`<sched.h>`](#header-sched-h) | 8 | 进程调度策略、优先级和主动让出处理器。 |
| [`<search.h>`](#header-search-h) | 11 | 哈希表、线性表、链式队列及二叉树搜索。 |
| [`<semaphore.h>`](#header-semaphore-h) | 11 | 命名和匿名信号量及定时等待。 |
| [`<setjmp.h>`](#header-setjmp-h) | 4 | 保存和恢复非局部跳转环境，包括信号屏蔽字版本。 |
| [`<signal.h>`](#header-signal-h) | 24 | 信号处理、信号集、信号等待及线程信号操作。 |
| [`<spawn.h>`](#header-spawn-h) | 23 | 创建进程以及配置创建属性和文件动作。 |
| [`<stdarg.h>`](#header-stdarg-h) | 4 | 可变参数列表操作；本节接口均为宏。 |
| [`<stdatomic.h>`](#header-stdatomic-h) | 29 | ISO C 原子操作、内存序和内存栅栏。 |
| [`<stdio.h>`](#header-stdio-h) | 68 | 标准流、文件流、格式化 I/O、内存流和流锁。 |
| [`<stdlib.h>`](#header-stdlib-h) | 72 | 内存分配、数字转换、进程退出、环境变量、排序及随机数。 |
| [`<string.h>`](#header-string-h) | 37 | 字节内存操作、字符串复制、拼接、比较、查找和分割。 |
| [`<strings.h>`](#header-strings-h) | 7 | 忽略大小写的字符串比较及首个置位位查询。 |
| [`<sys/ipc.h>`](#header-sys-ipc-h) | 1 | XSI IPC 键值和通用定义。 |
| [`<sys/mman.h>`](#header-sys-mman-h) | 14 | 内存映射、保护、同步、锁页、共享内存和类型化内存。 |
| [`<sys/msg.h>`](#header-sys-msg-h) | 4 | XSI 消息队列。 |
| [`<sys/resource.h>`](#header-sys-resource-h) | 5 | 资源限制、资源使用量和进程优先级；XSI 接口。 |
| [`<sys/select.h>`](#header-sys-select-h) | 6 | 文件描述符集合宏及 select/pselect 事件等待。 |
| [`<sys/sem.h>`](#header-sys-sem-h) | 3 | XSI 信号量集合。 |
| [`<sys/shm.h>`](#header-sys-shm-h) | 4 | XSI 共享内存段。 |
| [`<sys/socket.h>`](#header-sys-socket-h) | 19 | 套接字创建、连接、监听、收发及选项控制。 |
| [`<sys/stat.h>`](#header-sys-stat-h) | 16 | 文件元数据、权限、文件类型、目录/FIFO 创建及时间戳。 |
| [`<sys/statvfs.h>`](#header-sys-statvfs-h) | 2 | 文件系统容量和属性。 |
| [`<sys/time.h>`](#header-sys-time-h) | 1 | 时间值和兼容接口；select 统一列在 sys/select.h。 |
| [`<sys/times.h>`](#header-sys-times-h) | 1 | 进程及子进程 CPU 时间统计。 |
| [`<sys/uio.h>`](#header-sys-uio-h) | 2 | 分散读取和聚集写入。 |
| [`<sys/utsname.h>`](#header-sys-utsname-h) | 1 | 操作系统和机器标识。 |
| [`<sys/wait.h>`](#header-sys-wait-h) | 3 | 等待子进程状态变化。 |
| [`<syslog.h>`](#header-syslog-h) | 4 | 系统日志连接、过滤及消息提交。 |
| [`<termios.h>`](#header-termios-h) | 13 | 终端属性、速率、前后台会话、流控制和窗口尺寸。 |
| [`<threads.h>`](#header-threads-h) | 25 | ISO C 线程、互斥量、条件变量及线程专用存储。 |
| [`<time.h>`](#header-time-h) | 27 | 日历时间、时钟、休眠、定时器和时间格式转换。 |
| [`<uchar.h>`](#header-uchar-h) | 4 | 多字节字符与 UTF-16/UTF-32 编码单元的转换。 |
| [`<unistd.h>`](#header-unistd-h) | 90 | 文件描述符、进程、身份、路径、管道和系统配置。 |
| [`<utmpx.h>`](#header-utmpx-h) | 6 | 用户会话记录的查询、遍历和更新。 |
| [`<wchar.h>`](#header-wchar-h) | 76 | 宽字符字符串、宽字符 I/O 和多字节编码转换。 |
| [`<wctype.h>`](#header-wctype-h) | 36 | 宽字符分类、大小写转换及 locale 版本。 |
| [`<wordexp.h>`](#header-wordexp-h) | 2 | 按 shell 规则展开单词及释放结果。 |

## 3. 按头文件列出的全部当前接口

<a id="header-aio-h"></a>

### `<aio.h>`

异步文件 I/O：提交、取消、等待和查询请求。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/aio.h.html)，本节共 8 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`aio_cancel`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_cancel.html) | `int aio_cancel(int, struct aiocb *);` | 函数 |
| [`aio_error`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_error.html) | `int aio_error(const struct aiocb *);` | 函数 |
| [`aio_fsync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_fsync.html) | `int aio_fsync(int, struct aiocb *);` | 函数 |
| [`aio_read`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_read.html) | `int aio_read(struct aiocb *);` | 函数 |
| [`aio_return`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_return.html) | `ssize_t aio_return(struct aiocb *);` | 函数 |
| [`aio_suspend`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_suspend.html) | `int aio_suspend(const struct aiocb *const [], int, const struct timespec *);` | 函数 |
| [`aio_write`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aio_write.html) | `int aio_write(struct aiocb *);` | 函数 |
| [`lio_listio`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lio_listio.html) | `int lio_listio(int, struct aiocb *restrict const [restrict], int, struct sigevent *restrict);` | 函数 |

<a id="header-arpa-inet-h"></a>

### `<arpa/inet.h>`

IPv4/IPv6 地址文本转换及网络字节序转换。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/arpa_inet.h.html)，本节共 8 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`htonl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htonl.html) | `uint32_t htonl(uint32_t);` | 函数 |
| [`htons`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htons.html) | `uint16_t htons(uint16_t);` | 函数 |
| [`inet_addr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/inet_addr.html) | `in_addr_t inet_addr(const char *);` | 函数 |
| [`inet_ntoa`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/inet_ntoa.html) | `char *inet_ntoa(struct in_addr);` | 函数 |
| [`inet_ntop`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/inet_ntop.html) | `const char *inet_ntop(int, const void *restrict, char *restrict, socklen_t);` | 函数 |
| [`inet_pton`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/inet_pton.html) | `int inet_pton(int, const char *restrict, void *restrict);` | 函数 |
| [`ntohl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ntohl.html) | `uint32_t ntohl(uint32_t);` | 函数 |
| [`ntohs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ntohs.html) | `uint16_t ntohs(uint16_t);` | 函数 |

<a id="header-assert-h"></a>

### `<assert.h>`

运行期断言；assert 为函数式宏。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/assert.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`assert`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/assert.html) | `void assert(scalar expression);` | 宏 |

<a id="header-complex-h"></a>

### `<complex.h>`

复数运算、复数初等函数及复数构造宏。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/complex.h.html)，本节共 69 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`CMPLX`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/CMPLX.html) | `double complex CMPLX(double x, double y);` | 宏 |
| [`CMPLXF`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/CMPLXF.html) | `float complex CMPLXF(float x, float y);` | 宏 |
| [`CMPLXL`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/CMPLXL.html) | `long double complex CMPLXL(long double x, long double y);` | 宏 |
| [`cabs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cabs.html) | `double cabs(double complex);` | 函数 |
| [`cabsf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cabsf.html) | `float cabsf(float complex);` | 函数 |
| [`cabsl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cabsl.html) | `long double cabsl(long double complex);` | 函数 |
| [`cacos`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cacos.html) | `double complex cacos(double complex);` | 函数 |
| [`cacosf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cacosf.html) | `float complex cacosf(float complex);` | 函数 |
| [`cacosh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cacosh.html) | `double complex cacosh(double complex);` | 函数 |
| [`cacoshf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cacoshf.html) | `float complex cacoshf(float complex);` | 函数 |
| [`cacoshl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cacoshl.html) | `long double complex cacoshl(long double complex);` | 函数 |
| [`cacosl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cacosl.html) | `long double complex cacosl(long double complex);` | 函数 |
| [`carg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/carg.html) | `double carg(double complex);` | 函数 |
| [`cargf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cargf.html) | `float cargf(float complex);` | 函数 |
| [`cargl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cargl.html) | `long double cargl(long double complex);` | 函数 |
| [`casin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/casin.html) | `double complex casin(double complex);` | 函数 |
| [`casinf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/casinf.html) | `float complex casinf(float complex);` | 函数 |
| [`casinh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/casinh.html) | `double complex casinh(double complex);` | 函数 |
| [`casinhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/casinhf.html) | `float complex casinhf(float complex);` | 函数 |
| [`casinhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/casinhl.html) | `long double complex casinhl(long double complex);` | 函数 |
| [`casinl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/casinl.html) | `long double complex casinl(long double complex);` | 函数 |
| [`catan`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catan.html) | `double complex catan(double complex);` | 函数 |
| [`catanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catanf.html) | `float complex catanf(float complex);` | 函数 |
| [`catanh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catanh.html) | `double complex catanh(double complex);` | 函数 |
| [`catanhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catanhf.html) | `float complex catanhf(float complex);` | 函数 |
| [`catanhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catanhl.html) | `long double complex catanhl(long double complex);` | 函数 |
| [`catanl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catanl.html) | `long double complex catanl(long double complex);` | 函数 |
| [`ccos`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ccos.html) | `double complex ccos(double complex);` | 函数 |
| [`ccosf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ccosf.html) | `float complex ccosf(float complex);` | 函数 |
| [`ccosh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ccosh.html) | `double complex ccosh(double complex);` | 函数 |
| [`ccoshf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ccoshf.html) | `float complex ccoshf(float complex);` | 函数 |
| [`ccoshl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ccoshl.html) | `long double complex ccoshl(long double complex);` | 函数 |
| [`ccosl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ccosl.html) | `long double complex ccosl(long double complex);` | 函数 |
| [`cexp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cexp.html) | `double complex cexp(double complex);` | 函数 |
| [`cexpf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cexpf.html) | `float complex cexpf(float complex);` | 函数 |
| [`cexpl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cexpl.html) | `long double complex cexpl(long double complex);` | 函数 |
| [`cimag`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cimag.html) | `double cimag(double complex);` | 函数 |
| [`cimagf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cimagf.html) | `float cimagf(float complex);` | 函数 |
| [`cimagl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cimagl.html) | `long double cimagl(long double complex);` | 函数 |
| [`clog`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clog.html) | `double complex clog(double complex);` | 函数 |
| [`clogf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clogf.html) | `float complex clogf(float complex);` | 函数 |
| [`clogl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clogl.html) | `long double complex clogl(long double complex);` | 函数 |
| [`conj`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/conj.html) | `double complex conj(double complex);` | 函数 |
| [`conjf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/conjf.html) | `float complex conjf(float complex);` | 函数 |
| [`conjl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/conjl.html) | `long double complex conjl(long double complex);` | 函数 |
| [`cpow`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cpow.html) | `double complex cpow(double complex, double complex);` | 函数 |
| [`cpowf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cpowf.html) | `float complex cpowf(float complex, float complex);` | 函数 |
| [`cpowl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cpowl.html) | `long double complex cpowl(long double complex, long double complex);` | 函数 |
| [`cproj`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cproj.html) | `double complex cproj(double complex);` | 函数 |
| [`cprojf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cprojf.html) | `float complex cprojf(float complex);` | 函数 |
| [`cprojl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cprojl.html) | `long double complex cprojl(long double complex);` | 函数 |
| [`creal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/creal.html) | `double creal(double complex);` | 函数 |
| [`crealf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/crealf.html) | `float crealf(float complex);` | 函数 |
| [`creall`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/creall.html) | `long double creall(long double complex);` | 函数 |
| [`csin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csin.html) | `double complex csin(double complex);` | 函数 |
| [`csinf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csinf.html) | `float complex csinf(float complex);` | 函数 |
| [`csinh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csinh.html) | `double complex csinh(double complex);` | 函数 |
| [`csinhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csinhf.html) | `float complex csinhf(float complex);` | 函数 |
| [`csinhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csinhl.html) | `long double complex csinhl(long double complex);` | 函数 |
| [`csinl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csinl.html) | `long double complex csinl(long double complex);` | 函数 |
| [`csqrt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csqrt.html) | `double complex csqrt(double complex);` | 函数 |
| [`csqrtf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csqrtf.html) | `float complex csqrtf(float complex);` | 函数 |
| [`csqrtl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/csqrtl.html) | `long double complex csqrtl(long double complex);` | 函数 |
| [`ctan`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctan.html) | `double complex ctan(double complex);` | 函数 |
| [`ctanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctanf.html) | `float complex ctanf(float complex);` | 函数 |
| [`ctanh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctanh.html) | `double complex ctanh(double complex);` | 函数 |
| [`ctanhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctanhf.html) | `float complex ctanhf(float complex);` | 函数 |
| [`ctanhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctanhl.html) | `long double complex ctanhl(long double complex);` | 函数 |
| [`ctanl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctanl.html) | `long double complex ctanl(long double complex);` | 函数 |

<a id="header-ctype-h"></a>

### `<ctype.h>`

字符分类、大小写转换及 locale 版本。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/ctype.h.html)，本节共 28 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`isalnum`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isalnum.html) | `int isalnum(int);` | 函数 |
| [`isalnum_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isalnum_l.html) | `int isalnum_l(int, locale_t);` | 函数 |
| [`isalpha`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isalpha.html) | `int isalpha(int);` | 函数 |
| [`isalpha_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isalpha_l.html) | `int isalpha_l(int, locale_t);` | 函数 |
| [`isblank`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isblank.html) | `int isblank(int);` | 函数 |
| [`isblank_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isblank_l.html) | `int isblank_l(int, locale_t);` | 函数 |
| [`iscntrl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iscntrl.html) | `int iscntrl(int);` | 函数 |
| [`iscntrl_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iscntrl_l.html) | `int iscntrl_l(int, locale_t);` | 函数 |
| [`isdigit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isdigit.html) | `int isdigit(int);` | 函数 |
| [`isdigit_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isdigit_l.html) | `int isdigit_l(int, locale_t);` | 函数 |
| [`isgraph`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isgraph.html) | `int isgraph(int);` | 函数 |
| [`isgraph_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isgraph_l.html) | `int isgraph_l(int, locale_t);` | 函数 |
| [`islower`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/islower.html) | `int islower(int);` | 函数 |
| [`islower_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/islower_l.html) | `int islower_l(int, locale_t);` | 函数 |
| [`isprint`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isprint.html) | `int isprint(int);` | 函数 |
| [`isprint_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isprint_l.html) | `int isprint_l(int, locale_t);` | 函数 |
| [`ispunct`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ispunct.html) | `int ispunct(int);` | 函数 |
| [`ispunct_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ispunct_l.html) | `int ispunct_l(int, locale_t);` | 函数 |
| [`isspace`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isspace.html) | `int isspace(int);` | 函数 |
| [`isspace_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isspace_l.html) | `int isspace_l(int, locale_t);` | 函数 |
| [`isupper`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isupper.html) | `int isupper(int);` | 函数 |
| [`isupper_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isupper_l.html) | `int isupper_l(int, locale_t);` | 函数 |
| [`isxdigit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isxdigit.html) | `int isxdigit(int);` | 函数 |
| [`isxdigit_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isxdigit_l.html) | `int isxdigit_l(int, locale_t);` | 函数 |
| [`tolower`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tolower.html) | `int tolower(int);` | 函数 |
| [`tolower_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tolower_l.html) | `int tolower_l(int, locale_t);` | 函数 |
| [`toupper`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/toupper.html) | `int toupper(int);` | 函数 |
| [`toupper_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/toupper_l.html) | `int toupper_l(int, locale_t);` | 函数 |

<a id="header-devctl-h"></a>

### `<devctl.h>`

设备控制接口；受 Device Control（DC）选项约束。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/devctl.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`posix_devctl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_devctl.html) | `int posix_devctl(int, int, void *restrict, size_t, int *restrict);` | 函数 |

<a id="header-dirent-h"></a>

### `<dirent.h>`

目录流、目录遍历、排序及批量获取目录项。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/dirent.h.html)，本节共 12 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`alphasort`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/alphasort.html) | `int alphasort(const struct dirent **, const struct dirent **);` | 函数 |
| [`closedir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/closedir.html) | `int closedir(DIR *);` | 函数 |
| [`dirfd`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dirfd.html) | `int dirfd(DIR *);` | 函数 |
| [`fdopendir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fdopendir.html) | `DIR *fdopendir(int);` | 函数 |
| [`opendir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/opendir.html) | `DIR *opendir(const char *);` | 函数 |
| [`posix_getdents`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_getdents.html) | `ssize_t posix_getdents(int, void *, size_t, int);` | 函数 |
| [`readdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/readdir.html) | `struct dirent *readdir(DIR *);` | 函数 |
| [`readdir_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/readdir_r.html) | `int readdir_r(DIR *restrict, struct dirent *restrict, struct dirent **restrict);` | 函数 |
| [`rewinddir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rewinddir.html) | `void rewinddir(DIR *);` | 函数 |
| [`scandir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scandir.html) | `int scandir(const char *, struct dirent ***, int (*)(const struct dirent *), int (*)(const struct dirent **, const struct dirent **));` | 函数 |
| [`seekdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/seekdir.html) | `void seekdir(DIR *, long);` | 函数 |
| [`telldir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/telldir.html) | `long telldir(DIR *);` | 函数 |

<a id="header-dlfcn-h"></a>

### `<dlfcn.h>`

动态加载对象、符号查找和地址信息查询。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/dlfcn.h.html)，本节共 5 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`dladdr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dladdr.html) | `int dladdr(const void *restrict, Dl_info_t *restrict);` | 函数 |
| [`dlclose`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlclose.html) | `int dlclose(void *);` | 函数 |
| [`dlerror`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlerror.html) | `char *dlerror(void);` | 函数 |
| [`dlopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlopen.html) | `void *dlopen(const char *, int);` | 函数 |
| [`dlsym`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlsym.html) | `void *dlsym(void *restrict, const char *restrict);` | 函数 |

<a id="header-endian-h"></a>

### `<endian.h>`

主机、大端和小端整数表示之间的转换。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/endian.h.html)，本节共 12 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`be16toh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/be16toh.html) | `uint16_t be16toh(uint16_t);` | 函数 |
| [`be32toh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/be32toh.html) | `uint32_t be32toh(uint32_t);` | 函数 |
| [`be64toh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/be64toh.html) | `uint64_t be64toh(uint64_t);` | 函数 |
| [`htobe16`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htobe16.html) | `uint16_t htobe16(uint16_t);` | 函数 |
| [`htobe32`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htobe32.html) | `uint32_t htobe32(uint32_t);` | 函数 |
| [`htobe64`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htobe64.html) | `uint64_t htobe64(uint64_t);` | 函数 |
| [`htole16`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htole16.html) | `uint16_t htole16(uint16_t);` | 函数 |
| [`htole32`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htole32.html) | `uint32_t htole32(uint32_t);` | 函数 |
| [`htole64`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/htole64.html) | `uint64_t htole64(uint64_t);` | 函数 |
| [`le16toh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/le16toh.html) | `uint16_t le16toh(uint16_t);` | 函数 |
| [`le32toh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/le32toh.html) | `uint32_t le32toh(uint32_t);` | 函数 |
| [`le64toh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/le64toh.html) | `uint64_t le64toh(uint64_t);` | 函数 |

<a id="header-fcntl-h"></a>

### `<fcntl.h>`

打开和创建文件、文件描述符控制及文件访问建议。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/fcntl.h.html)，本节共 6 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`creat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/creat.html) | `int creat(const char *, mode_t);` | 函数 |
| [`fcntl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fcntl.html) | `int fcntl(int, int, ...);` | 函数 |
| [`open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/open.html) | `int open(const char *, int, ...);` | 函数 |
| [`openat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/openat.html) | `int openat(int, const char *, int, ...);` | 函数 |
| [`posix_fadvise`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_fadvise.html) | `int posix_fadvise(int, off_t, off_t, int);` | 函数 |
| [`posix_fallocate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_fallocate.html) | `int posix_fallocate(int, off_t, off_t);` | 函数 |

<a id="header-fenv-h"></a>

### `<fenv.h>`

浮点异常状态、舍入方式和浮点环境。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/fenv.h.html)，本节共 11 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`feclearexcept`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/feclearexcept.html) | `int feclearexcept(int);` | 函数 |
| [`fegetenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fegetenv.html) | `int fegetenv(fenv_t *);` | 函数 |
| [`fegetexceptflag`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fegetexceptflag.html) | `int fegetexceptflag(fexcept_t *, int);` | 函数 |
| [`fegetround`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fegetround.html) | `int fegetround(void);` | 函数 |
| [`feholdexcept`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/feholdexcept.html) | `int feholdexcept(fenv_t *);` | 函数 |
| [`feraiseexcept`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/feraiseexcept.html) | `int feraiseexcept(int);` | 函数 |
| [`fesetenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fesetenv.html) | `int fesetenv(const fenv_t *);` | 函数 |
| [`fesetexceptflag`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fesetexceptflag.html) | `int fesetexceptflag(const fexcept_t *, int);` | 函数 |
| [`fesetround`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fesetround.html) | `int fesetround(int);` | 函数 |
| [`fetestexcept`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fetestexcept.html) | `int fetestexcept(int);` | 函数 |
| [`feupdateenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/feupdateenv.html) | `int feupdateenv(const fenv_t *);` | 函数 |

<a id="header-fmtmsg-h"></a>

### `<fmtmsg.h>`

格式化诊断消息；XSI 接口。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/fmtmsg.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`fmtmsg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmtmsg.html) | `int fmtmsg(long, const char *, int, const char *, const char *, const char *);` | 函数 |

<a id="header-fnmatch-h"></a>

### `<fnmatch.h>`

文件名通配模式匹配。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/fnmatch.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`fnmatch`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fnmatch.html) | `int fnmatch(const char *, const char *, int);` | 函数 |

<a id="header-ftw-h"></a>

### `<ftw.h>`

递归遍历文件树；XSI 接口。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/ftw.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`nftw`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nftw.html) | `int nftw(const char *, int (*)(const char *, const struct stat *, int, struct FTW *), int, int);` | 函数 |

<a id="header-glob-h"></a>

### `<glob.h>`

路径名通配展开及结果释放。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/glob.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`glob`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/glob.html) | `int glob(const char *restrict, int, int(*)(const char *, int), glob_t *restrict);` | 函数 |
| [`globfree`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/globfree.html) | `void globfree(glob_t *);` | 函数 |

<a id="header-grp-h"></a>

### `<grp.h>`

用户组数据库查询和遍历。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/grp.h.html)，本节共 7 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`endgrent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endgrent.html) | `void endgrent(void);` | 函数 |
| [`getgrent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgrent.html) | `struct group *getgrent(void);` | 函数 |
| [`getgrgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgrgid.html) | `struct group *getgrgid(gid_t);` | 函数 |
| [`getgrgid_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgrgid_r.html) | `int getgrgid_r(gid_t, struct group *, char *, size_t, struct group **);` | 函数 |
| [`getgrnam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgrnam.html) | `struct group *getgrnam(const char *);` | 函数 |
| [`getgrnam_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgrnam_r.html) | `int getgrnam_r(const char *, struct group *, char *, size_t , struct group **);` | 函数 |
| [`setgrent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setgrent.html) | `void setgrent(void);` | 函数 |

<a id="header-iconv-h"></a>

### `<iconv.h>`

字符编码转换及转换描述符管理。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/iconv.h.html)，本节共 3 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`iconv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iconv.html) | `size_t iconv(iconv_t, char **restrict, size_t *restrict, char **restrict, size_t *restrict);` | 函数 |
| [`iconv_close`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iconv_close.html) | `int iconv_close(iconv_t);` | 函数 |
| [`iconv_open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iconv_open.html) | `iconv_t iconv_open(const char *, const char *);` | 函数 |

<a id="header-inttypes-h"></a>

### `<inttypes.h>`

最大宽度整数的转换、绝对值、除法及格式宏。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/inttypes.h.html)，本节共 6 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`imaxabs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/imaxabs.html) | `intmax_t imaxabs(intmax_t);` | 函数 |
| [`imaxdiv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/imaxdiv.html) | `imaxdiv_t imaxdiv(intmax_t, intmax_t);` | 函数 |
| [`strtoimax`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtoimax.html) | `intmax_t strtoimax(const char *restrict, char **restrict, int);` | 函数 |
| [`strtoumax`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtoumax.html) | `uintmax_t strtoumax(const char *restrict, char **restrict, int);` | 函数 |
| [`wcstoimax`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstoimax.html) | `intmax_t wcstoimax(const wchar_t *restrict, wchar_t **restrict, int);` | 函数 |
| [`wcstoumax`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstoumax.html) | `uintmax_t wcstoumax(const wchar_t *restrict, wchar_t **restrict, int);` | 函数 |

<a id="header-langinfo-h"></a>

### `<langinfo.h>`

查询 locale 的语言、编码和格式信息。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/langinfo.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`nl_langinfo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nl_langinfo.html) | `char *nl_langinfo(nl_item);` | 函数 |
| [`nl_langinfo_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nl_langinfo_l.html) | `char *nl_langinfo_l(nl_item, locale_t);` | 函数 |

<a id="header-libgen-h"></a>

### `<libgen.h>`

提取路径的目录部分和文件名部分。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/libgen.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`basename`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/basename.html) | `char *basename(char *);` | 函数 |
| [`dirname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dirname.html) | `char *dirname(char *);` | 函数 |

<a id="header-libintl-h"></a>

### `<libintl.h>`

gettext 消息翻译、消息域及字符集设置。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/libintl.h.html)，本节共 15 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`bind_textdomain_codeset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/bind_textdomain_codeset.html) | `char *bind_textdomain_codeset(const char *, const char *);` | 函数 |
| [`bindtextdomain`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/bindtextdomain.html) | `char *bindtextdomain(const char *, const char *);` | 函数 |
| [`dcgettext`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dcgettext.html) | `char *dcgettext(const char *, const char *, int);` | 函数 |
| [`dcgettext_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dcgettext_l.html) | `char *dcgettext_l(const char *, const char *, int, locale_t);` | 函数 |
| [`dcngettext`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dcngettext.html) | `char *dcngettext(const char *, const char *, const char *, unsigned long int, int);` | 函数 |
| [`dcngettext_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dcngettext_l.html) | `char *dcngettext_l(const char *, const char *, const char *, unsigned long int, int, locale_t);` | 函数 |
| [`dgettext`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dgettext.html) | `char *dgettext(const char *, const char *);` | 函数 |
| [`dgettext_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dgettext_l.html) | `char *dgettext_l(const char *, const char *, locale_t);` | 函数 |
| [`dngettext`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dngettext.html) | `char *dngettext(const char *, const char *, const char *, unsigned long int);` | 函数 |
| [`dngettext_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dngettext_l.html) | `char *dngettext_l(const char *, const char *, const char *, unsigned long int, locale_t);` | 函数 |
| [`gettext`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gettext.html) | `char *gettext(const char *);` | 函数 |
| [`gettext_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gettext_l.html) | `char *gettext_l(const char *, locale_t);` | 函数 |
| [`ngettext`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ngettext.html) | `char *ngettext(const char *, const char *, unsigned long int);` | 函数 |
| [`ngettext_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ngettext_l.html) | `char *ngettext_l(const char *, const char *, unsigned long int, locale_t);` | 函数 |
| [`textdomain`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/textdomain.html) | `char *textdomain(const char *);` | 函数 |

<a id="header-locale-h"></a>

### `<locale.h>`

全局及线程 locale 对象的创建、设置和查询。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/locale.h.html)，本节共 7 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`duplocale`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/duplocale.html) | `locale_t duplocale(locale_t);` | 函数 |
| [`freelocale`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/freelocale.html) | `void freelocale(locale_t);` | 函数 |
| [`getlocalename_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getlocalename_l.html) | `const char *getlocalename_l(int, locale_t);` | 函数 |
| [`localeconv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/localeconv.html) | `struct lconv *localeconv(void);` | 函数 |
| [`newlocale`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/newlocale.html) | `locale_t newlocale(int, const char *, locale_t);` | 函数 |
| [`setlocale`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setlocale.html) | `char *setlocale(int, const char *);` | 函数 |
| [`uselocale`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/uselocale.html) | `locale_t uselocale (locale_t);` | 函数 |

<a id="header-math-h"></a>

### `<math.h>`

实数数学函数、浮点分类和比较宏；f/l 后缀版本分别面向 float/long double。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/math.h.html)，本节共 189 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`acos`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/acos.html) | `double acos(double);` | 函数 |
| [`acosf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/acosf.html) | `float acosf(float);` | 函数 |
| [`acosh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/acosh.html) | `double acosh(double);` | 函数 |
| [`acoshf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/acoshf.html) | `float acoshf(float);` | 函数 |
| [`acoshl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/acoshl.html) | `long double acoshl(long double);` | 函数 |
| [`acosl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/acosl.html) | `long double acosl(long double);` | 函数 |
| [`asin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asin.html) | `double asin(double);` | 函数 |
| [`asinf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asinf.html) | `float asinf(float);` | 函数 |
| [`asinh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asinh.html) | `double asinh(double);` | 函数 |
| [`asinhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asinhf.html) | `float asinhf(float);` | 函数 |
| [`asinhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asinhl.html) | `long double asinhl(long double);` | 函数 |
| [`asinl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asinl.html) | `long double asinl(long double);` | 函数 |
| [`atan`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atan.html) | `double atan(double);` | 函数 |
| [`atan2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atan2.html) | `double atan2(double, double);` | 函数 |
| [`atan2f`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atan2f.html) | `float atan2f(float, float);` | 函数 |
| [`atan2l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atan2l.html) | `long double atan2l(long double, long double);` | 函数 |
| [`atanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atanf.html) | `float atanf(float);` | 函数 |
| [`atanh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atanh.html) | `double atanh(double);` | 函数 |
| [`atanhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atanhf.html) | `float atanhf(float);` | 函数 |
| [`atanhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atanhl.html) | `long double atanhl(long double);` | 函数 |
| [`atanl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atanl.html) | `long double atanl(long double);` | 函数 |
| [`cbrt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cbrt.html) | `double cbrt(double);` | 函数 |
| [`cbrtf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cbrtf.html) | `float cbrtf(float);` | 函数 |
| [`cbrtl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cbrtl.html) | `long double cbrtl(long double);` | 函数 |
| [`ceil`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ceil.html) | `double ceil(double);` | 函数 |
| [`ceilf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ceilf.html) | `float ceilf(float);` | 函数 |
| [`ceill`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ceill.html) | `long double ceill(long double);` | 函数 |
| [`copysign`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/copysign.html) | `double copysign(double, double);` | 函数 |
| [`copysignf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/copysignf.html) | `float copysignf(float, float);` | 函数 |
| [`copysignl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/copysignl.html) | `long double copysignl(long double, long double);` | 函数 |
| [`cos`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cos.html) | `double cos(double);` | 函数 |
| [`cosf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cosf.html) | `float cosf(float);` | 函数 |
| [`cosh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cosh.html) | `double cosh(double);` | 函数 |
| [`coshf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/coshf.html) | `float coshf(float);` | 函数 |
| [`coshl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/coshl.html) | `long double coshl(long double);` | 函数 |
| [`cosl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cosl.html) | `long double cosl(long double);` | 函数 |
| [`erf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erf.html) | `double erf(double);` | 函数 |
| [`erfc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erfc.html) | `double erfc(double);` | 函数 |
| [`erfcf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erfcf.html) | `float erfcf(float);` | 函数 |
| [`erfcl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erfcl.html) | `long double erfcl(long double);` | 函数 |
| [`erff`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erff.html) | `float erff(float);` | 函数 |
| [`erfl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erfl.html) | `long double erfl(long double);` | 函数 |
| [`exp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exp.html) | `double exp(double);` | 函数 |
| [`exp2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exp2.html) | `double exp2(double);` | 函数 |
| [`exp2f`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exp2f.html) | `float exp2f(float);` | 函数 |
| [`exp2l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exp2l.html) | `long double exp2l(long double);` | 函数 |
| [`expf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/expf.html) | `float expf(float);` | 函数 |
| [`expl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/expl.html) | `long double expl(long double);` | 函数 |
| [`expm1`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/expm1.html) | `double expm1(double);` | 函数 |
| [`expm1f`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/expm1f.html) | `float expm1f(float);` | 函数 |
| [`expm1l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/expm1l.html) | `long double expm1l(long double);` | 函数 |
| [`fabs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fabs.html) | `double fabs(double);` | 函数 |
| [`fabsf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fabsf.html) | `float fabsf(float);` | 函数 |
| [`fabsl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fabsl.html) | `long double fabsl(long double);` | 函数 |
| [`fdim`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fdim.html) | `double fdim(double, double);` | 函数 |
| [`fdimf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fdimf.html) | `float fdimf(float, float);` | 函数 |
| [`fdiml`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fdiml.html) | `long double fdiml(long double, long double);` | 函数 |
| [`floor`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/floor.html) | `double floor(double);` | 函数 |
| [`floorf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/floorf.html) | `float floorf(float);` | 函数 |
| [`floorl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/floorl.html) | `long double floorl(long double);` | 函数 |
| [`fma`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fma.html) | `double fma(double, double, double);` | 函数 |
| [`fmaf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmaf.html) | `float fmaf(float, float, float);` | 函数 |
| [`fmal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmal.html) | `long double fmal(long double, long double, long double);` | 函数 |
| [`fmax`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmax.html) | `double fmax(double, double);` | 函数 |
| [`fmaxf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmaxf.html) | `float fmaxf(float, float);` | 函数 |
| [`fmaxl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmaxl.html) | `long double fmaxl(long double, long double);` | 函数 |
| [`fmin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmin.html) | `double fmin(double, double);` | 函数 |
| [`fminf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fminf.html) | `float fminf(float, float);` | 函数 |
| [`fminl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fminl.html) | `long double fminl(long double, long double);` | 函数 |
| [`fmod`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmod.html) | `double fmod(double, double);` | 函数 |
| [`fmodf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmodf.html) | `float fmodf(float, float);` | 函数 |
| [`fmodl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmodl.html) | `long double fmodl(long double, long double);` | 函数 |
| [`fpclassify`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fpclassify.html) | `int fpclassify(real-floating x);` | 宏 |
| [`frexp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/frexp.html) | `double frexp(double, int *);` | 函数 |
| [`frexpf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/frexpf.html) | `float frexpf(float, int *);` | 函数 |
| [`frexpl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/frexpl.html) | `long double frexpl(long double, int *);` | 函数 |
| [`hypot`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/hypot.html) | `double hypot(double, double);` | 函数 |
| [`hypotf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/hypotf.html) | `float hypotf(float, float);` | 函数 |
| [`hypotl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/hypotl.html) | `long double hypotl(long double, long double);` | 函数 |
| [`ilogb`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ilogb.html) | `int ilogb(double);` | 函数 |
| [`ilogbf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ilogbf.html) | `int ilogbf(float);` | 函数 |
| [`ilogbl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ilogbl.html) | `int ilogbl(long double);` | 函数 |
| [`isfinite`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isfinite.html) | `int isfinite(real-floating x);` | 宏 |
| [`isgreater`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isgreater.html) | `int isgreater(real-floating x, real-floating y);` | 宏 |
| [`isgreaterequal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isgreaterequal.html) | `int isgreaterequal(real-floating x, real-floating y);` | 宏 |
| [`isinf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isinf.html) | `int isinf(real-floating x);` | 宏 |
| [`isless`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isless.html) | `int isless(real-floating x, real-floating y);` | 宏 |
| [`islessequal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/islessequal.html) | `int islessequal(real-floating x, real-floating y);` | 宏 |
| [`islessgreater`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/islessgreater.html) | `int islessgreater(real-floating x, real-floating y);` | 宏 |
| [`isnan`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isnan.html) | `int isnan(real-floating x);` | 宏 |
| [`isnormal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isnormal.html) | `int isnormal(real-floating x);` | 宏 |
| [`isunordered`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isunordered.html) | `int isunordered(real-floating x, real-floating y);` | 宏 |
| [`j0`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/j0.html) | `double j0(double);` | 函数 |
| [`j1`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/j1.html) | `double j1(double);` | 函数 |
| [`jn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/jn.html) | `double jn(int, double);` | 函数 |
| [`ldexp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ldexp.html) | `double ldexp(double, int);` | 函数 |
| [`ldexpf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ldexpf.html) | `float ldexpf(float, int);` | 函数 |
| [`ldexpl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ldexpl.html) | `long double ldexpl(long double, int);` | 函数 |
| [`lgamma`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lgamma.html) | `double lgamma(double);` | 函数 |
| [`lgammaf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lgammaf.html) | `float lgammaf(float);` | 函数 |
| [`lgammal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lgammal.html) | `long double lgammal(long double);` | 函数 |
| [`llrint`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llrint.html) | `long long llrint(double);` | 函数 |
| [`llrintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llrintf.html) | `long long llrintf(float);` | 函数 |
| [`llrintl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llrintl.html) | `long long llrintl(long double);` | 函数 |
| [`llround`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llround.html) | `long long llround(double);` | 函数 |
| [`llroundf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llroundf.html) | `long long llroundf(float);` | 函数 |
| [`llroundl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llroundl.html) | `long long llroundl(long double);` | 函数 |
| [`log`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log.html) | `double log(double);` | 函数 |
| [`log10`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log10.html) | `double log10(double);` | 函数 |
| [`log10f`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log10f.html) | `float log10f(float);` | 函数 |
| [`log10l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log10l.html) | `long double log10l(long double);` | 函数 |
| [`log1p`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log1p.html) | `double log1p(double);` | 函数 |
| [`log1pf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log1pf.html) | `float log1pf(float);` | 函数 |
| [`log1pl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log1pl.html) | `long double log1pl(long double);` | 函数 |
| [`log2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log2.html) | `double log2(double);` | 函数 |
| [`log2f`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log2f.html) | `float log2f(float);` | 函数 |
| [`log2l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/log2l.html) | `long double log2l(long double);` | 函数 |
| [`logb`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/logb.html) | `double logb(double);` | 函数 |
| [`logbf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/logbf.html) | `float logbf(float);` | 函数 |
| [`logbl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/logbl.html) | `long double logbl(long double);` | 函数 |
| [`logf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/logf.html) | `float logf(float);` | 函数 |
| [`logl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/logl.html) | `long double logl(long double);` | 函数 |
| [`lrint`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lrint.html) | `long lrint(double);` | 函数 |
| [`lrintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lrintf.html) | `long lrintf(float);` | 函数 |
| [`lrintl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lrintl.html) | `long lrintl(long double);` | 函数 |
| [`lround`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lround.html) | `long lround(double);` | 函数 |
| [`lroundf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lroundf.html) | `long lroundf(float);` | 函数 |
| [`lroundl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lroundl.html) | `long lroundl(long double);` | 函数 |
| [`modf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/modf.html) | `double modf(double, double *);` | 函数 |
| [`modff`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/modff.html) | `float modff(float, float *);` | 函数 |
| [`modfl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/modfl.html) | `long double modfl(long double, long double *);` | 函数 |
| [`nan`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nan.html) | `double nan(const char *);` | 函数 |
| [`nanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nanf.html) | `float nanf(const char *);` | 函数 |
| [`nanl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nanl.html) | `long double nanl(const char *);` | 函数 |
| [`nearbyint`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nearbyint.html) | `double nearbyint(double);` | 函数 |
| [`nearbyintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nearbyintf.html) | `float nearbyintf(float);` | 函数 |
| [`nearbyintl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nearbyintl.html) | `long double nearbyintl(long double);` | 函数 |
| [`nextafter`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nextafter.html) | `double nextafter(double, double);` | 函数 |
| [`nextafterf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nextafterf.html) | `float nextafterf(float, float);` | 函数 |
| [`nextafterl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nextafterl.html) | `long double nextafterl(long double, long double);` | 函数 |
| [`nexttoward`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nexttoward.html) | `double nexttoward(double, long double);` | 函数 |
| [`nexttowardf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nexttowardf.html) | `float nexttowardf(float, long double);` | 函数 |
| [`nexttowardl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nexttowardl.html) | `long double nexttowardl(long double, long double);` | 函数 |
| [`pow`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pow.html) | `double pow(double, double);` | 函数 |
| [`powf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/powf.html) | `float powf(float, float);` | 函数 |
| [`powl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/powl.html) | `long double powl(long double, long double);` | 函数 |
| [`remainder`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remainder.html) | `double remainder(double, double);` | 函数 |
| [`remainderf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remainderf.html) | `float remainderf(float, float);` | 函数 |
| [`remainderl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remainderl.html) | `long double remainderl(long double, long double);` | 函数 |
| [`remquo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remquo.html) | `double remquo(double, double, int *);` | 函数 |
| [`remquof`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remquof.html) | `float remquof(float, float, int *);` | 函数 |
| [`remquol`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remquol.html) | `long double remquol(long double, long double, int *);` | 函数 |
| [`rint`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rint.html) | `double rint(double);` | 函数 |
| [`rintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rintf.html) | `float rintf(float);` | 函数 |
| [`rintl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rintl.html) | `long double rintl(long double);` | 函数 |
| [`round`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/round.html) | `double round(double);` | 函数 |
| [`roundf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/roundf.html) | `float roundf(float);` | 函数 |
| [`roundl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/roundl.html) | `long double roundl(long double);` | 函数 |
| [`scalbln`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scalbln.html) | `double scalbln(double, long);` | 函数 |
| [`scalblnf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scalblnf.html) | `float scalblnf(float, long);` | 函数 |
| [`scalblnl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scalblnl.html) | `long double scalblnl(long double, long);` | 函数 |
| [`scalbn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scalbn.html) | `double scalbn(double, int);` | 函数 |
| [`scalbnf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scalbnf.html) | `float scalbnf(float, int);` | 函数 |
| [`scalbnl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scalbnl.html) | `long double scalbnl(long double, int);` | 函数 |
| [`signbit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/signbit.html) | `int signbit(real-floating x);` | 宏 |
| [`sin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sin.html) | `double sin(double);` | 函数 |
| [`sinf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sinf.html) | `float sinf(float);` | 函数 |
| [`sinh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sinh.html) | `double sinh(double);` | 函数 |
| [`sinhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sinhf.html) | `float sinhf(float);` | 函数 |
| [`sinhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sinhl.html) | `long double sinhl(long double);` | 函数 |
| [`sinl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sinl.html) | `long double sinl(long double);` | 函数 |
| [`sqrt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sqrt.html) | `double sqrt(double);` | 函数 |
| [`sqrtf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sqrtf.html) | `float sqrtf(float);` | 函数 |
| [`sqrtl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sqrtl.html) | `long double sqrtl(long double);` | 函数 |
| [`tan`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tan.html) | `double tan(double);` | 函数 |
| [`tanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tanf.html) | `float tanf(float);` | 函数 |
| [`tanh`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tanh.html) | `double tanh(double);` | 函数 |
| [`tanhf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tanhf.html) | `float tanhf(float);` | 函数 |
| [`tanhl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tanhl.html) | `long double tanhl(long double);` | 函数 |
| [`tanl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tanl.html) | `long double tanl(long double);` | 函数 |
| [`tgamma`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tgamma.html) | `double tgamma(double);` | 函数 |
| [`tgammaf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tgammaf.html) | `float tgammaf(float);` | 函数 |
| [`tgammal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tgammal.html) | `long double tgammal(long double);` | 函数 |
| [`trunc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/trunc.html) | `double trunc(double);` | 函数 |
| [`truncf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/truncf.html) | `float truncf(float);` | 函数 |
| [`truncl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/truncl.html) | `long double truncl(long double);` | 函数 |
| [`y0`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/y0.html) | `double y0(double);` | 函数 |
| [`y1`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/y1.html) | `double y1(double);` | 函数 |
| [`yn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/yn.html) | `double yn(int, double);` | 函数 |

<a id="header-monetary-h"></a>

### `<monetary.h>`

按 locale 格式化货币金额。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/monetary.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`strfmon`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strfmon.html) | `ssize_t strfmon(char *restrict, size_t, const char *restrict, ...);` | 函数 |
| [`strfmon_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strfmon_l.html) | `ssize_t strfmon_l(char *restrict, size_t, locale_t, const char *restrict, ...);` | 函数 |

<a id="header-mqueue-h"></a>

### `<mqueue.h>`

POSIX 消息队列及消息通知、定时收发。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/mqueue.h.html)，本节共 10 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`mq_close`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_close.html) | `int mq_close(mqd_t);` | 函数 |
| [`mq_getattr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_getattr.html) | `int mq_getattr(mqd_t, struct mq_attr *);` | 函数 |
| [`mq_notify`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_notify.html) | `int mq_notify(mqd_t, const struct sigevent *);` | 函数 |
| [`mq_open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_open.html) | `mqd_t mq_open(const char *, int, ...);` | 函数 |
| [`mq_receive`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_receive.html) | `ssize_t mq_receive(mqd_t, char *, size_t, unsigned *);` | 函数 |
| [`mq_send`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_send.html) | `int mq_send(mqd_t, const char *, size_t, unsigned);` | 函数 |
| [`mq_setattr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_setattr.html) | `int mq_setattr(mqd_t, const struct mq_attr *restrict, struct mq_attr *restrict);` | 函数 |
| [`mq_timedreceive`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_timedreceive.html) | `ssize_t mq_timedreceive(mqd_t, char *restrict, size_t, unsigned *restrict, const struct timespec *restrict);` | 函数 |
| [`mq_timedsend`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_timedsend.html) | `int mq_timedsend(mqd_t, const char *, size_t, unsigned, const struct timespec *);` | 函数 |
| [`mq_unlink`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mq_unlink.html) | `int mq_unlink(const char *);` | 函数 |

<a id="header-ndbm-h"></a>

### `<ndbm.h>`

键值数据库的打开、查询、遍历和更新；XSI 接口。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/ndbm.h.html)，本节共 9 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`dbm_clearerr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_clearerr.html) | `int dbm_clearerr(DBM *);` | 函数 |
| [`dbm_close`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_close.html) | `void dbm_close(DBM *);` | 函数 |
| [`dbm_delete`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_delete.html) | `int dbm_delete(DBM *, datum);` | 函数 |
| [`dbm_error`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_error.html) | `int dbm_error(DBM *);` | 函数 |
| [`dbm_fetch`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_fetch.html) | `datum dbm_fetch(DBM *, datum);` | 函数 |
| [`dbm_firstkey`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_firstkey.html) | `datum dbm_firstkey(DBM *);` | 函数 |
| [`dbm_nextkey`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_nextkey.html) | `datum dbm_nextkey(DBM *);` | 函数 |
| [`dbm_open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_open.html) | `DBM *dbm_open(const char *, int, mode_t);` | 函数 |
| [`dbm_store`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dbm_store.html) | `int dbm_store(DBM *, datum, datum, int);` | 函数 |

<a id="header-net-if-h"></a>

### `<net/if.h>`

网络接口名称与索引的转换和枚举。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/net_if.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`if_freenameindex`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/if_freenameindex.html) | `void if_freenameindex(struct if_nameindex *);` | 函数 |
| [`if_indextoname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/if_indextoname.html) | `char *if_indextoname(unsigned, char *);` | 函数 |
| [`if_nameindex`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/if_nameindex.html) | `struct if_nameindex *if_nameindex(void);` | 函数 |
| [`if_nametoindex`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/if_nametoindex.html) | `unsigned if_nametoindex(const char *);` | 函数 |

<a id="header-netdb-h"></a>

### `<netdb.h>`

主机、地址、网络、协议和服务数据库。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/netdb.h.html)，本节共 22 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`endhostent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endhostent.html) | `void endhostent(void);` | 函数 |
| [`endnetent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endnetent.html) | `void endnetent(void);` | 函数 |
| [`endprotoent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endprotoent.html) | `void endprotoent(void);` | 函数 |
| [`endservent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endservent.html) | `void endservent(void);` | 函数 |
| [`freeaddrinfo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/freeaddrinfo.html) | `void freeaddrinfo(struct addrinfo *);` | 函数 |
| [`gai_strerror`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gai_strerror.html) | `const char *gai_strerror(int);` | 函数 |
| [`getaddrinfo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getaddrinfo.html) | `int getaddrinfo(const char *restrict, const char *restrict, const struct addrinfo *restrict, struct addrinfo **restrict);` | 函数 |
| [`gethostent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gethostent.html) | `struct hostent *gethostent(void);` | 函数 |
| [`getnameinfo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getnameinfo.html) | `int getnameinfo(const struct sockaddr *restrict, socklen_t, char *restrict, socklen_t, char *restrict, socklen_t, int);` | 函数 |
| [`getnetbyaddr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getnetbyaddr.html) | `struct netent *getnetbyaddr(uint32_t, int);` | 函数 |
| [`getnetbyname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getnetbyname.html) | `struct netent *getnetbyname(const char *);` | 函数 |
| [`getnetent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getnetent.html) | `struct netent *getnetent(void);` | 函数 |
| [`getprotobyname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getprotobyname.html) | `struct protoent *getprotobyname(const char *);` | 函数 |
| [`getprotobynumber`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getprotobynumber.html) | `struct protoent *getprotobynumber(int);` | 函数 |
| [`getprotoent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getprotoent.html) | `struct protoent *getprotoent(void);` | 函数 |
| [`getservbyname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getservbyname.html) | `struct servent *getservbyname(const char *, const char *);` | 函数 |
| [`getservbyport`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getservbyport.html) | `struct servent *getservbyport(int, const char *);` | 函数 |
| [`getservent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getservent.html) | `struct servent *getservent(void);` | 函数 |
| [`sethostent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sethostent.html) | `void sethostent(int);` | 函数 |
| [`setnetent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setnetent.html) | `void setnetent(int);` | 函数 |
| [`setprotoent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setprotoent.html) | `void setprotoent(int);` | 函数 |
| [`setservent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setservent.html) | `void setservent(int);` | 函数 |

<a id="header-nl_types-h"></a>

### `<nl_types.h>`

消息目录的打开、读取和关闭。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/nl_types.h.html)，本节共 3 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`catclose`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catclose.html) | `int catclose(nl_catd);` | 函数 |
| [`catgets`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catgets.html) | `char *catgets(nl_catd, int, int, const char *);` | 函数 |
| [`catopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/catopen.html) | `nl_catd catopen(const char *, int);` | 函数 |

<a id="header-poll-h"></a>

### `<poll.h>`

等待多个文件描述符上的 I/O 事件。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/poll.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`poll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html) | `int poll(struct pollfd [], nfds_t, int);` | 函数 |
| [`ppoll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ppoll.html) | `int ppoll(struct pollfd [], nfds_t, const struct timespec *restrict, const sigset_t *restrict);` | 函数 |

<a id="header-pthread-h"></a>

### `<pthread.h>`

POSIX 线程、线程属性、取消、线程局部存储及同步原语。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/pthread.h.html)，本节共 102 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`pthread_atfork`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_atfork.html) | `int pthread_atfork(void (*)(void), void (*)(void), void(*)(void));` | 函数 |
| [`pthread_attr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_destroy.html) | `int pthread_attr_destroy(pthread_attr_t *);` | 函数 |
| [`pthread_attr_getdetachstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getdetachstate.html) | `int pthread_attr_getdetachstate(const pthread_attr_t *, int *);` | 函数 |
| [`pthread_attr_getguardsize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getguardsize.html) | `int pthread_attr_getguardsize(const pthread_attr_t *restrict, size_t *restrict);` | 函数 |
| [`pthread_attr_getinheritsched`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getinheritsched.html) | `int pthread_attr_getinheritsched(const pthread_attr_t *restrict, int *restrict);` | 函数 |
| [`pthread_attr_getschedparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getschedparam.html) | `int pthread_attr_getschedparam(const pthread_attr_t *restrict, struct sched_param *restrict);` | 函数 |
| [`pthread_attr_getschedpolicy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getschedpolicy.html) | `int pthread_attr_getschedpolicy(const pthread_attr_t *restrict, int *restrict);` | 函数 |
| [`pthread_attr_getscope`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getscope.html) | `int pthread_attr_getscope(const pthread_attr_t *restrict, int *restrict);` | 函数 |
| [`pthread_attr_getstack`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getstack.html) | `int pthread_attr_getstack(const pthread_attr_t *restrict, void **restrict, size_t *restrict);` | 函数 |
| [`pthread_attr_getstacksize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_getstacksize.html) | `int pthread_attr_getstacksize(const pthread_attr_t *restrict, size_t *restrict);` | 函数 |
| [`pthread_attr_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_init.html) | `int pthread_attr_init(pthread_attr_t *);` | 函数 |
| [`pthread_attr_setdetachstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setdetachstate.html) | `int pthread_attr_setdetachstate(pthread_attr_t *, int);` | 函数 |
| [`pthread_attr_setguardsize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setguardsize.html) | `int pthread_attr_setguardsize(pthread_attr_t *, size_t);` | 函数 |
| [`pthread_attr_setinheritsched`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setinheritsched.html) | `int pthread_attr_setinheritsched(pthread_attr_t *, int);` | 函数 |
| [`pthread_attr_setschedparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setschedparam.html) | `int pthread_attr_setschedparam(pthread_attr_t *restrict, const struct sched_param *restrict);` | 函数 |
| [`pthread_attr_setschedpolicy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setschedpolicy.html) | `int pthread_attr_setschedpolicy(pthread_attr_t *, int);` | 函数 |
| [`pthread_attr_setscope`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setscope.html) | `int pthread_attr_setscope(pthread_attr_t *, int);` | 函数 |
| [`pthread_attr_setstack`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setstack.html) | `int pthread_attr_setstack(pthread_attr_t *, void *, size_t);` | 函数 |
| [`pthread_attr_setstacksize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_attr_setstacksize.html) | `int pthread_attr_setstacksize(pthread_attr_t *, size_t);` | 函数 |
| [`pthread_barrier_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrier_destroy.html) | `int pthread_barrier_destroy(pthread_barrier_t *);` | 函数 |
| [`pthread_barrier_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrier_init.html) | `int pthread_barrier_init(pthread_barrier_t *restrict, const pthread_barrierattr_t *restrict, unsigned);` | 函数 |
| [`pthread_barrier_wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrier_wait.html) | `int pthread_barrier_wait(pthread_barrier_t *);` | 函数 |
| [`pthread_barrierattr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrierattr_destroy.html) | `int pthread_barrierattr_destroy(pthread_barrierattr_t *);` | 函数 |
| [`pthread_barrierattr_getpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrierattr_getpshared.html) | `int pthread_barrierattr_getpshared( const pthread_barrierattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_barrierattr_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrierattr_init.html) | `int pthread_barrierattr_init(pthread_barrierattr_t *);` | 函数 |
| [`pthread_barrierattr_setpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_barrierattr_setpshared.html) | `int pthread_barrierattr_setpshared(pthread_barrierattr_t *, int);` | 函数 |
| [`pthread_cancel`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cancel.html) | `int pthread_cancel(pthread_t);` | 函数 |
| [`pthread_cleanup_pop`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cleanup_pop.html) | `void pthread_cleanup_pop(int execute);` | 特殊接口 |
| [`pthread_cleanup_push`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cleanup_push.html) | `void pthread_cleanup_push(void (*routine)(void*), void *arg);` | 特殊接口 |
| [`pthread_cond_broadcast`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_broadcast.html) | `int pthread_cond_broadcast(pthread_cond_t *);` | 函数 |
| [`pthread_cond_clockwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_clockwait.html) | `int pthread_cond_clockwait(pthread_cond_t *restrict, pthread_mutex_t *restrict, clockid_t, const struct timespec *restrict);` | 函数 |
| [`pthread_cond_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_destroy.html) | `int pthread_cond_destroy(pthread_cond_t *);` | 函数 |
| [`pthread_cond_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_init.html) | `int pthread_cond_init(pthread_cond_t *restrict, const pthread_condattr_t *restrict);` | 函数 |
| [`pthread_cond_signal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_signal.html) | `int pthread_cond_signal(pthread_cond_t *);` | 函数 |
| [`pthread_cond_timedwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_timedwait.html) | `int pthread_cond_timedwait(pthread_cond_t *restrict, pthread_mutex_t *restrict, const struct timespec *restrict);` | 函数 |
| [`pthread_cond_wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_wait.html) | `int pthread_cond_wait(pthread_cond_t *restrict, pthread_mutex_t *restrict);` | 函数 |
| [`pthread_condattr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_destroy.html) | `int pthread_condattr_destroy(pthread_condattr_t *);` | 函数 |
| [`pthread_condattr_getclock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_getclock.html) | `int pthread_condattr_getclock(const pthread_condattr_t *restrict, clockid_t *restrict);` | 函数 |
| [`pthread_condattr_getpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_getpshared.html) | `int pthread_condattr_getpshared(const pthread_condattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_condattr_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_init.html) | `int pthread_condattr_init(pthread_condattr_t *);` | 函数 |
| [`pthread_condattr_setclock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_setclock.html) | `int pthread_condattr_setclock(pthread_condattr_t *, clockid_t);` | 函数 |
| [`pthread_condattr_setpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_setpshared.html) | `int pthread_condattr_setpshared(pthread_condattr_t *, int);` | 函数 |
| [`pthread_create`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_create.html) | `int pthread_create(pthread_t *restrict, const pthread_attr_t *restrict, void *(*)(void*), void *restrict);` | 函数 |
| [`pthread_detach`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_detach.html) | `int pthread_detach(pthread_t);` | 函数 |
| [`pthread_equal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_equal.html) | `int pthread_equal(pthread_t, pthread_t);` | 函数 |
| [`pthread_exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_exit.html) | `_Noreturn void pthread_exit(void *);` | 函数 |
| [`pthread_getcpuclockid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_getcpuclockid.html) | `int pthread_getcpuclockid(pthread_t, clockid_t *);` | 函数 |
| [`pthread_getschedparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_getschedparam.html) | `int pthread_getschedparam(pthread_t, int *restrict, struct sched_param *restrict);` | 函数 |
| [`pthread_getspecific`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_getspecific.html) | `void *pthread_getspecific(pthread_key_t);` | 函数 |
| [`pthread_join`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_join.html) | `int pthread_join(pthread_t, void **);` | 函数 |
| [`pthread_key_create`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_key_create.html) | `int pthread_key_create(pthread_key_t *, void (*)(void*));` | 函数 |
| [`pthread_key_delete`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_key_delete.html) | `int pthread_key_delete(pthread_key_t);` | 函数 |
| [`pthread_mutex_clocklock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_clocklock.html) | `int pthread_mutex_clocklock(pthread_mutex_t *restrict, clockid_t, const struct timespec *restrict);` | 函数 |
| [`pthread_mutex_consistent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_consistent.html) | `int pthread_mutex_consistent(pthread_mutex_t *);` | 函数 |
| [`pthread_mutex_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_destroy.html) | `int pthread_mutex_destroy(pthread_mutex_t *);` | 函数 |
| [`pthread_mutex_getprioceiling`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_getprioceiling.html) | `int pthread_mutex_getprioceiling(const pthread_mutex_t *restrict, int *restrict);` | 函数 |
| [`pthread_mutex_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_init.html) | `int pthread_mutex_init(pthread_mutex_t *restrict, const pthread_mutexattr_t *restrict);` | 函数 |
| [`pthread_mutex_lock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_lock.html) | `int pthread_mutex_lock(pthread_mutex_t *);` | 函数 |
| [`pthread_mutex_setprioceiling`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_setprioceiling.html) | `int pthread_mutex_setprioceiling(pthread_mutex_t *restrict, int, int *restrict);` | 函数 |
| [`pthread_mutex_timedlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_timedlock.html) | `int pthread_mutex_timedlock(pthread_mutex_t *restrict, const struct timespec *restrict);` | 函数 |
| [`pthread_mutex_trylock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_trylock.html) | `int pthread_mutex_trylock(pthread_mutex_t *);` | 函数 |
| [`pthread_mutex_unlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_unlock.html) | `int pthread_mutex_unlock(pthread_mutex_t *);` | 函数 |
| [`pthread_mutexattr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_destroy.html) | `int pthread_mutexattr_destroy(pthread_mutexattr_t *);` | 函数 |
| [`pthread_mutexattr_getprioceiling`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_getprioceiling.html) | `int pthread_mutexattr_getprioceiling( const pthread_mutexattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_mutexattr_getprotocol`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_getprotocol.html) | `int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_mutexattr_getpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_getpshared.html) | `int pthread_mutexattr_getpshared(const pthread_mutexattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_mutexattr_getrobust`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_getrobust.html) | `int pthread_mutexattr_getrobust(const pthread_mutexattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_mutexattr_gettype`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_gettype.html) | `int pthread_mutexattr_gettype(const pthread_mutexattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_mutexattr_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_init.html) | `int pthread_mutexattr_init(pthread_mutexattr_t *);` | 函数 |
| [`pthread_mutexattr_setprioceiling`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_setprioceiling.html) | `int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *, int);` | 函数 |
| [`pthread_mutexattr_setprotocol`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_setprotocol.html) | `int pthread_mutexattr_setprotocol(pthread_mutexattr_t *, int);` | 函数 |
| [`pthread_mutexattr_setpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_setpshared.html) | `int pthread_mutexattr_setpshared(pthread_mutexattr_t *, int);` | 函数 |
| [`pthread_mutexattr_setrobust`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_setrobust.html) | `int pthread_mutexattr_setrobust(pthread_mutexattr_t *, int);` | 函数 |
| [`pthread_mutexattr_settype`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutexattr_settype.html) | `int pthread_mutexattr_settype(pthread_mutexattr_t *, int);` | 函数 |
| [`pthread_once`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_once.html) | `int pthread_once(pthread_once_t *, void (*)(void));` | 函数 |
| [`pthread_rwlock_clockrdlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_clockrdlock.html) | `int pthread_rwlock_clockrdlock(pthread_rwlock_t *restrict, clockid_t, const struct timespec *restrict);` | 函数 |
| [`pthread_rwlock_clockwrlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_clockwrlock.html) | `int pthread_rwlock_clockwrlock(pthread_rwlock_t *restrict, clockid_t, const struct timespec *restrict);` | 函数 |
| [`pthread_rwlock_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_destroy.html) | `int pthread_rwlock_destroy(pthread_rwlock_t *);` | 函数 |
| [`pthread_rwlock_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_init.html) | `int pthread_rwlock_init(pthread_rwlock_t *restrict, const pthread_rwlockattr_t *restrict);` | 函数 |
| [`pthread_rwlock_rdlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_rdlock.html) | `int pthread_rwlock_rdlock(pthread_rwlock_t *);` | 函数 |
| [`pthread_rwlock_timedrdlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_timedrdlock.html) | `int pthread_rwlock_timedrdlock(pthread_rwlock_t *restrict, const struct timespec *restrict);` | 函数 |
| [`pthread_rwlock_timedwrlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_timedwrlock.html) | `int pthread_rwlock_timedwrlock(pthread_rwlock_t *restrict, const struct timespec *restrict);` | 函数 |
| [`pthread_rwlock_tryrdlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_tryrdlock.html) | `int pthread_rwlock_tryrdlock(pthread_rwlock_t *);` | 函数 |
| [`pthread_rwlock_trywrlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_trywrlock.html) | `int pthread_rwlock_trywrlock(pthread_rwlock_t *);` | 函数 |
| [`pthread_rwlock_unlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_unlock.html) | `int pthread_rwlock_unlock(pthread_rwlock_t *);` | 函数 |
| [`pthread_rwlock_wrlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlock_wrlock.html) | `int pthread_rwlock_wrlock(pthread_rwlock_t *);` | 函数 |
| [`pthread_rwlockattr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlockattr_destroy.html) | `int pthread_rwlockattr_destroy(pthread_rwlockattr_t *);` | 函数 |
| [`pthread_rwlockattr_getpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlockattr_getpshared.html) | `int pthread_rwlockattr_getpshared( const pthread_rwlockattr_t *restrict, int *restrict);` | 函数 |
| [`pthread_rwlockattr_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlockattr_init.html) | `int pthread_rwlockattr_init(pthread_rwlockattr_t *);` | 函数 |
| [`pthread_rwlockattr_setpshared`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_rwlockattr_setpshared.html) | `int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *, int);` | 函数 |
| [`pthread_self`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_self.html) | `pthread_t pthread_self(void);` | 函数 |
| [`pthread_setcancelstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_setcancelstate.html) | `int pthread_setcancelstate(int, int *);` | 函数 |
| [`pthread_setcanceltype`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_setcanceltype.html) | `int pthread_setcanceltype(int, int *);` | 函数 |
| [`pthread_setschedparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_setschedparam.html) | `int pthread_setschedparam(pthread_t, int, const struct sched_param *);` | 函数 |
| [`pthread_setschedprio`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_setschedprio.html) | `int pthread_setschedprio(pthread_t, int);` | 函数 |
| [`pthread_setspecific`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_setspecific.html) | `int pthread_setspecific(pthread_key_t, const void *);` | 函数 |
| [`pthread_spin_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_spin_destroy.html) | `int pthread_spin_destroy(pthread_spinlock_t *);` | 函数 |
| [`pthread_spin_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_spin_init.html) | `int pthread_spin_init(pthread_spinlock_t *, int);` | 函数 |
| [`pthread_spin_lock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_spin_lock.html) | `int pthread_spin_lock(pthread_spinlock_t *);` | 函数 |
| [`pthread_spin_trylock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_spin_trylock.html) | `int pthread_spin_trylock(pthread_spinlock_t *);` | 函数 |
| [`pthread_spin_unlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_spin_unlock.html) | `int pthread_spin_unlock(pthread_spinlock_t *);` | 函数 |
| [`pthread_testcancel`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_testcancel.html) | `void pthread_testcancel(void);` | 函数 |

<a id="header-pwd-h"></a>

### `<pwd.h>`

用户账户数据库查询和遍历。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/pwd.h.html)，本节共 7 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`endpwent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endpwent.html) | `void endpwent(void);` | 函数 |
| [`getpwent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpwent.html) | `struct passwd *getpwent(void);` | 函数 |
| [`getpwnam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpwnam.html) | `struct passwd *getpwnam(const char *);` | 函数 |
| [`getpwnam_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpwnam_r.html) | `int getpwnam_r(const char *, struct passwd *, char *, size_t, struct passwd **);` | 函数 |
| [`getpwuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpwuid.html) | `struct passwd *getpwuid(uid_t);` | 函数 |
| [`getpwuid_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpwuid_r.html) | `int getpwuid_r(uid_t, struct passwd *, char *, size_t, struct passwd **);` | 函数 |
| [`setpwent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setpwent.html) | `void setpwent(void);` | 函数 |

<a id="header-regex-h"></a>

### `<regex.h>`

正则表达式的编译、匹配、错误格式化和释放。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/regex.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`regcomp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/regcomp.html) | `int regcomp(regex_t *restrict, const char *restrict, int);` | 函数 |
| [`regerror`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/regerror.html) | `size_t regerror(int, const regex_t *restrict, char *restrict, size_t);` | 函数 |
| [`regexec`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/regexec.html) | `int regexec(const regex_t *restrict, const char *restrict, size_t, regmatch_t [restrict], int);` | 函数 |
| [`regfree`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/regfree.html) | `void regfree(regex_t *);` | 函数 |

<a id="header-sched-h"></a>

### `<sched.h>`

进程调度策略、优先级和主动让出处理器。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sched.h.html)，本节共 8 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`sched_get_priority_max`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_get_priority_max.html) | `int sched_get_priority_max(int);` | 函数 |
| [`sched_get_priority_min`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_get_priority_min.html) | `int sched_get_priority_min(int);` | 函数 |
| [`sched_getparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_getparam.html) | `int sched_getparam(pid_t, struct sched_param *);` | 函数 |
| [`sched_getscheduler`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_getscheduler.html) | `int sched_getscheduler(pid_t);` | 函数 |
| [`sched_rr_get_interval`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_rr_get_interval.html) | `int sched_rr_get_interval(pid_t, struct timespec *);` | 函数 |
| [`sched_setparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_setparam.html) | `int sched_setparam(pid_t, const struct sched_param *);` | 函数 |
| [`sched_setscheduler`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_setscheduler.html) | `int sched_setscheduler(pid_t, int, const struct sched_param *);` | 函数 |
| [`sched_yield`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sched_yield.html) | `int sched_yield(void);` | 函数 |

<a id="header-search-h"></a>

### `<search.h>`

哈希表、线性表、链式队列及二叉树搜索。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/search.h.html)，本节共 11 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`hcreate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/hcreate.html) | `int hcreate(size_t);` | 函数 |
| [`hdestroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/hdestroy.html) | `void hdestroy(void);` | 函数 |
| [`hsearch`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/hsearch.html) | `ENTRY *hsearch(ENTRY, ACTION);` | 函数 |
| [`insque`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/insque.html) | `void insque(void *, void *);` | 函数 |
| [`lfind`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lfind.html) | `void *lfind(const void *, const void *, size_t *, size_t, int (*)(const void *, const void *));` | 函数 |
| [`lsearch`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lsearch.html) | `void *lsearch(const void *, void *, size_t *, size_t, int (*)(const void *, const void *));` | 函数 |
| [`remque`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remque.html) | `void remque(void *);` | 函数 |
| [`tdelete`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tdelete.html) | `void *tdelete(const void *restrict, posix_tnode **restrict, int(*)(const void *, const void *));` | 函数 |
| [`tfind`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tfind.html) | `posix_tnode *tfind(const void *, posix_tnode *const *, int(*)(const void *, const void *));` | 函数 |
| [`tsearch`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tsearch.html) | `posix_tnode *tsearch(const void *, posix_tnode **, int(*)(const void *, const void *));` | 函数 |
| [`twalk`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/twalk.html) | `void twalk(const posix_tnode *, void (*)(const posix_tnode *, VISIT, int));` | 函数 |

<a id="header-semaphore-h"></a>

### `<semaphore.h>`

命名和匿名信号量及定时等待。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/semaphore.h.html)，本节共 11 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`sem_clockwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_clockwait.html) | `int sem_clockwait(sem_t *restrict, clockid_t, const struct timespec *restrict);` | 函数 |
| [`sem_close`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_close.html) | `int sem_close(sem_t *);` | 函数 |
| [`sem_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_destroy.html) | `int sem_destroy(sem_t *);` | 函数 |
| [`sem_getvalue`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_getvalue.html) | `int sem_getvalue(sem_t *restrict, int *restrict);` | 函数 |
| [`sem_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_init.html) | `int sem_init(sem_t *, int, unsigned);` | 函数 |
| [`sem_open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_open.html) | `sem_t *sem_open(const char *, int, ...);` | 函数 |
| [`sem_post`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_post.html) | `int sem_post(sem_t *);` | 函数 |
| [`sem_timedwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_timedwait.html) | `int sem_timedwait(sem_t *restrict, const struct timespec *restrict);` | 函数 |
| [`sem_trywait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_trywait.html) | `int sem_trywait(sem_t *);` | 函数 |
| [`sem_unlink`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_unlink.html) | `int sem_unlink(const char *);` | 函数 |
| [`sem_wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sem_wait.html) | `int sem_wait(sem_t *);` | 函数 |

<a id="header-setjmp-h"></a>

### `<setjmp.h>`

保存和恢复非局部跳转环境，包括信号屏蔽字版本。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/setjmp.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`longjmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/longjmp.html) | `_Noreturn void longjmp(jmp_buf, int);` | 函数 |
| [`setjmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setjmp.html) | `int setjmp(jmp_buf);` | 宏 |
| [`siglongjmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/siglongjmp.html) | `_Noreturn void siglongjmp(sigjmp_buf, int);` | 函数 |
| [`sigsetjmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigsetjmp.html) | `int sigsetjmp(sigjmp_buf, int);` | 特殊接口 |

<a id="header-signal-h"></a>

### `<signal.h>`

信号处理、信号集、信号等待及线程信号操作。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/signal.h.html)，本节共 24 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`kill`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/kill.html) | `int kill(pid_t, int);` | 函数 |
| [`killpg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/killpg.html) | `int killpg(pid_t, int);` | 函数 |
| [`psiginfo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/psiginfo.html) | `void psiginfo(const siginfo_t *, const char *);` | 函数 |
| [`psignal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/psignal.html) | `void psignal(int, const char *);` | 函数 |
| [`pthread_kill`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_kill.html) | `int pthread_kill(pthread_t, int);` | 函数 |
| [`pthread_sigmask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_sigmask.html) | `int pthread_sigmask(int, const sigset_t *restrict, sigset_t *restrict);` | 函数 |
| [`raise`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/raise.html) | `int raise(int);` | 函数 |
| [`sig2str`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sig2str.html) | `int sig2str(int, char *);` | 函数 |
| [`sigaction`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html) | `int sigaction(int, const struct sigaction *restrict, struct sigaction *restrict);` | 函数 |
| [`sigaddset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaddset.html) | `int sigaddset(sigset_t *, int);` | 函数 |
| [`sigaltstack`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaltstack.html) | `int sigaltstack(const stack_t *restrict, stack_t *restrict);` | 函数 |
| [`sigdelset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigdelset.html) | `int sigdelset(sigset_t *, int);` | 函数 |
| [`sigemptyset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigemptyset.html) | `int sigemptyset(sigset_t *);` | 函数 |
| [`sigfillset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigfillset.html) | `int sigfillset(sigset_t *);` | 函数 |
| [`sigismember`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigismember.html) | `int sigismember(const sigset_t *, int);` | 函数 |
| [`signal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/signal.html) | `void (*signal(int, void (*)(int)))(int);` | 函数 |
| [`sigpending`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigpending.html) | `int sigpending(sigset_t *);` | 函数 |
| [`sigprocmask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigprocmask.html) | `int sigprocmask(int, const sigset_t *restrict, sigset_t *restrict);` | 函数 |
| [`sigqueue`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigqueue.html) | `int sigqueue(pid_t, int, union sigval);` | 函数 |
| [`sigsuspend`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigsuspend.html) | `int sigsuspend(const sigset_t *);` | 函数 |
| [`sigtimedwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigtimedwait.html) | `int sigtimedwait(const sigset_t *restrict, siginfo_t *restrict, const struct timespec *restrict);` | 函数 |
| [`sigwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigwait.html) | `int sigwait(const sigset_t *restrict, int *restrict);` | 函数 |
| [`sigwaitinfo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigwaitinfo.html) | `int sigwaitinfo(const sigset_t *restrict, siginfo_t *restrict);` | 函数 |
| [`str2sig`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/str2sig.html) | `int str2sig(const char *restrict, int *restrict);` | 函数 |

<a id="header-spawn-h"></a>

### `<spawn.h>`

创建进程以及配置创建属性和文件动作。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/spawn.h.html)，本节共 23 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`posix_spawn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html) | `int posix_spawn(pid_t *restrict, const char *restrict, const posix_spawn_file_actions_t *restrict, const posix_spawnattr_t *restrict, char *const [restrict], char *const [restrict]);` | 函数 |
| [`posix_spawn_file_actions_addchdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_addchdir.html) | `int posix_spawn_file_actions_addchdir(posix_spawn_file_actions_t *restrict, const char *restrict);` | 函数 |
| [`posix_spawn_file_actions_addclose`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_addclose.html) | `int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *, int);` | 函数 |
| [`posix_spawn_file_actions_adddup2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_adddup2.html) | `int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *, int, int);` | 函数 |
| [`posix_spawn_file_actions_addfchdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_addfchdir.html) | `int posix_spawn_file_actions_addfchdir(posix_spawn_file_actions_t *, int);` | 函数 |
| [`posix_spawn_file_actions_addopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_addopen.html) | `int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *restrict, int, const char *restrict, int, mode_t);` | 函数 |
| [`posix_spawn_file_actions_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_destroy.html) | `int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *);` | 函数 |
| [`posix_spawn_file_actions_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn_file_actions_init.html) | `int posix_spawn_file_actions_init(posix_spawn_file_actions_t *);` | 函数 |
| [`posix_spawnattr_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_destroy.html) | `int posix_spawnattr_destroy(posix_spawnattr_t *);` | 函数 |
| [`posix_spawnattr_getflags`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_getflags.html) | `int posix_spawnattr_getflags(const posix_spawnattr_t *restrict, short *restrict);` | 函数 |
| [`posix_spawnattr_getpgroup`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_getpgroup.html) | `int posix_spawnattr_getpgroup(const posix_spawnattr_t *restrict, pid_t *restrict);` | 函数 |
| [`posix_spawnattr_getschedparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_getschedparam.html) | `int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict, struct sched_param *restrict);` | 函数 |
| [`posix_spawnattr_getschedpolicy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_getschedpolicy.html) | `int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict, int *restrict);` | 函数 |
| [`posix_spawnattr_getsigdefault`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_getsigdefault.html) | `int posix_spawnattr_getsigdefault(const posix_spawnattr_t *restrict, sigset_t *restrict);` | 函数 |
| [`posix_spawnattr_getsigmask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_getsigmask.html) | `int posix_spawnattr_getsigmask(const posix_spawnattr_t *restrict, sigset_t *restrict);` | 函数 |
| [`posix_spawnattr_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_init.html) | `int posix_spawnattr_init(posix_spawnattr_t *);` | 函数 |
| [`posix_spawnattr_setflags`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_setflags.html) | `int posix_spawnattr_setflags(posix_spawnattr_t *, short);` | 函数 |
| [`posix_spawnattr_setpgroup`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_setpgroup.html) | `int posix_spawnattr_setpgroup(posix_spawnattr_t *, pid_t);` | 函数 |
| [`posix_spawnattr_setschedparam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_setschedparam.html) | `int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict, const struct sched_param *restrict);` | 函数 |
| [`posix_spawnattr_setschedpolicy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_setschedpolicy.html) | `int posix_spawnattr_setschedpolicy(posix_spawnattr_t *, int);` | 函数 |
| [`posix_spawnattr_setsigdefault`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_setsigdefault.html) | `int posix_spawnattr_setsigdefault(posix_spawnattr_t *restrict, const sigset_t *restrict);` | 函数 |
| [`posix_spawnattr_setsigmask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnattr_setsigmask.html) | `int posix_spawnattr_setsigmask(posix_spawnattr_t *restrict, const sigset_t *restrict);` | 函数 |
| [`posix_spawnp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawnp.html) | `int posix_spawnp(pid_t *restrict, const char *restrict, const posix_spawn_file_actions_t *restrict, const posix_spawnattr_t *restrict, char *const [restrict], char *const [restrict]);` | 函数 |

<a id="header-stdarg-h"></a>

### `<stdarg.h>`

可变参数列表操作；本节接口均为宏。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdarg.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`va_arg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/va_arg.html) | `type va_arg(va_list ap, type);` | 宏 |
| [`va_copy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/va_copy.html) | `void va_copy(va_list dest, va_list src);` | 宏 |
| [`va_end`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/va_end.html) | `void va_end(va_list ap);` | 宏 |
| [`va_start`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/va_start.html) | `void va_start(va_list ap, argN);` | 宏 |

<a id="header-stdatomic-h"></a>

### `<stdatomic.h>`

ISO C 原子操作、内存序和内存栅栏。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdatomic.h.html)，本节共 29 个接口。

泛型接口的 `A` 表示原子类型，`C` 表示对应的非原子类型，`M` 表示适用的操作数类型。`kill_dependency(y)` 按宏调用形式列出，结果类型与参数类型相同，用于终止依赖链。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`atomic_compare_exchange_strong`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_compare_exchange_strong.html) | `_Bool atomic_compare_exchange_strong(volatile A *, C *, C);` | 原子泛型接口 |
| [`atomic_compare_exchange_strong_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_compare_exchange_strong_explicit.html) | `_Bool atomic_compare_exchange_strong_explicit(volatile A *, C *, C, memory_order, memory_order);` | 原子泛型接口 |
| [`atomic_compare_exchange_weak`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_compare_exchange_weak.html) | `_Bool atomic_compare_exchange_weak(volatile A *, C *, C);` | 原子泛型接口 |
| [`atomic_compare_exchange_weak_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_compare_exchange_weak_explicit.html) | `_Bool atomic_compare_exchange_weak_explicit(volatile A *, C *, C, memory_order, memory_order);` | 原子泛型接口 |
| [`atomic_exchange`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_exchange.html) | `C atomic_exchange(volatile A *, C);` | 原子泛型接口 |
| [`atomic_exchange_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_exchange_explicit.html) | `C atomic_exchange_explicit(volatile A *, C, memory_order);` | 原子泛型接口 |
| [`atomic_fetch_add`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_add.html) | `C atomic_fetch_add(volatile A *, M);` | 原子泛型接口 |
| [`atomic_fetch_add_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_add_explicit.html) | `C atomic_fetch_add_explicit(volatile A *, M, memory_order);` | 原子泛型接口 |
| [`atomic_fetch_and`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_and.html) | `C atomic_fetch_and(volatile A *, M);` | 原子泛型接口 |
| [`atomic_fetch_and_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_and_explicit.html) | `C atomic_fetch_and_explicit(volatile A *, M, memory_order);` | 原子泛型接口 |
| [`atomic_fetch_or`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_or.html) | `C atomic_fetch_or(volatile A *, M);` | 原子泛型接口 |
| [`atomic_fetch_or_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_or_explicit.html) | `C atomic_fetch_or_explicit(volatile A *, M, memory_order);` | 原子泛型接口 |
| [`atomic_fetch_sub`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_sub.html) | `C atomic_fetch_sub(volatile A *, M);` | 原子泛型接口 |
| [`atomic_fetch_sub_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_sub_explicit.html) | `C atomic_fetch_sub_explicit(volatile A *, M, memory_order);` | 原子泛型接口 |
| [`atomic_fetch_xor`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_xor.html) | `C atomic_fetch_xor(volatile A *, M);` | 原子泛型接口 |
| [`atomic_fetch_xor_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_fetch_xor_explicit.html) | `C atomic_fetch_xor_explicit(volatile A *, M, memory_order);` | 原子泛型接口 |
| [`atomic_flag_clear`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_flag_clear.html) | `void atomic_flag_clear(volatile atomic_flag *);` | 函数 |
| [`atomic_flag_clear_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_flag_clear_explicit.html) | `void atomic_flag_clear_explicit(volatile atomic_flag *, memory_order);` | 函数 |
| [`atomic_flag_test_and_set`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_flag_test_and_set.html) | `_Bool atomic_flag_test_and_set(volatile atomic_flag *);` | 函数 |
| [`atomic_flag_test_and_set_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_flag_test_and_set_explicit.html) | `_Bool atomic_flag_test_and_set_explicit( volatile atomic_flag *, memory_order);` | 函数 |
| [`atomic_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_init.html) | `void atomic_init(volatile A *, C);` | 原子泛型接口 |
| [`atomic_is_lock_free`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_is_lock_free.html) | `_Bool atomic_is_lock_free(const volatile A *);` | 原子泛型接口 |
| [`atomic_load`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_load.html) | `C atomic_load(const volatile A *);` | 原子泛型接口 |
| [`atomic_load_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_load_explicit.html) | `C atomic_load_explicit(const volatile A *, memory_order);` | 原子泛型接口 |
| [`atomic_signal_fence`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_signal_fence.html) | `void atomic_signal_fence(memory_order);` | 函数 |
| [`atomic_store`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_store.html) | `void atomic_store(volatile A *, C);` | 原子泛型接口 |
| [`atomic_store_explicit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_store_explicit.html) | `void atomic_store_explicit(volatile A *, C, memory_order);` | 原子泛型接口 |
| [`atomic_thread_fence`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atomic_thread_fence.html) | `void atomic_thread_fence(memory_order);` | 函数 |
| [`kill_dependency`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/kill_dependency.html) | `kill_dependency(y)` | 宏 |

<a id="header-stdio-h"></a>

### `<stdio.h>`

标准流、文件流、格式化 I/O、内存流和流锁。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdio.h.html)，本节共 68 个接口。

`asprintf` 和 `vasprintf` 的声明依据各自接口页补入；核对时官方 `stdio.h` 页的声明列表未列出这两项。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`asprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asprintf.html) | `int asprintf(char **restrict ptr, const char *restrict format, ...);` | 函数 |
| [`clearerr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clearerr.html) | `void clearerr(FILE *);` | 函数 |
| [`ctermid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctermid.html) | `char *ctermid(char *);` | 函数 |
| [`dprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dprintf.html) | `int dprintf(int, const char *restrict, ...);` | 函数 |
| [`fclose`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fclose.html) | `int fclose(FILE *);` | 函数 |
| [`fdopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fdopen.html) | `FILE *fdopen(int, const char *);` | 函数 |
| [`feof`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/feof.html) | `int feof(FILE *);` | 函数 |
| [`ferror`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ferror.html) | `int ferror(FILE *);` | 函数 |
| [`fflush`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fflush.html) | `int fflush(FILE *);` | 函数 |
| [`fgetc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fgetc.html) | `int fgetc(FILE *);` | 函数 |
| [`fgetpos`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fgetpos.html) | `int fgetpos(FILE *restrict, fpos_t *restrict);` | 函数 |
| [`fgets`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fgets.html) | `char *fgets(char *restrict, int, FILE *restrict);` | 函数 |
| [`fileno`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fileno.html) | `int fileno(FILE *);` | 函数 |
| [`flockfile`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/flockfile.html) | `void flockfile(FILE *);` | 函数 |
| [`fmemopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fmemopen.html) | `FILE *fmemopen(void *restrict, size_t, const char *restrict);` | 函数 |
| [`fopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fopen.html) | `FILE *fopen(const char *restrict, const char *restrict);` | 函数 |
| [`fprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fprintf.html) | `int fprintf(FILE *restrict, const char *restrict, ...);` | 函数 |
| [`fputc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fputc.html) | `int fputc(int, FILE *);` | 函数 |
| [`fputs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fputs.html) | `int fputs(const char *restrict, FILE *restrict);` | 函数 |
| [`fread`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fread.html) | `size_t fread(void *restrict, size_t, size_t, FILE *restrict);` | 函数 |
| [`freopen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/freopen.html) | `FILE *freopen(const char *restrict, const char *restrict, FILE *restrict);` | 函数 |
| [`fscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fscanf.html) | `int fscanf(FILE *restrict, const char *restrict, ...);` | 函数 |
| [`fseek`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fseek.html) | `int fseek(FILE *, long, int);` | 函数 |
| [`fseeko`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fseeko.html) | `int fseeko(FILE *, off_t, int);` | 函数 |
| [`fsetpos`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fsetpos.html) | `int fsetpos(FILE *, const fpos_t *);` | 函数 |
| [`ftell`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftell.html) | `long ftell(FILE *);` | 函数 |
| [`ftello`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftello.html) | `off_t ftello(FILE *);` | 函数 |
| [`ftrylockfile`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftrylockfile.html) | `int ftrylockfile(FILE *);` | 函数 |
| [`funlockfile`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/funlockfile.html) | `void funlockfile(FILE *);` | 函数 |
| [`fwrite`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fwrite.html) | `size_t fwrite(const void *restrict, size_t, size_t, FILE *restrict);` | 函数 |
| [`getc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getc.html) | `int getc(FILE *);` | 函数 |
| [`getc_unlocked`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getc_unlocked.html) | `int getc_unlocked(FILE *);` | 函数 |
| [`getchar`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getchar.html) | `int getchar(void);` | 函数 |
| [`getchar_unlocked`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getchar_unlocked.html) | `int getchar_unlocked(void);` | 函数 |
| [`getdelim`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getdelim.html) | `ssize_t getdelim(char **restrict, size_t *restrict, int, FILE *restrict);` | 函数 |
| [`getline`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getline.html) | `ssize_t getline(char **restrict, size_t *restrict, FILE *restrict);` | 函数 |
| [`open_memstream`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/open_memstream.html) | `FILE *open_memstream(char **, size_t *);` | 函数 |
| [`pclose`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pclose.html) | `int pclose(FILE *);` | 函数 |
| [`perror`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/perror.html) | `void perror(const char *);` | 函数 |
| [`popen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/popen.html) | `FILE *popen(const char *, const char *);` | 函数 |
| [`printf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/printf.html) | `int printf(const char *restrict, ...);` | 函数 |
| [`putc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putc.html) | `int putc(int, FILE *);` | 函数 |
| [`putc_unlocked`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putc_unlocked.html) | `int putc_unlocked(int, FILE *);` | 函数 |
| [`putchar`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putchar.html) | `int putchar(int);` | 函数 |
| [`putchar_unlocked`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putchar_unlocked.html) | `int putchar_unlocked(int);` | 函数 |
| [`puts`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/puts.html) | `int puts(const char *);` | 函数 |
| [`remove`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/remove.html) | `int remove(const char *);` | 函数 |
| [`rename`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html) | `int rename(const char *, const char *);` | 函数 |
| [`renameat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/renameat.html) | `int renameat(int, const char *, int, const char *);` | 函数 |
| [`rewind`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rewind.html) | `void rewind(FILE *);` | 函数 |
| [`scanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/scanf.html) | `int scanf(const char *restrict, ...);` | 函数 |
| [`setbuf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setbuf.html) | `void setbuf(FILE *restrict, char *restrict);` | 函数 |
| [`setvbuf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setvbuf.html) | `int setvbuf(FILE *restrict, char *restrict, int, size_t);` | 函数 |
| [`snprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/snprintf.html) | `int snprintf(char *restrict, size_t, const char *restrict, ...);` | 函数 |
| [`sprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sprintf.html) | `int sprintf(char *restrict, const char *restrict, ...);` | 函数 |
| [`sscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sscanf.html) | `int sscanf(const char *restrict, const char *restrict, ...);` | 函数 |
| [`tmpfile`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tmpfile.html) | `FILE *tmpfile(void);` | 函数 |
| [`tmpnam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tmpnam.html) | `char *tmpnam(char *);` | 函数 |
| [`ungetc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ungetc.html) | `int ungetc(int, FILE *);` | 函数 |
| [`vasprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vasprintf.html) | `int vasprintf(char **restrict ptr, const char *restrict format, va_list ap);` | 函数 |
| [`vdprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vdprintf.html) | `int vdprintf(int, const char *restrict, va_list);` | 函数 |
| [`vfprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vfprintf.html) | `int vfprintf(FILE *restrict, const char *restrict, va_list);` | 函数 |
| [`vfscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vfscanf.html) | `int vfscanf(FILE *restrict, const char *restrict, va_list);` | 函数 |
| [`vprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vprintf.html) | `int vprintf(const char *restrict, va_list);` | 函数 |
| [`vscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vscanf.html) | `int vscanf(const char *restrict, va_list);` | 函数 |
| [`vsnprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vsnprintf.html) | `int vsnprintf(char *restrict, size_t, const char *restrict, va_list);` | 函数 |
| [`vsprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vsprintf.html) | `int vsprintf(char *restrict, const char *restrict, va_list);` | 函数 |
| [`vsscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vsscanf.html) | `int vsscanf(const char *restrict, const char *restrict, va_list);` | 函数 |

<a id="header-stdlib-h"></a>

### `<stdlib.h>`

内存分配、数字转换、进程退出、环境变量、排序及随机数。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdlib.h.html)，本节共 72 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`_Exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/_Exit.html) | `_Noreturn void _Exit(int);` | 函数 |
| [`a64l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/a64l.html) | `long a64l(const char *);` | 函数 |
| [`abort`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/abort.html) | `_Noreturn void abort(void);` | 函数 |
| [`abs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/abs.html) | `int abs(int);` | 函数 |
| [`aligned_alloc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/aligned_alloc.html) | `void *aligned_alloc(size_t, size_t);` | 函数 |
| [`at_quick_exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/at_quick_exit.html) | `int at_quick_exit(void (*)(void));` | 函数 |
| [`atexit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atexit.html) | `int atexit(void (*)(void));` | 函数 |
| [`atof`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atof.html) | `double atof(const char *);` | 函数 |
| [`atoi`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atoi.html) | `int atoi(const char *);` | 函数 |
| [`atol`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atol.html) | `long atol(const char *);` | 函数 |
| [`atoll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/atoll.html) | `long long atoll(const char *);` | 函数 |
| [`bsearch`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/bsearch.html) | `void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));` | 函数 |
| [`calloc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/calloc.html) | `void *calloc(size_t, size_t);` | 函数 |
| [`div`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/div.html) | `div_t div(int, int);` | 函数 |
| [`drand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/drand48.html) | `double drand48(void);` | 函数 |
| [`erand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/erand48.html) | `double erand48(unsigned short [3]);` | 函数 |
| [`exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/exit.html) | `_Noreturn void exit(int);` | 函数 |
| [`free`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/free.html) | `void free(void *);` | 函数 |
| [`getenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getenv.html) | `char *getenv(const char *);` | 函数 |
| [`getsubopt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsubopt.html) | `int getsubopt(char **restrict, char *const *restrict, char **restrict);` | 函数 |
| [`grantpt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/grantpt.html) | `int grantpt(int);` | 函数 |
| [`initstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/initstate.html) | `char *initstate(unsigned, char *, size_t);` | 函数 |
| [`jrand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/jrand48.html) | `long jrand48(unsigned short [3]);` | 函数 |
| [`l64a`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/l64a.html) | `char *l64a(long);` | 函数 |
| [`labs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/labs.html) | `long labs(long);` | 函数 |
| [`lcong48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lcong48.html) | `void lcong48(unsigned short [7]);` | 函数 |
| [`ldiv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ldiv.html) | `ldiv_t ldiv(long, long);` | 函数 |
| [`llabs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/llabs.html) | `long long llabs(long long);` | 函数 |
| [`lldiv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lldiv.html) | `lldiv_t lldiv(long long, long long);` | 函数 |
| [`lrand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lrand48.html) | `long lrand48(void);` | 函数 |
| [`malloc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/malloc.html) | `void *malloc(size_t);` | 函数 |
| [`mblen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mblen.html) | `int mblen(const char *, size_t);` | 函数 |
| [`mbstowcs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbstowcs.html) | `size_t mbstowcs(wchar_t *restrict, const char *restrict, size_t);` | 函数 |
| [`mbtowc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbtowc.html) | `int mbtowc(wchar_t *restrict, const char *restrict, size_t);` | 函数 |
| [`mkdtemp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkdtemp.html) | `char *mkdtemp(char *);` | 函数 |
| [`mkostemp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkostemp.html) | `int mkostemp(char *, int);` | 函数 |
| [`mkstemp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkstemp.html) | `int mkstemp(char *);` | 函数 |
| [`mrand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mrand48.html) | `long mrand48(void);` | 函数 |
| [`nrand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nrand48.html) | `long nrand48(unsigned short [3]);` | 函数 |
| [`posix_memalign`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_memalign.html) | `int posix_memalign(void **, size_t, size_t);` | 函数 |
| [`posix_openpt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_openpt.html) | `int posix_openpt(int);` | 函数 |
| [`ptsname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ptsname.html) | `char *ptsname(int);` | 函数 |
| [`ptsname_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ptsname_r.html) | `int ptsname_r(int, char *, size_t);` | 函数 |
| [`putenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putenv.html) | `int putenv(char *);` | 函数 |
| [`qsort`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/qsort.html) | `void qsort(void *, size_t, size_t, int (*)(const void *, const void *));` | 函数 |
| [`qsort_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/qsort_r.html) | `void qsort_r(void *, size_t, size_t, int (*)(const void *, const void *, void *), void *);` | 函数 |
| [`quick_exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/quick_exit.html) | `_Noreturn void quick_exit(int);` | 函数 |
| [`rand`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rand.html) | `int rand(void);` | 函数 |
| [`random`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/random.html) | `long random(void);` | 函数 |
| [`realloc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/realloc.html) | `void *realloc(void *, size_t);` | 函数 |
| [`reallocarray`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/reallocarray.html) | `void *reallocarray(void *, size_t, size_t);` | 函数 |
| [`realpath`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/realpath.html) | `char *realpath(const char *restrict, char *restrict);` | 函数 |
| [`secure_getenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/secure_getenv.html) | `char *secure_getenv(const char *);` | 函数 |
| [`seed48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/seed48.html) | `unsigned short *seed48(unsigned short [3]);` | 函数 |
| [`setenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setenv.html) | `int setenv(const char *, const char *, int);` | 函数 |
| [`setkey`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setkey.html) | `void setkey(const char *);` | 函数 |
| [`setstate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setstate.html) | `char *setstate(char *);` | 函数 |
| [`srand`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/srand.html) | `void srand(unsigned);` | 函数 |
| [`srand48`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/srand48.html) | `void srand48(long);` | 函数 |
| [`srandom`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/srandom.html) | `void srandom(unsigned);` | 函数 |
| [`strtod`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtod.html) | `double strtod(const char *restrict, char **restrict);` | 函数 |
| [`strtof`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtof.html) | `float strtof(const char *restrict, char **restrict);` | 函数 |
| [`strtol`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtol.html) | `long strtol(const char *restrict, char **restrict, int);` | 函数 |
| [`strtold`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtold.html) | `long double strtold(const char *restrict, char **restrict);` | 函数 |
| [`strtoll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtoll.html) | `long long strtoll(const char *restrict, char **restrict, int);` | 函数 |
| [`strtoul`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtoul.html) | `unsigned long strtoul(const char *restrict, char **restrict, int);` | 函数 |
| [`strtoull`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtoull.html) | `unsigned long long strtoull(const char *restrict, char **restrict, int);` | 函数 |
| [`system`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/system.html) | `int system(const char *);` | 函数 |
| [`unlockpt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unlockpt.html) | `int unlockpt(int);` | 函数 |
| [`unsetenv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unsetenv.html) | `int unsetenv(const char *);` | 函数 |
| [`wcstombs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstombs.html) | `size_t wcstombs(char *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wctomb`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wctomb.html) | `int wctomb(char *, wchar_t);` | 函数 |

<a id="header-string-h"></a>

### `<string.h>`

字节内存操作、字符串复制、拼接、比较、查找和分割。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/string.h.html)，本节共 37 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`memccpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memccpy.html) | `void *memccpy(void *restrict, const void *restrict, int, size_t);` | 函数 |
| [`memchr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memchr.html) | `void *memchr(const void *, int, size_t);` | 函数 |
| [`memcmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memcmp.html) | `int memcmp(const void *, const void *, size_t);` | 函数 |
| [`memcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memcpy.html) | `void *memcpy(void *restrict, const void *restrict, size_t);` | 函数 |
| [`memmem`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memmem.html) | `void *memmem(const void *, size_t, const void *, size_t);` | 函数 |
| [`memmove`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memmove.html) | `void *memmove(void *, const void *, size_t);` | 函数 |
| [`memset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/memset.html) | `void *memset(void *, int, size_t);` | 函数 |
| [`stpcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stpcpy.html) | `char *stpcpy(char *restrict, const char *restrict);` | 函数 |
| [`stpncpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stpncpy.html) | `char *stpncpy(char *restrict, const char *restrict, size_t);` | 函数 |
| [`strcat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcat.html) | `char *strcat(char *restrict, const char *restrict);` | 函数 |
| [`strchr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strchr.html) | `char *strchr(const char *, int);` | 函数 |
| [`strcmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcmp.html) | `int strcmp(const char *, const char *);` | 函数 |
| [`strcoll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcoll.html) | `int strcoll(const char *, const char *);` | 函数 |
| [`strcoll_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcoll_l.html) | `int strcoll_l(const char *, const char *, locale_t);` | 函数 |
| [`strcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcpy.html) | `char *strcpy(char *restrict, const char *restrict);` | 函数 |
| [`strcspn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcspn.html) | `size_t strcspn(const char *, const char *);` | 函数 |
| [`strdup`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strdup.html) | `char *strdup(const char *);` | 函数 |
| [`strerror`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strerror.html) | `char *strerror(int);` | 函数 |
| [`strerror_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strerror_l.html) | `char *strerror_l(int, locale_t);` | 函数 |
| [`strerror_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strerror_r.html) | `int strerror_r(int, char *, size_t);` | 函数 |
| [`strlcat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlcat.html) | `size_t strlcat(char *restrict, const char *restrict, size_t);` | 函数 |
| [`strlcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlcpy.html) | `size_t strlcpy(char *restrict, const char *restrict, size_t);` | 函数 |
| [`strlen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlen.html) | `size_t strlen(const char *);` | 函数 |
| [`strncat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncat.html) | `char *strncat(char *restrict, const char *restrict, size_t);` | 函数 |
| [`strncmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncmp.html) | `int strncmp(const char *, const char *, size_t);` | 函数 |
| [`strncpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncpy.html) | `char *strncpy(char *restrict, const char *restrict, size_t);` | 函数 |
| [`strndup`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strndup.html) | `char *strndup(const char *, size_t);` | 函数 |
| [`strnlen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strnlen.html) | `size_t strnlen(const char *, size_t);` | 函数 |
| [`strpbrk`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strpbrk.html) | `char *strpbrk(const char *, const char *);` | 函数 |
| [`strrchr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strrchr.html) | `char *strrchr(const char *, int);` | 函数 |
| [`strsignal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strsignal.html) | `char *strsignal(int);` | 函数 |
| [`strspn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strspn.html) | `size_t strspn(const char *, const char *);` | 函数 |
| [`strstr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strstr.html) | `char *strstr(const char *, const char *);` | 函数 |
| [`strtok`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtok.html) | `char *strtok(char *restrict, const char *restrict);` | 函数 |
| [`strtok_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtok_r.html) | `char *strtok_r(char *restrict, const char *restrict, char **restrict);` | 函数 |
| [`strxfrm`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strxfrm.html) | `size_t strxfrm(char *restrict, const char *restrict, size_t);` | 函数 |
| [`strxfrm_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strxfrm_l.html) | `size_t strxfrm_l(char *restrict, const char *restrict, size_t, locale_t);` | 函数 |

<a id="header-strings-h"></a>

### `<strings.h>`

忽略大小写的字符串比较及首个置位位查询。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/strings.h.html)，本节共 7 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`ffs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ffs.html) | `int ffs(int);` | 函数 |
| [`ffsl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ffsl.html) | `int ffsl(long);` | 函数 |
| [`ffsll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ffsll.html) | `int ffsll(long long);` | 函数 |
| [`strcasecmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcasecmp.html) | `int strcasecmp(const char *, const char *);` | 函数 |
| [`strcasecmp_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strcasecmp_l.html) | `int strcasecmp_l(const char *, const char *, locale_t);` | 函数 |
| [`strncasecmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncasecmp.html) | `int strncasecmp(const char *, const char *, size_t);` | 函数 |
| [`strncasecmp_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strncasecmp_l.html) | `int strncasecmp_l(const char *, const char *, size_t, locale_t);` | 函数 |

<a id="header-sys-ipc-h"></a>

### `<sys/ipc.h>`

XSI IPC 键值和通用定义。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_ipc.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`ftok`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftok.html) | `key_t ftok(const char *, int);` | 函数 |

<a id="header-sys-mman-h"></a>

### `<sys/mman.h>`

内存映射、保护、同步、锁页、共享内存和类型化内存。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_mman.h.html)，本节共 14 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`mlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mlock.html) | `int mlock(const void *, size_t);` | 函数 |
| [`mlockall`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mlockall.html) | `int mlockall(int);` | 函数 |
| [`mmap`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mmap.html) | `void *mmap(void *, size_t, int, int, int, off_t);` | 函数 |
| [`mprotect`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mprotect.html) | `int mprotect(void *, size_t, int);` | 函数 |
| [`msync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/msync.html) | `int msync(void *, size_t, int);` | 函数 |
| [`munlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/munlock.html) | `int munlock(const void *, size_t);` | 函数 |
| [`munlockall`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/munlockall.html) | `int munlockall(void);` | 函数 |
| [`munmap`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/munmap.html) | `int munmap(void *, size_t);` | 函数 |
| [`posix_madvise`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_madvise.html) | `int posix_madvise(void *, size_t, int);` | 函数 |
| [`posix_mem_offset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_mem_offset.html) | `int posix_mem_offset(const void *restrict, size_t, off_t *restrict, size_t *restrict, int *restrict);` | 函数 |
| [`posix_typed_mem_get_info`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_typed_mem_get_info.html) | `int posix_typed_mem_get_info(int, struct posix_typed_mem_info *);` | 函数 |
| [`posix_typed_mem_open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_typed_mem_open.html) | `int posix_typed_mem_open(const char *, int, int);` | 函数 |
| [`shm_open`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shm_open.html) | `int shm_open(const char *, int, mode_t);` | 函数 |
| [`shm_unlink`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shm_unlink.html) | `int shm_unlink(const char *);` | 函数 |

<a id="header-sys-msg-h"></a>

### `<sys/msg.h>`

XSI 消息队列。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_msg.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`msgctl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/msgctl.html) | `int msgctl(int, int, struct msqid_ds *);` | 函数 |
| [`msgget`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/msgget.html) | `int msgget(key_t, int);` | 函数 |
| [`msgrcv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/msgrcv.html) | `ssize_t msgrcv(int, void *, size_t, long, int);` | 函数 |
| [`msgsnd`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/msgsnd.html) | `int msgsnd(int, const void *, size_t, int);` | 函数 |

<a id="header-sys-resource-h"></a>

### `<sys/resource.h>`

资源限制、资源使用量和进程优先级；XSI 接口。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_resource.h.html)，本节共 5 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`getpriority`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpriority.html) | `int getpriority(int, id_t);` | 函数 |
| [`getrlimit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getrlimit.html) | `int getrlimit(int, struct rlimit *);` | 函数 |
| [`getrusage`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getrusage.html) | `int getrusage(int, struct rusage *);` | 函数 |
| [`setpriority`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setpriority.html) | `int setpriority(int, id_t, int);` | 函数 |
| [`setrlimit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setrlimit.html) | `int setrlimit(int, const struct rlimit *);` | 函数 |

<a id="header-sys-select-h"></a>

### `<sys/select.h>`

文件描述符集合宏及 select/pselect 事件等待。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_select.h.html)，本节共 6 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`FD_CLR`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/FD_CLR.html) | `void FD_CLR(int, fd_set *);` | 宏 |
| [`FD_ISSET`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/FD_ISSET.html) | `int FD_ISSET(int, const fd_set *);` | 宏 |
| [`FD_SET`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/FD_SET.html) | `void FD_SET(int, fd_set *);` | 宏 |
| [`FD_ZERO`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/FD_ZERO.html) | `void FD_ZERO(fd_set *);` | 宏 |
| [`pselect`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pselect.html) | `int pselect(int, fd_set *restrict, fd_set *restrict, fd_set *restrict, const struct timespec *restrict, const sigset_t *restrict);` | 函数 |
| [`select`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/select.html) | `int select(int, fd_set *restrict, fd_set *restrict, fd_set *restrict, struct timeval *restrict);` | 函数 |

<a id="header-sys-sem-h"></a>

### `<sys/sem.h>`

XSI 信号量集合。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_sem.h.html)，本节共 3 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`semctl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/semctl.html) | `int semctl(int, int, int, ...);` | 函数 |
| [`semget`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/semget.html) | `int semget(key_t, int, int);` | 函数 |
| [`semop`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/semop.html) | `int semop(int, struct sembuf *, size_t);` | 函数 |

<a id="header-sys-shm-h"></a>

### `<sys/shm.h>`

XSI 共享内存段。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_shm.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`shmat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shmat.html) | `void *shmat(int, const void *, int);` | 函数 |
| [`shmctl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shmctl.html) | `int shmctl(int, int, struct shmid_ds *);` | 函数 |
| [`shmdt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shmdt.html) | `int shmdt(const void *);` | 函数 |
| [`shmget`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shmget.html) | `int shmget(key_t, size_t, int);` | 函数 |

<a id="header-sys-socket-h"></a>

### `<sys/socket.h>`

套接字创建、连接、监听、收发及选项控制。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_socket.h.html)，本节共 19 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`accept`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/accept.html) | `int accept(int, struct sockaddr *restrict, socklen_t *restrict);` | 函数 |
| [`accept4`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/accept4.html) | `int accept4(int, struct sockaddr *restrict, socklen_t *restrict, int);` | 函数 |
| [`bind`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/bind.html) | `int bind(int, const struct sockaddr *, socklen_t);` | 函数 |
| [`connect`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/connect.html) | `int connect(int, const struct sockaddr *, socklen_t);` | 函数 |
| [`getpeername`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpeername.html) | `int getpeername(int, struct sockaddr *restrict, socklen_t *restrict);` | 函数 |
| [`getsockname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsockname.html) | `int getsockname(int, struct sockaddr *restrict, socklen_t *restrict);` | 函数 |
| [`getsockopt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsockopt.html) | `int getsockopt(int, int, int, void *restrict, socklen_t *restrict);` | 函数 |
| [`listen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/listen.html) | `int listen(int, int);` | 函数 |
| [`recv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/recv.html) | `ssize_t recv(int, void *, size_t, int);` | 函数 |
| [`recvfrom`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/recvfrom.html) | `ssize_t recvfrom(int, void *restrict, size_t, int, struct sockaddr *restrict, socklen_t *restrict);` | 函数 |
| [`recvmsg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/recvmsg.html) | `ssize_t recvmsg(int, struct msghdr *, int);` | 函数 |
| [`send`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/send.html) | `ssize_t send(int, const void *, size_t, int);` | 函数 |
| [`sendmsg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sendmsg.html) | `ssize_t sendmsg(int, const struct msghdr *, int);` | 函数 |
| [`sendto`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sendto.html) | `ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);` | 函数 |
| [`setsockopt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setsockopt.html) | `int setsockopt(int, int, int, const void *, socklen_t);` | 函数 |
| [`shutdown`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shutdown.html) | `int shutdown(int, int);` | 函数 |
| [`sockatmark`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sockatmark.html) | `int sockatmark(int);` | 函数 |
| [`socket`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/socket.html) | `int socket(int, int, int);` | 函数 |
| [`socketpair`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/socketpair.html) | `int socketpair(int, int, int, int [2]);` | 函数 |

<a id="header-sys-stat-h"></a>

### `<sys/stat.h>`

文件元数据、权限、文件类型、目录/FIFO 创建及时间戳。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_stat.h.html)，本节共 16 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`chmod`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/chmod.html) | `int chmod(const char *, mode_t);` | 函数 |
| [`fchmod`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fchmod.html) | `int fchmod(int, mode_t);` | 函数 |
| [`fchmodat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fchmodat.html) | `int fchmodat(int, const char *, mode_t, int);` | 函数 |
| [`fstat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fstat.html) | `int fstat(int, struct stat *);` | 函数 |
| [`fstatat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fstatat.html) | `int fstatat(int, const char *restrict, struct stat *restrict, int);` | 函数 |
| [`futimens`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/futimens.html) | `int futimens(int, const struct timespec [2]);` | 函数 |
| [`lstat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lstat.html) | `int lstat(const char *restrict, struct stat *restrict);` | 函数 |
| [`mkdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkdir.html) | `int mkdir(const char *, mode_t);` | 函数 |
| [`mkdirat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkdirat.html) | `int mkdirat(int, const char *, mode_t);` | 函数 |
| [`mkfifo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkfifo.html) | `int mkfifo(const char *, mode_t);` | 函数 |
| [`mkfifoat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkfifoat.html) | `int mkfifoat(int, const char *, mode_t);` | 函数 |
| [`mknod`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mknod.html) | `int mknod(const char *, mode_t, dev_t);` | 函数 |
| [`mknodat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mknodat.html) | `int mknodat(int, const char *, mode_t, dev_t);` | 函数 |
| [`stat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stat.html) | `int stat(const char *restrict, struct stat *restrict);` | 函数 |
| [`umask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/umask.html) | `mode_t umask(mode_t);` | 函数 |
| [`utimensat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/utimensat.html) | `int utimensat(int, const char *, const struct timespec [2], int);` | 函数 |

<a id="header-sys-statvfs-h"></a>

### `<sys/statvfs.h>`

文件系统容量和属性。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_statvfs.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`fstatvfs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fstatvfs.html) | `int fstatvfs(int, struct statvfs *);` | 函数 |
| [`statvfs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/statvfs.html) | `int statvfs(const char *restrict, struct statvfs *restrict);` | 函数 |

<a id="header-sys-time-h"></a>

### `<sys/time.h>`

时间值和兼容接口；select 统一列在 sys/select.h。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_time.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`utimes`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/utimes.html) | `int utimes(const char *, const struct timeval [2]);` | 函数 |

<a id="header-sys-times-h"></a>

### `<sys/times.h>`

进程及子进程 CPU 时间统计。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_times.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`times`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/times.html) | `clock_t times(struct tms *);` | 函数 |

<a id="header-sys-uio-h"></a>

### `<sys/uio.h>`

分散读取和聚集写入。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_uio.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`readv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/readv.html) | `ssize_t readv(int, const struct iovec *, int);` | 函数 |
| [`writev`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/writev.html) | `ssize_t writev(int, const struct iovec *, int);` | 函数 |

<a id="header-sys-utsname-h"></a>

### `<sys/utsname.h>`

操作系统和机器标识。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_utsname.h.html)，本节共 1 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`uname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/uname.html) | `int uname(struct utsname *);` | 函数 |

<a id="header-sys-wait-h"></a>

### `<sys/wait.h>`

等待子进程状态变化。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_wait.h.html)，本节共 3 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wait.html) | `pid_t wait(int *);` | 函数 |
| [`waitid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/waitid.html) | `int waitid(idtype_t, id_t, siginfo_t *, int);` | 函数 |
| [`waitpid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/waitpid.html) | `pid_t waitpid(pid_t, int *, int);` | 函数 |

<a id="header-syslog-h"></a>

### `<syslog.h>`

系统日志连接、过滤及消息提交。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/syslog.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`closelog`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/closelog.html) | `void closelog(void);` | 函数 |
| [`openlog`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/openlog.html) | `void openlog(const char *, int, int);` | 函数 |
| [`setlogmask`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setlogmask.html) | `int setlogmask(int);` | 函数 |
| [`syslog`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/syslog.html) | `void syslog(int, const char *, ...);` | 函数 |

<a id="header-termios-h"></a>

### `<termios.h>`

终端属性、速率、前后台会话、流控制和窗口尺寸。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/termios.h.html)，本节共 13 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`cfgetispeed`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cfgetispeed.html) | `speed_t cfgetispeed(const struct termios *);` | 函数 |
| [`cfgetospeed`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cfgetospeed.html) | `speed_t cfgetospeed(const struct termios *);` | 函数 |
| [`cfsetispeed`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cfsetispeed.html) | `int cfsetispeed(struct termios *, speed_t);` | 函数 |
| [`cfsetospeed`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cfsetospeed.html) | `int cfsetospeed(struct termios *, speed_t);` | 函数 |
| [`tcdrain`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcdrain.html) | `int tcdrain(int);` | 函数 |
| [`tcflow`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcflow.html) | `int tcflow(int, int);` | 函数 |
| [`tcflush`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcflush.html) | `int tcflush(int, int);` | 函数 |
| [`tcgetattr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcgetattr.html) | `int tcgetattr(int, struct termios *);` | 函数 |
| [`tcgetsid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcgetsid.html) | `pid_t tcgetsid(int);` | 函数 |
| [`tcgetwinsize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcgetwinsize.html) | `int tcgetwinsize(int, struct winsize *);` | 函数 |
| [`tcsendbreak`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcsendbreak.html) | `int tcsendbreak(int, int);` | 函数 |
| [`tcsetattr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcsetattr.html) | `int tcsetattr(int, int, const struct termios *);` | 函数 |
| [`tcsetwinsize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcsetwinsize.html) | `int tcsetwinsize(int, const struct winsize *);` | 函数 |

<a id="header-threads-h"></a>

### `<threads.h>`

ISO C 线程、互斥量、条件变量及线程专用存储。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/threads.h.html)，本节共 25 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`call_once`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/call_once.html) | `void call_once(once_flag *, void (*)(void));` | 函数 |
| [`cnd_broadcast`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cnd_broadcast.html) | `int cnd_broadcast(cnd_t *);` | 函数 |
| [`cnd_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cnd_destroy.html) | `void cnd_destroy(cnd_t *);` | 函数 |
| [`cnd_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cnd_init.html) | `int cnd_init(cnd_t *);` | 函数 |
| [`cnd_signal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cnd_signal.html) | `int cnd_signal(cnd_t *);` | 函数 |
| [`cnd_timedwait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cnd_timedwait.html) | `int cnd_timedwait(cnd_t * restrict, mtx_t * restrict, const struct timespec * restrict);` | 函数 |
| [`cnd_wait`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/cnd_wait.html) | `int cnd_wait(cnd_t *, mtx_t *);` | 函数 |
| [`mtx_destroy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mtx_destroy.html) | `void mtx_destroy(mtx_t *);` | 函数 |
| [`mtx_init`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mtx_init.html) | `int mtx_init(mtx_t *, int);` | 函数 |
| [`mtx_lock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mtx_lock.html) | `int mtx_lock(mtx_t *);` | 函数 |
| [`mtx_timedlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mtx_timedlock.html) | `int mtx_timedlock(mtx_t * restrict, const struct timespec * restrict);` | 函数 |
| [`mtx_trylock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mtx_trylock.html) | `int mtx_trylock(mtx_t *);` | 函数 |
| [`mtx_unlock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mtx_unlock.html) | `int mtx_unlock(mtx_t *);` | 函数 |
| [`thrd_create`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_create.html) | `int thrd_create(thrd_t *, thrd_start_t, void *);` | 函数 |
| [`thrd_current`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_current.html) | `thrd_t thrd_current(void);` | 函数 |
| [`thrd_detach`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_detach.html) | `int thrd_detach(thrd_t);` | 函数 |
| [`thrd_equal`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_equal.html) | `int thrd_equal(thrd_t, thrd_t);` | 函数 |
| [`thrd_exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_exit.html) | `_Noreturn void thrd_exit(int);` | 函数 |
| [`thrd_join`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_join.html) | `int thrd_join(thrd_t, int *);` | 函数 |
| [`thrd_sleep`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_sleep.html) | `int thrd_sleep(const struct timespec *, struct timespec *);` | 函数 |
| [`thrd_yield`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/thrd_yield.html) | `void thrd_yield(void);` | 函数 |
| [`tss_create`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tss_create.html) | `int tss_create(tss_t *, tss_dtor_t);` | 函数 |
| [`tss_delete`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tss_delete.html) | `void tss_delete(tss_t);` | 函数 |
| [`tss_get`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tss_get.html) | `void *tss_get(tss_t);` | 函数 |
| [`tss_set`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tss_set.html) | `int tss_set(tss_t, void *);` | 函数 |

<a id="header-time-h"></a>

### `<time.h>`

日历时间、时钟、休眠、定时器和时间格式转换。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/time.h.html)，本节共 27 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`asctime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asctime.html) | `char *asctime(const struct tm *);` | 函数 |
| [`clock`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock.html) | `clock_t clock(void);` | 函数 |
| [`clock_getcpuclockid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_getcpuclockid.html) | `int clock_getcpuclockid(pid_t, clockid_t *);` | 函数 |
| [`clock_getres`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_getres.html) | `int clock_getres(clockid_t, struct timespec *);` | 函数 |
| [`clock_gettime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_gettime.html) | `int clock_gettime(clockid_t, struct timespec *);` | 函数 |
| [`clock_nanosleep`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_nanosleep.html) | `int clock_nanosleep(clockid_t, int, const struct timespec *, struct timespec *);` | 函数 |
| [`clock_settime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/clock_settime.html) | `int clock_settime(clockid_t, const struct timespec *);` | 函数 |
| [`ctime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctime.html) | `char *ctime(const time_t *);` | 函数 |
| [`difftime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/difftime.html) | `double difftime(time_t, time_t);` | 函数 |
| [`getdate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getdate.html) | `struct tm *getdate(const char *);` | 函数 |
| [`gmtime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gmtime.html) | `struct tm *gmtime(const time_t *);` | 函数 |
| [`gmtime_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gmtime_r.html) | `struct tm *gmtime_r(const time_t *restrict, struct tm *restrict);` | 函数 |
| [`localtime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/localtime.html) | `struct tm *localtime(const time_t *);` | 函数 |
| [`localtime_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/localtime_r.html) | `struct tm *localtime_r(const time_t *restrict, struct tm *restrict);` | 函数 |
| [`mktime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mktime.html) | `time_t mktime(struct tm *);` | 函数 |
| [`nanosleep`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nanosleep.html) | `int nanosleep(const struct timespec *, struct timespec *);` | 函数 |
| [`strftime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strftime.html) | `size_t strftime(char *restrict, size_t, const char *restrict, const struct tm *restrict);` | 函数 |
| [`strftime_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strftime_l.html) | `size_t strftime_l(char *restrict, size_t, const char *restrict, const struct tm *restrict, locale_t);` | 函数 |
| [`strptime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strptime.html) | `char *strptime(const char *restrict, const char *restrict, struct tm *restrict);` | 函数 |
| [`time`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/time.html) | `time_t time(time_t *);` | 函数 |
| [`timer_create`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timer_create.html) | `int timer_create(clockid_t, struct sigevent *restrict, timer_t *restrict);` | 函数 |
| [`timer_delete`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timer_delete.html) | `int timer_delete(timer_t);` | 函数 |
| [`timer_getoverrun`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timer_getoverrun.html) | `int timer_getoverrun(timer_t);` | 函数 |
| [`timer_gettime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timer_gettime.html) | `int timer_gettime(timer_t, struct itimerspec *);` | 函数 |
| [`timer_settime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timer_settime.html) | `int timer_settime(timer_t, int, const struct itimerspec *restrict, struct itimerspec *restrict);` | 函数 |
| [`timespec_get`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timespec_get.html) | `int timespec_get(struct timespec *, int);` | 函数 |
| [`tzset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tzset.html) | `void tzset(void);` | 函数 |

<a id="header-uchar-h"></a>

### `<uchar.h>`

多字节字符与 UTF-16/UTF-32 编码单元的转换。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/uchar.h.html)，本节共 4 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`c16rtomb`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/c16rtomb.html) | `size_t c16rtomb(char *restrict, char16_t, mbstate_t *restrict);` | 函数 |
| [`c32rtomb`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/c32rtomb.html) | `size_t c32rtomb(char *restrict, char32_t, mbstate_t *restrict);` | 函数 |
| [`mbrtoc16`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbrtoc16.html) | `size_t mbrtoc16(char16_t *restrict, const char *restrict, size_t, mbstate_t *restrict);` | 函数 |
| [`mbrtoc32`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbrtoc32.html) | `size_t mbrtoc32(char32_t *restrict, const char *restrict, size_t, mbstate_t *restrict);` | 函数 |

<a id="header-unistd-h"></a>

### `<unistd.h>`

文件描述符、进程、身份、路径、管道和系统配置。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/unistd.h.html)，本节共 90 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`_Fork`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/_Fork.html) | `pid_t _Fork(void);` | 函数 |
| [`_exit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/_exit.html) | `_Noreturn void _exit(int);` | 函数 |
| [`access`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/access.html) | `int access(const char *, int);` | 函数 |
| [`alarm`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/alarm.html) | `unsigned alarm(unsigned);` | 函数 |
| [`chdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/chdir.html) | `int chdir(const char *);` | 函数 |
| [`chown`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/chown.html) | `int chown(const char *, uid_t, gid_t);` | 函数 |
| [`close`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/close.html) | `int close(int);` | 函数 |
| [`confstr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/confstr.html) | `size_t confstr(int, char *, size_t);` | 函数 |
| [`crypt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/crypt.html) | `char *crypt(const char *, const char *);` | 函数 |
| [`dup`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dup.html) | `int dup(int);` | 函数 |
| [`dup2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dup2.html) | `int dup2(int, int);` | 函数 |
| [`dup3`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dup3.html) | `int dup3(int, int, int);` | 函数 |
| [`encrypt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/encrypt.html) | `void encrypt(char [64], int);` | 函数 |
| [`execl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/execl.html) | `int execl(const char *, const char *, ...);` | 函数 |
| [`execle`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/execle.html) | `int execle(const char *, const char *, ...);` | 函数 |
| [`execlp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/execlp.html) | `int execlp(const char *, const char *, ...);` | 函数 |
| [`execv`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/execv.html) | `int execv(const char *, char *const []);` | 函数 |
| [`execve`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/execve.html) | `int execve(const char *, char *const [], char *const []);` | 函数 |
| [`execvp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/execvp.html) | `int execvp(const char *, char *const []);` | 函数 |
| [`faccessat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/faccessat.html) | `int faccessat(int, const char *, int, int);` | 函数 |
| [`fchdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fchdir.html) | `int fchdir(int);` | 函数 |
| [`fchown`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fchown.html) | `int fchown(int, uid_t, gid_t);` | 函数 |
| [`fchownat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fchownat.html) | `int fchownat(int, const char *, uid_t, gid_t, int);` | 函数 |
| [`fdatasync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fdatasync.html) | `int fdatasync(int);` | 函数 |
| [`fexecve`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fexecve.html) | `int fexecve(int, char *const [], char *const []);` | 函数 |
| [`fork`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fork.html) | `pid_t fork(void);` | 函数 |
| [`fpathconf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fpathconf.html) | `long fpathconf(int, int);` | 函数 |
| [`fsync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fsync.html) | `int fsync(int);` | 函数 |
| [`ftruncate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftruncate.html) | `int ftruncate(int, off_t);` | 函数 |
| [`getcwd`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getcwd.html) | `char *getcwd(char *, size_t);` | 函数 |
| [`getegid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getegid.html) | `gid_t getegid(void);` | 函数 |
| [`getentropy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getentropy.html) | `int getentropy(void *, size_t);` | 函数 |
| [`geteuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/geteuid.html) | `uid_t geteuid(void);` | 函数 |
| [`getgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgid.html) | `gid_t getgid(void);` | 函数 |
| [`getgroups`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getgroups.html) | `int getgroups(int, gid_t []);` | 函数 |
| [`gethostid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gethostid.html) | `long gethostid(void);` | 函数 |
| [`gethostname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/gethostname.html) | `int gethostname(char *, size_t);` | 函数 |
| [`getlogin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getlogin.html) | `char *getlogin(void);` | 函数 |
| [`getlogin_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getlogin_r.html) | `int getlogin_r(char *, size_t);` | 函数 |
| [`getopt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getopt.html) | `int getopt(int, char * const [], const char *);` | 函数 |
| [`getpgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpgid.html) | `pid_t getpgid(pid_t);` | 函数 |
| [`getpgrp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpgrp.html) | `pid_t getpgrp(void);` | 函数 |
| [`getpid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getpid.html) | `pid_t getpid(void);` | 函数 |
| [`getppid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getppid.html) | `pid_t getppid(void);` | 函数 |
| [`getresgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getresgid.html) | `int getresgid(gid_t *restrict, gid_t *restrict, gid_t *restrict);` | 函数 |
| [`getresuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getresuid.html) | `int getresuid(uid_t *restrict, uid_t *restrict, uid_t *restrict);` | 函数 |
| [`getsid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsid.html) | `pid_t getsid(pid_t);` | 函数 |
| [`getuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getuid.html) | `uid_t getuid(void);` | 函数 |
| [`isatty`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/isatty.html) | `int isatty(int);` | 函数 |
| [`lchown`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lchown.html) | `int lchown(const char *, uid_t, gid_t);` | 函数 |
| [`link`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/link.html) | `int link(const char *, const char *);` | 函数 |
| [`linkat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/linkat.html) | `int linkat(int, const char *, int, const char *, int);` | 函数 |
| [`lockf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lockf.html) | `int lockf(int, int, off_t);` | 函数 |
| [`lseek`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/lseek.html) | `off_t lseek(int, off_t, int);` | 函数 |
| [`nice`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/nice.html) | `int nice(int);` | 函数 |
| [`pathconf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pathconf.html) | `long pathconf(const char *, int);` | 函数 |
| [`pause`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pause.html) | `int pause(void);` | 函数 |
| [`pipe`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe.html) | `int pipe(int [2]);` | 函数 |
| [`pipe2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe2.html) | `int pipe2(int [2], int);` | 函数 |
| [`posix_close`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_close.html) | `int posix_close(int, int);` | 函数 |
| [`pread`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pread.html) | `ssize_t pread(int, void *, size_t, off_t);` | 函数 |
| [`pwrite`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pwrite.html) | `ssize_t pwrite(int, const void *, size_t, off_t);` | 函数 |
| [`read`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html) | `ssize_t read(int, void *, size_t);` | 函数 |
| [`readlink`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/readlink.html) | `ssize_t readlink(const char *restrict, char *restrict, size_t);` | 函数 |
| [`readlinkat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/readlinkat.html) | `ssize_t readlinkat(int, const char *restrict, char *restrict, size_t);` | 函数 |
| [`rmdir`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rmdir.html) | `int rmdir(const char *);` | 函数 |
| [`setegid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setegid.html) | `int setegid(gid_t);` | 函数 |
| [`seteuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/seteuid.html) | `int seteuid(uid_t);` | 函数 |
| [`setgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setgid.html) | `int setgid(gid_t);` | 函数 |
| [`setpgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setpgid.html) | `int setpgid(pid_t, pid_t);` | 函数 |
| [`setregid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setregid.html) | `int setregid(gid_t, gid_t);` | 函数 |
| [`setresgid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setresgid.html) | `int setresgid(gid_t, gid_t, gid_t);` | 函数 |
| [`setresuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setresuid.html) | `int setresuid(uid_t, uid_t, uid_t);` | 函数 |
| [`setreuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setreuid.html) | `int setreuid(uid_t, uid_t);` | 函数 |
| [`setsid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setsid.html) | `pid_t setsid(void);` | 函数 |
| [`setuid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setuid.html) | `int setuid(uid_t);` | 函数 |
| [`sleep`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sleep.html) | `unsigned sleep(unsigned);` | 函数 |
| [`swab`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/swab.html) | `void swab(const void *restrict, void *restrict, ssize_t);` | 函数 |
| [`symlink`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/symlink.html) | `int symlink(const char *, const char *);` | 函数 |
| [`symlinkat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/symlinkat.html) | `int symlinkat(const char *, int, const char *);` | 函数 |
| [`sync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sync.html) | `void sync(void);` | 函数 |
| [`sysconf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sysconf.html) | `long sysconf(int);` | 函数 |
| [`tcgetpgrp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcgetpgrp.html) | `pid_t tcgetpgrp(int);` | 函数 |
| [`tcsetpgrp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcsetpgrp.html) | `int tcsetpgrp(int, pid_t);` | 函数 |
| [`truncate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/truncate.html) | `int truncate(const char *, off_t);` | 函数 |
| [`ttyname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ttyname.html) | `char *ttyname(int);` | 函数 |
| [`ttyname_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ttyname_r.html) | `int ttyname_r(int, char *, size_t);` | 函数 |
| [`unlink`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unlink.html) | `int unlink(const char *);` | 函数 |
| [`unlinkat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unlinkat.html) | `int unlinkat(int, const char *, int);` | 函数 |
| [`write`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/write.html) | `ssize_t write(int, const void *, size_t);` | 函数 |

<a id="header-utmpx-h"></a>

### `<utmpx.h>`

用户会话记录的查询、遍历和更新。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/utmpx.h.html)，本节共 6 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`endutxent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/endutxent.html) | `void endutxent(void);` | 函数 |
| [`getutxent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getutxent.html) | `struct utmpx *getutxent(void);` | 函数 |
| [`getutxid`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getutxid.html) | `struct utmpx *getutxid(const struct utmpx *);` | 函数 |
| [`getutxline`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getutxline.html) | `struct utmpx *getutxline(const struct utmpx *);` | 函数 |
| [`pututxline`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pututxline.html) | `struct utmpx *pututxline(const struct utmpx *);` | 函数 |
| [`setutxent`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setutxent.html) | `void setutxent(void);` | 函数 |

<a id="header-wchar-h"></a>

### `<wchar.h>`

宽字符字符串、宽字符 I/O 和多字节编码转换。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/wchar.h.html)，本节共 76 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`btowc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/btowc.html) | `wint_t btowc(int);` | 函数 |
| [`fgetwc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fgetwc.html) | `wint_t fgetwc(FILE *);` | 函数 |
| [`fgetws`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fgetws.html) | `wchar_t *fgetws(wchar_t *restrict, int, FILE *restrict);` | 函数 |
| [`fputwc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fputwc.html) | `wint_t fputwc(wchar_t, FILE *);` | 函数 |
| [`fputws`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fputws.html) | `int fputws(const wchar_t *restrict, FILE *restrict);` | 函数 |
| [`fwide`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fwide.html) | `int fwide(FILE *, int);` | 函数 |
| [`fwprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fwprintf.html) | `int fwprintf(FILE *restrict, const wchar_t *restrict, ...);` | 函数 |
| [`fwscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fwscanf.html) | `int fwscanf(FILE *restrict, const wchar_t *restrict, ...);` | 函数 |
| [`getwc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getwc.html) | `wint_t getwc(FILE *);` | 函数 |
| [`getwchar`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getwchar.html) | `wint_t getwchar(void);` | 函数 |
| [`mbrlen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbrlen.html) | `size_t mbrlen(const char *restrict, size_t, mbstate_t *restrict);` | 函数 |
| [`mbrtowc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbrtowc.html) | `size_t mbrtowc(wchar_t *restrict, const char *restrict, size_t, mbstate_t *restrict);` | 函数 |
| [`mbsinit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbsinit.html) | `int mbsinit(const mbstate_t *);` | 函数 |
| [`mbsnrtowcs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbsnrtowcs.html) | `size_t mbsnrtowcs(wchar_t *restrict, const char **restrict, size_t, size_t, mbstate_t *restrict);` | 函数 |
| [`mbsrtowcs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mbsrtowcs.html) | `size_t mbsrtowcs(wchar_t *restrict, const char **restrict, size_t, mbstate_t *restrict);` | 函数 |
| [`open_wmemstream`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/open_wmemstream.html) | `FILE *open_wmemstream(wchar_t **, size_t *);` | 函数 |
| [`putwc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putwc.html) | `wint_t putwc(wchar_t, FILE *);` | 函数 |
| [`putwchar`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/putwchar.html) | `wint_t putwchar(wchar_t);` | 函数 |
| [`swprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/swprintf.html) | `int swprintf(wchar_t *restrict, size_t, const wchar_t *restrict, ...);` | 函数 |
| [`swscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/swscanf.html) | `int swscanf(const wchar_t *restrict, const wchar_t *restrict, ...);` | 函数 |
| [`ungetwc`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ungetwc.html) | `wint_t ungetwc(wint_t, FILE *);` | 函数 |
| [`vfwprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vfwprintf.html) | `int vfwprintf(FILE *restrict, const wchar_t *restrict, va_list);` | 函数 |
| [`vfwscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vfwscanf.html) | `int vfwscanf(FILE *restrict, const wchar_t *restrict, va_list);` | 函数 |
| [`vswprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vswprintf.html) | `int vswprintf(wchar_t *restrict, size_t, const wchar_t *restrict, va_list);` | 函数 |
| [`vswscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vswscanf.html) | `int vswscanf(const wchar_t *restrict, const wchar_t *restrict, va_list);` | 函数 |
| [`vwprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vwprintf.html) | `int vwprintf(const wchar_t *restrict, va_list);` | 函数 |
| [`vwscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vwscanf.html) | `int vwscanf(const wchar_t *restrict, va_list);` | 函数 |
| [`wcpcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcpcpy.html) | `wchar_t *wcpcpy(wchar_t *restrict, const wchar_t *restrict);` | 函数 |
| [`wcpncpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcpncpy.html) | `wchar_t *wcpncpy(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wcrtomb`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcrtomb.html) | `size_t wcrtomb(char *restrict, wchar_t, mbstate_t *restrict);` | 函数 |
| [`wcscasecmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscasecmp.html) | `int wcscasecmp(const wchar_t *, const wchar_t *);` | 函数 |
| [`wcscasecmp_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscasecmp_l.html) | `int wcscasecmp_l(const wchar_t *, const wchar_t *, locale_t);` | 函数 |
| [`wcscat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscat.html) | `wchar_t *wcscat(wchar_t *restrict, const wchar_t *restrict);` | 函数 |
| [`wcschr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcschr.html) | `wchar_t *wcschr(const wchar_t *, wchar_t);` | 函数 |
| [`wcscmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscmp.html) | `int wcscmp(const wchar_t *, const wchar_t *);` | 函数 |
| [`wcscoll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscoll.html) | `int wcscoll(const wchar_t *, const wchar_t *);` | 函数 |
| [`wcscoll_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscoll_l.html) | `int wcscoll_l(const wchar_t *, const wchar_t *, locale_t);` | 函数 |
| [`wcscpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscpy.html) | `wchar_t *wcscpy(wchar_t *restrict, const wchar_t *restrict);` | 函数 |
| [`wcscspn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcscspn.html) | `size_t wcscspn(const wchar_t *, const wchar_t *);` | 函数 |
| [`wcsdup`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsdup.html) | `wchar_t *wcsdup(const wchar_t *);` | 函数 |
| [`wcsftime`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsftime.html) | `size_t wcsftime(wchar_t *restrict, size_t, const wchar_t *restrict, const struct tm *restrict);` | 函数 |
| [`wcslcat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcslcat.html) | `size_t wcslcat(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wcslcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcslcpy.html) | `size_t wcslcpy(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wcslen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcslen.html) | `size_t wcslen(const wchar_t *);` | 函数 |
| [`wcsncasecmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsncasecmp.html) | `int wcsncasecmp(const wchar_t *, const wchar_t *, size_t);` | 函数 |
| [`wcsncasecmp_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsncasecmp_l.html) | `int wcsncasecmp_l(const wchar_t *, const wchar_t *, size_t, locale_t);` | 函数 |
| [`wcsncat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsncat.html) | `wchar_t *wcsncat(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wcsncmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsncmp.html) | `int wcsncmp(const wchar_t *, const wchar_t *, size_t);` | 函数 |
| [`wcsncpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsncpy.html) | `wchar_t *wcsncpy(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wcsnlen`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsnlen.html) | `size_t wcsnlen(const wchar_t *, size_t);` | 函数 |
| [`wcsnrtombs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsnrtombs.html) | `size_t wcsnrtombs(char *restrict, const wchar_t **restrict, size_t, size_t, mbstate_t *restrict);` | 函数 |
| [`wcspbrk`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcspbrk.html) | `wchar_t *wcspbrk(const wchar_t *, const wchar_t *);` | 函数 |
| [`wcsrchr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsrchr.html) | `wchar_t *wcsrchr(const wchar_t *, wchar_t);` | 函数 |
| [`wcsrtombs`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsrtombs.html) | `size_t wcsrtombs(char *restrict, const wchar_t **restrict, size_t, mbstate_t *restrict);` | 函数 |
| [`wcsspn`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsspn.html) | `size_t wcsspn(const wchar_t *, const wchar_t *);` | 函数 |
| [`wcsstr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsstr.html) | `wchar_t *wcsstr(const wchar_t *restrict, const wchar_t *restrict);` | 函数 |
| [`wcstod`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstod.html) | `double wcstod(const wchar_t *restrict, wchar_t **restrict);` | 函数 |
| [`wcstof`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstof.html) | `float wcstof(const wchar_t *restrict, wchar_t **restrict);` | 函数 |
| [`wcstok`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstok.html) | `wchar_t *wcstok(wchar_t *restrict, const wchar_t *restrict, wchar_t **restrict);` | 函数 |
| [`wcstol`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstol.html) | `long wcstol(const wchar_t *restrict, wchar_t **restrict, int);` | 函数 |
| [`wcstold`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstold.html) | `long double wcstold(const wchar_t *restrict, wchar_t **restrict);` | 函数 |
| [`wcstoll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstoll.html) | `long long wcstoll(const wchar_t *restrict, wchar_t **restrict, int);` | 函数 |
| [`wcstoul`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstoul.html) | `unsigned long wcstoul(const wchar_t *restrict, wchar_t **restrict, int);` | 函数 |
| [`wcstoull`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcstoull.html) | `unsigned long long wcstoull(const wchar_t *restrict, wchar_t **restrict, int);` | 函数 |
| [`wcswidth`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcswidth.html) | `int wcswidth(const wchar_t *, size_t);` | 函数 |
| [`wcsxfrm`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsxfrm.html) | `size_t wcsxfrm(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wcsxfrm_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcsxfrm_l.html) | `size_t wcsxfrm_l(wchar_t *restrict, const wchar_t *restrict, size_t, locale_t);` | 函数 |
| [`wctob`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wctob.html) | `int wctob(wint_t);` | 函数 |
| [`wcwidth`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wcwidth.html) | `int wcwidth(wchar_t);` | 函数 |
| [`wmemchr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wmemchr.html) | `wchar_t *wmemchr(const wchar_t *, wchar_t, size_t);` | 函数 |
| [`wmemcmp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wmemcmp.html) | `int wmemcmp(const wchar_t *, const wchar_t *, size_t);` | 函数 |
| [`wmemcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wmemcpy.html) | `wchar_t *wmemcpy(wchar_t *restrict, const wchar_t *restrict, size_t);` | 函数 |
| [`wmemmove`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wmemmove.html) | `wchar_t *wmemmove(wchar_t *, const wchar_t *, size_t);` | 函数 |
| [`wmemset`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wmemset.html) | `wchar_t *wmemset(wchar_t *, wchar_t, size_t);` | 函数 |
| [`wprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wprintf.html) | `int wprintf(const wchar_t *restrict, ...);` | 函数 |
| [`wscanf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wscanf.html) | `int wscanf(const wchar_t *restrict, ...);` | 函数 |

<a id="header-wctype-h"></a>

### `<wctype.h>`

宽字符分类、大小写转换及 locale 版本。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/wctype.h.html)，本节共 36 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`iswalnum`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswalnum.html) | `int iswalnum(wint_t);` | 函数 |
| [`iswalnum_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswalnum_l.html) | `int iswalnum_l(wint_t, locale_t);` | 函数 |
| [`iswalpha`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswalpha.html) | `int iswalpha(wint_t);` | 函数 |
| [`iswalpha_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswalpha_l.html) | `int iswalpha_l(wint_t, locale_t);` | 函数 |
| [`iswblank`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswblank.html) | `int iswblank(wint_t);` | 函数 |
| [`iswblank_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswblank_l.html) | `int iswblank_l(wint_t, locale_t);` | 函数 |
| [`iswcntrl`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswcntrl.html) | `int iswcntrl(wint_t);` | 函数 |
| [`iswcntrl_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswcntrl_l.html) | `int iswcntrl_l(wint_t, locale_t);` | 函数 |
| [`iswctype`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswctype.html) | `int iswctype(wint_t, wctype_t);` | 函数 |
| [`iswctype_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswctype_l.html) | `int iswctype_l(wint_t, wctype_t, locale_t);` | 函数 |
| [`iswdigit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswdigit.html) | `int iswdigit(wint_t);` | 函数 |
| [`iswdigit_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswdigit_l.html) | `int iswdigit_l(wint_t, locale_t);` | 函数 |
| [`iswgraph`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswgraph.html) | `int iswgraph(wint_t);` | 函数 |
| [`iswgraph_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswgraph_l.html) | `int iswgraph_l(wint_t, locale_t);` | 函数 |
| [`iswlower`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswlower.html) | `int iswlower(wint_t);` | 函数 |
| [`iswlower_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswlower_l.html) | `int iswlower_l(wint_t, locale_t);` | 函数 |
| [`iswprint`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswprint.html) | `int iswprint(wint_t);` | 函数 |
| [`iswprint_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswprint_l.html) | `int iswprint_l(wint_t, locale_t);` | 函数 |
| [`iswpunct`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswpunct.html) | `int iswpunct(wint_t);` | 函数 |
| [`iswpunct_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswpunct_l.html) | `int iswpunct_l(wint_t, locale_t);` | 函数 |
| [`iswspace`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswspace.html) | `int iswspace(wint_t);` | 函数 |
| [`iswspace_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswspace_l.html) | `int iswspace_l(wint_t, locale_t);` | 函数 |
| [`iswupper`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswupper.html) | `int iswupper(wint_t);` | 函数 |
| [`iswupper_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswupper_l.html) | `int iswupper_l(wint_t, locale_t);` | 函数 |
| [`iswxdigit`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswxdigit.html) | `int iswxdigit(wint_t);` | 函数 |
| [`iswxdigit_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/iswxdigit_l.html) | `int iswxdigit_l(wint_t, locale_t);` | 函数 |
| [`towctrans`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/towctrans.html) | `wint_t towctrans(wint_t, wctrans_t);` | 函数 |
| [`towctrans_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/towctrans_l.html) | `wint_t towctrans_l(wint_t, wctrans_t, locale_t);` | 函数 |
| [`towlower`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/towlower.html) | `wint_t towlower(wint_t);` | 函数 |
| [`towlower_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/towlower_l.html) | `wint_t towlower_l(wint_t, locale_t);` | 函数 |
| [`towupper`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/towupper.html) | `wint_t towupper(wint_t);` | 函数 |
| [`towupper_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/towupper_l.html) | `wint_t towupper_l(wint_t, locale_t);` | 函数 |
| [`wctrans`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wctrans.html) | `wctrans_t wctrans(const char *);` | 函数 |
| [`wctrans_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wctrans_l.html) | `wctrans_t wctrans_l(const char *, locale_t);` | 函数 |
| [`wctype`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wctype.html) | `wctype_t wctype(const char *);` | 函数 |
| [`wctype_l`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wctype_l.html) | `wctype_t wctype_l(const char *, locale_t);` | 函数 |

<a id="header-wordexp-h"></a>

### `<wordexp.h>`

按 shell 规则展开单词及释放结果。

[官方头文件定义](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/wordexp.h.html)，本节共 2 个接口。

| 接口及官方说明 | C 声明 / 调用形式 | 类别 |
|---|---|---|
| [`wordexp`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wordexp.html) | `int wordexp(const char *restrict, wordexp_t *restrict, int);` | 函数 |
| [`wordfree`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/wordfree.html) | `void wordfree(wordexp_t *);` | 函数 |

## 4. 没有新增独立函数名称的标准头文件

以下头文件提供类型、常量、对象式宏、其他函数式宏，或对已经列出的接口作补充定义，因此没有在上表中新增独立函数名称。

| 头文件 | 主要内容 |
|---|---|
| [`<cpio.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/cpio.h.html) | cpio 归档格式常量。 |
| [`<errno.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/errno.h.html) | 错误码及 errno 宏。 |
| [`<float.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/float.h.html) | 浮点类型的范围和精度常量。 |
| [`<iso646.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/iso646.h.html) | ISO C 运算符的替代拼写宏。 |
| [`<limits.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/limits.h.html) | 实现限制和整数范围常量。 |
| [`<netinet/in.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/netinet_in.h.html) | IPv4/IPv6 地址类型、常量和地址判断宏。 |
| [`<netinet/tcp.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/netinet_tcp.h.html) | TCP 协议相关定义。 |
| [`<stdalign.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdalign.h.html) | 对齐相关宏。 |
| [`<stdbool.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdbool.h.html) | 布尔类型和常量宏。 |
| [`<stddef.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stddef.h.html) | 基础类型和 offsetof 宏。 |
| [`<stdint.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdint.h.html) | 定宽整数类型、范围和整数常量宏。 |
| [`<stdnoreturn.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/stdnoreturn.h.html) | 不返回函数的声明宏。 |
| [`<sys/types.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_types.h.html) | POSIX 基础系统类型。 |
| [`<sys/un.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/sys_un.h.html) | UNIX 域套接字地址类型。 |
| [`<tar.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/tar.h.html) | tar 归档格式常量。 |
| [`<tgmath.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/tgmath.h.html) | 与 math.h/complex.h 对应的类型泛型数学宏。 |

其中，`<tgmath.h>` 为标准规定的数学函数提供同名类型泛型宏，名称可对照 `<math.h>`、`<complex.h>` 及该头文件的映射表；不为每个重载类型重复计数。`<stddef.h>` 的 `offsetof(type, member-designator)`、`<sys/stat.h>` 的 `S_IS*`/`S_TYPEIS*`、`<netinet/in.h>` 的 `IN6_IS_ADDR_*` 等宏也有调用形式；它们是头文件定义的辅助宏，不属于本文按 System Interfaces 索引统计的接口名称集合。常量宏、类型名、对象名和所有辅助宏不计入函数总数。

## 5. 官方函数索引中出现的标准对象与对象式宏

这些名称不是函数，不能在名称后加 `()` 调用。下表的类型说明省略了声明中的 `extern`。

| 名称及官方说明 | 相关头文件 | 类型 / 形式 | 用途 |
|---|---|---|---|
| [`daylight`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/daylight.html) | `<time.h>` | `int daylight;` | 夏令时信息；XSI 接口 |
| [`environ`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/environ.html) | `<unistd.h>` | `char **environ;` | 进程环境字符串数组；声明方式参见 exec 接口页 |
| [`errno`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/errno.html) | `<errno.h>` | `可修改的 int 左值宏` | 线程对应的错误指示值；仅在接口约定时检查 |
| [`getdate_err`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getdate_err.html) | `<time.h>` | `int getdate_err;` | getdate() 的错误信息；XSI 接口 |
| [`in6addr_any`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/in6addr_any.html) | `<netinet/in.h>` | `const struct in6_addr in6addr_any;` | IPv6 未指定地址 |
| [`in6addr_loopback`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/in6addr_loopback.html) | `<netinet/in.h>` | `const struct in6_addr in6addr_loopback;` | IPv6 回环地址 |
| [`optarg`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/optarg.html) | `<unistd.h>` | `char *optarg;` | getopt() 当前选项的参数 |
| [`opterr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/opterr.html) | `<unistd.h>` | `int opterr;` | 控制 getopt() 是否打印错误消息 |
| [`optind`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/optind.html) | `<unistd.h>` | `int optind;` | getopt() 的下一个参数下标 |
| [`optopt`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/optopt.html) | `<unistd.h>` | `int optopt;` | getopt() 检测到的错误选项字符 |
| [`signgam`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/signgam.html) | `<math.h>` | `int signgam;` | lgamma() 相关的符号信息；XSI 接口 |
| [`stderr`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stderr.html) | `<stdio.h>` | `FILE * 类型的宏表达式` | 标准错误流 |
| [`stdin`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stdin.html) | `<stdio.h>` | `FILE * 类型的宏表达式` | 标准输入流 |
| [`stdout`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stdout.html) | `<stdio.h>` | `FILE * 类型的宏表达式` | 标准输出流 |
| [`timezone`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/timezone.html) | `<time.h>` | `long timezone;` | 标准时间相对 UTC 的秒差；XSI 接口 |
| [`tzname`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tzname.html) | `<time.h>` | `char *tzname[2];` | 标准时间和夏令时的时区名称 |

## 6. 版本变化与常见扩展边界

### POSIX.1-2024 已标准化的常见接口

以下接口已在本版标准中，不应再仅按旧版标准的印象将其判为平台扩展：

[`accept4`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/accept4.html), [`asprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asprintf.html), [`vasprintf`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/vasprintf.html), [`dup3`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dup3.html), [`pipe2`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe2.html), [`getentropy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getentropy.html), [`reallocarray`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/reallocarray.html), [`strlcpy`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlcpy.html), [`strlcat`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strlcat.html), [`qsort_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/qsort_r.html), [`posix_getdents`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_getdents.html), [`tcgetwinsize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcgetwinsize.html), [`tcsetwinsize`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/tcsetwinsize.html)。

### 已移除的接口与历史索引项

官方字母索引仍保留 `asctime_r`、`ctime_r` 两个名称对应的链接，但链接后的当前接口页已不声明这两个函数；其 Issue 8 CHANGE HISTORY 明确记录 Austin Group Defect 1410 将它们移除。因此这两个名称不计入本版当前接口总数。

| 历史接口 | 当前依据 | 可采用的当前接口 |
|---|---|---|
| `asctime_r` | [asctime 的 Issue 8 变更记录](https://pubs.opengroup.org/onlinepubs/9799919799/functions/asctime.html) | 按需要使用 `strftime` |
| `ctime_r` | [ctime 的 Issue 8 变更记录](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ctime.html) | 按需要使用 `localtime_r` 与 `strftime` |

`usleep`、`vfork`、`getwd`、`bzero`、`rand_r` 等历史接口也不在本版当前函数集合中。迁移时应按各自语义选择 `nanosleep`、`posix_spawn`/`fork`、`getcwd`、`memset` 等当前接口；这些替代方案并非在所有情形下都能直接逐字替换。

### 平台或实现扩展示例

`epoll_*`、`inotify_*`、`eventfd`、`signalfd`、`timerfd_*`、`futex`、`memfd_create`、`getrandom`、`clearenv`、`pthread_setname_np`、`malloc_usable_size` 等不属于本清单所依据的 POSIX.1-2024 接口集合。使用它们时应根据目标平台另外处理可移植性。

## 7. 来源与完整性核对

1. [POSIX.1-2024 标准入口](https://pubs.opengroup.org/onlinepubs/9799919799/)。
2. [System Interfaces 字母索引](https://pubs.opengroup.org/onlinepubs/9799919799/idx/functions.html)：本次核对取得 1243 个名称。
3. [标准头文件索引](https://pubs.opengroup.org/onlinepubs/9799919799/idx/head.html)：核对全部 86 个标准头文件，以其中的声明建立分类和 C 声明。
4. [System Interfaces 目录](https://pubs.opengroup.org/onlinepubs/9799919799/functions/contents.html) 及各接口页：补充函数式宏和 `asprintf`、`vasprintf` 等声明，并确认历史接口移除记录。

数量核对：**1243 个索引名称 = 1225 个当前函数及函数式接口 + 16 个对象或对象式宏 + 2 个已移除的历史索引项**。本文件按接口名称逐项列出，`float`、`double`、`long double` 版本分别列名，没有使用 `pthread_*` 或 `str*` 等通配写法代替完整清单。

接口的参数语义、所有可选条件、返回值、错误码及线程/信号安全性，以链接中的标准正文为准。
