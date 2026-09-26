#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "os/c_api.h"

#define LIBC_DESCRIPTOR_CAPACITY 32
#define LIBC_PATH_CAPACITY 96
#define LIBC_EXEC_STATE_MAGIC 0x4f53455845435354ULL

typedef enum DescriptorKind {
    DESCRIPTOR_FILE,
    DESCRIPTOR_PIPE_READ,
    DESCRIPTOR_PIPE_WRITE,
    DESCRIPTOR_TERMINAL_READ,
    DESCRIPTOR_TERMINAL_WRITE,
    DESCRIPTOR_SHARED_MEMORY
} DescriptorKind;

typedef struct OpenDescription {
    uint64_t handle;
    uint64_t offset;
    int flags;
    DescriptorKind kind;
    char path[LIBC_PATH_CAPACITY];
    uint32_t referenceCount;
} OpenDescription;

typedef struct FileDescriptor {
    OpenDescription* description;
    int closeOnExec;
} FileDescriptor;

typedef struct LibcExecDescriptorState {
    int32_t fileDescriptor;
    int32_t descriptionId;
    int32_t closeOnExec;
    int32_t kind;
    int32_t flags;
    uint32_t reserved;
    uint64_t handle;
    uint64_t offset;
    char path[LIBC_PATH_CAPACITY];
} LibcExecDescriptorState;

typedef struct LibcExecState {
    uint64_t magic;
    uint32_t size;
    uint32_t count;
    char currentDirectory[LIBC_PATH_CAPACITY];
    LibcExecDescriptorState descriptors[LIBC_DESCRIPTOR_CAPACITY];
} LibcExecState;

uint64_t libcFsService(void);
void libcCopyPath(char* destination, const char* path);
FileDescriptor* libcDescriptorSlot(int fileDescriptor);
OpenDescription* libcDescriptor(int fileDescriptor);
int libcAllocateDescriptor(uint64_t handle, const char* path, int flags,
                           DescriptorKind kind);
int libcReleaseDescriptor(int fileDescriptor);
void libcCloseAllDescriptors(void);
int libcDuplicateDescriptor(int fileDescriptor, int minimum, int closeOnExec);
void libcLockFileIo(void);
void libcUnlockFileIo(void);
void libcSetError(int value);
void libcSetCurrentDirectory(const char* path);
const char* libcGetCurrentDirectory(void);
pid_t libcRegisterChild(uint64_t handle);
int libcAfterForkChild(pid_t parentProcessId);
size_t libcExportExecState(LibcExecState* state);
void __libc_import_exec_state(const LibcExecState* state);

extern FileDescriptor libcDescriptors[LIBC_DESCRIPTOR_CAPACITY];
