/* Public domain.  */
#include <string.h>

size_t
strcspn (const char *s, const char *reject)
{
  size_t count = 0;
  
  while (*s != '\0' && strchr(reject, *s) == NULL) {
    count++;
    s++;
  }
  
  return count;
}