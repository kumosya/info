#include <cstdlib>
#include <iostream>
#include <unistd.h>

namespace std {

istream& istream::get(char& value) {
    good_ = ::read(fileDescriptor_, &value, 1) == 1;
    return *this;
}

istream& istream::getline(char* buffer, streamsize size) {
    if (buffer == nullptr || size <= 0) {
        good_ = false;
        return *this;
    }
    streamsize used = 0;
    while (used + 1 < size) {
        char character;
        if (::read(fileDescriptor_, &character, 1) != 1) {
            good_ = false;
            break;
        }
        if (character == '\n') break;
        buffer[used++] = character;
    }
    buffer[used] = '\0';
    return *this;
}

istream& istream::read(char* buffer, streamsize size) {
    good_ = buffer != nullptr && size >= 0 &&
            ::read(fileDescriptor_, buffer, static_cast<size_t>(size)) == size;
    return *this;
}

istream& istream::operator>>(int& value) {
    char buffer[32];
    getline(buffer, sizeof(buffer));
    if (good_) value = atoi(buffer);
    return *this;
}

}  // namespace std
