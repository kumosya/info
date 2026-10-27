#include <cstdio>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace std {

ostream& ostream::put(char character) {
    ::write(fileDescriptor_, &character, 1);
    return *this;
}

ostream& ostream::write(const char* data, streamsize size) {
    if (data != nullptr && size > 0) {
        ::write(fileDescriptor_, data, static_cast<size_t>(size));
    }
    return *this;
}

ostream& ostream::flush() { return *this; }

ostream& ostream::operator<<(const char* value) {
    return write(value, static_cast<streamsize>(strlen(value)));
}

ostream& ostream::operator<<(char value) { return put(value); }

ostream& ostream::operator<<(int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return *this << buffer;
}

ostream& ostream::operator<<(unsigned int value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", value);
    return *this << buffer;
}

ostream& ostream::operator<<(long value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%ld", value);
    return *this << buffer;
}

ostream& ostream::operator<<(unsigned long value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu", value);
    return *this << buffer;
}

ostream& ostream::operator<<(const void* value) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%p", value);
    return *this << buffer;
}

ostream& ostream::operator<<(ostream& (*manipulator)(ostream&)) {
    return manipulator(*this);
}

ostream& endl(ostream& stream) {
    stream.put('\n');
    return stream.flush();
}

ostream& flush(ostream& stream) { return stream.flush(); }

}  // namespace std
