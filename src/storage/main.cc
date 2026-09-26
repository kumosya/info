#include "os/abi.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/storage.h"
#include "os/sync.h"

namespace {

constexpr uint16_t virtioVendorId          = 0x1af4;
constexpr uint16_t virtioBlockDeviceId     = 0x1042;
constexpr uint8_t virtioCapabilityId       = 0x09;
constexpr uint8_t commonConfigurationType  = 1;
constexpr uint8_t notifyConfigurationType  = 2;
constexpr uint8_t isrConfigurationType     = 3;
constexpr uint8_t deviceConfigurationType  = 4;
constexpr uint8_t statusAcknowledge        = 1;
constexpr uint8_t statusDriver             = 2;
constexpr uint8_t statusDriverOk           = 4;
constexpr uint8_t statusFeaturesOk         = 8;
constexpr uint8_t statusFailed             = 128;
constexpr uint64_t featureReadOnly         = 1ULL << 5;
constexpr uint64_t featureFlush            = 1ULL << 9;
constexpr uint64_t featureVersionOne       = 1ULL << 32;
constexpr uint16_t descriptorNext          = 1;
constexpr uint16_t descriptorWrite         = 2;
constexpr uint32_t blockSize               = 512;
constexpr uint32_t queueSize               = 32;
constexpr uint32_t requestSlotCount        = 7;
constexpr uint32_t requestStride           = 544;
constexpr uint64_t ioTimeoutNanoseconds    = 2ULL * 1000 * 1000 * 1000;
constexpr uint64_t irqNotification         = 0x495251;
constexpr uint64_t queueDmaAddress         = 0x0000000030000000ULL;
constexpr uint64_t requestDmaAddress       = 0x0000000030001000ULL;
constexpr uint64_t irqStackAddress         = 0x0000000030010000ULL;
constexpr uint64_t irqStackSize            = 64 * 1024;
constexpr uint64_t workerStackAddress      = 0x0000000030020000ULL;
constexpr uint64_t workerStackSize         = 64 * 1024;
constexpr uint32_t workerThreadCount       = 3;
constexpr uint64_t mmioBaseAddress         = 0x0000000040000000ULL;
constexpr uint32_t maximumPartitionEntries = 128;
constexpr uint32_t maximumCapabilityWalk   = 48;

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

uint32_t alignUp(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t readLe32(const uint8_t* bytes) {
    return static_cast<uint32_t>(bytes[0]) |
           (static_cast<uint32_t>(bytes[1]) << 8) |
           (static_cast<uint32_t>(bytes[2]) << 16) |
           (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t readLe64(const uint8_t* bytes) {
    return static_cast<uint64_t>(readLe32(bytes)) |
           (static_cast<uint64_t>(readLe32(bytes + 4)) << 32);
}

uint32_t updateCrc32(uint32_t crc, const uint8_t* bytes, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (uint32_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^
                  (0xedb88320U & (0U - static_cast<uint32_t>(crc & 1U)));
        }
    }
    return crc;
}

class PciConfiguration {
   public:
    explicit PciConfiguration(const os::PciFunctionInfo& function)
        : function_(function) {}

    bool read32(uint16_t offset, uint32_t& value) const {
        return os::readPciConfig(function_, offset, value) == 0;
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
        return read32(4, command) &&
               os::writePciConfig(function_, 4, command | flags) == 0;
    }

   private:
    const os::PciFunctionInfo& function_;
};

template <typename Type>
Type readMmio(volatile uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<volatile Type*>(base + offset);
}

template <typename Type>
void writeMmio(volatile uint8_t* base, uint32_t offset, Type value) {
    *reinterpret_cast<volatile Type*>(base + offset) = value;
}

class VirtioPciTransport {
   public:
    bool initialize(const os::PciFunctionInfo& function, uint64_t mmioBase) {
        function_ = function;
        mmioBase_ = mmioBase;
        PciConfiguration configuration(function_);
        uint32_t statusCommand;
        uint8_t capabilityOffset;
        if (!configuration.read32(4, statusCommand) ||
            !configuration.read8(0x34, capabilityOffset)) {
            os::log("[STORAGE] unable to read PCI capability pointer\n");
            return false;
        }
        if ((statusCommand & (1U << 20)) == 0) {
            os::log("[STORAGE] PCI capability list is unavailable\n");
            return false;
        }
        if (!scanCapabilities(configuration, capabilityOffset)) {
            os::log("[STORAGE] required VirtIO PCI capabilities are missing\n");
            return false;
        }
        if (!mapBars()) {
            os::log("[STORAGE] VirtIO PCI BAR mapping failed\n");
            return false;
        }
        if (!configuration.writeCommand(0x0006)) {
            os::log("[STORAGE] PCI memory/bus-master enable failed\n");
            return false;
        }
        common_ = mappedBars_[commonCapability_.bar] + commonCapability_.offset;
        notify_ = mappedBars_[notifyCapability_.bar] + notifyCapability_.offset;
        isr_    = mappedBars_[isrCapability_.bar] + isrCapability_.offset;
        device_ = mappedBars_[deviceCapability_.bar] + deviceCapability_.offset;
        return true;
    }

    volatile uint8_t* common() const { return common_; }
    volatile uint8_t* device() const { return device_; }

    void notifyQueue(uint16_t queue, uint16_t notifyOffset) {
        writeMmio<uint16_t>(notify_, notifyOffset * notifyMultiplier_, queue);
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
            os::log("[STORAGE] VirtIO common capability missing\n");
        }
        if (!notifyCapability_.present) {
            os::log("[STORAGE] VirtIO notify capability missing\n");
        }
        if (!isrCapability_.present) {
            os::log("[STORAGE] VirtIO ISR capability missing\n");
        }
        if (!deviceCapability_.present) {
            os::log("[STORAGE] VirtIO device capability missing\n");
        }
        if (notifyMultiplier_ == 0) {
            os::log("[STORAGE] VirtIO notify multiplier is zero\n");
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
            type != notifyConfigurationType && type != isrConfigurationType &&
            type != deviceConfigurationType) {
            return true;
        }
        if (!configuration.read8(offset + 4, bar) || bar >= 6 || length < 16 ||
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
        capability->bar     = bar;
        capability->offset  = regionOffset;
        capability->length  = regionLength;
        if (type == notifyConfigurationType) {
            if (length < 20 || !configuration.read32(
                                   offset + 16, capability->notifyMultiplier)) {
                return false;
            }
        }
        const uint64_t end = static_cast<uint64_t>(regionOffset) + regionLength;
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
            const uint64_t address = mmioBase_ + bar * 0x10000ULL;
            const int64_t handle   = static_cast<int64_t>(os::mapDeviceMmio(
                function_, bar, address, requiredBarSize_[bar]));
            if (handle <= 0) {
                return false;
            }
            mappedBars_[bar] = reinterpret_cast<volatile uint8_t*>(address);
        }
        return true;
    }

    os::PciFunctionInfo function_{};
    VirtioPciCapability commonCapability_{};
    VirtioPciCapability notifyCapability_{};
    VirtioPciCapability isrCapability_{};
    VirtioPciCapability deviceCapability_{};
    uint32_t requiredBarSize_[6]{};
    volatile uint8_t* mappedBars_[6]{};
    volatile uint8_t* common_  = nullptr;
    volatile uint8_t* notify_  = nullptr;
    volatile uint8_t* isr_     = nullptr;
    volatile uint8_t* device_  = nullptr;
    uint32_t notifyMultiplier_ = 0;
    uint64_t mmioBase_ = 0;
};

class VirtioBlockDriver {
   public:
    bool initialize(const os::PciFunctionInfo& function, uint32_t deviceIndex,
                    uint64_t irqEndpoint) {
        function_ = function;
        deviceIndex_ = deviceIndex;
        if (!transport_.initialize(
                function_, mmioBaseAddress + deviceIndex * 0x100000ULL)) {
            os::log("[STORAGE] VirtIO PCI capability/BAR setup failed\n");
            return false;
        }
        volatile uint8_t* common = transport_.common();
        writeMmio<uint8_t>(common, 20, 0);
        writeMmio<uint8_t>(common, 20, statusAcknowledge | statusDriver);
        writeMmio<uint32_t>(common, 0, 0);
        const uint64_t lowFeatures = readMmio<uint32_t>(common, 4);
        writeMmio<uint32_t>(common, 0, 1);
        const uint64_t highFeatures   = readMmio<uint32_t>(common, 4);
        const uint64_t deviceFeatures = lowFeatures | (highFeatures << 32);
        if ((deviceFeatures & featureVersionOne) == 0) {
            os::log("[STORAGE] VirtIO 1.0 feature is unavailable\n");
            fail();
            return false;
        }
        negotiatedFeatures_ =
            featureVersionOne |
            (deviceFeatures & (featureReadOnly | featureFlush));
        writeMmio<uint32_t>(common, 8, 0);
        writeMmio<uint32_t>(common, 12,
                            static_cast<uint32_t>(negotiatedFeatures_));
        writeMmio<uint32_t>(common, 8, 1);
        writeMmio<uint32_t>(common, 12,
                            static_cast<uint32_t>(negotiatedFeatures_ >> 32));
        writeMmio<uint8_t>(common, 20,
                           statusAcknowledge | statusDriver | statusFeaturesOk);
        if ((readMmio<uint8_t>(common, 20) & statusFeaturesOk) == 0) {
            os::log("[STORAGE] VirtIO feature negotiation was rejected\n");
            fail();
            return false;
        }
        if (!initializeQueue()) {
            os::log("[STORAGE] VirtQueue setup failed\n");
            fail();
            return false;
        }
        blockCount_       = readMmio<uint64_t>(transport_.device(), 0);
        readOnly_         = (negotiatedFeatures_ & featureReadOnly) != 0;
        const uint8_t irq = transport_.interruptLine();
        if (blockCount_ == 0) {
            os::log("[STORAGE] VirtIO Block reports zero capacity\n");
            fail();
            return false;
        }
        if (irq > 15 ||
            static_cast<int64_t>(os::bindIrq(irq, irqEndpoint)) <= 0) {
            os::log("[STORAGE] VirtIO Block IRQ binding failed\n");
            fail();
            return false;
        }
        writeMmio<uint8_t>(common, 20,
                           statusAcknowledge | statusDriver | statusFeaturesOk |
                               statusDriverOk);
        online_ = true;
        return true;
    }

    os::StorageError readBlock(uint64_t block, void* buffer) {
        return submit(0, block, buffer, true);
    }

    os::StorageError writeBlock(uint64_t block, const void* buffer) {
        if (readOnly_) {
            return os::StorageError::ReadOnly;
        }
        return submit(1, block, const_cast<void*>(buffer), false);
    }

    os::StorageError flush() {
        if ((negotiatedFeatures_ & featureFlush) == 0) {
            return os::StorageError::Success;
        }
        return submit(4, 0, nullptr, false);
    }

    void handleInterrupt() {
        transport_.acknowledgeInterrupt();
        os::SpinLockGuard guard(requestLock_);
        processUsedLocked();
    }

    uint64_t blockCount() const { return blockCount_; }
    bool readOnly() const { return readOnly_; }

   private:
    bool initializeQueue() {
        os::DmaBufferInfo queueInfo{};
        os::DmaBufferInfo requestInfo{};
        const uint64_t dmaOffset = deviceIndex_ * 0x2000ULL;
        if (os::allocateDma(queueDmaAddress + dmaOffset, queueInfo) != 0 ||
            os::allocateDma(requestDmaAddress + dmaOffset, requestInfo) != 0 ||
            queueInfo.size < 4096 || requestInfo.size < 4096) {
            return false;
        }
        queuePhysical_   = queueInfo.deviceAddress;
        requestPhysical_ = requestInfo.deviceAddress;
        queueMemory_     = reinterpret_cast<uint8_t*>(queueInfo.virtualAddress);
        requestMemory_ = reinterpret_cast<uint8_t*>(requestInfo.virtualAddress);
        os::clearMemory(queueMemory_, 4096);
        os::clearMemory(requestMemory_, 4096);

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
        usedOffset_      = alignUp(availableOffset_ + 6 + 2 * queueSize, 4);
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

    os::StorageError submit(uint32_t type, uint64_t block, void* buffer,
                            bool deviceWrites) {
        if (type != 4 && (block >= blockCount_ || buffer == nullptr)) {
            return os::StorageError::InvalidBlock;
        }
        const uint64_t deadline = os::getMonotonicTime() + ioTimeoutNanoseconds;
        uint32_t slot           = requestSlotCount;
        while (slot == requestSlotCount) {
            {
                os::SpinLockGuard guard(requestLock_);
                if (!online_) {
                    return os::StorageError::DeviceOffline;
                }
                processUsedLocked();
                for (uint32_t index = 0; index < requestSlotCount; ++index) {
                    if (!slots_[index].active) {
                        slot = index;
                        break;
                    }
                }
                if (slot != requestSlotCount) {
                    submitLocked(slot, type, block, buffer, deviceWrites);
                }
            }
            if (slot == requestSlotCount) {
                if (os::getMonotonicTime() >= deadline) {
                    return os::StorageError::Timeout;
                }
                os::yield();
            }
        }

        while (true) {
            {
                os::SpinLockGuard guard(requestLock_);
                processUsedLocked();
                if (slots_[slot].completed) {
                    uint8_t* memory = requestMemory_ + slot * requestStride;
                    const uint8_t status = memory[16 + blockSize];
                    if (status == 0 && type == 0) {
                        for (uint32_t index = 0; index < blockSize; ++index) {
                            static_cast<uint8_t*>(buffer)[index] =
                                memory[16 + index];
                        }
                    }
                    slots_[slot] = {};
                    return status == 0 ? os::StorageError::Success
                                       : os::StorageError::Io;
                }
                if (!online_) {
                    return os::StorageError::DeviceOffline;
                }
            }
            if (os::getMonotonicTime() >= deadline) {
                os::SpinLockGuard guard(requestLock_);
                online_ = false;
                fail();
                return os::StorageError::Timeout;
            }
            os::yield();
        }
    }

    void submitLocked(uint32_t slot, uint32_t type, uint64_t block,
                      void* buffer, bool deviceWrites) {
        const uint32_t head     = slot * 3;
        uint8_t* memory         = requestMemory_ + slot * requestStride;
        const uint64_t physical = requestPhysical_ + slot * requestStride;
        auto* header            = reinterpret_cast<VirtioBlockRequest*>(memory);
        header->type            = type;
        header->reserved        = 0;
        header->sector          = block;
        if (type == 1) {
            for (uint32_t index = 0; index < blockSize; ++index) {
                memory[16 + index] = static_cast<const uint8_t*>(buffer)[index];
            }
        }
        memory[16 + blockSize] = 0xff;

        auto* descriptors =
            reinterpret_cast<VirtQueueDescriptor*>(queueMemory_);
        descriptors[head] = {
            physical, sizeof(VirtioBlockRequest), descriptorNext,
            static_cast<uint16_t>(type == 4 ? head + 2 : head + 1)};
        if (type != 4) {
            descriptors[head + 1] = {
                physical + 16, blockSize,
                static_cast<uint16_t>(descriptorNext |
                                      (deviceWrites ? descriptorWrite : 0)),
                static_cast<uint16_t>(head + 2)};
        }
        descriptors[head + 2]  = {physical + 16 + blockSize, 1, descriptorWrite,
                                  0};
        slots_[slot].active    = true;
        slots_[slot].completed = false;

        volatile uint16_t* available = reinterpret_cast<volatile uint16_t*>(
            queueMemory_ + availableOffset_);
        const uint16_t availableIndex = available[1];
        available[2 + (availableIndex % queueSize)] =
            static_cast<uint16_t>(head);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        available[1] = static_cast<uint16_t>(availableIndex + 1);
        transport_.notifyQueue(0, notifyOffset_);
    }

    void processUsedLocked() {
        volatile uint16_t* usedHeader =
            reinterpret_cast<volatile uint16_t*>(queueMemory_ + usedOffset_);
        const uint16_t usedIndex =
            __atomic_load_n(usedHeader + 1, __ATOMIC_ACQUIRE);
        auto* elements = reinterpret_cast<volatile VirtQueueUsedElement*>(
            queueMemory_ + usedOffset_ + 4);
        while (lastUsedIndex_ != usedIndex) {
            const uint32_t head = elements[lastUsedIndex_ % queueSize].id;
            if (head % 3 == 0 && head / 3 < requestSlotCount) {
                slots_[head / 3].completed = true;
            }
            ++lastUsedIndex_;
        }
    }

    void fail() {
        volatile uint8_t* common = transport_.common();
        writeMmio<uint8_t>(common, 20,
                           readMmio<uint8_t>(common, 20) | statusFailed);
    }

    os::PciFunctionInfo function_{};
    VirtioPciTransport transport_;
    struct RequestSlot {
        bool active;
        bool completed;
    };
    os::SpinLock requestLock_;
    RequestSlot slots_[requestSlotCount]{};
    uint8_t* queueMemory_        = nullptr;
    uint8_t* requestMemory_      = nullptr;
    uint64_t queuePhysical_      = 0;
    uint64_t requestPhysical_    = 0;
    uint64_t negotiatedFeatures_ = 0;
    uint64_t blockCount_         = 0;
    uint32_t availableOffset_    = 0;
    uint32_t usedOffset_         = 0;
    uint16_t notifyOffset_       = 0;
    uint16_t lastUsedIndex_      = 0;
    bool readOnly_               = false;
    bool online_                 = false;
    uint32_t deviceIndex_        = 0;
};

class GptParser {
   public:
    static bool findFirstPartition(VirtioBlockDriver& driver,
                                   os::PartitionInfo& partition) {
        uint8_t mbr[blockSize]{};
        if (driver.readBlock(0, mbr) != os::StorageError::Success ||
            mbr[510] != 0x55 || mbr[511] != 0xaa) {
            return false;
        }
        for (uint32_t index = 0; index < 4; ++index) {
            const uint8_t* entry = mbr + 446 + index * 16;
            const uint8_t type = entry[4];
            const uint32_t start = readLe32(entry + 8);
            const uint32_t count = readLe32(entry + 12);
            if (type != 0 && type != 0xee && count != 0 &&
                start < driver.blockCount() &&
                count <= driver.blockCount() - start) {
                partition = {start, count, type};
                return true;
            }
        }
        uint8_t header[blockSize]{};
        if (driver.readBlock(1, header) != os::StorageError::Success ||
            !validSignature(header)) {
            return false;
        }
        const uint32_t headerSize         = readLe32(header + 12);
        const uint32_t expectedHeaderCrc  = readLe32(header + 16);
        const uint64_t firstUsable        = readLe64(header + 40);
        const uint64_t lastUsable         = readLe64(header + 48);
        const uint64_t entriesBlock       = readLe64(header + 72);
        const uint32_t entryCount         = readLe32(header + 80);
        const uint32_t entrySize          = readLe32(header + 84);
        const uint32_t expectedEntriesCrc = readLe32(header + 88);
        if (headerSize < 92 || headerSize > blockSize || entryCount == 0 ||
            entryCount > maximumPartitionEntries || entrySize < 128 ||
            entrySize > blockSize || blockSize % entrySize != 0 ||
            firstUsable > lastUsable || lastUsable >= driver.blockCount()) {
            return false;
        }
        uint8_t savedCrc[4] = {header[16], header[17], header[18], header[19]};
        header[16] = header[17] = header[18] = header[19] = 0;
        const uint32_t headerCrc =
            updateCrc32(0xffffffffU, header, headerSize) ^ 0xffffffffU;
        for (uint32_t index = 0; index < 4; ++index) {
            header[16 + index] = savedCrc[index];
        }
        if (headerCrc != expectedHeaderCrc) {
            return false;
        }

        uint8_t block[blockSize]{};
        uint32_t crc          = 0xffffffffU;
        uint64_t currentBlock = ~0ULL;
        for (uint32_t index = 0; index < entryCount; ++index) {
            const uint64_t byteOffset =
                static_cast<uint64_t>(index) * entrySize;
            const uint64_t blockNumber = entriesBlock + byteOffset / blockSize;
            const uint32_t offset =
                static_cast<uint32_t>(byteOffset % blockSize);
            if (offset + entrySize > blockSize || blockNumber != currentBlock) {
                if (driver.readBlock(blockNumber, block) !=
                    os::StorageError::Success) {
                    return false;
                }
                currentBlock = blockNumber;
            }
            crc = updateCrc32(crc, block + offset, entrySize);
            if (partition.blockCount == 0 && !emptyGuid(block + offset)) {
                const uint64_t start = readLe64(block + offset + 32);
                const uint64_t end   = readLe64(block + offset + 40);
                if (start < firstUsable || end > lastUsable || start > end) {
                    return false;
                }
                partition = {start, end - start + 1, 1};
            }
        }
        crc ^= 0xffffffffU;
        return crc == expectedEntriesCrc && partition.blockCount != 0;
    }

   private:
    static bool validSignature(const uint8_t* header) {
        constexpr char signature[] = "EFI PART";
        for (uint32_t index = 0; index < 8; ++index) {
            if (header[index] != static_cast<uint8_t>(signature[index])) {
                return false;
            }
        }
        return true;
    }

    static bool emptyGuid(const uint8_t* value) {
        for (uint32_t index = 0; index < 16; ++index) {
            if (value[index] != 0) {
                return false;
            }
        }
        return true;
    }
};

class StorageService {
   public:
    bool initialize(uint64_t registry) {
        endpoint_    = os::createEndpoint();
        irqEndpoint_ = os::createEndpoint();
        if (endpoint_ == 0 || irqEndpoint_ == 0) {
            return false;
        }
        os::PciFunctionInfo functions[64]{};
        const int count = os::enumeratePci(functions, 64);
        for (int index = 0; index < count && driverCount_ < deviceCapacity;
             ++index) {
            if (functions[index].vendorId != virtioVendorId ||
                functions[index].deviceId != virtioBlockDeviceId) {
                continue;
            }
            if (!drivers_[driverCount_].initialize(
                    functions[index], driverCount_, irqEndpoint_)) {
                return false;
            }
            ++driverCount_;
        }
        if (driverCount_ == 0 ||
            !GptParser::findFirstPartition(drivers_[0], devices_[0])) {
            return false;
        }
        if (driverCount_ > 1) {
            devices_[1] = {0, drivers_[1].blockCount(), 0};
            deviceCount_ = 2;
        } else {
            deviceCount_ = 1;
        }
        if (!os::registerService(registry, os::storageServiceName, endpoint_)) {
            return false;
        }
        return true;
    }

    uint64_t irqEndpoint() const { return irqEndpoint_; }
    void handleInterrupt() {
        for (uint32_t index = 0; index < driverCount_; ++index) {
            drivers_[index].handleInterrupt();
        }
    }

    [[noreturn]] void run() {
        while (true) {
            os::IpcMessage request{};
            if (os::receiveMessage(endpoint_, request) != 0) {
                continue;
            }
            os::IpcMessage reply{};
            if (handle(request, reply)) {
                os::replyMessage(endpoint_, reply);
            }
        }
    }

   private:
    bool handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        const auto type = static_cast<os::StorageRequest>(request.type);
        if (type == os::StorageRequest::ReadAsync ||
            type == os::StorageRequest::WriteAsync) {
            handleAsync(request, type == os::StorageRequest::WriteAsync);
            return false;
        }
        if (type == os::StorageRequest::GetInfo) {
            const uint32_t deviceId = static_cast<uint32_t>(request.data[0]);
            if (deviceId >= deviceCount_) {
                reply.data[0] = encode(os::StorageError::DeviceNotFound);
                reply.length = 1;
                return true;
            }
            VirtioBlockDriver& selected = drivers_[deviceId];
            reply.data[0] = 0;
            reply.data[1] = devices_[deviceId].blockCount;
            reply.data[2] = blockSize;
            reply.data[3] = 1;
            reply.data[4] =
                static_cast<uint64_t>(os::BlockDeviceType::VirtioBlock);
            reply.data[5] = selected.readOnly() ? 1 : 0;
            reply.length  = 6;
            return true;
        }
        if (type == os::StorageRequest::Flush) {
            const uint32_t deviceId = static_cast<uint32_t>(request.data[0]);
            reply.data[0] = deviceId < deviceCount_
                                ? encode(drivers_[deviceId].flush())
                                : encode(os::StorageError::DeviceNotFound);
            reply.length  = 1;
            return true;
        }
        const uint32_t deviceId =
            static_cast<uint32_t>(request.data[0] >> os::storageDeviceShift);
        const uint64_t block  = request.data[0] & os::storageBlockMask;
        const uint32_t offset = static_cast<uint32_t>(request.data[1]);
        const uint32_t size   = static_cast<uint32_t>(request.data[2]);
        const uint32_t maximumSize =
            type == os::StorageRequest::ReadBlocks
                ? (os::ipcDataWordCount - 2) * sizeof(uint64_t)
                : (os::ipcDataWordCount - 3) * sizeof(uint64_t);
        if (deviceId >= deviceCount_ ||
            block >= devices_[deviceId].blockCount || offset > blockSize || size == 0 ||
            size > maximumSize || size > blockSize - offset) {
            reply.data[0] = encode(os::StorageError::InvalidArgument);
            reply.length  = 1;
            return true;
        }
        uint8_t data[blockSize]{};
        VirtioBlockDriver& selected = drivers_[deviceId];
        const uint64_t physicalBlock = devices_[deviceId].startBlock + block;
        os::StorageError result = os::StorageError::Success;
        if (type == os::StorageRequest::ReadBlocks) {
            result = selected.readBlock(physicalBlock, data);
            reply.data[0] = encode(result);
            if (result == os::StorageError::Success) {
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
        } else if (type == os::StorageRequest::WriteBlocks) {
            if (selected.readOnly()) {
                reply.data[0] = encode(os::StorageError::ReadOnly);
            } else {
                if (offset != 0 || size != blockSize) {
                    result =
                        selected.readBlock(physicalBlock, data);
                }
                for (uint32_t index = 0; index < size; ++index) {
                    data[offset + index] = reinterpret_cast<const uint8_t*>(
                        &request.data[3])[index];
                }
                if (result == os::StorageError::Success) {
                    result = selected.writeBlock(physicalBlock, data);
                }
                reply.data[0] = encode(result);
            }
            reply.length = 1;
        } else {
            reply.data[0] = encode(os::StorageError::InvalidArgument);
            reply.length  = 1;
        }
        return true;
    }

    void handleAsync(const os::IpcMessage& request, bool write) {
        if (request.handleCount != 1) {
            for (uint32_t index = 0; index < request.handleCount; ++index) {
                os::closeHandle(request.handles[index]);
            }
            return;
        }
        const uint64_t completionEndpoint = request.handles[0];
        const uint32_t deviceId =
            static_cast<uint32_t>(request.data[0] >> os::storageDeviceShift);
        const uint64_t block = request.data[0] & os::storageBlockMask;
        const uint64_t requestId = request.data[1];
        uint8_t data[blockSize]{};
        os::StorageError result = os::StorageError::InvalidArgument;
        if (deviceId < deviceCount_ && block < devices_[deviceId].blockCount &&
            (!write || (request.data[2] == blockSize &&
                        request.length == os::ipcDataWordCount))) {
            if (write) {
                for (uint32_t index = 0; index < blockSize; ++index) {
                    data[index] = reinterpret_cast<const uint8_t*>(
                        &request.data[3])[index];
                }
                result = drivers_[deviceId].writeBlock(
                    devices_[deviceId].startBlock + block, data);
            } else {
                result = drivers_[deviceId].readBlock(
                    devices_[deviceId].startBlock + block, data);
            }
        }
        os::IpcMessage completion{};
        completion.type    = os::storageCompletionNotification;
        completion.data[0] = requestId;
        completion.data[1] = encode(result);
        completion.data[2] =
            result == os::StorageError::Success ? blockSize : 0;
        completion.length = 3;
        if (!write && result == os::StorageError::Success) {
            for (uint32_t index = 0; index < blockSize; ++index) {
                reinterpret_cast<uint8_t*>(&completion.data[3])[index] =
                    data[index];
            }
            completion.length = os::ipcDataWordCount;
        }
        os::sendMessage(completionEndpoint, completion);
        os::closeHandle(completionEndpoint);
    }

    static uint64_t encode(os::StorageError error) {
        return static_cast<uint64_t>(static_cast<int64_t>(error));
    }

    uint64_t endpoint_    = 0;
    uint64_t irqEndpoint_ = 0;
    static constexpr uint32_t deviceCapacity = 2;
    VirtioBlockDriver drivers_[deviceCapacity];
    os::PartitionInfo devices_[deviceCapacity]{};
    uint32_t driverCount_ = 0;
    uint32_t deviceCount_ = 0;
};

StorageService storageService;

void irqMain() {
    while (true) {
        os::IpcMessage notification{};
        if (os::receiveMessage(storageService.irqEndpoint(), notification) ==
                0 &&
            notification.type == irqNotification) {
            storageService.handleInterrupt();
        }
    }
}

void workerMain() { storageService.run(); }

}  // namespace

extern "C" int userMain() {
    os::log("[STORAGE] Storage Service started in Ring 3\n");
    const uint64_t registry = os::getBootstrapHandle();
    if (registry == 0 || !storageService.initialize(registry) ||
        os::mapMemory(irqStackAddress, irqStackSize, 1) != irqStackAddress ||
        os::mapMemory(workerStackAddress, workerStackSize * workerThreadCount,
                      1) != workerStackAddress ||
        os::createThread(irqMain, irqStackAddress + irqStackSize) == 0) {
        os::log("[STORAGE] VirtIO Block initialization failed\n");
        return 1;
    }
    for (uint32_t index = 0; index < workerThreadCount; ++index) {
        const uint64_t stackTop =
            workerStackAddress + workerStackSize * (index + 1);
        if (os::createThread(workerMain, stackTop) == 0) {
            os::log("[STORAGE] unable to start I/O worker\n");
            return 2;
        }
    }
    os::log("[STORAGE] VirtIO Block, DMA, IRQ and GPT online\n");
    storageService.run();
}
