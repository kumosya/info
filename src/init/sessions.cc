/* 服务对象的成员函数实现。 */
#include "service/init/init.h"

namespace init {

    /* 根据失败次数计算会话重启间隔。 */
    uint64_t SessionSupervisor::retryDelay(uint32_t index, uint32_t failures) {
        const uint32_t maximum = index < os::textTerminalCount ? 3 : 5;
        return (1ULL << (failures < maximum ? failures : maximum)) *
               1000000000ULL;
    }

    /* 记录会话启动失败并安排重试。 */
    bool SessionSupervisor::failed(Session& session, uint32_t index) {
        if (session.failures < 5) {
            ++session.failures;
        }

        session.nextAttempt =
            os::Sys().readMonotonicTime() + retryDelay(index, session.failures);
        return false;
    }

    /* 按终端类型启动登录或图形会话。 */
    bool SessionSupervisor::start(uint32_t index) {
        Session& session = sessions_[index];
        if (index == os::textTerminalCount) {
            graphicalReady_ = false;
            if (!config_.getGraphical()[0]) {
                return failed(session, index);
            }

            const char* arguments[] = {config_.getGraphicalName(),
                                       "--os-terminal=7"};
            session.handle = os::detail::TaskControl().execute(
                config_.getGraphical(), 2, arguments);
            services::wire::TaskInfo controller{};
            const uint64_t window = os::detail::RegistryClient(
                                        os::detail::RegistryClient().endpoint())
                                        .lookup("guiw", 100);
            const bool bound =
                session.handle && window &&
                !os::detail::TaskQuery().getInfo(session.handle, controller) &&
                os::GraphicalSession(window).bind(
                    controller.id, controller.startedNanoseconds);
            if (!bound) {
                os::Sys().log("[INIT] graphical controller binding failed\n");
            }

            if (bound) {
                const uint64_t deadline =
                    os::Sys().readMonotonicTime() + 30000000000ULL;
                do {
                    os::GraphicalPhase phase;
                    uint64_t owner = 0;
                    int code = 0;
                    if (os::GraphicalSession(window).state(phase, owner) &&
                        owner == controller.id &&
                        (phase == os::GraphicalPhase::Greeter ||
                         phase == os::GraphicalPhase::Desktop)) {
                        graphicalReady_ = true;
                        break;
                    }

                    const int state = os::detail::TaskControl().getState(
                        session.handle, code);
                    if (state != 0 && state != 2) {
                        break;
                    }

                    os::sleep(25);
                } while (os::Sys().readMonotonicTime() < deadline);
            }

            if (window) {
                os::closeHandle(window);
            }

            if (!graphicalReady_) {
                if (session.handle) {
                    os::detail::TaskControl().terminate(session.handle, 1);
                    os::closeHandle(session.handle);
                    session.handle = 0;
                }

                return failed(session, index);
            }
        } else {
            const uint64_t console =
                os::detail::RegistryClient(
                    os::detail::RegistryClient().endpoint())
                    .lookup(services::terminalServiceName, 1000);
            if (!console) {
                return failed(session, index);
            }

            os::IpcMessage request{}, reply{};
            request.type =
                static_cast<uint64_t>(services::TerminalRequest::ResetSession);
            request.length = 1;
            request.data[0] = index;
            const int result = os::callService(console, request, reply);
            os::closeHandle(console);
            if (result || reply.data[0]) {
                return failed(session, index);
            }

            char terminal[] = "--os-terminal=1";
            terminal[14] = '1' + index;
            const char* arguments[] = {config_.getLoginName(), terminal};
            session.handle = os::detail::TaskControl().execute(
                config_.getLogin(), 2, arguments);
        }

        if (!session.handle) {
            return failed(session, index);
        }

        session.started = os::Sys().readMonotonicTime();
        return true;
    }

    /* 加载会话配置并启动初始会话。 */
    bool SessionSupervisor::init() {
        if (!config_.load()) {
            os::Sys().log("[INIT] invalid /etc/sessions\n");
            return false;
        }

        configured_ = true;
        terminalArguments_[0] = config_.getTerminalName();
        bool ready = start(os::textTerminalCount);
        if (ready) {
            os::Sys().log(
                "[INIT] graphical greeter ready; awaiting console "
                "handoff\n");
        }

        bool textReady = true;
        for (uint32_t i = 0; i < os::textTerminalCount; ++i) {
            if (!start(i)) {
                textReady = false;
            }
        }

        if (textReady) {
            os::Sys().log("[INIT] tty1-tty6 login sessions started\n");
        }

        return ready && textReady;
    }

    /* 查询终端会话是否已经可用。 */
    bool SessionSupervisor::available(uint32_t terminal) const {
        if (terminal > os::graphicalTerminalId || !sessions_[terminal].handle ||
            (terminal == os::graphicalTerminalId && !graphicalReady_)) {
            return false;
        }

        int code = 0;
        const int state = os::detail::TaskControl().getState(
            sessions_[terminal].handle, code);
        return state == 0 || state == 2;
    }

    /* 选择启动后显示的终端。 */
    uint32_t SessionSupervisor::initialTerminal(uint32_t preferred) const {
        if (available(preferred)) {
            return preferred;
        }

        for (uint32_t i = 0; i < os::textTerminalCount; ++i) {
            if (available(i)) {
                return i;
            }
        }

        return available(os::graphicalTerminalId) ? os::graphicalTerminalId
                                                  : UINT32_MAX;
    }

    /* 检查会话退出、挂起和用户切换状态。 */
    void SessionSupervisor::tick() {
        if (!configured_) {
            return;
        }

        const uint64_t console =
            os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
                .lookup("terminal", 1);
        const uint64_t consoleInstance =
            console ? os::Ipc::Handle(console, false).queryIdentity() : 0;
        if (console) {
            os::closeHandle(console);
        }

        if (consoleInstance && consoleInstance != consoleInstance_) {
            for (uint32_t i = 0; i < os::textTerminalCount; ++i) {
                sessions_[i].failures = 0;
                sessions_[i].nextAttempt = 0;
            }
        }

        consoleInstance_ = consoleInstance;
        const uint64_t window =
            os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
                .lookup("guiw", 1);
        const uint64_t instance =
            window ? os::Ipc::Handle(window, false).queryIdentity() : 0;
        os::GraphicalPhase phase = os::GraphicalPhase::Starting;
        uint64_t controller = 0;
        const bool known =
            window && os::GraphicalSession(window).state(phase, controller);
        if (known && instance == windowInstance_) {
            Session& active = sessions_[os::textTerminalCount];
            if (phase == os::GraphicalPhase::Suspended && active.handle) {
                services::wire::TaskInfo owner{};
                if (!os::detail::TaskQuery().getInfo(active.handle, owner) &&
                    owner.id == controller) {
                    for (Session& saved : parked_) {
                        if (!saved.handle) {
                            saved = active;
                            active = {};
                            break;
                        }
                    }
                }
            } else if (phase == os::GraphicalPhase::Desktop) {
                // 窗口服务只在重新认证后恢复旧 controller；init
                // 接回原进程句柄。
                for (Session& saved : parked_) {
                    if (saved.handle) {
                        services::wire::TaskInfo owner{};
                        if (!os::detail::TaskQuery().getInfo(saved.handle,
                                                             owner) &&
                            owner.id == controller) {
                            if (active.handle) {
                                services::wire::TaskInfo temporary{};
                                if (!os::detail::TaskQuery().getInfo(
                                        active.handle, temporary)) {
                                    os::SessionCleanup(temporary.sessionId)
                                        .terminate();
                                }

                                os::detail::TaskControl().terminate(
                                    active.handle, 0);
                                os::closeHandle(active.handle);
                            }

                            active = saved;
                            saved = {};
                            graphicalReady_ = true;
                            break;
                        }
                    }
                }
            }
        }

        // 清理已经退出的保留会话，避免槽位长期占用。
        for (Session& saved : parked_) {
            if (saved.handle) {
                int code = 0;
                const int state =
                    os::detail::TaskControl().getState(saved.handle, code);
                if ((instance && windowInstance_ &&
                     instance != windowInstance_) ||
                    (state != 0 && state != 2)) {
                    services::wire::TaskInfo owner{};
                    if (!os::detail::TaskQuery().getInfo(saved.handle, owner)) {
                        os::SessionCleanup(owner.sessionId).terminate();
                    }

                    os::detail::TaskControl().terminate(saved.handle, 1);
                    os::closeHandle(saved.handle);
                    saved = {};
                }
            }
        }

        if (window) {
            os::closeHandle(window);
        }

        if (instance && instance != windowInstance_) {
            Session& gui = sessions_[os::textTerminalCount];
            if (windowInstance_ && gui.handle) {
                os::detail::TaskControl().terminate(gui.handle, 1);
            }

            gui.desired = true;
            gui.failures = 0;
            gui.nextAttempt = 0;
        }

        windowInstance_ = instance;
        for (uint32_t i = 0; i <= os::textTerminalCount; ++i) {
            Session& session = sessions_[i];
            int code = 0;
            const int state =
                session.handle
                    ? os::detail::TaskControl().getState(session.handle, code)
                    : 0;
            if (session.handle && state != 0 && state != 2) {
                services::wire::TaskInfo previous{};
                if (!os::detail::TaskQuery().getInfo(session.handle,
                                                     previous)) {
                    os::SessionCleanup(previous.sessionId).terminate();
                }

                os::closeHandle(session.handle);
                session.handle = 0;
                if (code == 0 ||
                    os::Sys().readMonotonicTime() - session.started >
                        60000000000ULL) {
                    session.failures = 0;
                }

                if (code != 0 && session.failures < 5) {
                    ++session.failures;
                }

                session.nextAttempt = os::Sys().readMonotonicTime() +
                                      retryDelay(i, session.failures);
                if (i == os::textTerminalCount) {
                    graphicalReady_ = false;
                    os::Console graphical;
                    // 正常注销保留图形输出，下一次 greeter
                    // 接管；异常退出才退回文字控制台。
                    if (code != 0 && !graphical.open(os::graphicalTerminalId) &&
                        graphical.getActive()) {
                        os::Console first;
                        if (!first.open(0)) {
                            first.activate();
                        }
                    }
                }
            }

            if (!session.handle && session.desired &&
                (i < os::textTerminalCount || session.failures < 5) &&
                os::Sys().readMonotonicTime() >= session.nextAttempt &&
                (i < os::textTerminalCount || instance)) {
                start(i);
            }
        }
    }

}  // namespace init
