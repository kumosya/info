/* Public domain.  */
#include <string.h>

char *
strncat (char *restrict dest, const char *restrict src, size_t n)
{
  char *d = dest;
  
  // 先找到目标字符串的结尾
  while (*d != '\0') {
    d++;
  }
  
  // 然后复制源字符串，但最多复制n个字符
  while (n > 0 && *src != '\0') {
    *d++ = *src++;
    n--;
  }
  
  // 确保目标字符串以null字符结尾
  *d = '\0';
  
  return dest;
}