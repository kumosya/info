/* 内存区域、驻留页、后备对象与内存服务管理。 */

#pragma once
#include <os/detail/client.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include <cstddef>
#include <cstdint>

#include "service/common/protocol.h"
#include "service/common/recovery.h"

namespace mm {

    enum class BackingKind : uint8_t { Anonymous, File, Shared };

    /* 描述任务的一段连续虚拟区域，包括权限、后备对象和文件偏移。 */
    class Region {
        public:
            Region() = default;
            Region(uint64_t task, uint64_t address, uint64_t size,
                   uint32_t protection);
            ~Region() = default;

            uint64_t getTask() const {
                return task;
            }

            uint64_t getAddress() const {
                return address;
            }

            uint64_t getSize() const {
                return size;
            }

            uint64_t getOffset() const {
                return offset;
            }

            uint64_t getBacking() const {
                return backing;
            }

            uint32_t getProtection() const {
                return protection;
            }

            BackingKind getKind() const {
                return kind;
            }

            bool isUsed() const {
                return used;
            }

            bool isShared() const {
                return shared;
            }

        private:
            friend class RegionTable;
            friend class MemoryState;
            friend class MemoryManager;
            friend class Pager;
            friend class Recovery;
            friend class SharedMemoryManager;
            uint64_t task = 0, address = 0, size = 0, offset = 0, backing = 0;
            uint32_t protection = 0, maximumProtection = 0;
            BackingKind kind = BackingKind::Anonymous;
            bool lazy = true, shared = false, used = false;
    };

    /* 查找、插入、拆分与合并虚拟区域，操作时维护不重叠和偏移连续的规则。 */
    class RegionTable {
        public:
            static constexpr size_t capacity = 512;
            static constexpr uint64_t lower = 0x10000,
                                      upper = 0x0000800000000000ULL;
            static bool validRange(uint64_t address, uint64_t size);
            const Region* find(uint64_t task, uint64_t address) const;
            Region* find(uint64_t task, uint64_t address);
            uint64_t findGap(uint64_t task, uint64_t size,
                             uint64_t hint = 0x50000000) const;
            bool insert(const Region& region);
            bool remove(uint64_t task, uint64_t address, uint64_t size);
            bool protect(uint64_t task, uint64_t address, uint64_t size,
                         uint32_t protection);
            bool covered(uint64_t task, uint64_t address, uint64_t size) const;
            void removeTask(uint64_t task);
            size_t references(uint64_t backing) const;

            const Region& at(size_t index) const {
                return regions_[index];
            }

        private:
            Region regions_[capacity]{};
            Region* vacant();
            size_t vacancies() const;
            void merge(uint64_t task);
    };

    /* 封装用户态内存服务需要调用的内核内存请求。 */
    class KernelMemory {
        public:
            KernelMemory();
            ~KernelMemory() = default;
            int control(services::MemoryOperation operation, uint64_t task,
                        uint64_t cursor, os::IpcMessage& reply);
            int residency(uint64_t task, uint64_t address, uint64_t size,
                          bool lock, bool allowAbsent = true,
                          bool checkOnly = false);
            int lockState(uint64_t task, uint64_t& bytes, bool& future,
                          uint64_t& limit);
            int bindPager(uint64_t endpoint);
            bool available(uint64_t task, uint64_t address, uint64_t size,
                           int* error = nullptr);
            uint64_t pageState(uint64_t task, uint64_t address);
            int mapShared(uint64_t handle, uint64_t task, uint64_t address,
                          uint64_t size, uint64_t offset, uint64_t protection);

            uint64_t nativeProtection(uint32_t protection) const {
                return ((protection & 2) ? 1 : 0) | ((protection & 4) ? 2 : 0);
            }

        private:
            os::detail::RequestChannel channel_;
    };

    /* 记录一页实际内存对应的任务、地址、后备来源和脏页等状态。 */
    class ResidentPage {
        public:
            ResidentPage() = default;
            ResidentPage(uint64_t task, uint64_t address, uint64_t handle);
            ~ResidentPage() = default;

            uint64_t getTask() const {
                return task;
            }

            uint64_t getAddress() const {
                return address;
            }

            uint64_t getHandle() const {
                return handle;
            }

            uint64_t getBacking() const {
                return backing;
            }

            uint64_t getLastUse() const {
                return lastUse;
            }

            bool isUsed() const {
                return used;
            }

            bool isMapped() const {
                return mapped;
            }

            bool isDirty() const {
                return dirty;
            }

            bool isPinned() const {
                return pinned;
            }

        private:
            friend class MemoryState;
            friend class MemoryManager;
            friend class Pager;
            friend class ReclaimManager;
            friend class Recovery;
            uint64_t task = 0, address = 0, handle = 0, backing = 0,
                     lastUse = 0, fileOffset = 0;
            bool used = false, mapped = false, pinned = false, io = false,
                 cow = false, dirty = false;
    };

    /* 保存匿名、文件或共享区域的数据来源及其持有的句柄。 */
    class Backing {
        public:
            Backing() = default;
            Backing(uint64_t id, BackingKind kind, uint64_t handle = 0);
            ~Backing() = default;

            uint64_t getId() const {
                return id;
            }

            uint64_t getHandle() const {
                return handle;
            }

            uint64_t getDescriptor() const {
                return descriptor;
            }

            uint64_t getFileService() const {
                return fs;
            }

            uint64_t getLease() const {
                return lease;
            }

            BackingKind getKind() const {
                return kind;
            }

            bool isUsed() const {
                return used;
            }

            bool isWritable() const {
                return writable;
            }

        private:
            friend class MemoryState;
            friend class MemoryManager;
            friend class Pager;
            friend class Recovery;
            friend class SharedMemoryManager;
            uint64_t id = 0, handle = 0, descriptor = 0, fs = 0, lease = 0;
            BackingKind kind = BackingKind::Anonymous;
            bool used = false, writable = false;
    };

    /* 汇总区域、驻留页、后备对象和任务映像代号，供用户态内存策略共同使用。 */
    class MemoryState {
        public:
            static constexpr size_t pageCapacity = 32768, backingCapacity = 256;
            static constexpr size_t taskPageLimit = 16384;
            MemoryState() = default;
            ~MemoryState() = default;
            ResidentPage* page(uint64_t task, uint64_t address);
            ResidentPage* vacantPage();
            Backing* backing(uint64_t id);
            Backing* createBacking(BackingKind kind);
            uint64_t pagesUsed(uint64_t task) const;
            bool references(uint64_t backing) const;
            void collectBackings(bool releaseFiles = false);
            void releaseTask(uint64_t task);
            int synchronize(uint64_t task);
            bool sweep();
            void pruneRecovered();

            uint64_t domainAt(size_t index) const {
                return index < 128 ? domains_[index].task : 0;
            }

            uint64_t generation(uint64_t task) const;

        private:
            friend class MemoryManager;
            friend class Pager;
            friend class Recovery;
            friend class ReclaimManager;
            friend class PageAllocator;
            RegionTable regions;
            ResidentPage pages[pageCapacity]{};
            Backing backings[backingCapacity]{};
            uint64_t clock = 0;
            uint64_t nextBacking_ = 1;

            /* 把同一任务或服务角色的状态归为一个管理项。 */
            struct Domain {
                    uint64_t task = 0, generation = 0;
            } domains_[128];
    };

    /* 以位保存驻留页事务状态，让回滚记录占用较小的服务栈空间。 */
    class ResidentPageFlags {
        public:
            ResidentPageFlags() = default;
            ~ResidentPageFlags() = default;
            bool get(size_t index) const;
            void set(size_t index, bool value);

        private:
            uint64_t words_[(MemoryState::pageCapacity + 63) / 64]{};
    };

    /* 挑选可从文件重建的干净页面并回收，保留脏页和被固定的页面。 */
    class ReclaimManager {
        public:
            explicit ReclaimManager(MemoryState& state) : state_(state) {
            }

            uint64_t reclaim(uint64_t bytes);

        private:
            MemoryState& state_;
    };

    /* 申请单页对象，内存不足时先请求回收再重试。 */
    class PageAllocator {
        public:
            PageAllocator(MemoryState& state, ReclaimManager& reclaim)
                : state_(state), reclaim_(reclaim) {
            }

            int64_t allocate(uint64_t task);

        private:
            MemoryState& state_;
            ReclaimManager& reclaim_;
    };

    /* 解析共享内存映射请求，将区域关联到相应后备对象。 */
    class SharedMemoryManager {
        public:
            SharedMemoryManager(Backing& backing, Region& region)
                : backing_(backing), region_(region) {
            }

            ~SharedMemoryManager() = default;
            int attach(const os::IpcMessage& request);

        private:
            Backing& backing_;
            Region& region_;
    };

    /* 按区域权限和数据来源处理缺页、文件读入以及写时复制。 */
    class Pager {
        public:
            Pager(MemoryState& state, ReclaimManager& reclaim)
                : state_(state), reclaim_(reclaim), allocator_(state, reclaim) {
            }

            int fault(uint64_t task, uint64_t address, uint64_t flags);
            int populateLocked(uint64_t task, uint64_t address);
            int clone(uint64_t parent, uint64_t child);

        private:
            MemoryState& state_;
            ReclaimManager& reclaim_;
            PageAllocator allocator_;
            int loadFile(ResidentPage& page, const Region& region,
                         Backing& backing);
            int copyPrivate(ResidentPage& page, const Region& region);
    };

    constexpr uint64_t recoveryRpcMarker = 0x4d4d525043303031ULL;

    /* 保存服务恢复会话并提供检查点调用操作。 */
    class Recovery {
        public:
            explicit Recovery(MemoryState& state)
                : state_(state), capabilities_(checkpoint_) {
            }

            bool open(uint64_t& endpoint);
            bool prepare(const os::IpcMessage& request, uint64_t id);

            bool flush() {
                return !checkpoint_.prepare() && save();
            }

            bool save();
            bool activate();
            bool lookup(const os::IpcMessage& request, uint64_t id,
                        os::IpcMessage& reply);
            bool remember(const os::IpcMessage& request, uint64_t id,
                          const os::IpcMessage& reply);

        private:
            static constexpr size_t outcomeCapacity = 256;

            /* 保存一次内存请求的编号、调用者及返回结果。 */
            struct Outcome {
                    uint64_t task = 0, generation = 0, id = 0, digest = 0,
                             handle = 0;
                    uint64_t result[7]{};
                    uint32_t rights = 0, reserved = 0;
            } outcomes_[outcomeCapacity];

            static_assert(sizeof(Outcome) == 104);

            /* 保存可以写入检查点的服务状态，重启时据此重建运行记录。 */
            struct Snapshot {
                    uint64_t endpoint = 0;
                    MemoryState state;
                    Outcome outcomes[outcomeCapacity];
            } snapshot_;

            MemoryState& state_;
            os::Sys::Checkpoint checkpoint_;
            services::RecoveryCapabilities capabilities_;
            uint64_t endpoint_ = 0;
            bool validate() const;
            bool restoreFrozen();
            bool collectFileLeases();
            void collectOutcomes();
            static uint64_t digest(const os::IpcMessage& request);
    };
}  // namespace mm
