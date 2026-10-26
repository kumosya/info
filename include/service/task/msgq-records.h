#pragma once
#include <cstdint>
namespace os::wire {
struct MessageQueueAttributes {
    uint64_t flags, maximumMessages, messageSize, currentMessages;
};
static_assert(sizeof(MessageQueueAttributes) == 32);
}  // namespace os::wire
