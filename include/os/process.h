#pragma once

#include <stdint.h>

namespace os {

using ProcessHandle = uint64_t;

struct ProcessListEntry {
    uint64_t id;
    uint32_t state;
    char name[48];
};

ProcessHandle execute(const char* path);
ProcessHandle execute(const char* path, int argumentCount,
                      const char* const* arguments);
ProcessHandle executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
int waitProcess(ProcessHandle process, int* exitCode);
bool listProcess(uint32_t index, ProcessListEntry& entry);
bool killProcess(uint64_t processId);

}  // namespace os
