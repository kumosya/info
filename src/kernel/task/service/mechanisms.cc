/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/service/mechanisms.cc
 * 为创建线程、fork 和 exec 提供准备、提交与失败清理机制。
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/protocol.h"
#include "service/common/records.h"
#include <os/device.h>

namespace task {

    constexpr uint64_t pageSize = 4096;

    /*
     * 把任务机制中的错误码转换成系统调用返回表示。
     */
    static uint64_t error(SyscallError value) {
        return static_cast<uint64_t>(static_cast<int64_t>(value));
    }

    /*
     * 把 ELF
     * 加载失败原因对应到用户接口错误，使调用方能区分格式、权限和内存问题。
     */
    static uint64_t elfLoadError(::elf::ElfLoadError loadError) {
        switch (loadError) {
            case ::elf::ElfLoadError::OutOfMemory:
                return error(SyscallError::OutOfMemory);
            case ::elf::ElfLoadError::UnsupportedClass:
            case ::elf::ElfLoadError::UnsupportedEndian:
            case ::elf::ElfLoadError::UnsupportedVersion:
            case ::elf::ElfLoadError::UnsupportedMachine:
            case ::elf::ElfLoadError::UnsupportedType:
            case ::elf::ElfLoadError::UnsupportedProgramHeader:
                return error(SyscallError::NotSupported);
            default:
                return error(SyscallError::InvalidArgument);
        }
    }

    /*
     * 保存本次操作的调用任务，后续创建线程、fork 和 exec
     * 都以这个任务作为权限与归属依据。
     */
    TaskMechanisms::TaskMechanisms(Task& caller) : caller_(caller) {}

    /*
     * 验证用户入口和栈地址，创建属于当前任务的用户线程，并返回线程编号；失败时不发布可运行线程。
     */
    uint64_t TaskMechanisms::createThread(uint64_t entryPoint,
                                          uint64_t stackPointer,
                                          uint64_t argument) {
        Task* task = &caller_;

        if (task != task::getCurrent() ||
            !mm::pagerBridge().ensureUserMemory(entryPoint, 1, false) ||
            stackPointer < 8 ||
            !mm::pagerBridge().ensureUserMemory(stackPointer - 8, 8, true)) {
            return error(SyscallError::InvalidArgument);
        }

        Thread* thread = Thread::createUser(task, entryPoint, stackPointer,
                                            argument, 0, ~0ULL, false);

        if (!thread) {
            return error(SyscallError::OutOfMemory);
        }

        const uint64_t id = thread->getId();
        task::enqueueThread(thread);

        return id;
    }

    /*
     * 从当前系统调用现场创建子任务。
     * 复制地址空间和管理记录，继承句柄及身份，再准备返回值为零的子线程；任一步失败则清理子任务。
     */
    uint64_t TaskMechanisms::fork() {
        SyscallContext* context = currentThread()->syscallContext();

        Task* parent    = &caller_;
        Thread* current = currentThread();

        if (context == nullptr || parent == nullptr || current == nullptr ||
            parent == task::getIdle()) {
            return error(SyscallError::InvalidArgument);
        }

        // 硬件映射不能通过复制页表逃离其原任务归属。
        if (device::resourceDomains().owned(*parent)) {
            return error(SyscallError::PermissionDenied);
        }

        // 克隆地址空间前先把子进程看到的回复设为零；克隆后再写会污染写时复制关系，或让子进程看到父进程返回值。
        IpcMessage childReply{};
        childReply.length = 1;

        if (!task::copyToUser(reinterpret_cast<void*>(context->rdx),
                              &childReply, sizeof(childReply))) {
            return error(SyscallError::BadAddress);
        }

        // 先创建尚未运行的子任务，地址空间、句柄和线程现场都完成后才允许进入调度队列。
        Task* child = Task::create(parent->name(), parent);

        if (child == nullptr) {
            return error(SyscallError::OutOfMemory);
        }

        bool cloned = false;
        {
            PreemptionSpinLockGuard addressGuard(parent->addressSpace().lock());
            cloned = mm::cloneAddressSpaceRoot(
                         parent->addressSpace().rootPhysicalAddress(),
                         child->addressSpace().rootPhysicalAddress()) &&
                     ::mm::sharedMemory().cloneTaskMappings(*parent, *child) &&
                     mm::maps().cloneTask(*parent, *child);
        }

        // 复制任何一层记录失败，都交给统一子任务回收路径清理，不能把半初始化孩子留在全局任务表中。
        const bool inherited = cloned && child->inheritForFork(*parent);
        const bool pagerCloned = inherited && mm::pagerBridge().clone(*parent, *child);
        if (!pagerCloned) {
            device::serialWrite("[FORK-FAIL] maps=");
            device::serialWriteDecimal(cloned);
            device::serialWrite(" inherit=");
            device::serialWriteDecimal(inherited);
            device::serialWrite(" pager=");
            device::serialWriteDecimal(pagerCloned);
            device::serialWrite(" free_pages=");
            device::serialWriteDecimal(mm::getFreeMemoryBytes() / 4096);
            device::serialWrite("\n");
            child->destroy();

            return error(SyscallError::OutOfMemory);
        }

        // 子进程运行前清理继承标志；创建路径把 RAX
        // 设为零，并通过完整寄存器返回入口恢复执行。
        SyscallContext childContext     = *context;
        constexpr uint64_t allowedFlags = 0x0000000000240dd5ULL;
        childContext.userRflags =
            (childContext.userRflags & allowedFlags) | 0x202;

        if (Thread::createForkedUser(child, childContext,
                                     current->getSignalMask()) == nullptr) {
            child->destroy();

            return error(SyscallError::OutOfMemory);
        }

        return child->id();
    }

    /*
     * 准备并提交 exec。
     * 先安全读取新映像、参数和环境，构造新页表及用户栈，全部就绪后才交给
     * commitExec 替换旧程序。
     */
    bool TaskMechanisms::replace(uint64_t image, uint64_t& failureResult) {
        SyscallContext* context = currentThread()->syscallContext();

        constexpr uint32_t maximumArgumentCount    = 64;
        constexpr uint32_t maximumEnvironmentCount = 64;
        constexpr uint32_t maximumSharedImageCount = 4;
        constexpr uint32_t maximumStringLength     = 383;
        constexpr uint32_t maximumNameLength       = 95;
        constexpr uint64_t maximumImageSize        = OS_TASK_IMAGE_MAX_BYTES;
        constexpr uint64_t maximumRuntimeStateSize = 16 * 1024;
        Task* task                                 = &caller_;
        Thread* current                            = currentThread();
        services::wire::ExecImageInfo info{};

        if (context == nullptr || task == nullptr || current == nullptr ||
            !task::copyFromUser(&info, reinterpret_cast<const void*>(image),
                                sizeof(info)) ||
            info.image == nullptr || info.imageSize == 0 ||
            info.imageSize > maximumImageSize || info.name == nullptr ||
            info.argumentCount > maximumArgumentCount ||
            info.environmentCount > maximumEnvironmentCount ||
            info.sharedImageCount > maximumSharedImageCount ||
            (info.argumentCount != 0 && info.arguments == nullptr) ||
            (info.environmentCount != 0 && info.environment == nullptr) ||
            (info.sharedImageCount != 0 && info.sharedImages == nullptr) ||
            info.runtimeStateSize > maximumRuntimeStateSize ||
            (info.runtimeStateSize != 0 && info.runtimeState == nullptr)) {
            failureResult = error(SyscallError::InvalidArgument);

            return false;
        }

        const uint64_t oldRoot = task->addressSpace().rootPhysicalAddress();

        if (!mm::validateUserMemory(oldRoot,
                                    reinterpret_cast<uint64_t>(info.image),
                                    info.imageSize, false) ||
            (info.runtimeStateSize != 0 &&
             !mm::validateUserMemory(
                 oldRoot, reinterpret_cast<uint64_t>(info.runtimeState),
                 info.runtimeStateSize, false))) {
            failureResult = error(SyscallError::InvalidArgument);

            return false;
        }

        // 先复制共享映像描述再读取其中的用户指针；每个映像分别限制大小，描述数组有效不代表其中任意地址都有效。
        services::wire::SharedImageInfo userSharedImages[maximumSharedImageCount]{};
        ::elf::ElfSharedImage sharedImages[maximumSharedImageCount]{};

        if (info.sharedImageCount != 0 &&
            !task::copyFromUser(
                userSharedImages, info.sharedImages,
                info.sharedImageCount * sizeof(services::wire::SharedImageInfo))) {
            failureResult = error(SyscallError::InvalidArgument);

            return false;
        }

        for (uint32_t index = 0; index < info.sharedImageCount; ++index) {
            if (userSharedImages[index].image == nullptr ||
                userSharedImages[index].imageSize == 0 ||
                userSharedImages[index].imageSize > maximumImageSize ||
                !mm::validateUserMemory(
                    oldRoot,
                    reinterpret_cast<uint64_t>(userSharedImages[index].image),
                    userSharedImages[index].imageSize, false)) {
                failureResult = error(SyscallError::InvalidArgument);

                return false;
            }

            sharedImages[index] = {
                reinterpret_cast<uint64_t>(userSharedImages[index].image),
                userSharedImages[index].imageSize, nullptr};
        }

        char name[maximumNameLength + 1]{};
        bool nameTerminated = false;

        for (uint32_t byte = 0; byte <= maximumNameLength; ++byte) {
            if (!task::copyFromUser(&name[byte], info.name + byte, 1)) {
                failureResult = error(SyscallError::InvalidArgument);

                return false;
            }

            if (name[byte] == '\0') {
                nameTerminated = true;
                break;
            }
        }

        if (!nameTerminated || name[0] == '\0') {
            failureResult = error(SyscallError::InvalidArgument);

            return false;
        }

        uint64_t argumentPointers[maximumArgumentCount]{};
        uint64_t environmentPointers[maximumEnvironmentCount]{};

        // 最大参数与环境字符串放在堆上，避免压垮内核栈；ThreadCleanup
        // 负责普通失败和线程强制退出时的归还。
        class StringStorage {
            public:
                /*
                 * 为参数和环境字符串申请临时内核存储，并登记强制退出时的释放动作，避免读取用户数据途中终止造成泄漏。
                 */
                explicit StringStorage(size_t size) {
                    // 大块申请期间允许中断，但清理登记与线程移除之间不能出现未登记的资源窗口。
                    cpu::disablePreemption();
                    data_ = mm::allocateHeap(size);

                    if (data_) {
                        cleanup_.attach(data_,
                                        [](void* data) { mm::freeHeap(data); });
                    }

                    cpu::enablePreemption();
                }

                /*
                 * 返回临时字符串存储地址，供参数读取和用户栈构建使用。
                 */
                void* data() const { return data_; }

            private:
                ThreadCleanup cleanup_;
                void* data_ = nullptr;
        } strings((maximumArgumentCount + maximumEnvironmentCount) *
                  (maximumStringLength + 1));

        if (!strings.data()) {
            failureResult = error(SyscallError::OutOfMemory);

            return false;
        }

        char(*argumentStorage)[384] =
            static_cast<char(*)[maximumStringLength + 1]>(strings.data());
        char(*environmentStorage)[384] = argumentStorage + maximumArgumentCount;
        const char* arguments[maximumArgumentCount]{};
        const char* environment[maximumEnvironmentCount]{};

        if ((info.argumentCount != 0 &&
             !task::copyFromUser(argumentPointers, info.arguments,
                                 info.argumentCount * sizeof(uint64_t))) ||
            (info.environmentCount != 0 &&
             !task::copyFromUser(environmentPointers, info.environment,
                                 info.environmentCount * sizeof(uint64_t)))) {
            failureResult = error(SyscallError::InvalidArgument);

            return false;
        }

        for (uint32_t index = 0; index < info.argumentCount; ++index) {
            bool terminated = false;

            for (uint32_t byte = 0; byte <= maximumStringLength; ++byte) {
                if (!task::copyFromUser(&argumentStorage[index][byte],
                                        reinterpret_cast<const void*>(
                                            argumentPointers[index] + byte),
                                        1)) {
                    failureResult = error(SyscallError::InvalidArgument);

                    return false;
                }

                if (argumentStorage[index][byte] == '\0') {
                    terminated = true;
                    break;
                }
            }

            if (!terminated) {
                failureResult = error(SyscallError::InvalidArgument);

                return false;
            }

            arguments[index] = argumentStorage[index];
        }

        for (uint32_t index = 0; index < info.environmentCount; ++index) {
            bool terminated = false;

            for (uint32_t byte = 0; byte <= maximumStringLength; ++byte) {
                if (!task::copyFromUser(&environmentStorage[index][byte],
                                        reinterpret_cast<const void*>(
                                            environmentPointers[index] + byte),
                                        1)) {
                    failureResult = error(SyscallError::InvalidArgument);

                    return false;
                }

                if (environmentStorage[index][byte] == '\0') {
                    terminated = true;
                    break;
                }
            }

            if (!terminated) {
                failureResult = error(SyscallError::InvalidArgument);

                return false;
            }

            environment[index] = environmentStorage[index];
        }

        // 从这里到提交成功前，新根归本函数所有，失败必须销毁；旧进程仍在旧根上运行，因此可以正常返回失败而保留原程序。

        // 新程序先在独立页表根上构建，准备失败时仍可向旧程序正常报告错误。
        const uint64_t newRoot = mm::createAddressSpaceRoot();

        if (newRoot) {
            mm::assignPhysicalPageOwner(newRoot, task->id());
        }

        if (newRoot == 0) {
            failureResult = error(SyscallError::OutOfMemory);

            return false;
        }

        ::elf::ElfLoadResult loadResult{};
        ::elf::ElfLoadError loadError =
            info.sharedImageCount == 0
                ? ::elf::Loader(newRoot).loadFromAddressSpace(
                      oldRoot, reinterpret_cast<uint64_t>(info.image),
                      info.imageSize, loadResult)
                : ::elf::Loader(newRoot).loadDynamicSetFromAddressSpace(
                      oldRoot, reinterpret_cast<uint64_t>(info.image),
                      info.imageSize, sharedImages, info.sharedImageCount,
                      loadResult);
        // 运行数据放入独立只读且不可执行的区域；遇到与已准备 ELF
        // 段冲突时拒绝，不能覆盖程序映射。
        uint64_t runtimeStateAddress = 0;

        if (loadError == ::elf::ElfLoadError::None &&
            info.runtimeStateSize != 0) {
            runtimeStateAddress = services::execRuntimeStateAddress;
            const uint64_t mappedSize =
                (info.runtimeStateSize + pageSize - 1) & ~(pageSize - 1);

            for (uint64_t offset = 0; offset < mappedSize; offset += pageSize) {
                if (mm::getPhysicalAddressInAddressSpace(
                        newRoot, runtimeStateAddress + offset) != 0) {
                    loadError = ::elf::ElfLoadError::MappingFailed;
                    break;
                }

                const ::mm::UserPageMapResult mapped =
                    mm::allocateUserPageInAddressSpace(
                        newRoot, runtimeStateAddress + offset,
                        ::mm::pageUser | ::mm::pageWritable |
                            ::mm::pageNoExecute);

                if (mapped != ::mm::UserPageMapResult::Success) {
                    loadError = mapped == ::mm::UserPageMapResult::OutOfMemory
                                    ? ::elf::ElfLoadError::OutOfMemory
                                    : ::elf::ElfLoadError::MappingFailed;
                    break;
                }
            }

            uint8_t buffer[pageSize];
            uint64_t copied = 0;

            while (loadError == ::elf::ElfLoadError::None &&
                   copied < info.runtimeStateSize) {
                const uint64_t remaining = info.runtimeStateSize - copied;
                const uint64_t chunk =
                    remaining < pageSize ? remaining : pageSize;

                if (!mm::copyFromAddressSpace(
                        buffer, oldRoot,
                        reinterpret_cast<uint64_t>(info.runtimeState) + copied,
                        chunk) ||
                    !mm::copyToAddressSpace(
                        newRoot, runtimeStateAddress + copied, buffer, chunk)) {
                    loadError = ::elf::ElfLoadError::MappingFailed;
                    break;
                }

                copied += chunk;
            }

            if (loadError == ::elf::ElfLoadError::None) {
                for (uint64_t offset = 0; offset < mappedSize;
                     offset += pageSize) {
                    mm::protectPageInAddressSpace(
                        newRoot, runtimeStateAddress + offset,
                        ::mm::pageUser | ::mm::pageNoExecute);
                }
            }
        }

        // 新旧地址空间都仍可访问时，建立新用户栈中的参数、环境和辅助句柄信息。
        uint64_t stackPointer = 0;

        if (loadError == ::elf::ElfLoadError::None) {
            loadError = buildUserStack(newRoot, name, info.argumentCount,
                                       arguments, info.environmentCount,
                                       environment, runtimeStateAddress,
                                       stackPointer, &loadResult, task);
        }

        if (loadError != ::elf::ElfLoadError::None) {
            device::serialWrite("[EXEC-FAIL] loader=");
            device::serialWriteDecimal(static_cast<uint32_t>(loadError));
            device::serialWrite("\n");
            mm::destroyAddressSpaceRoot(newRoot);
            failureResult = elfLoadError(loadError);

            return false;
        }

        // 提交被拒绝时新根仍归本函数，应销毁；提交成功后不能再读取旧用户地址。

        // 这一步是新旧映像的所有权交接点；提交失败销毁新根，成功后旧用户指针不能再读写。
        if (!task->commitExec(newRoot, name, current)) {
            mm::destroyAddressSpaceRoot(newRoot);
            failureResult = error(SyscallError::Busy);

            return false;
        }

        task->setCommandLine(info.argumentCount, arguments);

        // 只安装新入口、栈和初始标志，重置浮点状态并标记现场已恢复；系统调用收尾必须跳过旧回复写入和普通返回值覆盖。
        *context            = {};
        context->userRip    = loadResult.getEntryPoint();
        context->userRsp    = stackPointer;
        context->userRflags = 0x202;
        current->setUserStack(reinterpret_cast<void*>(stackPointer));
        const uint64_t interruptFlags = cpu::disableInterrupts();
        current->resetFloatingPointState();
        current->restoreFloatingPointState();
        cpu::restoreInterrupts(interruptFlags);
        current->markContextRestored();

        return true;
    }

}  // namespace task
