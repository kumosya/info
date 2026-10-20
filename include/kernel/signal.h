#pragma once

#include <cstdint>

namespace kernel {

struct SyscallContext;
class Task;

enum class Signal : uint32_t {
    Hangup                = 1,
    Interrupt             = 2,
    Quit                  = 3,
    IllegalInstruction    = 4,
    Trap                  = 5,
    Abort                 = 6,
    Bus                   = 7,
    FloatingPoint         = 8,
    Kill                  = 9,
    User1                 = 10,
    SegmentationViolation = 11,
    User2                 = 12,
    BrokenPipe            = 13,
    Alarm                 = 14,
    Terminate             = 15,
    StackFault            = 16,
    Child                 = 17,
    Continue              = 18,
    Stop                  = 19,
    TerminalStop          = 20,
    TerminalInput         = 21,
    TerminalOutput        = 22,
    Urgent                = 23,
    CpuLimit              = 24,
    FileSizeLimit         = 25,
    VirtualAlarm          = 26,
    Profiling             = 27,
    WindowChanged         = 28
};

constexpr uint32_t maximumSignal       = 64;
constexpr uint64_t signalDefault       = 0;
constexpr uint64_t signalIgnore        = 1;
constexpr uint64_t signalFlagNoDefer   = 0x40000000ULL;
constexpr uint64_t signalFlagResetHand = 0x80000000ULL;

class SignalAction {
   public:
    SignalAction() = default;
    SignalAction(uint64_t handlerValue, uint64_t maskValue, uint64_t flagsValue,
                 uint64_t restorerValue)
        : handler(handlerValue),
          mask(maskValue),
          flags(flagsValue),
          restorer(restorerValue) {}
    uint64_t getHandler() const { return handler; }
    uint64_t getMask() const { return mask; }
    uint64_t getFlags() const { return flags; }
    uint64_t setFlags(uint64_t value) {
        flags = value;
        return flags;
    }
    uint64_t getRestorer() const { return restorer; }
    uint64_t& accessRestorer() { return restorer; }

   private:
    uint64_t handler;
    uint64_t mask;
    uint64_t flags;
    uint64_t restorer;
};

namespace signal {
bool isValidSignal(uint32_t signal);
bool sendSignalToTask(uint64_t taskId, uint32_t signal, Task* caller = nullptr);
bool sendSignalToTaskGroup(uint64_t taskGroupId, uint32_t signal, Task* caller = nullptr);
bool deliverPendingSignal(SyscallContext* context);
bool restoreSignalFrame(SyscallContext* context, uint64_t userFrame);

}  // namespace signal

}  // namespace kernel
