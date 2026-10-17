#include <stdio.h>
#include "os/task.h"

int main(int, char**) {
    puts("PID STATE NAME");
    for (uint32_t index = 0;; ++index) {
        os::TaskListEntry entry{};
        if (!os::listTask(index, entry)) break;
        const char* state = entry.state == 2 ? "Exited" : "Running";
        printf("%lu %s %s\n", static_cast<unsigned long>(entry.id), state,
               entry.name);
    }
    return 0;
}
