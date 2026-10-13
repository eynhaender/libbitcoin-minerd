#include "minerd/util/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace minerd::json {
namespace {
const Value& null_value() {
    static const Value v;
    return v;
}
const std::string& empty_string() {
    static const std::string s;
    return s;
}
const Array& empty_array() {
    static const Array a;
    return a;
}
const Object& empty_object() {
    static const Object o;
    return o;
}

void dump_string(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(char(c));
            }
        }
    }
    out.push_back('"');
}

void dump_value(const Value& v, std::string& out);

void dump_number(double d, std::string& out) {
    if (!std::isfinite(d)) { out += "null"; return; }
    char buf[32];
    if (d == std::floor(d) && std::fabs(d) < 9.007199254740992e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(d));
    else
        std::snprintf(buf, sizeof buf, "%.17g", d);
    out += buf;
}

void dump_value(const Value& v, std::string& out) {
    if (v.is_null()) out += "null";
    else if (v.is_bool()) out += v.as_bool() ? "true" : "false";
    else if (v.is_number()) dump_number(v.as_number(), out);
    else if (v.is_string()) dump_string(v.as_string(), out);
    else if (v.is_array()) {
        out.push_back('[');
        bool first = true;
        for (const auto& e : v.as_array()) {
            if (!first) out.push_back(',');
            first = false;
            dump_value(e, out);
        }
        out.push_back(']');
    } else {
        out.push_back('{');
        bool first = true;
        for (const auto& [k, e] : v.as_object()) {
            if (!first) out.push_back(',');
            first = false;
            dump_string(k, out);
            out.push_back(':');
            dump_value(e, out);
        }
        out.push_back('}');
    }
}

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    std::optional<Value> run(std::string* err) {
        auto v = value(0);
        ws();
        if (v && pos_ != t_.size()) fail("trailing characters");
        if (!v || failed_) {
            if (err) *err = err_ + " at offset " + std::to_string(pos_);
            return std::nullopt;
        }
        return v;
    }

private:
    static constexpr int kMaxDepth = 64;
    std::string_view t_;
    std::size_t pos_ = 0;
    bool failed_ = false;
    std::string err_;

    void fail(const char* m) {
        if (!failed_) { failed_ = true; err_ = m; }
    }
    void ws() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_;
    }
    bool lit(std::string_view s) {
        if (t_.substr(pos_, s.size()) == s) { pos_ += s.size(); return true; }
        return false;
    }

    std::optional<Value> value(int depth) {
        if (depth > kMaxDepth) { fail("nesting too deep"); return std::nullopt; }
        ws();
        if (pos_ >= t_.size()) { fail("unexpected end"); return std::nullopt; }
        const char c = t_[pos_];
        if (c == '{') return object(depth);
        if (c == '[') return array(depth);
        if (c == '"') { auto s = string(); if (!s) return std::nullopt; return Value(std::move(*s)); }
        if (lit("true")) return Value(true);
        if (lit("false")) return Value(false);
        if (lit("null")) return Value(nullptr);
        return number();
    }

    std::optional<Value> number() {
        const std::size_t start = pos_;
        if (pos_ < t_.size() && (t_[pos_] == '-' || t_[pos_] == '+')) ++pos_;
        while (pos_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[pos_])) || t_[pos_] == '.' ||
                                    t_[pos_] == 'e' || t_[pos_] == 'E' || t_[pos_] == '-' || t_[pos_] == '+'))
            ++pos_;
        if (start == pos_) { fail("unexpected character"); return std::nullopt; }
        std::string s(t_.substr(start, pos_ - start));
        char* end = nullptr;
        const double d = std::strtod(s.c_str(), &end);
        if (end != s.c_str() + s.size()) { fail("bad number"); return std::nullopt; }
        return Value(d);
    }

    static void append_utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out.push_back(char(cp));
        else if (cp < 0x800) { out.push_back(char(0xC0 | (cp >> 6))); out.push_back(char(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }

    std::optional<unsigned> hex4() {
        if (pos_ + 4 > t_.size()) { fail("bad \\u escape"); return std::nullopt; }
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else { fail("bad \\u escape"); return std::nullopt; }
        }
        return v;
    }

    std::optional<std::string> string() {
        ++pos_;  // opening quote
        std::string out;
        while (pos_ < t_.size()) {
            const char c = t_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (pos_ >= t_.size()) break;
            const char e = t_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto cp = hex4();
                if (!cp) return std::nullopt;
                unsigned code = *cp;
                if (code >= 0xD800 && code <= 0xDBFF && lit("\\u")) {
                    auto lo = hex4();
                    if (!lo) return std::nullopt;
                    code = 0x10000 + ((code - 0xD800) << 10) + (*lo - 0xDC00);
                }
                append_utf8(out, code);
                break;
            }
            default: fail("bad escape"); return std::nullopt;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> array(int depth) {
        ++pos_;
        Array a;
        ws();
        if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return Value(std::move(a)); }
        while (true) {
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            a.push_back(std::move(*v));
            ws();
            if (pos_ < t_.size() && t_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < t_.size() && t_[pos_] == ']') { ++pos_; return Value(std::move(a)); }
            fail("expected , or ]");
            return std::nullopt;
        }
    }

    std::optional<Value> object(int depth) {
        ++pos_;
        Object o;
        ws();
        if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return Value(std::move(o)); }
        while (true) {
            ws();
            if (pos_ >= t_.size() || t_[pos_] != '"') { fail("expected key"); return std::nullopt; }
            auto k = string();
            if (!k) return std::nullopt;
            ws();
            if (pos_ >= t_.size() || t_[pos_] != ':') { fail("expected :"); return std::nullopt; }
            ++pos_;
            auto v = value(depth + 1);
            if (!v) return std::nullopt;
            o[std::move(*k)] = std::move(*v);
            ws();
            if (pos_ < t_.size() && t_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < t_.size() && t_[pos_] == '}') { ++pos_; return Value(std::move(o)); }
            fail("expected , or }");
            return std::nullopt;
        }
    }
};
} // namespace

const std::string& Value::as_string() const { return is_string() ? std::get<std::string>(v_) : empty_string(); }
const Array& Value::as_array() const { return is_array() ? std::get<Array>(v_) : empty_array(); }
const Object& Value::as_object() const { return is_object() ? std::get<Object>(v_) : empty_object(); }

const Value& Value::operator[](const std::string& key) const {
    if (!is_object()) return null_value();
    const auto& o = std::get<Object>(v_);
    auto it = o.find(key);
    return it == o.end() ? null_value() : it->second;
}

const Value& Value::at(std::size_t i) const {
    if (!is_array()) return null_value();
    const auto& a = std::get<Array>(v_);
    return i < a.size() ? a[i] : null_value();
}

std::string Value::dump() const {
    std::string out;
    dump_value(*this, out);
    return out;
}

std::optional<Value> parse(std::string_view text, std::string* error) { return Parser(text).run(error); }

} // namespace minerd::json
