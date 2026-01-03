#include <cstdint>
#include <cstring>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/tty.h"

// 获取CPU信息
void CpuId::GetInfo() {
    std::uint32_t eax, ebx, ecx, edx;

    // 获取厂商ID (CPUID功能0)
    cpuid(0, eax, ebx, ecx, edx);

    // 拼接厂商ID字符串
    *(std::uint32_t *)&vendor_id[0] = ebx;
    *(std::uint32_t *)&vendor_id[4] = edx;
    *(std::uint32_t *)&vendor_id[8] = ecx;
    vendor_id[12]                   = '\0';

    // 获取CPU品牌字符串 (CPUID功能0x80000002-0x80000004)
    char brand_str[48] = {0};

    cpuid(0x80000002, eax, ebx, ecx, edx);
    *(std::uint32_t *)&brand_str[0]  = eax;
    *(std::uint32_t *)&brand_str[4]  = ebx;
    *(std::uint32_t *)&brand_str[8]  = ecx;
    *(std::uint32_t *)&brand_str[12] = edx;

    cpuid(0x80000003, eax, ebx, ecx, edx);
    *(std::uint32_t *)&brand_str[16] = eax;
    *(std::uint32_t *)&brand_str[20] = ebx;
    *(std::uint32_t *)&brand_str[24] = ecx;
    *(std::uint32_t *)&brand_str[28] = edx;

    cpuid(0x80000004, eax, ebx, ecx, edx);
    *(std::uint32_t *)&brand_str[32] = eax;
    *(std::uint32_t *)&brand_str[36] = ebx;
    *(std::uint32_t *)&brand_str[40] = ecx;
    *(std::uint32_t *)&brand_str[44] = edx;

    // 复制品牌字符串到info结构
    strncpy(brand_string, brand_str, sizeof(brand_string) - 1);
    brand_string[sizeof(brand_string) - 1] = '\0';

    // 获取CPU特性 (CPUID功能1)
    cpuid(1, eax, ebx, ecx, edx);

    // 提取系列、型号和步进
    stepping = eax & 0xF;
    model    = (eax >> 4) & 0xF;
    family   = (eax >> 8) & 0xF;

    // 处理扩展系列和型号
    if (family == 6 || family == 15) {
        model |= ((eax >> 16) & 0xF) << 4;
        family += (eax >> 20) & 0xFF;
    }

    // 保存特性标志
    features     = edx;
    ext_features = ecx;
}

// 检测CPU特性
bool CpuId::HasFeature(std::uint32_t feature_bit) {
    return (features & (1 << feature_bit)) != 0;
}

// 检测扩展CPU特性
bool CpuId::HasExtFeature(std::uint32_t feature_bit) {
    return (ext_features & (1 << feature_bit)) != 0;
}

// 打印CPU信息
void CpuId::PrintInfo() {
    if (!initialized) {
        Init();
    }

    tty::printf("CPU Information:\n");
    tty::printf("Vendor: %s\n", vendor_id);
    tty::printf("Brand: %s\n", brand_string);
    tty::printf("Family: %d, Model: %d, Stepping: %d\n", family, model,
                stepping);

    tty::printf("Features: ");
    // 打印一些常见特性
    if (HasFeature(3)) tty::printf("PSE ");       // Page Size Extension
    if (HasFeature(6)) tty::printf("PAE ");       // Physical Address Extension
    if (HasFeature(23)) tty::printf("MMX ");      // MMX
    if (HasFeature(25)) tty::printf("SSE ");      // SSE
    if (HasFeature(26)) tty::printf("SSE2 ");     // SSE2
    if (HasExtFeature(0)) tty::printf("SSE3 ");   // SSE3
    if (HasExtFeature(9)) tty::printf("SSSE3 ");  // SSSE3
    if (HasExtFeature(19)) tty::printf("SSE4.1 ");  // SSE4.1
    if (HasExtFeature(20)) tty::printf("SSE4.2 ");  // SSE4.2
    tty::printf("\n");
}

// 获取厂商ID
char *CpuId::GetVendorId() { return vendor_id; }

// 获取CPU品牌字符串
char *CpuId::GetBrandString() { return brand_string; }

// 获取CPU系列
std::uint32_t CpuId::GetFamily() { return family; }

// 获取CPU型号
std::uint32_t CpuId::GetModel() { return model; }

// 获取CPU步进
std::uint32_t CpuId::GetStepping() { return stepping; }

// 初始化CPU信息
void CpuId::Init() {
    GetInfo();
    initialized = true;
}
