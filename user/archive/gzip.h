/*
 * gzip.h - Buffered gzip streams with RFC 1951 fixed/dynamic Huffman support.
 */
#ifndef _USER_ARCHIVE_GZIP_H
#define _USER_ARCHIVE_GZIP_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace archive {
inline uint32_t crcByte(uint32_t crc, unsigned byte) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    return crc;
}

class RawInput {
   public:
    explicit RawInput(FILE* file) : file_(file) {}
    int peek() {
        if (position_ == count_) { count_ = fread(buffer_, 1, sizeof(buffer_), file_); position_ = 0; }
        return count_ ? buffer_[position_] : EOF;
    }
    int get() { const int value = peek(); if (value != EOF) ++position_; return value; }
    bool failed() const { return ferror(file_) != 0; }
   private:
    FILE* file_;
    unsigned char buffer_[8192]{};
    size_t position_ = 0, count_ = 0;
};

class Huffman {
   public:
    bool build(const unsigned char* lengths, unsigned count) {
        memset(count_, 0, sizeof(count_));
        for (unsigned i = 0; i < count; ++i) { if (lengths[i] > 15) return false; ++count_[lengths[i]]; }
        int left = 1;
        for (unsigned bits = 1; bits <= 15; ++bits) { left = left * 2 - count_[bits]; if (left < 0) return false; }
        unsigned offset[16]{};
        for (unsigned i = 1; i < 15; ++i) offset[i + 1] = offset[i] + count_[i];
        for (unsigned i = 0; i < count; ++i) if (lengths[i]) symbols_[offset[lengths[i]]++] = i;
        return true;
    }
    template <class Reader> int decode(Reader& input) const {
        unsigned code = 0, first = 0, index = 0;
        for (unsigned length = 1; length <= 15; ++length) {
            code |= input.bits(1);
            if (code >= first && code - first < count_[length]) return symbols_[index + code - first];
            index += count_[length]; first = (first + count_[length]) << 1; code <<= 1;
        }
        return -1;
    }
   private:
    unsigned short count_[16]{}, symbols_[288]{};
};

class Input {
   public:
    explicit Input(FILE* file, bool gzip = false) : raw_(file) {
        gzip_ = gzip || raw_.peek() == 0x1f;
        if (gzip_) { window_ = static_cast<unsigned char*>(malloc(32768)); if (!window_ || !header()) error_ = true; }
    }
    ~Input() { free(window_); }
    bool failed() const { return error_ || raw_.failed(); }
    size_t read(void* data, size_t size) {
        auto* output = static_cast<unsigned char*>(data);
        size_t count = 0;
        while (count < size && !error_) {
            const int value = gzip_ ? decoded() : raw_.get();
            if (value == EOF) break;
            output[count++] = value;
        }
        return count;
    }
    bool finish() {
        unsigned char buffer[4096];
        while (read(buffer, sizeof(buffer))) {}
        return !failed();
    }
    unsigned bits(unsigned count) {
        while (bitCount_ < count) {
            const int value = raw_.get();
            if (value == EOF) { error_ = true; return 0; }
            bitBuffer_ |= static_cast<uint32_t>(value) << bitCount_; bitCount_ += 8;
        }
        const unsigned result = bitBuffer_ & ((1u << count) - 1);
        bitBuffer_ >>= count; bitCount_ -= count; return result;
    }
   private:
    int byte() { const int value = raw_.get(); if (value == EOF) error_ = true; return value == EOF ? 0 : value; }
    uint32_t word() { uint32_t result = 0; for (unsigned n = 0; n < 4; ++n) result |= static_cast<uint32_t>(byte()) << (n * 8); return result; }
    bool header() {
        uint32_t check = 0xffffffff;
        auto get = [&]() { const unsigned value = byte(); check = crcByte(check, value); return value; };
        if (get() != 0x1f || get() != 0x8b || get() != 8) return false;
        const unsigned flags = get(); if (flags & 0xe0) return false;
        for (unsigned i = 0; i < 6; ++i) get();
        if (flags & 4) { unsigned size = get(); size |= get() << 8; while (size--) get(); }
        for (unsigned n = 0; n < 2; ++n) if (flags & (n ? 16 : 8)) {
            unsigned length = 0;
            while (get()) if (error_ || ++length > 65536) return false;
        }
        if (flags & 2) { unsigned expected = byte(); expected |= byte() << 8; if (expected != ((check ^ 0xffffffffu) & 0xffff)) return false; }
        crc_ = 0xffffffff; size_ = 0; position_ = 0; available_ = 0;
        final_ = false; ended_ = false; active_ = false; bitCount_ = 0; bitBuffer_ = 0;
        return !error_;
    }
    bool trees(unsigned type) {
        unsigned char lengths[320]{};
        if (type == 1) {
            for (unsigned i = 0; i < 288; ++i) lengths[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
            if (!literal_.build(lengths, 288)) return false;
            memset(lengths, 5, 32); return distance_.build(lengths, 32);
        }
        const unsigned literals = bits(5) + 257, distances = bits(5) + 1, codes = bits(4) + 4;
        if (literals > 286 || distances > 32) return false;
        static const unsigned char order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        unsigned char codeLengths[19]{};
        for (unsigned i = 0; i < codes; ++i) codeLengths[order[i]] = bits(3);
        Huffman codeTree;
        if (!codeTree.build(codeLengths, 19)) return false;
        for (unsigned i = 0; i < literals + distances;) {
            const int symbol = codeTree.decode(*this);
            if (symbol < 0) return false;
            if (symbol <= 15) { lengths[i++] = symbol; continue; }
            unsigned count = 0, value = 0;
            if (symbol == 16) { if (!i) return false; count = bits(2) + 3; value = lengths[i - 1]; }
            else if (symbol == 17) count = bits(3) + 3;
            else if (symbol == 18) count = bits(7) + 11;
            else return false;
            if (i + count > literals + distances) return false;
            while (count--) lengths[i++] = value;
        }
        return lengths[256] && literal_.build(lengths, literals) && distance_.build(lengths + literals, distances) && !error_;
    }
    int emit(unsigned value) {
        window_[position_++ & 32767] = value;
        if (available_ < 32768) ++available_;
        crc_ = crcByte(crc_, value); ++size_; return value;
    }
    int decoded() {
        static const unsigned short lengthBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const unsigned char lengthExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const unsigned short distanceBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const unsigned char distanceExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        while (!error_ && !ended_) {
            if (remaining_) { --remaining_; return emit(window_[(position_ - matchDistance_) & 32767]); }
            if (stored_) { --stored_; return emit(bits(8)); }
            if (!active_) {
                if (final_) {
                    bitCount_ = 0; bitBuffer_ = 0;
                    const uint32_t expectedCrc = word(), expectedSize = word();
                    if (expectedCrc != (crc_ ^ 0xffffffffu) || expectedSize != size_ || error_) { error_ = true; break; }
                    if (raw_.peek() == EOF) { ended_ = true; break; }
                    if (!header()) { error_ = true; break; }
                }
                final_ = bits(1); const unsigned type = bits(2);
                if (type == 0) {
                    bitCount_ = 0; bitBuffer_ = 0;
                    unsigned length = byte(); length |= byte() << 8;
                    unsigned complement = byte(); complement |= byte() << 8;
                    if ((length ^ complement) != 65535) { error_ = true; break; }
                    stored_ = length; continue;
                }
                if (type == 3 || !trees(type)) { error_ = true; break; }
                active_ = true;
            }
            const int symbol = literal_.decode(*this);
            if (symbol < 0 || symbol > 285) { error_ = true; break; }
            if (symbol < 256) return emit(symbol);
            if (symbol == 256) { active_ = false; continue; }
            const unsigned index = symbol - 257;
            remaining_ = lengthBase[index] + bits(lengthExtra[index]);
            const int distance = distance_.decode(*this);
            if (distance < 0 || distance >= 30) { error_ = true; break; }
            matchDistance_ = distanceBase[distance] + bits(distanceExtra[distance]);
            if (matchDistance_ > available_) { error_ = true; break; }
        }
        return EOF;
    }
    RawInput raw_;
    Huffman literal_, distance_;
    unsigned char* window_ = nullptr;
    uint32_t bitBuffer_ = 0, crc_ = 0xffffffff, size_ = 0, position_ = 0;
    unsigned bitCount_ = 0, remaining_ = 0, stored_ = 0, matchDistance_ = 0, available_ = 0;
    bool gzip_ = false, error_ = false, active_ = false, final_ = false, ended_ = false;
};

class Output {
   public:
    explicit Output(FILE* file, bool gzip) : file_(file), gzip_(gzip) {
        if (gzip_) {
            block_ = static_cast<unsigned char*>(malloc(32768));
            previous_ = static_cast<int*>(malloc(32768 * sizeof(int)));
            heads_ = static_cast<int*>(malloc(4096 * sizeof(int)));
            if (!block_ || !previous_ || !heads_) { error_ = true; return; }
            const unsigned char header[10] = {31,139,8,0,0,0,0,0,0,3};
            for (unsigned value : header) byte(value);
        }
    }
    ~Output() { free(block_); free(previous_); free(heads_); }
    bool write(const void* data, size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size && !error_; ++i) {
            if (!gzip_) { byte(bytes[i]); continue; }
            block_[count_++] = bytes[i]; crc_ = crcByte(crc_, bytes[i]); ++size_;
            if (count_ == 32768) block(false);
        }
        return !error_;
    }
    bool finish() {
        if (finished_) return !error_;
        if (gzip_ && !error_) {
            block(true);
            if (bitCount_) byte(bitBuffer_ & 255);
            const uint32_t crc = crc_ ^ 0xffffffffu;
            for (unsigned n = 0; n < 4; ++n) byte((crc >> (8 * n)) & 255);
            for (unsigned n = 0; n < 4; ++n) byte((size_ >> (8 * n)) & 255);
        }
        flush(); finished_ = true; return !error_ && fflush(file_) == 0;
    }
   private:
    void flush() { if (used_ && fwrite(buffer_, 1, used_, file_) != used_) error_ = true; used_ = 0; }
    void byte(unsigned value) { buffer_[used_++] = value; if (used_ == sizeof(buffer_)) flush(); }
    void bits(unsigned value, unsigned count) {
        bitBuffer_ |= value << bitCount_; bitCount_ += count;
        while (bitCount_ >= 8) { byte(bitBuffer_ & 255); bitBuffer_ >>= 8; bitCount_ -= 8; }
    }
    static unsigned reverse(unsigned value, unsigned count) { unsigned result = 0; while (count--) { result = result * 2 + (value & 1); value >>= 1; } return result; }
    void symbol(unsigned value) {
        const unsigned length = value < 144 ? 8 : value < 256 ? 9 : value < 280 ? 7 : 8;
        const unsigned code = value < 144 ? value + 48 : value < 256 ? value + 256 : value < 280 ? value - 256 : value - 88;
        bits(reverse(code, length), length);
    }
    static unsigned hash(const unsigned char* data) { return ((data[0] * 251u + data[1]) * 251u + data[2]) & 4095; }
    void block(bool final) {
        static const unsigned short lb[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
        static const unsigned char le[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
        static const unsigned short db[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
        static const unsigned char de[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
        bits(final ? 3 : 2, 3);
        for (unsigned i = 0; i < 4096; ++i) heads_[i] = -1;
        for (unsigned position = 0; position < count_;) {
            unsigned length = 0, distance = 0;
            if (position + 2 < count_) {
                int candidate = heads_[hash(block_ + position)];
                for (unsigned attempts = 0; candidate >= 0 && attempts < 32; candidate = previous_[candidate], ++attempts) {
                    unsigned match = 0;
                    while (match < 258 && position + match < count_ && block_[candidate + match] == block_[position + match]) ++match;
                    if (match > length && match >= 3) { length = match; distance = position - candidate; if (match == 258) break; }
                }
            }
            const unsigned consumed = length >= 3 ? length : 1;
            if (length >= 3) {
                unsigned li = 0; while (li < 28 && lb[li + 1] <= length) ++li;
                symbol(257 + li); bits(length - lb[li], le[li]);
                unsigned di = 0; while (di < 29 && db[di + 1] <= distance) ++di;
                bits(reverse(di, 5), 5); bits(distance - db[di], de[di]);
            } else symbol(block_[position]);
            for (unsigned i = 0; i < consumed; ++i) if (position + i + 2 < count_) {
                const unsigned code = hash(block_ + position + i);
                previous_[position + i] = heads_[code]; heads_[code] = position + i;
            }
            position += consumed;
        }
        symbol(256); count_ = 0;
    }
    FILE* file_;
    unsigned char buffer_[8192]{}, *block_ = nullptr;
    int* previous_ = nullptr;
    int* heads_ = nullptr;
    size_t used_ = 0;
    unsigned count_ = 0, bitBuffer_ = 0, bitCount_ = 0;
    uint32_t crc_ = 0xffffffff, size_ = 0;
    bool gzip_, error_ = false, finished_ = false;
};
}  // namespace archive
#endif
