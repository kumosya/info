#ifndef MM_H
#define MM_H

#include <page.h>
#include <cstddef>
#include <cstdint>

using namespace std;

namespace mm {
    namespace page {
        void *alloc(size_t size);
        void free(void *addr);
    } // namespace page
    namespace pool {
        void init();
    }
} // namespace mm

#endif /* MM_H */