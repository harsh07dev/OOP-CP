#pragma once
#ifndef JSON_MINI_H
#define JSON_MINI_H

// json_mini.h - Lightweight, zero-dependency C++17 JSON parser and serializer.
// Implements safe parsing and deterministic serialization for structured protocol messages.

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <cstdint>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <utility>

namespace protocol {
namespace json {

enum class Type { NUL, BOOLEAN, NUMBER, STRING, ARRAY, OBJECT };

class JsonValue {
public:
    Type type{Type::NUL};
    bool boolVal{false};
    int64_t intVal{0};
    double doubleVal{0.0};
    bool isDouble{false};
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::map<std::string, JsonValue> objVal; // std::map ensures deterministic key ordering

    JsonValue() = default;
    JsonValue(bool b) : type(Type::BOOLEAN), boolVal(b) {}
    JsonValue(int i) : type(Type::NUMBER), intVal(i), doubleVal(i), isDouble(false) {}
    JsonValue(int64_t i) : type(Type::NUMBER), intVal(i), doubleVal(static_cast<double>(i)), isDouble(false) {}
    JsonValue(double d) : type(Type::NUMBER), intVal(static_cast<int64_t>(d)), doubleVal(d), isDouble(true) {}
    JsonValue(const std::string& s) : type(Type::STRING), strVal(s) {}
    JsonValue(std::string_view s) : type(Type::STRING), strVal(s) {}
    JsonValue(const char* s) : type(Type::STRING), strVal(s ? s : "") {}

    static JsonValue object() {
        JsonValue v;
        v.type = Type::OBJECT;
        return v;
    }

    static JsonValue array() {
        JsonValue v;
        v.type = Type::ARRAY;
        return v;
    }

    bool isObject() const noexcept { return type == Type::OBJECT; }
    bool isString() const noexcept { return type == Type::STRING; }
    bool isNumber() const noexcept { return type == Type::NUMBER; }
    bool isBool() const noexcept { return type == Type::BOOLEAN; }
    bool isNull() const noexcept { return type == Type::NUL; }
    bool isArray() const noexcept { return type == Type::ARRAY; }

    bool has(const std::string& key) const {
        if (type != Type::OBJECT) return false;
        return objVal.find(key) != objVal.end();
    }

    const JsonValue& get(const std::string& key) const {
        static const JsonValue nullVal;
        if (type != Type::OBJECT) return nullVal;
        auto it = objVal.find(key);
        if (it != objVal.end()) return it->second;
        return nullVal;
    }

    std::string getString(const std::string& key, const std::string& defaultVal = "") const {
        const auto& v = get(key);
        if (v.isString()) return v.strVal;
        return defaultVal;
    }

    int64_t getInt(const std::string& key, int64_t defaultVal = 0) const {
        const auto& v = get(key);
        if (v.isNumber()) return v.intVal;
        return defaultVal;
    }

    bool getBool(const std::string& key, bool defaultVal = false) const {
        const auto& v = get(key);
        if (v.isBool()) return v.boolVal;
        return defaultVal;
    }

    void set(const std::string& key, const JsonValue& val) {
        if (type != Type::OBJECT) type = Type::OBJECT;
        objVal[key] = val;
    }

    static std::string escapeString(std::string_view s) {
        std::string out = "\"";
        out.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                        out += buf;
                    } else {
                        out += c;
                    }
                    break;
            }
        }
        out += '"';
        return out;
    }

    std::string serialize() const {
        switch (type) {
            case Type::NUL: return "null";
            case Type::BOOLEAN: return boolVal ? "true" : "false";
            case Type::NUMBER:
                if (isDouble) {
                    std::ostringstream ss;
                    ss << doubleVal;
                    return ss.str();
                }
                return std::to_string(intVal);
            case Type::STRING: return escapeString(strVal);
            case Type::ARRAY: {
                std::string s = "[";
                for (size_t i = 0; i < arrVal.size(); ++i) {
                    if (i > 0) s += ",";
                    s += arrVal[i].serialize();
                }
                s += "]";
                return s;
            }
            case Type::OBJECT: {
                std::string s = "{";
                bool first = true;
                for (const auto& kv : objVal) {
                    if (!first) s += ",";
                    first = false;
                    s += escapeString(kv.first);
                    s += ":";
                    s += kv.second.serialize();
                }
                s += "}";
                return s;
            }
        }
        return "null";
    }

    static bool parse(std::string_view s, JsonValue& outVal, std::string& outErr) {
        size_t idx = 0;
        skipWhitespace(s, idx);
        if (idx >= s.size()) {
            outErr = "Empty JSON payload";
            return false;
        }
        if (!parseValue(s, idx, outVal, outErr)) {
            return false;
        }
        skipWhitespace(s, idx);
        if (idx < s.size()) {
            outErr = "Trailing characters after JSON value at position " + std::to_string(idx);
            return false;
        }
        return true;
    }

private:
    static void skipWhitespace(std::string_view s, size_t& idx) noexcept {
        while (idx < s.size() && (s[idx] == ' ' || s[idx] == '\t' || s[idx] == '\r' || s[idx] == '\n')) {
            ++idx;
        }
    }

    static bool parseValue(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        skipWhitespace(s, idx);
        if (idx >= s.size()) {
            err = "Unexpected end of input";
            return false;
        }
        char c = s[idx];
        if (c == '{') return parseObject(s, idx, val, err);
        if (c == '[') return parseArray(s, idx, val, err);
        if (c == '"') return parseString(s, idx, val, err);
        if (c == 't' || c == 'f') return parseBool(s, idx, val, err);
        if (c == 'n') return parseNull(s, idx, val, err);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(s, idx, val, err);

        err = std::string("Unexpected character '") + c + "' at position " + std::to_string(idx);
        return false;
    }

    static bool parseString(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        if (s[idx] != '"') {
            err = "Expected '\"'";
            return false;
        }
        ++idx;
        std::string result;
        while (idx < s.size()) {
            char c = s[idx++];
            if (c == '"') {
                val.type = Type::STRING;
                val.strVal = std::move(result);
                return true;
            }
            if (c == '\\') {
                if (idx >= s.size()) {
                    err = "Unexpected end of escape sequence";
                    return false;
                }
                char esc = s[idx++];
                switch (esc) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        if (idx + 4 > s.size()) {
                            err = "Incomplete unicode escape";
                            return false;
                        }
                        uint32_t codepoint = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = s[idx++];
                            codepoint <<= 4;
                            if (h >= '0' && h <= '9') codepoint |= (h - '0');
                            else if (h >= 'a' && h <= 'f') codepoint |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') codepoint |= (h - 'A' + 10);
                            else {
                                err = "Invalid hex in unicode escape";
                                return false;
                            }
                        }
                        if (codepoint <= 0x7F) {
                            result += static_cast<char>(codepoint);
                        } else if (codepoint <= 0x7FF) {
                            result += static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            result += static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            result += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default:
                        result += esc;
                        break;
                }
            } else {
                result += c;
            }
        }
        err = "Unterminated string";
        return false;
    }

    static bool parseNumber(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        size_t start = idx;
        if (s[idx] == '-') ++idx;
        if (idx >= s.size() || !std::isdigit(static_cast<unsigned char>(s[idx]))) {
            err = "Invalid number format";
            return false;
        }
        while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
            ++idx;
        }
        bool isFloat = false;
        if (idx < s.size() && s[idx] == '.') {
            isFloat = true;
            ++idx;
            if (idx >= s.size() || !std::isdigit(static_cast<unsigned char>(s[idx]))) {
                err = "Invalid float format";
                return false;
            }
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
                ++idx;
            }
        }
        if (idx < s.size() && (s[idx] == 'e' || s[idx] == 'E')) {
            isFloat = true;
            ++idx;
            if (idx < s.size() && (s[idx] == '+' || s[idx] == '-')) ++idx;
            while (idx < s.size() && std::isdigit(static_cast<unsigned char>(s[idx]))) {
                ++idx;
            }
        }
        std::string numStr(s.substr(start, idx - start));
        val.type = Type::NUMBER;
        val.isDouble = isFloat;
        if (isFloat) {
            val.doubleVal = std::stod(numStr);
            val.intVal = static_cast<int64_t>(val.doubleVal);
        } else {
            val.intVal = std::stoll(numStr);
            val.doubleVal = static_cast<double>(val.intVal);
        }
        return true;
    }

    static bool parseBool(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        if (s.substr(idx, 4) == "true") {
            val.type = Type::BOOLEAN;
            val.boolVal = true;
            idx += 4;
            return true;
        }
        if (s.substr(idx, 5) == "false") {
            val.type = Type::BOOLEAN;
            val.boolVal = false;
            idx += 5;
            return true;
        }
        err = "Invalid boolean literal";
        return false;
    }

    static bool parseNull(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        if (s.substr(idx, 4) == "null") {
            val.type = Type::NUL;
            idx += 4;
            return true;
        }
        err = "Invalid null literal";
        return false;
    }

    static bool parseArray(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        if (s[idx] != '[') {
            err = "Expected '['";
            return false;
        }
        ++idx;
        val.type = Type::ARRAY;
        val.arrVal.clear();
        skipWhitespace(s, idx);
        if (idx < s.size() && s[idx] == ']') {
            ++idx;
            return true;
        }
        while (idx < s.size()) {
            JsonValue elem;
            if (!parseValue(s, idx, elem, err)) return false;
            val.arrVal.push_back(std::move(elem));
            skipWhitespace(s, idx);
            if (idx >= s.size()) break;
            if (s[idx] == ']') {
                ++idx;
                return true;
            }
            if (s[idx] == ',') {
                ++idx;
                skipWhitespace(s, idx);
            } else {
                err = "Expected ',' or ']' in array";
                return false;
            }
        }
        err = "Unclosed array";
        return false;
    }

    static bool parseObject(std::string_view s, size_t& idx, JsonValue& val, std::string& err) {
        if (s[idx] != '{') {
            err = "Expected '{'";
            return false;
        }
        ++idx;
        val.type = Type::OBJECT;
        val.objVal.clear();
        skipWhitespace(s, idx);
        if (idx < s.size() && s[idx] == '}') {
            ++idx;
            return true;
        }
        while (idx < s.size()) {
            skipWhitespace(s, idx);
            if (idx >= s.size() || s[idx] != '"') {
                err = "Expected string key in object";
                return false;
            }
            JsonValue keyVal;
            if (!parseString(s, idx, keyVal, err)) return false;
            skipWhitespace(s, idx);
            if (idx >= s.size() || s[idx] != ':') {
                err = "Expected ':' after object key";
                return false;
            }
            ++idx; // skip ':'
            JsonValue memberVal;
            if (!parseValue(s, idx, memberVal, err)) return false;
            val.objVal[keyVal.strVal] = std::move(memberVal);
            skipWhitespace(s, idx);
            if (idx >= s.size()) break;
            if (s[idx] == '}') {
                ++idx;
                return true;
            }
            if (s[idx] == ',') {
                ++idx;
                skipWhitespace(s, idx);
            } else {
                err = "Expected ',' or '}' in object";
                return false;
            }
        }
        err = "Unclosed object";
        return false;
    }
};

} // namespace json
} // namespace protocol

#endif // JSON_MINI_H
