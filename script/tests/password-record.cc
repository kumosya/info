/* 密码记录对象与 PBKDF2-SHA256 标准结果。 */
#include <cassert>
#include <cstring>
#include "service/users/password.h"

int main() {
    const users::Password empty;
    assert(empty.valid() && empty.verify(""));
    assert(!empty.verify("x"));
    const users::Password locked("!");
    assert(locked.valid() && !locked.verify(""));
    const users::Password malformed("broken hash");
    assert(!malformed.valid());

    uint8_t salt[16];
    for (unsigned index = 0; index < 16; ++index) {
        salt[index] = index;
    }

    users::Password record;
    record.encode("example password", salt);
    assert(record.valid());
    assert(std::strcmp(record.getEncoded(),
        "$pbkdf2-sha256$10000$000102030405060708090a0b0c0d0e0f$"
        "a92e009971dcd28fce9a8be05002f41e8386f2bea1850d4ff8c93384ad9adc78") == 0);
    assert(record.verify("example password"));
    assert(!record.verify("wrong password"));

    char encoded[128];
    std::strcpy(encoded, record.getEncoded());
    const users::Password copy(encoded);
    encoded[0] = '!';
    assert(copy.verify("example password"));

    return 0;
}
