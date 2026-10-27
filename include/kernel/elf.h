#pragma once

#include <cstddef>
#include <cstdint>

namespace kernel::task {
class Task;
}

namespace kernel::elf {

enum class ElfLoadError : uint8_t {
    None,
    InvalidArgument,
    InvalidMagic,
    UnsupportedClass,
    UnsupportedEndian,
    UnsupportedVersion,
    UnsupportedMachine,
    UnsupportedType,
    InvalidHeader,
    InvalidProgramHeader,
    UnsupportedProgramHeader,
    InvalidSegment,
    InvalidAddress,
    OverlappingSegments,
    InvalidPermissions,
    OutOfMemory,
    MappingFailed,
    InvalidEntryPoint,
    UndefinedSymbol
};

class ElfLoadResult {
   public:
    uint64_t getEntryPoint() const { return entryPoint_; }
    uint64_t getImageBase() const { return imageBase_; }
    uint64_t getImageEnd() const { return imageEnd_; }
    uint64_t getProgramHeaders() const { return programHeaders_; }
    uint64_t getProgramHeaderCount() const { return programHeaderCount_; }
    uint64_t getApplicationEntry() const { return applicationEntry_; }
    uint64_t getApplicationBias() const { return applicationBias_; }
    uint64_t getInterpreterBase() const { return interpreterBase_; }
    void setEntryPoint(uint64_t value) { entryPoint_ = value; }
    void setImageBase(uint64_t value) { imageBase_ = value; }
    void setImageEnd(uint64_t value) { imageEnd_ = value; }
    void setProgramHeaders(uint64_t value) { programHeaders_ = value; }
    void setProgramHeaderCount(uint64_t value) { programHeaderCount_ = value; }
    void setApplicationEntry(uint64_t value) { applicationEntry_ = value; }
    void setApplicationBias(uint64_t value) { applicationBias_ = value; }
    void setInterpreterBase(uint64_t value) { interpreterBase_ = value; }

   private:
    uint64_t entryPoint_         = 0;
    uint64_t imageBase_          = 0;
    uint64_t imageEnd_           = 0;
    uint64_t programHeaders_     = 0;
    uint64_t programHeaderCount_ = 0;
    uint64_t applicationEntry_   = 0;
    uint64_t applicationBias_    = 0;
    uint64_t interpreterBase_    = 0;
};

class ElfSharedImage {
   public:
    ElfSharedImage() = default;
    ElfSharedImage(uint64_t address, uint64_t bytes, const char* name,
                   uint64_t root = 0)
        : sourceAddress_(address),
          size_(bytes),
          name_(name),
          sourceRoot_(root) {}
    uint64_t getSourceAddress() const { return sourceAddress_; }
    uint64_t getSize() const { return size_; }
    const char* getName() const { return name_; }
    uint64_t getSourceRoot() const { return sourceRoot_; }

   private:
    uint64_t sourceAddress_ = 0, size_ = 0;
    const char* name_    = nullptr;
    uint64_t sourceRoot_ = 0;
};

const char* elfErrorName(ElfLoadError error);
bool runElfValidationSelfTest(kernel::task::Task& task);

class Loader {
   public:
    explicit Loader(kernel::task::Task& task);
    explicit Loader(uint64_t destinationRoot)
        : root_(destinationRoot), target_(nullptr) {}
    ElfLoadError load(const void* image, size_t size, ElfLoadResult& result);
    ElfLoadError loadFromAddressSpace(uint64_t sourceRoot,
                                      uint64_t sourceAddress, size_t size,
                                      ElfLoadResult& result);
    ElfLoadError loadDynamicSetFromAddressSpace(
        uint64_t sourceRoot, uint64_t executableAddress, size_t executableSize,
        const ElfSharedImage* sharedImages, uint32_t sharedImageCount,
        ElfLoadResult& result, bool trustedImports = false);

   private:
    uint64_t root_;
    kernel::task::Task* target_;
};
}  // namespace kernel::elf
