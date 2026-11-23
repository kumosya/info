/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/memory/shared/shared.cc
 * 管理共享内存名称、对象、句柄及任务映射。
 */

#include "kernel/cpu.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/task.h"
#include "service/common/identity.h"
#include "service/common/protocol.h"

namespace mm {

    static constinit SharedMemory sharedRegistry;

    // 对象、打开包装、映射和页面各有容量上限；匿名对象没有名称，也可以通过句柄传递和
    // fork 共享。
    constexpr uint64_t pageSize           = 4096;
    constexpr uint32_t objectCapacity     = 16;
    constexpr uint32_t handleCapacity     = 64;
    constexpr uint32_t mappingCapacity    = 64;
    constexpr uint32_t pageCapacity       = 64;
    constexpr uint32_t nameCapacity       = 64;
    constexpr uint64_t minimumUserAddress = 0x10000;
    constexpr uint64_t maximumUserAddress = 0x0000800000000000ULL;
    constexpr int openAccessMode          = 3;
    constexpr int openReadOnly            = 0;
    constexpr int openReadWrite           = 2;
    constexpr int openCreate              = 0x40;
    constexpr int openExclusive           = 0x80;

    /*
     * 逐字符比较共享内存名称，只有内容和终止位置都相同才算匹配。
     */
    static bool namesEqual(const char* left, const char* right) {
        uint32_t index = 0;

        while (left[index] != '\0' && left[index] == right[index]) {
            ++index;
        }

        return left[index] == right[index];
    }

    /*
     * 把共享内存名字复制进固定长度缓冲区，并保留结尾的空字符。
     */
    static void copyName(char* destination, const char* source) {
        uint32_t index = 0;

        while (index + 1 < nameCapacity && source[index] != '\0') {
            destination[index] = source[index];
            ++index;
        }

        destination[index] = '\0';
    }

    /*
     * 返回全局共享内存管理器，统一维护名字、对象和映射记录。
     */
    SharedMemory& sharedMemory() { return sharedRegistry; }

    /*
     * 检查对象是否已失去所有句柄和映射引用。
     * 满足释放条件时归还页面并清空对象槽位。
     */
    void SharedMemory::releaseObject(SharedMemoryObject& object) {
        // 名称、打开引用和映射任意一个仍在使用，就不能释放物理页；关闭最后一个句柄不一定意味着对象结束。
        if (object.linked || object.references != 0 || object.mappings != 0) {
            return;
        }

        for (uint32_t index = 0; index < object.pageCount; ++index) {
            if (object.pages[index] != 0) {
                // 专用池页面只清占用位，普通页面才交回通用分配器，两种来源不能混用释放路径。
                if (object.typedAllocation) {
                    typedUsed_[(object.pages[index] - typedBase_) / pageSize] =
                        false;
                } else {
                    freePhysicalPage(object.pages[index]);
                }
            }

            object.pages[index] = 0;
        }

        // 页面全部归还后再清空对象槽，使下一次创建不会继承旧名称和页数。
        object.typedPool = object.typedAllocation = false;
        object.typedPolicy                        = 0;
        object.used                               = false;
        object.name[0]                            = '\0';
        object.pageCount                          = 0;
        object.size                               = 0;
    }

    /*
     * 通过任务句柄解析共享内存对象，检查类型和所需权限。
     * 返回值用于受控的后续操作，不额外增加句柄。
     */
    SharedMemory::SharedMemoryHandle* SharedMemory::resolve(task::Task& task,
                                                            uint64_t handle,
                                                            uint32_t rights) {
        return static_cast<SharedMemoryHandle*>(
            task.handles().get(handle, task::HandleType::SharedMemory, rights));
    }

    /*
     * 按名字打开或创建共享内存对象。
     * 处理创建和排他标志，再为调用任务安装句柄；安装失败时撤销本次取得的资源。
     */
    uint64_t SharedMemory::open(::task::Task& task, const char* name,
                                int flags) {
        const int access     = flags & openAccessMode;
        const bool anonymous = name == nullptr;

        if ((!anonymous && (name[0] != '/' || name[1] == '\0')) ||
            (anonymous && !(flags & openCreate)) ||
            (access != openReadOnly && access != openReadWrite)) {
            return 0;
        }

        ::task::PreemptionSpinLockGuard guard(registryLock);
        SharedMemoryObject* object = nullptr;

        // 命名对象先按名称查找，匿名对象每次都创建独立实例，不参与名称查找。
        for (SharedMemoryObject& candidate : objects) {
            if (!anonymous && candidate.used && candidate.linked &&
                namesEqual(candidate.name, name)) {
                object = &candidate;
                break;
            }
        }

        if (object != nullptr && (flags & openCreate) != 0 &&
            (flags & openExclusive) != 0) {
            return 0;
        }

        // 没找到时只有带创建标志才能继续；拿到空槽后先建立零大小对象，实际页面由调整大小操作分配。
        if (object == nullptr) {
            if ((flags & openCreate) == 0) {
                return 0;
            }

            for (SharedMemoryObject& candidate : objects) {
                if (!candidate.isUsed()) {
                    object             = &candidate;
                    object->used       = true;
                    object->linked     = !anonymous;
                    object->references = 0;
                    object->mappings   = 0;
                    object->pageCount  = 0;
                    object->size       = 0;

                    if (anonymous) {
                        object->name[0] = 0;
                    } else {
                        copyName(object->name, name);
                    }

                    break;
                }
            }
        }

        if (object == nullptr) {
            return 0;
        }

        // 对象记录可以被多次打开共享，每次打开仍需要独立的句柄包装，供关闭时分别减少引用。
        SharedMemoryHandle* sharedHandle = nullptr;

        for (SharedMemoryHandle& candidate : handles) {
            if (!candidate.isUsed()) {
                sharedHandle     = &candidate;
                candidate.used   = true;
                candidate.object = object;
                break;
            }
        }

        if (sharedHandle == nullptr) {
            releaseObject(*object);

            return 0;
        }

        // 包装准备好后增加打开引用，再根据只读或读写模式限制交给任务的访问权。
        ++object->references;
        uint32_t rights = ::task::handleRightRead;

        if (access == openReadWrite) {
            rights |= ::task::handleRightWrite | ::task::handleRightManage;
        }

        const uint64_t result = task.handles().add(
            sharedHandle, ::task::HandleType::SharedMemory, rights);
        // 安装句柄失败时撤销本次包装与引用；仍有名称或映射的对象不能因此被释放。
        if (result == 0) {
            sharedHandle->used   = false;
            sharedHandle->object = nullptr;
            --object->references;
            releaseObject(*object);
        }

        return result;
    }

    /*
     * 创建带分配策略的类型化内存句柄，记录后续申请所需的策略和打开方式。
     */
    int64_t SharedMemory::openTyped(task::Task& task, int flags,
                                    unsigned policy) {
        if ((flags & ~(3 | 0x80000)) ||
            ((flags & 3) != 0 && (flags & 3) != 2)) {
            return -1;
        }

        if (policy != 1 && policy != 2) {
            return -7;
        }

        const uint64_t handle = open(task, nullptr, 0x42);

        if (!handle) {
            return -5;
        }

        bool ready = false;
        {
            task::PreemptionSpinLockGuard guard(registryLock);

            // 专用页池按需首次申请，以后类型化分配共享这片池，再用位图划分归属。
            if (!typedBase_) {
                typedBase_ = allocatePhysicalBlock(8);
            }

            if (typedBase_) {
                SharedMemory::SharedMemoryHandle* reference =
                    resolve(task, handle, 0);
                reference->object->typedPool   = true;
                reference->object->typedPolicy = policy;
                ready                          = true;
            }
        }

        // 准备专用池失败时关闭刚创建的匿名句柄，不把空壳对象留给调用者。
        if (!ready) {
            task.handles().close(handle);

            return -5;
        }

        // 请求只读访问时，用权限更窄的新句柄替换内部准备用的可写句柄。
        if ((flags & 3) == 0) {
            const uint64_t readonly = task.handles().duplicate(handle, 1, 0);
            task.handles().close(handle);

            return readonly ? static_cast<int64_t>(readonly) : -5;
        }

        return handle;
    }

    /*
     * 检查类型化内存句柄及权限，返回当前可用空间信息。
     */
    int SharedMemory::typedInfo(task::Task& task, uint64_t handle,
                                uint64_t& available) {
        task::HandleTable::Pin pin(task.handles(), handle,
                                   task::HandleType::SharedMemory, 1);
        mm::SharedMemory::SharedMemoryHandle* reference =
            pin.as<SharedMemoryHandle>();

        if (!reference) {
            return -2;
        }

        task::PreemptionSpinLockGuard guard(registryLock);

        if (!reference->object->typedPool) {
            return -1;
        }

        unsigned free = 0, run = 0, longest = 0;

        for (bool used : typedUsed_) {
            if (used) {
                run = 0;
            } else {
                ++free;
                ++run;

                if (run > longest) {
                    longest = run;
                }
            }
        }

        available =
            (reference->object->typedPolicy == 2 ? longest : free) * pageSize;

        return 0;
    }

    /*
     * 从类型化内存句柄申请指定大小的页块，检查策略、容量和内存资源，失败时保留原对象状态。
     */
    int64_t SharedMemory::allocateTyped(task::Task& task, uint64_t handle,
                                        uint64_t size) {
        task::HandleTable::Pin pin(task.handles(), handle,
                                   task::HandleType::SharedMemory, 1);
        mm::SharedMemory::SharedMemoryHandle* source =
            pin.as<SharedMemoryHandle>();

        if (!source) {
            return -2;
        }

        if (!size || (size & 4095) || size > pageCapacity * pageSize) {
            return -1;
        }

        // 为结果单独创建匿名对象；源句柄描述池和策略，不应被本次分配结果覆盖。
        const uint64_t allocation = open(task, nullptr, 0x42);

        if (!allocation) {
            return -5;
        }

        bool success = false;
        {
            task::PreemptionSpinLockGuard guard(registryLock);

            if (source->object->typedPool) {
                const unsigned count = size / pageSize;
                unsigned selected[pageCapacity]{}, found = 0;

                // 策略要求连续空间时，遇到已占页就从头累计；否则允许收集分散空页。
                for (unsigned i = 0; i < typedPageCount && found < count; ++i) {
                    if (typedUsed_[i]) {
                        if (source->object->typedPolicy == 2) {
                            found = 0;
                        }
                    } else {
                        selected[found++] = i;
                    }
                }

                // 找齐全部页面才一次性标记占用并清零，搜索失败不会消耗半份池空间。
                if (found == count) {
                    SharedMemoryObject* object =
                        resolve(task, allocation, 0)->object;
                    object->typedAllocation = true;
                    object->size            = size;
                    object->pageCount       = count;

                    for (unsigned i = 0; i < count; ++i) {
                        typedUsed_[selected[i]] = true;
                        object->pages[i] = typedBase_ + selected[i] * pageSize;
                        clearPhysicalPages(object->pages[i], pageSize);
                    }

                    success = true;
                }
            }
        }

        // 失败时关闭尚未公布的结果句柄，让临时匿名对象沿正常关闭路径回收。
        if (!success) {
            task.handles().close(allocation);

            return source->object->typedPool ? -5 : -1;
        }

        // 原句柄没有写权限时，结果也必须降为只读，不能借分配操作提升权限。
        if (!resolve(task, handle, task::handleRightWrite)) {
            const uint64_t readonly =
                task.handles().duplicate(allocation, 1, 0);
            task.handles().close(allocation);

            return readonly ? static_cast<int64_t>(readonly) : -5;
        }

        return allocation;
    }

    /*
     * 将共享内存句柄复制到另一任务，保留对象引用关系，并支持继承时指定目标句柄编号。
     */
    uint64_t SharedMemory::duplicateHandle(::task::Task& source,
                                           ::task::Task& destination,
                                           uint64_t handle,
                                           uint64_t destinationHandle) {
        return source.handles().copyTo(destination.handles(), handle,
                                       destinationHandle);
    }

    /*
     * 按名字撤销共享内存名称。
     * 已有句柄和映射继续持有对象，最后一位使用者退出后才真正释放页面。
     */
    bool SharedMemory::unlink(const char* name) {
        if (name == nullptr) {
            return false;
        }

        ::task::PreemptionSpinLockGuard guard(registryLock);

        for (SharedMemoryObject& object : objects) {
            if (!object.used || !object.linked ||
                !namesEqual(object.name, name)) {
                continue;
            }

            object.linked  = false;
            object.name[0] = '\0';
            releaseObject(object);

            return true;
        }

        return false;
    }

    /*
     * 改变共享内存对象大小。
     * 先验证权限和大小，再准备新增页面；遇到分配失败时归还本次新增资源。
     */
    bool SharedMemory::resize(::task::Task& task, uint64_t handle,
                              uint64_t size) {
        ::task::HandleTable::Pin pin(task.handles(), handle,
                                     ::task::HandleType::SharedMemory, 0);
        SharedMemoryHandle* sharedHandle =
            resolve(task, handle, ::task::handleRightManage);

        if (sharedHandle == nullptr || size > pageCapacity * pageSize) {
            return false;
        }

        SharedMemoryObject* object = sharedHandle->object;
        ::task::PreemptionSpinLockGuard registryGuard(registryLock);

        if (object->typedPool || object->typedAllocation) {
            return false;
        }

        // 有映射时不允许缩小，防止其他线程继续通过旧映射访问已释放页面。
        if (object->mappings != 0 && size < object->size) {
            return false;
        }

        const uint32_t pages =
            static_cast<uint32_t>((size + pageSize - 1) / pageSize);

        // 保存原页数作为回滚边界：扩容失败只能释放本次新增页，原内容应完整保留。
        const uint32_t originalPageCount = object->pageCount;

        // 先补齐新页并逐页清零；此时还不更新公开大小，避免调用方看到未准备好的数据。
        while (object->pageCount < pages) {
            const uint64_t physical = mm::allocatePhysicalPage();

            if (physical == 0) {
                while (object->pageCount > originalPageCount) {
                    mm::freePhysicalPage(object->pages[--object->pageCount]);
                    object->pages[object->pageCount] = 0;
                }

                return false;
            }

            uint8_t* bytes =
                reinterpret_cast<uint8_t*>(0xffff800000000000ULL + physical);

            for (uint64_t index = 0; index < pageSize; ++index) {
                bytes[index] = 0;
            }

            object->pages[object->pageCount++] = physical;
        }

        // 只有允许缩小时才走这里，按页回收超过新大小的尾部页面。
        while (object->pageCount > pages) {
            mm::freePhysicalPage(object->pages[--object->pageCount]);
            object->pages[object->pageCount] = 0;
        }

        // 物理页调整全部成功后，最后提交对外可见的字节大小。
        object->size = size;

        return true;
    }

    /*
     * 查询句柄对应共享内存对象的大小，失败时不把无效对象信息交给调用者。
     */
    bool SharedMemory::querySize(::task::Task& task, uint64_t handle,
                                 uint64_t& size) {
        ::task::HandleTable::Pin pin(task.handles(), handle,
                                     ::task::HandleType::SharedMemory, 0);
        mm::SharedMemory::SharedMemoryHandle* reference =
            pin.as<SharedMemoryHandle>();

        if (!reference) {
            return false;
        }

        ::task::PreemptionSpinLockGuard guard(registryLock);
        size = reference->object->size;

        return true;
    }

    /*
     * 通过句柄撤销对象名称，后续按名字打开将不能再找到它；现有使用者的引用仍然有效。
     */
    bool SharedMemory::unlink(::task::Task& task, uint64_t handle) {
        ::task::HandleTable::Pin pin(task.handles(), handle,
                                     ::task::HandleType::SharedMemory, 0);
        mm::SharedMemory::SharedMemoryHandle* reference =
            pin.as<SharedMemoryHandle>();

        if (!reference) {
            return false;
        }

        ::task::PreemptionSpinLockGuard guard(registryLock);
        SharedMemoryObject& object = *reference->object;

        if (!object.linked) {
            return false;
        }

        object.linked  = false;
        object.name[0] = 0;
        releaseObject(object);

        return true;
    }

    /*
     * 将共享对象的指定范围映射进任务。
     * 先检查偏移、权限和地址范围，再建立页表与映射记录，失败时撤销已做的部分工作。
     */
    uint64_t SharedMemory::map(::task::Task& task, uint64_t handle,
                               uint64_t address, uint64_t size, uint64_t offset,
                               uint64_t protection) {
        ::task::HandleTable::Pin pin(task.handles(), handle,
                                     ::task::HandleType::SharedMemory, 0);
        const uint64_t end = address + size;

        // 当前申请需要写权限时，原句柄也必须允许写；不能只修改页表标志来绕过句柄限制。
        const uint32_t requiredRights =
            ::task::handleRightRead |
            (((protection & 1) != 0) ? ::task::handleRightWrite : 0);
        SharedMemoryHandle* sharedHandle =
            resolve(task, handle, requiredRights);

        if (sharedHandle == nullptr || address == 0 || size == 0 ||
            (address & (pageSize - 1)) != 0 || (size & (pageSize - 1)) != 0 ||
            (offset & (pageSize - 1)) != 0 || (protection & ~3ULL) != 0 ||
            (protection & 3) == 3 || address < minimumUserAddress ||
            end <= address || end > maximumUserAddress) {
            return 0;
        }

        SharedMemoryObject* object = sharedHandle->object;
        ::task::PreemptionSpinLockGuard addressGuard(
            task.addressSpace().lock());
        ::task::PreemptionSpinLockGuard registryGuard(registryLock);

        // 对象末页允许按整页映射，但起始偏移仍必须位于实际对象内，并检查偏移加长度溢出。
        const uint64_t mappedObjectSize =
            (object->size + pageSize - 1) & ~(pageSize - 1);

        if (object->typedPool || object->size == 0 || offset >= object->size ||
            offset + size < offset || offset + size > mappedObjectSize) {
            return 0;
        }

        // 先取得空映射记录，后面才改页表，避免实际映射成功却没有位置保存引用关系。
        SharedMemoryMapping* mapping = nullptr;

        for (SharedMemoryMapping& candidate : mappings) {
            if (!candidate.isUsed()) {
                mapping = &candidate;
                break;
            }
        }

        if (mapping == nullptr) {
            return 0;
        }

        uint64_t flags = pageUser | pageExternal;
        ::task::AddressSpace::Residency residency =
            task.addressSpace().residency();
        const bool pager =
            task::getCurrent() && task::getCurrent() != &task &&
            task::getCurrent()->serviceRole() == services::ServiceRole::Memory;

        if (!pager && task.addressSpace().lockFuture() &&
            (!residency.set(address, size, true) ||
             residency.bytes > task.memlockLimit())) {
            return 0;
        }

        if ((protection & 1) != 0) {
            flags |= pageWritable;
        }

        if ((protection & 2) == 0) {
            flags |= pageNoExecute;
        }

        // 按对象页数组逐页映射；完成长度既是进度，也是中途失败时撤销前缀的依据。
        uint64_t mapped = 0;

        while (mapped < size) {
            const uint32_t page =
                static_cast<uint32_t>((offset + mapped) / pageSize);

            if (!mm::mapPageInAddressSpace(
                    task.addressSpace().rootPhysicalAddress(), address + mapped,
                    object->pages[page],
                    flags | (residency.contains(address + mapped) ? pageLocked
                                                                  : 0))) {
                // 本次只撤销自己已经安装的页面，页面仍归共享对象所有，不能在这里直接释放物理页。
                while (mapped != 0) {
                    mapped -= pageSize;
                    mm::unmapPageInAddressSpace(
                        task.addressSpace().rootPhysicalAddress(),
                        address + mapped);
                }

                return 0;
            }

            mapped += pageSize;
        }

        task.addressSpace().residency() = residency;

        // 全部页面准备成功后才发布记录并增加映射计数，关闭打开句柄不会影响这份映射寿命。
        mapping->used = true;
        mapping->pagerManaged =
            task::getCurrent() != &task &&
            task::getCurrent()->serviceRole() == services::ServiceRole::Memory;
        mapping->task    = &task;
        mapping->object  = object;
        mapping->address = address;
        mapping->size    = size;
        // 单独保存原句柄允许的写权限，不能仅凭当前页表标志判断以后是否允许增加写权限。
        mapping->writable =
            task.handles().get(handle, ::task::HandleType::SharedMemory,
                               ::task::handleRightWrite) != nullptr;
        ++object->mappings;

        return address;
    }

    /*
     * 为可能切开旧映射的解除操作预留记录空间。
     * 先预留可避免页表已改、却无处记录剩余映射的情况。
     */
    bool SharedMemory::prepareUnmap(::task::Task& task, uint64_t address,
                                    uint64_t size, void*& reservation) {
        reservation = nullptr;
        ::task::PreemptionSpinLockGuard guard(registryLock);

        // 只有删除范围严格落在旧映射中间，才会把一条记录切成两条；删整段或边缘不需要新槽。
        for (const SharedMemoryMapping& mapping : mappings) {
            if (!mapping.isUsed() || mapping.task != &task ||
                address <= mapping.address ||
                address + size >= mapping.address + mapping.size) {
                continue;
            }

            for (SharedMemoryMapping& spare : mappings) {
                if (!spare.isUsed()) {
                    spare = {};

                    // 立即占住空槽作为预留，防止真正提交前被其他映射请求拿走。
                    spare.used  = true;
                    reservation = &spare;

                    return true;
                }
            }

            return false;
        }

        return true;
    }

    /*
     * 取消尚未提交的解除映射预留，归还临时记录空间。
     */
    void SharedMemory::cancelUnmap(void* reservation) {
        if (!reservation) {
            return;
        }

        ::task::PreemptionSpinLockGuard guard(registryLock);
        *static_cast<SharedMemoryMapping*>(reservation) = {};
    }

    /*
     * 更新共享映射记录，按需要删除、缩短或拆成两段，并释放已经不再使用的对象引用。
     */
    bool SharedMemory::releaseMapping(::task::Task& task, uint64_t address,
                                      uint64_t size, void* reservation) {
        ::task::PreemptionSpinLockGuard guard(registryLock);
        SharedMemoryMapping* spare =
            static_cast<SharedMemoryMapping*>(reservation);
        const uint64_t end = address + size;
        bool changed       = false;

        for (SharedMemoryMapping& mapping : mappings) {
            if (!mapping.isUsed() || mapping.task != &task) {
                continue;
            }

            const uint64_t oldEnd = mapping.address + mapping.size;

            if (end <= mapping.address || address >= oldEnd) {
                continue;
            }

            changed = true;

            // 整段删除时撤掉记录并减少映射引用，最后一个使用者消失后再检查能否回收共享对象。
            if (address <= mapping.address && end >= oldEnd) {
                SharedMemoryObject* object = mapping.object;
                mapping                    = {};
                --object->mappings;
                releaseObject(*object);
            } else if (address > mapping.address && end < oldEnd) {
                if (!spare) {
                    return false;
                }

                // 中间删除会留下左右两段；右段使用提前预留的槽，两段各自持有一份映射计数。
                *spare         = mapping;
                spare->address = end;
                spare->size    = oldEnd - end;
                ++mapping.object->mappings;
                mapping.size = address - mapping.address;
                spare        = nullptr;
            } else if (address <= mapping.address) {
                // 删除前缀时移动起点，删除后缀则只缩短长度，保留下来的地址仍指向原共享页。
                mapping.address = end;
                mapping.size    = oldEnd - end;
            } else {
                mapping.size = address - mapping.address;
            }
        }

        // 实际提交没用到预留槽时及时归还，避免一次普通解除映射永久占据记录容量。
        if (spare) {
            *spare = {};
        }

        return changed;
    }

    /*
     * 检查请求的新权限是否超过共享映射创建时允许的上限。
     */
    bool SharedMemory::protectionAllowed(::task::Task& task, uint64_t address,
                                         uint64_t size, uint64_t protection) {
        ::task::PreemptionSpinLockGuard guard(registryLock);
        const uint64_t end = address + size;

        if (end <= address) {
            return false;
        }

        for (const SharedMemoryMapping& mapping : mappings) {
            if (!mapping.isUsed() || mapping.task != &task) {
                continue;
            }

            const uint64_t mappingEnd = mapping.address + mapping.size;

            if (end <= mapping.address || address >= mappingEnd) {
                continue;
            }

            if ((protection & 1) != 0 && !mapping.writable) {
                return false;
            }
        }

        return true;
    }

    /*
     * 删除一个任务的全部共享映射记录，归还对应引用；用于退出和替换程序映像。
     */
    void SharedMemory::releaseTaskMappings(::task::Task& task) {
        ::task::PreemptionSpinLockGuard guard(registryLock);

        for (SharedMemoryMapping& mapping : mappings) {
            if (!mapping.isUsed() || mapping.task != &task) {
                continue;
            }

            SharedMemoryObject* object = mapping.object;
            mapping                    = {};

            if (object->mappings != 0) {
                --object->mappings;
            }

            releaseObject(*object);
        }
    }

    /*
     * 复制源任务的共享映射记录到目标任务，并增加相应对象引用。
     * 失败时撤销目标的已复制记录。
     */
    bool SharedMemory::cloneTaskMappings(::task::Task& source,
                                         ::task::Task& destination) {
        ::task::PreemptionSpinLockGuard guard(registryLock);
        uint32_t required  = 0;
        uint32_t available = 0;

        // 先统计源记录数和空槽数，容量不足就整次拒绝，避免只复制一部分后才发现无处登记。
        for (const SharedMemoryMapping& mapping : mappings) {
            if (mapping.used && mapping.task == &source) {
                ++required;
            }

            if (!mapping.isUsed()) {
                ++available;
            }
        }

        if (required > available) {
            return false;
        }

        for (const SharedMemoryMapping& sourceMapping : mappings) {
            if (!sourceMapping.isUsed() || sourceMapping.task != &source) {
                continue;
            }

            for (SharedMemoryMapping& destinationMapping : mappings) {
                if (destinationMapping.used) {
                    continue;
                }

                // 新记录保留相同对象与范围，只改所属任务；额外增加映射计数，让父子都能独立退出。
                destinationMapping      = sourceMapping;
                destinationMapping.task = &destination;
                ++destinationMapping.object->mappings;
                break;
            }
        }

        return true;
    }

    /*
     * 关闭共享内存句柄背后的包装对象，归还它持有的共享对象引用，并检查能否释放对象。
     */
    void SharedMemory::closeHandleObject(void* object) {
        SharedMemoryHandle* sharedHandle =
            static_cast<SharedMemoryHandle*>(object);

        if (sharedHandle == nullptr || !sharedHandle->used) {
            return;
        }

        ::task::PreemptionSpinLockGuard guard(registryLock);
        SharedMemoryObject* shared = sharedHandle->object;
        sharedHandle->used         = false;
        sharedHandle->object       = nullptr;

        if (shared != nullptr && shared->references != 0) {
            --shared->references;
        }

        if (shared != nullptr) {
            releaseObject(*shared);
        }
    }

    /*
     * 查找游标之后的下一段有效共享映射，供按地址枚举任务内存使用。
     */
    bool SharedMemory::nextManagedMapping(::task::Task& task, uint64_t cursor,
                                          uint64_t& address, uint64_t& size) {
        ::task::PreemptionSpinLockGuard guard(registryLock);
        const SharedMemoryMapping* selected = nullptr;

        for (const SharedMemoryMapping& mapping : mappings) {
            if (mapping.used && mapping.task == &task && mapping.pagerManaged &&
                mapping.address >= cursor &&
                (!selected || mapping.address < selected->address)) {
                selected = &mapping;
            }
        }

        if (!selected) {
            return false;
        }

        address = selected->address;
        size    = selected->size;

        return true;
    }

}  // namespace mm
