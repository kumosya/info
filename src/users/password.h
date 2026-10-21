#ifndef _USERS_PASSWORD_H
#define _USERS_PASSWORD_H
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace users {
class Sha256 {
   public:
    Sha256() : state_{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19} {}
    void update(const void* input, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(input);
        total_ += size;
        while (size--) { buffer_[used_++] = *bytes++; if (used_ == 64) { block(); used_ = 0; } }
    }
    void finish(uint8_t output[32]) {
        const uint64_t bits = total_ * 8;
        const uint8_t marker = 0x80, zero = 0;
        update(&marker, 1);
        while (used_ != 56) update(&zero, 1);
        uint8_t length[8];
        for (unsigned i = 0; i < 8; ++i) length[i] = bits >> (56 - 8 * i);
        update(length, 8);
        for (unsigned i = 0; i < 32; ++i) output[i] = state_[i / 4] >> (24 - 8 * (i % 4));
    }
   private:
    static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
    void block() {
        static constexpr uint32_t constants[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t words[64];
        for (unsigned i = 0; i < 16; ++i) words[i] = (uint32_t(buffer_[i*4]) << 24) |
            (uint32_t(buffer_[i*4+1]) << 16) | (uint32_t(buffer_[i*4+2]) << 8) | buffer_[i*4+3];
        for (unsigned i = 16; i < 64; ++i) {
            const uint32_t a = words[i-15], b = words[i-2];
            words[i] = words[i-16] + (rotate(a,7)^rotate(a,18)^(a>>3)) + words[i-7] + (rotate(b,17)^rotate(b,19)^(b>>10));
        }
        uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (unsigned i = 0; i < 64; ++i) {
            const uint32_t first = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + constants[i] + words[i];
            const uint32_t second = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+first;d=c;c=b;b=a;a=first+second;
        }
        state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
    }
    uint32_t state_[8];
    uint8_t buffer_[64]{};
    size_t used_ = 0;
    uint64_t total_ = 0;
};

class Password {
   public:
    static bool valid(const char* encoded) {
        if (!*encoded || *encoded == '!' || *encoded == '*') return true;
        constexpr char prefix[] = "$pbkdf2-sha256$10000$";
        if (strncmp(encoded, prefix, sizeof(prefix) - 1)) return false;
        encoded += sizeof(prefix) - 1;
        uint8_t salt[16], hash[32];
        return strlen(encoded) == 97 && encoded[32] == '$' && decode(encoded, salt, 16) && decode(encoded + 33, hash, 32);
    }
    static void derive(const char* password, const uint8_t salt[16], uint8_t output[32], unsigned rounds = 10000) {
        uint8_t input[20], current[32];
        memcpy(input, salt, 16); input[16]=0; input[17]=0; input[18]=0; input[19]=1;
        hmac(password, input, sizeof(input), current);
        memcpy(output, current, 32);
        for (unsigned i = 1; i < rounds; ++i) {
            hmac(password, current, sizeof(current), current);
            for (unsigned n = 0; n < 32; ++n) output[n] ^= current[n];
        }
        erase(current, sizeof(current));
    }
    static bool verify(const char* password, const char* encoded) {
        if (!*encoded) return !*password;
        constexpr char prefix[] = "$pbkdf2-sha256$10000$";
        if (strncmp(encoded, prefix, sizeof(prefix)-1)) return false;
        encoded += sizeof(prefix)-1;
        if (strlen(encoded) != 32 + 1 + 64 || encoded[32] != '$') return false;
        uint8_t salt[16], expected[32], actual[32];
        if (!decode(encoded, salt, 16) || !decode(encoded + 33, expected, 32)) return false;
        derive(password, salt, actual);
        unsigned difference = 0;
        for (unsigned i = 0; i < 32; ++i) difference |= expected[i] ^ actual[i];
        erase(actual, sizeof(actual)); return difference == 0;
    }
    static void encode(const char* password, const uint8_t salt[16], char output[128]) {
        strcpy(output, "$pbkdf2-sha256$10000$");
        char* p = output + strlen(output);
        hex(salt, 16, p); p += 32; *p++ = '$';
        uint8_t derived[32]; derive(password, salt, derived); hex(derived, 32, p);
        erase(derived, sizeof(derived));
    }
    static void erase(void* pointer, size_t size) {
        volatile uint8_t* p = static_cast<volatile uint8_t*>(pointer); while (size--) *p++ = 0;
    }
   private:
    static void hmac(const char* password, const void* input, size_t size, uint8_t output[32]) {
        uint8_t key[64]{}, pad[64], inner[32];
        const size_t length = strlen(password);
        if (length > 64) { Sha256 hash; hash.update(password, length); hash.finish(key); }
        else memcpy(key, password, length);
        for (unsigned i = 0; i < 64; ++i) pad[i] = key[i] ^ 0x36;
        Sha256 first; first.update(pad, 64); first.update(input, size); first.finish(inner);
        for (unsigned i = 0; i < 64; ++i) pad[i] = key[i] ^ 0x5c;
        Sha256 second; second.update(pad, 64); second.update(inner, 32); second.finish(output);
        erase(key, sizeof(key)); erase(pad, sizeof(pad)); erase(inner, sizeof(inner));
    }
    static void hex(const uint8_t* bytes, unsigned count, char* output) {
        constexpr char digits[] = "0123456789abcdef";
        for (unsigned i = 0; i < count; ++i) { output[i*2] = digits[bytes[i]>>4]; output[i*2+1] = digits[bytes[i]&15]; }
        output[count*2] = 0;
    }
    static bool decode(const char* text, uint8_t* bytes, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            unsigned value = 0;
            for (unsigned n = 0; n < 2; ++n) {
                const char c = text[i*2+n];
                const unsigned digit = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : 16;
                if (digit == 16) return false;
                value = value * 16 + digit;
            }
            bytes[i] = value;
        }
        return true;
    }
};
}
#endif
