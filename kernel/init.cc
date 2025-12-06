#include <mm.h>
#include <entry.h>
#include <idt.h>
#include <video.h>

#include <stdint.h>

void cppinit(void)
{
    video::init();
    idt::init();
    int *p = (int *)0xffff8000001000;
    *p = 0x12345678;
    while (true);
}
