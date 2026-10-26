#pragma once
#include <os/ipc.h>
namespace os {
namespace detail {
class StorageClient;
}
class Storage {
   public:
    enum class Error : int64_t {
        Success         = 0,
        DeviceNotFound  = -1,
        DeviceOffline   = -2,
        InvalidBlock    = -3,
        Io              = -4,
        Timeout         = -5,
        ReadOnly        = -6,
        NoSpace         = -7,
        Dma             = -8,
        Corrupted       = -9,
        InvalidArgument = -10
    };

    class Number {
       public:
        Number(uint32_t major = 0, uint32_t minor = 0);

        uint64_t getValue() const;
        uint32_t getMajor() const;
        uint32_t getMinor() const;

        void setMajor(uint32_t value);
        void setMinor(uint32_t value);

       private:
        uint32_t major_, minor_;
    };
    class Part {
       public:
        uint64_t getStartBlock() const;
        uint64_t getBlockCount() const;
        uint32_t getType() const;
        uint32_t getNumber() const;
        const char* getUuid() const;

        void setStartBlock(uint64_t value);
        void setBlockCount(uint64_t value);
        void setType(uint32_t value);
        void setNumber(uint32_t value);
        void setUuid(const char* value);

       private:
        uint64_t startBlock_ = 0, blockCount_ = 0;
        uint32_t type_ = 0, number_ = 0;
        char uuid_[40]{};
    };

    class Device {
       public:
        enum class Type : uint32_t {
            VirtioBlock = 1,
            Ahci,
            Nvme,
            UsbStorage,
            BootImage
        };

        Device();
        Device(const Device&)            = delete;
        Device& operator=(const Device&) = delete;
        int open(uint32_t id);
        ~Device();
        int close();
        bool refresh();
        int read(uint64_t firstBlock, uint32_t count, void* buffer,
                 size_t capacity, uint32_t& completed);
        int write(uint64_t firstBlock, uint32_t count, const void* buffer,
                  size_t size, uint32_t& completed);
        bool flush();

        uint64_t getBlockCount() const;
        uint32_t getBlockSize() const;
        uint32_t getMaxTransferBlocks() const;
        Type getType() const;
        bool getReadOnly() const;
        uint32_t getId() const;
        uint32_t getMajor() const;
        uint32_t getMinor() const;
        uint32_t getParentId() const;
        uint32_t getPartitionNumber() const;
        uint64_t getStartBlock() const;
        const char* getName() const;
        const char* getPartitionUuid() const;
        const char* getHardwarePath() const;

       private:
        uint64_t blockCount_{};
        uint32_t blockSize_{};
        uint32_t maxTransferBlocks_{};
        Type type_{};
        bool readOnly_{};
        uint32_t id_{};
        uint32_t major_{};
        uint32_t minor_{};
        uint32_t parentId_{};
        uint32_t partitionNumber_{};
        uint32_t reserved_{};
        uint64_t startBlock_{};
        char name_[32]{};
        char partitionUuid_[40]{};
        char hardwarePath_[64]{};
        Ipc::Handle service_;
        uint32_t device_ = 0;
        uint32_t locked_ = 0;
        IpcMessage request_{}, reply_{};
        friend class detail::StorageClient;
        friend class Storage;
        bool openService(uint32_t device, uint64_t service);
        void lock();
        void unlock();
        class Guard {
           public:
            explicit Guard(Device& device);
            ~Guard();

           private:
            Device& device_;
        };
        int transfer(uint64_t first, uint32_t count, void* output,
                     const void* input, uint32_t& completed);
    };
    Storage();
    int refresh();
    int queryDevice(uint32_t id, Device& device);
    int nextDevice(uint32_t minimumId, Device& device);
    uint32_t getDeviceCount() const;

   private:
    uint32_t count_{};
};
}  // namespace os
