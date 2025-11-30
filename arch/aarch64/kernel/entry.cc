#include <stdint.h>

// QEMU AArch64 virt board UART address (PL011 UART)
#define UART_BASE 0x9000000

// Simple UART output function
void uart_putc(char c) {
    volatile uint8_t* uart = (volatile uint8_t*)UART_BASE;
    *uart = c;
}

// Print a string to UART
void uart_puts(const char* s) {
    while (*s) {
        uart_putc(*s++);
    }
}

// Kernel main function
void cppstart() {
    // Simple UART test
    uart_puts("Hello AArch64!\n");
    uart_puts("Kernel is running!\n");
    
    // Infinite loop with periodic output
    int count = 0;
    while (1) {
        uart_putc('.');
        
        // Simple delay loop
        for (int i = 0; i < 1000000; i++) {
            // NOP
            asm __volatile__ ("nop");
        }
        
        count++;
        if (count % 10 == 0) {
            uart_putc('\n');
        }
    }
}
