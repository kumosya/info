#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* 每层仅保存路径和遍历位置，不把 C 调用栈深度绑定到目录深度。 */
typedef struct WalkNode
{
    struct WalkNode* parent;
    struct stat status;
    long position;
    int level;
    int entered;
    char path[PATH_MAX];
} WalkNode;

/* 所有访问使用绝对路径；回调仍收到用户给出的路径形式。 */
static int absolute_name(const char* cwd, const char* name, char* result)
{
    size_t prefix = name[0] == '/' ? 0 : strlen(cwd);
    size_t length = strlen(name);
    if (prefix + (prefix ? 1 : 0) + length >= PATH_MAX)
    {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (prefix)
    {
        memcpy(result, cwd, prefix);
        result[prefix++] = '/';
    }
    memcpy(result + prefix, name, length + 1);
    return 0;
}

/* FTW_CHDIR 仅改变回调时的工作目录，并在任何返回路径恢复原目录。 */
static int visit(WalkNode* node, int type, int flags, const char* cwd,
                   int (*callback)(const char*, const struct stat*, int, struct FTW*))
{
    struct FTW info = {0, node->level};
    for (int index = 0; node->path[index]; ++index)
    {
        if (node->path[index] == '/') info.base = index + 1;
    }
    if (!(flags & FTW_CHDIR)) return callback(node->path, &node->status, type, &info);

    char directory[PATH_MAX];
    if (absolute_name(cwd, node->path, directory)) return -1;
    char* slash = strrchr(directory, '/');
    if (slash == directory) slash[1] = 0;
    else if (slash) *slash = 0;
    if (chdir(directory)) return -1;
    int result = callback(node->path, &node->status, type, &info);
    int saved = errno;
    if (chdir(cwd)) return -1;
    errno = saved;
    return result;
}

/*
 * 每次取目录项后就关闭目录流，最多持有一个 FD，满足任意正 nopenfd。
 * 下一次重开后按目录位置续读。遍历中的并发目录修改仍遵循 readdir 的约定。
 */
int nftw(const char* path,
           int (*callback)(const char*, const struct stat*, int, struct FTW*),
           int nopenfd, int flags)
{
    if (!path || !callback || nopenfd <= 0 ||
        (flags & ~(FTW_PHYS | FTW_MOUNT | FTW_CHDIR | FTW_DEPTH)))
    {
        errno = EINVAL;
        return -1;
    }
    if (strnlen(path, PATH_MAX) == PATH_MAX) { errno = ENAMETOOLONG; return -1; }
    if (!*path) { errno = ENOENT; return -1; }
    char cwd[PATH_MAX], absolute[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return -1;
    WalkNode* top = calloc(1, sizeof(*top));
    if (!top) return -1;
    strcpy(top->path, path);
    size_t length = strlen(top->path);
    while (length > 1 && top->path[length - 1] == '/') top->path[--length] = 0;
    dev_t device = 0;
    int result = 0;
    while (top && !result)
    {
        if (absolute_name(cwd, top->path, absolute)) { result = -1; break; }
        if (!top->entered)
        {
            int type = FTW_F;
            int error = (flags & FTW_PHYS) ? lstat(absolute, &top->status) : stat(absolute, &top->status);
            if (error)
            {
                int saved = errno;
                type = FTW_NS;
                if (!(flags & FTW_PHYS) && saved == ENOENT &&
                    !lstat(absolute, &top->status) && S_ISLNK(top->status.st_mode)) type = FTW_SLN;
                errno = saved;
            }
            else if (S_ISLNK(top->status.st_mode)) type = FTW_SL;
            else if (S_ISDIR(top->status.st_mode)) type = FTW_D;

            if (!top->level) device = top->status.st_dev;
            int skip = !error && (flags & FTW_MOUNT) && top->status.st_dev != device;
            for (WalkNode* ancestor = top->parent; ancestor && type == FTW_D; ancestor = ancestor->parent)
            {
                if (ancestor->status.st_dev == top->status.st_dev &&
                    ancestor->status.st_ino == top->status.st_ino) skip = 1;
            }
            if (skip) goto pop;
            if (type != FTW_D)
            {
                result = visit(top, type, flags, cwd, callback);
                goto pop;
            }
            DIR* directory = opendir(absolute);
            if (!directory)
            {
                result = visit(top, FTW_DNR, flags, cwd, callback);
                goto pop;
            }
            if (closedir(directory)) { result = -1; break; }
            top->entered = 1;
            if (!(flags & FTW_DEPTH)) result = visit(top, FTW_D, flags, cwd, callback);
            if (result) break;
        }

        DIR* directory = opendir(absolute);
        if (!directory) { result = -1; break; }
        seekdir(directory, top->position);
        struct dirent* entry;
        errno = 0;
        do { entry = readdir(directory); }
        while (entry && (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")));
        int saved = errno;
        if (!entry)
        {
            if (closedir(directory)) { result = -1; break; }
            if (saved) { errno = saved; result = -1; break; }
            if (flags & FTW_DEPTH) result = visit(top, FTW_DP, flags, cwd, callback);
            goto pop;
        }
        WalkNode* child = calloc(1, sizeof(*child));
        if (!child) { closedir(directory); result = -1; break; }
        top->position = telldir(directory);
        size_t parent_length = strlen(top->path), name_length = strlen(entry->d_name);
        if (parent_length + name_length + 2 > PATH_MAX)
        {
            free(child);
            closedir(directory);
            errno = ENAMETOOLONG;
            result = -1;
            break;
        }
        memcpy(child->path, top->path, parent_length);
        if (parent_length && child->path[parent_length - 1] != '/') child->path[parent_length++] = '/';
        memcpy(child->path + parent_length, entry->d_name, name_length + 1);
        if (closedir(directory)) { free(child); result = -1; break; }
        child->parent = top;
        child->level = top->level + 1;
        top = child;
        continue;

pop:
        {
            WalkNode* parent = top->parent;
            free(top);
            top = parent;
        }
    }
    int saved = errno;
    while (top)
    {
        WalkNode* parent = top->parent;
        free(top);
        top = parent;
    }
    errno = saved;
    return result;
}
