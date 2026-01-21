#include <cstdint>
#include <cstring>

#include "kernel/io.h"
#include "kernel/mm.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

int Service(int argc, char *argv[]) {
    while (true) {
        task::ipc::Message msg;
        
        if (ipc::Receive(&msg)) {
            switch (msg.type) {
            case 0:
                thread::Kill(msg.sender, msg.num[0]);
                break;
            
            default:
                tty::printk("Unknown message type: %d\n", msg.type);
                break;
            }
        }
    }
    return 0;
}

}  // namespace task