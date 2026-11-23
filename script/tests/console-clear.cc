/* 直接运行文字终端的清屏重绘，检查提示符和光标。 */
#define main console_service_test_entry
#include "../../src/console/main.cc"
#undef main
#include <cassert>

int main() {
    static Tty terminal;
    terminal.init(0, 80, 25, false);
    terminal.write("old output\nroot:/# test", 23);
    const uint32_t cursor = terminal.cursorX();
    terminal.redrawInputAfterClear();
    assert(terminal.cursorX() == cursor && terminal.cursorY() == 0);
    const char* expected = "root:/# test";
    for (uint32_t index = 0; expected[index]; ++index) {
        assert(terminal.cells()[index].character == expected[index]);
    }
    for (uint32_t index = 80; index < 80 * 25; ++index) {
        assert(terminal.cells()[index].character == ' ');
    }
    terminal.redrawInputAfterClear();
    assert(terminal.cursorX() == cursor && terminal.cursorY() == 0);
}
