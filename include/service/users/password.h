/*
 * 密码摘要生成与校验。
 */

#ifndef _USERS_PASSWORD_H
#define _USERS_PASSWORD_H
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace users {

    /* 按块累计输入并生成 SHA-256 摘要，供密码校验使用。 */
    class Sha256 {
        public:
            Sha256()
                : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19} {
            }

            void update(const void* input, size_t size);
            void finish(uint8_t output[32]);

        private:
            static uint32_t rotate(uint32_t x, unsigned n) {
                return (x >> n) | (x << (32 - n));
            }

            void block();
            uint32_t state_[8];
            uint8_t buffer_[64]{};
            size_t used_ = 0;
            uint64_t total_ = 0;
    };

    /* 封装密码记录的生成、解析和校验规则。 */
    class Password {
        public:
            Password() = default;
            explicit Password(const char* encoded);
            ~Password();
            bool valid() const;
            static void derive(const char* password, const uint8_t salt[16],
                               uint8_t output[32], unsigned rounds = 10000);
            bool verify(const char* password) const;
            void encode(const char* password, const uint8_t salt[16]);

            const char* getEncoded() const {
                return encoded_;
            }

            static void erase(void* pointer, size_t size) {
                volatile uint8_t* p = static_cast<volatile uint8_t*>(pointer);
                while (size--) {
                    *p++ = 0;
                }
            }

        private:
            char encoded_[128]{};
            bool inRange_ = true;
            static void hmac(const char* password, const void* input,
                             size_t size, uint8_t output[32]);
            static void hex(const uint8_t* bytes, unsigned count, char* output);
            static bool decode(const char* text, uint8_t* bytes,
                               unsigned count);
    };
}  // namespace users
#endif
