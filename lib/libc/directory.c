#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DIRECTORY_BATCH_CAPACITY 8

struct DirectoryStream {
    int descriptor;
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
    directory->descriptor=open(absolute,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(directory->descriptor<0) { free(directory); return NULL; }
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
        count = __libc_os_read_directory_batch(libcFsService(), value->path,
                                        value->index, value->entries,
                                        DIRECTORY_BATCH_CAPACITY);
        if (count <= 0) {
            if (count < 0) errno = EIO;
            return NULL;
        }
        value->batchIndex = 0;
        value->batchCount = (uint32_t)count;
        value->index += value->batchCount;
    }
    entry = &value->entries[value->batchIndex++];
    value->entry.d_ino = value->index - value->batchCount + value->batchIndex;
    value->entry.d_off = value->entry.d_ino;
    value->entry.d_reclen = sizeof(value->entry);
    OpenDescription* description=libcDescriptor(value->descriptor);
    if (description) description->offset=value->entry.d_off;
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
    int status=close(directory->descriptor);
    free(directory);
    return status;
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
    if (!directory) { errno=EBADF; return -1; }
    return directory->descriptor;
}

DIR* fdopendir(int fd) {
    OpenDescription* description=libcDescriptor(fd);
    struct stat status;
    if(!description || fstat(fd,&status)) return NULL;
    if(!S_ISDIR(status.st_mode)) { errno=ENOTDIR; return NULL; }
    DIR* directory=calloc(1,sizeof(*directory)); if(!directory) return NULL;
    directory->descriptor=fd; directory->index=description->offset;
    strncpy(directory->path,description->path,sizeof(directory->path)-1);
    return directory;
}
int readdir_r(DIR* restrict directory,struct dirent* restrict entry,struct dirent** restrict result) {
    int saved=errno; errno=0; struct dirent* found=readdir(directory); int error=errno; errno=saved;
    *result=NULL; if(found) { *entry=*found; *result=entry; } return error;
}
int alphasort(const struct dirent** a,const struct dirent** b) { return strcoll((*a)->d_name,(*b)->d_name); }
static int directory_compare(const void* a,const void* b,void* context) {
    int (*compare)(const struct dirent**,const struct dirent**)=context;
    return compare((const struct dirent**)a,(const struct dirent**)b);
}
int scandir(const char* path,struct dirent*** result,int (*select)(const struct dirent*),int (*compare)(const struct dirent**,const struct dirent**)) {
    DIR* directory=opendir(path); if(!directory) return -1;
    struct dirent** entries=NULL; size_t count=0; int error=0;
    while(1) {
        errno=0; struct dirent* entry=readdir(directory);
        if(!entry) { error=errno; break; }
        if(select && !select(entry)) continue;
        struct dirent* copy=malloc(sizeof(*copy));
        if(!copy) { error=ENOMEM; break; }
        struct dirent** grown=realloc(entries,(count+1)*sizeof(*entries));
        if(!grown) { free(copy); error=ENOMEM; break; }
        entries=grown; *copy=*entry; entries[count++]=copy;
    }
    closedir(directory);
    if(error) { while(count) free(entries[--count]); free(entries); errno=error; return -1; }
    if(compare) qsort_r(entries,count,sizeof(*entries),directory_compare,(void*)compare);
    *result=entries; return count;
}
ssize_t posix_getdents(int fd,void* buffer,size_t size,int flags) {
    OpenDescription* description=libcDescriptor(fd);
    if(!description) { errno=EBADF; return -1; }
    if(flags || size<sizeof(struct dirent)) { errno=EINVAL; return -1; }
    if(!buffer) { errno=EFAULT; return -1; }
    struct stat status; if(fstat(fd,&status)) return -1;
    if(!S_ISDIR(status.st_mode)) { errno=ENOTDIR; return -1; }
    size_t used=0;
    while(size-used>=sizeof(struct dirent)) {
        OsDirectoryEntry entry;
        int64_t n=__libc_os_read_directory_batch(libcFsService(),description->path,description->offset,&entry,1);
        if(n<0) { if(used) break; errno=EIO; return -1; }
        if(!n) break;
        struct dirent value={0}; value.d_ino=++description->offset; value.d_off=description->offset;
        value.d_type=entry.type==OS_FILE_TYPE_DIRECTORY?DT_DIR:DT_REG; value.d_reclen=sizeof(value);
        strncpy(value.d_name,entry.name,sizeof(value.d_name)-1);
        memcpy((char*)buffer+used,&value,sizeof(value)); used+=sizeof(value);
    }
    return used;
}
