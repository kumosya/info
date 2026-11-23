/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/service/mapping.cc
 * 检查调用身份，并执行普通页、页对象和共享内存映射。
 */

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/smp.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/device.h>

namespace mm {

    /*
     * 把内核错误码转换为接口约定的返回值，便于调用者按有符号结果判断失败。
     */
    static uint64_t error(::task::SyscallError value) {
        return static_cast<uint64_t>(static_cast<int64_t>(value));
    }

    /*
     * 判断调用任务是否为内存管理服务。
     * 跨任务内存操作需要使用这个身份检查。
     */
    static bool isMemoryManager(const ::task::Task& caller) {
        return caller.serviceRole() == os::ServiceRole::Memory &&
               caller.hasCapability(::task::taskCapabilityMemory) &&
               ::task::serviceRecovery().current(caller);
    }

    /*
     * 检查调用者是否有权修改目标任务的映射。
     * 普通任务只能操作允许的目标，内存服务按专用规则放行。
     */
    static bool mappingAuthorized(const ::task::Task& caller, uint64_t target) {
        return !target || target == caller.id() || isMemoryManager(caller);
    }

    /*
     * 建立目标任务的内存映射。
     * 先检查参数和权限，再按句柄类型选择普通页、页对象或共享内存路径，避免绕过对象的权限上限。
     */
    uint64_t MemoryService::map(uint64_t address, uint64_t size, uint64_t flags,
                                uint64_t handle, uint64_t offset,
                                uint64_t targetId) {
        ::task::Task* caller = task::getCurrent();

        if (!caller || !mappingAuthorized(*caller, targetId)) {
            return error(::task::SyscallError::PermissionDenied);
        }

        // 把目标任务固定下来后再操作地址空间，任务查找锁释放后也不会拿到悬空对象。
        ::task::TaskReference target =
            task::find(targetId ? targetId : caller->id());

        if (!target || target->isTerminated()) {
            return error(::task::SyscallError::NotFound);
        }

        const uint64_t protection = flags & 3;

        switch (flags & ~3ULL) {
            // 匿名请求申请普通页面；页对象和共享对象请求另走各自的句柄与来源检查。
            case OS_MAP_ANONYMOUS:
                if (handle || offset) {
                    return error(::task::SyscallError::InvalidArgument);
                }

                return mm::mapUserPages(*target, address, size, protection);
            // 从原句柄权限确定映射允许的最高权限，避免只读句柄以后通过修改页表取得写权限。
            case OS_MAP_PAGE_OBJECT: {
                if (protection == 3) {
                    return error(::task::SyscallError::PermissionDenied);
                }

                const uint32_t required =
                    ::task::handleRightRead |
                    ((protection & 1) ? ::task::handleRightWrite : 0);
                ::task::HandleTable::Pin pin(caller->handles(), handle,
                                             ::task::HandleType::PageObject,
                                             required);
                mm::PageObject* object = pin.as<mm::PageObject>();

                if (!object) {
                    return error(::task::SyscallError::InvalidHandle);
                }

                void* inspected         = nullptr;
                ::task::HandleType type = ::task::HandleType::None;
                uint32_t rights         = 0;

                if (!caller->handles().inspect(handle, &inspected, &type,
                                               &rights) ||
                    inspected != object ||
                    type != ::task::HandleType::PageObject) {
                    return error(::task::SyscallError::InvalidHandle);
                }

                const uint64_t maximum =
                    2 | ((rights & ::task::handleRightWrite) ? 1 : 0);

                return mappings_.map(*target, *object, address, size, offset,
                                     protection, maximum)
                           ? address
                           : error(::task::SyscallError::InvalidArgument);
            }

            // 跨任务映射使用目标任务内的临时句柄完成授权；建立成功后映射自己保留引用，临时句柄即可关闭。
            case OS_MAP_SHARED_OBJECT: {
                if (target == caller) {
                    return sharedMemory().map(*caller, handle, address, size,
                                              offset, protection);
                }

                const uint64_t copied =
                    sharedMemory().duplicateHandle(*caller, *target, handle);

                if (!copied) {
                    return error(::task::SyscallError::InvalidHandle);
                }

                const uint64_t result = sharedMemory().map(
                    *target, copied, address, size, offset, protection);
                target->handles().close(copied);

                return result;
            }

            default:
                return error(::task::SyscallError::InvalidArgument);
        }
    }

    /*
     * 撤销目标任务的地址区间映射。
     * 确认目标权限后交给统一解除映射流程，是否保留锁页记录由参数决定。
     */
    uint64_t MemoryService::unmap(uint64_t address, uint64_t size,
                                  uint64_t targetId, bool preserveLocks) {
        ::task::Task* caller = task::getCurrent();

        if (!caller || !mappingAuthorized(*caller, targetId)) {
            return error(::task::SyscallError::PermissionDenied);
        }

        if (preserveLocks && !isMemoryManager(*caller)) {
            return error(::task::SyscallError::PermissionDenied);
        }

        ::task::TaskReference target =
            task::find(targetId ? targetId : caller->id());

        return !target || target->isTerminated()
                   ? error(::task::SyscallError::NotFound)
                   : mm::unmapUserPages(*target, address, size, preserveLocks);
    }

    /*
     * 调整目标区间的读写执行权限。
     * 先核对操作资格，再由底层同时检查页表和映射对象允许的最大权限。
     */
    uint64_t MemoryService::protect(uint64_t address, uint64_t size,
                                    uint64_t protection, uint64_t targetId) {
        ::task::Task* caller = task::getCurrent();

        if (!caller || !mappingAuthorized(*caller, targetId)) {
            return error(::task::SyscallError::PermissionDenied);
        }

        ::task::TaskReference target =
            task::find(targetId ? targetId : caller->id());

        // 查找成功也要确认目标尚未终止，不能为已经退出的任务重新修改内存状态。
        if (!target || target->isTerminated()) {
            return error(::task::SyscallError::NotFound);
        }

        if (protection == OS_PROTECT_COPY_ON_WRITE) {
            if (!isMemoryManager(*caller)) {
                return error(::task::SyscallError::PermissionDenied);
            }

            ::task::PreemptionSpinLockGuard guard(
                target->addressSpace().lock());

            return mappings_.markCopyOnWrite(*target, address, size)
                       ? 0
                       : error(::task::SyscallError::InvalidArgument);
        }

        // 最终交给公共权限修改流程，让页表权限与对象允许上限采用相同检查。
        return mm::protectUserPages(*target, address, size, protection);
    }

}  // namespace mm
