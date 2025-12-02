#include <init.h>
#include <mm.h>
#include <entry.h>

#include <stdint.h>

void cppinit(void)
{
    boot::printf("init() function address: 0x%lx\n", (uint64_t)&cppinit);
    //while (true);
}
