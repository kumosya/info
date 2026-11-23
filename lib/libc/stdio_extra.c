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
#include "stdio_internal.h"

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
int getchar_unlocked(void) { if (!stdin->orientation) stdin->orientation = -1; return __libc_getc_unlocked(stdin); }
int getc_unlocked(FILE* stream) { if (!stream->orientation) stream->orientation = -1; return __libc_getc_unlocked(stream); }
int putchar_unlocked(int c) { if (!stdout->orientation) stdout->orientation = -1; return __libc_putc_unlocked(c,stdout); }
int putc_unlocked(int c,FILE* stream) { if (!stream->orientation) stream->orientation = -1; return __libc_putc_unlocked(c,stream); }
int fseek(FILE* stream,long offset,int whence) { return fseeko(stream,offset,whence); }
long ftell(FILE* stream) { return ftello(stream); }
int fgetpos(FILE* restrict stream,fpos_t* restrict result) { off_t p=ftello(stream); if(p<0) return -1; *result=p; return 0; }
int fsetpos(FILE* stream,const fpos_t* position) { return fseeko(stream,*position,SEEK_SET); }
void rewind(FILE* stream) { fseeko(stream,0,SEEK_SET); clearerr(stream); }
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
int fwide(FILE* stream,int mode) {
    if (!mode || stream->orientation) return stream->orientation;
    if (!__libc_stream_state(stream)) return 0;
    flockfile(stream);
    if (!stream->orientation) stream->orientation = mode > 0 ? 1 : -1;
    int orientation = stream->orientation;
    funlockfile(stream);
    return orientation;
}
/* ????????????????????? UTF-8 ????? */
wint_t fgetwc(FILE* stream)
{
    FileState* file = __libc_stream_state(stream);
    if (!file) return WEOF;
    flockfile(stream);
    if (stream->orientation < 0)
    {
        funlockfile(stream);
        errno = EILSEQ;
        return WEOF;
    }
    stream->orientation = 1;
    wint_t result = WEOF;
    if (file->wide_pushed != WEOF)
    {
        result = file->wide_pushed;
        file->wide_pushed = WEOF;
    }
    else if (file->memory_kind == 5)
    {
        if (stream->pushed != EOF)
        {
            result = stream->pushed == 0x80 && file->numeric_pushed != WEOF ?
                file->numeric_pushed : (wint_t)stream->pushed;
            file->numeric_pushed = WEOF;
            stream->pushed = EOF;
        }
        else if (file->position < file->length) result = ((wchar_t*)file->memory)[file->position++];
        else stream->endOfFile = 1;
    }
    else
    {
        mbstate_t state = {0};
        wchar_t value;
        unsigned used = 0;
        for (;;)
        {
            int byte = __libc_getc_unlocked(stream);
            if (byte == EOF)
            {
                if (used) { errno = EILSEQ; stream->error = 1; }
                break;
            }
            ++used;
            char input = (char)byte;
            size_t count = mbrtowc(&value, &input, 1, &state);
            if (count == (size_t)-2) continue;
            if (count == (size_t)-1) stream->error = 1;
            else result = (wint_t)value;
            break;
        }
    }
    funlockfile(stream);
    return result;
}

wint_t ungetwc(wint_t value, FILE* stream)
{
    FileState* file = __libc_stream_state(stream);
    if (!file || value == WEOF) return WEOF;
    flockfile(stream);
    wint_t result = WEOF;
    if (stream->orientation >= 0 && file->wide_pushed == WEOF)
    {
        stream->orientation = 1;
        if (value < 128) result = ungetc((int)value, stream) == EOF ? WEOF : value;
        else
        {
            file->wide_pushed = value;
            stream->endOfFile = 0;
            result = value;
        }
    }
    funlockfile(stream);
    return result;
}

wint_t getwc(FILE* stream) { return fgetwc(stream); }
wint_t getwchar(void) { return fgetwc(stdin); }
/* ?????? UTF-8?????????? wchar_t? */
wint_t fputwc(wchar_t value, FILE* stream)
{
    FileState* state = __libc_stream_state(stream);
    if (!state) return WEOF;
    flockfile(stream);
    wint_t result = WEOF;
    if (stream->orientation >= 0)
    {
        stream->orientation = 1;
        if (state->memory_kind == 3 || state->memory_kind == 4)
        {
            if (!__libc_stream_write_wchar(state, value)) result = (wint_t)value;
        }
        else
        {
            char bytes[4];
            mbstate_t conversion = {0};
            size_t count = wcrtomb(bytes, value, &conversion);
            if (count != (size_t)-1 && fwrite(bytes, 1, count, stream) == count) result = (wint_t)value;
            else stream->error = 1;
        }
    }
    else errno = EILSEQ;
    funlockfile(stream);
    return result;
}
wint_t putwc(wchar_t c,FILE* stream) { return fputwc(c,stream); }
wint_t putwchar(wchar_t c) { return fputwc(c,stdout); }
int fputws(const wchar_t* restrict s,FILE* restrict stream) {
    if (fwide(stream, 1) < 0) { errno = EILSEQ; return -1; }
    flockfile(stream);
    int result = 0;
    while (*s) if (fputwc(*s++, stream) == WEOF) { result = -1; break; }
    funlockfile(stream);
    return result;
}
wchar_t* fgetws(wchar_t* restrict s,int n,FILE* restrict stream) {
    if (n <= 0 || fwide(stream, 1) < 0) return NULL;
    flockfile(stream);
    int used = 0;
    while (used < n - 1) {
        wint_t value = fgetwc(stream);
        if (value == WEOF) break;
        s[used++] = (wchar_t)value;
        if (value == '\n') break;
    }
    if (used || n == 1) s[used] = 0;
    funlockfile(stream);
    return used || n == 1 ? s : NULL;
}
