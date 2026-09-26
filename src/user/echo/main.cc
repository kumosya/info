#include <stdio.h>

int main(int argumentCount, char** arguments) {
    for (int index = 1; index < argumentCount; ++index) {
        if (index != 1) {
            putchar(' ');
        }
        fputs(arguments[index], stdout);
    }
    return putchar('\n') == EOF ? 1 : 0;
}
