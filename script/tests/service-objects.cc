/* 启动状态、连接生命周期和图形会话回复解析。 */
#include <cassert>
#include <type_traits>

#include "service/common/protocol.h"
#include "service/console/boot.h"
#include <os/console.h>
#include <os/user.h>

static uint64_t returnedPhase = 0;
static uint32_t returnedLength = 3;
static int returnedError = 0;
static uint32_t callCount = 0;
static uint32_t closedHandles = 0;



    os::Sys::Transport::Transport(services::CoreServiceKind kind) : kind_(kind) {
    }

    os::Sys::Sys() : transport_(services::CoreServiceKind::System) {
    }

    int64_t os::Sys::Transport::result(IpcMessage& request, IpcMessage& reply) {
        assert(kind_ == services::CoreServiceKind::Task);
        assert(request.data[0] == 55);
        ++callCount;
        reply.length = returnedLength;
        reply.data[1] = 123;
        reply.data[2] = uint64_t(1000) | (uint64_t(1001) << 32);
        return returnedError;
    }

    os::Ipc::Handle::Handle(uint64_t value, bool owned)
        : value_(value), owned_(owned) {
    }

    os::Ipc::Handle::~Handle() {
        if (value_ && owned_) {
            ++closedHandles;
        }
    }

    uint64_t os::Ipc::Handle::getValue() const {
        return value_;
    }

    uint64_t os::Sys::readMonotonicTime() {
        return 100;
    }

    int os::callService(uint64_t endpoint, IpcMessage& request, IpcMessage& reply,
                    uint64_t deadline) {
        assert(endpoint == 7);
        assert(deadline > 100);
        ++callCount;
        reply.length = returnedLength;
        reply.data[1] = returnedPhase;
        reply.data[2] = 42;
        if (request.type == uint64_t(services::TerminalRequest::GetBootState)) {
            reply.data[2] = graphicalTerminalId;
        }

        return returnedError;
    }

  

int main() {
    static_assert(!std::is_copy_constructible_v<os::BootConsole>);
    static_assert(!std::is_copy_constructible_v<os::DisplayHandoff>);
    static_assert(!std::is_copy_constructible_v<os::GraphicalSession>);

    {
        os::GraphicalSession session(7);
        os::GraphicalPhase phase = os::GraphicalPhase::Starting;
        uint64_t controller = 0;

        for (uint64_t value = 0; value <= uint64_t(os::GraphicalPhase::Suspended);
             ++value) {
            returnedPhase = value;
            assert(session.state(phase, controller));
            assert(uint64_t(phase) == value && controller == 42);
        }

        returnedPhase = uint64_t(os::GraphicalPhase::Suspended) + 1;
        controller = 99;
        assert(!session.state(phase, controller));
        assert(phase == os::GraphicalPhase::Suspended && controller == 99);
        returnedPhase = 0;
        returnedLength = 2;
        assert(!session.state(phase, controller));
        returnedLength = 3;
        returnedError = -1;
        assert(!session.state(phase, controller));
        returnedError = 0;
        assert(session.bind(42, 123));
        assert(session.command(os::GraphicalRequest::Resume));
    }

    assert(closedHandles == 0);
    {
        os::GraphicalSession session(7, true);
    }
    assert(closedHandles == 1);

    const uint32_t previousCalls = callCount;
    os::GraphicalSession disconnected(0);
    assert(!disconnected.bind(1, 1));
    assert(!disconnected.command(os::GraphicalRequest::Resume));
    os::DisplayHandoff display(0);
    assert(!display.setGraphicalActive(true));
    os::BootConsole terminal(0);
    assert(!terminal.report("fs", true));
    assert(callCount == previousCalls);

    os::BootConsole connected(7);
    bool booting = false;
    uint32_t preferred = 0;
    returnedPhase = 1;
    assert(connected.state(booting, preferred));
    assert(booting && preferred == os::graphicalTerminalId);
    assert(connected.report("fs", true));
    assert(connected.finish(os::graphicalTerminalId));

    console::BootState state;
    services::wire::TaskInfo caller{};
    os::IpcMessage request{};
    request.type = uint64_t(services::TerminalRequest::BootComplete);
    request.length = 1;
    request.data[0] = os::graphicalTerminalId;
    assert(state.active());
    assert(state.validate(request, caller) == int(os::ErrorCode::PermissionDenied));
    caller.managedService = 1;
    caller.serviceRole = uint32_t(services::ServiceRole::Init);
    assert(state.validate(request, caller) == 0);
    request.handleCount = 1;
    assert(state.validate(request, caller) == int(os::ErrorCode::InvalidArgument));
    request.handleCount = 0;
    request.data[0] = os::graphicalTerminalId + 1;
    assert(state.validate(request, caller) == int(os::ErrorCode::InvalidArgument));
    request.data[0] = 0;
    state.complete();
    assert(!state.active());
    assert(state.validate(request, caller) == int(os::ErrorCode::Busy));

    os::SessionIdentity identity(55);
    os::SessionIdentityInfo info{};
    returnedLength = 3;
    assert(identity.query(info) == 0);
    assert(info.session == 123 && info.uid == 1000 && info.gid == 1001);
    assert(identity.getTask() == 55 && identity.getSession() == 123);
    assert(identity.getUserId() == 1000 && identity.getGroupId() == 1001);
    returnedLength = 1;
    assert(identity.query(info) == int(os::ErrorCode::IoError));
    assert(info.session == 123);
    returnedLength = 3;
    assert(identity.establish(1000, 1001) == 0);
    const uint32_t beforeInvalidGroups = callCount;
    assert(identity.establish(1000, 1001, false, 33, nullptr) ==
           int(os::ErrorCode::InvalidArgument));
    assert(callCount == beforeInvalidGroups);

    return 0;
}
