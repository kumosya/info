#ifndef _SCAN_INPUT_H
#define _SCAN_INPUT_H
#include <stddef.h>
#include <stdio.h>
#include <wchar.h>

#include <sys/types.h>
typedef struct {
    const unsigned char* position;
    const wchar_t* wide;
    FILE* stream;
    off_t count, limit;
    int exhausted, last;
} ScanInput;
static inline void sh_fromstring(ScanInput* f, const char* s) {
    f->position  = (const unsigned char*)s;
    f->wide      = NULL;
    f->stream    = NULL;
    f->count     = 0;
    f->limit     = 0;
    f->exhausted = 0;
}
static inline void shlim(ScanInput* f, off_t limit) {
    f->count     = 0;
    f->limit     = limit;
    f->exhausted = 0;
}
static inline off_t shcnt(ScanInput* f) { return f->count; }
static inline int shgetc(ScanInput* f) {
    if (f->exhausted || (f->limit && f->count >= f->limit)) {
        f->exhausted = 1;
        return EOF;
    }
    int c = f->stream ? fgetc(f->stream)
            : f->wide ? (*f->wide < 128 ? *f->wide : '@')
                      : *f->position;
    if (c == EOF || (!f->stream && !c)) {
        f->exhausted = 1;
        return EOF;
    }
    if (!f->stream) {
        if (f->wide)
            ++f->wide;
        else
            ++f->position;
    }
    ++f->count;
    f->last = c;
    return c;
}
static inline void shunget(ScanInput* f) {
    if (f->exhausted || !f->count) return;
    if (f->stream)
        ungetc(f->last, f->stream);
    else if (f->wide)
        --f->wide;
    else
        --f->position;
    --f->count;
}
#endif
