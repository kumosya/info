#include <errno.h>
#include <string.h>

char* strerror(int errorNumber) {
    switch (errorNumber) {
        case 0: return "Success";
        case EACCES: return "Permission denied";
        case EBADF: return "Bad file descriptor";
        case EEXIST: return "File exists";
        case EINVAL: return "Invalid argument";
        case EIO: return "Input/output error";
        case EISDIR: return "Is a directory";
        case EMFILE: return "Too many open files";
        case ENOENT: return "No such file or directory";
        case ENOMEM: return "Out of memory";
        case ENOSYS: return "Function not implemented";
        case ENOTDIR: return "Not a directory";
        case EPERM: return "Operation not permitted";
        case EPIPE: return "Broken pipe";
        case ERANGE: return "Result out of range";
        case ESRCH: return "No such process";
        default: return "Unknown error";
    }
}
