#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    FrcCurveReport half_burst_frc;
    PhaseDiversityReport phase_div;
    EsfrMtfReport esfr;
    double residual_noise_sfm = 0.0;
    double aliasing_chroma_ratio = 0.0;
    std::string mode_label;
    int scale_factor = 1;
    int frame_count = 0;
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
    const EvalOpticsParams& optics = {}
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

                float var_i = min_sig * min_sig + std::max(0.0f, gain) * std::max(0.0f, pred_obs_space);
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
    const EvalOpticsParams& optics = {}
) {
    SelfEvalReport rep;
    rep.mode_label = mode_label;
    rep.scale_factor = out_scale;
    rep.frame_count = (int)frames_lin.size();
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int N = (int)frames_lin.size();
    if (!pred_rgb || N == 0 || !cfa6x6 || H1 < 64 || W1 < 64) return rep;

    // 1. E04 fix: Full-CFA stride-1 LOO chi2 across ALL R/G/B phases (including prediction variance term 1 + 1/max(1, N-1)).
    const int k_hold = (N >= 2) ? 1 : 0;
    rep.loo_chi2 = compute_stratified_loo_chi2(
        pred_rgb, H_out, W_out, out_scale,
        frames_lin[k_hold].data(), H1, W1,
        flows_fy[k_hold].data(), flows_fx[k_hold].data(), Hh, Wh,
        cfa6x6, sig0, gain, mw_half[k_hold].data(), 1.0f, 0.78f, 1, optics);

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
    std::fprintf(f,
        "{\n"
        "  \"mode\": \"%s\",\n"
        "  \"scale\": %d,\n"
        "  \"frame_count\": %d,\n"
        "  \"loo_chi2\": {\n"
        "    \"valid\": %s,\n"
        "    \"all\": %.6f,\n"
        "    \"flat\": %.6f,\n"
        "    \"tex\": %.6f,\n"
        "    \"edge\": %.6f,\n"
        "    \"bias_norm\": %.6f,\n"
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
        rep.loo_chi2.valid ? "true" : "false",
        rep.loo_chi2.chi2_all, rep.loo_chi2.chi2_flat, rep.loo_chi2.chi2_tex, rep.loo_chi2.chi2_edge,
        rep.loo_chi2.bias_norm, rep.loo_chi2.n_flat, rep.loo_chi2.n_tex, rep.loo_chi2.n_edge,
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

} // namespace heraw_eval
