/* Public domain.  */
#include <string.h>

char *
strchr (const char *s, int c)
{
  const unsigned char *p = (const unsigned char *)s;
  unsigned char ch = (unsigned char)c;
  
  while (*p != ch) {
    if (*p == '\0') {
      return NULL;
    }
    p++;
  }
  
  return (char *)p;
}