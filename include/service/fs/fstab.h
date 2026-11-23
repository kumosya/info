/*
 * 挂载配置解析。
 */

#pragma once
#include <cstddef>
#include <cstdint>

namespace vfs::server {
    /* 保存一条挂载配置的设备、路径、格式及选项。 */
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

    /* 解析挂载配置并提供挂载项列表。 */
    class Fstab {
        public:
            // Advances by one line. A blank/comment line has present == false.
            static bool canonicalTarget(char* target);
            static bool next(const char*& text, MountSpec& spec, bool& present);
    };
}  // namespace vfs::server
