#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"
#include "osgui/gui.h"
#include "os/directories.h"
#include "os/file.h"
#include "os/pty.h"
#include "os/users.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

class DesktopRuntimeTest {
    static bool check(bool value, const char* name) {
        if (!value) {
            os::System::log("[DESKTOP TEST] FAIL: ");
            os::System::log(name);
            os::System::log("\n");
        }
        return value;
    }
    static int windowRequest(uint64_t service, os::WindowRequest operation,
                             uint64_t window, os::IpcMessage& reply) {
        os::IpcMessage request{};
        request.type = static_cast<uint64_t>(operation);
        request.length = 1;
        request.data[0] = window;
        return os::callService(service, request, reply,
            os::Clock::getMonotonicTime() + 1000000000ULL);
    }
   public:
    static bool directories() {
        const char* variables[] = {"XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CONFIG_HOME",
            "XDG_CACHE_HOME", "XDG_RUNTIME_DIR", "XDG_DATA_DIRS", "XDG_CONFIG_DIRS"};
        for (const char* variable : variables) unsetenv(variable);
        setenv("HOME", "/home/path-test/", 1);
        char path[512];
        using Scope = os::DirectoryScope;
        if (!check(os::Directories::data(Scope::User, path, sizeof(path), "info") &&
                   !strcmp(path, "/home/path-test/.local/share/info"), "XDG data default")) return false;
        if (!check(os::Directories::state(Scope::User, path, sizeof(path), "info") &&
                   !strcmp(path, "/home/path-test/.local/state/info"), "XDG state default")) return false;
        if (!check(os::Directories::config(Scope::User, path, sizeof(path), "info") &&
                   !strcmp(path, "/home/path-test/.config/info"), "XDG config default")) return false;
        if (!check(os::Directories::cache(Scope::System, path, sizeof(path), "info") &&
                   !strcmp(path, "/var/cache/info"), "system cache path")) return false;
        if (!check(os::Directories::state(Scope::System, path, sizeof(path), "info") &&
                   !strcmp(path, "/var/lib/info"), "system state path")) return false;
        setenv("XDG_DATA_HOME", "/opt/data///", 1);
        if (!check(os::Directories::data(Scope::User, path, sizeof(path), "info") &&
                   !strcmp(path, "/opt/data/info"), "live environment override")) return false;
        setenv("XDG_CONFIG_HOME", "relative/config", 1);
        if (!check(os::Directories::config(Scope::User, path, sizeof(path), "info") &&
                   !strcmp(path, "/home/path-test/.config/info"), "ignore relative XDG paths")) return false;
        setenv("XDG_DATA_DIRS", "/opt/share:relative::/usr/share/", 1);
        if (!check(os::Directories::dataSearch(1, path, sizeof(path), "info") &&
                   !strcmp(path, "/opt/share/info") &&
                   os::Directories::dataSearch(2, path, sizeof(path), "info") &&
                   !strcmp(path, "/usr/share/info") &&
                   !os::Directories::dataSearch(3, path, sizeof(path), "info"),
                   "XDG ordered search with invalid entries")) return false;
        if (!check(!os::Directories::data(Scope::User, path, sizeof(path), "../escape") &&
                   !os::Directories::data(Scope::User, path, 4, "info"),
                   "path traversal and capacity rejection")) return false;
        setenv("XDG_RUNTIME_DIR", "/tmp", 1);
        if (!check(!os::Directories::runtime(Scope::User, path, sizeof(path)),
                   "reject unsafe runtime permissions")) return false;
        mkdir("/tmp/osinfo-runtime-test", 0700);
        setenv("XDG_RUNTIME_DIR", "/tmp/osinfo-runtime-test", 1);
        if (!check(os::Directories::runtime(Scope::User, path, sizeof(path), "info") &&
                   !strcmp(path, "/tmp/osinfo-runtime-test/info"),
                   "owned private runtime directory")) return false;
        rmdir("/tmp/osinfo-runtime-test");
        for (const char* variable : variables) unsetenv(variable);
        setenv("HOME", "/root", 1);
        os::System::log("[DESKTOP TEST] XDG path and environment semantics passed\n");
        return true;
    }
    static bool windowQueue() {
        const uint64_t service = os::ServiceRegistry::lookupService(
            os::ServiceRegistry::serviceRegistry(), "guiw", 2000);
        const uint64_t endpoint = os::Endpoint::create();
        if (!check(service && endpoint, "window service connection")) return false;
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::WindowRequest::Create);
        request.length = 3;
        request.data[0] = 160;
        request.data[1] = 90;
        if (!check(os::callService(service, request, reply) == 0 && !reply.data[0] && reply.data[1],
                   "create saturation test window")) return false;
        const uint64_t window = reply.data[1];
        request = {};
        request.type = static_cast<uint64_t>(os::WindowRequest::SubscribeEvents);
        request.length = 1;
        request.data[0] = window;
        request.handleCount = 1;
        request.handles[0] = endpoint;
        request.handleRights[0] = 2;
        if (!check(os::callService(service, request, reply) == 0 && !reply.data[0],
                   "subscribe saturation endpoint")) return false;
        if (!check(windowRequest(service, os::WindowRequest::Show, window, reply) == 0,
                   "show saturation window")) return false;
        do {
            if (!check(windowRequest(service, os::WindowRequest::GetEvent,
                                     window, reply) == 0, "drain initial events")) return false;
        } while (!reply.data[0]);
        if (!check(static_cast<int64_t>(reply.data[0]) ==
                       static_cast<int>(os::ErrorCode::WouldBlock),
                   "arm window event waiter")) return false;
        request = {};
        request.type = static_cast<uint64_t>(os::WindowRequest::Create);
        request.length = 3;
        request.data[0] = 80;
        request.data[1] = 60;
        if (!check(os::callService(service, request, reply) == 0 && !reply.data[0] && reply.data[1],
                   "create alternate focus window")) return false;
        const uint64_t other = reply.data[1];
        if (!check(windowRequest(service, os::WindowRequest::Show, other, reply) == 0,
                   "move focus before saturation")) return false;
        os::IpcMessage notification{};
        notification.type = 0x1234;
        for (unsigned index = 0; index < 8; ++index)
            if (!check(os::sendMessage(endpoint, notification, 0) == 0,
                       "fill notification queue")) return false;
        os::System::log("[DESKTOP TEST] full notification queue armed\n");
        if (!check(windowRequest(service, os::WindowRequest::Raise, window, reply) == 0,
                   "window RPC stalled with full notification queue")) return false;
        if (!check(windowRequest(service, os::WindowRequest::GetDisplayInfo, 0, reply) == 0,
                   "window service remains responsive")) return false;
        for (unsigned index = 0; index < 8; ++index)
            if (!check(os::receiveMessage(endpoint, notification, 0) == 0 &&
                       notification.type == 0x1234, "drain saturated notifications")) return false;
        if (!check(os::receiveMessage(endpoint, notification,
                       os::Clock::getMonotonicTime() + 1000000000ULL) == 0 &&
                       notification.type == os::windowEventNotification,
                   "retry pending wake after queue drains")) return false;
        if (!check(windowRequest(service, os::WindowRequest::GetEvent, window, reply) == 0 &&
                   !reply.data[0], "window event survives notification backpressure")) return false;
        windowRequest(service, os::WindowRequest::Destroy, window, reply);
        windowRequest(service, os::WindowRequest::Destroy, other, reply);
        os::closeHandle(endpoint);
        os::closeHandle(service);
        os::System::log("[DESKTOP TEST] saturated window notifications passed\n");
        return true;
    }
    static bool shellArguments() {
        const char* names[] = {"sh", "/bin/sh", "-sh"};
        for (const char* name : names) {
            os::System::log("[DESKTOP TEST] shell invocation: ");
            os::System::log(name);
            os::System::log("\n");
            const pid_t child = fork();
            if (!check(child >= 0, "fork shell invocation")) return false;
            if (!child) {
                os::System::log("[DESKTOP TEST] child exec starting\n");
                const char* arguments[] = {name, "-c", "exit 23", nullptr};
                execv("/bin/sh", const_cast<char* const*>(arguments));
                _exit(99);
            }
            int status = 0;
            os::System::log("[DESKTOP TEST] parent waiting for shell\n");
            if (!check(waitpid(child, &status, 0) == child &&
                       WIFEXITED(status) && WEXITSTATUS(status) == 23,
                       "shell accepts path and login argv[0]")) return false;
        }
        os::System::log("[DESKTOP TEST] shell argv conventions passed\n");
        return true;
    }
    static bool loginSession() {
        os::UserCredentials credentials{};
        os::UserProfile profile{};
        strcpy(credentials.name, "user");
        strcpy(credentials.replacement, "desktop-test-only");
        if (!check(!os::UserClient::request(os::UserOperation::Password, credentials, profile),
                   "set isolated user test password")) return false;
        os::PtyHandles handles{};
        if (!check(os::Pty::createPty(handles), "create login PTY")) return false;
        os::File master = os::File::adoptHandle(handles.masterHandle, os::File::Backend::Terminal, O_RDWR);
        os::File slave = os::File::adoptHandle(handles.slaveHandle, os::File::Backend::Terminal, O_RDWR);
        const int descriptor = slave.toDescriptor();
        if (!check(descriptor >= 0 && os::Pty::setPtyNonblocking(handles.masterHandle, true),
                   "configure login PTY")) return false;
        const pid_t child = fork();
        if (!check(child >= 0, "fork login session")) { close(descriptor); return false; }
        if (!child) {
            os::System::log("[DESKTOP TEST] login child preparing descriptors\n");
            master.close();
            if (dup2(descriptor, 0) < 0 || dup2(descriptor, 1) < 0 || dup2(descriptor, 2) < 0) _exit(98);
            if (descriptor > 2) close(descriptor);
            const char* arguments[] = {"login", "user", nullptr};
            execv("/bin/login", const_cast<char* const*>(arguments));
            os::System::log("[DESKTOP TEST] login exec failed\n");
            _exit(99);
        }
        close(descriptor);
        char output[8192]{};
        bool ok = check(readUntil(master, output, sizeof(output), "Password: "), "login password prompt");
        if (ok) ok = master.write("desktop-test-only\n", 18) == 18;
        if (ok) ok = check(readUntil(master, output, sizeof(output), "\033[01;34m~\033[0m$"), "user HOME prompt");
        if (ok) {
            const char command[] = "echo XDG=$HOME:$XDG_DATA_HOME:$XDG_STATE_HOME:$XDG_CONFIG_HOME:$XDG_CACHE_HOME:$XDG_RUNTIME_DIR\n";
            ok = master.write(command, sizeof(command) - 1) == sizeof(command) - 1 &&
                 readUntil(master, output, sizeof(output),
                    "XDG=/home/user:/home/user/.local/share:/home/user/.local/state:/home/user/.config:/home/user/.cache:/run/user/1000");
        }
        if (ok) {
            const char command[] = "cd .local\n";
            ok = master.write(command, sizeof(command) - 1) == sizeof(command) - 1 &&
                 readUntil(master, output, sizeof(output), "\033[01;34m~/.local\033[0m$");
        }
        struct stat status{};
        if (ok) ok = !stat("/run/user/1000", &status) && status.st_uid == 1000 &&
                     (status.st_mode & 0777) == 0700;
        master.write("exit 0\n", 7);
        int result = 0;
        const uint64_t deadline = os::Clock::getMonotonicTime() + 3000000000ULL;
        pid_t waited = 0;
        while (!(waited = waitpid(child, &result, WNOHANG)) && os::Clock::getMonotonicTime() < deadline)
            os::sleep(10);
        if (!waited) { kill(child, SIGKILL); waitpid(child, &result, 0); ok = false; }
        ok = ok && waited == child && WIFEXITED(result) && WEXITSTATUS(result) == 0;
        const uint64_t cleanupDeadline = os::Clock::getMonotonicTime() + 2000000000ULL;
        while (!stat("/run/user/1000", &status) && os::Clock::getMonotonicTime() < cleanupDeadline)
            os::sleep(10);
        ok = ok && stat("/run/user/1000", &status) != 0;
        if (!check(ok, "real login, HOME prompts, XDG session and runtime cleanup")) {
            char message[128];
            std::snprintf(message, sizeof(message), "[DESKTOP TEST] child=%d waited=%d status=%d\n", child, waited, result);
            os::System::log(message);
            os::System::log(output);
            return false;
        }
        os::System::log("[DESKTOP TEST] real user login and XDG session lifecycle passed\n");
        return true;
    }
    static bool dynamicLinker() {
        const char* paths[] = {"/usr/bin/ld-probe", "/usr/bin/ld-undefined", "/usr/bin/ld-missing"};
        const int exits[] = {0, 127, 127};
        for (size_t i = 0; i < 3; ++i) {
            const pid_t child = fork();
            if (!check(child >= 0, "fork dynamic linker probe")) return false;
            if (!child) {
                const char* arguments[] = {paths[i], nullptr};
                execv(paths[i], const_cast<char* const*>(arguments));
                _exit(99);
            }
            int status = 0;
            if (!check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                WEXITSTATUS(status) == exits[i], paths[i])) return false;
        }
        os::System::log("[DESKTOP TEST] dynamic dependencies and explicit loader failures passed\n");
        return true;
    }
   private:
    static bool readUntil(os::File& master, char* output, size_t capacity, const char* expected) {
        size_t length = 0;
        output[0] = 0;
        const uint64_t deadline = os::Clock::getMonotonicTime() + 5000000000ULL;
        while (os::Clock::getMonotonicTime() < deadline && length + 1 < capacity) {
            const ssize_t count = master.read(output + length, capacity - length - 1);
            if (count > 0) {
                length += count;
                output[length] = 0;
                if (strstr(output, expected)) return true;
            } else os::sleep(5);
        }
        return false;
    }
};

int main(int argc, char** argv) {
    if (argc > 1 && argv[1][0] == 's')
        return DesktopRuntimeTest::shellArguments() ? 0 : 1;
    return DesktopRuntimeTest::directories() && DesktopRuntimeTest::windowQueue() &&
           DesktopRuntimeTest::shellArguments() && DesktopRuntimeTest::loginSession() &&
           DesktopRuntimeTest::dynamicLinker() ? 0 : 1;
}
