/* 服务对象的成员函数实现。 */
#include "service/fs/ext3/filesystem.h"

namespace fs::ext3::detail {

    /* 按目录项中的长度比较文件名。 */
    bool directory_encoding::namesEqual(const char* left, const char* right,
                                        size_t length) {
        for (size_t index = 0; index < length; ++index) {
            if (left[index] != right[index]) {
                return false;
            }
        }

        return true;
    }

}  // namespace fs::ext3::detail
