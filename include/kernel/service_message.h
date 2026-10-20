#pragma once
#include "kernel/ipc.h"
namespace kernel {
class MessageData {
   public:
    static bool name(const IpcMessage& message, uint32_t word, const char*& value, size_t limit = 64) {
        if (message.length <= word) return false;
        value = reinterpret_cast<const char*>(&message.data[word]);
        const size_t bytes = (message.length - word) * sizeof(uint64_t);
        for (size_t i = 0; i < bytes && i < limit; ++i) if (!value[i]) return i != 0;
        return false;
    }
    static void copy(void* destination, const void* source, size_t bytes) {
        auto* to = static_cast<uint8_t*>(destination);
        auto* from = static_cast<const uint8_t*>(source);
        for (size_t i = 0; i < bytes; ++i) to[i] = from[i];
    }
    static void created(IpcMessage& reply, uint64_t handle) {
        if (handle) reply.handles[reply.handleCount++] = handle;
    }
};

}  // namespace kernel
