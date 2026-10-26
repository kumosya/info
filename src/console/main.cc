#include "service/common/queries.h"
#include "service/common/records.h"
#include "service/common/channel.h"
#include "service/console/client.h"
#include "service/common/runtime.h"
#include "service/common/kernel-requests.h"
#include "service/common/requests.h"
#include <os/task.h>
#include <csignal>

#include <os/ipc.h>
#include <os/task.h>
#include <os/mm.h>
#include <os/device.h>
#include <os/service.h>
#include <os/console.h>
#include <os/file.h>
#include "kernel/graphic/font.h"
#include <os/input.h>
#include <os/ipc.h>
#include <os/service.h>
#include <os/sync.h>
#include "service/console/boot.h"

namespace {

constexpr uint64_t framebufferAddress = 0x0000000051000000ULL;
constexpr uint64_t inputStackAddress  = 0x0000000062000000ULL;
constexpr uint64_t inputStackSize     = 64 * 1024;
constexpr uint32_t cellWidth          = 8;
constexpr uint32_t cellHeight         = 16;
constexpr uint32_t maximumColumns     = 128;
constexpr uint32_t maximumRows        = 48;
constexpr uint32_t inputCapacity      = 512;
constexpr uint32_t lineCapacity       = 256;
constexpr uint32_t historyCapacity    = 16;

struct Cell {
    char character;
    uint8_t foreground;
    uint8_t background;
    uint8_t attributes;
};

class DisplayConsole {
   public:
    bool init() {
        if(info_.open(framebufferAddress) || !info_.getAddress() ||
           info_.getWidth()<cellWidth || info_.getHeight()<cellHeight) return false;
        surface_={reinterpret_cast<uint8_t*>(info_.getAddress()),info_.getWidth(),info_.getHeight(),info_.getPitch(),
            uint8_t(info_.getBitsPerPixel()),info_.getRedPosition(),info_.getGreenPosition(),info_.getBluePosition()};
        return graphic::valid(surface_);
    }

    uint32_t columns() const {
        const uint32_t value = info_.getWidth() / cellWidth;
        return value > maximumColumns ? maximumColumns : value;
    }

    uint32_t rows() const {
        const uint32_t value = info_.getHeight() / cellHeight;
        return value > maximumRows ? maximumRows : value;
    }

    void render(const Cell* cells, uint32_t columns, uint32_t rows,
                uint32_t cursorX, uint32_t cursorY) {
        for (uint32_t row = 0; row < rows; ++row) {
            for (uint32_t column = 0; column < columns; ++column) {
                const Cell& cell     = cells[row * columns + column];
                const uint32_t index = row * columns + column;
                const bool cursor    = column == cursorX && row == cursorY;
                const bool previousCursor =
                    column == previousCursorX_ && row == previousCursorY_;
                const Cell& previous = previousCells_[index];
                if (!valid_ || cursor || previousCursor ||
                    cell.character != previous.character ||
                    cell.foreground != previous.foreground ||
                    cell.background != previous.background ||
                    cell.attributes != previous.attributes) {
                    drawCell(column, row, cell, cursor);
                    previousCells_[index] = cell;
                }
            }
        }
        previousCursorX_ = cursorX;
        previousCursorY_ = cursorY;
        valid_           = true;
    }

    void invalidate() { valid_ = false; }

   private:
    void drawCell(uint32_t column, uint32_t row, const Cell& cell,
                  bool cursor) {
        const bool bright   = (cell.attributes & 1U) != 0;
        uint32_t foreground = os::Console::terminalColor(cell.foreground, bright);
        uint32_t background = os::Console::terminalColor(cell.background);
        if (cursor != ((cell.attributes & 4U) != 0)) {
            const uint32_t temporary = foreground;
            foreground               = background;
            background               = temporary;
        }
        graphic::drawGlyph(surface_,column*cellWidth,row*cellHeight,uint8_t(cell.character),
            foreground,background,(cell.attributes&2U)!=0);
    }

    graphic::Surface surface_{};
    os::Device::Frame info_{};
    Cell previousCells_[maximumColumns * maximumRows]{};
    uint32_t previousCursorX_ = 0;
    uint32_t previousCursorY_ = 0;
    bool valid_               = false;
};

class Tty {
   public:
    void init(uint32_t id, uint32_t columns, uint32_t rows, bool banner = true) {
        os::Runtime::clearMemory(this, sizeof(*this));
        insertMode_ = true;
        echo_ = true;
        id_         = id;
        columns_    = columns;
        rows_       = rows;
        clear();
        if (banner) {
            write("osinfo tty", 10);
            put(static_cast<char>('1' + id));
            put('\n');
        }
    }

    void write(const char* data, uint32_t size) {
        for (uint32_t index = 0; index < size; ++index) {
            consume(data[index]);
        }
    }

    void handleKey(const os::wire::KeyEvent& key) {
        os::Input::Key keyCode = navigationKey(key);
        if ((key.modifiers & os::Input::Modifier::Control) != 0 &&
            keyCode >= os::Input::Key::A && keyCode <= os::Input::Key::Z) {
            const char letter =
                static_cast<char>('A' + static_cast<uint32_t>(keyCode) -
                                  static_cast<uint32_t>(os::Input::Key::A));
            const char notation[] = {'^', letter};
            if (keyCode == os::Input::Key::C || keyCode == os::Input::Key::Z) {
                moveCursorForward(lineLength_ - lineCursor_);
                write(notation, 2);
                put('\n');
                lineLength_      = 0;
                lineCursor_      = 0;
                historyPosition_ = historyCount_;
                if (foregroundTask_ != 0) {
                    os::detail::TaskControl().sendSignal(static_cast<uint64_t>(-static_cast<int64_t>(foregroundTask_)),
                                   keyCode == os::Input::Key::C ? SIGINT
                                                            : SIGTSTP);
                } else {
                    enqueue('\n');
                }
            } else if (keyCode == os::Input::Key::D) {
                write(notation, 2);
                if (lineLength_ == 0) {
                    eofPending_ = true;
                } else {
                    commitLine(false);
                }
            } else if (keyCode == os::Input::Key::L) {
                clear();
                write("^L\n", 3);
            } else {
                write(notation, 2);
                appendControlCharacter(static_cast<char>(letter - 'A' + 1));
            }
            return;
        }
        if (!echo_) {
            if (keyCode == os::Input::Key::Enter || keyCode == os::Input::Key::KeypadEnter) {
                commitLine(true);
            } else if (keyCode == os::Input::Key::Backspace) {
                if (lineLength_) line_[--lineLength_] = 0;
                lineCursor_ = lineLength_;
            } else {
                os::wire::KeyEvent translated = key; translated.keyCode = keyCode;
                const char character = keyCharacter(translated);
                if (character && lineLength_ + 1 < lineCapacity) line_[lineLength_++] = character;
                lineCursor_ = lineLength_;
            }
            return;
        }
        if (keyCode == os::Input::Key::Left) {
            if (lineCursor_ != 0) {
                --lineCursor_;
                moveCursorBackward(1);
            }
            return;
        }
        if (keyCode == os::Input::Key::Right) {
            if (lineCursor_ < lineLength_) {
                ++lineCursor_;
                moveCursorForward(1);
            }
            return;
        }
        if (keyCode == os::Input::Key::Home) {
            moveCursorBackward(lineCursor_);
            lineCursor_ = 0;
            return;
        }
        if (keyCode == os::Input::Key::End) {
            moveCursorForward(lineLength_ - lineCursor_);
            lineCursor_ = lineLength_;
            return;
        }
        if (keyCode == os::Input::Key::Up || keyCode == os::Input::Key::PageUp) {
            recallHistory(keyCode == os::Input::Key::PageUp ? -16 : -1);
            return;
        }
        if (keyCode == os::Input::Key::Down || keyCode == os::Input::Key::PageDown) {
            recallHistory(keyCode == os::Input::Key::PageDown ? 16 : 1);
            return;
        }
        if (keyCode == os::Input::Key::Insert) {
            insertMode_ = !insertMode_;
            return;
        }
        if (keyCode == os::Input::Key::Backspace) {
            eraseInputCharacter(true);
            return;
        }
        if (keyCode == os::Input::Key::Delete) {
            eraseInputCharacter(false);
            return;
        }
        if (keyCode == os::Input::Key::Enter ||
            keyCode == os::Input::Key::KeypadEnter) {
            moveCursorForward(lineLength_ - lineCursor_);
            put('\n');
            commitLine(true);
            return;
        }
        if (keyCode == os::Input::Key::Tab) {
            for (uint32_t index = 0; index < 4; ++index) {
                appendInputCharacter(' ');
            }
            return;
        }
        os::wire::KeyEvent translated = key;
        translated.keyCode      = keyCode;
        const char character    = keyCharacter(translated);
        if (character != '\0') {
            appendInputCharacter(character);
        }
    }

    int64_t read(char* destination, uint32_t capacity) {
        if (inputCount_ == 0) {
            if (eofPending_) {
                eofPending_ = false;
                return 0;
            }
            return static_cast<int64_t>(os::ErrorCode::WouldBlock);
        }
        uint32_t count = inputCount_ < capacity ? inputCount_ : capacity;
        for (uint32_t index = 0; index < count; ++index) {
            destination[index] = input_[inputRead_];
            inputRead_         = (inputRead_ + 1) % inputCapacity;
        }
        inputCount_ -= count;
        return count;
    }

    const Cell* cells() const { return cells_; }
    uint32_t cursorX() const { return cursorX_; }
    uint32_t cursorY() const { return cursorY_; }
    void resetSession() {
        inputRead_ = inputWrite_ = inputCount_ = lineLength_ = lineCursor_ = 0;
        historyCount_ = historyPosition_ = 0;
        os::Runtime::clearMemory(line_, sizeof(line_));
        os::Runtime::clearMemory(history_, sizeof(history_));
        setForegroundTask(0);
        echo_ = insertMode_ = true;
        eofPending_ = false;
        ansi_ = {};
        style_ = {};
    }
    int setForegroundTask(uint64_t taskId) {
        os::IpcMessage request{}, reply{};
        request.type = static_cast<uint64_t>(os::TaskOperation::SetTerminalForeground); request.length = 4;
        request.data[0] = 1; request.data[1] = id_; request.data[2] = taskId;
        const int status=services::RequestChannel(os::CoreServiceKind::Task).result(request,reply);
        if (status) return status;
        foregroundTask_=taskId;
        if (!taskId) echo_=true;
        return 0;
    }
    uint64_t foregroundGroup() const { return foregroundTask_; }
    bool echo() const { return echo_; }
    void setEcho(bool enabled) { echo_ = enabled; }

   private:
    static os::Input::Key navigationKey(const os::wire::KeyEvent& key) {
        const bool numeric = (key.modifiers & os::Input::Modifier::NumLock) != 0 &&
                             (key.modifiers & os::Input::Modifier::Shift) == 0;
        if (numeric) {
            return key.keyCode;
        }
        switch (key.keyCode) {
            case os::Input::Key::Keypad0:
                return os::Input::Key::Insert;
            case os::Input::Key::Keypad1:
                return os::Input::Key::End;
            case os::Input::Key::Keypad2:
                return os::Input::Key::Down;
            case os::Input::Key::Keypad3:
                return os::Input::Key::PageDown;
            case os::Input::Key::Keypad4:
                return os::Input::Key::Left;
            case os::Input::Key::Keypad6:
                return os::Input::Key::Right;
            case os::Input::Key::Keypad7:
                return os::Input::Key::Home;
            case os::Input::Key::Keypad8:
                return os::Input::Key::Up;
            case os::Input::Key::Keypad9:
                return os::Input::Key::PageUp;
            case os::Input::Key::KeypadDecimal:
                return os::Input::Key::Delete;
            default:
                return key.keyCode;
        }
    }

    static char keyCharacter(const os::wire::KeyEvent& key) {
        const bool shift = (key.modifiers & os::Input::Modifier::Shift) != 0;
        const bool caps  = (key.modifiers & os::Input::Modifier::CapsLock) != 0;
        if (key.keyCode >= os::Input::Key::A && key.keyCode <= os::Input::Key::Z) {
            const uint32_t index = static_cast<uint32_t>(key.keyCode) -
                                   static_cast<uint32_t>(os::Input::Key::A);
            return static_cast<char>(((shift != caps) ? 'A' : 'a') + index);
        }
        if (key.keyCode >= os::Input::Key::Digit1 &&
            key.keyCode <= os::Input::Key::Digit9) {
            constexpr char normal[]  = "123456789";
            constexpr char shifted[] = "!@#$%^&*(";
            const uint32_t index     = static_cast<uint32_t>(key.keyCode) -
                                   static_cast<uint32_t>(os::Input::Key::Digit1);
            return shift ? shifted[index] : normal[index];
        }
        switch (key.keyCode) {
            case os::Input::Key::Digit0:
                return shift ? ')' : '0';
            case os::Input::Key::Space:
                return ' ';
            case os::Input::Key::Minus:
                return shift ? '_' : '-';
            case os::Input::Key::Equal:
                return shift ? '+' : '=';
            case os::Input::Key::LeftBracket:
                return shift ? '{' : '[';
            case os::Input::Key::RightBracket:
                return shift ? '}' : ']';
            case os::Input::Key::Semicolon:
                return shift ? ':' : ';';
            case os::Input::Key::Apostrophe:
                return shift ? '"' : '\'';
            case os::Input::Key::Grave:
                return shift ? '~' : '`';
            case os::Input::Key::Backslash:
                return shift ? '|' : '\\';
            case os::Input::Key::Comma:
                return shift ? '<' : ',';
            case os::Input::Key::Period:
                return shift ? '>' : '.';
            case os::Input::Key::Slash:
                return shift ? '?' : '/';
            case os::Input::Key::Keypad0:
                return '0';
            case os::Input::Key::Keypad1:
                return '1';
            case os::Input::Key::Keypad2:
                return '2';
            case os::Input::Key::Keypad3:
                return '3';
            case os::Input::Key::Keypad4:
                return '4';
            case os::Input::Key::Keypad5:
                return '5';
            case os::Input::Key::Keypad6:
                return '6';
            case os::Input::Key::Keypad7:
                return '7';
            case os::Input::Key::Keypad8:
                return '8';
            case os::Input::Key::Keypad9:
                return '9';
            case os::Input::Key::KeypadDecimal:
                return '.';
            case os::Input::Key::KeypadDivide:
                return '/';
            case os::Input::Key::KeypadMultiply:
                return '*';
            case os::Input::Key::KeypadMinus:
                return '-';
            case os::Input::Key::KeypadPlus:
                return '+';
            default:
                return '\0';
        }
    }

    void appendInputCharacter(char character) {
        if (lineLength_ + 1 >= lineCapacity) {
            return;
        }
        const uint32_t redrawStart = lineCursor_;
        if (insertMode_ || lineCursor_ == lineLength_) {
            for (uint32_t index = lineLength_; index > lineCursor_; --index) {
                line_[index] = line_[index - 1];
            }
            ++lineLength_;
        }
        line_[lineCursor_++] = character;
        for (uint32_t index = redrawStart; index < lineLength_; ++index) {
            put(line_[index]);
        }
        moveCursorBackward(lineLength_ - lineCursor_);
        historyPosition_ = historyCount_;
    }

    void appendControlCharacter(char character) { enqueue(character); }

    void eraseInputCharacter(bool backward) {
        if ((backward && lineCursor_ == 0) ||
            (!backward && lineCursor_ == lineLength_)) {
            return;
        }
        if (backward) {
            --lineCursor_;
            moveCursorBackward(1);
        }
        for (uint32_t index = lineCursor_; index + 1 < lineLength_; ++index) {
            line_[index] = line_[index + 1];
        }
        --lineLength_;
        for (uint32_t index = lineCursor_; index < lineLength_; ++index) {
            put(line_[index]);
        }
        put(' ');
        moveCursorBackward(lineLength_ - lineCursor_ + 1);
        historyPosition_ = historyCount_;
    }

    void commitLine(bool newline) {
        if (echo_) rememberLine();
        for (uint32_t index = 0; index < lineLength_; ++index) {
            enqueue(line_[index]);
        }
        if (newline) {
            enqueue('\n');
        }
        lineLength_      = 0;
        lineCursor_      = 0;
        historyPosition_ = historyCount_;
    }

    void rememberLine() {
        if (lineLength_ == 0) {
            return;
        }
        if (historyCount_ == historyCapacity) {
            for (uint32_t index = 1; index < historyCapacity; ++index) {
                for (uint32_t character = 0; character < lineCapacity;
                     ++character) {
                    history_[index - 1][character] = history_[index][character];
                }
            }
            --historyCount_;
        }
        uint32_t count =
            lineLength_ < lineCapacity - 1 ? lineLength_ : lineCapacity - 1;
        for (uint32_t index = 0; index < count; ++index) {
            history_[historyCount_][index] = line_[index];
        }
        history_[historyCount_][count] = '\0';
        ++historyCount_;
    }

    void recallHistory(int32_t direction) {
        if (historyCount_ == 0) {
            return;
        }
        int32_t position = static_cast<int32_t>(historyPosition_) + direction;
        if (position < 0) position = 0;
        if (position > static_cast<int32_t>(historyCount_)) {
            position = static_cast<int32_t>(historyCount_);
        }
        historyPosition_ = static_cast<uint32_t>(position);
        char replacement[lineCapacity]{};
        uint32_t length = 0;
        if (historyPosition_ < historyCount_) {
            while (length + 1 < lineCapacity &&
                   history_[historyPosition_][length] != '\0') {
                replacement[length] = history_[historyPosition_][length];
                ++length;
            }
        }
        replaceInputLine(replacement, length);
    }

    void replaceInputLine(const char* replacement, uint32_t length) {
        const uint32_t oldLength = lineLength_;
        moveCursorBackward(lineCursor_);
        for (uint32_t index = 0; index < length; ++index) {
            line_[index] = replacement[index];
            put(replacement[index]);
        }
        for (uint32_t index = length; index < oldLength; ++index) {
            put(' ');
        }
        if (oldLength > length) {
            moveCursorBackward(oldLength - length);
        }
        lineLength_ = length;
        lineCursor_ = length;
    }

    void moveCursorBackward(uint32_t count) {
        while (count-- != 0) {
            if (cursorX_ != 0) {
                --cursorX_;
            } else if (cursorY_ != 0) {
                --cursorY_;
                cursorX_ = columns_ - 1;
            }
        }
    }

    void moveCursorForward(uint32_t count) {
        while (count-- != 0) {
            if (++cursorX_ == columns_) {
                cursorX_ = 0;
                if (cursorY_ + 1 < rows_) {
                    ++cursorY_;
                }
            }
        }
    }

    void enqueue(char character) {
        if (inputCount_ == inputCapacity) {
            return;
        }
        input_[inputWrite_] = character;
        inputWrite_         = (inputWrite_ + 1) % inputCapacity;
        ++inputCount_;
    }

    void consume(char character) {
        const auto result = ansi_.feed(static_cast<uint8_t>(character));
        if (result == os::Console::EscapeParser::Result::Text) put(character);
        else if (result == os::Console::EscapeParser::Result::Command)
            applyAnsi(ansi_.getCommand());
    }

    void applyAnsi(char command) {
        const uint16_t parameterOne_ = ansi_.getParameter(0);
        const uint16_t parameterTwo_ = ansi_.getParameter(1);
        const uint32_t first = parameterOne_ == 0 ? 1 : parameterOne_;
        if (command == 'A') {
            cursorY_ = first > cursorY_ ? 0 : cursorY_ - first;
        } else if (command == 'B') {
            cursorY_ = cursorY_ + first >= rows_ ? rows_ - 1 : cursorY_ + first;
        } else if (command == 'C') {
            cursorX_ =
                cursorX_ + first >= columns_ ? columns_ - 1 : cursorX_ + first;
        } else if (command == 'D') {
            cursorX_ = first > cursorX_ ? 0 : cursorX_ - first;
        } else if (command == 'H') {
            cursorY_              = first > rows_ ? rows_ - 1 : first - 1;
            const uint32_t column = parameterTwo_ == 0 ? 1 : parameterTwo_;
            cursorX_ = column > columns_ ? columns_ - 1 : column - 1;
        } else if (command == 'J' &&
                   (parameterOne_ == 0 || parameterOne_ == 2)) {
            clear();
        } else if (command == 'K') {
            for (uint32_t column = cursorX_; column < columns_; ++column) {
                cells_[cursorY_ * columns_ + column] = blankCell();
            }
        } else if (command == 'm') {
            for (uint32_t i = 0; i < ansi_.getParameterCount(); ++i)
                style_.apply(ansi_.getParameter(i));
        }
    }

    void put(char character) {
        if (character == '\r') {
            cursorX_ = 0;
            return;
        }
        if (character == '\n') {
            cursorX_ = 0;
            advanceRow();
            return;
        }
        if (character == '\b') {
            if (cursorX_ != 0) {
                --cursorX_;
                cells_[cursorY_ * columns_ + cursorX_] = blankCell();
            }
            return;
        }
        if (character == '\t') {
            do {
                put(' ');
            } while ((cursorX_ & 3U) != 0);
            return;
        }
        if (character < 32 || character > 126) {
            return;
        }
        cells_[cursorY_ * columns_ + cursorX_] = {character, style_.getForeground(),
                                                  style_.getBackground(), style_.getAttributes()};
        if (++cursorX_ == columns_) {
            cursorX_ = 0;
            advanceRow();
        }
    }

    void advanceRow() {
        if (++cursorY_ < rows_) {
            return;
        }
        for (uint32_t row = 1; row < rows_; ++row) {
            for (uint32_t column = 0; column < columns_; ++column) {
                cells_[(row - 1) * columns_ + column] =
                    cells_[row * columns_ + column];
            }
        }
        for (uint32_t column = 0; column < columns_; ++column) {
            cells_[(rows_ - 1) * columns_ + column] = blankCell();
        }
        cursorY_ = rows_ - 1;
    }

    Cell blankCell() const { return {' ', 7, 0, 0}; }

    void clear() {
        for (uint32_t index = 0; index < maximumColumns * maximumRows;
             ++index) {
            cells_[index] = blankCell();
        }
        cursorX_ = cursorY_ = 0;
        style_ = {};
        ansi_ = {};
    }

    uint32_t id_;
    uint32_t columns_;
    uint32_t rows_;
    Cell cells_[maximumColumns * maximumRows];
    char input_[inputCapacity];
    char line_[lineCapacity];
    char history_[historyCapacity][lineCapacity];
    uint32_t inputRead_;
    uint32_t inputWrite_;
    uint32_t inputCount_;
    uint32_t lineLength_;
    uint32_t lineCursor_;
    uint32_t historyCount_;
    uint32_t historyPosition_;
    uint64_t foregroundTask_;
    bool echo_;
    uint32_t cursorX_;
    uint32_t cursorY_;
    os::Console::Style style_;
    os::Console::EscapeParser ansi_;
    bool eofPending_;
    bool insertMode_;
};

Tty terminalStorage[os::textTerminalCount];
Tty bootStorage;

class ConsoleService {
   public:
    bool init(uint64_t registry) {
        registry_ = registry;
        endpoint_ = os::detail::EndpointControl().create();
        if (registry_ == 0 || endpoint_ == 0 || !display_.init() ||
            !os::detail::RegistryClient(registry_).registerName(os::consoleServiceName, endpoint_) ||
            !os::detail::RegistryClient(registry_).registerName(os::terminalServiceName, endpoint_)) {
            return false;
        }
        for (uint32_t index = 0; index < os::textTerminalCount; ++index) {
            terminals_[index].init(index, display_.columns(),
                                         display_.rows());
        }
        loadConfiguration();
        // A restarted console takes the framebuffer back before showing boot output.
        const uint64_t window = os::detail::RegistryClient(registry_).lookup("guiw", 1);
        if (window) {
            const bool hidden = os::DisplayHandoff().setGraphicalActive(window, false);
            os::closeHandle(window);
            if (!hidden) return false;
        }
        bootStorage.init(0, display_.columns(), display_.rows(), false);
        const char heading[] = "osinfo: starting services...\n\n";
        bootStorage.write(heading, sizeof(heading) - 1);
        redrawLocked();
        return true;
    }

    uint64_t endpoint() const { return endpoint_; }

    void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        os::SpinLock::Guard guard(lock_);
        const auto operation = static_cast<os::TerminalRequest>(request.type);
        if (operation == os::TerminalRequest::GetBootState) {
            reply.data[0] = request.length || request.handleCount ? static_cast<int64_t>(os::ErrorCode::InvalidArgument) : 0;
            reply.data[1] = boot_.active() ? 1 : 0;
            reply.data[2] = defaultTerminal_;
            reply.length = 3;
        } else if (operation == os::TerminalRequest::BootStatus || operation == os::TerminalRequest::BootComplete) {
            os::wire::TaskInfo caller{};
            if (os::detail::TaskQuery().query(request.senderPid, caller) || !console::BootState::authorized(caller))
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied);
            else if (operation == os::TerminalRequest::BootStatus ? !console::BootState::validStatus(request) : !console::BootState::validComplete(request))
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            else if (!boot_.active())
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::Busy);
            else if (operation == os::TerminalRequest::BootStatus) {
                const char* prefix = request.data[0] ? "\x1b[32m[ OK ]\x1b[0m Started " : "\x1b[31m[FAILED]\x1b[0m Failed to start ";
                bootStorage.write(prefix, os::Runtime::stringLength(prefix));
                const char* name = reinterpret_cast<const char*>(&request.data[2]);
                bootStorage.write(name, os::Runtime::stringLength(name));
                bootStorage.write(".\n", 2);
                redrawLocked(); reply.data[0] = 0;
            } else {
                reply.data[0] = switchLocked(static_cast<uint32_t>(request.data[0]), true) ? 0 : static_cast<int64_t>(os::ErrorCode::IoError);
            }
        } else if (operation == os::TerminalRequest::Write) {
            const uint32_t terminal = static_cast<uint32_t>(request.data[0]);
            uint32_t length         = static_cast<uint32_t>(request.data[1]);
            if (terminal >= os::textTerminalCount ||
                length > 512 || request.length < 2 + (length + 7) / 8) {
                reply.data[0] = static_cast<uint64_t>(
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument));
            } else {
                const char* data =
                    reinterpret_cast<const char*>(&request.data[2]);
                terminals_[terminal].write(data, length);
                if (serialConsole_) {
                    char serial[513]{};
                    for (uint32_t index = 0; index < length; ++index) {
                        serial[index] = data[index];
                    }
                    os::Sys().log(serial);
                }
                if (!boot_.active() && activeTerminal_ == terminal) {
                    redrawLocked();
                }
                reply.data[0] = 0;
                reply.data[1] = length;
                reply.length  = 2;
            }
        } else if (operation == os::TerminalRequest::Read) {
            const uint32_t terminal = static_cast<uint32_t>(request.data[0]);
            uint32_t capacity       = static_cast<uint32_t>(request.data[1]);
            if (terminal >= os::textTerminalCount || capacity == 0) {
                reply.data[0] = static_cast<uint64_t>(
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument));
            } else {
                if (capacity > 6 * sizeof(uint64_t)) {
                    capacity = 6 * sizeof(uint64_t);
                }
                char data[6 * sizeof(uint64_t)]{};
                const int64_t count = terminals_[terminal].read(data, capacity);
                if (count < 0) {
                    reply.data[0] = static_cast<uint64_t>(count);
                } else {
                    reply.data[0] = 0;
                    reply.data[1] = static_cast<uint64_t>(count);
                    for (int64_t index = 0; index < count; ++index) {
                        reinterpret_cast<char*>(&reply.data[2])[index] =
                            data[index];
                    }
                    reply.length = 8;
                }
            }
        } else if (operation == os::TerminalRequest::GetState) {
            if (request.length!=1 || request.handleCount || request.data[0]>os::graphicalTerminalId)
                reply.data[0]=static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            else {
                const uint32_t id=static_cast<uint32_t>(request.data[0]);
                reply.data[0]=0; reply.data[1]=!boot_.active() && activeTerminal_==id;
                reply.data[2]=id<os::textTerminalCount?terminals_[id].echo():false;
                reply.data[3]=id<os::textTerminalCount?terminals_[id].foregroundGroup():0;
                reply.length=4;
            }
        } else if (operation == os::TerminalRequest::GetActive) {
            reply.data[0] = 0;
            reply.data[1] = boot_.active() ? static_cast<uint64_t>(-1) : activeTerminal_;
            reply.length  = 2;
        } else if (operation == os::TerminalRequest::Switch) {
            reply.data[0] = boot_.active() ? static_cast<int64_t>(os::ErrorCode::Busy) :
                request.length != 1 || request.data[0] > os::graphicalTerminalId ? static_cast<int64_t>(os::ErrorCode::InvalidArgument) :
                switchLocked(static_cast<uint32_t>(request.data[0])) ? 0 : static_cast<int64_t>(os::ErrorCode::IoError);
        } else if (operation == os::TerminalRequest::GetInfo) {
            reply.data[0] = 0;
            reply.data[1] = os::textTerminalCount;
            reply.data[2] = os::graphicalTerminalId;
            reply.data[3] = display_.columns();
            reply.data[4] = display_.rows();
            reply.length  = 5;
        } else if (operation == os::TerminalRequest::GetEcho || operation == os::TerminalRequest::SetEcho) {
            const uint32_t terminal = static_cast<uint32_t>(request.data[0]);
            if (request.length!=(operation==os::TerminalRequest::SetEcho?2U:1U) || request.data[0]>=os::textTerminalCount ||
                (operation==os::TerminalRequest::SetEcho && request.data[1]>1)) reply.data[0]=static_cast<int64_t>(os::ErrorCode::InvalidArgument);
            else {
                reply.data[0] = 0;
                reply.data[1] = terminals_[terminal].echo();
                reply.length = 2;
                if (operation == os::TerminalRequest::SetEcho) terminals_[terminal].setEcho(request.data[1] != 0);
            }
        } else if (operation == os::TerminalRequest::ResetSession) {
            os::wire::TaskInfo caller{};
            if (request.length != 1 || request.data[0] >= os::textTerminalCount ||
                os::detail::TaskQuery().query(request.senderPid, caller) || caller.serviceRole != static_cast<uint32_t>(os::ServiceRole::Init))
                reply.data[0] = static_cast<int64_t>(os::ErrorCode::PermissionDenied);
            else { terminals_[request.data[0]].resetSession(); reply.data[0] = 0; }
        } else if (operation ==
                   os::TerminalRequest::SetForegroundGroup) {
            const uint32_t terminal = static_cast<uint32_t>(request.data[0]);
            if (request.length!=2 || request.data[0]>=os::textTerminalCount) {
                reply.data[0] = static_cast<uint64_t>(
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument));
            } else {
                reply.data[0]=terminals_[terminal].setForegroundTask(request.data[1]);
            }
        } else {
            reply.data[0] = static_cast<uint64_t>(
                static_cast<int64_t>(os::ErrorCode::NotSupported));
        }
        if (reply.length == 0) {
            reply.length = 1;
        }
    }

    void handleInput(const os::Input::Event& event) {
        os::wire::InputEvent record{};record.type=event.getType();record.timestamp=event.getTimestamp();
        record.key.keyCode=event.getKeyCode();record.key.modifiers=event.getModifiers();record.key.pressed=event.getPressed();handleInput(record);
    }
    void handleInput(const os::wire::InputEvent& event) {
        if (event.type != os::Input::Event::Type::KeyDown) {
            return;
        }
        os::SpinLock::Guard guard(lock_);
        if (boot_.active()) return;
        const bool switchChord =
            (event.key.modifiers & os::Input::Modifier::Control) != 0 &&
            (event.key.modifiers & os::Input::Modifier::Shift) != 0 &&
            event.key.keyCode >= os::Input::Key::F1 &&
            event.key.keyCode <= os::Input::Key::F7;
        if (switchChord) {
            const uint32_t terminal = static_cast<uint32_t>(event.key.keyCode) -
                                      static_cast<uint32_t>(os::Input::Key::F1);
            switchLocked(terminal);
        } else if (activeTerminal_ < os::textTerminalCount) {
            terminals_[activeTerminal_].handleKey(event.key);
            redrawLocked();
        }
    }

   private:
    bool switchLocked(uint32_t terminal, bool finishingBoot = false) {
        if ((boot_.active() && !finishingBoot) || terminal > os::graphicalTerminalId) {
            return false;
        }
        const uint64_t window =
            os::detail::RegistryClient(registry_).lookup("guiw", terminal == os::graphicalTerminalId ? 100 : 1);
        if (window != 0) {
            const bool changed = os::DisplayHandoff().setGraphicalActive(window, terminal == os::graphicalTerminalId);
            os::closeHandle(window);
            if (!changed) return false;
        } else if (terminal == os::graphicalTerminalId) {
            return false;
        }
        if (finishingBoot) boot_.complete();
        activeTerminal_                       = terminal;
        constexpr const char* terminalNames[] = {
            "[CONSOLE] switched to tty1\n", "[CONSOLE] switched to tty2\n",
            "[CONSOLE] switched to tty3\n", "[CONSOLE] switched to tty4\n",
            "[CONSOLE] switched to tty5\n", "[CONSOLE] switched to tty6\n",
            "[CONSOLE] switched to tty7\n"};
        os::Sys().log(terminalNames[activeTerminal_]);
        if (activeTerminal_ < os::textTerminalCount) {
            display_.invalidate();
            redrawLocked();
        }
        return true;
    }

    void redrawLocked() {
        if (boot_.active()) {
            display_.render(bootStorage.cells(), display_.columns(), display_.rows(), maximumColumns, maximumRows);
            return;
        }
        if (activeTerminal_ >= os::textTerminalCount) return;
        Tty& terminal = terminals_[activeTerminal_];
        display_.render(terminal.cells(), display_.columns(), display_.rows(),
                        terminal.cursorX(), terminal.cursorY());
    }

    void loadConfiguration() {
        defaultTerminal_ = 0;
        serialConsole_   = false;
        os::File file;
        if (file.open("/etc/console", O_RDONLY)) return;
        char configuration[256]{};
        const int64_t count =
            file.read(configuration, sizeof(configuration) - 1);
        if (count <= 0) {
            return;
        }
        configuration[count]         = '\0';
        constexpr char terminalKey[] = "defaultTerminal=tty";
        constexpr char serialKey[]   = "serialConsole=on";
        for (int64_t index = 0; index < count; ++index) {
            uint32_t match = 0;
            while (terminalKey[match] != '\0' && index + match < count &&
                   configuration[index + match] == terminalKey[match]) {
                ++match;
            }
            if (terminalKey[match] == '\0' && index + match < count &&
                configuration[index + match] >= '1' &&
                configuration[index + match] <= '7') {
                defaultTerminal_ =
                    static_cast<uint32_t>(configuration[index + match] - '1');
            }
            match = 0;
            while (serialKey[match] != '\0' && index + match < count &&
                   configuration[index + match] == serialKey[match]) {
                ++match;
            }
            if (serialKey[match] == '\0') {
                serialConsole_ = true;
            }
        }
    }

    DisplayConsole display_;
    Tty* terminals_ = terminalStorage;
    os::SpinLock lock_;
    uint64_t registry_        = 0;
    uint64_t endpoint_        = 0;
    uint32_t activeTerminal_  = 0;
    uint32_t defaultTerminal_ = 0;
    bool serialConsole_       = false;
    console::BootState boot_;
};

class ConsoleApplication {
 public:
    static void inputMain(void* self) { static_cast<ConsoleApplication*>(self)->inputLoop(); }
ConsoleService consoleService;
uint64_t registryEndpoint;

void inputLoop() {
    os::Input client;
    if (client.open()) {
        os::Sys().log("[CONSOLE] input service unavailable\n");
        return;
    }
    while (true) {
        if (client.wait(os::ipcInfiniteDeadline)) {
            os::Sys().log("[CONSOLE] input subscription failed\n");
            return;
        }
        os::Input::Event event{};
        while (client.receive(event)) {
            consoleService.handleInput(event);
        }
    }
}


int run() {
    os::Sys().log("[CONSOLE] Console and TTY service started in Ring 3\n");
    registryEndpoint = os::getBootstrapHandle();
    if (!consoleService.init(registryEndpoint) ||
        os::mapMemory(inputStackAddress, inputStackSize, 1) !=
            inputStackAddress ||
        os::createThread(inputMain, inputStackAddress + inputStackSize, this) == 0) {
        os::Sys().log("[CONSOLE] initialization failed\n");
        return 1;
    }
    os::Sys().log("[CONSOLE] boot display online; waiting for init handoff\n");
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(consoleService.endpoint(), request) != 0) {
            continue;
        }
        os::IpcMessage reply{};
        consoleService.handle(request, reply);
        for (uint32_t index = 0; index < request.handleCount; ++index)
            os::closeHandle(request.handles[index]);
        os::replyMessage(consoleService.endpoint(), request.replyToken, reply);
    }
}
};
}  // namespace
int main(int, char**) {
    static ConsoleApplication application;
    return application.run();
}
