/* Public domain.  */
#include <stdio.h>
#include <stdarg.h>

int
printf(const char *format, ...)
{
    va_list ap;
    int ret;
    
    va_start(ap, format);
    ret = vfprintf(stdout, format, ap);
    va_end(ap);
    
    return ret;
}