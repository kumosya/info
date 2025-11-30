/* Public domain.  */
#include <string.h>

/* 这是一个简化实现，不考虑区域设置 */
int
strcoll (const char *s1, const char *s2)
{
  /* 在没有区域设置支持的情况下，使用strcmp作为替代 */
  return strcmp(s1, s2);
}