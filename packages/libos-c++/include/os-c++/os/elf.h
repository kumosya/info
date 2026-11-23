/*
 * ELF 程序格式与加载。
 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace os {
    /* 保存 ELF 文件类别、架构、入口和各头表位置。 */
    struct ElfHeader {
            uint8_t identity[16];
            uint16_t type, machine;
            uint32_t version;
            uint64_t entry, programOffset, sectionOffset;
            uint32_t flags;
            uint16_t size, programSize, programCount, sectionSize, sectionCount,
                sectionNames;
    };
    /* 描述一个 ELF 程序段的文件位置、内存范围和权限。 */
    struct ElfProgramHeader {
            uint32_t type, flags;
            uint64_t offset, address, physical, fileSize, memorySize, alignment;
    };
    /* 保存 ELF 动态链接表的一项标签和对应数值。 */
    struct ElfDynamic {
            int64_t tag;
            uint64_t value;
    };
    /* 保存 ELF 符号的名称索引、地址、大小及绑定属性。 */
    struct ElfSymbol {
            uint32_t name;
            uint8_t info, visibility;
            uint16_t section;
            uint64_t value, size;
    };
    /* 保存带显式加数的 ELF 重定位项，指明需要修正的地址与符号。 */
    struct ElfRela {
            uint64_t offset, info;
            int64_t addend;
    };

    static_assert(sizeof(ElfHeader) == 64);
    static_assert(sizeof(ElfProgramHeader) == 56);
    static_assert(sizeof(ElfDynamic) == 16);
    static_assert(sizeof(ElfSymbol) == 24);
    static_assert(sizeof(ElfRela) == 24);
    /* 校验并查询 ELF 文件头、程序段和动态加载信息。 */
    class Executable {
        public:
            Executable();
            ~Executable();
            int open(const void* image, size_t size);
            void close();
            int validate();
            bool getValid() const;
            uint16_t getArchitecture() const;
            uint16_t getType() const;
            uint64_t getEntry() const;
            uint32_t getProgramCount() const;
            int getProgram(uint32_t index, ElfProgramHeader& program) const;
            int getInterpreter(char* buffer, size_t capacity) const;

        private:
            const uint8_t* image_{};
            size_t size_{};
            bool valid_{};
            uint16_t architecture_{}, type_{}, programCount_{};
            uint64_t entry_{}, programOffset_{}, interpreterOffset_{},
                interpreterSize_{};
    };
}  // namespace os
