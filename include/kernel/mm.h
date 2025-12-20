#ifndef MM_H
#define MM_H

#include <cstddef>
#include <cstdint>
#include <page.h>

using namespace std;

namespace mm {
namespace page {
/* in mm/page.cc */
extern frame_mem frame;
extern pt_entry *kernel_pml4;

void map(pt_entry *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void *alloc(size_t size);
void free(void *addr);
} // namespace page
namespace slab {
void init();
}
} // namespace mm

#endif /* MM_H */