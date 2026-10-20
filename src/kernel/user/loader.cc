#include "kernel/boot.h"
#include "kernel/ipc.h"
#include "kernel/page.h"
#include "kernel/serial.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace {

constexpr uint64_t PAGE_SIZE                 = 4096;
constexpr uint64_t USER_STACK_TOP            = 0x00007ffffff00000ULL;
constexpr uint64_t USER_STACK_PAGES          = 64;
constexpr uint32_t MAXIMUM_ARGUMENT_LENGTH   = 383;
constexpr uint32_t MAXIMUM_ARGUMENT_COUNT    = 64;
constexpr uint32_t MAXIMUM_ENVIRONMENT_COUNT = 64;
constexpr uint64_t DIRECT_MAP_BASE           = 0xffff800000000000ULL;

struct ProgramImage {
    const char* moduleName;
    const char* name;
    uint64_t sourceAddress;
    uint64_t size;
    uint64_t capabilities;
    os::ServiceRole role;
    bool builtin;
};

#include "boot-abi.inc"
#include "boot-images.inc"
kernel::ElfSharedImage bootRuntimeImages[configuredRuntimeCount]{};
bool bootProgramsReady;

bool moduleNameEquals(const char* commandLine, const char* expected) {
    if (commandLine == nullptr || expected == nullptr) return false;
    uint32_t index = 0;
    while (expected[index] != '\0' && commandLine[index] == expected[index]) {
        ++index;
    }
    return expected[index] == '\0' &&
           (commandLine[index] == '\0' || commandLine[index] == ' ');
}

const kernel::BootModule* findModule(const kernel::BootInfo& bootInfo,
                                     const char* name) {
    for (uint32_t index = 0; index < bootInfo.moduleCount; ++index) {
        const kernel::BootModule& module = bootInfo.modules[index];
        if (module.end > module.start &&
            moduleNameEquals(module.commandLine, name)) {
            return &module;
        }
    }
    return nullptr;
}

bool initializeBootPrograms(const kernel::BootInfo& bootInfo) {
    for (ProgramImage& image : PROGRAM_IMAGES) {
        if (!image.builtin) continue;
        const kernel::BootModule* module =
            findModule(bootInfo, image.moduleName);
        if (module == nullptr) return false;
        image.sourceAddress = DIRECT_MAP_BASE + module->start;
        image.size          = module->end - module->start;
    }
    for (uint32_t i = 0; i < configuredRuntimeCount; ++i) {
        const auto* module = findModule(bootInfo, configuredRuntimeLibraries[i]);
        if (!module) return false;
        bootRuntimeImages[i] = {DIRECT_MAP_BASE + module->start, module->end - module->start,
                                configuredRuntimeLibraries[i], kernel::memory::getKernelAddressSpaceRoot()};
    }
    bootProgramsReady = true;
    return true;
}

uint64_t alignDown(uint64_t value, uint64_t alignment) {
    return value & ~(alignment - 1);
}

uint32_t stringLength(const char* value) {
    uint32_t length = 0;
    while (value != nullptr && value[length] != '\0' &&
           length <= MAXIMUM_ARGUMENT_LENGTH) {
        ++length;
    }
    return length;
}

}  // namespace

namespace kernel {

ElfLoadError UserProgramLoader::buildStack(
    uint64_t root, const char* programName, uint32_t argumentCount,
    const char* const* arguments, uint32_t environmentCount,
    const char* const* environment, uint64_t runtimeStateAddress,
    uint64_t& stackPointer, const ElfLoadResult* image) {
    const uint64_t stackBottom = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t address = stackBottom; address < USER_STACK_TOP;
         address += PAGE_SIZE) {
        const UserPageMapResult mapResult =
            memory::allocateUserPageInAddressSpace(
                root, address, pageUser | pageWritable | pageNoExecute);
        if (mapResult == UserPageMapResult::OutOfMemory) {
            return ElfLoadError::OutOfMemory;
        }
        if (mapResult != UserPageMapResult::Success) {
            return ElfLoadError::MappingFailed;
        }
    }

    const char* defaultArguments[] = {programName};
    if (argumentCount == 0) {
        argumentCount = 1;
        arguments     = defaultArguments;
    }
    if (argumentCount > MAXIMUM_ARGUMENT_COUNT || arguments == nullptr) {
        return ElfLoadError::InvalidArgument;
    }
    uint64_t cursor = USER_STACK_TOP;
    uint64_t argumentAddresses[MAXIMUM_ARGUMENT_COUNT]{};
    uint64_t environmentAddresses[MAXIMUM_ENVIRONMENT_COUNT]{};
    if (environmentCount > MAXIMUM_ENVIRONMENT_COUNT ||
        (environmentCount != 0 && environment == nullptr)) {
        return ElfLoadError::InvalidArgument;
    }
    for (uint32_t index = environmentCount; index != 0; --index) {
        const uint32_t length = stringLength(environment[index - 1]);
        if (length > MAXIMUM_ARGUMENT_LENGTH ||
            cursor < stackBottom + length + 1) {
            return ElfLoadError::InvalidArgument;
        }
        cursor -= length + 1;
        if (!memory::copyToAddressSpace(root, cursor, environment[index - 1],
                                        length + 1)) {
            return ElfLoadError::MappingFailed;
        }
        environmentAddresses[index - 1] = cursor;
    }
    for (uint32_t index = argumentCount; index != 0; --index) {
        const uint32_t argumentLength = stringLength(arguments[index - 1]);
        if (argumentLength > MAXIMUM_ARGUMENT_LENGTH ||
            cursor < stackBottom + argumentLength + 1) {
            return ElfLoadError::InvalidArgument;
        }
        cursor -= argumentLength + 1;
        if (!memory::copyToAddressSpace(root, cursor, arguments[index - 1],
                                        argumentLength + 1)) {
            return ElfLoadError::MappingFailed;
        }
        argumentAddresses[index - 1] = cursor;
    }

    const uint32_t executableNameLength = stringLength(programName);
    if (executableNameLength > MAXIMUM_ARGUMENT_LENGTH || cursor < stackBottom + executableNameLength + 1)
        return ElfLoadError::InvalidArgument;
    cursor -= executableNameLength + 1;
    const uint64_t executableNameAddress = cursor;
    if (!memory::copyToAddressSpace(root, cursor, programName, executableNameLength + 1))
        return ElfLoadError::MappingFailed;
    cursor                          = alignDown(cursor, 16);
    constexpr uint32_t auxiliaryWords = 20;
    const uint64_t startupWordCount = argumentCount + environmentCount + 3 + auxiliaryWords;
    cursor = alignDown(cursor - startupWordCount * sizeof(uint64_t), 16);
    uint64_t
        startupWords[MAXIMUM_ARGUMENT_COUNT + MAXIMUM_ENVIRONMENT_COUNT + 3 + auxiliaryWords]{};
    startupWords[0] = argumentCount;
    for (uint32_t index = 0; index < argumentCount; ++index) {
        startupWords[index + 1] = argumentAddresses[index];
    }
    const uint32_t environmentStart = argumentCount + 2;
    for (uint32_t index = 0; index < environmentCount; ++index) {
        startupWords[environmentStart + index] = environmentAddresses[index];
    }
    const uint64_t auxiliary[] = {
        3, image ? image->programHeaders : 0, 4, 56,
        5, image ? image->programHeaderCount : 0, 6, PAGE_SIZE,
        7, image ? image->interpreterBase : 0,
        9, image ? image->applicationEntry : 0,
        31, executableNameAddress,
        0x60000001, runtimeStateAddress,
        0x60000002, image ? image->applicationBias : 0, 0, 0};
    for (uint32_t index = 0; index < auxiliaryWords; ++index)
        startupWords[environmentStart + environmentCount + 1 + index] = auxiliary[index];
    if (cursor < stackBottom ||
        !memory::copyToAddressSpace(root, cursor, startupWords,
                                    startupWordCount * sizeof(uint64_t))) {
        return ElfLoadError::MappingFailed;
    }
    stackPointer = cursor;
    return ElfLoadError::None;
}

TaskReference UserProgramLoader::load(os::ProgramId program, Task* parent,
                              uint64_t bootstrapHandle, uint32_t argumentCount,
                              const char* const* arguments,
                              ElfLoadError* loadError) {
    if (loadError != nullptr) {
        *loadError = ElfLoadError::None;
    }
    const uint64_t index = static_cast<uint64_t>(program);
    if (index >= static_cast<uint64_t>(os::ProgramId::Count) ||
        parent == nullptr || !bootProgramsReady) {
        if (loadError != nullptr) {
            *loadError = ElfLoadError::InvalidArgument;
        }
        return nullptr;
    }

    const ProgramImage& image = PROGRAM_IMAGES[index];
    if (!image.builtin) { if (loadError) *loadError = ElfLoadError::InvalidArgument; return nullptr; }
    Task* task                = Task::create(image.name, parent);
    if (task == nullptr) {
        if (loadError != nullptr) {
            *loadError = ElfLoadError::OutOfMemory;
        }
        return nullptr;
    }
    TaskReference lifetime(task);
    task->grantCapabilities(image.capabilities);
    task->setServiceRole(image.role);
    task->setCommandLine(argumentCount, arguments);
    // Immutable boot identities grant mechanism permissions, never Ring 0 execution.
    task->setSystemService(false);

    if (bootstrapHandle != 0) {
        const uint64_t childHandle =
            Endpoint::duplicateHandle(parent, task, bootstrapHandle);
        if (childHandle == 0) {
            task->destroy();
            if (loadError != nullptr) {
                *loadError = ElfLoadError::InvalidArgument;
            }
            return nullptr;
        }
        task->setBootstrapHandle(childHandle);
        task->handles().restrictRights(childHandle, handleRightWrite);
    }

    ElfLoadResult result{};
    ElfLoadError error = ElfLoader::loadDynamicSetFromAddressSpace(
        task->addressSpace().rootPhysicalAddress(),
        memory::getKernelAddressSpaceRoot(), image.sourceAddress, image.size,
        bootRuntimeImages, configuredRuntimeCount, result, task->isSystemService());
    uint64_t stackPointer = 0;
    if (error == ElfLoadError::None) {
        error = UserProgramLoader::buildStack(
            task->addressSpace().rootPhysicalAddress(), image.name,
            argumentCount, arguments, 0, nullptr, 0, stackPointer, &result);
    }
    if (error != ElfLoadError::None ||
        Thread::createUser(task, result.entryPoint, stackPointer, 0,
                           USER_STACK_TOP - stackPointer) == nullptr) {
        task->destroy();
        if (error == ElfLoadError::None) {
            error = ElfLoadError::OutOfMemory;
        }
        if (loadError != nullptr) {
            *loadError = error;
        }
        return nullptr;
    }
    return lifetime;
}

TaskReference UserProgramLoader::loadImage(
    uint64_t sourceAddress, size_t imageSize, const char* name, Task* source,
    Task* parent, uint64_t bootstrapHandle, uint64_t capabilities,
    uint64_t standardInputHandle, uint64_t standardOutputHandle,
    uint64_t standardErrorHandle, uint32_t argumentCount,
    const char* const* arguments, const ElfSharedImage* sharedImages,
    uint32_t sharedImageCount, ElfLoadError* loadError, os::ServiceRole serviceRole, bool newSession,
    bool managedService, uint32_t terminalKind, uint32_t terminalIndex) {
    if (loadError != nullptr) {
        *loadError = ElfLoadError::None;
    }
    if (sourceAddress == 0 || imageSize == 0 || name == nullptr ||
        source == nullptr || parent == nullptr) {
        if (loadError != nullptr) {
            *loadError = ElfLoadError::InvalidArgument;
        }
        return nullptr;
    }
    Task* task = Task::create(name, parent);
    if (task == nullptr) {
        if (loadError != nullptr) {
            *loadError = ElfLoadError::OutOfMemory;
        }
        return nullptr;
    }
    task->grantCapabilities(capabilities);
    TaskReference lifetime(task);
    task->setServiceRole(serviceRole);
    task->setManagedService(managedService || serviceRole != os::ServiceRole::None);
    task->setCommandLine(argumentCount, arguments);
    if (bootstrapHandle != 0) {
        const uint64_t childHandle =
            Endpoint::duplicateHandle(source, task, bootstrapHandle);
        if (childHandle == 0) {
            task->destroy();
            if (loadError != nullptr) {
                *loadError = ElfLoadError::InvalidArgument;
            }
            return nullptr;
        }
        task->setBootstrapHandle(childHandle);
        task->handles().restrictRights(childHandle, handleRightWrite);
    }
    const uint64_t standardHandles[] = {
        standardInputHandle, standardOutputHandle, standardErrorHandle};
    bool hasPtyStandardHandle = newSession || managedService;
    for (uint32_t index = 0; index < 3; ++index) {
        if (standardHandles[index] == 0) {
            continue;
        }
        void* standardObject    = nullptr;
        HandleType standardType = HandleType::None;
        uint32_t standardRights = 0;
        source->handles().inspect(standardHandles[index], &standardObject,
                                  &standardType, &standardRights);
        const uint64_t childHandle =
            standardType == HandleType::Endpoint ? Endpoint::duplicateHandle(source, task, standardHandles[index]) : 0;
        if (childHandle == 0) {
            task->destroy();
            if (loadError != nullptr) {
                *loadError = ElfLoadError::InvalidArgument;
            }
            return nullptr;
        }
        task->setStandardHandle(index, childHandle);
    }
    if (hasPtyStandardHandle && !task->createSession()) {
        task->destroy();
        if (loadError != nullptr) {
            *loadError = ElfLoadError::InvalidArgument;
        }
        return nullptr;
    }
    if (terminalKind) task->setTerminal(terminalKind, terminalIndex);

    ElfLoadResult result{};
    ElfLoadError error = serviceRole != os::ServiceRole::None
            ? ElfLoader::loadDynamicSetFromAddressSpace(task->addressSpace().rootPhysicalAddress(), source->addressSpace().rootPhysicalAddress(), sourceAddress,
                imageSize, bootRuntimeImages, configuredRuntimeCount, result)
            : sharedImageCount == 0
            ? ElfLoader::loadFromAddressSpace(
                  *task, source->addressSpace().rootPhysicalAddress(),
                  sourceAddress, imageSize, result)
            : ElfLoader::loadDynamicSetFromAddressSpace(
                  task->addressSpace().rootPhysicalAddress(),
                  source->addressSpace().rootPhysicalAddress(), sourceAddress,
                  imageSize, sharedImages, sharedImageCount, result);
    uint64_t stackPointer = 0;
    if (error == ElfLoadError::None) {
        error = UserProgramLoader::buildStack(
            task->addressSpace().rootPhysicalAddress(), name, argumentCount,
            arguments, 0, nullptr, 0, stackPointer, &result);
    }
    if (error != ElfLoadError::None ||
        Thread::createUser(task, result.entryPoint, stackPointer) == nullptr) {
        task->destroy();
        if (error == ElfLoadError::None) {
            error = ElfLoadError::OutOfMemory;
        }
        if (loadError != nullptr) {
            *loadError = error;
        }
        return nullptr;
    }
    return lifetime;
}

Task* UserProgramLoader::startInitial(const BootInfo* bootInfo) {
    if (bootInfo == nullptr || !initializeBootPrograms(*bootInfo)) {
        serial::serialWrite("[ELF] required boot service module missing\n");
        return nullptr;
    }
    ElfLoadError error;
    auto init = UserProgramLoader::load(os::ProgramId::Init, task::getIdle(),
                                         0, 0, nullptr, &error);
    if (init == nullptr) {
        serial::serialWrite("[ELF] unable to load init: ");
        serial::serialWrite(ElfLoader::errorName(error));
        serial::serialWrite("\n");
        return nullptr;
    }
    serial::serialWrite("[ELF] ELF64 init image validated and mapped\n");
    Task::setInitTask(init);
    serial::serialWrite(
        "[USERLAND] init loaded into an isolated address space\n");
    return init;
}

}  // namespace kernel
