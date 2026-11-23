/*
 * 验证静态 ELF 布局、建立段页面，并在失败时撤销本次映射。
 */

#include "kernel/elf.h"

#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/sync.h"
#include "kernel/task.h"

namespace elf {

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

    /* 按 ELF64 文件布局读取的文件头，给出架构、入口与程序头数组位置。 */
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

    /* 描述一个程序段的文件范围、虚拟地址、内存长度和权限。 */
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

    /* 汇总同一页上全部段的读写执行需求，避免只按一个段设置权限。 */
    struct PagePermissions {
            bool writable;
            bool executable;
    };

    /*
     * 将地址向下对齐到页面边界。
     */
    static uint64_t alignDown(uint64_t value) {
        return value & ~(PAGE_SIZE - 1);
    }

    /*
     * 检查溢出后把长度或地址向上补齐到页面边界，通过输出参数返回结果。
     */
    static bool alignUp(uint64_t value, uint64_t& result) {
        if (value > UINT64_MAX - (PAGE_SIZE - 1)) {
            return false;
        }

        result = (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        return true;
    }

    /*
     * 校验一段偏移与长度没有越过映像总长，使用减法避免求和回绕。
     */
    static bool checkedRange(uint64_t offset, uint64_t size, uint64_t limit) {
        return offset <= limit && size <= limit - offset;
    }

    /*
     * 判断段对齐值是否为合法二次幂。
     */
    static bool isPowerOfTwo(uint64_t value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    /*
     * 判断两个左闭右开地址范围是否相交，端点恰好相接不算重叠。
     */
    static bool rangesOverlap(uint64_t leftStart, uint64_t leftEnd,
                              uint64_t rightStart, uint64_t rightEnd) {
        return leftStart < rightEnd && rightStart < leftEnd;
    }

    /*
     * 检查程序头是否描述一个非空的可加载内存段。
     */
    static bool isLoadable(const ProgramHeader& header) {
        return header.type == PROGRAM_HEADER_LOAD && header.memorySize != 0;
    }

    /*
     * 汇总所有覆盖指定页的段权限，同页不同段共用实际页面权限，合并后还要遵守禁止同时写与执行的规则。
     */
    static PagePermissions getPagePermissions(const ProgramHeader* headers,
                                              uint16_t headerCount,
                                              uint64_t pageAddress) {
        PagePermissions permissions{};
        const uint64_t pageEnd = pageAddress + PAGE_SIZE;

        // 汇总与此页相交的所有段；只看段起始地址会遗漏同页另一半需要的权限。
        for (uint16_t index = 0; index < headerCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment)) {
                continue;
            }

            const uint64_t segmentEnd =
                segment.virtualAddress + segment.memorySize;

            if (!rangesOverlap(pageAddress, pageEnd, segment.virtualAddress,
                               segmentEnd)) {
                continue;
            }

            permissions.writable |=
                (segment.flags & SEGMENT_FLAG_WRITABLE) != 0;
            permissions.executable |=
                (segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0;
        }

        return permissions;
    }

    /*
     * 验证静态 ELF
     * 文件头和程序头数组布局，拒绝不支持的架构、文件类型或越界头表。
     */
    static ElfLoadError validateHeader(const ElfHeader& header,
                                       uint64_t imageSize) {
        // 按顺序区分魔数、架构与布局错误，便于启动日志给出具体原因。
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

        // 固定大小的局部程序头数组不能容纳任意条目数；文件偏移和剩余长度也必须匹配。
        if (header.headerSize != sizeof(ElfHeader) ||
            header.programHeaderSize != sizeof(ProgramHeader) ||
            header.programHeaderCount == 0 ||
            header.programHeaderCount > MAXIMUM_PROGRAM_HEADERS ||
            header.programHeaderOffset > imageSize ||
            header.programHeaderCount >
                (imageSize - header.programHeaderOffset) /
                    sizeof(ProgramHeader)) {
            return ElfLoadError::InvalidHeader;
        }

        if (header.entry < MINIMUM_USER_ADDRESS ||
            header.entry >= MAXIMUM_USER_ADDRESS) {
            return ElfLoadError::InvalidEntryPoint;
        }

        return ElfLoadError::None;
    }

    /*
     * 检查全部程序段的文件范围、地址、权限、对齐、相互重叠及入口位置，尚不修改页表。
     */
    static ElfLoadError validateSegments(const ProgramHeader* headers,
                                         uint16_t headerCount,
                                         uint64_t imageSize,
                                         uint64_t entryPoint,
                                         ElfLoadResult& result) {
        bool foundLoadableSegment = false;
        bool entryIsExecutable    = false;
        uint64_t mappedBytes      = 0;
        result.setImageBase(UINT64_MAX);
        result.setImageEnd(0);

        // 这一轮只验证，不建映射；静态路径明确拒绝动态段与解释器，交给另一加载路径处理。
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

            // 限定支持的权限位，且禁止同一段同时可写可执行，减少数据被当作代码执行的机会。
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

            const uint64_t segmentEnd =
                segment.virtualAddress + segment.memorySize;
            uint64_t pageEnd;

            if (!alignUp(segmentEnd, pageEnd)) {
                return ElfLoadError::InvalidAddress;
            }

            // 以页为单位累计段占用并施加映像大小上限，避免输入要求分配过大的用户内存。
            const uint64_t segmentMappedBytes =
                pageEnd - alignDown(segment.virtualAddress);

            if (segmentMappedBytes > MAXIMUM_IMAGE_MEMORY - mappedBytes) {
                return ElfLoadError::InvalidAddress;
            }

            mappedBytes += segmentMappedBytes;

            // 检查实际段字节范围不重叠；页边界相同并不必然代表段内容重叠。
            for (uint16_t previous = 0; previous < index; ++previous) {
                const ProgramHeader& other = headers[previous];

                if (!isLoadable(other)) {
                    continue;
                }

                const uint64_t otherEnd =
                    other.virtualAddress + other.memorySize;

                if (rangesOverlap(segment.virtualAddress, segmentEnd,
                                  other.virtualAddress, otherEnd)) {
                    return ElfLoadError::OverlappingSegments;
                }
            }

            // 收集结果范围，同时确认入口真正落在一个可执行段内部，而不是映像的空洞中。
            foundLoadableSegment = true;

            if (segment.virtualAddress < result.getImageBase()) {
                result.setImageBase(segment.virtualAddress);
            }

            if (segmentEnd > result.getImageEnd()) {
                result.setImageEnd(segmentEnd);
            }

            if ((segment.flags & SEGMENT_FLAG_EXECUTABLE) != 0 &&
                entryPoint >= segment.virtualAddress &&
                entryPoint < segmentEnd) {
                entryIsExecutable = true;
            }
        }

        // 必须有可加载内容且入口可执行，才能进入后续按页权限复查。
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

            if (!alignUp(segment.virtualAddress + segment.memorySize,
                         pageEnd)) {
                return ElfLoadError::InvalidAddress;
            }

            for (uint64_t pageAddress = alignDown(segment.virtualAddress);
                 pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
                // 两个合法段共用一页时权限会合并，合并后仍不能同时可写可执行。
                const PagePermissions permissions =
                    getPagePermissions(headers, headerCount, pageAddress);

                if (permissions.writable && permissions.executable) {
                    return ElfLoadError::InvalidPermissions;
                }
            }
        }

        return ElfLoadError::None;
    }

    /*
     * 为内核缓冲区中的各程序段分配用户页并复制文件内容，未提供文件字节的内存部分保留清零初值。
     */
    static ElfLoadError mapSegments(uint64_t root, const uint8_t* image,
                                    const ProgramHeader* headers,
                                    uint16_t headerCount) {
        // 先分配全部段页面，再复制文件内容；新页面清零后，未被文件覆盖的 BSS
        // 部分保持零值。
        for (uint16_t index = 0; index < headerCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment)) {
                continue;
            }

            uint64_t pageEnd = 0;

            if (!alignUp(segment.virtualAddress + segment.memorySize,
                         pageEnd)) {
                return ElfLoadError::InvalidAddress;
            }

            for (uint64_t pageAddress = alignDown(segment.virtualAddress);
                 pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
                // 同一页可能由相邻段共享，已有页不重复分配，避免覆盖前一个段的数据。
                if (mm::getPhysicalAddressInAddressSpace(root, pageAddress) !=
                    0) {
                    continue;
                }

                // 设置合并后的用户读写与执行权限，不能简单照搬当前这一个段的标志。
                const PagePermissions permissions =
                    getPagePermissions(headers, headerCount, pageAddress);
                uint64_t pageFlags = mm::pageUser;

                if (permissions.writable) {
                    pageFlags |= mm::pageWritable;
                }

                if (!permissions.executable) {
                    pageFlags |= mm::pageNoExecute;
                }

                const mm::UserPageMapResult mapResult =
                    mm::allocateUserPageInAddressSpace(root, pageAddress,
                                                       pageFlags);

                if (mapResult == mm::UserPageMapResult::OutOfMemory) {
                    return ElfLoadError::OutOfMemory;
                }

                if (mapResult != mm::UserPageMapResult::Success) {
                    return ElfLoadError::MappingFailed;
                }
            }
        }

        for (uint16_t index = 0; index < headerCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment) || segment.fileSize == 0) {
                continue;
            }

            // 源数据在内核缓冲区，但目的地址属于目标页表，写入仍通过地址空间复制接口完成。
            if (!mm::copyToAddressSpace(root, segment.virtualAddress,
                                        image + segment.offset,
                                        segment.fileSize)) {
                return ElfLoadError::MappingFailed;
            }
        }

        return ElfLoadError::None;
    }

    /*
     * 从另一地址空间分块读取段数据并写入新地址空间，段页面准备与权限规则和普通加载路径一致。
     */
    static ElfLoadError mapSegmentsFromAddressSpace(
        uint64_t root, uint64_t sourceRoot, uint64_t sourceAddress,
        const ProgramHeader* headers, uint16_t headerCount) {
        // 先分配全部段页面，再复制文件内容；新页面清零后，未被文件覆盖的 BSS
        // 部分保持零值。
        for (uint16_t index = 0; index < headerCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment)) {
                continue;
            }

            uint64_t pageEnd = 0;

            if (!alignUp(segment.virtualAddress + segment.memorySize,
                         pageEnd)) {
                return ElfLoadError::InvalidAddress;
            }

            for (uint64_t pageAddress = alignDown(segment.virtualAddress);
                 pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
                // 同一页可能由相邻段共享，已有页不重复分配，避免覆盖前一个段的数据。
                if (mm::getPhysicalAddressInAddressSpace(root, pageAddress) !=
                    0) {
                    continue;
                }

                // 设置合并后的用户读写与执行权限，不能简单照搬当前这一个段的标志。
                const PagePermissions permissions =
                    getPagePermissions(headers, headerCount, pageAddress);
                uint64_t pageFlags = mm::pageUser;

                if (permissions.writable) {
                    pageFlags |= mm::pageWritable;
                }

                if (!permissions.executable) {
                    pageFlags |= mm::pageNoExecute;
                }

                const mm::UserPageMapResult mapResult =
                    mm::allocateUserPageInAddressSpace(root, pageAddress,
                                                       pageFlags);

                if (mapResult == mm::UserPageMapResult::OutOfMemory) {
                    return ElfLoadError::OutOfMemory;
                }

                if (mapResult != mm::UserPageMapResult::Success) {
                    return ElfLoadError::MappingFailed;
                }
            }
        }

        // 源和目标可能都不是当前页表，借用一页内核缓冲区分批读出再写入，避免直接解引用用户指针。
        uint8_t buffer[PAGE_SIZE];

        for (uint16_t index = 0; index < headerCount; ++index) {
            const ProgramHeader& segment = headers[index];
            uint64_t copied              = 0;

            while (isLoadable(segment) && copied < segment.fileSize) {
                const uint64_t remaining = segment.fileSize - copied;
                const uint64_t chunk =
                    remaining < sizeof(buffer) ? remaining : sizeof(buffer);

                if (!mm::copyFromAddressSpace(
                        buffer, sourceRoot,
                        sourceAddress + segment.offset + copied, chunk) ||
                    !mm::copyToAddressSpace(
                        root, segment.virtualAddress + copied, buffer, chunk)) {
                    return ElfLoadError::MappingFailed;
                }

                copied += chunk;
            }
        }

        return ElfLoadError::None;
    }

    /*
     * 撤销本次加载段对应的页面并归还物理页，供前面映射或拷贝失败时回滚。
     */
    static void rollbackMappedSegments(uint64_t root,
                                       const ProgramHeader* headers,
                                       uint16_t headerCount) {
        for (uint16_t index = 0; index < headerCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment)) {
                continue;
            }

            uint64_t pageEnd = 0;

            if (!alignUp(segment.virtualAddress + segment.memorySize,
                         pageEnd)) {
                continue;
            }

            for (uint64_t pageAddress = alignDown(segment.virtualAddress);
                 pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
                // 共享页可能已在前一个段的回滚中撤掉，因此每次重新查映射，只处理仍存在的页。
                const uint64_t physical =
                    mm::getPhysicalAddressInAddressSpace(root, pageAddress);

                if (physical == 0) {
                    continue;
                }

                // 确认映射撤销成功才归还物理页，防止仍可访问的页被分配给别的用途。
                if (mm::unmapPageInAddressSpace(root, pageAddress)) {
                    mm::freePhysicalPage(physical & ~(PAGE_SIZE - 1));
                }
            }
        }
    }

    /*
     * 让加载器绑定一个目标任务，具体操作时再取得该任务当前页表根并在其锁下修改。
     */
    Loader::Loader(::task::Task& task) : root_(0), target_(&task) {}

    /*
     * 从内核可访问的 ELF
     * 缓冲区加载静态程序，先完整验证再映射，失败时撤销本次建立的段页面。
     */
    ElfLoadError Loader::load(const void* image, size_t size,
                              ElfLoadResult& result) {
        result = {};

        if (!target_) {
            return ElfLoadError::InvalidArgument;
        }

        ::task::Task& task = *target_;

        if (image == nullptr || size < sizeof(ElfHeader)) {
            return ElfLoadError::InvalidArgument;
        }

        // 文件在内核可访问缓冲区，先验证头和全部段，任何结构错误都在修改页表之前返回。
        const uint8_t* bytes    = static_cast<const uint8_t*>(image);
        const ElfHeader& header = *reinterpret_cast<const ElfHeader*>(bytes);
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

        // 锁住目标地址空间，先确认所有待装载地址都为空，保证失败回滚只涉及本次新页。
        ::task::PreemptionSpinLockGuard addressGuard(
            task.addressSpace().lock());
        const uint64_t root = task.addressSpace().rootPhysicalAddress();

        for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment)) {
                continue;
            }

            uint64_t pageEnd = 0;

            if (!alignUp(segment.virtualAddress + segment.memorySize,
                         pageEnd)) {
                result = {};

                return ElfLoadError::InvalidAddress;
            }

            for (uint64_t pageAddress = alignDown(segment.virtualAddress);
                 pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
                if (mm::getPhysicalAddressInAddressSpace(root, pageAddress) !=
                    0) {
                    result = {};

                    return ElfLoadError::MappingFailed;
                }
            }
        }

        // 页面分配或复制失败时统一撤销本次段映射，并清空输出，调用者不会拿到半成品结果。
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

    /*
     * 从源任务地址空间加载
     * ELF；安全复制头表、验证全部段，再在目标根中映射和复制，失败时统一回滚。
     */
    ElfLoadError Loader::loadFromAddressSpace(uint64_t sourceRoot,
                                              uint64_t sourceAddress,
                                              size_t size,
                                              ElfLoadResult& result) {
        // 绑定任务时先锁定当前页表根，再复用按根操作的加载逻辑，避免在两套路径中重复维护验证规则。
        if (target_) {
            ::task::PreemptionSpinLockGuard guard(
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

        // 头和程序头都从源页表复制到内核；必须先验证头，才能用其中的条目数量决定复制长度。
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

        if (!mm::copyFromAddressSpace(
                headers, sourceRoot, sourceAddress + header.programHeaderOffset,
                headerBytes)) {
            return ElfLoadError::InvalidHeader;
        }

        const ElfLoadError segmentError = validateSegments(
            headers, header.programHeaderCount, size, header.entry, result);

        if (segmentError != ElfLoadError::None) {
            result = {};

            return segmentError;
        }

        // 在开始分配之前检查全部目标范围为空；这是后面能按段安全回滚的前提。
        const uint64_t root = destinationRoot;

        for (uint16_t index = 0; index < header.programHeaderCount; ++index) {
            const ProgramHeader& segment = headers[index];

            if (!isLoadable(segment)) {
                continue;
            }

            uint64_t pageEnd = 0;

            if (!alignUp(segment.virtualAddress + segment.memorySize,
                         pageEnd)) {
                result = {};

                return ElfLoadError::InvalidAddress;
            }

            for (uint64_t pageAddress = alignDown(segment.virtualAddress);
                 pageAddress < pageEnd; pageAddress += PAGE_SIZE) {
                if (mm::getPhysicalAddressInAddressSpace(root, pageAddress) !=
                    0) {
                    result = {};

                    return ElfLoadError::MappingFailed;
                }
            }
        }

        // 验证通过后才跨页表复制段数据，映射失败则撤销已建页并清空结果。
        const ElfLoadError mappingError =
            mapSegmentsFromAddressSpace(root, sourceRoot, sourceAddress,
                                        headers, header.programHeaderCount);

        if (mappingError != ElfLoadError::None) {
            rollbackMappedSegments(root, headers, header.programHeaderCount);
            result = {};

            return mappingError;
        }

        result.setEntryPoint(header.entry);

        return ElfLoadError::None;
    }

    /*
     * 把加载错误枚举转换成固定诊断文本，供启动日志报告具体失败原因。
     */
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

    /*
     * 构造合法和非法的小型 ELF
     * 输入执行加载自检，检查错误分类、段权限以及失败回滚是否符合约定。
     */
    bool runElfValidationSelfTest(::task::Task& task) {
        /* 自检专用的小型 ELF，包含文件头、两个程序段和测试载荷。 */
        struct TestImage {
                ElfHeader header;
                ProgramHeader segments[2];
                uint8_t data[16];
        } image{};

        // 先构造一份最小可执行输入，再逐项改坏字段，检查加载器是否返回预期的具体错误。
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

        // 把入口移出可执行段，确认不能仅凭地址在用户区就放行。
        image.header.programHeaderCount = 1;
        image.segments[0].type          = PROGRAM_HEADER_LOAD;
        image.segments[0].flags =
            SEGMENT_FLAG_READABLE | SEGMENT_FLAG_EXECUTABLE;
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
        image.segments[0].flags    = SEGMENT_FLAG_READABLE |
                                  SEGMENT_FLAG_WRITABLE |
                                  SEGMENT_FLAG_EXECUTABLE;

        if (Loader(task).load(&image, sizeof(image), result) !=
            ElfLoadError::InvalidPermissions) {
            return false;
        }

        image.segments[0].flags =
            SEGMENT_FLAG_READABLE | SEGMENT_FLAG_EXECUTABLE;
        image.header.programHeaderCount = 2;
        image.segments[1].type          = PROGRAM_HEADER_LOAD;

        // 最后把输入恢复为合法布局，确认成功映射，并检查最终页是用户可读、不可写的代码页。
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

        image.header.entry              = 0x400000;
        image.header.programHeaderCount = 2;

        // 构造两个实际不重叠却共用一页的段，专门检查页级权限合并后是否拒绝写执行组合。
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
            result.getImageBase() != 0x400000 ||
            result.getImageEnd() != 0x401000) {
            return false;
        }

        uint64_t flags = 0;

        return mm::getPhysicalAddressInAddressSpace(
                   task.addressSpace().rootPhysicalAddress(), 0x400000,
                   &flags) != 0 &&
               (flags & ::mm::pageUser) != 0 &&
               (flags & ::mm::pageWritable) == 0 &&
               (flags & ::mm::pageNoExecute) == 0;
    }

}  // namespace elf
