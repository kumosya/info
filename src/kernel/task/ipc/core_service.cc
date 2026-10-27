/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/core_service.cc
 * Core endpoint initialization and per-process bootstrap handles.
 */

#include "kernel/ipc.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace kernel::task {

namespace {
CoreServices registry;
}  // namespace

// Shared registry of resident kernel endpoint objects, not the user-space name
// registry used to discover filesystem or storage services.
CoreServices& coreServices() { return registry; }
// Initialize mapping bookkeeping, then bind each supported core kind to a
// concrete EndpointService. These endpoints execute in the caller's kernel
// path; no separate server thread is created for this dispatch.
bool CoreServices::init() {
    if (!mm::maps().init()) return false;
    EndpointService* services[] = {nullptr,
                                   nullptr,
                                   &memoryKernelService(),
                                   &deviceKernelService(),
                                   &systemKernelService(),
                                   &taskKernelService(),
                                   &pciKernelService(),
                                   &ipcKernelService()};
    for (uint32_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i) {
        if (!services[i]) continue;
        coreEndpoints[i] = Endpoint::create(services[i]);
        if (!coreEndpoints[i]) return false;
    }
    return true;
}
// Install a call-capable endpoint reference in caller's own handle table.
// Possession permits entry, while the selected service still checks roles,
// capabilities and target-resource permissions for each operation.
uint64_t CoreServices::getHandle(Task& caller, os::CoreServiceKind kind) {
    const auto index = static_cast<uint64_t>(kind);
    return index < static_cast<uint64_t>(os::CoreServiceKind::Count) &&
                   coreEndpoints[index]
               ? caller.handles().add(coreEndpoints[index],
                                      HandleType::Endpoint, handleRightWrite)
               : 0;
}

// Reuse kernelHandle only after pinning and comparing its actual object. A
// user-controlled numeric value cannot substitute another endpoint. Clear
// per-slot flags on reuse so the bootstrap survives fork/exec rules.
uint64_t CoreServices::bootstrap(Task& caller) {
    const auto index = static_cast<uint64_t>(os::CoreServiceKind::Ipc);
    auto pin         = HandleTable::Pin(caller.handles(), caller.kernelHandle(),
                                        HandleType::Endpoint, handleRightWrite);
    if (pin.as<Endpoint>() == coreEndpoints[index]) {
        caller.handles().setFlags(caller.kernelHandle(), 0);
        return caller.kernelHandle();
    }
    const uint64_t handle = getHandle(caller, os::CoreServiceKind::Ipc);
    caller.setKernelHandle(handle);
    return handle;
}

}  // namespace kernel::task
