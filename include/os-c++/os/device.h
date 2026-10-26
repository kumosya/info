#pragma once
#include <cstdint>
namespace os {
class Ipc;
class Device {
   public:
    class Pci {
       public:
        Pci();
        int enumerate(Device::Pci* functions, uint32_t capacity);
        int query(uint16_t segment, uint8_t bus, uint8_t device,
                  uint8_t function, Device::Pci& result);
        int readConfig(uint16_t offset, uint32_t& value);
        int writeConfig(uint16_t offset, uint32_t value);
        int open(uint16_t segment, uint8_t bus, uint8_t device,
                 uint8_t function);
        int close();
        uint16_t getSegment() const;
        uint8_t getBus() const;
        uint8_t getDevice() const;
        uint8_t getFunction() const;
        uint8_t getClassCode() const;
        uint8_t getSubclass() const;
        uint8_t getProgrammingInterface() const;
        uint16_t getVendorId() const;
        uint16_t getDeviceId() const;
        uint8_t getHeaderType() const;

       private:
        friend class Device;
        void decode(const void* bytes);
        void encode(void* bytes) const;
        bool valid_{};
        uint16_t segment_{};
        uint8_t bus_{}, device_{}, function_{}, classCode_{}, subclass_{},
            programmingInterface_{};
        uint16_t vendorId_{}, deviceId_{};
        uint8_t headerType_{};
    };
    class Dma;
    class Mmio;
    class Frame;
    class Irq;
    Device(const Device&)            = delete;
    Device& operator=(const Device&) = delete;
    ~Device();
    Device();
    Device& operator=(Device&& other) noexcept;
    int stop();
    int open(const Pci& function);
    int close();
    uint64_t getHandle() const;
    bool getValid() const;
    int getError() const;

   private:
    uint64_t handle_{};
    Pci function_;
    int error_{};
};
class Device::Dma {
   public:
    Dma(const Dma&)            = delete;
    Dma& operator=(const Dma&) = delete;
    ~Dma();
    Dma();
    Device::Dma& operator=(Device::Dma&& other) noexcept;
    int create(Device& device, uint64_t size, uint64_t alignment = 4096);
    int close();
    uint64_t getHandle() const;
    uint64_t getVirtualAddress() const;
    uint64_t getDeviceAddress() const;
    uint64_t getSize() const;
    bool getValid() const;
    int getError() const;

   private:
    uint64_t handle_{}, virtualAddress_{}, deviceAddress_{}, size_{};
    int error_{};
};
class Device::Mmio {
   public:
    Mmio(const Mmio&)            = delete;
    Mmio& operator=(const Mmio&) = delete;
    ~Mmio();
    Mmio();
    Device::Mmio& operator=(Device::Mmio&& other) noexcept;
    int open(Device& device, uint8_t bar, uint64_t offset, uint64_t size);
    int close();
    uint64_t getAddress() const;
    uint64_t getSize() const;
    uint8_t getBarIndex() const;
    bool getValid() const;
    int getError() const;

   private:
    uint64_t handle_{}, address_{}, size_{};
    uint8_t bar_{};
    int error_{};
};
class Device::Frame {
   public:
    enum class Format : uint8_t {
        Unknown = 0,
        Rgb     = 1,
        Bgr     = 2,
        Argb    = 3,
        Rgba    = 4,
        Bgra    = 5
    };
    Frame(const Frame&)            = delete;
    Frame& operator=(const Frame&) = delete;
    ~Frame();
    Frame();
    Device::Frame& operator=(Device::Frame&& other) noexcept;
    int refresh();
    int open(uint64_t address = 0);
    int close();
    uint64_t getAddress() const;
    uint64_t getSize() const;
    uint32_t getWidth() const;
    uint32_t getHeight() const;
    uint32_t getPitch() const;
    uint8_t getBitsPerPixel() const;
    Format getFormat() const;
    uint8_t getRedPosition() const;
    uint8_t getGreenPosition() const;
    uint8_t getBluePosition() const;
    bool getValid() const;
    int getError() const;

   private:
    void decode(const void* bytes);
    uint64_t handle_{}, address_{}, size_{};
    uint32_t width_{}, height_{}, pitch_{};
    uint8_t bitsPerPixel_{}, redPosition_{}, greenPosition_{}, bluePosition_{};
    Format format_{};
    int error_{};
};
class Device::Irq {
   public:
    Irq(const Irq&)            = delete;
    Irq& operator=(const Irq&) = delete;
    ~Irq();
    Irq();
    Device::Irq& operator=(Device::Irq&& other) noexcept;
    int acknowledge();
    int create(Device& device, uint8_t irq, Ipc& endpoint,
               bool levelTriggered = false);
    int close();
    uint8_t getIrq() const;
    uint64_t getHandle() const;
    bool getValid() const;
    int getError() const;

   private:
    uint64_t handle_{};
    uint8_t irq_{};
    int error_{};
};
}  // namespace os
