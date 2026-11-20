// heraw_suite_diffusion.hpp -- Unified Physical Optical Scattering Engine:
//   - Ricoh HDF (Near-Field Phase Diffraction Bloom)
//   - Black Pro-Mist (Near-Field Bloom + Carbon Dark Contrast Retention)
//   - Tiffen Ultra Contrast 1/8..5 (Wide-Angle Ambient Veiling, Zero Local Halo)
//   - Film Halation (Sub-Emulsion Red/Orange Back-Scatter)
//   - Tandem Cascade T_hal o T_near o T_uc (R2), 1/64 Exact Full-Width Gaussian (Cross-Section #4),
//     Exact Fixed-Scene Cross-Covariance Propagation (R1), and EXR ScatterField Delta Output.
// Pure 7-bit ASCII C++20 header. Zero external dependencies.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace heraw_diff {

enum class DiffusionMode : uint8_t {
    OFF = 0,
    HDF = 1,
    BLACK_MIST = 2,
    ULTRA_CONTRAST = 3,
    HALATION = 4,
    UC_HDF = 5,
    UC_MIST = 6,
    UC_HALATION = 7
};

enum class UcBaffleMode : uint8_t {
    MATTE_BOX = 0,
    HOOD = 1,
    UNBAFFLED = 2
};

inline DiffusionMode parse_diffusion_mode(const std::string& s) {
    if (s == "hdf") return DiffusionMode::HDF;
    if (s == "black-mist" || s == "blackmist") return DiffusionMode::BLACK_MIST;
    if (s == "ultra-contrast" || s == "ultracon" || s == "uc") return DiffusionMode::ULTRA_CONTRAST;
    if (s == "halation") return DiffusionMode::HALATION;
    if (s == "uc+hdf") return DiffusionMode::UC_HDF;
    if (s == "uc+mist" || s == "uc+black-mist") return DiffusionMode::UC_MIST;
    if (s == "uc+halation") return DiffusionMode::UC_HALATION;
    return DiffusionMode::OFF;
}

inline const char* diffusion_mode_str(DiffusionMode m) noexcept {
    switch (m) {
        case DiffusionMode::HDF: return "hdf";
        case DiffusionMode::BLACK_MIST: return "black-mist";
        case DiffusionMode::ULTRA_CONTRAST: return "ultra-contrast";
        case DiffusionMode::HALATION: return "halation";
        case DiffusionMode::UC_HDF: return "uc+hdf";
        case DiffusionMode::UC_MIST: return "uc+mist";
        case DiffusionMode::UC_HALATION: return "uc+halation";
        default: return "off";
    }
}

inline UcBaffleMode parse_baffle_mode(const std::string& s) {
    if (s == "hooded" || s == "matte-box") return UcBaffleMode::MATTE_BOX;
    if (s == "wide-flare" || s == "unbaffled" || s == "bare") return UcBaffleMode::UNBAFFLED;
    return UcBaffleMode::HOOD; // "standard" or "hood"
}

inline const char* baffle_mode_str(UcBaffleMode b) noexcept {
    switch (b) {
        case UcBaffleMode::HOOD: return "hood";
        case UcBaffleMode::UNBAFFLED: return "unbaffled";
        default: return "matte-box";
    }
}

inline float uc_grade_eta(const std::string& grade) noexcept {
    if (grade == "1/8") return 0.008f;
    if (grade == "1/4") return 0.015f;
    if (grade == "1/2") return 0.026f;
    if (grade == "1")   return 0.042f;
    if (grade == "2")   return 0.065f;
    if (grade == "3")   return 0.090f;
    if (grade == "4")   return 0.118f;
    if (grade == "5")   return 0.150f;
    return 0.065f; // default #2
}

struct DiffusionConfig {
    DiffusionMode mode = DiffusionMode::OFF;
    float strength = 1.0f;          // scales active eta/effect in [0..1.5]
    std::string uc_grade = "1";     // 1/8, 1/4, 1/2, 1, 2, 3, 4, 5
    UcBaffleMode uc_baffle = UcBaffleMode::HOOD;
    float uc_chroma = 0.0f;         // 0.0 = neutral/ambient scatter, >0 = warm optical glass ambient tint
    float uc_outlier_guard_mul = 16.0f; // R1 renamed rendering outlier guard (16 x P95)
    std::array<float, 3> wb_gain = {1.0f, 1.0f, 1.0f}; // white-balance gains when input rgb is camera-native RAW
    bool active() const noexcept { return mode != DiffusionMode::OFF && strength > 1e-4f; }
};

struct DiffusionTelemetry {
    bool applied = false;
    double wall_ms = 0.0;
    float eff_eta_uc = 0.0f;
    float eff_eta_near = 0.0f;
    float eff_sigma_wide_px = 0.0f;
    float ambient_rgb[3] = {0.0f, 0.0f, 0.0f};
    float p95_luma = 0.0f;
    float outlier_guard_limit = 0.0f;
    size_t image_bytes = 0;
};

// R1 Exact Output Noise Variance Propagation under Fixed Scene, Optics & Guiding Kernel:
//   z_p = (1 - eta) * x_p + eta * a_p,  where a_p = k_p^T x.
//   Var(z_p) = (1 - eta)^2 Var(x_p) + eta^2 Var(a_p) + 2*eta*(1 - eta) * Cov(x_p, a_p).
inline double eval_ultracon_output_variance(
    double var_x_p,
    double var_a_p,
    double cov_x_a_p,
    double eta
) noexcept {
    const double om = 1.0 - eta;
    return std::max(0.0, om * om * var_x_p + eta * eta * var_a_p + 2.0 * eta * om * cov_x_a_p);
}

// Conservative computable variance bound when only local variance var_x_p, ambient variance var_a_p,
// correlation length L_c or total count P are available (R1).
inline double eval_ultracon_variance_bound_cs(double var_x_p, double var_a_p, double eta) noexcept {
    const double cov_cs = std::sqrt(std::max(0.0, var_x_p) * std::max(0.0, var_a_p));
    return eval_ultracon_output_variance(var_x_p, var_a_p, cov_cs, eta);
}

namespace detail {

// Separable full-width Gaussian 1D convolution with symmetric boundary extension (preserves exact DC & 2nd moment).
inline void separable_gauss_rgb_full(
    std::vector<float>& rgb, int H, int W, float sigma, bool match_border_dc,
    const float border_val[3], float border_mix
) {
    if (H <= 0 || W <= 0 || sigma < 0.25f) return;
    const int rad = std::min(std::max(H, W) * 2, std::max(3, static_cast<int>(std::ceil(3.5f * sigma))));
    std::vector<double> kern(static_cast<size_t>(2 * rad + 1), 0.0);
    const double inv2s2 = 1.0 / (2.0 * static_cast<double>(sigma) * static_cast<double>(sigma));
    double ksum = 0.0;
    for (int k = -rad; k <= rad; ++k) {
        const double v = std::exp(-static_cast<double>(k * k) * inv2s2);
        kern[static_cast<size_t>(k + rad)] = v;
        ksum += v;
    }
    const double inv_ks = 1.0 / std::max(1e-18, ksum);
    for (double& kv : kern) kv *= inv_ks;

    std::vector<float> tmp(rgb.size(), 0.0f);
    // Horizontal pass
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            double s[3] = {0.0, 0.0, 0.0};
            for (int k = -rad; k <= rad; ++k) {
                const int xx = x + k;
                const double w = kern[static_cast<size_t>(k + rad)];
                if (xx >= 0 && xx < W) {
                    const float* p = &rgb[(static_cast<size_t>(y) * W + xx) * 3];
                    s[0] += w * p[0]; s[1] += w * p[1]; s[2] += w * p[2];
                } else {
                    const int cx = std::clamp(xx, 0, W - 1);
                    const float* p = &rgb[(static_cast<size_t>(y) * W + cx) * 3];
                    if (match_border_dc && border_mix > 0.0f) {
                        for (int c = 0; c < 3; ++c) s[c] += w * ((1.0 - border_mix) * p[c] + border_mix * border_val[c]);
                    } else {
                        s[0] += w * p[0]; s[1] += w * p[1]; s[2] += w * p[2];
                    }
                }
            }
            float* d = &tmp[(static_cast<size_t>(y) * W + x) * 3];
            d[0] = static_cast<float>(s[0]);
            d[1] = static_cast<float>(s[1]);
            d[2] = static_cast<float>(s[2]);
        }
    }
    // Vertical pass
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            double s[3] = {0.0, 0.0, 0.0};
            for (int k = -rad; k <= rad; ++k) {
                const int yy = y + k;
                const double w = kern[static_cast<size_t>(k + rad)];
                if (yy >= 0 && yy < H) {
                    const float* p = &tmp[(static_cast<size_t>(yy) * W + x) * 3];
                    s[0] += w * p[0]; s[1] += w * p[1]; s[2] += w * p[2];
                } else {
                    const int cy = std::clamp(yy, 0, H - 1);
                    const float* p = &tmp[(static_cast<size_t>(cy) * W + x) * 3];
                    if (match_border_dc && yy < 0 && border_mix > 0.0f) {
                        // Top out-of-FOV upper hemisphere sky dome contribution (--uc-baffle hood/unbaffled)
                        for (int c = 0; c < 3; ++c) s[c] += w * ((1.0 - border_mix) * p[c] + border_mix * border_val[c]);
                    } else {
                        s[0] += w * p[0]; s[1] += w * p[1]; s[2] += w * p[2];
                    }
                }
            }
            float* d = &rgb[(static_cast<size_t>(y) * W + x) * 3];
            d[0] = static_cast<float>(s[0]);
            d[1] = static_cast<float>(s[1]);
            d[2] = static_cast<float>(s[2]);
        }
    }
}

inline void downsample2x_rgb(const std::vector<float>& src, int H, int W,
                             std::vector<float>& dst, int& dH, int& dW) {
    dH = std::max(1, (H + 1) / 2);
    dW = std::max(1, (W + 1) / 2);
    dst.assign(static_cast<size_t>(dH) * dW * 3, 0.0f);
    for (int y = 0; y < dH; ++y) {
        const int y0 = std::min(H - 1, 2 * y);
        const int y1 = std::min(H - 1, 2 * y + 1);
        for (int x = 0; x < dW; ++x) {
            const int x0 = std::min(W - 1, 2 * x);
            const int x1 = std::min(W - 1, 2 * x + 1);
            const float* p00 = &src[(static_cast<size_t>(y0) * W + x0) * 3];
            const float* p01 = &src[(static_cast<size_t>(y0) * W + x1) * 3];
            const float* p10 = &src[(static_cast<size_t>(y1) * W + x0) * 3];
            const float* p11 = &src[(static_cast<size_t>(y1) * W + x1) * 3];
            float* d = &dst[(static_cast<size_t>(y) * dW + x) * 3];
            for (int c = 0; c < 3; ++c) d[c] = 0.25f * (p00[c] + p01[c] + p10[c] + p11[c]);
        }
    }
}

inline void sample_bilinear_rgb(const std::vector<float>& src, int sH, int sW,
                                float y, float x, float out[3]) noexcept {
    const float cy = std::clamp(y, 0.0f, static_cast<float>(sH - 1));
    const float cx = std::clamp(x, 0.0f, static_cast<float>(sW - 1));
    const int y0 = std::clamp(static_cast<int>(std::floor(cy)), 0, sH - 1);
    const int x0 = std::clamp(static_cast<int>(std::floor(cx)), 0, sW - 1);
    const int y1 = std::min(sH - 1, y0 + 1);
    const int x1 = std::min(sW - 1, x0 + 1);
    const float fy = cy - static_cast<float>(y0);
    const float fx = cx - static_cast<float>(x0);
    const float* p00 = &src[(static_cast<size_t>(y0) * sW + x0) * 3];
    const float* p01 = &src[(static_cast<size_t>(y0) * sW + x1) * 3];
    const float* p10 = &src[(static_cast<size_t>(y1) * sW + x0) * 3];
    const float* p11 = &src[(static_cast<size_t>(y1) * sW + x1) * 3];
    for (int c = 0; c < 3; ++c) {
        const float r0 = p00[c] + fx * (p01[c] - p00[c]);
        const float r1 = p10[c] + fx * (p11[c] - p10[c]);
        out[c] = r0 + fy * (r1 - r0);
    }
}

} // namespace detail

// Apply physical linear-domain scattering filter in-place (before tone mapping / highlight rolloff)
// and optionally write the signed ScatterField delta to *scatter_delta_out for EXR Part 4.
inline DiffusionTelemetry apply_optical_diffusion_linear(
    float* rgb, int H, int W,
    const DiffusionConfig& cfg,
    std::vector<float>* scatter_delta_out = nullptr
) {
    DiffusionTelemetry tel;
    if (!rgb || H < 2 || W < 2 || !cfg.active()) return tel;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t n_pix = static_cast<size_t>(H) * static_cast<size_t>(W);
    tel.image_bytes = n_pix * 3u * sizeof(float);

    std::vector<float> orig_copy;
    if (scatter_delta_out) {
        orig_copy.assign(rgb, rgb + n_pix * 3u);
    }

    const float wb_rel[3] = {
        std::max(0.05f, cfg.wb_gain[0]) / std::max(0.05f, cfg.wb_gain[1]),
        1.0f,
        std::max(0.05f, cfg.wb_gain[2]) / std::max(0.05f, cfg.wb_gain[1])
    };
    const bool apply_wb_norm =
        (std::fabs(wb_rel[0] - 1.0f) > 1e-4f || std::fabs(wb_rel[2] - 1.0f) > 1e-4f);
    if (apply_wb_norm) {
        for (size_t i = 0; i < n_pix; ++i) {
            rgb[i * 3 + 0] *= wb_rel[0];
            rgb[i * 3 + 2] *= wb_rel[2];
        }
    }

    const bool do_uc = (cfg.mode == DiffusionMode::ULTRA_CONTRAST ||
                        cfg.mode == DiffusionMode::UC_HDF ||
                        cfg.mode == DiffusionMode::UC_MIST ||
                        cfg.mode == DiffusionMode::UC_HALATION ||
                        cfg.mode == DiffusionMode::BLACK_MIST);
    const bool do_hdf = (cfg.mode == DiffusionMode::HDF || cfg.mode == DiffusionMode::UC_HDF);
    const bool do_mist = (cfg.mode == DiffusionMode::BLACK_MIST || cfg.mode == DiffusionMode::UC_MIST);
    const bool do_hal = (cfg.mode == DiffusionMode::HALATION || cfg.mode == DiffusionMode::UC_HALATION);

    // Stage 1: Ultra Contrast (T_U = I + u (U - I)) FIRST in cascade (R2: T_hal o T_near o T_uc)
    if (do_uc) {
        float u = (cfg.mode == DiffusionMode::BLACK_MIST)
            ? std::clamp(0.012f * (uc_grade_eta(cfg.uc_grade) / 0.042f), 0.004f, 0.024f)
            : uc_grade_eta(cfg.uc_grade);
        u = std::clamp(u * cfg.strength, 0.0f, 0.35f);
        tel.eff_eta_uc = u;
        if (u > 1e-5f) {
            // Compute P95 luma & top-band sky edge mean for outlier rendering guard (R1) + baffle
            std::vector<float> lums;
            const size_t stride = std::max<size_t>(1u, n_pix / 8192u);
            lums.reserve(n_pix / stride + 8);
            for (size_t i = 0; i < n_pix; i += stride) {
                const float yv = 0.2126f * rgb[i * 3 + 0] + 0.7152f * rgb[i * 3 + 1] + 0.0722f * rgb[i * 3 + 2];
                if (std::isfinite(yv)) lums.push_back(std::max(0.0f, yv));
            }
            float p95 = 0.18f;
            if (!lums.empty()) {
                size_t k95 = static_cast<size_t>(std::floor(0.95 * static_cast<double>(lums.size() - 1)));
                std::nth_element(lums.begin(), lums.begin() + k95, lums.end());
                p95 = std::max(0.02f, lums[k95]);
            }
            tel.p95_luma = p95;
            const float guard_lim = std::max(1.0f, cfg.uc_outlier_guard_mul * p95);
            tel.outlier_guard_limit = guard_lim;

            // Build guarded ambient input field
            std::vector<float> amb(n_pix * 3u);
            double sum_rgb[3] = {0.0, 0.0, 0.0};
            double top_rgb[3] = {0.0, 0.0, 0.0};
            size_t top_cnt = 0;
            const int top_rows = std::max(1, H / 10);
            for (int y = 0; y < H; ++y) {
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    const float r = std::max(0.0f, rgb[i * 3 + 0]);
                    const float g = std::max(0.0f, rgb[i * 3 + 1]);
                    const float b = std::max(0.0f, rgb[i * 3 + 2]);
                    const float pk = std::max(r, std::max(g, b));
                    const float sc = (pk > guard_lim) ? (guard_lim / pk) : 1.0f;
                    const float ar = r * sc, ag = g * sc, ab = b * sc;
                    amb[i * 3 + 0] = ar; amb[i * 3 + 1] = ag; amb[i * 3 + 2] = ab;
                    sum_rgb[0] += ar; sum_rgb[1] += ag; sum_rgb[2] += ab;
                    if (y < top_rows) { top_rgb[0] += ar; top_rgb[1] += ag; top_rgb[2] += ab; ++top_cnt; }
                }
            }
            float avg_amb[3] = {
                static_cast<float>(sum_rgb[0] / static_cast<double>(n_pix)),
                static_cast<float>(sum_rgb[1] / static_cast<double>(n_pix)),
                static_cast<float>(sum_rgb[2] / static_cast<double>(n_pix))
            };
            float sky_edge[3] = {
                static_cast<float>(top_rgb[0] / std::max<size_t>(1u, top_cnt)),
                static_cast<float>(top_rgb[1] / std::max<size_t>(1u, top_cnt)),
                static_cast<float>(top_rgb[2] / std::max<size_t>(1u, top_cnt))
            };
            for (int c = 0; c < 3; ++c) tel.ambient_rgb[c] = avg_amb[c];

            const float diag_px = std::hypot(static_cast<float>(H), static_cast<float>(W));
            const float sigma_wide = std::max(4.0f, 0.45f * diag_px);
            tel.eff_sigma_wide_px = sigma_wide;
            const float bmix = (cfg.uc_baffle == UcBaffleMode::UNBAFFLED) ? 0.70f :
                               (cfg.uc_baffle == UcBaffleMode::HOOD ? 0.35f : 0.0f);

            // Exact downsampling pyramid down to 1/64 (or smaller when tiny image) + EXACT full-width Gaussian (Cross-Section #4)
            std::vector<float> cur = std::move(amb), nxt;
            int cH = H, cW = W;
            int scale_div = 1;
            for (int lv = 0; lv < 6 && std::min(cH, cW) >= 4; ++lv) {
                int nH = 0, nW = 0;
                detail::downsample2x_rgb(cur, cH, cW, nxt, nH, nW);
                cur.swap(nxt);
                cH = nH; cW = nW; scale_div *= 2;
            }
            const float sigma_coarse = std::max(0.5f, sigma_wide / static_cast<float>(scale_div));
            detail::separable_gauss_rgb_full(cur, cH, cW, sigma_coarse, true, sky_edge, bmix);

            const float rho_dc = 0.65f;
            const float om_rho = 1.0f - rho_dc;
            const float chroma_k = std::clamp(cfg.uc_chroma, 0.0f, 1.0f);
            for (int y = 0; y < H; ++y) {
                const float sy = (static_cast<float>(y) + 0.5f) / static_cast<float>(scale_div) - 0.5f;
                for (int x = 0; x < W; ++x) {
                    const float sx = (static_cast<float>(x) + 0.5f) / static_cast<float>(scale_div) - 0.5f;
                    float local_wide[3] = {0.0f, 0.0f, 0.0f};
                    detail::sample_bilinear_rgb(cur, cH, cW, sy, sx, local_wide);
                    const size_t i = static_cast<size_t>(y) * W + x;
                    constexpr float kWarmTint[3] = {1.06f, 1.00f, 0.90f};
                    for (int c = 0; c < 3; ++c) {
                        const float e_c = om_rho * local_wide[c] + rho_dc * avg_amb[c];
                        const float fill_c = e_c * ((1.0f - chroma_k) + chroma_k * kWarmTint[c]);
                        rgb[i * 3 + c] = (1.0f - u) * rgb[i * 3 + c] + u * fill_c;
                    }
                }
            }
        }
    }

    // Stage 2: Near-Field Phase / Carbon Bloom T_N = I + n (N - I) (HDF / Black Pro-Mist)
    if (do_hdf || do_mist) {
        const float eta_near = std::clamp((do_hdf ? 0.14f : 0.085f) * cfg.strength, 0.0f, 0.45f);
        tel.eff_eta_near = eta_near;
        if (eta_near > 1e-5f) {
            const float diag_norm = std::max(0.25f, std::hypot(static_cast<float>(H), static_cast<float>(W)) / 4000.0f);
            const std::array<float, 4> sigmas = do_hdf
                ? std::array<float, 4>{3.0f * diag_norm, 12.0f * diag_norm, 38.0f * diag_norm, 110.0f * diag_norm}
                : std::array<float, 4>{2.2f * diag_norm, 8.0f * diag_norm, 24.0f * diag_norm, 68.0f * diag_norm};
            const std::array<float, 4> weights = {0.34f, 0.30f, 0.22f, 0.14f};

            // Flux-conserving highlight-weighted normalized near scatter field
            std::vector<float> near_src(rgb, rgb + n_pix * 3u);
            std::vector<float> near_acc(n_pix * 3u, 0.0f);
            const float dummy_b[3] = {0.0f, 0.0f, 0.0f};
            for (int lv = 0; lv < 4; ++lv) {
                std::vector<float> cur = near_src, nxt;
                int cH = H, cW = W;
                int div = 1;
                const int down_steps = (lv == 0) ? 0 : (lv == 1 ? 1 : (lv == 2 ? 2 : 4));
                for (int s = 0; s < down_steps && std::min(cH, cW) >= 4; ++s) {
                    int nH = 0, nW = 0;
                    detail::downsample2x_rgb(cur, cH, cW, nxt, nH, nW);
                    cur.swap(nxt); cH = nH; cW = nW; div *= 2;
                }
                detail::separable_gauss_rgb_full(cur, cH, cW, std::max(0.6f, sigmas[lv] / div), false, dummy_b, 0.0f);
                const float w_lv = weights[lv];
                for (int y = 0; y < H; ++y) {
                    const float sy = (div == 1) ? static_cast<float>(y) : ((static_cast<float>(y) + 0.5f) / div - 0.5f);
                    for (int x = 0; x < W; ++x) {
                        const float sx = (div == 1) ? static_cast<float>(x) : ((static_cast<float>(x) + 0.5f) / div - 0.5f);
                        float v[3];
                        if (div == 1) {
                            const float* p = &cur[(static_cast<size_t>(y) * W + x) * 3];
                            v[0] = p[0]; v[1] = p[1]; v[2] = p[2];
                        } else {
                            detail::sample_bilinear_rgb(cur, cH, cW, sy, sx, v);
                        }
                        const size_t i = static_cast<size_t>(y) * W + x;
                        for (int c = 0; c < 3; ++c) near_acc[i * 3 + c] += w_lv * v[c];
                    }
                }
            }
            for (size_t i = 0; i < n_pix * 3u; ++i) {
                rgb[i] = (1.0f - eta_near) * rgb[i] + eta_near * near_acc[i];
            }
        }
    }

    // Stage 3: Film Emulsion Back-Scatter Halation AFTER glass optics (R2)
    if (do_hal) {
        const float eta_hal = std::clamp(0.18f * cfg.strength, 0.0f, 0.45f);
        std::vector<float> exc(n_pix * 3u, 0.0f);
        for (size_t i = 0; i < n_pix; ++i) {
            const float r = rgb[i * 3 + 0], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
            const float lum = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
            const float ov = std::max(0.0f, lum - 0.75f);
            exc[i * 3 + 0] = ov * 0.82f; // red back-scatter dominant
            exc[i * 3 + 1] = ov * 0.18f; // minor green bleed
            exc[i * 3 + 2] = 0.0f;
        }
        int dH = 0, dW = 0;
        std::vector<float> d_exc;
        detail::downsample2x_rgb(exc, H, W, d_exc, dH, dW);
        const float diag_norm = std::max(0.25f, std::hypot(static_cast<float>(H), static_cast<float>(W)) / 4000.0f);
        const float dummy_b[3] = {0.0f, 0.0f, 0.0f};
        detail::separable_gauss_rgb_full(d_exc, dH, dW, std::max(1.0f, 4.5f * diag_norm), false, dummy_b, 0.0f);
        for (int y = 0; y < H; ++y) {
            const float sy = (static_cast<float>(y) + 0.5f) * 0.5f - 0.5f;
            for (int x = 0; x < W; ++x) {
                const float sx = (static_cast<float>(x) + 0.5f) * 0.5f - 0.5f;
                float hv[3];
                detail::sample_bilinear_rgb(d_exc, dH, dW, sy, sx, hv);
                const size_t i = static_cast<size_t>(y) * W + x;
                rgb[i * 3 + 0] += eta_hal * hv[0];
                rgb[i * 3 + 1] += eta_hal * hv[1];
            }
        }
    }

    if (apply_wb_norm) {
        const float inv_r = 1.0f / wb_rel[0];
        const float inv_b = 1.0f / wb_rel[2];
        for (size_t i = 0; i < n_pix; ++i) {
            rgb[i * 3 + 0] *= inv_r;
            rgb[i * 3 + 2] *= inv_b;
        }
    }

    if (scatter_delta_out) {
        scatter_delta_out->resize(n_pix * 3u);
        for (size_t i = 0; i < n_pix * 3u; ++i) (*scatter_delta_out)[i] = rgb[i] - orig_copy[i];
    }

    const auto t1 = std::chrono::steady_clock::now();
    tel.wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    tel.applied = true;
    return tel;
}

} // namespace heraw_diff
