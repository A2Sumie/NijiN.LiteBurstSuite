// heraw_suite_nib_bus.hpp -- Native C++20 NOPB/1 & NIB/1 Host Bus, JSONL Server,
// Chart-Aware Resampler, Provenance Tracker, and Sidecar Bundle Codec.
// Wire-exact with sdk/nib_v1.h and sdk/nib_wire.py. Strictly 7-bit ASCII.
#pragma once

#include "sdk/nib_v1.h"
#include "heraw_fs_utf8.hpp"
#include "heraw_suite_handheld_optics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace heraw_bus {

struct BusException : public std::runtime_error {
    std::string code;
    BusException(std::string c, const std::string& msg)
        : std::runtime_error(msg), code(std::move(c)) {}
};

inline void require_bus(bool ok, const char* code, const std::string& msg) {
    if (!ok) throw BusException(code, msg);
}

// --- Pure C++20 RFC 6234 SHA-256 & 64-bit FNV-1a ---
namespace detail {

inline uint32_t rotr32(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32u - n));
}

inline std::string sha256_hex(const std::string& data) {
    static constexpr uint32_t K[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    std::vector<uint8_t> msg(data.begin(), data.end());
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8ull;
    msg.push_back(0x80u);
    while ((msg.size() % 64u) != 56u) msg.push_back(0u);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xffu));

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64u) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const uint8_t* p = &msg[chunk + static_cast<size_t>(i) * 4u];
            w[i] = (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                   (static_cast<uint32_t>(p[2]) << 8)  | static_cast<uint32_t>(p[3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 8; ++i) {
        for (int s = 28; s >= 0; s -= 4) out.push_back(hex[(h[i] >> s) & 0xfu]);
    }
    return out;
}

inline uint64_t fnv1a64(const std::string& s) noexcept {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char b : s) {
        h ^= static_cast<uint64_t>(b);
        h *= 0x100000001b3ull;
    }
    return h;
}

inline void write_le16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
}
inline void write_le32(uint8_t* p, uint32_t v) noexcept {
    write_le16(p, static_cast<uint16_t>(v & 0xffffu));
    write_le16(p + 2, static_cast<uint16_t>((v >> 16) & 0xffffu));
}
inline void write_le64(uint8_t* p, uint64_t v) noexcept {
    write_le32(p, static_cast<uint32_t>(v & 0xffffffffull));
    write_le32(p + 4, static_cast<uint32_t>((v >> 32) & 0xffffffffull));
}
inline void write_lef32(uint8_t* p, float v) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    write_le32(p, bits);
}
inline double read_lef64(const uint8_t* p) noexcept {
    uint64_t bits = nib1_u64(p);
    double v = 0.0;
    std::memcpy(&v, &bits, 8);
    return v;
}
inline float half_to_float(uint16_t h) noexcept {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1fu;
    uint32_t mant = h & 0x03ffu;
    uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) bits = sign;
        else {
            exp = 1;
            while ((mant & 0x0400u) == 0) { mant <<= 1; --exp; }
            mant &= 0x03ffu;
            bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, 4);
    return out;
}

inline size_t dtype_bytes(uint16_t dt) {
    switch (dt) {
        case NIB1_U8: case NIB1_I8: case NIB1_BOOL: return 1;
        case NIB1_U16: case NIB1_I16: case NIB1_F16: return 2;
        case NIB1_I32: case NIB1_U32: case NIB1_F32: return 4;
        case NIB1_F64: case NIB1_I64: case NIB1_U64: return 8;
        default: throw BusException("dtype", "unsupported NIB1 dtype");
    }
}

// --- Compact DOM JSON Value (Sorted-Key ASCII Serializer + Recursive Descent Parser) ---
struct JVal {
    enum Type { NUL, BOOL, INT, REAL, STR, ARR, OBJ } type = NUL;
    bool b = false;
    int64_t i = 0;
    double d = 0.0;
    std::string s;
    std::vector<JVal> arr;
    std::map<std::string, JVal> obj;

    static JVal make_null() { return JVal{}; }
    static JVal make_bool(bool v) { JVal j; j.type = BOOL; j.b = v; return j; }
    static JVal make_int(int64_t v) { JVal j; j.type = INT; j.i = v; j.d = static_cast<double>(v); return j; }
    static JVal make_real(double v) { JVal j; j.type = REAL; j.d = v; return j; }
    static JVal make_str(std::string v) { JVal j; j.type = STR; j.s = std::move(v); return j; }
    static JVal make_arr(std::vector<JVal> v = {}) { JVal j; j.type = ARR; j.arr = std::move(v); return j; }
    static JVal make_obj(std::map<std::string, JVal> v = {}) { JVal j; j.type = OBJ; j.obj = std::move(v); return j; }

    bool is_num() const noexcept { return type == INT || type == REAL; }
    double as_num(double def = 0.0) const noexcept {
        if (type == INT) return static_cast<double>(i);
        if (type == REAL) return d;
        return def;
    }
    int64_t as_int(int64_t def = 0) const noexcept {
        if (type == INT) return i;
        if (type == REAL) return static_cast<int64_t>(d);
        return def;
    }
    std::string as_str(const std::string& def = "") const { return type == STR ? s : def; }
    bool has(const std::string& k) const { return type == OBJ && obj.find(k) != obj.end(); }
    const JVal& at(const std::string& k) const {
        static const JVal kNull{};
        if (type != OBJ) return kNull;
        auto it = obj.find(k);
        return it == obj.end() ? kNull : it->second;
    }
};

inline void dump_ascii_str(const std::string& s, std::string& out) {
    out.push_back('"');
    static constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < s.size(); ) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') { out += "\\\""; ++i; }
        else if (c == '\\') { out += "\\\\"; ++i; }
        else if (c == '\b') { out += "\\b"; ++i; }
        else if (c == '\f') { out += "\\f"; ++i; }
        else if (c == '\n') { out += "\\n"; ++i; }
        else if (c == '\r') { out += "\\r"; ++i; }
        else if (c == '\t') { out += "\\t"; ++i; }
        else if (c < 0x20u) {
            out += "\\u00"; out.push_back(hex[(c >> 4) & 0xf]); out.push_back(hex[c & 0xf]); ++i;
        } else if (c < 0x80u) {
            out.push_back(static_cast<char>(c)); ++i;
        } else {
            uint32_t cp = 0;
            size_t adv = 1;
            if ((c & 0xe0u) == 0xc0u && i + 1 < s.size()) {
                cp = ((c & 0x1fu) << 6) | (static_cast<uint8_t>(s[i + 1]) & 0x3fu); adv = 2;
            } else if ((c & 0xf0u) == 0xe0u && i + 2 < s.size()) {
                cp = ((c & 0x0fu) << 12) | ((static_cast<uint8_t>(s[i + 1]) & 0x3fu) << 6) |
                     (static_cast<uint8_t>(s[i + 2]) & 0x3fu); adv = 3;
            } else if ((c & 0xf8u) == 0xf0u && i + 3 < s.size()) {
                cp = ((c & 0x07u) << 18) | ((static_cast<uint8_t>(s[i + 1]) & 0x3fu) << 12) |
                     ((static_cast<uint8_t>(s[i + 2]) & 0x3fu) << 6) | (static_cast<uint8_t>(s[i + 3]) & 0x3fu); adv = 4;
            }
            if (cp <= 0xffffu) {
                out += "\\u";
                for (int sh = 12; sh >= 0; sh -= 4) out.push_back(hex[(cp >> sh) & 0xfu]);
            } else {
                cp -= 0x10000u;
                const uint32_t hi = 0xd800u + ((cp >> 10) & 0x3ffu);
                const uint32_t lo = 0xdc00u + (cp & 0x3ffu);
                out += "\\u"; for (int sh = 12; sh >= 0; sh -= 4) out.push_back(hex[(hi >> sh) & 0xfu]);
                out += "\\u"; for (int sh = 12; sh >= 0; sh -= 4) out.push_back(hex[(lo >> sh) & 0xfu]);
            }
            i += adv;
        }
    }
    out.push_back('"');
}

inline void dump_json_rec(const JVal& v, std::string& out) {
    switch (v.type) {
        case JVal::NUL: out += "null"; break;
        case JVal::BOOL: out += (v.b ? "true" : "false"); break;
        case JVal::INT: out += std::to_string(v.i); break;
        case JVal::REAL: {
            require_bus(std::isfinite(v.d), "json", "non-finite float in JSON");
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.17g", v.d);
            std::string s(buf);
            if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
                s += ".0";
            }
            out += s;
            break;
        }
        case JVal::STR: dump_ascii_str(v.s, out); break;
        case JVal::ARR:
            out.push_back('[');
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) out.push_back(',');
                dump_json_rec(v.arr[i], out);
            }
            out.push_back(']');
            break;
        case JVal::OBJ: {
            out.push_back('{');
            bool first = true;
            for (const auto& kv : v.obj) {
                if (!first) out.push_back(',');
                first = false;
                dump_ascii_str(kv.first, out);
                out.push_back(':');
                dump_json_rec(kv.second, out);
            }
            out.push_back('}');
            break;
        }
    }
}

inline std::string dump_json_canonical(const JVal& v) {
    std::string out;
    dump_json_rec(v, out);
    return out;
}

struct JParser {
    const std::string& src;
    size_t p = 0;

    void skip_ws() {
        while (p < src.size() && (src[p] == ' ' || src[p] == '\t' || src[p] == '\r' || src[p] == '\n')) ++p;
    }
    char peek() { skip_ws(); require_bus(p < src.size(), "json", "unexpected EOF"); return src[p]; }
    uint32_t read_hex4() {
        require_bus(p + 4 <= src.size(), "json", "truncated \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = src[p++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else throw BusException("json", "bad hex escape");
        }
        return v;
    }
    static void append_utf8(uint32_t cp, std::string& out) {
        if (cp <= 0x7fu) out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7ffu) {
            out.push_back(static_cast<char>(0xc0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else if (cp <= 0xffffu) {
            out.push_back(static_cast<char>(0xe0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        } else {
            out.push_back(static_cast<char>(0xf0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3fu)));
        }
    }
    std::string parse_str() {
        require_bus(peek() == '"', "json", "expected string");
        ++p;
        std::string out;
        while (p < src.size()) {
            char c = src[p++];
            if (c == '"') return out;
            if (c == '\\') {
                require_bus(p < src.size(), "json", "truncated escape");
                char e = src[p++];
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
                        uint32_t cp = read_hex4();
                        if (cp >= 0xd800u && cp <= 0xdbffu && p + 2 <= src.size() && src[p] == '\\' && src[p + 1] == 'u') {
                            p += 2;
                            uint32_t lo = read_hex4();
                            cp = 0x10000u + (((cp - 0xd800u) << 10) | (lo - 0xdc00u));
                        }
                        append_utf8(cp, out);
                        break;
                    }
                    default: throw BusException("json", "invalid escape");
                }
            } else {
                out.push_back(c);
            }
        }
        throw BusException("json", "unterminated string");
    }
    JVal parse_val() {
        char c = peek();
        if (c == '"') return JVal::make_str(parse_str());
        if (c == '{') {
            ++p;
            std::map<std::string, JVal> m;
            if (peek() == '}') { ++p; return JVal::make_obj(std::move(m)); }
            while (true) {
                std::string k = parse_str();
                require_bus(peek() == ':', "json", "expected ':'");
                ++p;
                m[std::move(k)] = parse_val();
                char sep = peek();
                ++p;
                if (sep == '}') break;
                require_bus(sep == ',', "json", "expected ',' or '}'");
            }
            return JVal::make_obj(std::move(m));
        }
        if (c == '[') {
            ++p;
            std::vector<JVal> a;
            if (peek() == ']') { ++p; return JVal::make_arr(std::move(a)); }
            while (true) {
                a.push_back(parse_val());
                char sep = peek();
                ++p;
                if (sep == ']') break;
                require_bus(sep == ',', "json", "expected ',' or ']'");
            }
            return JVal::make_arr(std::move(a));
        }
        if (c == 't' && src.compare(p, 4, "true") == 0) { p += 4; return JVal::make_bool(true); }
        if (c == 'f' && src.compare(p, 5, "false") == 0) { p += 5; return JVal::make_bool(false); }
        if (c == 'n' && src.compare(p, 4, "null") == 0) { p += 4; return JVal::make_null(); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t start = p;
            if (src[p] == '-') ++p;
            while (p < src.size() && src[p] >= '0' && src[p] <= '9') ++p;
            bool is_float = false;
            if (p < src.size() && src[p] == '.') { is_float = true; ++p; while (p < src.size() && src[p] >= '0' && src[p] <= '9') ++p; }
            if (p < src.size() && (src[p] == 'e' || src[p] == 'E')) {
                is_float = true; ++p;
                if (p < src.size() && (src[p] == '+' || src[p] == '-')) ++p;
                while (p < src.size() && src[p] >= '0' && src[p] <= '9') ++p;
            }
            std::string tok = src.substr(start, p - start);
            if (!is_float) {
                try { return JVal::make_int(std::stoll(tok)); } catch (...) {}
            }
            double dv = std::stod(tok);
            require_bus(std::isfinite(dv), "json", "non-finite number");
            return JVal::make_real(dv);
        }
        throw BusException("json", "unexpected token");
    }
};

inline JVal parse_json(const std::string& s) {
    JParser jp{s, 0};
    JVal v = jp.parse_val();
    jp.skip_ws();
    require_bus(jp.p == s.size(), "json", "trailing characters");
    return v;
}

inline bool is_ascii_id(const std::string& s) noexcept {
    if (s.empty()) return false;
    for (unsigned char c : s) if (c >= 0x80u) return false;
    return true;
}

inline std::string wire_key_for_name(const std::string& name) {
    require_bus(!name.empty(), "field", "field name required");
    bool ok = name.size() <= 39;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '.' || c == '_' || c == '-')) { ok = false; break; }
    }
    return ok ? name : ("id." + sha256_hex(name).substr(0, 36));
}

inline std::string canonical_frame_id(const std::string& raw_spec) {
    require_bus(!raw_spec.empty(), "frames", "empty frame specifier");
    if (is_ascii_id(raw_spec)) return raw_spec;
    return "src." + sha256_hex(raw_spec).substr(0, 24);
}

inline std::string format_sensor_geometry_id(int W1, int H1, int crop_left, int crop_top,
                                             bool is_xtrans, const uint8_t cfa6x6[6][6]) {
    char gbuf[192];
    if (!is_xtrans) {
        std::snprintf(gbuf, sizeof(gbuf), "sensor:W%dxH%d:crop%d,%d:bayer:%u%u%u%u",
            W1, H1, crop_left, crop_top,
            static_cast<unsigned>(cfa6x6[0][0] % 3u), static_cast<unsigned>(cfa6x6[0][1] % 3u),
            static_cast<unsigned>(cfa6x6[1][0] % 3u), static_cast<unsigned>(cfa6x6[1][1] % 3u));
        return gbuf;
    }
    char pat[37];
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            pat[r * 6 + c] = static_cast<char>('0' + (cfa6x6[r][c] % 3u));
        }
    }
    pat[36] = '\0';
    std::snprintf(gbuf, sizeof(gbuf), "sensor:W%dxH%d:crop%d,%d:xtrans:%s",
        W1, H1, crop_left, crop_top, pat);
    return gbuf;
}

inline const char* stage_name(uint32_t st) noexcept {
    static constexpr const char* kNames[6] = {
        "EXT_INPUT", "S1_CALIB", "S2_ALIGN", "S3_FUSE", "S4_OPTICS", "S5_DELIVER"
    };
    return st < 6 ? kNames[st] : "S3_FUSE";
}

inline uint32_t parse_stage_code(const std::string& s) {
    for (uint32_t i = 0; i < 6; ++i) if (s == stage_name(i)) return i;
    throw BusException("stage", "unknown stage: " + s);
}

inline const char* origin_name_str(uint16_t orig) noexcept {
    switch (orig) {
        case 0: return "nijin_measured";
        case 1: return "co_calibrated";
        case 2: return "testable_geom";
        case 3: return "partition_mask";
        case 4: return "generative_prior";
        default: return "unknown";
    }
}

} // namespace detail

struct ChartSpec {
    std::array<double, 6> affine = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0};
    std::array<double, 4> bbox = {0.0, 0.0, 1.0, 1.0};
    std::array<double, 2> canvas_wh = {1.0, 1.0};
    std::string space = "active_1x";

    static ChartSpec active(uint32_t w, uint32_t h) {
        ChartSpec c;
        c.bbox = {0.0, 0.0, static_cast<double>(w), static_cast<double>(h)};
        c.canvas_wh = {static_cast<double>(w), static_cast<double>(h)};
        return c;
    }

    std::array<uint32_t, 4> int_bbox() const noexcept {
        const uint32_t ix0 = static_cast<uint32_t>(std::max(0.0, std::floor(bbox[0] + 1e-6)));
        const uint32_t iy0 = static_cast<uint32_t>(std::max(0.0, std::floor(bbox[1] + 1e-6)));
        const uint32_t ix1 = std::max(ix0 + 1u, static_cast<uint32_t>(std::ceil(bbox[2] - 1e-6)));
        const uint32_t iy1 = std::max(iy0 + 1u, static_cast<uint32_t>(std::ceil(bbox[3] - 1e-6)));
        return {ix0, iy0, ix1, iy1};
    }

    bool sample_at_active(double x_act, double y_act, double& u_out, double& v_out) const noexcept {
        const double a = affine[0], b = affine[1], tx = affine[2];
        const double c = affine[3], d = affine[4], ty = affine[5];
        const double det = a * d - b * c;
        if (std::fabs(det) <= 1e-12) return false;
        const double u = (d * (x_act - tx) - b * (y_act - ty)) / det;
        const double v = (a * (y_act - ty) - c * (x_act - tx)) / det;
        if (!(bbox[0] <= u && u < bbox[2] && bbox[1] <= v && v < bbox[3])) return false;
        const int64_t i_min = static_cast<int64_t>(std::ceil(bbox[0] - 0.5));
        const int64_t i_max = static_cast<int64_t>(std::ceil(bbox[2] - 0.5)) - 1;
        const int64_t j_min = static_cast<int64_t>(std::ceil(bbox[1] - 0.5));
        const int64_t j_max = static_cast<int64_t>(std::ceil(bbox[3] - 0.5)) - 1;
        if (i_min > i_max || j_min > j_max) return false;
        u_out = std::clamp(u, static_cast<double>(i_min) + 0.5, static_cast<double>(i_max) + 0.5);
        v_out = std::clamp(v, static_cast<double>(j_min) + 0.5, static_cast<double>(j_max) + 0.5);
        return true;
    }

    std::pair<double, double> map_to_active(double u, double v) const noexcept {
        return {affine[0] * u + affine[1] * v + affine[2],
                affine[3] * u + affine[4] * v + affine[5]};
    }

    detail::JVal to_jval() const {
        using detail::JVal;
        std::vector<JVal> af, bb, cw;
        for (double x : affine) af.push_back(JVal::make_real(x));
        for (double x : bbox) bb.push_back(JVal::make_real(x));
        for (double x : canvas_wh) cw.push_back(JVal::make_real(x));
        return JVal::make_obj({
            {"affine", JVal::make_arr(std::move(af))},
            {"bbox", JVal::make_arr(std::move(bb))},
            {"canvas_wh", JVal::make_arr(std::move(cw))},
            {"space", JVal::make_str(space)}
        });
    }

    static ChartSpec from_jval(const detail::JVal& j) {
        ChartSpec c;
        const auto& af = j.at("affine").arr;
        const auto& bb = j.at("bbox").arr;
        require_bus(af.size() == 6 && bb.size() == 4, "chart", "invalid chart JSON");
        for (size_t i = 0; i < 6; ++i) c.affine[i] = af[i].as_num();
        for (size_t i = 0; i < 4; ++i) c.bbox[i] = bb[i].as_num();
        if (j.has("canvas_wh") && j.at("canvas_wh").arr.size() == 2) {
            c.canvas_wh = {j.at("canvas_wh").arr[0].as_num(), j.at("canvas_wh").arr[1].as_num()};
        } else {
            c.canvas_wh = {std::max(1.0, c.bbox[2]), std::max(1.0, c.bbox[3])};
        }
        c.space = j.at("space").as_str("active_1x");
        require_bus(std::fabs(c.affine[0] * c.affine[4] - c.affine[1] * c.affine[3]) > 1e-12 &&
                    c.bbox[0] >= 0.0 && c.bbox[0] < c.bbox[2] && c.bbox[1] >= 0.0 && c.bbox[1] < c.bbox[3],
                    "chart", "singular affine or invalid bbox");
        return c;
    }
};

struct NibTensor {
    std::string public_name;
    std::string wire_key;
    uint16_t dtype = NIB1_F32;
    uint16_t kind = NIB1_SCALAR;
    uint16_t origin = NIB1_ORIG_PARTITION_MASK;
    int32_t frame = -1;
    int32_t layer = -1;
    std::array<uint32_t, 4> shape = {1, 1, 1, 1};
    std::array<float, 6> affine = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    std::array<uint32_t, 4> bbox = {0, 0, 1, 1};
    detail::JVal meta = detail::JVal::make_obj();
    std::vector<uint8_t> data;

    std::vector<uint64_t> logical_dims() const {
        if (kind == NIB1_ND) {
            std::vector<uint64_t> out;
            for (const auto& d : meta.at("dims").arr) out.push_back(static_cast<uint64_t>(d.as_int()));
            return out;
        }
        return {shape[0], shape[1], shape[2], shape[3]};
    }

    size_t element_count() const {
        size_t prod = 1;
        for (uint64_t n : logical_dims()) prod *= static_cast<size_t>(n);
        return prod;
    }

    std::vector<double> unpack_doubles() const {
        const size_t n = element_count();
        const size_t ib = detail::dtype_bytes(dtype);
        require_bus(data.size() == n * ib, "shape", "payload byte size mismatch");
        std::vector<double> out(n, 0.0);
        for (size_t i = 0; i < n; ++i) {
            const uint8_t* p = &data[i * ib];
            switch (dtype) {
                case NIB1_U8: case NIB1_BOOL: out[i] = static_cast<double>(p[0]); break;
                case NIB1_I8: out[i] = static_cast<double>(static_cast<int8_t>(p[0])); break;
                case NIB1_U16: out[i] = static_cast<double>(nib1_u16(p)); break;
                case NIB1_I16: out[i] = static_cast<double>(static_cast<int16_t>(nib1_u16(p))); break;
                case NIB1_I32: out[i] = static_cast<double>(static_cast<int32_t>(nib1_u32(p))); break;
                case NIB1_U32: out[i] = static_cast<double>(nib1_u32(p)); break;
                case NIB1_F16: out[i] = static_cast<double>(detail::half_to_float(nib1_u16(p))); break;
                case NIB1_F32: out[i] = static_cast<double>(nib1_f32(p)); break;
                case NIB1_F64: out[i] = detail::read_lef64(p); break;
                case NIB1_I64: out[i] = static_cast<double>(static_cast<int64_t>(nib1_u64(p))); break;
                case NIB1_U64: out[i] = static_cast<double>(nib1_u64(p)); break;
                default: throw BusException("dtype", "unsupported dtype");
            }
        }
        return out;
    }

    void decode_rle_plane(int& h_out, int& w_out, std::vector<float>& plane_out) const {
        const auto& hw = meta.at("mask_hw").arr;
        require_bus(hw.size() == 2, "rle", "missing mask_hw");
        h_out = static_cast<int>(hw[0].as_int());
        w_out = static_cast<int>(hw[1].as_int());
        require_bus(h_out > 0 && w_out > 0 && (data.size() % 4u) == 0u, "rle", "invalid RLE tensor");
        const size_t n_runs = data.size() / 4u;
        std::vector<uint8_t> flat;
        flat.reserve(static_cast<size_t>(h_out) * static_cast<size_t>(w_out));
        uint8_t val = 0;
        size_t total = 0;
        for (size_t r = 0; r < n_runs; ++r) {
            const uint32_t cnt = nib1_u32(&data[r * 4u]);
            total += cnt;
            require_bus(total <= static_cast<size_t>(h_out) * static_cast<size_t>(w_out), "rle", "RLE overflow");
            flat.insert(flat.end(), cnt, val);
            val ^= 1u;
        }
        require_bus(total == static_cast<size_t>(h_out) * static_cast<size_t>(w_out), "rle", "RLE sum mismatch");
        plane_out.assign(total, 0.0f);
        const std::string order = meta.at("rle_order").as_str("F");
        if (order == "C") {
            for (size_t i = 0; i < total; ++i) plane_out[i] = static_cast<float>(flat[i]);
        } else {
            size_t idx = 0;
            for (int x = 0; x < w_out; ++x) {
                for (int y = 0; y < h_out; ++y) {
                    plane_out[static_cast<size_t>(y) * w_out + x] = static_cast<float>(flat[idx++]);
                }
            }
        }
    }
};

struct SessionIdentity {
    uint32_t active_w = 0;
    uint32_t active_h = 0;
    int32_t anchor = 0;
    std::vector<std::string> frames;
    std::string geometry_id;

    detail::JVal to_jval() const {
        using detail::JVal;
        std::vector<JVal> fr;
        for (const auto& f : frames) fr.push_back(JVal::make_str(f));
        return JVal::make_obj({
            {"active_wh", JVal::make_arr({JVal::make_int(active_w), JVal::make_int(active_h)})},
            {"anchor", JVal::make_int(anchor)},
            {"frames", JVal::make_arr(std::move(fr))},
            {"geometry_id", JVal::make_str(geometry_id)}
        });
    }

    std::string canonical_json() const { return detail::dump_json_canonical(to_jval()); }
    std::string sha256_id() const { return detail::sha256_hex(canonical_json()); }
    uint64_t quick_fnv() const { return detail::fnv1a64(canonical_json()); }

    static SessionIdentity from_jval(const detail::JVal& j) {
        SessionIdentity s;
        const auto& wh = j.at("active_wh").arr;
        const auto& fr = j.at("frames").arr;
        require_bus(wh.size() == 2 && !fr.empty(), "session", "invalid session identity");
        s.active_w = static_cast<uint32_t>(wh[0].as_int());
        s.active_h = static_cast<uint32_t>(wh[1].as_int());
        s.anchor = static_cast<int32_t>(j.at("anchor").as_int());
        for (const auto& f : fr) s.frames.push_back(f.as_str());
        s.geometry_id = j.at("geometry_id").as_str();
        require_bus(s.active_w > 0 && s.active_h > 0 && s.anchor >= 0 &&
                    static_cast<size_t>(s.anchor) < s.frames.size() && !s.geometry_id.empty(),
                    "session", "invalid session bounds");
        return s;
    }

    void verify_matches(const SessionIdentity& expected) const {
        std::vector<std::string> diffs;
        if (active_w != expected.active_w || active_h != expected.active_h) diffs.push_back("active_wh");
        if (anchor != expected.anchor) diffs.push_back("anchor");
        if (frames != expected.frames) diffs.push_back("frames");
        if (geometry_id != expected.geometry_id) diffs.push_back("geometry_id");
        if (!diffs.empty()) {
            std::string msg = "session mismatch: ";
            for (size_t i = 0; i < diffs.size(); ++i) { if (i) msg += ", "; msg += diffs[i]; }
            throw BusException("session", msg);
        }
    }
};

struct NibBundle {
    uint32_t stage = 3;
    int dirty_from = -1; // -1 = clean
    SessionIdentity session;
    detail::JVal session_meta = detail::JVal::make_obj();
    std::map<std::string, ChartSpec> charts;
    detail::JVal policy = detail::JVal::make_obj();
    std::vector<std::string> field_order;
    std::map<std::string, NibTensor> fields;

    void init_host(const SessionIdentity& id, uint32_t st = 3) {
        session = id;
        stage = st;
        dirty_from = -1;
        charts["active_1x"] = ChartSpec::active(id.active_w, id.active_h);
    }

    void mark_dirty(uint32_t st) {
        int idx = static_cast<int>(std::max(1u, st));
        if (dirty_from < 0 || idx < dirty_from) dirty_from = idx;
    }

    std::vector<std::string> cached_stages() const {
        const int lim = (dirty_from < 0) ? static_cast<int>(stage) : std::min(static_cast<int>(stage), dirty_from - 1);
        std::vector<std::string> out;
        for (int i = 1; i <= lim; ++i) out.push_back(detail::stage_name(static_cast<uint32_t>(i)));
        return out;
    }

    std::vector<std::string> invalidated_stages() const {
        std::vector<std::string> out;
        if (dirty_from < 0) return out;
        for (int i = std::max(1, dirty_from); i <= 5; ++i) out.push_back(detail::stage_name(static_cast<uint32_t>(i)));
        return out;
    }

    void put_tensor(NibTensor t, bool replace = true) {
        if (fields.find(t.public_name) == fields.end()) {
            field_order.push_back(t.public_name);
        } else {
            require_bus(replace, "exists", "field already exists: " + t.public_name);
        }
        fields[t.public_name] = std::move(t);
    }

    ChartSpec chart_for_tensor(const NibTensor& t) const {
        const std::string cid = t.meta.at("chart").as_str();
        if (!cid.empty()) {
            auto it = charts.find(cid);
            if (it != charts.end()) return it->second;
        }
        ChartSpec c;
        for (int i = 0; i < 6; ++i) c.affine[static_cast<size_t>(i)] = static_cast<double>(t.affine[static_cast<size_t>(i)]);
        if (t.meta.has("valid_bbox_f") && t.meta.at("valid_bbox_f").arr.size() == 4) {
            for (size_t i = 0; i < 4; ++i) c.bbox[i] = t.meta.at("valid_bbox_f").arr[i].as_num();
        } else {
            for (size_t i = 0; i < 4; ++i) c.bbox[i] = static_cast<double>(t.bbox[i]);
        }
        c.canvas_wh = {std::max(1.0, c.bbox[2]), std::max(1.0, c.bbox[3])};
        return c;
    }

    // Sample any scalar/RLE tensor (U8/F32/F64/RLE/logits) onto dst_chart (dst_h x dst_w)
    std::vector<float> sample_scalar_2d(const std::string& name, const ChartSpec& dst_chart,
                                        int dst_h, int dst_w, float def_val = 0.0f) const {
        auto it = fields.find(name);
        require_bus(it != fields.end(), "field", "missing field: " + name);
        const NibTensor& t = it->second;
        int sh = 0, sw = 0;
        std::vector<float> vals;
        bool discrete = false;
        const std::string enc = t.meta.at("encoding").as_str();
        if (enc == "rle_runs" || enc == "coco_rle") {
            t.decode_rle_plane(sh, sw, vals);
            discrete = true;
        } else {
            const auto dims = t.logical_dims();
            if (dims.size() == 4 && dims[0] == 1 && dims[3] == 1) {
                sh = static_cast<int>(dims[1]); sw = static_cast<int>(dims[2]);
            } else if (dims.size() == 2) {
                sh = static_cast<int>(dims[0]); sw = static_cast<int>(dims[1]);
            } else {
                throw BusException("shape", "unsupported 2D scalar shape for " + name);
            }
            const auto dv = t.unpack_doubles();
            vals.resize(dv.size());
            for (size_t i = 0; i < dv.size(); ++i) vals[i] = static_cast<float>(dv[i]);
            const std::string sem = t.meta.at("semantic").as_str();
            discrete = (t.dtype != NIB1_F16 && t.dtype != NIB1_F32 && t.dtype != NIB1_F64) ||
                       sem.rfind("mask.binary", 0) == 0;
        }
        const ChartSpec src_chart = chart_for_tensor(t);
        const bool is_logit = (t.meta.at("semantic").as_str().rfind("mask.logit", 0) == 0);
        std::vector<float> out(static_cast<size_t>(dst_h) * static_cast<size_t>(dst_w), def_val);
        for (int row = 0; row < dst_h; ++row) {
            for (int col = 0; col < dst_w; ++col) {
                const auto [x_act, y_act] = dst_chart.map_to_active(col + 0.5, row + 0.5);
                double u = 0.0, v = 0.0;
                if (!src_chart.sample_at_active(x_act, y_act, u, v)) continue;
                float val = 0.0f;
                if (discrete) {
                    const int ix = std::clamp(static_cast<int>(std::floor(u)), 0, sw - 1);
                    const int iy = std::clamp(static_cast<int>(std::floor(v)), 0, sh - 1);
                    val = vals[static_cast<size_t>(iy) * sw + ix];
                } else {
                    val = heraw_hoptics::detail::sample_scalar_clamped(
                        vals.data(), sh, sw, static_cast<float>(v - 0.5), static_cast<float>(u - 0.5));
                }
                out[static_cast<size_t>(row) * dst_w + col] = is_logit ? (val > 0.0f ? 1.0f : 0.0f) : val;
            }
        }
        return out;
    }

    detail::JVal describe_jval() const {
        using detail::JVal;
        std::map<std::string, JVal> ch_map, f_map;
        for (const auto& kv : charts) ch_map[kv.first] = kv.second.to_jval();
        for (const auto& name : field_order) {
            const auto& t = fields.at(name);
            JVal fm = t.meta;
            std::vector<JVal> sh, af, bb;
            for (uint64_t d : t.logical_dims()) sh.push_back(JVal::make_int(static_cast<int64_t>(d)));
            for (float a : t.affine) af.push_back(JVal::make_real(a));
            for (uint32_t b : t.bbox) bb.push_back(JVal::make_int(b));
            fm.obj["shape"] = JVal::make_arr(std::move(sh));
            fm.obj["dtype"] = JVal::make_int(t.dtype);
            fm.obj["kind"] = JVal::make_int(t.kind);
            fm.obj["origin"] = JVal::make_int(t.origin);
            fm.obj["origin_name"] = JVal::make_str(detail::origin_name_str(t.origin));
            fm.obj["frame"] = JVal::make_int(t.frame);
            fm.obj["layer"] = JVal::make_int(t.layer);
            fm.obj["affine"] = JVal::make_arr(std::move(af));
            fm.obj["bbox"] = JVal::make_arr(std::move(bb));
            f_map[name] = std::move(fm);
        }
        std::vector<JVal> cs, iv;
        for (const auto& s : cached_stages()) cs.push_back(JVal::make_str(s));
        for (const auto& s : invalidated_stages()) iv.push_back(JVal::make_str(s));
        return JVal::make_obj({
            {"api", JVal::make_str("nopb/1")},
            {"stage", JVal::make_str(detail::stage_name(stage))},
            {"cached_stages", JVal::make_arr(std::move(cs))},
            {"invalidated_stages", JVal::make_arr(std::move(iv))},
            {"session_id", JVal::make_str(session.sha256_id())},
            {"session", session.to_jval()},
            {"session_meta", session_meta},
            {"charts", JVal::make_obj(std::move(ch_map))},
            {"policy", policy},
            {"fields", JVal::make_obj(std::move(f_map))}
        });
    }
};

inline NibBundle read_nib_file(const std::string& path, const SessionIdentity* expected = nullptr) {
    std::ifstream fp(heraw_fs::from_utf8(path), std::ios::binary);
    require_bus(static_cast<bool>(fp), "io", "cannot open NIB file: " + path);
    fp.seekg(0, std::ios::end);
    const uint64_t end = static_cast<uint64_t>(fp.tellg());
    require_bus(end >= NIB1_HEADER_BYTES, "truncated", "truncated NIB header");
    fp.seekg(0, std::ios::beg);
    uint8_t hdr[NIB1_HEADER_BYTES];
    fp.read(reinterpret_cast<char*>(hdr), NIB1_HEADER_BYTES);
    require_bus(std::memcmp(hdr + NIB1_H_MAGIC, "NIJBUS01", 8) == 0 &&
                nib1_u32(hdr + NIB1_H_VERSION) == 1, "version", "invalid NIB/1 magic or version");

    const uint32_t stage = nib1_u32(hdr + NIB1_H_STAGE);
    const uint32_t nf = nib1_u32(hdr + NIB1_H_FRAMES);
    const uint32_t w = nib1_u32(hdr + NIB1_H_WIDTH);
    const uint32_t h = nib1_u32(hdr + NIB1_H_HEIGHT);
    const uint32_t count = nib1_u32(hdr + NIB1_H_COUNT);
    const uint64_t quick = nib1_u64(hdr + NIB1_H_QUICK_ID);
    const uint64_t jo = nib1_u64(hdr + NIB1_H_META_OFFSET);
    const uint64_t jb = nib1_u64(hdr + NIB1_H_META_BYTES);
    const uint64_t dir = nib1_u64(hdr + NIB1_H_DIRECTORY);
    require_bus(stage <= 5 && dir == NIB1_HEADER_BYTES &&
                jo == dir + static_cast<uint64_t>(count) * NIB1_DESC_BYTES &&
                jb > 0 && jb <= (16ull << 20) && jo + jb <= end, "bounds", "invalid NIB directory/manifest");

    std::string j_str(static_cast<size_t>(jb), '\0');
    fp.seekg(static_cast<std::streamoff>(jo), std::ios::beg);
    fp.read(j_str.data(), static_cast<std::streamsize>(jb));
    const detail::JVal manifest = detail::parse_json(j_str);

    NibBundle b;
    b.stage = stage;
    b.session = SessionIdentity::from_jval(manifest.at("session"));
    require_bus(b.session.frames.size() == nf && b.session.active_w == w && b.session.active_h == h &&
                manifest.at("session_id").as_str() == b.session.sha256_id() &&
                b.session.quick_fnv() == quick, "session", "session/header hash mismatch");
    if (expected) b.session.verify_matches(*expected);

    if (manifest.has("session_meta") && manifest.at("session_meta").type == detail::JVal::OBJ) {
        b.session_meta = manifest.at("session_meta");
    }
    if (manifest.has("policy") && manifest.at("policy").type == detail::JVal::OBJ) {
        b.policy = manifest.at("policy");
    }
    b.charts["active_1x"] = ChartSpec::active(w, h);
    if (manifest.has("charts") && manifest.at("charts").type == detail::JVal::OBJ) {
        for (const auto& kv : manifest.at("charts").obj) {
            b.charts[kv.first] = ChartSpec::from_jval(kv.second);
        }
    }

    const auto& f_meta_map = manifest.at("fields").obj;
    require_bus(f_meta_map.size() == count, "fields", "descriptor/manifest count mismatch");

    fp.seekg(static_cast<std::streamoff>(dir), std::ios::beg);
    uint64_t next_off = ((jo + jb) + 63ull) & ~63ull;
    std::vector<std::pair<std::string, uint64_t>> offsets;
    for (uint32_t idx = 0; idx < count; ++idx) {
        uint8_t d[NIB1_DESC_BYTES];
        fp.read(reinterpret_cast<char*>(d), NIB1_DESC_BYTES);
        size_t nlen = 0;
        while (nlen < NIB1_D_NAME_BYTES && d[nlen] != 0) ++nlen;
        require_bus(nlen > 0 && nlen < NIB1_D_NAME_BYTES, "name", "unterminated wire key");
        for (size_t pad = nlen; pad < NIB1_D_NAME_BYTES; ++pad) {
            require_bus(d[pad] == 0, "name", "nonzero wire key padding");
        }
        const std::string key(reinterpret_cast<const char*>(d), nlen);
        auto mit = f_meta_map.find(key);
        require_bus(mit != f_meta_map.end(), "field", "unbound descriptor: " + key);
        require_bus(nib1_u16(d + NIB1_D_CODEC) == 0, "codec", "unsupported codec");

        NibTensor t;
        t.wire_key = key;
        t.meta = mit->second;
        t.public_name = t.meta.at("public_name").as_str(key);
        t.dtype = nib1_u16(d + NIB1_D_TYPE);
        t.kind = nib1_u16(d + NIB1_D_KIND);
        t.origin = nib1_u16(d + NIB1_D_ORIGIN);
        t.frame = static_cast<int32_t>(nib1_u32(d + NIB1_D_FRAME));
        t.layer = static_cast<int32_t>(nib1_u32(d + NIB1_D_LAYER));
        for (int s = 0; s < 4; ++s) t.shape[static_cast<size_t>(s)] = nib1_u32(d + NIB1_D_SHAPE + s * 4);
        for (int s = 0; s < 6; ++s) t.affine[static_cast<size_t>(s)] = nib1_f32(d + NIB1_D_AFFINE + s * 4);
        for (int s = 0; s < 4; ++s) t.bbox[static_cast<size_t>(s)] = nib1_u32(d + NIB1_D_BBOX + s * 4);
        const uint64_t off = nib1_u64(d + NIB1_D_OFFSET);
        const uint64_t nbytes = nib1_u64(d + NIB1_D_BYTES);
        const uint64_t expected_bytes = static_cast<uint64_t>(t.element_count()) * detail::dtype_bytes(t.dtype);
        require_bus(off == next_off && nbytes == expected_bytes && off + nbytes <= end,
                    "bounds", "payload offset/length mismatch");
        t.data.resize(static_cast<size_t>(nbytes));
        offsets.push_back({t.public_name, off});
        next_off = ((off + nbytes) + 63ull) & ~63ull;
        b.put_tensor(std::move(t));
    }
    for (const auto& kv : offsets) {
        auto& t = b.fields.at(kv.first);
        if (!t.data.empty()) {
            fp.seekg(static_cast<std::streamoff>(kv.second), std::ios::beg);
            fp.read(reinterpret_cast<char*>(t.data.data()), static_cast<std::streamsize>(t.data.size()));
        }
    }
    return b;
}

inline void write_nib_file(const std::string& path, const NibBundle& b,
                           const std::vector<std::string>* filter_names = nullptr) {
    using detail::JVal;
    std::vector<std::string> names;
    if (filter_names && !filter_names->empty()) {
        for (const auto& n : *filter_names) {
            require_bus(b.fields.find(n) != b.fields.end(), "field", "missing export field: " + n);
            names.push_back(n);
        }
    } else {
        names = b.field_order;
    }

    std::map<std::string, JVal> fields_meta;
    for (const auto& n : names) {
        const auto& t = b.fields.at(n);
        const std::string key = detail::wire_key_for_name(n);
        JVal m = t.meta;
        if (key != n) m.obj["public_name"] = JVal::make_str(n);
        else m.obj.erase("public_name");
        fields_meta[key] = std::move(m);
    }
    std::map<std::string, JVal> doc_map = {
        {"session", b.session.to_jval()},
        {"session_id", JVal::make_str(b.session.sha256_id())},
        {"fields", JVal::make_obj(std::move(fields_meta))}
    };
    if (!b.charts.empty()) {
        std::map<std::string, JVal> cm;
        for (const auto& kv : b.charts) cm[kv.first] = kv.second.to_jval();
        doc_map["charts"] = JVal::make_obj(std::move(cm));
    }
    if (b.policy.type == JVal::OBJ && !b.policy.obj.empty()) doc_map["policy"] = b.policy;
    if (b.session_meta.type == JVal::OBJ && !b.session_meta.obj.empty()) doc_map["session_meta"] = b.session_meta;

    const std::string manifest = detail::dump_json_canonical(JVal::make_obj(std::move(doc_map)));
    const uint64_t jo = NIB1_HEADER_BYTES + static_cast<uint64_t>(names.size()) * NIB1_DESC_BYTES;
    uint64_t pos = ((jo + manifest.size()) + 63ull) & ~63ull;

    std::vector<std::array<uint8_t, NIB1_DESC_BYTES>> descs(names.size());
    std::vector<uint64_t> offs(names.size());
    for (size_t idx = 0; idx < names.size(); ++idx) {
        const auto& t = b.fields.at(names[idx]);
        const std::string key = detail::wire_key_for_name(names[idx]);
        auto& d = descs[idx];
        d.fill(0);
        std::memcpy(d.data() + NIB1_D_NAME, key.data(), key.size());
        detail::write_le16(d.data() + NIB1_D_TYPE, t.dtype);
        detail::write_le16(d.data() + NIB1_D_KIND, t.kind);
        detail::write_le16(d.data() + NIB1_D_ORIGIN, t.origin);
        detail::write_le16(d.data() + NIB1_D_CODEC, 0);
        detail::write_le32(d.data() + NIB1_D_FRAME, static_cast<uint32_t>(t.frame));
        detail::write_le32(d.data() + NIB1_D_LAYER, static_cast<uint32_t>(t.layer));
        for (int s = 0; s < 4; ++s) {
            const uint32_t dim_s = (t.kind == NIB1_ND) ? 0u : t.shape[static_cast<size_t>(s)];
            detail::write_le32(d.data() + NIB1_D_SHAPE + s * 4, dim_s);
        }
        for (int s = 0; s < 6; ++s) detail::write_lef32(d.data() + NIB1_D_AFFINE + s * 4, t.affine[static_cast<size_t>(s)]);
        for (int s = 0; s < 4; ++s) detail::write_le32(d.data() + NIB1_D_BBOX + s * 4, t.bbox[static_cast<size_t>(s)]);
        detail::write_le64(d.data() + NIB1_D_OFFSET, pos);
        detail::write_le64(d.data() + NIB1_D_BYTES, static_cast<uint64_t>(t.data.size()));
        offs[idx] = pos;
        pos = ((pos + t.data.size()) + 63ull) & ~63ull;
    }

    uint8_t hdr[NIB1_HEADER_BYTES] = {0};
    std::memcpy(hdr + NIB1_H_MAGIC, "NIJBUS01", 8);
    detail::write_le32(hdr + NIB1_H_VERSION, 1);
    detail::write_le32(hdr + NIB1_H_STAGE, b.stage);
    detail::write_le32(hdr + NIB1_H_FRAMES, static_cast<uint32_t>(b.session.frames.size()));
    detail::write_le32(hdr + NIB1_H_WIDTH, b.session.active_w);
    detail::write_le32(hdr + NIB1_H_HEIGHT, b.session.active_h);
    detail::write_le32(hdr + NIB1_H_COUNT, static_cast<uint32_t>(names.size()));
    detail::write_le64(hdr + NIB1_H_QUICK_ID, b.session.quick_fnv());
    detail::write_le64(hdr + NIB1_H_META_OFFSET, jo);
    detail::write_le64(hdr + NIB1_H_META_BYTES, static_cast<uint64_t>(manifest.size()));
    detail::write_le64(hdr + NIB1_H_DIRECTORY, NIB1_HEADER_BYTES);

    const std::string tmp_path = path + ".tmp_nib";
    {
        std::ofstream fp(heraw_fs::from_utf8(tmp_path), std::ios::binary | std::ios::trunc);
        require_bus(static_cast<bool>(fp), "io", "cannot write NIB file: " + path);
        fp.write(reinterpret_cast<const char*>(hdr), NIB1_HEADER_BYTES);
        for (const auto& d : descs) fp.write(reinterpret_cast<const char*>(d.data()), NIB1_DESC_BYTES);
        fp.write(manifest.data(), static_cast<std::streamsize>(manifest.size()));
        uint64_t cur = jo + manifest.size();
        static const uint8_t zeros[64] = {0};
        for (size_t idx = 0; idx < names.size(); ++idx) {
            if (offs[idx] > cur) {
                fp.write(reinterpret_cast<const char*>(zeros), static_cast<std::streamsize>(offs[idx] - cur));
                cur = offs[idx];
            }
            const auto& t = b.fields.at(names[idx]);
            if (!t.data.empty()) {
                fp.write(reinterpret_cast<const char*>(t.data.data()), static_cast<std::streamsize>(t.data.size()));
                cur += t.data.size();
            }
        }
        fp.flush();
        require_bus(static_cast<bool>(fp), "io", "flush failed writing NIB file");
    }
    std::error_code ec;
    std::filesystem::rename(heraw_fs::from_utf8(tmp_path), heraw_fs::from_utf8(path), ec);
    if (ec) {
        std::filesystem::remove(heraw_fs::from_utf8(path), ec);
        std::filesystem::rename(heraw_fs::from_utf8(tmp_path), heraw_fs::from_utf8(path), ec);
        require_bus(!ec, "io", "rename failed publishing NIB file");
    }
}

inline NibTensor make_image_f32_tensor(
    const std::string& name, const float* vals, uint32_t H, uint32_t W, uint32_t C,
    uint16_t origin, const std::string& chart_id, const ChartSpec& chart,
    const std::string& semantic, const std::string& unit, int32_t frame = -1,
    const std::map<std::string, detail::JVal>& extra_meta = {}
) {
    using detail::JVal;
    NibTensor t;
    t.public_name = name;
    t.wire_key = detail::wire_key_for_name(name);
    t.dtype = NIB1_F32;
    t.kind = NIB1_SCALAR;
    t.origin = origin;
    t.frame = frame;
    t.shape = {1u, H, W, C};
    for (size_t i = 0; i < 6; ++i) t.affine[i] = static_cast<float>(chart.affine[i]);
    t.bbox = chart.int_bbox();
    const size_t n = static_cast<size_t>(H) * W * C;
    t.data.resize(n * 4u);
    for (size_t i = 0; i < n; ++i) detail::write_lef32(&t.data[i * 4u], vals[i]);
    std::map<std::string, JVal> m = extra_meta;
    m["semantic"] = JVal::make_str(semantic);
    m["unit"] = JVal::make_str(unit);
    m["axes"] = JVal::make_arr({JVal::make_str("batch"), JVal::make_str("y"),
                                JVal::make_str("x"), JVal::make_str("channel")});
    m["chart"] = JVal::make_str(chart_id);
    std::vector<JVal> bf;
    for (double x : chart.bbox) bf.push_back(JVal::make_real(x));
    m["valid_bbox_f"] = JVal::make_arr(std::move(bf));
    t.meta = JVal::make_obj(std::move(m));
    return t;
}

inline NibTensor make_image_u8_tensor(
    const std::string& name, const uint8_t* vals, uint32_t H, uint32_t W, uint32_t C,
    uint16_t origin, const std::string& chart_id, const ChartSpec& chart,
    const std::string& semantic, const std::string& unit
) {
    using detail::JVal;
    NibTensor t;
    t.public_name = name;
    t.wire_key = detail::wire_key_for_name(name);
    t.dtype = NIB1_U8;
    t.kind = NIB1_SCALAR;
    t.origin = origin;
    t.shape = {1u, H, W, C};
    for (size_t i = 0; i < 6; ++i) t.affine[i] = static_cast<float>(chart.affine[i]);
    t.bbox = chart.int_bbox();
    const size_t n = static_cast<size_t>(H) * W * C;
    t.data.assign(vals, vals + n);
    std::vector<JVal> bf;
    for (double x : chart.bbox) bf.push_back(JVal::make_real(x));
    t.meta = JVal::make_obj({
        {"semantic", JVal::make_str(semantic)},
        {"unit", JVal::make_str(unit)},
        {"axes", JVal::make_arr({JVal::make_str("batch"), JVal::make_str("y"),
                                 JVal::make_str("x"), JVal::make_str("channel")})},
        {"chart", JVal::make_str(chart_id)},
        {"valid_bbox_f", JVal::make_arr(std::move(bf))}
    });
    return t;
}

inline void attach_external_into_host(NibBundle& host, const NibBundle& ext, bool replace = true, bool trusted_host = false) {
    host.session.verify_matches(ext.session);
    int earliest = -1;
    for (const auto& name : ext.field_order) {
        const auto& t = ext.fields.at(name);
        require_bus(trusted_host || t.origin != NIB1_ORIG_NIJIN_MEASURED, "origin",
                    "external attach cannot inject origin=0 field: " + name);
        require_bus(replace || host.fields.find(name) == host.fields.end(), "exists",
                    "field already exists: " + name);
    }
    for (const auto& kv : ext.charts) {
        const bool changed = (host.charts.find(kv.first) != host.charts.end() &&
                              detail::dump_json_canonical(host.charts[kv.first].to_jval()) !=
                              detail::dump_json_canonical(kv.second.to_jval()));
        host.charts[kv.first] = kv.second;
        if (changed) {
            for (const auto& fk : host.fields) {
                if (fk.second.meta.at("chart").as_str() == kv.first) {
                    const uint32_t st = detail::parse_stage_code(
                        fk.second.meta.at("target_stage").as_str(detail::stage_name(host.stage)));
                    earliest = (earliest < 0) ? static_cast<int>(st) : std::min(earliest, static_cast<int>(st));
                }
            }
        }
    }
    for (const auto& name : ext.field_order) {
        const auto& t = ext.fields.at(name);
        const uint32_t st = detail::parse_stage_code(
            t.meta.at("target_stage").as_str(detail::stage_name(ext.stage)));
        if (st > 0) earliest = (earliest < 0) ? static_cast<int>(st) : std::min(earliest, static_cast<int>(st));
        host.put_tensor(t, true);
    }
    if (ext.policy.type == detail::JVal::OBJ) {
        if (host.policy.type != detail::JVal::OBJ) host.policy = detail::JVal::make_obj();
        for (const auto& kv : ext.policy.obj) {
            uint32_t st = 4;
            if (kv.first == "align") st = 2;
            else if (kv.first == "fuse") st = 3;
            earliest = (earliest < 0) ? static_cast<int>(st) : std::min(earliest, static_cast<int>(st));
            if (kv.second.type == detail::JVal::OBJ && host.policy.obj[kv.first].type == detail::JVal::OBJ) {
                for (const auto& sk : kv.second.obj) host.policy.obj[kv.first].obj[sk.first] = sk.second;
            } else {
                host.policy.obj[kv.first] = kv.second;
            }
        }
    }
    if (earliest > 0) host.mark_dirty(static_cast<uint32_t>(earliest));
}

// --- Native Operator Execution on NibBundle ---
namespace ops {

inline NibBundle run_operator(const std::string& op, const NibBundle& sess,
                              const detail::JVal& inputs, const detail::JVal& params) {
    using detail::JVal;
    NibBundle out;
    out.init_host(sess.session, sess.stage);
    out.charts = sess.charts;
    out.session_meta = sess.session_meta;

    auto get_in = [&](const char* port) -> const NibTensor& {
        const std::string fname = inputs.at(port).as_str();
        require_bus(!fname.empty() && sess.fields.find(fname) != sess.fields.end(), "ports",
                    std::string("missing bound input port: ") + port);
        return sess.fields.at(fname);
    };

    if (op == "nijin.sensor.gaussian_nll_grad@1") {
        const auto& mu_t = get_in("mu");
        const auto& y_t  = get_in("y");
        const auto& v_t  = get_in("variance");
        const std::string u = mu_t.meta.at("unit").as_str();
        require_bus(y_t.meta.at("unit").as_str() == u && v_t.meta.at("unit").as_str() == u + "^2",
                    "unit", "unit mismatch in gaussian_nll_grad");
        const auto mu = mu_t.unpack_doubles(), y = y_t.unpack_doubles(), var = v_t.unpack_doubles();
        require_bus(mu.size() == y.size() && mu.size() == var.size(), "shape", "port size mismatch");
        NibTensor grad = mu_t;
        grad.public_name = "grad";
        grad.wire_key = "grad";
        grad.dtype = NIB1_F64;
        grad.origin = NIB1_ORIG_NIJIN_MEASURED;
        grad.meta.obj["semantic"] = JVal::make_str("sensor.mean_gradient@1");
        grad.meta.obj["unit"] = JVal::make_str("1/" + u);
        grad.data.resize(mu.size() * 8u);
        double loss = 0.0;
        const double two_pi = 2.0 * std::numbers::pi;
        for (size_t i = 0; i < mu.size(); ++i) {
            require_bus(var[i] > 0.0, "domain", "variance must be positive");
            const double r = mu[i] - y[i];
            loss += 0.5 * (r * r / var[i] + std::log(two_pi * var[i]));
            uint64_t bits = 0;
            const double g = r / var[i];
            std::memcpy(&bits, &g, 8);
            detail::write_le64(&grad.data[i * 8u], bits);
        }
        out.put_tensor(std::move(grad));
        NibTensor nll;
        nll.public_name = "nll"; nll.wire_key = "nll";
        nll.dtype = NIB1_F64; nll.kind = NIB1_ND; nll.origin = NIB1_ORIG_NIJIN_MEASURED;
        nll.shape = {0, 0, 0, 0}; nll.bbox = {0, 0, 0, 0};
        nll.meta = JVal::make_obj({{"semantic", JVal::make_str("math.nll@1")},
                                   {"unit", JVal::make_str("1")},
                                   {"dims", JVal::make_arr()}, {"axes", JVal::make_arr()}});
        nll.data.resize(8u);
        uint64_t lbits = 0; std::memcpy(&lbits, &loss, 8);
        detail::write_le64(nll.data.data(), lbits);
        out.put_tensor(std::move(nll));
        return out;
    }

    if (op == "nijin.sensor.diagonal_gls_prox@1") {
        const auto& y_t = get_in("y");
        const auto& z_t = get_in("z");
        const auto& w_t = get_in("precision");
        const auto& p_t = get_in("penalty");
        const auto y = y_t.unpack_doubles(), z = z_t.unpack_doubles();
        const auto w = w_t.unpack_doubles(), pen = p_t.unpack_doubles();
        require_bus(y.size() == z.size() && y.size() == w.size() && y.size() == pen.size(), "shape", "size mismatch");
        NibTensor xt = y_t;
        xt.public_name = "x"; xt.wire_key = "x"; xt.dtype = NIB1_F64;
        xt.meta.obj["semantic"] = JVal::make_str("sensor.estimate@1");
        xt.data.resize(y.size() * 8u);
        NibTensor pv = y_t;
        pv.public_name = "provenance"; pv.wire_key = "provenance"; pv.dtype = NIB1_U8;
        pv.origin = NIB1_ORIG_NIJIN_MEASURED;
        pv.meta.obj["semantic"] = JVal::make_str("provenance.bitmask@1");
        pv.meta.obj["unit"] = JVal::make_str("bitmask");
        pv.data.resize(y.size());
        const uint8_t pflag = (z_t.origin == NIB1_ORIG_GENERATIVE_PRIOR) ? NIB1_PROV_GENERATIVE : NIB1_PROV_CALIBRATED;
        bool any_gen = false;
        for (size_t i = 0; i < y.size(); ++i) {
            const double sc = std::max(w[i], pen[i]);
            require_bus(std::min(w[i], pen[i]) >= 0.0 && sc > 0.0, "underdetermined", "zero total precision");
            const double a = w[i] / sc, b = pen[i] / sc;
            const double est = (a / (a + b)) * y[i] + (b / (a + b)) * z[i];
            uint64_t bits = 0; std::memcpy(&bits, &est, 8);
            detail::write_le64(&xt.data[i * 8u], bits);
            const uint8_t mask = static_cast<uint8_t>(
                (w[i] > 0.0 ? static_cast<uint8_t>(NIB1_PROV_MEASURED) : 0u) | (pen[i] > 0.0 ? pflag : 0u));
            pv.data[i] = mask;
            if (mask & NIB1_PROV_GENERATIVE) any_gen = true;
        }
        xt.origin = any_gen ? NIB1_ORIG_GENERATIVE_PRIOR : NIB1_ORIG_NIJIN_MEASURED;
        out.put_tensor(std::move(xt));
        out.put_tensor(std::move(pv));
        return out;
    }

    if (op == "nijin.geometry.map_points@1") {
        const auto& pt = get_in("points");
        const auto dims = pt.logical_dims();
        require_bus(pt.kind == NIB1_ND && !dims.empty() && dims.back() == 2 &&
                    pt.meta.at("unit").as_str() == "external_px", "grid", "expected ND (...,2) external_px points");
        std::array<double, 6> af = sess.chart_for_tensor(pt).affine;
        if (params.has("affine") && params.at("affine").arr.size() == 6) {
            for (size_t i = 0; i < 6; ++i) af[i] = params.at("affine").arr[i].as_num();
        }
        const auto vals = pt.unpack_doubles();
        NibTensor mp = pt;
        mp.public_name = "points"; mp.wire_key = "points"; mp.dtype = NIB1_F64;
        mp.origin = NIB1_ORIG_NIJIN_MEASURED;
        mp.affine = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
        mp.bbox = {0, 0, sess.session.active_w, sess.session.active_h};
        mp.meta.obj["chart"] = JVal::make_str("active_1x");
        mp.meta.obj["semantic"] = JVal::make_str("geometry.active_xy@1");
        mp.meta.obj["unit"] = JVal::make_str("active_px");
        mp.data.resize(vals.size() * 8u);
        for (size_t i = 0; i + 1 < vals.size(); i += 2) {
            const double x = af[0] * vals[i] + af[1] * vals[i + 1] + af[2];
            const double y = af[3] * vals[i] + af[4] * vals[i + 1] + af[5];
            uint64_t bx = 0, by = 0;
            std::memcpy(&bx, &x, 8); std::memcpy(&by, &y, 8);
            detail::write_le64(&mp.data[i * 8u], bx);
            detail::write_le64(&mp.data[(i + 1) * 8u], by);
        }
        out.put_tensor(std::move(mp));
        return out;
    }

    if (op == "nijin.optics.composite_prior@1") {
        const auto& base = get_in("base_rgb");
        const auto& prior = get_in("prior_rgb");
        require_bus((base.origin == NIB1_ORIG_NIJIN_MEASURED ||
                     base.origin == NIB1_ORIG_CO_CALIBRATED ||
                     base.origin == NIB1_ORIG_GENERATIVE_PRIOR) &&
                    (prior.origin == NIB1_ORIG_GENERATIVE_PRIOR || prior.origin == NIB1_ORIG_CO_CALIBRATED),
                    "origin", "composite_prior requires valid base_rgb and external prior_rgb (1 or 4)");
        require_bus(base.shape[0] == 1 && base.shape[3] == 3 &&
                    prior.shape[0] == 1 && prior.shape[3] == 3, "shape", "RGB shape must be [1,H,W,3]");
        const int H = static_cast<int>(base.shape[1]), W = static_cast<int>(base.shape[2]);
        const int pH = static_cast<int>(prior.shape[1]), pW = static_cast<int>(prior.shape[2]);
        const size_t npix = static_cast<size_t>(H) * static_cast<size_t>(W);
        const ChartSpec dst_chart = sess.chart_for_tensor(base);
        const ChartSpec pr_chart = sess.chart_for_tensor(prior);
        std::vector<float> w_vals(npix, 1.0f);
        if (inputs.has("weight")) {
            const std::string wn = inputs.at("weight").as_str();
            w_vals = sess.sample_scalar_2d(wn, dst_chart, H, W, 0.0f);
            for (float& wv : w_vals) wv = std::clamp(wv, 0.0f, 1.0f);
        }
        if (inputs.has("protect_mask")) {
            const std::string mn = inputs.at("protect_mask").as_str();
            const auto m_vals = sess.sample_scalar_2d(mn, dst_chart, H, W, 0.0f);
            for (size_t i = 0; i < npix; ++i) if (m_vals[i] >= 0.5f) w_vals[i] = 0.0f;
        }
        const uint8_t default_base_prov = (base.origin == NIB1_ORIG_GENERATIVE_PRIOR)
            ? static_cast<uint8_t>(NIB1_PROV_GENERATIVE)
            : ((base.origin == NIB1_ORIG_CO_CALIBRATED)
                ? static_cast<uint8_t>(NIB1_PROV_CALIBRATED)
                : static_cast<uint8_t>(NIB1_PROV_MEASURED));
        std::vector<uint8_t> base_prov(npix, default_base_prov);
        if (inputs.has("base_provenance")) {
            const auto& bp_t = get_in("base_provenance");
            require_bus(bp_t.dtype == NIB1_U8 && bp_t.shape[0] == 1 &&
                        bp_t.shape[1] == static_cast<uint32_t>(H) &&
                        bp_t.shape[2] == static_cast<uint32_t>(W) && bp_t.shape[3] == 1 &&
                        bp_t.data.size() == npix, "shape", "base_provenance shape mismatch");
            base_prov = bp_t.data;
        }
        const auto bv = base.unpack_doubles(), pv = prior.unpack_doubles();
        const bool pr_discrete = (prior.dtype != NIB1_F16 && prior.dtype != NIB1_F32 && prior.dtype != NIB1_F64);
        auto sample_prior_rgb = [&](double u, double v, double out_c[3]) noexcept {
            if (pr_discrete) {
                const int ix = std::clamp(static_cast<int>(std::floor(u)), 0, pW - 1);
                const int iy = std::clamp(static_cast<int>(std::floor(v)), 0, pH - 1);
                const size_t pi = (static_cast<size_t>(iy) * pW + ix) * 3u;
                for (int c = 0; c < 3; ++c) out_c[c] = pv[pi + static_cast<size_t>(c)];
                return;
            }
            const double xc = std::clamp(u - 0.5, 0.0, static_cast<double>(pW - 1));
            const double yc = std::clamp(v - 0.5, 0.0, static_cast<double>(pH - 1));
            const int ix0 = static_cast<int>(std::floor(xc));
            const int iy0 = static_cast<int>(std::floor(yc));
            const int ix1 = std::min(pW - 1, ix0 + 1);
            const int iy1 = std::min(pH - 1, iy0 + 1);
            const double wx = xc - static_cast<double>(ix0);
            const double wy = yc - static_cast<double>(iy0);
            for (int c = 0; c < 3; ++c) {
                const double v00 = pv[(static_cast<size_t>(iy0) * pW + ix0) * 3u + static_cast<size_t>(c)];
                const double v01 = pv[(static_cast<size_t>(iy0) * pW + ix1) * 3u + static_cast<size_t>(c)];
                const double v10 = pv[(static_cast<size_t>(iy1) * pW + ix0) * 3u + static_cast<size_t>(c)];
                const double v11 = pv[(static_cast<size_t>(iy1) * pW + ix1) * 3u + static_cast<size_t>(c)];
                out_c[c] = (v00 * (1.0 - wx) + v01 * wx) * (1.0 - wy) +
                           (v10 * (1.0 - wx) + v11 * wx) * wy;
            }
        };
        std::vector<float> out_rgb(npix * 3u, 0.0f);
        std::vector<uint8_t> prov(npix, 0u);
        const uint8_t pflag = (prior.origin == NIB1_ORIG_GENERATIVE_PRIOR)
            ? static_cast<uint8_t>(NIB1_PROV_GENERATIVE) : static_cast<uint8_t>(NIB1_PROV_CALIBRATED);
        uint8_t union_prov = 0u;
        for (int row = 0; row < H; ++row) {
            for (int col = 0; col < W; ++col) {
                const size_t i = static_cast<size_t>(row) * static_cast<size_t>(W) + static_cast<size_t>(col);
                float a = w_vals[i];
                double pc[3] = {0.0, 0.0, 0.0};
                if (a > 0.0f) {
                    const auto [x_act, y_act] = dst_chart.map_to_active(col + 0.5, row + 0.5);
                    double u = 0.0, v = 0.0;
                    if (pr_chart.sample_at_active(x_act, y_act, u, v)) {
                        sample_prior_rgb(u, v, pc);
                    } else {
                        a = 0.0f;
                    }
                }
                for (size_t c = 0; c < 3; ++c) {
                    out_rgb[i * 3 + c] = static_cast<float>((1.0 - a) * bv[i * 3 + c] + a * pc[c]);
                }
                const uint8_t mask = static_cast<uint8_t>(
                    (a < 1.0f ? base_prov[i] : 0u) | (a > 0.0f ? pflag : 0u));
                prov[i] = mask;
                union_prov |= mask;
            }
        }
        const uint16_t out_orig = (union_prov & NIB1_PROV_GENERATIVE)
            ? NIB1_ORIG_GENERATIVE_PRIOR
            : ((union_prov & NIB1_PROV_CALIBRATED) ? NIB1_ORIG_CO_CALIBRATED : NIB1_ORIG_NIJIN_MEASURED);
        const std::string cid = base.meta.at("chart").as_str("active_1x");
        out.put_tensor(make_image_f32_tensor("rgb", out_rgb.data(), static_cast<uint32_t>(H), static_cast<uint32_t>(W), 3u,
            out_orig, cid, dst_chart,
            "radiance.linear_rgb@1", base.meta.at("unit").as_str("photon_linear")));
        out.put_tensor(make_image_u8_tensor("provenance", prov.data(), static_cast<uint32_t>(H), static_cast<uint32_t>(W), 1u,
            NIB1_ORIG_NIJIN_MEASURED, cid, dst_chart, "provenance.bitmask@1", "bitmask"));
        return out;
    }

    throw BusException("unsupported_operator", op);
}

inline detail::JVal host_describe_jval() {
    using detail::JVal;
    auto op_spec = [](std::map<std::string, JVal> req, std::map<std::string, JVal> opt,
                      std::map<std::string, JVal> outs) {
        return JVal::make_obj({
            {"inputs", JVal::make_obj(std::move(req))},
            {"optional_inputs", JVal::make_obj(std::move(opt))},
            {"outputs", JVal::make_obj(std::move(outs))},
            {"stages", JVal::make_arr({JVal::make_str("EXT_INPUT"), JVal::make_str("S1_CALIB"),
                JVal::make_str("S2_ALIGN"), JVal::make_str("S3_FUSE"),
                JVal::make_str("S4_OPTICS"), JVal::make_str("S5_DELIVER")})},
            {"parameters", JVal::make_obj()}
        });
    };
    return JVal::make_obj({
        {"api", JVal::make_str("nopb/1")},
        {"backend", JVal::make_str("nijinLBS-native")},
        {"transports", JVal::make_arr({JVal::make_str("nib-file"), JVal::make_str("mmap-slice"), JVal::make_str("live-session")})},
        {"stages", JVal::make_arr({JVal::make_str("EXT_INPUT"), JVal::make_str("S1_CALIB"),
            JVal::make_str("S2_ALIGN"), JVal::make_str("S3_FUSE"), JVal::make_str("S4_OPTICS"), JVal::make_str("S5_DELIVER")})},
        {"operators", JVal::make_obj({
            {"nijin.sensor.gaussian_nll_grad@1", op_spec(
                {{"mu", JVal::make_str("sensor.mean@1")}, {"y", JVal::make_str("sensor.reading@1")}, {"variance", JVal::make_str("sensor.variance@1")}},
                {}, {{"nll", JVal::make_str("math.nll@1")}, {"grad", JVal::make_str("sensor.mean_gradient@1")}})},
            {"nijin.sensor.diagonal_gls_prox@1", op_spec(
                {{"y", JVal::make_str("sensor.reading@1")}, {"z", JVal::make_str("sensor.candidate@1")},
                 {"precision", JVal::make_str("math.precision@1")}, {"penalty", JVal::make_str("math.precision@1")}},
                {}, {{"x", JVal::make_str("sensor.estimate@1")}, {"provenance", JVal::make_str("provenance.bitmask@1")}})},
            {"nijin.geometry.map_points@1", op_spec(
                {{"points", JVal::make_str("geometry.external_xy@1")}}, {}, {{"points", JVal::make_str("geometry.active_xy@1")}})},
            {"nijin.optics.composite_prior@1", op_spec(
                {{"base_rgb", JVal::make_str("radiance.*")}, {"prior_rgb", JVal::make_str("radiance.*")}},
                {{"weight", JVal::make_str("mask.*")}, {"protect_mask", JVal::make_str("mask.*")}, {"base_provenance", JVal::make_str("provenance.bitmask@1")}},
                {{"rgb", JVal::make_str("radiance.linear_rgb@1")}, {"provenance", JVal::make_str("provenance.bitmask@1")}})}
        })}
    });
}

} // namespace ops

// Callback implemented by heraw_sr2x.cpp to open a real RAW burst into a live NibBundle!
using OpenBurstCallback = std::function<NibBundle(const std::vector<std::string>& utf8_paths, int32_t anchor)>;

inline int run_bus_serve_loop(const OpenBurstCallback& open_burst_cb) {
    using detail::JVal;
    std::map<std::string, NibBundle> sessions;
    uint64_t next_sid = 1;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
        JVal id_val = JVal::make_null();
        JVal resp;
        try {
            const JVal req = detail::parse_json(line);
            require_bus(req.type == JVal::OBJ, "request", "expected JSON object");
            if (req.has("id")) id_val = req.at("id");
            require_bus(req.at("api").as_str() == "nopb/1", "version", "expected nopb/1");
            const std::string method = req.at("method").as_str();
            JVal result;
            if (method == "describe") {
                result = ops::host_describe_jval();
            } else if (method == "open_session") {
                std::vector<std::string> paths;
                for (const auto& f : req.at("frames").arr) paths.push_back(f.as_str());
                require_bus(!paths.empty(), "frames", "open_session requires non-empty frames list");
                const int32_t anchor = static_cast<int32_t>(req.at("anchor").as_int(0));
                NibBundle sess = open_burst_cb(paths, anchor);
                const std::string sid = "sess-" + std::to_string(next_sid++);
                result = sess.describe_jval();
                result.obj["session_handle"] = JVal::make_str(sid);
                sessions[sid] = std::move(sess);
            } else if (method == "session_describe") {
                const std::string sid = req.at("session_handle").as_str();
                require_bus(sessions.find(sid) != sessions.end(), "session", "unknown session_handle");
                result = sessions.at(sid).describe_jval();
            } else if (method == "attach") {
                const std::string sid = req.at("session_handle").as_str();
                require_bus(sessions.find(sid) != sessions.end(), "session", "unknown session_handle");
                auto& host_s = sessions.at(sid);
                const NibBundle ext = read_nib_file(req.at("input").as_str(), &host_s.session);
                const bool replace = !req.has("replace") || req.at("replace").b;
                // External wire attach can never claim _trusted_host; origin=0 is strictly host-owned.
                attach_external_into_host(host_s, ext, replace, false);
                result = host_s.describe_jval();
            } else if (method == "export") {
                const std::string sid = req.at("session_handle").as_str();
                require_bus(sessions.find(sid) != sessions.end(), "session", "unknown session_handle");
                const auto& host_s = sessions.at(sid);
                std::vector<std::string> filter;
                if (req.has("names") && req.at("names").type == JVal::ARR) {
                    for (const auto& n : req.at("names").arr) filter.push_back(n.as_str());
                }
                write_nib_file(req.at("output").as_str(), host_s, filter.empty() ? nullptr : &filter);
                result = host_s.describe_jval();
            } else if (method == "close_session") {
                const std::string sid = req.at("session_handle").as_str();
                require_bus(sessions.erase(sid) > 0, "session", "unknown session_handle");
                result = JVal::make_obj({{"closed", JVal::make_str(sid)}});
            } else if (method == "inspect") {
                if (req.has("session_handle")) {
                    const std::string sid = req.at("session_handle").as_str();
                    require_bus(sessions.find(sid) != sessions.end(), "session", "unknown session_handle");
                    result = sessions.at(sid).describe_jval();
                } else {
                    result = read_nib_file(req.at("input").as_str()).describe_jval();
                }
            } else if (method == "call") {
                NibBundle tmp_in;
                NibBundle* target = nullptr;
                if (req.has("session_handle")) {
                    const std::string sid = req.at("session_handle").as_str();
                    require_bus(sessions.find(sid) != sessions.end(), "session", "unknown session_handle");
                    target = &sessions.at(sid);
                } else {
                    tmp_in = read_nib_file(req.at("input").as_str());
                    target = &tmp_in;
                }
                NibBundle out = ops::run_operator(req.at("op").as_str(), *target, req.at("inputs"), req.at("params"));
                if (req.has("adopt_outputs") && req.at("adopt_outputs").type == JVal::OBJ) {
                    for (const auto& kv : req.at("adopt_outputs").obj) {
                        require_bus(out.fields.find(kv.first) != out.fields.end(), "output_ports", kv.first);
                        NibTensor adopted = out.fields.at(kv.first);
                        adopted.public_name = kv.second.as_str();
                        adopted.wire_key = detail::wire_key_for_name(adopted.public_name);
                        target->put_tensor(std::move(adopted), true);
                    }
                }
                if (req.has("output") && req.at("output").type == JVal::STR) {
                    write_nib_file(req.at("output").as_str(), out);
                }
                result = out.describe_jval();
            } else {
                throw BusException("method", "unknown method: " + method);
            }
            resp = JVal::make_obj({{"id", id_val}, {"ok", JVal::make_bool(true)}, {"result", std::move(result)}});
        } catch (const BusException& be) {
            resp = JVal::make_obj({
                {"id", id_val}, {"ok", JVal::make_bool(false)},
                {"error", JVal::make_obj({{"code", JVal::make_str(be.code)}, {"message", JVal::make_str(be.what())}})}
            });
        } catch (const std::exception& ex) {
            resp = JVal::make_obj({
                {"id", id_val}, {"ok", JVal::make_bool(false)},
                {"error", JVal::make_obj({{"code", JVal::make_str("internal")}, {"message", JVal::make_str(ex.what())}})}
            });
        }
        std::cout << detail::dump_json_canonical(resp) << "\n" << std::flush;
    }
    return 0;
}

} // namespace heraw_bus
