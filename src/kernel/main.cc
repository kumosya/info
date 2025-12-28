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
#include "multiboot2.h"

extern char *cmdline;

void KernelMain(uint8_t *addr) {
    asm __volatile__("mov %%cr3, %0" : "=r"(mm::page::kernel_pml4));
    mm::page::frame = pm;

    tty::video::Init(addr);
    pic::Init();
    gdt::Init();
    idt::Init();
    keyboard::Init();
    serial::Init();
    timer::Init(50);

    multiboot_tag_string *str = nullptr;
    multiboot_tag *tag                 = reinterpret_cast<multiboot_tag *>(addr + 8);
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_CMDLINE) {
            str = reinterpret_cast<multiboot_tag_string *>(tag);
        }
        tag = reinterpret_cast<multiboot_tag *>(reinterpret_cast<uint8_t *>(tag) + ((tag->size + 7) & ~7));
    }
    strcpy(cmdline, str->string);

    task::thread::Init();
    task::thread::Exit(0);
}
