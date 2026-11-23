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

    namespace Sys {

        /*
         * 分发系统信息和时间等请求，检查消息格式后把查询结果写入回复。
         */
        int call(Task& caller, const IpcMessage& request, IpcMessage& reply) {
            switch (static_cast<os::SystemOperation>(request.type)) {
                case os::SystemOperation::MonotonicTime:
                    reply.data[0] = time::getMonotonicTime();
                    break;
                case os::SystemOperation::WallClockTime:
                    reply.data[0] = time::getWallClockTime();
                    break;
                case os::SystemOperation::ClockResolution:
                    reply.data[0] = time::getClockResolution();
                    break;
                case os::SystemOperation::SetWallClockTime: {
                    if (request.length != 1) return -1;
                    os::wire::CredentialInfo identity{};
                    caller.credentials(identity);
                    if (identity.effectiveUid != 0) return -3;
                    if (!time::clock().setWallClockTime(request.data[0]))
                        return -1;
                    break;
                }

                // 系统信息由时间、物理内存和在线 CPU
                // 的当前读数组成，不把它当所有指标同一瞬间的原子快照。
                case os::SystemOperation::Info: {
                    const os::wire::SystemInfo info{
                        time::getMonotonicTime(), mm::getTotalMemoryBytes(),
                        mm::getFreeMemoryBytes(), cpu::getOnlineCpuCount(), 0};
                    copyMessageData(&reply.data[1], &info, sizeof(info));
                    break;
                }

                // 日志字符串在消息数据中，不是任意用户指针；同时受消息长度和
                // 512 字节日志上限约束，并要求结尾空字符。
                case os::SystemOperation::Log: {
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

    }  // namespace Sys

    /*
     * 返回系统内核服务入口，供核心服务表创建端点。
     */
    EndpointService& systemKernelService() {
        static EndpointService service(Sys::call);

        return service;
    }

}  // namespace task
