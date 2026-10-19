#pragma once

// Compiled only into the isolated lifecycle-test kernel.
namespace kernel {
class TaskLifecycleTest {
    struct Scenario {
        Task* target = nullptr;
        uint64_t newRoot = 0;
        uint32_t ready = 0;
        uint32_t release = 0;
        uint32_t completed = 0;
    };
    static constexpr uint64_t address = 0x40000000;
    static constexpr uint64_t sentinel = 0x12345678;

    static void require(bool value, const char* message) {
        if (value) return;
        serial::serialWrite("[TASK TEST] FAIL: ");
        serial::serialWrite(message);
        serial::serialWrite("\n");
        cpu::halt();
    }
    template <typename Predicate> static void await(Predicate ready) {
        const uint64_t deadline = time::getMonotonicTime() + 5000000000ULL;
        while (!ready()) {
            require(time::getMonotonicTime() < deadline, "wait deadline");
            schedule::yieldThread();
        }
    }
    static Thread* launch(Task* task, ThreadEntry entry, Scenario& scenario,
                          uint32_t cpuId) {
        Thread* thread = Thread::create(task, entry, &scenario,
                                         ThreadType::Kernel, false);
        require(thread != nullptr, "remote thread");
        // Publish the affinity before the first enqueue; a fast remote CPU can
        // already be running it when enqueueThread returns.
        thread->setCpuAffinity(1ULL << cpuId);
        thread->setCpuId(cpuId);
        schedule::enqueueThread(thread);
        return thread;
    }
    static void reap(Task* task) {
        const uint64_t id = task->id();
        task->destroy();
        int status = 0;
        uint64_t child = 0;
        require(task::getCurrent()->waitForChild(id, Thread::getCurrent(),
                    status, 0, child) == 0 && child == id, "task reap");
        require(task::find(id) == nullptr, "remove reaped task");
    }
    static void pinned(void* argument) {
        require(cpu::getCurrentId() == 1, "pinned target CPU");
        auto& scenario = *static_cast<Scenario*>(argument);
        cpu::disablePreemption();
        __atomic_store_n(&scenario.ready, 1U, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&scenario.release, __ATOMIC_ACQUIRE))
            asm volatile("pause" ::: "memory");
        cpu::enablePreemption();
    }
    static void requester(void* argument) {
        require(cpu::getCurrentId() == 2, "cleanup requester CPU");
        auto& scenario = *static_cast<Scenario*>(argument);
        scenario.target->destroy();
        __atomic_store_n(&scenario.completed, 1U, __ATOMIC_RELEASE);
    }
    static void interruptReclaimer() {
        for (uint32_t iteration = 0; iteration < 32; ++iteration) {
            Task* target = Task::create("reclaim-target", task::getCurrent());
            Task* caller = Task::create("reclaim-caller", task::getCurrent());
            require(target && caller, "reclaim tasks");
            Scenario busy{}, request{};
            request.target = target;
            launch(target, pinned, busy, 1);
            await([&] { return __atomic_load_n(&busy.ready, __ATOMIC_ACQUIRE); });
            launch(caller, requester, request, 2);
            // Reclamation is now waiting for CPU 1 to leave the target task.
            await([&] { return target->isTerminated(); });
            caller->terminateOtherThreads(-9);
            __atomic_store_n(&busy.release, 1U, __ATOMIC_RELEASE);
            reap(caller);
            require(!__atomic_load_n(&request.completed, __ATOMIC_ACQUIRE),
                    "requester stopped before cleanup completed");
            reap(target);
        }
        serial::serialWrite("[TASK TEST] killed cleanup requester passed (32 rounds)\n");
    }
    static void oldAddressSpace(void* argument) {
        require(cpu::getCurrentId() == 1, "old address-space CPU");
        auto& scenario = *static_cast<Scenario*>(argument);
        cpu::disablePreemption();
        auto* value = reinterpret_cast<volatile uint64_t*>(address);
        __atomic_store_n(&scenario.ready, 1U, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&scenario.release, __ATOMIC_ACQUIRE)) {
            *value = *value + 1;
            asm volatile("pause" ::: "memory");
        }
        cpu::enablePreemption();
    }
    static void exec(void* argument) {
        require(cpu::getCurrentId() == 2, "exec CPU");
        auto& scenario = *static_cast<Scenario*>(argument);
        require(scenario.target->commitExec(scenario.newRoot, "exec-committed",
                                             Thread::getCurrent()), "exec commit");
        require(*reinterpret_cast<volatile uint64_t*>(address) == sentinel,
                "new CR3 contents");
        require(scenario.target->addressSpace().generation() == 2,
                "exec address-space generation");
        __atomic_store_n(&scenario.completed, 1U, __ATOMIC_RELEASE);
    }
    static void execAndKill() {
        for (uint32_t iteration = 0; iteration < 64; ++iteration) {
            Task* target = Task::create("exec-target", task::getCurrent());
            require(target != nullptr, "exec target");
            const uint64_t root = memory::createAddressSpaceRoot();
            require(root && memory::allocateUserPageInAddressSpace(root, address,
                        pageUser | pageWritable | pageNoExecute) == UserPageMapResult::Success &&
                    memory::allocateUserPageInAddressSpace(
                        target->addressSpace().rootPhysicalAddress(), address,
                        pageUser | pageWritable | pageNoExecute) == UserPageMapResult::Success,
                    "exec page allocation");
            require(memory::copyToAddressSpace(root, address, &sentinel, sizeof(sentinel)),
                    "new root contents");
            Scenario busy{}, commit{};
            commit.target = target;
            commit.newRoot = root;
            Thread* sibling = launch(target, oldAddressSpace, busy, 1);
            await([&] { return __atomic_load_n(&busy.ready, __ATOMIC_ACQUIRE); });
            launch(target, exec, commit, 2);
            // exec has detached the sibling and is waiting for its actual CR3
            // switch. Alternate ordinary exec with a kill at this exact point.
            await([&] { return sibling->getState() == ThreadState::Terminated; });
            if (iteration & 1U) target->terminateOtherThreads(-9);
            __atomic_store_n(&busy.release, 1U, __ATOMIC_RELEASE);
            if (!(iteration & 1U))
                await([&] { return __atomic_load_n(&commit.completed, __ATOMIC_ACQUIRE); });
            reap(target);
        }
        serial::serialWrite("[TASK TEST] remote CR3 exec and concurrent kill passed (64 rounds)\n");
    }
   public:
    static void run() {
        require(smp::getOnlineCpuCount() >= 3, "requires at least three CPUs");
        interruptReclaimer();
        execAndKill();
    }
};
}  // namespace kernel
