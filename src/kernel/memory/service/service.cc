/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/service/service.cc
 * 校验内存服务请求，分发页对象、映射、查询及锁页操作。
 */

#include <new>

#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/kernel-requests.h"
#include "service/common/records.h"
#include <os/mm.h>

namespace mm {

    /*
     * 从请求数据指定位置取出名字，检查消息边界和字符串终止符。
     * 拒绝缺少终止符的名字，防止后续读取越界。
     */
    static bool readRequestName(const ::task::IpcMessage& request,
                                uint32_t word, const char*& name) {
        if (request.length <= word) {
            return false;
        }

        name = reinterpret_cast<const char*>(&request.data[word]);

        for (uint32_t i = 0; i < 64 && i < (request.length - word) * 8; ++i) {
            if (!name[i]) {
                return i != 0;
            }
        }

        return false;
    }

    /*
     * 按字节复制请求中的固定大小数据，不依赖用户态运行库。
     */
    static void copyRequestBytes(void* destination, const void* source,
                                 size_t size) {
        uint8_t* output      = static_cast<uint8_t*>(destination);
        const uint8_t* input = static_cast<const uint8_t*>(source);

        for (size_t i = 0; i < size; ++i) {
            output[i] = input[i];
        }
    }

    /*
     * 检查请求是否有权访问目标任务的内存对象，防止普通任务任意操作其他进程。
     */
    static bool requestAuthorized(::task::Task& caller, uint64_t target) {
        return !target || target == caller.id() ||
               (caller.serviceRole() == os::ServiceRole::Memory &&
                caller.hasCapability(::task::taskCapabilityMemory) &&
                ::task::serviceRecovery().current(caller));
    }

    /*
     * 分发内存内核服务请求。
     * 每个分支先校验消息布局和权限，再调用页对象、映射或锁页操作，把结果按协议写入回复。
     */
    int MemoryService::call(::task::Task& caller,
                            const ::task::IpcMessage& request,
                            ::task::IpcMessage& reply) {
        if (request.length < 5) {
            return -1;
        }

        const uint64_t* args = request.data;
        const char* name     = nullptr;

        switch (static_cast<os::MemoryOperation>(request.type)) {
            // 锁页请求既影响页表标志又影响额度记录，先定位目标，再交给整段预检查和提交路径。
            case os::MemoryOperation::LockRange:
            case os::MemoryOperation::LockAll:
            case os::MemoryOperation::LockState: {
                if (!requestAuthorized(caller, args[0])) {
                    return -3;
                }

                ::task::TaskReference target =
                    task::find(args[0] ? args[0] : caller.id());

                if (!target) {
                    return -4;
                }

                if (request.type == uint64_t(os::MemoryOperation::LockRange)) {
                    if (args[3] > 7) {
                        return -1;
                    }

                    if ((args[3] & 6) &&
                        caller.serviceRole() != os::ServiceRole::Memory) {
                        return -3;
                    }

                    reply.data[1] =
                        lockUserPages(*target, args[1], args[2], args[3] & 1,
                                      args[3] & 2, args[3] & 4);
                } else if (request.type ==
                           uint64_t(os::MemoryOperation::LockAll)) {
                    if (args[1] > 3 || args[2] > 128 ||
                        (args[2] &&
                         (caller.serviceRole() != os::ServiceRole::Memory ||
                          request.length < 3 + args[2] * 2))) {
                        return -1;
                    }

                    reply.data[1] =
                        lockAllUserPages(*target, unsigned(args[1]), args + 3,
                                         unsigned(args[2]));
                } else {
                    ::task::PreemptionSpinLockGuard guard(
                        target->addressSpace().lock());
                    reply.data[1] = target->addressSpace().lockedBytes();
                    reply.data[2] = target->addressSpace().lockFuture();
                    reply.data[3] = target->memlockLimit();
                }

                break;
            }

            case os::MemoryOperation::Map:
                if (request.length < 6) {
                    return -1;
                }

                reply.data[0] =
                    map(args[0], args[1], args[2], args[3], args[4], args[5]);
                break;
            // 不能解除或撤销同步回复缓冲区的写权限，否则操作成功后反而无法把结果写回调用者。
            case os::MemoryOperation::Unmap:
            case os::MemoryOperation::Protect: {
                const bool protectOperation =
                    request.type ==
                    static_cast<uint64_t>(os::MemoryOperation::Protect);
                const uint64_t target = args[protectOperation ? 3 : 2];
                ::task::SyscallContext* context =
                    ::task::currentThread()->syscallContext();
                // 同步请求必须保留可写的回复位置。
                if ((!target || target == caller.id()) && context && args[1] &&
                    args[0] <= UINT64_MAX - args[1] &&
                    context->rdx <= UINT64_MAX - sizeof(::task::IpcMessage) &&
                    args[0] < context->rdx + sizeof(::task::IpcMessage) &&
                    context->rdx < args[0] + args[1] &&
                    (!protectOperation || !(args[2] & 1))) {
                    return -1;
                }

                reply.data[0] =
                    protectOperation
                        ? protect(args[0], args[1], args[2], target)
                        : unmap(args[0], args[1], target, args[3] != 0);
                break;
            }

            // 绑定期间固定具有接收权限的端点；桥接层还会检查端点所有者和当前内存服务身份。

            // 绑定对象必须是能接收请求的端点，并由桥接层进一步核对所有者与服务当前实例身份。
            case os::MemoryOperation::BindPager: {
                ::task::HandleTable::Pin pin(caller.handles(), args[0],
                                             ::task::HandleType::Endpoint,
                                             ::task::handleRightRead);

                if (!pager_.bind(caller, pin.as<::task::Endpoint>())) {
                    return -3;
                }

                break;
            }

            // 只有当前内存服务可以冻结或恢复其他进程；返回页表代号用于识别服务重启期间目标是否执行过
            // exec。
            case os::MemoryOperation::Freeze:
            case os::MemoryOperation::Thaw: {
                if (caller.serviceRole() != os::ServiceRole::Memory ||
                    !caller.hasCapability(::task::taskCapabilityMemory) ||
                    !::task::serviceRecovery().current(caller)) {
                    return -3;
                }

                ::task::TaskReference target = task::find(args[0]);

                if (!args[0] || !target || target->isTerminated()) {
                    return -4;
                }

                if (!target->setMemoryFrozen(
                        request.type ==
                        static_cast<uint64_t>(os::MemoryOperation::Freeze))) {
                    return -8;
                }

                ::task::PreemptionSpinLockGuard guard(
                    target->addressSpace().lock());
                reply.data[1] = target->addressSpace().generation();
                break;
            }

            // 新内存服务按递增 PID
            // 查找旧实例留下的冻结任务，不依赖随时可能变化的链表位置。
            case os::MemoryOperation::FrozenNext: {
                if (caller.serviceRole() != os::ServiceRole::Memory ||
                    !caller.hasCapability(::task::taskCapabilityMemory) ||
                    !::task::serviceRecovery().current(caller)) {
                    return -3;
                }

                uint64_t cursor = args[0] ? args[0] : 1;

                while (true) {
                    ::task::TaskReference target = task::next(cursor);

                    if (!target) {
                        reply.data[1] = 0;
                        break;
                    }

                    cursor = target->id() + 1;

                    if (target->memoryFrozen() && !target->isTerminated()) {
                        reply.data[1] = target->id();
                        break;
                    }
                }

                break;
            }

            // 要求目标已冻结，再按虚拟地址合并页对象与共享映射的枚举结果；外部页面不能冒充独立匿名页恢复。

            // 服务恢复枚举只面向已经冻结的目标，避免边枚举边被客户线程修改映射关系。
            case os::MemoryOperation::MappingNext: {
                if (caller.serviceRole() != os::ServiceRole::Memory ||
                    !caller.hasCapability(::task::taskCapabilityMemory) ||
                    !::task::serviceRecovery().current(caller)) {
                    return -3;
                }

                ::task::TaskReference target = task::find(args[0]);

                if (!target || !target->memoryFrozen()) {
                    return -8;
                }

                uint64_t pageAddress = 0, pageSize = 0, sharedAddress = 0,
                         sharedSize = 0;
                const bool page     = mappings_.nextManagedMapping(
                    *target, args[1], pageAddress, pageSize);
                const bool shared = sharedMemory().nextManagedMapping(
                    *target, args[1], sharedAddress, sharedSize);
                reply.data[1] = reply.data[2] = 0;

                if (page && (!shared || pageAddress < sharedAddress)) {
                    reply.data[1] = pageAddress;
                    reply.data[2] = pageSize;
                } else if (shared) {
                    reply.data[1] = sharedAddress;
                    reply.data[2] = sharedSize;
                }

                break;
            }

            // 只公开选定的状态、权限、访问和脏页标志，不泄露物理地址或全部内部页表位。
            case os::MemoryOperation::PageState: {
                if (!requestAuthorized(caller, args[0])) {
                    return -3;
                }

                ::task::TaskReference target =
                    task::find(args[0] ? args[0] : caller.id());

                if (!target) {
                    return -4;
                }

                ::task::PreemptionSpinLockGuard guard(
                    target->addressSpace().lock());
                const uint64_t root =
                    target->addressSpace().rootPhysicalAddress();
                uint64_t flags = 0;

                if (root && mm::getPhysicalAddressInAddressSpace(root, args[1],
                                                                 &flags)) {
                    reply.data[1] =
                        flags & (pagePresent | pageWritable | pageUser |
                                 pageExternal | pageLocked | pageNoExecute |
                                 (1ULL << 5) | (1ULL << 6));
                }

                if (target->addressSpace().residency().contains(args[1])) {
                    reply.data[1] |= pageLocked;
                }

                break;
            }

            // 持页表根锁检查有界且对齐的区间；结果仅表示检查时可用，解锁后并不替调用者保留地址。

            // 地址可用性查询只观察当前状态，不保留该区间；后续真正映射仍需重新核对。
            case os::MemoryOperation::RangeAvailable: {
                if (!requestAuthorized(caller, args[0])) {
                    return -3;
                }

                if (!args[2] || ((args[1] | args[2]) & 4095) ||
                    args[1] < 0x10000 || args[1] >= 0x0000800000000000ULL ||
                    args[2] > 0x0000800000000000ULL - args[1]) {
                    return -1;
                }

                ::task::TaskReference target =
                    task::find(args[0] ? args[0] : caller.id());

                if (!target || target->isTerminated()) {
                    return -4;
                }

                ::task::PreemptionSpinLockGuard guard(
                    target->addressSpace().lock());
                const uint64_t root =
                    target->addressSpace().rootPhysicalAddress();

                if (!root) {
                    return -4;
                }

                reply.data[1] = 1;

                for (uint64_t offset = 0; offset < args[2]; offset += 4096) {
                    if (mm::getPhysicalAddressInAddressSpace(
                            root, args[1] + offset)) {
                        reply.data[1] = 0;
                        break;
                    }
                }

                break;
            }

            // 名称、打开句柄和映射分别有自己的寿命；新建的调用方句柄必须在回复的句柄数组中明确列出。
            case os::MemoryOperation::Open:
                if (!readRequestName(request, 5, name) ||
                    args[0] > UINT32_MAX) {
                    return -1;
                }

                reply.data[0] = sharedMemory().open(caller, name, args[0]);

                if (reply.data[0]) {
                    reply.handles[reply.handleCount++] = reply.data[0];
                }

                break;
            case os::MemoryOperation::Unlink:
                if (!readRequestName(request, 5, name)) {
                    return -1;
                }

                reply.data[0] = sharedMemory().unlink(name) ? 0 : -1;
                break;
            case os::MemoryOperation::Resize:
                reply.data[0] =
                    sharedMemory().resize(caller, args[0], args[1]) ? 0 : -1;
                break;

            // 类型化内存句柄描述的是池和分配策略，实际页面由后续分配请求独立取得。
            case os::MemoryOperation::TypedOpen: {
                if (!readRequestName(request, 5, name)) {
                    return -1;
                }

                const char expected[] = "/memory/ram";
                unsigned i            = 0;

                while (expected[i] && name[i] == expected[i]) {
                    ++i;
                }

                if (name[i] != expected[i]) {
                    return -4;
                }

                const int64_t handle = sharedMemory().openTyped(
                    caller, int(args[0]), unsigned(args[1]));

                if (handle <= 0) {
                    return int(handle);
                }

                reply.data[0]                      = handle;
                reply.handles[reply.handleCount++] = handle;
                break;
            }

            case os::MemoryOperation::TypedAllocate: {
                const int64_t handle =
                    sharedMemory().allocateTyped(caller, args[0], args[1]);

                if (handle <= 0) {
                    return int(handle);
                }

                reply.data[0]                      = handle;
                reply.handles[reply.handleCount++] = handle;
                break;
            }

            case os::MemoryOperation::TypedInfo:
                return sharedMemory().typedInfo(caller, args[0], reply.data[1]);
            case os::MemoryOperation::SharedInfo:
                if (!sharedMemory().querySize(caller, args[0], reply.data[1])) {
                    return -2;
                }

                break;
            case os::MemoryOperation::UnlinkSharedHandle:
                if (!sharedMemory().unlink(caller, args[0])) {
                    return -4;
                }

                break;
            // 先建立无名称的后备对象并调整大小，再公布句柄；失败时关闭唯一句柄，使无人使用的对象可以回收。
            case os::MemoryOperation::AnonymousShared: {
                if (!args[0] || args[0] > 64 * 4096) {
                    return -1;
                }

                const uint64_t handle =
                    sharedMemory().open(caller, nullptr, 0x42);

                if (!handle) {
                    return -5;
                }

                if (!sharedMemory().resize(caller, handle, args[0])) {
                    caller.handles().close(handle);

                    return -5;
                }

                reply.data[0]                      = handle;
                reply.handles[reply.handleCount++] = handle;
                break;
            }

            // 只有对象包装还不够，必须实际分配并清零页面后才能安装句柄；安装失败时归还引用，不能留下无主物理块。

            // 页对象包装和实际物理页都成功后才安装句柄；失败不能发布一个没有页面的对象。
            case os::MemoryOperation::AllocatePages: {
                if (args[0] > 12 || args[1] > UINT16_MAX ||
                    !requestAuthorized(caller, args[2])) {
                    return -3;
                }

                if (args[1] != anyMemoryNode &&
                    args[1] >= memoryTopology().nodeCount()) {
                    return -1;
                }

                void* storage = mm::allocateHeap(sizeof(PageObject));

                if (!storage) {
                    return -5;
                }

                PageObject* object = new (storage) PageObject(
                    args[0], args[2] ? args[2] : caller.id(), args[1]);

                if (!object->valid()) {
                    object->release();

                    return -5;
                }

                const uint64_t handle = caller.handles().add(
                    object, ::task::HandleType::PageObject,
                    ::task::handleRightRead | ::task::handleRightWrite |
                        ::task::handleRightManage);

                if (!handle) {
                    object->release();

                    return -5;
                }

                reply.data[0]                      = handle;
                reply.handles[reply.handleCount++] = handle;
                break;
            }

            // 读取物理信息期间固定对象，避免读取字段与填写回复之间被关闭释放。
            case os::MemoryOperation::PageInfo: {
                ::task::HandleTable::Pin pin(caller.handles(), args[0],
                                             ::task::HandleType::PageObject,
                                             ::task::handleRightRead);
                mm::PageObject* object = pin.as<PageObject>();

                if (!object) {
                    return -2;
                }

                PhysicalPageInfo physical{};
                queryPhysicalPage(object->physical(), physical);
                const os::wire::PageObjectInfo info{
                    object->owner(), object->size(), physical.getReferences(),
                    physical.getNode(), static_cast<uint16_t>(object->order())};
                copyRequestBytes(&reply.data[1], &info, sizeof(info));
                break;
            }

            // 这里分别采样映像代号和内存计数，不保证全局同时刻快照；物理占用也不能把所有虚拟别名重复相加。

            // 物理占用按所有者统计，虚拟地址的多个别名不能重复累计成多份物理内存。
            case os::MemoryOperation::Usage: {
                if (!requestAuthorized(caller, args[0])) {
                    return -3;
                }

                ::task::TaskReference target =
                    task::find(args[0] ? args[0] : caller.id());

                if (!target || target->isTerminated()) {
                    return -4;
                }

                uint64_t generation;
                {
                    ::task::PreemptionSpinLockGuard guard(
                        target->addressSpace().lock());
                    generation = target->addressSpace().generation();
                }

                const os::wire::MemoryUsage info{
                    getTotalMemoryBytes(),
                    getFreeMemoryBytes(),
                    getOwnedMemoryBytes(target->id()),
                    generation,
                    memoryTopology().nodeCount(),
                    0};
                copyRequestBytes(&reply.data[1], &info, sizeof(info));
                break;
            }

            // 清空公共内核缓存需要明确的内存管理能力，不能因为它不直接修改用户映射就跳过权限检查。
            case os::MemoryOperation::ReclaimCaches:
                if (!caller.hasCapability(::task::taskCapabilityMemory)) {
                    return -3;
                }

                reply.data[0] = mm::reclaimCaches();
                break;
            default:
                return -1;
        }

        // 各分支只填自己的结果字段，统一在末尾声明回复容量，未知操作则提前返回错误。
        reply.length = os::ipcDataWordCount;

        return 0;
    }

}  // namespace mm

namespace task {

    /*
     * 将通用端点请求转交给内存服务实现。
     */
    static int dispatchMemory(Task& caller, const IpcMessage& request,
                              IpcMessage& reply) {
        static mm::MemoryService implementation(mm::maps(), mm::pagerBridge());

        return implementation.call(caller, request, reply);
    }

    /*
     * 返回可供端点调用的内存内核服务描述，统一登记服务入口及其属性。
     */
    EndpointService& memoryKernelService() {
        static EndpointService service(dispatchMemory);

        return service;
    }

}  // namespace task
