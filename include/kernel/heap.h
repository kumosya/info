#pragma once

#include <stddef.h>

namespace kernel {

void initKernelHeap();
void* kernelAllocate(size_t size);
void kernelFree(void* address);

}  // namespace kernel
