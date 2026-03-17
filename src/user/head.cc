#include <cstdlib>
#include <cstdio>

int main(int argc, char *argv[]);
extern "C" void malloc_init();

extern "C" void _start(int argc, char *argv[]) {
    malloc_init();
    std::exit(main(argc, argv));
}