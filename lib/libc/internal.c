#include "internal.h"

#include <errno.h>

static OpenDescription openDescriptions[LIBC_DESCRIPTOR_CAPACITY];
FileDescriptor libcDescriptors[LIBC_DESCRIPTOR_CAPACITY];

static uint64_t fsEndpoint;
static uint8_t fileIoLock;
static char currentDirectory[LIBC_PATH_CAPACITY];
static uint8_t standardClosed[3];

static OpenDescription* allocateOpenDescription(uint64_t handle,
                                                const char* path, int flags,
                                                DescriptorKind kind);

static void copyString(char* destination, const char* source, size_t capacity) {
    size_t index = 0;
    if (destination == NULL || capacity == 0) return;
    while (source != NULL && source[index] != '\0' && index + 1 < capacity) {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static size_t stringLength(const char* value) {
    size_t length = 0;
    while (value != NULL && value[length] != '\0') ++length;
    return length;
}

uint64_t libcFsService(void) {
    if (fsEndpoint == 0) fsEndpoint = os_get_fs_service();
    return fsEndpoint;
}

void libcCopyPath(char* destination, const char* path) {
    size_t length;
    if (path == NULL || *path == '\0') {
        destination[0] = '\0';
        return;
    }
    if (path[0] == '/') {
        copyString(destination, path, LIBC_PATH_CAPACITY);
        return;
    }
    if (currentDirectory[0] == '\0') {
        currentDirectory[0] = '/';
        currentDirectory[1] = '\0';
    }
    copyString(destination, currentDirectory, LIBC_PATH_CAPACITY);
    length = stringLength(destination);
    if (length != 1 && length + 1 < LIBC_PATH_CAPACITY) {
        destination[length++] = '/';
        destination[length] = '\0';
    }
    if (path[0] == '.' && path[1] == '/') path += 2;
    copyString(destination + length, path, LIBC_PATH_CAPACITY - length);
}

FileDescriptor* libcDescriptorSlot(int fileDescriptor) {
    FileDescriptor* value;
    if (fileDescriptor < 0 || fileDescriptor >= LIBC_DESCRIPTOR_CAPACITY) {
        return NULL;
    }
    value = &libcDescriptors[fileDescriptor];
    return value->description == NULL ? NULL : value;
}

OpenDescription* libcDescriptor(int fileDescriptor) {
    FileDescriptor* slot = libcDescriptorSlot(fileDescriptor);
    if (slot == NULL && fileDescriptor >= 0 && fileDescriptor < 3 &&
        standardClosed[fileDescriptor] == 0) {
        uint64_t handle = os_get_standard_handle((uint32_t)fileDescriptor);
        DescriptorKind kind = fileDescriptor == 0
                                  ? (handle == 0 ? DESCRIPTOR_TERMINAL_READ
                                                 : DESCRIPTOR_PIPE_READ)
                                  : (handle == 0 ? DESCRIPTOR_TERMINAL_WRITE
                                                 : DESCRIPTOR_PIPE_WRITE);
        OpenDescription* description = allocateOpenDescription(
            handle, "terminal", fileDescriptor == 0 ? 0 : 1, kind);
        if (description == NULL) return NULL;
        libcDescriptors[fileDescriptor].description = description;
        libcDescriptors[fileDescriptor].closeOnExec = 0;
        slot = &libcDescriptors[fileDescriptor];
    }
    return slot == NULL ? NULL : slot->description;
}

static OpenDescription* allocateOpenDescription(uint64_t handle,
                                                const char* path, int flags,
                                                DescriptorKind kind) {
    uint32_t index;
    for (index = 0; index < LIBC_DESCRIPTOR_CAPACITY; ++index) {
        OpenDescription* description = &openDescriptions[index];
        if (description->referenceCount != 0) continue;
        description->handle = handle;
        description->offset = 0;
        description->flags = flags;
        description->kind = kind;
        description->referenceCount = 1;
        copyString(description->path, path, sizeof(description->path));
        return description;
    }
    errno = EMFILE;
    return NULL;
}

int libcAllocateDescriptor(uint64_t handle, const char* path, int flags,
                           DescriptorKind kind) {
    int index;
    OpenDescription* description =
        allocateOpenDescription(handle, path, flags, kind);
    if (description == NULL) return -1;
    for (index = 3; index < LIBC_DESCRIPTOR_CAPACITY; ++index) {
        if (libcDescriptors[index].description != NULL) continue;
        libcDescriptors[index].description = description;
        libcDescriptors[index].closeOnExec = 0;
        return index;
    }
    description->referenceCount = 0;
    errno = EMFILE;
    return -1;
}

int libcReleaseDescriptor(int fileDescriptor) {
    FileDescriptor* slot = libcDescriptorSlot(fileDescriptor);
    OpenDescription* description;
    int result;
    if (slot == NULL) return 0;
    description = slot->description;
    slot->description = NULL;
    slot->closeOnExec = 0;
    if (fileDescriptor >= 0 && fileDescriptor < 3) {
        standardClosed[fileDescriptor] = 1;
    }
    --description->referenceCount;
    if (description->referenceCount != 0) return 1;
    if (description->kind == DESCRIPTOR_FILE) {
        result = os_close_file(libcFsService(), description->handle);
    } else if ((description->kind == DESCRIPTOR_TERMINAL_READ ||
                description->kind == DESCRIPTOR_TERMINAL_WRITE) &&
               description->handle == 0) {
        result = 1;
    } else {
        result = os_close_handle(description->handle);
    }
    description->handle = 0;
    description->offset = 0;
    description->flags = 0;
    description->path[0] = '\0';
    return result;
}

void libcCloseAllDescriptors(void) {
    int fileDescriptor;
    for (fileDescriptor = 0;
         fileDescriptor < LIBC_DESCRIPTOR_CAPACITY; ++fileDescriptor) {
        if (libcDescriptors[fileDescriptor].description != NULL) {
            libcReleaseDescriptor(fileDescriptor);
        }
    }
}

int libcDuplicateDescriptor(int fileDescriptor, int minimum, int closeOnExec) {
    int index;
    OpenDescription* description = libcDescriptor(fileDescriptor);
    if (description == NULL) {
        errno = EBADF;
        return -1;
    }
    if (minimum < 0 || minimum >= LIBC_DESCRIPTOR_CAPACITY) {
        errno = EINVAL;
        return -1;
    }
    for (index = minimum; index < LIBC_DESCRIPTOR_CAPACITY; ++index) {
        if (libcDescriptors[index].description != NULL) continue;
        libcDescriptors[index].description = description;
        libcDescriptors[index].closeOnExec = closeOnExec;
        ++description->referenceCount;
        return index;
    }
    errno = EMFILE;
    return -1;
}

void libcLockFileIo(void) {
    while (__atomic_test_and_set(&fileIoLock, __ATOMIC_ACQUIRE)) os_yield();
}

void libcUnlockFileIo(void) {
    __atomic_clear(&fileIoLock, __ATOMIC_RELEASE);
}

void libcSetError(int value) { errno = value; }

void libcSetCurrentDirectory(const char* path) {
    copyString(currentDirectory, path, sizeof(currentDirectory));
}

const char* libcGetCurrentDirectory(void) {
    if (currentDirectory[0] == '\0') {
        currentDirectory[0] = '/';
        currentDirectory[1] = '\0';
    }
    return currentDirectory;
}

int libcAfterForkChild(pid_t parentProcessId) {
    uint64_t oldDescriptors[LIBC_DESCRIPTOR_CAPACITY];
    uint64_t newDescriptors[LIBC_DESCRIPTOR_CAPACITY];
    const int count = os_clone_file_descriptors(
        libcFsService(), (uint64_t)parentProcessId, oldDescriptors,
        newDescriptors, LIBC_DESCRIPTOR_CAPACITY);
    uint32_t descriptionIndex;
    fileIoLock = 0;
    if (count < 0) return -1;
    for (descriptionIndex = 0;
         descriptionIndex < LIBC_DESCRIPTOR_CAPACITY; ++descriptionIndex) {
        OpenDescription* description = &openDescriptions[descriptionIndex];
        int mapping;
        if (description->referenceCount == 0 ||
            description->kind != DESCRIPTOR_FILE) {
            continue;
        }
        for (mapping = 0; mapping < count; ++mapping) {
            if (description->handle == oldDescriptors[mapping]) {
                description->handle = newDescriptors[mapping];
                break;
            }
        }
        if (mapping == count) return -1;
    }
    return 0;
}

size_t libcExportExecState(LibcExecState* state) {
    int fileDescriptor;
    if (state == NULL) return 0;
    state->magic = LIBC_EXEC_STATE_MAGIC;
    state->size = sizeof(*state);
    state->count = 0;
    copyString(state->currentDirectory, libcGetCurrentDirectory(),
               sizeof(state->currentDirectory));
    for (fileDescriptor = 0;
         fileDescriptor < LIBC_DESCRIPTOR_CAPACITY; ++fileDescriptor) {
        FileDescriptor* slot = &libcDescriptors[fileDescriptor];
        LibcExecDescriptorState* output;
        OpenDescription* description = slot->description;
        if (description == NULL) continue;
        output = &state->descriptors[state->count++];
        output->fileDescriptor = fileDescriptor;
        output->descriptionId =
            (int32_t)(description - openDescriptions);
        output->closeOnExec = slot->closeOnExec;
        output->kind = description->kind;
        output->flags = description->flags;
        output->reserved = 0;
        output->handle = description->handle;
        output->offset = description->offset;
        copyString(output->path, description->path, sizeof(output->path));
    }
    return sizeof(*state);
}

void __libc_import_exec_state(const LibcExecState* state) {
    uint32_t index;
    uint8_t restored[LIBC_DESCRIPTOR_CAPACITY] = {0};
    uint8_t seen[LIBC_DESCRIPTOR_CAPACITY] = {0};
    if (state == NULL || state->magic != LIBC_EXEC_STATE_MAGIC ||
        state->size != sizeof(*state) ||
        state->count > LIBC_DESCRIPTOR_CAPACITY) {
        return;
    }
    copyString(currentDirectory, state->currentDirectory,
               sizeof(currentDirectory));
    for (index = 0; index < state->count; ++index) {
        const LibcExecDescriptorState* input = &state->descriptors[index];
        OpenDescription* description;
        if (input->fileDescriptor < 0 ||
            input->fileDescriptor >= LIBC_DESCRIPTOR_CAPACITY ||
            input->descriptionId < 0 ||
            input->descriptionId >= LIBC_DESCRIPTOR_CAPACITY ||
            input->closeOnExec) {
            continue;
        }
        description = &openDescriptions[input->descriptionId];
        if (!restored[input->descriptionId]) {
            description->handle = input->handle;
            description->offset = input->offset;
            description->flags = input->flags;
            description->kind = (DescriptorKind)input->kind;
            description->referenceCount = 0;
            copyString(description->path, input->path,
                       sizeof(description->path));
            restored[input->descriptionId] = 1;
        }
        libcDescriptors[input->fileDescriptor].description = description;
        libcDescriptors[input->fileDescriptor].closeOnExec = 0;
        ++description->referenceCount;
    }
    for (index = 0; index < state->count; ++index) {
        const LibcExecDescriptorState* input = &state->descriptors[index];
        if (input->descriptionId < 0 ||
            input->descriptionId >= LIBC_DESCRIPTOR_CAPACITY ||
            seen[input->descriptionId]) {
            continue;
        }
        seen[input->descriptionId] = 1;
        if (restored[input->descriptionId]) continue;
        if ((DescriptorKind)input->kind == DESCRIPTOR_FILE) {
            os_close_file(libcFsService(), input->handle);
        } else if (input->handle != 0) {
            os_close_handle(input->handle);
        }
    }
    for (index = 0; index < state->count; ++index) {
        const LibcExecDescriptorState* input = &state->descriptors[index];
        if (input->closeOnExec && input->fileDescriptor >= 0 &&
            input->fileDescriptor < 3) {
            standardClosed[input->fileDescriptor] = 1;
        }
    }
}
