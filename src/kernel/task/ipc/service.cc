/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/service.cc
 * IPC mechanism requests against the authenticated caller handle table.
 */

#include "kernel/ipc.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace kernel::task {

namespace {
// Encode a negative native error in the unsigned reply value field.
uint64_t error(SyscallError value) {
    return static_cast<uint64_t>(static_cast<int64_t>(value));
}
namespace IpcService {
// Operate only on the authenticated caller's table. Handle operation arguments
// are numeric IDs/rights/flags, not transferred object handles; new duplicates
// are separately advertised in the reply handle vector.
uint64_t handleOperation(Task& caller, uint64_t handle, uint64_t operation,
                         uint64_t rights, uint64_t flags) {
    auto& handles = caller.handles();
    switch (static_cast<os::HandleOperation>(operation)) {
        case os::HandleOperation::Duplicate: {
            if (rights > UINT32_MAX || flags > UINT32_MAX)
                return error(SyscallError::InvalidArgument);
            const uint64_t result = handles.duplicate(handle, rights, flags);
            return result ? result : error(SyscallError::InvalidArgument);
        }
        // Restriction changes one slot and cannot add rights, even if the
        // caller has some other more powerful handle to the same object.
        case os::HandleOperation::RestrictRights:
            return rights <= UINT32_MAX &&
                           handles.restrictRights(handle, rights)
                       ? 0
                       : error(SyscallError::PermissionDenied);
        case os::HandleOperation::SetFlags:
            return flags <= UINT32_MAX && handles.setFlags(handle, flags)
                       ? 0
                       : error(SyscallError::InvalidArgument);
        case os::HandleOperation::GetState: {
            HandleTable::Pin pin(handles, handle, HandleType::Endpoint, 0);
            auto* endpoint = pin.as<Endpoint>();
            return endpoint ? (endpoint->isClosed() ? 1 : 0)
                            : error(SyscallError::InvalidHandle);
        }
        case os::HandleOperation::References:
            return handles.references(handle);
        // Endpoint identity is stable across handle copies. Destruction needs
        // manage rights, request inspection needs receive rights, and neither
        // operation authorizes access by a raw user-supplied object address.
        case os::HandleOperation::ObjectIdentity:
        case os::HandleOperation::DestroyEndpoint:
        case os::HandleOperation::RequestActive: {
            const auto op = static_cast<os::HandleOperation>(operation);
            HandleTable::Pin pin(
                handles, handle, HandleType::Endpoint,
                op == os::HandleOperation::DestroyEndpoint ? handleRightManage
                : op == os::HandleOperation::RequestActive ? handleRightRead
                                                           : 0);
            auto* endpoint = pin.as<Endpoint>();
            if (!endpoint) return error(SyscallError::InvalidHandle);
            if (op == os::HandleOperation::ObjectIdentity)
                return endpoint->getId();
            if (op == os::HandleOperation::RequestActive)
                return endpoint->requestActive(rights) ? 1 : 0;
            endpoint->destroy();
            return 0;
        }
        case os::HandleOperation::Close:
            break;
        default:
            return error(SyscallError::InvalidArgument);
    }
    return caller.handles().close(handle) ? 0
                                          : error(SyscallError::InvalidHandle);
}

// Decode core IPC requests with no incoming handle transfers. Endpoint
// creation and core lookup return handles already installed in caller's table;
// application code receives them through the reply handle vector.
int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
    if (request.length < 5 || request.handleCount) return -1;
    const auto* args = request.data;
    switch (static_cast<os::IpcOperation>(request.type)) {
        case os::IpcOperation::CreateEndpoint: {
            auto* endpoint = Endpoint::create();
            if (!endpoint) return -5;
            const auto handle = caller.handles().add(
                endpoint, HandleType::Endpoint,
                handleRightRead | handleRightWrite | handleRightManage);
            if (!handle) return -5;
            reply.data[0] = handle;
            addCreatedHandle(reply, handle);
            break;
        }
        case os::IpcOperation::GetCoreService: {
            if (args[0] >= static_cast<uint64_t>(os::CoreServiceKind::Count))
                return -1;
            const auto handle = coreServices().getHandle(
                caller, static_cast<os::CoreServiceKind>(args[0]));
            if (!handle) return -4;
            reply.data[0] = handle;
            addCreatedHandle(reply, handle);
            break;
        }
        case os::IpcOperation::Handle:
            reply.data[0] =
                handleOperation(caller, args[0], args[1], args[2], args[3]);
            if (args[1] ==
                    static_cast<uint64_t>(os::HandleOperation::Duplicate) &&
                static_cast<int64_t>(reply.data[0]) > 0)
                addCreatedHandle(reply, reply.data[0]);
            break;
        default:
            return -1;
    }
    reply.length = 1;
    return 0;
}
}  // namespace IpcService
}  // namespace
// Bind the concrete handler without virtual dispatch or a service worker.
EndpointService& ipcKernelService() {
    static EndpointService service(IpcService::call);
    return service;
}

}  // namespace kernel::task
