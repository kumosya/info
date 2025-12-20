# libc 库文档

## 1. 概述

本项目实现了一个轻量级的 C 标准库(libc)。本库遵循 C 标准接口规范，确保与大多数 C 代码的兼容性，同时针对内核环境进行了适当的优化和调整。

## 2. 目录结构

```
lib/
└── libc/
    ├── include/         # 头文件目录
    │   ├── assert.h     # 断言功能
    │   ├── ctype.h      # 字符处理函数
    │   ├── errno.h      # 错误处理
    │   ├── fcntl.h      # 文件控制
    │   ├── limits.h     # 类型限制
    │   ├── setjmp.h     # 非局部跳转
    │   ├── signal.h     # 信号处理
    │   ├── stdarg.h     # 可变参数支持
    │   ├── stdbool.h    # 布尔类型支持
    │   ├── stddef.h     # 标准定义
    │   ├── stdint.h     # 整数类型定义
    │   ├── stdio.h      # 标准输入输出
    │   ├── stdlib.h     # 标准库函数
    │   ├── string.h     # 字符串处理
    │   ├── sys/         # 系统头文件
    │   │   ├── socket.h # 套接字接口
    │   │   ├── stat.h   # 文件状态
    │   │   ├── types.h  # 系统类型定义
    │   │   └── wait.h   # 进程等待
    │   ├── time.h       # 时间处理
    │   ├── unistd.h     # 系统调用接口
    │   └── wchar.h      # 宽字符支持
    ├── src/             # 实现文件目录
    ├── build/           # 构建输出目录
    ├── CMakeLists.txt   # CMake 构建文件
    └── libc_documentation.md  # 本文档
```

## 3. 已实现的头文件和函数

### 3.1 string.h - 字符串和内存操作

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `void *memcpy(void *dest, const void *src, size_t n)` | 内存复制 | 已实现 |
| `void *memmove(void *dest, const void *src, size_t n)` | 内存移动（可重叠） | 已实现 |
| `void *memset(void *s, int c, size_t n)` | 内存设置 | 已实现 |
| `int memcmp(const void *s1, const void *s2, size_t n)` | 内存比较 | 已实现 |
| `void *memchr(const void *s, int c, size_t n)` | 内存查找字符 | 已实现 |
| `char *strcpy(char *dest, const char *src)` | 字符串复制 | 已实现 |
| `char *strncpy(char *dest, const char *src, size_t n)` | 字符串复制（指定长度） | 已实现 |
| `char *strcat(char *dest, const char *src)` | 字符串连接 | 已实现 |
| `char *strncat(char *dest, const char *src, size_t n)` | 字符串连接（指定长度） | 已实现 |
| `int strcmp(const char *s1, const char *s2)` | 字符串比较 | 已实现 |
| `int strncmp(const char *s1, const char *s2, size_t n)` | 字符串比较（指定长度） | 已实现 |
| `char *strchr(const char *s, int c)` | 字符串查找字符 | 已实现 |
| `char *strrchr(const char *s, int c)` | 字符串反向查找字符 | 已实现 |
| `size_t strlen(const char *s)` | 字符串长度 | 已实现 |
| `size_t strnlen(const char *s, size_t maxlen)` | 字符串长度（最大限制） | 已实现 |
| `char *strstr(const char *haystack, const char *needle)` | 字符串查找子串 | 已实现 |
| `char *strtok(char *str, const char *delim)` | 字符串分割 | 已实现 |
| `char *strtok_r(char *str, const char *delim, char **saveptr)` | 可重入字符串分割 | 已实现 |
| `char *strerror(int errnum)` | 错误号转字符串 | 已实现 |
| `int strcoll(const char *s1, const char *s2)` | 字符串比较（当前区域设置） | 已实现 |
| `size_t strxfrm(char *dest, const char *src, size_t n)` | 字符串转换（用于排序） | 已实现 |
| `void *memccpy(void *dest, const void *src, int c, size_t n)` | 内存复制直到指定字符 | 已实现 |
| `void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen)` | 内存查找子内存块 | 已实现 |

### 3.2 stdio.h - 标准输入输出

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `FILE *fopen(const char *path, const char *mode)` | 打开文件 | 已实现 |
| `int fclose(FILE *stream)` | 关闭文件 | 已实现 |
| `int fprintf(FILE *stream, const char *format, ...)` | 格式化输出到文件 | 已实现 |
| `int printf(const char *format, ...)` | 格式化输出到标准输出 | 已实现 |
| `int sprintf(char *str, const char *format, ...)` | 格式化输出到字符串 | 已实现 |
| `int snprintf(char *str, size_t size, const char *format, ...)` | 格式化输出到字符串（限制长度） | 已实现 |
| `int vfprintf(FILE *stream, const char *format, va_list ap)` | 可变参数格式化输出到文件 | 已实现 |
| `int vprintf(const char *format, va_list ap)` | 可变参数格式化输出到标准输出 | 已实现 |
| `int vsprintf(char *str, const char *format, va_list ap)` | 可变参数格式化输出到字符串 | 未实现 |
| `int vsnprintf(char *str, size_t size, const char *format, va_list ap)` | 可变参数格式化输出到字符串（限制长度） | 已实现 |
| `int fscanf(FILE *stream, const char *format, ...)` | 格式化从文件读取 | 未实现 |
| `int scanf(const char *format, ...)` | 格式化从标准输入读取 | 未实现 |
| `int sscanf(const char *str, const char *format, ...)` | 格式化从字符串读取 | 未实现 |
| `int vfscanf(FILE *stream, const char *format, va_list ap)` | 可变参数格式化从文件读取 | 未实现 |
| `int vscanf(const char *format, va_list ap)` | 可变参数格式化从标准输入读取 | 未实现 |
| `int vsscanf(const char *str, const char *format, va_list ap)` | 可变参数格式化从字符串读取 | 未实现 |
| `size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)` | 从文件读取二进制数据 | 未实现 |
| `size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)` | 向文件写入二进制数据 | 未实现 |
| `int fgetc(FILE *stream)` | 从文件读取字符 | 未实现 |
| `int fputc(int c, FILE *stream)` | 向文件写入字符 | 未实现 |
| `char *fgets(char *s, int size, FILE *stream)` | 从文件读取字符串 | 未实现 |
| `int fputs(const char *s, FILE *stream)` | 向文件写入字符串 | 未实现 |
| `int getc(FILE *stream)` | 从文件读取字符（宏） | 未实现 |
| `int putc(int c, FILE *stream)` | 向文件写入字符（宏） | 未实现 |
| `int getchar(void)` | 从标准输入读取字符 | 已实现 |
| `int putchar(int c)` | 向标准输出写入字符 | 已实现 |
| `char *gets(char *s)` | 从标准输入读取字符串（不安全） | 未实现 |
| `int puts(const char *s)` | 向标准输出写入字符串 | 未实现 |
| `int ungetc(int c, FILE *stream)` | 将字符退回输入流 | 未实现 |
| `void clearerr(FILE *stream)` | 清除文件错误标志 | 未实现 |
| `int feof(FILE *stream)` | 检查文件结束标志 | 未实现 |
| `int ferror(FILE *stream)` | 检查文件错误标志 | 未实现 |
| `int fflush(FILE *stream)` | 刷新文件缓冲区 | 未实现 |
| `int fseek(FILE *stream, long offset, int whence)` | 移动文件位置指针 | 未实现 |
| `long ftell(FILE *stream)` | 获取文件位置指针 | 未实现 |
| `void rewind(FILE *stream)` | 重置文件位置指针到开头 | 未实现 |
| `int fileno(FILE *stream)` | 获取文件描述符 | 未实现 |
| `FILE *fdopen(int fd, const char *mode)` | 将文件描述符转换为 FILE* | 未实现 |
| `int setvbuf(FILE *stream, char *buf, int mode, size_t size)` | 设置文件缓冲区 | 未实现 |
| `int fgetpos(FILE *stream, fpos_t *pos)` | 获取文件位置 | 未实现 |
| `int fsetpos(FILE *stream, const fpos_t *pos)` | 设置文件位置 | 未实现 |
| `FILE *freopen(const char *path, const char *mode, FILE *stream)` | 重新打开文件 | 未实现 |
| `int remove(const char *path)` | 删除文件或目录 | 未实现 |
| `int rename(const char *oldpath, const char *newpath)` | 重命名文件或目录 | 未实现 |
| `void perror(const char *s)` | 打印错误信息 | 已实现 |
| `FILE *tmpfile(void)` | 创建临时文件 | 未实现 |
| `char *tmpnam(char *s)` | 生成临时文件名 | 未实现 |
| `char *tempnam(const char *dir, const char *prefix)` | 生成临时文件名（指定目录和前缀） | 未实现 |

### 3.3 stdlib.h - 标准库函数

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `void *malloc(size_t size)` | 动态内存分配 | 已实现 |
| `void *calloc(size_t nmemb, size_t size)` | 分配并清零内存 | 未实现 |
| `void *realloc(void *ptr, size_t size)` | 重新分配内存 | 未实现 |
| `void free(void *ptr)` | 释放内存 | 已实现 |
| `int rand(void)` | 生成随机数 | 已实现 |
| `void srand(unsigned int seed)` | 设置随机数种子 | 已实现 |
| `int abs(int j)` | 整数绝对值 | 已实现 |
| `long labs(long int j)` | 长整数绝对值 | 已实现 |
| `long long llabs(long long int j)` | 长长整数绝对值 | 已实现 |
| `double atof(const char *nptr)` | 字符串转浮点数 | 已实现 |
| `int atoi(const char *nptr)` | 字符串转整数 | 已实现 |
| `long int atol(const char *nptr)` | 字符串转长整数 | 已实现 |
| `long long int atoll(const char *nptr)` | 字符串转长长整数 | 已实现 |
| `double strtod(const char *nptr, char **endptr)` | 字符串转双精度浮点数 | 已实现 |
| `float strtof(const char *nptr, char **endptr)` | 字符串转单精度浮点数 | 已实现 |
| `long double strtold(const char *nptr, char **endptr)` | 字符串转长双精度浮点数 | 已实现 |
| `long int strtol(const char *nptr, char **endptr, int base)` | 字符串转长整数（指定基数） | 已实现 |
| `long long int strtoll(const char *nptr, char **endptr, int base)` | 字符串转长长整数（指定基数） | 已实现 |
| `unsigned long int strtoul(const char *nptr, char **endptr, int base)` | 字符串转无符号长整数（指定基数） | 已实现 |
| `unsigned long long int strtoull(const char *nptr, char **endptr, int base)` | 字符串转无符号长长整数（指定基数） | 已实现 |
| `void abort(void)` | 异常终止程序 | 已实现 |
| `int atexit(void (*func)(void))` | 注册程序终止函数 | 已实现 |
| `void exit(int status)` | 正常终止程序 | 已实现 |
| `int at_quick_exit(void (*func)(void))` | 注册快速退出函数 | 已实现 |
| `void quick_exit(int status)` | 快速终止程序 | 已实现 |
| `char *getenv(const char *name)` | 获取环境变量 | 已实现 |
| `int system(const char *command)` | 执行系统命令 | 已实现 |
| `void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))` | 二分查找 | 已实现 |
| `void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))` | 快速排序 | 已实现 |
| `div_t div(int numer, int denom)` | 整数除法（返回商和余数） | 已实现 |
| `ldiv_t ldiv(long int numer, long int denom)` | 长整数除法（返回商和余数） | 已实现 |
| `lldiv_t lldiv(long long int numer, long long int denom)` | 长长整数除法（返回商和余数） | 已实现 |

### 3.4 ctype.h - 字符处理

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `int isalnum(int c)` | 检查是否为字母或数字 | 已实现 |
| `int isalpha(int c)` | 检查是否为字母 | 已实现 |
| `int isblank(int c)` | 检查是否为空白字符（空格或制表符） | 已实现 |
| `int iscntrl(int c)` | 检查是否为控制字符 | 已实现 |
| `int isdigit(int c)` | 检查是否为数字 | 已实现 |
| `int isgraph(int c)` | 检查是否为可打印字符（非空格） | 已实现 |
| `int islower(int c)` | 检查是否为小写字母 | 已实现 |
| `int isprint(int c)` | 检查是否为可打印字符 | 已实现 |
| `int ispunct(int c)` | 检查是否为标点符号 | 已实现 |
| `int isspace(int c)` | 检查是否为空白字符 | 已实现 |
| `int isupper(int c)` | 检查是否为大写字母 | 已实现 |
| `int isxdigit(int c)` | 检查是否为十六进制数字 | 已实现 |
| `int tolower(int c)` | 转换为小写字母 | 已实现 |
| `int toupper(int c)` | 转换为大写字母 | 已实现 |

### 3.5 wchar.h - 宽字符支持

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `wchar_t *wcscpy(wchar_t *dest, const wchar_t *src)` | 宽字符串复制 | 已实现 |
| `wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n)` | 宽字符串复制（指定长度） | 已实现 |
| `wchar_t *wcscat(wchar_t *dest, const wchar_t *src)` | 宽字符串连接 | 已实现 |
| `wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n)` | 宽字符串连接（指定长度） | 已实现 |
| `int wcscmp(const wchar_t *s1, const wchar_t *s2)` | 宽字符串比较 | 已实现 |
| `int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n)` | 宽字符串比较（指定长度） | 已实现 |
| `size_t wcslen(const wchar_t *s)` | 宽字符串长度 | 已实现 |
| `wchar_t *wcschr(const wchar_t *s, wchar_t c)` | 宽字符串查找字符 | 已实现 |
| `wchar_t *wcsrchr(const wchar_t *s, wchar_t c)` | 宽字符串反向查找字符 | 已实现 |
| `wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle)` | 宽字符串查找子串 | 已实现 |
| `int wprintf(const wchar_t *format, ...)` | 宽字符格式化输出 | 已实现 |
| `int fwprintf(FILE *stream, const wchar_t *format, ...)` | 宽字符格式化输出到文件 | 已实现 |
| `int swprintf(wchar_t *str, size_t size, const wchar_t *format, ...)` | 宽字符格式化输出到宽字符串 | 已实现 |
| `int vwprintf(const wchar_t *format, va_list ap)` | 可变参数宽字符格式化输出 | 已实现 |
| `int vfwprintf(FILE *stream, const wchar_t *format, va_list ap)` | 可变参数宽字符格式化输出到文件 | 已实现 |
| `int vswprintf(wchar_t *str, size_t size, const wchar_t *format, va_list ap)` | 可变参数宽字符格式化输出到宽字符串 | 已实现 |
| `int wscanf(const wchar_t *format, ...)` | 宽字符格式化输入 | 已实现 |
| `int fwscanf(FILE *stream, const wchar_t *format, ...)` | 宽字符格式化输入从文件 | 已实现 |
| `int swscanf(const wchar_t *str, const wchar_t *format, ...)` | 宽字符格式化输入从宽字符串 | 已实现 |
| `int vwscanf(const wchar_t *format, va_list ap)` | 可变参数宽字符格式化输入 | 已实现 |
| `int vfwscanf(FILE *stream, const wchar_t *format, va_list ap)` | 可变参数宽字符格式化输入从文件 | 已实现 |
| `int vswscanf(const wchar_t *str, const wchar_t *format, va_list ap)` | 可变参数宽字符格式化输入从宽字符串 | 已实现 |
| `wint_t fgetwc(FILE *stream)` | 从文件读取宽字符 | 已实现 |
| `wint_t fputwc(wchar_t wc, FILE *stream)` | 向文件写入宽字符 | 已实现 |
| `wint_t getwc(FILE *stream)` | 从文件读取宽字符（宏） | 已实现 |
| `wint_t putwc(wchar_t wc, FILE *stream)` | 向文件写入宽字符（宏） | 已实现 |
| `wint_t getwchar(void)` | 从标准输入读取宽字符 | 已实现 |
| `wint_t putwchar(wchar_t wc)` | 向标准输出写入宽字符 | 已实现 |
| `wchar_t *fgetws(wchar_t *s, int n, FILE *stream)` | 从文件读取宽字符串 | 已实现 |
| `int fputws(const wchar_t *s, FILE *stream)` | 向文件写入宽字符串 | 已实现 |
| `wint_t ungetwc(wint_t wc, FILE *stream)` | 将宽字符退回输入流 | 已实现 |
| `int wcsftime(wchar_t *s, size_t maxsize, const wchar_t *format, const struct tm *timeptr)` | 宽字符时间格式化 | 已实现 |
| `size_t wcstombs(char *dest, const wchar_t *src, size_t max)` | 宽字符串转多字节字符串 | 已实现 |
| `size_t mbstowcs(wchar_t *dest, const char *src, size_t max)` | 多字节字符串转宽字符串 | 已实现 |
| `int wcrtomb(char *s, wchar_t wc, mbstate_t *ps)` | 宽字符转多字节字符 | 已实现 |
| `wint_t mbrtowc(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps)` | 多字节字符转宽字符 | 已实现 |
| `size_t mbrlen(const char *s, size_t n, mbstate_t *ps)` | 获取多字节字符长度 | 已实现 |
| `size_t mbsrtowcs(wchar_t *dest, const char **src, size_t len, mbstate_t *ps)` | 多字节字符串转宽字符串（可恢复） | 已实现 |
| `size_t wcsrtombs(char *dest, const wchar_t **src, size_t len, mbstate_t *ps)` | 宽字符串转多字节字符串（可恢复） | 已实现 |
| `int wcscoll(const wchar_t *s1, const wchar_t *s2)` | 宽字符串比较（当前区域设置） | 已实现 |
| `size_t wcsxfrm(wchar_t *dest, const wchar_t *src, size_t n)` | 宽字符串转换（用于排序） | 已实现 |
| `wchar_t *wcstok(wchar_t *str, const wchar_t *delim, wchar_t **ptr)` | 宽字符串分割 | 已实现 |
| `wchar_t *wcsdup(const wchar_t *s)` | 宽字符串复制（动态分配内存） | 已实现 |
| `void *wmemcpy(void *dest, const void *src, size_t n)` | 宽内存复制 | 已实现 |
| `void *wmemmove(void *dest, const void *src, size_t n)` | 宽内存移动（可重叠） | 已实现 |
| `int wmemcmp(const void *s1, const void *s2, size_t n)` | 宽内存比较 | 已实现 |
| `void *wmemchr(const void *s, wchar_t c, size_t n)` | 宽内存查找字符 | 已实现 |
| `void *wmemset(void *s, wchar_t c, size_t n)` | 宽内存设置 | 已实现 |

### 3.6 unistd.h - 系统调用接口

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `int access(const char *pathname, int mode)` | 检查文件访问权限 | 已实现 |
| `unsigned int alarm(unsigned int seconds)` | 设置闹钟信号 | 已实现 |
| `int chdir(const char *path)` | 更改当前工作目录 | 已实现 |
| `int chown(const char *pathname, uid_t owner, gid_t group)` | 更改文件所有者和组 | 已实现 |
| `int close(int fd)` | 关闭文件描述符 | 已实现 |
| `char *crypt(const char *key, const char *salt)` | 密码加密 | 已实现 |
| `int dup(int oldfd)` | 复制文件描述符 | 已实现 |
| `int dup2(int oldfd, int newfd)` | 复制文件描述符到指定文件描述符 | 已实现 |
| `void _exit(int status)` | 终止进程（不清理） | 已实现 |
| `int execve(const char *filename, char *const argv[], char *const envp[])` | 执行程序 | 已实现 |
| `pid_t fork(void)` | 创建子进程 | 已实现 |
| `long fpathconf(int fd, int name)` | 获取文件相关配置限制 | 已实现 |
| `int fsync(int fd)` | 将文件同步到磁盘 | 已实现 |
| `int ftruncate(int fd, off_t length)` | 截断文件 | 已实现 |
| `char *getcwd(char *buf, size_t size)` | 获取当前工作目录 | 已实现 |
| `gid_t getegid(void)` | 获取有效组ID | 已实现 |
| `uid_t geteuid(void)` | 获取有效用户ID | 已实现 |
| `gid_t getgid(void)` | 获取组ID | 已实现 |
| `int getgroups(int size, gid_t list[])` | 获取附加组ID列表 | 已实现 |
| `pid_t getpgrp(void)` | 获取进程组ID | 已实现 |
| `pid_t getpid(void)` | 获取进程ID | 已实现 |
| `pid_t getppid(void)` | 获取父进程ID | 已实现 |
| `uid_t getuid(void)` | 获取用户ID | 已实现 |
| `int isatty(int fd)` | 检查文件描述符是否关联到终端 | 已实现 |
| `int link(const char *oldpath, const char *newpath)` | 创建硬链接 | 已实现 |
| `off_t lseek(int fd, off_t offset, int whence)` | 移动文件读写指针 | 已实现 |
| `int nice(int inc)` | 调整进程优先级 | 已实现 |
| `long pathconf(const char *path, int name)` | 获取路径相关配置限制 | 已实现 |
| `int pause(void)` | 挂起进程直到信号到达 | 已实现 |
| `ssize_t read(int fd, void *buf, size_t count)` | 从文件描述符读取 | 已实现 |
| `int rmdir(const char *pathname)` | 删除空目录 | 已实现 |
| `int setgid(gid_t gid)` | 设置组ID | 已实现 |
| `int setpgid(pid_t pid, pid_t pgid)` | 设置进程组ID | 已实现 |
| `pid_t setsid(void)` | 创建会话 | 已实现 |
| `int setuid(uid_t uid)` | 设置用户ID | 已实现 |
| `unsigned int sleep(unsigned int seconds)` | 休眠指定秒数 | 已实现 |
| `void *sbrk(intptr_t increment)` | 调整数据段大小 | 已实现 |
| `int symlink(const char *target, const char *linkpath)` | 创建符号链接 | 已实现 |
| `int sync(void)` | 同步所有文件系统缓冲区 | 已实现 |
| `int truncate(const char *path, off_t length)` | 截断文件 | 已实现 |
| `char *ttyname(int fd)` | 获取终端名称 | 已实现 |
| `int unlink(const char *pathname)` | 删除文件 | 已实现 |
| `ssize_t write(int fd, const void *buf, size_t count)` | 向文件描述符写入 | 已实现 |

### 3.7 sys/types.h - 系统类型定义

| 类型/宏 | 功能描述 | 实现状态 |
|---------|----------|----------|
| `typedef ... clock_t` | 时钟类型 | 已实现 |
| `typedef ... time_t` | 时间类型 | 已实现 |
| `typedef ... suseconds_t` | 微秒类型 | 已实现 |
| `typedef ... useconds_t` | 无符号微秒类型 | 已实现 |
| `typedef ... dev_t` | 设备类型 | 已实现 |
| `typedef ... ino_t` | 索引节点类型 | 已实现 |
| `typedef ... mode_t` | 文件模式类型 | 已实现 |
| `typedef ... nlink_t` | 链接计数类型 | 已实现 |
| `typedef ... uid_t` | 用户ID类型 | 已实现 |
| `typedef ... gid_t` | 组ID类型 | 已实现 |
| `typedef ... off_t` | 文件偏移类型 | 已实现 |
| `typedef ... blksize_t` | 块大小类型 | 已实现 |
| `typedef ... blkcnt_t` | 块计数类型 | 已实现 |
| `typedef ... pid_t` | 进程ID类型 | 已实现 |
| `typedef ... key_t` | IPC键类型 | 已实现 |
| `typedef ... ssize_t` | 有符号大小类型 | 已实现 |
| `typedef ... socklen_t` | 套接字长度类型 | 已实现 |
| `typedef ... sigset_t` | 信号集类型 | 已实现 |

### 3.8 sys/stat.h - 文件状态

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `int chmod(const char *pathname, mode_t mode)` | 更改文件权限 | 已实现 |
| `int fchmod(int fd, mode_t mode)` | 更改文件描述符权限 | 已实现 |
| `int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags)` | 相对目录更改文件权限 | 已实现 |
| `int fstat(int fd, struct stat *statbuf)` | 获取文件描述符状态 | 已实现 |
| `int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags)` | 相对目录获取文件状态 | 已实现 |
| `int lstat(const char *pathname, struct stat *statbuf)` | 获取符号链接状态 | 已实现 |
| `int mkdir(const char *pathname, mode_t mode)` | 创建目录 | 已实现 |
| `int mkdirat(int dirfd, const char *pathname, mode_t mode)` | 相对目录创建目录 | 已实现 |
| `int mkfifo(const char *pathname, mode_t mode)` | 创建FIFO | 已实现 |
| `int mkfifoat(int dirfd, const char *pathname, mode_t mode)` | 相对目录创建FIFO | 已实现 |
| `int mknod(const char *pathname, mode_t mode, dev_t dev)` | 创建特殊文件 | 已实现 |
| `int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev)` | 相对目录创建特殊文件 | 已实现 |
| `int stat(const char *pathname, struct stat *statbuf)` | 获取文件状态 | 已实现 |
| `mode_t umask(mode_t mask)` | 设置文件权限掩码 | 已实现 |

### 3.9 sys/socket.h - 套接字接口

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)` | 接受连接 | 已实现 |
| `int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)` | 绑定地址 | 已实现 |
| `int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)` | 连接套接字 | 已实现 |
| `int getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)` | 获取对等方地址 | 已实现 |
| `int getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)` | 获取套接字地址 | 已实现 |
| `int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)` | 获取套接字选项 | 已实现 |
| `int listen(int sockfd, int backlog)` | 监听连接 | 已实现 |
| `ssize_t recv(int sockfd, void *buf, size_t len, int flags)` | 接收数据 | 已实现 |
| `ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)` | 从指定地址接收数据 | 已实现 |
| `ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)` | 接收消息 | 已实现 |
| `ssize_t send(int sockfd, const void *buf, size_t len, int flags)` | 发送数据 | 已实现 |
| `ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)` | 发送数据到指定地址 | 已实现 |
| `ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)` | 发送消息 | 已实现 |
| `int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)` | 设置套接字选项 | 已实现 |
| `int shutdown(int sockfd, int how)` | 关闭套接字的一部分 | 已实现 |
| `int sockatmark(int sockfd)` | 检查套接字标记 | 已实现 |
| `int socket(int domain, int type, int protocol)` | 创建套接字 | 已实现 |
| `int socketpair(int domain, int type, int protocol, int sv[2])` | 创建套接字对 | 已实现 |

### 3.10 sys/wait.h - 进程等待

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `pid_t wait(int *wstatus)` | 等待子进程终止 | 已实现 |
| `pid_t waitpid(pid_t pid, int *wstatus, int options)` | 等待指定子进程终止 | 已实现 |
| `pid_t waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options)` | 等待进程终止（扩展） | 已实现 |
| `int wait3(int *wstatus, int options, struct rusage *rusage)` | 等待子进程终止（带资源使用信息） | 已实现 |
| `int wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage)` | 等待指定子进程终止（带资源使用信息） | 已实现 |

### 3.11 fcntl.h - 文件控制

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `int open(const char *pathname, int flags, ...)` | 打开文件 | 已实现 |
| `int openat(int dirfd, const char *pathname, int flags, ...)` | 相对目录打开文件 | 已实现 |
| `int creat(const char *pathname, mode_t mode)` | 创建文件 | 已实现 |
| `int fcntl(int fd, int cmd, ...)` | 文件控制操作 | 已实现 |

### 3.12 signal.h - 信号处理

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `void (*signal(int signum, void (*handler)(int)))(int)` | 设置信号处理函数 | 已实现 |
| `int raise(int sig)` | 向进程发送信号 | 已实现 |
| `int kill(pid_t pid, int sig)` | 向进程发送信号 | 已实现 |
| `int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)` | 设置信号处理动作 | 已实现 |
| `int sigaddset(sigset_t *set, int signum)` | 将信号添加到信号集 | 已实现 |
| `int sigdelset(sigset_t *set, int signum)` | 从信号集删除信号 | 已实现 |
| `int sigemptyset(sigset_t *set)` | 清空信号集 | 已实现 |
| `int sigfillset(sigset_t *set)` | 填充信号集 | 已实现 |
| `int sigismember(const sigset_t *set, int signum)` | 检查信号是否在信号集中 | 已实现 |
| `int sigpending(sigset_t *set)` | 获取待处理信号集 | 已实现 |
| `int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)` | 修改进程信号掩码 | 已实现 |
| `int sigsuspend(const sigset_t *mask)` | 挂起进程直到信号到达 | 已实现 |
| `int sigwait(const sigset_t *set, int *sig)` | 等待信号集中的任一信号 | 已实现 |
| `int sigwaitinfo(const sigset_t *set, siginfo_t *info)` | 等待信号集中的任一信号（带信息） | 已实现 |
| `int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout)` | 等待信号（带超时） | 已实现 |
| `int pthread_kill(pthread_t thread, int sig)` | 向线程发送信号 | 已实现 |
| `int pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)` | 修改线程信号掩码 | 已实现 |

### 3.13 setjmp.h - 非局部跳转

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `int setjmp(jmp_buf env)` | 设置跳转点 | 已实现 |
| `void longjmp(jmp_buf env, int val)` | 跳转到setjmp设置的点 | 已实现 |
| `int sigsetjmp(sigjmp_buf env, int savesigs)` | 设置带信号掩码的跳转点 | 已实现 |
| `void siglongjmp(sigjmp_buf env, int val)` | 跳转到sigsetjmp设置的点 | 已实现 |

### 3.14 time.h - 时间处理

| 函数名 | 功能描述 | 实现状态 |
|--------|----------|----------|
| `time_t time(time_t *tloc)` | 获取当前时间 | 已实现 |
| `double difftime(time_t time1, time_t time0)` | 计算时间差 | 已实现 |
| `time_t mktime(struct tm *timeptr)` | 将分解时间转换为日历时间 | 已实现 |
| `struct tm *gmtime(const time_t *timeptr)` | 将日历时间转换为UTC分解时间 | 已实现 |
| `struct tm *localtime(const time_t *timeptr)` | 将日历时间转换为本地分解时间 | 已实现 |
| `char *asctime(const struct tm *timeptr)` | 将分解时间转换为字符串 | 已实现 |
| `char *ctime(const time_t *timeptr)` | 将日历时间转换为字符串 | 已实现 |
| `size_t strftime(char *s, size_t maxsize, const char *format, const struct tm *timeptr)` | 格式化时间字符串 | 已实现 |
| `clock_t clock(void)` | 测量程序使用的处理器时间 | 已实现 |
| `int nanosleep(const struct timespec *req, struct timespec *rem)` | 高精度休眠 | 已实现 |

### 3.15 assert.h - 断言功能

| 宏/函数 | 功能描述 | 实现状态 |
|---------|----------|----------|
| `assert(condition)` | 断言宏，条件为假时终止程序 | 已实现 |
| `void __assert_fail(const char *condition, const char *file, int line, const char *function)` | 断言失败处理函数 | 已实现 |

### 3.16 errno.h - 错误处理

| 变量/宏 | 功能描述 | 实现状态 |
|---------|----------|----------|
| `extern int errno` | 全局错误号变量 | 已实现 |
| `EPERM` | 操作不允许 | 已实现 |
| `ENOENT` | 没有此文件或目录 | 已实现 |
| `ESRCH` | 没有此进程 | 已实现 |
| `EINTR` | 被信号中断的系统调用 | 已实现 |
| `EIO` | I/O错误 | 已实现 |
| `ENXIO` | 没有此设备或地址 | 已实现 |
| `E2BIG` | 参数列表过长 | 已实现 |
| `ENOEXEC` | 可执行格式错误 | 已实现 |
| `EBADF` | 错误的文件描述符 | 已实现 |
| `ECHILD` | 没有子进程 | 已实现 |
| `EAGAIN` | 资源暂时不可用 | 已实现 |
| `ENOMEM` | 无法分配内存 | 已实现 |
| `EACCES` | 权限被拒绝 | 已实现 |
| `EFAULT` | 错误的地址 | 已实现 |
| `EBUSY` | 设备或资源忙 | 已实现 |
| `EEXIST` | 文件已存在 | 已实现 |
| `EXDEV` | 跨设备链接 | 已实现 |
| `ENODEV` | 没有此设备 | 已实现 |
| `ENOTDIR` | 不是目录 | 已实现 |
| `EISDIR` | 是目录 | 已实现 |
| `EINVAL` | 无效参数 | 已实现 |
| `ENFILE` | 文件表溢出 | 已实现 |
| `EMFILE` | 打开的文件过多 | 已实现 |
| `ENOTTY` | 不适用于设备的ioctl | 已实现 |
| `EFBIG` | 文件过大 | 已实现 |
| `ENOSPC` | 设备上没有剩余空间 | 已实现 |
| `ESPIPE` | 非法查找 | 已实现 |
| `EROFS` | 只读文件系统 | 已实现 |
| `EMLINK` | 链接过多 | 已实现 |
| `EPIPE` | 管道损坏 | 已实现 |
| `EDOM` | 数学函数参数超出定义域 | 已实现 |
| `ERANGE` | 结果超出范围 | 已实现 |

### 3.17 limits.h - 类型限制

| 宏 | 功能描述 | 实现状态 |
|----|----------|----------|
| `CHAR_BIT` | char类型的位数 | 已实现 |
| `SCHAR_MIN` | signed char类型的最小值 | 已实现 |
| `SCHAR_MAX` | signed char类型的最大值 | 已实现 |
| `UCHAR_MAX` | unsigned char类型的最大值 | 已实现 |
| `CHAR_MIN` | char类型的最小值 | 已实现 |
| `CHAR_MAX` | char类型的最大值 | 已实现 |
| `MB_LEN_MAX` | 多字节字符的最大字节数 | 已实现 |
| `SHRT_MIN` | short类型的最小值 | 已实现 |
| `SHRT_MAX` | short类型的最大值 | 已实现 |
| `USHRT_MAX` | unsigned short类型的最大值 | 已实现 |
| `INT_MIN` | int类型的最小值 | 已实现 |
| `INT_MAX` | int类型的最大值 | 已实现 |
| `UINT_MAX` | unsigned int类型的最大值 | 已实现 |
| `LONG_MIN` | long类型的最小值 | 已实现 |
| `LONG_MAX` | long类型的最大值 | 已实现 |
| `ULONG_MAX` | unsigned long类型的最大值 | 已实现 |
| `LLONG_MIN` | long long类型的最小值 | 已实现 |
| `LLONG_MAX` | long long类型的最大值 | 已实现 |
| `ULLONG_MAX` | unsigned long long类型的最大值 | 已实现 |

### 3.18 stdarg.h - 可变参数支持

| 类型/宏 | 功能描述 | 实现状态 |
|---------|----------|----------|
| `va_list` | 可变参数列表类型 | 已实现 |
| `va_start(ap, last)` | 初始化可变参数列表 | 已实现 |
| `va_arg(ap, type)` | 获取下一个参数 | 已实现 |
| `va_end(ap)` | 结束可变参数处理 | 已实现 |
| `va_copy(dest, src)` | 复制可变参数列表 | 已实现 |

### 3.19 stdbool.h - 布尔类型支持

| 宏 | 功能描述 | 实现状态 |
|----|----------|----------|
| `bool` | 布尔类型（_Bool的别名） | 已实现 |
| `true` | 真值（1） | 已实现 |
| `false` | 假值（0） | 已实现 |
| `__bool_true_false_are_defined` | 指示此头文件已包含 | 已实现 |

### 3.20 stddef.h - 标准定义

| 类型/宏 | 功能描述 | 实现状态 |
|---------|----------|----------|
| `NULL` | 空指针常量 | 已实现 |
| `size_t` | 无符号整数类型，用于表示大小 | 已实现 |
| `ptrdiff_t` | 有符号整数类型，用于指针差值 | 已实现 |
| `wchar_t` | 宽字符类型 | 已实现 |
| `offsetof(type, member)` | 获取结构体成员的偏移量 | 已实现 |
| `container_of(ptr, type, member)` | 通过成员指针获取结构体指针 | 已实现 |

### 3.21 stdint.h - 整数类型定义

| 类型/宏 | 功能描述 | 实现状态 |
|---------|----------|----------|
| `int8_t` | 8位有符号整数类型 | 已实现 |
| `int16_t` | 16位有符号整数类型 | 已实现 |
| `int32_t` | 32位有符号整数类型 | 已实现 |
| `int64_t` | 64位有符号整数类型 | 已实现 |
| `uint8_t` | 8位无符号整数类型 | 已实现 |
| `uint16_t` | 16位无符号整数类型 | 已实现 |
| `uint32_t` | 32位无符号整数类型 | 已实现 |
| `uint64_t` | 64位无符号整数类型 | 已实现 |
| `int_least8_t` | 至少8位的最小有符号整数类型 | 已实现 |
| `int_least16_t` | 至少16位的最小有符号整数类型 | 已实现 |
| `int_least32_t` | 至少32位的最小有符号整数类型 | 已实现 |
| `int_least64_t` | 至少64位的最小有符号整数类型 | 已实现 |
| `uint_least8_t` | 至少8位的最小无符号整数类型 | 已实现 |
| `uint_least16_t` | 至少16位的最小无符号整数类型 | 已实现 |
| `uint_least32_t` | 至少32位的最小无符号整数类型 | 已实现 |
| `uint_least64_t` | 至少64位的最小无符号整数类型 | 已实现 |
| `int_fast8_t` | 至少8位的最快有符号整数类型 | 已实现 |
| `int_fast16_t` | 至少16位的最快有符号整数类型 | 已实现 |
| `int_fast32_t` | 至少32位的最快有符号整数类型 | 已实现 |
| `int_fast64_t` | 至少64位的最快有符号整数类型 | 已实现 |
| `uint_fast8_t` | 至少8位的最快无符号整数类型 | 已实现 |
| `uint_fast16_t` | 至少16位的最快无符号整数类型 | 已实现 |
| `uint_fast32_t` | 至少32位的最快无符号整数类型 | 已实现 |
| `uint_fast64_t` | 至少64位的最快无符号整数类型 | 已实现 |
| `intptr_t` | 能够保存指针值的有符号整数类型 | 已实现 |
| `uintptr_t` | 能够保存指针值的无符号整数类型 | 已实现 |
| `intmax_t` | 最大的有符号整数类型 | 已实现 |
| `uintmax_t` | 最大的无符号整数类型 | 已实现 |
| 各种类型限制宏 | 如INT8_MIN, INT8_MAX, UINT8_MAX等 | 已实现 |

## 4. 系统 API 占位实现

以下系统 API 目前仅提供了占位实现，在实际内核中需要根据具体的内核实现进行适配：

### 4.1 文件操作

| API | 功能描述 | 状态 |
|-----|----------|------|
| open, close, read, write | 基础文件操作 | 占位实现 |
| lseek, stat, fstat, chmod | 文件定位和状态操作 | 占位实现 |
| mkdir, rmdir, unlink, rename | 文件系统操作 | 占位实现 |

### 4.2 进程控制

| API | 功能描述 | 状态 |
|-----|----------|------|
| fork, execve, wait, exit | 进程创建和控制 | 占位实现 |
| getpid, getppid, getuid, getgid | 进程和用户ID获取 | 占位实现 |
| nice, sleep, alarm | 进程调度和时间相关 | 占位实现 |

### 4.3 内存管理

| API | 功能描述 | 状态 |
|-----|----------|------|
| malloc, free, realloc, calloc | 动态内存分配 | 基础实现 |
| sbrk | 内存段调整 | 占位实现 |

### 4.4 信号处理

| API | 功能描述 | 状态 |
|-----|----------|------|
| signal, raise, kill | 基本信号操作 | 占位实现 |
| sigaction, sigprocmask, sigsuspend | 高级信号处理 | 占位实现 |

### 4.5 时间管理

| API | 功能描述 | 状态 |
|-----|----------|------|
| time, clock, gettimeofday | 时间获取函数 | 占位实现 |
| sleep, usleep, nanosleep | 休眠函数 | 占位实现 |

### 4.6 网络相关

| API | 功能描述 | 状态 |
|-----|----------|------|
| socket, bind, listen, accept | 套接字操作 | 占位实现 |
| connect, send, recv, close | 连接和数据传输 | 占位实现 |
| setsockopt, getsockopt | 套接字选项 | 占位实现 |

### 4.7 环境变量

| API | 功能描述 | 状态 |
|-----|----------|------|
| getenv, setenv, unsetenv | 环境变量操作 | 占位实现 |

## 5. 未完成的任务

以下是尚未实现或需要进一步完善的功能：

### 5.1 标准 I/O 库

- 完善缓冲机制
- 实现临时文件处理
- 支持宽字符 I/O

### 5.2 数学函数库

- 实现基本数学函数（sin, cos, tan 等）
- 实现高级数学函数（对数、指数等）
- 支持浮点数运算

### 5.3 多线程支持

- 实现 pthread 接口
- 支持线程同步原语（mutex, semaphore 等）
- 线程局部存储

### 5.4 完整的错误处理

- 扩展错误码定义
- 增强错误信息
- 提供调试支持

### 5.5 区域设置支持

- 实现基本的区域设置功能
- 支持不同字符编码
- 本地化字符串和数值格式化

### 5.6 动态加载

- 实现 dlopen, dlsym, dlclose 等接口
- 支持动态库加载和符号解析

### 5.7 安全功能

- 实现内存安全检查
- 提供安全字符串操作函数
- 输入验证和边界检查

## 6. 构建说明

### 6.1 编译选项

libc 库使用 CMake 构建系统，支持多种编译选项：

- `LIBC_DEBUG`: 启用调试信息和额外的检查
- `LIBC_NO_BUILTINS`: 禁用编译器内置函数，强制使用库实现
- `LIBC_DISABLE_ASSERTS`: 禁用断言检查，减小库体积
- `LIBC_MULTITHREAD_SUPPORT`: 启用多线程支持（当前未实现）

### 6.2 构建步骤

在 libc 目录下执行以下命令：

```bash
mkdir build
cd build
cmake ..
make
```

### 6.3 多架构支持

本库设计支持多种架构，主要针对 x86_64 架构进行了优化。如需在其他架构上使用，可能需要修改部分架构相关的代码。

## 7. 贡献指南

如果您希望为 libc 库贡献代码，请遵循以下步骤：

1. 确保您的代码符合项目的编码规范
2. 为新功能编写适当的单元测试
3. 确保所有现有测试通过
4. 更新相关文档
5. 提交 pull request
