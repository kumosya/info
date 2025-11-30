/* Public domain.  */
#include <string.h>

size_t
strspn (const char *s, const char *accept)
{
  size_t count = 0;
  
  while (*s != '\0' && strchr(accept, *s) != NULL) {
    count++;
    s++;
  }
  
  return count;
}