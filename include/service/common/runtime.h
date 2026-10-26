#pragma once
#include <cstddef>

#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace os {
class Runtime {
   public:
    static bool absolutePath(const char* path, char* buffer, size_t capacity);
    static void bindWorkingDirectory(const char* (*reader)());
    static uint32_t getConsoleId();
    static int setConsoleId(uint32_t id);
    static void initializeHeap();
    static void* allocateMemory(size_t size);
    static bool freeMemory(void* pointer);
    static uint64_t kernelBootstrapHandle();
    static const char* environmentValue(const char* name);
    static void bindEnvironment(char*** environment);
    static int printString(const char* string);
    static void copyBytes(void* destination, const void* source, size_t size);
    static void clearMemory(void* destination, size_t size);
    static void copyString(char* destination, const char* source,
                           size_t capacity);
    static bool stringsEqual(const char* left, const char* right);
    static size_t stringLength(const char* value);
    static void runtimeFini();
    static void prepare(char** environment);
    static int runtimeInit(int argumentCount, char** arguments,
                           char** environment);

   private:
    static char** initialEnvironment_;
    static char*** environmentSlot_;
    static void (*sharedFinalizer_)();
    static bool prepared_;
    static uint32_t consoleId_;
    static const char* (*workingDirectory_)();
};
}  // namespace os

#include <os/task.h>
namespace os {
// Library helpers below use kernel-service IPC unless they are one of the six
// entries.
[[noreturn]] void exitTask(int exitCode);
[[noreturn]] void exitThread(int exitCode);
uint64_t createThread(void (*entry)(), uint64_t stackPointer);
uint64_t createThread(void (*entry)(void*), uint64_t stackPointer,
                      void* argument);
int waitAddress(volatile uint32_t* address, uint32_t expected);
int wakeAddress(volatile uint32_t* address, uint32_t count);
int64_t forkTask();
int replaceTaskImage(const Task::Exec& image);
void yield();
void sleep(uint64_t milliseconds);
uint64_t mapMemory(uint64_t address, uint64_t size, uint64_t protection);
bool unmapMemory(uint64_t address, uint64_t size);
bool protectMemory(uint64_t address, uint64_t size, uint64_t protection);
uint64_t getStandardHandle(uint32_t index);
uint64_t getBootstrapHandle();
bool closeHandle(uint64_t handle);


}
