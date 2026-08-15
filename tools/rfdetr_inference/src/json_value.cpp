#include "json_value.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

namespace questlab::rfdetr::internal {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    JsonValue Parse() {
        SkipWhitespace();
        JsonValue result = ParseValue();
        SkipWhitespace();
        if (position_ != text_.size()) {
            Fail("Unexpected trailing JSON content");
        }
        return result;
    }

private:
    [[noreturn]] void Fail(const std::string& message) const {
        throw std::runtime_error(
            message + " at byte " + std::to_string(position_));
    }

    void SkipWhitespace() {
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (character != ' ' && character != '\t' &&
                character != '\r' && character != '\n') {
                break;
            }
            ++position_;
        }
    }

    bool Consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void Expect(char expected) {
        if (!Consume(expected)) {
            Fail(std::string("Expected '") + expected + "'");
        }
    }

    JsonValue ParseValue() {
        if (position_ >= text_.size()) {
            Fail("Unexpected end of JSON");
        }
        switch (text_[position_]) {
            case 'n':
                return ParseLiteral("null", JsonValue::Type::Null);
            case 't': {
                JsonValue value = ParseLiteral("true", JsonValue::Type::Boolean);
                value.boolean = true;
                return value;
            }
            case 'f':
                return ParseLiteral("false", JsonValue::Type::Boolean);
            case '"': {
                JsonValue value;
                value.type = JsonValue::Type::String;
                value.string = ParseString();
                return value;
            }
            case '[':
                return ParseArray();
            case '{':
                return ParseObject();
            default:
                if (text_[position_] == '-' ||
                    (text_[position_] >= '0' && text_[position_] <= '9')) {
                    return ParseNumber();
                }
                Fail("Unexpected JSON token");
        }
    }

    JsonValue ParseLiteral(
        const std::string& literal,
        JsonValue::Type type) {
        if (text_.compare(position_, literal.size(), literal) != 0) {
            Fail("Invalid JSON literal");
        }
        position_ += literal.size();
        JsonValue value;
        value.type = type;
        return value;
    }

    static void AppendUtf8(uint32_t codePoint, std::string* output) {
        if (codePoint <= 0x7fU) {
            output->push_back(static_cast<char>(codePoint));
        } else if (codePoint <= 0x7ffU) {
            output->push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
            output->push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
        } else {
            output->push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
            output->push_back(static_cast<char>(
                0x80U | ((codePoint >> 6U) & 0x3fU)));
            output->push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
        }
    }

    uint32_t ParseHexQuad() {
        uint32_t result = 0;
        for (int index = 0; index < 4; ++index) {
            if (position_ >= text_.size()) {
                Fail("Truncated JSON Unicode escape");
            }
            const char character = text_[position_++];
            result <<= 4U;
            if (character >= '0' && character <= '9') {
                result |= static_cast<uint32_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                result |= static_cast<uint32_t>(character - 'a' + 10);
            } else if (character >= 'A' && character <= 'F') {
                result |= static_cast<uint32_t>(character - 'A' + 10);
            } else {
                Fail("Invalid JSON Unicode escape");
            }
        }
        return result;
    }

    std::string ParseString() {
        Expect('"');
        std::string output;
        while (position_ < text_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(text_[position_++]);
            if (character == '"') {
                return output;
            }
            if (character < 0x20U) {
                Fail("Control character in JSON string");
            }
            if (character != '\\') {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= text_.size()) {
                Fail("Truncated JSON escape");
            }
            const char escaped = text_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case '/': output.push_back('/'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                case 'u': {
                    const uint32_t codePoint = ParseHexQuad();
                    if (codePoint >= 0xd800U && codePoint <= 0xdfffU) {
                        Fail("JSON surrogate pairs are unsupported");
                    }
                    AppendUtf8(codePoint, &output);
                    break;
                }
                default:
                    Fail("Invalid JSON escape");
            }
        }
        Fail("Unterminated JSON string");
    }

    JsonValue ParseNumber() {
        const size_t start = position_;
        Consume('-');
        if (Consume('0')) {
            if (position_ < text_.size() && text_[position_] >= '0' &&
                text_[position_] <= '9') {
                Fail("Leading zero in JSON number");
            }
        } else {
            if (position_ >= text_.size() || text_[position_] < '1' ||
                text_[position_] > '9') {
                Fail("Invalid JSON number");
            }
            while (position_ < text_.size() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }
        if (Consume('.')) {
            if (position_ >= text_.size() || text_[position_] < '0' ||
                text_[position_] > '9') {
                Fail("Invalid JSON fraction");
            }
            while (position_ < text_.size() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }
        if (position_ < text_.size() &&
            (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() &&
                (text_[position_] == '+' || text_[position_] == '-')) {
                ++position_;
            }
            if (position_ >= text_.size() || text_[position_] < '0' ||
                text_[position_] > '9') {
                Fail("Invalid JSON exponent");
            }
            while (position_ < text_.size() && text_[position_] >= '0' &&
                   text_[position_] <= '9') {
                ++position_;
            }
        }
        const std::string token = text_.substr(start, position_ - start);
        char* end = nullptr;
        const double number = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0' || !std::isfinite(number)) {
            Fail("Invalid finite JSON number");
        }
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = number;
        return value;
    }

    JsonValue ParseArray() {
        Expect('[');
        JsonValue value;
        value.type = JsonValue::Type::Array;
        SkipWhitespace();
        if (Consume(']')) {
            return value;
        }
        while (true) {
            SkipWhitespace();
            value.array.push_back(ParseValue());
            SkipWhitespace();
            if (Consume(']')) {
                return value;
            }
            Expect(',');
        }
    }

    JsonValue ParseObject() {
        Expect('{');
        JsonValue value;
        value.type = JsonValue::Type::Object;
        SkipWhitespace();
        if (Consume('}')) {
            return value;
        }
        while (true) {
            SkipWhitespace();
            if (position_ >= text_.size() || text_[position_] != '"') {
                Fail("Expected JSON object key");
            }
            const std::string key = ParseString();
            SkipWhitespace();
            Expect(':');
            SkipWhitespace();
            JsonValue child = ParseValue();
            if (!value.object.emplace(key, std::move(child)).second) {
                Fail("Duplicate JSON object key: " + key);
            }
            SkipWhitespace();
            if (Consume('}')) {
                return value;
            }
            Expect(',');
        }
    }

    const std::string& text_;
    size_t position_ = 0;
};

}  // namespace

const JsonValue& JsonValue::At(const std::string& key) const {
    if (type != Type::Object) {
        throw std::runtime_error("Expected JSON object before key: " + key);
    }
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        throw std::runtime_error("Missing JSON key: " + key);
    }
    return iterator->second;
}

const std::string& JsonValue::AsString(const std::string& context) const {
    if (type != Type::String) {
        throw std::runtime_error("Expected JSON string: " + context);
    }
    return string;
}

double JsonValue::AsNumber(const std::string& context) const {
    if (type != Type::Number) {
        throw std::runtime_error("Expected JSON number: " + context);
    }
    return number;
}

bool JsonValue::AsBoolean(const std::string& context) const {
    if (type != Type::Boolean) {
        throw std::runtime_error("Expected JSON boolean: " + context);
    }
    return boolean;
}

const std::vector<JsonValue>& JsonValue::AsArray(
    const std::string& context) const {
    if (type != Type::Array) {
        throw std::runtime_error("Expected JSON array: " + context);
    }
    return array;
}

bool ParseJson(
    const std::string& text,
    JsonValue* value,
    std::string* error) {
    if (value == nullptr) {
        if (error != nullptr) {
            *error = "JSON output pointer is null";
        }
        return false;
    }
    try {
        *value = Parser(text).Parse();
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

}  // namespace questlab::rfdetr::internal
