/*
 * 准备启动程序身份、句柄、映像及用户参数栈。
 */

#include "kernel/boot.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "service/common/entry.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"

namespace task {

    constexpr uint64_t PAGE_SIZE                 = 4096;
    constexpr uint64_t USER_STACK_TOP            = 0x00007ffffff00000ULL;
    constexpr uint64_t USER_STACK_PAGES          = 64;
    constexpr uint32_t MAXIMUM_ARGUMENT_LENGTH   = 383;
    constexpr uint32_t MAXIMUM_ARGUMENT_COUNT    = 64;
    constexpr uint32_t MAXIMUM_ENVIRONMENT_COUNT = 64;
    constexpr uint64_t DIRECT_MAP_BASE           = 0xffff800000000000ULL;

    /* 内置程序配置及其引导模块来源，固定能力和服务角色由此确定。 */
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

    elf::ElfSharedImage bootRuntimeImages[configuredRuntimeCount]{};
    static bool bootProgramsReady;

    /*
     * 比较引导模块命令行的第一个名称字段，只允许完整名字后接终止符或空格，避免前缀相同的模块误匹配。
     */
    static bool moduleNameEquals(const char* commandLine,
                                 const char* expected) {
        if (commandLine == nullptr || expected == nullptr) {
            return false;
        }

        uint32_t index = 0;

        while (expected[index] != '\0' &&
               commandLine[index] == expected[index]) {
            ++index;
        }

        return expected[index] == '\0' &&
               (commandLine[index] == '\0' || commandLine[index] == ' ');
    }

    /*
     * 遍历引导模块，返回与指定程序名匹配的模块记录。
     */
    static const ::BootModule* findModule(const ::BootInfo& bootInfo,
                                          const char* name) {
        for (uint32_t index = 0; index < bootInfo.getModuleCount(); ++index) {
            const ::BootModule& module = bootInfo.getModules()[index];

            if (module.getEnd() > module.getStart() &&
                moduleNameEquals(module.getCommandLine(), name)) {
                return &module;
            }
        }

        return nullptr;
    }

    /*
     * 按配置查找必要启动程序和运行库模块，登记其地址与大小；缺失必要模块时拒绝启动用户程序。
     */
    static bool initializeBootPrograms(const ::BootInfo& bootInfo) {
        // 固定内置程序从引导模块取源数据，模块未找到时不发布准备完成状态。
        for (ProgramImage& image : PROGRAM_IMAGES) {
            if (!image.builtin) {
                continue;
            }

            const ::BootModule* module = findModule(bootInfo, image.moduleName);

            if (module == nullptr) {
                return false;
            }

            image.sourceAddress = DIRECT_MAP_BASE + module->getStart();
            image.size          = module->getEnd() - module->getStart();
        }

        // 运行库也以源地址、长度和源页表记录下来，之后加载服务时可以从内核直接映射读取。
        for (uint32_t i = 0; i < configuredRuntimeCount; ++i) {
            const ::BootModule* module =
                findModule(bootInfo, configuredRuntimeLibraries[i]);

            if (!module) {
                return false;
            }

            bootRuntimeImages[i] = {DIRECT_MAP_BASE + module->getStart(),
                                    module->getEnd() - module->getStart(),
                                    configuredRuntimeLibraries[i],
                                    mm::getKernelAddressSpaceRoot()};
        }

        bootProgramsReady = true;

        return true;
    }

    /*
     * 按指定对齐值向下取整，供用户栈按 ABI 要求排列数据。
     */
    static uint64_t alignDown(uint64_t value, uint64_t alignment) {
        return value & ~(alignment - 1);
    }

    /*
     * 计算普通字符串长度，空指针按空字符串处理。
     */
    static uint32_t stringLength(const char* value) {
        uint32_t length = 0;

        while (value != nullptr && value[length] != '\0' &&
               length <= MAXIMUM_ARGUMENT_LENGTH) {
            ++length;
        }

        return length;
    }

    /*
     * 由固定服务角色查询允许的能力位，不接受程序自己随意声明内核权限。
     */
    bool serviceCapabilities(os::ServiceRole role, uint64_t& capabilities) {
        for (const ProgramImage& image : PROGRAM_IMAGES) {
            if (image.role == role && !image.builtin) {
                capabilities = image.capabilities;

                return true;
            }
        }

        return false;
    }

    /*
     * 为新程序分配用户栈，倒序保存参数与环境字符串，再建立
     * argc、指针数组和辅助向量，最后返回对齐后的初始栈顶。
     */
    ::elf::ElfLoadError buildUserStack(
        uint64_t root, const char* programName, uint32_t argumentCount,
        const char* const* arguments, uint32_t environmentCount,
        const char* const* environment, uint64_t runtimeStateAddress,
        uint64_t& stackPointer, const ::elf::ElfLoadResult* image,
        Task* owner) {
        // 预先分配固定数量的用户栈页，可写但不可执行；若失败，由创建新任务的上层统一销毁地址空间。
        const uint64_t stackBottom =
            USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;

        for (uint64_t address = stackBottom; address < USER_STACK_TOP;
             address += PAGE_SIZE) {
            const ::mm::UserPageMapResult mapResult =
                mm::allocateUserPageInAddressSpace(
                    root, address,
                    ::mm::pageUser | ::mm::pageWritable | ::mm::pageNoExecute);

            if (mapResult == ::mm::UserPageMapResult::OutOfMemory) {
                return ::elf::ElfLoadError::OutOfMemory;
            }

            if (mapResult != ::mm::UserPageMapResult::Success) {
                return ::elf::ElfLoadError::MappingFailed;
            }
        }

        // 未给参数时用程序名作为
        // argv[0]，同时限制参数数量，防止越过局部数组容量。
        const char* defaultArguments[] = {programName};

        if (argumentCount == 0) {
            argumentCount = 1;
            arguments     = defaultArguments;
        }

        if (argumentCount > MAXIMUM_ARGUMENT_COUNT || arguments == nullptr) {
            return ::elf::ElfLoadError::InvalidArgument;
        }

        uint64_t cursor = USER_STACK_TOP;
        uint64_t argumentAddresses[MAXIMUM_ARGUMENT_COUNT]{};
        uint64_t environmentAddresses[MAXIMUM_ENVIRONMENT_COUNT]{};

        if (environmentCount > MAXIMUM_ENVIRONMENT_COUNT ||
            (environmentCount != 0 && environment == nullptr)) {
            return ::elf::ElfLoadError::InvalidArgument;
        }

        // 栈向低地址增长，字符串按逆序放入，并记录它们在目标用户空间中的地址。
        for (uint32_t index = environmentCount; index != 0; --index) {
            const uint32_t length = stringLength(environment[index - 1]);

            if (length > MAXIMUM_ARGUMENT_LENGTH ||
                cursor < stackBottom + length + 1) {
                return ::elf::ElfLoadError::InvalidArgument;
            }

            cursor -= length + 1;

            if (!mm::copyToAddressSpace(root, cursor, environment[index - 1],
                                        length + 1)) {
                return ::elf::ElfLoadError::MappingFailed;
            }

            environmentAddresses[index - 1] = cursor;
        }

        // 参数与环境变量使用相同的长度和剩余栈容量检查；复制包含末尾零，保证用户态得到完整字符串。
        for (uint32_t index = argumentCount; index != 0; --index) {
            const uint32_t argumentLength = stringLength(arguments[index - 1]);

            if (argumentLength > MAXIMUM_ARGUMENT_LENGTH ||
                cursor < stackBottom + argumentLength + 1) {
                return ::elf::ElfLoadError::InvalidArgument;
            }

            cursor -= argumentLength + 1;

            if (!mm::copyToAddressSpace(root, cursor, arguments[index - 1],
                                        argumentLength + 1)) {
                return ::elf::ElfLoadError::MappingFailed;
            }

            argumentAddresses[index - 1] = cursor;
        }

        // 单独保存可执行文件名，辅助向量的 EXECFN
        // 指向它，不依赖调用者是否自定义 argv[0]。
        const uint32_t executableNameLength = stringLength(programName);

        if (executableNameLength > MAXIMUM_ARGUMENT_LENGTH ||
            cursor < stackBottom + executableNameLength + 1) {
            return ::elf::ElfLoadError::InvalidArgument;
        }

        cursor -= executableNameLength + 1;
        const uint64_t executableNameAddress = cursor;

        if (!mm::copyToAddressSpace(root, cursor, programName,
                                    executableNameLength + 1)) {
            return ::elf::ElfLoadError::MappingFailed;
        }

        cursor = alignDown(cursor, 16);

        // 按十六字节对齐为启动字数组留位置；其中包括
        // argc、argv、envp、各自终止零和辅助向量。
        constexpr uint32_t auxiliaryWords = 24;
        const uint64_t kernelHandle =
            owner ? coreServices().bootstrap(*owner) : 0;

        if (owner && !kernelHandle) {
            return ::elf::ElfLoadError::OutOfMemory;
        }

        const uint64_t startupWordCount =
            argumentCount + environmentCount + 3 + auxiliaryWords;
        cursor = alignDown(cursor - startupWordCount * sizeof(uint64_t), 16);
        uint64_t startupWords[MAXIMUM_ARGUMENT_COUNT +
                              MAXIMUM_ENVIRONMENT_COUNT + 3 + auxiliaryWords]{};
        startupWords[0] = argumentCount;

        for (uint32_t index = 0; index < argumentCount; ++index) {
            startupWords[index + 1] = argumentAddresses[index];
        }

        // 启动数组预先清零，因此 argv 和 envp 后面的空槽自然成为终止指针。
        const uint32_t environmentStart = argumentCount + 2;

        for (uint32_t index = 0; index < environmentCount; ++index) {
            startupWords[environmentStart + index] =
                environmentAddresses[index];
        }

        // 辅助向量按类型和值成对排列，传递程序头、页大小、解释器与主入口，以及运行时和内核服务句柄。
        const uint64_t auxiliary[] = {
            3,
            image ? image->getProgramHeaders() : 0,
            4,
            56,
            5,
            image ? image->getProgramHeaderCount() : 0,
            6,
            PAGE_SIZE,
            7,
            image ? image->getInterpreterBase() : 0,
            9,
            image ? image->getApplicationEntry() : 0,
            31,
            executableNameAddress,
            0x60000001,
            runtimeStateAddress,
            0x60000002,
            image ? image->getApplicationBias() : 0,
            OS_AUX_KERNEL_HANDLE,
            kernelHandle,
            OS_AUX_REGISTRY_HANDLE,
            owner ? owner->bootstrapHandle() : 0,
            0,
            0};

        for (uint32_t index = 0; index < auxiliaryWords; ++index) {
            startupWords[environmentStart + environmentCount + 1 + index] =
                auxiliary[index];
        }

        // 全部布局完成后统一复制到用户栈，只有复制成功才返回可交给初始线程的栈指针。
        if (cursor < stackBottom ||
            !mm::copyToAddressSpace(root, cursor, startupWords,
                                    startupWordCount * sizeof(uint64_t))) {
            return ::elf::ElfLoadError::MappingFailed;
        }

        stackPointer = cursor;

        return ::elf::ElfLoadError::None;
    }

    /*
     * 从已登记引导模块启动内置服务，准备身份、引导句柄、动态映像和用户栈；任何准备失败都销毁未完成任务。
     */
    TaskReference loadProgram(os::ProgramId program, Task* parent,
                              uint64_t bootstrapHandle, uint32_t argumentCount,
                              const char* const* arguments,
                              ::elf::ElfLoadError* loadError) {
        if (loadError != nullptr) {
            *loadError = ::elf::ElfLoadError::None;
        }

        const uint64_t index = static_cast<uint64_t>(program);

        if (index >= static_cast<uint64_t>(os::ProgramId::Count) ||
            parent == nullptr || !bootProgramsReady) {
            if (loadError != nullptr) {
                *loadError = ::elf::ElfLoadError::InvalidArgument;
            }

            return nullptr;
        }

        // 固定程序编号只允许启动内置条目，能力和服务角色从内核配置取，不能由程序自行声明。
        const ProgramImage& image = PROGRAM_IMAGES[index];

        if (!image.builtin) {
            if (loadError) {
                *loadError = ::elf::ElfLoadError::InvalidArgument;
            }

            return nullptr;
        }

        Task* task = Task::create(image.name, parent);

        if (task == nullptr) {
            if (loadError != nullptr) {
                *loadError = ::elf::ElfLoadError::OutOfMemory;
            }

            return nullptr;
        }

        // 新任务创建后用强引用保护构造期；后续失败会显式销毁任务并随返回释放这份引用。
        TaskReference lifetime(task);
        task->grantCapabilities(image.capabilities);
        task->setServiceRole(image.role);
        task->setCommandLine(argumentCount, arguments);
        // 固定启动身份只授予接口能力；服务程序仍在用户态运行，不因此获得 Ring 0
        // 执行权。
        task->setSystemService(false);

        // 引导端点需要复制到子任务的句柄表，子任务得到的是自己的句柄编号，并只保留写权限。
        if (bootstrapHandle != 0) {
            const uint64_t childHandle =
                Endpoint::duplicateHandle(parent, task, bootstrapHandle);

            if (childHandle == 0) {
                // 准备失败时销毁尚未完成的子任务，由任务清理路径回收已建立的页和句柄。
                task->destroy();

                if (loadError != nullptr) {
                    *loadError = ::elf::ElfLoadError::InvalidArgument;
                }

                return nullptr;
            }

            task->setBootstrapHandle(childHandle);
            task->handles().restrictRights(childHandle, handleRightWrite);
        }

        // 先装载程序与运行库，再建立用户栈，两个步骤都成功后才创建可以调度的初始线程。
        ::elf::ElfLoadResult result{};
        ::elf::ElfLoadError error =
            ::elf::Loader(task->addressSpace().rootPhysicalAddress())
                .loadDynamicSetFromAddressSpace(
                    mm::getKernelAddressSpaceRoot(), image.sourceAddress,
                    image.size, bootRuntimeImages, configuredRuntimeCount,
                    result, task->isSystemService());
        uint64_t stackPointer = 0;

        if (error == ::elf::ElfLoadError::None) {
            error = buildUserStack(task->addressSpace().rootPhysicalAddress(),
                                   image.name, argumentCount, arguments, 0,
                                   nullptr, 0, stackPointer, &result, task);
        }

        if (error != ::elf::ElfLoadError::None ||
            Thread::createUser(task, result.getEntryPoint(), stackPointer, 0,
                               USER_STACK_TOP - stackPointer) == nullptr) {
            task->destroy();

            if (error == ::elf::ElfLoadError::None) {
                error = ::elf::ElfLoadError::OutOfMemory;
            }

            if (loadError != nullptr) {
                *loadError = error;
            }

            return nullptr;
        }

        return lifetime;
    }

    /*
     * 按调用方提供的映像启动任务，分别管理真实数据来源与逻辑父任务，复制标准句柄并设置会话后加载程序和初始线程。
     */
    TaskReference loadProgramImage(
        uint64_t sourceAddress, size_t imageSize, const char* name,
        Task* source, Task* parent, uint64_t bootstrapHandle,
        uint64_t capabilities, uint64_t standardInputHandle,
        uint64_t standardOutputHandle, uint64_t standardErrorHandle,
        uint32_t argumentCount, const char* const* arguments,
        const ::elf::ElfSharedImage* sharedImages, uint32_t sharedImageCount,
        ::elf::ElfLoadError* loadError, os::ServiceRole serviceRole,
        bool newSession, bool managedService, uint32_t terminalKind,
        uint32_t terminalIndex) {
        if (loadError != nullptr) {
            *loadError = ::elf::ElfLoadError::None;
        }

        if (sourceAddress == 0 || imageSize == 0 || name == nullptr ||
            source == nullptr || parent == nullptr) {
            if (loadError != nullptr) {
                *loadError = ::elf::ElfLoadError::InvalidArgument;
            }

            return nullptr;
        }

        // parent 决定进程关系，source
        // 提供映像和句柄；两者可能不是同一个任务，不能混用。
        Task* task = Task::create(name, parent);

        if (task == nullptr) {
            if (loadError != nullptr) {
                *loadError = ::elf::ElfLoadError::OutOfMemory;
            }

            return nullptr;
        }

        task->grantCapabilities(capabilities);
        TaskReference lifetime(task);
        task->setServiceRole(serviceRole);
        task->setManagedService(managedService ||
                                serviceRole != os::ServiceRole::None);
        task->setCommandLine(argumentCount, arguments);

        // 引导句柄从真实来源任务复制，失败时销毁新任务，不能留下没有启动通信通道的半成品。
        if (bootstrapHandle != 0) {
            const uint64_t childHandle =
                Endpoint::duplicateHandle(source, task, bootstrapHandle);

            if (childHandle == 0) {
                task->destroy();

                if (loadError != nullptr) {
                    *loadError = ::elf::ElfLoadError::InvalidArgument;
                }

                return nullptr;
            }

            task->setBootstrapHandle(childHandle);
            task->handles().restrictRights(childHandle, handleRightWrite);
        }

        // 标准输入、输出、错误分别检查并复制端点句柄；空值允许跳过，非端点不当作标准流继承。
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
                standardType == HandleType::Endpoint
                    ? Endpoint::duplicateHandle(source, task,
                                                standardHandles[index])
                    : 0;

            if (childHandle == 0) {
                task->destroy();

                if (loadError != nullptr) {
                    *loadError = ::elf::ElfLoadError::InvalidArgument;
                }

                return nullptr;
            }

            task->setStandardHandle(index, childHandle);
        }

        // 按会话或受管服务需求建立会话，再设置终端身份，后续用户代码看到的是完整的启动环境。
        if (hasPtyStandardHandle && !task->createSession()) {
            task->destroy();

            if (loadError != nullptr) {
                *loadError = ::elf::ElfLoadError::InvalidArgument;
            }

            return nullptr;
        }

        if (terminalKind) {
            task->setTerminal(terminalKind, terminalIndex);
        }

        // 服务使用配置的运行库；普通程序按是否提供共享映像选择静态或动态路径，源页表始终取
        // source。
        ::elf::ElfLoadResult result{};
        ::elf::ElfLoadError error =
            serviceRole != os::ServiceRole::None
                ? ::elf::Loader(task->addressSpace().rootPhysicalAddress())
                      .loadDynamicSetFromAddressSpace(
                          source->addressSpace().rootPhysicalAddress(),
                          sourceAddress, imageSize, bootRuntimeImages,
                          configuredRuntimeCount, result)
            : sharedImageCount == 0
                ? ::elf::Loader(*task).loadFromAddressSpace(
                      source->addressSpace().rootPhysicalAddress(),
                      sourceAddress, imageSize, result)
                : ::elf::Loader(task->addressSpace().rootPhysicalAddress())
                      .loadDynamicSetFromAddressSpace(
                          source->addressSpace().rootPhysicalAddress(),
                          sourceAddress, imageSize, sharedImages,
                          sharedImageCount, result);

        // ELF
        // 完成后构造参数栈并创建初始线程；任一阶段失败都走同一个新任务销毁责任。
        uint64_t stackPointer = 0;

        if (error == ::elf::ElfLoadError::None) {
            error = buildUserStack(task->addressSpace().rootPhysicalAddress(),
                                   name, argumentCount, arguments, 0, nullptr,
                                   0, stackPointer, &result, task);
        }

        if (error != ::elf::ElfLoadError::None ||
            Thread::createUser(task, result.getEntryPoint(), stackPointer) ==
                nullptr) {
            task->destroy();

            if (error == ::elf::ElfLoadError::None) {
                error = ::elf::ElfLoadError::OutOfMemory;
            }

            if (loadError != nullptr) {
                *loadError = error;
            }

            return nullptr;
        }

        return lifetime;
    }

    /*
     * 确认必需引导模块已准备，启动 Init
     * 并为孤儿收养保留它的任务引用，失败时输出加载原因。
     */
    Task* startInitialProgram(const BootInfo* bootInfo) {
        if (bootInfo == nullptr || !initializeBootPrograms(*bootInfo)) {
            device::serialWrite("[ELF] required boot service module missing\n");

            return nullptr;
        }

        // 以空闲任务为初始父任务启动
        // Init；具体加载错误会转成日志，启动成功后保留其任务引用用于孤儿收养。
        ::elf::ElfLoadError error;
        ::task::TaskReference init = loadProgram(
            os::ProgramId::Init, task::getIdle(), 0, 0, nullptr, &error);

        if (init == nullptr) {
            device::serialWrite("[ELF] unable to load init: ");
            device::serialWrite(::elf::elfErrorName(error));
            device::serialWrite("\n");

            return nullptr;
        }

        device::serialWrite("[ELF] ELF64 init image validated and mapped\n");
        Task::setInitTask(init);
        device::serialWrite(
            "[USERLAND] init loaded into an isolated address space\n");

        return init;
    }

}  // namespace task
