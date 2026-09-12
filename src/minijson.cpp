#include "minijson.h"
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace minijson {

// =====================================================================
// Value accessors
// =====================================================================

size_t Value::size() const {
    if (type == Type::Array) return arr.size();
    if (type == Type::Object) return obj.size();
    if (type == Type::String) return str.size();
    if (type == Type::Number) return 1;
    return 0;
}

const Value* Value::get(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (const auto& kv : obj) {
        if (kv.first == key) return &kv.second;
    }
    return nullptr;
}

const Value* Value::at(size_t idx) const {
    if (type != Type::Array || idx >= arr.size()) return nullptr;
    return &arr[idx];
}

static std::string trim_number(std::string s) {
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last == dot) --last;
        s = s.substr(0, last + 1);
    }
    return s;
}

std::string Value::as_string() const {
    if (type == Type::String) return str;
    if (type == Type::Number) return trim_number(std::to_string(num));
    if (type == Type::Bool) return b ? "true" : "false";
    return {};
}

double Value::as_number() const {
    return type == Type::Number ? num : 0.0;
}

// =====================================================================
// Recursive-descent parser
// =====================================================================

namespace {

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out += static_cast<char>(cp);
    } else if (cp <= 0x7FF) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Parses 4 hex digits starting at s[pos].
bool parse_hex4(const std::string& s, size_t pos, uint32_t& out) {
    if (pos + 4 > s.size()) return false;
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
        int h = hex_val(s[pos + k]);
        if (h < 0) return false;
        v = (v << 4) | static_cast<uint32_t>(h);
    }
    out = v;
    return true;
}

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    bool run(Value& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return i_ == s_.size();
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void skip_ws() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }

    bool parse_value(Value& v) {
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '{') return parse_object(v);
        if (c == '[') return parse_array(v);
        if (c == '"') {
            if (!parse_string(v.str)) return false;
            v.type = Type::String;
            return true;
        }
        if (c == 't' || c == 'f') {
            bool val;
            if (c == 't' ? consume("true") : consume("false")) {
                v.b = (c == 't');
                v.type = Type::Bool;
                return true;
            }
            return false;
        }
        if (c == 'n' && consume("null")) {
            v.type = Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i_;
            if (s_[i_] == '-' || s_[i_] == '+') ++i_;
            while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
            if (i_ < s_.size() && s_[i_] == '.') {
                ++i_;
                while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
            }
            if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
                ++i_;
                if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
                while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
            }
            std::string num = s_.substr(start, i_ - start);
            if (num.empty() || num == "-") return false;
            v.num = std::strtod(num.c_str(), nullptr);
            v.type = Type::Number;
            return true;
        }
        return false;
    }

    bool parse_string(std::string& out) {
        // leading '"'
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == '"') { ++i_; return true; }
            if (c != '\\') { out += c; ++i_; continue; }
            if (i_ + 1 >= s_.size()) return false;
            char n = s_[i_ + 1];
            if (n == 'n') { out += '\n'; i_ += 2; continue; }
            if (n == 't') { out += '\t'; i_ += 2; continue; }
            if (n == 'r') { out += '\r'; i_ += 2; continue; }
            if (n == '"' || n == '\\' || n == '/') { out += n; i_ += 2; continue; }
            if (n == 'u') {
                uint32_t cp;
                if (!parse_hex4(s_, i_ + 2, cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (i_ + 11 < s_.size() && s_[i_ + 6] == '\\' && s_[i_ + 7] == 'u') {
                        uint32_t low;
                        if (parse_hex4(s_, i_ + 8, low) && low >= 0xDC00 && low <= 0xDFFF) {
                            uint32_t combined = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            append_utf8(out, combined);
                            i_ += 12;
                            continue;
                        }
                    }
                    append_utf8(out, cp); // unpaired surrogate — emit as-is
                } else {
                    append_utf8(out, cp);
                }
                i_ += 6;
                continue;
            }
            return false; // unknown escape in server JSON
        }
        return false;
    }

    bool consume(const char* lit) {
        size_t len = std::strlen(lit);
        if (s_.compare(i_, len, lit) != 0) return false;
        i_ += len;
        return true;
    }

    bool parse_array(Value& v) {
        ++i_; // '['
        skip_ws();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; v.type = Type::Array; return true; }
        while (i_ < s_.size()) {
            skip_ws();
            Value item;
            if (!parse_value(item)) return false;
            v.arr.push_back(std::move(item));
            skip_ws();
            if (i_ >= s_.size()) return false;
            char c = s_[i_];
            if (c == ',') { ++i_; continue; }
            if (c == ']') { ++i_; v.type = Type::Array; return true; }
            return false;
        }
        return false;
    }

    bool parse_object(Value& v) {
        ++i_; // '{'
        skip_ws();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; v.type = Type::Object; return true; }
        while (i_ < s_.size()) {
            skip_ws();
            std::string key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (i_ >= s_.size() || s_[i_] != ':') return false;
            ++i_;
            skip_ws();
            Value item;
            if (!parse_value(item)) return false;
            v.obj.emplace_back(std::move(key), std::move(item));
            skip_ws();
            if (i_ >= s_.size()) return false;
            char c = s_[i_];
            if (c == ',') { ++i_; continue; }
            if (c == '}') { ++i_; v.type = Type::Object; return true; }
            return false;
        }
        return false;
    }
};

} // namespace

bool parse(const std::string& text, Value& out) {
    Parser p(text);
    return p.run(out);
}

} // namespace minijson