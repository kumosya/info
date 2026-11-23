/* 控制台启动状态的维护与请求检查。 */
#include "service/console/boot.h"

#include "service/common/identity.h"
#include "service/common/protocol.h"

namespace console {

    /* 新控制台从启动显示阶段开始。 */
    BootState::BootState() : active_(true) {
    }

    /* 启动进度和显示交接由受管理的 init 服务发起。 */
    bool BootState::authorized(const services::wire::TaskInfo& caller) const {
        return caller.managedService &&
               caller.serviceRole ==
                   static_cast<uint32_t>(services::ServiceRole::Init);
    }

    /* 进度消息携带启动结果、错误码和服务名称。 */
    bool BootState::validStatus(const os::IpcMessage& request) const {
        if (request.length != 10 || request.handleCount ||
            request.data[0] > 1) {
            return false;
        }

        const unsigned char* name =
            reinterpret_cast<const unsigned char*>(&request.data[2]);
        if (!name[0]) {
            return false;
        }

        for (unsigned index = 0; index < 64; ++index) {
            if (!name[index]) {
                return true;
            }

            if (name[index] < 32 || name[index] > 126) {
                return false;
            }
        }

        return false;
    }

    /* 交接消息指定最终显示的文字终端或图形终端。 */
    bool BootState::validComplete(const os::IpcMessage& request) const {
        return request.length == 1 && !request.handleCount &&
               request.data[0] <= os::graphicalTerminalId;
    }

    /* 结合调用者、消息格式和当前阶段给出处理结果。 */
    int BootState::validate(const os::IpcMessage& request,
                            const services::wire::TaskInfo& caller) const {
        if (!authorized(caller)) {
            return static_cast<int>(os::ErrorCode::PermissionDenied);
        }

        const services::TerminalRequest operation =
            static_cast<services::TerminalRequest>(request.type);
        const bool valid =
            operation == services::TerminalRequest::BootStatus
                ? validStatus(request)
                : operation == services::TerminalRequest::BootComplete &&
                      validComplete(request);
        if (!valid) {
            return static_cast<int>(os::ErrorCode::InvalidArgument);
        }

        return active_ ? 0 : static_cast<int>(os::ErrorCode::Busy);
    }

    /* 显示交接成功后结束本次启动显示。 */
    void BootState::complete() {
        active_ = false;
    }

}  // namespace console
