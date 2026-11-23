/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/sys/service.cc
 * 提供系统时间、内存和 CPU 查询，以及有长度限制的串口日志。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/records.h"
#include <os/device.h>

namespace task {

    namespace system_service {

        /*
         * 分发系统信息和时间等请求，检查消息格式后把查询结果写入回复。
         */
        int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
            switch (static_cast<services::SystemOperation>(request.type)) {
                case services::SystemOperation::MonotonicTime:
                    reply.data[0] = time::getMonotonicTime();
                    break;
                case services::SystemOperation::WallClockTime:
                    reply.data[0] = time::getWallClockTime();
                    break;
                case services::SystemOperation::ClockResolution:
                    reply.data[0] = time::getClockResolution();
                    break;
                case services::SystemOperation::SetWallClockTime: {
                    if (request.length != 1) return -1;
                    services::wire::CredentialInfo identity{};
                    caller.credentials(identity);
                    if (identity.effectiveUid != 0) return -3;
                    if (!time::clock().setWallClockTime(request.data[0]))
                        return -1;
                    break;
                }

                // 系统信息由时间、物理内存和在线 CPU
                // 的当前读数组成，不把它当所有指标同一瞬间的原子快照。
                case services::SystemOperation::Info: {
                    const services::wire::SystemInfo info{
                        time::getMonotonicTime(), mm::getTotalMemoryBytes(),
                        mm::getFreeMemoryBytes(), cpu::getOnlineCpuCount(),
                        static_cast<uint32_t>(os::Sys::Architecture::Amd64)};
                    copyMessageData(&reply.data[1], &info, sizeof(info));
                    break;
                }

                // 日志字符串在消息数据中，不是任意用户指针；同时受消息长度和
                // 512 字节日志上限约束，并要求结尾空字符。
                case services::SystemOperation::Log: {
                    const char* text;

                    if (!readMessageName(request, 0, text, 512)) {
                        return -1;
                    }

                    device::serialWrite(text);
                    break;
                }

                default:
                    return -1;
            }

            // 公共出口填写回复长度，分支只负责自己的字段，错误分支则不走成功回复。
            reply.length = os::ipcDataWordCount;

            return 0;
        }

    }  // namespace system_service

    /*
     * 返回系统内核服务入口，供核心服务表创建端点。
     */
    EndpointService& systemKernelService() {
        static EndpointService service(system_service::call);

        return service;
    }

}  // namespace task
