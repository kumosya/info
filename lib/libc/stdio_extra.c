#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <wchar.h>
#include <sys/stat.h>

int vasprintf(char** restrict output,const char* restrict format,va_list args) {
    va_list copy; va_copy(copy,args);
    int length=vsnprintf(NULL,0,format,copy); va_end(copy);
    if (length<0) return -1;
    char* buffer=malloc((size_t)length+1);
    if (!buffer) return -1;
    va_copy(copy,args); int result=vsnprintf(buffer,(size_t)length+1,format,copy); va_end(copy);
    if (result<0) { free(buffer); return -1; }
    *output=buffer; return result;
}
int asprintf(char** restrict out,const char* restrict format,...) {
    va_list args; va_start(args,format); int result=vasprintf(out,format,args); va_end(args); return result;
}
int vfprintf(FILE* restrict stream,const char* restrict format,va_list args) {
    if (!stream) { errno=EINVAL; return -1; }
    char* buffer; int length=vasprintf(&buffer,format,args);
    if (length<0) return -1;
    size_t sent=fwrite(buffer,1,length,stream); free(buffer);
    return sent==(size_t)length ? length : -1;
}
int vprintf(const char* restrict format,va_list args) { return vfprintf(stdout,format,args); }
int vdprintf(int fd,const char* restrict format,va_list args) {
    char* buffer; int length=vasprintf(&buffer,format,args); if (length<0) return -1;
    size_t sent=0;
    while (sent<(size_t)length) { ssize_t n=write(fd,buffer+sent,length-sent); if(n<=0) { free(buffer); return -1; } sent+=n; }
    free(buffer); return length;
}
int dprintf(int fd,const char* restrict format,...) { va_list args; va_start(args,format); int n=vdprintf(fd,format,args); va_end(args); return n; }
int vsprintf(char* restrict out,const char* restrict format,va_list args) { return vsnprintf(out,(size_t)INT_MAX,format,args); }
int sprintf(char* restrict out,const char* restrict format,...) { va_list args; va_start(args,format); int n=vsprintf(out,format,args); va_end(args); return n; }
int getc(FILE* stream) { return fgetc(stream); }
int putc(int c,FILE* stream) { return fputc(c,stream); }
int getchar_unlocked(void) { return fgetc(stdin); }
int getc_unlocked(FILE* stream) { return fgetc(stream); }
int putchar_unlocked(int c) { return fputc(c,stdout); }
int putc_unlocked(int c,FILE* stream) { return fputc(c,stream); }
int ungetc(int c,FILE* stream) {
    if (!stream || c==EOF || stream->pushed!=EOF) return EOF;
    stream->pushed=(unsigned char)c; stream->endOfFile=0; return stream->pushed;
}
int fseeko(FILE* stream,off_t offset,int whence) {
    if (!stream) { errno=EINVAL; return -1; }
    if (whence==SEEK_CUR && stream->pushed!=EOF) --offset;
    if (lseek(fileno(stream),offset,whence)<0) return -1;
    stream->pushed=EOF; stream->endOfFile=0; return 0;
}
int fseek(FILE* stream,long offset,int whence) { return fseeko(stream,offset,whence); }
off_t ftello(FILE* stream) { if (!stream) { errno=EINVAL; return -1; } off_t n=lseek(fileno(stream),0,SEEK_CUR); return n>=0 && stream->pushed!=EOF ? n-1 : n; }
long ftell(FILE* stream) { return ftello(stream); }
int fgetpos(FILE* restrict stream,fpos_t* restrict result) { off_t p=ftello(stream); if(p<0) return -1; *result=p; return 0; }
int fsetpos(FILE* stream,const fpos_t* position) { return fseeko(stream,*position,SEEK_SET); }
void rewind(FILE* stream) { fseeko(stream,0,SEEK_SET); clearerr(stream); }
FILE* fdopen(int fd,const char* mode) {
    if (fcntl(fd,F_GETFL)<0) return NULL;
    if (!mode || !*mode || !strchr("rwa",*mode)) { errno=EINVAL; return NULL; }
    FILE* stream=calloc(1,sizeof(*stream)); if(!stream) return NULL;
    stream->fileDescriptor=fd; stream->owned=1; stream->pushed=EOF;
    return stream;
}
FILE* freopen(const char* restrict path,const char* restrict mode,FILE* restrict stream) {
    if (!stream) { errno=EINVAL; return NULL; }
    if (!path) { errno=ENOSYS; return NULL; }
    FILE* fresh=fopen(path,mode); if(!fresh) return NULL;
    close(stream->fileDescriptor); *stream=*fresh; free(fresh); return stream;
}
ssize_t getdelim(char** restrict line,size_t* restrict capacity,int delimiter,FILE* restrict stream) {
    if(!line || !capacity || !stream) { errno=EINVAL; return -1; }
    if(!*line) { *capacity=128; *line=malloc(*capacity); if(!*line) return -1; }
    size_t length=0;
    while(1) {
        int c=fgetc(stream); if(c==EOF) { if(!length) return -1; break; }
        if(length+1>=*capacity) { size_t n=*capacity>1?*capacity*2:128; if(n<=*capacity || n>(size_t)SSIZE_MAX) { errno=EOVERFLOW; return -1; } char* p=realloc(*line,n); if(!p) return -1; *line=p; *capacity=n; }
        (*line)[length++]=c; if(c==(unsigned char)delimiter) break;
    }
    (*line)[length]=0; return length;
}
ssize_t getline(char** restrict line,size_t* restrict size,FILE* restrict stream) { return getdelim(line,size,'\n',stream); }
int remove(const char* path) { struct stat value; if(lstat(path,&value)) return -1; return S_ISDIR(value.st_mode)?rmdir(path):unlink(path); }
int fwide(FILE* stream,int mode) { if(!stream->orientation && mode) stream->orientation=mode>0?1:-1; return stream->orientation; }
wint_t fgetwc(FILE* stream) {
    mbstate_t state={0}; wchar_t wc;
    while(1) { int c=fgetc(stream); if(c==EOF) return WEOF; char byte=c; size_t n=mbrtowc(&wc,&byte,1,&state); if(n==(size_t)-2) continue; if(n==(size_t)-1) { stream->error=1; return WEOF; } return wc; }
}
wint_t getwc(FILE* stream) { return fgetwc(stream); }
wint_t getwchar(void) { return fgetwc(stdin); }
wint_t fputwc(wchar_t c,FILE* stream) { char bytes[4]; size_t n=wcrtomb(bytes,c,NULL); if(n==(size_t)-1 || fwrite(bytes,1,n,stream)!=n) return WEOF; return c; }
wint_t putwc(wchar_t c,FILE* stream) { return fputwc(c,stream); }
wint_t putwchar(wchar_t c) { return fputwc(c,stdout); }
int fputws(const wchar_t* restrict s,FILE* restrict stream) { while(*s) if(fputwc(*s++,stream)==WEOF) return -1; return 0; }
wchar_t* fgetws(wchar_t* restrict s,int n,FILE* restrict stream) { if(n<=0) return NULL; int i=0; while(i<n-1) { wint_t c=fgetwc(stream); if(c==WEOF) break; s[i++]=c; if(c=='\n') break; } s[i]=0; return i?s:NULL; }
