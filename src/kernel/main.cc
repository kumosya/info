#include <cstdint>
#include <cstring>

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

void KernelMain(uint8_t *addr) {
    asm __volatile__("mov %%cr3, %0" : "=r"(mm::page::kernel_pml4));
    mm::page::frame = pm;

    tty::video::Init(addr);
    pic::Init();
    gdt::Init();
    idt::Init();
    keyboard::Init();
    serial::Init();
    timer::Init(100);

    mm::slab::Init();

    task::thread::Init();
    task::thread::Exit(0);
}
