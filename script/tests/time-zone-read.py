"""宿主机回归测试：python3 script/tests/time-zone-read.py（需要 C 编译器）。"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[2] / "lib/libc/portable/time/utc.c").read_text()
start = source.index("struct local_zone {")
end = source.index("static int zone_offset(", start)
code = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const char* configuration;
static size_t calls, chunk = 4096, opens;
static FILE* fixture_open(const char* path, const char* mode) {
    (void)path;(void)mode;++opens;
    if(!configuration)return NULL;
    FILE* file=tmpfile();assert(file);
    fwrite(configuration,1,strlen(configuration),file);rewind(file);return file;
}
static size_t fixture_read(void* data,size_t size,size_t count,FILE* file) {
    ++calls;if(count>chunk)count=chunk;return fread(data,size,count,file);
}
#define fopen fixture_open
#define fread fixture_read
'''
code += source[start:end]
code += r'''
#undef fopen
#undef fread
static void check(const char* text,int minutes,int rule) {
    configuration=text;struct local_zone zone=read_zone();
    assert(zone.minutes==minutes && zone.rule==rule);
}
int main(void) {
    unsetenv("TZ");
    calls=0;
    check("offset_minutes=480\ndst_rule=none\n",480,0);
    assert(calls==2);
    // 每次转换都应重新加载配置，不能沿用上一次时区。
    check("offset_minutes=-300\r\ndst_rule=us\r\n",-300,1);
    chunk=1;
    check("offset_minutes=60\ndst_rule=eu",60,2);
    chunk=4096;
    check("offset_minutes=600\ndst_rule=australia\n",600,3);
    check("offset_minutes=720\ndst_rule=new-zealand\n",720,4);
    check("offset_minutes=841\ndst_rule=unknown\n",0,0);
    check("offset_minutes=-720\n",-720,0);
    check("offset_minutes=840\n",840,0);
    char longLine[512];memset(longLine,'x',sizeof(longLine));
    strcpy(longLine+400,"\noffset_minutes=480\n");
    check(longLine,480,0);
    check(NULL,0,0);
    setenv("TZ","UTC",1);size_t previous=opens;
    check("offset_minutes=480\n",0,0);assert(opens==previous);
    puts("timezone: bulk/short reads, DST rules and live configuration reload passed");
}
'''
with tempfile.TemporaryDirectory(prefix="osinfo-time-zone-test-") as folder:
    path = Path(folder)
    (path / "test.c").write_text(code)
    subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-fsanitize=address,undefined",
                    str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
