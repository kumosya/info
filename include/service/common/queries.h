#pragma once
#include <os/service.h>
#include "service/common/records.h"
namespace os::detail {
class TaskQuery : public Sys::Transport {
   public:
    TaskQuery();
    int exitInfo(uint64_t id, wire::ExitInfo& info);
    uint64_t spawnImage(const wire::SpawnImageInfo& info);
    int getInfo(uint64_t taskHandle, wire::TaskInfo& info);
    int query(uint64_t taskId, wire::TaskInfo& info);
    int threadInfo(uint64_t taskId, uint64_t minimumTid,
                   wire::ThreadInfo& info);
    int credentials(uint64_t taskId, wire::CredentialInfo& info);
};
class MemoryQuery : public Sys::Transport {
   public:
    MemoryQuery();
    int pageInfo(uint64_t handle, wire::PageObjectInfo& info);
    int usage(wire::MemoryUsage& info, uint64_t target = 0);
};
class SystemQuery : public Sys::Transport {
   public:
    SystemQuery();
    bool getSystemInfo(wire::SystemInfo& info);
};
}  // namespace os::detail

namespace os::detail {
class PciQuery : public Sys::Transport {
   public:
    PciQuery();
    int enumerate(wire::PciFunctionInfo* functions, uint32_t capacity);
    int read(const wire::PciFunctionInfo& function, uint16_t offset,
             uint32_t& value);
    int write(const wire::PciFunctionInfo& function, uint16_t offset,
              uint32_t value, uint64_t requester);
    int query(uint16_t segment, uint8_t bus, uint8_t device, uint8_t function,
              wire::PciFunctionInfo& result);
};
}  // namespace os::detail

namespace os::detail {
class MemoryControl : public Sys::Transport {
   public:
    MemoryControl();
    uint64_t allocatePages(uint32_t order, uint16_t node = UINT16_MAX,
                           uint64_t owner = 0);
    uint64_t mapPages(uint64_t handle, uint64_t address, uint64_t size,
                      uint64_t offset, uint64_t protection,
                      uint64_t target = 0);
    int64_t reclaimCaches();
    int unmapTarget(uint64_t address, uint64_t size, uint64_t target = 0);
    int protectTarget(uint64_t address, uint64_t size, uint64_t protection,
                      uint64_t target = 0);
    uint64_t mapSharedMemory(uint64_t handle, uint64_t address, uint64_t size,
                             uint64_t offset, uint64_t protection,
                             uint64_t target = 0);
    int resizeSharedMemory(uint64_t handle, uint64_t size);
    int unlinkSharedMemory(const char* name);
    uint64_t openSharedMemory(const char* name, int flags);
};
class MappingClient {
   public:
    uint64_t anonymous(uint64_t size, uint32_t protection, bool lazy = true,
                       uint64_t hint = 0, uint64_t* identity = nullptr);
    uint64_t shared(uint64_t handle, uint64_t size, uint32_t protection,
                    uint64_t offset = 0, uint64_t hint = 0,
                    uint64_t* identity = nullptr);
    uint64_t file(uint64_t descriptor, uint64_t size, uint32_t protection,
                  uint64_t offset = 0, uint64_t hint = 0,
                  uint64_t* identity = nullptr);
    int unmap(uint64_t address, uint64_t size);
    int protect(uint64_t address, uint64_t size, uint32_t protection);
    int sync(uint64_t address, uint64_t size);
    uint64_t openShared(const char* name, int flags);
    int unlinkShared(const char* name);
    int owned(uint64_t operation, uint64_t address, uint64_t size,
              uint64_t identity, uint64_t argument = 0);
};
}  // namespace os::detail

namespace os::detail {
class EndpointControl {
   public:
    uint64_t create();
    int cancel(uint64_t endpoint, uint64_t threadId);
};
class PipeClient : public Ipc::Pipe {
   public:
    explicit PipeClient(uint64_t handle);
    uint64_t getIdentity() const;
};
class QueueClient : public Ipc::MsgQueue {
   public:
    explicit QueueClient(uint64_t handle);
};
}  // namespace os::detail

#include <os/pty.h>
namespace os::detail {
class PtyClient : public Pty {
 public:
    explicit PtyClient(uint64_t handle);
    bool getNumber(uint32_t& number);
};
}

#include <os/storage.h>
namespace os::detail {
class StorageClient {
 public:
    int64_t deviceCount();
    bool open(Storage::Device& device, uint32_t id, uint64_t service);
};
}

namespace os::detail {
class RegistryClient : public Sys::Registry {
 public:
    explicit RegistryClient(uint64_t endpoint = 0);
    uint64_t lookup(const char* name, uint32_t attempts = 100, uint64_t* generation = nullptr);
    bool registerName(const char* name, uint64_t endpoint, uint64_t access = 0, uint64_t* generation = nullptr);
    bool unregister(const char* name, uint64_t generation);
    uint64_t endpoint() const;
};
class ManagerClient : public Sys::Manager {
 public:
    explicit ManagerClient(const char* name = "control.init");
    using Sys::Manager::call;
};
}

namespace os::detail {
class TaskControl : public Task {
 public:
    TaskControl();
    explicit TaskControl(uint64_t id);
    uint64_t getId() const;
    int refresh(uint64_t id = 0);
    int next(uint64_t minimumId);
    int refreshExit(uint64_t id = 0);
    int setGroup(uint64_t group, uint64_t id = 0);
    using Task::execute;
    using Task::executeWithIo;
    using Task::executeService;
    using Task::refreshHandle;
    using Task::signalReturn;
    using Task::spawnProgram;
    using Task::spawnImage;
    using Task::waitChild;
    using Task::waitThread;
    using Task::terminate;
    using Task::sendSignal;
    using Task::sendThreadSignal;
    using Task::createSession;
    using Task::signalStack;
    using Task::coreLimit;
    using Task::recordImage;
    using Task::accessible;
    using Task::recordAuxiliary;
    using Task::getThreadId;
    using Task::getState;
    using Task::getIdentifiers;
    using Task::getPendingSignals;
    using Task::setSignalAction;
    using Task::setSignalMask;
    using Task::setCredentials;
    using Task::wait;
    using Task::id;
    using Task::kill;
};
}
