#include <cstdlib>
#include <cstring>
#include "os/abi.h"
namespace { int* value; }
__attribute__((constructor)) static void initialize() {
    value = static_cast<int*>(malloc(sizeof(int)));
    if (value) *value = getenv("HOME") && !setenv("OSINFO_LINKER_CTOR", "ready", 1) ? 37 : -100;
}
__attribute__((destructor)) static void finalize() {
    free(value);
    os::System::log("[LD TEST] shared-library destructor passed\n");
}
extern "C" int osinfoProbeBase() {
    const char* environment = getenv("OSINFO_LINKER_CTOR");
    return value && environment && !strcmp(environment, "ready") ? *value : -100;
}
