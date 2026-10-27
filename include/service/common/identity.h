#pragma once
#include <cstdint>

namespace os {
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
}  // namespace os
