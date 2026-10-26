.global feclearexcept
.type feclearexcept,@function
feclearexcept:
	sub $8,%rsp
		# maintain exceptions in the sse mxcsr, clear x87 exceptions
	mov %edi,%ecx
	and $0x3f,%ecx
	fnstsw %ax
	test %eax,%ecx
	jz 1f
	fnclex
1:	stmxcsr (%rsp)
	and $0x3f,%eax
	or %eax,(%rsp)
	test %ecx,(%rsp)
	jz 1f
	not %ecx
	and %ecx,(%rsp)
	ldmxcsr (%rsp)
1:	xor %eax,%eax
	add $8,%rsp
	ret

.global feraiseexcept
.type feraiseexcept,@function
feraiseexcept:
	sub $40,%rsp
	and $0x3f,%edi
	fnstenv (%rsp)
	or %di,4(%rsp)
	fldenv (%rsp)
	fwait
	stmxcsr 32(%rsp)
	or %edi,32(%rsp)
	ldmxcsr 32(%rsp)
	xor %eax,%eax
	add $40,%rsp
	ret

.global __fesetround
.hidden __fesetround
.type __fesetround,@function
__fesetround:
	push %rax
	xor %eax,%eax
	mov %edi,%ecx
	fnstcw (%rsp)
	andb $0xf3,1(%rsp)
	or %ch,1(%rsp)
	fldcw (%rsp)
	stmxcsr (%rsp)
	shl $3,%ch
	andb $0x9f,1(%rsp)
	or %ch,1(%rsp)
	ldmxcsr (%rsp)
	pop %rcx
	ret

.global fegetround
.type fegetround,@function
fegetround:
	push %rax
	stmxcsr (%rsp)
	pop %rax
	shr $3,%eax
	and $0xc00,%eax
	ret

.global fegetenv
.type fegetenv,@function
fegetenv:
	xor %eax,%eax
	fnstenv (%rdi)
	stmxcsr 28(%rdi)
	fldenv (%rdi)
	ret

.global fesetenv
.type fesetenv,@function
fesetenv:
	xor %eax,%eax
	inc %rdi
	jz 1f
	fldenv -1(%rdi)
	ldmxcsr 27(%rdi)
	ret
1:	push %rax
	push %rax
	pushq $0xffff
	pushq $0x37f
	fldenv (%rsp)
	pushq $0x1f80
	ldmxcsr (%rsp)
	add $40,%rsp
	ret

.global fetestexcept
.type fetestexcept,@function
fetestexcept:
	and $0x3f,%edi
	push %rax
	stmxcsr (%rsp)
	pop %rsi
	fnstsw %ax
	or %esi,%eax
	and %edi,%eax
	ret

.section .note.GNU-stack,"",@progbits
