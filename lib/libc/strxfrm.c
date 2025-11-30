/* Public domain.  */
#include <string.h>

/* 这是一个简化实现，不考虑区域设置 */
size_t
strxfrm (char *restrict dest, const char *restrict src, size_t n)
{
  size_t len = strlen(src);
  
  /* 如果dest不为NULL且n大于0，则复制字符串 */
  if (dest != NULL && n > 0) {
    size_t i = 0;
    while (i < n - 1 && src[i] != '\0') {
      dest[i] = src[i];
      i++;
    }
    
    /* 确保目标字符串以null字符结尾 */
    dest[i] = '\0';
  }
  
  /* 返回完整的字符串长度，不考虑n的限制 */
  return len;
}