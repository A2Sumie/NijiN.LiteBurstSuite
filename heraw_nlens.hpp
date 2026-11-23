#ifndef HERAW_NLENS_HPP
#define HERAW_NLENS_HPP

// Vendor-neutral .nlens v1 open physical optical calibration model.
// Supports Tier 1 (datasheet/calibrated MTF), Tier 2 (asymmetric MTF + odd coma phase + spherical skirt),
// and Tier 3 (exit-pupil wavefront + mechanical cat's-eye stops + non-paraxial coverglass angular spectrum,
// evaluated via zero-padded 128x128 2D FFT linear pupil autocorrelation).

#include "heraw_fs_utf8.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace heraw_nlens {

enum class Tier : uint8_t {
    None = 0,
    DatasheetMtf = 1,
    LabMtfAsymmetric = 2,
    RaytraceWavefront = 3
};

struct OwnershipFlags {
    bool pixel_aperture = false;
    bool silicon_diffusion = false;
    bool chief_ray_lca_in_warp = true;
    bool geometry_tilt_removed = true;
};

struct MechanicalStop {
    double z_rel_exit_mm = 0.0;
    double radius_mm = 25.0;
    double decenter_x_mm = 0.0;
    double decenter_y_mm = 0.0;
};

struct ZernikeTerm {
    int n = 0;
    int m = 0;
    bool is_sin = false;
    double coeff_um = 0.0;
};

struct SphericalSkirt {
    double weight = 0.0;
    double sigma_um = 0.0;
};

struct DielectricLayer {
    double delta_d_mm = 0.0;
    double n_lambda = 1.5168;
};

struct StateSlice {
    double focal_mm = 50.0;
    double focus_m = 1000.0;
    double focus_diopter = 0.001;
    double aperture_fnum = 2.8;
    double exit_pupil_z_mm = 85.0;
    double signed_exit_z_mm = -85.0;
    double exit_diameter_mm = 0.0;
    double na_air = 0.0;
    double lambda_nm = 550.0;
    std::array<double, 3> spectral_rgb{1.0, 1.0, 1.0};
    bool has_chief_q = false;
    std::array<double, 2> chief_q_xy{0.0, 0.0};
    double cra_power_intensity = 3.0; // default cos^3 intensity => cos^1.5 amplitude
    double silicon_sigma_um = 0.0;
    double loca_um = 0.0;
    std::string phase_source = "signed_psf";
    std::array<double, 4> cubic_phase_rad_mm3{0.0, 0.0, 0.0, 0.0};
    bool has_cubic_phase_mm3 = false;
    std::vector<DielectricLayer> stack;
    std::vector<double> radii_mm;
    std::vector<std::array<double, 2>> field_xy_mm;
    std::vector<double> vignetting_ev;
    std::vector<double> distortion_pct;
    std::vector<double> lca_red_ppm;
    std::vector<double> lca_blue_ppm;
    std::vector<double> mtf_lp_mm;
    std::vector<std::vector<double>> mtf_s; // [r_idx][f_idx]
    std::vector<std::vector<double>> mtf_t; // [r_idx][f_idx]
    double mtf_se = 0.02;
    std::vector<double> coma_c31_waves;     // per radius
    SphericalSkirt skirt{};
    // Optional Tier 3 wavefront fields per radius
    std::vector<MechanicalStop> stops;
    std::vector<std::vector<ZernikeTerm>> wavefront_by_r;
};

struct EvaluatedFieldPoint {
    bool valid = false;
    Tier effective_tier = Tier::None;
    std::string fallback_reason;
    double vig_gain_linear = 1.0;
    double dist_pct = 0.0;
    double lca_r_frac = 0.0;
    double lca_b_frac = 0.0;
    SphericalSkirt skirt{};
    // Complex 2D OTF on 64x64 frequency grid (output-grid cycles, row-major ky=0..63, kx=0..63)
    // H(0,0) = 1.0 + 0i, strictly 64x64 Hermitian symmetric.
    std::array<float, 64 * 64> otf_re{};
    std::array<float, 64 * 64> otf_im{};
    double mtf50_1x_cy_px = 0.30;
    double eff_sigma_1x_px = 0.55;
};

struct Profile {
    bool loaded = false;
    Tier tier = Tier::None;
    std::string source_path;
    std::string lens_name;
    std::string mount_name;
    std::string target_sensor;
    std::string authority = "datasheet";
    std::vector<std::string> match_aliases;
    double fmin_mm = 0.0, fmax_mm = 0.0;
    double aopen_fnum = 0.0, amax_fnum = 0.0;
    double dmin_diopter = 0.0, dmax_diopter = 0.0;
    double rmax_cal_mm = 0.0;
    double image_circle_mm = 43.2666;
    double sensor_half_w_mm = 18.0;
    double sensor_half_h_mm = 12.0;
    double target_pitch_um = 4.35;
    double coverglass_mm = 1.20;
    double design_coverglass_mm = 1.20;
    double coverglass_nd = 1.5168;
    OwnershipFlags includes{};
    std::vector<StateSlice> states;
};

namespace detail {

struct JNode {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JNode> arr;
    std::vector<std::pair<std::string, JNode>> obj;

    const JNode* find(std::string_view k) const {
        if (type != OBJ) return nullptr;
        for (const auto& kv : obj) {
            if (kv.first == k) return &kv.second;
        }
        return nullptr;
    }
    double num_or(std::string_view k, double def) const {
        const JNode* n = find(k);
        return (n && n->type == NUM && std::isfinite(n->num)) ? n->num : def;
    }
    bool bool_or(std::string_view k, bool def) const {
        const JNode* n = find(k);
        return (n && n->type == BOOL) ? n->b : def;
    }
    std::string str_or(std::string_view k, std::string_view def = "") const {
        const JNode* n = find(k);
        return (n && n->type == STR) ? n->str : std::string(def);
    }
    std::vector<double> num_vec(std::string_view k) const {
        std::vector<double> out;
        const JNode* n = find(k);
        if (!n || n->type != ARR) return out;
        out.reserve(n->arr.size());
        for (const auto& el : n->arr) {
            out.push_back(el.type == NUM && std::isfinite(el.num) ? el.num : 0.0);
        }
        return out;
    }
};

class JParser {
    std::string_view s_;
    size_t i_ = 0;

    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_;
    }

public:
    explicit JParser(std::string_view s) : s_(s) {}

    bool parse_root(JNode& out) {
        skip_ws();
        if (!parse_val(out)) return false;
        skip_ws();
        return i_ == s_.size();
    }

private:
    bool parse_str(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        out.clear();
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i_ >= s_.size()) return false;
                char e = s_[i_++];
                switch (e) {
                    case '"': case '\\': case '/': out.push_back(e); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u':
                        if (i_ + 4 > s_.size()) return false;
                        i_ += 4;
                        out.push_back('?');
                        break;
                    default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parse_val(JNode& out) {
        skip_ws();
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '"') {
            out.type = JNode::STR;
            return parse_str(out.str);
        }
        if (c == '{') {
            out.type = JNode::OBJ;
            ++i_;
            skip_ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            while (i_ < s_.size()) {
                std::string k;
                skip_ws();
                if (!parse_str(k)) return false;
                skip_ws();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                JNode v;
                if (!parse_val(v)) return false;
                out.obj.emplace_back(std::move(k), std::move(v));
                skip_ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
                return false;
            }
            return false;
        }
        if (c == '[') {
            out.type = JNode::ARR;
            ++i_;
            skip_ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            while (i_ < s_.size()) {
                JNode v;
                if (!parse_val(v)) return false;
                out.arr.push_back(std::move(v));
                skip_ws();
                if (i_ < s_.size() && s_[i_] == ',') { ++i_; continue; }
                if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
                return false;
            }
            return false;
        }
        if (s_.substr(i_, 4) == "true") { out.type = JNode::BOOL; out.b = true; i_ += 4; return true; }
        if (s_.substr(i_, 5) == "false") { out.type = JNode::BOOL; out.b = false; i_ += 5; return true; }
        if (s_.substr(i_, 4) == "null") { out.type = JNode::NUL; i_ += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* beg = s_.data() + i_;
            char* endp = nullptr;
            std::string tmp(beg, std::min<size_t>(64, s_.size() - i_));
            double v = std::strtod(tmp.c_str(), &endp);
            if (endp == tmp.c_str() || !std::isfinite(v)) return false;
            out.type = JNode::NUM;
            out.num = v;
            i_ += static_cast<size_t>(endp - tmp.c_str());
            return true;
        }
        return false;
    }
};

inline double lerp1d(const std::vector<double>& xs, const std::vector<double>& ys, double x) {
    if (xs.empty() || ys.empty()) return 0.0;
    const size_t n = std::min(xs.size(), ys.size());
    if (n == 1 || x <= xs[0]) return ys[0];
    if (x >= xs[n - 1]) return ys[n - 1];
    size_t hi = 1;
    while (hi + 1 < n && xs[hi] < x) ++hi;
    const size_t lo = hi - 1;
    const double span = xs[hi] - xs[lo];
    const double t = (span > 1e-12) ? std::clamp((x - xs[lo]) / span, 0.0, 1.0) : 0.0;
    return ys[lo] + t * (ys[hi] - ys[lo]);
}

inline double circular_pupil_cutoff_lp_mm(double lambda_nm, double eff_fnum) noexcept {
    return 1.0e6 / (std::max(100.0, lambda_nm) * std::max(0.5, eff_fnum));
}

inline double airy_mtf(double q) noexcept {
    if (q <= 0.0) return 1.0;
    if (q >= 1.0) return 0.0;
    return (2.0 / std::numbers::pi_v<double>) *
           (std::acos(q) - q * std::sqrt(std::max(0.0, 1.0 - q * q)));
}

// Continuous C0/DC-quadratic + knot-anchored Airy-bounded Gaussian tail MTF evaluator (M3).
inline double eval_mtf_curve_safe(const std::vector<double>& freqs_lp_mm,
                                  const std::vector<double>& mtf_vals,
                                  double se_mtf,
                                  double query_lp_mm,
                                  double cutoff_lp_mm = 0.0) {
    if (query_lp_mm <= 0.0) return 1.0;
    if (cutoff_lp_mm > 0.0 && query_lp_mm >= cutoff_lp_mm) return 0.0;
    const size_t n = std::min(freqs_lp_mm.size(), mtf_vals.size());
    if (n == 0) {
        double v = std::exp(-2.0 * std::pow(std::numbers::pi_v<double> * 0.0025 * query_lp_mm, 2));
        if (cutoff_lp_mm > 0.0) v *= airy_mtf(query_lp_mm / cutoff_lp_mm);
        return std::clamp(v, 0.0, 1.0);
    }
    const double f1 = std::max(1e-6, freqs_lp_mm[0]);
    const double m1 = std::clamp(mtf_vals[0], 0.0, 1.0);
    if (query_lp_mm < f1) {
        const double u = query_lp_mm / f1;
        return std::clamp(1.0 - (1.0 - m1) * u * u, 0.0, 1.0);
    }
    const double fn = std::max(f1, freqs_lp_mm[n - 1]);
    const double mn = std::clamp(mtf_vals[n - 1], 0.0, 1.0);
    if (query_lp_mm <= fn) {
        if (n == 1) return m1;
        return std::clamp(lerp1d(freqs_lp_mm, mtf_vals, query_lp_mm), 0.0, 1.0);
    }
    if (mn <= 0.0) return 0.0;
    double k_tail = 0.0;
    const double se_eff = std::max(0.005, se_mtf);
    if (n >= 2 && freqs_lp_mm[n - 1] > freqs_lp_mm[n - 2] + 1e-6 &&
        mtf_vals[n - 2] > mn + 0.5 * se_eff && mtf_vals[n - 2] <= 0.9999 && mn >= 0.002) {
        const double fa = freqs_lp_mm[n - 2];
        const double ma = std::clamp(mtf_vals[n - 2], mn + 1e-6, 0.9999);
        k_tail = std::log(ma / std::max(0.002, mn)) / std::max(1e-6, fn * fn - fa * fa);
    } else {
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double fi2 = freqs_lp_mm[i] * freqs_lp_mm[i];
            const double ti = -std::log(std::clamp(mtf_vals[i], 0.002, 0.9999));
            num += fi2 * ti;
            den += fi2 * fi2;
        }
        k_tail = (den > 1e-12) ? (num / den) : (-std::log(std::clamp(mn, 0.002, 0.9999)) / (fn * fn));
    }
    k_tail = std::clamp(k_tail, 1e-8, 0.25);
    double val = mn * std::exp(-k_tail * (query_lp_mm * query_lp_mm - fn * fn));
    if (cutoff_lp_mm > fn * 1.0001) {
        const double a_fn = airy_mtf(fn / cutoff_lp_mm);
        if (a_fn > 1e-9) val *= airy_mtf(query_lp_mm / cutoff_lp_mm) / a_fn;
        else val = 0.0;
    }
    return std::clamp(val, 0.0, 1.0);
}

inline void fft1d_pow2(std::complex<double>* a, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * std::numbers::pi_v<double> / double(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const double inv = 1.0 / double(n);
        for (int i = 0; i < n; ++i) a[i] *= inv;
    }
}

inline void fft2d_128(std::vector<std::complex<double>>& grid, int N, bool inverse) {
    std::vector<std::complex<double>> col(static_cast<size_t>(N));
    for (int r = 0; r < N; ++r) fft1d_pow2(grid.data() + size_t(r) * N, N, inverse);
    for (int c = 0; c < N; ++c) {
        for (int r = 0; r < N; ++r) col[size_t(r)] = grid[size_t(r) * N + c];
        fft1d_pow2(col.data(), N, inverse);
        for (int r = 0; r < N; ++r) grid[size_t(r) * N + c] = col[size_t(r)];
    }
}

inline double zernike_radial(int n, int m_abs, double rho) {
    if (n == 0 && m_abs == 0) return 1.0;
    if (n == 1 && m_abs == 1) return rho;
    const double r2 = rho * rho;
    if (n == 2) return (m_abs == 0) ? (2.0 * r2 - 1.0) : r2;
    if (n == 3) return (m_abs == 1) ? (3.0 * r2 * rho - 2.0 * rho) : (r2 * rho);
    if (n == 4) {
        if (m_abs == 0) return 6.0 * r2 * r2 - 6.0 * r2 + 1.0;
        if (m_abs == 2) return 4.0 * r2 * r2 - 3.0 * r2;
        if (m_abs == 4) return r2 * r2;
    }
    if (n == 6 && m_abs == 0) return 20.0 * r2 * r2 * r2 - 30.0 * r2 * r2 + 12.0 * r2 - 1.0;
    double sum = 0.0;
    const int kmax = (n - m_abs) / 2;
    auto fact = [](int v) {
        double f = 1.0;
        for (int i = 2; i <= v; ++i) f *= double(i);
        return f;
    };
    for (int k = 0; k <= kmax; ++k) {
        const double num = ((k & 1) ? -1.0 : 1.0) * fact(n - k);
        const double den = fact(k) * fact((n + m_abs) / 2 - k) * fact((n - m_abs) / 2 - k);
        sum += (num / den) * std::pow(rho, n - 2 * k);
    }
    return sum;
}

inline std::string norm_token(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c)) out.push_back(char(std::tolower(c)));
    }
    return out;
}

} // namespace detail

inline bool load_nlens_json(std::string_view json_text, Profile& out, std::string* err = nullptr) {
    out = Profile{};
    detail::JNode root;
    detail::JParser parser(json_text);
    if (!parser.parse_root(root) || root.type != detail::JNode::OBJ) {
        if (err) *err = "invalid JSON syntax in .nlens profile";
        return false;
    }
    const std::string schema = root.str_or("schema", "");
    const int ver_num = int(std::lround(root.num_or("version", 0.0)));
    if (schema != "nijin.nlens.v1" && ver_num != 1) {
        if (err) *err = "unsupported .nlens schema (expected schema=nijin.nlens.v1 or version=1)";
        return false;
    }
    const std::string tier_s = root.str_or("tier", "datasheet_mtf");
    if (tier_s == "datasheet_mtf" || tier_s == "calibrated_field") out.tier = Tier::DatasheetMtf;
    else if (tier_s == "lab_mtf_asymmetric") out.tier = Tier::LabMtfAsymmetric;
    else if (tier_s == "raytrace_wavefront") out.tier = Tier::RaytraceWavefront;
    else {
        if (err) *err = "unknown .nlens tier: " + tier_s;
        return false;
    }
    out.authority = root.str_or("authority", "datasheet");
    out.lens_name = root.str_or("profile_id", "Optical Profile");

    if (const auto* lens = root.find("lens"); lens && lens->type == detail::JNode::OBJ) {
        out.lens_name = lens->str_or("name", out.lens_name);
        out.mount_name = lens->str_or("mount", "");
        out.image_circle_mm = std::clamp(lens->num_or("image_circle_mm", 43.2666), 5.0, 150.0);
        const double diag_half = 0.5 * out.image_circle_mm;
        out.sensor_half_w_mm = diag_half * (36.0 / 43.266615305567875);
        out.sensor_half_h_mm = diag_half * (24.0 / 43.266615305567875);
        auto fr = lens->num_vec("focal_range_mm");
        if (fr.size() >= 2) { out.fmin_mm = std::min(fr[0], fr[1]); out.fmax_mm = std::max(fr[0], fr[1]); }
        auto ar = lens->num_vec("aperture_range_fnum");
        if (ar.size() >= 2) { out.aopen_fnum = std::min(ar[0], ar[1]); out.amax_fnum = std::max(ar[0], ar[1]); }
        if (const auto* al = lens->find("match_aliases"); al && al->type == detail::JNode::ARR) {
            for (const auto& e : al->arr) {
                if (e.type == detail::JNode::STR && !e.str.empty()) out.match_aliases.push_back(e.str);
            }
        }
    }
    if (out.match_aliases.empty() && !out.lens_name.empty() && root.find("lens") != nullptr) {
        out.match_aliases.push_back(out.lens_name);
    }

    if (const auto* sc = root.find("sensor_calibration"); sc && sc->type == detail::JNode::OBJ) {
        out.target_sensor = sc->str_or("target_sensor", "");
        out.target_pitch_um = std::clamp(sc->num_or("pixel_pitch_um", 4.35), 0.7, 25.0);
        out.coverglass_mm = std::clamp(sc->num_or("coverglass_mm", 1.20), 0.0, 6.0);
        out.design_coverglass_mm = std::clamp(sc->num_or("design_coverglass_mm", out.coverglass_mm), 0.0, 6.0);
        out.coverglass_nd = std::clamp(sc->num_or("coverglass_index_d", 1.5168), 1.3, 2.1);
    }
    if (const auto* inc = root.find("includes"); inc && inc->type == detail::JNode::OBJ) {
        out.includes.pixel_aperture = inc->bool_or("pixel_aperture", false);
        out.includes.silicon_diffusion = inc->find("silicon_diffusion")
            ? inc->bool_or("silicon_diffusion", false)
            : inc->bool_or("diffusion", false);
        out.includes.chief_ray_lca_in_warp = inc->find("chief_ray_lca_in_warp")
            ? inc->bool_or("chief_ray_lca_in_warp", true)
            : inc->bool_or("registration", out.tier != Tier::RaytraceWavefront);
        out.includes.geometry_tilt_removed = inc->bool_or("geometry_tilt_removed", true);
    }

    auto parse_zernike_term = [&](const detail::JNode& t, ZernikeTerm& zt) -> bool {
        if (t.type != detail::JNode::OBJ) return false;
        zt.n = int(std::lround(t.num_or("n", -1.0)));
        const int raw_m = int(std::lround(t.num_or("m", 0.0)));
        const std::string br = t.str_or("branch", "");
        zt.is_sin = (raw_m < 0) || t.bool_or("sin", false) || (br == "sin");
        zt.m = std::abs(raw_m);
        zt.coeff_um = t.find("opd_um") ? t.num_or("opd_um", 0.0) : t.num_or("coeff_um", 0.0);
        const int n_min = out.includes.geometry_tilt_removed ? 2 : 0;
        if (zt.n < n_min || zt.n > 8 || zt.m > zt.n || ((zt.n - zt.m) % 2 != 0)) return false;
        if (zt.m == 0 && zt.is_sin) return false;
        return true;
    };

    auto parse_state_obj = [&](const detail::JNode& st) -> bool {
        if (st.type != detail::JNode::OBJ) return false;
        StateSlice s;
        s.focal_mm = std::clamp(st.num_or("focal_mm", 50.0), 1.0, 3000.0);
        s.focus_m = std::clamp(st.num_or("focus_m", 1000.0), 0.05, 1e6);
        s.focus_diopter = (s.focus_m >= 500.0) ? 0.0 : (1.0 / s.focus_m);
        s.aperture_fnum = std::clamp(st.num_or("aperture_fnum", st.num_or("f_number", 2.8)), 0.7, 64.0);
        const double raw_ez = st.num_or("exit_pupil_z_mm", 85.0);
        s.signed_exit_z_mm = -std::abs(raw_ez);
        s.exit_pupil_z_mm = std::clamp(std::abs(raw_ez), 5.0, 2000.0);
        s.radii_mm = st.num_vec("radii_mm");
        if (s.radii_mm.empty()) s.radii_mm = {0.0, 5.4, 10.8, 15.1, 18.4, 21.63};
        s.vignetting_ev = st.num_vec("vignetting_ev");
        s.distortion_pct = st.num_vec("distortion_pct");
        s.lca_red_ppm = st.num_vec("lca_red_ppm");
        s.lca_blue_ppm = st.num_vec("lca_blue_ppm");
        s.mtf_lp_mm = st.num_vec("mtf_lp_mm");
        s.mtf_se = std::clamp(st.num_or("mtf_se", 0.02), 0.001, 0.25);
        s.coma_c31_waves = st.num_vec("coma_c31_waves");

        auto read_2d = [&](std::string_view key, std::vector<std::vector<double>>& dst) {
            if (const auto* m = st.find(key); m && m->type == detail::JNode::ARR) {
                for (const auto& row : m->arr) {
                    if (row.type == detail::JNode::ARR) {
                        std::vector<double> r;
                        for (const auto& v : row.arr) {
                            r.push_back(v.type == detail::JNode::NUM && std::isfinite(v.num) ? v.num : 0.5);
                        }
                        dst.push_back(std::move(r));
                    }
                }
            }
        };
        read_2d("mtf_s", s.mtf_s);
        read_2d("mtf_t", s.mtf_t);

        if (const auto* sk = st.find("spherical_skirt"); sk && sk->type == detail::JNode::OBJ) {
            s.skirt.weight = std::clamp(sk->num_or("weight", 0.0), 0.0, 0.35);
            s.skirt.sigma_um = std::clamp(sk->num_or("sigma_um", 0.0), 0.0, 250.0);
        }
        if (const auto* stp = st.find("mechanical_stops"); stp && stp->type == detail::JNode::ARR) {
            for (const auto& m : stp->arr) {
                if (m.type != detail::JNode::OBJ) continue;
                MechanicalStop ms;
                ms.z_rel_exit_mm = m.num_or("z_rel_exit_mm", 0.0);
                ms.radius_mm = std::max(0.5, m.num_or("radius_mm", 25.0));
                ms.decenter_x_mm = m.num_or("decenter_x_mm", 0.0);
                ms.decenter_y_mm = m.num_or("decenter_y_mm", 0.0);
                s.stops.push_back(ms);
            }
        }
        if (const auto* wf = st.find("wavefront_zernike_um"); wf && wf->type == detail::JNode::ARR) {
            for (const auto& r_arr : wf->arr) {
                std::vector<ZernikeTerm> terms;
                if (r_arr.type == detail::JNode::ARR) {
                    for (const auto& t : r_arr.arr) {
                        ZernikeTerm zt;
                        if (parse_zernike_term(t, zt)) terms.push_back(zt);
                    }
                }
                s.wavefront_by_r.push_back(std::move(terms));
            }
        }
        out.states.push_back(std::move(s));
        return true;
    };

    if (const auto* sts = root.find("states"); sts && sts->type == detail::JNode::ARR) {
        for (const auto& st : sts->arr) parse_state_obj(st);
    }
    // Full canonical `nodes[]` v1 syntax support (nlens-v1.schema.json + example-wave.nlens):
    // parses `field_xy`, `focus_diopter`, `lambda_nm`, `spectral_rgb`, `mtf`, `asym`, and `wave`!
    if (out.states.empty()) {
        if (const auto* nds = root.find("nodes"); nds && nds->type == detail::JNode::ARR) {
            struct RawExpandedNode {
                StateSlice proto;
                double x_mm = 0.0;
                double y_mm = 0.0;
                double r_mm = 0.0;
                std::vector<double> lp_mm;
                std::vector<double> mtf_s;
                std::vector<double> mtf_t;
                double mtf_se = 0.02;
                std::vector<ZernikeTerm> wave_terms;
            };
            std::vector<RawExpandedNode> parsed_nodes;
            for (const auto& nd : nds->arr) {
                if (nd.type != detail::JNode::OBJ) continue;
                RawExpandedNode rn;
                StateSlice& s = rn.proto;
                s.focal_mm = std::clamp(nd.num_or("focal_mm", 50.0), 1.0, 3000.0);
                const double diopt = nd.find("focus_diopter")
                    ? std::max(0.0, nd.num_or("focus_diopter", 0.0))
                    : std::max(0.0, nd.num_or("focus_diopters", 0.0));
                s.focus_diopter = diopt;
                s.focus_m = (diopt > 1e-4) ? (1.0 / diopt) : 1000.0;
                s.aperture_fnum = std::clamp(nd.num_or("f_number", 2.8), 0.7, 64.0);
                s.lambda_nm = std::clamp(nd.num_or("lambda_nm", 550.0), 350.0, 1100.0);
                auto srgb = nd.num_vec("spectral_rgb");
                if (srgb.size() >= 3) s.spectral_rgb = {srgb[0], srgb[1], srgb[2]};
                out.authority = nd.str_or("authority", out.authority);

                double hx = nd.num_or("field_x", 0.0), hy = nd.num_or("field_y", 0.0);
                auto fxy = nd.num_vec("field_xy");
                if (fxy.size() >= 2) { hx = fxy[0]; hy = fxy[1]; }
                rn.x_mm = hx * out.sensor_half_w_mm;
                rn.y_mm = hy * out.sensor_half_h_mm;
                rn.r_mm = std::hypot(rn.x_mm, rn.y_mm);

                if (const auto* mtf = nd.find("mtf"); mtf && mtf->type == detail::JNode::ARR) {
                    struct Knot { double lp; double ms; double mt; double se; };
                    std::vector<Knot> knots;
                    for (const auto& pt : mtf->arr) {
                        if (pt.type != detail::JNode::OBJ) continue;
                        knots.push_back({
                            pt.num_or("lp_mm", 10.0),
                            pt.num_or("s", 0.8),
                            pt.num_or("t", 0.8),
                            std::clamp(pt.num_or("se", 0.02), 0.001, 0.25),
                        });
                    }
                    std::sort(knots.begin(), knots.end(), [](const Knot& a, const Knot& b) { return a.lp < b.lp; });
                    std::vector<double> knot_wsum;
                    double max_knot_se = 0.001;
                    for (const auto& k : knots) {
                        const double w = 1.0 / std::max(1e-8, k.se * k.se);
                        max_knot_se = std::max(max_knot_se, k.se);
                        if (!rn.lp_mm.empty() && std::abs(rn.lp_mm.back() - k.lp) <= 1e-6) {
                            const double w0 = knot_wsum.back();
                            const double wn = w0 + w;
                            rn.mtf_s.back() = (w0 * rn.mtf_s.back() + w * k.ms) / wn;
                            rn.mtf_t.back() = (w0 * rn.mtf_t.back() + w * k.mt) / wn;
                            knot_wsum.back() = wn;
                        } else {
                            rn.lp_mm.push_back(k.lp);
                            rn.mtf_s.push_back(k.ms);
                            rn.mtf_t.push_back(k.mt);
                            knot_wsum.push_back(w);
                        }
                    }
                    rn.mtf_se = max_knot_se;
                    s.mtf_se = rn.mtf_se;
                }
                if (const auto* as = nd.find("asym"); as && as->type == detail::JNode::OBJ) {
                    s.loca_um = as->num_or("loca_um", 0.0);
                    s.phase_source = as->str_or("phase_source", "signed_psf");
                    auto cp = as->num_vec("cubic_phase_rad_mm3");
                    if (cp.size() >= 4) {
                        s.cubic_phase_rad_mm3 = {cp[0], cp[1], cp[2], cp[3]};
                        s.has_cubic_phase_mm3 = (s.phase_source == "signed_psf");
                    }
                }
                if (const auto* wv = nd.find("wave"); wv && wv->type == detail::JNode::OBJ) {
                    s.signed_exit_z_mm = wv->num_or("exit_z_mm", -85.0);
                    s.exit_pupil_z_mm = std::clamp(std::abs(s.signed_exit_z_mm), 5.0, 2000.0);
                    s.exit_diameter_mm = std::max(0.0, wv->num_or("exit_diameter_mm", 0.0));
                    s.na_air = std::clamp(wv->num_or("na_air", 0.0), 0.0, 0.98);
                    auto cq = wv->num_vec("chief_q_xy");
                    if (cq.size() >= 2) {
                        s.chief_q_xy = {cq[0], cq[1]};
                        s.has_chief_q = true;
                    }
                    s.cra_power_intensity = std::max(0.0, wv->num_or("cra_power_intensity", 0.0));
                    s.silicon_sigma_um = std::max(0.0, wv->num_or("silicon_sigma_um", 0.0));
                    if (const auto* tr = wv->find("terms"); tr && tr->type == detail::JNode::ARR) {
                        for (const auto& t : tr->arr) {
                            ZernikeTerm zt;
                            if (parse_zernike_term(t, zt)) rn.wave_terms.push_back(zt);
                        }
                    }
                    if (const auto* bf = wv->find("baffles"); bf && bf->type == detail::JNode::ARR) {
                        for (const auto& b : bf->arr) {
                            if (b.type != detail::JNode::OBJ) continue;
                            MechanicalStop ms;
                            ms.z_rel_exit_mm = b.num_or("z_mm", s.signed_exit_z_mm) - s.signed_exit_z_mm;
                            ms.radius_mm = std::max(0.5, b.num_or("radius_mm", 25.0));
                            auto cxy = b.num_vec("center_xy_mm");
                            if (cxy.size() >= 2) { ms.decenter_x_mm = cxy[0]; ms.decenter_y_mm = cxy[1]; }
                            s.stops.push_back(ms);
                        }
                    }
                    if (const auto* stk = wv->find("stack"); stk && stk->type == detail::JNode::ARR) {
                        for (const auto& ly : stk->arr) {
                            if (ly.type != detail::JNode::OBJ) continue;
                            DielectricLayer dl;
                            dl.delta_d_mm = ly.num_or("delta_d_mm", 0.0);
                            dl.n_lambda = std::clamp(ly.num_or("n_lambda", 1.5), 1.0001, 3.0);
                            if (std::abs(dl.delta_d_mm) > 1e-9) s.stack.push_back(dl);
                        }
                    }
                }
                parsed_nodes.push_back(std::move(rn));
            }

            // Group strictly by physical shot state (focal_mm, focus_diopter, aperture_fnum, lambda_nm),
            // independent of node listing order or per-node MTF spatial-frequency grids!
            std::vector<std::vector<RawExpandedNode>> groups;
            for (auto& rn : parsed_nodes) {
                bool placed = false;
                for (auto& g : groups) {
                    const auto& ref = g.front().proto;
                    if (std::abs(ref.focal_mm - rn.proto.focal_mm) < 1e-3 &&
                        std::abs(ref.focus_diopter - rn.proto.focus_diopter) < 1e-4 &&
                        std::abs(ref.aperture_fnum - rn.proto.aperture_fnum) < 1e-3 &&
                        std::abs(ref.lambda_nm - rn.proto.lambda_nm) < 0.5) {
                        g.push_back(std::move(rn));
                        placed = true;
                        break;
                    }
                }
                if (!placed) groups.push_back({std::move(rn)});
            }

            for (auto& g : groups) {
                // Canonicalize node ordering by (x_mm, y_mm) so any permutation of `nodes[]` builds an identical StateSlice
                std::sort(g.begin(), g.end(), [](const RawExpandedNode& a, const RawExpandedNode& b) {
                    if (std::abs(a.x_mm - b.x_mm) > 1e-9) return a.x_mm < b.x_mm;
                    if (std::abs(a.y_mm - b.y_mm) > 1e-9) return a.y_mm < b.y_mm;
                    return a.r_mm < b.r_mm;
                });

                std::vector<double> union_lp;
                double max_se = 0.001;
                for (const auto& nd : g) {
                    max_se = std::max(max_se, nd.mtf_se);
                    for (double f : nd.lp_mm) union_lp.push_back(f);
                }
                std::sort(union_lp.begin(), union_lp.end());
                std::vector<double> dedup_lp;
                for (double f : union_lp) {
                    if (dedup_lp.empty() || std::abs(f - dedup_lp.back()) > 1e-6) dedup_lp.push_back(f);
                }

                StateSlice st = g.front().proto;
                st.mtf_lp_mm = dedup_lp;
                st.mtf_se = max_se;
                st.radii_mm.clear();
                st.field_xy_mm.clear();
                st.mtf_s.clear();
                st.mtf_t.clear();
                st.wavefront_by_r.clear();

                std::vector<double> dup_wsum;
                const double fc_state = detail::circular_pupil_cutoff_lp_mm(st.lambda_nm, st.aperture_fnum);
                for (const auto& nd : g) {
                    const double w_nd = 1.0 / std::max(1e-8, nd.mtf_se * nd.mtf_se);
                    std::vector<double> cur_s, cur_t;
                    if (!dedup_lp.empty() && !nd.lp_mm.empty() && !nd.mtf_s.empty()) {
                        bool exact_grid = (nd.lp_mm.size() == dedup_lp.size());
                        if (exact_grid) {
                            for (size_t fi = 0; fi < dedup_lp.size(); ++fi) {
                                if (std::abs(nd.lp_mm[fi] - dedup_lp[fi]) > 1e-6) { exact_grid = false; break; }
                            }
                        }
                        if (exact_grid) {
                            cur_s = nd.mtf_s;
                            cur_t = nd.mtf_t;
                        } else {
                            cur_s.resize(dedup_lp.size());
                            cur_t.resize(dedup_lp.size());
                            for (size_t fi = 0; fi < dedup_lp.size(); ++fi) {
                                const double fq = dedup_lp[fi];
                                cur_s[fi] = detail::eval_mtf_curve_safe(nd.lp_mm, nd.mtf_s, nd.mtf_se, fq, fc_state);
                                cur_t[fi] = detail::eval_mtf_curve_safe(nd.lp_mm, nd.mtf_t, nd.mtf_se, fq, fc_state);
                            }
                        }
                    }
                    if (!st.field_xy_mm.empty() &&
                        std::hypot(st.field_xy_mm.back()[0] - nd.x_mm, st.field_xy_mm.back()[1] - nd.y_mm) <= 1e-6) {
                        const double w0 = dup_wsum.back();
                        const double wn = w0 + w_nd;
                        if (!cur_s.empty() && !st.mtf_s.empty() && st.mtf_s.back().size() == cur_s.size()) {
                            for (size_t fi = 0; fi < cur_s.size(); ++fi) {
                                st.mtf_s.back()[fi] = (w0 * st.mtf_s.back()[fi] + w_nd * cur_s[fi]) / wn;
                                st.mtf_t.back()[fi] = (w0 * st.mtf_t.back()[fi] + w_nd * cur_t[fi]) / wn;
                            }
                        }
                        if ((!nd.wave_terms.empty() || out.tier == Tier::RaytraceWavefront) && !st.wavefront_by_r.empty()) {
                            auto& dst_w = st.wavefront_by_r.back();
                            for (auto& ex : dst_w) ex.coeff_um *= (w0 / wn);
                            for (const auto& z : nd.wave_terms) {
                                bool found = false;
                                for (auto& ex : dst_w) {
                                    if (ex.n == z.n && ex.m == z.m && ex.is_sin == z.is_sin) {
                                        ex.coeff_um += (w_nd / wn) * z.coeff_um;
                                        found = true;
                                        break;
                                    }
                                }
                                if (!found) {
                                    ZernikeTerm nz = z;
                                    nz.coeff_um *= (w_nd / wn);
                                    dst_w.push_back(nz);
                                }
                            }
                        }
                        dup_wsum.back() = wn;
                    } else {
                        st.radii_mm.push_back(nd.r_mm);
                        st.field_xy_mm.push_back({nd.x_mm, nd.y_mm});
                        if (!cur_s.empty()) {
                            st.mtf_s.push_back(std::move(cur_s));
                            st.mtf_t.push_back(std::move(cur_t));
                        }
                        if (!nd.wave_terms.empty() || out.tier == Tier::RaytraceWavefront) {
                            st.wavefront_by_r.push_back(nd.wave_terms);
                        }
                        dup_wsum.push_back(w_nd);
                    }
                }
                out.states.push_back(std::move(st));
            }
            std::sort(out.states.begin(), out.states.end(), [](const StateSlice& a, const StateSlice& b) {
                if (std::abs(a.focal_mm - b.focal_mm) > 1e-6) return a.focal_mm < b.focal_mm;
                if (std::abs(a.aperture_fnum - b.aperture_fnum) > 1e-6) return a.aperture_fnum < b.aperture_fnum;
                if (std::abs(a.focus_diopter - b.focus_diopter) > 1e-6) return a.focus_diopter < b.focus_diopter;
                return a.lambda_nm < b.lambda_nm;
            });
        }
    }

    if (out.states.empty()) {
        if (err) *err = ".nlens profile contains no valid optical states";
        return false;
    }
    for (const auto& s : out.states) {
        if (out.tier == Tier::RaytraceWavefront && s.wavefront_by_r.empty()) {
            if (err) *err = "raytrace_wavefront .nlens state lacks wave/wavefront_zernike_um payload";
            return false;
        }
        if ((out.tier == Tier::DatasheetMtf || out.tier == Tier::LabMtfAsymmetric) &&
            (s.mtf_lp_mm.empty() || s.mtf_s.empty())) {
            if (err) *err = "MTF .nlens state lacks mtf curve knots";
            return false;
        }
    }
    // Calibration hull bounds are always strictly determined by actual calibrated states
    out.fmin_mm = out.states.front().focal_mm;
    out.fmax_mm = out.states.front().focal_mm;
    out.aopen_fnum = out.states.front().aperture_fnum;
    out.amax_fnum = out.states.front().aperture_fnum;
    out.dmin_diopter = out.states.front().focus_diopter;
    out.dmax_diopter = out.states.front().focus_diopter;
    out.rmax_cal_mm = 0.0;
    for (const auto& s : out.states) {
        out.fmin_mm = std::min(out.fmin_mm, s.focal_mm);
        out.fmax_mm = std::max(out.fmax_mm, s.focal_mm);
        out.aopen_fnum = std::min(out.aopen_fnum, s.aperture_fnum);
        out.amax_fnum = std::max(out.amax_fnum, s.aperture_fnum);
        out.dmin_diopter = std::min(out.dmin_diopter, s.focus_diopter);
        out.dmax_diopter = std::max(out.dmax_diopter, s.focus_diopter);
        for (double rv : s.radii_mm) out.rmax_cal_mm = std::max(out.rmax_cal_mm, rv);
    }
    out.loaded = true;
    return true;
}

inline bool load_nlens_file(std::string_view utf8_path, Profile& out, std::string* err = nullptr) {
    std::ifstream f(heraw_fs::from_utf8(utf8_path), std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open .nlens file: " + std::string(utf8_path);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (!load_nlens_json(ss.str(), out, err)) return false;
    out.source_path = std::string(utf8_path);
    return true;
}

inline bool is_generic_sensor_label(std::string_view ts_norm) noexcept {
    return ts_norm.empty() || ts_norm.starts_with("fullframe") || ts_norm == "ff" ||
           ts_norm.starts_with("35mm") || ts_norm.starts_with("apsc") || ts_norm == "mft" ||
           ts_norm.starts_with("microfourthirds") || ts_norm.starts_with("mediumformat") ||
           ts_norm.starts_with("generic") || ts_norm == "any";
}

inline bool body_matches_profile(std::string_view exif_body, const Profile& cand) {
    const std::string b_norm = detail::norm_token(exif_body);
    const std::string ts_norm = detail::norm_token(cand.target_sensor);
    if (!is_generic_sensor_label(ts_norm)) {
        if (b_norm.empty()) return false;
        if (b_norm.find(ts_norm) == std::string::npos && ts_norm.find(b_norm) == std::string::npos) {
            return false;
        }
    }
    const std::string m_norm = detail::norm_token(cand.mount_name);
    if (!m_norm.empty() && !b_norm.empty()) {
        auto has = [&](std::string_view hay, std::string_view ndl) { return hay.find(ndl) != std::string_view::npos; };
        if (has(m_norm, "nikon") && (has(b_norm, "sony") || has(b_norm, "canon") || has(b_norm, "fuji"))) return false;
        if (has(m_norm, "sonye") && (has(b_norm, "nikon") || has(b_norm, "canon") || has(b_norm, "fuji"))) return false;
        if (has(m_norm, "canon") && (has(b_norm, "sony") || has(b_norm, "nikon") || has(b_norm, "fuji"))) return false;
        if (has(m_norm, "fuji") && (has(b_norm, "sony") || has(b_norm, "nikon") || has(b_norm, "canon"))) return false;
    }
    return true;
}

namespace detail {

// Pairwise Frank-Wolfe minimum-norm projection onto probability simplex Delta^{M-1} for R^3 vectors d[i]
inline double project_simplex_r3(
    const std::vector<std::array<double, 3>>& d,
    std::vector<double>& w_out) {
    const size_t m = d.size();
    w_out.assign(m, 0.0);
    if (m == 0) return 1e30;
    size_t best_i = 0;
    double best_n2 = 1e60;
    for (size_t i = 0; i < m; ++i) {
        const double n2 = d[i][0] * d[i][0] + d[i][1] * d[i][1] + d[i][2] * d[i][2];
        if (n2 < best_n2) { best_n2 = n2; best_i = i; }
    }
    w_out[best_i] = 1.0;
    if (m == 1 || best_n2 <= 1e-24) return std::sqrt(best_n2);

    std::array<double, 3> r = d[best_i];
    for (int iter = 0; iter < 128; ++iter) {
        size_t i_fw = 0, i_aw = best_i;
        double min_dot = 1e60, max_dot = -1e60;
        for (size_t i = 0; i < m; ++i) {
            const double dt = r[0] * d[i][0] + r[1] * d[i][1] + r[2] * d[i][2];
            if (dt < min_dot) { min_dot = dt; i_fw = i; }
            if (w_out[i] > 1e-15 && dt > max_dot) { max_dot = dt; i_aw = i; }
        }
        const double r2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
        if (r2 <= 1e-24 || (r2 - min_dot) <= 1e-15) break;
        const std::array<double, 3> v = {d[i_fw][0] - d[i_aw][0], d[i_fw][1] - d[i_aw][1], d[i_fw][2] - d[i_aw][2]};
        const double vv = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (vv <= 1e-24) break;
        const double rv = r[0] * v[0] + r[1] * v[1] + r[2] * v[2];
        if (rv >= -1e-16) break;
        const double gamma = std::clamp(-rv / vv, 0.0, w_out[i_aw]);
        if (gamma <= 1e-16) break;
        w_out[i_fw] += gamma;
        w_out[i_aw] = std::max(0.0, w_out[i_aw] - gamma);
        r = {0.0, 0.0, 0.0};
        for (size_t i = 0; i < m; ++i) {
            if (w_out[i] > 0.0) {
                r[0] += w_out[i] * d[i][0];
                r[1] += w_out[i] * d[i][1];
                r[2] += w_out[i] * d[i][2];
            }
        }
    }
    return std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
}

inline bool project_state_convex_hull(
    const Profile& prof,
    double focal_mm,
    double focus_m,
    double aperture_fnum,
    std::vector<double>& state_weights,
    std::string* out_reason = nullptr) {
    if (!prof.loaded || prof.states.empty()) {
        if (out_reason) *out_reason = "profile_not_loaded";
        return false;
    }
    constexpr double kTolLf = 0.025; // ~2.5% focal rounding tolerance
    constexpr double kTolLa = 0.040; // ~4.0% aperture rounding tolerance (< 1/8 EV)
    constexpr double kTolD  = 0.050; // 0.05 diopter tolerance

    const bool has_focal = (focal_mm > 0.0);
    const bool has_ap    = (aperture_fnum > 0.0);
    const bool has_focus = (focus_m > 0.05);

    const double lf_q = std::log(std::max(1.0, has_focal ? focal_mm : prof.fmin_mm));
    const double la_q = std::log(std::max(0.7, has_ap ? aperture_fnum : prof.aopen_fnum));
    const double d_q  = has_focus ? ((focus_m >= 500.0) ? 0.0 : (1.0 / focus_m)) : 0.0;

    const double lf_min = std::log(std::max(1.0, prof.fmin_mm));
    const double lf_max = std::log(std::max(1.0, prof.fmax_mm));
    if (has_focal && (lf_q < lf_min - kTolLf || lf_q > lf_max + kTolLf)) {
        if (out_reason) *out_reason = "outside_focal_hull";
        return false;
    }
    const double la_min = std::log(std::max(0.7, prof.aopen_fnum));
    const double la_max = std::log(std::max(0.7, prof.amax_fnum));
    if (has_ap && (la_q < la_min - kTolLa || la_q > la_max + kTolLa)) {
        if (out_reason) *out_reason = "outside_aperture_hull";
        return false;
    }
    if (has_focus && (d_q < prof.dmin_diopter - kTolD || d_q > prof.dmax_diopter + kTolD)) {
        if (out_reason) *out_reason = "outside_focus_hull";
        return false;
    }

    std::vector<std::array<double, 3>> diff(prof.states.size());
    for (size_t i = 0; i < prof.states.size(); ++i) {
        const auto& s = prof.states[i];
        diff[i][0] = has_focal ? ((std::log(std::max(1.0, s.focal_mm)) - lf_q) / kTolLf) : 0.0;
        diff[i][1] = has_ap    ? ((std::log(std::max(0.7, s.aperture_fnum)) - la_q) / kTolLa) : 0.0;
        diff[i][2] = has_focus ? ((s.focus_diopter - d_q) / kTolD) : 0.0;
    }
    const double res_norm = project_simplex_r3(diff, state_weights);
    if (res_norm > 1.0) {
        if (out_reason) *out_reason = "outside_state_hull";
        return false;
    }
    return true;
}

inline bool project_state_field_hull(
    const StateSlice& st,
    double x_mm,
    double y_mm,
    std::vector<double>& field_weights) {
    if (!st.field_xy_mm.empty()) {
        std::vector<std::array<double, 3>> d(st.field_xy_mm.size());
        double max_r = 0.0;
        for (size_t k = 0; k < st.field_xy_mm.size(); ++k) {
            d[k] = {st.field_xy_mm[k][0] - x_mm, st.field_xy_mm[k][1] - y_mm, 0.0};
            max_r = std::max(max_r, std::hypot(st.field_xy_mm[k][0], st.field_xy_mm[k][1]));
        }
        const double dist_mm = project_simplex_r3(d, field_weights);
        const double tol_xy_mm = std::max(0.05, 0.01 * max_r);
        return dist_mm <= tol_xy_mm;
    }
    field_weights.clear();
    const double r_mm = std::hypot(x_mm, y_mm);
    double r_max = 0.0;
    for (double rv : st.radii_mm) r_max = std::max(r_max, rv);
    const double tol_r_mm = std::max(0.05, 0.02 * r_max);
    return r_mm <= r_max + tol_r_mm;
}

inline double cross2d(const std::array<double, 2>& o,
                      const std::array<double, 2>& a,
                      const std::array<double, 2>& b) noexcept {
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
}

inline std::vector<std::array<double, 2>> convex_hull_2d(std::vector<std::array<double, 2>> pts) {
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
        if (std::abs(a[0] - b[0]) > 1e-9) return a[0] < b[0];
        return a[1] < b[1];
    });
    std::vector<std::array<double, 2>> u;
    u.reserve(pts.size());
    for (const auto& p : pts) {
        if (u.empty() || std::hypot(p[0] - u.back()[0], p[1] - u.back()[1]) > 1e-8) {
            u.push_back(p);
        }
    }
    if (u.size() <= 2) return u;

    std::vector<std::array<double, 2>> lo, hi;
    for (const auto& p : u) {
        while (lo.size() >= 2 && cross2d(lo[lo.size() - 2], lo.back(), p) <= 1e-11) lo.pop_back();
        lo.push_back(p);
    }
    for (auto it = u.rbegin(); it != u.rend(); ++it) {
        while (hi.size() >= 2 && cross2d(hi[hi.size() - 2], hi.back(), *it) <= 1e-11) hi.pop_back();
        hi.push_back(*it);
    }
    lo.pop_back();
    hi.pop_back();
    lo.insert(lo.end(), hi.begin(), hi.end());
    if (lo.size() < 3) return {u.front(), u.back()};
    double area2 = 0.0;
    for (size_t i = 0; i < lo.size(); ++i) {
        const auto& p0 = lo[i];
        const auto& p1 = lo[(i + 1) % lo.size()];
        area2 += p0[0] * p1[1] - p1[0] * p0[1];
    }
    if (std::abs(area2) <= 1e-10) return {u.front(), u.back()};
    return lo;
}

inline double point_to_seg_dist_2d(double px, double py, double ax, double ay, double bx, double by) noexcept {
    const double vx = bx - ax, vy = by - ay;
    const double vv = vx * vx + vy * vy;
    if (vv <= 1e-20) return std::hypot(px - ax, py - ay);
    const double t = std::clamp(((px - ax) * vx + (py - ay) * vy) / vv, 0.0, 1.0);
    return std::hypot(px - (ax + t * vx), py - (ay + t * vy));
}

inline double point_to_convex_poly_dist_2d(const std::vector<std::array<double, 2>>& poly, double px, double py) noexcept {
    if (poly.empty()) return 1e30;
    if (poly.size() == 1) return std::hypot(px - poly[0][0], py - poly[0][1]);
    if (poly.size() == 2) return point_to_seg_dist_2d(px, py, poly[0][0], poly[0][1], poly[1][0], poly[1][1]);
    bool inside = true;
    const std::array<double, 2> q{px, py};
    for (size_t i = 0; i < poly.size(); ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % poly.size()];
        const double elen = std::max(1e-12, std::hypot(b[0] - a[0], b[1] - a[1]));
        if (cross2d(a, b, q) / elen < -1e-10) {
            inside = false;
            break;
        }
    }
    if (inside) return 0.0;
    double best = 1e30;
    for (size_t i = 0; i < poly.size(); ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % poly.size()];
        best = std::min(best, point_to_seg_dist_2d(px, py, a[0], a[1], b[0], b[1]));
    }
    return best;
}

inline bool segments_touch_2d(const std::array<double, 2>& a0, const std::array<double, 2>& a1,
                              const std::array<double, 2>& b0, const std::array<double, 2>& b1,
                              std::array<double, 2>* out_pt = nullptr) noexcept {
    const double r_x = a1[0] - a0[0], r_y = a1[1] - a0[1];
    const double s_x = b1[0] - b0[0], s_y = b1[1] - b0[1];
    const double rxs = r_x * s_y - r_y * s_x;
    const double qpx = b0[0] - a0[0], qpy = b0[1] - a0[1];
    if (std::abs(rxs) > 1e-14) {
        const double t = (qpx * s_y - qpy * s_x) / rxs;
        const double u = (qpx * r_y - qpy * r_x) / rxs;
        if (t >= -1e-10 && t <= 1.0 + 1e-10 && u >= -1e-10 && u <= 1.0 + 1e-10) {
            if (out_pt) {
                const double tc = std::clamp(t, 0.0, 1.0);
                *out_pt = {a0[0] + tc * r_x, a0[1] + tc * r_y};
            }
            return true;
        }
        return false;
    }
    if (point_to_seg_dist_2d(a0[0], a0[1], b0[0], b0[1], b1[0], b1[1]) <= 1e-9) {
        if (out_pt) *out_pt = a0;
        return true;
    }
    if (point_to_seg_dist_2d(a1[0], a1[1], b0[0], b0[1], b1[0], b1[1]) <= 1e-9) {
        if (out_pt) *out_pt = a1;
        return true;
    }
    if (point_to_seg_dist_2d(b0[0], b0[1], a0[0], a0[1], a1[0], a1[1]) <= 1e-9) {
        if (out_pt) *out_pt = b0;
        return true;
    }
    if (point_to_seg_dist_2d(b1[0], b1[1], a0[0], a0[1], a1[0], a1[1]) <= 1e-9) {
        if (out_pt) *out_pt = b1;
        return true;
    }
    return false;
}

inline std::vector<std::array<double, 2>> intersect_convex_polys_2d(
    const std::vector<std::array<double, 2>>& A_in,
    const std::vector<std::array<double, 2>>& B_in) {
    if (A_in.empty() || B_in.empty()) return {};
    const auto* A = &A_in;
    const auto* B = &B_in;
    if (A->size() > B->size() && B->size() < 3) std::swap(A, B);

    if (A->size() == 1) {
        if (point_to_convex_poly_dist_2d(*B, (*A)[0][0], (*A)[0][1]) <= 1e-8) return {*A};
        return {};
    }
    if (A->size() == 2 && B->size() == 2) {
        std::vector<std::array<double, 2>> pts;
        for (const auto& p : *A) {
            if (point_to_seg_dist_2d(p[0], p[1], (*B)[0][0], (*B)[0][1], (*B)[1][0], (*B)[1][1]) <= 1e-8) pts.push_back(p);
        }
        for (const auto& p : *B) {
            if (point_to_seg_dist_2d(p[0], p[1], (*A)[0][0], (*A)[0][1], (*A)[1][0], (*A)[1][1]) <= 1e-8) pts.push_back(p);
        }
        std::array<double, 2> ip{};
        if (segments_touch_2d((*A)[0], (*A)[1], (*B)[0], (*B)[1], &ip)) pts.push_back(ip);
        return convex_hull_2d(std::move(pts));
    }
    if (A->size() == 2 && B->size() >= 3) {
        double t0 = 0.0, t1 = 1.0;
        const double dx = (*A)[1][0] - (*A)[0][0], dy = (*A)[1][1] - (*A)[0][1];
        for (size_t i = 0; i < B->size(); ++i) {
            const auto& v0 = (*B)[i];
            const auto& v1 = (*B)[(i + 1) % B->size()];
            const double ex = v1[0] - v0[0], ey = v1[1] - v0[1];
            const double elen = std::max(1e-12, std::hypot(ex, ey));
            const double nx = -ey / elen, ny = ex / elen; // inward normal of CCW edge
            const double num = -(((*A)[0][0] - v0[0]) * nx + ((*A)[0][1] - v0[1]) * ny) - 1e-9;
            const double den = dx * nx + dy * ny;
            if (std::abs(den) <= 1e-14) {
                if (num > 0.0) return {};
            } else if (den > 0.0) {
                t0 = std::max(t0, num / den);
            } else {
                t1 = std::min(t1, num / den);
            }
            if (t0 > t1 + 1e-9) return {};
        }
        t0 = std::clamp(t0, 0.0, 1.0);
        t1 = std::clamp(t1, 0.0, 1.0);
        const std::array<double, 2> p0{(*A)[0][0] + t0 * dx, (*A)[0][1] + t0 * dy};
        const std::array<double, 2> p1{(*A)[0][0] + t1 * dx, (*A)[0][1] + t1 * dy};
        if (std::hypot(p1[0] - p0[0], p1[1] - p0[1]) <= 1e-8) return {p0};
        return {p0, p1};
    }

    // Both A and B have >= 3 CCW vertices: Sutherland-Hodgman half-plane clipping
    std::vector<std::array<double, 2>> cur = *A;
    for (size_t ei = 0; ei < B->size() && !cur.empty(); ++ei) {
        const auto& v0 = (*B)[ei];
        const auto& v1 = (*B)[(ei + 1) % B->size()];
        const double ex = v1[0] - v0[0], ey = v1[1] - v0[1];
        const double elen = std::max(1e-12, std::hypot(ex, ey));
        auto sdist = [&](const std::array<double, 2>& q) noexcept {
            return (ex * (q[1] - v0[1]) - ey * (q[0] - v0[0])) / elen;
        };
        std::vector<std::array<double, 2>> nxt;
        nxt.reserve(cur.size() + 2);
        for (size_t i = 0; i < cur.size(); ++i) {
            const auto& p = cur[i];
            const auto& q = cur[(i + 1) % cur.size()];
            const double dp = sdist(p);
            const double dq = sdist(q);
            const bool in_p = (dp >= -1e-9);
            const bool in_q = (dq >= -1e-9);
            if (in_p && in_q) {
                nxt.push_back(q);
            } else if (in_p && !in_q) {
                const double t = std::clamp(dp / std::max(1e-18, dp - dq), 0.0, 1.0);
                nxt.push_back({p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])});
            } else if (!in_p && in_q) {
                const double t = std::clamp(dp / std::min(-1e-18, dp - dq), 0.0, 1.0);
                nxt.push_back({p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])});
                nxt.push_back(q);
            }
        }
        cur = std::move(nxt);
    }
    return convex_hull_2d(std::move(cur));
}

// Exact closed-form Euclidean distance test dist(conv_poly, [x0,x1]x[y0,y1]) <= tol_mm
inline bool box_intersects_convex_poly_2d(
    const std::vector<std::array<double, 2>>& poly,
    double tol_mm,
    double x0_mm, double x1_mm,
    double y0_mm, double y1_mm) noexcept {
    if (poly.empty()) return false;
    if (x0_mm > x1_mm) std::swap(x0_mm, x1_mm);
    if (y0_mm > y1_mm) std::swap(y0_mm, y1_mm);

    for (const auto& v : poly) {
        const double cx = std::clamp(v[0], x0_mm, x1_mm);
        const double cy = std::clamp(v[1], y0_mm, y1_mm);
        if (std::hypot(v[0] - cx, v[1] - cy) <= tol_mm) return true;
    }
    const std::array<std::array<double, 2>, 4> bcorners{{
        {x0_mm, y0_mm}, {x1_mm, y0_mm}, {x1_mm, y1_mm}, {x0_mm, y1_mm}
    }};
    for (const auto& bc : bcorners) {
        if (point_to_convex_poly_dist_2d(poly, bc[0], bc[1]) <= tol_mm) return true;
    }
    if (poly.size() >= 2) {
        const size_t ne = (poly.size() == 2) ? 1 : poly.size();
        for (size_t i = 0; i < ne; ++i) {
            const auto& p0 = poly[i];
            const auto& p1 = poly[(i + 1) % poly.size()];
            for (size_t j = 0; j < 4; ++j) {
                if (segments_touch_2d(p0, p1, bcorners[j], bcorners[(j + 1) % 4])) return true;
            }
        }
    }
    return false;
}

struct CommonFieldSupport {
    bool valid = false;
    bool has_poly = false;
    std::vector<std::array<double, 2>> poly;
    double poly_tol_mm = 0.0;
    bool has_radial = false;
    double radial_core_mm = 1e30;
    double radial_limit_mm = 1e30;
};

inline CommonFieldSupport compute_common_field_support(
    const Profile& prof,
    const std::vector<double>& state_weights) {
    CommonFieldSupport sup;
    bool any_active = false;
    for (size_t i = 0; i < prof.states.size(); ++i) {
        if (state_weights[i] <= 1e-6) continue;
        any_active = true;
        const auto& st = prof.states[i];
        if (!st.field_xy_mm.empty()) {
            auto ci = convex_hull_2d(st.field_xy_mm);
            if (ci.empty()) return CommonFieldSupport{};
            if (!sup.has_poly) {
                sup.poly = std::move(ci);
                sup.has_poly = true;
            } else {
                sup.poly = intersect_convex_polys_2d(sup.poly, ci);
                if (sup.poly.empty()) return CommonFieldSupport{};
            }
        } else {
            double r_max = 0.0;
            for (double rv : st.radii_mm) r_max = std::max(r_max, rv);
            const double r_lim = r_max + std::max(0.05, 0.02 * r_max);
            sup.has_radial = true;
            sup.radial_core_mm = std::min(sup.radial_core_mm, r_max);
            sup.radial_limit_mm = std::min(sup.radial_limit_mm, r_lim);
        }
    }
    if (!any_active) return CommonFieldSupport{};
    if (sup.has_poly) {
        double max_r = 0.0;
        for (const auto& v : sup.poly) max_r = std::max(max_r, std::hypot(v[0], v[1]));
        sup.poly_tol_mm = std::max(0.05, 0.01 * max_r);
        if (sup.has_radial && point_to_convex_poly_dist_2d(sup.poly, 0.0, 0.0) > sup.radial_limit_mm + sup.poly_tol_mm) {
            return CommonFieldSupport{};
        }
    }
    sup.valid = true;
    return sup;
}

inline double eval_common_field_support_weight(const CommonFieldSupport& sup, double x_mm, double y_mm) noexcept {
    if (!sup.valid) return 0.0;
    double w = 1.0;
    if (sup.has_poly) {
        const double dp = point_to_convex_poly_dist_2d(sup.poly, x_mm, y_mm);
        if (dp >= sup.poly_tol_mm) return 0.0;
        if (dp > 0.0) {
            const double u = std::clamp(dp / std::max(1e-9, sup.poly_tol_mm), 0.0, 1.0);
            w = std::min(w, 1.0 - u * u * (3.0 - 2.0 * u));
        }
    }
    if (sup.has_radial) {
        const double r = std::hypot(x_mm, y_mm);
        if (r >= sup.radial_limit_mm) return 0.0;
        if (r > sup.radial_core_mm) {
            const double span = std::max(1e-9, sup.radial_limit_mm - sup.radial_core_mm);
            const double u = std::clamp((r - sup.radial_core_mm) / span, 0.0, 1.0);
            w = std::min(w, 1.0 - u * u * (3.0 - 2.0 * u));
        }
    }
    return w;
}

inline bool point_in_common_field_support(const CommonFieldSupport& sup, double x_mm, double y_mm) noexcept {
    if (!sup.valid) return false;
    if (sup.has_poly && point_to_convex_poly_dist_2d(sup.poly, x_mm, y_mm) > sup.poly_tol_mm) return false;
    if (sup.has_radial && std::hypot(x_mm, y_mm) > sup.radial_limit_mm) return false;
    return true;
}

inline bool box_intersects_common_field_support(
    const CommonFieldSupport& sup,
    double x0_mm, double x1_mm,
    double y0_mm, double y1_mm) {
    if (!sup.valid) return false;
    if (x0_mm > x1_mm) std::swap(x0_mm, x1_mm);
    if (y0_mm > y1_mm) std::swap(y0_mm, y1_mm);
    if (sup.has_poly && !sup.has_radial) {
        return box_intersects_convex_poly_2d(sup.poly, sup.poly_tol_mm, x0_mm, x1_mm, y0_mm, y1_mm);
    }
    if (!sup.has_poly && sup.has_radial) {
        return std::hypot(std::clamp(0.0, x0_mm, x1_mm), std::clamp(0.0, y0_mm, y1_mm)) <= sup.radial_limit_mm;
    }
    if (!box_intersects_convex_poly_2d(sup.poly, sup.poly_tol_mm, x0_mm, x1_mm, y0_mm, y1_mm)) return false;
    if (std::hypot(std::clamp(0.0, x0_mm, x1_mm), std::clamp(0.0, y0_mm, y1_mm)) > sup.radial_limit_mm) return false;

    // Hybrid poly + radial disk: exact 1-Lipschitz 2D branch-and-bound
    auto eval_excess = [&](double x, double y) noexcept {
        const double dp = sup.has_poly ? (point_to_convex_poly_dist_2d(sup.poly, x, y) - sup.poly_tol_mm) : -1e30;
        const double dr = sup.has_radial ? (std::hypot(x, y) - sup.radial_limit_mm) : -1e30;
        return std::max(dp, dr);
    };
    struct Cell { double xa, xb, ya, yb; int depth; };
    std::vector<Cell> stk;
    stk.reserve(64);
    stk.push_back({x0_mm, x1_mm, y0_mm, y1_mm, 0});
    while (!stk.empty()) {
        const Cell c = stk.back();
        stk.pop_back();
        const double cx = 0.5 * (c.xa + c.xb);
        const double cy = 0.5 * (c.ya + c.yb);
        const double g = eval_excess(cx, cy);
        if (g <= 0.0) return true;
        const double half_diag = 0.5 * std::hypot(c.xb - c.xa, c.yb - c.ya);
        if (g > half_diag || c.depth >= 18) continue;
        stk.push_back({c.xa, cx, c.ya, cy, c.depth + 1});
        stk.push_back({cx, c.xb, c.ya, cy, c.depth + 1});
        stk.push_back({c.xa, cx, cy, c.yb, c.depth + 1});
        stk.push_back({cx, c.xb, cy, c.yb, c.depth + 1});
    }
    return false;
}

inline bool box_intersects_state_field_hull(
    const StateSlice& st,
    double x0_mm, double x1_mm,
    double y0_mm, double y1_mm) {
    if (x0_mm > x1_mm) std::swap(x0_mm, x1_mm);
    if (y0_mm > y1_mm) std::swap(y0_mm, y1_mm);
    if (!st.field_xy_mm.empty()) {
        const auto poly = convex_hull_2d(st.field_xy_mm);
        double max_r = 0.0;
        for (const auto& pt : poly) max_r = std::max(max_r, std::hypot(pt[0], pt[1]));
        const double tol_xy_mm = std::max(0.05, 0.01 * max_r);
        return box_intersects_convex_poly_2d(poly, tol_xy_mm, x0_mm, x1_mm, y0_mm, y1_mm);
    }
    double r_max = 0.0;
    for (double rv : st.radii_mm) r_max = std::max(r_max, rv);
    const double tol_r_mm = std::max(0.05, 0.02 * r_max);
    const double cx = std::clamp(0.0, x0_mm, x1_mm);
    const double cy = std::clamp(0.0, y0_mm, y1_mm);
    return std::hypot(cx, cy) <= r_max + tol_r_mm;
}

} // namespace detail

inline bool is_shot_within_state_hull(
    const Profile& prof,
    double focal_mm,
    double focus_m,
    double aperture_fnum,
    std::string* out_reason = nullptr) {
    std::vector<double> weights;
    return detail::project_state_convex_hull(prof, focal_mm, focus_m, aperture_fnum, weights, out_reason);
}

inline bool does_box_intersect_profile_field_hull(
    const Profile& prof,
    double focal_mm,
    double focus_m,
    double aperture_fnum,
    double x0_mm, double x1_mm,
    double y0_mm, double y1_mm) {
    std::vector<double> state_weights;
    if (!detail::project_state_convex_hull(prof, focal_mm, focus_m, aperture_fnum, state_weights, nullptr)) {
        return false;
    }
    const auto common_sup = detail::compute_common_field_support(prof, state_weights);
    return detail::box_intersects_common_field_support(common_sup, x0_mm, x1_mm, y0_mm, y1_mm);
}

inline bool is_sensor_box_fully_inside_profile_field(
    const Profile& prof,
    double focal_mm,
    double focus_m,
    double aperture_fnum,
    double x0_mm, double x1_mm,
    double y0_mm, double y1_mm) {
    std::vector<double> state_weights;
    if (!detail::project_state_convex_hull(prof, focal_mm, focus_m, aperture_fnum, state_weights, nullptr)) {
        return false;
    }
    const auto common_sup = detail::compute_common_field_support(prof, state_weights);
    if (!common_sup.valid) return false;
    const std::array<std::array<double, 2>, 4> corners{{
        {x0_mm, y0_mm}, {x1_mm, y0_mm}, {x1_mm, y1_mm}, {x0_mm, y1_mm}
    }};
    for (const auto& c : corners) {
        if (detail::eval_common_field_support_weight(common_sup, c[0], c[1]) < 0.999999) {
            return false;
        }
    }
    return true;
}

inline bool get_profile_common_support_centroid_mm(
    const Profile& prof,
    double focal_mm,
    double focus_m,
    double aperture_fnum,
    double& out_x_mm,
    double& out_y_mm) {
    std::vector<double> state_weights;
    if (!detail::project_state_convex_hull(prof, focal_mm, focus_m, aperture_fnum, state_weights, nullptr)) {
        return false;
    }
    const auto common_sup = detail::compute_common_field_support(prof, state_weights);
    if (!common_sup.valid) return false;
    if (common_sup.has_poly && !common_sup.poly.empty()) {
        double sx = 0.0, sy = 0.0;
        for (const auto& v : common_sup.poly) { sx += v[0]; sy += v[1]; }
        out_x_mm = sx / double(common_sup.poly.size());
        out_y_mm = sy / double(common_sup.poly.size());
        return true;
    }
    out_x_mm = 0.0;
    out_y_mm = 0.0;
    return true;
}

struct ProfileFieldBlender {
    detail::CommonFieldSupport sup{};
    bool active = false;

    static ProfileFieldBlender make(const Profile& prof, double focal_mm, double focus_m, double aperture_fnum) {
        ProfileFieldBlender b;
        std::vector<double> state_weights;
        if (detail::project_state_convex_hull(prof, focal_mm, focus_m, aperture_fnum, state_weights, nullptr)) {
            b.sup = detail::compute_common_field_support(prof, state_weights);
            b.active = b.sup.valid;
        }
        return b;
    }

    float eval_weight(double x_mm, double y_mm) const noexcept {
        if (!active) return 0.0f;
        return static_cast<float>(detail::eval_common_field_support_weight(sup, x_mm, y_mm));
    }
};

inline bool discover_matching_nlens(
    std::string_view exif_lens,
    double focal_mm,
    double aperture,
    Profile& out,
    std::string_view exe_dir_utf8 = {},
    std::string_view exif_body = {},
    double focus_m = 0.0,
    std::string* reject_summary = nullptr
) {
    const std::string q_norm = detail::norm_token(exif_lens);
    if (q_norm.empty()) return false;

    std::vector<std::filesystem::path> dirs;
    if (const char* home = std::getenv("HOME"); home && *home) {
        dirs.push_back(std::filesystem::path(home) / ".config" / "nijin" / "lenses");
    }
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        dirs.push_back(std::filesystem::path(appdata) / "nijin" / "lenses");
    }
#endif
    if (!exe_dir_utf8.empty()) {
        const std::filesystem::path exe_d = heraw_fs::from_utf8(std::string(exe_dir_utf8));
        dirs.push_back(exe_d / "lenses");
        dirs.push_back(exe_d / ".." / "Resources" / "lenses");
    }
    dirs.push_back(std::filesystem::path("lenses"));

    struct ScoredCand {
        int score = 0;
        std::string sort_key;
        Profile prof;
    };
    std::vector<ScoredCand> matches;

    for (const auto& d : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(d, ec)) continue;
        std::vector<std::filesystem::path> files;
        for (const auto& ent : std::filesystem::directory_iterator(d, ec)) {
            if (ec || !ent.is_regular_file(ec)) continue;
            if (ent.path().extension() == ".nlens") files.push_back(ent.path());
        }
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
            return heraw_fs::to_utf8(a) < heraw_fs::to_utf8(b);
        });
        for (const auto& fp : files) {
            Profile cand;
            if (!load_nlens_file(heraw_fs::to_utf8(fp), cand)) continue;
            bool name_match = false;
            bool exact_alias = false;
            for (const auto& al : cand.match_aliases) {
                const std::string an = detail::norm_token(al);
                if (!an.empty() && (q_norm.find(an) != std::string::npos || an.find(q_norm) != std::string::npos)) {
                    name_match = true;
                    if (an == q_norm) exact_alias = true;
                    break;
                }
            }
            if (!name_match) continue;
            if (!body_matches_profile(exif_body, cand)) continue;
            std::vector<double> st_weights;
            if (!detail::project_state_convex_hull(cand, focal_mm, focus_m, aperture, st_weights, nullptr)) continue;
            const bool specific_body = !is_generic_sensor_label(detail::norm_token(cand.target_sensor));
            const int sc = (exact_alias ? 100 : 50) + (specific_body ? 30 : 0) + int(cand.tier) * 5;
            matches.push_back({sc, heraw_fs::to_utf8(fp), std::move(cand)});
        }
        if (!matches.empty()) break; // Precedence by search-directory tier, deterministic within directory
    }
    if (matches.empty()) return false;
    std::sort(matches.begin(), matches.end(), [](const ScoredCand& a, const ScoredCand& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.sort_key < b.sort_key;
    });
    if (matches.size() >= 2 && matches[0].score == matches[1].score &&
        matches[0].prof.lens_name != matches[1].prof.lens_name) {
        if (reject_summary) {
            *reject_summary = "ambiguous equal-score .nlens profiles: " +
                matches[0].sort_key + " vs " + matches[1].sort_key;
        }
        return false;
    }
    out = std::move(matches[0].prof);
    return true;
}

// Evaluate complex 64x64 OTF (and scalar distortion/vignette/LCA) at field coordinates (x_mm, y_mm)
// for output grid scale `out_scale` in {1, 2} and sensor pixel pitch `sensor_pitch_um`.
// Strictly abstains (`valid = false`) outside the profile's calibrated (focal, focus, aperture, field) hull.
inline EvaluatedFieldPoint evaluate_field_point(
    const Profile& prof,
    double focal_mm,
    double focus_m,
    double aperture_fnum,
    double x_mm,
    double y_mm,
    double sensor_pitch_um,
    int out_scale,
    double defocus_offset_um = 0.0,
    double eval_lambda_nm = 0.0) {
    EvaluatedFieldPoint res;
    if (!prof.loaded || prof.states.empty()) {
        res.fallback_reason = "profile_not_loaded";
        return res;
    }

    const double r_mm = std::hypot(x_mm, y_mm);
    // Strict joint convex-hull enforcement in (log(focal), log(aperture), focus_diopter)
    std::vector<double> state_weights;
    if (!detail::project_state_convex_hull(prof, focal_mm, focus_m, aperture_fnum, state_weights, &res.fallback_reason)) {
        return res;
    }

    // Enforce membership in the exact common field support across all active states first
    const auto common_sup = detail::compute_common_field_support(prof, state_weights);
    if (!detail::point_in_common_field_support(common_sup, x_mm, y_mm)) {
        res.fallback_reason = "outside_field_hull";
        return res;
    }

    // Collect active states in the convex combination and compute per-state field interpolation weights
    struct ActiveStateEval {
        size_t idx = 0;
        double weight = 0.0;
        std::vector<double> field_weights;
    };
    std::vector<ActiveStateEval> active_states;
    double w_sum = 0.0;
    for (size_t i = 0; i < prof.states.size(); ++i) {
        if (state_weights[i] <= 1e-6) continue;
        std::vector<double> fw;
        if (!detail::project_state_field_hull(prof.states[i], x_mm, y_mm, fw)) {
            res.fallback_reason = "outside_field_hull";
            return res;
        }
        active_states.push_back({i, state_weights[i], std::move(fw)});
        w_sum += state_weights[i];
    }
    if (active_states.empty() || w_sum <= 1e-12) {
        res.fallback_reason = "outside_state_hull";
        return res;
    }
    for (auto& as : active_states) as.weight /= w_sum;

    const double phi_field = std::atan2(y_mm, x_mm);
    const double c_phi = std::cos(phi_field), s_phi = std::sin(phi_field);
    const double pitch_mm = std::max(0.0007, sensor_pitch_um * 1e-3);
    const double out_pitch_mm = pitch_mm / double(std::max(1, out_scale));

    auto eval_single_state = [&](const StateSlice& st, const std::vector<double>& field_w, EvaluatedFieldPoint& dst) {
        const double ev_drop = st.vignetting_ev.empty() ? 0.0 : detail::lerp1d(st.radii_mm, st.vignetting_ev, r_mm);
        dst.vig_gain_linear = std::pow(2.0, std::max(0.0, -ev_drop));
        dst.dist_pct = st.distortion_pct.empty() ? 0.0 : detail::lerp1d(st.radii_mm, st.distortion_pct, r_mm);
        dst.lca_r_frac = st.lca_red_ppm.empty() ? 0.0 : (1e-6 * detail::lerp1d(st.radii_mm, st.lca_red_ppm, r_mm));
        dst.lca_b_frac = st.lca_blue_ppm.empty() ? 0.0 : (1e-6 * detail::lerp1d(st.radii_mm, st.lca_blue_ppm, r_mm));
        dst.skirt = st.skirt;

        const double lam_nm = (eval_lambda_nm > 300.0) ? eval_lambda_nm : st.lambda_nm;
        const double lam_um = lam_nm * 1e-3;
        const double lam_mm = lam_nm * 1e-6;
        // Chromatic axial defocus shift when evaluating away from nominal state wavelength
        const double chrom_defocus_um = (std::abs(lam_nm - st.lambda_nm) > 1.0 && std::abs(st.loca_um) > 1e-6)
            ? (st.loca_um * ((lam_nm - 550.0) / 150.0))
            : 0.0;
        const double tot_defocus_um = defocus_offset_um + chrom_defocus_um;

        // Tier 3: Zero-padded 2D FFT linear pupil autocorrelation (M=64->N=128, adaptive upsample M=128->N=256)
        if (prof.tier == Tier::RaytraceWavefront) {
            if (st.wavefront_by_r.empty()) {
                dst.valid = false;
                dst.effective_tier = Tier::None;
                dst.fallback_reason = "missing_wavefront_terms";
                return;
            }
            const double na = (st.na_air > 1e-4)
                ? std::min(0.95, st.na_air)
                : std::min(0.48, 1.0 / std::max(1.4, 2.0 * st.aperture_fnum));
            const double r_pup_mm = (st.exit_diameter_mm > 1e-3)
                ? (0.5 * st.exit_diameter_mm)
                : std::max(1.0, st.exit_pupil_z_mm * na);
            const double delta_d_um = (prof.coverglass_mm - prof.design_coverglass_mm) * 1000.0;
            const double ng = prof.coverglass_nd;
            const double q_cx = st.has_chief_q ? st.chief_q_xy[0] : (x_mm / std::hypot(r_mm, st.exit_pupil_z_mm));
            const double q_cy = st.has_chief_q ? st.chief_q_xy[1] : (y_mm / std::hypot(r_mm, st.exit_pupil_z_mm));

            // Interpolate field wavefront Zernike terms via 2D convex hull weights (`nodes[]`) or radial knots (`states[]`)
            std::vector<ZernikeTerm> zterms_interp;
            auto accum_zernike = [&](const std::vector<ZernikeTerm>& src, double wt) {
                if (wt <= 1e-9) return;
                for (const auto& z : src) {
                    if (std::abs(z.coeff_um) <= 1e-12) continue;
                    bool found = false;
                    for (auto& ex : zterms_interp) {
                        if (ex.n == z.n && ex.m == z.m && ex.is_sin == z.is_sin) {
                            ex.coeff_um += wt * z.coeff_um;
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        ZernikeTerm nz = z;
                        nz.coeff_um = wt * z.coeff_um;
                        zterms_interp.push_back(nz);
                    }
                }
            };
            if (!st.field_xy_mm.empty() && field_w.size() == st.wavefront_by_r.size()) {
                for (size_t k = 0; k < field_w.size(); ++k) accum_zernike(st.wavefront_by_r[k], field_w[k]);
            } else if (st.radii_mm.size() >= 2 && st.radii_mm.size() == st.wavefront_by_r.size()) {
                const double rq = std::clamp(r_mm, st.radii_mm.front(), st.radii_mm.back());
                auto it = std::upper_bound(st.radii_mm.begin(), st.radii_mm.end(), rq);
                if (it == st.radii_mm.begin()) {
                    accum_zernike(st.wavefront_by_r.front(), 1.0);
                } else if (it == st.radii_mm.end()) {
                    accum_zernike(st.wavefront_by_r.back(), 1.0);
                } else {
                    const size_t i1_r = size_t(it - st.radii_mm.begin());
                    const size_t i0_r = i1_r - 1;
                    const double span_r = std::max(1e-9, st.radii_mm[i1_r] - st.radii_mm[i0_r]);
                    const double t_r = std::clamp((rq - st.radii_mm[i0_r]) / span_r, 0.0, 1.0);
                    accum_zernike(st.wavefront_by_r[i0_r], 1.0 - t_r);
                    accum_zernike(st.wavefront_by_r[i1_r], t_r);
                }
            } else {
                size_t r_idx = 0;
                for (size_t k = 1; k < st.radii_mm.size() && k < st.wavefront_by_r.size(); ++k) {
                    if (std::abs(st.radii_mm[k] - r_mm) < std::abs(st.radii_mm[r_idx] - r_mm)) r_idx = k;
                }
                accum_zernike(st.wavefront_by_r[r_idx], 1.0);
            }
            const auto& zterms = zterms_interp;

            // Precompute dielectric stack + coverglass OPD with exact chief-ray piston & tilt subtraction (A11)
            auto plate_w_um = [&](double qx, double qy) -> double {
                const double q2 = std::min(0.92, qx * qx + qy * qy);
                double sum_um = 0.0;
                if (std::abs(delta_d_um) >= 1e-6) {
                    sum_um += delta_d_um * (std::sqrt(ng * ng - q2) - std::sqrt(1.0 - q2));
                }
                for (const auto& ly : st.stack) {
                    sum_um += (ly.delta_d_mm * 1000.0) *
                              (std::sqrt(ly.n_lambda * ly.n_lambda - q2) - std::sqrt(1.0 - q2));
                }
                return sum_um;
            };
            const bool has_plate = (std::abs(delta_d_um) >= 1e-6) || !st.stack.empty();
            const double w_c0 = has_plate ? plate_w_um(q_cx, q_cy) : 0.0;
            const double eps_q = 1e-4;
            const double dw_dqx = (has_plate && prof.includes.geometry_tilt_removed)
                ? ((plate_w_um(q_cx + eps_q, q_cy) - plate_w_um(q_cx - eps_q, q_cy)) / (2.0 * eps_q))
                : 0.0;
            const double dw_dqy = (has_plate && prof.includes.geometry_tilt_removed)
                ? ((plate_w_um(q_cx, q_cy + eps_q) - plate_w_um(q_cx, q_cy - eps_q)) / (2.0 * eps_q))
                : 0.0;
            const double amp_exp = 0.5 * std::max(0.0, st.cra_power_intensity);
            const double si_sigma_mm = (!prof.includes.silicon_diffusion && st.silicon_sigma_um > 0.0)
                ? (st.silicon_sigma_um * 1e-3)
                : 0.0;

            auto run_pupil_grid = [&](int m_pup, int n_fft, std::vector<std::complex<double>>& pup_out,
                                      double& flux_out, double& max_step_out) {
                pup_out.assign(size_t(n_fft) * size_t(n_fft), {0.0, 0.0});
                flux_out = 0.0;
                max_step_out = 0.0;
                const double half_c = 0.5 * double(m_pup - 1);
                std::vector<double> prev_row_w(size_t(m_pup), 0.0);
                std::vector<uint8_t> prev_row_valid(size_t(m_pup), 0u);
                for (int iy = 0; iy < m_pup; ++iy) {
                    const double eta = (double(iy) - half_c) / half_c;
                    double prev_w = 0.0;
                    bool prev_valid = false;
                    for (int ix = 0; ix < m_pup; ++ix) {
                        const double xi = (double(ix) - half_c) / half_c;
                        const double rho2 = xi * xi + eta * eta;
                        if (rho2 > 1.0) {
                            prev_valid = false;
                            prev_row_valid[size_t(ix)] = 0u;
                            continue;
                        }
                        const double qx = q_cx + na * xi, qy = q_cy + na * eta;
                        const double qz = std::sqrt(std::max(0.05, 1.0 - std::min(0.95, qx * qx + qy * qy)));
                        bool clipped = false;
                        for (const auto& ms : st.stops) {
                            const double px = r_pup_mm * xi + ms.z_rel_exit_mm * (qx / qz) - ms.decenter_x_mm;
                            const double py = r_pup_mm * eta + ms.z_rel_exit_mm * (qy / qz) - ms.decenter_y_mm;
                            if (px * px + py * py > ms.radius_mm * ms.radius_mm) { clipped = true; break; }
                        }
                        if (clipped) {
                            prev_valid = false;
                            prev_row_valid[size_t(ix)] = 0u;
                            continue;
                        }

                        const double rho = std::sqrt(rho2);
                        const double theta = std::atan2(eta, xi) - phi_field;
                        double w_um = 0.5 * tot_defocus_um * na * na * (2.0 * rho2 - 1.0);
                        for (const auto& zt : zterms) {
                            const double rad = detail::zernike_radial(zt.n, zt.m, rho);
                            const double ang = (zt.m == 0) ? 1.0 : (zt.is_sin ? std::sin(zt.m * theta) : std::cos(zt.m * theta));
                            w_um += zt.coeff_um * rad * ang;
                        }
                        if (has_plate) {
                            w_um += plate_w_um(qx, qy) - w_c0 - dw_dqx * (qx - q_cx) - dw_dqy * (qy - q_cy);
                        }
                        // Active-neighbor-only phase step guard (ignores exterior zero-padded pupil rim!)
                        if (prev_valid) {
                            max_step_out = std::max(max_step_out, std::abs(w_um - prev_w) / lam_um);
                        }
                        if (prev_row_valid[size_t(ix)]) {
                            max_step_out = std::max(max_step_out, std::abs(w_um - prev_row_w[size_t(ix)]) / lam_um);
                        }
                        prev_w = w_um;
                        prev_valid = true;
                        prev_row_w[size_t(ix)] = w_um;
                        prev_row_valid[size_t(ix)] = 1u;

                        const double amp = (amp_exp > 1e-9) ? std::pow(qz, amp_exp) : 1.0;
                        const double ph = 2.0 * std::numbers::pi_v<double> * (w_um / lam_um);
                        pup_out[size_t(iy) * size_t(n_fft) + size_t(ix)] = std::polar(amp, ph);
                        flux_out += amp * amp;
                    }
                }
            };

            int m_pup = 64, n_fft = 128;
            std::vector<std::complex<double>> pup;
            double flux = 0.0, max_dphase_step = 0.0;
            run_pupil_grid(m_pup, n_fft, pup, flux, max_dphase_step);
            if (flux > 1e-9 && max_dphase_step >= 0.45 && max_dphase_step < 0.90) {
                m_pup = 128;
                n_fft = 256;
                run_pupil_grid(m_pup, n_fft, pup, flux, max_dphase_step);
            }
            if (flux > 1e-9 && max_dphase_step >= 0.45 && max_dphase_step < 1.80) {
                m_pup = 256;
                n_fft = 512;
                run_pupil_grid(m_pup, n_fft, pup, flux, max_dphase_step);
            }
            if (!(flux > 1e-9) || max_dphase_step >= 0.45) {
                dst.valid = false;
                dst.effective_tier = Tier::None;
                dst.fallback_reason = !(flux > 1e-9) ? "clipped_pupil_zero_flux" : "pupil_phase_nyquist_exceeded";
                return;
            }

            detail::fft2d_128(pup, n_fft, false);
            for (auto& c : pup) c = std::complex<double>(std::norm(c), 0.0);
            detail::fft2d_128(pup, n_fft, true);
            const double inv0 = 1.0 / std::max(1e-12, pup[0].real());
            const double f_step_lp_mm = (2.0 * na / lam_mm) / double(m_pup - 1);
            const double max_lag = double(m_pup - 2);

            // Note: IFFT_+(|FFT_-(P)|^2)[+u] = sum_j P[j+u] conj(P[j]) = conj(H(+u)).
            // Under convention `fft_negative;pupil_exp_positive`, H(+f) = sum_j P[j] conj(P[j+u]) = conj(R[+u]).
            auto sample_h_cont = [&](double fx_lp, double fy_lp) -> std::complex<double> {
                const double ux = fx_lp / f_step_lp_mm;
                const double uy = fy_lp / f_step_lp_mm;
                if (std::hypot(ux, uy) >= max_lag) return {0.0, 0.0};
                const int iy0 = int(std::floor(uy)), ix0 = int(std::floor(ux));
                const double ty = uy - iy0, tx = ux - ix0;
                auto at = [&](int r, int c) {
                    const int rr = (r % n_fft + n_fft) % n_fft;
                    const int cc = (c % n_fft + n_fft) % n_fft;
                    return pup[size_t(rr) * size_t(n_fft) + size_t(cc)] * inv0;
                };
                const auto r_pos = (1.0 - ty) * ((1.0 - tx) * at(iy0, ix0) + tx * at(iy0, ix0 + 1)) +
                                   ty * ((1.0 - tx) * at(iy0 + 1, ix0) + tx * at(iy0 + 1, ix0 + 1));
                std::complex<double> h_val = std::conj(r_pos);
                if (si_sigma_mm > 0.0) {
                    const double f2 = fx_lp * fx_lp + fy_lp * fy_lp;
                    h_val *= std::exp(-2.0 * std::numbers::pi_v<double> * std::numbers::pi_v<double> * si_sigma_mm * si_sigma_mm * f2);
                }
                return h_val;
            };

            for (int ky = 0; ky < 64; ++ky) {
                const int sy = (ky < 32) ? ky : (ky - 64);
                const double fy_pos = (double(sy) / 64.0) / out_pitch_mm;
                for (int kx = 0; kx < 64; ++kx) {
                    const int sx = (kx < 32) ? kx : (kx - 64);
                    const double fx_pos = (double(sx) / 64.0) / out_pitch_mm;
                    std::complex<double> h_bin;
                    // Symmetrize Nyquist boundary lines kx==32 / ky==32 so the 64x64 discrete OTF
                    // has exact Hermitian symmetry everywhere and strictly real spatial PSF!
                    if (kx == 32 && ky == 32) {
                        const double fn = (32.0 / 64.0) / out_pitch_mm;
                        h_bin = 0.25 * (sample_h_cont(fn, fn) + sample_h_cont(-fn, fn) +
                                        sample_h_cont(fn, -fn) + sample_h_cont(-fn, -fn));
                        h_bin.imag(0.0);
                    } else if (kx == 32) {
                        const double fn = (32.0 / 64.0) / out_pitch_mm;
                        h_bin = 0.5 * (sample_h_cont(fn, fy_pos) + sample_h_cont(-fn, fy_pos));
                        if (ky == 0) h_bin.imag(0.0);
                    } else if (ky == 32) {
                        const double fn = (32.0 / 64.0) / out_pitch_mm;
                        h_bin = 0.5 * (sample_h_cont(fx_pos, fn) + sample_h_cont(fx_pos, -fn));
                        if (kx == 0) h_bin.imag(0.0);
                    } else if (kx == 0 && ky == 0) {
                        h_bin = {1.0, 0.0};
                    } else {
                        h_bin = sample_h_cont(fx_pos, fy_pos);
                    }
                    double re = h_bin.real();
                    double im = h_bin.imag();
                    if (std::abs(im) < 1e-12) im = 0.0;
                    dst.otf_re[ky * 64 + kx] = float(re);
                    dst.otf_im[ky * 64 + kx] = float(im);
                }
            }
            dst.otf_re[0] = 1.0f;
            dst.otf_im[0] = 0.0f;
            dst.effective_tier = Tier::RaytraceWavefront;
            dst.valid = true;
            return;
        }

        // Tier 1 / Tier 2: Interpolate radial S/T MTF curves + optional odd tangential coma cubic phase
        std::vector<double> mtf_s_r(st.mtf_lp_mm.size(), 0.8), mtf_t_r(st.mtf_lp_mm.size(), 0.8);
        if (!st.mtf_s.empty() && !st.mtf_lp_mm.empty()) {
            if (!st.field_xy_mm.empty() && field_w.size() == st.mtf_s.size()) {
                for (size_t fi = 0; fi < st.mtf_lp_mm.size(); ++fi) {
                    double ss = 0.0, st_v = 0.0;
                    for (size_t k = 0; k < field_w.size(); ++k) {
                        const double vs = (fi < st.mtf_s[k].size()) ? st.mtf_s[k][fi] : 0.8;
                        const double vt = (k < st.mtf_t.size() && fi < st.mtf_t[k].size()) ? st.mtf_t[k][fi] : vs;
                        ss += field_w[k] * vs;
                        st_v += field_w[k] * vt;
                    }
                    mtf_s_r[fi] = ss;
                    mtf_t_r[fi] = st_v;
                }
            } else {
                const size_t nr = std::min(st.radii_mm.size(), st.mtf_s.size());
                for (size_t fi = 0; fi < st.mtf_lp_mm.size(); ++fi) {
                    std::vector<double> vs(nr), vt(nr);
                    for (size_t ri = 0; ri < nr; ++ri) {
                        vs[ri] = (fi < st.mtf_s[ri].size()) ? st.mtf_s[ri][fi] : 0.8;
                        vt[ri] = (ri < st.mtf_t.size() && fi < st.mtf_t[ri].size()) ? st.mtf_t[ri][fi] : vs[ri];
                    }
                    std::vector<double> rs(st.radii_mm.begin(), st.radii_mm.begin() + ptrdiff_t(nr));
                    mtf_s_r[fi] = detail::lerp1d(rs, vs, r_mm);
                    mtf_t_r[fi] = detail::lerp1d(rs, vt, r_mm);
                }
            }
        }
        const bool allow_asym_phase = (prof.tier == Tier::LabMtfAsymmetric) && (st.phase_source != "assume_zero");
        const double coma_waves = (allow_asym_phase && !st.coma_c31_waves.empty())
            ? detail::lerp1d(st.radii_mm, st.coma_c31_waves, r_mm) : 0.0;
        const double cutoff_lp = detail::circular_pupil_cutoff_lp_mm(lam_nm, st.aperture_fnum);
        const double defocus_blur_mm = std::abs(tot_defocus_um) * 1e-3 / std::max(1.4, 2.0 * st.aperture_fnum);

        for (int ky = 0; ky < 64; ++ky) {
            const int sy = (ky < 32) ? ky : (ky - 64);
            const double fy_lp = (double(sy) / 64.0) / out_pitch_mm;
            for (int kx = 0; kx < 64; ++kx) {
                const int sx = (kx < 32) ? kx : (kx - 64);
                const double fx_lp = (double(sx) / 64.0) / out_pitch_mm;
                const double f_lp = std::hypot(fx_lp, fy_lp);
                if (f_lp <= 1e-9) {
                    dst.otf_re[ky * 64 + kx] = 1.0f;
                    dst.otf_im[ky * 64 + kx] = 0.0f;
                    continue;
                }
                // Project (fx, fy) onto radial (tangential) and azimuthal (sagittal) axes
                const double f_tan = (r_mm > 1e-4) ? (fx_lp * c_phi + fy_lp * s_phi) : fx_lp;
                const double f_sag = (r_mm > 1e-4) ? (-fx_lp * s_phi + fy_lp * c_phi) : fy_lp;
                const double wt2 = (f_tan * f_tan) / (f_lp * f_lp);
                const double ws2 = (f_sag * f_sag) / (f_lp * f_lp);
                const double ms = detail::eval_mtf_curve_safe(st.mtf_lp_mm, mtf_s_r, st.mtf_se, f_lp, cutoff_lp);
                const double mt = detail::eval_mtf_curve_safe(st.mtf_lp_mm, mtf_t_r, st.mtf_se, f_lp, cutoff_lp);
                double mag = ws2 * ms + wt2 * mt;
                if (defocus_blur_mm > 1e-6) {
                    const double arg = std::numbers::pi_v<double> * defocus_blur_mm * f_lp;
                    mag *= std::exp(-0.5 * arg * arg);
                }
                // Odd cubic coma phase along radial direction (negative sign matches `fft_negative;pupil_exp_positive`
                // and zeroed on Nyquist boundary lines kx==32 / ky==32 for exact 64x64 Hermitian symmetry)
                double ph = 0.0;
                if (kx != 32 && ky != 32) {
                    if (allow_asym_phase && st.has_cubic_phase_mm3) {
                        const auto& cp = st.cubic_phase_rad_mm3;
                        ph = cp[0] * f_tan * f_tan * f_tan +
                             cp[1] * f_tan * f_tan * f_sag +
                             cp[2] * f_tan * f_sag * f_sag +
                             cp[3] * f_sag * f_sag * f_sag;
                    } else if (std::abs(coma_waves) > 1e-5) {
                        const double u_tan = std::clamp(f_tan / std::max(50.0, cutoff_lp), -1.0, 1.0);
                        const double u_rad = std::clamp(f_lp / std::max(50.0, cutoff_lp), 0.0, 1.0);
                        ph = -2.0 * std::numbers::pi_v<double> * coma_waves * (u_tan * u_rad * u_rad) * 0.35;
                    }
                }
                double im = mag * std::sin(ph);
                if (std::abs(im) < 1e-12) im = 0.0;
                dst.otf_re[ky * 64 + kx] = float(mag * std::cos(ph));
                dst.otf_im[ky * 64 + kx] = float(im);
            }
        }
        dst.effective_tier = (allow_asym_phase && (st.has_cubic_phase_mm3 || std::abs(coma_waves) > 1e-5 || st.skirt.weight > 1e-4))
            ? Tier::LabMtfAsymmetric
            : Tier::DatasheetMtf;
        dst.valid = true;
    };

    if (active_states.size() == 1) {
        eval_single_state(prof.states[active_states[0].idx], active_states[0].field_weights, res);
    } else {
        res.valid = true;
        res.effective_tier = Tier::RaytraceWavefront;
        res.vig_gain_linear = 0.0;
        res.dist_pct = 0.0;
        res.lca_r_frac = 0.0;
        res.lca_b_frac = 0.0;
        res.skirt = {};
        res.otf_re.fill(0.0f);
        res.otf_im.fill(0.0f);
        for (const auto& as : active_states) {
            EvaluatedFieldPoint cur;
            eval_single_state(prof.states[as.idx], as.field_weights, cur);
            if (!cur.valid) {
                res.valid = false;
                res.effective_tier = Tier::None;
                res.fallback_reason = cur.fallback_reason;
                break;
            }
            res.effective_tier = std::min(res.effective_tier, cur.effective_tier);
            res.vig_gain_linear += as.weight * cur.vig_gain_linear;
            res.dist_pct += as.weight * cur.dist_pct;
            res.lca_r_frac += as.weight * cur.lca_r_frac;
            res.lca_b_frac += as.weight * cur.lca_b_frac;
            res.skirt.weight += as.weight * cur.skirt.weight;
            res.skirt.sigma_um += as.weight * cur.skirt.sigma_um;
            // Interpolate complex OTF directly via true convex hull weights
            for (size_t k = 0; k < 64 * 64; ++k) {
                res.otf_re[k] += float(as.weight * cur.otf_re[k]);
                res.otf_im[k] += float(as.weight * cur.otf_im[k]);
            }
        }
    }

    // Derive equivalent 1x MTF50 and Gaussian sigma from axial OTF bins
    if (res.valid) {
        double m_quarter = 0.5 * (std::hypot(res.otf_re[0 * 64 + (16 / out_scale)], res.otf_im[0 * 64 + (16 / out_scale)]) +
                                  std::hypot(res.otf_re[(16 / out_scale) * 64 + 0], res.otf_im[(16 / out_scale) * 64 + 0]));
        m_quarter = std::clamp(m_quarter, 0.05, 0.995);
        const double f_q = (double(16 / out_scale) / 64.0) * double(out_scale); // ~0.25 cy/px_1x
        res.eff_sigma_1x_px = std::clamp(std::sqrt(-std::log(m_quarter) / (2.0 * std::numbers::pi_v<double> * std::numbers::pi_v<double> * f_q * f_q)), 0.22, 2.20);
        res.mtf50_1x_cy_px = std::clamp(std::sqrt(std::numbers::ln2_v<double>) / (std::sqrt(2.0) * std::numbers::pi_v<double> * res.eff_sigma_1x_px), 0.08, 0.48);
    }
    return res;
}

} // namespace heraw_nlens

#endif // HERAW_NLENS_HPP
