#include <stdio.h>

#include "os/file.h"
#include "os/protocol.h"
#include "os/service.h"

int main(int argumentCount, char** arguments) {
    if (argumentCount != 3) {
        fputs("usage: mkfs TYPE DEVICE\n", stderr);
        return 1;
    }
    const uint64_t service =
        os::lookupService(os::serviceRegistry(), os::fsServiceName, 1000);
    if (service == 0 ||
        !(os::File(service).format(arguments[2], arguments[1]) == 0)) {
        fputs("mkfs: device is busy or format is not writable\n", stderr);
        return 1;
    }
    return 0;
}
