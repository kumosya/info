#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argumentCount, char** arguments) {
    const char* value = argumentCount > 1 ? arguments[1] : "1";
    char* end = nullptr;
    const unsigned long seconds = strtoul(value, &end, 10);
    if (value[0] == '\0' || end == nullptr || *end != '\0' || seconds == 0 ||
        seconds > 3600) {
        fputs("sleep: invalid duration\n", stderr);
        return 1;
    }
    sleep(static_cast<unsigned int>(seconds));
    return 0;
}
