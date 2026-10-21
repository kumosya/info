#include "os/task.h"
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include "os/abi.h"
#include "os/service.h"
#include "os/users.h"
#include "password.h"
#include "accounts.h"
#include "sessions.h"

namespace users {
class UserService {
   public:
    bool init() {
        if (!accounts_.load()) { os::System::log("[USERS] cannot load account database\n"); return false; }
        if (!directories_.initialize()) { os::System::log("[USERS] cannot initialize system directories\n"); return false; }
        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            if (!directories_.prepare(accounts_.at(i).profile)) {
                os::System::log("[USERS] cannot prepare home: ");
                os::System::log(accounts_.at(i).profile.home);
                os::System::log("\n");
                return false;
            }
        }
        if (!sessions_.initialize()) { os::System::log("[USERS] cannot recover authenticated sessions\n"); return false; }
        endpoint_ = os::Endpoint::create();
        return endpoint_ && os::ServiceRegistry::registerService(os::ServiceRegistry::serviceRegistry(), "users", endpoint_);
    }
    void run() {
        os::System::log("[USERS] account and authentication service started\n");
        while (true) {
            sessions_.sweep();
            os::IpcMessage request{}, reply{};
            if (os::receiveMessage(endpoint_, request, os::Clock::getMonotonicTime() + 100000000ULL)) continue;
            reply.data[0] = 1; reply.length = 1;
            if (request.length >= (sizeof(os::UserCredentials) + 7) / 8) handle(request, reply);
            os::replyMessage(endpoint_, request.replyToken, reply);
            Password::erase(&request, sizeof(request));
        }
    }
   private:
    Account* find(const char* name, uint32_t uid) {
        return accounts_.find(name, uid);
    }
    bool saveShadow() {
        FILE* output = std::fopen("/etc/.shadow.tmp", "w");
        if (!output) return false;
        bool ok = chmod("/etc/.shadow.tmp", 0600) == 0;
        for (uint32_t i = 0; i < accounts_.count(); ++i)
            ok = std::fprintf(output, "%s:%s:0:0:99999:7:::\n", accounts_.at(i).profile.name, accounts_.at(i).hash) >= 0 && ok;
        ok = std::fclose(output) == 0 && ok;
        return ok && std::rename("/etc/.shadow.tmp", "/etc/shadow") == 0;
    }
    bool allowedSudo(const Account& account) {
        if (account.profile.uid == 0) return true;
        FILE* file = fopen("/etc/sudoers", "r");
        if (!file) return false;
        char line[256]; bool allowed = false;
        while (std::fgets(line, sizeof(line), file)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            char* p = line; while (*p == ' ' || *p == '\t') ++p;
            char* end = p; while (*end && *end != ' ' && *end != '\t' && *end != '\n') ++end;
            if (!*end) continue;
            *end++ = 0;
            if (std::strcmp(p, account.profile.name)) continue;
            while (*end == ' ' || *end == '\t') ++end;
            char* newline = std::strchr(end, '\n'); if (newline) *newline = 0;
            if (!std::strcmp(end, "ALL=(ALL) ALL")) { allowed = true; break; }
        }
        fclose(file); return allowed;
    }
    void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        os::UserCredentials input{};
        struct Erase { os::UserCredentials& value; ~Erase() { Password::erase(&value, sizeof(value)); } } erase{input};
        std::memcpy(&input, request.data, sizeof(input));
        if (!std::memchr(input.name, 0, sizeof(input.name)) || !std::memchr(input.password, 0, sizeof(input.password)) ||
            !std::memchr(input.replacement, 0, sizeof(input.replacement))) return;
        os::TaskInfo caller{};
        if (os::Task::query(request.senderPid, caller)) return;
        const auto operation = static_cast<os::UserOperation>(request.type);
        Account* account = find(input.name, input.id);
        if (operation == os::UserOperation::AutoLogin) {
            account = accounts_.automatic();
            if (!account || caller.effectiveUid || caller.managedService || !caller.terminalKind ||
                caller.sessionId != caller.id || caller.taskGroupId != caller.id) return;
        }
        if (operation == os::UserOperation::Who) {
            os::TaskInfo task{};
            if (os::Task::query(input.id, task) || task.state == 2) return;
            const char* name = std::strrchr(task.name, '/'); name = name ? name + 1 : task.name;
            if (std::strcmp(name, "sh")) return;
            account = find("", task.uid);
            reply.data[40] = task.id;
        }
        if (!account) return;
        if (operation == os::UserOperation::Login || operation == os::UserOperation::AutoLogin) {
            if (operation == os::UserOperation::Login && !Password::verify(input.password, account->hash)) return;
            // Reserve all fallible session resources before changing credentials.
            if (!sessions_.prepare(request.senderPid, account->profile)) return;
            if (os::SessionIdentity::establish(request.senderPid, account->profile.uid, account->profile.gid)) {
                sessions_.cancel(account->profile); return;
            }
            sessions_.commit(request.senderPid, account->profile);
        } else if (operation == os::UserOperation::BeginSession) {
            if (caller.effectiveUid != account->profile.uid ||
                !sessions_.begin(request.senderPid, account->profile)) return;
        } else if (operation == os::UserOperation::Sudo) {
            account = find("", caller.uid);
            if (!account || !allowedSudo(*account) || (caller.effectiveUid != 0 && !Password::verify(input.password, account->hash)) ||
                os::Task::setCredentials( request.senderPid, 0, 0, 1)) return;
        } else if (operation == os::UserOperation::Password) {
            if (caller.effectiveUid && (caller.uid != account->profile.uid || !Password::verify(input.password, account->hash))) return;
            if (strlen(input.replacement) < 1) return;
            uint8_t salt[16];
            const uint64_t values[2] = {os::Clock::getMonotonicTime() ^ ++saltCounter_, os::Clock::getWallClockTime() ^ request.senderPid};
            std::memcpy(salt, values, sizeof(salt));
            char old[128]; std::strcpy(old, account->hash);
            Password::encode(input.replacement, salt, account->hash);
            if (!saveShadow()) { std::strcpy(account->hash, old); Password::erase(old, sizeof(old)); return; }
            Password::erase(old, sizeof(old));
        } else if (operation != os::UserOperation::Lookup && operation != os::UserOperation::Who) return;
        reply.data[0] = 0;
        std::memcpy(&reply.data[1], &account->profile, sizeof(account->profile));
        reply.length = operation == os::UserOperation::Who ? 41 : 1 + (sizeof(account->profile) + 7) / 8;
        Password::erase(&input, sizeof(input));
    }
    Accounts accounts_;
    DirectoryManager directories_;
    SessionManager sessions_{directories_, accounts_};
    uint64_t endpoint_ = 0, saltCounter_ = 0;
};
}

namespace { users::UserService service; }
int main(int, char**) { if (!service.init()) return 1; service.run(); return 0; }
