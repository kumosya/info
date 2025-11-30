/* Public domain.  */
#include <string.h>

char *
strcpy (char *restrict dest, const char *restrict src)
{
  char *d = dest;
  const char *s = src;
  
  while ((*d++ = *s++));
  
  return dest;
}