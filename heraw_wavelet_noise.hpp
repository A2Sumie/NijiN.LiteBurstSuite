// heraw_wavelet_noise.hpp -- Wavelet-lossy RAW noise calibration, NLT variance propagation,
// deadzone-immune PG estimation, and subband/Walsh spectrum helpers. Pure 7-bit ASCII.
#pragma once

#include "heraw_gpu_backend.hpp"
#include "heraw_wvdc_router.hpp"

#ifdef HERAW_HAS_WVDC
#include "nijinWVDC.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace heraw_wvn {

inline float wvn_clamp_f(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float wvn_percentile(std::vector<float>& vals, float q) {
    if (vals.empty()) return 0.0f;
    float qc = wvn_clamp_f(q, 0.0f, 1.0f);
    size_t idx = static_cast<size_t>(std::lround(double(qc) * double(vals.size() - 1u)));
    if (idx >= vals.size()) idx = vals.size() - 1u;
    std::nth_element(vals.begin(), vals.begin() + static_cast<ptrdiff_t>(idx), vals.end());
    return vals[idx];
}

// Acklam inverse standard normal CDF for p in (0, 1).
inline double wvn_norm_cdf_inv(double p) {
    if (p <= 0.5) return 0.0;
    if (p >= 0.999999) return 4.753424;
    static constexpr double a1 = -3.969683028665376e+01;
    static constexpr double a2 =  2.209460984245205e+02;
    static constexpr double a3 = -2.759285104469687e+02;
    static constexpr double a4 =  1.383577518672690e+02;
    static constexpr double a5 = -3.066479806614716e+01;
    static constexpr double a6 =  2.506628277459239e+00;

    static constexpr double b1 = -5.447609879822406e+01;
    static constexpr double b2 =  1.615858368580409e+02;
    static constexpr double b3 = -1.556989798598866e+02;
    static constexpr double b4 =  6.680131188771972e+01;
    static constexpr double b5 = -1.328068155288572e+01;

    static constexpr double c1 = -7.784894002430293e-03;
    static constexpr double c2 = -3.223964580411365e-01;
    static constexpr double c3 = -2.400758277161838e+00;
    static constexpr double c4 = -2.549732539343734e+00;
    static constexpr double c5 =  4.374664141464968e+00;
    static constexpr double c6 =  2.938163982698783e+00;

    static constexpr double d1 =  7.784695709041462e-03;
    static constexpr double d2 =  3.224671290700398e-01;
    static constexpr double d3 =  2.445134137142996e+00;
    static constexpr double d4 =  3.754408661907416e+00;

    constexpr double p_high = 1.0 - 0.02425;
    if (p <= p_high) {
        double q = p - 0.5;
        double r = q * q;
        return (((((a1 * r + a2) * r + a3) * r + a4) * r + a5) * r + a6) * q /
               (((((b1 * r + b2) * r + b3) * r + b4) * r + b5) * r + 1.0);
    }
    double q = std::sqrt(-2.0 * std::log(1.0 - p));
    return -(((((c1 * q + c2) * q + c3) * q + c4) * q + c5) * q + c6) /
            ((((d1 * q + d2) * q + d3) * q + d4) * q + 1.0);
}

inline bool wvn_solve_2x2(const double M[2][2], const double rhs[2], double out[2]) {
    double det = M[0][0] * M[1][1] - M[0][1] * M[1][0];
    if (!std::isfinite(det) || std::fabs(det) < 1e-18) return false;
    out[0] = (rhs[0] * M[1][1] - rhs[1] * M[0][1]) / det;
    out[1] = (M[0][0] * rhs[1] - M[1][0] * rhs[0]) / det;
    return std::isfinite(out[0]) && std::isfinite(out[1]);
}

inline bool wvn_solve_3x3(double A[3][3], double b[3], double out[3]) {
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        double max_v = std::fabs(A[col][col]);
        for (int r = col + 1; r < 3; ++r) {
            if (std::fabs(A[r][col]) > max_v) {
                max_v = std::fabs(A[r][col]);
                piv = r;
            }
        }
        if (max_v < 1e-18) return false;
        if (piv != col) {
            for (int c = col; c < 3; ++c) std::swap(A[col][c], A[piv][c]);
            std::swap(b[col], b[piv]);
        }
        double inv_p = 1.0 / A[col][col];
        for (int r = col + 1; r < 3; ++r) {
            double f = A[r][col] * inv_p;
            for (int c = col; c < 3; ++c) A[r][c] -= f * A[col][c];
            b[r] -= f * b[col];
        }
    }
    for (int r = 2; r >= 0; --r) {
        double s = b[r];
        for (int c = r + 1; c < 3; ++c) s -= A[r][c] * out[c];
        out[r] = s / A[r][r];
        if (!std::isfinite(out[r])) return false;
    }
    return true;
}

struct BurstWaveletContext {
    bool active = false;
    bool from_wvdc = false;
    bool is_arw6 = false;
    int H1 = 0, W1 = 0, Hh = 0, Wh = 0;

    float mean_q_luma_code_var = 0.0f;
    float mean_q_chroma_code_var = 0.0f;
    float mean_q_luma_code_std = 0.0f;
    float mean_q_chroma_code_std = 0.0f;

    float q_nlt_s0 = 1.0f;
    float q_nlt_s1 = 0.0f;
    float q_nlt_s2 = 0.0f;
    float nlt_slope_lut[256];

    float luma_scale_psd[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float chroma_scale_psd[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float walsh16_luma_norm[16];

    float half_q_factor = 0.85f;
    float fine_blur_q_factor = 0.35f;
    float wide_blur_q_factor = 0.12f;

    std::vector<float> qvar_luma_code_row;
    std::vector<float> qvar_chroma_code_row;
    std::vector<float> qrel_luma_half;

    BurstWaveletContext() {
        std::fill(std::begin(nlt_slope_lut), std::end(nlt_slope_lut), 1.0f);
        std::fill(std::begin(walsh16_luma_norm), std::end(walsh16_luma_norm), 1.0f);
    }

    void update_blur_factors() {
        half_q_factor = std::max(0.05f, 1.0f - 0.5f * luma_scale_psd[5]);
        fine_blur_q_factor = std::max(0.01f,
            0.92f * luma_scale_psd[0] + 0.78f * luma_scale_psd[1] +
            0.52f * luma_scale_psd[2] + 0.25f * luma_scale_psd[3] +
            0.12f * luma_scale_psd[4] + 0.06f * luma_scale_psd[5]);
        wide_blur_q_factor = std::max(0.005f,
            0.72f * luma_scale_psd[0] + 0.42f * luma_scale_psd[1] +
            0.16f * luma_scale_psd[2] + 0.05f * luma_scale_psd[3] +
            0.02f * luma_scale_psd[4] + 0.01f * luma_scale_psd[5]);
    }

    void fit_nlt_quadratic() {
        double A[3][3] = {};
        double rhs[3] = {};
        double A2[2][2] = {};
        double rhs2[2] = {};
        double y_min = 1e30, y_mid = 0.0;
        for (int k = 0; k < 256; ++k) {
            double x = double(k) / 255.0;
            double y = double(nlt_slope_lut[k]) * double(nlt_slope_lut[k]);
            if (y < y_min) y_min = y;
            if (k == 128) y_mid = y;
            double w = (x <= 0.88) ? 1.0 : 0.25;
            double p[3] = {1.0, x, x * x};
            for (int r = 0; r < 3; ++r) {
                rhs[r] += w * p[r] * y;
                for (int c = 0; c < 3; ++c) A[r][c] += w * p[r] * p[c];
            }
            for (int r = 0; r < 2; ++r) {
                rhs2[r] += w * p[r] * y;
                for (int c = 0; c < 2; ++c) A2[r][c] += w * p[r] * p[c];
            }
        }
        double coef3[3] = {1.0, 0.0, 0.0};
        auto poly_nonneg_01 = [](float c0, float c1, float c2) -> bool {
            if (c0 < 0.0f || (c0 + c1 + c2) < 0.0f) return false;
            if (c2 > 1e-8f) {
                float xv = -c1 / (2.0f * c2);
                if (xv >= 0.0f && xv <= 1.0f && (c0 + xv * (c1 + xv * c2)) < 0.0f) return false;
            }
            for (int k = 1; k < 32; ++k) {
                float x = float(k) / 32.0f;
                if ((c0 + x * (c1 + x * c2)) < 0.0f) return false;
            }
            return true;
        };
        if (wvn_solve_3x3(A, rhs, coef3)) {
            float c0 = std::max(0.0f, float(coef3[0]));
            float c1 = float(coef3[1]);
            float c2 = float(coef3[2]);
            if (poly_nonneg_01(c0, c1, c2)) {
                q_nlt_s0 = c0; q_nlt_s1 = c1; q_nlt_s2 = c2;
                return;
            }
        }
        double coef2[2] = {1.0, 0.0};
        if (wvn_solve_2x2(A2, rhs2, coef2)) {
            float a0 = std::max(0.0f, float(coef2[0]));
            float a1 = std::max(-a0, float(coef2[1]));
            if (poly_nonneg_01(a0, a1, 0.0f)) {
                q_nlt_s0 = a0; q_nlt_s1 = a1; q_nlt_s2 = 0.0f;
                return;
            }
        }
        q_nlt_s0 = std::max(0.0f, float(y_min));
        q_nlt_s1 = std::max(0.0f, 2.0f * float(y_mid - y_min));
        q_nlt_s2 = 0.0f;
    }

    template <typename UFrame>
    void init_from_unpacked_frames(
        const std::vector<UFrame>& uframes,
        const std::vector<int>& light_indices,
        const float self_black_delta[4],
        int h1,
        int w1) {
        H1 = h1;
        W1 = w1;
        Hh = h1 / 2;
        Wh = w1 / 2;
        active = false;
        from_wvdc = false;
        is_arw6 = false;
#ifdef HERAW_HAS_WVDC
        if (H1 <= 0 || W1 <= 0) return;
        std::vector<double> row_lvar(static_cast<size_t>(H1), 0.0);
        std::vector<double> row_cvar(static_cast<size_t>(H1), 0.0);
        double nlt_acc[256] = {};
        double lpsd_acc[6] = {};
        double cpsd_acc[6] = {};
        double w16_acc[16] = {};
        int n_act = 0;
        int n_arw6 = 0;

        for (int idx : light_indices) {
            const auto& uf = uframes[idx];
            if (!uf.wvdc_quant.available) continue;
            nijinwvdc::WaveletQuantProfile qp = uf.wvdc_quant;
            float blk_avg = 0.25f * ((uf.black_c[0] + self_black_delta[0]) +
                                     (uf.black_c[1] + self_black_delta[1]) +
                                     (uf.black_c[2] + self_black_delta[2]) +
                                     (uf.black_c[3] + self_black_delta[3]));
            nijinwvdc::recalibrate_quant_profile_range(qp, blk_avg, uf.white);
            if (qp.codec == nijinwvdc::CodecFamily::Arw6) ++n_arw6;

            const uint32_t rstep = std::max<uint32_t>(1u, qp.row_step);
            const size_t n_ql = qp.q_luma_code_row.size();
            const size_t n_qc = qp.q_chroma_code_row.size();
            for (int y = 0; y < H1; ++y) {
                size_t rb = static_cast<size_t>(y) / rstep;
                float ql = (n_ql > 0u) ? qp.q_luma_code_row[std::min(rb, n_ql - 1u)] : qp.mean_luma_code_std;
                float qc = (n_qc > 0u) ? qp.q_chroma_code_row[std::min(rb, n_qc - 1u)] : qp.mean_chroma_code_std;
                row_lvar[static_cast<size_t>(y)] += double(ql) * double(ql);
                row_cvar[static_cast<size_t>(y)] += double(qc) * double(qc);
            }
            for (int k = 0; k < 256; ++k) nlt_acc[k] += double(qp.nlt_slope_lut[k]);
            for (int k = 0; k < 6; ++k) {
                lpsd_acc[k] += double(qp.luma_scale_psd[k]);
                cpsd_acc[k] += double(qp.chroma_scale_psd[k]);
            }
            for (int k = 0; k < 16; ++k) w16_acc[k] += double(qp.walsh16_luma_norm[k]);
            ++n_act;
        }
        if (n_act <= 0) return;

        active = true;
        from_wvdc = true;
        is_arw6 = (n_arw6 > 0);
        const double inv_n = 1.0 / double(n_act);

        qvar_luma_code_row.resize(static_cast<size_t>(H1));
        qvar_chroma_code_row.resize(static_cast<size_t>(H1));
        double sum_lv = 0.0, sum_cv = 0.0;
        for (int y = 0; y < H1; ++y) {
            float lv = float(row_lvar[static_cast<size_t>(y)] * inv_n);
            float cv = float(row_cvar[static_cast<size_t>(y)] * inv_n);
            qvar_luma_code_row[static_cast<size_t>(y)] = lv;
            qvar_chroma_code_row[static_cast<size_t>(y)] = cv;
            sum_lv += lv;
            sum_cv += cv;
        }
        mean_q_luma_code_var = float(sum_lv / double(H1));
        mean_q_chroma_code_var = float(sum_cv / double(H1));
        mean_q_luma_code_std = std::sqrt(std::max(0.0f, mean_q_luma_code_var));
        mean_q_chroma_code_std = std::sqrt(std::max(0.0f, mean_q_chroma_code_var));

        qrel_luma_half.resize(static_cast<size_t>(std::max(1, Hh)), 1.0f);
        const float denom = std::max(1e-6f, mean_q_luma_code_std);
        for (int rh = 0; rh < Hh; ++rh) {
            float v0 = qvar_luma_code_row[static_cast<size_t>(2 * rh)];
            float v1 = qvar_luma_code_row[static_cast<size_t>(std::min(H1 - 1, 2 * rh + 1))];
            float std_r = std::sqrt(std::max(0.0f, 0.5f * (v0 + v1)));
            qrel_luma_half[static_cast<size_t>(rh)] = wvn_clamp_f(std_r / denom, 0.75f, 1.85f);
        }

        for (int k = 0; k < 256; ++k) nlt_slope_lut[k] = float(nlt_acc[k] * inv_n);
        for (int k = 0; k < 6; ++k) {
            luma_scale_psd[k] = float(lpsd_acc[k] * inv_n);
            chroma_scale_psd[k] = float(cpsd_acc[k] * inv_n);
        }
        for (int k = 0; k < 16; ++k) walsh16_luma_norm[k] = float(w16_acc[k] * inv_n);
        update_blur_factors();
        fit_nlt_quadratic();
#else
        (void)uframes;
        (void)light_indices;
        (void)self_black_delta;
#endif
    }

    bool maybe_activate_blind_dng(
        const std::string& anchor_path,
        const float* g_half0,
        const float* g0_smooth,
        const std::vector<float>& bin_edges,
        int hh,
        int wh,
        float anchor_white = 65535.0f) {
        if (active) return false;
        if (anchor_white < 60000.0f) return false;
        if (heraw_wvdc::lower_ext_of(anchor_path) != ".dng") return false;
        if (!g_half0 || !g0_smooth || bin_edges.size() < 6u || hh < 16 || wh < 16) return false;

        std::array<std::vector<float>, 4> b_lag1, b_lag2;
        for (int r = 2; r + 3 < hh; r += 2) {
            const float* r0 = g_half0 + static_cast<size_t>(r) * wh;
            const float* r1 = g_half0 + static_cast<size_t>(r + 1) * wh;
            const float* r2 = g_half0 + static_cast<size_t>(r + 2) * wh;
            const float* gs = g0_smooth + static_cast<size_t>(r) * wh;
            for (int c = 2; c + 3 < wh; c += 2) {
                float iv = gs[c];
                float h1 = std::fabs(0.5f * (r0[c] - r0[c + 1] - r1[c] + r1[c + 1]));
                float h2 = std::fabs(0.5f * (r0[c] - r0[c + 2] - r2[c] + r2[c + 2]));
                for (int b = 1; b <= 4; ++b) {
                    if (iv >= bin_edges[b] && iv <= bin_edges[b + 1] + 1e-6f) {
                        b_lag1[b - 1].push_back(h1);
                        b_lag2[b - 1].push_back(h2);
                    }
                }
            }
        }
        int hit_bins = 0;
        double var_acc = 0.0;
        for (int bi = 0; bi < 4; ++bi) {
            if (b_lag1[bi].size() < 128u || b_lag2[bi].size() < 128u) continue;
            size_t n_zero1 = 0;
            for (float v : b_lag1[bi]) {
                if (v <= 1e-6f) ++n_zero1;
            }
            if (float(n_zero1) / float(b_lag1[bi].size()) < 0.05f) continue;
            float s1 = wvn_percentile(b_lag1[bi], 0.20f) / 0.25334710f;
            float s2 = wvn_percentile(b_lag2[bi], 0.20f) / 0.25334710f;
            if (s2 > 1.32f * std::max(1e-6f, s1)) {
                ++hit_bins;
                var_acc += std::max(0.0, double(s2) * double(s2) - double(s1) * double(s1));
            }
        }
        if (hit_bins < 3) return false;

        active = true;
        from_wvdc = false;
        is_arw6 = false;
        H1 = hh * 2; W1 = wh * 2; Hh = hh; Wh = wh;
        mean_q_luma_code_var = float(var_acc / double(hit_bins));
        mean_q_chroma_code_var = 2.0f * mean_q_luma_code_var;
        mean_q_luma_code_std = std::sqrt(std::max(0.0f, mean_q_luma_code_var));
        mean_q_chroma_code_std = std::sqrt(std::max(0.0f, mean_q_chroma_code_var));
        q_nlt_s0 = 1.0f; q_nlt_s1 = 0.0f; q_nlt_s2 = 0.0f;
        std::fill(std::begin(nlt_slope_lut), std::end(nlt_slope_lut), 1.0f);
        std::fill(std::begin(walsh16_luma_norm), std::end(walsh16_luma_norm), 1.0f);
        const float def_psd[6] = {0.10f, 0.25f, 0.30f, 0.20f, 0.10f, 0.05f};
        for (int k = 0; k < 6; ++k) {
            luma_scale_psd[k] = def_psd[k];
            chroma_scale_psd[k] = def_psd[k];
        }
        update_blur_factors();
        qvar_luma_code_row.assign(static_cast<size_t>(std::max(1, H1)), mean_q_luma_code_var);
        qvar_chroma_code_row.assign(static_cast<size_t>(std::max(1, H1)), mean_q_chroma_code_var);
        qrel_luma_half.assign(static_cast<size_t>(std::max(1, Hh)), 1.0f);
        return true;
    }

    float eval_nlt_slope(float iv) const {
        if (!active) return 1.0f;
        float x = wvn_clamp_f(iv, 0.0f, 1.0f) * 255.0f;
        int i0 = std::max(0, std::min(254, static_cast<int>(x)));
        float f = x - float(i0);
        return (1.0f - f) * nlt_slope_lut[i0] + f * nlt_slope_lut[i0 + 1];
    }

    float eval_nlt_sq_poly(float iv) const {
        if (!active) return 1.0f;
        return heraw_gpu::gpu_eval_nlt_sq(q_nlt_s0, q_nlt_s1, q_nlt_s2, iv);
    }

    float eval_plane_qstd(int y1, float iv, bool is_chroma) const {
        if (!active || H1 <= 0) return 0.0f;
        int yc = std::max(0, std::min(H1 - 1, y1));
        float qv = is_chroma ? qvar_chroma_code_row[static_cast<size_t>(yc)]
                             : qvar_luma_code_row[static_cast<size_t>(yc)];
        return std::sqrt(std::max(0.0f, qv)) * eval_nlt_slope(iv);
    }

    float eval_half_qvar(int rh, float iv) const {
        if (!active || H1 <= 0) return 0.0f;
        int y1 = std::max(0, std::min(H1 - 1, 2 * rh));
        return half_q_factor * qvar_luma_code_row[static_cast<size_t>(y1)] * eval_nlt_sq_poly(iv);
    }

    float eval_half_qvar_fine(int rh, float iv) const {
        if (!active || H1 <= 0) return 0.0f;
        int y1 = std::max(0, std::min(H1 - 1, 2 * rh));
        return fine_blur_q_factor * qvar_luma_code_row[static_cast<size_t>(y1)] * eval_nlt_sq_poly(iv);
    }

    float eval_half_qvar_wide(int rh, float iv) const {
        if (!active || H1 <= 0) return 0.0f;
        int y1 = std::max(0, std::min(H1 - 1, 2 * rh));
        return wide_blur_q_factor * qvar_luma_code_row[static_cast<size_t>(y1)] * eval_nlt_sq_poly(iv);
    }

    double eval_td_equiv_1px_qvar(float iv) const {
        if (!active) return 0.0;
        return 2.0 * double(half_q_factor) * double(mean_q_luma_code_var) * double(eval_nlt_sq_poly(iv));
    }

    float precinct_boost_half(int rh) const {
        if (!active || rh < 0 || rh >= Hh || qrel_luma_half.empty()) return 1.0f;
        return qrel_luma_half[static_cast<size_t>(rh)];
    }

    double physical_shot_floor(float iso) const {
        double eff_iso = double(std::max(64.0f, (iso > 0.0f) ? iso : 64.0f));
        return eff_iso / (64.0 * 90000.0);
    }

    template <typename ParFn>
    bool estimate_sensor_pg(
        const float* g_half0,
        const float* g0_smooth,
        const std::vector<float>& bin_edges,
        int hh,
        int wh,
        float iso,
        int n_threads,
        ParFn&& par_rows,
        double& out_sig0_sq,
        double& out_gain) const {
        out_sig0_sq = 1e-6;
        out_gain = std::max(physical_shot_floor(iso), 1e-4);
        if (!g_half0 || !g0_smooth || hh < 16 || wh < 16 || bin_edges.size() < 13u) return false;

        const int Hq = hh / 2;
        const int Wq = wh / 2;
        std::vector<float> g_qtr(static_cast<size_t>(Hq) * static_cast<size_t>(Wq));
        par_rows(Hq, n_threads, [&](int r0, int r1, int) {
            for (int rq = r0; rq < r1; ++rq) {
                const float* p0 = g_half0 + static_cast<size_t>(2 * rq) * wh;
                const float* p1 = p0 + wh;
                float* dst = g_qtr.data() + static_cast<size_t>(rq) * Wq;
                for (int cq = 0; cq < Wq; ++cq) {
                    const int c2 = 2 * cq;
                    dst[cq] = 0.25f * (p0[c2] + p0[c2 + 1] + p1[c2] + p1[c2 + 1]);
                }
            }
        }, 16);

        const int q_step = (Hq >= 64 && Wq >= 64) ? 2 : 1;
        const int n_qrows = (Hq - 1 + q_step - 1) / q_step;
        constexpr int NBINS = 12;
        constexpr int K_STRIPS = 64;
        const int n_strips = std::max(1, std::min(K_STRIPS, n_qrows));
        struct BinSlice {
            std::array<std::vector<float>, NBINS> bins;
            std::array<double, NBINS> sums{};
        };
        std::vector<BinSlice> slices(static_cast<size_t>(n_strips));
        par_rows(n_strips, n_threads, [&](int s0, int s1, int) {
            for (int si = s0; si < s1; ++si) {
                const int qi0 = (si * n_qrows) / n_strips;
                const int qi1 = ((si + 1) * n_qrows) / n_strips;
                auto& sl = slices[static_cast<size_t>(si)];
                for (int qi = qi0; qi < qi1; ++qi) {
                    const int rq = qi * q_step;
                    if (rq + 1 >= Hq) break;
                    const float* q0 = g_qtr.data() + static_cast<size_t>(rq) * Wq;
                    const float* q1 = q0 + Wq;
                    const float* gs = g0_smooth + static_cast<size_t>(2 * rq) * wh;
                    for (int cq = 0; cq + 1 < Wq; cq += q_step) {
                        const float iv = gs[2 * cq];
                        const double iv_pos = std::max(0.0f, iv);
                        const float hh_q = std::fabs(0.5f * (q0[cq] - q0[cq + 1] - q1[cq] + q1[cq + 1]));
                        for (int b = 0; b < NBINS; ++b) {
                            if (iv >= bin_edges[b] && iv <= bin_edges[b + 1] + 1e-6f) {
                                sl.bins[b].push_back(hh_q);
                                sl.sums[b] += iv_pos;
                            }
                        }
                    }
                }
            }
        }, 1);

        struct BinPt { double mean_i, v_1px, q_1px; };
        std::vector<BinPt> pts;
        pts.reserve(NBINS);
        const double c_qtr = 8.0 * (double(walsh16_luma_norm[0]) / 16.0) *
                             (0.35 * double(luma_scale_psd[1]) +
                              0.15 * double(luma_scale_psd[2]) +
                              0.05 * double(luma_scale_psd[3]));

        for (int b = 0; b < NBINS; ++b) {
            size_t tot = 0;
            double isum = 0.0;
            for (int si = 0; si < n_strips; ++si) {
                tot += slices[static_cast<size_t>(si)].bins[b].size();
                isum += slices[static_cast<size_t>(si)].sums[b];
            }
            if (tot < 32u) continue;
            std::vector<float> bin;
            bin.reserve(tot);
            size_t n_zero = 0;
            for (int si = 0; si < n_strips; ++si) {
                for (float v : slices[static_cast<size_t>(si)].bins[b]) {
                    if (v <= 1e-6f) ++n_zero;
                    bin.push_back(v);
                }
            }
            float f_zero = float(n_zero) / float(tot);
            if (f_zero >= 0.92f) continue;

            float q_use = (f_zero < 0.05f) ? 0.18f : std::min(0.65f, f_zero + 0.22f * (1.0f - f_zero));
            float p_val = wvn_percentile(bin, q_use);
            double z_quant = std::max(0.05, wvn_norm_cdf_inv(0.5 + 0.5 * double(q_use)));
            double s_qtr = double(p_val) / z_quant;
            double mean_i = isum / double(tot);
            double q_1px = double(mean_q_luma_code_var) * double(eval_nlt_sq_poly(float(mean_i)));
            double v_1px = std::max(0.0, 8.0 * s_qtr * s_qtr - c_qtr * q_1px);
            pts.push_back(BinPt{mean_i, v_1px, q_1px});
        }

        const double g_floor = physical_shot_floor(iso);
        if (pts.size() < 3u) {
            if (!pts.empty()) {
                double m_v = 0.0;
                for (const auto& pt : pts) m_v += pt.v_1px;
                m_v /= double(pts.size());
                out_sig0_sq = std::max(1e-8, 0.5 * m_v);
                out_gain = std::max(g_floor, 0.05 * std::sqrt(out_sig0_sq));
                return true;
            }
            return false;
        }

        double coef[2] = {1e-6, g_floor};
        for (int it = 0; it < 3; ++it) {
            double M[2][2] = {};
            double rhs[2] = {};
            for (const auto& pt : pts) {
                double v_pred = (it == 0) ? pt.v_1px : std::max(1e-8, coef[0] + coef[1] * pt.mean_i);
                double denom = v_pred + pt.q_1px + 1e-6;
                double w = 1.0 / denom;
                if (it > 0) {
                    double resid = std::fabs(pt.v_1px - v_pred);
                    double hscale = std::max(1e-6, 0.5 * (v_pred + pt.q_1px));
                    if (resid > hscale) w *= (hscale / resid);
                }
                M[0][0] += w;
                M[0][1] += w * pt.mean_i;
                M[1][1] += w * pt.mean_i * pt.mean_i;
                rhs[0] += w * pt.v_1px;
                rhs[1] += w * pt.mean_i * pt.v_1px;
            }
            M[1][0] = M[0][1];
            double next_c[2] = {};
            if (!wvn_solve_2x2(M, rhs, next_c)) break;
            coef[0] = next_c[0];
            coef[1] = next_c[1];
        }
        out_sig0_sq = std::max(1e-8, coef[0]);
        out_gain = std::max(g_floor, coef[1]);
        return true;
    }

    void fill_gpu_job_wavelet_params(
        heraw_gpu::Stage45GpuJob& gjob,
        double pg_sig0_sq,
        double pg_gain,
        float mean_motion) const {
        if (!active) return;
        gjob.q_luma_var = mean_q_luma_code_var;
        gjob.q_chroma_var = mean_q_chroma_code_var;
        gjob.q_nlt_s0 = q_nlt_s0;
        gjob.q_nlt_s1 = q_nlt_s1;
        gjob.q_nlt_s2 = q_nlt_s2;
        gjob.q_corr_gain = (gjob.N > 1 && mean_motion < 0.08f) ? 0.18f : 0.0f;
        gjob.qvar_luma_row = qvar_luma_code_row.empty() ? nullptr : qvar_luma_code_row.data();
        gjob.qvar_chroma_row = qvar_chroma_code_row.empty() ? nullptr : qvar_chroma_code_row.data();
        gjob.nlt_slope_lut256 = nlt_slope_lut;
        gjob.walsh16_norm = walsh16_luma_norm;
        gjob.qvar_rows = static_cast<int>(qvar_luma_code_row.size());

        const float i_mid = 0.18f;
        const float q_mid = mean_q_luma_code_var * eval_nlt_sq_poly(i_mid);
        const float pg_mid = float(pg_sig0_sq + pg_gain * double(i_mid));
        const float fq = q_mid / (pg_mid + q_mid + 1e-10f);

        const float w_raw[5] = {
            wvn_clamp_f(std::sqrt(0.75f + 1.2f * (luma_scale_psd[4] + luma_scale_psd[5])), 0.85f, 1.25f),
            wvn_clamp_f(std::sqrt(0.80f + 1.8f * (luma_scale_psd[3] + 0.5f * luma_scale_psd[4])), 0.90f, 1.40f),
            wvn_clamp_f(std::sqrt(0.85f + 2.4f * (luma_scale_psd[2] + 0.5f * luma_scale_psd[3])), 0.95f, 1.55f),
            wvn_clamp_f(std::sqrt(0.90f + 2.8f * (luma_scale_psd[1] + 0.5f * luma_scale_psd[2])), 0.95f, 1.65f),
            wvn_clamp_f(std::sqrt(0.95f + 3.0f * (luma_scale_psd[0] + 0.5f * luma_scale_psd[1])), 0.95f, 1.70f)
        };
        for (int s = 0; s < 5; ++s) {
            gjob.dct_scale_mul[s] = 1.0f + fq * (w_raw[s] - 1.0f);
        }
    }

    void modulate_walsh_kernel_power(
        std::array<float, 16>& kp,
        double pg_sig0_sq,
        double pg_gain) const {
        if (!active) return;
        const float i_mid = 0.18f;
        const float q_mid = mean_q_luma_code_var * eval_nlt_sq_poly(i_mid);
        const float pg_mid = float(pg_sig0_sq + pg_gain * double(i_mid));
        const float fq = q_mid / (pg_mid + q_mid + 1e-10f);

        // Permute sequency-ordered walsh16_luma_norm into natural WHT-4 order expected by heraw_core.
        static constexpr int seq_for_nat[4] = {0, 3, 1, 2};
        float w_nat[16] = {};
        for (int nr = 0; nr < 4; ++nr) {
            for (int nc = 0; nc < 4; ++nc) {
                w_nat[nr * 4 + nc] = walsh16_luma_norm[seq_for_nat[nr] * 4 + seq_for_nat[nc]];
            }
        }
        kp[0] = 1.0f;
        const float w0 = std::max(0.25f, w_nat[0]);
        for (int f = 1; f < 16; ++f) {
            float ratio = w_nat[f] / w0;
            float mul = (1.0f - fq) + fq * ratio;
            kp[f] = wvn_clamp_f(kp[f] * mul, 0.02f, 2.5f);
        }
    }
};

} // namespace heraw_wvn
