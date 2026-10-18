#include "kernel/elf.h"
#include "kernel/page.h"
#include "kernel/services.h"

namespace {

constexpr uint8_t elfClass64             = 2;
constexpr uint8_t elfLittleEndian        = 1;
constexpr uint8_t elfVersion             = 1;
constexpr uint16_t elfTypeExecutable     = 2;
constexpr uint16_t elfTypeShared         = 3;
constexpr uint16_t elfMachineX86_64      = 62;
constexpr uint32_t programLoad           = 1;
constexpr uint32_t programDynamic        = 2;
constexpr uint32_t programInterpreter    = 3;
constexpr uint32_t segmentExecutable     = 1;
constexpr uint32_t segmentWritable       = 2;
constexpr uint32_t segmentReadable       = 4;
constexpr uint64_t pageSize              = 4096;
constexpr uint64_t minimumUserAddress    = 0x10000;
constexpr uint64_t maximumUserAddress    = 0x0000800000000000ULL;
constexpr uint64_t directMapBase         = 0xffff800000000000ULL;
constexpr uint32_t maximumImages         = 5;
constexpr uint16_t maximumProgramHeaders = 128;
constexpr uint64_t sharedImageBases[]    = {
    0x0000001000000000ULL, 0x0000001400000000ULL, 0x0000001800000000ULL,
    0x0000001c00000000ULL};

constexpr int64_t dynamicNull              = 0;
constexpr int64_t dynamicPltRelocationSize = 2;
constexpr int64_t dynamicHash              = 4;
constexpr int64_t dynamicStringTable       = 5;
constexpr int64_t dynamicSymbolTable       = 6;
constexpr int64_t dynamicRela              = 7;
constexpr int64_t dynamicRelaSize          = 8;
constexpr int64_t dynamicRelaEntrySize     = 9;
constexpr int64_t dynamicStringTableSize   = 10;
constexpr int64_t dynamicSymbolEntrySize   = 11;
constexpr int64_t dynamicPltRelocation     = 20;
constexpr int64_t dynamicJumpRelocation    = 23;
constexpr uint64_t dynamicRelaType         = 7;

constexpr uint32_t relocation64       = 1;
constexpr uint32_t relocationGlobDat  = 6;
constexpr uint32_t relocationJumpSlot = 7;
constexpr uint32_t relocationRelative = 8;
constexpr uint8_t symbolBindingWeak   = 2;
constexpr uint16_t sectionUndefined   = 0;
constexpr uint16_t sectionAbsolute    = 0xfff1;

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

struct [[gnu::packed]] DynamicEntry {
    int64_t tag;
    uint64_t value;
};

struct [[gnu::packed]] Symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t sectionIndex;
    uint64_t value;
    uint64_t size;
};

struct [[gnu::packed]] Rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
};

struct Image {
    uint64_t sourceAddress;
    uint64_t sourceSize;
    uint64_t bias;
    uint64_t imageBase;
    uint64_t imageEnd;
    uint64_t entryPoint;
    uint64_t dynamicAddress;
    uint64_t dynamicSize;
    uint64_t stringTable;
    uint64_t stringTableSize;
    uint64_t symbolTable;
    uint64_t symbolCount;
    uint64_t hashBuckets;
    uint64_t hashChains;
    uint32_t hashBucketCount;
    uint64_t rela;
    uint64_t relaSize;
    uint64_t jumpRela;
    uint64_t jumpRelaSize;
};

uint64_t alignDown(uint64_t value) { return value & ~(pageSize - 1); }

bool alignUp(uint64_t value, uint64_t& result) {
    if (value > UINT64_MAX - (pageSize - 1)) return false;
    result = (value + pageSize - 1) & ~(pageSize - 1);
    return true;
}

bool checkedRange(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool powerOfTwo(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool readSource(void* destination, uint64_t sourceRoot, uint64_t imageAddress,
                uint64_t imageSize, uint64_t offset, uint64_t size) {
    if (!checkedRange(offset, size, imageSize) || destination == nullptr ||
        imageAddress > UINT64_MAX - offset) {
        return false;
    }
    const uint64_t source = imageAddress + offset;
    if (sourceRoot == kernel::memory::getKernelAddressSpaceRoot() &&
        source >= directMapBase && size <= UINT64_MAX - source) {
        auto* output      = static_cast<uint8_t*>(destination);
        const auto* input = reinterpret_cast<const uint8_t*>(source);
        for (uint64_t index = 0; index < size; ++index) {
            output[index] = input[index];
        }
        return true;
    }
    return kernel::memory::copyFromAddressSpace(destination, sourceRoot, source,
                                                size);
}

kernel::ElfLoadError validateHeader(const ElfHeader& header, uint64_t imageSize,
                                    bool executable) {
    if (header.identifier[0] != 0x7f || header.identifier[1] != 'E' ||
        header.identifier[2] != 'L' || header.identifier[3] != 'F') {
        return kernel::ElfLoadError::InvalidMagic;
    }
    if (header.identifier[4] != elfClass64) {
        return kernel::ElfLoadError::UnsupportedClass;
    }
    if (header.identifier[5] != elfLittleEndian) {
        return kernel::ElfLoadError::UnsupportedEndian;
    }
    if (header.identifier[6] != elfVersion || header.version != elfVersion) {
        return kernel::ElfLoadError::UnsupportedVersion;
    }
    if ((executable && header.type != elfTypeExecutable &&
         header.type != elfTypeShared) ||
        (!executable && header.type != elfTypeShared)) {
        return kernel::ElfLoadError::UnsupportedType;
    }
    if (header.machine != elfMachineX86_64) {
        return kernel::ElfLoadError::UnsupportedMachine;
    }
    if (header.headerSize != sizeof(ElfHeader) ||
        header.programHeaderSize != sizeof(ProgramHeader) ||
        header.programHeaderCount == 0 ||
        header.programHeaderCount > maximumProgramHeaders ||
        !checkedRange(header.programHeaderOffset,
                      static_cast<uint64_t>(header.programHeaderCount) *
                          sizeof(ProgramHeader),
                      imageSize)) {
        return kernel::ElfLoadError::InvalidHeader;
    }
    return kernel::ElfLoadError::None;
}

void permissionsForPage(const ProgramHeader* headers, uint16_t count,
                        uint64_t page, bool& writable, bool& executable) {
    writable               = false;
    executable             = false;
    const uint64_t pageEnd = page + pageSize;
    for (uint16_t index = 0; index < count; ++index) {
        const ProgramHeader& header = headers[index];
        if (header.type != programLoad || header.memorySize == 0) continue;
        const uint64_t end = header.virtualAddress + header.memorySize;
        if (page < end && header.virtualAddress < pageEnd) {
            writable |= (header.flags & segmentWritable) != 0;
            executable |= (header.flags & segmentExecutable) != 0;
        }
    }
}

kernel::ElfLoadError loadImage(uint64_t destinationRoot, uint64_t sourceRoot,
                               uint64_t preferredBase, bool executable,
                               Image& image) {
    ElfHeader header{};
    if (!readSource(&header, sourceRoot, image.sourceAddress, image.sourceSize,
                    0, sizeof(header))) {
        return kernel::ElfLoadError::InvalidArgument;
    }
    kernel::ElfLoadError result =
        validateHeader(header, image.sourceSize, executable);
    if (result != kernel::ElfLoadError::None) return result;
    ProgramHeader headers[maximumProgramHeaders]{};
    if (!readSource(headers, sourceRoot, image.sourceAddress, image.sourceSize,
                    header.programHeaderOffset,
                    static_cast<uint64_t>(header.programHeaderCount) *
                        sizeof(ProgramHeader))) {
        return kernel::ElfLoadError::InvalidProgramHeader;
    }

    uint64_t minimum = UINT64_MAX;
    uint64_t maximum = 0;
    bool hasLoad     = false;
    for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (segment.type == programInterpreter) {
            return kernel::ElfLoadError::UnsupportedProgramHeader;
        }
        if (segment.type == programDynamic) {
            image.dynamicAddress = segment.virtualAddress;
            image.dynamicSize    = segment.memorySize;
        }
        if (segment.type != programLoad) continue;
        if (segment.fileSize > segment.memorySize ||
            !checkedRange(segment.offset, segment.fileSize, image.sourceSize) ||
            (segment.flags &
             ~(segmentReadable | segmentWritable | segmentExecutable)) != 0 ||
            ((segment.flags & segmentWritable) != 0 &&
             (segment.flags & segmentExecutable) != 0) ||
            (segment.alignment > 1 &&
             (!powerOfTwo(segment.alignment) ||
              (segment.virtualAddress & (segment.alignment - 1)) !=
                  (segment.offset & (segment.alignment - 1)))) ||
            segment.virtualAddress + segment.memorySize <
                segment.virtualAddress) {
            return kernel::ElfLoadError::InvalidSegment;
        }
        if (segment.memorySize == 0) continue;
        hasLoad = true;
        if (alignDown(segment.virtualAddress) < minimum) {
            minimum = alignDown(segment.virtualAddress);
        }
        uint64_t end = 0;
        if (!alignUp(segment.virtualAddress + segment.memorySize, end)) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        if (end > maximum) maximum = end;
        for (uint16_t previous = 0; previous < index; ++previous) {
            const ProgramHeader& other = headers[previous];
            if (other.type != programLoad || other.memorySize == 0) continue;
            const uint64_t leftEnd =
                segment.virtualAddress + segment.memorySize;
            const uint64_t rightEnd = other.virtualAddress + other.memorySize;
            if (segment.virtualAddress < rightEnd &&
                other.virtualAddress < leftEnd) {
                return kernel::ElfLoadError::OverlappingSegments;
            }
        }
    }
    if (!hasLoad || minimum == UINT64_MAX || maximum <= minimum) {
        return kernel::ElfLoadError::InvalidProgramHeader;
    }
    image.bias = header.type == elfTypeShared ? preferredBase - minimum : 0;
    image.imageBase = image.bias + minimum;
    image.imageEnd  = image.bias + maximum;
    if (image.imageBase < minimumUserAddress ||
        image.imageEnd <= image.imageBase ||
        image.imageEnd > maximumUserAddress ||
        header.entry > maximumUserAddress - image.bias) {
        return kernel::ElfLoadError::InvalidAddress;
    }
    image.entryPoint = image.bias + header.entry;
    if (image.dynamicAddress != 0) image.dynamicAddress += image.bias;

    for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
        const ProgramHeader& segment = headers[index];
        if (segment.type != programLoad || segment.memorySize == 0) continue;
        uint64_t pageEnd = 0;
        alignUp(segment.virtualAddress + segment.memorySize, pageEnd);
        for (uint64_t page = alignDown(segment.virtualAddress); page < pageEnd;
             page += pageSize) {
            const uint64_t address = image.bias + page;
            if (kernel::memory::getPhysicalAddressInAddressSpace(
                    destinationRoot, address) != 0) {
                continue;
            }
            bool writable       = false;
            bool executablePage = false;
            permissionsForPage(headers, header.programHeaderCount, page,
                               writable, executablePage);
            if (writable && executablePage) {
                return kernel::ElfLoadError::InvalidPermissions;
            }
            uint64_t flags = kernel::pageUser;
            if (writable) flags |= kernel::pageWritable;
            if (!executablePage) flags |= kernel::pageNoExecute;
            const kernel::UserPageMapResult mapped =
                kernel::memory::allocateUserPageInAddressSpace(destinationRoot,
                                                               address, flags);
            if (mapped == kernel::UserPageMapResult::OutOfMemory) {
                return kernel::ElfLoadError::OutOfMemory;
            }
            if (mapped != kernel::UserPageMapResult::Success) {
                return kernel::ElfLoadError::MappingFailed;
            }
        }
    }
    uint8_t buffer[pageSize];
    for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
        const ProgramHeader& segment = headers[index];
        uint64_t copied              = 0;
        while (segment.type == programLoad && copied < segment.fileSize) {
            const uint64_t remaining = segment.fileSize - copied;
            const uint64_t chunk = remaining < pageSize ? remaining : pageSize;
            if (!readSource(buffer, sourceRoot, image.sourceAddress,
                            image.sourceSize, segment.offset + copied, chunk) ||
                !kernel::memory::copyToAddressSpace(
                    destinationRoot,
                    image.bias + segment.virtualAddress + copied, buffer,
                    chunk)) {
                return kernel::ElfLoadError::MappingFailed;
            }
            copied += chunk;
        }
    }
    if (executable && (image.entryPoint < image.imageBase ||
                       image.entryPoint >= image.imageEnd)) {
        return kernel::ElfLoadError::InvalidEntryPoint;
    }
    return kernel::ElfLoadError::None;
}

bool dynamicPointer(uint64_t root, const Image& image, uint64_t value,
                    uint64_t size, uint64_t& result) {
    if (value > maximumUserAddress - image.bias) return false;
    result = image.bias + value;
    return kernel::memory::validateUserMemory(root, result, size, false);
}

kernel::ElfLoadError readDynamicInfo(uint64_t root, Image& image) {
    if (image.dynamicAddress == 0 || image.dynamicSize < sizeof(DynamicEntry) ||
        image.dynamicSize % sizeof(DynamicEntry) != 0 ||
        !kernel::memory::validateUserMemory(root, image.dynamicAddress,
                                            image.dynamicSize, false)) {
        return kernel::ElfLoadError::UnsupportedProgramHeader;
    }
    uint64_t hash              = 0;
    uint64_t relaEntrySize     = sizeof(Rela);
    uint64_t symbolEntrySize   = sizeof(Symbol);
    uint64_t pltRelocationType = dynamicRelaType;
    const uint64_t entries     = image.dynamicSize / sizeof(DynamicEntry);
    for (uint64_t index = 0; index < entries; ++index) {
        DynamicEntry entry{};
        if (!kernel::memory::copyFromAddressSpace(
                &entry, root,
                image.dynamicAddress + index * sizeof(DynamicEntry),
                sizeof(entry))) {
            return kernel::ElfLoadError::MappingFailed;
        }
        if (entry.tag == dynamicNull) break;
        uint64_t pointer = 0;
        switch (entry.tag) {
            case dynamicHash:
                if (!dynamicPointer(root, image, entry.value, 8, pointer)) {
                    return kernel::ElfLoadError::InvalidAddress;
                }
                hash = pointer;
                break;
            case dynamicStringTable:
                if (!dynamicPointer(root, image, entry.value, 1, pointer)) {
                    return kernel::ElfLoadError::InvalidAddress;
                }
                image.stringTable = pointer;
                break;
            case dynamicStringTableSize:
                image.stringTableSize = entry.value;
                break;
            case dynamicSymbolTable:
                if (!dynamicPointer(root, image, entry.value, sizeof(Symbol),
                                    pointer)) {
                    return kernel::ElfLoadError::InvalidAddress;
                }
                image.symbolTable = pointer;
                break;
            case dynamicSymbolEntrySize:
                symbolEntrySize = entry.value;
                break;
            case dynamicRela:
                if (!dynamicPointer(root, image, entry.value, sizeof(Rela),
                                    pointer)) {
                    return kernel::ElfLoadError::InvalidAddress;
                }
                image.rela = pointer;
                break;
            case dynamicRelaSize:
                image.relaSize = entry.value;
                break;
            case dynamicRelaEntrySize:
                relaEntrySize = entry.value;
                break;
            case dynamicJumpRelocation:
                if (!dynamicPointer(root, image, entry.value, sizeof(Rela),
                                    pointer)) {
                    return kernel::ElfLoadError::InvalidAddress;
                }
                image.jumpRela = pointer;
                break;
            case dynamicPltRelocationSize:
                image.jumpRelaSize = entry.value;
                break;
            case dynamicPltRelocation:
                pltRelocationType = entry.value;
                break;
            default:
                break;
        }
    }
    if (symbolEntrySize != sizeof(Symbol) || relaEntrySize != sizeof(Rela) ||
        pltRelocationType != dynamicRelaType || image.stringTable == 0 ||
        image.stringTableSize == 0 || image.symbolTable == 0 || hash == 0 ||
        image.relaSize % sizeof(Rela) != 0 ||
        image.jumpRelaSize % sizeof(Rela) != 0 ||
        !kernel::memory::validateUserMemory(root, image.stringTable,
                                            image.stringTableSize, false) ||
        (image.relaSize != 0 && !kernel::memory::validateUserMemory(
                                    root, image.rela, image.relaSize, false)) ||
        (image.jumpRelaSize != 0 &&
         !kernel::memory::validateUserMemory(root, image.jumpRela,
                                             image.jumpRelaSize, false))) {
        return kernel::ElfLoadError::UnsupportedProgramHeader;
    }
    uint32_t hashHeader[2]{};
    if (!kernel::memory::copyFromAddressSpace(hashHeader, root, hash,
                                              sizeof(hashHeader)) ||
        hashHeader[0] == 0 || hashHeader[0] > 65536 ||
        hashHeader[1] == 0 || hashHeader[1] > 65536 ||
        !kernel::memory::validateUserMemory(root, hash,
            (2ULL + hashHeader[0] + hashHeader[1]) * sizeof(uint32_t), false)) {
        return kernel::ElfLoadError::InvalidHeader;
    }
    image.symbolCount = hashHeader[1];
    image.hashBucketCount = hashHeader[0];
    image.hashBuckets = hash + 2 * sizeof(uint32_t);
    image.hashChains = image.hashBuckets + hashHeader[0] * sizeof(uint32_t);
    if (!kernel::memory::validateUserMemory(root, image.symbolTable,
                                            image.symbolCount * sizeof(Symbol),
                                            false)) {
        return kernel::ElfLoadError::InvalidAddress;
    }
    return kernel::ElfLoadError::None;
}

bool readSymbol(uint64_t root, const Image& image, uint32_t index,
                Symbol& symbol) {
    return index < image.symbolCount &&
           kernel::memory::copyFromAddressSpace(
               &symbol, root, image.symbolTable + index * sizeof(Symbol),
               sizeof(symbol));
}

bool symbolNamesEqual(uint64_t root, const Image& left,
                      const Symbol& leftSymbol, const Image& right,
                      const Symbol& rightSymbol) {
    if (leftSymbol.name >= left.stringTableSize ||
        rightSymbol.name >= right.stringTableSize) {
        return false;
    }
    uint64_t remaining = left.stringTableSize - leftSymbol.name;
    if (remaining > right.stringTableSize - rightSymbol.name)
        remaining = right.stringTableSize - rightSymbol.name;
    for (uint64_t offset = 0; offset < remaining;) {
        char a[64], b[64];
        const uint64_t count = remaining - offset < sizeof(a) ? remaining - offset : sizeof(a);
        if (!kernel::memory::copyFromAddressSpace(a, root,
                left.stringTable + leftSymbol.name + offset, count) ||
            !kernel::memory::copyFromAddressSpace(b, root,
                right.stringTable + rightSymbol.name + offset, count)) return false;
        for (uint64_t i = 0; i < count; ++i) {
            if (a[i] != b[i]) return false;
            if (a[i] == 0) return true;
        }
        offset += count;
    }
    return false;
}

bool symbolHash(uint64_t root, const Image& image, const Symbol& symbol,
                 uint32_t& hash) {
    if (symbol.name >= image.stringTableSize) return false;
    hash = 0;
    const uint64_t remaining = image.stringTableSize - symbol.name;
    for (uint64_t offset = 0; offset < remaining;) {
        uint8_t bytes[64];
        const uint64_t count = remaining - offset < sizeof(bytes) ? remaining - offset : sizeof(bytes);
        if (!kernel::memory::copyFromAddressSpace(bytes, root,
                image.stringTable + symbol.name + offset, count)) return false;
        for (uint64_t i = 0; i < count; ++i) {
            if (bytes[i] == 0) return true;
            hash = (hash << 4) + bytes[i];
            const uint32_t high = hash & 0xf0000000U;
            hash ^= high >> 24;
            hash &= ~high;
        }
        offset += count;
    }
    return false;
}

bool resolveSymbol(uint64_t root, const Image* images, uint32_t imageCount,
                   const Image& requester, uint32_t symbolIndex,
                   uint64_t& value, bool trustedImports) {
    if (symbolIndex == 0) {
        value = 0;
        return true;
    }
    Symbol requested{};
    if (!readSymbol(root, requester, symbolIndex, requested)) return false;
    if (trustedImports) {
        char name[128]{};
        uint32_t i = 0;
        for (; i + 1 < sizeof(name) && requested.name + i < requester.stringTableSize; ++i) {
            if (!kernel::memory::copyFromAddressSpace(&name[i], root,
                    requester.stringTable + requested.name + i, 1)) return false;
            if (!name[i]) break;
        }
        if (i + 1 < sizeof(name) && !name[i]) {
            value = kernel::Services::resolve(name);
            if (value) return true;
        }
    }
    uint32_t hash;
    if (!symbolHash(root, requester, requested, hash)) return false;
    for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
        const Image& image = images[imageIndex];
        uint32_t index = 0;
        if (!kernel::memory::copyFromAddressSpace(&index, root,
                image.hashBuckets + (hash % image.hashBucketCount) * sizeof(uint32_t),
                sizeof(index))) return false;
        uint64_t visited = 0;
        while (index != 0) {
            if (++visited > image.symbolCount) return false;
            Symbol candidate{};
            if (!readSymbol(root, image, index, candidate)) return false;
            const uint8_t binding = candidate.info >> 4;
            if (candidate.sectionIndex != sectionUndefined &&
                (binding == 1 || binding == symbolBindingWeak) &&
                symbolNamesEqual(root, requester, requested, image, candidate)) {
                value = candidate.sectionIndex == sectionAbsolute
                            ? candidate.value : image.bias + candidate.value;
                return true;
            }
            if (!kernel::memory::copyFromAddressSpace(&index, root,
                    image.hashChains + index * sizeof(uint32_t), sizeof(index))) return false;
        }
    }
    if ((requested.info >> 4) == symbolBindingWeak) {
        value = 0;
        return true;
    }
    return false;
}

kernel::ElfLoadError applyRelocationTable(uint64_t root, const Image* images,
                                          uint32_t imageCount,
                                          const Image& image, uint64_t table,
                                          uint64_t size, bool trustedImports) {
    for (uint64_t offset = 0; offset < size; offset += sizeof(Rela)) {
        Rela relocation{};
        if (!kernel::memory::copyFromAddressSpace(
                &relocation, root, table + offset, sizeof(relocation)) ||
            relocation.offset > maximumUserAddress - image.bias) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        const uint32_t type   = static_cast<uint32_t>(relocation.info);
        const uint32_t symbol = static_cast<uint32_t>(relocation.info >> 32);
        const uint64_t target = image.bias + relocation.offset;
        if (!kernel::memory::validateUserMemory(root, target, sizeof(uint64_t),
                                                false)) {
            return kernel::ElfLoadError::InvalidAddress;
        }
        uint64_t value = 0;
        if (type == relocationRelative) {
            value = image.bias + static_cast<uint64_t>(relocation.addend);
        } else if (type == relocation64 || type == relocationGlobDat ||
                   type == relocationJumpSlot) {
            uint64_t symbolValue = 0;
            if (!resolveSymbol(root, images, imageCount, image, symbol,
                               symbolValue, trustedImports)) {
                return kernel::ElfLoadError::UndefinedSymbol;
            }
            value = type == relocation64
                        ? symbolValue + static_cast<uint64_t>(relocation.addend)
                        : symbolValue;
        } else {
            return kernel::ElfLoadError::UnsupportedProgramHeader;
        }
        if (!kernel::memory::copyToAddressSpace(root, target, &value,
                                                sizeof(value))) {
            return kernel::ElfLoadError::MappingFailed;
        }
    }
    return kernel::ElfLoadError::None;
}

}  // namespace

namespace kernel {

ElfLoadError ElfLoader::loadDynamicSetFromAddressSpace(
    uint64_t destinationRoot, uint64_t sourceRoot, uint64_t executableAddress,
    size_t executableSize, const ElfSharedImage* sharedImages,
    uint32_t sharedImageCount, ElfLoadResult& result, bool trustedImports) {
    result = {};
    if (destinationRoot == 0 || sourceRoot == 0 || executableAddress == 0 ||
        executableSize == 0 || sharedImageCount == 0 ||
        sharedImageCount >= maximumImages || sharedImages == nullptr) {
        return ElfLoadError::InvalidArgument;
    }
    Image images[maximumImages]{};
    images[0].sourceAddress = executableAddress;
    images[0].sourceSize    = executableSize;
    ElfLoadError error =
        loadImage(destinationRoot, sourceRoot, 0x400000, true, images[0]);
    if (error != ElfLoadError::None) return error;
    for (uint32_t index = 0; index < sharedImageCount; ++index) {
        images[index + 1].sourceAddress = sharedImages[index].sourceAddress;
        images[index + 1].sourceSize    = sharedImages[index].size;
        error = loadImage(destinationRoot, sourceRoot, sharedImageBases[index],
                          false, images[index + 1]);
        if (error != ElfLoadError::None) return error;
    }
    const uint32_t imageCount = sharedImageCount + 1;
    for (uint32_t index = 0; index < imageCount; ++index) {
        error = readDynamicInfo(destinationRoot, images[index]);
        if (error != ElfLoadError::None) return error;
    }
    for (uint32_t index = 0; index < imageCount; ++index) {
        error = applyRelocationTable(destinationRoot, images, imageCount,
                                     images[index], images[index].rela,
                                     images[index].relaSize, trustedImports);
        if (error != ElfLoadError::None) return error;
        error = applyRelocationTable(destinationRoot, images, imageCount,
                                     images[index], images[index].jumpRela,
                                     images[index].jumpRelaSize, trustedImports);
        if (error != ElfLoadError::None) return error;
    }
    result.entryPoint = images[0].entryPoint;
    result.imageBase  = images[0].imageBase;
    result.imageEnd   = images[0].imageEnd;
    return ElfLoadError::None;
}

}  // namespace kernel
