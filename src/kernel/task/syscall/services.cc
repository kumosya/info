/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/syscall/services.cc
 * 封装内核服务调用、句柄检查及设备访问的公共入口。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/sync.h"
#include "kernel/syscall.h"
#include "kernel/task.h"
#include "kernel/time.h"
#include "service/common/records.h"

namespace task {

    /*
     * 检查端点句柄类型和权限，并用 Pin
     * 保留对象，使操作期间关闭句柄也不会释放端点。
     */
    static HandleTable::Pin endpoint(uint64_t handle, uint32_t rights) {
        return HandleTable::Pin(task::getCurrent()->handles(), handle,
                                HandleType::Endpoint, rights);
    }

    /*
     * 检查当前任务是否具有设备访问能力，作为硬件相关接口的公共入口校验。
     */
    static bool deviceAccess() {
        Task* task = task::getCurrent();

        return task && task->isSystemService() &&
               task->hasCapability(taskCapabilityDevice);
    }

    /*
     * 取得指定核心服务句柄，安装和授权由核心服务注册表负责。
     */
    uint64_t serviceCoreHandle(services::CoreServiceKind kind) {
        return coreServices().getHandle(*task::getCurrent(), kind);
    }

    /*
     * 读取当前任务的标准输入、输出或错误句柄。
     */
    uint64_t serviceStandardHandle(uint32_t index) {
        return task::getCurrent()->standardHandle(index);
    }

    /*
     * 返回当前线程编号。
     */
    uint64_t serviceThreadId() { return currentThread()->getId(); }

    /*
     * 在用户地址值保持为给定值时等待，转交地址等待机制处理检查与入队。
     */
    int serviceWaitAddress(volatile uint32_t* address, uint32_t expected) {
        if ((reinterpret_cast<uint64_t>(address) & 3) != 0) {
            return -1;
        }

        const int result = task::waitOnUserAddress(address, expected);

        return result == -10 ? -10 : result < 0 ? -1 : result > 0 ? -6 : 0;
    }

    /*
     * 唤醒在指定用户地址等待的线程，数量受参数限制。
     */
    int serviceWakeAddress(volatile uint32_t* address, uint32_t count) {
        return (reinterpret_cast<uint64_t>(address) & 3)
                   ? -1
                   : task::wakeUserAddress(address, count);
    }

    /*
     * 复制句柄并限制新权限，返回新编号或对应错误。
     */
    int64_t serviceDuplicateHandle(uint64_t handle, uint32_t rights,
                                   uint32_t flags) {
        const uint64_t result =
            task::getCurrent()->handles().duplicate(handle, rights, flags);

        return result ? static_cast<int64_t>(result) : -1;
    }

    /*
     * 收紧句柄访问权限，不能增加原句柄未持有的权限。
     */
    int serviceRestrictHandle(uint64_t handle, uint32_t rights) {
        return task::getCurrent()->handles().restrictRights(handle, rights)
                   ? 0
                   : -3;
    }

    /*
     * 设置句柄标志，具体有效性由句柄表校验。
     */
    int serviceSetHandleFlags(uint64_t handle, uint32_t flags) {
        return task::getCurrent()->handles().setFlags(handle, flags) ? 0 : -1;
    }

    /*
     * 查询句柄状态，失效句柄返回对应错误。
     */
    int serviceHandleState(uint64_t handle) {
        HandleTable::Pin pin  = endpoint(handle, 0);
        task::Endpoint* value = pin.as<Endpoint>();

        return value ? (value->isClosed() ? 1 : 0) : -2;
    }

    /*
     * 为当前任务创建通信端点并安装句柄，失败时释放新端点。
     */
    uint64_t serviceCreateEndpoint() {
        Endpoint* value = Endpoint::create();

        return value ? task::getCurrent()->handles().add(
                           value, HandleType::Endpoint, 7)
                     : 0;
    }

    /*
     * 读取当前任务启动时获得的引导句柄。
     */
    uint64_t serviceBootstrapHandle() {
        return task::getCurrent()->bootstrapHandle();
    }

    /*
     * 关闭当前任务的指定句柄，归还它持有的对象引用。
     */
    bool serviceCloseHandle(uint64_t handle) {
        return task::getCurrent()->handles().close(handle);
    }

    /*
     * 结束整个当前任务，内部先终止其他线程再退出调用线程。
     */
    void serviceExitTask(int code) { task::exitCurrent(code); }

    /*
     * 只结束当前线程，任务是否结束由剩余线程数量决定。
     */
    void serviceExitThread(int code) { exitCurrentThread(code); }

    /*
     * 主动让出 CPU，让调度器重新选择可运行线程。
     */
    void serviceYield() { task::yieldThread(); }

    /*
     * 让当前线程休眠指定毫秒数，唤醒时间由调度器时钟处理。
     */
    void serviceSleep(uint64_t milliseconds) {
        sleepCurrentThread(milliseconds);
    }

    /*
     * 使用用户入口和栈创建一个无参数线程，复用统一线程创建机制。
     */
    uint64_t serviceCreateThread(void (*entry)(), uint64_t stack) {
        return serviceCreateThreadWithArgument(
            reinterpret_cast<void (*)(void*)>(entry), stack, nullptr);
    }

    /*
     * 创建带一个参数的用户线程，参数通过初始寄存器现场传递。
     */
    uint64_t serviceCreateThreadWithArgument(void (*entry)(void*),
                                             uint64_t stack, void* argument) {
        Thread* thread = Thread::createUser(
            task::getCurrent(), reinterpret_cast<uint64_t>(entry), stack,
            reinterpret_cast<uint64_t>(argument), 0, ~0ULL, false);

        if (!thread) {
            return 0;
        }

        const uint64_t id = thread->getId();
        task::enqueueThread(thread);

        return id;
    }

    /*
     * 固定发送端点对象后发送消息，截止时间交给端点等待逻辑处理。
     */
    int serviceSend(uint64_t handle, os::IpcMessage& message,
                    uint64_t deadline) {
        HandleTable::Pin pin   = endpoint(handle, handleRightWrite);
        task::Endpoint* target = pin.as<Endpoint>();

        return target ? target->send(&message, deadline)
                      : static_cast<int>(os::ErrorCode::InvalidHandle);
    }

    /*
     * 固定接收端点并读取消息，接收权限和对象寿命在调用期间得到保证。
     */
    int serviceReceive(uint64_t handle, os::IpcMessage& message,
                       uint64_t deadline) {
        HandleTable::Pin pin   = endpoint(handle, handleRightRead);
        task::Endpoint* target = pin.as<Endpoint>();

        return target ? target->receive(&message, deadline)
                      : static_cast<int>(os::ErrorCode::InvalidHandle);
    }

    /*
     * 向端点发起同步调用并等待回复，统一处理超时和服务上下文要求。
     */
    int serviceCall(uint64_t handle, os::IpcMessage& request,
                    os::IpcMessage& reply, uint64_t deadline) {
        HandleTable::Pin pin   = endpoint(handle, handleRightWrite);
        task::Endpoint* target = pin.as<Endpoint>();

        if (!target) {
            return static_cast<int>(os::ErrorCode::InvalidHandle);
        }

        // 服务可能使用另一套页表完成调用；内核栈共享，但程序全局变量和堆地址属于各自私有地址空间。

        // 回复先保存在共享的内核栈对象中，成功后才复制给调用方；服务执行期间不能依赖原程序私有堆地址。
        os::IpcMessage completion{};
        const int result = target->call(&request, &completion, deadline);

        if (!result) {
            reply = completion;
        }

        return result;
    }

    /*
     * 把调用令牌写入回复消息，再交给对应端点完成同步调用。
     */
    int serviceReply(uint64_t handle, uint64_t token, os::IpcMessage& message) {
        HandleTable::Pin pin   = endpoint(handle, handleRightRead);
        task::Endpoint* target = pin.as<Endpoint>();
        message.replyToken     = token;

        return target ? target->reply(&message)
                      : static_cast<int>(os::ErrorCode::InvalidHandle);
    }

    /*
     * 取消端点上指定线程的未完成调用，先验证调用者的端点访问权限。
     */
    int serviceCancel(uint64_t handle, uint64_t thread) {
        HandleTable::Pin pin   = endpoint(handle, 0);
        task::Endpoint* target = pin.as<Endpoint>();

        return target ? target->cancel(thread)
                      : static_cast<int>(os::ErrorCode::InvalidHandle);
    }

    /*
     * 读取系统单调时间，不受墙上时钟调整影响。
     */
    uint64_t serviceMonotonicTime() { return time::getMonotonicTime(); }

    /*
     * 将文本写入内核日志，供服务启动和故障诊断使用。
     */
    void serviceLog(const char* text) {
        if (text) {
            device::serialWrite(text);
        }
    }

    /*
     * 查询任务信息并按接口格式返回，查找期间保留目标任务引用。
     */
    int serviceQueryTask(uint64_t id, services::wire::TaskInfo& info) {
        ::task::TaskReference target = task::find(id);

        if (!target) {
            return static_cast<int>(os::ErrorCode::NotFound);
        }

        info                      = {};
        info.id                   = target->id();
        info.trustedDeviceService = target->isSystemService() &&
                                    target->hasCapability(taskCapabilityDevice);

        return 0;
    }

    /*
     * 检查设备权限并映射 PCI 设备的 MMIO 区间，使调用者可以访问设备寄存器。
     */
    uint64_t serviceMapMmio(const services::wire::PciFunctionInfo& function,
                            uint8_t bar, uint64_t address, uint64_t size) {
        if (!deviceAccess()) {
            return 0;
        }

        void* storage = mm::allocateHeap(sizeof(device::MmioRegion));
        device::MmioRegion* region = storage ? new (storage) device::MmioRegion : nullptr;
        if (region && !region->map(*task::getCurrent(), function, bar, address, size)) {
            mm::freeHeap(region);
            region = nullptr;
        }

        // 映射资源对象准备好后再安装句柄，失败时销毁对象以撤销尚未交给调用者的设备资源。
        const uint64_t handle = region ? task::getCurrent()->handles().add(
                                             region, HandleType::MmioRegion, 3)
                                       : 0;

        if (region && !handle) {
            region->destroy();
        }

        return handle;
    }

    /*
     * 为设备申请 DMA 缓冲区，记录设备与任务归属，并返回物理和映射信息。
     */
    int serviceAllocateDma(uint64_t device, uint64_t address,
                           services::wire::DmaBufferInfo& info) {
        if (!deviceAccess()) {
            return static_cast<int>(os::ErrorCode::PermissionDenied);
        }

        HandleTable::Pin pin(task::getCurrent()->handles(), device,
                             HandleType::DeviceDomain, handleRightManage);
        device::ResourceDomain* domain = pin.as<device::ResourceDomain>();

        // DMA
        // 申请必须绑定有效的设备资源域，不能仅凭一个设备编号取得不受管理的物理内存。
        if (!domain) {
            return static_cast<int>(os::ErrorCode::InvalidHandle);
        }

        void* storage = mm::allocateHeap(sizeof(device::DmaBuffer));
        device::DmaBuffer* buffer = storage ? new (storage) device::DmaBuffer : nullptr;
        if (buffer && !buffer->create(*task::getCurrent(), *domain, address, info)) {
            mm::freeHeap(buffer);
            buffer = nullptr;
        }

        if (!buffer) {
            return static_cast<int>(os::ErrorCode::OutOfMemory);
        }

        info.handle =
            task::getCurrent()->handles().add(buffer, HandleType::DmaBuffer, 3);

        // 缓冲区已经建立但句柄安装失败，立即销毁缓冲区，避免设备资源域里留下无法关闭的分配。
        if (!info.handle) {
            buffer->destroy();
        }

        return info.handle ? 0 : static_cast<int>(os::ErrorCode::OutOfMemory);
    }

    /*
     * 把硬件中断绑定到通知端点，检查 IRQ、触发模式和端点权限。
     */
    uint64_t serviceBindIrq(uint8_t irq, uint64_t handle, bool levelTriggered) {
        HandleTable::Pin pin   = endpoint(handle, handleRightRead);
        task::Endpoint* target = pin.as<Endpoint>();

        if (!deviceAccess() || !target || irq > 15) {
            return 0;
        }

        void* storage = mm::allocateHeap(sizeof(device::InterruptBinding));
        device::InterruptBinding* binding = storage ? new (storage) device::InterruptBinding : nullptr;
        if (binding && !binding->create(*task::getCurrent(), irq, target, levelTriggered)) {
            mm::freeHeap(binding);
            binding = nullptr;
        }
        const uint64_t result = binding ? task::getCurrent()->handles().add(
            binding, HandleType::InterruptBinding, handleRightManage) : 0;
        if (binding && !result) {
            binding->destroy();
        }
        return result;
    }

    /*
     * 确认中断已经处理，解除需要显式确认的中断屏蔽状态。
     */
    int serviceAcknowledgeIrq(uint64_t handle) {
        device::InterruptBinding* binding =
            static_cast<device::InterruptBinding*>(
                task::getCurrent()->handles().get(
                    handle, HandleType::InterruptBinding, handleRightManage));

        return deviceAccess() && binding &&
                       binding->acknowledge(*task::getCurrent())
                   ? 0
                   : -1;
    }

    /*
     * 按名称解析允许服务调用的内核接口，只暴露明确列出的符号。
     */
    uint64_t serviceResolve(const char* symbol) {
        struct Import {
                const char* name;
                uint64_t address;
        };

        // 允许导入的内核函数在固定列表中逐项声明，不提供任意符号地址查询。
        const Import imports[] = {
            {"_ZN2os17getStandardHandleEj",
             reinterpret_cast<uint64_t>(&serviceStandardHandle)},
            {"_ZN2os11unmapMemoryEmm",
             reinterpret_cast<uint64_t>(&serviceUnmapMemory)},
            {"_ZN2os13protectMemoryEmmm",
             reinterpret_cast<uint64_t>(&serviceProtectMemory)},
            {"_ZN2os11waitAddressEPVjj",
             reinterpret_cast<uint64_t>(&serviceWaitAddress)},
            {"_ZN2os15notifyReadinessEv",
             reinterpret_cast<uint64_t>(&task::notifyReadiness)},
            {"_ZN2os11wakeAddressEPVjj",
             reinterpret_cast<uint64_t>(&serviceWakeAddress)},
            {"_ZN2os18getBootstrapHandleEv",
             reinterpret_cast<uint64_t>(&serviceBootstrapHandle)},
            {"_ZN2os11closeHandleEm",
             reinterpret_cast<uint64_t>(&serviceCloseHandle)},
            {"_ZN2os8exitTaskEi", reinterpret_cast<uint64_t>(&serviceExitTask)},
            {"_ZN2os10exitThreadEi",
             reinterpret_cast<uint64_t>(&serviceExitThread)},
            {"_ZN2os5yieldEv", reinterpret_cast<uint64_t>(&serviceYield)},
            {"_ZN2os5sleepEm", reinterpret_cast<uint64_t>(&serviceSleep)},
            {"_ZN2os9mapMemoryEmmm",
             reinterpret_cast<uint64_t>(&serviceMapMemory)},
            {"_ZN2os12createThreadEPFvvEm",
             reinterpret_cast<uint64_t>(&serviceCreateThread)},
            {"_ZN2os12createThreadEPFvPvEmS0_",
             reinterpret_cast<uint64_t>(&serviceCreateThreadWithArgument)},
            {"_ZN2os11sendMessageEmRNS_10IpcMessageEm",
             reinterpret_cast<uint64_t>(&serviceSend)},
            {"_ZN2os14receiveMessageEmRNS_10IpcMessageEm",
             reinterpret_cast<uint64_t>(&serviceReceive)},
            {"_ZN2os11callServiceEmRNS_10IpcMessageES1_m",
             reinterpret_cast<uint64_t>(&serviceCall)},
            {"_ZN2os12replyMessageEmmRNS_10IpcMessageE",
             reinterpret_cast<uint64_t>(&serviceReply)},
        };

        // 仅按完整名字匹配受信接口，未列出的符号返回零，即使它存在于内核也不向服务公开。
        for (const Import& item : imports) {
            uint32_t i = 0;

            while (item.name[i] && symbol[i] == item.name[i]) {
                ++i;
            }

            if (!item.name[i] && !symbol[i]) {
                return item.address;
            }
        }

        return 0;
    }

    /*
     * 为当前任务映射普通用户内存，参数检查由公共内存操作完成。
     */
    uint64_t serviceMapMemory(uint64_t address, uint64_t size,
                              uint64_t protection) {
        Task* caller = task::getCurrent();

        return caller ? mm::mapUserPages(*caller, address, size, protection)
                      : static_cast<uint64_t>(-1);
    }

    /*
     * 撤销当前任务的用户内存区间映射。
     */
    bool serviceUnmapMemory(uint64_t address, uint64_t size) {
        Task* caller = task::getCurrent();

        return caller ? mm::unmapUserPages(*caller, address, size) == 0 : false;
    }

    /*
     * 调整当前任务指定内存区间的权限。
     */
    bool serviceProtectMemory(uint64_t address, uint64_t size,
                              uint64_t protection) {
        Task* caller = task::getCurrent();

        return caller ? mm::protectUserPages(*caller, address, size,
                                             protection) == 0
                      : false;
    }

}  // namespace task
