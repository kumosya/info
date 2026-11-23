/* 配置文本范围及格式检查。 */
#include <os/runtime.h>

namespace os {

    /* 字符串形式以结束字符确定文本长度。 */
    ConfigurationText::ConfigurationText(const char* text)
        : text_(text), size_(0) {
        while (text_ && text_[size_]) {
            ++size_;
        }
    }

    /* 缓冲区形式直接保存调用方给出的文本范围。 */
    ConfigurationText::ConfigurationText(const char* text, size_t size)
        : text_(text), size_(size) {
    }

    /* 配置内容接受可见字符、换行和成对的回车换行。 */
    bool ConfigurationText::valid() const {
        if (!text_) {
            return false;
        }

        for (size_t index = 0; index < size_; ++index) {
            const unsigned char value =
                static_cast<unsigned char>(text_[index]);
            if ((value < 32 && value != '\n' && value != '\r') ||
                value == 127) {
                return false;
            }

            if (value == '\r' &&
                (index + 1 == size_ || text_[index + 1] != '\n')) {
                return false;
            }
        }

        return true;
    }

    /* 路径的各级目录名按对象保存的文本范围逐段检查。 */
    bool ConfigurationText::absolutePath() const {
        if (!text_ || size_ < 2 || text_[0] != '/') {
            return false;
        }

        size_t begin = 1;

        for (size_t index = begin; index <= size_; ++index) {
            if (index < size_ && text_[index] != '/') {
                const unsigned char value =
                    static_cast<unsigned char>(text_[index]);
                if (value <= 32 || value == 127 || value == '\\') {
                    return false;
                }

                continue;
            }

            const size_t length = index - begin;

            if (!length || (length == 1 && text_[begin] == '.') ||
                (length == 2 && text_[begin] == '.' &&
                 text_[begin + 1] == '.')) {
                return false;
            }

            begin = index + 1;
        }

        return true;
    }

}  // namespace os
