// Minimal JSON reader for the gate tools.
//
// WHY THIS EXISTS: the gates link nothing but the C++ standard library (decision D0-1),
// so that `checks` builds and runs on a bare Linux runner with no Qt and no vcpkg --
// which is what makes "CI runs checks BEFORE compiling" real rather than aspirational.
// That rules out Qt's JSON and any vcpkg-provided library, so we read JSON ourselves.
//
// SCOPE, deliberately small: enough to read our own rules.json and vcpkg.json. It is a
// reader, not a library. No writing, no pretty-printing, no schema validation, no
// duplicate-key policy beyond last-wins. If this file ever grows past a few hundred
// lines, that is a signal the gates have taken on a job that belongs elsewhere.
//
// It DOES report the line and column of a parse error, because a config file that fails
// with "parse error" and no position is a config file people stop editing.

#ifndef RAWPAGE_TOOLS_JSON_H
#define RAWPAGE_TOOLS_JSON_H

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace rawpage::tools {

class JsonValue;
using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

class JsonError : public std::runtime_error {
public:
    JsonError(const std::string& what, int line, int column)
        : std::runtime_error(what + " (line " + std::to_string(line) + ", column "
                             + std::to_string(column) + ")") {}
};

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    JsonValue() = default;
    explicit JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    explicit JsonValue(double n) : type_(Type::Number), number_(n) {}
    explicit JsonValue(std::string s) : type_(Type::String), string_(std::move(s)) {}
    explicit JsonValue(JsonArray a) : type_(Type::Array), array_(std::move(a)) {}
    explicit JsonValue(JsonObject o) : type_(Type::Object), object_(std::move(o)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double asNumber(double fallback = 0.0) const { return type_ == Type::Number ? number_ : fallback; }
    const std::string& asString() const { return string_; }
    const JsonArray& asArray() const { return array_; }
    const JsonObject& asObject() const { return object_; }

    // Object lookup that never throws: a missing key yields a null value, so callers
    // read optional configuration without a cascade of contains() checks.
    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue kNull;
        if (type_ != Type::Object) return kNull;
        auto it = object_.find(key);
        return it == object_.end() ? kNull : it->second;
    }

    // Convenience: read an array of strings, skipping non-strings. Used for every
    // list in rules.json, all of which are string lists.
    std::vector<std::string> asStringList() const {
        std::vector<std::string> out;
        for (const JsonValue& v : array_) {
            if (v.isString()) out.push_back(v.asString());
        }
        return out;
    }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    JsonArray array_;
    JsonObject object_;
};

namespace detail {

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    JsonValue parse() {
        skipWhitespace();
        JsonValue v = parseValue();
        skipWhitespace();
        if (pos_ != text_.size()) fail("trailing content after the top-level value");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& message) const { throw JsonError(message, line_, column_); }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }
    bool done() const { return pos_ >= text_.size(); }

    char take() {
        char c = text_[pos_++];
        if (c == '\n') { ++line_; column_ = 1; } else { ++column_; }
        return c;
    }

    void expect(char c) {
        if (done() || peek() != c) fail(std::string("expected '") + c + "'");
        take();
    }

    void skipWhitespace() {
        while (!done()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { take(); continue; }
            break;
        }
    }

    JsonValue parseValue() {
        if (done()) fail("unexpected end of input");
        switch (peek()) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return JsonValue(parseString());
        case 't': expectLiteral("true");  return JsonValue(true);
        case 'f': expectLiteral("false"); return JsonValue(false);
        case 'n': expectLiteral("null");  return JsonValue();
        default:  return JsonValue(parseNumber());
        }
    }

    void expectLiteral(const std::string& literal) {
        for (char c : literal) {
            if (done() || peek() != c) fail("invalid literal, expected '" + literal + "'");
            take();
        }
    }

    JsonValue parseObject() {
        expect('{');
        JsonObject object;
        skipWhitespace();
        if (peek() == '}') { take(); return JsonValue(std::move(object)); }
        for (;;) {
            skipWhitespace();
            if (peek() != '"') fail("expected a string key");
            std::string key = parseString();
            skipWhitespace();
            expect(':');
            skipWhitespace();
            object[key] = parseValue();     // last wins on a duplicate key
            skipWhitespace();
            if (peek() == ',') { take(); continue; }
            if (peek() == '}') { take(); break; }
            fail("expected ',' or '}' in object");
        }
        return JsonValue(std::move(object));
    }

    JsonValue parseArray() {
        expect('[');
        JsonArray array;
        skipWhitespace();
        if (peek() == ']') { take(); return JsonValue(std::move(array)); }
        for (;;) {
            skipWhitespace();
            array.push_back(parseValue());
            skipWhitespace();
            if (peek() == ',') { take(); continue; }
            if (peek() == ']') { take(); break; }
            fail("expected ',' or ']' in array");
        }
        return JsonValue(std::move(array));
    }

    std::string parseString() {
        expect('"');
        std::string out;
        for (;;) {
            if (done()) fail("unterminated string");
            char c = take();
            if (c == '"') break;
            if (c != '\\') { out.push_back(c); continue; }
            if (done()) fail("unterminated escape sequence");
            char esc = take();
            switch (esc) {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u':  out += parseUnicodeEscape(); break;
            default:   fail("unknown escape sequence");
            }
        }
        return out;
    }

    // \uXXXX -> UTF-8. Surrogate pairs are joined; a lone surrogate is passed through
    // as U+FFFD rather than failing, because a config file is not worth refusing over
    // one malformed character.
    std::string parseUnicodeEscape() {
        auto hex4 = [this]() -> uint32_t {
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                if (done()) fail("truncated \\u escape");
                char c = take();
                value <<= 4;
                if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
                else fail("invalid hex digit in \\u escape");
            }
            return value;
        };

        uint32_t cp = hex4();
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
                take(); take();
                uint32_t low = hex4();
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else {
                    cp = 0xFFFD;
                }
            } else {
                cp = 0xFFFD;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }

        std::string out;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        return out;
    }

    double parseNumber() {
        size_t start = pos_;
        if (peek() == '-') take();
        while (!done() && ((peek() >= '0' && peek() <= '9') || peek() == '.' || peek() == 'e'
                           || peek() == 'E' || peek() == '+' || peek() == '-')) {
            take();
        }
        if (start == pos_) fail("expected a value");
        try {
            return std::stod(text_.substr(start, pos_ - start));
        } catch (const std::exception&) {
            fail("malformed number");
        }
    }

    const std::string& text_;
    size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace detail

inline JsonValue parseJson(const std::string& text) {
    return detail::JsonParser(text).parse();
}

} // namespace rawpage::tools

#endif // RAWPAGE_TOOLS_JSON_H
