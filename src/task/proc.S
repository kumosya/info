
.global enter_syscall
enter_syscall:
	sti
    pushq %rax
	movq %es, %rax
	pushq %rax
	movq %ds, %rax
	pushq %rax
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

	movq $0x10, %rdx
	movq %rdx, %ds
	movq %rdx, %es
	movq %rsp, %rdi

	call SyscallMain

.global ret_syscall
ret_syscall:
	movq %rax, 0x80(%rsp)
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
	
	sysexitq

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
	movq %rcx, %rsi
    
    sti
	callq *%rbx
	cli

	movq %rax, %rdi
	callq do_exit
