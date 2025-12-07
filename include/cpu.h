#ifndef CPU_H
#define CPU_H

/* CR0 */
#define CR0_PG (1 << 31)

/* CR4 */
#define CR4_PSE (1 << 4)
#define CR4_PAE (1 << 5)
#define CR4_PGE (1 << 7)

/* Segment selector */
#define SELECTOR_RPL (0)
#define SELECTOR_TI (2)
#define SELECTOR_INDEX (3)

#ifndef ASM_FILE

/* C++ Codes */

#endif /* ASM_FILE */

#endif /* CPU_H */