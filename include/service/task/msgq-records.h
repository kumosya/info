/*
 * 消息队列属性通信布局。
 */

#pragma once
#include <cstdint>
namespace os::wire {
    /* 保存消息队列标志、容量、单条消息上限和当前消息数。 */
    struct MessageQueueAttributes {
            uint64_t flags, maximumMessages, messageSize, currentMessages;
    };
    static_assert(sizeof(MessageQueueAttributes) == 32);
}  // namespace os::wire
