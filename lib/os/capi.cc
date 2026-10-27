#include <new>

#include "service/common/channel.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include <os/capi.h>
#include <os/console.h>
#include <os/dir.h>
#include <os/file.h>
#include <os/mm.h>
#include <os/pty.h>
#include <os/task.h>
namespace {
using namespace os;
constexpr int invalid = static_cast<int>(ErrorCode::InvalidArgument),
              io      = static_cast<int>(ErrorCode::IoError);
class CapiEncoding {
   public:
    int rawOutput(int64_t value, uint64_t* result);
    int rawCall(uint64_t service, IpcMessage& request, IpcMessage& reply);
    int fsResult(uint64_t service, IpcMessage& request, IpcMessage& reply);
    bool pathRequest(IpcMessage& request, FsRequest type, const char* path);
    int fileSimple(uint64_t fs, FsRequest type, uint64_t handle, uint64_t a = 0,
                   uint64_t b = 0);
    int decodeStat(const IpcMessage& reply, OsFileStat& out);
};
int CapiEncoding::rawOutput(int64_t value, uint64_t* result) {
    if (!result) return invalid;
    if (value < 0) return value;
    *result = value;
    return 0;
}
int CapiEncoding::rawCall(uint64_t service, IpcMessage& request,
                          IpcMessage& reply) {
    Ipc::Handle lookup;
    if (!service) {
        service = detail::RegistryClient().lookup("fs", 1000);
        lookup.adopt(service);
    }
    if (!service) return static_cast<int>(ErrorCode::NotFound);
    return callService(service, request, reply);
}
int CapiEncoding::fsResult(uint64_t service, IpcMessage& request,
                           IpcMessage& reply) {
    const int status = rawCall(service, request, reply);
    return status          ? status
           : !reply.length ? io
                           : static_cast<int>(reply.data[0]);
}
bool CapiEncoding::pathRequest(IpcMessage& request, FsRequest type,
                               const char* path) {
    char resolved[384];
    if (!Runtime::absolutePath(path, resolved, sizeof(resolved))) return false;
    request.type   = uint64_t(type);
    request.length = 48;
    Runtime::copyString(reinterpret_cast<char*>(request.data), resolved, 384);
    return true;
}
int CapiEncoding::fileSimple(uint64_t fs, FsRequest type, uint64_t handle,
                             uint64_t a, uint64_t b) {
    IpcMessage q{}, r{};
    q.type    = uint64_t(type);
    q.length  = (type == FsRequest::Close || type == FsRequest::Sync ||
                type == FsRequest::DirClose)
                    ? 1
                    : 3;
    q.data[0] = handle;
    q.data[1] = a;
    q.data[2] = b;
    return fsResult(fs, q, r);
}
int CapiEncoding::decodeStat(const IpcMessage& r, OsFileStat& out) {
    if (r.length < 13) return io;
    OsFileStat s{};
    s.type             = r.data[1];
    s.size             = r.data[2];
    s.mode             = r.data[3];
    s.inode            = r.data[4];
    s.links            = r.data[5];
    s.uid              = r.data[6];
    s.gid              = r.data[7];
    s.accessTime       = r.data[8];
    s.modificationTime = r.data[9];
    s.changeTime       = r.data[10];
    s.device           = r.data[11];
    s.specialDevice    = r.data[12];
    out                = s;
    return 0;
}
}  // namespace

using namespace os;

static_assert(sizeof(OsIpcMessage) == sizeof(IpcMessage));
static_assert(sizeof(OsTaskInfo) == sizeof(wire::TaskInfo));
extern "C" {
int osFileOpen(uint64_t fs, const char* path, int flags, uint32_t mode,
               uint64_t* handle) {
    if (!handle || !path || (flags & O_ACCMODE) > O_RDWR) return invalid;
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::Open, path)) return invalid;
    q.length         = 50;
    q.data[48]       = flags;
    q.data[49]       = mode;
    const int status = CapiEncoding().rawCall(fs, q, r);
    if (status) return status;
    if (r.length < 2) return io;
    if (!r.data[0]) return r.data[1] ? static_cast<int>(r.data[1]) : io;
    *handle = r.data[0];
    return 0;
}

int osFileClose(uint64_t fs, uint64_t handle) {
    return CapiEncoding().fileSimple(fs, FsRequest::Close, handle);
}

int64_t osFileRead(uint64_t fs, uint64_t handle, void* buffer, size_t size) {
    File file;
    const int status = file.borrow(fs, handle, nullptr, O_RDWR);
    if (status) return status;
    return file.read(buffer, size);
}

int64_t osFileWrite(uint64_t fs, uint64_t handle, const void* buffer,
                    size_t size) {
    File file;
    const int status = file.borrow(fs, handle, nullptr, O_RDWR);
    if (status) return status;
    return file.write(buffer, size);
}

int64_t osFileReadAt(uint64_t fs, uint64_t handle, void* buffer, size_t size,
                     uint64_t offset) {
    if (offset > INT64_MAX) return invalid;
    File file;
    const int status = file.borrow(fs, handle, nullptr, O_RDWR);
    if (status) return status;
    return file.pread(buffer, size, offset);
}

int64_t osFileWriteAt(uint64_t fs, uint64_t handle, const void* buffer,
                      size_t size, uint64_t offset) {
    if (offset > INT64_MAX) return invalid;
    File file;
    const int status = file.borrow(fs, handle, nullptr, O_RDWR);
    if (status) return status;
    return file.pwrite(buffer, size, offset);
}

int64_t osFileSeek(uint64_t fs, uint64_t handle, int64_t offset, int whence) {
    File file;
    const int status = file.borrow(fs, handle, nullptr, O_RDWR);
    if (status) return status;
    return file.seek(offset, whence);
}

int osFileTruncate(uint64_t fs, uint64_t handle, uint64_t length) {
    if (length > INT64_MAX) return invalid;
    File file;
    const int status = file.borrow(fs, handle, nullptr, O_RDWR);
    if (status) return status;
    return file.truncate(length);
}

int osFileFlush(uint64_t fs, uint64_t handle) {
    return CapiEncoding().fileSimple(fs, FsRequest::Sync, handle);
}

int osFileQuery(uint64_t fs, uint64_t handle, OsFileStat* result) {
    if (!result) return invalid;
    IpcMessage q{}, r{};
    q.type           = uint64_t(FsRequest::Fstat);
    q.length         = 1;
    q.data[0]        = handle;
    const int status = CapiEncoding().fsResult(fs, q, r);
    if (status) return status;
    return CapiEncoding().decodeStat(r, *result);
}

int osFileSetMode(uint64_t fs, uint64_t handle, uint32_t mode) {
    return CapiEncoding().fileSimple(fs, FsRequest::Fchmod, handle, mode, 0);
}

int osFileSetOwner(uint64_t fs, uint64_t handle, uint32_t uid, uint32_t gid) {
    return CapiEncoding().fileSimple(fs, FsRequest::Fchown, handle, uid, gid);
}

int osFileSetTimes(uint64_t fs, uint64_t handle, uint64_t accessTime,
                   uint64_t modificationTime) {
    return CapiEncoding().fileSimple(fs, FsRequest::Ftimes, handle, accessTime,
                                     modificationTime);
}

int osFileReadAsync(uint64_t fs, uint64_t handle, uint64_t offset, size_t size,
                    uint64_t completionEndpoint, uint64_t requestId) {
    if (!fs || !handle || !completionEndpoint || !size || size > 4088)
        return invalid;
    IpcMessage q{};
    q.type        = uint64_t(FsRequest::ReadAtAsync);
    q.length      = 4;
    q.data[0]     = handle;
    q.data[1]     = offset;
    q.data[2]     = size;
    q.data[3]     = requestId;
    q.handleCount = 1;
    q.handles[0]  = completionEndpoint;
    return sendMessage(fs, q);
}

int osFileWriteAsync(uint64_t fs, uint64_t handle, uint64_t offset,
                     const void* buffer, size_t size,
                     uint64_t completionEndpoint, uint64_t requestId) {
    if (!fs || !handle || !completionEndpoint || !size || size > 4088)
        return invalid;
    IpcMessage q{};
    q.type        = uint64_t(FsRequest::WriteAtAsync);
    q.length      = 4;
    q.data[0]     = handle;
    q.data[1]     = offset;
    q.data[2]     = size;
    q.data[3]     = requestId;
    q.handleCount = 1;
    q.handles[0]  = completionEndpoint;
    if (!buffer) return invalid;
    Runtime::copyBytes(&q.data[4], buffer, size);
    q.length += (size + 7) / 8;
    return sendMessage(fs, q);
}

int osFileWaitOperation(uint64_t completionEndpoint,
                        OsFileOperationResult* result, void* buffer,
                        size_t capacity, uint64_t deadline) {
    if (!result) return invalid;
    IpcMessage r{};
    const int status = receiveMessage(completionEndpoint, r, deadline);
    if (status) return status;
    Ipc::Handle cleanup[4];
    for (unsigned i = 0; i < r.handleCount && i < 4; ++i)
        cleanup[i].adopt(r.handles[i]);
    if (r.type != fsCompletionNotification || r.length < 3 || r.handleCount ||
        r.data[2] > 4088)
        return io;
    if (!r.data[1] && r.length > 3) {
        if (!buffer || r.data[2] > capacity ||
            r.length < 3 + (r.data[2] + 7) / 8)
            return invalid;
        Runtime::copyBytes(buffer, &r.data[3], r.data[2]);
    }
    *result = {r.data[0], static_cast<int64_t>(r.data[1]), r.data[2]};
    return 0;
}

int osFsQuery(const char* path, OsFileStat* result) {
    if (!result) return invalid;
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::Stat, path)) return invalid;
    const int status = CapiEncoding().fsResult(0, q, r);
    return status ? status : CapiEncoding().decodeStat(r, *result);
}

int osFsRemove(const char* path) {
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::RemoveFile, path))
        return invalid;
    return CapiEncoding().fsResult(0, q, r);
}

int osFsRename(const char* existing, const char* target) {
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::Rename, existing))
        return invalid;
    if (!target || Runtime::stringLength(target) >= 384) return invalid;
    Runtime::copyString(reinterpret_cast<char*>(q.data) + 384, target, 384);
    q.length = 96;
    return CapiEncoding().fsResult(0, q, r);
}

int osFsLink(const char* existing, const char* target) {
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::Link, existing))
        return invalid;
    if (!target || Runtime::stringLength(target) >= 384) return invalid;
    Runtime::copyString(reinterpret_cast<char*>(q.data) + 384, target, 384);
    q.length = 96;
    return CapiEncoding().fsResult(0, q, r);
}

int osFsFormat(const char* device, const char* type) {
    return Fs().format(device, type);
}

int osDirOpen(const char* path, uint64_t* handle) {
    if (!handle) return invalid;
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::DirOpen, path))
        return invalid;
    const int status = CapiEncoding().fsResult(0, q, r);
    if (status) return status;
    if (r.length < 2 || !r.data[1]) return io;
    *handle = r.data[1];
    return 0;
}

int osDirClose(uint64_t handle) {
    return CapiEncoding().fileSimple(0, FsRequest::DirClose, handle);
}

int osDirCreate(const char* path, uint32_t mode) {
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::DirCreate, path))
        return invalid;
    q.length   = 49;
    q.data[48] = mode;
    return CapiEncoding().fsResult(0, q, r);
}

int osDirRemove(const char* path) {
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::DirRemove, path))
        return invalid;
    return CapiEncoding().fsResult(0, q, r);
}

int osDirRename(const char* existing, const char* target) {
    IpcMessage q{}, r{};
    if (!CapiEncoding().pathRequest(q, FsRequest::DirRename, existing))
        return invalid;
    if (!target || Runtime::stringLength(target) >= 384) return invalid;
    Runtime::copyString(reinterpret_cast<char*>(q.data) + 384, target, 384);
    q.length = 96;
    return CapiEncoding().fsResult(0, q, r);
}

int osDirRead(uint64_t handle, uint64_t offset, OsDirectoryEntry* entries,
              size_t capacity, size_t* count) {
    if (!count || (!entries && capacity)) return invalid;
    IpcMessage q{}, r{};
    q.type           = uint64_t(FsRequest::DirRead);
    q.length         = 3;
    q.data[0]        = handle;
    q.data[1]        = offset;
    q.data[2]        = capacity > 14 ? 14 : capacity;
    const int status = CapiEncoding().fsResult(0, q, r);
    if (status) return status;
    if (r.length < 2 || r.data[1] > q.data[2] ||
        r.length < 2 + (r.data[1] * sizeof(OsDirectoryEntry) + 7) / 8)
        return io;
    Runtime::copyBytes(entries, &r.data[2],
                       r.data[1] * sizeof(OsDirectoryEntry));
    *count = r.data[1];
    return 0;
}

int osMountCreate(const char* source, const char* target, const char* type,
                  unsigned long flags, const void* data, uint64_t* mountId) {
    if (!mountId) return invalid;
    void* storage = Runtime::allocateMemory(sizeof(Mount));
    if (!storage) return static_cast<int>(ErrorCode::OutOfMemory);
    auto* mount      = new (storage) Mount;
    const int status = mount->create(source, target, type, flags, data);
    if (status) {
        mount->~Mount();
        Runtime::freeMemory(mount);
        return status;
    }
    *mountId = reinterpret_cast<uint64_t>(mount);
    return 0;
}

int osMountClose(uint64_t mountId) {
    if (!mountId) return invalid;
    auto* mount      = reinterpret_cast<Mount*>(mountId);
    const int status = mount->close();
    if (status) return status;
    mount->~Mount();
    Runtime::freeMemory(mount);
    return 0;
}

int osMountQuery(uint32_t index, OsMountStat* result) {
    if (!result) return invalid;
    Mount mount;
    const int status = Fs().queryMount(index, mount);
    if (status) return status;
    OsMountStat value{};
    Runtime::copyString(value.device, mount.getDevice(), 64);
    Runtime::copyString(value.target, mount.getTarget(), 64);
    Runtime::copyString(value.type, mount.getType(), 16);
    value.readOnly = mount.getReadOnly();
    *result        = value;
    return 0;
}

int osIpcCreate(uint64_t* endpoint) {
    if (!endpoint) return invalid;
    const int64_t value = detail::EndpointControl().create();
    if (value <= 0) return value ? value : io;
    *endpoint = value;
    return 0;
}

int osIpcSend(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline) {
    if (!message) return invalid;
    return static_cast<int>(invokeSyscall(SyscallNumber::SendMessage, endpoint,
                                          reinterpret_cast<uint64_t>(message),
                                          deadline));
}

int osIpcReceive(uint64_t endpoint, OsIpcMessage* message, uint64_t deadline) {
    if (!message) return invalid;
    return static_cast<int>(
        invokeSyscall(SyscallNumber::ReceiveMessage, endpoint,
                      reinterpret_cast<uint64_t>(message), deadline));
}

int osIpcCall(uint64_t endpoint, OsIpcMessage* request, OsIpcMessage* reply,
              uint64_t deadline) {
    if (!request || !reply) return invalid;
    return static_cast<int>(invokeSyscall(SyscallNumber::CallService, endpoint,
                                          reinterpret_cast<uint64_t>(request),
                                          reinterpret_cast<uint64_t>(reply),
                                          deadline));
}

int osIpcReply(uint64_t endpoint, uint64_t replyToken, OsIpcMessage* reply) {
    if (!reply) return invalid;
    reply->replyToken = replyToken;
    return static_cast<int>(invokeSyscall(SyscallNumber::ReplyMessage, endpoint,
                                          reinterpret_cast<uint64_t>(reply)));
}

int osIpcHandleClose(uint64_t handle) {
    return Ipc::Handle(handle, false).close();
}

int osIpcHandleDuplicate(uint64_t handle, uint32_t rights, uint32_t flags,
                         uint64_t* result) {
    if (!result) return invalid;
    Ipc::Handle duplicate;
    const int status =
        Ipc::Handle(handle, false).duplicate(rights, flags, duplicate);
    if (status) return status;
    *result = duplicate.release();
    return 0;
}

int osIpcHandleRestrict(uint64_t handle, uint32_t rights) {
    return Ipc::Handle(handle, false).restrictRights(rights);
}

int osIpcHandleSetFlags(uint64_t handle, uint32_t flags) {
    return Ipc::Handle(handle, false).setFlags(flags);
}

int osIpcPipeCreate(uint64_t* readHandle, uint64_t* writeHandle) {
    if (!readHandle || !writeHandle || readHandle == writeHandle)
        return invalid;
    Ipc::Pipe pipe;
    const int status = pipe.create();
    if (status) return status;
    *readHandle  = pipe.releaseReadHandle();
    *writeHandle = pipe.releaseWriteHandle();
    return 0;
}

int64_t osIpcPipeRead(uint64_t readHandle, void* buffer, size_t size) {
    return detail::PipeClient(readHandle).read(buffer, size);
}

int64_t osIpcPipeWrite(uint64_t writeHandle, const void* buffer, size_t size) {
    return detail::PipeClient(writeHandle).write(buffer, size);
}

int osIpcMsgQueueOpen(const char* name, int flags, uint32_t maximumMessages,
                      uint32_t messageSize, uint64_t* handle) {
    if (!handle) return invalid;
    Ipc::MsgQueue queue;
    const int status = queue.open(name, flags, maximumMessages, messageSize);
    if (status) return status;
    *handle = queue.release();
    return 0;
}

int osIpcMsgQueueUnlink(const char* name) {
    return Ipc::MsgQueue().unlink(name);
}

int osIpcMsgQueueSend(uint64_t handle, const void* buffer, size_t size,
                      uint32_t priority) {
    return detail::QueueClient(handle).send(buffer, size, priority);
}

int64_t osIpcMsgQueueReceive(uint64_t handle, void* buffer, size_t capacity,
                             uint32_t* priority) {
    if (!priority) return invalid;
    return detail::QueueClient(handle).receive(buffer, capacity, *priority);
}

int osIpcMsgQueueQuery(uint64_t handle, OsMessageQueueAttributes* result) {
    if (!result) return invalid;
    detail::QueueClient queue(handle);
    const int status = queue.refresh();
    if (status) return status;
    *result = {queue.getFlags(), queue.getMaximumMessages(),
               queue.getMessageSize(), queue.getCurrentMessages()};
    return 0;
}

int osIpcMsgQueueSetFlags(uint64_t handle, uint64_t flags, uint64_t* previous) {
    return detail::QueueClient(handle).setFlags(flags, previous);
}

int osIpcMsgQueueSetNotification(uint64_t handle, int type, uint32_t signal) {
    return detail::QueueClient(handle).setNotification(type, signal);
}

int64_t osConsoleRead(void* buffer, size_t size) {
    Console console;
    const int status = console.open(Runtime::getConsoleId());
    return status ? status : console.read(buffer, size);
}

int64_t osConsoleWrite(const void* buffer, size_t size) {
    Console console;
    const int status = console.open(Runtime::getConsoleId());
    return status ? status : console.write(buffer, size);
}

int osConsoleGetId(uint32_t* id) {
    if (!id) return invalid;
    Console console;
    const int status = console.open(Runtime::getConsoleId());
    if (status) return status;
    *id = console.getId();
    return 0;
}

int osConsoleSetId(uint32_t id) { return Runtime::setConsoleId(id); }

int osPtyCreate(uint64_t* master, uint64_t* slave) {
    if (!master || !slave || master == slave) return invalid;
    Pty pty;
    const int status = pty.create();
    if (status) return status;
    *master = pty.releaseMaster();
    *slave  = pty.releaseSlave();
    return 0;
}

int osPtyGetNumber(uint64_t master, uint32_t* number) {
    if (!number) return invalid;
    detail::PtyClient pty(master);
    return pty.getNumber(*number) ? 0 : pty.getError();
}

int osPtySetNonblocking(uint64_t master, int enabled) {
    detail::PtyClient pty(master);
    return pty.setNonblocking(enabled) ? 0 : pty.getError();
}

int osPtySetForegroundGroup(uint64_t master, uint64_t group) {
    detail::PtyClient pty(master);
    return pty.setForegroundGroup(group) ? 0 : pty.getError();
}

int osTaskGetId(uint64_t* id) {
    return CapiEncoding().rawOutput(detail::TaskControl().getId(), id);
}

int osTaskQuery(uint64_t id, OsTaskInfo* result) {
    if (!result) return invalid;
    wire::TaskInfo value{};
    const int status = detail::TaskQuery().query(id, value);
    if (!status) Runtime::copyBytes(result, &value, sizeof(value));
    return status;
}

int osTaskGetParentId(uint64_t id, uint64_t* parentId) {
    if (!parentId) return invalid;
    wire::TaskInfo value{};
    const int status = detail::TaskQuery().query(id, value);
    if (!status) *parentId = value.parentId;
    return status;
}

int osTaskGetGroupId(uint64_t id, uint64_t* groupId) {
    if (!groupId) return invalid;
    wire::TaskInfo value{};
    const int status = detail::TaskQuery().query(id, value);
    if (!status) *groupId = value.taskGroupId;
    return status;
}

int osTaskGetSessionId(uint64_t id, uint64_t* sessionId) {
    if (!sessionId) return invalid;
    wire::TaskInfo value{};
    const int status = detail::TaskQuery().query(id, value);
    if (!status) *sessionId = value.sessionId;
    return status;
}

int osTaskExecute(const char* path, uint32_t argumentCount,
                  const char* const* arguments, uint64_t input, uint64_t output,
                  uint64_t error, uint64_t* handle) {
    if (!handle) return invalid;
    const int64_t value = detail::TaskControl().executeWithIo(
        path, argumentCount, arguments, input, output, error);
    return CapiEncoding().rawOutput(value, handle);
}

int64_t osTaskFork(void) { return forkTask(); }

int osTaskReplaceImage(const OsExecImageInfo* image) {
    if (image == nullptr) return -1;
    os::IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(os::TaskOperation::ReplaceImage);
    request.length  = 5;
    request.data[0] = reinterpret_cast<uint64_t>(image);
    return services::RequestChannel(os::CoreServiceKind::Task)
        .result(request, reply);
}

int64_t osTaskWaitChild(int64_t id, int* status, int options) {
    if (!status) return invalid;
    return detail::TaskControl().waitChild(id, *status, options);
}

int osTaskSendSignal(uint64_t id, uint32_t signal) {
    return detail::TaskControl().sendSignal(id, signal);
}

void osTaskExit(int status) { exitTask(status); }

int osTaskThreadGetId(uint64_t* id) {
    return CapiEncoding().rawOutput(detail::TaskControl().getThreadId(), id);
}

int osTaskThreadCreate(void (*entry)(void*), void* argument, void* stack,
                       size_t stackSize, uint64_t* id) {
    if (!id) return invalid;
    Task::Thread thread;
    const int status = thread.create(entry, argument, stack, stackSize);
    if (!status) *id = thread.getId();
    return status;
}

int osTaskThreadWait(uint64_t id, int* exitCode, int noHang) {
    if (!exitCode) return invalid;
    return detail::TaskControl().waitThread(id, *exitCode, noHang);
}

int osTaskThreadSendSignal(uint64_t id, uint32_t signal) {
    return detail::TaskControl().sendThreadSignal(id, signal);
}

void osTaskThreadExit(int status) { exitThread(status); }

int osTaskSessionCreate(uint64_t* id) {
    return CapiEncoding().rawOutput(detail::TaskControl().createSession(), id);
}

int osTaskSetGroup(uint64_t id, uint64_t group) {
    return detail::TaskControl().setGroup(group, id);
}

int osTaskGetNice(uint64_t id, int* value) {
    if (!value) return invalid;
    wire::TaskInfo task{};
    const int status = detail::TaskQuery().query(id, task);
    if (!status) *value = task.nice;
    return status;
}

int osTaskSetNice(uint64_t id, int value) {
    Task task;
    const int status = id ? task.open(id) : task.openCurrent();
    return status ? status : task.setNice(value);
}

int osTaskGetResourceLimit(uint64_t id, int resource, uint64_t* soft,
                           uint64_t* hard) {
    if (!soft || !hard) return invalid;
    Task task;
    const int status = id ? task.open(id) : task.openCurrent();
    return status ? status : task.queryResourceLimit(resource, *soft, *hard);
}

int osTaskSetResourceLimit(uint64_t id, int resource, uint64_t soft,
                           uint64_t hard) {
    Task task;
    const int status = id ? task.open(id) : task.openCurrent();
    return status ? status : task.setResourceLimit(resource, soft, hard);
}

int osTaskGetCredentials(uint64_t id, OsCredentialInfo* result) {
    if (!result) return invalid;
    wire::CredentialInfo value{};
    const int status = detail::TaskQuery().credentials(id, value);
    if (!status) Runtime::copyBytes(result, &value, sizeof(value));
    return status;
}

int osTaskSetUserIds(uint32_t real, uint32_t effective, uint32_t saved) {
    Task task;
    const int status = task.openCurrent();
    if (status) return status;
    Task::Credential credential(task);
    return credential.setUserIds(real, effective, saved);
}

int osTaskSetGroupIds(uint32_t real, uint32_t effective, uint32_t saved) {
    Task task;
    const int status = task.openCurrent();
    if (status) return status;
    Task::Credential credential(task);
    return credential.setGroupIds(real, effective, saved);
}

int osTaskSetGroups(const uint32_t* groups, uint32_t count) {
    Task task;
    const int status = task.openCurrent();
    if (status) return status;
    Task::Credential credential(task);
    return credential.setGroups(groups, count);
}

void osTaskYield(void) { yield(); }

int osTaskSleep(uint64_t milliseconds) {
    sleep(milliseconds);
    return 0;
}

int osTaskGetSignalAction(uint32_t signal, OsSignalAction* result) {
    if (!result) return invalid;
    wire::SignalAction value{};
    const int status =
        detail::TaskControl().setSignalAction(signal, nullptr, &value);
    if (!status) Runtime::copyBytes(result, &value, sizeof(value));
    return status;
}

int osTaskSetSignalAction(uint32_t signal, const OsSignalAction* action,
                          OsSignalAction* previous) {
    return detail::TaskControl().setSignalAction(
        signal, reinterpret_cast<const wire::SignalAction*>(action),
        reinterpret_cast<wire::SignalAction*>(previous));
}

int osTaskGetSignalMask(uint64_t* mask) {
    if (!mask) return invalid;
    return detail::TaskControl().setSignalMask(0, 0, *mask);
}

int osTaskSetSignalMask(int how, uint64_t mask, uint64_t* previous) {
    uint64_t old     = 0;
    const int status = detail::TaskControl().setSignalMask(how, mask, old);
    if (!status && previous) *previous = old;
    return status;
}

int osTaskGetPendingSignals(uint64_t* mask) {
    if (!mask) return invalid;
    return detail::TaskControl().getPendingSignals(*mask);
}

int osTaskWaitSignal(uint64_t mask) {
    return detail::TaskControl().waitSignal(mask);
}

int osTaskGetSignalStack(OsSignalStack* result) {
    if (!result) return invalid;
    return detail::TaskControl().signalStack(nullptr, result);
}

int osTaskSetSignalStack(const OsSignalStack* stack, OsSignalStack* previous) {
    return detail::TaskControl().signalStack(stack, previous);
}

int osTaskGetCoreLimit(uint64_t* soft, uint64_t* hard) {
    if (!soft || !hard) return invalid;
    return detail::TaskControl().coreLimit(false, *soft, *hard);
}

int osTaskSetCoreLimit(uint64_t soft, uint64_t hard) {
    return detail::TaskControl().coreLimit(true, soft, hard);
}

void osTaskSignalReturn(void* frame) {
    detail::TaskControl().signalReturn(frame);
}

int osMemMapAnonymous(uint64_t address, uint64_t size, uint32_t protection,
                      uint32_t flags, uint64_t* result) {
    if (!result || (flags & ~1U)) return invalid;
    if (flags & 1U) {
        IpcMessage q{}, r{};
        q.type    = uint64_t(MemoryOperation::AnonymousShared);
        q.length  = 2;
        q.data[0] = size;
        const int64_t raw =
            services::RequestChannel(CoreServiceKind::Memory).result(q, r);
        if (raw <= 0) return raw ? raw : io;
        Ipc::Handle backing(raw);
        return CapiEncoding().rawOutput(
            detail::MappingClient().shared(raw, size, protection, 0, address),
            result);
    }
    return CapiEncoding().rawOutput(
        detail::MappingClient().anonymous(size, protection, true, address),
        result);
}

int osMemMapFile(uint64_t handle, uint64_t address, uint64_t size,
                 uint64_t offset, uint32_t protection, uint32_t flags,
                 uint64_t* result) {
    if (!result || (flags & ~1U)) return invalid;
    if (flags & 1U) return static_cast<int>(ErrorCode::NotSupported);
    return CapiEncoding().rawOutput(
        detail::MappingClient().file(handle, size, protection, offset, address),
        result);
}

int osMemMapShared(uint64_t handle, uint64_t address, uint64_t size,
                   uint64_t offset, uint32_t protection, uint64_t* result) {
    if (!result) return invalid;
    return CapiEncoding().rawOutput(
        detail::MappingClient().shared(handle, size, protection, offset,
                                       address),
        result);
}

int osMemMapUnmap(uint64_t address, uint64_t size) {
    return detail::MappingClient().unmap(address, size);
}

int osMemMapSetProtection(uint64_t address, uint64_t size,
                          uint32_t protection) {
    return detail::MappingClient().protect(address, size, protection);
}

int osMemMapSync(uint64_t address, uint64_t size, int mode) {
    if (mode != 1 && mode != 4) return invalid;
    return detail::MappingClient().sync(address, size);
}

int osMemShareOpen(const char* name, int flags, uint64_t* handle) {
    if (!handle) return invalid;
    return CapiEncoding().rawOutput(
        detail::MemoryControl().openSharedMemory(name, flags), handle);
}

int osMemShareUnlink(const char* name) {
    return detail::MemoryControl().unlinkSharedMemory(name);
}

int osMemShareResize(uint64_t handle, uint64_t size) {
    return detail::MemoryControl().resizeSharedMemory(handle, size);
}

int osMemCheckRange(const void* address, uint64_t size, int writable) {
    return detail::TaskControl().accessible(address, size, writable)
               ? 0
               : static_cast<int>(ErrorCode::BadAddress);
}

int osSyncWaitAddress(volatile uint32_t* address, uint32_t expected) {
    if (!address) return invalid;
    return waitAddress(address, expected);
}

int osSyncWakeAddress(volatile uint32_t* address, uint32_t count) {
    if (!address) return invalid;
    return wakeAddress(address, count);
}

int osSysReadMonotonicTime(uint64_t* nanoseconds) {
    if (!nanoseconds) return invalid;
    *nanoseconds = Sys().readMonotonicTime();
    return 0;
}

int osSysReadWallClockTime(uint64_t* nanoseconds) {
    if (!nanoseconds) return invalid;
    *nanoseconds = Sys().readWallClockTime();
    return 0;
}

int osSysReadClockResolution(uint64_t* nanoseconds) {
    if (!nanoseconds) return invalid;
    *nanoseconds = Sys().readClockResolution();
    return 0;
}

int osSysConnectionLookup(const char* name, uint64_t* endpoint,
                          uint64_t* generation) {
    if (!endpoint) return invalid;
    uint64_t current  = 0;
    const auto handle = detail::RegistryClient().lookup(name, 100, &current);
    if (!handle) return static_cast<int>(ErrorCode::NotFound);
    *endpoint = handle;
    if (generation) *generation = current;
    return 0;
}
}
