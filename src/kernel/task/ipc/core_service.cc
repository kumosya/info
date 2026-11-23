/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/core_service.cc
 * 建立核心服务端点，并向任务分配访问句柄。
 */

#include "kernel/ipc.h"
#include "kernel/page.h"
#include "kernel/task.h"

namespace task {

    static CoreServices registry;

    /*
     * 返回核心服务注册表，使任务通过约定的服务类别取得内核入口。
     */
    CoreServices& coreServices() { return registry; }

    /*
     * 创建并登记各核心服务端点。
     * 任一必要端点初始化失败时返回失败，启动方据此停止继续建立用户服务。
     */
    bool CoreServices::init() {
        if (!mm::maps().init()) {
            return false;
        }

        // 数组索引与协议里的核心服务类别对应，空项明确保留，不把不同服务挤到错误编号上。
        EndpointService* services[] = {nullptr,
                                       nullptr,
                                       &memoryKernelService(),
                                       &deviceKernelService(),
                                       &systemKernelService(),
                                       &taskKernelService(),
                                       &pciKernelService(),
                                       &ipcKernelService()};

        for (uint32_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i) {
            if (!services[i]) {
                continue;
            }

            // 每个实际内核入口包装为端点，之后普通任务通过受限句柄访问，而不是直接取得内核函数地址。
            coreEndpoints[i] = Endpoint::create(services[i]);

            if (!coreEndpoints[i]) {
                return false;
            }
        }

        return true;
    }

    /*
     * 按服务类别为调用任务安装句柄，并给予对应服务所需的访问权限。
     */
    uint64_t CoreServices::getHandle(Task& caller, os::CoreServiceKind kind) {
        const uint64_t index = static_cast<uint64_t>(kind);

        return index < static_cast<uint64_t>(os::CoreServiceKind::Count) &&
                       coreEndpoints[index]
                   ? caller.handles().add(coreEndpoints[index],
                                          HandleType::Endpoint,
                                          handleRightWrite)
                   : 0;
    }

    /*
     * 返回调用任务的引导服务句柄，供新进程连接启动时提供的服务。
     */
    uint64_t CoreServices::bootstrap(Task& caller) {
        const uint64_t index = static_cast<uint64_t>(os::CoreServiceKind::Ipc);
        HandleTable::Pin pin =
            HandleTable::Pin(caller.handles(), caller.kernelHandle(),
                             HandleType::Endpoint, handleRightWrite);

        // 已保存句柄若仍指向正确 IPC 核心端点就复用，并清除可能妨碍继承的标志。
        if (pin.as<Endpoint>() == coreEndpoints[index]) {
            caller.handles().setFlags(caller.kernelHandle(), 0);

            return caller.kernelHandle();
        }

        // 原缓存句柄无效或指错对象时重新安装，再记录到任务，不能仅凭旧编号继续调用。
        const uint64_t handle = getHandle(caller, os::CoreServiceKind::Ipc);
        caller.setKernelHandle(handle);

        return handle;
    }

}  // namespace task
