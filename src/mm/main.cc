/*
 * src/mm/main.cc
 * 用户态内存服务主循环，协调请求、锁页、映射修改及恢复检查点。
 */

#include <os/detail/client.h>
#include <os/file.h>
#include <os/ipc.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/task.h>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/mm/mm.h"

namespace mm {

    class MemoryManager {
        public:
            /*
             * 启动并注册用户态内存服务。
             * 恢复已有检查点、绑定补页端点后进入请求循环；每次修改先准备恢复状态，保存结果后再恢复被冻结任务并回复。
             */
            int run() {
                const uint64_t registry = os::getBootstrapHandle();

                if (!registry || !recovery_.open(endpoint_) ||
                    KernelMemory().bindPager(endpoint_) ||
                    !os::detail::RegistryClient(registry).registerName(
                        "mm", endpoint_) ||
                    !recovery_.activate()) {
                    return 1;
                }

                os::Sys().log(
                    "[MM] Ring 3 memory policy and pager service ready\n");
                uint64_t nextSweep = 0;
                bool dirty = false;

                while (true) {
                    const uint64_t now = os::Sys().readMonotonicTime();

                    // 定期清理已退出或换程序的任务记录，只有管理状态发生变化才需要保存新检查点。
                    if (now >= nextSweep) {
                        dirty = state_.sweep() || dirty;

                        if (dirty && recovery_.flush()) {
                            dirty = false;
                        }

                        nextSweep = now + 50000000;
                    }

                    os::IpcMessage request{}, reply{};
                    const int received =
                        os::receiveMessage(endpoint_, request, nextSweep);

                    if (received == static_cast<int>(os::ErrorCode::Timeout)) {
                        continue;
                    }

                    if (received) {
                        return 2;
                    }

                    uint64_t id = 0;
                    bool resumeOnly = false;

                    // 恢复协议把请求编号放在消息尾部，先剥离协议字段，再按原业务消息格式解析。
                    if (request.length >= 3 &&
                        request.data[request.length - 3] == recoveryRpcMarker) {
                        id = request.data[request.length - 2];
                        resumeOnly = request.data[request.length - 1] != 0;
                        request.length -= 3;
                    } else if (request.type ==
                                   static_cast<uint64_t>(
                                       services::MmOperation::Fork) &&
                               request.length == 1) {
                        id = request.data[0] | (1ULL << 63);
                    }

                    // 先寻找已提交结果，再决定是否执行；客户端重试不能导致同一次映射修改做两遍。
                    if (!recovery_.lookup(request, id, reply)) {
                        if (resumeOnly) {
                            reply.data[0] =
                                static_cast<uint64_t>(os::ErrorCode::PeerDied);
                        } else if (!recovery_.prepare(request, id)) {
                            os::Sys().log(
                                "[MM] checkpoint preparation failed\n");

                            if (!recovery_.activate()) {
                                return 2;
                            }

                            reply.data[0] = static_cast<uint64_t>(
                                os::ErrorCode::OutOfMemory);
                        } else {
                            // 预备阶段完成后才执行业务修改；之后先保存返回结果和快照，再解冻客户并回复。
                            handle(request, reply);

                            if (!recovery_.remember(request, id, reply) ||
                                !recovery_.save()) {
                                return 2;
                            }

                            if (!recovery_.activate()) {
                                return 2;
                            }

                            dirty = false;
                        }
                    }

                    reply.length = os::ipcDataWordCount;
                    os::replyMessage(endpoint_, request.replyToken, reply);

                    // 消息传入和回复暂用句柄都由本轮关闭，真正需要长期保留的引用已转给状态表或检查点。
                    for (uint32_t i = 0; i < request.handleCount; ++i) {
                        os::closeHandle(request.handles[i]);
                    }

                    for (uint32_t i = 0; i < reply.handleCount; ++i) {
                        os::closeHandle(reply.handles[i]);
                    }
                }
            }

        private:
            MemoryState state_;
            Recovery recovery_{state_};
            ReclaimManager reclaim_{state_};
            Pager pager_{state_, reclaim_};
            uint64_t endpoint_ = 0;

            /*
             * 逐页准备目标区间的实际页面，可按需要暂时固定页面以防回收。
             * 没有区域记录的地址必须已有内核映射，否则返回内存错误。
             */
            int populate(uint64_t task, uint64_t address, uint64_t size,
                         bool pin = false) {
                for (uint64_t offset = 0; offset < size; offset += 4096) {
                    if (state_.regions.find(task, address + offset)) {
                        const int status =
                            pager_.populateLocked(task, address + offset);

                        if (status) {
                            return status == -5 ? 12 : status == -3 ? 13 : 12;
                        }

                        if (pin) {
                            if (ResidentPage* page =
                                    state_.page(task, address + offset)) {
                                page->pinned = true;
                            }
                        }
                    } else if (!(KernelMemory().pageState(task,
                                                          address + offset) &
                                 1)) {
                        return 12;
                    }
                }

                return 0;
            }

            /*
             * 处理区间锁页、整进程锁页和内存建议请求。
             * 先检查覆盖范围和额度，再补齐页面并提交内核操作；失败时恢复临时固定状态。
             */
            int residency(const os::IpcMessage& request, unsigned operation) {
                const uint64_t task = request.senderPid,
                               address = request.data[0],
                               size = request.data[1];

                if (operation <= 1) {
                    if (!RegionTable::validRange(address, size)) {
                        return 22;
                    }

                    // 允许尚无页面的禁止访问区域之前先验证区域覆盖；本批已经补入的页面先固定，防止后续补页回收它们。
                    if (!operation) {
                        for (uint64_t offset = 0; offset < size;
                             offset += 4096) {
                            if (!state_.regions.find(task, address + offset) &&
                                !(KernelMemory().pageState(task,
                                                           address + offset) &
                                  1)) {
                                return 12;
                            }
                        }
                    }

                    // 先让内核只检查锁页条件，不真正修改；预检查失败就不继续分配页面。
                    const int preflight = KernelMemory().residency(
                        task, address, size, operation == 0, true, true);

                    if (preflight) {
                        return preflight;
                    }

                    // 保存原固定状态，补页过程可能触发回收，因此本次新补页面先固定，失败时恢复旧值。
                    ResidentPageFlags pinned;

                    for (size_t i = 0; i < MemoryState::pageCapacity; ++i) {
                        pinned.set(i, state_.pages[i].pinned);
                    }

                    int status =
                        !operation ? populate(task, address, size, true) : 0;

                    if (!status) {
                        status = KernelMemory().residency(task, address, size,
                                                          operation == 0);
                    }

                    if (status) {
                        for (size_t i = 0; i < MemoryState::pageCapacity; ++i) {
                            state_.pages[i].pinned = pinned.get(i);
                        }
                    }

                    if (!status) {
                        for (ResidentPage& page : state_.pages) {
                            if (page.used && page.task == task &&
                                page.address >= address &&
                                page.address - address < size) {
                                page.pinned = operation == 0;
                            }
                        }
                    }

                    return status;
                }

                // 内存建议与真正锁页分别处理；需要预读时补入页面，但不把它们永久标成不可回收。
                if (operation == 4) {
                    if (request.data[2] > 4 ||
                        !RegionTable::validRange(address, size)) {
                        return 22;
                    }

                    for (uint64_t offset = 0; offset < size; offset += 4096) {
                        if (!state_.regions.find(task, address + offset) &&
                            !(KernelMemory().pageState(task, address + offset) &
                              1)) {
                            return 12;
                        }
                    }

                    return request.data[2] == 3 ? populate(task, address, size)
                                                : 0;
                }

                const unsigned int flags =
                    operation == 3 ? 0 : unsigned(request.data[0]);

                if ((operation == 2 && !flags) || flags > 3) {
                    return 22;
                }

                os::IpcMessage command{}, reply{};
                command.type = uint64_t(services::MemoryOperation::LockAll);
                command.length = 5;
                command.data[0] = task;
                command.data[1] = flags;
                unsigned count = 0;

                // 用局部对象保存原固定状态；任意提前返回都会恢复，只有明确提交后才保留新状态。
                struct PinTransaction {
                        MemoryState& state;
                        ResidentPageFlags original;
                        bool committed = false;

                        /*
                         * 记录所有驻留页原来的固定状态，供整进程锁页失败时回滚。
                         */
                        explicit PinTransaction(MemoryState& value)
                            : state(value) {
                            for (size_t i = 0; i < MemoryState::pageCapacity;
                                 ++i) {
                                original.set(i, state.pages[i].pinned);
                            }
                        }

                        /*
                         * 锁页事务没有提交时恢复原状态，防止只补了一部分页面就留下永久固定标记。
                         */
                        ~PinTransaction() {
                            if (!committed) {
                                for (size_t i = 0;
                                     i < MemoryState::pageCapacity; ++i) {
                                    state.pages[i].pinned = original.get(i);
                                }
                            }
                        }
                } pins(state_);

                if (flags & 1) {
                    for (size_t i = 0; i < RegionTable::capacity; ++i) {
                        const Region& region = state_.regions.at(i);

                        if (!region.used || region.task != task) {
                            continue;
                        }

                        const int status =
                            populate(task, region.address, region.size, true);

                        if (status) {
                            return status;
                        }

                        if (count == 128) {
                            return 12;
                        }

                        command.data[3 + count * 2] = region.address;
                        command.data[4 + count * 2] = region.size;
                        ++count;
                    }
                }

                command.data[2] = count;

                if (command.length < 3 + count * 2) {
                    command.length = 3 + count * 2;
                }

                const int status = os::detail::RequestChannel(
                                       services::CoreServiceKind::Memory)
                                       .result(command, reply);

                if (status) {
                    return 5;
                }

                if (reply.data[1]) {
                    return int(reply.data[1]);
                }

                if ((flags & 1) || !flags) {
                    for (ResidentPage& page : state_.pages) {
                        if (page.used && page.task == task) {
                            page.pinned = flags != 0;
                        }
                    }
                }

                // 只有内核整进程锁页请求成功，局部回滚对象才停止恢复旧标记。
                pins.committed = true;

                return 0;
            }

            /*
             * 先在区域表副本中计算删除结果，再解除实际页面并提交记录。
             * 对象和文件引用在不再被使用时回收。
             */
            int unmap(uint64_t task, uint64_t address, uint64_t size) {
                // 区域元数据先在副本中试做，实际内核操作成功后才提交正式记录。
                RegionTable staged = state_.regions;

                if (!staged.remove(task, address, size)) {
                    return -1;
                }

                if (KernelMemory().residency(task, address, size, false, true,
                                             true)) {
                    return -5;
                }

                ResidentPageFlags wasMapped;

                for (size_t i = 0; i < MemoryState::pageCapacity; ++i) {
                    ResidentPage& page = state_.pages[i];

                    if (!page.used || page.task != task ||
                        page.address < address ||
                        page.address - address >= size || !page.mapped) {
                        continue;
                    }

                    wasMapped.set(i, true);

                    if (os::detail::MemoryControl().unmapTarget(
                            page.address, 4096, task, true)) {
                        // 解除映射事务提交之前，继续保留后备对象引用。
                        for (size_t n = 0; n < i; ++n) {
                            if (wasMapped.get(n)) {
                                const Region* old = state_.regions.find(
                                    task, state_.pages[n].address);

                                if (old) {
                                    pager_.fault(task, state_.pages[n].address,
                                                 (old->protection & 1)   ? 0
                                                 : (old->protection & 4) ? 16
                                                                         : 2);
                                }
                            }
                        }

                        return -11;
                    }

                    page.mapped = false;
                }

                if (KernelMemory().residency(task, address, size, false)) {
                    return -11;
                }

                // 到这里实际解除已完成，再让区域查询看到新布局，随后收集无人引用的后备对象。
                state_.regions = staged;

                // 逐页撤销实际映射及页面引用，区域表草稿只有在实际操作成功后才可发布。
                for (ResidentPage& page : state_.pages) {
                    if (page.used && page.task == task &&
                        page.address >= address &&
                        page.address - address < size) {
                        if (page.handle) {
                            os::closeHandle(page.handle);
                        }

                        page = {};
                    }
                }

                state_.collectBackings();

                return 0;
            }

            /*
             * 先验证区域覆盖和新权限，再调整实际映射。
             * 需要时重建页记录，并使区域表与内核页表保持一致。
             */
            int protect(uint64_t task, uint64_t address, uint64_t size,
                        uint32_t protection) {
                // 区域元数据先在副本中试做，实际内核操作成功后才提交正式记录。

                // 先在副本里检查覆盖范围、权限上限和拆分容量，原区域表继续作为失败恢复的依据。
                RegionTable staged = state_.regions;

                if (!staged.protect(task, address, size, protection)) {
                    return -3;
                }

                ResidentPageFlags changed;

                for (size_t index = 0; index < MemoryState::pageCapacity;
                     ++index) {
                    ResidentPage& page = state_.pages[index];

                    if (!page.used || page.task != task ||
                        page.address < address ||
                        page.address - address >= size || !page.mapped) {
                        continue;
                    }

                    page.dirty = page.dirty ||
                                 (KernelMemory().pageState(task, page.address) &
                                  (1ULL << 6));
                    const int result =
                        protection
                            ? os::detail::MemoryControl().protectTarget(
                                  page.address, 4096,
                                  KernelMemory().nativeProtection(protection) &
                                      (page.cow ? ~1ULL : ~0ULL),
                                  task)
                            : os::detail::MemoryControl().unmapTarget(
                                  page.address, 4096, task, true);

                    if (result) {
                        // 权限修改失败时，按尚未提交的原区域表恢复页表权限。
                        for (size_t n = 0; n < index; ++n) {
                            if (changed.get(n)) {
                                ResidentPage& previous = state_.pages[n];
                                const Region* old =
                                    state_.regions.find(task, previous.address);

                                if (!old) {
                                    continue;
                                }

                                if (!protection) {
                                    pager_.fault(task, previous.address,
                                                 (old->protection & 1)   ? 0
                                                 : (old->protection & 4) ? 16
                                                                         : 2);
                                } else {
                                    os::detail::MemoryControl().protectTarget(
                                        previous.address, 4096,
                                        KernelMemory().nativeProtection(
                                            old->protection) &
                                            (previous.cow ? ~1ULL : ~0ULL),
                                        task);
                                }
                            }
                        }

                        return result;
                    }

                    changed.set(index, true);

                    if (!protection) {
                        page.mapped = false;
                    }
                }

                // 所有页权限都调整成功后才公布新区域权限，避免管理表提前声称操作已经完成。
                state_.regions = staged;

                return 0;
            }

            /*
             * 按匿名、文件或共享来源建立虚拟区域。
             * 检查范围、权限和文件租用信息，选定地址后登记区域，按标志决定是否立即补页。
             */
            int64_t reserve(const os::IpcMessage& request, BackingKind kind) {
                const uint64_t* args = request.data;
                const uint64_t size = args[0];
                const uint32_t protection = static_cast<uint32_t>(args[1]);
                const uint64_t hint =
                    kind == BackingKind::Anonymous ? args[3] : args[4];

                if (!size || (size & 4095) || size > 32ULL * 1024 * 1024 ||
                    args[1] > 7 || (args[1] & 6) == 6 || (args[2] & ~7ULL)) {
                    return -1;
                }

                // 先创建后备对象记录，再根据匿名、文件或共享来源填充它；失败路径统一收集没有区域引用的记录。
                Backing* backing = state_.createBacking(kind);

                if (!backing) {
                    return -5;
                }

                Region region{};
                region.task = request.senderPid;
                region.size = size;
                region.backing = backing->id;
                region.protection = protection;
                region.maximumProtection = 7;
                region.kind = kind;
                region.lazy = !(args[2] & services::mmFlagEager);
                region.shared = args[2] & services::mmFlagShared;

                // 共享来源通过传入句柄建立长期引用，区域权限上限也由原句柄权限决定。
                if (kind == BackingKind::Shared) {
                    const int result =
                        SharedMemoryManager(*backing, region).attach(request);

                    if (result) {
                        state_.collectBackings();

                        return result;
                    }
                }

                // 文件来源先兑现文件服务发出的映射授权，取得描述符和租用编号，不能相信调用者自报文件身份。
                if (kind == BackingKind::File) {
                    if ((args[2] & services::mmFlagShared) ||
                        (args[3] & 4095) || args[3] > UINT64_MAX - size ||
                        request.length < 6) {
                        state_.collectBackings();

                        return -1;
                    }

                    // 只有受保护的文件服务可以验证并兑现文件映射授权。
                    const uint64_t fs =
                        os::detail::RegistryClient(
                            os::detail::RegistryClient().endpoint())
                            .lookup(services::fsServiceName, 1);
                    os::IpcMessage claim{}, reply{};
                    claim.type = static_cast<uint64_t>(
                        services::FsRequest::ClaimMapping);
                    claim.length = 2;
                    claim.data[0] = args[5];
                    claim.data[1] = request.senderPid;
                    const int result =
                        fs ? os::callService(fs, claim, reply) : -4;

                    if (result || static_cast<int64_t>(reply.data[0])) {
                        if (fs) {
                            os::closeHandle(fs);
                        }

                        state_.collectBackings();

                        return result ? result
                                      : static_cast<int64_t>(reply.data[0]);
                    }

                    backing->fs = fs;
                    backing->descriptor = reply.data[1];
                    backing->lease = reply.length >= 4 ? reply.data[3] : 0;
                    region.offset = args[3];
                }

                uint64_t candidate = hint ? hint : 0x50000000;

                // 区域表的空洞还可能被内核其他映射占用，因此同时查询实际页表，并限制寻找次数。
                for (unsigned attempt = 0; attempt < 16; ++attempt) {
                    region.address =
                        state_.regions.findGap(region.task, size, candidate);

                    if (!region.address) {
                        break;
                    }

                    int availabilityError = 0;

                    if (KernelMemory().available(region.task, region.address,
                                                 size, &availabilityError)) {
                        break;
                    }

                    if (availabilityError) {
                        state_.collectBackings();

                        return availabilityError;
                    }

                    candidate = region.address + size;
                    region.address = 0;
                }

                if (!region.address || !state_.regions.insert(region)) {
                    os::Sys().log("[MM] virtual address reservation failed\n");
                    state_.collectBackings();

                    return -5;
                }

                uint64_t lockedBytes = 0, limit = 0;
                bool future = false;

                if (KernelMemory().lockState(region.task, lockedBytes, future,
                                             limit)) {
                    unmap(region.task, region.address, region.size);

                    return -11;
                }

                // 任务要求未来映射自动锁定时，先检查并落实锁页约定，失败应撤销刚建立的区域。
                if (future) {
                    if (KernelMemory().residency(region.task, region.address,
                                                 region.size, true)) {
                        unmap(region.task, region.address, region.size);

                        return -5;
                    }

                    const int error =
                        populate(region.task, region.address, region.size);

                    if (error) {
                        unmap(region.task, region.address, region.size);

                        return -5;
                    }

                    for (ResidentPage& page : state_.pages) {
                        if (page.used && page.task == region.task &&
                            page.address >= region.address &&
                            page.address - region.address < region.size) {
                            page.pinned = true;
                        }
                    }
                }

                // 立即映射且允许访问时，在返回前补齐各页；延迟区域只登记地址和来源，等真正访问时补页。
                if (!region.lazy && protection) {
                    for (uint64_t offset = 0; offset < size; offset += 4096) {
                        const int result =
                            pager_.fault(region.task, region.address + offset,
                                         (protection & 1)   ? 0
                                         : (protection & 4) ? 16
                                                            : 2);

                        if (result) {
                            unmap(region.task, region.address, region.size);

                            return result;
                        }
                    }
                }

                return region.address;
            }

            /*
             * 分发用户态内存请求，先验证消息长度和调用者，再交给区域、补页、共享内存或回收模块。
             */
            void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
                reply.data[0] = static_cast<int64_t>(-1);

                if (!request.senderPid) {
                    return;
                }

                const int synchronized = state_.synchronize(request.senderPid);

                if (synchronized < 0) {
                    reply.data[0] = synchronized;

                    return;
                }

                const services::MmOperation operation =
                    static_cast<services::MmOperation>(request.type);

                // exec 后内核已复制新程序的页表；旧代记录清理完即可为 fork 建立子记录。
                // 子任务的父子关系和未运行状态仍由 Pager::clone 核对。
                if (synchronized &&
                    operation != services::MmOperation::MapAnonymous &&
                    operation != services::MmOperation::MapShared &&
                    operation != services::MmOperation::MapFile &&
                    operation != services::MmOperation::Fork) {
                    reply.data[0] =
                        static_cast<int64_t>(os::ErrorCode::PeerDied);

                    return;
                }

                if (operation >= services::MmOperation::Lock &&
                    operation <= services::MmOperation::Advise) {
                    reply.data[0] = 0;

                    if (request.length < 3) {
                        reply.data[1] = 22;
                    } else {
                        reply.data[1] = residency(
                            request, unsigned(operation) -
                                         unsigned(services::MmOperation::Lock));
                    }

                    return;
                }

                // 把请求先按操作分类，再由每个分支检查自己的长度、句柄数量和访问范围，不让格式错误进入内存状态修改。
                switch (operation) {
                    // 偏移查询只针对允许导出的共享对象，返回引用前仍要核对区域和后备来源，不能提供任意物理地址。
                    case services::MmOperation::MemoryOffset: {
                        if (request.length != 2) {
                            break;
                        }

                        const uint64_t address = request.data[0],
                                       size = request.data[1];

                        if (!size || address < 0x10000 ||
                            address >= 0x800000000000ULL ||
                            size > 0x800000000000ULL - address) {
                            break;
                        }

                        const Region* region =
                            state_.regions.find(request.senderPid, address);

                        if (!region) {
                            reply.data[0] = -4;
                            break;
                        }

                        const Backing* backing =
                            state_.backing(region->backing);

                        if (!backing || backing->kind != BackingKind::Shared) {
                            reply.data[0] = -7;
                            break;
                        }

                        const uint64_t offset =
                            region->offset + address - region->address;
                        uint64_t end = region->address + region->size;

                        while (end < address + size) {
                            const Region* next =
                                state_.regions.find(request.senderPid, end);

                            if (!next || next->backing != region->backing ||
                                next->offset != offset + end - address) {
                                break;
                            }

                            end = next->address + next->size;
                        }

                        if (end < address + size) {
                            reply.data[0] = -7;
                            break;
                        }

                        const unsigned rights = backing->writable ? 7 : 1;
                        os::Ipc::Handle duplicate;
                        const int status =
                            os::Ipc::Handle(backing->handle, false)
                                .duplicate(rights, 0, duplicate);

                        if (status) {
                            reply.data[0] = status;
                            break;
                        }

                        reply.data[0] = 0;
                        reply.data[1] = offset;
                        reply.data[2] = end - address;
                        reply.data[3] = rights;
                        reply.handleCount = 1;
                        reply.handles[0] = duplicate.release();
                        reply.handleRights[0] = rights;
                        reply.handleOptions[0] =
                            services::ipcHandleRestrictRights;
                        break;
                    }

                    // 匿名映射没有文件或共享对象来源，登记区域后按延迟标志决定何时实际分配页面。
                    case services::MmOperation::MapAnonymous:
                        if (request.length >= 4) {
                            reply.data[0] =
                                reserve(request, BackingKind::Anonymous);
                        }

                        if (static_cast<int64_t>(reply.data[0]) > 0) {
                            const Region* region = state_.regions.find(
                                request.senderPid, reply.data[0]);

                            if (region) {
                                reply.data[1] = region->backing;
                            }
                        }

                        break;

                    // 共享映射需要从消息句柄取得授权，返回的虚拟地址属于调用者，后备引用则由内存服务保存。
                    case services::MmOperation::MapShared:
                        if (request.length >= 5) {
                            reply.data[0] =
                                reserve(request, BackingKind::Shared);
                        }

                        if (static_cast<int64_t>(reply.data[0]) > 0) {
                            const Region* region = state_.regions.find(
                                request.senderPid, reply.data[0]);

                            if (region) {
                                reply.data[1] = region->backing;
                            }
                        }

                        break;

                    // 文件映射使用文件服务签发的授权，避免把普通整数描述符误当跨进程可用权限。
                    case services::MmOperation::MapFile:
                        if (request.length >= 6) {
                            reply.data[0] = reserve(request, BackingKind::File);
                        }

                        if (static_cast<int64_t>(reply.data[0]) > 0) {
                            const Region* region = state_.regions.find(
                                request.senderPid, reply.data[0]);

                            if (region) {
                                reply.data[1] = region->backing;
                            }
                        }

                        break;

                    // 缺页请求交给 pager
                    // 核对区域权限并准备实际页面，不能仅因收到请求就增加访问权限。
                    case services::MmOperation::Fault:
                        if (request.length >= 2) {
                            reply.data[0] =
                                pager_.fault(request.senderPid, request.data[0],
                                             request.data[1]);
                        }

                        break;

                    // fork
                    // 只复制符合亲子关系的未运行子任务记录，页面共享及写时复制规则由专用路径处理。
                    case services::MmOperation::Fork:
                        if (request.length == 1) {
                            reply.data[0] = pager_.clone(request.senderPid,
                                                         request.data[0]);
                        }

                        break;
                    case services::MmOperation::Unmap:
                        if (request.length >= 2) {
                            reply.data[0] =
                                unmap(request.senderPid, request.data[0],
                                      request.data[1]);
                        }

                        break;
                    case services::MmOperation::Protect:
                        if (request.length >= 3 && request.data[2] <= 7) {
                            reply.data[0] =
                                protect(request.senderPid, request.data[0],
                                        request.data[1], request.data[2]);
                        }

                        break;

                    // 按对象操作涉及多段映射时，先检查与调用者归属相符的区域，再执行删除或改权限。
                    case services::MmOperation::UnmapOwned:
                    case services::MmOperation::ProtectOwned:
                    case services::MmOperation::SyncOwned: {
                        if (request.length != 5 ||
                            !RegionTable::validRange(request.data[0],
                                                     request.data[1]) ||
                            !request.data[2]) {
                            break;
                        }

                        if (operation == services::MmOperation::ProtectOwned &&
                            request.data[3] > 7) {
                            break;
                        }

                        const uint64_t address = request.data[0],
                                       size = request.data[1],
                                       identity = request.data[2];

                        if (operation == services::MmOperation::UnmapOwned) {
                            reply.data[0] = 0;

                            for (size_t index = 0;
                                 index < RegionTable::capacity; ++index) {
                                const Region region = state_.regions.at(index);

                                if (!region.used ||
                                    region.task != request.senderPid ||
                                    region.backing != identity ||
                                    region.address >= address + size ||
                                    address >= region.address + region.size) {
                                    continue;
                                }

                                const uint64_t first = region.address > address
                                                           ? region.address
                                                           : address;
                                const uint64_t end =
                                    region.address + region.size <
                                            address + size
                                        ? region.address + region.size
                                        : address + size;
                                const int status = unmap(request.senderPid,
                                                         first, end - first);

                                if (status) {
                                    reply.data[0] = status;
                                    break;
                                }

                                index = static_cast<size_t>(-1);
                            }
                        } else {
                            uint64_t cursor = address;

                            while (cursor < address + size) {
                                const Region* region = state_.regions.find(
                                    request.senderPid, cursor);

                                if (!region || region->backing != identity) {
                                    break;
                                }

                                cursor = region->address + region->size;
                            }

                            if (cursor < address + size) {
                                reply.data[0] = static_cast<int64_t>(
                                    os::ErrorCode::NotFound);
                                break;
                            }

                            reply.data[0] =
                                operation == services::MmOperation::ProtectOwned
                                    ? protect(request.senderPid, address, size,
                                              request.data[3])
                                    : 0;
                        }

                        break;
                    }

                    // 用管理状态计算任务的区域与驻留用量，同时向内核查询真实物理内存数据，区分虚拟长度与物理占用。
                    case services::MmOperation::Usage: {
                        services::wire::MemoryUsage usage{};
                        const int result = os::detail::MemoryQuery().usage(
                            usage, request.senderPid);
                        reply.data[0] = result;

                        if (!result) {
                            os::Runtime::copyBytes(&reply.data[1], &usage,
                                                   sizeof(usage));
                        }

                        break;
                    }

                    case services::MmOperation::Reclaim:
                        reply.data[0] = reclaim_.reclaim(4096);
                        break;
                    case services::MmOperation::Sync:
                        if (request.length == 2) {
                            reply.data[0] =
                                state_.regions.covered(request.senderPid,
                                                       request.data[0],
                                                       request.data[1])
                                    ? 0
                                    : -4;
                        }

                        break;

                    // 共享对象名称操作和地址映射分别管理，打开名称本身不会立刻建立用户页表映射。
                    case services::MmOperation::SharedOpen:
                    case services::MmOperation::SharedUnlink: {
                        if (request.length != 9) {
                            break;
                        }

                        const char* name =
                            reinterpret_cast<const char*>(&request.data[1]);
                        size_t length = 0;

                        while (length < 64 && name[length]) {
                            ++length;
                        }

                        if (length < 2 || length >= 64 || name[0] != '/') {
                            break;
                        }

                        if (operation == services::MmOperation::SharedUnlink) {
                            reply.data[0] =
                                os::detail::MemoryControl().unlinkSharedMemory(
                                    name);
                        } else {
                            const int64_t handle =
                                os::detail::MemoryControl().openSharedMemory(
                                    name, request.data[0]);
                            reply.data[0] = handle > 0 ? 0
                                            : handle   ? handle
                                                       : -4;

                            if (handle > 0) {
                                reply.handleCount = 1;
                                reply.handles[0] = handle;
                                reply.handleRights[0] =
                                    (request.data[0] & 3) == 0 ? 1 : 7;
                                reply.handleOptions[0] =
                                    services::ipcHandleRestrictRights;
                            }
                        }

                        break;
                    }

                    default:
                        break;
                }
            }
    };

}  // namespace mm

/*
 * 创建内存服务对象并进入主循环，退出码交给启动管理者判断服务是否正常结束。
 */
int main(int, char**) {
    static mm::MemoryManager manager;

    return manager.run();
}
