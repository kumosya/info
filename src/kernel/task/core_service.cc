#include "kernel/core_service.h"
#include "kernel/page.h"
#include "kernel/task.h"
namespace kernel {
namespace {
Endpoint* coreEndpoints[static_cast<uint64_t>(os::CoreServiceKind::Count)];
}  // namespace

bool CoreServices::initialize() {
    if (!memory::PageMappings::initialize()) return false;
    EndpointService* services[] = {nullptr, nullptr, &memoryKernelService(), &deviceKernelService(), &systemKernelService(), &taskKernelService(), &pciKernelService()};
    for (uint32_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i) {
        if (!services[i]) continue;
        coreEndpoints[i] = Endpoint::create(services[i]);
        if (!coreEndpoints[i]) return false;
    }
    return true;
}
uint64_t CoreServices::getHandle(Task& caller, os::CoreServiceKind kind) {
    const auto index = static_cast<uint64_t>(kind);
    return index < static_cast<uint64_t>(os::CoreServiceKind::Count) && coreEndpoints[index]
        ? caller.handles().add(coreEndpoints[index], HandleType::Endpoint, handleRightWrite) : 0;
}
}  // namespace kernel
