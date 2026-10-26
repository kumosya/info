#include "service/users/messages.h"
#pragma once

#include "service/common/configuration.h"
#include <os/user.h>
#include "service/users/password.h"
#include <cstdio>
#include <cstring>

namespace users {
struct Account {
    os::wire::UserProfile profile{};
    char hash[128]{};
    bool shadow = false;
};

class Accounts {
   public:
    static constexpr uint32_t capacity = 32;
    bool load(const char* passwd = "/etc/passwd", const char* shadow = "/etc/shadow") {
        clear();
        if (!read(passwd, false) || !count_ || !read(shadow, true)) { clear(); return false; }
        for (uint32_t i = 0; i < count_; ++i) if (!entries_[i].shadow) { clear(); return false; }
        const Account* root = find("root", 0);
        if (!root || root->profile.uid || root->profile.gid) { clear(); return false; }
        loaded_ = true;
        return true;
    }
    uint32_t count() const { return count_; }
    Account& at(uint32_t index) { return entries_[index]; }
    Account* find(const char* name, uint32_t uid) {
        for (uint32_t i = 0; i < count_; ++i)
            if (*name ? !std::strcmp(entries_[i].profile.name, name) : entries_[i].profile.uid == uid) return &entries_[i];
        return nullptr;
    }
    Account* automatic() {
        return loaded_ && count_ == 1 && !std::strcmp(entries_[0].profile.name, "root") &&
            !entries_[0].profile.uid && !entries_[0].profile.gid && entries_[0].shadow && !*entries_[0].hash ? &entries_[0] : nullptr;
    }
   private:
    Account entries_[capacity]{};
    uint32_t count_ = 0;
    bool loaded_ = false;
    void clear() { Password::erase(entries_, sizeof(entries_)); count_ = 0; loaded_ = false; }
    static bool name(const char* value) {
        const size_t length = std::strlen(value);
        if (!length || length >= sizeof(os::wire::UserProfile::name) || (*value >= '0' && *value <= '9')) return false;
        for (; *value; ++value) if (!(*value >= 'a' && *value <= 'z') && !(*value >= 'A' && *value <= 'Z') &&
            !(*value >= '0' && *value <= '9') && *value != '_' && *value != '-') return false;
        return true;
    }
    static bool number(const char* value, uint32_t& result) {
        if (!*value) return false;
        result = 0;
        for (; *value; ++value) {
            if (*value < '0' || *value > '9' || result > (65534U - (*value - '0')) / 10) return false;
            result = result * 10 + *value - '0';
        }
        return true;
    }
    bool row(char* line, bool shadow) {
        char* fields[9]{line}; unsigned columns = 1;
        for (char* p = line; *p; ++p) if (*p == ':') {
            if (columns == 9) return false;
            *p = 0; fields[columns++] = p + 1;
        }
        if (columns != (shadow ? 9U : 7U) || !name(fields[0])) return false;
        if (shadow) {
            Account* account = find(fields[0], 0);
            if (!account || account->shadow || std::strlen(fields[1]) >= sizeof(account->hash) ||
                !Password::valid(fields[1])) return false;
            for (unsigned i = 2; i < 9; ++i) {
                const char* p = fields[i];
                if (*p == '-') { if (std::strcmp(p, "-1")) return false; continue; }
                for (; *p; ++p) if (*p < '0' || *p > '9') return false;
            }
            std::strcpy(account->hash, fields[1]); account->shadow = true;
            return true;
        }
        uint32_t uid, gid;
        if (count_ == capacity || find(fields[0], 0) || !number(fields[2], uid) || !number(fields[3], gid) ||
            find("", uid) || std::strcmp(fields[1], "x") || !os::ConfigurationText::absolutePath(fields[5]) ||
            !std::strcmp(fields[5], "/") || !os::ConfigurationText::absolutePath(fields[6]) ||
            std::strlen(fields[5]) >= sizeof(os::wire::UserProfile::home) || std::strlen(fields[6]) >= sizeof(os::wire::UserProfile::shell)) return false;
        auto& account = entries_[count_++];
        account.profile.uid = uid; account.profile.gid = gid;
        std::strcpy(account.profile.name, fields[0]); std::strcpy(account.profile.home, fields[5]); std::strcpy(account.profile.shell, fields[6]);
        return true;
    }
    bool read(const char* path, bool shadow) {
        FILE* file = std::fopen(path, "r");
        if (!file) return false;
        char line[512]{}; size_t used = 0; bool ok = true;
        while (true) {
            const int c = std::fgetc(file);
            if (c == EOF || c == '\n') {
                if (used && line[used - 1] == '\r') --used;
                line[used] = 0;
                if ((used && *line != '#' && !row(line, shadow)) || std::ferror(file)) { ok = false; break; }
                used = 0;
                if (c == EOF) break;
            } else {
                if (used + 1 == sizeof(line) || c == 0 || (c < 32 && c != '\r') || c == 127 || (used && line[used - 1] == '\r')) { ok = false; break; }
                line[used++] = static_cast<char>(c);
            }
        }
        Password::erase(line, sizeof(line));
        if (std::fclose(file)) ok = false;
        return ok;
    }
};
}  // namespace users
