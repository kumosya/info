/*
 * 服务注册、连接与恢复。
 */

#pragma once
#include <os/ipc.h>

namespace services {

    enum class CoreServiceKind : uint64_t;
    enum class TaskOperation : uint64_t;
    enum class ServiceOperation : uint64_t;
}  // namespace services

namespace services::wire {

    struct ServiceStatus;
}

namespace os {

    namespace detail {

        class RegistryClient;
        class ManagerClient;
    }  // namespace detail

    /* 封装系统信息、时间和日志等核心服务请求。 */
    class Sys {
        public:
            enum class Architecture : uint32_t {
                Unknown = 0,
                Amd64 = 1
            };

            class Manager;

            /* 封装与服务端点的消息交换及错误传播。 */
            class Transport {
                protected:
                    friend class Sys;
                    friend class SessionIdentity;
                    explicit Transport(services::CoreServiceKind kind);
                    uint64_t openHandle();
                    int call(IpcMessage& request, IpcMessage& reply,
                             uint64_t deadline = UINT64_MAX);
                    int64_t result(IpcMessage& request, IpcMessage& reply);

                private:
                    services::CoreServiceKind kind_;
                    int callOnce(IpcMessage& request, IpcMessage& reply,
                                 uint64_t deadline);
            };

            /* 保存服务状态查询结果。 */
            class Status {
                public:
                    enum class State : uint32_t {
                        Stopped,
                        Running,
                        Failed,
                        Starting
                    };
                    const char* getName() const;
                    const char* getOwner() const;
                    const char* getExecutable() const;
                    uint64_t getTaskId() const;
                    State getState() const;
                    bool getEnabled() const;
                    bool getDesired() const;
                    bool getBuiltin() const;
                    uint32_t getRestartCount() const;
                    int32_t getExitCode() const;
                    const char* getBlockedBy() const;

                private:
                    friend class Manager;
                    char name_[24]{}, owner_[24]{}, executable_[128]{};
                    uint64_t taskId_{};
                    State state_{};
                    bool enabled_{}, desired_{}, builtin_{};
                    uint32_t restartCount_{};
                    int32_t exitCode_{};
                    char blockedBy_[24]{};
            };

            /* 保存服务恢复会话并提供检查点调用操作。 */
            class Recovery : public Transport {
                public:
                    Recovery();
                    int acquire(IpcMessage& snapshot);
                    int prepare(uint64_t bytes, bool logRecord);
                    int commit(uint64_t handle, uint64_t bytes,
                               uint64_t sequence, IpcMessage& result);
                    int append(uint64_t handle, uint64_t bytes,
                               uint64_t sequence, IpcMessage& result);
                    int readLog(uint32_t index, IpcMessage& record);
                    uint64_t getEpoch() const;

                private:
                    uint64_t epoch_ = 0;
                    int invoke(uint64_t operation, uint64_t a, uint64_t b,
                               uint64_t c, IpcMessage& reply);
            };

            /* 按名称注册和查询服务端点。 */
            class Registry {
                public:
                    explicit Registry(uint64_t endpoint = 0);

                private:
                    friend class detail::RegistryClient;
                    uint64_t lookupRaw(const char* name,
                                       uint32_t attempts = 100,
                                       uint64_t* generation = nullptr);
                    bool registerRaw(const char* name, uint64_t endpoint,
                                     uint64_t access = 0,
                                     uint64_t* generation = nullptr);
                    bool unregisterRaw(const char* name, uint64_t generation);

                public:
                    int lookup(const char* name, Ipc& endpoint);
                    int registerName(const char* name, Ipc& endpoint,
                                     uint64_t& generation);
                    int unregisterName(const char* name, uint64_t generation);
                    uint64_t getEndpoint() const;

                    void setDefault(uint64_t endpoint);

                private:
                    uint64_t registry_;
                    bool putName(IpcMessage& message, const char* name);
            };

            /* 保存服务连接及重连所需信息。 */
            class Connection {
                public:
                    bool connect(const char* name, uint64_t registry = 0);
                    int call(IpcMessage& request, IpcMessage& reply,
                             uint64_t deadline = UINT64_MAX);
                    void disconnect();
                    uint64_t getGeneration() const;
                    bool getConnected() const;
                    const char* getName() const;

                private:
                    Ipc::Handle endpoint_;
                    char name_[24]{};
                    uint64_t registry_ = 0;
                    uint64_t generation_ = 0;
            };

            /* 向启动管理服务查询、启动和停止受管理服务。 */
            class Manager {
                public:
                    explicit Manager(const char* name = "control.init");

                private:
                    friend class detail::ManagerClient;
                    int call(services::ServiceOperation operation,
                             const char* name, uint64_t index,
                             services::wire::ServiceStatus& status);

                public:
                    int list(uint32_t index, Status& status);
                    int query(const char* name, Status& status);
                    int start(const char* name);
                    int stop(const char* name);
                    int restart(const char* name);
                    int enable(const char* name);
                    int disable(const char* name);

                private:
                    int queryResult(services::ServiceOperation operation,
                                    const char* name, uint64_t index,
                                    Status& status);
                    int update(uint64_t operation, const char* name);
                    char manager_[24];
            };

            /* 封装服务检查点的准备、保存、提交和恢复对象操作。 */
            class Checkpoint {
                public:
                    Checkpoint();
                    ~Checkpoint();
                    Checkpoint(const Checkpoint&) = delete;
                    Checkpoint& operator=(const Checkpoint&) = delete;
                    int close();
                    bool getValid() const;
                    int getError() const;
                    bool getRestored() const;
                    // Positive means an existing checkpoint was restored; zero
                    // means a fresh domain.
                    int open(void* state, uint64_t bytes, uint64_t schema);
                    int prepare();
                    int commit(const void* state);
                    // WouldBlock requires compaction into a full checkpoint
                    // before any mutation.
                    int prepareLog(uint64_t bytes);
                    int append(const void* record, uint64_t bytes);
                    int readLog(uint32_t index, void* record, uint64_t bytes);
                    uint64_t put(uint64_t handle);
                    uint64_t get(uint64_t token);
                    bool drop(uint64_t token);
                    uint64_t next(uint64_t& cursor);
                    uint64_t getSequence() const;
                    uint64_t getEpoch() const;
                    uint32_t getRecordCount() const;
                    int queryGeneration(uint64_t task, uint64_t& value);

                private:
                    Recovery recovery_;
                    bool opened_{}, restored_{};
                    int error_{};
                    uint64_t epoch_ = 0, sequence_ = 0, bytes_ = 0, schema_ = 0,
                             staging_ = 0, capacity_ = 0;
                    uint64_t baseSequence_ = 0;
                    uint32_t records_ = 0;
                    int openSnapshot(void* state, uint64_t bytes,
                                     uint64_t schema);
                    int ensureStaging(uint64_t total);
                    int publish(const void* state, uint64_t bytes, bool append);
                    uint64_t checksum(const void* data, uint64_t size);
                    int call(services::TaskOperation op, uint64_t a, uint64_t b,
                             uint64_t c, IpcMessage& reply);
            };

        public:
            Sys();

            bool refresh();

            void log(const char* message);

            uint64_t getUptimeNanoseconds() const;
            uint64_t getTotalMemoryBytes() const;
            uint64_t getFreeMemoryBytes() const;
            uint32_t getOnlineCpus() const;
            Architecture getArchitecture() const;
            uint64_t readClockResolution();
            uint64_t readWallClockTime();
            uint64_t readMonotonicTime();

        private:
            Transport transport_;
            int64_t result(IpcMessage& request, IpcMessage& reply);
            uint64_t uptimeNanoseconds_{};
            uint64_t totalMemoryBytes_{};
            uint64_t freeMemoryBytes_{};
            uint32_t onlineCpus_{};
            Architecture architecture_{};
    };
}  // namespace os
