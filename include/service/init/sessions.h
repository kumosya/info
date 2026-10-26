#include "service/common/queries.h"
#include "service/common/records.h"
#include <os/task.h>
#pragma once
#include "service/console/client.h"
#include "service/common/runtime.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/init/session.h"
#include <os/console.h>
#include "service/console/display-session.h"
class SessionSupervisor {
    os::SessionConfiguration config_;
    struct Session { uint64_t handle = 0, started = 0, nextAttempt = 0; bool desired = true; uint32_t failures = 0; };
    Session sessions_[os::textTerminalCount + 1]{};
    uint64_t windowInstance_ = 0;
    uint64_t consoleInstance_ = 0;
    bool configured_ = false, graphicalReady_ = false;
    static uint64_t retryDelay(uint32_t index, uint32_t failures) {
        const uint32_t maximum = index < os::textTerminalCount ? 3 : 5;
        return (1ULL << (failures < maximum ? failures : maximum)) * 1000000000ULL;
    }
    static bool failed(Session& session, uint32_t index) {
        if (session.failures < 5) ++session.failures;
        session.nextAttempt = os::Sys().readMonotonicTime() + retryDelay(index, session.failures);
        return false;
    }
    bool start(uint32_t index) {
        auto& session = sessions_[index];
        if (index == os::textTerminalCount) {
            graphicalReady_ = false;
            if (!config_.graphical[0]) return failed(session,index);
            const char* arguments[] = {config_.graphicalName,"--os-terminal=7"};
            session.handle = os::detail::TaskControl().execute(config_.graphical,2,arguments);
            os::wire::TaskInfo controller{};
            const uint64_t window = os::detail::RegistryClient(os::detail::RegistryClient().endpoint()).lookup("guiw", 100);
            const bool bound = session.handle && window && !os::detail::TaskQuery().getInfo(session.handle,controller) &&
                os::GraphicalSession::bind(window,controller.id,controller.startedNanoseconds);
            if(!bound)os::Sys().log("[INIT] graphical controller binding failed\n");
            if (bound) {
                const uint64_t deadline=os::Sys().readMonotonicTime()+30000000000ULL;
                do {
                    os::GraphicalPhase phase;uint64_t owner=0;int code=0;
                    if(os::GraphicalSession::state(window,phase,owner) && owner==controller.id &&
                        (phase==os::GraphicalPhase::Greeter || phase==os::GraphicalPhase::Desktop)){graphicalReady_=true;break;}
                    const int state=os::detail::TaskControl().getState(session.handle,code);
                    if(state!=0 && state!=2)break;
                    os::sleep(25);
                }while(os::Sys().readMonotonicTime()<deadline);
            }
            if(window)os::closeHandle(window);
            if(!graphicalReady_){if(session.handle){os::detail::TaskControl().terminate(session.handle,1);os::closeHandle(session.handle);session.handle=0;}return failed(session,index);}
        }
        else {
            const uint64_t console = os::detail::RegistryClient(os::detail::RegistryClient().endpoint()).lookup(os::terminalServiceName, 1000);
            if (!console) return failed(session, index);
            os::IpcMessage request{}, reply{}; request.type = static_cast<uint64_t>(os::TerminalRequest::ResetSession);
            request.length = 1; request.data[0] = index;
            const int result = os::callService(console, request, reply); os::closeHandle(console);
            if (result || reply.data[0]) return failed(session, index);
            char terminal[] = "--os-terminal=1"; terminal[14] = '1' + index;
            const char* arguments[] = {config_.loginName, terminal};
            session.handle = os::detail::TaskControl().execute(config_.login, 2, arguments);
        }
        if (!session.handle) return failed(session, index);
        session.started = os::Sys().readMonotonicTime(); return true;
    }
    const char* terminalArguments_[1]{};
 public:
    bool init() {
        if (!config_.load()) { os::Sys().log("[INIT] invalid /etc/sessions\n"); return false; }
        configured_ = true;
        terminalArguments_[0] = config_.terminalName;
        bool ready = start(os::textTerminalCount);
        if (ready) os::Sys().log("[INIT] graphical greeter ready; awaiting console handoff\n");
        bool textReady = true;
        for (uint32_t i = 0; i < os::textTerminalCount; ++i) if (!start(i)) textReady = false;
        if (textReady) os::Sys().log("[INIT] tty1-tty6 login sessions started\n");
        return ready && textReady;
    }
    bool available(uint32_t terminal) const {
        if (terminal > os::graphicalTerminalId || !sessions_[terminal].handle || (terminal == os::graphicalTerminalId && !graphicalReady_)) return false;
        int code = 0;
        const int state = os::detail::TaskControl().getState(sessions_[terminal].handle, code);
        return state == 0 || state == 2;
    }
    uint32_t initialTerminal(uint32_t preferred) const {
        if (available(preferred)) return preferred;
        for (uint32_t i = 0; i < os::textTerminalCount; ++i) if (available(i)) return i;
        return available(os::graphicalTerminalId) ? os::graphicalTerminalId : UINT32_MAX;
    }
    void tick() {
        if (!configured_) return;
        const auto console = os::detail::RegistryClient(os::detail::RegistryClient().endpoint()).lookup("terminal", 1);
        const uint64_t consoleInstance = console ? os::Ipc::Handle(console, false).queryIdentity() : 0;
        if (console) os::closeHandle(console);
        if (consoleInstance && consoleInstance != consoleInstance_) for (uint32_t i = 0; i < os::textTerminalCount; ++i) {
            sessions_[i].failures = 0; sessions_[i].nextAttempt = 0;
        }
        consoleInstance_ = consoleInstance;
        const auto window = os::detail::RegistryClient(os::detail::RegistryClient().endpoint()).lookup("guiw", 1);
        const uint64_t instance = window ? os::Ipc::Handle(window, false).queryIdentity() : 0;
        if (window) os::closeHandle(window);
        if (instance && instance != windowInstance_) {
            auto& gui = sessions_[os::textTerminalCount];
            if (windowInstance_ && gui.handle) os::detail::TaskControl().terminate(gui.handle,1);
            gui.desired = true; gui.failures = 0; gui.nextAttempt = 0;
        }
        windowInstance_ = instance;
        for (uint32_t i = 0; i <= os::textTerminalCount; ++i) {
            auto& session = sessions_[i]; int code = 0;
            const int state = session.handle ? os::detail::TaskControl().getState(session.handle, code) : 0;
            if (session.handle && state != 0 && state != 2) {
                os::wire::TaskInfo previous{};
                if (!os::detail::TaskQuery().getInfo(session.handle, previous)) os::SessionCleanup::terminate(previous.sessionId);
                os::closeHandle(session.handle); session.handle = 0;
                if (code == 0 || os::Sys().readMonotonicTime() - session.started > 60000000000ULL) session.failures = 0;
                if (code != 0 && session.failures < 5) ++session.failures;
                session.nextAttempt = os::Sys().readMonotonicTime() + retryDelay(i, session.failures);
                if (i == os::textTerminalCount) { graphicalReady_=false;os::Console graphical; if(!graphical.open(os::graphicalTerminalId) && graphical.getActive()) { os::Console first; if(!first.open(0)) first.activate(); } }
            }
            if (!session.handle && session.desired && (i < os::textTerminalCount || session.failures < 5) && os::Sys().readMonotonicTime() >= session.nextAttempt &&
                (i < os::textTerminalCount || instance)) start(i);
        }
    }
};
