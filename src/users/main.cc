#include "service/users/messages.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#define _GNU_SOURCE 1
#include <os/task.h>
#include <grp.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>
#include <os/service.h>
#include <os/user.h>
#include "service/users/password.h"
#include "service/users/accounts.h"
#include "service/users/sessions.h"

namespace users {
class UserService {
   public:
    bool init() {
        if (!accounts_.load()) { os::Sys().log("[USERS] cannot load account database\n"); return false; }
        if (!directories_.init()) { os::Sys().log("[USERS] cannot init system directories\n"); return false; }
        for (uint32_t i = 0; i < accounts_.count(); ++i) {
            if (!directories_.prepare(accounts_.at(i).profile)) {
                os::Sys().log("[USERS] cannot prepare home: ");
                os::Sys().log(accounts_.at(i).profile.home);
                os::Sys().log("\n");
                return false;
            }
        }
        if (!sessions_.init()) { os::Sys().log("[USERS] cannot recover authenticated sessions\n"); return false; }
        endpoint_ = os::detail::EndpointControl().create();
        return endpoint_ && os::detail::RegistryClient(os::detail::RegistryClient().endpoint()).registerName("users", endpoint_);
    }
    void run() {
        os::Sys().log("[USERS] account and authentication service started\n");
        while (true) {
            sessions_.sweep();
            os::IpcMessage request{}, reply{};
            if (os::receiveMessage(endpoint_, request, os::Sys().readMonotonicTime() + 100000000ULL)) continue;
            reply.data[0] = 1; reply.length = 1;
            if (request.length >= (sizeof(os::wire::UserCredentials) + 7) / 8) handle(request, reply);
            os::replyMessage(endpoint_, request.replyToken, reply);
            for(uint32_t i=0;i<request.handleCount;++i)os::closeHandle(request.handles[i]);
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
        os::wire::UserCredentials input{};
        struct Erase { os::wire::UserCredentials& value; ~Erase() { Password::erase(&value, sizeof(value)); } } erase{input};
        std::memcpy(&input, request.data, sizeof(input));
        if (!std::memchr(input.name, 0, sizeof(input.name)) || !std::memchr(input.password, 0, sizeof(input.password)) ||
            !std::memchr(input.replacement, 0, sizeof(input.replacement))) return;
        os::wire::TaskInfo caller{};
        if (os::detail::TaskQuery().query(request.senderPid, caller)) return;
        const auto operation = static_cast<os::wire::UserOperation>(request.type);
        Account* account = find(input.name, input.id);
        if (operation == os::wire::UserOperation::List) {
            Account* listed=nullptr;uint32_t available=0;
            for(uint32_t i=0;i<accounts_.count();++i){
                auto& entry=accounts_.at(i);
                if(entry.hash[0]=='!' || entry.hash[0]=='*' || std::strstr(entry.profile.shell,"nologin") || !std::strcmp(entry.profile.shell,"/bin/false"))continue;
                if(available++==input.id){listed=&entry;break;}
            }
            if(!listed)return;
            reply.data[0] = 0;
            std::memcpy(&reply.data[1], &listed->profile, sizeof(os::wire::UserProfile));
            reply.length = 1 + (sizeof(os::wire::UserProfile) + 7) / 8;
            return;
        }
        const bool graphical = operation == os::wire::UserOperation::GraphicalLogin;
        if (graphical) {
            os::wire::TaskInfo parent{};
            if (caller.managedService || caller.effectiveUid || caller.sessionId != caller.id ||
                os::detail::TaskQuery().query(caller.parentId,parent) || !parent.managedService ||
                parent.serviceRole != uint32_t(os::ServiceRole::Init)) return;
        }
        if (operation == os::wire::UserOperation::AutoLogin) {
            account = accounts_.automatic();
            if (!account || caller.effectiveUid || caller.managedService || !caller.terminalKind ||
                caller.sessionId != caller.id || caller.taskGroupId != caller.id) return;
        }
        if (operation == os::wire::UserOperation::Who) {
            os::wire::TaskInfo task{};
            if (os::detail::TaskQuery().query(input.id, task) || task.state == 2) return;
            const char* name = std::strrchr(task.name, '/'); name = name ? name + 1 : task.name;
            if (std::strcmp(name, "sh")) return;
            account = find("", task.uid);
            reply.data[40] = task.id;
        }
        if (!account) return;
        if (operation == os::wire::UserOperation::Login || operation == os::wire::UserOperation::AutoLogin || graphical) {
            if (operation != os::wire::UserOperation::AutoLogin && !Password::verify(input.password, account->hash)) return;
            if (graphical && os::Ipc::Handle(endpoint_, false).requestActive(request.replyToken) <= 0) return;
            // Reserve all fallible session resources before changing credentials.
            if (!sessions_.prepare(request.senderPid, account->profile)) return;
            gid_t groups[os::maximumSupplementaryGroups]; int groupCount = os::maximumSupplementaryGroups;
            if (getgrouplist(account->profile.name, account->profile.gid, groups, &groupCount) < 0 ||
                os::SessionIdentity::establish(request.senderPid, account->profile.uid, account->profile.gid, false, groupCount, groups)) {
                sessions_.cancel(account->profile); return;
            }
            sessions_.commit(request.senderPid, account->profile);
        } else if (operation == os::wire::UserOperation::BeginSession) {
            if (caller.effectiveUid != account->profile.uid ||
                !sessions_.begin(request.senderPid, account->profile)) return;
        } else if (operation == os::wire::UserOperation::Sudo) {
            account = find("", caller.uid);
            if (!account || !allowedSudo(*account) || (caller.effectiveUid != 0 && !Password::verify(input.password, account->hash)) ||
                os::detail::TaskControl().setCredentials( request.senderPid, 0, 0, 1)) return;
        } else if (operation == os::wire::UserOperation::Password) {
            if (caller.effectiveUid && (caller.uid != account->profile.uid || !Password::verify(input.password, account->hash))) return;
            if (strlen(input.replacement) < 1) return;
            uint8_t salt[16];
            const uint64_t values[2] = {os::Sys().readMonotonicTime() ^ ++saltCounter_, os::Sys().readWallClockTime() ^ request.senderPid};
            std::memcpy(salt, values, sizeof(salt));
            char old[128]; std::strcpy(old, account->hash);
            Password::encode(input.replacement, salt, account->hash);
            if (!saveShadow()) { std::strcpy(account->hash, old); Password::erase(old, sizeof(old)); return; }
            Password::erase(old, sizeof(old));
        } else if (operation != os::wire::UserOperation::Lookup && operation != os::wire::UserOperation::Who) return;
        reply.data[0] = 0;
        std::memcpy(&reply.data[1], &account->profile, sizeof(account->profile));
        reply.length = operation == os::wire::UserOperation::Who ? 41 : 1 + (sizeof(account->profile) + 7) / 8;
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
