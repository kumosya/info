#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../user-prompt.h"
extern "C" char** environ;

int main(int argc, char** argv) {
    if (argc > 2) { fputs("usage: login [USER]\n", stderr); return 2; }
    os::UserCredentials credentials{};
    if (argc == 2) os::Runtime::copyString(credentials.name, argv[1], sizeof(credentials.name));
    else {
        fputs("login: ", stdout);
        if (!fgets(credentials.name, sizeof(credentials.name), stdin)) return 1;
        char* newline = strchr(credentials.name, '\n'); if (newline) *newline = 0;
    }
    if (!coreutils::password("Password: ", credentials.password, sizeof(credentials.password))) return 1;
    os::UserProfile profile{};
    const int result = os::UserClient::request(os::UserOperation::Login, credentials, profile);
    coreutils::erase(&credentials, sizeof(credentials));
    if (result) { fputs("login: authentication failed\n", stderr); return 1; }
    setsid(); coreutils::environment(profile);
    char* arguments[] = {profile.shell, nullptr};
    execve(profile.shell, arguments, environ);
    perror(profile.shell); return 1;
}
