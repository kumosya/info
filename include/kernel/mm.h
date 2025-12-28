#ifndef MM_H
#define MM_H

#include "page.h"

#include <cstddef>
#include <cstdint>

namespace mm {

int Proc(int argc, char *argv[]);

namespace page {
/* in mm/page.cc */
extern FrameMem frame;
extern PTE *kernel_pml4;

void Map(PTE *pml4, std::uint64_t virt_addr, std::uint64_t phys_addr, std::uint64_t flags);
void *Alloc(std::size_t size);
void Free(void *addr);
}  // namespace page
namespace slab {
void Init();
}
}  // namespace mm

#endif /* MM_H */