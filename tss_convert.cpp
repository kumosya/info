// 汇编代码翻译成C++
#include <cstdint>

struct GdtEntry {
    std::uint64_t value;
};

struct TssEntry {
    // TSS结构定义
    std::uint32_t reserved0;
    std::uint64_t rsp0;
    std::uint64_t rsp1;
    std::uint64_t rsp2;
    std::uint32_t reserved1;
    std::uint32_t ist1;
    std::uint32_t ist2;
    std::uint32_t ist3;
    std::uint32_t ist4;
    std::uint32_t ist5;
    std::uint32_t ist6;
    std::uint32_t ist7;
    std::uint32_t reserved2;
    std::uint16_t reserved3;
    std::uint16_t iomap_base;
} __attribute__((packed));

// 假设的GDT表和TSS表
GdtEntry gdt_table[8]; // 至少6个条目
TssEntry tss_table;

int main() {
    // 1. 加载TSS表的地址到rdx
    std::uint64_t tss_addr = reinterpret_cast<std::uint64_t>(&tss_table);
    std::uint64_t rax = 0;
    std::uint64_t rcx = 0;
    std::uint64_t rdx = tss_addr;
    std::uint64_t rdi = reinterpret_cast<std::uint64_t>(&gdt_table);

    // 2. 设置rax为0x89（访问字节）
    rax = 0x89;
    // 3. 将rax左移40位
    rax <<= 40;
    
    // 4. 获取TSS地址的第24-31位
    rcx = static_cast<std::uint32_t>(rdx); // 取rdx的低32位
    rcx >>= 24;
    rcx <<= 56;
    rax += rcx;
    
    // 5. 获取TSS地址的第0-23位
    rcx = static_cast<std::uint32_t>(rdx); // 取rdx的低32位
    rcx &= 0xffffff;
    rcx <<= 16;
    rax += rcx;
    
    // 6. 加上103（可能是TSS描述符的某些标志位）
    rax += 103;
    
    // 7. 将rax存储到GDT表的第64个字节处（第5个条目）
    reinterpret_cast<std::uint64_t*>(gdt_table)[5] = rax;
    
    // 8. 获取TSS地址的高32位并存储到GDT表的第72个字节处
    rdx >>= 32;
    reinterpret_cast<std::uint64_t*>(gdt_table)[6] = rdx;
    
    return 0;
}

// 与之前修改的SetEntry函数对比：
// void SetEntry(int index, std::uint64_t base, std::uint64_t limit, std::uint8_t access, std::uint8_t gran) {
//     std::uint64_t descriptor;
//     descriptor = (limit & 0x000F0000ULL) >> 16;         // Limit bits 16-19
//     descriptor |= static_cast<std::uint64_t>(gran & 0x0F) << 52;    // Granularity
//     descriptor |= static_cast<std::uint64_t>(access & 0xFF) << 40;  // Access byte
//     descriptor |= (base & 0xFF000000ULL) >> 24;        // Base bits 24-31
//     descriptor |= (base & 0x00FFFFFFULL) << 16;  // Base bits 0-23
//     descriptor |= (limit & 0x0000FFFFULL);        // Limit bits 0-15
//     gdt_table[index] = descriptor;
// }