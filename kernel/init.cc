#include <mm.h>
#include <entry.h>
#include <idt.h>
#include <tty.h>
#include <page.h>
#include <pic.h>
#include <serial.h>
#include <timer.h>
#include <serial.h>
#include <timer.h>
#include <kassert.h>

#include <stdint.h>

uint64_t pm_addr;

void cppinit()
{
#ifndef OUTPUT_TO_SERIAL
    tty::video::init();
#endif
    pic::init();
    idt::init();
    serial::init();
    timer::init(100);

    mm::pool::init();
    
    int *p = new int;
    *p = 0x3456789a;
    tty::printf("p = 0x%lx, *p = 0x%x\n", (uint64_t)p, *p);
    delete p;

    while (true);
}
