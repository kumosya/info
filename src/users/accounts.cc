/* 服务对象的成员函数实现。 */
#include "service/users/users.h"

namespace users {

    /* 加载账户资料和密码记录。 */
    bool Accounts::load(const char* passwd, const char* shadow) {
        clear();
        if (!read(passwd, false) || !count_ || !read(shadow, true)) {
            clear();
            return false;
        }

        for (uint32_t i = 0; i < count_; ++i) {
            if (!entries_[i].shadow) {
                clear();
                return false;
            }
        }

        const Account* root = find("root", 0);
        if (!root || root->profile.uid || root->profile.gid) {
            clear();
            return false;
        }

        loaded_ = true;
        return true;
    }

    /* 按名称或用户编号查找账户。 */
    Account* Accounts::find(const char* name, uint32_t uid) {
        for (uint32_t i = 0; i < count_; ++i) {
            if (*name ? !std::strcmp(entries_[i].profile.name, name)
                      : entries_[i].profile.uid == uid) {
                return &entries_[i];
            }
        }

        return nullptr;
    }

    /* 读取自动登录配置对应的账户。 */
    Account* Accounts::automatic() {
        return loaded_ && count_ == 1 &&
                       !std::strcmp(entries_[0].profile.name, "root") &&
                       !entries_[0].profile.uid && !entries_[0].profile.gid &&
                       entries_[0].shadow && !*entries_[0].hash
                   ? &entries_[0]
                   : nullptr;
    }

    /* 清空账户集合与密码副本。 */
    void Accounts::clear() {
        Password::erase(entries_, sizeof(entries_));
        count_ = 0;
        loaded_ = false;
    }

    /* 检查账户名称的字符与长度。 */
    bool Accounts::name(const char* value) {
        const size_t length = std::strlen(value);
        if (!length || length >= sizeof(services::wire::UserProfile::name) ||
            (*value >= '0' && *value <= '9')) {
            return false;
        }

        for (; *value; ++value) {
            if (!(*value >= 'a' && *value <= 'z') &&
                !(*value >= 'A' && *value <= 'Z') &&
                !(*value >= '0' && *value <= '9') && *value != '_' &&
                *value != '-') {
                return false;
            }
        }

        return true;
    }

    /* 解析账户资料中的身份编号。 */
    bool Accounts::number(const char* value, uint32_t& result) {
        if (!*value) {
            return false;
        }

        result = 0;
        for (; *value; ++value) {
            if (*value < '0' || *value > '9' ||
                result > (65534U - (*value - '0')) / 10) {
                return false;
            }

            result = result * 10 + *value - '0';
        }

        return true;
    }

    /* 解析一行账户或密码记录。 */
    bool Accounts::row(char* line, bool shadow) {
        char* fields[9]{line};
        unsigned columns = 1;
        for (char* p = line; *p; ++p) {
            if (*p == ':') {
                if (columns == 9) {
                    return false;
                }

                *p = 0;
                fields[columns++] = p + 1;
            }
        }

        if (columns != (shadow ? 9U : 7U) || !name(fields[0])) {
            return false;
        }

        if (shadow) {
            Account* account = find(fields[0], 0);
            if (!account || account->shadow ||
                std::strlen(fields[1]) >= sizeof(account->hash) ||
                !Password(fields[1]).valid()) {
                return false;
            }

            for (unsigned i = 2; i < 9; ++i) {
                const char* p = fields[i];
                if (*p == '-') {
                    if (std::strcmp(p, "-1")) {
                        return false;
                    }

                    continue;
                }

                for (; *p; ++p) {
                    if (*p < '0' || *p > '9') {
                        return false;
                    }
                }
            }

            std::strcpy(account->hash, fields[1]);
            account->shadow = true;
            return true;
        }

        uint32_t uid, gid;
        if (count_ == capacity || find(fields[0], 0) ||
            !number(fields[2], uid) || !number(fields[3], gid) ||
            find("", uid) || std::strcmp(fields[1], "x") ||
            !os::ConfigurationText(fields[5]).absolutePath() ||
            !std::strcmp(fields[5], "/") ||
            !os::ConfigurationText(fields[6]).absolutePath() ||
            std::strlen(fields[5]) >=
                sizeof(services::wire::UserProfile::home) ||
            std::strlen(fields[6]) >=
                sizeof(services::wire::UserProfile::shell)) {
            return false;
        }

        Account& account = entries_[count_++];
        account.profile.uid = uid;
        account.profile.gid = gid;
        std::strcpy(account.profile.name, fields[0]);
        std::strcpy(account.profile.home, fields[5]);
        std::strcpy(account.profile.shell, fields[6]);
        return true;
    }

    /* 逐行读取账户配置文件。 */
    bool Accounts::read(const char* path, bool shadow) {
        FILE* file = std::fopen(path, "r");
        if (!file) {
            return false;
        }

        char line[512]{};
        size_t used = 0;
        bool ok = true;
        while (true) {
            const int c = std::fgetc(file);
            if (c == EOF || c == '\n') {
                if (used && line[used - 1] == '\r') {
                    --used;
                }

                line[used] = 0;
                if ((used && *line != '#' && !row(line, shadow)) ||
                    std::ferror(file)) {
                    ok = false;
                    break;
                }

                used = 0;
                if (c == EOF) {
                    break;
                }
            } else {
                if (used + 1 == sizeof(line) || c == 0 ||
                    (c < 32 && c != '\r') || c == 127 ||
                    (used && line[used - 1] == '\r')) {
                    ok = false;
                    break;
                }

                line[used++] = static_cast<char>(c);
            }
        }

        Password::erase(line, sizeof(line));
        if (std::fclose(file)) {
            ok = false;
        }

        return ok;
    }

}  // namespace users
