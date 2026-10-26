#include <errno.h>
#include <stdint.h>
#include "internal.h"

/* Kernel thread ids provide TLS without an ELF TLS segment requirement. */
static struct { uint64_t id; int value; } errors[128];
static int bootstrap_error;
void __libc_errno_fork_child(uint64_t previous) {
    const uint64_t current = __libc_os_get_thread_id();
    int value = bootstrap_error;
    for (unsigned i = 0; i < 128; ++i) {
        if (errors[i].id == previous) value = errors[i].value;
        errors[i].id = 0; errors[i].value = 0;
    }
    errors[0].value = value; errors[0].id = current;
}
int* __errno_location(void) {
    long thread = __libc_os_get_thread_id();
    if (thread <= 0) return &bootstrap_error;
    uint64_t id = (uint64_t)thread;
    for (unsigned i=0; i<128; ++i) {
        uint64_t current = __atomic_load_n(&errors[i].id,__ATOMIC_ACQUIRE);
        if (current == id) return &errors[i].value;
    }
    for (unsigned i=0; i<128; ++i) {
        uint64_t current=0;
        if (__atomic_compare_exchange_n(&errors[i].id,&current,id,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return &errors[i].value;
    }
    return &bootstrap_error;
}
void __libc_release_errno(void) {
    uint64_t id = __libc_os_get_thread_id();
    for (unsigned i=0;i<128;++i) if (__atomic_load_n(&errors[i].id,__ATOMIC_ACQUIRE)==id) {
        errors[i].value=0;
        __atomic_store_n(&errors[i].id,0,__ATOMIC_RELEASE);
        return;
    }
}
