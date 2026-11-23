/* 系统启动、服务注册与登录会话监护。 */

#pragma once
#include <os/console.h>
#include <os/detail/client.h>
#include <os/device.h>
#include <os/dir.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/sync.h>
#include <os/task.h>
#include <os/user.h>

#include "service/common/protocol.h"
#include "service/common/records.h"

namespace init {

    /* 读取并保存终端会话的启动程序和相关配置。 */
    class SessionConfiguration {
        public:
            SessionConfiguration() = default;
            ~SessionConfiguration() = default;
            bool load();

            const char* getTerminal() const {
                return terminal;
            }

            const char* getTerminalName() const {
                return terminalName;
            }

            const char* getLogin() const {
                return login;
            }

            const char* getLoginName() const {
                return loginName;
            }

            const char* getGraphical() const {
                return graphical;
            }

            const char* getGraphicalName() const {
                return graphicalName;
            }

        private:
            char terminal[128]{}, terminalName[24]{}, login[128]{},
                loginName[24]{}, graphical[128]{}, graphicalName[24]{};
    };

    /* 收集启动各阶段的结果，并向启动终端输出进度或失败信息。 */
    class BootReporter {
            /* 保存所属表中的一个条目，由外层容器统一分配和回收。 */
            struct Entry {
                    char name[64]{};
                    bool started = false;
                    int32_t error = 0;
                    uint64_t revision = 0, sent = 0;
            };

            Entry entries_[96]{};
            uint32_t count_ = 0;
            os::SpinLock lock_;
            uint64_t instance_ = 0;
            uint32_t preferred_ = os::graphicalTerminalId;
            bool handedOff_ = false;

        public:
            uint32_t preferredTerminal() const {
                return preferred_;
            }

            void record(const char* name, bool started, int32_t error = 0);
            // Only init's main thread pumps IPC; the lifecycle observer only
            // appends under lock.
            bool pump(uint64_t registry, uint32_t terminal = UINT32_MAX,
                      uint32_t fallback = UINT32_MAX);
    };

    /* 处理服务注册与查询，保护固定服务名并维护当前服务实例。 */
    class InitServiceManager {
        public:
            bool init();

            uint64_t endpoint() const {
                return endpoint_;
            }

        private:
            /* 保存所属容器中的一条记录，其有效性由外层对象管理。 */
            struct Record {
                    char name[services::serviceNameCapacity]{};
                    uint64_t endpoint = 0;
                    uint64_t publisher = 0;
                    uint64_t generation = 0;
                    uint32_t uid = 0;
                    services::ServiceRole role = services::ServiceRole::None;
                    uint64_t access = 0;
            };

            static constexpr uint32_t capacity = 64;
            Record records_[capacity]{};
            uint32_t count_ = 0;
            uint64_t endpoint_ = 0;

            static void threadMain(void* manager) {
                static_cast<InitServiceManager*>(manager)->run();
            }

            static bool query(uint64_t id, services::wire::TaskInfo& info) {
                return os::detail::TaskQuery().query(id, info) == 0 &&
                       info.state != 2;
            }

            static bool validName(const os::IpcMessage& request);
            static services::ServiceRole requiredRole(const char* name);
            void invalidate(Record& record);
            void sweep();
            void handle(os::IpcMessage& request, os::IpcMessage& reply);
            void run();
    };

    /* 监护多个终端会话，启动登录程序并处理会话退出后的重启。 */
    class SessionSupervisor {
            init::SessionConfiguration config_;

            /* 保存一个登录或终端会话的启动状态和关联任务。 */
            struct Session {
                    uint64_t handle = 0, started = 0, nextAttempt = 0;
                    bool desired = true;
                    uint32_t failures = 0;
            };

            Session sessions_[os::textTerminalCount + 1]{};
            Session parked_[8]{};
            uint64_t windowInstance_ = 0;
            uint64_t consoleInstance_ = 0;
            bool configured_ = false, graphicalReady_ = false;
            static uint64_t retryDelay(uint32_t index, uint32_t failures);
            static bool failed(Session& session, uint32_t index);
            bool start(uint32_t index);
            const char* terminalArguments_[1]{};

        public:
            bool init();
            bool available(uint32_t terminal) const;
            uint32_t initialTerminal(uint32_t preferred) const;
            void tick();
    };

}  // namespace init
