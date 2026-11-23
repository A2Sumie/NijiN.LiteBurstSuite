// heraw_stage4c_subspace.hpp -- Stage 4C deterministic physics subspace operators:
//   1. M2: Right-censored photodiode forward-projection KKT inequality enforcement
//   2. M5-1: Frame-coherent 2x axial/diagonal Nyquist carrier regression
//   3. M5-2: Per-channel [-1, 1)^2 CFA sublattice & Brillouin coset damped SVD unmixing
// Pure 7-bit ASCII C++20 header. Zero heap allocations inside tile workers; 2-color parallel COLA.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <functional>
#include <numbers>
#include <vector>

namespace heraw_s4c {

namespace detail {

inline int clamp_i(int v, int lo, int hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float clamp_f(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float bilerp_clamped(const float* img, int H, int W, float y, float x) noexcept {
    if (!img || H <= 0 || W <= 0) return 0.0f;
    const float yc = clamp_f(y, 0.0f, float(H - 1));
    const float xc = clamp_f(x, 0.0f, float(W - 1));
    const int y0 = int(yc), x0 = int(xc);
    const int y1 = std::min(H - 1, y0 + 1), x1 = std::min(W - 1, x0 + 1);
    const float fy = yc - float(y0), fx = xc - float(x0);
    const float* r0 = img + size_t(y0) * size_t(W);
    const float* r1 = img + size_t(y1) * size_t(W);
    return (1.0f - fy) * ((1.0f - fx) * r0[x0] + fx * r0[x1]) +
           fy * ((1.0f - fx) * r1[x0] + fx * r1[x1]);
}

template <int N>
inline void fft1d_fixed(std::complex<double>* a, bool inverse) noexcept {
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        const double ang = 2.0 * std::numbers::pi_v<double> / double(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < N; i += len) {
            std::complex<double> w(1.0, 0.0);
            const int half = len >> 1;
            for (int j = 0; j < half; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + half] * w;
                a[i + j] = u + v;
                a[i + j + half] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        constexpr double inv = 1.0 / double(N);
        for (int i = 0; i < N; ++i) a[i] *= inv;
    }
}

template <int N>
inline void fft2d_fixed(std::array<std::complex<double>, N * N>& grid, bool inverse) noexcept {
    std::array<std::complex<double>, N> col;
    for (int r = 0; r < N; ++r) fft1d_fixed<N>(grid.data() + size_t(r * N), inverse);
    for (int c = 0; c < N; ++c) {
        for (int r = 0; r < N; ++r) col[size_t(r)] = grid[size_t(r * N + c)];
        fft1d_fixed<N>(col.data(), inverse);
        for (int r = 0; r < N; ++r) grid[size_t(r * N + c)] = col[size_t(r)];
    }
}

inline void fft1d_pow2(std::complex<double>* a, int N, bool inverse) noexcept {
    for (int i = 1, j = 0; i < N; ++i) {
        int bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= N; len <<= 1) {
        const double ang = 2.0 * std::numbers::pi_v<double> / double(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        const int half = len >> 1;
        for (int i = 0; i < N; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < half; ++j) {
                const auto u = a[i + j];
                const auto v = a[i + j + half] * w;
                a[i + j] = u + v;
                a[i + j + half] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        const double inv = 1.0 / double(N);
        for (int i = 0; i < N; ++i) a[i] *= inv;
    }
}

inline void fft2d_pow2(std::vector<std::complex<double>>& grid, int H, int W, bool inverse) {
    std::vector<std::complex<double>> col(static_cast<size_t>(H));
    for (int r = 0; r < H; ++r) fft1d_pow2(grid.data() + size_t(r) * size_t(W), W, inverse);
    for (int c = 0; c < W; ++c) {
        for (int r = 0; r < H; ++r) col[size_t(r)] = grid[size_t(r) * size_t(W) + size_t(c)];
        fft1d_pow2(col.data(), H, inverse);
        for (int r = 0; r < H; ++r) grid[size_t(r) * size_t(W) + size_t(c)] = col[size_t(r)];
    }
}

inline double bicubic_clamped(const float* img, int H, int W, float y, float x) noexcept {
    const float yc = clamp_f(y, 0.0f, float(H - 1));
    const float xc = clamp_f(x, 0.0f, float(W - 1));
    const int iy = int(std::floor(yc)), ix = int(std::floor(xc));
    const double ty = double(yc) - double(iy), tx = double(xc) - double(ix);
    auto cw = [](double t, double w[4]) noexcept {
        const double t2 = t * t, t3 = t2 * t;
        w[0] = -0.5 * t + t2 - 0.5 * t3;
        w[1] = 1.0 - 2.5 * t2 + 1.5 * t3;
        w[2] = 0.5 * t + 2.0 * t2 - 1.5 * t3;
        w[3] = -0.5 * t2 + 0.5 * t3;
    };
    double wy_v[4], wx_v[4];
    cw(ty, wy_v);
    cw(tx, wx_v);
    double sum = 0.0;
    for (int dy = -1; dy <= 2; ++dy) {
        const int ry = clamp_i(iy + dy, 0, H - 1);
        const float* row = img + size_t(ry) * size_t(W);
        double rsum = 0.0;
        for (int dx = -1; dx <= 2; ++dx) {
            rsum += wx_v[dx + 1] * double(row[clamp_i(ix + dx, 0, W - 1)]);
        }
        sum += wy_v[dy + 1] * rsum;
    }
    return sum;
}

inline void sep_gauss_2d_clamped(const float* src, float* dst, int H, int W, float sigma) {
    const int rad = std::clamp(int(std::ceil(2.6f * sigma)), 1, 36);
    std::vector<float> k(size_t(2 * rad + 1));
    double ks = 0.0;
    const double inv2s2 = 0.5 / (double(sigma) * double(sigma));
    for (int d = -rad; d <= rad; ++d) {
        const double w = std::exp(-double(d * d) * inv2s2);
        k[size_t(d + rad)] = float(w);
        ks += w;
    }
    for (float& v : k) v = float(double(v) / ks);
    std::vector<float> tmp(size_t(H) * size_t(W));
    for (int r = 0; r < H; ++r) {
        const float* srow = src + size_t(r) * size_t(W);
        float* trow = tmp.data() + size_t(r) * size_t(W);
        for (int c = 0; c < W; ++c) {
            float acc = 0.0f;
            for (int d = -rad; d <= rad; ++d) acc += k[size_t(d + rad)] * srow[clamp_i(c + d, 0, W - 1)];
            trow[c] = acc;
        }
    }
    for (int r = 0; r < H; ++r) {
        float* drow = dst + size_t(r) * size_t(W);
        for (int c = 0; c < W; ++c) drow[c] = 0.0f;
        for (int d = -rad; d <= rad; ++d) {
            const float kw = k[size_t(d + rad)];
            const float* trow = tmp.data() + size_t(clamp_i(r + d, 0, H - 1)) * size_t(W);
            for (int c = 0; c < W; ++c) drow[c] += kw * trow[c];
        }
    }
}

template <int MaxD>
inline void jacobi_sym_eig(int D, double A[MaxD][MaxD], double evals[MaxD], double evecs[MaxD][MaxD]) noexcept {
    for (int i = 0; i < D; ++i) {
        for (int j = 0; j < D; ++j) evecs[i][j] = (i == j) ? 1.0 : 0.0;
    }
    if (D <= 1) {
        if (D == 1) evals[0] = A[0][0];
        return;
    }
    for (int sweep = 0; sweep < 64; ++sweep) {
        int p = 0, q = 1;
        double mx = 0.0;
        for (int i = 0; i < D; ++i) {
            for (int j = i + 1; j < D; ++j) {
                const double av = std::abs(A[i][j]);
                if (av > mx) { mx = av; p = i; q = j; }
            }
        }
        if (mx < 1e-14) break;
        const double app = A[p][p], aqq = A[q][q], apq = A[p][q];
        const double tau = (aqq - app) / (2.0 * apq);
        const double t = (tau >= 0.0)
            ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
            : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
        const double c = 1.0 / std::sqrt(1.0 + t * t);
        const double s = t * c;
        A[p][p] = app - t * apq;
        A[q][q] = aqq + t * apq;
        A[p][q] = A[q][p] = 0.0;
        for (int k = 0; k < D; ++k) {
            if (k == p || k == q) continue;
            const double akp = A[k][p], akq = A[k][q];
            A[k][p] = A[p][k] = c * akp - s * akq;
            A[k][q] = A[q][k] = s * akp + c * akq;
        }
        for (int k = 0; k < D; ++k) {
            const double vkp = evecs[k][p], vkq = evecs[k][q];
            evecs[k][p] = c * vkp - s * vkq;
            evecs[k][q] = s * vkp + c * vkq;
        }
    }
    for (int i = 0; i < D; ++i) evals[i] = A[i][i];
    for (int i = 0; i < D; ++i) {
        int best = i;
        for (int j = i + 1; j < D; ++j) {
            if (evals[j] > evals[best]) best = j;
        }
        if (best != i) {
            std::swap(evals[i], evals[best]);
            for (int k = 0; k < D; ++k) std::swap(evecs[k][i], evecs[k][best]);
        }
    }
}

inline bool invert_2x2_spd(double m00, double m01, double m11,
                           double half_sqrt[2][2], double inv_half_sqrt[2][2]) noexcept {
    const double tr = m00 + m11;
    if (tr <= 1e-12) return false;
    const double df = m00 - m11;
    const double disc = std::sqrt(std::max(0.0, df * df + 4.0 * m01 * m01));
    const double l1 = std::max(0.0, 0.5 * (tr + disc));
    const double l2 = std::max(0.0, 0.5 * (tr - disc));
    if (l1 <= 1e-12) return false;
    double v1x = 1.0, v1y = 0.0;
    if (std::abs(m01) > 1e-13) {
        v1x = l1 - m11;
        v1y = m01;
        const double nm = std::hypot(v1x, v1y);
        if (nm > 1e-15) { v1x /= nm; v1y /= nm; }
    } else if (m11 > m00) {
        v1x = 0.0; v1y = 1.0;
    }
    const double v2x = -v1y, v2y = v1x;
    const double s1 = std::sqrt(l1);
    const double is1 = 1.0 / s1;
    const bool r2 = (l2 > 1e-5 * l1);
    const double s2 = r2 ? std::sqrt(l2) : 0.0;
    const double is2 = r2 ? (1.0 / s2) : 0.0;
    half_sqrt[0][0] = s1 * v1x * v1x + s2 * v2x * v2x;
    half_sqrt[0][1] = s1 * v1x * v1y + s2 * v2x * v2y;
    half_sqrt[1][0] = half_sqrt[0][1];
    half_sqrt[1][1] = s1 * v1y * v1y + s2 * v2y * v2y;

    inv_half_sqrt[0][0] = is1 * v1x * v1x + is2 * v2x * v2x;
    inv_half_sqrt[0][1] = is1 * v1x * v1y + is2 * v2x * v2y;
    inv_half_sqrt[1][0] = inv_half_sqrt[0][1];
    inv_half_sqrt[1][1] = is1 * v1y * v1y + is2 * v2y * v2y;
    return true;
}

} // namespace detail

using ParallelForFn = std::function<void(int, const std::function<void(int, int)>&)>;

struct FrameCensorSpec {
    std::array<float, 4> clip_level{1.0f, 1.0f, 1.0f, 1.0f};
    float adc_step = 1.0f / 16383.0f;
    float sigma_read = 1.5f / 16383.0f;
};

struct Stage4cOpticsShift {
    int full_H1 = 0;
    int full_W1 = 0;
    int origin_y_1x = 0;
    int origin_x_1x = 0;
    float lca_r_k1 = 0.0f, lca_r_k2 = 0.0f;
    float lca_b_k1 = 0.0f, lca_b_k2 = 0.0f;
    const std::vector<float>* frame_ois_shifts = nullptr;
};

inline bool is_site_nearwhite_clipped(double dn_or_lin, double clip_level, double half_step) noexcept {
    return dn_or_lin >= (clip_level - half_step);
}

inline double conservative_censored_lower_bound(double clip_level, double adc_step, double sigma_read) noexcept {
    return clip_level - 0.5 * adc_step - 6.0 * sigma_read;
}

// M2: Enforce conservative forward-projection inequalities A x >= L on right-censored raw sites.
// Strictly preserves all pixels supported by unsaturated observations (inv_m == 0 when unsat_mass > 0).
inline size_t enforce_censored_site_projections(
    float* rgb,
    int H1,
    int W1,
    int out_scale,
    int core_y0_1x,
    int core_y1_1x,
    const uint8_t cfa6x6[6][6],
    const std::vector<std::vector<float>>& frames_lin,
    const std::vector<std::vector<float>>& flows_fy,
    const std::vector<std::vector<float>>& flows_fx,
    const std::vector<std::vector<float>>& mw_half,
    const std::vector<FrameCensorSpec>& fspecs,
    const Stage4cOpticsShift& opt = {},
    const std::array<float, 3>& cam_wb = {1.0f, 1.0f, 1.0f},
    bool bracket_exposures = true,
    bool allow_wb_headroom_prior = false,
    uint8_t* bound_only_mask_out = nullptr) {
    const int N = static_cast<int>(frames_lin.size());
    if (!rgb || N <= 0 || H1 < 4 || W1 < 4 || (out_scale != 1 && out_scale != 2)) return 0;
    if (static_cast<int>(fspecs.size()) < N) return 0;
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int H_out = H1 * out_scale, W_out = W1 * out_scale;
    const int y0_1x = detail::clamp_i(core_y0_1x, 0, H1);
    const int y1_1x = detail::clamp_i(core_y1_1x > 0 ? core_y1_1x : H1, y0_1x, H1);
    if (y0_1x >= y1_1x) return 0;

    // Fast pre-scan + bounding box of clipped rows/cols so localized highlights skip 99% of the frame!
    const int scan_r0 = std::max(0, y0_1x - 4);
    const int scan_r1 = std::min(H1, y1_1x + 4);
    int clip_r_min = H1, clip_r_max = -1, clip_c_min = W1, clip_c_max = -1;
    for (int n = 0; n < N; ++n) {
        if (frames_lin[n].size() < size_t(H1) * size_t(W1)) continue;
        const auto& sp = fspecs[n];
        const float half_s = 0.5f * sp.adc_step;
        const float thr_min = std::min({sp.clip_level[0], sp.clip_level[1], sp.clip_level[2], sp.clip_level[3]}) - half_s;
        const float* fp = frames_lin[n].data();
        const float* mwp = (n < int(mw_half.size()) && mw_half[n].size() >= size_t(Hh) * size_t(Wh)) ? mw_half[n].data() : nullptr;
        for (int r = scan_r0; r < scan_r1; ++r) {
            const float* frow = fp + size_t(r) * size_t(W1);
            const int rh = detail::clamp_i(r >> 1, 0, std::max(0, Hh - 1));
            const int p_row = (r & 1) << 1;
            for (int c = 0; c < W1; ++c) {
                const float v = frow[c];
                if (v < thr_min) continue;
                const float clip_p = sp.clip_level[p_row | (c & 1)];
                if (v >= clip_p - half_s) {
                    const float mw = mwp ? mwp[size_t(rh) * size_t(Wh) + detail::clamp_i(c >> 1, 0, std::max(0, Wh - 1))] : 1.0f;
                    if (mw >= 0.35f) {
                        clip_r_min = std::min(clip_r_min, r);
                        clip_r_max = std::max(clip_r_max, r);
                        clip_c_min = std::min(clip_c_min, c);
                        clip_c_max = std::max(clip_c_max, c);
                    }
                }
            }
        }
    }
    if (clip_r_max < clip_r_min) return 0;

    // Restrict accumulation window around clipped sites (+16 px margin for flow & kernel radius)
    const int wr0 = std::max(scan_r0, clip_r_min - 16);
    const int wr1 = std::min(scan_r1, clip_r_max + 17);
    const int wc0 = std::max(0, clip_c_min - 16);
    const int wc1 = std::min(W1, clip_c_max + 17);

    const int full_H1 = opt.full_H1 > 0 ? opt.full_H1 : H1;
    const int full_W1 = opt.full_W1 > 0 ? opt.full_W1 : W1;
    const float opt_cy = 0.5f * float(full_H1);
    const float opt_cx = 0.5f * float(full_W1);
    const float inv_opt_r = 1.0f / std::max(1.0f, std::max(opt_cy, opt_cx));

    // Robust global median flow per frame over UNSATURATED cells to clamp runaway flow inside flat/clipped patches
    std::vector<std::array<float, 2>> med_flow(static_cast<size_t>(N), {0.0f, 0.0f});
    std::vector<float> iqr_flow(static_cast<size_t>(N), 999.0f);
    for (int n = 1; n < N; ++n) {
        if (n >= int(flows_fx.size()) || flows_fx[n].empty() || flows_fy[n].empty()) continue;
        const float* fp_n = (frames_lin[n].size() >= size_t(H1) * size_t(W1)) ? frames_lin[n].data() : nullptr;
        const float* mwp_n = (n < int(mw_half.size()) && mw_half[n].size() >= size_t(Hh) * size_t(Wh)) ? mw_half[n].data() : nullptr;
        const float unsat_lim = 0.85f * std::min({fspecs[n].clip_level[0], fspecs[n].clip_level[1],
                                                  fspecs[n].clip_level[2], fspecs[n].clip_level[3]});
        std::vector<float> sx, sy;
        const size_t step = std::max<size_t>(1u, flows_fx[n].size() / 1024u);
        sx.reserve(1032); sy.reserve(1032);
        for (size_t i = 0; i < flows_fx[n].size(); i += step) {
            if (mwp_n && mwp_n[i] < 0.50f) continue;
            if (fp_n) {
                const int rh = int(i / size_t(std::max(1, Wh)));
                const int ch_x = int(i % size_t(std::max(1, Wh)));
                const int r0 = detail::clamp_i(2 * rh, 0, H1 - 2);
                const int c0 = detail::clamp_i(2 * ch_x, 0, W1 - 2);
                const float mx2 = std::max(std::max(fp_n[size_t(r0) * W1 + c0], fp_n[size_t(r0) * W1 + c0 + 1]),
                                           std::max(fp_n[size_t(r0 + 1) * W1 + c0], fp_n[size_t(r0 + 1) * W1 + c0 + 1]));
                if (mx2 >= unsat_lim) continue;
            }
            sx.push_back(2.0f * flows_fx[n][i]);
            sy.push_back(2.0f * flows_fy[n][i]);
        }
        if (sx.size() < 8u) {
            sx.clear(); sy.clear();
            for (size_t i = 0; i < flows_fx[n].size(); i += step) {
                sx.push_back(2.0f * flows_fx[n][i]);
                sy.push_back(2.0f * flows_fy[n][i]);
            }
        }
        if (!sx.empty()) {
            std::sort(sx.begin(), sx.end());
            std::sort(sy.begin(), sy.end());
            med_flow[size_t(n)] = {sx[sx.size() / 2u], sy[sy.size() / 2u]};
            iqr_flow[size_t(n)] = std::max(sx[(sx.size() * 3u) / 4u] - sx[sx.size() / 4u],
                                           sy[(sy.size() * 3u) / 4u] - sy[sy.size() / 4u]);
        }
    }

    const int sub_oy0 = std::max(0, (wr0 - 4) * out_scale);
    const int sub_oy1 = std::min(H_out, (wr1 + 4) * out_scale);
    const int sub_ox0 = std::max(0, (wc0 - 4) * out_scale);
    const int sub_ox1 = std::min(W_out, (wc1 + 4) * out_scale);
    const int sub_oh = std::max(1, sub_oy1 - sub_oy0);
    const int sub_ow = std::max(1, sub_ox1 - sub_ox0);
    const size_t n_sub3 = size_t(sub_oh) * size_t(sub_ow) * 3u;

    std::vector<float> unsat_mass(n_sub3, 0.0f);
    std::vector<float> bound_wsum(n_sub3, 0.0f);
    std::vector<float> bound_vsum(n_sub3, 0.0f);

    struct ClippedObs {
        int y_c_out = 0;
        int x_c_out = 0;
        uint8_t ch = 0;
        float lower = 0.0f;
    };
    std::vector<ClippedObs> clipped_list;
    clipped_list.reserve(4096);

    auto ch_shift = [&](int n, uint8_t ch, int rr, int cc, bool near_sat, float& dy_tot, float& dx_tot) noexcept {
        const float yh = detail::clamp_f(0.5f * float(rr), 0.0f, float(std::max(0, Hh - 1)));
        const float xh = detail::clamp_f(0.5f * float(cc), 0.0f, float(std::max(0, Wh - 1)));
        float dy = (n > 0 && n < int(flows_fy.size()) && !flows_fy[n].empty())
            ? (2.0f * detail::bilerp_clamped(flows_fy[n].data(), Hh, Wh, yh, xh)) : 0.0f;
        float dx = (n > 0 && n < int(flows_fx.size()) && !flows_fx[n].empty())
            ? (2.0f * detail::bilerp_clamped(flows_fx[n].data(), Hh, Wh, yh, xh)) : 0.0f;
        if (n > 0 && (iqr_flow[size_t(n)] < 0.45f || near_sat)) {
            const float tol = std::max(0.6f, std::min(1.5f, 1.5f * iqr_flow[size_t(n)]));
            dx = detail::clamp_f(dx, med_flow[size_t(n)][0] - tol, med_flow[size_t(n)][0] + tol);
            dy = detail::clamp_f(dy, med_flow[size_t(n)][1] - tol, med_flow[size_t(n)][1] + tol);
        }
        if (ch == 0 || ch == 2) {
            const int b = (ch == 2) ? 1 : 0;
            const float ry = float(rr + opt.origin_y_1x) - opt_cy;
            const float rx = float(cc + opt.origin_x_1x) - opt_cx;
            const float rn = (rx * inv_opt_r) * (rx * inv_opt_r) + (ry * inv_opt_r) * (ry * inv_opt_r);
            const float lca = (b ? opt.lca_b_k1 : opt.lca_r_k1) + (b ? opt.lca_b_k2 : opt.lca_r_k2) * rn;
            dy += lca * ry;
            dx += lca * rx;
            if (opt.frame_ois_shifts && opt.frame_ois_shifts->size() >= size_t(n + 1) * 4u) {
                dy += (*opt.frame_ois_shifts)[size_t(n) * 4u + size_t(b * 2)];
                dx += (*opt.frame_ois_shifts)[size_t(n) * 4u + size_t(b * 2 + 1)];
            }
        }
        dy_tot = dy;
        dx_tot = dx;
    };

    for (int n = 0; n < N; ++n) {
        if (frames_lin[n].size() < size_t(H1) * size_t(W1)) continue;
        const auto& sp = fspecs[n];
        const float half_s = 0.5f * sp.adc_step;
        const float* fp = frames_lin[n].data();
        const float* mwp = (n < int(mw_half.size()) && mw_half[n].size() >= size_t(Hh) * size_t(Wh)) ? mw_half[n].data() : nullptr;

        for (int rr = wr0; rr < wr1; ++rr) {
            const float* frow = fp + size_t(rr) * size_t(W1);
            const uint8_t* rcfa = cfa6x6[rr % 6];
            const int p_row = (rr & 1) << 1;
            const float yh = 0.5f * float(rr);
            for (int cc = wc0; cc < wc1; ++cc) {
                const float mw = (n == 0) ? 1.0f : (mwp ? detail::bilerp_clamped(mwp, Hh, Wh, yh, 0.5f * float(cc)) : 1.0f);
                if (mw < 0.35f) continue;
                const uint8_t ch = rcfa[cc % 6];
                const float v = frow[cc];
                const float clip_p = sp.clip_level[p_row | (cc & 1)];
                const bool is_sat = (v >= clip_p - half_s);
                float dy = 0.0f, dx = 0.0f;
                ch_shift(n, ch, rr, cc, is_sat || (v >= 0.85f * clip_p), dy, dx);
                const float y_anc = float(rr) - dy;
                const float x_anc = float(cc) - dx;
                if (y_anc < -1.0f || y_anc > float(H1) || x_anc < -1.0f || x_anc > float(W1)) continue;

                const float yc_out = y_anc * float(out_scale);
                const float xc_out = x_anc * float(out_scale);

                if (!is_sat) {
                    // Exact per-sublattice covering radius: Green quincunx L_inf spacing is 1.0 native px
                    // (1.25x margin avoids bleeding >1 cell deep into clipped interiors), R/B is 2.0 native px.
                    const float rad_u = ((ch == 1) ? 1.25f : 2.05f) * float(out_scale);
                    const float inv_ru = 1.0f / rad_u;
                    const int oy0 = std::max(sub_oy0, int(std::ceil(yc_out - rad_u + 1e-4f)));
                    const int oy1 = std::min(sub_oy1 - 1, int(std::floor(yc_out + rad_u - 1e-4f)));
                    const int ox0 = std::max(sub_ox0, int(std::ceil(xc_out - rad_u + 1e-4f)));
                    const int ox1 = std::min(sub_ox1 - 1, int(std::floor(xc_out + rad_u - 1e-4f)));
                    const float w_obs = mw / std::max(1e-8f, sp.sigma_read * sp.sigma_read);
                    for (int oy = oy0; oy <= oy1; ++oy) {
                        const float wy = std::max(0.0f, 1.0f - std::abs(float(oy) - yc_out) * inv_ru);
                        if (wy <= 0.0f) continue;
                        const size_t rbase = size_t(oy - sub_oy0) * size_t(sub_ow);
                        for (int ox = ox0; ox <= ox1; ++ox) {
                            const float wx = std::max(0.0f, 1.0f - std::abs(float(ox) - xc_out) * inv_ru);
                            const float w = w_obs * wy * wx;
                            if (w > 0.0f) unsat_mass[(rbase + size_t(ox - sub_ox0)) * 3u + ch] += w;
                        }
                    }
                } else {
                    const float lower = float(conservative_censored_lower_bound(clip_p, sp.adc_step, sp.sigma_read));
                    if (lower <= 0.0f) continue;
                    const float rad_b = ((ch == 1) ? 1.5f : 2.0f) * float(out_scale);
                    const float inv_rb = 1.0f / rad_b;
                    const int oy0 = std::max(sub_oy0, int(std::ceil(yc_out - rad_b + 1e-4f)));
                    const int oy1 = std::min(sub_oy1 - 1, int(std::floor(yc_out + rad_b - 1e-4f)));
                    const int ox0 = std::max(sub_ox0, int(std::ceil(xc_out - rad_b + 1e-4f)));
                    const int ox1 = std::min(sub_ox1 - 1, int(std::floor(xc_out + rad_b - 1e-4f)));
                    for (int oy = oy0; oy <= oy1; ++oy) {
                        const float wy = std::max(0.0f, 1.0f - std::abs(float(oy) - yc_out) * inv_rb);
                        if (wy <= 0.0f) continue;
                        const size_t rbase = size_t(oy - sub_oy0) * size_t(sub_ow);
                        for (int ox = ox0; ox <= ox1; ++ox) {
                            const float wx = std::max(0.0f, 1.0f - std::abs(float(ox) - xc_out) * inv_rb);
                            const float w = mw * wy * wx;
                            if (w <= 0.0f) continue;
                            const size_t idx3 = (rbase + size_t(ox - sub_ox0)) * 3u + ch;
                            if (bound_wsum[idx3] <= 0.0f) {
                                bound_wsum[idx3] = w;
                                bound_vsum[idx3] = lower;
                            } else {
                                bound_vsum[idx3] = std::max(bound_vsum[idx3], lower);
                                bound_wsum[idx3] += w;
                            }
                        }
                    }
                    const int y_ci = detail::clamp_i(int(std::lround(yc_out)), sub_oy0, sub_oy1 - 1);
                    const int x_ci = detail::clamp_i(int(std::lround(xc_out)), sub_ox0, sub_ox1 - 1);
                    clipped_list.push_back({y_ci, x_ci, ch, lower});
                }
            }
        }
    }
    if (clipped_list.empty()) return 0;

    const int out_y0 = std::max(sub_oy0, y0_1x * out_scale);
    const int out_y1 = std::min(sub_oy1, y1_1x * out_scale);
    size_t changed = 0;

    // 1. Lift purely bound-only pixels (unsat_mass == 0) directly to max(current, L_max),
    //    and record per-channel right-censored bound-only bitmask (bit 0=R, bit 1=G, bit 2=B).
    for (int oy = out_y0; oy < out_y1; ++oy) {
        const size_t sbase = size_t(oy - sub_oy0) * size_t(sub_ow);
        for (int ox = sub_ox0; ox < sub_ox1; ++ox) {
            const size_t sub_idx3 = (sbase + size_t(ox - sub_ox0)) * 3u;
            const size_t full_px = size_t(oy) * size_t(W_out) + size_t(ox);
            const size_t full_idx3 = full_px * 3u;
            uint8_t bmask = 0u;
            for (int ch = 0; ch < 3; ++ch) {
                if (unsat_mass[sub_idx3 + size_t(ch)] <= 1e-4f && bound_wsum[sub_idx3 + size_t(ch)] > 1e-4f) {
                    bmask = static_cast<uint8_t>(bmask | (1u << ch));
                    const float lb = bound_vsum[sub_idx3 + size_t(ch)];
                    if (rgb[full_idx3 + size_t(ch)] < lb) {
                        rgb[full_idx3 + size_t(ch)] = lb;
                        ++changed;
                    }
                }
            }
            if (bound_only_mask_out && bmask != 0u) {
                bound_only_mask_out[full_px] = static_cast<uint8_t>(bound_only_mask_out[full_px] | bmask);
            }
        }
    }

    // 2. Forward-projection KKT half-space sweeps over clipped sites:
    //    Pixels with direct unsaturated support (unsat_mass > 1e-4) have inv_m == 0.0 and are NEVER altered.
    for (int sweep = 0; sweep < 3; ++sweep) {
        for (const auto& obs : clipped_list) {
            if (obs.y_c_out < out_y0 - 2 || obs.y_c_out >= out_y1 + 2) continue;
            const int r_box = (out_scale == 2) ? 1 : 0;
            float proj = 0.0f, a_sum = 0.0f;
            struct Tap { size_t full_idx3; float a; float inv_m; int oy; };
            Tap taps[9];
            int nt = 0;
            for (int dy = -r_box; dy <= r_box; ++dy) {
                const int oy = detail::clamp_i(obs.y_c_out + dy, sub_oy0, sub_oy1 - 1);
                const float wy = (r_box == 0) ? 1.0f : ((dy == 0) ? 0.5f : 0.25f);
                for (int dx = -r_box; dx <= r_box; ++dx) {
                    const int ox = detail::clamp_i(obs.x_c_out + dx, sub_ox0, sub_ox1 - 1);
                    const float wx = (r_box == 0) ? 1.0f : ((dx == 0) ? 0.5f : 0.25f);
                    const float a = wy * wx;
                    const size_t sub_idx3 = (size_t(oy - sub_oy0) * size_t(sub_ow) + size_t(ox - sub_ox0)) * 3u + obs.ch;
                    const size_t full_idx3 = (size_t(oy) * size_t(W_out) + size_t(ox)) * 3u + obs.ch;
                    const float inv_m = (unsat_mass[sub_idx3] > 1e-4f) ? 0.0f : 1.0f;
                    taps[nt++] = {full_idx3, a, inv_m, oy};
                    proj += a * rgb[full_idx3];
                    a_sum += a;
                }
            }
            if (a_sum <= 0.0f) continue;
            proj /= a_sum;
            const float deficit = obs.lower - proj;
            if (deficit <= 1e-6f) continue;
            float denom = 0.0f, full_norm = 0.0f;
            for (int k = 0; k < nt; ++k) {
                const float an = taps[k].a / a_sum;
                denom += taps[k].inv_m * an * an;
                full_norm += an * an;
            }
            if (denom <= 1e-18f) continue;
            // If reliable neighbours supply the majority of the squared footprint, the
            // missing fraction is too weak to disambiguate a shifted clipped edge.
            if (bracket_exposures && denom < 0.5f * full_norm) continue;
            // With bracket support elsewhere in the footprint, project in the full
            // footprint metric before freezing measured samples (a relaxed inequality).
            const float step = deficit / (bracket_exposures ? full_norm : denom);
            for (int k = 0; k < nt; ++k) {
                if (taps[k].inv_m <= 0.0f || taps[k].oy < out_y0 || taps[k].oy >= out_y1) continue;
                const float an = taps[k].a / a_sum;
                const float dx_v = taps[k].inv_m * an * step;
                if (dx_v > 1e-7f) {
                    rgb[taps[k].full_idx3] += dx_v;
                    ++changed;
                }
            }
        }
    }

    // 3. Multi-Frame White-Balance Headroom Highlight Reconstruction (equal-exposure bursts):
    //    When Green saturates at raw=1.0 while R (wb_r > wb_g) or B (wb_b > wb_g) retains scene-radiance
    //    headroom up to wb_c / wb_g, Green-guided chroma fusion loses sharp R/B highlight modulation and
    //    pins G to 1.0 (causing magenta highlight clipping and damped specular crests). Restore direct
    //    multi-frame R/B observations inside Green-saturated highlight zones and reconstruct censored G
    //    via local unsaturated highlight-shoulder chromaticity ratios under censored MLE.
    const float wb_r = std::max(0.25f, cam_wb[0]);
    const float wb_g = std::max(0.25f, cam_wb[1]);
    const float wb_b = std::max(0.25f, cam_wb[2]);
    if (!bracket_exposures && N >= 2 && std::max(wb_r, wb_b) > 1.05f * wb_g) {
        const size_t n_sub1 = size_t(sub_oh) * size_t(sub_ow);
        bool any_g_sat = false;
        for (size_t i = 0; i < n_sub1; ++i) {
            if (bound_wsum[i * 3u + 1u] > 0.05f) { any_g_sat = true; break; }
        }
        if (any_g_sat) {
            std::vector<float> dir_vsum(n_sub3, 0.0f), dir_wsum(n_sub3, 0.0f);
            std::vector<float> crs_vsum(n_sub3, 0.0f), crs_wsum(n_sub3, 0.0f);
            const float rad_fine = 0.80f * float(out_scale);
            const float inv_rad_fine = 1.0f / rad_fine;
            const float rad_crs = 2.15f * float(out_scale);
            const float inv_rad_crs = 1.0f / rad_crs;
            for (int n = 0; n < N; ++n) {
                if (frames_lin[n].size() < size_t(H1) * size_t(W1)) continue;
                const auto& sp = fspecs[n];
                const float half_s = 0.5f * sp.adc_step;
                const float* fp = frames_lin[n].data();
                const float* mwp = (n < int(mw_half.size()) && mw_half[n].size() >= size_t(Hh) * size_t(Wh)) ? mw_half[n].data() : nullptr;
                for (int rr = wr0; rr < wr1; ++rr) {
                    const float* frow = fp + size_t(rr) * size_t(W1);
                    const uint8_t* rcfa = cfa6x6[rr % 6];
                    const int p_row = (rr & 1) << 1;
                    const float yh = 0.5f * float(rr);
                    for (int cc = wc0; cc < wc1; ++cc) {
                        const uint8_t ch = rcfa[cc % 6];
                        if (ch == 1) continue;
                        const float mw = (n == 0) ? 1.0f : (mwp ? detail::bilerp_clamped(mwp, Hh, Wh, yh, 0.5f * float(cc)) : 1.0f);
                        if (mw < 0.35f) continue;
                        const float v = frow[cc];
                        const float clip_p = sp.clip_level[p_row | (cc & 1)];
                        const bool is_sat = (v >= clip_p - half_s);
                        float dy = 0.0f, dx = 0.0f;
                        ch_shift(n, ch, rr, cc, is_sat || v >= 0.85f * clip_p, dy, dx);
                        const float y_anc = float(rr) - dy;
                        const float x_anc = float(cc) - dx;
                        if (y_anc < -1.0f || y_anc > float(H1) || x_anc < -1.0f || x_anc > float(W1)) continue;
                        const float v_eff = is_sat ? float(conservative_censored_lower_bound(clip_p, sp.adc_step, sp.sigma_read)) : v;
                        if (v_eff <= 0.0f) continue;
                        const float yc_out = y_anc * float(out_scale);
                        const float xc_out = x_anc * float(out_scale);
                        const int oy0 = std::max(sub_oy0, int(std::ceil(yc_out - rad_crs + 1e-4f)));
                        const int oy1 = std::min(sub_oy1 - 1, int(std::floor(yc_out + rad_crs - 1e-4f)));
                        const int ox0 = std::max(sub_ox0, int(std::ceil(xc_out - rad_crs + 1e-4f)));
                        const int ox1 = std::min(sub_ox1 - 1, int(std::floor(xc_out + rad_crs - 1e-4f)));
                        for (int oy = oy0; oy <= oy1; ++oy) {
                            const float dy_o = std::abs(float(oy) - yc_out);
                            const float wy_c = std::max(0.0f, 1.0f - dy_o * inv_rad_crs);
                            if (wy_c <= 0.0f) continue;
                            const float wy_f = std::max(0.0f, 1.0f - dy_o * inv_rad_fine);
                            const size_t rbase = size_t(oy - sub_oy0) * size_t(sub_ow);
                            for (int ox = ox0; ox <= ox1; ++ox) {
                                const float dx_o = std::abs(float(ox) - xc_out);
                                const float wx_c = std::max(0.0f, 1.0f - dx_o * inv_rad_crs);
                                if (wx_c <= 0.0f) continue;
                                const size_t idx3 = (rbase + size_t(ox - sub_ox0)) * 3u + ch;
                                const float wc = mw * wy_c * wx_c;
                                crs_vsum[idx3] += wc * v_eff;
                                crs_wsum[idx3] += wc;
                                const float wx_f = std::max(0.0f, 1.0f - dx_o * inv_rad_fine);
                                const float wf = mw * wy_f * wx_f;
                                if (wf > 0.0f) {
                                    dir_vsum[idx3] += wf * v_eff;
                                    dir_wsum[idx3] += wf;
                                }
                            }
                        }
                    }
                }
            }

            std::vector<float> sm_wg, sm_wr, sm_wb, sm_w;
            std::vector<float> g_sat_in(n_sub1, 0.0f), g_sat_sm(n_sub1, 0.0f);
            if (allow_wb_headroom_prior) {
                std::vector<float> sh_wg(n_sub1, 0.0f), sh_wr(n_sub1, 0.0f), sh_wb(n_sub1, 0.0f), sh_w(n_sub1, 0.0f);
                sm_wg.resize(n_sub1); sm_wr.resize(n_sub1); sm_wb.resize(n_sub1); sm_w.resize(n_sub1);
                for (int sy = 0; sy < sub_oh; ++sy) {
                    const int oy = sub_oy0 + sy;
                    for (int sx = 0; sx < sub_ow; ++sx) {
                        const int ox = sub_ox0 + sx;
                        const size_t si = size_t(sy) * size_t(sub_ow) + size_t(sx);
                        const size_t s3 = si * 3u;
                        const size_t f3 = (size_t(oy) * size_t(W_out) + size_t(ox)) * 3u;
                        if (bound_wsum[s3 + 1u] > 0.05f) g_sat_in[si] = 1.0f;
                        if (bound_wsum[s3 + 0u] <= 1e-4f && bound_wsum[s3 + 1u] <= 1e-4f && bound_wsum[s3 + 2u] <= 1e-4f) {
                            const float rv = rgb[f3 + 0u], gv = rgb[f3 + 1u], bv = rgb[f3 + 2u];
                            if (gv >= 0.05f && gv <= 0.88f && rv >= 0.03f && rv <= 0.88f && bv >= 0.03f && bv <= 0.88f) {
                                const float ws = gv * gv;
                                sh_wg[si] = ws * gv;
                                sh_wr[si] = ws * rv;
                                sh_wb[si] = ws * bv;
                                sh_w[si] = ws;
                            }
                        }
                    }
                }
                const float sig_sh = 3.0f * float(out_scale);
                detail::sep_gauss_2d_clamped(sh_wg.data(), sm_wg.data(), sub_oh, sub_ow, sig_sh);
                detail::sep_gauss_2d_clamped(sh_wr.data(), sm_wr.data(), sub_oh, sub_ow, sig_sh);
                detail::sep_gauss_2d_clamped(sh_wb.data(), sm_wb.data(), sub_oh, sub_ow, sig_sh);
                detail::sep_gauss_2d_clamped(sh_w.data(), sm_w.data(), sub_oh, sub_ow, sig_sh);
            } else {
                for (size_t si = 0; si < n_sub1; ++si) {
                    if (bound_wsum[si * 3u + 1u] > 0.05f) g_sat_in[si] = 1.0f;
                }
            }
            detail::sep_gauss_2d_clamped(g_sat_in.data(), g_sat_sm.data(), sub_oh, sub_ow, 0.75f * float(out_scale));

            const float neutral_gr = wb_r / wb_g;
            const float neutral_gb = wb_b / wb_g;
            const bool r_has_hr = (wb_r > 1.02f * wb_g);
            const bool b_has_hr = (wb_b > 1.02f * wb_g);

            for (int oy = out_y0; oy < out_y1; ++oy) {
                const int sy = oy - sub_oy0;
                for (int ox = sub_ox0; ox < sub_ox1; ++ox) {
                    const int sx = ox - sub_ox0;
                    const size_t si = size_t(sy) * size_t(sub_ow) + size_t(sx);
                    const float alpha_zone = detail::clamp_f(2.5f * g_sat_sm[si], 0.0f, 1.0f);
                    if (alpha_zone <= 1e-4f) continue;
                    const size_t s3 = si * 3u;
                    const size_t f3 = (size_t(oy) * size_t(W_out) + size_t(ox)) * 3u;

                    const float r_cur = rgb[f3 + 0u];
                    const float g_cur = rgb[f3 + 1u];
                    const float b_cur = rgb[f3 + 2u];

                    auto eval_ch_dir = [&](size_t ch_off, float cur_v) noexcept -> float {
                        const float wf = dir_wsum[s3 + ch_off];
                        const float wc = crs_wsum[s3 + ch_off];
                        const float vf = (wf > 1e-4f) ? (dir_vsum[s3 + ch_off] / wf) : ((wc > 1e-4f) ? (crs_vsum[s3 + ch_off] / wc) : cur_v);
                        const float vc = (wc > 1e-4f) ? (crs_vsum[s3 + ch_off] / wc) : vf;
                        const float beta = detail::clamp_f(wf / 0.35f, 0.0f, 1.0f);
                        return beta * vf + (1.0f - beta) * vc;
                    };
                    const float r_dir = eval_ch_dir(0u, r_cur);
                    const float b_dir = eval_ch_dir(2u, b_cur);
                    const float r_new = (1.0f - alpha_zone) * r_cur + alpha_zone * std::max(r_cur, r_dir);
                    const float b_new = (1.0f - alpha_zone) * b_cur + alpha_zone * std::max(b_cur, b_dir);

                    float g_new = g_cur;
                    if (allow_wb_headroom_prior) {
                        const float rho_gr = (sm_w[si] > 1e-5f)
                            ? detail::clamp_f(sm_wg[si] / std::max(1e-6f, sm_wr[si]), 0.5f * neutral_gr, 2.0f * neutral_gr)
                            : neutral_gr;
                        const float rho_gb = (sm_w[si] > 1e-5f)
                            ? detail::clamp_f(sm_wg[si] / std::max(1e-6f, sm_wb[si]), 0.5f * neutral_gb, 2.0f * neutral_gb)
                            : neutral_gb;

                        const float sat_flag_g = (bound_wsum[s3 + 1u] > 0.02f)
                            ? 1.0f : detail::clamp_f((g_cur - 0.90f) / 0.08f, 0.0f, 1.0f);
                        const float alpha_g = alpha_zone * sat_flag_g;
                        if (alpha_g > 1e-4f && (r_has_hr || b_has_hr)) {
                            const bool r_cens = (bound_wsum[s3 + 0u] > 0.05f);
                            const bool b_cens = (bound_wsum[s3 + 2u] > 0.05f);
                            const float gr_v = r_has_hr ? (rho_gr * r_new) : 0.0f;
                            const float gb_v = b_has_hr ? (rho_gb * b_new) : 0.0f;
                            float g_est = g_cur;
                            if (r_has_hr && b_has_hr && !r_cens && !b_cens) {
                                g_est = (wb_r * gr_v + wb_b * gb_v) / (wb_r + wb_b);
                            } else if (r_has_hr && !r_cens) {
                                g_est = b_has_hr ? std::max(gr_v, gb_v) : gr_v;
                            } else if (b_has_hr && !b_cens) {
                                g_est = r_has_hr ? std::max(gb_v, gr_v) : gb_v;
                            } else {
                                g_est = std::max(gr_v, gb_v);
                            }
                            g_new = (1.0f - alpha_g) * g_cur + alpha_g * std::max(g_cur, g_est);
                        }
                    }
                    if (std::abs(r_new - r_cur) > 1e-7f || std::abs(g_new - g_cur) > 1e-7f || std::abs(b_new - b_cur) > 1e-7f) {
                        rgb[f3 + 0u] = r_new;
                        rgb[f3 + 1u] = g_new;
                        rgb[f3 + 2u] = b_new;
                        ++changed;
                    }
                }
            }
        }
    }
    return changed;
}

// Standalone M5-nyquist 1D continuous solver (reused by module verification and tile solver).
inline bool fit_nyquist_1d_carrier(
    const double* shifts,
    const double* observed,
    int N,
    double out_coef[3],
    double* out_r2 = nullptr,
    double* out_f_stat = nullptr,
    double* out_min_sv = nullptr) noexcept {
    out_coef[0] = out_coef[1] = out_coef[2] = 0.0;
    if (!shifts || !observed || N < 3) return false;
    double GtG[8][8]{};
    double Gty[3]{};
    double mean_y = 0.0;
    for (int n = 0; n < N; ++n) mean_y += observed[n];
    mean_y /= double(N);
    double tss = 0.0;
    for (int n = 0; n < N; ++n) {
        const double p = 2.0 * std::numbers::pi_v<double> * shifts[n];
        const double g[3] = {1.0, std::cos(p), std::sin(p)};
        const double y = observed[n];
        tss += (y - mean_y) * (y - mean_y);
        for (int i = 0; i < 3; ++i) {
            Gty[i] += g[i] * y;
            for (int j = 0; j < 3; ++j) GtG[i][j] += g[i] * g[j];
        }
    }
    double evals[8]{}, evecs[8][8]{};
    detail::jacobi_sym_eig<8>(3, GtG, evals, evecs);
    const double sv_min = std::sqrt(std::max(0.0, evals[2]));
    if (out_min_sv) *out_min_sv = sv_min;
    const double e_floor = std::max(1e-12, 1e-10 * evals[0]);
    for (int k = 0; k < 3; ++k) {
        if (evals[k] <= e_floor) continue;
        double proj = 0.0;
        for (int i = 0; i < 3; ++i) proj += evecs[i][k] * Gty[i];
        const double s = proj / evals[k];
        for (int i = 0; i < 3; ++i) out_coef[i] += s * evecs[i][k];
    }
    double rss = 0.0;
    for (int n = 0; n < N; ++n) {
        const double p = 2.0 * std::numbers::pi_v<double> * shifts[n];
        const double pred = out_coef[0] + out_coef[1] * std::cos(p) + out_coef[2] * std::sin(p);
        const double e = observed[n] - pred;
        rss += e * e;
    }
    const double r2 = (tss > 1e-20) ? std::clamp(1.0 - rss / tss, 0.0, 1.0) : 0.0;
    const double dof = double(std::max(1, N - 3));
    const double f_stat = (rss > 1e-24 && tss > rss) ? (((tss - rss) * 0.5) / (rss / dof)) : ((tss > 1e-16) ? 1e9 : 0.0);
    if (out_r2) *out_r2 = r2;
    if (out_f_stat) *out_f_stat = f_stat;
    return true;
}

// M5 Stage 4C: L1-resident stack-FFT + 2-color parallel COLA sublattice & Brillouin coset unmixer.
inline void reconstruct_sublattice_and_nyquist_subspace(
    float* rgb_2x,
    int H1,
    int W1,
    int core_y0_1x,
    int core_y1_1x,
    const uint8_t cfa6x6[6][6],
    const std::array<float, 3>& cam_wb,
    bool bracket_exposures,
    const std::vector<std::vector<float>>& frames_lin,
    const std::vector<std::vector<float>>& flows_fy,
    const std::vector<std::vector<float>>& flows_fx,
    const std::vector<std::vector<float>>& mw_half,
    const std::vector<FrameCensorSpec>& fspecs,
    const Stage4cOpticsShift& opt = {},
    const ParallelForFn& par_for = nullptr) {
    const int N = static_cast<int>(frames_lin.size());
    if (!rgb_2x || N < 4 || H1 < 32 || W1 < 32) return;
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            if (cfa6x6[r][c] != cfa6x6[r & 1][c & 1]) return;
        }
    }
    int r_dy = -1, r_dx = -1, b_dy = -1, b_dx = -1;
    int g1_dy = -1, g1_dx = -1, g2_dy = -1, g2_dx = -1;
    for (int dr = 0; dr < 2; ++dr) {
        for (int dc = 0; dc < 2; ++dc) {
            const uint8_t ch = cfa6x6[dr][dc];
            if (ch == 0) { r_dy = dr; r_dx = dc; }
            else if (ch == 2) { b_dy = dr; b_dx = dc; }
            else if (ch == 1) {
                if (g1_dy < 0) { g1_dy = dr; g1_dx = dc; }
                else { g2_dy = dr; g2_dx = dc; }
            }
        }
    }
    if (r_dy < 0 || b_dy < 0 || g1_dy < 0 || g2_dy < 0) return;

    const int Hh = H1 / 2, Wh = W1 / 2;
    const int H2 = H1 * 2, W2 = W1 * 2;
    const int y0_1x = detail::clamp_i(core_y0_1x, 0, H1);
    const int y1_1x = detail::clamp_i(core_y1_1x > 0 ? core_y1_1x : H1, y0_1x, H1);
    if (y0_1x >= y1_1x) return;

    const int full_H1 = opt.full_H1 > 0 ? opt.full_H1 : H1;
    const int full_W1 = opt.full_W1 > 0 ? opt.full_W1 : W1;
    const float opt_cy = 0.5f * float(full_H1);
    const float opt_cx = 0.5f * float(full_W1);
    const float inv_opt_r = 1.0f / std::max(1.0f, std::max(opt_cy, opt_cx));

    // Global median & IQR of frame flows so globally rigid bursts ignore local alias-induced flow jitter
    std::vector<std::array<double, 2>> med_flow(static_cast<size_t>(N), {0.0, 0.0});
    std::vector<double> iqr_flow(static_cast<size_t>(N), 999.0);
    for (int n = 1; n < N; ++n) {
        if (n >= int(flows_fx.size()) || flows_fx[n].empty() || flows_fy[n].empty()) continue;
        std::vector<double> sx, sy;
        const size_t step = std::max<size_t>(1u, flows_fx[n].size() / 512u);
        sx.reserve(520); sy.reserve(520);
        for (size_t i = 0; i < flows_fx[n].size(); i += step) {
            sx.push_back(2.0 * double(flows_fx[n][i]));
            sy.push_back(2.0 * double(flows_fy[n][i]));
        }
        if (!sx.empty()) {
            std::sort(sx.begin(), sx.end());
            std::sort(sy.begin(), sy.end());
            med_flow[size_t(n)] = {sx[sx.size() / 2u], sy[sy.size() / 2u]};
            iqr_flow[size_t(n)] = std::max(sx[(sx.size() * 3u) / 4u] - sx[sx.size() / 4u],
                                           sy[(sy.size() * 3u) / 4u] - sy[sy.size() / 4u]);
        }
    }

    constexpr int TILE_1X = 32;
    constexpr int STRIDE_1X = 16;
    constexpr int TILE_2X = TILE_1X * 2;
    constexpr int SUB_N = TILE_1X / 2; // 16

    std::array<double, TILE_2X> hann64{};
    std::array<double, TILE_1X> hann32{};
    std::array<double, SUB_N> hann16{};
    for (int i = 0; i < TILE_2X; ++i) {
        const double s = std::sin(std::numbers::pi_v<double> * (double(i) + 0.5) / double(TILE_2X));
        hann64[size_t(i)] = s * s;
    }
    for (int i = 0; i < TILE_1X; ++i) {
        const double s = std::sin(std::numbers::pi_v<double> * (double(i) + 0.5) / double(TILE_1X));
        hann32[size_t(i)] = s * s;
    }
    for (int i = 0; i < SUB_N; ++i) {
        const double s = std::sin(std::numbers::pi_v<double> * (double(i) + 0.5) / double(SUB_N));
        hann16[size_t(i)] = s * s;
    }

    auto build_starts = [](int lo, int hi, int limit) {
        std::vector<int> starts;
        const int s_beg = std::max(0, (lo - STRIDE_1X) & ~1);
        const int s_end = std::min(limit - TILE_1X, hi) & ~1;
        for (int p = s_beg; p <= s_end; p += STRIDE_1X) starts.push_back(p);
        if (limit >= TILE_1X) {
            const int last = (limit - TILE_1X) & ~1;
            if ((starts.empty() || starts.back() < last) && last <= hi) starts.push_back(last);
        }
        return starts;
    };
    const auto ty_list = build_starts(y0_1x, y1_1x, H1);
    const auto tx_list = build_starts(0, W1, W1);
    if (ty_list.empty() || tx_list.empty()) return;

    const int buf_y0_2x = ty_list.front() * 2;
    const int buf_y1_2x = std::min(H2, (ty_list.back() + TILE_1X) * 2);
    const int buf_h2 = std::max(1, buf_y1_2x - buf_y0_2x);

    std::vector<float> accum_delta(size_t(buf_h2) * size_t(W2) * 3u, 0.0f);
    std::vector<float> accum_wsum(size_t(buf_h2) * size_t(W2), 0.0f);

    auto run_range = [&](int count, const auto& fn) {
        if (par_for && count > 1) {
            par_for(count, [&](int b, int e) { fn(b, e); });
        } else {
            fn(0, count);
        }
    };

    const float wbr = std::max(0.25f, cam_wb[0]);
    const float wbb = std::max(0.25f, cam_wb[2]);
    const float wb_ch[3] = {wbr, 1.0f, wbb};
    const float inv_wb_ch[3] = {1.0f / wbr, 1.0f, 1.0f / wbb};

    auto sample_rgb2x_ch = [&](double y_anc_1x, double x_anc_1x, int ch) noexcept -> double {
        const float y2 = float(y_anc_1x * 2.0);
        const float x2 = float(x_anc_1x * 2.0);
        const float yc = detail::clamp_f(y2, 0.0f, float(H2 - 1));
        const float xc = detail::clamp_f(x2, 0.0f, float(W2 - 1));
        const int iy0 = int(yc), ix0 = int(xc);
        const int iy1 = std::min(H2 - 1, iy0 + 1), ix1 = std::min(W2 - 1, ix0 + 1);
        const float fy = yc - float(iy0), fx = xc - float(ix0);
        const float* p00 = rgb_2x + (size_t(iy0) * size_t(W2) + size_t(ix0)) * 3u + ch;
        const float* p01 = rgb_2x + (size_t(iy0) * size_t(W2) + size_t(ix1)) * 3u + ch;
        const float* p10 = rgb_2x + (size_t(iy1) * size_t(W2) + size_t(ix0)) * 3u + ch;
        const float* p11 = rgb_2x + (size_t(iy1) * size_t(W2) + size_t(ix1)) * 3u + ch;
        return double((1.0f - fy) * ((1.0f - fx) * (*p00) + fx * (*p01)) +
                      fy * ((1.0f - fx) * (*p10) + fx * (*p11)));
    };

    // Process one tile row ty0 across all tx0 (thread-safe when ty_idx parity is fixed!)
    auto process_tile_row = [&](int ty0) {
        // Stack-allocated L1-resident FFT buffers per worker thread
        std::array<std::complex<double>, 16u * 16u> fft16;
        std::array<double, 16u * 16u> psd16;
        std::array<std::complex<double>, 32u * 32u> fft32;
        std::array<double, 32u * 32u> psd32;
        std::array<double, TILE_2X * TILE_2X * 3> tile_delta;

        std::vector<int> valid_frames;
        valid_frames.reserve(size_t(N));
        std::vector<std::array<double, 2>> all_rigid_g_shifts;
        all_rigid_g_shifts.reserve(size_t(N));
        std::vector<std::array<double, 2>> shift_ch[3];
        for (int c = 0; c < 3; ++c) shift_ch[c].reserve(size_t(N));
        std::vector<double> sigma_vf;
        sigma_vf.reserve(size_t(N));

        std::vector<std::array<double, SUB_N * SUB_N>> res_r, res_b, res_g1, res_g2;
        std::vector<std::array<double, TILE_1X * TILE_1X>> raw_rv_buf, res_wb_1x;
        std::vector<std::array<double, 16>> frame_blk_sums, m51_blk_sums;
        std::vector<std::array<double, 3>> frame_mean_ch;

        for (int tx0 : tx_list) {
            // Accumulate COLA window weight regardless of whether tile modifies delta
            for (int r2 = 0; r2 < TILE_2X; ++r2) {
                const int gy2 = ty0 * 2 + r2;
                if (gy2 < buf_y0_2x || gy2 >= buf_y1_2x) continue;
                const double wy = hann64[size_t(r2)];
                float* wrow = accum_wsum.data() + size_t(gy2 - buf_y0_2x) * size_t(W2) + size_t(tx0 * 2);
                for (int c2 = 0; c2 < TILE_2X; ++c2) {
                    wrow[c2] += float(wy * hann64[size_t(c2)]);
                }
            }

            valid_frames.clear();
            all_rigid_g_shifts.clear();
            for (int c = 0; c < 3; ++c) shift_ch[c].clear();
            sigma_vf.clear();

            bool tile_clipped = false;
            bool tile_any_clip_px = false;
            const float ry_c = (float(ty0 + TILE_1X / 2 + opt.origin_y_1x) - opt_cy);
            const float rx_c = (float(tx0 + TILE_1X / 2 + opt.origin_x_1x) - opt_cx);
            const float rn_c = (rx_c * inv_opt_r) * (rx_c * inv_opt_r) + (ry_c * inv_opt_r) * (ry_c * inv_opt_r);
            const float lca_r = opt.lca_r_k1 + opt.lca_r_k2 * rn_c;
            const float lca_b = opt.lca_b_k1 + opt.lca_b_k2 * rn_c;

            for (int n = 0; n < N; ++n) {
                const float* mwp = (n < int(mw_half.size()) && mw_half[n].size() >= size_t(Hh) * size_t(Wh)) ? mw_half[n].data() : nullptr;
                double mw_sum = 0.0;
                double sx_s = 0.0, sx_s2 = 0.0, sy_s = 0.0, sy_s2 = 0.0;
                int hcnt = 0;
                for (int hr = ty0 / 2; hr < (ty0 + TILE_1X) / 2; ++hr) {
                    for (int hc = tx0 / 2; hc < (tx0 + TILE_1X) / 2; ++hc) {
                        const size_t hi = size_t(detail::clamp_i(hr, 0, Hh - 1)) * size_t(Wh) + size_t(detail::clamp_i(hc, 0, Wh - 1));
                        const double mw = (n == 0) ? 1.0 : (mwp ? double(mwp[hi]) : 1.0);
                        const double dx = (n > 0 && n < int(flows_fx.size()) && !flows_fx[n].empty()) ? (2.0 * double(flows_fx[n][hi])) : 0.0;
                        const double dy = (n > 0 && n < int(flows_fy.size()) && !flows_fy[n].empty()) ? (2.0 * double(flows_fy[n][hi])) : 0.0;
                        mw_sum += mw;
                        sx_s += dx; sx_s2 += dx * dx;
                        sy_s += dy; sy_s2 += dy * dy;
                        ++hcnt;
                    }
                }
                const double mw_mean = mw_sum / double(std::max(1, hcnt));
                double mx = sx_s / double(std::max(1, hcnt));
                double my = sy_s / double(std::max(1, hcnt));
                const double std_x = std::sqrt(std::max(0.0, sx_s2 / double(std::max(1, hcnt)) - mx * mx));
                const double std_y = std::sqrt(std::max(0.0, sy_s2 / double(std::max(1, hcnt)) - my * my));
                if (n > 0 && iqr_flow[size_t(n)] < 0.25) {
                    if (std::hypot(mx - med_flow[size_t(n)][0], my - med_flow[size_t(n)][1]) <= 0.65) {
                        // Globally rigid burst: stabilize local alias-jittered flow toward global frame shift
                        const double w_glob = (std_x > 0.08 || std_y > 0.08) ? 0.65 : 0.25;
                        mx = (1.0 - w_glob) * mx + w_glob * med_flow[size_t(n)][0];
                        my = (1.0 - w_glob) * my + w_glob * med_flow[size_t(n)][1];
                    }
                } else if (std_x > 0.40 || std_y > 0.40) {
                    continue;
                }

                const int r_beg = int(std::floor(double(ty0) - std::abs(my)));
                const int r_end = int(std::ceil(double(ty0 + TILE_1X - 1) + std::abs(my)));
                const int c_beg = int(std::floor(double(tx0) - std::abs(mx)));
                const int c_end = int(std::ceil(double(tx0 + TILE_1X - 1) + std::abs(mx)));
                if (r_beg < -4 || r_end >= H1 + 4 || c_beg < -4 || c_end >= W1 + 4) continue;

                if (n < int(fspecs.size())) {
                    const auto& sp = fspecs[n];
                    const float half_s = 0.5f * sp.adc_step;
                    const float thr_min = std::min({sp.clip_level[0], sp.clip_level[1], sp.clip_level[2], sp.clip_level[3]}) - half_s;
                    const float* fp = frames_lin[n].data();
                    int clip_cnt = 0;
                    for (int rr = std::max(0, ty0 - 2); rr < std::min(H1, ty0 + TILE_1X + 2) && !tile_clipped; ++rr) {
                        const int p_row = (rr & 1) << 1;
                        const float* frow = fp + size_t(rr) * size_t(W1);
                        for (int cc = std::max(0, tx0 - 2); cc < std::min(W1, tx0 + TILE_1X + 2); ++cc) {
                            const float v = frow[cc];
                            if (v >= thr_min && v >= sp.clip_level[p_row | (cc & 1)] - half_s) {
                                tile_any_clip_px = true;
                                ++clip_cnt;
                                if (bracket_exposures || clip_cnt > 220) {
                                    tile_clipped = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (tile_clipped) break;
                all_rigid_g_shifts.push_back({mx, my});
                if (mw_mean < 0.35) continue;

                double ois_r_dy = 0.0, ois_r_dx = 0.0, ois_b_dy = 0.0, ois_b_dx = 0.0;
                if (opt.frame_ois_shifts && opt.frame_ois_shifts->size() >= size_t(n + 1) * 4u) {
                    ois_r_dy = (*opt.frame_ois_shifts)[size_t(n) * 4u + 0u];
                    ois_r_dx = (*opt.frame_ois_shifts)[size_t(n) * 4u + 1u];
                    ois_b_dy = (*opt.frame_ois_shifts)[size_t(n) * 4u + 2u];
                    ois_b_dx = (*opt.frame_ois_shifts)[size_t(n) * 4u + 3u];
                }
                valid_frames.push_back(n);
                shift_ch[1].push_back({mx, my});
                shift_ch[0].push_back({mx + double(lca_r * rx_c) + ois_r_dx, my + double(lca_r * ry_c) + ois_r_dy});
                shift_ch[2].push_back({mx + double(lca_b * rx_c) + ois_b_dx, my + double(lca_b * ry_c) + ois_b_dy});
                sigma_vf.push_back((n < int(fspecs.size())) ? std::max(1e-5, double(fspecs[n].sigma_read)) : 1e-4);
            }
            if (tile_clipped) continue;
            const int Nv = static_cast<int>(valid_frames.size());
            const bool can_try_m51 = (!bracket_exposures && !tile_any_clip_px && N >= 6 && static_cast<int>(all_rigid_g_shifts.size()) == N);
            const bool can_try_m52 = (Nv >= std::max(4, (3 * N + 3) / 4));
            if (!can_try_m51 && !can_try_m52) continue;

            tile_delta.fill(0.0);
            bool tile_modified = false;

            // Single-pass 32x32 residual extraction across all valid frames (reused by both M5-1 and M5-2)
            if (can_try_m52) {
                raw_rv_buf.resize(static_cast<size_t>(Nv));
                frame_blk_sums.resize(static_cast<size_t>(Nv));
                frame_mean_ch.resize(static_cast<size_t>(Nv));
                for (int vi = 0; vi < Nv; ++vi) {
                    const int n = valid_frames[size_t(vi)];
                    const float* fp = frames_lin[size_t(n)].data();
                    const bool have_sp = (n < int(fspecs.size()));
                    const float half_s = have_sp ? (0.5f * fspecs[size_t(n)].adc_step) : 0.0f;
                    auto& rbuf = raw_rv_buf[size_t(vi)];
                    auto& bsums = frame_blk_sums[size_t(vi)];
                    bsums.fill(0.0);
                    for (int r = 0; r < TILE_1X; ++r) {
                        const int rr = ty0 + r;
                        const int p_row = (rr & 1) << 1;
                        const uint8_t* rcfa = cfa6x6[rr & 1];
                        const float* frow = fp + size_t(rr) * size_t(W1);
                        const int br4 = (r >> 3) * 4;
                        double* rdst = rbuf.data() + size_t(r * TILE_1X);
                        for (int c = 0; c < TILE_1X; ++c) {
                            const int cc = tx0 + c;
                            const uint8_t ch = rcfa[cc & 1];
                            const float v_raw = frow[cc];
                            if (tile_any_clip_px && have_sp && v_raw >= fspecs[size_t(n)].clip_level[p_row | (cc & 1)] - half_s) {
                                rdst[c] = 0.0;
                                continue;
                            }
                            const double c_dx = shift_ch[ch][size_t(vi)][0];
                            const double c_dy = shift_ch[ch][size_t(vi)][1];
                            const double rv0 = double(v_raw) - sample_rgb2x_ch(double(rr) - c_dy, double(cc) - c_dx, ch);
                            rdst[c] = rv0;
                            bsums[size_t(br4 + (c >> 3))] += rv0 * double(wb_ch[ch]);
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // Part M5-1: Frame-coherent 2x axial Nyquist carrier {(1,0), (0,1)}
            // -----------------------------------------------------------------
            if (can_try_m51) {
                const bool use_valid_bsums = (can_try_m52 && Nv == N);
                if (!use_valid_bsums) {
                    m51_blk_sums.resize(static_cast<size_t>(N));
                    for (int n = 0; n < N; ++n) {
                        const float* fp = frames_lin[size_t(n)].data();
                        auto& bsums = m51_blk_sums[size_t(n)];
                        bsums.fill(0.0);
                        const double g_dx = all_rigid_g_shifts[size_t(n)][0];
                        const double g_dy = all_rigid_g_shifts[size_t(n)][1];
                        double ois_r_dy = 0.0, ois_r_dx = 0.0, ois_b_dy = 0.0, ois_b_dx = 0.0;
                        if (opt.frame_ois_shifts && opt.frame_ois_shifts->size() >= size_t(n + 1) * 4u) {
                            ois_r_dy = (*opt.frame_ois_shifts)[size_t(n) * 4u + 0u];
                            ois_r_dx = (*opt.frame_ois_shifts)[size_t(n) * 4u + 1u];
                            ois_b_dy = (*opt.frame_ois_shifts)[size_t(n) * 4u + 2u];
                            ois_b_dx = (*opt.frame_ois_shifts)[size_t(n) * 4u + 3u];
                        }
                        const double ch_dx[3] = {g_dx + double(lca_r * rx_c) + ois_r_dx, g_dx, g_dx + double(lca_b * rx_c) + ois_b_dx};
                        const double ch_dy[3] = {g_dy + double(lca_r * ry_c) + ois_r_dy, g_dy, g_dy + double(lca_b * ry_c) + ois_b_dy};
                        for (int r = 0; r < TILE_1X; ++r) {
                            const int rr = ty0 + r;
                            const uint8_t* rcfa = cfa6x6[rr & 1];
                            const float* frow = fp + size_t(rr) * size_t(W1);
                            const int br4 = (r >> 3) * 4;
                            for (int c = 0; c < TILE_1X; ++c) {
                                const int cc = tx0 + c;
                                const uint8_t ch = rcfa[cc & 1];
                                const double rv0 = double(frow[cc]) - sample_rgb2x_ch(double(rr) - ch_dy[ch], double(cc) - ch_dx[ch], ch);
                                bsums[size_t(br4 + (c >> 3))] += rv0 * double(wb_ch[ch]);
                            }
                        }
                    }
                }
                const auto& bsums_src = use_valid_bsums ? frame_blk_sums : m51_blk_sums;
                std::array<double, 64> mean_res{};
                std::array<double, 64> sub_var{};
                const int nv_cap = std::min(N, 64);
                for (int vi = 0; vi < nv_cap; ++vi) {
                    const auto& bsums = bsums_src[size_t(vi)];
                    double blk_means[16]{};
                    double m = 0.0;
                    for (int b = 0; b < 16; ++b) {
                        blk_means[b] = bsums[size_t(b)] * (1.0 / 64.0);
                        m += blk_means[b];
                    }
                    m *= (1.0 / 16.0);
                    double sv = 0.0;
                    for (double v : blk_means) sv += (v - m) * (v - m);
                    mean_res[size_t(vi)] = m;
                    sub_var[size_t(vi)] = sv * (1.0 / 16.0);
                }
                double m_bar = 0.0;
                for (int vi = 0; vi < nv_cap; ++vi) m_bar += mean_res[size_t(vi)];
                m_bar /= double(nv_cap);
                double tss = 0.0, max_sq = 0.0, mean_sub_var = 0.0, max_sub_var = 0.0;
                for (int vi = 0; vi < nv_cap; ++vi) {
                    const double d = mean_res[size_t(vi)] - m_bar;
                    tss += d * d;
                    max_sq = std::max(max_sq, d * d);
                    mean_sub_var += sub_var[size_t(vi)];
                    max_sub_var = std::max(max_sub_var, sub_var[size_t(vi)]);
                }
                mean_sub_var /= double(nv_cap);
                const double tss_per_f = tss / double(nv_cap);
                if (tss > 1e-8 && tss_per_f > 6.0 * mean_sub_var && max_sub_var < 0.35 * tss_per_f && (max_sq / tss) <= 0.52) {
                    double best_f = 0.0;
                    int best_axis = -1;
                    double best_coef[3]{};
                    std::array<double, 64> u_sh{};
                    for (int axis = 0; axis < 2; ++axis) {
                        double max_frac = 0.0;
                        for (int vi = 0; vi < nv_cap; ++vi) {
                            u_sh[size_t(vi)] = all_rigid_g_shifts[size_t(vi)][size_t(axis)];
                            max_frac = std::max(max_frac, std::abs(u_sh[size_t(vi)] - std::round(u_sh[size_t(vi)])));
                        }
                        if (max_frac < 0.15) continue;
                        double coef[3]{}, r2 = 0.0, f_st = 0.0, min_sv = 0.0;
                        if (fit_nyquist_1d_carrier(u_sh.data(), mean_res.data(), nv_cap, coef, &r2, &f_st, &min_sv)) {
                            if (min_sv >= 0.85 && r2 >= 0.90 && f_st >= 28.0 && f_st > best_f) {
                                best_f = f_st;
                                best_axis = axis;
                                std::memcpy(best_coef, coef, sizeof(coef));
                            }
                        }
                    }
                    const double sig0_m51 = (!fspecs.empty()) ? std::max(1e-5, double(fspecs[0].sigma_read)) : 1e-4;
                    if (best_axis >= 0 && std::abs(best_coef[1]) > 4.0 * (sig0_m51 / 16.0)) {
                        const double dc_c = use_valid_bsums ? 0.0 : best_coef[0];
                        const double amp_c = best_coef[1];
                        for (int r2 = 0; r2 < TILE_2X; ++r2) {
                            const int Y2 = ty0 * 2 + r2;
                            for (int c2 = 0; c2 < TILE_2X; ++c2) {
                                const int X2 = tx0 * 2 + c2;
                                const double sgn = (((best_axis == 0 ? X2 : Y2) & 1) == 0) ? 1.0 : -1.0;
                                const double dv = dc_c + amp_c * sgn;
                                const size_t idx3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                                tile_delta[idx3 + 0] += dv * double(inv_wb_ch[0]);
                                tile_delta[idx3 + 1] += dv;
                                tile_delta[idx3 + 2] += dv * double(inv_wb_ch[2]);
                            }
                        }
                        tile_modified = true;
                    }
                }
            }
            if (!can_try_m52) {
                if (tile_modified) {
                    for (int r2 = 0; r2 < TILE_2X; ++r2) {
                        const int gy2 = ty0 * 2 + r2;
                        if (gy2 < buf_y0_2x || gy2 >= buf_y1_2x) continue;
                        const double wy = hann64[size_t(r2)];
                        float* drow = accum_delta.data() + (size_t(gy2 - buf_y0_2x) * size_t(W2) + size_t(tx0 * 2)) * 3u;
                        for (int c2 = 0; c2 < TILE_2X; ++c2) {
                            const float w = float(wy * hann64[size_t(c2)]);
                            const size_t idx3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                            drow[c2 * 3 + 0] += w * float(tile_delta[idx3 + 0]);
                            drow[c2 * 3 + 1] += w * float(tile_delta[idx3 + 1]);
                            drow[c2 * 3 + 2] += w * float(tile_delta[idx3 + 2]);
                        }
                    }
                }
                continue;
            }

            // -----------------------------------------------------------------
            // Part M5-2: Per-channel & Brillouin coset damped SVD unmixing
            // -----------------------------------------------------------------
            res_wb_1x.resize(static_cast<size_t>(Nv));

            double blk_e16[16]{};
            double sq_r = 0.0, sq_b = 0.0, sq_wb = 0.0;
            for (int vi = 0; vi < Nv; ++vi) {
                const auto& rbuf = raw_rv_buf[size_t(vi)];
                double mean_ch[3]{};
                int cnt_ch[3]{};
                double wb_mean = 0.0;
                for (int r = 0; r < TILE_1X; ++r) {
                    const uint8_t* rcfa = cfa6x6[(ty0 + r) & 1];
                    const double* rsrc = rbuf.data() + size_t(r * TILE_1X);
                    double* wbdst = res_wb_1x[size_t(vi)].data() + size_t(r * TILE_1X);
                    for (int c = 0; c < TILE_1X; ++c) {
                        const uint8_t ch = rcfa[(tx0 + c) & 1];
                        double rv = rsrc[c];
                        if (tile_modified) {
                            const double dx = shift_ch[ch][size_t(vi)][0];
                            const double dy = shift_ch[ch][size_t(vi)][1];
                            const int r2_loc = detail::clamp_i(int(std::lround((double(r) - dy) * 2.0)), 0, TILE_2X - 1);
                            const int c2_loc = detail::clamp_i(int(std::lround((double(c) - dx) * 2.0)), 0, TILE_2X - 1);
                            rv -= tile_delta[(size_t(r2_loc) * size_t(TILE_2X) + size_t(c2_loc)) * 3u + ch];
                        }
                        const double wv = rv * double(wb_ch[ch]);
                        wbdst[c] = wv;
                        wb_mean += wv;
                        mean_ch[ch] += rv;
                        ++cnt_ch[ch];
                    }
                }
                for (int ch = 0; ch < 3; ++ch) mean_ch[ch] /= double(std::max(1, cnt_ch[ch]));
                frame_mean_ch[size_t(vi)] = {mean_ch[0], mean_ch[1], mean_ch[2]};
                wb_mean *= (1.0 / double(TILE_1X * TILE_1X));
                for (int r = 0; r < TILE_1X; ++r) {
                    double* wbdst = res_wb_1x[size_t(vi)].data() + size_t(r * TILE_1X);
                    const int br4 = (r >> 3) * 4;
                    for (int c = 0; c < TILE_1X; ++c) {
                        const double v = wbdst[c] - wb_mean;
                        wbdst[c] = v;
                        blk_e16[br4 + (c >> 3)] += v * v;
                        sq_wb += v * v;
                    }
                }
                for (int qy = 0; qy < SUB_N; ++qy) {
                    const double* rr_row = rbuf.data() + size_t((2 * qy + r_dy) * TILE_1X);
                    const double* rb_row = rbuf.data() + size_t((2 * qy + b_dy) * TILE_1X);
                    for (int qx = 0; qx < SUB_N; ++qx) {
                        const double vr = rr_row[2 * qx + r_dx] - mean_ch[0];
                        const double vb = rb_row[2 * qx + b_dx] - mean_ch[2];
                        sq_r += vr * vr;
                        sq_b += vb * vb;
                    }
                }
            }

            const double sig2_base = sigma_vf[0] * sigma_vf[0];
            const double var_r = sq_r / double(Nv * SUB_N * SUB_N);
            const double var_b = sq_b / double(Nv * SUB_N * SUB_N);
            const double var_wb = sq_wb / double(Nv * TILE_1X * TILE_1X);

            // Fast noise-floor early exit: skip sublattice unpacking & FFTs completely on 99% of clean/noise-only tiles!
            const double sig_wb = sigma_vf[0] * double(std::max({1.0f, wbr, wbb}));
            const bool need_luma_fft = (var_wb > 1.35 * sig_wb * sig_wb);
            const bool need_r_fft = (var_r > 1.35 * sig2_base);
            const bool need_b_fft = (var_b > 1.35 * sig2_base);

            double blk_mean_e = 0.0, blk_max_e = 0.0;
            for (double e : blk_e16) { blk_mean_e += e; blk_max_e = std::max(blk_max_e, e); }
            blk_mean_e *= (1.0 / 16.0);
            const double tile_conc = (blk_mean_e > 1e-14) ? (blk_max_e / blk_mean_e) : 99.0;
            // Reject point sources (PS01-05) and isolated stars (A10), where energy concentrates >6x in one 8x8 subblock
            if (blk_mean_e <= 1e-14 || tile_conc > 6.2 || (!need_luma_fft && !need_r_fft && !need_b_fft)) {
                if (tile_modified) {
                    for (int r2 = 0; r2 < TILE_2X; ++r2) {
                        const int gy2 = ty0 * 2 + r2;
                        if (gy2 < buf_y0_2x || gy2 >= buf_y1_2x) continue;
                        const double wy = hann64[size_t(r2)];
                        float* drow = accum_delta.data() + (size_t(gy2 - buf_y0_2x) * size_t(W2) + size_t(tx0 * 2)) * 3u;
                        for (int c2 = 0; c2 < TILE_2X; ++c2) {
                            const float w = float(wy * hann64[size_t(c2)]);
                            const size_t idx3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                            drow[c2 * 3 + 0] += w * float(tile_delta[idx3 + 0]);
                            drow[c2 * 3 + 1] += w * float(tile_delta[idx3 + 1]);
                            drow[c2 * 3 + 2] += w * float(tile_delta[idx3 + 2]);
                        }
                    }
                }
                continue;
            }

            res_r.resize(static_cast<size_t>(Nv));
            res_b.resize(static_cast<size_t>(Nv));
            res_g1.resize(static_cast<size_t>(Nv));
            res_g2.resize(static_cast<size_t>(Nv));
            for (int vi = 0; vi < Nv; ++vi) {
                const auto& rbuf = raw_rv_buf[size_t(vi)];
                const auto& mch = frame_mean_ch[size_t(vi)];
                for (int qy = 0; qy < SUB_N; ++qy) {
                    const double* rr_row = rbuf.data() + size_t((2 * qy + r_dy) * TILE_1X);
                    const double* rb_row = rbuf.data() + size_t((2 * qy + b_dy) * TILE_1X);
                    const double* rg1_row = rbuf.data() + size_t((2 * qy + g1_dy) * TILE_1X);
                    const double* rg2_row = rbuf.data() + size_t((2 * qy + g2_dy) * TILE_1X);
                    for (int qx = 0; qx < SUB_N; ++qx) {
                        const size_t qi = size_t(qy * SUB_N + qx);
                        res_r[size_t(vi)][qi] = rr_row[2 * qx + r_dx] - mch[0];
                        res_b[size_t(vi)][qi] = rb_row[2 * qx + b_dx] - mch[2];
                        res_g1[size_t(vi)][qi] = rg1_row[2 * qx + g1_dx] - mch[1];
                        res_g2[size_t(vi)][qi] = rg2_row[2 * qx + g2_dx] - mch[1];
                    }
                }
            }

            // Separable 2D matched-filter continuous frequency refinement on 16x16 sublattice (step=2 native px)
            auto refine_freq_sub16 = [&](const std::vector<std::array<double, SUB_N * SUB_N>>& sub_res,
                                         double& fx_io, double& fy_io) noexcept {
                constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
                auto eval_power = [&](double fx, double fy) noexcept -> double {
                    std::array<std::complex<double>, SUB_N> ex, ey;
                    for (int q = 0; q < SUB_N; ++q) {
                        const double ax = -kTwoPi * fx * double(2 * q);
                        const double ay = -kTwoPi * fy * double(2 * q);
                        ex[size_t(q)] = {hann16[size_t(q)] * std::cos(ax), hann16[size_t(q)] * std::sin(ax)};
                        ey[size_t(q)] = {hann16[size_t(q)] * std::cos(ay), hann16[size_t(q)] * std::sin(ay)};
                    }
                    double tot = 0.0;
                    for (int vi = 0; vi < Nv; ++vi) {
                        const auto& sr = sub_res[size_t(vi)];
                        std::complex<double> z(0.0, 0.0);
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            std::complex<double> rx(0.0, 0.0);
                            const size_t rbase = size_t(qy * SUB_N);
                            for (int qx = 0; qx < SUB_N; ++qx) rx += sr[rbase + size_t(qx)] * ex[size_t(qx)];
                            z += rx * ey[size_t(qy)];
                        }
                        tot += std::norm(z);
                    }
                    return tot;
                };
                double step = 1.0 / 64.0;
                double p0 = eval_power(fx_io, fy_io);
                for (int z = 0; z < 4; ++z) {
                    const double pxm = eval_power(fx_io - step, fy_io);
                    const double pxp = eval_power(fx_io + step, fy_io);
                    if (pxm > p0 && pxm >= pxp) { fx_io -= step; p0 = pxm; }
                    else if (pxp > p0)          { fx_io += step; p0 = pxp; }
                    else {
                        const double den = std::log(std::max(1e-30, pxm)) - 2.0 * std::log(std::max(1e-30, p0)) + std::log(std::max(1e-30, pxp));
                        if (den < -1e-9) {
                            const double d = std::clamp(0.5 * (std::log(std::max(1e-30, pxm)) - std::log(std::max(1e-30, pxp))) / den, -0.5, 0.5) * step;
                            const double pn = eval_power(fx_io + d, fy_io);
                            if (pn > p0) { fx_io += d; p0 = pn; }
                        }
                    }
                    const double pym = eval_power(fx_io, fy_io - step);
                    const double pyp = eval_power(fx_io, fy_io + step);
                    if (pym > p0 && pym >= pyp) { fy_io -= step; p0 = pym; }
                    else if (pyp > p0)          { fy_io += step; p0 = pyp; }
                    else {
                        const double den = std::log(std::max(1e-30, pym)) - 2.0 * std::log(std::max(1e-30, p0)) + std::log(std::max(1e-30, pyp));
                        if (den < -1e-9) {
                            const double d = std::clamp(0.5 * (std::log(std::max(1e-30, pym)) - std::log(std::max(1e-30, pyp))) / den, -0.5, 0.5) * step;
                            const double pn = eval_power(fx_io, fy_io + d);
                            if (pn > p0) { fy_io += d; p0 = pn; }
                        }
                    }
                    step *= 0.35;
                }
            };

            auto refine_freq_1x32 = [&](double& fx_io, double& fy_io) noexcept {
                constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
                auto eval_power = [&](double fx, double fy) noexcept -> double {
                    std::array<std::complex<double>, TILE_1X> ex, ey;
                    for (int q = 0; q < TILE_1X; ++q) {
                        const double ax = -kTwoPi * fx * double(q);
                        const double ay = -kTwoPi * fy * double(q);
                        ex[size_t(q)] = {hann32[size_t(q)] * std::cos(ax), hann32[size_t(q)] * std::sin(ax)};
                        ey[size_t(q)] = {hann32[size_t(q)] * std::cos(ay), hann32[size_t(q)] * std::sin(ay)};
                    }
                    double tot = 0.0;
                    for (int vi = 0; vi < Nv; ++vi) {
                        const auto& sr = res_wb_1x[size_t(vi)];
                        std::complex<double> z(0.0, 0.0);
                        for (int r = 0; r < TILE_1X; ++r) {
                            std::complex<double> rx(0.0, 0.0);
                            const size_t rbase = size_t(r * TILE_1X);
                            for (int c = 0; c < TILE_1X; ++c) rx += sr[rbase + size_t(c)] * ex[size_t(c)];
                            z += rx * ey[size_t(r)];
                        }
                        tot += std::norm(z);
                    }
                    return tot;
                };
                double step = 1.0 / 64.0;
                double p0 = eval_power(fx_io, fy_io);
                for (int z = 0; z < 4; ++z) {
                    const double pxm = eval_power(fx_io - step, fy_io);
                    const double pxp = eval_power(fx_io + step, fy_io);
                    if (pxm > p0 && pxm >= pxp) { fx_io -= step; p0 = pxm; }
                    else if (pxp > p0)          { fx_io += step; p0 = pxp; }
                    else {
                        const double den = std::log(std::max(1e-30, pxm)) - 2.0 * std::log(std::max(1e-30, p0)) + std::log(std::max(1e-30, pxp));
                        if (den < -1e-9) {
                            const double d = std::clamp(0.5 * (std::log(std::max(1e-30, pxm)) - std::log(std::max(1e-30, pxp))) / den, -0.5, 0.5) * step;
                            const double pn = eval_power(fx_io + d, fy_io);
                            if (pn > p0) { fx_io += d; p0 = pn; }
                        }
                    }
                    const double pym = eval_power(fx_io, fy_io - step);
                    const double pyp = eval_power(fx_io, fy_io + step);
                    if (pym > p0 && pym >= pyp) { fy_io -= step; p0 = pym; }
                    else if (pyp > p0)          { fy_io += step; p0 = pyp; }
                    else {
                        const double den = std::log(std::max(1e-30, pym)) - 2.0 * std::log(std::max(1e-30, p0)) + std::log(std::max(1e-30, pyp));
                        if (den < -1e-9) {
                            const double d = std::clamp(0.5 * (std::log(std::max(1e-30, pym)) - std::log(std::max(1e-30, pyp))) / den, -0.5, 0.5) * step;
                            const double pn = eval_power(fx_io, fy_io + d);
                            if (pn > p0) { fy_io += d; p0 = pn; }
                        }
                    }
                    step *= 0.35;
                }
            };

            auto solve_rb_sublattice = [&](int ch, int off_y, int off_x,
                                           std::vector<std::array<double, SUB_N * SUB_N>>& sub_res) {
                for (int peak_iter = 0; peak_iter < 3; ++peak_iter) {
                    psd16.fill(0.0);
                    double sub_sq_tot = 0.0;
                    for (int vi = 0; vi < Nv; ++vi) {
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            const double wy = hann16[size_t(qy)];
                            for (int qx = 0; qx < SUB_N; ++qx) {
                                const double rv = sub_res[size_t(vi)][size_t(qy * SUB_N + qx)];
                                sub_sq_tot += rv * rv;
                                fft16[size_t(qy * 16 + qx)] = {wy * hann16[size_t(qx)] * rv, 0.0};
                            }
                        }
                        detail::fft2d_fixed<16>(fft16, false);
                        for (size_t k = 0; k < 256u; ++k) psd16[k] += std::norm(fft16[k]);
                    }
                    const double rms_in = std::sqrt(sub_sq_tot / double(Nv * SUB_N * SUB_N));
                    if (rms_in <= 1.12 * sigma_vf[0]) break;

                    std::array<double, 140> ns{};
                    size_t n_ns = 0;
                    double best_p = 0.0, p_k1_max = 0.0;
                    int best_ky = 0, best_kx = 0;
                    for (int ky = 0; ky <= 8; ++ky) {
                        for (int kx = 0; kx < 16; ++kx) {
                            const int sx = (kx <= 8) ? kx : (kx - 16);
                            if (ky == 0 && sx <= 0) continue;
                            if (ky == 8 && sx < 0) continue;
                            const double p = psd16[size_t(ky * 16 + kx)];
                            if (ky <= 1 && std::abs(sx) <= 1) p_k1_max = std::max(p_k1_max, p);
                            if (n_ns < ns.size()) ns[n_ns++] = p;
                            if (p > best_p) { best_p = p; best_ky = ky; best_kx = kx; }
                        }
                    }
                    if (n_ns < 4u) break;
                    std::nth_element(ns.begin(), ns.begin() + ptrdiff_t(n_ns / 2u), ns.begin() + ptrdiff_t(n_ns));
                    const double med_p = ns[n_ns / 2u];
                    const double sig_floor = double(Nv) * 64.0 * sigma_vf[0] * sigma_vf[0];
                    if (best_p < std::max(7.0 * med_p, 12.0 * sig_floor)) break;
                    if (peak_iter == 0) {
                        std::array<uint8_t, 256> in_lobe{};
                        const int cky = (16 - best_ky) & 15, ckx = (16 - best_kx) & 15;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                in_lobe[size_t(((best_ky + dy) & 15) * 16 + ((best_kx + dx) & 15))] = 1u;
                                in_lobe[size_t(((cky + dy) & 15) * 16 + ((ckx + dx) & 15))] = 1u;
                            }
                        }
                        double ac_net = 0.0, lobe_net = 0.0;
                        for (int r = 0; r < 16; ++r) {
                            const int sy = (r <= 8) ? r : (r - 16);
                            for (int c = 0; c < 16; ++c) {
                                const int sx = (c <= 8) ? c : (c - 16);
                                if (std::abs(sy) <= 1 && std::abs(sx) <= 1) continue;
                                const size_t k = size_t(r * 16 + c);
                                const double pn = std::max(0.0, psd16[k] - med_p);
                                ac_net += pn;
                                if (in_lobe[k]) lobe_net += pn;
                            }
                        }
                        if (ac_net > 0.0 && lobe_net < 0.28 * ac_net) break;
                    }

                    const int sx_b = (best_kx <= 8) ? best_kx : (best_kx - 16);
                    double fx_car = double(sx_b) / 32.0;
                    double fy_car = double(best_ky) / 32.0;
                    refine_freq_sub16(sub_res, fx_car, fy_car);
                    if (std::abs(fy_car) < 0.0025) fy_car = 0.0;
                    if (std::abs(fx_car) < 0.0025) fx_car = 0.0;
                    fx_car = std::fmod(std::fmod(fx_car, 0.5) + 0.5, 0.5);
                    fy_car = std::fmod(std::fmod(fy_car, 0.5) + 0.5, 0.5);
                    if (fx_car < 0.0025 || fx_car > 0.4975) fx_car = 0.0;
                    if (fy_car < 0.0025 || fy_car > 0.4975) fy_car = 0.0;
                    if (fx_car == 0.0 && fy_car == 0.0) break;

                    double max_frac_x = 0.0, max_frac_y = 0.0;
                    double max_sub_frac_x = 0.0, max_sub_frac_y = 0.0;
                    for (int vi = 0; vi < Nv; ++vi) {
                        const double dx = shift_ch[ch][size_t(vi)][0];
                        const double dy = shift_ch[ch][size_t(vi)][1];
                        max_frac_x = std::max(max_frac_x, std::abs(dx - std::round(dx)));
                        max_frac_y = std::max(max_frac_y, std::abs(dy - std::round(dy)));
                        max_sub_frac_x = std::max(max_sub_frac_x, std::abs(0.5 * dx - std::round(0.5 * dx)));
                        max_sub_frac_y = std::max(max_sub_frac_y, std::abs(0.5 * dy - std::round(0.5 * dy)));
                    }

                    std::array<double, SUB_N * SUB_N> Cq{}, Sq{}, Wq{};
                    const int nv_use = std::min(Nv, 64);
                    for (int qy = 0; qy < SUB_N; ++qy) {
                        const double y_abs = double(ty0 + 2 * qy + off_y);
                        for (int qx = 0; qx < SUB_N; ++qx) {
                            const double x_abs = double(tx0 + 2 * qx + off_x);
                            const size_t qi = size_t(qy * SUB_N + qx);
                            const double ang = 2.0 * std::numbers::pi_v<double> * (fx_car * x_abs + fy_car * y_abs);
                            Cq[qi] = std::cos(ang);
                            Sq[qi] = std::sin(ang);
                        }
                    }
                    double sub_blk_pow[16]{};
                    for (int vi = 0; vi < nv_use; ++vi) {
                        double bc_b[16]{}, bs_b[16]{};
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            const int by4 = (qy >> 2) * 4;
                            for (int qx = 0; qx < SUB_N; ++qx) {
                                const size_t qi = size_t(qy * SUB_N + qx);
                                const int bi = by4 + (qx >> 2);
                                const double rv = sub_res[size_t(vi)][qi];
                                bc_b[bi] += rv * Cq[qi];
                                bs_b[bi] += rv * Sq[qi];
                            }
                        }
                        for (int b = 0; b < 16; ++b) sub_blk_pow[b] += bc_b[b] * bc_b[b] + bs_b[b] * bs_b[b];
                    }
                    double sub_pmax = 0.0;
                    for (double v : sub_blk_pow) sub_pmax = std::max(sub_pmax, v);
                    const double sub_pnoise = double(nv_use) * 16.0 * sigma_vf[0] * sigma_vf[0];
                    double m_sub_blk[16]{};
                    int n_active_sub = 0;
                    for (int b = 0; b < 16; ++b) {
                        if (sub_blk_pow[b] >= 0.08 * sub_pmax && sub_blk_pow[b] > 2.0 * sub_pnoise) {
                            m_sub_blk[b] = 1.0;
                            ++n_active_sub;
                        }
                    }
                    if (n_active_sub < 2) break;

                    double w_sum = 0.0, m00 = 0.0, m01 = 0.0, m11 = 0.0;
                    for (int qy = 0; qy < SUB_N; ++qy) {
                        const int by4 = (qy >> 2) * 4;
                        for (int qx = 0; qx < SUB_N; ++qx) {
                            const size_t qi = size_t(qy * SUB_N + qx);
                            const double w = hann16[size_t(qy)] * hann16[size_t(qx)] * m_sub_blk[by4 + (qx >> 2)];
                            const double cq = Cq[qi], sq = Sq[qi];
                            Wq[qi] = w;
                            w_sum += w;
                            m00 += w * cq * cq; m01 += w * cq * sq; m11 += w * sq * sq;
                        }
                    }
                    if (w_sum <= 1e-12) break;
                    m00 /= w_sum; m01 /= w_sum; m11 /= w_sum;
                    double hsqrt[2][2]{}, ihsqrt[2][2]{};
                    if (!detail::invert_2x2_spd(m00, m01, m11, hsqrt, ihsqrt)) break;

                    std::array<double, 64> yw0_v{}, yw1_v{};
                    double proj_rss_before = 0.0;
                    for (int vi = 0; vi < nv_use; ++vi) {
                        double bc = 0.0, bs = 0.0;
                        for (size_t qi = 0; qi < SUB_N * SUB_N; ++qi) {
                            const double wr = Wq[qi] * sub_res[size_t(vi)][qi];
                            bc += wr * Cq[qi];
                            bs += wr * Sq[qi];
                        }
                        bc /= w_sum; bs /= w_sum;
                        const double yw0 = ihsqrt[0][0] * bc + ihsqrt[0][1] * bs;
                        const double yw1 = ihsqrt[1][0] * bc + ihsqrt[1][1] * bs;
                        yw0_v[size_t(vi)] = yw0;
                        yw1_v[size_t(vi)] = yw1;
                        proj_rss_before += yw0 * yw0 + yw1 * yw1;
                    }
                    if (proj_rss_before <= 1e-16) break;

                    struct CandFreq { double fx, fy; int kx_half, ky_half; };
                    const double var_proj = (sigma_vf[0] * sigma_vf[0]) / 128.0;
                    auto eval_coset_model = [&](const CandFreq* clist, int cm, double& out_rss, double out_u[8]) noexcept -> bool {
                        if (cm <= 0 || cm > 4) return false;
                        const int D = 2 * cm;
                        double AtA[8][8]{};
                        double Atb[8]{};
                        std::array<std::array<double, 8>, 64> rows0{}, rows1{};
                        for (int vi = 0; vi < nv_use; ++vi) {
                            double* row0 = rows0[size_t(vi)].data();
                            double* row1 = rows1[size_t(vi)].data();
                            const double dx = shift_ch[ch][size_t(vi)][0];
                            const double dy = shift_ch[ch][size_t(vi)][1];
                            for (int m = 0; m < cm; ++m) {
                                const int par = ((clist[m].kx_half * off_x + clist[m].ky_half * off_y) & 1);
                                const double sig = par ? -1.0 : 1.0;
                                const double ph = 2.0 * std::numbers::pi_v<double> * (clist[m].fx * dx + clist[m].fy * dy);
                                const double cs = sig * std::cos(ph), sn = sig * std::sin(ph);
                                row0[2 * m + 0] = hsqrt[0][0] * cs + hsqrt[0][1] * sn;
                                row0[2 * m + 1] = hsqrt[0][0] * (-sn) + hsqrt[0][1] * cs;
                                row1[2 * m + 0] = hsqrt[1][0] * cs + hsqrt[1][1] * sn;
                                row1[2 * m + 1] = hsqrt[1][0] * (-sn) + hsqrt[1][1] * cs;
                            }
                            const double yw0 = yw0_v[size_t(vi)], yw1 = yw1_v[size_t(vi)];
                            for (int i = 0; i < D; ++i) {
                                Atb[i] += row0[i] * yw0 + row1[i] * yw1;
                                for (int j = 0; j < D; ++j) AtA[i][j] += row0[i] * row0[j] + row1[i] * row1[j];
                            }
                        }
                        double evals[8]{}, evecs[8][8]{};
                        detail::jacobi_sym_eig<8>(D, AtA, evals, evecs);
                        const double e_floor = std::max(0.06 * evals[0], 48.0 * var_proj);
                        if (evals[0] <= std::max(1e-6, 48.0 * var_proj) || evals[D - 1] <= e_floor) return false;
                        for (int i = 0; i < 8; ++i) out_u[i] = 0.0;
                        for (int k = 0; k < D; ++k) {
                            double proj = 0.0;
                            for (int i = 0; i < D; ++i) proj += evecs[i][k] * Atb[i];
                            const double step = proj / (evals[k] + 2.0 * var_proj);
                            for (int i = 0; i < D; ++i) out_u[i] += step * evecs[i][k];
                        }
                        double amp_sq = 0.0;
                        for (int i = 0; i < D; ++i) amp_sq += out_u[i] * out_u[i];
                        if (amp_sq <= 1e-10 || std::sqrt(amp_sq) > 2.4 * rms_in) return false;
                        double rss = 0.0;
                        for (int vi = 0; vi < nv_use; ++vi) {
                            double p0 = 0.0, p1 = 0.0;
                            for (int i = 0; i < D; ++i) {
                                p0 += rows0[size_t(vi)][size_t(i)] * out_u[i];
                                p1 += rows1[size_t(vi)][size_t(i)] * out_u[i];
                            }
                            const double e0 = p0 - yw0_v[size_t(vi)], e1 = p1 - yw1_v[size_t(vi)];
                            rss += e0 * e0 + e1 * e1;
                        }
                        out_rss = rss;
                        return true;
                    };

                    const bool pure_x = (fy_car == 0.0);
                    const bool pure_y = (fx_car == 0.0);
                    const int kx_sub = (fx_car <= 0.25) ? 0 : -1;
                    const int ky_sub = (fy_car <= 0.25) ? 0 : -1;
                    const CandFreq c_null{fx_car + 0.5 * double(kx_sub), fy_car + 0.5 * double(ky_sub), kx_sub, ky_sub};
                    double rss_null = proj_rss_before;
                    double u_null[8]{};
                    const bool ok_null = eval_coset_model(&c_null, 1, rss_null, u_null);

                    std::array<CandFreq, 4> cands{};
                    int M = 0;
                    double u_sol[8]{};
                    double best_score = 1e30, best_rss = 1e30;

                    for (int ky = -2; ky <= 1; ++ky) {
                        if (pure_x && ky != 0) continue;
                        const double fy = fy_car + 0.5 * double(ky);
                        if (fy < -1.0 || fy >= 1.0) continue;
                        if (max_frac_y < 0.05 && (fy < -0.5 || fy >= 0.5)) continue;
                        if (max_sub_frac_y < 0.025 && std::abs(fy) > 0.2501) continue;
                        for (int kx = -2; kx <= 1; ++kx) {
                            if (pure_y && kx != 0) continue;
                            const double fx = fx_car + 0.5 * double(kx);
                            if (fx < -1.0 || fx >= 1.0) continue;
                            if (max_frac_x < 0.05 && (fx < -0.5 || fx >= 0.5)) continue;
                            if (max_sub_frac_x < 0.025 && std::abs(fx) > 0.2501) continue;

                            const int kx_1x = (fx < -0.5) ? (kx + 2) : ((fx >= 0.5) ? (kx - 2) : kx);
                            const int ky_1x = (fy < -0.5) ? (ky + 2) : ((fy >= 0.5) ? (ky - 2) : ky);
                            const int tk[3][2] = {{kx_sub, ky_sub}, {kx_1x, ky_1x}, {kx, ky}};
                            std::array<CandFreq, 4> tc{};
                            int tm = 0;
                            for (int s = 0; s < 3; ++s) {
                                const double tfx = fx_car + 0.5 * double(tk[s][0]);
                                const double tfy = fy_car + 0.5 * double(tk[s][1]);
                                bool dup = false;
                                for (int q = 0; q < tm; ++q) {
                                    if ((std::abs(tfx - tc[size_t(q)].fx) < 1e-6 && std::abs(tfy - tc[size_t(q)].fy) < 1e-6) ||
                                        (std::abs(tfx + tc[size_t(q)].fx) < 1e-6 && std::abs(tfy + tc[size_t(q)].fy) < 1e-6)) {
                                        dup = true;
                                        break;
                                    }
                                }
                                if (!dup) tc[size_t(tm++)] = {tfx, tfy, tk[s][0], tk[s][1]};
                            }
                            if (tm < 2) continue;
                            double cand_rss = 1e30, cand_u[8]{};
                            if (eval_coset_model(tc.data(), tm, cand_rss, cand_u)) {
                                const double sc = cand_rss * (1.0 + 0.015 * (fx * fx + fy * fy));
                                if (sc < best_score) {
                                    best_score = sc;
                                    best_rss = cand_rss;
                                    cands = tc;
                                    M = tm;
                                    std::memcpy(u_sol, cand_u, sizeof(u_sol));
                                }
                            }
                        }
                    }

                    if ((pure_x || pure_y) && nv_use >= 8) {
                        std::array<CandFreq, 4> ax_c{};
                        int am = 0;
                        for (int k = -2; k <= 1; ++k) {
                            const double fx = pure_x ? (fx_car + 0.5 * double(k)) : 0.0;
                            const double fy = pure_y ? (fy_car + 0.5 * double(k)) : 0.0;
                            if (fx < -1.0 || fx >= 1.0 || fy < -1.0 || fy >= 1.0) continue;
                            if (pure_x && max_frac_x < 0.05 && (fx < -0.5 || fx >= 0.5)) continue;
                            if (pure_y && max_frac_y < 0.05 && (fy < -0.5 || fy >= 0.5)) continue;
                            if (pure_x && max_sub_frac_x < 0.025 && std::abs(fx) > 0.2501) continue;
                            if (pure_y && max_sub_frac_y < 0.025 && std::abs(fy) > 0.2501) continue;
                            bool dup = false;
                            for (int q = 0; q < am; ++q) {
                                if (std::abs(fx + ax_c[size_t(q)].fx) < 1e-6 && std::abs(fy + ax_c[size_t(q)].fy) < 1e-6) dup = true;
                            }
                            if (!dup && am < 4) ax_c[size_t(am++)] = {fx, fy, pure_x ? k : 0, pure_y ? k : 0};
                        }
                        if (am == 4) {
                            double rss4 = 1e30, u4[8]{};
                            if (eval_coset_model(ax_c.data(), 4, rss4, u4) && rss4 < 0.55 * best_rss) {
                                best_rss = rss4;
                                cands = ax_c;
                                M = 4;
                                std::memcpy(u_sol, u4, sizeof(u_sol));
                            }
                        }
                    }

                    const float* fp0_car = frames_lin[size_t(valid_frames[0])].data();
                    double rc0_mean = 0.0;
                    for (int qy = 0; qy < SUB_N; ++qy) {
                        const float* rch = fp0_car + size_t(ty0 + 2 * qy + off_y) * size_t(W1);
                        for (int qx = 0; qx < SUB_N; ++qx) rc0_mean += double(rch[tx0 + 2 * qx + off_x]);
                    }
                    rc0_mean *= (1.0 / double(SUB_N * SUB_N));
                    double rc0_c = 0.0, rc0_s = 0.0;
                    for (int qy = 0; qy < SUB_N; ++qy) {
                        const float* rch = fp0_car + size_t(ty0 + 2 * qy + off_y) * size_t(W1);
                        for (int qx = 0; qx < SUB_N; ++qx) {
                            const size_t qi = size_t(qy * SUB_N + qx);
                            const double w = Wq[qi];
                            if (w <= 0.0) continue;
                            const double cv = double(rch[tx0 + 2 * qx + off_x]) - rc0_mean;
                            rc0_c += w * cv * Cq[qi];
                            rc0_s += w * cv * Sq[qi];
                        }
                    }
                    const double raw_car_amp = 2.0 * std::hypot(rc0_c, rc0_s) / w_sum;

                    bool accept_model = false;
                    if (M >= 2 && best_rss <= 0.35 * proj_rss_before) {
                        double amp_in_sq = 0.0, amp_out_sq = 0.0;
                        for (int m = 0; m < M; ++m) {
                            const double a2 = u_sol[2 * m + 0] * u_sol[2 * m + 0] + u_sol[2 * m + 1] * u_sol[2 * m + 1];
                            if (std::abs(cands[size_t(m)].fx) > 0.2505 || std::abs(cands[size_t(m)].fy) > 0.2505) {
                                amp_out_sq += a2;
                            } else {
                                amp_in_sq += a2;
                            }
                        }
                        const double amp_tot_sq = amp_in_sq + amp_out_sq;
                        const bool near_dc_fold = (std::hypot(c_null.fx, c_null.fy) < 0.075);
                        const double req_rss_ratio = near_dc_fold ? 0.16 : 0.28;
                        const double req_out_frac = near_dc_fold ? 0.60 : 0.28;
                        if (best_rss < req_rss_ratio * rss_null && amp_tot_sq > 1e-12 &&
                            amp_out_sq >= req_out_frac * amp_tot_sq &&
                            std::sqrt(amp_out_sq) >= std::max(2.4 * sigma_vf[0], 0.35 * raw_car_amp)) {
                            accept_model = true;
                        }
                    }
                    if (!accept_model && ok_null && rss_null < 0.40 * proj_rss_before) {
                        // Narrowband pure-chroma in-band tone attenuated by Green-guided chroma filter (e.g. CA01..08):
                        // strictly rank-2 M=1 (c_null) with exact unit projection gain so all overlapping tiles agree.
                        const double f_null_rad = std::hypot(c_null.fx, c_null.fy);
                        if (f_null_rad >= 0.095 && best_p > 2.0 * p_k1_max && best_p > 7.5 * med_p) {
                            const float* fp0 = frames_lin[size_t(valid_frames[0])].data();
                            double rg_mean = 0.0, rc_mean = 0.0;
                            for (int qy = 0; qy < SUB_N; ++qy) {
                                const float* rg1 = fp0 + size_t(ty0 + 2 * qy + g1_dy) * size_t(W1);
                                const float* rg2 = fp0 + size_t(ty0 + 2 * qy + g2_dy) * size_t(W1);
                                const float* rch = fp0 + size_t(ty0 + 2 * qy + off_y) * size_t(W1);
                                for (int qx = 0; qx < SUB_N; ++qx) {
                                    rg_mean += 0.5 * (double(rg1[tx0 + 2 * qx + g1_dx]) + double(rg2[tx0 + 2 * qx + g2_dx]));
                                    rc_mean += double(rch[tx0 + 2 * qx + off_x]);
                                }
                            }
                            rg_mean *= (1.0 / double(SUB_N * SUB_N));
                            rc_mean *= (1.0 / double(SUB_N * SUB_N));
                            double rg_c = 0.0, rg_s = 0.0, rc_c = 0.0, rc_s = 0.0;
                            for (int qy = 0; qy < SUB_N; ++qy) {
                                const float* rg1 = fp0 + size_t(ty0 + 2 * qy + g1_dy) * size_t(W1);
                                const float* rg2 = fp0 + size_t(ty0 + 2 * qy + g2_dy) * size_t(W1);
                                const float* rch = fp0 + size_t(ty0 + 2 * qy + off_y) * size_t(W1);
                                for (int qx = 0; qx < SUB_N; ++qx) {
                                    const size_t qi = size_t(qy * SUB_N + qx);
                                    const double w = Wq[qi];
                                    if (w <= 0.0) continue;
                                    const double a1 = 2.0 * std::numbers::pi_v<double> * (c_null.fx * double(tx0 + 2 * qx + g1_dx) + c_null.fy * double(ty0 + 2 * qy + g1_dy));
                                    const double a2 = 2.0 * std::numbers::pi_v<double> * (c_null.fx * double(tx0 + 2 * qx + g2_dx) + c_null.fy * double(ty0 + 2 * qy + g2_dy));
                                    const double gv1 = (double(rg1[tx0 + 2 * qx + g1_dx]) - rg_mean) * double(inv_wb_ch[ch]);
                                    const double gv2 = (double(rg2[tx0 + 2 * qx + g2_dx]) - rg_mean) * double(inv_wb_ch[ch]);
                                    rg_c += 0.5 * w * (gv1 * std::cos(a1) + gv2 * std::cos(a2));
                                    rg_s += 0.5 * w * (gv1 * std::sin(a1) + gv2 * std::sin(a2));
                                    const double cv = double(rch[tx0 + 2 * qx + off_x]) - rc_mean;
                                    rc_c += w * cv * Cq[qi];
                                    rc_s += w * cv * Sq[qi];
                                }
                            }
                            rg_c /= w_sum; rg_s /= w_sum;
                            rc_c /= w_sum; rc_s /= w_sum;
                            const double raw_g_amp = 2.0 * std::hypot(rg_c, rg_s);
                            const double raw_c_amp = 2.0 * std::hypot(rc_c, rc_s);
                            const double c_amp = std::hypot(u_null[0], u_null[1]);
                            if (c_amp > 2.2 * sigma_vf[0] && raw_c_amp > 2.8 * sigma_vf[0] &&
                                c_amp >= 0.32 * raw_c_amp && raw_g_amp < 0.28 * raw_c_amp) {
                                cands[0] = c_null;
                                M = 1;
                                std::memcpy(u_sol, u_null, sizeof(u_sol));
                                best_rss = rss_null;
                                accept_model = true;
                            }
                        }
                    }
                    if (!accept_model) {
                        break;
                    }

                    auto eval_blk_env_2x = [](const double g_blk[64], int r2, int c2) noexcept -> double {
                        const int by = std::clamp(r2 >> 3, 0, 7);
                        const int bx = std::clamp(c2 >> 3, 0, 7);
                        double g = g_blk[by * 8 + bx];
                        if (g <= 0.0) return 0.0;
                        const int ly = r2 & 7, lx = c2 & 7;
                        if (ly < 2 && by > 0 && g_blk[(by - 1) * 8 + bx] < g) {
                            const double t = (double(ly) + 0.5) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_blk[(by - 1) * 8 + bx]) + t * g;
                        } else if (ly >= 6 && by < 7 && g_blk[(by + 1) * 8 + bx] < g) {
                            const double t = (7.5 - double(ly)) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_blk[(by + 1) * 8 + bx]) + t * g;
                        }
                        if (lx < 2 && bx > 0 && g_blk[by * 8 + (bx - 1)] < g) {
                            const double t = (double(lx) + 0.5) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_blk[by * 8 + (bx - 1)]) + t * g;
                        } else if (lx >= 6 && bx < 7 && g_blk[by * 8 + (bx + 1)] < g) {
                            const double t = (7.5 - double(lx)) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_blk[by * 8 + (bx + 1)]) + t * g;
                        }
                        return g;
                    };

                    // Fine 8x8 (4x4 1x-px = 8x8 2x-px) per-cell raw RSS reduction gate:
                    // strictly prevents localized grating chips from bleeding onto adjacent edges/flat backgrounds!
                    double rss0_sb[64]{}, rss1_sb[64]{};
                    for (int vi = 0; vi < Nv; ++vi) {
                        const double dx = shift_ch[ch][size_t(vi)][0];
                        const double dy = shift_ch[ch][size_t(vi)][1];
                        double cx16[4][SUB_N], sx16[4][SUB_N], cy16[4][SUB_N], sy16[4][SUB_N];
                        for (int m = 0; m < M; ++m) {
                            for (int q = 0; q < SUB_N; ++q) {
                                const double ax = 2.0 * std::numbers::pi_v<double> * cands[size_t(m)].fx * (double(tx0 + 2 * q + off_x) - dx);
                                const double ay = 2.0 * std::numbers::pi_v<double> * cands[size_t(m)].fy * (double(ty0 + 2 * q + off_y) - dy);
                                cx16[m][q] = std::cos(ax); sx16[m][q] = std::sin(ax);
                                cy16[m][q] = std::cos(ay); sy16[m][q] = std::sin(ay);
                            }
                        }
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            const int by8 = (qy >> 1) * 8;
                            for (int qx = 0; qx < SUB_N; ++qx) {
                                double dv = 0.0;
                                for (int m = 0; m < M; ++m) {
                                    const double c_xy = cx16[m][qx] * cy16[m][qy] - sx16[m][qx] * sy16[m][qy];
                                    const double s_xy = sx16[m][qx] * cy16[m][qy] + cx16[m][qx] * sy16[m][qy];
                                    dv += u_sol[2 * m + 0] * c_xy + u_sol[2 * m + 1] * s_xy;
                                }
                                const double r0 = sub_res[size_t(vi)][size_t(qy * SUB_N + qx)];
                                const double r1 = r0 - dv;
                                const int bi = by8 + (qx >> 1);
                                rss0_sb[bi] += r0 * r0;
                                rss1_sb[bi] += r1 * r1;
                            }
                        }
                    }
                    double max_rss0_sb = 0.0;
                    for (double v : rss0_sb) max_rss0_sb = std::max(max_rss0_sb, v);
                    double g_sb[64]{};
                    double max_g_sb = 0.0;
                    const double min_drop_sb = 0.4 * double(Nv) * 4.0 * sigma_vf[0] * sigma_vf[0];
                    for (int b = 0; b < 64; ++b) {
                        if (rss0_sb[b] > 1e-20 && rss0_sb[b] >= 0.04 * max_rss0_sb &&
                            rss1_sb[b] < 0.92 * rss0_sb[b] && (rss0_sb[b] - rss1_sb[b]) > min_drop_sb) {
                            g_sb[b] = std::clamp((0.92 - rss1_sb[b] / rss0_sb[b]) / 0.16, 0.0, 1.0);
                            max_g_sb = std::max(max_g_sb, g_sb[b]);
                        }
                    }
                    if (max_g_sb <= 0.0) break;

                    // Separable 2x synthesis (zero sin/cos inside 64x64 loop!)
                    double cx64[4][TILE_2X], sx64[4][TILE_2X], cy64[4][TILE_2X], sy64[4][TILE_2X];
                    for (int m = 0; m < M; ++m) {
                        for (int i = 0; i < TILE_2X; ++i) {
                            const double ax = 2.0 * std::numbers::pi_v<double> * cands[size_t(m)].fx * (double(tx0) + 0.5 * double(i));
                            const double ay = 2.0 * std::numbers::pi_v<double> * cands[size_t(m)].fy * (double(ty0) + 0.5 * double(i));
                            cx64[m][i] = std::cos(ax); sx64[m][i] = std::sin(ax);
                            cy64[m][i] = std::cos(ay); sy64[m][i] = std::sin(ay);
                        }
                    }
                    for (int r2 = 0; r2 < TILE_2X; ++r2) {
                        for (int c2 = 0; c2 < TILE_2X; ++c2) {
                            const double gb = eval_blk_env_2x(g_sb, r2, c2);
                            if (gb <= 0.0) continue;
                            double dv = 0.0;
                            for (int m = 0; m < M; ++m) {
                                const double c_xy = cx64[m][c2] * cy64[m][r2] - sx64[m][c2] * sy64[m][r2];
                                const double s_xy = sx64[m][c2] * cy64[m][r2] + cx64[m][c2] * sy64[m][r2];
                                dv += u_sol[2 * m + 0] * c_xy + u_sol[2 * m + 1] * s_xy;
                            }
                            tile_delta[(size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u + size_t(ch)] += gb * dv;
                        }
                    }
                    for (int vi = 0; vi < Nv; ++vi) {
                        const double dx = shift_ch[ch][size_t(vi)][0];
                        const double dy = shift_ch[ch][size_t(vi)][1];
                        double cx16[4][SUB_N], sx16[4][SUB_N], cy16[4][SUB_N], sy16[4][SUB_N];
                        for (int m = 0; m < M; ++m) {
                            for (int q = 0; q < SUB_N; ++q) {
                                const double ax = 2.0 * std::numbers::pi_v<double> * cands[size_t(m)].fx * (double(tx0 + 2 * q + off_x) - dx);
                                const double ay = 2.0 * std::numbers::pi_v<double> * cands[size_t(m)].fy * (double(ty0 + 2 * q + off_y) - dy);
                                cx16[m][q] = std::cos(ax); sx16[m][q] = std::sin(ax);
                                cy16[m][q] = std::cos(ay); sy16[m][q] = std::sin(ay);
                            }
                        }
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            const int by8 = (qy >> 1) * 8;
                            for (int qx = 0; qx < SUB_N; ++qx) {
                                const double gb = g_sb[by8 + (qx >> 1)];
                                if (gb <= 0.0) continue;
                                double dv = 0.0;
                                for (int m = 0; m < M; ++m) {
                                    const double c_xy = cx16[m][qx] * cy16[m][qy] - sx16[m][qx] * sy16[m][qy];
                                    const double s_xy = sx16[m][qx] * cy16[m][qy] + cx16[m][qx] * sy16[m][qy];
                                    dv += u_sol[2 * m + 0] * c_xy + u_sol[2 * m + 1] * s_xy;
                                }
                                sub_res[size_t(vi)][size_t(qy * SUB_N + qx)] -= gb * dv;
                            }
                        }
                    }
                    tile_modified = true;
                }
            };

            auto solve_1x_brillouin_luma = [&]() {
                if (!need_luma_fft) return;
                double solved_fx[3]{}, solved_fy[3]{};
                int n_solved_peaks = 0;
                for (int peak_iter = 0; peak_iter < 3; ++peak_iter) {
                    psd32.fill(0.0);
                    double wb_sq_tot = 0.0;
                    for (int vi = 0; vi < Nv; ++vi) {
                        for (int r = 0; r < TILE_1X; ++r) {
                            const double wy = hann32[size_t(r)];
                            for (int c = 0; c < TILE_1X; ++c) {
                                const double rv = res_wb_1x[size_t(vi)][size_t(r * TILE_1X + c)];
                                wb_sq_tot += rv * rv;
                                fft32[size_t(r * 32 + c)] = {wy * hann32[size_t(c)] * rv, 0.0};
                            }
                        }
                        detail::fft2d_fixed<32>(fft32, false);
                        for (size_t k = 0; k < 1024u; ++k) psd32[k] += std::norm(fft32[k]);
                    }
                    const double rms_wb = std::sqrt(wb_sq_tot / double(Nv * TILE_1X * TILE_1X));
                    if (rms_wb <= 1.15 * sig_wb) break;

                    std::array<double, 560> ns{};
                    size_t n_ns = 0;
                    double best_p = 0.0, p_k1_max = 0.0;
                    int best_ky = 0, best_kx = 0;
                    for (int ky = 0; ky <= 16; ++ky) {
                        for (int kx = 0; kx < 32; ++kx) {
                            const int sx = (kx <= 16) ? kx : (kx - 32);
                            if (ky == 0 && sx <= 0) continue;
                            if (ky == 16 && sx < 0) continue;
                            const double p = psd32[size_t(ky * 32 + kx)];
                            if (ky <= 1 && std::abs(sx) <= 1) {
                                p_k1_max = std::max(p_k1_max, p);
                                continue;
                            }
                            if (n_ns < ns.size()) ns[n_ns++] = p;
                            if (std::hypot(double(sx) / 32.0, double(ky) / 32.0) >= 0.16 && p > best_p) {
                                best_p = p; best_ky = ky; best_kx = kx;
                            }
                        }
                    }
                    if (n_ns < 8u || best_p <= 0.0) break;
                    std::nth_element(ns.begin(), ns.begin() + ptrdiff_t(n_ns / 2u), ns.begin() + ptrdiff_t(n_ns));
                    const double med_p = ns[n_ns / 2u];
                    const double sig_floor = double(Nv) * 256.0 * sig_wb * sig_wb;
                    if (best_p < std::max(11.0 * med_p, 20.0 * sig_floor)) break;
                    bool curved_arc_only = false;
                    double ac32_net_ref = 0.0;
                    if (peak_iter == 0) {
                        std::array<uint8_t, 1024> in_lobe32{};
                        const int cky = (32 - best_ky) & 31, ckx = (32 - best_kx) & 31;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                in_lobe32[size_t(((best_ky + dy) & 31) * 32 + ((best_kx + dx) & 31))] = 1u;
                                in_lobe32[size_t(((cky + dy) & 31) * 32 + ((ckx + dx) & 31))] = 1u;
                            }
                        }
                        double ac32_net = 0.0, lobe32_net = 0.0;
                        double p2_best = 0.0;
                        int ky2_b = 0, kx2_b = 0;
                        for (int r = 0; r < 32; ++r) {
                            const int sy = (r <= 16) ? r : (r - 32);
                            for (int c = 0; c < 32; ++c) {
                                const int sx = (c <= 16) ? c : (c - 32);
                                if (std::abs(sy) <= 1 && std::abs(sx) <= 1) continue;
                                const size_t k = size_t(r * 32 + c);
                                const double pn = std::max(0.0, psd32[k] - med_p);
                                ac32_net += pn;
                                if (in_lobe32[k]) {
                                    lobe32_net += pn;
                                } else if (r <= 16 && !(r == 0 && sx <= 0) && !(r == 16 && sx < 0) &&
                                           std::hypot(double(sx) / 32.0, double(r) / 32.0) >= 0.16 && psd32[k] > p2_best) {
                                    p2_best = psd32[k]; ky2_b = r; kx2_b = c;
                                }
                            }
                        }
                        double lobe2_net = 0.0;
                        if (p2_best > std::max(8.0 * med_p, 12.0 * sig_floor)) {
                            std::array<uint8_t, 1024> in_lobe2{};
                            const int cky2 = (32 - ky2_b) & 31, ckx2 = (32 - kx2_b) & 31;
                            for (int dy = -1; dy <= 1; ++dy) {
                                for (int dx = -1; dx <= 1; ++dx) {
                                    const size_t i1 = size_t(((ky2_b + dy) & 31) * 32 + ((kx2_b + dx) & 31));
                                    const size_t i2 = size_t(((cky2 + dy) & 31) * 32 + ((ckx2 + dx) & 31));
                                    if (!in_lobe32[i1]) in_lobe2[i1] = 1u;
                                    if (!in_lobe32[i2]) in_lobe2[i2] = 1u;
                                }
                            }
                            for (size_t k = 0; k < 1024u; ++k) {
                                if (in_lobe2[k]) lobe2_net += std::max(0.0, psd32[k] - med_p);
                            }
                        }
                        ac32_net_ref = ac32_net;
                        const bool one_lobe_ok = (lobe32_net >= 0.36 * ac32_net);
                        const bool two_lobe_ok = (lobe32_net >= 0.22 * ac32_net && (lobe32_net + lobe2_net) >= 0.52 * ac32_net);
                        const bool arc_cand_ok = (best_p > 2.8 * p_k1_max && best_p > 14.0 * med_p);
                        if (ac32_net > 0.0 && !one_lobe_ok && !two_lobe_ok) {
                            if (!arc_cand_ok) break;
                            curved_arc_only = true;
                        }
                    }

                    const int sx_b = (best_kx <= 16) ? best_kx : (best_kx - 32);
                    double fx0 = double(sx_b) / 32.0;
                    double fy0 = double(best_ky) / 32.0;
                    refine_freq_1x32(fx0, fy0);
                    if (std::abs(fy0) < 0.0025) fy0 = 0.0;
                    if (std::abs(fx0) < 0.0025) fx0 = 0.0;
                    fx0 = std::fmod(std::fmod(fx0, 1.0) + 1.0, 1.0);
                    fy0 = std::fmod(std::fmod(fy0, 1.0) + 1.0, 1.0);
                    if (fx0 < 0.0025 || fx0 > 0.9975) fx0 = 0.0;
                    if (fy0 < 0.0025 || fy0 > 0.9975) fy0 = 0.0;
                    if (fx0 == 0.0 && fy0 == 0.0) break;
                    bool dup_carrier = false;
                    for (int sp = 0; sp < n_solved_peaks; ++sp) {
                        auto wrap_d = [](double d) { return d - std::round(d); };
                        const double d1 = std::hypot(wrap_d(fx0 - solved_fx[sp]), wrap_d(fy0 - solved_fy[sp]));
                        const double d2 = std::hypot(wrap_d(fx0 + solved_fx[sp]), wrap_d(fy0 + solved_fy[sp]));
                        if (std::min(d1, d2) < 0.035) { dup_carrier = true; break; }
                    }
                    if (dup_carrier) break;

                    // Wavefront-Curvature Chirplet Focusing:
                    // Estimate signed transverse quadratic chirp alpha_chirp = +-f_true / R along wavefront tangent
                    // (xc_abs, yc_abs = tile center). Planar gratings keep exact alpha_chirp == 0.0 unless curvature
                    // increases coherent matched power by >12%.
                    const double xc_abs = double(tx0) + 15.5;
                    const double yc_abs = double(ty0) + 15.5;
                    const double ux0 = (fx0 > 0.5) ? (fx0 - 1.0) : fx0;
                    const double uy0 = (fy0 > 0.5) ? (fy0 - 1.0) : fy0;
                    const double fn0 = std::hypot(ux0, uy0);
                    const double ch_tx = (fn0 > 1e-9) ? (-uy0 / fn0) : 0.0;
                    const double ch_ty = (fn0 > 1e-9) ? ( ux0 / fn0) : 1.0;
                    double alpha_chirp = 0.0;

                    auto eval_chirp_pow = [&](double fx_t, double fy_t, double al_t) noexcept -> double {
                        std::array<double, TILE_1X * TILE_1X> base_ph, tau0_arr, hw32;
                        for (int r = 0; r < TILE_1X; ++r) {
                            const double y_a = double(ty0 + r);
                            const double wy = hann32[size_t(r)];
                            for (int c = 0; c < TILE_1X; ++c) {
                                const double x_a = double(tx0 + c);
                                const size_t idx = size_t(r * TILE_1X + c);
                                base_ph[idx] = 2.0 * std::numbers::pi_v<double> * (fx_t * x_a + fy_t * y_a);
                                tau0_arr[idx] = ch_tx * (x_a - xc_abs) + ch_ty * (y_a - yc_abs);
                                hw32[idx] = wy * hann32[size_t(c)];
                            }
                        }
                        double tot = 0.0;
                        const int nv_p = std::min(Nv, 16);
                        for (int vi = 0; vi < nv_p; ++vi) {
                            const double dtau = ch_tx * shift_ch[1][size_t(vi)][0] + ch_ty * shift_ch[1][size_t(vi)][1];
                            const auto& sr = res_wb_1x[size_t(vi)];
                            double zr = 0.0, zi = 0.0;
                            for (size_t idx = 0; idx < TILE_1X * TILE_1X; ++idx) {
                                const double t = tau0_arr[idx] - dtau;
                                const double ang = base_ph[idx] + std::numbers::pi_v<double> * al_t * t * t;
                                const double wv = hw32[idx] * sr[idx];
                                zr += wv * std::cos(ang);
                                zi -= wv * std::sin(ang);
                            }
                            tot += zr * zr + zi * zi;
                        }
                        return tot * (double(Nv) / double(std::max(1, nv_p)));
                    };

                    const double p_al0 = eval_chirp_pow(fx0, fy0, 0.0);
                    double best_al_p = p_al0;
                    int best_ai = 4;
                    double al_pows[9]{};
                    al_pows[4] = p_al0;
                    for (int ai = 0; ai < 9; ++ai) {
                        if (ai == 4) continue;
                        const double al = double(ai - 4) * 0.0025;
                        al_pows[ai] = eval_chirp_pow(fx0, fy0, al);
                        if (al_pows[ai] > best_al_p) {
                            best_al_p = al_pows[ai];
                            best_ai = ai;
                        }
                    }
                    if (best_ai != 4 && best_al_p > 1.12 * p_al0) {
                        alpha_chirp = double(best_ai - 4) * 0.0025;
                        if (best_ai > 0 && best_ai < 8) {
                            const double pm = al_pows[best_ai - 1], p0c = al_pows[best_ai], pp = al_pows[best_ai + 1];
                            const double den = pm - 2.0 * p0c + pp;
                            if (den < -1e-12) {
                                const double d_al = std::clamp(0.5 * (pm - pp) / den, -0.5, 0.5) * 0.0025;
                                const double pn = eval_chirp_pow(fx0, fy0, alpha_chirp + d_al);
                                if (pn > best_al_p) { alpha_chirp += d_al; best_al_p = pn; }
                            }
                        }
                        // Re-center (fx0, fy0) at the focused chirp rate!
                        double fstep = 1.0 / 128.0;
                        for (int z = 0; z < 2; ++z) {
                            const double pxm = eval_chirp_pow(fx0 - fstep, fy0, alpha_chirp);
                            const double pxp = eval_chirp_pow(fx0 + fstep, fy0, alpha_chirp);
                            if (pxm > best_al_p && pxm >= pxp) { fx0 -= fstep; best_al_p = pxm; }
                            else if (pxp > best_al_p)          { fx0 += fstep; best_al_p = pxp; }
                            const double pym = eval_chirp_pow(fx0, fy0 - fstep, alpha_chirp);
                            const double pyp = eval_chirp_pow(fx0, fy0 + fstep, alpha_chirp);
                            if (pym > best_al_p && pym >= pyp) { fy0 -= fstep; best_al_p = pym; }
                            else if (pyp > best_al_p)          { fy0 += fstep; best_al_p = pyp; }
                            fstep *= 0.4;
                        }
                        fx0 = std::fmod(std::fmod(fx0, 1.0) + 1.0, 1.0);
                        fy0 = std::fmod(std::fmod(fy0, 1.0) + 1.0, 1.0);
                        if (fx0 < 0.0025 || fx0 > 0.9975) fx0 = 0.0;
                        if (fy0 < 0.0025 || fy0 > 0.9975) fy0 = 0.0;
                        if (fx0 == 0.0 && fy0 == 0.0) break;
                    }
                    if (curved_arc_only && (alpha_chirp == 0.0 || best_al_p < std::max(24.0 * sig_floor, 0.14 * ac32_net_ref))) {
                        break;
                    }

                    // Verify BOTH residual Green AND actual raw Green sensor carry energy at (fx0, fy0, alpha_chirp)
                    // (strictly prevents pure R/B CFA tones like CA01..08 or C01..04 from entering luma solver!)
                    double g_pow = 0.0, raw_g_pow = 0.0;
                    {
                        double gw_s = 0.0;
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            for (int qx = 0; qx < SUB_N; ++qx) gw_s += 2.0 * hann16[size_t(qy)] * hann16[size_t(qx)];
                        }
                        for (int vi = 0; vi < Nv; ++vi) {
                            const float* fpv = frames_lin[size_t(valid_frames[size_t(vi)])].data();
                            double rgv_mean = 0.0;
                            for (int qy = 0; qy < SUB_N; ++qy) {
                                const float* rg1 = fpv + size_t(ty0 + 2 * qy + g1_dy) * size_t(W1);
                                const float* rg2 = fpv + size_t(ty0 + 2 * qy + g2_dy) * size_t(W1);
                                for (int qx = 0; qx < SUB_N; ++qx) {
                                    rgv_mean += 0.5 * (double(rg1[tx0 + 2 * qx + g1_dx]) + double(rg2[tx0 + 2 * qx + g2_dx]));
                                }
                            }
                            rgv_mean *= (1.0 / double(SUB_N * SUB_N));
                            const double dtau = ch_tx * shift_ch[1][size_t(vi)][0] + ch_ty * shift_ch[1][size_t(vi)][1];
                            double gpr = 0.0, gpi = 0.0;
                            double rgr = 0.0, rgi = 0.0;
                            for (int qy = 0; qy < SUB_N; ++qy) {
                                const size_t rb = size_t(qy * SUB_N);
                                const double wy = hann16[size_t(qy)];
                                const double y1 = double(ty0 + 2 * qy + g1_dy);
                                const double y2 = double(ty0 + 2 * qy + g2_dy);
                                const float* rg1 = fpv + size_t(ty0 + 2 * qy + g1_dy) * size_t(W1);
                                const float* rg2 = fpv + size_t(ty0 + 2 * qy + g2_dy) * size_t(W1);
                                for (int qx = 0; qx < SUB_N; ++qx) {
                                    const double w = wy * hann16[size_t(qx)];
                                    const double x1 = double(tx0 + 2 * qx + g1_dx);
                                    const double x2 = double(tx0 + 2 * qx + g2_dx);
                                    const double t1 = ch_tx * (x1 - xc_abs) + ch_ty * (y1 - yc_abs) - dtau;
                                    const double t2 = ch_tx * (x2 - xc_abs) + ch_ty * (y2 - yc_abs) - dtau;
                                    const double a1 = 2.0 * std::numbers::pi_v<double> * (fx0 * x1 + fy0 * y1) + std::numbers::pi_v<double> * alpha_chirp * t1 * t1;
                                    const double a2 = 2.0 * std::numbers::pi_v<double> * (fx0 * x2 + fy0 * y2) + std::numbers::pi_v<double> * alpha_chirp * t2 * t2;
                                    const double c1 = std::cos(a1), s1 = std::sin(a1);
                                    const double c2 = std::cos(a2), s2 = std::sin(a2);
                                    const double v1 = w * res_g1[size_t(vi)][rb + size_t(qx)];
                                    const double v2 = w * res_g2[size_t(vi)][rb + size_t(qx)];
                                    gpr += v1 * c1 + v2 * c2;
                                    gpi -= v1 * s1 + v2 * s2;
                                    const double rv1 = w * (double(rg1[tx0 + 2 * qx + g1_dx]) - rgv_mean);
                                    const double rv2 = w * (double(rg2[tx0 + 2 * qx + g2_dx]) - rgv_mean);
                                    rgr += rv1 * c1 + rv2 * c2;
                                    rgi -= rv1 * s1 + rv2 * s2;
                                }
                            }
                            const double inv_gw = 1.0 / std::max(1e-12, gw_s);
                            g_pow += (gpr * gpr + gpi * gpi) * inv_gw * inv_gw;
                            raw_g_pow += (rgr * rgr + rgi * rgi) * inv_gw * inv_gw;
                        }
                    }
                    const double raw_g_mean_pow = raw_g_pow / double(std::max(1, Nv));
                    const double raw_g_amp = 2.0 * std::sqrt(std::max(0.0, raw_g_mean_pow));
                    if (g_pow < std::max(double(Nv) * 18.0 * (sig2_base / 256.0), 0.12 * rms_wb * rms_wb)) break;
                    if (raw_g_mean_pow < 12.0 * (sig2_base / 256.0)) break;

                    double max_frac_x = 0.0, max_frac_y = 0.0;
                    for (int vi = 0; vi < Nv; ++vi) {
                        const double dx = shift_ch[1][size_t(vi)][0];
                        const double dy = shift_ch[1][size_t(vi)][1];
                        max_frac_x = std::max(max_frac_x, std::abs(dx - std::round(dx)));
                        max_frac_y = std::max(max_frac_y, std::abs(dy - std::round(dy)));
                    }
                    if (max_frac_x < 0.005 && max_frac_y < 0.005) break;

                    std::array<std::array<double, 2>, 4> b_cands{};
                    int M = 0;
                    const int ax_vals[2] = {-1, 0};
                    const int ay_vals[2] = {-1, 0};
                    for (int iy = (fy0 == 0.0 ? 1 : 0); iy < 2; ++iy) {
                        const double fy = fy0 + double(ay_vals[iy]);
                        if (fy < -1.0 || fy >= 1.0) continue;
                        if (max_frac_y < 0.005 && (fy < -0.5 || fy >= 0.5)) continue;
                        for (int ix = (fx0 == 0.0 ? 1 : 0); ix < 2; ++ix) {
                            const double fx = fx0 + double(ax_vals[ix]);
                            if (fx < -1.0 || fx >= 1.0) continue;
                            if (max_frac_x < 0.005 && (fx < -0.5 || fx >= 0.5)) continue;
                            if (M < 4) b_cands[size_t(M++)] = {fx, fy};
                        }
                    }
                    if (M < 2) break;

                    std::array<double, TILE_1X * TILE_1X> C1x{}, S1x{}, W1x{}, tau0_1x{}, base_ang_1x{};
                    const int nv_use = std::min(Nv, 64);
                    for (int r = 0; r < TILE_1X; ++r) {
                        const double y_abs = double(ty0 + r);
                        for (int c = 0; c < TILE_1X; ++c) {
                            const double x_abs = double(tx0 + c);
                            const size_t idx = size_t(r * TILE_1X + c);
                            const double tau0 = ch_tx * (x_abs - xc_abs) + ch_ty * (y_abs - yc_abs);
                            const double b_ang = 2.0 * std::numbers::pi_v<double> * (fx0 * x_abs + fy0 * y_abs);
                            const double ang = b_ang + std::numbers::pi_v<double> * alpha_chirp * tau0 * tau0;
                            tau0_1x[idx] = tau0;
                            base_ang_1x[idx] = b_ang;
                            C1x[idx] = std::cos(ang);
                            S1x[idx] = std::sin(ang);
                        }
                    }
                    double blk_pow1x[16]{};
                    for (int vi = 0; vi < nv_use; ++vi) {
                        const double dtau = ch_tx * shift_ch[1][size_t(vi)][0] + ch_ty * shift_ch[1][size_t(vi)][1];
                        double bc_b[16]{}, bs_b[16]{};
                        for (int r = 0; r < TILE_1X; ++r) {
                            const int by4 = (r >> 3) * 4;
                            for (int c = 0; c < TILE_1X; ++c) {
                                const size_t idx = size_t(r * TILE_1X + c);
                                const int bi = by4 + (c >> 3);
                                const double rv = res_wb_1x[size_t(vi)][idx];
                                if (alpha_chirp == 0.0) {
                                    bc_b[bi] += rv * C1x[idx];
                                    bs_b[bi] += rv * S1x[idx];
                                } else {
                                    const double t = tau0_1x[idx] - dtau;
                                    const double ang = base_ang_1x[idx] + std::numbers::pi_v<double> * alpha_chirp * t * t;
                                    bc_b[bi] += rv * std::cos(ang);
                                    bs_b[bi] += rv * std::sin(ang);
                                }
                            }
                        }
                        for (int b = 0; b < 16; ++b) blk_pow1x[b] += bc_b[b] * bc_b[b] + bs_b[b] * bs_b[b];
                    }
                    double pmax_1x = 0.0;
                    for (double v : blk_pow1x) pmax_1x = std::max(pmax_1x, v);
                    const double pnoise_1x = double(nv_use) * 64.0 * sig_wb * sig_wb;
                    double m_blk1x[16]{};
                    int n_active_1x = 0;
                    for (int b = 0; b < 16; ++b) {
                        if (blk_pow1x[b] >= 0.12 * pmax_1x && blk_pow1x[b] > 2.5 * pnoise_1x) {
                            m_blk1x[b] = 1.0;
                            ++n_active_1x;
                        }
                    }
                    if (n_active_1x < 2) break;

                    const bool use_green_only_1x = !(std::abs(fx0 - 0.5) < 0.03 && std::abs(fy0 - 0.5) < 0.03);
                    double w_s1 = 0.0, m00 = 0.0, m01 = 0.0, m11 = 0.0;
                    for (int r = 0; r < TILE_1X; ++r) {
                        const int by4 = (r >> 3) * 4;
                        const uint8_t* rcfa = cfa6x6[(ty0 + r) & 1];
                        for (int c = 0; c < TILE_1X; ++c) {
                            const size_t idx = size_t(r * TILE_1X + c);
                            const double cfa_m = (!use_green_only_1x || rcfa[(tx0 + c) & 1] == 1) ? 1.0 : 0.0;
                            const double w = cfa_m * hann32[size_t(r)] * hann32[size_t(c)] * m_blk1x[by4 + (c >> 3)];
                            const double cq = C1x[idx], sq = S1x[idx];
                            W1x[idx] = w;
                            w_s1 += w;
                            m00 += w * cq * cq; m01 += w * cq * sq; m11 += w * sq * sq;
                        }
                    }
                    if (w_s1 <= 1e-12) break;
                    m00 /= w_s1; m01 /= w_s1; m11 /= w_s1;
                    double hsqrt[2][2]{}, ihsqrt[2][2]{};
                    if (!detail::invert_2x2_spd(m00, m01, m11, hsqrt, ihsqrt)) break;

                    // Direct Raw Sensor Carrier Observations (zero Stage-4A kernel/bilinear bias):
                    // High-pass raw Green (sigma=3.5 px normalized CFA Gaussian on 52x52 window) when use_green_only_1x,
                    // or res_wb_1x at exact (0.5, 0.5) checkerboard corner.
                    constexpr int LP_R = 10;
                    constexpr int LP_S = TILE_1X + 2 * LP_R; // 52
                    double gk10[2 * LP_R + 1]{};
                    for (int d = -LP_R; d <= LP_R; ++d) {
                        gk10[d + LP_R] = std::exp(-0.5 * double(d * d) / (3.5 * 3.5));
                    }

                    std::array<double, 64> yw0_raw{}, yw1_raw{};
                    double proj_rss_raw = 0.0;
                    double raw_hp_sq = 0.0;
                    for (int vi = 0; vi < nv_use; ++vi) {
                        std::array<double, TILE_1X * TILE_1X> hp_buf{};
                        if (use_green_only_1x) {
                            const float* fpv = frames_lin[size_t(valid_frames[size_t(vi)])].data();
                            std::array<double, LP_S * LP_S> num_p{}, den_p{}, tmp_n{}, tmp_d{};
                            for (int pr = 0; pr < LP_S; ++pr) {
                                const int rr = std::clamp(ty0 - LP_R + pr, 0, H1 - 1);
                                const uint8_t* rcfa = cfa6x6[rr & 1];
                                const float* frow = fpv + size_t(rr) * size_t(W1);
                                for (int pc = 0; pc < LP_S; ++pc) {
                                    const int cc = std::clamp(tx0 - LP_R + pc, 0, W1 - 1);
                                    if (rcfa[cc & 1] == 1) {
                                        num_p[size_t(pr * LP_S + pc)] = double(frow[cc]);
                                        den_p[size_t(pr * LP_S + pc)] = 1.0;
                                    }
                                }
                            }
                            for (int pr = 0; pr < LP_S; ++pr) {
                                for (int pc = 0; pc < TILE_1X; ++pc) {
                                    const int c0 = pc + LP_R;
                                    double sn = 0.0, sd = 0.0;
                                    for (int d = -LP_R; d <= LP_R; ++d) {
                                        const double kw = gk10[d + LP_R];
                                        sn += kw * num_p[size_t(pr * LP_S + c0 + d)];
                                        sd += kw * den_p[size_t(pr * LP_S + c0 + d)];
                                    }
                                    tmp_n[size_t(pr * TILE_1X + pc)] = sn;
                                    tmp_d[size_t(pr * TILE_1X + pc)] = sd;
                                }
                            }
                            for (int r = 0; r < TILE_1X; ++r) {
                                const int r0 = r + LP_R;
                                const int rr = ty0 + r;
                                const uint8_t* rcfa = cfa6x6[rr & 1];
                                const float* frow = fpv + size_t(rr) * size_t(W1);
                                for (int c = 0; c < TILE_1X; ++c) {
                                    if (rcfa[(tx0 + c) & 1] != 1) continue;
                                    double sn = 0.0, sd = 0.0;
                                    for (int d = -LP_R; d <= LP_R; ++d) {
                                        const double kw = gk10[d + LP_R];
                                        sn += kw * tmp_n[size_t((r0 + d) * TILE_1X + c)];
                                        sd += kw * tmp_d[size_t((r0 + d) * TILE_1X + c)];
                                    }
                                    const double lp = (sd > 1e-12) ? (sn / sd) : 0.0;
                                    const double hv = double(frow[tx0 + c]) - lp;
                                    hp_buf[size_t(r * TILE_1X + c)] = hv;
                                    raw_hp_sq += hv * hv;
                                }
                            }
                        } else {
                            hp_buf = res_wb_1x[size_t(vi)];
                            for (double v : hp_buf) raw_hp_sq += v * v;
                        }

                        double bc = 0.0, bs = 0.0;
                        if (alpha_chirp == 0.0) {
                            for (size_t idx = 0; idx < TILE_1X * TILE_1X; ++idx) {
                                const double wr = W1x[idx] * hp_buf[idx];
                                bc += wr * C1x[idx];
                                bs += wr * S1x[idx];
                            }
                            bc /= w_s1; bs /= w_s1;
                            yw0_raw[size_t(vi)] = ihsqrt[0][0] * bc + ihsqrt[0][1] * bs;
                            yw1_raw[size_t(vi)] = ihsqrt[1][0] * bc + ihsqrt[1][1] * bs;
                        } else {
                            const double dtau = ch_tx * shift_ch[1][size_t(vi)][0] + ch_ty * shift_ch[1][size_t(vi)][1];
                            double mv00 = 0.0, mv01 = 0.0, mv11 = 0.0;
                            for (size_t idx = 0; idx < TILE_1X * TILE_1X; ++idx) {
                                const double w = W1x[idx];
                                if (w <= 0.0) continue;
                                const double t = tau0_1x[idx] - dtau;
                                const double ang = base_ang_1x[idx] + std::numbers::pi_v<double> * alpha_chirp * t * t;
                                const double cq = std::cos(ang), sq = std::sin(ang);
                                const double wr = w * hp_buf[idx];
                                bc += wr * cq; bs += wr * sq;
                                mv00 += w * cq * cq; mv01 += w * cq * sq; mv11 += w * sq * sq;
                            }
                            bc /= w_s1; bs /= w_s1;
                            mv00 /= w_s1; mv01 /= w_s1; mv11 /= w_s1;
                            double hs_v[2][2]{}, ihs_v[2][2]{};
                            if (detail::invert_2x2_spd(mv00, mv01, mv11, hs_v, ihs_v)) {
                                yw0_raw[size_t(vi)] = ihs_v[0][0] * bc + ihs_v[0][1] * bs;
                                yw1_raw[size_t(vi)] = ihs_v[1][0] * bc + ihs_v[1][1] * bs;
                            } else {
                                yw0_raw[size_t(vi)] = ihsqrt[0][0] * bc + ihsqrt[0][1] * bs;
                                yw1_raw[size_t(vi)] = ihsqrt[1][0] * bc + ihsqrt[1][1] * bs;
                            }
                        }
                        proj_rss_raw += yw0_raw[size_t(vi)] * yw0_raw[size_t(vi)] + yw1_raw[size_t(vi)] * yw1_raw[size_t(vi)];
                    }
                    if (proj_rss_raw <= 1e-16) break;
                    const double rms_hp_raw = std::sqrt(raw_hp_sq / double(nv_use * (use_green_only_1x ? (TILE_1X * TILE_1X / 2) : (TILE_1X * TILE_1X))));

                    const double var_proj = use_green_only_1x ? (sig2_base / 225.0) : ((sig_wb * sig_wb) / 450.0);
                    double max_iqr_f = 0.0;
                    for (int vi = 1; vi < nv_use; ++vi) {
                        max_iqr_f = std::max(max_iqr_f, iqr_flow[size_t(valid_frames[size_t(vi)])]);
                    }

                    auto solve_luma_cands = [&](const std::array<std::array<double, 2>, 4>& cset, int cm,
                                                double out_u_tot[8], double& out_rss) noexcept -> bool {
                        const int Dc = 2 * cm;
                        double AtA_c[8][8]{};
                        double Atb_c[8]{};
                        std::array<std::array<double, 8>, 64> r0_v{}, r1_v{};
                        for (int vi = 0; vi < nv_use; ++vi) {
                            double* row0 = r0_v[size_t(vi)].data();
                            double* row1 = r1_v[size_t(vi)].data();
                            const double dx = shift_ch[1][size_t(vi)][0];
                            const double dy = shift_ch[1][size_t(vi)][1];
                            double hs_use[2][2] = {{hsqrt[0][0], hsqrt[0][1]}, {hsqrt[1][0], hsqrt[1][1]}};
                            if (alpha_chirp != 0.0) {
                                const double dtau = ch_tx * dx + ch_ty * dy;
                                double mv00 = 0.0, mv01 = 0.0, mv11 = 0.0;
                                for (size_t idx = 0; idx < TILE_1X * TILE_1X; ++idx) {
                                    const double w = W1x[idx];
                                    if (w <= 0.0) continue;
                                    const double t = tau0_1x[idx] - dtau;
                                    const double ang = base_ang_1x[idx] + std::numbers::pi_v<double> * alpha_chirp * t * t;
                                    const double cq = std::cos(ang), sq = std::sin(ang);
                                    mv00 += w * cq * cq; mv01 += w * cq * sq; mv11 += w * sq * sq;
                                }
                                mv00 /= w_s1; mv01 /= w_s1; mv11 /= w_s1;
                                double ihs_tmp[2][2]{};
                                detail::invert_2x2_spd(mv00, mv01, mv11, hs_use, ihs_tmp);
                            }
                            for (int m = 0; m < cm; ++m) {
                                const double ph = 2.0 * std::numbers::pi_v<double> * (cset[size_t(m)][0] * dx + cset[size_t(m)][1] * dy);
                                const double cs = std::cos(ph), sn = std::sin(ph);
                                row0[2 * m + 0] = hs_use[0][0] * cs + hs_use[0][1] * sn;
                                row0[2 * m + 1] = hs_use[0][0] * (-sn) + hs_use[0][1] * cs;
                                row1[2 * m + 0] = hs_use[1][0] * cs + hs_use[1][1] * sn;
                                row1[2 * m + 1] = hs_use[1][0] * (-sn) + hs_use[1][1] * cs;
                            }
                            const double yw0 = yw0_raw[size_t(vi)], yw1 = yw1_raw[size_t(vi)];
                            for (int i = 0; i < Dc; ++i) {
                                Atb_c[i] += row0[i] * yw0 + row1[i] * yw1;
                                for (int j = 0; j < Dc; ++j) AtA_c[i][j] += row0[i] * row0[j] + row1[i] * row1[j];
                            }
                        }
                        double evals[8]{}, evecs[8][8]{};
                        detail::jacobi_sym_eig<8>(Dc, AtA_c, evals, evecs);
                        const double rel_floor = (Nv >= 32) ? (1e-5 * evals[0]) : ((max_iqr_f < 0.12) ? (2.5e-4 * evals[0]) : (0.02 * evals[0]));
                        const double e_floor = std::max(rel_floor, 80.0 * var_proj);
                        if (evals[0] <= std::max(1e-8, 80.0 * var_proj) || evals[Dc - 1] <= e_floor) return false;
                        for (int i = 0; i < 8; ++i) out_u_tot[i] = 0.0;
                        for (int k = 0; k < Dc; ++k) {
                            double proj = 0.0;
                            for (int i = 0; i < Dc; ++i) proj += evecs[i][k] * Atb_c[i];
                            const double step = proj / (evals[k] + 2.0 * var_proj);
                            for (int i = 0; i < Dc; ++i) out_u_tot[i] += step * evecs[i][k];
                        }
                        double amp_sq = 0.0;
                        for (int i = 0; i < Dc; ++i) amp_sq += out_u_tot[i] * out_u_tot[i];
                        const double amp_lim = 2.6 * std::max({rms_wb, rms_hp_raw, raw_g_amp});
                        if (amp_sq <= 1e-10 || std::sqrt(amp_sq) > amp_lim) return false;
                        double rss = 0.0;
                        for (int vi = 0; vi < nv_use; ++vi) {
                            double p0 = 0.0, p1 = 0.0;
                            for (int i = 0; i < Dc; ++i) {
                                p0 += r0_v[size_t(vi)][size_t(i)] * out_u_tot[i];
                                p1 += r1_v[size_t(vi)][size_t(i)] * out_u_tot[i];
                            }
                            const double e0 = p0 - yw0_raw[size_t(vi)], e1 = p1 - yw1_raw[size_t(vi)];
                            rss += e0 * e0 + e1 * e1;
                        }
                        out_rss = rss;
                        return true;
                    };

                    double u_sol[8]{};
                    double proj_rss_after = 1e30;
                    bool ok_full = solve_luma_cands(b_cands, M, u_sol, proj_rss_after);
                    if (M == 4 && max_frac_x >= 0.005 && max_frac_y >= 0.005) {
                        const double fxb = (fx0 <= 0.5) ? fx0 : (fx0 - 1.0);
                        const double fyb = (fy0 <= 0.5) ? fy0 : (fy0 - 1.0);
                        std::array<std::array<double, 2>, 4> c2_dom{};
                        c2_dom[0] = {fxb, fyb};
                        if (std::abs(fxb) >= std::abs(fyb)) {
                            c2_dom[1] = {(fxb >= 0.0) ? (fxb - 1.0) : (fxb + 1.0), fyb};
                        } else {
                            c2_dom[1] = {fxb, (fyb >= 0.0) ? (fyb - 1.0) : (fyb + 1.0)};
                        }
                        double u2_dom[8]{}, rss2_dom = 1e30;
                        if (solve_luma_cands(c2_dom, 2, u2_dom, rss2_dom) &&
                            rss2_dom <= 0.18 * proj_rss_raw &&
                            (!ok_full || ((rss2_dom - proj_rss_after) <= 32.0 * var_proj) || (proj_rss_after > 0.72 * rss2_dom && rss2_dom <= 0.15 * proj_rss_raw))) {
                            b_cands = c2_dom;
                            M = 2;
                            std::memcpy(u_sol, u2_dom, sizeof(u_sol));
                            proj_rss_after = rss2_dom;
                            ok_full = true;
                        }
                    }
                    if (!ok_full || proj_rss_after > 0.38 * proj_rss_raw) break;

                    // Project current (rgb_2x + tile_delta) onto winning basis b_cands[0..M-1] so we only add the exact difference u_del_ch!
                    double u_cur_ch[3][8]{};
                    if (use_green_only_1x) {
                        const int Dc = 2 * M;
                        double mean2x[3]{}, w2_sum = 0.0;
                        for (int r2 = 0; r2 < TILE_2X; ++r2) {
                            const int gy2 = std::clamp(ty0 * 2 + r2, 0, H2 - 1);
                            const int by4 = (r2 >> 4) * 4;
                            const double wy2 = hann64[size_t(r2)];
                            for (int c2 = 0; c2 < TILE_2X; ++c2) {
                                const double w2 = wy2 * hann64[size_t(c2)] * m_blk1x[by4 + (c2 >> 4)];
                                if (w2 <= 0.0) continue;
                                const int gx2 = std::clamp(tx0 * 2 + c2, 0, W2 - 1);
                                const float* rp = rgb_2x + (size_t(gy2) * size_t(W2) + size_t(gx2)) * 3u;
                                const size_t id3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                                w2_sum += w2;
                                for (int ch = 0; ch < 3; ++ch) mean2x[ch] += w2 * (double(rp[ch]) + tile_delta[id3 + size_t(ch)]);
                            }
                        }
                        if (w2_sum > 1e-12) {
                            for (int ch = 0; ch < 3; ++ch) mean2x[ch] /= w2_sum;
                            double G2[8][8]{}, B2[3][8]{};
                            for (int r2 = 0; r2 < TILE_2X; ++r2) {
                                const int gy2 = std::clamp(ty0 * 2 + r2, 0, H2 - 1);
                                const int by4 = (r2 >> 4) * 4;
                                const double wy2 = hann64[size_t(r2)];
                                const double y_a = double(ty0) + 0.5 * double(r2);
                                for (int c2 = 0; c2 < TILE_2X; ++c2) {
                                    const double w2 = wy2 * hann64[size_t(c2)] * m_blk1x[by4 + (c2 >> 4)];
                                    if (w2 <= 0.0) continue;
                                    const int gx2 = std::clamp(tx0 * 2 + c2, 0, W2 - 1);
                                    const double x_a = double(tx0) + 0.5 * double(c2);
                                    const double t = (alpha_chirp != 0.0) ? (ch_tx * (x_a - xc_abs) + ch_ty * (y_a - yc_abs)) : 0.0;
                                    const double qph = std::numbers::pi_v<double> * alpha_chirp * t * t;
                                    double phi[8]{};
                                    for (int m = 0; m < M; ++m) {
                                        const double ang = 2.0 * std::numbers::pi_v<double> * (b_cands[size_t(m)][0] * x_a + b_cands[size_t(m)][1] * y_a) + qph;
                                        phi[2 * m + 0] = std::cos(ang);
                                        phi[2 * m + 1] = std::sin(ang);
                                    }
                                    const float* rp = rgb_2x + (size_t(gy2) * size_t(W2) + size_t(gx2)) * 3u;
                                    const size_t id3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                                    const double vch[3] = {
                                        (double(rp[0]) + tile_delta[id3 + 0] - mean2x[0]) * w2,
                                        (double(rp[1]) + tile_delta[id3 + 1] - mean2x[1]) * w2,
                                        (double(rp[2]) + tile_delta[id3 + 2] - mean2x[2]) * w2
                                    };
                                    for (int i = 0; i < Dc; ++i) {
                                        B2[0][i] += phi[i] * vch[0];
                                        B2[1][i] += phi[i] * vch[1];
                                        B2[2][i] += phi[i] * vch[2];
                                        const double wphi = w2 * phi[i];
                                        for (int j = i; j < Dc; ++j) G2[i][j] += wphi * phi[j];
                                    }
                                }
                            }
                            for (int i = 0; i < Dc; ++i) {
                                for (int j = 0; j < Dc; ++j) {
                                    if (j < i) G2[i][j] = G2[j][i];
                                }
                            }
                            for (int i = 0; i < Dc; ++i) {
                                for (int j = 0; j < Dc; ++j) G2[i][j] /= w2_sum;
                                for (int ch = 0; ch < 3; ++ch) B2[ch][i] /= w2_sum;
                            }
                            double ev2[8]{}, vec2[8][8]{};
                            detail::jacobi_sym_eig<8>(Dc, G2, ev2, vec2);
                            if (ev2[0] > 1e-8) {
                                for (int k = 0; k < Dc; ++k) {
                                    if (ev2[k] <= 0.10 * ev2[0]) continue;
                                    for (int ch = 0; ch < 3; ++ch) {
                                        double pr = 0.0;
                                        for (int i = 0; i < Dc; ++i) pr += vec2[i][k] * B2[ch][i];
                                        const double st = pr / ev2[k];
                                        for (int i = 0; i < Dc; ++i) u_cur_ch[ch][i] += st * vec2[i][k];
                                    }
                                }
                            }
                        }
                    }

                    double u_del_ch[3][8]{};
                    for (int ch = 0; ch < 3; ++ch) {
                        const double scale_ch = use_green_only_1x ? double(inv_wb_ch[ch]) : double(inv_wb_ch[ch]);
                        for (int i = 0; i < 2 * M; ++i) {
                            u_del_ch[ch][i] = u_sol[i] * scale_ch - u_cur_ch[ch][i];
                        }
                    }

                    auto eval_del_pt = [&](const double u_ch[8], int iy2, int ix2) noexcept -> double {
                        const double x_a = 0.5 * double(ix2);
                        const double y_a = 0.5 * double(iy2);
                        const double t = (alpha_chirp != 0.0) ? (ch_tx * (x_a - xc_abs) + ch_ty * (y_a - yc_abs)) : 0.0;
                        const double qph = std::numbers::pi_v<double> * alpha_chirp * t * t;
                        double v = 0.0;
                        for (int m = 0; m < M; ++m) {
                            const double ang = 2.0 * std::numbers::pi_v<double> * (b_cands[size_t(m)][0] * x_a + b_cands[size_t(m)][1] * y_a) + qph;
                            v += u_ch[2 * m + 0] * std::cos(ang) + u_ch[2 * m + 1] * std::sin(ang);
                        }
                        return v;
                    };

                    auto sample_del_bilerp = [&](const double u_ch[8], double y_anc_1x, double x_anc_1x) noexcept -> double {
                        const float y2 = float(y_anc_1x * 2.0);
                        const float x2 = float(x_anc_1x * 2.0);
                        const float yc = detail::clamp_f(y2, 0.0f, float(H2 - 1));
                        const float xc = detail::clamp_f(x2, 0.0f, float(W2 - 1));
                        const int iy0 = int(yc), ix0 = int(xc);
                        const int iy1 = std::min(H2 - 1, iy0 + 1), ix1 = std::min(W2 - 1, ix0 + 1);
                        const float fy = yc - float(iy0), fx = xc - float(ix0);
                        const double p00 = eval_del_pt(u_ch, iy0, ix0);
                        const double p01 = (ix1 == ix0) ? p00 : eval_del_pt(u_ch, iy0, ix1);
                        const double p10 = (iy1 == iy0) ? p00 : eval_del_pt(u_ch, iy1, ix0);
                        const double p11 = (iy1 == iy0) ? p01 : ((ix1 == ix0) ? p10 : eval_del_pt(u_ch, iy1, ix1));
                        return (1.0 - double(fy)) * ((1.0 - double(fx)) * p00 + double(fx) * p01) +
                               double(fy) * ((1.0 - double(fx)) * p10 + double(fx) * p11);
                    };

                    // Exact per-4x4-1x-cell (8x8 2x-px) sensor RSS reduction gate relative to Stage 4A's rgb_2x!
                    double rss0_b1[64]{}, rss1_b1[64]{};
                    std::vector<std::array<double, TILE_1X * TILE_1X>> del_wb_arr(static_cast<size_t>(Nv));
                    for (int vi = 0; vi < Nv; ++vi) {
                        double dm = 0.0;
                        for (int r = 0; r < TILE_1X; ++r) {
                            const int rr = ty0 + r;
                            const uint8_t* rcfa = cfa6x6[rr & 1];
                            for (int c = 0; c < TILE_1X; ++c) {
                                const int cc = tx0 + c;
                                const uint8_t ch = rcfa[cc & 1];
                                const double c_dx = shift_ch[ch][size_t(vi)][0];
                                const double c_dy = shift_ch[ch][size_t(vi)][1];
                                const double d_wb = sample_del_bilerp(u_del_ch[ch], double(rr) - c_dy, double(cc) - c_dx) * double(wb_ch[ch]);
                                del_wb_arr[size_t(vi)][size_t(r * TILE_1X + c)] = d_wb;
                                dm += d_wb;
                            }
                        }
                        dm *= (1.0 / double(TILE_1X * TILE_1X));
                        for (int r = 0; r < TILE_1X; ++r) {
                            const int by8 = (r >> 2) * 8;
                            for (int c = 0; c < TILE_1X; ++c) {
                                const size_t idx = size_t(r * TILE_1X + c);
                                const double d_wb = del_wb_arr[size_t(vi)][idx] - dm;
                                del_wb_arr[size_t(vi)][idx] = d_wb;
                                const double r0 = res_wb_1x[size_t(vi)][idx];
                                const double r1 = r0 - d_wb;
                                const int bi = by8 + (c >> 2);
                                rss0_b1[bi] += r0 * r0;
                                rss1_b1[bi] += r1 * r1;
                            }
                        }
                    }
                    double max_rss0_b1 = 0.0;
                    for (double v : rss0_b1) max_rss0_b1 = std::max(max_rss0_b1, v);
                    double g_b1[64]{};
                    double max_g_b1 = 0.0;
                    const double min_drop_b1 = 0.5 * double(Nv) * 16.0 * sig_wb * sig_wb;
                    for (int b = 0; b < 64; ++b) {
                        if (rss0_b1[b] > 1e-20 && rss0_b1[b] >= 0.04 * max_rss0_b1 &&
                            rss1_b1[b] < 0.88 * rss0_b1[b] && (rss0_b1[b] - rss1_b1[b]) > min_drop_b1) {
                            g_b1[b] = std::clamp((0.88 - rss1_b1[b] / rss0_b1[b]) / 0.25, 0.0, 1.0);
                            max_g_b1 = std::max(max_g_b1, g_b1[b]);
                        }
                    }
                    if (max_g_b1 <= 0.0) break;

                    auto eval_b1_env_2x = [&](int r2, int c2) noexcept -> double {
                        const int by = std::clamp(r2 >> 3, 0, 7);
                        const int bx = std::clamp(c2 >> 3, 0, 7);
                        double g = g_b1[by * 8 + bx];
                        if (g <= 0.0) return 0.0;
                        const int ly = r2 & 7, lx = c2 & 7;
                        if (ly < 2 && by > 0 && g_b1[(by - 1) * 8 + bx] < g) {
                            const double t = (double(ly) + 0.5) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_b1[(by - 1) * 8 + bx]) + t * g;
                        } else if (ly >= 6 && by < 7 && g_b1[(by + 1) * 8 + bx] < g) {
                            const double t = (7.5 - double(ly)) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_b1[(by + 1) * 8 + bx]) + t * g;
                        }
                        if (lx < 2 && bx > 0 && g_b1[by * 8 + (bx - 1)] < g) {
                            const double t = (double(lx) + 0.5) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_b1[by * 8 + (bx - 1)]) + t * g;
                        } else if (lx >= 6 && bx < 7 && g_b1[by * 8 + (bx + 1)] < g) {
                            const double t = (7.5 - double(lx)) * 0.5;
                            g = (1.0 - t) * 0.5 * (g + g_b1[by * 8 + (bx + 1)]) + t * g;
                        }
                        return g;
                    };

                    if (alpha_chirp == 0.0) {
                        double cx64[4][TILE_2X], sx64[4][TILE_2X], cy64[4][TILE_2X], sy64[4][TILE_2X];
                        for (int m = 0; m < M; ++m) {
                            for (int i = 0; i < TILE_2X; ++i) {
                                const double ax = 2.0 * std::numbers::pi_v<double> * b_cands[size_t(m)][0] * (double(tx0) + 0.5 * double(i));
                                const double ay = 2.0 * std::numbers::pi_v<double> * b_cands[size_t(m)][1] * (double(ty0) + 0.5 * double(i));
                                cx64[m][i] = std::cos(ax); sx64[m][i] = std::sin(ax);
                                cy64[m][i] = std::cos(ay); sy64[m][i] = std::sin(ay);
                            }
                        }
                        for (int r2 = 0; r2 < TILE_2X; ++r2) {
                            for (int c2 = 0; c2 < TILE_2X; ++c2) {
                                const double gb = eval_b1_env_2x(r2, c2);
                                if (gb <= 0.0) continue;
                                double dv0 = 0.0, dv1 = 0.0, dv2 = 0.0;
                                for (int m = 0; m < M; ++m) {
                                    const double c_xy = cx64[m][c2] * cy64[m][r2] - sx64[m][c2] * sy64[m][r2];
                                    const double s_xy = sx64[m][c2] * cy64[m][r2] + cx64[m][c2] * sy64[m][r2];
                                    dv0 += u_del_ch[0][2 * m + 0] * c_xy + u_del_ch[0][2 * m + 1] * s_xy;
                                    dv1 += u_del_ch[1][2 * m + 0] * c_xy + u_del_ch[1][2 * m + 1] * s_xy;
                                    dv2 += u_del_ch[2][2 * m + 0] * c_xy + u_del_ch[2][2 * m + 1] * s_xy;
                                }
                                const size_t idx3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                                tile_delta[idx3 + 0] += gb * dv0;
                                tile_delta[idx3 + 1] += gb * dv1;
                                tile_delta[idx3 + 2] += gb * dv2;
                            }
                        }
                    } else {
                        for (int r2 = 0; r2 < TILE_2X; ++r2) {
                            const double y2_a = double(ty0) + 0.5 * double(r2);
                            for (int c2 = 0; c2 < TILE_2X; ++c2) {
                                const double gb = eval_b1_env_2x(r2, c2);
                                if (gb <= 0.0) continue;
                                const double x2_a = double(tx0) + 0.5 * double(c2);
                                const double t = ch_tx * (x2_a - xc_abs) + ch_ty * (y2_a - yc_abs);
                                const double qph = std::numbers::pi_v<double> * alpha_chirp * t * t;
                                double dv0 = 0.0, dv1 = 0.0, dv2 = 0.0;
                                for (int m = 0; m < M; ++m) {
                                    const double ang = 2.0 * std::numbers::pi_v<double> * (b_cands[size_t(m)][0] * x2_a + b_cands[size_t(m)][1] * y2_a) + qph;
                                    const double c_xy = std::cos(ang), s_xy = std::sin(ang);
                                    dv0 += u_del_ch[0][2 * m + 0] * c_xy + u_del_ch[0][2 * m + 1] * s_xy;
                                    dv1 += u_del_ch[1][2 * m + 0] * c_xy + u_del_ch[1][2 * m + 1] * s_xy;
                                    dv2 += u_del_ch[2][2 * m + 0] * c_xy + u_del_ch[2][2 * m + 1] * s_xy;
                                }
                                const size_t idx3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                                tile_delta[idx3 + 0] += gb * dv0;
                                tile_delta[idx3 + 1] += gb * dv1;
                                tile_delta[idx3 + 2] += gb * dv2;
                            }
                        }
                    }
                    for (int vi = 0; vi < Nv; ++vi) {
                        for (int r = 0; r < TILE_1X; ++r) {
                            const int by8 = (r >> 2) * 8;
                            for (int c = 0; c < TILE_1X; ++c) {
                                const double gb = g_b1[by8 + (c >> 2)];
                                if (gb <= 0.0) continue;
                                const size_t idx = size_t(r * TILE_1X + c);
                                res_wb_1x[size_t(vi)][idx] -= gb * del_wb_arr[size_t(vi)][idx];
                            }
                        }
                        for (int qy = 0; qy < SUB_N; ++qy) {
                            const int by8 = (qy >> 1) * 8;
                            for (int qx = 0; qx < SUB_N; ++qx) {
                                const double gb = g_b1[by8 + (qx >> 1)];
                                if (gb <= 0.0) continue;
                                const size_t qi = size_t(qy * SUB_N + qx);
                                auto eval_sub_chg = [&](int off_y, int off_x, int ch_i) {
                                    const double rr_d = double(ty0 + 2 * qy + off_y);
                                    const double cc_d = double(tx0 + 2 * qx + off_x);
                                    return gb * sample_del_bilerp(u_del_ch[ch_i], rr_d - shift_ch[ch_i][size_t(vi)][1], cc_d - shift_ch[ch_i][size_t(vi)][0]);
                                };
                                res_r[size_t(vi)][qi] -= eval_sub_chg(r_dy, r_dx, 0);
                                res_b[size_t(vi)][qi] -= eval_sub_chg(b_dy, b_dx, 2);
                                res_g1[size_t(vi)][qi] -= eval_sub_chg(g1_dy, g1_dx, 1);
                                res_g2[size_t(vi)][qi] -= eval_sub_chg(g2_dy, g2_dx, 1);
                            }
                        }
                    }
                    if (n_solved_peaks < 3) {
                        solved_fx[n_solved_peaks] = fx0;
                        solved_fy[n_solved_peaks] = fy0;
                        ++n_solved_peaks;
                    }
                    tile_modified = true;
                }
            };

            solve_1x_brillouin_luma();
            if (need_r_fft || tile_modified) solve_rb_sublattice(0, r_dy, r_dx, res_r);
            if (need_b_fft || tile_modified) solve_rb_sublattice(2, b_dy, b_dx, res_b);

            if (tile_modified) {
                for (int r2 = 0; r2 < TILE_2X; ++r2) {
                    const int gy2 = ty0 * 2 + r2;
                    if (gy2 < buf_y0_2x || gy2 >= buf_y1_2x) continue;
                    const double wy = hann64[size_t(r2)];
                    float* drow = accum_delta.data() + (size_t(gy2 - buf_y0_2x) * size_t(W2) + size_t(tx0 * 2)) * 3u;
                    for (int c2 = 0; c2 < TILE_2X; ++c2) {
                        const float w = float(wy * hann64[size_t(c2)]);
                        const size_t idx3 = (size_t(r2) * size_t(TILE_2X) + size_t(c2)) * 3u;
                        drow[c2 * 3 + 0] += w * float(tile_delta[idx3 + 0]);
                        drow[c2 * 3 + 1] += w * float(tile_delta[idx3 + 1]);
                        drow[c2 * 3 + 2] += w * float(tile_delta[idx3 + 2]);
                    }
                }
            }
        }
    };

    // 3-color disjoint tile-row sweeps: zero mutexes, zero overlapping row writes even for tail-clamped H1!
    const int n_ty = static_cast<int>(ty_list.size());
    for (int color = 0; color < 3; ++color) {
        const int n_pass = (n_ty - color + 2) / 3;
        if (n_pass <= 0) continue;
        run_range(n_pass, [&](int i0, int i1) {
            for (int i = i0; i < i1; ++i) {
                process_tile_row(ty_list[size_t(i * 3 + color)]);
            }
        });
    }

    const int out_y0 = std::max(buf_y0_2x, y0_1x * 2);
    const int out_y1 = std::min(buf_y1_2x, y1_1x * 2);
    run_range(out_y1 - out_y0, [&](int dr0, int dr1) {
        for (int dr = dr0; dr < dr1; ++dr) {
            const int r2 = out_y0 + dr;
            float* dst = rgb_2x + size_t(r2) * size_t(W2) * 3u;
            const float* dlt = accum_delta.data() + size_t(r2 - buf_y0_2x) * size_t(W2) * 3u;
            const float* ws = accum_wsum.data() + size_t(r2 - buf_y0_2x) * size_t(W2);
            for (int c2 = 0; c2 < W2; ++c2) {
                const float w = ws[c2];
                if (w <= 1e-8f) continue;
                const float inv_w = 1.0f / w;
                dst[c2 * 3 + 0] += dlt[c2 * 3 + 0] * inv_w;
                dst[c2 * 3 + 1] += dlt[c2 * 3 + 1] * inv_w;
                dst[c2 * 3 + 2] += dlt[c2 * 3 + 2] * inv_w;
            }
        }
    });
}

// Stage 3 Reciprocal-Lattice Global Rigid Verifier & Curved-Arc Brillouin Registration:
//   1. Candidate A: Whole-frame anti-aliased bicubic IRLS Gauss-Newton on g0_pre vs gi_pres[n] (includes outer border ring,
//      eliminating aperture sliding on interior beat waves / multi-direction gratings such as W04, K04, A09).
//   2. Candidate B: Full-1x White-Balanced Top-96 Brillouin Carrier Arc Solver + Macroscopic L10.5 Envelope Selector
//      (immune to 2*f0-1 / 3*f0-1 nonlinear specular harmonic moire zoneplates on curved grooves M02..M05 and C03).
//   Strictly gated against align_frame_pyramid's local flow residuals so parallax, occlusion, rolling shutter, zoom,
//   rotation, and periodic-wrap fiducial bursts remain 100% untouched.
inline void refine_global_rigid_registration(
    int H1,
    int W1,
    int Hh,
    int Wh,
    const uint8_t cfa6x6[6][6],
    const std::array<float, 3>& cam_wb,
    float sig0_1px,
    const std::vector<std::vector<float>>& frames_lin,
    const float* g0_pre,
    const std::vector<std::vector<float>>& gi_pres,
    const std::vector<double>& frame_init_scales,
    std::vector<std::vector<float>>& flows_fy,
    std::vector<std::vector<float>>& flows_fx) {
    const int N = static_cast<int>(frames_lin.size());
    if (N < 2 || H1 < 32 || W1 < 32 || Hh < 16 || Wh < 16 || !g0_pre || int(gi_pres.size()) < N) return;
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            if (cfa6x6[r][c] != cfa6x6[r & 1][c & 1]) return;
        }
    }

    auto pct_vec = [](std::vector<float>& v, float q) -> float {
        if (v.empty()) return 0.0f;
        const size_t idx = std::min(v.size() - 1u, size_t(std::clamp(q, 0.0f, 1.0f) * float(v.size() - 1u) + 0.5f));
        std::nth_element(v.begin(), v.begin() + ptrdiff_t(idx), v.end());
        return v[idx];
    };

    const bool allow_bril = (H1 <= 1024 && W1 <= 1024 && H1 >= 64 && W1 >= 64);
    if (!allow_bril) return;

    auto build_quincunx_g1x = [&](int fn, std::vector<float>& dst) {
        dst.resize(size_t(H1) * size_t(W1));
        const float* src = frames_lin[size_t(fn)].data();
        for (int r = 0; r < H1; ++r) {
            const int rm = (r > 0) ? (r - 1) : std::min(H1 - 1, 1);
            const int rp = (r + 1 < H1) ? (r + 1) : std::max(0, H1 - 2);
            const float* row0 = src + size_t(r) * size_t(W1);
            const float* rowm = src + size_t(rm) * size_t(W1);
            const float* rowp = src + size_t(rp) * size_t(W1);
            float* drow = dst.data() + size_t(r) * size_t(W1);
            const uint8_t* rcfa = cfa6x6[r & 1];
            for (int c = 0; c < W1; ++c) {
                if (rcfa[c & 1] == 1) {
                    drow[c] = row0[c];
                } else {
                    const int cm = (c > 0) ? (c - 1) : std::min(W1 - 1, 1);
                    const int cp = (c + 1 < W1) ? (c + 1) : std::max(0, W1 - 2);
                    drow[c] = 0.25f * (rowm[c] + rowp[c] + row0[cm] + row0[cp]);
                }
            }
        }
    };

    // Gate on true super-0.5x-Nyquist aliasing ([0.25..0.50] vs [0.03..0.22] cy/px_1x energy on Frame 0):
    // leaves non-aliased scenes (ER01..07, FI01..06, feathers, rails, etc.) 100% untouched!
    std::vector<float> g1_0, g1_b135(size_t(H1) * size_t(W1)), g1_b600(size_t(H1) * size_t(W1));
    build_quincunx_g1x(0, g1_0);
    detail::sep_gauss_2d_clamped(g1_0.data(), g1_b135.data(), H1, W1, 1.35f);
    detail::sep_gauss_2d_clamped(g1_0.data(), g1_b600.data(), H1, W1, 6.0f);

    std::vector<float> hi_sq(size_t(H1) * size_t(W1), 0.0f);
    double sum_ehi = 0.0, sum_elo = 0.0;
    for (int r = 0; r < H1; ++r) {
        for (int c = 0; c < W1; ++c) {
            const size_t i = size_t(r) * size_t(W1) + size_t(c);
            const float dh = g1_0[i] - g1_b135[i];
            const float dl = g1_b135[i] - g1_b600[i];
            hi_sq[i] = dh * dh;
            if (r >= 8 && r + 8 < H1 && c >= 8 && c + 8 < W1) {
                sum_ehi += double(dh) * double(dh);
                sum_elo += double(dl) * double(dl);
            }
        }
    }
    double max_tile_rat = 0.0;
    for (int r = 8; r + 40 <= H1; r += 16) {
        for (int c = 8; c + 40 <= W1; c += 16) {
            double eh = 0.0, el = 0.0;
            for (int dy = 0; dy < 32; ++dy) {
                const size_t rb = size_t(r + dy) * size_t(W1) + size_t(c);
                for (int dx = 0; dx < 32; ++dx) {
                    eh += double(hi_sq[rb + size_t(dx)]);
                    const float dl = g1_b135[rb + size_t(dx)] - g1_b600[rb + size_t(dx)];
                    el += double(dl) * double(dl);
                }
            }
            eh *= (1.0 / 1024.0);
            el *= (1.0 / 1024.0);
            if (eh > 1e-4) max_tile_rat = std::max(max_tile_rat, eh / std::max(1e-6, el));
        }
    }
    const float wb_ch[3] = {std::max(0.25f, cam_wb[0]), 1.0f, std::max(0.25f, cam_wb[2])};

    // Sublattice spectral independence test on Frame 0: detect genuinely independent R/B spatial carriers
    // (such as multi-color independent gratings where |F_R(k)|^2 or |F_B(k)|^2 peaks at k absent in Green),
    // while keeping achromatic super-Nyquist Brillouin quartets (K05/K06/K08), grooves (M03/M05), halftone
    // vignettes (I03), and broadband parallax scenes (D02-D04) strictly on their exact Rev.2 paths.
    const bool geom_ok_mc = (Hh >= 32 && Wh >= 32 && Hh <= 512 && Wh <= 512);
    int PY_h = 32, PX_h = 32;
    int sub_hh = std::min(Hh, 256), sub_wh = std::min(Wh, 256);
    int y0_hh = (Hh - sub_hh) / 2;
    int x0_hh = (Wh - sub_wh) / 2;
    while (PY_h < sub_hh && PY_h < 256) PY_h <<= 1;
    while (PX_h < sub_wh && PX_h < 256) PX_h <<= 1;
    const int PY_q = PY_h * 2, PX_q = PX_h * 2;
    const int sub_hq = sub_hh * 2, sub_wq = sub_wh * 2;
    const int y0_hq = y0_hh * 2, x0_hq = x0_hh * 2;
    std::vector<double> wy_h, wx_h, wy_q, wx_q;
    std::array<std::vector<std::complex<double>>, 4> F0_sub;
    std::vector<std::complex<double>> F0_gqx;
    std::array<std::vector<float>, 4> S0_sub, L5_0_sub;

    auto extract_sublattice_wb = [&](int fn, int p, std::vector<float>& dst) {
        dst.resize(size_t(Hh) * size_t(Wh));
        const int dy_p = p >> 1, dx_p = p & 1;
        const float wbp = wb_ch[cfa6x6[dy_p][dx_p]];
        const float* src = frames_lin[size_t(fn)].data();
        for (int r = 0; r < Hh; ++r) {
            const float* srow = src + size_t(2 * r + dy_p) * size_t(W1) + size_t(dx_p);
            float* drow = dst.data() + size_t(r) * size_t(Wh);
            for (int c = 0; c < Wh; ++c) drow[c] = srow[2 * c] * wbp;
        }
    };
    auto build_sub_fft = [&](const std::vector<float>& sub_im, std::vector<std::complex<double>>& F_out) {
        F_out.assign(size_t(PY_h) * size_t(PX_h), {0.0, 0.0});
        double w_sum = 0.0, vw_sum = 0.0;
        for (int r = 0; r < sub_hh; ++r) {
            const double wy = wy_h[size_t(r)];
            const float* srow = sub_im.data() + size_t(y0_hh + r) * size_t(Wh) + size_t(x0_hh);
            for (int c = 0; c < sub_wh; ++c) {
                const double w = wy * wx_h[size_t(c)];
                w_sum += w;
                vw_sum += w * double(srow[c]);
            }
        }
        const double mu = vw_sum / std::max(1e-12, w_sum);
        for (int r = 0; r < sub_hh; ++r) {
            const double wy = wy_h[size_t(r)];
            const float* srow = sub_im.data() + size_t(y0_hh + r) * size_t(Wh) + size_t(x0_hh);
            auto* drow = F_out.data() + size_t(r) * size_t(PX_h);
            for (int c = 0; c < sub_wh; ++c) {
                drow[c] = {wy * wx_h[size_t(c)] * (double(srow[c]) - mu), 0.0};
            }
        }
        detail::fft2d_pow2(F_out, PY_h, PX_h, false);
    };

    bool has_chroma_carrier = false;
    if (geom_ok_mc) {
        wy_h.resize(size_t(sub_hh));
        wx_h.resize(size_t(sub_wh));
        double wy2_s = 0.0, wx2_s = 0.0;
        for (int r = 0; r < sub_hh; ++r) {
            const double s = std::sin(std::numbers::pi_v<double> * (double(r) + 0.5) / double(sub_hh));
            wy_h[size_t(r)] = s * s;
            wy2_s += wy_h[size_t(r)] * wy_h[size_t(r)];
        }
        for (int c = 0; c < sub_wh; ++c) {
            const double s = std::sin(std::numbers::pi_v<double> * (double(c) + 0.5) / double(sub_wh));
            wx_h[size_t(c)] = s * s;
            wx2_s += wx_h[size_t(c)] * wx_h[size_t(c)];
        }
        int p_r = -1, p_b = -1;
        std::vector<int> p_gs;
        for (int p = 0; p < 4; ++p) {
            extract_sublattice_wb(0, p, S0_sub[size_t(p)]);
            build_sub_fft(S0_sub[size_t(p)], F0_sub[size_t(p)]);
            const uint8_t ch_p = cfa6x6[p >> 1][p & 1];
            if (ch_p == 0) p_r = p;
            else if (ch_p == 2) p_b = p;
            else if (ch_p == 1) p_gs.push_back(p);
        }
        if (p_r >= 0 && p_b >= 0 && !p_gs.empty()) {
            const size_t n_bins_h = size_t(PY_h) * size_t(PX_h);
            const double w2_tot = wy2_s * wx2_s;
            auto eval_indep_ratio = [&](const std::array<std::vector<double>, 4>& P_sum,
                                        const std::array<std::vector<double>, 4>* P_min,
                                        const std::array<std::vector<double>, 4>* P_max,
                                        int n_ens) -> bool {
                double e_rb_tot = 0.0, e_rb_indep = 0.0;
                size_t n_ac = 0;
                for (int r = 0; r < PY_h; ++r) {
                    const double fy = (r <= PY_h / 2) ? (double(r) / double(PY_h)) : (double(r - PY_h) / double(PY_h));
                    if (std::abs(fy) > 0.475) continue;
                    for (int c = 1; c < PX_h / 2; ++c) {
                        const double fx = double(c) / double(PX_h);
                        if (fx <= 0.015 || fx > 0.475) continue;
                        if (std::hypot(fy * double(PY_h), fx * double(PX_h)) < 2.0) continue;
                        const size_t k = size_t(r) * size_t(PX_h) + size_t(c);
                        double pg = 0.0;
                        for (int gp : p_gs) pg += P_sum[size_t(gp)][k];
                        pg /= double(p_gs.size());
                        const double pr = P_sum[size_t(p_r)][k];
                        const double pb = P_sum[size_t(p_b)][k];
                        e_rb_tot += pr + pb;
                        const bool stat_r = (!P_min || !P_max || (*P_min)[size_t(p_r)][k] >= 0.18 * (*P_max)[size_t(p_r)][k]);
                        const bool stat_b = (!P_min || !P_max || (*P_min)[size_t(p_b)][k] >= 0.18 * (*P_max)[size_t(p_b)][k]);
                        if (pg < 0.16 * pr && stat_r) e_rb_indep += pr;
                        if (pg < 0.16 * pb && stat_b) e_rb_indep += pb;
                        ++n_ac;
                    }
                }
                const double e_nse_rb = double(n_ens) * double(n_ac) * w2_tot * double(sig0_1px) * double(sig0_1px) *
                                        (double(wb_ch[0]) * double(wb_ch[0]) + double(wb_ch[2]) * double(wb_ch[2]));
                return (n_ac > 0u) && (e_rb_tot > 24.0 * e_nse_rb) && (e_rb_indep > 0.28 * e_rb_tot);
            };
            std::array<std::vector<double>, 4> P_ens, P_min, P_max;
            for (int p = 0; p < 4; ++p) {
                P_ens[size_t(p)].resize(n_bins_h);
                for (size_t k = 0; k < n_bins_h; ++k) P_ens[size_t(p)][k] = std::norm(F0_sub[size_t(p)][k]);
            }
            if (eval_indep_ratio(P_ens, nullptr, nullptr, 1)) {
                const int M_ens = std::min(N, 8);
                if (M_ens <= 1) {
                    has_chroma_carrier = true;
                } else {
                    for (int p = 0; p < 4; ++p) {
                        P_min[size_t(p)] = P_ens[size_t(p)];
                        P_max[size_t(p)] = P_ens[size_t(p)];
                    }
                    std::vector<float> sn_tmp;
                    std::vector<std::complex<double>> fn_tmp;
                    for (int fn = 1; fn < M_ens; ++fn) {
                        for (int p = 0; p < 4; ++p) {
                            extract_sublattice_wb(fn, p, sn_tmp);
                            build_sub_fft(sn_tmp, fn_tmp);
                            for (size_t k = 0; k < n_bins_h; ++k) {
                                const double pw = std::norm(fn_tmp[k]);
                                P_ens[size_t(p)][k] += pw;
                                P_min[size_t(p)][k] = std::min(P_min[size_t(p)][k], pw);
                                P_max[size_t(p)][k] = std::max(P_max[size_t(p)][k], pw);
                            }
                        }
                    }
                    has_chroma_carrier = eval_indep_ratio(P_ens, &P_min, &P_max, M_ens);
                }
            }
        }
    }

    const double glob_rat = sum_ehi / std::max(1e-8, sum_elo);
    if (glob_rat < 4.5 && max_tile_rat < 120.0 && !has_chroma_carrier) return;

    // Path A: Cramer-Rao-Verified Unaliased-Background 1x Gauss-Newton
    // (solves localized super-Nyquist gratings/screens with textured border/background such as A09, K03, K04, W03, W04)
    std::vector<float> env_hi(size_t(H1) * size_t(W1));
    detail::sep_gauss_2d_clamped(hi_sq.data(), env_hi.data(), H1, W1, 3.5f);
    std::vector<float> env_in;
    env_in.reserve(size_t(std::max(1, H1 - 6)) * size_t(std::max(1, W1 - 6)));
    for (int r = 3; r + 3 < H1; ++r) {
        for (int c = 3; c + 3 < W1; ++c) {
            env_in.push_back(env_hi[size_t(r) * size_t(W1) + size_t(c)]);
        }
    }
    const float p06_hi = pct_vec(env_in, 0.06f);
    const float p90_hi = pct_vec(env_in, 0.90f);
    std::vector<std::pair<int, int>> unal_pts;
    std::vector<float> sm0_1x, gx0_1x, gy0_1x;
    bool unal_bg_ready = false;
    if (p06_hi < 0.08f * p90_hi && p06_hi < 2.5e-4f) {
        const float thr_hi = std::max(3.5f * p06_hi, 2.0e-5f);
        for (int r = 3; r + 3 < H1; ++r) {
            for (int c = 3; c + 3 < W1; ++c) {
                if (env_hi[size_t(r) * size_t(W1) + size_t(c)] <= thr_hi) {
                    unal_pts.emplace_back(r, c);
                }
            }
        }
        if (unal_pts.size() >= 1500u) {
            sm0_1x.resize(size_t(H1) * size_t(W1));
            detail::sep_gauss_2d_clamped(g1_0.data(), sm0_1x.data(), H1, W1, 0.80f);
            gx0_1x.assign(size_t(H1) * size_t(W1), 0.0f);
            gy0_1x.assign(size_t(H1) * size_t(W1), 0.0f);
            for (int r = 1; r + 1 < H1; ++r) {
                for (int c = 1; c + 1 < W1; ++c) {
                    const size_t i = size_t(r) * size_t(W1) + size_t(c);
                    gx0_1x[i] = 0.5f * (sm0_1x[i + 1] - sm0_1x[i - 1]);
                    gy0_1x[i] = 0.5f * (sm0_1x[i + size_t(W1)] - sm0_1x[i - size_t(W1)]);
                }
            }
            double jxx = 0.0, jxy = 0.0, jyy = 0.0;
            for (const auto& [r, c] : unal_pts) {
                const size_t i = size_t(r) * size_t(W1) + size_t(c);
                const double gx = double(gx0_1x[i]), gy = double(gy0_1x[i]);
                jxx += gx * gx; jxy += gx * gy; jyy += gy * gy;
            }
            const double inv_n = 1.0 / double(unal_pts.size());
            jxx *= inv_n; jxy *= inv_n; jyy *= inv_n;
            const double grad_lmin = 0.5 * ((jxx + jyy) - std::hypot(jxx - jyy, 2.0 * jxy));
            if (grad_lmin >= 1.5e-5) unal_bg_ready = true;
        }
    }
    int PY = 64, PX = 64;
    int sub_h = std::min(H1, 512), sub_w = std::min(W1, 512);
    int y0_sub = ((H1 - sub_h) / 2) & ~1;
    int x0_sub = ((W1 - sub_w) / 2) & ~1;
    while (PY < sub_h && PY < 512) PY <<= 1;
    while (PX < sub_w && PX < 512) PX <<= 1;

    std::vector<double> wy_b, wx_b;
    std::vector<std::complex<double>> F0_bril;
    std::vector<float> wb0_1x, L10_0;
    bool bril_ref_active = false;

    auto build_wb_1x = [&](int fn, std::vector<float>& dst) {
        if (has_chroma_carrier) {
            build_quincunx_g1x(fn, dst);
            return;
        }
        dst.resize(size_t(H1) * size_t(W1));
        const float* src = frames_lin[size_t(fn)].data();
        for (int r = 0; r < H1; ++r) {
            const uint8_t* rcfa = cfa6x6[r & 1];
            const float* srow = src + size_t(r) * size_t(W1);
            float* drow = dst.data() + size_t(r) * size_t(W1);
            for (int c = 0; c < W1; ++c) {
                drow[c] = srow[c] * wb_ch[rcfa[c & 1]];
            }
        }
    };

    auto build_padded_fft = [&](const std::vector<float>& wb_im, std::vector<std::complex<double>>& F_out) {
        F_out.assign(size_t(PY) * size_t(PX), {0.0, 0.0});
        double w_sum = 0.0, vw_sum = 0.0;
        for (int r = 0; r < sub_h; ++r) {
            const double wy = wy_b[size_t(r)];
            const float* srow = wb_im.data() + size_t(y0_sub + r) * size_t(W1) + size_t(x0_sub);
            for (int c = 0; c < sub_w; ++c) {
                const double w = wy * wx_b[size_t(c)];
                w_sum += w;
                vw_sum += w * double(srow[c]);
            }
        }
        const double mu = vw_sum / std::max(1e-12, w_sum);
        for (int r = 0; r < sub_h; ++r) {
            const double wy = wy_b[size_t(r)];
            const float* srow = wb_im.data() + size_t(y0_sub + r) * size_t(W1) + size_t(x0_sub);
            auto* drow = F_out.data() + size_t(r) * size_t(PX);
            for (int c = 0; c < sub_w; ++c) {
                drow[c] = {wy * wx_b[size_t(c)] * (double(srow[c]) - mu), 0.0};
            }
        }
        detail::fft2d_pow2(F_out, PY, PX, false);
    };

    if (allow_bril) {
        wy_b.resize(size_t(sub_h));
        wx_b.resize(size_t(sub_w));
        for (int r = 0; r < sub_h; ++r) {
            const double s = std::sin(std::numbers::pi_v<double> * (double(r) + 0.5) / double(sub_h));
            wy_b[size_t(r)] = s * s;
        }
        for (int c = 0; c < sub_w; ++c) {
            const double s = std::sin(std::numbers::pi_v<double> * (double(c) + 0.5) / double(sub_w));
            wx_b[size_t(c)] = s * s;
        }
        build_wb_1x(0, wb0_1x);
        build_padded_fft(wb0_1x, F0_bril);

        // Quick frame-0 gate: only activate Brillouin arc solver if anchor frame has a 2D curved high-freq carrier!
        struct Bin0 { double mag, kx, ky; };
        std::vector<Bin0> b0;
        b0.reserve(size_t(PY) * size_t(PX / 2));
        for (int r = 0; r < PY; ++r) {
            const double fy = (r <= PY / 2) ? (double(r) / double(PY)) : (double(r - PY) / double(PY));
            if (std::abs(fy) > 0.48) continue;
            for (int c = 1; c < PX / 2; ++c) {
                const double fx = double(c) / double(PX);
                if (fx <= 0.01 || fx > 0.48) continue;
                if (std::hypot(fy * double(PY), fx * double(PX)) < 2.0) continue;
                const double m = std::abs(F0_bril[size_t(r) * size_t(PX) + size_t(c)]);
                b0.push_back({m, fx, fy});
            }
        }
        if (b0.size() >= 96u) {
            std::nth_element(b0.begin(), b0.end() - 96, b0.end(),
                             [](const Bin0& a, const Bin0& b) { return a.mag < b.mag; });
            std::array<int, 96> sec0{};
            std::array<double, 8> sec0_w{};
            double raw0_s = 0.0, w_s = 0.0, kr_s = 0.0, Jxx = 0.0, Jxy = 0.0, Jyy = 0.0;
            for (int i = 0; i < 96; ++i) {
                const auto& bi = b0[b0.size() - 96u + size_t(i)];
                raw0_s += bi.mag;
                const double ang = std::atan2(bi.ky, bi.kx);
                const int sb = std::clamp(int((ang + 0.5 * std::numbers::pi_v<double>) / std::numbers::pi_v<double> * 8.0), 0, 7);
                sec0[size_t(i)] = sb;
                sec0_w[size_t(sb)] += bi.mag;
            }
            for (int i = 0; i < 96; ++i) {
                const auto& bi = b0[b0.size() - 96u + size_t(i)];
                const double sw = sec0_w[size_t(sec0[size_t(i)])];
                const double w = bi.mag * ((sw > 1e-20 && raw0_s > 1e-20) ? std::clamp((raw0_s / 8.0) / sw, 0.25, 4.0) : 1.0);
                w_s += w;
                kr_s += w * std::hypot(bi.kx, bi.ky);
                Jxx += w * bi.kx * bi.kx;
                Jxy += w * bi.kx * bi.ky;
                Jyy += w * bi.ky * bi.ky;
            }
            if (w_s > 1e-20) {
                const double kr_m0 = kr_s / w_s;
                const double tr = Jxx + Jyy;
                const double disc = std::hypot(Jxx - Jyy, 2.0 * Jxy);
                const double lam_min = 0.5 * (tr - disc);
                const double lam_max = 0.5 * (tr + disc);
                if (kr_m0 >= 0.26 && lam_min >= 0.0015 * lam_max) {
                    bril_ref_active = true;
                    L10_0.resize(size_t(H1) * size_t(W1));
                    detail::sep_gauss_2d_clamped(wb0_1x.data(), L10_0.data(), H1, W1, 10.5f);
                }
            }
        }
    }

    // Path C setup: 4-Sublattice (R, G0, G1, B) Cross-Spectral Phase-Coherent Rigid Refiner
    // Eliminates 1D Green-only aperture drift and Bayer CFA carrier sidebands on chromatic scenes.
    bool mc_sub_ready = false;
    auto build_gqx_fft = [&](int fn, std::vector<std::complex<double>>& F_out) {
        F_out.assign(size_t(PY_q) * size_t(PX_q), {0.0, 0.0});
        const float* src = frames_lin[size_t(fn)].data();
        const double wbg = double(wb_ch[1]);
        double w_sum = 0.0, vw_sum = 0.0;
        for (int r = 0; r < sub_hq; ++r) {
            const int rr = y0_hq + r;
            const uint8_t* rcfa = cfa6x6[rr % 6];
            const double wy = wy_q[size_t(r)];
            const float* srow = src + size_t(rr) * size_t(W1) + size_t(x0_hq);
            for (int c = 0; c < sub_wq; ++c) {
                if (rcfa[(x0_hq + c) % 6] != 1) continue;
                const double w = wy * wx_q[size_t(c)];
                w_sum += w;
                vw_sum += w * double(srow[c]) * wbg;
            }
        }
        const double mu = vw_sum / std::max(1e-12, w_sum);
        for (int r = 0; r < sub_hq; ++r) {
            const int rr = y0_hq + r;
            const uint8_t* rcfa = cfa6x6[rr % 6];
            const double wy = wy_q[size_t(r)];
            const float* srow = src + size_t(rr) * size_t(W1) + size_t(x0_hq);
            auto* drow = F_out.data() + size_t(r) * size_t(PX_q);
            for (int c = 0; c < sub_wq; ++c) {
                if (rcfa[(x0_hq + c) % 6] != 1) continue;
                drow[c] = {wy * wx_q[size_t(c)] * (double(srow[c]) * wbg - mu), 0.0};
            }
        }
        detail::fft2d_pow2(F_out, PY_q, PX_q, false);
    };

    if (geom_ok_mc && has_chroma_carrier) {
        wy_q.resize(size_t(sub_hq));
        wx_q.resize(size_t(sub_wq));
        for (int r = 0; r < sub_hq; ++r) {
            const double s = std::sin(std::numbers::pi_v<double> * (double(r) + 0.5) / double(sub_hq));
            wy_q[size_t(r)] = s * s;
        }
        for (int c = 0; c < sub_wq; ++c) {
            const double s = std::sin(std::numbers::pi_v<double> * (double(c) + 0.5) / double(sub_wq));
            wx_q[size_t(c)] = s * s;
        }
        for (int p = 0; p < 4; ++p) {
            L5_0_sub[size_t(p)].resize(size_t(Hh) * size_t(Wh));
            const float sig_lp = (cfa6x6[p >> 1][p & 1] == 1) ? 1.35f : 2.8f;
            detail::sep_gauss_2d_clamped(S0_sub[size_t(p)].data(), L5_0_sub[size_t(p)].data(), Hh, Wh, sig_lp);
        }
        build_gqx_fft(0, F0_gqx);
        mc_sub_ready = true;
    }

    if (!unal_bg_ready && !bril_ref_active && !mc_sub_ready) return;
    std::vector<float> g1_n, smn_1x, wbn_1x, L10_n;
    std::array<std::vector<float>, 4> Sn_sub, L5_n_sub;
    std::vector<std::complex<double>> Fn_bril, Fn_sub_p, Fn_gqx;

    for (int n = 1; n < N; ++n) {
        if (n < int(frame_init_scales.size()) && std::abs(frame_init_scales[size_t(n)] - 1.0) >= 0.005) continue;
        if (flows_fy[size_t(n)].size() != size_t(Hh) * size_t(Wh) ||
            flows_fx[size_t(n)].size() != size_t(Hh) * size_t(Wh)) continue;

        if (unal_bg_ready) {
            build_quincunx_g1x(n, g1_n);
            smn_1x.resize(size_t(H1) * size_t(W1));
            detail::sep_gauss_2d_clamped(g1_n.data(), smn_1x.data(), H1, W1, 0.80f);
            std::vector<float> b135_n(size_t(H1) * size_t(W1)), hsq_n(size_t(H1) * size_t(W1)), env_hi_n(size_t(H1) * size_t(W1));
            detail::sep_gauss_2d_clamped(g1_n.data(), b135_n.data(), H1, W1, 1.35f);
            for (size_t i = 0; i < size_t(H1) * size_t(W1); ++i) {
                const float d = g1_n[i] - b135_n[i];
                hsq_n[i] = d * d;
            }
            detail::sep_gauss_2d_clamped(hsq_n.data(), env_hi_n.data(), H1, W1, 3.5f);
            const float thr_hi_sym = std::max(3.5f * p06_hi, 2.0e-5f);

            // Evaluate pyramid residual on unaliased pixels AND get median pyramid shift
            std::vector<float> fx_s, fy_s;
            fx_s.reserve(256); fy_s.reserve(256);
            const size_t st_f = std::max<size_t>(1u, flows_fx[size_t(n)].size() / 256u);
            for (size_t i = 0; i < flows_fx[size_t(n)].size(); i += st_f) {
                fx_s.push_back(2.0f * flows_fx[size_t(n)][i]);
                fy_s.push_back(2.0f * flows_fy[size_t(n)][i]);
            }
            const int med_dx_i = fx_s.empty() ? 0 : int(std::lround(pct_vec(fx_s, 0.50f)));
            const int med_dy_i = fy_s.empty() ? 0 : int(std::lround(pct_vec(fy_s, 0.50f)));

            const size_t p_step = std::max<size_t>(1u, unal_pts.size() / 8192u);
            double sum_u_pyr = 0.0;
            int cnt_u_pyr = 0;
            for (size_t pi = 0; pi < unal_pts.size(); pi += p_step) {
                const auto [r, c] = unal_pts[pi];
                const size_t hi = size_t(detail::clamp_i(r / 2, 0, Hh - 1)) * size_t(Wh) + size_t(detail::clamp_i(c / 2, 0, Wh - 1));
                const float wy = float(r) + 2.0f * flows_fy[size_t(n)][hi];
                const float wx = float(c) + 2.0f * flows_fx[size_t(n)][hi];
                if (wy < 2.0f || wy >= float(H1 - 2) || wx < 2.0f || wx >= float(W1 - 2)) continue;
                sum_u_pyr += std::abs(detail::bicubic_clamped(smn_1x.data(), H1, W1, wy, wx) - double(sm0_1x[size_t(r) * size_t(W1) + size_t(c)]));
                ++cnt_u_pyr;
            }
            const double mae_u_pyr = (cnt_u_pyr > 0) ? (sum_u_pyr / double(cnt_u_pyr)) : 1e9;

            auto eval_int_cand = [&](int dy, int dx) -> double {
                double acc = 0.0;
                int cnt = 0;
                for (size_t pi = 0; pi < unal_pts.size(); pi += p_step) {
                    const auto [r, c] = unal_pts[pi];
                    const int ry = r + dy, cx = c + dx;
                    if (ry < 2 || ry + 2 >= H1 || cx < 2 || cx + 2 >= W1) continue;
                    if (env_hi_n[size_t(ry) * size_t(W1) + size_t(cx)] > thr_hi_sym) continue;
                    acc += std::abs(double(smn_1x[size_t(ry) * size_t(W1) + size_t(cx)] - sm0_1x[size_t(r) * size_t(W1) + size_t(c)]));
                    ++cnt;
                }
                return (cnt >= 160) ? (acc / double(cnt)) : 1e30;
            };
            double best_cm = 1e30;
            int best_dy = 0, best_dx = 0;
            for (int dy = -4; dy <= 4; ++dy) {
                for (int dx = -4; dx <= 4; ++dx) {
                    const double m = eval_int_cand(dy, dx);
                    if (m < best_cm) { best_cm = m; best_dy = dy; best_dx = dx; }
                }
            }
            if (std::abs(med_dy_i) > 4 || std::abs(med_dx_i) > 4) {
                for (int dy = med_dy_i - 3; dy <= med_dy_i + 3; ++dy) {
                    for (int dx = med_dx_i - 3; dx <= med_dx_i + 3; ++dx) {
                        const double m = eval_int_cand(dy, dx);
                        if (m < best_cm) { best_cm = m; best_dy = dy; best_dx = dx; }
                    }
                }
            }
            if (best_cm < 1e20) {
                double dx_f = double(best_dx), dy_f = double(best_dy);
                for (int it = 0; it < 15; ++it) {
                    double Jxx = 0.0, Jxy = 0.0, Jyy = 0.0, bx = 0.0, by = 0.0;
                    for (size_t pi = 0; pi < unal_pts.size(); pi += p_step) {
                        const auto [r, c] = unal_pts[pi];
                        const float wy = float(double(r) + dy_f), wx = float(double(c) + dx_f);
                        if (wy < 2.0f || wy >= float(H1 - 3) || wx < 2.0f || wx >= float(W1 - 3)) continue;
                        if (env_hi_n[size_t(std::lround(wy)) * size_t(W1) + size_t(std::lround(wx))] > thr_hi_sym) continue;
                        const size_t i = size_t(r) * size_t(W1) + size_t(c);
                        const double res = detail::bicubic_clamped(smn_1x.data(), H1, W1, wy, wx) - double(sm0_1x[i]);
                        const double wt = 1.0 / std::max(1e-4, std::abs(res) + 0.002);
                        const double gx = double(gx0_1x[i]), gy = double(gy0_1x[i]);
                        Jxx += wt * gx * gx; Jxy += wt * gx * gy; Jyy += wt * gy * gy;
                        bx -= wt * gx * res; by -= wt * gy * res;
                    }
                    const double det = Jxx * Jyy - Jxy * Jxy;
                    if (det <= 1e-16) break;
                    const double ddx = std::clamp((Jyy * bx - Jxy * by) / det, -0.5, 0.5);
                    const double ddy = std::clamp((Jxx * by - Jxy * bx) / det, -0.5, 0.5);
                    dx_f += ddx; dy_f += ddy;
                    if (std::hypot(ddx, ddy) < 1e-5) break;
                }
                std::vector<float> res_u;
                res_u.reserve(unal_pts.size() / p_step + 1u);
                double sum_u = 0.0;
                for (size_t pi = 0; pi < unal_pts.size(); pi += p_step) {
                    const auto [r, c] = unal_pts[pi];
                    const float wy = float(double(r) + dy_f), wx = float(double(c) + dx_f);
                    if (wy < 2.0f || wy >= float(H1 - 2) || wx < 2.0f || wx >= float(W1 - 2)) continue;
                    if (env_hi_n[size_t(std::lround(wy)) * size_t(W1) + size_t(std::lround(wx))] > thr_hi_sym) continue;
                    const float df = float(std::abs(detail::bicubic_clamped(smn_1x.data(), H1, W1, wy, wx) - double(sm0_1x[size_t(r) * size_t(W1) + size_t(c)])));
                    res_u.push_back(df);
                    sum_u += double(df);
                }
                if (res_u.size() >= 160u) {
                    const double mae_u = sum_u / double(res_u.size());
                    const float med_u = pct_vec(res_u, 0.50f);
                    const float p98_u = pct_vec(res_u, 0.98f);
                    if (p98_u <= std::max({0.009f, 4.5f * sig0_1px, 4.2f * med_u * 1.4826f}) &&
                        mae_u <= 1.05 * mae_u_pyr + 0.35 * double(sig0_1px) + 2e-4) {
                        std::fill(flows_fx[size_t(n)].begin(), flows_fx[size_t(n)].end(), float(0.5 * dx_f));
                        std::fill(flows_fy[size_t(n)].begin(), flows_fy[size_t(n)].end(), float(0.5 * dy_f));
                        continue;
                    }
                }
            }
        }

        if (bril_ref_active && H1 >= 56 && W1 >= 56) {
            build_wb_1x(n, wbn_1x);
            build_padded_fft(wbn_1x, Fn_bril);
            struct CSpec { double mag, kx, ky, phi; };
            std::vector<CSpec> cs;
            cs.reserve(size_t(PY) * size_t(PX / 2));
            for (int r = 0; r < PY; ++r) {
                const double fy = (r <= PY / 2) ? (double(r) / double(PY)) : (double(r - PY) / double(PY));
                if (std::abs(fy) > 0.48) continue;
                for (int c = 1; c < PX / 2; ++c) {
                    const double fx = double(c) / double(PX);
                    if (fx <= 0.01 || fx > 0.48) continue;
                    if (std::hypot(fy * double(PY), fx * double(PX)) < 2.0) continue;
                    const size_t k = size_t(r) * size_t(PX) + size_t(c);
                    const auto prod = F0_bril[k] * std::conj(Fn_bril[k]);
                    cs.push_back({std::abs(prod), fx, fy, std::arg(prod)});
                }
            }
            if (cs.size() >= 96u) {
                std::nth_element(cs.begin(), cs.end() - 96, cs.end(),
                                 [](const CSpec& a, const CSpec& b) { return a.mag < b.mag; });
                constexpr int KB = 96;
                std::array<double, KB> kx_a{}, ky_a{}, phi_a{}, wt_a{};
                std::array<int, KB> sec_a{};
                std::array<double, 8> sec_w{};
                double raw_ws = 0.0, w_s = 0.0, kr_s = 0.0, Jxx = 0.0, Jxy = 0.0, Jyy = 0.0;
                for (int i = 0; i < KB; ++i) {
                    const auto& b = cs[cs.size() - size_t(KB) + size_t(i)];
                    kx_a[size_t(i)] = b.kx;
                    ky_a[size_t(i)] = b.ky;
                    phi_a[size_t(i)] = b.phi;
                    wt_a[size_t(i)] = b.mag;
                    raw_ws += b.mag;
                    const double ang = std::atan2(b.ky, b.kx);
                    const int sb = std::clamp(int((ang + 0.5 * std::numbers::pi_v<double>) / std::numbers::pi_v<double> * 8.0), 0, 7);
                    sec_a[size_t(i)] = sb;
                    sec_w[size_t(sb)] += b.mag;
                }
                if (raw_ws > 1e-20) {
                    for (int i = 0; i < KB; ++i) {
                        const double sw = sec_w[size_t(sec_a[size_t(i)])];
                        const double bal = (sw > 1e-20) ? std::clamp((raw_ws / 8.0) / sw, 0.25, 4.0) : 1.0;
                        wt_a[size_t(i)] *= bal;
                        w_s += wt_a[size_t(i)];
                    }
                    for (int i = 0; i < KB; ++i) {
                        wt_a[size_t(i)] /= w_s;
                        const double w = wt_a[size_t(i)];
                        kr_s += w * std::hypot(kx_a[size_t(i)], ky_a[size_t(i)]);
                        Jxx += w * kx_a[size_t(i)] * kx_a[size_t(i)];
                        Jxy += w * kx_a[size_t(i)] * ky_a[size_t(i)];
                        Jyy += w * ky_a[size_t(i)] * ky_a[size_t(i)];
                    }
                    const double tr = Jxx + Jyy;
                    const double disc = std::hypot(Jxx - Jyy, 2.0 * Jxy);
                    const double lam_min = 0.5 * (tr - disc);
                    const double lam_max = 0.5 * (tr + disc);
                    if (kr_s >= 0.27 && lam_min >= 0.0015 * lam_max) {
                        const int n_modes = (kr_s >= 0.33) ? 4 : 1;
                        std::array<std::array<double, KB>, 4> qx_m{}, qy_m{};
                        const double pens[4] = {0.0, 0.01, 0.01, 0.02};
                        for (int i = 0; i < KB; ++i) {
                            const double kx = kx_a[size_t(i)], ky = ky_a[size_t(i)];
                            const double kr = std::hypot(kx, ky);
                            const bool hx = (kr >= 0.26 && kx >= 0.24);
                            const bool hy = (kr >= 0.26 && std::abs(ky) >= 0.24);
                            const double sy = (ky >= 0.0) ? 1.0 : -1.0;
                            qx_m[0][size_t(i)] = kx;              qy_m[0][size_t(i)] = ky;
                            qx_m[1][size_t(i)] = hx ? kx - 1 : kx; qy_m[1][size_t(i)] = ky;
                            qx_m[2][size_t(i)] = kx;              qy_m[2][size_t(i)] = hy ? ky - sy : ky;
                            qx_m[3][size_t(i)] = hx ? kx - 1 : kx; qy_m[3][size_t(i)] = hy ? ky - sy : ky;
                        }

                        L10_n.resize(size_t(H1) * size_t(W1));
                        detail::sep_gauss_2d_clamped(wbn_1x.data(), L10_n.data(), H1, W1, 10.5f);

                        auto eval_mode_score = [&](int mi, double dx, double dy) noexcept -> double {
                            double acc = 0.0;
                            for (int i = 0; i < KB; ++i) {
                                const double ang = 2.0 * std::numbers::pi_v<double> *
                                    (dx * qx_m[size_t(mi)][size_t(i)] + dy * qy_m[size_t(mi)][size_t(i)]) - phi_a[size_t(i)];
                                acc += wt_a[size_t(i)] * std::cos(ang);
                            }
                            return acc - pens[mi];
                        };

                        constexpr int G_SIDE = 57; // [-5.6 .. +5.6] step 0.20
                        std::array<double, G_SIDE * G_SIDE> sc_grid{};
                        struct BrilPeak { double bs, dx, dy, e10; };
                        std::vector<BrilPeak> peaks;

                        const int m10 = std::min(26, std::min(H1, W1) / 4);
                        const int s10_r = std::max(2, (H1 - 2 * m10) / 64);
                        const int s10_c = std::max(2, (W1 - 2 * m10) / 64);

                        for (int mi = 0; mi < n_modes; ++mi) {
                            double max_g = -1e9;
                            for (int iy = 0; iy < G_SIDE; ++iy) {
                                const double dy = -5.6 + 0.20 * double(iy);
                                for (int ix = 0; ix < G_SIDE; ++ix) {
                                    const double dx = -5.6 + 0.20 * double(ix);
                                    const double v = eval_mode_score(mi, dx, dy);
                                    sc_grid[size_t(iy * G_SIDE + ix)] = v;
                                    max_g = std::max(max_g, v);
                                }
                            }
                            struct LocMax { double v; int iy, ix; };
                            std::vector<LocMax> lms;
                            for (int iy = 2; iy + 2 < G_SIDE; ++iy) {
                                for (int ix = 2; ix + 2 < G_SIDE; ++ix) {
                                    const double v = sc_grid[size_t(iy * G_SIDE + ix)];
                                    if (v < max_g - 0.20) continue;
                                    bool ok_m = true;
                                    for (int dy = -2; dy <= 2 && ok_m; ++dy) {
                                        for (int dx = -2; dx <= 2; ++dx) {
                                            if (sc_grid[size_t((iy + dy) * G_SIDE + (ix + dx))] > v + 1e-12) { ok_m = false; break; }
                                        }
                                    }
                                    if (ok_m) lms.push_back({v, iy, ix});
                                }
                            }
                            std::sort(lms.begin(), lms.end(), [](const LocMax& a, const LocMax& b) { return a.v > b.v; });
                            const size_t n_keep = std::min<size_t>(lms.size(), 8u);
                            for (size_t li = 0; li < n_keep; ++li) {
                                double bx = -5.6 + 0.20 * double(lms[li].ix);
                                double by = -5.6 + 0.20 * double(lms[li].iy);
                                double bs = lms[li].v;
                                double step = 0.06;
                                for (int z = 0; z < 5; ++z) {
                                    double best_x = bx, best_y = by;
                                    for (int dy = -2; dy <= 2; ++dy) {
                                        for (int dx = -2; dx <= 2; ++dx) {
                                            const double tx = bx + double(dx) * step;
                                            const double ty = by + double(dy) * step;
                                            const double sv = eval_mode_score(mi, tx, ty);
                                            if (sv > bs) { bs = sv; best_x = tx; best_y = ty; }
                                        }
                                    }
                                    bx = best_x; by = best_y;
                                    step *= 0.38;
                                }
                                double s_e10 = 0.0;
                                int cnt_10 = 0;
                                for (int r = m10; r < H1 - m10; r += s10_r) {
                                    for (int c = m10; c < W1 - m10; c += s10_c) {
                                        const float v0 = L10_0[size_t(r) * size_t(W1) + size_t(c)];
                                        const float vn = detail::bilerp_clamped(L10_n.data(), H1, W1, float(double(r) + by), float(double(c) + bx));
                                        s_e10 += std::abs(double(vn - v0));
                                        ++cnt_10;
                                    }
                                }
                                if (cnt_10 > 0) peaks.push_back({bs, bx, by, s_e10 / double(cnt_10)});
                            }
                        }

                        if (!peaks.empty()) {
                            double max_bs = -1e9;
                            for (const auto& p : peaks) max_bs = std::max(max_bs, p.bs);
                            if (max_bs >= 0.86) {
                                const BrilPeak* best_p = nullptr;
                                for (const auto& p : peaks) {
                                    if (p.bs >= max_bs - 0.15) {
                                        if (!best_p || p.e10 < best_p->e10) best_p = &p;
                                    }
                                }
                                if (best_p) {
                                    // Verify on macroscopic L10 that rigid (bx, by) does not lose to local flow (parallax/rotation guard!)
                                    std::vector<float> r10_rig;
                                    double sum10_pyr = 0.0;
                                    int cnt10_p = 0;
                                    for (int r = m10; r < H1 - m10; r += s10_r) {
                                        for (int c = m10; c < W1 - m10; c += s10_c) {
                                            const float v0 = L10_0[size_t(r) * size_t(W1) + size_t(c)];
                                            const float v_rig = detail::bilerp_clamped(L10_n.data(), H1, W1, float(double(r) + best_p->dy), float(double(c) + best_p->dx));
                                            r10_rig.push_back(std::abs(v_rig - v0));
                                            const size_t hi = size_t(detail::clamp_i(r / 2, 0, Hh - 1)) * size_t(Wh) + size_t(detail::clamp_i(c / 2, 0, Wh - 1));
                                            const float v_pyr = detail::bilerp_clamped(L10_n.data(), H1, W1,
                                                float(r) + 2.0f * flows_fy[size_t(n)][hi], float(c) + 2.0f * flows_fx[size_t(n)][hi]);
                                            sum10_pyr += std::abs(double(v_pyr - v0));
                                            ++cnt10_p;
                                        }
                                    }
                                    const float med_10 = pct_vec(r10_rig, 0.50f);
                                    const float p98_10 = pct_vec(r10_rig, 0.98f);
                                    const double mae10_pyr = (cnt10_p > 0) ? (sum10_pyr / double(cnt10_p)) : 1e9;
                                    if (p98_10 <= std::max({0.012f, 1.8f * sig0_1px, 4.8f * med_10 * 1.4826f}) &&
                                        best_p->e10 <= 1.20 * mae10_pyr + 0.30 * double(sig0_1px) + 0.0012) {
                                        std::fill(flows_fx[size_t(n)].begin(), flows_fx[size_t(n)].end(), float(0.5 * best_p->dx));
                                        std::fill(flows_fy[size_t(n)].begin(), flows_fy[size_t(n)].end(), float(0.5 * best_p->dy));
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (mc_sub_ready) {
            std::vector<std::complex<double>> C_rb(size_t(PY_h) * size_t(PX_h), {0.0, 0.0});
            for (int p = 0; p < 4; ++p) {
                extract_sublattice_wb(n, p, Sn_sub[size_t(p)]);
                L5_n_sub[size_t(p)].resize(size_t(Hh) * size_t(Wh));
                const float sig_lp = (cfa6x6[p >> 1][p & 1] == 1) ? 1.35f : 2.8f;
                detail::sep_gauss_2d_clamped(Sn_sub[size_t(p)].data(), L5_n_sub[size_t(p)].data(), Hh, Wh, sig_lp);
                if (cfa6x6[p >> 1][p & 1] != 1) {
                    build_sub_fft(Sn_sub[size_t(p)], Fn_sub_p);
                    const auto& F0p = F0_sub[size_t(p)];
                    for (size_t k = 0; k < C_rb.size(); ++k) {
                        C_rb[k] += F0p[k] * std::conj(Fn_sub_p[k]);
                    }
                }
            }
            build_gqx_fft(n, Fn_gqx);

            struct SubSpec { double mag, kx, ky, phi; bool is_rb; };
            std::vector<SubSpec> cs_rb, cs_g;
            cs_rb.reserve(size_t(PY_h) * size_t(PX_h / 2));
            cs_g.reserve(size_t(PY_q) * size_t(PX_q / 2));
            for (int r = 0; r < PY_h; ++r) {
                const double fy = (r <= PY_h / 2) ? (double(r) / double(PY_h)) : (double(r - PY_h) / double(PY_h));
                if (std::abs(fy) > 0.475) continue;
                for (int c = 1; c < PX_h / 2; ++c) {
                    const double fx = double(c) / double(PX_h);
                    if (fx <= 0.015 || fx > 0.475) continue;
                    const double kr = std::hypot(fx, fy);
                    if (kr > 0.485 || std::hypot(fy * double(PY_h), fx * double(PX_h)) < 2.0) continue;
                    const auto prod = C_rb[size_t(r) * size_t(PX_h) + size_t(c)];
                    cs_rb.push_back({std::abs(prod), fx, fy, std::arg(prod), true});
                }
            }
            for (int r = 0; r < PY_q; ++r) {
                const double fy_1x = (r <= PY_q / 2) ? (double(r) / double(PY_q)) : (double(r - PY_q) / double(PY_q));
                for (int c = 1; c < PX_q / 2; ++c) {
                    const double fx_1x = double(c) / double(PX_q);
                    if (fx_1x <= 0.008 || (fx_1x + std::abs(fy_1x)) > 0.455) continue;
                    if (std::hypot(fy_1x * double(PY_q), fx_1x * double(PX_q)) < 2.0) continue;
                    const auto prod_g = F0_gqx[size_t(r) * size_t(PX_q) + size_t(c)] *
                                        std::conj(Fn_gqx[size_t(r) * size_t(PX_q) + size_t(c)]);
                    cs_g.push_back({0.5 * std::abs(prod_g), 2.0 * fx_1x, 2.0 * fy_1x, std::arg(prod_g), false});
                }
            }

            constexpr int KBG = 64;
            std::vector<SubSpec> top_g;
            double top_g_ws = 0.0;
            if (cs_g.size() >= size_t(KBG)) {
                std::nth_element(cs_g.begin(), cs_g.end() - KBG, cs_g.end(),
                                 [](const SubSpec& a, const SubSpec& b) { return a.mag < b.mag; });
                top_g.assign(cs_g.end() - KBG, cs_g.end());
                for (const auto& b : top_g) top_g_ws += b.mag;
            }
            auto eval_gqx_coh = [&](double dxh, double dyh) noexcept -> double {
                if (top_g_ws <= 1e-20) return 1.0;
                double acc = 0.0;
                for (const auto& b : top_g) {
                    const double ang = 2.0 * std::numbers::pi_v<double> * (dxh * b.kx + dyh * b.ky) - b.phi;
                    acc += b.mag * std::cos(ang);
                }
                return acc / top_g_ws;
            };

            std::vector<SubSpec> cs_h;
            cs_h.reserve(cs_rb.size() + cs_g.size());
            cs_h.insert(cs_h.end(), cs_rb.begin(), cs_rb.end());
            cs_h.insert(cs_h.end(), cs_g.begin(), cs_g.end());

            constexpr int KBH = 96;
            if (cs_h.size() >= size_t(KBH) && !top_g.empty()) {
                std::nth_element(cs_h.begin(), cs_h.end() - KBH, cs_h.end(),
                                 [](const SubSpec& a, const SubSpec& b) { return a.mag < b.mag; });
                std::array<double, KBH> kx_a{}, ky_a{}, phi_a{}, wt_a{};
                std::array<bool, KBH> rb_a{};
                std::array<int, KBH> sec_a{};
                std::array<double, 8> sec_w{};
                double raw_ws = 0.0, w_s = 0.0, w_rb_s = 0.0, Jxx = 0.0, Jxy = 0.0, Jyy = 0.0;
                for (int i = 0; i < KBH; ++i) {
                    const auto& b = cs_h[cs_h.size() - size_t(KBH) + size_t(i)];
                    kx_a[size_t(i)] = b.kx;
                    ky_a[size_t(i)] = b.ky;
                    phi_a[size_t(i)] = b.phi;
                    wt_a[size_t(i)] = b.mag;
                    rb_a[size_t(i)] = b.is_rb;
                    raw_ws += b.mag;
                    const double ang = std::atan2(b.ky, b.kx);
                    const int sb = std::clamp(int((ang + 0.5 * std::numbers::pi_v<double>) / std::numbers::pi_v<double> * 8.0), 0, 7);
                    sec_a[size_t(i)] = sb;
                    sec_w[size_t(sb)] += b.mag;
                }
                if (raw_ws > 1e-20) {
                    for (int i = 0; i < KBH; ++i) {
                        const double sw = sec_w[size_t(sec_a[size_t(i)])];
                        const double bal = (sw > 1e-20) ? std::clamp((raw_ws / 8.0) / sw, 0.25, 4.0) : 1.0;
                        wt_a[size_t(i)] *= bal;
                        w_s += wt_a[size_t(i)];
                    }
                    for (int i = 0; i < KBH; ++i) {
                        wt_a[size_t(i)] /= w_s;
                        const double w = wt_a[size_t(i)];
                        if (rb_a[size_t(i)]) w_rb_s += w;
                        Jxx += w * kx_a[size_t(i)] * kx_a[size_t(i)];
                        Jxy += w * kx_a[size_t(i)] * ky_a[size_t(i)];
                        Jyy += w * ky_a[size_t(i)] * ky_a[size_t(i)];
                    }
                    const double tr = Jxx + Jyy;
                    const double disc = std::hypot(Jxx - Jyy, 2.0 * Jxy);
                    const double lam_min = 0.5 * (tr - disc);
                    const double lam_max = 0.5 * (tr + disc);
                    if (lam_min >= 0.010 * lam_max && lam_min > 1e-5) {
                        std::vector<float> fx_s, fy_s;
                        fx_s.reserve(256); fy_s.reserve(256);
                        const size_t st_f = std::max<size_t>(1u, flows_fx[size_t(n)].size() / 256u);
                        for (size_t i = 0; i < flows_fx[size_t(n)].size(); i += st_f) {
                            fx_s.push_back(flows_fx[size_t(n)][i]);
                            fy_s.push_back(flows_fy[size_t(n)][i]);
                        }
                        const double med_dx_h = fx_s.empty() ? 0.0 : double(pct_vec(fx_s, 0.50f));
                        const double med_dy_h = fy_s.empty() ? 0.0 : double(pct_vec(fy_s, 0.50f));
                        const double coh_g_pyr = eval_gqx_coh(med_dx_h, med_dy_h);

                        auto eval_coh = [&](double dxh, double dyh) noexcept -> double {
                            double acc = 0.0;
                            for (int i = 0; i < KBH; ++i) {
                                const double ang = 2.0 * std::numbers::pi_v<double> *
                                    (dxh * kx_a[size_t(i)] + dyh * ky_a[size_t(i)]) - phi_a[size_t(i)];
                                acc += wt_a[size_t(i)] * std::cos(ang);
                            }
                            return acc;
                        };
                        auto eval_rb_coh = [&](double dxh, double dyh) noexcept -> double {
                            if (w_rb_s <= 1e-12) return 1.0;
                            double acc = 0.0;
                            for (int i = 0; i < KBH; ++i) {
                                if (!rb_a[size_t(i)]) continue;
                                const double ang = 2.0 * std::numbers::pi_v<double> *
                                    (dxh * kx_a[size_t(i)] + dyh * ky_a[size_t(i)]) - phi_a[size_t(i)];
                                acc += wt_a[size_t(i)] * std::cos(ang);
                            }
                            return acc / w_rb_s;
                        };
                        auto newton_refine = [&](double& dxh, double& dyh) noexcept -> double {
                            constexpr double k2Pi = 2.0 * std::numbers::pi_v<double>;
                            constexpr double k4Pi2 = k2Pi * k2Pi;
                            for (int it = 0; it < 8; ++it) {
                                double gx = 0.0, gy = 0.0, hxx = 0.0, hxy = 0.0, hyy = 0.0;
                                for (int i = 0; i < KBH; ++i) {
                                    const double kx = kx_a[size_t(i)], ky = ky_a[size_t(i)], w = wt_a[size_t(i)];
                                    const double ang = k2Pi * (dxh * kx + dyh * ky) - phi_a[size_t(i)];
                                    const double sn = std::sin(ang);
                                    const double cs_pos = std::max(0.15, std::cos(ang));
                                    gx -= k2Pi * w * sn * kx;
                                    gy -= k2Pi * w * sn * ky;
                                    hxx += k4Pi2 * w * cs_pos * kx * kx;
                                    hxy += k4Pi2 * w * cs_pos * kx * ky;
                                    hyy += k4Pi2 * w * cs_pos * ky * ky;
                                }
                                const double det = hxx * hyy - hxy * hxy;
                                if (det <= 1e-18) break;
                                const double sx = std::clamp((hyy * gx - hxy * gy) / det, -0.25, 0.25);
                                const double sy = std::clamp((hxx * gy - hxy * gx) / det, -0.25, 0.25);
                                dxh += sx;
                                dyh += sy;
                                if (std::hypot(sx, sy) < 1e-6) break;
                            }
                            return eval_coh(dxh, dyh);
                        };

                        constexpr int GH_SIDE = 49; // [-3.0 .. +3.0] step 0.125 half-px
                        std::array<double, GH_SIDE * GH_SIDE> sc_h{};
                        struct SubPeak { double coh, coh_g, coh_rb, dxh, dyh, e5; };
                        std::vector<SubPeak> peaks_h;

                        const int m5 = std::max(14, std::min(20, std::min(Hh, Wh) / 4));
                        const int s5_r = std::max(1, (Hh - 2 * m5) / 40);
                        const int s5_c = std::max(1, (Wh - 2 * m5) / 40);
                        auto collect_peaks_around = [&](double cx0, double cy0) {
                            double max_g = -1e9;
                            for (int iy = 0; iy < GH_SIDE; ++iy) {
                                const double dy = cy0 - 3.0 + 0.125 * double(iy);
                                for (int ix = 0; ix < GH_SIDE; ++ix) {
                                    const double dx = cx0 - 3.0 + 0.125 * double(ix);
                                    const double v = eval_coh(dx, dy);
                                    sc_h[size_t(iy * GH_SIDE + ix)] = v;
                                    max_g = std::max(max_g, v);
                                }
                            }
                            struct LMax { double v; int iy, ix; };
                            std::vector<LMax> lms;
                            for (int iy = 1; iy + 1 < GH_SIDE; ++iy) {
                                for (int ix = 1; ix + 1 < GH_SIDE; ++ix) {
                                    const double v = sc_h[size_t(iy * GH_SIDE + ix)];
                                    if (v < max_g - 0.16) continue;
                                    bool ok = true;
                                    for (int dy = -1; dy <= 1 && ok; ++dy) {
                                        for (int dx = -1; dx <= 1; ++dx) {
                                            if (sc_h[size_t((iy + dy) * GH_SIDE + (ix + dx))] > v + 1e-12) { ok = false; break; }
                                        }
                                    }
                                    if (ok) lms.push_back({v, iy, ix});
                                }
                            }
                            std::sort(lms.begin(), lms.end(), [](const LMax& a, const LMax& b) { return a.v > b.v; });
                            const size_t nk = std::min<size_t>(lms.size(), 6u);
                            for (size_t li = 0; li < nk; ++li) {
                                double bx = cx0 - 3.0 + 0.125 * double(lms[li].ix);
                                double by = cy0 - 3.0 + 0.125 * double(lms[li].iy);
                                const double coh = newton_refine(bx, by);
                                const double cg = eval_gqx_coh(bx, by);
                                const double crb = eval_rb_coh(bx, by);
                                // Phase-closure check: unaliased R/B carriers MUST agree simultaneously with alias-free 1x Green quincunx!
                                if (cg < 0.90 || crb < 0.88 || cg < coh_g_pyr - 0.006) continue;
                                double s_e5 = 0.0;
                                int cnt_5 = 0;
                                for (int r = m5; r < Hh - m5; r += s5_r) {
                                    for (int c = m5; c < Wh - m5; c += s5_c) {
                                        for (int gp = 0; gp < 4; ++gp) {
                                            if (cfa6x6[gp >> 1][gp & 1] != 1) continue;
                                            const float v0 = L5_0_sub[size_t(gp)][size_t(r) * size_t(Wh) + size_t(c)];
                                            const float vn = detail::bilerp_clamped(L5_n_sub[size_t(gp)].data(), Hh, Wh, float(double(r) + by), float(double(c) + bx));
                                            s_e5 += std::abs(double(vn - v0));
                                            ++cnt_5;
                                        }
                                    }
                                }
                                if (cnt_5 > 0) peaks_h.push_back({coh, cg, crb, bx, by, s_e5 / double(cnt_5)});
                            }
                        };
                        collect_peaks_around(0.0, 0.0);
                        if (std::hypot(med_dx_h, med_dy_h) > 2.2) collect_peaks_around(med_dx_h, med_dy_h);

                        if (!peaks_h.empty()) {
                            double max_coh = -1e9;
                            for (const auto& pk : peaks_h) max_coh = std::max(max_coh, pk.coh);
                            if (max_coh >= 0.92) {
                                const SubPeak* best_pk = nullptr;
                                for (const auto& pk : peaks_h) {
                                    if (pk.coh >= max_coh - 0.06) {
                                        if (!best_pk || pk.e5 < best_pk->e5) best_pk = &pk;
                                    }
                                }
                                if (best_pk) {
                                    const double fin_dxh = best_pk->dxh, fin_dyh = best_pk->dyh;
                                    std::vector<float> r5_rig;
                                    double sum5_pyr = 0.0, sum5_rig = 0.0;
                                    int cnt5_p = 0;
                                    for (int r = m5; r < Hh - m5; r += s5_r) {
                                        for (int c = m5; c < Wh - m5; c += s5_c) {
                                            const size_t hi = size_t(r) * size_t(Wh) + size_t(c);
                                            const float py = float(r) + flows_fy[size_t(n)][hi];
                                            const float px = float(c) + flows_fx[size_t(n)][hi];
                                            float cell_rig = 0.0f;
                                            for (int gp = 0; gp < 4; ++gp) {
                                                if (cfa6x6[gp >> 1][gp & 1] != 1) continue;
                                                const float v0 = L5_0_sub[size_t(gp)][hi];
                                                const float vr = detail::bilerp_clamped(L5_n_sub[size_t(gp)].data(), Hh, Wh, float(double(r) + fin_dyh), float(double(c) + fin_dxh));
                                                const float vp = detail::bilerp_clamped(L5_n_sub[size_t(gp)].data(), Hh, Wh, py, px);
                                                const float ar = std::abs(vr - v0);
                                                cell_rig = std::max(cell_rig, ar);
                                                sum5_rig += double(ar);
                                                sum5_pyr += std::abs(double(vp - v0));
                                                ++cnt5_p;
                                            }
                                            r5_rig.push_back(cell_rig);
                                        }
                                    }
                                    const float med_5 = pct_vec(r5_rig, 0.50f);
                                    const float p98_5 = pct_vec(r5_rig, 0.98f);
                                    const double mae5_rig = (cnt5_p > 0) ? (sum5_rig / double(cnt5_p)) : 1e9;
                                    const double mae5_pyr = (cnt5_p > 0) ? (sum5_pyr / double(cnt5_p)) : 1e9;
                                    if (p98_5 <= std::max({0.016f, 2.5f * sig0_1px, 5.2f * med_5 * 1.4826f}) &&
                                        mae5_rig <= 1.02 * mae5_pyr + 0.20 * double(sig0_1px) + 5e-5) {
                                        std::fill(flows_fx[size_t(n)].begin(), flows_fx[size_t(n)].end(), float(fin_dxh));
                                        std::fill(flows_fy[size_t(n)].begin(), flows_fy[size_t(n)].end(), float(fin_dyh));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace heraw_s4c
