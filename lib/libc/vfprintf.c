/* Public domain.  */
#include <stdio.h>
#include <stdarg.h>

int
vfprintf(FILE *stream, const char *format, va_list ap)
{
    /* 系统API暂时留作空实现 */
    /* 实际实现需要解析format字符串并处理各种格式说明符 */
    return 0;
}