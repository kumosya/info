#include <cstdio>
#include <fcntl.h>
#include <csignal>
#include <unistd.h>

#include "os/abi.h"
#include <sys/stat.h>
#include <sys/wait.h>

class PackageRuntimeTest {
    static bool run(const char* const* arguments, int expected = 0) {
        os::System::log("[INPKG TEST] run ");
        os::System::log(arguments[1]);
        os::System::log("\n");
        pid_t child = fork();
        if (child < 0) return false;
        if (!child) {
            const int output = open("/tmp/inpkg-runtime-output", O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (output < 0 || dup2(output, STDOUT_FILENO) < 0 || dup2(output, STDERR_FILENO) < 0) _exit(98);
            close(output);
            execv(arguments[0], const_cast<char* const*>(arguments));
            _exit(99);
        }
        int status = 0;
        pid_t waited = 0;
        const uint64_t deadline = os::Clock::getMonotonicTime() + 30000000000ULL;
        while (!(waited = waitpid(child, &status, WNOHANG)) && os::Clock::getMonotonicTime() < deadline) os::sleep(10);
        if (!waited) { kill(child, SIGKILL); waitpid(child, &status, 0); }
        const bool ok = waited == child && WIFEXITED(status) && WEXITSTATUS(status) == expected;
        if (!ok) {
            char detail[128];
            snprintf(detail, sizeof(detail), "[INPKG TEST] wait=%d child=%d status=%d\n", waited, child, status);
            os::System::log(detail);
            os::System::log("[INPKG TEST] command failed or timed out\n");
            FILE* file = fopen("/tmp/inpkg-runtime-output", "r");
            if (file) { char line[512]; while (fgets(line, sizeof(line), file)) os::System::log(line); fclose(file); }
        }
        return ok;
    }
    static bool write(const char* path, const char* value) {
        FILE* file = fopen(path, "w");
        if (!file) return false;
        bool ok = fputs(value, file) >= 0;
        return fclose(file) == 0 && ok;
    }

   public:
    static bool check() {
        if (mkdir("/tmp/inpkg-runtime-payload", 0755) != 0) return false;
        if (!write("/tmp/inpkg-runtime-payload/probe", "old bytes\n") ||
            !write("/tmp/inpkg-runtime.conf",
                   "name=runtime-probe\nversion=1\nbin=/usr/bin/"
                   "runtime-probe\ninstall=probe -> /usr/bin/runtime-probe\n"))
            return false;
        const char* list[]    = {"/usr/bin/inpkg", "list", "--system", nullptr};
        const char* crossUpgrade[] = {
            "/usr/bin/inpkg", "upgrade", "/tmp/cross-platform-probe.inpk",
            "--system", "-y", nullptr};
        const char* crossRemove[] = {
            "/usr/bin/inpkg", "remove", "cross-platform-probe",
            "--system", "-y", nullptr};
        if (!run(list) || !run(crossUpgrade) || !run(crossRemove) ||
            access("/usr/bin/cross-platform-probe", F_OK) == 0)
            return false;
        os::System::log("[INPKG TEST] Linux database upgrade and removal passed\n");
        const char* pack[]    = {"/usr/bin/inpkg",
                                 "pack",
                                 "/tmp/inpkg-runtime-payload",
                                 "--manifest",
                                 "/tmp/inpkg-runtime.conf",
                                 "--output",
                                 "/tmp/runtime-probe.inpk",
                                 nullptr};
        const char* missing[] = {
            "/usr/bin/inpkg", "upgrade", "/tmp/runtime-probe.inpk",
            "--system",       "-y",      nullptr};
        const char* install[] = {
            "/usr/bin/inpkg", "upgrade",           "/tmp/runtime-probe.inpk",
            "--system",       "--install-missing", "-y",
            nullptr};
        const char* duplicate[] = {
            "/usr/bin/inpkg", "install", "/tmp/runtime-probe.inpk",
            "--system",       "-y",      nullptr};
        const char* remove[] = {"/usr/bin/inpkg", "remove", "runtime-probe",
                                "--system",       "-y",     nullptr};
        if (!run(pack) || !run(missing, 1) || !run(install) ||
            !run(duplicate, 1))
            return false;
        if (!write("/tmp/inpkg-runtime-payload/probe", "new bytes\n") ||
            !run(pack) || !run(missing) || !run(remove))
            return false;
        const char* local[] = {
            "/usr/bin/inpkg", "upgrade",           "/tmp/runtime-probe.inpk",
            "--root",         "--install-missing", "-y",
            nullptr};
        const char* removeLocal[] = {
            "/usr/bin/inpkg", "remove", "runtime-probe",
            "--root",         "-y",     nullptr};
        if (!run(local) || !run(removeLocal)) return false;
        os::System::log(
            "[INPKG TEST] target install, upgrade, missing, local scope and "
            "removal passed\n");
        return true;
    }
};
int main() {
    os::System::log("[INPKG TEST] start\n");
    if (!PackageRuntimeTest::check()) {
        os::System::log("[INPKG TEST] FAIL\n");
        return 1;
    }
    const char* arguments[] = {"terminal", nullptr};
    execv("/usr/bin/terminal.real", const_cast<char* const*>(arguments));
    return 2;
}
