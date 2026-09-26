#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define DIRECTORY_BATCH_CAPACITY 8

struct DirectoryStream {
    char path[LIBC_PATH_CAPACITY];
    uint64_t index;
    uint32_t batchIndex;
    uint32_t batchCount;
    OsDirectoryEntry entries[DIRECTORY_BATCH_CAPACITY];
    struct dirent entry;
};

DIR* opendir(const char* path) {
    char absolute[LIBC_PATH_CAPACITY];
    struct stat status;
    struct DirectoryStream* directory;
    libcCopyPath(absolute, path);
    if (stat(absolute, &status) != 0 || !S_ISDIR(status.st_mode)) {
        errno = ENOTDIR;
        return NULL;
    }
    directory = malloc(sizeof(*directory));
    if (directory == NULL) return NULL;
    memset(directory, 0, sizeof(*directory));
    strncpy(directory->path, absolute, sizeof(directory->path) - 1);
    return directory;
}

struct dirent* readdir(DIR* value) {
    OsDirectoryEntry* entry;
    int64_t count;
    if (value == NULL) {
        errno = EBADF;
        return NULL;
    }
    if (value->batchIndex == value->batchCount) {
        count = os_read_directory_batch(libcFsService(), value->path,
                                        value->index, value->entries,
                                        DIRECTORY_BATCH_CAPACITY);
        if (count <= 0) return NULL;
        value->batchIndex = 0;
        value->batchCount = (uint32_t)count;
        value->index += value->batchCount;
    }
    entry = &value->entries[value->batchIndex++];
    value->entry.d_ino = value->index - value->batchCount + value->batchIndex;
    value->entry.d_type = entry->type == OS_FILE_TYPE_DIRECTORY ? DT_DIR : DT_REG;
    strncpy(value->entry.d_name, entry->name,
            sizeof(value->entry.d_name) - 1);
    value->entry.d_name[sizeof(value->entry.d_name) - 1] = '\0';
    return &value->entry;
}

int closedir(DIR* directory) {
    if (directory == NULL) {
        errno = EBADF;
        return -1;
    }
    free(directory);
    return 0;
}

void rewinddir(DIR* directory) {
    if (directory != NULL) {
        directory->index = 0;
        directory->batchIndex = 0;
        directory->batchCount = 0;
    }
}

void seekdir(DIR* directory, long location) {
    if (directory != NULL && location >= 0) {
        directory->index = (uint64_t)location;
        directory->batchIndex = 0;
        directory->batchCount = 0;
    }
}

long telldir(DIR* directory) {
    if (directory == NULL) {
        errno = EBADF;
        return -1;
    }
    return (long)(directory->index - directory->batchCount +
                  directory->batchIndex);
}

int dirfd(DIR* directory) {
    (void)directory;
    errno = ENOSYS;
    return -1;
}
