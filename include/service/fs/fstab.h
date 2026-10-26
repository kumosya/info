#pragma once
#include <cstddef>
#include <cstdint>

namespace vfs::server {
struct MountSpec {
    char source[128]{};
    char target[128]{};
    char type[16]{};
    char options[64]{};
    uint32_t dump  = 0;
    uint32_t pass  = 0;
    bool readOnly  = false;
    bool automatic = true;
    bool required  = true;
};

class Fstab {
   public:
    // Advances by one line. A blank/comment line has present == false.
    static bool canonicalTarget(char* target);
    static bool next(const char*& text, MountSpec& spec, bool& present);
};
}  // namespace vfs::server
