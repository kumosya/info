#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace {
bool identifier(const char* name, bool group, unsigned& id) {
    if (!*name) { id = static_cast<unsigned>(-1); return true; }
    char* end;
    const unsigned long number = strtoul(name, &end, 10);
    if (!*end && number < 65535) { id = number; return true; }
    FILE* file = fopen(group ? "/etc/group" : "/etc/passwd", "r");
    if (!file) return false;
    char line[256]; bool found = false;
    while (fgets(line, sizeof(line), file)) {
        char* first = strchr(line, ':');
        if (!first) continue;
        *first++ = 0;
        char* second = strchr(first, ':');
        if (strcmp(line, name) || !second) continue;
        id = strtoul(second + 1, &end, 10);
        found = *end == ':' && id < 65535;
        break;
    }
    fclose(file); return found;
}
}
int main(int argc, char** argv) {
    if (argc < 3) { fputs("usage: chown OWNER[:GROUP] FILE ...\n", stderr); return 2; }
    char owner[64];
    if (strlen(argv[1]) >= sizeof(owner)) return 2;
    strcpy(owner, argv[1]);
    char* group = strchr(owner, ':');
    if (group) *group++ = 0;
    unsigned uid, gid = static_cast<unsigned>(-1);
    if (!identifier(owner, false, uid) || (group && !identifier(group, true, gid))) {
        fputs("chown: unknown user or group\n", stderr); return 1;
    }
    int result = 0;
    for (int i = 2; i < argc; ++i) if (chown(argv[i], uid, gid)) { perror(argv[i]); result = 1; }
    return result;
}
