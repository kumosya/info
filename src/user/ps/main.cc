#include <stdio.h>
#include "os/process.h"

int main(int, char**) {
    puts("PID STATE NAME");
    for (uint32_t index = 0;; ++index) {
        os::ProcessListEntry entry{};
        if (!os::listProcess(index, entry)) break;
        const char* state = entry.state == 2 ? "Exited" : "Running";
        printf("%lu %s %s\n", static_cast<unsigned long>(entry.id), state,
               entry.name);
    }
    return 0;
}
