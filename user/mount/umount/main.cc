#include <stdio.h>

#include "os/file.h"
#include "os/protocol.h"
#include "os/service.h"

int main(int argumentCount, char** arguments) {
    if (argumentCount != 2) {
        fputs("usage: umount TARGET\n", stderr);
        return 1;
    }
    const uint64_t service =
        os::ServiceRegistry::lookupService(os::ServiceRegistry::serviceRegistry(), os::fsServiceName, 1000);
    if (service == 0 || !(os::File(service).umount(arguments[1]) == 0)) {
        fputs("umount: target is busy or not mounted\n", stderr);
        return 1;
    }
    return 0;
}
