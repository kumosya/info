#include <os/device.h>

#include <os/ipc.h>
#include <os/service.h>
#include "service/common/channel.h"
#include "service/common/kernel-requests.h"
#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
namespace os {
namespace {
class PciConnection {
   public:
    ~PciConnection() {
        const uint64_t endpoint =
            __atomic_exchange_n(&endpoint_, 0ULL, __ATOMIC_ACQ_REL);
        if (endpoint) os::Ipc::Handle(endpoint, false).close();
    }
    int call(IpcMessage& request, IpcMessage& reply) {
        uint64_t endpoint = __atomic_load_n(&endpoint_, __ATOMIC_ACQUIRE);
        if (!endpoint) {
            const uint64_t opened =
                detail::RegistryClient().lookup(pciServiceName, 1000);
            if (!opened) return static_cast<int>(ErrorCode::NotFound);
            const int flags =
                os::Ipc::Handle(opened, false).setFlags(handleFlagCloseOnExec);
            if (flags) {
                os::Ipc::Handle(opened, false).close();
                return flags;
            }
            uint64_t expected = 0;
            if (__atomic_compare_exchange_n(&endpoint_, &expected, opened,
                                            false, __ATOMIC_RELEASE,
                                            __ATOMIC_ACQUIRE))
                endpoint = opened;
            else {
                os::Ipc::Handle(opened, false).close();
                endpoint = expected;
            }
        }
        const int status = callService(endpoint, request, reply);
        if (status == static_cast<int>(ErrorCode::InvalidHandle) ||
            status == static_cast<int>(ErrorCode::PeerDied) ||
            status == static_cast<int>(ErrorCode::EndpointClosed)) {
            uint64_t expected = endpoint;
            if (__atomic_compare_exchange_n(&endpoint_, &expected, 0ULL, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                os::Ipc::Handle(endpoint, false).close();
        }
        if (status) return status;
        if (!reply.length) return static_cast<int>(ErrorCode::IoError);
        const int64_t result = static_cast<int64_t>(reply.data[0]);
        return result > 0 ? static_cast<int>(ErrorCode::InvalidArgument)
                          : static_cast<int>(result);
    }

   private:
    uint64_t endpoint_{};
};
PciConnection pciConnection;
}  // namespace
Device::Pci::Pci() = default;
void Device::Pci::decode(const void* bytes) {
    wire::PciFunctionInfo value{};
    Runtime::copyBytes(&value, bytes, sizeof(value));
    segment_              = value.segment;
    bus_                  = value.bus;
    device_               = value.device;
    function_             = value.function;
    classCode_            = value.classCode;
    subclass_             = value.subclass;
    programmingInterface_ = value.programmingInterface;
    vendorId_             = value.vendorId;
    deviceId_             = value.deviceId;
    headerType_           = value.headerType;
    valid_                = true;
}
void Device::Pci::encode(void* bytes) const {
    const wire::PciFunctionInfo value{segment_,
                                      bus_,
                                      device_,
                                      function_,
                                      classCode_,
                                      subclass_,
                                      programmingInterface_,
                                      vendorId_,
                                      deviceId_,
                                      headerType_,
                                      {}};
    Runtime::copyBytes(bytes, &value, sizeof(value));
}
int Device::Pci::enumerate(Pci* functions, uint32_t capacity) {
    if (!functions || !capacity)
        return static_cast<int>(ErrorCode::InvalidArgument);
    const uint32_t limit = capacity < 64 ? capacity : 64;
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(PciServiceRequest::Enumerate);
    request.length   = 1;
    request.data[0]  = limit;
    const int status = pciConnection.call(request, reply);
    if (status) return status;
    if (reply.length < 2 || reply.data[1] > limit ||
        reply.length <
            2 + (reply.data[1] * sizeof(wire::PciFunctionInfo) + 7) / 8)
        return static_cast<int>(ErrorCode::IoError);
    for (uint32_t i = 0; i < reply.data[1]; ++i) {
        wire::PciFunctionInfo value{};
        Runtime::copyBytes(&value,
                           reinterpret_cast<const uint8_t*>(&reply.data[2]) +
                               i * sizeof(value),
                           sizeof(value));
        if (value.device >= 32 || value.function >= 8 ||
            value.vendorId == 0xffff)
            return static_cast<int>(ErrorCode::IoError);
    }
    for (uint32_t i = 0; i < reply.data[1]; ++i)
        functions[i].decode(reinterpret_cast<const uint8_t*>(&reply.data[2]) +
                            i * sizeof(wire::PciFunctionInfo));
    return reply.data[1];
}
int Device::Pci::query(uint16_t segment, uint8_t bus, uint8_t device,
                       uint8_t function, Pci& result) {
    if (device >= 32 || function >= 8)
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(PciServiceRequest::Query);
    request.length   = 4;
    request.data[0]  = segment;
    request.data[1]  = bus;
    request.data[2]  = device;
    request.data[3]  = function;
    const int status = pciConnection.call(request, reply);
    if (status) return status;
    if (reply.length < 1 + (sizeof(wire::PciFunctionInfo) + 7) / 8)
        return static_cast<int>(ErrorCode::IoError);
    wire::PciFunctionInfo value{};
    Runtime::copyBytes(&value, &reply.data[1], sizeof(value));
    if (value.segment != segment || value.bus != bus ||
        value.device != device || value.function != function ||
        value.vendorId == 0xffff)
        return static_cast<int>(ErrorCode::IoError);
    result.decode(&value);
    return 0;
}
int Device::Pci::open(uint16_t segment, uint8_t bus, uint8_t device,
                      uint8_t function) {
    return query(segment, bus, device, function, *this);
}
int Device::Pci::close() {
    *this = Pci();
    return 0;
}
int Device::Pci::readConfig(uint16_t offset, uint32_t& value) {
    if (!valid_) return static_cast<int>(ErrorCode::InvalidHandle);
    if (offset & 3U) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PciServiceRequest::ReadConfig);
    request.length  = 3;
    request.data[0] = offset;
    encode(&request.data[1]);
    const int status = pciConnection.call(request, reply);
    if (status) return status;
    if (reply.length < 2 || reply.data[1] > UINT32_MAX)
        return static_cast<int>(ErrorCode::IoError);
    value = reply.data[1];
    return 0;
}
int Device::Pci::writeConfig(uint16_t offset, uint32_t value) {
    if (!valid_) return static_cast<int>(ErrorCode::InvalidHandle);
    if (offset & 3U) return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PciServiceRequest::WriteConfig);
    request.length  = 4;
    request.data[0] = offset;
    encode(&request.data[1]);
    request.data[3] = value;
    return pciConnection.call(request, reply);
}
Device::Device() = default;
Device::~Device() { close(); }
int Device::open(const Pci& function) {
    if (handle_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!function.valid_)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type   = static_cast<uint64_t>(DeviceOperation::ClaimDevice);
    request.length = 5;
    function.encode(request.data);
    const int64_t value = services::RequestChannel(CoreServiceKind::Device)
                              .result(request, reply);
    if (value <= 0)
        return error_ = value ? static_cast<int>(value)
                              : static_cast<int>(ErrorCode::InvalidHandle);
    handle_       = value;
    function_     = function;
    return error_ = 0;
}
int Device::stop() {
    if (!handle_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(DeviceOperation::StopDevice);
    request.length  = 5;
    request.data[0] = handle_;
    return error_   = services::RequestChannel(CoreServiceKind::Device)
                        .result(request, reply);
}
int Device::Dma::create(Device& device, uint64_t size, uint64_t alignment) {
    if (handle_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!device.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (!size || size > 8 * 1024 * 1024ULL || !alignment ||
        (alignment & (alignment - 1)) || alignment > 8 * 1024 * 1024ULL)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(DeviceOperation::CreateDma);
    request.length   = 5;
    request.data[0]  = size;
    request.data[1]  = device.handle_;
    request.data[2]  = alignment;
    const int status = services::RequestChannel(CoreServiceKind::Device)
                           .result(request, reply);
    if (status) return error_ = status;
    if (reply.length < 1 + (sizeof(wire::DmaBufferInfo) + 7) / 8)
        return error_ = static_cast<int>(ErrorCode::IoError);
    wire::DmaBufferInfo value{};
    Runtime::copyBytes(&value, &reply.data[1], sizeof(value));
    handle_         = value.handle;
    virtualAddress_ = value.virtualAddress;
    deviceAddress_  = value.deviceAddress;
    size_           = value.size;
    if (!handle_ || !virtualAddress_ || size_ < size ||
        (deviceAddress_ & (alignment - 1))) {
        close();
        return error_ = static_cast<int>(ErrorCode::IoError);
    }
    return error_ = 0;
}
int Device::Mmio::open(Device& device, uint8_t bar, uint64_t offset,
                       uint64_t size) {
    if (handle_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!device.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    const uint64_t delta = offset & 4095;
    if (bar >= 6 || !size || size > UINT64_MAX - 4095 - delta ||
        offset > UINT64_MAX - size)
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(DeviceOperation::MapBar);
    request.length  = 6;
    request.data[0] = bar;
    request.data[1] = size + delta;
    request.data[2] = offset - delta;
    device.function_.encode(&request.data[3]);
    request.data[5]     = device.handle_;
    const int64_t value = services::RequestChannel(CoreServiceKind::Device)
                              .result(request, reply);
    if (value <= 0)
        return error_ = value ? static_cast<int>(value)
                              : static_cast<int>(ErrorCode::InvalidHandle);
    handle_ = value;
    if (reply.length < 3 || !reply.data[1] || reply.data[2] < size + delta ||
        reply.data[1] > UINT64_MAX - delta) {
        close();
        return error_ = static_cast<int>(ErrorCode::IoError);
    }
    address_      = reply.data[1] + delta;
    size_         = reply.data[2] - delta;
    bar_          = bar;
    return error_ = 0;
}
int Device::Frame::open(uint64_t address) {
    if (handle_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (address &&
        (address < 0x10000 || address >= 0x800000000000ULL || (address & 4095)))
        return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type        = static_cast<uint64_t>(DeviceOperation::Framebuffer);
    request.length      = 5;
    request.data[0]     = address;
    const int64_t value = services::RequestChannel(CoreServiceKind::Device)
                              .result(request, reply);
    if (value <= 0)
        return error_ = value ? static_cast<int>(value)
                              : static_cast<int>(ErrorCode::InvalidHandle);
    handle_ = value;
    if (reply.length < 1 + (sizeof(wire::DisplayInfo) + 7) / 8) {
        close();
        return error_ = static_cast<int>(ErrorCode::IoError);
    }
    decode(&reply.data[1]);
    if (!address_ || !width_ || !height_ || !pitch_ ||
        static_cast<uint64_t>(pitch_) * height_ > size_) {
        close();
        return error_ = static_cast<int>(ErrorCode::IoError);
    }
    return error_ = 0;
}
int Device::Frame::refresh() {
    if (!handle_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type     = static_cast<uint64_t>(DeviceOperation::FrameInfo);
    request.length   = 5;
    request.data[0]  = handle_;
    const int status = services::RequestChannel(CoreServiceKind::Device)
                           .result(request, reply);
    if (status) return error_ = status;
    if (reply.length < 1 + (sizeof(wire::DisplayInfo) + 7) / 8)
        return error_ = static_cast<int>(ErrorCode::IoError);
    wire::DisplayInfo info{};
    Runtime::copyBytes(&info, &reply.data[1], sizeof(info));
    if (!info.address || !info.width || !info.height || !info.pitch ||
        static_cast<uint64_t>(info.pitch) * info.height > info.size ||
        static_cast<uint8_t>(info.format) > 5)
        return error_ = static_cast<int>(ErrorCode::IoError);
    decode(&info);
    return error_ = 0;
}
void Device::Frame::decode(const void* bytes) {
    wire::DisplayInfo info{};
    Runtime::copyBytes(&info, bytes, sizeof(info));
    address_       = info.address;
    size_          = info.size;
    width_         = info.width;
    height_        = info.height;
    pitch_         = info.pitch;
    bitsPerPixel_  = info.bitsPerPixel;
    format_        = static_cast<Format>(info.format);
    redPosition_   = info.redPosition;
    greenPosition_ = info.greenPosition;
    bluePosition_  = info.bluePosition;
}
int Device::Irq::create(Device& device, uint8_t irq, Ipc& endpoint,
                        bool levelTriggered) {
    if (handle_) return error_ = static_cast<int>(ErrorCode::Busy);
    if (!device.getValid() || !endpoint.getValid())
        return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    if (irq > 15) return error_ = static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type        = static_cast<uint64_t>(DeviceOperation::BindIrq);
    request.length      = 5;
    request.data[0]     = irq;
    request.data[1]     = endpoint.getHandle();
    request.data[2]     = levelTriggered;
    request.data[3]     = device.handle_;
    const int64_t value = services::RequestChannel(CoreServiceKind::Device)
                              .result(request, reply);
    if (value <= 0)
        return error_ = value ? static_cast<int>(value)
                              : static_cast<int>(ErrorCode::InvalidHandle);
    handle_       = value;
    irq_          = irq;
    return error_ = 0;
}
int Device::Irq::acknowledge() {
    if (!handle_) return error_ = static_cast<int>(ErrorCode::InvalidHandle);
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(DeviceOperation::AcknowledgeIrq);
    request.length  = 5;
    request.data[0] = handle_;
    return error_   = services::RequestChannel(CoreServiceKind::Device)
                        .result(request, reply);
}
int Device::close() {
    if (handle_) {
        const int status = os::Ipc::Handle(handle_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
    }
    handle_       = {};
    function_     = {};
    return error_ = 0;
}
Device& Device::operator=(Device&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        handle_         = other.handle_;
        other.handle_   = {};
        function_       = other.function_;
        other.function_ = {};
        error_          = other.error_;
        other.error_    = 0;
    }
    return *this;
}
bool Device::getValid() const { return handle_ != 0; }
int Device::getError() const { return error_; }
Device::Dma::Dma() = default;
Device::Dma::~Dma() { close(); }
int Device::Dma::close() {
    if (handle_) {
        const int status = os::Ipc::Handle(handle_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
    }
    handle_         = {};
    virtualAddress_ = {};
    deviceAddress_  = {};
    size_           = {};
    return error_   = 0;
}
Device::Dma& Device::Dma::operator=(Dma&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        handle_               = other.handle_;
        other.handle_         = {};
        virtualAddress_       = other.virtualAddress_;
        other.virtualAddress_ = {};
        deviceAddress_        = other.deviceAddress_;
        other.deviceAddress_  = {};
        size_                 = other.size_;
        other.size_           = {};
        error_                = other.error_;
        other.error_          = 0;
    }
    return *this;
}
bool Device::Dma::getValid() const { return handle_ != 0; }
int Device::Dma::getError() const { return error_; }
Device::Mmio::Mmio() = default;
Device::Mmio::~Mmio() { close(); }
int Device::Mmio::close() {
    if (handle_) {
        const int status = os::Ipc::Handle(handle_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
    }
    handle_       = {};
    address_      = {};
    size_         = {};
    bar_          = {};
    return error_ = 0;
}
Device::Mmio& Device::Mmio::operator=(Mmio&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        handle_        = other.handle_;
        other.handle_  = {};
        address_       = other.address_;
        other.address_ = {};
        size_          = other.size_;
        other.size_    = {};
        bar_           = other.bar_;
        other.bar_     = {};
        error_         = other.error_;
        other.error_   = 0;
    }
    return *this;
}
bool Device::Mmio::getValid() const { return handle_ != 0; }
int Device::Mmio::getError() const { return error_; }
Device::Frame::Frame() = default;
Device::Frame::~Frame() { close(); }
int Device::Frame::close() {
    if (handle_) {
        const int status = os::Ipc::Handle(handle_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
    }
    handle_        = {};
    address_       = {};
    size_          = {};
    width_         = {};
    height_        = {};
    pitch_         = {};
    bitsPerPixel_  = {};
    redPosition_   = {};
    greenPosition_ = {};
    bluePosition_  = {};
    format_        = {};
    return error_  = 0;
}
Device::Frame& Device::Frame::operator=(Frame&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        handle_              = other.handle_;
        other.handle_        = {};
        address_             = other.address_;
        other.address_       = {};
        size_                = other.size_;
        other.size_          = {};
        width_               = other.width_;
        other.width_         = {};
        height_              = other.height_;
        other.height_        = {};
        pitch_               = other.pitch_;
        other.pitch_         = {};
        bitsPerPixel_        = other.bitsPerPixel_;
        other.bitsPerPixel_  = {};
        redPosition_         = other.redPosition_;
        other.redPosition_   = {};
        greenPosition_       = other.greenPosition_;
        other.greenPosition_ = {};
        bluePosition_        = other.bluePosition_;
        other.bluePosition_  = {};
        format_              = other.format_;
        other.format_        = {};
        error_               = other.error_;
        other.error_         = 0;
    }
    return *this;
}
bool Device::Frame::getValid() const { return handle_ != 0; }
int Device::Frame::getError() const { return error_; }
Device::Irq::Irq() = default;
Device::Irq::~Irq() { close(); }
int Device::Irq::close() {
    if (handle_) {
        const int status = os::Ipc::Handle(handle_, false).close();
        if (status && status != static_cast<int>(ErrorCode::InvalidHandle))
            return error_ = status;
    }
    handle_       = {};
    irq_          = {};
    return error_ = 0;
}
Device::Irq& Device::Irq::operator=(Irq&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            error_ = static_cast<int>(ErrorCode::Busy);
            return *this;
        }
        handle_       = other.handle_;
        other.handle_ = {};
        irq_          = other.irq_;
        other.irq_    = {};
        error_        = other.error_;
        other.error_  = 0;
    }
    return *this;
}
bool Device::Irq::getValid() const { return handle_ != 0; }
int Device::Irq::getError() const { return error_; }
uint64_t Device::getHandle() const { return handle_; }
uint16_t Device::Pci::getSegment() const { return segment_; }
uint8_t Device::Pci::getBus() const { return bus_; }
uint8_t Device::Pci::getDevice() const { return device_; }
uint8_t Device::Pci::getFunction() const { return function_; }
uint8_t Device::Pci::getClassCode() const { return classCode_; }
uint8_t Device::Pci::getSubclass() const { return subclass_; }
uint8_t Device::Pci::getProgrammingInterface() const {
    return programmingInterface_;
}
uint16_t Device::Pci::getVendorId() const { return vendorId_; }
uint16_t Device::Pci::getDeviceId() const { return deviceId_; }
uint8_t Device::Pci::getHeaderType() const { return headerType_; }
uint64_t Device::Dma::getHandle() const { return handle_; }
uint64_t Device::Dma::getVirtualAddress() const { return virtualAddress_; }
uint64_t Device::Dma::getDeviceAddress() const { return deviceAddress_; }
uint64_t Device::Dma::getSize() const { return size_; }
uint64_t Device::Mmio::getAddress() const { return address_; }
uint64_t Device::Mmio::getSize() const { return size_; }
uint8_t Device::Mmio::getBarIndex() const { return bar_; }
uint64_t Device::Frame::getAddress() const { return address_; }
uint64_t Device::Frame::getSize() const { return size_; }
uint32_t Device::Frame::getWidth() const { return width_; }
uint32_t Device::Frame::getHeight() const { return height_; }
uint32_t Device::Frame::getPitch() const { return pitch_; }
uint8_t Device::Frame::getBitsPerPixel() const { return bitsPerPixel_; }
Device::Frame::Format Device::Frame::getFormat() const { return format_; }
uint8_t Device::Frame::getRedPosition() const { return redPosition_; }
uint8_t Device::Frame::getGreenPosition() const { return greenPosition_; }
uint8_t Device::Frame::getBluePosition() const { return bluePosition_; }
uint64_t Device::Irq::getHandle() const { return handle_; }
uint8_t Device::Irq::getIrq() const { return irq_; }

detail::PciQuery::PciQuery() : Sys::Transport(CoreServiceKind::Pci) {}
int detail::PciQuery::enumerate(wire::PciFunctionInfo* functions,
                                uint32_t capacity) {
    if (!functions || !capacity || capacity > 64)
        return static_cast<int>(ErrorCode::InvalidArgument);
    IpcMessage request{}, reply{};
    request.type        = static_cast<uint64_t>(PciOperation::Enumerate);
    request.length      = 5;
    request.data[0]     = capacity;
    const int64_t count = result(request, reply);
    if (count < 0) return count;
    if (static_cast<uint64_t>(count) > capacity ||
        reply.length < 1 + (count * sizeof(wire::PciFunctionInfo) + 7) / 8)
        return static_cast<int>(ErrorCode::IoError);
    Runtime::copyBytes(functions, &reply.data[1], count * sizeof(*functions));
    return count;
}
int detail::PciQuery::read(const wire::PciFunctionInfo& function,
                           uint16_t offset, uint32_t& value) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PciOperation::ReadConfig);
    request.length  = 5;
    request.data[0] = offset;
    Runtime::copyBytes(&request.data[1], &function, sizeof(function));
    const int status = result(request, reply);
    if (status) return status;
    if (reply.length < 2 || reply.data[1] > UINT32_MAX)
        return static_cast<int>(ErrorCode::IoError);
    value = reply.data[1];
    return 0;
}
int detail::PciQuery::write(const wire::PciFunctionInfo& function,
                            uint16_t offset, uint32_t value,
                            uint64_t requester) {
    IpcMessage request{}, reply{};
    request.type    = static_cast<uint64_t>(PciOperation::WriteConfig);
    request.length  = 5;
    request.data[0] = offset;
    Runtime::copyBytes(&request.data[1], &function, sizeof(function));
    request.data[3] = value;
    request.data[4] = requester;
    return result(request, reply);
}
int detail::PciQuery::query(uint16_t segment, uint8_t bus, uint8_t device,
                            uint8_t function, wire::PciFunctionInfo& result) {
    if (device >= 32 || function >= 8)
        return static_cast<int>(ErrorCode::InvalidArgument);
    wire::PciFunctionInfo info{};
    info.segment      = segment;
    info.bus          = bus;
    info.device       = device;
    info.function     = function;
    uint32_t identity = 0, kind = 0, header = 0;
    int status = read(info, 0, identity);
    if (status) return status;
    if ((identity & 0xffff) == 0xffff)
        return static_cast<int>(ErrorCode::NotFound);
    if ((status = read(info, 8, kind)) || (status = read(info, 12, header)))
        return status;
    info.vendorId             = identity;
    info.deviceId             = identity >> 16;
    info.classCode            = kind >> 24;
    info.subclass             = kind >> 16;
    info.programmingInterface = kind >> 8;
    info.headerType           = header >> 16;
    result                    = info;
    return 0;
}
}  // namespace os
