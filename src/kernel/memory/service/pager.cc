/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/service/pager.cc
 * 连接内核与用户态内存服务，处理缺页及服务恢复期间的通信。
 */

#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/protocol.h"

namespace mm {

    static PagerBridge pager;

    /*
     * 返回内核与用户态内存服务之间的通信桥梁。
     */
    PagerBridge& pagerBridge() { return pager; }

    /*
     * 向当前绑定的内存服务发送请求并等待回复。
     * 通信期间保留端点引用；服务切换或请求失败时按接口规则返回错误。
     */
    int PagerBridge::exchange(::task::IpcMessage& request,
                              ::task::IpcMessage& reply, bool optional) {
        const uint64_t flags = cpu::disableInterrupts();
        cpu::enableInterrupts();

        // 整次等待共用一个二十秒截止时间；服务反复断开也不能让一次请求无限期重试。
        const uint64_t deadline = time::getMonotonicTime() + 20000000000ULL;
        bool observed           = false;

        while (true) {
            ::task::Endpoint* endpoint;
            {
                ::task::InterruptSpinLockGuard guard(pagerLock);

                // 在绑定锁内取得端点并加引用，放锁以后即使绑定被替换，这次通信仍持有有效对象。
                endpoint = pagerEndpoint;

                if (endpoint) {
                    endpoint->retain();
                    observed = true;
                }
            }

            // 首次绑定之前没有内存服务可能只是启动尚未完成；曾经绑定后失联则按恢复处理中重试，但不能反复延长原截止时间。
            if (!endpoint && !observed) {
                cpu::restoreInterrupts(flags);

                return optional ? 0 : static_cast<int>(os::ErrorCode::PeerDied);
            }

            const int result =
                endpoint ? endpoint->callFromKernel(&request, &reply, deadline)
                         : static_cast<int>(os::ErrorCode::PeerDied);

            // 一次尝试结束就归还临时端点引用，下次重试重新读取绑定，才能连到新实例。
            if (endpoint) {
                endpoint->release();
            }

            if (result != static_cast<int>(os::ErrorCode::PeerDied) &&
                result != static_cast<int>(os::ErrorCode::EndpointClosed)) {
                cpu::restoreInterrupts(flags);

                return result;
            }

            if (time::getMonotonicTime() >= deadline) {
                cpu::restoreInterrupts(flags);

                return static_cast<int>(os::ErrorCode::Timeout);
            }

            // 服务正在恢复时短暂睡眠，让新实例有机会启动，而不是占着 CPU 空转。
            ::task::sleepCurrentThread(1);
        }
    }

    /*
     * 绑定负责补页的服务端点，并登记所属任务。
     * 旧绑定的引用在替换后释放，避免端点被提前销毁。
     */
    bool PagerBridge::bind(::task::Task& owner, ::task::Endpoint* endpoint) {
        // 同时核对能力、固定服务角色、当前实例身份和端点所有者，普通进程不能冒充补页服务。
        if (!endpoint || !owner.hasCapability(::task::taskCapabilityMemory) ||
            owner.serviceRole() != services::ServiceRole::Memory ||
            !::task::serviceRecovery().current(owner) ||
            endpoint->getOwner() != &owner) {
            return false;
        }

        ::task::InterruptSpinLockGuard guard(pagerLock);

        // 旧绑定仍存活且属于另一任务时拒绝抢占，防止两个活动实例同时处理客户映射。
        if (pagerEndpoint && !pagerEndpoint->isClosed() &&
            pagerEndpoint->getOwner() != &owner) {
            return false;
        }

        // 先取得新绑定引用再归还旧引用；即使两者恰好是同一端点，也不会中途把对象释放。
        endpoint->retain();

        if (pagerEndpoint) {
            pagerEndpoint->release();
        }

        pagerEndpoint = endpoint;

        return true;
    }

    /*
     * 把当前任务的缺页地址和访问方式交给内存服务处理。
     * 回复成功表示可以重新执行触发缺页的指令。
     */
    bool PagerBridge::resolve(uint64_t address, uint64_t flags, int* failure) {
        if (failure) {
            *failure = -4;
        }

        ::task::Task* caller = task::getCurrent();

        // 不把内存服务自己的缺页再交还给它处理，否则它会等待自己回复；明显非法地址也直接拒绝。
        if (!caller || caller->serviceRole() == services::ServiceRole::Memory ||
            address < 0x10000 || address >= 0x0000800000000000ULL ||
            (flags & 8)) {
            return false;
        }

        // 把硬件访问标志连同地址交给内存服务，服务据此区分读取、写入与执行缺页。
        ::task::IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(services::MmOperation::Fault);
        request.length   = 3;
        request.data[0]  = address;
        request.data[1]  = flags;
        const int result = exchange(request, reply);

        if (result || static_cast<int64_t>(reply.data[0])) {
            if (failure) {
                *failure = result ? result : static_cast<int>(reply.data[0]);
            }

            return false;
        }

        // 回复成功后再核对实际页表，而不是仅信任服务返回值；执行访问还要检查不可执行位。
        ::task::PreemptionSpinLockGuard guard(caller->addressSpace().lock());
        const uint64_t root = caller->addressSpace().rootPhysicalAddress();
        uint64_t actual     = 0;

        return root &&
               mm::validateUserMemory(root, address, 1, (flags & 2) != 0) &&
               mm::getPhysicalAddressInAddressSpace(root, address, &actual) &&
               (!(flags & 16) || !(actual & pageNoExecute));
    }

    /*
     * 通知内存服务为 fork 复制父任务的映射管理信息，目标子任务已由调用者创建。
     */
    bool PagerBridge::clone(::task::Task& parent, ::task::Task& child) {
        if (parent.serviceRole() == services::ServiceRole::Memory) {
            return false;
        }

        ::task::IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(services::MmOperation::Fork);
        request.length   = 1;
        request.data[0]  = child.id();
        const int result = exchange(request, reply, true);

        return !result && static_cast<int64_t>(reply.data[0]) == 0;
    }

    /*
     * 确保一段用户内存具备所需访问权限。
     * 已有页面先直接验证，缺页时逐页请求内存服务准备，再交给后续拷贝使用。
     */
    bool PagerBridge::ensureUserMemory(uint64_t address, uint64_t size,
                                       bool writable) {
        ::task::Task* caller = task::getCurrent();

        if (!caller) {
            return false;
        }

        if (!size) {
            return true;
        }

        if (address < 0x10000 || address >= 0x0000800000000000ULL ||
            size > 0x0000800000000000ULL - address) {
            return false;
        }

        // 首尾地址向页边界换算，逐页准备，避免只检查第一个字节却在后续跨页复制时出错。
        const uint64_t last = (address + size - 1) & ~4095ULL;

        for (uint64_t page = address & ~4095ULL;; page += 4096) {
            bool valid;
            {
                ::task::PreemptionSpinLockGuard guard(
                    caller->addressSpace().lock());
                valid = mm::validateUserMemory(
                    caller->addressSpace().rootPhysicalAddress(), page, 1,
                    writable);
            }

            // 已满足权限的页不再请求服务；缺页或写权限不足时走补页路径，保留写时复制的处理机会。
            if (!valid && !resolve(page, writable ? 2 : 0)) {
                return false;
            }

            if (page == last) {
                return true;
            }
        }
    }

}  // namespace mm
