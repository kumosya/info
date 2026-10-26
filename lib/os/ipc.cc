#include <os/ipc.h>

#include <os/console.h>
#include <os/device.h>
#include <os/mm.h>
#include <os/service.h>
#include <os/task.h>
#include "service/common/channel.h"
#include "service/common/entry.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/console/client.h"

namespace os {
int Ipc::MsgQueue::open(const char* name, int flags, uint32_t maximumMessages,
                        uint32_t messageSize) {
    if (getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!name || !*name || Runtime::stringLength(name) >= sizeof(name_))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MessageQueueOperation::Open);
    request.length  = 5;
    request.data[0] = static_cast<uint32_t>(flags);
    request.data[1] = maximumMessages;
    request.data[2] = messageSize;
    Runtime::copyString(reinterpret_cast<char*>(&request.data[3]), name, 64);
    request.length   = 3 + 8;
    const int status = services::RequestChannel(CoreServiceKind::MessageQueue)
                           .call(request, reply);
    for (unsigned i = 0; i < reply.handleCount && i < 4; ++i)
        if (reply.handles[i]) cleanup_[i].adopt(reply.handles[i]);
    const int64_t result = status ? status
                           : reply.length
                               ? static_cast<int64_t>(reply.data[0])
                               : static_cast<int>(ErrorCode::IoError);
    if (result || reply.handleCount != 1 || !reply.handles[0]) {
        close();
        return error_ = result ? result : static_cast<int>(ErrorCode::IoError);
    }
    handle_.adopt(cleanup_[0].release());
    Runtime::copyString(name_, name, sizeof(name_));
    return error_ = 0;
}

int Ipc::MsgQueue::unlink(const char* name) {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(MessageQueueOperation::Unlink);
    request.length = 5;
    if (!name) name = name_;
    if (!name || !*name || Runtime::stringLength(name) >= sizeof(name_))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    Runtime::copyString(reinterpret_cast<char*>(&request.data[3]), name, 64);
    request.length = 3 + 8;
    return error_  = services::RequestChannel(CoreServiceKind::MessageQueue)
                        .result(request, reply);
}

int Ipc::MsgQueue::send(const void* data, size_t size, uint32_t priority) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MessageQueueOperation::Send);
    request.length  = 5;
    request.data[0] = handle_.getValue();
    request.data[1] = size;
    request.data[2] = priority;
    if ((!data && size) || size > 256)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    Runtime::copyBytes(&request.data[3], data, size);
    request.length = 3 + (size + 7) / 8;
    return error_  = services::RequestChannel(CoreServiceKind::MessageQueue)
                        .result(request, reply);
}

int64_t Ipc::MsgQueue::receive(void* data, size_t capacity,
                               uint32_t& priority) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MessageQueueOperation::Receive);
    request.length  = 5;
    request.data[0] = handle_.getValue();
    request.data[1] = capacity > 256 ? 256 : capacity;
    if (!data && capacity)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const int64_t count =
        services::RequestChannel(CoreServiceKind::MessageQueue)
            .result(request, reply);
    if (count < 0) {
        error_ = count;
        return count;
    }
    if (static_cast<uint64_t>(count) > request.data[1] ||
        reply.length < 2 + (count + 7) / 8 || reply.data[1] > UINT32_MAX)
        return error_ = static_cast<int>(ErrorCode::IoError);
    Runtime::copyBytes(data, &reply.data[2], count);
    priority = reply.data[1];
    error_   = 0;
    return count;
}

int Ipc::MsgQueue::refresh() {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MessageQueueOperation::Attributes);
    request.length  = 5;
    request.data[0] = handle_.getValue();
    const int64_t status =
        services::RequestChannel(CoreServiceKind::MessageQueue)
            .result(request, reply);
    if (!status) {
        if (reply.length < 5 || (reply.data[1] & ~0x800ULL) || !reply.data[2] ||
            reply.data[2] > 16 || !reply.data[3] || reply.data[3] > 256 ||
            reply.data[4] > reply.data[2])
            return error_ = static_cast<int>(ErrorCode::IoError);
        flags_           = reply.data[1];
        maximumMessages_ = reply.data[2];
        messageSize_     = reply.data[3];
        currentMessages_ = reply.data[4];
    }
    return error_ = status;
}

int Ipc::MsgQueue::setFlags(uint64_t flags, uint64_t* previous) {
    if (flags & ~0x800ULL)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MessageQueueOperation::Flags);
    request.length  = 5;
    request.data[0] = handle_.getValue();
    request.data[1] = flags;
    const int64_t status =
        services::RequestChannel(CoreServiceKind::MessageQueue)
            .result(request, reply);
    if (!status) {
        if (reply.length < 2 || (reply.data[1] & ~0x800ULL))
            return error_ = static_cast<int>(ErrorCode::IoError);
        if (previous) *previous = reply.data[1];
        flags_ = flags;
    }
    return error_ = status;
}

int Ipc::MsgQueue::setNotification(int type, uint32_t signal) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(MessageQueueOperation::Notify);
    request.length  = 5;
    request.data[0] = handle_.getValue();
    request.data[1] = static_cast<uint32_t>(type);
    request.data[2] = signal;
    return error_   = services::RequestChannel(CoreServiceKind::MessageQueue)
                        .result(request, reply);
}

uint64_t detail::EndpointControl::create() {
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(IpcOperation::CreateEndpoint);
    request.length = 5;
    return services::RequestChannel(CoreServiceKind::Ipc)
        .result(request, reply);
}

int detail::EndpointControl::cancel(uint64_t endpoint, uint64_t threadId) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::SendMessage, endpoint, 0, 0, threadId));
}

Ipc::Pipe::Pipe() = default;
Ipc::Pipe::Pipe(uint64_t handle)
    : read_(handle, false), write_(handle, false) {}
Ipc::Pipe::~Pipe() { close(); }
Ipc::Pipe::Pipe(Pipe&& other) noexcept { *this = static_cast<Pipe&&>(other); }
Ipc::Pipe& Ipc::Pipe::operator=(Pipe&& other) noexcept {
    if (this == &other) return *this;
    if (getValid()) {
        error_ = static_cast<int>(ErrorCode::Busy);
        return *this;
    }
    read_  = static_cast<Handle&&>(other.read_);
    write_ = static_cast<Handle&&>(other.write_);
    for (unsigned i = 0; i < 4; ++i)
        cleanup_[i] = static_cast<Handle&&>(other.cleanup_[i]);
    identity_       = other.identity_;
    other.identity_ = 0;
    error_          = other.error_;
    other.error_    = 0;
    return *this;
}
int Ipc::Pipe::create(bool duplex) {
    if (getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(duplex ? PipeOperation::CreateChannel
                                                  : PipeOperation::Create);
    request.length = 5;
    const int status =
        services::RequestChannel(CoreServiceKind::Pipe).result(request, reply);
    if (status || reply.handleCount != 2 || !reply.handles[0] ||
        !reply.handles[1] || reply.handles[0] == reply.handles[1]) {
        // Retain every unexpected received reference for explicit cleanup.
        for (unsigned i = 0; i < reply.handleCount && i < 4; ++i)
            if (reply.handles[i]) cleanup_[i].adopt(reply.handles[i]);
        close();
        return error_ = status ? status : static_cast<int>(ErrorCode::IoError);
    }
    read_.adopt(reply.handles[0]);
    write_.adopt(reply.handles[1]);
    identity_     = queryIdentity();
    return error_ = 0;
}
int Ipc::Pipe::close() {
    const int first = read_.getOwned() ? read_.close() : (read_.release(), 0);
    const int second =
        write_.getOwned() ? write_.close() : (write_.release(), 0);
    int status = first ? first : second;
    for (auto& handle : cleanup_) {
        const int result = handle.close();
        if (!status) status = result;
    }
    if (!getValid()) identity_ = 0;
    return error_ = status;
}
uint64_t Ipc::Pipe::releaseReadHandle() { return read_.release(); }
uint64_t Ipc::Pipe::releaseWriteHandle() { return write_.release(); }
uint64_t Ipc::Pipe::getReadHandle() const { return read_.getValue(); }
uint64_t Ipc::Pipe::getWriteHandle() const { return write_.getValue(); }
bool Ipc::Pipe::getValid() const {
    if (read_.getValid() || write_.getValid()) return true;
    for (const auto& handle : cleanup_)
        if (handle.getValid()) return true;
    return false;
}
int Ipc::Pipe::getError() const { return error_; }
detail::PipeClient::PipeClient(uint64_t handle) : Ipc::Pipe(handle) {}
detail::QueueClient::QueueClient(uint64_t handle) : Ipc::MsgQueue(handle) {}
Ipc::MsgQueue::MsgQueue() = default;
Ipc::MsgQueue::MsgQueue(uint64_t handle) : handle_(handle, false) {}
Ipc::MsgQueue::~MsgQueue() { close(); }
int Ipc::MsgQueue::close() {
    int status = handle_.getOwned() ? handle_.close() : (handle_.release(), 0);
    for (auto& handle : cleanup_) {
        const int result = handle.close();
        if (!status) status = result;
    }
    if (!getValid()) {
        flags_ = maximumMessages_ = messageSize_ = currentMessages_ = 0;
        name_[0]                                                    = 0;
    }
    return error_ = status;
}
uint64_t Ipc::MsgQueue::release() {
    const uint64_t value = handle_.release();
    close();
    return value;
}
uint64_t Ipc::MsgQueue::getHandle() const { return handle_.getValue(); }
uint64_t Ipc::MsgQueue::getFlags() const { return flags_; }
uint64_t Ipc::MsgQueue::getMaximumMessages() const { return maximumMessages_; }
uint64_t Ipc::MsgQueue::getMessageSize() const { return messageSize_; }
uint64_t Ipc::MsgQueue::getCurrentMessages() const { return currentMessages_; }
bool Ipc::MsgQueue::getValid() const {
    if (handle_.getValid()) return true;
    for (const auto& handle : cleanup_)
        if (handle.getValid()) return true;
    return false;
}
int Ipc::MsgQueue::getError() const { return error_; }

uint64_t Ipc::Pipe::getIdentity() const { return identity_; }
uint64_t detail::PipeClient::getIdentity() const { return queryIdentity(); }
uint64_t Ipc::Pipe::queryIdentity() const {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PipeOperation::Identity);
    request.length  = 5;
    request.data[0] = read_.getValue();
    const auto value =
        services::RequestChannel(CoreServiceKind::Pipe).result(request, reply);
    return value > 0 ? value : 0;
}

bool Ipc::Pipe::setNonblocking(bool enabled) {
    const uint64_t handles[] = {read_.getValue(), write_.getValue()};
    error_                   = 0;
    if (!handles[0] && !handles[1]) {
        error_ = static_cast<int>(ErrorCode::InvalidHandle);
        return false;
    }
    for (unsigned i = 0; i < 2; ++i) {
        if (!handles[i] || (i && handles[i] == handles[0])) continue;
        IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(PipeOperation::Nonblocking);
        request.length   = 5;
        request.data[0]  = handles[i];
        request.data[1]  = enabled;
        const int status = services::RequestChannel(CoreServiceKind::Pipe)
                               .result(request, reply);
        if (!error_) error_ = status;
    }
    return error_ == 0;
}

bool Ipc::Pipe::signalEof() {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PipeOperation::Eof);
    request.length  = 5;
    request.data[0] = write_.getValid() ? write_.getValue() : read_.getValue();
    error_ =
        services::RequestChannel(CoreServiceKind::Pipe).result(request, reply);
    return error_ == 0;
}

int64_t Ipc::Pipe::read(void* buffer, size_t size) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PipeOperation::Read);
    request.length  = 5;
    request.data[0] = read_.getValue();
    request.data[1] = size > 512 ? 512 : size;
    if (!size) {
        error_ = 0;
        return 0;
    }
    if (!buffer) return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const int64_t count =
        services::RequestChannel(CoreServiceKind::Pipe).result(request, reply);
    if (count < 0) {
        error_ = count;
        return count;
    }
    if (static_cast<uint64_t>(count) > request.data[1] ||
        reply.length < 1 + (count + 7) / 8)
        return error_ = static_cast<int>(ErrorCode::IoError);
    Runtime::copyBytes(buffer, &reply.data[1], count);
    error_ = 0;
    return count;
}

int64_t Ipc::Pipe::write(const void* buffer, size_t size) {
    if (!buffer && size)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    size_t written = 0;
    while (written < size) {
        IpcMessage request{}, reply{};
        request.type       = static_cast<uint64_t>(PipeOperation::Write);
        const size_t chunk = size - written > 512 ? 512 : size - written;
        request.data[0]    = write_.getValue();
        request.data[1]    = chunk;
        Runtime::copyBytes(&request.data[3],
                           static_cast<const uint8_t*>(buffer) + written,
                           chunk);
        request.length      = 3 + (chunk + 7) / 8;
        const int64_t count = services::RequestChannel(CoreServiceKind::Pipe)
                                  .result(request, reply);
        if (count <= 0 || static_cast<uint64_t>(count) > chunk) {
            error_ = count < 0 ? count : static_cast<int>(ErrorCode::IoError);
            return written ? static_cast<int64_t>(written) : error_;
        }
        written += count;
    }
    error_ = 0;
    return written;
}

int sendMessage(uint64_t endpoint, IpcMessage& message, uint64_t deadline) {
    return static_cast<int>(invokeSyscall(SyscallNumber::SendMessage, endpoint,
                                          reinterpret_cast<uint64_t>(&message),
                                          deadline));
}
}  // namespace os

namespace os {
int receiveMessage(uint64_t endpoint, IpcMessage& message, uint64_t deadline) {
    return static_cast<int>(
        invokeSyscall(SyscallNumber::ReceiveMessage, endpoint,
                      reinterpret_cast<uint64_t>(&message), deadline));
}
}  // namespace os

namespace os {
int callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply,
                uint64_t deadline) {
    return static_cast<int>(invokeSyscall(SyscallNumber::CallService, endpoint,
                                          reinterpret_cast<uint64_t>(&request),
                                          reinterpret_cast<uint64_t>(&reply),
                                          deadline));
}
}  // namespace os

namespace os {
int replyMessage(uint64_t endpoint, uint64_t replyToken, IpcMessage& reply) {
    reply.replyToken = replyToken;
    return static_cast<int>(invokeSyscall(SyscallNumber::ReplyMessage, endpoint,
                                          reinterpret_cast<uint64_t>(&reply)));
}
}  // namespace os

namespace os {
bool closeHandle(uint64_t handle) {
    return Ipc::Handle(handle, false).close() == 0;
}
}  // namespace os

namespace os {
Ipc::Handle::Handle(uint64_t value, bool owned)
    : value_(value), owned_(owned) {}
Ipc::Handle::~Handle() {
    if (owned_) close();
}
Ipc::Handle::Handle(Handle&& other) noexcept
    : value_(other.value_), owned_(other.owned_) {
    other.value_ = 0;
    other.owned_ = false;
}
Ipc::Handle& Ipc::Handle::operator=(Handle&& other) noexcept {
    if (this != &other && !value_) {
        value_       = other.value_;
        owned_       = other.owned_;
        other.value_ = 0;
        other.owned_ = false;
    }
    return *this;
}
uint64_t Ipc::Handle::release() {
    const uint64_t value = value_;
    value_               = 0;
    owned_               = false;
    return value;
}
int Ipc::Handle::reset(uint64_t value, bool owned) {
    if (value_ == value) {
        owned_ = owned;
        return 0;
    }
    if (value_ && owned_) {
        const int status = close();
        if (status && value_) return status;
    }
    value_ = value;
    owned_ = value && owned;
    return 0;
}
int64_t Ipc::Handle::invoke(uint64_t operation, uint64_t rights,
                            uint64_t flags) const {
    if (!value_) return static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(IpcOperation::Handle);
    request.length  = 5;
    request.data[0] = value_;
    request.data[1] = operation;
    request.data[2] = rights;
    request.data[3] = flags;
    return services::RequestChannel(CoreServiceKind::Ipc)
        .result(request, reply);
}
int Ipc::Handle::close() {
    if (!value_) return 0;
    const int result = invoke(static_cast<uint64_t>(HandleOperation::Close));
    if (!result || result == static_cast<int>(ErrorCode::InvalidHandle)) {
        value_ = 0;
        owned_ = false;
    }
    return result;
}
int Ipc::Handle::restrictRights(uint32_t rights) {
    return invoke(static_cast<uint64_t>(HandleOperation::RestrictRights),
                  rights);
}
int Ipc::Handle::destroyEndpoint() {
    return invoke(static_cast<uint64_t>(HandleOperation::DestroyEndpoint));
}
int Ipc::Handle::requestActive(uint64_t token) const {
    return invoke(static_cast<uint64_t>(HandleOperation::RequestActive), token);
}
int Ipc::Handle::queryState() const {
    return invoke(static_cast<uint64_t>(HandleOperation::GetState));
}
uint64_t Ipc::Handle::queryIdentity() const {
    const int64_t value =
        invoke(static_cast<uint64_t>(HandleOperation::ObjectIdentity));
    return value > 0 ? value : 0;
}
int64_t Ipc::Handle::queryReferences() const {
    return invoke(static_cast<uint64_t>(HandleOperation::References));
}
int Ipc::Handle::adopt(uint64_t handle) {
    if (value_) return static_cast<int>(ErrorCode::Busy);
    if (!handle) return static_cast<int>(ErrorCode::InvalidHandle);
    value_ = handle;
    owned_ = true;
    return 0;
}
int Ipc::Handle::borrow(uint64_t handle) {
    if (value_) return static_cast<int>(ErrorCode::Busy);
    if (!handle) return static_cast<int>(ErrorCode::InvalidHandle);
    value_ = handle;
    owned_ = false;
    return 0;
}
int Ipc::Handle::duplicate(uint32_t rights, uint32_t flags,
                           Handle& result) const {
    if (result.getValid()) return static_cast<int>(ErrorCode::Busy);
    if (flags & ~handleFlagsMask)
        return static_cast<int>(ErrorCode::InvalidArgument);
    const int64_t value = invoke(
        static_cast<uint64_t>(HandleOperation::Duplicate), rights, flags);
    if (value <= 0)
        return value ? static_cast<int>(value)
                     : static_cast<int>(ErrorCode::InvalidHandle);
    return result.adopt(value);
}
uint64_t Ipc::Handle::getValue() const { return value_; }
bool Ipc::Handle::getValid() const { return value_ != 0; }
bool Ipc::Handle::getOwned() const { return owned_; }
int Ipc::Handle::setFlags(uint32_t flags) {
    if (flags & ~handleFlagsMask)
        return static_cast<int>(ErrorCode::InvalidArgument);
    return invoke(static_cast<uint64_t>(HandleOperation::SetFlags), 0, flags);
}
Ipc::Msg::Msg() = default;
Ipc::Msg::~Msg() { clear(); }
Ipc::Msg::Msg(Msg&& other) noexcept { *this = static_cast<Msg&&>(other); }
Ipc::Msg& Ipc::Msg::operator=(Msg&& other) noexcept {
    if (this == &other || clear()) return *this;
    wire_ = other.wire_;
    size_ = other.size_;
    for (unsigned i = 0; i < 4; ++i) {
        owned_[i]       = other.owned_[i];
        other.owned_[i] = false;
    }
    other.wire_ = {};
    other.size_ = 0;
    return *this;
}
int Ipc::Msg::clear() {
    int status = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!owned_[i] || !wire_.handles[i]) continue;
        Handle handle(wire_.handles[i]);
        const int result = handle.close();
        if (!handle.getValid()) {
            owned_[i]        = false;
            wire_.handles[i] = 0;
        } else {
            handle.release();
            if (!status) status = result;
        }
    }
    if (status) return status;
    wire_ = {};
    size_ = 0;
    return 0;
}
int Ipc::Msg::read(size_t offset, void* buffer, size_t size) const {
    if (offset > size_ || size > size_ - offset || (size && !buffer))
        return static_cast<int>(ErrorCode::InvalidArgument);
    if (size)
        Runtime::copyBytes(
            buffer, reinterpret_cast<const uint8_t*>(wire_.data) + offset,
            size);
    return 0;
}
int Ipc::Msg::write(size_t offset, const void* buffer, size_t size) {
    if (offset > sizeof(wire_.data) || size > sizeof(wire_.data) - offset ||
        (size && !buffer))
        return static_cast<int>(ErrorCode::InvalidArgument);
    if (size)
        Runtime::copyBytes(reinterpret_cast<uint8_t*>(wire_.data) + offset,
                           buffer, size);
    if (offset + size > size_) size_ = offset + size;
    wire_.length = (size_ + 7) / 8;
    return 0;
}
int Ipc::Msg::attachHandle(Handle& handle, bool transfer) {
    if (!handle.getValid() || (transfer && !handle.getOwned()))
        return static_cast<int>(ErrorCode::InvalidHandle);
    if (wire_.handleCount >= 4) return static_cast<int>(ErrorCode::OutOfMemory);
    for (unsigned i = 0; i < wire_.handleCount; ++i)
        if (wire_.handles[i] == handle.getValue() && (transfer || owned_[i]))
            return static_cast<int>(ErrorCode::InvalidArgument);
    const unsigned i       = wire_.handleCount++;
    wire_.handles[i]       = transfer ? handle.release() : handle.getValue();
    wire_.handleOptions[i] = transfer ? ipcHandleMove : 0;
    owned_[i]              = transfer;
    return 0;
}
int Ipc::Msg::takeHandle(uint32_t index, Handle& handle) {
    if (handle.getValid()) return static_cast<int>(ErrorCode::Busy);
    if (index >= wire_.handleCount || !owned_[index] || !wire_.handles[index])
        return static_cast<int>(ErrorCode::InvalidHandle);
    const int status = handle.adopt(wire_.handles[index]);
    if (!status) {
        owned_[index]              = false;
        wire_.handles[index]       = 0;
        wire_.handleOptions[index] = 0;
    }
    return status;
}
void Ipc::Msg::sent(bool success) {
    for (unsigned i = 0; i < wire_.handleCount; ++i) {
        if (!owned_[i] || !(wire_.handleOptions[i] & ipcHandleMove)) continue;
        // Calls may time out after enqueue committed the move. Handles include
        // a generation, so an invalid source cannot alias a newly allocated
        // handle.
        Handle reference(wire_.handles[i], false);
        if (success || reference.queryReferences() ==
                           static_cast<int>(ErrorCode::InvalidHandle)) {
            owned_[i]              = false;
            wire_.handles[i]       = 0;
            wire_.handleOptions[i] = 0;
        }
    }
}
int Ipc::Msg::accepted() {
    // Preserve responsibility for every installed handle even for a bad
    // payload.
    for (unsigned i = 0; i < wire_.handleCount && i < 4; ++i)
        owned_[i] = wire_.handles[i] != 0;
    if (wire_.length > ipcDataWordCount || wire_.handleCount > 4)
        return static_cast<int>(ErrorCode::IoError);
    size_ = wire_.length * 8;
    return 0;
}
size_t Ipc::Msg::getCapacity() const { return sizeof(wire_.data); }
uint64_t Ipc::Msg::getType() const { return wire_.type; }
size_t Ipc::Msg::getSize() const { return size_; }
uint32_t Ipc::Msg::getHandleCount() const { return wire_.handleCount; }
uint64_t Ipc::Msg::getSenderId() const { return wire_.senderPid; }
uint64_t Ipc::Msg::getSenderThreadId() const { return wire_.senderTid; }
void Ipc::Msg::setType(uint64_t type) { wire_.type = type; }
Ipc::Ipc() = default;
Ipc::~Ipc() { close(); }
Ipc& Ipc::operator=(Ipc&& other) noexcept {
    if (this != &other) {
        if (handle_.getValid()) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        handle_      = static_cast<Handle&&>(other.handle_);
        error_       = other.error_;
        other.error_ = 0;
    }
    return *this;
}
int Ipc::create() {
    if (handle_.getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    const int64_t value = detail::EndpointControl().create();
    if (value <= 0)
        return error_ = value ? static_cast<int>(value)
                              : static_cast<int>(ErrorCode::InvalidHandle);
    return error_ = handle_.adopt(value);
}
int Ipc::open(Handle&& handle) {
    if (handle_.getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!handle.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    const int status = handle.queryState();
    if (status < 0) return error_ = status;
    handle_       = static_cast<Handle&&>(handle);
    return error_ = 0;
}
int Ipc::close() {
    if (!handle_.getOwned()) {
        handle_.release();
        return error_ = 0;
    }
    return error_ = handle_.close();
}
int Ipc::destroy() { return error_ = handle_.destroyEndpoint(); }
int Ipc::send(Msg& message, uint64_t deadline) {
    const int status = sendMessage(getHandle(), message.wire_, deadline);
    message.sent(!status);
    return error_ = status;
}
int Ipc::receive(Msg& message, uint64_t deadline) {
    const int cleared = message.clear();
    if (cleared) return error_ = cleared;
    const int status = receiveMessage(getHandle(), message.wire_, deadline);
    return error_    = status ? status : message.accepted();
}
int Ipc::call(Msg& request, Msg& reply, uint64_t deadline) {
    if (&request == &reply)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const int cleared = reply.clear();
    if (cleared) return error_ = cleared;
    const int status =
        callService(getHandle(), request.wire_, reply.wire_, deadline);
    request.sent(!status);
    return error_ = status ? status : reply.accepted();
}
int Ipc::reply(const Msg& request, Msg& reply) {
    if (&request == &reply)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const int status =
        replyMessage(getHandle(), request.wire_.replyToken, reply.wire_);
    reply.sent(!status);
    return error_ = status;
}
int Ipc::cancel(uint64_t threadId) {
    return error_ = detail::EndpointControl().cancel(getHandle(), threadId);
}
uint64_t Ipc::getHandle() const { return handle_.getValue(); }
bool Ipc::getValid() const { return handle_.getValid(); }
int Ipc::getError() const { return error_; }
uint64_t Ipc::getInfiniteDeadline() const { return UINT64_MAX; }
}  // namespace os
