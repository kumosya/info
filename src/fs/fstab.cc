#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include "service/common/protocol.h"
#include "service/fs/mount.h"

namespace fs::server {

    namespace fstab_fields {

        static bool space(char c);
        static bool field(const char*& cursor, char* output, size_t capacity);
        static bool number(const char* text, uint32_t& value);
    }  // namespace fstab_fields

    bool fstab_fields::space(char c) {
        return c == ' ' || c == '\t' || c == '\r';
    }

    bool fstab_fields::field(const char*& cursor, char* output,
                             size_t capacity) {
        while (fstab_fields::space(*cursor)) {
            ++cursor;
        }

        size_t length = 0;
        while (*cursor && *cursor != '\n' && *cursor != '#' &&
               !fstab_fields::space(*cursor)) {
            char value = *cursor++;
            if (value == '\\') {
                uint32_t decoded = 0;
                for (uint32_t index = 0; index < 3; ++index) {
                    if (*cursor < '0' || *cursor > '7') {
                        return false;
                    }

                    decoded =
                        decoded * 8 + static_cast<uint32_t>(*cursor++ - '0');
                }

                if (decoded == 0 || decoded > 255) {
                    return false;
                }

                value = static_cast<char>(decoded);
            }

            if (length + 1 == capacity) {
                return false;
            }

            output[length++] = value;
        }

        output[length] = '\0';
        return length != 0;
    }

    bool fstab_fields::number(const char* text, uint32_t& value) {
        value = 0;
        if (!*text) {
            return false;
        }

        for (; *text; ++text) {
            if (*text < '0' || *text > '9' || value > 100000) {
                return false;
            }

            value = value * 10 + static_cast<uint32_t>(*text - '0');
        }

        return true;
    }

    bool Fstab::canonicalTarget(char* target) {
        if (!target || target[0] != '/') {
            return false;
        }

        size_t read = 1, write = 1;
        while (target[read]) {
            while (target[read] == '/') {
                ++read;
            }

            const size_t begin = read;
            while (target[read] && target[read] != '/') {
                ++read;
            }

            const size_t length = read - begin;
            if (!length || (length == 1 && target[begin] == '.')) {
                continue;
            }

            if (length == 2 && target[begin] == '.' &&
                target[begin + 1] == '.') {
                if (write > 1) {
                    while (write > 1 && target[write - 1] != '/') {
                        --write;
                    }

                    if (write > 1) {
                        --write;
                    }
                }

                continue;
            }

            if (write > 1) {
                target[write++] = '/';
            }

            for (size_t i = 0; i < length; ++i) {
                target[write++] = target[begin + i];
            }
        }

        target[write] = '\0';
        return true;
    }

    bool Fstab::next(MountSpec& spec, bool& present) {
        const char*& text = cursor_;
        spec = {};
        present = false;
        const char* cursor = text;
        while (fstab_fields::space(*cursor)) {
            ++cursor;
        }

        if (*cursor && *cursor != '\n' && *cursor != '#') {
            if (!fstab_fields::field(cursor, spec.source,
                                     sizeof(spec.source)) ||
                !fstab_fields::field(cursor, spec.target,
                                     sizeof(spec.target)) ||
                !fstab_fields::field(cursor, spec.type, sizeof(spec.type)) ||
                !fstab_fields::field(cursor, spec.options,
                                     sizeof(spec.options)) ||
                !canonicalTarget(spec.target)) {
                return false;
            }

            char value[16]{};
            for (uint32_t index = 0; index < 2; ++index) {
                while (fstab_fields::space(*cursor)) {
                    ++cursor;
                }

                if (!*cursor || *cursor == '\n' || *cursor == '#') {
                    break;
                }

                uint32_t count = 0;
                if (!fstab_fields::field(cursor, value, sizeof(value)) ||
                    !fstab_fields::number(value, count) ||
                    count > (index == 0 ? 1U : 2U)) {
                    return false;
                }

                if (index == 0) {
                    spec.dump = count;
                } else {
                    spec.pass = count;
                }
            }

            while (fstab_fields::space(*cursor)) {
                ++cursor;
            }

            if (*cursor && *cursor != '\n' && *cursor != '#') {
                return false;
            }

            for (size_t pos = 0; spec.options[pos];) {
                size_t end = pos;
                while (spec.options[end] && spec.options[end] != ',') {
                    ++end;
                }

                if (end == pos) {
                    return false;
                }

                char option[32]{};
                if (end - pos >= sizeof(option)) {
                    return false;
                }

                for (size_t i = pos; i < end; ++i) {
                    option[i - pos] = spec.options[i];
                }

                if (os::Runtime::stringsEqual(option, "ro")) {
                    spec.readOnly = true;
                } else if (os::Runtime::stringsEqual(option, "rw")) {
                    spec.readOnly = false;
                } else if (os::Runtime::stringsEqual(option, "auto")) {
                    spec.automatic = true;
                } else if (os::Runtime::stringsEqual(option, "noauto")) {
                    spec.automatic = false;
                } else if (os::Runtime::stringsEqual(option, "nofail")) {
                    spec.required = false;
                } else if (os::Runtime::stringsEqual(option, "fail")) {
                    spec.required = true;
                } else if (!os::Runtime::stringsEqual(option, "defaults")) {
                    return false;
                }

                if (spec.options[end] == ',' && !spec.options[end + 1]) {
                    return false;
                }

                pos = spec.options[end] ? end + 1 : end;
            }

            present = true;
        }

        while (*cursor && *cursor != '\n') {
            ++cursor;
        }

        text = *cursor ? cursor + 1 : cursor;
        return true;
    }
}  // namespace fs::server
