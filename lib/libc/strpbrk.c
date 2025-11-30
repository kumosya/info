/* Public domain.  */
#include <string.h>

char *
strpbrk (const char *s, const char *charset)
{
  while (*s != '\0') {
    if (strchr(charset, *s) != NULL) {
      return (char *)s;
    }
    s++;
  }
  
  return NULL;
}