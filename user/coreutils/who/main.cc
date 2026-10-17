#include <stdio.h>
#include <string.h>
#include "../user-prompt.h"
#include "os/task.h"
int main(int, char**) {
    puts("USER       PID     HOME");
    for (uint32_t index = 0;; ++index) {
        os::TaskInfo task{};
        if (os::invokeSyscall(os::SyscallNumber::EnumerateTask, index, reinterpret_cast<uint64_t>(&task))) break;
        const char* name = strrchr(task.name, '/'); name = name ? name + 1 : task.name;
        if (task.state == 2 || strcmp(name, "sh")) continue;
        os::UserProfile profile{};
        if (coreutils::account(task.uid, profile)) printf("%-10s %-7llu %s\n", profile.name,
            static_cast<unsigned long long>(task.id), profile.home);
    }
    return 0;
}
