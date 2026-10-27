#pragma once

#include <cstdint>

constexpr uint32_t multiboot2Magic             = 0x36d76289;
constexpr uint32_t multibootTagEnd             = 0;
constexpr uint32_t multibootTagBootCommandLine = 1;
constexpr uint32_t multibootTagBootLoaderName  = 2;
constexpr uint32_t multibootTagModule          = 3;
constexpr uint32_t multibootTagMemoryMap       = 6;
constexpr uint32_t multibootTagFramebuffer     = 8;
constexpr uint32_t multibootTagElfSections     = 9;
constexpr uint32_t multibootTagAcpiOld         = 14;
constexpr uint32_t multibootTagAcpiNew         = 15;

constexpr uint32_t memoryAvailable = 1;

class MemoryRegion {
   public:
    uint64_t getBase() const { return base_; }
    uint64_t getLength() const { return length_; }
    uint32_t getType() const { return type_; }
    void setBase(uint64_t value) { base_ = value; }
    void setLength(uint64_t value) { length_ = value; }
    void setType(uint32_t value) { type_ = value; }

   private:
    uint64_t base_{};
    uint64_t length_{};
    uint32_t type_{};
};

class BootModule {
   public:
    uint32_t getStart() const { return start_; }
    uint32_t getEnd() const { return end_; }
    const char* getCommandLine() const { return commandLine_; }
    void setStart(uint32_t value) { start_ = value; }
    void setEnd(uint32_t value) { end_ = value; }
    void setCommandLine(const char* value) { commandLine_ = value; }

   private:
    uint32_t start_{};
    uint32_t end_{};
    const char* commandLine_{};
};

class FramebufferInfo {
   public:
    uint64_t getAddress() const { return address_; }
    uint32_t getPitch() const { return pitch_; }
    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    uint8_t getBitsPerPixel() const { return bitsPerPixel_; }
    uint8_t getType() const { return type_; }
    uint8_t getRedPosition() const { return redPosition_; }
    uint8_t getRedMaskSize() const { return redMaskSize_; }
    uint8_t getGreenPosition() const { return greenPosition_; }
    uint8_t getGreenMaskSize() const { return greenMaskSize_; }
    uint8_t getBluePosition() const { return bluePosition_; }
    uint8_t getBlueMaskSize() const { return blueMaskSize_; }
    void setAddress(uint64_t value) { address_ = value; }
    void setPitch(uint32_t value) { pitch_ = value; }
    void setWidth(uint32_t value) { width_ = value; }
    void setHeight(uint32_t value) { height_ = value; }
    void setBitsPerPixel(uint8_t value) { bitsPerPixel_ = value; }
    void setType(uint8_t value) { type_ = value; }
    void setRedPosition(uint8_t value) { redPosition_ = value; }
    void setRedMaskSize(uint8_t value) { redMaskSize_ = value; }
    void setGreenPosition(uint8_t value) { greenPosition_ = value; }
    void setGreenMaskSize(uint8_t value) { greenMaskSize_ = value; }
    void setBluePosition(uint8_t value) { bluePosition_ = value; }
    void setBlueMaskSize(uint8_t value) { blueMaskSize_ = value; }

   private:
    uint64_t address_{};
    uint32_t pitch_{};
    uint32_t width_{};
    uint32_t height_{};
    uint8_t bitsPerPixel_{};
    uint8_t type_{};
    uint8_t redPosition_{};
    uint8_t redMaskSize_{};
    uint8_t greenPosition_{};
    uint8_t greenMaskSize_{};
    uint8_t bluePosition_{};
    uint8_t blueMaskSize_{};
};

class BootInfo {
   public:
    const MemoryRegion* getMemoryMap() const { return memoryMap_; }
    uint32_t getMemoryMapCount() const { return memoryMapCount_; }
    const BootModule* getModules() const { return modules_; }
    uint32_t getModuleCount() const { return moduleCount_; }
    const char* getCommandLine() const { return commandLine_; }
    const char* getBootLoaderName() const { return bootLoaderName_; }
    const void* getAcpiRoot() const { return acpiRoot_; }
    const void* getElfSections() const { return elfSections_; }
    FramebufferInfo& getFramebuffer() { return framebuffer_; }
    const FramebufferInfo& getFramebuffer() const { return framebuffer_; }
    uint64_t getMultibootAddress() const { return multibootAddress_; }
    uint32_t getMultibootSize() const { return multibootSize_; }
    uint32_t getReserved() const { return reserved_; }
    uint64_t getPageTableRoot() const { return pageTableRoot_; }
    uint64_t getBootstrapStart() const { return bootstrapStart_; }
    uint64_t getBootstrapEnd() const { return bootstrapEnd_; }
    void setMemoryMap(const MemoryRegion* value) { memoryMap_ = value; }
    void setMemoryMapCount(uint32_t value) { memoryMapCount_ = value; }
    void setModules(const BootModule* value) { modules_ = value; }
    void setModuleCount(uint32_t value) { moduleCount_ = value; }
    void setCommandLine(const char* value) { commandLine_ = value; }
    void setBootLoaderName(const char* value) { bootLoaderName_ = value; }
    void setAcpiRoot(const void* value) { acpiRoot_ = value; }
    void setElfSections(const void* value) { elfSections_ = value; }
    void setFramebuffer(FramebufferInfo value) { framebuffer_ = value; }
    void setMultibootAddress(uint64_t value) { multibootAddress_ = value; }
    void setMultibootSize(uint32_t value) { multibootSize_ = value; }
    void setReserved(uint32_t value) { reserved_ = value; }
    void setPageTableRoot(uint64_t value) { pageTableRoot_ = value; }
    void setBootstrapStart(uint64_t value) { bootstrapStart_ = value; }
    void setBootstrapEnd(uint64_t value) { bootstrapEnd_ = value; }

   private:
    const MemoryRegion* memoryMap_{};
    uint32_t memoryMapCount_{};
    const BootModule* modules_{};
    uint32_t moduleCount_{};
    const char* commandLine_{};
    const char* bootLoaderName_{};
    const void* acpiRoot_{};
    const void* elfSections_{};
    FramebufferInfo framebuffer_{};
    uint64_t multibootAddress_{};
    uint32_t multibootSize_{};
    uint32_t reserved_{};
    uint64_t pageTableRoot_{};
    uint64_t bootstrapStart_{};
    uint64_t bootstrapEnd_{};
};

bool parseMultibootInfo(uint64_t address, BootInfo* bootInfo);
