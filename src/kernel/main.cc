#include "cpu.h"
#include "io.h"
#include "kassert.h"
#include "mm.h"
#include "page.h"
#include "tty.h"
#include "task.h"
#include "keyboard.h"

#include <cstdint>

using namespace std;

void delay(void) {
    for (int i = 0; i<0xfffffff; i++);
}

void cppmain(uint8_t *addr) {
    tty::video::init(addr);
    pic::init();
    gdt::init();
    idt::init();
    keyboard::init();
    serial::init();
    timer::init(100);

    task::proc::init();

    mm::pool::init();
    int *p = new int;
    *p     = 0x3456789a;
    tty::printf("p = 0x%lx, *p = 0x%x\n", (uint64_t)p, *p);
    delete p;

    while (true) {
        delay();
        tty::printf("A");
    }
}
