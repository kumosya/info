/* POSIX adapters share the descriptor table and keep errno conversion here. */
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/time.h>

static long syscall_result(long value) { if(value<0) { errno=-value; return -1; } return value; }
int sched_yield(void) { return syscall_result(os_syscall(OS_SYS_YIELD,0,0,0,0,0,0)); }
pid_t getpgid(pid_t pid) {
    OsTaskIdentifiers ids;
    long result = os_syscall(OS_SYS_GET_TASK_IDENTIFIERS,(long)&ids,pid,0,0,0,0);
    if(result<0) { errno=result==-4?ESRCH:EINVAL; return -1; }
    return (pid_t)ids.taskGroupId;
}
int setpgid(pid_t pid,pid_t group) {
    long result = os_syscall(OS_SYS_SET_TASK_GROUP,group,pid,0,0,0,0);
    if(result<0) { errno=result==-4?ESRCH:EPERM; return -1; }
    return 0;
}
int killpg(pid_t group,int signal) { return group<0 ? (errno=EINVAL,-1) : kill(-group,signal); }
int execv(const char* path,char* const arguments[]) { return execve(path,arguments,environ); }
int execvp(const char* file,char* const arguments[]) {
    if(!file || !*file) { errno=ENOENT; return -1; }
    if(strchr(file,'/')) return execv(file,arguments);
    const char* search=getenv("PATH"); if(!search) search="/bin:/usr/bin";
    int denied=0;
    do {
        const char* end=strchr(search,':'); size_t n=end?(size_t)(end-search):strlen(search);
        char path[LIBC_PATH_CAPACITY]; size_t length=strlen(file);
        if(n+length+2<=sizeof(path)) {
            if(n) { memcpy(path,search,n); path[n++]='/'; }
            memcpy(path+n,file,length+1); execv(path,arguments);
            if(errno==EACCES) denied=1;
            else if(errno!=ENOENT && errno!=ENOTDIR) return -1;
        }
        if(!end) break;
        search=end+1;
    } while(1);
    errno=denied?EACCES:ENOENT; return -1;
}
static int exec_arguments(const char* path,const char* first,va_list args,int search,int explicit_environment) {
    char* arguments[65]; size_t count=0;
    const char* value=first;
    while(value) {
        if(count==64) { errno=E2BIG; return -1; }
        arguments[count++]=(char*)value; value=va_arg(args,const char*);
    }
    arguments[count]=NULL;
    char* const* environment=explicit_environment?va_arg(args,char* const*):environ;
    return search?execvp(path,arguments):execve(path,arguments,environment);
}
int execl(const char* path,const char* first,...) { va_list a; va_start(a,first); int n=exec_arguments(path,first,a,0,0); va_end(a); return n; }
int execlp(const char* path,const char* first,...) { va_list a; va_start(a,first); int n=exec_arguments(path,first,a,1,0); va_end(a); return n; }
int execle(const char* path,const char* first,...) { va_list a; va_start(a,first); int n=exec_arguments(path,first,a,0,1); va_end(a); return n; }
pid_t _Fork(void) { return fork(); }
void _Exit(int status) { _exit(status); }
int system(const char* command) {
    if(!command) return access("/bin/sh",X_OK)==0;
    pid_t child=fork(); if(child<0) return -1;
    if(!child) { execl("/bin/sh","sh","-c",command,(char*)NULL); _exit(127); }
    int status;
    while(waitpid(child,&status,0)<0) if(errno!=EINTR) return -1;
    return status;
}
int libcPathAt(int directory,const char* path,char absolute[LIBC_PATH_CAPACITY]) {
    if(!path) { errno=EFAULT; return -1; }
    if(!*path) { errno=ENOENT; return -1; }
    if(strlen(path)>=LIBC_PATH_CAPACITY) { errno=ENAMETOOLONG; return -1; }
    if(path[0]=='/' || directory==AT_FDCWD) {
        if(path[0]!='/' && strlen(libcGetCurrentDirectory())+strlen(path)+2>LIBC_PATH_CAPACITY) { errno=ENAMETOOLONG; return -1; }
        libcCopyPath(absolute,path); return 0;
    }
    OpenDescription* descriptor=libcDescriptor(directory);
    if(!descriptor) { errno=EBADF; return -1; }
    struct stat status; if(fstat(directory,&status)) return -1;
    if(!S_ISDIR(status.st_mode)) { errno=ENOTDIR; return -1; }
    size_t parent=strlen(descriptor->path),child=strlen(path);
    if(parent+child+2>LIBC_PATH_CAPACITY) { errno=ENAMETOOLONG; return -1; }
    memcpy(absolute,descriptor->path,parent); absolute[parent++]='/'; memcpy(absolute+parent,path,child+1); return 0;
}
int openat(int directory,const char* path,int flags,...) {
    mode_t mode=0666;
    if(flags&O_CREAT) { va_list a; va_start(a,flags); mode=va_arg(a,mode_t); va_end(a); }
    char absolute[LIBC_PATH_CAPACITY]; if(libcPathAt(directory,path,absolute)) return -1;
    return open(absolute,flags,mode);
}
int mkdirat(int directory,const char* path,mode_t mode) { char absolute[LIBC_PATH_CAPACITY]; return libcPathAt(directory,path,absolute)?-1:mkdir(absolute,mode); }
int unlinkat(int directory,const char* path,int flags) {
    if(flags&~AT_REMOVEDIR) { errno=EINVAL; return -1; }
    char absolute[LIBC_PATH_CAPACITY]; if(libcPathAt(directory,path,absolute)) return -1;
    return flags&AT_REMOVEDIR?rmdir(absolute):unlink(absolute);
}
int renameat(int old_directory,const char* old_path,int new_directory,const char* new_path) {
    char source[LIBC_PATH_CAPACITY],target[LIBC_PATH_CAPACITY];
    if(libcPathAt(old_directory,old_path,source) || libcPathAt(new_directory,new_path,target)) return -1;
    return rename(source,target);
}
int faccessat(int directory,const char* path,int mode,int flags) {
    if(flags) { errno=ENOTSUP; return -1; }
    char absolute[LIBC_PATH_CAPACITY]; return libcPathAt(directory,path,absolute)?-1:access(absolute,mode);
}
int fchmodat(int directory,const char* path,mode_t mode,int flags) {
    if(flags) { errno=ENOTSUP; return -1; }
    char absolute[LIBC_PATH_CAPACITY]; return libcPathAt(directory,path,absolute)?-1:chmod(absolute,mode);
}
int fchownat(int directory,const char* path,uid_t uid,gid_t gid,int flags) {
    if(flags&~AT_SYMLINK_NOFOLLOW) { errno=EINVAL; return -1; }
    char absolute[LIBC_PATH_CAPACITY]; return libcPathAt(directory,path,absolute)?-1:chown(absolute,uid,gid);
}
int fchdir(int fd) {
    OpenDescription* description=libcDescriptor(fd); struct stat status;
    if(!description) { errno=EBADF; return -1; }
    if(fstat(fd,&status)) return -1;
    if(!S_ISDIR(status.st_mode)) { errno=ENOTDIR; return -1; }
    return chdir(description->path);
}
long fpathconf(int fd,int name) { OpenDescription* d=libcDescriptor(fd); if(!d) { errno=EBADF; return -1; } return pathconf(d->path,name); }
int lstat(const char* restrict path,struct stat* restrict status) { return stat(path,status); }
int futimens(int fd,const struct timespec times[2]) { OpenDescription* d=libcDescriptor(fd); if(!d) { errno=EBADF; return -1; } return utimensat(AT_FDCWD,d->path,times,0); }
int utimes(const char* path,const struct timeval times[2]) {
    if(!times) return utimensat(AT_FDCWD,path,NULL,0);
    struct timespec values[2]; for(int i=0;i<2;++i) {
        if(times[i].tv_usec<0 || times[i].tv_usec>=1000000) { errno=EINVAL; return -1; }
        values[i].tv_sec=times[i].tv_sec; values[i].tv_nsec=times[i].tv_usec*1000;
    }
    return utimensat(AT_FDCWD,path,values,0);
}
int gethostname(char* buffer,size_t size) {
    if(!buffer || !size) { errno=EINVAL; return -1; }
    int fd=open("/etc/hostname",O_RDONLY); if(fd<0) return -1;
    ssize_t length=read(fd,buffer,size); int error=errno; close(fd);
    if(length<0) { errno=error; return -1; }
    size_t n=0; while(n<(size_t)length && buffer[n]!='\n' && buffer[n]!='\r') ++n;
    if(n==size) { errno=ENAMETOOLONG; return -1; }
    buffer[n]=0; return 0;
}
int uname(struct utsname* result) {
    if(!result) { errno=EFAULT; return -1; }
    memset(result,0,sizeof(*result)); strcpy(result->sysname,"osinfo");
    strcpy(result->release,"0.2"); strcpy(result->version,"osinfo kernel"); strcpy(result->machine,"x86_64");
    if(gethostname(result->nodename,sizeof(result->nodename))) return -1;
    return 0;
}
