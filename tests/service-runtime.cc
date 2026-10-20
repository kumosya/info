#include "os/task.h"
#include "os/service_control.h"
#include "os/pty.h"
#include "os/file.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <initializer_list>

class RuntimeTest {
    static bool require(bool ok, const char* message) {
        os::System::log(ok ? "[RUNTIME TEST] PASS " : "[RUNTIME TEST] FAIL ");
        os::System::log(message); os::System::log("\n"); return ok;
    }
    static bool directory(const char* path) {
        DIR* dir = opendir(path); if (!dir) return false;
        unsigned count = 0; bool ok = true;
        errno = 0;
        while (dirent* entry = readdir(dir)) {
            char child[192]; snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            struct stat status{};
            if (stat(child, &status) || entry->d_ino != status.st_ino) ok = false;
            if (++count > 256) { ok = false; break; }
            errno = 0;
        }
        if (errno) ok = false;
        closedir(dir); return ok && count;
    }
    static bool command(const char* const* args, int expected, const char* contains) {
        int descriptors[2]; if (pipe(descriptors)) return false;
        const pid_t child = fork();
        if (!child) {
            close(descriptors[0]); dup2(descriptors[1], 1); dup2(descriptors[1], 2); close(descriptors[1]);
            execv(args[0], const_cast<char* const*>(args)); _exit(99);
        }
        close(descriptors[1]);
        if (child < 0) { close(descriptors[0]); return false; }
        char output[8192]{}; size_t used = 0; char bytes[512];
        while (true) {
            const ssize_t count = read(descriptors[0], bytes, sizeof(bytes)); if (count <= 0) break;
            const size_t copy = static_cast<size_t>(count) < sizeof(output) - used - 1 ? count : sizeof(output) - used - 1;
            memcpy(output + used, bytes, copy); used += copy;
        }
        close(descriptors[0]); int status = 0;
        const bool ok = waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == expected && (!contains || strstr(output, contains));
        if (!ok) { os::System::log(output); os::System::log("\n"); }
        return ok;
    }
    static bool services() {
        os::ServiceStatus status{}, task{}, device{};
        if (os::ServiceControl::call(os::ServiceOperation::Status, "task", 0, task) ||
            os::ServiceControl::call(os::ServiceOperation::Status, "device", 0, device)) return false;
        for (const char* name : {"pipe", "msgq", "pci", "storage", "users", "tty", "console", "guiw"}) {
            if (os::ServiceControl::call(os::ServiceOperation::Status, name, 0, status)) return false;
            os::TaskInfo info{};
            if (os::Task::query(status.pid, info) || !info.managedService) return false;
            if ((!strcmp(name, "pipe") || !strcmp(name, "msgq")) && info.parentId != task.pid) return false;
            if ((!strcmp(name, "pci") || !strcmp(name, "storage")) && info.parentId != device.pid) return false;
            if (os::Task::sendSignal(status.pid, SIGKILL) != static_cast<int>(os::ErrorCode::PermissionDenied)) return false;
        }
        if (os::Task::executeService("/bin/sleep", "fake-service")) return false;
        if (!require(os::ServiceControl::call(os::ServiceOperation::Stop, "console", 0, status) == static_cast<int>(os::ErrorCode::Busy) &&
            !strcmp(status.blockedBy, "guiw"), "dependent service blocks stop")) return false;
        if (!require(os::ServiceControl::call(os::ServiceOperation::Start, "test-sleeper", 0, status) == 0 && status.pid, "start test service")) return false;
        const uint64_t first = status.pid;
        os::TaskInfo managed{};
        if (os::Task::query(first, managed) || os::Task::sendSignal(-managed.taskGroupId, SIGKILL) == 0 || os::Task::query(first, managed)) return false;
        if (os::Task::sendSignal(first, SIGTERM) != static_cast<int>(os::ErrorCode::PermissionDenied)) return false;
        if (!require(os::ServiceControl::call(os::ServiceOperation::Restart, "test-sleeper", 0, status) == 0 && status.pid != first, "restart test service")) return false;
        if (!require(os::ServiceControl::call(os::ServiceOperation::Enable, "test-sleeper", 0, status) == 0 && status.enabled, "enable test service")) return false;
        FILE* config = fopen("/etc/services", "r"); char persisted[2048]{};
        if (!config) return false;
        fread(persisted, 1, sizeof(persisted) - 1, config); fclose(config);
        if (!require(strstr(persisted, "test-sleeper|init|1|"), "enabled setting persisted to file")) return false;
        if (!require(os::ServiceControl::call(os::ServiceOperation::Disable, "test-sleeper", 0, status) == 0 && !status.enabled, "disable test service")) return false;
        if (!require(os::ServiceControl::call(os::ServiceOperation::Stop, "test-sleeper", 0, status) == 0 && !status.pid && !status.desired, "stop test service")) return false;
        const auto parent = os::Task::getId();
        const pid_t unprivileged = fork();
        if (!unprivileged) {
            if (setuid(1000)) _exit(90);
            os::ServiceStatus denied{}; os::TaskInfo privateInfo{};
            const bool protectedControl = os::ServiceControl::call(os::ServiceOperation::Start, "test-sleeper", 0, denied) == static_cast<int>(os::ErrorCode::PermissionDenied);
            char path[96]; snprintf(path, sizeof(path), "/proc/%llu/cmdline", static_cast<unsigned long long>(parent));
            FILE* command = fopen(path, "r"); const bool hidden = !command;
            if (command) fclose(command);
            const bool redacted = !os::Task::query(parent, privateInfo) && !privateInfo.commandLineSize;
            _exit(protectedControl && hidden && redacted ? 0 : 91);
        }
        int childStatus = 0;
        if (!require(unprivileged > 0 && waitpid(unprivileged, &childStatus, 0) == unprivileged && WIFEXITED(childStatus) && !WEXITSTATUS(childStatus),
            "non-root service mutation denied and foreign command arguments hidden")) return false;
        return true;
    }
    static bool shells() {
        uint64_t old[6]{};
        os::File fs; os::DirectoryEntry entries[8]{}; uint64_t cursor = 0;
        while (true) {
            const auto count = fs.readDirectoryBatch("/proc", cursor, entries, 8);
            if (count < 0) return false;
            if (!count) break;
            for (int64_t i = 0; i < count; ++i) {
                cursor = entries[i].nextOffset;
                os::TaskInfo info{}; const auto pid = strtoull(entries[i].name, nullptr, 10);
                if (pid && !os::Task::query(pid, info) && info.terminalKind == 1 && info.terminalIndex < 6 && strstr(info.name, "/sh")) old[info.terminalIndex] = pid;
            }
        }
        for (auto pid : old) if (!pid) return false;
        const char* args[] = {"/bin/killall", "sh", nullptr};
        if (!command(args, 0, nullptr)) return false;
        for (unsigned cycle = 0; cycle < 3; ++cycle) {
            uint64_t replacement[6]{};
            for (unsigned attempt = 0; attempt < 100; ++attempt) {
                cursor = 0;
                while (true) {
                    const auto count = fs.readDirectoryBatch("/proc", cursor, entries, 8); if (count < 0) return false; if (!count) break;
                    for (int64_t i = 0; i < count; ++i) {
                        cursor = entries[i].nextOffset;
                        os::TaskInfo info{}; const auto pid = strtoull(entries[i].name, nullptr, 10);
                        if (pid && !os::Task::query(pid, info) && info.terminalKind == 1 && info.terminalIndex < 6 &&
                            strstr(info.name, "/login") && pid != old[info.terminalIndex]) replacement[info.terminalIndex] = pid;
                    }
                }
                unsigned found = 0; for (auto pid : replacement) if (pid) ++found;
                if (found == 6) break;
                os::sleep(100);
            }
            for (auto pid : replacement) if (!pid) return false;
            if (cycle < 2) for (unsigned i = 0; i < 6; ++i) {
                old[i] = replacement[i]; if (os::Task::sendSignal(old[i], SIGKILL)) return false;
            }
        }
        return true;
    }
    static bool ptys() {
        const char* forgedArguments[] = {"sleep", "60", "--os-terminal=6"};
        const auto forged = os::Task::execute("/bin/sleep", 3, forgedArguments);
        os::TaskInfo forgedInfo{};
        const bool metadata = forged && !os::Task::getInfo(forged, forgedInfo) && !forgedInfo.terminalKind;
        if (forged) { os::Task::terminate(forged, -9); os::closeHandle(forged); }
        if (!require(metadata, "ordinary arguments cannot forge controlling TTY metadata")) return false;
        os::PtyHandles controlled{};
        if (!os::Pty::createPty(controlled)) return false;
        const char* arguments[] = {"sleep", "60"};
        const auto child = os::Task::executeWithIo("/bin/sleep", 2, arguments, controlled.slaveHandle, controlled.slaveHandle, controlled.slaveHandle);
        os::closeHandle(controlled.slaveHandle);
        os::TaskInfo info{};
        bool selected = child && !os::Task::getInfo(child, info) && info.terminalKind == 2 &&
            os::Pty::setPtyForegroundGroup(controlled.masterHandle, info.id) && !os::Task::query(info.id, info) && info.terminalForegroundGroup == info.id;
        char tty[32]; snprintf(tty, sizeof(tty), "pts/%u", info.terminalIndex);
        const char* selection[] = {"/bin/ps", "-t", tty, nullptr};
        selected = selected && command(selection, 0, "sleep");
        if (child) { os::Task::terminate(child, -9); os::closeHandle(child); }
        os::closeHandle(controlled.masterHandle); os::sleep(150);
        if (!require(selected, "ps selects actual PTY and foreground group metadata")) return false;
        for (unsigned i = 0; i < 20; ++i) {
            os::PtyHandles pty{};
            if (!os::Pty::createPty(pty)) return false;
            uint32_t number = 99;
            if (!os::Pty::getPtyNumber(pty.masterHandle, number) || number >= 8) return false;
            os::closeHandle(pty.masterHandle); os::closeHandle(pty.slaveHandle);
            os::sleep(150);
        }
        return true;
    }
 public:
    static bool check() {
        os::sleep(2000);
        for (const char* path : {"/sys", "/sys/", "/sys/kernel", "/sys/cpu", "/sys/memory", "/proc", "/proc/self"})
            if (!require(directory(path), path)) return false;
        FILE* file = fopen("/proc/self/status", "r"); char status[1536]{};
        if (!file) return require(false, "open self status");
        fread(status, 1, sizeof(status) - 1, file); fclose(file);
        char pid[32]; snprintf(pid, sizeof(pid), "Pid:\t%llu\n", static_cast<unsigned long long>(os::Task::getId()));
        if (!require(strstr(status, pid), "self follows requester")) return false;
        os::File snapshot;
        const auto completed = os::Endpoint::create(); os::FileOperationResult result{}; char async[64]{};
        const bool asynchronous = completed && !snapshot.open("/proc/self/status", O_RDONLY) &&
            snapshot.readAsync(0, sizeof(async), completed, 123) && os::File::waitOperation(completed, result, async, sizeof(async)) &&
            !result.status && result.requestId == 123 && result.transferred && !memcmp(async, status, result.transferred);
        if (completed) os::closeHandle(completed);
        if (!require(asynchronous, "proc snapshot asynchronous read")) return false;
        const char* selections[] = {"-a", "-x", "-e", "-A", "-ef"};
        for (const char* option : selections) {
            const char* args[] = {"/bin/ps", option, nullptr};
            if (!require(command(args, 0, "PID"), option)) return false;
        }
        const char* ps[] = {"/bin/ps", nullptr};
        const char* tty[] = {"/bin/ps", "-t", "tty1", nullptr};
        const char* pty[] = {"/bin/ps", "-t", "pts/0", nullptr};
        const char* ls[] = {"/bin/ls", "-l", "/sys", nullptr};
        const char* ctl[] = {"/usr/bin/systemctl", "list", nullptr};
        if (!require(command(ps, 0, "PID") && command(tty, 0, "tty1") && command(pty, 0, "PID"), "ps default and terminal selection") ||
            !require(command(ls, 0, "kernel"), "ls -l /sys") || !require(command(ctl, 0, "storage"), "systemctl CLI") ||
            !require(services(), "owners, file services, signal protection and persistent control") ||
            !require(shells(), "six TTY sessions recover after killall sh and repeated login exits") ||
            !require(ptys(), "20 PTY create/close cycles")) return false;
        return true;
    }
};

int main() {
    if (!RuntimeTest::check()) return 1;
    os::System::log("[RUNTIME TEST] ALL PASSED\n");
    const char* arguments[] = {"terminal", nullptr};
    execv("/usr/bin/terminal.real", const_cast<char* const*>(arguments));
    return 2;
}
