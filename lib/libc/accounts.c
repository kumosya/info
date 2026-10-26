#define _GNU_SOURCE 1
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static FILE *passwd_stream, *group_stream;
static char passwd_buffer[1024], group_buffer[1024];
static struct passwd passwd_value;
static struct group group_value;

static int fields(char* line,char** parts,int count) {
    size_t length=strcspn(line,"\r\n"); line[length]=0;
    if(!length || line[0]=='#') return 0;
    char* next=line;
    for(int i=0;i<count;++i) {
        parts[i]=next;
        char* delimiter=strchr(next,':');
        if(!delimiter) return i==count-1;
        *delimiter=0; next=delimiter+1;
    }
    return 1;
}
static int read_passwd(FILE* stream,struct passwd* value,char* buffer,size_t size) {
    if(size<2) return ERANGE;
    while(fgets(buffer,size,stream)) {
        size_t length=strlen(buffer);
        if(length==size-1 && buffer[length-1]!='\n') return ERANGE;
        char* p[7]; if(!fields(buffer,p,7)) continue;
        char *end; unsigned long uid=strtoul(p[2],&end,10); if(*end || uid>UINT32_MAX) continue;
        unsigned long gid=strtoul(p[3],&end,10); if(*end || gid>UINT32_MAX) continue;
        *value=(struct passwd){p[0],p[1],uid,gid,p[4],p[5],p[6]}; return 0;
    }
    return ferror(stream)?(errno?errno:EIO):ENOENT;
}
static int read_group(FILE* stream,struct group* value,char* buffer,size_t size) {
    if(size<2) return ERANGE;
    while(fgets(buffer,size,stream)) {
        size_t length=strlen(buffer);
        if(length==size-1 && buffer[length-1]!='\n') return ERANGE;
        char* p[4]; if(!fields(buffer,p,4)) continue;
        char* end; unsigned long gid=strtoul(p[2],&end,10); if(*end || gid>UINT32_MAX) continue;
        uintptr_t address=((uintptr_t)buffer+length+1+sizeof(char*)-1)&~(uintptr_t)(sizeof(char*)-1);
        size_t offset=address-(uintptr_t)buffer, count=*p[3]?1:0;
        for(char* c=p[3];*c;++c) if(*c==',') ++count;
        if(offset>size || count+1>(size-offset)/sizeof(char*)) return ERANGE;
        char** members=(char**)address; size_t used=0;
        if(*p[3]) { members[used++]=p[3]; for(char* c=p[3];*c;++c) if(*c==',') { *c=0; members[used++]=c+1; } }
        members[used]=NULL; *value=(struct group){p[0],p[1],gid,members}; return 0;
    }
    return ferror(stream)?(errno?errno:EIO):ENOENT;
}
void setpwent(void) { if(passwd_stream) rewind(passwd_stream); else passwd_stream=fopen("/etc/passwd","r"); }
void endpwent(void) { if(passwd_stream) fclose(passwd_stream); passwd_stream=NULL; }
struct passwd* getpwent(void) { if(!passwd_stream) setpwent(); if(!passwd_stream) return NULL; int e=read_passwd(passwd_stream,&passwd_value,passwd_buffer,sizeof(passwd_buffer)); if(e) { if(e!=ENOENT) errno=e; return NULL; } return &passwd_value; }
static int find_passwd(const char* name,uid_t uid,struct passwd* value,char* buffer,size_t size,struct passwd** result) {
    *result=NULL; FILE* file=fopen("/etc/passwd","r"); if(!file) return errno;
    int e;
    while(!(e=read_passwd(file,value,buffer,size))) if(name?!strcmp(name,value->pw_name):uid==value->pw_uid) { *result=value; break; }
    fclose(file); return e==ENOENT?0:e;
}
int getpwnam_r(const char* name,struct passwd* value,char* buffer,size_t size,struct passwd** result) { return find_passwd(name,0,value,buffer,size,result); }
int getpwuid_r(uid_t uid,struct passwd* value,char* buffer,size_t size,struct passwd** result) { return find_passwd(NULL,uid,value,buffer,size,result); }
struct passwd* getpwnam(const char* name) { struct passwd* result; int e=getpwnam_r(name,&passwd_value,passwd_buffer,sizeof(passwd_buffer),&result); if(e) errno=e; return e?NULL:result; }
struct passwd* getpwuid(uid_t uid) { struct passwd* result; int e=getpwuid_r(uid,&passwd_value,passwd_buffer,sizeof(passwd_buffer),&result); if(e) errno=e; return e?NULL:result; }
void setgrent(void) { if(group_stream) rewind(group_stream); else group_stream=fopen("/etc/group","r"); }
void endgrent(void) { if(group_stream) fclose(group_stream); group_stream=NULL; }
struct group* getgrent(void) { if(!group_stream) setgrent(); if(!group_stream) return NULL; int e=read_group(group_stream,&group_value,group_buffer,sizeof(group_buffer)); if(e) { if(e!=ENOENT) errno=e; return NULL; } return &group_value; }
static int find_group(const char* name,gid_t gid,struct group* value,char* buffer,size_t size,struct group** result) {
    *result=NULL; FILE* file=fopen("/etc/group","r"); if(!file) return errno;
    int e;
    while(!(e=read_group(file,value,buffer,size))) if(name?!strcmp(name,value->gr_name):gid==value->gr_gid) { *result=value; break; }
    fclose(file); return e==ENOENT?0:e;
}
int getgrnam_r(const char* name,struct group* value,char* buffer,size_t size,struct group** result) { return find_group(name,0,value,buffer,size,result); }
int getgrgid_r(gid_t gid,struct group* value,char* buffer,size_t size,struct group** result) { return find_group(NULL,gid,value,buffer,size,result); }
struct group* getgrnam(const char* name) { struct group* result; int e=getgrnam_r(name,&group_value,group_buffer,sizeof(group_buffer),&result); if(e) errno=e; return e?NULL:result; }
struct group* getgrgid(gid_t gid) { struct group* result; int e=getgrgid_r(gid,&group_value,group_buffer,sizeof(group_buffer),&result); if(e) errno=e; return e?NULL:result; }
int getlogin_r(char* buffer,size_t size) {
    struct passwd value,*found; char storage[1024];
    int error=getpwuid_r(getuid(),&value,storage,sizeof(storage),&found);
    if(error || !found) return error?error:ENOENT;
    size_t length=strlen(value.pw_name); if(length>=size) return ERANGE;
    memcpy(buffer,value.pw_name,length+1); return 0;
}
char* getlogin(void) { static char name[64]; int e=getlogin_r(name,sizeof(name)); if(e) { errno=e; return NULL; } return name; }
int getgrouplist(const char* name, gid_t base, gid_t* groups, int* capacity) {
    if (!name || !capacity || *capacity < 0 || base == (gid_t)-1) { errno = EINVAL; return -1; }
    gid_t values[32] = {base}; unsigned count = 1;
    FILE* file = fopen("/etc/group", "r"); if (!file) return -1;
    struct group group; char buffer[1024]; int error;
    while (!(error = read_group(file, &group, buffer, sizeof(buffer)))) {
        int member = 0;
        for (char** entry = group.gr_mem; entry && *entry; ++entry) if (!strcmp(*entry, name)) { member = 1; break; }
        if (!member) continue;
        unsigned i = 0; while (i < count && values[i] != group.gr_gid) ++i;
        if (i != count) continue;
        if (count == 32) { error = E2BIG; break; }
        values[count++] = group.gr_gid;
    }
    fclose(file);
    if (error != ENOENT) { errno = error; return -1; }
    const int size = *capacity; *capacity = count;
    if ((unsigned)size < count) { errno = ERANGE; return -1; }
    if (!groups) { errno = EFAULT; return -1; }
    memcpy(groups, values, count * sizeof(gid_t)); return count;
}
int initgroups(const char* name, gid_t base) {
    gid_t groups[32]; int count = 32;
    return getgrouplist(name, base, groups, &count) < 0 ? -1 : setgroups(count, groups);
}
