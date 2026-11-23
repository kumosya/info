#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * 按实际路径逐段查询，不能先消掉 a/..：a 可能不存在、不是目录或是链接。
 * 链接目标插回尚未处理的路径；最多展开 40 次以阻止链接环。
 */
char* realpath(const char* restrict path, char* restrict output)
{
    char pending[PATH_MAX], resolved[PATH_MAX];
    if (!path) { errno = EINVAL; return NULL; }
    size_t length = strnlen(path, PATH_MAX);
    if (!length) { errno = ENOENT; return NULL; }
    if (length == PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
    memcpy(pending, path, length + 1);

    if (*path == '/') strcpy(resolved, "/");
    else if (!getcwd(resolved, sizeof(resolved))) return NULL;
    size_t used = strlen(resolved);
    unsigned links = 0;
    char* cursor = pending;
    while (*cursor)
    {
        while (*cursor == '/') ++cursor;
        if (!*cursor) break;
        char* first = cursor;
        while (*cursor && *cursor != '/') ++cursor;
        size_t count = (size_t)(cursor - first);
        if (count == 1 && *first == '.') continue;
        if (count == 2 && first[0] == '.' && first[1] == '.')
        {
            while (used > 1 && resolved[used - 1] != '/') --used;
            if (used > 1) --used;
            resolved[used] = 0;
            continue;
        }

        size_t parent = used;
        if (used > 1) resolved[used++] = '/';
        if (count >= sizeof(resolved) - used) { errno = ENAMETOOLONG; return NULL; }
        memcpy(resolved + used, first, count);
        used += count;
        resolved[used] = 0;
        struct stat status;
        if (lstat(resolved, &status)) return NULL;
        if (S_ISLNK(status.st_mode))
        {
            if (++links > 40) { errno = ELOOP; return NULL; }
            char target[PATH_MAX], rest[PATH_MAX];
            ssize_t size = readlink(resolved, target, sizeof(target));
            if (size < 0) return NULL;
            if (!size || (size_t)size + strlen(cursor) >= sizeof(pending))
            {
                errno = size ? ENAMETOOLONG : ENOENT;
                return NULL;
            }
            strcpy(rest, cursor);
            memcpy(pending, target, (size_t)size);
            strcpy(pending + size, rest);
            used = target[0] == '/' ? 1 : parent;
            resolved[used] = 0;
            cursor = pending;
        }
        else if (*cursor && !S_ISDIR(status.st_mode))
        {
            errno = ENOTDIR;
            return NULL;
        }
    }

    if (!output) output = malloc(used + 1);
    if (output) memcpy(output, resolved, used + 1);
    return output;
}
