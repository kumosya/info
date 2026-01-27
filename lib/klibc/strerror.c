/* Public domain.  */
#include <string.h>
#include <stdio.h>

/* 简单的错误消息表 */
static const char *error_messages[] = {
  "Success",
  "Operation not permitted",
  "No such file or directory",
  "No such process",
  "Interrupted system call",
  "Input/output error",
  "No such device or address",
  "Argument list too long",
  "Exec format error",
  "Bad file descriptor",
  "No child processes",
  "Resource temporarily unavailable"
};

char *
strerror (int errnum)
{
  static char unknown_error[32];
  
  /* 检查错误码是否在已知范围内 */
  if (errnum >= 0 && (size_t)errnum < sizeof(error_messages) / sizeof(error_messages[0])) {
    return (char *)error_messages[errnum];
  }
  
  /* 对于未知错误码，返回格式化的错误消息 */
  sprintf(unknown_error, "Unknown error %d", errnum);
  return unknown_error;
}