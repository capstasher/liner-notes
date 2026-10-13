// Small, dependency-free JSON reader/writer. UTF-8 in, UTF-8 out.
#pragma once
#include <string>
#include <vector>

namespace ln {
namespace json {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> items;        // Array elements, or Object values
    std::vector<std::string> keys;   // Object keys (parallel to items)

    bool isNull() const { return type == Null; }
    bool isArray() const { return type == Array; }
    bool isObject() const { return type == Object; }
    bool isString() const { return type == String; }
    size_t size() const { return (type == Array || type == Object) ? items.size() : 0; }

    const Value* find(const std::string& key) const;
    const Value& operator[](const std::string& key) const;   // null Value if missing
    const Value& operator[](size_t i) const;                  // null Value if out of range

    std::string str(const std::string& def = "") const;      // strings; numbers formatted
    double num(double def = 0) const;
    int integer(int def = 0) const;                            // numbers or numeric strings
    bool boolean(bool def = false) const { return type == Bool ? b : def; }
};

bool parse(const std::string& text, Value& out, std::string* error = nullptr);
std::string quote(const std::string& s);   // JSON string literal, with quotes

} // namespace json
} // namespace ln
