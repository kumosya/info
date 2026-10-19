#include "os/abi.h"
#include "os/task.h"
#include "os/mm.h"
#include "os/file.h"
#include "os/service.h"
#include "os/core_service.h"
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

class MemoryRuntimeTest {
    inline static volatile sig_atomic_t signalReceived_ = 0;
    static void signalHandler(int) { signalReceived_ = 1; }
    static bool check(bool value, const char* name) {
        if (!value) { os::System::log("[MM TEST] FAIL: "); os::System::log(name); os::System::log("\n"); }
        return value;
    }
    static bool services() {
        const uint64_t registry = os::ServiceRegistry::serviceRegistry();
        const uint64_t endpoint = os::Endpoint::create();
        if (!check(endpoint && !os::ServiceRegistry::registerService(registry, "memory.kernel", endpoint), "reject protected service name")) return false;
        uint64_t generation = 0, observed = 0;
        if (!check(os::ServiceRegistry::registerService(registry, "mm-test-service", endpoint, 0, &generation), "register test service")) return false;
        const uint64_t client = os::ServiceRegistry::lookupService(registry, "mm-test-service", 1, &observed);
        os::IpcMessage message{};
        if (!check(client && observed == generation && os::receiveMessage(client, message, 0) < 0, "publish only client rights")) return false;
        if (!check(os::callService(client, message, message, os::Clock::getMonotonicTime() + 2000000) == static_cast<int>(os::ErrorCode::Timeout), "RPC deadline")) return false;
        if (!check(os::receiveMessage(endpoint, message, 0) == static_cast<int>(os::ErrorCode::WouldBlock), "timeout removes unhandled request")) return false;
        if (!check(!os::ServiceRegistry::unregisterService(registry, "mm-test-service", generation + 1) &&
            os::ServiceRegistry::unregisterService(registry, "mm-test-service", generation), "unregister generation authorization")) return false;
        uint64_t nextGeneration = 0;
        if (!check(os::ServiceRegistry::registerService(registry, "mm-test-service", endpoint, 0, &nextGeneration) && nextGeneration > generation, "service re-registration generation")) return false;
        os::ServiceRegistry::unregisterService(registry, "mm-test-service", nextGeneration);
        os::closeHandle(client); os::closeHandle(endpoint);
        os::PipeHandles pipe{};
        if (!check(os::Pipe::createPipe(pipe), "service death synchronization pipe")) return false;
        const pid_t child = fork();
        if (!check(child >= 0, "fork service publisher")) return false;
        if (!child) {
            os::closeHandle(pipe.readHandle);
            const uint64_t owned = os::Endpoint::create();
            const char ready = owned && os::ServiceRegistry::registerService(registry, "mm-test-death", owned) ? 1 : 0;
            os::Pipe::writePipe(pipe.writeHandle, &ready, 1);
            if (!ready) _exit(40);
            for (;;) os::sleep(10);
        }
        os::closeHandle(pipe.writeHandle);
        char ready = 0;
        if (!check(os::Pipe::readPipe(pipe.readHandle, &ready, 1) == 1 && ready, "child service publication")) return false;
        os::closeHandle(pipe.readHandle);
        const uint64_t stale = os::ServiceRegistry::lookupService(registry, "mm-test-death", 1, &generation);
        int status = 0;
        if (!check(stale && kill(child, SIGKILL) == 0 && waitpid(child, &status, 0) == child, "service death and task reap")) return false;
        const int dead = os::callService(stale, message, message, os::Clock::getMonotonicTime() + 2000000);
        if (!check(dead == static_cast<int>(os::ErrorCode::PeerDied) || dead == static_cast<int>(os::ErrorCode::EndpointClosed), "stale service endpoint explicit failure")) return false;
        const uint64_t replacement = os::Endpoint::create();
        if (!check(os::ServiceRegistry::registerService(registry, "mm-test-death", replacement, 0, &nextGeneration) && nextGeneration > generation, "republish after owner death")) return false;
        const uint64_t fresh = os::ServiceRegistry::lookupService(registry, "mm-test-death", 1);
        message.type = 88;
        if (!check(fresh && os::sendMessage(fresh, message) == 0 && os::receiveMessage(replacement, message, 0) == 0 && message.type == 88, "reconnect to replacement endpoint")) return false;
        os::ServiceRegistry::unregisterService(registry, "mm-test-death", nextGeneration);
        os::closeHandle(stale); os::closeHandle(fresh); os::closeHandle(replacement);
        os::System::log("[MM TEST] service publication and RPC deadlines passed\n");
        return true;
    }
    static bool channels() {
        os::PipeHandles pipe{};
        if (!check(os::Pipe::createPipe(pipe), "Ring3 pipe creation")) return false;
        const char payload[] = "abc"; char output[8]{};
        if (!check(os::Pipe::writePipe(pipe.writeHandle, payload, 3) == 3 && os::Pipe::readPipe(pipe.readHandle, output, 3) == 3 && output[2] == 'c', "Ring3 pipe data")) return false;
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::PipeOperation::Read); request.length = 3;
        request.data[0] = pipe.readHandle; request.data[1] = 1;
        if (!check(os::CoreServiceClient::call(os::CoreServiceKind::Pipe, request, reply, os::Clock::getMonotonicTime() + 2000000) == static_cast<int>(os::ErrorCode::Timeout), "Ring3 pending read deadline")) return false;
        os::closeHandle(pipe.writeHandle);
        if (!check(os::Pipe::readPipe(pipe.readHandle, output, 1) == 0, "last writer close produces EOF")) return false;
        os::closeHandle(pipe.readHandle);
        os::ChannelHandles pair{};
        if (!check(os::Pipe::createChannelPair(pair) && os::Pipe::setChannelNonblocking(pair.firstHandle, true), "Ring3 channel creation")) return false;
        if (!check(os::Pipe::readPipe(pair.firstHandle, output, 1) == -6 && os::Pipe::writePipe(pair.secondHandle, payload, 1) == 1 && os::Pipe::readPipe(pair.firstHandle, output, 1) == 1, "Ring3 channel direction and nonblocking")) return false;
        if (!check(os::Pipe::signalChannelEof(pair.secondHandle) && os::Pipe::readPipe(pair.firstHandle, output, 1) == 0 && os::Pipe::readPipe(pair.firstHandle, output, 1) == -6, "channel one-shot EOF")) return false;
        os::closeHandle(pair.firstHandle); os::closeHandle(pair.secondHandle);
        const int64_t queue = os::MessageQueue::openMessageQueue("/mm-runtime-queue", O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK, 2, 8);
        if (!check(queue > 0, "Ring3 msgq creation")) return false;
        if (!check(os::MessageQueue::sendMessageQueue(queue, payload, 3, 1) == 0 && os::MessageQueue::sendMessageQueue(queue, payload, 3, 9) == 0 && os::MessageQueue::sendMessageQueue(queue, payload, 3, 2) == -6, "msgq capacity")) return false;
        uint32_t priority = 0;
        if (!check(os::MessageQueue::receiveMessageQueue(queue, output, 8, priority) == 3 && priority == 9, "msgq priority ordering")) return false;
        const int64_t readOnly = os::Handle::duplicate(queue, 1);
        if (!check(readOnly > 0 && os::MessageQueue::sendMessageQueue(readOnly, payload, 3, 1) == -3, "msgq handle rights")) return false;
        if (!check(os::MessageQueue::unlinkMessageQueue("/mm-runtime-queue") == 0 && os::MessageQueue::receiveMessageQueue(readOnly, output, 8, priority) == 3 && priority == 1, "unlinked msgq remains live")) return false;
        os::closeHandle(readOnly); os::closeHandle(queue);
        os::System::log("[MM TEST] Ring3 pipe and msgq passed\n");
        return true;
    }
   public:
    static bool afterExec() {
        os::MemoryUsage usage{};
        if (!check(os::Memory::usage(usage) == 0 && usage.generation > 1,
                   "exec advances address-space generation")) return false;
        auto* data = static_cast<volatile unsigned char*>(mmap(nullptr, 4096,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (!check(data != MAP_FAILED && data[0] == 0,
                   "fresh lazy mapping after exec")) return false;
        data[0] = 53;
        if (!check(data[0] == 53 && munmap(const_cast<unsigned char*>(data), 4096) == 0,
                   "post-exec MM metadata")) return false;
        os::System::log("[MM TEST] exec resets memory policy generation passed\n");
        return true;
    }
    static bool run() {
        auto* data = static_cast<volatile unsigned char*>(mmap(nullptr, 12288, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (!check(data != MAP_FAILED, "reserve lazy anonymous")) return false;
        if (!check(data[0] == 0 && data[8192] == 0, "zero-fill page faults")) return false;
        data[0] = 41; data[8192] = 73;
        if (!check(mprotect(const_cast<unsigned char*>(data), 4096, PROT_NONE) == 0 &&
                   mprotect(const_cast<unsigned char*>(data), 4096, PROT_READ | PROT_WRITE) == 0 && data[0] == 41, "PROT_NONE restore")) return false;
        if (!check(munmap(const_cast<unsigned char*>(data + 4096), 4096) == 0, "partial unmap lazy hole")) return false;
        if (!check(data[0] == 41 && data[8192] == 73, "split preserves neighbors")) return false;
        const pid_t child = fork();
        if (!check(child >= 0, "fork MM regions")) return false;
        if (!child) {
            if (data[0] != 41 || data[8192] != 73) _exit(31);
            data[0] = 99;
            _exit(data[0] == 99 ? 0 : 32);
        }
        int status = 0;
        if (!check(waitpid(child, &status, 0) == child && status == 0 && data[0] == 41, "COW child isolation")) return false;
        data[0] = 42;
        if (!check(data[0] == 42, "COW parent write")) return false;
        if (!check(munmap(const_cast<unsigned char*>(data), 12288) == 0, "unmap across split and hole")) return false;
        auto* buffer = static_cast<os::IpcMessage*>(mmap(nullptr, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        const uint64_t endpoint = os::Endpoint::create();
        os::IpcMessage sent{}; sent.type = 77;
        if (!check(buffer != MAP_FAILED && endpoint && os::sendMessage(endpoint, sent) == 0 &&
            os::receiveMessage(endpoint, *buffer) == 0 && buffer->type == 77, "syscall copy into lazy buffer")) return false;
        os::closeHandle(endpoint); munmap(buffer, 8192);

        const int shared = shm_open("/mm-runtime-test", O_CREAT | O_EXCL | O_RDWR, 0600);
        if (!check(shared >= 0 && ftruncate(shared, 4096) == 0, "shared object create")) return false;
        auto* first = static_cast<volatile unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shared, 0));
        auto* second = static_cast<volatile unsigned char*>(mmap(nullptr, 4096, PROT_READ, MAP_SHARED, shared, 0));
        if (!check(first != MAP_FAILED && second != MAP_FAILED, "shared object map")) return false;
        first[0] = 123;
        if (!check(second[0] == 123 && mprotect(const_cast<unsigned char*>(second), 4096, PROT_READ | PROT_WRITE) == 0, "shared visibility and permitted protect")) return false;
        close(shared); shm_unlink("/mm-runtime-test");
        if (!check(second[0] == 123, "shared lifetime after unlink and close")) return false;
        munmap(const_cast<unsigned char*>(first), 4096); munmap(const_cast<unsigned char*>(second), 4096);

        int file = open("/etc/hostname", O_RDONLY);
        if (!check(file >= 0, "open file backing")) return false;
        unsigned char expected = 0;
        if (!check(pread(file, &expected, 1, 0) == 1, "file reference read")) return false;
        auto* mapped = static_cast<volatile unsigned char*>(mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, file, 0));
        close(file);
        if (!check(mapped != MAP_FAILED && mapped[0] == expected, "delegated lazy file mapping after close")) return false;
        mapped[0] = expected ^ 0x7f;
        if (!check(msync(const_cast<unsigned char*>(mapped), 4096, MS_SYNC) == 0, "private sync")) return false;
        munmap(const_cast<unsigned char*>(mapped), 4096);

        if (!channels() || !services()) return false;

        struct sigaction action{}; action.sa_handler = signalHandler; sigemptyset(&action.sa_mask);
        if (!check(sigaction(SIGUSR1, &action, nullptr) == 0 && kill(getpid(), SIGUSR1) == 0 && signalReceived_, "signal return through task service")) return false;
        os::System::log("[MM TEST] anonymous, protection, partial unmap, COW, shared, file and signal passed\n");
        return true;
    }
};
int main(int argc, char** argv) {
    if (argc > 1 && argv[1][0] == 'e') {
        if (!MemoryRuntimeTest::afterExec()) return 1;
    } else {
        if (!MemoryRuntimeTest::run()) return 1;
        // Leave a region in MM so the new image must invalidate old policy.
        auto* old = static_cast<volatile unsigned char*>(mmap(nullptr, 4096,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (old == MAP_FAILED) return 3;
        old[0] = 91;
        const char* next[] = {"terminal", "exec-check", nullptr};
        execv("/usr/bin/terminal", const_cast<char* const*>(next));
        return 4;
    }
    const char* arguments[] = {"terminal", nullptr};
    execv("/usr/bin/terminal.real", const_cast<char* const*>(arguments));
    return 2;
}
