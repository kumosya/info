#include "block.h"
#include "cpu.h"
#include "fs/ext2.h"
#include "ide.h"
#include "io.h"
#include "kassert.h"
#include "keyboard.h"
#include "mm.h"
#include "page.h"
#include "task.h"
#include "tty.h"
#include "vfs.h"

#include <cstdint>
#include <cstring>

using namespace std;

void delay(void) {
    for (int i = 0; i < 0xfffffff; i++)
        ;
}

void kernel_main(uint8_t *addr) {
    asm __volatile__("mov %%cr3, %0" : "=r"(mm::page::kernel_pml4));
    mm::page::frame = pm;

    tty::video::init(addr);
    pic::init();
    gdt::init();
    idt::init();
    keyboard::init();
    serial::init();
    timer::init(100);

    task::thread::init();

    mm::slab::init();
    int *p = new int;
    *p     = 0x3456789a;
    tty::printf("p = 0x%lx, *p = 0x%x\n", (uint64_t)p, *p);
    delete p;

    ide::init();
    vfs::init();

    while (true) {
        delay();
        tty::printf("A");
    }
}
