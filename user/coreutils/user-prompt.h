#ifndef _COREUTILS_USER_PROMPT_H
#define _COREUTILS_USER_PROMPT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "os/abi.h"
#include "os/protocol.h"
#include "os/pty.h"
#include "os/service.h"
#include "os/terminal.h"
#include "os/users.h"

namespace coreutils {
inline void erase(void* data, size_t size) {
    volatile char* bytes = static_cast<volatile char*>(data); while (size--) *bytes++ = 0;
}
inline bool password(const char* prompt, char* output, size_t capacity) {
    if (!isatty(0)) { fputs("password input requires a terminal\n", stderr); return false; }
    const uint64_t handle = os::getStandardHandle(0);
    os::TerminalAttributes attributes{};
    uint64_t terminal = 0;
    bool previousEcho = true;
    if (handle) {
        if (!os::Pty::getPtyAttributes(handle, attributes)) return false;
        const auto hidden = [&] { auto value = attributes; value.echo = false; return value; }();
        if (!os::Pty::setPtyAttributes(handle, hidden)) return false;
    } else {
        terminal = os::ServiceRegistry::lookupService(os::ServiceRegistry::serviceRegistry(), os::terminalServiceName, 2000);
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::TerminalRequest::SetEcho);
        request.data[0] = os::Terminal::getId(); request.data[1] = 0; request.length = 2;
        if (!terminal || os::callService(terminal, request, reply) || reply.data[0]) {
            if (terminal) os::closeHandle(terminal);
            return false;
        }
        previousEcho = reply.data[1] != 0;
    }
    fputs(prompt, stdout); fflush(stdout);
    bool ok = fgets(output, capacity, stdin) != nullptr;
    char* newline = ok ? strchr(output, '\n') : nullptr;
    if (newline) *newline = 0;
    else if (ok) { while (true) { const int c = getchar(); if (c == '\n' || c == EOF) break; } ok = false; }
    if (handle) os::Pty::setPtyAttributes(handle, attributes);
    else {
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::TerminalRequest::SetEcho);
        request.data[0] = os::Terminal::getId(); request.data[1] = previousEcho; request.length = 2;
        os::callService(terminal, request, reply); os::closeHandle(terminal);
    }
    puts(""); return ok;
}
inline bool account(uint32_t uid, os::UserProfile& profile) {
    os::UserCredentials request{}; request.id = uid;
    return os::UserClient::request(os::UserOperation::Lookup, request, profile) == 0;
}
inline void environment(const os::UserProfile& profile) {
    setenv("USER", profile.name, 1); setenv("LOGNAME", profile.name, 1);
    setenv("HOME", profile.home, 1); setenv("SHELL", profile.shell, 1);
    chdir(profile.home);
}
}
#endif
