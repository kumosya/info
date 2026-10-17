#include <setjmp.h>
#include <signal.h>
hidden int __sigsetjmp_tail(sigjmp_buf buffer, int result) {
    sigset_t *mask = (sigset_t*)buffer->__ss;
    sigprocmask(SIG_SETMASK, result ? mask : 0, result ? 0 : mask);
    return result;
}
_Noreturn void siglongjmp(sigjmp_buf buffer, int result) { longjmp(buffer, result); }
