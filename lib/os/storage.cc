#include <os/storage.h>

#include <os/service.h>
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"

namespace os {
uint64_t Storage::Part::getStartBlock() const { return startBlock_; }
uint64_t Storage::Part::getBlockCount() const { return blockCount_; }
uint32_t Storage::Part::getType() const { return type_; }
uint32_t Storage::Part::getNumber() const { return number_; }
const char* Storage::Part::getUuid() const { return uuid_; }
void Storage::Part::setStartBlock(uint64_t value) { startBlock_ = value; }
void Storage::Part::setBlockCount(uint64_t value) { blockCount_ = value; }
void Storage::Part::setType(uint32_t value) { type_ = value; }
void Storage::Part::setNumber(uint32_t value) { number_ = value; }
void Storage::Part::setUuid(const char* value) {
    Runtime::copyString(uuid_, value ? value : "", sizeof(uuid_));
}
uint64_t Storage::Device::getBlockCount() const { return blockCount_; }
uint32_t Storage::Device::getBlockSize() const { return blockSize_; }
uint32_t Storage::Device::getMaxTransferBlocks() const {
    return maxTransferBlocks_;
}
Storage::Device::Type Storage::Device::getType() const { return type_; }
bool Storage::Device::getReadOnly() const { return readOnly_; }
uint32_t Storage::Device::getId() const { return id_; }
uint32_t Storage::Device::getMajor() const { return major_; }
uint32_t Storage::Device::getMinor() const { return minor_; }
uint32_t Storage::Device::getParentId() const { return parentId_; }
uint32_t Storage::Device::getPartitionNumber() const {
    return partitionNumber_;
}
uint64_t Storage::Device::getStartBlock() const { return startBlock_; }
const char* Storage::Device::getName() const { return name_; }
const char* Storage::Device::getPartitionUuid() const { return partitionUuid_; }
const char* Storage::Device::getHardwarePath() const { return hardwarePath_; }
Storage::Number::Number(uint32_t major, uint32_t minor)
    : major_(major), minor_(minor) {}
uint64_t Storage::Number::getValue() const {
    return (uint64_t(major_) << 32) | minor_;
}
uint32_t Storage::Number::getMajor() const { return major_; }
uint32_t Storage::Number::getMinor() const { return minor_; }
void Storage::Number::setMajor(uint32_t value) { major_ = value; }
void Storage::Number::setMinor(uint32_t value) { minor_ = value; }
Storage::Device::Device() = default;
void Storage::Device::lock() {
    uint32_t expected = 0;
    while (!__atomic_compare_exchange_n(&locked_, &expected, 1, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        waitAddress(&locked_, 1);
        expected = 0;
    }
}
void Storage::Device::unlock() {
    __atomic_store_n(&locked_, 0, __ATOMIC_RELEASE);
    wakeAddress(&locked_, 1);
}
Storage::Device::Guard::Guard(Storage::Device& device) : device_(device) {
    device_.lock();
}
Storage::Device::Guard::~Guard() { device_.unlock(); }
bool Storage::Device::openService(uint32_t device, uint64_t service) {
    if (service_.getValid()) return false;
    if (device >= maximumStorageDevices) return false;
    device_ = device;
    if (service)
        service_.reset(service, false);
    else
        service_.reset(detail::RegistryClient().lookup("storage", 1000));
    if (service_.getValue() && refresh()) return true;
    service_.reset();
    blockCount_        = {};
    blockSize_         = {};
    maxTransferBlocks_ = {};
    type_              = {};
    readOnly_          = {};
    id_                = {};
    major_             = {};
    minor_             = {};
    parentId_          = {};
    partitionNumber_   = {};
    reserved_          = {};
    startBlock_        = {};
    Runtime::clearMemory(name_, sizeof(name_));
    Runtime::clearMemory(partitionUuid_, sizeof(partitionUuid_));
    Runtime::clearMemory(hardwarePath_, sizeof(hardwarePath_));
    return false;
}
bool Storage::Device::refresh() {
    Guard guard(*this);
    request_         = {};
    reply_           = {};
    request_.type    = static_cast<uint64_t>(StorageRequest::GetInfo);
    request_.length  = 1;
    request_.data[0] = device_;
    if (!service_.getValue() ||
        callService(service_.getValue(), request_, reply_) || reply_.data[0] ||
        reply_.length < storageIdentityWord +
                            (sizeof(wire::StorageDeviceIdentity) + 7) / 8 ||
        !reply_.data[1] || reply_.data[2] != 512 ||
        reply_.data[1] > UINT64_MAX / 512)
        return false;
    blockCount_        = reply_.data[1];
    blockSize_         = uint32_t(reply_.data[2]);
    maxTransferBlocks_ = uint32_t(reply_.data[3]);
    type_              = static_cast<Storage::Device::Type>(reply_.data[4]);
    readOnly_          = reply_.data[5] != 0;
    wire::StorageDeviceIdentity value{};
    Runtime::copyBytes(&value, &reply_.data[storageIdentityWord],
                       sizeof(value));
    id_              = value.id;
    major_           = value.major;
    minor_           = value.minor;
    parentId_        = value.parentId;
    partitionNumber_ = value.partitionNumber;
    reserved_        = value.reserved;
    startBlock_      = value.startBlock;
    Runtime::copyBytes(name_, value.name, sizeof(name_));
    Runtime::copyBytes(partitionUuid_, value.partitionUuid,
                       sizeof(partitionUuid_));
    Runtime::copyBytes(hardwarePath_, value.hardwarePath,
                       sizeof(hardwarePath_));
    name_[sizeof(name_) - 1]                   = 0;
    partitionUuid_[sizeof(partitionUuid_) - 1] = 0;
    hardwarePath_[sizeof(hardwarePath_) - 1]   = 0;
    return true;
}
int Storage::Device::read(uint64_t first, uint32_t count, void* buffer,
                          size_t capacity, uint32_t& completed) {
    completed = 0;
    if (uint64_t(count) * 512 > capacity)
        return static_cast<int>(ErrorCode::InvalidArgument);
    return transfer(first, count, buffer, nullptr, completed);
}
int Storage::Device::write(uint64_t first, uint32_t count, const void* buffer,
                           size_t size, uint32_t& completed) {
    completed = 0;
    if (uint64_t(count) * 512 > size)
        return static_cast<int>(ErrorCode::InvalidArgument);
    return transfer(first, count, nullptr, buffer, completed);
}
int Storage::Device::transfer(uint64_t first, uint32_t count, void* output,
                              const void* input, uint32_t& completed) {
    completed = 0;
    Guard guard(*this);
    if (!service_.getValue() || (!output && !input) || (input && readOnly_) ||
        !count || first >= blockCount_ || count > blockCount_ - first ||
        !maxTransferBlocks_)
        return static_cast<int>(ErrorCode::InvalidArgument);
    uint32_t done = 0;
    while (done < count) {
        const uint32_t limit = maxTransferBlocks_ < maximumStorageTransferBlocks
                                   ? maxTransferBlocks_
                                   : maximumStorageTransferBlocks;
        const uint32_t batch = count - done < limit ? count - done : limit,
                       bytes = batch * 512;
        request_             = {};
        reply_               = {};
        request_.type        = static_cast<uint64_t>(
            input ? StorageRequest::WriteRange : StorageRequest::ReadRange);
        request_.data[0] = (uint64_t(device_) << 56) | (first + done);
        request_.data[1] = batch;
        request_.length  = 2;
        if (input) {
            Runtime::copyBytes(
                &request_.data[2],
                static_cast<const uint8_t*>(input) + uint64_t(done) * 512,
                bytes);
            request_.length += bytes / 8;
        }
        if (callService(service_.getValue(), request_, reply_) ||
            reply_.length < 2 || reply_.data[0] || reply_.data[1] != bytes ||
            (!input && reply_.length < 2 + bytes / 8))
            return static_cast<int>(ErrorCode::IoError);
        if (output)
            Runtime::copyBytes(
                static_cast<uint8_t*>(output) + uint64_t(done) * 512,
                &reply_.data[2], bytes);
        done += batch;
        completed = done;
    }
    return 0;
}
bool Storage::Device::flush() {
    Guard guard(*this);
    request_         = {};
    reply_           = {};
    request_.type    = static_cast<uint64_t>(StorageRequest::Flush);
    request_.length  = 1;
    request_.data[0] = device_;
    return service_.getValue() &&
           !callService(service_.getValue(), request_, reply_) &&
           reply_.length >= 1 && !reply_.data[0];
}
int64_t detail::StorageClient::deviceCount() {
    Ipc::Handle service(detail::RegistryClient().lookup("storage", 1000));
    if (!service.getValue()) return static_cast<int>(ErrorCode::NotFound);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(StorageRequest::GetCount);
    const int status = callService(service.getValue(), request, reply);
    if (status) return status;
    if (reply.length < 2) return static_cast<int>(ErrorCode::InvalidArgument);
    return reply.data[0] ? static_cast<int64_t>(reply.data[0])
                         : static_cast<int64_t>(reply.data[1]);
}
}  // namespace os

namespace os {
Storage::Storage() = default;
int Storage::refresh() {
    const int64_t count = detail::StorageClient().deviceCount();
    if (count < 0) return count;
    if (count > maximumStorageDevices)
        return static_cast<int>(ErrorCode::IoError);
    count_ = count;
    return 0;
}
uint32_t Storage::getDeviceCount() const { return count_; }
int Storage::queryDevice(uint32_t id, Device& device) {
    return device.open(id);
}
int Storage::nextDevice(uint32_t minimumId, Device& device) {
    if (device.getBlockCount() && device.service_.getValid())
        return static_cast<int>(ErrorCode::Busy);
    const int status = refresh();
    if (status) return status;
    return minimumId < count_ ? device.open(minimumId)
                              : static_cast<int>(ErrorCode::NotFound);
}
Storage::Device::~Device() { close(); }
int Storage::Device::open(uint32_t id) {
    if (service_.getValid()) return static_cast<int>(ErrorCode::Busy);
    return openService(id, 0) ? 0 : static_cast<int>(ErrorCode::IoError);
}
int Storage::Device::close() {
    Guard guard(*this);
    return service_.reset();
}
bool detail::StorageClient::open(Storage::Device& device, uint32_t id,
                                 uint64_t service) {
    return device.openService(id, service);
}
}  // namespace os
