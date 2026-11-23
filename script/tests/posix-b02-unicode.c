#define _GNU_SOURCE 1
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static size_t locale_width=4;
static size_t scans;
size_t __ctype_get_mb_cur_max(void) {return locale_width;}
size_t fixture_strlen(const char* text) {++scans;const char* end=text;while(*end)++end;return (size_t)(end-text);}

static void invalid_prefixes(void)
{
    const char* invalid[]={"\xe0\x80","\xed\xa0","\xf0\x80","\xf4\x90"};
    for(size_t i=0;i<4;++i){
        mbstate_t state={0};wchar_t value=123;errno=0;
        assert(mbrtowc(&value,invalid[i],2,&state)==(size_t)-1);
        assert(errno==EILSEQ && value==123 && mbsinit(&state));
        assert(mbrtowc(&value,invalid[i],1,&state)==(size_t)-2);
        errno=0;assert(mbrtowc(&value,invalid[i]+1,1,&state)==(size_t)-1);
        assert(errno==EILSEQ && value==123);
    }
}

static void split_and_count(void)
{
    const char text[]="A\xe4\xb8\xad\xf0\x9f\x98\x80";
    const char* source=text;wchar_t output[5]={99,99,99,99,99};mbstate_t state={0};
    assert(mbsnrtowcs(output,&source,3,4,&state)==1);
    assert(source==text+3 && output[0]=='A' && output[1]==99 && !mbsinit(&state));
    mbstate_t saved=state;const char* counted=source;
    assert(mbsnrtowcs(NULL,&source,6,0,&state)==2);
    assert(source==counted && !memcmp(&saved,&state,sizeof(state)));
    assert(mbsrtowcs(output+1,&source,4,&state)==2 && !source);
    assert(output[1]==0x4e2d && output[2]==0x1f600 && !output[3] && output[4]==99);

    const char invalid[]="A\xed\xa0";source=invalid;memset(&state,0,sizeof(state));
    assert(mbsnrtowcs(output,&source,3,4,&state)==(size_t)-1 && source==invalid+1);
    const wchar_t bad[]={0xd800,0};const wchar_t* wide=bad;char bytes[8];memset(bytes,0x5a,sizeof(bytes));errno=0;
    assert(wcsrtombs(bytes,&wide,0,&state)==0 && wide==bad && bytes[0]==0x5a && errno==0);
    assert(wcsrtombs(bytes,&wide,8,&state)==(size_t)-1 && wide==bad && errno==EILSEQ);

    const wchar_t valid[]={0x4e2d,0x1f600,0};wide=valid;memset(&state,0,sizeof(state));saved=state;
    assert(wcsrtombs(NULL,&wide,0,&state)==7 && wide==valid && !memcmp(&saved,&state,sizeof(state)));
    assert(wcsnrtombs(bytes,&wide,2,2,&state)==0 && wide==valid && bytes[0]==0x5a);
    assert(wcsnrtombs(bytes,&wide,2,3,&state)==3 && wide==valid+1);
    assert(wcsrtombs(bytes+3,&wide,5,&state)==4 && !wide && !bytes[7]);
    assert(!memcmp(bytes,"\xe4\xb8\xad\xf0\x9f\x98\x80",7));

    /* ps=NULL 时，各接口保留自己的内部状态，不能串用另一接口的半字符。 */
    const char lead[]={ (char)0xe4 };source=lead;
    assert(mbsnrtowcs(output,&source,1,1,NULL)==0 && source==lead+1);
    source="Z";assert(mbsrtowcs(output,&source,2,NULL)==1 && !source && output[0]=='Z');
    source="\xb8\xad";assert(mbsnrtowcs(output,&source,2,1,NULL)==1 && output[0]==0x4e2d);
}

static void scalars_and_limits(void)
{
    /* 独立的标准编码向量，防止编码器和解码器以同一种错误互相抵消。 */
    static const struct {uint32_t code;const char* bytes;size_t count;} vectors[]={
        {0x7f,"\x7f",1},{0x80,"\xc2\x80",2},{0x7ff,"\xdf\xbf",2},
        {0x800,"\xe0\xa0\x80",3},{0xd7ff,"\xed\x9f\xbf",3},
        {0xe000,"\xee\x80\x80",3},{0xffff,"\xef\xbf\xbf",3},
        {0x10000,"\xf0\x90\x80\x80",4},{0x10ffff,"\xf4\x8f\xbf\xbf",4}
    };
    for(size_t i=0;i<sizeof(vectors)/sizeof(vectors[0]);++i){
        char bytes[4];mbstate_t state={0};wchar_t decoded=0;
        assert(wcrtomb(bytes,(wchar_t)vectors[i].code,&state)==vectors[i].count);
        assert(!memcmp(bytes,vectors[i].bytes,vectors[i].count));
        for(size_t split=1;split<vectors[i].count;++split){
            memset(&state,0,sizeof(state));
            assert(mbrtowc(&decoded,vectors[i].bytes,split,&state)==(size_t)-2);
            assert(mbrtowc(&decoded,vectors[i].bytes+split,vectors[i].count-split,&state)==vectors[i].count-split);
            assert(decoded==(wchar_t)vectors[i].code);
        }
    }
    for(uint32_t code=0;code<=0x10ffff;code+=97){
        if(code>=0xd800 && code<=0xdfff)continue;
        char bytes[4];wchar_t decoded=-1;mbstate_t encoding={0},decoding={0};
        const size_t count=wcrtomb(bytes,(wchar_t)code,&encoding);
        assert(count>=1 && count<=4);
        assert(mbrtowc(&decoded,bytes,count,&decoding)==(code?count:0));
        assert(decoded==(wchar_t)code && mbsinit(&decoding));
    }
    char text[8193];wchar_t output[8193];memset(text,'x',8192);text[8192]=0;
    const char* source=text;mbstate_t state={0};scans=0;
    assert(mbsrtowcs(output,&source,8193,&state)==8192 && !source && !output[8192]);
    assert(scans==0); /* 长字符串转换不应逐字符重新扫描剩余字符串。 */
    locale_width=1;source="\xc2\xa2";errno=0;
    assert(mbsrtowcs(output,&source,1,&state)==(size_t)-1 && errno==EILSEQ);
    locale_width=4;
}

int main(void)
{
    invalid_prefixes();split_and_count();scalars_and_limits();
    puts("B02 UTF-8: prefixes, split input, source/state, zero capacity and linear scan passed");
}
