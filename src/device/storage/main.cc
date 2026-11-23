#include <os/detail/client.h>
#include <os/device.h>
#include <os/ipc.h>
#include <os/mm.h>
#include <os/runtime.h>
#include <os/service.h>
#include <os/storage.h>
#include <os/sync.h>
#include <os/task.h>

#include "service/common/protocol.h"
#include "service/common/records.h"
#include "service/device/storage/ahci.h"
#include "service/device/storage/block_backend.h"
#include "service/device/storage/boot_image.h"
#include "service/device/storage/ide.h"
#include "service/device/storage/partition.h"
#include "service/device/storage/range_reply.h"
#include "service/device/storage/request_lifecycle.h"
#include "service/device/storage/request_queue.h"
#include "service/device/storage/statistics.h"

constexpr uint16_t virtioVendorId = 0x1af4;
constexpr uint16_t virtioBlockDeviceId = 0x1042;
constexpr uint8_t virtioCapabilityId = 0x09;
constexpr uint8_t commonConfigurationType = 1;
constexpr uint8_t notifyConfigurationType = 2;
constexpr uint8_t isrConfigurationType = 3;
constexpr uint8_t deviceConfigurationType = 4;
constexpr uint8_t statusAcknowledge = 1;
constexpr uint8_t statusDriver = 2;
constexpr uint8_t statusDriverOk = 4;
constexpr uint8_t statusFeaturesOk = 8;
constexpr uint64_t featureReadOnly = 1ULL << 5;
constexpr uint64_t featureSizeMax = 1ULL << 1;
constexpr uint64_t featureSegmentMax = 1ULL << 2;
constexpr uint64_t featureBlockSize = 1ULL << 6;
constexpr uint64_t featureFlush = 1ULL << 9;
constexpr uint64_t featureCacheConfig = 1ULL << 11;
constexpr uint64_t featureVersionOne = 1ULL << 32;
constexpr uint16_t descriptorNext = 1;
constexpr uint16_t descriptorWrite = 2;
constexpr uint32_t blockSize = 512;
constexpr uint32_t queueSize = 32;
constexpr uint32_t requestSlotCount = 7;
constexpr uint32_t requestDmaBytes = 4096;
constexpr uint64_t ioTimeoutNanoseconds = 2ULL * 1000 * 1000 * 1000;
constexpr uint64_t irqNotification = 0x495251;
constexpr uint64_t queueDmaAddress = 0x0000000031000000ULL;
constexpr uint64_t irqStackAddress = 0x0000000030010000ULL;
constexpr uint64_t irqStackSize = 64 * 1024;
constexpr uint64_t workerStackAddress = 0x0000000030020000ULL;
constexpr uint64_t workerStackSize = 64 * 1024;
constexpr uint32_t workerThreadCount = 3;
constexpr uint64_t mmioBaseAddress = 0x0000000040000000ULL;
constexpr uint32_t maximumCapabilityWalk = 48;

struct VirtioPciCapability {
        bool present;
        uint8_t bar;
        uint32_t offset;
        uint32_t length;
        uint32_t notifyMultiplier;
};

struct VirtQueueDescriptor {
        uint64_t address;
        uint32_t length;
        uint16_t flags;
        uint16_t next;
} __attribute__((packed));

struct VirtQueueUsedElement {
        uint32_t id;
        uint32_t length;
} __attribute__((packed));

struct VirtioBlockRequest {
        uint32_t type;
        uint32_t reserved;
        uint64_t sector;
} __attribute__((packed));

static uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

class PciConfiguration {
    public:
        explicit PciConfiguration(const os::Device::Pci& function)
            : function_(function) {
        }

        bool read32(uint16_t offset, uint32_t& value) const {
            os::Device::Pci function = function_;
            return function.readConfig(offset, value) == 0;
        }

        bool read8(uint16_t offset, uint8_t& value) const {
            uint32_t word;
            if (!read32(static_cast<uint16_t>(offset & ~3U), word)) {
                return false;
            }

            value = static_cast<uint8_t>(word >> ((offset & 3U) * 8));
            return true;
        }

        bool writeCommand(uint16_t flags) const {
            uint32_t command;
            os::Device::Pci function = function_;
            return read32(4, command) &&
                   function.writeConfig(4, (command | flags) & 0xffff) == 0;
        }

    private:
        const os::Device::Pci& function_;
};

template <typename Type>
static Type readMmio(volatile uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<volatile Type*>(base + offset);
}

template <typename Type>
static void writeMmio(volatile uint8_t* base, uint32_t offset, Type value) {
    *reinterpret_cast<volatile Type*>(base + offset) = value;
}

class VirtioPciTransport {
    public:
        bool init(const os::Device::Pci& function, os::Device& owner) {
            function_ = function;
            owner_ = &owner;
            PciConfiguration configuration(function_);
            uint32_t statusCommand;
            uint8_t capabilityOffset;
            if (!configuration.read32(4, statusCommand) ||
                !configuration.read8(0x34, capabilityOffset)) {
                os::Sys().log(
                    "[STORAGE] unable to read PCI capability pointer\n");
                return false;
            }

            if ((statusCommand & (1U << 20)) == 0) {
                os::Sys().log("[STORAGE] PCI capability list is unavailable\n");
                return false;
            }

            if (!scanCapabilities(configuration, capabilityOffset)) {
                os::Sys().log(
                    "[STORAGE] required VirtIO PCI capabilities are missing\n");
                return false;
            }

            if (!mapBars()) {
                os::Sys().log("[STORAGE] VirtIO PCI BAR mapping failed\n");
                return false;
            }

            if (!configuration.writeCommand(0x0006)) {
                os::Sys().log(
                    "[STORAGE] PCI memory/bus-master enable failed\n");
                return false;
            }

            common_ =
                mappedBars_[commonCapability_.bar] + commonCapability_.offset;
            notify_ =
                mappedBars_[notifyCapability_.bar] + notifyCapability_.offset;
            isr_ = mappedBars_[isrCapability_.bar] + isrCapability_.offset;
            device_ =
                mappedBars_[deviceCapability_.bar] + deviceCapability_.offset;
            return true;
        }

        volatile uint8_t* common() const {
            return common_;
        }

        volatile uint8_t* device() const {
            return device_;
        }

        uint32_t deviceConfigSize() const {
            return deviceCapability_.length;
        }

        bool closeMappings() {
            bool closed = true;
            for (os::Device::Mmio& mapping : barMappings_) {
                if (mapping.close()) {
                    closed = false;
                }
            }

            return closed;
        }

        void notifyQueue(uint16_t queue, uint16_t notifyOffset) {
            writeMmio<uint16_t>(notify_, notifyOffset * notifyMultiplier_,
                                queue);
        }

        void acknowledgeInterrupt() {
            static_cast<void>(readMmio<uint8_t>(isr_, 0));
        }

        uint8_t interruptLine() const {
            PciConfiguration configuration(function_);
            uint8_t line = 0xff;
            configuration.read8(0x3c, line);
            return line;
        }

    private:
        bool scanCapabilities(const PciConfiguration& configuration,
                              uint8_t offset) {
            uint32_t walked = 0;
            while (offset >= 0x40 && walked++ < maximumCapabilityWalk) {
                uint8_t id;
                uint8_t next;
                if (!configuration.read8(offset, id) ||
                    !configuration.read8(offset + 1, next)) {
                    return false;
                }

                if (id == virtioCapabilityId &&
                    !readVirtioCapability(configuration, offset)) {
                    return false;
                }

                if (next == 0 || next == offset) {
                    break;
                }

                offset = next;
            }

            notifyMultiplier_ = notifyCapability_.notifyMultiplier;
            if (!commonCapability_.present) {
                os::Sys().log("[STORAGE] VirtIO common capability missing\n");
            }

            if (!notifyCapability_.present) {
                os::Sys().log("[STORAGE] VirtIO notify capability missing\n");
            }

            if (!isrCapability_.present) {
                os::Sys().log("[STORAGE] VirtIO ISR capability missing\n");
            }

            if (!deviceCapability_.present) {
                os::Sys().log("[STORAGE] VirtIO device capability missing\n");
            }

            if (notifyMultiplier_ == 0) {
                os::Sys().log("[STORAGE] VirtIO notify multiplier is zero\n");
            }

            return commonCapability_.present && notifyCapability_.present &&
                   isrCapability_.present && deviceCapability_.present &&
                   notifyMultiplier_ != 0;
        }

        bool readVirtioCapability(const PciConfiguration& configuration,
                                  uint8_t offset) {
            uint8_t length;
            uint8_t type;
            uint8_t bar;
            uint32_t regionOffset;
            uint32_t regionLength;
            if (!configuration.read8(offset + 2, length) ||
                !configuration.read8(offset + 3, type)) {
                return false;
            }

            if (type != commonConfigurationType &&
                type != notifyConfigurationType &&
                type != isrConfigurationType &&
                type != deviceConfigurationType) {
                return true;
            }

            if (!configuration.read8(offset + 4, bar) || bar >= 6 ||
                length < 16 ||
                !configuration.read32(offset + 8, regionOffset) ||
                !configuration.read32(offset + 12, regionLength) ||
                regionLength == 0) {
                return false;
            }

            VirtioPciCapability* capability = nullptr;
            if (type == commonConfigurationType) {
                capability = &commonCapability_;
            } else if (type == notifyConfigurationType) {
                capability = &notifyCapability_;
            } else if (type == isrConfigurationType) {
                capability = &isrCapability_;
            } else if (type == deviceConfigurationType) {
                capability = &deviceCapability_;
            } else {
                return true;
            }

            capability->present = true;
            capability->bar = bar;
            capability->offset = regionOffset;
            capability->length = regionLength;
            if (type == notifyConfigurationType) {
                if (length < 20 ||
                    !configuration.read32(offset + 16,
                                          capability->notifyMultiplier)) {
                    return false;
                }
            }

            const uint64_t end =
                static_cast<uint64_t>(regionOffset) + regionLength;
            if (end > 64 * 1024) {
                return false;
            }

            const uint32_t required = alignUp(static_cast<uint32_t>(end), 4096);
            if (required > requiredBarSize_[bar]) {
                requiredBarSize_[bar] = required;
            }

            return true;
        }

        bool mapBars() {
            for (uint8_t bar = 0; bar < 6; ++bar) {
                if (requiredBarSize_[bar] == 0) {
                    continue;
                }

                if (!owner_ || barMappings_[bar].open(*owner_, bar, 0,
                                                      requiredBarSize_[bar])) {
                    return false;
                }

                mappedBars_[bar] = reinterpret_cast<volatile uint8_t*>(
                    barMappings_[bar].getAddress());
            }

            return true;
        }

        os::Device::Pci function_{};
        VirtioPciCapability commonCapability_{};
        VirtioPciCapability notifyCapability_{};
        VirtioPciCapability isrCapability_{};
        VirtioPciCapability deviceCapability_{};
        uint32_t requiredBarSize_[6]{};
        volatile uint8_t* mappedBars_[6]{};
        volatile uint8_t* common_ = nullptr;
        volatile uint8_t* notify_ = nullptr;
        volatile uint8_t* isr_ = nullptr;
        volatile uint8_t* device_ = nullptr;
        uint32_t notifyMultiplier_ = 0;
        os::Device* owner_{};
        os::Device::Mmio barMappings_[6];
};

class VirtioBlockDriver final : public storage::BlockBackend {
    public:
        bool init(const os::Device::Pci& function, uint32_t deviceIndex,
                  os::Ipc& irqEndpoint, os::Device::Irq (&boundIrqs)[16]) {
            function_ = function;
            deviceIndex_ = deviceIndex;
            const uint64_t claimStart = os::Sys().readMonotonicTime();
            do {
                if (device_.open(function_)) {
                    os::sleep(10);
                }
            } while (!device_.getValid() &&
                     !storage::expired(claimStart,
                                       os::Sys().readMonotonicTime(),
                                       ioTimeoutNanoseconds));
            if (!device_.getValid()) {
                return false;
            }

            if (!transport_.init(function_, device_)) {
                os::Sys().log(
                    "[STORAGE] VirtIO PCI capability/BAR setup failed\n");
                stop();
                return false;
            }

            volatile uint8_t* common = transport_.common();
            writeMmio<uint8_t>(common, 20, 0);
            const uint64_t resetStart = os::Sys().readMonotonicTime();
            while (readMmio<uint8_t>(common, 20) != 0) {
                if (storage::expired(resetStart, os::Sys().readMonotonicTime(),
                                     ioTimeoutNanoseconds)) {
                    stop();
                    return false;
                }

                os::yield();
            }

            writeMmio<uint8_t>(common, 20, statusAcknowledge | statusDriver);
            writeMmio<uint32_t>(common, 0, 0);
            const uint64_t lowFeatures = readMmio<uint32_t>(common, 4);
            writeMmio<uint32_t>(common, 0, 1);
            const uint64_t highFeatures = readMmio<uint32_t>(common, 4);
            const uint64_t deviceFeatures = lowFeatures | (highFeatures << 32);
            if ((deviceFeatures & featureVersionOne) == 0) {
                os::Sys().log("[STORAGE] VirtIO 1.0 feature is unavailable\n");
                stop();
                return false;
            }

            negotiatedFeatures_ =
                featureVersionOne |
                (deviceFeatures &
                 (featureReadOnly | featureFlush | featureSizeMax |
                  featureSegmentMax | featureBlockSize | featureCacheConfig));
            writeMmio<uint32_t>(common, 8, 0);
            writeMmio<uint32_t>(common, 12,
                                static_cast<uint32_t>(negotiatedFeatures_));
            writeMmio<uint32_t>(common, 8, 1);
            writeMmio<uint32_t>(
                common, 12, static_cast<uint32_t>(negotiatedFeatures_ >> 32));
            writeMmio<uint8_t>(
                common, 20,
                statusAcknowledge | statusDriver | statusFeaturesOk);
            if ((readMmio<uint8_t>(common, 20) & statusFeaturesOk) == 0) {
                os::Sys().log(
                    "[STORAGE] VirtIO feature negotiation was rejected\n");
                stop();
                return false;
            }

            if (!readGeometry()) {
                os::Sys().log("[STORAGE] invalid VirtIO Block geometry\n");
                stop();
                return false;
            }

            if (!initializeQueue()) {
                os::Sys().log("[STORAGE] VirtQueue setup failed\n");
                stop();
                return false;
            }

            readOnly_ = (negotiatedFeatures_ & featureReadOnly) != 0;
            const uint8_t irq = transport_.interruptLine();
            if (irq > 15) {
                stop();
                return false;
            }

            if (!boundIrqs[irq].getValid()) {
                boundIrqs[irq].create(device_, irq, irqEndpoint, true);
            }

            if (!boundIrqs[irq].getValid()) {
                os::Sys().log("[STORAGE] VirtIO Block IRQ binding failed\n");
                stop();
                return false;
            }

            writeMmio<uint8_t>(common, 20,
                               statusAcknowledge | statusDriver |
                                   statusFeaturesOk | statusDriverOk);
            online_ = true;
            return true;
        }

        storage::BlockInfo info() const override {
            os::Mutex::Guard guard(requestLock_);
            return {blockCount_,
                    logicalBlockSize_,
                    maxTransferBlocks_,
                    online_,
                    readOnly_,
                    cacheMode_,
                    (negotiatedFeatures_ & featureFlush) != 0};
        }

        storage::TransferResult read(uint64_t first, uint32_t count,
                                     void* buffer, size_t bytes) override {
            using Error = os::Storage::Error;
            Error error =
                info().validateTransfer(first, count, buffer, bytes, false);
            if (error != Error::Success) {
                return {error, 0};
            }

            error = submit(0, first, buffer, true, count);
            return {error, error == Error::Success ? count : 0};
        }

        storage::TransferResult write(uint64_t first, uint32_t count,
                                      const void* buffer,
                                      size_t bytes) override {
            using Error = os::Storage::Error;
            Error error =
                info().validateTransfer(first, count, buffer, bytes, true);
            if (error != Error::Success) {
                return {error, 0};
            }

            error = submit(1, first, const_cast<void*>(buffer), false, count);
            return {error, error == Error::Success ? count : 0};
        }

        os::Storage::Error flush() override {
            using Error = os::Storage::Error;
            using Action = storage::FlushAction;
            switch (info().getFlushAction()) {
                case Action::Complete:
                    return Error::Success;
                case Action::Command:
                    return submit(4, 0, nullptr, false, 0);
                case Action::Unsupported:
                    return Error::NotSupported;
                case Action::Unknown:
                    return Error::CapabilityUnknown;
                case Action::Offline:
                    return Error::DeviceOffline;
            }

            return Error::Io;
        }

        /* 先发布整批描述符，只通知设备一次，再逐项收取完成结果。 */
        void submitBatch(storage::BlockTransfer* requests,
                         uint32_t count) override {
            using Error = os::Storage::Error;
            for (uint32_t base = 0; base < count; base += requestSlotCount) {
                const uint32_t length = count - base < requestSlotCount
                                            ? count - base
                                            : requestSlotCount;
                uint32_t slots[requestSlotCount];
                uint64_t started[requestSlotCount];
                Error errors[requestSlotCount];
                bool published = false;
                for (uint32_t index = 0; index < length; ++index) {
                    storage::BlockTransfer& request = requests[base + index];
                    errors[index] = info().validateTransfer(
                        request.getFirst(), request.getCount(),
                        request.getBuffer(), request.getBytes(),
                        request.isWrite());
                    if (errors[index] == Error::Success) {
                        errors[index] =
                            begin(request.isWrite() ? 1 : 0, request.getFirst(),
                                  request.getBuffer(), !request.isWrite(),
                                  request.getCount(), started[index],
                                  slots[index], false);
                        published |= errors[index] == Error::Success;
                    }
                }

                if (published) {
                    os::Mutex::Guard guard(requestLock_);
                    if (online_) {
                        transport_.notifyQueue(0, notifyOffset_);
                    }
                }

                for (uint32_t index = 0; index < length; ++index) {
                    storage::BlockTransfer& request = requests[base + index];
                    if (errors[index] == Error::Success) {
                        errors[index] =
                            finish(slots[index], started[index],
                                   request.isWrite() ? 1 : 0,
                                   request.getBuffer(), request.getCount());
                    }

                    request.setResult(
                        {errors[index], errors[index] == Error::Success
                                            ? request.getCount()
                                            : 0});
                }
            }
        }

        storage::StopResult stop() override {
            using Stop = storage::StopResult;
            os::Mutex::Guard stopGuard(stopLock_);
            if (stopRequested_) {
                return stopResult_;
            }

            {
                os::Mutex::Guard guard(requestLock_);
                freezeLocked();
            }

            stopRequested_ = true;
            const int result = device_.getValid() ? device_.stop() : 0;
            if (result == 0) {
                stopResult_ = Stop::Stopped;
            } else if (result == static_cast<int>(os::ErrorCode::IoError)) {
                stopResult_ = Stop::Quarantined;
            }

            // Closing handles drops mappings and references only. The kernel
            // retains the physical pages until the domain is confirmed stopped.
            bool closed = transport_.closeMappings();
            for (os::Device::Dma& dma : requestDma_) {
                if (dma.close()) {
                    closed = false;
                }
            }

            if (queueDma_.close()) {
                closed = false;
            }

            if (device_.close()) {
                closed = false;
            }

            if (!closed) {
                os::Sys().log("[STORAGE] device handle cleanup incomplete\n");
            }

            return stopResult_;
        }

        bool handleInterrupt() {
            os::Mutex::Guard guard(requestLock_);
            if (!online_) {
                return false;
            }

            transport_.acknowledgeInterrupt();
            if (taskUsedLocked()) {
                return false;
            }

            freezeLocked();
            return true;
        }

    private:
        void freezeLocked() {
            online_ = false;
            slots_.freeze();
        }

        bool readGeometry() {
            const bool hasSize = (negotiatedFeatures_ & featureBlockSize) != 0;
            const bool hasMax = (negotiatedFeatures_ & featureSizeMax) != 0;
            const bool hasSegments =
                (negotiatedFeatures_ & featureSegmentMax) != 0;
            const bool hasCache =
                (negotiatedFeatures_ & featureCacheConfig) != 0;
            const uint32_t required = hasCache      ? 33
                                      : hasSize     ? 24
                                      : hasSegments ? 16
                                      : hasMax      ? 12
                                                    : 8;
            if (transport_.deviceConfigSize() < required) {
                return false;
            }

            volatile uint8_t* config = transport_.device();
            for (uint32_t attempt = 0; attempt < 8; ++attempt) {
                const uint8_t generation =
                    readMmio<uint8_t>(transport_.common(), 21);
                const uint64_t sectors =
                    readMmio<uint32_t>(config, 0) |
                    (uint64_t(readMmio<uint32_t>(config, 4)) << 32);
                const uint32_t logical = hasSize
                                             ? readMmio<uint32_t>(config, 20)
                                             : storage::virtioSectorBytes;
                constexpr uint32_t slotPayload =
                    requestDmaBytes - sizeof(VirtioBlockRequest) - 1;
                const uint32_t sizeMax =
                    hasMax ? readMmio<uint32_t>(config, 8) : slotPayload;
                const uint32_t segments =
                    hasSegments ? readMmio<uint32_t>(config, 12) : 1;
                const uint32_t writeback =
                    hasCache ? readMmio<uint8_t>(config, 32) : 0;
                if (generation != readMmio<uint8_t>(transport_.common(), 21)) {
                    continue;
                }

                if (logical < storage::virtioSectorBytes ||
                    (logical & (logical - 1)) || !sectors ||
                    sectors > UINT64_MAX / storage::virtioSectorBytes ||
                    sectors % (logical / storage::virtioSectorBytes)) {
                    return false;
                }

                blockCount_ = sectors / (logical / storage::virtioSectorBytes);
                logicalBlockSize_ = logical;
                const uint32_t payload =
                    sizeMax < slotPayload ? sizeMax : slotPayload;
                maxTransferBlocks_ = segments ? payload / logical : 0;
                cacheMode_ =
                    hasCache
                        ? (writeback == 0   ? storage::CacheMode::WriteThrough
                           : writeback == 1 ? storage::CacheMode::WriteBack
                                            : storage::CacheMode::Unknown)
                        : (negotiatedFeatures_ & featureFlush
                               ? storage::CacheMode::WriteBack
                               : storage::CacheMode::Unknown);
                return true;
            }

            return false;
        }

        bool initializeQueue() {
            if (queueDma_.create(device_, 4096) || queueDma_.getSize() < 4096) {
                return false;
            }

            queuePhysical_ = queueDma_.getDeviceAddress();
            queueMemory_ =
                reinterpret_cast<uint8_t*>(queueDma_.getVirtualAddress());
            os::Runtime::clearMemory(queueMemory_, 4096);
            for (uint32_t slot = 0; slot < requestSlotCount; ++slot) {
                if (requestDma_[slot].create(device_, requestDmaBytes) ||
                    requestDma_[slot].getSize() < requestDmaBytes) {
                    return false;
                }

                requestPhysical_[slot] = requestDma_[slot].getDeviceAddress();
                requestMemory_[slot] = reinterpret_cast<uint8_t*>(
                    requestDma_[slot].getVirtualAddress());
                os::Runtime::clearMemory(requestMemory_[slot], requestDmaBytes);
            }

            volatile uint8_t* common = transport_.common();
            writeMmio<uint16_t>(common, 22, 0);
            const uint16_t availableSize = readMmio<uint16_t>(common, 24);
            if (availableSize < queueSize) {
                return false;
            }

            writeMmio<uint16_t>(common, 24, queueSize);
            writeMmio<uint16_t>(common, 26, 0xffff);
            const uint32_t descriptorBytes =
                sizeof(VirtQueueDescriptor) * queueSize;
            availableOffset_ = descriptorBytes;
            usedOffset_ = alignUp(availableOffset_ + 6 + 2 * queueSize, 4);
            if (usedOffset_ + 6 + 8 * queueSize > 4096) {
                return false;
            }

            writeMmio<uint64_t>(common, 32, queuePhysical_);
            writeMmio<uint64_t>(common, 40, queuePhysical_ + availableOffset_);
            writeMmio<uint64_t>(common, 48, queuePhysical_ + usedOffset_);
            notifyOffset_ = readMmio<uint16_t>(common, 30);
            writeMmio<uint16_t>(common, 28, 1);
            return readMmio<uint16_t>(common, 28) == 1;
        }

        os::Storage::Error submit(uint32_t type, uint64_t block, void* buffer,
                                  bool deviceWrites, uint32_t count) {
            uint64_t start = 0;
            uint32_t slot = requestSlotCount;
            const os::Storage::Error error = begin(
                type, block, buffer, deviceWrites, count, start, slot, true);
            return error == os::Storage::Error::Success
                       ? finish(slot, start, type, buffer, count)
                       : error;
        }

        /* 取得一个空槽并发布命令；批次提交把通知延迟到最后一条发布后。 */
        os::Storage::Error begin(uint32_t type, uint64_t block, void* buffer,
                                 bool deviceWrites, uint32_t count,
                                 uint64_t& start, uint32_t& slot, bool notify) {
            using Error = os::Storage::Error;
            start = os::Sys().readMonotonicTime();
            slot = requestSlotCount;
            while (slot == requestSlotCount) {
                bool fault = false;
                {
                    os::Mutex::Guard guard(requestLock_);
                    if (!online_) {
                        return Error::DeviceOffline;
                    }

                    if (storage::expired(start, os::Sys().readMonotonicTime(),
                                         ioTimeoutNanoseconds)) {
                        return Error::Timeout;
                    }

                    if (!taskUsedLocked()) {
                        freezeLocked();
                        fault = true;
                    } else {
                        slot = slots_.findFree();
                        if (slot != requestSlotCount) {
                            submitLocked(slot, type, block, buffer,
                                         deviceWrites, count, notify);
                        }
                    }
                }

                if (fault) {
                    stop();
                    return Error::Io;
                }

                if (slot == requestSlotCount) {
                    os::yield();
                }
            }

            return Error::Success;
        }

        /* 每条命令单独确认结果，错误完成也由原槽位归还给原请求。 */
        os::Storage::Error finish(uint32_t slot, uint64_t start, uint32_t type,
                                  void* buffer, uint32_t count) {
            using Error = os::Storage::Error;
            for (uint32_t spins = 0;; ++spins) {
                Error stopError = Error::Success;
                const bool checkTime = (spins & 63U) == 63U;
                {
                    os::Mutex::Guard guard(requestLock_);
                    if (!online_) {
                        return Error::DeviceOffline;
                    }

                    if (!taskUsedLocked()) {
                        stopError = Error::Io;
                    } else if (slots_.completed(slot)) {
                        uint8_t* memory = requestMemory_[slot];
                        const uint32_t bytes = count * logicalBlockSize_;
                        const uint8_t status = memory[16 + bytes];
                        const uint32_t expected = type == 0 ? bytes + 1 : 1;
                        const bool complete =
                            status == 0 && slots_.usedLength(slot) == expected;
                        if (complete && type == 0) {
                            for (uint32_t index = 0; index < bytes; ++index) {
                                static_cast<uint8_t*>(buffer)[index] =
                                    memory[16 + index];
                            }
                        }

                        slots_.consume(slot);
                        if (complete) {
                            return Error::Success;
                        }

                        return status == 2 ? Error::NotSupported : Error::Io;
                    } else if (checkTime &&
                               storage::expired(start,
                                                os::Sys().readMonotonicTime(),
                                                ioTimeoutNanoseconds)) {
                        stopError = Error::Timeout;
                    }

                    if (stopError != Error::Success) {
                        freezeLocked();
                    }
                }

                if (stopError != Error::Success) {
                    os::Sys().log(
                        stopError == Error::Timeout
                            ? "[STORAGE] VirtIO submitted request timed out\n"
                            : "[STORAGE] invalid VirtIO completion\n");
                    stop();
                    return stopError;
                }

                if (checkTime) {
                    os::yield();
                } else {
                    asm volatile("pause");
                }
            }
        }

        void submitLocked(uint32_t slot, uint32_t type, uint64_t block,
                          void* buffer, bool deviceWrites, uint32_t count,
                          bool notify) {
            const uint32_t head = slot * 3;
            uint8_t* memory = requestMemory_[slot];
            const uint64_t physical = requestPhysical_[slot];
            const uint32_t bytes = count * logicalBlockSize_;
            VirtioBlockRequest* header =
                reinterpret_cast<VirtioBlockRequest*>(memory);
            header->type = type;
            header->reserved = 0;
            header->sector =
                block * (logicalBlockSize_ / storage::virtioSectorBytes);
            if (type == 1) {
                for (uint32_t index = 0; index < bytes; ++index) {
                    memory[16 + index] =
                        static_cast<const uint8_t*>(buffer)[index];
                }
            }

            memory[16 + bytes] = 0xff;

            VirtQueueDescriptor* descriptors =
                reinterpret_cast<VirtQueueDescriptor*>(queueMemory_);
            descriptors[head] = {
                physical, sizeof(VirtioBlockRequest), descriptorNext,
                static_cast<uint16_t>(type == 4 ? head + 2 : head + 1)};
            if (type != 4) {
                descriptors[head + 1] = {
                    physical + 16, bytes,
                    static_cast<uint16_t>(descriptorNext |
                                          (deviceWrites ? descriptorWrite : 0)),
                    static_cast<uint16_t>(head + 2)};
            }

            descriptors[head + 2] = {physical + 16 + bytes, 1, descriptorWrite,
                                     0};
            slots_.submit(slot);

            volatile uint16_t* available = reinterpret_cast<volatile uint16_t*>(
                queueMemory_ + availableOffset_);
            const uint16_t availableIndex = available[1];
            available[2 + (availableIndex % queueSize)] =
                static_cast<uint16_t>(head);
            __atomic_thread_fence(__ATOMIC_RELEASE);
            available[1] = static_cast<uint16_t>(availableIndex + 1);
            if (notify) {
                transport_.notifyQueue(0, notifyOffset_);
            }
        }

        bool taskUsedLocked() {
            volatile uint16_t* usedHeader =
                reinterpret_cast<volatile uint16_t*>(queueMemory_ +
                                                     usedOffset_);
            const uint16_t usedIndex =
                __atomic_load_n(usedHeader + 1, __ATOMIC_ACQUIRE);
            if (!slots_.validUsedAdvance(lastUsedIndex_, usedIndex)) {
                return false;
            }

            volatile VirtQueueUsedElement* elements =
                reinterpret_cast<volatile VirtQueueUsedElement*>(
                    queueMemory_ + usedOffset_ + 4);
            while (lastUsedIndex_ != usedIndex) {
                const volatile VirtQueueUsedElement& entry =
                    elements[lastUsedIndex_ % queueSize];
                if (!slots_.recordUsed(entry.id, entry.length)) {
                    return false;
                }

                ++lastUsedIndex_;
            }

            return true;
        }

        os::Device::Pci function_{};
        os::Device device_;
        VirtioPciTransport transport_;
        os::Device::Dma queueDma_;
        os::Device::Dma requestDma_[requestSlotCount];
        mutable os::Mutex requestLock_;
        os::Mutex stopLock_;
        storage::StopResult stopResult_ = storage::StopResult::Unconfirmed;
        bool stopRequested_ = false;
        storage::VirtioRequestSlots<requestSlotCount> slots_;
        uint8_t* queueMemory_ = nullptr;
        uint8_t* requestMemory_[requestSlotCount]{};
        uint64_t queuePhysical_ = 0;
        uint64_t requestPhysical_[requestSlotCount]{};
        uint64_t negotiatedFeatures_ = 0;
        uint64_t blockCount_ = 0;
        uint32_t logicalBlockSize_ = storage::virtioSectorBytes;
        uint32_t maxTransferBlocks_ = 0;
        storage::CacheMode cacheMode_ = storage::CacheMode::Unknown;
        uint32_t availableOffset_ = 0;
        uint32_t usedOffset_ = 0;
        uint16_t notifyOffset_ = 0;
        uint16_t lastUsedIndex_ = 0;
        bool readOnly_ = false;
        bool online_ = false;
        uint32_t deviceIndex_ = 0;
};

class PartitionDevice final : public storage::PartitionReader {
    public:
        PartitionDevice(storage::BlockBackend& backend, uint64_t blockCount)
            : backend_(backend), blockCount_(blockCount) {
        }

        uint64_t getBlockCount() const override {
            return blockCount_;
        }

        os::Storage::Error readBlock(uint64_t block, void* data) override {
            return backend_.read(block, 1, data, blockSize).getError();
        }

    private:
        storage::BlockBackend& backend_;
        uint64_t blockCount_;
};

class StorageService {
        using DeviceType = os::Storage::Device::Type;

        class Driver {
            public:
                Driver() = default;

                Driver(storage::BlockBackend& backend, DeviceType type,
                       const os::Device::Pci& function,
                       uint32_t unit = UINT32_MAX)
                    : backend_(&backend),
                      type_(type),
                      function_(function),
                      unit_(unit) {
                }

                storage::BlockBackend& getBackend() const {
                    return *backend_;
                }

                DeviceType getType() const {
                    return type_;
                }

                const os::Device::Pci& getFunction() const {
                    return function_;
                }

                uint32_t getUnit() const {
                    return unit_;
                }

            private:
                storage::BlockBackend* backend_ = nullptr;
                DeviceType type_{};
                os::Device::Pci function_{};
                uint32_t unit_ = UINT32_MAX;
        };

    public:
        int startup();

        static void workerMain(void* self) {
            static_cast<StorageService*>(self)->work();
        }

        static void irqMain(void* self) {
            static_cast<StorageService*>(self)->runIrq();
        }

        void runIrq() {
            while (true) {
                os::IpcMessage notification{};
                if (os::receiveMessage(irqEndpoint(), notification) == 0 &&
                    notification.type == irqNotification) {
                    handleInterrupt(
                        static_cast<uint32_t>(notification.data[0]));
                }
            }
        }

        bool init(uint64_t registry) {
            endpoint_ = os::detail::EndpointControl().create();
            const int irqStatus = irqEndpoint_.create();
            if (endpoint_ == 0 || irqStatus) {
                return false;
            }

            if (boot_.init()) {
                drivers_[driverCount_++] =
                    Driver(boot_, DeviceType::BootImage, {});
            }

            firstVirtio_ = driverCount_;
            os::Device::Pci functions[64]{};
            const int count = os::Device::Pci().enumerate(functions, 64);
            for (int index = 0; index < count && driverCount_ < deviceCapacity;
                 ++index) {
                if (functions[index].getClassCode() == 1 &&
                    functions[index].getSubclass() == 6 &&
                    functions[index].getProgrammingInterface() == 1) {
                    if (ahciControllerCount_ == deviceCapacity) {
                        continue;
                    }

                    storage::AhciController& controller =
                        ahciControllers_[ahciControllerCount_++];
                    if (!controller.init(functions[index])) {
                        os::Sys().log(
                            "[STORAGE] AHCI controller initialization "
                            "failed\n");
                        continue;
                    }

                    for (uint32_t port = 0;
                         port < 32 && driverCount_ < deviceCapacity; ++port) {
                        if (!(controller.ports() & (1U << port))) {
                            continue;
                        }

                        storage::AhciDisk& disk = ahci_[driverCount_];
                        if (disk.init(controller, port)) {
                            drivers_[driverCount_++] = Driver(
                                disk, DeviceType::Ahci, functions[index], port);
                            os::Sys().log("[STORAGE] AHCI ATA disk online\n");
                        }
                    }

                    continue;
                }

                if (functions[index].getClassCode() == 1 &&
                    functions[index].getSubclass() == 1) {
                    if (ideControllerCount_ == deviceCapacity) {
                        continue;
                    }

                    storage::IdeController& controller =
                        ideControllers_[ideControllerCount_++];
                    if (!controller.init(functions[index])) {
                        os::Sys().log(
                            "[STORAGE] IDE controller claim failed\n");
                        continue;
                    }

                    for (uint32_t channel = 0; channel < 2; ++channel) {
                        for (uint32_t slave = 0;
                             slave < 2 && driverCount_ < deviceCapacity;
                             ++slave) {
                            storage::IdeDisk& disk = ide_[driverCount_];
                            if (disk.init(controller, channel, slave)) {
                                drivers_[driverCount_++] = Driver(
                                    disk, DeviceType::Ide, functions[index],
                                    channel * 2 + slave);
                                os::Sys().log(
                                    "[STORAGE] ATA PIO disk online\n");
                            }
                        }
                    }

                    continue;
                }

                if (functions[index].getVendorId() != virtioVendorId ||
                    functions[index].getDeviceId() != virtioBlockDeviceId) {
                    continue;
                }

                VirtioBlockDriver& driver = virtio_[driverCount_];
                if (!driver.init(functions[index], driverCount_, irqEndpoint_,
                                 irqBindings_)) {
                    return false;
                }

                drivers_[driverCount_++] =
                    Driver(driver, DeviceType::VirtioBlock, functions[index]);
            }

            // Partition discovery already performs I/O. Drain level-triggered
            // interrupts before the first request, not after publishing the
            // service.
            if (driverCount_ == 0 ||
                os::mapMemory(irqStackAddress, irqStackSize, 1) !=
                    irqStackAddress ||
                os::createThread(irqMain, irqStackAddress + irqStackSize,
                                 this) == 0) {
                return false;
            }

            for (uint32_t driver = 0; driver < driverCount_; ++driver) {
                if (deviceCount_ == services::maximumStorageDevices) {
                    return false;
                }

                const uint32_t parent = deviceCount_;
                storage::BlockBackend& backend = drivers_[driver].getBackend();
                const storage::BlockInfo info = backend.info();
                PartitionDevice reader{backend, info.getBlockCount()};
                os::Storage::Part whole{};
                whole.setBlockCount(reader.getBlockCount());
                addDevice(driver, parent, whole);
                os::Storage::Part partitions[services::maximumStorageDevices]{};
                uint32_t count = 0;
                if (publishedLimit(info) &&
                    storage::PartitionTable(reader).read(
                        partitions,
                        services::maximumStorageDevices - deviceCount_,
                        count)) {
                    for (uint32_t index = 0; index < count; ++index) {
                        addDevice(driver, parent, partitions[index]);
                    }
                }
            }

            for (uint32_t index = 0; index < driverCount_; ++index) {
                const DeviceType type = drivers_[index].getType();
                const storage::QueueMode mode =
                    type == DeviceType::VirtioBlock
                        ? storage::QueueMode::Parallel
                    : type == DeviceType::BootImage
                        ? storage::QueueMode::Memory
                        : storage::QueueMode::Serial;
                policies_[index] = storage::QueuePolicy(
                    mode,
                    drivers_[index].getBackend().info().getMaxTransferBlocks());
            }

            if (!os::detail::RegistryClient(registry).registerName(
                    services::storageServiceName, endpoint_)) {
                return false;
            }

            return true;
        }

        uint64_t irqEndpoint() const {
            return irqEndpoint_.getHandle();
        }

        void handleInterrupt(uint32_t irq) {
            uint32_t faulty = 0;
            for (uint32_t index = firstVirtio_; index < driverCount_; ++index) {
                if (drivers_[index].getType() == DeviceType::VirtioBlock &&
                    virtio_[index].handleInterrupt()) {
                    faulty |= 1U << index;
                }
            }

            if (irq < 16 && irqBindings_[irq].getValid()) {
                irqBindings_[irq].acknowledge();
            }

            for (uint32_t index = firstVirtio_; index < driverCount_; ++index) {
                if (faulty & (1U << index)) {
                    virtio_[index].stop();
                }
            }
        }

        [[noreturn]] void run() {
            while (true) {
                os::IpcMessage request{};
                if (os::receiveMessage(endpoint_, request) != 0) {
                    continue;
                }

                // 只有异步读写接收完成端点，其余操作归还意外传入的句柄。
                if (request.handleCount &&
                    request.type != static_cast<uint64_t>(
                                        services::StorageRequest::ReadAsync) &&
                    request.type != static_cast<uint64_t>(
                                        services::StorageRequest::WriteAsync)) {
                    for (uint32_t index = 0; index < request.handleCount;
                         ++index) {
                        os::closeHandle(request.handles[index]);
                    }

                    os::IpcMessage reply{};
                    reply.data[0] = encode(os::Storage::Error::InvalidArgument);
                    reply.length = 1;
                    os::replyMessage(endpoint_, request.replyToken, reply);
                    continue;
                }

                if (queueRequest(request)) {
                    continue;
                }

                os::IpcMessage reply{};
                if (dispatch(request, reply)) {
                    os::replyMessage(endpoint_, request.replyToken, reply);
                }
            }
        }

    private:
        using Counters = storage::RequestCounters<os::SpinLock>;

        /* 排队期间持有完整消息及完成端点，数据不会引用接收线程的栈。 */
        class PendingIo {
            private:
                friend class StorageService;
                os::IpcMessage request{};
                uint64_t physical = 0;
                uint32_t blocks = 0, driver = 0;
                bool write = false, mergeable = false;
        };

        bool queueRequest(const os::IpcMessage& request);
        void work();
        void executeBatch(const uint32_t* indices, uint32_t count);
        void completeIo(PendingIo& pending, storage::TransferResult result,
                        const uint8_t* data);

        static storage::IoDirection directionFor(
            services::StorageRequest type) {
            using Request = services::StorageRequest;
            using Direction = storage::IoDirection;
            switch (type) {
                case Request::ReadBlocks:
                case Request::ReadRange:
                case Request::ReadAsync:
                    return Direction::Read;
                case Request::WriteBlocks:
                case Request::WriteRange:
                case Request::WriteAsync:
                    return Direction::Write;
                case Request::Flush:
                    return Direction::Flush;
                default:
                    return Direction::None;
            }
        }

        bool dispatch(const os::IpcMessage& request, os::IpcMessage& reply,
                      bool counted = false) {
            using Request = services::StorageRequest;
            using Direction = storage::IoDirection;
            using Error = os::Storage::Error;
            const Request type = static_cast<Request>(request.type);
            const Direction direction = directionFor(type);
            const uint64_t id =
                type == Request::Flush
                    ? request.data[0]
                    : request.data[0] >> services::storageDeviceShift;
            Counters* counters = direction != Direction::None &&
                                         request.length >= 1 &&
                                         id < deviceCount_
                                     ? &counters_[deviceDrivers_[id]]
                                     : nullptr;
            if (counters && !counted) {
                counters->begin();
            }

            if (type == Request::ReadAsync || type == Request::WriteAsync) {
                const Error error =
                    handleAsync(request, type == Request::WriteAsync);
                if (counters) {
                    counters->finish(direction, error,
                                     error == Error::Success ? blockSize : 0);
                }

                return false;
            }

            const bool respond = handle(request, reply);
            if (counters) {
                const Error error = storage::storageError(reply.data[0]);
                uint64_t bytes = 0;
                if (type == Request::WriteBlocks && error == Error::Success) {
                    bytes = request.data[2];
                } else if (reply.length >= 2) {
                    bytes = reply.data[1];
                }

                counters->finish(direction, error, bytes);
            }

            return respond;
        }

        bool handle(const os::IpcMessage& request, os::IpcMessage& reply) {
            const services::StorageRequest type =
                static_cast<services::StorageRequest>(request.type);
            if (type == services::StorageRequest::ReadRange ||
                type == services::StorageRequest::WriteRange) {
                const uint32_t id =
                    request.data[0] >> services::storageDeviceShift;
                const uint64_t block =
                    request.data[0] & services::storageBlockMask;
                const uint64_t count = request.data[1];
                const bool write = type == services::StorageRequest::WriteRange;
                reply.data[0] = encode(os::Storage::Error::InvalidArgument);
                reply.length = 1;
                storage::BlockInfo info{};
                uint64_t physical = 0;
                if (request.length < 2 || count > UINT32_MAX ||
                    !mapRange(id, block, count, info, physical)) {
                    return true;
                }

                const uint64_t bytes = count * info.getLogicalBlockSize();
                if (write && request.length < 2 + bytes / sizeof(uint64_t)) {
                    return true;
                }

                storage::BlockBackend& backend = backendFor(id);
                const storage::TransferResult result =
                    write
                        ? writeOrderFor(id).run([&] {
                              return backend.write(physical, count,
                                                   &request.data[2], bytes);
                          })
                        : backend.read(physical, count, &reply.data[2], bytes);
                storage::encodeRangeReply(reply, result, count,
                                          info.getLogicalBlockSize(), !write);
                return true;
            }

            if (type == services::StorageRequest::GetCount) {
                reply.data[0] = 0;
                reply.data[1] = deviceCount_;
                reply.length = 2;
                return true;
            }

            if (type == services::StorageRequest::GetStatistics) {
                reply.length = 1;
                if (request.length < 1 || request.data[0] >= deviceCount_) {
                    reply.data[0] = encode(os::Storage::Error::DeviceNotFound);
                    return true;
                }

                const os::Storage::Statistics stats =
                    counters_[deviceDrivers_[request.data[0]]].snapshot();
                reply.data[0] = 0;
                reply.data[1] = stats.completedRequests;
                reply.data[2] = stats.readBytes;
                reply.data[3] = stats.writtenBytes;
                reply.data[4] = stats.errors;
                reply.data[5] = stats.timeouts;
                reply.data[6] = stats.inFlight;
                reply.length = services::storage_protocol::statisticsWords;
                return true;
            }

            if (type == services::StorageRequest::GetInfo) {
                if (request.length < 1 || request.data[0] >= deviceCount_) {
                    reply.data[0] = encode(os::Storage::Error::DeviceNotFound);
                    reply.length = 1;
                    return true;
                }

                const uint32_t deviceId =
                    static_cast<uint32_t>(request.data[0]);
                const Driver& selected = drivers_[deviceDrivers_[deviceId]];
                const storage::BlockInfo info = selected.getBackend().info();
                reply.data[0] = 0;
                reply.data[1] = devices_[deviceId].getBlockCount();
                reply.data[2] = info.getLogicalBlockSize();
                reply.data[3] = publishedLimit(info);
                reply.data[4] = static_cast<uint64_t>(selected.getType());
                reply.data[5] = info.getReadOnly() ? 1 : 0;
                os::Runtime::copyBytes(
                    &reply.data[services::storageIdentityWord],
                    &identities_[deviceId],
                    sizeof(services::wire::StorageDeviceIdentity));
                reply.length =
                    services::storageIdentityWord +
                    (sizeof(services::wire::StorageDeviceIdentity) + 7) / 8;
                return true;
            }

            if (type == services::StorageRequest::Flush) {
                const uint32_t deviceId =
                    static_cast<uint32_t>(request.data[0]);
                reply.data[0] =
                    request.length >= 1 && request.data[0] < deviceCount_
                        ? encode(writeOrderFor(deviceId).run(
                              [&] { return backendFor(deviceId).flush(); }))
                        : encode(os::Storage::Error::DeviceNotFound);
                reply.length = 1;
                return true;
            }

            const uint32_t deviceId = static_cast<uint32_t>(
                request.data[0] >> services::storageDeviceShift);
            const uint64_t block = request.data[0] & services::storageBlockMask;
            const uint32_t offset = static_cast<uint32_t>(request.data[1]);
            const uint32_t size = static_cast<uint32_t>(request.data[2]);
            const uint32_t maximumSize =
                type == services::StorageRequest::ReadBlocks
                    ? (os::ipcDataWordCount - 2) * sizeof(uint64_t)
                    : (os::ipcDataWordCount - 3) * sizeof(uint64_t);
            storage::BlockInfo info{};
            uint64_t physicalBlock = 0;
            if (request.length < 3 || request.data[1] > blockSize ||
                request.data[2] > maximumSize || offset > blockSize ||
                size == 0 || size > blockSize - offset ||
                (type == services::StorageRequest::WriteBlocks &&
                 request.length < 3 + (size + 7) / 8) ||
                !mapRange(deviceId, block, 1, info, physicalBlock)) {
                reply.data[0] = encode(os::Storage::Error::InvalidArgument);
                reply.length = 1;
                return true;
            }

            uint8_t data[blockSize]{};
            storage::BlockBackend& backend = backendFor(deviceId);
            os::Storage::Error result = os::Storage::Error::Success;
            if (type == services::StorageRequest::ReadBlocks) {
                result = backend.read(physicalBlock, 1, data, sizeof(data))
                             .getError();
                reply.data[0] = encode(result);
                if (result == os::Storage::Error::Success) {
                    reply.data[1] = size;
                    for (uint32_t index = 0; index < size; ++index) {
                        reinterpret_cast<uint8_t*>(&reply.data[2])[index] =
                            data[offset + index];
                    }

                    reply.length = static_cast<uint32_t>(
                        2 + (size + sizeof(uint64_t) - 1) / sizeof(uint64_t));
                } else {
                    reply.length = 1;
                }
            } else if (type == services::StorageRequest::WriteBlocks) {
                reply.data[0] = encode(writeOrderFor(deviceId).run([&] {
                    if (info.getReadOnly()) {
                        return os::Storage::Error::ReadOnly;
                    }

                    if (offset != 0 || size != blockSize) {
                        result =
                            backend.read(physicalBlock, 1, data, sizeof(data))
                                .getError();
                        if (result != os::Storage::Error::Success) {
                            return result;
                        }
                    }

                    for (uint32_t index = 0; index < size; ++index) {
                        data[offset + index] = reinterpret_cast<const uint8_t*>(
                            &request.data[3])[index];
                    }

                    return backend.write(physicalBlock, 1, data, sizeof(data))
                        .getError();
                }));
                reply.length = 1;
            } else {
                reply.data[0] = encode(os::Storage::Error::InvalidArgument);
                reply.length = 1;
            }

            return true;
        }

        os::Storage::Error handleAsync(const os::IpcMessage& request,
                                       bool write) {
            if (request.handleCount != 1) {
                for (uint32_t index = 0; index < request.handleCount; ++index) {
                    os::closeHandle(request.handles[index]);
                }

                return os::Storage::Error::InvalidArgument;
            }

            const uint64_t completionEndpoint = request.handles[0];
            const uint32_t deviceId = static_cast<uint32_t>(
                request.data[0] >> services::storageDeviceShift);
            const uint64_t block = request.data[0] & services::storageBlockMask;
            const uint64_t requestId = request.data[1];
            uint8_t data[blockSize]{};
            os::Storage::Error result = os::Storage::Error::InvalidArgument;
            storage::BlockInfo info{};
            uint64_t physical = 0;
            if (request.length >= 2 &&
                mapRange(deviceId, block, 1, info, physical) &&
                (!write || (request.data[2] == blockSize &&
                            request.length == os::ipcDataWordCount))) {
                if (write) {
                    for (uint32_t index = 0; index < blockSize; ++index) {
                        data[index] = reinterpret_cast<const uint8_t*>(
                            &request.data[3])[index];
                    }

                    result = writeOrderFor(deviceId).run([&] {
                        return backendFor(deviceId)
                            .write(physical, 1, data, sizeof(data))
                            .getError();
                    });
                } else {
                    result = backendFor(deviceId)
                                 .read(physical, 1, data, sizeof(data))
                                 .getError();
                }
            }

            os::IpcMessage completion{};
            completion.type = services::storageCompletionNotification;
            completion.data[0] = requestId;
            completion.data[1] = encode(result);
            completion.data[2] =
                result == os::Storage::Error::Success ? blockSize : 0;
            completion.length = 3;
            if (!write && result == os::Storage::Error::Success) {
                for (uint32_t index = 0; index < blockSize; ++index) {
                    reinterpret_cast<uint8_t*>(&completion.data[3])[index] =
                        data[index];
                }

                completion.length = os::ipcDataWordCount;
            }

            // An abandoned/full completion endpoint must not block a worker.
            os::sendMessage(completionEndpoint, completion, 0);
            os::closeHandle(completionEndpoint);
            return result;
        }

        static uint64_t encode(os::Storage::Error error) {
            return static_cast<uint64_t>(static_cast<int64_t>(error));
        }

        uint64_t endpoint_ = 0;
        os::Ipc irqEndpoint_;

        storage::BlockBackend& backendFor(uint32_t deviceId) {
            return drivers_[deviceDrivers_[deviceId]].getBackend();
        }

        storage::WriteOrder<os::Mutex>& writeOrderFor(uint32_t deviceId) {
            return writeOrder_[deviceDrivers_[deviceId]];
        }

        static uint32_t publishedLimit(const storage::BlockInfo& info) {
            // Current partition and filesystem code operates on 512-byte
            // blocks.
            return info.getLogicalBlockSize() == blockSize
                       ? services::storage_protocol::rangeLimit(
                             info.getLogicalBlockSize(),
                             info.getMaxTransferBlocks())
                       : 0;
        }

        bool mapRange(uint32_t id, uint64_t first, uint32_t count,
                      storage::BlockInfo& info, uint64_t& physical) {
            if (id >= deviceCount_) {
                return false;
            }

            info = backendFor(id).info();
            const os::Storage::Part& part = devices_[id];
            return count <= publishedLimit(info) &&
                   storage::translatePartitionRange(
                       info.getBlockCount(), part.getStartBlock(),
                       part.getBlockCount(), first, count, physical);
        }

        static void decimal(char* output, uint32_t value) {
            char reverse[10];
            uint32_t count = 0;
            do {
                reverse[count++] = static_cast<char>('0' + value % 10);
                value /= 10;
            } while (value);
            for (uint32_t i = 0; i < count; ++i) {
                output[i] = reverse[count - i - 1];
            }

            output[count] = '\0';
        }

        static void hexByte(char* output, uint8_t value) {
            constexpr char hex[] = "0123456789abcdef";
            output[0] = hex[value >> 4];
            output[1] = hex[value & 15];
        }

        void addDevice(uint32_t driver, uint32_t parent,
                       const os::Storage::Part& partition) {
            const uint32_t id = deviceCount_++;
            devices_[id] = partition;
            deviceDrivers_[id] = driver;
            services::wire::StorageDeviceIdentity& identity = identities_[id];
            identity.id = id;
            identity.major = 252;
            identity.minor = driver * 256 + partition.getNumber();
            identity.parentId = parent;
            identity.partitionNumber = partition.getNumber();
            identity.startBlock = partition.getStartBlock();
            os::Runtime::copyString(identity.partitionUuid, partition.getUuid(),
                                    sizeof(identity.partitionUuid));
            os::Runtime::copyString(identity.name, "vda",
                                    sizeof(identity.name));
            if (drivers_[driver].getType() == DeviceType::Ide) {
                identity.name[0] = 'h';
                identity.major = 3;
            }

            if (drivers_[driver].getType() == DeviceType::Ahci) {
                identity.name[0] = 's';
                identity.major = 8;
            }

            identity.name[2] = static_cast<char>('a' + driver);
            if (partition.getNumber() != 0) {
                decimal(identity.name + 3, partition.getNumber());
            }

            os::Runtime::copyString(identity.hardwarePath, "pci-0000:00:00.0",
                                    sizeof(identity.hardwarePath));
            const os::Device::Pci& function = drivers_[driver].getFunction();
            hexByte(identity.hardwarePath + 9, function.getBus());
            hexByte(identity.hardwarePath + 12, function.getDevice());
            identity.hardwarePath[15] =
                static_cast<char>('0' + function.getFunction());
            uint32_t pathLength = 16;
            if (drivers_[driver].getType() == DeviceType::Ide ||
                drivers_[driver].getType() == DeviceType::Ahci) {
                // 一个 PCI IDE
                // 控制器可挂四块磁盘，路径必须包含通道和主从盘编号。
                os::Runtime::copyString(
                    identity.hardwarePath + pathLength, "-ata-",
                    sizeof(identity.hardwarePath) - pathLength);
                decimal(identity.hardwarePath + pathLength + 5,
                        drivers_[driver].getUnit());
                pathLength = os::Runtime::stringLength(identity.hardwarePath);
            }

            if (partition.getNumber() != 0) {
                os::Runtime::copyString(
                    identity.hardwarePath + pathLength, "-part",
                    sizeof(identity.hardwarePath) - pathLength);
                decimal(identity.hardwarePath + pathLength + 5,
                        partition.getNumber());
            }

            if (drivers_[driver].getType() == DeviceType::BootImage) {
                os::Runtime::copyString(identity.name, "ram0",
                                        sizeof(identity.name));
                os::Runtime::copyString(identity.hardwarePath, "boot-rootfs",
                                        sizeof(identity.hardwarePath));
                identity.major = 1;
                identity.minor = partition.getNumber();
                if (partition.getNumber()) {
                    identity.name[4] = 'p';
                    decimal(identity.name + 5, partition.getNumber());
                    os::Runtime::copyString(identity.hardwarePath + 11, "-part",
                                            sizeof(identity.hardwarePath) - 11);
                    decimal(identity.hardwarePath + 16, partition.getNumber());
                }
            }

            os::Sys().log("[STORAGE] /dev/");
            os::Sys().log(identity.name);
            os::Sys().log(" registered\n");
        }

        static constexpr uint32_t deviceCapacity = 8;
        storage::BootImageDriver boot_;
        VirtioBlockDriver virtio_[deviceCapacity];
        storage::IdeController ideControllers_[deviceCapacity];
        storage::IdeDisk ide_[deviceCapacity];
        uint32_t ideControllerCount_ = 0;
        storage::AhciController ahciControllers_[deviceCapacity];
        storage::AhciDisk ahci_[deviceCapacity];
        uint32_t ahciControllerCount_ = 0;
        Driver drivers_[deviceCapacity]{};
        storage::WriteOrder<os::Mutex> writeOrder_[deviceCapacity];
        Counters counters_[deviceCapacity];
        storage::RequestQueue queue_;
        storage::QueuePolicy policies_[deviceCapacity];
        PendingIo pending_[storage::RequestQueue::capacity];
        os::Mutex queueLock_;
        volatile uint32_t queueChanged_ = 0;
        os::Storage::Part devices_[services::maximumStorageDevices]{};
        services::wire::StorageDeviceIdentity
            identities_[services::maximumStorageDevices]{};
        uint32_t deviceDrivers_[services::maximumStorageDevices]{};
        uint32_t driverCount_ = 0;
        uint32_t firstVirtio_ = 0;
        uint32_t deviceCount_ = 0;
        os::Device::Irq irqBindings_[16];
};

/* 接收线程只负责验证队列归属和保存请求，磁盘等待由工作线程承担。 */
bool StorageService::queueRequest(const os::IpcMessage& request) {
    using Request = services::StorageRequest;
    using Direction = storage::QueueDirection;
    const Request type = static_cast<Request>(request.type);
    if (directionFor(type) == storage::IoDirection::None || !request.length) {
        return false;
    }

    const uint64_t id = type == Request::Flush
                            ? request.data[0]
                            : request.data[0] >> services::storageDeviceShift;
    if (id >= deviceCount_) {
        return false;
    }

    const bool asynchronous =
        type == Request::ReadAsync || type == Request::WriteAsync;
    const bool range =
        type == Request::ReadRange || type == Request::WriteRange;
    const uint32_t driver = deviceDrivers_[id];
    const bool write =
        type == Request::WriteRange || type == Request::WriteAsync;
    uint64_t physical = 0;
    uint32_t blockCount = 0;
    storage::BlockInfo info;
    const uint64_t blocks = range ? request.data[1] : 1;
    const uint64_t first = request.data[0] & services::storageBlockMask;
    bool mergeable = (range || asynchronous) && request.length >= 2 &&
                     request.handleCount == (asynchronous ? 1U : 0U) &&
                     blocks <= UINT32_MAX &&
                     mapRange(static_cast<uint32_t>(id), first,
                              static_cast<uint32_t>(blocks), info, physical);
    if (mergeable) {
        blockCount = static_cast<uint32_t>(blocks);
        const uint32_t bytes = blockCount * blockSize;
        if (write) {
            mergeable = asynchronous
                            ? request.length == os::ipcDataWordCount &&
                                  request.data[2] == blockSize
                            : request.length >= 2 + bytes / sizeof(uint64_t);
        }
    }

    // 刷新、旧式部分扇区读写、格式错误请求都在原位置单独处理。
    const Direction direction = !mergeable ? Direction::Barrier
                                : write    ? Direction::Write
                                           : Direction::Read;
    while (true) {
        queueLock_.lock();
        const uint32_t slot =
            queue_.enqueue(driver, direction, physical, blockCount);
        if (slot != storage::RequestQueue::capacity) {
            // 消息正文只复制一次，排队元数据直接写入已领取的槽位。
            PendingIo& pending = pending_[slot];
            pending.request = request;
            pending.driver = driver;
            pending.physical = physical;
            pending.blocks = blockCount;
            pending.write = write;
            pending.mergeable = mergeable;
            counters_[driver].begin();
            __atomic_add_fetch(&queueChanged_, 1, __ATOMIC_RELEASE);
            queueLock_.unlock();
            os::AddressWait(queueChanged_).wakeAll();
            return true;
        }

        // 队列满时停止继续接收，IPC 本身把压力传回请求方。
        const uint32_t generation =
            __atomic_load_n(&queueChanged_, __ATOMIC_ACQUIRE);
        queueLock_.unlock();
        os::AddressWait(queueChanged_).wait(generation);
    }
}

/* 各工作线程领取不同设备的批次，空闲时睡眠等待新请求。 */
void StorageService::work() {
    while (true) {
        uint32_t indices[storage::RequestQueue::batchCapacity];
        queueLock_.lock();
        const uint32_t count = queue_.take(policies_, driverCount_, indices);
        const uint32_t generation =
            __atomic_load_n(&queueChanged_, __ATOMIC_ACQUIRE);
        queueLock_.unlock();
        if (!count) {
            os::AddressWait(queueChanged_).wait(generation);
            continue;
        }

        executeBatch(indices, count);

        queueLock_.lock();
        queue_.finish(indices, count);
        __atomic_add_fetch(&queueChanged_, 1, __ATOMIC_RELEASE);
        queueLock_.unlock();
        os::AddressWait(queueChanged_).wakeAll();
    }
}

/* 合并相邻范围，并把每条原请求的结果按已完成的连续前缀拆回去。 */
void StorageService::executeBatch(const uint32_t* indices, uint32_t count) {
    PendingIo& first = pending_[indices[0]];
    if (!first.mergeable) {
        os::IpcMessage reply{};
        if (dispatch(first.request, reply, true)) {
            os::replyMessage(endpoint_, first.request.replyToken, reply);
        }

        return;
    }

    constexpr uint32_t capacity = storage::RequestQueue::batchCapacity;
    uint8_t buffers[capacity][4096];
    storage::BlockTransfer transfers[capacity];
    const storage::QueuePolicy& policy = policies_[first.driver];
    storage::MergePlan plan(policy.getBlockLimit());

    for (uint32_t index = 0; index < count; ++index) {
        PendingIo& item = pending_[indices[index]];
        if (!plan.append(item.physical, item.blocks)) {
            // 入队范围已校验；仍把异常完成归还给每个原调用者。
            for (uint32_t failed = 0; failed < count; ++failed) {
                completeIo(pending_[indices[failed]],
                           {os::Storage::Error::Io, 0}, nullptr);
            }

            return;
        }

        const uint32_t group = plan.getGroup(index);
        if (item.write) {
            const bool asynchronous =
                item.request.type ==
                static_cast<uint64_t>(services::StorageRequest::WriteAsync);
            os::Runtime::copyBytes(
                buffers[group] + plan.getOffset(index) * blockSize,
                &item.request.data[asynchronous ? 3 : 2],
                item.blocks * blockSize);
        }
    }

    const uint32_t groupCount = plan.getGroupCount();
    for (uint32_t group = 0; group < groupCount; ++group) {
        transfers[group] = storage::BlockTransfer(
            plan.getFirst(group), plan.getBlocks(group), buffers[group],
            plan.getBlocks(group) * blockSize, first.write);
    }

    // 串行 ATA 每次交一条命令，VirtIO 一次发布多个互不重叠的命令。
    storage::BlockBackend& backend = drivers_[first.driver].getBackend();
    for (uint32_t base = 0; base < groupCount; base += policy.getDepth()) {
        const uint32_t length = groupCount - base < policy.getDepth()
                                    ? groupCount - base
                                    : policy.getDepth();
        backend.submitBatch(transfers + base, length);
    }

    for (uint32_t index = 0; index < count; ++index) {
        PendingIo& item = pending_[indices[index]];
        const uint32_t group = plan.getGroup(index);
        storage::TransferResult result = transfers[group].getResult();
        if (result.getCompleted() > plan.getBlocks(group) ||
            (result.getError() == os::Storage::Error::Success &&
             result.getCompleted() != plan.getBlocks(group))) {
            result = {os::Storage::Error::Io, 0};
        }

        const uint32_t done = plan.completed(index, result.getCompleted());
        completeIo(item,
                   {done == item.blocks ? os::Storage::Error::Success
                                        : result.getError(),
                    done},
                   buffers[group] + plan.getOffset(index) * blockSize);
    }
}

/* 每个调用者仍得到自己的请求编号、字节数和完成端点通知。 */
void StorageService::completeIo(PendingIo& pending,
                                storage::TransferResult result,
                                const uint8_t* data) {
    const os::IpcMessage& request = pending.request;
    const bool asynchronous =
        request.type ==
            static_cast<uint64_t>(services::StorageRequest::ReadAsync) ||
        request.type ==
            static_cast<uint64_t>(services::StorageRequest::WriteAsync);
    const uint32_t bytes = result.getCompleted() * blockSize;
    os::IpcMessage reply{};
    if (asynchronous) {
        reply.type = services::storageCompletionNotification;
        reply.data[0] = request.data[1];
        reply.data[1] = encode(result.getError());
        reply.data[2] = bytes;
        reply.length = 3;
        if (!pending.write && bytes) {
            os::Runtime::copyBytes(&reply.data[3], data, bytes);
            reply.length += bytes / sizeof(uint64_t);
        }

        os::sendMessage(request.handles[0], reply, 0);
        os::closeHandle(request.handles[0]);
    } else {
        storage::encodeRangeReply(reply, result, pending.blocks, blockSize,
                                  !pending.write);
        if (!pending.write && bytes) {
            os::Runtime::copyBytes(&reply.data[2], data, bytes);
        }

        os::replyMessage(endpoint_, request.replyToken, reply);
    }

    counters_[pending.driver].finish(pending.write ? storage::IoDirection::Write
                                                   : storage::IoDirection::Read,
                                     result.getError(), bytes);
}

/* 完成设备发现后启动执行线程，主线程持续接收和排队。 */
int StorageService::startup() {
    os::Sys().log("[STORAGE] Storage Service started in Ring 3\n");
    const uint64_t registry = os::getBootstrapHandle();
    if (registry == 0 || !init(registry) ||
        os::mapMemory(workerStackAddress, workerStackSize * workerThreadCount,
                      1) != workerStackAddress) {
        os::Sys().log("[STORAGE] block backend initialization failed\n");
        return 1;
    }

    for (uint32_t index = 0; index < workerThreadCount; ++index) {
        const uint64_t stackTop =
            workerStackAddress + workerStackSize * (index + 1);
        if (os::createThread(workerMain, stackTop, this) == 0) {
            os::Sys().log("[STORAGE] unable to start I/O worker\n");
            return 2;
        }
    }

    os::Sys().log("[STORAGE] block backends online\n");
    run();

    return 0;
}

int main(int, char**) {
    static StorageService service;
    return service.startup();
}
