#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../user-prompt.h"

int main(int argc, char** argv) {
    if (argc > 2) { fputs("usage: passwd [USER]\n", stderr); return 2; }
    os::UserCredentials credentials{}; credentials.id = getuid();
    if (argc == 2) os::copyString(credentials.name, argv[1], sizeof(credentials.name));
    os::UserProfile profile{};
    if (os::UserClient::request(os::UserOperation::Lookup, credentials, profile)) { fputs("passwd: unknown user\n", stderr); return 1; }
    if (geteuid() && !coreutils::password("Current password: ", credentials.password, sizeof(credentials.password))) return 1;
    char confirmation[128]{};
    bool ok = coreutils::password("New password: ", credentials.replacement, sizeof(credentials.replacement)) &&
              coreutils::password("Retype new password: ", confirmation, sizeof(confirmation)) &&
              !strcmp(credentials.replacement, confirmation);
    coreutils::erase(confirmation, sizeof(confirmation));
    if (ok) ok = os::UserClient::request(os::UserOperation::Password, credentials, profile) == 0;
    coreutils::erase(&credentials, sizeof(credentials));
    puts(ok ? "Password updated." : "passwd: password was not changed");
    return ok ? 0 : 1;
}
