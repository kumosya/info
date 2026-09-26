#include <new>
#include <stdlib.h>

void* operator new(size_t size) noexcept { return malloc(size); }

void* operator new[](size_t size) noexcept { return malloc(size); }

void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return malloc(size);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return malloc(size);
}

void operator delete(void* pointer) noexcept { free(pointer); }

void operator delete[](void* pointer) noexcept { free(pointer); }

void operator delete(void* pointer, size_t) noexcept { free(pointer); }

void operator delete[](void* pointer, size_t) noexcept { free(pointer); }

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    free(pointer);
}

namespace std {

const nothrow_t nothrow{};

}  // namespace std

extern "C" [[noreturn]] void __cxa_pure_virtual() { abort(); }
