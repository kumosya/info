/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/service/service.cc
 * 校验并分发任务、身份、信号、等待及程序映像请求。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include "service/users/identity.h"
#include <os/device.h>

namespace task {

    // 源数据必须从真实调用者地址空间读取；逻辑父任务或新建子任务可能使用不同页表，不能用它们解释调用者指针。
    class TaskImages {
            Task& caller_;

            /*
             * 将任务服务错误转换为接口约定的返回值。
             */
            static uint64_t error(SyscallError value) {
                return static_cast<uint64_t>(static_cast<int64_t>(value));
            }

            /*
             * 通过受保护的用户内存拷贝读取数据。
             * 先检查用户范围，遇到无效页面时返回失败，避免内核直接解引用用户指针。
             */
            bool copyFromUser(void* destination, const void* source,
                              size_t size) {
                const uint64_t address = reinterpret_cast<uint64_t>(source);

                if (!destination || !size || address < 0x10000 ||
                    address + size < address ||
                    (address + size > 0x0000800000000000ULL &&
                     !(caller_.isSystemService() &&
                       address >= 0xffff800000000000ULL))) {
                    return false;
                }

                PreemptionSpinLockGuard guard(caller_.addressSpace().lock());
                const uint64_t root =
                    caller_.addressSpace().rootPhysicalAddress();

                if (!mm::validateUserMemory(root, address, size, false)) {
                    return false;
                }

                if (address >= 0xffff800000000000ULL &&
                    caller_.isSystemService()) {
                    copyMessageData(destination, source, size);

                    return true;
                }

                return mm::copyFromAddressSpace(destination, root, address,
                                                size);
            }

            /*
             * 将加载器错误转换成任务接口能表达的失败原因。
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

        public:
            /*
             * 保存创建程序的调用任务，作为新任务的父任务和权限检查来源。
             */
            explicit TaskImages(Task& caller) : caller_(caller) {}

            /*
             * 按内置程序编号启动任务。
             * 验证参数数量并复制用户字符串，再调用加载器建立任务、地址空间和初始线程。
             */
            uint64_t spawnProgram(uint64_t program, uint64_t bootstrapHandle,
                                  uint64_t argumentCount,
                                  uint64_t userArguments) {
                constexpr uint32_t maximumArgumentCount  = 64;
                constexpr uint32_t maximumArgumentLength = 383;
                Task* parent                             = &caller_;
                const os::ProgramId requested =
                    static_cast<os::ProgramId>(program);
                // Init 可以启动引导程序；Task 和 Device
                // 只允许启动各自列出的下属服务，不能任意启动所有内置映像。
                const bool permitted =
                    parent &&
                    (parent->serviceRole() == os::ServiceRole::Init ||
                     (parent->serviceRole() == os::ServiceRole::Task &&
                      (requested == os::ProgramId::PipeServer ||
                       requested == os::ProgramId::MessageQueueServer)) ||
                     (parent->serviceRole() == os::ServiceRole::Device &&
                      (requested == os::ProgramId::PciServer ||
                       requested == os::ProgramId::StorageServer)));

                if (!permitted) {
                    return error(SyscallError::PermissionDenied);
                }

                if (program >= static_cast<uint64_t>(os::ProgramId::Count)) {
                    return error(SyscallError::InvalidArgument);
                }

                if (argumentCount > maximumArgumentCount ||
                    (argumentCount != 0 && userArguments == 0)) {
                    return error(SyscallError::InvalidArgument);
                }

                // 先把参数指针数组拷入内核，再逐条读取字符串；有效数组不代表其中每个用户指针都可访问。
                uint64_t userPointers[maximumArgumentCount]{};
                char argumentStorage[maximumArgumentCount]
                                    [maximumArgumentLength + 1]{};
                const char* arguments[maximumArgumentCount]{};

                if (argumentCount != 0 &&
                    !copyFromUser(userPointers,
                                  reinterpret_cast<const void*>(userArguments),
                                  argumentCount * sizeof(uint64_t))) {
                    return error(SyscallError::InvalidArgument);
                }

                for (uint32_t index = 0; index < argumentCount; ++index) {
                    bool terminated = false;

                    for (uint32_t byte = 0; byte <= maximumArgumentLength;
                         ++byte) {
                        if (!copyFromUser(&argumentStorage[index][byte],
                                          reinterpret_cast<const void*>(
                                              userPointers[index] + byte),
                                          1)) {
                            return error(SyscallError::InvalidArgument);
                        }

                        if (argumentStorage[index][byte] == '\0') {
                            terminated = true;
                            break;
                        }
                    }

                    // 每个参数必须在单条长度上限内出现空字符，防止从用户内存无界扫描。
                    if (!terminated) {
                        return error(SyscallError::InvalidArgument);
                    }

                    arguments[index] = argumentStorage[index];
                }

                ::elf::ElfLoadError loadError = ::elf::ElfLoadError::None;

                // 参数全部准备完再交给统一加载器，返回带引用的任务对象保护后续句柄安装。
                ::task::TaskReference child = loadProgram(
                    static_cast<os::ProgramId>(program), parent,
                    bootstrapHandle, static_cast<uint32_t>(argumentCount),
                    arguments, &loadError);

                if (child == nullptr) {
                    device::serialWrite("[ELF] dynamic image load failed: ");
                    device::serialWrite(::elf::elfErrorName(loadError));
                    device::serialWrite("\n");

                    return elfLoadError(loadError);
                }

                // 给调用方安装任务句柄，PID 和句柄是不同编号，不能把 PID
                // 直接当管理权限使用。
                const uint64_t handle =
                    parent->handles().add(child, HandleType::Task,
                                          handleRightRead | handleRightManage);

                return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
            }

            /*
             * 按用户提供的映像描述启动任务。
             * 先读取并验证文件映像、参数和共享库描述，再传给统一加载流程；失败时释放暂存资源。
             */
            uint64_t spawnImage(uint64_t userInfo) {
                constexpr uint32_t maximumArgumentCount    = 64;
                constexpr uint32_t maximumArgumentLength   = 383;
                constexpr uint32_t maximumNameLength       = 63;
                constexpr uint32_t maximumSharedImageCount = 4;
                constexpr uint64_t maximumImageSize        = 8ULL * 1024 * 1024;
                Task* parent                               = &caller_;
                os::wire::SpawnImageInfo info{};

                if (parent == nullptr ||
                    !parent->hasCapability(taskCapabilitySpawn) ||
                    !copyFromUser(&info,
                                  reinterpret_cast<const void*>(userInfo),
                                  sizeof(info)) ||
                    info.image == nullptr || info.imageSize == 0 ||
                    info.imageSize > maximumImageSize || info.name == nullptr ||
                    info.argumentCount > maximumArgumentCount ||
                    info.sharedImageCount > maximumSharedImageCount ||
                    (info.argumentCount != 0 && info.arguments == nullptr) ||
                    (info.sharedImageCount != 0 &&
                     info.sharedImages == nullptr) ||
                    info.capabilities != 0 || info.terminalKind > 2 ||
                    (info.flags & ~(os::spawnFlagNewSession |
                                    os::spawnFlagManagedService)) ||
                    !mm::validateUserMemory(
                        parent->addressSpace().rootPhysicalAddress(),
                        reinterpret_cast<uint64_t>(info.image), info.imageSize,
                        false)) {
                    return error(SyscallError::InvalidArgument);
                }

                // 先复制共享库描述数组，再分别验证每个映像的地址与大小，避免跟随不受检查的嵌套指针。
                os::wire::SharedImageInfo
                    userSharedImages[maximumSharedImageCount]{};
                ::elf::ElfSharedImage sharedImages[maximumSharedImageCount]{};

                if (info.sharedImageCount != 0 &&
                    !copyFromUser(userSharedImages, info.sharedImages,
                                  info.sharedImageCount *
                                      sizeof(os::wire::SharedImageInfo))) {
                    return error(SyscallError::InvalidArgument);
                }

                for (uint32_t index = 0; index < info.sharedImageCount;
                     ++index) {
                    if (userSharedImages[index].image == nullptr ||
                        userSharedImages[index].imageSize == 0 ||
                        userSharedImages[index].imageSize > maximumImageSize ||
                        !mm::validateUserMemory(
                            parent->addressSpace().rootPhysicalAddress(),
                            reinterpret_cast<uint64_t>(
                                userSharedImages[index].image),
                            userSharedImages[index].imageSize, false)) {
                        return error(SyscallError::InvalidArgument);
                    }

                    sharedImages[index] = {reinterpret_cast<uint64_t>(
                                               userSharedImages[index].image),
                                           userSharedImages[index].imageSize,
                                           nullptr};
                }

                // 加载期间保留逻辑父任务引用，但数据和句柄仍来自
                // caller_；父子关系本身不授予受管理服务角色。
                ::task::TaskReference logicalParentReference = task::find(
                    info.parentTaskId ? info.parentTaskId : parent->id());
                Task* logicalParent = logicalParentReference;

                if (info.parentTaskId != 0) {
                    if (logicalParent == nullptr ||
                        logicalParent == task::getIdle() ||
                        logicalParent->isTerminated()) {
                        return error(SyscallError::InvalidArgument);
                    }
                }

                // 服务能力由允许的角色推导，普通创建请求不能自行填写能力位取得特权。
                uint64_t serviceCapabilities = 0;
                const bool managed = info.flags & os::spawnFlagManagedService;
                // 只有 Task 代表 Init
                // 才能创建受监护实例，避免普通程序自行申请恢复或身份管理特权。
                if ((managed || info.serviceRole) &&
                    (parent->serviceRole() != os::ServiceRole::Task ||
                     !logicalParent ||
                     logicalParent->serviceRole() != os::ServiceRole::Init ||
                     !managed)) {
                    return error(SyscallError::PermissionDenied);
                }

                if (info.serviceRole &&
                    !task::serviceCapabilities(
                        static_cast<os::ServiceRole>(info.serviceRole),
                        serviceCapabilities)) {
                    return error(SyscallError::PermissionDenied);
                }

                // 程序名同样来自用户空间，要按固定长度读取并确认终止，之后只使用内核副本。
                char name[maximumNameLength + 1]{};
                bool nameTerminated = false;

                for (uint32_t byte = 0; byte <= maximumNameLength; ++byte) {
                    if (!copyFromUser(&name[byte], info.name + byte, 1)) {
                        return error(SyscallError::InvalidArgument);
                    }

                    if (name[byte] == '\0') {
                        nameTerminated = true;
                        break;
                    }
                }

                if (!nameTerminated || name[0] == '\0') {
                    return error(SyscallError::InvalidArgument);
                }

                uint64_t userPointers[maximumArgumentCount]{};
                char argumentStorage[maximumArgumentCount]
                                    [maximumArgumentLength + 1]{};
                const char* arguments[maximumArgumentCount]{};

                if (info.argumentCount != 0 &&
                    !copyFromUser(userPointers, info.arguments,
                                  info.argumentCount * sizeof(uint64_t))) {
                    return error(SyscallError::InvalidArgument);
                }

                for (uint32_t index = 0; index < info.argumentCount; ++index) {
                    bool terminated = false;

                    for (uint32_t byte = 0; byte <= maximumArgumentLength;
                         ++byte) {
                        if (!copyFromUser(&argumentStorage[index][byte],
                                          reinterpret_cast<const void*>(
                                              userPointers[index] + byte),
                                          1)) {
                            return error(SyscallError::InvalidArgument);
                        }

                        if (argumentStorage[index][byte] == '\0') {
                            terminated = true;
                            break;
                        }
                    }

                    if (!terminated) {
                        return error(SyscallError::InvalidArgument);
                    }

                    arguments[index] = argumentStorage[index];
                }

                ::elf::ElfLoadError loadError = ::elf::ElfLoadError::None;
                ::task::TaskReference child   = loadProgramImage(
                    reinterpret_cast<uint64_t>(info.image), info.imageSize,
                    name, parent, logicalParent, info.bootstrapHandle,
                    serviceCapabilities, info.standardInputHandle,
                    info.standardOutputHandle, info.standardErrorHandle,
                    info.argumentCount, arguments, sharedImages,
                    info.sharedImageCount, &loadError,
                    static_cast<os::ServiceRole>(info.serviceRole),
                    (info.flags & os::spawnFlagNewSession) != 0, managed,
                    info.terminalKind, info.terminalIndex);

                if (child == nullptr) {
                    device::serialWrite("[ELF] spawn image failed: ");
                    device::serialWrite(::elf::elfErrorName(loadError));
                    device::serialWrite("\n");

                    return elfLoadError(loadError);
                }

                // 映像加载成功后才安装并公布任务句柄，期间任务对象由局部引用保持有效。
                const uint64_t handle =
                    parent->handles().add(child, HandleType::Task,
                                          handleRightRead | handleRightManage);

                return handle == 0 ? error(SyscallError::OutOfMemory) : handle;
            }
    };

    namespace TaskService {

        /*
         * 把公共错误枚举换成任务服务的负错误码。
         */
        static int failure(os::ErrorCode code) {
            return static_cast<int>(code);
        }

        /*
         * 把任务基本信息编码到回复，包括身份、父任务、线程和终端信息；按调用者身份控制可见内容。
         */
        static void describe(Task& target, Task& caller, IpcMessage& reply) {
            os::wire::TaskInfo info{};
            os::wire::CredentialInfo credentials{};
            target.credentials(credentials);
            info.id           = target.id();
            info.state        = static_cast<uint32_t>(target.state());
            info.exitCode     = target.exitCode();
            info.uid          = credentials.uid;
            info.gid          = credentials.gid;
            info.effectiveUid = credentials.effectiveUid;
            info.effectiveGid = credentials.effectiveGid;
            info.parentId     = target.parentId();
            info.serviceRole  = static_cast<uint32_t>(target.serviceRole());
            info.taskGroupId  = target.taskGroupId();
            info.sessionId    = target.sessionId();
            info.terminalForegroundGroup = task::terminalForeground(
                target.terminalKind(), target.terminalIndex());
            info.startedNanoseconds = target.startedNanoseconds();
            info.cpuNanoseconds     = target.cpuNanoseconds();
            info.terminalKind       = target.terminalKind();
            info.terminalIndex      = target.terminalIndex();
            info.commandLineSize =
                caller.effectiveUid() == 0 ||
                        caller.effectiveUid() == target.effectiveUid()
                    ? target.commandLineSize()
                    : 0;
            info.managedService = target.managedService();
            info.mainThreadId   = target.mainThreadId();
            info.nice           = target.mainNice();
            info.threadCount    = target.threadCount();

            for (size_t i = 0; i < info.commandLineSize; ++i) {
                info.commandLine[i] = target.commandLine()[i];
            }

            info.trustedDeviceService =
                target.hasCapability(taskCapabilityDevice) &&
                (target.serviceRole() == os::ServiceRole::Device ||
                 target.serviceRole() == os::ServiceRole::Storage);

            for (size_t i = 0; i + 1 < sizeof(info.name) && target.name()[i];
                 ++i) {
                info.name[i] = target.name()[i];
            }

            copyMessageData(&reply.data[1], &info, sizeof(info));
        }

        /*
         * 分发任务内核服务操作。
         * 查询、身份修改、信号、等待、资源限制和映像操作分别校验请求，再调用各自的任务机制。
         */
        int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
            // 该端点直接在经过认证的调用线程内执行，不通过伪装当前 CPU
            // 任务来冒充发送者。
            Thread* thread = currentThread();

            if (!thread || thread->getTask() != &caller || request.length < 4) {
                return failure(os::ErrorCode::InvalidArgument);
            }

            const uint64_t* args = request.data;
            // 恢复操作还需验证拥有者、实例代号和提交序号；操作编号正确不能证明服务身份合法。
            if (request.type >=
                    static_cast<uint64_t>(os::TaskOperation::RecoveryAcquire) &&
                request.type <=
                    static_cast<uint64_t>(os::TaskOperation::RecoveryList)) {
                return serviceRecovery().call(caller, request, reply);
            }

            if (request.type ==
                    static_cast<uint64_t>(os::TaskOperation::RecoveryAppend) ||
                request.type ==
                    static_cast<uint64_t>(os::TaskOperation::RecoveryReadLog) ||
                request.type == static_cast<uint64_t>(
                                    os::TaskOperation::RecoveryGeneration)) {
                return serviceRecovery().call(caller, request, reply);
            }

            // 把只读查询、会修改当前寄存器的操作和跨任务控制分别处理，每条路径仍独立检查消息长度与权限。
            switch (static_cast<os::TaskOperation>(request.type)) {
                // 当前现场操作直接执行；退出不会返回，exec
                // 成功会安装新执行位置并跳过已经失效的旧回复区。
                case os::TaskOperation::ExitTask:
                    task::exitCurrent(static_cast<int>(args[0]));
                case os::TaskOperation::ExitThread:
                    exitCurrentThread(static_cast<int>(args[0]));
                case os::TaskOperation::CreateThread:
                    reply.data[0] = TaskMechanisms(caller).createThread(
                        args[0], args[1], args[2]);
                    break;
                case os::TaskOperation::Yield:
                    task::yieldThread();
                    break;
                case os::TaskOperation::Sleep:
                    sleepCurrentThread(args[0]);
                    break;
                case os::TaskOperation::StandardHandle:
                    reply.data[0] =
                        caller.standardHandle(static_cast<uint32_t>(args[0]));
                    break;
                case os::TaskOperation::Fork:
                    reply.data[0] = TaskMechanisms(caller).fork();
                    break;
                case os::TaskOperation::ReplaceImage: {
                    uint64_t failureResult = 0;

                    if (!TaskMechanisms(caller).replace(args[0],
                                                        failureResult)) {
                        reply.data[0] = failureResult;
                    }

                    break;
                }

                // 子操作零表示查询，修改只作用于调用者；附加组全部验证后才能改任何身份字段。
                case os::TaskOperation::Credentials: {
                    if (args[0] > 9) {
                        reply.data[1] = 22;
                        break;
                    }

                    if (!args[0]) {
                        ::task::TaskReference target =
                            task::find(args[1] ? args[1] : caller.id());

                        if (!target) {
                            return failure(os::ErrorCode::NotFound);
                        }

                        if (target != &caller && caller.effectiveUid() &&
                            caller.serviceRole() != os::ServiceRole::Fs &&
                            caller.serviceRole() != os::ServiceRole::Users) {
                            return failure(os::ErrorCode::PermissionDenied);
                        }

                        os::wire::CredentialInfo info{};
                        target->credentials(info);
                        copyMessageData(&reply.data[2], &info, sizeof(info));
                    } else {
                        if (args[1] > UINT32_MAX || args[2] > UINT32_MAX ||
                            args[3] > UINT32_MAX) {
                            reply.data[1] = 22;
                            break;
                        }

                        uint32_t groups[os::maximumSupplementaryGroups]{};

                        if (args[0] == 9) {
                            if (args[1] > os::maximumSupplementaryGroups ||
                                request.length < 4 + args[1]) {
                                reply.data[1] = 22;
                                break;
                            }

                            bool valid = true;

                            for (uint32_t i = 0; i < args[1]; ++i) {
                                if (args[4 + i] >= UINT32_MAX) {
                                    valid = false;
                                }

                                groups[i] = args[4 + i];
                            }

                            if (!valid) {
                                reply.data[1] = 22;
                                break;
                            }
                        }

                        reply.data[1] = caller.changeCredentials(
                            args[0], args[1], args[2], args[3], groups);
                    }

                    break;
                }

                // 按最小 TID 枚举，不使用会随并发增删变化的链表位置。
                case os::TaskOperation::ThreadInfo: {
                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    os::wire::ThreadInfo info{};

                    if (!target->threadInfo(args[1], info)) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    copyMessageData(&reply.data[1], &info, sizeof(info));
                    break;
                }

                // 通信成功不代表 POSIX 操作成功，业务错误码和查询到的 nice
                // 值需要分别保存。
                case os::TaskOperation::Priority: {
                    if (args[0] > 2 || args[1] > 2) {
                        reply.data[1] = 22;
                        break;
                    }

                    int result    = 0;
                    reply.data[1] = applyTaskPriority(
                        caller, *thread, args[0], args[1], args[2],
                        static_cast<int>(args[3]), result);
                    reply.data[2] = static_cast<int64_t>(result);
                    break;
                }

                // 先保留目标引用，再由目标在自身锁内检查调用身份以及资源软硬上限。
                case os::TaskOperation::ResourceLimit: {
                    if (request.length < 5 || args[2] > 1 || args[1] >= 16) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target) {
                        reply.data[1] = 3;
                        break;
                    }

                    uint64_t soft = args[3], hard = args[4];
                    reply.data[1] = target->resourceLimit(
                        caller, static_cast<int>(args[1]), args[2], soft, hard);
                    reply.data[2] = soft;
                    reply.data[3] = hard;
                    break;
                }

                // 认证来源与可修改的进程身份不同；跨进程读取需要用户管理权限或受认可的身份观察服务。
                case os::TaskOperation::Authentication: {
                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    const bool identityObserver =
                        caller.managedService() &&
                        caller.hasCapability(taskCapabilityReadIdentity);

                    if (target != &caller && !identityObserver &&
                        (caller.serviceRole() != os::ServiceRole::Users ||
                         !caller.hasCapability(taskCapabilityUsers))) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    os::SessionIdentityInfo identity{};
                    target->authentication(identity.session, identity.uid,
                                           identity.gid);
                    copyMessageData(&reply.data[1], &identity,
                                    sizeof(identity));
                    break;
                }

                // 只有同时具有 Users
                // 角色和能力的服务才能认证目标；先验证全部附加组再提交身份变化。
                case os::TaskOperation::AuthenticateSession: {
                    if (caller.serviceRole() != os::ServiceRole::Users ||
                        !caller.hasCapability(taskCapabilityUsers)) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    ::task::TaskReference target = task::find(args[0]);
                    const uint64_t count = request.length >= 5 ? args[4] : 0;

                    if (count > os::maximumSupplementaryGroups ||
                        request.length < (count ? 5 + count : 4)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    uint32_t groups[os::maximumSupplementaryGroups]{};

                    for (uint32_t i = 0; i < count; ++i) {
                        if (args[5 + i] >= UINT32_MAX) {
                            return failure(os::ErrorCode::InvalidArgument);
                        }

                        groups[i] = args[5 + i];
                    }

                    if (!target || args[1] >= 65535 || args[2] >= 65535 ||
                        args[3] > 1 ||
                        !target->authenticate(args[1], args[2], args[3], count,
                                              groups)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    break;
                }

                // 创建出的 Task
                // 句柄要放进回复句柄数组，调用者不能把消息投递成功状态当成资源编号。
                case os::TaskOperation::SpawnProgram:
                case os::TaskOperation::SpawnImage: {
                    TaskImages images(caller);
                    reply.data[0] =
                        request.type == static_cast<uint64_t>(
                                            os::TaskOperation::SpawnProgram)
                            ? images.spawnProgram(args[0], args[1], args[2],
                                                  args[3])
                            : images.spawnImage(args[0]);

                    if (static_cast<int64_t>(reply.data[0]) >= 0) {
                        addCreatedHandle(reply, reply.data[0]);
                    }

                    break;
                }

                // 等待方法在条件锁内检查和登记，退出这些锁后才真正阻塞；醒来重新检查，因为通知不会替某个线程保留事件。

                // 等待类请求必须先检查并登记条件，放掉相关状态锁后才睡眠；醒来重新确认事件是否仍可领取。
                case os::TaskOperation::WaitChild:
                case os::TaskOperation::WaitId:
                case os::TaskOperation::WaitThread: {
                    const bool waitId =
                        request.type ==
                        static_cast<uint64_t>(os::TaskOperation::WaitId);
                    const bool child =
                        request.type !=
                        static_cast<uint64_t>(os::TaskOperation::WaitThread);
                    // 检查未知等待标志和
                    // INT64_MIN，避免对最小整数取负溢出；waitid
                    // 还必须至少选择一类事件。
                    const uint64_t allowed = waitId ? 0x0100000fULL : 11ULL;

                    if ((child && ((args[1] & ~allowed) ||
                                   (waitId && !(args[1] & 14U)))) ||
                        (!child && args[1] > 1) ||
                        static_cast<int64_t>(args[0]) == INT64_MIN) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    const uint32_t options =
                        args[1] | (waitId ? 0x10000000U : 0U);
                    os::wire::ChildEvent event{};

                    while (true) {
                        int status  = 0;
                        uint64_t id = 0;
                        const int result =
                            child ? caller.waitForChild(
                                        static_cast<int64_t>(args[0]), thread,
                                        status, options, id,
                                        waitId ? &event : nullptr)
                                  : caller.waitForThread(args[0], thread,
                                                         status, args[1]);

                        if (result < 0) {
                            return failure(os::ErrorCode::NotFound);
                        }

                        if (result == 2) {
                            if (!child) {
                                return failure(os::ErrorCode::WouldBlock);
                            }

                            break;
                        }

                        if (result == 0) {
                            reply.data[0] = child ? id : 0;
                            reply.data[1] = static_cast<int64_t>(status);
                            break;
                        }

                        blockWaitingThread();

                        if (const int error = thread->waitError()) {
                            // 子任务事件与 SIGCHLD
                            // 可能同时唤醒请求，优先返回已经完成的子任务事件，再考虑被信号打断。
                            if (child &&
                                error == static_cast<int>(
                                             os::ErrorCode::Interrupted) &&
                                caller.waitForChild(
                                    static_cast<int64_t>(args[0]), thread,
                                    status, options | 1U, id,
                                    waitId ? &event : nullptr) == 0) {
                                reply.data[0] = id;
                                reply.data[1] = static_cast<int64_t>(status);
                                break;
                            }

                            return error;
                        }
                    }

                    if (waitId) {
                        reply.data[2] = event.uid;
                        reply.data[3] = event.code;
                        reply.data[4] = static_cast<int64_t>(event.status);
                    }

                    break;
                }

                // 读取权限较宽，管理权限需要
                // root、自身或直接父任务；后续控制仍需遵守受监护服务限制。
                case os::TaskOperation::OpenTask: {
                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target || target == task::getIdle()) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    uint32_t rights = handleRightRead;

                    if (caller.effectiveUid() == 0 ||
                        target->parentId() == caller.id() ||
                        target == &caller) {
                        rights |= handleRightManage;
                    }

                    const uint64_t handle =
                        caller.handles().add(target, HandleType::Task, rights);

                    if (!handle) {
                        return failure(os::ErrorCode::OutOfMemory);
                    }

                    addCreatedHandle(reply, handle);
                    break;
                }

                case os::TaskOperation::CorePolicyQuery: {
                    os::wire::CorePolicy policy{};
                    coreDump().query(policy);
                    copyMessageData(&reply.data[1], &policy, sizeof(policy));
                    break;
                }

                // 整个查询和控制期间固定对象；普通终止不能控制托管服务，受保护控制只允许
                // Init 或获准的直接监督父任务。
                case os::TaskOperation::State:
                case os::TaskOperation::Info:
                case os::TaskOperation::Terminate:
                case os::TaskOperation::ControlSignal:
                case os::TaskOperation::ControlTerminate: {
                    const bool controlled =
                        request.type == static_cast<uint64_t>(
                                            os::TaskOperation::ControlSignal) ||
                        request.type ==
                            static_cast<uint64_t>(
                                os::TaskOperation::ControlTerminate);
                    const bool terminate =
                        controlled ||
                        request.type ==
                            static_cast<uint64_t>(os::TaskOperation::Terminate);
                    HandleTable::Pin pin(
                        caller.handles(), args[0], HandleType::Task,
                        terminate ? handleRightManage : handleRightRead);
                    Task* target = pin.as<Task>();

                    if (!target) {
                        return failure(os::ErrorCode::InvalidHandle);
                    }

                    if (terminate) {
                        if (target == &caller || target == task::getIdle()) {
                            return failure(os::ErrorCode::InvalidHandle);
                        }

                        if (controlled) {
                            if (!target->managedService() ||
                                (caller.serviceRole() !=
                                     os::ServiceRole::Init &&
                                 (target->parentId() != caller.id() ||
                                  caller.serviceRole() ==
                                      os::ServiceRole::None))) {
                                return failure(os::ErrorCode::PermissionDenied);
                            }

                            if (request.type ==
                                static_cast<uint64_t>(
                                    os::TaskOperation::ControlSignal)) {
                                if (args[1] != 15 ||
                                    !target->deliverSignal(15)) {
                                    return failure(
                                        os::ErrorCode::InvalidArgument);
                                }

                                break;
                            }
                        } else if (target->managedService()) {
                            return failure(os::ErrorCode::PermissionDenied);
                        }

                        target->terminateOtherThreads(
                            static_cast<int>(args[1]));
                        target->destroy();
                    } else if (request.type ==
                               static_cast<uint64_t>(os::TaskOperation::Info)) {
                        describe(*target, caller, reply);
                    } else {
                        reply.data[1] =
                            static_cast<int64_t>(target->exitCode());
                        reply.data[0] = target->state() == TaskState::Stopped
                                            ? 2
                                        : target->isTerminated() &&
                                                !coreDump().pending(*target)
                                            ? 1
                                            : 0;

                        if (reply.data[0] == 1) {
                            target->destroy();
                        }
                    }

                    break;
                }

                case os::TaskOperation::Identity:
                    reply.data[0] = caller.id();
                    break;
                case os::TaskOperation::ThreadIdentity:
                    reply.data[0] = thread->getId();
                    break;
                // 恢复系统调用现场并标记已恢复，阻止普通结果赋值和旧回复拷贝覆盖刚恢复的寄存器。
                case os::TaskOperation::SignalReturn:
                    if (thread->getType() != ThreadType::User ||
                        !thread->syscallContext() ||
                        !task::restoreSignalFrame(thread->syscallContext(),
                                                  args[0])) {
                        task::fatal(11);
                    }

                    thread->markContextRestored();
                    break;
                // 正在使用的备用信号栈不能被处理函数从脚下替换；此处检查范围，真正投递时还会验证可写页面。
                case os::TaskOperation::SignalStack: {
                    const OsSignalStack& old = thread->signalStack;
                    const uint64_t rsp       = thread->syscallContext()
                                                   ? thread->syscallContext()->userRsp
                                                   : 0;
                    const uint64_t base =
                        reinterpret_cast<uint64_t>(old.pointer);
                    const bool active =
                        old.size && rsp >= base && rsp - base < old.size;
                    OsSignalStack previous = old;
                    previous.flags         = old.size ? (active ? 1 : 0) : 2;

                    if (args[0]) {
                        if (active) {
                            return failure(os::ErrorCode::PermissionDenied);
                        }

                        OsSignalStack value{};
                        copyMessageData(&value, &args[1], sizeof(value));
                        const uint64_t address =
                            reinterpret_cast<uint64_t>(value.pointer);

                        if (value.flags != 0 && value.flags != 2) {
                            return failure(os::ErrorCode::InvalidArgument);
                        }

                        if (value.flags == 2) {
                            value = {};
                        } else if (value.size < 2048 || address < 0x10000 ||
                                   address >= 0x800000000000ULL ||
                                   value.size > 0x800000000000ULL - address) {
                            return failure(os::ErrorCode::InvalidArgument);
                        }

                        thread->signalStack = value;
                    }

                    copyMessageData(&reply.data[1], &previous,
                                    sizeof(previous));
                    break;
                }

                // 读取旧的任务转储上限，并按权限应用更新；全局写入服务策略是另一项请求。
                case os::TaskOperation::CoreLimit:
                    reply.data[1] = caller.coreLimit();
                    reply.data[2] = caller.coreHardLimit();

                    if (args[0] && !caller.setCoreLimit(args[1], args[2])) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    break;
                // 故障寄存器和地址可能包含敏感数据，返回诊断前检查亲缘、身份、可转储属性或明确授权。
                case os::TaskOperation::ExitInfo: {
                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target ||
                        (target != &caller && caller.effectiveUid() &&
                         !caller.hasCapability(taskCapabilitySpawn) &&
                         (target->parentId() != caller.id() ||
                          !target->dumpable() ||
                          target->effectiveUid() != caller.effectiveUid()))) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    os::wire::ExitInfo info{};
                    target->getExitInfo(info);
                    copyMessageData(&reply.data[1], &info, sizeof(info));
                    break;
                }

                // 只登记有界用户映射和完整终止路径，供转储文件使用；登记元数据本身不建立映射，也不授予访问权限。
                case os::TaskOperation::ImageMapping: {
                    os::wire::ImageMapping image{};

                    if (request.length < sizeof(image) / 8) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    copyMessageData(&image, args, sizeof(image));

                    if (image.begin < 0x10000 || image.begin >= image.end ||
                        image.end > 0x800000000000ULL ||
                        ((image.begin | image.end | image.offset) & 4095) ||
                        !image.path[0] || image.path[sizeof(image.path) - 1] ||
                        !mm::validateUserMemory(
                            caller.addressSpace().rootPhysicalAddress(),
                            image.begin, 1, false) ||
                        !caller.recordImage(image)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    break;
                }

                // 只为当前任务准备合法的延迟内存，保留写权限和写时复制检查，不能变成任意物理内存复制接口。
                case os::TaskOperation::ProbeMemory: {
                    if (args[2] > 1) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    if (caller.isSystemService() &&
                        args[0] >= 0xffff800000000000ULL) {
                        if (!mm::validateUserMemory(
                                caller.addressSpace().rootPhysicalAddress(),
                                args[0], args[1], args[2])) {
                            return failure(os::ErrorCode::BadAddress);
                        }
                    } else if (!mm::pagerBridge().ensureUserMemory(
                                   args[0], args[1], args[2])) {
                        return failure(os::ErrorCode::BadAddress);
                    }

                    break;
                }

                // 通过投递代号判断是否真的安装了处理函数，不能把普通唤醒算作一次信号投递。
                case os::TaskOperation::SignalGeneration:
                    reply.data[1] = thread->signalGeneration;
                    break;
                // 限制程序头数量后才保存调试所需辅助信息，避免用户数据越过元数据容量。
                case os::TaskOperation::Auxiliary:
                    if (!args[1] || args[1] > 128 || args[2] < 0x10000 ||
                        args[2] >= 0x800000000000ULL ||
                        args[3] >= 0x800000000000ULL ||
                        !mm::validateUserMemory(
                            caller.addressSpace().rootPhysicalAddress(),
                            args[0], args[1] * 56, false)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    caller.recordAuxiliary(args[0], args[1], args[2], args[3]);
                    break;
                // CoreDump 检查固定 Task
                // 写入服务身份；捕获数据已独立于退出进程地址空间，之后可以安全进行用户态磁盘写入。
                case os::TaskOperation::CoreConfigure: {
                    os::wire::CorePolicy policy{};
                    copyMessageData(&policy, args, sizeof(policy));
                    const int result = coreDump().configure(caller, policy);

                    if (result) {
                        return result;
                    }

                    break;
                }

                case os::TaskOperation::CoreAcquire: {
                    os::wire::CoreTicket ticket{};
                    const int result = coreDump().acquire(caller, ticket);

                    if (result) {
                        return result;
                    }

                    copyMessageData(&reply.data[1], &ticket, sizeof(ticket));
                    break;
                }

                // 先按票据读入有界内核临时块，再按普通用户内存检查拷贝；票据不能用来任意读取
                // Task 内存。
                case os::TaskOperation::CoreRead: {
                    if (caller.serviceRole() != os::ServiceRole::Task) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    uint8_t buffer[4096];

                    if (!args[3] || args[2] > sizeof(buffer)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    const int result = coreDump().read(caller, args[0], args[1],
                                                       buffer, args[2]);

                    if (result < 0) {
                        return result;
                    }

                    if (!task::copyToUser(reinterpret_cast<void*>(args[3]),
                                          buffer, result)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    reply.data[0] = result;
                    break;
                }

                case os::TaskOperation::CoreFinish: {
                    const int result =
                        coreDump().finish(caller, args[0], args[1]);

                    if (result) {
                        return result;
                    }

                    break;
                }

                // TaskReference 在链表解锁前保留引用；按 PID
                // 前进避免下一次请求把变化后的链表位置误当成同一任务。
                case os::TaskOperation::Query:
                case os::TaskOperation::Enumerate: {
                    ::task::TaskReference target =
                        request.type ==
                                static_cast<uint64_t>(os::TaskOperation::Query)
                            ? task::find(args[0] ? args[0] : caller.id())
                        : args[1] == 1          ? task::next(args[0])
                        : args[0] <= UINT32_MAX ? task::at(args[0])
                                                : TaskReference();

                    if (!target) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    describe(*target, caller, reply);
                    break;
                }

                // 在这里完成权限检查；底层设置函数只更新记录，假定上层已经确认调用者资格。
                case os::TaskOperation::SetCredentials: {
                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target || args[1] >= 65535 || args[2] >= 65535 ||
                        args[3] > 1) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    if (!caller.hasCapability(taskCapabilityUsers) &&
                        (&caller != target || (caller.effectiveUid() != 0 &&
                                               (args[1] != caller.uid() ||
                                                args[2] != caller.gid())))) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    target->setCredentials(args[1], args[2], args[3]);
                    break;
                }

                // 控制台和伪终端槽位由各自固定服务角色控制；前台进程组用于信号路由，与图形窗口尺寸无关。
                case os::TaskOperation::SetTerminalForeground:
                    if ((args[0] == 1 &&
                         caller.serviceRole() != os::ServiceRole::Console) ||
                        (args[0] == 2 &&
                         caller.serviceRole() != os::ServiceRole::Tty) ||
                        (args[0] != 1 && args[0] != 2)) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    if (!task::setTerminalForeground(args[0], args[1],
                                                     args[2])) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    break;
                // 使用真实调用者身份和
                // PID、进程组选择规则；托管服务控制另走专用路径，零号信号只探测存在性，不支持
                // -1 广播。
                case os::TaskOperation::SendSignal: {
                    const int64_t target = static_cast<int64_t>(args[0]);

                    if (args[1] > maximumSignal || target == INT64_MIN) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    if (target > 0 && args[1]) {
                        ::task::TaskReference process = task::find(target);

                        if (process && process->managedService()) {
                            return failure(os::ErrorCode::PermissionDenied);
                        }
                    }

                    const bool delivered =
                        target > 0
                            ? task::sendSignalToTask(target, args[1], &caller)
                        : target == 0
                            ? task::sendSignalToTaskGroup(caller.taskGroupId(),
                                                          args[1], &caller)
                            : target < -1 && task::sendSignalToTaskGroup(
                                                 -target, args[1], &caller);

                    if (!delivered) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    break;
                }

                // 定向线程信号只查找调用者自身的线程链表。
                case os::TaskOperation::SendThreadSignal:
                    if (args[1] > maximumSignal) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    if (!caller.deliverThreadSignal(args[0], args[1])) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    break;
                // 任务已经是进程组组长时拒绝创建新会话，避免直接改写其会话、进程组和终端关联。
                case os::TaskOperation::CreateSession:
                    if (!caller.createSession()) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    reply.data[0] = caller.sessionId();
                    break;
                // 返回父任务、进程组和会话编号，这些是身份数字，不是对象句柄。
                case os::TaskOperation::Identifiers: {
                    ::task::TaskReference target =
                        task::find(args[0] ? args[0] : caller.id());

                    if (!target) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    os::wire::TaskIdentifiers ids{target->parentId(),
                                                  target->taskGroupId(),
                                                  target->sessionId()};
                    copyMessageData(&reply.data[1], &ids, sizeof(ids));
                    break;
                }

                // 父任务可指定直接孩子，但目标 Task
                // 仍会检查新进程组必须处于同一会话。
                case os::TaskOperation::SetGroup: {
                    ::task::TaskReference target =
                        task::find(args[1] ? args[1] : caller.id());

                    if (!target || (target != &caller &&
                                    target->parentId() != caller.id())) {
                        return failure(os::ErrorCode::NotFound);
                    }

                    if (!target->setTaskGroup(args[0])) {
                        return failure(os::ErrorCode::PermissionDenied);
                    }

                    break;
                }

                // 先检查支持的标志以及处理函数和返回入口映射；实际投递时仍需可执行用户页，登记成功不意味着以后可以绕过执行权限。

                // 登记处理函数时检查入口和标志，真正投递信号时还会重新检查用户页面权限。
                case os::TaskOperation::SignalAction: {
                    if (args[0] > maximumSignal ||
                        !task::isValidSignal(args[0]) || args[1] > 1) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    SignalAction action{}, old{};

                    if (args[1]) {
                        if (request.length < 8) {
                            return failure(os::ErrorCode::InvalidArgument);
                        }

                        copyMessageData(&action, &args[4], sizeof(action));

                        if ((action.getFlags() &
                             ~(signalFlagNoDefer | signalFlagResetHand |
                               signalFlagOnStack | 0x00000004ULL |
                               0x10000000ULL)) ||
                            (action.getHandler() > signalIgnore &&
                             (!action.getRestorer() ||
                              !mm::validateUserMemory(
                                  caller.addressSpace().rootPhysicalAddress(),
                                  action.getHandler(), 1, false) ||
                              !mm::validateUserMemory(
                                  caller.addressSpace().rootPhysicalAddress(),
                                  action.getRestorer(), 1, false))) ||
                            !caller.setSignalAction(args[0], action, &old)) {
                            return failure(os::ErrorCode::InvalidArgument);
                        }
                    } else if (!caller.getSignalAction(args[0], old)) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    copyMessageData(&reply.data[1], &old, sizeof(old));
                    break;
                }

                // 信号掩码属于当前线程；无论增加、删除还是整体替换，都清除不可屏蔽的
                // Kill 和 Stop 位。
                case os::TaskOperation::SignalMask: {
                    if (args[0] > 2) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    const uint64_t old = thread->getSignalMask();
                    const uint64_t mask =
                        args[1] &
                        ~((1ULL << (static_cast<uint32_t>(Signal::Kill) - 1)) |
                          (1ULL << (static_cast<uint32_t>(Signal::Stop) - 1)));
                    thread->setSignalMask(args[0] == 0   ? old | mask
                                          : args[0] == 1 ? old & ~mask
                                                         : mask);
                    reply.data[1] = old;
                    break;
                }

                case os::TaskOperation::PendingSignals:
                    reply.data[1] = caller.pendingSignals(thread);
                    break;
                // 同步选择可以取走接受集合内的待处理信号，不受普通异步投递掩码限制，本次调用不执行用户处理函数。
                case os::TaskOperation::TakeSignal: {
                    if (!args[0]) {
                        return failure(os::ErrorCode::InvalidArgument);
                    }

                    SignalAction action{};
                    const uint32_t value = caller.takePendingSignal(
                        thread, args[0], action, false);

                    if (!value) {
                        return failure(os::ErrorCode::WouldBlock);
                    }

                    reply.data[0] = value;
                    break;
                }

                default:
                    return failure(os::ErrorCode::InvalidArgument);
            }

            reply.length = os::ipcDataWordCount;

            return 0;
        }

    }  // namespace TaskService

    /*
     * 返回任务内核服务入口，并登记需要保存或修改系统调用现场的操作类别。
     */
    EndpointService& taskKernelService() {
        static EndpointService service(TaskService::call);

        return service;
    }

}  // namespace task
