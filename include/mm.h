#ifndef MM_H
#define MM_H

#include <page.h>
#include <stddef.h>
#include <stdint.h>

namespace mm {
    namespace page {
        void* alloc(size_t size);
        void free(void* addr);
    }
    namespace pool {
        void init();
    }
}

#endif /* MM_H */