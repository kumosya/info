/* 配置文本的字符范围、路径分段与缓冲区长度检查。 */
#include <cassert>

#include <os/runtime.h>

int main() {
    assert(os::ConfigurationText("name=value\n").valid());
    assert(os::ConfigurationText("name=value\r\n").valid());
    assert(!os::ConfigurationText("name=value\r").valid());
    assert(!os::ConfigurationText("name=\x01").valid());
    assert(!os::ConfigurationText(nullptr).valid());

    const char bounded[] = {'a', '\n', '\x01'};
    assert(os::ConfigurationText(bounded, 2).valid());
    assert(!os::ConfigurationText(bounded, 3).valid());

    assert(os::ConfigurationText("/etc/users").absolutePath());
    assert(os::ConfigurationText("/etc/users", 4).absolutePath());
    assert(!os::ConfigurationText("etc/users").absolutePath());
    assert(!os::ConfigurationText("/etc//users").absolutePath());
    assert(!os::ConfigurationText("/etc/../users").absolutePath());
    assert(!os::ConfigurationText("/etc/./users").absolutePath());
    assert(!os::ConfigurationText("/etc/users/").absolutePath());
    assert(!os::ConfigurationText("/etc/user name").absolutePath());
    assert(!os::ConfigurationText("/etc\\users").absolutePath());

    return 0;
}
