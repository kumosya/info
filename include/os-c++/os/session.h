#pragma once
#include "os/file.h"
#include "os/configuration.h"
#include "os/task.h"
namespace os {
class SessionCleanup {
 public:
    static void terminate(uint64_t session) {
        TaskInfo self{};
        if (!session || Task::query(0, self) || self.sessionId == session) return;
        File fs; DirectoryEntry entries[8]{}; uint64_t cursor = 0;
        for (unsigned batch = 0; batch < 128; ++batch) {
            const auto count = fs.readDirectoryBatch("/proc", cursor, entries, 8);
            if (count <= 0) break;
            for (ssize_t i = 0; i < count; ++i) {
                cursor = entries[i].nextOffset;
                uint64_t pid = 0;
                for (const char* p = entries[i].name; *p >= '0' && *p <= '9'; ++p) pid = pid * 10 + *p - '0';
                TaskInfo info{};
                if (pid && !Task::query(pid, info) && info.sessionId == session && !info.managedService)
                    Task::sendSignal(pid, 9);
            }
        }
    }
};
class SessionConfiguration {
 public:
    char terminal[128]{}, terminalName[24]{}, login[128]{}, loginName[24]{};
    bool load() {
        File input; if (input.open("/etc/sessions", O_RDONLY)) return false;
        char bytes[1024]{}; size_t used = 0;
        while (used + 1 < sizeof(bytes)) { const auto count = input.read(bytes + used, sizeof(bytes) - used - 1); if (count < 0) return false; if (!count) break; used += count; }
        char extra; if (input.read(&extra, 1) != 0) return false;
        if (!ConfigurationText::valid(bytes, used)) return false;
        unsigned seen = 0;
        for (char* line = bytes; *line;) {
            char* end = line; while (*end && *end != '\n') ++end;
            const bool last = !*end; *end = 0; if (end > line && end[-1] == '\r') end[-1] = 0;
            if (*line && *line != '#') {
                char* field[3] = {line}; unsigned count = 1;
                for (char* p = line; *p; ++p) if (*p == '|') { if (count == 3) return false; *p = 0; field[count++] = p + 1; }
                if (count != 3 || !ConfigurationText::absolutePath(field[1]) || !*field[2] || Runtime::stringLength(field[1]) >= 128 || Runtime::stringLength(field[2]) >= 24) return false;
                unsigned bit = Runtime::stringsEqual(field[0], "terminal") ? 1 : Runtime::stringsEqual(field[0], "login") ? 2 :
                    Runtime::stringsEqual(field[0], "shell") ? 4 : Runtime::stringsEqual(field[0], "recovery") ? 8 : 0;
                if (!bit || (seen & bit)) return false;
                seen |= bit;
                // Legacy shell/recovery files use recovery for every session.
                if (bit != 4) {
                    char* path = bit == 1 ? terminal : login;
                    char* name = bit == 1 ? terminalName : loginName;
                    Runtime::copyString(path, field[1], 128); Runtime::copyString(name, field[2], 24);
                }
            }
            if (last) break;
            line = end + 1;
        }
        return seen == 3 || seen == 13;
    }
};
}
