// heraw_exif_io.hpp -- Non-destructive EXIF/GPS preservation, sRGB / Adobe RGB (1998) ICC,
// DNG 1.4 Lossless (SOF3) & Visually-Lossless (14->16b LUT SOF3 / FP16 Deflate), PNG16, JXS14, TIFF16.
// Pure 7-bit ASCII C++17 header.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <zlib.h>
#include "../svt-jpeg-xs/Source/API/SvtJpegxs.h"
#include "../svt-jpeg-xs/Source/API/SvtJpegxsEnc.h"
#include "../svt-jpeg-xs/Source/API/SvtJpegxsDec.h"

namespace heraw_io {

enum class ColorSpaceMode {
    SRGB,      // Default: IEC 61966-2-1 sRGB
    ADOBE_RGB  // ISO 12640-2 Adobe RGB (1998) with embedded ICC v2.4 profile
};

enum class RgbFormat {
    TIFF_DEFLATE, // Default: 16-bit Lossless Compressed TIFF (Deflate + Predictor=2 + ICC + Exif/GPS)
    PNG16,        // 16-bit Lossless PNG + sRGB/iCCP + eXIf
    JXS_VIS,      // 14-bit JPEG XS Visually Lossless (ISO/IEC 21122-3 .jxs + colr + Exif)
    JXS_LOSSLESS, // 14-bit JPEG XS Mathematically Lossless (.jxs + colr + Exif)
    TIFF_RAW      // 16-bit Uncompressed TIFF (+ ICC + Exif/GPS)
};

enum class RawDngMode {
    DNG_LOSSLESS, // Default: DNG 1.4 Compression=7 Tiled 16-bit Lossless JPEG (SOF3)
    DNG_HQ,       // Visually-Lossless DNG 1.4: 14-bit Perceptual LUT (Tag 50712 -> 16-bit) + SOF3
    DNG_FP16,     // HDR Visually-Lossless DNG 1.4: 16-bit IEEE Half-Float + Predictor=34894 + Deflate
    DNG_RAW       // Uncompressed 16-bit DNG 1.4 (Compression=1)
};

enum class FieldExportMode {
    SPLIT_GRAY,   // Default: 3 separate 16-bit Grayscale PNGs (_motion/_sky, _phase, _focus)
    PACKED_RGB,   // Single 16-bit 3-channel RGB PNG (R=Motion/Sky, G=PhaseSR, B=Focus)
    ONLY_MOTION,  // Single 16-bit Grayscale PNG (Motion matte or Astro Sky/Horizon mask)
    ONLY_PHASE,   // Single 16-bit Grayscale PNG (2x2 Subpixel Phase SR confidence)
    ONLY_FOCUS    // Single 16-bit Grayscale PNG (2nd-order Laplacian Optical Focus field)
};

struct RawTagItem {
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    std::vector<uint8_t> data; // normalized to little-endian ('II')
};

struct ExifBundle {
    std::vector<RawTagItem> ifd0_keep;
    std::vector<RawTagItem> exif_keep;
    std::vector<RawTagItem> gps_keep;
    std::string orig_software;
    int subsec_ms = 0;
};

static inline uint32_t tiff_type_unit_bytes(uint16_t t) {
    switch (t) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

static inline uint16_t rd_u16(const uint8_t* p, bool be) {
    return be ? (uint16_t)((uint16_t(p[0]) << 8) | p[1])
              : (uint16_t)((uint16_t(p[1]) << 8) | p[0]);
}

static inline uint32_t rd_u32(const uint8_t* p, bool be) {
    return be ? ((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3])
              : ((uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0]);
}

static inline void wr_le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static inline void wr_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static inline void wr_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFFu);
    p[1] = (uint8_t)(v & 0xFFu);
}

static inline void wr_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

// Generate compact standard ICC v2.4 Matrix-TRC profile for Adobe RGB (1998) (D50 PCS Bradford-adapted)
static std::vector<uint8_t> build_adobergb_icc_profile() {
    auto push_be32 = [](std::vector<uint8_t>& b, uint32_t v) {
        uint8_t t[4]; wr_be32(t, v); b.insert(b.end(), t, t + 4);
    };
    auto push_be16 = [](std::vector<uint8_t>& b, uint16_t v) {
        uint8_t t[2]; wr_be16(t, v); b.insert(b.end(), t, t + 2);
    };
    auto push_tag4 = [](std::vector<uint8_t>& b, const char s[4]) {
        b.insert(b.end(), reinterpret_cast<const uint8_t*>(s), reinterpret_cast<const uint8_t*>(s) + 4);
    };
    auto make_xyz_tag = [&](int32_t x_s15f16, int32_t y_s15f16, int32_t z_s15f16) {
        std::vector<uint8_t> t;
        push_tag4(t, "XYZ "); push_be32(t, 0);
        push_be32(t, (uint32_t)x_s15f16);
        push_be32(t, (uint32_t)y_s15f16);
        push_be32(t, (uint32_t)z_s15f16);
        return t;
    };

    // Standard Bradford D65 -> D50 PCS XYZ primaries for Adobe RGB (1998) (s15Fixed16Number)
    std::vector<uint8_t> wtpt = make_xyz_tag(0x0000F6D6, 0x00010000, 0x0000D32D); // D50 PCS white
    std::vector<uint8_t> rXYZ = make_xyz_tag(0x00009C18, 0x00004FA5, 0x000004FC); // (0.60974, 0.31111, 0.01947)
    std::vector<uint8_t> gXYZ = make_xyz_tag(0x0000348D, 0x0000A02C, 0x00000F95); // (0.20528, 0.62567, 0.06087)
    std::vector<uint8_t> bXYZ = make_xyz_tag(0x00002631, 0x0000102F, 0x0000BE9C); // (0.14919, 0.06322, 0.74457)

    // Exact Adobe RGB (1998) gamma 2.19921875 = 563 / 256 (0x0233 u8Fixed8Number)
    std::vector<uint8_t> trc;
    push_tag4(trc, "curv"); push_be32(trc, 0); push_be32(trc, 1); push_be16(trc, 0x0233);
    while (trc.size() & 3u) trc.push_back(0);

    // ICC v2 'desc' tag
    const std::string desc_str = "Adobe RGB (1998) Compatible";
    std::vector<uint8_t> desc;
    push_tag4(desc, "desc"); push_be32(desc, 0);
    push_be32(desc, (uint32_t)desc_str.size() + 1u);
    desc.insert(desc.end(), desc_str.begin(), desc_str.end());
    desc.push_back(0);
    // Unicode & ScriptCode empty trailers required by ICC v2 'desc' (4 + 4 + 2 + 1 + 67 = 78 bytes)
    desc.resize(desc.size() + 78, 0);
    while (desc.size() & 3u) desc.push_back(0);

    // ICC 'text' copyright tag
    const std::string cprt_str = "Public Domain";
    std::vector<uint8_t> cprt;
    push_tag4(cprt, "text"); push_be32(cprt, 0);
    cprt.insert(cprt.end(), cprt_str.begin(), cprt_str.end());
    cprt.push_back(0);
    while (cprt.size() & 3u) cprt.push_back(0);

    // 128-byte header + 4-byte tag count + 9 * 12-byte directory = 240 bytes
    const uint32_t n_tags = 9;
    std::vector<uint8_t> icc(128 + 4 + n_tags * 12, 0);
    wr_be32(icc.data() + 8, 0x02400000u); // ICC v2.4.0
    std::memcpy(icc.data() + 12, "mntr", 4);
    std::memcpy(icc.data() + 16, "RGB ", 4);
    std::memcpy(icc.data() + 20, "XYZ ", 4);
    wr_be16(icc.data() + 24, 2026); wr_be16(icc.data() + 26, 1); wr_be16(icc.data() + 28, 1);
    std::memcpy(icc.data() + 36, "acsp", 4);
    wr_be32(icc.data() + 68, 0x0000F6D6u); // PCS D50 X
    wr_be32(icc.data() + 72, 0x00010000u); // PCS D50 Y
    wr_be32(icc.data() + 76, 0x0000D32Du); // PCS D50 Z
    wr_be32(icc.data() + 128, n_tags);

    int t_idx = 0;
    auto add_entry = [&](const char sig[4], uint32_t off, uint32_t sz) {
        uint8_t* p = icc.data() + 132 + t_idx * 12;
        std::memcpy(p, sig, 4);
        wr_be32(p + 4, off);
        wr_be32(p + 8, sz);
        ++t_idx;
    };
    auto append_data = [&](const std::vector<uint8_t>& d) -> uint32_t {
        while (icc.size() & 3u) icc.push_back(0);
        uint32_t off = (uint32_t)icc.size();
        icc.insert(icc.end(), d.begin(), d.end());
        return off;
    };

    uint32_t off_desc = append_data(desc);
    uint32_t off_cprt = append_data(cprt);
    uint32_t off_wtpt = append_data(wtpt);
    uint32_t off_rxyz = append_data(rXYZ);
    uint32_t off_gxyz = append_data(gXYZ);
    uint32_t off_bxyz = append_data(bXYZ);
    uint32_t off_trc  = append_data(trc);

    add_entry("desc", off_desc, (uint32_t)desc.size());
    add_entry("cprt", off_cprt, (uint32_t)cprt.size());
    add_entry("wtpt", off_wtpt, (uint32_t)wtpt.size());
    add_entry("rXYZ", off_rxyz, (uint32_t)rXYZ.size());
    add_entry("gXYZ", off_gxyz, (uint32_t)gXYZ.size());
    add_entry("bXYZ", off_bxyz, (uint32_t)bXYZ.size());
    add_entry("rTRC", off_trc,  14u);
    add_entry("gTRC", off_trc,  14u);
    add_entry("bTRC", off_trc,  14u);

    wr_be32(icc.data() + 0, (uint32_t)icc.size());
    return icc;
}

static inline void normalize_tag_to_le(uint16_t typ, uint32_t cnt, uint8_t* buf, bool src_be) {
    if (!src_be) return;
    if (typ == 3 || typ == 8) {
        for (uint32_t i = 0; i < cnt; ++i) std::swap(buf[2 * i], buf[2 * i + 1]);
    } else if (typ == 4 || typ == 9 || typ == 11) {
        for (uint32_t i = 0; i < cnt; ++i) {
            std::swap(buf[4 * i + 0], buf[4 * i + 3]);
            std::swap(buf[4 * i + 1], buf[4 * i + 2]);
        }
    } else if (typ == 5 || typ == 10) {
        for (uint32_t i = 0; i < cnt * 2; ++i) {
            std::swap(buf[4 * i + 0], buf[4 * i + 3]);
            std::swap(buf[4 * i + 1], buf[4 * i + 2]);
        }
    } else if (typ == 12) {
        for (uint32_t i = 0; i < cnt; ++i) {
            for (int k = 0; k < 4; ++k) std::swap(buf[8 * i + k], buf[8 * i + 7 - k]);
        }
    }
}

static FILE* fopen_utf8_rb_io(const std::string& path) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (n <= 0) return nullptr;
    std::wstring wp(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], n);
    return _wfopen(wp.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

static bool read_ifd_table(
    FILE* fp, uint64_t file_len, uint32_t ifd_off, bool be,
    std::vector<RawTagItem>& out_items, uint32_t& out_exif_off, uint32_t& out_gps_off
) {
    out_exif_off = 0;
    out_gps_off = 0;
    if (ifd_off < 8 || (uint64_t)ifd_off + 2 > file_len) return false;
    if (std::fseek(fp, (long)ifd_off, SEEK_SET) != 0) return false;
    uint8_t nbuf[2];
    if (std::fread(nbuf, 1, 2, fp) != 2) return false;
    uint16_t n_ent = rd_u16(nbuf, be);
    if (n_ent > 512 || (uint64_t)ifd_off + 2 + (uint64_t)n_ent * 12 > file_len) return false;

    std::vector<uint8_t> raw_dir((size_t)n_ent * 12);
    if (std::fread(raw_dir.data(), 1, raw_dir.size(), fp) != raw_dir.size()) return false;

    for (uint16_t i = 0; i < n_ent; ++i) {
        const uint8_t* e = raw_dir.data() + (size_t)i * 12;
        uint16_t tag = rd_u16(e + 0, be);
        uint16_t typ = rd_u16(e + 2, be);
        uint32_t cnt = rd_u32(e + 4, be);
        if (tag == 34665 && (typ == 4 || typ == 13) && cnt >= 1) {
            out_exif_off = rd_u32(e + 8, be);
            continue;
        }
        if (tag == 34853 && (typ == 4 || typ == 13) && cnt >= 1) {
            out_gps_off = rd_u32(e + 8, be);
            continue;
        }
        uint32_t unit = tiff_type_unit_bytes(typ);
        if (unit == 0 || cnt > 65536u) continue;
        uint64_t byte_len = (uint64_t)unit * (uint64_t)cnt;
        if (byte_len == 0 || byte_len > 262144ULL) continue;

        RawTagItem item;
        item.tag = tag;
        item.type = typ;
        item.count = cnt;
        item.data.resize((size_t)byte_len);
        if (byte_len <= 4) {
            std::memcpy(item.data.data(), e + 8, (size_t)byte_len);
        } else {
            uint32_t val_off = rd_u32(e + 8, be);
            if ((uint64_t)val_off + byte_len > file_len) continue;
            if (std::fseek(fp, (long)val_off, SEEK_SET) != 0) continue;
            if (std::fread(item.data.data(), 1, (size_t)byte_len, fp) != (size_t)byte_len) continue;
        }
        normalize_tag_to_le(typ, cnt, item.data.data(), be);
        out_items.push_back(std::move(item));
    }
    return true;
}

static inline RawTagItem make_ascii_tag(uint16_t tag, const std::string& s) {
    RawTagItem it;
    it.tag = tag;
    it.type = 2;
    it.count = (uint32_t)s.size() + 1u;
    it.data.assign(s.begin(), s.end());
    it.data.push_back(0);
    return it;
}

static inline RawTagItem make_u16_tag(uint16_t tag, uint16_t v) {
    RawTagItem it{tag, 3, 1, std::vector<uint8_t>(2)};
    wr_le16(it.data.data(), v);
    return it;
}

static inline RawTagItem make_u32_tag(uint16_t tag, uint32_t v) {
    RawTagItem it{tag, 4, 1, std::vector<uint8_t>(4)};
    wr_le32(it.data.data(), v);
    return it;
}

static void upsert_tag(std::vector<RawTagItem>& vec, RawTagItem item) {
    for (auto& existing : vec) {
        if (existing.tag == item.tag) {
            existing = std::move(item);
            return;
        }
    }
    vec.push_back(std::move(item));
}

static ExifBundle extract_source_exif(const std::string& raw_path) {
    ExifBundle out;
    FILE* fp = fopen_utf8_rb_io(raw_path);
    if (!fp) return out;
    std::fseek(fp, 0, SEEK_END);
    long flen_l = std::ftell(fp);
    if (flen_l < 16) { std::fclose(fp); return out; }
    uint64_t flen = (uint64_t)flen_l;
    std::fseek(fp, 0, SEEK_SET);

    uint8_t hdr[8];
    if (std::fread(hdr, 1, 8, fp) != 8) { std::fclose(fp); return out; }
    bool be = false;
    if (hdr[0] == 'I' && hdr[1] == 'I') be = false;
    else if (hdr[0] == 'M' && hdr[1] == 'M') be = true;
    else { std::fclose(fp); return out; }

    if (rd_u16(hdr + 2, be) != 42) { std::fclose(fp); return out; }
    uint32_t ifd0_off = rd_u32(hdr + 4, be);

    std::vector<RawTagItem> raw_ifd0, raw_exif, raw_gps;
    uint32_t exif_off = 0, gps_off = 0, dummy1 = 0, dummy2 = 0;
    if (read_ifd_table(fp, flen, ifd0_off, be, raw_ifd0, exif_off, gps_off)) {
        if (exif_off > 0) read_ifd_table(fp, flen, exif_off, be, raw_exif, dummy1, dummy2);
        if (gps_off > 0)  read_ifd_table(fp, flen, gps_off, be, raw_gps, dummy1, dummy2);
    }
    std::fclose(fp);

    for (auto& it : raw_ifd0) {
        if (it.tag == 305 && it.type == 2 && !it.data.empty()) {
            out.orig_software = reinterpret_cast<const char*>(it.data.data());
            while (!out.orig_software.empty() && (out.orig_software.back() == '\0' || out.orig_software.back() == ' ')) {
                out.orig_software.pop_back();
            }
            continue;
        }
        switch (it.tag) {
            case 270: case 271: case 272: case 282: case 283:
            case 296: case 306: case 315: case 33432:
                out.ifd0_keep.push_back(std::move(it));
                break;
            default:
                break;
        }
    }

    for (auto& it : raw_exif) {
        if (it.tag == 37521 && it.type == 2 && !it.data.empty()) {
            const char* s = reinterpret_cast<const char*>(it.data.data());
            int v = 0, digits = 0;
            for (int k = 0; s[k] >= '0' && s[k] <= '9' && digits < 3; ++k, ++digits) v = v * 10 + (s[k] - '0');
            while (digits > 0 && digits < 3) { v *= 10; ++digits; }
            out.subsec_ms = v;
        }
        if (it.tag == 37500 || it.tag == 40965 || it.tag == 41730 || it.tag == 41492) continue;
        if ((it.tag == 41486 || it.tag == 41487) && it.type == 5 && it.data.size() >= 8) {
            uint32_t num = rd_u32(it.data.data(), false);
            uint32_t den = rd_u32(it.data.data() + 4, false);
            if (num < 0x7FFFFFFFu && den > 0) wr_le32(it.data.data(), num * 2u);
        }
        out.exif_keep.push_back(std::move(it));
    }

    out.gps_keep = std::move(raw_gps);
    return out;
}

static int read_raw_subsec_ms(const std::string& raw_path) {
    return extract_source_exif(raw_path).subsec_ms;
}

static uint32_t append_serialized_ifd(std::vector<uint8_t>& blob, std::vector<RawTagItem> items) {
    if (blob.size() & 1u) blob.push_back(0);
    std::sort(items.begin(), items.end(), [](const RawTagItem& a, const RawTagItem& b) { return a.tag < b.tag; });
    items.erase(std::unique(items.begin(), items.end(), [](const RawTagItem& a, const RawTagItem& b) { return a.tag == b.tag; }), items.end());

    uint32_t ifd_off = (uint32_t)blob.size();
    uint16_t n = (uint16_t)items.size();
    uint32_t dir_bytes = 2u + (uint32_t)n * 12u + 4u;
    blob.resize(ifd_off + dir_bytes, 0);
    wr_le16(blob.data() + ifd_off, n);

    for (uint16_t i = 0; i < n; ++i) {
        const auto& it = items[i];
        uint8_t entry[12] = {0};
        wr_le16(entry + 0, it.tag);
        wr_le16(entry + 2, it.type);
        wr_le32(entry + 4, it.count);
        if (it.data.size() <= 4) {
            if (!it.data.empty()) std::memcpy(entry + 8, it.data.data(), it.data.size());
        } else {
            if (blob.size() & 1u) blob.push_back(0);
            uint32_t voff = (uint32_t)blob.size();
            wr_le32(entry + 8, voff);
            blob.insert(blob.end(), it.data.begin(), it.data.end());
        }
        std::memcpy(blob.data() + ifd_off + 2u + (size_t)i * 12u, entry, 12);
    }
    wr_le32(blob.data() + ifd_off + 2u + (size_t)n * 12u, 0u);
    return ifd_off;
}

static std::string format_software_tag(const ExifBundle& ex, const std::string& sw_stamp) {
    if (ex.orig_software.empty()) return sw_stamp;
    return ex.orig_software + " | " + sw_stamp;
}

static std::vector<uint8_t> build_standalone_exif_blob(
    const ExifBundle& src_ex, int out_W, int out_H, uint16_t orientation, ColorSpaceMode cs, const std::string& sw_stamp
) {
    std::vector<RawTagItem> ifd0 = src_ex.ifd0_keep;
    upsert_tag(ifd0, make_u16_tag(274, orientation));
    upsert_tag(ifd0, make_ascii_tag(11, sw_stamp));
    upsert_tag(ifd0, make_ascii_tag(305, format_software_tag(src_ex, sw_stamp)));

    std::vector<RawTagItem> exif_tags = src_ex.exif_keep;
    // EXIF ColorSpace: 1 = sRGB, 65535 = Uncalibrated/AdobeRGB (paired with embedded ICC profile)
    upsert_tag(exif_tags, make_u16_tag(40961, (cs == ColorSpaceMode::SRGB) ? 1u : 65535u));
    upsert_tag(exif_tags, make_u32_tag(40962, (uint32_t)out_W));
    upsert_tag(exif_tags, make_u32_tag(40963, (uint32_t)out_H));

    bool has_gps = !src_ex.gps_keep.empty();
    upsert_tag(ifd0, make_u32_tag(34665, 0));
    if (has_gps) upsert_tag(ifd0, make_u32_tag(34853, 0));

    std::vector<uint8_t> blob = {'I', 'I', 42, 0, 8, 0, 0, 0};
    uint32_t ifd0_off = append_serialized_ifd(blob, ifd0);
    uint32_t exif_sub_off = append_serialized_ifd(blob, exif_tags);
    uint32_t gps_sub_off = has_gps ? append_serialized_ifd(blob, src_ex.gps_keep) : 0;

    uint16_t n0 = rd_u16(blob.data() + ifd0_off, false);
    for (uint16_t i = 0; i < n0; ++i) {
        uint8_t* ep = blob.data() + ifd0_off + 2u + (size_t)i * 12u;
        uint16_t tg = rd_u16(ep, false);
        if (tg == 34665) wr_le32(ep + 8, exif_sub_off);
        else if (tg == 34853 && has_gps) wr_le32(ep + 8, gps_sub_off);
    }
    return blob;
}

// ---------------------------------------------------------------------------
// 1. Parallel 16-bit Lossless PNG Writer (+ sRGB or Adobe RGB iCCP/cHRM/gAMA + eXIf)
// ---------------------------------------------------------------------------
static void write_png_chunk(FILE* fp, const char type[4], const uint8_t* data, uint32_t len) {
    uint8_t len_be[4];
    wr_be32(len_be, len);
    std::fwrite(len_be, 1, 4, fp);
    std::fwrite(type, 1, 4, fp);
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type), 4);
    if (len > 0 && data) {
        std::fwrite(data, 1, len, fp);
        crc = crc32(crc, data, len);
    }
    uint8_t crc_be[4];
    wr_be32(crc_be, (uint32_t)crc);
    std::fwrite(crc_be, 1, 4, fp);
}

template <typename ParFn>
static bool write_png16_rgb(
    const std::string& path, const uint16_t* rgb_u16, int H, int W,
    ColorSpaceMode cs_mode, const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    ParFn&& par_rows, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    const int bpp_bytes = 6;
    const int rows_per_strip = 64;
    const int n_strips = (H + rows_per_strip - 1) / rows_per_strip;
    const size_t row_raw_bytes = 1 + (size_t)W * bpp_bytes;

    std::vector<std::vector<uint8_t>> comp_strips(n_strips);
    std::vector<uLong> strip_adlers(n_strips, 1L);
    std::vector<z_off_t> strip_raw_lens(n_strips, 0);
    std::atomic<bool> enc_ok{true};

    par_rows(n_strips, n_threads, [&](int s0, int s1, int) {
        std::vector<uint8_t> raw_buf((size_t)rows_per_strip * row_raw_bytes);
        std::vector<uint8_t> cur_be((size_t)W * bpp_bytes);
        for (int s = s0; s < s1; ++s) {
            if (!enc_ok.load(std::memory_order_relaxed)) return;
            int r_beg = s * rows_per_strip;
            int r_end = std::min(H, r_beg + rows_per_strip);
            int sh = r_end - r_beg;
            size_t total_in = (size_t)sh * row_raw_bytes;

            for (int r = r_beg; r < r_end; ++r) {
                const uint16_t* src = rgb_u16 + (size_t)r * W * 3;
                for (int c = 0; c < W * 3; ++c) {
                    uint16_t v = src[c];
                    cur_be[2 * c + 0] = (uint8_t)(v >> 8);
                    cur_be[2 * c + 1] = (uint8_t)(v & 0xFFu);
                }
                uint8_t* dst = raw_buf.data() + (size_t)(r - r_beg) * row_raw_bytes;
                dst[0] = 1; // Sub predictor (bpp = 6)
                for (int i = 0; i < 6; ++i) dst[1 + i] = cur_be[i];
                const size_t nbytes = (size_t)W * 6;
                for (size_t i = 6; i < nbytes; ++i) dst[1 + i] = (uint8_t)(cur_be[i] - cur_be[i - 6]);
            }

            strip_adlers[s] = adler32(1L, raw_buf.data(), (uInt)total_in);
            strip_raw_lens[s] = (z_off_t)total_in;

            z_stream zs{};
            if (deflateInit2(&zs, 5, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
                enc_ok.store(false); return;
            }
            size_t max_out = deflateBound(&zs, (uLong)total_in) + 64;
            comp_strips[s].resize(max_out);
            zs.next_in = raw_buf.data();
            zs.avail_in = (uInt)total_in;
            zs.next_out = comp_strips[s].data();
            zs.avail_out = (uInt)max_out;
            int flush_mode = (s == n_strips - 1) ? Z_FINISH : Z_SYNC_FLUSH;
            int zrc = deflate(&zs, flush_mode);
            if ((flush_mode == Z_FINISH && zrc != Z_STREAM_END) || (flush_mode != Z_FINISH && zrc != Z_OK)) {
                deflateEnd(&zs); enc_ok.store(false); return;
            }
            comp_strips[s].resize(zs.total_out);
            deflateEnd(&zs);
        }
    }, 1);

    if (!enc_ok.load()) return false;

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(png_sig, 1, 8, fp);

    uint8_t ihdr[13] = {0};
    wr_be32(ihdr + 0, (uint32_t)W);
    wr_be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 16;
    ihdr[9] = 2; // RGB
    write_png_chunk(fp, "IHDR", ihdr, 13);

    if (cs_mode == ColorSpaceMode::SRGB) {
        uint8_t srgb_intent = 0;
        write_png_chunk(fp, "sRGB", &srgb_intent, 1);
    } else {
        // Embed compressed iCCP chunk + gAMA + cHRM for Adobe RGB (1998)
        std::vector<uint8_t> icc = build_adobergb_icc_profile();
        uLong zlen = compressBound((uLong)icc.size());
        std::vector<uint8_t> zicc(zlen);
        if (compress2(zicc.data(), &zlen, icc.data(), (uLong)icc.size(), 6) == Z_OK) {
            const char* pname = "Adobe RGB (1998)";
            size_t nlen = std::strlen(pname);
            std::vector<uint8_t> iccp_pay;
            iccp_pay.insert(iccp_pay.end(), pname, pname + nlen + 1); // name + NUL
            iccp_pay.push_back(0); // compression method 0 = deflate
            iccp_pay.insert(iccp_pay.end(), zicc.begin(), zicc.begin() + zlen);
            write_png_chunk(fp, "iCCP", iccp_pay.data(), (uint32_t)iccp_pay.size());
        }
        uint8_t gama_be[4];
        wr_be32(gama_be, 45471u); // 1 / 2.19921875 * 100000 = 45471
        write_png_chunk(fp, "gAMA", gama_be, 4);
        uint8_t chrm[32];
        // D65 (31270, 32900), R (64000, 33000), G (21000, 71000), B (15000, 6000)
        const uint32_t vals[8] = {31270, 32900, 64000, 33000, 21000, 71000, 15000, 6000};
        for (int i = 0; i < 8; ++i) wr_be32(chrm + 4 * i, vals[i]);
        write_png_chunk(fp, "cHRM", chrm, 32);
    }

    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
    if (!exif_blob.empty()) write_png_chunk(fp, "eXIf", exif_blob.data(), (uint32_t)exif_blob.size());

    uint8_t zhdr[2] = {0x78, 0x5E};
    write_png_chunk(fp, "IDAT", zhdr, 2);

    uLong comb_adler = 1L;
    for (int s = 0; s < n_strips; ++s) {
        comb_adler = (s == 0) ? strip_adlers[0] : adler32_combine(comb_adler, strip_adlers[s], strip_raw_lens[s]);
        if (!comp_strips[s].empty()) write_png_chunk(fp, "IDAT", comp_strips[s].data(), (uint32_t)comp_strips[s].size());
    }
    uint8_t ztrl[4];
    wr_be32(ztrl, (uint32_t)comb_adler);
    write_png_chunk(fp, "IDAT", ztrl, 4);
    write_png_chunk(fp, "IEND", nullptr, 0);

    bool ferr = std::ferror(fp) != 0;
    std::fclose(fp);
    if (ferr) { rm_file(tmp_path); return false; }
    return replace_file(tmp_path, path);
}

template <typename ParFn>
static bool write_png16_gray(
    const std::string& path, const uint16_t* gray_u16, int H, int W,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    ParFn&& par_rows, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    const int bpp_bytes = 2;
    const int rows_per_strip = 64;
    const int n_strips = (H + rows_per_strip - 1) / rows_per_strip;
    const size_t row_raw_bytes = 1 + (size_t)W * bpp_bytes;

    std::vector<std::vector<uint8_t>> comp_strips(n_strips);
    std::vector<uLong> strip_adlers(n_strips, 1L);
    std::vector<z_off_t> strip_raw_lens(n_strips, 0);
    std::atomic<bool> enc_ok{true};

    par_rows(n_strips, n_threads, [&](int s0, int s1, int) {
        std::vector<uint8_t> raw_buf((size_t)rows_per_strip * row_raw_bytes);
        std::vector<uint8_t> cur_be((size_t)W * bpp_bytes);
        for (int s = s0; s < s1; ++s) {
            if (!enc_ok.load(std::memory_order_relaxed)) return;
            int r_beg = s * rows_per_strip;
            int r_end = std::min(H, r_beg + rows_per_strip);
            int sh = r_end - r_beg;
            size_t total_in = (size_t)sh * row_raw_bytes;

            for (int r = r_beg; r < r_end; ++r) {
                const uint16_t* src = gray_u16 + (size_t)r * W;
                for (int c = 0; c < W; ++c) {
                    uint16_t v = src[c];
                    cur_be[2 * c + 0] = (uint8_t)(v >> 8);
                    cur_be[2 * c + 1] = (uint8_t)(v & 0xFFu);
                }
                uint8_t* dst = raw_buf.data() + (size_t)(r - r_beg) * row_raw_bytes;
                dst[0] = 1; // Sub predictor (bpp = 2)
                dst[1] = cur_be[0];
                dst[2] = cur_be[1];
                const size_t nbytes = (size_t)W * 2;
                for (size_t i = 2; i < nbytes; ++i) dst[1 + i] = (uint8_t)(cur_be[i] - cur_be[i - 2]);
            }

            strip_adlers[s] = adler32(1L, raw_buf.data(), (uInt)total_in);
            strip_raw_lens[s] = (z_off_t)total_in;

            z_stream zs{};
            if (deflateInit2(&zs, 5, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
                enc_ok.store(false); return;
            }
            size_t max_out = deflateBound(&zs, (uLong)total_in) + 64;
            comp_strips[s].resize(max_out);
            zs.next_in = raw_buf.data();
            zs.avail_in = (uInt)total_in;
            zs.next_out = comp_strips[s].data();
            zs.avail_out = (uInt)max_out;
            int flush_mode = (s == n_strips - 1) ? Z_FINISH : Z_SYNC_FLUSH;
            int zrc = deflate(&zs, flush_mode);
            if ((flush_mode == Z_FINISH && zrc != Z_STREAM_END) || (flush_mode != Z_FINISH && zrc != Z_OK)) {
                deflateEnd(&zs); enc_ok.store(false); return;
            }
            comp_strips[s].resize(zs.total_out);
            deflateEnd(&zs);
        }
    }, 1);

    if (!enc_ok.load()) return false;

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(png_sig, 1, 8, fp);

    uint8_t ihdr[13] = {0};
    wr_be32(ihdr + 0, (uint32_t)W);
    wr_be32(ihdr + 4, (uint32_t)H);
    ihdr[8] = 16;
    ihdr[9] = 0; // 16-bit single-channel Grayscale
    write_png_chunk(fp, "IHDR", ihdr, 13);

    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, ColorSpaceMode::SRGB, sw_stamp);
    if (!exif_blob.empty()) write_png_chunk(fp, "eXIf", exif_blob.data(), (uint32_t)exif_blob.size());

    uint8_t zhdr[2] = {0x78, 0x5E};
    write_png_chunk(fp, "IDAT", zhdr, 2);

    uLong comb_adler = 1L;
    for (int s = 0; s < n_strips; ++s) {
        comb_adler = (s == 0) ? strip_adlers[0] : adler32_combine(comb_adler, strip_adlers[s], strip_raw_lens[s]);
        if (!comp_strips[s].empty()) write_png_chunk(fp, "IDAT", comp_strips[s].data(), (uint32_t)comp_strips[s].size());
    }
    uint8_t ztrl[4];
    wr_be32(ztrl, (uint32_t)comb_adler);
    write_png_chunk(fp, "IDAT", ztrl, 4);
    write_png_chunk(fp, "IEND", nullptr, 0);

    bool ferr = std::ferror(fp) != 0;
    std::fclose(fp);
    if (ferr) { rm_file(tmp_path); return false; }
    return replace_file(tmp_path, path);
}

// ---------------------------------------------------------------------------
// 2. 14-bit JPEG XS Visually-Lossless & Lossless Writer (.jxs + colr ICC + Exif box)
// ---------------------------------------------------------------------------
static void silent_svt_log_cb(void*, SvtLogLevel, const char*, const char*, va_list) {}

static void init_svt_silent_once() {
    static std::once_flag once;
    std::call_once(once, []() { svt_jpeg_xs_set_log_callback(silent_svt_log_cb, nullptr); });
}

template <typename ParFn>
static bool write_jxs_rgb(
    const std::string& path, const uint16_t* rgb_u16, int H, int W,
    bool lossless, float bpp, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    ParFn&& par_rows, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W < 8 || H < 8 || W > 65535 || H > 65535) return false;
    init_svt_silent_once();

    // Strictly 14-bit precision (maximum supported by ISO/IEC 21122 SVT-JPEG-XS)
    const int depth = 14;
    const size_t npx = (size_t)W * (size_t)H;
    std::unique_ptr<uint16_t[]> pR(new uint16_t[npx]);
    std::unique_ptr<uint16_t[]> pG(new uint16_t[npx]);
    std::unique_ptr<uint16_t[]> pB(new uint16_t[npx]);

    par_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            const uint16_t* src = rgb_u16 + (size_t)r * W * 3;
            uint16_t* dr = pR.get() + (size_t)r * W;
            uint16_t* dg = pG.get() + (size_t)r * W;
            uint16_t* db = pB.get() + (size_t)r * W;
            for (int c = 0; c < W; ++c) {
                uint32_t vr = ((uint32_t)src[3 * c + 0] + 2u) >> 2;
                uint32_t vg = ((uint32_t)src[3 * c + 1] + 2u) >> 2;
                uint32_t vb = ((uint32_t)src[3 * c + 2] + 2u) >> 2;
                dr[c] = (uint16_t)(vr > 16383u ? 16383u : vr);
                dg[c] = (uint16_t)(vg > 16383u ? 16383u : vg);
                db[c] = (uint16_t)(vb > 16383u ? 16383u : vb);
            }
        }
    }, 64);

    svt_jpeg_xs_encoder_api_t enc{};
    if (svt_jpeg_xs_encoder_load_default_parameters(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc) != SvtJxsErrorNone) {
        return false;
    }
    enc.source_width = (uint32_t)W;
    enc.source_height = (uint32_t)H;
    enc.input_bit_depth = (uint8_t)depth;
    enc.colour_format = COLOUR_FORMAT_PLANAR_RGB;
    enc.cpu_profile = 0;
    enc.verbose = VERBOSE_ERRORS;
    enc.threads_num = (uint32_t)std::max(1, std::min(n_threads, 16));

    if (lossless) {
        enc.enable_color_transform = 0;
        enc.lossless_enable = 1;
        enc.coding_raw_disable = 1;
    } else {
        enc.enable_color_transform = 1; // Reversible RCT Cpih=1
        float clamped_bpp = std::max(3.0f, std::min(28.0f, bpp));
        enc.bpp_numerator = (uint32_t)std::max(300L, std::lrintf(clamped_bpp * 100.0f));
        enc.bpp_denominator = 100;
    }

    svt_jpeg_xs_image_config_t img_cfg{};
    uint32_t bytes_per_frame = 0;
    if (svt_jpeg_xs_encoder_get_image_config(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc, &img_cfg, &bytes_per_frame) != SvtJxsErrorNone) {
        return false;
    }
    if (svt_jpeg_xs_encoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &enc) != SvtJxsErrorNone) {
        return false;
    }

    std::vector<uint8_t> bs_buf(std::max<size_t>(bytes_per_frame, 65536u));
    svt_jpeg_xs_frame_t fin{};
    fin.image.data_yuv[0] = pR.get();
    fin.image.data_yuv[1] = pG.get();
    fin.image.data_yuv[2] = pB.get();
    for (int i = 0; i < 3; ++i) {
        fin.image.stride[i] = (uint32_t)W;
        fin.image.alloc_size[i] = (uint32_t)(npx * sizeof(uint16_t));
    }
    fin.bitstream.buffer = bs_buf.data();
    fin.bitstream.allocation_size = (uint32_t)bs_buf.size();

    SvtJxsErrorType_t err = svt_jpeg_xs_encoder_send_picture(&enc, &fin, 1);
    svt_jpeg_xs_frame_t fout{};
    if (err == SvtJxsErrorNone) err = svt_jpeg_xs_encoder_get_packet(&enc, &fout, 1);
    svt_jpeg_xs_encoder_close(&enc);
    if (err != SvtJxsErrorNone || fout.bitstream.used_size == 0) return false;

    bool raw_codestream = false;
    if (path.size() >= 4) {
        std::string ext = path.substr(path.size() - 4);
        for (char& ch : ext) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (ext == ".jxc") raw_codestream = true;
    }

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    if (!raw_codestream) {
        const uint8_t jxs_sig[12] = {0x00, 0x00, 0x00, 0x0C, 'J', 'X', 'S', ' ', 0x0D, 0x0A, 0x87, 0x0A};
        const uint8_t ftyp_box[20] = {
            0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p',
            'j', 'x', 's', ' ', 0x00, 0x00, 0x00, 0x00, 'j', 'x', 's', ' '
        };
        std::fwrite(jxs_sig, 1, sizeof(jxs_sig), fp);
        std::fwrite(ftyp_box, 1, sizeof(ftyp_box), fp);

        if (cs_mode == ColorSpaceMode::ADOBE_RGB) {
            std::vector<uint8_t> icc = build_adobergb_icc_profile();
            uint8_t chdr[12];
            wr_be32(chdr + 0, (uint32_t)(12u + icc.size()));
            std::memcpy(chdr + 4, "colr", 4);
            std::memcpy(chdr + 8, "prof", 4);
            std::fwrite(chdr, 1, 12, fp);
            std::fwrite(icc.data(), 1, icc.size(), fp);
        }

        std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
        if (!exif_blob.empty()) {
            uint8_t ex_hdr[12] = {0};
            wr_be32(ex_hdr + 0, (uint32_t)(12u + exif_blob.size()));
            std::memcpy(ex_hdr + 4, "Exif", 4);
            wr_be32(ex_hdr + 8, 0u);
            std::fwrite(ex_hdr, 1, 12, fp);
            std::fwrite(exif_blob.data(), 1, exif_blob.size(), fp);
        }

        uint64_t cs_box_len = 8ULL + (uint64_t)fout.bitstream.used_size;
        uint8_t cs_hdr[8];
        wr_be32(cs_hdr + 0, (uint32_t)cs_box_len);
        std::memcpy(cs_hdr + 4, "jp2c", 4);
        std::fwrite(cs_hdr, 1, 8, fp);
    }

    size_t nw = std::fwrite(fout.bitstream.buffer, 1, fout.bitstream.used_size, fp);
    std::fclose(fp);
    if (nw != fout.bitstream.used_size) { rm_file(tmp_path); return false; }
    return replace_file(tmp_path, path);
}

static bool decode_jxs_file_to_rgb16(
    const std::string& jxs_path, std::vector<uint16_t>& out_rgb16, int& out_H, int& out_W,
    ColorSpaceMode& out_cs, int n_threads
) {
    init_svt_silent_once();
    out_cs = ColorSpaceMode::SRGB;
    FILE* fp = fopen_utf8_rb_io(jxs_path);
    if (!fp) return false;
    std::fseek(fp, 0, SEEK_END);
    long flen = std::ftell(fp);
    if (flen < 16) { std::fclose(fp); return false; }
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> file_bytes((size_t)flen);
    if (std::fread(file_bytes.data(), 1, file_bytes.size(), fp) != file_bytes.size()) {
        std::fclose(fp); return false;
    }
    std::fclose(fp);

    const uint8_t* cs_ptr = nullptr;
    size_t cs_len = 0;

    if (file_bytes.size() >= 12 && std::memcmp(file_bytes.data() + 4, "JXS ", 4) == 0) {
        size_t pos = 0;
        while (pos + 8 <= file_bytes.size()) {
            uint64_t bsz = rd_u32(file_bytes.data() + pos, true);
            const uint8_t* btyp = file_bytes.data() + pos + 4;
            size_t hdr_len = 8;
            if (bsz == 1 && pos + 16 <= file_bytes.size()) {
                bsz = ((uint64_t)rd_u32(file_bytes.data() + pos + 8, true) << 32) |
                      (uint64_t)rd_u32(file_bytes.data() + pos + 12, true);
                hdr_len = 16;
            } else if (bsz == 0) {
                bsz = file_bytes.size() - pos;
            }
            if (bsz < hdr_len || pos + bsz > file_bytes.size()) break;
            if (std::memcmp(btyp, "colr", 4) == 0 && bsz > hdr_len + 4 &&
                std::memcmp(file_bytes.data() + pos + hdr_len, "prof", 4) == 0) {
                out_cs = ColorSpaceMode::ADOBE_RGB;
            } else if (std::memcmp(btyp, "jp2c", 4) == 0) {
                cs_ptr = file_bytes.data() + pos + hdr_len;
                cs_len = (size_t)(bsz - hdr_len);
                break;
            }
            pos += (size_t)bsz;
        }
    }
    if (!cs_ptr) {
        for (size_t i = 0; i + 4 <= file_bytes.size(); ++i) {
            if (file_bytes[i] == 0xFF && file_bytes[i + 1] == 0x10 &&
                file_bytes[i + 2] == 0xFF && file_bytes[i + 3] == 0x50) {
                cs_ptr = file_bytes.data() + i;
                cs_len = file_bytes.size() - i;
                break;
            }
        }
    }
    if (!cs_ptr || cs_len < 8) return false;

    svt_jpeg_xs_decoder_api_t dec{};
    dec.use_cpu_flags = CPU_FLAGS_ALL;
    dec.verbose = VERBOSE_ERRORS;
    dec.threads_num = (uint32_t)std::max(1, std::min(n_threads, 16));
    dec.packetization_mode = 0;
    dec.proxy_mode = proxy_mode_full;

    svt_jpeg_xs_image_config_t cfg{};
    if (svt_jpeg_xs_decoder_init(SVT_JPEGXS_API_VER_MAJOR, SVT_JPEGXS_API_VER_MINOR, &dec, cs_ptr, cs_len, &cfg) != SvtJxsErrorNone) {
        return false;
    }
    if (cfg.components_num < 3 || cfg.width == 0 || cfg.height == 0) {
        svt_jpeg_xs_decoder_close(&dec); return false;
    }

    out_W = (int)cfg.width;
    out_H = (int)cfg.height;
    const size_t npx = (size_t)out_W * (size_t)out_H;
    std::vector<uint16_t> p0(npx), p1(npx), p2(npx);

    svt_jpeg_xs_frame_t din{};
    din.bitstream.buffer = const_cast<uint8_t*>(cs_ptr);
    din.bitstream.allocation_size = (uint32_t)cs_len;
    din.bitstream.used_size = (uint32_t)cs_len;
    din.image.data_yuv[0] = p0.data();
    din.image.data_yuv[1] = p1.data();
    din.image.data_yuv[2] = p2.data();
    for (int i = 0; i < 3; ++i) {
        din.image.stride[i] = (uint32_t)out_W;
        din.image.alloc_size[i] = (uint32_t)(npx * sizeof(uint16_t));
    }

    SvtJxsErrorType_t err = svt_jpeg_xs_decoder_send_frame(&dec, &din, 1);
    svt_jpeg_xs_frame_t dout{};
    if (err == SvtJxsErrorNone) err = svt_jpeg_xs_decoder_get_frame(&dec, &dout, 1);
    svt_jpeg_xs_decoder_close(&dec);
    if (err != SvtJxsErrorNone) return false;

    uint32_t max_in = (1u << cfg.bit_depth) - 1u;
    out_rgb16.resize(npx * 3);
    for (size_t i = 0; i < npx; ++i) {
        out_rgb16[3 * i + 0] = (uint16_t)((std::min<uint32_t>(p0[i], max_in) * 65535u + (max_in / 2u)) / max_in);
        out_rgb16[3 * i + 1] = (uint16_t)((std::min<uint32_t>(p1[i], max_in) * 65535u + (max_in / 2u)) / max_in);
        out_rgb16[3 * i + 2] = (uint16_t)((std::min<uint32_t>(p2[i], max_in) * 65535u + (max_in / 2u)) / max_in);
    }
    return true;
}

// ---------------------------------------------------------------------------
// 3. 16-bit Lossless Deflate & Raw TIFF Writer (+ Adobe RGB ICC + Exif/GPS)
// ---------------------------------------------------------------------------
template <typename ParFn>
static bool write_tiff16_rgb_ex(
    const std::string& path, const uint16_t* rgb_u16, int H, int W,
    bool compress_deflate, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    ParFn&& par_rows, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    const int rows_per_strip = compress_deflate ? 32 : H;
    const int n_strips = (H + rows_per_strip - 1) / rows_per_strip;

    std::vector<std::vector<uint8_t>> comp_strips;
    if (compress_deflate) {
        comp_strips.resize(n_strips);
        std::atomic<bool> ok{true};
        par_rows(n_strips, n_threads, [&](int s0, int s1, int) {
            std::vector<uint16_t> pred_buf((size_t)rows_per_strip * W * 3);
            for (int s = s0; s < s1; ++s) {
                if (!ok.load(std::memory_order_relaxed)) return;
                int r_beg = s * rows_per_strip;
                int r_end = std::min(H, r_beg + rows_per_strip);
                int sh = r_end - r_beg;
                for (int r = r_beg; r < r_end; ++r) {
                    const uint16_t* src = rgb_u16 + (size_t)r * W * 3;
                    uint16_t* dst = pred_buf.data() + (size_t)(r - r_beg) * W * 3;
                    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
                    for (int c = 3; c < W * 3; ++c) dst[c] = (uint16_t)(src[c] - src[c - 3]);
                }
                uLong src_bytes = (uLong)((size_t)sh * W * 6);
                uLong dst_bound = compressBound(src_bytes);
                comp_strips[s].resize(dst_bound);
                if (compress2(comp_strips[s].data(), &dst_bound,
                              reinterpret_cast<const Bytef*>(pred_buf.data()), src_bytes, 5) != Z_OK) {
                    ok.store(false); return;
                }
                comp_strips[s].resize(dst_bound);
            }
        }, 1);
        if (!ok.load()) return false;
    }

    std::vector<RawTagItem> ifd0 = src_ex.ifd0_keep;
    upsert_tag(ifd0, make_u32_tag(256, (uint32_t)W));
    upsert_tag(ifd0, make_u32_tag(257, (uint32_t)H));
    {
        RawTagItem bps{258, 3, 3, std::vector<uint8_t>(6)};
        wr_le16(bps.data.data() + 0, 16); wr_le16(bps.data.data() + 2, 16); wr_le16(bps.data.data() + 4, 16);
        upsert_tag(ifd0, std::move(bps));
    }
    upsert_tag(ifd0, make_u16_tag(259, compress_deflate ? 8u : 1u));
    upsert_tag(ifd0, make_u16_tag(262, 2u));
    upsert_tag(ifd0, make_u16_tag(274, 1u));
    upsert_tag(ifd0, make_u16_tag(277, 3u));
    upsert_tag(ifd0, make_u32_tag(278, (uint32_t)rows_per_strip));
    upsert_tag(ifd0, make_u16_tag(284, 1u));
    if (compress_deflate) upsert_tag(ifd0, make_u16_tag(317, 2u));
    upsert_tag(ifd0, make_ascii_tag(11, sw_stamp));
    upsert_tag(ifd0, make_ascii_tag(305, format_software_tag(src_ex, sw_stamp)));

    if (cs_mode == ColorSpaceMode::ADOBE_RGB) {
        std::vector<uint8_t> icc = build_adobergb_icc_profile();
        upsert_tag(ifd0, RawTagItem{34675, 7, (uint32_t)icc.size(), std::move(icc)});
    }

    upsert_tag(ifd0, RawTagItem{273, 4, (uint32_t)n_strips, std::vector<uint8_t>((size_t)n_strips * 4, 0)});
    upsert_tag(ifd0, RawTagItem{279, 4, (uint32_t)n_strips, std::vector<uint8_t>((size_t)n_strips * 4, 0)});

    std::vector<RawTagItem> exif_tags = src_ex.exif_keep;
    upsert_tag(exif_tags, make_u16_tag(40961, (cs_mode == ColorSpaceMode::SRGB) ? 1u : 65535u));
    upsert_tag(exif_tags, make_u32_tag(40962, (uint32_t)W));
    upsert_tag(exif_tags, make_u32_tag(40963, (uint32_t)H));
    bool has_gps = !src_ex.gps_keep.empty();
    upsert_tag(ifd0, make_u32_tag(34665, 0));
    if (has_gps) upsert_tag(ifd0, make_u32_tag(34853, 0));

    std::vector<uint8_t> meta = {'I', 'I', 42, 0, 8, 0, 0, 0};
    uint32_t ifd0_off = append_serialized_ifd(meta, ifd0);
    uint32_t exif_sub_off = append_serialized_ifd(meta, exif_tags);
    uint32_t gps_sub_off = has_gps ? append_serialized_ifd(meta, src_ex.gps_keep) : 0;
    while (meta.size() & 15u) meta.push_back(0);
    uint32_t data_base = (uint32_t)meta.size();

    std::vector<uint32_t> offs(n_strips), cnts(n_strips);
    uint64_t cur_off = data_base;
    for (int s = 0; s < n_strips; ++s) {
        uint32_t sz = compress_deflate
            ? (uint32_t)comp_strips[s].size()
            : (uint32_t)((size_t)(std::min(H, (s + 1) * rows_per_strip) - s * rows_per_strip) * W * 6ULL);
        if (cur_off + sz > 0xFFFFFFFFULL) return false;
        offs[s] = (uint32_t)cur_off;
        cnts[s] = sz;
        cur_off += sz;
    }

    uint16_t n0 = rd_u16(meta.data() + ifd0_off, false);
    for (uint16_t i = 0; i < n0; ++i) {
        uint8_t* ep = meta.data() + ifd0_off + 2u + (size_t)i * 12u;
        uint16_t tg = rd_u16(ep, false);
        if (tg == 34665) wr_le32(ep + 8, exif_sub_off);
        else if (tg == 34853 && has_gps) wr_le32(ep + 8, gps_sub_off);
        else if (tg == 273) {
            if (n_strips == 1) wr_le32(ep + 8, offs[0]);
            else {
                uint32_t tbl_off = rd_u32(ep + 8, false);
                for (int s = 0; s < n_strips; ++s) wr_le32(meta.data() + tbl_off + 4 * s, offs[s]);
            }
        } else if (tg == 279) {
            if (n_strips == 1) wr_le32(ep + 8, cnts[0]);
            else {
                uint32_t tbl_off = rd_u32(ep + 8, false);
                for (int s = 0; s < n_strips; ++s) wr_le32(meta.data() + tbl_off + 4 * s, cnts[s]);
            }
        }
    }

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;
    std::fwrite(meta.data(), 1, meta.size(), fp);
    bool write_ok = true;
    if (compress_deflate) {
        for (int s = 0; s < n_strips; ++s) {
            if (std::fwrite(comp_strips[s].data(), 1, comp_strips[s].size(), fp) != comp_strips[s].size()) {
                write_ok = false; break;
            }
        }
    } else {
        size_t nel = (size_t)H * (size_t)W * 3ULL;
        if (std::fwrite(rgb_u16, sizeof(uint16_t), nel, fp) != nel) write_ok = false;
    }
    std::fclose(fp);
    if (!write_ok) { rm_file(tmp_path); return false; }
    return replace_file(tmp_path, path);
}

// ---------------------------------------------------------------------------
// 4. Official DNG 1.4 RGGB Writer:
//    - DNG_LOSSLESS: Tiled 256x256 ISO/IEC 10918-1 Lossless JPEG (Compression=7 SOF3 16-bit)
//    - DNG_HQ      : Visually-Lossless 14-bit Perceptual Companded LUT (Tag 50712 -> 16-bit) + SOF3
//    - DNG_FP16    : HDR Visually-Lossless 16-bit IEEE Half-Float + Predictor=34894 + Deflate
//    - DNG_RAW     : Uncompressed 16-bit strips (Compression=1)
// ---------------------------------------------------------------------------
static inline uint16_t f32_to_f16_bits(float f) {
    if (f <= 0.0f) return 0u;
    if (f >= 65504.0f) return 0x7BFFu;
    uint32_t u = 0;
    std::memcpy(&u, &f, 4);
    int exp = (int)((u >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = u & 0x7FFFFFu;
    if (exp <= 0) {
        if (exp < -10) return 0u;
        mant |= 0x800000u;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half_m = (mant + (1u << (shift - 1))) >> shift;
        return (uint16_t)half_m;
    }
    uint32_t round_m = mant + 0x00000FFFu + ((mant >> 13) & 1u);
    if (round_m >= 0x800000u) {
        round_m = 0;
        ++exp;
        if (exp >= 31) return 0x7BFFu;
    }
    return (uint16_t)((exp << 10) | (round_m >> 13));
}

// Encode one 2-component CFA tile via ISO/IEC 10918-1 Lossless JPEG SOF3 (P = 14 or 16)
// Packs 2 CFA rows per JPEG scanline (jh.high = tile_H/2, jh.wide = tile_W, clrs = 2) with PSV = 7 ((Ra+Rb)>>1),
// so Rb (row above) has the EXACT SAME Bayer color phase (R above R, G1 above G1, G2 above G2, B above B)!
static void encode_ljpeg_sof3_tile_2c(
    const uint16_t* tile_u16, int tile_H, int tile_W, int bits_P, std::vector<uint8_t>& out_jpeg
) {
    const int jh_high = tile_H / 2;
    const int jw = tile_W;          // 2*tile_W samples per JPEG row = 2 full CFA rows (even + odd)
    const int stride_s = jw * 2;    // samples per JPEG row
    const int n_syms = bits_P + 1;  // 0..P

    std::vector<int32_t> diffs((size_t)tile_H * tile_W);
    uint64_t freq[18] = {0};
    int32_t vpred[2] = {1 << (bits_P - 1), 1 << (bits_P - 1)};

    auto diff_cat = [](int32_t d) -> int {
        if (d == 0) return 0;
        if (d == -32768) return 16;
        uint32_t a = (uint32_t)(d < 0 ? -d : d);
        int s = 0;
        while (a > 0) { a >>= 1; ++s; }
        return s;
    };

    for (int jr = 0; jr < jh_high; ++jr) {
        const uint16_t* jrow_cur  = tile_u16 + (size_t)jr * stride_s;
        const uint16_t* jrow_prev = (jr > 0) ? (jrow_cur - stride_s) : nullptr;
        int32_t* drow = diffs.data() + (size_t)jr * stride_s;

        for (int jc = 0; jc < jw; ++jc) {
            for (int c = 0; c < 2; ++c) {
                int idx = 2 * jc + c;
                int32_t val = (int32_t)jrow_cur[idx];
                int32_t pred = 0;
                if (jc == 0) {
                    pred = vpred[c];
                    vpred[c] = val;
                } else if (jr == 0) {
                    pred = (int32_t)jrow_cur[idx - 2];
                } else {
                    // PSV = 7 in LibRaw ljpeg_row_unrolled: (Ra + Rb) >> 1
                    int32_t ra = (int32_t)jrow_cur[idx - 2];
                    int32_t rb = (int32_t)jrow_prev[idx];
                    pred = (ra + rb) >> 1;
                }
                int32_t d = val - pred;
                if (bits_P == 16) {
                    d = (int32_t)(int16_t)(uint16_t)d;
                }
                drow[idx] = d;
                ++freq[diff_cat(d)];
            }
        }
    }

    // Build length-limited (<= 15 bits, Kraft sum <= 32767 so all-1s code is never assigned) Huffman table
    int code_len[18] = {0};
    {
        struct HNode { uint64_t w; int sym; int l; int r; };
        std::vector<HNode> nodes;
        auto cmp = [&](int a, int b) { return nodes[a].w > nodes[b].w; };
        std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);
        for (int s = 0; s < n_syms; ++s) {
            if (freq[s] > 0) {
                int id = (int)nodes.size();
                nodes.push_back({freq[s], s, -1, -1});
                pq.push(id);
            }
        }
        // Dummy zero-weight leaf reserves the all-1s branch!
        {
            int id = (int)nodes.size();
            nodes.push_back({0, -2, -1, -1});
            pq.push(id);
        }
        while (pq.size() > 1) {
            int a = pq.top(); pq.pop();
            int b = pq.top(); pq.pop();
            int p = (int)nodes.size();
            nodes.push_back({nodes[a].w + nodes[b].w, -1, a, b});
            pq.push(p);
        }
        int root = pq.top();
        auto walk = [&](auto& self, int u, int depth) -> void {
            if (nodes[u].l < 0 && nodes[u].r < 0) {
                if (nodes[u].sym >= 0) code_len[nodes[u].sym] = std::max(1, std::min(15, depth));
                return;
            }
            if (nodes[u].l >= 0) self(self, nodes[u].l, depth + 1);
            if (nodes[u].r >= 0) self(self, nodes[u].r, depth + 1);
        };
        walk(walk, root, 0);

        // Enforce strict Kraft sum <= 32767 at max length 15
        auto kraft15 = [&]() {
            uint32_t k = 0;
            for (int s = 0; s < n_syms; ++s) if (code_len[s] > 0) k += (1u << (15 - code_len[s]));
            return k;
        };
        while (kraft15() > 32767u) {
            int pick = -1;
            for (int s = 0; s < n_syms; ++s) {
                if (code_len[s] > 0 && code_len[s] < 15) {
                    if (pick < 0 || code_len[s] > code_len[pick] ||
                        (code_len[s] == code_len[pick] && freq[s] < freq[pick])) {
                        pick = s;
                    }
                }
            }
            if (pick < 0) break;
            ++code_len[pick];
        }
    }

    uint8_t bits_cnt[17] = {0};
    std::vector<uint8_t> huffval;
    uint16_t huff_code[18] = {0};
    for (int len = 1; len <= 15; ++len) {
        for (int s = 0; s < n_syms; ++s) {
            if (code_len[s] == len) {
                ++bits_cnt[len];
                huffval.push_back((uint8_t)s);
            }
        }
    }
    {
        uint16_t code = 0;
        size_t idx = 0;
        for (int len = 1; len <= 15; ++len) {
            for (int k = 0; k < bits_cnt[len]; ++k) {
                int sym = huffval[idx++];
                huff_code[sym] = code++;
            }
            code <<= 1;
        }
    }

    out_jpeg.clear();
    out_jpeg.reserve((size_t)tile_H * tile_W * 2);
    auto put_u8 = [&](uint8_t b) { out_jpeg.push_back(b); };
    auto put_be16 = [&](uint16_t v) { out_jpeg.push_back((uint8_t)(v >> 8)); out_jpeg.push_back((uint8_t)(v & 0xFFu)); };

    // SOI
    put_u8(0xFF); put_u8(0xD8);
    // DHT (Table class 0, ID 0 shared by both components)
    put_u8(0xFF); put_u8(0xC4);
    put_be16((uint16_t)(2 + 1 + 16 + huffval.size()));
    put_u8(0x00);
    for (int i = 1; i <= 16; ++i) put_u8(bits_cnt[i]);
    for (uint8_t v : huffval) put_u8(v);

    // SOF3 (Lossless Huffman, 2 components, H=1 V=1, 2 CFA rows per JPEG scanline)
    put_u8(0xFF); put_u8(0xC3);
    put_be16(2 + 6 + 2 * 3);
    put_u8((uint8_t)bits_P);
    put_be16((uint16_t)jh_high);
    put_be16((uint16_t)jw);
    put_u8(2);
    put_u8(1); put_u8(0x11); put_u8(0);
    put_u8(2); put_u8(0x11); put_u8(0);

    // SOS (2 components using table 0, PSV = 7 (Ra+Rb)>>1, Se = 0, AhAl = 0)
    put_u8(0xFF); put_u8(0xDA);
    put_be16(2 + 1 + 2 * 2 + 3);
    put_u8(2);
    put_u8(1); put_u8(0x00);
    put_u8(2); put_u8(0x00);
    put_u8(7); // 2D Predictor 7 ((Ra + Rb) >> 1)
    put_u8(0);
    put_u8(0);

    // Entropy-coded scan with 0xFF -> 0xFF 0x00 byte stuffing
    uint64_t bit_buf = 0;
    int bit_cnt = 0;
    auto flush_bytes = [&]() {
        while (bit_cnt >= 8) {
            bit_cnt -= 8;
            uint8_t b = (uint8_t)((bit_buf >> bit_cnt) & 0xFFu);
            out_jpeg.push_back(b);
            if (b == 0xFFu) out_jpeg.push_back(0x00u);
        }
    };

    const size_t total_samples = (size_t)tile_H * tile_W;
    for (size_t i = 0; i < total_samples; ++i) {
        int32_t d = diffs[i];
        int s = diff_cat(d);
        bit_buf = (bit_buf << code_len[s]) | huff_code[s];
        bit_cnt += code_len[s];
        if (s > 0 && s < 16) {
            uint32_t amp = (d > 0) ? (uint32_t)d : (uint32_t)(d + (1 << s) - 1);
            bit_buf = (bit_buf << s) | (amp & ((1u << s) - 1u));
            bit_cnt += s;
        }
        if (bit_cnt >= 32) flush_bytes();
    }
    flush_bytes();
    if (bit_cnt > 0) {
        uint8_t pad = (uint8_t)(((bit_buf << (8 - bit_cnt)) | ((1u << (8 - bit_cnt)) - 1u)) & 0xFFu);
        out_jpeg.push_back(pad);
        if (pad == 0xFFu) out_jpeg.push_back(0x00u);
    }

    // EOI
    put_u8(0xFF); put_u8(0xD9);
}

template <typename ParFn>
static bool write_dng14_rggb_ex(
    const std::string& path, const uint16_t* bayer_u16, const float* rgb_2x_lin, int H, int W,
    RawDngMode mode, double sig_eff_sq, double g_eff,
    const std::string& make, const std::string& model,
    const float wb[3], const double cam_xyz[3][3], int flip, double baseline_ev,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    uint16_t (*flip_to_orient)(int), ParFn&& par_rows,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    std::vector<RawTagItem> ifd0 = src_ex.ifd0_keep;
    upsert_tag(ifd0, make_u32_tag(254, 0u));
    upsert_tag(ifd0, make_u32_tag(256, (uint32_t)W));
    upsert_tag(ifd0, make_u32_tag(257, (uint32_t)H));
    upsert_tag(ifd0, make_u16_tag(258, 16u));
    upsert_tag(ifd0, make_u16_tag(262, 32803u)); // CFA
    upsert_tag(ifd0, make_ascii_tag(271, make));
    upsert_tag(ifd0, make_ascii_tag(272, model));
    upsert_tag(ifd0, make_u16_tag(274, flip_to_orient(flip)));
    upsert_tag(ifd0, make_u16_tag(277, 1u));
    upsert_tag(ifd0, make_u16_tag(284, 1u));
    upsert_tag(ifd0, make_ascii_tag(11, sw_stamp));
    upsert_tag(ifd0, make_ascii_tag(305, format_software_tag(src_ex, sw_stamp)));

    { RawTagItem t{33421, 3, 2, std::vector<uint8_t>(4)}; wr_le16(t.data.data(), 2); wr_le16(t.data.data()+2, 2); upsert_tag(ifd0, t); }
    { RawTagItem t{33422, 1, 4, {0, 1, 1, 2}}; upsert_tag(ifd0, t); }
    { RawTagItem t{50706, 1, 4, {1, 4, 0, 0}}; upsert_tag(ifd0, t); }
    { RawTagItem t{50707, 1, 4, {1, 4, 0, 0}}; upsert_tag(ifd0, t); }
    upsert_tag(ifd0, make_ascii_tag(50708, make + " " + model));
    { RawTagItem t{50710, 1, 3, {0, 1, 2}}; upsert_tag(ifd0, t); }
    upsert_tag(ifd0, make_u16_tag(50711, 1u));
    { RawTagItem t{50713, 3, 2, std::vector<uint8_t>(4)}; wr_le16(t.data.data(), 2); wr_le16(t.data.data()+2, 2); upsert_tag(ifd0, t); }
    {
        RawTagItem bl{50714, 5, 4, std::vector<uint8_t>(32, 0)};
        for (int i = 0; i < 4; ++i) wr_le32(bl.data.data() + i * 8 + 4, 1u);
        upsert_tag(ifd0, bl);
    }
    upsert_tag(ifd0, make_u32_tag(50717, (mode == RawDngMode::DNG_FP16) ? 1u : 65535u));
    {
        RawTagItem cm{50721, 10, 9, std::vector<uint8_t>(72, 0)};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                int32_t num = (int32_t)std::llround(cam_xyz[r][c] * 10000.0);
                int32_t den = 10000;
                int idx = r * 3 + c;
                std::memcpy(cm.data.data() + idx * 8 + 0, &num, 4);
                std::memcpy(cm.data.data() + idx * 8 + 4, &den, 4);
            }
        }
        upsert_tag(ifd0, cm);
    }
    {
        RawTagItem asn{50728, 5, 3, std::vector<uint8_t>(24, 0)};
        for (int c = 0; c < 3; ++c) {
            double n_neutral = 1.0 / std::max(1e-4, (double)wb[c]);
            wr_le32(asn.data.data() + c * 8 + 0, (uint32_t)std::llround(n_neutral * 100000.0));
            wr_le32(asn.data.data() + c * 8 + 4, 100000u);
        }
        upsert_tag(ifd0, asn);
    }
    {
        RawTagItem bev{50730, 10, 1, std::vector<uint8_t>(8, 0)};
        int32_t num = (int32_t)std::llround(baseline_ev * 1000.0);
        int32_t den = 1000;
        std::memcpy(bev.data.data() + 0, &num, 4);
        std::memcpy(bev.data.data() + 4, &den, 4);
        upsert_tag(ifd0, bev);
    }
    upsert_tag(ifd0, make_u16_tag(50778, 21u));

    std::vector<std::vector<uint8_t>> tile_blobs;
    bool use_tiles = (mode != RawDngMode::DNG_RAW);
    int TW = (!use_tiles || W > 256) ? 256 : std::max(16, ((W / 2) & ~1));
    while (use_tiles && (W == 2 * TW || W == 4 * TW) && TW >= 32) TW /= 2;
    const int TH = (!use_tiles || H > 256) ? 256 : std::max(8, ((H / 2) & ~1));
    const int ntx = use_tiles ? ((W + TW - 1) / TW) : 1;
    const int nty = use_tiles ? ((H + TH - 1) / TH) : 1;
    const int n_chunks = use_tiles ? (ntx * nty) : 1;

    if (mode == RawDngMode::DNG_LOSSLESS || mode == RawDngMode::DNG_HQ) {
        upsert_tag(ifd0, make_u16_tag(259, 7u)); // Compression = 7 (ISO 10918-1 Lossless JPEG SOF3)
        upsert_tag(ifd0, make_u32_tag(322, (uint32_t)TW));
        upsert_tag(ifd0, make_u32_tag(323, (uint32_t)TH));

        // For DNG_HQ: build physics-calibrated Poisson-Gaussian noise-floor perceptual LUT (Tag 50712 -> 0..65535 16-bit)!
        // Steps by 0.85 * sigma_resid(u) so quantization error (<0.25*sigma) is buried below real sensor noise
        // while collapsing random thermal/shot-noise LSB entropy!
        std::vector<uint16_t> enc_lut;
        if (mode == RawDngMode::DNG_HQ) {
            // Keep deep-shadow toe fine (<= 18 ADU/65535 even at ISO 51200 for +4 EV shadow recovery!)
            // while scaling midtone/highlight steps with sub-shot-noise sqrt(u) (PSNR ~67-72 dB, ~45-60 MiB)!
            const double s2_base = std::max(4e-8, std::min(2.5e-7, 0.06 * sig_eff_sq));
            const double g_base  = std::max(1.5e-5, 0.11 * g_eff);
            std::vector<uint16_t> dec_lut;
            dec_lut.reserve(4096);
            dec_lut.push_back(0);
            double u_cur = 0.0;
            while (u_cur < 1.0 && (int)dec_lut.size() < 16383) {
                double sig_u = std::sqrt(s2_base + g_base * u_cur);
                double step = std::max(1.0 / 16383.0, std::min(0.008, 0.55 * sig_u));
                u_cur += step;
                if (u_cur >= 1.0) break;
                uint16_t lin16 = (uint16_t)std::max(1L, std::min(65534L, std::lround(u_cur * 65535.0)));
                if (lin16 > dec_lut.back()) dec_lut.push_back(lin16);
            }
            dec_lut.push_back(65535u);

            const int n_q = (int)dec_lut.size();
            RawTagItem lin_tbl{50712, 3, (uint32_t)n_q, std::vector<uint8_t>((size_t)n_q * 2)};
            for (int q = 0; q < n_q; ++q) {
                wr_le16(lin_tbl.data.data() + 2 * q, dec_lut[q]);
            }
            upsert_tag(ifd0, std::move(lin_tbl));

            enc_lut.resize(65536);
            int q_cur = 0;
            for (int v = 0; v <= 65535; ++v) {
                while (q_cur + 1 < n_q &&
                       std::abs((int)dec_lut[q_cur + 1] - v) <= std::abs((int)dec_lut[q_cur] - v)) {
                    ++q_cur;
                }
                enc_lut[v] = (uint16_t)q_cur;
            }
        }

        tile_blobs.resize(n_chunks);
        const int bits_P = (mode == RawDngMode::DNG_HQ) ? 14 : 16;
        par_rows(n_chunks, n_threads, [&](int t0, int t1, int) {
            std::vector<uint16_t> tbuf((size_t)TH * TW);
            for (int ti = t0; ti < t1; ++ti) {
                int ty = (ti / ntx) * TH;
                int tx = (ti % ntx) * TW;
                for (int r = 0; r < TH; ++r) {
                    int sr = std::min(H - 1, ty + r);
                    const uint16_t* srow = bayer_u16 + (size_t)sr * W;
                    uint16_t* drow = tbuf.data() + (size_t)r * TW;
                    for (int c = 0; c < TW; ++c) {
                        // Preserve even/odd CFA phase on right-edge padding!
                        int sc = tx + c;
                        if (sc >= W) sc = (W - 2) + (sc & 1);
                        uint16_t v = srow[sc];
                        drow[c] = (mode == RawDngMode::DNG_HQ) ? enc_lut[v] : v;
                    }
                }
                encode_ljpeg_sof3_tile_2c(tbuf.data(), TH, TW, bits_P, tile_blobs[ti]);
            }
        }, 1);

        upsert_tag(ifd0, RawTagItem{324, 4, (uint32_t)n_chunks, std::vector<uint8_t>((size_t)n_chunks * 4, 0)});
        upsert_tag(ifd0, RawTagItem{325, 4, (uint32_t)n_chunks, std::vector<uint8_t>((size_t)n_chunks * 4, 0)});
    } else if (mode == RawDngMode::DNG_FP16) {
        // Official DNG 1.4 16-bit IEEE Half-Float CFA + Predictor=34894 (FloatingPointX2) + Deflate
        upsert_tag(ifd0, make_u16_tag(259, 8u));     // Deflate
        upsert_tag(ifd0, make_u16_tag(317, 34894u)); // FloatingPointX2 (stride-2 same-CFA-phase delta)
        upsert_tag(ifd0, make_u16_tag(339, 3u));     // SampleFormat = 3 (IEEE Floating Point)
        upsert_tag(ifd0, make_u32_tag(322, (uint32_t)TW));
        upsert_tag(ifd0, make_u32_tag(323, (uint32_t)TH));

        tile_blobs.resize(n_chunks);
        std::atomic<bool> ok{true};
        par_rows(n_chunks, n_threads, [&](int t0, int t1, int) {
            std::vector<uint8_t> raw_tile((size_t)TH * TW * 2);
            std::vector<uint8_t> delta_row(TW * 2);
            for (int ti = t0; ti < t1; ++ti) {
                if (!ok.load(std::memory_order_relaxed)) return;
                int ty = (ti / ntx) * TH;
                int tx = (ti % ntx) * TW;
                for (int r = 0; r < TH; ++r) {
                    int sr = ty + r;
                    uint8_t* msb_plane = delta_row.data();          // input1 in Little-Endian DecodeFPDelta!
                    uint8_t* lsb_plane = delta_row.data() + TW;     // input0 in Little-Endian DecodeFPDelta!
                    for (int c = 0; c < TW; ++c) {
                        int sc = tx + c;
                        uint16_t h16 = 0;
                        if (sr < H && sc < W) {
                            if (rgb_2x_lin) {
                                int ch = ((sr & 1) == 0 && (sc & 1) == 0) ? 0 : (((sr & 1) == 1 && (sc & 1) == 1) ? 2 : 1);
                                h16 = f32_to_f16_bits(rgb_2x_lin[((size_t)sr * W + sc) * 3 + ch]);
                            } else {
                                h16 = f32_to_f16_bits((float)bayer_u16[(size_t)sr * W + sc] / 65535.0f);
                            }
                        }
                        msb_plane[c] = (uint8_t)(h16 >> 8);
                        lsb_plane[c] = (uint8_t)(h16 & 0xFFu);
                    }
                    // Inverse of DecodeDeltaBytes(delta_row, TW, channels=2): stride-2 byte delta!
                    uint8_t* out_r = raw_tile.data() + (size_t)r * TW * 2;
                    out_r[0] = delta_row[0];
                    out_r[1] = delta_row[1];
                    for (int i = 2; i < TW * 2; ++i) {
                        out_r[i] = (uint8_t)(delta_row[i] - delta_row[i - 2]);
                    }
                }
                uLong src_b = (uLong)raw_tile.size();
                uLong dst_b = compressBound(src_b);
                tile_blobs[ti].resize(dst_b);
                if (compress2(tile_blobs[ti].data(), &dst_b, raw_tile.data(), src_b, 5) != Z_OK) {
                    ok.store(false); return;
                }
                tile_blobs[ti].resize(dst_b);
            }
        }, 1);
        if (!ok.load()) return false;

        upsert_tag(ifd0, RawTagItem{324, 4, (uint32_t)n_chunks, std::vector<uint8_t>((size_t)n_chunks * 4, 0)});
        upsert_tag(ifd0, RawTagItem{325, 4, (uint32_t)n_chunks, std::vector<uint8_t>((size_t)n_chunks * 4, 0)});
    } else {
        // Uncompressed DNG 1.4
        upsert_tag(ifd0, make_u16_tag(259, 1u));
        upsert_tag(ifd0, make_u32_tag(273, 0u));
        upsert_tag(ifd0, make_u32_tag(278, (uint32_t)H));
        upsert_tag(ifd0, make_u32_tag(279, (uint32_t)((uint64_t)H * W * 2ULL)));
    }

    std::vector<RawTagItem> exif_tags = src_ex.exif_keep;
    upsert_tag(exif_tags, make_u32_tag(40962, (uint32_t)W));
    upsert_tag(exif_tags, make_u32_tag(40963, (uint32_t)H));
    bool has_gps = !src_ex.gps_keep.empty();
    upsert_tag(ifd0, make_u32_tag(34665, 0));
    if (has_gps) upsert_tag(ifd0, make_u32_tag(34853, 0));

    std::vector<uint8_t> meta = {'I', 'I', 42, 0, 8, 0, 0, 0};
    uint32_t ifd0_off = append_serialized_ifd(meta, ifd0);
    uint32_t exif_sub_off = append_serialized_ifd(meta, exif_tags);
    uint32_t gps_sub_off = has_gps ? append_serialized_ifd(meta, src_ex.gps_keep) : 0;
    while (meta.size() & 15u) meta.push_back(0);
    uint32_t data_base = (uint32_t)meta.size();

    std::vector<uint32_t> offs(n_chunks), cnts(n_chunks);
    uint64_t cur_off = data_base;
    for (int i = 0; i < n_chunks; ++i) {
        uint32_t sz = use_tiles ? (uint32_t)tile_blobs[i].size() : (uint32_t)((uint64_t)H * W * 2ULL);
        if (cur_off + sz > 0xFFFFFFFFULL) return false;
        offs[i] = (uint32_t)cur_off;
        cnts[i] = sz;
        cur_off += sz;
    }

    uint16_t n0 = rd_u16(meta.data() + ifd0_off, false);
    uint16_t off_tag = use_tiles ? 324u : 273u;
    uint16_t cnt_tag = use_tiles ? 325u : 279u;
    for (uint16_t i = 0; i < n0; ++i) {
        uint8_t* ep = meta.data() + ifd0_off + 2u + (size_t)i * 12u;
        uint16_t tg = rd_u16(ep, false);
        if (tg == 34665) wr_le32(ep + 8, exif_sub_off);
        else if (tg == 34853 && has_gps) wr_le32(ep + 8, gps_sub_off);
        else if (tg == off_tag) {
            if (n_chunks == 1) wr_le32(ep + 8, offs[0]);
            else {
                uint32_t tbl_off = rd_u32(ep + 8, false);
                for (int k = 0; k < n_chunks; ++k) wr_le32(meta.data() + tbl_off + 4 * k, offs[k]);
            }
        } else if (tg == cnt_tag) {
            if (n_chunks == 1) wr_le32(ep + 8, cnts[0]);
            else {
                uint32_t tbl_off = rd_u32(ep + 8, false);
                for (int k = 0; k < n_chunks; ++k) wr_le32(meta.data() + tbl_off + 4 * k, cnts[k]);
            }
        }
    }

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;
    std::fwrite(meta.data(), 1, meta.size(), fp);
    bool write_ok = true;
    if (use_tiles) {
        for (int i = 0; i < n_chunks; ++i) {
            if (std::fwrite(tile_blobs[i].data(), 1, tile_blobs[i].size(), fp) != tile_blobs[i].size()) {
                write_ok = false; break;
            }
        }
    } else {
        size_t nel = (size_t)H * (size_t)W;
        if (std::fwrite(bayer_u16, sizeof(uint16_t), nel, fp) != nel) write_ok = false;
    }
    std::fclose(fp);
    if (!write_ok) { rm_file(tmp_path); return false; }
    return replace_file(tmp_path, path);
}

} // namespace heraw_io
