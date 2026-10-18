#include <mutex>

#include "os/abi.h"
#include "os/ansi.h"
#include "osgui/gui.h"
#include "os/input.h"
#include "os/task.h"
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

struct Cell {
    char character = ' ';
    uint8_t foreground = 7;
    uint8_t background = 0;
    uint8_t attributes = 0;
};

struct TerminalTab {
    os::PtyHandles pty;
    os::TaskHandle shell;
    Cell cells[terminalRows][terminalColumns];
    uint32_t cursorX;
    uint32_t cursorY;
    os::terminal::EscapeParser parser;
    os::terminal::Style style;
    bool active;
};

uint64_t windowService;
os::WindowHandle windowHandle;
TerminalTab tabs[tabCapacity];
uint32_t tabCount;
uint32_t activeTab;
std::mutex terminalLock;
bool dirty = true;
Cell painted[terminalRows][terminalColumns];
uint32_t paintedTab = tabCapacity;
uint32_t paintedTabCount;
uint32_t paintedCursorX;
uint32_t paintedCursorY;

void clearTab(TerminalTab& tab) {
    for (uint32_t row = 0; row < terminalRows; ++row) {
        for (uint32_t column = 0; column < terminalColumns; ++column) {
            tab.cells[row][column] = {};
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
        tab.cells[terminalRows - 1][column] = {};
    }
    tab.cursorY = terminalRows - 1;
}

void newLine(TerminalTab& tab) {
    tab.cursorX = 0;
    if (++tab.cursorY == terminalRows) scroll(tab);
}

Cell blank(const TerminalTab& tab) {
    return {' ', tab.style.foreground(), tab.style.background(), tab.style.attributes()};
}

void consumeOutput(TerminalTab& tab, const uint8_t* bytes, size_t size) {
    for (size_t index = 0; index < size; ++index) {
        const uint8_t value = bytes[index];
        const auto result = tab.parser.feed(value);
        if (result == os::terminal::EscapeParser::Result::Pending) continue;
        if (result == os::terminal::EscapeParser::Result::Command) {
            const uint32_t first = tab.parser.parameter(0);
            const uint32_t count = first ? first : 1;
            switch (tab.parser.command()) {
                case 'm':
                    for (uint32_t i = 0; i < tab.parser.count(); ++i)
                        tab.style.apply(tab.parser.parameter(i));
                    break;
                case 'J':
                    if (first == 0 || first == 2)
                        for (auto& row : tab.cells) for (auto& cell : row) cell = blank(tab);
                    break;
                case 'H': case 'f': {
                    tab.cursorY = count > terminalRows ? terminalRows - 1 : count - 1;
                    const uint32_t column = tab.parser.parameter(1) ? tab.parser.parameter(1) : 1;
                    tab.cursorX = column > terminalColumns ? terminalColumns - 1 : column - 1;
                    break;
                }
                case 'A': tab.cursorY = count > tab.cursorY ? 0 : tab.cursorY - count; break;
                case 'B': tab.cursorY = tab.cursorY + count < terminalRows ? tab.cursorY + count : terminalRows - 1; break;
                case 'C': tab.cursorX = tab.cursorX + count < terminalColumns ? tab.cursorX + count : terminalColumns - 1; break;
                case 'D': tab.cursorX = count > tab.cursorX ? 0 : tab.cursorX - count; break;
                case 'K':
                    for (uint32_t column = tab.cursorX; column < terminalColumns; ++column)
                        tab.cells[tab.cursorY][column] = blank(tab);
                    break;
            }
            continue;
        }
        if (value == '\r') tab.cursorX = 0;
        else if (value == '\n') newLine(tab);
        else if (value == 8 || value == 0x7f) { if (tab.cursorX) --tab.cursorX; }
        else if (value == '\t') {
            do {
                tab.cells[tab.cursorY][tab.cursorX++] = blank(tab);
                if (tab.cursorX == terminalColumns) newLine(tab);
            } while ((tab.cursorX & 7U) != 0);
        } else if (value >= 32) {
            tab.cells[tab.cursorY][tab.cursorX++] = {static_cast<char>(value),
                tab.style.foreground(), tab.style.background(), tab.style.attributes()};
            if (tab.cursorX == terminalColumns) newLine(tab);
        }
    }
}

void drawLabel(int32_t x, int32_t y, const char* text, uint32_t color) {
    os::Window::drawText(windowService, windowHandle, x, y, text, color);
}

void render() {
    std::lock_guard<std::mutex> guard(terminalLock);
    if (tabCount == 0 || !dirty) return;
    dirty = false;
    const bool full = paintedTab != activeTab || paintedTabCount != tabCount;
    if (full) {
    os::Window::fillRectangle(windowService, windowHandle, 0, 0, windowWidth,
                      windowHeight, os::terminal::Palette::color(0));
    for (uint32_t index = 0; index < tabCount; ++index) {
        const uint32_t color = index == activeTab ? 0x00313a43U : 0x00222930U;
        os::Window::fillRectangle(windowService, windowHandle,
                          static_cast<int32_t>(index * tabWidth), 0,
                          tabWidth - 1, 30, color);
        char title[] = "Terminal 1";
        title[9] = static_cast<char>('1' + index);
        drawLabel(static_cast<int32_t>(index * tabWidth + 12), 7, title,
                  0x00e7edf3U);
    }
    const int32_t plusX = static_cast<int32_t>(tabCount * tabWidth);
    os::Window::fillRectangle(windowService, windowHandle, plusX, 0, 36, 30,
                      0x002b8a65U);
    drawLabel(plusX + 14, 7, "+", 0x00ffffffU);
    }

    const TerminalTab& tab = tabs[activeTab];
    auto changed = [&](uint32_t row, uint32_t column) {
        const Cell& a = tab.cells[row][column];
        const Cell& b = painted[row][column];
        return full || a.character != b.character || a.foreground != b.foreground ||
            a.background != b.background || a.attributes != b.attributes ||
            (row == paintedCursorY && column == paintedCursorX) ||
            (row == tab.cursorY && column == tab.cursorX);
    };
    char line[48];
    for (uint32_t row = 0; row < terminalRows; ++row) {
        for (uint32_t start = 0; start < terminalColumns;) {
            if (!changed(row, start)) { ++start; continue; }
            const Cell& cell = tab.cells[row][start];
            uint32_t length = 0;
            while (start + length < terminalColumns && length < 47) {
                const Cell& next = tab.cells[row][start + length];
                if (next.foreground != cell.foreground || next.background != cell.background ||
                    next.attributes != cell.attributes || !changed(row, start + length)) break;
                line[length++] = next.character;
            }
            line[length] = 0;
            uint32_t foreground = os::terminal::Palette::color(cell.foreground, cell.attributes & 1U);
            uint32_t background = os::terminal::Palette::color(cell.background);
            if (cell.attributes & 4U) {
                const uint32_t temporary = foreground;
                foreground = background;
                background = temporary;
            }
            const int32_t x = 8 + static_cast<int32_t>(start * 8);
            const int32_t y = 38 + static_cast<int32_t>(row * 16);
            os::Window::fillRectangle(windowService, windowHandle, x, y, length * 8, 16, background);
            drawLabel(x, y, line, foreground);
            if (cell.attributes & 2U)
                os::Window::fillRectangle(windowService, windowHandle, x, y + 14, length * 8, 1, foreground);
            start += length;
        }
    }
    os::Window::fillRectangle(windowService, windowHandle,
                      8 + static_cast<int32_t>(tab.cursorX * 8),
                      38 + static_cast<int32_t>(tab.cursorY * 16), 8, 2,
                      0x0067d6a3U);
    os::Window::presentWindow(windowService, windowHandle);
    for (uint32_t row = 0; row < terminalRows; ++row)
        for (uint32_t column = 0; column < terminalColumns; ++column)
            painted[row][column] = tab.cells[row][column];
    paintedTab = activeTab;
    paintedTabCount = tabCount;
    paintedCursorX = tab.cursorX;
    paintedCursorY = tab.cursorY;
}

void readTab(uint32_t index) {
    uint8_t buffer[256];
    while (tabs[index].active) {
        const int64_t count =
            os::Pipe::readPipe(tabs[index].pty.masterHandle, buffer, sizeof(buffer));
        if (count <= 0) break;
        {
            std::lock_guard<std::mutex> guard(terminalLock);
            consumeOutput(tabs[index], buffer, static_cast<size_t>(count));
            if (index == activeTab) dirty = true;
        }

    }
    tabs[index].active = false;
}

void renderMain() {
    while (true) { render(); os::sleep(16); }
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
    if (!os::Pty::createPty(tab.pty)) return false;
    const char* arguments[] = {"sh"};
    tab.shell = os::Process::executeWithIo("/bin/sh", 1, arguments,
                                  tab.pty.slaveHandle, tab.pty.slaveHandle,
                                  tab.pty.slaveHandle);
    if (tab.shell == 0) {
        os::closeHandle(tab.pty.masterHandle);
        os::closeHandle(tab.pty.slaveHandle);
        return false;
    }
    os::closeHandle(tab.pty.slaveHandle);
    tab.pty.slaveHandle = 0;
    os::TaskInfo info{};
    if (os::Process::getTaskInfo(tab.shell, info) != 0 ||
        !os::Pty::setPtyForegroundGroup(tab.pty.masterHandle, info.id)) {
        return false;
    }
    const os::TerminalWindowSize size{terminalRows, terminalColumns,
                                      windowWidth, windowHeight - 30};
    os::Pty::setPtyWindowSize(tab.pty.masterHandle, size);
    tab.active = true;
    void (*entries[])() = {readTab0, readTab1, readTab2, readTab3};
    const uint64_t stackAddress = stackBase + stackSize * index;
    if (os::mapMemory(stackAddress, stackSize, 1) != stackAddress ||
        os::createThread(entries[index], stackAddress + stackSize) == 0) {
        tab.active = false;
        return false;
    }
    {
        std::lock_guard<std::mutex> guard(terminalLock);
        ++tabCount;
        activeTab = index;
        dirty = true;
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
        os::Pipe::writePipe(tabs[activeTab].pty.masterHandle, sequence, 3);
        return;
    }
    const char value = translateKey(event.keyCode, event.modifiers);
    if (value != 0) {
        os::Pipe::writePipe(tabs[activeTab].pty.masterHandle, &value, 1);
    }
}

}  // namespace

int main(int, char**) {
    const uint64_t registry = os::ServiceRegistry::serviceRegistry();
    windowService = os::ServiceRegistry::lookupService(registry, os::guiwServiceName, 30000);
    if (windowService == 0) return 1;
    windowHandle = os::Window::createWindow(windowService, windowWidth, windowHeight,
                                    "Terminal");
    if (windowHandle == 0 || !createTab()) return 2;
    os::Window::moveWindow(windowService, windowHandle, 130, 90);
    os::Window::showWindow(windowService, windowHandle);
    render();
    const uint64_t renderStack = stackBase + stackSize * tabCapacity;
    if (os::mapMemory(renderStack, stackSize, 1) != renderStack ||
        os::createThread(renderMain, renderStack + stackSize) == 0) return 3;
    while (true) {
        os::WindowEvent event{};
        if (!os::Window::getNextWindowEvent(windowService, windowHandle, event)) break;
        if (event.type == os::WindowEventType::WindowClose) break;
        if (event.type == os::WindowEventType::KeyDown) {
            handleKey(event);
        } else if (event.type == os::WindowEventType::MouseButtonDown &&
                   event.y >= 0 && event.y < 30) {
            const uint32_t selected = static_cast<uint32_t>(event.x) / tabWidth;
            if (selected < tabCount) {
                { std::lock_guard<std::mutex> guard(terminalLock);
                  activeTab = selected; dirty = true; }
                render();
            } else if (selected == tabCount && createTab()) {
                render();
            }
        } else if (event.type == os::WindowEventType::WindowResize) {
            const os::TerminalWindowSize size{
                terminalRows, terminalColumns,
                static_cast<uint32_t>(event.x),
                static_cast<uint32_t>(event.y > 30 ? event.y - 30 : 1)};
            os::Pty::setPtyWindowSize(tabs[activeTab].pty.masterHandle, size);
            { std::lock_guard<std::mutex> guard(terminalLock); dirty = true; }
            render();
        }
    }
    return 0;
}
