/*
 * src/task/core_dump.cc
 * 用户态核心转储写入服务，检查目录与空间后分块写文件。
 */

#include "service/task/core_dump.h"

#include <os/detail/client.h>
#include "service/common/protocol.h"
#include <os/runtime.h>
#include <os/device.h>
#include <os/dir.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

static services::wire::CorePolicy policy{1, 8 * 1024 * 1024, 2, 10000};
static uint64_t maximumSpace = 64 * 1024 * 1024;
static char directory[192]   = "/var/core";
static uint64_t fs           = 0;
alignas(16) static uint8_t workerStack[64 * 1024]{};

/*
 * 构造并调用核心转储控制请求，把公共消息格式和错误处理集中在一个入口。
 */
static int operation(services::TaskOperation type, os::IpcMessage& request,
                     os::IpcMessage& reply) {
    request.type = static_cast<uint64_t>(type);

    if (request.length < 4) {
        request.length = 4;
    }

    const int result = os::detail::RequestChannel(services::CoreServiceKind::Task)
                           .call(request, reply,
                                 os::Sys().readMonotonicTime() + 1000000000ULL);

    return result ? result : static_cast<int64_t>(reply.data[0]);
}

/*
 * 把十进制字符串转换为无符号整数，检查非法字符及乘加溢出。
 */
static bool number(const char* value, uint64_t& result) {
    if (!*value) {
        return false;
    }

    result = 0;

    for (; *value; ++value) {
        if (*value < '0' || *value > '9' ||
            result > (UINT64_MAX - (*value - '0')) / 10) {
            return false;
        }

        result = result * 10 + *value - '0';
    }

    return true;
}

/*
 * 读取并校验转储配置，解析目录、容量和开关，再把策略提交给内核。
 */
static bool configuration() {
    os::File input;

    if (input.open("/etc/core", O_RDONLY)) {
        return false;
    }

    // 拒绝可能被其他用户替换的策略文件，避免转储上限被悄悄放宽。
    if (input.refresh() || input.getType() != os::File::Type::File ||
        input.getUid() || (input.getMode() & 0022)) {
        return false;
    }

    char text[2048]{};

    // 配置只读入固定缓冲区并预留结尾，读取过长时直接拒绝，不能把截断配置当完整策略。
    const ssize_t count = input.read(text, sizeof(text) - 1);

    if (count <= 0 || count == sizeof(text) - 1) {
        return false;
    }

    // 按行去掉尾部换行与空白，跳过注释，再分割键值；遇到未知配置或非法数字直接拒绝整份策略。
    for (char* line = text; *line;) {
        char* next = line;

        while (*next && *next != '\n') {
            ++next;
        }

        char* end = next;

        if (*next) {
            *next++ = 0;
        }

        while (end > line &&
               (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
            *--end = 0;
        }

        while (*line == ' ' || *line == '\t') {
            ++line;
        }

        if (*line && *line != '#') {
            char* equal = line;

            while (*equal && *equal != '=') {
                ++equal;
            }

            if (!*equal) {
                return false;
            }

            *equal++       = 0;
            uint64_t value = 0;

            if (os::Runtime::stringsEqual(line, "directory")) {
                if (equal[0] != '/' ||
                    os::Runtime::stringLength(equal) >= sizeof(directory)) {
                    return false;
                }

                os::Runtime::copyString(directory, equal, sizeof(directory));
            } else {
                if (!number(equal, value)) {
                    return false;
                }

                if (os::Runtime::stringsEqual(line, "enabled")) {
                    policy.enabled = value;
                } else if (os::Runtime::stringsEqual(line, "maximum_bytes")) {
                    policy.maximumBytes = value;
                } else if (os::Runtime::stringsEqual(line,
                                                     "maximum_concurrent")) {
                    policy.maximumConcurrent = value;
                } else if (os::Runtime::stringsEqual(line, "timeout_ms")) {
                    policy.timeoutMilliseconds = value;
                } else if (os::Runtime::stringsEqual(line, "maximum_space")) {
                    maximumSpace = value;
                } else {
                    return false;
                }
            }
        }

        line = next;
    }

    return true;
}

/*
 * 检查转储输出目录的类型、所有者和权限，避免将故障进程内存写入不受控位置。
 */
static bool controlledDirectory() {
    // 每级父目录都必须属于
    // root，且其他用户不可写；当前文件系统不解析符号链接，因此要求实际目录。
    os::File root;

    if (root.refresh("/") || root.getType() != os::File::Type::Directory ||
        root.getUid() || (root.getMode() & 0022)) {
        return false;
    }

    char path[sizeof(directory)]{};
    size_t length = 0;

    for (size_t i = 0;; ++i) {
        const char c = directory[i];

        if ((c == '/' && i) || !c) {
            path[length] = 0;

            if (!length ||
                (length >= 2 && path[length - 1] == '.' &&
                 (path[length - 2] == '/' || path[length - 2] == '.'))) {
                return false;
            }

            os::File item;

            if (item.refresh(path)) {
                if (os::Dir().create(path, c ? 0755 : 0700) ||
                    item.refresh(path)) {
                    return false;
                }
            }

            if (item.getType() != os::File::Type::Directory || item.getUid() ||
                (item.getMode() & 0022)) {
                return false;
            }

            if (!c && (item.getMode() & 0077)) {
                return false;
            }
        }

        if (!c) {
            break;
        }

        if (length + 1 >= sizeof(path)) {
            return false;
        }

        path[length++] = c;
    }

    return true;
}

/*
 * 统计转储目录已有文件大小，判断本次写入是否超出目录空间预算。
 */
static bool spaceAvailable(uint64_t needed) {
    uint64_t used = 0;
    os::Dir dir;

    if (dir.open(directory)) {
        return false;
    }

    // 限制枚举数量并累计目录现有文件大小，目录异常巨大或读取出错时宁可拒绝新转储。
    for (uint64_t index = 0; index < 4096; ++index) {
        os::Dir::Entry entry;
        const int result = dir.readNext(entry);

        if (result < 0) {
            return false;
        }

        // 枚举结束后才比较目录预算；用减法判断剩余量，避免当前占用与本次大小相加溢出。
        if (!result) {
            return used <= maximumSpace && needed <= maximumSpace - used;
        }

        if (os::Runtime::stringsEqual(entry.getName(), ".") ||
            os::Runtime::stringsEqual(entry.getName(), "..")) {
            continue;
        }

        char path[512];
        os::Runtime::copyString(path, directory, sizeof(path));
        size_t n  = os::Runtime::stringLength(path);
        path[n++] = '/';
        path[n]   = 0;
        os::Runtime::copyString(path + n, entry.getName(), sizeof(path) - n);
        os::File file;

        if (file.refresh(path)) {
            return false;
        }

        if (file.getSize() > maximumSpace - used) {
            return false;
        }

        used += file.getSize();
    }

    return false;
}

/*
 * 把整数追加为十进制文本，供构造转储文件名使用。
 */
static void decimal(char* output, size_t& used, uint64_t value) {
    char reversed[20];
    unsigned n = 0;
    do {
        reversed[n++] = '0' + value % 10;
        value /= 10;
    } while (value);

    while (n) {
        output[used++] = reversed[--n];
    }

    output[used] = 0;
}

/*
 * 按票据分块读取内核捕获的数据并写入文件。
 * 检查目录、空间和写入长度，所有失败都通过结果协议通知内核。
 */
static bool writeCore(const services::wire::CoreTicket& ticket) {
    if (!controlledDirectory() || !spaceAvailable(ticket.bytes)) {
        return false;
    }

    // 用任务身份和启动信息构造最终文件名，先写临时文件，避免外界看到只完成一半的核心文件。
    char final[320]{};
    os::Runtime::copyString(final, directory, sizeof(final));
    size_t used         = os::Runtime::stringLength(final);
    const char prefix[] = "/core.";

    for (char c : prefix) {
        if (c) {
            final[used++] = c;
        }
    }

    decimal(final, used, ticket.pid);
    final[used++] = '.';
    decimal(final, used, ticket.started);
    final[used++] = '.';
    decimal(final, used, ticket.id);
    char partial[336];
    os::Runtime::copyString(partial, final, sizeof(partial));
    os::Runtime::copyString(partial + used, ".partial", sizeof(partial) - used);
    os::File file;

    if (file.open(partial, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600)) {
        return false;
    }

    bool success = true;
    uint8_t buffer[4096];

    // 每轮只向内核读取一个有上限的数据块，并检查文件是否完整写入，不能把短写当成功。
    for (uint64_t offset = 0; offset < ticket.bytes;) {
        if (os::Sys().readMonotonicTime() >= ticket.deadline) {
            success = false;
            break;
        }

        const uint64_t bytes = ticket.bytes - offset < sizeof(buffer)
                                   ? ticket.bytes - offset
                                   : sizeof(buffer);
        os::IpcMessage request{}, reply{};
        request.length  = 4;
        request.data[0] = ticket.id;
        request.data[1] = offset;
        request.data[2] = bytes;
        request.data[3] = reinterpret_cast<uint64_t>(buffer);

        if (operation(services::TaskOperation::CoreRead, request, reply) !=
                static_cast<int>(bytes) ||
            file.write(buffer, bytes) != static_cast<ssize_t>(bytes)) {
            success = false;
            break;
        }

        offset += bytes;
    }

    // 内容写完先刷新，再发布最终文件名，刷新失败不能让外界误认为转储完整成功。
    if (success) {
        success = !file.flush();
    }

    // 先写完临时文件，再用硬链接一次性发布；目标已存在时拒绝，避免覆盖已有转储。
    if (success) {
        success = !os::Fs().link(partial, final);
    }

    // 不论成功或失败都关闭文件并移除临时名，成功时最终硬链接已经持有完整文件。
    file.close();
    os::Fs().removeFile(partial);

    return success;
}

/*
 * 持续领取待写转储，执行文件写出，再提交成功或失败结果；空闲时等待，避免忙循环。
 */
static void run() {
    fs = os::detail::RegistryClient(os::getBootstrapHandle())
             .lookup(services::fsServiceName, 30000);

    if (!fs) {
        os::Sys().log("[CORE] disabled: fs service unavailable\n");
        os::exitThread(0);
    }

    // 配置和目标目录未通过检查时停止写入线程，不能使用不明确的默认路径继续导出进程内存。
    if (!configuration()) {
        os::Sys().log(
            "[CORE] disabled: /etc/core must be readable, valid, "
            "root-owned and not group/other-writable\n");
        os::exitThread(0);
    }

    if (!controlledDirectory()) {
        os::Sys().log("[CORE] disabled: unsafe or unavailable directory: ");
        os::Sys().log(directory);
        os::Sys().log("\n");
        os::exitThread(0);
    }

    os::IpcMessage request{}, reply{};
    request.length = sizeof(policy) / 8;
    os::Runtime::copyBytes(request.data, &policy, sizeof(policy));

    if (operation(services::TaskOperation::CoreConfigure, request, reply)) {
        os::Sys().log("[CORE] policy rejected\n");
        os::exitThread(0);
    }

    os::Sys().log("[CORE] bounded ELF writer online\n");

    // 后台循环领取内核捕获票据，磁盘写入在用户服务完成，不占用故障进程的退出执行栈。
    while (true) {
        request        = {};
        reply          = {};
        request.length = 1;

        if (operation(services::TaskOperation::CoreAcquire, request, reply)) {
            os::sleep(50);
            continue;
        }

        services::wire::CoreTicket ticket{};
        os::Runtime::copyBytes(&ticket, &reply.data[1], sizeof(ticket));

        // 文件写入的成功与失败都要回报内核，让内核归还捕获缓冲并完成延后的退出通知。
        const bool success = writeCore(ticket);
        request            = {};
        reply              = {};
        request.length     = 2;
        request.data[0]    = ticket.id;
        request.data[1]    = success;
        const int finished =
            operation(services::TaskOperation::CoreFinish, request, reply);

        if (finished && success) {
            os::Sys().log("[CORE] completion expired\n");
        } else {
            os::Sys().log(
                success ? "[CORE] core committed\n"
                        : "[CORE] write failed; process exit continues\n");
        }
    }
}

/*
 * 创建转储写入线程，失败时让服务启动路径得到明确结果。
 */
static bool startWriter() {
    return os::createThread(run, reinterpret_cast<uint64_t>(
                                     workerStack + sizeof(workerStack))) != 0;
}

/*
 * 对外提供转储写入服务启动入口，内部复用线程创建逻辑。
 */
bool task::core::start() { return startWriter(); }
