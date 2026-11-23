/*
 * 加载程序、共享映像或解释器，并执行内核支持的动态重定位。
 */

#include "kernel/elf.h"
#include "kernel/page.h"
#include "kernel/syscall.h"
#include "kernel/task.h"

namespace elf {

    constexpr uint8_t elfClass64                 = 2;
    constexpr uint8_t elfLittleEndian            = 1;
    constexpr uint8_t elfVersion                 = 1;
    constexpr uint16_t elfTypeExecutable         = 2;
    constexpr uint16_t elfTypeShared             = 3;
    constexpr uint16_t elfMachineX86_64          = 62;
    constexpr uint32_t programLoad               = 1;
    constexpr uint32_t programDynamic            = 2;
    constexpr uint32_t programInterpreter        = 3;
    constexpr uint32_t segmentExecutable         = 1;
    constexpr uint32_t segmentWritable           = 2;
    constexpr uint32_t segmentReadable           = 4;
    constexpr uint64_t pageSize                  = 4096;
    constexpr uint64_t minimumUserAddress        = 0x10000;
    constexpr uint64_t maximumUserAddress        = 0x0000800000000000ULL;
    constexpr uint64_t directMapBase             = 0xffff800000000000ULL;
    constexpr uint32_t maximumImages             = 5;
    constexpr uint16_t maximumProgramHeaders     = 128;
    static constexpr uint64_t sharedImageBases[] = {
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

    /* 按 ELF64 文件布局读取的文件头，给出架构、入口与程序头数组位置。 */
    struct [[gnu::packed]] DynamicElfHeader {
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

    /* 描述一个程序段的文件范围、虚拟地址、内存长度和权限。 */
    struct [[gnu::packed]] DynamicProgramHeader {
            uint32_t type;
            uint32_t flags;
            uint64_t offset;
            uint64_t virtualAddress;
            uint64_t physicalAddress;
            uint64_t fileSize;
            uint64_t memorySize;
            uint64_t alignment;
    };

    /* 动态段的一项标签和值，指出符号、字符串或重定位表的信息。 */
    struct [[gnu::packed]] DynamicEntry {
            int64_t tag;
            uint64_t value;
    };

    /* 符号表中的一条记录，保存名称偏移、绑定方式、定义位置和值。 */
    struct [[gnu::packed]] Symbol {
            uint32_t name;
            uint8_t info;
            uint8_t other;
            uint16_t sectionIndex;
            uint64_t value;
            uint64_t size;
    };

    /* 带显式附加值的重定位记录，指定需要修改的位置、类型与符号。 */
    struct [[gnu::packed]] Rela {
            uint64_t offset;
            uint64_t info;
            int64_t addend;
    };

    /* 一份已加载映像的地址范围、装载偏移和动态表位置，供跨映像查符号使用。 */
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

    /*
     * 把虚拟地址向下对齐到页面边界。
     */
    static uint64_t alignDown(uint64_t value) {
        return value & ~(pageSize - 1);
    }

    /*
     * 检查向上取整是否溢出，成功时返回覆盖原值的整页边界。
     */
    static bool alignUp(uint64_t value, uint64_t& result) {
        if (value > UINT64_MAX - (pageSize - 1)) {
            return false;
        }

        result = (value + pageSize - 1) & ~(pageSize - 1);

        return true;
    }

    /*
     * 用减法检查文件偏移与长度是否完全位于映像内，避免偏移加长度溢出。
     */
    static bool checkedRange(uint64_t offset, uint64_t size, uint64_t limit) {
        return offset <= limit && size <= limit - offset;
    }

    /*
     * 判断非零数是否只有一个置位，用于校验 ELF 段的对齐要求。
     */
    static bool powerOfTwo(uint64_t value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    /*
     * 从指定源地址空间读取有界映像片段；源在内核直接映射中时直接复制，其他情况使用跨页表读取接口。
     */
    static bool readSource(void* destination, uint64_t sourceRoot,
                           uint64_t imageAddress, uint64_t imageSize,
                           uint64_t offset, uint64_t size) {
        if (!checkedRange(offset, size, imageSize) || destination == nullptr ||
            imageAddress > UINT64_MAX - offset) {
            return false;
        }

        // 源地址属于
        // sourceRoot，不能一律强转指针读取；只有内核直接映射范围可以直接逐字节复制。
        const uint64_t source = imageAddress + offset;

        if (sourceRoot == mm::getKernelAddressSpaceRoot() &&
            source >= directMapBase && size <= UINT64_MAX - source) {
            uint8_t* output      = static_cast<uint8_t*>(destination);
            const uint8_t* input = reinterpret_cast<const uint8_t*>(source);

            for (uint64_t index = 0; index < size; ++index) {
                output[index] = input[index];
            }

            return true;
        }

        // 普通源地址必须通过页表翻译复制，即使当前 CPU
        // 使用的不是源页表也能安全读取。
        return mm::copyFromAddressSpace(destination, sourceRoot, source, size);
    }

    /*
     * 检查 ELF
     * 标识、架构、类型以及程序头数组的大小与范围，区分主程序和共享映像允许的文件类型。
     */
    static ElfLoadError validateHeader(const DynamicElfHeader& header,
                                       uint64_t imageSize, bool executable) {
        // 先识别文件魔数，再核对位数、字节序、版本和机器架构，避免把不兼容布局当成本机结构。
        if (header.identifier[0] != 0x7f || header.identifier[1] != 'E' ||
            header.identifier[2] != 'L' || header.identifier[3] != 'F') {
            return ElfLoadError::InvalidMagic;
        }

        if (header.identifier[4] != elfClass64) {
            return ElfLoadError::UnsupportedClass;
        }

        if (header.identifier[5] != elfLittleEndian) {
            return ElfLoadError::UnsupportedEndian;
        }

        if (header.identifier[6] != elfVersion ||
            header.version != elfVersion) {
            return ElfLoadError::UnsupportedVersion;
        }

        if ((executable && header.type != elfTypeExecutable &&
             header.type != elfTypeShared) ||
            (!executable && header.type != elfTypeShared)) {
            return ElfLoadError::UnsupportedType;
        }

        if (header.machine != elfMachineX86_64) {
            return ElfLoadError::UnsupportedMachine;
        }

        // 程序头数组来自文件，既要符合结构大小，也要完整落在文件内，并受固定数组容量限制。
        if (header.headerSize != sizeof(DynamicElfHeader) ||
            header.programHeaderSize != sizeof(DynamicProgramHeader) ||
            header.programHeaderCount == 0 ||
            header.programHeaderCount > maximumProgramHeaders ||
            !checkedRange(header.programHeaderOffset,
                          static_cast<uint64_t>(header.programHeaderCount) *
                              sizeof(DynamicProgramHeader),
                          imageSize)) {
            return ElfLoadError::InvalidHeader;
        }

        return ElfLoadError::None;
    }

    /*
     * 合并落在同一页上的所有可加载段权限，页粒度权限必须满足整页内容而不是只看某个段。
     */
    static void permissionsForPage(const DynamicProgramHeader* headers,
                                   uint16_t count, uint64_t page,
                                   bool& writable, bool& executable) {
        writable               = false;
        executable             = false;
        const uint64_t pageEnd = page + pageSize;

        // 硬件权限按页生效；两个不重叠的段仍可能共享一页，因此要收集所有覆盖此页的段权限。
        for (uint16_t index = 0; index < count; ++index) {
            const DynamicProgramHeader& header = headers[index];

            if (header.type != programLoad || header.memorySize == 0) {
                continue;
            }

            const uint64_t end = header.virtualAddress + header.memorySize;

            if (page < end && header.virtualAddress < pageEnd) {
                writable |= (header.flags & segmentWritable) != 0;
                executable |= (header.flags & segmentExecutable) != 0;
            }
        }
    }

    /*
     * 验证并装入一个程序或共享映像。
     * 先检查段范围、重叠与权限，再建立清零页面并复制文件数据，记录装载偏移；失败交给上层销毁未提交地址空间。
     */
    static ElfLoadError loadImage(uint64_t destinationRoot, uint64_t sourceRoot,
                                  uint64_t preferredBase, bool executable,
                                  Image& image, bool allowInterpreter = false) {
        // 先把头和程序头复制到内核局部存储，再解释文件内容；尚不修改目标地址空间。
        DynamicElfHeader header{};

        if (!readSource(&header, sourceRoot, image.sourceAddress,
                        image.sourceSize, 0, sizeof(header))) {
            return ElfLoadError::InvalidArgument;
        }

        ElfLoadError result =
            validateHeader(header, image.sourceSize, executable);

        if (result != ElfLoadError::None) {
            return result;
        }

        DynamicProgramHeader headers[maximumProgramHeaders]{};

        if (!readSource(headers, sourceRoot, image.sourceAddress,
                        image.sourceSize, header.programHeaderOffset,
                        static_cast<uint64_t>(header.programHeaderCount) *
                            sizeof(DynamicProgramHeader))) {
            return ElfLoadError::InvalidProgramHeader;
        }

        // 首轮只验证段布局，并统计整个映像的页边界与动态段位置，为选择装载偏移作准备。
        uint64_t minimum = UINT64_MAX;
        uint64_t maximum = 0;
        bool hasLoad     = false;

        for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
            const DynamicProgramHeader& segment = headers[index];

            if (segment.type == programInterpreter && !allowInterpreter) {
                return ElfLoadError::UnsupportedProgramHeader;
            }

            if (segment.type == programDynamic) {
                image.dynamicAddress = segment.virtualAddress;
                image.dynamicSize    = segment.memorySize;
            }

            if (segment.type != programLoad) {
                continue;
            }

            // 文件数据不能大于内存段；同时验证范围、权限、对齐及地址加法，拒绝单段同时可写可执行。
            if (segment.fileSize > segment.memorySize ||
                !checkedRange(segment.offset, segment.fileSize,
                              image.sourceSize) ||
                (segment.flags & ~(segmentReadable | segmentWritable |
                                   segmentExecutable)) != 0 ||
                ((segment.flags & segmentWritable) != 0 &&
                 (segment.flags & segmentExecutable) != 0) ||
                (segment.alignment > 1 &&
                 (!powerOfTwo(segment.alignment) ||
                  (segment.virtualAddress & (segment.alignment - 1)) !=
                      (segment.offset & (segment.alignment - 1)))) ||
                segment.virtualAddress + segment.memorySize <
                    segment.virtualAddress) {
                return ElfLoadError::InvalidSegment;
            }

            if (segment.memorySize == 0) {
                continue;
            }

            hasLoad = true;

            if (alignDown(segment.virtualAddress) < minimum) {
                minimum = alignDown(segment.virtualAddress);
            }

            uint64_t end = 0;

            if (!alignUp(segment.virtualAddress + segment.memorySize, end)) {
                return ElfLoadError::InvalidAddress;
            }

            if (end > maximum) {
                maximum = end;
            }

            // 比较段的实际字节范围，拒绝内容重叠；仅共享页边界的段留到后面合并页权限。
            for (uint16_t previous = 0; previous < index; ++previous) {
                const DynamicProgramHeader& other = headers[previous];

                if (other.type != programLoad || other.memorySize == 0) {
                    continue;
                }

                const uint64_t leftEnd =
                    segment.virtualAddress + segment.memorySize;
                const uint64_t rightEnd =
                    other.virtualAddress + other.memorySize;

                if (segment.virtualAddress < rightEnd &&
                    other.virtualAddress < leftEnd) {
                    return ElfLoadError::OverlappingSegments;
                }
            }
        }

        if (!hasLoad || minimum == UINT64_MAX || maximum <= minimum) {
            return ElfLoadError::InvalidProgramHeader;
        }

        // 共享映像按首个页边界平移到首选基址；固定地址可执行文件保持原地址，再检查最终范围是否在用户空间内。
        image.bias = header.type == elfTypeShared ? preferredBase - minimum : 0;
        image.imageBase = image.bias + minimum;
        image.imageEnd  = image.bias + maximum;

        if (image.imageBase < minimumUserAddress ||
            image.imageEnd <= image.imageBase ||
            image.imageEnd > maximumUserAddress ||
            header.entry > maximumUserAddress - image.bias) {
            return ElfLoadError::InvalidAddress;
        }

        // 文件里的入口和动态段地址是装载前的数值，统一加上偏移，后面的访问才指向真正映射位置。
        image.entryPoint = image.bias + header.entry;

        if (image.dynamicAddress != 0) {
            image.dynamicAddress += image.bias;
        }

        for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
            const DynamicProgramHeader& segment = headers[index];

            if (segment.type != programLoad || segment.memorySize == 0) {
                continue;
            }

            uint64_t pageEnd = 0;
            alignUp(segment.virtualAddress + segment.memorySize, pageEnd);

            for (uint64_t page = alignDown(segment.virtualAddress);
                 page < pageEnd; page += pageSize) {
                // 同页可能被多个段覆盖，只分配一次；第一次分配时就按所有相关段计算最终权限。
                const uint64_t address = image.bias + page;

                if (mm::getPhysicalAddressInAddressSpace(destinationRoot,
                                                         address) != 0) {
                    continue;
                }

                bool writable       = false;
                bool executablePage = false;
                permissionsForPage(headers, header.programHeaderCount, page,
                                   writable, executablePage);

                // 即使各段分别合法，合并到同一页后仍可能同时可写可执行，这里再次拒绝。
                if (writable && executablePage) {
                    return ElfLoadError::InvalidPermissions;
                }

                uint64_t flags = mm::pageUser;

                if (writable) {
                    flags |= mm::pageWritable;
                }

                if (!executablePage) {
                    flags |= mm::pageNoExecute;
                }

                const mm::UserPageMapResult mapped =
                    mm::allocateUserPageInAddressSpace(destinationRoot, address,
                                                       flags);

                if (mapped == mm::UserPageMapResult::OutOfMemory) {
                    return ElfLoadError::OutOfMemory;
                }

                if (mapped != mm::UserPageMapResult::Success) {
                    return ElfLoadError::MappingFailed;
                }
            }
        }

        // 页面分配时已清零，再分块复制文件提供的字节；文件末尾以外的内存部分自然保持为零。
        uint8_t buffer[pageSize];

        for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
            const DynamicProgramHeader& segment = headers[index];
            uint64_t copied                     = 0;

            while (segment.type == programLoad && copied < segment.fileSize) {
                const uint64_t remaining = segment.fileSize - copied;
                const uint64_t chunk =
                    remaining < pageSize ? remaining : pageSize;

                if (!readSource(buffer, sourceRoot, image.sourceAddress,
                                image.sourceSize, segment.offset + copied,
                                chunk) ||
                    !mm::copyToAddressSpace(
                        destinationRoot,
                        image.bias + segment.virtualAddress + copied, buffer,
                        chunk)) {
                    return ElfLoadError::MappingFailed;
                }

                copied += chunk;
            }
        }

        // 映射后核对入口至少位于映像范围内；任一步失败由上层销毁尚未提交的新任务地址空间。
        if (executable && (image.entryPoint < image.imageBase ||
                           image.entryPoint >= image.imageEnd)) {
            return ElfLoadError::InvalidEntryPoint;
        }

        return ElfLoadError::None;
    }

    /*
     * 分别装入主程序和指定解释器，设置真正初始入口以及主程序入口、程序头和解释器基址等辅助信息。
     */
    static ElfLoadError loadInterpreter(uint64_t root, uint64_t sourceRoot,
                                        Image& application,
                                        const DynamicElfHeader& header,
                                        const ElfSharedImage& interpreter,
                                        ElfLoadResult& result) {
        ElfLoadError error =
            loadImage(root, sourceRoot, 0x400000, true, application, true);

        if (error != ElfLoadError::None) {
            return error;
        }

        // 主程序和解释器分别映射，解释器采用独立高地址区间，避免占用主程序的默认位置。
        Image loader{};
        loader.sourceAddress = interpreter.getSourceAddress();
        loader.sourceSize    = interpreter.getSize();
        error =
            loadImage(root, sourceRoot, 0x0000070000000000ULL, true, loader);

        if (error != ElfLoadError::None) {
            return error;
        }

        // 解释器必须自包含，才能在尚未装载其他库时独立开始执行。
        if (loader.dynamicSize) {
            return ElfLoadError::UnsupportedProgramHeader;
        }

        // 为主程序头表另建只读且不可执行的用户页，解释器通过辅助向量读取这些加载信息。
        constexpr uint64_t metadata = 0x20000;
        const uint64_t bytes =
            sizeof(DynamicElfHeader) +
            header.programHeaderCount * sizeof(DynamicProgramHeader);

        for (uint64_t offset = 0; offset < bytes; offset += pageSize) {
            if (mm::allocateUserPageInAddressSpace(
                    root, metadata + offset,
                    ::mm::pageUser | ::mm::pageNoExecute) !=
                ::mm::UserPageMapResult::Success) {
                return ElfLoadError::MappingFailed;
            }
        }

        if (!mm::copyToAddressSpace(root, metadata, &header, sizeof(header))) {
            return ElfLoadError::MappingFailed;
        }

        DynamicProgramHeader headers[maximumProgramHeaders]{};

        if (!readSource(headers, sourceRoot, application.sourceAddress,
                        application.sourceSize, header.programHeaderOffset,
                        bytes - sizeof(header)) ||
            !mm::copyToAddressSpace(root, metadata + sizeof(header), headers,
                                    bytes - sizeof(header))) {
            return ElfLoadError::MappingFailed;
        }

        // 初始执行入口属于解释器，辅助信息中的 applicationEntry
        // 则保留主程序入口，供解释器完成工作后跳转。
        result.setEntryPoint(loader.entryPoint);
        result.setImageBase(application.imageBase);
        result.setImageEnd(application.imageEnd);
        result.setApplicationEntry(application.entryPoint);
        result.setApplicationBias(application.bias);
        result.setInterpreterBase(loader.imageBase);
        result.setProgramHeaders(metadata + sizeof(header));
        result.setProgramHeaderCount(header.programHeaderCount);

        return ElfLoadError::None;
    }

    /*
     * 把映像内动态地址加装载偏移，检查结果范围并确认目标页可读后返回有效地址。
     */
    static bool dynamicPointer(uint64_t root, const Image& image,
                               uint64_t value, uint64_t size,
                               uint64_t& result) {
        if (value > maximumUserAddress - image.bias) {
            return false;
        }

        result = image.bias + value;

        return mm::validateUserMemory(root, result, size, false);
    }

    /*
     * 解析动态段中的字符串表、符号表、哈希表和重定位表位置，检查表项大小及整段可读范围，供后续符号查找使用。
     */
    static ElfLoadError readDynamicInfo(uint64_t root, Image& image) {
        if (image.dynamicAddress == 0 ||
            image.dynamicSize < sizeof(DynamicEntry) ||
            image.dynamicSize % sizeof(DynamicEntry) != 0 ||
            !mm::validateUserMemory(root, image.dynamicAddress,
                                    image.dynamicSize, false)) {
            return ElfLoadError::UnsupportedProgramHeader;
        }

        // 动态段由标签和值组成；先收集表地址和大小，遇到结束标签就停止。
        uint64_t hash              = 0;
        uint64_t relaEntrySize     = sizeof(Rela);
        uint64_t symbolEntrySize   = sizeof(Symbol);
        uint64_t pltRelocationType = dynamicRelaType;
        const uint64_t entries     = image.dynamicSize / sizeof(DynamicEntry);

        for (uint64_t index = 0; index < entries; ++index) {
            DynamicEntry entry{};

            if (!mm::copyFromAddressSpace(
                    &entry, root,
                    image.dynamicAddress + index * sizeof(DynamicEntry),
                    sizeof(entry))) {
                return ElfLoadError::MappingFailed;
            }

            if (entry.tag == dynamicNull) {
                break;
            }

            uint64_t pointer = 0;

            // 动态表中的地址还未加装载偏移，每个地址都经 dynamicPointer
            // 换算并验证可读。
            switch (entry.tag) {
                case dynamicHash:
                    if (!dynamicPointer(root, image, entry.value, 8, pointer)) {
                        return ElfLoadError::InvalidAddress;
                    }

                    hash = pointer;
                    break;
                case dynamicStringTable:
                    if (!dynamicPointer(root, image, entry.value, 1, pointer)) {
                        return ElfLoadError::InvalidAddress;
                    }

                    image.stringTable = pointer;
                    break;
                case dynamicStringTableSize:
                    image.stringTableSize = entry.value;
                    break;
                case dynamicSymbolTable:
                    if (!dynamicPointer(root, image, entry.value,
                                        sizeof(Symbol), pointer)) {
                        return ElfLoadError::InvalidAddress;
                    }

                    image.symbolTable = pointer;
                    break;
                case dynamicSymbolEntrySize:
                    symbolEntrySize = entry.value;
                    break;
                case dynamicRela:
                    if (!dynamicPointer(root, image, entry.value, sizeof(Rela),
                                        pointer)) {
                        return ElfLoadError::InvalidAddress;
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
                        return ElfLoadError::InvalidAddress;
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

        // 收集完成再统一检查各表格式与完整范围，不能只验证表的第一个字节。
        if (symbolEntrySize != sizeof(Symbol) ||
            relaEntrySize != sizeof(Rela) ||
            pltRelocationType != dynamicRelaType || image.stringTable == 0 ||
            image.stringTableSize == 0 || image.symbolTable == 0 || hash == 0 ||
            image.relaSize % sizeof(Rela) != 0 ||
            image.jumpRelaSize % sizeof(Rela) != 0 ||
            !mm::validateUserMemory(root, image.stringTable,
                                    image.stringTableSize, false) ||
            (image.relaSize != 0 &&
             !mm::validateUserMemory(root, image.rela, image.relaSize,
                                     false)) ||
            (image.jumpRelaSize != 0 &&
             !mm::validateUserMemory(root, image.jumpRela, image.jumpRelaSize,
                                     false))) {
            return ElfLoadError::UnsupportedProgramHeader;
        }

        // SysV 哈希头给出桶数和符号数；限制两者规模，并验证整份桶链表均可读取。
        uint32_t hashHeader[2]{};

        if (!mm::copyFromAddressSpace(hashHeader, root, hash,
                                      sizeof(hashHeader)) ||
            hashHeader[0] == 0 || hashHeader[0] > 65536 || hashHeader[1] == 0 ||
            hashHeader[1] > 65536 ||
            !mm::validateUserMemory(
                root, hash,
                (2ULL + hashHeader[0] + hashHeader[1]) * sizeof(uint32_t),
                false)) {
            return ElfLoadError::InvalidHeader;
        }

        // 符号数取自链表长度，计算桶和链的实际位置后，继续验证完整符号表。
        image.symbolCount     = hashHeader[1];
        image.hashBucketCount = hashHeader[0];
        image.hashBuckets     = hash + 2 * sizeof(uint32_t);
        image.hashChains = image.hashBuckets + hashHeader[0] * sizeof(uint32_t);

        if (!mm::validateUserMemory(root, image.symbolTable,
                                    image.symbolCount * sizeof(Symbol),
                                    false)) {
            return ElfLoadError::InvalidAddress;
        }

        return ElfLoadError::None;
    }

    /*
     * 检查符号索引后，从目标地址空间复制一条固定大小符号记录。
     */
    static bool readSymbol(uint64_t root, const Image& image, uint32_t index,
                           Symbol& symbol) {
        return index < image.symbolCount &&
               mm::copyFromAddressSpace(
                   &symbol, root, image.symbolTable + index * sizeof(Symbol),
                   sizeof(symbol));
    }

    /*
     * 在两个映像的字符串表范围内逐字节比较符号名，只有完整匹配且遇到终止符才相等。
     */
    static bool symbolNamesEqual(uint64_t root, const Image& left,
                                 const Symbol& leftSymbol, const Image& right,
                                 const Symbol& rightSymbol) {
        if (leftSymbol.name >= left.stringTableSize ||
            rightSymbol.name >= right.stringTableSize) {
            return false;
        }

        // 比较长度取两个字符串表剩余范围中的较小值，名字即使缺少终止符也不能读出表外。
        uint64_t remaining = left.stringTableSize - leftSymbol.name;

        if (remaining > right.stringTableSize - rightSymbol.name) {
            remaining = right.stringTableSize - rightSymbol.name;
        }

        // 用小块跨地址空间复制降低逐字节翻译成本；只有字符相同且遇到零才算完整名字相等。
        for (uint64_t offset = 0; offset < remaining;) {
            char a[64], b[64];
            const uint64_t count =
                remaining - offset < sizeof(a) ? remaining - offset : sizeof(a);

            if (!mm::copyFromAddressSpace(
                    a, root, left.stringTable + leftSymbol.name + offset,
                    count) ||
                !mm::copyFromAddressSpace(
                    b, root, right.stringTable + rightSymbol.name + offset,
                    count)) {
                return false;
            }

            for (uint64_t i = 0; i < count; ++i) {
                if (a[i] != b[i]) {
                    return false;
                }

                if (a[i] == 0) {
                    return true;
                }
            }

            offset += count;
        }

        return false;
    }

    /*
     * 在字符串表边界内计算传统 ELF 符号哈希，拒绝没有终止符的名称。
     */
    static bool symbolHash(uint64_t root, const Image& image,
                           const Symbol& symbol, uint32_t& hash) {
        if (symbol.name >= image.stringTableSize) {
            return false;
        }

        hash                     = 0;
        const uint64_t remaining = image.stringTableSize - symbol.name;

        // 在字符串表边界内分块读取并计算 ELF
        // 哈希；到边界仍未遇到零表示名称无效。
        for (uint64_t offset = 0; offset < remaining;) {
            uint8_t bytes[64];
            const uint64_t count = remaining - offset < sizeof(bytes)
                                       ? remaining - offset
                                       : sizeof(bytes);

            if (!mm::copyFromAddressSpace(
                    bytes, root, image.stringTable + symbol.name + offset,
                    count)) {
                return false;
            }

            for (uint64_t i = 0; i < count; ++i) {
                if (bytes[i] == 0) {
                    return true;
                }

                hash = (hash << 4) + bytes[i];

                // 按 SysV ELF
                // 哈希规则折回高四位，使结果与文件中的桶表生成方式一致。
                const uint32_t high = hash & 0xf0000000U;
                hash ^= high >> 24;
                hash &= ~high;
            }

            offset += count;
        }

        return false;
    }

    /*
     * 按允许规则解析符号：可选受信内核导入，再搜索已加载映像的哈希链；未定义弱符号可解析为零，普通未定义符号报错。
     */
    static bool resolveSymbol(uint64_t root, const Image* images,
                              uint32_t imageCount, const Image& requester,
                              uint32_t symbolIndex, uint64_t& value,
                              bool trustedImports) {
        // 符号零约定为空符号，值为零，无需继续查表。
        if (symbolIndex == 0) {
            value = 0;

            return true;
        }

        Symbol requested{};

        if (!readSymbol(root, requester, symbolIndex, requested)) {
            return false;
        }

        // 只有调用者明确开启受信任导入时，才尝试通过内核限定的 serviceResolve
        // 查找入口。
        if (trustedImports) {
            char name[128]{};
            uint32_t i = 0;

            for (; i + 1 < sizeof(name) &&
                   requested.name + i < requester.stringTableSize;
                 ++i) {
                if (!mm::copyFromAddressSpace(
                        &name[i], root,
                        requester.stringTable + requested.name + i, 1)) {
                    return false;
                }

                if (!name[i]) {
                    break;
                }
            }

            if (i + 1 < sizeof(name) && !name[i]) {
                value = task::serviceResolve(name);

                if (value) {
                    return true;
                }
            }
        }

        uint32_t hash;

        if (!symbolHash(root, requester, requested, hash)) {
            return false;
        }

        // 按映像顺序在哈希桶中查找；桶只缩小候选范围，最终仍要比较完整符号名。
        for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex) {
            const Image& image = images[imageIndex];
            uint32_t index     = 0;

            if (!mm::copyFromAddressSpace(
                    &index, root,
                    image.hashBuckets +
                        (hash % image.hashBucketCount) * sizeof(uint32_t),
                    sizeof(index))) {
                return false;
            }

            // 用符号总数限制链遍历，损坏链即使成环也不能把内核困在无限查找中。
            uint64_t visited = 0;

            while (index != 0) {
                if (++visited > image.symbolCount) {
                    return false;
                }

                Symbol candidate{};

                if (!readSymbol(root, image, index, candidate)) {
                    return false;
                }

                const uint8_t binding = candidate.info >> 4;

                if (candidate.sectionIndex != sectionUndefined &&
                    (binding == 1 || binding == symbolBindingWeak) &&
                    symbolNamesEqual(root, requester, requested, image,
                                     candidate)) {
                    // 绝对符号已经是最终值，其余已定义符号需加所在映像的装载偏移。
                    value = candidate.sectionIndex == sectionAbsolute
                                ? candidate.value
                                : image.bias + candidate.value;

                    return true;
                }

                if (!mm::copyFromAddressSpace(
                        &index, root,
                        image.hashChains + index * sizeof(uint32_t),
                        sizeof(index))) {
                    return false;
                }
            }
        }

        // 缺少弱符号允许得到零；普通未定义符号则导致加载失败。
        if ((requested.info >> 4) == symbolBindingWeak) {
            value = 0;

            return true;
        }

        return false;
    }

    /*
     * 逐条读取并应用支持的 x86-64 RELA
     * 重定位，按类型计算目标值；符号解析或地址访问失败时立即返回加载错误。
     */
    static ElfLoadError applyRelocationTable(uint64_t root, const Image* images,
                                             uint32_t imageCount,
                                             const Image& image, uint64_t table,
                                             uint64_t size,
                                             bool trustedImports) {
        for (uint64_t offset = 0; offset < size; offset += sizeof(Rela)) {
            Rela relocation{};

            if (!mm::copyFromAddressSpace(&relocation, root, table + offset,
                                          sizeof(relocation)) ||
                relocation.offset > maximumUserAddress - image.bias) {
                return ElfLoadError::InvalidAddress;
            }

            // RELA 的 info
            // 低半部给出类型，高半部给出符号索引；目标偏移属于当前映像。
            const uint32_t type = static_cast<uint32_t>(relocation.info);
            const uint32_t symbol =
                static_cast<uint32_t>(relocation.info >> 32);
            const uint64_t target = image.bias + relocation.offset;

            if (!mm::validateUserMemory(root, target, sizeof(uint64_t),
                                        false)) {
                return ElfLoadError::InvalidAddress;
            }

            uint64_t value = 0;

            // 相对重定位只需装载偏移加
            // addend；需要符号的类型先在整个映像集合中解析。
            if (type == relocationRelative) {
                value = image.bias + static_cast<uint64_t>(relocation.addend);
            } else if (type == relocation64 || type == relocationGlobDat ||
                       type == relocationJumpSlot) {
                uint64_t symbolValue = 0;

                if (!resolveSymbol(root, images, imageCount, image, symbol,
                                   symbolValue, trustedImports)) {
                    return ElfLoadError::UndefinedSymbol;
                }

                value =
                    type == relocation64
                        ? symbolValue + static_cast<uint64_t>(relocation.addend)
                        : symbolValue;
            } else {
                return ElfLoadError::UnsupportedProgramHeader;
            }

            // 算好八字节最终值后通过目标页表写入，任何复制失败都中止加载，不能带着半份重定位启动。
            if (!mm::copyToAddressSpace(root, target, &value, sizeof(value))) {
                return ElfLoadError::MappingFailed;
            }
        }

        return ElfLoadError::None;
    }

    /*
     * 装入主程序及共享映像集合。
     * 有解释器时交由解释器路径启动，否则读取各动态表并完成内核支持的重定位后返回主入口。
     */
    ElfLoadError Loader::loadDynamicSetFromAddressSpace(
        uint64_t sourceRoot, uint64_t executableAddress, size_t executableSize,
        const ElfSharedImage* sharedImages, uint32_t sharedImageCount,
        ElfLoadResult& result, bool trustedImports) {
        // 绑定任务的调用先持地址空间锁，再转为明确页表根的内部调用，保证加载时根不会被替换。
        if (target_) {
            ::task::PreemptionSpinLockGuard guard(
                target_->addressSpace().lock());

            return Loader(target_->addressSpace().rootPhysicalAddress())
                .loadDynamicSetFromAddressSpace(
                    sourceRoot, executableAddress, executableSize, sharedImages,
                    sharedImageCount, result, trustedImports);
        }

        const uint64_t destinationRoot = root_;
        result                         = {};

        if (destinationRoot == 0 || sourceRoot == 0 || executableAddress == 0 ||
            executableSize == 0 || sharedImageCount == 0 ||
            sharedImageCount >= maximumImages || sharedImages == nullptr) {
            return ElfLoadError::InvalidArgument;
        }

        // 零号是主程序，其余槽是共享映像；先验证主程序头并检查是否声明解释器。
        Image images[maximumImages]{};
        images[0].sourceAddress = executableAddress;
        images[0].sourceSize    = executableSize;
        DynamicElfHeader header{};

        if (!readSource(&header, sourceRoot, executableAddress, executableSize,
                        0, sizeof(header))) {
            return ElfLoadError::InvalidHeader;
        }

        ElfLoadError validation = validateHeader(header, executableSize, true);

        if (validation != ElfLoadError::None) {
            return validation;
        }

        bool interpreted = false;

        for (uint16_t i = 0; i < header.programHeaderCount; ++i) {
            DynamicProgramHeader segment{};

            if (!readSource(&segment, sourceRoot, executableAddress,
                            executableSize,
                            header.programHeaderOffset + i * sizeof(segment),
                            sizeof(segment))) {
                return ElfLoadError::InvalidProgramHeader;
            }

            if (segment.type != programInterpreter) {
                continue;
            }

            if (interpreted || segment.fileSize < 2 || segment.fileSize > 256) {
                return ElfLoadError::InvalidProgramHeader;
            }

            // 解释器路径必须是单个完整绝对路径，不能含提前结束的零或缺少末尾终止符。
            char path[256]{};

            if (!readSource(path, sourceRoot, executableAddress, executableSize,
                            segment.offset, segment.fileSize) ||
                path[0] != '/' || path[segment.fileSize - 1] || !path[1]) {
                return ElfLoadError::InvalidProgramHeader;
            }

            for (uint64_t byte = 0; byte + 1 < segment.fileSize; ++byte) {
                if (!path[byte]) {
                    return ElfLoadError::InvalidProgramHeader;
                }
            }

            interpreted = true;
        }

        // 有解释器时只接收指定的一份解释器映像，后续共享库装载交给它；内核不再做这里的符号重定位。
        if (interpreted) {
            if (sharedImageCount != 1) {
                return ElfLoadError::InvalidArgument;
            }

            return loadInterpreter(destinationRoot, sourceRoot, images[0],
                                   header, sharedImages[0], result);
        }

        ElfLoadError error =
            loadImage(destinationRoot, sourceRoot, 0x400000, true, images[0]);

        if (error != ElfLoadError::None) {
            return error;
        }

        // 无解释器时由内核装入全部共享映像；每份可有独立来源页表，否则沿用主程序来源。
        for (uint32_t index = 0; index < sharedImageCount; ++index) {
            images[index + 1].sourceAddress =
                sharedImages[index].getSourceAddress();
            images[index + 1].sourceSize = sharedImages[index].getSize();
            error =
                loadImage(destinationRoot,
                          sharedImages[index].getSourceRoot()
                              ? sharedImages[index].getSourceRoot()
                              : sourceRoot,
                          sharedImageBases[index], false, images[index + 1]);

            if (error != ElfLoadError::None) {
                return error;
            }
        }

        // 必须先把所有动态表解析好，之后解析符号时才能在整个集合中查找定义。
        const uint32_t imageCount = sharedImageCount + 1;

        for (uint32_t index = 0; index < imageCount; ++index) {
            error = readDynamicInfo(destinationRoot, images[index]);

            if (error != ElfLoadError::None) {
                return error;
            }
        }

        for (uint32_t index = 0; index < imageCount; ++index) {
            // 每份映像依次处理普通 RELA 和跳转槽
            // RELA，任一错误都交由上层撤销新任务。
            error = applyRelocationTable(
                destinationRoot, images, imageCount, images[index],
                images[index].rela, images[index].relaSize, trustedImports);

            if (error != ElfLoadError::None) {
                return error;
            }

            error = applyRelocationTable(destinationRoot, images, imageCount,
                                         images[index], images[index].jumpRela,
                                         images[index].jumpRelaSize,
                                         trustedImports);

            if (error != ElfLoadError::None) {
                return error;
            }
        }

        // 所有映射与重定位成功后才发布主入口和映像范围，调用者随后创建初始用户线程。
        result.setEntryPoint(images[0].entryPoint);
        result.setImageBase(images[0].imageBase);
        result.setImageEnd(images[0].imageEnd);

        return ElfLoadError::None;
    }

}  // namespace elf
