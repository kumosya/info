#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "os_adapter.h"

#define ENVIRONMENT_CAPACITY 128

char** environ;
static char* ownedEnvironment[ENVIRONMENT_CAPACITY];
static int environmentInitialized;

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

char* getenv(const char* name) {
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

int setenv(const char* name, const char* value, int overwrite) {
    size_t nameLength;
    size_t valueLength;
    size_t index;
    char* entry;
    if (!validName(name) || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!overwrite && getenv(name) != NULL) return 0;
    nameLength = strlen(name);
    valueLength = strlen(value);
    entry = malloc(nameLength + valueLength + 2);
    if (entry == NULL) return -1;
    memcpy(entry, name, nameLength);
    entry[nameLength] = '=';
    memcpy(entry + nameLength + 1, value, valueLength + 1);
    if (environ != ownedEnvironment) {
        index = 0;
        while (environ != NULL && environ[index] != NULL &&
               index + 1 < ENVIRONMENT_CAPACITY) {
            ownedEnvironment[index] = environ[index];
            ++index;
        }
        ownedEnvironment[index] = NULL;
        __libc_set_environ(ownedEnvironment);
    }
    for (index = 0; environ[index] != NULL; ++index) {
        if (strncmp(environ[index], name, nameLength) == 0 &&
            environ[index][nameLength] == '=') {
            environ[index] = entry;
            return 0;
        }
    }
    if (index + 1 >= ENVIRONMENT_CAPACITY) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }
    environ[index] = entry;
    environ[index + 1] = NULL;
    return 0;
}

int unsetenv(const char* name) {
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
            break;
        }
    }
    return 0;
}

int clearenv(void) {
    ownedEnvironment[0] = NULL;
    __libc_set_environ(ownedEnvironment);
    return 0;
}
