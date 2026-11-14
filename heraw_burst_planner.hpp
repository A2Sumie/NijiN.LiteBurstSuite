// heraw_burst_planner.hpp -- Batch folder scanner, EXIF time sorter, conditional frame/motion
// suitability analyzer, multi-group partitioner, and <=2-option interactive advisor.
// Pure 7-bit ASCII C++17 header.
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "../libraw-heraw/libraw/libraw.h"
#include "heraw_libraw_dyn.hpp"
#include "heraw_exif_io.hpp"

namespace heraw_plan {

enum class ExecPolicy {
    AUTO_PRUNE,  // Auto-reject unsuitable frames; require >= 4 valid frames per group
    MULTI_GROUP, // Partition long sequences / folders into all valid >= 4-frame groups
    FORCE_ALL,   // Force execution down to 1 frame, bypassing <4 & motion blocks
    INTERACTIVE  // Present at most 2 recommended options on anomalies / <4 / long bursts
};

enum SuitFlag : uint32_t {
    SUIT_OK               = 0,
    SUIT_DARK             = 1u << 0,
    SUIT_DIM_MISMATCH     = 1u << 1,
    SUIT_EXPOSURE_OUTLIER = 1u << 2,
    SUIT_MOTION_BLUR      = 1u << 3,
    SUIT_MOTION_TOO_LARGE = 1u << 4,
    SUIT_PHASE_REDUNDANT  = 1u << 5
};

enum class MotionRegime {
    SINGLE_FRAME,
    NORMAL_HANDHELD,
    TOO_SMALL_TRIPOD,
    TOO_LARGE_PARTIAL
};

struct RawHeaderInfo {
    std::string path;
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
    int orig_order = 0;
};

struct FrameDiag {
    int idx = 0;
    std::string path;
    int64_t timestamp = 0;
    int subsec_ms = 0;
    float iso = 0.0f;
    float shutter = 0.0f;
    float med_sig = 0.0f;
    float block_iqr = 0.0f;
    float hf_sigma = 1.0f;
    double acutance = 0.0;
    float shift_1x = 0.0f;
    float dx_1x = 0.0f;
    float dy_1x = 0.0f;
    float zncc = 1.0f;
    float local_spread_1x = 0.0f;
    uint32_t flags = SUIT_OK;
    int assigned_group = -1; // -1 = unassigned/dropped, >=0 = group index
    bool is_crown = false;
    bool shutter_shock = false;
};

struct ScenePrescription {
    std::string scene_code = "handheld_standard"; // handheld_standard | tripod_static | action_subject | lowlight_high_iso | astro_sky | astro_landscape | aeb_bracket
    std::string rec_preset = "photo";             // photo | sci | arc | astro
    std::string rec_anchor = "auto";              // auto | first | middle | <1..N>
    int best_anchor_1based = 1;                   // 1-based index in cluster
    int quad_coverage = 1;                        // 1..4 2x2 subpixel quadrants
    int sr_score = 75;                            // 0..100 super-resolution potential score
    int star_count = 0;                           // detected pinpoint stars on ref frame
    float motion_area_pct = 0.0f;                 // % of area with local moving subjects
    float max_ev_spread = 0.0f;                   // max exposure spread in EV stops
    float rec_denoise = 1.00f;
    float rec_deghost = 1.00f;
    int rec_psf_iters = 4;
    double est_dng_mb = 0.0;                      // estimated 2x lossless DNG size (MiB)
    bool shutter_shock_first = false;
    std::string summary_en;
};

struct PlannedGroup {
    std::vector<int> light_uframe_indices; // index 0 = sharpest alignment anchor (REF)
    std::vector<int> dark_uframe_indices;
    std::string naming_stem;               // stem of earliest chronological frame in group
    MotionRegime motion = MotionRegime::NORMAL_HANDHELD;
    float max_shift_1x = 0.0f;
    float phase_spread_1x = 0.0f;
    int quad_coverage = 1;                 // 1..4 2x2 subpixel quadrants covered
    double mean_acutance = 0.0;
};

struct InteractiveChoicePlan {
    int id = 1; // 1 or 2
    std::string title;
    std::string detail;
    bool aborts = false;
    std::vector<PlannedGroup> groups;
};

struct ClusterPlanResult {
    std::vector<FrameDiag> diags;
    MotionRegime overall_motion = MotionRegime::NORMAL_HANDHELD;
    bool needs_interactive_prompt = false;
    std::string condition_code;
    std::string condition_summary;
    std::vector<PlannedGroup> selected_groups;
    std::vector<InteractiveChoicePlan> options; // ALWAYS <= 2 elements when needs_interactive_prompt
    ScenePrescription rx;
};

static inline std::string basename_of(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    return (pos == std::string::npos) ? p : p.substr(pos + 1);
}

static inline std::string stem_of(const std::string& p) {
    std::string b = basename_of(p);
    size_t dot = b.find_last_of('.');
    return (dot == std::string::npos) ? b : b.substr(0, dot);
}

static inline bool has_supported_raw_ext(const std::string& path) {
    std::string b = basename_of(path);
    if (b.empty() || b[0] == '.') return false; // Ignore macOS AppleDouble ._* and hidden sidecars
    size_t dot = b.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = b.substr(dot);
    for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
    static const char* kExts[] = {
        ".nef", ".nrw", ".dng", ".arw", ".cr2", ".cr3",
        ".raf", ".orf", ".rw2", ".pef", ".srw", ".iiq"
    };
    for (const char* e : kExts) {
        if (ext == e) return true;
    }
    return false;
}

// Natural filename comparison ("Z8S_8462" < "Z8S_8463" < "Z8S_10000")
static bool natural_less(const std::string& a, const std::string& b) {
    size_t ia = 0, ib = 0;
    while (ia < a.size() && ib < b.size()) {
        unsigned char ca = (unsigned char)a[ia];
        unsigned char cb = (unsigned char)b[ib];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            while (ia < a.size() && a[ia] == '0') ++ia;
            while (ib < b.size() && b[ib] == '0') ++ib;
            size_t ja = ia, jb = ib;
            while (ja < a.size() && std::isdigit((unsigned char)a[ja])) ++ja;
            while (jb < b.size() && std::isdigit((unsigned char)b[jb])) ++jb;
            if (ja - ia != jb - ib) return (ja - ia) < (jb - ib);
            int cmp = a.compare(ia, ja - ia, b, ib, jb - ib);
            if (cmp != 0) return cmp < 0;
            ia = ja; ib = jb;
        } else {
            char la = (char)std::tolower(ca);
            char lb = (char)std::tolower(cb);
            if (la != lb) return la < lb;
            ++ia; ++ib;
        }
    }
    return a.size() < b.size();
}

static bool is_directory_utf8(const std::string& path) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (n <= 0) return false;
    std::wstring wp(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wp[0], n);
    DWORD attr = GetFileAttributesW(wp.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && ((attr & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    return S_ISDIR(st.st_mode);
#endif
}

static void scan_dir_utf8(const std::string& dir, bool recursive, std::vector<std::string>& out_files) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, dir.data(), (int)dir.size(), nullptr, 0);
    if (n <= 0) return;
    std::wstring wdir(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, dir.data(), (int)dir.size(), &wdir[0], n);
    std::wstring pat = wdir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::string> local_files, subdirs;
    do {
        if ( std::wcscmp(fd.cFileName, L".") == 0 || std::wcscmp(fd.cFileName, L"..") == 0 ) continue;
        std::wstring wfull = wdir + L"\\" + fd.cFileName;
        int ulen = WideCharToMultiByte(CP_UTF8, 0, wfull.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (ulen <= 1) continue;
        std::string full(ulen - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wfull.c_str(), -1, &full[0], ulen, nullptr, nullptr);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recursive) subdirs.push_back(std::move(full));
        } else if (has_supported_raw_ext(full)) {
            local_files.push_back(std::move(full));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(local_files.begin(), local_files.end(), natural_less);
    std::sort(subdirs.begin(), subdirs.end(), natural_less);
    for (auto& f : local_files) out_files.push_back(std::move(f));
    for (const auto& sd : subdirs) scan_dir_utf8(sd, true, out_files);
#else
    DIR* dp = ::opendir(dir.c_str());
    if (!dp) return;
    std::vector<std::string> local_files, subdirs;
    while (struct dirent* de = ::readdir(dp)) {
        if (std::strcmp(de->d_name, ".") == 0 || std::strcmp(de->d_name, "..") == 0) continue;
        std::string full = dir + "/" + de->d_name;
        struct stat st{};
        if (::stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (recursive) subdirs.push_back(std::move(full));
        } else if (S_ISREG(st.st_mode) && has_supported_raw_ext(full)) {
            local_files.push_back(std::move(full));
        }
    }
    ::closedir(dp);
    std::sort(local_files.begin(), local_files.end(), natural_less);
    std::sort(subdirs.begin(), subdirs.end(), natural_less);
    for (auto& f : local_files) out_files.push_back(std::move(f));
    for (const auto& sd : subdirs) scan_dir_utf8(sd, true, out_files);
#endif
}

// Expand files & directories into a deduplicated RAW file list; sets saw_dir=true if any directory was scanned
static std::vector<std::string> expand_raw_inputs(
    const std::vector<std::string>& raw_args, const std::vector<std::string>& scan_dirs,
    bool recursive, bool& out_saw_dir
) {
    out_saw_dir = !scan_dirs.empty();
    std::vector<std::string> expanded;
    for (const auto& d : scan_dirs) scan_dir_utf8(d, recursive, expanded);
    for (const auto& p : raw_args) {
        if (is_directory_utf8(p)) {
            out_saw_dir = true;
            scan_dir_utf8(p, recursive, expanded);
        } else {
            expanded.push_back(p);
        }
    }
    std::vector<std::string> unique_out;
    unique_out.reserve(expanded.size());
    for (const auto& f : expanded) {
        if (std::find(unique_out.begin(), unique_out.end(), f) == unique_out.end()) {
            unique_out.push_back(f);
        }
    }
    return unique_out;
}

template <typename ParFn>
static std::vector<RawHeaderInfo> scan_raw_headers_fast(
    const std::vector<std::string>& paths, bool sort_by_time, int n_threads, ParFn&& par_rows
) {
    const int n = (int)paths.size();
    std::vector<RawHeaderInfo> infos(n);
    par_rows(n, n_threads, [&](int i0, int i1, int) {
        for (int i = i0; i < i1; ++i) {
            RawHeaderInfo& hi = infos[i];
            hi.path = paths[i];
            hi.orig_order = i;
            const auto& dyn = heraw_lr::global_dyn_libraw();
            if (dyn.active) {
                libraw_data_t* dlr = dyn.p_init(0);
                if (!dlr) continue;
                if (dyn.open_path(dlr, hi.path) == LIBRAW_SUCCESS) {
                    hi.ok = true;
                    libraw_iparams_t* ip = dyn.p_get_iparams(dlr);
                    libraw_imgother_t* io = dyn.p_get_imgother(dlr);
                    hi.make = (ip && ip->make[0]) ? ip->make : "Camera";
                    hi.model = (ip && ip->model[0]) ? ip->model : "Model";
                    hi.width = dyn.p_get_iwidth(dlr) & ~1;
                    hi.height = dyn.p_get_iheight(dlr) & ~1;
                    if (io) {
                        hi.timestamp = (int64_t)io->timestamp;
                        hi.shot_order = io->shot_order;
                        hi.iso = io->iso_speed;
                        hi.shutter = io->shutter;
                        hi.aperture = io->aperture;
                        hi.focal_len = io->focal_len;
                    }
                    hi.subsec_ms = heraw_io::read_raw_subsec_ms(hi.path);
                }
                dyn.p_close(dlr);
                continue;
            }
            std::unique_ptr<LibRaw> lr(new LibRaw());
#ifdef _WIN32
            int wn = MultiByteToWideChar(CP_UTF8, 0, hi.path.data(), (int)hi.path.size(), nullptr, 0);
            std::wstring wp(wn > 0 ? wn : 0, L'\0');
            if (wn > 0) MultiByteToWideChar(CP_UTF8, 0, hi.path.data(), (int)hi.path.size(), &wp[0], wn);
            int rc = lr->open_file(wp.c_str());
#else
            int rc = lr->open_file(hi.path.c_str());
#endif
            if (rc != LIBRAW_SUCCESS) continue;
            hi.ok = true;
            hi.make = lr->imgdata.idata.make[0] ? lr->imgdata.idata.make : "Camera";
            hi.model = lr->imgdata.idata.model[0] ? lr->imgdata.idata.model : "Model";
            hi.width = lr->imgdata.sizes.width & ~1;
            hi.height = lr->imgdata.sizes.height & ~1;
            hi.timestamp = (int64_t)lr->imgdata.other.timestamp;
            hi.shot_order = lr->imgdata.other.shot_order;
            hi.iso = lr->imgdata.other.iso_speed;
            hi.shutter = lr->imgdata.other.shutter;
            hi.aperture = lr->imgdata.other.aperture;
            hi.focal_len = lr->imgdata.other.focal_len;
            hi.subsec_ms = heraw_io::read_raw_subsec_ms(hi.path);
        }
    }, 1);

    if (sort_by_time) {
        std::stable_sort(infos.begin(), infos.end(), [](const RawHeaderInfo& a, const RawHeaderInfo& b) {
            if (a.ok != b.ok) return a.ok > b.ok;
            std::string cam_a = a.make + "|" + a.model;
            std::string cam_b = b.make + "|" + b.model;
            if (cam_a != cam_b) return cam_a < cam_b;
            if (a.width != b.width) return a.width > b.width;
            if (a.height != b.height) return a.height > b.height;
            if (a.timestamp > 0 && b.timestamp > 0 && a.timestamp != b.timestamp) {
                return a.timestamp < b.timestamp;
            }
            if (a.timestamp == b.timestamp && a.subsec_ms != b.subsec_ms) {
                return a.subsec_ms < b.subsec_ms;
            }
            if (a.shot_order != b.shot_order) return a.shot_order < b.shot_order;
            return natural_less(a.path, b.path);
        });
    }
    return infos;
}

// Group header-scanned files into memory-bounded candidate burst clusters
static std::vector<std::vector<RawHeaderInfo>> cluster_headers_into_bursts(
    const std::vector<RawHeaderInfo>& infos, bool split_by_time_and_meta, double burst_gap_s
) {
    std::vector<std::vector<RawHeaderInfo>> clusters;
    for (const auto& hi : infos) {
        if (!hi.ok) continue;
        if (clusters.empty() || !split_by_time_and_meta) {
            if (clusters.empty()) clusters.emplace_back();
            clusters.back().push_back(hi);
            continue;
        }
        const RawHeaderInfo& prev = clusters.back().back();
        bool same_cam = (hi.make == prev.make && hi.model == prev.model &&
                         hi.width == prev.width && hi.height == prev.height);
        bool same_focal = (hi.focal_len <= 0.0f || prev.focal_len <= 0.0f ||
                           std::fabs(hi.focal_len - prev.focal_len) <= 0.03f * std::max(hi.focal_len, prev.focal_len));
        double dt = 0.0;
        if (hi.timestamp > 0 && prev.timestamp > 0) {
            dt = (double)(hi.timestamp - prev.timestamp) + 0.001 * (double)(hi.subsec_ms - prev.subsec_ms);
        }
        bool time_close = (hi.timestamp == 0 || prev.timestamp == 0 || (std::fabs(dt) <= burst_gap_s));
        // Cap single raw unpack cluster at 32 frames (~2.8 GB Bayer u16 max) so 1000-frame timelapses never OOM
        bool under_ram_cap = (clusters.back().size() < 32);
        if (same_cam && same_focal && time_close && under_ram_cap) {
            clusters.back().push_back(hi);
        } else {
            clusters.push_back({hi});
        }
    }
    return clusters;
}

static inline std::string flags_to_string(uint32_t fl) {
    if (fl == SUIT_OK) return "OK";
    if (fl & SUIT_DARK) return "DARK";
    std::string s;
    auto add = [&](const char* k) { if (!s.empty()) s += "|"; s += k; };
    if (fl & SUIT_DIM_MISMATCH)     add("DIM_MISMATCH");
    if (fl & SUIT_EXPOSURE_OUTLIER) add("EXPOSURE_OUTLIER");
    if (fl & SUIT_MOTION_TOO_LARGE) add("MOTION_TOO_LARGE");
    if (fl & SUIT_MOTION_BLUR)      add("MOTION_BLUR");
    if (fl & SUIT_PHASE_REDUNDANT)  add("PHASE_REDUNDANT");
    return s.empty() ? "OK" : s;
}

static inline const char* motion_regime_str(MotionRegime m) {
    switch (m) {
        case MotionRegime::SINGLE_FRAME:      return "single_frame";
        case MotionRegime::NORMAL_HANDHELD:   return "normal_handheld";
        case MotionRegime::TOO_SMALL_TRIPOD:  return "motion_too_small";
        case MotionRegime::TOO_LARGE_PARTIAL: return "motion_too_large";
    }
    return "normal_handheld";
}

struct ThumbStar {
    float x_1x = 0.0f, y_1x = 0.0f, flux = 0.0f, snr = 0.0f;
};

// Fast 1/8 Green thumbnail + 1/4-scale (4x4 Bayer Green quad) matched-filter star-centroid & dark-frame telemetry
struct FastThumb {
    int H = 0, W = 0;
    int orig_H = 0, orig_W = 0;
    float p50_norm = 0.0f, p75_norm = 0.0f;
    float bm_span = 0.0f, p5th_peak = 0.0f;
    float max_star_snr = 0.0f, top3_star_flux = 0.0f;
    int stars_snr65_cnt = 0;
    bool is_dark_frame = false;
    std::vector<float> img;
    std::vector<ThumbStar> stars_1x;
};

template <typename UFrame>
static FastThumb build_fast_thumb(const UFrame& uf) {
    FastThumb t;
    t.orig_H = uf.H;
    t.orig_W = uf.W;
    t.H = std::max(1, uf.H / 8);
    t.W = std::max(1, uf.W / 8);
    t.img.resize((size_t)t.H * t.W, 0.0f);
    float b_g = 0.5f * (uf.black_c[1] + uf.black_c[2]);
    float inv_s = 1.0f / std::max(1.0f, uf.white - b_g);

    // Build 1/4-scale (4x4 1x pixels = 2x2 Bayer Green quads) plane gq + hot-pixel-immune trimmed quad gtrim
    const int Hq = std::max(1, uf.H / 4), Wq = std::max(1, uf.W / 4);
    std::vector<float> gq((size_t)Hq * Wq, 0.0f), gtrim((size_t)Hq * Wq, 0.0f);
    std::vector<float> samp;
    samp.reserve((size_t)(Hq / 4 + 1) * (Wq / 4 + 1));

    for (int r = 0; r < Hq; ++r) {
        for (int c = 0; c < Wq; ++c) {
            float gv[4];
            int k = 0;
            for (int dy = 0; dy < 2; ++dy) {
                const uint16_t* r0 = uf.bayer.data() + (size_t)(2 * (2 * r + dy)) * uf.W + 4 * c;
                const uint16_t* r1 = r0 + uf.W;
                for (int dx = 0; dx < 2; ++dx) {
                    float g1 = ((float)r0[2 * dx + 1] - b_g) * inv_s;
                    float g2 = ((float)r1[2 * dx]     - b_g) * inv_s;
                    gv[k++] = 0.5f * (g1 + g2);
                }
            }
            std::sort(gv, gv + 4);
            float v = 0.25f * (gv[0] + gv[1] + gv[2] + gv[3]);
            gq[(size_t)r * Wq + c] = v;
            // Middle-two average drops single-quad hot pixel / cosmic ray while keeping multi-pixel optical star PSF
            gtrim[(size_t)r * Wq + c] = 0.5f * (gv[1] + gv[2]);
            if (((r | c) & 3) == 0) samp.push_back(v);
        }
    }

    for (int r = 0; r < t.H; ++r) {
        for (int c = 0; c < t.W; ++c) {
            int r0 = std::min(Hq - 1, 2 * r), r1 = std::min(Hq - 1, 2 * r + 1);
            int c0 = std::min(Wq - 1, 2 * c), c1 = std::min(Wq - 1, 2 * c + 1);
            t.img[(size_t)r * t.W + c] = 0.25f * (
                gq[(size_t)r0 * Wq + c0] + gq[(size_t)r0 * Wq + c1] +
                gq[(size_t)r1 * Wq + c0] + gq[(size_t)r1 * Wq + c1]);
        }
    }

    if (!samp.empty()) {
        std::sort(samp.begin(), samp.end());
        t.p50_norm = samp[samp.size() / 2];
        t.p75_norm = samp[(samp.size() * 3) / 4];

        if (Hq >= 32 && Wq >= 32) {
            std::vector<float> bmeans;
            for (int by = 0; by + 16 <= Hq; by += 16) {
                for (int bx = 0; bx + 16 <= Wq; bx += 16) {
                    double s = 0.0;
                    for (int r = 0; r < 16; ++r)
                        for (int c = 0; c < 16; ++c) s += gq[(size_t)(by + r) * Wq + (bx + c)];
                    bmeans.push_back((float)(s * (1.0 / 256.0)));
                }
            }
            if (!bmeans.empty()) {
                std::sort(bmeans.begin(), bmeans.end());
                t.bm_span = bmeans[(bmeans.size() * 99) / 100] - bmeans[bmeans.size() / 100];
            }
        }

        // Extract pinpoint stars via 5x5 ([1,4,6,4,1]/16) optical PSF matched filter when >=75% of frame is dark
        if (t.p50_norm < 0.095f && t.p75_norm < 0.16f && Hq >= 24 && Wq >= 24) {
            std::vector<float> buf1((size_t)Hq * Wq, 0.0f), gpsf = gtrim;
            for (int pass = 0; pass < 2; ++pass) {
                for (int r = 0; r < Hq; ++r) {
                    for (int c = 0; c < Wq; ++c) {
                        int cm = std::max(0, c - 1), cp = std::min(Wq - 1, c + 1);
                        buf1[(size_t)r * Wq + c] = 0.25f * (gpsf[(size_t)r * Wq + cm] + 2.0f * gpsf[(size_t)r * Wq + c] + gpsf[(size_t)r * Wq + cp]);
                    }
                }
                for (int r = 0; r < Hq; ++r) {
                    int rm = std::max(0, r - 1), rp = std::min(Hq - 1, r + 1);
                    for (int c = 0; c < Wq; ++c) {
                        gpsf[(size_t)r * Wq + c] = 0.25f * (buf1[(size_t)rm * Wq + c] + 2.0f * buf1[(size_t)r * Wq + c] + buf1[(size_t)rp * Wq + c]);
                    }
                }
            }

            std::vector<float> psamp;
            psamp.reserve((size_t)(Hq / 4 + 1) * (Wq / 4 + 1));
            for (int r = 4; r < Hq - 4; r += 4)
                for (int c = 4; c < Wq - 4; c += 4) psamp.push_back(gpsf[(size_t)r * Wq + c]);
            std::sort(psamp.begin(), psamp.end());
            float pp25 = psamp[psamp.size() / 4], pp50 = psamp[psamp.size() / 2], pp75 = psamp[(psamp.size() * 3) / 4];
            float sig_psf = std::max(1e-5f, (pp75 - pp25) / 1.349f);

            std::vector<float> top_peaks;
            const int r_ring = (Hq < 200) ? 4 : 7;
            const int r_diag = (Hq < 200) ? 3 : 5;
            const int r_nms  = (Hq < 200) ? 2 : 3;
            for (int r = r_ring + 1; r + r_ring + 1 < Hq; ++r) {
                for (int c = r_ring + 1; c + r_ring + 1 < Wq; ++c) {
                    float v0 = gpsf[(size_t)r * Wq + c];
                    if (v0 < pp50 + 4.6f * sig_psf) continue;
                    bool is_max = true;
                    for (int dy = -r_nms; dy <= r_nms && is_max; ++dy) {
                        for (int dx = -r_nms; dx <= r_nms; ++dx) {
                            if ((dy || dx) && gpsf[(size_t)(r + dy) * Wq + (c + dx)] >= v0) { is_max = false; break; }
                        }
                    }
                    if (!is_max) continue;
                    float rv[8] = {
                        gpsf[(size_t)(r - r_ring) * Wq + c], gpsf[(size_t)(r + r_ring) * Wq + c],
                        gpsf[(size_t)r * Wq + (c - r_ring)], gpsf[(size_t)r * Wq + (c + r_ring)],
                        gpsf[(size_t)(r - r_diag) * Wq + (c - r_diag)], gpsf[(size_t)(r - r_diag) * Wq + (c + r_diag)],
                        gpsf[(size_t)(r + r_diag) * Wq + (c - r_diag)], gpsf[(size_t)(r + r_diag) * Wq + (c + r_diag)]
                    };
                    std::sort(rv, rv + 8);
                    float bg_loc = rv[3];
                    if (rv[6] > bg_loc + 4.5f * sig_psf + 0.015f) continue; // Radial symmetry check
                    float exc = v0 - bg_loc;
                    float snr = exc / sig_psf;
                    if (snr < 4.8f) continue;
                    top_peaks.push_back(exc);
                    if (snr >= 6.5f) ++t.stars_snr65_cnt;
                    t.max_star_snr = std::max(t.max_star_snr, snr);

                    double sw = 0.0, sx = 0.0, sy = 0.0;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            double w = std::max(0.0, (double)gpsf[(size_t)(r + dy) * Wq + (c + dx)] - (double)bg_loc);
                            sw += w; sx += w * (c + dx); sy += w * (r + dy);
                        }
                    }
                    if (sw > 1e-6) {
                        t.stars_1x.push_back({(float)((sx / sw + 0.5) * 4.0), (float)((sy / sw + 0.5) * 4.0), exc, snr});
                    }
                }
            }
            if (top_peaks.size() >= 5) {
                std::nth_element(top_peaks.begin(), top_peaks.begin() + 4, top_peaks.end(), std::greater<float>());
                t.p5th_peak = top_peaks[4];
            } else if (!top_peaks.empty()) {
                t.p5th_peak = *std::max_element(top_peaks.begin(), top_peaks.end());
            }
            std::sort(t.stars_1x.begin(), t.stars_1x.end(), [](const ThumbStar& a, const ThumbStar& b) { return a.snr > b.snr; });
            if (t.stars_1x.size() > 90) t.stars_1x.resize(90);
            for (size_t k = 0; k < std::min<size_t>(3, t.stars_1x.size()); ++k) {
                if (t.stars_1x[k].snr >= 5.5f) t.top3_star_flux += t.stars_1x[k].flux;
            }

            // True zero-light lens-cap dark frame across ISO 64 .. ISO 102400 (0 stars >=6.5s & max_snr <7.2)
            if (std::fabs(t.p50_norm) < 0.0025f && t.bm_span < 0.0028f &&
                t.stars_snr65_cnt == 0 && t.max_star_snr < 7.2f) {
                t.is_dark_frame = true;
                t.stars_1x.clear();
            }
        }
    }
    return t;
}

// Returns shift in 1x sensor pixels (1 thumb pixel = 8 1x pixels), mean ZNCC, and non-rigid block spread
static void measure_pair_motion_1x(
    const FastThumb& ref, const FastThumb& cur,
    float& out_dx_1x, float& out_dy_1x, float& out_shift_1x, float& out_zncc, float& out_spread_1x
) {
    out_dx_1x = 0.0f; out_dy_1x = 0.0f; out_shift_1x = 0.0f; out_zncc = 0.0f; out_spread_1x = 0.0f;
    if (ref.H != cur.H || ref.W != cur.W || ref.H < 48 || ref.W < 48) return;

    const int H = ref.H, W = ref.W;
    const int bw = std::min(40, std::min(H, W) / 4);
    const int rad = std::min(24, std::min(H, W) / 4); // +-24 thumb px = +-192 1x pixels!

    struct CandPatch { int y0, x0; double grad_e; double r_mean; double r_var; };
    std::vector<CandPatch> cands;
    const int Np = bw * bw;
    for (int gy = 0; gy < 4; ++gy) {
        int y0 = (H - bw) * (2 * gy + 1) / 8;
        for (int gx = 0; gx < 4; ++gx) {
            int x0 = (W - bw) * (2 * gx + 1) / 8;
            double r_sum = 0.0, r_sq = 0.0, ge = 0.0;
            for (int r = 0; r < bw; ++r) {
                const float* rp = ref.img.data() + (size_t)(y0 + r) * W + x0;
                for (int c = 0; c < bw; ++c) {
                    r_sum += rp[c];
                    r_sq += (double)rp[c] * rp[c];
                    if (r + 1 < bw && c + 1 < bw) {
                        double dx = rp[c + 1] - rp[c];
                        double dy = rp[W + c] - rp[c];
                        ge += dx * dx + dy * dy;
                    }
                }
            }
            double r_mean = r_sum / Np;
            double r_var = r_sq - Np * r_mean * r_mean;
            if (r_var > 1e-7) cands.push_back({y0, x0, ge, r_mean, r_var});
        }
    }
    // Rank by in-focus spatial gradient energy so out-of-focus bokeh noise patches never corrupt ZNCC!
    std::sort(cands.begin(), cands.end(), [](const CandPatch& a, const CandPatch& b) { return a.grad_e > b.grad_e; });
    if (cands.size() > 5) cands.resize(5);

    struct Vec2 { float dx, dy, ncc; };
    std::vector<Vec2> hits;

    for (const auto& cp : cands) {
        const int y0 = cp.y0, x0 = cp.x0;
        const double r_mean = cp.r_mean, r_var = cp.r_var;

            auto eval_ncc = [&](int dy, int dx) -> double {
                int cy = y0 + dy, cx = x0 + dx;
                if (cy < 0 || cx < 0 || cy + bw > H || cx + bw > W) return -2.0;
                double c_sum = 0.0, c_sq = 0.0, rc_dot = 0.0;
                for (int r = 0; r < bw; ++r) {
                    const float* rp = ref.img.data() + (size_t)(y0 + r) * W + x0;
                    const float* cp = cur.img.data() + (size_t)(cy + r) * W + cx;
                    for (int c = 0; c < bw; ++c) {
                        double cv = cp[c];
                        c_sum += cv;
                        c_sq += cv * cv;
                        rc_dot += (double)rp[c] * cv;
                    }
                }
                double c_var = c_sq - (c_sum * c_sum) / Np;
                if (c_var < 1e-7) return -2.0;
                return (rc_dot - r_mean * c_sum) / std::sqrt(r_var * c_var);
            };

            int best_dy = 0, best_dx = 0;
            double best_ncc = -2.0;
            for (int dy = -rad; dy <= rad; dy += 2) {
                for (int dx = -rad; dx <= rad; dx += 2) {
                    double v = eval_ncc(dy, dx);
                    if (v > best_ncc) { best_ncc = v; best_dy = dy; best_dx = dx; }
                }
            }
            int cdy = best_dy, cdx = best_dx;
            for (int dy = cdy - 1; dy <= cdy + 1; ++dy) {
                for (int dx = cdx - 1; dx <= cdx + 1; ++dx) {
                    double v = eval_ncc(dy, dx);
                    if (v > best_ncc) { best_ncc = v; best_dy = dy; best_dx = dx; }
                }
            }
            if (best_ncc < 0.15) continue;

            double sub_x = 0.0, sub_y = 0.0;
            double xm = eval_ncc(best_dy, best_dx - 1), xp = eval_ncc(best_dy, best_dx + 1);
            if (xm > -1.0 && xp > -1.0) {
                double d = xm - 2.0 * best_ncc + xp;
                if (std::fabs(d) > 1e-7) sub_x = std::max(-0.75, std::min(0.75, 0.5 * (xm - xp) / d));
            }
            double ym = eval_ncc(best_dy - 1, best_dx), yp = eval_ncc(best_dy + 1, best_dx);
            if (ym > -1.0 && yp > -1.0) {
                double d = ym - 2.0 * best_ncc + yp;
                if (std::fabs(d) > 1e-7) sub_y = std::max(-0.75, std::min(0.75, 0.5 * (ym - yp) / d));
            }
            hits.push_back({(float)((best_dx + sub_x) * 8.0), (float)((best_dy + sub_y) * 8.0), (float)best_ncc});
    }

    float ncc_med = 0.0f;
    if (!hits.empty()) {
        std::vector<float> xs, ys, nccs;
        for (const auto& h : hits) { xs.push_back(h.dx); ys.push_back(h.dy); nccs.push_back(h.ncc); }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        std::sort(nccs.begin(), nccs.end());
        out_dx_1x = xs[xs.size() / 2];
        out_dy_1x = ys[ys.size() / 2];
        out_shift_1x = std::hypot(out_dx_1x, out_dy_1x);
        out_zncc = nccs[nccs.size() / 2];
        ncc_med = out_zncc;

        std::vector<float> devs;
        for (const auto& h : hits) {
            devs.push_back(std::hypot(h.dx - out_dx_1x, h.dy - out_dy_1x));
        }
        std::sort(devs.begin(), devs.end());
        out_spread_1x = devs[devs.size() / 2];
    }

    // Native 1x star-centroid pair matcher for sparse & high-ISO night-sky bursts (e.g. 120-150mm 1/640s ISO 102400!)
    if (ncc_med < 0.52f && !ref.stars_1x.empty() && !cur.stars_1x.empty()) {
        const auto& s0 = ref.stars_1x;
        const auto& s1 = cur.stars_1x;
        size_t n0 = std::min<size_t>(s0.size(), 48);
        size_t n1 = std::min<size_t>(s1.size(), 56);
        float max_sh = std::min(520.0f, 0.25f * (float)std::max(ref.orig_H, ref.orig_W));
        int best_inliers = 0;
        float best_dx = 0.0f, best_dy = 0.0f, best_snr_pair = 0.0f;
        for (size_t i = 0; i < n0; ++i) {
            for (size_t j = 0; j < n1; ++j) {
                float cdx = s1[j].x_1x - s0[i].x_1x;
                float cdy = s1[j].y_1x - s0[i].y_1x;
                if (std::fabs(cdx) > max_sh || std::fabs(cdy) > max_sh) continue;
                int inl = 0;
                float max_s = std::min(s0[i].snr, s1[j].snr);
                for (size_t k = 0; k < n0; ++k) {
                    float tx = s0[k].x_1x + cdx, ty = s0[k].y_1x + cdy;
                    for (size_t m = 0; m < n1; ++m) {
                        if (std::fabs(s1[m].x_1x - tx) < 10.0f && std::fabs(s1[m].y_1x - ty) < 10.0f) {
                            if (std::hypot(s1[m].x_1x - tx, s1[m].y_1x - ty) < 10.0f) {
                                ++inl;
                                max_s = std::max(max_s, std::min(s0[k].snr, s1[m].snr));
                                break;
                            }
                        }
                    }
                }
                if (inl > best_inliers || (inl == best_inliers && max_s > best_snr_pair)) {
                    best_inliers = inl; best_dx = cdx; best_dy = cdy; best_snr_pair = max_s;
                }
            }
        }
        bool star_lock_ok = (best_inliers >= 3) ||
                            (best_inliers >= 2 && best_snr_pair >= 10.0f) ||
                            (best_inliers == 1 && best_snr_pair >= 22.0f && std::min(n0, n1) <= 3);
        if (star_lock_ok) {
            double sw = 0.0, sx = 0.0, sy = 0.0;
            std::vector<float> devs;
            for (size_t k = 0; k < n0; ++k) {
                float tx = s0[k].x_1x + best_dx, ty = s0[k].y_1x + best_dy;
                int bm = -1;
                float bd = 10.0f;
                for (size_t m = 0; m < n1; ++m) {
                    float d = std::hypot(s1[m].x_1x - tx, s1[m].y_1x - ty);
                    if (d < bd) { bd = d; bm = (int)m; }
                }
                if (bm >= 0) {
                    double wk = std::max(1.0, (double)std::min(s0[k].snr, s1[bm].snr));
                    sw += wk;
                    sx += wk * (s1[bm].x_1x - s0[k].x_1x);
                    sy += wk * (s1[bm].y_1x - s0[k].y_1x);
                    devs.push_back(bd);
                }
            }
            if (!devs.empty() && sw > 0.0) {
                out_dx_1x = (float)(sx / sw);
                out_dy_1x = (float)(sy / sw);
                out_shift_1x = std::hypot(out_dx_1x, out_dy_1x);
                out_zncc = std::min(0.98f, 0.50f + (float)best_inliers / (float)std::max<size_t>(3, std::min<size_t>(n0, 10)));
                std::sort(devs.begin(), devs.end());
                out_spread_1x = devs[devs.size() / 2];
                return;
            }
        }
    }

    if (hits.empty()) {
        out_zncc = 0.0f;
        out_shift_1x = 999.0f;
    }
}

template <typename UFrame>
static PlannedGroup finalize_planned_group(
    const std::vector<int>& clean_indices,
    const std::vector<int>& dark_indices,
    const std::vector<UFrame>& uframes,
    const std::vector<FrameDiag>& diags,
    const std::vector<FastThumb>& thumbs,
    int anchor_mode = 0 // 0=auto sharpest, -1=first, -2=middle, >=1=1-based index
) {
    PlannedGroup pg;
    pg.dark_uframe_indices = dark_indices;
    if (clean_indices.empty()) return pg;

    // Naming stem uses the first frame in the group's input/time sequence
    pg.naming_stem = stem_of(uframes[clean_indices[0]].path);

    int best_pos = 0;
    if (anchor_mode == -1) {
        best_pos = 0;
    } else if (anchor_mode == -2) {
        best_pos = (int)(clean_indices.size() / 2);
    } else if (anchor_mode >= 1) {
        best_pos = std::min((int)clean_indices.size() - 1, anchor_mode - 1);
    } else {
        for (size_t k = 1; k < clean_indices.size(); ++k) {
            if (diags[clean_indices[k]].acutance > diags[clean_indices[best_pos]].acutance) {
                best_pos = (int)k;
            }
        }
    }
    pg.light_uframe_indices.push_back(clean_indices[best_pos]);
    for (size_t k = 0; k < clean_indices.size(); ++k) {
        if ((int)k != best_pos) pg.light_uframe_indices.push_back(clean_indices[k]);
    }

    int ref_idx = pg.light_uframe_indices[0];
    bool quad_seen[4] = {true, false, false, false};
    double sum_acut = diags[ref_idx].acutance;
    double sum_p2 = 0.0;
    float max_sh = 0.0f;

    for (size_t k = 1; k < pg.light_uframe_indices.size(); ++k) {
        int idx = pg.light_uframe_indices[k];
        sum_acut += diags[idx].acutance;
        float dx = 0, dy = 0, sh = 0, ncc = 1, sp = 0;
        measure_pair_motion_1x(thumbs[ref_idx], thumbs[idx], dx, dy, sh, ncc, sp);
        max_sh = std::max(max_sh, sh);
        sum_p2 += (double)(sh * sh);
        int qx = ((int)std::llround(dx * 2.0f)) & 1;
        int qy = ((int)std::llround(dy * 2.0f)) & 1;
        quad_seen[qy * 2 + qx] = true;
    }

    pg.mean_acutance = sum_acut / (double)pg.light_uframe_indices.size();
    pg.max_shift_1x = max_sh;
    pg.phase_spread_1x = (pg.light_uframe_indices.size() > 1)
        ? (float)std::sqrt(sum_p2 / (double)(pg.light_uframe_indices.size() - 1))
        : 0.0f;
    pg.quad_coverage = (int)quad_seen[0] + (int)quad_seen[1] + (int)quad_seen[2] + (int)quad_seen[3];

    if (pg.light_uframe_indices.size() <= 1) {
        pg.motion = MotionRegime::SINGLE_FRAME;
    } else if (max_sh < 0.15f) {
        pg.motion = MotionRegime::TOO_SMALL_TRIPOD;
    } else {
        pg.motion = MotionRegime::NORMAL_HANDHELD;
    }
    return pg;
}

template <typename UFrame, typename PctFn>
static ClusterPlanResult analyze_and_plan_cluster(
    const std::vector<UFrame>& uframes,
    const std::vector<RawHeaderInfo>& hdr_meta,
    ExecPolicy policy,
    bool is_batch_scan,
    int target_group_size,
    int max_frames_cap,
    float min_rel_sharpness,
    float max_shift_px,
    PctFn&& quick_pct,
    int anchor_mode = 0,
    const std::string& current_preset = "photo"
) {
    ClusterPlanResult res;
    const int n_total = (int)uframes.size();
    res.diags.resize(n_total);
    if (n_total == 0) return res;

    // 1. Compute single-frame signal, read-noise, and acutance stats
    float max_iqr = 0.0f;
    for (int i = 0; i < n_total; ++i) {
        const auto& uf = uframes[i];
        FrameDiag& d = res.diags[i];
        d.idx = i;
        d.path = uf.path;
        if (i < (int)hdr_meta.size()) {
            d.timestamp = hdr_meta[i].timestamp;
            d.subsec_ms = hdr_meta[i].subsec_ms;
            d.iso = hdr_meta[i].iso;
            d.shutter = hdr_meta[i].shutter;
        }
        int bh = 64, bw = 64;
        std::vector<float> bmeans;
        bmeans.reserve((size_t)std::max(1, uf.H / bh) * std::max(1, uf.W / bw));
        float avg_black = 0.25f * (uf.black_c[0] + uf.black_c[1] + uf.black_c[2] + uf.black_c[3]);
        for (int by = 0; by < uf.H / bh; ++by) {
            for (int bx = 0; bx < uf.W / bw; ++bx) {
                double s = 0.0;
                for (int r = 0; r < bh; r += 2) {
                    const uint16_t* row = uf.bayer.data() + (size_t)(by * bh + r) * uf.W + bx * bw;
                    for (int c = 0; c < bw; c += 2) s += row[c];
                }
                bmeans.push_back((float)(s / (double)((bh / 2) * (bw / 2))) - avg_black);
            }
        }
        float p25 = bmeans.empty() ? 0.0f : quick_pct(bmeans, 0.25f);
        float p50 = bmeans.empty() ? 0.0f : quick_pct(bmeans, 0.50f);
        float p75 = bmeans.empty() ? 0.0f : quick_pct(bmeans, 0.75f);

        std::vector<float> hh;
        double grad_sq = 0.0;
        int64_t grad_cnt = 0;
        float inv_span = 1.0f / std::max(1.0f, uf.white - avg_black);
        for (int r = 0; r + 2 < uf.H; r += 8) {
            for (int c = 1; c + 2 < uf.W; c += 8) {
                float g00 = (float)uf.bayer[(size_t)r * uf.W + c];
                float g01 = (float)uf.bayer[(size_t)r * uf.W + c + 2];
                float g10 = (float)uf.bayer[(size_t)(r + 2) * uf.W + c];
                float g11 = (float)uf.bayer[(size_t)(r + 2) * uf.W + c + 2];
                hh.push_back(std::fabs(0.5f * (g00 - g01 - g10 + g11)));
                double gx = (double)(g01 - g00) * inv_span;
                double gy = (double)(g10 - g00) * inv_span;
                grad_sq += gx * gx + gy * gy;
                ++grad_cnt;
            }
        }
        d.med_sig = p50;
        d.block_iqr = p75 - p25;
        d.hf_sigma = std::max(1.0f, hh.empty() ? 1.0f : (quick_pct(hh, 0.50f) / 0.6745f));
        d.acutance = std::sqrt(grad_sq / (double)std::max<int64_t>(1, grad_cnt)) * 1000.0;
        max_iqr = std::max(max_iqr, d.block_iqr);
    }

    // 2. Build fast 1/8 + 1/4 Green telemetry & automatically filter zero-light lens-cap dark frames (SUIT_DARK)
    std::vector<FastThumb> thumbs(n_total);
    for (int i = 0; i < n_total; ++i) thumbs[i] = build_fast_thumb(uframes[i]);

    std::vector<int> dark_idx, light_raw;
    for (int i = 0; i < n_total; ++i) {
        // Guard against high-ISO night-sky noise self-triggering: never flag frames containing real optical stars,
        // and only apply mixed-cluster relative blank check when another frame has structured daytime IQR (>1.8*hf_sigma)
        bool is_blank = thumbs[i].is_dark_frame ||
                        ((n_total >= 2) &&
                         (thumbs[i].stars_snr65_cnt == 0 && thumbs[i].max_star_snr < 7.5f) &&
                         (max_iqr > std::max(30.0f, 1.8f * res.diags[i].hf_sigma)) &&
                         (std::fabs(res.diags[i].med_sig) < 0.25f * res.diags[i].hf_sigma) &&
                         (res.diags[i].block_iqr < std::max(6.0f, 0.20f * res.diags[i].hf_sigma)));
        if (is_blank) {
            res.diags[i].flags |= SUIT_DARK;
            dark_idx.push_back(i);
        } else {
            // On dark night-sky frames, blend acutance with optical star peak flux so star-trailed frames rank lower than pinpoint stars
            if (thumbs[i].p50_norm < 0.045f && thumbs[i].max_star_snr >= 12.0f && thumbs[i].top3_star_flux > 0.0f) {
                res.diags[i].acutance = 0.35 * res.diags[i].acutance + 250.0 * (double)thumbs[i].top3_star_flux;
            }
            light_raw.push_back(i);
        }
    }

    if (light_raw.empty()) {
        res.condition_code = "all_dark_frames";
        res.condition_summary = "All frames in cluster are zero-light dark frames (lens cap closed); skipped.";
        return res;
    }

    // 3. Majority sensor dimensions check
    int maj_H = uframes[light_raw[0]].H;
    int maj_W = uframes[light_raw[0]].W;
    {
        int best_cnt = 0;
        for (int idx : light_raw) {
            int c = 0;
            for (int j : light_raw) if (uframes[j].H == uframes[idx].H && uframes[j].W == uframes[idx].W) ++c;
            if (c > best_cnt) { best_cnt = c; maj_H = uframes[idx].H; maj_W = uframes[idx].W; }
        }
    }

    std::vector<int> same_dim_lights;
    for (int idx : light_raw) {
        if (uframes[idx].H != maj_H || uframes[idx].W != maj_W) {
            res.diags[idx].flags |= SUIT_DIM_MISMATCH;
        } else {
            same_dim_lights.push_back(idx);
        }
    }
    if (same_dim_lights.empty()) return res;

    // 4. Split into scene-coherent segments by visual / star-centroid overlap
    std::vector<std::vector<int>> raw_segments;
    for (int idx : same_dim_lights) {
        if (raw_segments.empty()) {
            raw_segments.push_back({idx});
            res.diags[idx].zncc = 1.0f;
            continue;
        }
        int seg_anchor = raw_segments.back()[0];
        int seg_prev = raw_segments.back().back();
        float dx = 0, dy = 0, sh = 0, ncc = 0, sp = 0;
        measure_pair_motion_1x(thumbs[seg_anchor], thumbs[idx], dx, dy, sh, ncc, sp);
        res.diags[idx].dx_1x = dx;
        res.diags[idx].dy_1x = dy;
        res.diags[idx].shift_1x = sh;
        res.diags[idx].zncc = ncc;
        res.diags[idx].local_spread_1x = sp;

        // Also check step-to-step continuity for smooth camera panning in multi-group sequences
        float s_dx = 0, s_dy = 0, s_sh = 0, s_ncc = 0, s_sp = 0;
        measure_pair_motion_1x(thumbs[seg_prev], thumbs[idx], s_dx, s_dy, s_sh, s_ncc, s_sp);

        float eff_max_sh = (thumbs[seg_anchor].stars_1x.size() >= 2 && thumbs[seg_anchor].max_star_snr >= 10.0f)
            ? std::max(max_shift_px, 360.0f) : max_shift_px;
        bool coherent_to_anchor = (ncc >= 0.36f && sh <= eff_max_sh && sp <= 38.0f);
        if (coherent_to_anchor) {
            raw_segments.back().push_back(idx);
        } else if ((is_batch_scan || policy == ExecPolicy::MULTI_GROUP || same_dim_lights.size() >= 8) && s_ncc >= 0.36f && s_sh <= eff_max_sh) {
            // Start a fresh super-resolution group when panning drifts beyond anchor window
            res.diags[idx].dx_1x = 0; res.diags[idx].dy_1x = 0; res.diags[idx].shift_1x = 0; res.diags[idx].zncc = 1.0f;
            raw_segments.push_back({idx});
        } else if (is_batch_scan || policy == ExecPolicy::MULTI_GROUP) {
            // Distinct scene inside a scanned folder -> start new segment
            res.diags[idx].dx_1x = 0; res.diags[idx].dy_1x = 0; res.diags[idx].shift_1x = 0; res.diags[idx].zncc = 1.0f;
            raw_segments.push_back({idx});
        } else {
            // Single-burst mode: mark frame as MOTION_TOO_LARGE outlier relative to burst anchor
            res.diags[idx].flags |= SUIT_MOTION_TOO_LARGE;
            res.overall_motion = MotionRegime::TOO_LARGE_PARTIAL;
        }
    }

    // 5. Within each coherent segment, evaluate exposure & acutance suitability and partition groups
    const int grp_sz = std::max(4, target_group_size > 0 ? target_group_size : 8);
    std::vector<PlannedGroup> clean_groups_4plus;
    std::vector<int> all_clean_single;
    int dropped_anomalies = (int)(light_raw.size() - same_dim_lights.size());
    for (int idx : same_dim_lights) if (res.diags[idx].flags != SUIT_OK) ++dropped_anomalies;

    for (const auto& seg : raw_segments) {
        if (seg.empty()) continue;
        std::vector<float> meds, acuts;
        for (int idx : seg) {
            meds.push_back(std::max(1.0f, res.diags[idx].med_sig));
            acuts.push_back((float)res.diags[idx].acutance);
        }
        float med_exp = quick_pct(meds, 0.50f);
        float med_acu = quick_pct(acuts, 0.50f);

        std::vector<int> seg_clean;
        for (int idx : seg) {
            float eratio = std::max(1.0f, res.diags[idx].med_sig) / std::max(1.0f, med_exp);
            if (med_exp > 25.0f && (eratio < 0.52f || eratio > 1.92f)) {
                res.diags[idx].flags |= SUIT_EXPOSURE_OUTLIER;
                ++dropped_anomalies;
                continue;
            }
            if (seg.size() >= 3 && res.diags[idx].acutance < (double)min_rel_sharpness * (double)med_acu) {
                res.diags[idx].flags |= SUIT_MOTION_BLUR;
                ++dropped_anomalies;
                continue;
            }
            seg_clean.push_back(idx);
        }

        // Remove near-identical phase duplicates ONLY when segment has > grp_sz frames
        if ((int)seg_clean.size() > grp_sz && policy != ExecPolicy::MULTI_GROUP && !is_batch_scan) {
            std::vector<int> dedup;
            for (int idx : seg_clean) {
                bool dup = false;
                if ((int)dedup.size() + ((int)seg_clean.size() - (int)dedup.size()) > grp_sz) {
                    for (int kept : dedup) {
                        float dx = res.diags[idx].dx_1x - res.diags[kept].dx_1x;
                        float dy = res.diags[idx].dy_1x - res.diags[kept].dy_1x;
                        if (std::hypot(dx, dy) < 0.06f && res.diags[idx].acutance <= res.diags[kept].acutance) {
                            dup = true;
                            break;
                        }
                    }
                }
                if (dup && (int)seg_clean.size() > 4) {
                    res.diags[idx].flags |= SUIT_PHASE_REDUNDANT;
                } else {
                    dedup.push_back(idx);
                }
            }
            if (dedup.size() >= 4) seg_clean.swap(dedup);
        }

        for (int idx : seg_clean) all_clean_single.push_back(idx);

        if ((int)seg_clean.size() >= 4) {
            bool want_split = (policy == ExecPolicy::MULTI_GROUP) || (is_batch_scan && (int)seg_clean.size() >= 10);
            if (want_split && (int)seg_clean.size() >= 8 && (int)seg_clean.size() > grp_sz) {
                int n_sub = std::max(1, ((int)seg_clean.size() + grp_sz - 1) / grp_sz);
                while (n_sub > 1 && (int)seg_clean.size() / n_sub < 4) --n_sub;
                for (int g = 0; g < n_sub; ++g) {
                    int s0 = ((int)seg_clean.size() * g) / n_sub;
                    int s1 = ((int)seg_clean.size() * (g + 1)) / n_sub;
                    std::vector<int> sub(seg_clean.begin() + s0, seg_clean.begin() + s1);
                    if (max_frames_cap >= 4 && (int)sub.size() > max_frames_cap) {
                        std::stable_sort(sub.begin(), sub.end(), [&](int a, int b) {
                            return res.diags[a].acutance > res.diags[b].acutance;
                        });
                        sub.resize(max_frames_cap);
                        std::sort(sub.begin(), sub.end());
                    }
                    clean_groups_4plus.push_back(finalize_planned_group(sub, dark_idx, uframes, res.diags, thumbs, anchor_mode));
                }
            } else {
                std::vector<int> sub = seg_clean;
                if (max_frames_cap > 0 && (int)sub.size() > max_frames_cap) {
                    std::stable_sort(sub.begin(), sub.end(), [&](int a, int b) {
                        return res.diags[a].acutance > res.diags[b].acutance;
                    });
                    sub.resize(max_frames_cap);
                    std::sort(sub.begin(), sub.end());
                }
                clean_groups_4plus.push_back(finalize_planned_group(sub, dark_idx, uframes, res.diags, thumbs, anchor_mode));
            }
        }
    }

    if (!clean_groups_4plus.empty() && res.overall_motion != MotionRegime::TOO_LARGE_PARTIAL) {
        res.overall_motion = clean_groups_4plus[0].motion;
    } else if (same_dim_lights.size() == 1) {
        res.overall_motion = MotionRegime::SINGLE_FRAME;
    }

    auto mark_assigned = [&](const std::vector<PlannedGroup>& grps) {
        for (auto& d : res.diags) d.assigned_group = -1;
        for (size_t g = 0; g < grps.size(); ++g) {
            for (int idx : grps[g].light_uframe_indices) res.diags[idx].assigned_group = (int)g;
        }
    };

    // 5b. 5D Scene Diagnosis & Smart Prescription Engine (Wizard 2.0)
    {
        ScenePrescription& rx = res.rx;
        rx.est_dng_mb = (double)maj_H * 2.0 * (double)maj_W * 2.0 * 2.0 * 0.39 / (1024.0 * 1024.0);

        // Crown sharpest frame & Frame-1 shutter-shock check
        int crown_idx = same_dim_lights[0];
        for (int idx : same_dim_lights) {
            if ((res.diags[idx].flags == SUIT_OK) &&
                (res.diags[crown_idx].flags != SUIT_OK || res.diags[idx].acutance > res.diags[crown_idx].acutance)) {
                crown_idx = idx;
            }
        }
        res.diags[crown_idx].is_crown = true;
        rx.best_anchor_1based = crown_idx + 1;
        if (same_dim_lights.size() >= 3) {
            int f0 = same_dim_lights[0], f1 = same_dim_lights[1];
            if (res.diags[f0].acutance < 0.88 * res.diags[f1].acutance) {
                res.diags[f0].shutter_shock = true;
                rx.shutter_shock_first = true;
            }
        }

        // Exposure spread (AEB bracket check) & ISO / Shutter telemetry
        float sig_min = 1e30f, sig_max = 0.0f, max_iso = 0.0f, max_shut = 0.0f;
        for (int idx : same_dim_lights) {
            float s = std::max(1.0f, res.diags[idx].med_sig);
            sig_min = std::min(sig_min, s);
            sig_max = std::max(sig_max, s);
            max_iso = std::max(max_iso, res.diags[idx].iso);
            max_shut = std::max(max_shut, res.diags[idx].shutter);
        }
        if (sig_max > 25.0f && sig_min > 1.0f) {
            rx.max_ev_spread = std::log2(sig_max / sig_min);
        }

        // Pinpoint star count & upper/lower horizon split from crown frame's 1/4-scale matched-filter star centroids
        float tp50 = thumbs[crown_idx].p50_norm;
        int vis_stars = 0, upper_stars = 0, lower_stars = 0;
        for (const auto& st : thumbs[crown_idx].stars_1x) {
            if (st.snr < 5.5f) continue;
            ++vis_stars;
            if (st.y_1x < 0.62f * (float)uframes[crown_idx].H) ++upper_stars;
            else if (st.y_1x > 0.70f * (float)uframes[crown_idx].H) ++lower_stars;
        }
        rx.star_count = std::max(thumbs[crown_idx].stars_snr65_cnt, vis_stars);

        // Local moving-object area % across burst thumbnails (aligned by global dx/dy)
        const PlannedGroup* ref_grp = clean_groups_4plus.empty() ? nullptr : &clean_groups_4plus[0];
        if (ref_grp) rx.quad_coverage = ref_grp->quad_coverage;
        if (same_dim_lights.size() >= 2 && thumbs[crown_idx].H >= 24 && thumbs[crown_idx].W >= 24) {
            const FastThumb& t0 = thumbs[crown_idx];
            int t1_idx = (same_dim_lights[0] == crown_idx) ? same_dim_lights[1] : same_dim_lights[0];
            const FastThumb& t1 = thumbs[t1_idx];
            float dx_t = 0, dy_t = 0, sh_t = 0, ncc_t = 0, sp_t = 0;
            measure_pair_motion_1x(t0, t1, dx_t, dy_t, sh_t, ncc_t, sp_t);
            int idy = (int)std::lrintf(dy_t / 8.0f), idx_x = (int)std::lrintf(dx_t / 8.0f);
            float noise_thr = std::max(0.020f, 4.5f * (res.diags[crown_idx].hf_sigma / std::max(1.0f, (float)uframes[crown_idx].white)));
            int mov_cnt = 0, tot_cnt = 0;
            for (int r = 4; r + 4 < t0.H; r += 2) {
                int rr = r + idy;
                if (rr < 1 || rr + 1 >= t1.H) continue;
                for (int c = 4; c + 4 < t0.W; c += 2) {
                    int cc = c + idx_x;
                    if (cc < 1 || cc + 1 >= t1.W) continue;
                    float g0 = t0.img[(size_t)r * t0.W + c];
                    float gx = 0.5f * std::fabs(t0.img[(size_t)r * t0.W + c + 1] - t0.img[(size_t)r * t0.W + c - 1]);
                    float gy = 0.5f * std::fabs(t0.img[(size_t)(r + 1) * t0.W + c] - t0.img[(size_t)(r - 1) * t0.W + c]);
                    float diff = std::fabs(t1.img[(size_t)rr * t1.W + cc] - g0) - 0.8f * (gx + gy);
                    if (diff > noise_thr) ++mov_cnt;
                    ++tot_cnt;
                }
            }
            if (tot_cnt > 0) rx.motion_area_pct = 100.0f * (float)mov_cnt / (float)tot_cnt;
        }

        // Classify 5D Scene & Compute Optimal Parameter Prescription (supports both wide-field & ultra-high-ISO telephoto astro!)
        bool is_astro_scene = (tp50 < 0.045f && thumbs[crown_idx].p75_norm < 0.095f &&
                               (thumbs[crown_idx].stars_snr65_cnt >= 3 ||
                                (thumbs[crown_idx].stars_snr65_cnt >= 2 && thumbs[crown_idx].max_star_snr >= 12.0f) ||
                                thumbs[crown_idx].max_star_snr >= 22.0f)) ||
                              (thumbs[crown_idx].stars_snr65_cnt >= 8 && tp50 < 0.095f);
        if (rx.max_ev_spread >= 0.85f && !is_astro_scene) {
            rx.scene_code = "aeb_bracket";
            rx.rec_preset = "photo";
            rx.rec_anchor = "auto";
            rx.rec_denoise = 1.00f;
            rx.rec_deghost = 1.25f;
            rx.rec_psf_iters = 4;
            rx.sr_score = 55;
            rx.summary_en = "Detected exposure bracketing (AEB spread > 0.85 EV); pruning exposure outliers prevents ghost halos.";
        } else if (is_astro_scene) {
            bool has_horizon = (upper_stars >= 5 && lower_stars * 4 <= upper_stars);
            rx.scene_code = has_horizon ? "astro_landscape" : "astro_sky";
            rx.rec_preset = "astro";
            rx.rec_anchor = "middle";
            rx.rec_denoise = (max_iso >= 25600.0f) ? 1.15f : 0.85f;
            rx.rec_deghost = 0.85f;
            rx.rec_psf_iters = 3;
            rx.sr_score = std::min(98, 72 + rx.quad_coverage * 6);
            rx.summary_en = has_horizon
                ? "Detected starry landscape: enables Astro preset (star-eater guard, star color lock, dual-domain sky/ground alignment, middle-frame pose)."
                : "Detected handheld starfield: enables Astro preset (matched-filter star alignment, star color preservation, dark-sky pedestal lock).";
        } else if (ref_grp && ref_grp->motion == MotionRegime::TOO_SMALL_TRIPOD) {
            rx.scene_code = "tripod_static";
            rx.rec_preset = "photo";
            rx.rec_anchor = "auto";
            rx.rec_denoise = 0.75f;
            rx.rec_deghost = 1.00f;
            rx.rec_psf_iters = 5;
            rx.sr_score = 68;
            rx.summary_en = "Locked tripod burst (shift < 0.15 px): high multi-frame SNR enables low-phase cross-channel detail + 5-iter PSF recovery.";
        } else if (rx.motion_area_pct >= 8.5f) {
            rx.scene_code = "action_subject";
            rx.rec_preset = "photo";
            rx.rec_anchor = std::to_string(rx.best_anchor_1based);
            rx.rec_denoise = 1.05f;
            rx.rec_deghost = 1.20f;
            rx.rec_psf_iters = 4;
            rx.sr_score = std::min(95, 65 + rx.quad_coverage * 7);
            rx.summary_en = "Static background + moving subject detected: anchors sharpest pose frame with rigid micro-patch SR & anti-hollow matte.";
        } else if (max_iso >= 3200.0f) {
            rx.scene_code = "lowlight_high_iso";
            rx.rec_preset = "photo";
            rx.rec_anchor = "auto";
            rx.rec_denoise = 1.25f;
            rx.rec_deghost = 1.05f;
            rx.rec_psf_iters = 3;
            rx.sr_score = std::min(94, 66 + rx.quad_coverage * 7);
            rx.summary_en = "High-ISO low-light handheld burst: boosts GAT-DCT denoise to 1.25x while temporal gradient coherence protects micro-textures.";
        } else {
            rx.scene_code = "handheld_standard";
            rx.rec_preset = "photo";
            rx.rec_anchor = "auto";
            rx.rec_denoise = 1.00f;
            rx.rec_deghost = 1.00f;
            rx.rec_psf_iters = 4;
            rx.sr_score = std::min(98, 66 + rx.quad_coverage * 8);
            rx.summary_en = "Optimal handheld subpixel burst: full 2x anisotropic de-aliasing + temporal coherence micro-texture preservation.";
        }
    }

    // 6. Apply Execution Policy
    if (policy == ExecPolicy::FORCE_ALL) {
        std::vector<int> keep = same_dim_lights;
        if (max_frames_cap > 0 && (int)keep.size() > max_frames_cap) {
            std::stable_sort(keep.begin(), keep.end(), [&](int a, int b) {
                return res.diags[a].acutance > res.diags[b].acutance;
            });
            keep.resize(max_frames_cap);
            std::sort(keep.begin(), keep.end());
        }
        res.selected_groups = {finalize_planned_group(keep, dark_idx, uframes, res.diags, thumbs, anchor_mode)};
        res.condition_code = "force_all";
        res.condition_summary = "Force-all policy active: retained all readable same-dimension light frames.";
        mark_assigned(res.selected_groups);
        return res;
    }

    if (policy == ExecPolicy::MULTI_GROUP) {
        res.selected_groups = clean_groups_4plus;
        res.condition_code = res.selected_groups.empty() ? "no_valid_group_4plus" : "multi_group_ok";
        res.condition_summary = res.selected_groups.empty()
            ? "No super-resolvable group with >= 4 suitable frames found."
            : ("Discovered " + std::to_string(res.selected_groups.size()) + " super-resolvable group(s) (>= 4 valid frames each).");
        mark_assigned(res.selected_groups);
        return res;
    }

    if (policy == ExecPolicy::AUTO_PRUNE) {
        if (is_batch_scan) {
            res.selected_groups = clean_groups_4plus;
        } else if (!clean_groups_4plus.empty()) {
            // Single-output invocation: pick the highest-acutance valid group
            size_t best_g = 0;
            for (size_t g = 1; g < clean_groups_4plus.size(); ++g) {
                if (clean_groups_4plus[g].mean_acutance > clean_groups_4plus[best_g].mean_acutance) best_g = g;
            }
            res.selected_groups = {clean_groups_4plus[best_g]};
        }
        res.condition_code = res.selected_groups.empty() ? "insufficient_suitable_frames" : "auto_prune_ok";
        res.condition_summary = res.selected_groups.empty()
            ? ("Fewer than 4 suitable frames remain (" + std::to_string(all_clean_single.size()) +
               " valid of " + std::to_string(same_dim_lights.size()) + " light frames). Use --policy interactive or --policy force-all.")
            : "Auto-prune policy selected clean burst frames (>= 4).";
        mark_assigned(res.selected_groups);
        return res;
    }

    // --- ExecPolicy::INTERACTIVE (At Most 2 Recommended Options!) ---
    // Case 1: Multiple groups or long continuous sequence (> 8 clean frames)
    if (clean_groups_4plus.size() >= 2) {
        res.needs_interactive_prompt = true;
        res.condition_code = "long_or_multi_burst";
        res.condition_summary = "Detected " + std::to_string(clean_groups_4plus.size()) +
            " distinct super-resolvable burst groups (>= 4 clean frames each).";

        InteractiveChoicePlan c1;
        c1.id = 1;
        c1.title = "Export all " + std::to_string(clean_groups_4plus.size()) + " valid super-resolvable groups (Recommended)";
        c1.detail = "Processes every valid >=4-frame group with automatic outlier pruning.";
        c1.groups = clean_groups_4plus;

        size_t best_g = 0;
        for (size_t g = 1; g < clean_groups_4plus.size(); ++g) {
            if (clean_groups_4plus[g].mean_acutance > clean_groups_4plus[best_g].mean_acutance) best_g = g;
        }
        InteractiveChoicePlan c2;
        c2.id = 2;
        c2.title = "Export only the single sharpest burst group (" +
                   std::to_string(clean_groups_4plus[best_g].light_uframe_indices.size()) + " frames)";
        c2.detail = "Produces one 2x output from the highest-acutance coherent group.";
        c2.groups = {clean_groups_4plus[best_g]};

        res.options = {c1, c2};
        res.selected_groups = c1.groups;
        mark_assigned(res.selected_groups);
        return res;
    }

    if (clean_groups_4plus.size() == 1 && clean_groups_4plus[0].light_uframe_indices.size() >= 10 && max_frames_cap == 0) {
        const auto& g0 = clean_groups_4plus[0];
        int n_clean = (int)g0.light_uframe_indices.size();
        res.needs_interactive_prompt = true;
        res.condition_code = "long_single_burst";
        res.condition_summary = "Long coherent burst of " + std::to_string(n_clean) + " clean frames detected.";

        // Option 1: Split into multiple balanced >=4-frame groups OR cap to sharpest 8
        InteractiveChoicePlan c1;
        c1.id = 1;
        int n_sub = n_clean / grp_sz;
        if (n_sub >= 2 && n_clean / n_sub >= 4) {
            c1.title = "Partition into " + std::to_string(n_sub) + " balanced burst groups (Recommended for multi-shot)";
            c1.detail = "Splits the long burst into sequential high-SNR super-resolution groups.";
            for (int k = 0; k < n_sub; ++k) {
                int s0 = (n_clean * k) / n_sub;
                int s1 = (n_clean * (k + 1)) / n_sub;
                std::vector<int> sub(all_clean_single.begin() + s0, all_clean_single.begin() + s1);
                c1.groups.push_back(finalize_planned_group(sub, dark_idx, uframes, res.diags, thumbs, anchor_mode));
            }
        } else {
            std::vector<int> top8 = all_clean_single;
            std::stable_sort(top8.begin(), top8.end(), [&](int a, int b) {
                return res.diags[a].acutance > res.diags[b].acutance;
            });
            top8.resize(8);
            c1.title = "Select the 8 sharpest frames for optimal single-image SR (Recommended)";
            c1.detail = "Captures full subpixel sweet-spot quality without diminishing-return runtime.";
            c1.groups = {finalize_planned_group(top8, dark_idx, uframes, res.diags, thumbs, anchor_mode)};
        }

        InteractiveChoicePlan c2;
        c2.id = 2;
        c2.title = "Fuse all " + std::to_string(n_clean) + " clean frames into a single ultra-low-noise output";
        c2.detail = "Maximizes deep-shadow shot-noise reduction at higher runtime cost.";
        c2.groups = {g0};

        res.options = {c1, c2};
        res.selected_groups = c1.groups;
        mark_assigned(res.selected_groups);
        return res;
    }

    // Case 2: Insufficient clean frames (< 4)
    if (clean_groups_4plus.empty()) {
        res.needs_interactive_prompt = true;
        if (same_dim_lights.size() >= 4) {
            res.condition_code = "anomalies_reduced_below_4";
            res.condition_summary = "Strict suitability rejected " + std::to_string(dropped_anomalies) +
                " anomalous/blurred/shifted frame(s), leaving " + std::to_string(all_clean_single.size()) +
                " (< 4 minimum).";

            // Rank same_dim_lights by quality (preferring OK frames, then highest zncc * acutance)
            std::vector<int> top4 = same_dim_lights;
            std::stable_sort(top4.begin(), top4.end(), [&](int a, int b) {
                bool oka = (res.diags[a].flags == SUIT_OK);
                bool okb = (res.diags[b].flags == SUIT_OK);
                if (oka != okb) return oka > okb;
                return (res.diags[a].acutance * std::max(0.1f, res.diags[a].zncc)) >
                       (res.diags[b].acutance * std::max(0.1f, res.diags[b].zncc));
            });
            top4.resize(4);

            InteractiveChoicePlan c1;
            c1.id = 1;
            c1.title = "Retain top 4 best-aligned frames to preserve 4-frame 2x phase coverage (Recommended)";
            c1.detail = "Relaxes blur/motion threshold on borderline frames with pixel-wise de-ghosting.";
            c1.groups = {finalize_planned_group(top4, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

            std::vector<int> strict_keep = all_clean_single.empty()
                ? std::vector<int>{top4[0]}
                : all_clean_single;
            InteractiveChoicePlan c2;
            c2.id = 2;
            c2.title = "Force-execute strictly on the " + std::to_string(strict_keep.size()) + " cleanest frame(s) (< 4 fallback)";
            c2.detail = "Prioritizes zero outlier contamination; spatial 2x resolution degrades gracefully.";
            c2.groups = {finalize_planned_group(strict_keep, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

            res.options = {c1, c2};
            res.selected_groups = c1.groups;
        } else {
            res.condition_code = "under_4_input_frames";
            res.condition_summary = "Only " + std::to_string(same_dim_lights.size()) +
                " light frame(s) provided (algorithm requires >= 4 frames for full 2x super-resolution).";

            InteractiveChoicePlan c1;
            c1.id = 1;
            c1.title = "Force-execute degraded " + std::to_string(same_dim_lights.size()) + "-frame fusion (Recommended if no more frames)";
            c1.detail = "Runs anisotropic 2x reconstruction + GAT-DCT denoise on available frame(s).";
            c1.groups = {finalize_planned_group(same_dim_lights, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

            InteractiveChoicePlan c2;
            c2.id = 2;
            c2.title = "Cancel execution now (add >= 4 burst frames)";
            c2.detail = "Aborts cleanly without writing partial-resolution output.";
            c2.aborts = true;

            res.options = {c1, c2};
            res.selected_groups = c1.groups;
        }
        mark_assigned(res.selected_groups);
        return res;
    }

    // Case 3: >= 4 clean frames remain, check if anomalies were dropped OR motion is tripod-locked too small!
    const PlannedGroup& g_clean = clean_groups_4plus[0];
    if (dropped_anomalies > 0) {
        res.needs_interactive_prompt = true;
        res.condition_code = "anomalous_frames_detected";
        res.condition_summary = "Detected " + std::to_string(dropped_anomalies) +
            " unsuitable frame(s) (blur / large motion / exposure mismatch); " +
            std::to_string(g_clean.light_uframe_indices.size()) + " clean frames remain.";

        InteractiveChoicePlan c1;
        c1.id = 1;
        c1.title = "Auto-prune " + std::to_string(dropped_anomalies) + " unsuitable frame(s) and fuse " +
                   std::to_string(g_clean.light_uframe_indices.size()) + " clean frames (Recommended)";
        c1.detail = "Prevents blurred or mis-exposed frames from softening fine details.";
        c1.groups = {g_clean};

        InteractiveChoicePlan c2;
        c2.id = 2;
        c2.title = "Force-keep all " + std::to_string(same_dim_lights.size()) + " same-dimension frames";
        c2.detail = "Includes flagged frames using pixel-level motion de-ghosting weights.";
        c2.groups = {finalize_planned_group(same_dim_lights, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

        res.options = {c1, c2};
        res.selected_groups = c1.groups;
        mark_assigned(res.selected_groups);
        return res;
    }

    if (g_clean.motion == MotionRegime::TOO_SMALL_TRIPOD) {
        res.needs_interactive_prompt = true;
        res.condition_code = "motion_too_small";
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "Inter-frame motion is very small (max_shift=%.2f px < 0.15 px, likely locked tripod): "
            "temporal denoising is effective, while subpixel 2x phase diversity is limited.",
            g_clean.max_shift_1x);
        res.condition_summary = buf;

        InteractiveChoicePlan c1;
        c1.id = 1;
        c1.title = "Continue fusion for multi-frame noise reduction + 2x upscale (Recommended)";
        c1.detail = "Produces ultra-clean low-noise 2x output from static burst.";
        c1.groups = {g_clean};

        InteractiveChoicePlan c2;
        c2.id = 2;
        c2.title = "Cancel execution (re-shoot handheld / IBIS shift for full subpixel aliasing removal)";
        c2.detail = "Aborts cleanly without writing output.";
        c2.aborts = true;

        res.options = {c1, c2};
        res.selected_groups = c1.groups;
        mark_assigned(res.selected_groups);
        return res;
    }

    // Case 4: Clean 4..9 frame burst -> prompt if Astro scene mismatch, otherwise populate wizard options cleanly
    if (res.rx.rec_preset == "astro" && current_preset != "astro") {
        res.needs_interactive_prompt = true;
        res.condition_code = "astro_scene_detected";
        res.condition_summary = res.rx.summary_en;

        std::vector<int> clean_seq;
        for (int idx : same_dim_lights) if (res.diags[idx].flags == SUIT_OK) clean_seq.push_back(idx);
        PlannedGroup g_mid = finalize_planned_group(clean_seq, dark_idx, uframes, res.diags, thumbs, -2);

        InteractiveChoicePlan c1;
        c1.id = 1;
        c1.title = "Switch to Astro preset + middle-frame horizon anchor (Recommended)";
        c1.detail = "Activates star-eater protection, star color lock, and dual-domain sky/ground alignment.";
        c1.groups = {g_mid};

        InteractiveChoicePlan c2;
        c2.id = 2;
        c2.title = "Keep current preset (" + current_preset + ") and fuse " + std::to_string(g_clean.light_uframe_indices.size()) + " frames";
        c2.detail = "Runs standard daytime pipeline parameters without astro star protections.";
        c2.groups = {g_clean};

        res.options = {c1, c2};
        res.selected_groups = c1.groups;
        mark_assigned(res.selected_groups);
        return res;
    }

    res.needs_interactive_prompt = false;
    res.condition_code = "ok";
    res.condition_summary = res.rx.summary_en;
    res.selected_groups = {g_clean};
    {
        InteractiveChoicePlan c1;
        c1.id = 1;
        c1.title = "Apply recommended " + res.rx.rec_preset + " prescription (" + std::to_string(g_clean.light_uframe_indices.size()) + " frames)";
        c1.detail = res.rx.summary_en;
        c1.groups = {g_clean};
        res.options = {c1};
    }
    mark_assigned(res.selected_groups);
    return res;
}

} // namespace heraw_plan
