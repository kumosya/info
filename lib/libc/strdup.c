/* Public domain.  */
#include <string.h>
#include <stdlib.h>

char *
strdup (const char *s)
{
  size_t len = strlen(s) + 1;
  char *new_str = (char *)malloc(len);
  
  if (new_str != NULL) {
    memcpy(new_str, s, len);
  }
  
  return new_str;
}