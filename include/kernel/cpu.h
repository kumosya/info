#ifndef INFO_KERNEL_CPU_H_
#define INFO_KERNEL_CPU_H_

/* CR0 */
#define CR0_PG (1 << 31)

/* CR4 */
#define CR4_PSE (1 << 4)
#define CR4_PAE (1 << 5)
#define CR4_PGE (1 << 7)
#define CR4_OSFXSR (1 << 9)      /* OS supports FXSAVE/FXRSTOR */
#define CR4_OSXMMEXCPT (1 << 10) /* OS supports SIMD FP exceptions */

/* Segment selector */
#define SELECTOR_RPL (0)
#define SELECTOR_TI (2)
#define SELECTOR_INDEX (3)

#define GDT_TYPE_ACCESSED (1 << 0)
#define GDT_TYPE_RW (1 << 1)
#define GDT_TYPE_CONFORMING (1 << 2)
#define GDT_TYPE_EXPAND_DOWN (1 << 2)

#define GDT_TYPE_CODE (1 << 3 | 1 << 4)
#define GDT_TYPE_DATA (1 << 4)

#define GDT_DPL_RING0 (0 << 5)
#define GDT_DPL_RING3 (3 << 5)

#define GDT_PRESENT (1 << 7)

#ifndef ASM_FILE

#include <cstdint>

struct faultStack_code {
    std::uint64_t r11, r10, r9, r8;
    std::uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    std::uint64_t error_code, rip, cs, rflags, rsp, ss;
} __attribute__((packed));

struct faultStack_nocode {
    std::uint64_t r11, r10, r9, r8;
    std::uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    std::uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

namespace idt {
struct Entry {
    std::uint16_t offset_low;
    std::uint16_t selector;
    std::uint8_t ist;
    std::uint8_t type_attr;
    std::uint16_t offset_mid;
    std::uint32_t offset_high;
    std::uint32_t zero;
} __attribute__((packed));

struct Ptr {
    std::uint16_t limit;
    std::uint64_t base;
} __attribute__((packed));

void Init();
}  // namespace idt

namespace gdt {

struct Ptr {
    std::uint16_t limit;
    std::uint64_t base;
} __attribute__((packed));

struct TssEntry {
    std::uint32_t reserved0;
    std::uint64_t rsp0;
    std::uint64_t rsp1;
    std::uint64_t rsp2;
    std::uint64_t reserved1;
    std::uint64_t ist1;
    std::uint64_t ist2;
    std::uint64_t ist3;
    std::uint64_t ist4;
    std::uint64_t ist5;
    std::uint64_t ist6;
    std::uint64_t ist7;
    std::uint64_t reserved2;
    std::uint16_t reserved3;
    std::uint16_t iomap_base;
} __attribute__((packed, aligned(16)));

extern TssEntry *tss;
void SetEntry(int index, std::uint64_t base, std::uint64_t limit,
              std::uint8_t access, std::uint8_t gran);
void SetTss(int index, std::uint64_t tss_base, std::uint8_t access);
void Init();
}  // namespace gdt

// Stubs implemented in interrupt.S
extern "C" void pit_stub();
extern "C" void kbd_stub();

extern "C" void de_stub();
extern "C" void debug_stub();
extern "C" void nmi_stub();
extern "C" void bp_stub();
extern "C" void of_stub();
extern "C" void br_stub();
extern "C" void ud_stub();
extern "C" void nm_stub();
extern "C" void df_stub();
extern "C" void ts_stub();
extern "C" void np_stub();
extern "C" void ss_stub();
extern "C" void gp_stub();
extern "C" void pf_stub();
extern "C" void mf_stub();
extern "C" void ac_stub();
extern "C" void mc_stub();
extern "C" void xm_stub();
extern "C" void ve_stub();
extern "C" void cp_stub();

// CPU信息结构
class CpuId {
   public:
    // 获取CPU信息
    void GetInfo();
    // 检测CPU特性
    bool HasFeature(std::uint32_t feature_bit);
    // 检测扩展CPU特性
    bool HasExtFeature(std::uint32_t feature_bit);
    // 打印CPU信息
    void PrintInfo();

    char *GetVendorId();
    char *GetBrandString();
    std::uint32_t GetFamily();
    std::uint32_t GetModel();
    std::uint32_t GetStepping();

   private:
    void Init();
    bool initialized = false;

    char vendor_id[13];          // 厂商ID
    char brand_string[49];       // CPU品牌字符串
    std::uint32_t family;        // CPU系列
    std::uint32_t model;         // CPU型号
    std::uint32_t stepping;      // 步进
    std::uint32_t features;      // 特性标志
    std::uint32_t ext_features;  // 扩展特性标志
};

#endif /* ASM_FILE */

#endif /* INFO_KERNEL_CPU_H_ */