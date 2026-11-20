#include <cstdio>

#include "service/common/requests.h"
#include "service/device/storage/request_lifecycle.h"

int main() {
    using Slots = storage::VirtioRequestSlots<3>;
    Slots slots;
    unsigned tests = 0, failed = 0;
    auto check = [&](bool ok, const char* label) {
        ++tests;
        if (!ok) ++failed;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    };

    check(!storage::expired(10, 14, 5) && storage::expired(10, 15, 5),
          "submission budget expires at deadline");
    check(storage::expired(UINT64_MAX - 1, 3, 5) &&
              os::StorageProtocol::ipcDeadlineAfter(UINT64_MAX - 2, 4) ==
                  UINT64_MAX - 1,
          "clock wrap and finite IPC deadline");
    check(slots.findFree() == 0 && !slots.recordUsed(0, 513),
          "completion for a free slot rejected");
    slots.submit(0);
    slots.submit(1);
    slots.submit(2);
    check(slots.findFree() == 3, "full queue has no available slot");
    check(!slots.recordUsed(1, 513) && !slots.recordUsed(9, 513),
          "misaligned or out-of-range descriptor rejected");
    check(slots.recordUsed(3, 513) && slots.completed(1) &&
              slots.usedLength(1) == 513 && !slots.completed(0),
          "out-of-order completion only changes its own slot");
    check(!slots.recordUsed(3, 1) && slots.usedLength(1) == 513,
          "duplicate does not overwrite the first result");
    slots.consume(1);
    check(!slots.recordUsed(3, 513) && slots.findFree() == 1,
          "consumed result cannot be delivered again");
    slots.submit(1);
    slots.freeze();
    check(!slots.recordUsed(0, 513) && slots.findFree() == 3 &&
              !slots.completed(0) && !slots.completed(1),
          "timeout freezes submitted slots and rejects late completion");
    check(Slots::validUsedAdvance(65534, 1) && !Slots::validUsedAdvance(2, 6),
          "used index wrap accepted, excessive advance rejected");
    std::printf("%u/%u passed\n", tests - failed, tests);
    return failed ? 1 : 0;
}
