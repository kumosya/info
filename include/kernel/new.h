#pragma once

#include <stddef.h>

inline void* operator new(size_t, void* address) noexcept { return address; }
inline void operator delete(void*, void*) noexcept {}
