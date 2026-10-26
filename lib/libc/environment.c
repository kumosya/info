#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "lock.h"

#define ENVIRONMENT_CAPACITY 128

char** environ;
static char* ownedEnvironment[ENVIRONMENT_CAPACITY];
static int environmentInitialized;
static char* allocatedEntries[ENVIRONMENT_CAPACITY];
static volatile uint32_t environmentLock;
void __libc_environment_fork_prepare(void) { libcWaitLock(&environmentLock); }
void __libc_environment_fork_parent(void) { libcWaitUnlock(&environmentLock); }
void __libc_environment_fork_child(void) { environmentLock = 0; }

static void releaseUnused(void) {
    for (size_t i = 0; i < ENVIRONMENT_CAPACITY; ++i) {
        if (!allocatedEntries[i]) continue;
        size_t j = 0;
        while (environ && environ[j] && environ[j] != allocatedEntries[i]) ++j;
        if (!environ || !environ[j]) { free(allocatedEntries[i]); allocatedEntries[i] = NULL; }
    }
}

void __libc_set_environ(char** environment) {
    environmentInitialized = 1;
    environ = environment;
    __libc_os_bind_environment(&environ);
}

void __libc_initialize_environ(char** environment) {
    if (!environmentInitialized) __libc_set_environ(environment);
}

static int validName(const char* name) {
    return name != NULL && *name != '\0' && strchr(name, '=') == NULL;
}

static char* lookup(const char* name) {
    size_t length;
    size_t index;
    if (!validName(name) || environ == NULL) return NULL;
    length = strlen(name);
    for (index = 0; environ[index] != NULL; ++index) {
        if (strncmp(environ[index], name, length) == 0 &&
            environ[index][length] == '=') {
            return environ[index] + length + 1;
        }
    }
    return NULL;
}

static int install(const char* name, size_t nameLength, char* entry) {
    size_t index;
    if (environ != ownedEnvironment) {
        index = 0;
        while (environ != NULL && environ[index] != NULL &&
               index + 1 < ENVIRONMENT_CAPACITY) {
            ownedEnvironment[index] = environ[index];
            ++index;
        }
        if (environ && environ[index]) { errno = ENOMEM; return -1; }
        ownedEnvironment[index] = NULL;
        __libc_set_environ(ownedEnvironment);
    }
    for (index = 0; environ[index] != NULL; ++index) {
        if (strncmp(environ[index], name, nameLength) == 0 &&
            environ[index][nameLength] == '=') {
            environ[index] = entry;
            releaseUnused();
            return 0;
        }
    }
    if (index + 1 >= ENVIRONMENT_CAPACITY) {
        errno = ENOMEM;
        return -1;
    }
    environ[index] = entry;
    environ[index + 1] = NULL;
    return 0;
}

static int removeEntry(const char* name) {
    size_t length;
    size_t index;
    if (!validName(name)) {
        errno = EINVAL;
        return -1;
    }
    if (environ == NULL) return 0;
    length = strlen(name);
    for (index = 0; environ[index] != NULL; ++index) {
        if (strncmp(environ[index], name, length) == 0 &&
            environ[index][length] == '=') {
            do {
                environ[index] = environ[index + 1];
                ++index;
            } while (environ[index - 1] != NULL);
            index = (size_t)-1;
        }
    }
    releaseUnused(); return 0;
}

int clearenv(void) {
    libcWaitLock(&environmentLock);
    ownedEnvironment[0] = NULL;
    __libc_set_environ(ownedEnvironment);
    releaseUnused();
    libcWaitUnlock(&environmentLock);
    return 0;
}
char* getenv(const char* name) {
    libcWaitLock(&environmentLock); char* result = lookup(name); libcWaitUnlock(&environmentLock); return result;
}
int setenv(const char* name, const char* value, int overwrite) {
    if (!validName(name) || !value) { errno = EINVAL; return -1; }
    libcWaitLock(&environmentLock);
    if (!overwrite && lookup(name)) { libcWaitUnlock(&environmentLock); return 0; }
    size_t nameLength = strlen(name), valueLength = strlen(value);
    if (valueLength > SIZE_MAX - nameLength - 2) { libcWaitUnlock(&environmentLock); errno = ENOMEM; return -1; }
    char* entry = malloc(nameLength + valueLength + 2);
    if (!entry) { libcWaitUnlock(&environmentLock); return -1; }
    memcpy(entry, name, nameLength); entry[nameLength] = '='; memcpy(entry + nameLength + 1, value, valueLength + 1);
    size_t slot = 0; while (slot < ENVIRONMENT_CAPACITY && allocatedEntries[slot]) ++slot;
    if (slot == ENVIRONMENT_CAPACITY || install(name, nameLength, entry)) {
        free(entry); libcWaitUnlock(&environmentLock); errno = ENOMEM; return -1;
    }
    allocatedEntries[slot] = entry;
    libcWaitUnlock(&environmentLock); return 0;
}
int unsetenv(const char* name) {
    if (!validName(name)) { errno = EINVAL; return -1; }
    libcWaitLock(&environmentLock); int result = removeEntry(name); libcWaitUnlock(&environmentLock); return result;
}
int putenv(char* entry) {
    if (!entry || !*entry || *entry == '=') { errno = EINVAL; return -1; }
    char* equal = strchr(entry, '=');
    if (!equal) return unsetenv(entry);
    libcWaitLock(&environmentLock); int result = install(entry, (size_t)(equal - entry), entry); libcWaitUnlock(&environmentLock); return result;
}
