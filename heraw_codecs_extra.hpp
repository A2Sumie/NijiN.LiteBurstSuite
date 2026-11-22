#ifndef HERAW_CODECS_EXTRA_HPP
#define HERAW_CODECS_EXTRA_HPP

// Streaming multi-format RGB & physical-field encoders for nijinLBS fix2:
// JXL (VarDCT d=0.5 & Modular lossless via JxlChunkedFrameInputSource),
// Tiled JP2 (1024x1024 9/7 & 5/3 via opj_write_tile),
// Streaming PSD / auto-PSB (16-bit planar + ICC 0x040F + EXIF 0x0422),
// TIFF-LZW (16-bit Compression=5 + Predictor=2) & TIFF-8 (Deflate + Predictor=2),
// PNG-8, JPEG 4:4:4 Q95 (libjpeg-turbo), WebP VP8L (libwebp + libwebpmux),
// and Class F-A..F-D physical-field companders & multi-precision writers.

#include "heraw_exif_io.hpp"

#ifndef JXL_STATIC_DEFINE
#define JXL_STATIC_DEFINE
#endif
#ifndef JXL_THREADS_STATIC_DEFINE
#define JXL_THREADS_STATIC_DEFINE
#endif
#ifndef JXL_CMS_STATIC_DEFINE
#define JXL_CMS_STATIC_DEFINE
#endif
#ifndef OPJ_STATIC
#define OPJ_STATIC
#endif

#include <jxl/cms.h>
#include <jxl/encode.h>
#include <jxl/resizable_parallel_runner.h>
#include <openjpeg.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <jpeglib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <vector>

namespace heraw_io {

// Standard ICC v2.4 Matrix-TRC profile for sRGB IEC61966-2.1 (D50 PCS Bradford-adapted, 256-entry TRC)
inline const std::vector<uint8_t>& get_srgb_icc_profile() {
    static const std::vector<uint8_t> icc = []() {
        std::vector<uint8_t> b(128 + 4 + 9 * 12, 0);
        auto set_be16 = [](uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); };
        auto set_be32 = [](uint8_t* p, uint32_t v) {
            p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
        };
        auto set_tag4 = [&](size_t off, const char s[4]) { std::memcpy(b.data() + off, s, 4); };
        auto pad4 = [&]() { while (b.size() & 3u) b.push_back(0); };
        auto push_be16 = [&](uint16_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); };
        auto push_be32 = [&](uint32_t v) {
            b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16));
            b.push_back(uint8_t(v >> 8));  b.push_back(uint8_t(v));
        };
        auto push_tag4 = [&](const char s[4]) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t(s[i])); };

        set_be32(b.data() + 8, 0x02400000u);
        set_tag4(12, "mntr"); set_tag4(16, "RGB "); set_tag4(20, "XYZ ");
        set_be16(b.data() + 24, 2026); set_be16(b.data() + 26, 1); set_be16(b.data() + 28, 1);
        set_tag4(36, "acsp");
        set_be32(b.data() + 68, 0x0000F6D6u);
        set_be32(b.data() + 72, 0x00010000u);
        set_be32(b.data() + 76, 0x0000D32Du);
        set_be32(b.data() + 128, 9u);

        int t_idx = 0;
        auto add_entry = [&](const char sig[4], uint32_t off, uint32_t sz) {
            const size_t e = 132u + size_t(t_idx++) * 12u;
            set_tag4(e, sig);
            set_be32(b.data() + e + 4u, off);
            set_be32(b.data() + e + 8u, sz);
        };

        const uint32_t off_desc = uint32_t(b.size());
        constexpr char desc_str[] = "sRGB IEC61966-2.1";
        push_tag4("desc"); push_be32(0); push_be32(uint32_t(sizeof(desc_str)));
        for (char c : desc_str) b.push_back(uint8_t(c));
        b.resize(b.size() + 78, 0);
        pad4();
        const uint32_t sz_desc = uint32_t(b.size()) - off_desc;

        const uint32_t off_cprt = uint32_t(b.size());
        constexpr char cprt_str[] = "Public Domain";
        push_tag4("text"); push_be32(0);
        for (char c : cprt_str) b.push_back(uint8_t(c));
        pad4();
        const uint32_t sz_cprt = uint32_t(b.size()) - off_cprt;

        auto push_xyz = [&](int32_t x, int32_t y, int32_t z) -> uint32_t {
            pad4();
            const uint32_t off = uint32_t(b.size());
            push_tag4("XYZ "); push_be32(0);
            push_be32(uint32_t(x)); push_be32(uint32_t(y)); push_be32(uint32_t(z));
            return off;
        };
        const uint32_t off_wtpt = push_xyz(0x0000F6D6, 0x00010000, 0x0000D32D);
        const uint32_t off_rxyz = push_xyz(0x00006FA2, 0x000038F5, 0x00000390);
        const uint32_t off_gxyz = push_xyz(0x00006299, 0x0000B785, 0x000018DA);
        const uint32_t off_bxyz = push_xyz(0x000024A0, 0x00000F84, 0x0000B6CF);

        pad4();
        const uint32_t off_trc = uint32_t(b.size());
        push_tag4("curv"); push_be32(0); push_be32(256u);
        for (int i = 0; i < 256; ++i) {
            const double s = double(i) / 255.0;
            const double lin = (s <= 0.04045) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
            push_be16(uint16_t(std::clamp(std::lround(lin * 65535.0), 0L, 65535L)));
        }
        const uint32_t sz_trc = uint32_t(b.size()) - off_trc;
        pad4();

        add_entry("desc", off_desc, sz_desc);
        add_entry("cprt", off_cprt, sz_cprt);
        add_entry("wtpt", off_wtpt, 20u);
        add_entry("rXYZ", off_rxyz, 20u);
        add_entry("gXYZ", off_gxyz, 20u);
        add_entry("bXYZ", off_bxyz, 20u);
        add_entry("rTRC", off_trc, sz_trc);
        add_entry("gTRC", off_trc, sz_trc);
        add_entry("bTRC", off_trc, sz_trc);

        set_be32(b.data(), uint32_t(b.size()));
        return b;
    }();
    return icc;
}

inline std::span<const uint8_t> icc_profile_for_cs(ColorSpaceMode cs) {
    if (cs == ColorSpaceMode::ADOBE_RGB) {
        return std::span<const uint8_t>(kAdobeRgbIccProfile.data(), kAdobeRgbIccProfile.size());
    }
    const auto& s = get_srgb_icc_profile();
    return std::span<const uint8_t>(s.data(), s.size());
}

// Endpoint-safe deterministic coordinate-hashed TPDF dither (A16: 0 and 65535 map to exact 0 and 255
// with zero +q/8 black lift, and gray axis shares dither scalar across channels to avoid chroma speckle).
inline double tpdf_dither_at(int y, int x) noexcept {
    uint64_t z1 = (uint64_t(uint32_t(y)) << 32) ^ uint64_t(uint32_t(x)) ^ 0x9E3779B97F4A7C15ULL;
    z1 = (z1 ^ (z1 >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z1 = (z1 ^ (z1 >> 27)) * 0x94D049BB133111EBULL;
    z1 ^= (z1 >> 31);
    uint64_t z2 = z1 * 0xD1342543DE82EF95ULL + 0xA5A5A5A55A5A5A5AULL;
    z2 = (z2 ^ (z2 >> 29)) * 0x94D049BB133111EBULL;
    z2 ^= (z2 >> 32);
    const double u1 = double(z1 >> 11) * (1.0 / 9007199254740992.0);
    const double u2 = double(z2 >> 11) * (1.0 / 9007199254740992.0);
    return u1 - u2;
}

inline uint8_t quantize_u16_to_u8_endpoint_safe(uint16_t v16, double d_tpdf, bool enable_dither) noexcept {
    if (v16 == 0u) return 0u;
    if (v16 == 65535u) return 255u;
    const double scaled = double(v16) * (255.0 / 65535.0) + (enable_dither ? d_tpdf : 0.0);
    return static_cast<uint8_t>(std::clamp(int(std::lround(scaled)), 0, 255));
}

inline void convert_row_u16_to_u8_rgb(const uint16_t* src_u16, uint8_t* dst_u8, int y, int W, bool dither_on) {
    for (int x = 0; x < W; ++x) {
        const double d = dither_on ? tpdf_dither_at(y, x) : 0.0;
        dst_u8[3 * x + 0] = quantize_u16_to_u8_endpoint_safe(src_u16[3 * x + 0], d, dither_on);
        dst_u8[3 * x + 1] = quantize_u16_to_u8_endpoint_safe(src_u16[3 * x + 1], d, dither_on);
        dst_u8[3 * x + 2] = quantize_u16_to_u8_endpoint_safe(src_u16[3 * x + 2], d, dither_on);
    }
}

using RowGenU16 = std::function<void(int r0, int r1, uint16_t* dst)>;

inline void fill_rows_u16(const uint16_t* rgb_u16, const RowGenU16& row_gen, int W, int r0, int r1, uint16_t* dst) {
    if (row_gen) {
        row_gen(r0, r1, dst);
    } else if (rgb_u16) {
        std::memcpy(dst, rgb_u16 + size_t(r0) * W * 3, size_t(r1 - r0) * W * 3 * sizeof(uint16_t));
    }
}

// ---------------------------------------------------------------------------
// 1. Streaming JPEG XL RGB Writer (VarDCT d=0.5 & Modular Lossless, 16-bit)
// ---------------------------------------------------------------------------
namespace detail_jxl {

struct FileOutCtx {
    FILE* fp = nullptr;
    std::vector<uint8_t> buf;
    bool ok = true;
    explicit FileOutCtx(FILE* f) : fp(f), buf(256 * 1024) {}
};

inline void* out_get_buf(void* opaque, size_t* size) {
    auto* c = static_cast<FileOutCtx*>(opaque);
    *size = std::min<size_t>(*size > 0 ? *size : c->buf.size(), c->buf.size());
    return c->buf.data();
}
inline void out_rel_buf(void* opaque, size_t written) {
    auto* c = static_cast<FileOutCtx*>(opaque);
    if (written > 0 && c->ok) {
        if (std::fwrite(c->buf.data(), 1, written, c->fp) != written) c->ok = false;
    }
}
inline void out_seek(void* opaque, uint64_t pos) {
    auto* c = static_cast<FileOutCtx*>(opaque);
    if (c->ok && !seek_file_io(c->fp, pos)) c->ok = false;
}
inline void out_finalized(void*, uint64_t) {}

struct ChunkInCtx {
    const uint16_t* full_rgb = nullptr;
    const RowGenU16* row_gen = nullptr;
    int W = 0, H = 0;
    std::mutex mtx;
};

inline void chunk_get_fmt(void*, JxlPixelFormat* fmt) {
    fmt->num_channels = 3;
    fmt->data_type = JXL_TYPE_UINT16;
    fmt->endianness = JXL_NATIVE_ENDIAN;
    fmt->align = 0;
}
inline const void* chunk_get_color(void* opaque, size_t xpos, size_t ypos, size_t xsize, size_t ysize, size_t* row_offset) {
    auto* c = static_cast<ChunkInCtx*>(opaque);
    *row_offset = xsize * 3 * sizeof(uint16_t);
    auto* buf = new uint16_t[xsize * ysize * 3];
    if (c->full_rgb) {
        for (size_t r = 0; r < ysize; ++r) {
            const uint16_t* sp = c->full_rgb + ((ypos + r) * size_t(c->W) + xpos) * 3;
            std::memcpy(buf + r * xsize * 3, sp, xsize * 3 * sizeof(uint16_t));
        }
    } else if (c->row_gen && *c->row_gen) {
        std::vector<uint16_t> strip(ysize * size_t(c->W) * 3);
        {
            std::lock_guard<std::mutex> lk(c->mtx);
            (*c->row_gen)(int(ypos), int(ypos + ysize), strip.data());
        }
        for (size_t r = 0; r < ysize; ++r) {
            const uint16_t* sp = strip.data() + (r * size_t(c->W) + xpos) * 3;
            std::memcpy(buf + r * xsize * 3, sp, xsize * 3 * sizeof(uint16_t));
        }
    } else {
        std::memset(buf, 0, xsize * ysize * 3 * sizeof(uint16_t));
    }
    return buf;
}
inline void chunk_get_extra_fmt(void*, size_t, JxlPixelFormat*) {}
inline const void* chunk_get_extra(void*, size_t, size_t, size_t, size_t, size_t, size_t*) { return nullptr; }
inline void chunk_release(void*, const void* buf) {
    delete[] static_cast<const uint16_t*>(buf);
}

} // namespace detail_jxl

inline bool write_jxl_rgb_stream(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, bool lossless, float distance, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || (!rgb_u16 && !row_gen)) return false;
    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    JxlEncoder* enc = JxlEncoderCreate(nullptr);
    void* runner = JxlResizableParallelRunnerCreate(nullptr);
    if (!enc || !runner) {
        if (runner) JxlResizableParallelRunnerDestroy(runner);
        if (enc) JxlEncoderDestroy(enc);
        return finish_output(fp, false, tmp_path, path, rm_file, replace_file);
    }
    JxlResizableParallelRunnerSetThreads(runner, size_t(std::clamp(n_threads, 1, 32)));

    bool ok = (JxlEncoderSetParallelRunner(enc, JxlResizableParallelRunner, runner) == JXL_ENC_SUCCESS);
    if (const JxlCmsInterface* cms = JxlGetDefaultCms(); cms && ok) {
        JxlEncoderSetCms(enc, *cms);
    }

    detail_jxl::FileOutCtx out_ctx(fp);
    JxlEncoderOutputProcessor out_proc{};
    out_proc.opaque = &out_ctx;
    out_proc.get_buffer = detail_jxl::out_get_buf;
    out_proc.release_buffer = detail_jxl::out_rel_buf;
    out_proc.seek = detail_jxl::out_seek;
    out_proc.set_finalized_position = detail_jxl::out_finalized;
    if (ok) ok = (JxlEncoderSetOutputProcessor(enc, out_proc) == JXL_ENC_SUCCESS);

    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
    if (ok && !exif_blob.empty()) {
        ok = (JxlEncoderUseBoxes(enc) == JXL_ENC_SUCCESS);
    }

    JxlBasicInfo info{};
    JxlEncoderInitBasicInfo(&info);
    info.xsize = uint32_t(W);
    info.ysize = uint32_t(H);
    info.bits_per_sample = 16;
    info.exponent_bits_per_sample = 0;
    info.num_color_channels = 3;
    info.num_extra_channels = 0;
    info.uses_original_profile = lossless ? JXL_TRUE : JXL_FALSE;
    if (ok) ok = (JxlEncoderSetBasicInfo(enc, &info) == JXL_ENC_SUCCESS);
    if (ok && JxlEncoderGetRequiredCodestreamLevel(enc) == 10) {
        ok = (JxlEncoderSetCodestreamLevel(enc, 10) == JXL_ENC_SUCCESS);
    }

    if (ok) {
        if (cs_mode == ColorSpaceMode::SRGB) {
            JxlColorEncoding ce{};
            JxlColorEncodingSetToSRGB(&ce, JXL_FALSE);
            ok = (JxlEncoderSetColorEncoding(enc, &ce) == JXL_ENC_SUCCESS);
        } else {
            ok = (JxlEncoderSetICCProfile(enc, kAdobeRgbIccProfile.data(), kAdobeRgbIccProfile.size()) == JXL_ENC_SUCCESS);
        }
    }

    if (ok && !exif_blob.empty()) {
        std::vector<uint8_t> jxl_exif(4 + exif_blob.size(), 0);
        std::memcpy(jxl_exif.data() + 4, exif_blob.data(), exif_blob.size());
        ok = (JxlEncoderAddBox(enc, "Exif", jxl_exif.data(), jxl_exif.size(), JXL_FALSE) == JXL_ENC_SUCCESS);
        JxlEncoderCloseBoxes(enc);
    }

    JxlEncoderFrameSettings* fset = ok ? JxlEncoderFrameSettingsCreate(enc, nullptr) : nullptr;
    if (fset) {
        JxlEncoderFrameSettingsSetOption(fset, JXL_ENC_FRAME_SETTING_EFFORT, lossless ? 5 : 6);
        if (lossless) {
            ok = (JxlEncoderSetFrameLossless(fset, JXL_TRUE) == JXL_ENC_SUCCESS);
        } else {
            ok = (JxlEncoderSetFrameDistance(fset, std::clamp(distance, 0.05f, 6.0f)) == JXL_ENC_SUCCESS);
        }
    } else {
        ok = false;
    }

    detail_jxl::ChunkInCtx in_ctx{rgb_u16, &row_gen, W, H, {}};
    JxlChunkedFrameInputSource src{};
    src.opaque = &in_ctx;
    src.get_color_channels_pixel_format = detail_jxl::chunk_get_fmt;
    src.get_color_channel_data_at = detail_jxl::chunk_get_color;
    src.get_extra_channel_pixel_format = detail_jxl::chunk_get_extra_fmt;
    src.get_extra_channel_data_at = detail_jxl::chunk_get_extra;
    src.release_buffer = detail_jxl::chunk_release;

    if (ok) ok = (JxlEncoderAddChunkedFrame(fset, JXL_TRUE, src) == JXL_ENC_SUCCESS);
    if (ok) {
        JxlEncoderCloseInput(enc);
        ok = (JxlEncoderFlushInput(enc) == JXL_ENC_SUCCESS) && out_ctx.ok;
    }

    JxlResizableParallelRunnerDestroy(runner);
    JxlEncoderDestroy(enc);
    return finish_output(fp, ok, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 2. Tiled (1024x1024) JPEG 2000 RGB Writer (OpenJPEG 2.5, < 8 MiB tile RAM)
// ---------------------------------------------------------------------------
namespace detail_opj {

inline OPJ_SIZE_T f_write(void* p_buffer, OPJ_SIZE_T p_nb_bytes, void* p_user_data) {
    FILE* fp = static_cast<FILE*>(p_user_data);
    return std::fwrite(p_buffer, 1, p_nb_bytes, fp);
}
inline OPJ_OFF_T f_skip(OPJ_OFF_T p_nb_bytes, void* p_user_data) {
    FILE* fp = static_cast<FILE*>(p_user_data);
#ifdef _WIN32
    return (_fseeki64(fp, p_nb_bytes, SEEK_CUR) == 0) ? p_nb_bytes : -1;
#else
    return (fseeko(fp, static_cast<off_t>(p_nb_bytes), SEEK_CUR) == 0) ? p_nb_bytes : -1;
#endif
}
inline OPJ_BOOL f_seek(OPJ_OFF_T p_nb_bytes, void* p_user_data) {
    FILE* fp = static_cast<FILE*>(p_user_data);
    return seek_file_io(fp, static_cast<uint64_t>(p_nb_bytes)) ? OPJ_TRUE : OPJ_FALSE;
}
inline void silent_cb(const char*, void*) {}

} // namespace detail_opj

inline bool write_jp2_rgb_tiled(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, bool lossless, float comp_ratio, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || (!rgb_u16 && !row_gen)) return false;
    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    constexpr int TILE_W = 1024;
    constexpr int TILE_H = 1024;

    opj_cparameters_t params{};
    opj_set_default_encoder_parameters(&params);
    params.tcp_numlayers = 1;
    params.cp_disto_alloc = 1;
    params.tcp_mct = 1;
    params.irreversible = lossless ? 0 : 1;
    params.tcp_rates[0] = lossless ? 0.0f : std::clamp(comp_ratio, 2.0f, 40.0f);
    params.tile_size_on = OPJ_TRUE;
    params.cp_tdx = TILE_W;
    params.cp_tdy = TILE_H;
    params.numresolution = ( std::min(W, H) >= 256 ) ? 6 : 3;

    opj_image_cmptparm_t cmpt[3]{};
    for (int c = 0; c < 3; ++c) {
        cmpt[c].dx = 1;
        cmpt[c].dy = 1;
        cmpt[c].w = OPJ_UINT32(W);
        cmpt[c].h = OPJ_UINT32(H);
        cmpt[c].x0 = 0;
        cmpt[c].y0 = 0;
        cmpt[c].prec = 16;
        cmpt[c].sgnd = 0;
    }

    const OPJ_COLOR_SPACE clrspc = (cs_mode == ColorSpaceMode::SRGB) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_UNSPECIFIED;
    opj_image_t* img = opj_image_tile_create(3, cmpt, clrspc);
    if (!img) return finish_output(fp, false, tmp_path, path, rm_file, replace_file);
    img->x0 = 0; img->y0 = 0;
    img->x1 = OPJ_UINT32(W); img->y1 = OPJ_UINT32(H);

    if (cs_mode == ColorSpaceMode::ADOBE_RGB) {
        img->icc_profile_len = OPJ_UINT32(kAdobeRgbIccProfile.size());
        img->icc_profile_buf = static_cast<OPJ_BYTE*>(std::malloc(img->icc_profile_len));
        if (img->icc_profile_buf) {
            std::memcpy(img->icc_profile_buf, kAdobeRgbIccProfile.data(), img->icc_profile_len);
        }
    }

    opj_codec_t* codec = opj_create_compress(OPJ_CODEC_JP2);
    opj_stream_t* stream = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_FALSE);
    bool ok = (codec != nullptr && stream != nullptr);
    if (ok) {
        opj_set_info_handler(codec, detail_opj::silent_cb, nullptr);
        opj_set_warning_handler(codec, detail_opj::silent_cb, nullptr);
        opj_set_error_handler(codec, detail_opj::silent_cb, nullptr);
        opj_codec_set_threads(codec, std::clamp(n_threads, 1, 16));
        opj_stream_set_user_data(stream, fp, nullptr);
        opj_stream_set_write_function(stream, detail_opj::f_write);
        opj_stream_set_skip_function(stream, detail_opj::f_skip);
        opj_stream_set_seek_function(stream, detail_opj::f_seek);
        ok = (opj_setup_encoder(codec, &params, img) == OPJ_TRUE) &&
             (opj_start_compress(codec, img, stream) == OPJ_TRUE);
    }

    const int ntx = (W + TILE_W - 1) / TILE_W;
    const int nty = (H + TILE_H - 1) / TILE_H;
    std::vector<uint16_t> strip_inter;
    std::vector<uint16_t> tile_planar(size_t(TILE_W) * TILE_H * 3);

    for (int ty = 0; ok && ty < nty; ++ty) {
        const int y0 = ty * TILE_H;
        const int th = std::min(TILE_H, H - y0);
        const uint16_t* band_ptr = nullptr;
        if (rgb_u16) {
            band_ptr = rgb_u16 + size_t(y0) * W * 3;
        } else {
            strip_inter.resize(size_t(th) * W * 3);
            row_gen(y0, y0 + th, strip_inter.data());
            band_ptr = strip_inter.data();
        }
        for (int tx = 0; ok && tx < ntx; ++tx) {
            const int x0 = tx * TILE_W;
            const int tw = std::min(TILE_W, W - x0);
            const size_t plane_px = size_t(tw) * th;
            uint16_t* pR = tile_planar.data();
            uint16_t* pG = pR + plane_px;
            uint16_t* pB = pG + plane_px;
            for (int r = 0; r < th; ++r) {
                const uint16_t* sp = band_ptr + (size_t(r) * W + x0) * 3;
                for (int c = 0; c < tw; ++c) {
                    pR[size_t(r) * tw + c] = sp[3 * c + 0];
                    pG[size_t(r) * tw + c] = sp[3 * c + 1];
                    pB[size_t(r) * tw + c] = sp[3 * c + 2];
                }
            }
            const OPJ_UINT32 tile_idx = OPJ_UINT32(ty * ntx + tx);
            const OPJ_UINT32 data_bytes = OPJ_UINT32(plane_px * 3 * sizeof(uint16_t));
            ok = (opj_write_tile(codec, tile_idx, reinterpret_cast<OPJ_BYTE*>(tile_planar.data()), data_bytes, stream) == OPJ_TRUE);
        }
    }

    if (ok) ok = (opj_end_compress(codec, stream) == OPJ_TRUE);
    if (stream) opj_stream_destroy(stream);
    if (codec) opj_destroy_codec(codec);
    opj_image_destroy(img);

    // Append standard ISO/ExifTool JP2 EXIF UUID box (be7acfcb-97a9-42e8-9c71-999491e3afac)
    if (ok) {
        std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
        if (!exif_blob.empty() && exif_blob.size() < UINT32_MAX - 32u) {
            static const uint8_t kExifUuid[16] = {
                0xBE, 0x7A, 0xCF, 0xCB, 0x97, 0xA9, 0x42, 0xE8,
                0x9C, 0x71, 0x99, 0x94, 0x91, 0xE3, 0xAF, 0xAC
            };
#ifdef _WIN32
            _fseeki64(fp, 0, SEEK_END);
#else
            fseeko(fp, 0, SEEK_END);
#endif
            uint8_t bhdr[24];
            wr_be32(bhdr + 0, uint32_t(24u + exif_blob.size()));
            std::memcpy(bhdr + 4, "uuid", 4);
            std::memcpy(bhdr + 8, kExifUuid, 16);
            if (std::fwrite(bhdr, 1, 24, fp) != 24 ||
                std::fwrite(exif_blob.data(), 1, exif_blob.size(), fp) != exif_blob.size()) {
                ok = false;
            }
        }
    }
    return finish_output(fp, ok, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 3. Streaming 16-bit Adobe PSD / auto-PSB Writer (Apple Preview + PS + LR + C1)
// ---------------------------------------------------------------------------
inline bool write_psd_rgb_stream(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || W > 300000 || H > 300000 || (!rgb_u16 && !row_gen)) return false;
    const bool is_psb = (W > 30000 || H > 30000 || (uint64_t(W) * uint64_t(H) * 6ULL > 1900000000ULL));

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;
    std::setvbuf(fp, nullptr, _IOFBF, 4 * 1024 * 1024);

    // 1. 26-byte PSD/PSB File Header
    uint8_t hdr[26] = {0};
    std::memcpy(hdr + 0, "8BPS", 4);
    wr_be16(hdr + 4, is_psb ? 2u : 1u);
    wr_be16(hdr + 12, 3u); // 3 channels (RGB)
    wr_be32(hdr + 14, uint32_t(H));
    wr_be32(hdr + 18, uint32_t(W));
    wr_be16(hdr + 22, 16u); // 16 bits/channel
    wr_be16(hdr + 24, 3u);  // Color mode 3 = RGB
    bool ok = (std::fwrite(hdr, 1, 26, fp) == 26);

    // 2. Color Mode Data (0 bytes)
    uint8_t z4[4] = {0, 0, 0, 0};
    if (ok) ok = (std::fwrite(z4, 1, 4, fp) == 4);

    // 3. Image Resources: 0x03ED (300 PPI), 0x040F (ICC Profile), 0x0422 (EXIF1)
    std::vector<uint8_t> irb;
    auto append_8bim = [&](uint16_t id, std::span<const uint8_t> pay) {
        const size_t base = irb.size();
        irb.resize(base + 12);
        std::memcpy(irb.data() + base, "8BIM", 4);
        wr_be16(irb.data() + base + 4, id);
        irb[base + 6] = 0; irb[base + 7] = 0; // empty Pascal string padded to 2B
        wr_be32(irb.data() + base + 8, uint32_t(pay.size()));
        irb.insert(irb.end(), pay.begin(), pay.end());
        if (pay.size() & 1u) irb.push_back(0);
    };
    {
        uint8_t res_info[16] = {0};
        wr_be32(res_info + 0, 300u << 16); wr_be16(res_info + 4, 1); wr_be16(res_info + 6, 2);
        wr_be32(res_info + 8, 300u << 16); wr_be16(res_info + 12, 1); wr_be16(res_info + 14, 2);
        append_8bim(0x03ED, res_info);
    }
    append_8bim(0x040F, icc_profile_for_cs(cs_mode));
    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
    if (!exif_blob.empty()) append_8bim(0x0422, exif_blob);

    uint8_t irb_len[4];
    wr_be32(irb_len, uint32_t(irb.size()));
    if (ok) ok = (std::fwrite(irb_len, 1, 4, fp) == 4) && (std::fwrite(irb.data(), 1, irb.size(), fp) == irb.size());

    // 4. Layer and Mask Info (0 bytes: 4B length in PSD, 8B length in PSB)
    uint8_t z8[8] = {0};
    if (ok) ok = (std::fwrite(z8, 1, is_psb ? 8 : 4, fp) == size_t(is_psb ? 8 : 4));

    // 5. Image Data Section: Uncompressed Big-Endian Planar RGB (Compression = 0)
    // Wait: Compression = 0 (Raw Big-Endian Planar) is 100% streaming in 1 pass WITHOUT needing channel transpose
    // if rgb_u16 is in RAM, OR if row_gen is used, streaming 3 passes (or buffered strips) writes planar R, G, B!
    // Even better: PackBits RLE (Compression = 1) only works well on 8-bit; on 16-bit photographic data, Photoshop
    // itself writes Raw (0) or ZIP because byte-level PackBits expands noisy 16-bit LSBs! And Raw (0) is universally
    // decoded at maximum speed by Preview, Finder, Photoshop, Lightroom, and Capture One!
    uint8_t comp_be[2];
    wr_be16(comp_be, 0u); // Compression = 0 (Raw Planar Big-Endian)
    if (ok) ok = (std::fwrite(comp_be, 1, 2, fp) == 2);

    constexpr int STRIP_H = 64;
    std::vector<uint16_t> inter_strip;
    std::vector<uint8_t> plane_be(size_t(STRIP_H) * W * 2);

    for (int ch = 0; ok && ch < 3; ++ch) {
        for (int r0 = 0; ok && r0 < H; r0 += STRIP_H) {
            const int r1 = std::min(H, r0 + STRIP_H);
            const int sh = r1 - r0;
            const uint16_t* src_base = nullptr;
            if (rgb_u16) {
                src_base = rgb_u16 + size_t(r0) * W * 3;
            } else {
                inter_strip.resize(size_t(sh) * W * 3);
                row_gen(r0, r1, inter_strip.data());
                src_base = inter_strip.data();
            }
            for (int r = 0; r < sh; ++r) {
                const uint16_t* sp = src_base + size_t(r) * W * 3 + ch;
                uint8_t* dp = plane_be.data() + size_t(r) * W * 2;
                for (int x = 0; x < W; ++x) {
                    const uint16_t v = sp[3 * x];
                    dp[2 * x + 0] = uint8_t(v >> 8);
                    dp[2 * x + 1] = uint8_t(v & 0xFFu);
                }
            }
            const size_t nb = size_t(sh) * W * 2;
            if (std::fwrite(plane_be.data(), 1, nb, fp) != nb) ok = false;
        }
    }
    return finish_output(fp, ok, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 4. TIFF 6.0 MSB-first LZW Encoder + Parallel TIFF-LZW (16b) & TIFF-8 (8b)
// ---------------------------------------------------------------------------
inline void encode_tiff_lzw_strip(const uint8_t* src, size_t len, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(len / 2 + 64);
    constexpr int CLEAR_CODE = 256;
    constexpr int EOI_CODE = 257;
    constexpr int FIRST_CODE = 258;
    // Open-addressed hash table (size 8192 = power of 2, > 2 * 4096)
    std::array<int32_t, 8192> h_key;
    std::array<uint16_t, 8192> h_val{};
    h_key.fill(-1);

    uint32_t bit_buf = 0;
    int bit_cnt = 0;
    auto put_code = [&](int code, int width) {
        bit_buf = (bit_buf << width) | uint32_t(code);
        bit_cnt += width;
        while (bit_cnt >= 8) {
            bit_cnt -= 8;
            out.push_back(uint8_t((bit_buf >> bit_cnt) & 0xFFu));
        }
    };

    int next_code = FIRST_CODE;
    int code_width = 9;
    put_code(CLEAR_CODE, code_width);
    if (len == 0) {
        put_code(EOI_CODE, code_width);
        if (bit_cnt > 0) out.push_back(uint8_t((bit_buf << (8 - bit_cnt)) & 0xFFu));
        return;
    }

    int omega = src[0];
    for (size_t i = 1; i < len; ++i) {
        const uint8_t k = src[i];
        const int32_t key = (omega << 8) | int32_t(k);
        uint32_t slot = (uint32_t(key) * 2654435761u) & 8191u;
        bool found = false;
        while (h_key[slot] != -1) {
            if (h_key[slot] == key) {
                omega = h_val[slot];
                found = true;
                break;
            }
            slot = (slot + 1u) & 8191u;
        }
        if (!found) {
            put_code(omega, code_width);
            h_key[slot] = key;
            h_val[slot] = uint16_t(next_code++);
            // TIFF 6.0 "early change" bit-width transitions at 511, 1023, 2047, reset at 4094
            if (next_code == 512) code_width = 10;
            else if (next_code == 1024) code_width = 11;
            else if (next_code == 2048) code_width = 12;
            else if (next_code >= 4094) {
                put_code(CLEAR_CODE, code_width);
                h_key.fill(-1);
                next_code = FIRST_CODE;
                code_width = 9;
            }
            omega = k;
        }
    }
    put_code(omega, code_width);
    if (next_code == 511) code_width = 10;
    else if (next_code == 1023) code_width = 11;
    else if (next_code == 2047) code_width = 12;
    put_code(EOI_CODE, code_width);
    if (bit_cnt > 0) out.push_back(uint8_t((bit_buf << (8 - bit_cnt)) & 0xFFu));
}

template <typename ParFn>
inline bool write_tiff_lzw_or_8_rgb(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, bool use_lzw_16, bool dither_8, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    ParFn&& par_rows, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || (!rgb_u16 && !row_gen)) return false;
    constexpr int rows_per_strip = 32;
    const int n_strips = (H + rows_per_strip - 1) / rows_per_strip;
    std::vector<std::vector<uint8_t>> strips(static_cast<size_t>(n_strips));
    std::atomic<bool> ok{true};

    par_rows(n_strips, n_threads, [&](int s0, int s1, int) {
        std::vector<uint16_t> src_buf(static_cast<size_t>(rows_per_strip) * W * 3);
        std::vector<uint8_t> pred_bytes(static_cast<size_t>(rows_per_strip) * W * 3 * (use_lzw_16 ? 2 : 1));
        for (int s = s0; s < s1; ++s) {
            if (!ok.load(std::memory_order_relaxed)) return;
            const int r0 = s * rows_per_strip;
            const int r1 = std::min(H, r0 + rows_per_strip);
            const int sh = r1 - r0;
            fill_rows_u16(rgb_u16, row_gen, W, r0, r1, src_buf.data());

            if (use_lzw_16) {
                auto* pred16 = reinterpret_cast<uint16_t*>(pred_bytes.data());
                for (int r = 0; r < sh; ++r) {
                    const uint16_t* sp = src_buf.data() + size_t(r) * W * 3;
                    uint16_t* dp = pred16 + size_t(r) * W * 3;
                    dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
                    for (int c = 3; c < W * 3; ++c) dp[c] = uint16_t(sp[c] - sp[c - 3]);
                }
                encode_tiff_lzw_strip(pred_bytes.data(), size_t(sh) * W * 6, strips[size_t(s)]);
            } else {
                std::vector<uint8_t> row8(static_cast<size_t>(W) * 3);
                for (int r = 0; r < sh; ++r) {
                    const uint16_t* sp = src_buf.data() + size_t(r) * W * 3;
                    convert_row_u16_to_u8_rgb(sp, row8.data(), r0 + r, W, dither_8);
                    uint8_t* dp = pred_bytes.data() + size_t(r) * W * 3;
                    dp[0] = row8[0]; dp[1] = row8[1]; dp[2] = row8[2];
                    for (int c = 3; c < W * 3; ++c) dp[c] = uint8_t(row8[c] - row8[c - 3]);
                }
                const uLong src_b = uLong(size_t(sh) * W * 3);
                uLong dst_b = compressBound(src_b);
                strips[size_t(s)].resize(dst_b);
                if (compress2(strips[size_t(s)].data(), &dst_b, pred_bytes.data(), src_b, 5) != Z_OK) {
                    ok.store(false);
                    return;
                }
                strips[size_t(s)].resize(dst_b);
            }
        }
    }, 1);
    if (!ok.load()) return false;

    std::vector<RawTagItem> ifd0 = src_ex.ifd0_keep;
    upsert_tag(ifd0, make_u32_tag(256, uint32_t(W)));
    upsert_tag(ifd0, make_u32_tag(257, uint32_t(H)));
    {
        RawTagItem bps{258, 3, 3, std::vector<uint8_t>(6)};
        const uint16_t bval = use_lzw_16 ? 16u : 8u;
        wr_le16(bps.data.data() + 0, bval); wr_le16(bps.data.data() + 2, bval); wr_le16(bps.data.data() + 4, bval);
        upsert_tag(ifd0, std::move(bps));
    }
    upsert_tag(ifd0, make_u16_tag(259, use_lzw_16 ? 5u : 8u)); // 5 = LZW, 8 = Deflate
    upsert_tag(ifd0, make_u16_tag(262, 2u));
    upsert_tag(ifd0, make_u16_tag(274, 1u));
    upsert_tag(ifd0, make_u16_tag(277, 3u));
    upsert_tag(ifd0, make_u32_tag(278, uint32_t(rows_per_strip)));
    upsert_tag(ifd0, make_u16_tag(284, 1u));
    upsert_tag(ifd0, make_u16_tag(317, 2u)); // Predictor = 2
    upsert_software_tags(ifd0, src_ex, sw_stamp);
    {
        const auto icc = icc_profile_for_cs(cs_mode);
        upsert_tag(ifd0, RawTagItem{34675, 7, uint32_t(icc.size()), std::vector<uint8_t>(icc.begin(), icc.end())});
    }
    upsert_tag(ifd0, RawTagItem{273, 4, uint32_t(n_strips), std::vector<uint8_t>(size_t(n_strips) * 4, 0)});
    upsert_tag(ifd0, RawTagItem{279, 4, uint32_t(n_strips), std::vector<uint8_t>(size_t(n_strips) * 4, 0)});

    std::vector<RawTagItem> exif_tags = src_ex.exif_keep;
    upsert_tag(exif_tags, make_u16_tag(40961, (cs_mode == ColorSpaceMode::SRGB) ? 1u : 65535u));
    upsert_tag(exif_tags, make_u32_tag(40962, uint32_t(W)));
    upsert_tag(exif_tags, make_u32_tag(40963, uint32_t(H)));
    const bool has_gps = !src_ex.gps_keep.empty();
    upsert_tag(ifd0, make_u32_tag(34665, 0));
    if (has_gps) upsert_tag(ifd0, make_u32_tag(34853, 0));

    std::vector<uint8_t> meta = {'I', 'I', 42, 0, 8, 0, 0, 0};
    const uint32_t ifd0_off = append_serialized_ifd(meta, ifd0);
    const uint32_t exif_sub_off = append_serialized_ifd(meta, exif_tags);
    const uint32_t gps_sub_off = has_gps ? append_serialized_ifd(meta, src_ex.gps_keep) : 0;
    if (!ifd0_off || !exif_sub_off || (has_gps && !gps_sub_off)) return false;
    while (meta.size() & 15u) meta.push_back(0);

    std::vector<uint32_t> offs(static_cast<size_t>(n_strips)), cnts(static_cast<size_t>(n_strips));
    uint64_t cur_off = meta.size();
    for (int s = 0; s < n_strips; ++s) {
        const uint64_t sz = strips[size_t(s)].size();
        if (cur_off + sz > UINT32_MAX) return false;
        offs[size_t(s)] = uint32_t(cur_off);
        cnts[size_t(s)] = uint32_t(sz);
        cur_off += sz;
    }

    const uint16_t n0 = rd_u16(meta.data() + ifd0_off, false);
    for (uint16_t i = 0; i < n0; ++i) {
        uint8_t* ep = meta.data() + ifd0_off + 2u + size_t(i) * 12u;
        const uint16_t tg = rd_u16(ep, false);
        if (tg == 34665) wr_le32(ep + 8, exif_sub_off);
        else if (tg == 34853 && has_gps) wr_le32(ep + 8, gps_sub_off);
        else if (tg == 273) {
            if (n_strips == 1) wr_le32(ep + 8, offs[0]);
            else {
                const uint32_t tbl = rd_u32(ep + 8, false);
                for (int s = 0; s < n_strips; ++s) wr_le32(meta.data() + tbl + 4 * s, offs[size_t(s)]);
            }
        } else if (tg == 279) {
            if (n_strips == 1) wr_le32(ep + 8, cnts[0]);
            else {
                const uint32_t tbl = rd_u32(ep + 8, false);
                for (int s = 0; s < n_strips; ++s) wr_le32(meta.data() + tbl + 4 * s, cnts[size_t(s)]);
            }
        }
    }

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;
    bool wok = (std::fwrite(meta.data(), 1, meta.size(), fp) == meta.size());
    for (int s = 0; wok && s < n_strips; ++s) {
        if (std::fwrite(strips[size_t(s)].data(), 1, strips[size_t(s)].size(), fp) != strips[size_t(s)].size()) wok = false;
    }
    return finish_output(fp, wok, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 5. Parallel 8-bit PNG RGB & Grayscale Writers
// ---------------------------------------------------------------------------
template <typename ParFn>
inline bool write_png8_rgb(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, bool dither_8, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp, int n_threads,
    ParFn&& par_rows, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || (!rgb_u16 && !row_gen)) return false;
    constexpr int rows_per_strip = 64;
    const int n_strips = (H + rows_per_strip - 1) / rows_per_strip;
    const size_t row_raw_bytes = 1 + size_t(W) * 3;
    std::vector<std::vector<uint8_t>> comp_strips(static_cast<size_t>(n_strips));
    std::vector<uLong> strip_adlers(static_cast<size_t>(n_strips), 1L);
    std::vector<z_off_t> strip_raw_lens(static_cast<size_t>(n_strips), 0);
    std::atomic<bool> enc_ok{true};

    par_rows(n_strips, n_threads, [&](int s0, int s1, int) {
        std::vector<uint16_t> src16(static_cast<size_t>(rows_per_strip) * W * 3);
        std::vector<uint8_t> raw_buf(static_cast<size_t>(rows_per_strip) * row_raw_bytes);
        std::vector<uint8_t> row8(static_cast<size_t>(W) * 3);
        for (int s = s0; s < s1; ++s) {
            if (!enc_ok.load(std::memory_order_relaxed)) return;
            const int r_beg = s * rows_per_strip;
            const int r_end = std::min(H, r_beg + rows_per_strip);
            const int sh = r_end - r_beg;
            fill_rows_u16(rgb_u16, row_gen, W, r_beg, r_end, src16.data());
            const size_t total_in = size_t(sh) * row_raw_bytes;

            for (int r = 0; r < sh; ++r) {
                convert_row_u16_to_u8_rgb(src16.data() + size_t(r) * W * 3, row8.data(), r_beg + r, W, dither_8);
                uint8_t* dst = raw_buf.data() + size_t(r) * row_raw_bytes;
                dst[0] = 1; // Sub predictor (bpp = 3)
                dst[1] = row8[0]; dst[2] = row8[1]; dst[3] = row8[2];
                for (size_t i = 3; i < size_t(W) * 3; ++i) dst[1 + i] = uint8_t(row8[i] - row8[i - 3]);
            }
            strip_adlers[size_t(s)] = adler32(1L, raw_buf.data(), uInt(total_in));
            strip_raw_lens[size_t(s)] = z_off_t(total_in);

            z_stream zs{};
            if (deflateInit2(&zs, 5, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) { enc_ok.store(false); return; }
            const size_t max_out = deflateBound(&zs, uLong(total_in)) + 64;
            comp_strips[size_t(s)].resize(max_out);
            zs.next_in = raw_buf.data(); zs.avail_in = uInt(total_in);
            zs.next_out = comp_strips[size_t(s)].data(); zs.avail_out = uInt(max_out);
            const int flush_mode = (s == n_strips - 1) ? Z_FINISH : Z_SYNC_FLUSH;
            const int zrc = deflate(&zs, flush_mode);
            if ((flush_mode == Z_FINISH && zrc != Z_STREAM_END) || (flush_mode != Z_FINISH && zrc != Z_OK)) {
                deflateEnd(&zs); enc_ok.store(false); return;
            }
            comp_strips[size_t(s)].resize(zs.total_out);
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
    wr_be32(ihdr + 0, uint32_t(W)); wr_be32(ihdr + 4, uint32_t(H));
    ihdr[8] = 8; ihdr[9] = 2;
    write_png_chunk(fp, "IHDR", ihdr);

    if (cs_mode == ColorSpaceMode::SRGB) {
        const uint8_t srgb_intent = 0;
        write_png_chunk(fp, "sRGB", {&srgb_intent, 1});
    } else {
        uLong zlen = compressBound(uLong(kAdobeRgbIccProfile.size()));
        std::vector<uint8_t> zicc(zlen);
        if (compress2(zicc.data(), &zlen, kAdobeRgbIccProfile.data(), uLong(kAdobeRgbIccProfile.size()), 6) == Z_OK) {
            const char* pname = "Adobe RGB (1998)";
            std::vector<uint8_t> iccp_pay(pname, pname + std::strlen(pname) + 1);
            iccp_pay.push_back(0);
            iccp_pay.insert(iccp_pay.end(), zicc.begin(), zicc.begin() + zlen);
            write_png_chunk(fp, "iCCP", iccp_pay);
        }
    }
    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
    if (!exif_blob.empty()) write_png_chunk(fp, "eXIf", exif_blob);

    uint8_t zhdr[2] = {0x78, 0x5E};
    write_png_chunk(fp, "IDAT", zhdr);
    uLong comb_adler = 1L;
    for (int s = 0; s < n_strips; ++s) {
        comb_adler = (s == 0) ? strip_adlers[0] : adler32_combine(comb_adler, strip_adlers[size_t(s)], strip_raw_lens[size_t(s)]);
        if (!comp_strips[size_t(s)].empty()) write_png_chunk(fp, "IDAT", comp_strips[size_t(s)]);
    }
    uint8_t ztrl[4]; wr_be32(ztrl, uint32_t(comb_adler));
    write_png_chunk(fp, "IDAT", ztrl);
    write_png_chunk(fp, "IEND");
    return finish_output(fp, true, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 6. Streaming JPEG 4:4:4 Q95 Writer (libjpeg-turbo + ICC APP2 + Exif APP1)
// ---------------------------------------------------------------------------
inline bool write_jpeg_444_rgb(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, int quality, bool dither_8, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || W > 65500 || H > 65500 || (!rgb_u16 && !row_gen)) return false;
    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    jpeg_compress_struct cinfo{};
    jpeg_error_mgr jerr{};
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width = JDIMENSION(W);
    cinfo.image_height = JDIMENSION(H);
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(quality, 60, 100), TRUE);
    cinfo.optimize_coding = TRUE;
    // Enforce strict 4:4:4 (1x1 sampling on Y, Cb, Cr) so fine color edges never blur
    cinfo.comp_info[0].h_samp_factor = 1; cinfo.comp_info[0].v_samp_factor = 1;
    cinfo.comp_info[1].h_samp_factor = 1; cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1; cinfo.comp_info[2].v_samp_factor = 1;

    jpeg_start_compress(&cinfo, TRUE);

    // APP1 Exif\0\0
    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
    if (!exif_blob.empty() && exif_blob.size() <= 65527u) {
        std::vector<uint8_t> app1(6 + exif_blob.size());
        std::memcpy(app1.data(), "Exif\0\0", 6);
        std::memcpy(app1.data() + 6, exif_blob.data(), exif_blob.size());
        jpeg_write_marker(&cinfo, JPEG_APP0 + 1, app1.data(), static_cast<unsigned int>(app1.size()));
    }
    // APP2 ICC_PROFILE\0
    {
        const auto icc = icc_profile_for_cs(cs_mode);
        constexpr size_t MAX_CHUNK = 65519;
        const size_t n_chunks = (icc.size() + MAX_CHUNK - 1) / MAX_CHUNK;
        for (size_t ci = 0; ci < n_chunks; ++ci) {
            const size_t off = ci * MAX_CHUNK;
            const size_t sz = std::min(MAX_CHUNK, icc.size() - off);
            std::vector<uint8_t> app2(14 + sz);
            std::memcpy(app2.data(), "ICC_PROFILE\0", 12);
            app2[12] = uint8_t(ci + 1);
            app2[13] = uint8_t(n_chunks);
            std::memcpy(app2.data() + 14, icc.data() + off, sz);
            jpeg_write_marker(&cinfo, JPEG_APP0 + 2, app2.data(), static_cast<unsigned int>(app2.size()));
        }
    }

    constexpr int STRIP_H = 32;
    std::vector<uint16_t> src16(static_cast<size_t>(STRIP_H) * W * 3);
    std::vector<uint8_t> row8(static_cast<size_t>(W) * 3);
    for (int r0 = 0; r0 < H; r0 += STRIP_H) {
        const int r1 = std::min(H, r0 + STRIP_H);
        fill_rows_u16(rgb_u16, row_gen, W, r0, r1, src16.data());
        for (int r = r0; r < r1; ++r) {
            convert_row_u16_to_u8_rgb(src16.data() + size_t(r - r0) * W * 3, row8.data(), r, W, dither_8);
            JSAMPROW rp[1] = {row8.data()};
            jpeg_write_scanlines(&cinfo, rp, 1);
        }
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return finish_output(fp, true, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 7. Lossless VP8L WebP Writer (libwebp + libwebpmux RIFF ICCP + EXIF)
// ---------------------------------------------------------------------------
inline bool write_webp_rgb(
    const std::string& path, const uint16_t* rgb_u16, const RowGenU16& row_gen,
    int H, int W, bool dither_8, ColorSpaceMode cs_mode,
    const ExifBundle& src_ex, const std::string& sw_stamp,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || W > 16383 || H > 16383 || (!rgb_u16 && !row_gen)) return false;
    std::vector<uint8_t> rgb8(static_cast<size_t>(W) * H * 3);
    constexpr int STRIP_H = 64;
    std::vector<uint16_t> src16(static_cast<size_t>(STRIP_H) * W * 3);
    for (int r0 = 0; r0 < H; r0 += STRIP_H) {
        const int r1 = std::min(H, r0 + STRIP_H);
        fill_rows_u16(rgb_u16, row_gen, W, r0, r1, src16.data());
        for (int r = r0; r < r1; ++r) {
            convert_row_u16_to_u8_rgb(
                src16.data() + size_t(r - r0) * W * 3,
                rgb8.data() + size_t(r) * W * 3, r, W, dither_8);
        }
    }

    WebPConfig cfg{};
    if (!WebPConfigInit(&cfg) || !WebPConfigLosslessPreset(&cfg, 5)) return false;
    cfg.exact = 1;
    cfg.thread_level = 1;

    WebPPicture pic{};
    if (!WebPPictureInit(&pic)) return false;
    pic.use_argb = 1;
    pic.width = W;
    pic.height = H;
    WebPMemoryWriter mw{};
    WebPMemoryWriterInit(&mw);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &mw;

    bool ok = (WebPPictureImportRGB(&pic, rgb8.data(), W * 3) != 0) && (WebPEncode(&cfg, &pic) != 0);
    WebPPictureFree(&pic);
    if (!ok) { WebPMemoryWriterClear(&mw); return false; }

    WebPMux* mux = WebPMuxNew();
    WebPData out_mux_data{nullptr, 0};
    if (mux) {
        WebPData img_d{mw.mem, mw.size};
        ok = (WebPMuxSetImage(mux, &img_d, 0) == WEBP_MUX_OK);
        const auto icc = icc_profile_for_cs(cs_mode);
        WebPData icc_d{icc.data(), icc.size()};
        if (ok) ok = (WebPMuxSetChunk(mux, "ICCP", &icc_d, 0) == WEBP_MUX_OK);
        std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, cs_mode, sw_stamp);
        if (ok && !exif_blob.empty()) {
            WebPData ex_d{exif_blob.data(), exif_blob.size()};
            ok = (WebPMuxSetChunk(mux, "EXIF", &ex_d, 1) == WEBP_MUX_OK);
        }
        if (ok) ok = (WebPMuxAssemble(mux, &out_mux_data) == WEBP_MUX_OK);
        WebPMuxDelete(mux);
    } else {
        ok = false;
    }
    WebPMemoryWriterClear(&mw);
    if (!ok || !out_mux_data.bytes) { WebPDataClear(&out_mux_data); return false; }

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) { WebPDataClear(&out_mux_data); return false; }
    const bool wok = (std::fwrite(out_mux_data.bytes, 1, out_mux_data.size, fp) == out_mux_data.size);
    WebPDataClear(&out_mux_data);
    return finish_output(fp, wok, tmp_path, path, rm_file, replace_file);
}

// ---------------------------------------------------------------------------
// 8. Physical-Field Mathematical Companders & Multi-Precision Writers (A15)
// ---------------------------------------------------------------------------
enum class FieldFormatChoice : uint8_t { AUTO = 0, PNG, TIFF, JXL, EXR, WEBP, NIB };
enum class FieldPrecisionChoice : uint8_t { AUTO = 0, U8, U16, F16, F32, LOG16, OCT32 };
enum class FieldScaleChoice : uint8_t { NATIVE = 0, SCALE_1X = 1, SCALE_2X = 2 };

// Signed symmetric u16 with exact zero at 32768 and 0 reserved for unobservable/NaN
inline uint16_t encode_signed_u16_z32768(float v, float scale_abs) noexcept {
    if (!std::isfinite(v)) return 0u;
    if (!(scale_abs > 0.0f)) return 32768u;
    const double q = std::clamp(double(v) / double(scale_abs), -1.0, 1.0);
    const long c = 32768L + std::lround(32767.0 * q);
    return static_cast<uint16_t>(std::clamp(c, 1L, 65535L));
}

// Signed 8-bit compander with exact zero at 128, finite [-scale, +scale] -> [1, 255], and NaN -> 0
inline uint8_t encode_signed_u8_z128(float v, float scale_abs) noexcept {
    if (!std::isfinite(v)) return 0u;
    if (!(scale_abs > 0.0f)) return 128u;
    const double q = std::clamp(double(v) / double(scale_abs), -1.0, 1.0);
    const long c = 128L + std::lround(127.0 * q);
    return static_cast<uint8_t>(std::clamp(c, 1L, 255L));
}

// Globally optimal A15 log1p-u16 compander with mu = S / x0 (<= 0.025% relative error across 7 decades)
inline uint16_t encode_log1p_u16(float v, float max_scale, float x0_floor) noexcept {
    if (!std::isfinite(v) || v <= 0.0f || !(max_scale > 0.0f)) return 0u;
    const double S = double(max_scale);
    const double x0 = std::max(1e-9, std::min(0.1 * S, double(x0_floor)));
    const double mu = std::max(1.0, S / x0);
    const double xc = std::min(S, double(v));
    const double k = 65535.0 * std::log1p(mu * xc / S) / std::log1p(mu);
    return static_cast<uint16_t>(std::clamp(std::lround(k), 0L, 65535L));
}

// Unit-normalized 16-bit-per-axis octahedral normal encoding (<= 0.00371 deg angular error, (0,0) = NaN)
inline std::pair<uint16_t, uint16_t> encode_slope_oct32(float slope_nx, float slope_ny) noexcept {
    if (!std::isfinite(slope_nx) || !std::isfinite(slope_ny)) return {0u, 0u};
    const double inv_len = 1.0 / std::sqrt(1.0 + double(slope_nx) * slope_nx + double(slope_ny) * slope_ny);
    const double nx = double(slope_nx) * inv_len;
    const double ny = double(slope_ny) * inv_len;
    const double nz = 1.0 * inv_len; // >= 0 (upper hemisphere for heightfield (-gx, -gy, 1))
    const double inv_l1 = 1.0 / std::max(1e-15, std::abs(nx) + std::abs(ny) + std::abs(nz));
    double px = nx * inv_l1;
    double py = ny * inv_l1;
    if (nz < 0.0) {
        const double ox = (1.0 - std::abs(py)) * (px >= 0.0 ? 1.0 : -1.0);
        const double oy = (1.0 - std::abs(px)) * (py >= 0.0 ? 1.0 : -1.0);
        px = ox; py = oy;
    }
    const auto q16 = [](double u) -> uint16_t {
        const long c = 32768L + std::lround(32767.0 * std::clamp(u, -1.0, 1.0));
        return static_cast<uint16_t>(std::clamp(c, 1L, 65535L));
    };
    return {q16(px), q16(py)};
}

// Write 8-bit single-channel grayscale lossless PNG
template <typename RowFnU8>
inline bool write_png8_gray_stream(
    const std::string& path, int H, int W, RowFnU8&& fill_row_u8,
    const ExifBundle& src_ex, const std::string& sw_stamp,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0) return false;
    const size_t row_raw = 1 + size_t(W);
    std::vector<uint8_t> raw_strip(64 * row_raw);
    std::vector<uint8_t> row_cur(static_cast<size_t>(W));
    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;
    static const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    std::fwrite(png_sig, 1, 8, fp);
    uint8_t ihdr[13] = {0};
    wr_be32(ihdr + 0, uint32_t(W)); wr_be32(ihdr + 4, uint32_t(H));
    ihdr[8] = 8; ihdr[9] = 0;
    write_png_chunk(fp, "IHDR", ihdr);
    uint8_t gama_be[4]; wr_be32(gama_be, 100000u);
    write_png_chunk(fp, "gAMA", gama_be);
    std::vector<uint8_t> exif_blob = build_standalone_exif_blob(src_ex, W, H, 1, ColorSpaceMode::ADOBE_RGB, sw_stamp);
    if (!exif_blob.empty()) write_png_chunk(fp, "eXIf", exif_blob);

    z_stream zs{};
    if (deflateInit(&zs, 5) != Z_OK) return finish_output(fp, false, tmp_path, path, rm_file, replace_file);
    std::vector<uint8_t> zout(128 * 1024);
    bool ok = true;
    for (int r0 = 0; ok && r0 < H; r0 += 64) {
        const int r1 = std::min(H, r0 + 64);
        const int sh = r1 - r0;
        for (int r = 0; r < sh; ++r) {
            fill_row_u8(r0 + r, row_cur.data());
            uint8_t* dp = raw_strip.data() + size_t(r) * row_raw;
            dp[0] = 1; dp[1] = row_cur[0];
            for (int x = 1; x < W; ++x) dp[1 + x] = uint8_t(row_cur[size_t(x)] - row_cur[size_t(x - 1)]);
        }
        zs.next_in = raw_strip.data();
        zs.avail_in = uInt(size_t(sh) * row_raw);
        const int flush = (r1 == H) ? Z_FINISH : Z_NO_FLUSH;
        do {
            zs.next_out = zout.data();
            zs.avail_out = uInt(zout.size());
            const int rc = deflate(&zs, flush);
            if (rc == Z_STREAM_ERROR) { ok = false; break; }
            const size_t have = zout.size() - zs.avail_out;
            if (have > 0) write_png_chunk(fp, "IDAT", std::span<const uint8_t>(zout.data(), have));
        } while (zs.avail_out == 0);
    }
    deflateEnd(&zs);
    write_png_chunk(fp, "IEND");
    return finish_output(fp, ok, tmp_path, path, rm_file, replace_file);
}

// Write 1-channel or 2-channel Deflate TIFF for scalar/pair physical fields (u8, u16, IEEE f16, IEEE f32)
template <typename FillBytesFn>
inline bool write_tiff_field_stream(
    const std::string& path, int H, int W, int spp, int bits_per_sample, uint16_t sample_format,
    FillBytesFn&& fill_row_bytes, const ExifBundle& src_ex, const std::string& sw_stamp,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || spp < 1 || spp > 4) return false;
    const size_t bytes_per_sample = size_t(bits_per_sample / 8);
    const size_t row_bytes = size_t(W) * size_t(spp) * bytes_per_sample;
    constexpr int rows_per_strip = 32;
    const int n_strips = (H + rows_per_strip - 1) / rows_per_strip;
    std::vector<std::vector<uint8_t>> strips(static_cast<size_t>(n_strips));
    std::vector<uint8_t> raw_strip(static_cast<size_t>(rows_per_strip) * row_bytes);

    for (int s = 0; s < n_strips; ++s) {
        const int r0 = s * rows_per_strip;
        const int r1 = std::min(H, r0 + rows_per_strip);
        const int sh = r1 - r0;
        for (int r = 0; r < sh; ++r) {
            fill_row_bytes(r0 + r, raw_strip.data() + size_t(r) * row_bytes);
        }
        const uLong sb = uLong(size_t(sh) * row_bytes);
        uLong db = compressBound(sb);
        strips[size_t(s)].resize(db);
        if (compress2(strips[size_t(s)].data(), &db, raw_strip.data(), sb, 5) != Z_OK) return false;
        strips[size_t(s)].resize(db);
    }

    std::vector<RawTagItem> ifd0 = src_ex.ifd0_keep;
    upsert_tag(ifd0, make_u32_tag(256, uint32_t(W)));
    upsert_tag(ifd0, make_u32_tag(257, uint32_t(H)));
    if (spp == 1) {
        upsert_tag(ifd0, make_u16_tag(258, uint16_t(bits_per_sample)));
    } else {
        RawTagItem bps{258, 3, uint32_t(spp), std::vector<uint8_t>(size_t(spp) * 2)};
        for (int c = 0; c < spp; ++c) wr_le16(bps.data.data() + 2 * c, uint16_t(bits_per_sample));
        upsert_tag(ifd0, std::move(bps));
    }
    upsert_tag(ifd0, make_u16_tag(259, 8u)); // Deflate
    upsert_tag(ifd0, make_u16_tag(262, 1u)); // BlackIsZero
    upsert_tag(ifd0, make_u16_tag(274, 1u));
    upsert_tag(ifd0, make_u16_tag(277, uint16_t(spp)));
    upsert_tag(ifd0, make_u32_tag(278, uint32_t(rows_per_strip)));
    upsert_tag(ifd0, make_u16_tag(284, 1u));
    if (spp > 1) {
        RawTagItem es{338, 3, uint32_t(spp - 1), std::vector<uint8_t>(size_t(spp - 1) * 2, 0)};
        upsert_tag(ifd0, std::move(es));
    }
    if (spp == 1) {
        upsert_tag(ifd0, make_u16_tag(339, sample_format));
    } else {
        RawTagItem sf{339, 3, uint32_t(spp), std::vector<uint8_t>(size_t(spp) * 2)};
        for (int c = 0; c < spp; ++c) wr_le16(sf.data.data() + 2 * c, sample_format);
        upsert_tag(ifd0, std::move(sf));
    }
    upsert_software_tags(ifd0, src_ex, sw_stamp);
    upsert_tag(ifd0, RawTagItem{273, 4, uint32_t(n_strips), std::vector<uint8_t>(size_t(n_strips) * 4, 0)});
    upsert_tag(ifd0, RawTagItem{279, 4, uint32_t(n_strips), std::vector<uint8_t>(size_t(n_strips) * 4, 0)});

    std::vector<uint8_t> meta = {'I', 'I', 42, 0, 8, 0, 0, 0};
    const uint32_t ifd0_off = append_serialized_ifd(meta, ifd0);
    if (!ifd0_off) return false;
    while (meta.size() & 15u) meta.push_back(0);

    std::vector<uint32_t> offs(static_cast<size_t>(n_strips)), cnts(static_cast<size_t>(n_strips));
    uint64_t cur = meta.size();
    for (int s = 0; s < n_strips; ++s) {
        offs[size_t(s)] = uint32_t(cur);
        cnts[size_t(s)] = uint32_t(strips[size_t(s)].size());
        cur += strips[size_t(s)].size();
    }
    const uint16_t n0 = rd_u16(meta.data() + ifd0_off, false);
    for (uint16_t i = 0; i < n0; ++i) {
        uint8_t* ep = meta.data() + ifd0_off + 2u + size_t(i) * 12u;
        const uint16_t tg = rd_u16(ep, false);
        if (tg == 273) {
            if (n_strips == 1) wr_le32(ep + 8, offs[0]);
            else {
                const uint32_t tbl = rd_u32(ep + 8, false);
                for (int s = 0; s < n_strips; ++s) wr_le32(meta.data() + tbl + 4 * s, offs[size_t(s)]);
            }
        } else if (tg == 279) {
            if (n_strips == 1) wr_le32(ep + 8, cnts[0]);
            else {
                const uint32_t tbl = rd_u32(ep + 8, false);
                for (int s = 0; s < n_strips; ++s) wr_le32(meta.data() + tbl + 4 * s, cnts[size_t(s)]);
            }
        }
    }

    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;
    bool wok = (std::fwrite(meta.data(), 1, meta.size(), fp) == meta.size());
    for (int s = 0; wok && s < n_strips; ++s) {
        if (std::fwrite(strips[size_t(s)].data(), 1, strips[size_t(s)].size(), fp) != strips[size_t(s)].size()) wok = false;
    }
    return finish_output(fp, wok, tmp_path, path, rm_file, replace_file);
}

// Write single-channel scalar Modular Lossless JXL (u8, u16, or IEEE f16; no XYB RGB transform!)
inline bool write_jxl_field_gray(
    const std::string& path, const void* plane_bytes, int H, int W, int bits_per_sample, bool is_float16,
    int n_threads, FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || !plane_bytes) return false;
    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) return false;

    JxlEncoder* enc = JxlEncoderCreate(nullptr);
    void* runner = JxlResizableParallelRunnerCreate(nullptr);
    if (!enc || !runner) {
        if (runner) JxlResizableParallelRunnerDestroy(runner);
        if (enc) JxlEncoderDestroy(enc);
        return finish_output(fp, false, tmp_path, path, rm_file, replace_file);
    }
    JxlResizableParallelRunnerSetThreads(runner, size_t(std::clamp(n_threads, 1, 16)));
    bool ok = (JxlEncoderSetParallelRunner(enc, JxlResizableParallelRunner, runner) == JXL_ENC_SUCCESS);

    detail_jxl::FileOutCtx out_ctx(fp);
    JxlEncoderOutputProcessor out_proc{};
    out_proc.opaque = &out_ctx;
    out_proc.get_buffer = detail_jxl::out_get_buf;
    out_proc.release_buffer = detail_jxl::out_rel_buf;
    out_proc.seek = detail_jxl::out_seek;
    out_proc.set_finalized_position = detail_jxl::out_finalized;
    if (ok) ok = (JxlEncoderSetOutputProcessor(enc, out_proc) == JXL_ENC_SUCCESS);

    JxlBasicInfo info{};
    JxlEncoderInitBasicInfo(&info);
    info.xsize = uint32_t(W);
    info.ysize = uint32_t(H);
    info.bits_per_sample = uint32_t(bits_per_sample);
    info.exponent_bits_per_sample = is_float16 ? 5u : 0u;
    info.num_color_channels = 1;
    info.num_extra_channels = 0;
    info.uses_original_profile = JXL_TRUE;
    if (ok) ok = (JxlEncoderSetBasicInfo(enc, &info) == JXL_ENC_SUCCESS);
    if (ok && JxlEncoderGetRequiredCodestreamLevel(enc) == 10) {
        ok = (JxlEncoderSetCodestreamLevel(enc, 10) == JXL_ENC_SUCCESS);
    }
    if (ok) {
        JxlColorEncoding ce{};
        JxlColorEncodingSetToLinearSRGB(&ce, JXL_TRUE);
        ok = (JxlEncoderSetColorEncoding(enc, &ce) == JXL_ENC_SUCCESS);
    }

    JxlEncoderFrameSettings* fset = ok ? JxlEncoderFrameSettingsCreate(enc, nullptr) : nullptr;
    if (fset) {
        JxlEncoderFrameSettingsSetOption(fset, JXL_ENC_FRAME_SETTING_EFFORT, 5);
        JxlEncoderFrameSettingsSetOption(fset, JXL_ENC_FRAME_SETTING_MODULAR, 1);
        ok = (JxlEncoderSetFrameLossless(fset, JXL_TRUE) == JXL_ENC_SUCCESS);
    } else {
        ok = false;
    }

    JxlPixelFormat fmt{};
    fmt.num_channels = 1;
    fmt.data_type = is_float16 ? JXL_TYPE_FLOAT16 : ((bits_per_sample == 8) ? JXL_TYPE_UINT8 : JXL_TYPE_UINT16);
    fmt.endianness = JXL_NATIVE_ENDIAN;
    fmt.align = 0;
    const size_t total_bytes = size_t(W) * H * size_t(bits_per_sample / 8);
    if (ok) ok = (JxlEncoderAddImageFrame(fset, &fmt, plane_bytes, total_bytes) == JXL_ENC_SUCCESS);
    if (ok) {
        JxlEncoderCloseInput(enc);
        ok = (JxlEncoderFlushInput(enc) == JXL_ENC_SUCCESS) && out_ctx.ok;
    }
    JxlResizableParallelRunnerDestroy(runner);
    JxlEncoderDestroy(enc);
    return finish_output(fp, ok, tmp_path, path, rm_file, replace_file);
}

// Write 8-bit grayscale lossless VP8L WebP for Class F-A preview masks
template <typename RowFnU8>
inline bool write_webp_field_gray(
    const std::string& path, int H, int W, RowFnU8&& fill_row_u8,
    FILE* (*open_wb)(const std::string&),
    void (*rm_file)(const std::string&), bool (*replace_file)(const std::string&, const std::string&)
) {
    if (W <= 0 || H <= 0 || W > 16383 || H > 16383) return false;
    std::vector<uint8_t> rgb8(static_cast<size_t>(W) * H * 3);
    std::vector<uint8_t> row(static_cast<size_t>(W));
    for (int r = 0; r < H; ++r) {
        fill_row_u8(r, row.data());
        uint8_t* dp = rgb8.data() + size_t(r) * W * 3;
        for (int x = 0; x < W; ++x) {
            dp[3 * x + 0] = dp[3 * x + 1] = dp[3 * x + 2] = row[size_t(x)];
        }
    }
    uint8_t* out_mem = nullptr;
    const size_t sz = WebPEncodeLosslessRGB(rgb8.data(), W, H, W * 3, &out_mem);
    if (sz == 0 || !out_mem) { if (out_mem) WebPFree(out_mem); return false; }
    std::string tmp_path = path + ".tmp";
    FILE* fp = open_wb(tmp_path);
    if (!fp) { WebPFree(out_mem); return false; }
    const bool wok = (std::fwrite(out_mem, 1, sz, fp) == sz);
    WebPFree(out_mem);
    return finish_output(fp, wok, tmp_path, path, rm_file, replace_file);
}

} // namespace heraw_io

#endif // HERAW_CODECS_EXTRA_HPP
