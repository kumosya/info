#pragma once
#include "os/session.h"
#include "os/terminal.h"
class SessionSupervisor {
    os::SessionConfiguration config_;
    struct Session { uint64_t handle = 0, started = 0, nextAttempt = 0; bool recovery = false, desired = true; uint32_t failures = 0; };
    Session sessions_[os::textTerminalCount + 1]{};
    uint64_t windowInstance_ = 0;
    uint64_t consoleInstance_ = 0;
    bool configured_ = false;
    static bool failed(Session& session) {
        ++session.failures;
        session.nextAttempt = os::Clock::getMonotonicTime() + (1ULL << (session.failures < 5 ? session.failures : 5)) * 1000000000ULL;
        return false;
    }
    bool start(uint32_t index) {
        auto& session = sessions_[index];
        if (index == os::textTerminalCount) session.handle = os::Task::execute(config_.terminal, 1, terminalArguments_);
        else {
            const uint64_t console = os::ServiceRegistry::lookupService(os::ServiceRegistry::serviceRegistry(), os::terminalServiceName, 1000);
            if (!console) return failed(session);
            os::IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(os::TerminalRequest::ResetSession);
            request.length = 1; request.data[0] = index;
            const int result = os::callService(console, request, reply); os::closeHandle(console);
            if (result || reply.data[0]) return failed(session);
            char terminal[] = "--os-terminal=1"; terminal[14] = '1' + index;
            const char* arguments[] = {session.recovery ? config_.recoveryName : config_.shellName, terminal};
            session.handle = os::Task::execute(session.recovery ? config_.recovery : config_.shell, 2, arguments);
        }
        if (!session.handle) return failed(session);
        session.started = os::Clock::getMonotonicTime(); return true;
    }
    const char* terminalArguments_[1]{};
 public:
    bool initialize() {
        if (!config_.load()) { os::System::log("[INIT] invalid /etc/sessions\n"); return false; }
        configured_ = true;
        terminalArguments_[0] = config_.terminalName;
        bool ready = start(os::textTerminalCount);
        if (ready && os::Terminal::switchTo(os::graphicalTerminalId)) os::System::log("[INIT] GUI terminal started through PTY\n");
        bool textReady = true;
        for (uint32_t i = 0; i < os::textTerminalCount; ++i) if (!start(i)) textReady = false;
        if (textReady) os::System::log("[INIT] tty1-tty6 shells started\n");
        return ready && textReady;
    }
    void tick() {
        if (!configured_) return;
        const auto console = os::ServiceRegistry::lookupService(os::ServiceRegistry::serviceRegistry(), "terminal", 1);
        const uint64_t consoleInstance = console ? os::Handle::identity(console) : 0;
        if (console) os::closeHandle(console);
        if (consoleInstance && consoleInstance != consoleInstance_) for (uint32_t i = 0; i < os::textTerminalCount; ++i) {
            sessions_[i].failures = 0; sessions_[i].nextAttempt = 0;
        }
        consoleInstance_ = consoleInstance;
        const auto window = os::ServiceRegistry::lookupService(os::ServiceRegistry::serviceRegistry(), "guiw", 1);
        const uint64_t instance = window ? os::Handle::identity(window) : 0;
        if (window) os::closeHandle(window);
        if (instance && instance != windowInstance_) {
            auto& gui = sessions_[os::textTerminalCount]; gui.desired = true; gui.failures = 0; gui.nextAttempt = 0;
        }
        windowInstance_ = instance;
        for (uint32_t i = 0; i <= os::textTerminalCount; ++i) {
            auto& session = sessions_[i]; int code = 0;
            const int state = session.handle ? os::Task::getState(session.handle, code) : 0;
            if (session.handle && state != 0 && state != 2) {
                os::TaskInfo previous{};
                if (i < os::textTerminalCount && !os::Task::getInfo(session.handle, previous)) os::SessionCleanup::terminate(previous.sessionId);
                os::closeHandle(session.handle); session.handle = 0; session.recovery = true;
                if (os::Clock::getMonotonicTime() - session.started > 60000000000ULL) session.failures = 0;
                ++session.failures;
                session.nextAttempt = os::Clock::getMonotonicTime() + (1ULL << (session.failures < 5 ? session.failures : 5)) * 1000000000ULL;
                if (i == os::textTerminalCount && code == 0) session.desired = false;
            }
            if (!session.handle && session.desired && session.failures < 5 && os::Clock::getMonotonicTime() >= session.nextAttempt &&
                (i < os::textTerminalCount || instance)) start(i);
        }
    }
};
