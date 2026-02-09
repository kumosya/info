#include <cstdlib>
#include <cstdio>

int main(int argc, char *argv[]);

extern "C" void _start(int argc, char *argv[]) {
    std::exit(main(argc, argv));
}