#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "../ld-x86-64/loader-api.h"
#include "internal.h"

static const OsDynamicLoader* loader;
typedef struct { char text[192]; int pending; } Error;
static pthread_key_t error_key;
static pthread_once_t error_once = PTHREAD_ONCE_INIT;
static int key_ready;
static void make_key(void) { key_ready = pthread_key_create(&error_key, free) == 0; }
static Error* begin(void) {
    pthread_once(&error_once, make_key);
    if (!key_ready) { errno = ENOMEM; return NULL; }
    Error* error = pthread_getspecific(error_key);
    if (!error) {
        error = calloc(1, sizeof(*error));
        if (!error || pthread_setspecific(error_key, error)) { free(error); errno = ENOMEM; return NULL; }
    }
    error->pending = 0; error->text[0] = 0;
    if (!loader) { strcpy(error->text, "runtime dynamic loading is unavailable"); error->pending = 1; errno = ENOSYS; }
    return error;
}
void __osinfo_bind_loader(const OsDynamicLoader* api) {
    if (api && api->version == 1 && api->size == sizeof(*api)) loader = api;
}
void* dlopen(const char* path, int flags) {
    Error* error = begin(); if (!error || !loader) return NULL;
    char absolute[LIBC_PATH_CAPACITY];
    if (path && strchr(path, '/') && path[0] != '/') {
        if (libcPathAt(-100, path, absolute)) { strcpy(error->text, "invalid library path"); error->pending = 1; return NULL; }
        path = absolute;
    }
    void* result = loader->open(path, flags, error->text, sizeof(error->text));
    error->pending = error->text[0] != 0;
    return result;
}
void* dlsym(void* handle, const char* name) {
    Error* error = begin(); if (!error || !loader) return NULL;
    void* result = loader->symbol(handle, name, __builtin_return_address(0), error->text, sizeof(error->text));
    error->pending = error->text[0] != 0;
    return result;
}
int dlclose(void* handle) {
    Error* error = begin(); if (!error || !loader) return -1;
    const int result = loader->close(handle, error->text, sizeof(error->text));
    error->pending = error->text[0] != 0;
    return result;
}
char* dlerror(void) {
    pthread_once(&error_once, make_key);
    Error* error = key_ready ? pthread_getspecific(error_key) : NULL;
    if (!error || !error->pending) return NULL;
    error->pending = 0; return error->text;
}
