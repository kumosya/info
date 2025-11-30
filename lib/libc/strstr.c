/* Public domain.  */
#include <string.h>

char *
strstr (const char *haystack, const char *needle)
{
  // 处理特殊情况
  if (*needle == '\0') {
    return (char *)haystack;
  }
  
  const char *h, *n;
  
  while (*haystack != '\0') {
    h = haystack;
    n = needle;
    
    while (*n != '\0' && *h == *n) {
      h++;
      n++;
    }
    
    if (*n == '\0') {
      return (char *)haystack;
    }
    
    haystack++;
  }
  
  return NULL;
}