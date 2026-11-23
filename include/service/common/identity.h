/* 服务角色、程序身份和启动入口编号。 */

#pragma once
/* Native startup auxiliary-vector tags, separate from syscall entry numbers. */
#define OS_AUX_KERNEL_HANDLE 0x60000004
#define OS_AUX_REGISTRY_HANDLE 0x60000005
/* Operations used by the freestanding first-user-thread probe. */
#define OS_TASK_EXIT_OPERATION 96
#define OS_TASK_YIELD_OPERATION 99

#ifndef __ASSEMBLER__
#include <cstdint>

namespace services {

    enum class ServiceRole : uint32_t {
        None,
        Init,
        Task,
        Fs,
        Pci,
        Device,
        Storage,
        Tty,
        Console,
        Window,
        Users,
        Memory,
        Pipe,
        MessageQueue
    };
    enum class ProgramId : uint64_t {
        Init = 0,
        TaskServer,
        FsServer,
        PciServer,
        DeviceServer,
        StorageServer,
        TtyServer,
        ConsoleServer,
        WindowServer,
        UsersServer,
        MemoryManager,
        PipeServer,
        MessageQueueServer,
        Count
    };
}  // namespace services

#endif
