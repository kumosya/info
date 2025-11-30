/* Public domain.  */
#include <string.h>

// 静态变量，用于保存上一次分割的位置
static char *saved_token = NULL;

char *
strtok (char *restrict s, const char *restrict delim)
{
  char *token;
  
  // 如果s为NULL，则从上一次分割的位置继续
  if (s == NULL) {
    s = saved_token;
    // 如果没有更多的字符可分割，返回NULL
    if (s == NULL) {
      return NULL;
    }
  }
  
  // 跳过所有在分隔符集合中的字符
  while (*s != '\0' && strchr(delim, *s) != NULL) {
    s++;
  }
  
  // 如果到达字符串末尾，返回NULL
  if (*s == '\0') {
    saved_token = NULL;
    return NULL;
  }
  
  // 记录token的起始位置
  token = s;
  
  // 寻找分隔符或字符串结束符
  while (*s != '\0' && strchr(delim, *s) == NULL) {
    s++;
  }
  
  // 如果不是到达字符串末尾，则将分隔符替换为null字符，并保存下一次分割的位置
  if (*s != '\0') {
    *s = '\0';
    saved_token = s + 1;
  } else {
    // 否则，下一次分割将返回NULL
    saved_token = NULL;
  }
  
  return token;
}