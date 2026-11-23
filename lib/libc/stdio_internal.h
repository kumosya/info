#pragma once
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

/* 文件流共用的状态：锁、缓冲，以及可选的内存文件位置和所有权。 */
typedef struct __FileState {
        FILE* stream;
        struct __FileState* next;
        volatile uint32_t lock;
        uint64_t owner;
        unsigned depth;
        unsigned readable, writable, append;
        int mode, phase;
        unsigned char* buffer;
        size_t capacity, begin, end;
        int buffer_owned;
        unsigned char* memory;
        size_t memory_capacity, length, position;
        int memory_kind, memory_owned;
        int memory_truncated;
        char** result;
        size_t* result_size;
        wchar_t** wide_result;
        wint_t wide_pushed;
        wint_t numeric_pushed;
        pid_t child;
} FileState;

FileState* __libc_stream_state(FILE* stream);
int __libc_stream_mode(const char* mode, int* flags);
int __libc_stream_flush(FileState* state);
int __libc_getc_unlocked(FILE* stream);
int __libc_putc_unlocked(int value, FILE* stream);
int __libc_stream_write_wchar(FileState* state, wchar_t value);
int __libc_popen_descriptors(int* descriptors, size_t capacity);
FILE* __libc_wide_string_stream(wchar_t* memory, size_t length, int input);
