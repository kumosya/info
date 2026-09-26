#include <mutex>

#include "os/abi.h"
#include "os/gui.h"
#include "os/input.h"
#include "os/process.h"
#include "os/protocol.h"
#include "os/pty.h"
#include "os/service.h"

namespace {

constexpr uint32_t windowWidth = 704;
constexpr uint32_t windowHeight = 448;
constexpr uint32_t terminalColumns = 86;
constexpr uint32_t terminalRows = 24;
constexpr uint32_t tabCapacity = 4;
constexpr uint32_t tabWidth = 136;
constexpr uint64_t stackBase = 0x0000000068000000ULL;
constexpr uint64_t stackSize = 64 * 1024;

struct TerminalTab {
    os::PtyHandles pty;
    os::ProcessHandle shell;
    char cells[terminalRows][terminalColumns];
    uint32_t cursorX;
    uint32_t cursorY;
    uint8_t escapeState;
    bool active;
};

uint64_t windowService;
os::WindowHandle windowHandle;
TerminalTab tabs[tabCapacity];
uint32_t tabCount;
uint32_t activeTab;
std::mutex terminalLock;

void clearTab(TerminalTab& tab) {
    for (uint32_t row = 0; row < terminalRows; ++row) {
        for (uint32_t column = 0; column < terminalColumns; ++column) {
            tab.cells[row][column] = ' ';
        }
    }
    tab.cursorX = 0;
    tab.cursorY = 0;
}

void scroll(TerminalTab& tab) {
    for (uint32_t row = 1; row < terminalRows; ++row) {
        for (uint32_t column = 0; column < terminalColumns; ++column) {
            tab.cells[row - 1][column] = tab.cells[row][column];
        }
    }
    for (uint32_t column = 0; column < terminalColumns; ++column) {
        tab.cells[terminalRows - 1][column] = ' ';
    }
    tab.cursorY = terminalRows - 1;
}

void newLine(TerminalTab& tab) {
    tab.cursorX = 0;
    if (++tab.cursorY == terminalRows) scroll(tab);
}

void consumeOutput(TerminalTab& tab, const uint8_t* bytes, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        const uint8_t value = bytes[index];
        if (tab.escapeState == 1) {
            tab.escapeState = value == '[' ? 2 : 0;
            continue;
        }
        if (tab.escapeState == 2) {
            if (value == 'J') clearTab(tab);
            if (value == 'H') tab.cursorX = tab.cursorY = 0;
            if ((value >= '0' && value <= '9') || value == ';') continue;
            tab.escapeState = 0;
            continue;
        }
        if (value == 0x1b) {
            tab.escapeState = 1;
        } else if (value == '\r') {
            tab.cursorX = 0;
        } else if (value == '\n') {
            newLine(tab);
        } else if (value == 8 || value == 0x7f) {
            if (tab.cursorX != 0) --tab.cursorX;
        } else if (value == '\t') {
            do {
                tab.cells[tab.cursorY][tab.cursorX++] = ' ';
                if (tab.cursorX == terminalColumns) newLine(tab);
            } while ((tab.cursorX & 7U) != 0);
        } else if (value >= 32) {
            tab.cells[tab.cursorY][tab.cursorX++] = static_cast<char>(value);
            if (tab.cursorX == terminalColumns) newLine(tab);
        }
    }
}

void drawLabel(int32_t x, int32_t y, const char* text, uint32_t color) {
    os::drawText(windowService, windowHandle, x, y, text, color);
}

void render() {
    std::lock_guard<std::mutex> guard(terminalLock);
    if (tabCount == 0) return;
    os::fillRectangle(windowService, windowHandle, 0, 0, windowWidth,
                      windowHeight, 0x0013181dU);
    for (uint32_t index = 0; index < tabCount; ++index) {
        const uint32_t color = index == activeTab ? 0x00313a43U : 0x00222930U;
        os::fillRectangle(windowService, windowHandle,
                          static_cast<int32_t>(index * tabWidth), 0,
                          tabWidth - 1, 30, color);
        char title[] = "Terminal 1";
        title[9] = static_cast<char>('1' + index);
        drawLabel(static_cast<int32_t>(index * tabWidth + 12), 7, title,
                  0x00e7edf3U);
    }
    const int32_t plusX = static_cast<int32_t>(tabCount * tabWidth);
    os::fillRectangle(windowService, windowHandle, plusX, 0, 36, 30,
                      0x002b8a65U);
    drawLabel(plusX + 14, 7, "+", 0x00ffffffU);

    const TerminalTab& tab = tabs[activeTab];
    char line[48];
    for (uint32_t row = 0; row < terminalRows; ++row) {
        for (uint32_t start = 0; start < terminalColumns; start += 47) {
            const uint32_t length = terminalColumns - start < 47
                                        ? terminalColumns - start
                                        : 47;
            for (uint32_t index = 0; index < length; ++index) {
                line[index] = tab.cells[row][start + index];
            }
            line[length] = '\0';
            drawLabel(8 + static_cast<int32_t>(start * 8),
                      38 + static_cast<int32_t>(row * 16), line,
                      0x00d7e0e8U);
        }
    }
    os::fillRectangle(windowService, windowHandle,
                      8 + static_cast<int32_t>(tab.cursorX * 8),
                      38 + static_cast<int32_t>(tab.cursorY * 16), 8, 2,
                      0x0067d6a3U);
    os::presentWindow(windowService, windowHandle);
}

void readTab(uint32_t index) {
    uint8_t buffer[256];
    while (tabs[index].active) {
        const int64_t count =
            os::readPipe(tabs[index].pty.masterHandle, buffer, sizeof(buffer));
        if (count <= 0) break;
        {
            std::lock_guard<std::mutex> guard(terminalLock);
            consumeOutput(tabs[index], buffer, static_cast<size_t>(count));
        }
        if (index == activeTab) render();
    }
    tabs[index].active = false;
}

void readTab0() { readTab(0); }
void readTab1() { readTab(1); }
void readTab2() { readTab(2); }
void readTab3() { readTab(3); }

bool createTab() {
    if (tabCount == tabCapacity) return false;
    const uint32_t index = tabCount;
    TerminalTab& tab = tabs[index];
    clearTab(tab);
    if (!os::createPty(tab.pty)) return false;
    const char* arguments[] = {"shell"};
    tab.shell = os::executeWithIo("/bin/shell", 1, arguments,
                                  tab.pty.slaveHandle, tab.pty.slaveHandle,
                                  tab.pty.slaveHandle);
    if (tab.shell == 0) {
        os::closeHandle(tab.pty.masterHandle);
        os::closeHandle(tab.pty.slaveHandle);
        return false;
    }
    os::closeHandle(tab.pty.slaveHandle);
    tab.pty.slaveHandle = 0;
    os::ProcessInfo info{};
    if (os::getProcessInfo(tab.shell, info) != 0 ||
        !os::setPtyForegroundGroup(tab.pty.masterHandle, info.id)) {
        return false;
    }
    const os::TerminalWindowSize size{terminalRows, terminalColumns,
                                      windowWidth, windowHeight - 30};
    os::setPtyWindowSize(tab.pty.masterHandle, size);
    tab.active = true;
    ++tabCount;
    activeTab = index;
    void (*entries[])() = {readTab0, readTab1, readTab2, readTab3};
    const uint64_t stackAddress = stackBase + stackSize * index;
    if (os::mapMemory(stackAddress, stackSize, 1) != stackAddress ||
        os::createThread(entries[index], stackAddress + stackSize) == 0) {
        tab.active = false;
        return false;
    }
    return true;
}

char translateKey(os::KeyCode key, uint32_t modifiers) {
    const bool shift = (modifiers & os::keyModifierShift) != 0;
    const bool caps = (modifiers & os::keyModifierCapsLock) != 0;
    if (key >= os::KeyCode::A && key <= os::KeyCode::Z) {
        char value = static_cast<char>('a' + static_cast<uint32_t>(key) -
                                      static_cast<uint32_t>(os::KeyCode::A));
        if ((modifiers & os::keyModifierControl) != 0) {
            return static_cast<char>(value - 'a' + 1);
        }
        return shift != caps ? static_cast<char>(value - 'a' + 'A') : value;
    }
    if (key >= os::KeyCode::Digit1 && key <= os::KeyCode::Digit9) {
        static constexpr char shifted[] = "!@#$%^&*(";
        const uint32_t offset = static_cast<uint32_t>(key) -
                                static_cast<uint32_t>(os::KeyCode::Digit1);
        return shift ? shifted[offset] : static_cast<char>('1' + offset);
    }
    switch (key) {
        case os::KeyCode::Digit0: return shift ? ')' : '0';
        case os::KeyCode::Space: return ' ';
        case os::KeyCode::Enter:
        case os::KeyCode::KeypadEnter: return '\n';
        case os::KeyCode::Backspace: return 0x7f;
        case os::KeyCode::Tab: return '\t';
        case os::KeyCode::Minus: return shift ? '_' : '-';
        case os::KeyCode::Equal: return shift ? '+' : '=';
        case os::KeyCode::LeftBracket: return shift ? '{' : '[';
        case os::KeyCode::RightBracket: return shift ? '}' : ']';
        case os::KeyCode::Semicolon: return shift ? ':' : ';';
        case os::KeyCode::Apostrophe: return shift ? '"' : '\'';
        case os::KeyCode::Grave: return shift ? '~' : '`';
        case os::KeyCode::Backslash: return shift ? '|' : '\\';
        case os::KeyCode::Comma: return shift ? '<' : ',';
        case os::KeyCode::Period: return shift ? '>' : '.';
        case os::KeyCode::Slash: return shift ? '?' : '/';
        default: return 0;
    }
}

void handleKey(const os::WindowEvent& event) {
    static constexpr char up[] = "\033[A";
    static constexpr char down[] = "\033[B";
    static constexpr char right[] = "\033[C";
    static constexpr char left[] = "\033[D";
    const char* sequence = nullptr;
    if (event.keyCode == os::KeyCode::Up) sequence = up;
    if (event.keyCode == os::KeyCode::Down) sequence = down;
    if (event.keyCode == os::KeyCode::Right) sequence = right;
    if (event.keyCode == os::KeyCode::Left) sequence = left;
    if (sequence != nullptr) {
        os::writePipe(tabs[activeTab].pty.masterHandle, sequence, 3);
        return;
    }
    const char value = translateKey(event.keyCode, event.modifiers);
    if (value != 0) {
        os::writePipe(tabs[activeTab].pty.masterHandle, &value, 1);
    }
}

}  // namespace

int main(int, char**) {
    const uint64_t registry = os::serviceRegistry();
    windowService = os::lookupService(registry, os::windowServiceName, 30000);
    if (windowService == 0) return 1;
    windowHandle = os::createWindow(windowService, windowWidth, windowHeight,
                                    "Terminal");
    if (windowHandle == 0 || !createTab()) return 2;
    os::moveWindow(windowService, windowHandle, 130, 90);
    os::showWindow(windowService, windowHandle);
    render();
    while (true) {
        os::WindowEvent event{};
        if (!os::getNextWindowEvent(windowService, windowHandle, event)) break;
        if (event.type == os::WindowEventType::WindowClose) break;
        if (event.type == os::WindowEventType::KeyDown) {
            handleKey(event);
        } else if (event.type == os::WindowEventType::MouseButtonDown &&
                   event.y >= 0 && event.y < 30) {
            const uint32_t selected = static_cast<uint32_t>(event.x) / tabWidth;
            if (selected < tabCount) {
                activeTab = selected;
                render();
            } else if (selected == tabCount && createTab()) {
                render();
            }
        } else if (event.type == os::WindowEventType::WindowResize) {
            const os::TerminalWindowSize size{
                terminalRows, terminalColumns,
                static_cast<uint32_t>(event.x),
                static_cast<uint32_t>(event.y > 30 ? event.y - 30 : 1)};
            os::setPtyWindowSize(tabs[activeTab].pty.masterHandle, size);
            render();
        }
    }
    return 0;
}
