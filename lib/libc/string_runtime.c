#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void* memchr(const void* memory, int value, size_t size) {
    const unsigned char* bytes = memory;
    for (size_t i = 0; i < size; ++i) if (bytes[i] == (unsigned char)value) return (void*)(bytes + i);
    return NULL;
}

void* memcpy(void* destination, const void* source, size_t size) {
#if defined(__x86_64__)
    void* result = destination;
    size_t words = size / 8, tail = size % 8;
    __asm__ volatile("rep movsq" : "+D"(destination), "+S"(source), "+c"(words) : : "memory");
    __asm__ volatile("rep movsb" : "+D"(destination), "+S"(source), "+c"(tail) : : "memory");
    return result;
#else
    unsigned char* target = destination;
    const unsigned char* input = source;
    size_t index;
    for (index = 0; index < size; ++index) target[index] = input[index];
    return destination;
#endif
}

void* memmove(void* destination, const void* source, size_t size) {
    unsigned char* target = destination;
    const unsigned char* input = source;
    size_t index;
    if (target < input) {
        for (index = 0; index < size; ++index) target[index] = input[index];
    } else {
        while (size != 0) {
            --size;
            target[size] = input[size];
        }
    }
    return destination;
}

void* memset(void* destination, int value, size_t size) {
#if defined(__x86_64__)
    void* result = destination;
    const uint64_t pattern = UINT64_C(0x0101010101010101) * (unsigned char)value;
    size_t words = size / 8, tail = size % 8;
    __asm__ volatile("rep stosq" : "+D"(destination), "+c"(words) : "a"(pattern) : "memory");
    __asm__ volatile("rep stosb" : "+D"(destination), "+c"(tail) : "a"(pattern) : "memory");
    return result;
#else
    unsigned char* target = destination;
    size_t index;
    for (index = 0; index < size; ++index) target[index] = (unsigned char)value;
    return destination;
#endif
}

int memcmp(const void* left, const void* right, size_t size) {
    const unsigned char* first = left;
    const unsigned char* second = right;
    size_t index;
    for (index = 0; index < size; ++index) {
        if (first[index] != second[index]) return first[index] - second[index];
    }
    return 0;
}

size_t strlen(const char* string) {
    size_t length = 0;
    while (string != NULL && string[length] != '\0') ++length;
    return length;
}

size_t strnlen(const char* string, size_t maximum) {
    size_t length = 0;
    while (string != NULL && length < maximum && string[length] != '\0') {
        ++length;
    }
    return length;
}

char* strcpy(char* destination, const char* source) {
    char* result = destination;
    while ((*destination++ = *source++) != '\0') {}
    return result;
}

char* strncpy(char* destination, const char* source, size_t size) {
    size_t index = 0;
    while (index < size && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    while (index < size) destination[index++] = '\0';
    return destination;
}

char* strcat(char* destination, const char* source) {
    strcpy(destination + strlen(destination), source);
    return destination;
}

int strcmp(const char* left, const char* right) {
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char* left, const char* right, size_t size) {
    while (size != 0 && *left != '\0' && *left == *right) {
        ++left;
        ++right;
        --size;
    }
    return size == 0 ? 0 : (unsigned char)*left - (unsigned char)*right;
}

char* strchr(const char* string, int character) {
    do {
        if (*string == (char)character) return (char*)string;
    } while (*string++ != '\0');
    return NULL;
}

char* strrchr(const char* string, int character) {
    const char* result = NULL;
    do {
        if (*string == (char)character) result = string;
    } while (*string++ != '\0');
    return (char*)result;
}

char* strstr(const char* string, const char* search) {
    size_t length = strlen(search);
    if (length == 0) return (char*)string;
    while (*string != '\0') {
        if (strncmp(string, search, length) == 0) return (char*)string;
        ++string;
    }
    return NULL;
}

char* strdup(const char* string) {
    size_t size = strlen(string) + 1;
    char* result = malloc(size);
    return result == NULL ? NULL : memcpy(result, string, size);
}
