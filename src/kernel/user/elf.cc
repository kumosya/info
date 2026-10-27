#include "kernel/elf.h"

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/task.h"

namespace kernel::elf {

namespace {

constexpr uint8_t ELF_CLASS_64             = 2;
constexpr uint8_t ELF_DATA_LITTLE_ENDIAN   = 1;
constexpr uint8_t ELF_CURRENT_VERSION      = 1;
constexpr uint16_t ELF_TYPE_EXECUTABLE     = 2;
constexpr uint16_t ELF_MACHINE_X86_64      = 62;
constexpr uint32_t PROGRAM_HEADER_LOAD     = 1;
constexpr uint32_t PROGRAM_HEADER_DYNAMIC  = 2;
constexpr uint32_t PROGRAM_HEADER_INTERP   = 3;
constexpr uint32_t SEGMENT_FLAG_EXECUTABLE = 1;
constexpr uint32_t SEGMENT_FLAG_WRITABLE   = 2;
constexpr uint32_t SEGMENT_FLAG_READABLE   = 4;
constexpr uint32_t SEGMENT_FLAGS_SUPPORTED =
    SEGMENT_FLAG_EXECUTABLE | SEGMENT_FLAG_WRITABLE | SEGMENT_FLAG_READABLE;
constexpr uint64_t PAGE_SIZE               = 4096;
constexpr uint64_t MINIMUM_USER_ADDRESS    = 0x10000;
constexpr uint64_t MAXIMUM_USER_ADDRESS    = 0x0000800000000000ULL;
constexpr uint16_t MAXIMUM_PROGRAM_HEADERS = 128;
constexpr uint64_t MAXIMUM_IMAGE_MEMORY    = 256ULL * 1024 * 1024;

struct [[gnu::packed]] ElfHeader {
    uint8_t identifier[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t programHeaderOffset;
    uint64_t sectionHeaderOffset;
    uint32_t flags;
    uint16_t headerSize;
    uint16_t programHeaderSize;
    uint16_t programHeaderCount;
    uint16_t sectionHeaderSize;
    uint16_t sectionHeaderCount;
    uint16_t sectionNameIndex;
};

struct [[gnu::packed]] ProgramHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtualAddress;
    uint64_t physicalAddress;
    uint64_t fileSize;
    uint64_t memorySize;
    uint64_t alignment;
};

struct PagePermissions {
    bool writable;
    bool executable;
};

uint64_t alignDown(uint64_t value) { return value & ~(PAGE_SIZE - 1); }

bool alignUp(uint64_t value, uint64_t& result) {
    if (value > UINT64_MAX - (PAGE_SIZE - 1)) {
        return false;
    }
    result = (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    return true;
}

bool checkedRange(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool isPowerOfTwo(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool rangesOverlap(uint64_t leftStart, uint64_t leftEnd, uint64_t rightStart,
                   uint64_t rightEnd) {
    return leftStart < rightEnd && rightStart < leftEnd;
}

bool isLoadable(const ProgramHeader& header) {
    return header.type == PROGRAM_HEADER_LOAD && header.memorySize != 0;
}

PagePermissions getPagePermissions(const ProgramHeader* headers,
                                   uint16_t headerCount, uint64_t pageAddress) {
    PagePermissions permissions{};
    const uint64_t pageEnd = pageAddress + PAGE_SIZE;
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        const uint64_t segmentEnd = segment.virtualAddress + segment.memorySize;
        if (!rangesOverlap(pageAddress, pageEnd, segment.virtualAddress,
                           segmentEnd)) {
            continue;
        }
        permissions.writable |= (segment.flags & SEGMENT_FLAG_WRITABLE) != 0;
        permissions.executable |=
            (segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0;
    }
    return permissions;
}

ElfLoadError validateHeader(const ElfHeader& header, uint64_t imageSize) {
    if (header.identifier[0] != 0x7f || header.identifier[1] != 'E' ||
        header.identifier[2] != 'L' || header.identifier[3] != 'F') {
        return ElfLoadError::InvalidMagic;
    }
    if (header.identifier[4] != ELF_CLASS_64) {
        return ElfLoadError::UnsupportedClass;
    }
    if (header.identifier[5] != ELF_DATA_LITTLE_ENDIAN) {
        return ElfLoadError::UnsupportedEndian;
    }
    if (header.identifier[6] != ELF_CURRENT_VERSION ||
        header.version != ELF_CURRENT_VERSION) {
        return ElfLoadError::UnsupportedVersion;
    }
    if (header.type != ELF_TYPE_EXECUTABLE) {
        return ElfLoadError::UnsupportedType;
    }
    if (header.machine != ELF_MACHINE_X86_64) {
        return ElfLoadError::UnsupportedMachine;
    }
    if (header.headerSize != sizeof(ElfHeader) ||
        header.programHeaderSize != sizeof(ProgramHeader) ||
        header.programHeaderCount == 0 ||
        header.programHeaderCount > MAXIMUM_PROGRAM_HEADERS ||
        header.programHeaderOffset > imageSize ||
        header.programHeaderCount >
            (imageSize - header.programHeaderOffset) / sizeof(ProgramHeader)) {
        return ElfLoadError::InvalidHeader;
    }
    if (header.entry < MINIMUM_USER_ADDRESS ||
        header.entry >= MAXIMUM_USER_ADDRESS) {
        return ElfLoadError::InvalidEntryPoint;
    }
    return ElfLoadError::None;
}

ElfLoadError validateSegments(const ProgramHeader* headers,
                              uint16_t headerCount, uint64_t imageSize,
                              uint64_t entryPoint, ElfLoadResult& result) {
    bool foundLoadableSegment = false;
    bool entryIsExecutable    = false;
    uint64_t mappedBytes      = 0;
    result.setImageBase(UINT64_MAX);
    result.setImageEnd(0);

    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (segment.type == PROGRAM_HEADER_DYNAMIC ||
            segment.type == PROGRAM_HEADER_INTERP) {
            return ElfLoadError::UnsupportedProgramHeader;
        }
        if (segment.type != PROGRAM_HEADER_LOAD) {
            continue;
        }
        if (segment.fileSize > segment.memorySize ||
            !checkedRange(segment.offset, segment.fileSize, imageSize)) {
            return ElfLoadError::InvalidSegment;
        }
        if (segment.memorySize == 0) {
            continue;
        }
        if ((segment.flags & ~SEGMENT_FLAGS_SUPPORTED) != 0 ||
            ((segment.flags & SEGMENT_FLAG_WRITABLE) != 0 &&
             (segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0)) {
            return ElfLoadError::InvalidPermissions;
        }
        if (segment.alignment > 1 &&
            (!isPowerOfTwo(segment.alignment) ||
             (segment.virtualAddress & (segment.alignment - 1)) !=
                 (segment.offset & (segment.alignment - 1)))) {
            return ElfLoadError::InvalidSegment;
        }
        if (segment.virtualAddress < MINIMUM_USER_ADDRESS ||
            segment.memorySize >
                MAXIMUM_USER_ADDRESS - segment.virtualAddress) {
            return ElfLoadError::InvalidAddress;
        }

        const uint64_t segmentEnd = segment.virtualAddress + segment.memorySize;
        uint64_t pageEnd;
        if (!alignUp(segmentEnd, pageEnd)) {
            return ElfLoadError::InvalidAddress;
        }
        const uint64_t segmentMappedBytes =
            pageEnd - alignDown(segment.virtualAddress);
        if (segmentMappedBytes > MAXIMUM_IMAGE_MEMORY - mappedBytes) {
            return ElfLoadError::InvalidAddress;
        }
        mappedBytes += segmentMappedBytes;

        for (uint16_t previous = 0; previous < index; ++previous) {
            const ProgramHeader& other = headers[previous];
            if (!isLoadable(other)) {
                continue;
            }
            const uint64_t otherEnd = other.virtualAddress + other.memorySize;
            if (rangesOverlap(segment.virtualAddress, segmentEnd,
                              other.virtualAddress, otherEnd)) {
                return ElfLoadError::OverlappingSegments;
            }
        }

        foundLoadableSegment = true;
        if (segment.virtualAddress < result.getImageBase()) {
            result.setImageBase(segment.virtualAddress);
        }
        if (segmentEnd > result.getImageEnd()) {
            result.setImageEnd(segmentEnd);
        }
        if ((segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0 &&
            entryPoint >= segment.virtualAddress && entryPoint < segmentEnd) {
            entryIsExecutable = true;
        }
    }

    if (!foundLoadableSegment) {
        return ElfLoadError::InvalidProgramHeader;
    }
    if (!entryIsExecutable) {
        return ElfLoadError::InvalidEntryPoint;
    }

    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            return ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            const PagePermissions permissions =
                getPagePermissions(headers, headerCount, pageAddress);
            if (permissions.writable && permissions.executable) {
                return ElfLoadError::InvalidPermissions;
            }
        }
    }
    return ElfLoadError::None;
}

ElfLoadError mapSegments(uint64_t root, const uint8_t* image,
                         const ProgramHeader* headers, uint16_t headerCount) {
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            return ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            if (kernel::mm::getPhysicalAddressInAddressSpace(
                    root, pageAddress) != 0) {
                continue;
            }
            const PagePermissions permissions =
                getPagePermissions(headers, headerCount, pageAddress);
            uint64_t pageFlags = kernel::mm::pageUser;
            if (permissions.writable) {
                pageFlags |= kernel::mm::pageWritable;
            }
            if (!permissions.executable) {
                pageFlags |= kernel::mm::pageNoExecute;
            }
            const kernel::mm::UserPageMapResult mapResult =
                kernel::mm::allocateUserPageInAddressSpace(root, pageAddress,
                                                           pageFlags);
            if (mapResult == kernel::mm::UserPageMapResult::OutOfMemory) {
                return ElfLoadError::OutOfMemory;
            }
            if (mapResult != kernel::mm::UserPageMapResult::Success) {
                return ElfLoadError::MappingFailed;
            }
        }
    }

    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment) || segment.fileSize == 0) {
            continue;
        }
        if (!kernel::mm::copyToAddressSpace(root, segment.virtualAddress,
                                            image + segment.offset,
                                            segment.fileSize)) {
            return ElfLoadError::MappingFailed;
        }
    }
    return ElfLoadError::None;
}

ElfLoadError mapSegmentsFromAddressSpace(uint64_t root, uint64_t sourceRoot,
                                         uint64_t sourceAddress,
                                         const ProgramHeader* headers,
                                         uint16_t headerCount) {
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            return ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            if (kernel::mm::getPhysicalAddressInAddressSpace(
                    root, pageAddress) != 0) {
                continue;
            }
            const PagePermissions permissions =
                getPagePermissions(headers, headerCount, pageAddress);
            uint64_t pageFlags = kernel::mm::pageUser;
            if (permissions.writable) {
                pageFlags |= kernel::mm::pageWritable;
            }
            if (!permissions.executable) {
                pageFlags |= kernel::mm::pageNoExecute;
            }
            const kernel::mm::UserPageMapResult mapResult =
                kernel::mm::allocateUserPageInAddressSpace(root, pageAddress,
                                                           pageFlags);
            if (mapResult == kernel::mm::UserPageMapResult::OutOfMemory) {
                return ElfLoadError::OutOfMemory;
            }
            if (mapResult != kernel::mm::UserPageMapResult::Success) {
                return ElfLoadError::MappingFailed;
            }
        }
    }

    uint8_t buffer[PAGE_SIZE];
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        uint64_t copied              = 0;
        while (isLoadable(segment) && copied < segment.fileSize) {
            const uint64_t remaining = segment.fileSize - copied;
            const uint64_t chunk =
                remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (!kernel::mm::copyFromAddressSpace(
                    buffer, sourceRoot, sourceAddress + segment.offset + copied,
                    chunk) ||
                !kernel::mm::copyToAddressSpace(
                    root, segment.virtualAddress + copied, buffer, chunk)) {
                return ElfLoadError::MappingFailed;
            }
            copied += chunk;
        }
    }
    return ElfLoadError::None;
}

void rollbackMappedSegments(uint64_t root, const ProgramHeader* headers,
                            uint16_t headerCount) {
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            continue;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            const uint64_t physical =
                kernel::mm::getPhysicalAddressInAddressSpace(root, pageAddress);
            if (physical == 0) {
                continue;
            }
            if (kernel::mm::unmapPageInAddressSpace(root, pageAddress)) {
                kernel::mm::freePhysicalPage(physical & ~(PAGE_SIZE - 1));
            }
        }
    }
}

}  // namespace

Loader::Loader(::kernel::task::Task& task) : root_(0), target_(&task) {}

ElfLoadError Loader::load(const void* image, size_t size,
                          ElfLoadResult& result) {
    result = {};
    if (!target_) return ElfLoadError::InvalidArgument;
    ::kernel::task::Task& task = *target_;
    if (image == nullptr || size < sizeof(ElfHeader)) {
        return ElfLoadError::InvalidArgument;
    }

    const uint8_t* bytes           = static_cast<const uint8_t*>(image);
    const ElfHeader& header        = *reinterpret_cast<const ElfHeader*>(bytes);
    const ElfLoadError headerError = validateHeader(header, size);
    if (headerError != ElfLoadError::None) {
        return headerError;
    }
    const ProgramHeader* headers = reinterpret_cast<const ProgramHeader*>(
        bytes + header.programHeaderOffset);
    const ElfLoadError segmentError = validateSegments(
        headers, header.programHeaderCount, size, header.entry, result);
    if (segmentError != ElfLoadError::None) {
        result = {};
        return segmentError;
    }

    ::kernel::task::PreemptionSpinLockGuard addressGuard(
        task.addressSpace().lock());
    const uint64_t root = task.addressSpace().rootPhysicalAddress();
    for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            result = {};
            return ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            if (mm::getPhysicalAddressInAddressSpace(root, pageAddress) != 0) {
                result = {};
                return ElfLoadError::MappingFailed;
            }
        }
    }

    const ElfLoadError mappingError =
        mapSegments(root, bytes, headers, header.programHeaderCount);
    if (mappingError != ElfLoadError::None) {
        rollbackMappedSegments(root, headers, header.programHeaderCount);
        result = {};
        return mappingError;
    }
    result.setEntryPoint(header.entry);
    return ElfLoadError::None;
}

ElfLoadError Loader::loadFromAddressSpace(uint64_t sourceRoot,
                                          uint64_t sourceAddress, size_t size,
                                          ElfLoadResult& result) {
    if (target_) {
        ::kernel::task::PreemptionSpinLockGuard guard(
            target_->addressSpace().lock());
        return Loader(target_->addressSpace().rootPhysicalAddress())
            .loadFromAddressSpace(sourceRoot, sourceAddress, size, result);
    }
    const uint64_t destinationRoot = root_;
    result                         = {};
    if (destinationRoot == 0 || sourceRoot == 0 || sourceAddress == 0 ||
        size < sizeof(ElfHeader)) {
        return ElfLoadError::InvalidArgument;
    }
    ElfHeader header{};
    if (!mm::copyFromAddressSpace(&header, sourceRoot, sourceAddress,
                                  sizeof(header))) {
        return ElfLoadError::InvalidArgument;
    }
    const ElfLoadError headerError = validateHeader(header, size);
    if (headerError != ElfLoadError::None) {
        return headerError;
    }
    ProgramHeader headers[MAXIMUM_PROGRAM_HEADERS]{};
    const uint64_t headerBytes =
        static_cast<uint64_t>(header.programHeaderCount) *
        sizeof(ProgramHeader);
    if (!mm::copyFromAddressSpace(headers, sourceRoot,
                                  sourceAddress + header.programHeaderOffset,
                                  headerBytes)) {
        return ElfLoadError::InvalidHeader;
    }
    const ElfLoadError segmentError = validateSegments(
        headers, header.programHeaderCount, size, header.entry, result);
    if (segmentError != ElfLoadError::None) {
        result = {};
        return segmentError;
    }

    const uint64_t root = destinationRoot;
    for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            result = {};
            return ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            if (mm::getPhysicalAddressInAddressSpace(root, pageAddress) != 0) {
                result = {};
                return ElfLoadError::MappingFailed;
            }
        }
    }
    const ElfLoadError mappingError = mapSegmentsFromAddressSpace(
        root, sourceRoot, sourceAddress, headers, header.programHeaderCount);
    if (mappingError != ElfLoadError::None) {
        rollbackMappedSegments(root, headers, header.programHeaderCount);
        result = {};
        return mappingError;
    }
    result.setEntryPoint(header.entry);
    return ElfLoadError::None;
}

const char* elfErrorName(ElfLoadError error) {
    switch (error) {
        case ElfLoadError::None:
            return "none";
        case ElfLoadError::InvalidArgument:
            return "invalid argument";
        case ElfLoadError::InvalidMagic:
            return "invalid magic";
        case ElfLoadError::UnsupportedClass:
            return "unsupported class";
        case ElfLoadError::UnsupportedEndian:
            return "unsupported endian";
        case ElfLoadError::UnsupportedVersion:
            return "unsupported version";
        case ElfLoadError::UnsupportedMachine:
            return "unsupported machine";
        case ElfLoadError::UnsupportedType:
            return "unsupported type";
        case ElfLoadError::InvalidHeader:
            return "invalid header";
        case ElfLoadError::InvalidProgramHeader:
            return "invalid program header";
        case ElfLoadError::UnsupportedProgramHeader:
            return "unsupported program header";
        case ElfLoadError::InvalidSegment:
            return "invalid segment";
        case ElfLoadError::InvalidAddress:
            return "invalid address";
        case ElfLoadError::OverlappingSegments:
            return "overlapping segments";
        case ElfLoadError::InvalidPermissions:
            return "invalid permissions";
        case ElfLoadError::OutOfMemory:
            return "out of memory";
        case ElfLoadError::MappingFailed:
            return "mapping failed";
        case ElfLoadError::InvalidEntryPoint:
            return "invalid entry point";
        case ElfLoadError::UndefinedSymbol:
            return "undefined symbol";
    }
    return "unknown";
}

bool runElfValidationSelfTest(::kernel::task::Task& task) {
    struct TestImage {
        ElfHeader header;
        ProgramHeader segments[2];
        uint8_t data[16];
    } image{};

    image.header.identifier[0]       = 0x7f;
    image.header.identifier[1]       = 'E';
    image.header.identifier[2]       = 'L';
    image.header.identifier[3]       = 'F';
    image.header.identifier[4]       = ELF_CLASS_64;
    image.header.identifier[5]       = ELF_DATA_LITTLE_ENDIAN;
    image.header.identifier[6]       = ELF_CURRENT_VERSION;
    image.header.type                = ELF_TYPE_EXECUTABLE;
    image.header.machine             = ELF_MACHINE_X86_64;
    image.header.version             = ELF_CURRENT_VERSION;
    image.header.entry               = 0x400000;
    image.header.programHeaderOffset = sizeof(ElfHeader);
    image.header.headerSize          = sizeof(ElfHeader);
    image.header.programHeaderSize   = sizeof(ProgramHeader);
    image.header.programHeaderCount  = 1;
    image.segments[0].type           = PROGRAM_HEADER_LOAD;
    image.segments[0].flags = SEGMENT_FLAG_READABLE | SEGMENT_FLAG_EXECUTABLE;
    image.segments[0].virtualAddress = 0x400000;
    image.segments[0].memorySize     = PAGE_SIZE;
    image.segments[0].alignment      = 1;

    ElfLoadResult result{};
    image.header.identifier[0] = 0;
    if (Loader(task).load(&image, sizeof(image), result) !=
        ElfLoadError::InvalidMagic) {
        return false;
    }

    image.header.identifier[0] = 0x7f;
    image.segments[0].flags =
        SEGMENT_FLAG_READABLE | SEGMENT_FLAG_WRITABLE | SEGMENT_FLAG_EXECUTABLE;
    if (Loader(task).load(&image, sizeof(image), result) !=
        ElfLoadError::InvalidPermissions) {
        return false;
    }

    image.segments[0].flags = SEGMENT_FLAG_READABLE | SEGMENT_FLAG_EXECUTABLE;
    image.header.programHeaderCount  = 2;
    image.segments[1].type           = PROGRAM_HEADER_LOAD;
    image.segments[1].flags          = SEGMENT_FLAG_READABLE;
    image.segments[1].virtualAddress = 0x400800;
    image.segments[1].memorySize     = PAGE_SIZE;
    image.segments[1].alignment      = 1;
    if (Loader(task).load(&image, sizeof(image), result) !=
        ElfLoadError::OverlappingSegments) {
        return false;
    }

    image.header.programHeaderCount = 1;
    image.header.entry              = 0x600000;
    if (Loader(task).load(&image, sizeof(image), result) !=
        ElfLoadError::InvalidEntryPoint) {
        return false;
    }

    image.header.entry               = 0x400000;
    image.header.programHeaderCount  = 2;
    image.segments[0].memorySize     = PAGE_SIZE / 2;
    image.segments[0].fileSize       = sizeof(image.data) / 2;
    image.segments[0].offset         = __builtin_offsetof(TestImage, data);
    image.segments[1].virtualAddress = 0x400000 + PAGE_SIZE / 2;
    image.segments[1].memorySize     = PAGE_SIZE / 2;
    image.segments[1].fileSize       = sizeof(image.data) / 2;
    image.segments[1].offset =
        __builtin_offsetof(TestImage, data) + sizeof(image.data) / 2;
    image.segments[1].flags = SEGMENT_FLAG_READABLE | SEGMENT_FLAG_WRITABLE;
    if (Loader(task).load(&image, sizeof(image), result) !=
        ElfLoadError::InvalidPermissions) {
        return false;
    }

    image.segments[1].flags = SEGMENT_FLAG_READABLE;
    if (Loader(task).load(&image, sizeof(image), result) !=
            ElfLoadError::None ||
        result.getEntryPoint() != 0x400000 ||
        result.getImageBase() != 0x400000 || result.getImageEnd() != 0x401000) {
        return false;
    }
    uint64_t flags = 0;
    return mm::getPhysicalAddressInAddressSpace(
               task.addressSpace().rootPhysicalAddress(), 0x400000, &flags) !=
               0 &&
           (flags & ::kernel::mm::pageUser) != 0 &&
           (flags & ::kernel::mm::pageWritable) == 0 &&
           (flags & ::kernel::mm::pageNoExecute) == 0;
}

}  // namespace kernel::elf
