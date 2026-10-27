#ifndef _FS_FAT32_NAME_H
#define _FS_FAT32_NAME_H

#include <cstddef>
#include <cstdint>

namespace fat32::detail {

class NameCodec {
   public:
    static uint8_t checksum(const char* name) {
        uint8_t sum = 0;
        for (unsigned i = 0; i < 11; ++i)
            sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) +
                                       static_cast<uint8_t>(name[i]));
        return sum;
    }
    static bool equal(const char* left, const char* right) {
        for (size_t i = 0;; ++i) {
            const char a =
                left[i] >= 'a' && left[i] <= 'z' ? left[i] - 32 : left[i];
            const char b =
                right[i] >= 'a' && right[i] <= 'z' ? right[i] - 32 : right[i];
            if (a != b) return false;
            if (!a) return true;
        }
    }
    static bool toUtf16(const char* name, uint16_t* units, size_t& length) {
        length       = 0;
        size_t bytes = 0;
        while (name[bytes]) {
            uint32_t code    = static_cast<uint8_t>(name[bytes++]);
            unsigned extra   = 0;
            uint32_t minimum = 0;
            if (code >= 0xc2 && code <= 0xdf) {
                code &= 31;
                extra   = 1;
                minimum = 0x80;
            } else if (code >= 0xe0 && code <= 0xef) {
                code &= 15;
                extra   = 2;
                minimum = 0x800;
            } else if (code >= 0xf0 && code <= 0xf4) {
                code &= 7;
                extra   = 3;
                minimum = 0x10000;
            } else if (code >= 0x80)
                return false;
            for (unsigned i = 0; i < extra; ++i) {
                const uint8_t byte = name[bytes++];
                if ((byte & 0xc0) != 0x80) return false;
                code = (code << 6) | (byte & 63);
            }
            if (bytes > 255 || code < minimum || code > 0x10ffff ||
                (code >= 0xd800 && code <= 0xdfff) || code < 32 ||
                code == '"' || code == '*' || code == '/' || code == ':' ||
                code == '<' || code == '>' || code == '?' || code == '\\' ||
                code == '|')
                return false;
            if (code >= 0x10000) {
                if (length + 2 > 255) return false;
                code -= 0x10000;
                units[length++] = 0xd800 | (code >> 10);
                units[length++] = 0xdc00 | (code & 1023);
            } else {
                if (length == 255) return false;
                units[length++] = static_cast<uint16_t>(code);
            }
        }
        return length && name[bytes - 1] != ' ' && name[bytes - 1] != '.';
    }
    static bool toUtf8(const uint16_t* units, size_t capacity, char* name) {
        size_t output = 0;
        for (size_t i = 0; i < capacity && units[i] && units[i] != 0xffff;
             ++i) {
            uint32_t code = units[i];
            if (code >= 0xd800 && code <= 0xdbff) {
                if (++i >= capacity || units[i] < 0xdc00 || units[i] > 0xdfff)
                    return false;
                code = 0x10000 + ((code - 0xd800) << 10) + units[i] - 0xdc00;
            } else if (code >= 0xdc00 && code <= 0xdfff)
                return false;
            const unsigned count = code < 0x80      ? 1
                                   : code < 0x800   ? 2
                                   : code < 0x10000 ? 3
                                                    : 4;
            if (output + count > 255) return false;
            if (count == 1)
                name[output++] = static_cast<char>(code);
            else {
                name[output++] = static_cast<char>((count == 2   ? 0xc0
                                                    : count == 3 ? 0xe0
                                                                 : 0xf0) |
                                                   (code >> (6 * (count - 1))));
                for (unsigned n = count - 1; n; --n)
                    name[output++] = static_cast<char>(
                        0x80 | ((code >> (6 * (n - 1))) & 63));
            }
        }
        name[output] = 0;
        return output != 0;
    }
};
inline constexpr uint8_t unitOffsets[13] = {1,  3,  5,  7,  9,  14, 16,
                                            18, 20, 22, 24, 28, 30};

}  // namespace fat32::detail
#endif
