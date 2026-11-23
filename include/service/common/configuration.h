/*
 * 服务配置文本解析。
 */

#pragma once
#include <cstddef>

namespace os {
    /* 解析固定缓冲区中的配置文本，检查字符及键值边界。 */
    class ConfigurationText {
        public:
            static bool valid(const char* bytes, size_t size) {
                for (size_t i = 0; i < size; ++i) {
                    const unsigned char value =
                        static_cast<unsigned char>(bytes[i]);
                    if ((value < 32 && value != '\n' && value != '\r') ||
                        value == 127)
                        return false;
                    if (value == '\r' &&
                        (i + 1 == size || bytes[i + 1] != '\n'))
                        return false;
                }
                return true;
            }
            static bool absolutePath(const char* path) {
                if (!path || path[0] != '/' || !path[1]) return false;
                const char* begin = path + 1;
                for (const char* p = begin;; ++p) {
                    if (*p && *p != '/') {
                        if (static_cast<unsigned char>(*p) <= 32 || *p == 127 ||
                            *p == '\\')
                            return false;
                        continue;
                    }
                    const size_t length = p - begin;
                    if (!length || (length == 1 && begin[0] == '.') ||
                        (length == 2 && begin[0] == '.' && begin[1] == '.'))
                        return false;
                    if (!*p) return true;
                    begin = p + 1;
                }
            }
    };
}  // namespace os
