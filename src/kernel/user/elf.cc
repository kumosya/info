#include "kernel/elf.h"

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/process.h"
#include "kernel/spinlock.h"

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

kernel::ElfLoadError validateHeader(const ElfHeader& header,
                                    uint64_t imageSize) {
    if (header.identifier[0] != 0x7f || header.identifier[1] != 'E' ||
        header.identifier[2] != 'L' || header.identifier[3] != 'F') {
        return kernel::ElfLoadError::InvalidMagic;
    }
    if (header.identifier[4] != ELF_CLASS_64) {
        return kernel::ElfLoadError::UnsupportedClass;
    }
    if (header.identifier[5] != ELF_DATA_LITTLE_ENDIAN) {
        return kernel::ElfLoadError::UnsupportedEndian;
    }
    if (header.identifier[6] != ELF_CURRENT_VERSION ||
        header.version != ELF_CURRENT_VERSION) {
        return kernel::ElfLoadError::UnsupportedVersion;
    }
    if (header.type != ELF_TYPE_EXECUTABLE) {
        return kernel::ElfLoadError::UnsupportedType;
    }
    if (header.machine != ELF_MACHINE_X86_64) {
        return kernel::ElfLoadError::UnsupportedMachine;
    }
    if (header.headerSize != sizeof(ElfHeader) ||
        header.programHeaderSize != sizeof(ProgramHeader) ||
        header.programHeaderCount == 0 ||
        header.programHeaderCount > MAXIMUM_PROGRAM_HEADERS ||
        header.programHeaderOffset > imageSize ||
        header.programHeaderCount >
            (imageSize - header.programHeaderOffset) / sizeof(ProgramHeader)) {
        return kernel::ElfLoadError::InvalidHeader;
    }
    if (header.entry < MINIMUM_USER_ADDRESS ||
        header.entry >= MAXIMUM_USER_ADDRESS) {
        return kernel::ElfLoadError::InvalidEntryPoint;
    }
    return kernel::ElfLoadError::None;
}

kernel::ElfLoadError validateSegments(const ProgramHeader* headers,
                                      uint16_t headerCount, uint64_t imageSize,
                                      uint64_t entryPoint,
                                      kernel::ElfLoadResult& result) {
    bool foundLoadableSegment = false;
    bool entryIsExecutable    = false;
    uint64_t mappedBytes      = 0;
    result.imageBase          = UINT64_MAX;
    result.imageEnd           = 0;

    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (segment.type == PROGRAM_HEADER_DYNAMIC ||
            segment.type == PROGRAM_HEADER_INTERP) {
            return kernel::ElfLoadError::UnsupportedProgramHeader;
        }
        if (segment.type != PROGRAM_HEADER_LOAD) {
            continue;
        }
        if (segment.fileSize > segment.memorySize ||
            !checkedRange(segment.offset, segment.fileSize, imageSize)) {
            return kernel::ElfLoadError::InvalidSegment;
        }
        if (segment.memorySize == 0) {
            continue;
        }
        if ((segment.flags & ~SEGMENT_FLAGS_SUPPORTED) != 0 ||
            ((segment.flags & SEGMENT_FLAG_WRITABLE) != 0 &&
             (segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0)) {
            return kernel::ElfLoadError::InvalidPermissions;
        }
        if (segment.alignment > 1 &&
            (!isPowerOfTwo(segment.alignment) ||
             (segment.virtualAddress & (segment.alignment - 1)) !=
                 (segment.offset & (segment.alignment - 1)))) {
            return kernel::ElfLoadError::InvalidSegment;
        }
        if (segment.virtualAddress < MINIMUM_USER_ADDRESS ||
            segment.memorySize >
                MAXIMUM_USER_ADDRESS - segment.virtualAddress) {
            return kernel::ElfLoadError::InvalidAddress;
        }

        const uint64_t segmentEnd = segment.virtualAddress + segment.memorySize;
        uint64_t pageEnd;
        if (!alignUp(segmentEnd, pageEnd)) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        const uint64_t segmentMappedBytes =
            pageEnd - alignDown(segment.virtualAddress);
        if (segmentMappedBytes > MAXIMUM_IMAGE_MEMORY - mappedBytes) {
            return kernel::ElfLoadError::InvalidAddress;
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
                return kernel::ElfLoadError::OverlappingSegments;
            }
        }

        foundLoadableSegment = true;
        if (segment.virtualAddress < result.imageBase) {
            result.imageBase = segment.virtualAddress;
        }
        if (segmentEnd > result.imageEnd) {
            result.imageEnd = segmentEnd;
        }
        if ((segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0 &&
            entryPoint >= segment.virtualAddress && entryPoint < segmentEnd) {
            entryIsExecutable = true;
        }
    }

    if (!foundLoadableSegment) {
        return kernel::ElfLoadError::InvalidProgramHeader;
    }
    if (!entryIsExecutable) {
        return kernel::ElfLoadError::InvalidEntryPoint;
    }

    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            const PagePermissions permissions =
                getPagePermissions(headers, headerCount, pageAddress);
            if (permissions.writable && permissions.executable) {
                return kernel::ElfLoadError::InvalidPermissions;
            }
        }
    }
    return kernel::ElfLoadError::None;
}

kernel::ElfLoadError mapSegments(uint64_t root, const uint8_t* image,
                                 const ProgramHeader* headers,
                                 uint16_t headerCount) {
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            if (kernel::getPhysicalAddressInAddressSpace(root, pageAddress) !=
                0) {
                continue;
            }
            const PagePermissions permissions =
                getPagePermissions(headers, headerCount, pageAddress);
            uint64_t pageFlags = kernel::pageUser;
            if (permissions.writable) {
                pageFlags |= kernel::pageWritable;
            }
            if (!permissions.executable) {
                pageFlags |= kernel::pageNoExecute;
            }
            const kernel::UserPageMapResult mapResult =
                kernel::allocateUserPageInAddressSpace(root, pageAddress,
                                                       pageFlags);
            if (mapResult == kernel::UserPageMapResult::OutOfMemory) {
                return kernel::ElfLoadError::OutOfMemory;
            }
            if (mapResult != kernel::UserPageMapResult::Success) {
                return kernel::ElfLoadError::MappingFailed;
            }
        }
    }

    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment) || segment.fileSize == 0) {
            continue;
        }
        if (!kernel::copyToAddressSpace(root, segment.virtualAddress,
                                        image + segment.offset,
                                        segment.fileSize)) {
            return kernel::ElfLoadError::MappingFailed;
        }
    }
    return kernel::ElfLoadError::None;
}

kernel::ElfLoadError mapSegmentsFromAddressSpace(
    uint64_t root, uint64_t sourceRoot, uint64_t sourceAddress,
    const ProgramHeader* headers, uint16_t headerCount) {
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (!isLoadable(segment)) {
            continue;
        }
        uint64_t pageEnd = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, pageEnd)) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        for (uint64_t pageAddress = alignDown(segment.virtualAddress);
             pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
            if (kernel::getPhysicalAddressInAddressSpace(root, pageAddress) !=
                0) {
                continue;
            }
            const PagePermissions permissions =
                getPagePermissions(headers, headerCount, pageAddress);
            uint64_t pageFlags = kernel::pageUser;
            if (permissions.writable) {
                pageFlags |= kernel::pageWritable;
            }
            if (!permissions.executable) {
                pageFlags |= kernel::pageNoExecute;
            }
            const kernel::UserPageMapResult mapResult =
                kernel::allocateUserPageInAddressSpace(root, pageAddress,
                                                       pageFlags);
            if (mapResult == kernel::UserPageMapResult::OutOfMemory) {
                return kernel::ElfLoadError::OutOfMemory;
            }
            if (mapResult != kernel::UserPageMapResult::Success) {
                return kernel::ElfLoadError::MappingFailed;
            }
        }
    }

    uint8_t buffer[PAGE_SIZE];
    for (uint16_t index = 0; index < headerCount; ++index) {
        const ProgramHeader& segment = headers[index];
        uint64_t copied = 0;
        while (isLoadable(segment) && copied < segment.fileSize) {
            const uint64_t remaining = segment.fileSize - copied;
            const uint64_t chunk = remaining < sizeof(buffer)
                                       ? remaining
                                       : sizeof(buffer);
            if (!kernel::copyFromAddressSpace(
                    buffer, sourceRoot,
                    sourceAddress + segment.offset + copied, chunk) ||
                !kernel::copyToAddressSpace(
                    root, segment.virtualAddress + copied, buffer, chunk)) {
                return kernel::ElfLoadError::MappingFailed;
            }
            copied += chunk;
        }
    }
    return kernel::ElfLoadError::None;
}

void rollbackMappedSegments(uint64_t root,
                            const ProgramHeader* headers,
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
                kernel::getPhysicalAddressInAddressSpace(root, pageAddress);
            if (physical == 0) {
                continue;
            }
            if (kernel::unmapPageInAddressSpace(root, pageAddress)) {
                kernel::freePhysicalPage(physical & ~(PAGE_SIZE - 1));
            }
        }
    }
}

}  // namespace

namespace kernel {

ElfLoadError ElfLoader::load(Process& process, const void* image, size_t size,
                             ElfLoadResult& result) {
    result = {};
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

    PreemptionSpinLockGuard addressGuard(process.addressSpace().lock());
    const uint64_t root = process.addressSpace().rootPhysicalAddress();
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
            if (getPhysicalAddressInAddressSpace(root, pageAddress) != 0) {
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
    result.entryPoint = header.entry;
    return ElfLoadError::None;
}

ElfLoadError ElfLoader::loadFromAddressSpace(
    Process& process, uint64_t sourceRoot, uint64_t sourceAddress, size_t size,
    ElfLoadResult& result) {
    PreemptionSpinLockGuard addressGuard(process.addressSpace().lock());
    return loadFromAddressSpace(process.addressSpace().rootPhysicalAddress(),
                                sourceRoot, sourceAddress, size, result);
}

ElfLoadError ElfLoader::loadFromAddressSpace(
    uint64_t destinationRoot, uint64_t sourceRoot, uint64_t sourceAddress,
    size_t size, ElfLoadResult& result) {
    result = {};
    if (destinationRoot == 0 || sourceRoot == 0 || sourceAddress == 0 ||
        size < sizeof(ElfHeader)) {
        return ElfLoadError::InvalidArgument;
    }
    ElfHeader header{};
    if (!copyFromAddressSpace(&header, sourceRoot, sourceAddress,
                              sizeof(header))) {
        return ElfLoadError::InvalidArgument;
    }
    const ElfLoadError headerError = validateHeader(header, size);
    if (headerError != ElfLoadError::None) {
        return headerError;
    }
    ProgramHeader headers[MAXIMUM_PROGRAM_HEADERS]{};
    const uint64_t headerBytes =
        static_cast<uint64_t>(header.programHeaderCount) * sizeof(ProgramHeader);
    if (!copyFromAddressSpace(headers, sourceRoot,
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
            if (getPhysicalAddressInAddressSpace(root, pageAddress) != 0) {
                result = {};
                return ElfLoadError::MappingFailed;
            }
        }
    }
    const ElfLoadError mappingError = mapSegmentsFromAddressSpace(
        root, sourceRoot, sourceAddress, headers,
        header.programHeaderCount);
    if (mappingError != ElfLoadError::None) {
        rollbackMappedSegments(root, headers, header.programHeaderCount);
        result = {};
        return mappingError;
    }
    result.entryPoint = header.entry;
    return ElfLoadError::None;
}

const char* getElfLoadErrorName(ElfLoadError error) {
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

bool runElfLoaderValidationSelfTest(Process& process) {
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
    if (ElfLoader::load(process, &image, sizeof(image), result) !=
        ElfLoadError::InvalidMagic) {
        return false;
    }

    image.header.identifier[0] = 0x7f;
    image.segments[0].flags =
        SEGMENT_FLAG_READABLE | SEGMENT_FLAG_WRITABLE | SEGMENT_FLAG_EXECUTABLE;
    if (ElfLoader::load(process, &image, sizeof(image), result) !=
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
    if (ElfLoader::load(process, &image, sizeof(image), result) !=
        ElfLoadError::OverlappingSegments) {
        return false;
    }

    image.header.programHeaderCount = 1;
    image.header.entry              = 0x600000;
    if (ElfLoader::load(process, &image, sizeof(image), result) !=
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
    if (ElfLoader::load(process, &image, sizeof(image), result) !=
        ElfLoadError::InvalidPermissions) {
        return false;
    }

    image.segments[1].flags = SEGMENT_FLAG_READABLE;
    if (ElfLoader::load(process, &image, sizeof(image), result) !=
            ElfLoadError::None ||
        result.entryPoint != 0x400000 || result.imageBase != 0x400000 ||
        result.imageEnd != 0x401000) {
        return false;
    }
    uint64_t flags = 0;
    return getPhysicalAddressInAddressSpace(
               process.addressSpace().rootPhysicalAddress(), 0x400000,
               &flags) != 0 &&
           (flags & pageUser) != 0 && (flags & pageWritable) == 0 &&
           (flags & pageNoExecute) == 0;
}

}  // namespace kernel
