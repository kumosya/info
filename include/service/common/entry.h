#pragma once
/* Native startup auxiliary-vector tags, separate from syscall entry numbers. */
#define OS_AUX_KERNEL_HANDLE 0x60000004
#define OS_AUX_REGISTRY_HANDLE 0x60000005
/* Operations used by the freestanding first-user-thread probe. */
#define OS_TASK_EXIT_OPERATION 96
#define OS_TASK_YIELD_OPERATION 99
