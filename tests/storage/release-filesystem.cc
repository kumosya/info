#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <os/file.h>
#include <os/service.h>
#include <os/task.h>
#include <unistd.h>
#include "service/common/queries.h"

namespace {
constexpr const char* target = "/mnt/storage-gate";
constexpr size_t chunkSize = 8192;
constexpr unsigned chunks = 8;
const char* files[] = {"/mnt/storage-gate/worker-a", "/mnt/storage-gate/worker-b"};
const char* ready[] = {"/release-ready-a", "/release-ready-b"};
constexpr const char* startPath = "/release-start";
constexpr const char* executable = "/usr/bin/m0-storage-probe";

bool check(bool ok, const char* message) {
    if (!ok) {
        os::Sys().log("[RELEASE FS] FAIL ");
        os::Sys().log(message);
        os::Sys().log("\n");
    }
    return ok;
}

unsigned char pattern(unsigned worker, unsigned chunk, size_t offset) {
    return (worker * 113 + chunk * 71 + offset * 17 + offset / 257) & 255;
}

bool publish(const char* path) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    return fd >= 0 && close(fd) == 0;
}

bool waitFor(const char* path) {
    for (unsigned tries = 0; tries < 20; ++tries) {
        if (access(path, F_OK) == 0) return true;
        sleep(1);
    }
    return false;
}

bool worker(unsigned id) {
    if (!publish(ready[id]) || !waitFor(startPath)) return false;
    const int file = open(files[id], O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (file < 0) return false;
    unsigned char data[chunkSize];
    for (unsigned chunk = 0; chunk < chunks; ++chunk) {
        for (size_t i = 0; i < sizeof(data); ++i) data[i] = pattern(id, chunk, i);
        size_t offset = 0;
        while (offset < sizeof(data)) {
            const auto written = write(file, data + offset, sizeof(data) - offset);
            if (written <= 0) return false;
            offset += written;
        }
        if (fsync(file)) return false;
    }
    return close(file) == 0;
}

bool verify(unsigned id) {
    const int file = open(files[id], O_RDONLY);
    if (file < 0) return false;
    unsigned char data[chunkSize];
    for (unsigned chunk = 0; chunk < chunks; ++chunk) {
        size_t offset = 0;
        while (offset < sizeof(data)) {
            const auto count = read(file, data + offset, sizeof(data) - offset);
            if (count <= 0) return false;
            offset += count;
        }
        for (size_t i = 0; i < sizeof(data); ++i)
            if (data[i] != pattern(id, chunk, i)) return false;
    }
    const bool end = read(file, data, 1) == 0;
    return close(file) == 0 && end;
}

bool run() {
    os::Mount mount;
    if (!check(geteuid() == 0 && !mount.create("/dev/vdb", target, "ext3"),
               "mount independent ext3 disk")) return false;
    os::detail::TaskControl task;
    uint64_t children[2]{};
    for (unsigned id = 0; id < 2; ++id) {
        const char* argv[] = {executable, id ? "--worker-b" : "--worker-a"};
        children[id] = task.executeWithIo(executable, 2, argv, 0, 0);
        if (!check(children[id] != 0, "start independent writer"))
            return false;
    }
    if (!check(waitFor(ready[0]) && waitFor(ready[1]) && publish(startPath),
               "both independent writers reached the start gate")) return false;
    bool workersPassed = true;
    for (const auto child : children) {
        int status = 1;
        workersPassed &= task.wait(child, &status) == 0 && status == 0;
    }
    if (!check(workersPassed && verify(0) && verify(1),
               "two process writes and fsync preserve distinct data")) return false;
    os::Sys().log("[RELEASE FS] concurrent writers passed\n");

    const int openFile = open(files[0], O_RDONLY);
    if (!check(openFile >= 0 && mount.close() != 0, "open file blocks unmount"))
        return false;
    if (!check(!close(openFile) && !mount.close() &&
                   access(files[0], F_OK) == -1 && errno == ENOENT,
               "unmount hides volume data")) return false;
    if (!check(!mount.create("/dev/vdb", target, "ext3") && verify(0) &&
                   verify(1) && !mount.close(),
               "remount retains both writers' data")) return false;
    os::Sys().log("[RELEASE FS] unmount and remount passed\n");
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "--worker-a") ||
                      !std::strcmp(argv[1], "--worker-b")))
        return check(worker(argv[1][9] == 'b'), "independent writer") ? 0 : 1;
    os::Task task;
    if (task.openCurrent() == 0 && task.getTerminalKind() == 1 &&
        task.getTerminalIndex() == 0 && run())
        os::Sys().log("[RELEASE FS] PASS\n");
    for (;;) sleep(1);
}
