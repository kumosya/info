/* Public domain.  */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

void *
memchr (const void *s, int c, size_t n)
{
  const unsigned char *p = s;
  unsigned char ch = (unsigned char)c;
  
  while (n-- > 0) {
    if (*p == ch) {
      return (void *)p;
    }
    p++;
  }
  
  return NULL;
}

int
memcmp (const void *str1, const void *str2, size_t count)
{
  const unsigned char *s1 = str1;
  const unsigned char *s2 = str2;

  while (count-- > 0)
    {
      if (*s1++ != *s2++)
	  return s1[-1] < s2[-1] ? -1 : 1;
    }
  return 0;
}

void *
memcpy (void *dest, const void *src, size_t len)
{
  char *d = dest;
  const char *s = src;
  while (len--)
    *d++ = *s++;
  return dest;
}


void *
memmove (void *dest, const void *src, size_t len)
{
  char *d = dest;
  const char *s = src;
  if (d < s)
    while (len--)
      *d++ = *s++;
  else
    {
      char *lasts = s + (len-1);
      char *lastd = d + (len-1);
      while (len--)
        *lastd-- = *lasts--;
    }
  return dest;
}

void *
memset (void *dest, int val, size_t len)
{
  unsigned char *ptr = dest;
  while (len-- > 0)
    *ptr++ = val;
  return dest;
}


char *
strcat (char *restrict dest, const char *restrict src)
{
  char *d = dest;
  
  // 先找到目标字符串的结尾
  while (*d != '\0') {
    d++;
  }
  
  // 然后复制源字符串
  while ((*d++ = *src++));
  
  return dest;
}

char *
strchr (const char *s, int c)
{
  const unsigned char *p = (const unsigned char *)s;
  unsigned char ch = (unsigned char)c;
  
  while (*p != ch) {
    if (*p == '\0') {
      return NULL;
    }
    p++;
  }
  
  return (char *)p;
}

int
strcmp (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *)s1;
  const unsigned char *p2 = (const unsigned char *)s2;
  
  while (*p1 == *p2) {
    if (*p1 == '\0') {
      return 0;
    }
    p1++;
    p2++;
  }
  
  return *p1 - *p2;
}


char *
strcpy (char *restrict dest, const char *restrict src)
{
  char *d = dest;
  const char *s = src;
  
  while ((*d++ = *s++));
  
  return dest;
}


size_t
strlen (const char *s)
{
  const char *p = s;
  
  while (*p != '\0') {
    p++;
  }
  
  return (size_t)(p - s);
}


char *
strncat (char *restrict dest, const char *restrict src, size_t n)
{
  char *d = dest;
  
  // 先找到目标字符串的结尾
  while (*d != '\0') {
    d++;
  }
  
  // 然后复制源字符串，但最多复制n个字符
  while (n > 0 && *src != '\0') {
    *d++ = *src++;
    n--;
  }
  
  // 确保目标字符串以null字符结尾
  *d = '\0';
  
  return dest;
}

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


char *
strncpy (char *restrict dest, const char *restrict src, size_t n)
{
  char *d = dest;
  const char *s = src;
  
  while (n > 0 && *s != '\0') {
    *d++ = *s++;
    n--;
  }
  
  while (n > 0) {
    *d++ = '\0';
    n--;
  }
  
  return dest;
}
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

char *
strrchr (const char *s, int c)
{
  const unsigned char *p = (const unsigned char *)s;
  const unsigned char *last = NULL;
  unsigned char ch = (unsigned char)c;
  
  while (*p != '\0') {
    if (*p == ch) {
      last = p;
    }
    p++;
  }
  
  // 检查null字符本身
  if (ch == '\0') {
    return (char *)p;
  }
  
  return (char *)last;
}


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
/*
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
*/

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
