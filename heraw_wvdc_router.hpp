// heraw_wvdc_router.hpp -- Decoder routing and container adapters for nijinWVDC
// (.NEV N-RAW, Nikon TicoRAW HE/HE*, raw .jxs/.A000, Sony ARW6). Pure 7-bit ASCII.
#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef HERAW_HAS_WVDC
#include "nijinWVDC.h"
#endif

namespace heraw_wvdc {

enum class RawDecoderPref {
    AUTO = 0,
    WVDC = 1,
    LIBRAW = 2
};

inline FILE* open_rb_utf8(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) return nullptr;
    int wn = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wn <= 0) return nullptr;
    std::wstring wp(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], wn);
    return _wfopen(wp.c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

inline std::string lower_ext_of(const std::string& path) {
    size_t sl = path.find_last_of("/\\");
    std::string b = (sl == std::string::npos) ? path : path.substr(sl + 1);
    size_t dot = b.find_last_of('.');
    if (dot == std::string::npos) return std::string();
    std::string ext = b.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

inline bool split_virtual_raw_path(
    const std::string& vpath,
    std::string& base_path_out,
    uint32_t& frame_index_out) {
    static const char kTag[] = "#frame=";
    size_t pos = vpath.rfind(kTag);
    if (pos != std::string::npos) {
        std::string idx_s = vpath.substr(pos + sizeof(kTag) - 1);
        char* endp = nullptr;
        unsigned long val = std::strtoul(idx_s.c_str(), &endp, 10);
        if (!idx_s.empty() && endp && *endp == '\0') {
            base_path_out = vpath.substr(0, pos);
            frame_index_out = static_cast<uint32_t>(val);
            return true;
        }
    }
    base_path_out = vpath;
    frame_index_out = 0u;
    return false;
}

inline bool is_nev_path(const std::string& path) {
    std::string base;
    uint32_t fidx = 0u;
    split_virtual_raw_path(path, base, fidx);
    return lower_ext_of(base) == ".nev";
}

// Distinguish bare TicoRAW/ARW6 codestreams (.jxs starting with 0xFF 0x10 or "A000")
// from ISO-BMFF rendered RGB .jxs outputs (which start with "'ftyp'").
inline bool is_bayer_codestream_magic(const std::string& path) {
    std::string base;
    uint32_t fidx = 0u;
    split_virtual_raw_path(path, base, fidx);
    FILE* fp = open_rb_utf8(base);
    if (!fp) return false;
    uint8_t h[8] = {};
    size_t got = std::fread(h, 1, sizeof(h), fp);
    std::fclose(fp);
    if (got < 4) return false;
    if (h[0] == 0xFFu && h[1] == 0x10u) return true;
    if (std::memcmp(h, "A000", 4) == 0) return true;
    return false;
}

inline int exif_orientation_to_libraw_flip(uint16_t orient) {
    switch (orient) {
        case 3: return 3;
        case 6: return 6;
        case 8: return 5;
        default: return 0;
    }
}

inline int64_t parse_exif_datetime_utc_sec(const std::string& dt) {
    int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0, sc = 0;
    int64_t base_ts = 1700000000LL;
    if (std::sscanf(dt.c_str(), "%d:%d:%d %d:%d:%d", &yr, &mo, &dy, &hr, &mn, &sc) == 6 &&
        yr >= 1980 && yr <= 2100 && mo >= 1 && mo <= 12 && dy >= 1 && dy <= 31) {
        std::tm tmv{};
        tmv.tm_year = yr - 1900;
        tmv.tm_mon = mo - 1;
        tmv.tm_mday = dy;
        tmv.tm_hour = hr;
        tmv.tm_min = mn;
        tmv.tm_sec = sc;
        tmv.tm_isdst = 0;
#ifdef _WIN32
        std::time_t tt = _mkgmtime(&tmv);
#else
        std::time_t tt = timegm(&tmv);
#endif
        if (tt > 0) base_ts = static_cast<int64_t>(tt);
    }
    return base_ts;
}

inline int64_t parse_exif_datetime_utc(const std::string& dt, double pts_offset_s, int& out_subsec_ms) {
    const int64_t base_ts = parse_exif_datetime_utc_sec(dt);
    double total = static_cast<double>(base_ts) + std::max(0.0, pts_offset_s);
    int64_t sec_part = static_cast<int64_t>(std::floor(total));
    int ms = static_cast<int>(std::lround((total - static_cast<double>(sec_part)) * 1000.0));
    if (ms >= 1000) {
        sec_part += ms / 1000;
        ms %= 1000;
    }
    out_subsec_ms = ms;
    return sec_part;
}

#ifdef HERAW_HAS_WVDC

class MediaReaderCache {
public:
    static std::shared_ptr<const nijinwvdc::MediaReader> open_cached(
        const std::string& base_path,
        std::string& err) {
        static MediaReaderCache g_cache;
        return g_cache.get_or_open(base_path, err);
    }

private:
    struct Entry {
        std::string path;
        std::shared_ptr<const nijinwvdc::MediaReader> reader;
    };
    std::mutex mtx_;
    std::vector<Entry> lru_;

    std::shared_ptr<const nijinwvdc::MediaReader> get_or_open(
        const std::string& base_path,
        std::string& err) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (size_t i = 0; i < lru_.size(); ++i) {
                if (lru_[i].path == base_path) {
                    auto r = lru_[i].reader;
                    if (i > 0) {
                        Entry e = lru_[i];
                        lru_.erase(lru_.begin() + i);
                        lru_.insert(lru_.begin(), std::move(e));
                    }
                    return r;
                }
            }
        }

        auto nr = std::make_shared<nijinwvdc::MediaReader>();
        if (!nr->open_file(base_path, err)) {
            return nullptr;
        }
        std::shared_ptr<const nijinwvdc::MediaReader> cr = nr;
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& e : lru_) {
            if (e.path == base_path) return e.reader;
        }
        lru_.insert(lru_.begin(), Entry{base_path, cr});
        if (lru_.size() > 6u) {
            lru_.pop_back();
        }
        return cr;
    }
};

inline bool parse_nev_frame_spec(
    const std::string& spec,
    uint32_t total_frames,
    std::vector<uint32_t>& out_indices,
    std::string& err) {
    out_indices.clear();
    if (total_frames == 0u) {
        err = "NEV clip contains 0 video frames";
        return false;
    }
    std::string s = spec.empty() ? "0-7" : spec;
    if (s == "all" || s == "ALL") {
        out_indices.reserve(total_frames);
        for (uint32_t i = 0u; i < total_frames; ++i) out_indices.push_back(i);
        return true;
    }
    size_t start = 0;
    while (start < s.size()) {
        size_t comma = s.find(',', start);
        std::string tok = (comma == std::string::npos) ? s.substr(start) : s.substr(start, comma - start);
        if (tok.empty()) {
            err = "Empty token in --nev-frames specification: " + s;
            return false;
        }
        size_t dash = tok.find('-');
        if (dash == std::string::npos) {
            char* endp = nullptr;
            long v = std::strtol(tok.c_str(), &endp, 10);
            if (!endp || *endp != '\0' || v < 0) {
                err = "Invalid frame index in --nev-frames: " + tok;
                return false;
            }
            if (static_cast<uint32_t>(v) < total_frames) {
                out_indices.push_back(static_cast<uint32_t>(v));
            }
        } else {
            std::string sa = tok.substr(0, dash);
            std::string sb = tok.substr(dash + 1);
            char* e1 = nullptr;
            char* e2 = nullptr;
            long a = std::strtol(sa.c_str(), &e1, 10);
            long b = std::strtol(sb.c_str(), &e2, 10);
            if (sa.empty() || sb.empty() || !e1 || *e1 != '\0' || !e2 || *e2 != '\0' || a < 0 || b < a) {
                err = "Invalid frame range in --nev-frames: " + tok;
                return false;
            }
            uint32_t ua = static_cast<uint32_t>(a);
            uint32_t ub = std::min<uint32_t>(static_cast<uint32_t>(b), total_frames - 1u);
            for (uint32_t k = ua; k <= ub; ++k) {
                out_indices.push_back(k);
            }
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    if (out_indices.empty()) {
        err = "No frames selected within clip bounds 0.." + std::to_string(total_frames - 1u);
        return false;
    }
    return true;
}

inline bool expand_nev_clip_frames(
    const std::string& path,
    const std::string& nev_frame_spec,
    std::vector<std::string>& out_virtual_paths,
    std::string& err) {
    std::string base;
    uint32_t req_f = 0u;
    if (split_virtual_raw_path(path, base, req_f)) {
        out_virtual_paths.push_back(path);
        return true;
    }
    auto reader = MediaReaderCache::open_cached(base, err);
    if (!reader) return false;
    const uint32_t nframes = reader->frame_count();
    std::vector<uint32_t> idxs;
    if (!parse_nev_frame_spec(nev_frame_spec, nframes, idxs, err)) return false;
    for (uint32_t fi : idxs) {
        out_virtual_paths.push_back(base + "#frame=" + std::to_string(fi));
    }
    return true;
}

inline bool can_decode_with_wvdc(const std::string& vpath) {
    std::string base;
    uint32_t fidx = 0u;
    split_virtual_raw_path(vpath, base, fidx);
    const std::string ext = lower_ext_of(base);
    if (ext == ".nev") {
        std::string err;
        auto r = MediaReaderCache::open_cached(base, err);
        return r != nullptr && r->codec_family() == nijinwvdc::CodecFamily::TicoRaw;
    }
    if (ext == ".jxs") {
        return is_bayer_codestream_magic(base);
    }
    if (ext == ".nef" || ext == ".nrw") {
        std::string err;
        auto r = MediaReaderCache::open_cached(base, err);
        return r != nullptr && r->codec_family() == nijinwvdc::CodecFamily::TicoRaw;
    }
    if (ext == ".arw") {
        std::string err;
        auto r = MediaReaderCache::open_cached(base, err);
        return r != nullptr && r->codec_family() == nijinwvdc::CodecFamily::Arw6;
    }
    return false;
}

struct WvdcHeaderProbe {
    bool ok = false;
    std::string make;
    std::string model;
    int width = 0;
    int height = 0;
    int64_t timestamp = 0;
    int subsec_ms = 0;
    uint32_t shot_order = 0;
    float iso = 0.0f;
    float shutter = 0.0f;
    float aperture = 0.0f;
    float focal_len = 0.0f;
    float focus_distance_m = 0.0f;
    bool focus_is_infinity = false;
};

inline bool probe_header_via_wvdc(const std::string& vpath, WvdcHeaderProbe& out) {
    out = WvdcHeaderProbe();
    std::string base;
    uint32_t fidx = 0u;
    split_virtual_raw_path(vpath, base, fidx);
    const std::string ext = lower_ext_of(base);
    if (ext != ".nev" && ext != ".jxs" && ext != ".nef" && ext != ".nrw" && ext != ".arw") {
        return false;
    }
    if (ext == ".jxs" && !is_bayer_codestream_magic(base)) {
        return false;
    }
    std::string err;
    auto reader = MediaReaderCache::open_cached(base, err);
    if (!reader) return false;
    const auto cf = reader->codec_family();
    if (cf != nijinwvdc::CodecFamily::TicoRaw && cf != nijinwvdc::CodecFamily::Arw6) {
        return false;
    }
    const uint32_t nf = reader->frame_count();
    if (fidx >= nf) return false;
    nijinwvdc::NevFrameMeta meta;
    if (!reader->read_frame_packet(fidx, meta, nullptr, err)) return false;
    const auto& clip = reader->clip_info();
    out.make = !clip.make.empty() ? clip.make : (cf == nijinwvdc::CodecFamily::Arw6 ? "SONY" : "NIKON CORPORATION");
    out.model = !clip.model.empty() ? clip.model : (cf == nijinwvdc::CodecFamily::Arw6 ? "ARW6" : "N-RAW");
    if (meta.has_crop && meta.crop[2] >= 64u && meta.crop[3] >= 64u) {
        out.width = static_cast<int>(meta.crop[2]) & ~1;
        out.height = static_cast<int>(meta.crop[3]) & ~1;
    } else {
        out.width = static_cast<int>(meta.width) & ~1;
        out.height = static_cast<int>(meta.height) & ~1;
    }
    out.iso = static_cast<float>(meta.iso);
    out.shutter = meta.exposure_time_s;
    out.aperture = meta.fnumber;
    out.focal_len = meta.focal_length_mm;
    out.focus_distance_m = meta.focus_distance_m;
    out.focus_is_infinity = meta.focus_is_infinity;
    out.shot_order = fidx + 1u;

    nijinwvdc::VideoFrameTiming timing;
    std::string terr;
    double pts_s = 0.0;
    if (reader->get_frame_timing(fidx, timing, terr)) {
        pts_s = timing.pts_seconds;
    }
    if (!clip.datetime.empty() || ext == ".nev") {
        out.timestamp = parse_exif_datetime_utc(clip.datetime, pts_s, out.subsec_ms);
    }
    out.ok = (out.width >= 64 && out.height >= 64);
    return out.ok;
}

template <typename UFrame, typename EnrichFn>
inline bool unpack_frame_via_wvdc(
    UFrame& uf,
    int inner_threads,
    EnrichFn&& enrich_libraw_header) {
    std::string base;
    uint32_t fidx = 0u;
    split_virtual_raw_path(uf.path, base, fidx);
    const std::string ext = lower_ext_of(base);
    if (ext == ".jxs" && !is_bayer_codestream_magic(base)) {
        uf.status = -12;
        uf.err_msg = "Input .jxs is not a raw Bayer TicoRAW/ARW6 codestream";
        return false;
    }

    std::string err;
    auto reader = MediaReaderCache::open_cached(base, err);
    if (!reader) {
        uf.status = -2;
        uf.err_msg = err.empty() ? "nijinWVDC failed opening container" : err;
        return false;
    }

    static thread_local nijinwvdc::DecoderSession tls_session(1);
    nijinwvdc::DecodeOptions opts;
    opts.num_threads = std::max(1, inner_threads);
    opts.apply_active_crop = true;
    opts.want_quant_profile = true;

    std::vector<uint16_t> dec_bayer;
    nijinwvdc::DecodedFrameInfo dec_info;
    if (!tls_session.decode_frame(*reader, fidx, opts, dec_bayer, dec_info, err)) {
        uf.status = -4;
        uf.err_msg = err.empty() ? "nijinWVDC decode_frame failed" : err;
        return false;
    }

    const int raw_w = static_cast<int>(dec_info.output_width);
    const int raw_h = static_cast<int>(dec_info.output_height);
    if (raw_w < 64 || raw_h < 64) {
        uf.status = -11;
        uf.err_msg = "Invalid decoded dimensions from nijinWVDC";
        return false;
    }

    // Find (dy_r, dx_r) in {0,1}^2 so output (0,0) aligns onto Red (cfa == 0).
    int dy_r = 0, dx_r = 0;
    const uint8_t* dcfa = dec_info.cfa_pattern;
    bool found_r = false;
    for (int dr = 0; dr < 2 && !found_r; ++dr) {
        for (int dc = 0; dc < 2 && !found_r; ++dc) {
            uint8_t c00 = dcfa[(dr * 2) + dc];
            uint8_t c01 = dcfa[(dr * 2) + (dc ^ 1)];
            uint8_t c10 = dcfa[((dr ^ 1) * 2) + dc];
            uint8_t c11 = dcfa[((dr ^ 1) * 2) + (dc ^ 1)];
            if (c00 == 0u && c01 == 1u && c10 == 1u && c11 == 2u) {
                dy_r = dr;
                dx_r = dc;
                found_r = true;
            }
        }
    }

    const int W = (raw_w - dx_r) & ~1;
    const int H = (raw_h - dy_r) & ~1;
    if (W < 64 || H < 64) {
        uf.status = -11;
        uf.err_msg = "Invalid Bayer dimensions after RGGB parity shift";
        return false;
    }

    uf.W = W;
    uf.H = H;
    uf.crop_left = static_cast<int>(dec_info.offset_x) + dx_r;
    uf.crop_top  = static_cast<int>(dec_info.offset_y) + dy_r;
    uf.is_xtrans = false;
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            uf.cfa6x6[r][c] = ((r & 1) == 0) ? ((c & 1) == 0 ? 0u : 1u)
                                             : ((c & 1) == 0 ? 1u : 2u);
        }
    }

    if (dy_r == 0 && dx_r == 0 && W == raw_w && H == raw_h) {
        uf.bayer.swap(dec_bayer);
    } else {
        uf.bayer.resize(static_cast<size_t>(H) * static_cast<size_t>(W));
        for (int r = 0; r < H; ++r) {
            std::memcpy(
                uf.bayer.data() + static_cast<size_t>(r) * static_cast<size_t>(W),
                dec_bayer.data() + static_cast<size_t>(r + dy_r) * static_cast<size_t>(raw_w) + dx_r,
                static_cast<size_t>(W) * sizeof(uint16_t));
        }
    }

    const auto& clip = reader->clip_info();
    const auto& meta = dec_info.has_meta ? dec_info.meta : nijinwvdc::NevFrameMeta();
    nijinwvdc::DngWriteOptions dng_opts;
    if (dec_info.codec == nijinwvdc::CodecFamily::Arw6) {
        nijinwvdc::Arw6Info arw6;
        arw6.width = dec_info.coded_width;
        arw6.height = dec_info.coded_height;
        arw6.bit_depth = dec_info.bit_depth;
        nijinwvdc::build_dng_options_from_clip_and_frame(clip, meta, arw6, true, 0, dng_opts);
    } else {
        nijinwvdc::TicoRawInfo jxs;
        jxs.width = static_cast<uint16_t>(dec_info.coded_width);
        jxs.height = static_cast<uint16_t>(dec_info.coded_height);
        const uint16_t raw_bd = (dec_info.quant_profile.available && dec_info.quant_profile.raw_bit_depth > 0u)
                                    ? dec_info.quant_profile.raw_bit_depth
                                    : 14u;
        jxs.in_bitdepth = static_cast<int>(raw_bd);
        jxs.out_bitdepth = static_cast<int>(dec_info.bit_depth);
        jxs.cdt_depth = static_cast<int>(raw_bd);
        nijinwvdc::build_dng_options_from_clip_and_frame(clip, meta, jxs, true, 0, dng_opts);
        if (meta.white_level == 0u) {
            dng_opts.white = static_cast<uint16_t>((1u << raw_bd) - 1u);
        }
    }

    uf.make = !dng_opts.make.empty() ? dng_opts.make : "Camera";
    uf.model = !dng_opts.model.empty() ? dng_opts.model : "Model";
    uf.flip = exif_orientation_to_libraw_flip(dng_opts.orientation);
    uf.white = static_cast<float>(std::max<uint16_t>(1023u, dng_opts.white));

    if (meta.has_black) {
        uint16_t sb[4] = {
            meta.black[((dy_r & 1) * 2) + (dx_r & 1)],
            meta.black[((dy_r & 1) * 2) + ((dx_r ^ 1) & 1)],
            meta.black[(((dy_r ^ 1) & 1) * 2) + (dx_r & 1)],
            meta.black[(((dy_r ^ 1) & 1) * 2) + ((dx_r ^ 1) & 1)],
        };
        for (int p = 0; p < 4; ++p) uf.black_c[p] = static_cast<float>(sb[p]);
    } else {
        const float b_def = (dec_info.codec == nijinwvdc::CodecFamily::Arw6) ? 512.0f
                          : static_cast<float>(dng_opts.black[0]);
        for (int p = 0; p < 4; ++p) uf.black_c[p] = b_def;
    }
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            uf.black_6x6[r][c] = uf.black_c[((r & 1) * 2) | (c & 1)];
        }
    }

    const double mr = (meta.has_wb && meta.wb_rggb[0] > 1e-6) ? meta.wb_rggb[0] : 1.0;
    const double mg0 = (meta.has_wb && meta.wb_rggb[1] > 1e-6) ? meta.wb_rggb[1] : 1.0;
    const double mg1 = (meta.has_wb && meta.wb_rggb[2] > 1e-6) ? meta.wb_rggb[2] : mg0;
    const double mb = (meta.has_wb && meta.wb_rggb[3] > 1e-6) ? meta.wb_rggb[3] : 1.0;
    const double mg = 0.5 * (mg0 + mg1);
    uf.wb[0] = static_cast<float>(mr / std::max(1e-6, mg));
    uf.wb[1] = 1.0f;
    uf.wb[2] = static_cast<float>(mb / std::max(1e-6, mg));

    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            uf.cam_xyz[r][c] = dng_opts.color_matrix1[r * 3 + c];
            uf.rgb_cam[r][c] = 0.0;
        }
    }

    uf.focal_mm = meta.focal_length_mm;
    uf.focal_opt_mm = meta.focal_length_opt_mm;
    uf.focus_distance_m = meta.focus_distance_m;
    uf.focus_distance_low_m = meta.focus_distance_low_m;
    uf.focus_distance_high_m = meta.focus_distance_high_m;
    uf.focus_is_infinity = meta.focus_is_infinity;
    if (meta.has_lens_position_abs) {
        uf.focus_step = std::max(1.0f, 100000.0f + static_cast<float>(meta.lens_position_abs));
    } else if (meta.focus_position > 0u) {
        uf.focus_step = static_cast<float>(256u - std::min<uint16_t>(255u, meta.focus_position));
    }
    uf.aperture = meta.fnumber;
    uf.shutter_s = meta.exposure_time_s;
    uf.iso = static_cast<float>(meta.iso);
    uf.lens = clip.lens_model;

    double pts_s = dec_info.has_timing ? dec_info.timing.pts_seconds : 0.0;
    if (dec_info.has_timing && dec_info.timing.timescale > 0u) {
        pts_s = static_cast<double>(dec_info.timing.pts_ticks) /
                static_cast<double>(dec_info.timing.timescale);
        uf.has_pts_ticks = true;
        uf.pts_ticks = dec_info.timing.pts_ticks;
        uf.pts_timescale = dec_info.timing.timescale;
    }
    if (!clip.datetime.empty() || ext == ".nev") {
        // P0-CLK: retain exact sub-millisecond double timestamp from tick/timescale instead of rounding to integer ms.
        const int64_t base_sec = parse_exif_datetime_utc_sec(clip.datetime);
        uf.pts_base_sec = base_sec;
        uf.timestamp_s = static_cast<double>(base_sec) + std::max(0.0, pts_s);
    }
    if (ext == ".nev" ||
        uf.model.find("Z 8") != std::string::npos || uf.model.find("Z8") != std::string::npos ||
        uf.model.find("Z 9") != std::string::npos || uf.model.find("Z9") != std::string::npos ||
        (uf.shutter_s > 0.0f && uf.shutter_s < (1.0f / 8100.0f))) {
        uf.shutter_kind = 1;
    } else if (uf.model.find("ILCE-9M3") != std::string::npos) {
        uf.shutter_kind = 2;
    }

    // Header-only LibRaw metadata pass on .NEF/.ARW gives fine makernote metadata (focus, OIS, cam_xyz)
    if (ext == ".nef" || ext == ".nrw" || ext == ".arw") {
        enrich_libraw_header(base, uf);
    }

    uf.wvdc_quant = std::move(dec_info.quant_profile);
    if (uf.wvdc_quant.available) {
        if (dy_r > 0 || H != raw_h) {
            nijinwvdc::crop_quant_profile_vertical(
                uf.wvdc_quant, static_cast<uint32_t>(dy_r), static_cast<uint32_t>(H));
        }
        const float blk_avg = 0.25f * (uf.black_c[0] + uf.black_c[1] + uf.black_c[2] + uf.black_c[3]);
        nijinwvdc::recalibrate_quant_profile_range(uf.wvdc_quant, blk_avg, uf.white);
    }

    uf.status = 0;
    return true;
}

inline bool export_virtual_frame_dng(
    const std::string& vpath,
    const std::string& out_dng_path,
    int inner_threads,
    std::string& err_out) {
    err_out.clear();
    std::string base;
    uint32_t fidx = 0u;
    split_virtual_raw_path(vpath, base, fidx);
    const std::string ext = lower_ext_of(base);
    if (ext == ".jxs" && !is_bayer_codestream_magic(base)) {
        err_out = "Input .jxs is not a raw Bayer TicoRAW/ARW6 codestream";
        return false;
    }
    auto reader = MediaReaderCache::open_cached(base, err_out);
    if (!reader) {
        if (err_out.empty()) err_out = "nijinWVDC failed opening container";
        return false;
    }

    static thread_local nijinwvdc::DecoderSession tls_session(1);
    nijinwvdc::DecodeOptions opts;
    opts.num_threads = std::max(1, inner_threads);
    opts.apply_active_crop = true;
    opts.want_quant_profile = false;

    std::vector<uint16_t> dec_bayer;
    nijinwvdc::DecodedFrameInfo dec_info;
    if (!tls_session.decode_frame(*reader, fidx, opts, dec_bayer, dec_info, err_out)) {
        if (err_out.empty()) err_out = "nijinWVDC decode_frame failed";
        return false;
    }
    if (dec_info.output_width < 64u || dec_info.output_height < 64u) {
        err_out = "Invalid decoded dimensions from nijinWVDC";
        return false;
    }

    const auto& clip = reader->clip_info();
    const auto& meta = dec_info.has_meta ? dec_info.meta : nijinwvdc::NevFrameMeta();
    nijinwvdc::DngWriteOptions dng_opts;
    if (dec_info.codec == nijinwvdc::CodecFamily::Arw6) {
        nijinwvdc::Arw6Info arw6;
        arw6.width = dec_info.coded_width;
        arw6.height = dec_info.coded_height;
        arw6.bit_depth = dec_info.bit_depth;
        nijinwvdc::build_dng_options_from_clip_and_frame(clip, meta, arw6, true, 0, dng_opts);
    } else {
        nijinwvdc::TicoRawInfo jxs;
        jxs.width = static_cast<uint16_t>(dec_info.coded_width);
        jxs.height = static_cast<uint16_t>(dec_info.coded_height);
        const uint16_t raw_bd = dec_info.bit_depth > 0u ? dec_info.bit_depth : 14u;
        jxs.in_bitdepth = static_cast<int>(raw_bd);
        jxs.out_bitdepth = static_cast<int>(raw_bd);
        jxs.cdt_depth = static_cast<int>(raw_bd);
        nijinwvdc::build_dng_options_from_clip_and_frame(clip, meta, jxs, true, 0, dng_opts);
        if (meta.white_level == 0u) {
            dng_opts.white = static_cast<uint16_t>((1u << raw_bd) - 1u);
        }
    }
    dng_opts.width = dec_info.output_width;
    dng_opts.height = dec_info.output_height;
    std::memcpy(dng_opts.cfa_pattern, dec_info.cfa_pattern, 4);
    if (dec_info.output_width != dec_info.coded_width ||
        dec_info.output_height != dec_info.coded_height) {
        dng_opts.has_crop = false;
    }
    return nijinwvdc::write_cfa_dng(out_dng_path, dec_bayer.data(), dng_opts, err_out);
}

#endif // HERAW_HAS_WVDC

} // namespace heraw_wvdc
