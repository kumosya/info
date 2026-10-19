#include "os/abi.h"
extern "C" int osinfoProbeValue();
int main() {
    if (osinfoProbeValue() != 43) return 91;
    os::System::log("[LD TEST] transitive dependency and constructor passed\n");
    return 0;
}
