/*
 * src/mm/recovery.cc
 * 保存内存管理快照和请求结果，使服务重启后可以接管原映射。
 */

#include <os/detail/client.h>
#include <os/runtime.h>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/mm/mm.h"

namespace mm {

    /*
     * 为请求内容计算摘要，用于判断重试是否真的是同一次操作，避免把相同编号的不同请求当成旧结果。
     */
    uint64_t Recovery::digest(const os::IpcMessage& request) {
        uint64_t value =
            request.type ^ (static_cast<uint64_t>(request.length) << 32);

        for (uint32_t i = 0; i < request.length; ++i) {
            value = (value ^ request.data[i]) * 1099511628211ULL;
        }

        return value;
    }

    /*
     * 清理已经不需要保留的请求结果，释放结果记录所占容量。
     */
    void Recovery::collectOutcomes() {
        for (Outcome& outcome : outcomes_) {
            if (outcome.task) {
                services::wire::MemoryUsage usage{};
                bool dead =
                    os::detail::MemoryQuery().usage(usage, outcome.task) ||
                    usage.generation != outcome.generation;

                if (!dead && (outcome.id >> 63)) {
                    services::wire::TaskInfo child{};
                    dead = os::detail::TaskQuery().query(
                               outcome.id & ~(1ULL << 63), child) ||
                           child.state != 0;
                }

                if (dead) {
                    if (outcome.handle) {
                        os::closeHandle(outcome.handle);
                    }

                    outcome = {};
                }
            }
        }
    }

    /*
     * 按调用任务、请求编号和摘要查找已提交结果，并重新安装回复所需的句柄，保证服务重启后的重试不会重复执行修改。
     */
    bool Recovery::lookup(const os::IpcMessage& request, uint64_t id,
                          os::IpcMessage& reply) {
        if (!id) {
            return false;
        }

        services::wire::MemoryUsage usage{};

        if (os::detail::MemoryQuery().usage(usage, request.senderPid)) {
            return false;
        }

        for (const Outcome& outcome : outcomes_) {
            if (outcome.task == request.senderPid &&
                outcome.generation == usage.generation && outcome.id == id) {
                // 同一编号却带不同内容不是合法重试，明确返回错误，不能把上一次结果套给另一条请求。
                if (outcome.digest != digest(request)) {
                    reply.data[0] = static_cast<uint64_t>(-1LL);

                    return true;
                }

                os::Runtime::copyBytes(reply.data, outcome.result,
                                       sizeof(outcome.result));
                reply.length = 8;

                // 保存的结果句柄仍属于服务本地，重试回复时需要再复制一份，不能直接把旧编号当客户端句柄。
                if (outcome.handle) {
                    const uint32_t rights = outcome.rights ? outcome.rights : 7;
                    os::Ipc::Handle handleOwner;
                    const int handleStatus =
                        os::Ipc::Handle(outcome.handle, false)
                            .duplicate(rights, 0, handleOwner);
                    const int64_t handle =
                        handleStatus
                            ? handleStatus
                            : static_cast<int64_t>(handleOwner.release());

                    if (handle <= 0) {
                        reply.data[0] = static_cast<uint64_t>(-5LL);

                        return true;
                    }

                    reply.handleCount = 1;
                    reply.handles[0] = handle;
                    reply.handleRights[0] = rights;
                    reply.handleOptions[0] = services::ipcHandleRestrictRights;
                }

                return true;
            }
        }

        return false;
    }

    /*
     * 保存一次请求的回复及其对象引用，供之后重试时返回相同结果。
     */
    bool Recovery::remember(const os::IpcMessage& request, uint64_t id,
                            const os::IpcMessage& reply) {
        if (!id) {
            return true;
        }

        // 优先复用该任务已有的普通结果槽，否则找空槽；fork
        // 的专用结果按独立规则保留。
        Outcome* target = nullptr;

        for (Outcome& outcome : outcomes_) {
            if (outcome.task == request.senderPid && !(outcome.id >> 63) &&
                !(id >> 63)) {
                target = &outcome;
                break;
            }

            if (!outcome.task && !target) {
                target = &outcome;
            }
        }

        if (!target) {
            return false;
        }

        services::wire::MemoryUsage usage{};

        if (os::detail::MemoryQuery().usage(usage, request.senderPid) ||
            usage.generation != state_.generation(request.senderPid)) {
            return true;
        }

        // 回复带对象时，先复制出服务自己持有的一份引用，原回复句柄发送后关闭也不影响结果重放。
        uint64_t handle = 0;

        if (reply.handleCount) {
            if (!reply.handleRights[0] || (reply.handleRights[0] & ~7U)) {
                return false;
            }

            os::Ipc::Handle duplicateOwner;
            const int duplicateStatus =
                os::Ipc::Handle(reply.handles[0], false)
                    .duplicate(reply.handleRights[0], 0, duplicateOwner);
            const int64_t duplicate =
                duplicateStatus
                    ? duplicateStatus
                    : static_cast<int64_t>(duplicateOwner.release());

            if (duplicate <= 0) {
                return false;
            }

            handle = duplicate;
        }

        // 新结果引用已经准备好，才关闭旧槽引用并替换，避免复制失败时把可用旧结果也丢掉。
        if (target->handle) {
            os::closeHandle(target->handle);
        }

        *target = {};
        target->task = request.senderPid;
        target->generation = usage.generation;
        target->id = id;
        target->digest = digest(request);
        target->handle = handle;
        target->rights = reply.handleCount ? reply.handleRights[0] : 0;
        os::Runtime::copyBytes(target->result, reply.data,
                               sizeof(target->result));

        return true;
    }

    /*
     * 在执行修改前准备检查点，必要时冻结涉及的任务。
     * 这样服务中途崩溃时，任务不会继续使用一半更新的映射状态。
     */
    bool Recovery::prepare(const os::IpcMessage& request, uint64_t id) {
        // 先收集已退出任务的旧结果，确认这次请求有地方保存最终回复，再开始修改映射。
        collectOutcomes();
        bool capacity = !id;

        for (const Outcome& outcome : outcomes_) {
            if (!outcome.task || (outcome.task == request.senderPid &&
                                  !(outcome.id >> 63) && !(id >> 63))) {
                capacity = true;
            }
        }

        if (!capacity || checkpoint_.prepare()) {
            return false;
        }

        const services::MmOperation operation =
            static_cast<services::MmOperation>(request.type);
        const bool changesMappings =
            operation != services::MmOperation::Usage &&
            operation != services::MmOperation::Sync &&
            operation != services::MmOperation::SharedOpen &&
            operation != services::MmOperation::SharedUnlink &&
            operation != services::MmOperation::SyncOwned;

        // 只读查询或不改地址映射的操作无需冻结客户进程，减少无意义的暂停。
        if (!changesMappings) {
            return true;
        }

        os::IpcMessage reply{};

        struct TaskFreezer {
                os::IpcMessage& reply;

                /* 已经退出的任务无需冻结；其他错误必须中止检查点准备。 */
                bool operator()(uint64_t task) const {
                    if (!task) {
                        return true;
                    }

                    const int result = KernelMemory().control(
                        services::MemoryOperation::Freeze, task, 0, reply);

                    return !result ||
                           result == static_cast<int>(os::ErrorCode::NotFound);
                }
        } freeze{reply};

        // 改映射前冻结所有相关任务，服务若在修改途中退出，它们也不会继续观察半提交状态。
        for (size_t i = 0; i < 128; ++i) {
            if (!freeze(state_.domainAt(i))) {
                return false;
            }
        }

        if (!freeze(request.senderPid)) {
            return false;
        }

        // fork 还涉及尚未运行的孩子，需要把孩子一起纳入冻结范围。
        return operation != services::MmOperation::Fork ||
               freeze(request.data[0]);
    }

    /*
     * 把区域、页面、后备对象和请求结果写入检查点，并登记需要跨服务重启保留的句柄引用。
     */
    bool Recovery::save() {
        capabilities_.begin();

        // 恢复快照复制的是管理状态，运行时句柄还要转换成可在新实例中重新领取的令牌。
        snapshot_.state = state_;
        snapshot_.endpoint = capabilities_.encode(endpoint_);

        if (!snapshot_.endpoint) {
            return false;
        }

        // 在快照副本里把服务本地页句柄转换成恢复令牌，运行中的 state_
        // 仍保留可直接使用的原句柄。
        for (ResidentPage& page : snapshot_.state.pages) {
            if (page.used && page.handle) {
                page.handle = capabilities_.encode(page.handle);

                if (!page.handle) {
                    return false;
                }
            }
        }

        for (Backing& backing : snapshot_.state.backings) {
            if (!backing.used) {
                continue;
            }

            // 无人引用的后备记录不写进新快照，避免恢复时把已经结束的文件或共享对象重新救活。
            if (!state_.references(backing.id)) {
                backing = {};
                continue;
            }

            if (backing.handle) {
                backing.handle = capabilities_.encode(backing.handle);

                if (!backing.handle) {
                    return false;
                }
            }

            // 文件服务连接不能作为裸句柄跨实例照搬，恢复时会根据租用关系重新建立。
            backing.fs = 0;
        }

        for (size_t i = 0; i < outcomeCapacity; ++i) {
            snapshot_.outcomes[i] = outcomes_[i];

            if (snapshot_.outcomes[i].handle) {
                snapshot_.outcomes[i].handle =
                    capabilities_.encode(snapshot_.outcomes[i].handle);

                if (!snapshot_.outcomes[i].handle) {
                    return false;
                }
            }
        }

        // 快照和代管引用全部准备好才提交，提交成功后再清理不再需要的对象引用。
        if (checkpoint_.commit(&snapshot_) || !capabilities_.collect()) {
            return false;
        }

        state_.collectBackings(true);

        return true;
    }

    /*
     * 验证恢复快照的版本、数量和相互引用，拒绝损坏或不兼容的状态。
     */
    bool Recovery::validate() const {
        for (size_t i = 0; i < RegionTable::capacity; ++i) {
            const Region& region = state_.regions.at(i);

            if (!region.used) {
                continue;
            }

            if (!RegionTable::validRange(region.address, region.size) ||
                !state_.generation(region.task) || region.protection > 7 ||
                region.maximumProtection > 7 ||
                (region.protection & ~region.maximumProtection) ||
                (region.protection & 6) == 6 ||
                region.kind > BackingKind::Shared) {
                return false;
            }

            // 恢复数据也不能假定可信，除了单条范围有效，还要检查同一任务区域之间没有互相覆盖。
            for (size_t n = 0; n < i; ++n) {
                const Region& other = state_.regions.at(n);

                if (other.used && other.task == region.task &&
                    other.address < region.address + region.size &&
                    region.address < other.address + other.size) {
                    return false;
                }
            }
        }

        // 每页必须能找到对应区域并引用同一后备对象，崩溃时仍在读写中的页面也不能直接恢复为完成状态。
        for (const ResidentPage& page : state_.pages) {
            if (page.used) {
                const Region* region =
                    state_.regions.find(page.task, page.address);

                if (!region || page.address & 4095 ||
                    page.backing != region->backing || page.io) {
                    return false;
                }
            }
        }

        return true;
    }

    /*
     * 从冻结快照恢复服务状态，重新取得代管句柄，并剔除已不存在或已更换映像的任务记录。
     */
    bool Recovery::restoreFrozen() {
        uint64_t cursor = 1;

        while (true) {
            os::IpcMessage reply{};

            if (KernelMemory().control(services::MemoryOperation::FrozenNext,
                                       cursor, 0, reply)) {
                return false;
            }

            const uint64_t task = reply.data[1];

            if (!task) {
                return true;
            }

            // 按 PID 递增枚举冻结任务，不依赖可能随服务恢复变化的链表下标。
            cursor = task + 1;
            // 被冻结任务尚未运行，因此不会观察到未提交的页表变化。
            while (true) {
                if (KernelMemory().control(
                        services::MemoryOperation::MappingNext, task, 0,
                        reply)) {
                    return false;
                }

                if (!reply.data[1]) {
                    break;
                }

                if (os::detail::MemoryControl().unmapTarget(
                        reply.data[1], reply.data[2], task)) {
                    return false;
                }
            }

            // 旧实例可能只改了一半页表，先撤销受管理旧映射，再按已提交的页面记录重建。
            for (ResidentPage& page : state_.pages) {
                if (page.used && page.task == task && page.mapped) {
                    const Region* region =
                        state_.regions.find(task, page.address);
                    const Backing* backing = state_.backing(page.backing);

                    if (!region || !backing || !region->protection) {
                        return false;
                    }

                    // 写时复制页面重建时仍然禁止直接写入，并恢复专用标记，不能因服务重启失去私有复制保护。
                    const uint64_t protection =
                        KernelMemory().nativeProtection(region->protection) &
                        (page.cow ? ~1ULL : ~0ULL);
                    const int result =
                        backing->kind == BackingKind::Shared
                            ? KernelMemory().mapShared(
                                  backing->handle, task, page.address, 4096,
                                  region->offset + page.address -
                                      region->address,
                                  protection)
                        : os::detail::MemoryControl().mapPages(
                              page.handle, page.address, 4096, 0, protection,
                              task) == page.address
                            ? 0
                            : -5;

                    if (result ||
                        (page.cow && os::detail::MemoryControl().protectTarget(
                                         page.address, 4096, 4, task))) {
                        return false;
                    }
                }
            }
        }
    }

    /*
     * 提交当前恢复阶段并恢复允许继续执行的任务，使新状态开始对运行中的进程生效。
     */
    bool Recovery::activate() {
        uint64_t cursor = 1;

        while (true) {
            os::IpcMessage reply{};

            if (KernelMemory().control(services::MemoryOperation::FrozenNext,
                                       cursor, 0, reply)) {
                return false;
            }

            const uint64_t task = reply.data[1];

            if (!task) {
                return true;
            }

            // 先推进枚举游标，再恢复这个任务；下轮即使任务退出或链表变化，也不会重复从原位置开始。
            cursor = task + 1;
            const int result = KernelMemory().control(
                services::MemoryOperation::Thaw, task, 0, reply);

            // 枚举与恢复之间目标可能已退出，NotFound
            // 可以忽略，其他错误则不能假装任务已经恢复。
            if (result && result != static_cast<int>(os::ErrorCode::NotFound)) {
                return false;
            }
        }
    }

    /*
     * 重新收集文件映射租用关系，确保恢复后的文件后备对象仍然得到文件服务支持。
     */
    bool Recovery::collectFileLeases() {
        const uint64_t fs =
            os::detail::RegistryClient(os::detail::RegistryClient().endpoint())
                .lookup(services::fsServiceName, 1000);

        if (!fs) {
            return false;
        }

        bool ok = true;
        uint64_t cursor = 0;

        while (true) {
            os::IpcMessage request{}, reply{};
            request.type =
                static_cast<uint64_t>(services::FsRequest::ListMappings);
            request.length = 1;
            request.data[0] = cursor;

            if (os::callService(fs, request, reply) ||
                static_cast<int64_t>(reply.data[0])) {
                ok = false;
                break;
            }

            if (!reply.data[1]) {
                break;
            }

            cursor = reply.data[4];

            // 用文件服务枚举的租用编号对照恢复后的后备表，只有仍被有效映射引用的租用才继续保留。
            bool used = false;

            for (const Backing& backing : state_.backings) {
                if (backing.used && backing.lease == reply.data[1]) {
                    used = true;
                }
            }

            // 旧实例留下却不在已提交快照中的租用应归还，否则文件后备会一直以为还有内存映射使用它。
            if (!used) {
                request.type =
                    static_cast<uint64_t>(services::FsRequest::ReleaseMapping);
                request.data[0] = reply.data[1];

                if (os::callService(fs, request, reply) ||
                    static_cast<int64_t>(reply.data[0])) {
                    ok = false;
                    break;
                }
            }
        }

        os::closeHandle(fs);

        return ok;
    }

    /*
     * 打开已有恢复端点或创建新端点，区分首次启动与重启恢复，再建立可以接收请求的状态。
     */
    bool Recovery::open(uint64_t& endpoint) {
        const int restored = checkpoint_.open(&snapshot_, sizeof(snapshot_),
                                              0x4d4d303030303031ULL);

        if (restored < 0) {
            return false;
        }

        capabilities_.begin();

        // 重启路径先装入旧状态并剔除过期任务，再校验剩余记录；不能直接把快照当新实例可用状态。
        if (restored) {
            state_ = snapshot_.state;
            state_.pruneRecovered();

            if (!validate()) {
                return false;
            }

            // 对象在新实例中可能得到不同本地编号，端点和页句柄都要从令牌重新领取。
            endpoint_ = capabilities_.decode(snapshot_.endpoint);

            for (ResidentPage& page : state_.pages) {
                if (page.used && page.handle) {
                    page.handle = capabilities_.decode(page.handle);

                    if (!page.handle) {
                        return false;
                    }
                }
            }

            // 后备句柄从代管表领取，文件相关资源还需按已保存租用信息重连文件服务。
            for (Backing& backing : state_.backings) {
                if (backing.used) {
                    if (backing.handle) {
                        backing.handle = capabilities_.decode(backing.handle);

                        if (!backing.handle) {
                            return false;
                        }
                    }

                    if (backing.lease) {
                        backing.fs =
                            os::detail::RegistryClient(
                                os::detail::RegistryClient().endpoint())
                                .lookup(services::fsServiceName, 1000);

                        if (!backing.fs) {
                            return false;
                        }
                    }
                }
            }

            for (size_t i = 0; i < outcomeCapacity; ++i) {
                const Outcome& saved = snapshot_.outcomes[i];
                services::wire::MemoryUsage usage{};

                if (!saved.task ||
                    os::detail::MemoryQuery().usage(usage, saved.task) ||
                    usage.generation != saved.generation) {
                    continue;
                }

                outcomes_[i] = saved;

                if (saved.handle) {
                    outcomes_[i].handle = capabilities_.decode(saved.handle);

                    if (!outcomes_[i].handle) {
                        return false;
                    }
                }
            }
        } else {
            // 首次启动没有旧快照，创建新端点并保存初始状态；重启则接管已有端点，客户端无需更换对象身份。
            endpoint_ = os::detail::EndpointControl().create();
        }

        if (!endpoint_ || !restoreFrozen() || checkpoint_.prepare() ||
            !save() || !capabilities_.discardOrphans() ||
            !collectFileLeases()) {
            return false;
        }

        endpoint = endpoint_;
        os::Sys().log(restored ? "[MM] VMA and backing checkpoint restored\n"
                               : "[MM] recovery domain initialized\n");

        return true;
    }

}  // namespace mm
