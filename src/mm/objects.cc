/* 服务对象的成员函数实现。 */
#include "service/mm/mm.h"

namespace mm {

    /* 一个连接对象复用同一内核内存服务通道。 */
    KernelMemory::KernelMemory() : channel_(services::CoreServiceKind::Memory) {
    }

    /* 构造并提交内核内存控制请求。 */
    int KernelMemory::control(services::MemoryOperation operation,
                              uint64_t task, uint64_t cursor,
                              os::IpcMessage& reply) {
        os::IpcMessage request{};
        request.type = static_cast<uint64_t>(operation);
        request.length = 5;
        request.data[0] = task;
        request.data[1] = cursor;
        return channel_.result(request, reply);
    }

    /* 查询或修改地址范围的锁页状态。 */
    int KernelMemory::residency(uint64_t task, uint64_t address, uint64_t size,
                                bool lock, bool allowAbsent, bool checkOnly) {
        os::IpcMessage request{}, reply{};
        request.type = uint64_t(services::MemoryOperation::LockRange);
        request.length = 5;
        request.data[0] = task;
        request.data[1] = address;
        request.data[2] = size;
        request.data[3] =
            (lock ? 1 : 0) | (allowAbsent ? 2 : 0) | (checkOnly ? 4 : 0);
        const int status = channel_.result(request, reply);
        return status ? 5 : int(reply.data[1]);
    }

    /* 读取任务的锁页用量和限制。 */
    int KernelMemory::lockState(uint64_t task, uint64_t& bytes, bool& future,
                                uint64_t& limit) {
        os::IpcMessage request{}, reply{};
        request.type = uint64_t(services::MemoryOperation::LockState);
        request.length = 5;
        request.data[0] = task;
        const int status = channel_.result(request, reply);
        if (status) {
            return 5;
        }

        bytes = reply.data[1];
        future = reply.data[2];
        limit = reply.data[3];
        return 0;
    }

    /* 登记用户态补页服务的端点。 */
    int KernelMemory::bindPager(uint64_t endpoint) {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::MemoryOperation::BindPager);
        request.length = 5;
        request.data[0] = endpoint;
        return channel_.result(request, reply);
    }

    /* 检查任务中的地址范围是否可用于映射。 */
    bool KernelMemory::available(uint64_t task, uint64_t address, uint64_t size,
                                 int* error) {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::MemoryOperation::RangeAvailable);
        request.length = 5;
        request.data[0] = task;
        request.data[1] = address;
        request.data[2] = size;
        const int status = channel_.result(request, reply);
        if (error) {
            *error = status;
        }

        return status == 0 && reply.data[1] == 1;
    }

    /* 查询单页的内核映射状态。 */
    uint64_t KernelMemory::pageState(uint64_t task, uint64_t address) {
        os::IpcMessage request{}, reply{};
        request.type =
            static_cast<uint64_t>(services::MemoryOperation::PageState);
        request.length = 5;
        request.data[0] = task;
        request.data[1] = address;
        return channel_.result(request, reply) == 0 ? reply.data[1] : 0;
    }

    /* 把共享页对象映射到指定任务。 */
    int KernelMemory::mapShared(uint64_t handle, uint64_t task,
                                uint64_t address, uint64_t size,
                                uint64_t offset, uint64_t protection) {
        const int64_t result = os::detail::MemoryControl().mapSharedMemory(
            handle, address, size, offset, protection, task);
        return result == static_cast<int64_t>(address) ? 0
               : result < 0                            ? result
                                                       : -1;
    }

    /* 查询任务当前的映像代号。 */
    uint64_t MemoryState::generation(uint64_t task) const {
        for (const Domain& domain : domains_) {
            if (domain.task == task) {
                return domain.generation;
            }
        }

        return 0;
    }

}  // namespace mm
