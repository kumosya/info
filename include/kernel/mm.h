#ifndef INFO_KERNEL_MM_H_
#define INFO_KERNEL_MM_H_

#include <cstddef>
#include <cstdint>

#include "kernel/page.h"

namespace mm {

int Service(int argc, char *argv[]);

inline static std::uint64_t Vir2Phy(std::uint64_t virt) {
    return virt - IDENTITY_BASE;
}

inline static std::uint64_t Phy2Vir(std::uint64_t phy) {
    return phy + IDENTITY_BASE;
}


namespace page {
/* in mm/page.cc */
extern FrameMem frame;
extern PTE *kernel_pml4;

void Map(PTE *pml4, std::uint64_t virt_addr, std::uint64_t phys_addr,
         std::uint64_t flags);
void *Alloc(std::size_t size);
void Free(void *addr);
void UpdateKernelPml4(PTE *user_pml4);

std::uint64_t AnalyzePageTable(PTE* pml4, std::uint64_t virt_addr);
}  // namespace page
namespace slab {
void Init();
}
}  // namespace mm

#endif /* INFO_KERNEL_MM_H_ */