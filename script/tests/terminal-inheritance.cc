/* 由 init 在六个文字终端上分别启动，检查 exec 前后的终端和运行环境。 */
#include <os/detail/client.h>
#include <os/runtime.h>
#include <os/service.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" char** environ;

/* 把真实终端身份和失败阶段写到串口，供宿主逐终端验收。 */
static int report(const char* result, uint32_t terminal, uint32_t actual) {
    char text[160];
    snprintf(text, sizeof(text), "[TTY-EXEC] %s tty=%u runtime=%u\n",
             result, terminal + 1, actual + 1);
    os::Sys().log(text);
    return strcmp(result, "PASS") ? 1 : 0;
}

/* 首次启动使用 init 分配的终端，替换映像时省去终端参数。 */
int main(int argc, char**) {
    os::detail::TaskControl self;
    if (self.refresh(0) || self.getTerminalKind() != 1 ||
        self.getTerminalIndex() >= 6) {
        os::Sys().log("[TTY-EXEC] FAIL invalid controlling terminal\n");
        return 1;
    }

    const uint32_t terminal = self.getTerminalIndex();
    const uint32_t actual = os::Runtime::current().getConsoleId();
    if (actual != terminal) {
        return report("FAIL terminal identity", terminal, actual);
    }

    if (argc == 1) {
        char marker[16];
        snprintf(marker, sizeof(marker), "%u", terminal);
        if (setenv("TTY_EXEC_MARKER", marker, 1) || chdir("/tmp")) {
            return report("FAIL prepare", terminal, actual);
        }

        // 建立标准输出描述符，使 exec 同时覆盖终端选择和描述符恢复。
        printf("tty%u exec inheritance test\n", terminal + 1);
        fflush(stdout);
        char* arguments[] = {const_cast<char*>("tty-probe"),
                             const_cast<char*>("after-exec"), nullptr};
        execve("/bin/tty-probe", arguments, environ);
        return report("FAIL execve", terminal, actual);
    }

    char directory[64];
    const char* marker = getenv("TTY_EXEC_MARKER");
    if (argc != 2 || !marker || strtoul(marker, nullptr, 10) != terminal ||
        !getcwd(directory, sizeof(directory)) || strcmp(directory, "/tmp") ||
        !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return report("FAIL inherited state", terminal, actual);
    }

    report("PASS", terminal, actual);
    char* arguments[] = {const_cast<char*>("login"), nullptr};
    execve("/bin/login", arguments, environ);
    return report("FAIL login handoff", terminal, actual);
}
