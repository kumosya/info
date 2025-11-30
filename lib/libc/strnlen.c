/* Public domain.  */
#include <string.h>

size_t
strnlen (const char *s, size_t maxlen)
{
  size_t len = 0;
  
  while (len < maxlen && *s != '\0') {
    len++;
    s++;
  }
  
  return len;
}