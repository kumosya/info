#pragma once
#include <cstdint>
namespace vfs::server {
class ProcessObjectPath {
    static bool prefix(const char*& cursor, const char* text) {
        const char* next = cursor;
        while (*text)
            if (*next++ != *text++) return false;
        cursor = next;
        return true;
    }
    static bool number(const char*& cursor, uint64_t& value) {
        if (*cursor < '1' || *cursor > '9') return false;
        value = 0;
        do {
            const unsigned digit = *cursor++ - '0';
            if (value > (UINT64_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        } while (*cursor >= '0' && *cursor <= '9');
        return true;
    }

   public:
    enum Kind { Other, Directory, File, Denied };
    // Object numbers are FS-native descriptors, not POSIX FD numbers. The
    // authenticated caller can reopen only its own retained regular objects.
    static Kind parse(const char* path, uint64_t caller, uint64_t& object) {
        const char* cursor = path;
        if (!prefix(cursor, "/proc/")) return Other;
        uint64_t owner;
        if (prefix(cursor, "self"))
            owner = caller;
        else if (!number(cursor, owner))
            return Other;
        if (!prefix(cursor, "/object")) return Other;
        if (*cursor && *cursor != '/') return Other;
        if (owner != caller || !caller) return Denied;
        if (!*cursor || !*++cursor) {
            object = 0;
            return Directory;
        }
        if (!number(cursor, object) || *cursor) return Denied;
        return File;
    }
};
}  // namespace vfs::server
