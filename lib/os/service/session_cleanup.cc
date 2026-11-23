#include <os/detail/client.h>
#include <os/runtime.h>

#include "service/common/protocol.h"
/* 认证会话所属进程的退出清理。 */
#include <os/dir.h>
#include <os/user.h>

namespace os {

    /* 清理对象对应一个认证会话，操作由调用者发起。 */
    SessionCleanup::SessionCleanup(uint64_t session) : session_(session) {
    }

    /* 遍历进程目录并结束指定认证会话的用户进程。 */
    void SessionCleanup::terminate() {
        const uint64_t session = session_;
        services::wire::TaskInfo self{};
        if (!session || os::detail::TaskQuery().query(0, self) ||
            self.sessionId == session) {
            return;
        }

        Dir fs;
        Dir::Entry entries[8];
        if (fs.open("/proc")) {
            return;
        }

        for (;;) {
            size_t count = 0;
            if (fs.readBatch(entries, 8, count)) {
                break;
            }

            if (count <= 0) {
                break;
            }

            for (size_t i = 0; i < count; ++i) {
                uint64_t pid = 0;
                for (const char* p = entries[i].getName();
                     *p >= '0' && *p <= '9'; ++p) {
                    pid = pid * 10 + *p - '0';
                }

                services::wire::TaskInfo info{};
                if (pid && !os::detail::TaskQuery().query(pid, info) &&
                    !info.managedService) {
                    // 图形终端的 shell 会另建 POSIX
                    // 会话；注销还要按认证会话清理， 否则关闭桌面后这些
                    // shell 和后台程序仍会残留。
                    os::SessionIdentityInfo identity{};
                    if (info.sessionId == session ||
                        (!os::SessionIdentity(pid).query(identity) &&
                         identity.session == session)) {
                        detail::TaskControl().sendSignal(pid, 9);
                    }
                }
            }
        }
    }

}  // namespace os
