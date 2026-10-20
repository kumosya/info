#include "os/abi.h"
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

class BaseToolsTest {
    static bool run(const char* const* arguments, int expected, char* output, size_t capacity) {
        int descriptors[2];
        if (pipe(descriptors) != 0) return false;
        pid_t child = fork();
        if (child < 0) { close(descriptors[0]); close(descriptors[1]); return false; }
        if (!child) {
            close(descriptors[0]);
            if (dup2(descriptors[1], STDOUT_FILENO) < 0) _exit(98);
            close(descriptors[1]);
            setenv("HOSTNAME", "forged-environment-host", 1);
            execv(arguments[0], const_cast<char* const*>(arguments));
            _exit(99);
        }
        close(descriptors[1]);
        size_t used = 0;
        while (used + 1 < capacity) {
            ssize_t count = read(descriptors[0], output + used, capacity - used - 1);
            if (count <= 0) break;
            used += count;
        }
        output[used] = 0; close(descriptors[0]);
        int status = 0;
        return waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == expected;
    }
 public:
    static bool check() {
        char output[2048]{}, host[64]{}, version[64]{};
        FILE* file = fopen("/sys/kernel/hostname", "r");
        if (!file) return false;
        bool readHost = fgets(host, sizeof(host), file) != nullptr; fclose(file);
        file = fopen("/sys/kernel/osrelease", "r");
        if (!file) return false;
        bool readVersion = fgets(version, sizeof(version), file) != nullptr; fclose(file);
        host[strcspn(host, "\r\n")] = 0; version[strcspn(version, "\r\n")] = 0;
        const char* info[] = {"/usr/bin/sysinfo", nullptr};
        if (!readHost || !readVersion || !run(info, 0, output, sizeof(output)) ||
            !strstr(output, host) || !strstr(output, version) || strstr(output, "forged-environment-host")) return false;
        const char* mount[] = {"/sbin/mount", nullptr};
        const char* unmount[] = {"/sbin/umount", "/mnt/ext4", nullptr};
        const char* remount[] = {"/sbin/mount", "LABEL=OSINFO_EXT4", "/mnt/ext4", "ext4", nullptr};
        const char* format[] = {"/usr/sbin/mkfs", "ext4", "/dev/not-a-test-device", nullptr};
        const char* echo[] = {"/bin/echo", "base-tools-echo", nullptr};
        if (!run(mount, 0, output, sizeof(output)) || !strstr(output, "/mnt/ext4") ||
            !run(unmount, 0, output, sizeof(output)) || !run(remount, 0, output, sizeof(output)) ||
            !run(format, 1, output, sizeof(output)) || !run(echo, 0, output, sizeof(output)) || !strstr(output, "base-tools-echo")) return false;
        os::System::log("[BASE TOOLS TEST] configured sysinfo, mount, umount, mkfs rejection and coreutils passed\n");
        return true;
    }
};

int main() {
    if (!BaseToolsTest::check()) { os::System::log("[BASE TOOLS TEST] FAIL\n"); return 1; }
    const char* arguments[] = {"terminal", nullptr};
    execv("/usr/bin/terminal.real", const_cast<char* const*>(arguments));
    return 2;
}
