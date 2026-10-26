/* BSD random state format: header = generator type + 5 * rear index. */
#define _GNU_SOURCE 1
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "lock.h"

static const unsigned degrees[] = {1, 7, 15, 31, 63};
static const unsigned separations[] = {0, 3, 1, 3, 1};
static char default_state[128];
static char* active = default_state;
static unsigned type = 3, rear, front = 3;
static int initialized;
static volatile uint32_t random_lock;
void __libc_random_fork_prepare(void) { libcWaitLock(&random_lock); }
void __libc_random_fork_parent(void) { libcWaitUnlock(&random_lock); }
void __libc_random_fork_child(void) { random_lock = 0; }

static uint32_t load(unsigned index) { uint32_t word; memcpy(&word, active + 4 + index * 4, 4); return word; }
static void store(unsigned index, uint32_t word) { memcpy(active + 4 + index * 4, &word, 4); }
static void save(void) { uint32_t header = type + 5 * rear; memcpy(active, &header, 4); }
static long next(void) {
    if (!type) { uint32_t word = (load(0) * 1103515245U + 12345U) & 0x7fffffffU; store(0, word); return word; }
    uint32_t sum = load(front) + load(rear); store(front, sum);
    front = (front + 1) % degrees[type]; rear = (rear + 1) % degrees[type];
    return sum >> 1;
}
static void seed(unsigned value) {
    if (!value) value = 1;
    store(0, value); rear = 0; front = separations[type];
    if (type) {
        int64_t word = (int32_t)value;
        for (unsigned i = 1; i < degrees[type]; ++i) {
            word = (16807 * word) % 2147483647;
            if (word < 0) word += 2147483647;
            store(i, (uint32_t)word);
        }
        for (unsigned i = 0; i < 10 * degrees[type]; ++i) (void)next();
    }
    save(); initialized = 1;
}
long random(void) {
    libcWaitLock(&random_lock); if (!initialized) seed(1);
    long result = next(); save(); libcWaitUnlock(&random_lock); return result;
}
void srandom(unsigned value) {
    libcWaitLock(&random_lock); seed(value); libcWaitUnlock(&random_lock);
}
char* initstate(unsigned value, char* state, size_t size) {
    if (!state || size < 8) { errno = EINVAL; return NULL; }
    unsigned selected = size >= 256 ? 4 : size >= 128 ? 3 : size >= 64 ? 2 : size >= 32 ? 1 : 0;
    libcWaitLock(&random_lock); if (!initialized) seed(1); save();
    char* previous = active; active = state; type = selected; seed(value);
    libcWaitUnlock(&random_lock); return previous;
}
char* setstate(char* state) {
    if (!state) { errno = EINVAL; return NULL; }
    libcWaitLock(&random_lock); if (!initialized) seed(1);
    uint32_t header; memcpy(&header, state, 4);
    unsigned selected = header % 5, index = header / 5;
    if (index >= degrees[selected] || (!selected && index)) {
        libcWaitUnlock(&random_lock); errno = EINVAL; return NULL;
    }
    save(); char* previous = active;
    if (state != active) {
        active = state; type = selected; rear = index; front = (rear + separations[type]) % degrees[type];
    }
    libcWaitUnlock(&random_lock); return previous;
}
