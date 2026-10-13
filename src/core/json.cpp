#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

namespace ln {
namespace json {

static const Value& nullValue() { static const Value v; return v; }

const Value* Value::find(const std::string& key) const {
    if (type != Object) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i)
        if (keys[i] == key) return &items[i];
    return nullptr;
}
const Value& Value::operator[](const std::string& key) const { const Value* v = find(key); return v ? *v : nullValue(); }
const Value& Value::operator[](size_t i) const { return (type == Array && i < items.size()) ? items[i] : nullValue(); }

std::string Value::str(const std::string& def) const {
    if (type == String) return s;
    if (type == Number) {
        char buf[64];
        if (std::fabs(n - std::round(n)) < 1e-9 && std::fabs(n) < 1e15) std::snprintf(buf, sizeof buf, "%lld", (long long)std::llround(n));
        else std::snprintf(buf, sizeof buf, "%g", n);
        return buf;
    }
    if (type == Bool) return b ? "true" : "false";
    return def;
}
double Value::num(double def) const {
    if (type == Number) return n;
    if (type == String && !s.empty()) { char* e = nullptr; double d = std::strtod(s.c_str(), &e); if (e && *e == '\0') return d; }
    return def;
}
int Value::integer(int def) const { double d = num(double(def)); return int(std::llround(d)); }

namespace {
struct Parser {
    const std::string& t;
    size_t p = 0;
    std::string err;
    explicit Parser(const std::string& text) : t(text) {}

    void ws() { while (p < t.size() && (t[p] == ' ' || t[p] == '\t' || t[p] == '\n' || t[p] == '\r')) ++p; }
    bool fail(const char* m) { if (err.empty()) err = std::string(m) + " at offset " + std::to_string(p); return false; }

    static void putUtf8(std::string& o, uint32_t cp) {
        if (cp < 0x80) o += char(cp);
        else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
        else { o += char(0xF0 | (cp >> 18)); o += char(0x80 | ((cp >> 12) & 0x3F)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
    }
    bool hex4(uint32_t& v) {
        if (p + 4 > t.size()) return fail("truncated \\u escape");
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = t[p++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail("bad hex digit");
        }
        return true;
    }
    bool str(std::string& o) {
        if (t[p] != '"') return fail("expected string");
        ++p;
        while (p < t.size()) {
            char c = t[p++];
            if (c == '"') return true;
            if (c != '\\') { o += c; continue; }
            if (p >= t.size()) break;
            char e = t[p++];
            switch (e) {
                case '"': o += '"'; break;  case '\\': o += '\\'; break; case '/': o += '/'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break;  case 'n': o += '\n'; break;
                case 'r': o += '\r'; break; case 't': o += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 1 < t.size() && t[p] == '\\' && t[p + 1] == 'u') {
                        p += 2; uint32_t lo = 0;
                        if (!hex4(lo)) return false;
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else { putUtf8(o, 0xFFFD); cp = lo; }
                    }
                    putUtf8(o, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool number(double& d) {
        // Locale-independent: never trust strtod inside a host process.
        size_t s = p; bool neg = false;
        if (t[p] == '-') { neg = true; ++p; }
        double v = 0; bool digits = false;
        while (p < t.size() && t[p] >= '0' && t[p] <= '9') { v = v * 10 + (t[p++] - '0'); digits = true; }
        if (p < t.size() && t[p] == '.') {
            ++p; double f = 0.1;
            while (p < t.size() && t[p] >= '0' && t[p] <= '9') { v += (t[p++] - '0') * f; f *= 0.1; digits = true; }
        }
        if (!digits) { p = s; return fail("bad number"); }
        if (p < t.size() && (t[p] == 'e' || t[p] == 'E')) {
            ++p; bool eneg = false; int ex = 0;
            if (p < t.size() && (t[p] == '+' || t[p] == '-')) eneg = (t[p++] == '-');
            while (p < t.size() && t[p] >= '0' && t[p] <= '9') ex = ex * 10 + (t[p++] - '0');
            v *= std::pow(10.0, eneg ? -ex : ex);
        }
        d = neg ? -v : v;
        return true;
    }
    bool value(Value& v, int depth) {
        if (depth > 200) return fail("nesting too deep");
        ws();
        if (p >= t.size()) return fail("unexpected end");
        char c = t[p];
        if (c == '{') {
            v.type = Value::Object; ++p; ws();
            if (p < t.size() && t[p] == '}') { ++p; return true; }
            for (;;) {
                ws(); std::string k;
                if (p >= t.size() || !str(k)) return fail("expected key");
                ws(); if (p >= t.size() || t[p] != ':') return fail("expected ':'");
                ++p; Value child;
                if (!value(child, depth + 1)) return false;
                v.keys.push_back(std::move(k)); v.items.push_back(std::move(child));
                ws(); if (p >= t.size()) return fail("unexpected end");
                if (t[p] == ',') { ++p; continue; }
                if (t[p] == '}') { ++p; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Value::Array; ++p; ws();
            if (p < t.size() && t[p] == ']') { ++p; return true; }
            for (;;) {
                Value child;
                if (!value(child, depth + 1)) return false;
                v.items.push_back(std::move(child));
                ws(); if (p >= t.size()) return fail("unexpected end");
                if (t[p] == ',') { ++p; continue; }
                if (t[p] == ']') { ++p; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Value::String; return str(v.s); }
        if (t.compare(p, 4, "true") == 0)  { v.type = Value::Bool; v.b = true;  p += 4; return true; }
        if (t.compare(p, 5, "false") == 0) { v.type = Value::Bool; v.b = false; p += 5; return true; }
        if (t.compare(p, 4, "null") == 0)  { v.type = Value::Null; p += 4; return true; }
        v.type = Value::Number;
        return number(v.n);
    }
};
} // namespace

bool parse(const std::string& text, Value& out, std::string* error) {
    Parser ps(text);
    size_t start = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) start = 3;
    ps.p = start;
    Value v;
    bool ok = ps.value(v, 0);
    if (ok) { ps.ws(); if (ps.p != text.size()) ok = ps.fail("trailing characters"); }
    if (!ok) { if (error) *error = ps.err; return false; }
    out = std::move(v);
    return true;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o += char(c);
        }
    }
    return o + "\"";
}

} // namespace json
} // namespace ln
