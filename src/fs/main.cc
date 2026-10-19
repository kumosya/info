#include "os/task.h"
#include "server.h"
#include <fcntl.h>
#include <errno.h>

namespace vfs::server {

bool FsServer::handle(const os::IpcMessage& request, os::IpcMessage& reply) {
    const auto operation = static_cast<os::FsRequest>(request.type);
    if (!permitted(request)) {
        if ((operation == os::FsRequest::ReadAtAsync || operation == os::FsRequest::WriteAtAsync) && request.handleCount == 1) {
            os::IpcMessage completion{};
            completion.type = os::fsCompletionNotification;
            completion.data[0] = request.data[3]; completion.data[1] = static_cast<uint64_t>(-3LL); completion.length = 3;
            os::sendMessage(request.handles[0], completion); os::closeHandle(request.handles[0]);
            return false;
        }
        reply.data[0] = operation == os::FsRequest::Open ? 0 :
            operation == os::FsRequest::Read || operation == os::FsRequest::Write ||
            operation == os::FsRequest::ReadAt || operation == os::FsRequest::WriteAt ? static_cast<uint64_t>(-3LL) : 1;
        reply.length = operation == os::FsRequest::Open ? 2 : 1;
        if (operation == os::FsRequest::Open) reply.data[1] = static_cast<uint64_t>(-EACCES);
        return true;
    }
    if (operation == os::FsRequest::ReadAtAsync ||
        operation == os::FsRequest::WriteAtAsync) {
        handleAsync(request, operation == os::FsRequest::WriteAtAsync);
        return false;
    }
    const char* path = reinterpret_cast<const char*>(request.data);
    switch (operation) {
        case os::FsRequest::Open: {
            const int flags = request.length >= 49 ? static_cast<int>(request.data[48]) : O_RDONLY;
            const uint16_t mode = request.length >= 50 ? request.data[49] & 07777 : 0666;
            os::IpcMessage status{}; stat(path,status);
            const bool exists = status.data[0] == 0;
            reply.length=2;
            if ((flags&O_ACCMODE) > O_RDWR) {
                reply.data[0]=0; reply.data[1]=static_cast<uint64_t>(-EINVAL); break;
            }
            if ((flags&O_ACCMODE) > O_RDWR || (!exists && !(flags&O_CREAT)) ||
                (exists && (flags&(O_CREAT|O_EXCL)) == (O_CREAT|O_EXCL)) ||
                (exists && (flags&O_DIRECTORY) && status.data[1] != static_cast<uint64_t>(os::FileType::Directory))) {
                reply.data[0]=0; reply.data[1]=static_cast<uint64_t>(!exists?-ENOENT:(flags&O_DIRECTORY)?-ENOTDIR:-EEXIST); break;
            }
            if (!exists) {
                if (!create(path,false)) { reply.data[0]=0; reply.data[1]=static_cast<uint64_t>(-EEXIST); break; }
                os::TaskInfo caller{};
                if (!os::Process::queryTask(request.senderPid, caller))
                    changeOwner(path,caller.effectiveUid,caller.effectiveGid);
                changeMode(path,mode);
            }
            if ((flags&O_TRUNC) && (flags&O_ACCMODE)!=O_RDONLY && truncate(path)) {
                reply.data[0]=0; reply.data[1]=static_cast<uint64_t>(-EIO); break;
            }
            reply.data[0] = open(request.senderPid, path, flags);
            reply.data[1] = reply.data[0] ? 0 : static_cast<uint64_t>(-EIO);
            break;
        }
        case os::FsRequest::Close:
            reply.data[0] = close(request.senderPid, request.data[0]);
            break;
        case os::FsRequest::Read:
            read(request.senderPid, request.data[0], request.data[1], reply);
            break;
        case os::FsRequest::Write:
            write(request.senderPid, request, reply);
            break;
        case os::FsRequest::Seek:
            seek(request.senderPid, request.data[0],
                 static_cast<int64_t>(request.data[1]), request.data[2], reply);
            break;
        case os::FsRequest::Stat:
            stat(path, reply);
            break;
        case os::FsRequest::Create:
        case os::FsRequest::MakeDirectory:
            reply.data[0] = create(path, operation == os::FsRequest::MakeDirectory) ? 0 : 1;
            if (!reply.data[0]) {
                os::TaskInfo caller{};
                if (!os::Process::queryTask(request.senderPid, caller))
                    changeOwner(path, caller.effectiveUid, caller.effectiveGid);
            }
            break;
        case os::FsRequest::Remove:
            reply.data[0] = remove(path) ? 0 : 1;
            break;
        case os::FsRequest::ReadDirectory:
            readDirectory(path, request.data[48], reply);
            break;
        case os::FsRequest::ReadDirectoryBatch:
            readDirectoryBatch(path, request.data[48], request.data[49], reply);
            break;
        case os::FsRequest::Truncate:
            reply.data[0] = truncate(path);
            break;
        case os::FsRequest::Link:
            reply.data[0] = link(path, path + 384) ? 0 : 1;
            break;
        case os::FsRequest::Rename:
            reply.data[0] = rename(path, path + 384) ? 0 : 1;
            break;
        case os::FsRequest::ChangeMode:
            reply.data[0] =
                changeMode(path, static_cast<uint16_t>(request.data[48])) ? 0
                                                                          : 1;
            break;
        case os::FsRequest::ChangeOwner:
            if (request.data[48] > 0xffffU || request.data[49] > 0xffffU) {
                reply.data[0] = 1;
            } else {
                reply.data[0] =
                    changeOwner(path, static_cast<uint16_t>(request.data[48]),
                                static_cast<uint16_t>(request.data[49]))
                        ? 0
                        : 1;
            }
            break;
        case os::FsRequest::ChangeTimes:
            if (request.data[48] > UINT32_MAX ||
                request.data[49] > UINT32_MAX) {
                reply.data[0] = 1;
            } else {
                reply.data[0] =
                    changeTimes(path, static_cast<uint32_t>(request.data[48]),
                                static_cast<uint32_t>(request.data[49]))
                        ? 0
                        : 1;
            }
            break;
        case os::FsRequest::CloneDescriptors:
            cloneDescriptors(request.data[0], request.senderPid, reply);
            break;
        case os::FsRequest::DelegateMapping:
            delegateMapping(request, reply);
            break;
        case os::FsRequest::ClaimMapping:
            claimMapping(request, reply);
            break;
        case os::FsRequest::CloneForChild:
            cloneDescriptors(request.senderPid, request.data[0], reply);
            break;
        case os::FsRequest::Fstat:
            descriptorStat(request.senderPid, request.data[0], reply);
            break;
        case os::FsRequest::ReadAt: case os::FsRequest::WriteAt:
            transferAt(request, reply, operation == os::FsRequest::WriteAt);
            break;
        case os::FsRequest::Mount:
            reply.data[0] = mount(request) ? 0 : 1;
            break;
        case os::FsRequest::Unmount:
            reply.data[0] = unmount(request) ? 0 : 1;
            break;
        case os::FsRequest::ListMounts:
            listMount(static_cast<uint32_t>(request.data[0]), reply);
            break;
        case os::FsRequest::Format:
            reply.data[0] = format(request) ? 0 : 1;
            break;
        default:
            reply.data[0] = 1;
            break;
    }
    if (reply.length == 0) reply.length = 1;
    return true;
}

bool FsServer::format(const os::IpcMessage&) const {
    // Formatting mounted or unverified devices is not supported.
    return false;
}

OpenFileDescription* FsServer::getDescriptor(uint64_t ownerPid,
                                             uint64_t descriptor) {
    FileDescriptor* entry = getDescriptorRecord(ownerPid, descriptor);
    return entry == nullptr ? nullptr : entry->description;
}

}  // namespace vfs::server

namespace vfs::server {

class FsApplication {
 public:
    static void runFsWorker(void* self) { static_cast<FsApplication*>(self)->workerLoop(); }
FsServer fsServer;
uint64_t fsEndpoint;

[[noreturn]] void workerLoop() {
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(fsEndpoint, request) != 0) continue;
        os::IpcMessage reply{};
        if (fsServer.handle(request, reply)) {
            os::replyMessage(fsEndpoint, request.replyToken, reply);
        }
    }
}


int run() {
    os::System::log("[FS] FS Server started in Ring 3\n");
    const uint64_t registry = os::getBootstrapHandle();
    fsEndpoint              = os::Endpoint::create();
    if (registry == 0 || fsEndpoint == 0 ||
        !os::ServiceRegistry::registerService(registry, os::fsServiceName, fsEndpoint)) {
        os::System::log("[FS] registration failed\n");
        return 1;
    }
    const uint64_t storage =
        os::ServiceRegistry::lookupService(registry, os::storageServiceName, 5000);
    const uint64_t device =
        os::ServiceRegistry::lookupService(registry, os::deviceServiceName, 5000);
    if (!fsServer.init(storage, device)) {
        os::System::log("[FS] fstab filesystem initialization failed\n");
        return 2;
    }
    if (os::mapMemory(workerStackAddress, workerStackSize * workerThreadCount,
                      1) != workerStackAddress) {
        os::System::log("[FS] worker stack allocation failed\n");
        return 3;
    }
    for (uint32_t index = 0; index < workerThreadCount; ++index) {
        const uint64_t stackTop =
            workerStackAddress + workerStackSize * (index + 1);
        if (os::createThread(runFsWorker, stackTop, this) == 0) {
            os::System::log("[FS] unable to start request worker\n");
            return 4;
        }
    }
    os::System::log("[FS] storage IPC probe complete\n");
    os::System::log("[FS] mounted filesystems configured in /etc/fstab\n");
    workerLoop();
}
};
}  // namespace vfs::server
int main(int, char**) { static vfs::server::FsApplication application; return application.run(); }
