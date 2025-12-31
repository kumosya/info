#include <cstdint>

#include "io.h"
#include "keyboard.h"
#include "mm.h"
#include "task.h"
#include "tty.h"

namespace mm {

int Proc(int argc, char *argv[]) {
    tty::printf(
        "mm task is running, argc: %d, argv[0]: %s, pcb addr: 0x%lx, pid: %d\n",
        argc, argv[0], (std::uint64_t)task::current_proc,
        task::current_proc->pid);

    while (true) {
    }
    return 0;
}

}  // namespace mm
