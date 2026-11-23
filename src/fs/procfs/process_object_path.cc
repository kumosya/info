/* 服务对象的成员函数实现。 */
#include "service/fs/procfs/filesystem.h"

namespace fs::procfs {

    /* 匹配并推进进程对象路径前缀。 */
    bool ProcessObjectPath::prefix(const char*& cursor, const char* text) {
        const char* next = cursor;
        while (*text) {
            if (*next++ != *text++) {
                return false;
            }
        }

        cursor = next;
        return true;
    }

    /* 解析路径中的无符号编号。 */
    bool ProcessObjectPath::number(const char*& cursor, uint64_t& value) {
        if (*cursor < '1' || *cursor > '9') {
            return false;
        }

        value = 0;
        do {
            const unsigned digit = *cursor++ - '0';
            if (value > (UINT64_MAX - digit) / 10) {
                return false;
            }

            value = value * 10 + digit;
        } while (*cursor >= '0' && *cursor <= '9');
        return true;
    }

    /* 解析调用者允许重新打开的进程文件对象。 */
    ProcessObjectPath::Kind ProcessObjectPath::parse(uint64_t& object) {
        const char* path = path_;
        const uint64_t caller = caller_;
        const char* cursor = path;
        if (!prefix(cursor, "/proc/")) {
            return Other;
        }

        uint64_t owner;
        if (prefix(cursor, "self")) {
            owner = caller;
        } else if (!number(cursor, owner)) {
            return Other;
        }

        if (!prefix(cursor, "/object")) {
            return Other;
        }

        if (*cursor && *cursor != '/') {
            return Other;
        }

        if (owner != caller || !caller) {
            return Denied;
        }

        if (!*cursor || !*++cursor) {
            object = 0;
            return Directory;
        }

        if (!number(cursor, object) || *cursor) {
            return Denied;
        }

        return File;
    }

}  // namespace fs::procfs
