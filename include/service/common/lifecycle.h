/*
 * 服务依赖、监护和重启。
 */

#pragma once
#include <os/detail/client.h>
#include <os/file.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include <initializer_list>

#include "service/common/protocol.h"
#include "service/common/records.h"

namespace services {

    /* 维护子服务的启动顺序、依赖和运行状态，并在故障后按规则重启。 */
    class Lifecycle {
            static constexpr uint32_t capacity = 32;

            /* 保存一个受管理服务的名称、配置、依赖及当前运行任务。 */
            struct Unit {
                    services::wire::ServiceStatus status{};
                    services::ProgramId program{};
                    char dependenciesText[128]{}, ready[24]{}, arguments[128]{};
                    uint32_t order = 0, timeout = 30000;
                    bool restart = true, self = false;
                    uint64_t task = 0, nextAttempt = 0, runningSince = 0;
                    uint32_t failures = 0, enabledOffset = 0;
            };

            Unit units_[capacity]{};
            uint32_t count_ = 0;
            uint64_t registry_ = 0, endpoint_ = 0;
            char owner_[24]{}, controlName_[24]{};
            char config_[8192]{}, parse_[8192]{};
            size_t configSize_ = 0;
            void (*observer_)(void*,
                              const services::wire::ServiceStatus&) = nullptr;
            void* observerContext_ = nullptr;

            static bool equal(const char* a, const char* b) {
                return os::Runtime::stringsEqual(a, b);
            }

            static bool validName(const char* value);
            static bool number(const char* text, uint32_t& result);
            static bool copy(char* output, size_t capacity, const char* input);
            static bool validArguments(const char* text);
            bool waitReady(Unit& unit);
            Unit* find(const char* name);
            static bool bootName(const char* name);
            bool dependencies(Unit& unit, bool runtime);
            bool start(Unit& unit);
            bool failed(Unit& unit, int code);
            bool restoresState(Unit& unit);
            void release(Unit& unit);
            bool terminate(Unit& unit, bool force = false);
            void tick();
            bool persist(Unit& unit, bool enabled);
            int control(services::ServiceOperation operation, const char* name,
                        uint64_t index, services::wire::ServiceStatus& status);

            static void thread(void* self) {
                static_cast<Lifecycle*>(self)->run();
            }

            void run();

        public:
            void observe(void (*observer)(void*,
                                          const services::wire::ServiceStatus&),
                         void* context) {
                observer_ = observer;
                observerContext_ = context;
            }

            bool init(const char* owner, uint64_t registry);
            bool bootstrap();
            bool load();
            bool readConfiguration();
            static bool configError(uint32_t line);
            bool supervise();
    };
}  // namespace services
