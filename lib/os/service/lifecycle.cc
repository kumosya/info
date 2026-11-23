/* 服务对象的成员函数实现。 */
#include "service/common/lifecycle.h"

namespace services {

#include "startup.inc"

    /* 检查服务名称的长度与可用字符。 */
    bool Lifecycle::validName(const char* value) {
        size_t length = 0;
        for (; value[length]; ++length) {
            if (length >= 23 ||
                !((value[length] >= 'a' && value[length] <= 'z') ||
                  (value[length] >= '0' && value[length] <= '9') ||
                  value[length] == '.' || value[length] == '-' ||
                  value[length] == '_')) {
                return false;
            }
        }

        return length != 0;
    }

    /* 解析配置文件中的非负整数。 */
    bool Lifecycle::number(const char* text, uint32_t& result) {
        if (!*text) {
            return false;
        }

        uint64_t value = 0;
        for (; *text; ++text) {
            if (*text < '0' || *text > '9') {
                return false;
            }

            value = value * 10 + *text - '0';
            if (value > 1000000) {
                return false;
            }
        }

        result = value;
        return true;
    }

    /* 按目标缓冲区容量复制配置字段。 */
    bool Lifecycle::copy(char* output, size_t capacity, const char* input) {
        if (os::Runtime::stringLength(input) >= capacity) {
            return false;
        }

        os::Runtime::copyString(output, input, capacity);
        return true;
    }

    /* 检查逗号分隔的启动参数。 */
    bool Lifecycle::validArguments(const char* text) {
        if (equal(text, "-")) {
            return true;
        }

        unsigned count = 1;
        if (!*text || *text == ',') {
            return false;
        }

        for (; *text; ++text) {
            if (*text == ',' && (!text[1] || text[1] == ',' || ++count > 8)) {
                return false;
            }
        }

        return true;
    }

    /* 等待服务在注册中心登记就绪端点。 */
    bool Lifecycle::waitReady(Unit& unit) {
        const uint64_t deadline = os::Sys().readMonotonicTime() +
                                  static_cast<uint64_t>(unit.timeout) * 1000000;
        do {
            os::IpcMessage request{}, reply{};
            request.type =
                static_cast<uint64_t>(services::RegistryRequest::Lookup);
            request.length = 3;
            copy(reinterpret_cast<char*>(request.data), 24, unit.ready);
            if (!os::callService(registry_, request, reply) && !reply.data[0] &&
                reply.handleCount == 1) {
                const bool owned =
                    reply.length >= 3 && reply.data[2] == unit.status.pid;
                os::closeHandle(reply.handles[0]);
                return owned;
            }

            int code = 0;
            if (os::detail::TaskControl().getState(unit.task, code)) {
                return false;
            }

            os::sleep(1);
        } while (os::Sys().readMonotonicTime() < deadline);
        return false;
    }

    /* 按名称查找受管理的服务。 */
    Lifecycle::Unit* Lifecycle::find(const char* name) {
        for (uint32_t i = 0; i < count_; ++i) {
            if (equal(name, units_[i].status.name)) {
                return &units_[i];
            }
        }

        return nullptr;
    }

    /* 识别系统启动阶段的服务名称。 */
    bool Lifecycle::bootName(const char* name) {
        if (equal(name, "init")) {
            return true;
        }

        for (const ConfiguredService& unit : configuredServices) {
            if (equal(name, unit.name)) {
                return true;
            }
        }

        return false;
    }

    /* 检查服务所依赖的其他服务状态。 */
    bool Lifecycle::dependencies(Unit& unit, bool runtime) {
        if (equal(unit.dependenciesText, "-")) {
            return true;
        }

        char names[128];
        copy(names, sizeof(names), unit.dependenciesText);
        char* name = names;
        for (char* p = names;; ++p) {
            if (*p == ',' || !*p) {
                const bool end = !*p;
                *p = 0;
                if (!validName(name) || equal(name, unit.status.name)) {
                    return false;
                }

                Unit* dependency = find(name);
                if (!dependency && !bootName(name)) {
                    return false;
                }

                if (dependency && !dependency->status.builtin &&
                    (dependency->order >= unit.order ||
                     (unit.status.enabled && !dependency->status.enabled))) {
                    return false;
                }

                if (runtime) {
                    const uint64_t endpoint =
                        os::detail::RegistryClient(registry_).lookup(
                            name, unit.timeout);
                    if (!endpoint) {
                        return false;
                    }

                    os::closeHandle(endpoint);
                }

                if (end) {
                    return true;
                }

                name = p + 1;
            }
        }
    }

    /* 准备启动参数并运行一个服务。 */
    bool Lifecycle::start(Unit& unit) {
        if (unit.self) {
            return true;
        }

        if (unit.task) {
            int code = 0;
            const int state =
                os::detail::TaskControl().getState(unit.task, code);
            if (state == 0 || state == 2) {
                return true;
            }

            os::closeHandle(unit.task);
            unit.task = 0;
        }

        if (!unit.status.builtin && !dependencies(unit, true)) {
            return failed(unit, -1);
        }

        unit.status.state = os::Sys::Status::State::Starting;
        if (unit.status.builtin) {
            unit.task =
                os::detail::TaskControl().spawnProgram(unit.program, registry_);
        } else {
            const char* arguments[9] = {unit.status.name};
            uint32_t count = 1;
            char storage[128];
            copy(storage, sizeof(storage), unit.arguments);
            if (!equal(storage, "-")) {
                char* begin = storage;
                for (char* p = storage;; ++p) {
                    if (*p == ',' || !*p) {
                        const bool end = !*p;
                        *p = 0;
                        if (!*begin || count == 9) {
                            return failed(unit, -1);
                        }

                        arguments[count++] = begin;
                        if (end) {
                            break;
                        }

                        begin = p + 1;
                    }
                }
            }

            unit.task = os::detail::TaskControl().executeService(
                unit.status.executable, count, arguments);
        }

        if (!unit.task || static_cast<int64_t>(unit.task) < 0) {
            unit.task = 0;
            return failed(unit, -1);
        }

        services::wire::TaskInfo info{};
        if (os::detail::TaskQuery().getInfo(unit.task, info)) {
            terminate(unit, true);
            return failed(unit, -1);
        }

        unit.status.pid = info.id;
        if (!equal(unit.ready, "-")) {
            if (!waitReady(unit)) {
                terminate(unit, true);
                return failed(unit, -1);
            }
        }

        unit.status.state = os::Sys::Status::State::Running;
        unit.runningSince = os::Sys().readMonotonicTime();
        if (observer_) {
            observer_(observerContext_, unit.status);
        }

        return true;
    }

    /* 记录失败次数并安排下次启动。 */
    bool Lifecycle::failed(Unit& unit, int code) {
        unit.status.state = os::Sys::Status::State::Failed;
        unit.status.exitCode = code;
        if (observer_) {
            observer_(observerContext_, unit.status);
        }

        ++unit.failures;
        unit.nextAttempt =
            os::Sys().readMonotonicTime() +
            (1ULL << (unit.failures < 5 ? unit.failures : 5)) * 1000000000;
        os::Sys().log("[SERVICE] startup/exit failure: ");
        os::Sys().log(unit.status.name);
        os::Sys().log("\n");
        if (code < 0) {
            const char* reason = code == -11  ? "SIGSEGV"
                                 : code == -4 ? "SIGILL"
                                 : code == -8 ? "SIGFPE"
                                 : code == -7 ? "SIGBUS"
                                 : code == -5 ? "SIGTRAP"
                                 : code == -6 ? "SIGABRT"
                                              : "fatal signal";
            os::Sys().log("[SERVICE] exit reason: ");
            os::Sys().log(reason);
            os::Sys().log("\n");
        }

        return false;
    }

    /* 查询服务是否使用检查点恢复。 */
    bool Lifecycle::restoresState(Unit& unit) {
        if (unit.status.builtin) {
            return unit.program == services::ProgramId::MemoryManager ||
                   unit.program == services::ProgramId::PipeServer;
        }

        if (!unit.task) {
            return equal(unit.status.executable, "/sbin/sys/users.elf");
        }

        services::wire::TaskInfo info{};
        return !os::detail::TaskQuery().getInfo(unit.task, info) &&
               info.managedService &&
               info.serviceRole ==
                   static_cast<uint32_t>(services::ServiceRole::Users);
    }

    /* 释放已经退出的服务句柄。 */
    void Lifecycle::release(Unit& unit) {
        int status = 0;
        if (unit.status.pid) {
            os::detail::TaskControl().waitChild(
                static_cast<int64_t>(unit.status.pid), status, 1);
        }

        os::closeHandle(unit.task);
        unit.task = 0;
        unit.status.pid = 0;
    }

    /* 通知服务停止并处理退出等待。 */
    bool Lifecycle::terminate(Unit& unit, bool force) {
        if (!unit.task) {
            return true;
        }

        os::IpcMessage request{}, reply{};
        request.length = 4;
        request.data[0] = unit.task;
        request.type = static_cast<uint64_t>(
            force ? services::TaskOperation::ControlTerminate
                  : services::TaskOperation::ControlSignal);
        request.data[1] = force ? static_cast<uint64_t>(-15LL) : 15;
        int64_t result =
            os::detail::RequestChannel(services::CoreServiceKind::Task)
                .result(request, reply);
        int code = 0;
        if (!force && result >= 0) {
            const uint64_t deadline =
                os::Sys().readMonotonicTime() + 2000000000;
            while (!os::detail::TaskControl().getState(unit.task, code) &&
                   os::Sys().readMonotonicTime() < deadline) {
                os::sleep(10);
            }

            if (!os::detail::TaskControl().getState(unit.task, code)) {
                return terminate(unit, true);
            }
        } else if (result < 0 &&
                   !os::detail::TaskControl().getState(unit.task, code)) {
            return false;
        }

        release(unit);
        unit.status.state = os::Sys::Status::State::Stopped;
        unit.status.exitCode = code;
        return true;
    }

    /* 检查服务退出状态并执行到期重启。 */
    void Lifecycle::tick() {
        for (uint32_t i = 0; i < count_; ++i) {
            Unit& unit = units_[i];
            if (unit.self) {
                continue;
            }

            int code = 0;
            const int state =
                unit.task ? os::detail::TaskControl().getState(unit.task, code)
                          : 0;
            if (unit.task && state != 0 && state != 2) {
                release(unit);
                if (os::Sys().readMonotonicTime() - unit.runningSince >
                    60000000000ULL) {
                    unit.failures = 0;
                }

                failed(unit, code);
            }

            if (!unit.task && unit.status.desired && unit.restart &&
                unit.failures < 5 &&
                os::Sys().readMonotonicTime() >= unit.nextAttempt) {
                ++unit.status.restarts;
                start(unit);
            }
        }

        int status = 0;
        while (os::detail::TaskControl().waitChild(-1, status, 1) > 0) {
        }
    }

    /* 保存服务启用状态的修改。 */
    bool Lifecycle::persist(Unit& unit, bool enabled) {
        if (!configSize_ || unit.status.builtin) {
            return false;
        }

        os::File input;
        if (input.open("/etc/services", O_RDONLY)) {
            return false;
        }

        char current[8192];
        size_t used = 0;
        while (used < sizeof(current)) {
            const ssize_t count =
                input.read(current + used, sizeof(current) - used);
            if (count < 0) {
                return false;
            }

            if (!count) {
                break;
            }

            used += count;
        }

        if (used != configSize_) {
            return false;
        }

        for (size_t i = 0; i < used; ++i) {
            if (current[i] != config_[i]) {
                return false;
            }
        }

        current[unit.enabledOffset] = enabled ? '1' : '0';
        os::File output;
        if (output.open("/etc/.services.tmp", O_CREAT | O_WRONLY | O_TRUNC,
                        0644)) {
            return false;
        }

        used = 0;
        while (used < configSize_) {
            const ssize_t count =
                output.write(current + used, configSize_ - used);
            if (count <= 0) {
                return false;
            }

            used += count;
        }

        if (output.flush() || output.close() ||
            os::Fs().rename("/etc/.services.tmp", "/etc/services")) {
            return false;
        }

        config_[unit.enabledOffset] = current[unit.enabledOffset];
        unit.status.enabled = enabled;
        return true;
    }

    /* 处理服务启停和状态查询请求。 */
    int Lifecycle::control(services::ServiceOperation operation,
                           const char* name, uint64_t index,
                           services::wire::ServiceStatus& status) {
        Unit* unit = operation == services::ServiceOperation::List
                         ? index < count_ ? &units_[index] : nullptr
                         : find(name);
        if (!unit) {
            if (!equal(owner_, "init")) {
                return static_cast<int>(os::ErrorCode::NotFound);
            }

            uint64_t cursor = index >= count_ ? index - count_ : 0;
            for (const char* owner : {"task", "device"}) {
                uint64_t children = 0;
                bool belongs = false;
                for (const ConfiguredService& entry : configuredServices) {
                    if (equal(entry.owner, owner)) {
                        ++children;
                        belongs = belongs || equal(entry.name, name);
                    }
                }

                char manager[24] = "control.";
                os::Runtime::copyString(manager + 8, owner,
                                        sizeof(manager) - 8);
                if (operation == services::ServiceOperation::List &&
                    cursor < children) {
                    return os::detail::ManagerClient(manager).call(
                        operation, nullptr, cursor, status);
                }

                if (operation != services::ServiceOperation::List && belongs) {
                    return os::detail::ManagerClient(manager).call(
                        operation, name, 0, status);
                }

                cursor = cursor >= children ? cursor - children : 0;
            }

            return static_cast<int>(os::ErrorCode::NotFound);
        }

        switch (operation) {
            case services::ServiceOperation::List:
            case services::ServiceOperation::Status:
                break;
            case services::ServiceOperation::Enable:
            case services::ServiceOperation::Disable:
                if (!persist(*unit,
                             operation == services::ServiceOperation::Enable)) {
                    return static_cast<int>(os::ErrorCode::PermissionDenied);
                }

                break;
            case services::ServiceOperation::Start:
                unit->status.desired = 1;
                unit->failures = 0;
                if (!start(*unit)) {
                    return static_cast<int>(os::ErrorCode::IoError);
                }

                break;
            case services::ServiceOperation::Stop:
            case services::ServiceOperation::Restart: {
                const bool restoring =
                    operation == services::ServiceOperation::Restart &&
                    restoresState(*unit);
                if (unit->status.builtin && !restoring) {
                    return static_cast<int>(os::ErrorCode::PermissionDenied);
                }

                for (uint32_t i = 0; !restoring && i < count_; ++i) {
                    if (&units_[i] != unit && units_[i].status.desired) {
                        char dependencies[128];
                        copy(dependencies, sizeof(dependencies),
                             units_[i].dependenciesText);
                        char* token = dependencies;
                        for (char* p = token;; ++p) {
                            if (*p == ',' || !*p) {
                                bool end = !*p;
                                *p = 0;
                                if (equal(token, unit->status.name)) {
                                    status = unit->status;
                                    copy(status.blockedBy,
                                         sizeof(status.blockedBy),
                                         units_[i].status.name);
                                    return static_cast<int>(
                                        os::ErrorCode::Busy);
                                }

                                if (end) {
                                    break;
                                }

                                token = p + 1;
                            }
                        }
                    }
                }

                unit->status.desired = 0;
                if (!terminate(*unit)) {
                    return static_cast<int>(os::ErrorCode::PermissionDenied);
                }

                if (operation == services::ServiceOperation::Restart) {
                    unit->status.desired = 1;
                    unit->failures = 0;
                    ++unit->status.restarts;
                    if (!start(*unit)) {
                        return static_cast<int>(os::ErrorCode::IoError);
                    }
                }

                break;
            }

            default:
                return static_cast<int>(os::ErrorCode::InvalidArgument);
        }

        status = unit->status;
        return 0;
    }

    /* 接收控制消息并定期执行服务监护。 */
    void Lifecycle::run() {
        while (true) {
            tick();
            os::IpcMessage request{}, reply{};
            const uint64_t deadline = os::Sys().readMonotonicTime() + 250000000;
            if (os::receiveMessage(endpoint_, request, deadline)) {
                continue;
            }

            reply.length = 1;
            reply.data[0] =
                static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            if (request.type == services::serviceControlMessage &&
                request.length == 5 && !request.handleCount &&
                reinterpret_cast<const char*>(&request.data[2])[23] == 0) {
                services::wire::TaskInfo caller{};
                const services::ServiceOperation operation =
                    static_cast<services::ServiceOperation>(request.data[0]);
                if (os::detail::TaskQuery().query(request.senderPid, caller) ||
                    (operation != services::ServiceOperation::List &&
                     operation != services::ServiceOperation::Status &&
                     caller.effectiveUid)) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::PermissionDenied);
                } else {
                    services::wire::ServiceStatus status{};
                    reply.data[0] =
                        control(operation,
                                reinterpret_cast<const char*>(&request.data[2]),
                                request.data[1], status);
                    if (!reply.data[0] ||
                        static_cast<int64_t>(reply.data[0]) ==
                            static_cast<int>(os::ErrorCode::Busy)) {
                        os::Runtime::copyBytes(&reply.data[1], &status,
                                               sizeof(status));
                        reply.length = 1 + (sizeof(status) + 7) / 8;
                    }
                }
            }

            for (uint32_t i = 0; i < request.handleCount; ++i) {
                os::closeHandle(request.handles[i]);
            }

            os::replyMessage(endpoint_, request.replyToken, reply);
        }
    }

    /* 绑定服务配置、注册中心和观察回调。 */
    bool Lifecycle::init(const char* owner, uint64_t registry) {
        registry_ = registry;
        copy(owner_, sizeof(owner_), owner);
        copy(controlName_, sizeof(controlName_), "control.");
        copy(controlName_ + 8, sizeof(controlName_) - 8, owner);
        if (equal(owner, "init")) {
            Unit& unit = units_[count_++];
            unit.self = true;
            copy(unit.status.name, sizeof(unit.status.name), "init");
            copy(unit.status.owner, sizeof(unit.status.owner), "kernel");
            unit.status.builtin = unit.status.enabled = unit.status.desired = 1;
            unit.status.state = os::Sys::Status::State::Running;
            unit.status.pid = os::detail::TaskControl().getId();
        }

        return true;
    }

    /* 按启动顺序运行基础服务。 */
    bool Lifecycle::bootstrap() {
        for (const ConfiguredService& entry : configuredServices) {
            if (equal(entry.owner, owner_)) {
                if (count_ == capacity) {
                    return false;
                }

                Unit& unit = units_[count_++];
                unit.program = entry.program;
                copy(unit.status.name, sizeof(unit.status.name), entry.name);
                copy(unit.status.owner, sizeof(unit.status.owner), owner_);
                copy(unit.ready, sizeof(unit.ready), entry.ready);
                unit.status.builtin = unit.status.enabled =
                    unit.status.desired = 1;
                if (!start(unit)) {
                    return false;
                }
            }
        }

        return true;
    }

    /* 读取并解析服务配置。 */
    bool Lifecycle::load() {
        const uint32_t originalCount = count_;
        if (readConfiguration()) {
            return true;
        }

        count_ = originalCount;
        configSize_ = 0;
        return false;
    }

    /* 从配置文件读取服务记录。 */
    bool Lifecycle::readConfiguration() {
        os::File input;
        if (input.open("/etc/services", O_RDONLY)) {
            return false;
        }

        while (configSize_ + 1 < sizeof(config_)) {
            const ssize_t count = input.read(config_ + configSize_,
                                             sizeof(config_) - configSize_ - 1);
            if (count < 0) {
                return false;
            }

            if (!count) {
                break;
            }

            configSize_ += count;
        }

        char extra;
        if (input.read(&extra, 1) != 0) {
            return false;
        }

        if (!os::ConfigurationText(config_, configSize_).valid()) {
            return configError(0);
        }

        os::Runtime::copyBytes(parse_, config_, configSize_ + 1);
        const uint32_t first = count_;
        uint32_t lineNumber = 0;
        for (char* line = parse_; *line;) {
            ++lineNumber;
            char* end = line;
            while (*end && *end != '\n') {
                ++end;
            }

            const bool last = !*end;
            *end = 0;
            if (end > line && end[-1] == '\r') {
                end[-1] = 0;
            }

            if (*line && *line != '#') {
                char* fields[10]{};
                uint32_t columns = 1;
                fields[0] = line;
                for (char* p = line; *p; ++p) {
                    if (*p == '|') {
                        if (columns == 10) {
                            return configError(lineNumber);
                        }

                        *p = 0;
                        fields[columns++] = p + 1;
                    }
                }

                if (columns != 10 || count_ == capacity ||
                    !validName(fields[0]) || bootName(fields[0]) ||
                    find(fields[0]) || !equal(fields[1], owner_) ||
                    (!equal(fields[2], "0") && !equal(fields[2], "1")) ||
                    !os::ConfigurationText(fields[3]).absolutePath() ||
                    !validArguments(fields[4]) ||
                    (!equal(fields[9], "0") && !equal(fields[9], "1"))) {
                    return configError(lineNumber);
                }

                Unit& unit = units_[count_++];
                if (!copy(unit.status.name, sizeof(unit.status.name),
                          fields[0]) ||
                    !copy(unit.status.owner, sizeof(unit.status.owner),
                          fields[1]) ||
                    !copy(unit.status.executable,
                          sizeof(unit.status.executable), fields[3]) ||
                    !copy(unit.arguments, sizeof(unit.arguments), fields[4]) ||
                    !copy(unit.dependenciesText, sizeof(unit.dependenciesText),
                          fields[5]) ||
                    !copy(unit.ready, sizeof(unit.ready), fields[7]) ||
                    !number(fields[6], unit.order) ||
                    !number(fields[8], unit.timeout) || !unit.timeout ||
                    unit.timeout > 30000 ||
                    (!equal(unit.ready, "-") && !validName(unit.ready))) {
                    return configError(lineNumber);
                }

                unit.status.enabled = unit.status.desired = fields[2][0] == '1';
                unit.restart = fields[9][0] == '1';
                unit.enabledOffset = fields[2] - parse_;
            }

            if (last) {
                break;
            }

            line = end + 1;
        }

        for (uint32_t i = first; i < count_; ++i) {
            if (!dependencies(units_[i], false)) {
                return configError(0);
            }

            for (uint32_t j = first; j < i; ++j) {
                if (units_[j].order == units_[i].order) {
                    return configError(0);
                }
            }
        }

        for (uint32_t i = first; i < count_; ++i) {
            for (uint32_t j = i + 1; j < count_; ++j) {
                if (units_[j].order < units_[i].order) {
                    Unit temporary = units_[i];
                    units_[i] = units_[j];
                    units_[j] = temporary;
                }
            }
        }

        for (uint32_t i = first; i < count_; ++i) {
            if (units_[i].status.enabled) {
                start(units_[i]);
            }
        }

        return true;
    }

    /* 报告配置文件中的错误行。 */
    bool Lifecycle::configError(uint32_t line) {
        os::Sys().log("[SERVICE] invalid /etc/services, line ");
        char digits[12]{};
        uint32_t used = 0;
        do {
            digits[used++] = '0' + line % 10;
            line /= 10;
        } while (line);
        while (used) {
            char value[2] = {digits[--used], 0};
            os::Sys().log(value);
        }

        os::Sys().log("\n");
        return false;
    }

    /* 创建服务监护线程。 */
    bool Lifecycle::supervise() {
        endpoint_ = os::detail::EndpointControl().create();
        if (!endpoint_ || !os::detail::RegistryClient(registry_).registerName(
                              controlName_, endpoint_)) {
            return false;
        }

        constexpr uint64_t stack = 0x73000000;
        return os::mapMemory(stack, 65536, 1) == stack &&
               os::createThread(thread, stack + 65536, this);
    }

}  // namespace services
