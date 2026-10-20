#ifndef _OS_TASK_H
#define _OS_TASK_H

#include <cstdint>
#include "os/abi.h"

namespace os {

using TaskHandle = uint64_t;

class Task {
   public:
    [[noreturn]] static void signalReturn(void* frame);
    static uint64_t getThreadId();
    static uint64_t spawnProgram(ProgramId program, uint64_t bootstrapHandle,
                      uint32_t argumentCount       = 0,
                      const char* const* arguments = nullptr);
    static uint64_t spawnImage(const SpawnImageInfo& info);
    static int64_t waitChild(int64_t taskId, int& status, int options);
    static int waitThread(uint64_t threadId, int& exitCode, bool noHang = false);
    static int getState(uint64_t taskHandle, int& exitCode);
    static uint64_t getId();
    static int getInfo(uint64_t taskHandle, TaskInfo& info);
    static int terminate(uint64_t taskHandle, int exitCode);
    static int sendSignal(uint64_t taskId, uint32_t signal);
    static int sendThreadSignal(uint64_t threadId, uint32_t signal);
    static int64_t createSession();
    static int getIdentifiers(TaskIdentifiers& identifiers, uint64_t taskId = 0);
    static int setGroup(uint64_t taskGroupId, uint64_t taskId = 0);
    static int setSignalAction(uint32_t signal, const SignalAction* action,
                    SignalAction* oldAction);
    static int setSignalMask(int how, uint64_t mask, uint64_t& oldMask);
    static int getPendingSignals(uint64_t& mask);
    static int waitSignal(uint64_t mask);
    static int query(uint64_t taskId, TaskInfo& info);
    static int setCredentials(uint64_t taskId, uint32_t uid, uint32_t gid, bool effectiveOnly = false);

    static bool kill(uint64_t taskId);
    static int wait(TaskHandle task, int* exitCode);
    static TaskHandle executeWithIo(const char* path, int argumentCount,
                            const char* const* arguments,
                            uint64_t standardInputHandle,
                            uint64_t standardOutputHandle,
                            uint64_t standardErrorHandle = 0);
    static TaskHandle execute(const char* path);
    static TaskHandle execute(const char* path, int argumentCount,
                      const char* const* arguments);
    static TaskHandle executeService(const char* path, const char* name);
    static TaskHandle executeService(const char* path, int argumentCount, const char* const* arguments);
   private:
    static TaskHandle executeRequest(const char* path, int argumentCount, const char* const* arguments,
        uint64_t input, uint64_t output, uint64_t error, bool service);
};

}  // namespace os

#endif
