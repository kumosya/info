#include <cstdlib>

#include <kernel/syscall.h>

int main(int argc, char *argv[]);

extern "C" void _start(int argc, char *argv[]) {
    std::exit(main(argc, argv));
    while (true);  // we should never reach here
}