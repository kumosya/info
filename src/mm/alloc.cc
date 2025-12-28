#include "keyboard.h"

#include <cstdint>

#include "mm.h"
#include "io.h"
#include "tty.h"
#include "task.h"

namespace mm {

int Proc(int argc, char *argv[]) {
    tty::printf("mm task is running, argc: %d, argv[0]: %s, pcb addr: 0x%lx\n", argc, argv[0], (std::uint64_t)task::current_proc);
    
    while (true) {
    }
    return 0;
}

}
