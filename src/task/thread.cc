#include "task.h"
#include "tty.h"
#include <cstdint>
using namespace std;
namespace task::thread {

void init() {
    tty::printf("Thread init.");
}

} // namespace task