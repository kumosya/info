#include <stdio.h>

int main(int, char**) {
    return puts("Hello from osinfo") == EOF ? 1 : 0;
}
