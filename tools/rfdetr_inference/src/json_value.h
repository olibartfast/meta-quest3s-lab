#pragma once

#include <map>
#include <string>
#include <vector>

namespace questlab::rfdetr::internal {

class JsonValue {
public:
    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    const JsonValue& At(const std::string& key) const;
    const std::string& AsString(const std::string& context) const;
    double AsNumber(const std::string& context) const;
    bool AsBoolean(const std::string& context) const;
    const std::vector<JsonValue>& AsArray(const std::string& context) const;
};

bool ParseJson(
    const std::string& text,
    JsonValue* value,
    std::string* error);

}  // namespace questlab::rfdetr::internal
