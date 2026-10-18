#include "os/task.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "os/abi.h"
#include "os/service.h"
#include "os/users.h"
#include "password.h"

namespace users {
class UserService {
   public:
    bool init() {
        if (!load()) return false;
        mkdir("/etc/inpkg", 0755); mkdir("/etc/inpkg/system", 0755);
        for (uint32_t i = 0; i < count_; ++i) {
            char path[128]; snprintf(path, sizeof(path), "/etc/inpkg/%s", accounts_[i].profile.name);
            mkdir(path, 0700); chown(path, accounts_[i].profile.uid, accounts_[i].profile.gid);
            const char* suffixes[] = {"", "/app", "/app/bin", "/app/lib", "/app/include"};
            for (const char* suffix : suffixes) {
                snprintf(path, sizeof(path), "%s%s", accounts_[i].profile.home, suffix);
                mkdir(path, *suffix ? 0755 : accounts_[i].profile.uid ? 0755 : 0700);
                chown(path, accounts_[i].profile.uid, accounts_[i].profile.gid);
            }
        }
        endpoint_ = os::createEndpoint();
        return endpoint_ && os::ServiceRegistry::registerService(os::ServiceRegistry::serviceRegistry(), "users", endpoint_);
    }
    void run() {
        os::System::log("[USERS] account and authentication service started\n");
        while (true) {
            os::IpcMessage request{}, reply{};
            if (os::receiveMessage(endpoint_, request)) continue;
            reply.data[0] = 1; reply.length = 1;
            if (request.length >= (sizeof(os::UserCredentials) + 7) / 8) handle(request, reply);
            os::replyMessage(endpoint_, request.replyToken, reply);
            Password::erase(&request, sizeof(request));
        }
    }
   private:
    class Account {
        friend class UserService;
       private:
        os::UserProfile profile{};
        char hash[128]{};
    };
    static bool validName(const char* name) {
        if (!*name || strlen(name) > 31 || (name[0] >= '0' && name[0] <= '9')) return false;
        for (const char* p = name; *p; ++p)
            if (!(*p >= 'a' && *p <= 'z') && !(*p >= 'A' && *p <= 'Z') &&
                !(*p >= '0' && *p <= '9') && *p != '_' && *p != '-') return false;
        return true;
    }
    Account* find(const char* name, uint32_t uid) {
        for (uint32_t i = 0; i < count_; ++i)
            if (*name ? !strcmp(accounts_[i].profile.name, name) : accounts_[i].profile.uid == uid) return &accounts_[i];
        return nullptr;
    }
    bool load() {
        FILE* input = fopen("/etc/passwd", "r");
        if (!input) return false;
        char line[512]; count_ = 0;
        while (count_ < 32 && fgets(line, sizeof(line), input)) {
            if (line[0] == '#' || !strchr(line, ':')) continue;
            char* fields[7]{line}; unsigned columns = 1;
            for (char* p = line; *p; ++p) {
                if (*p == ':' && columns < 7) { *p = 0; fields[columns++] = p + 1; }
                else if (*p == '\n' || *p == '\r') *p = 0;
            }
            if (columns != 7 || !validName(fields[0])) continue;
            Account& account = accounts_[count_++];
            account = {};
            account.profile.uid = strtoul(fields[2], nullptr, 10);
            account.profile.gid = strtoul(fields[3], nullptr, 10);
            if (account.profile.uid >= 65535 || account.profile.gid >= 65535) { --count_; continue; }
            os::Runtime::copyString(account.profile.name, fields[0], sizeof(account.profile.name));
            os::Runtime::copyString(account.profile.home, fields[5], sizeof(account.profile.home));
            os::Runtime::copyString(account.profile.shell, fields[6], sizeof(account.profile.shell));
            strcpy(account.hash, "!");
        }
        fclose(input);
        input = fopen("/etc/shadow", "r");
        if (!input) return false;
        while (fgets(line, sizeof(line), input)) {
            char* separator = strchr(line, ':'); if (!separator) continue;
            *separator++ = 0;
            char* end = separator; while (*end && *end != ':' && *end != '\n' && *end != '\r') ++end; *end = 0;
            Account* account = find(line, 0);
            if (account) os::Runtime::copyString(account->hash, separator, sizeof(account->hash));
        }
        fclose(input);
        return find("root", 0) != nullptr;
    }
    bool saveShadow() {
        FILE* output = fopen("/etc/.shadow.tmp", "w");
        if (!output) return false;
        bool ok = chmod("/etc/.shadow.tmp", 0600) == 0;
        for (uint32_t i = 0; i < count_; ++i)
            ok = fprintf(output, "%s:%s:0:0:99999:7:::\n", accounts_[i].profile.name, accounts_[i].hash) >= 0 && ok;
        ok = fclose(output) == 0 && ok;
        return ok && rename("/etc/.shadow.tmp", "/etc/shadow") == 0;
    }
    bool allowedSudo(const Account& account) {
        if (account.profile.uid == 0) return true;
        FILE* file = fopen("/etc/sudoers", "r");
        if (!file) return false;
        char line[256]; bool allowed = false;
        while (fgets(line, sizeof(line), file)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            char* p = line; while (*p == ' ' || *p == '\t') ++p;
            char* end = p; while (*end && *end != ' ' && *end != '\t' && *end != '\n') ++end;
            if (!*end) continue;
            *end++ = 0;
            if (strcmp(p, account.profile.name)) continue;
            while (*end == ' ' || *end == '\t') ++end;
            char* newline = strchr(end, '\n'); if (newline) *newline = 0;
            if (!strcmp(end, "ALL=(ALL) ALL")) { allowed = true; break; }
        }
        fclose(file); return allowed;
    }
    void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        os::UserCredentials input{};
        memcpy(&input, request.data, sizeof(input));
        if (!memchr(input.name, 0, sizeof(input.name)) || !memchr(input.password, 0, sizeof(input.password)) ||
            !memchr(input.replacement, 0, sizeof(input.replacement))) return;
        os::TaskInfo caller{};
        if (os::Process::queryTask(request.senderPid, caller)) return;
        const auto operation = static_cast<os::UserOperation>(request.type);
        Account* account = find(input.name, input.id);
        if (operation == os::UserOperation::Who) {
            os::TaskInfo task{};
            if (os::Process::enumerateTask(input.id, task) || task.state == 2) return;
            const char* name = strrchr(task.name, '/'); name = name ? name + 1 : task.name;
            if (strcmp(name, "sh")) return;
            account = find("", task.uid);
            reply.data[40] = task.id;
        }
        if (!account) return;
        if (operation == os::UserOperation::Login) {
            if (!Password::verify(input.password, account->hash) ||
                os::Process::setCredentials( request.senderPid,
                                 account->profile.uid, account->profile.gid)) return;
        } else if (operation == os::UserOperation::Sudo) {
            account = find("", caller.uid);
            if (!account || !allowedSudo(*account) || (caller.effectiveUid != 0 && !Password::verify(input.password, account->hash)) ||
                os::Process::setCredentials( request.senderPid, 0, 0, 1)) return;
        } else if (operation == os::UserOperation::Password) {
            if (caller.effectiveUid && (caller.uid != account->profile.uid || !Password::verify(input.password, account->hash))) return;
            if (strlen(input.replacement) < 1) return;
            uint8_t salt[16];
            const uint64_t values[2] = {os::Clock::getMonotonicTime() ^ ++saltCounter_, os::Clock::getWallClockTime() ^ request.senderPid};
            memcpy(salt, values, sizeof(salt));
            char old[128]; strcpy(old, account->hash);
            Password::encode(input.replacement, salt, account->hash);
            if (!saveShadow()) { strcpy(account->hash, old); Password::erase(old, sizeof(old)); return; }
            Password::erase(old, sizeof(old));
        } else if (operation != os::UserOperation::Lookup && operation != os::UserOperation::Who) return;
        reply.data[0] = 0;
        memcpy(&reply.data[1], &account->profile, sizeof(account->profile));
        reply.length = operation == os::UserOperation::Who ? 41 : 1 + (sizeof(account->profile) + 7) / 8;
        Password::erase(&input, sizeof(input));
    }
    Account accounts_[32]{};
    uint32_t count_ = 0;
    uint64_t endpoint_ = 0, saltCounter_ = 0;
};
}

namespace { users::UserService service; }
int main(int, char**) { if (!service.init()) return 1; service.run(); return 0; }
