#pragma once
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include "service/common/runtime.h"
#include "service/console/client.h"
#include <os/console.h>
#include <os/service.h>
#include <os/sync.h>

class BootReporter {
    struct Entry {
        char name[64]{};
        bool started      = false;
        int32_t error     = 0;
        uint64_t revision = 0, sent = 0;
    };
    Entry entries_[96]{};
    uint32_t count_ = 0;
    os::SpinLock lock_;
    uint64_t instance_  = 0;
    uint32_t preferred_ = os::graphicalTerminalId;
    bool handedOff_     = false;

   public:
    uint32_t preferredTerminal() const { return preferred_; }
    void record(const char* name, bool started, int32_t error = 0) {
        os::SpinLock::Guard guard(lock_);
        uint32_t index = 0;
        while (index < count_ &&
               !os::Runtime::stringsEqual(entries_[index].name, name))
            ++index;
        if (index == count_) {
            if (count_ == 96) return;
            os::Runtime::copyString(entries_[count_++].name, name,
                                    sizeof(Entry::name));
        }
        auto& entry = entries_[index];
        if (entry.revision && entry.started == started && entry.error == error)
            return;
        entry.started = started;
        entry.error   = error;
        ++entry.revision;
    }
    // Only init's main thread pumps IPC; the lifecycle observer only appends
    // under lock.
    bool pump(uint64_t registry, uint32_t terminal = UINT32_MAX,
              uint32_t fallback = UINT32_MAX) {
        const uint64_t endpoint =
            os::detail::RegistryClient(registry).lookup("console", 1);
        if (!endpoint) return false;
        const uint64_t instance =
            os::Ipc::Handle(endpoint, false).queryIdentity();
        if (!instance) {
            os::closeHandle(endpoint);
            return false;
        }
        if (instance != instance_) {
            bool active;
            if (!os::BootConsole().state(endpoint, active, preferred_)) {
                os::closeHandle(endpoint);
                return false;
            }
            instance_  = instance;
            handedOff_ = !active;
            os::SpinLock::Guard guard(lock_);
            for (uint32_t i = 0; i < count_; ++i) entries_[i].sent = 0;
        } else if (!handedOff_) {
            // A lost completion reply must not leave init retrying an already
            // finished boot.
            bool active;
            uint32_t preferred;
            if (!os::BootConsole().state(endpoint, active, preferred)) {
                os::closeHandle(endpoint);
                return false;
            }
            handedOff_ = !active;
        }
        bool delivered = true;
        if (!handedOff_) {
            for (uint32_t index = 0; index < 96; ++index) {
                Entry entry;
                {
                    os::SpinLock::Guard guard(lock_);
                    if (index >= count_) break;
                    entry = entries_[index];
                }
                if (entry.sent == entry.revision) continue;
                if (!os::BootConsole().report(endpoint, entry.name,
                                              entry.started, entry.error)) {
                    delivered = false;
                    break;
                }
                os::SpinLock::Guard guard(lock_);
                if (entries_[index].revision == entry.revision)
                    entries_[index].sent = entry.revision;
            }
            if (delivered && terminal <= os::graphicalTerminalId) {
                handedOff_ = os::BootConsole().finish(endpoint, terminal);
                if (!handedOff_ && fallback < os::textTerminalCount &&
                    fallback != terminal)
                    handedOff_ = os::BootConsole().finish(endpoint, fallback);
                if (handedOff_)
                    os::Sys().log(
                        "[INIT] boot display complete; console TTY switching "
                        "enabled\n");
            }
        }
        os::closeHandle(endpoint);
        return delivered && handedOff_;
    }
};
