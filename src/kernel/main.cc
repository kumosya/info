#include <cstdint>
#include <cstring>

#include "kernel/block.h"
#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/keyboard.h"
#include "kernel/mm.h"
#include "kernel/multiboot2.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"
#include "kernel/vfs.h"

char *cmdline = nullptr;

void KernelMain(std::uint8_t *addr) {
    asm __volatile__("mov %%cr3, %0" : "=r"(mm::page::kernel_pml4));
    mm::page::kernel_pml4 =
        (PTE *)mm::Phy2Vir((std::uint64_t)mm::page::kernel_pml4);
    mm::page::frame = pm;

    tty::video::Init((std::uint8_t *)mm::Phy2Vir((std::uint64_t)addr));

    pic::Init();
    gdt::Init();
    idt::Init();

    serial::Init();
    timer::Init(TIMER_FREQUENCY);

    asm volatile("cli");
    multiboot_tag_string *str = nullptr;
    multiboot_tag *tag        = reinterpret_cast<multiboot_tag *>(
        (std::uint8_t *)mm::Phy2Vir((std::uint64_t)addr) + 8);
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_CMDLINE) {
            str = reinterpret_cast<multiboot_tag_string *>(tag);
        }
        tag = reinterpret_cast<multiboot_tag *>(
            reinterpret_cast<std::uint8_t *>(tag) + ((tag->size + 7) & ~7));
    }

    cmdline = (char *)mm::page::Alloc(strlen(str->string) * sizeof(char));
    strcpy(cmdline, str->string);

    task::thread::Init();

    asm volatile("sti");
    while (true) {
        // asm volatile("hlt");
    }
    // task::thread::Exit(0);
}

void *__dso_handle = nullptr;

extern "C" int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
