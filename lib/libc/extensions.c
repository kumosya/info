#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

void* reallocarray(void* memory,size_t count,size_t size) {
    if(size && count>SIZE_MAX/size) { errno=ENOMEM; return NULL; }
    return realloc(memory,count*size);
}
char* secure_getenv(const char* name) { return getuid()==geteuid() && getgid()==getegid()?getenv(name):NULL; }
char* strerror_l(int error,locale_t locale) { (void)locale; return strerror(error); }
int strerror_r(int error,char* buffer,size_t size) {
    const char* message=strerror(error); size_t length=strlen(message);
    if(length>=size) { if(size) { memcpy(buffer,message,size-1); buffer[size-1]=0; } return ERANGE; }
    memcpy(buffer,message,length+1); return 0;
}
char* strsignal(int signal) {
    switch(signal) {
        case SIGHUP:return "Hangup"; case SIGINT:return "Interrupt"; case SIGQUIT:return "Quit";
        case SIGILL:return "Illegal instruction"; case SIGABRT:return "Aborted"; case SIGFPE:return "Floating point exception";
        case SIGKILL:return "Killed"; case SIGSEGV:return "Segmentation fault"; case SIGPIPE:return "Broken pipe";
        case SIGALRM:return "Alarm clock"; case SIGTERM:return "Terminated"; case SIGCHLD:return "Child status changed";
        case SIGCONT:return "Continued"; case SIGSTOP:return "Stopped"; case SIGTSTP:return "Stopped";
        default:return "Unknown signal";
    }
}
static void (*quick_handlers[16])(void);
static unsigned quick_count;
int at_quick_exit(void (*routine)(void)) { if(quick_count==16) return -1; quick_handlers[quick_count++]=routine; return 0; }
_Noreturn void quick_exit(int status) { while(quick_count) quick_handlers[--quick_count](); _Exit(status); }
char* __randname(char* output) {
    static uint64_t counter;
    struct timespec value={0}; clock_gettime(CLOCK_MONOTONIC,&value);
    uint64_t number=__atomic_fetch_add(&counter,1,__ATOMIC_RELAXED)^((uint64_t)value.tv_nsec<<16)^((uint64_t)getpid()<<32);
    static const char alphabet[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for(unsigned i=0;i<6;++i) { output[i]=alphabet[number%62]; number=number/62+0x9e3779b9U; }
    return output;
}
