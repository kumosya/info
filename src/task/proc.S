
.global enter_syscall
EnterSyscall:
    // TODO
    //callq system_call_handler

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
	addq $0x38, %rsp
	iretq

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
