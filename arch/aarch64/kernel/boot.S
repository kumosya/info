/* AArch64 Kernel Boot Code */

#include <cpp_mangle.h>

.set noreorder

.section .text, "ax"
.globl _start
_start:
    /* Set up stack pointer */
    mov     x29, #0x80000000   /* Stack base address */
    mov     sp, x29
    add     sp, sp, #0x4000     /* 16KB stack */
    
    /* Early UART test */
    mov     x0, #0x9000000      /* UART_BASE */
    mov     w1, #'B'            /* Send 'B' character */
    strb    w1, [x0]            /* Write to UART */
    
    /* Send 'O' character */
    mov     w1, #'O'
    strb    w1, [x0]
    
    /* Send 'O' character */
    mov     w1, #'O'
    strb    w1, [x0]
    
    /* Send 'T' character */
    mov     w1, #'T'
    strb    w1, [x0]
    
    /* Send '!' character */
    mov     w1, #'!'
    strb    w1, [x0]
    
    /* Send newline */
    mov     w1, #'\n'
    strb    w1, [x0]
    
    /* Call C++ kernel entry */
    bl      CPPSTART
    
    /* Infinite loop */
1:  b       1b
