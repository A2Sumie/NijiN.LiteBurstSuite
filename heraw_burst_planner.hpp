// heraw_burst_planner.hpp -- Batch folder scanner, EXIF time sorter, conditional frame/motion
// suitability analyzer, multi-group partitioner, and <=2-option interactive advisor.
// Pure 7-bit ASCII C++20 header.
#pragma once

#include "heraw_fs_utf8.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <thread>
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
#include "heraw_suite_measurement.hpp"
#include "heraw_wvdc_router.hpp"

namespace heraw_plan {

enum class ExecPolicy {
    AUTO_PRUNE,  // Auto-reject unsuitable frames; require >= 4 valid frames per group
    MULTI_GROUP, // Partition long sequences / folders into all valid >= 4-frame groups
    FORCE_ALL,   // Force execution down to 1 frame, bypassing <4 & motion blocks
    INTERACTIVE  // Present at most 2 recommended options on anomalies / <4 / long bursts
};

struct PlanDepthGate {
    int mode = 0;                      // 0 = auto, 1 = relaxed, 2 = force
    float min_disparity_1x_px = -1.0f; // <0 uses mode default
    float min_coherence = -1.0f;       // <0 uses mode default
    float min_support = -1.0f;         // <0 uses mode default
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
    float focus_distance_m = 0.0f;
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
    double af_acutance = 0.0;
    int af_score = 80;       // 0..99 normalized sharpness bar for GUI Frame Inspector
    float shift_1x = 0.0f;
    float dx_1x = 0.0f;
    float dy_1x = 0.0f;
    float zncc = 1.0f;
    float local_spread_1x = 0.0f;
    uint32_t flags = SUIT_OK;
    int assigned_group = -1; // -1 = unassigned/dropped, >=0 = group index
    bool is_crown = false;
    bool shutter_shock = false;
    float clipped_fraction = 0.0f;
};

struct ScenePrescription {
    std::string scene_code = "handheld_standard"; // handheld_standard | tripod_static | action_subject | lowlight_high_iso | astro_sky | astro_landscape | aeb_bracket
    std::string rec_preset = "photo";             // photo | sci | arc | astro
    std::string rec_anchor = "auto";              // auto | first | middle | <1..N>
    std::string cfa_type = "bayer";               // bayer | xtrans
    std::string olpf_auto = "none";               // none | weak4 | gauss16
    std::string shutter_type = "unknown";         // unknown | mech | elec | global
    bool has_ois = false;
    int best_anchor_1based = 1;                   // 1-based index in cluster
    int quad_coverage = 1;                        // 1..4 2x2 subpixel quadrants
    int sr_score = 75;                            // 0..100 super-resolution potential score
    int star_count = 0;                           // detected pinpoint stars on ref frame
    float motion_area_pct = 0.0f;                 // % of area with local moving subjects
    float max_ev_spread = 0.0f;                   // max exposure spread in EV stops
    float max_drift_2x_px = 0.0f;                 // max inter-frame shift in 2x pixels (for W5/W9 gates)
    float max_parallax_2x_px = 0.0f;              // estimated epipolar parallax span in 2x pixels (for L1/W6/W7 >=8px gate)
    float parallax_coherence = 0.0f;              // rank-1 epipolar parallax coherence in [0..1]
    float af_norm_x = -1.0f;
    float af_norm_y = -1.0f;
    float aperture = 0.0f;
    float shutter_s = 0.0f;
    float focus_distance_m = 0.0f;
    float focal_opt_mm = 0.0f;
    float focus_distance_low_m = 0.0f;
    float focus_distance_high_m = 0.0f;
    float dof_near_m = 0.0f;
    float dof_far_m = 0.0f;
    float hyperfocal_m = 0.0f;
    bool focus_is_infinity = false;
    bool infinity_in_dof = false;
    bool depth_observable = false;
    bool has_focus_steps = false;
    float rec_denoise = 1.00f;
    float rec_deghost = 1.00f;
    int rec_psf_iters = 4;
    double est_dng_mb = 0.0;                      // estimated 2x lossless DNG size (MiB)
    bool shutter_shock_first = false;
    float phi_schur_2d = 0.0f;                    // A14 4-alias Bayer Schur completeness in [0..1]
    float phi_schur_x = 0.0f;                     // 1D horizontal alias completeness in [0..1]
    float phi_schur_y = 0.0f;                     // 1D vertical alias completeness in [0..1]
    float snr_gain_db_est = 0.0f;                 // 10*log10(N_eff) shadow/midtone SNR gain (dB)
    std::string prelim_suitability = "INSUFFICIENT_EVIDENCE";
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
    float phi_schur_2d = 0.0f;             // A14 4-alias Schur completeness in [0..1]
    float phi_schur_x = 0.0f;
    float phi_schur_y = 0.0f;
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
    std::string base;
    uint32_t fidx = 0u;
    bool has_frame = heraw_wvdc::split_virtual_raw_path(p, base, fidx);
    std::string b = basename_of(base);
    size_t dot = b.find_last_of('.');
    std::string stem = (dot == std::string::npos) ? b : b.substr(0, dot);
    if (has_frame) {
        char fbuf[16];
        std::snprintf(fbuf, sizeof(fbuf), "_f%04u", fidx);
        stem += fbuf;
    }
    return stem;
}

static inline bool has_supported_raw_ext(const std::string& path) {
    std::string base;
    uint32_t fidx = 0u;
    heraw_wvdc::split_virtual_raw_path(path, base, fidx);
    std::string b = basename_of(base);
    if (b.empty() || b[0] == '.') return false; // Ignore macOS AppleDouble ._* and hidden sidecars
    size_t dot = b.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = b.substr(dot);
    for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
    static const char* kExts[] = {
        ".nef", ".nrw", ".dng", ".arw", ".cr2", ".cr3",
        ".raf", ".orf", ".rw2", ".pef", ".srw", ".iiq",
        ".nev", ".jxs", ".dxo", ".4rw"
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
            if (heraw_wvdc::lower_ext_of(full) != ".jxs" ||
                heraw_wvdc::is_bayer_codestream_magic(full)) {
                local_files.push_back(std::move(full));
            }
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
            if (heraw_wvdc::lower_ext_of(full) != ".jxs" ||
                heraw_wvdc::is_bayer_codestream_magic(full)) {
                local_files.push_back(std::move(full));
            }
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
    const std::vector<std::string>& paths, bool sort_by_time, int n_threads, ParFn&& par_rows,
    int decoder_pref = 0
) {
    const int n = (int)paths.size();
    std::vector<RawHeaderInfo> infos(n);
    par_rows(n, n_threads, [&](int i0, int i1, int) {
        for (int i = i0; i < i1; ++i) {
            RawHeaderInfo& hi = infos[i];
            hi.path = paths[i];
            hi.orig_order = i;
            {
                std::string base_p;
                uint32_t fidx = 0u;
                heraw_wvdc::split_virtual_raw_path(hi.path, base_p, fidx);
                size_t dot = base_p.find_last_of('.');
                std::string ext = (dot == std::string::npos) ? "" : base_p.substr(dot);
                for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                if (ext == ".dxo" || ext == ".4rw") {
                    heraw_dxo::DxoSharedMeta dxo;
                    if (heraw_dxo::probe_dxo_container(base_p, dxo) && dxo.valid) {
                        hi.ok = true;
                        hi.make = dxo.make;
                        hi.model = dxo.model;
                        hi.width = dxo.out_w & ~1;
                        hi.height = dxo.out_h & ~1;
                        int64_t base_ts = heraw_wvdc::parse_exif_datetime_utc_sec(dxo.datetime);
                        int ms = dxo.subsec_ms;
                        if (fidx < dxo.frame_windows.size()) {
                            int64_t total_ms = (int64_t)ms + (int64_t)std::llround(dxo.frame_windows[fidx].center_rel_s * 1000.0);
                            base_ts += total_ms / 1000;
                            ms = (int)(total_ms % 1000);
                            if (ms < 0) { ms += 1000; --base_ts; }
                        }
                        hi.timestamp = base_ts;
                        hi.subsec_ms = ms;
                        hi.shot_order = fidx;
                        hi.iso = dxo.iso;
                        hi.shutter = dxo.shutter_s;
                        hi.aperture = dxo.aperture;
                        hi.focal_len = dxo.focal_mm;
                    }
                    continue;
                }
            }
#ifdef HERAW_HAS_WVDC
            if (decoder_pref != 2) {
                heraw_wvdc::WvdcHeaderProbe wp;
                if (heraw_wvdc::probe_header_via_wvdc(hi.path, wp)) {
                    hi.ok = true;
                    hi.make = wp.make;
                    hi.model = wp.model;
                    hi.width = wp.width;
                    hi.height = wp.height;
                    hi.timestamp = wp.timestamp;
                    hi.shot_order = wp.shot_order;
                    hi.iso = wp.iso;
                    hi.shutter = wp.shutter;
                    hi.aperture = wp.aperture;
                    hi.focal_len = wp.focal_len;
                    hi.focus_distance_m = wp.focus_is_infinity ? 1000000.0f : wp.focus_distance_m;
                    if (wp.subsec_ms > 0 || heraw_wvdc::is_nev_path(hi.path)) {
                        hi.subsec_ms = wp.subsec_ms;
                    } else {
                        float tmp_fm = 0.0f;
                        heraw_io::read_raw_quick_exif(hi.path, hi.subsec_ms, tmp_fm);
                        if (hi.focus_distance_m <= 0.0f && tmp_fm > 0.0f) hi.focus_distance_m = tmp_fm;
                    }
                    continue;
                }
            }
#else
            (void)decoder_pref;
#endif
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
                    heraw_io::read_raw_quick_exif(hi.path, hi.subsec_ms, hi.focus_distance_m);
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
            heraw_io::read_raw_quick_exif(hi.path, hi.subsec_ms, hi.focus_distance_m);
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
    const std::vector<RawHeaderInfo>& infos, bool split_by_time_and_meta, double burst_gap_s,
    int max_cluster_frames = 128
) {
    const int eff_cluster_cap = std::max(8, std::min(512, max_cluster_frames));
    std::vector<std::vector<RawHeaderInfo>> raw_clusters;
    std::vector<double> cur_dead_dts;
    bool cur_has_subsec = false;

    auto header_dead_dt = [](const RawHeaderInfo& prev, const RawHeaderInfo& cur, bool& has_ts) -> double {
        has_ts = (cur.timestamp > 0 && prev.timestamp > 0);
        if (!has_ts) return 0.0;
        double dt = (double)(cur.timestamp - prev.timestamp) + 0.001 * (double)(cur.subsec_ms - prev.subsec_ms);
        double exp_span = std::max(0.0, (double)std::max(prev.shutter, cur.shutter));
        return std::max(0.0, std::fabs(dt) - exp_span);
    };

    for (const auto& hi : infos) {
        if (!hi.ok) continue;
        if (raw_clusters.empty() || !split_by_time_and_meta) {
            if (raw_clusters.empty()) raw_clusters.emplace_back();
            raw_clusters.back().push_back(hi);
            continue;
        }
        const auto& cur = raw_clusters.back();
        const RawHeaderInfo& prev = cur.back();
        bool same_cam = (hi.make == prev.make && hi.model == prev.model &&
                         hi.width == prev.width && hi.height == prev.height);
        bool same_focal = (hi.focal_len <= 0.0f || prev.focal_len <= 0.0f ||
                           std::fabs(hi.focal_len - prev.focal_len) <= 0.03f * std::max(hi.focal_len, prev.focal_len));

        bool has_ts = false;
        double dead_dt = header_dead_dt(prev, hi, has_ts);
        double exp_span = std::max(0.0, (double)std::max(prev.shutter, hi.shutter));
        // Long exposures (astro / ND / in-camera LENR dark subtraction) need exposure-proportional gap allowance
        double eff_gap_s = (exp_span >= 0.35)
            ? std::max(burst_gap_s, std::min(38.0, 1.05 * exp_span + 3.5))
            : burst_gap_s;
        bool time_close = (!has_ts || dead_dt <= eff_gap_s);

        // Detect deliberate aperture setup switch (> 1 stop) across a pause
        bool same_setup = true;
        if (dead_dt > 0.75 && prev.aperture > 0.5f && hi.aperture > 0.5f) {
            float ar = std::max(prev.aperture, hi.aperture) / std::min(prev.aperture, hi.aperture);
            if (ar > 1.38f) same_setup = false;
        }

        // Detect shutter release & re-compose pause between two distinct high-speed bursts
        bool cadence_continuous = true;
        if (has_ts && exp_span < 0.25 && cur.size() >= 3 && cur_dead_dts.size() >= 2) {
            std::vector<double> tmp = cur_dead_dts;
            std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
            double med_dt = tmp[tmp.size() / 2];
            bool subsec_active = cur_has_subsec || (hi.subsec_ms > 0) || (prev.subsec_ms > 0);
            if (subsec_active) {
                if (med_dt <= 0.28 && dead_dt > std::max(1.20, 6.5 * std::max(0.04, med_dt))) {
                    cadence_continuous = false;
                }
            } else {
                double span_s = (double)(cur.back().timestamp - cur.front().timestamp);
                if (cur.size() >= 4 && span_s <= (double)cur.size() * 0.35 && dead_dt >= 2.05) {
                    cadence_continuous = false;
                }
            }
        }

        bool prev_had_subsec = (prev.subsec_ms > 0);
        bool under_hard_cap = ((int)cur.size() < std::max(512, eff_cluster_cap));
        if (same_cam && same_focal && same_setup && time_close && cadence_continuous && under_hard_cap) {
            raw_clusters.back().push_back(hi);
            if (has_ts) cur_dead_dts.push_back(dead_dt);
            if (hi.subsec_ms > 0 || prev_had_subsec) cur_has_subsec = true;
        } else {
            raw_clusters.push_back({hi});
            cur_dead_dts.clear();
            cur_has_subsec = (hi.subsec_ms > 0);
        }
    }

    if (!split_by_time_and_meta) return raw_clusters;

    // Balance any >eff_cluster_cap continuous burst into memory-bounded chunks without <4-frame orphan tails
    std::vector<std::vector<RawHeaderInfo>> clusters;
    clusters.reserve(raw_clusters.size());
    for (auto& cl : raw_clusters) {
        if ((int)cl.size() <= eff_cluster_cap) {
            clusters.push_back(std::move(cl));
            continue;
        }
        int total = (int)cl.size();
        int n_chunks = (total + eff_cluster_cap - 1) / eff_cluster_cap;
        int start = 0;
        for (int k = 1; k < n_chunks; ++k) {
            int ideal = (total * k) / n_chunks;
            int lo = std::max(start + 8, ideal - 4);
            int hi_idx = std::min(total - (n_chunks - k) * 8, ideal + 4);
            int best_cut = ideal;
            if (lo <= hi_idx) {
                double best_score = -1.0;
                for (int c = lo; c <= hi_idx; ++c) {
                    bool hts = false;
                    double ddt = header_dead_dt(cl[(size_t)c - 1], cl[(size_t)c], hts);
                    double score = ddt - 0.02 * std::abs(c - ideal);
                    if (score > best_score) { best_score = score; best_cut = c; }
                }
            }
            clusters.emplace_back(cl.begin() + start, cl.begin() + best_cut);
            start = best_cut;
        }
        if (start < total) {
            clusters.emplace_back(cl.begin() + start, cl.end());
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
    int sat_count = 0;
    bool is_dark_frame = false;
    std::vector<float> img;
    std::vector<uint8_t> sat_mask;
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
    float b_g = 0.0f;
    int bg_cnt = 0;
    for (int py = 0; py < 6; ++py) {
        for (int px = 0; px < 6; ++px) {
            if (uf.cfa6x6[py][px] == 1) { b_g += uf.black_6x6[py][px]; ++bg_cnt; }
        }
    }
    b_g = (bg_cnt > 0) ? (b_g / (float)bg_cnt) : (0.5f * (uf.black_c[1] + uf.black_c[2]));
    float inv_s = 1.0f / std::max(1.0f, uf.white - b_g);

    // Build 1/4-scale (4x4 1x pixels = 2x2 Green sub-blocks; supports both 2x2 Bayer & 6x6 X-Trans!)
    const int Hq = std::max(1, uf.H / 4), Wq = std::max(1, uf.W / 4);
    std::vector<float> gq((size_t)Hq * Wq, 0.0f), gtrim((size_t)Hq * Wq, 0.0f);
    std::vector<float> samp;
    samp.reserve((size_t)(Hq / 4 + 1) * (Wq / 4 + 1));

    if (!uf.is_xtrans &&
        ((uf.cfa6x6[0][0] == 1) != (uf.cfa6x6[0][1] == 1)) &&
        ((uf.cfa6x6[1][0] == 1) != (uf.cfa6x6[1][1] == 1))) {
        const int g0_dx = (uf.cfa6x6[0][1] == 1) ? 1 : 0;
        const int g1_dx = 1 - g0_dx;
        const float b_sum = uf.black_6x6[0][g0_dx] + uf.black_6x6[1][g1_dx];
        const float half_inv = 0.5f * inv_s;
        for (int r = 0; r < Hq; ++r) {
            const uint16_t* r0_top = uf.bayer.data() + (size_t)(4 * r) * uf.W;
            const uint16_t* r1_top = r0_top + uf.W;
            const uint16_t* r0_bot = r0_top + (size_t)2 * uf.W;
            const uint16_t* r1_bot = r0_bot + uf.W;
            for (int c = 0; c < Wq; ++c) {
                const int cx = 4 * c;
                const float g0 = ((float)r0_top[cx + g0_dx] + (float)r1_top[cx + g1_dx] - b_sum) * half_inv;
                const float g1 = ((float)r0_top[cx + 2 + g0_dx] + (float)r1_top[cx + 2 + g1_dx] - b_sum) * half_inv;
                const float g2 = ((float)r0_bot[cx + g0_dx] + (float)r1_bot[cx + g1_dx] - b_sum) * half_inv;
                const float g3 = ((float)r0_bot[cx + 2 + g0_dx] + (float)r1_bot[cx + 2 + g1_dx] - b_sum) * half_inv;
                const float sum4 = (g0 + g1) + (g2 + g3);
                const float lo = std::min(std::min(g0, g1), std::min(g2, g3));
                const float hi = std::max(std::max(g0, g1), std::max(g2, g3));
                const float v = 0.25f * sum4;
                gq[(size_t)r * Wq + c] = v;
                gtrim[(size_t)r * Wq + c] = 0.5f * (sum4 - lo - hi);
                if (((r | c) & 3) == 0) samp.push_back(v);
            }
        }
    } else {
        for (int r = 0; r < Hq; ++r) {
            for (int c = 0; c < Wq; ++c) {
                float gv[4];
                int k = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    int r_base = 4 * r + 2 * dy;
                    const uint16_t* r0 = uf.bayer.data() + (size_t)r_base * uf.W + 4 * c;
                    const uint16_t* r1 = r0 + uf.W;
                    int pr0 = r_base % 6, pr1 = (r_base + 1) % 6;
                    for (int dx = 0; dx < 2; ++dx) {
                        int c_base = 4 * c + 2 * dx;
                        int pc0 = c_base % 6, pc1 = (c_base + 1) % 6;
                        float gs = 0.0f;
                        int gc = 0;
                        if (uf.cfa6x6[pr0][pc0] == 1) { gs += ((float)r0[2 * dx]     - uf.black_6x6[pr0][pc0]) * inv_s; ++gc; }
                        if (uf.cfa6x6[pr0][pc1] == 1) { gs += ((float)r0[2 * dx + 1] - uf.black_6x6[pr0][pc1]) * inv_s; ++gc; }
                        if (uf.cfa6x6[pr1][pc0] == 1) { gs += ((float)r1[2 * dx]     - uf.black_6x6[pr1][pc0]) * inv_s; ++gc; }
                        if (uf.cfa6x6[pr1][pc1] == 1) { gs += ((float)r1[2 * dx + 1] - uf.black_6x6[pr1][pc1]) * inv_s; ++gc; }
                        gv[k++] = (gc > 0) ? (gs / (float)gc) : 0.0f;
                    }
                }
                const float sum4 = (gv[0] + gv[1]) + (gv[2] + gv[3]);
                const float lo = std::min(std::min(gv[0], gv[1]), std::min(gv[2], gv[3]));
                const float hi = std::max(std::max(gv[0], gv[1]), std::max(gv[2], gv[3]));
                const float v = 0.25f * sum4;
                gq[(size_t)r * Wq + c] = v;
                gtrim[(size_t)r * Wq + c] = 0.5f * (sum4 - lo - hi);
                if (((r | c) & 3) == 0) samp.push_back(v);
            }
        }
    }

    t.sat_mask.assign((size_t)t.H * t.W, 0);
    for (int r = 0; r < t.H; ++r) {
        for (int c = 0; c < t.W; ++c) {
            int r0 = std::min(Hq - 1, 2 * r), r1 = std::min(Hq - 1, 2 * r + 1);
            int c0 = std::min(Wq - 1, 2 * c), c1 = std::min(Wq - 1, 2 * c + 1);
            float v = 0.25f * (
                gq[(size_t)r0 * Wq + c0] + gq[(size_t)r0 * Wq + c1] +
                gq[(size_t)r1 * Wq + c0] + gq[(size_t)r1 * Wq + c1]);
            t.img[(size_t)r * t.W + c] = v;
            if (v >= 0.78f) {
                t.sat_mask[(size_t)r * t.W + c] = 1;
                ++t.sat_count;
            }
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

// Returns shift in 1x sensor pixels (1 thumb pixel = 8 1x pixels), mean ZNCC, non-rigid block spread, and optional rank-1 parallax coherence
static void measure_pair_motion_1x(
    const FastThumb& ref, const FastThumb& cur,
    float& out_dx_1x, float& out_dy_1x, float& out_shift_1x, float& out_zncc, float& out_spread_1x,
    float* out_parallax_coh = nullptr
) {
    out_dx_1x = 0.0f; out_dy_1x = 0.0f; out_shift_1x = 0.0f; out_zncc = 0.0f; out_spread_1x = 0.0f;
    if (out_parallax_coh) *out_parallax_coh = 0.0f;
    if (ref.H != cur.H || ref.W != cur.W || ref.H < 12 || ref.W < 12) return;

    const int H = ref.H, W = ref.W;
    const int bw = std::max(4, std::min(40, std::min(H, W) / 4));
    const int rad = std::max(2, std::min(24, std::min(H, W) / 4)); // +-24 thumb px = +-192 1x pixels!

    struct CandPatch { int y0, x0; double score; double grad_e; double r_mean; double r_var; };
    std::vector<CandPatch> cands;
    const int Np = bw * bw;
    for (int gy = 0; gy < 4; ++gy) {
        int y0 = (H - bw) * (2 * gy + 1) / 8;
        for (int gx = 0; gx < 4; ++gx) {
            int x0 = (W - bw) * (2 * gx + 1) / 8;
            double r_sum = 0.0, r_sq = 0.0, ge = 0.0;
            double sxx = 0.0, syy = 0.0, sxy = 0.0;
            for (int r = 0; r < bw; ++r) {
                const float* rp = ref.img.data() + (size_t)(y0 + r) * W + x0;
                for (int c = 0; c < bw; ++c) {
                    r_sum += rp[c];
                    r_sq += (double)rp[c] * rp[c];
                    if (r + 1 < bw && c + 1 < bw) {
                        double dx = rp[c + 1] - rp[c];
                        double dy = rp[W + c] - rp[c];
                        ge += dx * dx + dy * dy;
                        sxx += dx * dx; syy += dy * dy; sxy += dx * dy;
                    }
                }
            }
            double r_mean = r_sum / Np;
            double r_var = r_sq - Np * r_mean * r_mean;
            if (r_var > 1e-7) {
                double tr = sxx + syy;
                double lam_min = 0.5 * std::max(0.0, tr - std::hypot(sxx - syy, 2.0 * sxy));
                double corner_ratio = (tr > 1e-12) ? std::min(1.0, (2.0 * lam_min) / tr) : 0.0;
                double sc = ge * (0.25 + 0.75 * corner_ratio);
                cands.push_back({y0, x0, sc, ge, r_mean, r_var});
            }
        }
    }
    // Rank by in-focus 2D structure score so out-of-focus bokeh & 1D periodic stripes rank lower than true corners!
    std::sort(cands.begin(), cands.end(), [](const CandPatch& a, const CandPatch& b) { return a.score > b.score; });
    if (cands.size() > 12) cands.resize(12);

    struct Vec2 { float ux, uy, dx, dy, ncc; bool periodic_ambiguous; };
    std::vector<Vec2> hits;

    // 2-plane Summed Area Table on cur.img gives O(1) patch mean/variance across all candidate patches & shifts
    const int sw_sat = W + 1;
    std::vector<double> sat_s((size_t)(H + 1) * sw_sat, 0.0), sat_q((size_t)(H + 1) * sw_sat, 0.0);
    for (int r = 0; r < H; ++r) {
        double rs = 0.0, rq = 0.0;
        const float* __restrict crow = cur.img.data() + (size_t)r * W;
        const double* __restrict ps = sat_s.data() + (size_t)r * sw_sat;
        const double* __restrict pq = sat_q.data() + (size_t)r * sw_sat;
        double* __restrict ds = sat_s.data() + (size_t)(r + 1) * sw_sat;
        double* __restrict dq = sat_q.data() + (size_t)(r + 1) * sw_sat;
        for (int c = 0; c < W; ++c) {
            const double cv = crow[c];
            rs += cv;
            rq += cv * cv;
            ds[c + 1] = ps[c + 1] + rs;
            dq[c + 1] = pq[c + 1] + rq;
        }
    }

    for (const auto& cp : cands) {
        const int y0 = cp.y0, x0 = cp.x0;
        const double r_mean = cp.r_mean, r_var = cp.r_var;

            auto eval_ncc = [&](int dy, int dx) -> double {
                int cy = y0 + dy, cx = x0 + dx;
                if (cy < 0 || cx < 0 || cy + bw > H || cx + bw > W) return -2.0;
                if (ref.sat_count > 0 || cur.sat_count > 0) {
                    const uint8_t* rm = ref.sat_mask.data();
                    const uint8_t* cm = cur.sat_mask.data();
                    double sr = 0.0, sc = 0.0, srr = 0.0, scc = 0.0, src = 0.0;
                    int cnt = 0;
                    for (int r = 0; r < bw; ++r) {
                        const float* rp = ref.img.data() + (size_t)(y0 + r) * W + x0;
                        const float* curp = cur.img.data() + (size_t)(cy + r) * W + cx;
                        const uint8_t* rmp = rm + (size_t)(y0 + r) * W + x0;
                        const uint8_t* cmp = cm + (size_t)(cy + r) * W + cx;
                        for (int c = 0; c < bw; ++c) {
                            if (rmp[c] || cmp[c]) continue;
                            double a = rp[c], b = curp[c];
                            sr += a; sc += b; srr += a * a; scc += b * b; src += a * b;
                            ++cnt;
                        }
                    }
                    if (cnt < Np / 4) return -2.0;
                    double rv = srr - sr * sr / (double)cnt;
                    double cv = scc - sc * sc / (double)cnt;
                    if (rv < 1e-8 || cv < 1e-8) return -2.0;
                    return (src - sr * sc / (double)cnt) / std::sqrt(rv * cv);
                }
                const size_t i00 = (size_t)cy * sw_sat + cx;
                const size_t i01 = i00 + bw;
                const size_t i10 = (size_t)(cy + bw) * sw_sat + cx;
                const size_t i11 = i10 + bw;
                const double c_sum = sat_s[i11] - sat_s[i01] - sat_s[i10] + sat_s[i00];
                const double c_sq  = sat_q[i11] - sat_q[i01] - sat_q[i10] + sat_q[i00];
                const double c_var = c_sq - (c_sum * c_sum) / Np;
                if (c_var < 1e-7) return -2.0;
                double rc_dot = 0.0;
                for (int r = 0; r < bw; ++r) {
                    const float* __restrict rp = ref.img.data() + (size_t)(y0 + r) * W + x0;
                    const float* __restrict curp = cur.img.data() + (size_t)(cy + r) * W + cx;
                    float row_dot = 0.0f;
                    for (int c = 0; c < bw; ++c) row_dot += rp[c] * curp[c];
                    rc_dot += (double)row_dot;
                }
                return (rc_dot - r_mean * c_sum) / std::sqrt(r_var * c_var);
            };

            struct PeakSample { int dy, dx; double ncc; };
            std::vector<PeakSample> grid_vals;
            grid_vals.reserve((size_t)((2 * rad / 2 + 1) * (2 * rad / 2 + 1)) + 81u);

            int best_dy = 0, best_dx = 0;
            double best_ncc = -2.0, best_eff = -3.0;
            auto consider = [&](int dy, int dx) {
                double v = eval_ncc(dy, dx);
                if (v <= -1.5) return;
                grid_vals.push_back({dy, dx, v});
                double v_eff = v - 1e-3 * std::hypot((double)dy, (double)dx);
                if (v_eff > best_eff) { best_eff = v_eff; best_ncc = v; best_dy = dy; best_dx = dx; }
            };
            const int fine_rad = std::min(rad, 4);
            for (int dy = -fine_rad; dy <= fine_rad; ++dy) {
                for (int dx = -fine_rad; dx <= fine_rad; ++dx) {
                    consider(dy, dx);
                }
            }
            for (int dy = -rad; dy <= rad; dy += 2) {
                for (int dx = -rad; dx <= rad; dx += 2) {
                    if (std::abs(dy) <= fine_rad && std::abs(dx) <= fine_rad) continue;
                    consider(dy, dx);
                }
            }
            int cdy = best_dy, cdx = best_dx;
            for (int dy = cdy - 1; dy <= cdy + 1; ++dy) {
                for (int dx = cdx - 1; dx <= cdx + 1; ++dx) {
                    double v = eval_ncc(dy, dx);
                    double v_eff = v - 1e-3 * std::hypot((double)dy, (double)dx);
                    if (v_eff > best_eff) { best_eff = v_eff; best_ncc = v; best_dy = dy; best_dx = dx; }
                }
            }
            if (best_ncc < 0.15) continue;

            double second_ncc = -2.0;
            for (const auto& ps : grid_vals) {
                if (std::max(std::abs(ps.dy - best_dy), std::abs(ps.dx - best_dx)) >= 3) {
                    if (ps.ncc > second_ncc) second_ncc = ps.ncc;
                }
            }
            const bool is_periodic_ambig = (second_ncc > 0.70 && (best_ncc - second_ncc) < 0.065);

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
            const float ux = (float)(((double)x0 + 0.5 * bw - 0.5 * W) * 8.0);
            const float uy = (float)(((double)y0 + 0.5 * bw - 0.5 * H) * 8.0);
            hits.push_back({ux, uy, (float)((best_dx + sub_x) * 8.0), (float)((best_dy + sub_y) * 8.0), (float)best_ncc, is_periodic_ambig});
    }

    float ncc_med = 0.0f;
    if (!hits.empty()) {
        std::vector<const Vec2*> seed_pool;
        for (const auto& h : hits) if (!h.periodic_ambiguous) seed_pool.push_back(&h);
        if (seed_pool.size() < 3u) {
            seed_pool.clear();
            for (const auto& h : hits) seed_pool.push_back(&h);
        }
        const size_t n_top = std::min<size_t>(5, seed_pool.size());
        std::vector<float> xs, ys, nccs;
        for (size_t i = 0; i < n_top; ++i) {
            xs.push_back(seed_pool[i]->dx);
            ys.push_back(seed_pool[i]->dy);
            nccs.push_back(seed_pool[i]->ncc);
        }
        std::sort(xs.begin(), xs.end());
        std::sort(ys.begin(), ys.end());
        std::sort(nccs.begin(), nccs.end());
        out_dx_1x = xs[xs.size() / 2];
        out_dy_1x = ys[ys.size() / 2];
        out_shift_1x = std::hypot(out_dx_1x, out_dy_1x);
        out_zncc = nccs[nccs.size() / 2];
        ncc_med = out_zncc;

        // Fit 4-DOF rigid camera similarity (translation + camera roll + zoom breathing)
        // via IRLS before measuring residual parallax spread across confident patches.
        double tx = out_dx_1x, ty = out_dy_1x, sim_a = 0.0, sim_b = 0.0;
        const float max_patch_dev = std::max(18.0f, 1.25f * out_shift_1x + 12.0f);
        size_t non_ambig_near = 0;
        for (const auto& h : hits) {
            if (!h.periodic_ambiguous && h.ncc >= 0.52f && std::hypot(h.dx - out_dx_1x, h.dy - out_dy_1x) <= max_patch_dev) {
                ++non_ambig_near;
            }
        }
        std::vector<const Vec2*> conf_hits;
        for (const auto& h : hits) {
            if (h.ncc < 0.52f) continue;
            if (h.periodic_ambiguous && non_ambig_near >= 3u) continue;
            if (std::hypot(h.dx - out_dx_1x, h.dy - out_dy_1x) > max_patch_dev) continue;
            conf_hits.push_back(&h);
        }
        if (conf_hits.size() >= 4) {
            for (int iter = 0; iter < 4; ++iter) {
                double sw = 0, mux = 0, muy = 0, mdx = 0, mdy = 0;
                std::vector<double> ws(conf_hits.size(), 0.0);
                for (size_t i = 0; i < conf_hits.size(); ++i) {
                    const auto& h = *conf_hits[i];
                    const double px = tx + sim_a * h.ux - sim_b * h.uy;
                    const double py = ty + sim_b * h.ux + sim_a * h.uy;
                    const double err = std::hypot(h.dx - px, h.dy - py);
                    const double scale = (iter == 0) ? 8.0 : 2.2;
                    const double w = (double)h.ncc / (1.0 + (err / scale) * (err / scale));
                    ws[i] = w; sw += w;
                    mux += w * h.ux; muy += w * h.uy;
                    mdx += w * h.dx; mdy += w * h.dy;
                }
                if (sw < 1e-6) break;
                mux /= sw; muy /= sw; mdx /= sw; mdy /= sw;
                double r2 = 0, na = 0, nb = 0;
                for (size_t i = 0; i < conf_hits.size(); ++i) {
                    const auto& h = *conf_hits[i];
                    const double x = h.ux - mux, y = h.uy - muy;
                    const double u = h.dx - mdx, v = h.dy - mdy;
                    r2 += ws[i] * (x * x + y * y);
                    na += ws[i] * (x * u + y * v);
                    nb += ws[i] * (x * v - y * u);
                }
                if (r2 > 1e4) {
                    sim_a = std::max(-0.04, std::min(0.04, na / r2));
                    sim_b = std::max(-0.08, std::min(0.08, nb / r2));
                }
                tx = mdx - sim_a * mux + sim_b * muy;
                ty = mdy - sim_b * mux - sim_a * muy;
            }
        }
        std::vector<float> devs;
        double cxx = 0.0, cyy = 0.0, cxy = 0.0, rad_dot = 0.0, rad_den = 0.0;
        for (const auto* hp : conf_hits) {
            if (hp->periodic_ambiguous || hp->ncc < 0.72f) continue;
            const double px = tx + sim_a * hp->ux - sim_b * hp->uy;
            const double py = ty + sim_b * hp->ux + sim_a * hp->uy;
            const double rx = hp->dx - px, ry = hp->dy - py;
            const float res_1x = std::max(0.0f, (float)std::hypot(rx, ry) - 0.65f);
            devs.push_back(res_1x);
            if (res_1x > 0.25f) {
                cxx += rx * rx; cyy += ry * ry; cxy += rx * ry;
                const double rn = std::hypot((double)hp->ux, (double)hp->uy);
                if (rn > 1.0) {
                    rad_dot += (rx * hp->ux + ry * hp->uy) / rn;
                    rad_den += std::hypot(rx, ry);
                }
            }
        }
        if (devs.size() >= 6) {
            const double tr = cxx + cyy;
            const double disc = std::hypot(cxx - cyy, 2.0 * cxy);
            const float epi_coh = (tr > 1e-6) ? (float)(disc / tr) : 0.0f;
            const float rad_coh = (rad_den > 1e-6) ? (float)std::min(1.0, std::fabs(rad_dot) / rad_den) : 0.0f;
            const float par_coh = std::max(epi_coh, rad_coh);
            if (out_parallax_coh) *out_parallax_coh = par_coh;
            std::sort(devs.begin(), devs.end());
            float raw_spread = devs[(devs.size() * 4) / 5] - devs[devs.size() / 5];
            out_spread_1x = (par_coh >= 0.45f) ? raw_spread : (raw_spread * par_coh);
        }
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

// [C8] Quadrant-Maximizing Phase-Striding Frame Selector for 30-120 fps high-speed bursts (A9 III, X-H2S, EOS R1, A7 V)
static std::vector<int> select_phase_diverse_subset(
    const std::vector<int>& pool, int K, const std::vector<FrameDiag>& diags, bool chronological_out = true
) {
    if (K <= 0 || (int)pool.size() <= K) return pool;
    std::vector<uint8_t> used(pool.size(), 0);
    std::vector<int> chosen;
    chosen.reserve((size_t)K);

    // 1. Always lock the sharpest Crown frame first
    size_t best_i = 0;
    for (size_t i = 1; i < pool.size(); ++i) {
        if (diags[pool[i]].acutance > diags[pool[best_i]].acutance) best_i = i;
    }
    double crown_acu = std::max(1e-9, diags[pool[best_i]].acutance);
    used[best_i] = 1;
    chosen.push_back(pool[best_i]);

    bool quad_cov[4] = {false, false, false, false};
    auto quad_of = [&](int idx) -> int {
        int qx = ((int)std::llround(diags[idx].dx_1x * 2.0f)) & 1;
        int qy = ((int)std::llround(diags[idx].dy_1x * 2.0f)) & 1;
        return qy * 2 + qx;
    };
    quad_cov[quad_of(pool[best_i])] = true;

    // 2. Greedily fill missing 2x2 subpixel quadrants with sharp candidates (>=86% of crown sharpness)
    for (int pass = 0; pass < 3 && (int)chosen.size() < K; ++pass) {
        int pick = -1;
        double best_sc = -1.0;
        for (size_t i = 0; i < pool.size(); ++i) {
            if (used[i]) continue;
            int q = quad_of(pool[i]);
            if (quad_cov[q]) continue;
            double rel_a = diags[pool[i]].acutance / crown_acu;
            if (rel_a < 0.86) continue;
            if (diags[pool[i]].acutance > best_sc) { best_sc = diags[pool[i]].acutance; pick = (int)i; }
        }
        if (pick < 0) break;
        used[(size_t)pick] = 1;
        quad_cov[quad_of(pool[(size_t)pick])] = true;
        chosen.push_back(pool[(size_t)pick]);
    }

    // 3. Fill remaining slots maximizing acutance * phase-diversity bonus
    while ((int)chosen.size() < K) {
        int pick = -1;
        double best_sc = -1.0;
        for (size_t i = 0; i < pool.size(); ++i) {
            if (used[i]) continue;
            float min_pd = 1e9f;
            float fx = diags[pool[i]].dx_1x - std::floor(diags[pool[i]].dx_1x);
            float fy = diags[pool[i]].dy_1x - std::floor(diags[pool[i]].dy_1x);
            for (int c_idx : chosen) {
                float cx = diags[c_idx].dx_1x - std::floor(diags[c_idx].dx_1x);
                float cy = diags[c_idx].dy_1x - std::floor(diags[c_idx].dy_1x);
                float dx = std::fabs(fx - cx); dx = std::min(dx, 1.0f - dx);
                float dy = std::fabs(fy - cy); dy = std::min(dy, 1.0f - dy);
                min_pd = std::min(min_pd, std::hypot(dx, dy));
            }
            double sc = diags[pool[i]].acutance * (1.0 + 0.22 * (double)min_pd);
            if (sc > best_sc) { best_sc = sc; pick = (int)i; }
        }
        if (pick < 0) break;
        used[(size_t)pick] = 1;
        chosen.push_back(pool[(size_t)pick]);
    }

    if (chronological_out) std::sort(chosen.begin(), chosen.end());
    return chosen;
}

static inline std::string detect_sensor_olpf_profile(const std::string& make, const std::string& model, int W, int H, bool is_xtrans) {
    if (is_xtrans) return "none";
    std::string m = make + " " + model;
    for (char& ch : m) ch = (char)std::toupper((unsigned char)ch);
    const auto r1=m.find("EOS R1");
    const bool is_r1=r1!=std::string::npos && (r1+6==m.size() || !std::isalnum((unsigned char)m[r1+6]));
    if (is_r1 || m.find("1D X MARK III") != std::string::npos) return "gauss16";
    (void)W; (void)H;
    for (const auto* camera : {"ILCE-9M3", "ILCE-7M5", "ILCE-7M4", "EOS R3",
                              "EOS R6", "NIKON Z 6", "NIKON Z F", "NIKON ZF"})
        if (m.find(camera) != std::string::npos) return "weak4";
    return "none";
}

struct AliasSchurCompleteness {
    double phi_2d = 0.0; // Conservative 4-alias { (0,0),(0,1),(1,0),(1,1) } Schur completeness in [0,1]
    double phi_x = 0.0;  // Horizontal 2-alias { (0,0),(1,0) } completeness in [0,1]
    double phi_y = 0.0;  // Vertical 2-alias { (0,0),(0,1) } completeness in [0,1]
    double n_eff = 0.0;  // Effective inverse-variance frame count (sum w)^2 / sum(w^2)
    bool bi_rank_ok = false;
};

// A14.2: Exact Modified Gram-Schmidt Schur complement completeness I_a / ||C^{-1/2} a||^2
// across the 4 integer sensor Brillouin-zone aliases in [-1, 1)^2 cy/px_1x, including
// registration error ellipsoid perturbation (+/- sigma_reg_1x_px).
inline AliasSchurCompleteness compute_alias_schur_completeness(
    std::span<const std::array<float, 2>> shifts_yx_1x,
    std::span<const float> weights = {},
    double sigma_reg_1x_px = 0.035
) {
    AliasSchurCompleteness out;
    const size_t N = shifts_yx_1x.size();
    if (N == 0) return out;

    std::vector<double> w(N, 1.0);
    double sum_w = 0.0, sum_w2 = 0.0;
    for (size_t i = 0; i < N; ++i) {
        w[i] = (i < weights.size()) ? std::max(0.0, (double)weights[i]) : 1.0;
        sum_w += w[i];
        sum_w2 += w[i] * w[i];
    }
    if (sum_w <= 1e-12) return out;
    out.n_eff = (sum_w * sum_w) / std::max(1e-15, sum_w2);

    auto eval_config = [&](double pert_y, double pert_x, double& p2d, double& px, double& py) {
        constexpr double kTwoPi = 6.28318530717958647692;
        static constexpr double kBy[4] = {0.0, 1.0, 0.0, 1.0};
        static constexpr double kBx[4] = {0.0, 0.0, 1.0, 1.0};
        struct Cpx { double r, i; };
        std::array<std::vector<Cpx>, 4> cols;
        for (int b = 0; b < 4; ++b) cols[(size_t)b].resize(N);
        for (size_t n = 0; n < N; ++n) {
            const double sw = std::sqrt(w[n]);
            const double sgn = (n == 0) ? 0.0 : ((n & 1u) ? 1.0 : -1.0);
            const double uy = (double)shifts_yx_1x[n][0] + sgn * pert_y;
            const double ux = (double)shifts_yx_1x[n][1] + sgn * pert_x;
            for (int b = 0; b < 4; ++b) {
                const double ang = kTwoPi * (kBy[b] * uy + kBx[b] * ux);
                cols[(size_t)b][n] = {sw * std::cos(ang), sw * std::sin(ang)};
            }
        }
        auto dot_c = [&](const std::vector<Cpx>& a, const std::vector<Cpx>& b) -> Cpx {
            double sr = 0.0, si = 0.0;
            for (size_t n = 0; n < N; ++n) {
                sr += a[n].r * b[n].r + a[n].i * b[n].i;
                si += a[n].r * b[n].i - a[n].i * b[n].r;
            }
            return {sr, si};
        };
        // 1D 2-alias completeness: 1 - |<v0, vb>|^2 / sum_w^2
        if (N >= 2) {
            const Cpx cy = dot_c(cols[0], cols[1]);
            const Cpx cx = dot_c(cols[0], cols[2]);
            py = std::clamp(1.0 - (cy.r * cy.r + cy.i * cy.i) / (sum_w * sum_w), 0.0, 1.0);
            px = std::clamp(1.0 - (cx.r * cx.r + cx.i * cx.i) / (sum_w * sum_w), 0.0, 1.0);
        } else {
            px = 0.0;
            py = 0.0;
        }
        if (N < 4) {
            p2d = 0.0;
            return;
        }
        double min_phi = 1.0;
        std::vector<std::vector<Cpx>> q_basis;
        q_basis.reserve(3);
        std::vector<Cpx> cur(N);
        for (int target = 0; target < 4; ++target) {
            q_basis.clear();
            for (int d = 0; d < 4; ++d) {
                if (d == target) continue;
                cur = cols[(size_t)d];
                for (const auto& q : q_basis) {
                    const Cpx proj = dot_c(q, cur);
                    for (size_t n = 0; n < N; ++n) {
                        cur[n].r -= (proj.r * q[n].r - proj.i * q[n].i);
                        cur[n].i -= (proj.r * q[n].i + proj.i * q[n].r);
                    }
                }
                const double nrm2 = dot_c(cur, cur).r;
                if (nrm2 > 1e-12 * sum_w) {
                    const double inv_n = 1.0 / std::sqrt(nrm2);
                    for (size_t n = 0; n < N; ++n) { cur[n].r *= inv_n; cur[n].i *= inv_n; }
                    q_basis.push_back(cur);
                }
            }
            cur = cols[(size_t)target];
            for (const auto& q : q_basis) {
                const Cpx proj = dot_c(q, cur);
                for (size_t n = 0; n < N; ++n) {
                    cur[n].r -= (proj.r * q[n].r - proj.i * q[n].i);
                    cur[n].i -= (proj.r * q[n].i + proj.i * q[n].r);
                }
            }
            const double ip = std::max(0.0, dot_c(cur, cur).r);
            min_phi = std::min(min_phi, std::clamp(ip / sum_w, 0.0, 1.0));
        }
        p2d = min_phi;
    };

    double p2d_0 = 0.0, px_0 = 0.0, py_0 = 0.0;
    eval_config(0.0, 0.0, p2d_0, px_0, py_0);
    out.phi_2d = p2d_0;
    out.phi_x = px_0;
    out.phi_y = py_0;
    if (sigma_reg_1x_px > 0.0 && N >= 2) {
        static constexpr double kDirs[4][2] = {{1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0}};
        for (const auto& dr : kDirs) {
            double p2 = 0.0, px = 0.0, py = 0.0;
            eval_config(dr[0] * sigma_reg_1x_px, dr[1] * sigma_reg_1x_px, p2, px, py);
            out.phi_2d = std::min(out.phi_2d, p2);
            out.phi_x = std::min(out.phi_x, px);
            out.phi_y = std::min(out.phi_y, py);
        }
    }
    out.bi_rank_ok = (N >= 4 && out.phi_2d >= 0.05 && out.phi_x >= 0.15 && out.phi_y >= 0.15);
    return out;
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

    // Naming stem uses the earliest frame (or clean .NEV range stem for virtual frames)
    pg.naming_stem = stem_of(uframes[clean_indices[0]].path);
    {
        std::string first_base;
        uint32_t f_min = 0u, f_max = 0u;
        bool all_same_virt = heraw_wvdc::split_virtual_raw_path(uframes[clean_indices[0]].path, first_base, f_min);
        if (all_same_virt) {
            f_max = f_min;
            for (size_t k = 1; k < clean_indices.size(); ++k) {
                std::string kb;
                uint32_t kf = 0u;
                if (!heraw_wvdc::split_virtual_raw_path(uframes[clean_indices[k]].path, kb, kf) || kb != first_base) {
                    all_same_virt = false;
                    break;
                }
                f_min = std::min(f_min, kf);
                f_max = std::max(f_max, kf);
            }
            if (all_same_virt) {
                std::string b = basename_of(first_base);
                size_t dot = b.find_last_of('.');
                std::string base_stem = (dot == std::string::npos) ? b : b.substr(0, dot);
                if (clean_indices.size() > 1) {
                    char rbuf[40];
                    std::snprintf(rbuf, sizeof(rbuf), "_f%04u-%04u", f_min, f_max);
                    pg.naming_stem = base_stem + rbuf;
                } else {
                    char rbuf[24];
                    std::snprintf(rbuf, sizeof(rbuf), "_f%04u", f_min);
                    pg.naming_stem = base_stem + rbuf;
                }
            }
        }
    }

    int best_pos = 0;
    if (anchor_mode == -1) {
        best_pos = 0;
    } else if (anchor_mode == -2) {
        best_pos = (int)(clean_indices.size() / 2);
    } else if (anchor_mode >= 1) {
        auto chosen = std::find(clean_indices.begin(), clean_indices.end(), anchor_mode - 1);
        if (chosen != clean_indices.end()) best_pos = (int)(chosen - clean_indices.begin());
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
    std::vector<std::array<float, 2>> shifts_yx;
    shifts_yx.reserve(pg.light_uframe_indices.size());
    shifts_yx.push_back({0.0f, 0.0f});

    for (size_t k = 1; k < pg.light_uframe_indices.size(); ++k) {
        int idx = pg.light_uframe_indices[k];
        sum_acut += diags[idx].acutance;
        float dx = 0, dy = 0, sh = 0, ncc = 1, sp = 0;
        measure_pair_motion_1x(thumbs[ref_idx], thumbs[idx], dx, dy, sh, ncc, sp);
        max_sh = std::max(max_sh, sh);
        sum_p2 += (double)(sh * sh);
        shifts_yx.push_back({dy, dx});
        int qx = ((int)std::llround(dx * 2.0f)) & 1;
        int qy = ((int)std::llround(dy * 2.0f)) & 1;
        quad_seen[qy * 2 + qx] = true;
    }

    const AliasSchurCompleteness schur = compute_alias_schur_completeness(shifts_yx);
    pg.phi_schur_2d = (float)schur.phi_2d;
    pg.phi_schur_x = (float)schur.phi_x;
    pg.phi_schur_y = (float)schur.phi_y;
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
    const std::string& current_preset = "photo",
    int n_threads = 0,
    int min_valid_frames = 5,
    PlanDepthGate depth_gate = {}
) {
    ClusterPlanResult res;
    const int n_total = (int)uframes.size();
    res.diags.resize(n_total);
    if (n_total == 0) return res;

    // 1 & 2. Parallel per-frame signal/noise/acutance + 1/4 & 1/8 FastThumb telemetry
    std::vector<FastThumb> thumbs(n_total);
    auto eval_one_frame = [&](int i) {
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
        float avg_black = 0.0f;
        for (int py = 0; py < 6; ++py)
            for (int px = 0; px < 6; ++px) avg_black += uf.black_6x6[py][px];
        avg_black *= (1.0f / 36.0f);
        if (avg_black <= 0.0f) avg_black = 0.25f * (uf.black_c[0] + uf.black_c[1] + uf.black_c[2] + uf.black_c[3]);
        const float clip_raw_thr = avg_black + 0.98f * std::max(1.0f, uf.white - avg_black);
        size_t clip_cnt = 0, clip_tot = 0;
        for (int by = 0; by < uf.H / bh; ++by) {
            for (int bx = 0; bx < uf.W / bw; ++bx) {
                double s = 0.0;
                int cnt = 0;
                for (int r = 0; r < bh; ++r) {
                    int r_abs = by * bh + r;
                    const uint16_t* row = uf.bayer.data() + (size_t)r_abs * uf.W + bx * bw;
                    for (int c = 0; c < bw; ++c) {
                        const uint16_t rv = row[c];
                        if ((float)rv >= clip_raw_thr) ++clip_cnt;
                        ++clip_tot;
                        s += (double)rv - (double)uf.black_6x6[r_abs % 6][(bx * bw + c) % 6];
                        ++cnt;
                    }
                }
                bmeans.push_back((float)(s / (double)std::max(1, cnt)));
            }
        }
        if (clip_tot == 0 && !uf.bayer.empty()) {
            for (int r = 0; r < uf.H; ++r) {
                const uint16_t* row = uf.bayer.data() + (size_t)r * uf.W;
                for (int c = 0; c < uf.W; ++c) {
                    if ((float)row[c] >= clip_raw_thr) ++clip_cnt;
                    ++clip_tot;
                }
            }
        }
        d.clipped_fraction = (clip_tot > 0) ? (float)((double)clip_cnt / (double)clip_tot) : 0.0f;
        float p25 = bmeans.empty() ? 0.0f : quick_pct(bmeans, 0.25f);
        float p50 = bmeans.empty() ? 0.0f : quick_pct(bmeans, 0.50f);
        float p75 = bmeans.empty() ? 0.0f : quick_pct(bmeans, 0.75f);

        std::vector<float> hh;
        hh.reserve((size_t)(uf.H / 6 + 1) * (size_t)(uf.W / 6 + 1));
        double grad_sq = 0.0, af_grad_sq = 0.0;
        int64_t grad_cnt = 0, af_grad_cnt = 0;
        float inv_span = 1.0f / std::max(1.0f, uf.white - avg_black);
        const int cfa_step = uf.is_xtrans ? 6 : 2;
        const bool has_af = (uf.af_norm_x >= 0.02f && uf.af_norm_x <= 0.98f && uf.af_norm_y >= 0.02f && uf.af_norm_y <= 0.98f);
        const int af_x0 = has_af ? (int)((uf.af_norm_x - 0.08f) * uf.W) : 0;
        const int af_x1 = has_af ? (int)((uf.af_norm_x + 0.08f) * uf.W) : 0;
        const int af_y0 = has_af ? (int)((uf.af_norm_y - 0.08f) * uf.H) : 0;
        const int af_y1 = has_af ? (int)((uf.af_norm_y + 0.08f) * uf.H) : 0;

        const float sat_raw_thr = avg_black + 0.78f * std::max(1.0f, uf.white - avg_black);
        int c0_green = 0;
        for (int c_try = 0; c_try < 6; ++c_try) if (uf.cfa6x6[0][c_try] == 1) { c0_green = c_try; break; }
        for (int r = 0; r + cfa_step < uf.H; r += 6) {
            for (int c = c0_green; c + cfa_step < uf.W; c += 6) {
                float g00 = (float)uf.bayer[(size_t)r * uf.W + c];
                float g01 = (float)uf.bayer[(size_t)r * uf.W + c + cfa_step];
                float g10 = (float)uf.bayer[(size_t)(r + cfa_step) * uf.W + c];
                float g11 = (float)uf.bayer[(size_t)(r + cfa_step) * uf.W + c + cfa_step];
                if (g00 >= sat_raw_thr || g01 >= sat_raw_thr || g10 >= sat_raw_thr || g11 >= sat_raw_thr) continue;
                if (!uf.is_xtrans) hh.push_back(std::fabs(0.5f * (g00 - g01 - g10 + g11)));
                double gx = (double)(g01 - g00) * inv_span;
                double gy = (double)(g10 - g00) * inv_span;
                double ge = gx * gx + gy * gy;
                grad_sq += ge;
                ++grad_cnt;
                if (has_af && r >= af_y0 && r <= af_y1 && c >= af_x0 && c <= af_x1) {
                    af_grad_sq += ge;
                    ++af_grad_cnt;
                }
            }
        }
        if (uf.is_xtrans) {
            int gr0 = 0, gc0 = 0;
            bool found_g2x2 = false;
            for (int rr = 0; rr < 5 && !found_g2x2; ++rr) {
                for (int cc = 0; cc < 5 && !found_g2x2; ++cc) {
                    if (uf.cfa6x6[rr][cc] == 1 && uf.cfa6x6[rr][cc+1] == 1 &&
                        uf.cfa6x6[rr+1][cc] == 1 && uf.cfa6x6[rr+1][cc+1] == 1) {
                        gr0 = rr; gc0 = cc; found_g2x2 = true;
                    }
                }
            }
            for (int r = gr0; r + 1 < uf.H; r += 6) {
                for (int c = gc0; c + 1 < uf.W; c += 6) {
                    float v00 = (float)uf.bayer[(size_t)r * uf.W + c] - uf.black_6x6[r%6][c%6];
                    float v01 = (float)uf.bayer[(size_t)r * uf.W + c + 1] - uf.black_6x6[r%6][(c+1)%6];
                    float v10 = (float)uf.bayer[(size_t)(r + 1) * uf.W + c] - uf.black_6x6[(r+1)%6][c%6];
                    float v11 = (float)uf.bayer[(size_t)(r + 1) * uf.W + c + 1] - uf.black_6x6[(r+1)%6][(c+1)%6];
                    hh.push_back(std::fabs(0.5f * (v00 - v01 - v10 + v11)));
                }
            }
        }
        d.med_sig = p50;
        d.block_iqr = p75 - p25;
        d.hf_sigma = std::max(1.0f, hh.empty() ? 1.0f : (quick_pct(hh, 0.50f) / 0.6745f));
        double noise_grad_bias = 4.0 * (double)(d.hf_sigma * inv_span) * (double)(d.hf_sigma * inv_span);
        double whole_acu = std::sqrt(std::max(0.0, grad_sq / (double)std::max<int64_t>(1, grad_cnt) - noise_grad_bias)) * 1000.0;
        d.af_acutance = (af_grad_cnt >= 16)
            ? (std::sqrt(std::max(0.0, af_grad_sq / (double)af_grad_cnt - noise_grad_bias)) * 1000.0)
            : whole_acu;
        d.acutance = (af_grad_cnt >= 16) ? (0.65 * d.af_acutance + 0.35 * whole_acu) : whole_acu;
        thumbs[i] = build_fast_thumb(uf);
    };

    const int hw_nt = (int)std::max(1u, std::thread::hardware_concurrency());
    const int eff_nt = std::min(n_total, std::min(8, n_threads > 0 ? n_threads : hw_nt));
    if (eff_nt <= 1) {
        for (int i = 0; i < n_total; ++i) eval_one_frame(i);
    } else {
        std::atomic<int> next_f{0};
        std::vector<heraw_sync::JoiningThread> workers;
        workers.reserve((size_t)eff_nt);
        for (int t = 0; t < eff_nt; ++t) {
            workers.emplace_back([&]() {
                while (true) {
                    const int i = next_f.fetch_add(1, std::memory_order_relaxed);
                    if (i >= n_total) break;
                    eval_one_frame(i);
                }
            });
        }
    }

    float max_iqr = 0.0f;
    for (int i = 0; i < n_total; ++i) max_iqr = std::max(max_iqr, res.diags[i].block_iqr);

    int astro_like_cnt = 0;
    for (int i = 0; i < n_total; ++i) {
        if (!thumbs[i].is_dark_frame &&
            !uframes[i].clearly_near_macro &&
            thumbs[i].p50_norm < 0.045f &&
            thumbs[i].p75_norm < 0.095f &&
            thumbs[i].stars_snr65_cnt >= 3 &&
            thumbs[i].max_star_snr >= 12.0f) {
            ++astro_like_cnt;
        }
    }
    const bool cluster_is_astro_rank = (astro_like_cnt > 0 && astro_like_cnt * 2 >= std::max(1, n_total));

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
            // Apply star-flux acutance ranking uniformly across the whole cluster on genuine night-sky sets
            if (cluster_is_astro_rank && thumbs[i].p50_norm < 0.045f &&
                thumbs[i].max_star_snr >= 12.0f && thumbs[i].top3_star_flux > 0.0f) {
                res.diags[i].acutance = 0.35 * res.diags[i].acutance + 250.0 * (double)thumbs[i].top3_star_flux;
            }
            light_raw.push_back(i);
        }
    }

    if (light_raw.empty() && policy == ExecPolicy::FORCE_ALL) light_raw=dark_idx;
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
    std::vector<float> diag_par_coh((size_t)n_total, 0.0f);
    std::vector<std::vector<int>> raw_segments;
    for (int idx : same_dim_lights) {
        if (raw_segments.empty()) {
            raw_segments.push_back({idx});
            res.diags[idx].zncc = 1.0f;
            continue;
        }
        int seg_anchor = raw_segments.back()[0];
        int seg_prev = raw_segments.back().back();
        float dx = 0, dy = 0, sh = 0, ncc = 0, sp = 0, pcoh = 0;
        measure_pair_motion_1x(thumbs[seg_anchor], thumbs[idx], dx, dy, sh, ncc, sp, &pcoh);
        res.diags[idx].dx_1x = dx;
        res.diags[idx].dy_1x = dy;
        res.diags[idx].shift_1x = sh;
        res.diags[idx].zncc = ncc;
        res.diags[idx].local_spread_1x = sp;
        diag_par_coh[(size_t)idx] = pcoh;

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
    const int eff_min = std::max(3, (target_group_size > 0 && target_group_size < min_valid_frames)
        ? target_group_size : min_valid_frames);
    const int grp_sz = std::max(eff_min, target_group_size > 0 ? target_group_size : 8);
    std::vector<PlannedGroup> clean_groups_4plus;
    std::vector<int> all_clean_single;
    int dropped_anomalies = (int)(light_raw.size() - same_dim_lights.size());
    for (int idx : same_dim_lights) if (res.diags[idx].flags != SUIT_OK) ++dropped_anomalies;

    for (const auto& seg : raw_segments) {
        if (seg.empty()) continue;
        std::vector<float> meds, acuts, exposure_values;
        bool exposure_metadata=true;
        for(int idx:seg) {
            if((size_t)idx>=hdr_meta.size() || hdr_meta[idx].shutter<=0 || hdr_meta[idx].iso<=0) {
                exposure_metadata=false;break;
            }
            const auto& h=hdr_meta[idx];
            exposure_values.push_back(h.shutter*h.iso/(h.aperture>0 ? h.aperture*h.aperture : 1.0f));
        }
        const float reference_exposure=exposure_metadata ? quick_pct(exposure_values,0.5f) : 1.0f;
        const bool bracket_exif=exposure_metadata && !exposure_values.empty() &&
            *std::max_element(exposure_values.begin(),exposure_values.end()) >
            1.15f * *std::min_element(exposure_values.begin(),exposure_values.end());
        // Detect non-EXIF AEB exposure bracketing across structurally matching frames
        bool non_exif_bracket = false;
        float med_sig_anchor = 1.0f;
        if (!seg.empty()) {
            std::vector<float> seg_sigs;
            float sig_lo = 1e30f, sig_hi = 0.0f;
            int ref_i = seg[0];
            for (int idx : seg) {
                float dx_t = 0, dy_t = 0, sh_t = 0, ncc_t = 1.0f, sp_t = 0;
                if (idx != ref_i) measure_pair_motion_1x(thumbs[ref_i], thumbs[idx], dx_t, dy_t, sh_t, ncc_t, sp_t);
                if (idx == ref_i || ncc_t >= 0.72f) {
                    float s = std::max(1.0f, res.diags[idx].med_sig);
                    sig_lo = std::min(sig_lo, s);
                    sig_hi = std::max(sig_hi, s);
                    seg_sigs.push_back(s);
                }
            }
            if (seg_sigs.size() >= 2 && sig_hi > 1.35f * sig_lo) {
                non_exif_bracket = true;
                med_sig_anchor = quick_pct(seg_sigs, 0.5f);
            }
        }
        const bool bracket = bracket_exif || non_exif_bracket;
        auto exposure_scale=[&](int idx) {
            if(!bracket) return 1.0f;
            if (bracket_exif) {
                const auto& h=hdr_meta[idx];
                return std::max(0.01f,h.shutter*h.iso/(h.aperture>0?h.aperture*h.aperture:1.0f)/reference_exposure);
            }
            return std::max(0.01f, std::max(1.0f, res.diags[idx].med_sig) / std::max(1.0f, med_sig_anchor));
        };
        for (int idx : seg) {
            meds.push_back(std::max(1.0f, res.diags[idx].med_sig)/exposure_scale(idx));
            res.diags[idx].acutance/=exposure_scale(idx);
            acuts.push_back((float)res.diags[idx].acutance);
        }
        float med_exp = quick_pct(meds, 0.50f);
        float med_acu = quick_pct(acuts, 0.50f);

        float seg_diop_min = 1e30f, seg_diop_max = -1e30f;
        int seg_diop_cnt = 0;
        float seg_step_min = 1e30f, seg_step_max = -1e30f;
        int seg_step_cnt = 0;
        for (int idx : seg) {
            if (uframes[idx].focus_distance_m > 0.0f) {
                float diop = 1.0f / uframes[idx].focus_distance_m;
                seg_diop_min = std::min(seg_diop_min, diop);
                seg_diop_max = std::max(seg_diop_max, diop);
                ++seg_diop_cnt;
            }
            if (uframes[idx].focus_step > 0.0f) {
                seg_step_min = std::min(seg_step_min, uframes[idx].focus_step);
                seg_step_max = std::max(seg_step_max, uframes[idx].focus_step);
                ++seg_step_cnt;
            }
        }
        const bool seg_is_focus_sweep =
            (seg_diop_cnt >= 3 && (seg_diop_max - seg_diop_min) >= 0.05f) ||
            (seg_step_cnt >= 3 && (seg_step_max - seg_step_min) >= 2.0f);

        std::vector<int> seg_clean;
        for (int idx : seg) {
            float eratio = std::max(1.0f, res.diags[idx].med_sig) / exposure_scale(idx) / std::max(1.0f, med_exp);
            if (!bracket && med_exp > 25.0f && (eratio < 0.52f || eratio > 1.92f)) {
                res.diags[idx].flags |= SUIT_EXPOSURE_OUTLIER;
                ++dropped_anomalies;
                continue;
            }
            if (seg.size() >= 3 && res.diags[idx].acutance < (seg_is_focus_sweep ? 0.25 : 1.0) * (double)min_rel_sharpness * (double)med_acu) {
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
                        bool diff_focus = false;
                        if (uframes[idx].focus_distance_m > 0.0f && uframes[kept].focus_distance_m > 0.0f) {
                            if (std::fabs(1.0f / uframes[idx].focus_distance_m - 1.0f / uframes[kept].focus_distance_m) > 0.015f) {
                                diff_focus = true;
                            }
                        } else if (uframes[idx].focus_step > 0.0f && uframes[kept].focus_step > 0.0f) {
                            if (std::fabs(uframes[idx].focus_step - uframes[kept].focus_step) > 0.5f) {
                                diff_focus = true;
                            }
                        }
                        bool diff_exp = bracket &&
                            (std::fabs(std::log2(std::max(1.0f, res.diags[idx].med_sig) /
                                                 std::max(1.0f, res.diags[kept].med_sig))) > 0.35f);
                        if (!diff_focus && !diff_exp && std::hypot(dx, dy) < 0.06f && res.diags[idx].acutance <= res.diags[kept].acutance) {
                            dup = true;
                            break;
                        }
                    }
                }
                if (dup && (int)seg_clean.size() > eff_min) {
                    res.diags[idx].flags |= SUIT_PHASE_REDUNDANT;
                } else {
                    dedup.push_back(idx);
                }
            }
            if ((int)dedup.size() >= eff_min) seg_clean.swap(dedup);
        }

        for (int idx : seg_clean) all_clean_single.push_back(idx);

        if ((int)seg_clean.size() >= eff_min) {
            bool per_burst_cap_mode = (policy == ExecPolicy::AUTO_PRUNE && max_frames_cap >= eff_min);
            bool want_split = !per_burst_cap_mode &&
                              ((policy == ExecPolicy::MULTI_GROUP) || (is_batch_scan && (int)seg_clean.size() >= 2 * eff_min));
            if (want_split && (int)seg_clean.size() >= 2 * eff_min && (int)seg_clean.size() > grp_sz) {
                const int M = (int)seg_clean.size();
                int n_sub = std::max(1, (M + grp_sz - 1) / grp_sz);
                while (n_sub > 1 && M / n_sub < eff_min) --n_sub;
                if (n_sub <= 1) {
                    std::vector<int> sub = seg_clean;
                    if (max_frames_cap >= eff_min && (int)sub.size() > max_frames_cap) {
                        sub = select_phase_diverse_subset(sub, max_frames_cap, res.diags, true);
                    }
                    clean_groups_4plus.push_back(finalize_planned_group(sub, dark_idx, uframes, res.diags, thumbs, anchor_mode));
                } else {
                    // Valley- and phase-aware DP partitioning: split at natural time pauses / pan steps while balancing size & 2x2 phase coverage
                    std::vector<double> step_dt(M, 0.0), step_sh(M, 0.0);
                    std::vector<float> dt_vals, sh_vals;
                    for (int k = 1; k < M; ++k) {
                        int i0 = seg_clean[(size_t)k - 1], i1 = seg_clean[(size_t)k];
                        if ((size_t)i0 < hdr_meta.size() && (size_t)i1 < hdr_meta.size() &&
                            hdr_meta[(size_t)i0].timestamp > 0 && hdr_meta[(size_t)i1].timestamp > 0) {
                            double raw_dt = (double)(hdr_meta[(size_t)i1].timestamp - hdr_meta[(size_t)i0].timestamp) +
                                            0.001 * (double)(hdr_meta[(size_t)i1].subsec_ms - hdr_meta[(size_t)i0].subsec_ms);
                            double exp_s = std::max(0.0, (double)std::max(hdr_meta[(size_t)i0].shutter, hdr_meta[(size_t)i1].shutter));
                            step_dt[k] = std::max(0.0, std::fabs(raw_dt) - exp_s);
                            if (step_dt[k] > 0.0) dt_vals.push_back((float)step_dt[k]);
                        }
                        double ddx = (double)res.diags[(size_t)i1].dx_1x - (double)res.diags[(size_t)i0].dx_1x;
                        double ddy = (double)res.diags[(size_t)i1].dy_1x - (double)res.diags[(size_t)i0].dy_1x;
                        step_sh[k] = std::hypot(ddx, ddy);
                        if (step_sh[k] > 0.0) sh_vals.push_back((float)step_sh[k]);
                    }
                    double med_dt = dt_vals.empty() ? 0.10 : std::max(0.04, (double)quick_pct(dt_vals, 0.50f));
                    double med_sh = sh_vals.empty() ? 1.00 : std::max(0.50, (double)quick_pct(sh_vals, 0.50f));
                    const double target_len = (double)M / (double)n_sub;
                    const int max_len = std::max(grp_sz + 2, (M + n_sub - 1) / n_sub + 2);

                    auto seg_cost = [&](int a, int b) -> double {
                        int len = b - a;
                        double diff = (double)len - target_len;
                        double cost = 1.35 * diff * diff;
                        if (a > 0) {
                            double dt_bonus = std::min(3.5, step_dt[a] / med_dt);
                            double sh_bonus = std::min(2.5, step_sh[a] / med_sh);
                            cost -= (1.10 * dt_bonus + 0.75 * sh_bonus);
                        }
                        uint32_t qmask = 0;
                        float bx = res.diags[(size_t)seg_clean[(size_t)a]].dx_1x;
                        float by = res.diags[(size_t)seg_clean[(size_t)a]].dy_1x;
                        double max_span = 0.0;
                        for (int t = a; t < b; ++t) {
                            float rx_d = res.diags[(size_t)seg_clean[(size_t)t]].dx_1x - bx;
                            float ry_d = res.diags[(size_t)seg_clean[(size_t)t]].dy_1x - by;
                            int qx = ((int)std::llround(2.0f * rx_d)) & 1;
                            int qy = ((int)std::llround(2.0f * ry_d)) & 1;
                            qmask |= (1u << (qy * 2 + qx));
                            max_span = std::max(max_span, (double)std::hypot(rx_d, ry_d));
                        }
                        int qcnt = std::popcount(static_cast<unsigned>(qmask));
                        cost -= 0.45 * (double)qcnt;
                        if (max_span > 32.0) cost += 0.03 * (max_span - 32.0);
                        return cost;
                    };

                    const double kInf = 1e18;
                    std::vector<std::vector<double>> dp((size_t)n_sub + 1, std::vector<double>((size_t)M + 1, kInf));
                    std::vector<std::vector<int>> prev_cut((size_t)n_sub + 1, std::vector<int>((size_t)M + 1, -1));
                    dp[0][0] = 0.0;
                    for (int g = 1; g <= n_sub; ++g) {
                        for (int b = g * eff_min; b <= M - (n_sub - g) * eff_min; ++b) {
                            int a_min = std::max((g - 1) * eff_min, b - max_len);
                            int a_max = b - eff_min;
                            for (int a = a_min; a <= a_max; ++a) {
                                if (dp[(size_t)g - 1][(size_t)a] >= kInf * 0.5) continue;
                                double cand = dp[(size_t)g - 1][(size_t)a] + seg_cost(a, b);
                                if (cand < dp[(size_t)g][(size_t)b]) {
                                    dp[(size_t)g][(size_t)b] = cand;
                                    prev_cut[(size_t)g][(size_t)b] = a;
                                }
                            }
                        }
                    }

                    std::vector<int> bounds((size_t)n_sub + 1, 0);
                    bounds[(size_t)n_sub] = M;
                    bool dp_ok = (dp[(size_t)n_sub][(size_t)M] < kInf * 0.5);
                    if (dp_ok) {
                        int cur_b = M;
                        for (int g = n_sub; g >= 1; --g) {
                            int pa = prev_cut[(size_t)g][(size_t)cur_b];
                            if (pa < 0) { dp_ok = false; break; }
                            bounds[(size_t)g - 1] = pa;
                            cur_b = pa;
                        }
                    }
                    if (!dp_ok) {
                        for (int g = 0; g <= n_sub; ++g) bounds[(size_t)g] = (M * g) / n_sub;
                    }
                    for (int g = 0; g < n_sub; ++g) {
                        std::vector<int> sub(seg_clean.begin() + bounds[(size_t)g], seg_clean.begin() + bounds[(size_t)g + 1]);
                        if (max_frames_cap >= eff_min && (int)sub.size() > max_frames_cap) {
                            sub = select_phase_diverse_subset(sub, max_frames_cap, res.diags, true);
                        }
                        clean_groups_4plus.push_back(finalize_planned_group(sub, dark_idx, uframes, res.diags, thumbs, anchor_mode));
                    }
                }
            } else {
                std::vector<int> sub = seg_clean;
                if (max_frames_cap > 0 && (int)sub.size() > max_frames_cap) {
                    sub = select_phase_diverse_subset(sub, max_frames_cap, res.diags, true);
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
        double crown_acu = std::max(1e-9, res.diags[crown_idx].acutance);
        for (auto& dg : res.diags) {
            dg.af_score = std::max(1, std::min(99, (int)std::lround(99.0 * (dg.acutance / crown_acu))));
        }
        const auto& ufc = uframes[crown_idx];
        rx.cfa_type = ufc.is_xtrans ? "xtrans" : "bayer";
        rx.olpf_auto = detect_sensor_olpf_profile(ufc.make, ufc.model, ufc.W, ufc.H, ufc.is_xtrans);
        rx.shutter_type = ufc.shutter_kind == 2 ? "global" :
            (ufc.shutter_kind == 1 ? "elec" : (ufc.shutter_kind == 0 ? "mech" : "unknown"));
        rx.has_ois = ufc.has_ois;
        rx.af_norm_x = ufc.af_norm_x;
        rx.af_norm_y = ufc.af_norm_y;
        rx.aperture = ufc.aperture;
        rx.shutter_s = ufc.shutter_s;
        rx.focus_distance_m = ufc.focus_distance_m;
        rx.focal_opt_mm = ufc.focal_opt_mm;
        rx.focus_distance_low_m = ufc.focus_distance_low_m;
        rx.focus_distance_high_m = ufc.focus_distance_high_m;
        rx.dof_near_m = ufc.dof_near_m;
        rx.dof_far_m = ufc.dof_far_m;
        rx.hyperfocal_m = ufc.hyperfocal_m;
        rx.focus_is_infinity = ufc.focus_is_infinity;
        rx.infinity_in_dof = ufc.infinity_in_dof;
        int clean_cnt = 0;
        float f_min = 1e30f, f_max = -1e30f;
        int f_valid = 0;
        float st_min = 1e30f, st_max = -1e30f;
        int st_valid = 0;
        float max_par_coh = 0.0f;
        for (int idx : same_dim_lights) {
            rx.max_drift_2x_px = std::max(rx.max_drift_2x_px, 2.0f * res.diags[idx].shift_1x);
            if (res.diags[idx].flags == SUIT_OK) {
                ++clean_cnt;
                rx.max_parallax_2x_px = std::max(rx.max_parallax_2x_px, 2.0f * res.diags[idx].local_spread_1x);
                max_par_coh = std::max(max_par_coh, diag_par_coh[(size_t)idx]);
                if (uframes[idx].focus_distance_m > 0.0f) {
                    const float diop = 1.0f / uframes[idx].focus_distance_m;
                    f_min = std::min(f_min, diop);
                    f_max = std::max(f_max, diop);
                    ++f_valid;
                }
                if (uframes[idx].focus_step > 0.0f) {
                    st_min = std::min(st_min, uframes[idx].focus_step);
                    st_max = std::max(st_max, uframes[idx].focus_step);
                    ++st_valid;
                }
            }
        }
        rx.has_focus_steps = (f_valid >= 3 && (f_max - f_min) > 1e-6f) ||
                             (st_valid >= 3 && (st_max - st_min) > 1e-4f);
        // Also evaluate peak-to-peak baseline pair across clean frames when anchor is mid-sweep
        if (clean_cnt >= 3) {
            int a_best = -1, b_best = -1;
            float max_pair_sh = 0.0f;
            for (size_t i = 0; i < same_dim_lights.size(); ++i) {
                int ia = same_dim_lights[i];
                if (res.diags[ia].flags != SUIT_OK) continue;
                for (size_t j = i + 1; j < same_dim_lights.size(); ++j) {
                    int ib = same_dim_lights[j];
                    if (res.diags[ib].flags != SUIT_OK) continue;
                    float d = std::hypot(res.diags[ia].dx_1x - res.diags[ib].dx_1x,
                                         res.diags[ia].dy_1x - res.diags[ib].dy_1x);
                    if (d > max_pair_sh) { max_pair_sh = d; a_best = ia; b_best = ib; }
                }
            }
            if (a_best >= 0 && b_best >= 0) {
                rx.max_drift_2x_px = std::max(rx.max_drift_2x_px, 2.0f * max_pair_sh);
                float dx_p = 0, dy_p = 0, sh_p = 0, ncc_p = 0, sp_p = 0, coh_p = 0;
                measure_pair_motion_1x(thumbs[a_best], thumbs[b_best], dx_p, dy_p, sh_p, ncc_p, sp_p, &coh_p);
                if (ncc_p >= 0.45f) {
                    rx.max_parallax_2x_px = std::max(rx.max_parallax_2x_px, 2.0f * sp_p);
                    max_par_coh = std::max(max_par_coh, coh_p);
                }
            }
        }
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
        if (ref_grp) {
            rx.quad_coverage = ref_grp->quad_coverage;
            rx.phi_schur_2d = ref_grp->phi_schur_2d;
            rx.phi_schur_x = ref_grp->phi_schur_x;
            rx.phi_schur_y = ref_grp->phi_schur_y;
            const size_t n_grp = ref_grp->light_uframe_indices.size();
            rx.snr_gain_db_est = (n_grp >= 1) ? (float)(10.0 * std::log10((double)n_grp)) : 0.0f;
            if (n_grp < 2) {
                rx.prelim_suitability = "INSUFFICIENT_EVIDENCE";
            } else if (n_grp < 4 || ref_grp->phi_schur_2d < 0.35f || ref_grp->motion == MotionRegime::TOO_SMALL_TRIPOD) {
                rx.prelim_suitability = "RECOMMEND_1X";
            } else if (ref_grp->phi_schur_2d >= 0.55f && rx.aperture <= 11.0f) {
                rx.prelim_suitability = "STRONG_2X";
            } else {
                rx.prelim_suitability = "MARGINAL_2X";
            }
        } else if (!same_dim_lights.empty()) {
            rx.snr_gain_db_est = (float)(10.0 * std::log10((double)same_dim_lights.size()));
            rx.prelim_suitability = (same_dim_lights.size() < 2) ? "INSUFFICIENT_EVIDENCE" : "RECOMMEND_1X";
        }
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
        if (ufc.clearly_near_macro && thumbs[crown_idx].stars_snr65_cnt < 25) {
            is_astro_scene = false;
        }
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
        if (rx.scene_code == "astro_sky") { rx.max_parallax_2x_px = 0.0f; max_par_coh = 0.0f; }
        rx.parallax_coherence = max_par_coh;
        const float eff_min_2x = (depth_gate.min_disparity_1x_px >= 0.0f)
            ? (2.0f * depth_gate.min_disparity_1x_px)
            : ((depth_gate.mode == 1) ? 1.4f : 3.0f);
        const float eff_coh = (depth_gate.min_coherence >= 0.0f)
            ? depth_gate.min_coherence
            : ((depth_gate.mode == 1) ? 0.25f : 0.45f);
        if (depth_gate.mode == 2) {
            rx.depth_observable = (clean_cnt >= 2 || (policy == ExecPolicy::FORCE_ALL && same_dim_lights.size() >= 2));
        } else {
            rx.depth_observable = (clean_cnt >= 3 && rx.scene_code != "astro_sky" &&
                                   rx.scene_code != "tripod_static" &&
                                   rx.max_parallax_2x_px >= eff_min_2x &&
                                   max_par_coh >= eff_coh);
        }
    }

    // 6. Apply Execution Policy
    if (policy == ExecPolicy::FORCE_ALL) {
        std::vector<int> keep = same_dim_lights;
        if (keep.empty()) {
            for (size_t i = 0; i < uframes.size(); ++i)
                if (uframes[i].status == 0 && uframes[i].W == uframes[0].W && uframes[i].H == uframes[0].H)
                    keep.push_back((int)i);
        }
        if (max_frames_cap > 0 && (int)keep.size() > max_frames_cap) {
            keep = select_phase_diverse_subset(keep, max_frames_cap, res.diags, true);
        }
        res.selected_groups = {finalize_planned_group(keep, dark_idx, uframes, res.diags, thumbs, anchor_mode)};
        res.condition_code = "force_all";
        res.condition_summary = "Force-all policy active: retained all readable same-dimension light frames.";
        mark_assigned(res.selected_groups);
        return res;
    }

    const std::string min_s = std::to_string(eff_min);
    if (policy == ExecPolicy::MULTI_GROUP) {
        res.selected_groups = clean_groups_4plus;
        res.condition_code = res.selected_groups.empty() ? "no_valid_group_4plus" : "multi_group_ok";
        res.condition_summary = res.selected_groups.empty()
            ? ("No burst group with >= " + min_s + " suitable frames found.")
            : ("Discovered " + std::to_string(res.selected_groups.size()) + " burst group(s) (>= " + min_s + " valid frames each).");
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
        if (res.selected_groups.empty()) {
            size_t max_seg_clean = 0;
            for (const auto& seg : raw_segments) {
                size_t sc = 0;
                for (int idx : seg) if ((res.diags[idx].flags & (SUIT_MOTION_BLUR | SUIT_EXPOSURE_OUTLIER)) == 0) ++sc;
                if (sc > max_seg_clean) max_seg_clean = sc;
            }
            size_t shown_valid = (raw_segments.size() > 1 && max_seg_clean < all_clean_single.size())
                ? max_seg_clean : all_clean_single.size();
            res.condition_summary = "Fewer than " + min_s + " suitable frames remain (" + std::to_string(shown_valid) +
                " valid in largest aligned segment of " + std::to_string(same_dim_lights.size()) +
                " light frames). Use --rgb-scale 1x (min 3 frames) or --policy force-all.";
        } else {
            res.condition_summary = "Auto-prune policy selected clean burst frames (>= " + min_s + ").";
        }
        mark_assigned(res.selected_groups);
        return res;
    }

    // --- ExecPolicy::INTERACTIVE (At Most 2 Recommended Options!) ---
    // Case 1: Multiple groups or long continuous sequence (> 8 clean frames)
    if (clean_groups_4plus.size() >= 2) {
        res.needs_interactive_prompt = true;
        res.condition_code = "long_or_multi_burst";
        res.condition_summary = "Detected " + std::to_string(clean_groups_4plus.size()) +
            " distinct burst groups (>= " + min_s + " clean frames each).";

        InteractiveChoicePlan c1;
        c1.id = 1;
        c1.title = "Export all " + std::to_string(clean_groups_4plus.size()) + " valid burst groups (Recommended)";
        c1.detail = "Processes every valid >=" + min_s + "-frame group with automatic outlier pruning.";
        c1.groups = clean_groups_4plus;

        size_t best_g = 0;
        for (size_t g = 1; g < clean_groups_4plus.size(); ++g) {
            if (clean_groups_4plus[g].mean_acutance > clean_groups_4plus[best_g].mean_acutance) best_g = g;
        }
        InteractiveChoicePlan c2;
        c2.id = 2;
        c2.title = "Export only the single sharpest burst group (" +
                   std::to_string(clean_groups_4plus[best_g].light_uframe_indices.size()) + " frames)";
        c2.detail = "Produces one output from the highest-acutance coherent group.";
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

        // Option 1: Split into multiple balanced >=eff_min-frame groups OR cap to sharpest 8
        InteractiveChoicePlan c1;
        c1.id = 1;
        int n_sub = n_clean / grp_sz;
        if (n_sub >= 2 && n_clean / n_sub >= eff_min) {
            c1.title = "Partition into " + std::to_string(n_sub) + " balanced burst groups (Recommended for multi-shot)";
            c1.detail = "Splits the long burst into sequential high-SNR burst groups.";
            for (int k = 0; k < n_sub; ++k) {
                int s0 = (n_clean * k) / n_sub;
                int s1 = (n_clean * (k + 1)) / n_sub;
                std::vector<int> sub(all_clean_single.begin() + s0, all_clean_single.begin() + s1);
                c1.groups.push_back(finalize_planned_group(sub, dark_idx, uframes, res.diags, thumbs, anchor_mode));
            }
        } else {
            std::vector<int> top8 = select_phase_diverse_subset(all_clean_single, 8, res.diags, true);
            c1.title = "Select 8 phase-diverse sharpest frames for optimal single-image SR (Recommended)";
            c1.detail = "Maximizes 2x2 subpixel quadrant coverage & AF sharpness without diminishing-return runtime.";
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

    // Case 2: Insufficient clean frames (< eff_min)
    if (clean_groups_4plus.empty()) {
        res.needs_interactive_prompt = true;
        if ((int)same_dim_lights.size() >= eff_min) {
            res.condition_code = "anomalies_reduced_below_4";
            res.condition_summary = "Strict suitability rejected " + std::to_string(dropped_anomalies) +
                " anomalous/blurred/shifted frame(s), leaving " + std::to_string(all_clean_single.size()) +
                " (< " + min_s + " minimum).";

            // Rank same_dim_lights by quality (preferring OK frames, then highest zncc * acutance)
            std::vector<int> topk = same_dim_lights;
            std::stable_sort(topk.begin(), topk.end(), [&](int a, int b) {
                bool oka = (res.diags[a].flags == SUIT_OK);
                bool okb = (res.diags[b].flags == SUIT_OK);
                if (oka != okb) return oka > okb;
                return (res.diags[a].acutance * std::max(0.1f, res.diags[a].zncc)) >
                       (res.diags[b].acutance * std::max(0.1f, res.diags[b].zncc));
            });
            topk.resize((size_t)eff_min);

            InteractiveChoicePlan c1;
            c1.id = 1;
            c1.title = "Retain top " + min_s + " best-aligned frames to preserve phase coverage (Recommended)";
            c1.detail = "Relaxes blur/motion threshold on borderline frames with pixel-wise de-ghosting.";
            c1.groups = {finalize_planned_group(topk, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

            std::vector<int> strict_keep = all_clean_single.empty()
                ? std::vector<int>{topk[0]}
                : all_clean_single;
            InteractiveChoicePlan c2;
            c2.id = 2;
            c2.title = "Force-execute strictly on the " + std::to_string(strict_keep.size()) + " cleanest frame(s) (< " + min_s + " fallback)";
            c2.detail = "Prioritizes zero outlier contamination; spatial resolution degrades gracefully.";
            c2.groups = {finalize_planned_group(strict_keep, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

            res.options = {c1, c2};
            res.selected_groups = c1.groups;
        } else {
            res.condition_code = "under_4_input_frames";
            res.condition_summary = "Only " + std::to_string(same_dim_lights.size()) +
                " light frame(s) provided (standard mode requires >= " + min_s + " frames; 1x True-RGB supports >= 3 frames).";

            InteractiveChoicePlan c1;
            c1.id = 1;
            c1.title = "Force-execute " + std::to_string(same_dim_lights.size()) + "-frame fusion (Recommended if no more frames)";
            c1.detail = "Runs anisotropic multi-frame fusion + GAT-DCT denoise on available frame(s).";
            c1.groups = {finalize_planned_group(same_dim_lights, dark_idx, uframes, res.diags, thumbs, anchor_mode)};

            InteractiveChoicePlan c2;
            c2.id = 2;
            c2.title = "Cancel execution now (add >= " + min_s + " burst frames)";
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

// F-G: Marginal Fisher information (delta log det) Next-Shot Planner.
struct NextShotRecommendation {
    float shift_y_1x_px = 0.0f;
    float shift_x_1x_px = 0.5f;
    float delta_log_det = 0.0f;
    float ev_offset_stop = 0.0f;
    float focus_step_diopters = 0.0f;
    int covered_quadrants = 0;
    std::string summary;
};

inline NextShotRecommendation compute_next_shot_plan(
    std::span<const std::array<float, 2>> shifts_yx_1x,
    float clipped_fraction = 0.0f,
    float focus_span_diopters = 0.0f,
    int distinct_focus_steps = 1) {
    NextShotRecommendation rec;
    auto phase_basis = [](float dy, float dx) -> std::array<double, 4> {
        auto wrap1 = [](float v) {
            float r = v - std::floor(v);
            return (r < 0.0f) ? (r + 1.0f) : r;
        };
        const float py = wrap1(dy), px = wrap1(dx);
        const float qy[4] = {0.0f, 0.0f, 0.5f, 0.5f};
        const float qx[4] = {0.0f, 0.5f, 0.0f, 0.5f};
        std::array<double, 4> b{};
        double s = 0.0;
        for (int q = 0; q < 4; ++q) {
            const double d_y = std::fabs(py - qy[q]);
            const double d_x = std::fabs(px - qx[q]);
            const double cy = std::min(d_y, 1.0 - d_y);
            const double cx = std::min(d_x, 1.0 - d_x);
            b[static_cast<size_t>(q)] = std::exp(-6.0 * (cy * cy + cx * cx));
            s += b[static_cast<size_t>(q)] * b[static_cast<size_t>(q)];
        }
        const double inv = (s > 1e-12) ? (1.0 / std::sqrt(s)) : 1.0;
        for (int q = 0; q < 4; ++q) b[static_cast<size_t>(q)] *= inv;
        return b;
    };

    double F[4][4] = {};
    for (int i = 0; i < 4; ++i) F[i][i] = 1e-2;
    int quad_mask = 0;
    for (const auto& sh : shifts_yx_1x) {
        const auto b = phase_basis(sh[0], sh[1]);
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) F[r][c] += b[static_cast<size_t>(r)] * b[static_cast<size_t>(c)];
        auto wrap1 = [](float v) { float r = v - std::floor(v); return r < 0 ? r + 1 : r; };
        int iy = (wrap1(sh[0]) >= 0.25f && wrap1(sh[0]) < 0.75f) ? 1 : 0;
        int ix = (wrap1(sh[1]) >= 0.25f && wrap1(sh[1]) < 0.75f) ? 1 : 0;
        quad_mask |= (1 << (2 * iy + ix));
    }
    for (int q = 0; q < 4; ++q) if (quad_mask & (1 << q)) ++rec.covered_quadrants;

    // Invert 4x4 symmetric PD matrix F
    double invF[4][4] = {};
    {
        double M[4][8] = {};
        for (int r = 0; r < 4; ++r) {
            for (int c = 0; c < 4; ++c) M[r][c] = F[r][c];
            M[r][4 + r] = 1.0;
        }
        for (int col = 0; col < 4; ++col) {
            int piv = col;
            for (int r = col + 1; r < 4; ++r)
                if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
            if (piv != col) for (int k = 0; k < 8; ++k) std::swap(M[col][k], M[piv][k]);
            const double d = std::max(1e-12, M[col][col]);
            for (int k = 0; k < 8; ++k) M[col][k] /= d;
            for (int r = 0; r < 4; ++r) {
                if (r == col) continue;
                const double f = M[r][col];
                for (int k = 0; k < 8; ++k) M[r][k] -= f * M[col][k];
            }
        }
        for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) invF[r][c] = M[r][4 + c];
    }

    static constexpr float kCand[4][2] = {{0.0f, 0.0f}, {0.0f, 0.5f}, {0.5f, 0.0f}, {0.5f, 0.5f}};
    double best_dld = -1.0;
    for (int q = 0; q < 4; ++q) {
        const auto b = phase_basis(kCand[q][0], kCand[q][1]);
        double quad_form = 0.0;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) quad_form += b[static_cast<size_t>(r)] * invF[r][c] * b[static_cast<size_t>(c)];
        const double dld = std::log(std::max(1.0, 1.0 + quad_form));
        if (dld > best_dld) {
            best_dld = dld;
            rec.shift_y_1x_px = kCand[q][0];
            rec.shift_x_1x_px = kCand[q][1];
            rec.delta_log_det = static_cast<float>(dld);
        }
    }
    if (clipped_fraction > 0.005f) rec.ev_offset_stop = -2.5f;
    if (focus_span_diopters > 0.25f && distinct_focus_steps < 3) {
        rec.focus_step_diopters = 0.35f * focus_span_diopters;
    }
    char msg[192];
    std::snprintf(msg, sizeof(msg),
        "Next shot shift=(%.2f, %.2f) 1x px (d_logdet=%.3f, covered_quad=%d/4, ev=%.1f, dF=%.2f m^-1)",
        rec.shift_y_1x_px, rec.shift_x_1x_px, rec.delta_log_det, rec.covered_quadrants,
        rec.ev_offset_stop, rec.focus_step_diopters);
    rec.summary = msg;
    return rec;
}

} // namespace heraw_plan
