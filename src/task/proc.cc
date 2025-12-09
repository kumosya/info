#include "task.h"
#include "tty.h"
#include <cstdint>
using namespace std;
namespace task::proc {
void init() {
    
    tty::printf("Multitask system initialized.\n");
}
} // namespace task