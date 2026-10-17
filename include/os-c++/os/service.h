#ifndef _OS_SERVICE_H
#define _OS_SERVICE_H

#include <stdint.h>

namespace os {

class SystemService {
   public:
    static bool isPrivileged() {
        uint16_t selector;
        asm volatile("mov %%cs, %0" : "=r"(selector));
        return (selector & 3) == 0;
    }
    static int readPort8(uint16_t port, uint8_t& value) {
        if (!isPrivileged()) return -1;
        asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
        return 0;
    }
    static int writePort8(uint16_t port, uint8_t value) {
        if (!isPrivileged()) return -1;
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
        return 0;
    }
};

void setServiceRegistry(uint64_t endpoint);
uint64_t serviceRegistry();
bool registerService(uint64_t registry, const char* name, uint64_t endpoint);
uint64_t lookupService(uint64_t registry, const char* name,
                       uint32_t attempts = 100);

}  // namespace os

#endif
