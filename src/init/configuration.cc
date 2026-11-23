/* 服务对象的成员函数实现。 */
#include "service/init/init.h"

namespace init {

    /* 读取终端、登录程序和图形会话的启动配置。 */
    bool SessionConfiguration::load() {
        os::File input;
        if (input.open("/etc/sessions", O_RDONLY)) {
            return false;
        }

        char bytes[1024]{};
        size_t used = 0;
        while (used + 1 < sizeof(bytes)) {
            const ssize_t count =
                input.read(bytes + used, sizeof(bytes) - used - 1);
            if (count < 0) {
                return false;
            }

            if (!count) {
                break;
            }

            used += count;
        }

        char extra;
        if (input.read(&extra, 1) != 0) {
            return false;
        }

        if (!os::ConfigurationText(bytes, used).valid()) {
            return false;
        }

        unsigned seen = 0;
        for (char* line = bytes; *line;) {
            char* end = line;
            while (*end && *end != '\n') {
                ++end;
            }

            const bool last = !*end;
            *end = 0;
            if (end > line && end[-1] == '\r') {
                end[-1] = 0;
            }

            if (*line && *line != '#') {
                char* field[3] = {line};
                unsigned count = 1;
                for (char* p = line; *p; ++p) {
                    if (*p == '|') {
                        if (count == 3) {
                            return false;
                        }

                        *p = 0;
                        field[count++] = p + 1;
                    }
                }

                if (count != 3 ||
                    !os::ConfigurationText(field[1]).absolutePath() ||
                    !*field[2] || os::Runtime::stringLength(field[1]) >= 128 ||
                    os::Runtime::stringLength(field[2]) >= 24) {
                    return false;
                }

                unsigned bit =
                    os::Runtime::stringsEqual(field[0], "terminal")    ? 1
                    : os::Runtime::stringsEqual(field[0], "login")     ? 2
                    : os::Runtime::stringsEqual(field[0], "shell")     ? 4
                    : os::Runtime::stringsEqual(field[0], "recovery")  ? 8
                    : os::Runtime::stringsEqual(field[0], "graphical") ? 16
                                                                       : 0;
                if (!bit || (seen & bit)) {
                    return false;
                }

                seen |= bit;
                // Legacy shell/recovery files use recovery for every
                // session.
                if (bit != 4) {
                    char* path = bit == 16  ? graphical
                                 : bit == 1 ? terminal
                                            : login;
                    char* name = bit == 16  ? graphicalName
                                 : bit == 1 ? terminalName
                                            : loginName;
                    os::Runtime::copyString(path, field[1], 128);
                    os::Runtime::copyString(name, field[2], 24);
                }
            }

            if (last) {
                break;
            }

            line = end + 1;
        }

        return seen == 3 || seen == 13 || seen == 19;
    }

}  // namespace init
