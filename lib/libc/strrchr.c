/* Public domain.  */
#include <string.h>

char *
strrchr (const char *s, int c)
{
  const unsigned char *p = (const unsigned char *)s;
  const unsigned char *last = NULL;
  unsigned char ch = (unsigned char)c;
  
  while (*p != '\0') {
    if (*p == ch) {
      last = p;
    }
    p++;
  }
  
  // 检查null字符本身
  if (ch == '\0') {
    return (char *)p;
  }
  
  return (char *)last;
}