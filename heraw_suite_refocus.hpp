// heraw_suite_refocus.hpp -- Optical Light-Field Bokeh, Moment Prefix Sums,
// 4D Synthetic-Aperture Ray Integration & Bessel Shift-Focus Deconvolution.
// C++20, header-only, strictly 7-bit ASCII.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace heraw_refocus {

using RowTask = std::function<void(int, int)>;
using ParallelRows = std::function<void(int rows, const RowTask& task)>;

enum class ApertureWeightMode { Voronoi = 0, Dwell = 1 };

struct RefocusParams {
    float focus_depth = 0.5f;
    float radius_px = 12.0f;
    int max_radius = 64;
    bool layered_occlusion = false;
    int aperture_blades = 0;
    float catseye_vignette = 0.0f;
    const float* revealed_bg_rgb = nullptr;       // 3 * H * W camera-linear RGB
    const float* revealed_bg_depth = nullptr;     // depth_h * depth_w or H * W in [0, 1]
    int revealed_bg_depth_h = 0;
    int revealed_bg_depth_w = 0;
    float tilt_x = 0.0f;
    float tilt_y = 0.0f;
    float spherical_aberration = 0.0f;            // [-0.5, +0.5], negative = creamy bg / bright fg rim
    float stf_apodization = 0.0f;                 // [0, 1], smooth radial roll-off
    float blade_angle_deg = 0.0f;
    float native_coc_slope_px = 0.0f;             // Captured CoC slope in output px per unit depth
    float native_focus_depth = 0.5f;
    bool restore_glints = false;
    const std::array<float, 3>* illuminant_rgb = nullptr;
    ApertureWeightMode weight_mode = ApertureWeightMode::Voronoi;
};

namespace detail {

inline float clamp01(float x) noexcept { return std::max(0.0f, std::min(1.0f, x)); }

inline float sample_bilinear(const float* p, int H, int W, float y, float x) noexcept {
    y = std::max(0.0f, std::min(static_cast<float>(H - 1), y));
    x = std::max(0.0f, std::min(static_cast<float>(W - 1), x));
    const int y0 = static_cast<int>(y), x0 = static_cast<int>(x);
    const int y1 = std::min(y0 + 1, H - 1), x1 = std::min(x0 + 1, W - 1);
    const float ty = y - y0, tx = x - x0;
    return (1.0f - ty) * ((1.0f - tx) * p[static_cast<size_t>(y0) * W + x0] +
                          tx * p[static_cast<size_t>(y0) * W + x1]) +
           ty * ((1.0f - tx) * p[static_cast<size_t>(y1) * W + x0] +
                 tx * p[static_cast<size_t>(y1) * W + x1]);
}

// Floating-point aperture row span [x_lo, x_hi] at row offset dy for real radius r >= 0.
inline void polygon_row_span_float(float dy, float r, int blades, float angle_rad,
                                   float& x_lo, float& x_hi) noexcept {
    if (r <= 1e-6f || std::fabs(dy) > r) { x_lo = 1.0f; x_hi = -1.0f; return; }
    if (blades < 5) {
        const float ex = std::sqrt(std::max(0.0f, r * r - dy * dy));
        x_lo = -ex; x_hi = ex;
        return;
    }
    const float ap = r * std::cos(std::numbers::pi_v<float> / static_cast<float>(blades));
    float lo = -r, hi = r;
    for (int m = 0; m < blades; ++m) {
        const float th = angle_rad + 2.0f * std::numbers::pi_v<float> * static_cast<float>(m) / static_cast<float>(blades);
        const float cx = std::cos(th), cy = std::sin(th);
        const float rhs = ap - dy * cy;
        if (std::fabs(cx) < 1e-5f) {
            if (rhs < 0.0f) { x_lo = 1.0f; x_hi = -1.0f; return; }
        } else if (cx > 0.0f) {
            hi = std::min(hi, rhs / cx);
        } else {
            lo = std::max(lo, rhs / cx);
        }
    }
    x_lo = lo;
    x_hi = hi;
}

// Compute effective per-pixel radial weights c0 + c2 * (dx^2 + dy^2) / R^2 >= 0.05.
inline void radial_poly_coeffs(float sa, float stf, float s_sign, float& c0, float& c2) noexcept {
    const float beta = std::max(-0.5f, std::min(0.5f, -s_sign * sa));
    const float ap = std::max(0.0f, std::min(1.0f, stf));
    double k0 = 1.0 + 0.92 * static_cast<double>(ap) - static_cast<double>(beta);
    double k2 = 2.0 * static_cast<double>(beta) - 1.84 * static_cast<double>(ap);
    const double min_end = std::min(k0, k0 + k2);
    if (min_end < 0.08) {
        const double damp = (1.0 - 0.08) / std::max(1e-6, 1.0 - min_end);
        k0 = 1.0 + damp * (k0 - 1.0);
        k2 = damp * k2;
    }
    c0 = static_cast<float>(k0);
    c2 = static_cast<float>(k2);
}

// Reconstruct isolated specular point-source flux from unsaturated PSF skirts.
inline void boost_clipped_glints(const float* src_rgb, int H, int W,
                                 const std::array<float, 3>* illum,
                                 std::vector<float>& dst_rgb,
                                 const ParallelRows& parallel_rows) {
    const size_t p = static_cast<size_t>(H) * W;
    dst_rgb.assign(src_rgb, src_rgb + 3 * p);
    if (H < 16 || W < 16) return;
    const RowTask glint_rows = [&](int r0, int r1) {
        for (int y = std::max(5, r0); y < std::min(H - 5, r1); ++y) {
            for (int x = 5; x < W - 5; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                const float r = src_rgb[3 * i + 0], g = src_rgb[3 * i + 1], b = src_rgb[3 * i + 2];
                const float pk = std::max({r, g, b});
                if (pk < 0.94f) continue;
                double ring_bg = 0.0;
                int bg_cnt = 0;
                for (int dy = -5; dy <= 5; ++dy) for (int dx = -5; dx <= 5; ++dx) {
                    const int d2 = dy * dy + dx * dx;
                    if (d2 >= 18 && d2 <= 26) {
                        const size_t q = static_cast<size_t>(y + dy) * W + (x + dx);
                        ring_bg += static_cast<double>(src_rgb[3 * q + 1]);
                        ++bg_cnt;
                    }
                }
                if (bg_cnt == 0) continue;
                const float bg_g = static_cast<float>(ring_bg / bg_cnt);
                if (bg_g > 0.65f) continue; // Bright sky/wall area, not a compact glint.
                double skirt_num_g = 0.0, skirt_den = 0.0;
                int skirt_cnt = 0, clipped_skirt = 0;
                for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
                    const int d2 = dy * dy + dx * dx;
                    if (d2 < 1 || d2 > 10) continue;
                    const size_t q = static_cast<size_t>(y + dy) * W + (x + dx);
                    const float qpk = std::max({src_rgb[3 * q + 0], src_rgb[3 * q + 1], src_rgb[3 * q + 2]});
                    if (qpk >= 0.92f) { ++clipped_skirt; continue; }
                    if (d2 < 4) continue;
                    const double psf_w = std::exp(-static_cast<double>(d2) / (2.0 * 1.45 * 1.45));
                    skirt_num_g += std::max(0.0, static_cast<double>(src_rgb[3 * q + 1]) - bg_g);
                    skirt_den += psf_w;
                    ++skirt_cnt;
                }
                if (clipped_skirt <= 5 && skirt_cnt >= 4 && skirt_den > 1e-4) {
                    const double gain_g = skirt_num_g / skirt_den;
                    if (gain_g > pk * 1.15) {
                        const float capped_g = static_cast<float>(std::min(16.0, std::max(static_cast<double>(pk), gain_g)));
                        if (illum && (*illum)[1] > 1e-4f) {
                            for (int c = 0; c < 3; ++c)
                                dst_rgb[3 * i + c] = std::max(src_rgb[3 * i + c], capped_g * ((*illum)[c] / (*illum)[1]));
                        } else {
                            const float sc = capped_g / std::max(1e-4f, g);
                            for (int c = 0; c < 3; ++c) dst_rgb[3 * i + c] = std::max(src_rgb[3 * i + c], src_rgb[3 * i + c] * sc);
                        }
                    }
                }
            }
        }
    };
    if (parallel_rows) parallel_rows(H, glint_rows); else glint_rows(0, H);
}

inline void fft_1d(std::vector<double>& re, std::vector<double>& im, bool inverse) {
    const size_t n = re.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    const double sgn = inverse ? 1.0 : -1.0;
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = sgn * 2.0 * std::numbers::pi / static_cast<double>(len);
        const double wlen_r = std::cos(ang), wlen_i = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            double wr = 1.0, wi = 0.0;
            for (size_t j = 0; j < len / 2; ++j) {
                const size_t u = i + j, v = i + j + len / 2;
                const double tr = re[v] * wr - im[v] * wi;
                const double ti = re[v] * wi + im[v] * wr;
                re[v] = re[u] - tr; im[v] = im[u] - ti;
                re[u] += tr; im[u] += ti;
                const double nwr = wr * wlen_r - wi * wlen_i;
                wi = wr * wlen_i + wi * wlen_r;
                wr = nwr;
            }
        }
    }
    if (inverse) {
        const double inv_n = 1.0 / static_cast<double>(n);
        for (size_t i = 0; i < n; ++i) { re[i] *= inv_n; im[i] *= inv_n; }
    }
}

inline void fft_2d(std::vector<double>& re, std::vector<double>& im, int N, bool inverse) {
    std::vector<double> tr(static_cast<size_t>(N)), ti(static_cast<size_t>(N));
    for (int y = 0; y < N; ++y) {
        const size_t off = static_cast<size_t>(y) * N;
        std::copy_n(re.data() + off, N, tr.data());
        std::copy_n(im.data() + off, N, ti.data());
        fft_1d(tr, ti, inverse);
        std::copy_n(tr.data(), N, re.data() + off);
        std::copy_n(ti.data(), N, im.data() + off);
    }
    for (int x = 0; x < N; ++x) {
        for (int y = 0; y < N; ++y) {
            tr[static_cast<size_t>(y)] = re[static_cast<size_t>(y) * N + x];
            ti[static_cast<size_t>(y)] = im[static_cast<size_t>(y) * N + x];
        }
        fft_1d(tr, ti, inverse);
        for (int y = 0; y < N; ++y) {
            re[static_cast<size_t>(y) * N + x] = tr[static_cast<size_t>(y)];
            im[static_cast<size_t>(y) * N + x] = ti[static_cast<size_t>(y)];
        }
    }
}

// Pillbox optical transfer function H(w) = 2 * J1(R * w) / (R * w) via convergent
// Bessel power-series recurrence (machine-precision across first lobe, zero libstdc++ dependency).
inline double pillbox_otf(double R, double omega_mag) noexcept {
    const double x = R * omega_mag;
    if (x < 1e-4) return 1.0 - 0.125 * x * x;
    if (x <= 12.0) {
        const double q = -0.25 * x * x;
        double sum = 1.0, term = 1.0;
        for (int k = 1; k <= 28; ++k) {
            term *= q / (static_cast<double>(k) * static_cast<double>(k + 1));
            sum += term;
            if (std::fabs(term) < 1e-16) break;
        }
        return sum;
    }
    const double chi = x - 0.75 * std::numbers::pi, inv_x = 1.0 / x, inv_x2 = inv_x * inv_x;
    const double p1 = 1.0 - (15.0 / 128.0) * inv_x2;
    const double q1 = inv_x * (0.375 - (105.0 / 1024.0) * inv_x2);
    return 2.0 * std::sqrt(2.0 / (std::numbers::pi * x)) * (p1 * std::cos(chi) - q1 * std::sin(chi)) * inv_x;
}

} // namespace detail

inline std::vector<float> render_refocus(const float* image_rgb, int H, int W,
                                         const float* depth, int depth_h, int depth_w,
                                         const RefocusParams& opts,
                                         const ParallelRows& parallel_rows = {}) {
    if (!image_rgb || !depth || H <= 0 || W <= 0 || depth_h <= 0 || depth_w <= 0)
        throw std::invalid_argument("Invalid refocus dimensions");
    if (!std::isfinite(opts.focus_depth) || !std::isfinite(opts.radius_px) ||
        opts.radius_px < 0.0f || opts.max_radius < 0 || opts.max_radius > 256 ||
        !std::isfinite(opts.catseye_vignette) || !std::isfinite(opts.blade_angle_deg) ||
        !std::isfinite(opts.native_coc_slope_px) || !std::isfinite(opts.native_focus_depth) ||
        !std::isfinite(opts.tilt_x) || !std::isfinite(opts.tilt_y) ||
        !std::isfinite(opts.spherical_aberration) || !std::isfinite(opts.stf_apodization))
        throw std::invalid_argument("Invalid refocus options");
    const size_t p = static_cast<size_t>(H) * static_cast<size_t>(W);
    const size_t dp = static_cast<size_t>(depth_h) * static_cast<size_t>(depth_w);
    for (size_t i = 0; i < dp; ++i)
        if (!std::isfinite(depth[i])) throw std::invalid_argument("Nonfinite relative depth");

    std::vector<float> out(image_rgb, image_rgb + 3 * p);
    if (opts.radius_px <= 0.0f || opts.max_radius <= 0) return out;

    std::vector<float> glint_rgb;
    const float* active_src_rgb = image_rgb;
    if (opts.restore_glints) {
        detail::boost_clipped_glints(image_rgb, H, W, opts.illuminant_rgb, glint_rgb, parallel_rows);
        active_src_rgb = glint_rgb.data();
        out.assign(active_src_rgb, active_src_rgb + 3 * p);
    }

    const bool use_layered_engine = opts.layered_occlusion ||
        std::fabs(opts.spherical_aberration) > 1e-3f || opts.stf_apodization > 1e-3f ||
        opts.aperture_blades >= 5;
    if (use_layered_engine) {
        constexpr int K_STRATA = 32;
        constexpr int BAND_H = 512;
        const bool use_3disc_stf = (opts.stf_apodization > 1e-3f &&
            std::fabs(opts.spherical_aberration) <= 1e-3f && opts.aperture_blades < 5);
        const bool has_moments = (std::fabs(opts.spherical_aberration) > 1e-3f ||
            (opts.stf_apodization > 1e-3f && !use_3disc_stf));
        const bool has_tilt = (std::fabs(opts.tilt_x) > 1e-4f || std::fabs(opts.tilt_y) > 1e-4f);
        const bool has_nat_coc = (opts.native_coc_slope_px > 1e-3f);
        const float angle_rad = opts.blade_angle_deg * (std::numbers::pi_v<float> / 180.0f);

        std::vector<float> pix_dfg(p, 0.0f), pix_dbg(p, 0.0f);
        std::vector<uint8_t> has_rev_bg(p, 0);
        const int bgdh = (opts.revealed_bg_depth && opts.revealed_bg_depth_h > 0) ? opts.revealed_bg_depth_h : depth_h;
        const int bgdw = (opts.revealed_bg_depth && opts.revealed_bg_depth_w > 0) ? opts.revealed_bg_depth_w : depth_w;

        const RowTask prep_depth = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) {
                const float sy = (y + 0.5f) * static_cast<float>(depth_h) / static_cast<float>(H) - 0.5f;
                const float syc = std::max(0.0f, std::min(static_cast<float>(depth_h - 1), sy));
                const int dy0 = static_cast<int>(std::floor(syc)), dy1 = std::min(depth_h - 1, dy0 + 1);
                const float fyd = syc - static_cast<float>(dy0);
                const float by = (y + 0.5f) * static_cast<float>(bgdh) / static_cast<float>(H) - 0.5f;
                const int sy_i = std::max(0, std::min(depth_h - 1, static_cast<int>(std::lround(sy))));
                for (int x = 0; x < W; ++x) {
                    const size_t i = static_cast<size_t>(y) * W + x;
                    const float sx = (x + 0.5f) * static_cast<float>(depth_w) / static_cast<float>(W) - 0.5f;
                    const float sxc = std::max(0.0f, std::min(static_cast<float>(depth_w - 1), sx));
                    const int dx0 = static_cast<int>(std::floor(sxc)), dx1 = std::min(depth_w - 1, dx0 + 1);
                    const float fxd = sxc - static_cast<float>(dx0);
                    const float d00 = depth[static_cast<size_t>(dy0) * depth_w + dx0];
                    const float d01 = depth[static_cast<size_t>(dy0) * depth_w + dx1];
                    const float d10 = depth[static_cast<size_t>(dy1) * depth_w + dx0];
                    const float d11 = depth[static_cast<size_t>(dy1) * depth_w + dx1];
                    const float dmin4 = std::min({d00, d01, d10, d11}), dmax4 = std::max({d00, d01, d10, d11});
                    float df = (dmax4 - dmin4 > 0.10f)
                        ? depth[static_cast<size_t>(fyd >= 0.5f ? dy1 : dy0) * depth_w + static_cast<size_t>(fxd >= 0.5f ? dx1 : dx0)]
                        : ((1.0f - fyd) * ((1.0f - fxd) * d00 + fxd * d01) + fyd * ((1.0f - fxd) * d10 + fxd * d11));
                    df = detail::clamp01(df);
                    pix_dfg[i] = df;
                    if (opts.revealed_bg_rgb) {
                        float db = df;
                        if (opts.revealed_bg_depth && opts.revealed_bg_depth_h > 0 && opts.revealed_bg_depth_w > 0) {
                            db = detail::clamp01(detail::sample_bilinear(
                                opts.revealed_bg_depth, bgdh, bgdw, by,
                                (x + 0.5f) * static_cast<float>(bgdw) / static_cast<float>(W) - 0.5f));
                        } else {
                            const int sx_i = std::max(0, std::min(depth_w - 1, static_cast<int>(std::lround(sx))));
                            for (int dym = -8; dym <= 8; dym += 2) {
                                const int rry = std::max(0, std::min(depth_h - 1, sy_i + dym));
                                for (int dxm = -8; dxm <= 8; dxm += 2) {
                                    const int rrx = std::max(0, std::min(depth_w - 1, sx_i + dxm));
                                    db = std::min(db, detail::clamp01(depth[static_cast<size_t>(rry) * depth_w + rrx]));
                                }
                            }
                        }
                        if (df - db >= 0.08f) {
                            pix_dbg[i] = db;
                            has_rev_bg[i] = 1;
                        } else {
                            pix_dbg[i] = df;
                        }
                    } else {
                        pix_dbg[i] = df;
                    }
                }
            }
        };
        if (parallel_rows) parallel_rows(H, prep_depth); else prep_depth(0, H);

        std::vector<int> stratum_used(K_STRATA, 0);

        const float img_cy = 0.5f * static_cast<float>(H - 1), img_cx = 0.5f * static_cast<float>(W - 1);
        const float inv_h_norm = 2.0f / std::max(1.0f, static_cast<float>(H));
        const float inv_w_norm = 2.0f / std::max(1.0f, static_cast<float>(W));
        const float inv_diag = 1.0f / std::max(1.0f, std::hypot(img_cy, img_cx));
        const float vig = detail::clamp01(opts.catseye_vignette);

        auto eval_radius = [&](float d_s, float y, float x) noexcept -> float {
            const float d_syn = has_tilt
                ? detail::clamp01(opts.focus_depth + opts.tilt_x * ((x - img_cx) * inv_w_norm) +
                                  opts.tilt_y * ((y - img_cy) * inv_h_norm))
                : opts.focus_depth;
            const float r_tgt = opts.radius_px * std::fabs(d_s - d_syn);
            if (!has_nat_coc) return std::min(static_cast<float>(opts.max_radius), r_tgt);
            const float r_nat = opts.native_coc_slope_px * std::fabs(d_s - opts.native_focus_depth);
            const float r_add = std::sqrt(std::max(0.0f, r_tgt * r_tgt - r_nat * r_nat));
            return std::min(static_cast<float>(opts.max_radius), r_add);
        };

        const size_t band_p = static_cast<size_t>(std::min(BAND_H, H)) * static_cast<size_t>(W);
        const int max_ext_span = BAND_H + 2 * opts.max_radius + 4;
        const size_t max_ext_h = static_cast<size_t>(std::min(H, max_ext_span));
        const size_t pref_stride = static_cast<size_t>(W) + 1;

        std::vector<double> tot_r(band_p), tot_g(band_p), tot_b(band_p), tot_a(band_p);
        std::vector<double> sh_r(band_p), sh_g(band_p), sh_b(band_p), sh_a(band_p), sheet_peak_a(band_p);
        std::vector<int> last_act_s(band_p, -1);

        std::vector<double> pref_r0(max_ext_h * pref_stride), pref_g0(max_ext_h * pref_stride),
                            pref_b0(max_ext_h * pref_stride), pref_a0(max_ext_h * pref_stride);
        std::vector<double> pref_r1(has_moments ? max_ext_h * pref_stride : 0),
                            pref_g1(has_moments ? max_ext_h * pref_stride : 0),
                            pref_b1(has_moments ? max_ext_h * pref_stride : 0),
                            pref_a1(has_moments ? max_ext_h * pref_stride : 0);
        std::vector<double> pref_r2(has_moments ? max_ext_h * pref_stride : 0),
                            pref_g2(has_moments ? max_ext_h * pref_stride : 0),
                            pref_b2(has_moments ? max_ext_h * pref_stride : 0),
                            pref_a2(has_moments ? max_ext_h * pref_stride : 0);

        constexpr int BLK = 512;

        auto flush_sheet = [&](size_t bi) {
            if (sh_a[bi] <= 1e-10) return;
            const double sh_cov = std::min(1.0, sh_a[bi]);
            const double sc = sh_cov / sh_a[bi];
            const double rem = std::max(0.0, 1.0 - tot_a[bi]);
            tot_r[bi] += rem * (sh_r[bi] * sc);
            tot_g[bi] += rem * (sh_g[bi] * sc);
            tot_b[bi] += rem * (sh_b[bi] * sc);
            tot_a[bi] += rem * sh_cov;
            sh_r[bi] = sh_g[bi] = sh_b[bi] = sh_a[bi] = 0.0;
        };

        for (int band_y0 = 0; band_y0 < H; band_y0 += BAND_H) {
            const int band_h = std::min(BAND_H, H - band_y0);
            const size_t cur_band_p = static_cast<size_t>(band_h) * static_cast<size_t>(W);
            std::fill_n(tot_r.begin(), cur_band_p, 0.0);
            std::fill_n(tot_g.begin(), cur_band_p, 0.0);
            std::fill_n(tot_b.begin(), cur_band_p, 0.0);
            std::fill_n(tot_a.begin(), cur_band_p, 0.0);
            std::fill_n(sh_r.begin(), cur_band_p, 0.0);
            std::fill_n(sh_g.begin(), cur_band_p, 0.0);
            std::fill_n(sh_b.begin(), cur_band_p, 0.0);
            std::fill_n(sh_a.begin(), cur_band_p, 0.0);
            std::fill_n(sheet_peak_a.begin(), cur_band_p, 0.0);
            std::fill_n(last_act_s.begin(), cur_band_p, -1);

            std::fill(stratum_used.begin(), stratum_used.end(), 0);
            const int scan_y0 = std::max(0, band_y0 - opts.max_radius);
            const int scan_y1 = std::min(H, band_y0 + band_h + opts.max_radius);
            for (int y = scan_y0; y < scan_y1; ++y) {
                const size_t row_off = static_cast<size_t>(y) * W;
                for (int x = 0; x < W; ++x) {
                    const size_t i = row_off + x;
                    const int b0 = std::max(0, std::min(K_STRATA - 1, static_cast<int>(std::floor(pix_dfg[i] * (K_STRATA - 1)))));
                    const int b1 = std::min(K_STRATA - 1, b0 + 1);
                    stratum_used[static_cast<size_t>(b0)] = 1;
                    stratum_used[static_cast<size_t>(b1)] = 1;
                    if (has_rev_bg[i]) {
                        const int gb0 = std::max(0, std::min(K_STRATA - 1, static_cast<int>(std::floor(pix_dbg[i] * (K_STRATA - 1)))));
                        const int gb1 = std::min(K_STRATA - 1, gb0 + 1);
                        stratum_used[static_cast<size_t>(gb0)] = 1;
                        stratum_used[static_cast<size_t>(gb1)] = 1;
                    }
                }
            }

            for (int s = K_STRATA - 1; s >= 0; --s) {
                if (!stratum_used[static_cast<size_t>(s)]) continue;
                bool any_rem = false;
                for (size_t bi = 0; bi < cur_band_p; ++bi) {
                    if (tot_a[bi] + std::min(1.0, sh_a[bi]) * (1.0 - tot_a[bi]) < 0.9999) { any_rem = true; break; }
                }
                if (!any_rem) break;

                const float d_center = static_cast<float>(s) / static_cast<float>(K_STRATA - 1);
                float max_r_s = 0.0f;
                if (!has_tilt) {
                    max_r_s = eval_radius(d_center, img_cy, img_cx);
                } else {
                    for (int cy_t : {band_y0, band_y0 + band_h - 1})
                        for (int cx_t : {0, W - 1})
                            max_r_s = std::max(max_r_s, eval_radius(d_center, static_cast<float>(cy_t), static_cast<float>(cx_t)));
                }
                const int r_ext_lim = std::min(opts.max_radius, static_cast<int>(std::ceil(max_r_s)) + 1);
                const int ext_y0 = std::max(0, band_y0 - r_ext_lim);
                const int ext_y1 = std::min(H, band_y0 + band_h + r_ext_lim);
                const int ext_h = ext_y1 - ext_y0;

                const RowTask build_pref = [&](int r0, int r1) {
                    for (int ly = r0; ly < r1; ++ly) {
                        const int y = ext_y0 + ly;
                        const size_t row_p = static_cast<size_t>(ly) * pref_stride;
                        double sr0 = 0, sg0 = 0, sb0 = 0, sa0 = 0;
                        double sr1 = 0, sg1 = 0, sb1 = 0, sa1 = 0;
                        double sr2 = 0, sg2 = 0, sb2 = 0, sa2 = 0;
                        pref_r0[row_p] = pref_g0[row_p] = pref_b0[row_p] = pref_a0[row_p] = 0.0;
                        if (has_moments) {
                            pref_r1[row_p] = pref_g1[row_p] = pref_b1[row_p] = pref_a1[row_p] = 0.0;
                            pref_r2[row_p] = pref_g2[row_p] = pref_b2[row_p] = pref_a2[row_p] = 0.0;
                        }
                        const float sf = static_cast<float>(s);
                        for (int x = 0; x < W; ++x) {
                            if ((x & (BLK - 1)) == 0) {
                                sr0 = sg0 = sb0 = sa0 = 0.0;
                                sr1 = sg1 = sb1 = sa1 = 0.0;
                                sr2 = sg2 = sb2 = sa2 = 0.0;
                            }
                            const size_t i = static_cast<size_t>(y) * W + x;
                            float wt = std::max(0.0f, 1.0f - std::fabs(pix_dfg[i] * static_cast<float>(K_STRATA - 1) - sf));
                            const float* pix_src = active_src_rgb;
                            if (has_rev_bg[i] && wt < 1e-5f) {
                                const float w_rev = std::max(0.0f, 1.0f - std::fabs(pix_dbg[i] * static_cast<float>(K_STRATA - 1) - sf));
                                if (w_rev > 0.0f) { wt = w_rev; pix_src = opts.revealed_bg_rgb; }
                            }
                            const float pr = pix_src[3 * i + 0], pg = pix_src[3 * i + 1], pb = pix_src[3 * i + 2];
                            const double dw = static_cast<double>(wt);
                            const double r_w = dw * (std::isfinite(pr) ? static_cast<double>(pr) : 0.0);
                            const double g_w = dw * (std::isfinite(pg) ? static_cast<double>(pg) : 0.0);
                            const double b_w = dw * (std::isfinite(pb) ? static_cast<double>(pb) : 0.0);
                            const double a_w = dw;
                            sr0 += r_w; sg0 += g_w; sb0 += b_w; sa0 += a_w;
                            pref_r0[row_p + x + 1] = sr0; pref_g0[row_p + x + 1] = sg0;
                            pref_b0[row_p + x + 1] = sb0; pref_a0[row_p + x + 1] = sa0;
                            if (has_moments) {
                                const int blk = x / BLK;
                                const double xi = static_cast<double>(x - (blk * BLK + BLK / 2));
                                const double xi2 = xi * xi;
                                sr1 += xi * r_w; sg1 += xi * g_w; sb1 += xi * b_w; sa1 += xi * a_w;
                                sr2 += xi2 * r_w; sg2 += xi2 * g_w; sb2 += xi2 * b_w; sa2 += xi2 * a_w;
                                pref_r1[row_p + x + 1] = sr1; pref_g1[row_p + x + 1] = sg1;
                                pref_b1[row_p + x + 1] = sb1; pref_a1[row_p + x + 1] = sa1;
                                pref_r2[row_p + x + 1] = sr2; pref_g2[row_p + x + 1] = sg2;
                                pref_b2[row_p + x + 1] = sb2; pref_a2[row_p + x + 1] = sa2;
                            }
                        }
                    }
                };
                if (parallel_rows) parallel_rows(ext_h, build_pref); else build_pref(0, ext_h);

                const RowTask comp_rows = [&](int r0, int r1) {
                    auto px_val = [&](const std::vector<double>& pf, size_t row_p, int px) noexcept -> double {
                        return ((px & (BLK - 1)) == 0) ? pf[row_p + px + 1] : (pf[row_p + px + 1] - pf[row_p + px]);
                    };
                    auto circle_strip_antideriv = [](double rad_d, double u) noexcept -> double {
                        if (rad_d <= 1e-9) return 0.0;
                        const double uc = std::max(-rad_d, std::min(rad_d, u));
                        const double sv = std::max(-1.0, std::min(1.0, uc / rad_d));
                        return 0.5 * (uc * std::sqrt(std::max(0.0, rad_d * rad_d - uc * uc)) +
                                      rad_d * rad_d * std::asin(sv));
                    };
                    for (int by = r0; by < r1; ++by) {
                        const int y = band_y0 + by;
                        const float ry = (y - img_cy) * inv_diag;
                        const size_t band_row = static_cast<size_t>(by) * static_cast<size_t>(W);
                        for (int x = 0; x < W; ++x) {
                            const size_t bi = band_row + x;
                            if (tot_a[bi] >= 0.9999) continue;
                            const float r_f = has_tilt
                                ? eval_radius(d_center, static_cast<float>(y), static_cast<float>(x))
                                : max_r_s;
                            const float rx = (x - img_cx) * inv_diag;
                            const float rn = std::hypot(ry, rx);
                            const float d_syn_loc = has_tilt
                                ? detail::clamp01(opts.focus_depth + opts.tilt_x * ((x - img_cx) * inv_w_norm) +
                                                  opts.tilt_y * ((y - img_cy) * inv_h_norm))
                                : opts.focus_depth;
                            const float sign_df = (d_center >= d_syn_loc) ? 1.0f : -1.0f;
                            const float poly_ang = angle_rad +
                                ((d_center < d_syn_loc && (opts.aperture_blades & 1)) ? std::numbers::pi_v<float> : 0.0f);

                            auto eval_disc_unit = [&](float disc_r, double& dr_out, double& dg_out,
                                                      double& db_out, double& da_out, bool& valid_out) {
                                dr_out = dg_out = db_out = da_out = 0.0;
                                valid_out = false;
                                if (disc_r < 0.05f) {
                                    if (y >= ext_y0 && y < ext_y1) {
                                        const size_t row_p = static_cast<size_t>(y - ext_y0) * pref_stride;
                                        dr_out = px_val(pref_r0, row_p, x);
                                        dg_out = px_val(pref_g0, row_p, x);
                                        db_out = px_val(pref_b0, row_p, x);
                                        da_out = px_val(pref_a0, row_p, x);
                                        valid_out = true;
                                    }
                                    return;
                                }
                                double ar_loc = 0.0, ag_loc = 0.0, ab_loc = 0.0, aa_loc = 0.0, na_loc = 0.0;
                                const int r_lim = std::min(opts.max_radius, static_cast<int>(std::ceil(disc_r + 0.5f)));
                                const float off_x = (vig > 0.01f && disc_r > 2.0f) ? (-sign_df * vig * disc_r * 0.8f * rx) : 0.0f;
                                const float off_y = (vig > 0.01f && disc_r > 2.0f) ? (-sign_df * vig * disc_r * 0.8f * ry) : 0.0f;
                                float c0 = 1.0f, c2 = 0.0f;
                                if (has_moments) {
                                    detail::radial_poly_coeffs(opts.spherical_aberration, opts.stf_apodization, sign_df, c0, c2);
                                }
                                const double inv_R2 = 1.0 / std::max(0.25, static_cast<double>(disc_r) * static_cast<double>(disc_r));
                                const double c2_norm = static_cast<double>(c2) * inv_R2;
                                const double rad_d = static_cast<double>(disc_r);

                                const int dy0 = std::max(-r_lim, ext_y0 - y);
                                const int dy1 = std::min(r_lim, ext_y1 - 1 - y);
                                for (int dy = dy0; dy <= dy1; ++dy) {
                                    double wy = 0.0;
                                    float xl_f = 0.0f, xh_f = 0.0f;
                                    if (opts.aperture_blades >= 5) {
                                        wy = static_cast<double>(detail::clamp01(
                                            disc_r + 0.5f - std::fabs(static_cast<float>(dy))));
                                        if (wy <= 1e-5) continue;
                                        detail::polygon_row_span_float(static_cast<float>(dy), disc_r,
                                            opts.aperture_blades, poly_ang, xl_f, xh_f);
                                    } else {
                                        const double y0_s = std::max(-rad_d, static_cast<double>(dy) - 0.5);
                                        const double y1_s = std::min(rad_d, static_cast<double>(dy) + 0.5);
                                        wy = y1_s - y0_s;
                                        if (wy <= 1e-6) continue;
                                        const double strip_area = 2.0 * (
                                            circle_strip_antideriv(rad_d, y1_s) - circle_strip_antideriv(rad_d, y0_s));
                                        if (strip_area <= 1e-9) continue;
                                        const float ex = static_cast<float>(0.5 * strip_area / wy);
                                        xl_f = -ex;
                                        xh_f = ex;
                                    }
                                    if (xh_f < xl_f) continue;
                                    if (vig > 0.01f && rn > 0.15f && disc_r > 2.0f) {
                                        const double dyc = static_cast<double>(dy) - static_cast<double>(off_y);
                                        const double cy0 = std::max(-rad_d, dyc - 0.5);
                                        const double cy1 = std::min(rad_d, dyc + 0.5);
                                        if (cy1 <= cy0 + 1e-6) continue;
                                        const double c_area = 2.0 * (
                                            circle_strip_antideriv(rad_d, cy1) - circle_strip_antideriv(rad_d, cy0));
                                        if (c_area <= 1e-9) continue;
                                        const float cex = static_cast<float>(0.5 * c_area / (cy1 - cy0));
                                        xl_f = std::max(xl_f, off_x - cex);
                                        xh_f = std::min(xh_f, off_x + cex);
                                        if (xh_f < xl_f) continue;
                                    }
                                    const double alpha_y = static_cast<double>(c0) + c2_norm * static_cast<double>(dy * dy);
                                    const int ly = y + dy - ext_y0;
                                    const size_t row_p = static_cast<size_t>(ly) * pref_stride;

                                    const float ul = std::max(0.0f, static_cast<float>(x) + xl_f + 0.5f);
                                    const float uh = std::min(static_cast<float>(W), static_cast<float>(x) + xh_f + 0.5f);
                                    if (uh <= ul + 1e-5f) continue;
                                    const int kl = std::max(0, std::min(W - 1, static_cast<int>(std::floor(ul))));
                                    const int kr = std::max(0, std::min(W - 1, static_cast<int>(std::floor(uh - 1e-6f))));
                                    if (kr < kl) continue;

                                    auto accum_frac = [&](int px_col, float frac) {
                                        if (px_col < 0 || px_col >= W || frac <= 1e-5f) return;
                                        const double dxf = static_cast<double>(px_col - x);
                                        const double rad_w = has_moments
                                            ? std::max(0.05, static_cast<double>(c0) + static_cast<double>(c2) *
                                                std::min(1.0, (static_cast<double>(dy * dy) + dxf * dxf) * inv_R2))
                                            : 1.0;
                                        const double kw = wy * static_cast<double>(frac) * rad_w;
                                        const double wt_a = px_val(pref_a0, row_p, px_col);
                                        if (wt_a > 1e-10) {
                                            ar_loc += kw * px_val(pref_r0, row_p, px_col);
                                            ag_loc += kw * px_val(pref_g0, row_p, px_col);
                                            ab_loc += kw * px_val(pref_b0, row_p, px_col);
                                            aa_loc += kw * wt_a;
                                        }
                                        na_loc += kw;
                                    };

                                    if (kl == kr) {
                                        accum_frac(kl, std::min(1.0f, uh - ul));
                                    } else {
                                        const float fl = static_cast<float>(kl + 1) - ul;
                                        const float fr = uh - static_cast<float>(kr);
                                        const int ix0 = (fl >= 0.9999f) ? kl : (kl + 1);
                                        const int ix1 = (fr >= 0.9999f) ? kr : (kr - 1);
                                        if (fl > 1e-5f && fl < 0.9999f) accum_frac(kl, fl);
                                        if (ix0 <= ix1) {
                                            if (ix1 - ix0 <= 8) {
                                                for (int px = ix0; px <= ix1; ++px) {
                                                    const double dxf = static_cast<double>(px - x);
                                                    const double rad_w = has_moments
                                                        ? std::max(0.05, static_cast<double>(c0) + static_cast<double>(c2) *
                                                            std::min(1.0, (static_cast<double>(dy * dy) + dxf * dxf) * inv_R2))
                                                        : 1.0;
                                                    const double kw = wy * rad_w;
                                                    const double wt_a = px_val(pref_a0, row_p, px);
                                                    if (wt_a > 1e-10) {
                                                        ar_loc += kw * px_val(pref_r0, row_p, px);
                                                        ag_loc += kw * px_val(pref_g0, row_p, px);
                                                        ab_loc += kw * px_val(pref_b0, row_p, px);
                                                        aa_loc += kw * wt_a;
                                                    }
                                                    na_loc += kw;
                                                }
                                            } else {
                                                for (int seg_a = ix0; seg_a <= ix1; ) {
                                                    const int blk = seg_a / BLK;
                                                    const int seg_c = std::min(ix1, blk * BLK + BLK - 1);
                                                    const size_t s1 = static_cast<size_t>(seg_c) + 1;
                                                    const size_t s0 = static_cast<size_t>(seg_a);
                                                    const bool at_start = ((seg_a & (BLK - 1)) == 0);
                                                    const double span_len = static_cast<double>(seg_c - seg_a + 1);
                                                    const double dr0 = pref_r0[row_p + s1] - (at_start ? 0.0 : pref_r0[row_p + s0]);
                                                    const double dg0 = pref_g0[row_p + s1] - (at_start ? 0.0 : pref_g0[row_p + s0]);
                                                    const double db0 = pref_b0[row_p + s1] - (at_start ? 0.0 : pref_b0[row_p + s0]);
                                                    const double da0 = pref_a0[row_p + s1] - (at_start ? 0.0 : pref_a0[row_p + s0]);
                                                    if (!has_moments) {
                                                        ar_loc += wy * dr0; ag_loc += wy * dg0;
                                                        ab_loc += wy * db0; aa_loc += wy * da0;
                                                        na_loc += wy * span_len;
                                                    } else {
                                                        const double d_x = static_cast<double>(x - (blk * BLK + BLK / 2));
                                                        const double dr1 = pref_r1[row_p + s1] - (at_start ? 0.0 : pref_r1[row_p + s0]);
                                                        const double dg1 = pref_g1[row_p + s1] - (at_start ? 0.0 : pref_g1[row_p + s0]);
                                                        const double db1 = pref_b1[row_p + s1] - (at_start ? 0.0 : pref_b1[row_p + s0]);
                                                        const double da1 = pref_a1[row_p + s1] - (at_start ? 0.0 : pref_a1[row_p + s0]);
                                                        const double dr2 = pref_r2[row_p + s1] - (at_start ? 0.0 : pref_r2[row_p + s0]);
                                                        const double dg2 = pref_g2[row_p + s1] - (at_start ? 0.0 : pref_g2[row_p + s0]);
                                                        const double db2 = pref_b2[row_p + s1] - (at_start ? 0.0 : pref_b2[row_p + s0]);
                                                        const double da2 = pref_a2[row_p + s1] - (at_start ? 0.0 : pref_a2[row_p + s0]);
                                                        const double k_m0 = alpha_y + c2_norm * d_x * d_x;
                                                        const double k_m1 = -2.0 * c2_norm * d_x;
                                                        ar_loc += wy * (k_m0 * dr0 + k_m1 * dr1 + c2_norm * dr2);
                                                        ag_loc += wy * (k_m0 * dg0 + k_m1 * dg1 + c2_norm * dg2);
                                                        ab_loc += wy * (k_m0 * db0 + k_m1 * db1 + c2_norm * db2);
                                                        aa_loc += wy * (k_m0 * da0 + k_m1 * da1 + c2_norm * da2);
                                                        double sum_dx2 = 0.0;
                                                        const int dx_a = seg_a - x, dx_b = seg_c - x;
                                                        auto sum_sq = [](int n) -> double {
                                                            const double dn = static_cast<double>(n);
                                                            return dn * (dn + 1.0) * (2.0 * dn + 1.0) / 6.0;
                                                        };
                                                        if (dx_a <= 0 && dx_b >= 0) sum_dx2 = sum_sq(-dx_a) + sum_sq(dx_b);
                                                        else if (dx_a > 0) sum_dx2 = sum_sq(dx_b) - sum_sq(dx_a - 1);
                                                        else sum_dx2 = sum_sq(-dx_a) - sum_sq(-dx_b - 1);
                                                        na_loc += wy * (alpha_y * span_len + c2_norm * sum_dx2);
                                                    }
                                                    seg_a = seg_c + 1;
                                                }
                                            }
                                        }
                                        if (fr > 1e-5f && fr < 0.9999f) accum_frac(kr, fr);
                                    }
                                }
                                if (disc_r < 1.0f && y >= band_y0 && y < band_y0 + band_h) {
                                    const double c_wt = static_cast<double>(1.0f - disc_r);
                                    const size_t row_p = static_cast<size_t>(y - ext_y0) * pref_stride;
                                    const double cr = px_val(pref_r0, row_p, x);
                                    const double cg = px_val(pref_g0, row_p, x);
                                    const double cb = px_val(pref_b0, row_p, x);
                                    const double ca = px_val(pref_a0, row_p, x);
                                    if (na_loc > 1e-8) {
                                        const double inv = 1.0 / na_loc;
                                        dr_out = (1.0 - c_wt) * (ar_loc * inv) + c_wt * cr;
                                        dg_out = (1.0 - c_wt) * (ag_loc * inv) + c_wt * cg;
                                        db_out = (1.0 - c_wt) * (ab_loc * inv) + c_wt * cb;
                                        da_out = (1.0 - c_wt) * (aa_loc * inv) + c_wt * ca;
                                    } else {
                                        dr_out = cr; dg_out = cg; db_out = cb; da_out = ca;
                                    }
                                    valid_out = true;
                                    return;
                                }
                                if (na_loc > 1e-8) {
                                    const double inv = 1.0 / na_loc;
                                    dr_out = ar_loc * inv; dg_out = ag_loc * inv;
                                    db_out = ab_loc * inv; da_out = aa_loc * inv;
                                    valid_out = true;
                                }
                            };

                            double ar = 0.0, ag = 0.0, ab = 0.0, aa = 0.0, norm_area = 0.0;
                            if (!use_3disc_stf || r_f < 0.05f) {
                                bool ok = false;
                                eval_disc_unit(r_f, ar, ag, ab, aa, ok);
                                norm_area = ok ? 1.0 : 0.0;
                            } else {
                                const double ap = static_cast<double>(detail::clamp01(opts.stf_apodization));
                                const float alphas[3] = {0.28f, 0.58f, 1.00f};
                                const double weights[3] = {0.220 * ap, 0.406 * ap, 1.0 - 0.626 * ap};
                                double w_tot = 0.0;
                                for (int m = 0; m < 3; ++m) {
                                    if (weights[m] <= 1e-8) continue;
                                    double sr = 0.0, sg = 0.0, sb = 0.0, sa_v = 0.0;
                                    bool ok = false;
                                    eval_disc_unit(alphas[m] * r_f, sr, sg, sb, sa_v, ok);
                                    if (ok) {
                                        ar += weights[m] * sr;
                                        ag += weights[m] * sg;
                                        ab += weights[m] * sb;
                                        aa += weights[m] * sa_v;
                                        w_tot += weights[m];
                                    }
                                }
                                norm_area = w_tot;
                            }
                            if (norm_area <= 1e-8 || aa <= 1e-10) continue;
                            const double inv_na = 1.0 / norm_area;
                            const double a_s = aa * inv_na;
                            if (a_s < 0.015) continue;
                            if (last_act_s[bi] >= 0 &&
                                ((last_act_s[bi] - s) >= 2 ||
                                 (sheet_peak_a[bi] > 0.05 && a_s < 0.15 * sheet_peak_a[bi] && (last_act_s[bi] - s) >= 1))) {
                                flush_sheet(bi);
                                sheet_peak_a[bi] = 0.0;
                            }
                            sh_r[bi] += ar * inv_na;
                            sh_g[bi] += ag * inv_na;
                            sh_b[bi] += ab * inv_na;
                            sh_a[bi] += a_s;
                            sheet_peak_a[bi] = std::max(sheet_peak_a[bi], a_s);
                            last_act_s[bi] = s;
                            if (sh_a[bi] >= 0.9999) { flush_sheet(bi); sheet_peak_a[bi] = 0.0; }
                        }
                    }
                };
                if (parallel_rows) parallel_rows(band_h, comp_rows); else comp_rows(0, band_h);
            }
            for (int by = 0; by < band_h; ++by) {
                for (int x = 0; x < W; ++x) {
                    const size_t bi = static_cast<size_t>(by) * static_cast<size_t>(W) + x;
                    flush_sheet(bi);
                    const size_t oi = static_cast<size_t>(band_y0 + by) * static_cast<size_t>(W) + x;
                    if (tot_a[bi] > 1e-8) {
                        const float inv_a = static_cast<float>(1.0 / tot_a[bi]);
                        out[3 * oi + 0] = static_cast<float>(tot_r[bi]) * inv_a;
                        out[3 * oi + 1] = static_cast<float>(tot_g[bi]) * inv_a;
                        out[3 * oi + 2] = static_cast<float>(tot_b[bi]) * inv_a;
                    }
                }
            }
        }
        return out;
    }

    // Doubly-stochastic symmetric disk exchange mode with Scheimpflug & native CoC support.
    const float img_cy = 0.5f * static_cast<float>(H - 1), img_cx = 0.5f * static_cast<float>(W - 1);
    const float inv_h_norm = 2.0f / std::max(1.0f, static_cast<float>(H));
    const float inv_w_norm = 2.0f / std::max(1.0f, static_cast<float>(W));
    const bool has_tilt = (std::fabs(opts.tilt_x) > 1e-4f || std::fabs(opts.tilt_y) > 1e-4f);
    const bool has_nat_coc = (opts.native_coc_slope_px > 1e-3f);

    std::vector<uint16_t> radius(p);
    std::vector<float> area(static_cast<size_t>(opts.max_radius) + 1, 1.0f);
    std::vector<std::vector<int>> extents(static_cast<size_t>(opts.max_radius) + 1);
    extents[0].assign(1, 0);
    for (int r = 1; r <= opts.max_radius; ++r) {
        int count = 0;
        extents[static_cast<size_t>(r)].resize(static_cast<size_t>(r) + 1);
        for (int dy = 0; dy <= r; ++dy) {
            const int extent = static_cast<int>(std::floor(std::sqrt(static_cast<float>(r * r - dy * dy))));
            extents[static_cast<size_t>(r)][static_cast<size_t>(dy)] = extent;
            count += (2 * extent + 1) * (dy == 0 ? 1 : 2);
        }
        area[static_cast<size_t>(r)] = static_cast<float>(count);
    }
    for (int y = 0; y < H; ++y) {
        const float sy = (y + 0.5f) * static_cast<float>(depth_h) / static_cast<float>(H) - 0.5f;
        for (int x = 0; x < W; ++x) {
            const float d = detail::sample_bilinear(
                depth, depth_h, depth_w, sy, (x + 0.5f) * static_cast<float>(depth_w) / static_cast<float>(W) - 0.5f);
            const float d_syn = has_tilt
                ? detail::clamp01(opts.focus_depth + opts.tilt_x * ((x - img_cx) * inv_w_norm) +
                                  opts.tilt_y * ((y - img_cy) * inv_h_norm))
                : opts.focus_depth;
            float rf = opts.radius_px * std::fabs(d - d_syn);
            if (has_nat_coc) {
                const float r_nat = opts.native_coc_slope_px * std::fabs(d - opts.native_focus_depth);
                rf = std::sqrt(std::max(0.0f, rf * rf - r_nat * r_nat));
            }
            const float r = std::min(static_cast<float>(opts.max_radius), rf);
            radius[static_cast<size_t>(y) * W + x] = static_cast<uint16_t>(std::floor(r + 0.5f));
        }
    }
    if (parallel_rows) {
        const RowTask gather = [&](int begin, int end) {
            for (int y = begin; y < end; ++y) for (int x = 0; x < W; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                const int r = radius[i];
                std::array<double, 3> sum{{active_src_rgb[3 * i], active_src_rgb[3 * i + 1], active_src_rgb[3 * i + 2]}};
                for (int dy = std::max(-r, -y); dy <= std::min(r, H - 1 - y); ++dy) {
                    const int extent = extents[static_cast<size_t>(r)][static_cast<size_t>(std::abs(dy))];
                    for (int dx = std::max(-extent, -x); dx <= std::min(extent, W - 1 - x); ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        const size_t j = static_cast<size_t>(y + dy) * W + x + dx;
                        if (dx * dx + dy * dy > radius[j] * radius[j]) continue;
                        const float wt = 1.0f / std::max(area[radius[i]], area[radius[j]]);
                        for (size_t c = 0; c < 3; ++c) sum[c] += wt * (active_src_rgb[3 * j + c] - active_src_rgb[3 * i + c]);
                    }
                }
                for (size_t c = 0; c < 3; ++c) out[3 * i + c] = static_cast<float>(sum[c]);
            }
        };
        parallel_rows(H, gather);
        return out;
    }
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        const int r = radius[i];
        for (int dy = 0; dy <= r; ++dy) {
            const int extent = extents[static_cast<size_t>(r)][static_cast<size_t>(dy)];
            for (int dx = (dy == 0 ? 1 : -extent); dx <= extent; ++dx) {
                const int yy = y + dy, xx = x + dx;
                if (yy >= H || xx < 0 || xx >= W) continue;
                const size_t j = static_cast<size_t>(yy) * W + xx;
                if (dx * dx + dy * dy > radius[j] * radius[j]) continue;
                const float wt = 1.0f / std::max(area[radius[i]], area[radius[j]]);
                for (size_t c = 0; c < 3; ++c) {
                    const float delta = wt * (active_src_rgb[3 * j + c] - active_src_rgb[3 * i + c]);
                    out[3 * i + c] += delta; out[3 * j + c] -= delta;
                }
            }
        }
    }
    return out;
}

// Regime C1: 64x64 50%-overlapping COLA 2D FFT Wiener deconvolution using measured pillbox
// Bessel OTF 2*J1(R*w)/(R*w) within the first lobe (R*w <= 3.45) and 5x5 local minimax clamp.
inline void deconvolve_shift_focus_bessel(std::vector<float>& img_rgb, int H, int W,
                                          const float* depth, int depth_h, int depth_w,
                                          float cam_focus_depth, float target_focus_depth,
                                          float tilt_x, float tilt_y, float native_slope_px,
                                          float noise_var, const ParallelRows& parallel_rows = {}) {
    if (!depth || depth_h <= 0 || depth_w <= 0 || H < 64 || W < 64 ||
        native_slope_px < 0.35f || img_rgb.size() != static_cast<size_t>(H) * W * 3) return;
    constexpr int T = 64, CORE = 48, HALO = 8, STEP = 24;
    constexpr int STRIP_H = 512, TOP_KEEP = HALO + CORE + 16;
    const float img_cy = 0.5f * static_cast<float>(H - 1), img_cx = 0.5f * static_cast<float>(W - 1);
    const float inv_h_norm = 2.0f / static_cast<float>(H), inv_w_norm = 2.0f / static_cast<float>(W);
    const float nvar = std::max(1e-8f, noise_var);

    std::vector<double> win1(CORE);
    for (int i = 0; i < CORE; ++i) win1[i] = std::sin(std::numbers::pi * (i + 0.5) / CORE);

    std::vector<int> tx0s;
    for (int x0 = 0; x0 < W; x0 += STEP) {
        const int xx = std::min(x0, W - CORE);
        if (tx0s.empty() || tx0s.back() != xx) tx0s.push_back(xx);
        if (x0 + CORE >= W) break;
    }
    const int ntx = static_cast<int>(tx0s.size());

    auto smooth_step = [](double e0, double e1, double x) noexcept -> double {
        const double denom = e1 - e0;
        if (std::fabs(denom) < 1e-9) return (x >= e1) ? 1.0 : 0.0;
        const double t = std::max(0.0, std::min(1.0, (x - e0) / denom));
        return t * t * (3.0 - 2.0 * t);
    };

    const size_t top_rows_cap = static_cast<size_t>(std::min(H, TOP_KEEP));
    std::vector<float> top_buf(top_rows_cap * W * 3, 0.0f);
    int top_start = 0, top_count = 0;

    std::vector<float> acc, wsum, strip_orig;
    for (int s_y0 = 0; s_y0 < H; s_y0 += STRIP_H) {
        const int s_y1 = std::min(H, s_y0 + STRIP_H);
        const int sh = s_y1 - s_y0;
        const size_t sp = static_cast<size_t>(sh) * W;
        acc.assign(sp * 3, 0.0f);
        wsum.assign(sp, 0.0f);
        strip_orig.assign(img_rgb.begin() + static_cast<ptrdiff_t>(s_y0) * W * 3,
                          img_rgb.begin() + static_cast<ptrdiff_t>(s_y1) * W * 3);

        auto sample_orig_row = [&](int ry) noexcept -> const float* {
            const int cy = (ry < 0) ? (-ry) : (ry >= H ? (2 * (H - 1) - ry) : ry);
            const int c = std::max(0, std::min(H - 1, cy));
            if (c >= s_y0 && c < s_y1) return &strip_orig[static_cast<size_t>(c - s_y0) * W * 3];
            if (c < s_y0 && c >= top_start && c < top_start + top_count)
                return &top_buf[static_cast<size_t>(c - top_start) * W * 3];
            return &img_rgb[static_cast<size_t>(c) * W * 3];
        };

        std::vector<int> ty0s;
        for (int y0 = 0; y0 < H; y0 += STEP) {
            const int yy = std::min(y0, H - CORE);
            if (yy + CORE > s_y0 && yy < s_y1) {
                if (ty0s.empty() || ty0s.back() != yy) ty0s.push_back(yy);
            }
            if (y0 + CORE >= H) break;
        }
        const int nty = static_cast<int>(ty0s.size());

        // Partition CORE=48 rows by phase modulo 3 so clamped tail tiles never overlap within a pass.
        for (int phase_y = 0; phase_y < 3; ++phase_y) {
            const int n_band = (nty - phase_y + 2) / 3;
            const RowTask run_band = [&](int b0, int b1) {
                std::vector<double> re(T * T), im(T * T);
                for (int bi = b0; bi < b1; ++bi) {
                    const int ty_idx = phase_y + 3 * bi;
                    const int cy0 = ty0s[static_cast<size_t>(ty_idx)];
                    for (int tx_idx = 0; tx_idx < ntx; ++tx_idx) {
                        const int cx0 = tx0s[static_cast<size_t>(tx_idx)];
                        double r_mean = 0.0, d_err_sum = 0.0;
                        int cnt = 0;
                        for (int ly = 0; ly < CORE; ly += 4) for (int lx = 0; lx < CORE; lx += 4) {
                            const int y = cy0 + ly, x = cx0 + lx;
                            const float sy = (y + 0.5f) * static_cast<float>(depth_h) / H - 0.5f;
                            const float sx = (x + 0.5f) * static_cast<float>(depth_w) / W - 0.5f;
                            const float d = detail::clamp01(detail::sample_bilinear(depth, depth_h, depth_w, sy, sx));
                            const float d_syn = detail::clamp01(target_focus_depth + tilt_x * ((x - img_cx) * inv_w_norm) +
                                                                tilt_y * ((y - img_cy) * inv_h_norm));
                            r_mean += native_slope_px * std::fabs(d - cam_focus_depth);
                            d_err_sum += std::fabs(d - d_syn);
                            ++cnt;
                        }
                        const float R_tile = static_cast<float>(r_mean / std::max(1, cnt));
                        const float d_diff = static_cast<float>(d_err_sum / std::max(1, cnt));
                        const double act_w = smooth_step(0.40, 0.70, R_tile) *
                                             smooth_step(3.45, 3.05, R_tile) *
                                             smooth_step(0.18, 0.11, d_diff);
                        for (int ly = 0; ly < CORE; ++ly) {
                            const int y = cy0 + ly;
                            if (y < s_y0 || y >= s_y1) continue;
                            const float* row_ptr = sample_orig_row(y);
                            const int sy_loc = y - s_y0;
                            for (int lx = 0; lx < CORE; ++lx) {
                                const int x = cx0 + lx;
                                const size_t oi = static_cast<size_t>(sy_loc) * W + x;
                                const double w2 = win1[ly] * win1[ly] * win1[lx] * win1[lx];
                                wsum[oi] += static_cast<float>(w2);
                                if (act_w <= 1e-4) {
                                    for (int c = 0; c < 3; ++c)
                                        acc[3 * oi + c] += static_cast<float>(w2 * row_ptr[3 * x + c]);
                                }
                            }
                        }
                        if (act_w <= 1e-4) continue;
                        for (int c = 0; c < 3; ++c) {
                            double cmean = 0.0;
                            for (int py = 0; py < T; ++py) {
                                const float* row_ptr = sample_orig_row(cy0 - HALO + py);
                                for (int px = 0; px < T; ++px) {
                                    const int rx = cx0 - HALO + px;
                                    const int crx = std::max(0, std::min(W - 1, (rx < 0) ? (-rx) : (rx >= W ? (2 * (W - 1) - rx) : rx)));
                                    cmean += row_ptr[3 * crx + c];
                                }
                            }
                            cmean /= (T * T);
                            for (int py = 0; py < T; ++py) {
                                const float* row_ptr = sample_orig_row(cy0 - HALO + py);
                                for (int px = 0; px < T; ++px) {
                                    const int rx = cx0 - HALO + px;
                                    const int crx = std::max(0, std::min(W - 1, (rx < 0) ? (-rx) : (rx >= W ? (2 * (W - 1) - rx) : rx)));
                                    re[static_cast<size_t>(py) * T + px] = static_cast<double>(row_ptr[3 * crx + c]) - cmean;
                                    im[static_cast<size_t>(py) * T + px] = 0.0;
                                }
                            }
                            detail::fft_2d(re, im, T, false);
                            double hf_p = 0.0;
                            int hf_cnt = 0;
                            const double ns_psd = static_cast<double>(nvar) * (T * T);
                            for (int ky = 0; ky < T; ++ky) {
                                const int sy = (ky <= T / 2) ? ky : (ky - T);
                                const double wy = 2.0 * std::numbers::pi * sy / T;
                                for (int kx = 0; kx < T; ++kx) {
                                    const int sx = (kx <= T / 2) ? kx : (kx - T);
                                    const double wx = 2.0 * std::numbers::pi * sx / T;
                                    const double rw = R_tile * std::hypot(wx, wy);
                                    if (rw >= 1.0 && rw <= 3.45) {
                                        const size_t fi = static_cast<size_t>(ky) * T + kx;
                                        hf_p += re[fi] * re[fi] + im[fi] * im[fi];
                                        ++hf_cnt;
                                    }
                                }
                            }
                            const double sig_psd = std::max(1e-7, (hf_p / std::max(1, hf_cnt)) - 0.25 * ns_psd);
                            for (int ky = 0; ky < T; ++ky) {
                                const int sy = (ky <= T / 2) ? ky : (ky - T);
                                const double wy = 2.0 * std::numbers::pi * sy / T;
                                for (int kx = 0; kx < T; ++kx) {
                                    const int sx = (kx <= T / 2) ? kx : (kx - T);
                                    const double wx = 2.0 * std::numbers::pi * sx / T;
                                    const double wm = std::hypot(wx, wy);
                                    const double rw = R_tile * wm;
                                    double gain = 1.0;
                                    if (rw > 0.05 && rw <= 3.45) {
                                        const double h = detail::pillbox_otf(R_tile, wm);
                                        const double snr_inv = ns_psd / std::max(ns_psd, sig_psd);
                                        double g_raw = std::min(3.2, std::max(1.0, h / (h * h + snr_inv * 0.28)));
                                        double taper = (rw <= 2.6) ? 1.0
                                            : 0.5 * (1.0 + std::cos(std::numbers::pi * (rw - 2.6) / 0.85));
                                        gain = 1.0 + act_w * taper * (g_raw - 1.0);
                                    }
                                    const size_t fi = static_cast<size_t>(ky) * T + kx;
                                    re[fi] *= gain; im[fi] *= gain;
                                }
                            }
                            detail::fft_2d(re, im, T, true);
                            for (int ly = 0; ly < CORE; ++ly) {
                                const int y = cy0 + ly;
                                if (y < s_y0 || y >= s_y1) continue;
                                const int sy_loc = y - s_y0;
                                for (int lx = 0; lx < CORE; ++lx) {
                                    const int x = cx0 + lx;
                                    const size_t oi = static_cast<size_t>(sy_loc) * W + x;
                                    const double w2 = win1[ly] * win1[ly] * win1[lx] * win1[lx];
                                    const double v = re[static_cast<size_t>(ly + HALO) * T + (lx + HALO)] + cmean;
                                    acc[3 * oi + c] += static_cast<float>(w2 * v);
                                }
                            }
                        }
                    }
                }
            };
            if (parallel_rows && n_band > 1) parallel_rows(n_band, run_band);
            else if (n_band > 0) run_band(0, n_band);
        }

        const int next_keep = std::min(sh, static_cast<int>(top_rows_cap));
        top_start = s_y1 - next_keep;
        top_count = next_keep;
        std::copy(strip_orig.begin() + static_cast<ptrdiff_t>(sh - next_keep) * W * 3,
                  strip_orig.end(), top_buf.begin());

        const float nsig = std::sqrt(nvar);
        const RowTask clamp_out = [&](int y_rel0, int y_rel1) {
            for (int y_rel = y_rel0; y_rel < y_rel1; ++y_rel) {
                const int y = s_y0 + y_rel;
                for (int x = 0; x < W; ++x) {
                    const size_t si = static_cast<size_t>(y_rel) * W + x;
                    if (wsum[si] <= 1e-6f) continue;
                    const float invw = 1.0f / wsum[si];
                    const size_t gi = static_cast<size_t>(y) * W + x;
                    for (int c = 0; c < 3; ++c) {
                        float lo = strip_orig[3 * si + c], hi = lo;
                        for (int dy = -2; dy <= 2; ++dy) {
                            const int yy = std::max(0, std::min(H - 1, y + dy));
                            const float* rptr = (yy >= s_y0 && yy < s_y1)
                                ? &strip_orig[static_cast<size_t>(yy - s_y0) * W * 3]
                                : &img_rgb[static_cast<size_t>(yy) * W * 3];
                            for (int dx = -2; dx <= 2; ++dx) {
                                const int xx = std::max(0, std::min(W - 1, x + dx));
                                const float nv = rptr[3 * xx + c];
                                lo = std::min(lo, nv); hi = std::max(hi, nv);
                            }
                        }
                        const float span = hi - lo;
                        const float v = acc[3 * si + c] * invw;
                        const float floor_v = (lo >= 0.0f) ? std::max(0.0f, lo - 0.65f * span - 1.5f * nsig) : (lo - 0.65f * span - 1.5f * nsig);
                        const float ceil_v = hi + 0.65f * span + 1.5f * nsig;
                        img_rgb[3 * gi + c] = std::max(floor_v, std::min(ceil_v, v));
                    }
                }
            }
        };
        if (parallel_rows) parallel_rows(sh, clamp_out); else clamp_out(0, sh);
    }
}

// F-B: Mechanical Cat's-eye two-circle intersection pupil indicator/taper in [0, 1].
// Clips axial pupil disk ||(dx, dy)|| <= r against displaced barrel disk shifted by (norm_fx, norm_fy).
inline float catseye_pupil_weight(float dy, float dx, float r, float catseye_vignette,
                                  float norm_fy, float norm_fx) noexcept {
    if (r <= 1e-6f) return 0.0f;
    const float d0 = std::hypot(dy, dx);
    const float w0 = std::max(0.0f, std::min(1.0f, r + 0.5f - d0));
    if (w0 <= 0.0f) return 0.0f;
    const float vig = detail::clamp01(catseye_vignette);
    const float rn = std::hypot(norm_fy, norm_fx);
    if (vig <= 0.01f || rn <= 0.01f) return w0;
    const float off_x = -vig * r * 0.8f * norm_fx;
    const float off_y = -vig * r * 0.8f * norm_fy;
    const float d1 = std::hypot(dy - off_y, dx - off_x);
    const float w1 = std::max(0.0f, std::min(1.0f, r + 0.5f - d1));
    return std::min(w0, w1);
}

// Q8: 6-endpoint variable projection contour unmixing for CFA / RGB observations:
// Model: G = a * G_F + (1-a) * G_B,
//        R = a * (G_F + C_RF) + (1-a) * (G_B + C_RB),
//        B = a * (G_F + C_BF) + (1-a) * (G_B + C_BB).
struct ContourUnmix6Result {
    bool observable = false;
    bool endpoints_identifiable = false;
    bool sum_only_observable = false;
    double gf = 0.0, crf = 0.0, cbf = 0.0;
    double gb = 0.0, crb = 0.0, cbb = 0.0;
    double alpha_var_w = 0.0;
    double residual_rms = 0.0;
    double min_gram_eig_ratio = 0.0;
};

inline ContourUnmix6Result solve_contour_unmix_6endpoint(
    std::span<const float> alpha,
    std::span<const std::array<float, 3>> obs_rgb,
    std::span<const std::array<float, 3>> weight_rgb = {}) {
    ContourUnmix6Result res;
    const size_t n = std::min(alpha.size(), obs_rgb.size());
    if (n < 2) return res;
    double wsum = 0.0, asum = 0.0;
    double g_a = 0.0, g_ab = 0.0, g_b = 0.0;
    std::array<double, 3> ch_wsum{{0.0, 0.0, 0.0}};
    std::array<double, 3> ch_asum{{0.0, 0.0, 0.0}};
    double A[6][6] = {};
    double rhs[6] = {};
    for (size_t i = 0; i < n; ++i) {
        const double a = std::max(0.0, std::min(1.0, static_cast<double>(alpha[i])));
        const double b = 1.0 - a;
        const std::array<float, 3> w = (i < weight_rgb.size())
            ? weight_rgb[i]
            : std::array<float, 3>{{1.0f, 1.0f, 1.0f}};
        for (int c = 0; c < 3; ++c) {
            const double wc = std::max(0.0, static_cast<double>(w[static_cast<size_t>(c)]));
            if (wc <= 0.0 || !std::isfinite(obs_rgb[i][static_cast<size_t>(c)])) continue;
            ch_wsum[static_cast<size_t>(c)] += wc;
            ch_asum[static_cast<size_t>(c)] += wc * a;
            wsum += wc;
            asum += wc * a;
            g_a += wc * a * a;
            g_ab += wc * a * b;
            g_b += wc * b * b;
            double row[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
            if (c == 0) { // R = a*(GF + CRF) + b*(GB + CRB)
                row[0] = a; row[1] = a; row[3] = b; row[4] = b;
            } else if (c == 1) { // G = a*GF + b*GB
                row[0] = a; row[3] = b;
            } else { // B = a*(GF + CBF) + b*(GB + CBB)
                row[0] = a; row[2] = a; row[3] = b; row[5] = b;
            }
            const double y = static_cast<double>(obs_rgb[i][static_cast<size_t>(c)]);
            for (int r = 0; r < 6; ++r) {
                rhs[r] += wc * row[r] * y;
                for (int k = 0; k < 6; ++k) A[r][k] += wc * row[r] * row[k];
            }
        }
    }
    if (wsum <= 1e-12) return res;
    const double mean_a = asum / wsum;
    double avar = 0.0;
    std::array<double, 3> ch_avar{{0.0, 0.0, 0.0}};
    for (size_t i = 0; i < n; ++i) {
        const double a = std::max(0.0, std::min(1.0, static_cast<double>(alpha[i])));
        const std::array<float, 3> w = (i < weight_rgb.size())
            ? weight_rgb[i]
            : std::array<float, 3>{{1.0f, 1.0f, 1.0f}};
        for (int c = 0; c < 3; ++c) {
            const double wc = std::max(0.0, static_cast<double>(w[static_cast<size_t>(c)]));
            if (wc <= 0.0 || !std::isfinite(obs_rgb[i][static_cast<size_t>(c)])) continue;
            const double d = a - mean_a;
            avar += wc * d * d;
            if (ch_wsum[static_cast<size_t>(c)] > 1e-12) {
                const double ma_c = ch_asum[static_cast<size_t>(c)] / ch_wsum[static_cast<size_t>(c)];
                const double dc = a - ma_c;
                ch_avar[static_cast<size_t>(c)] += wc * dc * dc;
            }
        }
    }
    res.alpha_var_w = avar / wsum;
    const double tr = g_a + g_b;
    const double disc = std::hypot(g_a - g_b, 2.0 * g_ab);
    const double lam1 = 0.5 * (tr + disc);
    const double lam2 = std::max(0.0, 0.5 * (tr - disc));
    res.min_gram_eig_ratio = (lam1 > 1e-12) ? (lam2 / lam1) : 0.0;
    bool all_ch_var = true;
    for (int c = 0; c < 3; ++c) {
        if (ch_wsum[static_cast<size_t>(c)] <= 1e-12 ||
            (ch_avar[static_cast<size_t>(c)] / ch_wsum[static_cast<size_t>(c)]) < 1e-4) {
            all_ch_var = false;
        }
    }
    res.endpoints_identifiable = (res.alpha_var_w >= 1e-4) && all_ch_var && (res.min_gram_eig_ratio >= 1e-4);
    res.sum_only_observable = !res.endpoints_identifiable && (wsum > 1e-12);

    // Solve damped 6x6 normal equations (ridge keeps null-space bounded cleanly when unidentifiable)
    double M[6][7] = {};
    for (int r = 0; r < 6; ++r) {
        for (int k = 0; k < 6; ++k) M[r][k] = A[r][k];
        M[r][r] += 1e-9;
        M[r][6] = rhs[r];
    }
    for (int col = 0; col < 6; ++col) {
        int piv = col;
        for (int r = col + 1; r < 6; ++r) {
            if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
        }
        if (piv != col) for (int k = col; k < 7; ++k) std::swap(M[col][k], M[piv][k]);
        const double diag = M[col][col];
        if (std::fabs(diag) < 1e-15) continue;
        for (int k = col; k < 7; ++k) M[col][k] /= diag;
        for (int r = 0; r < 6; ++r) {
            if (r == col) continue;
            const double f = M[r][col];
            for (int k = col; k < 7; ++k) M[r][k] -= f * M[col][k];
        }
    }
    res.gf = M[0][6]; res.crf = M[1][6]; res.cbf = M[2][6];
    res.gb = M[3][6]; res.crb = M[4][6]; res.cbb = M[5][6];

    double rss = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double a = std::max(0.0, std::min(1.0, static_cast<double>(alpha[i])));
        const double b = 1.0 - a;
        const std::array<float, 3> w = (i < weight_rgb.size())
            ? weight_rgb[i]
            : std::array<float, 3>{{1.0f, 1.0f, 1.0f}};
        const double pred[3] = {
            a * (res.gf + res.crf) + b * (res.gb + res.crb),
            a * res.gf + b * res.gb,
            a * (res.gf + res.cbf) + b * (res.gb + res.cbb)
        };
        for (int c = 0; c < 3; ++c) {
            const double wc = std::max(0.0, static_cast<double>(w[static_cast<size_t>(c)]));
            if (wc <= 0.0) continue;
            const double e = pred[c] - static_cast<double>(obs_rgb[i][static_cast<size_t>(c)]);
            rss += wc * e * e;
        }
    }
    res.residual_rms = std::sqrt(rss / wsum);
    res.observable = std::isfinite(res.residual_rms);
    return res;
}

struct SeeThroughLayers {
    int H = 0, W = 0;
    std::vector<float> front_rgb;       // 3 * H * W foreground occluder RGB
    std::vector<float> rear_rgb;        // 3 * H * W unoccluded rear background RGB
    std::vector<float> coverage;        // H * W foreground occluder coverage alpha in [0, 1]
    std::vector<float> angular_support; // H * W rear unoccluded angular weight fraction in [0, 1]
};

// Regime B: True 4D Synthetic-Aperture Plenoptic Ray Reprojection Refocus with Bounded
// 2D Voronoi / 1D Line-Aperture / Uniform Dwell weighting & See-Through Layers.
struct SyntheticApertureInput {
    int H1 = 0, W1 = 0;
    int H_half = 0, W_half = 0;
    int N = 0;
    const std::vector<float>* frames_lin = nullptr;    // size N, H1 * W1
    const uint8_t* cfa6x6 = nullptr;                  // 36 entries
    const float* depth_half = nullptr;                // H_half * W_half in [0, 1]
    float rho_low = 0.0f, rho_high = 0.0f;
    float half_focal_px = 0.0f;
    bool plane_detilted = false;
    float tilt_rho_y = 0.0f, tilt_rho_x = 0.0f;
    const std::array<double, 9>* cam_h = nullptr;     // size N
    double cam_cx = 0.0, cam_cy = 0.0, cam_scale = 1.0;
    const std::array<float, 2>* base_yx = nullptr;    // size N
    const float* base_z = nullptr;                    // size N or nullptr
    float noise_shot = 0.0f, noise_read_var = 1e-6f;
    const uint8_t* cam_valid = nullptr;               // optional size N validity flags
    ApertureWeightMode weight_mode = ApertureWeightMode::Voronoi;
    float catseye_vignette = 0.0f;
};

namespace detail {

// Sutherland-Hodgman polygon clip against half-plane a*x + b*y + c <= 0
inline void clip_poly_halfplane(std::vector<std::array<double, 2>>& poly,
                                double a, double b, double c) {
    if (poly.empty()) return;
    std::vector<std::array<double, 2>> out;
    out.reserve(poly.size() + 2);
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
        const auto& p0 = poly[i];
        const auto& p1 = poly[(i + 1) % n];
        const double d0 = a * p0[0] + b * p0[1] + c;
        const double d1 = a * p1[0] + b * p1[1] + c;
        const bool in0 = (d0 <= 1e-12);
        const bool in1 = (d1 <= 1e-12);
        if (in0 && in1) {
            out.push_back(p1);
        } else if (in0 && !in1) {
            const double t = d0 / std::max(1e-18, d0 - d1);
            out.push_back({{p0[0] + t * (p1[0] - p0[0]), p0[1] + t * (p1[1] - p0[1])}});
        } else if (!in0 && in1) {
            const double t = d0 / std::max(1e-18, d0 - d1);
            out.push_back({{p0[0] + t * (p1[0] - p0[0]), p0[1] + t * (p1[1] - p0[1])}});
            out.push_back(p1);
        }
    }
    poly = std::move(out);
}

inline double poly_area(const std::vector<std::array<double, 2>>& poly) {
    if (poly.size() < 3) return 0.0;
    double area = 0.0;
    for (size_t i = 0; i < poly.size(); ++i) {
        const auto& p0 = poly[i];
        const auto& p1 = poly[(i + 1) % poly.size()];
        area += p0[0] * p1[1] - p1[0] * p0[1];
    }
    return 0.5 * std::fabs(area);
}

inline void compute_aperture_site_weights(int N,
                                        const std::array<float, 2>* base_yx,
                                        const float* base_z,
                                        const uint8_t* cam_valid,
                                        ApertureWeightMode mode,
                                        std::vector<double>& out_w,
                                        std::vector<float>& out_nn_step) {
    out_w.assign(static_cast<size_t>(N), 0.0);
    out_nn_step.assign(static_cast<size_t>(N), 0.0f);
    std::vector<int> active;
    active.reserve(static_cast<size_t>(N));
    for (int k = 0; k < N; ++k) {
        if (!cam_valid || cam_valid[k]) active.push_back(k);
    }
    if (active.empty()) return;
    if (mode == ApertureWeightMode::Dwell || active.size() == 1) {
        for (int k : active) {
            out_w[static_cast<size_t>(k)] = 1.0;
            out_nn_step[static_cast<size_t>(k)] = 1.0f;
        }
        return;
    }

    double my = 0.0, mx = 0.0;
    for (int k : active) { my += base_yx[k][0]; mx += base_yx[k][1]; }
    my /= static_cast<double>(active.size());
    mx /= static_cast<double>(active.size());
    double cyy = 0.0, cyx = 0.0, cxx = 0.0, max_r = 0.0;
    std::vector<double> nn_vals;
    nn_vals.reserve(active.size());
    for (int k : active) {
        const double dyc = base_yx[k][0] - my, dxc = base_yx[k][1] - mx;
        cyy += dyc * dyc; cyx += dyc * dxc; cxx += dxc * dxc;
        max_r = std::max(max_r, std::hypot(dyc, dxc));
        double min_d2 = 1e18;
        for (int m : active) {
            if (m == k) continue;
            const double dy = base_yx[k][0] - base_yx[m][0];
            const double dx = base_yx[k][1] - base_yx[m][1];
            const double dz = base_z ? 0.5 * (base_z[k] - base_z[m]) : 0.0;
            const double d2 = dy * dy + dx * dx + dz * dz;
            if (d2 > 1e-8 && d2 < min_d2) min_d2 = d2;
        }
        const double step = (min_d2 < 1e17) ? std::sqrt(min_d2) : 1.0;
        out_nn_step[static_cast<size_t>(k)] = static_cast<float>(step);
        nn_vals.push_back(step);
    }
    std::sort(nn_vals.begin(), nn_vals.end());
    const double med_nn = nn_vals[nn_vals.size() / 2];

    const double tr = cyy + cxx;
    const double disc = std::hypot(cyy - cxx, 2.0 * cyx);
    const double lam1 = 0.5 * (tr + disc);
    const double lam2 = std::max(0.0, 0.5 * (tr - disc));
    if (lam1 > 1e-12 && lam2 <= 0.02 * lam1) {
        // 1D collinear line-aperture sweep fallback (Voronoi interval length along principal axis)
        double uy = 1.0, ux = 0.0;
        if (disc > 1e-14 * std::max(1.0, tr)) {
            if (cyy >= cxx) { uy = 0.5 * (cyy - cxx + disc); ux = cyx; }
            else { uy = cyx; ux = 0.5 * (cxx - cyy + disc); }
            const double un = std::hypot(uy, ux);
            if (un > 1e-18) { uy /= un; ux /= un; }
        }
        std::vector<std::pair<double, int>> proj;
        proj.reserve(active.size());
        for (int k : active) proj.push_back({uy * (base_yx[k][0] - my) + ux * (base_yx[k][1] - mx), k});
        std::sort(proj.begin(), proj.end());
        // Group near-duplicate sites within 1e-4
        struct Cluster1D { double pos; std::vector<int> ids; };
        std::vector<Cluster1D> cls;
        for (const auto& pr : proj) {
            if (!cls.empty() && std::fabs(pr.first - cls.back().pos) < 1e-4) {
                cls.back().ids.push_back(pr.second);
            } else {
                cls.push_back({pr.first, {pr.second}});
            }
        }
        const double pad = std::max(1e-3, 0.5 * med_nn);
        double wsum = 0.0;
        for (size_t i = 0; i < cls.size(); ++i) {
            const double left = (i == 0) ? (cls[i].pos - pad) : 0.5 * (cls[i - 1].pos + cls[i].pos);
            const double right = (i + 1 == cls.size()) ? (cls[i].pos + pad) : 0.5 * (cls[i].pos + cls[i + 1].pos);
            const double len = std::max(1e-5, right - left) / static_cast<double>(cls[i].ids.size());
            for (int id : cls[i].ids) {
                out_w[static_cast<size_t>(id)] = len;
                wsum += len;
            }
        }
        if (wsum > 0.0) {
            const double scale = static_cast<double>(active.size()) / wsum;
            for (int k : active) out_w[static_cast<size_t>(k)] = std::max(0.05, std::min(8.0, out_w[static_cast<size_t>(k)] * scale));
        }
        return;
    }

    // 2D pupil-clipped exact Voronoi polygon cell areas (Sutherland-Hodgman)
    const double bound_r = std::max(1e-3, 1.15 * max_r + 0.35 * med_nn);
    constexpr int DISK_VERTS = 24;
    std::vector<std::array<double, 2>> base_disk;
    base_disk.reserve(DISK_VERTS);
    for (int v = 0; v < DISK_VERTS; ++v) {
        const double th = (2.0 * std::numbers::pi * v) / DISK_VERTS;
        base_disk.push_back({{mx + bound_r * std::cos(th), my + bound_r * std::sin(th)}});
    }
    double wsum = 0.0;
    for (int k : active) {
        std::vector<std::array<double, 2>> cell = base_disk;
        const double px = base_yx[k][1], py = base_yx[k][0];
        int mult = 1;
        for (int m : active) {
            if (m == k) continue;
            const double qx = base_yx[m][1], qy = base_yx[m][0];
            const double dx = qx - px, dy = qy - py;
            const double d2 = dx * dx + dy * dy;
            if (d2 < 1e-8) { ++mult; continue; }
            const double mx2 = 0.5 * (px + qx), my2 = 0.5 * (py + qy);
            // Half-plane (q - p) . (x - m) <= 0  ->  dx*x + dy*y - (dx*mx2 + dy*my2) <= 0
            clip_poly_halfplane(cell, dx, dy, -(dx * mx2 + dy * my2));
            if (cell.empty()) break;
        }
        const double area = std::max(1e-6, poly_area(cell) / static_cast<double>(mult));
        out_w[static_cast<size_t>(k)] = area;
        wsum += area;
    }
    if (wsum > 0.0) {
        const double scale = static_cast<double>(active.size()) / wsum;
        for (int k : active) {
            out_w[static_cast<size_t>(k)] = std::max(0.05, std::min(8.0, out_w[static_cast<size_t>(k)] * scale));
        }
    }
}

} // namespace detail

inline std::vector<float> render_synthetic_aperture(const SyntheticApertureInput& in,
                                                      float focus_depth, float tilt_x, float tilt_y,
                                                      bool see_through, int out_H, int out_W,
                                                      const ParallelRows& parallel_rows = {},
                                                      SeeThroughLayers* out_layers = nullptr) {
    const size_t p = static_cast<size_t>(out_H) * static_cast<size_t>(out_W);
    std::vector<float> out(3 * p, 0.0f);
    if (out_layers) {
        out_layers->H = out_H;
        out_layers->W = out_W;
        out_layers->front_rgb.assign(3 * p, 0.0f);
        out_layers->rear_rgb.assign(3 * p, 0.0f);
        out_layers->coverage.assign(p, 0.0f);
        out_layers->angular_support.assign(p, 0.0f);
    }
    if (in.N < 2 || !in.frames_lin || !in.depth_half || !in.cam_h || !in.base_yx) return out;

    // 1. Bounded 2D Voronoi / 1D Line-Aperture / Uniform Dwell pupil weights
    std::vector<double> vor_w;
    std::vector<float> nn_step;
    detail::compute_aperture_site_weights(in.N, in.base_yx, in.base_z, in.cam_valid, in.weight_mode, vor_w, nn_step);

    const float rho_span = std::max(1e-6f, in.rho_high - in.rho_low);
    const float hcy = 0.5f * static_cast<float>(in.H_half - 1), hcx = 0.5f * static_cast<float>(in.W_half - 1);
    const float ocy = 0.5f * static_cast<float>(out_H - 1), ocx = 0.5f * static_cast<float>(out_W - 1);
    const float inv_oh = 2.0f / std::max(1.0f, static_cast<float>(out_H));
    const float inv_ow = 2.0f / std::max(1.0f, static_cast<float>(out_W));

    bool is_bayer2 = true;
    for (int r = 0; r < 6 && is_bayer2; ++r) {
        for (int c = 0; c < 6; ++c) {
            if (in.cfa6x6[r * 6 + c] != in.cfa6x6[(r & 1) * 6 + (c & 1)]) { is_bayer2 = false; break; }
        }
    }
    const int cfa_lo = is_bayer2 ? -1 : -2;
    const int cfa_hi = is_bayer2 ?  2 :  3;
    const float tent_r = is_bayer2 ? 2.0f : 3.0f;

    auto raw_rgb_at = [&](int k, float y1, float x1, std::array<float, 3>& rgb, bool allow_clamp) -> bool {
        if (!std::isfinite(y1) || !std::isfinite(x1)) return false;
        if (!allow_clamp && (y1 < -0.5f || x1 < -0.5f ||
            y1 > static_cast<float>(in.H1) - 0.5f || x1 > static_cast<float>(in.W1) - 0.5f)) return false;
        const float yc = std::max(0.0f, std::min(static_cast<float>(in.H1 - 1), y1));
        const float xc = std::max(0.0f, std::min(static_cast<float>(in.W1 - 1), x1));
        const int iy = static_cast<int>(std::floor(yc)), ix = static_cast<int>(std::floor(xc));
        float sum[3] = {0, 0, 0}, wt[3] = {0, 0, 0};
        for (int r = std::max(0, iy + cfa_lo); r <= std::min(in.H1 - 1, iy + cfa_hi); ++r) {
            for (int c = std::max(0, ix + cfa_lo); c <= std::min(in.W1 - 1, ix + cfa_hi); ++c) {
                const float w = std::max(0.0f, tent_r - std::fabs(yc - r)) * std::max(0.0f, tent_r - std::fabs(xc - c));
                const uint8_t ch = in.cfa6x6[(r % 6) * 6 + (c % 6)];
                const float v = in.frames_lin[k][static_cast<size_t>(r) * in.W1 + c];
                if (std::isfinite(v)) { sum[ch] += w * v; wt[ch] += w; }
            }
        }
        for (int c = 0; c < 3; ++c) rgb[static_cast<size_t>(c)] = (wt[c] > 0.0f) ? (sum[c] / wt[c]) : 0.0f;
        return wt[0] > 0.0f && wt[1] > 0.0f && wt[2] > 0.0f;
    };

    const RowTask ray_rows = [&](int y0, int y1) {
        std::vector<std::array<float, 3>> samples_rgb;
        std::vector<float> samples_lum, samples_w;
        std::vector<uint8_t> samples_clr;
        samples_rgb.reserve(static_cast<size_t>(in.N));
        samples_lum.reserve(static_cast<size_t>(in.N));
        samples_w.reserve(static_cast<size_t>(in.N));
        samples_clr.reserve(static_cast<size_t>(in.N));
        const float half_to_out = static_cast<float>(out_H) / std::max(1.0f, static_cast<float>(in.H_half));
        const float raw_to_out = static_cast<float>(in.H1) / std::max(1.0f, static_cast<float>(out_H));
        for (int y = y0; y < y1; ++y) {
            const float hy = (y + 0.5f) * static_cast<float>(in.H_half) / static_cast<float>(out_H) - 0.5f;
            for (int x = 0; x < out_W; ++x) {
                const float hx = (x + 0.5f) * static_cast<float>(in.W_half) / static_cast<float>(out_W) - 0.5f;
                const float d_ref = detail::clamp01(detail::sample_bilinear(in.depth_half, in.H_half, in.W_half, hy, hx));
                const float rho_ref = in.rho_low + d_ref * rho_span;
                const float d_syn = detail::clamp01(focus_depth + tilt_x * ((x - ocx) * inv_ow) +
                                                    tilt_y * ((y - ocy) * inv_oh));
                const float rho_syn = in.rho_low + d_syn * rho_span;
                const float pb = (in.plane_detilted && in.half_focal_px > 0.0f)
                    ? (in.tilt_rho_y * ((hy - hcy) / in.half_focal_px) + in.tilt_rho_x * ((hx - hcx) / in.half_focal_px))
                    : 0.0f;
                const float r_target = rho_syn - pb;
                const float n_fov_y = (in.half_focal_px > 0.0f) ? ((hy - hcy) / in.half_focal_px) : 0.0f;
                const float n_fov_x = (in.half_focal_px > 0.0f) ? ((hx - hcx) / in.half_focal_px) : 0.0f;
                const bool behind_fg = see_through && (d_ref - d_syn > 0.14f);

                samples_rgb.clear(); samples_lum.clear(); samples_w.clear(); samples_clr.clear();
                for (int k = 0; k < in.N; ++k) {
                    if (in.cam_valid && !in.cam_valid[k]) continue;
                    const double u = (hx - in.cam_cx) / in.cam_scale, v = (hy - in.cam_cy) / in.cam_scale;
                    const auto& hm = in.cam_h[k];
                    const double dn = hm[6] * u + hm[7] * v + hm[8];
                    if (std::fabs(dn) < 1e-9) continue;
                    const float hsy = static_cast<float>((hm[3] * u + hm[4] * v + hm[5]) / dn * in.cam_scale + in.cam_cy);
                    const float hsx = static_cast<float>((hm[0] * u + hm[1] * v + hm[2]) / dn * in.cam_scale + in.cam_cx);
                    const float ev_y = in.base_yx[k][0] - (in.base_z ? in.base_z[k] * n_fov_y : 0.0f);
                    const float ev_x = in.base_yx[k][1] - (in.base_z ? in.base_z[k] * n_fov_x : 0.0f);
                    const float qy = hsy + r_target * ev_y;
                    const float qx = hsx + r_target * ev_x;
                    uint8_t ray_cleared = 0;
                    if (behind_fg) {
                        const float x0_y = hy - (rho_ref - rho_syn) * ev_y;
                        const float x0_x = hx - (rho_ref - rho_syn) * ev_x;
                        if (x0_y >= 0.0f && x0_x >= 0.0f &&
                            x0_y <= static_cast<float>(in.H_half - 1) && x0_x <= static_cast<float>(in.W_half - 1)) {
                            const float d_occ = detail::sample_bilinear(in.depth_half, in.H_half, in.W_half, x0_y, x0_x);
                            if (std::isfinite(d_occ) && d_occ <= d_syn + 0.10f) ray_cleared = 1;
                        }
                    }
                    // Plenoptic Voronoi anti-aliasing prefilter (skip when cleared ray hits focused background)
                    const float d_aa = (behind_fg && ray_cleared) ? d_syn : d_ref;
                    const float r_aa = std::min(6.0f, 0.45f * nn_step[static_cast<size_t>(k)] *
                        std::fabs(d_aa - d_syn) * rho_span * half_to_out);
                    std::array<float, 3> c_avg{{0.0f, 0.0f, 0.0f}};
                    if (r_aa < 0.75f) {
                        if (!raw_rgb_at(k, 2.0f * qy + 0.5f, 2.0f * qx + 0.5f, c_avg, false)) continue;
                    } else {
                        const int nr = static_cast<int>(std::floor(r_aa * raw_to_out));
                        int n_ok = 0;
                        std::array<float, 3> c_t{};
                        for (int sy_o = -nr; sy_o <= nr; sy_o += std::max(1, nr)) {
                            for (int sx_o = -nr; sx_o <= nr; sx_o += std::max(1, nr)) {
                                if (raw_rgb_at(k, 2.0f * qy + 0.5f + sy_o, 2.0f * qx + 0.5f + sx_o, c_t, false)) {
                                    for (int ch = 0; ch < 3; ++ch) c_avg[static_cast<size_t>(ch)] += c_t[static_cast<size_t>(ch)];
                                    ++n_ok;
                                }
                            }
                        }
                        if (n_ok == 0) continue;
                        for (int ch = 0; ch < 3; ++ch) c_avg[static_cast<size_t>(ch)] /= static_cast<float>(n_ok);
                    }
                    float wk = static_cast<float>(vor_w[static_cast<size_t>(k)]);
                    if (in.catseye_vignette > 0.01f && in.half_focal_px > 0.0f) {
                        const float bnorm = std::max(1e-3f, std::hypot(in.base_yx[k][0], in.base_yx[k][1]));
                        wk *= catseye_pupil_weight(in.base_yx[k][0], in.base_yx[k][1], bnorm + 0.25f,
                                                   in.catseye_vignette, (y - ocy) * inv_oh, (x - ocx) * inv_ow);
                        if (wk <= 1e-5f) continue;
                    }
                    samples_rgb.push_back(c_avg);
                    samples_lum.push_back(c_avg[1]);
                    samples_w.push_back(wk);
                    samples_clr.push_back(ray_cleared);
                }
                const size_t oi = static_cast<size_t>(y) * out_W + x;
                if (samples_rgb.empty()) {
                    std::array<float, 3> fb{{0.0f, 0.0f, 0.0f}};
                    if (raw_rgb_at(0, 2.0f * hy + 0.5f, 2.0f * hx + 0.5f, fb, true)) {
                        for (int c = 0; c < 3; ++c) {
                            out[3 * oi + c] = fb[static_cast<size_t>(c)];
                            if (out_layers) {
                                out_layers->front_rgb[3 * oi + c] = fb[static_cast<size_t>(c)];
                                out_layers->rear_rgb[3 * oi + c] = fb[static_cast<size_t>(c)];
                            }
                        }
                    }
                    continue;
                }
                if (out_layers) {
                    double occ_r = 0, occ_g = 0, occ_b = 0, occ_w = 0, all_w = 0;
                    for (size_t s = 0; s < samples_rgb.size(); ++s) {
                        const double wt = samples_w[s];
                        all_w += wt;
                        if (!samples_clr[s]) {
                            occ_r += wt * samples_rgb[s][0];
                            occ_g += wt * samples_rgb[s][1];
                            occ_b += wt * samples_rgb[s][2];
                            occ_w += wt;
                        }
                    }
                    if (occ_w > 1e-8) {
                        out_layers->front_rgb[3 * oi + 0] = static_cast<float>(occ_r / occ_w);
                        out_layers->front_rgb[3 * oi + 1] = static_cast<float>(occ_g / occ_w);
                        out_layers->front_rgb[3 * oi + 2] = static_cast<float>(occ_b / occ_w);
                    }
                    if (all_w > 1e-8) {
                        out_layers->coverage[oi] = static_cast<float>(occ_w / all_w);
                        out_layers->angular_support[oi] = static_cast<float>(1.0 - occ_w / all_w);
                    }
                }
                if (behind_fg) {
                    double clr_lums = 0.0, clr_w = 0.0;
                    int clr_cnt = 0;
                    for (size_t s = 0; s < samples_rgb.size(); ++s) {
                        if (samples_clr[s]) { clr_lums += samples_lum[s] * samples_w[s]; clr_w += samples_w[s]; ++clr_cnt; }
                    }
                    if (clr_cnt >= 2 && clr_w > 1e-8) {
                        const float mode_lum = static_cast<float>(clr_lums / clr_w);
                        const float sig = std::max(0.01f, 3.0f * std::sqrt(in.noise_read_var + in.noise_shot * std::max(0.0f, mode_lum)));
                        double sr = 0, sg = 0, sb = 0, sw = 0;
                        for (size_t s = 0; s < samples_rgb.size(); ++s) {
                            if (!samples_clr[s]) continue;
                            const float z = std::fabs(samples_lum[s] - mode_lum) / sig;
                            const double wt = samples_w[s] * (1.0 / (1.0 + z * z));
                            sr += wt * samples_rgb[s][0]; sg += wt * samples_rgb[s][1];
                            sb += wt * samples_rgb[s][2]; sw += wt;
                        }
                        if (sw > 1e-8) {
                            out[3 * oi + 0] = static_cast<float>(sr / sw);
                            out[3 * oi + 1] = static_cast<float>(sg / sw);
                            out[3 * oi + 2] = static_cast<float>(sb / sw);
                            if (out_layers) {
                                out_layers->rear_rgb[3 * oi + 0] = out[3 * oi + 0];
                                out_layers->rear_rgb[3 * oi + 1] = out[3 * oi + 1];
                                out_layers->rear_rgb[3 * oi + 2] = out[3 * oi + 2];
                            }
                            continue;
                        }
                    }
                }
                double sr = 0, sg = 0, sb = 0, sw = 0;
                for (size_t s = 0; s < samples_rgb.size(); ++s) {
                    const double wt = samples_w[s];
                    sr += wt * samples_rgb[s][0]; sg += wt * samples_rgb[s][1];
                    sb += wt * samples_rgb[s][2]; sw += wt;
                }
                if (sw > 1e-8) {
                    out[3 * oi + 0] = static_cast<float>(sr / sw);
                    out[3 * oi + 1] = static_cast<float>(sg / sw);
                    out[3 * oi + 2] = static_cast<float>(sb / sw);
                    if (out_layers) {
                        out_layers->rear_rgb[3 * oi + 0] = out[3 * oi + 0];
                        out_layers->rear_rgb[3 * oi + 1] = out[3 * oi + 1];
                        out_layers->rear_rgb[3 * oi + 2] = out[3 * oi + 2];
                    }
                }
            }
        }
    };
    if (parallel_rows) parallel_rows(out_H, ray_rows); else ray_rows(0, out_H);
    return out;
}

} // namespace heraw_refocus
