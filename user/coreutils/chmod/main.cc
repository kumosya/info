#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace {
bool mode(const char* text, mode_t previous, mode_t& result) {
    result = previous & 07777;
    if (*text >= '0' && *text <= '7') {
        char* end;
        const unsigned long value = strtoul(text, &end, 8);
        if (*end || value > 07777) return false;
        result = value; return true;
    }
    while (*text) {
        unsigned who = 0;
        for (; *text == 'u' || *text == 'g' || *text == 'o' || *text == 'a'; ++text)
            who |= *text == 'u' ? 04700 : *text == 'g' ? 02070 : *text == 'o' ? 01007 : 07777;
        if (!who) who = 07777;
        const char operation = *text++;
        if (operation != '+' && operation != '-' && operation != '=') return false;
        unsigned bits = 0;
        while (*text && *text != ',') {
            const char c = *text++;
            if (c == 'r') bits |= 0444;
            else if (c == 'w') bits |= 0222;
            else if (c == 'x') bits |= 0111;
            else if (c == 'X') { if (S_ISDIR(previous) || (previous & 0111)) bits |= 0111; }
            else if (c == 's') bits |= 06000;
            else if (c == 't') bits |= 01000;
            else return false;
        }
        bits &= who;
        result = operation == '+' ? result | bits : operation == '-' ? result & ~bits : (result & ~who) | bits;
        if (*text == ',') { ++text; if (!*text) return false; }
    }
    return true;
}
}
int main(int argc, char** argv) {
    if (argc < 3) { fputs("usage: chmod MODE FILE ...\n", stderr); return 2; }
    int result = 0;
    for (int i = 2; i < argc; ++i) {
        struct stat status{}; mode_t updated;
        if (stat(argv[i], &status) || !mode(argv[1], status.st_mode, updated) || chmod(argv[i], updated)) {
            perror(argv[i]); result = 1;
        }
    }
    return result;
}
