#pragma once

#include <stdint.h>

#define DT_UNKNOWN 0
#define DT_REG 8
#define DT_DIR 4

struct dirent {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[48];
};

typedef struct DirectoryStream DIR;

#ifdef __cplusplus
extern "C" {
#endif
DIR* opendir(const char* path);
struct dirent* readdir(DIR* directory);
int closedir(DIR* directory);
void rewinddir(DIR* directory);
void seekdir(DIR* directory, long location);
long telldir(DIR* directory);
int dirfd(DIR* directory);
#ifdef __cplusplus
}
#endif
