#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include <os/detail/client.h>
#include "service/common/protocol.h"
#include <os/runtime.h>
#include <os/pty.h>
#include <os/service.h>

extern "C" int process_test_main(int, char**);
extern "C" int sync_test_main(void);
extern "C" int memory_test_main(void);
extern "C" int clock_test_main(void);
extern "C" int stream_test_main(void);
extern "C" int readiness_test_main(void);
extern "C" int readiness_console_test(void);

/* 把信号等待的实际状态写入串口，便于自动回归定位失败条件。 */
extern "C" void test_report_signal_join(int result, int caught, int expected) {
    char message[128];
    snprintf(message, sizeof(message),
             "[POSIX] SIGNAL join=%d caught=%d expected=%d\n",
             result, caught, expected);
    os::Sys().log(message);
}

/* 将控制台等待结果写到串口，区分超时、信号中断和描述符错误。 */
extern "C" void test_report_console_poll(int result, int events, int error) {
    char message[128];
    snprintf(message, sizeof(message), "[POSIX] CONSOLE poll=%d events=%x errno=%d\n", result, events, error);
    os::Sys().log(message);
    snprintf(message, sizeof(message), "[POSIX] CONSOLE flags=%d tty=%d handle=%llu terminal=%u\n",
        fcntl(STDIN_FILENO, F_GETFL), isatty(STDIN_FILENO),
        static_cast<unsigned long long>(os::getStandardHandle(0)), os::Runtime::current().getConsoleId());
    os::Sys().log(message);
}

/* 测试私有内核等待 ABI，不把 libc 内部适配函数暴露成公共动态符号。 */
extern "C" int test_wait_address_masked(volatile uint32_t* address,
                                        uint32_t expected, uint64_t deadline,
                                        const uint64_t* mask) {
    return static_cast<int>(os::invokeSyscall(
        os::SyscallNumber::WaitAddress, reinterpret_cast<uint64_t>(address),
        expected, deadline, reinterpret_cast<uint64_t>(mask)));
}

/* PTY 用实际主从通道验证行规程和双向就绪，不依赖尚未实现的 posix_openpt。 */
static int channelReady(uint64_t handle) {
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(services::PipeOperation::Readiness);
    request.length  = 1;
    request.data[0] = handle;
    return os::detail::RequestChannel(services::CoreServiceKind::Pipe)
        .result(request, reply);
}
static bool waitReadable(uint64_t handle) {
    OsReadinessState state{};
    if (os::invokeSyscall(os::SyscallNumber::Readiness, 0,
                          reinterpret_cast<uint64_t>(&state)))
        return false;
    const uint64_t deadline = os::Sys().readMonotonicTime() + 3000000000ULL;
    for (;;) {
        state.generation = os::invokeSyscall(os::SyscallNumber::Readiness, 4);
        const int ready  = channelReady(handle);
        if (ready < 0) return false;
        if (ready & POLLIN) return true;
        const int status =
            os::invokeSyscall(os::SyscallNumber::Readiness, 1,
                              reinterpret_cast<uint64_t>(&state), deadline);
        if (status && status != static_cast<int>(os::ErrorCode::WouldBlock))
            return false;
    }
}
static int ptyReadinessTest() {
    os::Pty pty;
    if (pty.create()) return 1;
    if (!pty.refreshAttributes()) return 2;
    pty.setCanonicalMode(true);
    pty.setEcho(false);
    if (!pty.applyAttributes()) return 2;
    os::detail::PipeClient master(pty.getMasterHandle()),
        slave(pty.getSlaveHandle());
    if (!(channelReady(pty.getMasterHandle()) & POLLOUT)) return 3;
    if (master.write("r", 1) != 1) return 4;
    os::sleep(50);
    if (channelReady(pty.getSlaveHandle()) & POLLIN) return 5;
    if (master.write("\n", 1) != 1 || !waitReadable(pty.getSlaveHandle()))
        return 6;
    if (!(channelReady(pty.getSlaveHandle()) & POLLIN)) return 7;
    char bytes[2];
    if (slave.read(bytes, sizeof(bytes)) != 2 || bytes[0] != 'r' ||
        bytes[1] != '\n')
        return 8;
    if (slave.write("o", 1) != 1 || !waitReadable(pty.getMasterHandle()))
        return 9;
    if (master.read(bytes, 1) != 1 || bytes[0] != 'o') return 10;
    return 0;
}

/* 安装到专用测试镜像的登录入口；只有 tty1 执行，避免多个套件竞争全局资源。 */
static void report(const char* name, int result) {
    char message[128];
    snprintf(message, sizeof(message), "[POSIX] %s RESULT %d errno=%d\n", name,
             result, errno);
    os::Sys().log(message);
}

int main(int argc, char** argv) {
    /* spawn/fexecve 的子模式复用同一测试映像。 */
    if (argc > 1) return process_test_main(argc, argv);
    if (os::Runtime::current().getConsoleId() != 0)
        for (;;) os::sleep(1000);
    os::sleep(2000);
    os::Sys().log("[POSIX] B03 BEGIN\n");
    int result = process_test_main(argc, argv);
    report("B03", result);
    {
        os::Sys().log("[POSIX] B10 BEGIN\n");
        result = sync_test_main();
        report("B10", result);
    }
    {
        os::Sys().log("[POSIX] B04 BEGIN\n");
        result = memory_test_main();
        report("B04", result);
    }
    {
        os::Sys().log("[POSIX] B09 CLOCK BEGIN\n");
        result = clock_test_main();
        report("B09 CLOCK", result);
    }
    {
        os::Sys().log("[POSIX] B07 BEGIN\n");
        result = stream_test_main();
        report("B07", result);
    }
    {
        os::Sys().log("[POSIX] B05 BEGIN\n");
        result = readiness_test_main();
        report("B05", result);
    }
    {
        result = ptyReadinessTest();
        report("B05 PTY", result);
    }
    {
        os::Sys().log("[POSIX] B05 CONSOLE WAIT\n");
        result = readiness_console_test();
        report("B05 CONSOLE", result);
    }
    /* 保留现场供 QMP 检查，不触发登录监督者无限重启测试。 */
    for (;;) os::sleep(1000);
}
