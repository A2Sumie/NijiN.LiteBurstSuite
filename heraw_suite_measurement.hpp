// heraw_suite_measurement.hpp -- Stage 1 Unified Measurement Substrate:
//   - P0-CLK / R11: ClockProvenance + 0.1 Hz Error-Budget Certificate
//   - P0-F / E01 / E05 / E11: NativeSampleView + PreDestripeRowTap (Tap-0)
//   - Q10: Trajectory Characteristic-Function Flat-Field / C13 Spectral Gate
//   - Native .DXO Multi-SubIFD + MakerNote (ImageTimeStamps + Gyro) Reader
// Pure 7-bit ASCII C++20 header. Zero external dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
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

namespace heraw_meas {

enum class ClockSource : uint8_t {
    PtsTicks = 0,
    HwExposure = 1,
    MakerNoteDxoTs = 2,
    ExifSubsecMs = 3,
    ModelEstimate = 4
};

inline const char* clock_source_name(ClockSource s) noexcept {
    switch (s) {
        case ClockSource::PtsTicks: return "pts_ticks";
        case ClockSource::HwExposure: return "hw_exposure";
        case ClockSource::MakerNoteDxoTs: return "makernote_dxo_ts";
        case ClockSource::ExifSubsecMs: return "exif_subsec_ms";
        case ClockSource::ModelEstimate: return "model_estimate";
    }
    return "model_estimate";
}

// Five-field clock provenance & error-budget record (P0-CLK + Astra R11).
struct ClockProvenance {
    ClockSource source = ClockSource::ModelEstimate;
    int64_t tick_num = 0;
    uint64_t tick_den = 1;
    double tick_resolution_s = 1e-3;
    double timestamp_s = 0.0;
    double uncertainty_s = 5e-4;
    double exposure_offset_s = 0.0;
    double drift_ppm = 50.0;
    double row_period_s = 0.0;
    double exposure_s = 0.0;
    int readout_dir = 1; // +1 = top->bottom, -1 = bottom->top, 0 = global shutter
    bool row_calibrated = false;

    static ClockProvenance from_pts_ticks(
        int64_t num, uint64_t den, double base_epoch_s,
        double row_period_sec, double exp_sec, bool row_cal, int dir = 1
    ) noexcept {
        ClockProvenance p;
        p.source = ClockSource::PtsTicks;
        p.tick_num = num;
        p.tick_den = (den > 0) ? den : 1ULL;
        p.tick_resolution_s = 1.0 / static_cast<double>(p.tick_den);
        p.timestamp_s = base_epoch_s + static_cast<double>(num) / static_cast<double>(p.tick_den);
        p.uncertainty_s = 0.5 * p.tick_resolution_s + 2e-6;
        p.drift_ppm = 25.0;
        p.row_period_s = row_period_sec;
        p.exposure_s = exp_sec;
        p.readout_dir = dir;
        p.row_calibrated = row_cal;
        return p;
    }
};

struct FreqCertResult {
    bool hz_calibrated = false;
    bool certified_within_tol = false;
    double crb_sigma_hz = 0.0;
    double clock_sigma_hz = 0.0;
    double total_sigma_hz = 0.0;
    double ci95_halfwidth_hz = 0.0;
};

// Astra R11: evaluate frequency uncertainty from full CRB + timing jitter + ppm scale drift.
inline FreqCertResult evaluate_freq_precision_budget(
    double freq_hz,
    double span_s,
    double fisher_info_hz2,
    const ClockProvenance& cp,
    double target_tol_hz = 0.1
) noexcept {
    FreqCertResult out;
    out.hz_calibrated = (cp.source == ClockSource::PtsTicks ||
                         cp.source == ClockSource::HwExposure ||
                         cp.source == ClockSource::MakerNoteDxoTs ||
                         cp.source == ClockSource::ExifSubsecMs) &&
                        (span_s > 1e-4) && std::isfinite(freq_hz);
    const double eff_span = std::max(1e-4, span_s);
    out.crb_sigma_hz = (fisher_info_hz2 > 1e-12)
        ? (1.0 / std::sqrt(fisher_info_hz2))
        : (1.0 / eff_span);
    const double jitter_hz = (std::abs(freq_hz) > 0.0)
        ? (std::abs(freq_hz) * (cp.uncertainty_s / eff_span))
        : (cp.uncertainty_s / eff_span);
    const double ppm_hz = std::abs(freq_hz) * (cp.drift_ppm * 1e-6);
    out.clock_sigma_hz = std::hypot(jitter_hz, ppm_hz);
    out.total_sigma_hz = std::hypot(out.crb_sigma_hz, out.clock_sigma_hz);
    out.ci95_halfwidth_hz = 1.95996398454 * out.total_sigma_hz;
    out.certified_within_tol = out.hz_calibrated && cp.row_calibrated &&
                               (out.ci95_halfwidth_hz <= target_tol_hz);
    return out;
}

// Immutable per-frame physical sample descriptors extracted at decode/linearize boundary (E01/E05/E11).
struct NativeFrameContract {
    int frame_index = 0;
    float exp_scale = 1.0f;       // multiplier taking normalized working domain back to native linear: mu_nat = exp_scale * x_norm
    float read_var_norm = 0.0f;   // read noise variance in normalized working domain (E05 unified)
    float shot_coeff_norm = 0.0f; // shot coefficient in normalized working domain
    float quant_c0_norm = 0.0f;   // constant quantization term in normalized working domain
    float quant_c1_norm = 0.0f;   // linear quantization term in normalized working domain
    float quant_c2_norm = 0.0f;   // quadratic quantization term in normalized working domain
    float sat_norm = 0.98f;       // saturation threshold in normalized working domain
    ClockProvenance clock;

    // Evaluate total noise variance in normalized working domain once per observation (no double counting).
    float total_variance_norm(float x_norm) const noexcept {
        const float x_pos = std::max(0.0f, x_norm);
        const float qv = std::max(0.0f, quant_c0_norm + x_pos * (quant_c1_norm + x_pos * quant_c2_norm));
        return std::max(1e-12f, read_var_norm + shot_coeff_norm * x_pos + qv);
    }

    float radiometric_precision_norm(float x_norm) const noexcept {
        return 1.0f / total_variance_norm(x_norm);
    }
};

// Tap-0 row photometric summary captured BEFORE apply_row_destripe (E11 / O2 / O5 / O7).
struct PreDestripeFrameTap {
    int H1 = 0;
    int W1 = 0;
    ClockProvenance clock;
    std::vector<float> row_green_mean;
    std::vector<uint32_t> row_green_valid;
};

struct PreDestripeTapSet {
    int H1 = 0;
    int W1 = 0;
    bool valid = false;
    std::vector<PreDestripeFrameTap> frames;
};

template <typename ReadFn>
inline PreDestripeTapSet capture_pre_destripe_tap(
    int N, int H1, int W1, const uint8_t cfa6x6[6][6],
    const std::vector<float>& sat_limits_norm,
    const std::vector<ClockProvenance>& clocks,
    ReadFn&& read_frame_row
) {
    PreDestripeTapSet out;
    out.H1 = H1;
    out.W1 = W1;
    if (N <= 0 || H1 < 2 || W1 < 2) return out;
    const bool is_bayer = !((cfa6x6[0][0] == 1 && cfa6x6[0][1] == 1) ||
                            (cfa6x6[0][0] == cfa6x6[2][0] && cfa6x6[0][1] != cfa6x6[0][3]));
    (void)is_bayer;
    out.frames.resize(static_cast<size_t>(N));
    for (int n = 0; n < N; ++n) {
        auto& ft = out.frames[static_cast<size_t>(n)];
        ft.H1 = H1;
        ft.W1 = W1;
        if (n < static_cast<int>(clocks.size())) ft.clock = clocks[static_cast<size_t>(n)];
        ft.row_green_mean.assign(static_cast<size_t>(H1), 0.0f);
        ft.row_green_valid.assign(static_cast<size_t>(H1), 0u);
        const float sat_n = (n < static_cast<int>(sat_limits_norm.size()))
            ? sat_limits_norm[static_cast<size_t>(n)] : 0.98f;
        const int col_step = std::max(1, W1 / 256);
        for (int r = 0; r < H1; ++r) {
            const float* row = read_frame_row(n, r);
            if (!row) continue;
            const int rm = r % 6;
            double sum = 0.0;
            uint32_t cnt = 0u;
            for (int c = 0; c < W1; c += col_step) {
                int cc = c;
                if (cfa6x6[rm][cc % 6] != 1) {
                    if (cc + 1 < W1 && cfa6x6[rm][(cc + 1) % 6] == 1) ++cc;
                    else continue;
                }
                const float v = row[cc];
                if (std::isfinite(v) && v >= -0.05f && v < 0.96f * sat_n) {
                    sum += static_cast<double>(v);
                    ++cnt;
                }
            }
            if (cnt > 0u) {
                ft.row_green_mean[static_cast<size_t>(r)] = static_cast<float>(sum / static_cast<double>(cnt));
                ft.row_green_valid[static_cast<size_t>(r)] = cnt;
            }
        }
    }
    out.valid = true;
    return out;
}

// Q10: Trajectory Characteristic-Function Flat-Field / C13 Spectral Identifiability Gate.
// Given shifts (dy_k, dx_k) in 1x native px and frame weights w_k, evaluates:
//   chi_d(theta) = (sum w_k e^{-i (theta_y dy_k + theta_x dx_k)}) / (sum w_k)
//   F_a(theta) = S_w * (1 - |chi_d(theta)|^2)
//   T_a(theta) = (F_a >= gate_thr * S_w) ? F_a / (F_a + lambda_a) : 0
struct FlatFieldSpectralSample {
    double chi_re = 1.0;
    double chi_im = 0.0;
    double abs_chi_sq = 1.0;
    double norm_info = 0.0;  // 1 - |chi_d(theta)|^2 in [0, 1]
    double total_info = 0.0; // S_w * norm_info
    double wiener_gain = 0.0;
    bool identifiable = false;
};

inline FlatFieldSpectralSample eval_flatfield_spectral_transfer(
    const float* dy_1x,
    const float* dx_1x,
    const float* weights,
    int n_frames,
    double theta_y, // rad / native-px (= 2*pi / lambda_y)
    double theta_x, // rad / native-px (= 2*pi / lambda_x)
    double lambda_reg = 0.05,
    double min_norm_info = 0.02
) noexcept {
    FlatFieldSpectralSample res;
    if (!dy_1x || !dx_1x || n_frames <= 0) return res;
    double sw = 0.0, cr = 0.0, ci = 0.0;
    for (int k = 0; k < n_frames; ++k) {
        const double wk = weights ? std::max(0.0, static_cast<double>(weights[k])) : 1.0;
        if (wk <= 0.0) continue;
        const double ph = theta_y * static_cast<double>(dy_1x[k]) + theta_x * static_cast<double>(dx_1x[k]);
        sw += wk;
        cr += wk * std::cos(ph);
        ci -= wk * std::sin(ph);
    }
    if (sw <= 1e-12) return res;
    res.chi_re = cr / sw;
    res.chi_im = ci / sw;
    res.abs_chi_sq = std::min(1.0, res.chi_re * res.chi_re + res.chi_im * res.chi_im);
    res.norm_info = std::max(0.0, 1.0 - res.abs_chi_sq);
    res.total_info = sw * res.norm_info;
    res.identifiable = (res.norm_info >= std::max(0.0, min_norm_info));
    if (res.identifiable && (res.total_info + lambda_reg) > 1e-12) {
        res.wiener_gain = res.total_info / (res.total_info + std::max(0.0, lambda_reg));
    } else {
        res.wiener_gain = 0.0;
    }
    return res;
}

} // namespace heraw_meas

// ---------------------------------------------------------------------------
// Native .DXO (DxO ONE SuperRAW) Multi-SubIFD + MakerNote + GainMap Reader
// Verified directly against ref/DXO_*.DXO & rtos_raw.bin IMX183ModeInfoList.
// ---------------------------------------------------------------------------
namespace heraw_dxo {

struct DxoSubIfdInfo {
    uint64_t ifd_offset = 0;
    uint32_t sub_index = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint16_t bits_per_sample = 0;
    uint16_t compression = 0;
    uint16_t photometric = 0;
    uint16_t samples_per_pixel = 0;
    std::vector<uint64_t> strip_offsets;
    std::vector<uint64_t> strip_byte_counts;
    uint32_t rows_per_strip = 0;
    bool is_cfa_frame = false;
};

struct DxoTimestampWindow {
    uint16_t start_hh = 0, start_mm = 0, start_ss = 0, start_ms = 0;
    uint16_t end_hh = 0, end_mm = 0, end_ss = 0, end_ms = 0;
    double start_rel_s = 0.0;
    double end_rel_s = 0.0;
    double center_rel_s = 0.0;
};

struct DxoGyroSample {
    uint16_t seq_ms = 0;
    double raw_x = 0.0, raw_y = 0.0, raw_z = 0.0;
};

struct DxoSharedMeta {
    bool valid = false;
    bool big_endian = false;
    uint64_t file_size = 0;
    std::string make = "DxO";
    std::string model = "DxO ONE";
    std::string datetime;
    uint16_t orientation = 1;
    int subsec_ms = 0;
    float focal_mm = 11.9f;
    float aperture = 0.0f;
    float shutter_s = 0.0f;
    float iso = 100.0f;
    uint32_t active_top = 0, active_left = 0, active_bottom = 0, active_right = 0;
    int active_w = 0, active_h = 0;
    int crop_x = 0, crop_y = 0, crop_w = 0, crop_h = 0;
    int aligned_top = 0, aligned_left = 0, out_w = 0, out_h = 0; // canonical even RGGB box shared by ALL frames
    uint8_t cfa2x2[4] = {0, 1, 1, 2};
    float black_c[4] = {200.0f, 200.0f, 200.0f, 200.0f};
    float white_level = 4094.0f;
    float wb[3] = {1.0f, 1.0f, 1.0f};
    double cam_xyz[3][3] = {{0}};
    bool has_lut = false;
    std::array<uint16_t, 256> lin_lut{};
    std::array<float, 256> quant_var_lut{}; // hardware companding quantization variance Delta_b^2 / 12
    std::vector<DxoSubIfdInfo> cfa_ifds;
    std::vector<DxoTimestampWindow> frame_windows;
    std::vector<DxoGyroSample> gyro_samples;
    double imx183_row_period_s = 876.0 / 72000000.0; // 12.1666667 us default normal still mode
    double imx183_frame_period_s = 3739.0 * 876.0 / 72000000.0; // 45.4911667 ms
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

inline bool is_dxo_path(const std::string& path) {
    size_t pos = path.rfind("#frame=");
    std::string base = (pos != std::string::npos) ? path.substr(0, pos) : path;
    size_t sl = base.find_last_of("/\\");
    std::string name = (sl == std::string::npos) ? base : base.substr(sl + 1);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    for (char& ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return ext == ".dxo" || ext == ".4rw";
}

namespace detail {

inline uint16_t rd16(const uint8_t* p, bool be) noexcept {
    return be ? static_cast<uint16_t>((uint16_t(p[0]) << 8) | p[1])
              : static_cast<uint16_t>((uint16_t(p[1]) << 8) | p[0]);
}

inline uint32_t rd32(const uint8_t* p, bool be) noexcept {
    return be ? ((uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3])
              : ((uint32_t(p[3]) << 24) | (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | p[0]);
}

inline int32_t rds32(const uint8_t* p, bool be) noexcept {
    uint32_t u = rd32(p, be);
    int32_t s = 0;
    std::memcpy(&s, &u, 4);
    return s;
}

inline uint32_t tiff_sz(uint16_t t) noexcept {
    switch (t) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

struct TiffTagRec {
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    uint32_t raw_val_off = 0;
    uint64_t entry_val_pos = 0;
};

inline bool read_file_slice(FILE* fp, uint64_t file_sz, uint64_t off, void* dst, size_t n) {
    if (!fp || off > file_sz || n > file_sz - off) return false;
#ifdef _WIN32
    if (_fseeki64(fp, static_cast<__int64>(off), SEEK_SET) != 0) return false;
#else
    if (fseeko(fp, static_cast<off_t>(off), SEEK_SET) != 0) return false;
#endif
    return std::fread(dst, 1, n, fp) == n;
}

inline bool read_ifd(FILE* fp, uint64_t file_sz, uint64_t off, bool be,
                     std::vector<TiffTagRec>& out_tags, uint64_t& next_ifd) {
    out_tags.clear();
    next_ifd = 0;
    uint8_t cnt_buf[2];
    if (!read_file_slice(fp, file_sz, off, cnt_buf, 2)) return false;
    const uint16_t cnt = rd16(cnt_buf, be);
    if (cnt == 0 || cnt > 1024) return false;
    std::vector<uint8_t> raw(size_t(cnt) * 12 + 4);
    if (!read_file_slice(fp, file_sz, off + 2, raw.data(), raw.size())) return false;
    out_tags.reserve(cnt);
    for (uint16_t i = 0; i < cnt; ++i) {
        const uint8_t* ep = raw.data() + size_t(i) * 12;
        TiffTagRec rec;
        rec.tag = rd16(ep, be);
        rec.type = rd16(ep + 2, be);
        rec.count = rd32(ep + 4, be);
        rec.raw_val_off = rd32(ep + 8, be);
        rec.entry_val_pos = off + 2 + uint64_t(i) * 12 + 8;
        out_tags.push_back(rec);
    }
    next_ifd = rd32(raw.data() + size_t(cnt) * 12, be);
    return true;
}

inline bool read_tag_bytes(FILE* fp, uint64_t file_sz, const TiffTagRec& rec,
                           std::vector<uint8_t>& out) {
    const uint32_t us = tiff_sz(rec.type);
    if (us == 0 || rec.count == 0) return false;
    const uint64_t total = uint64_t(us) * uint64_t(rec.count);
    if (total > 16ULL * 1024ULL * 1024ULL) return false;
    out.resize(static_cast<size_t>(total));
    const uint64_t src_off = (total <= 4) ? rec.entry_val_pos : uint64_t(rec.raw_val_off);
    return read_file_slice(fp, file_sz, src_off, out.data(), static_cast<size_t>(total));
}

inline std::vector<double> read_tag_numbers(FILE* fp, uint64_t file_sz,
                                            const TiffTagRec& rec, bool be) {
    std::vector<uint8_t> buf;
    std::vector<double> vals;
    if (!read_tag_bytes(fp, file_sz, rec, buf)) return vals;
    const uint32_t us = tiff_sz(rec.type);
    vals.reserve(rec.count);
    for (uint32_t i = 0; i < rec.count; ++i) {
        const uint8_t* p = buf.data() + size_t(i) * us;
        double v = 0.0;
        switch (rec.type) {
            case 1: case 7: v = static_cast<double>(p[0]); break;
            case 6: v = static_cast<double>(static_cast<int8_t>(p[0])); break;
            case 3: v = static_cast<double>(rd16(p, be)); break;
            case 8: v = static_cast<double>(static_cast<int16_t>(rd16(p, be))); break;
            case 4: v = static_cast<double>(rd32(p, be)); break;
            case 9: v = static_cast<double>(rds32(p, be)); break;
            case 5: {
                uint32_t num = rd32(p, be), den = rd32(p + 4, be);
                v = (den != 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 0.0;
                break;
            }
            case 10: {
                int32_t num = rds32(p, be), den = rds32(p + 4, be);
                v = (den != 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 0.0;
                break;
            }
            default: break;
        }
        vals.push_back(v);
    }
    return vals;
}

inline std::string read_tag_ascii(FILE* fp, uint64_t file_sz, const TiffTagRec& rec) {
    std::vector<uint8_t> buf;
    if (!read_tag_bytes(fp, file_sz, rec, buf)) return "";
    size_t len = 0;
    while (len < buf.size() && buf[len] != '\0') ++len;
    std::string s(reinterpret_cast<const char*>(buf.data()), len);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

inline const TiffTagRec* find_tag(const std::vector<TiffTagRec>& tags, uint16_t id) {
    for (const auto& t : tags) if (t.tag == id) return &t;
    return nullptr;
}

} // namespace detail

// Probe all CFA sub-frames and shared metadata from a .DXO SuperRAW DNG file.
inline bool probe_dxo_container(const std::string& path, DxoSharedMeta& out) {
    out = DxoSharedMeta{};
    FILE* fp = open_rb_utf8(path);
    if (!fp) return false;
#ifdef _WIN32
    _fseeki64(fp, 0, SEEK_END);
    uint64_t fsz = static_cast<uint64_t>(_ftelli64(fp));
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_END);
    uint64_t fsz = static_cast<uint64_t>(ftello(fp));
    fseeko(fp, 0, SEEK_SET);
#endif
    out.file_size = fsz;
    if (fsz < 64) { std::fclose(fp); return false; }

    uint8_t hdr[8];
    if (!detail::read_file_slice(fp, fsz, 0, hdr, 8)) { std::fclose(fp); return false; }
    bool be = false;
    if (hdr[0] == 'I' && hdr[1] == 'I') be = false;
    else if (hdr[0] == 'M' && hdr[1] == 'M') be = true;
    else { std::fclose(fp); return false; }
    if (detail::rd16(hdr + 2, be) != 42) { std::fclose(fp); return false; }
    out.big_endian = be;
    const uint32_t ifd0_off = detail::rd32(hdr + 4, be);

    std::vector<detail::TiffTagRec> ifd0;
    uint64_t next_ifd0 = 0;
    if (!detail::read_ifd(fp, fsz, ifd0_off, be, ifd0, next_ifd0)) {
        std::fclose(fp);
        return false;
    }

    if (const auto* t = detail::find_tag(ifd0, 271)) out.make = detail::read_tag_ascii(fp, fsz, *t);
    if (const auto* t = detail::find_tag(ifd0, 272)) out.model = detail::read_tag_ascii(fp, fsz, *t);
    if (const auto* t = detail::find_tag(ifd0, 306)) out.datetime = detail::read_tag_ascii(fp, fsz, *t);
    if (const auto* t = detail::find_tag(ifd0, 274)) {
        auto v = detail::read_tag_numbers(fp, fsz, *t, be);
        if (!v.empty()) out.orientation = static_cast<uint16_t>(std::lround(v[0]));
    }
    if (const auto* t = detail::find_tag(ifd0, 50721)) { // ColorMatrix1
        auto v = detail::read_tag_numbers(fp, fsz, *t, be);
        if (v.size() >= 9) {
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out.cam_xyz[r][c] = v[size_t(r) * 3 + c];
        }
    }
    if (const auto* t = detail::find_tag(ifd0, 50722)) { // ColorMatrix2 (prefer D65 if valid)
        auto v = detail::read_tag_numbers(fp, fsz, *t, be);
        if (v.size() >= 9) {
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out.cam_xyz[r][c] = v[size_t(r) * 3 + c];
        }
    }
    if (const auto* t = detail::find_tag(ifd0, 50728)) { // AsShotNeutral
        auto v = detail::read_tag_numbers(fp, fsz, *t, be);
        if (v.size() >= 3 && v[0] > 1e-6 && v[1] > 1e-6 && v[2] > 1e-6) {
            out.wb[0] = static_cast<float>(v[1] / v[0]);
            out.wb[1] = 1.0f;
            out.wb[2] = static_cast<float>(v[1] / v[2]);
        }
    }

    // Parse ExifIFD + "DXO ONE\0" MakerNote
    if (const auto* texif = detail::find_tag(ifd0, 34665)) {
        auto ev = detail::read_tag_numbers(fp, fsz, *texif, be);
        if (!ev.empty()) {
            std::vector<detail::TiffTagRec> exif;
            uint64_t nex = 0;
            if (detail::read_ifd(fp, fsz, static_cast<uint64_t>(ev[0]), be, exif, nex)) {
                if (const auto* t = detail::find_tag(exif, 33434)) {
                    auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                    if (!v.empty()) out.shutter_s = static_cast<float>(v[0]);
                }
                if (const auto* t = detail::find_tag(exif, 33437)) {
                    auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                    if (!v.empty()) out.aperture = static_cast<float>(v[0]);
                }
                if (const auto* t = detail::find_tag(exif, 34855)) {
                    auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                    if (!v.empty()) out.iso = static_cast<float>(v[0]);
                }
                if (const auto* t = detail::find_tag(exif, 37386)) {
                    auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                    if (!v.empty() && v[0] > 0.5) out.focal_mm = static_cast<float>(v[0]);
                }
                if (const auto* t = detail::find_tag(exif, 36867)) {
                    std::string s = detail::read_tag_ascii(fp, fsz, *t);
                    if (!s.empty()) out.datetime = s;
                }
                if (const auto* t = detail::find_tag(exif, 37521)) { // SubSecTimeOriginal
                    std::string s = detail::read_tag_ascii(fp, fsz, *t);
                    if (!s.empty()) {
                        while (s.size() < 3) s.push_back('0');
                        out.subsec_ms = std::clamp(std::atoi(s.substr(0, 3).c_str()), 0, 999);
                    }
                }
                if (const auto* tmn = detail::find_tag(exif, 37500)) {
                    std::vector<uint8_t> mn;
                    if (detail::read_tag_bytes(fp, fsz, *tmn, mn) &&
                        mn.size() >= 16 && std::memcmp(mn.data(), "DXO ONE\0", 8) == 0) {
                        const uint32_t hdr_sz = detail::rd32(mn.data() + 8, false);
                        if (hdr_sz >= 8 && hdr_sz + 2 <= mn.size()) {
                            const uint16_t mn_cnt = detail::rd16(mn.data() + hdr_sz, false);
                            for (uint16_t mi = 0; mi < mn_cnt && mi < 64; ++mi) {
                                const size_t pos = size_t(hdr_sz) + 2 + size_t(mi) * 12;
                                if (pos + 12 > mn.size()) break;
                                const uint16_t eid = detail::rd16(mn.data() + pos, false);
                                const uint16_t ecnt = detail::rd16(mn.data() + pos + 4, false);
                                const uint32_t eoff = detail::rd32(mn.data() + pos + 8, false);
                                if (eid == 15 && ecnt >= 8 && eoff + size_t(ecnt) * 2 <= mn.size()) {
                                    // id=15 ImageTimeStamps: 8 u16 per frame pair: (hh,mm,ss,ms) start, (hh,mm,ss,ms) end
                                    const size_t n_wins = ecnt / 8;
                                    out.frame_windows.reserve(n_wins);
                                    double base_s = 0.0;
                                    for (size_t wi = 0; wi < n_wins; ++wi) {
                                        const uint8_t* p = mn.data() + eoff + wi * 16;
                                        DxoTimestampWindow w;
                                        w.start_hh = detail::rd16(p + 0, false);
                                        w.start_mm = detail::rd16(p + 2, false);
                                        w.start_ss = detail::rd16(p + 4, false);
                                        w.start_ms = detail::rd16(p + 6, false);
                                        w.end_hh   = detail::rd16(p + 8, false);
                                        w.end_mm   = detail::rd16(p + 10, false);
                                        w.end_ss   = detail::rd16(p + 12, false);
                                        w.end_ms   = detail::rd16(p + 14, false);
                                        const double ts0 = ((w.start_hh * 60.0 + w.start_mm) * 60.0 + w.start_ss) + 0.001 * w.start_ms;
                                        double ts1 = ((w.end_hh * 60.0 + w.end_mm) * 60.0 + w.end_ss) + 0.001 * w.end_ms;
                                        if (ts1 < ts0) ts1 += 86400.0;
                                        if (wi == 0) base_s = ts0;
                                        double rel0 = ts0 - base_s;
                                        if (rel0 < -1000.0) rel0 += 86400.0;
                                        double rel1 = ts1 - base_s;
                                        if (rel1 < -1000.0) rel1 += 86400.0;
                                        w.start_rel_s = rel0;
                                        w.end_rel_s = rel1;
                                        w.center_rel_s = 0.5 * (rel0 + rel1);
                                        out.frame_windows.push_back(w);
                                    }
                                } else if (eid == 14 && ecnt >= 7 && eoff + size_t(ecnt) * 2 <= mn.size()) {
                                    // id=14 AdvancedGyroInformation: 7 x u16 (hh, mm, ss, seq_ms, gx, gy, gz)
                                    const size_t n_rec = ecnt / 7;
                                    out.gyro_samples.reserve(n_rec);
                                    for (size_t gi = 0; gi < n_rec; ++gi) {
                                        const uint8_t* p = mn.data() + eoff + gi * 14;
                                        DxoGyroSample g;
                                        g.seq_ms = detail::rd16(p + 6, false);
                                        g.raw_x = static_cast<double>(detail::rd16(p + 8, false)) - 32768.0;
                                        g.raw_y = static_cast<double>(detail::rd16(p + 10, false)) - 32768.0;
                                        g.raw_z = static_cast<double>(detail::rd16(p + 12, false)) - 32768.0;
                                        out.gyro_samples.push_back(g);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    if (out.shutter_s > 0.046f) {
        // rtos_raw.bin Mode 10: line_time=1752 @ 72 MHz = 24.3333 us/row
        out.imx183_row_period_s = 1752.0 / 72000000.0;
    }

    // Collect SubIFDs (tag 330)
    std::vector<uint64_t> sub_offs;
    if (const auto* tsub = detail::find_tag(ifd0, 330)) {
        auto v = detail::read_tag_numbers(fp, fsz, *tsub, be);
        for (double x : v) sub_offs.push_back(static_cast<uint64_t>(x));
    }
    if (sub_offs.empty()) { std::fclose(fp); return false; }

    bool got_shared_cfa_meta = false;
    uint32_t raw_w_ref = 0, raw_h_ref = 0;

    for (size_t si = 0; si < sub_offs.size(); ++si) {
        std::vector<detail::TiffTagRec> stags;
        uint64_t snext = 0;
        if (!detail::read_ifd(fp, fsz, sub_offs[si], be, stags, snext)) continue;

        DxoSubIfdInfo info;
        info.sub_index = static_cast<uint32_t>(si);
        info.ifd_offset = sub_offs[si];
        auto get_u32 = [&](uint16_t tag, uint32_t def = 0) -> uint32_t {
            const auto* t = detail::find_tag(stags, tag);
            if (!t) return def;
            auto v = detail::read_tag_numbers(fp, fsz, *t, be);
            return v.empty() ? def : static_cast<uint32_t>(std::lround(v[0]));
        };
        info.width = get_u32(256);
        info.height = get_u32(257);
        info.bits_per_sample = static_cast<uint16_t>(get_u32(258));
        info.compression = static_cast<uint16_t>(get_u32(259, 1));
        info.photometric = static_cast<uint16_t>(get_u32(262));
        info.samples_per_pixel = static_cast<uint16_t>(get_u32(277, 1));
        info.rows_per_strip = get_u32(278, info.height);
        if (const auto* toff = detail::find_tag(stags, 273)) {
            auto v = detail::read_tag_numbers(fp, fsz, *toff, be);
            for (double x : v) info.strip_offsets.push_back(static_cast<uint64_t>(x));
        }
        if (const auto* tsbc = detail::find_tag(stags, 279)) {
            auto v = detail::read_tag_numbers(fp, fsz, *tsbc, be);
            for (double x : v) info.strip_byte_counts.push_back(static_cast<uint64_t>(x));
        }
        // Only single-channel uncompressed CFA raw frames (skip SubIFD previews!)
        const bool is_cfa = (info.photometric == 32803u && info.samples_per_pixel == 1u &&
                             (info.bits_per_sample == 8u || info.bits_per_sample == 16u) &&
                             info.compression == 1u && !info.strip_offsets.empty() &&
                             info.width >= 64u && info.height >= 64u);
        if (!is_cfa) continue;

        if (!got_shared_cfa_meta) {
            raw_w_ref = info.width;
            raw_h_ref = info.height;
            out.active_top = 0; out.active_left = 0;
            out.active_bottom = info.height; out.active_right = info.width;
            if (const auto* t = detail::find_tag(stags, 50829)) { // ActiveArea
                auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                if (v.size() >= 4) {
                    out.active_top = static_cast<uint32_t>(std::max(0.0, v[0]));
                    out.active_left = static_cast<uint32_t>(std::max(0.0, v[1]));
                    out.active_bottom = std::min(info.height, static_cast<uint32_t>(v[2]));
                    out.active_right = std::min(info.width, static_cast<uint32_t>(v[3]));
                }
            }
            out.active_w = static_cast<int>(out.active_right - out.active_left);
            out.active_h = static_cast<int>(out.active_bottom - out.active_top);
            out.crop_x = 0; out.crop_y = 0;
            out.crop_w = out.active_w; out.crop_h = out.active_h;
            if (const auto* t = detail::find_tag(stags, 50719)) {
                auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                if (v.size() >= 2) { out.crop_x = static_cast<int>(v[0]); out.crop_y = static_cast<int>(v[1]); }
            }
            if (const auto* t = detail::find_tag(stags, 50720)) {
                auto v = detail::read_tag_numbers(fp, fsz, *t, be);
                if (v.size() >= 2) { out.crop_w = static_cast<int>(v[0]); out.crop_h = static_cast<int>(v[1]); }
            }
            if (const auto* tcfa = detail::find_tag(stags, 33422)) {
                std::vector<uint8_t> cb;
                if (detail::read_tag_bytes(fp, fsz, *tcfa, cb) && cb.size() >= 4) {
                    for (int k = 0; k < 4; ++k) out.cfa2x2[k] = cb[size_t(k)];
                }
            }
            // Align canonical even RGGB output origin cleanly inside ActiveArea matching LibRaw active crop.
            int dy_r = 0, dx_r = 0;
            bool found_r = false;
            for (int dr = 0; dr < 2 && !found_r; ++dr) {
                for (int dc = 0; dc < 2 && !found_r; ++dc) {
                    auto ch = [&](int yy, int xx) {
                        return out.cfa2x2[(size_t(yy & 1) * 2) + size_t(xx & 1)];
                    };
                    if (ch(out.active_top + dr, out.active_left + dc) == 0 &&
                        ch(out.active_top + dr, out.active_left + dc + 1) == 1 &&
                        ch(out.active_top + dr + 1, out.active_left + dc) == 1 &&
                        ch(out.active_top + dr + 1, out.active_left + dc + 1) == 2) {
                        dy_r = dr; dx_r = dc; found_r = true;
                    }
                }
            }
            out.aligned_top = static_cast<int>(out.active_top) + dy_r;
            out.aligned_left = static_cast<int>(out.active_left) + dx_r;
            out.out_w = (std::max(0, out.active_w - dx_r)) & ~1;
            out.out_h = (std::max(0, out.active_h - dy_r)) & ~1;
            if (const auto* tblk = detail::find_tag(stags, 50714)) {
                auto v = detail::read_tag_numbers(fp, fsz, *tblk, be);
                if (!v.empty()) {
                    for (int k = 0; k < 4; ++k) {
                        out.black_c[k] = static_cast<float>(v[size_t(k) % v.size()]);
                    }
                }
            }
            if (const auto* twht = detail::find_tag(stags, 50717)) {
                auto v = detail::read_tag_numbers(fp, fsz, *twht, be);
                if (!v.empty()) out.white_level = static_cast<float>(v[0]);
            }
            if (const auto* tlut = detail::find_tag(stags, 50712)) {
                auto v = detail::read_tag_numbers(fp, fsz, *tlut, be);
                if (!v.empty()) {
                    out.has_lut = true;
                    const size_t n_lut = v.size();
                    for (size_t k = 0; k < 256; ++k) {
                        const size_t idx = std::min(k, n_lut - 1);
                        out.lin_lut[k] = static_cast<uint16_t>(std::clamp(std::lround(v[idx]), 0L, 65535L));
                    }
                    const float inv_span = 1.0f / std::max(1.0f, out.white_level - out.black_c[0]);
                    for (size_t k = 0; k < 256; ++k) {
                        if (k >= n_lut && n_lut > 0) {
                            out.quant_var_lut[k] = out.quant_var_lut[n_lut - 1];
                            continue;
                        }
                        const float lo = (k > 0)
                            ? 0.5f * static_cast<float>(out.lin_lut[k] + out.lin_lut[k - 1])
                            : std::max(0.0f, static_cast<float>(out.lin_lut[0]) -
                                0.5f * ((n_lut > 1) ? static_cast<float>(out.lin_lut[1] - out.lin_lut[0]) : 1.0f));
                        const float hi = (k + 1 < n_lut)
                            ? 0.5f * static_cast<float>(out.lin_lut[k + 1] + out.lin_lut[k])
                            : static_cast<float>(out.lin_lut[255]);
                        const float d = std::max(1.0f, hi - lo) * inv_span;
                        out.quant_var_lut[k] = (d * d) / 12.0f;
                    }
                }
            }
            got_shared_cfa_meta = true;
        }
        if (info.width != raw_w_ref || info.height != raw_h_ref) continue;
        info.is_cfa_frame = true;
        out.cfa_ifds.push_back(std::move(info));
    }

    std::fclose(fp);
    out.valid = (got_shared_cfa_meta && !out.cfa_ifds.empty() && out.out_w >= 64 && out.out_h >= 64);
    return out.valid;
}

// Read active canonical RGGB rows [r0, r1) (in out_h coordinates) directly into float or uint16 buffer.
// Supports both full-frame unpack and low-memory 2 GiB stripe streaming without touching LibRaw shot_select.
inline bool read_dxo_frame_rows_u16(
    const std::string& path,
    const DxoSharedMeta& meta,
    uint32_t frame_idx,
    int r0,
    int r1,
    uint16_t* dst_bayer
) {
    if (!meta.valid || frame_idx >= meta.cfa_ifds.size() || !dst_bayer) return false;
    if (r0 < 0) r0 = 0;
    if (r1 > meta.out_h) r1 = meta.out_h;
    if (r0 >= r1) return true;
    const auto& ifd = meta.cfa_ifds[frame_idx];
    FILE* fp = open_rb_utf8(path);
    if (!fp) return false;

    const int raw_w = static_cast<int>(ifd.width);
    const int n_rows = r1 - r0;
    std::vector<uint8_t> row_buf(static_cast<size_t>(raw_w) * (ifd.bits_per_sample == 16 ? 2 : 1));
    bool ok = true;

    for (int i = 0; i < n_rows; ++i) {
        const int src_row = meta.aligned_top + r0 + i;
        uint64_t row_off = 0;
        if (ifd.strip_offsets.size() == 1) {
            row_off = ifd.strip_offsets[0] + static_cast<uint64_t>(src_row) * static_cast<uint64_t>(row_buf.size());
        } else {
            const uint32_t rps = std::max<uint32_t>(1u, ifd.rows_per_strip);
            const size_t strip_i = static_cast<size_t>(src_row) / rps;
            const size_t in_strip = static_cast<size_t>(src_row) % rps;
            if (strip_i >= ifd.strip_offsets.size()) { ok = false; break; }
            row_off = ifd.strip_offsets[strip_i] + static_cast<uint64_t>(in_strip) * static_cast<uint64_t>(row_buf.size());
        }
        if (!detail::read_file_slice(fp, meta.file_size, row_off, row_buf.data(), row_buf.size())) {
            ok = false;
            break;
        }
        uint16_t* dst = dst_bayer + static_cast<size_t>(i) * static_cast<size_t>(meta.out_w);
        if (ifd.bits_per_sample == 8) {
            const uint8_t* src = row_buf.data() + meta.aligned_left;
            if (meta.has_lut) {
                for (int c = 0; c < meta.out_w; ++c) dst[c] = meta.lin_lut[src[c]];
            } else {
                for (int c = 0; c < meta.out_w; ++c) dst[c] = src[c];
            }
        } else {
            const uint8_t* src = row_buf.data() + static_cast<size_t>(meta.aligned_left) * 2;
            for (int c = 0; c < meta.out_w; ++c) dst[c] = detail::rd16(src + size_t(c) * 2, meta.big_endian);
        }
    }

    std::fclose(fp);
    return ok;
}

} // namespace heraw_dxo
