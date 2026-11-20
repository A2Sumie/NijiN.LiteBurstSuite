// heraw_suite_exr_stream.hpp -- Stage 3 Zero-Dependency OpenEXR Multi-Part Writer/Reader & Streaming Engine:
//   - F-E / R12: Native Multi-Part Tiled OpenEXR 2.0 (ZIP / NONE, signed IEEE HALF & FLOAT)
//   - F-A / Cross-Section #3: 2 GiB Stripe-Tiled Zero-Temp-Disk Two-Pass Sufficient Statistics & Telemetry
// Pure 7-bit ASCII C++20 header. Relies only on standard C++20 and already-linked zlib.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <zlib.h>

namespace heraw_exr {

enum class PixelType : int32_t {
    UINT = 0,
    HALF = 1,
    FLOAT = 2
};

enum class CompressionType : uint8_t {
    NONE = 0,
    ZIP = 3
};

// Signed IEEE-754 float32 -> binary16 conversion (preserves negative shadow noise & signed premultiplied colors).
inline uint16_t f32_to_f16_signed(float f) noexcept {
    const uint32_t u = std::bit_cast<uint32_t>(f);
    const uint16_t sign = static_cast<uint16_t>((u >> 16) & 0x8000u);
    if (!std::isfinite(f)) {
        return std::isnan(f) ? static_cast<uint16_t>(sign | 0x7E00u) : static_cast<uint16_t>(sign | 0x7C00u);
    }
    const uint32_t abs_u = u & 0x7FFFFFFFu;
    const float abs_f = std::bit_cast<float>(abs_u);
    if (abs_f >= 65504.0f) return static_cast<uint16_t>(sign | 0x7BFFu);
    int exp = static_cast<int>((abs_u >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = abs_u & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return sign;
        mant |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exp);
        const uint32_t half_m = (mant + (1u << (shift - 1)) - 1u + ((mant >> shift) & 1u)) >> shift;
        return static_cast<uint16_t>(sign | half_m);
    }
    const uint32_t round_m = mant + 0x00000FFFu + ((mant >> 13) & 1u);
    if (round_m >= 0x800000u) {
        ++exp;
        if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
        return static_cast<uint16_t>(sign | (exp << 10));
    }
    return static_cast<uint16_t>(sign | (exp << 10) | (round_m >> 13));
}

inline float f16_signed_to_f32(uint16_t h) noexcept {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x03FFu;
    uint32_t u = 0;
    if (exp == 0u) {
        if (mant == 0u) {
            u = sign;
        } else {
            uint32_t m = mant;
            int e = 127 - 14;
            while ((m & 0x0400u) == 0u) { m <<= 1; --e; }
            m &= 0x03FFu;
            u = sign | (static_cast<uint32_t>(e) << 23) | (m << 13);
        }
    } else if (exp == 31u) {
        u = sign | 0x7F800000u | (mant << 13);
    } else {
        const uint32_t e = exp + (127u - 15u);
        u = sign | (e << 23) | (mant << 13);
    }
    return std::bit_cast<float>(u);
}

struct ExrChannelSpec {
    std::string name; // OpenEXR requires chlist sorted alphabetically by ASCII byte order
    PixelType type = PixelType::HALF;
    bool linear = true;
    const float* data = nullptr; // row-major [height * stride_pixels + x], stride_components >= 1
    int comp_offset = 0;
    int comp_stride = 1;
};

struct ExrAttr {
    std::string name;
    std::string type;
    std::vector<uint8_t> payload;
};

struct ExrPart {
    std::string name;
    int width = 0;
    int height = 0;
    int tile_w = 64;
    int tile_h = 64;
    CompressionType compression = CompressionType::ZIP;
    std::vector<ExrChannelSpec> channels;
    std::vector<ExrAttr> extra_attrs;
    std::array<float, 8> chromaticities = {0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f};
    bool has_chromaticities = false;
};

struct DecodedExrChannel {
    std::string name;
    PixelType type = PixelType::HALF;
    std::vector<float> pixels;
};

struct DecodedExrPart {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<DecodedExrChannel> channels;
    const DecodedExrChannel* find_channel(const std::string& cname) const {
        for (const auto& ch : channels) if (ch.name == cname) return &ch;
        return nullptr;
    }
};

namespace detail {

inline FILE* open_wb_utf8(const std::string& path) {
#ifdef _WIN32
    int wn = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wn <= 0) return nullptr;
    std::wstring wp(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], wn);
    return _wfopen(wp.c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

inline FILE* open_rb_utf8(const std::string& path) {
#ifdef _WIN32
    int wn = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wn <= 0) return nullptr;
    std::wstring wp(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], wn);
    return _wfopen(wp.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

inline void push_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
inline void push_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
}
inline void push_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
}
inline void push_i32(std::vector<uint8_t>& out, int32_t v) {
    uint32_t u = 0; std::memcpy(&u, &v, 4); push_u32(out, u);
}
inline void push_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
}
inline void push_f32(std::vector<uint8_t>& out, float f) {
    uint32_t u = 0; std::memcpy(&u, &f, 4); push_u32(out, u);
}
inline void push_cstr(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}
inline void push_attr(std::vector<uint8_t>& out, const std::string& name,
                      const std::string& type, const std::vector<uint8_t>& val) {
    push_cstr(out, name);
    push_cstr(out, type);
    push_u32(out, static_cast<uint32_t>(val.size()));
    out.insert(out.end(), val.begin(), val.end());
}

inline uint32_t rd_u32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline int32_t rd_i32(const uint8_t* p) noexcept {
    uint32_t u = rd_u32(p);
    int32_t s = 0; std::memcpy(&s, &u, 4); return s;
}
inline uint64_t rd_u64(const uint8_t* p) noexcept {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= (uint64_t(p[i]) << (i * 8));
    return u;
}
inline float rd_f32(const uint8_t* p) noexcept {
    uint32_t u = rd_u32(p);
    float f = 0.0f; std::memcpy(&f, &u, 4); return f;
}

// IlmImf standard ZIP byte interleave + horizontal delta predictor
inline void exr_zip_reorder_and_predict(const std::vector<uint8_t>& raw, std::vector<uint8_t>& tmp) {
    const size_t n = raw.size();
    tmp.resize(n);
    const size_t half = (n + 1) / 2;
    size_t i1 = 0, i2 = half;
    for (size_t i = 0; i < n; ++i) {
        if ((i & 1u) == 0u) tmp[i1++] = raw[i];
        else tmp[i2++] = raw[i];
    }
    int prev = tmp.empty() ? 0 : static_cast<int>(tmp[0]);
    for (size_t i = 1; i < n; ++i) {
        const int cur = static_cast<int>(tmp[i]);
        const int d = cur - prev + 128;
        prev = cur;
        tmp[i] = static_cast<uint8_t>(d & 0xFF);
    }
}

inline void exr_zip_unpredict_and_reconstruct(std::vector<uint8_t>& buf, std::vector<uint8_t>& raw_out) {
    const size_t n = buf.size();
    raw_out.resize(n);
    if (n == 0) return;
    int p = static_cast<int>(buf[0]);
    for (size_t i = 1; i < n; ++i) {
        p = (p + static_cast<int>(buf[i]) - 128) & 0xFF;
        buf[i] = static_cast<uint8_t>(p);
    }
    const size_t half = (n + 1) / 2;
    size_t i1 = 0, i2 = half;
    for (size_t i = 0; i < n; ++i) {
        raw_out[i] = ((i & 1u) == 0u) ? buf[i1++] : buf[i2++];
    }
}

} // namespace detail

// Write standard Multi-Part Tiled OpenEXR 2.0 container (compatible with Nuke, Fusion, DaVinci, OpenEXR SDK).
inline bool write_multipart_exr(
    const std::string& path,
    const std::vector<ExrPart>& parts_in,
    std::string* err_out = nullptr
) {
    if (parts_in.empty()) {
        if (err_out) *err_out = "Empty EXR part list";
        return false;
    }

    // Clone and canonicalize parts (OpenEXR mandates alphabetical chlist sorting)
    std::vector<ExrPart> parts = parts_in;
    for (auto& pt : parts) {
        if (pt.width <= 0 || pt.height <= 0 || pt.channels.empty()) {
            if (err_out) *err_out = "Invalid EXR part dimensions or channels";
            return false;
        }
        pt.tile_w = std::max(8, pt.tile_w);
        pt.tile_h = std::max(8, pt.tile_h);
        std::sort(pt.channels.begin(), pt.channels.end(), [](const ExrChannelSpec& a, const ExrChannelSpec& b) {
            return a.name < b.name;
        });
    }

    std::vector<uint8_t> file_hdr;
    detail::push_u32(file_hdr, 0x01312F76u); // OpenEXR magic
    // Version 2 with MULTI_PART_FILE (0x1000) flag set (tile mode specified per-part via type="tiledimage")
    detail::push_u32(file_hdr, 0x00001002u);

    std::vector<int> part_ntx(parts.size()), part_nty(parts.size()), part_chunks(parts.size());
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        const auto& pt = parts[pi];
        const int ntx = (pt.width + pt.tile_w - 1) / pt.tile_w;
        const int nty = (pt.height + pt.tile_h - 1) / pt.tile_h;
        part_ntx[pi] = ntx;
        part_nty[pi] = nty;
        part_chunks[pi] = ntx * nty;

        // channels
        std::vector<uint8_t> chbuf;
        for (const auto& ch : pt.channels) {
            detail::push_cstr(chbuf, ch.name);
            detail::push_i32(chbuf, static_cast<int32_t>(ch.type));
            detail::push_u8(chbuf, ch.linear ? 1u : 0u);
            detail::push_u8(chbuf, 0u); detail::push_u8(chbuf, 0u); detail::push_u8(chbuf, 0u);
            detail::push_i32(chbuf, 1);
            detail::push_i32(chbuf, 1);
        }
        detail::push_u8(chbuf, 0u);
        detail::push_attr(file_hdr, "channels", "chlist", chbuf);

        detail::push_attr(file_hdr, "chunkCount", "int", [&]() {
            std::vector<uint8_t> b; detail::push_i32(b, part_chunks[pi]); return b;
        }());
        detail::push_attr(file_hdr, "compression", "compression", {static_cast<uint8_t>(pt.compression)});
        auto make_box2i = [&](int x0, int y0, int x1, int y1) {
            std::vector<uint8_t> b;
            detail::push_i32(b, x0); detail::push_i32(b, y0);
            detail::push_i32(b, x1); detail::push_i32(b, y1);
            return b;
        };
        detail::push_attr(file_hdr, "dataWindow", "box2i", make_box2i(0, 0, pt.width - 1, pt.height - 1));
        detail::push_attr(file_hdr, "displayWindow", "box2i", make_box2i(0, 0, pt.width - 1, pt.height - 1));
        detail::push_attr(file_hdr, "lineOrder", "lineOrder", {0u}); // INCREASING_Y
        detail::push_attr(file_hdr, "name", "string",
                          std::vector<uint8_t>(pt.name.begin(), pt.name.end()));
        detail::push_attr(file_hdr, "pixelAspectRatio", "float", [&]() {
            std::vector<uint8_t> b; detail::push_f32(b, 1.0f); return b;
        }());
        detail::push_attr(file_hdr, "screenWindowCenter", "v2f", [&]() {
            std::vector<uint8_t> b; detail::push_f32(b, 0.0f); detail::push_f32(b, 0.0f); return b;
        }());
        detail::push_attr(file_hdr, "screenWindowWidth", "float", [&]() {
            std::vector<uint8_t> b; detail::push_f32(b, 1.0f); return b;
        }());
        detail::push_attr(file_hdr, "tiles", "tiledesc", [&]() {
            std::vector<uint8_t> b;
            detail::push_u32(b, static_cast<uint32_t>(pt.tile_w));
            detail::push_u32(b, static_cast<uint32_t>(pt.tile_h));
            detail::push_u8(b, 0u); // ONE_LEVEL | ROUND_DOWN
            return b;
        }());
        {
            static const char kTiledType[] = "tiledimage";
            detail::push_attr(file_hdr, "type", "string",
                              std::vector<uint8_t>(kTiledType, kTiledType + sizeof(kTiledType) - 1));
        }
        detail::push_attr(file_hdr, "version", "int", [&]() {
            std::vector<uint8_t> b; detail::push_i32(b, 1); return b;
        }());
        if (pt.has_chromaticities) {
            std::vector<uint8_t> cb;
            for (float v : pt.chromaticities) detail::push_f32(cb, v);
            detail::push_attr(file_hdr, "chromaticities", "chromaticities", cb);
        }
        for (const auto& ea : pt.extra_attrs) {
            detail::push_attr(file_hdr, ea.name, ea.type, ea.payload);
        }
        detail::push_u8(file_hdr, 0u); // End of part header
    }
    detail::push_u8(file_hdr, 0u); // End of multi-part headers list

    const uint64_t tbl_start = file_hdr.size();
    size_t total_chunks = 0;
    for (int cnt : part_chunks) total_chunks += static_cast<size_t>(cnt);
    std::vector<uint64_t> chunk_offs(total_chunks, 0ULL);

    FILE* fp = detail::open_wb_utf8(path);
    if (!fp) {
        if (err_out) *err_out = "Failed opening output EXR path";
        return false;
    }
    if (std::fwrite(file_hdr.data(), 1, file_hdr.size(), fp) != file_hdr.size()) {
        std::fclose(fp);
        return false;
    }
    // Reserve chunk offset tables
    std::vector<uint8_t> zero_tbl(total_chunks * 8u, 0u);
    if (std::fwrite(zero_tbl.data(), 1, zero_tbl.size(), fp) != zero_tbl.size()) {
        std::fclose(fp);
        return false;
    }

    uint64_t cursor = tbl_start + zero_tbl.size();
    size_t global_chunk_idx = 0;
    std::vector<uint8_t> raw_tile, pred_buf, comp_buf;
    bool write_ok = true;

    for (size_t pi = 0; pi < parts.size() && write_ok; ++pi) {
        const auto& pt = parts[pi];
        const int ntx = part_ntx[pi], nty = part_nty[pi];
        for (int ty = 0; ty < nty && write_ok; ++ty) {
            const int y0 = ty * pt.tile_h;
            const int th = std::min(pt.tile_h, pt.height - y0);
            for (int tx = 0; tx < ntx && write_ok; ++tx) {
                const int x0 = tx * pt.tile_w;
                const int tw = std::min(pt.tile_w, pt.width - x0);

                size_t row_bytes = 0;
                for (const auto& ch : pt.channels) {
                    row_bytes += static_cast<size_t>(tw) * (ch.type == PixelType::HALF ? 2u : 4u);
                }
                raw_tile.resize(row_bytes * static_cast<size_t>(th));
                size_t dst_pos = 0;
                for (int r = 0; r < th; ++r) {
                    const int sr = y0 + r;
                    for (const auto& ch : pt.channels) {
                        for (int c = 0; c < tw; ++c) {
                            const int sc = x0 + c;
                            const float val = ch.data
                                ? ch.data[(static_cast<size_t>(sr) * pt.width + sc) * ch.comp_stride + ch.comp_offset]
                                : 0.0f;
                            if (ch.type == PixelType::HALF) {
                                const uint16_t h16 = f32_to_f16_signed(val);
                                raw_tile[dst_pos++] = static_cast<uint8_t>(h16 & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((h16 >> 8) & 0xFFu);
                            } else if (ch.type == PixelType::UINT) {
                                const uint32_t u = (val > 0.0f && std::isfinite(val))
                                    ? static_cast<uint32_t>(std::llround(std::min(static_cast<double>(val), 4294967040.0)))
                                    : 0u;
                                raw_tile[dst_pos++] = static_cast<uint8_t>(u & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((u >> 8) & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((u >> 16) & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((u >> 24) & 0xFFu);
                            } else {
                                uint32_t u = 0;
                                std::memcpy(&u, &val, 4);
                                raw_tile[dst_pos++] = static_cast<uint8_t>(u & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((u >> 8) & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((u >> 16) & 0xFFu);
                                raw_tile[dst_pos++] = static_cast<uint8_t>((u >> 24) & 0xFFu);
                            }
                        }
                    }
                }

                const uint8_t* payload_ptr = raw_tile.data();
                uint32_t payload_sz = static_cast<uint32_t>(raw_tile.size());
                if (pt.compression == CompressionType::ZIP && !raw_tile.empty()) {
                    detail::exr_zip_reorder_and_predict(raw_tile, pred_buf);
                    uLong bound = compressBound(static_cast<uLong>(pred_buf.size()));
                    comp_buf.resize(static_cast<size_t>(bound));
                    if (compress2(comp_buf.data(), &bound, pred_buf.data(),
                                  static_cast<uLong>(pred_buf.size()), 6) == Z_OK &&
                        bound < raw_tile.size()) {
                        payload_ptr = comp_buf.data();
                        payload_sz = static_cast<uint32_t>(bound);
                    }
                }

                chunk_offs[global_chunk_idx++] = cursor;
                uint8_t chdr[24];
                // Multi-part chunk header: partNumber(i32) + tileX(i32) + tileY(i32) + levelX(i32) + levelY(i32) + dataSize(u32)
                std::vector<uint8_t> htmp;
                htmp.reserve(24);
                detail::push_i32(htmp, static_cast<int32_t>(pi));
                detail::push_i32(htmp, tx);
                detail::push_i32(htmp, ty);
                detail::push_i32(htmp, 0);
                detail::push_i32(htmp, 0);
                detail::push_u32(htmp, payload_sz);
                std::memcpy(chdr, htmp.data(), 24);
                if (std::fwrite(chdr, 1, 24, fp) != 24 ||
                    std::fwrite(payload_ptr, 1, payload_sz, fp) != payload_sz) {
                    write_ok = false;
                    break;
                }
                cursor += 24ULL + static_cast<uint64_t>(payload_sz);
            }
        }
    }

    if (write_ok) {
        std::vector<uint8_t> tbl_bytes;
        tbl_bytes.reserve(total_chunks * 8u);
        for (uint64_t off : chunk_offs) detail::push_u64(tbl_bytes, off);
#ifdef _WIN32
        write_ok = (_fseeki64(fp, static_cast<__int64>(tbl_start), SEEK_SET) == 0);
#else
        write_ok = (fseeko(fp, static_cast<off_t>(tbl_start), SEEK_SET) == 0);
#endif
        if (write_ok) {
            write_ok = (std::fwrite(tbl_bytes.data(), 1, tbl_bytes.size(), fp) == tbl_bytes.size());
        }
    }
    if (std::fclose(fp) != 0) write_ok = false;
    return write_ok;
}

// Self-contained Multi-Part Tiled OpenEXR 2.0 Reader (verifies R12 round-trip and negative HALF values).
inline bool read_multipart_exr(
    const std::string& path,
    std::vector<DecodedExrPart>& parts_out,
    std::string* err_out = nullptr
) {
    parts_out.clear();
    FILE* fp = detail::open_rb_utf8(path);
    if (!fp) { if (err_out) *err_out = "Cannot open EXR"; return false; }
#ifdef _WIN32
    _fseeki64(fp, 0, SEEK_END);
    uint64_t fsz = static_cast<uint64_t>(_ftelli64(fp));
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_END);
    uint64_t fsz = static_cast<uint64_t>(ftello(fp));
    fseeko(fp, 0, SEEK_SET);
#endif
    if (fsz < 16 || fsz > 1024ULL * 1024ULL * 1024ULL) {
        std::fclose(fp);
        return false;
    }
    std::vector<uint8_t> buf(static_cast<size_t>(fsz));
    if (std::fread(buf.data(), 1, buf.size(), fp) != buf.size()) {
        std::fclose(fp);
        return false;
    }
    std::fclose(fp);
    if (detail::rd_u32(buf.data()) != 0x01312F76u) return false;
    const uint32_t ver = detail::rd_u32(buf.data() + 4);
    const bool is_multipart = (ver & 0x1000u) != 0u;
    size_t pos = 8;

    struct PartMeta {
        std::string name;
        int w = 0, h = 0, tw = 64, th = 64;
        uint8_t comp = 0;
        int chunk_count = 0;
        std::vector<std::pair<std::string, PixelType>> chs;
    };
    std::vector<PartMeta> pmetas;

    auto read_cstr = [&](std::string& s) -> bool {
        size_t start = pos;
        while (pos < buf.size() && buf[pos] != 0) ++pos;
        if (pos >= buf.size()) return false;
        s.assign(reinterpret_cast<const char*>(buf.data() + start), pos - start);
        ++pos;
        return true;
    };

    while (pos < buf.size()) {
        if (is_multipart && buf[pos] == 0) { ++pos; break; }
        PartMeta pm;
        bool has_any = false;
        while (pos < buf.size() && buf[pos] != 0) {
            has_any = true;
            std::string aname, atype;
            if (!read_cstr(aname) || !read_cstr(atype) || pos + 4 > buf.size()) return false;
            const uint32_t asize = detail::rd_u32(buf.data() + pos);
            pos += 4;
            if (pos + asize > buf.size()) return false;
            const uint8_t* p = buf.data() + pos;
            if (aname == "name" && atype == "string") {
                pm.name.assign(reinterpret_cast<const char*>(p), asize);
            } else if (aname == "compression" && asize >= 1) {
                pm.comp = p[0];
            } else if (aname == "chunkCount" && asize == 4) {
                pm.chunk_count = detail::rd_i32(p);
            } else if (aname == "dataWindow" && asize == 16) {
                const int x0 = detail::rd_i32(p), y0 = detail::rd_i32(p + 4);
                const int x1 = detail::rd_i32(p + 8), y1 = detail::rd_i32(p + 12);
                pm.w = x1 - x0 + 1;
                pm.h = y1 - y0 + 1;
            } else if (aname == "tiles" && asize >= 9) {
                pm.tw = static_cast<int>(detail::rd_u32(p));
                pm.th = static_cast<int>(detail::rd_u32(p + 4));
            } else if (aname == "channels" && atype == "chlist") {
                size_t cp = 0;
                while (cp < asize && p[cp] != 0) {
                    size_t n0 = cp;
                    while (cp < asize && p[cp] != 0) ++cp;
                    if (cp + 17 > asize) return false;
                    std::string cn(reinterpret_cast<const char*>(p + n0), cp - n0);
                    ++cp;
                    PixelType pt = static_cast<PixelType>(detail::rd_i32(p + cp));
                    cp += 16;
                    pm.chs.emplace_back(cn, pt);
                }
            }
            pos += asize;
        }
        if (pos < buf.size() && buf[pos] == 0) ++pos;
        if (!has_any) break;
        pmetas.push_back(std::move(pm));
        if (!is_multipart) break;
    }

    if (pmetas.empty()) return false;
    parts_out.resize(pmetas.size());
    std::vector<uint64_t> all_chunk_offs;
    size_t total_chunks = 0;
    for (size_t i = 0; i < pmetas.size(); ++i) {
        const auto& pm = pmetas[i];
        auto& dp = parts_out[i];
        dp.name = pm.name;
        dp.width = pm.w;
        dp.height = pm.h;
        dp.channels.resize(pm.chs.size());
        for (size_t c = 0; c < pm.chs.size(); ++c) {
            dp.channels[c].name = pm.chs[c].first;
            dp.channels[c].type = pm.chs[c].second;
            dp.channels[c].pixels.assign(static_cast<size_t>(pm.w) * pm.h, 0.0f);
        }
        const int ntx = (pm.w + std::max(1, pm.tw) - 1) / std::max(1, pm.tw);
        const int nty = (pm.h + std::max(1, pm.th) - 1) / std::max(1, pm.th);
        const int cnt = pm.chunk_count > 0 ? pm.chunk_count : (ntx * nty);
        total_chunks += static_cast<size_t>(cnt);
    }
    if (pos + total_chunks * 8 > buf.size()) return false;
    all_chunk_offs.resize(total_chunks);
    for (size_t i = 0; i < total_chunks; ++i) {
        all_chunk_offs[i] = detail::rd_u64(buf.data() + pos + i * 8);
    }

    std::vector<uint8_t> pred_buf, raw_tile;
    for (size_t ci = 0; ci < total_chunks; ++ci) {
        const size_t coff = static_cast<size_t>(all_chunk_offs[ci]);
        if (coff + (is_multipart ? 24 : 20) > buf.size()) return false;
        size_t hp = coff;
        int part_idx = 0;
        if (is_multipart) {
            part_idx = detail::rd_i32(buf.data() + hp);
            hp += 4;
        }
        if (part_idx < 0 || part_idx >= static_cast<int>(pmetas.size())) return false;
        const int tx = detail::rd_i32(buf.data() + hp + 0);
        const int ty = detail::rd_i32(buf.data() + hp + 4);
        const uint32_t data_sz = detail::rd_u32(buf.data() + hp + 16);
        hp += 20;
        if (hp + data_sz > buf.size()) return false;

        const auto& pm = pmetas[static_cast<size_t>(part_idx)];
        auto& dp = parts_out[static_cast<size_t>(part_idx)];
        const int x0 = tx * pm.tw, y0 = ty * pm.th;
        const int tw = std::min(pm.tw, pm.w - x0), th = std::min(pm.th, pm.h - y0);
        if (tw <= 0 || th <= 0) continue;

        size_t row_bytes = 0;
        for (const auto& c : pm.chs) row_bytes += static_cast<size_t>(tw) * (c.second == PixelType::HALF ? 2u : 4u);
        const size_t expected_raw = row_bytes * static_cast<size_t>(th);
        const uint8_t* raw_ptr = buf.data() + hp;

        if (pm.comp == 3 && data_sz < expected_raw) {
            pred_buf.resize(expected_raw);
            uLongf dst_len = static_cast<uLongf>(expected_raw);
            if (uncompress(pred_buf.data(), &dst_len, buf.data() + hp, static_cast<uLong>(data_sz)) != Z_OK ||
                dst_len != expected_raw) {
                return false;
            }
            detail::exr_zip_unpredict_and_reconstruct(pred_buf, raw_tile);
            raw_ptr = raw_tile.data();
        } else if (data_sz != expected_raw) {
            return false;
        }

        size_t rp = 0;
        for (int r = 0; r < th; ++r) {
            const int sr = y0 + r;
            for (size_t cidx = 0; cidx < pm.chs.size(); ++cidx) {
                const PixelType pt = pm.chs[cidx].second;
                auto& dst_vec = dp.channels[cidx].pixels;
                for (int c = 0; c < tw; ++c) {
                    const int sc = x0 + c;
                    float val = 0.0f;
                    if (pt == PixelType::HALF) {
                        const uint16_t h16 = static_cast<uint16_t>(raw_ptr[rp]) |
                                             (static_cast<uint16_t>(raw_ptr[rp + 1]) << 8);
                        rp += 2;
                        val = f16_signed_to_f32(h16);
                    } else if (pt == PixelType::UINT) {
                        const uint32_t u = detail::rd_u32(raw_ptr + rp);
                        rp += 4;
                        val = static_cast<float>(u);
                    } else {
                        val = detail::rd_f32(raw_ptr + rp);
                        rp += 4;
                    }
                    dst_vec[static_cast<size_t>(sr) * pm.w + sc] = val;
                }
            }
        }
    }
    return true;
}

} // namespace heraw_exr

// ---------------------------------------------------------------------------
// F-A & Audit Cross-Section #3: 2 GiB Zero-Temp-Disk Stripe Streaming Engine
// ---------------------------------------------------------------------------
namespace heraw_stream {

struct StreamingTelemetry {
    bool streaming_active = false;
    bool stripe_mode = false;
    uint64_t peak_bytes = 0;
    uint64_t pixel_working_bytes = 0;
    uint64_t frame_index_bytes = 0;
    uint64_t budget_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    int stripe_height_1x = 0;
    int halo_rows_1x = 32;
    int num_stripes = 1;
    int full_decode_calls = 0;
    int roi_reads = 0;
    int completed_passes = 0;
    std::string effective_tier = "full"; // "full" | "stripe-2pass" | "roi-replay-narrow"
};

struct StripePlan {
    bool use_streaming = false;
    bool use_stripe = false;
    int stripe_h1 = 0;
    int halo_h1 = 32;
    int n_stripes = 1;
    uint64_t est_full_stack_bytes = 0;
    uint64_t est_working_bytes = 0;        // O(1)-in-N pixel working set (active_rgb + stripe_work)
    uint64_t est_frame_index_bytes = 0;    // O(N) compressed per-frame store (Audit Cross-Section #3)
    uint64_t est_total_resident_bytes = 0; // est_working_bytes + est_frame_index_bytes
    std::string effective_tier = "full";
};

// Plan memory-bounded streaming geometry against explicit mem_budget_gb (Audit Cross-Section #3).
// Example: 8256 x 4644 at 2x RGB32f = 1.714 GiB alone for 1 buffer; 2 GiB budget triggers stripe mode.
inline StripePlan plan_streaming_execution(
    int N, int H1, int W1, int out_scale,
    int stream_pref, // 0=auto, 1=on, -1=off
    int mem_budget_gb
) noexcept {
    StripePlan pl;
    const uint64_t budget = static_cast<uint64_t>(std::max(1, mem_budget_gb)) * 1024ULL * 1024ULL * 1024ULL;
    const uint64_t p1 = static_cast<uint64_t>(std::max(1, H1)) * static_cast<uint64_t>(std::max(1, W1));
    const uint64_t p_out = p1 * static_cast<uint64_t>(out_scale * out_scale);
    const uint64_t active_rgb_b = p_out * 12ULL;
    pl.est_full_stack_bytes = static_cast<uint64_t>(std::max(1, N)) * p1 * 4ULL + active_rgb_b;

    const bool auto_trigger = (N > 20) || (pl.est_full_stack_bytes > (3ULL * 1024ULL * 1024ULL * 1024ULL)) ||
                              (active_rgb_b > (budget * 55ULL) / 100ULL);
    if (stream_pref < 0) pl.use_streaming = false;
    else if (stream_pref > 0) pl.use_streaming = true;
    else pl.use_streaming = auto_trigger;

    if (!pl.use_streaming) {
        pl.use_stripe = false;
        pl.stripe_h1 = H1;
        pl.halo_h1 = 0;
        pl.n_stripes = 1;
        pl.est_working_bytes = pl.est_full_stack_bytes;
        pl.est_frame_index_bytes = 0;
        pl.est_total_resident_bytes = pl.est_full_stack_bytes;
        pl.effective_tier = "full";
        return pl;
    }

    // Subtract active_rgb_b when sizing O(1) working stripe buffers against budget
    const uint64_t avail_stripe_b = (budget > active_rgb_b) ? (budget - active_rgb_b) : (budget / 8ULL);
    const uint64_t target_pix_budget = std::min<uint64_t>((budget * 65ULL) / 100ULL, (avail_stripe_b * 85ULL) / 100ULL);
    const uint64_t bytes_per_1x_row = static_cast<uint64_t>(std::max(1, W1)) *
        (4ULL * 2ULL + 12ULL * static_cast<uint64_t>(out_scale * out_scale) * 2ULL);
    const uint64_t max_total_rows = target_pix_budget / std::max<uint64_t>(1ULL, bytes_per_1x_row);
    const uint64_t max_core_rows = (max_total_rows > 192ULL) ? (max_total_rows - 64ULL) : 128ULL;
    if (static_cast<int>(max_core_rows) >= H1 && mem_budget_gb >= 4 && active_rgb_b <= (budget * 55ULL) / 100ULL) {
        pl.use_stripe = false;
        pl.stripe_h1 = H1;
        pl.halo_h1 = 0;
        pl.n_stripes = 1;
        pl.est_working_bytes = std::min<uint64_t>(pl.est_full_stack_bytes,
            p1 * static_cast<uint64_t>(8u) + active_rgb_b);
        pl.est_frame_index_bytes = 0;
        pl.est_total_resident_bytes = pl.est_working_bytes;
        pl.effective_tier = "streaming-2pass";
    } else {
        int sh = std::max(128, std::min(H1, static_cast<int>(max_core_rows) & ~63));
        if (sh <= 0) sh = 128;
        pl.use_stripe = (sh < H1);
        pl.stripe_h1 = sh;
        pl.halo_h1 = 32;
        pl.n_stripes = (H1 + sh - 1) / sh;
        const uint64_t stripe_work_b = static_cast<uint64_t>(sh + 2 * pl.halo_h1) * bytes_per_1x_row;
        pl.est_working_bytes = active_rgb_b + stripe_work_b;
        pl.est_frame_index_bytes = pl.use_stripe ? (static_cast<uint64_t>(std::max(1, N)) * p1 * 3ULL) / 2ULL : 0ULL;
        pl.est_total_resident_bytes = pl.est_working_bytes + pl.est_frame_index_bytes;
        pl.effective_tier = pl.use_stripe ? "stripe-2pass" : "streaming-2pass";
    }
    return pl;
}

} // namespace heraw_stream
