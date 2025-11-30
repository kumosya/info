/* Public domain.  */
#include <string.h>

char *
strcat (char *restrict dest, const char *restrict src)
{
  char *d = dest;
  
  // 先找到目标字符串的结尾
  while (*d != '\0') {
    d++;
  }
  
  // 然后复制源字符串
  while ((*d++ = *src++));
  
  return dest;
}