/* Stateful C / UTF-8 conversion, including split multibyte input. */
#include <wchar.h>
#include <uchar.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

int mbsinit(const mbstate_t* state) { return !state || !state->__opaque1; }
size_t mbrtowc(wchar_t* restrict output,const char* restrict input,size_t size,mbstate_t* restrict state) {
    static mbstate_t internal;
    if (!state) state = &internal;
    if (!input) { input = ""; size = 1; output = NULL; }
    if (!size) return (size_t)-2;
    unsigned remaining = state->__opaque1 & 255, total = state->__opaque1 >> 8;
    uint32_t code = state->__opaque2;
    size_t consumed = 0;
    if (!remaining) {
        unsigned char c = input[consumed++];
        if (c < 128) { if (output) *output = c; return c ? 1 : 0; }
        if (MB_CUR_MAX == 1) goto invalid;
        if (c >= 0xc2 && c <= 0xdf) { code = c & 31; remaining = 1; total = 2; }
        else if (c >= 0xe0 && c <= 0xef) { code = c & 15; remaining = 2; total = 3; }
        else if (c >= 0xf0 && c <= 0xf4) { code = c & 7; remaining = 3; total = 4; }
        else goto invalid;
    }
    while (remaining && consumed < size) {
        unsigned char c = input[consumed++];
        if ((c & 0xc0) != 0x80) goto invalid;
        code = (code << 6) | (c & 63); --remaining;
    }
    if (remaining) { state->__opaque1 = (total << 8) | remaining; state->__opaque2 = code; return (size_t)-2; }
    if ((total == 2 && code < 0x80) || (total == 3 && code < 0x800) ||
        (total == 4 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) goto invalid;
    memset(state,0,sizeof(*state)); if (output) *output = (wchar_t)code; return consumed;
invalid:
    memset(state,0,sizeof(*state)); errno = EILSEQ; return (size_t)-1;
}
size_t mbrlen(const char* restrict s,size_t n,mbstate_t* restrict state) { return mbrtowc(NULL,s,n,state); }
size_t wcrtomb(char* restrict output,wchar_t character,mbstate_t* restrict state) {
    char discard[4]; uint32_t c = character;
    if (!output) { output = discard; c = 0; }
    if (state) memset(state,0,sizeof(*state));
    if (c < 128) { output[0] = c; return 1; }
    if (MB_CUR_MAX == 1 || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) { errno = EILSEQ; return (size_t)-1; }
    if (c < 0x800) { output[0] = 0xc0|(c>>6); output[1] = 0x80|(c&63); return 2; }
    if (c < 0x10000) { output[0] = 0xe0|(c>>12); output[1] = 0x80|((c>>6)&63); output[2] = 0x80|(c&63); return 3; }
    output[0] = 0xf0|(c>>18); output[1] = 0x80|((c>>12)&63); output[2] = 0x80|((c>>6)&63); output[3] = 0x80|(c&63); return 4;
}
size_t mbsrtowcs(wchar_t* restrict output,const char** restrict source,size_t size,mbstate_t* restrict state) {
    static mbstate_t internal; if (!state) state = &internal;
    mbstate_t copy = *state; mbstate_t* conversion = output ? state : &copy;
    const char* s = *source; size_t count = 0;
    while (!output || count < size) {
        wchar_t c; size_t bytes = mbrtowc(&c,s,strlen(s)+1,conversion);
        if (bytes == (size_t)-1 || bytes == (size_t)-2) { if (output) *source = s; return (size_t)-1; }
        if (output) output[count] = c;
        if (!c) { if (output) *source = NULL; return count; }
        s += bytes; ++count;
    }
    if (output) *source = s;
    return count;
}
size_t wcsrtombs(char* restrict output,const wchar_t** restrict source,size_t size,mbstate_t* restrict state) {
    const wchar_t* s = *source; size_t count = 0; char encoded[4];
    while (1) {
        size_t bytes = wcrtomb(encoded,*s,state);
        if (bytes == (size_t)-1) { if (output) *source = s; return bytes; }
        if (output && bytes > size-count) { *source = s; return count; }
        if (output) memcpy(output+count,encoded,bytes);
        if (!*s) { if (output) *source = NULL; return count; }
        count += bytes; ++s;
    }
}
int mblen(const char* s,size_t n) { return mbtowc(NULL,s,n); }
int mbtowc(wchar_t* restrict out,const char* restrict s,size_t n) {
    mbstate_t state = {0}; if (!s) return 0;
    size_t result = mbrtowc(out,s,n,&state);
    if (result == (size_t)-2) { errno = EILSEQ; return -1; }
    return result == (size_t)-1 ? -1 : (int)result;
}
int wctomb(char* s,wchar_t c) { return s ? (int)wcrtomb(s,c,NULL) : 0; }
size_t mbstowcs(wchar_t* restrict out,const char* restrict s,size_t n) { mbstate_t state={0}; const char* source=s; return mbsrtowcs(out,&source,n,&state); }
size_t wcstombs(char* restrict out,const wchar_t* restrict s,size_t n) { mbstate_t state={0}; const wchar_t* source=s; return wcsrtombs(out,&source,n,&state); }
wint_t btowc(int c) { return c >= 0 && c < 128 ? (wint_t)c : WEOF; }
int wctob(wint_t c) { return c < 128 ? (int)c : EOF; }
size_t mbrtoc32(char32_t* restrict out,const char* restrict s,size_t n,mbstate_t* restrict state) {
    wchar_t c; size_t result=mbrtowc(&c,s,n,state); if (out && result < (size_t)-2) *out=c; return result;
}
size_t c32rtomb(char* restrict out,char32_t c,mbstate_t* restrict state) { return wcrtomb(out,c,state); }
size_t mbrtoc16(char16_t* restrict out,const char* restrict s,size_t n,mbstate_t* restrict state) {
    static mbstate_t internal; if (!state) state=&internal;
    if (state->__opaque1 == 0x80000000U) {
        if (out) *out=state->__opaque2;
        memset(state,0,sizeof(*state)); return (size_t)-3;
    }
    wchar_t c; size_t result=mbrtowc(&c,s,n,state);
    if (result >= (size_t)-2) return result;
    if (c > 0xffff) {
        unsigned value=c-0x10000;
        if (out) *out=0xd800+(value>>10);
        state->__opaque1=0x80000000U; state->__opaque2=0xdc00+(value&1023);
    } else if (out) *out=c;
    return result;
}
size_t c16rtomb(char* restrict out,char16_t c,mbstate_t* restrict state) {
    static mbstate_t internal; if (!state) state=&internal;
    if (!out) { memset(state,0,sizeof(*state)); return 1; }
    if (state->__opaque1 == 0x40000000U) {
        unsigned high=state->__opaque2; memset(state,0,sizeof(*state));
        if (c<0xdc00 || c>0xdfff) { errno=EILSEQ; return (size_t)-1; }
        return wcrtomb(out,0x10000+((high-0xd800)<<10)+(c-0xdc00),state);
    }
    if (c>=0xd800 && c<=0xdbff) { state->__opaque1=0x40000000U; state->__opaque2=c; return 0; }
    return wcrtomb(out,c,state);
}
