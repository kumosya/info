/**
 * @file task/proc.S
 * @brief Process entry and system calls
 * @author Kumosya, 2025-2026
 **/
 /* clang-format off */

#define ASM_FILE 1
#include "kernel/cpu.h"

.global enter_syscall
.global ret_syscall
enter_syscall:
	swapgs		/* User -> Kernel */

	movq %rsp, %r12
	movq %gs:4, %rsp

	/* Save volatile registers onto the user stack (will copy to kernel
	   stack below). */
	pushq %rbp
	pushq %rdi
	pushq %rsi
	pushq %rdx
	pushq %rcx
	pushq %rbx
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

	movq %r10, %rcx  /* syscall number in r10 */
	pushq %rax

	call SyscallMain
	popq %rax

ret_syscall:
	/* Syscall handler ran on the kernel stack. Restore kernel-saved
		return slot and registers while still on kernel stack, then
		restore the user stack pointer and perform swapgs before
		the final `sysretq`. */
	/* Store syscall return value into the saved rax slot on the
		kernel stack (will be popped into %rax below). */
	movq %rax, RAX(%rsp)

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp    /* loads syscall return value into %rax */

	movq $USER_CS, %rax
	movq %rax, %ds
	movq %rax, %es

	movq %r12, %rsp /* restore original user rsp */
	swapgs			/* kernel -> user */
	sysretq

.global kernel_thread_entry
kernel_thread_entry:
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %rbp
	popq %rax
	movq %rax, %ds
	popq %rax
	movq %rax, %es
	popq %rax

	movq %rdx, %rdi
    
    sti
	callq *%rbx
	cli

	movq %rax, %rdi
	callq do_exit
