#pragma once

#include <stdint.h>

namespace os {

void setServiceRegistry(uint64_t endpoint);
uint64_t serviceRegistry();
bool registerService(uint64_t registry, const char* name, uint64_t endpoint);
uint64_t lookupService(uint64_t registry, const char* name,
                       uint32_t attempts = 100);

}  // namespace os
