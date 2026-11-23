/*
 * 服务通用运行辅助。
 */

#pragma once
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

#include <cstddef>

namespace os {

    class RuntimeHeap;

    /* 保存一个进程的启动信息、环境、终端选择和退出处理。 */
    class Runtime {
        public:
            static Runtime& current();
            Runtime(const Runtime&) = delete;
            Runtime& operator=(const Runtime&) = delete;
            bool absolutePath(const char* path, char* buffer, size_t capacity);
            void bindWorkingDirectory(const char* (*reader)());
            uint32_t getConsoleId() const;
            int setConsoleId(uint32_t id);
            void initializeHeap();
            void* allocateMemory(size_t size);
            bool freeMemory(void* pointer);
            void prepareHeapFork();
            void finishHeapFork();
            void resetHeapAfterFork();
            uint64_t kernelBootstrapHandle() const;
            uint64_t registryBootstrapHandle() const;
            const char* environmentValue(const char* name) const;
            void bindEnvironment(char*** environment);
            int printString(const char* string);
            static void copyBytes(void* destination, const void* source,
                                  size_t size);
            static void clearMemory(void* destination, size_t size);
            static void copyString(char* destination, const char* source,
                                   size_t capacity);
            static bool stringsEqual(const char* left, const char* right);
            static size_t stringLength(const char* value);
            int addFinalizer(void (*function)());
            void runtimeFini();
            void prepare(char** environment);
            int runtimeInit(int argumentCount, char** arguments,
                            char** environment);

        private:
            constexpr Runtime() = default;
            ~Runtime() = default;
            static Runtime instance_;
            RuntimeHeap& heap();
            void runFinalizers();
            void lockFinalizers();
            void unlockFinalizers();

            char** initialEnvironment_ = nullptr;
            char*** environmentSlot_ = nullptr;
            void (*sharedFinalizer_)() = nullptr;
            bool prepared_ = false;
            bool finalized_ = false;
            uint32_t consoleId_ = 0;
            const char* (*workingDirectory_)() = nullptr;
            uint64_t startupKernelHandle_ = 0;
            uint64_t startupRegistryHandle_ = 0;
            void (*finalizers_[128])(){};
            uint32_t finalizerCount_ = 0;
            uint8_t finalizerLock_ = 0;
    };

    /* 进程、线程与内存操作的运行库入口。 */
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

    /* 解析固定缓冲区中的配置文本，检查字符及键值边界。 */
    class ConfigurationText {
        public:
            explicit ConfigurationText(const char* text);
            ConfigurationText(const char* text, size_t size);
            ~ConfigurationText() = default;
            bool valid() const;
            bool absolutePath() const;

        private:
            const char* text_;
            size_t size_;
    };

}  // namespace os
