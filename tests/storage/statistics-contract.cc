#include <cstdio>
#include <cstddef>
#include <mutex>
#include <thread>
#include <type_traits>

#include "service/device/storage/statistics.h"

namespace {
class Lock : public std::mutex {
   public:
    using Guard = std::lock_guard<Lock>;
};
}  // namespace

using Statistics = os::Storage::Statistics;
static_assert(std::is_standard_layout_v<Statistics>);
static_assert(std::is_aggregate_v<Statistics>);
static_assert(sizeof(Statistics) == 6 * sizeof(uint64_t));
static_assert(offsetof(Statistics, completedRequests) == 0);
static_assert(offsetof(Statistics, readBytes) == sizeof(uint64_t));
static_assert(offsetof(Statistics, writtenBytes) == 2 * sizeof(uint64_t));
static_assert(offsetof(Statistics, errors) == 3 * sizeof(uint64_t));
static_assert(offsetof(Statistics, timeouts) == 4 * sizeof(uint64_t));
static_assert(offsetof(Statistics, inFlight) == 5 * sizeof(uint64_t));

int main() {
    using Direction = storage::IoDirection;
    using Error = os::Storage::Error;
    storage::RequestCounters<Lock> counters;
    counters.begin();
    counters.begin();
    const auto active = counters.snapshot();
    if (active.inFlight != 2 || active.completedRequests != 0) return 1;
    counters.finish(Direction::Read, Error::Io, 512);
    counters.finish(Direction::Write, Error::Timeout, 0);
    counters.begin();
    counters.finish(Direction::Write, Error::Success, 1024);
    counters.begin();
    counters.finish(Direction::Flush, Error::NotSupported, 0);
    const auto done = counters.snapshot();
    if (done.inFlight || done.completedRequests != 4 || done.readBytes != 512 ||
        done.writtenBytes != 1024 || done.errors != 3 || done.timeouts != 1)
        return 1;

    storage::RequestCounters<Lock> concurrent;
    auto load = [&] {
        for (unsigned i = 0; i < 10000; ++i) {
            concurrent.begin();
            concurrent.finish(Direction::Read, Error::Success, 512);
        }
    };
    std::thread a(load), b(load);
    bool coherent = true;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto s = concurrent.snapshot();
        coherent &= s.inFlight <= 2 && s.readBytes == s.completedRequests * 512;
    }
    a.join();
    b.join();
    const auto s = concurrent.snapshot();
    if (!coherent || s.inFlight || s.completedRequests != 20000) return 1;
    counters.begin();
    counters.finish(Direction::Read, Error::Success, UINT64_MAX);
    if (counters.snapshot().readBytes != UINT64_MAX) return 1;
    std::puts("3/3 passed: accounting, concurrent snapshots, saturation");
}
