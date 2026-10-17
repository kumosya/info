#include <stdio.h>

#include "os/file.h"
#include "os/protocol.h"
#include "os/service.h"

namespace {

uint64_t fsService() {
    return os::lookupService(os::serviceRegistry(), os::fsServiceName, 1000);
}

}  // namespace

int main(int argumentCount, char** arguments) {
    const uint64_t service = fsService();
    if (service == 0) return 1;
    if (argumentCount == 1) {
        for (uint32_t index = 0;; ++index) {
            os::MountInfo info{};
            if (!os::File(service).listMount(index, info)) break;
            printf("%s on %s type %s (%s)\n", info.device, info.target,
                   info.type, info.readOnly ? "ro" : "rw");
        }
        return 0;
    }
    if (argumentCount != 4) {
        fputs("usage: mount [DEVICE TARGET TYPE]\n", stderr);
        return 1;
    }
    if (!(os::File(service).mount(arguments[1], arguments[2], arguments[3]) == 0)) {
        fputs("mount: unsupported device, target, or filesystem\n", stderr);
        return 1;
    }
    return 0;
}
