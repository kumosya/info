/* Public domain.  */
#include <string.h>

int
strncmp (const char *s1, const char *s2, size_t n)
{
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  
  while (n > 0) {
    if (*p1 != *p2) {
      return *p1 - *p2;
    }
    if (*p1 == '\0') {
      return 0;
    }
    p1++;
    p2++;
    n--;
  }
  
  return 0;
}