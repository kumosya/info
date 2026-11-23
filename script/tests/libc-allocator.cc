/* 在受限页级分配记录下检查大量小对象、对齐、重分配及并发回收。 */
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" {
void* osinfo_test_malloc(size_t);
void osinfo_test_free(void*);
void* osinfo_test_calloc(size_t, size_t);
void* osinfo_test_realloc(void*, size_t);
int osinfo_test_posix_memalign(void**, size_t, size_t);
void __libc_allocator_fork_prepare(void);
void __libc_allocator_fork_parent(void);
void __libc_allocator_fork_child(void);
}

static std::atomic<size_t> activeRecords;
static std::atomic<size_t> allocationCalls;

/* 模拟运行库的页级接口，并保留原先的记录数量限制。 */
extern "C" void* __libc_os_allocate_memory(size_t size) {
    const size_t count = activeRecords.fetch_add(1);
    if (count >= 4096) {
        activeRecords.fetch_sub(1);
        return nullptr;
    }
    void* result = std::aligned_alloc(4096, (size + 4095) & ~size_t(4095));
    if (!result) {
        activeRecords.fetch_sub(1);
    } else {
        allocationCalls.fetch_add(1);
    }
    return result;
}

extern "C" int __libc_os_free_memory(void* pointer) {
    std::free(pointer);
    activeRecords.fetch_sub(1);
    return 0;
}

extern "C" int __libc_os_wait_address(volatile uint32_t*, uint32_t) {
    std::this_thread::yield();
    return 0;
}

extern "C" int __libc_os_wake_address(volatile uint32_t*, uint32_t) {
    return 0;
}

/* 不同线程交错使用尺寸组，验证空闲块不会同时交给两个申请。 */
static void exerciseThread(unsigned seed) {
    for (size_t index = 0; index < 5000; ++index) {
        const size_t size = (index * 37 + seed) % 4000 + 1;
        unsigned char* value = static_cast<unsigned char*>(osinfo_test_malloc(size));
        assert(value && reinterpret_cast<uintptr_t>(value) % alignof(std::max_align_t) == 0);
        std::memset(value, seed, size);
        assert(value[size - 1] == seed);
        osinfo_test_free(value);
    }
}

int main() {
    std::vector<unsigned char*> objects;
    for (size_t index = 0; index < 25000; ++index) {
        const size_t size = index % 249 + 1;
        unsigned char* value = static_cast<unsigned char*>(osinfo_test_malloc(size));
        assert(value && reinterpret_cast<uintptr_t>(value) % alignof(std::max_align_t) == 0);
        std::memset(value, index % 251, size);
        objects.push_back(value);
    }
    assert(activeRecords.load() < 256);
    for (size_t index = 0; index < objects.size(); ++index) {
        assert(objects[index][index % 249] == index % 251);
        osinfo_test_free(objects[index]);
    }
    assert(activeRecords.load() <= 8);

    unsigned char* value = static_cast<unsigned char*>(osinfo_test_calloc(137, 1));
    assert(value && value[136] == 0);
    std::memset(value, 42, 137);
    value = static_cast<unsigned char*>(osinfo_test_realloc(value, 70000));
    assert(value && value[136] == 42);
    value = static_cast<unsigned char*>(osinfo_test_realloc(value, 64));
    assert(value && value[63] == 42);
    osinfo_test_free(value);

    void* aligned = nullptr;
    assert(!osinfo_test_posix_memalign(&aligned, 65536, 8192));
    assert(reinterpret_cast<uintptr_t>(aligned) % 65536 == 0);
    osinfo_test_free(aligned);

    std::thread first(exerciseThread, 1);
    std::thread second(exerciseThread, 2);
    std::thread third(exerciseThread, 3);
    first.join();
    second.join();
    third.join();
    __libc_allocator_fork_prepare();
    __libc_allocator_fork_parent();
    __libc_allocator_fork_prepare();
    __libc_allocator_fork_child();
    exerciseThread(4);
    assert(activeRecords.load() <= 8);
    assert(allocationCalls.load() < 512);
}
