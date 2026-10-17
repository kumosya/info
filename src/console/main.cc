#include <signal.h>

#include "os/abi.h"
#include "os/ansi.h"
#include "os/file.h"
#include "osgui/font.h"
#include "osgui/gui.h"
#include "os/input.h"
#include "os/protocol.h"
#include "os/service.h"
#include "os/sync.h"
#include "os/terminal.h"

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
        return os::mapFramebuffer(framebufferAddress, info_) == 0 &&
               info_.address != 0 && info_.width >= cellWidth &&
               info_.height >= cellHeight &&
               (info_.bitsPerPixel == 24 || info_.bitsPerPixel == 32);
    }

    uint32_t columns() const {
        const uint32_t value = info_.width / cellWidth;
        return value > maximumColumns ? maximumColumns : value;
    }

    uint32_t rows() const {
        const uint32_t value = info_.height / cellHeight;
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
        uint32_t foreground = os::terminal::color(cell.foreground, bright);
        uint32_t background = os::terminal::color(cell.background);
        if (cursor != ((cell.attributes & 4U) != 0)) {
            const uint32_t temporary = foreground;
            foreground               = background;
            background               = temporary;
        }
        for (uint32_t pixelY = 0; pixelY < cellHeight; ++pixelY) {
            const uint8_t bits =
                os::consoleFont[static_cast<uint8_t>(cell.character)][pixelY];
            for (uint32_t pixelX = 0; pixelX < cellWidth; ++pixelX) {
                const bool set = (bits & (1U << (7 - pixelX))) != 0;
                const bool underline =
                    (cell.attributes & 2U) != 0 && pixelY == cellHeight - 2;
                drawPixel(column * cellWidth + pixelX,
                          row * cellHeight + pixelY,
                          set || underline ? foreground : background);
            }
        }
    }

    void drawPixel(uint32_t x, uint32_t y, uint32_t colorValue) {
        uint32_t packed = (((colorValue >> 16) & 0xffU) << info_.redPosition) |
                          (((colorValue >> 8) & 0xffU) << info_.greenPosition) |
                          ((colorValue & 0xffU) << info_.bluePosition);
        uint8_t* pixel = reinterpret_cast<uint8_t*>(info_.address) +
                         static_cast<uint64_t>(y) * info_.pitch +
                         static_cast<uint64_t>(x) * (info_.bitsPerPixel / 8);
        pixel[0] = static_cast<uint8_t>(packed);
        pixel[1] = static_cast<uint8_t>(packed >> 8);
        pixel[2] = static_cast<uint8_t>(packed >> 16);
        if (info_.bitsPerPixel == 32) {
            pixel[3] = static_cast<uint8_t>(packed >> 24);
        }
    }

    os::DisplayInfo info_{};
    Cell previousCells_[maximumColumns * maximumRows]{};
    uint32_t previousCursorX_ = 0;
    uint32_t previousCursorY_ = 0;
    bool valid_               = false;
};

class Tty {
   public:
    void init(uint32_t id, uint32_t columns, uint32_t rows) {
        os::clearMemory(this, sizeof(*this));
        insertMode_ = true;
        echo_ = true;
        id_         = id;
        columns_    = columns;
        rows_       = rows;
        clear();
        write("osinfo tty", 10);
        put(static_cast<char>('1' + id));
        put('\n');
    }

    void write(const char* data, uint32_t size) {
        for (uint32_t index = 0; index < size; ++index) {
            consume(data[index]);
        }
    }

    void handleKey(const os::KeyEvent& key) {
        os::KeyCode keyCode = navigationKey(key);
        if ((key.modifiers & os::keyModifierControl) != 0 &&
            keyCode >= os::KeyCode::A && keyCode <= os::KeyCode::Z) {
            const char letter =
                static_cast<char>('A' + static_cast<uint32_t>(keyCode) -
                                  static_cast<uint32_t>(os::KeyCode::A));
            const char notation[] = {'^', letter};
            if (keyCode == os::KeyCode::C || keyCode == os::KeyCode::Z) {
                moveCursorForward(lineLength_ - lineCursor_);
                write(notation, 2);
                put('\n');
                lineLength_      = 0;
                lineCursor_      = 0;
                historyPosition_ = historyCount_;
                if (foregroundTask_ != 0) {
                    os::sendSignal(static_cast<uint64_t>(-static_cast<int64_t>(foregroundTask_)),
                                   keyCode == os::KeyCode::C ? SIGINT
                                                            : SIGTSTP);
                } else {
                    enqueue('\n');
                }
            } else if (keyCode == os::KeyCode::D) {
                write(notation, 2);
                if (lineLength_ == 0) {
                    eofPending_ = true;
                } else {
                    commitLine(false);
                }
            } else if (keyCode == os::KeyCode::L) {
                clear();
                write("^L\n", 3);
            } else {
                write(notation, 2);
                appendControlCharacter(static_cast<char>(letter - 'A' + 1));
            }
            return;
        }
        if (!echo_) {
            if (keyCode == os::KeyCode::Enter || keyCode == os::KeyCode::KeypadEnter) {
                commitLine(true);
            } else if (keyCode == os::KeyCode::Backspace) {
                if (lineLength_) line_[--lineLength_] = 0;
                lineCursor_ = lineLength_;
            } else {
                os::KeyEvent translated = key; translated.keyCode = keyCode;
                const char character = keyCharacter(translated);
                if (character && lineLength_ + 1 < lineCapacity) line_[lineLength_++] = character;
                lineCursor_ = lineLength_;
            }
            return;
        }
        if (keyCode == os::KeyCode::Left) {
            if (lineCursor_ != 0) {
                --lineCursor_;
                moveCursorBackward(1);
            }
            return;
        }
        if (keyCode == os::KeyCode::Right) {
            if (lineCursor_ < lineLength_) {
                ++lineCursor_;
                moveCursorForward(1);
            }
            return;
        }
        if (keyCode == os::KeyCode::Home) {
            moveCursorBackward(lineCursor_);
            lineCursor_ = 0;
            return;
        }
        if (keyCode == os::KeyCode::End) {
            moveCursorForward(lineLength_ - lineCursor_);
            lineCursor_ = lineLength_;
            return;
        }
        if (keyCode == os::KeyCode::Up || keyCode == os::KeyCode::PageUp) {
            recallHistory(keyCode == os::KeyCode::PageUp ? -16 : -1);
            return;
        }
        if (keyCode == os::KeyCode::Down || keyCode == os::KeyCode::PageDown) {
            recallHistory(keyCode == os::KeyCode::PageDown ? 16 : 1);
            return;
        }
        if (keyCode == os::KeyCode::Insert) {
            insertMode_ = !insertMode_;
            return;
        }
        if (keyCode == os::KeyCode::Backspace) {
            eraseInputCharacter(true);
            return;
        }
        if (keyCode == os::KeyCode::Delete) {
            eraseInputCharacter(false);
            return;
        }
        if (keyCode == os::KeyCode::Enter ||
            keyCode == os::KeyCode::KeypadEnter) {
            moveCursorForward(lineLength_ - lineCursor_);
            put('\n');
            commitLine(true);
            return;
        }
        if (keyCode == os::KeyCode::Tab) {
            for (uint32_t index = 0; index < 4; ++index) {
                appendInputCharacter(' ');
            }
            return;
        }
        os::KeyEvent translated = key;
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
    void setForegroundTask(uint64_t taskId) {
        foregroundTask_ = taskId;
        if (!taskId) echo_ = true;
    }
    bool echo() const { return echo_; }
    void setEcho(bool enabled) { echo_ = enabled; }

   private:
    static os::KeyCode navigationKey(const os::KeyEvent& key) {
        const bool numeric = (key.modifiers & os::keyModifierNumLock) != 0 &&
                             (key.modifiers & os::keyModifierShift) == 0;
        if (numeric) {
            return key.keyCode;
        }
        switch (key.keyCode) {
            case os::KeyCode::Keypad0:
                return os::KeyCode::Insert;
            case os::KeyCode::Keypad1:
                return os::KeyCode::End;
            case os::KeyCode::Keypad2:
                return os::KeyCode::Down;
            case os::KeyCode::Keypad3:
                return os::KeyCode::PageDown;
            case os::KeyCode::Keypad4:
                return os::KeyCode::Left;
            case os::KeyCode::Keypad6:
                return os::KeyCode::Right;
            case os::KeyCode::Keypad7:
                return os::KeyCode::Home;
            case os::KeyCode::Keypad8:
                return os::KeyCode::Up;
            case os::KeyCode::Keypad9:
                return os::KeyCode::PageUp;
            case os::KeyCode::KeypadDecimal:
                return os::KeyCode::Delete;
            default:
                return key.keyCode;
        }
    }

    static char keyCharacter(const os::KeyEvent& key) {
        const bool shift = (key.modifiers & os::keyModifierShift) != 0;
        const bool caps  = (key.modifiers & os::keyModifierCapsLock) != 0;
        if (key.keyCode >= os::KeyCode::A && key.keyCode <= os::KeyCode::Z) {
            const uint32_t index = static_cast<uint32_t>(key.keyCode) -
                                   static_cast<uint32_t>(os::KeyCode::A);
            return static_cast<char>(((shift != caps) ? 'A' : 'a') + index);
        }
        if (key.keyCode >= os::KeyCode::Digit1 &&
            key.keyCode <= os::KeyCode::Digit9) {
            constexpr char normal[]  = "123456789";
            constexpr char shifted[] = "!@#$%^&*(";
            const uint32_t index     = static_cast<uint32_t>(key.keyCode) -
                                   static_cast<uint32_t>(os::KeyCode::Digit1);
            return shift ? shifted[index] : normal[index];
        }
        switch (key.keyCode) {
            case os::KeyCode::Digit0:
                return shift ? ')' : '0';
            case os::KeyCode::Space:
                return ' ';
            case os::KeyCode::Minus:
                return shift ? '_' : '-';
            case os::KeyCode::Equal:
                return shift ? '+' : '=';
            case os::KeyCode::LeftBracket:
                return shift ? '{' : '[';
            case os::KeyCode::RightBracket:
                return shift ? '}' : ']';
            case os::KeyCode::Semicolon:
                return shift ? ':' : ';';
            case os::KeyCode::Apostrophe:
                return shift ? '"' : '\'';
            case os::KeyCode::Grave:
                return shift ? '~' : '`';
            case os::KeyCode::Backslash:
                return shift ? '|' : '\\';
            case os::KeyCode::Comma:
                return shift ? '<' : ',';
            case os::KeyCode::Period:
                return shift ? '>' : '.';
            case os::KeyCode::Slash:
                return shift ? '?' : '/';
            case os::KeyCode::Keypad0:
                return '0';
            case os::KeyCode::Keypad1:
                return '1';
            case os::KeyCode::Keypad2:
                return '2';
            case os::KeyCode::Keypad3:
                return '3';
            case os::KeyCode::Keypad4:
                return '4';
            case os::KeyCode::Keypad5:
                return '5';
            case os::KeyCode::Keypad6:
                return '6';
            case os::KeyCode::Keypad7:
                return '7';
            case os::KeyCode::Keypad8:
                return '8';
            case os::KeyCode::Keypad9:
                return '9';
            case os::KeyCode::KeypadDecimal:
                return '.';
            case os::KeyCode::KeypadDivide:
                return '/';
            case os::KeyCode::KeypadMultiply:
                return '*';
            case os::KeyCode::KeypadMinus:
                return '-';
            case os::KeyCode::KeypadPlus:
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
        if (result == os::terminal::EscapeParser::Result::Text) put(character);
        else if (result == os::terminal::EscapeParser::Result::Command)
            applyAnsi(ansi_.command());
    }

    void applyAnsi(char command) {
        const uint16_t parameterOne_ = ansi_.parameter(0);
        const uint16_t parameterTwo_ = ansi_.parameter(1);
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
            for (uint32_t i = 0; i < ansi_.count(); ++i)
                style_.apply(ansi_.parameter(i));
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
        cells_[cursorY_ * columns_ + cursorX_] = {character, style_.foreground(),
                                                  style_.background(), style_.attributes()};
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
    os::terminal::Style style_;
    os::terminal::EscapeParser ansi_;
    bool eofPending_;
    bool insertMode_;
};

Tty terminalStorage[os::textTerminalCount];

class ConsoleService {
   public:
    bool init(uint64_t registry) {
        registry_ = registry;
        endpoint_ = os::createEndpoint();
        if (registry_ == 0 || endpoint_ == 0 || !display_.init() ||
            !os::registerService(registry_, os::consoleServiceName,
                                 endpoint_) ||
            !os::registerService(registry_, os::terminalServiceName,
                                 endpoint_)) {
            return false;
        }
        for (uint32_t index = 0; index < os::textTerminalCount; ++index) {
            terminals_[index].init(index, display_.columns(),
                                         display_.rows());
        }
        loadConfiguration();
        if (defaultTerminal_ >= os::textTerminalCount) {
            defaultTerminal_ = 0;
        }
        activeTerminal_ = defaultTerminal_;
        if (activeTerminal_ < os::textTerminalCount) {
            redrawLocked();
        }
        return true;
    }

    uint64_t endpoint() const { return endpoint_; }

    void handle(const os::IpcMessage& request, os::IpcMessage& reply) {
        os::SpinLockGuard guard(lock_);
        const auto operation = static_cast<os::TerminalRequest>(request.type);
        if (operation == os::TerminalRequest::Write) {
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
                    os::log(serial);
                }
                if (activeTerminal_ == terminal) {
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
        } else if (operation == os::TerminalRequest::GetActive) {
            reply.data[0] = 0;
            reply.data[1] = activeTerminal_;
            reply.length  = 2;
        } else if (operation == os::TerminalRequest::Switch) {
            reply.data[0] =
                switchLocked(static_cast<uint32_t>(request.data[0])) ? 0 : 1;
        } else if (operation == os::TerminalRequest::GetInfo) {
            reply.data[0] = 0;
            reply.data[1] = os::textTerminalCount;
            reply.data[2] = os::graphicalTerminalId;
            reply.data[3] = display_.columns();
            reply.data[4] = display_.rows();
            reply.length  = 5;
        } else if (operation == os::TerminalRequest::GetEcho || operation == os::TerminalRequest::SetEcho) {
            const uint32_t terminal = static_cast<uint32_t>(request.data[0]);
            if (terminal >= os::textTerminalCount) reply.data[0] = 1;
            else {
                reply.data[0] = 0;
                reply.data[1] = terminals_[terminal].echo();
                reply.length = 2;
                if (operation == os::TerminalRequest::SetEcho) terminals_[terminal].setEcho(request.data[1] != 0);
            }
        } else if (operation ==
                   os::TerminalRequest::SetForegroundTask) {
            const uint32_t terminal = static_cast<uint32_t>(request.data[0]);
            if (terminal >= os::textTerminalCount) {
                reply.data[0] = static_cast<uint64_t>(
                    static_cast<int64_t>(os::ErrorCode::InvalidArgument));
            } else {
                terminals_[terminal].setForegroundTask(request.data[1]);
                reply.data[0] = 0;
            }
        } else {
            reply.data[0] = static_cast<uint64_t>(
                static_cast<int64_t>(os::ErrorCode::NotSupported));
        }
        if (reply.length == 0) {
            reply.length = 1;
        }
    }

    void handleInput(const os::InputEvent& event) {
        if (event.type != os::InputEventType::KeyDown) {
            return;
        }
        os::SpinLockGuard guard(lock_);
        const bool switchChord =
            (event.key.modifiers & os::keyModifierControl) != 0 &&
            (event.key.modifiers & os::keyModifierShift) != 0 &&
            event.key.keyCode >= os::KeyCode::F1 &&
            event.key.keyCode <= os::KeyCode::F7;
        if (switchChord) {
            const uint32_t terminal = static_cast<uint32_t>(event.key.keyCode) -
                                      static_cast<uint32_t>(os::KeyCode::F1);
            switchLocked(terminal);
        } else if (activeTerminal_ < os::textTerminalCount) {
            terminals_[activeTerminal_].handleKey(event.key);
            redrawLocked();
        }
    }

   private:
    bool switchLocked(uint32_t terminal) {
        if (terminal > os::graphicalTerminalId) {
            return false;
        }
        const uint64_t window =
            os::lookupService(registry_, os::guiwServiceName,
                              terminal == os::graphicalTerminalId ? 100 : 1);
        if (window != 0) {
            os::setWindowSessionActive(window,
                                       terminal == os::graphicalTerminalId);
        }
        activeTerminal_                       = terminal;
        constexpr const char* terminalNames[] = {
            "[CONSOLE] switched to tty1\n", "[CONSOLE] switched to tty2\n",
            "[CONSOLE] switched to tty3\n", "[CONSOLE] switched to tty4\n",
            "[CONSOLE] switched to tty5\n", "[CONSOLE] switched to tty6\n",
            "[CONSOLE] switched to tty7\n"};
        os::log(terminalNames[activeTerminal_]);
        if (activeTerminal_ < os::textTerminalCount) {
            display_.invalidate();
            redrawLocked();
        }
        return true;
    }

    void redrawLocked() {
        if (activeTerminal_ >= os::textTerminalCount) return;
        Tty& terminal = terminals_[activeTerminal_];
        display_.render(terminal.cells(), display_.columns(), display_.rows(),
                        terminal.cursorX(), terminal.cursorY());
    }

    void loadConfiguration() {
        defaultTerminal_ = 0;
        serialConsole_   = false;
        const uint64_t fs =
            os::lookupService(registry_, os::fsServiceName, 5000);
        const uint64_t file =
            fs == 0 ? 0 : ([&]() -> uint64_t { os::File opened(fs); return opened.open("/etc/console.conf", O_RDONLY) == 0 ? opened.releaseNativeHandle() : 0; }());
        if (file == 0) {
            return;
        }
        char configuration[256]{};
        const int64_t count =
            os::File::borrow(fs, file).read(configuration, sizeof(configuration) - 1);
        os::File::borrow(fs, file).close();
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
};

ConsoleService consoleService;
uint64_t registryEndpoint;

void inputMain() {
    const uint64_t input =
        os::lookupService(registryEndpoint, os::inputServiceName, 5000);
    os::InputClient client;
    if (!client.connect(input)) {
        os::log("[CONSOLE] input service unavailable\n");
        return;
    }
    while (true) {
        if (!client.wait()) {
            os::log("[CONSOLE] input subscription failed\n");
            return;
        }
        os::InputEvent event{};
        while (client.receive(event)) {
            consoleService.handleInput(event);
        }
    }
}

}  // namespace

int main(int, char**) {
    os::log("[CONSOLE] Console and TTY service started in Ring 3\n");
    registryEndpoint = os::getBootstrapHandle();
    if (!consoleService.init(registryEndpoint) ||
        os::mapMemory(inputStackAddress, inputStackSize, 1) !=
            inputStackAddress ||
        os::createThread(inputMain, inputStackAddress + inputStackSize) == 0) {
        os::log("[CONSOLE] initialization failed\n");
        return 1;
    }
    os::log("[CONSOLE] tty1-tty6 and tty7 session switching online\n");
    while (true) {
        os::IpcMessage request{};
        if (os::receiveMessage(consoleService.endpoint(), request) != 0) {
            continue;
        }
        os::IpcMessage reply{};
        consoleService.handle(request, reply);
        os::replyMessage(consoleService.endpoint(), reply);
    }
}
