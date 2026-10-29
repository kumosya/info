#include "service/task/core_dump.h"

#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include <os/device.h>
#include <os/dir.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace {
os::wire::CorePolicy policy{1, 8 * 1024 * 1024, 2, 10000};
uint64_t maximumSpace = 64 * 1024 * 1024;
char directory[192]   = "/var/core";
uint64_t fs           = 0;
alignas(16) uint8_t workerStack[64 * 1024]{};
int operation(os::TaskOperation type, os::IpcMessage& request,
              os::IpcMessage& reply) {
    request.type = static_cast<uint64_t>(type);
    if (request.length < 4) request.length = 4;
    const int result = services::RequestChannel(os::CoreServiceKind::Task)
                           .call(request, reply,
                                 os::Sys().readMonotonicTime() + 1000000000ULL);
    return result ? result : static_cast<int64_t>(reply.data[0]);
}

bool number(const char* value, uint64_t& result) {
    if (!*value) return false;
    result = 0;
    for (; *value; ++value) {
        if (*value < '0' || *value > '9' ||
            result > (UINT64_MAX - (*value - '0')) / 10)
            return false;
        result = result * 10 + *value - '0';
    }
    return true;
}

bool configuration() {
    os::File input;
    if (input.open("/etc/core", O_RDONLY)) return false;
    // Reject a policy that another user could replace with unsafe limits.
    if (input.refresh() || input.getType() != os::File::Type::File ||
        input.getUid() || (input.getMode() & 0022))
        return false;
    char text[2048]{};
    const auto count = input.read(text, sizeof(text) - 1);
    if (count <= 0 || count == sizeof(text) - 1) return false;
    for (char* line = text; *line;) {
        char* next = line;
        while (*next && *next != '\n') ++next;
        char* end = next;
        if (*next) *next++ = 0;
        while (end > line &&
               (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        while (*line == ' ' || *line == '\t') ++line;
        if (*line && *line != '#') {
            char* equal = line;
            while (*equal && *equal != '=') ++equal;
            if (!*equal) return false;
            *equal++       = 0;
            uint64_t value = 0;
            if (os::Runtime::stringsEqual(line, "directory")) {
                if (equal[0] != '/' ||
                    os::Runtime::stringLength(equal) >= sizeof(directory))
                    return false;
                os::Runtime::copyString(directory, equal, sizeof(directory));
            } else {
                if (!number(equal, value)) return false;
                if (os::Runtime::stringsEqual(line, "enabled"))
                    policy.enabled = value;
                else if (os::Runtime::stringsEqual(line, "maximum_bytes"))
                    policy.maximumBytes = value;
                else if (os::Runtime::stringsEqual(line, "maximum_concurrent"))
                    policy.maximumConcurrent = value;
                else if (os::Runtime::stringsEqual(line, "timeout_ms"))
                    policy.timeoutMilliseconds = value;
                else if (os::Runtime::stringsEqual(line, "maximum_space"))
                    maximumSpace = value;
                else
                    return false;
            }
        }
        line = next;
    }
    return true;
}
bool controlledDirectory() {
    // Every ancestor must be root-owned and not writable by other users.
    // OSInfo's current VFS does not resolve symbolic links; require real
    // directories.
    os::File root;
    if (root.refresh("/") || root.getType() != os::File::Type::Directory ||
        root.getUid() || (root.getMode() & 0022))
        return false;
    char path[sizeof(directory)]{};
    size_t length = 0;
    for (size_t i = 0;; ++i) {
        const char c = directory[i];
        if ((c == '/' && i) || !c) {
            path[length] = 0;
            if (!length ||
                (length >= 2 && path[length - 1] == '.' &&
                 (path[length - 2] == '/' || path[length - 2] == '.')))
                return false;
            os::File item;
            if (item.refresh(path)) {
                if (os::Dir().create(path, c ? 0755 : 0700) ||
                    item.refresh(path))
                    return false;
            }
            if (item.getType() != os::File::Type::Directory || item.getUid() ||
                (item.getMode() & 0022))
                return false;
            if (!c && (item.getMode() & 0077)) return false;
        }
        if (!c) break;
        if (length + 1 >= sizeof(path)) return false;
        path[length++] = c;
    }
    return true;
}
bool spaceAvailable(uint64_t needed) {
    uint64_t used = 0;
    os::Dir dir;
    if (dir.open(directory)) return false;
    for (uint64_t index = 0; index < 4096; ++index) {
        os::Dir::Entry entry;
        const int result = dir.readNext(entry);
        if (result < 0) return false;
        if (!result)
            return used <= maximumSpace && needed <= maximumSpace - used;
        if (os::Runtime::stringsEqual(entry.getName(), ".") ||
            os::Runtime::stringsEqual(entry.getName(), ".."))
            continue;
        char path[512];
        os::Runtime::copyString(path, directory, sizeof(path));
        size_t n  = os::Runtime::stringLength(path);
        path[n++] = '/';
        path[n]   = 0;
        os::Runtime::copyString(path + n, entry.getName(), sizeof(path) - n);
        os::File file;
        if (file.refresh(path)) return false;
        if (file.getSize() > maximumSpace - used) return false;
        used += file.getSize();
    }
    return false;
}
void decimal(char* output, size_t& used, uint64_t value) {
    char reversed[20];
    unsigned n = 0;
    do {
        reversed[n++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (n) output[used++] = reversed[--n];
    output[used] = 0;
}
bool writeCore(const os::wire::CoreTicket& ticket) {
    if (!controlledDirectory() || !spaceAvailable(ticket.bytes)) return false;
    char final[320]{};
    os::Runtime::copyString(final, directory, sizeof(final));
    size_t used         = os::Runtime::stringLength(final);
    const char prefix[] = "/core.";
    for (char c : prefix)
        if (c) final[used++] = c;
    decimal(final, used, ticket.pid);
    final[used++] = '.';
    decimal(final, used, ticket.started);
    final[used++] = '.';
    decimal(final, used, ticket.id);
    char partial[336];
    os::Runtime::copyString(partial, final, sizeof(partial));
    os::Runtime::copyString(partial + used, ".partial", sizeof(partial) - used);
    os::File file;
    if (file.open(partial, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600))
        return false;
    bool success = true;
    uint8_t buffer[4096];
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
        if (operation(os::TaskOperation::CoreRead, request, reply) !=
                static_cast<int>(bytes) ||
            file.write(buffer, bytes) != static_cast<ssize_t>(bytes)) {
            success = false;
            break;
        }
        offset += bytes;
    }
    if (success) success = !file.flush();
    // A hard link publishes a complete file atomically and refuses an
    // existing destination.
    if (success) success = !os::Fs().link(partial, final);
    file.close();
    os::Fs().removeFile(partial);
    return success;
}
void run() {
    fs = os::detail::RegistryClient(os::getBootstrapHandle())
             .lookup(os::fsServiceName, 30000);
    if (!fs) {
        os::Sys().log("[CORE] disabled: fs service unavailable\n");
        os::exitThread(0);
    }
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
    if (operation(os::TaskOperation::CoreConfigure, request, reply)) {
        os::Sys().log("[CORE] policy rejected\n");
        os::exitThread(0);
    }
    os::Sys().log("[CORE] bounded ELF writer online\n");
    while (true) {
        request        = {};
        reply          = {};
        request.length = 1;
        if (operation(os::TaskOperation::CoreAcquire, request, reply)) {
            os::sleep(50);
            continue;
        }
        os::wire::CoreTicket ticket{};
        os::Runtime::copyBytes(&ticket, &reply.data[1], sizeof(ticket));
        const bool success = writeCore(ticket);
        request            = {};
        reply              = {};
        request.length     = 2;
        request.data[0]    = ticket.id;
        request.data[1]    = success;
        const int finished =
            operation(os::TaskOperation::CoreFinish, request, reply);
        if (finished && success)
            os::Sys().log("[CORE] completion expired\n");
        else
            os::Sys().log(
                success ? "[CORE] core committed\n"
                        : "[CORE] write failed; process exit continues\n");
    }
}

bool startWriter() {
    return os::createThread(run, reinterpret_cast<uint64_t>(
                                     workerStack + sizeof(workerStack))) != 0;
}
}  // namespace
bool taskCore::start() { return startWriter(); }
