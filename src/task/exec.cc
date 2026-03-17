/**
 * @file task/exec.cc
 * @brief Executive functions for process execution
 * @author Kumosya, 2025-2026
 **/

#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "kernel/cpu.h"
#include "kernel/io.h"
#include "kernel/elf.h"
#include "kernel/mm.h"
#include "kernel/page.h"
#include "kernel/task.h"
#include "kernel/tty.h"

namespace task {

extern "C" std::uint64_t ExecProc(Task *pcb, task::Registers *regs) {
    task::SwitchTable(pcb);

    return 1;
}

int Task::Execve(const char *filename, const char *argv[], const char *envp[]) {
    int file = open(filename, O_RDONLY, 0);

    if (file == -1) {
        return -1;
    }

    Elf64_Ehdr ehdr;
    read(file, &ehdr, sizeof(ehdr));

    if (memcmp(ehdr.e_ident, "\x7f\x45\x4c\x46", 4) != 0) {
        tty::printk("execve: not an available elf file\n");
        return -1;
    }
    if (ehdr.e_type != ET_EXEC) {
        tty::printk("execve: not executable\n");
        return -2;
    }
    if (ehdr.e_machine != EM_X86_64) {
        tty::printk("execve: not x86_64\n");
        return -3;
    }

    PTE *user_pml4 = (PTE *)mm::page::Alloc(512 * sizeof(PTE));
    memset(user_pml4, 0, 512 * sizeof(PTE));

    memcpy(&user_pml4[256], &mm::page::kernel_pml4[256], 256 * sizeof(PTE));

    for (std::uint16_t i = 0; i < ehdr.e_phnum; i++) {
        Elf64_Phdr phdr;
        std::uint64_t phdr_offset = ehdr.e_phoff + i * ehdr.e_phentsize;
        lseek(file, phdr_offset, SEEK_SET);
        read(file, &phdr, sizeof(phdr));

        if (phdr.p_type == PT_LOAD) {
            std::uint64_t page_start = phdr.p_vaddr & ~0xFFF;
            std::uint64_t page_end =
                (phdr.p_vaddr + phdr.p_memsz + 0xFFF) & ~0xFFF;
            std::uint64_t page_count = (page_end - page_start) / 0x1000;

            void *page_addr = mm::page::Alloc(page_end - page_start);

            for (std::uint64_t j = 0; j <= page_count; j++) {
                mm::page::Map(
                    user_pml4, page_start + j * 0x1000,
                    mm::Vir2Phy((std::uint64_t)page_addr + j * 0x1000),
                    PTE_PRESENT | PTE_WRITABLE | PTE_USER);
            }

            lseek(file, phdr.p_offset, SEEK_SET);
            read(file, (void *)page_addr, phdr.p_filesz);

            if (i == 1) {
                text_start = phdr.p_vaddr;
                text_end   = phdr.p_vaddr + phdr.p_memsz;
            }
        }
    }

    if (this != nullptr &&
        thread != nullptr) {
        gdt::tss->rsp0 = thread->rsp0;
    }

    thread->rip =
        reinterpret_cast<std::uint64_t>(ret_syscall);
    
    thread->rsp = reinterpret_cast<std::uint64_t>(
        reinterpret_cast<char *>(this) + sizeof(task::Task) +
        KERNEL_STACK_SIZE - sizeof(task::Registers));

    task::Registers *regs = (task::Registers *)thread->rsp;

    pml4 = user_pml4;
    stack_base = USER_STACK_BASE;
    stack_allocated = 0;
    heap_start = heap_end = USER_HEAP_BASE;

    uint64_t argc = 0, len = 0;
    char **user_argv =
        reinterpret_cast<char **>(mm::page::Alloc(argc * sizeof(char *)));
    while (argv[argc] != nullptr) {
        len             = strlen(argv[argc]) + 1;
        user_argv[argc] = reinterpret_cast<char *>(
            mm::Vir2Phy(reinterpret_cast<std::uint64_t>(
                mm::page::Alloc(len * sizeof(char)))));
        strcpy(reinterpret_cast<char *>(mm::Phy2Vir(
                   reinterpret_cast<std::uint64_t>(user_argv[argc]))),
               argv[argc]);
        mm::page::Map(user_pml4,
                      reinterpret_cast<std::uint64_t>(user_argv[argc]),
                      reinterpret_cast<std::uint64_t>(user_argv[argc]),
                      PTE_PRESENT | PTE_WRITABLE | PTE_USER);
        argc++;
    }
    mm::page::Map(user_pml4,
                  mm::Vir2Phy(reinterpret_cast<std::uint64_t>(user_argv)),
                  mm::Vir2Phy(reinterpret_cast<std::uint64_t>(user_argv)),
                  PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    regs->rdi = argc;
    regs->rsi = mm::Vir2Phy(reinterpret_cast<std::uint64_t>(user_argv));
    regs->rcx = ehdr.e_entry;
    regs->rflags = (1 << 9) | (1 << 1);
    regs->r11 = regs->rflags;

    is_kernel = false;
    MkStack();
    regs->r12 = stack_base;
    regs->rax = 1;
    regs->ds = regs->es = 0;
    
    if (this == task::current_proc) {
        __asm__ __volatile__(
        "movq %2, %%rsp \n"
        "pushq %3\n"
        "jmp ExecProc\n" ::"D"(this), "S"(regs),
        "m"(thread->rsp),
        "m"(thread->rip)
        : "memory");
    }
    
    return 0;
}

}  // namespace task::thread
