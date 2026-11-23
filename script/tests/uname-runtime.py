"""在宿主上验证 libc uname 的系统文件读取和错误处理。"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "lib/libc/posix_runtime.c").read_text()
implementation = source[source.index("static int read_system_text("):]
implementation = implementation[:implementation.index("\n}", implementation.index("int uname(")) + 2]
harness = r'''
#include <sys/utsname.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
static const char* values[] = {"OSInfo\n", "0.1.6\n", "osinfo 0.1.6\n", "amd64\n"};
static int selected, offset, missing, interrupted, closed;
static int fake_open(const char* path, int flags) {
    const char* paths[] = {"/sys/kernel/ostype", "/sys/kernel/osrelease",
                          "/sys/kernel/version", "/sys/kernel/machine"};
    assert(flags == (O_RDONLY | O_CLOEXEC));
    if (missing) { errno = ENOENT; return -1; }
    for (selected = 0; selected < 4; ++selected) {
        if (!strcmp(paths[selected], path)) { offset = 0; return 7; }
    }
    assert(0); return -1;
}
static ssize_t fake_read(int fd, void* buffer, size_t size) {
    assert(fd == 7 && size > 0);
    if (interrupted) { interrupted = 0; errno = EINTR; return -1; }
    if (!values[selected][offset]) return 0;
    ((char*)buffer)[0] = values[selected][offset++];
    return 1;
}
static int fake_close(int fd) { assert(fd == 7); ++closed; return 0; }
static int fake_hostname(char* output, size_t size) {
    assert(size > strlen("changed-host")); strcpy(output, "changed-host"); return 0;
}
#define open fake_open
#define read fake_read
#define close fake_close
#define gethostname fake_hostname
#define uname tested_uname
'''
checks = r'''
int main(void) {
    struct utsname result;
    interrupted = 1;
    assert(tested_uname(&result) == 0 && closed == 4);
    assert(!strcmp(result.sysname, "OSInfo") && !strcmp(result.release, "0.1.6"));
    assert(!strcmp(result.version, "osinfo 0.1.6"));
    assert(!strcmp(result.machine, "amd64") && !strcmp(result.nodename, "changed-host"));
    values[1] = "9.8.7\r\n";
    assert(tested_uname(&result) == 0 && !strcmp(result.release, "9.8.7"));
    struct utsname saved = result;
    missing = 1;
    assert(tested_uname(&result) == -1 && errno == ENOENT);
    assert(!memcmp(&saved, &result, sizeof(result)));
    missing = 0; values[0] = "\n";
    assert(tested_uname(&result) == -1 && errno == EIO);
    char large[sizeof(result.sysname) + 2];
    memset(large, 'x', sizeof(large) - 1); large[sizeof(large) - 1] = 0;
    values[0] = large;
    assert(tested_uname(&result) == -1 && errno == ENAMETOOLONG);
    assert(tested_uname(NULL) == -1 && errno == EFAULT);
    puts("uname runtime PASS");
}
'''
with tempfile.TemporaryDirectory(prefix="osinfo-uname-") as directory:
    test = Path(directory) / "uname.c"
    binary = Path(directory) / "uname"
    test.write_text(harness + implementation + checks)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
