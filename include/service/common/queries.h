/*
 * 核心服务查询与控制封装。
 */

#pragma once
#include "service/common/records.h"
#include <os/service.h>
namespace os::detail {
    /* 按任务或线程编号查询状态、身份和执行信息。 */
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
    /* 向内核查询内存用量、页对象和任务映像代号。 */
    class MemoryQuery : public Sys::Transport {
        public:
            MemoryQuery();
            int pageInfo(uint64_t handle, wire::PageObjectInfo& info);
            int usage(wire::MemoryUsage& info, uint64_t target = 0);
    };
    /* 查询系统版本、运行时长及其他系统级信息。 */
    class SystemQuery : public Sys::Transport {
        public:
            SystemQuery();
            bool getSystemInfo(wire::SystemInfo& info);
    };
}  // namespace os::detail

namespace os::detail {
    /* 按编号查询 PCI 设备功能信息。 */
    class PciQuery : public Sys::Transport {
        public:
            PciQuery();
            int enumerate(wire::PciFunctionInfo* functions, uint32_t capacity);
            int read(const wire::PciFunctionInfo& function, uint16_t offset,
                     uint32_t& value);
            int write(const wire::PciFunctionInfo& function, uint16_t offset,
                      uint32_t value, uint64_t requester);
            int query(uint16_t segment, uint8_t bus, uint8_t device,
                      uint8_t function, wire::PciFunctionInfo& result);
    };
}  // namespace os::detail

namespace os::detail {
    /* 封装具有权限检查的内核页面、映射及锁页控制请求。 */
    class MemoryControl : public Sys::Transport {
        public:
            MemoryControl();
            uint64_t allocatePages(uint32_t order, uint16_t node = UINT16_MAX,
                                   uint64_t owner = 0);
            uint64_t mapPages(uint64_t handle, uint64_t address, uint64_t size,
                              uint64_t offset, uint64_t protection,
                              uint64_t target = 0);
            int64_t reclaimCaches();
            int unmapTarget(uint64_t address, uint64_t size,
                            uint64_t target = 0, bool preserveLocks = false);
            int protectTarget(uint64_t address, uint64_t size,
                              uint64_t protection, uint64_t target = 0);
            uint64_t mapSharedMemory(uint64_t handle, uint64_t address,
                                     uint64_t size, uint64_t offset,
                                     uint64_t protection, uint64_t target = 0);
            int resizeSharedMemory(uint64_t handle, uint64_t size);
            int unlinkSharedMemory(const char* name);
            uint64_t openSharedMemory(const char* name, int flags);
    };
    /* 封装向用户态内存服务提出的区域建立、撤销和权限请求。 */
    class MappingClient {
        public:
            int64_t openTyped(const char* name, int flags, unsigned policy);
            int64_t allocateTyped(uint64_t handle, uint64_t size);
            int typedInfo(uint64_t handle, uint64_t& available);
            int64_t memoryOffset(uint64_t address, uint64_t size,
                                 uint64_t& offset, uint64_t& contiguous,
                                 unsigned& rights);
            int policy(unsigned operation, uint64_t address, uint64_t size,
                       uint64_t argument);
            uint64_t anonymous(uint64_t size, uint32_t protection,
                               bool lazy = true, uint64_t hint = 0,
                               uint64_t* identity = nullptr);
            uint64_t shared(uint64_t handle, uint64_t size, uint32_t protection,
                            uint64_t offset = 0, uint64_t hint = 0,
                            uint64_t* identity = nullptr);
            uint64_t file(uint64_t descriptor, uint64_t size,
                          uint32_t protection, uint64_t offset = 0,
                          uint64_t hint = 0, uint64_t* identity = nullptr);
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
    /* 封装端点创建、销毁及活动请求检查等控制操作。 */
    class EndpointControl {
        public:
            uint64_t create();
            int cancel(uint64_t endpoint, uint64_t threadId);
    };
    /* 构造并发送管道或双向通道的读写及控制请求。 */
    class PipeClient : public Ipc::Pipe {
        public:
            explicit PipeClient(uint64_t handle);
            uint64_t getIdentity() const;
    };
    /* 构造命名消息队列的打开、收发和属性请求。 */
    class QueueClient : public Ipc::MsgQueue {
        public:
            explicit QueueClient(uint64_t handle);
    };
}  // namespace os::detail

#include <os/pty.h>
namespace os::detail {
    /* 构造伪终端服务请求，处理主从端和终端属性。 */
    class PtyClient : public Pty {
        public:
            explicit PtyClient(uint64_t handle);
            bool getNumber(uint32_t& number);
    };
}  // namespace os::detail

#include <os/storage.h>
namespace os::detail {
    /* 向存储服务查询设备并请求块读写。 */
    class StorageClient {
        public:
            int64_t deviceCount();
            bool open(Storage::Device& device, uint32_t id, uint64_t service);
    };
}  // namespace os::detail

namespace os::detail {
    /* 按服务名注册或查询端点，并处理注册服务返回状态。 */
    class RegistryClient : public Sys::Registry {
        public:
            explicit RegistryClient(uint64_t endpoint = 0);
            uint64_t lookup(const char* name, uint32_t attempts = 100,
                            uint64_t* generation = nullptr);
            bool registerName(const char* name, uint64_t endpoint,
                              uint64_t access      = 0,
                              uint64_t* generation = nullptr);
            bool unregister(const char* name, uint64_t generation);
            uint64_t endpoint() const;
    };
    /* 向启动管理器发送服务状态与控制请求。 */
    class ManagerClient : public Sys::Manager {
        public:
            explicit ManagerClient(const char* name = "control.init");
            using Sys::Manager::call;
    };
}  // namespace os::detail

namespace os::detail {
    /* 构造任务创建、执行、等待、身份及资源控制请求。 */
    class TaskControl : public Task {
        public:
            TaskControl();
            explicit TaskControl(uint64_t id);
            uint64_t getId() const;
            int refresh(uint64_t id = 0);
            int next(uint64_t minimumId);
            int refreshExit(uint64_t id = 0);
            int setGroup(uint64_t group, uint64_t id = 0);
            using Task::accessible;
            using Task::coreLimit;
            using Task::createSession;
            using Task::execute;
            using Task::executeService;
            using Task::executeWithIo;
            using Task::getIdentifiers;
            using Task::getPendingSignals;
            using Task::getState;
            using Task::getThreadId;
            using Task::id;
            using Task::kill;
            using Task::recordAuxiliary;
            using Task::recordImage;
            using Task::refreshHandle;
            using Task::sendSignal;
            using Task::sendThreadSignal;
            using Task::setCredentials;
            using Task::setSignalAction;
            using Task::setSignalMask;
            using Task::signalReturn;
            using Task::signalStack;
            using Task::spawnImage;
            using Task::spawnProgram;
            using Task::terminate;
            using Task::wait;
            using Task::waitChild;
            using Task::waitThread;
    };
}  // namespace os::detail
