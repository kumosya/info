/*
 * 引导信息与启动协调。
 */

#pragma once
#include <os/console.h>
#include <os/ipc.h>
#include <os/task.h>

#include "service/common/records.h"

namespace console {

    /* 保存启动终端与图形显示交接的当前状态。 */
    class BootState {
        public:
            BootState();
            ~BootState() = default;

            bool active() const {
                return active_;
            }

            int validate(const os::IpcMessage& request,
                         const services::wire::TaskInfo& caller) const;
            void complete();

        private:
            bool active_;
            bool authorized(const services::wire::TaskInfo& caller) const;
            bool validStatus(const os::IpcMessage& request) const;
            bool validComplete(const os::IpcMessage& request) const;
    };
}  // namespace console
