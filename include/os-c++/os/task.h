#ifndef _OS_TASK_H
#define _OS_TASK_H

#include <stdint.h>

namespace os {

using TaskHandle = uint64_t;

struct TaskListEntry {
    uint64_t id;
    uint32_t state;
    char name[48];
};

TaskHandle execute(const char* path);
TaskHandle execute(const char* path, int argumentCount,
                      const char* const* arguments);
TaskHandle executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
int waitTask(TaskHandle task, int* exitCode);
bool listTask(uint32_t index, TaskListEntry& entry);
bool killTask(uint64_t taskId);

}  // namespace os

#endif
