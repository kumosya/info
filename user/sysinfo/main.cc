#include <stdio.h>
#include <stdlib.h>

namespace {
void field(const char* label, const char* path) {
    FILE* input = fopen(path, "r");
    char text[256]{};
    if (input) { fgets(text, sizeof(text), input); fclose(input); }
    printf("\033[01;36m%-12s\033[0m %s", label, text[0] ? text : "unavailable\n");
}
}

int main(int, char**) {
    const char* user = getenv("USER");
    const char* host = getenv("HOSTNAME");
    printf("\n\033[01;32m"
        "                     _          __        \n"
        "   ___   ___        (_) _ __   / _|  ___  \n"
        "  / _ \\ / __| _____ | || '_ \\ | |_  / _ \\ \n"
        " | (_) |\\__ \\|_____|| || | | ||  _|| (_) |\n"
        "  \\___/ |___/       |_||_| |_||_|   \\___/ \n"
        "\033[0m\n\n%s@%s\n",
           user ? user : "root", host ? host : "localhost");
    field("OS", "/sys/kernel/ostype");
    field("Kernel", "/sys/kernel/osrelease");
    field("Uptime (s)", "/sys/uptime");
    field("CPU Cores", "/sys/cpu/online");
    field("Memory bytes", "/sys/memory/total");
    field("Free bytes", "/sys/memory/free");
    return 0;
}
