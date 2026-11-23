#include <thread>

#include <os/detail/client.h>
#include <os/runtime.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>

namespace std::detail {

    struct ThreadState {
            ThreadInvoker invoker;
            void* object;
            void* stack;
            uint64_t id;
            bool completed;
            bool detached;
            bool used;
    };

    constexpr uint32_t threadCapacity = 8;
    constexpr size_t threadStackSize  = 64 * 1024;
    static ThreadState states[threadCapacity]{};
    static bool stateLock;

    static void lockStates() {
        while (__atomic_test_and_set(&stateLock, __ATOMIC_ACQUIRE)) {
            os::yield();
        }
    }

    static void unlockStates() { __atomic_clear(&stateLock, __ATOMIC_RELEASE); }

    static void run(void* argument) {
        ThreadState* state = static_cast<ThreadState*>(argument);
        state->invoker(state->object);
        __atomic_store_n(&state->completed, true, __ATOMIC_RELEASE);
        os::exitThread(0);
    }

    static void releaseState(ThreadState& state) {
        os::Runtime::current().freeMemory(state.stack);
        state = {};
    }

    static void reapDetachedThreads() {
        for (ThreadState& state : states) {
            int exitCode = 0;
            if (state.used && state.detached && state.id != 0 &&
                os::detail::TaskControl().waitThread(state.id, exitCode,
                                                     true) == 0) {
                releaseState(state);
            }
        }
    }

    ThreadState* createStandardThread(ThreadInvoker invoker, void* object) {
        if (invoker == nullptr) {
            return nullptr;
        }
        lockStates();
        reapDetachedThreads();
        for (uint32_t index = 0; index < threadCapacity; ++index) {
            ThreadState& state = states[index];
            if (state.used) {
                continue;
            }
            state       = {};
            state.stack = os::Runtime::current().allocateMemory(threadStackSize);
            if (state.stack == nullptr) {
                unlockStates();
                return nullptr;
            }
            state.invoker = invoker;
            state.object  = object;
            state.used    = true;
            state.id      = os::createThread(
                run, reinterpret_cast<uint64_t>(state.stack) + threadStackSize,
                &state);
            if (state.id == 0) {
                os::Runtime::current().freeMemory(state.stack);
                state = {};
                unlockStates();
                return nullptr;
            }
            unlockStates();
            return &state;
        }
        unlockStates();
        return nullptr;
    }

    void joinStandardThread(ThreadState* state) {
        if (state == nullptr) {
            return;
        }
        int exitCode = 0;
        os::detail::TaskControl().waitThread(state->id, exitCode);
        lockStates();
        releaseState(*state);
        unlockStates();
    }

    void detachStandardThread(ThreadState* state) {
        if (state != nullptr) {
            state->detached = true;
        }
    }

    uint64_t getStandardThreadId(const ThreadState* state) {
        return state == nullptr ? 0 : state->id;
    }

}  // namespace std::detail
