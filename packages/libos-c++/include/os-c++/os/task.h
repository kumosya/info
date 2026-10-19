#ifndef _OS_TASK_H
#define _OS_TASK_H

#include <stdint.h>
#include "os/abi.h"

namespace os {

using TaskHandle = uint64_t;

struct TaskListEntry {
    uint64_t id;
    uint32_t state;
    char name[48];
};

class Process {
   public:
    [[noreturn]] static void signalReturn(void* frame);
    static uint64_t getThreadId();
    static uint64_t spawnProgram(ProgramId program, uint64_t bootstrapHandle,
                      uint32_t argumentCount       = 0,
                      const char* const* arguments = nullptr);
    static uint64_t spawnImage(const SpawnImageInfo& info);
    static int64_t waitChild(int64_t taskId, int& status, int options);
    static int waitThread(uint64_t threadId, int& exitCode, bool noHang = false);
    static int getTaskState(uint64_t taskHandle, int& exitCode);
    static uint64_t getTaskId();
    static int getTaskInfo(uint64_t taskHandle, TaskInfo& info);
    static int terminateTask(uint64_t taskHandle, int exitCode);
    static int sendSignal(uint64_t taskId, uint32_t signal);
    static int sendThreadSignal(uint64_t threadId, uint32_t signal);
    static int64_t createSession();
    static int getTaskIdentifiers(TaskIdentifiers& identifiers, uint64_t taskId = 0);
    static int setTaskGroup(uint64_t taskGroupId, uint64_t taskId = 0);
    static int setSignalAction(uint32_t signal, const SignalAction* action,
                    SignalAction* oldAction);
    static int setSignalMask(int how, uint64_t mask, uint64_t& oldMask);
    static int getPendingSignals(uint64_t& mask);
    static int waitSignal(uint64_t mask);
    static int queryTask(uint64_t taskId, TaskInfo& info);
    static int enumerateTask(uint32_t index, TaskInfo& info);
    static int setCredentials(uint64_t taskId, uint32_t uid, uint32_t gid, bool effectiveOnly = false);

    static bool killTask(uint64_t taskId);
    static bool listTask(uint32_t index, TaskListEntry& entry);
    static int waitTask(TaskHandle task, int* exitCode);
    static TaskHandle executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
    static TaskHandle execute(const char* path);
    static TaskHandle execute(const char* path, int argumentCount,
                      const char* const* arguments);
};

}  // namespace os

#endif
