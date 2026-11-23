/* 检查普通代码节的 entrySize 提示和固定记录表的长度校验。 */
#include <os/elf.h>
#include <cassert>
#include <cstring>

/* 每次使用独立解析对象，验证完整 ELF 预检流程。 */
static bool accepts(const unsigned char* image, size_t size) {
    os::Executable executable;
    return executable.open(image, size) == 0;
}

int main() {
    unsigned char image[1024]{};
    os::ElfHeader header{};
    const unsigned char identity[] = {127, 'E', 'L', 'F', 2, 1, 1};
    std::memcpy(header.identity, identity, sizeof(identity));
    header.type = 2;
    header.machine = 62;
    header.version = 1;
    header.entry = 0x400010;
    header.programOffset = sizeof(header);
    header.sectionOffset = 512;
    header.size = sizeof(header);
    header.programSize = sizeof(os::ElfProgramHeader);
    header.programCount = 1;
    header.sectionSize = 64;
    header.sectionCount = 2;
    std::memcpy(image, &header, sizeof(header));

    os::ElfProgramHeader program{};
    program.type = 1;
    program.flags = 5;
    program.offset = 256;
    program.address = 0x400000;
    program.fileSize = program.memorySize = 37;
    program.alignment = 1;
    std::memcpy(image + sizeof(header), &program, sizeof(program));

    // sh_type、sh_flags、sh_addr、sh_offset、sh_size、sh_link/info、sh_addralign、sh_entsize。
    uint64_t section[8] = {uint64_t(1) << 32, 6, 0x400000, 256, 37, 0, 16, 16};
    std::memcpy(image + 576, section, sizeof(section));
    assert(accepts(image, sizeof(image)));

    section[0] = uint64_t(2) << 32;
    std::memcpy(image + 576, section, sizeof(section));
    assert(!accepts(image, sizeof(image)));

    section[4] = 32;
    std::memcpy(image + 576, section, sizeof(section));
    assert(accepts(image, sizeof(image)));

    section[3] = sizeof(image) - 8;
    std::memcpy(image + 576, section, sizeof(section));
    assert(!accepts(image, sizeof(image)));

    section[0] = uint64_t(1) << 32;
    section[1] = 0x10;
    section[3] = 256;
    section[4] = 37;
    std::memcpy(image + 576, section, sizeof(section));
    assert(!accepts(image, sizeof(image)));
    return 0;
}
