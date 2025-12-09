/* Public domain.  */
#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* 标准文件描述符 */
#define stdin  ((FILE *) 0)
#define stdout ((FILE *) 1)
#define stderr ((FILE *) 2)

/* 缓冲区大小 */
#define BUFSIZ 8192

/* 文件位置类型 */
typedef long long fpos_t;

/* 文件结构体 */
typedef struct {
    unsigned char *_buf;
    int _fd;
    size_t _size;
    size_t _len;
    int _flags;
    int _error;
    int _eof;
} FILE;

/* 文件操作模式 */
#define _IOFBF 0 /* 全缓冲 */
#define _IOLBF 1 /* 行缓冲 */
#define _IONBF 2 /* 无缓冲 */

/* 文件打开标志 */
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_APPEND  0x008
#define O_CREAT   0x020
#define O_TRUNC   0x040
#define O_EXCL    0x080
#define O_TEXT    0x400
#define O_BINARY  0x800

/* 格式化输出标志 */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* 错误常量 */
#define EOF (-1)

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

/* 内存操作函数 */
int remove(const char *filename);
int rename(const char *old_name, const char *new_name);
FILE *tmpfile(void);
char *tmpnam(char *s);

/* 文件操作函数 */
FILE *fopen(const char *filename, const char *mode);
FILE *freopen(const char *filename, const char *mode, FILE *stream);
int fclose(FILE *stream);

/* 缓冲操作函数 */
int fflush(FILE *stream);
void setbuf(FILE *stream, char *buf);
int setvbuf(FILE *stream, char *buf, int mode, size_t size);

/* 格式化输入函数 */
int fprintf(FILE *stream, const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);
int printf(const char *format, ...);
int scanf(const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int sscanf(const char *str, const char *format, ...);

/* 可变参数格式化函数 */
int vfprintf(FILE *stream, const char *format, va_list ap);
int vfscanf(FILE *stream, const char *format, va_list ap);
int vprintf(const char *format, va_list ap);
int vscanf(const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsscanf(const char *str, const char *format, va_list ap);

/* 字符输入输出函数 */
int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int getc(FILE *stream);
int getchar(void);
char *gets(char *s); /* 已弃用，但保留兼容性 */
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
int ungetc(int c, FILE *stream);

/* 块输入输出函数 */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

/* 文件定位函数 */
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);
long ftell(FILE *stream);
int fseek(FILE *stream, long offset, int whence);
void rewind(FILE *stream);

/* 文件状态函数 */
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/* 二进制文件操作 */
int fileno(FILE *stream);
FILE *fdopen(int fd, const char *mode);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* _STDIO_H */
