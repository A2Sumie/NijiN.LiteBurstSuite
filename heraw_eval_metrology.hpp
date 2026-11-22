#pragma once

#include "heraw_fs_utf8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace heraw_eval {

struct EvalOpticsParams {
    float psf_sigma_1x = 0.0f;
    float lca_r_k1 = 0.0f, lca_r_k2 = 0.0f;
    float lca_b_k1 = 0.0f, lca_b_k2 = 0.0f;
    float ois_r_dy = 0.0f, ois_r_dx = 0.0f;
    float ois_b_dy = 0.0f, ois_b_dx = 0.0f;
    int n_threads = 1;
};

struct StratifiedChi2Report {
    double chi2_all = 0.0;
    double chi2_flat = 0.0;
    double chi2_tex = 0.0;
    double chi2_edge = 0.0;
    size_t n_flat = 0;
    size_t n_tex = 0;
    size_t n_edge = 0;
    double bias_norm = 0.0;
    bool valid = false;
};

struct FrcCurveReport {
    static constexpr int NUM_BINS = 32;
    double freq_nyq[NUM_BINS] = {};
    double frc[NUM_BINS] = {};
    double counts[NUM_BINS] = {};
    double frc_50_nyq = 0.0;
    double frc_143_nyq = 0.0;
    double frc_area = 0.0;
    double frc_half_band_mean = 0.0; // mean in [0.25, 0.75] Nyquist
    int tiles_used = 0;
    bool valid = false;
};

struct PhaseDiversityReport {
    double det_norm = 0.0;    // determinant of normalized 5x5 harmonic info matrix^(1/5)
    double min_eig_norm = 0.0;
    double cond_number = 0.0;
    size_t samples = 0;
    bool valid = false;
};

struct EsfrMtfReport {
    double edge_angle_deg = 0.0;
    double mtf50_cy_px = 0.0;
    double mtf30_cy_px = 0.0;
    double mtf_nyq_at_05 = 0.0; // MTF at 0.5 cy/px
    double overshoot_pct = 0.0;
    double undershoot_pct = 0.0;
    bool valid = false;
};

struct DeadLeavesReport {
    double ratio_low_band = 0.0;  // [0.10, 0.30] cy/px
    double ratio_mid_band = 0.0;  // [0.30, 0.60] cy/px
    double ratio_high_band = 0.0; // [0.60, 0.90] cy/px
    double ratio_overall = 0.0;
    bool valid = false;
};

struct ShadowBiasReport {
    double mean_signed_rel_bias = 0.0; // mean((out - true) / max(eps, true))
    double rmse = 0.0;
    double slope = 0.0;
    double intercept = 0.0;
    size_t samples = 0;
    bool valid = false;
};

struct SelfEvalReport {
    StratifiedChi2Report loo_chi2;
    StratifiedChi2Report fullstack_self_chi2;
    FrcCurveReport half_burst_frc;
    PhaseDiversityReport phase_div;
    EsfrMtfReport esfr;
    double residual_noise_sfm = 0.0;
    double aliasing_chroma_ratio = 0.0;
    std::string mode_label;
    int scale_factor = 1;
    int frame_count = 0;
};

struct IndependentLooEvalInput {
    const float* loo_pred_rgb = nullptr;
    const float* held_out_raw = nullptr;
    const float* held_fy_h = nullptr;
    const float* held_fx_h = nullptr;
    const float* held_mw_h = nullptr;
    float exp_scale = 1.0f;
    float sat_limit = 0.78f;
    int n_train = 0;
    EvalOpticsParams optics{};
};

namespace detail {

inline float sample_bilinear_clamped(const float* img, int H, int W, int ch, int c, float y, float x) {
    float cy = std::min(std::max(y, 0.0f), (float)(H - 1));
    float cx = std::min(std::max(x, 0.0f), (float)(W - 1));
    int y0 = (int)std::floor(cy);
    int x0 = (int)std::floor(cx);
    int y1 = std::min(y0 + 1, H - 1);
    int x1 = std::min(x0 + 1, W - 1);
    float fy = cy - (float)y0;
    float fx = cx - (float)x0;
    float v00 = img[((size_t)y0 * W + x0) * ch + c];
    float v01 = img[((size_t)y0 * W + x1) * ch + c];
    float v10 = img[((size_t)y1 * W + x0) * ch + c];
    float v11 = img[((size_t)y1 * W + x1) * ch + c];
    return (1.0f - fy) * ((1.0f - fx) * v00 + fx * v01) +
           fy * ((1.0f - fx) * v10 + fx * v11);
}

inline float sample_bicubic_clamped(const float* img, int H, int W, int ch, int c, float y, float x) {
    float cy = std::min(std::max(y, 0.0f), (float)(H - 1));
    float cx = std::min(std::max(x, 0.0f), (float)(W - 1));
    int iy = (int)std::floor(cy);
    int ix = (int)std::floor(cx);
    float fy = cy - (float)iy;
    float fx = cx - (float)ix;
    auto keys_w = [](float t, float w[4]) {
        const float a = -0.5f;
        float t2 = t * t, t3 = t2 * t;
        w[0] = a * t3 - 2.0f * a * t2 + a * t;
        w[1] = (a + 2.0f) * t3 - (a + 3.0f) * t2 + 1.0f;
        w[2] = -(a + 2.0f) * t3 + (2.0f * a + 3.0f) * t2 - a * t;
        w[3] = -a * t3 + a * t2;
    };
    float wy[4], wx[4];
    keys_w(fy, wy);
    keys_w(fx, wx);
    float acc = 0.0f;
    for (int dy = -1; dy <= 2; ++dy) {
        int yy = std::min(std::max(iy + dy, 0), H - 1);
        float row = 0.0f;
        for (int dx = -1; dx <= 2; ++dx) {
            int xx = std::min(std::max(ix + dx, 0), W - 1);
            row += wx[dx + 1] * img[((size_t)yy * W + xx) * ch + c];
        }
        acc += wy[dy + 1] * row;
    }
    return acc;
}

inline void fft1d_radix2(std::vector<std::complex<double>>& a, bool inverse = false) {
    const size_t n = a.size();
    if (n <= 1) return;
    size_t j = 0;
    for (size_t i = 1; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    const double two_pi = (inverse ? 2.0 : -2.0) * 3.14159265358979323846;
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = two_pi / (double)len;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<double> u = a[i + k];
                std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (auto& x : a) x /= (double)n;
    }
}

inline void fft2d_radix2(std::vector<std::complex<double>>& buf, int N) {
    std::vector<std::complex<double>> line(N);
    for (int r = 0; r < N; ++r) {
        std::memcpy(line.data(), &buf[(size_t)r * N], (size_t)N * sizeof(std::complex<double>));
        fft1d_radix2(line, false);
        std::memcpy(&buf[(size_t)r * N], line.data(), (size_t)N * sizeof(std::complex<double>));
    }
    for (int c = 0; c < N; ++c) {
        for (int r = 0; r < N; ++r) line[r] = buf[(size_t)r * N + c];
        fft1d_radix2(line, false);
        for (int r = 0; r < N; ++r) buf[(size_t)r * N + c] = line[r];
    }
}

inline double tukey1d(int idx, int n, double alpha = 0.25) {
    if (n <= 1) return 1.0;
    double x = (double)idx / (double)(n - 1);
    if (x < 0.5 * alpha) {
        return 0.5 * (1.0 + std::cos(3.14159265358979323846 * (2.0 * x / alpha - 1.0)));
    }
    if (x > 1.0 - 0.5 * alpha) {
        return 0.5 * (1.0 + std::cos(3.14159265358979323846 * (2.0 * (x - 1.0) / alpha + 1.0)));
    }
    return 1.0;
}

inline void jacobi_symmetric_eigenvalues(std::vector<double>& A, int n, std::vector<double>& evals) {
    evals.resize(n);
    for (int sweep = 0; sweep < 32; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                double apq = A[(size_t)p * n + q];
                off += std::abs(apq);
                if (std::abs(apq) <= 1e-14) continue;
                double app = A[(size_t)p * n + p];
                double aqq = A[(size_t)q * n + q];
                double tau = (aqq - app) / (2.0 * apq);
                double t = (tau >= 0.0) ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
                                        : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
                double c = 1.0 / std::sqrt(1.0 + t * t);
                double s = t * c;
                A[(size_t)p * n + p] = app - t * apq;
                A[(size_t)q * n + q] = aqq + t * apq;
                A[(size_t)p * n + q] = 0.0;
                A[(size_t)q * n + p] = 0.0;
                for (int r = 0; r < n; ++r) {
                    if (r == p || r == q) continue;
                    double arp = A[(size_t)r * n + p];
                    double arq = A[(size_t)r * n + q];
                    A[(size_t)r * n + p] = A[(size_t)p * n + r] = c * arp - s * arq;
                    A[(size_t)r * n + q] = A[(size_t)q * n + r] = s * arp + c * arq;
                }
            }
        }
        if (off < 1e-12) break;
    }
    for (int i = 0; i < n; ++i) evals[i] = A[(size_t)i * n + i];
    std::sort(evals.begin(), evals.end());
}

} // namespace detail

// Evaluates coarse-flow-locked leave-one-out whitened chi^2_nu stratified into
// flat, textured, and edge pixels.
// - pred_rgb: reconstructed linear RGB on output grid [H_out * W_out * 3]
// - out_scale: 1 or 2 relative to raw sensor grid (H1 x W1)
// - held_out_raw: raw mosaic plane [H1 * W1] after dark subtraction + linear scaling into pred_rgb space
// - fy_h, fx_h: half-res flow (H1/2 x W1/2) mapping held-out frame -> anchor grid in half-px units
// - cfa6x6: 36-byte color filter table (0=R,1=G,2=B)
// - sig0, gain: sensor noise model variance = sig0^2 + gain * max(0, pred)
// - mw_h: optional half-res stationary weight [H1/2 * W1/2] (null -> all stationary)
inline StratifiedChi2Report compute_stratified_loo_chi2(
    const float* pred_rgb, int H_out, int W_out, int out_scale,
    const float* held_out_raw, int H1, int W1,
    const float* fy_h, const float* fx_h, int Hh, int Wh,
    const uint8_t* cfa6x6,
    float sig0, float gain,
    const float* mw_h = nullptr,
    float exp_scale = 1.0f,
    float sat_limit = 0.78f,
    int stride = 2,
    const EvalOpticsParams& optics = {},
    float pred_var_scale = 1.0f
) {
    StratifiedChi2Report rep;
    if (!pred_rgb || !held_out_raw || !cfa6x6 || H1 < 16 || W1 < 16) return rep;
    const int margin = 8;
    const float min_sig = std::max(1e-6f, sig0);
    const int nt = (H1 >= 256 && optics.n_threads > 1) ? std::min(8, optics.n_threads) : 1;

    // Optional forward optical PSF blur on a copy of pred_rgb when deconvolved in pipeline
    std::vector<float> pred_blur;
    const float* pred_ch_src = pred_rgb;
    if (optics.psf_sigma_1x > 0.10f) {
        const float sigma_out = optics.psf_sigma_1x * float(out_scale);
        const int rad = std::clamp(int(std::ceil(2.5f * sigma_out)), 1, 6);
        float wgt[13]{};
        double wsum = 0.0;
        for (int k = -rad; k <= rad; ++k) {
            double wk = std::exp(-0.5 * double(k * k) / (double(sigma_out) * double(sigma_out)));
            wgt[k + rad] = float(wk);
            wsum += wk;
        }
        const float inv_w = float(1.0 / wsum);
        for (int k = 0; k <= 2 * rad; ++k) wgt[k] *= inv_w;

        const size_t total_floats = size_t(H_out) * size_t(W_out) * 3;
        std::vector<float> tmp(total_floats);
        pred_blur.resize(total_floats);
        auto run_rows = [&](int total_rows, auto&& fn) {
            if (nt <= 1) { fn(0, total_rows); return; }
            std::vector<std::thread> workers;
            workers.reserve(nt);
            const int per = (total_rows + nt - 1) / nt;
            for (int t = 0; t < nt; ++t) {
                int a = t * per, b = std::min(total_rows, a + per);
                if (a >= b) break;
                workers.emplace_back(fn, a, b);
            }
            for (auto& th : workers) th.join();
        };
        run_rows(H_out, [&](int r0, int r1) {
            for (int r = r0; r < r1; ++r) {
                const float* srow = pred_rgb + size_t(r) * W_out * 3;
                float* drow = tmp.data() + size_t(r) * W_out * 3;
                for (int c = 0; c < W_out; ++c) {
                    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f;
                    for (int k = -rad; k <= rad; ++k) {
                        const int cx = std::min(W_out - 1, std::max(0, c + k));
                        const float w = wgt[k + rad];
                        a0 += w * srow[cx * 3 + 0];
                        a1 += w * srow[cx * 3 + 1];
                        a2 += w * srow[cx * 3 + 2];
                    }
                    drow[c * 3 + 0] = a0; drow[c * 3 + 1] = a1; drow[c * 3 + 2] = a2;
                }
            }
        });
        run_rows(H_out, [&](int r0, int r1) {
            for (int r = r0; r < r1; ++r) {
                float* drow = pred_blur.data() + size_t(r) * W_out * 3;
                for (int c = 0; c < W_out; ++c) {
                    float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f;
                    for (int k = -rad; k <= rad; ++k) {
                        const int ry = std::min(H_out - 1, std::max(0, r + k));
                        const float w = wgt[k + rad];
                        const float* sp = tmp.data() + (size_t(ry) * W_out + c) * 3;
                        a0 += w * sp[0]; a1 += w * sp[1]; a2 += w * sp[2];
                    }
                    drow[c * 3 + 0] = a0; drow[c * 3 + 1] = a1; drow[c * 3 + 2] = a2;
                }
            }
        });
        pred_ch_src = pred_blur.data();
    }

    // Build coarse box-smoothed flow (radius 4 on half grid => 16x16 full-res box)
    // to strictly lock evaluation against subpixel noise fitting.
    std::vector<float> smooth_fy((size_t)Hh * Wh, 0.0f);
    std::vector<float> smooth_fx((size_t)Hh * Wh, 0.0f);
    if (fy_h && fx_h && Hh > 0 && Wh > 0) {
        const int rbox = 4;
        auto box_rows = [&](int hr0, int hr1) {
            for (int hr = hr0; hr < hr1; ++hr) {
                int y0 = std::max(0, hr - rbox), y1 = std::min(Hh - 1, hr + rbox);
                for (int hc = 0; hc < Wh; ++hc) {
                    int x0 = std::max(0, hc - rbox), x1 = std::min(Wh - 1, hc + rbox);
                    double sy = 0.0, sx = 0.0;
                    int cnt = 0;
                    for (int yy = y0; yy <= y1; ++yy) {
                        for (int xx = x0; xx <= x1; ++xx) {
                            sy += fy_h[(size_t)yy * Wh + xx];
                            sx += fx_h[(size_t)yy * Wh + xx];
                            ++cnt;
                        }
                    }
                    double inv = cnt > 0 ? (1.0 / cnt) : 0.0;
                    smooth_fy[(size_t)hr * Wh + hc] = (float)(sy * inv);
                    smooth_fx[(size_t)hr * Wh + hc] = (float)(sx * inv);
                }
            }
        };
        if (nt <= 1 || Hh < 256) {
            box_rows(0, Hh);
        } else {
            std::vector<std::thread> workers;
            workers.reserve(nt);
            const int per = (Hh + nt - 1) / nt;
            for (int t = 0; t < nt; ++t) {
                int a = t * per, b = std::min(Hh, a + per);
                if (a >= b) break;
                workers.emplace_back(box_rows, a, b);
            }
            for (auto& th : workers) th.join();
        }
    }

    const int st = std::max(1, stride);
    const int n_eval_rows = std::max(0, (H1 - margin - margin + st - 1) / st);
    const float cy_opt = 0.5f * float(H1), cx_opt = 0.5f * float(W1);
    const float inv_ropt = 1.0f / std::max(1.0f, std::max(cy_opt, cx_opt));

    struct SliceAccum {
        double sum_flat = 0.0, sum_tex = 0.0, sum_edge = 0.0, sum_res = 0.0;
        size_t n_flat = 0, n_tex = 0, n_edge = 0;
    };
    std::vector<SliceAccum> slices(std::max(1, nt));
    auto eval_slice = [&](int si, int row_idx0, int row_idx1) {
        SliceAccum acc;
        for (int ri = row_idx0; ri < row_idx1; ++ri) {
            const int r = margin + ri * st;
            for (int c = margin; c < W1 - margin; c += st) {
                float ay = (float)r, ax = (float)c;
                if (fy_h && fx_h && Hh > 0 && Wh > 0) {
                    float sy0 = detail::sample_bilinear_clamped(smooth_fy.data(), Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                    float sx0 = detail::sample_bilinear_clamped(smooth_fx.data(), Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                    float ay0 = ay - 2.0f * sy0, ax0 = ax - 2.0f * sx0;
                    float sy1 = detail::sample_bilinear_clamped(smooth_fy.data(), Hh, Wh, 1, 0, 0.5f * ay0, 0.5f * ax0);
                    float sx1 = detail::sample_bilinear_clamped(smooth_fx.data(), Hh, Wh, 1, 0, 0.5f * ay0, 0.5f * ax0);
                    ay = (float)r - 2.0f * sy1;
                    ax = (float)c - 2.0f * sx1;
                }
                if (ay < 2.0f || ay >= (float)(H1 - 3) || ax < 2.0f || ax >= (float)(W1 - 3)) continue;
                if (mw_h) {
                    float mw_v = detail::sample_bilinear_clamped(mw_h, Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                    if (mw_v < 0.85f) continue;
                }

                float raw_obs = held_out_raw[(size_t)r * W1 + c];
                if (!std::isfinite(raw_obs) || raw_obs >= sat_limit) continue;

                int ch = (int)cfa6x6[(r % 6) * 6 + (c % 6)];
                if (ch < 0 || ch > 2) continue;

                float lpy = (out_scale == 2) ? (2.0f * ay) : ay;
                float lpx = (out_scale == 2) ? (2.0f * ax) : ax;
                if (lpy < 2.0f || lpy >= (float)(H_out - 3) || lpx < 2.0f || lpx >= (float)(W_out - 3)) continue;

                float ry = ay, rx = ax;
                if (ch != 1) {
                    const float dyc = ay - cy_opt, dxc = ax - cx_opt;
                    const float rn = (dxc * inv_ropt) * (dxc * inv_ropt) + (dyc * inv_ropt) * (dyc * inv_ropt);
                    const float lca = (ch == 0) ? (optics.lca_r_k1 + optics.lca_r_k2 * rn)
                                                : (optics.lca_b_k1 + optics.lca_b_k2 * rn);
                    ry -= lca * dyc + (ch == 0 ? optics.ois_r_dy : optics.ois_b_dy);
                    rx -= lca * dxc + (ch == 0 ? optics.ois_r_dx : optics.ois_b_dx);
                }
                float py = (out_scale == 2) ? (2.0f * ry) : ry;
                float px = (out_scale == 2) ? (2.0f * rx) : rx;
                if (py < 2.0f || py >= (float)(H_out - 3) || px < 2.0f || px >= (float)(W_out - 3)) continue;

                float pred_ch = detail::sample_bicubic_clamped(pred_ch_src, H_out, W_out, 3, ch, py, px);
                float pred_luma = 0.25f * detail::sample_bicubic_clamped(pred_rgb, H_out, W_out, 3, 0, lpy, lpx)
                                + 0.50f * detail::sample_bicubic_clamped(pred_rgb, H_out, W_out, 3, 1, lpy, lpx)
                                + 0.25f * detail::sample_bicubic_clamped(pred_rgb, H_out, W_out, 3, 2, lpy, lpx);
                float lum_xp = 0.50f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 1, lpy, lpx + (float)out_scale)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 0, lpy, lpx + (float)out_scale)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 2, lpy, lpx + (float)out_scale);
                float lum_xm = 0.50f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 1, lpy, lpx - (float)out_scale)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 0, lpy, lpx - (float)out_scale)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 2, lpy, lpx - (float)out_scale);
                float lum_yp = 0.50f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 1, lpy + (float)out_scale, lpx)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 0, lpy + (float)out_scale, lpx)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 2, lpy + (float)out_scale, lpx);
                float lum_ym = 0.50f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 1, lpy - (float)out_scale, lpx)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 0, lpy - (float)out_scale, lpx)
                             + 0.25f * detail::sample_bilinear_clamped(pred_rgb, H_out, W_out, 3, 2, lpy - (float)out_scale, lpx);

                float pred_obs_space = pred_ch / std::max(1e-5f, exp_scale);
                if (pred_obs_space >= sat_limit) continue;

                float var_i = std::max(1.0f, pred_var_scale) *
                              (min_sig * min_sig + std::max(0.0f, gain) * std::max(0.0f, pred_obs_space));
                float sig_i = std::sqrt(std::max(1e-12f, var_i));
                float z = (raw_obs - pred_obs_space) / sig_i;
                if (std::abs(z) > 8.0f) continue;

                float gx = 0.5f * (lum_xp - lum_xm);
                float gy = 0.5f * (lum_yp - lum_ym);
                float gmag = std::sqrt(gx * gx + gy * gy);
                float var_l = min_sig * min_sig + std::max(0.0f, gain) * std::max(0.0f, pred_luma);
                float g_snr = gmag / std::max(1e-6f, std::sqrt(var_l));

                double chi2_pt = (double)z * (double)z;
                acc.sum_res += z;
                if (g_snr < 2.0f) {
                    acc.sum_flat += chi2_pt;
                    ++acc.n_flat;
                } else if (g_snr < 8.0f) {
                    acc.sum_tex += chi2_pt;
                    ++acc.n_tex;
                } else {
                    acc.sum_edge += chi2_pt;
                    ++acc.n_edge;
                }
            }
        }
        slices[si] = acc;
    };

    if (nt <= 1 || n_eval_rows < 32) {
        eval_slice(0, 0, n_eval_rows);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(nt);
        const int per = (n_eval_rows + nt - 1) / nt;
        for (int t = 0; t < nt; ++t) {
            int a = t * per, b = std::min(n_eval_rows, a + per);
            if (a >= b) break;
            workers.emplace_back(eval_slice, t, a, b);
        }
        for (auto& th : workers) th.join();
    }

    double sum_chi_flat = 0.0, sum_chi_tex = 0.0, sum_chi_edge = 0.0, sum_norm_res = 0.0;
    for (const auto& s : slices) {
        sum_chi_flat += s.sum_flat; sum_chi_tex += s.sum_tex; sum_chi_edge += s.sum_edge;
        sum_norm_res += s.sum_res;
        rep.n_flat += s.n_flat; rep.n_tex += s.n_tex; rep.n_edge += s.n_edge;
    }

    size_t n_tot = rep.n_flat + rep.n_tex + rep.n_edge;
    if (n_tot == 0) return rep;
    rep.chi2_flat = rep.n_flat > 0 ? (sum_chi_flat / (double)rep.n_flat) : 0.0;
    rep.chi2_tex  = rep.n_tex  > 0 ? (sum_chi_tex  / (double)rep.n_tex)  : 0.0;
    rep.chi2_edge = rep.n_edge > 0 ? (sum_chi_edge / (double)rep.n_edge) : 0.0;
    rep.chi2_all  = (sum_chi_flat + sum_chi_tex + sum_chi_edge) / (double)n_tot;
    rep.bias_norm = sum_norm_res / (double)n_tot;
    rep.valid = true;
    return rep;
}

// Disjoint Half-Burst Fourier Ring Correlation on two luma images [H x W]
// (reconstructed from disjoint half-bursts with independent anchors and brought
// into alignment once via coordinate shift).
inline FrcCurveReport compute_half_burst_frc(
    const float* luma_a, const float* luma_b, int H, int W, int tile_size = 128
) {
    FrcCurveReport rep;
    int N = 64;
    while (N * 2 <= tile_size && N * 2 <= std::min(H, W) && N < 256) N *= 2;
    if (!luma_a || !luma_b || H < N || W < N) return rep;

    for (int b = 0; b < FrcCurveReport::NUM_BINS; ++b) {
        rep.freq_nyq[b] = ((double)b + 0.5) / (double)FrcCurveReport::NUM_BINS;
    }

    const int ny_half = N / 2;
    struct TileScore { int y0, x0; double grad; };
    std::vector<TileScore> cands;
    int step = std::max(32, N / 2);
    for (int y0 = 0; y0 + N <= H; y0 += step) {
        for (int x0 = 0; x0 + N <= W; x0 += step) {
            double gsum = 0.0;
            for (int r = 1; r < N - 1; r += 2) {
                for (int c = 1; c < N - 1; c += 2) {
                    double dx = luma_a[(size_t)(y0 + r) * W + (x0 + c + 1)] - luma_a[(size_t)(y0 + r) * W + (x0 + c - 1)];
                    double dy = luma_a[(size_t)(y0 + r + 1) * W + (x0 + c)] - luma_a[(size_t)(y0 + r - 1) * W + (x0 + c)];
                    gsum += dx * dx + dy * dy;
                }
            }
            cands.push_back({y0, x0, gsum});
        }
    }
    if (cands.empty()) return rep;
    std::sort(cands.begin(), cands.end(), [](const TileScore& p, const TileScore& q) {
        return p.grad > q.grad;
    });
    const int max_tiles = std::min((int)cands.size(), 8);

    double num[FrcCurveReport::NUM_BINS] = {};
    double den_a[FrcCurveReport::NUM_BINS] = {};
    double den_b[FrcCurveReport::NUM_BINS] = {};
    std::vector<double> win(N);
    for (int i = 0; i < N; ++i) win[i] = detail::tukey1d(i, N, 0.25);

    std::vector<std::complex<double>> fa((size_t)N * N), fb((size_t)N * N);
    for (int ti = 0; ti < max_tiles; ++ti) {
        int y0 = cands[ti].y0, x0 = cands[ti].x0;
        double ma = 0.0, mb = 0.0;
        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                ma += luma_a[(size_t)(y0 + r) * W + (x0 + c)];
                mb += luma_b[(size_t)(y0 + r) * W + (x0 + c)];
            }
        }
        ma /= (double)(N * N);
        mb /= (double)(N * N);
        for (int r = 0; r < N; ++r) {
            double wr = win[r];
            for (int c = 0; c < N; ++c) {
                double w = wr * win[c];
                fa[(size_t)r * N + c] = std::complex<double>(w * (luma_a[(size_t)(y0 + r) * W + (x0 + c)] - ma), 0.0);
                fb[(size_t)r * N + c] = std::complex<double>(w * (luma_b[(size_t)(y0 + r) * W + (x0 + c)] - mb), 0.0);
            }
        }
        detail::fft2d_radix2(fa, N);
        detail::fft2d_radix2(fb, N);

        for (int r = 0; r < N; ++r) {
            int ky = (r <= ny_half) ? r : (r - N);
            for (int c = 0; c < N; ++c) {
                int kx = (c <= ny_half) ? c : (c - N);
                if (kx == 0 && ky == 0) continue;
                double rad = std::sqrt((double)(kx * kx + ky * ky)) / (double)ny_half;
                if (rad >= 1.0) continue;
                int bin = std::min(FrcCurveReport::NUM_BINS - 1, (int)(rad * FrcCurveReport::NUM_BINS));
                const auto& A = fa[(size_t)r * N + c];
                const auto& B = fb[(size_t)r * N + c];
                num[bin] += A.real() * B.real() + A.imag() * B.imag();
                den_a[bin] += std::norm(A);
                den_b[bin] += std::norm(B);
                rep.counts[bin] += 1.0;
            }
        }
        ++rep.tiles_used;
    }

    int half_cnt = 0;
    double half_sum = 0.0;
    double prev_frc = 1.0, prev_f = 0.0;
    bool got50 = false, got143 = false;
    for (int b = 0; b < FrcCurveReport::NUM_BINS; ++b) {
        double d = std::sqrt(den_a[b] * den_b[b]);
        rep.frc[b] = (d > 1e-18) ? std::max(-1.0, std::min(1.0, num[b] / d)) : 0.0;
        rep.frc_area += std::max(0.0, rep.frc[b]) / (double)FrcCurveReport::NUM_BINS;
        if (rep.freq_nyq[b] >= 0.25 && rep.freq_nyq[b] <= 0.75) {
            half_sum += rep.frc[b];
            ++half_cnt;
        }
        if (!got50 && rep.frc[b] < 0.5) {
            double frac = (prev_frc - 0.5) / std::max(1e-9, prev_frc - rep.frc[b]);
            rep.frc_50_nyq = prev_f + frac * (rep.freq_nyq[b] - prev_f);
            got50 = true;
        }
        if (!got143 && rep.frc[b] < 0.143) {
            double frac = (prev_frc - 0.143) / std::max(1e-9, prev_frc - rep.frc[b]);
            rep.frc_143_nyq = prev_f + frac * (rep.freq_nyq[b] - prev_f);
            got143 = true;
        }
        prev_frc = rep.frc[b];
        prev_f = rep.freq_nyq[b];
    }
    if (!got50) rep.frc_50_nyq = 1.0;
    if (!got143) rep.frc_143_nyq = 1.0;
    rep.frc_half_band_mean = half_cnt > 0 ? (half_sum / half_cnt) : 0.0;
    rep.valid = (rep.tiles_used > 0);
    return rep;
}

// Phase-diversity determinant over burst subpixel shifts (dy, dx) in CFA units.
// Evaluates normalized Gramian of phi(dx, dy) = [1, cos(pi*dx), sin(pi*dx), cos(pi*dy), sin(pi*dy)].
inline PhaseDiversityReport compute_phase_diversity_det(
    const float* dx_samples, const float* dy_samples, size_t n_samples
) {
    PhaseDiversityReport rep;
    if (!dx_samples || !dy_samples || n_samples == 0) return rep;
    constexpr int D = 5;
    std::vector<double> G(D * D, 0.0);
    const double pi = 3.14159265358979323846;
    for (size_t i = 0; i < n_samples; ++i) {
        double v[D] = {
            1.0,
            std::cos(pi * (double)dx_samples[i]),
            std::sin(pi * (double)dx_samples[i]),
            std::cos(pi * (double)dy_samples[i]),
            std::sin(pi * (double)dy_samples[i])
        };
        for (int a = 0; a < D; ++a) {
            for (int b = 0; b < D; ++b) G[(size_t)a * D + b] += v[a] * v[b];
        }
    }
    for (double& x : G) x /= (double)n_samples;
    std::vector<double> evals;
    detail::jacobi_symmetric_eigenvalues(G, D, evals);
    int rank_dim = std::min((int)n_samples, D);
    int start_ev = D - rank_dim;
    double prod = 1.0;
    for (int k = start_ev; k < D; ++k) prod *= std::max(0.0, evals[k]);
    rep.min_eig_norm = std::max(0.0, evals[start_ev]);
    rep.det_norm = std::pow(prod, 1.0 / (double)rank_dim);
    rep.cond_number = (evals[start_ev] > 1e-12) ? (evals.back() / evals[start_ev]) : 1e12;
    rep.samples = n_samples;
    rep.valid = true;
    return rep;
}

// Physical Target Oracle 1: Slanted-edge eSFR (MTF50 & overshoot/undershoot)
// Input is a single-channel ROI [H x W] containing a vertical-ish or horizontal-ish step edge.
inline EsfrMtfReport measure_esfr_mtf50(const float* roi, int H, int W) {
    EsfrMtfReport rep;
    if (!roi || H < 16 || W < 16) return rep;

    // Detect dominant edge orientation (vertical edge -> scan rows; horizontal -> transpose conceptually)
    double sum_abs_gx = 0.0, sum_abs_gy = 0.0;
    for (int r = 1; r < H - 1; ++r) {
        for (int c = 1; c < W - 1; ++c) {
            sum_abs_gx += std::abs(roi[(size_t)r * W + c + 1] - roi[(size_t)r * W + c - 1]);
            sum_abs_gy += std::abs(roi[(size_t)(r + 1) * W + c] - roi[(size_t)(r - 1) * W + c]);
        }
    }
    bool transpose = (sum_abs_gy > sum_abs_gx);
    int R = transpose ? W : H;
    int C = transpose ? H : W;
    auto at = [&](int r, int c) -> double {
        return transpose ? (double)roi[(size_t)c * W + r] : (double)roi[(size_t)r * W + c];
    };

    // Estimate subpixel edge centroid per scanline and fit straight line c_edge = slope * r + intercept
    double s_r = 0.0, s_c = 0.0, s_rr = 0.0, s_rc = 0.0;
    int line_cnt = 0;
    std::vector<double> deriv(C, 0.0);
    for (int r = 2; r < R - 2; ++r) {
        double dsum = 0.0, cdsum = 0.0;
        for (int c = 2; c < C - 2; ++c) {
            double d = std::abs(0.5 * (at(r, c + 1) - at(r, c - 1)));
            deriv[c] = d;
            dsum += d;
            cdsum += (double)c * d;
        }
        if (dsum > 1e-6) {
            double c_cen = cdsum / dsum;
            s_r += (double)r;
            s_c += c_cen;
            s_rr += (double)r * (double)r;
            s_rc += (double)r * c_cen;
            ++line_cnt;
        }
    }
    if (line_cnt < 4) return rep;
    double denom = (double)line_cnt * s_rr - s_r * s_r;
    double slope = (std::abs(denom) > 1e-9) ? ((double)line_cnt * s_rc - s_r * s_c) / denom : 0.0;
    double intercept = (s_c - slope * s_r) / (double)line_cnt;
    rep.edge_angle_deg = std::atan(slope) * (180.0 / 3.14159265358979323846);

    // 4x supersampled ESF binning centered on edge
    constexpr int OS = 4;
    constexpr int HALF_SPAN_PX = 16;
    constexpr int BINS = 2 * HALF_SPAN_PX * OS; // 128 bins
    std::vector<double> bin_sum(BINS, 0.0), bin_cnt(BINS, 0.0);
    for (int r = 2; r < R - 2; ++r) {
        double c_edge = slope * (double)r + intercept;
        for (int c = 0; c < C; ++c) {
            double dist = ((double)c - c_edge) * std::cos(std::atan(slope));
            int b = (int)std::floor((dist + (double)HALF_SPAN_PX) * (double)OS);
            if (b >= 0 && b < BINS) {
                bin_sum[b] += at(r, c);
                bin_cnt[b] += 1.0;
            }
        }
    }

    std::vector<double> esf(BINS, 0.0);
    int last_good = -1;
    for (int b = 0; b < BINS; ++b) {
        if (bin_cnt[b] > 0.0) {
            esf[b] = bin_sum[b] / bin_cnt[b];
            if (last_good >= 0 && b > last_good + 1) {
                for (int k = last_good + 1; k < b; ++k) {
                    double t = (double)(k - last_good) / (double)(b - last_good);
                    esf[k] = (1.0 - t) * esf[last_good] + t * esf[b];
                }
            } else if (last_good < 0 && b > 0) {
                for (int k = 0; k < b; ++k) esf[k] = esf[b];
            }
            last_good = b;
        }
    }
    if (last_good < 0) return rep;
    for (int k = last_good + 1; k < BINS; ++k) esf[k] = esf[last_good];

    double low_val = 0.0, high_val = 0.0;
    for (int k = 0; k < BINS / 8; ++k) low_val += esf[k];
    for (int k = BINS - BINS / 8; k < BINS; ++k) high_val += esf[k];
    low_val /= (double)(BINS / 8);
    high_val /= (double)(BINS / 8);
    if (high_val < low_val) {
        std::swap(low_val, high_val);
        for (double& v : esf) v = high_val + low_val - v;
    }
    double amp = high_val - low_val;
    if (amp < 1e-4) return rep;

    double esf_max = esf[0], esf_min = esf[0];
    for (double v : esf) { esf_max = std::max(esf_max, v); esf_min = std::min(esf_min, v); }
    rep.overshoot_pct = std::max(0.0, (esf_max - high_val) / amp * 100.0);
    rep.undershoot_pct = std::max(0.0, (low_val - esf_min) / amp * 100.0);

    // Compute LSF and apply Hamming window
    std::vector<std::complex<double>> lsf(BINS, {0.0, 0.0});
    double dc = 0.0;
    for (int b = 1; b < BINS - 1; ++b) {
        double d = 0.5 * (esf[b + 1] - esf[b - 1]);
        double hw = 0.54 - 0.46 * std::cos(2.0 * 3.14159265358979323846 * b / (double)(BINS - 1));
        lsf[b] = {d * hw, 0.0};
        dc += d * hw;
    }
    if (std::abs(dc) < 1e-12) return rep;
    detail::fft1d_radix2(lsf, false);
    double mag0 = std::abs(lsf[0]);
    if (mag0 < 1e-12) return rep;

    // Frequency bin k corresponds to k * OS / BINS cycles/pixel. Nyquist = 0.5 cy/px.
    const double df = (double)OS / (double)BINS;
    bool got50 = false, got30 = false;
    double prev_f = 0.0, prev_m = 1.0;
    int max_k = std::min(BINS / 2, (int)std::ceil(0.85 / df));
    for (int k = 1; k <= max_k; ++k) {
        double f = (double)k * df;
        double m = std::abs(lsf[k]) / mag0;
        if (k == 1 || (prev_f <= 0.5 && f >= 0.5)) {
            if (prev_f <= 0.5 && f >= 0.5) {
                double t = (0.5 - prev_f) / std::max(1e-9, f - prev_f);
                rep.mtf_nyq_at_05 = (1.0 - t) * prev_m + t * m;
            }
        }
        if (!got50 && m < 0.5) {
            double t = (prev_m - 0.5) / std::max(1e-9, prev_m - m);
            rep.mtf50_cy_px = prev_f + t * (f - prev_f);
            got50 = true;
        }
        if (!got30 && m < 0.3) {
            double t = (prev_m - 0.3) / std::max(1e-9, prev_m - m);
            rep.mtf30_cy_px = prev_f + t * (f - prev_f);
            got30 = true;
        }
        prev_f = f;
        prev_m = m;
    }
    if (!got50) rep.mtf50_cy_px = (double)max_k * df;
    if (!got30) rep.mtf30_cy_px = (double)max_k * df;
    rep.valid = true;
    return rep;
}

// Physical Target Oracle 2: Stochastic Dead-Leaves Spectral Transfer Ratio
inline DeadLeavesReport measure_dead_leaves_transfer(
    const float* luma_out, const float* luma_ref, int N
) {
    DeadLeavesReport rep;
    if (!luma_out || !luma_ref || N < 32 || (N & (N - 1)) != 0) return rep;
    std::vector<double> win(N);
    for (int i = 0; i < N; ++i) win[i] = detail::tukey1d(i, N, 0.2);

    std::vector<std::complex<double>> fo((size_t)N * N), fr((size_t)N * N);
    double mo = 0.0, mr = 0.0;
    for (int i = 0; i < N * N; ++i) { mo += luma_out[i]; mr += luma_ref[i]; }
    mo /= (double)(N * N);
    mr /= (double)(N * N);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            double w = win[y] * win[x];
            fo[(size_t)y * N + x] = {w * (luma_out[(size_t)y * N + x] - mo), 0.0};
            fr[(size_t)y * N + x] = {w * (luma_ref[(size_t)y * N + x] - mr), 0.0};
        }
    }
    detail::fft2d_radix2(fo, N);
    detail::fft2d_radix2(fr, N);

    const int nh = N / 2;
    double sum_lo = 0, sum_lr = 0, sum_mo = 0, sum_mr = 0, sum_ho = 0, sum_hr = 0;
    for (int y = 0; y < N; ++y) {
        int ky = (y <= nh) ? y : (y - N);
        for (int x = 0; x < N; ++x) {
            int kx = (x <= nh) ? x : (x - N);
            if (kx == 0 && ky == 0) continue;
            double f_nyq = std::sqrt((double)(kx * kx + ky * ky)) / (double)nh;
            double po = std::norm(fo[(size_t)y * N + x]);
            double pr = std::norm(fr[(size_t)y * N + x]);
            if (f_nyq >= 0.10 && f_nyq < 0.30) { sum_lo += po; sum_lr += pr; }
            else if (f_nyq >= 0.30 && f_nyq < 0.60) { sum_mo += po; sum_mr += pr; }
            else if (f_nyq >= 0.60 && f_nyq <= 0.90) { sum_ho += po; sum_hr += pr; }
        }
    }
    rep.ratio_low_band  = (sum_lr > 1e-18) ? std::sqrt(sum_lo / sum_lr) : 0.0;
    rep.ratio_mid_band  = (sum_mr > 1e-18) ? std::sqrt(sum_mo / sum_mr) : 0.0;
    rep.ratio_high_band = (sum_hr > 1e-18) ? std::sqrt(sum_ho / sum_hr) : 0.0;
    double tot_r = sum_lr + sum_mr + sum_hr;
    rep.ratio_overall = (tot_r > 1e-18) ? std::sqrt((sum_lo + sum_mo + sum_ho) / tot_r) : 0.0;
    rep.valid = (tot_r > 1e-18);
    return rep;
}

struct ChromaAliasingComponents {
    double ratio_iso = 0.0;
    double ratio_horiz = 0.0;
    double ratio_vert = 0.0;
    bool denom_valid = false;
};

// Physical Target Oracle 3 (E10 fixed): Symmetric 2D directional & isotropic high-frequency chroma aliasing.
inline ChromaAliasingComponents measure_achromatic_aliasing_components(const float* rgb, int H, int W) {
    ChromaAliasingComponents out;
    if (!rgb || H < 8 || W < 8) return out;
    double chroma_hx = 0.0, chroma_hy = 0.0, luma_hf = 0.0;
    for (int y = 1; y < H - 1; ++y) {
        for (int x = 1; x < W - 1; ++x) {
            auto lum = [&](int yy, int xx) {
                const float* p = &rgb[((size_t)yy * W + xx) * 3];
                return 0.25 * p[0] + 0.50 * p[1] + 0.25 * p[2];
            };
            const float* p = &rgb[((size_t)y * W + x) * 3];
            double c1 = p[0] - p[2];
            double c2 = p[1] - 0.5 * (p[0] + p[2]);
            double l_lap = 4.0 * lum(y, x) - lum(y - 1, x) - lum(y + 1, x) - lum(y, x - 1) - lum(y, x + 1);
            const float* pxp = &rgb[((size_t)y * W + (x + 1)) * 3];
            const float* pxm = &rgb[((size_t)y * W + (x - 1)) * 3];
            const float* pyp = &rgb[((size_t)(y + 1) * W + x) * 3];
            const float* pym = &rgb[((size_t)(y - 1) * W + x) * 3];
            double c1_hx = 2.0 * c1 - (pxp[0] - pxp[2]) - (pxm[0] - pxm[2]);
            double c2_hx = 2.0 * c2 - (pxp[1] - 0.5 * (pxp[0] + pxp[2])) - (pxm[1] - 0.5 * (pxm[0] + pxm[2]));
            double c1_hy = 2.0 * c1 - (pyp[0] - pyp[2]) - (pym[0] - pym[2]);
            double c2_hy = 2.0 * c2 - (pyp[1] - 0.5 * (pyp[0] + pyp[2])) - (pym[1] - 0.5 * (pym[0] + pym[2]));
            chroma_hx += c1_hx * c1_hx + c2_hx * c2_hx;
            chroma_hy += c1_hy * c1_hy + c2_hy * c2_hy;
            luma_hf += l_lap * l_lap;
        }
    }
    if (luma_hf > 1e-15) {
        out.denom_valid = true;
        out.ratio_horiz = std::sqrt(chroma_hx / luma_hf);
        out.ratio_vert  = std::sqrt(chroma_hy / luma_hf);
        // Symmetric 2D aliasing norm matching 1D stripe magnitude (0.500 for pure (-1)^x or (-1)^y).
        out.ratio_iso   = std::sqrt((chroma_hx + chroma_hy) / luma_hf);
    }
    return out;
}

inline double measure_achromatic_aliasing_ratio(const float* rgb, int H, int W) {
    return measure_achromatic_aliasing_components(rgb, H, W).ratio_iso;
}

// Physical Target Oracle 4: Shadow Signed Linearity Bias (detecting ReLU truncation cliffs)
inline ShadowBiasReport measure_shadow_linearity_bias(
    const float* out_vals, const float* true_vals, size_t n
) {
    ShadowBiasReport rep;
    if (!out_vals || !true_vals || n == 0) return rep;
    double sx = 0, sy = 0, sxx = 0, sxy = 0, se = 0, srel = 0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; ++i) {
        double t = (double)true_vals[i];
        double o = (double)out_vals[i];
        if (!std::isfinite(t) || !std::isfinite(o)) continue;
        sx += t; sy += o; sxx += t * t; sxy += t * o;
        se += (o - t) * (o - t);
        if (t > 1e-5) srel += (o - t) / t;
        ++cnt;
    }
    if (cnt < 2) return rep;
    double denom = (double)cnt * sxx - sx * sx;
    rep.slope = (std::abs(denom) > 1e-18) ? (((double)cnt * sxy - sx * sy) / denom) : 1.0;
    rep.intercept = (sy - rep.slope * sx) / (double)cnt;
    rep.rmse = std::sqrt(se / (double)cnt);
    rep.mean_signed_rel_bias = srel / (double)cnt;
    rep.samples = cnt;
    rep.valid = true;
    return rep;
}

// Physical Target Oracle 5: Residual Noise Spectral Flatness Measure (SFM in [0, 1])
// Computed on flat residual patches via 3x3 overlapping Welch PSD inside the native sensor Brillouin zone.
inline double measure_residual_noise_sfm(const float* resid_patch, int N = 64, int out_scale = 1) {
    if (!resid_patch || N < 16 || (N & (N - 1)) != 0) return 0.0;
    constexpr double kPi = 3.14159265358979323846;
    const int M = (N >= 64) ? (N / 2) : N;
    const int stride = (N >= 64) ? (N / 4) : 0;
    const int n_grid = (N >= 64) ? 3 : 1;
    std::vector<double> win(M);
    for (int i = 0; i < M; ++i) {
        win[i] = (N >= 64) ? (0.5 * (1.0 - std::cos(2.0 * kPi * (double)i / (double)(M - 1))))
                           : detail::tukey1d(i, M, 0.15);
    }
    std::vector<double> psd((size_t)M * M, 0.0);
    std::vector<std::complex<double>> buf((size_t)M * M);
    const double inv_nwin = 1.0 / (double)(n_grid * n_grid);
    for (int gy = 0; gy < n_grid; ++gy) {
        for (int gx = 0; gx < n_grid; ++gx) {
            const int y0 = gy * stride, x0 = gx * stride;
            double m = 0.0;
            for (int y = 0; y < M; ++y) {
                for (int x = 0; x < M; ++x) m += (double)resid_patch[(size_t)(y0 + y) * N + (x0 + x)];
            }
            m /= (double)(M * M);
            for (int y = 0; y < M; ++y) {
                for (int x = 0; x < M; ++x) {
                    buf[(size_t)y * M + x] = {win[y] * win[x] * ((double)resid_patch[(size_t)(y0 + y) * N + (x0 + x)] - m), 0.0};
                }
            }
            detail::fft2d_radix2(buf, M);
            for (size_t i = 0; i < (size_t)M * M; ++i) psd[i] += std::norm(buf[i]) * inv_nwin;
        }
    }
    const int r2_min = (M >= 32) ? 4 : 1;
    const int br_lim = (out_scale >= 2) ? std::max(1, M / (2 * out_scale)) : M;
    double log_sum = 0.0, arith_sum = 0.0;
    int cnt = 0;
    for (int y = 0; y < M; ++y) {
        const int ky = (y <= M / 2) ? y : (y - M);
        if (out_scale >= 2 && std::abs(ky) > br_lim) continue;
        for (int x = 0; x < M; ++x) {
            const int kx = (x <= M / 2) ? x : (x - M);
            if (out_scale >= 2 && std::abs(kx) > br_lim) continue;
            if (kx * kx + ky * ky < r2_min) continue;
            const double p = psd[(size_t)y * M + x];
            if (p <= 1e-30) continue;
            log_sum += std::log(p);
            arith_sum += p;
            ++cnt;
        }
    }
    if (cnt == 0 || arith_sum <= 0.0) return 0.0;
    double gm = std::exp(log_sum / (double)cnt);
    double am = arith_sum / (double)cnt;
    return std::max(0.0, std::min(1.0, gm / am));
}

// High-level burst self-evaluation harness (inverse-crime-free LOO chi2 +
// disjoint independent-anchor half-burst FRC + phase-diversity + flat SFM).
inline SelfEvalReport evaluate_burst_self(
    const float* pred_rgb, int H_out, int W_out, int out_scale,
    const std::vector<std::vector<float>>& frames_lin,
    const std::vector<std::vector<float>>& g_halfs,
    const std::vector<std::vector<float>>& flows_fy,
    const std::vector<std::vector<float>>& flows_fx,
    const std::vector<std::vector<float>>& mw_half,
    int H1, int W1, const uint8_t* cfa6x6,
    float sig0, float gain, const std::string& mode_label,
    const EvalOpticsParams& optics = {},
    const IndependentLooEvalInput* ind_loo = nullptr
) {
    SelfEvalReport rep;
    rep.mode_label = mode_label;
    rep.scale_factor = out_scale;
    rep.frame_count = (int)frames_lin.size();
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int N = (int)frames_lin.size();
    if (!pred_rgb || N == 0 || !cfa6x6 || H1 < 64 || W1 < 64) return rep;

    const int k_fs = (N >= 2) ? (N - 1) : 0;
    rep.fullstack_self_chi2 = compute_stratified_loo_chi2(
        pred_rgb, H_out, W_out, out_scale,
        frames_lin[k_fs].data(), H1, W1,
        flows_fy[k_fs].data(), flows_fx[k_fs].data(), Hh, Wh,
        cfa6x6, sig0, gain, mw_half[k_fs].data(), 1.0f, 0.78f, 1, optics, 1.0f);

    if (N >= 2 && ind_loo && ind_loo->loo_pred_rgb && ind_loo->held_out_raw && ind_loo->n_train >= 1) {
        const float pvar = 1.0f + 1.0f / float(std::max(1, ind_loo->n_train));
        rep.loo_chi2 = compute_stratified_loo_chi2(
            ind_loo->loo_pred_rgb, H_out, W_out, out_scale,
            ind_loo->held_out_raw, H1, W1,
            ind_loo->held_fy_h, ind_loo->held_fx_h, Hh, Wh,
            cfa6x6, sig0, gain, ind_loo->held_mw_h,
            ind_loo->exp_scale, ind_loo->sat_limit, 1, ind_loo->optics, pvar);
    } else {
        rep.loo_chi2 = StratifiedChi2Report{};
    }

    // 2. Phase diversity determinant over sampled stationary flow vectors
    std::vector<float> pdx, pdy;
    const int step_pd = std::max(16, std::min(Hh, Wh) / 8);
    for (int hr = 16; hr < Hh - 16; hr += step_pd) {
        for (int hc = 16; hc < Wh - 16; hc += step_pd) {
            size_t hi = (size_t)hr * Wh + hc;
            for (int n = 0; n < N; ++n) {
                if (mw_half[n][hi] < 0.8f) continue;
                pdx.push_back(2.0f * flows_fx[n][hi]);
                pdy.push_back(2.0f * flows_fy[n][hi]);
            }
        }
    }
    rep.phase_div = compute_phase_diversity_det(pdx.data(), pdy.data(), pdx.size());

    // 3. Disjoint independent-anchor half-burst FRC (even {0,2,..} vs odd {1,3,..})
    (void)g_halfs;
    if (N >= 4) {
        const int crop_out = (out_scale == 2) ? 256 : 128;
        const int crop_1x = crop_out / out_scale;
        int best_y = H1 / 2 - crop_1x / 2, best_x = W1 / 2 - crop_1x / 2;
        double best_score = -1.0;
        const int border = 32;
        int cy_step = std::max(32, (H1 - 2 * border - crop_1x) / 6);
        int cx_step = std::max(32, (W1 - 2 * border - crop_1x) / 6);
        for (int y0 = border; y0 + crop_1x <= H1 - border; y0 += cy_step) {
            for (int x0 = border; x0 + crop_1x <= W1 - border; x0 += cx_step) {
                double ge = 0.0;
                bool clean = true;
                for (int r = y0 / 2; r < (y0 + crop_1x) / 2 - 1 && clean; r += 2) {
                    int yo = std::min(H_out - 2, std::max(1, r * 2 * out_scale));
                    for (int c = x0 / 2; c < (x0 + crop_1x) / 2 - 1; c += 2) {
                        size_t hi = (size_t)r * Wh + c;
                        int xo = std::min(W_out - 2, std::max(1, c * 2 * out_scale));
                        const float* p = &pred_rgb[((size_t)yo * W_out + xo) * 3];
                        float g0 = 0.25f * p[0] + 0.50f * p[1] + 0.25f * p[2];
                        if (mw_half[1][hi] < 0.85f || g0 > 0.72f) { clean = false; break; }
                        const float* px = &pred_rgb[((size_t)yo * W_out + xo + 1) * 3];
                        const float* py = &pred_rgb[((size_t)(yo + 1) * W_out + xo) * 3];
                        float gx = (0.25f * px[0] + 0.50f * px[1] + 0.25f * px[2]) - g0;
                        float gy = (0.25f * py[0] + 0.50f * py[1] + 0.25f * py[2]) - g0;
                        ge += (double)gx * (double)gx + (double)gy * (double)gy;
                    }
                }
                if (clean && ge > best_score) { best_score = ge; best_y = y0 & ~1; best_x = x0 & ~1; }
            }
        }

        // Compute median shift of frame 1 within the crop as independent anchor lattice shift
        float f1_dy = 0.0f, f1_dx = 0.0f;
        {
            std::vector<float> vys, vxs;
            for (int r = best_y / 2; r < (best_y + crop_1x) / 2; r += 2) {
                for (int c = best_x / 2; c < (best_x + crop_1x) / 2; c += 2) {
                    size_t hi = (size_t)r * Wh + c;
                    if (mw_half[1][hi] >= 0.8f) {
                        vys.push_back(2.0f * flows_fy[1][hi]);
                        vxs.push_back(2.0f * flows_fx[1][hi]);
                    }
                }
            }
            if (!vys.empty()) {
                std::nth_element(vys.begin(), vys.begin() + vys.size() / 2, vys.end());
                std::nth_element(vxs.begin(), vxs.begin() + vxs.size() / 2, vxs.end());
                f1_dy = vys[vys.size() / 2];
                f1_dx = vxs[vxs.size() / 2];
            }
        }

        auto fuse_green_crop = [&](int parity, float base_dy, float base_dx) {
            std::vector<float> out((size_t)crop_out * crop_out, 0.0f);
            const float inv_2s2 = 1.0f / (2.0f * 0.42f * 0.42f);
            for (int r_o = 0; r_o < crop_out; ++r_o) {
                float ay = (float)best_y + (float)r_o / (float)out_scale;
                for (int c_o = 0; c_o < crop_out; ++c_o) {
                    float ax = (float)best_x + (float)c_o / (float)out_scale;
                    float f1y = detail::sample_bilinear_clamped(flows_fy[1].data(), Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax) * 2.0f;
                    float f1x = detail::sample_bilinear_clamped(flows_fx[1].data(), Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax) * 2.0f;
                    float ref_ay = ay + (parity == 1 ? (base_dy - f1y) : 0.0f);
                    float ref_ax = ax + (parity == 1 ? (base_dx - f1x) : 0.0f);
                    int hry = std::min(Hh - 1, std::max(0, (int)(0.5f * ref_ay)));
                    int hrx = std::min(Wh - 1, std::max(0, (int)(0.5f * ref_ax)));
                    double wsum = 0.0, vsum = 0.0;
                    for (int n = parity; n < N; n += 2) {
                        float mwv = mw_half[n][(size_t)hry * Wh + hrx];
                        if (mwv < 0.25f) continue;
                        float dy_n = !flows_fy[n].empty() ? 2.0f * detail::sample_bilinear_clamped(flows_fy[n].data(), Hh, Wh, 1, 0, 0.5f * ref_ay, 0.5f * ref_ax) : 0.0f;
                        float dx_n = !flows_fx[n].empty() ? 2.0f * detail::sample_bilinear_clamped(flows_fx[n].data(), Hh, Wh, 1, 0, 0.5f * ref_ay, 0.5f * ref_ax) : 0.0f;
                        float ty = ref_ay + dy_n;
                        float tx = ref_ax + dx_n;
                        int ylo = std::max(0, (int)std::floor(ty) - 1);
                        int yhi = std::min(H1 - 1, (int)std::floor(ty) + 2);
                        int xlo = std::max(0, (int)std::floor(tx) - 1);
                        int xhi = std::min(W1 - 1, (int)std::floor(tx) + 2);
                        for (int yy = ylo; yy <= yhi; ++yy) {
                            for (int xx = xlo; xx <= xhi; ++xx) {
                                if (cfa6x6[(yy % 6) * 6 + (xx % 6)] != 1) continue;
                                float d2 = (yy - ty) * (yy - ty) + (xx - tx) * (xx - tx);
                                if (d2 > 2.25f) continue;
                                float w = mwv * std::exp(-d2 * inv_2s2);
                                wsum += w;
                                vsum += w * frames_lin[n][(size_t)yy * W1 + xx];
                            }
                        }
                    }
                    out[(size_t)r_o * crop_out + c_o] = (wsum > 1e-8) ? (float)(vsum / wsum) : 0.0f;
                }
            }
            return out;
        };

        std::vector<float> even_crop = fuse_green_crop(0, 0.0f, 0.0f);
        std::vector<float> odd_native = fuse_green_crop(1, f1_dy, f1_dx);
        std::vector<float> odd_warped((size_t)crop_out * crop_out, 0.0f);
        for (int r_o = 0; r_o < crop_out; ++r_o) {
            float ay = (float)best_y + (float)r_o / (float)out_scale;
            for (int c_o = 0; c_o < crop_out; ++c_o) {
                float ax = (float)best_x + (float)c_o / (float)out_scale;
                float f1y = detail::sample_bilinear_clamped(flows_fy[1].data(), Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax) * 2.0f;
                float f1x = detail::sample_bilinear_clamped(flows_fx[1].data(), Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax) * 2.0f;
                float ry_o = (float)r_o + (f1y - f1_dy) * (float)out_scale;
                float rx_o = (float)c_o + (f1x - f1_dx) * (float)out_scale;
                odd_warped[(size_t)r_o * crop_out + c_o] = detail::sample_bicubic_clamped(
                    odd_native.data(), crop_out, crop_out, 1, 0, ry_o, rx_o);
            }
        }
        rep.half_burst_frc = compute_half_burst_frc(even_crop.data(), odd_warped.data(), crop_out, crop_out, crop_out);
    }

    // 4. Residual noise SFM on minimum-residual flat 64x64 patch and aliasing ratio on textured patch
    if (H_out >= 128 && W_out >= 128) {
        auto get_l = [&](int y, int x) {
            const float* p = &pred_rgb[((size_t)y * W_out + x) * 3];
            return 0.25f * p[0] + 0.50f * p[1] + 0.25f * p[2];
        };
        // Exact 1D discrete orthogonal polynomials on k in 0..63 for bokeh/vignette detrending
        double p1d[4][64]{}, n1d[4]{};
        double mu2 = 0.0, mu4 = 0.0;
        for (int k = 0; k < 64; ++k) {
            double t = ((double)k - 31.5) / 31.5;
            mu2 += t * t;
            mu4 += t * t * t * t;
        }
        mu2 /= 64.0;
        mu4 /= 64.0;
        const double mu42 = mu4 / mu2;
        for (int k = 0; k < 64; ++k) {
            double t = ((double)k - 31.5) / 31.5;
            p1d[0][k] = 1.0;
            p1d[1][k] = t;
            p1d[2][k] = t * t - mu2;
            p1d[3][k] = t * t * t - mu42 * t;
            for (int a = 0; a < 4; ++a) n1d[a] += p1d[a][k] * p1d[a][k];
        }

        int flat_y = H_out / 4, flat_x = W_out / 4, fb_y = flat_y, fb_x = flat_x;
        double min_quad_var = 1e30, min_ge = 1e30;
        int tex_y = H_out / 2 - 32, tex_x = W_out / 2 - 32;
        double max_ge = -1.0;
        int ps = std::max(64, std::min(H_out, W_out) / 8);
        for (int y0 = 32; y0 + 64 <= H_out - 32; y0 += ps) {
            for (int x0 = 32; x0 + 64 <= W_out - 32; x0 += ps) {
                double ge = 0.0, ch = 0.0, lm = 0.0;
                for (int r = 1; r < 63; r += 2) {
                    for (int c = 1; c < 63; c += 2) {
                        const float* p = &pred_rgb[((size_t)(y0 + r) * W_out + (x0 + c)) * 3];
                        const float* px = &pred_rgb[((size_t)(y0 + r) * W_out + (x0 + c + 1)) * 3];
                        const float* py = &pred_rgb[((size_t)(y0 + r + 1) * W_out + (x0 + c)) * 3];
                        double l0 = 0.25 * p[0] + 0.5 * p[1] + 0.25 * p[2];
                        double lx = 0.25 * px[0] + 0.5 * px[1] + 0.25 * px[2];
                        double ly = 0.25 * py[0] + 0.5 * py[1] + 0.25 * py[2];
                        ge += (lx - l0) * (lx - l0) + (ly - l0) * (ly - l0);
                        ch += std::abs(p[0] - p[2]) + std::abs(p[1] - 0.5 * (p[0] + p[2]));
                        lm += std::max(0.0, l0);
                    }
                }
                if (lm > 1.0 && ge < min_ge) { min_ge = ge; fb_y = y0; fb_x = x0; }
                if (lm > 1.0 && ch / lm < 0.25 && ge > max_ge) { max_ge = ge; tex_y = y0; tex_x = x0; }

                double sum_l = 0.0, sum_l2 = 0.0, max_l = -1e9;
                double qcoef[4][4]{};
                for (int r = 0; r < 64; ++r) {
                    double rb[3] = {};
                    const int y = y0 + r;
                    for (int c = 0; c < 64; ++c) {
                        const double v = (double)get_l(y, x0 + c);
                        sum_l += v;
                        sum_l2 += v * v;
                        if (v > max_l) max_l = v;
                        rb[0] += v;
                        rb[1] += p1d[1][c] * v;
                        rb[2] += p1d[2][c] * v;
                    }
                    for (int a = 0; a <= 2; ++a) {
                        for (int b = 0; a + b <= 2; ++b) qcoef[a][b] += p1d[a][r] * rb[b];
                    }
                }
                const double mean_l = sum_l / 4096.0;
                if (mean_l > 0.002 && max_l < 0.85) {
                    double fit_e = 0.0;
                    for (int a = 0; a <= 2; ++a) {
                        for (int b = 0; a + b <= 2; ++b) {
                            fit_e += (qcoef[a][b] * qcoef[a][b]) / (n1d[a] * n1d[b]);
                        }
                    }
                    const double qvar = std::max(0.0, sum_l2 - fit_e) / 4096.0;
                    if (qvar < min_quad_var) {
                        min_quad_var = qvar;
                        flat_y = y0;
                        flat_x = x0;
                    }
                }
            }
        }
        if (min_quad_var >= 1e29) { flat_y = fb_y; flat_x = fb_x; }

        std::vector<float> flat_hp(64 * 64, 0.0f);
        double coef[4][4]{};
        for (int r = 0; r < 64; ++r) {
            double rb[4] = {};
            const int y = flat_y + r;
            for (int c = 0; c < 64; ++c) {
                const double v = (double)get_l(y, flat_x + c);
                flat_hp[(size_t)r * 64 + c] = (float)v;
                for (int b = 0; b < 4; ++b) rb[b] += p1d[b][c] * v;
            }
            for (int a = 0; a < 4; ++a) {
                for (int b = 0; a + b <= 3; ++b) coef[a][b] += p1d[a][r] * rb[b];
            }
        }
        for (int a = 0; a < 4; ++a) {
            for (int b = 0; a + b <= 3; ++b) coef[a][b] /= (n1d[a] * n1d[b]);
        }
        for (int r = 0; r < 64; ++r) {
            for (int c = 0; c < 64; ++c) {
                double fit = 0.0;
                for (int a = 0; a < 4; ++a) {
                    const double pa = p1d[a][r];
                    for (int b = 0; a + b <= 3; ++b) fit += coef[a][b] * pa * p1d[b][c];
                }
                flat_hp[(size_t)r * 64 + c] = (float)((double)flat_hp[(size_t)r * 64 + c] - fit);
            }
        }
        rep.residual_noise_sfm = measure_residual_noise_sfm(flat_hp.data(), 64, out_scale);

        std::vector<float> tex_rgb(64 * 64 * 3);
        for (int r = 0; r < 64; ++r) {
            std::memcpy(&tex_rgb[(size_t)r * 64 * 3],
                        &pred_rgb[((size_t)(tex_y + r) * W_out + tex_x) * 3], 64 * 3 * sizeof(float));
        }
        rep.aliasing_chroma_ratio = measure_achromatic_aliasing_ratio(tex_rgb.data(), 64, 64);

        // 5. Sharpest clean two-plateau unsaturated 64x64 step edge for self eSFR MTF50 & overshoot
        int esfr_y = -1, esfr_x = -1;
        double best_edge_score = -1.0;
        const int eps_step = std::max(32, std::min(H_out, W_out) / 12);
        for (int y0 = 32; y0 + 64 <= H_out - 32; y0 += eps_step) {
            for (int x0 = 32; x0 + 64 <= W_out - 32; x0 += eps_step) {
                float mn = 1e9f, mx = -1e9f;
                double max_gx = 0.0, max_gy = 0.0;
                for (int r = 4; r < 60; ++r) {
                    for (int c = 4; c < 60; ++c) {
                        const float l0 = get_l(y0 + r, x0 + c);
                        const float lx = get_l(y0 + r, x0 + c + 1);
                        const float ly = get_l(y0 + r + 1, x0 + c);
                        mn = std::min(mn, l0);
                        mx = std::max(mx, l0);
                        max_gx = std::max(max_gx, (double)std::abs(lx - l0));
                        max_gy = std::max(max_gy, (double)std::abs(ly - l0));
                    }
                }
                if (mx >= 0.85f || (mx - mn) < 0.08f) continue;

                auto check_step = [&](bool horiz_edge) -> double {
                    double sa = 0.0, sa2 = 0.0, sb = 0.0, sb2 = 0.0;
                    const double n_strip = 16.0 * 56.0;
                    for (int u = 0; u < 16; ++u) {
                        for (int v = 4; v < 60; ++v) {
                            double va = horiz_edge ? get_l(y0 + u, x0 + v) : get_l(y0 + v, x0 + u);
                            double vb = horiz_edge ? get_l(y0 + 48 + u, x0 + v) : get_l(y0 + v, x0 + 48 + u);
                            sa += va; sa2 += va * va;
                            sb += vb; sb2 += vb * vb;
                        }
                    }
                    double mu_a = sa / n_strip, mu_b = sb / n_strip;
                    double sd_a = std::sqrt(std::max(0.0, sa2 / n_strip - mu_a * mu_a));
                    double sd_b = std::sqrt(std::max(0.0, sb2 / n_strip - mu_b * mu_b));
                    double step_amp = std::abs(mu_b - mu_a);
                    if (step_amp < 0.06 || step_amp < 0.65 * (double)(mx - mn) ||
                        std::max(sd_a, sd_b) > 0.12 * step_amp) {
                        return -1.0;
                    }
                    double sum_sg = 0.0, sum_ag = 0.0;
                    for (int u = 16; u < 47; ++u) {
                        for (int v = 4; v < 60; ++v) {
                            double g = horiz_edge ? (get_l(y0 + u + 1, x0 + v) - get_l(y0 + u, x0 + v))
                                                  : (get_l(y0 + v, x0 + u + 1) - get_l(y0 + v, x0 + u));
                            sum_sg += g;
                            sum_ag += std::abs(g);
                        }
                    }
                    double cons = std::abs(sum_sg) / (sum_ag + 1e-9);
                    if (cons < 0.65) return -1.0;
                    return step_amp * (horiz_edge ? max_gy : max_gx);
                };

                double score = std::max(check_step(false), check_step(true));
                if (score > best_edge_score) {
                    best_edge_score = score;
                    esfr_y = y0;
                    esfr_x = x0;
                }
            }
        }
        if (esfr_y >= 0 && esfr_x >= 0) {
            std::vector<float> edge_roi(64 * 64);
            for (int r = 0; r < 64; ++r) {
                for (int c = 0; c < 64; ++c) {
                    edge_roi[(size_t)r * 64 + c] = get_l(esfr_y + r, esfr_x + c);
                }
            }
            rep.esfr = measure_esfr_mtf50(edge_roi.data(), 64, 64);
        }
    }
    return rep;
}

inline bool write_self_eval_json(const std::string& path, const SelfEvalReport& rep) {
    if (path.empty()) return false;
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto fmt_opt = [](bool ok, double v) -> std::string {
        if (!ok || !std::isfinite(v)) return "null";
        char b[64];
        std::snprintf(b, sizeof(b), "%.6f", v);
        return std::string(b);
    };
    const bool lv = rep.loo_chi2.valid;
    const bool fv = rep.fullstack_self_chi2.valid;
    std::fprintf(f,
        "{\n"
        "  \"mode\": \"%s\",\n"
        "  \"scale\": %d,\n"
        "  \"frame_count\": %d,\n"
        "  \"loo_chi2\": {\n"
        "    \"valid\": %s,\n"
        "    \"all\": %s,\n"
        "    \"flat\": %s,\n"
        "    \"tex\": %s,\n"
        "    \"edge\": %s,\n"
        "    \"bias_norm\": %s,\n"
        "    \"n_flat\": %zu,\n"
        "    \"n_tex\": %zu,\n"
        "    \"n_edge\": %zu\n"
        "  },\n"
        "  \"fullstack_self_chi2\": {\n"
        "    \"valid\": %s,\n"
        "    \"all\": %s,\n"
        "    \"flat\": %s,\n"
        "    \"tex\": %s,\n"
        "    \"edge\": %s,\n"
        "    \"bias_norm\": %s,\n"
        "    \"n_flat\": %zu,\n"
        "    \"n_tex\": %zu,\n"
        "    \"n_edge\": %zu\n"
        "  },\n"
        "  \"half_burst_frc\": {\n"
        "    \"valid\": %s,\n"
        "    \"tiles_used\": %d,\n"
        "    \"frc_50_nyq\": %.6f,\n"
        "    \"frc_143_nyq\": %.6f,\n"
        "    \"frc_area\": %.6f,\n"
        "    \"frc_half_band_mean\": %.6f\n"
        "  },\n"
        "  \"phase_diversity\": {\n"
        "    \"valid\": %s,\n"
        "    \"det_norm\": %.6f,\n"
        "    \"min_eig_norm\": %.6f,\n"
        "    \"cond_number\": %.4f,\n"
        "    \"samples\": %zu\n"
        "  },\n"
        "  \"esfr\": {\n"
        "    \"valid\": %s,\n"
        "    \"mtf50_cy_px\": %.6f,\n"
        "    \"mtf30_cy_px\": %.6f,\n"
        "    \"overshoot_pct\": %.4f,\n"
        "    \"undershoot_pct\": %.4f\n"
        "  },\n"
        "  \"residual_noise_sfm\": %.6f,\n"
        "  \"aliasing_chroma_ratio\": %.6f\n"
        "}\n",
        rep.mode_label.c_str(), rep.scale_factor, rep.frame_count,
        lv ? "true" : "false",
        fmt_opt(lv, rep.loo_chi2.chi2_all).c_str(),
        fmt_opt(lv, rep.loo_chi2.chi2_flat).c_str(),
        fmt_opt(lv, rep.loo_chi2.chi2_tex).c_str(),
        fmt_opt(lv, rep.loo_chi2.chi2_edge).c_str(),
        fmt_opt(lv, rep.loo_chi2.bias_norm).c_str(),
        lv ? rep.loo_chi2.n_flat : 0u, lv ? rep.loo_chi2.n_tex : 0u, lv ? rep.loo_chi2.n_edge : 0u,
        fv ? "true" : "false",
        fmt_opt(fv, rep.fullstack_self_chi2.chi2_all).c_str(),
        fmt_opt(fv, rep.fullstack_self_chi2.chi2_flat).c_str(),
        fmt_opt(fv, rep.fullstack_self_chi2.chi2_tex).c_str(),
        fmt_opt(fv, rep.fullstack_self_chi2.chi2_edge).c_str(),
        fmt_opt(fv, rep.fullstack_self_chi2.bias_norm).c_str(),
        fv ? rep.fullstack_self_chi2.n_flat : 0u, fv ? rep.fullstack_self_chi2.n_tex : 0u, fv ? rep.fullstack_self_chi2.n_edge : 0u,
        rep.half_burst_frc.valid ? "true" : "false",
        rep.half_burst_frc.tiles_used,
        rep.half_burst_frc.frc_50_nyq, rep.half_burst_frc.frc_143_nyq,
        rep.half_burst_frc.frc_area, rep.half_burst_frc.frc_half_band_mean,
        rep.phase_div.valid ? "true" : "false",
        rep.phase_div.det_norm, rep.phase_div.min_eig_norm,
        rep.phase_div.cond_number, rep.phase_div.samples,
        rep.esfr.valid ? "true" : "false",
        rep.esfr.mtf50_cy_px, rep.esfr.mtf30_cy_px,
        rep.esfr.overshoot_pct, rep.esfr.undershoot_pct,
        rep.residual_noise_sfm, rep.aliasing_chroma_ratio
    );
    std::fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// Track 5 (A14): Unequal-Variance Split-Half FRC, Streaming 4.763 MiB + <= 578 KiB local odd-flow cache
// 16-Patch Stage-4A Linear-Green Repeatability, Chromatic Fringe & 4-Tier Verdict
// ---------------------------------------------------------------------------

struct UnequalSplitFrcConv {
    double s_full = 0.0;        // Full-stack SNR S / v_F
    double frc_full_rep = 0.0;  // Correlation between two independent full stacks: s_F / (1 + s_F)
    double frc_vs_truth = 0.0;  // Correlation of full stack against noise-free truth: sqrt(s_F / (1 + s_F))
};

// A14.3: Exact quadratic solution of (1 - r^2) S^2 - r^2 (v1 + v2) S - r^2 v1 v2 = 0
// with actual full-stack relative noise variance v_F = w^T C w / (sum w)^2 (defaults to v1*v2/(v1+v2)).
// Negative r_half is preserved honestly on frc_full_rep without artificial ReLU clamping.
inline UnequalSplitFrcConv convert_unequal_split_frc(
    double r_half, double v1, double v2, double v_full_override = 0.0
) noexcept {
    UnequalSplitFrcConv out;
    if (!(v1 > 0.0) || !(v2 > 0.0) || !std::isfinite(r_half)) return out;
    if (r_half <= 0.0) {
        out.s_full = 0.0;
        out.frc_full_rep = std::max(-1.0, r_half);
        out.frc_vs_truth = 0.0;
        return out;
    }
    if (r_half >= 0.999999) {
        out.s_full = 1e6;
        out.frc_full_rep = 1.0;
        out.frc_vs_truth = 1.0;
        return out;
    }
    const double r2 = r_half * r_half;
    const double omr2 = std::max(1e-12, 1.0 - r2);
    const double vsum = v1 + v2;
    const double vprod = v1 * v2;
    const double disc = std::max(0.0, r2 * r2 * vsum * vsum + 4.0 * r2 * omr2 * vprod);
    const double S = (r2 * vsum + std::sqrt(disc)) / (2.0 * omr2);
    const double v_full = (v_full_override > 0.0 && std::isfinite(v_full_override))
        ? v_full_override
        : (vprod / vsum);
    out.s_full = std::max(0.0, S / std::max(1e-30, v_full));
    out.frc_full_rep = out.s_full / (1.0 + out.s_full);
    out.frc_vs_truth = std::sqrt(out.frc_full_rep);
    return out;
}

// Measure R-G and B-G slanted-edge ESF centroid offset in 1x sensor pixels on a 64x64 output ROI.
inline double measure_rgb_edge_fringe_1x_px(const float* rgb_roi, int H, int W, int out_scale) {
    if (!rgb_roi || H < 16 || W < 16 || out_scale < 1) return 0.0;
    double sum_gx = 0.0, sum_gy = 0.0;
    for (int r = 1; r < H - 1; ++r) {
        for (int c = 1; c < W - 1; ++c) {
            const float g0 = rgb_roi[((size_t)r * W + c) * 3 + 1];
            const float gx = rgb_roi[((size_t)r * W + c + 1) * 3 + 1] - g0;
            const float gy = rgb_roi[((size_t)(r + 1) * W + c) * 3 + 1] - g0;
            sum_gx += std::abs((double)gx);
            sum_gy += std::abs((double)gy);
        }
    }
    const bool transpose = (sum_gy > sum_gx);
    const int R = transpose ? W : H;
    const int C = transpose ? H : W;
    auto at_ch = [&](int r, int c, int ch) -> double {
        return transpose ? (double)rgb_roi[((size_t)c * W + r) * 3 + ch]
                         : (double)rgb_roi[((size_t)r * W + c) * 3 + ch];
    };

    auto ch_centroid = [&](int ch, bool& ok) -> double {
        ok = false;
        double ws = 0.0, cs = 0.0;
        int rows_ok = 0;
        for (int r = 4; r < R - 4; ++r) {
            double row_w = 0.0, row_cw = 0.0;
            for (int c = 3; c < C - 3; ++c) {
                const double d = std::abs(0.5 * (at_ch(r, c + 1, ch) - at_ch(r, c - 1, ch)));
                row_w += d;
                row_cw += (double)c * d;
            }
            if (row_w > 1e-4) {
                ws += row_w;
                cs += row_cw;
                ++rows_ok;
            }
        }
        if (rows_ok < 8 || ws <= 1e-4) return 0.0;
        ok = true;
        return cs / ws;
    };

    bool ok_r = false, ok_g = false, ok_b = false;
    const double cr = ch_centroid(0, ok_r);
    const double cg = ch_centroid(1, ok_g);
    const double cb = ch_centroid(2, ok_b);
    if (!ok_r || !ok_g || !ok_b) return 0.0;
    const double d_max_out = std::max(std::abs(cr - cg), std::abs(cb - cg));
    return std::min(4.0, d_max_out / (double)out_scale);
}

// Streaming 16-patch (128x128 output lattice) Split-Half Stage-4A Linear-Green accumulator (4.763 MiB + <= 578 KiB odd-flow cache).
// Memory footprint of patch planes: 16 * 128 * 128 * 4 * sizeof(float) = 4.00 MiB exact!
struct StreamingSplitHalfFrc {
    static constexpr int MAX_PATCHES = 16;
    static constexpr int PATCH_N = 128;
    static constexpr int NUM_BINS = 32;

    struct PatchMeta {
        int y0_1x = 0;
        int x0_1x = 0;
        float f1_dy_1x = 0.0f;
        float f1_dx_1x = 0.0f;
        double w_even_frame_sum = 0.0;
        double w_odd_frame_sum = 0.0;
        double w2_even_frame_sum = 0.0;
        double w2_odd_frame_sum = 0.0;
        double phi_2d = 0.0;
        double phi_x = 0.0;
        double phi_y = 0.0;
        double n_eff = 0.0;
    };

    static constexpr int FLOW_PATCH_SIDE = 68; // 64 half-res core + 2-px margin on each side (<= 578 KiB total)

    int out_scale = 2;
    int H1 = 0, W1 = 0;
    int n_patches = 0;
    int n_even_frames = 0;
    int n_odd_frames = 0;
    bool has_odd_ref_flows = false;
    uint8_t cfa6x6[36] = {};
    std::vector<PatchMeta> patches;
    // 4.00 MiB flat storage: [patch][plane: 0=even_v, 1=even_w, 2=odd_v, 3=odd_w][PATCH_N * PATCH_N]
    std::vector<float> planes;
    // Bounded patch-local first-odd reference flow cache: [patch][0=fy, 1=fx][FLOW_PATCH_SIDE * FLOW_PATCH_SIDE]
    std::vector<float> odd_ref_flows;

    // Initialize up to 16 disjoint patches from anchor frame 0 ONLY (independent scout, zero fused-noise bias).
    void init_from_anchor(
        const float* anchor_raw_lin, int in_H1, int in_W1, int in_out_scale,
        const uint8_t* in_cfa6x6, float sat_limit = 0.80f
    ) {
        H1 = in_H1;
        W1 = in_W1;
        out_scale = std::clamp(in_out_scale, 1, 2);
        n_patches = 0;
        n_even_frames = 0;
        n_odd_frames = 0;
        has_odd_ref_flows = false;
        patches.clear();
        planes.clear();
        odd_ref_flows.clear();
        if (!anchor_raw_lin || !in_cfa6x6) return;
        std::memcpy(cfa6x6, in_cfa6x6, 36);

        const int crop_1x = PATCH_N / out_scale; // 64 at 2x, 128 at 1x
        const int margin = 16;
        if (H1 < crop_1x + 2 * margin || W1 < crop_1x + 2 * margin) return;

        struct Cand { int y0, x0; double score; };
        std::vector<Cand> cands;
        const int step_y = std::max(crop_1x / 2, (H1 - 2 * margin - crop_1x) / 6);
        const int step_x = std::max(crop_1x / 2, (W1 - 2 * margin - crop_1x) / 6);
        for (int y0 = margin; y0 + crop_1x <= H1 - margin; y0 += step_y) {
            for (int x0 = margin; x0 + crop_1x <= W1 - margin; x0 += step_x) {
                double ge = 0.0;
                bool clean = true;
                int g_cnt = 0;
                for (int r = y0 + 2; r < y0 + crop_1x - 2 && clean; ++r) {
                    for (int c = x0 + 2; c < x0 + crop_1x - 2; ++c) {
                        const float v0 = anchor_raw_lin[(size_t)r * W1 + c];
                        if (!std::isfinite(v0) || v0 >= sat_limit) { clean = false; break; }
                        if (cfa6x6[(r % 6) * 6 + (c % 6)] == 1 && c + 2 < x0 + crop_1x && r + 2 < y0 + crop_1x) {
                            const float vx = anchor_raw_lin[(size_t)r * W1 + (c + 2)];
                            const float vy = anchor_raw_lin[(size_t)(r + 2) * W1 + c];
                            if (cfa6x6[(r % 6) * 6 + ((c + 2) % 6)] == 1) ge += (double)(vx - v0) * (vx - v0);
                            if (cfa6x6[((r + 2) % 6) * 6 + (c % 6)] == 1) ge += (double)(vy - v0) * (vy - v0);
                            ++g_cnt;
                        }
                    }
                }
                if (clean && g_cnt >= 16) {
                    cands.push_back({y0 & ~1, x0 & ~1, ge / (double)g_cnt});
                }
            }
        }
        if (cands.empty()) return;
        // Deterministic tie-breaking by coordinates after gradient energy sort
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
            if (std::abs(a.score - b.score) > 1e-12) return a.score > b.score;
            if (a.y0 != b.y0) return a.y0 < b.y0;
            return a.x0 < b.x0;
        });

        // Select non-overlapping sharp patches (up to 12) + stratified full-frame grid (up to 4) = <= 16
        auto overlaps = [&](int y0, int x0) {
            for (const auto& p : patches) {
                if (std::abs(y0 - p.y0_1x) < crop_1x && std::abs(x0 - p.x0_1x) < crop_1x) return true;
            }
            return false;
        };
        for (const auto& c : cands) {
            if ((int)patches.size() >= 12) break;
            if (!overlaps(c.y0, c.x0)) {
                PatchMeta pm;
                pm.y0_1x = c.y0;
                pm.x0_1x = c.x0;
                patches.push_back(pm);
            }
        }
        // Add up to 4 stratified coverage patches across quadrants if room remains
        for (size_t k = cands.size() / 2; k < cands.size() && (int)patches.size() < MAX_PATCHES; ++k) {
            if (!overlaps(cands[k].y0, cands[k].x0)) {
                PatchMeta pm;
                pm.y0_1x = cands[k].y0;
                pm.x0_1x = cands[k].x0;
                patches.push_back(pm);
            }
        }
        n_patches = (int)patches.size();
        planes.assign((size_t)n_patches * 4u * (size_t)(PATCH_N * PATCH_N), 0.0f);
    }

    // Choose a deterministic phase-balanced Even/Odd frame partition (Frame 0 in Even, floor(N/2) in Odd)
    // using ONLY global shift phases (zero pixel-noise inspection => 100% independent half-stack noise!).
    static std::vector<int> choose_phase_balanced_parities(const std::vector<std::array<float, 2>>& shifts_yx_1x) {
        const int N = (int)shifts_yx_1x.size();
        std::vector<int> par(std::max(0, N), 0);
        for (int i = 0; i < N; ++i) par[(size_t)i] = i & 1;
        if (N < 4 || N > 12) return par;
        const int k_odd = N / 2;
        auto score_subset = [&](uint32_t mask_odd) -> double {
            auto half_div = [&](int want_odd) -> double {
                double cx = 0.0, sx = 0.0, cy = 0.0, sy = 0.0, cxy = 0.0, sxy = 0.0;
                int cnt = 0;
                for (int i = 0; i < N; ++i) {
                    const int is_o = ((mask_odd >> i) & 1u) ? 1 : 0;
                    if (is_o != want_odd) continue;
                    const double uy = shifts_yx_1x[(size_t)i][0];
                    const double ux = shifts_yx_1x[(size_t)i][1];
                    constexpr double kTwoPi = 6.28318530717958647692;
                    cx += std::cos(kTwoPi * ux); sx += std::sin(kTwoPi * ux);
                    cy += std::cos(kTwoPi * uy); sy += std::sin(kTwoPi * uy);
                    cxy += std::cos(kTwoPi * (ux + uy)); sxy += std::sin(kTwoPi * (ux + uy));
                    ++cnt;
                }
                if (cnt <= 1) return 0.0;
                const double inv2 = 1.0 / ((double)cnt * (double)cnt);
                const double dx = std::max(0.0, 1.0 - (cx * cx + sx * sx) * inv2);
                const double dy = std::max(0.0, 1.0 - (cy * cy + sy * sy) * inv2);
                const double dxy = std::max(0.0, 1.0 - (cxy * cxy + sxy * sxy) * inv2);
                return 0.40 * dx + 0.40 * dy + 0.20 * dxy;
            };
            return std::min(half_div(0), half_div(1));
        };
        uint32_t best_mask = 0u;
        for (int i = 0; i < N; ++i) if (i & 1) best_mask |= (1u << i);
        double best_sc = score_subset(best_mask);
        const uint32_t lim = 1u << N;
        for (uint32_t m = 2u; m < lim; m += 2u) {
            if (__builtin_popcount(m) != k_odd) continue;
            const double sc = score_subset(m);
            if (sc > best_sc + 1e-9) {
                best_sc = sc;
                best_mask = m;
            }
        }
        for (int i = 0; i < N; ++i) par[(size_t)i] = ((best_mask >> i) & 1u) ? 1 : 0;
        return par;
    }

    // Accumulate a single frame's linear CFA + intra-frame directional HA Green observations (strictly zero cross-frame leakage!).
    void accumulate_frame(
        int frame_idx,
        const float* frame_raw_lin,
        const float* fy_h, const float* fx_h,
        const float* mw_h,
        int Hh, int Wh,
        const float* fy_ref1_h = nullptr, const float* fx_ref1_h = nullptr,
        float frame_inv_var_weight = 1.0f,
        float sat_limit = 0.90f,
        int explicit_parity = -1
    ) {
        if (n_patches <= 0 || !frame_raw_lin) return;
        const int parity = (explicit_parity >= 0) ? (explicit_parity & 1) : (frame_idx & 1);
        const bool is_first_odd = (parity == 1 && n_odd_frames == 0);
        if (parity == 0) ++n_even_frames;
        else ++n_odd_frames;

        const int crop_1x = PATCH_N / out_scale;
        const float sig_splat = (out_scale == 2) ? 0.27f : 0.38f;
        const float r2_lim = (out_scale == 2) ? 1.44f : 1.96f;
        const float inv_2s2 = 1.0f / (2.0f * sig_splat * sig_splat);
        const float fw = std::max(1e-4f, frame_inv_var_weight);

        // Lock odd-reference median shift and bounded 68x68 patch-local flow per patch on first odd frame arrival
        // (eliminates whole-frame 90.88 MiB odd flow copy on Z8!)
        if (is_first_odd && fy_h && fx_h && Hh > 0 && Wh > 0) {
            odd_ref_flows.assign((size_t)n_patches * 2u * (size_t)(FLOW_PATCH_SIDE * FLOW_PATCH_SIDE), 0.0f);
            has_odd_ref_flows = true;
            for (int pi = 0; pi < n_patches; ++pi) {
                auto& pm = patches[(size_t)pi];
                float* dst_fy = odd_ref_flows.data() + ((size_t)pi * 2u + 0u) * (FLOW_PATCH_SIDE * FLOW_PATCH_SIDE);
                float* dst_fx = odd_ref_flows.data() + ((size_t)pi * 2u + 1u) * (FLOW_PATCH_SIDE * FLOW_PATCH_SIDE);
                const int hy_base = (pm.y0_1x / 2) - 2;
                const int hx_base = (pm.x0_1x / 2) - 2;
                for (int lr = 0; lr < FLOW_PATCH_SIDE; ++lr) {
                    const int hr = std::clamp(hy_base + lr, 0, Hh - 1);
                    for (int lc = 0; lc < FLOW_PATCH_SIDE; ++lc) {
                        const int hc = std::clamp(hx_base + lc, 0, Wh - 1);
                        dst_fy[(size_t)lr * FLOW_PATCH_SIDE + lc] = fy_h[(size_t)hr * Wh + hc];
                        dst_fx[(size_t)lr * FLOW_PATCH_SIDE + lc] = fx_h[(size_t)hr * Wh + hc];
                    }
                }
                std::vector<float> vys, vxs;
                for (int r = pm.y0_1x / 2; r < (pm.y0_1x + crop_1x) / 2; r += 2) {
                    const int hr = std::clamp(r, 0, Hh - 1);
                    for (int c = pm.x0_1x / 2; c < (pm.x0_1x + crop_1x) / 2; c += 2) {
                        const int hc = std::clamp(c, 0, Wh - 1);
                        const size_t hi = (size_t)hr * Wh + hc;
                        if (!mw_h || mw_h[hi] >= 0.75f) {
                            vys.push_back(2.0f * fy_h[hi]);
                            vxs.push_back(2.0f * fx_h[hi]);
                        }
                    }
                }
                if (!vys.empty()) {
                    std::nth_element(vys.begin(), vys.begin() + vys.size() / 2, vys.end());
                    std::nth_element(vxs.begin(), vxs.begin() + vxs.size() / 2, vxs.end());
                    pm.f1_dy_1x = vys[vys.size() / 2];
                    pm.f1_dx_1x = vxs[vxs.size() / 2];
                }
            }
        }

        auto eval_intra_frame_green = [&](int yy, int xx, float& out_g, float& out_site_w) -> bool {
            const float v0 = frame_raw_lin[(size_t)yy * W1 + xx];
            if (!std::isfinite(v0) || v0 >= sat_limit) return false;
            if (cfa6x6[(yy % 6) * 6 + (xx % 6)] == 1) {
                out_g = v0;
                out_site_w = 1.0f;
                return true;
            }
            if (yy < 2 || yy + 2 >= H1 || xx < 2 || xx + 2 >= W1) return false;
            if (cfa6x6[(yy % 6) * 6 + ((xx - 1) % 6)] != 1 ||
                cfa6x6[(yy % 6) * 6 + ((xx + 1) % 6)] != 1 ||
                cfa6x6[((yy - 1) % 6) * 6 + (xx % 6)] != 1 ||
                cfa6x6[((yy + 1) % 6) * 6 + (xx % 6)] != 1) {
                return false;
            }
            const float gl = frame_raw_lin[(size_t)yy * W1 + (xx - 1)];
            const float gr = frame_raw_lin[(size_t)yy * W1 + (xx + 1)];
            const float gu = frame_raw_lin[(size_t)(yy - 1) * W1 + xx];
            const float gd = frame_raw_lin[(size_t)(yy + 1) * W1 + xx];
            const float cl2 = frame_raw_lin[(size_t)yy * W1 + (xx - 2)];
            const float cr2 = frame_raw_lin[(size_t)yy * W1 + (xx + 2)];
            const float cu2 = frame_raw_lin[(size_t)(yy - 2) * W1 + xx];
            const float cd2 = frame_raw_lin[(size_t)(yy + 2) * W1 + xx];
            if (!std::isfinite(gl) || !std::isfinite(gr) || !std::isfinite(gu) || !std::isfinite(gd) ||
                gl >= sat_limit || gr >= sat_limit || gu >= sat_limit || gd >= sat_limit) {
                return false;
            }
            const float lap_h = 2.0f * v0 - cl2 - cr2;
            const float lap_v = 2.0f * v0 - cu2 - cd2;
            const float gh = 0.5f * (gl + gr) + 0.25f * lap_h;
            const float gv = 0.5f * (gu + gd) + 0.25f * lap_v;
            const float dh = std::fabs(gl - gr) + std::fabs(lap_h) + 1e-6f;
            const float dv = std::fabs(gu - gd) + std::fabs(lap_v) + 1e-6f;
            const float g_mn = std::min(std::min(gl, gr), std::min(gu, gd));
            const float g_mx = std::max(std::max(gl, gr), std::max(gu, gd));
            out_g = std::clamp((dv * gh + dh * gv) / (dh + dv), g_mn, g_mx);
            out_site_w = 0.45f;
            return true;
        };

        for (int pi = 0; pi < n_patches; ++pi) {
            auto& pm = patches[(size_t)pi];
            float* v_plane = planes.data() + ((size_t)pi * 4u + (parity == 0 ? 0u : 2u)) * (PATCH_N * PATCH_N);
            float* w_plane = planes.data() + ((size_t)pi * 4u + (parity == 0 ? 1u : 3u)) * (PATCH_N * PATCH_N);
            double mw_sum = 0.0;
            int mw_cnt = 0;

            for (int r_o = 0; r_o < PATCH_N; ++r_o) {
                const float ay = (float)pm.y0_1x + (float)r_o / (float)out_scale;
                for (int c_o = 0; c_o < PATCH_N; ++c_o) {
                    const float ax = (float)pm.x0_1x + (float)c_o / (float)out_scale;
                    float ref_ay = ay, ref_ax = ax;
                    if (parity == 1) {
                        if (has_odd_ref_flows) {
                            const float* pfy = odd_ref_flows.data() + ((size_t)pi * 2u + 0u) * (FLOW_PATCH_SIDE * FLOW_PATCH_SIDE);
                            const float* pfx = odd_ref_flows.data() + ((size_t)pi * 2u + 1u) * (FLOW_PATCH_SIDE * FLOW_PATCH_SIDE);
                            const float ly = 0.5f * ay - (float)((pm.y0_1x / 2) - 2);
                            const float lx = 0.5f * ax - (float)((pm.x0_1x / 2) - 2);
                            const float f1y = 2.0f * detail::sample_bilinear_clamped(pfy, FLOW_PATCH_SIDE, FLOW_PATCH_SIDE, 1, 0, ly, lx);
                            const float f1x = 2.0f * detail::sample_bilinear_clamped(pfx, FLOW_PATCH_SIDE, FLOW_PATCH_SIDE, 1, 0, ly, lx);
                            ref_ay = ay + (pm.f1_dy_1x - f1y);
                            ref_ax = ax + (pm.f1_dx_1x - f1x);
                        } else if (fy_ref1_h && fx_ref1_h && Hh > 0 && Wh > 0) {
                            const float f1y = 2.0f * detail::sample_bilinear_clamped(fy_ref1_h, Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                            const float f1x = 2.0f * detail::sample_bilinear_clamped(fx_ref1_h, Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                            ref_ay = ay + (pm.f1_dy_1x - f1y);
                            ref_ax = ax + (pm.f1_dx_1x - f1x);
                        }
                    }
                    const int hry = (Hh > 0) ? std::clamp((int)(0.5f * ref_ay), 0, Hh - 1) : 0;
                    const int hrx = (Wh > 0) ? std::clamp((int)(0.5f * ref_ax), 0, Wh - 1) : 0;
                    const float mwv = (mw_h && Hh > 0 && Wh > 0) ? mw_h[(size_t)hry * Wh + hrx] : 1.0f;
                    if (mwv < 0.25f) continue;
                    mw_sum += mwv;
                    ++mw_cnt;

                    const float dy_n = (fy_h && Hh > 0 && Wh > 0)
                        ? (2.0f * detail::sample_bilinear_clamped(fy_h, Hh, Wh, 1, 0, 0.5f * ref_ay, 0.5f * ref_ax))
                        : 0.0f;
                    const float dx_n = (fx_h && Hh > 0 && Wh > 0)
                        ? (2.0f * detail::sample_bilinear_clamped(fx_h, Hh, Wh, 1, 0, 0.5f * ref_ay, 0.5f * ref_ax))
                        : 0.0f;
                    const float ty = ref_ay + dy_n;
                    const float tx = ref_ax + dx_n;
                    const int ylo = std::max(0, (int)std::floor(ty) - 1);
                    const int yhi = std::min(H1 - 1, (int)std::floor(ty) + 2);
                    const int xlo = std::max(0, (int)std::floor(tx) - 1);
                    const int xhi = std::min(W1 - 1, (int)std::floor(tx) + 2);
                    float loc_w = 0.0f, loc_v = 0.0f;
                    for (int yy = ylo; yy <= yhi; ++yy) {
                        for (int xx = xlo; xx <= xhi; ++xx) {
                            const float d2 = (yy - ty) * (yy - ty) + (xx - tx) * (xx - tx);
                            if (d2 > r2_lim) continue;
                            float sv = 0.0f, site_w = 1.0f;
                            if (!eval_intra_frame_green(yy, xx, sv, site_w)) continue;
                            const float wk = fw * mwv * site_w * std::exp(-d2 * inv_2s2);
                            loc_w += wk;
                            loc_v += wk * sv;
                        }
                    }
                    const size_t oidx = (size_t)r_o * PATCH_N + c_o;
                    v_plane[oidx] += loc_v;
                    w_plane[oidx] += loc_w;
                }
            }
            if (mw_cnt > 0) {
                const double mw_bar = mw_sum / (double)mw_cnt;
                const double eff_fw = (double)fw * mw_bar;
                // Frame k has relative noise variance 1/fw and normalized weight w_k = fw * mw_bar,
                // so w_k^2 * sigma_k^2 = (fw * mw_bar)^2 * (1 / fw) = fw * mw_bar^2 (scale-invariant in mw!).
                const double w2_sig_k = (double)fw * mw_bar * mw_bar;
                if (parity == 0) {
                    pm.w_even_frame_sum += eff_fw;
                    pm.w2_even_frame_sum += w2_sig_k;
                } else {
                    pm.w_odd_frame_sum += eff_fw;
                    pm.w2_odd_frame_sum += w2_sig_k;
                }
            }
        }
    }
};

struct BurstSuitabilityScorecard {
    bool valid = false;
    std::string verdict = "INSUFFICIENT_EVIDENCE"; // INSUFFICIENT_EVIDENCE | RECOMMEND_1X | MARGINAL_2X | STRONG_2X
    std::string bottleneck = "missing_evidence";   // none | missing_evidence | phase_coverage | motion_rejection | noise | diffraction | single_axis_only | anisotropic_weak_axis
    std::string repeatability_kind = "shared_flow_stage4a_green_split_half_frc";
    int n_frames = 0;
    int n_even = 0;
    int n_odd = 0;
    int out_scale = 1;
    int patches_used = 0;
    double n_eff_p15 = 0.0;
    double n_eff_median = 0.0;
    double phi_l = 0.0;          // P15 4-alias Bayer Schur completeness in [0, 1]
    double phi_x_l = 0.0;        // P15 horizontal 2-alias completeness
    double phi_y_l = 0.0;        // P15 vertical 2-alias completeness
    bool bi_rank_ok = false;
    double f_l_cy_1x = 0.0;      // Strict conservative bidirectional 4-sector minimum f_min (cy/px_1x)
    double f_min_cy_1x = 0.0;    // Minimum directional bandwidth across [0, 45, 90, 135] deg
    double f_median_cy_1x = 0.0; // Median directional bandwidth across [0, 45, 90, 135] deg
    double f_max_cy_1x = 0.0;    // Strongest directional bandwidth across [0, 45, 90, 135] deg
    double f_dir_cy_1x[4] = {};  // [0]=H(0deg), [1]=D45, [2]=V(90deg), [3]=D135
    double f_l_lp_mm = 0.0;      // 1000 * f_l / pitch_um (0 if pitch unknown)
    double f_l_lw_ph = 0.0;      // 2 * f_l * H1
    double mp_1x = 0.0;
    double mp_band = 0.0;        // Supported frequency-area effective megapixels (<= 4 * mp_1x)
    double supported_area_vs_1x = 0.0;
    double frc50_half_cy_1x = 0.0;
    double frc143_half_cy_1x = 0.0;
    double frc50_full_cy_1x = 0.0;
    double frc143_full_cy_1x = 0.0;
    double esfr_mtf50_cy_1x = 0.0;
    double esfr_overshoot_pct = 0.0;
    double residual_fringe_1x_px = 0.0;
    double snr_gain_db = 0.0;
    int unpacked_frames = 0;
    bool loo_chi2_measured = false;
    double loo_chi2_all = 0.0;
    double ring_freq_cy_1x[StreamingSplitHalfFrc::NUM_BINS] = {};
    double ring_frc_half[StreamingSplitHalfFrc::NUM_BINS] = {};
    double ring_frc_se[StreamingSplitHalfFrc::NUM_BINS] = {};
    double ring_frc_full[StreamingSplitHalfFrc::NUM_BINS] = {};
};

// Finalize streaming Split-Half FRC + directional bandwidth + post-render eSFR/fringe into BurstSuitabilityScorecard.
inline BurstSuitabilityScorecard evaluate_streaming_suitability_scorecard(
    const StreamingSplitHalfFrc& acc,
    const float* fy_ref1_h, const float* fx_ref1_h, int Hh, int Wh,
    std::span<const double> patch_phi_2d,
    std::span<const double> patch_phi_x,
    std::span<const double> patch_phi_y,
    std::span<const double> patch_n_eff,
    const float* pred_rgb_out, int H_out, int W_out,
    double sensor_pitch_um = 0.0,
    double fnum_aperture = 0.0,
    double loo_chi2 = -1.0,
    int unpacked_frames = 0
) {
    BurstSuitabilityScorecard sc;
    sc.n_frames = acc.n_even_frames + acc.n_odd_frames;
    sc.n_even = acc.n_even_frames;
    sc.n_odd = acc.n_odd_frames;
    sc.unpacked_frames = (unpacked_frames > 0) ? unpacked_frames : sc.n_frames;
    if (sc.n_frames == 0 && sc.unpacked_frames == 1) {
        sc.n_frames = 1;
        sc.n_even = 1;
        sc.n_odd = 0;
    }
    sc.out_scale = acc.out_scale;
    sc.mp_1x = ((double)acc.H1 * (double)acc.W1) * 1e-6;
    sc.loo_chi2_measured = (sc.unpacked_frames >= 2 && loo_chi2 > 0.0 && std::isfinite(loo_chi2));
    sc.loo_chi2_all = sc.loo_chi2_measured ? loo_chi2 : 0.0;

    auto pct_vec = [](std::vector<double> v, double q) -> double {
        if (v.empty()) return 0.0;
        std::sort(v.begin(), v.end());
        const double pos = std::clamp(q, 0.0, 1.0) * (double)(v.size() - 1);
        const size_t i0 = (size_t)std::floor(pos);
        const size_t i1 = std::min(v.size() - 1, i0 + 1);
        const double t = pos - (double)i0;
        return (1.0 - t) * v[i0] + t * v[i1];
    };

    if (!patch_phi_2d.empty()) {
        sc.phi_l = pct_vec(std::vector<double>(patch_phi_2d.begin(), patch_phi_2d.end()), 0.15);
    }
    if (!patch_phi_x.empty()) {
        sc.phi_x_l = pct_vec(std::vector<double>(patch_phi_x.begin(), patch_phi_x.end()), 0.15);
    }
    if (!patch_phi_y.empty()) {
        sc.phi_y_l = pct_vec(std::vector<double>(patch_phi_y.begin(), patch_phi_y.end()), 0.15);
    }
    if (!patch_n_eff.empty()) {
        sc.n_eff_p15 = pct_vec(std::vector<double>(patch_n_eff.begin(), patch_n_eff.end()), 0.15);
        sc.n_eff_median = pct_vec(std::vector<double>(patch_n_eff.begin(), patch_n_eff.end()), 0.50);
    } else if (sc.n_frames >= 1) {
        sc.n_eff_p15 = (double)sc.n_frames;
        sc.n_eff_median = (double)sc.n_frames;
    }
    sc.bi_rank_ok = (sc.n_frames >= 4 && sc.phi_l >= 0.05 && sc.phi_x_l >= 0.15 && sc.phi_y_l >= 0.15);
    sc.snr_gain_db = (sc.n_eff_median > 1.0) ? (10.0 * std::log10(sc.n_eff_median)) : 0.0;

    // Post-render slanted-edge eSFR MTF50, overshoot %, and R/B vs G chromatic fringe on sharpest step edge
    if (pred_rgb_out && H_out >= 128 && W_out >= 128) {
        auto get_l = [&](int y, int x) -> float {
            const float* p = &pred_rgb_out[((size_t)y * W_out + x) * 3];
            return 0.25f * p[0] + 0.50f * p[1] + 0.25f * p[2];
        };
        int best_y = -1, best_x = -1;
        double best_edge = -1.0;
        const int stp = std::max(32, std::min(H_out, W_out) / 12);
        for (int y0 = 32; y0 + 64 <= H_out - 32; y0 += stp) {
            for (int x0 = 32; x0 + 64 <= W_out - 32; x0 += stp) {
                float mn = 1e9f, mx = -1e9f;
                double mgx = 0.0, mgy = 0.0;
                for (int r = 4; r < 60; r += 2) {
                    for (int c = 4; c < 60; c += 2) {
                        const float l0 = get_l(y0 + r, x0 + c);
                        mn = std::min(mn, l0);
                        mx = std::max(mx, l0);
                        mgx = std::max(mgx, (double)std::abs(get_l(y0 + r, x0 + c + 1) - l0));
                        mgy = std::max(mgy, (double)std::abs(get_l(y0 + r + 1, x0 + c) - l0));
                    }
                }
                if (mx >= 0.85f || (mx - mn) < 0.08f) continue;
                auto chk = [&](bool horiz) -> double {
                    double sa = 0, sa2 = 0, sb = 0, sb2 = 0;
                    const double ns = 16.0 * 56.0;
                    for (int u = 0; u < 16; ++u) {
                        for (int v = 4; v < 60; ++v) {
                            const double va = horiz ? get_l(y0 + u, x0 + v) : get_l(y0 + v, x0 + u);
                            const double vb = horiz ? get_l(y0 + 48 + u, x0 + v) : get_l(y0 + v, x0 + 48 + u);
                            sa += va; sa2 += va * va; sb += vb; sb2 += vb * vb;
                        }
                    }
                    const double mua = sa / ns, mub = sb / ns;
                    const double sda = std::sqrt(std::max(0.0, sa2 / ns - mua * mua));
                    const double sdb = std::sqrt(std::max(0.0, sb2 / ns - mub * mub));
                    const double amp = std::abs(mub - mua);
                    if (amp < 0.06 || std::max(sda, sdb) > 0.14 * amp) return -1.0;
                    return amp * (horiz ? mgy : mgx);
                };
                const double sc_e = std::max(chk(false), chk(true));
                if (sc_e > best_edge) { best_edge = sc_e; best_y = y0; best_x = x0; }
            }
        }
        if (best_y >= 0 && best_x >= 0) {
            std::vector<float> edge_l(64 * 64), edge_rgb(64 * 64 * 3);
            for (int r = 0; r < 64; ++r) {
                for (int c = 0; c < 64; ++c) {
                    edge_l[(size_t)r * 64 + c] = get_l(best_y + r, best_x + c);
                    const float* sp = &pred_rgb_out[((size_t)(best_y + r) * W_out + (best_x + c)) * 3];
                    std::memcpy(&edge_rgb[((size_t)r * 64 + c) * 3], sp, 3 * sizeof(float));
                }
            }
            const EsfrMtfReport es = measure_esfr_mtf50(edge_l.data(), 64, 64);
            if (es.valid) {
                sc.esfr_mtf50_cy_1x = es.mtf50_cy_px * (double)acc.out_scale;
                sc.esfr_overshoot_pct = es.overshoot_pct;
            }
            sc.residual_fringe_1x_px = measure_rgb_edge_fringe_1x_px(edge_rgb.data(), 64, 64, acc.out_scale);
        }
    }

    constexpr int N = StreamingSplitHalfFrc::PATCH_N;
    constexpr int BINS = StreamingSplitHalfFrc::NUM_BINS;
    const double nyq_cy_1x = 0.5 * (double)acc.out_scale; // 1.0 cy/px_1x at 2x, 0.5 cy/px_1x at 1x
    for (int b = 0; b < BINS; ++b) {
        sc.ring_freq_cy_1x[b] = (((double)b + 0.5) / (double)BINS) * nyq_cy_1x;
    }

    if (acc.n_patches <= 0 || acc.n_even_frames < 1 || acc.n_odd_frames < 1) {
        sc.verdict = "INSUFFICIENT_EVIDENCE";
        sc.bottleneck = "missing_evidence";
        return sc;
    }

    // Reuse single-thread FFT & work buffers: 2 * 128^2 complex<float> (0.25 MiB) + 2 * 128^2 float (0.125 MiB)
    std::vector<float> p_even((size_t)N * N), p_odd_nat((size_t)N * N), p_odd((size_t)N * N);
    std::vector<uint8_t> valid_m((size_t)N * N);
    std::vector<std::complex<double>> fa((size_t)N * N), fb((size_t)N * N);
    std::vector<double> hann(N);
    for (int i = 0; i < N; ++i) {
        hann[i] = 0.5 * (1.0 - std::cos(6.28318530717958647692 * (double)i / (double)(N - 1)));
    }

    struct PatchRingStat {
        double num[BINS] = {};
        double da[BINS] = {};
        double db[BINS] = {};
        double dir_num[4][BINS] = {};
        double dir_da[4][BINS] = {};
        double dir_db[4][BINS] = {};
        double v1 = 1.0;
        double v2 = 1.0;
        double v_full = 0.5;
    };
    std::vector<PatchRingStat> pstats;
    pstats.reserve((size_t)acc.n_patches);

    const int nh = N / 2;
    for (int pi = 0; pi < acc.n_patches; ++pi) {
        const auto& pm = acc.patches[(size_t)pi];
        const float* ev_v = acc.planes.data() + ((size_t)pi * 4u + 0u) * (N * N);
        const float* ev_w = acc.planes.data() + ((size_t)pi * 4u + 1u) * (N * N);
        const float* od_v = acc.planes.data() + ((size_t)pi * 4u + 2u) * (N * N);
        const float* od_w = acc.planes.data() + ((size_t)pi * 4u + 3u) * (N * N);

        int valid_px = 0;
        for (int i = 0; i < N * N; ++i) {
            if (ev_w[i] > 1e-5f) p_even[i] = ev_v[i] / ev_w[i];
            else p_even[i] = 0.0f;
            if (od_w[i] > 1e-5f) p_odd_nat[i] = od_v[i] / od_w[i];
            else p_odd_nat[i] = 0.0f;
        }
        // Warp Odd half once into Even half anchor coordinates via coarse flow shift
        double w_win_sum = 0.0, mean_a = 0.0, mean_b = 0.0;
        for (int r_o = 0; r_o < N; ++r_o) {
            const float ay = (float)pm.y0_1x + (float)r_o / (float)acc.out_scale;
            for (int c_o = 0; c_o < N; ++c_o) {
                const float ax = (float)pm.x0_1x + (float)c_o / (float)acc.out_scale;
                float ry_o = (float)r_o, rx_o = (float)c_o;
                if (acc.has_odd_ref_flows) {
                    const float* pfy = acc.odd_ref_flows.data() + ((size_t)pi * 2u + 0u) * (StreamingSplitHalfFrc::FLOW_PATCH_SIDE * StreamingSplitHalfFrc::FLOW_PATCH_SIDE);
                    const float* pfx = acc.odd_ref_flows.data() + ((size_t)pi * 2u + 1u) * (StreamingSplitHalfFrc::FLOW_PATCH_SIDE * StreamingSplitHalfFrc::FLOW_PATCH_SIDE);
                    const float ly = 0.5f * ay - (float)((pm.y0_1x / 2) - 2);
                    const float lx = 0.5f * ax - (float)((pm.x0_1x / 2) - 2);
                    const float f1y = 2.0f * detail::sample_bilinear_clamped(pfy, StreamingSplitHalfFrc::FLOW_PATCH_SIDE, StreamingSplitHalfFrc::FLOW_PATCH_SIDE, 1, 0, ly, lx);
                    const float f1x = 2.0f * detail::sample_bilinear_clamped(pfx, StreamingSplitHalfFrc::FLOW_PATCH_SIDE, StreamingSplitHalfFrc::FLOW_PATCH_SIDE, 1, 0, ly, lx);
                    ry_o += (f1y - pm.f1_dy_1x) * (float)acc.out_scale;
                    rx_o += (f1x - pm.f1_dx_1x) * (float)acc.out_scale;
                } else if (fy_ref1_h && fx_ref1_h && Hh > 0 && Wh > 0) {
                    const float f1y = 2.0f * detail::sample_bilinear_clamped(fy_ref1_h, Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                    const float f1x = 2.0f * detail::sample_bilinear_clamped(fx_ref1_h, Hh, Wh, 1, 0, 0.5f * ay, 0.5f * ax);
                    ry_o += (f1y - pm.f1_dy_1x) * (float)acc.out_scale;
                    rx_o += (f1x - pm.f1_dx_1x) * (float)acc.out_scale;
                }
                const int iy = std::clamp((int)std::lround(ry_o), 0, N - 1);
                const int ix = std::clamp((int)std::lround(rx_o), 0, N - 1);
                const size_t idx = (size_t)r_o * N + c_o;
                const bool ok = (ev_w[idx] > 1e-5f) && (od_w[(size_t)iy * N + ix] > 1e-5f) &&
                                (ry_o >= 1.0f && ry_o <= (float)(N - 2) && rx_o >= 1.0f && rx_o <= (float)(N - 2));
                valid_m[idx] = ok ? 1u : 0u;
                p_odd[idx] = ok ? detail::sample_bicubic_clamped(p_odd_nat.data(), N, N, 1, 0, ry_o, rx_o) : 0.0f;
                if (ok) {
                    const double w = hann[r_o] * hann[c_o];
                    w_win_sum += w;
                    mean_a += w * (double)p_even[idx];
                    mean_b += w * (double)p_odd[idx];
                    ++valid_px;
                }
            }
        }
        if (valid_px < (N * N) / 2 || w_win_sum <= 1e-9) continue;
        mean_a /= w_win_sum;
        mean_b /= w_win_sum;

        for (int r_o = 0; r_o < N; ++r_o) {
            const double wr = hann[r_o];
            for (int c_o = 0; c_o < N; ++c_o) {
                const size_t idx = (size_t)r_o * N + c_o;
                if (!valid_m[idx]) {
                    fa[idx] = {0.0, 0.0};
                    fb[idx] = {0.0, 0.0};
                } else {
                    const double w = wr * hann[c_o];
                    fa[idx] = {w * ((double)p_even[idx] - mean_a), 0.0};
                    fb[idx] = {w * ((double)p_odd[idx] - mean_b), 0.0};
                }
            }
        }
        detail::fft2d_radix2(fa, N);
        detail::fft2d_radix2(fb, N);

        PatchRingStat ps{};
        ps.v1 = (pm.w_even_frame_sum > 1e-9 && pm.w2_even_frame_sum > 0.0)
            ? (pm.w2_even_frame_sum / (pm.w_even_frame_sum * pm.w_even_frame_sum))
            : (1.0 / std::max(1, acc.n_even_frames));
        ps.v2 = (pm.w_odd_frame_sum > 1e-9 && pm.w2_odd_frame_sum > 0.0)
            ? (pm.w2_odd_frame_sum / (pm.w_odd_frame_sum * pm.w_odd_frame_sum))
            : (1.0 / std::max(1, acc.n_odd_frames));
        const double w_tot = pm.w_even_frame_sum + pm.w_odd_frame_sum;
        const double w2_tot = pm.w2_even_frame_sum + pm.w2_odd_frame_sum;
        ps.v_full = (w_tot > 1e-9 && w2_tot > 0.0)
            ? (w2_tot / (w_tot * w_tot))
            : ((ps.v1 * ps.v2) / std::max(1e-18, ps.v1 + ps.v2));

        // Count conjugate half-plane ONLY ONCE: kx > 0 || (kx == 0 && ky > 0)
        for (int r = 0; r < N; ++r) {
            const int ky = (r <= nh) ? r : (r - N);
            for (int c = 0; c < N; ++c) {
                const int kx = (c <= nh) ? c : (c - N);
                if (kx < 0 || (kx == 0 && ky <= 0)) continue;
                const double rad = std::hypot((double)kx, (double)ky) / (double)nh;
                if (rad >= 1.0) continue;
                const int bin = std::min(BINS - 1, (int)(rad * (double)BINS));
                const auto& A = fa[(size_t)r * N + c];
                const auto& B = fb[(size_t)r * N + c];
                const double c_re = A.real() * B.real() + A.imag() * B.imag();
                const double na = std::norm(A);
                const double nb = std::norm(B);
                ps.num[bin] += c_re;
                ps.da[bin] += na;
                ps.db[bin] += nb;

                // 4 directional 45-deg sectors in [0, pi): 0=H(0deg), 1=D45, 2=V(90deg), 3=D135
                double ang = std::atan2((double)ky, (double)kx); // in (-pi/2, pi/2] since kx >= 0
                if (ang < 0.0) ang += 3.14159265358979323846;
                const int sec = ((int)std::floor((ang + 0.39269908169872415) / 0.7853981633974483)) & 3;
                ps.dir_num[sec][bin] += c_re;
                ps.dir_da[sec][bin] += na;
                ps.dir_db[sec][bin] += nb;
            }
        }
        pstats.push_back(ps);
    }

    sc.patches_used = (int)pstats.size();
    if (sc.patches_used < 2) {
        sc.verdict = "INSUFFICIENT_EVIDENCE";
        sc.bottleneck = "missing_evidence";
        return sc;
    }

    double tot_num[BINS] = {}, tot_da[BINS] = {}, tot_db[BINS] = {};
    double tot_dir_num[4][BINS] = {}, tot_dir_da[4][BINS] = {}, tot_dir_db[4][BINS] = {};
    double v1_mean = 0.0, v2_mean = 0.0, v_full_mean = 0.0;
    for (const auto& ps : pstats) {
        v1_mean += ps.v1;
        v2_mean += ps.v2;
        v_full_mean += ps.v_full;
        for (int b = 0; b < BINS; ++b) {
            tot_num[b] += ps.num[b];
            tot_da[b] += ps.da[b];
            tot_db[b] += ps.db[b];
            for (int d = 0; d < 4; ++d) {
                tot_dir_num[d][b] += ps.dir_num[d][b];
                tot_dir_da[d][b] += ps.dir_da[d][b];
                tot_dir_db[d][b] += ps.dir_db[d][b];
            }
        }
    }
    v1_mean /= (double)sc.patches_used;
    v2_mean /= (double)sc.patches_used;
    v_full_mean /= (double)sc.patches_used;

    // Delete-1-patch jackknife standard error for isotropic & directional rings
    double dir_se[4][BINS] = {};
    const int M = sc.patches_used;
    for (int b = 0; b < BINS; ++b) {
        const double den = std::sqrt(tot_da[b] * tot_db[b]);
        sc.ring_frc_half[b] = (den > 1e-18) ? std::clamp(tot_num[b] / den, -1.0, 1.0) : 0.0;
        if (M >= 2) {
            double jk_mean = 0.0;
            std::vector<double> r_loo((size_t)M, 0.0);
            for (int m = 0; m < M; ++m) {
                const double nm = tot_num[b] - pstats[(size_t)m].num[b];
                const double dam = std::max(0.0, tot_da[b] - pstats[(size_t)m].da[b]);
                const double dbm = std::max(0.0, tot_db[b] - pstats[(size_t)m].db[b]);
                const double dm = std::sqrt(dam * dbm);
                r_loo[(size_t)m] = (dm > 1e-18) ? std::clamp(nm / dm, -1.0, 1.0) : 0.0;
                jk_mean += r_loo[(size_t)m];
            }
            jk_mean /= (double)M;
            double jk_var = 0.0;
            for (int m = 0; m < M; ++m) {
                const double dv = r_loo[(size_t)m] - jk_mean;
                jk_var += dv * dv;
            }
            sc.ring_frc_se[b] = std::sqrt(((double)(M - 1) / (double)M) * jk_var);
        }
        const auto conv = convert_unequal_split_frc(sc.ring_frc_half[b], v1_mean, v2_mean, v_full_mean);
        sc.ring_frc_full[b] = conv.frc_full_rep;

        for (int d = 0; d < 4; ++d) {
            if (M >= 2) {
                double jkm = 0.0;
                std::vector<double> r_d((size_t)M, 0.0);
                for (int m = 0; m < M; ++m) {
                    const double nm = tot_dir_num[d][b] - pstats[(size_t)m].dir_num[d][b];
                    const double dam = std::max(0.0, tot_dir_da[d][b] - pstats[(size_t)m].dir_da[d][b]);
                    const double dbm = std::max(0.0, tot_dir_db[d][b] - pstats[(size_t)m].dir_db[d][b]);
                    const double dm = std::sqrt(dam * dbm);
                    r_d[(size_t)m] = (dm > 1e-18) ? std::clamp(nm / dm, -1.0, 1.0) : 0.0;
                    jkm += r_d[(size_t)m];
                }
                jkm /= (double)M;
                double jkv = 0.0;
                for (int m = 0; m < M; ++m) {
                    const double dv = r_d[(size_t)m] - jkm;
                    jkv += dv * dv;
                }
                dir_se[d][b] = std::sqrt(((double)(M - 1) / (double)M) * jkv);
            }
        }
    }

    // Interpolate FRC50 and FRC0.143 on both raw half-split and A14.3 converted full-stack curves
    auto interp_crossing = [&](const double* curve, double thr) -> double {
        double prev_v = 1.0, prev_f = 0.0;
        for (int b = 0; b < BINS; ++b) {
            const double f = sc.ring_freq_cy_1x[b];
            const double v = curve[b];
            if (v < thr) {
                const double frac = (prev_v - thr) / std::max(1e-9, prev_v - v);
                return prev_f + frac * (f - prev_f);
            }
            prev_v = v;
            prev_f = f;
        }
        return nyq_cy_1x;
    };
    sc.frc50_half_cy_1x = interp_crossing(sc.ring_frc_half, 0.50);
    sc.frc143_half_cy_1x = interp_crossing(sc.ring_frc_half, 0.143);
    sc.frc50_full_cy_1x = interp_crossing(sc.ring_frc_full, 0.50);
    sc.frc143_full_cy_1x = interp_crossing(sc.ring_frc_full, 0.143);

    // A14.2: Connected supported directional bandwidth f_L(theta) from low frequency (at least 2 contiguous rings,
    // full-stack FRC 90% jackknife lower confidence bound >= 0.143 [s_F >= 1/6], never jumping across zeros!)
    const double phi_dir[4] = {sc.phi_x_l, sc.phi_l, sc.phi_y_l, sc.phi_l};
    for (int d = 0; d < 4; ++d) {
        int last_ok_bin = -1;
        int consec_ok = 0;
        for (int b = 0; b < BINS; ++b) {
            const double f_cy = sc.ring_freq_cy_1x[b];
            // Above native 0.50 cy/px_1x, directional alias completeness must also hold!
            if (f_cy > 0.50 && phi_dir[d] < 0.30) break;
            const double den = std::sqrt(tot_dir_da[d][b] * tot_dir_db[d][b]);
            const double r_d = (den > 1e-18) ? std::clamp(tot_dir_num[d][b] / den, -1.0, 1.0) : 0.0;
            const double r_lcb = r_d - 1.28155 * dir_se[d][b];
            const auto conv_lcb = convert_unequal_split_frc(r_lcb, v1_mean, v2_mean, v_full_mean);
            if (conv_lcb.frc_full_rep >= 0.143 && r_lcb > 0.04) {
                last_ok_bin = b;
                ++consec_ok;
            } else {
                break; // Connected passband stops at first zero/sub-threshold ring!
            }
        }
        sc.f_dir_cy_1x[d] = (consec_ok >= 2 && last_ok_bin >= 1) ? sc.ring_freq_cy_1x[last_ok_bin] : 0.0;
    }

    std::array<double, 4> sorted_dir = {sc.f_dir_cy_1x[0], sc.f_dir_cy_1x[1], sc.f_dir_cy_1x[2], sc.f_dir_cy_1x[3]};
    std::sort(sorted_dir.begin(), sorted_dir.end());
    sc.f_min_cy_1x = sorted_dir[0];
    sc.f_median_cy_1x = 0.5 * (sorted_dir[1] + sorted_dir[2]);
    sc.f_max_cy_1x = sorted_dir[3];
    // Strict conservative bidirectional lower bound: f_l_cy_1x is ALWAYS min_{d=0..3} f_dir[d].
    // Strong-axis and median directional bandwidths are reported separately for user inspection.
    sc.f_l_cy_1x = sc.f_min_cy_1x;
    if (sensor_pitch_um > 0.1) {
        sc.f_l_lp_mm = (1000.0 * sc.f_l_cy_1x) / sensor_pitch_um;
    }
    sc.f_l_lw_ph = 2.0 * sc.f_l_cy_1x * (double)acc.H1;

    // Supported area inside [-1, 1]^2 (native Nyquist square [-0.5, 0.5]^2 has area 1.0; 2x square has area 4.0)
    double area_acc = 0.0;
    for (int d = 0; d < 4; ++d) {
        // Quarter-sector of disk clipped to [-1, 1]^2 output Nyquist square
        const double r = std::clamp(sc.f_dir_cy_1x[d], 0.0, (double)nyq_cy_1x * 1.41421356);
        const double half_side = nyq_cy_1x;
        double sec_area = 0.0;
        if (r <= half_side) {
            sec_area = 0.25 * 3.14159265358979323846 * r * r;
        } else if (r >= half_side * 1.41421356237) {
            sec_area = half_side * half_side;
        } else {
            const double alpha = std::acos(half_side / r);
            sec_area = half_side * std::sqrt(r * r - half_side * half_side) +
                       0.25 * r * r * (3.14159265358979323846 - 4.0 * alpha);
        }
        area_acc += sec_area;
    }
    sc.supported_area_vs_1x = std::clamp(area_acc, 0.0, 4.0);
    sc.mp_band = std::min(4.0 * sc.mp_1x, sc.mp_1x * sc.supported_area_vs_1x);
    sc.valid = true;

    // Part III.3 Priority-Ordered Conservative Bidirectional Suitability Decision Table
    const double max_1d_f = std::max(sc.f_dir_cy_1x[0], sc.f_dir_cy_1x[2]);
    if (sc.f_max_cy_1x <= 0.05 && max_1d_f <= 0.05) {
        sc.verdict = "INSUFFICIENT_EVIDENCE";
        sc.bottleneck = "missing_evidence";
    } else if (sc.f_l_cy_1x <= 0.52 || sc.phi_l < 0.35 || sc.n_eff_p15 < 3.2 || !sc.bi_rank_ok) {
        sc.verdict = "RECOMMEND_1X";
        if (!sc.bi_rank_ok && max_1d_f > 0.56 && std::max(sc.phi_x_l, sc.phi_y_l) >= 0.50) {
            sc.bottleneck = "single_axis_only";
        } else if (sc.phi_l < 0.35) {
            sc.bottleneck = "phase_coverage";
        } else if (sc.n_eff_p15 < 3.2) {
            sc.bottleneck = "motion_rejection";
        } else if (fnum_aperture >= 11.0 && sensor_pitch_um > 0.1 && (1000.0 * 0.52 / sensor_pitch_um) > (1200.0 / fnum_aperture)) {
            sc.bottleneck = "diffraction";
        } else if (sc.f_max_cy_1x > 0.52 && sc.f_l_cy_1x <= 0.52) {
            sc.bottleneck = "anisotropic_weak_axis";
        } else {
            sc.bottleneck = "noise";
        }
    } else if (sc.f_l_cy_1x > 0.68 && sc.phi_l >= 0.55) {
        sc.verdict = "STRONG_2X";
        sc.bottleneck = "none";
    } else {
        sc.verdict = "MARGINAL_2X";
        sc.bottleneck = (sc.phi_l < 0.55) ? "phase_coverage" : "noise";
    }
    return sc;
}

inline std::string format_suitability_cli_banner(const BurstSuitabilityScorecard& sc) {
    const std::string lp_str = (sc.f_l_lp_mm > 0.0)
        ? (", " + std::to_string((int)std::lround(sc.f_l_lp_mm)) + " lp/mm")
        : std::string();
    char buf[1200];
    std::snprintf(
        buf, sizeof(buf),
        "  [Metrics] Verdict=%s (bottleneck=%s) | N=%d (even=%d, odd=%d, N_eff_P15=%.2f) | SNR_gain=+%.2f dB\n"
        "  [Metrics] Phase Schur Phi_L=%.3f (H=%.3f, V=%.3f) | f_L(min)=%.3f, f_med=%.3f, f_max=%.3f cy/px_1x (%.0f LW/PH%s) | MP_band=%.1f / %.1f MP (%.2fx area)\n"
        "  [Metrics] Split-Half FRC50=%.3f, FRC0.143=%.3f cy/px_1x (%d patches) | eSFR MTF50=%.3f cy/px_1x, overshoot=%.2f%%, fringe=%.3f px_1x\n",
        sc.verdict.c_str(), sc.bottleneck.c_str(),
        sc.n_frames, sc.n_even, sc.n_odd, sc.n_eff_p15, sc.snr_gain_db,
        sc.phi_l, sc.phi_x_l, sc.phi_y_l,
        sc.f_l_cy_1x, sc.f_median_cy_1x, sc.f_max_cy_1x, sc.f_l_lw_ph, lp_str.c_str(),
        sc.mp_band, sc.mp_1x, sc.supported_area_vs_1x,
        sc.frc50_full_cy_1x, sc.frc143_full_cy_1x, sc.patches_used,
        sc.esfr_mtf50_cy_1x, sc.esfr_overshoot_pct, sc.residual_fringe_1x_px
    );
    return std::string(buf);
}

inline bool write_suitability_metrics_json(const std::string& utf8_path, const BurstSuitabilityScorecard& sc) {
    if (utf8_path.empty()) return false;
    std::error_code ec;
    const auto p_fs = heraw_fs::from_utf8(utf8_path);
    if (std::filesystem::is_directory(p_fs, ec)) return false;
    std::ofstream f(p_fs, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(6);
    ss << "{\n"
       << "  \"schema\": \"nijin.metrics.v2\",\n"
       << "  \"valid\": " << (sc.valid ? "true" : "false") << ",\n"
       << "  \"verdict\": \"" << sc.verdict << "\",\n"
       << "  \"bottleneck\": \"" << sc.bottleneck << "\",\n"
       << "  \"repeatability_kind\": \"" << sc.repeatability_kind << "\",\n"
       << "  \"n_frames\": " << sc.n_frames << ",\n"
       << "  \"unpacked_frames\": " << sc.unpacked_frames << ",\n"
       << "  \"n_even\": " << sc.n_even << ",\n"
       << "  \"n_odd\": " << sc.n_odd << ",\n"
       << "  \"out_scale\": " << sc.out_scale << ",\n"
       << "  \"patches_used\": " << sc.patches_used << ",\n"
       << "  \"n_eff_p15\": " << sc.n_eff_p15 << ",\n"
       << "  \"n_eff_median\": " << sc.n_eff_median << ",\n"
       << "  \"snr_gain_db\": " << sc.snr_gain_db << ",\n"
       << "  \"nominal_weight_snr_gain_db\": " << sc.snr_gain_db << ",\n"
       << "  \"phi_schur_2d\": " << sc.phi_l << ",\n"
       << "  \"f_l_max_1x\": " << sc.f_l_cy_1x << ",\n"
       << "  \"f_min_cy_1x\": " << sc.f_min_cy_1x << ",\n"
       << "  \"f_median_cy_1x\": " << sc.f_median_cy_1x << ",\n"
       << "  \"f_max_cy_1x\": " << sc.f_max_cy_1x << ",\n"
       << "  \"mp_band_est\": " << sc.mp_band << ",\n"
       << "  \"frc143_cy_1x\": " << sc.frc143_full_cy_1x << ",\n"
       << "  \"mtf50_1x\": " << sc.esfr_mtf50_cy_1x << ",\n"
       << "  \"chroma_fringe_px\": " << sc.residual_fringe_1x_px << ",\n"
       << "  \"alias_schur\": {\n"
       << "    \"phi_l\": " << sc.phi_l << ",\n"
       << "    \"phi_x_l\": " << sc.phi_x_l << ",\n"
       << "    \"phi_y_l\": " << sc.phi_y_l << ",\n"
       << "    \"bi_rank_ok\": " << (sc.bi_rank_ok ? "true" : "false") << "\n"
       << "  },\n"
       << "  \"bandwidth\": {\n"
       << "    \"f_l_cy_1x\": " << sc.f_l_cy_1x << ",\n"
       << "    \"f_min_cy_1x\": " << sc.f_min_cy_1x << ",\n"
       << "    \"f_median_cy_1x\": " << sc.f_median_cy_1x << ",\n"
       << "    \"f_max_cy_1x\": " << sc.f_max_cy_1x << ",\n"
       << "    \"f_dir_cy_1x\": [" << sc.f_dir_cy_1x[0] << ", " << sc.f_dir_cy_1x[1] << ", "
       << sc.f_dir_cy_1x[2] << ", " << sc.f_dir_cy_1x[3] << "],\n"
       << "    \"f_l_lp_mm\": " << sc.f_l_lp_mm << ",\n"
       << "    \"f_l_lw_ph\": " << sc.f_l_lw_ph << ",\n"
       << "    \"mp_1x\": " << sc.mp_1x << ",\n"
       << "    \"mp_band\": " << sc.mp_band << ",\n"
       << "    \"supported_area_vs_1x\": " << sc.supported_area_vs_1x << "\n"
       << "  },\n"
       << "  \"split_half_frc\": {\n"
       << "    \"frc50_half_cy_1x\": " << sc.frc50_half_cy_1x << ",\n"
       << "    \"frc143_half_cy_1x\": " << sc.frc143_half_cy_1x << ",\n"
       << "    \"frc50_full_cy_1x\": " << sc.frc50_full_cy_1x << ",\n"
       << "    \"frc143_full_cy_1x\": " << sc.frc143_full_cy_1x << "\n"
       << "  },\n"
       << "  \"post_render_esfr\": {\n"
       << "    \"mtf50_cy_1x\": " << sc.esfr_mtf50_cy_1x << ",\n"
       << "    \"overshoot_pct\": " << sc.esfr_overshoot_pct << ",\n"
       << "    \"residual_fringe_1x_px\": " << sc.residual_fringe_1x_px << "\n"
       << "  },\n"
       << "  \"loo_chi2_measured\": " << (sc.loo_chi2_measured ? "true" : "false") << ",\n"
       << "  \"loo_chi2_all\": " << (sc.loo_chi2_measured ? std::to_string(sc.loo_chi2_all) : "null") << "\n"
       << "}\n";
    const std::string payload = ss.str();
    f.write(payload.data(), (std::streamsize)payload.size());
    f.flush();
    return (bool)f;
}

} // namespace heraw_eval
