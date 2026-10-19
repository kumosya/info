// Host concurrency tests for the actual kernel handle table and Endpoint code.
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "kernel/cpu.h"
#include "kernel/device.h"
#include "kernel/ipc.h"
#include "kernel/core_service.h"
#include "kernel/page.h"
#include "kernel/mm.h"
#include "kernel/task.h"
#include "kernel/thread.h"

namespace {
thread_local kernel::CpuLocalData localCpu;
thread_local kernel::Thread localThread{};
std::atomic<int> allocationBudget{-1};
std::atomic<int> liveAllocations{0};
std::atomic<uint64_t> nextId{1};
struct Event { std::condition_variable condition; bool pending = false; };
std::mutex eventLock;
std::unordered_map<kernel::Thread*, Event> events;
void enter(kernel::Task* task) {
    localThread.setTask(task);
    localThread.setId(nextId.fetch_add(1));
    localCpu.setCurrentTask(task);
    localCpu.setCurrentThread(&localThread);
}
constexpr int code(os::ErrorCode error) { return static_cast<int>(error); }
}

namespace kernel::heap {
void* kernelAllocate(size_t size) {
    int budget = allocationBudget.load();
    if (budget == 0) return nullptr;
    if (budget > 0) --allocationBudget;
    void* result = std::calloc(1, size);
    if (result) ++liveAllocations;
    return result;
}
void kernelFree(void* pointer) { if (pointer) --liveAllocations; std::free(pointer); }
}
namespace kernel::cpu {
CpuLocalData* getCurrent() { return &localCpu; }
}
namespace kernel::interrupt {
uint64_t disable() { return 0; }
void restore(uint64_t) {}
}
namespace kernel::memory {
uint64_t createAddressSpaceRoot() { return nextId.fetch_add(1) << 12; }
void destroyAddressSpaceRoot(uint64_t) {}
bool assignPhysicalPageOwner(uint64_t, uint64_t) { return true; }
}
namespace kernel::serial { void serialWrite(const char*) {} }
namespace kernel {
Thread* Thread::getCurrent() { return &localThread; }
void Thread::prepareWait() {
    std::lock_guard<std::mutex> guard(eventLock);
    events[getCurrent()].pending = false;
}
void Thread::blockCurrent() {
    if (!getCurrent()->waitError()) {
        std::unique_lock<std::mutex> guard(eventLock);
        auto& event = events[getCurrent()];
        event.condition.wait(guard, [&] { return event.pending || getCurrent()->waitError(); });
        event.pending = false;
    }
    if (getCurrent()->waitError()) getCurrent()->cancelWait();
}
void Thread::wake(Thread* thread) {
    std::lock_guard<std::mutex> guard(eventLock);
    auto& event = events[thread];
    event.pending = true;
    event.condition.notify_one();
}
void SharedMemory::closeHandleObject(void*) {}
void device::InterruptBinding::destroy() {}
void memory::PageObject::release() {}
void memory::PageMappings::releaseTask(Task&) {}
}
namespace kernel::time {
uint64_t getMonotonicTime() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

int main() {
    using namespace kernel;
    Task* server = Task::create("server");
    Task* client = Task::create("client");
    Task* receiver = Task::create("receiver");
    assert(server && client && receiver);
    enter(server);
    Endpoint::init();
    auto* endpoint = Endpoint::create();
    const auto service = server->handles().add(endpoint, HandleType::Endpoint, 7);
    assert(service);
    auto clientEndpoint = server->handles().copyTo(client->handles(), service);
    assert(client->handles().restrictRights(clientEndpoint, handleRightWrite));
    assert(!client->handles().restrictRights(clientEndpoint, 7));
    assert(!client->handles().duplicate(clientEndpoint, 7));
    auto receiverEndpoint = server->handles().copyTo(receiver->handles(), service);
    assert(receiver->handles().restrictRights(receiverEndpoint, handleRightRead));
    assert(server->handles().references(service) == 3);
    assert(server->handles().references(UINT64_MAX) == -2);

    // A later invalid handle cannot partially install or consume an earlier move.
    const auto original = client->handles().add(server, HandleType::Task, 3);
    os::IpcMessage bad{};
    bad.handleCount = 2;
    bad.handles[0] = original;
    bad.handles[1] = UINT64_MAX;
    bad.handleOptions[0] = os::ipcHandleMove;
    HandleTable::Transfer rejected;
    assert(!rejected.capture(client->handles(), bad));
    assert(client->handles().get(original, HandleType::Task, 3));

    // A failed reply-side reservation must preserve the source move handle.
    HandleTable destination;
    assert(destination.init(1));
    assert(destination.add(server, HandleType::Task, 1));
    bad.handleCount = 1;
    HandleTable::Transfer transaction;
    assert(transaction.capture(client->handles(), bad, false));
    allocationBudget = 0;
    assert(!transaction.install(destination, bad));
    allocationBudget = -1;
    assert(client->handles().get(original, HandleType::Task, 3));
    assert(transaction.install(destination, bad));
    assert(!client->handles().get(original, HandleType::Task, 0));
    destination.dispose();

    // Queue references survive closing the source and install into the actual
    // receiver, even when that receiver does not own the Endpoint.
    enter(client);
    const auto carried = client->handles().add(server, HandleType::Task, 3);
    os::IpcMessage message{};
    message.handleCount = 1;
    message.handles[0] = carried;
    message.handleOptions[0] = os::ipcHandleRestrictRights;
    message.handleRights[0] = 1;
    assert(endpoint->send(&message, 0) == 0);
    assert(client->handles().close(carried));
    enter(receiver);
    os::IpcMessage received{};
    assert(endpoint->receive(&received, 0) == 0);
    assert(receiver->handles().get(received.handles[0], HandleType::Task, 1) == server);
    assert(!receiver->handles().get(received.handles[0], HandleType::Task, 2));
    receiver->handles().close(received.handles[0]);

    // Full queues and failed authorization do not consume move handles.
    enter(client);
    os::IpcMessage empty{};
    for (int i = 0; i < 8; ++i) assert(endpoint->send(&empty, 0) == 0);
    message.handles[0] = client->handles().add(server, HandleType::Task, 1);
    message.handleOptions[0] = os::ipcHandleMove;
    assert(endpoint->send(&message, 0) == code(os::ErrorCode::WouldBlock));
    assert(client->handles().get(message.handles[0], HandleType::Task, 1));
    enter(server);
    for (int i = 0; i < 8; ++i) assert(endpoint->receive(&received, 0) == 0);

    std::atomic<bool> stopTimer{false};
    std::thread timer([&] {
        while (!stopTimer) {
            Endpoint::expireWaits(time::getMonotonicTime());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    // Exercise timeout and cancellation in an inline mechanism service.
    class ResidentReader final : public EndpointService {
       public:
        bool ready = false;
        std::atomic<unsigned> entered{0};
        int call(Task&, const IpcMessage&, IpcMessage& reply) override {
            ++entered;
            while (!ready && !Thread::getCurrent()->waitError()) {
                Thread::prepareWait();
                Thread::blockCurrent();
            }
            const int64_t result = ready ? 1 : Thread::getCurrent()->waitError();
            reply.data[0] = result;
            return result < 0 ? static_cast<int>(result) : 0;
        }
    } reader;
    enter(client);
    auto* resident = Endpoint::create(&reader);
    const auto residentHandle = client->handles().add(resident, HandleType::Endpoint, handleRightWrite);
    assert(residentHandle);
    assert(resident->call(&empty, &received, 0) == code(os::ErrorCode::WouldBlock));
    assert(resident->call(&empty, &received, time::getMonotonicTime() + 5000000) == code(os::ErrorCode::Timeout));
    const unsigned beforeCancel = reader.entered;
    std::atomic<uint64_t> residentThread{0};
    std::thread nativeCancelled([&] {
        enter(client);
        residentThread = Thread::getCurrent()->getId();
        IpcMessage request{}, reply{};
        assert(resident->call(&request, &reply) == code(os::ErrorCode::Cancelled));
    });
    while (reader.entered == beforeCancel) std::this_thread::yield();
    assert(resident->cancel(residentThread) == 0);
    nativeCancelled.join();
    reader.ready = true;
    assert(resident->call(&empty, &received, 0) == 0 && received.data[0] == 1);
    resident->destroy();
    assert(client->handles().close(residentHandle));
    enter(server);

    std::atomic<int> replies{0};
    auto caller = [&] {
        enter(client);
        os::IpcMessage request{}, response{};
        assert(endpoint->call(&request, &response, time::getMonotonicTime() + 1000000000) == 0);
        assert(response.data[0] == 42);
        ++replies;
    };
    std::thread first(caller), second(caller);
    os::IpcMessage one{}, two{}, response{};
    assert(endpoint->receive(&one) == 0);
    assert(endpoint->receive(&two) == 0);
    assert(one.replyToken && one.replyToken != two.replyToken);
    assert(endpoint->requestActive(one.replyToken));
    response.replyToken = UINT64_MAX;
    assert(endpoint->reply(&response) < 0);
    response.data[0] = 42;
    response.replyToken = two.replyToken;
    assert(endpoint->reply(&response) == 0);
    assert(!endpoint->requestActive(two.replyToken));
    assert(endpoint->reply(&response) < 0);
    response.replyToken = one.replyToken;
    assert(endpoint->reply(&response) == 0);
    first.join(); second.join();
    assert(replies == 2);

    enter(client);
    assert(endpoint->call(&empty, &received, time::getMonotonicTime() + 5000000) == code(os::ErrorCode::Timeout));
    enter(server);
    assert(endpoint->receive(&received, 0) == code(os::ErrorCode::WouldBlock));
    std::atomic<uint64_t> waitingId{0};
    std::thread cancelled([&] {
        enter(client);
        waitingId = Thread::getCurrent()->getId();
        os::IpcMessage request{}, reply{};
        assert(endpoint->call(&request, &reply) == code(os::ErrorCode::Cancelled));
    });
    assert(endpoint->receive(&received) == 0);
    enter(client);
    assert(endpoint->cancel(waitingId) == 0);
    cancelled.join();
    enter(server);
    response.replyToken = received.replyToken;
    assert(endpoint->reply(&response) < 0);

    std::thread closed([&] {
        enter(client);
        os::IpcMessage request{}, reply{};
        assert(endpoint->call(&request, &reply) == code(os::ErrorCode::EndpointClosed));
    });
    assert(endpoint->receive(&received) == 0);
    endpoint->destroy();
    closed.join();
    stopTimer = true;
    timer.join();
    std::puts("kernel IPC: transaction, rights, receiver identity, queue, token, timeout, cancellation, resident waits and close passed");
}
