#pragma once
#include <cstddef>
#include <cstdint>

#include <os/ipc.h>
namespace os {
class File;
class Mem {
   public:
    class Share;
    class Page {
       public:
        Page();
        ~Page();
        Page(const Page&)            = delete;
        Page& operator=(const Page&) = delete;
        int create(uint32_t order, uint16_t node, uint64_t owner = 0);
        int open(Ipc::Handle&& handle);
        int close();
        int refresh();
        uint64_t getHandle() const;
        uint64_t getOwner() const;
        uint64_t getSize() const;
        uint32_t getReferences() const;
        uint16_t getNode() const;
        uint16_t getOrder() const;
        bool getValid() const;
        int getError() const;

       private:
        Ipc::Handle handle_;
        uint64_t owner_{}, size_{};
        uint32_t references_{};
        uint16_t node_{}, order_{};
        int error_{};
    };
    class Map {
       public:
        Map();
        ~Map();
        Map(const Map&)            = delete;
        Map& operator=(const Map&) = delete;
        Map(Map&& other) noexcept;
        Map& operator=(Map&& other) noexcept;
        int create(uint64_t size, uint32_t protection, bool shared = false);
        int open(File& file, uint64_t size, uint64_t offset,
                 uint32_t protection, bool shared);
        int open(Share& memory, uint64_t size, uint64_t offset,
                 uint32_t protection);
        int close();
        uint64_t release();
        int sync(int mode);
        uint64_t getAddress() const;
        uint64_t getSize() const;
        uint32_t getProtection() const;
        uint64_t getOffset() const;
        bool getShared() const;
        bool getMapped() const;
        bool getValid() const;
        int getError() const;
        int setProtection(uint32_t protection);

       private:
        uint64_t address_{}, size_{}, offset_{}, identity_{};
        uint32_t protection_{};
        bool shared_{};
        int error_{};
        Ipc::Handle backing_;
        int bind(int64_t address, uint64_t identity, uint64_t size,
                 uint64_t offset, uint32_t protection, bool shared);
    };
    class Share {
       public:
        Share();
        ~Share();
        Share(const Share&)            = delete;
        Share& operator=(const Share&) = delete;
        Share(Share&& other) noexcept;
        Share& operator=(Share&& other) noexcept;
        int open(const char* name, int flags);
        int close();
        int unlink();
        int resize(uint64_t size);
        int refresh();
        const char* getName() const;
        uint64_t getSize() const;
        uint64_t getHandle() const;
        bool getOpened() const;
        bool getValid() const;
        int getError() const;

       private:
        Ipc::Handle handle_;
        char name_[64]{};
        uint64_t size_{};
        int error_{};
    };
    Mem();
    int refresh(uint64_t taskId = 0);
    int64_t reclaimCaches();
    uint64_t getTotal() const;
    uint64_t getFree() const;
    uint64_t getOwned() const;
    uint64_t getGeneration() const;
    uint32_t getNodeCount() const;
    bool getSnapshotValid() const;

   private:
    uint64_t total_{}, free_{}, owned_{}, generation_{};
    uint32_t nodes_{};
    bool snapshotValid_{};
};
}  // namespace os
