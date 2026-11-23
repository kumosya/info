/* 在独立测试镜像中切换真实用户身份，检查服务端权限和 libc 查询结果。 */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <os/service.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int permissionFailure(int line) {
    char text[96];
    snprintf(text, sizeof(text),
             "[STORAGE-TEST] PERMISSION FAIL line=%d errno=%d\n", line, errno);
    os::Sys().log(text);
    return line;
}

#define REQUIRE(value)                          \
    do {                                        \
        if (!(value))                           \
            return permissionFailure(__LINE__); \
    } while (0)

/* 创建文件并设定测试需要的身份和权限，内容供后续读取比对。 */
static bool createFile(const char* path, mode_t mode, uid_t uid, gid_t gid) {
    const int descriptor = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0600);
    if (descriptor < 0) {
        return false;
    }

    const bool written = write(descriptor, "p", 1) == 1;
    const bool closed = close(descriptor) == 0;
    return written && closed && chmod(path, mode) == 0 &&
           chown(path, uid, gid) == 0;
}

/* 保留测试进程的恢复身份，每轮操作使用指定的真实和有效用户编号。 */
static bool identity(uid_t uid, gid_t gid, bool supplementary) {
    const gid_t group = 2001;
    return seteuid(0) == 0 && setresuid(0, 0, 0) == 0 &&
           setresgid(0, 0, 0) == 0 &&
           setgroups(supplementary ? 1 : 0, supplementary ? &group : nullptr) ==
               0 &&
           setresgid(gid, gid, 0) == 0 && setresuid(uid, uid, 0) == 0;
}

/* 覆盖属主、主组、附加组、其他用户、目录搜索和 sticky 目录覆盖。 */
int filesystemPermissions() {
    REQUIRE(geteuid() == 0);
    REQUIRE(mkdir("/tmp/permissions-test", 0755) == 0);
    REQUIRE(chmod("/tmp/permissions-test", 0755) == 0);
    REQUIRE(mkdir("/tmp/permissions-test/locked", 0700) == 0);
    REQUIRE(mkdir("/tmp/permissions-test/sticky", 0777) == 0);
    REQUIRE(chmod("/tmp/permissions-test/sticky", 01777) == 0);
    REQUIRE(chown("/tmp/permissions-test/sticky", 1003, 2003) == 0);
    REQUIRE(createFile("/tmp/permissions-test/private", 0600, 1001, 2001));
    REQUIRE(createFile("/tmp/permissions-test/group", 0640, 1001, 2001));
    REQUIRE(createFile("/tmp/permissions-test/other", 0004, 1001, 2001));
    REQUIRE(createFile("/tmp/permissions-test/root", 0600, 0, 0));
    REQUIRE(createFile("/tmp/permissions-test/locked/file", 0644, 0, 0));
    REQUIRE(createFile("/tmp/permissions-test/sticky/owner", 0600, 1001, 2001));
    REQUIRE(createFile("/tmp/permissions-test/sticky/other", 0600, 1002, 2002));
    REQUIRE(
        createFile("/tmp/permissions-test/sticky/remove", 0600, 1001, 2001));
    const int inherited = open("/tmp/permissions-test/root", O_RDWR);
    REQUIRE(inherited >= 0);

    REQUIRE(identity(1001, 2001, false));
    int descriptor = open("/tmp/permissions-test/private", O_RDWR);
    REQUIRE(descriptor >= 0 && fchmod(descriptor, 0600) == 0 &&
            close(descriptor) == 0);
    REQUIRE(access("/tmp/permissions-test/private", R_OK | W_OK) == 0);
    // 属主位优先，即使其他用户位允许读取，属主也使用自己的权限位。
    REQUIRE(access("/tmp/permissions-test/other", R_OK) == -1 &&
            errno == EACCES);
    REQUIRE(open("/tmp/permissions-test/other", O_RDONLY) < 0);
    REQUIRE(unlink("/tmp/permissions-test/sticky/other") < 0);
    REQUIRE(rename("/tmp/permissions-test/sticky/owner",
                   "/tmp/permissions-test/sticky/other") < 0);

    REQUIRE(identity(1002, 2002, true));
    REQUIRE(access("/tmp/permissions-test/group", R_OK) == 0);
    REQUIRE(access("/tmp/permissions-test/group", W_OK) == -1 &&
            errno == EACCES);
    descriptor = open("/tmp/permissions-test/group", O_RDONLY);
    REQUIRE(descriptor >= 0 && fchmod(descriptor, 0666) < 0 &&
            close(descriptor) == 0);
    REQUIRE(open("/tmp/permissions-test/group", O_WRONLY) < 0);
    REQUIRE(open("/tmp/permissions-test/private", O_RDONLY) < 0);
    REQUIRE(chdir("/tmp/permissions-test/locked") < 0);
    REQUIRE(open("/tmp/permissions-test/locked/file", O_RDONLY) < 0);
    REQUIRE(unlink("/tmp/permissions-test/sticky/owner") < 0);
    REQUIRE(open("/tmp/permissions-test/root", O_RDONLY) < 0);
    char value = 0;
    REQUIRE(read(inherited, &value, 1) == 1 && value == 'p');
    REQUIRE(write(inherited, "q", 1) == 1);

    REQUIRE(identity(1002, 2002, false));
    REQUIRE(access("/tmp/permissions-test/group", R_OK) < 0);
    REQUIRE(open("/tmp/permissions-test/group", O_RDONLY) < 0);
    REQUIRE(access("/tmp/permissions-test/other", R_OK) == 0);
    descriptor = open("/tmp/permissions-test/other", O_RDONLY);
    REQUIRE(descriptor >= 0 && close(descriptor) == 0);
    REQUIRE(unlink("/tmp/permissions-test/sticky/other") == 0);

    REQUIRE(identity(1003, 2003, false));
    REQUIRE(unlink("/tmp/permissions-test/sticky/remove") == 0);
    REQUIRE(identity(0, 0, false));
    REQUIRE(close(inherited) == 0);
    REQUIRE(access("/tmp/permissions-test/private", X_OK) == -1 &&
            errno == EACCES);
    const char* files[] = {"private", "group",       "other",
                           "root",    "locked/file", "sticky/owner"};
    for (const char* file : files) {
        char path[128];
        snprintf(path, sizeof(path), "/tmp/permissions-test/%s", file);
        REQUIRE(unlink(path) == 0);
    }

    REQUIRE(rmdir("/tmp/permissions-test/locked") == 0);
    REQUIRE(rmdir("/tmp/permissions-test/sticky") == 0);
    REQUIRE(rmdir("/tmp/permissions-test") == 0);
    os::Sys().log("[STORAGE-TEST] permissions passed\n");
    return 0;
}
