#pragma once
#include <cstdint>

namespace storage {

    // Elapsed time keeps the request budget valid across an unsigned wrap.
    constexpr bool expired(uint64_t start, uint64_t now, uint64_t budget) {
        return now - start >= budget;
    }

    // The driver serializes all transitions with its request lock.
    template <uint32_t Count>
    class VirtioRequestSlots {
            static_assert(Count > 0 && Count <= UINT16_MAX / 3,
                          "请求槽数量必须能用非零的描述符索引范围表示");

        public:
            uint32_t findFree() const {
                if (frozen_) {
                    return Count;
                }

                for (uint32_t i = 0; i < Count; ++i) {
                    if (slots_[i].state == State::Free) {
                        return i;
                    }
                }

                return Count;
            }

            void submit(uint32_t slot) {
                slots_[slot] = {State::Submitted, 0};
            }

            static constexpr bool validUsedAdvance(uint16_t previous,
                                                   uint16_t current) {
                return static_cast<uint16_t>(current - previous) <= Count;
            }

            bool recordUsed(uint32_t head, uint32_t bytes) {
                if (frozen_ || head % 3 || head / 3 >= Count) {
                    return false;
                }

                Slot& slot = slots_[head / 3];
                if (slot.state != State::Submitted) {
                    return false;
                }

                slot = {State::Completed, bytes};
                return true;
            }

            bool completed(uint32_t slot) const {
                return slots_[slot].state == State::Completed;
            }

            uint32_t usedLength(uint32_t slot) const {
                return slots_[slot].usedLength;
            }

            void consume(uint32_t slot) {
                slots_[slot] = {};
            }

            void freeze() {
                frozen_ = true;
                for (Slot& slot : slots_) {
                    if (slot.state != State::Free) {
                        slot.state = State::Held;
                    }
                }
            }

        private:
            enum class State : uint8_t { Free, Submitted, Completed, Held };

            class Slot {
                public:
                    State state;
                    uint32_t usedLength;
            };

            Slot slots_[Count]{};
            bool frozen_ = false;
    };
}  // namespace storage
