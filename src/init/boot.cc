/* 服务对象的成员函数实现。 */
#include "service/init/init.h"

namespace init {

    /* 保存一条服务启动结果并增加其版本。 */
    void BootReporter::record(const char* name, bool started, int32_t error) {
        os::SpinLock::Guard guard(lock_);
        uint32_t index = 0;
        while (index < count_ &&
               !os::Runtime::stringsEqual(entries_[index].name, name)) {
            ++index;
        }

        if (index == count_) {
            if (count_ == 96) {
                return;
            }

            os::Runtime::copyString(entries_[count_++].name, name,
                                    sizeof(Entry::name));
        }

        Entry& entry = entries_[index];
        if (entry.revision && entry.started == started &&
            entry.error == error) {
            return;
        }

        entry.started = started;
        entry.error = error;
        ++entry.revision;
    }

    /* 发送待报告的启动结果并完成显示交接。 */
    bool BootReporter::pump(uint64_t registry, uint32_t terminal,
                            uint32_t fallback) {
        const uint64_t endpoint =
            os::detail::RegistryClient(registry).lookup("console", 1);
        if (!endpoint) {
            return false;
        }

        os::BootConsole console(endpoint);
        const uint64_t instance =
            os::Ipc::Handle(endpoint, false).queryIdentity();
        if (!instance) {
            os::closeHandle(endpoint);
            return false;
        }

        if (instance != instance_) {
            bool active;
            if (!console.state(active, preferred_)) {
                os::closeHandle(endpoint);
                return false;
            }

            instance_ = instance;
            handedOff_ = !active;
            os::SpinLock::Guard guard(lock_);
            for (uint32_t i = 0; i < count_; ++i) {
                entries_[i].sent = 0;
            }
        } else if (!handedOff_) {
            // A lost completion reply must not leave init retrying an
            // already finished boot.
            bool active;
            uint32_t preferred;
            if (!console.state(active, preferred)) {
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
                    if (index >= count_) {
                        break;
                    }

                    entry = entries_[index];
                }

                if (entry.sent == entry.revision) {
                    continue;
                }

                if (!console.report(entry.name, entry.started, entry.error)) {
                    delivered = false;
                    break;
                }

                os::SpinLock::Guard guard(lock_);
                if (entries_[index].revision == entry.revision) {
                    entries_[index].sent = entry.revision;
                }
            }

            if (delivered && terminal <= os::graphicalTerminalId) {
                handedOff_ = console.finish(terminal);
                if (!handedOff_ && fallback < os::textTerminalCount &&
                    fallback != terminal) {
                    handedOff_ = console.finish(fallback);
                }

                if (handedOff_) {
                    os::Sys().log(
                        "[INIT] boot display complete; console TTY "
                        "switching "
                        "enabled\n");
                }
            }
        }

        os::closeHandle(endpoint);
        return delivered && handedOff_;
    }

}  // namespace init
