#pragma once

#include <cctype>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace host_json {
class Json {
   public:
    using Array  = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object>
        value = nullptr;

    const Object& object() const {
        if (const auto* result = std::get_if<Object>(&value)) return *result;
        throw std::runtime_error("expected a JSON object");
    }
    const Array& array() const {
        if (const auto* result = std::get_if<Array>(&value)) return *result;
        throw std::runtime_error("expected a JSON array");
    }
    const std::string& string() const {
        if (const auto* result = std::get_if<std::string>(&value))
            return *result;
        throw std::runtime_error("expected a JSON string");
    }
    const Json* find(const std::string& key) const {
        const auto& fields = object();
        auto it            = fields.find(key);
        return it == fields.end() ? nullptr : &it->second;
    }
    const Json& at(const std::string& key) const {
        if (auto* field = find(key)) return *field;
        throw std::runtime_error("missing configuration field: " + key);
    }
    std::string text(const std::string& key, std::string fallback = {}) const {
        if (auto* field = find(key)) {
            try {
                return field->string();
            } catch (const std::exception&) {
                throw std::runtime_error(key + ": expected a string");
            }
        }
        return fallback;
    }
    std::vector<std::string> strings(const std::string& key) const {
        std::vector<std::string> result;
        if (auto* field = find(key)) {
            try {
                for (const auto& item : field->array())
                    result.push_back(item.string());
            } catch (const std::exception&) {
                throw std::runtime_error(key +
                                         ": expected an array of strings");
            }
        }
        return result;
    }

    static Json parse(const std::string& input);
};

class JsonParser {
    const std::string& input_;
    size_t position_ = 0;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("JSON byte " + std::to_string(position_) +
                                 ": " + message);
    }
    void whitespace() {
        while (position_ < input_.size() &&
               std::string(" \r\n\t").find(input_[position_]) !=
                   std::string::npos)
            ++position_;
    }
    bool take(char c) {
        whitespace();
        if (position_ < input_.size() && input_[position_] == c) {
            ++position_;
            return true;
        }
        return false;
    }
    void expect(char c) {
        if (!take(c)) fail(std::string("expected '") + c + "'");
    }
    unsigned hex() {
        unsigned value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (position_ == input_.size()) fail("incomplete Unicode escape");
            char c         = input_[position_++];
            unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                             : c >= 'a' && c <= 'f' ? c - 'a' + 10
                             : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                    : 16;
            if (digit == 16) fail("invalid Unicode escape");
            value = value * 16 + digit;
        }
        return value;
    }
    static void utf8(std::string& result, unsigned code) {
        if (code < 0x80)
            result += static_cast<char>(code);
        else if (code < 0x800) {
            result += static_cast<char>(0xc0 | (code >> 6));
            result += static_cast<char>(0x80 | (code & 63));
        } else if (code < 0x10000) {
            result += static_cast<char>(0xe0 | (code >> 12));
            result += static_cast<char>(0x80 | ((code >> 6) & 63));
            result += static_cast<char>(0x80 | (code & 63));
        } else {
            result += static_cast<char>(0xf0 | (code >> 18));
            result += static_cast<char>(0x80 | ((code >> 12) & 63));
            result += static_cast<char>(0x80 | ((code >> 6) & 63));
            result += static_cast<char>(0x80 | (code & 63));
        }
    }
    std::string string() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            unsigned char c = input_[position_++];
            if (c == '"') return result;
            if (c < 32) fail("control byte in string");
            if (c != '\\') {
                result += static_cast<char>(c);
                continue;
            }
            if (position_ == input_.size()) fail("incomplete escape");
            switch (input_[position_++]) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'u': {
                    unsigned code = hex();
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (input_.substr(position_, 2) != "\\u")
                            fail("missing low surrogate");
                        position_ += 2;
                        unsigned low = hex();
                        if (low < 0xdc00 || low > 0xdfff)
                            fail("invalid low surrogate");
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff)
                        fail("unpaired low surrogate");
                    utf8(result, code);
                    break;
                }
                default:
                    fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    Json value(unsigned depth) {
        if (depth > 64) fail("maximum nesting exceeded");
        whitespace();
        if (position_ == input_.size()) fail("expected a value");
        Json result;
        const char c = input_[position_];
        if (c == '"') {
            result.value = string();
            return result;
        }
        if (take('{')) {
            Json::Object fields;
            if (!take('}')) {
                do {
                    auto key = string();
                    expect(':');
                    if (!fields.emplace(key, value(depth + 1)).second)
                        fail("duplicate key: " + key);
                } while (take(','));
                expect('}');
            }
            result.value = std::move(fields);
            return result;
        }
        if (take('[')) {
            Json::Array items;
            if (!take(']')) {
                do {
                    items.push_back(value(depth + 1));
                } while (take(','));
                expect(']');
            }
            result.value = std::move(items);
            return result;
        }
        for (const auto& literal :
             {std::string("true"), std::string("false"), std::string("null")}) {
            if (input_.compare(position_, literal.size(), literal) == 0) {
                position_ += literal.size();
                if (literal != "null") result.value = literal == "true";
                return result;
            }
        }
        const size_t begin = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ == input_.size() ||
            !std::isdigit(static_cast<unsigned char>(input_[position_])))
            fail("expected a number");
        if (input_[position_] == '0')
            ++position_;
        else
            while (position_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[position_])))
                ++position_;
        for (char marker : {'.', 'e'}) {
            if (position_ < input_.size() &&
                (input_[position_] == marker ||
                 (marker == 'e' && input_[position_] == 'E'))) {
                ++position_;
                if (marker == 'e' && position_ < input_.size() &&
                    (input_[position_] == '+' || input_[position_] == '-'))
                    ++position_;
                const size_t digits = position_;
                while (
                    position_ < input_.size() &&
                    std::isdigit(static_cast<unsigned char>(input_[position_])))
                    ++position_;
                if (digits == position_) fail("missing number digits");
            }
        }
        double number = std::stod(input_.substr(begin, position_ - begin));
        if (!std::isfinite(number)) fail("number out of range");
        result.value = number;
        return result;
    }

   public:
    explicit JsonParser(const std::string& input) : input_(input) {}
    Json parse() {
        if (input_.compare(0, 3, "\xef\xbb\xbf") == 0) position_ = 3;
        Json result = value(0);
        whitespace();
        if (position_ != input_.size()) fail("trailing content");
        return result;
    }
};
inline Json Json::parse(const std::string& input) {
    return JsonParser(input).parse();
}
}  // namespace host_json
