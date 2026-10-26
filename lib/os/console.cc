#include "service/common/queries.h"
#include <os/console.h>

#include <os/ipc.h>
#include <os/service.h>
#include <os/syscall.h>
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/console/client.h"

namespace os {
Console::Console() = default;
Console::~Console() { close(); }
Console& Console::operator=(Console&& other) noexcept {
    if (this == &other) return *this;
    if (endpoint_) {
        error_ = static_cast<int>(ErrorCode::Busy);
        return *this;
    }
    endpoint_        = other.endpoint_;
    id_              = other.id_;
    valid_           = other.valid_;
    active_          = other.active_;
    echo_            = other.echo_;
    foregroundGroup_ = other.foregroundGroup_;
    error_           = other.error_;
    other.endpoint_  = 0;
    other.id_        = 0;
    other.valid_ = other.active_ = other.echo_ = false;
    other.foregroundGroup_                     = 0;
    other.error_                               = 0;
    return *this;
}
int Console::open(uint32_t id) {
    if (endpoint_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (id > graphicalTerminalId)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const uint64_t endpoint =
        detail::RegistryClient().lookup(terminalServiceName, 1000);
    if (!endpoint) return error_ = static_cast<int>(ErrorCode::NotFound);
    endpoint_        = endpoint;
    id_              = id;
    const int status = refresh();
    if (status) {
        close();
        return error_ = status;
    }
    return 0;
}
int Console::openCurrent() { return open(Runtime::getConsoleId()); }
int Console::close() {
    if (endpoint_) {
        const int status = Ipc::Handle(endpoint_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
    }
    endpoint_ = 0;
    id_       = 0;
    valid_ = active_ = echo_ = false;
    foregroundGroup_         = 0;
    return error_            = 0;
}
int Console::refresh() {
    if (!endpoint_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TerminalRequest::GetState);
    request.length   = 1;
    request.data[0]  = id_;
    const int status = callService(endpoint_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 1) return error_ = static_cast<int>(ErrorCode::IoError);
    if (reply.data[0]) return error_ = static_cast<int>(reply.data[0]);
    if (reply.length != 4 || reply.data[1] > 1 || reply.data[2] > 1)
        return error_ = static_cast<int>(ErrorCode::IoError);
    active_          = reply.data[1] != 0;
    echo_            = reply.data[2] != 0;
    foregroundGroup_ = reply.data[3];
    valid_           = true;
    return error_    = 0;
}
int64_t Console::read(void* buffer, size_t size) {
    if (!getValid()) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (!size) {
        error_ = 0;
        return 0;
    }
    if (!buffer) return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    const size_t capacity = size < 48 ? size : 48;
    while (true) {
        IpcMessage request{}, reply{};
        request.type     = static_cast<uint64_t>(TerminalRequest::Read);
        request.length   = 2;
        request.data[0]  = id_;
        request.data[1]  = capacity;
        const int status = callService(endpoint_, request, reply);
        if (status) return error_ = status;
        if (reply.length < 1)
            return error_ = static_cast<int>(ErrorCode::IoError);
        const int result = static_cast<int>(reply.data[0]);
        if (result == static_cast<int>(ErrorCode::WouldBlock)) {
            sleep(8);
            continue;
        }
        if (result) return error_ = result;
        if (reply.length < 2 || reply.data[1] > capacity ||
            reply.length < 2 + (reply.data[1] + 7) / 8)
            return error_ = static_cast<int>(ErrorCode::IoError);
        Runtime::copyBytes(buffer, &reply.data[2], reply.data[1]);
        error_ = 0;
        return reply.data[1];
    }
}
int64_t Console::write(const void* buffer, size_t size) {
    if (!getValid()) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (!size) {
        error_ = 0;
        return 0;
    }
    if (!buffer || size > static_cast<size_t>(INT64_MAX))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    size_t offset = 0;
    while (offset < size) {
        const size_t chunk = size - offset < 512 ? size - offset : 512;
        IpcMessage request{}, reply{};
        request.type    = static_cast<uint64_t>(TerminalRequest::Write);
        request.data[0] = id_;
        request.data[1] = chunk;
        request.length  = 2 + (chunk + 7) / 8;
        Runtime::copyBytes(&request.data[2],
                           static_cast<const uint8_t*>(buffer) + offset, chunk);
        int status = callService(endpoint_, request, reply);
        if (!status)
            status = reply.length < 1 ? static_cast<int>(ErrorCode::IoError)
                                      : static_cast<int>(reply.data[0]);
        if (!status &&
            (reply.length < 2 || !reply.data[1] || reply.data[1] > chunk))
            status = static_cast<int>(ErrorCode::IoError);
        if (status) {
            error_ = status;
            return offset ? static_cast<int64_t>(offset) : status;
        }
        offset += reply.data[1];
    }
    error_ = 0;
    return static_cast<int64_t>(offset);
}
int Console::activate() {
    if (!getValid()) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TerminalRequest::Switch);
    request.length   = 1;
    request.data[0]  = id_;
    const int status = callService(endpoint_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 1) return error_ = static_cast<int>(ErrorCode::IoError);
    if (reply.data[0]) return error_ = static_cast<int>(reply.data[0]);
    active_       = true;
    return error_ = 0;
}
uint32_t Console::getId() const { return id_; }
bool Console::getOpened() const { return endpoint_ != 0; }
bool Console::getActive() const { return active_; }
bool Console::getEcho() const { return echo_; }
uint64_t Console::getForegroundGroup() const { return foregroundGroup_; }
bool Console::getValid() const { return endpoint_ && valid_; }
int Console::getError() const { return error_; }
int Console::setEcho(bool enabled) {
    if (!getValid()) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(TerminalRequest::SetEcho);
    request.length   = 2;
    request.data[0]  = id_;
    request.data[1]  = enabled;
    const int status = callService(endpoint_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 1) return error_ = static_cast<int>(ErrorCode::IoError);
    if (reply.data[0]) return error_ = static_cast<int>(reply.data[0]);
    echo_         = enabled;
    return error_ = 0;
}
int Console::setForegroundGroup(uint64_t group) {
    if (!getValid()) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(TerminalRequest::SetForegroundGroup);
    request.length = 2;
    request.data[0]  = id_;
    request.data[1]  = group;
    const int status = callService(endpoint_, request, reply);
    if (status) return error_ = status;
    if (reply.length < 1) return error_ = static_cast<int>(ErrorCode::IoError);
    if (reply.data[0]) return error_ = static_cast<int>(reply.data[0]);
    foregroundGroup_ = group;
    if (!group) echo_ = true;
    return error_ = 0;
}

Console::EscapeParser::Result Console::EscapeParser::feed(uint8_t value) {
    if (value == 0x1b) {
        reset();
        state_ = 1;
        return Result::Pending;
    }
    if (value == 0x18 || value == 0x1a) {
        reset();
        return Result::Pending;
    }
    if (!state_) return Result::Text;
    if (state_ == 1) {
        if (value != '[') {
            reset();
            return Result::Text;
        }
        state_ = 2;
        count_ = 1;
        return Result::Pending;
    }
    if (value >= 0x40 && value <= 0x7e) {
        const bool valid = state_ == 2;
        state_           = 0;
        if (valid) command_ = static_cast<char>(value);
        return valid ? Result::Command : Result::Pending;
    }
    if (state_ == 3 || value < 0x20) return Result::Pending;
    if (value >= '0' && value <= '9') {
        uint16_t& parameter = parameters_[count_ - 1];
        const uint32_t next = parameter * 10U + value - '0';
        parameter           = next > 9999 ? 9999 : next;
    } else if (value == ';') {
        if (count_ < 8)
            ++count_;
        else
            state_ = 3;
    } else
        state_ = 3;
    return Result::Pending;
}
void Console::EscapeParser::reset() {
    for (auto& parameter : parameters_) parameter = 0;
    count_ = state_ = 0;
    command_        = 0;
}
char Console::EscapeParser::getCommand() const { return command_; }
uint32_t Console::EscapeParser::getParameterCount() const { return count_; }
uint16_t Console::EscapeParser::getParameter(uint32_t index) const {
    return index < count_ ? parameters_[index] : 0;
}
void Console::Style::reset() {
    foreground_ = 7;
    background_ = attributes_ = 0;
}
void Console::Style::apply(uint16_t value) {
    if (!value)
        reset();
    else if (value == 1)
        attributes_ |= 1;
    else if (value == 4)
        attributes_ |= 2;
    else if (value == 7)
        attributes_ |= 4;
    else if (value == 22)
        attributes_ &= ~1U;
    else if (value == 24)
        attributes_ &= ~2U;
    else if (value == 27)
        attributes_ &= ~4U;
    else if (value == 39)
        foreground_ = 7;
    else if (value == 49)
        background_ = 0;
    else if (value >= 30 && value <= 37)
        foreground_ = value - 30;
    else if (value >= 40 && value <= 47)
        background_ = value - 40;
    else if (value >= 90 && value <= 97)
        foreground_ = value - 90 + 8;
    else if (value >= 100 && value <= 107)
        background_ = value - 100 + 8;
}
uint8_t Console::Style::getForeground() const { return foreground_; }
uint8_t Console::Style::getBackground() const { return background_; }
uint8_t Console::Style::getAttributes() const { return attributes_; }
void Console::Style::setForeground(uint8_t value) {
    foreground_ = value < 16 ? value : 7;
}
void Console::Style::setBackground(uint8_t value) {
    background_ = value < 16 ? value : 0;
}
void Console::Style::setAttributes(uint8_t value) { attributes_ = value & 7U; }
uint32_t Console::terminalColor(uint8_t index, bool bright) {
    constexpr uint32_t palette[] = {0x171a1f, 0xb84040, 0x4aa36b, 0xc49a44,
                                    0x4a82bd, 0x9c5caf, 0x3aa6a0, 0xd9dde3,
                                    0x646b75, 0xef6666, 0x6ed596, 0xf0c674,
                                    0x7fb7e8, 0xc792ea, 0x5fd7d1, 0xffffff};
    return index < 16 ? palette[index | (bright ? 8U : 0U)] : palette[7];
}
}  // namespace os
