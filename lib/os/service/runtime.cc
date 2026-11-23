#include <os/console.h>
#include <os/detail/client.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include "service/common/identity.h"
#include "service/common/protocol.h"
#include "service/common/records.h"

extern "C" {
using RuntimeFunction = void (*)();
extern RuntimeFunction __init_array_start[];
extern RuntimeFunction __init_array_end[];
extern RuntimeFunction __fini_array_start[];
extern RuntimeFunction __fini_array_end[];
}

namespace os {

    constinit Runtime Runtime::instance_{};

    Runtime& Runtime::current() {
        return instance_;
    }

    int Runtime::addFinalizer(void (*function)()) {
        if (!function) {
            return -1;
        }

        lockFinalizers();

        if (finalizerCount_ == 128) {
            unlockFinalizers();
            return -1;
        }

        finalizers_[finalizerCount_++] = function;
        unlockFinalizers();
        return 0;
    }

    void Runtime::runFinalizers() {
        while (true) {
            lockFinalizers();

            if (!finalizerCount_) {
                unlockFinalizers();
                return;
            }

            void (*function)() = finalizers_[--finalizerCount_];
            unlockFinalizers();
            function();
        }
    }

    void Runtime::lockFinalizers() {
        while (__atomic_test_and_set(&finalizerLock_, __ATOMIC_ACQUIRE)) {
            asm volatile("pause");
        }
    }

    void Runtime::unlockFinalizers() {
        __atomic_clear(&finalizerLock_, __ATOMIC_RELEASE);
    }

    int Runtime::printString(const char* string) {
        if (string == nullptr) {
            return static_cast<int>(ErrorCode::InvalidArgument);
        }

        Console console;
        const int opened = console.openCurrent();

        if (opened) {
            return opened;
        }

        const int64_t result =
            console.write(string, Runtime::stringLength(string));
        return result < 0 ? static_cast<int>(result) : 0;
    }

    const char* Runtime::environmentValue(const char* name) const {
        if (!name || !*name) {
            return nullptr;
        }

        size_t length = 0;

        while (name[length]) {
            if (name[length] == '=') {
                return nullptr;
            }

            ++length;
        }

        char** environment =
            environmentSlot_ ? *environmentSlot_ : initialEnvironment_;
        if (!environment) {
            return nullptr;
        }

        for (size_t index = 0; environment[index]; ++index) {
            const char* entry = environment[index];
            size_t position = 0;

            while (position < length && entry[position] == name[position]) {
                ++position;
            }

            if (position == length && entry[position] == '=') {
                return entry + length + 1;
            }
        }

        return nullptr;
    }

    void Runtime::bindEnvironment(char*** environment) {
        environmentSlot_ = environment;
    }

    void Runtime::prepare(char** environment) {
        if (prepared_) {
            return;
        }

        prepared_ = true;
        initialEnvironment_ = environment;
        environmentSlot_ = nullptr;
        sharedFinalizer_ = nullptr;

        if (environment) {
            char** end = environment;

            while (*end) {
                ++end;
            }

            const auto* auxiliary = reinterpret_cast<const uint64_t*>(end + 1);

            for (uint32_t index = 0; index < 32 && auxiliary[index * 2];
                 ++index) {
                const uint64_t tag = auxiliary[index * 2],
                               value = auxiliary[index * 2 + 1];
                if (tag == OS_AUX_KERNEL_HANDLE) {
                    startupKernelHandle_ = value;
                }

                if (tag == OS_AUX_REGISTRY_HANDLE) {
                    startupRegistryHandle_ = value;
                }

                if (tag == 0x60000003) {
                    sharedFinalizer_ = reinterpret_cast<void (*)()>(value);
                }
            }
        }

        Runtime::initializeHeap();
        consoleId_ = 0;
        workingDirectory_ = nullptr;
    }

    int Runtime::runtimeInit(int argumentCount, char** arguments,
                             char** environment) {
        prepare(environment);
        constexpr char prefix[] = "--os-terminal=";

        for (int index = 0; index < argumentCount; ++index) {
            const char* argument = arguments[index];
            uint32_t position = 0;

            while (argument != nullptr && prefix[position] != '\0' &&
                   argument[position] == prefix[position]) {
                ++position;
            }

            if (prefix[position] != '\0' || argument[position] < '1' ||
                argument[position] > '7' || argument[position + 1] != '\0') {
                continue;
            }

            setConsoleId(static_cast<uint32_t>(argument[position] - '1'));

            for (int move = index + 1; move < argumentCount; ++move) {
                arguments[move - 1] = arguments[move];
            }

            --argumentCount;
            arguments[argumentCount] = nullptr;
            break;
        }

        for (RuntimeFunction* constructor = __init_array_start;
             constructor < __init_array_end; ++constructor) {
            (*constructor)();
        }

        return argumentCount;
    }

    void Runtime::runtimeFini() {
        if (finalized_) {
            return;
        }

        finalized_ = true;
        runFinalizers();

        for (RuntimeFunction* destructor = __fini_array_end;
             destructor != __fini_array_start;) {
            (*--destructor)();
        }

        if (sharedFinalizer_) {
            RuntimeFunction finalizer = sharedFinalizer_;
            sharedFinalizer_ = nullptr;
            finalizer();
        }
    }

    size_t Runtime::stringLength(const char* value) {
        size_t length = 0;

        while (value != nullptr && value[length] != '\0') {
            ++length;
        }

        return length;
    }

    bool Runtime::stringsEqual(const char* left, const char* right) {
        if (left == nullptr || right == nullptr) {
            return left == right;
        }

        while (*left != '\0' && *left == *right) {
            ++left;
            ++right;
        }

        return *left == *right;
    }

    void Runtime::copyString(char* destination, const char* source,
                             size_t capacity) {
        if (destination == nullptr || capacity == 0) {
            return;
        }

        size_t index = 0;

        while (source != nullptr && source[index] != '\0' &&
               index + 1 < capacity) {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
    }

    void Runtime::clearMemory(void* destination, size_t size) {
        size_t words = size / 8, tail = size % 8;
        asm volatile("rep stosq"
                     : "+D"(destination), "+c"(words)
                     : "a"(uint64_t(0))
                     : "memory");
        asm volatile("rep stosb"
                     : "+D"(destination), "+c"(tail)
                     : "a"(uint64_t(0))
                     : "memory");
    }

    void Runtime::copyBytes(void* destination, const void* source,
                            size_t size) {
        size_t words = size / 8, tail = size % 8;
        asm volatile("rep movsq"
                     : "+D"(destination), "+S"(source), "+c"(words)
                     :
                     : "memory");
        asm volatile("rep movsb"
                     : "+D"(destination), "+S"(source), "+c"(tail)
                     :
                     : "memory");
    }

    uint64_t Runtime::kernelBootstrapHandle() const {
        return startupKernelHandle_;
    }

    uint64_t Runtime::registryBootstrapHandle() const {
        return startupRegistryHandle_;
    }

    uint64_t getBootstrapHandle() {
        return Runtime::current().registryBootstrapHandle();
    }
}  // namespace os

extern "C" int osRuntimeInit(int argc, char** argv, char** environment) {
    return os::Runtime::current().runtimeInit(argc, argv, environment);
}

extern "C" void osRuntimeFini() {
    os::Runtime::current().runtimeFini();
}

extern "C" __attribute__((weak)) int atexit(void (*function)()) {
    return os::Runtime::current().addFinalizer(function);
}

namespace os {

    uint32_t Runtime::getConsoleId() const {
        return __atomic_load_n(&consoleId_, __ATOMIC_ACQUIRE);
    }

    int Runtime::setConsoleId(uint32_t id) {
        if (id > 6) {
            return static_cast<int>(ErrorCode::InvalidArgument);
        }

        __atomic_store_n(&consoleId_, id, __ATOMIC_RELEASE);
        return 0;
    }

    void Runtime::bindWorkingDirectory(const char* (*reader)()) {
        workingDirectory_ = reader;
    }

    bool Runtime::absolutePath(const char* path, char* buffer,
                               size_t capacity) {
        if (!path || !*path || !buffer || capacity < 2) {
            return false;
        }

        const char* cwd =
            workingDirectory_ ? workingDirectory_() : environmentValue("PWD");
        if (!cwd || cwd[0] != '/') {
            cwd = "/";
        }

        char combined[768];
        size_t used = 0;

        if (path[0] != '/') {
            const size_t n = stringLength(cwd);

            if (n + 2 >= sizeof(combined)) {
                return false;
            }

            copyBytes(combined, cwd, n);
            used = n;
            combined[used++] = '/';
        }

        const size_t n = stringLength(path);

        if (n >= sizeof(combined) - used) {
            return false;
        }

        copyBytes(combined + used, path, n + 1);
        size_t write = 1;
        buffer[0] = '/';
        size_t position = 0;

        while (combined[position]) {
            while (combined[position] == '/') {
                ++position;
            }

            const size_t first = position;

            while (combined[position] && combined[position] != '/') {
                ++position;
            }

            const size_t length = position - first;

            if (!length) {
                break;
            }

            if (length == 1 && combined[first] == '.') {
                continue;
            }

            if (length == 2 && combined[first] == '.' &&
                combined[first + 1] == '.') {
                while (write > 1 && buffer[write - 1] != '/') {
                    --write;
                }

                if (write > 1) {
                    --write;
                }

                continue;
            }

            const size_t separator = write > 1 ? 1 : 0;

            if (separator + length >= capacity - write) {
                return false;
            }

            if (separator) {
                buffer[write++] = '/';
            }

            copyBytes(buffer + write, combined + first, length);
            write += length;
        }

        buffer[write] = 0;
        return true;
    }
}  // namespace os
