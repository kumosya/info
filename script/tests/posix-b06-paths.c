#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char* osinfo_realpath(const char*, char*);
int osinfo_nftw(const char*, int (*)(const char*, const struct stat*, int, struct FTW*), int, int);
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "B06 FAIL line=%d errno=%d\n", __LINE__, errno); return 1; } } while (0)
static unsigned directories, files, links, postorder, callbacks;
static int stop;
static int record(const char* path, const struct stat* status, int type, struct FTW* info)
{
    (void)status;
    if (info->level < 0 || info->base < 0 || (size_t)info->base > strlen(path)) return 91;
    ++callbacks;
    directories += type == FTW_D;
    files += type == FTW_F;
    links += type == FTW_SL;
    postorder += type == FTW_DP;
    return stop ? 37 : 0;
}

int main(int argc, char** argv)
{
    CHECK(argc == 2 && chdir(argv[1]) == 0);
    CHECK(mkdir("tree", 0700) == 0 && mkdir("tree/sub", 0700) == 0);
    int fd = open("tree/sub/file", O_CREAT | O_WRONLY, 0600);
    CHECK(fd >= 0 && close(fd) == 0);
    CHECK(symlink("sub", "tree/link") == 0);
    CHECK(symlink("../..", "tree/sub/back") == 0);
    CHECK(symlink("loop", "loop") == 0);
    char expected[PATH_MAX], actual[PATH_MAX];
    const char* cases[] = {"tree/sub/../sub/file", "tree/link/file", "tree/./link/../sub", "tree/..", "/", NULL};
    for (unsigned index = 0; cases[index]; ++index)
    {
        CHECK(realpath(cases[index], expected) != NULL);
        CHECK(osinfo_realpath(cases[index], actual) != NULL && !strcmp(expected, actual));
        char* allocated = osinfo_realpath(cases[index], NULL);
        CHECK(allocated && !strcmp(expected, allocated));
        free(allocated);
    }
    CHECK(!osinfo_realpath("tree/missing/../sub", actual) && errno == ENOENT);
    CHECK(!osinfo_realpath("tree/sub/file/..", actual) && errno == ENOTDIR);
    CHECK(!osinfo_realpath("loop", actual) && errno == ELOOP);
    CHECK(!osinfo_realpath("", actual) && errno == ENOENT);
    if (geteuid() != 0)
    {
        CHECK(chmod("tree/sub", 0600) == 0);
        CHECK(!realpath("tree/sub/file", expected) && errno == EACCES);
        CHECK(!osinfo_realpath("tree/sub/file", actual) && errno == EACCES);
        CHECK(chmod("tree/sub", 0700) == 0);
    }
    CHECK(osinfo_nftw("tree", record, 1, FTW_PHYS) == 0);
    CHECK(directories == 2 && files == 1 && links == 2 && callbacks == 5);
    directories = files = links = callbacks = 0;
    CHECK(osinfo_nftw("tree", record, 1, FTW_PHYS | FTW_DEPTH) == 0);
    CHECK(postorder == 2 && !directories && files == 1 && links == 2);
    CHECK(getcwd(expected, sizeof(expected)) != NULL);
    CHECK(osinfo_nftw("tree", record, 1, FTW_PHYS | FTW_CHDIR) == 0);
    CHECK(getcwd(actual, sizeof(actual)) && !strcmp(actual, expected));
    stop = 1;
    CHECK(osinfo_nftw("tree", record, 1, FTW_PHYS | FTW_CHDIR) == 37);
    CHECK(getcwd(actual, sizeof(actual)) && !strcmp(actual, expected));
    stop = 0;
    CHECK(osinfo_nftw("tree", record, 1, 0) == 0);
    CHECK(osinfo_nftw("tree", record, 0, 0) == -1 && errno == EINVAL);
    return 0;
}
