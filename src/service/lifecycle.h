#pragma once
#include "os/service_control.h"
#include "os/core_service.h"
#include "os/file.h"
#include "os/configuration.h"
#include <initializer_list>

namespace services {
#include "startup.inc"
class Lifecycle {
    static constexpr uint32_t capacity = 32;
    struct Unit {
        os::ServiceStatus status{};
        os::ProgramId program{};
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
    static bool equal(const char* a, const char* b) { return os::Runtime::stringsEqual(a, b); }
    static bool validName(const char* value) {
        size_t length = 0;
        for (; value[length]; ++length) if (length >= 23 || !((value[length] >= 'a' && value[length] <= 'z') ||
            (value[length] >= '0' && value[length] <= '9') || value[length] == '.' || value[length] == '-' || value[length] == '_')) return false;
        return length != 0;
    }
    static bool number(const char* text, uint32_t& result) {
        if (!*text) return false;
        uint64_t value = 0;
        for (; *text; ++text) { if (*text < '0' || *text > '9') return false; value = value * 10 + *text - '0'; if (value > 1000000) return false; }
        result = value; return true;
    }
    static bool copy(char* output, size_t capacity, const char* input) {
        if (os::Runtime::stringLength(input) >= capacity) return false;
        os::Runtime::copyString(output, input, capacity); return true;
    }
    static bool validArguments(const char* text) {
        if (equal(text, "-")) return true;
        unsigned count = 1;
        if (!*text || *text == ',') return false;
        for (; *text; ++text) if (*text == ',' && (!text[1] || text[1] == ',' || ++count > 8)) return false;
        return true;
    }
    bool waitReady(Unit& unit) {
        const uint64_t deadline = os::Clock::getMonotonicTime() + static_cast<uint64_t>(unit.timeout) * 1000000;
        do {
            os::IpcMessage request{}, reply{};
            request.type = static_cast<uint64_t>(os::RegistryRequest::Lookup); request.length = 3;
            copy(reinterpret_cast<char*>(request.data), 24, unit.ready);
            if (!os::callService(registry_, request, reply) && !reply.data[0] && reply.handleCount == 1) {
                const bool owned = reply.length >= 3 && reply.data[2] == unit.status.pid;
                os::closeHandle(reply.handles[0]);
                return owned;
            }
            int code = 0;
            if (os::Task::getState(unit.task, code)) return false;
            os::sleep(1);
        } while (os::Clock::getMonotonicTime() < deadline);
        return false;
    }
    Unit* find(const char* name) { for (uint32_t i = 0; i < count_; ++i) if (equal(name, units_[i].status.name)) return &units_[i]; return nullptr; }
    static bool bootName(const char* name) {
        if (equal(name, "init")) return true;
        for (const auto& unit : configuredServices) if (equal(name, unit.name)) return true;
        return false;
    }
    bool dependencies(Unit& unit, bool runtime) {
        if (equal(unit.dependenciesText, "-")) return true;
        char names[128]; copy(names, sizeof(names), unit.dependenciesText);
        char* name = names;
        for (char* p = names;; ++p) if (*p == ',' || !*p) {
            const bool end = !*p; *p = 0;
            if (!validName(name) || equal(name, unit.status.name)) return false;
            auto* dependency = find(name);
            if (!dependency && !bootName(name)) return false;
            if (dependency && !dependency->status.builtin && (dependency->order >= unit.order ||
                (unit.status.enabled && !dependency->status.enabled))) return false;
            if (runtime) {
                const uint64_t endpoint = os::ServiceRegistry::lookupService(registry_, name, unit.timeout);
                if (!endpoint) return false;
                os::closeHandle(endpoint);
            }
            if (end) return true;
            name = p + 1;
        }
    }
    bool start(Unit& unit) {
        if (unit.self) return true;
        if (unit.task) { int code = 0; const int state = os::Task::getState(unit.task, code); if (state == 0 || state == 2) return true; os::closeHandle(unit.task); unit.task = 0; }
        if (!unit.status.builtin && !dependencies(unit, true)) return failed(unit, -1);
        unit.status.state = os::ServiceState::Starting;
        if (unit.status.builtin) unit.task = os::Task::spawnProgram(unit.program, registry_);
        else {
            const char* arguments[9] = {unit.status.name}; uint32_t count = 1;
            char storage[128]; copy(storage, sizeof(storage), unit.arguments);
            if (!equal(storage, "-")) {
                char* begin = storage;
                for (char* p = storage;; ++p) if (*p == ',' || !*p) {
                    const bool end = !*p; *p = 0;
                    if (!*begin || count == 9) return failed(unit, -1);
                    arguments[count++] = begin; if (end) break; begin = p + 1;
                }
            }
            unit.task = os::Task::executeService(unit.status.executable, count, arguments);
        }
        if (!unit.task || static_cast<int64_t>(unit.task) < 0) { unit.task = 0; return failed(unit, -1); }
        os::TaskInfo info{};
        if (os::Task::getInfo(unit.task, info)) { terminate(unit, true); return failed(unit, -1); }
        unit.status.pid = info.id;
        if (!equal(unit.ready, "-")) {
            if (!waitReady(unit)) { terminate(unit, true); return failed(unit, -1); }
        }
        unit.status.state = os::ServiceState::Running; unit.runningSince = os::Clock::getMonotonicTime();
        return true;
    }
    bool failed(Unit& unit, int code) {
        unit.status.state = os::ServiceState::Failed; unit.status.exitCode = code;
        ++unit.failures;
        unit.nextAttempt = os::Clock::getMonotonicTime() + (1ULL << (unit.failures < 5 ? unit.failures : 5)) * 1000000000;
        os::System::log("[SERVICE] startup/exit failure: "); os::System::log(unit.status.name); os::System::log("\n");
        return false;
    }
    bool terminate(Unit& unit, bool force = false) {
        if (!unit.task) return true;
        os::IpcMessage request{}, reply{}; request.length = 4; request.data[0] = unit.task;
        request.type = static_cast<uint64_t>(force ? os::TaskOperation::ControlTerminate : os::TaskOperation::ControlSignal);
        request.data[1] = force ? static_cast<uint64_t>(-15LL) : 15;
        auto result = os::CoreServiceClient::result(os::CoreServiceKind::Task, request, reply);
        int code = 0;
        if (!force && result >= 0) {
            const uint64_t deadline = os::Clock::getMonotonicTime() + 2000000000;
            while (!os::Task::getState(unit.task, code) && os::Clock::getMonotonicTime() < deadline) os::sleep(10);
            if (!os::Task::getState(unit.task, code)) return terminate(unit, true);
        } else if (result < 0 && !os::Task::getState(unit.task, code)) return false;
        os::closeHandle(unit.task); unit.task = 0; unit.status.pid = 0;
        unit.status.state = os::ServiceState::Stopped; unit.status.exitCode = code;
        return true;
    }
    void tick() {
        for (uint32_t i = 0; i < count_; ++i) {
            auto& unit = units_[i]; if (unit.self) continue;
            int code = 0;
            const int state = unit.task ? os::Task::getState(unit.task, code) : 0;
            if (unit.task && state != 0 && state != 2) {
                os::closeHandle(unit.task); unit.task = 0; unit.status.pid = 0;
                if (os::Clock::getMonotonicTime() - unit.runningSince > 60000000000ULL) unit.failures = 0;
                failed(unit, code);
            }
            if (!unit.task && unit.status.desired && unit.restart && unit.failures < 5 &&
                os::Clock::getMonotonicTime() >= unit.nextAttempt) { ++unit.status.restarts; start(unit); }
        }
        int status = 0; while (os::Task::waitChild(-1, status, 1) > 0) {}
    }
    bool persist(Unit& unit, bool enabled) {
        if (!configSize_ || unit.status.builtin) return false;
        os::File input; if (input.open("/etc/services", O_RDONLY)) return false;
        char current[8192]; size_t used = 0;
        while (used < sizeof(current)) { const auto count = input.read(current + used, sizeof(current) - used); if (count < 0) return false; if (!count) break; used += count; }
        if (used != configSize_) return false;
        for (size_t i = 0; i < used; ++i) if (current[i] != config_[i]) return false;
        current[unit.enabledOffset] = enabled ? '1' : '0';
        os::File output; if (output.open("/etc/.services.tmp", O_CREAT | O_WRONLY | O_TRUNC, 0644)) return false;
        used = 0;
        while (used < configSize_) { const auto count = output.write(current + used, configSize_ - used); if (count <= 0) return false; used += count; }
        if (output.fsync() || output.close() || output.rename("/etc/.services.tmp", "/etc/services")) return false;
        config_[unit.enabledOffset] = current[unit.enabledOffset]; unit.status.enabled = enabled; return true;
    }
    int control(os::ServiceOperation operation, const char* name, uint64_t index, os::ServiceStatus& status) {
        Unit* unit = operation == os::ServiceOperation::List ? index < count_ ? &units_[index] : nullptr : find(name);
        if (!unit) {
            if (!equal(owner_, "init")) return static_cast<int>(os::ErrorCode::NotFound);
            uint64_t cursor = index >= count_ ? index - count_ : 0;
            for (const char* owner : {"task", "device"}) {
                uint64_t children = 0; bool belongs = false;
                for (const auto& entry : configuredServices) if (equal(entry.owner, owner)) { ++children; belongs = belongs || equal(entry.name, name); }
                char manager[24] = "control."; os::Runtime::copyString(manager + 8, owner, sizeof(manager) - 8);
                if (operation == os::ServiceOperation::List && cursor < children) return os::ServiceControl::call(operation, nullptr, cursor, status, manager);
                if (operation != os::ServiceOperation::List && belongs) return os::ServiceControl::call(operation, name, 0, status, manager);
                cursor = cursor >= children ? cursor - children : 0;
            }
            return static_cast<int>(os::ErrorCode::NotFound);
        }
        switch (operation) {
            case os::ServiceOperation::List: case os::ServiceOperation::Status: break;
            case os::ServiceOperation::Enable: case os::ServiceOperation::Disable:
                if (!persist(*unit, operation == os::ServiceOperation::Enable)) return static_cast<int>(os::ErrorCode::PermissionDenied);
                break;
            case os::ServiceOperation::Start:
                unit->status.desired = 1; unit->failures = 0;
                if (!start(*unit)) return static_cast<int>(os::ErrorCode::IoError);
                break;
            case os::ServiceOperation::Stop: case os::ServiceOperation::Restart:
                if (unit->status.builtin) return static_cast<int>(os::ErrorCode::PermissionDenied);
                for (uint32_t i = 0; i < count_; ++i) if (&units_[i] != unit && units_[i].status.desired) {
                    char dependencies[128]; copy(dependencies, sizeof(dependencies), units_[i].dependenciesText);
                    char* token = dependencies;
                    for (char* p = token;; ++p) if (*p == ',' || !*p) { bool end = !*p; *p = 0;
                        if (equal(token, unit->status.name)) {
                            status = unit->status;
                            copy(status.blockedBy, sizeof(status.blockedBy), units_[i].status.name);
                            return static_cast<int>(os::ErrorCode::Busy);
                        }
                        if (end) break;
                        token = p + 1;
                    }
                }
                unit->status.desired = 0;
                if (!terminate(*unit)) return static_cast<int>(os::ErrorCode::PermissionDenied);
                if (operation == os::ServiceOperation::Restart) { unit->status.desired = 1; unit->failures = 0; ++unit->status.restarts; if (!start(*unit)) return static_cast<int>(os::ErrorCode::IoError); }
                break;
            default: return static_cast<int>(os::ErrorCode::InvalidArgument);
        }
        status = unit->status; return 0;
    }
    static void thread(void* self) { static_cast<Lifecycle*>(self)->run(); }
    void run() {
        while (true) {
            tick(); os::IpcMessage request{}, reply{};
            const uint64_t deadline = os::Clock::getMonotonicTime() + 250000000;
            if (os::receiveMessage(endpoint_, request, deadline)) continue;
            reply.length = 1; reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            if (request.type == os::serviceControlMessage && request.length == 5 && !request.handleCount &&
                reinterpret_cast<const char*>(&request.data[2])[23] == 0) {
                os::TaskInfo caller{}; const auto operation = static_cast<os::ServiceOperation>(request.data[0]);
                if (os::Task::query(request.senderPid, caller) ||
                    (operation != os::ServiceOperation::List && operation != os::ServiceOperation::Status && caller.effectiveUid)) reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied);
                else {
                    os::ServiceStatus status{};
                    reply.data[0] = control(operation, reinterpret_cast<const char*>(&request.data[2]), request.data[1], status);
                    if (!reply.data[0] || static_cast<int64_t>(reply.data[0]) == static_cast<int>(os::ErrorCode::Busy)) {
                        os::Runtime::copyBytes(&reply.data[1], &status, sizeof(status)); reply.length = 1 + (sizeof(status) + 7) / 8;
                    }
                }
            }
            for (uint32_t i = 0; i < request.handleCount; ++i) os::closeHandle(request.handles[i]);
            os::replyMessage(endpoint_, request.replyToken, reply);
        }
    }
 public:
    bool initialize(const char* owner, uint64_t registry) {
        registry_ = registry; copy(owner_, sizeof(owner_), owner);
        copy(controlName_, sizeof(controlName_), "control."); copy(controlName_ + 8, sizeof(controlName_) - 8, owner);
        if (equal(owner, "init")) {
            auto& unit = units_[count_++]; unit.self = true;
            copy(unit.status.name, sizeof(unit.status.name), "init"); copy(unit.status.owner, sizeof(unit.status.owner), "kernel");
            unit.status.builtin = unit.status.enabled = unit.status.desired = 1;
            unit.status.state = os::ServiceState::Running; unit.status.pid = os::Task::getId();
        }
        return true;
    }
    bool bootstrap() {
        for (const auto& entry : configuredServices) if (equal(entry.owner, owner_)) {
            if (count_ == capacity) return false;
            auto& unit = units_[count_++]; unit.program = entry.program;
            copy(unit.status.name, sizeof(unit.status.name), entry.name); copy(unit.status.owner, sizeof(unit.status.owner), owner_);
            copy(unit.ready, sizeof(unit.ready), entry.ready); unit.status.builtin = unit.status.enabled = unit.status.desired = 1;
            if (!start(unit)) return false;
        }
        return true;
    }
    bool load() {
        const uint32_t originalCount = count_;
        if (readConfiguration()) return true;
        count_ = originalCount; configSize_ = 0;
        return false;
    }
    bool readConfiguration() {
        os::File input; if (input.open("/etc/services", O_RDONLY)) return false;
        while (configSize_ + 1 < sizeof(config_)) {
            const auto count = input.read(config_ + configSize_, sizeof(config_) - configSize_ - 1);
            if (count < 0) return false;
            if (!count) break;
            configSize_ += count;
        }
        char extra; if (input.read(&extra, 1) != 0) return false;
        if (!os::ConfigurationText::valid(config_, configSize_)) return configError(0);
        os::Runtime::copyBytes(parse_, config_, configSize_ + 1);
        const uint32_t first = count_; uint32_t lineNumber = 0;
        for (char* line = parse_; *line;) {
            ++lineNumber; char* end = line; while (*end && *end != '\n') ++end;
            const bool last = !*end; *end = 0; if (end > line && end[-1] == '\r') end[-1] = 0;
            if (*line && *line != '#') {
                char* fields[10]{}; uint32_t columns = 1; fields[0] = line;
                for (char* p = line; *p; ++p) if (*p == '|') { if (columns == 10) return configError(lineNumber); *p = 0; fields[columns++] = p + 1; }
                if (columns != 10 || count_ == capacity || !validName(fields[0]) || bootName(fields[0]) || find(fields[0]) ||
                    !equal(fields[1], owner_) || (!equal(fields[2], "0") && !equal(fields[2], "1")) || !os::ConfigurationText::absolutePath(fields[3]) || !validArguments(fields[4]) ||
                    (!equal(fields[9], "0") && !equal(fields[9], "1"))) return configError(lineNumber);
                auto& unit = units_[count_++];
                if (!copy(unit.status.name, sizeof(unit.status.name), fields[0]) || !copy(unit.status.owner, sizeof(unit.status.owner), fields[1]) ||
                    !copy(unit.status.executable, sizeof(unit.status.executable), fields[3]) || !copy(unit.arguments, sizeof(unit.arguments), fields[4]) ||
                    !copy(unit.dependenciesText, sizeof(unit.dependenciesText), fields[5]) || !copy(unit.ready, sizeof(unit.ready), fields[7]) ||
                    !number(fields[6], unit.order) || !number(fields[8], unit.timeout) || !unit.timeout || unit.timeout > 30000 ||
                    (!equal(unit.ready, "-") && !validName(unit.ready))) return configError(lineNumber);
                unit.status.enabled = unit.status.desired = fields[2][0] == '1'; unit.restart = fields[9][0] == '1';
                unit.enabledOffset = fields[2] - parse_;
            }
            if (last) break;
            line = end + 1;
        }
        for (uint32_t i = first; i < count_; ++i) {
            if (!dependencies(units_[i], false)) return configError(0);
            for (uint32_t j = first; j < i; ++j) if (units_[j].order == units_[i].order) return configError(0);
        }
        for (uint32_t i = first; i < count_; ++i) for (uint32_t j = i + 1; j < count_; ++j)
            if (units_[j].order < units_[i].order) { Unit temporary = units_[i]; units_[i] = units_[j]; units_[j] = temporary; }
        for (uint32_t i = first; i < count_; ++i) if (units_[i].status.enabled) start(units_[i]);
        return true;
    }
    static bool configError(uint32_t line) {
        os::System::log("[SERVICE] invalid /etc/services, line "); char digits[12]{}; uint32_t used = 0;
        do { digits[used++] = '0' + line % 10; line /= 10; } while (line);
        while (used) { char value[2] = {digits[--used], 0}; os::System::log(value); }
        os::System::log("\n"); return false;
    }
    bool supervise() {
        endpoint_ = os::Endpoint::create();
        if (!endpoint_ || !os::ServiceRegistry::registerService(registry_, controlName_, endpoint_)) return false;
        constexpr uint64_t stack = 0x73000000;
        return os::mapMemory(stack, 65536, 1) == stack && os::createThread(thread, stack + 65536, this);
    }
};
}
