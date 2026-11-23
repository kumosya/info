#include <cassert>
#include <cstdint>
#include "service/guiw/textures.h"

int main()
{
    guiw::TexturePixels zero(0), overflow(SIZE_MAX);
    assert(static_cast<uint32_t*>(zero) == nullptr);
    assert(static_cast<uint32_t*>(overflow) == nullptr);
    guiw::TexturePixels saved;
    {
        guiw::TexturePixels pixels(4096);
        uint32_t* data = pixels;
        assert(data);
        for (unsigned index = 0; index < 4096; ++index) {
            data[index] = index * 17;
        }
        saved = pixels;
        saved = saved;
    }
    uint32_t* data = saved;
    assert(data && data[4095] == 4095 * 17);
    saved = zero;
    assert(static_cast<uint32_t*>(saved) == nullptr);
    return 0;
}
