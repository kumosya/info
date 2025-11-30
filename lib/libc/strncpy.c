/* Public domain.  */
#include <string.h>

char *
strncpy (char *restrict dest, const char *restrict src, size_t n)
{
  char *d = dest;
  const char *s = src;
  
  while (n > 0 && *s != '\0') {
    *d++ = *s++;
    n--;
  }
  
  while (n > 0) {
    *d++ = '\0';
    n--;
  }
  
  return dest;
}