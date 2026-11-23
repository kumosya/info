/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/service.cc
 * 提供端点和句柄管理的内核服务入口。
 */

#include "kernel/ipc.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace task {

    /*
     * 把句柄操作错误转换成统一的系统调用返回值。
     */
    static uint64_t error(SyscallError value) {
        return static_cast<uint64_t>(static_cast<int64_t>(value));
    }

    namespace ipc_service {

        /*
         * 分发关闭、复制、权限收紧和标志操作；具体句柄校验与引用处理交给句柄表完成。
         */
        uint64_t handleOperation(Task& caller, uint64_t handle,
                                 uint64_t operation, uint64_t rights,
                                 uint64_t flags) {
            HandleTable& handles = caller.handles();

            switch (static_cast<services::HandleOperation>(operation)) {
                // 复制请求保留对象身份，但新权限只能取原权限的子集；新句柄编号由目标空槽决定。
                case services::HandleOperation::Duplicate: {
                    if (rights > UINT32_MAX || flags > UINT32_MAX) {
                        return error(SyscallError::InvalidArgument);
                    }

                    const uint64_t result =
                        handles.duplicate(handle, rights, flags);

                    return result ? result
                                  : error(SyscallError::InvalidArgument);
                }

                // 权限收紧只修改一个槽位；即使调用者另有更强句柄，也不能用它为当前槽位增加权限。
                case services::HandleOperation::RestrictRights:
                    return rights <= UINT32_MAX &&
                                   handles.restrictRights(handle, rights)
                               ? 0
                               : error(SyscallError::PermissionDenied);
                case services::HandleOperation::SetFlags:
                    return flags <= UINT32_MAX &&
                                   handles.setFlags(handle, flags)
                               ? 0
                               : error(SyscallError::InvalidArgument);

                // 查询有效性不取得对象长期所有权，只返回当前句柄是否还能通过表内检查。
                case services::HandleOperation::GetState: {
                    HandleTable::Pin pin(handles, handle, HandleType::Endpoint,
                                         0);
                    task::Endpoint* endpoint = pin.as<Endpoint>();

                    return endpoint ? (endpoint->isClosed() ? 1 : 0)
                                    : error(SyscallError::InvalidHandle);
                }

                case services::HandleOperation::References:
                    return handles.references(handle);
                // 端点身份不随句柄复制改变；销毁需要管理权限，请求查询需要接收权限，不能直接拿用户提供的对象地址当授权。
                case services::HandleOperation::ObjectIdentity:
                case services::HandleOperation::DestroyEndpoint:
                case services::HandleOperation::RequestActive: {
                    const services::HandleOperation op =
                        static_cast<services::HandleOperation>(operation);
                    HandleTable::Pin pin(
                        handles, handle, HandleType::Endpoint,
                        op == services::HandleOperation::DestroyEndpoint
                            ? handleRightManage
                        : op == services::HandleOperation::RequestActive
                            ? handleRightRead
                            : 0);
                    task::Endpoint* endpoint = pin.as<Endpoint>();

                    if (!endpoint) {
                        return error(SyscallError::InvalidHandle);
                    }

                    if (op == services::HandleOperation::ObjectIdentity) {
                        return endpoint->getId();
                    }

                    if (op == services::HandleOperation::RequestActive) {
                        return endpoint->requestActive(rights) ? 1 : 0;
                    }

                    endpoint->destroy();

                    return 0;
                }

                case services::HandleOperation::Close:
                    break;
                default:
                    return error(SyscallError::InvalidArgument);
            }

            return caller.handles().close(handle)
                       ? 0
                       : error(SyscallError::InvalidHandle);
        }

        /*
         * 处理 IPC
         * 内核服务请求，校验请求长度和操作类别后调用端点及句柄管理接口。
         */
        int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
            if (request.length < 5 || request.handleCount) {
                return -1;
            }

            const uint64_t* args = request.data;

            switch (static_cast<services::IpcOperation>(request.type)) {
                // 先创建端点对象再安装调用者句柄，安装失败需要归还本次新对象，不能留下不可访问端点。
                case services::IpcOperation::CreateEndpoint: {
                    Endpoint* endpoint = Endpoint::create();

                    if (!endpoint) {
                        return -5;
                    }

                    const uint64_t handle = caller.handles().add(
                        endpoint, HandleType::Endpoint,
                        handleRightRead | handleRightWrite | handleRightManage);

                    if (!handle) {
                        return -5;
                    }

                    reply.data[0] = handle;
                    addCreatedHandle(reply, handle);
                    break;
                }

                // 只按协议服务类别取得已登记核心入口，不接受用户任意给出的内核地址。
                case services::IpcOperation::GetCoreService: {
                    if (args[0] >=
                        static_cast<uint64_t>(services::CoreServiceKind::Count)) {
                        return -1;
                    }

                    const uint64_t handle = coreServices().getHandle(
                        caller, static_cast<services::CoreServiceKind>(args[0]));

                    if (!handle) {
                        return -4;
                    }

                    reply.data[0] = handle;
                    addCreatedHandle(reply, handle);
                    break;
                }

                case services::IpcOperation::Handle:
                    reply.data[0] = handleOperation(caller, args[0], args[1],
                                                    args[2], args[3]);

                    if (args[1] == static_cast<uint64_t>(
                                       services::HandleOperation::Duplicate) &&
                        static_cast<int64_t>(reply.data[0]) > 0) {
                        addCreatedHandle(reply, reply.data[0]);
                    }

                    break;
                default:
                    return -1;
            }

            reply.length = 1;

            return 0;
        }

    }  // namespace ipc_service

    /*
     * 返回 IPC 内核服务入口描述，供核心服务表创建对应端点。
     */
    EndpointService& ipcKernelService() {
        static EndpointService service(ipc_service::call);

        return service;
    }

}  // namespace task
