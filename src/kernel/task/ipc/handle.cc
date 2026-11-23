/*
 * Copyright (c) 2026, Kumosya
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * src/kernel/task/ipc/handle.cc
 * 维护带代号的句柄、共享对象引用以及消息中的句柄转移。
 */

#include <new>

#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/mm.h"
#include "kernel/task.h"

namespace task {

    /*
     * 清零新句柄表存储，保证未使用槽位不会带着旧内存中的指针和权限。
     */
    static void clearMemory(void* address, uint64_t size) {
        uint8_t* bytes = static_cast<uint8_t*>(address);

        for (uint64_t index = 0; index < size; ++index) {
            bytes[index] = 0;
        }
    }

    /*
     * 初始化尚未发布的句柄表。
     * 记录所属任务及是否为恢复服务代管引用，分配失败时容量保持为零。
     */
    bool HandleTable::init(uint32_t capacity, Task* owner, bool escrow) {
        owner_    = owner;
        escrow_   = escrow;
        disposed_ = false;

        // 分配成功前先公开零容量，失败时不会留下“有容量却没有表存储”的半初始化状态。
        capacity_ = 0;
        lock_.init();
        entries_ = static_cast<HandleEntry*>(
            mm::allocateHeap(sizeof(HandleEntry) * capacity));

        if (entries_ == nullptr) {
            return false;
        }

        // 新内存中的旧字节可能看起来像有效对象，清零以后才能让槽位检查访问它。
        clearMemory(entries_, sizeof(HandleEntry) * capacity);
        capacity_ = capacity;
        lock_.init();

        return true;
    }

    /*
     * 在表锁内读取当前容量。
     */
    uint32_t HandleTable::capacity() {
        InterruptSpinLockGuard guard(lock_);

        return capacity_;
    }

    /*
     * 在持锁状态下扩容。
     * 先分配并复制新数组，成功后才替换旧表，内存不足时原有句柄仍然有效。
     */
    bool HandleTable::growLocked(uint32_t required) {
        if (disposed_) {
            return false;
        }

        uint32_t capacity = capacity_;

        // 容量按倍数增长，并在乘二前检查上限，避免增长回绕成较小容量。
        while (capacity < required) {
            if (capacity == 0 || capacity > UINT32_MAX / 2) {
                return false;
            }

            capacity *= 2;
        }

        if (capacity == capacity_) {
            return true;
        }

        // 先申请新数组；此时旧 entries_ 仍有效，申请失败无需修复任何现有句柄。
        HandleEntry* entries = static_cast<HandleEntry*>(
            mm::allocateHeap(sizeof(HandleEntry) * capacity));

        if (!entries) {
            return false;
        }

        clearMemory(entries, sizeof(HandleEntry) * capacity);

        for (uint32_t i = 0; i < capacity_; ++i) {
            entries[i] = entries_[i];
        }

        // 所有槽位复制完成后才替换旧表；句柄编码使用索引而不是槽位地址，因此扩容不会改变编号。
        mm::freeHeap(entries_);
        entries_  = entries;
        capacity_ = capacity;

        return true;
    }

    /*
     * 包装内核对象并安装新句柄。
     * 任务和端点对象需要额外保留对象引用；安装失败时只撤销本次增加的资源。
     */
    uint64_t HandleTable::add(void* object, HandleType type, uint32_t rights) {
        if (object == nullptr || type == HandleType::None) {
            return 0;
        }

        Reference* reference =
            static_cast<Reference*>(mm::allocateHeap(sizeof(Reference)));

        if (!reference) {
            return 0;
        }

        // 一份共享包装记录对象与类型；任务和端点还要各自保留对象层引用，两层计数用途不同。
        new (reference) Reference(object, type, 1);

        if (type == HandleType::Task) {
            static_cast<Task*>(object)->retain();
        }

        if (type == HandleType::Endpoint) {
            static_cast<Endpoint*>(object)->retain();
        }

        InterruptSpinLockGuard guard(lock_);

        // 表锁内选槽和安装，其他 CPU 不会同时占用同一个空槽。
        const uint64_t result = installLocked(reference, rights, 0);

        // 安装失败表示新包装尚未交给槽位，归还刚增加的对象引用并释放包装即可。
        if (!result) {
            if (type == HandleType::Task) {
                static_cast<Task*>(object)->release();
            }

            if (type == HandleType::Endpoint) {
                static_cast<Endpoint*>(object)->release();
            }

            mm::freeHeap(reference);
        }

        return result;
    }

    /*
     * 把调用方提供的引用安装到空槽。
     * 句柄低位编码槽号、高位编码代号；槽位复用时增加代号，避免旧句柄误指向新对象。
     */
    uint64_t HandleTable::installLocked(Reference* reference, uint32_t rights,
                                        uint32_t flags) {
        if (disposed_) {
            return 0;
        }

        for (uint32_t index = 0;; ++index) {
            // 扫到表尾仍无空槽就扩容，扩容失败不会消耗调用方传入的引用。
            if (index == capacity_) {
                if (!growLocked(capacity_ + 1)) {
                    return 0;
                }
            }

            HandleEntry& entry = entries_[index];

            if (entry.getObject() != nullptr) {
                continue;
            }

            // 每次复用槽位都更换代号，旧句柄即使索引相同也不会命中新对象。
            entry.setGeneration(entry.getGeneration() + 1);

            if (entry.getGeneration() == 0) {
                entry.setGeneration(entry.getGeneration() + 1);
            }

            // 槽位同时保存共享引用和检查用的元数据，填好后再编码成对外句柄。
            entry.reference = reference;
            entry.flags     = flags;
            entry.setObject(reference->object);
            entry.setRights(rights);
            entry.setType(static_cast<uint16_t>(reference->type));

            return (static_cast<uint64_t>(entry.getGeneration()) << 32) |
                   (index + 1);
        }

        return 0;
    }

    /*
     * 按指定槽位和代号安装对象，用于保持继承句柄的编号。
     * 已占用的槽位直接拒绝，避免覆盖其他对象。
     */
    bool HandleTable::addAt(uint64_t handle, void* object, HandleType type,
                            uint32_t rights) {
        // 继承场景需要保持编号，先把指定句柄拆成槽号和代号分别核对。
        const uint32_t encodedIndex = static_cast<uint32_t>(handle);
        const uint16_t generation   = static_cast<uint16_t>(handle >> 32);

        if (object == nullptr || type == HandleType::None ||
            encodedIndex == 0 || generation == 0) {
            return false;
        }

        InterruptSpinLockGuard guard(lock_);

        if (disposed_ || !growLocked(encodedIndex)) {
            return false;
        }

        // 即使目标编号合法，也不能覆盖正在使用的槽，否则会丢失原对象引用。
        HandleEntry& entry = entries_[encodedIndex - 1];

        if (entry.getObject() != nullptr) {
            return false;
        }

        Reference* reference =
            static_cast<Reference*>(mm::allocateHeap(sizeof(Reference)));

        if (!reference) {
            return false;
        }

        new (reference) Reference(object, type, 1);

        if (type == HandleType::Task) {
            static_cast<Task*>(object)->retain();
        }

        if (type == HandleType::Endpoint) {
            static_cast<Endpoint*>(object)->retain();
        }

        // 所有分配与占用检查通过后才填入目标槽，原编号和访问权限一起保留。
        entry.reference = reference;
        entry.flags     = 0;
        entry.setObject(object);
        entry.setRights(rights);
        entry.setType(static_cast<uint16_t>(type));
        entry.setGeneration(generation);

        return true;
    }

    /*
     * 校验槽号、代号、对象类型和权限后返回借用指针。
     * 返回后表锁已释放，需要长期使用对象时应使用 Pin。
     */
    void* HandleTable::get(uint64_t handle, HandleType type,
                           uint32_t requiredRights) {
        const uint32_t encodedIndex = static_cast<uint32_t>(handle);
        const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
        InterruptSpinLockGuard guard(lock_);

        // 先检查索引再访问数组，零句柄和过期代号不能参与对象查找。
        if (encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
            return nullptr;
        }

        // 类型与权限也属于有效性检查；知道一个编号不等于可以把它当任意类型对象使用。
        const HandleEntry& entry = entries_[encodedIndex - 1];

        if (entry.getObject() == nullptr ||
            entry.getGeneration() != generation ||
            entry.getType() != static_cast<uint16_t>(type) ||
            (entry.getRights() & requiredRights) != requiredRights) {
            return nullptr;
        }

        return entry.getObject();
    }

    /*
     * 读取一个句柄的对象、类型和权限。
     * 只提供借用信息，不为返回指针增加引用。
     */
    bool HandleTable::inspect(uint64_t handle, void** object, HandleType* type,
                              uint32_t* rights) {
        const uint32_t encodedIndex = static_cast<uint32_t>(handle);
        const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
        InterruptSpinLockGuard guard(lock_);

        if (object == nullptr || type == nullptr || rights == nullptr ||
            encodedIndex == 0 || encodedIndex > capacity_ || generation == 0) {
            return false;
        }

        const HandleEntry& entry = entries_[encodedIndex - 1];

        if (entry.getObject() == nullptr ||
            entry.getGeneration() != generation) {
            return false;
        }

        *object = entry.getObject();
        *type   = static_cast<HandleType>(entry.getType());
        *rights = entry.getRights();
        return true;
    }

    /*
     * 按表索引读取有效槽位，并还原完整句柄值，供句柄继承和枚举使用。
     */
    bool HandleTable::inspectAt(uint32_t index, uint64_t* handle, void** object,
                                HandleType* type, uint32_t* rights) {
        InterruptSpinLockGuard guard(lock_);

        if (handle == nullptr || object == nullptr || type == nullptr ||
            rights == nullptr || index >= capacity_) {
            return false;
        }

        const HandleEntry& entry = entries_[index];

        if (entry.getObject() == nullptr || entry.getGeneration() == 0) {
            return false;
        }

        *handle =
            (static_cast<uint64_t>(entry.getGeneration()) << 32) | (index + 1);
        *object = entry.getObject();
        *type   = static_cast<HandleType>(entry.getType());
        *rights = entry.getRights();
        return true;
    }

    /*
     * 先在锁内清空槽位，再在锁外归还对象引用。
     * 先摘除可防止重复关闭，锁外释放避免对象清理反过来取得本表锁。
     */
    bool HandleTable::close(uint64_t handle) {
        const uint32_t encodedIndex = static_cast<uint32_t>(handle);
        const uint16_t generation   = static_cast<uint16_t>(handle >> 32);
        Reference* reference;
        {
            InterruptSpinLockGuard guard(lock_);

            if (encodedIndex == 0 || encodedIndex > capacity_ ||
                generation == 0) {
                return false;
            }

            HandleEntry& entry = entries_[encodedIndex - 1];

            if (entry.getObject() == nullptr ||
                entry.getGeneration() != generation) {
                return false;
            }

            // 先把共享引用保存到局部，再清空槽位，让后续同编号查找立即失败。
            reference = entry.reference;

            if (escrow_) {
                __atomic_sub_fetch(&reference->escrow, 1U, __ATOMIC_ACQ_REL);
            }

            entry.reference = nullptr;
            entry.flags     = 0;
            entry.setObject(nullptr);
            entry.setRights(0);
            entry.setType(static_cast<uint16_t>(HandleType::None));
        }

        // 关闭最后一个接收权限句柄还会影响端点所有者，放表锁后再通知端点，避免跨对象嵌套锁。
        if (reference->type == HandleType::Endpoint && owner_ &&
            !hasObjectRights(reference->object, HandleType::Endpoint,
                             handleRightRead)) {
            static_cast<Endpoint*>(reference->object)->receiverClosed(owner_);
        }

        // 对象清理可能进一步关闭其他句柄，必须在句柄表临界区外执行。
        (reference)->release();

        return true;
    }

    /*
     * 增加共享句柄引用计数，使多个槽位可以指向同一个对象包装。
     */
    void HandleTable::Reference::retain() {
        task::HandleTable::Reference* reference = this;
        __atomic_add_fetch(&reference->count, 1U, __ATOMIC_RELAXED);
    }

    /*
     * 检查句柄后保留对象引用，并登记线程清理动作。
     * 即使线程被强制终止，也能归还这份临时引用。
     */
    HandleTable::Pin::Pin(HandleTable& table, uint64_t handle, HandleType type,
                          uint32_t rights) {
        InterruptSpinLockGuard guard(table.lock_);

        if (!table.validLocked(handle)) {
            return;
        }

        const HandleEntry& entry =
            table.entries_[static_cast<uint32_t>(handle) - 1];

        // 表锁内完成类型与权限检查，检查和加引用之间不能让另一个线程关闭槽位。
        if (entry.reference->type != type ||
            (entry.getRights() & rights) != rights) {
            return;
        }

        // 保留的是共享包装而不是槽位地址；随后扩容或关闭表都不影响这次临时引用。
        reference_ = entry.reference;
        (reference_)->retain();

        // 同时登记强制退出清理，线程被终止而没有正常析构时也不会漏掉引用。
        cleanup_.attach(reference_, [](void* value) {
            (static_cast<Reference*>(value))->release();
        });
    }

    /*
     * 清理成员负责归还构造时保留的共享引用。
     */
    HandleTable::Pin::~Pin() = default;

    /*
     * 归还共享引用。
     * 只有最后一份引用才执行对象类型对应的关闭或销毁逻辑，并释放引用包装。
     */
    void HandleTable::Reference::release() {
        task::HandleTable::Reference* reference = this;

        // 原子计数归零的调用者才有资格关闭对象，其余使用者只归还自己的一份引用。
        if (__atomic_sub_fetch(&reference->count, 1U, __ATOMIC_ACQ_REL) != 0) {
            return;
        }

        void* object          = reference->object;
        const HandleType type = reference->type;

        // 不同对象有不同结束动作；这里只按已记录类型分发，不能把所有对象简单当堆内存释放。
        if (type == HandleType::SharedMemory) {
            ::mm::sharedMemory().closeHandleObject(object);
        } else if (type == HandleType::PageObject) {
            static_cast<mm::PageObject*>(object)->release();
        } else if (type == HandleType::Task) {
            static_cast<Task*>(object)->release();
        } else if (type == HandleType::Endpoint) {
            static_cast<Endpoint*>(object)->release();
        } else if (type == HandleType::InterruptBinding) {
            static_cast<device::InterruptBinding*>(object)->destroy();
        } else if (type == HandleType::DmaBuffer) {
            static_cast<device::DmaBuffer*>(object)->destroy();
        } else if (type == HandleType::MmioRegion) {
            static_cast<device::MmioRegion*>(object)->destroy();
        } else if (type == HandleType::FramebufferMapping) {
            static_cast<device::FrameMapping*>(object)->destroy();
        } else if (type == HandleType::DeviceDomain) {
            static_cast<device::ResourceDomain*>(object)->closeLease();
        }

        // 对象层清理完成以后，最后再释放共享引用包装本身。
        mm::freeHeap(reference);
    }

    /*
     * 在表锁已持有时检查句柄的槽位和代号是否仍然匹配。
     */
    bool HandleTable::validLocked(uint64_t handle) const {
        const uint32_t slot = static_cast<uint32_t>(handle);

        return slot && slot <= capacity_ && (handle >> 48) == 0 &&
               entries_[slot - 1].getObject() &&
               entries_[slot - 1].getGeneration() == (handle >> 32);
    }

    /*
     * 复制出一个新句柄，权限只能从原句柄中削减，不能凭复制操作获得额外权限。
     */
    uint64_t HandleTable::duplicate(uint64_t handle, uint32_t rights,
                                    uint32_t flags) {
        InterruptSpinLockGuard guard(lock_);

        // 在同一把表锁下核对源句柄并准备目标，避免源槽位在中间被关闭或复用。
        if (!validLocked(handle) || (flags & ~os::handleFlagsMask)) {
            return 0;
        }

        HandleEntry& entry = entries_[static_cast<uint32_t>(handle) - 1];

        if ((rights & entry.getRights()) != rights) {
            return 0;
        }

        Reference* reference = entry.reference;
        (reference)->retain();

        // 新槽只共享对象引用，权限仍受旧槽约束；安装失败时撤销本次多加的一份引用。
        const uint64_t result = installLocked(reference, rights, flags);

        if (!result) {
            (reference)->release();
        }

        return result;
    }

    /*
     * 收紧已有句柄的权限。
     * 要求新权限是旧权限的子集，防止调用者提升自己的访问能力。
     */
    bool HandleTable::restrictRights(uint64_t handle, uint32_t rights) {
        InterruptSpinLockGuard guard(lock_);

        if (!validLocked(handle)) {
            return false;
        }

        HandleEntry& entry = entries_[static_cast<uint32_t>(handle) - 1];

        if ((rights & entry.getRights()) != rights) {
            return false;
        }

        entry.setRights(rights);

        return true;
    }

    /*
     * 在两张句柄表之间复制引用，并支持指定目标编号。
     * 锁的取得顺序保持一致，避免两任务同时互相复制时死锁。
     */
    uint64_t HandleTable::copyTo(HandleTable& destination, uint64_t handle,
                                 uint64_t destinationHandle) {
        // 同表复制直接复用 duplicate，避免对同一把自旋锁加锁两次。
        if (&destination == this) {
            void* object;
            HandleType type;
            uint32_t rights;

            return inspect(handle, &object, &type, &rights)
                       ? duplicate(handle, rights)
                       : 0;
        }

        // 跨表复制统一按表地址顺序拿锁；相反方向同时复制也会遵守相同顺序，不会互相等待。
        HandleTable* first  = reinterpret_cast<uintptr_t>(this) <
                                     reinterpret_cast<uintptr_t>(&destination)
                                  ? this
                                  : &destination;
        HandleTable* second = first == this ? &destination : this;
        InterruptSpinLockGuard firstGuard(first->lock_);
        InterruptSpinLockGuard secondGuard(second->lock_);

        if (destination.disposed_ || !validLocked(handle)) {
            return 0;
        }

        HandleEntry& source = entries_[static_cast<uint32_t>(handle) - 1];

        // 指定编号的继承路径要额外检查槽号、代号和目标空闲状态，不能覆盖目标已有资源。
        if (destinationHandle) {
            const uint32_t slot = static_cast<uint32_t>(destinationHandle);
            const uint64_t generation = destinationHandle >> 32;

            if (!slot || !generation || generation > UINT16_MAX ||
                !destination.growLocked(slot) ||
                destination.entries_[slot - 1].getObject()) {
                return 0;
            }

            HandleEntry& entry = destination.entries_[slot - 1];
            entry              = source;
            entry.setGeneration(static_cast<uint16_t>(generation));
            (entry.reference)->retain();

            if (destination.escrow_) {
                __atomic_add_fetch(&entry.reference->escrow, 1U,
                                   __ATOMIC_RELAXED);
            }

            return destinationHandle;
        }

        // 普通安装先保留一份引用，安装失败就归还；成功后这份引用归目标槽所有。
        (source.reference)->retain();
        const uint64_t result = destination.installLocked(
            source.reference, source.getRights(), source.flags);

        if (result && destination.escrow_) {
            __atomic_add_fetch(&source.reference->escrow, 1U, __ATOMIC_RELAXED);
        }

        if (!result) {
            (source.reference)->release();
        }

        return result;
    }

    /*
     * 验证句柄并设置允许的句柄标志，例如执行新程序时关闭或禁止继承。
     */
    bool HandleTable::setFlags(uint64_t handle, uint32_t flags) {
        InterruptSpinLockGuard guard(lock_);

        if (!validLocked(handle) || (flags & ~os::handleFlagsMask)) {
            return false;
        }

        entries_[static_cast<uint32_t>(handle) - 1].flags = flags;

        return true;
    }

    /*
     * 在表锁内读取句柄标志，拒绝已失效的句柄。
     */
    bool HandleTable::getFlags(uint64_t handle, uint32_t& flags) {
        InterruptSpinLockGuard guard(lock_);

        if (!validLocked(handle)) {
            return false;
        }

        flags = entries_[static_cast<uint32_t>(handle) - 1].flags;

        return true;
    }

    /*
     * 扫描表中是否存在指向指定对象、类型匹配且权限足够的句柄。
     */
    bool HandleTable::hasObjectRights(void* object, HandleType type,
                                      uint32_t rights) {
        InterruptSpinLockGuard guard(lock_);

        for (uint32_t i = 0; i < capacity_; ++i) {
            const HandleEntry& entry = entries_[i];

            if (entry.getObject() == object &&
                entry.getType() == static_cast<uint16_t>(type) &&
                (entry.getRights() & rights) == rights) {
                return true;
            }
        }

        return false;
    }

    /*
     * 读取共享对象的引用数量，供服务判断对象是否还有其他使用者。
     */
    int64_t HandleTable::references(uint64_t handle) {
        InterruptSpinLockGuard guard(lock_);

        if (!validLocked(handle)) {
            return -2;
        }

        const Reference* reference =
            entries_[static_cast<uint32_t>(handle) - 1].reference;

        return static_cast<int64_t>(
                   __atomic_load_n(&reference->count, __ATOMIC_ACQUIRE)) -
               __atomic_load_n(&reference->escrow, __ATOMIC_ACQUIRE);
    }

    /*
     * 把消息携带的句柄转为独立对象引用。
     * 先验证全部句柄及转移权限，再按需要提交移动，失败时归还已取得的引用。
     */
    bool HandleTable::Transfer::capture(HandleTable& source,
                                        const os::IpcMessage& message,
                                        bool commitMove) {
        if (count_ || message.handleCount > 4) {
            return false;
        }

        InterruptSpinLockGuard guard(source.lock_);

        // 第一轮只验证输入，不移动任何句柄；最后一项非法时，前面几项也必须保持原样。
        for (uint32_t i = 0; i < message.handleCount; ++i) {
            if (!source.validLocked(message.handles[i])) {
                return false;
            }

            const HandleEntry& entry =
                source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
            const HandleType type = entry.reference->type;
            // 设备映射和中断绑定与任务归属绑定，不能经通用 IPC
            // 转移绕过其权限及退出清理规则。
            if (type == HandleType::MmioRegion ||
                type == HandleType::DmaBuffer ||
                type == HandleType::InterruptBinding ||
                type == HandleType::DeviceDomain ||
                type == HandleType::FramebufferMapping) {
                return false;
            }

            if (message.handleOptions[i] &
                ~(os::ipcHandleRestrictRights | os::ipcHandleMove)) {
                return false;
            }

            if ((message.handleOptions[i] & os::ipcHandleRestrictRights) &&
                (message.handleRights[i] & entry.getRights()) !=
                    message.handleRights[i]) {
                return false;
            }

            // 同一消息不能重复移动同一源句柄，否则第一次清槽后第二次就会重复消耗原引用。
            for (uint32_t j = 0; j < i; ++j) {
                if (message.handles[i] == message.handles[j] &&
                    ((message.handleOptions[i] | message.handleOptions[j]) &
                     os::ipcHandleMove)) {
                    return false;
                }
            }
        }

        // 全部输入验证通过后，先保留消息引用，再关闭移动来源句柄；即使来源不再持有句柄，队列消息仍拥有对象。
        for (uint32_t i = 0; i < message.handleCount; ++i) {
            HandleEntry& entry =
                source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
            references_[i] = entry.reference;
            rights_[i] =
                (message.handleOptions[i] & os::ipcHandleRestrictRights)
                    ? message.handleRights[i]
                    : entry.getRights();
            (references_[i])->retain();
        }

        count_ = message.handleCount;
        // 捕获引用是消息入队的提交点；发送路径之后不能再失败，移动后的队列引用成为新拥有者。
        for (uint32_t i = 0; i < count_; ++i) {
            if (!(message.handleOptions[i] & os::ipcHandleMove)) {
                continue;
            }

            // 需要延迟提交时先记下来源，等接收方有足够槽位再真正清除源句柄。
            if (!commitMove) {
                moveSource_     = &source;
                moveHandles_[i] = message.handles[i];
                continue;
            }

            HandleEntry& entry =
                source.entries_[static_cast<uint32_t>(message.handles[i]) - 1];
            entry.reference = nullptr;
            entry.setObject(nullptr);
            entry.setRights(0);
            entry.setType(static_cast<uint16_t>(HandleType::None));
            entry.flags = 0;
            (references_[i])->release();
        }

        return true;
    }

    /*
     * 将暂存引用安装进接收方句柄表。
     * 安装成功才把新句柄编号写回消息；失败时清理已安装槽位。
     */
    bool HandleTable::Transfer::install(HandleTable& destination,
                                        os::IpcMessage& message) {
        if (!moveSource_ || moveSource_ == &destination) {
            InterruptSpinLockGuard guard(destination.lock_);

            return installLocked(destination, message);
        }

        HandleTable* first  = reinterpret_cast<uintptr_t>(moveSource_) <
                                     reinterpret_cast<uintptr_t>(&destination)
                                  ? moveSource_
                                  : &destination;
        HandleTable* second = first == moveSource_ ? &destination : moveSource_;
        InterruptSpinLockGuard firstGuard(first->lock_);
        InterruptSpinLockGuard secondGuard(second->lock_);

        return installLocked(destination, message);
    }

    /*
     * 在目标表锁内安装消息中的全部引用。
     * 容量和槽位检查集中处理，避免对外发布半份转移结果。
     */
    bool HandleTable::Transfer::installLocked(HandleTable& destination,
                                              os::IpcMessage& message) {
        if (destination.disposed_) {
            return false;
        }

        // 延迟移动在提交前再次核对源槽仍指向同一包装，防止源句柄已被关闭并复用。
        if (moveSource_) {
            for (uint32_t i = 0; i < count_; ++i) {
                if (moveHandles_[i] &&
                    (!moveSource_->validLocked(moveHandles_[i]) ||
                     moveSource_
                             ->entries_[static_cast<uint32_t>(moveHandles_[i]) -
                                        1]
                             .reference != references_[i])) {
                    return false;
                }
            }
        }

        // 先确保目标能容纳整份句柄数组，再做不可逆的源槽清除，不能只移动一半。
        uint32_t free = 0;

        for (uint32_t i = 0; i < destination.capacity_; ++i) {
            if (!destination.entries_[i].getObject()) {
                ++free;
            }
        }

        // 目标容量不足时先扩容，失败仍可保留待传递引用并让接收操作重试。
        if (free < count_ &&
            !destination.growLocked(destination.capacity_ + count_ - free)) {
            return false;
        }

        if (moveSource_) {
            for (uint32_t i = 0; i < count_; ++i) {
                if (!moveHandles_[i]) {
                    continue;
                }

                HandleEntry& entry =
                    moveSource_
                        ->entries_[static_cast<uint32_t>(moveHandles_[i]) - 1];
                entry.reference = nullptr;
                entry.setObject(nullptr);
                entry.setRights(0);
                entry.setType(static_cast<uint16_t>(HandleType::None));
                entry.flags = 0;
                (references_[i])->release();
                moveHandles_[i] = 0;
            }

            moveSource_ = nullptr;
        }

        // 把发送方编号改成接收方新句柄；清空暂存引用数组表示所有权已交给目标槽，不能再重复归还这些引用。
        for (uint32_t i = 0; i < count_; ++i) {
            message.handles[i] =
                destination.installLocked(references_[i], rights_[i], 0);
            message.handleRights[i]  = rights_[i];
            message.handleOptions[i] = 0;
            references_[i]           = nullptr;
        }

        message.handleCount = count_;
        count_              = 0;

        return true;
    }

    /*
     * 释放传输对象暂存的引用并清空计数，取消未完成的句柄传递。
     */
    void HandleTable::Transfer::clear() {
        for (uint32_t i = 0; i < count_; ++i) {
            (references_[i])->release();
            references_[i] = nullptr;
        }

        count_      = 0;
        moveSource_ = nullptr;

        for (uint64_t& handle : moveHandles_) {
            handle = 0;
        }
    }

    /*
     * 关闭带执行关闭标志的句柄。
     * 逐个从表中摘下并在锁外释放，避免清理对象时嵌套表锁。
     */
    void HandleTable::closeOnExec() {
        // 每个槽位在锁内检查是否带关闭标记，再交给公共关闭路径；不把对象销毁放在整表扫描的锁内。
        for (uint32_t i = 0; i < capacity(); ++i) {
            uint64_t handle = 0;
            {
                InterruptSpinLockGuard guard(lock_);
                const HandleEntry& entry = entries_[i];

                if (entry.getObject() &&
                    (entry.flags & os::handleFlagCloseOnExec)) {
                    handle =
                        (static_cast<uint64_t>(entry.getGeneration()) << 32) |
                        (i + 1);
                }
            }

            if (handle) {
                close(handle);
            }
        }
    }

    /*
     * 将表标记为不可继续安装，并释放全部槽位及表存储。
     * 用于任务资源最终回收。
     */
    void HandleTable::dispose() {
        {
            InterruptSpinLockGuard guard(lock_);
            disposed_ = true;
        }

        closeAll();
        InterruptSpinLockGuard guard(lock_);
        mm::freeHeap(entries_);
        entries_  = nullptr;
        capacity_ = 0;
    }

    /*
     * 反复取出并关闭有效句柄，直到表为空；对象释放动作不放在表锁内执行。
     */
    void HandleTable::closeAll() {
        for (uint32_t index = 0; index < capacity_; ++index) {
            uint64_t handle = 0;
            {
                InterruptSpinLockGuard guard(lock_);
                const HandleEntry& entry = entries_[index];

                if (entry.getObject() != nullptr) {
                    handle =
                        (static_cast<uint64_t>(entry.getGeneration()) << 32) |
                        (index + 1);
                }
            }

            if (handle != 0) {
                close(handle);
            }
        }
    }

    /*
     * 保存启动时传给任务的引导句柄编号。
     */
    void Task::setBootstrapHandle(uint64_t handle) {
        bootstrapHandle_ = handle;
    }

    /*
     * 读取任务保存的引导句柄编号。
     */
    uint64_t Task::bootstrapHandle() const { return bootstrapHandle_; }

    /*
     * 设置标准输入、输出或错误句柄，只接受预留的三个位置。
     */
    void Task::setStandardHandle(uint32_t index, uint64_t handle) {
        if (index < 3) {
            standardHandles_[index] = handle;
        }
    }

    /*
     * 按标准流索引读取句柄，越界时返回零。
     */
    uint64_t Task::standardHandle(uint32_t index) const {
        return index < 3 ? standardHandles_[index] : 0;
    }

    /*
     * 读取任务保存的内核服务句柄编号。
     */
    uint64_t Task::kernelHandle() const { return kernelHandle_; }

    /*
     * 保存任务访问内核服务所用的句柄编号。
     */
    void Task::setKernelHandle(uint64_t handle) { kernelHandle_ = handle; }

}  // namespace task
