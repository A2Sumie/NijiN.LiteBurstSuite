// heraw_suite_handheld_optics.hpp -- Handheld Single-Focus Linear HDR Optical Operators:
//   1. OP-O1: 1st-Order Continuous Spectral CA (lateral radial/prismatic & axial-aura)
//      - Schott N-SK16 / N-F2 3-term Sellmeier dispersion anchored at 555.0 nm
//      - Monotone-psi equal-shift-step quadrature (<= 0.60 px step) with d(lambda) weights
//      - Curvature-penalized NNLS 7-simplex positive basis + channelwise flux cap + delta form
//   2. OP-O3: Wide-Aperture Positive Bokeh Reshaping (De-Nisen, Swirl, Anamorphic, Onion-Clean)
//      - Locked k(s) = 0.45 * s Gaussian apodization on existing defocus discs
//      - Pure-background support erosion & normalized convolution K*(m_bg*c) / K*(m_bg)
// Pure 7-bit ASCII C++20 header. Zero external dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

namespace heraw_hoptics {

using RowTask = std::function<void(int, int)>;
using ParallelRows = std::function<void(int rows, const RowTask& task)>;

enum class SpectralCaMode : uint8_t {
    OFF = 0,
    LATERAL = 1,
    AXIAL_AURA = 2
};

inline SpectralCaMode parse_spectral_ca_mode(const std::string& s) noexcept {
    if (s == "lateral") return SpectralCaMode::LATERAL;
    if (s == "axial-aura" || s == "axial_aura" || s == "aura") return SpectralCaMode::AXIAL_AURA;
    return SpectralCaMode::OFF;
}

inline const char* spectral_ca_mode_str(SpectralCaMode m) noexcept {
    switch (m) {
        case SpectralCaMode::LATERAL: return "lateral";
        case SpectralCaMode::AXIAL_AURA: return "axial-aura";
        default: return "off";
    }
}

struct SpectralCaConfig {
    SpectralCaMode mode = SpectralCaMode::OFF;
    float power_px = 0.0f;         // max dispersion shift / aura radius in output pixels
    bool radial = true;            // true = off-axis radial pullback, false = directional prism
    float angle_deg = 0.0f;        // prism dispersion angle when radial == false
    float aura_threshold = 0.72f;  // highlight onset in scene-linear luma for axial-aura
    float aura_mix = 0.65f;        // wet aura strength preserving 100% dry 555nm sharp core
    float dark_sigma = 0.0f;       // optional dark noise std for low-SNR pilot gating
    std::array<float, 3> wb_gain = {1.0f, 1.0f, 1.0f};
    const float* emitter_mask = nullptr; // optional H*W mask in [0,1] for axial-aura
    const float* protect_mask = nullptr; // optional H*W foreground protection mask in [0,1]

    bool active() const noexcept { return mode != SpectralCaMode::OFF && power_px > 1e-4f; }
};

struct BokehShapeConfig {
    float de_nisen = 0.0f;         // [0..1] -> sigma_add = 0.45 * de_nisen * R_hat
    float swirl = 0.0f;            // [-1..1] tangential off-axis Petzval bokeh swirl
    float aspect = 1.0f;           // [1..2] vertical anamorphic oval stretch
    float onion_clean = 0.0f;      // [0..1] radial ring suppression inside bright bokeh discs
    float focus_gate_px = 1.35f;   // R_hat <= focus_gate_px treated as sharp in-focus subject
    float fixed_defocus_px = 0.0f; // >0 overrides blind R_hat estimation (for controlled/bus runs)
    std::array<float, 3> wb_gain = {1.0f, 1.0f, 1.0f};
    const float* protect_mask = nullptr; // optional H*W foreground mask (1 = keep crisp)
    const float* focus_map_half = nullptr; // optional Hh*Wh native focus confidence in [0,1]
    int focus_h = 0;
    int focus_w = 0;

    bool active() const noexcept {
        return de_nisen > 1e-4f || std::fabs(swirl) > 1e-4f ||
               aspect > 1.001f || onion_clean > 1e-4f;
    }
};

struct HandheldOpticsTelemetry {
    bool bokeh_applied = false;
    bool spectral_applied = false;
    int spectral_nodes = 0;
    float max_step_px = 0.0f;
    double bokeh_ms = 0.0;
    double spectral_ms = 0.0;
};

// Schott N-SK16 - N-F2 3-term Sellmeier refractive index difference (wavelength in nm).
inline double hyperchromat_delta_n(double wl_nm) noexcept {
    const double u2 = (wl_nm * 1e-3) * (wl_nm * 1e-3);
    auto sellmeier = [&](double b1, double b2, double b3, double c1, double c2, double c3) noexcept {
        const double n2 = 1.0 + (b1 * u2) / (u2 - c1) + (b2 * u2) / (u2 - c2) + (b3 * u2) / (u2 - c3);
        return std::sqrt(std::max(1.0, n2));
    };
    // Schott N-SK16 & N-F2 catalog Sellmeier coefficients (matches artifacts/hyperchromat-lensbench)
    const double n_sk16 = sellmeier(1.34317774, 0.241144399, 0.994317969,
                                    0.00704687339, 0.0229005, 92.7508526);
    const double n_f2   = sellmeier(1.34533359, 0.209073176, 0.937357162,
                                    0.00997743871, 0.0470450767, 111.886764);
    return n_sk16 - n_f2;
}

// Normalized dispersion curve psi(lambda): psi(555.0) == 0, psi(435.84) - psi(656.27) == +1.0.
// Note delta_n(435.84) - delta_n(656.27) < 0, so dividing by (dn_g - dn_C) makes psi(435.84) > 0.
inline double hyperchromat_psi(double wl_nm) noexcept {
    const double dn_0 = hyperchromat_delta_n(555.0);
    const double dn_g = hyperchromat_delta_n(435.84);
    const double dn_c = hyperchromat_delta_n(656.27);
    const double denom = dn_g - dn_c;
    return (hyperchromat_delta_n(wl_nm) - dn_0) / denom;
}

inline double invert_hyperchromat_psi(double target_psi, double lo_nm, double hi_nm) noexcept {
    double a = lo_nm, b = hi_nm;
    const double pa = hyperchromat_psi(a);
    const double pb = hyperchromat_psi(b);
    const bool dec = (pa > pb);
    for (int it = 0; it < 40; ++it) {
        const double mid = 0.5 * (a + b);
        const double pm = hyperchromat_psi(mid);
        if ((dec && pm > target_psi) || (!dec && pm < target_psi)) a = mid;
        else b = mid;
    }
    return 0.5 * (a + b);
}

namespace detail {

// Smooth Wyman analytic CIE 1931 2-deg observer + D65 -> nonnegative wide-gamut RGB weights.
inline std::array<double, 3> spectral_rgb_response(double wl_nm) noexcept {
    auto pw_gauss = [](double x, double mu, double s1, double s2) noexcept {
        const double d = (x - mu) * ((x < mu) ? s1 : s2);
        return std::exp(-0.5 * d * d);
    };
    const double x_bar = 1.056 * pw_gauss(wl_nm, 599.8, 0.0264, 0.0323) +
                         0.362 * pw_gauss(wl_nm, 442.0, 0.0624, 0.0374) -
                         0.065 * pw_gauss(wl_nm, 501.1, 0.0490, 0.0382);
    const double y_bar = 0.821 * pw_gauss(wl_nm, 568.8, 0.0213, 0.0247) +
                         0.286 * pw_gauss(wl_nm, 530.9, 0.0613, 0.0322);
    const double z_bar = 1.217 * pw_gauss(wl_nm, 437.0, 0.0845, 0.0278) +
                         0.681 * pw_gauss(wl_nm, 459.0, 0.0385, 0.0725);
    // Smooth D65 relative spectral power approximation across 410..680 nm
    const double d65 = 0.92 + 0.16 * std::exp(-0.5 * std::pow((wl_nm - 460.0) / 48.0, 2.0)) -
                       0.10 * std::max(0.0, (wl_nm - 580.0) / 100.0);
    const double X = std::max(0.0, x_bar) * d65;
    const double Y = std::max(0.0, y_bar) * d65;
    const double Z = std::max(0.0, z_bar) * d65;
    // Rec.2020 linear matrix clamped to physical nonnegative cone so pure R/G/B vertices lie inside cone
    const double r = std::max(0.0,  1.716651 * X - 0.355671 * Y - 0.253366 * Z);
    const double g = std::max(0.0, -0.666684 * X + 1.616481 * Y + 0.015769 * Z);
    const double b = std::max(0.0,  0.017640 * X - 0.042771 * Y + 0.942103 * Z);
    return {r, g, b};
}

struct SpectralBasisTable {
    int Q = 0;
    float max_step_px = 0.0f;
    std::vector<double> wl_nm;
    std::vector<double> psi;
    std::vector<std::array<double, 3>> t_col; // synthesis column t_q, sum_q t_q == (1,1,1)
    // 7 basis spectra: 0=W, 1=R, 2=G, 3=B, 4=C(0,1,1), 5=M(1,0,1), 6=Y(1,1,0)
    std::array<std::vector<double>, 7> basis;
};

inline SpectralBasisTable build_spectral_table(float max_shift_px) {
    SpectralBasisTable tab;
    const double psi_410 = hyperchromat_psi(410.0);
    const double psi_680 = hyperchromat_psi(680.0);
    const double step_target = 0.58;
    const double amp = std::max(1.0, static_cast<double>(std::fabs(max_shift_px)));
    const int n_blue = std::clamp(static_cast<int>(std::ceil(amp * std::fabs(psi_410) / step_target)), 8, 96);
    const int n_red  = std::clamp(static_cast<int>(std::ceil(amp * std::fabs(psi_680) / step_target)), 4, 48);

    std::vector<double> bounds;
    bounds.reserve(static_cast<size_t>(n_blue + n_red + 1));
    for (int i = 0; i < n_blue; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(n_blue);
        const double p_t = psi_410 * (1.0 - frac);
        bounds.push_back(i == 0 ? 410.0 : invert_hyperchromat_psi(p_t, 410.0, 555.0));
    }
    bounds.push_back(555.0);
    for (int i = 1; i <= n_red; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(n_red);
        const double p_t = psi_680 * frac;
        bounds.push_back(i == n_red ? 680.0 : invert_hyperchromat_psi(p_t, 555.0, 680.0));
    }

    // Include explicit 555.0 nm anchor node plus segment midpoints
    const int n_seg = static_cast<int>(bounds.size()) - 1;
    tab.Q = n_seg + 1;
    tab.wl_nm.resize(static_cast<size_t>(tab.Q));
    tab.psi.resize(static_cast<size_t>(tab.Q));
    tab.t_col.resize(static_cast<size_t>(tab.Q));

    int idx = 0;
    for (int s = 0; s < n_seg; ++s) {
        if (s == n_blue) {
            tab.wl_nm[static_cast<size_t>(idx)] = 555.0;
            tab.psi[static_cast<size_t>(idx)] = 0.0;
            tab.t_col[static_cast<size_t>(idx)] = {0.0, 0.0, 0.0};
            ++idx;
        }
        const double a = bounds[static_cast<size_t>(s)];
        const double b = bounds[static_cast<size_t>(s + 1)];
        const double mid = 0.5 * (a + b);
        const double dw = b - a;
        tab.wl_nm[static_cast<size_t>(idx)] = mid;
        tab.psi[static_cast<size_t>(idx)] = hyperchromat_psi(mid);
        auto resp = spectral_rgb_response(mid);
        tab.t_col[static_cast<size_t>(idx)] = {resp[0] * dw, resp[1] * dw, resp[2] * dw};
        ++idx;
    }
    // Give the explicit 555 nm anchor node a small local quadrature share from adjacent green segments
    const size_t i_anc = static_cast<size_t>(n_blue);
    if (i_anc > 0 && i_anc + 1 < static_cast<size_t>(tab.Q)) {
        for (int c = 0; c < 3; ++c) {
            const double share = 0.25 * (tab.t_col[i_anc - 1][c] + tab.t_col[i_anc + 1][c]);
            tab.t_col[i_anc - 1][c] *= 0.75;
            tab.t_col[i_anc + 1][c] *= 0.75;
            tab.t_col[i_anc][c] = share;
        }
    }

    std::array<double, 3> col_sum = {0.0, 0.0, 0.0};
    for (int q = 0; q < tab.Q; ++q) {
        for (int c = 0; c < 3; ++c) col_sum[c] += tab.t_col[static_cast<size_t>(q)][c];
    }
    for (int q = 0; q < tab.Q; ++q) {
        for (int c = 0; c < 3; ++c) {
            tab.t_col[static_cast<size_t>(q)][c] /= std::max(1e-12, col_sum[c]);
        }
    }

    double max_dp = 0.0;
    for (int q = 0; q + 1 < tab.Q; ++q) {
        max_dp = std::max(max_dp, std::fabs(tab.psi[static_cast<size_t>(q + 1)] - tab.psi[static_cast<size_t>(q)]));
    }
    tab.max_step_px = static_cast<float>(max_dp * static_cast<double>(std::fabs(max_shift_px)));

    // Solve curvature-penalized NNLS: min_{s >= 0} ||T s - target||^2 + mu ||D2 s||^2
    // White basis S_W == 1 identically since sum_q t_q == (1,1,1)!
    const int Q = tab.Q;
    tab.basis[0].assign(static_cast<size_t>(Q), 1.0);

    const std::array<std::array<double, 3>, 6> targets = {{
        {1.0, 0.0, 0.0}, // R
        {0.0, 1.0, 0.0}, // G
        {0.0, 0.0, 1.0}, // B
        {0.0, 1.0, 1.0}, // C
        {1.0, 0.0, 1.0}, // M
        {1.0, 1.0, 0.0}  // Y
    }};

    const double mu = 1e-4 * static_cast<double>(Q);
    // Estimate Lipschitz constant of T^T T + mu D2^T D2
    double t_fro2 = 0.0;
    for (int q = 0; q < Q; ++q) {
        for (int c = 0; c < 3; ++c) t_fro2 += tab.t_col[static_cast<size_t>(q)][c] * tab.t_col[static_cast<size_t>(q)][c];
    }
    const double L = std::max(1e-3, t_fro2 + 16.0 * mu);
    const double step = 0.9 / L;

    for (size_t k = 0; k < 6; ++k) {
        std::vector<double> s(static_cast<size_t>(Q), 0.35), y(static_cast<size_t>(Q), 0.35), s_next(static_cast<size_t>(Q), 0.0);
        double tk = 1.0;
        for (int it = 0; it < 220; ++it) {
            double ts[3] = {0.0, 0.0, 0.0};
            for (int q = 0; q < Q; ++q) {
                for (int c = 0; c < 3; ++c) ts[c] += tab.t_col[static_cast<size_t>(q)][c] * y[static_cast<size_t>(q)];
            }
            const double r0 = ts[0] - targets[k][0];
            const double r1 = ts[1] - targets[k][1];
            const double r2 = ts[2] - targets[k][2];
            for (int q = 0; q < Q; ++q) {
                double g = tab.t_col[static_cast<size_t>(q)][0] * r0 +
                           tab.t_col[static_cast<size_t>(q)][1] * r1 +
                           tab.t_col[static_cast<size_t>(q)][2] * r2;
                // D2^T D2 y interior stencil
                double d2_adj = 0.0;
                if (q >= 2) d2_adj += y[static_cast<size_t>(q - 2)] - 2.0 * y[static_cast<size_t>(q - 1)] + y[static_cast<size_t>(q)];
                if (q >= 1 && q + 1 < Q) d2_adj += -2.0 * (y[static_cast<size_t>(q - 1)] - 2.0 * y[static_cast<size_t>(q)] + y[static_cast<size_t>(q + 1)]);
                if (q + 2 < Q) d2_adj += y[static_cast<size_t>(q)] - 2.0 * y[static_cast<size_t>(q + 1)] + y[static_cast<size_t>(q + 2)];
                g += mu * d2_adj;
                s_next[static_cast<size_t>(q)] = std::max(0.0, y[static_cast<size_t>(q)] - step * g);
            }
            const double tk_next = 0.5 * (1.0 + std::sqrt(1.0 + 4.0 * tk * tk));
            const double beta = (tk - 1.0) / tk_next;
            for (int q = 0; q < Q; ++q) {
                const size_t uq = static_cast<size_t>(q);
                y[uq] = std::max(0.0, s_next[uq] + beta * (s_next[uq] - s[uq]));
                s[uq] = s_next[uq];
            }
            tk = tk_next;
        }
        tab.basis[k + 1] = std::move(s);
    }
    return tab;
}

inline void eval_lovasz_capped_spectrum(
    const SpectralBasisTable& tab, float r, float g, float b, float gate, float* out_s
) noexcept {
    const double rp = std::max(0.0, static_cast<double>(r));
    const double gp = std::max(0.0, static_cast<double>(g));
    const double bp = std::max(0.0, static_cast<double>(b));
    const int Q = tab.Q;
    if (gate <= 1e-6f || (rp <= 1e-12 && gp <= 1e-12 && bp <= 1e-12)) {
        for (int q = 0; q < Q; ++q) out_s[q] = 0.0f;
        return;
    }
    double c_min = rp, c_mid = gp, c_max = bp;
    int pri = 3, sec = 4; // default R <= G <= B -> sec=C(G+B), pri=B
    if (rp >= gp && rp >= bp) {
        c_max = rp; pri = 1;
        if (gp >= bp) { c_mid = gp; c_min = bp; sec = 6; } // Y (R+G)
        else          { c_mid = bp; c_min = gp; sec = 5; } // M (R+B)
    } else if (gp >= rp && gp >= bp) {
        c_max = gp; pri = 2;
        if (rp >= bp) { c_mid = rp; c_min = bp; sec = 6; } // Y (R+G)
        else          { c_mid = bp; c_min = rp; sec = 4; } // C (G+B)
    } else {
        c_max = bp; pri = 3;
        if (rp >= gp) { c_mid = rp; c_min = gp; sec = 5; } // M (R+B)
        else          { c_mid = gp; c_min = rp; sec = 4; } // C (G+B)
    }
    const double w0 = c_min;
    const double w1 = c_mid - c_min;
    const double w2 = c_max - c_mid;
    const auto& b0 = tab.basis[0];
    const auto& b1 = tab.basis[static_cast<size_t>(sec)];
    const auto& b2 = tab.basis[static_cast<size_t>(pri)];

    double rec[3] = {0.0, 0.0, 0.0};
    for (int q = 0; q < Q; ++q) {
        const size_t uq = static_cast<size_t>(q);
        const double sq = w0 * b0[uq] + w1 * b1[uq] + w2 * b2[uq];
        out_s[q] = static_cast<float>(sq);
        rec[0] += tab.t_col[uq][0] * sq;
        rec[1] += tab.t_col[uq][1] * sq;
        rec[2] += tab.t_col[uq][2] * sq;
    }
    // Round-2 Audit OP-O1 channelwise nonnegativity cap: T * s_hat <= c_+ componentwise
    double alpha = static_cast<double>(gate);
    const double cp[3] = {rp, gp, bp};
    for (int c = 0; c < 3; ++c) {
        if (rec[c] > cp[c] + 1e-12 && rec[c] > 1e-12) {
            alpha = std::min(alpha, static_cast<double>(gate) * (cp[c] / rec[c]));
        }
    }
    if (alpha < 0.999999) {
        const float af = static_cast<float>(std::max(0.0, alpha));
        for (int q = 0; q < Q; ++q) out_s[q] *= af;
    }
}

inline float sample_scalar_clamped(const float* plane, int H, int W, float y, float x) noexcept {
    const float cy = std::clamp(y, 0.0f, static_cast<float>(H - 1));
    const float cx = std::clamp(x, 0.0f, static_cast<float>(W - 1));
    const int y0 = std::clamp(static_cast<int>(std::floor(cy)), 0, H - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(cx)), 0, W - 1);
    const int y1 = std::min(H - 1, y0 + 1);
    const int x1 = std::min(W - 1, x0 + 1);
    const float fy = cy - static_cast<float>(y0);
    const float fx = cx - static_cast<float>(x0);
    const float v0 = plane[static_cast<size_t>(y0) * W + x0] * (1.0f - fx) +
                     plane[static_cast<size_t>(y0) * W + x1] * fx;
    const float v1 = plane[static_cast<size_t>(y1) * W + x0] * (1.0f - fx) +
                     plane[static_cast<size_t>(y1) * W + x1] * fx;
    return v0 * (1.0f - fy) + v1 * fy;
}

// Fast 2-pass separable box blur approximating isotropic disc/Gaussian for scalar plane.
inline void box_blur_scalar_2d(const float* src, float* dst, float* scratch, int H, int W, float radius_f) {
    const int rad = std::max(1, static_cast<int>(std::round(radius_f)));
    const float inv = 1.0f / static_cast<float>(2 * rad + 1);
    for (int y = 0; y < H; ++y) {
        double acc = static_cast<double>(src[static_cast<size_t>(y) * W]) * static_cast<double>(rad + 1);
        for (int k = 1; k <= rad; ++k) {
            acc += src[static_cast<size_t>(y) * W + std::min(W - 1, k)];
        }
        for (int x = 0; x < W; ++x) {
            scratch[static_cast<size_t>(y) * W + x] = static_cast<float>(acc) * inv;
            const int x_add = std::min(W - 1, x + rad + 1);
            const int x_rem = std::max(0, x - rad);
            acc += static_cast<double>(src[static_cast<size_t>(y) * W + x_add]) -
                   static_cast<double>(src[static_cast<size_t>(y) * W + x_rem]);
        }
    }
    for (int x = 0; x < W; ++x) {
        double acc = static_cast<double>(scratch[x]) * static_cast<double>(rad + 1);
        for (int k = 1; k <= rad; ++k) {
            acc += scratch[static_cast<size_t>(std::min(H - 1, k)) * W + x];
        }
        for (int y = 0; y < H; ++y) {
            dst[static_cast<size_t>(y) * W + x] = static_cast<float>(acc) * inv;
            const int y_add = std::min(H - 1, y + rad + 1);
            const int y_rem = std::max(0, y - rad);
            acc += static_cast<double>(scratch[static_cast<size_t>(y_add) * W + x]) -
                   static_cast<double>(scratch[static_cast<size_t>(y_rem) * W + x]);
        }
    }
}

// Blind edge-spread defocus radius estimator R_hat(x) on 1/2 grid with SNR confidence diffusion.
inline void estimate_defocus_radius_map(
    const float* rgb, int H, int W, const BokehShapeConfig& cfg,
    std::vector<float>& r_hat_out, std::vector<float>& pure_bg_weight_out
) {
    const size_t npix = static_cast<size_t>(H) * static_cast<size_t>(W);
    r_hat_out.assign(npix, 0.0f);
    pure_bg_weight_out.assign(npix, 1.0f);

    if (cfg.fixed_defocus_px > 0.0f) {
        std::fill(r_hat_out.begin(), r_hat_out.end(), cfg.fixed_defocus_px);
    } else {
        const int hH = std::max(2, (H + 1) / 2);
        const int hW = std::max(2, (W + 1) / 2);
        const size_t hp = static_cast<size_t>(hH) * static_cast<size_t>(hW);
        std::vector<float> lum(hp, 0.0f), b1(hp, 0.0f), b2(hp, 0.0f), scr(hp, 0.0f);
        for (int y = 0; y < hH; ++y) {
            const int sy = std::min(H - 1, 2 * y);
            for (int x = 0; x < hW; ++x) {
                const int sx = std::min(W - 1, 2 * x);
                const float* p = &rgb[(static_cast<size_t>(sy) * W + sx) * 3];
                lum[static_cast<size_t>(y) * hW + x] =
                    std::max(0.0f, 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]);
            }
        }
        box_blur_scalar_2d(lum.data(), b1.data(), scr.data(), hH, hW, 1.0f);
        box_blur_scalar_2d(b1.data(), b2.data(), scr.data(), hH, hW, 3.0f);

        // Estimate gradient ratio ||grad(b2)|| / ||grad(lum)|| -> wider blur keeps higher coarse/fine ratio
        std::vector<float> r_num(hp, 0.0f), r_den(hp, 1e-4f);
        const float sigma_ref = 3.2f;
        for (int y = 1; y + 1 < hH; ++y) {
            for (int x = 1; x + 1 < hW; ++x) {
                const size_t i = static_cast<size_t>(y) * hW + x;
                const float gx0 = 0.5f * (lum[i + 1] - lum[i - 1]);
                const float gy0 = 0.5f * (lum[i + hW] - lum[i - hW]);
                const float g_fine = std::hypot(gx0, gy0);
                const float gx2 = 0.5f * (b2[i + 1] - b2[i - 1]);
                const float gy2 = 0.5f * (b2[i + hW] - b2[i - hW]);
                const float g_coarse = std::hypot(gx2, gy2);
                const float conf = std::clamp((g_fine - 0.004f) * 25.0f, 0.0f, 1.0f);
                if (conf > 1e-3f && g_fine > 1e-5f) {
                    const float ratio = std::clamp(g_coarse / g_fine, 0.05f, 0.985f);
                    // For Gaussian edge sigma_0 vs added sigma_ref: ratio = sigma_0 / sqrt(sigma_0^2 + sigma_ref^2)
                    const float sig0 = sigma_ref * ratio / std::sqrt(std::max(1e-4f, 1.0f - ratio * ratio));
                    const float coc_full = std::clamp(2.0f * 1.8f * sig0, 0.0f, 36.0f);
                    r_num[i] = conf * coc_full;
                    r_den[i] = conf + 1e-4f;
                }
            }
        }
        // Smoothly propagate confident edge blur estimates into neighboring bokeh regions
        std::vector<float> s_num(hp, 0.0f), s_den(hp, 0.0f);
        box_blur_scalar_2d(r_num.data(), s_num.data(), scr.data(), hH, hW, 6.0f);
        box_blur_scalar_2d(r_den.data(), s_den.data(), scr.data(), hH, hW, 6.0f);
        box_blur_scalar_2d(s_num.data(), r_num.data(), scr.data(), hH, hW, 6.0f);
        box_blur_scalar_2d(s_den.data(), r_den.data(), scr.data(), hH, hW, 6.0f);
        for (size_t i = 0; i < hp; ++i) {
            const float evidence = std::clamp((r_den[i] - 1e-4f) * 8.0f, 0.0f, 1.0f);
            b1[i] = evidence * (r_num[i] / std::max(1e-4f, r_den[i]));
        }
        for (int y = 0; y < H; ++y) {
            const float hy = (static_cast<float>(y) + 0.5f) * 0.5f - 0.5f;
            for (int x = 0; x < W; ++x) {
                const float hx = (static_cast<float>(x) + 0.5f) * 0.5f - 0.5f;
                r_hat_out[static_cast<size_t>(y) * W + x] = sample_scalar_clamped(b1.data(), hH, hW, hy, hx);
            }
        }
    }

    // Build foreground protection mask fg(x) in [0,1]
    std::vector<float> fg(npix, 0.0f);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(y) * W + x;
            float f_prot = 0.0f;
            if (cfg.protect_mask) {
                f_prot = std::max(f_prot, std::clamp(cfg.protect_mask[i], 0.0f, 1.0f));
            }
            if (cfg.focus_map_half && cfg.focus_h > 0 && cfg.focus_w > 0) {
                const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(cfg.focus_h) / static_cast<float>(H) - 0.5f;
                const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(cfg.focus_w) / static_cast<float>(W) - 0.5f;
                const float fc = sample_scalar_clamped(cfg.focus_map_half, cfg.focus_h, cfg.focus_w, fy, fx);
                f_prot = std::max(f_prot, std::clamp((fc - 0.65f) * 3.0f, 0.0f, 1.0f));
            }
            if (r_hat_out[i] <= cfg.focus_gate_px) {
                const float gate = std::clamp((cfg.focus_gate_px - r_hat_out[i]) / std::max(0.25f, 0.5f * cfg.focus_gate_px), 0.0f, 1.0f);
                f_prot = std::max(f_prot, gate);
            }
            fg[i] = f_prot;
        }
    }

    // Round-2 Audit OP-O3 anti-bleed rule: erode background support by 2 px around any fg > 0.05
    // so mixed boundary pixels (c=0.5, m=0.5) never inject foreground light into K*(m_bg * c)!
    for (int y = 0; y < H; ++y) {
        const int y0 = std::max(0, y - 2), y1 = std::min(H - 1, y + 2);
        for (int x = 0; x < W; ++x) {
            const int x0 = std::max(0, x - 2), x1 = std::min(W - 1, x + 2);
            float max_fg = 0.0f;
            for (int yy = y0; yy <= y1; ++yy) {
                for (int xx = x0; xx <= x1; ++xx) {
                    max_fg = std::max(max_fg, fg[static_cast<size_t>(yy) * W + xx]);
                }
            }
            // Output pixel uses crisp identity wherever fg[i] >= 0.5, and pure_bg_weight_out is 0 on mixed rim!
            pure_bg_weight_out[static_cast<size_t>(y) * W + x] =
                (max_fg > 0.05f) ? 0.0f : (1.0f - fg[static_cast<size_t>(y) * W + x]);
        }
    }
    // Encode crisp foreground lock as negative sentinel or keep separate:
    for (size_t i = 0; i < npix; ++i) {
        if (fg[i] >= 0.5f) r_hat_out[i] = 0.0f;
    }
}

} // namespace detail

// Apply OP-O3 wide-aperture positive bokeh reshaping in-place on linear RGB.
inline void apply_bokeh_shaping_linear(
    float* rgb, int H, int W, const BokehShapeConfig& cfg,
    HandheldOpticsTelemetry* tel = nullptr,
    const ParallelRows& parallel_rows = {}
) {
    if (!rgb || H < 4 || W < 4 || !cfg.active()) return;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t npix = static_cast<size_t>(H) * static_cast<size_t>(W);

    const float wb_rel[3] = {
        std::max(0.05f, cfg.wb_gain[0]) / std::max(0.05f, cfg.wb_gain[1]),
        1.0f,
        std::max(0.05f, cfg.wb_gain[2]) / std::max(0.05f, cfg.wb_gain[1])
    };
    const bool apply_wb = (std::fabs(wb_rel[0] - 1.0f) > 1e-4f || std::fabs(wb_rel[2] - 1.0f) > 1e-4f);
    if (apply_wb) {
        for (size_t i = 0; i < npix; ++i) {
            rgb[i * 3 + 0] *= wb_rel[0];
            rgb[i * 3 + 2] *= wb_rel[2];
        }
    }

    std::vector<float> r_hat, bg_sup;
    detail::estimate_defocus_radius_map(rgb, H, W, cfg, r_hat, bg_sup);
    const std::vector<float> src(rgb, rgb + npix * 3u);

    const float s_dn = std::clamp(cfg.de_nisen, 0.0f, 1.0f);
    const float k_dn = 0.45f * s_dn; // Locked Round-2 OP-O3 mapping: k(s) = 0.45 * s
    const float swirl_k = std::clamp(cfg.swirl, -1.0f, 1.0f);
    const float aspect_k = std::clamp(cfg.aspect, 1.0f, 2.0f);
    const float anam_mul = (aspect_k > 1.001f) ? (0.42f * std::sqrt(aspect_k * aspect_k - 1.0f)) : 0.0f;
    const float onion_k = std::clamp(cfg.onion_clean, 0.0f, 1.0f);
    const float cx_img = 0.5f * static_cast<float>(W - 1);
    const float cy_img = 0.5f * static_cast<float>(H - 1);
    const float r_corner = std::max(1.0f, std::hypot(cx_img, cy_img));

    const bool need_2d_blur = (k_dn > 1e-4f || anam_mul > 1e-4f || onion_k > 1e-4f);
    const bool need_swirl   = (std::fabs(swirl_k) > 1e-4f);
    std::vector<float> mid_rgb;
    float* stage1_dst = need_swirl ? (mid_rgb.assign(src.begin(), src.end()), mid_rgb.data()) : rgb;

    if (need_2d_blur) {
        const RowTask work_blur = [&](int y0, int y1) {
            std::vector<double> gx, gy;
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    if (cfg.protect_mask && cfg.protect_mask[i] >= 0.5f) continue;
                    const float R_loc = r_hat[i];
                    if (R_loc <= cfg.focus_gate_px) continue;

                    const float blend_gate = (cfg.protect_mask)
                        ? (1.0f - std::clamp(cfg.protect_mask[i], 0.0f, 1.0f))
                        : std::clamp((R_loc - cfg.focus_gate_px) / std::max(0.5f, 0.6f * cfg.focus_gate_px), 0.0f, 1.0f);
                    if (blend_gate <= 1e-4f) continue;

                    const float sig_iso = std::max(k_dn * R_loc, 0.18f * onion_k * R_loc);
                    const float sig_x = sig_iso;
                    const float sig_y = std::hypot(sig_iso, anam_mul * R_loc);
                    if (sig_y < 0.25f && sig_x < 0.25f) continue;

                    const float sx_eff = std::max(0.35f, sig_x);
                    const float sy_eff = std::max(0.35f, sig_y);
                    const int rx = std::min(48, std::max(1, static_cast<int>(std::ceil(3.2f * sx_eff))));
                    const int ry = std::min(48, std::max(1, static_cast<int>(std::ceil(3.2f * sy_eff))));
                    gx.resize(static_cast<size_t>(2 * rx + 1));
                    gy.resize(static_cast<size_t>(2 * ry + 1));
                    const double inv2sx2 = 0.5 / (static_cast<double>(sx_eff) * static_cast<double>(sx_eff));
                    const double inv2sy2 = 0.5 / (static_cast<double>(sy_eff) * static_cast<double>(sy_eff));
                    for (int dx = -rx; dx <= rx; ++dx) gx[static_cast<size_t>(dx + rx)] = std::exp(-dx * dx * inv2sx2);
                    for (int dy = -ry; dy <= ry; ++dy) gy[static_cast<size_t>(dy + ry)] = std::exp(-dy * dy * inv2sy2);

                    double acc_r = 0.0, acc_g = 0.0, acc_b = 0.0, acc_w = 0.0;
                    const int yy0 = std::max(0, y - ry), yy1 = std::min(H - 1, y + ry);
                    const int xx0 = std::max(0, x - rx), xx1 = std::min(W - 1, x + rx);
                    for (int yy = yy0; yy <= yy1; ++yy) {
                        const double wy = gy[static_cast<size_t>(yy - y + ry)];
                        const size_t row_off = static_cast<size_t>(yy) * W;
                        for (int xx = xx0; xx <= xx1; ++xx) {
                            const float m_sup = bg_sup[row_off + xx];
                            if (m_sup <= 1e-5f) continue;
                            const double w = wy * gx[static_cast<size_t>(xx - x + rx)] * static_cast<double>(m_sup);
                            const float* sp = &src[(row_off + xx) * 3];
                            acc_r += w * sp[0];
                            acc_g += w * sp[1];
                            acc_b += w * sp[2];
                            acc_w += w;
                        }
                    }
                    if (acc_w > 0.05) {
                        const float inv_w = static_cast<float>(1.0 / acc_w);
                        stage1_dst[i * 3 + 0] = (1.0f - blend_gate) * src[i * 3 + 0] + blend_gate * static_cast<float>(acc_r) * inv_w;
                        stage1_dst[i * 3 + 1] = (1.0f - blend_gate) * src[i * 3 + 1] + blend_gate * static_cast<float>(acc_g) * inv_w;
                        stage1_dst[i * 3 + 2] = (1.0f - blend_gate) * src[i * 3 + 2] + blend_gate * static_cast<float>(acc_b) * inv_w;
                    }
                }
            }
        };
        if (parallel_rows) parallel_rows(H, work_blur);
        else work_blur(0, H);
    }

    // Chiral logarithmic-spiral + positive skew-coma Petzval swirl kernel:
    // r(phi; s) = r0 * exp(kappa_r * sign(s) * phi), theta(phi) = theta0 + phi.
    if (need_swirl) {
        const float* swirl_in = need_2d_blur ? mid_rgb.data() : src.data();
        const double sgn = (swirl_k >= 0.0f) ? 1.0 : -1.0;
        constexpr double kSpiralPitch = 0.45;
        const RowTask work_swirl = [&](int y0, int y1) {
            auto sample_rgb_clamped = [&](float fy, float fx, float out_c[3]) noexcept {
                const float yc = std::clamp(fy, 0.0f, static_cast<float>(H - 1));
                const float xc = std::clamp(fx, 0.0f, static_cast<float>(W - 1));
                const int iy0 = static_cast<int>(std::floor(yc));
                const int ix0 = static_cast<int>(std::floor(xc));
                const int iy1 = std::min(H - 1, iy0 + 1);
                const int ix1 = std::min(W - 1, ix0 + 1);
                const float wy = yc - static_cast<float>(iy0);
                const float wx = xc - static_cast<float>(ix0);
                const float* p00 = &swirl_in[(static_cast<size_t>(iy0) * W + ix0) * 3];
                const float* p01 = &swirl_in[(static_cast<size_t>(iy0) * W + ix1) * 3];
                const float* p10 = &swirl_in[(static_cast<size_t>(iy1) * W + ix0) * 3];
                const float* p11 = &swirl_in[(static_cast<size_t>(iy1) * W + ix1) * 3];
                for (int c = 0; c < 3; ++c) {
                    const float top = p00[c] * (1.0f - wx) + p01[c] * wx;
                    const float bot = p10[c] * (1.0f - wx) + p11[c] * wx;
                    out_c[c] = top * (1.0f - wy) + bot * wy;
                }
            };
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    rgb[i * 3 + 0] = swirl_in[i * 3 + 0];
                    rgb[i * 3 + 1] = swirl_in[i * 3 + 1];
                    rgb[i * 3 + 2] = swirl_in[i * 3 + 2];
                    if (cfg.protect_mask && cfg.protect_mask[i] >= 0.5f) continue;
                    const float R_loc = r_hat[i];
                    if (R_loc <= cfg.focus_gate_px) continue;

                    const float blend_gate = (cfg.protect_mask)
                        ? (1.0f - std::clamp(cfg.protect_mask[i], 0.0f, 1.0f))
                        : std::clamp((R_loc - cfg.focus_gate_px) / std::max(0.5f, 0.6f * cfg.focus_gate_px), 0.0f, 1.0f);
                    if (blend_gate <= 1e-4f) continue;

                    const double dx_c = static_cast<double>(x) - static_cast<double>(cx_img);
                    const double dy_c = static_cast<double>(y) - static_cast<double>(cy_img);
                    const double rad_c = std::hypot(dx_c, dy_c);
                    if (rad_c <= 2.0) continue;

                    const double arc_half = std::min(28.0, 0.72 * static_cast<double>(std::fabs(swirl_k)) *
                        (rad_c / static_cast<double>(r_corner)) * static_cast<double>(R_loc));
                    if (arc_half < 0.35) continue;

                    const double theta0 = std::atan2(dy_c, dx_c);
                    const double phi_max = std::min(0.65, arc_half / std::max(4.0, rad_c));
                    const int n_tap = std::max(4, static_cast<int>(std::ceil(arc_half * 1.6)));
                    double sr = 0.0, sg = 0.0, sb = 0.0, sw = 0.0;
                    for (int t = -n_tap; t <= n_tap; ++t) {
                        const double u = static_cast<double>(t) / static_cast<double>(n_tap);
                        const double phi = u * phi_max;
                        const double rt = rad_c * std::exp(kSpiralPitch * sgn * phi);
                        const float sx = static_cast<float>(static_cast<double>(cx_img) + rt * std::cos(theta0 + phi));
                        const float sy = static_cast<float>(static_cast<double>(cy_img) + rt * std::sin(theta0 + phi));
                        if (sx < 0.0f || sx > static_cast<float>(W - 1) || sy < 0.0f || sy > static_cast<float>(H - 1)) continue;
                        const float m_sup = detail::sample_scalar_clamped(bg_sup.data(), H, W, sy, sx);
                        if (m_sup <= 1e-5f) continue;
                        const double coma = 1.0 + 0.30 * sgn * std::tanh(1.5 * u);
                        const double wt = std::exp(-0.5 * (u * u) / 0.40) * coma * static_cast<double>(m_sup);
                        float sc[3];
                        sample_rgb_clamped(sy, sx, sc);
                        sr += wt * sc[0];
                        sg += wt * sc[1];
                        sb += wt * sc[2];
                        sw += wt;
                    }
                    if (sw > 0.05) {
                        const float inv_w = static_cast<float>(1.0 / sw);
                        rgb[i * 3 + 0] = (1.0f - blend_gate) * swirl_in[i * 3 + 0] + blend_gate * static_cast<float>(sr) * inv_w;
                        rgb[i * 3 + 1] = (1.0f - blend_gate) * swirl_in[i * 3 + 1] + blend_gate * static_cast<float>(sg) * inv_w;
                        rgb[i * 3 + 2] = (1.0f - blend_gate) * swirl_in[i * 3 + 2] + blend_gate * static_cast<float>(sb) * inv_w;
                    }
                }
            }
        };
        if (parallel_rows) parallel_rows(H, work_swirl);
        else work_swirl(0, H);
    }

    if (apply_wb) {
        const float inv_r = 1.0f / wb_rel[0];
        const float inv_b = 1.0f / wb_rel[2];
        for (size_t i = 0; i < npix; ++i) {
            rgb[i * 3 + 0] *= inv_r;
            rgb[i * 3 + 2] *= inv_b;
        }
    }

    if (tel) {
        const auto t1 = std::chrono::steady_clock::now();
        tel->bokeh_applied = true;
        tel->bokeh_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
}

// Apply OP-O1 continuous spectral CA (lateral or axial-aura) in-place on linear RGB.
inline void apply_spectral_ca_linear(
    float* rgb, int H, int W, const SpectralCaConfig& cfg,
    HandheldOpticsTelemetry* tel = nullptr,
    const ParallelRows& parallel_rows = {}
) {
    if (!rgb || H < 2 || W < 2 || !cfg.active()) return;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t npix = static_cast<size_t>(H) * static_cast<size_t>(W);

    const float wb_rel[3] = {
        std::max(0.05f, cfg.wb_gain[0]) / std::max(0.05f, cfg.wb_gain[1]),
        1.0f,
        std::max(0.05f, cfg.wb_gain[2]) / std::max(0.05f, cfg.wb_gain[1])
    };
    const bool apply_wb = (std::fabs(wb_rel[0] - 1.0f) > 1e-4f || std::fabs(wb_rel[2] - 1.0f) > 1e-4f);
    if (apply_wb) {
        for (size_t i = 0; i < npix; ++i) {
            rgb[i * 3 + 0] *= wb_rel[0];
            rgb[i * 3 + 2] *= wb_rel[2];
        }
    }

    const detail::SpectralBasisTable tab = detail::build_spectral_table(cfg.power_px);
    const int Q = tab.Q;
    const std::vector<float> orig_rgb(rgb, rgb + npix * 3u);

    // Optional low-pass pilot luma for dark-noise SNR gating (OP-O1 Round-2 fix)
    std::vector<float> dark_gate(npix, 1.0f);
    if (cfg.dark_sigma > 1e-6f) {
        std::vector<float> lum(npix, 0.0f), smooth_lum(npix, 0.0f), scr(npix, 0.0f);
        for (size_t i = 0; i < npix; ++i) {
            lum[i] = 0.2126f * orig_rgb[i * 3 + 0] + 0.7152f * orig_rgb[i * 3 + 1] + 0.0722f * orig_rgb[i * 3 + 2];
        }
        detail::box_blur_scalar_2d(lum.data(), smooth_lum.data(), scr.data(), H, W, 2.0f);
        const float inv_s = 1.0f / (2.5f * cfg.dark_sigma);
        for (size_t i = 0; i < npix; ++i) {
            dark_gate[i] = std::clamp((smooth_lum[i] - 0.5f * cfg.dark_sigma) * inv_s, 0.0f, 1.0f);
        }
    }

    // Stream band-by-band so peak memory is O(H * W) instead of O(Q * H * W)!
    std::vector<double> delta_rgb(npix * 3u, 0.0);
    std::vector<float> band_s(npix, 0.0f), band_k(npix, 0.0f), band_scr(npix, 0.0f);

    const float cx_img = 0.5f * static_cast<float>(W - 1);
    const float cy_img = 0.5f * static_cast<float>(H - 1);
    const float r_norm_inv = 1.0f / std::max(1.0f, std::hypot(cx_img, cy_img));
    const float ang_rad = cfg.angle_deg * (std::numbers::pi_v<float> / 180.0f);
    const float dir_x = std::cos(ang_rad);
    const float dir_y = std::sin(ang_rad);

    // Precompute per-pixel spectral weights band-by-band without storing Q full-res planes
    // Actually Q <= 145 and evaluating 3 basis lookups per pixel per band is 3 FMAs;
    // even faster: precompute (w0, w1, w2, sec, pri, alpha_cone) per pixel ONCE (16 bytes/pixel)!
    struct PixLift {
        float w0 = 0.0f, w1 = 0.0f, w2 = 0.0f;
        uint8_t sec = 4, pri = 3;
    };
    std::vector<PixLift> lifts(npix);
    const RowTask prep_lifts = [&](int y0, int y1) {
        std::vector<float> tmp_s(static_cast<size_t>(Q));
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                float r = orig_rgb[i * 3 + 0];
                float g = orig_rgb[i * 3 + 1];
                float b = orig_rgb[i * 3 + 2];
                float gate = dark_gate[i];
                if (cfg.mode == SpectralCaMode::AXIAL_AURA) {
                    const float lum = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
                    const float hi = (lum > cfg.aura_threshold)
                        ? ((lum - cfg.aura_threshold) / std::max(1e-4f, lum))
                        : 0.0f;
                    gate *= hi;
                    if (cfg.emitter_mask) gate *= std::clamp(cfg.emitter_mask[i], 0.0f, 1.0f);
                }
                const double rp = std::max(0.0, static_cast<double>(r));
                const double gp = std::max(0.0, static_cast<double>(g));
                const double bp = std::max(0.0, static_cast<double>(b));
                if (gate <= 1e-6f || (rp <= 1e-12 && gp <= 1e-12 && bp <= 1e-12)) continue;

                double c_min = rp, c_mid = gp, c_max = bp;
                uint8_t pri = 3, sec = 4;
                if (rp >= gp && rp >= bp) {
                    c_max = rp; pri = 1;
                    if (gp >= bp) { c_mid = gp; c_min = bp; sec = 6; }
                    else          { c_mid = bp; c_min = gp; sec = 5; }
                } else if (gp >= rp && gp >= bp) {
                    c_max = gp; pri = 2;
                    if (rp >= bp) { c_mid = rp; c_min = bp; sec = 6; }
                    else          { c_mid = bp; c_min = rp; sec = 4; }
                } else {
                    c_max = bp; pri = 3;
                    if (rp >= gp) { c_mid = rp; c_min = gp; sec = 5; }
                    else          { c_mid = gp; c_min = rp; sec = 4; }
                }
                const double w0 = c_min, w1 = c_mid - c_min, w2 = c_max - c_mid;
                const auto& b0 = tab.basis[0];
                const auto& b1 = tab.basis[sec];
                const auto& b2 = tab.basis[pri];
                double rec[3] = {0.0, 0.0, 0.0};
                for (int q = 0; q < Q; ++q) {
                    const size_t uq = static_cast<size_t>(q);
                    const double sq = w0 * b0[uq] + w1 * b1[uq] + w2 * b2[uq];
                    rec[0] += tab.t_col[uq][0] * sq;
                    rec[1] += tab.t_col[uq][1] * sq;
                    rec[2] += tab.t_col[uq][2] * sq;
                }
                double alpha = static_cast<double>(gate);
                const double cp[3] = {rp, gp, bp};
                for (int c = 0; c < 3; ++c) {
                    if (rec[c] > cp[c] + 1e-12 && rec[c] > 1e-12) {
                        alpha = std::min(alpha, static_cast<double>(gate) * (cp[c] / rec[c]));
                    }
                }
                const float af = static_cast<float>(std::max(0.0, alpha));
                lifts[i] = PixLift{
                    static_cast<float>(w0) * af,
                    static_cast<float>(w1) * af,
                    static_cast<float>(w2) * af,
                    sec, pri
                };
            }
        }
    };
    if (parallel_rows) parallel_rows(H, prep_lifts);
    else prep_lifts(0, H);

    for (int q = 0; q < Q; ++q) {
        const size_t uq = static_cast<size_t>(q);
        const float psi_q = static_cast<float>(tab.psi[uq]);
        if (std::fabs(psi_q * cfg.power_px) <= 1e-5f) continue;

        const double b0_q = tab.basis[0][uq];
        double b_lut[7];
        b_lut[0] = b0_q;
        for (int k = 1; k < 7; ++k) b_lut[k] = tab.basis[static_cast<size_t>(k)][uq];

        const RowTask fill_band = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    const PixLift& L = lifts[i];
                    band_s[i] = static_cast<float>(
                        L.w0 * b0_q + L.w1 * b_lut[L.sec] + L.w2 * b_lut[L.pri]);
                }
            }
        };
        if (parallel_rows) parallel_rows(H, fill_band);
        else fill_band(0, H);

        if (cfg.mode == SpectralCaMode::LATERAL) {
            // Surface-brightness-preserving pullback K_q[s](x) = s(x - dx_q(x))
            const float shift_amp = cfg.power_px * psi_q;
            const RowTask warp_band = [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) {
                    for (int x = 0; x < W; ++x) {
                        float dx = 0.0f, dy = 0.0f;
                        if (cfg.radial) {
                            dx = -shift_amp * (static_cast<float>(x) - cx_img) * r_norm_inv;
                            dy = -shift_amp * (static_cast<float>(y) - cy_img) * r_norm_inv;
                        } else {
                            dx = -shift_amp * dir_x;
                            dy = -shift_amp * dir_y;
                        }
                        const size_t i = static_cast<size_t>(y) * W + x;
                        band_k[i] = detail::sample_scalar_clamped(
                            band_s.data(), H, W, static_cast<float>(y) - dy, static_cast<float>(x) - dx);
                    }
                }
            };
            if (parallel_rows) parallel_rows(H, warp_band);
            else warp_band(0, H);
        } else {
            // AXIAL_AURA: 3-disc positive apodized blur of radius R_q = power_px * |psi_q|
            const float r_q = cfg.power_px * std::fabs(psi_q);
            if (r_q < 0.35f) continue;
            std::vector<float> sub_b(npix, 0.0f);
            std::fill(band_k.begin(), band_k.end(), 0.0f);
            const float alphas[3] = {0.28f, 0.58f, 1.00f};
            const float weights[3] = {0.220f, 0.406f, 0.374f};
            for (int m = 0; m < 3; ++m) {
                const float rm = std::max(0.5f, alphas[m] * r_q * 0.55f);
                // Two iterated box passes give smooth positive circularly-tapered support with unit DC
                detail::box_blur_scalar_2d(band_s.data(), sub_b.data(), band_scr.data(), H, W, rm);
                detail::box_blur_scalar_2d(sub_b.data(), sub_b.data(), band_scr.data(), H, W, rm);
                const float wm = weights[m] * cfg.aura_mix;
                for (size_t i = 0; i < npix; ++i) band_k[i] += wm * sub_b[i];
            }
            // Scale subtracted local term by aura_mix so flat fields conserve exact color (K_q[1] - 1 == 0)
            const float mix = cfg.aura_mix;
            for (size_t i = 0; i < npix; ++i) band_s[i] *= mix;
        }

        const double tr = tab.t_col[uq][0];
        const double tg = tab.t_col[uq][1];
        const double tb = tab.t_col[uq][2];
        const RowTask accum_delta = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    const double diff = static_cast<double>(band_k[i]) - static_cast<double>(band_s[i]);
                    delta_rgb[i * 3 + 0] += tr * diff;
                    delta_rgb[i * 3 + 1] += tg * diff;
                    delta_rgb[i * 3 + 2] += tb * diff;
                }
            }
        };
        if (parallel_rows) parallel_rows(H, accum_delta);
        else accum_delta(0, H);
    }

    for (size_t i = 0; i < npix; ++i) {
        const float keep = cfg.protect_mask ? std::clamp(cfg.protect_mask[i], 0.0f, 1.0f) : 0.0f;
        const float w_eff = 1.0f - keep;
        if (w_eff <= 0.0f) continue;
        rgb[i * 3 + 0] = static_cast<float>(static_cast<double>(orig_rgb[i * 3 + 0]) + w_eff * delta_rgb[i * 3 + 0]);
        rgb[i * 3 + 1] = static_cast<float>(static_cast<double>(orig_rgb[i * 3 + 1]) + w_eff * delta_rgb[i * 3 + 1]);
        rgb[i * 3 + 2] = static_cast<float>(static_cast<double>(orig_rgb[i * 3 + 2]) + w_eff * delta_rgb[i * 3 + 2]);
    }

    if (apply_wb) {
        const float inv_r = 1.0f / wb_rel[0];
        const float inv_b = 1.0f / wb_rel[2];
        for (size_t i = 0; i < npix; ++i) {
            rgb[i * 3 + 0] *= inv_r;
            rgb[i * 3 + 2] *= inv_b;
        }
    }

    if (tel) {
        const auto t1 = std::chrono::steady_clock::now();
        tel->spectral_applied = true;
        tel->spectral_nodes = Q;
        tel->max_step_px = tab.max_step_px;
        tel->spectral_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
}

} // namespace heraw_hoptics
