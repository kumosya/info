#include <fenv.h>

int fegetexcept(void) {
    unsigned short control;
    __asm__ volatile("fnstcw %0" : "=m"(control));
    return ~control & FE_ALL_EXCEPT;
}
static int change_exceptions(int exceptions, int enabled) {
    unsigned short control;
    unsigned mxcsr;
    __asm__ volatile("fnstcw %0" : "=m"(control));
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    const int old = ~control & FE_ALL_EXCEPT;
    exceptions &= FE_ALL_EXCEPT;
    if (enabled) { control &= ~exceptions; mxcsr &= ~((unsigned)exceptions << 7); }
    else { control |= exceptions; mxcsr |= (unsigned)exceptions << 7; }
    __asm__ volatile("fldcw %0" : : "m"(control));
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    return old;
}
int feenableexcept(int exceptions) { return change_exceptions(exceptions, 1); }
int fedisableexcept(int exceptions) { return change_exceptions(exceptions, 0); }
