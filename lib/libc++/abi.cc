#include <cstdlib>

namespace {

struct DestructorEntry {
    void (*destructor)(void*);
    void* object;
};

constexpr size_t destructorCapacity = 32;
DestructorEntry destructors[destructorCapacity];
size_t destructorCount;

}  // namespace

extern "C" {

void* __dso_handle = &__dso_handle;

int __cxa_atexit(void (*destructor)(void*), void* object, void*) {
    if (destructor == nullptr || destructorCount == destructorCapacity) {
        return -1;
    }
    destructors[destructorCount++] = {destructor, object};
    return 0;
}

void __cxa_finalize(void*) {
    while (destructorCount != 0) {
        DestructorEntry& entry = destructors[--destructorCount];
        entry.destructor(entry.object);
    }
}

[[noreturn]] void __cxa_deleted_virtual() { abort(); }

}  // extern "C"
