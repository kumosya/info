#include <os/pty.h>

#include <os/service.h>
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/tty/records.h"

namespace os {
Pty::Pty() = default;
Pty::Pty(uint64_t handle) : master_(handle, false) {}
Pty::~Pty() { close(); }
Pty::Pty(Pty&& other) noexcept
    : number_(other.number_),
      error_(other.error_),
      canonicalMode_(other.canonicalMode_),
      echo_(other.echo_),
      signalMode_(other.signalMode_),
      eraseCharacter_(other.eraseCharacter_),
      interruptCharacter_(other.interruptCharacter_),
      eofCharacter_(other.eofCharacter_),
      suspendCharacter_(other.suspendCharacter_),
      quitCharacter_(other.quitCharacter_),
      rows_(other.rows_),
      columns_(other.columns_),
      pixelWidth_(other.pixelWidth_),
      pixelHeight_(other.pixelHeight_),
      master_(static_cast<Ipc::Handle&&>(other.master_)),
      slave_(static_cast<Ipc::Handle&&>(other.slave_)),
      service_(static_cast<Ipc::Handle&&>(other.service_)) {}
Pty& Pty::operator=(Pty&& other) noexcept {
    if (this != &other) {
        if (getValid() || service_.getValid()) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        number_             = other.number_;
        error_              = other.error_;
        canonicalMode_      = other.canonicalMode_;
        echo_               = other.echo_;
        signalMode_         = other.signalMode_;
        eraseCharacter_     = other.eraseCharacter_;
        interruptCharacter_ = other.interruptCharacter_;
        eofCharacter_       = other.eofCharacter_;
        suspendCharacter_   = other.suspendCharacter_;
        quitCharacter_      = other.quitCharacter_;
        rows_               = other.rows_;
        columns_            = other.columns_;
        pixelWidth_         = other.pixelWidth_;
        pixelHeight_        = other.pixelHeight_;
        master_             = static_cast<Ipc::Handle&&>(other.master_);
        slave_              = static_cast<Ipc::Handle&&>(other.slave_);
        service_            = static_cast<Ipc::Handle&&>(other.service_);
    }
    return *this;
}
bool Pty::request(uint64_t operation, const void* input, size_t inputSize,
                  void* output, size_t outputSize) {
    if (!master_.getValue()) {
        error_ = static_cast<int>(ErrorCode::InvalidHandle);
        return false;
    }
    if (!service_.getValue())
        service_.reset(detail::RegistryClient().lookup("tty", 100));
    if (!service_.getValue()) {
        error_ = static_cast<int>(ErrorCode::NotFound);
        return false;
    }
    IpcMessage message{}, reply{};
    message.type        = operation;
    message.handleCount = 1;
    message.handles[0]  = master_.getValue();
    message.length      = (inputSize + 7) / 8;
    if (input) Runtime::copyBytes(message.data, input, inputSize);
    const int status = callService(service_.getValue(), message, reply);
    if (status) {
        service_.reset();
        error_ = status;
        return false;
    }
    if (reply.length < 1 + (outputSize + 7) / 8) {
        error_ = static_cast<int>(ErrorCode::IoError);
        return false;
    }
    if (reply.data[0]) {
        error_ = reply.data[0];
        return false;
    }
    if (output) Runtime::copyBytes(output, &reply.data[1], outputSize);
    error_ = 0;
    return true;
}
int Pty::create() {
    if (getValid()) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!service_.getValue())
        service_.reset(detail::RegistryClient().lookup("tty", 100));
    if (!service_.getValue())
        return error_ = static_cast<int>(ErrorCode::IoError);
    IpcMessage request{}, reply{};
    request.type = static_cast<uint64_t>(TtyRequest::CreatePty);
    if (callService(service_.getValue(), request, reply) || reply.length < 1 ||
        reply.data[0] || reply.handleCount != 2) {
        for (uint32_t i = 0; i < reply.handleCount && i < 4; ++i)
            closeHandle(reply.handles[i]);
        return error_ = static_cast<int>(ErrorCode::IoError);
    }
    master_.reset(reply.handles[0]);
    slave_.reset(reply.handles[1]);
    return refreshNumber() ? 0 : error_;
}
uint64_t Pty::getMasterHandle() const { return master_.getValue(); }
uint64_t Pty::getSlaveHandle() const { return slave_.getValue(); }
bool Pty::refreshNumber() {
    uint64_t value = 0;
    if (!request(static_cast<uint64_t>(TtyRequest::GetNumber), nullptr, 0,
                 &value, sizeof(value)))
        return false;
    number_ = value;
    return true;
}
bool Pty::getCanonicalMode() const { return canonicalMode_; }
bool Pty::getEcho() const { return echo_; }
bool Pty::getSignalMode() const { return signalMode_; }
uint8_t Pty::getEraseCharacter() const { return eraseCharacter_; }
uint8_t Pty::getInterruptCharacter() const { return interruptCharacter_; }
uint8_t Pty::getEofCharacter() const { return eofCharacter_; }
uint8_t Pty::getSuspendCharacter() const { return suspendCharacter_; }
uint8_t Pty::getQuitCharacter() const { return quitCharacter_; }
uint32_t Pty::getRows() const { return rows_; }
uint32_t Pty::getColumns() const { return columns_; }
uint32_t Pty::getPixelWidth() const { return pixelWidth_; }
uint32_t Pty::getPixelHeight() const { return pixelHeight_; }

bool Pty::setNonblocking(bool enabled) {
    const uint64_t value = enabled;
    return request(static_cast<uint64_t>(TtyRequest::SetNonblocking), &value,
                   sizeof(value), nullptr, 0);
}
bool Pty::setForegroundGroup(uint64_t group) {
    return request(static_cast<uint64_t>(TtyRequest::SetForegroundGroup),
                   &group, sizeof(group), nullptr, 0);
}
void Pty::setCanonicalMode(bool value) { canonicalMode_ = value; }
void Pty::setEcho(bool value) { echo_ = value; }
void Pty::setSignalMode(bool value) { signalMode_ = value; }
void Pty::setEraseCharacter(uint8_t value) { eraseCharacter_ = value; }
void Pty::setInterruptCharacter(uint8_t value) { interruptCharacter_ = value; }
void Pty::setEofCharacter(uint8_t value) { eofCharacter_ = value; }
void Pty::setSuspendCharacter(uint8_t value) { suspendCharacter_ = value; }
void Pty::setQuitCharacter(uint8_t value) { quitCharacter_ = value; }
void Pty::setRows(uint32_t value) { rows_ = value; }
void Pty::setColumns(uint32_t value) { columns_ = value; }
void Pty::setPixelWidth(uint32_t value) { pixelWidth_ = value; }
void Pty::setPixelHeight(uint32_t value) { pixelHeight_ = value; }
uint64_t Pty::releaseMaster() { return master_.release(); }
uint64_t Pty::releaseSlave() { return slave_.release(); }
bool Pty::refreshAttributes() {
    wire::TerminalAttributes value{};
    if (!request(static_cast<uint64_t>(TtyRequest::GetAttributes), nullptr, 0,
                 &value, sizeof(value)))
        return false;
    canonicalMode_      = value.canonicalMode;
    echo_               = value.echo;
    signalMode_         = value.signalMode;
    eraseCharacter_     = value.eraseCharacter;
    interruptCharacter_ = value.interruptCharacter;
    eofCharacter_       = value.eofCharacter;
    suspendCharacter_   = value.suspendCharacter;
    quitCharacter_      = value.quitCharacter;
    return true;
}
bool Pty::applyAttributes() {
    wire::TerminalAttributes value{};
    value.canonicalMode      = canonicalMode_;
    value.echo               = echo_;
    value.signalMode         = signalMode_;
    value.eraseCharacter     = eraseCharacter_;
    value.interruptCharacter = interruptCharacter_;
    value.eofCharacter       = eofCharacter_;
    value.suspendCharacter   = suspendCharacter_;
    value.quitCharacter      = quitCharacter_;
    return request(static_cast<uint64_t>(TtyRequest::SetAttributes), &value,
                   sizeof(value), nullptr, 0);
}
bool Pty::refreshWindowSize() {
    wire::TerminalWindowSize value{};
    if (!request(static_cast<uint64_t>(TtyRequest::GetWindowSize), nullptr, 0,
                 &value, sizeof(value)))
        return false;
    rows_        = value.rows;
    columns_     = value.columns;
    pixelWidth_  = value.pixelWidth;
    pixelHeight_ = value.pixelHeight;
    return true;
}
bool Pty::applyWindowSize() {
    wire::TerminalWindowSize value{};
    value.rows        = rows_;
    value.columns     = columns_;
    value.pixelWidth  = pixelWidth_;
    value.pixelHeight = pixelHeight_;
    return request(static_cast<uint64_t>(TtyRequest::SetWindowSize), &value,
                   sizeof(value), nullptr, 0);
}
}  // namespace os

namespace os {
int Pty::adopt(uint64_t master, uint64_t slave) {
    if (getValid() || service_.getValid())
        return error_ = static_cast<int>(ErrorCode::Busy);
    if (!master || !slave || master == slave)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    master_.adopt(master);
    slave_.adopt(slave);
    return refreshNumber() ? 0 : error_;
}
int Pty::borrow(uint64_t master) {
    if (getValid() || service_.getValid())
        return error_ = static_cast<int>(ErrorCode::Busy);
    const int result = master_.borrow(master);
    if (result) return error_ = result;
    return refreshNumber() ? 0 : error_;
}
int Pty::open(Ipc::Handle&& master) {
    if (getValid() || service_.getValid())
        return error_ = static_cast<int>(ErrorCode::Busy);
    if (!master.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    master_.borrow(master.getValue());
    if (!refreshNumber()) {
        master_.release();
        return error_;
    }
    master_.release();
    master_       = static_cast<Ipc::Handle&&>(master);
    return error_ = 0;
}
int Pty::close() {
    int result             = 0;
    Ipc::Handle* handles[] = {&master_, &slave_, &service_};
    for (auto* handle : handles) {
        const int status =
            handle->getOwned() ? handle->close() : (handle->release(), 0);
        if (!result) result = status;
    }
    if (!getValid()) number_ = 0;
    return error_ = result;
}
uint32_t Pty::getNumber() const { return number_; }
bool Pty::getValid() const { return master_.getValid() || slave_.getValid(); }
int Pty::getError() const { return error_; }
int Pty::queryForegroundGroup(uint64_t& group) {
    uint64_t value = 0;
    if (!request(static_cast<uint64_t>(TtyRequest::GetForegroundGroup), nullptr,
                 0, &value, sizeof(value)))
        return error_;
    group = value;
    return 0;
}
detail::PtyClient::PtyClient(uint64_t handle) : Pty(handle) {}
bool detail::PtyClient::getNumber(uint32_t& number) {
    if (!refreshNumber()) return false;
    number = number_;
    return true;
}
}  // namespace os
