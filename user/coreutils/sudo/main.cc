#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../user-prompt.h"
extern "C" char** environ;

int main(int argc, char** argv) {
    if (argc < 2) { fputs("usage: sudo COMMAND [ARG ...]\n", stderr); return 2; }
    os::UserProfile profile{};
    os::UserCredentials credentials{}; credentials.id = getuid();
    if (!coreutils::account(getuid(), profile)) return 1;
    if (geteuid() && !coreutils::password("[sudo] password: ", credentials.password, sizeof(credentials.password))) return 1;
    const int status = os::UserClient::request(os::UserOperation::Sudo, credentials, profile);
    coreutils::erase(&credentials, sizeof(credentials));
    if (status) { fputs("sudo: authentication failed or user is not in /etc/sudoers\n", stderr); return 1; }
    os::UserProfile root{};
    if (!coreutils::account(0, root) || !coreutils::SessionEnvironment::apply(root, true, false)) {
        fputs("sudo: cannot initialize root environment\n", stderr); return 1;
    }
    setenv("SUDO_USER", profile.name, 1);
    char id[24]; snprintf(id, sizeof(id), "%u", profile.uid); setenv("SUDO_UID", id, 1);
    if (strchr(argv[1], '/')) { execve(argv[1], argv + 1, environ); perror(argv[1]); return 1; }
    const char* directories[] = {"/usr/sbin", "/usr/bin", "/sbin", "/bin"};
    char path[384]; struct stat file{};
    for (const char* directory : directories) {
        if (snprintf(path, sizeof(path), "%s/%s", directory, argv[1]) >= static_cast<int>(sizeof(path))) return 1;
        if (!stat(path, &file) && S_ISREG(file.st_mode) && (file.st_mode & 0111)) {
            execve(path, argv + 1, environ); perror(path); return 1;
        }
    }
    fputs("sudo: command not found\n", stderr); return 127;
}
