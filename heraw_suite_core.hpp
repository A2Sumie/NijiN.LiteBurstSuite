// Shared CPU/GPU camera-linear Suite stages. C++17, header-only.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif
#include "heraw_cpu_cache.hpp"

namespace heraw_core {

using Frames = std::vector<std::vector<float>>;
using RangeTask = std::function<void(int, int)>;
// The callback must execute each index exactly once and join before returning.
struct Execution {
    int tile = 64;
    std::function<void(int, const RangeTask&)> parallel;
    int diagnostic_stride = 1; // Forward-error telemetry only; updates use all samples.
    std::function<bool(const float*,Frames&)> sensor_residual;
};
struct Noise {
    float read_variance = 1e-6f;
    float shot = 0.0f;
    // Optional N-element arrays after exposure normalization. Caller owns them.
    const float* frame_read_variances = nullptr;
    const float* frame_shot_coefficients = nullptr;
    const float* frame_saturation_levels = nullptr;
    float variance(float signal) const {
        return std::max(1e-12f, read_variance + shot * std::max(0.0f, signal));
    }
    float frame_variance(std::size_t n, float signal) const {
        const float read = frame_read_variances ? frame_read_variances[n] : read_variance;
        const float gain = frame_shot_coefficients ? frame_shot_coefficients[n] : shot;
        return std::max(1e-12f, read + gain * std::max(0.0f, signal));
    }
    bool unsaturated(std::size_t n, float signal) const {
        return !frame_saturation_levels || signal < frame_saturation_levels[n];
    }
};
struct ExposureStats {
    std::vector<float> gains; // Multiplier applied to each source, reference = 1.
    std::vector<float> variance_scales;
    std::vector<float> read_variances, shot_coefficients, saturation_levels;
    std::size_t samples = 0;
};
struct StageStats {
    std::size_t samples = 0, changed = 0, rejected = 0;
    double before = 0.0, after = 0.0, rms_update = 0.0;
    double noise_variance = 0.0, signal_power = 0.0;
    std::size_t peak_scratch_bytes = 0; // Per worker, excludes caller buffers.
    int iterations = 0;
    std::size_t candidate_samples = 0, index_bytes = 0;
    // C6: fill/transform/filter worker sums. C1: index/diagnostics/update wall time.
    std::array<double,3> worker_seconds{};
};
struct HalfStackOptions {
    float strength = 1.0f;
    float effective_frames = 0.0f;
    bool phase_balanced = false;
    // Optional H2*W2 static/effective-count maps, in reference coordinates.
    const float* effective_count = nullptr;
    const float* static_weight = nullptr;
    // Optional 16-bin power response of the reconstruction kernel, DC first.
    const float* kernel_power = nullptr;
    const float* fusion_eta=nullptr;
    int fusion_eta_h=0,fusion_eta_w=0;
};
struct SpatialPsfZone {
    // Normalized image coordinates [0,1], sigma in RGB2x pixels, radians.
    float cx = .5f, cy = .5f;
    float sigma_major_2x = 0, sigma_minor_2x = 0, angle = 0;
    bool valid = false;
};

// Power transfer in the native 4x4 Walsh order, DC=1. Covariance units
// are squared output pixels. The inverse-covariance overload converts
// reconstruction precision (usually in RAW pixels) using pixel_scale=2.
std::array<float, 16> kernel_power_from_covariance(float cov_xx, float cov_xy,
                                                  float cov_yy,
                                                  float minimum_power = .02f);
std::array<float, 16> kernel_power_from_inverse_covariance(float inv_xx,
                                                          float inv_xy,
                                                          float inv_yy,
                                                          float pixel_scale = 2.0f,
                                                          float minimum_power = .02f);
// Compact measured Gaussian PSF, support ceil(3*sigma) capped at 12 pixels.
// Noise is the effective noise of rgb2x. Invalid/unmeasured zones are skipped.
// Additive matched residual descent preserves signed camera-linear values.
StageStats spatial_psf_deconvolve(float* rgb2x, int H2, int W2,
                                 const std::vector<SpatialPsfZone>& zones,
                                 const Noise& noise, int iterations = 1,
                                 float strength = .7f,
                                 const Execution& execution = {});

// H1/W1 are even RAW dimensions; RGB2x is interleaved camera-linear RGB,
// H2=2*H1, W2=2*W1. Signed samples are retained by all stages.
// fy/fx/mw have N planes of (H1/2)*(W1/2), or are empty (identity/unit).
// Flow is reference -> source, in half-resolution pixels. A RAW coordinate
// (r,c) reads flow at (r/2,c/2), and source=(r,c)+2*(fy,fx).
// CFA is the sensor-origin 6x6 repeating table, R=0 G=1 B=2.
ExposureStats exposure_normalize(Frames& raw, Frames& g_halfs, int H1, int W1,
                                const Noise& noise = {}, const Frames& fy = {},
                                const Frames& fx = {}, const Frames& mw = {},
                                const Execution& execution = {});
StageStats silicon_residual_cleanup(Frames& raw, int H1, int W1,
                                   const uint8_t* cfa6x6, const Frames& fy,
                                   const Frames& fx, const Frames& mw, const Noise& noise,
                                   const float* scene_rgb2x = nullptr,
                                   const Execution& execution = {});
StageStats row_rolling_shutter_smooth(Frames& fy, Frames& fx, const Frames& mw,
                                    int Hh, int Wh, float strength = 0.5f,
                                    const Execution& execution = {});
// Four interleaved directions, or one caller-selected direction per pixel.
StageStats directional_lucky_weights(const Frames& g_halfs, int Hh, int Wh,
                                     const Frames& fy, const Frames& fx,
                                     const Noise& noise, Frames& weights,
                                      const Execution& execution = {},
                                      const uint8_t* selected_direction = nullptr);
StageStats compact_ibp(const Frames& raw, int H1, int W1,
                       const uint8_t* cfa6x6, const Frames& fy,
                       const Frames& fx, const Frames& mw, const Noise& noise,
                       float* rgb2x, int iterations = 1,
                       const Execution& execution = {});
StageStats color_line_refine(float* rgb2x, int H2, int W2, const Noise& noise,
                            float strength = 0.25f,
                            const Execution& execution = {});
// Caller can provide independent reconstructed halves, or use the RAW overload
// below to reconstruct small half-stack tiles without N full RGB canvases.
StageStats half_stack_denoise(float* rgb2x, const float* half_a,
                             const float* half_b, int H2, int W2, const Noise& noise,
                             const HalfStackOptions& options = {},
                             const Execution& execution = {});
StageStats half_stack_denoise(const Frames& raw, int H1, int W1,
                             const uint8_t* cfa6x6, const Frames& fy,
                             const Frames& fx, const Frames& mw, const Noise& noise,
                             float* rgb2x, const HalfStackOptions& options = {},
                             const Execution& execution = {});

namespace detail {
inline int clamp(int x, int n) { return std::max(0, std::min(n - 1, x)); }
inline float clamp01(float x) { return std::max(0.0f, std::min(1.0f, x)); }
inline void run(int n, const Execution& e, const RangeTask& f) {
    if (e.parallel) e.parallel(n, f); else f(0, n);
}
inline void shape(int h, int w) {
    if (h < 1 || w < 1 || h > (1 << 25) || w > (1 << 25))
        throw std::invalid_argument("invalid image dimensions");
}
inline void planes(const Frames& p, std::size_t n, std::size_t size,
                   bool optional = true) {
    if (optional && p.empty()) return;
    if (p.size() != n) throw std::invalid_argument("plane count mismatch");
    for (const auto& v : p)
        if (v.size() != size) throw std::invalid_argument("plane size mismatch");
}
inline void validate(const Frames& raw, int h, int w, const uint8_t* cfa,
                     const Frames& fy, const Frames& fx, const Frames& mw) {
    shape(h, w);
    if ((h & 1) || (w & 1) || raw.empty())
        throw std::invalid_argument("RAW dimensions must be even and stack nonempty");
    planes(raw, raw.size(), std::size_t(h) * w, false);
    const auto half = std::size_t(h / 2) * (w / 2);
    planes(fy, raw.size(), half); planes(fx, raw.size(), half);
    planes(mw, raw.size(), half);
    if (fy.empty() != fx.empty()) throw std::invalid_argument("flow pair required");
    if (cfa) for (int k = 0; k < 36; ++k)
        if (cfa[k] > 2) throw std::invalid_argument("invalid CFA color");
}
inline float sample(const float* a, int h, int w, float y, float x,
                    int stride = 1, int channel = 0) {
    y = std::max(0.0f, std::min(float(h - 1), y));
    x = std::max(0.0f, std::min(float(w - 1), x));
    const int r = int(y), c = int(x), r1 = std::min(r + 1, h - 1),
              c1 = std::min(c + 1, w - 1);
    const float ty = y - r, tx = x - c;
    const float oty = 1.0f - ty, otx = 1.0f - tx;
    auto at = [&](int yy, int xx) { return a[(std::size_t(yy) * w + xx) * stride + channel]; };
    return std::fma(oty, std::fma(otx, at(r, c), tx * at(r, c1)),
                    ty * std::fma(otx, at(r1, c), tx * at(r1, c1)));
}
inline float field(const Frames& p, std::size_t n, int h, int w,
                   float y, float x, float fallback = 0.0f) {
    return p.empty() ? fallback : sample(p[n].data(), h, w, y, x);
}
inline bool inside(float y, float x, int h, int w) {
    return std::isfinite(y) && std::isfinite(x) && y >= 0 && x >= 0 &&
           y <= h - 1 && x <= w - 1;
}
inline float median(std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    auto m = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), m, v.end());
    return *m;
}
#if HERAW_AVX512_ENABLED
__attribute__((target("avx512f,avx512vl,avx512bw,avx512dq,avx512cd,fma")))
inline bool plane_all_finite_lt_avx512(const float* p, std::size_t n, float upper_exclusive) {
    std::size_t i = 0;
    const __m512 up_v = _mm512_set1_ps(upper_exclusive);
    __mmask16 ok_all = 0xFFFFu;
    for (; i + 16 <= n; i += 16) {
        const __m512 x = _mm512_loadu_ps(p + i);
        const __mmask16 non_fin = _mm512_fpclass_ps_mask(x, 0x99); // QNaN | +Inf | -Inf | SNaN
        const __mmask16 lt = _mm512_cmp_ps_mask(x, up_v, _CMP_LT_OQ);
        ok_all = __mmask16(ok_all & (~non_fin) & lt);
    }
    if (ok_all != 0xFFFFu) return false;
    for (; i < n; ++i) {
        if (!std::isfinite(p[i]) || !(p[i] < upper_exclusive)) return false;
    }
    return true;
}

__attribute__((target("avx512f,avx512vl,fma")))
inline void scale_ci_w_avx512(
    const float* __restrict ci, const float* __restrict sqrt_inv_hp,
    float* __restrict ci_w, std::size_t n_blocks
) {
    const __m512 w = _mm512_loadu_ps(sqrt_inv_hp);
    for (std::size_t bi = 0; bi < n_blocks; ++bi) {
        _mm512_storeu_ps(ci_w + bi * 16, _mm512_mul_ps(_mm512_loadu_ps(ci + bi * 48), w));
    }
}

__attribute__((target("avx512f,avx512vl,fma")))
inline void compute_hsum_row_avx512(const float* __restrict row_ci, float* __restrict dst_row, int bw) {
    for (int bx = 1; bx < bw - 1; ++bx) {
        const float* p0 = row_ci + std::size_t(bx - 1) * 48;
        const float* p1 = p0 + 48;
        const float* p2 = p1 + 48;
        float* d = dst_row + std::size_t(bx) * 48;
        for (int kf = 0; kf < 48; kf += 16) {
            const __m512 v0 = _mm512_loadu_ps(p0 + kf);
            const __m512 v1 = _mm512_loadu_ps(p1 + kf);
            const __m512 v2 = _mm512_loadu_ps(p2 + kf);
            _mm512_storeu_ps(d + kf, _mm512_fmadd_ps(v2, v2, _mm512_fmadd_ps(v1, v1, _mm512_mul_ps(v0, v0))));
        }
    }
}
#endif

inline bool plane_all_finite_lt(const std::vector<float>& v, float upper_exclusive) {
    const float* p = v.data();
    const std::size_t n = v.size();
#if HERAW_AVX512_ENABLED
    if (heraw_cache::use_avx512()) {
        return plane_all_finite_lt_avx512(p, n, upper_exclusive);
    }
#endif
    std::size_t i = 0;
#if defined(__aarch64__)
    const float32x4_t up_v = vdupq_n_f32(upper_exclusive);
    const uint32x4_t exp_mask = vdupq_n_u32(0x7F800000u);
    uint32x4_t ok_all = vdupq_n_u32(0xFFFFFFFFu);
    for (; i + 4 <= n; i += 4) {
        const float32x4_t x = vld1q_f32(p + i);
        const uint32x4_t fin = vmvnq_u32(vceqq_u32(vandq_u32(vreinterpretq_u32_f32(x), exp_mask), exp_mask));
        ok_all = vandq_u32(ok_all, vandq_u32(fin, vcltq_f32(x, up_v)));
    }
    if (vminvq_u32(ok_all) == 0) return false;
#elif defined(__AVX2__)
    const __m256 up_v = _mm256_set1_ps(upper_exclusive);
    const __m256i exp_mask = _mm256_set1_epi32(0x7F800000);
    __m256 ok_all = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
    for (; i + 8 <= n; i += 8) {
        const __m256 x = _mm256_loadu_ps(p + i);
        const __m256i xi = _mm256_castps_si256(x);
        const __m256 fin = _mm256_castsi256_ps(_mm256_xor_si256(
            _mm256_cmpeq_epi32(_mm256_and_si256(xi, exp_mask), exp_mask), _mm256_set1_epi32(-1)));
        ok_all = _mm256_and_ps(ok_all, _mm256_and_ps(fin, _mm256_cmp_ps(x, up_v, _CMP_LT_OQ)));
    }
    if (_mm256_movemask_ps(ok_all) != 0xFF) return false;
#endif
    for (; i < n; ++i) {
        if (!std::isfinite(p[i]) || !(p[i] < upper_exclusive)) return false;
    }
    return true;
}

inline void inverse_flow(const Frames& fy, const Frames& fx, std::size_t n,
                         int h, int w, float sy, float sx, float& ry, float& rx) {
    ry = sy; rx = sx;
    if (fy.empty()) return;
    const float* py = fy[n].data();
    const float* px = fx[n].data();
    const float h_max = float(h - 1), w_max = float(w - 1);
    int prev_r = -1, prev_c = -1;
    float py00 = 0, py01 = 0, py10 = 0, py11 = 0;
    float px00 = 0, px01 = 0, px10 = 0, px11 = 0;
    for (int k = 0; k < 5; ++k) {
        const float y = std::max(0.0f, std::min(h_max, ry * .5f));
        const float x = std::max(0.0f, std::min(w_max, rx * .5f));
        const int r = int(y), c = int(x);
        if (r != prev_r || c != prev_c) {
            prev_r = r; prev_c = c;
            const int r1 = std::min(r + 1, h - 1), c1 = std::min(c + 1, w - 1);
            const std::size_t i00 = std::size_t(r) * w + c, i01 = std::size_t(r) * w + c1;
            const std::size_t i10 = std::size_t(r1) * w + c, i11 = std::size_t(r1) * w + c1;
            py00 = py[i00]; py01 = py[i01]; py10 = py[i10]; py11 = py[i11];
            px00 = px[i00]; px01 = px[i01]; px10 = px[i10]; px11 = px[i11];
        }
        const float ty = y - r, tx = x - c;
        const float oty = 1.0f - ty, otx = 1.0f - tx;
        const float dy = std::fma(oty, std::fma(otx, py00, tx * py01), ty * std::fma(otx, py10, tx * py11));
        const float dx = std::fma(oty, std::fma(otx, px00, tx * px01), ty * std::fma(otx, px10, tx * px11));
        const float next_y = std::fma(-2.0f, dy, sy), next_x = std::fma(-2.0f, dx, sx);
        const bool settled = std::fabs(next_y - ry) <= 1e-7f && std::fabs(next_x - rx) <= 1e-7f;
        ry = next_y; rx = next_x;
        if (settled) break;
    }
}

inline void inverse_flow4(const float* py, const float* px, int h, int w,
                          float sy, float sx0, float ry_out[4], float rx_out[4]) {
#if defined(__aarch64__)
    const float32x4_t zero_v = vdupq_n_f32(0.0f);
    const float32x4_t one_v = vdupq_n_f32(1.0f);
    const float32x4_t two_v = vdupq_n_f32(2.0f);
    const float32x4_t half_v = vdupq_n_f32(0.5f);
    const float32x4_t eps_v = vdupq_n_f32(1e-7f);
    const float32x4_t hmax_v = vdupq_n_f32(float(h - 1));
    const float32x4_t wmax_v = vdupq_n_f32(float(w - 1));
    const float32x4_t sy_v = vdupq_n_f32(sy);
    const float sx_init[4] = {sx0, sx0 + 1.0f, sx0 + 2.0f, sx0 + 3.0f};
    const float32x4_t sx_v = vld1q_f32(sx_init);
    float32x4_t ry_v = sy_v, rx_v = sx_v;
    uint32x4_t active = vdupq_n_u32(0xFFFFFFFFu);
    const uint8x16_t byte_seq = {0,1,2,3, 0,1,2,3, 0,1,2,3, 0,1,2,3};
    for (int k = 0; k < 5; ++k) {
        const float32x4_t y_v = vminq_f32(hmax_v, vmaxq_f32(zero_v, vmulq_f32(ry_v, half_v)));
        const float32x4_t x_v = vminq_f32(wmax_v, vmaxq_f32(zero_v, vmulq_f32(rx_v, half_v)));
        const int32x4_t r_i = vcvtq_s32_f32(y_v);
        const int32x4_t c_i = vcvtq_s32_f32(x_v);
        const float32x4_t ty = vsubq_f32(y_v, vcvtq_f32_s32(r_i));
        const float32x4_t tx = vsubq_f32(x_v, vcvtq_f32_s32(c_i));
        const float32x4_t oty = vsubq_f32(one_v, ty);
        const float32x4_t otx = vsubq_f32(one_v, tx);
        float32x4_t py00, py01, py10, py11, px00, px01, px10, px11;
        const int r_min = vminvq_s32(r_i), r_max = vmaxvq_s32(r_i);
        const int c_min = vminvq_s32(c_i), c_max = vmaxvq_s32(c_i);
        if (r_min == r_max && c_max <= c_min + 2 && c_min >= 0 && c_min + 3 < w) {
            const int r1 = std::min(r_min + 1, h - 1);
            const std::size_t off0 = std::size_t(r_min) * w + c_min;
            const std::size_t off1 = std::size_t(r1) * w + c_min;
            const uint32x4_t dc4 = vreinterpretq_u32_s32(vshlq_n_s32(vsubq_s32(c_i, vdupq_n_s32(c_min)), 2));
            const uint8x16_t idx0 = vaddq_u8(vreinterpretq_u8_u32(vmulq_n_u32(dc4, 0x01010101u)), byte_seq);
            const uint8x16_t idx1 = vaddq_u8(idx0, vdupq_n_u8(4));
            const uint8x16_t vy0 = vreinterpretq_u8_f32(vld1q_f32(py + off0));
            const uint8x16_t vy1 = vreinterpretq_u8_f32(vld1q_f32(py + off1));
            const uint8x16_t vx0 = vreinterpretq_u8_f32(vld1q_f32(px + off0));
            const uint8x16_t vx1 = vreinterpretq_u8_f32(vld1q_f32(px + off1));
            py00 = vreinterpretq_f32_u8(vqtbl1q_u8(vy0, idx0));
            py01 = vreinterpretq_f32_u8(vqtbl1q_u8(vy0, idx1));
            py10 = vreinterpretq_f32_u8(vqtbl1q_u8(vy1, idx0));
            py11 = vreinterpretq_f32_u8(vqtbl1q_u8(vy1, idx1));
            px00 = vreinterpretq_f32_u8(vqtbl1q_u8(vx0, idx0));
            px01 = vreinterpretq_f32_u8(vqtbl1q_u8(vx0, idx1));
            px10 = vreinterpretq_f32_u8(vqtbl1q_u8(vx1, idx0));
            px11 = vreinterpretq_f32_u8(vqtbl1q_u8(vx1, idx1));
        } else {
            int rl[4], cl[4];
            vst1q_s32(rl, r_i); vst1q_s32(cl, c_i);
            float ay00[4], ay01[4], ay10[4], ay11[4], ax00[4], ax01[4], ax10[4], ax11[4];
            for (int l = 0; l < 4; ++l) {
                const int r = rl[l], c = cl[l];
                const int r1 = std::min(r + 1, h - 1), c1 = std::min(c + 1, w - 1);
                const std::size_t i00 = std::size_t(r) * w + c, i01 = std::size_t(r) * w + c1;
                const std::size_t i10 = std::size_t(r1) * w + c, i11 = std::size_t(r1) * w + c1;
                ay00[l] = py[i00]; ay01[l] = py[i01]; ay10[l] = py[i10]; ay11[l] = py[i11];
                ax00[l] = px[i00]; ax01[l] = px[i01]; ax10[l] = px[i10]; ax11[l] = px[i11];
            }
            py00 = vld1q_f32(ay00); py01 = vld1q_f32(ay01); py10 = vld1q_f32(ay10); py11 = vld1q_f32(ay11);
            px00 = vld1q_f32(ax00); px01 = vld1q_f32(ax01); px10 = vld1q_f32(ax10); px11 = vld1q_f32(ax11);
        }
        const float32x4_t dy = vfmaq_f32(vmulq_f32(ty, vfmaq_f32(vmulq_f32(tx, py11), otx, py10)),
                                         oty, vfmaq_f32(vmulq_f32(tx, py01), otx, py00));
        const float32x4_t dx = vfmaq_f32(vmulq_f32(ty, vfmaq_f32(vmulq_f32(tx, px11), otx, px10)),
                                         oty, vfmaq_f32(vmulq_f32(tx, px01), otx, px00));
        const float32x4_t ny = vbslq_f32(active, vfmsq_f32(sy_v, two_v, dy), ry_v);
        const float32x4_t nx = vbslq_f32(active, vfmsq_f32(sx_v, two_v, dx), rx_v);
        const uint32x4_t moving = vcgtq_f32(vmaxq_f32(vabdq_f32(ny, ry_v), vabdq_f32(nx, rx_v)), eps_v);
        active = vandq_u32(active, moving);
        ry_v = ny; rx_v = nx;
        if (vmaxvq_u32(active) == 0) break;
    }
    vst1q_f32(ry_out, ry_v);
    vst1q_f32(rx_out, rx_v);
#elif defined(__AVX2__)
    const __m128 zero_v = _mm_setzero_ps();
    const __m128 one_v = _mm_set1_ps(1.0f);
    const __m128 two_v = _mm_set1_ps(2.0f);
    const __m128 half_v = _mm_set1_ps(0.5f);
    const __m128 hmax_v = _mm_set1_ps(float(h - 1));
    const __m128 wmax_v = _mm_set1_ps(float(w - 1));
    const __m128 sy_v = _mm_set1_ps(sy);
    const __m128 sx_v = _mm_setr_ps(sx0, sx0 + 1.0f, sx0 + 2.0f, sx0 + 3.0f);
    const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
    const __m128 eps_v = _mm_set1_ps(1e-7f);
    __m128 ry_v = sy_v, rx_v = sx_v;
    __m128 active = _mm_castsi128_ps(_mm_set1_epi32(-1));
    for (int k = 0; k < 5; ++k) {
        const __m128 y_v = _mm_min_ps(hmax_v, _mm_max_ps(zero_v, _mm_mul_ps(ry_v, half_v)));
        const __m128 x_v = _mm_min_ps(wmax_v, _mm_max_ps(zero_v, _mm_mul_ps(rx_v, half_v)));
        const __m128i r_i = _mm_cvttps_epi32(y_v);
        const __m128i c_i = _mm_cvttps_epi32(x_v);
        const __m128 ty = _mm_sub_ps(y_v, _mm_cvtepi32_ps(r_i));
        const __m128 tx = _mm_sub_ps(x_v, _mm_cvtepi32_ps(c_i));
        const __m128 oty = _mm_sub_ps(one_v, ty);
        const __m128 otx = _mm_sub_ps(one_v, tx);
        int rl[4], cl[4];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(rl), r_i);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(cl), c_i);
        __m128 py00, py01, py10, py11, px00, px01, px10, px11;
        const int r0 = rl[0], c_min = std::min({cl[0], cl[1], cl[2], cl[3]});
        const int c_max = std::max({cl[0], cl[1], cl[2], cl[3]});
        if (rl[1] == r0 && rl[2] == r0 && rl[3] == r0 && c_max <= c_min + 2 && c_min >= 0 && c_min + 3 < w) {
            const int r1 = std::min(r0 + 1, h - 1);
            const std::size_t off0 = std::size_t(r0) * w + c_min;
            const std::size_t off1 = std::size_t(r1) * w + c_min;
            const __m128i dc = _mm_sub_epi32(c_i, _mm_set1_epi32(c_min));
            const __m128i dc1 = _mm_add_epi32(dc, _mm_set1_epi32(1));
            const __m128 vy0 = _mm_loadu_ps(py + off0), vy1 = _mm_loadu_ps(py + off1);
            const __m128 vx0 = _mm_loadu_ps(px + off0), vx1 = _mm_loadu_ps(px + off1);
            py00 = _mm_permutevar_ps(vy0, dc); py01 = _mm_permutevar_ps(vy0, dc1);
            py10 = _mm_permutevar_ps(vy1, dc); py11 = _mm_permutevar_ps(vy1, dc1);
            px00 = _mm_permutevar_ps(vx0, dc); px01 = _mm_permutevar_ps(vx0, dc1);
            px10 = _mm_permutevar_ps(vx1, dc); px11 = _mm_permutevar_ps(vx1, dc1);
        } else {
            float ay00[4], ay01[4], ay10[4], ay11[4], ax00[4], ax01[4], ax10[4], ax11[4];
            for (int l = 0; l < 4; ++l) {
                const int r = rl[l], c = cl[l];
                const int r1 = std::min(r + 1, h - 1), c1 = std::min(c + 1, w - 1);
                const std::size_t i00 = std::size_t(r) * w + c, i01 = std::size_t(r) * w + c1;
                const std::size_t i10 = std::size_t(r1) * w + c, i11 = std::size_t(r1) * w + c1;
                ay00[l] = py[i00]; ay01[l] = py[i01]; ay10[l] = py[i10]; ay11[l] = py[i11];
                ax00[l] = px[i00]; ax01[l] = px[i01]; ax10[l] = px[i10]; ax11[l] = px[i11];
            }
            py00 = _mm_loadu_ps(ay00); py01 = _mm_loadu_ps(ay01); py10 = _mm_loadu_ps(ay10); py11 = _mm_loadu_ps(ay11);
            px00 = _mm_loadu_ps(ax00); px01 = _mm_loadu_ps(ax01); px10 = _mm_loadu_ps(ax10); px11 = _mm_loadu_ps(ax11);
        }
        const __m128 dy = _mm_fmadd_ps(oty, _mm_fmadd_ps(otx, py00, _mm_mul_ps(tx, py01)),
                                       _mm_mul_ps(ty, _mm_fmadd_ps(otx, py10, _mm_mul_ps(tx, py11))));
        const __m128 dx = _mm_fmadd_ps(oty, _mm_fmadd_ps(otx, px00, _mm_mul_ps(tx, px01)),
                                       _mm_mul_ps(ty, _mm_fmadd_ps(otx, px10, _mm_mul_ps(tx, px11))));
        const __m128 ny = _mm_blendv_ps(ry_v, _mm_fnmadd_ps(two_v, dy, sy_v), active);
        const __m128 nx = _mm_blendv_ps(rx_v, _mm_fnmadd_ps(two_v, dx, sx_v), active);
        const __m128 err = _mm_max_ps(_mm_and_ps(_mm_sub_ps(ny, ry_v), abs_mask),
                                      _mm_and_ps(_mm_sub_ps(nx, rx_v), abs_mask));
        active = _mm_and_ps(active, _mm_cmpgt_ps(err, eps_v));
        ry_v = ny; rx_v = nx;
        if (_mm_movemask_ps(active) == 0) break;
    }
    _mm_storeu_ps(ry_out, ry_v);
    _mm_storeu_ps(rx_out, rx_v);
#else
    const float h_max = float(h - 1), w_max = float(w - 1);
    for (int l = 0; l < 4; ++l) {
        const float sx = sx0 + float(l);
        float ry = sy, rx = sx;
        int prev_r = -1, prev_c = -1;
        float py00 = 0, py01 = 0, py10 = 0, py11 = 0;
        float px00 = 0, px01 = 0, px10 = 0, px11 = 0;
        for (int k = 0; k < 5; ++k) {
            const float y = std::max(0.0f, std::min(h_max, ry * .5f));
            const float x = std::max(0.0f, std::min(w_max, rx * .5f));
            const int r = int(y), c = int(x);
            if (r != prev_r || c != prev_c) {
                prev_r = r; prev_c = c;
                const int r1 = std::min(r + 1, h - 1), c1 = std::min(c + 1, w - 1);
                const std::size_t i00 = std::size_t(r) * w + c, i01 = std::size_t(r) * w + c1;
                const std::size_t i10 = std::size_t(r1) * w + c, i11 = std::size_t(r1) * w + c1;
                py00 = py[i00]; py01 = py[i01]; py10 = py[i10]; py11 = py[i11];
                px00 = px[i00]; px01 = px[i01]; px10 = px[i10]; px11 = px[i11];
            }
            const float ty = y - r, tx = x - c;
            const float oty = 1.0f - ty, otx = 1.0f - tx;
            const float dy = std::fma(oty, std::fma(otx, py00, tx * py01), ty * std::fma(otx, py10, tx * py11));
            const float dx = std::fma(oty, std::fma(otx, px00, tx * px01), ty * std::fma(otx, px10, tx * px11));
            const float next_y = std::fma(-2.0f, dy, sy), next_x = std::fma(-2.0f, dx, sx);
            const bool settled = std::fabs(next_y - ry) <= 1e-7f && std::fabs(next_x - rx) <= 1e-7f;
            ry = next_y; rx = next_x;
            if (settled) break;
        }
        ry_out[l] = ry; rx_out[l] = rx;
    }
#endif
}
inline float raw_color(const std::vector<float>& a, int h, int w,
                       const uint8_t* cfa, float y, float x, int color,
                       const Noise* noise=nullptr,std::size_t frame=0) {
    const int iy = int(std::floor(y)), ix = int(std::floor(x));
    const int r_lo = std::max(0, iy - 2), r_hi = std::min(h - 1, iy + 3);
    const int c_lo = std::max(0, ix - 2), c_hi = std::min(w - 1, ix + 3);
    float kx[6];
    int cm[6];
    for (int c = c_lo; c <= c_hi; ++c) {
        kx[c - c_lo] = std::max(0.0f, 3.0f - std::fabs(c - x));
        cm[c - c_lo] = c % 6;
    }
    float sum = 0, weight = 0;
    for (int r = r_lo; r <= r_hi; ++r) {
        const float ky = std::max(0.0f, 3.0f - std::fabs(r - y));
        if (ky <= 0.0f) continue;
        const uint8_t* rcfa = cfa + (r % 6) * 6;
        const float* row = a.data() + std::size_t(r) * w;
        for (int c = c_lo; c <= c_hi; ++c) {
            if (rcfa[cm[c - c_lo]] == color) {
                const float k = ky * kx[c - c_lo];
                const float value = row[c];
                if (std::isfinite(value) && (!noise||noise->unsaturated(frame,value))) {
                    sum += k * value; weight += k;
                }
            }
        }
    }
    return weight > 0 ? sum / weight : 0;
}
inline std::array<float,3> raw_rgb(const std::vector<float>& a,int h,int w,
                                 const uint8_t* cfa,float y,float x) {
    const int iy=int(std::floor(y)),ix=int(std::floor(x));
    const int r_lo=std::max(0,iy-2),r_hi=std::min(h-1,iy+3);
    const int c_lo=std::max(0,ix-2),c_hi=std::min(w-1,ix+3);
    const int cw = c_hi - c_lo + 1;
    float kx[6];
    int cm[6];
    int cmod = c_lo % 6;
    for(int dc=0;dc<cw;++dc) {
        kx[dc]=std::max(0.0f,3-std::fabs((c_lo+dc)-x));
        cm[dc]=cmod;
        if(++cmod==6) cmod=0;
    }
    std::array<float,3> sum{},weight{};
    int rmod = r_lo % 6;
    for(int r=r_lo;r<=r_hi;++r) {
        const float ky=std::max(0.0f,3-std::fabs(r-y));
        const int cur_rmod = rmod;
        if(++rmod==6) rmod=0;
        if(ky<=0.0f) continue;
        const uint8_t* rcfa=cfa+cur_rmod*6;
        const float* row=a.data()+std::size_t(r)*w+c_lo;
        for(int dc=0;dc<cw;++dc) {
            const float value=row[dc];
            if(!std::isfinite(value)) continue;
            const int channel=rcfa[cm[dc]];
            const float k=ky*kx[dc];
            sum[channel]+=k*value;weight[channel]+=k;
        }
    }
    for(int k=0;k<3;++k) sum[k]=weight[k]>0?sum[k]/weight[k]:0;
    return sum;
}
inline float median_small(float* v, int n) {
    if (n <= 0) return 0.0f;
    for (int i = 1; i < n; ++i) {
        const float key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; --j; }
        v[j + 1] = key;
    }
    return v[n / 2];
}
inline std::array<float,3> raw_rgb_bayer(const std::vector<float>& a, int h, int w,
                                         const uint8_t* cfa, float y, float x) {
    const int iy = int(std::floor(y)), ix = int(std::floor(x));
    if (iy < 2 || iy + 3 >= h || ix < 2 || ix + 3 >= w)
        return raw_rgb(a, h, w, cfa, y, x);
    const float fy = y - float(iy), fx = x - float(ix);
    const float kx0 = 1.0f - fx, kx1 = 2.0f - fx, kx2 = 3.0f - fx;
    const float kx3 = 2.0f + fx, kx4 = 1.0f + fx, kx5 = fx;
    const float ky0 = 1.0f - fy, ky1 = 2.0f - fy, ky2 = 3.0f - fy;
    const float ky3 = 2.0f + fy, ky4 = 1.0f + fy, ky5 = fy;
    const int r_lo = iy - 2, c_lo = ix - 2;
    const float* base = a.data() + std::size_t(r_lo) * w + c_lo;
    float he[6], ho[6];
    for (int dr = 0; dr < 6; ++dr) {
        const float* rptr = base + std::size_t(dr) * w;
        he[dr] = kx0 * rptr[0] + kx2 * rptr[2] + kx4 * rptr[4];
        ho[dr] = kx1 * rptr[1] + kx3 * rptr[3] + kx5 * rptr[5];
    }
    const float s_ee = ky0 * he[0] + ky2 * he[2] + ky4 * he[4];
    const float s_eo = ky0 * ho[0] + ky2 * ho[2] + ky4 * ho[4];
    const float s_oe = ky1 * he[1] + ky3 * he[3] + ky5 * he[5];
    const float s_oo = ky1 * ho[1] + ky3 * ho[3] + ky5 * ho[5];
    if (!std::isfinite(s_ee + s_eo + s_oe + s_oo))
        return raw_rgb(a, h, w, cfa, y, x);
    const int pr = r_lo & 1, pc = c_lo & 1;
    const int c_ee = cfa[pr * 6 + pc];
    const int c_eo = cfa[pr * 6 + (pc ^ 1)];
    const int c_oe = cfa[(pr ^ 1) * 6 + pc];
    const int c_oo = cfa[(pr ^ 1) * 6 + (pc ^ 1)];
    const float wy_e = 5.0f - fy, wy_o = 4.0f + fy;
    const float wx_e = 5.0f - fx, wx_o = 4.0f + fx;
    std::array<float,3> sum{}, weight{};
    sum[c_ee] += s_ee; weight[c_ee] += wy_e * wx_e;
    sum[c_eo] += s_eo; weight[c_eo] += wy_e * wx_o;
    sum[c_oe] += s_oe; weight[c_oe] += wy_o * wx_e;
    sum[c_oo] += s_oo; weight[c_oo] += wy_o * wx_o;
    return {weight[0] > 0 ? sum[0] / weight[0] : 0.0f,
            weight[1] > 0 ? sum[1] / weight[1] : 0.0f,
            weight[2] > 0 ? sum[2] / weight[2] : 0.0f};
}
inline void add_stats(StageStats& a, const StageStats& b) {
    a.samples += b.samples; a.changed += b.changed; a.rejected += b.rejected;
    a.before += b.before; a.after += b.after;
    a.rms_update += b.rms_update; a.noise_variance += b.noise_variance;
    a.signal_power += b.signal_power;
    a.candidate_samples += b.candidate_samples;
    for(size_t i=0;i<a.worker_seconds.size();++i) a.worker_seconds[i]+=b.worker_seconds[i];
    a.peak_scratch_bytes = std::max(a.peak_scratch_bytes, b.peak_scratch_bytes);
}
} // namespace detail

inline ExposureStats exposure_normalize(Frames& raw, Frames& g, int h, int w,
                                        const Noise& noise, const Frames& fy,
                                        const Frames& fx, const Frames& mw,
                                        const Execution& execution) {
    detail::validate(raw, h, w, nullptr, fy, fx, mw);
    const int hh = h / 2, wh = w / 2;
    detail::planes(g, raw.size(), std::size_t(hh) * wh, false);
    ExposureStats result;
    result.gains.assign(raw.size(), 1);
    result.variance_scales.assign(raw.size(), 1);
    result.read_variances.assign(raw.size(), noise.read_variance);
    result.shot_coefficients.assign(raw.size(), noise.shot);
    result.saturation_levels.assign(raw.size(), .98f);
    if (noise.frame_read_variances) result.read_variances[0] = noise.frame_read_variances[0];
    if (noise.frame_shot_coefficients) result.shot_coefficients[0] = noise.frame_shot_coefficients[0];
    if (noise.frame_saturation_levels) result.saturation_levels[0] = noise.frame_saturation_levels[0];
    std::vector<std::size_t> counts(raw.size());
    detail::run(int(raw.size()), execution, [&](int begin, int end) {
        for (int n = std::max(1, begin); n < end; ++n) {
            std::vector<float> ratios;
            const int step = std::max(1, int(std::sqrt(double(hh) * wh / 32768.0)));
            for (int r = 0; r < hh; r += step) for (int c = 0; c < wh; c += step) {
                const float y = r + detail::field(fy, n, hh, wh, float(r), float(c));
                const float x = c + detail::field(fx, n, hh, wh, float(r), float(c));
                if (!detail::inside(y, x, hh, wh) ||
                    detail::field(mw, n, hh, wh, float(r), float(c), 1) < .5f) continue;
                const float a = g[0][std::size_t(r) * wh + c];
                const float b = detail::sample(g[n].data(), hh, wh, y, x);
                const float low = 4 * std::sqrt(noise.read_variance > 0 ? noise.read_variance : 0);
                if (a > low && b > low && a < .85f && b < .85f && std::isfinite(a + b))
                    ratios.push_back(std::log(a / b));
            }
            counts[n] = ratios.size();
            if (ratios.size() >= 8) {
                const float center = detail::median(ratios);
                std::vector<float> dev(ratios.size());
                for (std::size_t i = 0; i < ratios.size(); ++i) dev[i] = std::fabs(ratios[i] - center);
                const float tolerance = std::max(.015f, 3 * 1.4826f * detail::median(dev));
                double sum = 0; int count = 0;
                for (float v : ratios) if (std::fabs(v - center) <= tolerance) { sum += v; ++count; }
                const float gain = std::exp(float(sum / std::max(1, count)));
                result.gains[n] = std::max(1.0f / 64, std::min(64.0f, gain));
                result.variance_scales[n] = result.gains[n] * result.gains[n];
            }
            const float read = noise.frame_read_variances ? noise.frame_read_variances[n] : noise.read_variance;
            const float shot = noise.frame_shot_coefficients ? noise.frame_shot_coefficients[n] : noise.shot;
            result.read_variances[n] = read * result.variance_scales[n];
            result.shot_coefficients[n] = shot * result.gains[n];
            result.saturation_levels[n] = (noise.frame_saturation_levels ? noise.frame_saturation_levels[n] : .98f) * result.gains[n];
            for (float& v : raw[n]) v *= result.gains[n];
            for (float& v : g[n]) v *= result.gains[n];
        }
    });
    result.samples = std::accumulate(counts.begin(), counts.end(), std::size_t(0));
    return result;
}

inline StageStats row_rolling_shutter_smooth(Frames& fy, Frames& fx, const Frames& mw,
                                            int h, int w, float strength,
                                            const Execution& execution) {
    detail::shape(h, w);
    detail::planes(fx, fy.size(), std::size_t(h) * w, false);
    detail::planes(fy, fy.size(), std::size_t(h) * w, false);
    detail::planes(mw, fy.size(), std::size_t(h) * w);
    strength = detail::clamp01(strength);
    const int n_frames = int(fy.size());
    const int n_planes = n_frames * 2;
    std::vector<std::vector<float>> rows(n_planes, std::vector<float>(h, 0.0f));
    std::vector<std::vector<unsigned char>> has_row(n_planes, std::vector<unsigned char>(h, 0));
    detail::run(n_planes * h, execution, [&](int begin, int end) {
        std::vector<float> values;
        values.reserve(w);
        for (int idx = begin; idx < end; ++idx) {
            const int p = idx / h, r = idx % h;
            const int n = p >> 1;
            const float* fptr = ((p & 1) ? fx[n] : fy[n]).data() + std::size_t(r) * w;
            const float* mptr = mw.empty() ? nullptr : (mw[n].data() + std::size_t(r) * w);
            values.clear();
            for (int c = 0; c < w; ++c) {
                const float fv = fptr[c];
                if ((!mptr || mptr[c] >= .5f) && std::isfinite(fv)) values.push_back(fv);
            }
            if (!values.empty()) {
                rows[p][r] = detail::median(values);
                has_row[p][r] = 1;
            }
        }
    });
    for (int p = 0; p < n_planes; ++p) {
        for (int r = 0; r < h; ++r) {
            if (!has_row[p][r]) rows[p][r] = r ? rows[p][r - 1] : 0.0f;
        }
    }
    std::vector<float> deltas(std::size_t(n_planes) * h, 0.0f);
    const int k[5] = {1, 4, 6, 4, 1};
    for (int p = 0; p < n_planes; ++p) {
        const auto& row = rows[p];
        for (int r = 0; r < h; ++r) {
            float sum = 0, den = 0;
            for (int d = -2; d <= 2; ++d) if (r + d >= 0 && r + d < h) {
                sum += k[d + 2] * row[r + d]; den += k[d + 2];
            }
            const float sm = (r < 2 || r >= h - 2) ? row[r] : sum / den;
            deltas[std::size_t(p) * h + r] = strength * (sm - row[r]);
        }
    }
    std::vector<double> row_rms(std::size_t(n_planes) * h, 0.0);
    std::vector<std::size_t> row_changed(std::size_t(n_planes) * h, 0);
    detail::run(n_planes * h, execution, [&](int begin, int end) {
        for (int idx = begin; idx < end; ++idx) {
            const int p = idx / h, r = idx % h;
            const int n = p >> 1;
            const float delta = deltas[idx];
            if (delta == 0.0f) continue;
            float* fptr = ((p & 1) ? fx[n] : fy[n]).data() + std::size_t(r) * w;
            const float* mptr = mw.empty() ? nullptr : (mw[n].data() + std::size_t(r) * w);
            double l_rms = 0.0;
            std::size_t l_chg = 0;
            if (!mptr) {
                for (int c = 0; c < w; ++c) fptr[c] += delta;
                l_rms = double(w) * double(delta) * double(delta);
                l_chg = std::size_t(w);
            } else {
                for (int c = 0; c < w; ++c) {
                    const float gd = detail::clamp01(mptr[c]) * delta;
                    fptr[c] += gd;
                    l_rms += double(gd) * double(gd);
                    if (gd != 0.0f) ++l_chg;
                }
            }
            row_rms[idx] = l_rms;
            row_changed[idx] = l_chg;
        }
    });
    StageStats out;
    out.samples = std::size_t(n_planes) * h * w;
    out.peak_scratch_bytes = std::size_t(2 * h + w) * sizeof(float);
    for (std::size_t i = 0; i < row_rms.size(); ++i) {
        out.rms_update += row_rms[i];
        out.changed += row_changed[i];
    }
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.samples));
    return out;
}

inline StageStats directional_lucky_weights(const Frames& g, int h, int w,
                                             const Frames& fy, const Frames& fx,
                                             const Noise& noise, Frames& weights,
                                             const Execution& execution,
                                             const uint8_t* selected_direction) {
    detail::shape(h, w);
    detail::planes(g, g.size(), std::size_t(h) * w, false);
    detail::planes(fy, g.size(), std::size_t(h) * w);
    detail::planes(fx, g.size(), std::size_t(h) * w);
    if (fy.empty() != fx.empty()) throw std::invalid_argument("flow pair required");
    const int directions=selected_direction?1:4;
    if(selected_direction) for(size_t i=0;i<std::size_t(h)*w;++i)
        if(selected_direction[i]>3) throw std::invalid_argument("direction must be 0..3");
    weights.assign(g.size(), std::vector<float>(std::size_t(h) * w * directions, 1));
    const int dy[4] = {0, 1, 1, 1}, dx[4] = {1, 1, 0, -1};
    std::vector<StageStats> row_stats(h);
    const bool has_flow = !fy.empty();
    const float h_lim = float(h - 3), w_lim = float(w - 3);
    detail::run(h, execution, [&](int begin, int end) {
        float scores[32][4];
        const std::size_t n_frames = g.size();
        for (int r = begin; r < end; ++r) {
            double r_after = 0;
            std::size_t r_changed = 0;
            for (int c = 0; c < w; ++c) {
                const auto pixel=std::size_t(r)*w+c;
                float best[4] = {};
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const float y = has_flow ? (float(r) + fy[n][pixel]) : float(r);
                    const float x = has_flow ? (float(c) + fx[n][pixel]) : float(c);
                    const float* gp = g[n].data();
                    if (y >= 2.0f && y < h_lim && x >= 2.0f && x < w_lim) {
                        const int iy = int(y), ix = int(x);
                        const float ty = y - float(iy), tx = x - float(ix);
                        const float* base = gp + std::size_t(iy) * w + ix;
                        if (ty == 0.0f && tx == 0.0f) {
                            const float mid = base[0];
                            const float nvar6 = 6.0f * noise.variance(mid);
                            for (int index = 0; index < directions; ++index) {
                                const int d = selected_direction ? selected_direction[pixel] : index;
                                const int s_step = dy[d] * w + dx[d];
                                const int t_step = dx[d] * w - dy[d];
                                const float v_m = base[-t_step + s_step] + base[-t_step - s_step] - 2.0f * base[-t_step];
                                const float v_0 = base[s_step] + base[-s_step] - 2.0f * mid;
                                const float v_p = base[t_step + s_step] + base[t_step - s_step] - 2.0f * base[t_step];
                                const float score = std::max(0.0f, (v_m * v_m + v_0 * v_0 + v_p * v_p) * (1.0f / 3.0f) - nvar6);
                                if (n_frames <= 32) scores[n][index] = score;
                                else weights[n][pixel * directions + index] = score;
                                best[d] = std::max(best[d], score);
                            }
                        } else {
                            const float oty = 1.0f - ty, otx = 1.0f - tx;
                            auto bilerp_off = [&](int off) -> float {
                                const float* p = base + off;
                                return oty * (otx * p[0] + tx * p[1]) + ty * (otx * p[w] + tx * p[w + 1]);
                            };
                            const float mid = bilerp_off(0);
                            const float nvar6 = 6.0f * noise.variance(mid);
                            for (int index = 0; index < directions; ++index) {
                                const int d = selected_direction ? selected_direction[pixel] : index;
                                const int s_step = dy[d] * w + dx[d];
                                const int t_step = dx[d] * w - dy[d];
                                const float v_m = bilerp_off(-t_step + s_step) + bilerp_off(-t_step - s_step) - 2.0f * bilerp_off(-t_step);
                                const float v_0 = bilerp_off(s_step) + bilerp_off(-s_step) - 2.0f * mid;
                                const float v_p = bilerp_off(t_step + s_step) + bilerp_off(t_step - s_step) - 2.0f * bilerp_off(t_step);
                                const float score = std::max(0.0f, (v_m * v_m + v_0 * v_0 + v_p * v_p) * (1.0f / 3.0f) - nvar6);
                                if (n_frames <= 32) scores[n][index] = score;
                                else weights[n][pixel * directions + index] = score;
                                best[d] = std::max(best[d], score);
                            }
                        }
                    } else {
                        const float mid = detail::sample(gp, h, w, y, x);
                        const float nvar6 = 6.0f * noise.variance(mid);
                        for (int index = 0; index < directions; ++index) {
                            const int d=selected_direction?selected_direction[pixel]:index;
                            float score = 0;
                            for (int t = -1; t <= 1; ++t) {
                                const float yy = y + t * dx[d], xx = x - t * dy[d];
                                const float v = detail::sample(gp, h, w, yy + dy[d], xx + dx[d]) +
                                                detail::sample(gp, h, w, yy - dy[d], xx - dx[d]) -
                                            2 * detail::sample(gp, h, w, yy, xx);
                                score += v * v * (1.0f / 3.0f);
                            }
                            score = std::max(0.0f, score - nvar6);
                            if (n_frames <= 32) scores[n][index] = score;
                            else weights[n][pixel*directions+index] = score;
                            best[d] = std::max(best[d], score);
                        }
                    }
                }
                for (std::size_t n = 0; n < n_frames; ++n) for (int index = 0; index < directions; ++index) {
                    const int d=selected_direction?selected_direction[pixel]:index;
                    const float sc = (n_frames <= 32) ? scores[n][index] : weights[n][pixel*directions+index];
                    const float v = best[d] > 1e-12f ? .25f + .75f * std::sqrt(sc / best[d]) : 1.0f;
                    weights[n][pixel*directions+index] = v;
                    r_after += v;
                    if (v < .999f) ++r_changed;
                }
            }
            row_stats[r].after = r_after;
            row_stats[r].changed = r_changed;
        }
    });
    StageStats out;
    out.samples = std::size_t(h) * w * g.size() * directions;
    out.before = double(out.samples);
    for (const auto& rs : row_stats) { out.after += rs.after; out.changed += rs.changed; }
    return out;
}

namespace detail {
struct Footprint {
    std::array<std::size_t, 16> index{};
    std::array<float, 16> weight{};
    int count = 0;
};
struct InverseFlowCache {
    std::vector<std::array<float,4>> bounds;
    const Frames* mask=nullptr;
};
inline void cached_inverse_flow(const Frames& fy,const Frames& fx,size_t n,int h,int w,
                                float sy,float sx,float& y,float& x,const InverseFlowCache* cache,
                                float* gate=nullptr) {
    if(!cache) {inverse_flow(fy,fx,n,h,w,sy,sx,y,x);return;}
    const auto& b=cache->bounds[n];
    if(b[0]==b[1]&&b[2]==b[3]) {
        y=sy-b[0];x=sx-b[2];
    } else inverse_flow(fy,fx,n,h,w,sy,sx,y,x);
    if(gate) *gate=inside(y,x,h*2,w*2)?(cache->mask?field(*cache->mask,n,h,w,y*.5f,x*.5f,1):1):0;
}
// Pixel-aperture tent, width two RAW pixels, normalized at sensor boundaries.
// The very same coefficients are used in projection and its exact transpose.
inline Footprint footprint(float ry, float rx, int h2, int w2, int color) {
    Footprint a;
    const float y = 2 * ry, x = 2 * rx;
    const int r0 = int(std::floor(y)) - 1, c0 = int(std::floor(x)) - 1;
    float sum = 0;
    for (int r = r0; r < r0 + 4; ++r) for (int c = c0; c < c0 + 4; ++c) {
        if (r < 0 || c < 0 || r >= h2 || c >= w2) continue;
        const float k = std::max(0.0f, 1 - .5f * std::fabs(r - y)) *
                        std::max(0.0f, 1 - .5f * std::fabs(c - x));
        if (k <= 0) continue;
        a.index[a.count] = (std::size_t(r) * w2 + c) * 3 + color;
        a.weight[a.count++] = k; sum += k;
    }
    if (sum > 0) {
        const float inverse_sum=1/sum;
        for (int k = 0; k < a.count; ++k) a.weight[k] *= inverse_sum;
    }
    return a;
}
inline float project(const float* rgb, const Footprint& a) {
    float value = 0;
    for (int k = 0; k < a.count; ++k) value += a.weight[k] * rgb[a.index[k]];
    return value;
}
inline float project_tent(const float* rgb, float ry, float rx, int h2, int w2, int ch) {
    const float y = 2 * ry, x = 2 * rx;
    const int fr0 = int(std::floor(y)) - 1, fc0 = int(std::floor(x)) - 1;
    if (fr0 >= 0 && fr0 + 3 < h2 && fc0 >= 0 && fc0 + 3 < w2) {
        const float fy_f = y - float(fr0 + 1), fx_f = x - float(fc0 + 1);
        const float ky0 = 0.25f * (1.0f - fy_f), ky1 = 0.5f - 0.25f * fy_f;
        const float ky2 = 0.25f * (1.0f + fy_f), ky3 = 0.25f * fy_f;
        const float kx0 = 0.25f * (1.0f - fx_f), kx1 = 0.5f - 0.25f * fx_f;
        const float kx2 = 0.25f * (1.0f + fx_f), kx3 = 0.25f * fx_f;
        const std::size_t row_str = std::size_t(w2) * 3;
        const float* rp0 = rgb + std::size_t(fr0) * row_str + std::size_t(fc0) * 3 + ch;
        const float* rp1 = rp0 + row_str;
        const float* rp2 = rp1 + row_str;
        const float* rp3 = rp2 + row_str;
        return ky0 * (kx0 * rp0[0] + kx1 * rp0[3] + kx2 * rp0[6] + kx3 * rp0[9]) +
               ky1 * (kx0 * rp1[0] + kx1 * rp1[3] + kx2 * rp1[6] + kx3 * rp1[9]) +
               ky2 * (kx0 * rp2[0] + kx1 * rp2[3] + kx2 * rp2[6] + kx3 * rp2[9]) +
               ky3 * (kx0 * rp3[0] + kx1 * rp3[3] + kx2 * rp3[6] + kx3 * rp3[9]);
    }
    float ky[4], kx[4];
    for (int d = 0; d < 4; ++d) {
        ky[d] = std::max(0.0f, 1 - .5f * std::fabs((fr0 + d) - y));
        kx[d] = std::max(0.0f, 1 - .5f * std::fabs((fc0 + d) - x));
    }
    float wgt[16];
    const float* ptr[16];
    int cnt = 0;
    float sum = 0;
    for (int dr = 0; dr < 4; ++dr) {
        const int fr = fr0 + dr;
        if (fr < 0 || fr >= h2 || ky[dr] <= 0) continue;
        const float* row = rgb + std::size_t(fr) * w2 * 3 + ch;
        for (int dc = 0; dc < 4; ++dc) {
            const int fc = fc0 + dc;
            if (fc < 0 || fc >= w2) continue;
            const float k = ky[dr] * kx[dc];
            if (k <= 0) continue;
            wgt[cnt] = k;
            ptr[cnt++] = row + std::size_t(fc) * 3;
            sum += k;
        }
    }
    if (sum <= 0) return 0;
    const float inv_sum = 1 / sum;
    float value = 0;
    for (int i = 0; i < cnt; ++i) value += (wgt[i] * inv_sum) * (*ptr[i]);
    return value;
}
inline double forward_error(const Frames& raw, int h, int w, const uint8_t* cfa,
                             const Frames& fy, const Frames& fx, const Frames& mw,
                             const Noise& noise, const float* rgb, std::size_t& count,
                             const Execution& execution = {}, const InverseFlowCache* cache=nullptr,
                             const std::vector<uint8_t>* frame_all_valid = nullptr,
                             const std::vector<uint8_t>* frame_unit_mw = nullptr) {
    double sum = 0; count = 0;
    std::vector<double> row_sum(raw.size()*h,0);
    std::vector<std::size_t> row_count(row_sum.size(),0);
    const int stride=std::max(1,execution.diagnostic_stride);
    const int hh = h / 2, wh = w / 2, h2 = 2 * h, w2 = 2 * w;
    run(int(row_sum.size()),execution,[&](int begin,int end) {
      for(int row=begin;row<end;++row) {
        const std::size_t n=std::size_t(row)/h;
        const int r=row%h;
        const uint8_t* rcfa = cfa + (r % 6) * 6;
        const float* rptr = raw[n].data() + std::size_t(r) * w;
        const int first_c=int((stride-(std::size_t(r)*w+n*19)%stride)%stride);
        const bool all_valid = frame_all_valid && (*frame_all_valid)[n] != 0;
        const bool unit_mw = (frame_unit_mw && (*frame_unit_mw)[n] != 0) || mw.empty();
        const float rv_n = noise.frame_read_variances ? noise.frame_read_variances[n] : noise.read_variance;
        const float sh_n = noise.frame_shot_coefficients ? noise.frame_shot_coefficients[n] : noise.shot;
        double r_s = 0.0;
        std::size_t r_c = 0;
        if (stride == 1 && !fy.empty() && cache &&
            !(cache->bounds[n][0] == cache->bounds[n][1] && cache->bounds[n][2] == cache->bounds[n][3])) {
            const float* py = fy[n].data();
            const float* px = fx[n].data();
            int c = 0;
            for (; c + 4 <= w; c += 4) {
                float y4[4], x4[4];
                inverse_flow4(py, px, hh, wh, float(r), float(c), y4, x4);
                for (int l = 0; l < 4; ++l) {
                    const int cc = c + l;
                    const float y = y4[l], x = x4[l];
                    if (!inside(y, x, h, w)) continue;
                    const float v = rptr[cc];
                    if (!all_valid && (!std::isfinite(v) || !noise.unsaturated(n, v))) continue;
                    const float weight = unit_mw ? 1.0f : clamp01(sample(mw[n].data(), hh, wh, .5f * y, .5f * x));
                    if (weight <= 0) continue;
                    const double residual = v - project_tent(rgb, y, x, h2, w2, rcfa[cc % 6]);
                    const float fvar = std::max(1e-12f, rv_n + sh_n * std::max(0.0f, v));
                    r_s += residual * residual * weight / fvar;
                    ++r_c;
                }
            }
            for (; c < w; ++c) {
                float y, x, gate = 0;
                cached_inverse_flow(fy, fx, n, hh, wh, float(r), float(c), y, x, cache, &gate);
                if (!inside(y, x, h, w)) continue;
                const float v = rptr[c];
                if (!all_valid && (!std::isfinite(v) || !noise.unsaturated(n, v))) continue;
                const float weight = unit_mw ? 1.0f : clamp01(gate);
                if (weight <= 0) continue;
                const double residual = v - project_tent(rgb, y, x, h2, w2, rcfa[c % 6]);
                const float fvar = std::max(1e-12f, rv_n + sh_n * std::max(0.0f, v));
                r_s += residual * residual * weight / fvar;
                ++r_c;
            }
        } else {
            for (int c = first_c; c < w; c += stride) {
                float y, x, gate = 0;
                cached_inverse_flow(fy, fx, n, hh, wh, float(r), float(c), y, x, cache, &gate);
                if (!inside(y, x, h, w)) continue;
                const float v = rptr[c];
                if (!all_valid && (!std::isfinite(v) || !noise.unsaturated(n, v))) continue;
                const float weight = unit_mw ? 1.0f : clamp01(cache ? gate : field(mw, n, hh, wh, .5f * y, .5f * x, 1));
                if (weight <= 0) continue;
                const double residual = v - project_tent(rgb, y, x, h2, w2, rcfa[c % 6]);
                const float fvar = std::max(1e-12f, rv_n + sh_n * std::max(0.0f, v));
                r_s += residual * residual * weight / fvar;
                ++r_c;
            }
        }
        row_sum[row] = r_s;
        row_count[row] = r_c;
      }
    });
    for(std::size_t row=0;row<row_sum.size();++row) {sum+=row_sum[row];count+=row_count[row];}
    return sum;
}

struct IbpSourceIndex {
    static constexpr int block = 8;
    int columns = 0;
    std::vector<std::size_t> offsets;
    std::vector<uint32_t> blocks;
};

inline IbpSourceIndex index_ibp_sources(const Frames& raw, std::size_t n, int h, int w,
                                       const Frames& fy, const Frames& fx, const Frames& mw,
                                       const Noise& noise, int tile, const Execution& execution,
                                       const InverseFlowCache* cache=nullptr,
                                       bool frame_all_valid=false) {
    IbpSourceIndex result;
    const int block=IbpSourceIndex::block;
    const int br=(h+block-1)/block,bc=(w+block-1)/block;
    const int nr=(2*h+tile-1)/tile,nc=(2*w+tile-1)/tile;
    result.columns=bc;
    std::vector<std::array<int,4>> rectangles(std::size_t(br)*bc,{-1,-1,-1,-1});
    std::vector<uint8_t> unit_mw_blk(std::size_t(br)*bc, 0);
    const int hh=h/2, wh=w/2;
    const bool can_fast_patch = (tile >= 16 && !fy.empty() && hh >= 2 && wh >= 2);
    run(br*bc,execution,[&](int begin,int end) {
        for(int b=begin;b<end;++b) {
            const int r0=(b/bc)*block,c0=(b%bc)*block;
            const int r_end=std::min(h,r0+block), c_end=std::min(w,c0+block);
            if(can_fast_patch) {
                const float* pfy=fy[n].data();
                const float* pfx=fx[n].data();
                const float* pmw=mw.empty()?nullptr:mw[n].data();
                const int sr0=std::max(0, std::min(hh-1, r0/2));
                const int sr1=std::max(0, std::min(hh-1, (r_end-1)/2 + 1));
                const int sc0=std::max(0, std::min(wh-1, c0/2));
                const int sc1=std::max(0, std::min(wh-1, (c_end-1)/2 + 1));
                float fy0_min=std::numeric_limits<float>::infinity(), fy0_max=-fy0_min;
                float fx0_min=fy0_min, fx0_max=-fy0_min;
                for(int hr=sr0; hr<=sr1; ++hr) {
                    const std::size_t off=std::size_t(hr)*wh;
                    for(int hc=sc0; hc<=sc1; ++hc) {
                        const float vy=pfy[off+hc], vx=pfx[off+hc];
                        fy0_min=std::min(fy0_min,vy); fy0_max=std::max(fy0_max,vy);
                        fx0_min=std::min(fx0_min,vx); fx0_max=std::max(fx0_max,vx);
                    }
                }
                if (std::isfinite(fy0_min + fy0_max + fx0_min + fx0_max) &&
                    (fy0_max - fy0_min) <= 12.0f && (fx0_max - fx0_min) <= 12.0f) {
                    // 2-step interval refinement handles panning shifts with spatial flow gradients.
                    const int tr0 = std::max(0, std::min(hh - 1, int(std::floor(0.5f * float(r0) - fy0_max)) - 2));
                    const int tr1 = std::max(0, std::min(hh - 1, int(std::ceil(0.5f * float(r_end - 1) - fy0_min)) + 3));
                    const int tc0 = std::max(0, std::min(wh - 1, int(std::floor(0.5f * float(c0) - fx0_max)) - 2));
                    const int tc1 = std::max(0, std::min(wh - 1, int(std::ceil(0.5f * float(c_end - 1) - fx0_min)) + 3));
                    if (tr1 - tr0 <= 24 && tc1 - tc0 <= 24) {
                        float fy1_min = std::numeric_limits<float>::infinity(), fy1_max = -fy1_min;
                        float fx1_min = fy1_min, fx1_max = -fy1_min;
                        for (int hr = tr0; hr <= tr1; ++hr) {
                            const std::size_t off = std::size_t(hr) * wh;
                            for (int hc = tc0; hc <= tc1; ++hc) {
                                const float vy = pfy[off + hc], vx = pfx[off + hc];
                                fy1_min = std::min(fy1_min, vy); fy1_max = std::max(fy1_max, vy);
                                fx1_min = std::min(fx1_min, vx); fx1_max = std::max(fx1_max, vx);
                            }
                        }
                        const float env_fy_min = std::min(fy0_min, fy1_min), env_fy_max = std::max(fy0_max, fy1_max);
                        const float env_fx_min = std::min(fx0_min, fx1_min), env_fx_max = std::max(fx0_max, fx1_max);
                        const int cr0 = std::max(0, std::min(hh - 1, int(std::floor(0.5f * float(r0) - env_fy_max)) - 3));
                        const int cr1 = std::max(0, std::min(hh - 1, int(std::ceil(0.5f * float(r_end - 1) - env_fy_min)) + 4));
                        const int cc0 = std::max(0, std::min(wh - 1, int(std::floor(0.5f * float(c0) - env_fx_max)) - 3));
                        const int cc1 = std::max(0, std::min(wh - 1, int(std::ceil(0.5f * float(c_end - 1) - env_fx_min)) + 4));
                        if (std::isfinite(fy1_min + fy1_max + fx1_min + fx1_max) &&
                            cr1 - cr0 <= 32 && cc1 - cc0 <= 32) {
                            float fy_min = std::numeric_limits<float>::infinity(), fy_max = -fy_min;
                            float fx_min = fy_min, fx_max = -fy_min;
                            float mw_max = pmw ? 0.0f : 1.0f, mw_min = 1.0f;
                            for (int hr = cr0; hr <= cr1; ++hr) {
                                const std::size_t off = std::size_t(hr) * wh;
                                for (int hc = cc0; hc <= cc1; ++hc) {
                                    const float vy = pfy[off + hc], vx = pfx[off + hc];
                                    fy_min = std::min(fy_min, vy); fy_max = std::max(fy_max, vy);
                                    fx_min = std::min(fx_min, vx); fx_max = std::max(fx_max, vx);
                                    if (pmw) {
                                        const float vm = pmw[off + hc];
                                        mw_max = std::max(mw_max, vm);
                                        mw_min = std::min(mw_min, vm);
                                    }
                                }
                            }
                            const float all_fy_max = std::max(fy0_max, fy_max), all_fy_min = std::min(fy0_min, fy_min);
                            const float all_fx_max = std::max(fx0_max, fx_max), all_fx_min = std::min(fx0_min, fx_min);
                            const float u_lo_y = std::max(0.0f, std::min(float(hh - 1), 0.5f * float(r0) - all_fy_max));
                            const float u_hi_y = std::max(0.0f, std::min(float(hh - 1), 0.5f * float(r_end - 1) - all_fy_min));
                            const float u_lo_x = std::max(0.0f, std::min(float(wh - 1), 0.5f * float(c0) - all_fx_max));
                            const float u_hi_x = std::max(0.0f, std::min(float(wh - 1), 0.5f * float(c_end - 1) - all_fx_min));
                            if (std::isfinite(fy_min + fy_max + fx_min + fx_max) &&
                                int(u_lo_y) >= cr0 && std::min(int(u_hi_y) + 1, hh - 1) <= cr1 &&
                                int(u_lo_x) >= cc0 && std::min(int(u_hi_x) + 1, wh - 1) <= cc1) {
                                if (mw_max <= 0.0f) continue;
                                const float ry_lo = float(r0) - 2.0f * all_fy_max, ry_hi = float(r_end - 1) - 2.0f * all_fy_min;
                                const float rx_lo = float(c0) - 2.0f * all_fx_max, rx_hi = float(c_end - 1) - 2.0f * all_fx_min;
                                if (ry_hi < 0.0f || ry_lo > float(h - 1) || rx_hi < 0.0f || rx_lo > float(w - 1)) continue;
                                bool any_valid = frame_all_valid;
                                for (int r = r0; r < r_end && !any_valid; ++r) {
                                    const float* rptr = raw[n].data() + std::size_t(r) * w;
                                    for (int c = c0; c < c_end; ++c) {
                                        const float v = rptr[c];
                                        if (std::isfinite(v) && noise.unsaturated(n, v)) { any_valid = true; break; }
                                    }
                                }
                                if (!any_valid) continue;
                                const float ymin = 2.0f * std::max(0.0f, ry_lo), ymax = 2.0f * std::min(float(h - 1), ry_hi);
                                const float xmin = 2.0f * std::max(0.0f, rx_lo), xmax = 2.0f * std::min(float(w - 1), rx_hi);
                                rectangles[b] = {
                                    clamp(int(std::floor((ymin - 2) / tile)), nr), clamp(int(std::floor((ymax + 2) / tile)), nr),
                                    clamp(int(std::floor((xmin - 2) / tile)), nc), clamp(int(std::floor((xmax + 2) / tile)), nc)};
                                if (mw_min >= 1.0f) unit_mw_blk[b] = 1;
                                continue;
                            }
                        }
                    }
                }
            }
            float ymin=std::numeric_limits<float>::infinity(),ymax=-ymin,xmin=ymin,xmax=-ymin;
            bool all_unit = true;
            for(int r=r0;r<r_end;++r) {
                const float* rptr = raw[n].data() + std::size_t(r) * w;
                for(int c=c0;c<c_end;++c) {
                    const float v=rptr[c];
                    if(!frame_all_valid && (!std::isfinite(v)||!noise.unsaturated(n,v))) continue;
                    float y,x,gate=0;cached_inverse_flow(fy,fx,n,hh,wh,float(r),float(c),y,x,cache,&gate);
                    const float gval = clamp01(cache?gate:field(mw,n,hh,wh,y*.5f,x*.5f,1));
                    if(!inside(y,x,h,w)||gval<=0) continue;
                    if(gval < 1.0f) all_unit = false;
                    ymin=std::min(ymin,2*y);ymax=std::max(ymax,2*y);
                    xmin=std::min(xmin,2*x);xmax=std::max(xmax,2*x);
                }
            }
            if(ymin<=ymax) {
                rectangles[b]={
                    clamp(int(std::floor((ymin-2)/tile)),nr),clamp(int(std::floor((ymax+2)/tile)),nr),
                    clamp(int(std::floor((xmin-2)/tile)),nc),clamp(int(std::floor((xmax+2)/tile)),nc)};
                if(all_unit) unit_mw_blk[b] = 1;
            }
        }
    });
    result.offsets.assign(std::size_t(nr)*nc+1,0);
    for(const auto& r:rectangles) if(r[0]>=0)
        for(int y=r[0];y<=r[1];++y) for(int x=r[2];x<=r[3];++x) ++result.offsets[std::size_t(y)*nc+x+1];
    std::partial_sum(result.offsets.begin(),result.offsets.end(),result.offsets.begin());
    result.blocks.resize(result.offsets.back());
    auto cursor=result.offsets;
    // Ascending source-block IDs allow each consumer to retain n/r/c summation order.
    for(std::size_t b=0;b<rectangles.size();++b) {
        const auto& r=rectangles[b];
        if(r[0]<0) continue;
        const uint32_t packed = uint32_t(b) | (unit_mw_blk[b] ? 0x80000000u : 0u);
        for(int y=r[0];y<=r[1];++y) for(int x=r[2];x<=r[3];++x)
            result.blocks[cursor[std::size_t(y)*nc+x]++]=packed;
    }
    return result;
}
} // namespace detail

inline StageStats silicon_residual_cleanup(Frames& raw, int h, int w,
                                          const uint8_t* cfa, const Frames& fy,
                                          const Frames& fx, const Frames& mw,
                                          const Noise& noise, const float* scene_rgb,
                                          const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!cfa) throw std::invalid_argument("CFA required");
    StageStats out;
    if (raw.size() < 4 || fy.empty()) return out;
    float dither = 0;
    for (std::size_t n = 1; n < raw.size(); ++n)
        for (std::size_t i = 0; i < fy[n].size(); i += std::max<std::size_t>(1, fy[n].size() / 4096))
            dither = std::max(dither, 2 * std::hypot(fy[n][i], fx[n][i]));
    if (dither < 4) return out;
    const int hh = h / 2, wh = w / 2;
    bool is_bayer = true;
    for (int r = 0; r < 6 && is_bayer; ++r)
        for (int c = 0; c < 6; ++c)
            if (cfa[r * 6 + c] != cfa[(r & 1) * 6 + (c & 1)]) { is_bayer = false; break; }
    std::vector<float> scene;
    if (!scene_rgb) {
        scene.resize(std::size_t(hh) * wh * 3);
        const std::size_t n_frames = raw.size();
        detail::run(hh, execution, [&](int first, int last) {
            std::array<std::vector<float>,3> values;
            if (n_frames > 16) for (auto& v : values) v.reserve(n_frames);
            for (int r = first; r < last; ++r) for (int c = 0; c < wh; ++c) {
                const std::size_t hi = std::size_t(r) * wh + c;
                if (n_frames <= 16) {
                    float vals[3][16];
                    int cnt = 0;
                    for (std::size_t n = 0; n < n_frames; ++n) {
                        if (!mw.empty() && mw[n][hi] < .7f) continue;
                        const float y = 2 * (r + fy[n][hi]);
                        const float x = 2 * (c + fx[n][hi]);
                        if (!detail::inside(y, x, h, w)) continue;
                        const auto v = is_bayer ? detail::raw_rgb_bayer(raw[n], h, w, cfa, y, x)
                                                : detail::raw_rgb(raw[n], h, w, cfa, y, x);
                        vals[0][cnt] = v[0]; vals[1][cnt] = v[1]; vals[2][cnt] = v[2];
                        ++cnt;
                    }
                    float* dst = scene.data() + (std::size_t(r) * wh + c) * 3;
                    if (cnt == 0) {
                        const auto fb = is_bayer ? detail::raw_rgb_bayer(raw[0], h, w, cfa, float(2 * r), float(2 * c))
                                                 : detail::raw_rgb(raw[0], h, w, cfa, float(2 * r), float(2 * c));
                        dst[0] = fb[0]; dst[1] = fb[1]; dst[2] = fb[2];
                    } else {
                        dst[0] = detail::median_small(vals[0], cnt);
                        dst[1] = detail::median_small(vals[1], cnt);
                        dst[2] = detail::median_small(vals[2], cnt);
                    }
                } else {
                    for (auto& v : values) v.clear();
                    for (std::size_t n = 0; n < n_frames; ++n) {
                        if (!mw.empty() && mw[n][hi] < .7f) continue;
                        const float y = 2 * (r + fy[n][hi]);
                        const float x = 2 * (c + fx[n][hi]);
                        if (!detail::inside(y, x, h, w)) continue;
                        const auto v = is_bayer ? detail::raw_rgb_bayer(raw[n], h, w, cfa, y, x)
                                                : detail::raw_rgb(raw[n], h, w, cfa, y, x);
                        for (int k = 0; k < 3; ++k) values[k].push_back(v[k]);
                    }
                    const auto fallback = values[0].empty()
                        ? (is_bayer ? detail::raw_rgb_bayer(raw[0], h, w, cfa, float(2 * r), float(2 * c))
                                    : detail::raw_rgb(raw[0], h, w, cfa, float(2 * r), float(2 * c)))
                        : std::array<float,3>{};
                    for (int k = 0; k < 3; ++k) scene[(std::size_t(r) * wh + c) * 3 + k] =
                        values[k].empty() ? fallback[k] : detail::median(values[k]);
                }
            }
        });
    }
    auto predicted = [&](float y, float x, int color) {
        if (scene_rgb)
            return detail::project_tent(scene_rgb, y, x, 2 * h, 2 * w, color);
        return detail::sample(scene.data(), hh, wh, y * .5f, x * .5f, 3, color);
    };
    Frames residual_cache(raw.size(),std::vector<float>(std::size_t(h)*w));
    const bool accelerated=!scene_rgb && execution.sensor_residual &&
        execution.sensor_residual(scene.data(),residual_cache);
    if(!accelerated) {
        std::vector<uint8_t> zero_flow(raw.size(), 1), high_mw(raw.size(), 1);
        for (std::size_t n = 0; n < raw.size(); ++n) {
            if (!fy.empty()) {
                for (std::size_t i = 0; i < fy[n].size(); ++i)
                    if (fy[n][i] != 0.0f || fx[n][i] != 0.0f) { zero_flow[n] = 0; break; }
            }
            if (!mw.empty()) {
                for (float m : mw[n])
                    if (!(m >= .7f)) { high_mw[n] = 0; break; }
            }
        }
        const float* scene_ptr = scene.empty() ? nullptr : scene.data();
        const float sy_hi = float(hh - 2), sx_hi = float(wh - 2);
        detail::run(int(raw.size()*h),execution,[&](int first,int last) {
            for(int row=first;row<last;++row) {
                const size_t n=size_t(row)/h;
                const int r=row%h;
                const uint8_t* rcfa = cfa + (r % 6) * 6;
                const float* raw_row = raw[n].data() + std::size_t(r) * w;
                float* res_row = residual_cache[n].data() + std::size_t(r) * w;
                const bool zf = zero_flow[n] != 0;
                const bool hmw = high_mw[n] != 0;
                const float* mw_n = (!hmw && !mw.empty()) ? mw[n].data() : nullptr;
                auto eval_pixel = [&](int c, int k, float ry, float rx) {
                    if (!zf && !detail::inside(ry, rx, h, w)) {
                        res_row[c] = std::numeric_limits<float>::quiet_NaN();
                        return;
                    }
                    const float sy = 0.5f * ry, sx = 0.5f * rx;
                    if (mw_n && detail::sample(mw_n, hh, wh, sy, sx) < .7f) {
                        res_row[c] = std::numeric_limits<float>::quiet_NaN();
                        return;
                    }
                    const float v = raw_row[c];
                    if (!scene_rgb && scene_ptr && sy >= 1.0f && sy < sy_hi && sx >= 1.0f && sx < sx_hi) {
                        const int r0 = int(sy), c0 = int(sx);
                        const float ty = sy - float(r0), tx = sx - float(c0);
                        const float oty = 1.0f - ty, otx = 1.0f - tx;
                        const std::size_t s_str = std::size_t(wh) * 3;
                        const float* sr0 = scene_ptr + std::size_t(r0) * s_str + k;
                        const float* sr1 = sr0 + s_str;
                        const float a00 = sr0[std::size_t(c0) * 3], a01 = sr0[std::size_t(c0 + 1) * 3];
                        const float a10 = sr1[std::size_t(c0) * 3], a11 = sr1[std::size_t(c0 + 1) * 3];
                        const float h0 = otx * a00 + tx * a01;
                        const float h1 = otx * a10 + tx * a11;
                        const float estimate = oty * h0 + ty * h1;
                        if (!std::isfinite(v + estimate) || estimate >= .15f) {
                            res_row[c] = std::numeric_limits<float>::quiet_NaN();
                            return;
                        }
                        const float var4 = 4.0f * noise.variance(estimate);
                        float pred_xp, pred_xm, pred_yp, pred_ym;
                        if (tx < 0.5f) {
                            const float tx_s = tx + 0.5f, otx_s = 1.0f - tx_s;
                            const float a0m = sr0[std::size_t(c0 - 1) * 3], a1m = sr1[std::size_t(c0 - 1) * 3];
                            pred_xp = oty * (otx_s * a00 + tx_s * a01) + ty * (otx_s * a10 + tx_s * a11);
                            pred_xm = oty * (otx_s * a0m + tx_s * a00) + ty * (otx_s * a1m + tx_s * a10);
                        } else {
                            const float tx_s = tx - 0.5f, otx_s = 1.0f - tx_s;
                            const float a0p = sr0[std::size_t(c0 + 2) * 3], a1p = sr1[std::size_t(c0 + 2) * 3];
                            pred_xm = oty * (otx_s * a00 + tx_s * a01) + ty * (otx_s * a10 + tx_s * a11);
                            pred_xp = oty * (otx_s * a01 + tx_s * a0p) + ty * (otx_s * a11 + tx_s * a1p);
                        }
                        if (ty < 0.5f) {
                            const float ty_s = ty + 0.5f, oty_s = 1.0f - ty_s;
                            const float* srm1 = sr0 - s_str;
                            const float hm1 = otx * srm1[std::size_t(c0) * 3] + tx * srm1[std::size_t(c0 + 1) * 3];
                            pred_yp = oty_s * h0 + ty_s * h1;
                            pred_ym = oty_s * hm1 + ty_s * h0;
                        } else {
                            const float ty_s = ty - 0.5f, oty_s = 1.0f - ty_s;
                            const float* sr2 = sr1 + s_str;
                            const float hp2 = otx * sr2[std::size_t(c0) * 3] + tx * sr2[std::size_t(c0 + 1) * 3];
                            pred_ym = oty_s * h0 + ty_s * h1;
                            pred_yp = oty_s * h1 + ty_s * hp2;
                        }
                        const float gx = 0.5f * (pred_xp - pred_xm);
                        const float gy = 0.5f * (pred_yp - pred_ym);
                        res_row[c] = (gx * gx + gy * gy < var4)
                            ? (v - estimate)
                            : std::numeric_limits<float>::quiet_NaN();
                        return;
                    }
                    const float estimate = predicted(ry, rx, k);
                    if (!std::isfinite(v + estimate) || estimate >= .15f) {
                        res_row[c] = std::numeric_limits<float>::quiet_NaN();
                        return;
                    }
                    const float var4 = 4.0f * noise.variance(estimate);
                    const float gx = .5f * (predicted(ry, rx + 1, k) - predicted(ry, rx - 1, k));
                    const float gy = .5f * (predicted(ry + 1, rx, k) - predicted(ry - 1, rx, k));
                    res_row[c] = (gx * gx + gy * gy < var4)
                        ? (v - estimate)
                        : std::numeric_limits<float>::quiet_NaN();
                };
                int cm = 0;
                int c = 0;
                if (!zf && !fy.empty()) {
                    const float* py = fy[n].data();
                    const float* px = fx[n].data();
                    for (; c + 4 <= w; c += 4) {
                        float ry4[4], rx4[4];
                        detail::inverse_flow4(py, px, hh, wh, float(r), float(c), ry4, rx4);
                        for (int l = 0; l < 4; ++l) {
                            const int k = rcfa[cm];
                            if (++cm == 6) cm = 0;
                            eval_pixel(c + l, k, ry4[l], rx4[l]);
                        }
                    }
                }
                for (; c < w; ++c) {
                    const int k = rcfa[cm];
                    if (++cm == 6) cm = 0;
                    float ry = float(r), rx = float(c);
                    if (!zf) detail::inverse_flow(fy, fx, n, hh, wh, float(r), float(c), ry, rx);
                    eval_pixel(c, k, ry, rx);
                }
            }
        });
    }
    std::vector<std::vector<float>> row(raw.size(), std::vector<float>(h)),
                                    col(raw.size(), std::vector<float>(w));
    double hp_weights[97];
    for (int d = -48; d <= 48; ++d) hp_weights[d + 48] = std::exp(-.5 * d * d / (16 * 16));
    auto highpass = [&hp_weights](std::vector<float>& v, const std::vector<unsigned char>& valid) {
        const auto source = v;
        const int sz = int(v.size());
        for (int i = 0; i < sz; ++i) {
            if (!valid[i]) { v[i] = 0; continue; }
            double sum = 0, den = 0;
            const int d0 = std::max(-48, -i), d1 = std::min(48, sz - 1 - i);
            for (int d = d0; d <= d1; ++d) {
                const int j = i + d;
                if (!valid[j]) continue;
                const double weight = hp_weights[d + 48];
                sum += weight * source[j]; den += weight;
            }
            v[i] = source[i] - float(sum / std::max(1e-20, den));
        }
    };
    {
        const int n_frames = int(raw.size());
        std::vector<std::vector<unsigned char>> row_valid(n_frames, std::vector<unsigned char>(h, 0));
        detail::run(n_frames * h, execution, [&](int first, int last) {
            std::vector<float> values;
            values.reserve((w + 1) / 2);
            for (int idx = first; idx < last; ++idx) {
                const int n = idx / h, r = idx % h;
                values.clear();
                const float* rptr = residual_cache[n].data() + std::size_t(r) * w;
                for (int c = 0; c < w; c += 2) {
                    const float v = rptr[c];
                    if (std::isfinite(v)) values.push_back(v);
                }
                if (values.size() >= 8) { row[n][r] = detail::median(values); row_valid[n][r] = 1; }
            }
        });
        for (int n = 0; n < n_frames; ++n) highpass(row[n], row_valid[n]);

        constexpr int kColStrip = 128;
        const int n_strips = (w + kColStrip - 1) / kColStrip;
        std::vector<std::vector<unsigned char>> col_valid(n_frames, std::vector<unsigned char>(w, 0));
        detail::run(n_frames * n_strips, execution, [&](int first, int last) {
            std::array<std::vector<float>, kColStrip> buckets;
            for (auto& b : buckets) b.reserve((h + 1) / 2);
            for (int idx = first; idx < last; ++idx) {
                const int n = idx / n_strips, s = idx % n_strips;
                const int c0 = s * kColStrip, c1 = std::min(w, c0 + kColStrip), cw = c1 - c0;
                for (int dc = 0; dc < cw; ++dc) buckets[dc].clear();
                for (int r = 0; r < h; r += 2) {
                    const float rv = row[n][r];
                    const float* rptr = residual_cache[n].data() + std::size_t(r) * w + c0;
                    for (int dc = 0; dc < cw; ++dc) {
                        const float v = rptr[dc];
                        if (std::isfinite(v)) buckets[dc].push_back(v - rv);
                    }
                }
                for (int dc = 0; dc < cw; ++dc) {
                    if (buckets[dc].size() >= 8) {
                        col[n][c0 + dc] = detail::median(buckets[dc]);
                        col_valid[n][c0 + dc] = 1;
                    }
                }
            }
        });
        for (int n = 0; n < n_frames; ++n) highpass(col[n], col_valid[n]);
    }
    const float hot_sigma = std::sqrt(noise.variance(.075f));
    const float hot_thr = 4 * hot_sigma;
    const std::size_t n_frames = raw.size();
    std::vector<const float*> res_ptrs(n_frames), col_ptrs(n_frames);
    std::vector<float*> raw_ptrs(n_frames);
    for (std::size_t n = 0; n < n_frames; ++n) {
        res_ptrs[n] = residual_cache[n].data();
        col_ptrs[n] = col[n].data();
        raw_ptrs[n] = raw[n].data();
    }
    std::vector<StageStats> row_stats(h);
    detail::run(h, execution, [&](int first, int last) {
        std::vector<float> values_vec, cached_vec, row_r_vec;
        std::vector<unsigned char> valid_vec;
        if (n_frames > 16) {
            values_vec.reserve(n_frames);
            cached_vec.resize(n_frames);
            row_r_vec.resize(n_frames);
            valid_vec.resize(n_frames);
        }
        float values_s[16], cached_s[16], row_r_s[16];
        unsigned char valid_s[16];
        float* cached_p = (n_frames <= 16) ? cached_s : cached_vec.data();
        float* row_r = (n_frames <= 16) ? row_r_s : row_r_vec.data();
        unsigned char* valid_p = (n_frames <= 16) ? valid_s : valid_vec.data();
        for (int r = first; r < last; ++r) {
            for (std::size_t n = 0; n < n_frames; ++n) row_r[n] = row[n][r];
            double r_before = 0, r_after = 0, r_rms = 0;
            std::size_t r_samples = 0, r_changed = 0;
            const std::size_t row_off = std::size_t(r) * w;
            auto scalar_col = [&](int c) {
                const std::size_t pix = row_off + c;
                int valid_cnt = 0, above_thr = 0;
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const float cv = res_ptrs[n][pix];
                    cached_p[n] = cv;
                    if (std::isfinite(cv)) {
                        valid_p[n] = 1;
                        ++valid_cnt;
                        const float dv = cv - row_r[n] - col_ptrs[n][c];
                        if (std::fabs(dv) > hot_thr) ++above_thr;
                    } else {
                        valid_p[n] = 0;
                    }
                }
                float hot = 0;
                if (std::size_t(valid_cnt) * 4 >= n_frames * 3 && valid_cnt >= 3 &&
                    above_thr >= valid_cnt / 2) {
                    if (n_frames <= 16) {
                        int k = 0;
                        for (std::size_t n = 0; n < n_frames; ++n)
                            if (valid_p[n]) values_s[k++] = cached_p[n] - row_r[n] - col_ptrs[n][c];
                        float tmp_m[16];
                        std::copy_n(values_s, valid_cnt, tmp_m);
                        const float center = detail::median_small(tmp_m, valid_cnt);
                        const float tol = std::max(3 * hot_sigma, .35f * std::fabs(center));
                        int agree = 0;
                        for (int i = 0; i < valid_cnt; ++i)
                            if (values_s[i] * center > 0 && std::fabs(values_s[i] - center) < tol) ++agree;
                        if (std::fabs(center) > hot_thr && agree * 4 >= valid_cnt * 3) hot = center;
                    } else {
                        values_vec.clear();
                        for (std::size_t n = 0; n < n_frames; ++n)
                            if (valid_p[n]) values_vec.push_back(cached_p[n] - row_r[n] - col_ptrs[n][c]);
                        const float center = detail::median(values_vec);
                        const float tol = std::max(3 * hot_sigma, .35f * std::fabs(center));
                        int agree = 0;
                        for (float v : values_vec)
                            if (v * center > 0 && std::fabs(v - center) < tol) ++agree;
                        if (std::fabs(center) > hot_thr && agree * 4 >= valid_cnt * 3) hot = center;
                    }
                }
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const float correction = row_r[n] + col_ptrs[n][c] + hot;
                    if (valid_p[n]) {
                        const float v = cached_p[n];
                        r_before += double(v) * v;
                        const float d = v - correction;
                        r_after += double(d) * d;
                        ++r_samples;
                    }
                    raw_ptrs[n][pix] -= correction;
                    r_rms += double(correction) * correction;
                    if (correction != 0) ++r_changed;
                }
            };
            int c = 0;
#if defined(__aarch64__)
            const float32x4_t thr_v = vdupq_n_f32(hot_thr);
            const uint32x4_t exp_mask = vdupq_n_u32(0x7F800000u);
            for (; c + 4 <= w; c += 4) {
                const std::size_t pix = row_off + c;
                uint32x4_t any_hot = vdupq_n_u32(0);
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const float32x4_t cv = vld1q_f32(res_ptrs[n] + pix);
                    const float32x4_t corr = vaddq_f32(vdupq_n_f32(row_r[n]), vld1q_f32(col_ptrs[n] + c));
                    any_hot = vorrq_u32(any_hot, vcgtq_f32(vabdq_f32(cv, corr), thr_v));
                }
                if (vmaxvq_u32(any_hot) != 0) {
                    for (int l = 0; l < 4; ++l) scalar_col(c + l);
                    continue;
                }
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const float32x4_t cv = vld1q_f32(res_ptrs[n] + pix);
                    const float32x4_t corr = vaddq_f32(vdupq_n_f32(row_r[n]), vld1q_f32(col_ptrs[n] + c));
                    const float32x4_t rw = vld1q_f32(raw_ptrs[n] + pix);
                    vst1q_f32(raw_ptrs[n] + pix, vsubq_f32(rw, corr));
                    const uint32x4_t fin = vmvnq_u32(vceqq_u32(vandq_u32(vreinterpretq_u32_f32(cv), exp_mask), exp_mask));
                    float cv_a[4], cr_a[4];
                    uint32_t fn_a[4];
                    vst1q_f32(cv_a, cv); vst1q_f32(cr_a, corr); vst1q_u32(fn_a, fin);
                    for (int l = 0; l < 4; ++l) {
                        const float cor = cr_a[l];
                        if (fn_a[l]) {
                            const float v = cv_a[l];
                            const float d = v - cor;
                            r_before += double(v) * v;
                            r_after += double(d) * d;
                            ++r_samples;
                        }
                        r_rms += double(cor) * cor;
                        if (cor != 0.0f) ++r_changed;
                    }
                }
            }
#elif defined(__AVX2__)
            const __m128 thr_v = _mm_set1_ps(hot_thr);
            const __m128 abs_mask = _mm_castsi128_ps(_mm_set1_epi32(0x7FFFFFFF));
            const __m128i exp_mask = _mm_set1_epi32(0x7F800000);
            for (; c + 4 <= w; c += 4) {
                const std::size_t pix = row_off + c;
                __m128 any_hot = _mm_setzero_ps();
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const __m128 cv = _mm_loadu_ps(res_ptrs[n] + pix);
                    const __m128 corr = _mm_add_ps(_mm_set1_ps(row_r[n]), _mm_loadu_ps(col_ptrs[n] + c));
                    const __m128 ad = _mm_and_ps(_mm_sub_ps(cv, corr), abs_mask);
                    any_hot = _mm_or_ps(any_hot, _mm_cmp_ps(ad, thr_v, _CMP_GT_OQ));
                }
                if (_mm_movemask_ps(any_hot) != 0) {
                    for (int l = 0; l < 4; ++l) scalar_col(c + l);
                    continue;
                }
                for (std::size_t n = 0; n < n_frames; ++n) {
                    const __m128 cv = _mm_loadu_ps(res_ptrs[n] + pix);
                    const __m128 corr = _mm_add_ps(_mm_set1_ps(row_r[n]), _mm_loadu_ps(col_ptrs[n] + c));
                    const __m128 rw = _mm_loadu_ps(raw_ptrs[n] + pix);
                    _mm_storeu_ps(raw_ptrs[n] + pix, _mm_sub_ps(rw, corr));
                    const int fin_mask = _mm_movemask_ps(_mm_castsi128_ps(
                        _mm_cmpeq_epi32(_mm_and_si128(_mm_castps_si128(cv), exp_mask), exp_mask))) ^ 0xF;
                    float cv_a[4], cr_a[4];
                    _mm_storeu_ps(cv_a, cv); _mm_storeu_ps(cr_a, corr);
                    for (int l = 0; l < 4; ++l) {
                        const float cor = cr_a[l];
                        if ((fin_mask >> l) & 1) {
                            const float v = cv_a[l];
                            const float d = v - cor;
                            r_before += double(v) * v;
                            r_after += double(d) * d;
                            ++r_samples;
                        }
                        r_rms += double(cor) * cor;
                        if (cor != 0.0f) ++r_changed;
                    }
                }
            }
#endif
            for (; c < w; ++c) scalar_col(c);
            row_stats[r].before = r_before;
            row_stats[r].after = r_after;
            row_stats[r].rms_update = r_rms;
            row_stats[r].samples = r_samples;
            row_stats[r].changed = r_changed;
        }
    });
    for (auto& s : row_stats) detail::add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, std::size_t(h) * w * raw.size()));
    out.peak_scratch_bytes = (scene.size() + raw.size() * (std::size_t(h)*w+h+w+1)) * sizeof(float);
    return out;
}

inline StageStats compact_ibp(const Frames& raw, int h, int w, const uint8_t* cfa,
                              const Frames& fy, const Frames& fx, const Frames& mw,
                              const Noise& noise, float* rgb, int iterations,
                              const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!rgb || !cfa) throw std::invalid_argument("RGB and CFA required");
    StageStats out;
    iterations = std::max(0, std::min(2, iterations));
    if (!iterations) return out;
    const auto index_start=std::chrono::steady_clock::now();
    const int h2 = 2 * h, w2 = 2 * w, tile = std::max(16, std::min(128, execution.tile));
    const int hh = h / 2, wh = w / 2;
    const int nr = (h2 + tile - 1) / tile, nc = (w2 + tile - 1) / tile;
    std::vector<std::array<float, 4>> bounds(raw.size(), {0, 0, 0, 0});
    if (!fy.empty()) for (std::size_t n = 0; n < raw.size(); ++n) {
        bounds[n]={2*fy[n][0],2*fy[n][0],2*fx[n][0],2*fx[n][0]};
        for (std::size_t i = 0; i < fy[n].size(); ++i) {
            if (!std::isfinite(fy[n][i]) || !std::isfinite(fx[n][i]))
                throw std::invalid_argument("nonfinite flow");
            bounds[n][0] = std::min(bounds[n][0], 2 * fy[n][i]);
            bounds[n][1] = std::max(bounds[n][1], 2 * fy[n][i]);
            bounds[n][2] = std::min(bounds[n][2], 2 * fx[n][i]);
            bounds[n][3] = std::max(bounds[n][3], 2 * fx[n][i]);
        }
    }
    std::vector<uint8_t> frame_unit_mw(raw.size(), 1), frame_all_valid(raw.size(), 0);
    for (std::size_t n = 0; n < raw.size(); ++n) {
        if (!mw.empty()) {
            for (float m : mw[n]) {
                if (!(m >= 1.0f)) { frame_unit_mw[n] = 0; break; }
            }
        }
        const float sat_n = noise.frame_saturation_levels ? noise.frame_saturation_levels[n] : std::numeric_limits<float>::infinity();
        frame_all_valid[n] = detail::plane_all_finite_lt(raw[n], sat_n) ? 1 : 0;
    }
    const detail::InverseFlowCache mapping{bounds,mw.empty()?nullptr:&mw};
    std::vector<detail::IbpSourceIndex> source_index(raw.size());
    const double span=tile*.5+6;
    for(std::size_t n=0;n<raw.size();++n) {
        const auto& b=bounds[n];
        const double area=(span+double(b[1])-b[0])*(span+double(b[3])-b[2]);
        if(!std::isfinite(area)||area>1.25*span*span) {
            source_index[n]=detail::index_ibp_sources(raw,n,h,w,fy,fx,mw,noise,tile,execution,&mapping,frame_all_valid[n]!=0);
            out.index_bytes+=source_index[n].offsets.size()*sizeof(std::size_t)+
                source_index[n].blocks.size()*sizeof(uint32_t);
        }
    }
    const auto before_start=std::chrono::steady_clock::now();
    out.worker_seconds[0]=std::chrono::duration<double>(before_start-index_start).count();
    out.before = detail::forward_error(raw, h, w, cfa, fy, fx, mw, noise, rgb, out.samples, execution, &mapping, &frame_all_valid, &frame_unit_mw);
    const auto update_start=std::chrono::steady_clock::now();
    out.worker_seconds[1]=std::chrono::duration<double>(update_start-before_start).count();
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        // Four-color block coordinate descent keeps halo reads race-free.
        for (int color = 0; color < 4; ++color) detail::run(nr * nc, execution, [&](int first, int last) {
            std::vector<float> gradient, mass;
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != color) continue;
                StageStats local_stat = stats[ti];
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h2 - r0), tw = std::min(tile, w2 - c0);
                const std::size_t plane_sz = std::size_t(th) * tw;
                const std::size_t gsize = plane_sz * 3;
                gradient.assign(gsize, 0.0f);
                mass.assign(gsize, 0.0f);
                for (std::size_t n = 0; n < raw.size(); ++n) {
                    const float* rframe = raw[n].data();
                    const bool const_shift = (bounds[n][0] == bounds[n][1] && bounds[n][2] == bounds[n][3]);
                    const float shift_y = bounds[n][0], shift_x = bounds[n][2];
                    const bool all_valid = frame_all_valid[n] != 0;
                    const float sat_n = noise.frame_saturation_levels ? noise.frame_saturation_levels[n] : std::numeric_limits<float>::infinity();
                    const float rv_n = noise.frame_read_variances ? noise.frame_read_variances[n] : noise.read_variance;
                    const float sh_n = noise.frame_shot_coefficients ? noise.frame_shot_coefficients[n] : noise.shot;
                    const float* py = fy.empty() ? nullptr : fy[n].data();
                    const float* px = fx.empty() ? nullptr : fx[n].data();
                    const float* mw_n = mw.empty() ? nullptr : mw[n].data();
                    auto accumulate_resolved = [&](int ch, float v, float ry, float rx, bool known_unit_mw) {
                        if (!detail::inside(ry, rx, h, w) || 2 * ry < r0 - 2 ||
                            2 * ry > r0 + th + 1 || 2 * rx < c0 - 2 || 2 * rx > c0 + tw + 1) return;
                        const float gate = (known_unit_mw || !mw_n) ? 1.0f :
                            detail::clamp01(detail::sample(mw_n, hh, wh, ry * .5f, rx * .5f));
                        if (gate <= 0) return;
                        const float iw = gate / std::max(1e-12f, rv_n + sh_n * std::max(0.0f, v));
                        const float y2 = 2 * ry, x2 = 2 * rx;
                        const int fr0 = int(std::floor(y2)) - 1, fc0 = int(std::floor(x2)) - 1;
                        float* grad_ch = gradient.data() + std::size_t(ch) * plane_sz;
                        float* mass_ch = mass.data() + std::size_t(ch) * plane_sz;
                        if (fr0 >= 0 && fr0 + 3 < h2 && fc0 >= 0 && fc0 + 3 < w2) {
                            const float fy_f = y2 - float(fr0 + 1), fx_f = x2 - float(fc0 + 1);
                            const float ky[4] = {0.25f * (1.0f - fy_f), 0.5f - 0.25f * fy_f, 0.25f * (1.0f + fy_f), 0.25f * fy_f};
                            const float kx[4] = {0.25f * (1.0f - fx_f), 0.5f - 0.25f * fx_f, 0.25f * (1.0f + fx_f), 0.25f * fx_f};
                            const std::size_t row_str = std::size_t(w2) * 3;
                            const float* rp0 = rgb + std::size_t(fr0) * row_str + std::size_t(fc0) * 3 + ch;
                            const float* rp1 = rp0 + row_str;
                            const float* rp2 = rp1 + row_str;
                            const float* rp3 = rp2 + row_str;
                            const float proj =
                                ky[0] * (kx[0] * rp0[0] + kx[1] * rp0[3] + kx[2] * rp0[6] + kx[3] * rp0[9]) +
                                ky[1] * (kx[0] * rp1[0] + kx[1] * rp1[3] + kx[2] * rp1[6] + kx[3] * rp1[9]) +
                                ky[2] * (kx[0] * rp2[0] + kx[1] * rp2[3] + kx[2] * rp2[6] + kx[3] * rp2[9]) +
                                ky[3] * (kx[0] * rp3[0] + kx[1] * rp3[3] + kx[2] * rp3[6] + kx[3] * rp3[9]);
                            const float g_scale = iw * (v - proj);
                            if (fr0 >= r0 && fr0 + 3 < r0 + th && fc0 >= c0 && fc0 + 3 < c0 + tw) {
                                float* gp = grad_ch + std::size_t(fr0 - r0) * tw + (fc0 - c0);
                                float* mp = mass_ch + std::size_t(fr0 - r0) * tw + (fc0 - c0);
#if defined(__aarch64__)
                                const float32x4_t kx_v = vld1q_f32(kx);
                                for (int dr = 0; dr < 4; ++dr, gp += tw, mp += tw) {
                                    vst1q_f32(gp, vaddq_f32(vld1q_f32(gp), vmulq_n_f32(kx_v, ky[dr] * g_scale)));
                                    vst1q_f32(mp, vaddq_f32(vld1q_f32(mp), vmulq_n_f32(kx_v, ky[dr] * iw)));
                                }
#elif defined(__AVX2__)
                                const __m128 kx_v = _mm_loadu_ps(kx);
                                for (int dr = 0; dr < 4; ++dr, gp += tw, mp += tw) {
                                    _mm_storeu_ps(gp, _mm_fmadd_ps(kx_v, _mm_set1_ps(ky[dr] * g_scale), _mm_loadu_ps(gp)));
                                    _mm_storeu_ps(mp, _mm_fmadd_ps(kx_v, _mm_set1_ps(ky[dr] * iw), _mm_loadu_ps(mp)));
                                }
#else
                                for (int dr = 0; dr < 4; ++dr, gp += tw, mp += tw) {
                                    const float wy_g = ky[dr] * g_scale;
                                    const float wy_m = ky[dr] * iw;
                                    for (int dc = 0; dc < 4; ++dc) {
                                        gp[dc] += wy_g * kx[dc];
                                        mp[dc] += wy_m * kx[dc];
                                    }
                                }
#endif
                            } else {
                                for (int dr = 0; dr < 4; ++dr) {
                                    const int ly = fr0 - r0 + dr;
                                    if (ly < 0 || ly >= th) continue;
                                    const float wy_g = ky[dr] * g_scale;
                                    const float wy_m = ky[dr] * iw;
                                    float* gp = grad_ch + std::size_t(ly) * tw;
                                    float* mp = mass_ch + std::size_t(ly) * tw;
                                    for (int dc = 0; dc < 4; ++dc) {
                                        const int lx = fc0 - c0 + dc;
                                        if (lx < 0 || lx >= tw) continue;
                                        gp[lx] += wy_g * kx[dc];
                                        mp[lx] += wy_m * kx[dc];
                                    }
                                }
                            }
                            return;
                        }
                        float ky[4], kx[4];
                        for (int d = 0; d < 4; ++d) {
                            ky[d] = std::max(0.0f, 1 - .5f * std::fabs((fr0 + d) - y2));
                            kx[d] = std::max(0.0f, 1 - .5f * std::fabs((fc0 + d) - x2));
                        }
                        float wgt[16];
                        int ly_arr[16], lx_arr[16];
                        const float* ptr[16];
                        int cnt = 0;
                        float sum = 0;
                        for (int dr = 0; dr < 4; ++dr) {
                            const int fr = fr0 + dr;
                            if (fr < 0 || fr >= h2 || ky[dr] <= 0) continue;
                            const float* rgb_row = rgb + std::size_t(fr) * w2 * 3 + ch;
                            for (int dc = 0; dc < 4; ++dc) {
                                const int fc = fc0 + dc;
                                if (fc < 0 || fc >= w2) continue;
                                const float k = ky[dr] * kx[dc];
                                if (k <= 0) continue;
                                wgt[cnt] = k;
                                ly_arr[cnt] = fr - r0;
                                lx_arr[cnt] = fc - c0;
                                ptr[cnt++] = rgb_row + std::size_t(fc) * 3;
                                sum += k;
                            }
                        }
                        if (sum <= 0) return;
                        const float inv_sum = 1 / sum;
                        float proj = 0;
                        for (int k = 0; k < cnt; ++k) {
                            wgt[k] *= inv_sum;
                            proj += wgt[k] * (*ptr[k]);
                        }
                        const float residual = v - proj;
                        for (int k = 0; k < cnt; ++k) {
                            const int ly = ly_arr[k], lx = lx_arr[k];
                            if (ly < 0 || ly >= th || lx < 0 || lx >= tw) continue;
                            const auto local = std::size_t(ly) * tw + lx;
                            grad_ch[local] += wgt[k] * iw * residual;
                            mass_ch[local] += wgt[k] * iw;
                        }
                    };
                    auto accumulate_span = [&](int r, int c_beg, int c_end_excl, const uint8_t* rcfa, bool blk_unit) {
                        if (c_beg >= c_end_excl) return;
                        local_stat.candidate_samples += std::size_t(c_end_excl - c_beg);
                        const float* rrow = rframe + std::size_t(r) * w;
                        int cm = c_beg % 6;
                        int c = c_beg;
                        if (const_shift) {
                            const float ry = float(r) - shift_y;
                            if (ry < 0.0f || ry > float(h - 1) || 2 * ry < r0 - 2 || 2 * ry > r0 + th + 1) return;
                            for (; c < c_end_excl; ++c) {
                                const int ch = rcfa[cm];
                                if (++cm == 6) cm = 0;
                                const float v = rrow[c];
                                if (!all_valid && (!std::isfinite(v) || !(v < sat_n))) continue;
                                accumulate_resolved(ch, v, ry, float(c) - shift_x, blk_unit);
                            }
                            return;
                        }
                        for (; c + 4 <= c_end_excl; c += 4) {
                            if (!all_valid) {
                                const float v0 = rrow[c], v1 = rrow[c + 1], v2 = rrow[c + 2], v3 = rrow[c + 3];
                                const bool ok0 = std::isfinite(v0) && (v0 < sat_n);
                                const bool ok1 = std::isfinite(v1) && (v1 < sat_n);
                                const bool ok2 = std::isfinite(v2) && (v2 < sat_n);
                                const bool ok3 = std::isfinite(v3) && (v3 < sat_n);
                                if (!(ok0 || ok1 || ok2 || ok3)) {
                                    cm += 4; if (cm >= 6) cm -= 6;
                                    continue;
                                }
                                float ry4[4], rx4[4];
                                detail::inverse_flow4(py, px, hh, wh, float(r), float(c), ry4, rx4);
                                const bool oks[4] = {ok0, ok1, ok2, ok3};
                                const float vs[4] = {v0, v1, v2, v3};
                                for (int l = 0; l < 4; ++l) {
                                    const int ch = rcfa[cm];
                                    if (++cm == 6) cm = 0;
                                    if (oks[l]) accumulate_resolved(ch, vs[l], ry4[l], rx4[l], blk_unit);
                                }
                            } else {
                                float ry4[4], rx4[4];
                                detail::inverse_flow4(py, px, hh, wh, float(r), float(c), ry4, rx4);
                                for (int l = 0; l < 4; ++l) {
                                    const int ch = rcfa[cm];
                                    if (++cm == 6) cm = 0;
                                    accumulate_resolved(ch, rrow[c + l], ry4[l], rx4[l], blk_unit);
                                }
                            }
                        }
                        for (; c < c_end_excl; ++c) {
                            const int ch = rcfa[cm];
                            if (++cm == 6) cm = 0;
                            const float v = rrow[c];
                            if (!all_valid && (!std::isfinite(v) || !(v < sat_n))) continue;
                            float ry, rx;
                            detail::inverse_flow(fy, fx, n, hh, wh, float(r), float(c), ry, rx);
                            accumulate_resolved(ch, v, ry, rx, blk_unit);
                        }
                    };
                    const auto& index=source_index[n];
                    const bool fn_unit = frame_unit_mw[n] != 0;
                    if(index.offsets.empty()) {
                        const auto lower = [](float v, int size) {
                            return int(std::clamp(double(std::floor(v))-1, 0.0, double(size)));
                        };
                        const auto upper = [](float v, int size) {
                            return int(std::clamp(double(std::ceil(v))+1, -1.0, double(size-1)));
                        };
                        const int sy0 = lower(.5f * (r0 - 2) + bounds[n][0], h);
                        const int sy1 = upper(.5f * (r0 + th + 2) + bounds[n][1], h);
                        const int sx0 = lower(.5f * (c0 - 2) + bounds[n][2], w);
                        const int sx1 = upper(.5f * (c0 + tw + 2) + bounds[n][3], w);
                        for(int r=sy0;r<=sy1;++r) {
                            const uint8_t* rcfa = cfa + (r % 6) * 6;
                            accumulate_span(r, sx0, sx1 + 1, rcfa, fn_unit);
                        }
                    } else {
                        const int block=detail::IbpSourceIndex::block;
                        std::size_t begin=index.offsets[ti],end=index.offsets[ti+1];
                        while(begin<end) {
                            const int block_row=int(index.blocks[begin] & 0x7FFFFFFFu)/index.columns;
                            std::size_t next=begin+1;
                            while(next<end && int(index.blocks[next] & 0x7FFFFFFFu)/index.columns==block_row) ++next;
                            for(int r=block_row*block;r<std::min(h,(block_row+1)*block);++r) {
                                const uint8_t* rcfa = cfa + (r % 6) * 6;
                                std::size_t k = begin;
                                while (k < next) {
                                    const uint32_t pk = index.blocks[k];
                                    const bool blk_unit = fn_unit || ((pk & 0x80000000u) != 0);
                                    int col_blk = int((pk & 0x7FFFFFFFu) % index.columns);
                                    const int x_beg = col_blk * block;
                                    ++k;
                                    while (k < next) {
                                        const uint32_t pk2 = index.blocks[k];
                                        const bool blk_unit2 = fn_unit || ((pk2 & 0x80000000u) != 0);
                                        const int col_blk2 = int((pk2 & 0x7FFFFFFFu) % index.columns);
                                        if (col_blk2 != col_blk + 1 || blk_unit2 != blk_unit) break;
                                        col_blk = col_blk2;
                                        ++k;
                                    }
                                    accumulate_span(r, x_beg, std::min(w, (col_blk + 1) * block), rcfa, blk_unit);
                                }
                            }
                            begin=next;
                        }
                    }
                }
                for (int r = 0; r < th; ++r) {
                    float* rgb_row = rgb + (std::size_t(r + r0) * w2 + c0) * 3;
                    const std::size_t r_off = std::size_t(r) * tw;
                    for (int c = 0; c < tw; ++c) {
                        for (int channel = 0; channel < 3; ++channel) {
                            const auto pi = std::size_t(channel) * plane_sz + r_off + c;
                            const float m = mass[pi];
                            if (m <= 0) continue;
                            const float delta = .9f * gradient[pi] / m;
                            rgb_row[c * 3 + channel] += delta;
                            local_stat.rms_update += double(delta) * delta;
                            if (delta != 0) ++local_stat.changed;
                        }
                    }
                }
                local_stat.peak_scratch_bytes = gsize * sizeof(float) * 2;
                stats[ti] = local_stat;
            }
        });
    }
    for (const auto& s : stats) detail::add_stats(out, s);
    std::size_t count = 0;
    const auto after_start=std::chrono::steady_clock::now();
    out.worker_seconds[2]=std::chrono::duration<double>(after_start-update_start).count();
    out.after = detail::forward_error(raw, h, w, cfa, fy, fx, mw, noise, rgb, count, execution, &mapping, &frame_all_valid, &frame_unit_mw);
    out.worker_seconds[1]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-after_start).count();
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.changed));
    out.iterations = iterations;
    return out;
}

inline StageStats compact_ibp_1x(const Frames& raw, int h, int w, const uint8_t* cfa,
                                 const Frames& fy, const Frames& fx, const Frames& mw,
                                 const Noise& noise, float* rgb_1x, int iterations,
                                 const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!rgb_1x || !cfa) throw std::invalid_argument("RGB and CFA required");
    StageStats out;
    iterations = std::max(0, std::min(2, iterations));
    if (!iterations) return out;
    const auto index_start = std::chrono::steady_clock::now();
    const int tile = std::max(16, std::min(128, execution.tile));
    const int hh = h / 2, wh = w / 2;
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    std::vector<std::array<float, 4>> bounds(raw.size(), {0, 0, 0, 0});
    if (!fy.empty()) for (std::size_t n = 0; n < raw.size(); ++n) {
        bounds[n] = {2 * fy[n][0], 2 * fy[n][0], 2 * fx[n][0], 2 * fx[n][0]};
        for (std::size_t i = 0; i < fy[n].size(); ++i) {
            if (!std::isfinite(fy[n][i]) || !std::isfinite(fx[n][i]))
                throw std::invalid_argument("nonfinite flow");
            bounds[n][0] = std::min(bounds[n][0], 2 * fy[n][i]);
            bounds[n][1] = std::max(bounds[n][1], 2 * fy[n][i]);
            bounds[n][2] = std::min(bounds[n][2], 2 * fx[n][i]);
            bounds[n][3] = std::max(bounds[n][3], 2 * fx[n][i]);
        }
    }
    std::vector<uint8_t> frame_unit_mw(raw.size(), 1), frame_all_valid(raw.size(), 0);
    for (std::size_t n = 0; n < raw.size(); ++n) {
        if (!mw.empty()) {
            for (float m : mw[n]) {
                if (!(m >= 1.0f)) { frame_unit_mw[n] = 0; break; }
            }
        }
        const float sat_n = noise.frame_saturation_levels ? noise.frame_saturation_levels[n] : std::numeric_limits<float>::infinity();
        frame_all_valid[n] = detail::plane_all_finite_lt(raw[n], sat_n) ? 1 : 0;
    }
    auto project_bilerp_1x = [&](const float* rgb, float ry, float rx, int ch) -> float {
        const float yc = std::max(0.0f, std::min(float(h - 1), ry));
        const float xc = std::max(0.0f, std::min(float(w - 1), rx));
        const int r0 = int(yc), c0 = int(xc);
        const int r1 = std::min(r0 + 1, h - 1), c1 = std::min(c0 + 1, w - 1);
        const float ty = yc - float(r0), tx = xc - float(c0);
        const float* rp0 = rgb + std::size_t(r0) * w * 3 + ch;
        const float* rp1 = rgb + std::size_t(r1) * w * 3 + ch;
        return (1.0f - ty) * ((1.0f - tx) * rp0[std::size_t(c0) * 3] + tx * rp0[std::size_t(c1) * 3]) +
               ty * ((1.0f - tx) * rp1[std::size_t(c0) * 3] + tx * rp1[std::size_t(c1) * 3]);
    };
    auto eval_forward_1x = [&](std::size_t& count) -> double {
        std::vector<double> row_sum(raw.size() * h, 0.0);
        std::vector<std::size_t> row_cnt(row_sum.size(), 0);
        const int stride = std::max(1, execution.diagnostic_stride);
        detail::run(int(row_sum.size()), execution, [&](int begin, int end) {
            for (int row = begin; row < end; ++row) {
                const std::size_t n = std::size_t(row) / h;
                const int r = row % h;
                const uint8_t* rcfa = cfa + (r % 6) * 6;
                const float* rptr = raw[n].data() + std::size_t(r) * w;
                const int first_c = int((stride - (std::size_t(r) * w + n * 19) % stride) % stride);
                const bool all_valid = frame_all_valid[n] != 0;
                const bool unit_mw = frame_unit_mw[n] != 0 || mw.empty();
                const float rv_n = noise.frame_read_variances ? noise.frame_read_variances[n] : noise.read_variance;
                const float sh_n = noise.frame_shot_coefficients ? noise.frame_shot_coefficients[n] : noise.shot;
                double rs = 0.0;
                std::size_t rc = 0;
                for (int c = first_c; c < w; c += stride) {
                    float ry = float(r), rx = float(c);
                    if (!fy.empty()) detail::inverse_flow(fy, fx, n, hh, wh, float(r), float(c), ry, rx);
                    if (!detail::inside(ry, rx, h, w)) continue;
                    const float v = rptr[c];
                    if (!all_valid && (!std::isfinite(v) || !noise.unsaturated(n, v))) continue;
                    const float weight = unit_mw ? 1.0f : detail::clamp01(detail::sample(mw[n].data(), hh, wh, .5f * ry, .5f * rx));
                    if (weight <= 0.0f) continue;
                    const double residual = v - project_bilerp_1x(rgb_1x, ry, rx, rcfa[c % 6]);
                    const float fvar = std::max(1e-12f, rv_n + sh_n * std::max(0.0f, v));
                    rs += residual * residual * weight / fvar;
                    ++rc;
                }
                row_sum[row] = rs;
                row_cnt[row] = rc;
            }
        });
        double sum = 0.0; count = 0;
        for (std::size_t i = 0; i < row_sum.size(); ++i) { sum += row_sum[i]; count += row_cnt[i]; }
        return sum;
    };
    const auto before_start = std::chrono::steady_clock::now();
    out.worker_seconds[0] = std::chrono::duration<double>(before_start - index_start).count();
    out.before = eval_forward_1x(out.samples);
    const auto update_start = std::chrono::steady_clock::now();
    out.worker_seconds[1] = std::chrono::duration<double>(update_start - before_start).count();
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        for (int color = 0; color < 4; ++color) detail::run(nr * nc, execution, [&](int first, int last) {
            std::vector<float> gradient, mass;
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != color) continue;
                StageStats local_stat = stats[ti];
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h - r0), tw = std::min(tile, w - c0);
                const std::size_t plane_sz = std::size_t(th) * tw;
                const std::size_t gsize = plane_sz * 3;
                gradient.assign(gsize, 0.0f);
                mass.assign(gsize, 0.0f);
                for (std::size_t n = 0; n < raw.size(); ++n) {
                    const float* rframe = raw[n].data();
                    const bool const_shift = (bounds[n][0] == bounds[n][1] && bounds[n][2] == bounds[n][3]);
                    const float shift_y = bounds[n][0], shift_x = bounds[n][2];
                    const bool all_valid = frame_all_valid[n] != 0;
                    const bool fn_unit = frame_unit_mw[n] != 0;
                    const float sat_n = noise.frame_saturation_levels ? noise.frame_saturation_levels[n] : std::numeric_limits<float>::infinity();
                    const float rv_n = noise.frame_read_variances ? noise.frame_read_variances[n] : noise.read_variance;
                    const float sh_n = noise.frame_shot_coefficients ? noise.frame_shot_coefficients[n] : noise.shot;
                    const float* py = fy.empty() ? nullptr : fy[n].data();
                    const float* px = fx.empty() ? nullptr : fx[n].data();
                    const float* mw_n = mw.empty() ? nullptr : mw[n].data();
                    auto accum_one = [&](int ch, float v, float ry, float rx) {
                        if (!detail::inside(ry, rx, h, w) || ry < r0 - 1 || ry > r0 + th || rx < c0 - 1 || rx > c0 + tw) return;
                        const float gate = (fn_unit || !mw_n) ? 1.0f : detail::clamp01(detail::sample(mw_n, hh, wh, ry * .5f, rx * .5f));
                        if (gate <= 0.0f) return;
                        const float iw = gate / std::max(1e-12f, rv_n + sh_n * std::max(0.0f, v));
                        const int ir0 = int(std::floor(ry)), ic0 = int(std::floor(rx));
                        const int ir1 = std::min(ir0 + 1, h - 1), ic1 = std::min(ic0 + 1, w - 1);
                        const float fy_f = ry - float(ir0), fx_f = rx - float(ic0);
                        const float wgt[4] = {(1.0f - fy_f) * (1.0f - fx_f), (1.0f - fy_f) * fx_f,
                                              fy_f * (1.0f - fx_f), fy_f * fx_f};
                        const int frs[4] = {ir0, ir0, ir1, ir1};
                        const int fcs[4] = {ic0, ic1, ic0, ic1};
                        float proj = 0.0f;
                        for (int k = 0; k < 4; ++k) proj += wgt[k] * rgb_1x[(std::size_t(frs[k]) * w + fcs[k]) * 3 + ch];
                        const float residual = v - proj;
                        float* grad_ch = gradient.data() + std::size_t(ch) * plane_sz;
                        float* mass_ch = mass.data() + std::size_t(ch) * plane_sz;
                        for (int k = 0; k < 4; ++k) {
                            const int ly = frs[k] - r0, lx = fcs[k] - c0;
                            if (ly < 0 || ly >= th || lx < 0 || lx >= tw || wgt[k] <= 0.0f) continue;
                            const auto local = std::size_t(ly) * tw + lx;
                            grad_ch[local] += wgt[k] * iw * residual;
                            mass_ch[local] += wgt[k] * iw;
                        }
                    };
                    const int sy0 = std::max(0, int(std::floor(float(r0 - 2) + bounds[n][0])));
                    const int sy1 = std::min(h - 1, int(std::ceil(float(r0 + th + 2) + bounds[n][1])));
                    const int sx0 = std::max(0, int(std::floor(float(c0 - 2) + bounds[n][2])));
                    const int sx1 = std::min(w - 1, int(std::ceil(float(c0 + tw + 2) + bounds[n][3])));
                    for (int r = sy0; r <= sy1; ++r) {
                        const uint8_t* rcfa = cfa + (r % 6) * 6;
                        const float* rrow = rframe + std::size_t(r) * w;
                        int cm = sx0 % 6;
                        int c = sx0;
                        if (const_shift) {
                            const float ry = float(r) - shift_y;
                            for (; c <= sx1; ++c) {
                                const int ch = rcfa[cm]; if (++cm == 6) cm = 0;
                                const float v = rrow[c];
                                if (!all_valid && (!std::isfinite(v) || !(v < sat_n))) continue;
                                accum_one(ch, v, ry, float(c) - shift_x);
                            }
                            continue;
                        }
                        if (py && px) {
                            for (; c + 4 <= sx1 + 1; c += 4) {
                                float ry4[4], rx4[4];
                                detail::inverse_flow4(py, px, hh, wh, float(r), float(c), ry4, rx4);
                                for (int l = 0; l < 4; ++l) {
                                    const int ch = rcfa[cm]; if (++cm == 6) cm = 0;
                                    const float v = rrow[c + l];
                                    if (!all_valid && (!std::isfinite(v) || !(v < sat_n))) continue;
                                    accum_one(ch, v, ry4[l], rx4[l]);
                                }
                            }
                        }
                        for (; c <= sx1; ++c) {
                            const int ch = rcfa[cm]; if (++cm == 6) cm = 0;
                            const float v = rrow[c];
                            if (!all_valid && (!std::isfinite(v) || !(v < sat_n))) continue;
                            float ry = float(r), rx = float(c);
                            if (py && px) detail::inverse_flow(fy, fx, n, hh, wh, float(r), float(c), ry, rx);
                            accum_one(ch, v, ry, rx);
                        }
                    }
                }
                for (int r = 0; r < th; ++r) {
                    float* rgb_row = rgb_1x + (std::size_t(r + r0) * w + c0) * 3;
                    const std::size_t r_off = std::size_t(r) * tw;
                    for (int c = 0; c < tw; ++c) {
                        for (int channel = 0; channel < 3; ++channel) {
                            const auto pi = std::size_t(channel) * plane_sz + r_off + c;
                            const float m = mass[pi];
                            if (m <= 0.0f) continue;
                            const float cur = rgb_row[c * 3 + channel];
                            const float prior_prec = 0.35f / std::max(1e-10f, noise.variance(cur));
                            const float delta = 0.65f * gradient[pi] / (m + prior_prec);
                            rgb_row[c * 3 + channel] = std::max(0.0f, cur + delta);
                            local_stat.rms_update += double(delta) * delta;
                            if (delta != 0.0f) ++local_stat.changed;
                        }
                    }
                }
                local_stat.peak_scratch_bytes = gsize * sizeof(float) * 2;
                stats[ti] = local_stat;
            }
        });
    }
    for (const auto& s : stats) detail::add_stats(out, s);
    std::size_t count = 0;
    const auto after_start = std::chrono::steady_clock::now();
    out.worker_seconds[2] = std::chrono::duration<double>(after_start - update_start).count();
    out.after = eval_forward_1x(count);
    out.worker_seconds[1] += std::chrono::duration<double>(std::chrono::steady_clock::now() - after_start).count();
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.changed));
    out.iterations = iterations;
    return out;
}

inline StageStats color_line_refine(float* rgb, int h, int w, const Noise& noise,
                                   float strength, const Execution& execution) {
    detail::shape(h, w);
    if (!rgb) throw std::invalid_argument("RGB required");
    strength = detail::clamp01(strength);
    const int tile = std::max(16, std::min(256, execution.tile));
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    const std::ptrdiff_t row_str = std::ptrdiff_t(w) * 3;
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    for (int color = 0; color < 4; ++color)
        detail::run(nr * nc, execution, [&](int first, int last) {
            std::vector<float> output;
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != color) continue;
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h - r0), tw = std::min(tile, w - c0);
                output.resize(std::size_t(th) * tw * 3);
                double rms_acc = 0.0;
                std::size_t chg_acc = 0;
                auto eval_px = [&](const float* p_ptr, const float* n_up, const float* n_dn,
                                   const float* n_lt, const float* n_rt, float* dst) {
                    const float p0 = p_ptr[0], p1 = p_ptr[1], p2 = p_ptr[2];
                    const float m0 = 0.25f * (n_up[0] + n_dn[0] + n_lt[0] + n_rt[0]);
                    const float m1 = 0.25f * (n_up[1] + n_dn[1] + n_lt[1] + n_rt[1]);
                    const float m2 = 0.25f * (n_up[2] + n_dn[2] + n_lt[2] + n_rt[2]);
                    const float dg_u = n_up[1] - p1, dg_d = n_dn[1] - p1;
                    const float dg_l = n_lt[1] - p1, dg_r = n_rt[1] - p1;
                    const float lum_grad = dg_u * dg_u + dg_d * dg_d + dg_l * dg_l + dg_r * dg_r;
                    const float dc_u0 = n_up[0] - p0 - dg_u, dc_u2 = n_up[2] - p2 - dg_u;
                    const float dc_d0 = n_dn[0] - p0 - dg_d, dc_d2 = n_dn[2] - p2 - dg_d;
                    const float dc_l0 = n_lt[0] - p0 - dg_l, dc_l2 = n_lt[2] - p2 - dg_l;
                    const float dc_r0 = n_rt[0] - p0 - dg_r, dc_r2 = n_rt[2] - p2 - dg_r;
                    const float chroma_grad =
                        dc_u0 * dc_u0 + dc_u2 * dc_u2 + dc_d0 * dc_d0 + dc_d2 * dc_d2 +
                        dc_l0 * dc_l0 + dc_l2 * dc_l2 + dc_r0 * dc_r0 + dc_r2 * dc_r2;
                    const float v8 = 8.0f * noise.variance(p1);
                    const float gate = strength * (lum_grad / (lum_grad + v8)) * (v8 / (v8 + chroma_grad));
                    const float lap0 = p0 - m0, lap1 = p1 - m1, lap2 = p2 - m2;
                    const float iw0 = 1.0f / noise.variance(p0);
                    const float iw1 = 1.0f / noise.variance(p1);
                    const float iw2 = 1.0f / noise.variance(p2);
                    const float shared = (iw0 * lap0 + iw1 * lap1 + iw2 * lap2) / (iw0 + iw1 + iw2);
                    const float d0 = gate * (shared - lap0);
                    const float d1 = gate * (shared - lap1);
                    const float d2 = gate * (shared - lap2);
                    dst[0] = p0 + d0; dst[1] = p1 + d1; dst[2] = p2 + d2;
                    rms_acc += double(d0) * d0 + double(d1) * d1 + double(d2) * d2;
                    chg_acc += std::size_t(d0 != 0.0f) + std::size_t(d1 != 0.0f) + std::size_t(d2 != 0.0f);
                };
                for (int r = 0; r < th; ++r) {
                    const int y = r + r0;
                    const float* row_p = rgb + std::size_t(y) * row_str;
                    const float* row_u = rgb + std::size_t(y > 0 ? y - 1 : 0) * row_str;
                    const float* row_d = rgb + std::size_t(y + 1 < h ? y + 1 : h - 1) * row_str;
                    float* out_row = output.data() + std::size_t(r) * tw * 3;
                    int c = 0;
                    if (c0 == 0 && tw > 0) {
                        const int xr = (w > 1) ? 1 : 0;
                        eval_px(row_p, row_u, row_d, row_p, row_p + xr * 3, out_row);
                        c = 1;
                    }
                    const int c_int_end = (c0 + tw >= w) ? (tw - 1) : tw;
                    for (; c < c_int_end; ++c) {
                        const std::size_t x3 = std::size_t(c0 + c) * 3;
                        eval_px(row_p + x3, row_u + x3, row_d + x3, row_p + x3 - 3, row_p + x3 + 3, out_row + c * 3);
                    }
                    for (; c < tw; ++c) {
                        const int x = c0 + c;
                        const std::size_t x3 = std::size_t(x) * 3;
                        const std::size_t xl3 = std::size_t(x > 0 ? x - 1 : 0) * 3;
                        const std::size_t xr3 = std::size_t(x + 1 < w ? x + 1 : w - 1) * 3;
                        eval_px(row_p + x3, row_u + x3, row_d + x3, row_p + xl3, row_p + xr3, out_row + c * 3);
                    }
                }
                stats[ti].rms_update += rms_acc;
                stats[ti].samples += std::size_t(th) * tw * 3;
                stats[ti].changed += chg_acc;
                for (int r = 0; r < th; ++r)
                    std::copy_n(output.data() + std::size_t(r) * tw * 3, std::size_t(tw) * 3,
                                rgb + (std::size_t(r0 + r) * w + c0) * 3);
                stats[ti].peak_scratch_bytes = output.size() * sizeof(float);
            }
        });
    StageStats out; for (auto& s : stats) detail::add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.samples));
    return out;
}

namespace detail {
inline void hadamard8(float* a) {
    const float u0 = a[0] + a[1], v0 = a[0] - a[1];
    const float u1 = a[2] + a[3], v1 = a[2] - a[3];
    const float u2 = a[4] + a[5], v2 = a[4] - a[5];
    const float u3 = a[6] + a[7], v3 = a[6] - a[7];
    const float b0 = u0 + u1, b1 = v0 + v1, b2 = u0 - u1, b3 = v0 - v1;
    const float b4 = u2 + u3, b5 = v2 + v3, b6 = u2 - u3, b7 = v2 - v3;
    constexpr float s = 0.35355339059327376f;
    a[0] = (b0 + b4) * s; a[1] = (b1 + b5) * s; a[2] = (b2 + b6) * s; a[3] = (b3 + b7) * s;
    a[4] = (b0 - b4) * s; a[5] = (b1 - b5) * s; a[6] = (b2 - b6) * s; a[7] = (b3 - b7) * s;
}
inline void hadamard(float* a, int n) {
    if (n == 8) { hadamard8(a); return; }
    for (int width = 1; width < n; width *= 2)
        for (int base = 0; base < n; base += 2 * width)
            for (int k = 0; k < width; ++k) {
                const float u = a[base + k], v = a[base + k + width];
                a[base + k] = u + v; a[base + k + width] = u - v;
            }
    const float scale = 1 / std::sqrt(float(n));
    for (int i = 0; i < n; ++i) a[i] *= scale;
}
inline void transform4(float* a) {
#if defined(__aarch64__)
    auto trn4 = [](float32x4_t& x0, float32x4_t& x1, float32x4_t& x2, float32x4_t& x3) {
        const float32x4_t t0 = vtrn1q_f32(x0, x1);
        const float32x4_t t1 = vtrn2q_f32(x0, x1);
        const float32x4_t t2 = vtrn1q_f32(x2, x3);
        const float32x4_t t3 = vtrn2q_f32(x2, x3);
        x0 = vreinterpretq_f32_f64(vtrn1q_f64(vreinterpretq_f64_f32(t0), vreinterpretq_f64_f32(t2)));
        x1 = vreinterpretq_f32_f64(vtrn1q_f64(vreinterpretq_f64_f32(t1), vreinterpretq_f64_f32(t3)));
        x2 = vreinterpretq_f32_f64(vtrn2q_f64(vreinterpretq_f64_f32(t0), vreinterpretq_f64_f32(t2)));
        x3 = vreinterpretq_f32_f64(vtrn2q_f64(vreinterpretq_f64_f32(t1), vreinterpretq_f64_f32(t3)));
    };
    float32x4_t x0 = vld1q_f32(a + 0), x1 = vld1q_f32(a + 4);
    float32x4_t x2 = vld1q_f32(a + 8), x3 = vld1q_f32(a + 12);
    trn4(x0, x1, x2, x3);
    const float32x4_t hs0 = vaddq_f32(x0, x1), hd0 = vsubq_f32(x0, x1);
    const float32x4_t hs1 = vaddq_f32(x2, x3), hd1 = vsubq_f32(x2, x3);
    x0 = vaddq_f32(hs0, hs1); x1 = vaddq_f32(hd0, hd1);
    x2 = vsubq_f32(hs0, hs1); x3 = vsubq_f32(hd0, hd1);
    trn4(x0, x1, x2, x3);
    const float32x4_t vs0 = vaddq_f32(x0, x1), vd0 = vsubq_f32(x0, x1);
    const float32x4_t vs1 = vaddq_f32(x2, x3), vd1 = vsubq_f32(x2, x3);
    const float32x4_t q = vdupq_n_f32(0.25f);
    vst1q_f32(a + 0,  vmulq_f32(vaddq_f32(vs0, vs1), q));
    vst1q_f32(a + 4,  vmulq_f32(vaddq_f32(vd0, vd1), q));
    vst1q_f32(a + 8,  vmulq_f32(vsubq_f32(vs0, vs1), q));
    vst1q_f32(a + 12, vmulq_f32(vsubq_f32(vd0, vd1), q));
#elif defined(__AVX2__)
    __m128 x0 = _mm_loadu_ps(a + 0), x1 = _mm_loadu_ps(a + 4);
    __m128 x2 = _mm_loadu_ps(a + 8), x3 = _mm_loadu_ps(a + 12);
    _MM_TRANSPOSE4_PS(x0, x1, x2, x3);
    const __m128 hs0 = _mm_add_ps(x0, x1), hd0 = _mm_sub_ps(x0, x1);
    const __m128 hs1 = _mm_add_ps(x2, x3), hd1 = _mm_sub_ps(x2, x3);
    x0 = _mm_add_ps(hs0, hs1); x1 = _mm_add_ps(hd0, hd1);
    x2 = _mm_sub_ps(hs0, hs1); x3 = _mm_sub_ps(hd0, hd1);
    _MM_TRANSPOSE4_PS(x0, x1, x2, x3);
    const __m128 vs0 = _mm_add_ps(x0, x1), vd0 = _mm_sub_ps(x0, x1);
    const __m128 vs1 = _mm_add_ps(x2, x3), vd1 = _mm_sub_ps(x2, x3);
    const __m128 q = _mm_set1_ps(0.25f);
    _mm_storeu_ps(a + 0,  _mm_mul_ps(_mm_add_ps(vs0, vs1), q));
    _mm_storeu_ps(a + 4,  _mm_mul_ps(_mm_add_ps(vd0, vd1), q));
    _mm_storeu_ps(a + 8,  _mm_mul_ps(_mm_sub_ps(vs0, vs1), q));
    _mm_storeu_ps(a + 12, _mm_mul_ps(_mm_sub_ps(vd0, vd1), q));
#else
    float t[16];
    for (int r = 0; r < 4; ++r) {
        const float* p = a + r * 4;
        const float s0 = p[0] + p[1], d0 = p[0] - p[1];
        const float s1 = p[2] + p[3], d1 = p[2] - p[3];
        t[r * 4 + 0] = s0 + s1; t[r * 4 + 1] = d0 + d1;
        t[r * 4 + 2] = s0 - s1; t[r * 4 + 3] = d0 - d1;
    }
    for (int c = 0; c < 4; ++c) {
        const float s0 = t[0 + c] + t[4 + c], d0 = t[0 + c] - t[4 + c];
        const float s1 = t[8 + c] + t[12 + c], d1 = t[8 + c] - t[12 + c];
        a[0 + c]  = 0.25f * (s0 + s1);
        a[4 + c]  = 0.25f * (d0 + d1);
        a[8 + c]  = 0.25f * (s0 - s1);
        a[12 + c] = 0.25f * (d0 - d1);
    }
#endif
}
inline std::array<float, 9> klt_basis(const std::vector<float>& rgb,
                                     const std::array<float, 3>& scale) {
    double mean[3] = {}, cov[3][3] = {};
    const std::size_t count = rgb.size() / 3;
    const float inv_s0 = 1.0f / scale[0], inv_s1 = 1.0f / scale[1], inv_s2 = 1.0f / scale[2];
    const float* p = rgb.data();
    for (std::size_t i = 0; i < count; ++i) {
        mean[0] += double(p[i * 3 + 0] * inv_s0);
        mean[1] += double(p[i * 3 + 1] * inv_s1);
        mean[2] += double(p[i * 3 + 2] * inv_s2);
    }
    const double inverse_count = 1.0 / double(std::max<std::size_t>(1, count));
    mean[0] *= inverse_count; mean[1] *= inverse_count; mean[2] *= inverse_count;
    double c00 = 0, c01 = 0, c02 = 0, c11 = 0, c12 = 0, c22 = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const double d0 = double(p[i * 3 + 0] * inv_s0) - mean[0];
        const double d1 = double(p[i * 3 + 1] * inv_s1) - mean[1];
        const double d2 = double(p[i * 3 + 2] * inv_s2) - mean[2];
        c00 += d0 * d0; c01 += d0 * d1; c02 += d0 * d2;
        c11 += d1 * d1; c12 += d1 * d2; c22 += d2 * d2;
    }
    cov[0][0] = c00 * inverse_count; cov[0][1] = cov[1][0] = c01 * inverse_count;
    cov[0][2] = cov[2][0] = c02 * inverse_count; cov[1][1] = c11 * inverse_count;
    cov[1][2] = cov[2][1] = c12 * inverse_count; cov[2][2] = c22 * inverse_count;
    std::array<float, 9> basis = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int sweep = 0; sweep < 12; ++sweep) {
        int p = 0, q = 1;
        if (std::fabs(cov[0][2]) > std::fabs(cov[p][q])) { p = 0; q = 2; }
        if (std::fabs(cov[1][2]) > std::fabs(cov[p][q])) { p = 1; q = 2; }
        if (std::fabs(cov[p][q]) < 1e-12) break;
        const double angle = .5 * std::atan2(2 * cov[p][q], cov[q][q] - cov[p][p]);
        const double cs = std::cos(angle), sn = std::sin(angle);
        for (int i = 0; i < 3; ++i) {
            const double a = cov[i][p], b = cov[i][q];
            cov[i][p] = cs * a - sn * b; cov[i][q] = sn * a + cs * b;
        }
        for (int i = 0; i < 3; ++i) {
            const double a = cov[p][i], b = cov[q][i];
            cov[p][i] = cs * a - sn * b; cov[q][i] = sn * a + cs * b;
            const float u = basis[i * 3 + p], v = basis[i * 3 + q];
            basis[i * 3 + p] = float(cs * u - sn * v);
            basis[i * 3 + q] = float(sn * u + cs * v);
        }
    }
    return basis;
}
// Fill produces two independent camera-linear estimates and measured N_eff.
template<class Fill>
inline StageStats denoise_tiles(float* rgb, int h, int w, const Noise& noise,
                                const HalfStackOptions& opt, const Execution& execution,
                                bool independent, Fill fill) {
    shape(h, w);
    if (!rgb) throw std::invalid_argument("RGB required");
    const int tile = 64; // A shared color KLT per 64x64 region.
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    const float strength = clamp01(opt.strength);
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    float hp_arr[16], inv_hp[16], sqrt_inv_hp[16];
    for (int f = 0; f < 16; ++f) {
        hp_arr[f] = opt.kernel_power ? std::max(.02f, opt.kernel_power[f]) : 1.0f;
        inv_hp[f] = 1.0f / hp_arr[f];
        sqrt_inv_hp[f] = std::sqrt(inv_hp[f]);
    }
    const bool avx512 = heraw_cache::use_avx512();
    (void)avx512;
    for (int checker = 0; checker < 4; ++checker)
        run(nr * nc, execution, [&](int first, int last) {
            std::vector<float> input, a, b, neff, ci, ci_w, hsum, ca, cb, output;
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != checker) continue;
                StageStats local_stat{};
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h - r0), tw = std::min(tile, w - c0);
                const int bh = (th + 3) / 4 + 4, bw = (tw + 3) / 4 + 4;
                const int ph = bh * 4, pw = bw * 4;
                auto cost_start=std::chrono::steady_clock::now();
                auto cost_mark=[&](size_t phase) {
                    const auto now=std::chrono::steady_clock::now();
                    local_stat.worker_seconds[phase]+=std::chrono::duration<double>(now-cost_start).count();
                    cost_start=now;
                };
                const std::size_t pixels = std::size_t(ph) * pw;
                input.resize(pixels * 3);
                a.resize(independent ? input.size() : 0);
                b.resize(a.size());
                neff.resize(pixels);
                std::size_t fill_scratch=0;
                std::array<float, 3> scales{};
                const float inv_pixels = 1.0f / float(pixels);
                for (int r = 0; r < ph; ++r) {
                    const int y = clamp(r0 + r - 8, h);
                    const float* rgb_row = rgb + std::size_t(y) * w * 3;
                    for (int c = 0; c < pw; ++c) {
                        const int x = clamp(c0 + c - 8, w);
                        const auto p = std::size_t(r) * pw + c;
                        for (int k = 0; k < 3; ++k) {
                            const float val = rgb_row[std::size_t(x) * 3 + k];
                            input[p * 3 + k] = val;
                            scales[k] += noise.variance(val) * inv_pixels;
                        }
                        if constexpr (!std::is_invocable_v<Fill,int,int,int,int,float*,float*,float*>)
                            fill(y, x, independent?a.data()+p*3:nullptr, independent?b.data()+p*3:nullptr, neff[p]);
                    }
                }
                if constexpr (std::is_invocable_v<Fill,int,int,int,int,float*,float*,float*>)
                    fill_scratch=fill(r0-8,c0-8,ph,pw,a.data(),b.data(),neff.data());
                for (float& s : scales) s = std::sqrt(std::max(1e-12f, s));
                cost_mark(0);
                const auto basis = klt_basis(input, scales);
                std::array<float,9> whitened_basis;
                for(int j=0;j<3;++j) for(int k=0;k<3;++k)
                    whitened_basis[j*3+k]=basis[j*3+k]/scales[j];
                const bool spectrum=independent && opt.phase_balanced;
                ci.resize(input.size());
                ca.resize(spectrum ? input.size() : 0);
                cb.resize(ca.size());
                const float wb00 = whitened_basis[0], wb01 = whitened_basis[1], wb02 = whitened_basis[2];
                const float wb10 = whitened_basis[3], wb11 = whitened_basis[4], wb12 = whitened_basis[5];
                const float wb20 = whitened_basis[6], wb21 = whitened_basis[7], wb22 = whitened_basis[8];
                auto forward = [&](const std::vector<float>& src, std::vector<float>& dst) {
                    if (bh <= 0 || bw <= 0) return;
                    const std::size_t need = std::size_t(bh) * bw * 48;
                    if (dst.size() < need) dst.resize(need);
                    const float* sp = src.data();
                    for (int by = 0; by < bh; ++by) for (int bx = 0; bx < bw; ++bx) {
                        float blk0[16], blk1[16], blk2[16];
                        for (int y = 0; y < 4; ++y) {
                            const float* srow = sp + (std::size_t(by * 4 + y) * pw + bx * 4) * 3;
                            for (int x = 0; x < 4; ++x) {
                                const float s0 = srow[x * 3 + 0], s1 = srow[x * 3 + 1], s2 = srow[x * 3 + 2];
                                const int idx = y * 4 + x;
                                blk0[idx] = wb00 * s0 + wb10 * s1 + wb20 * s2;
                                blk1[idx] = wb01 * s0 + wb11 * s1 + wb21 * s2;
                                blk2[idx] = wb02 * s0 + wb12 * s1 + wb22 * s2;
                            }
                        }
                        transform4(blk0);
                        transform4(blk1);
                        transform4(blk2);
                        float* dp = dst.data() + (std::size_t(by) * bw + bx) * 48;
                        for (int i = 0; i < 16; ++i) {
                            dp[i] = blk0[i];
                            dp[16 + i] = blk1[i];
                            dp[32 + i] = blk2[i];
                        }
                    }
                };
                forward(input, ci);
                if (spectrum) { forward(a, ca); forward(b, cb); }
                const std::size_t n_blocks = std::size_t(bh) * bw;
                ci_w.resize(n_blocks * 16);
#if HERAW_AVX512_ENABLED
                if (avx512) {
                    detail::scale_ci_w_avx512(ci.data(), sqrt_inv_hp, ci_w.data(), n_blocks);
                } else
#endif
                {
                    for (std::size_t bi = 0; bi < n_blocks; ++bi) {
                        const float* c0_ptr = ci.data() + bi * 48;
                        float* cw_ptr = ci_w.data() + bi * 16;
                        for (int f = 0; f < 16; ++f) cw_ptr[f] = c0_ptr[f] * sqrt_inv_hp[f];
                    }
                }
                cost_mark(1);
                output.resize(std::size_t(th) * tw * 3);
                const std::size_t row_stride48 = std::size_t(bw) * 48;
                hsum.resize(3 * row_stride48);
                float* hsum_base = hsum.data();
                auto compute_hsum_row = [&](int r_blk, float* dst_row) {
                    const float* row_ci = ci.data() + std::size_t(r_blk) * row_stride48;
#if HERAW_AVX512_ENABLED
                    if (avx512) {
                        detail::compute_hsum_row_avx512(row_ci, dst_row, bw);
                        return;
                    }
#endif
                    for (int bx = 1; bx < bw - 1; ++bx) {
                        const float* p0 = row_ci + std::size_t(bx - 1) * 48;
                        const float* p1 = p0 + 48;
                        const float* p2 = p1 + 48;
                        float* d = dst_row + std::size_t(bx) * 48;
#if defined(__aarch64__)
                        for (int kf = 0; kf < 48; kf += 4) {
                            const float32x4_t v0 = vld1q_f32(p0 + kf);
                            const float32x4_t v1 = vld1q_f32(p1 + kf);
                            const float32x4_t v2 = vld1q_f32(p2 + kf);
                            vst1q_f32(d + kf, vfmaq_f32(vfmaq_f32(vmulq_f32(v0, v0), v1, v1), v2, v2));
                        }
#elif defined(__AVX2__)
                        for (int kf = 0; kf < 48; kf += 8) {
                            const __m256 v0 = _mm256_loadu_ps(p0 + kf);
                            const __m256 v1 = _mm256_loadu_ps(p1 + kf);
                            const __m256 v2 = _mm256_loadu_ps(p2 + kf);
                            _mm256_storeu_ps(d + kf, _mm256_fmadd_ps(v2, v2, _mm256_fmadd_ps(v1, v1, _mm256_mul_ps(v0, v0))));
                        }
#else
                        for (int kf = 0; kf < 48; ++kf)
                            d[kf] = p0[kf] * p0[kf] + p1[kf] * p1[kf] + p2[kf] * p2[kf];
#endif
                    }
                };
                if (bh >= 5 && bw >= 5) {
                    compute_hsum_row(1, hsum_base + (1 % 3) * row_stride48);
                    compute_hsum_row(2, hsum_base + (2 % 3) * row_stride48);
                }
                for (int by = 2; by < bh - 2; ++by) {
                    compute_hsum_row(by + 1, hsum_base + ((by + 1) % 3) * row_stride48);
                    const float* hs0 = hsum_base + ((by - 1) % 3) * row_stride48;
                    const float* hs1 = hsum_base + (by % 3) * row_stride48;
                    const float* hs2 = hsum_base + ((by + 1) % 3) * row_stride48;
                    for (int bx = 2; bx < bw - 2; ++bx) {
                    const auto block_index = std::size_t(by) * bw + bx;
                    float count = std::numeric_limits<float>::max(), gate = 1;
                    if (!opt.effective_count && !opt.static_weight) {
                        for (int y = 0; y < 4; ++y) {
                            const float* np = neff.data() + std::size_t(by * 4 + y) * pw + bx * 4;
                            count = std::min(count, std::min(std::min(np[0], np[1]), std::min(np[2], np[3])));
                        }
                    } else {
                        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                            const auto p = std::size_t(by * 4 + y) * pw + bx * 4 + x;
                            count = std::min(count, neff[p]);
                            const auto global = std::size_t(clamp(r0 + by * 4 + y - 8, h)) * w +
                                                clamp(c0 + bx * 4 + x - 8, w);
                            if (opt.effective_count) count = std::min(count, opt.effective_count[global]);
                            if (opt.static_weight) gate = std::min(gate, clamp01(opt.static_weight[global]));
                        }
                    }
                    const bool ensemble = independent && opt.phase_balanced && count >= 5 && gate >= .7f;
                    const float analytic = 1 / std::max(1.0f, count);
                    std::pair<float, std::size_t> top8[8];
                    bool use_nonlocal = false;
                    int central = -1;
                    if (count >= 2) {
                        const float max_inf = std::numeric_limits<float>::infinity();
                        const float nl_limit = 64.0f * analytic;
                        for (int m = 0; m < 8; ++m) top8[m] = {max_inf, std::numeric_limits<std::size_t>::max()};
                        int valid_found = 0;
                        {
                        const float* cw_ref = ci_w.data() + block_index * 16;
#if defined(__aarch64__)
                        const float32x4_t ref0 = vld1q_f32(cw_ref + 0);
                        const float32x4_t ref1 = vld1q_f32(cw_ref + 4);
                        const float32x4_t ref2 = vld1q_f32(cw_ref + 8);
                        const float32x4_t ref3 = vld1q_f32(cw_ref + 12);
#elif defined(__AVX2__)
                        const __m128 ref0_128 = _mm_loadu_ps(cw_ref + 0);
                        const __m128 ref1_128 = _mm_loadu_ps(cw_ref + 4);
                        const __m256 ref23_256 = _mm256_loadu_ps(cw_ref + 8);
#endif
                        int rem = 25;
                        for (int dy = -2; dy <= 2 && valid_found + rem >= 8; ++dy) {
                            for (int dx = -2; dx <= 2; ++dx) {
                                --rem;
                                const auto j = std::size_t(by + dy) * bw + bx + dx;
                                const float* cw_j = ci_w.data() + j * 16;
                                const float cutoff = std::min(nl_limit, top8[7].first);
                                float distance = 0.0f;
#if defined(__aarch64__)
                                const float32x4_t d0 = vsubq_f32(vld1q_f32(cw_j + 0), ref0);
                                distance = vaddvq_f32(vmulq_f32(d0, d0));
                                if (distance > cutoff) { if (valid_found + rem < 8) break; continue; }
                                const float32x4_t d1 = vsubq_f32(vld1q_f32(cw_j + 4), ref1);
                                const float32x4_t d2 = vsubq_f32(vld1q_f32(cw_j + 8), ref2);
                                const float32x4_t d3 = vsubq_f32(vld1q_f32(cw_j + 12), ref3);
                                const float32x4_t acc = vfmaq_f32(vfmaq_f32(vmulq_f32(d1, d1), d2, d2), d3, d3);
                                distance += vaddvq_f32(acc);
#elif defined(__AVX2__)
                                const __m128 d0 = _mm_sub_ps(_mm_loadu_ps(cw_j + 0), ref0_128);
                                __m128 sq0 = _mm_mul_ps(d0, d0);
                                sq0 = _mm_hadd_ps(sq0, sq0);
                                sq0 = _mm_hadd_ps(sq0, sq0);
                                distance = _mm_cvtss_f32(sq0);
                                if (distance > cutoff) { if (valid_found + rem < 8) break; continue; }
                                const __m128 d1 = _mm_sub_ps(_mm_loadu_ps(cw_j + 4), ref1_128);
                                const __m256 d23 = _mm256_sub_ps(_mm256_loadu_ps(cw_j + 8), ref23_256);
                                const __m256 sq23 = _mm256_mul_ps(d23, d23);
                                __m128 sum128 = _mm_fmadd_ps(d1, d1,
                                    _mm_add_ps(_mm256_castps256_ps128(sq23), _mm256_extractf128_ps(sq23, 1)));
                                sum128 = _mm_hadd_ps(sum128, sum128);
                                sum128 = _mm_hadd_ps(sum128, sum128);
                                distance += _mm_cvtss_f32(sum128);
#else
                                const float d0 = cw_j[0] - cw_ref[0];
                                distance = d0 * d0;
                                if (distance > cutoff) { if (valid_found + rem < 8) break; continue; }
                                for (int f = 1; f < 4; ++f) {
                                    const float d = cw_j[f] - cw_ref[f];
                                    distance += d * d;
                                }
                                if (distance > cutoff) { if (valid_found + rem < 8) break; continue; }
                                for (int f = 4; f < 16; ++f) {
                                    const float d = cw_j[f] - cw_ref[f];
                                    distance += d * d;
                                }
#endif
                                if (distance <= nl_limit) ++valid_found;
                                if (distance < cutoff ||
                                    (distance == cutoff && j < top8[7].second)) {
                                    int pos = 7;
                                    while (pos > 0 && (distance < top8[pos - 1].first ||
                                           (distance == top8[pos - 1].first && j < top8[pos - 1].second))) {
                                        top8[pos] = top8[pos - 1];
                                        --pos;
                                    }
                                    top8[pos] = {distance, j};
                                }
                                if (valid_found + rem < 8) break;
                            }
                        }
                        }
                        use_nonlocal = (top8[7].first <= nl_limit);
                        if (use_nonlocal) {
                            for (int j = 0; j < 8; ++j)
                                if (top8[j].second == block_index) { central = j; break; }
                        }
                    }
                    float channels[3][16];
                    const std::size_t bx48 = std::size_t(bx) * 48;
                    for (int k = 0; k < 3; ++k) {
                        float filtered[16];
                        if (ensemble) {
                            for (int f = 0; f < 16; ++f) {
                                const auto idx = block_index * 48 + k * 16 + f;
                                const float hp = hp_arr[f];
                                double signal = 0, nv = 0;
                                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                                    const auto j = (std::size_t(by + dy) * bw + bx + dx) * 48 + k * 16 + f;
                                    const double difference = ca[j] - cb[j];
                                    nv += .25 * difference * difference;
                                    signal += double(ca[j]) * cb[j];
                                }
                                nv = (nv * inv_hp[f] + 4 * analytic) / 13;
                                signal = std::max(0.0, signal / (9 * double(hp)));
                                const float gain = f == 0 ? 1 : float(signal / (signal + nv + 1e-20));
                                filtered[f] = ci[idx] * (1 - strength * gate * (1 - gain));
                                if (f) {
                                    local_stat.noise_variance += nv * hp * scales[k] * scales[k];
                                    local_stat.signal_power += signal * hp * scales[k] * scales[k];
                                    ++local_stat.samples;
                                }
                            }
                        } else {
                            const float* s0 = hs0 + bx48 + k * 16;
                            const float* s1 = hs1 + bx48 + k * 16;
                            const float* s2 = hs2 + bx48 + k * 16;
                            const float* ci_p = ci.data() + block_index * 48 + k * 16;
                            filtered[0] = ci_p[0];
                            const float sk2 = scales[k] * scales[k];
                            const float sg = strength * gate;
                            double sp_sum = 0;
                            for (int f = 1; f < 16; ++f) {
                                const float sig_sq = s0[f] + s1[f] + s2[f];
                                const double signal = std::max(0.0, double(sig_sq) * (1.0 / 9.0) * inv_hp[f] - analytic);
                                const float gain = float(signal / (signal + analytic + 1e-20));
                                filtered[f] = ci_p[f] * (1.0f - sg * (1.0f - gain));
                                sp_sum += signal * hp_arr[f];
                            }
                            local_stat.signal_power += sp_sum * sk2;
                            for (int f = 1; f < 16; ++f)
                                local_stat.noise_variance += double(analytic) * hp_arr[f] * sk2;
                            local_stat.samples += 15;
                        }
                        if (use_nonlocal && central >= 0) {
                            constexpr int freqs[4] = {0, 1, 4, 5};
                            float g4[8][4];
                            for (int j = 0; j < 8; ++j) {
                                const float* cp = ci.data() + top8[j].second * 48 + k * 16;
                                g4[j][0] = cp[0]; g4[j][1] = cp[1]; g4[j][2] = cp[4]; g4[j][3] = cp[5];
                            }
#if defined(__aarch64__)
                            auto h8_4_simd = [](float32x4_t& x0, float32x4_t& x1, float32x4_t& x2, float32x4_t& x3,
                                                float32x4_t& x4, float32x4_t& x5, float32x4_t& x6, float32x4_t& x7) {
                                const float32x4_t s = vdupq_n_f32(0.35355339059327376f);
                                const float32x4_t u0 = vaddq_f32(x0, x1), v0 = vsubq_f32(x0, x1);
                                const float32x4_t u1 = vaddq_f32(x2, x3), v1 = vsubq_f32(x2, x3);
                                const float32x4_t u2 = vaddq_f32(x4, x5), v2 = vsubq_f32(x4, x5);
                                const float32x4_t u3 = vaddq_f32(x6, x7), v3 = vsubq_f32(x6, x7);
                                const float32x4_t b0 = vaddq_f32(u0, u1), b1 = vaddq_f32(v0, v1), b2 = vsubq_f32(u0, u1), b3 = vsubq_f32(v0, v1);
                                const float32x4_t b4 = vaddq_f32(u2, u3), b5 = vaddq_f32(v2, v3), b6 = vsubq_f32(u2, u3), b7 = vsubq_f32(v2, v3);
                                x0 = vmulq_f32(vaddq_f32(b0, b4), s); x1 = vmulq_f32(vaddq_f32(b1, b5), s);
                                x2 = vmulq_f32(vaddq_f32(b2, b6), s); x3 = vmulq_f32(vaddq_f32(b3, b7), s);
                                x4 = vmulq_f32(vsubq_f32(b0, b4), s); x5 = vmulq_f32(vsubq_f32(b1, b5), s);
                                x6 = vmulq_f32(vsubq_f32(b2, b6), s); x7 = vmulq_f32(vsubq_f32(b3, b7), s);
                            };
                            float32x4_t r[8] = {
                                vld1q_f32(g4[0]), vld1q_f32(g4[1]), vld1q_f32(g4[2]), vld1q_f32(g4[3]),
                                vld1q_f32(g4[4]), vld1q_f32(g4[5]), vld1q_f32(g4[6]), vld1q_f32(g4[7])
                            };
                            h8_4_simd(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
                            const float ahp4[4] = {analytic * hp_arr[0], analytic * hp_arr[1],
                                                   analytic * hp_arr[4], analytic * hp_arr[5]};
                            const float32x4_t ahp_v = vld1q_f32(ahp4);
                            const float32x4_t eps_v = vdupq_n_f32(1e-20f), one_v = vdupq_n_f32(1.0f), zero_v = vdupq_n_f32(0.0f);
                            for (int j = 1; j < 8; ++j) {
                                const float32x4_t pw_v = vaddq_f32(vmulq_f32(r[j], r[j]), eps_v);
                                r[j] = vmulq_f32(r[j], vmaxq_f32(zero_v, vsubq_f32(one_v, vdivq_f32(ahp_v, pw_v))));
                            }
                            h8_4_simd(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
                            float c_out[4];
                            vst1q_f32(c_out, r[central]);
                            const float blend = .25f * strength * gate;
                            for (int l = 0; l < 4; ++l) {
                                const int f = freqs[l];
                                filtered[f] = (1.0f - blend) * filtered[f] + blend * c_out[l];
                            }
#elif defined(__AVX2__)
                            auto h8_4_simd = [](__m128& x0, __m128& x1, __m128& x2, __m128& x3,
                                                __m128& x4, __m128& x5, __m128& x6, __m128& x7) {
                                const __m128 s = _mm_set1_ps(0.35355339059327376f);
                                const __m128 u0 = _mm_add_ps(x0, x1), v0 = _mm_sub_ps(x0, x1);
                                const __m128 u1 = _mm_add_ps(x2, x3), v1 = _mm_sub_ps(x2, x3);
                                const __m128 u2 = _mm_add_ps(x4, x5), v2 = _mm_sub_ps(x4, x5);
                                const __m128 u3 = _mm_add_ps(x6, x7), v3 = _mm_sub_ps(x6, x7);
                                const __m128 b0 = _mm_add_ps(u0, u1), b1 = _mm_add_ps(v0, v1), b2 = _mm_sub_ps(u0, u1), b3 = _mm_sub_ps(v0, v1);
                                const __m128 b4 = _mm_add_ps(u2, u3), b5 = _mm_add_ps(v2, v3), b6 = _mm_sub_ps(u2, u3), b7 = _mm_sub_ps(v2, v3);
                                x0 = _mm_mul_ps(_mm_add_ps(b0, b4), s); x1 = _mm_mul_ps(_mm_add_ps(b1, b5), s);
                                x2 = _mm_mul_ps(_mm_add_ps(b2, b6), s); x3 = _mm_mul_ps(_mm_add_ps(b3, b7), s);
                                x4 = _mm_mul_ps(_mm_sub_ps(b0, b4), s); x5 = _mm_mul_ps(_mm_sub_ps(b1, b5), s);
                                x6 = _mm_mul_ps(_mm_sub_ps(b2, b6), s); x7 = _mm_mul_ps(_mm_sub_ps(b3, b7), s);
                            };
                            __m128 r[8] = {
                                _mm_loadu_ps(g4[0]), _mm_loadu_ps(g4[1]), _mm_loadu_ps(g4[2]), _mm_loadu_ps(g4[3]),
                                _mm_loadu_ps(g4[4]), _mm_loadu_ps(g4[5]), _mm_loadu_ps(g4[6]), _mm_loadu_ps(g4[7])
                            };
                            h8_4_simd(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
                            const __m128 ahp_v = _mm_setr_ps(analytic * hp_arr[0], analytic * hp_arr[1],
                                                             analytic * hp_arr[4], analytic * hp_arr[5]);
                            const __m128 eps_v = _mm_set1_ps(1e-20f), one_v = _mm_set1_ps(1.0f), zero_v = _mm_setzero_ps();
                            for (int j = 1; j < 8; ++j) {
                                const __m128 pw_v = _mm_add_ps(_mm_mul_ps(r[j], r[j]), eps_v);
                                r[j] = _mm_mul_ps(r[j], _mm_max_ps(zero_v, _mm_sub_ps(one_v, _mm_div_ps(ahp_v, pw_v))));
                            }
                            h8_4_simd(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
                            float c_out[4];
                            _mm_storeu_ps(c_out, r[central]);
                            const float blend = .25f * strength * gate;
                            for (int l = 0; l < 4; ++l) {
                                const int f = freqs[l];
                                filtered[f] = (1.0f - blend) * filtered[f] + blend * c_out[l];
                            }
#else
                            auto h8_4 = [](float (&x)[8][4]) {
                                constexpr float s = 0.35355339059327376f;
                                for (int l = 0; l < 4; ++l) {
                                    const float u0 = x[0][l] + x[1][l], v0 = x[0][l] - x[1][l];
                                    const float u1 = x[2][l] + x[3][l], v1 = x[2][l] - x[3][l];
                                    const float u2 = x[4][l] + x[5][l], v2 = x[4][l] - x[5][l];
                                    const float u3 = x[6][l] + x[7][l], v3 = x[6][l] - x[7][l];
                                    const float b0 = u0 + u1, b1 = v0 + v1, b2 = u0 - u1, b3 = v0 - v1;
                                    const float b4 = u2 + u3, b5 = v2 + v3, b6 = u2 - u3, b7 = v2 - v3;
                                    x[0][l] = (b0 + b4) * s; x[1][l] = (b1 + b5) * s;
                                    x[2][l] = (b2 + b6) * s; x[3][l] = (b3 + b7) * s;
                                    x[4][l] = (b0 - b4) * s; x[5][l] = (b1 - b5) * s;
                                    x[6][l] = (b2 - b6) * s; x[7][l] = (b3 - b7) * s;
                                }
                            };
                            h8_4(g4);
                            const float ahp4[4] = {analytic * hp_arr[0], analytic * hp_arr[1],
                                                   analytic * hp_arr[4], analytic * hp_arr[5]};
                            for (int j = 1; j < 8; ++j) {
                                for (int l = 0; l < 4; ++l) {
                                    const float power = g4[j][l] * g4[j][l];
                                    g4[j][l] *= std::max(0.0f, 1.0f - ahp4[l] / (power + 1e-20f));
                                }
                            }
                            h8_4(g4);
                            const float blend = .25f * strength * gate;
                            for (int l = 0; l < 4; ++l) {
                                const int f = freqs[l];
                                filtered[f] = (1.0f - blend) * filtered[f] + blend * g4[central][l];
                            }
#endif
                        }
                        transform4(filtered);
                        std::copy_n(filtered, 16, channels[k]);
                    }
                    const float bs00 = basis[0] * scales[0], bs01 = basis[1] * scales[0], bs02 = basis[2] * scales[0];
                    const float bs10 = basis[3] * scales[1], bs11 = basis[4] * scales[1], bs12 = basis[5] * scales[1];
                    const float bs20 = basis[6] * scales[2], bs21 = basis[7] * scales[2], bs22 = basis[8] * scales[2];
                    for (int y = 0; y < 4; ++y) {
                        const int ly = by * 4 + y - 8;
                        if (ly >= th) continue;
                        for (int x = 0; x < 4; ++x) {
                            const int lx = bx * 4 + x - 8;
                            if (lx >= tw) continue;
                            const int idx = y * 4 + x;
                            const float c0_v = channels[0][idx], c1_v = channels[1][idx], c2_v = channels[2][idx];
                            const float v0 = bs00 * c0_v + bs01 * c1_v + bs02 * c2_v;
                            const float v1 = bs10 * c0_v + bs11 * c1_v + bs12 * c2_v;
                            const float v2 = bs20 * c0_v + bs21 * c1_v + bs22 * c2_v;
                            const float* old_p = input.data() + (std::size_t(by * 4 + y) * pw + bx * 4 + x) * 3;
                            float* out_p = output.data() + (std::size_t(ly) * tw + lx) * 3;
                            out_p[0] = v0; out_p[1] = v1; out_p[2] = v2;
                            const double d0 = v0 - old_p[0], d1 = v1 - old_p[1], d2 = v2 - old_p[2];
                            local_stat.rms_update += d0 * d0 + d1 * d1 + d2 * d2;
                            if (std::fabs(d0) > 1e-9) ++local_stat.changed;
                            if (std::fabs(d1) > 1e-9) ++local_stat.changed;
                            if (std::fabs(d2) > 1e-9) ++local_stat.changed;
                        }
                    }
                    if (!ensemble) ++local_stat.rejected;
                    }
                }
                for (int r = 0; r < th; ++r)
                    std::copy_n(output.data() + std::size_t(r) * tw * 3, std::size_t(tw) * 3,
                                rgb + (std::size_t(r0 + r) * w + c0) * 3);
                local_stat.peak_scratch_bytes = fill_scratch+
                    (input.size() * 2 + ci_w.size() + a.size() + b.size() + ca.size() + cb.size() + neff.size() + output.size()) * sizeof(float);
                cost_mark(2);
                stats[ti] = local_stat;
            }
        });
    StageStats out; for (auto& s : stats) add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / (double(h) * w * 3));
    out.noise_variance /= double(std::max<std::size_t>(1, out.samples));
    out.signal_power /= double(std::max<std::size_t>(1, out.samples));
    return out;
}
} // namespace detail

inline StageStats half_stack_denoise(float* rgb, const float* half_a, const float* half_b,
                                    int h, int w, const Noise& noise,
                                    const HalfStackOptions& options,
                                    const Execution& execution) {
    if ((half_a == nullptr) != (half_b == nullptr))
        throw std::invalid_argument("both independent half stacks required");
    if(options.fusion_eta && (options.fusion_eta_h<=0||options.fusion_eta_w<=0))
        throw std::invalid_argument("fusion eta dimensions required");
    return detail::denoise_tiles(rgb, h, w, noise, options, execution, half_a && half_b,
        [&](int y, int x, float* a, float* b, float& count) {
            const auto p = (std::size_t(y) * w + x) * 3;
            if(half_a && half_b) for (int k = 0; k < 3; ++k) {
                a[k] = half_a[p + k];
                b[k] = half_b[p + k];
            }
            count = std::max(1.0f, options.effective_frames);
            if(options.fusion_eta) {
                const float eta_scale = (h > options.fusion_eta_h * 6) ? 0.125f : 0.25f;
                const float eta=detail::sample(options.fusion_eta,options.fusion_eta_h,options.fusion_eta_w,
                    y*eta_scale,x*eta_scale);
                count=eta>0?std::min(count,std::max(1.0f,1/eta)):1;
            }
        });
}

namespace detail {
inline std::size_t reconstruct_half_tile(const Frames& raw,int h,int w,const uint8_t* cfa,
                                  const Frames& fy,const Frames& fx,const Frames& mw,
                                  const Noise& noise,const float* rgb,const HalfStackOptions& options,
                                  int y0,int x0,int th,int tw,float* a,float* b,float* count) {
    const size_t pixels=size_t(th)*tw;
    const float missing=std::numeric_limits<float>::quiet_NaN();
    struct HalfTileTls {
        std::vector<float> values, gates, ys, xs, horizontal, filtered;
    };
    static thread_local HalfTileTls tls;
    auto& values = tls.values;
    auto& gates = tls.gates;
    auto& ys = tls.ys;
    auto& xs = tls.xs;
    auto& horizontal = tls.horizontal;
    auto& filtered = tls.filtered;
    values.assign(raw.size()*pixels*3,missing);
    gates.assign(raw.size()*pixels,0);
    ys.resize(pixels);
    xs.resize(pixels);
    const size_t base_scratch=(values.size()+gates.size()+ys.size()+xs.size())*sizeof(float);
    size_t peak_scratch=base_scratch;
    const int hh = h / 2, wh = w / 2;
    const float hh_max = float(hh - 1), wh_max = float(wh - 1);
    for(size_t n=0;n<raw.size();++n) {
        int lo_y=h,lo_x=w,hi_y=-1,hi_x=-1;
        size_t active=0;
        const float* mw_p = mw.empty() ? nullptr : mw[n].data();
        const float* fy_p = fy.empty() ? nullptr : fy[n].data();
        const float* fx_p = fx.empty() ? nullptr : fx[n].data();
        for(int r=0;r<th;++r) {
            const int y=clamp(y0+r,2*h);
            const float qy = std::max(0.0f, std::min(hh_max, .25f * y));
            const int ry0 = int(qy), ry1 = std::min(ry0 + 1, hh - 1);
            const float ty = qy - ry0;
            for(int c=0;c<tw;++c) {
                const size_t i=size_t(r)*tw+c;
                const int x=clamp(x0+c,2*w);
                const float qx = std::max(0.0f, std::min(wh_max, .25f * x));
                const int rx0 = int(qx), rx1 = std::min(rx0 + 1, wh - 1);
                const float tx = qx - rx0;
                const size_t i00 = size_t(ry0) * wh + rx0, i01 = size_t(ry0) * wh + rx1;
                const size_t i10 = size_t(ry1) * wh + rx0, i11 = size_t(ry1) * wh + rx1;
                const float weight = mw_p ? clamp01((1 - ty) * ((1 - tx) * mw_p[i00] + tx * mw_p[i01]) +
                                                    ty * ((1 - tx) * mw_p[i10] + tx * mw_p[i11])) : 1.0f;
                if(weight<=.01f) continue;
                const float dy = fy_p ? ((1 - ty) * ((1 - tx) * fy_p[i00] + tx * fy_p[i01]) +
                                         ty * ((1 - tx) * fy_p[i10] + tx * fy_p[i11])) : 0.0f;
                const float dx = fx_p ? ((1 - ty) * ((1 - tx) * fx_p[i00] + tx * fx_p[i01]) +
                                         ty * ((1 - tx) * fx_p[i10] + tx * fx_p[i11])) : 0.0f;
                const float yy=.5f*y+2*dy;
                const float xx=.5f*x+2*dx;
                if(!inside(yy,xx,h,w)) continue;
                gates[n*pixels+i]=weight;ys[i]=yy;xs[i]=xx;++active;
                const int iy=int(std::floor(yy)),ix=int(std::floor(xx));
                lo_y=std::min(lo_y,iy);lo_x=std::min(lo_x,ix);
                hi_y=std::max(hi_y,std::min(h-1,iy+1));hi_x=std::max(hi_x,std::min(w-1,ix+1));
            }
        }
        if(!active) continue;
        const int fh=hi_y-lo_y+1,fw=hi_x-lo_x+1;
        filtered.clear();
        // A radius-three tent is bilinear interpolation of a separable
        // integer [1,2,3,2,1] convolution. Dense local warps reuse those sums.
        if(size_t(fh)*fw*2<=active) {
            const int top=std::max(0,lo_y-2),bottom=std::min(h-1,hi_y+2);
            horizontal.assign(size_t(bottom-top+1)*fw*6,0);
            filtered.assign(size_t(fh)*fw*6,0);
            peak_scratch=std::max(peak_scratch,base_scratch+(filtered.size()+horizontal.size())*sizeof(float));
            for(int y=top;y<=bottom;++y) {
                const uint8_t* rcfa = cfa + (y % 6) * 6;
                const float* rptr = raw[n].data() + size_t(y) * w;
                for(int x=lo_x;x<=hi_x;++x) {
                    float* dst=horizontal.data()+(size_t(y-top)*fw+x-lo_x)*6;
                    for(int dx=-2;dx<=2;++dx) {
                        const int xx=x+dx;
                        if(xx<0||xx>=w) continue;
                        const float v=rptr[xx];
                        if(!std::isfinite(v)||!noise.unsaturated(n,v)) continue;
                        const int ch=rcfa[xx%6];
                        const float k=float(3-std::abs(dx));
                        dst[ch]+=k*v;dst[ch+3]+=k;
                    }
                }
            }
            for(int y=lo_y;y<=hi_y;++y) for(int x=0;x<fw;++x) {
                float* dst=filtered.data()+(size_t(y-lo_y)*fw+x)*6;
                for(int dy=-2;dy<=2;++dy) {
                    const int yy=y+dy;
                    if(yy<top||yy>bottom) continue;
                    const float* src=horizontal.data()+(size_t(yy-top)*fw+x)*6;
                    const float k=float(3-std::abs(dy));
                    for(int ch=0;ch<6;++ch) dst[ch]+=k*src[ch];
                }
            }
        }
        for(size_t i=0;i<pixels;++i) {
            if(gates[n*pixels+i]<=0) continue;
            const float yy=ys[i],xx=xs[i];
            float v[3]={},den[3]={};
            if(!filtered.empty()) {
                const float sy = std::max(0.0f, std::min(float(fh - 1), yy - lo_y));
                const float sx = std::max(0.0f, std::min(float(fw - 1), xx - lo_x));
                const int sr = int(sy), sc = int(sx);
                const int sr1 = std::min(sr + 1, fh - 1), sc1 = std::min(sc + 1, fw - 1);
                const float sty = sy - sr, stx = sx - sc;
                const float* p00 = filtered.data() + (size_t(sr) * fw + sc) * 6;
                const float* p01 = filtered.data() + (size_t(sr) * fw + sc1) * 6;
                const float* p10 = filtered.data() + (size_t(sr1) * fw + sc) * 6;
                const float* p11 = filtered.data() + (size_t(sr1) * fw + sc1) * 6;
                for(int ch=0;ch<3;++ch) {
                    v[ch]=(1-sty)*((1-stx)*p00[ch]+stx*p01[ch])+sty*((1-stx)*p10[ch]+stx*p11[ch]);
                    den[ch]=(1-sty)*((1-stx)*p00[ch+3]+stx*p01[ch+3])+sty*((1-stx)*p10[ch+3]+stx*p11[ch+3]);
                }
            } else {
                const int iy=int(std::floor(yy)),ix=int(std::floor(xx));
                for(int r=std::max(0,iy-2);r<=std::min(h-1,iy+3);++r) {
                    const uint8_t* rcfa = cfa + (r % 6) * 6;
                    const float ky = std::max(0.0f, 3 - std::fabs(r - yy));
                    for(int c=std::max(0,ix-2);c<=std::min(w-1,ix+3);++c) {
                        const int ch=rcfa[c%6];
                        const float k=ky*std::max(0.0f,3-std::fabs(c-xx));
                        const float value=raw[n][size_t(r)*w+c];
                        if(std::isfinite(value)&&noise.unsaturated(n,value)) {v[ch]+=k*value;den[ch]+=k;}
                    }
                }
            }
            for(int ch=0;ch<3;++ch) if(den[ch]>0) values[(n*pixels+i)*3+ch]=v[ch]/den[ch];
        }
    }
    for(int r=0;r<th;++r) for(int c=0;c<tw;++c) {
        const size_t i=size_t(r)*tw+c;
        float sums[2][3]={},weights[2][3]={},total=0,squared=0;
        int contributed[2]={};
        for(size_t n=0;n<raw.size();++n) {
            const float weight=gates[n*pixels+i];
            if(weight<=0) continue;
            const int half=int(n%2);
            bool valid=true;float precision=0;
            for(int ch=0;ch<3;++ch) {
                const float v=values[(n*pixels+i)*3+ch];
                if(std::isfinite(v)) {
                    const float optimal=weight/noise.frame_variance(n,v);
                    sums[half][ch]+=optimal*v;weights[half][ch]+=optimal;
                    if(ch==1) precision=optimal;
                } else valid=false;
            }
            if(valid) {total+=precision;squared+=precision*precision;++contributed[half];}
        }
        const size_t global=size_t(clamp(y0+r,2*h))*(2*w)+clamp(x0+c,2*w);
        for(int ch=0;ch<3;++ch) {
            a[i*3+ch]=weights[0][ch]>0?sums[0][ch]/weights[0][ch]:rgb[global*3+ch];
            b[i*3+ch]=weights[1][ch]>0?sums[1][ch]/weights[1][ch]:rgb[global*3+ch];
        }
        count[i]=squared>0?total*total/squared:1;
        // Re-evaluate the original accumulation near hard N_eff gates, where
        // equivalent separable arithmetic could change a discrete decision.
        if(std::fabs(count[i]-2)<2e-5f || (options.phase_balanced&&std::fabs(count[i]-5)<5e-5f)) {
            total=0;squared=0;
            const int y=clamp(y0+r,2*h),x=clamp(x0+c,2*w);
            for(size_t n=0;n<raw.size();++n) {
                if(gates[n*pixels+i]<=0) continue;
                const float* v=values.data()+(n*pixels+i)*3;
                if(!std::isfinite(v[0])||!std::isfinite(v[1])||!std::isfinite(v[2])) continue;
                const float yy=.5f*y+2*field(fy,n,h/2,w/2,.25f*y,.25f*x);
                const float xx=.5f*x+2*field(fx,n,h/2,w/2,.25f*y,.25f*x);
                const float value=raw_color(raw[n],h,w,cfa,yy,xx,1,&noise,n);
                const float precision=gates[n*pixels+i]/noise.frame_variance(n,value);
                total+=precision;squared+=precision*precision;
            }
            count[i]=squared>0?total*total/squared:1;
        }
        if(contributed[0]<2||contributed[1]<2) count[i]=std::min(count[i],4.0f);
        if(options.effective_frames>0) count[i]=std::min(count[i],options.effective_frames);
    }
    return peak_scratch;
}
} // namespace detail

inline StageStats half_stack_denoise(const Frames& raw, int h, int w,
                                    const uint8_t* cfa, const Frames& fy,
                                    const Frames& fx, const Frames& mw, const Noise& noise,
                                    float* rgb, const HalfStackOptions& options,
                                    const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!cfa) throw std::invalid_argument("CFA required");
    return detail::denoise_tiles(rgb, 2 * h, 2 * w, noise, options, execution, raw.size() >= 2,
        [&](int y0,int x0,int th,int tw,float* a,float* b,float* count) {
            return detail::reconstruct_half_tile(raw,h,w,cfa,fy,fx,mw,noise,rgb,options,y0,x0,th,tw,a,b,count);
        });
}

inline std::array<float, 16> kernel_power_from_covariance(float xx, float xy,
                                                        float yy, float floor) {
    if (!std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yy) ||
        !std::isfinite(floor) || xx < 0 || yy < 0 ||
        double(xx) * yy < double(xy) * xy || floor < 0 || floor > 1)
        throw std::invalid_argument("finite positive-semidefinite covariance required");
    // WHT-4 natural-order rows: DC, Nyquist, quarter-cycle, quarter-cycle.
    // Real Walsh modes mix +/- Fourier frequencies. Average both signs,
    // including Nyquist, so an oblique covariance respects real symmetry.
    const double frequency[4] = {0, .5, .25, .25};
    constexpr double four_pi_squared = 39.478417604357434;
    std::array<float, 16> result{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        double power = 0;
        for (int sy : {-1, 1}) for (int sx : {-1, 1}) {
            const double fx = sx * frequency[c], fy = sy * frequency[r];
            const double quadratic = std::max(0.0, xx * fx * fx + 2 * xy * fx * fy + yy * fy * fy);
            power += .25 * std::exp(-four_pi_squared * quadratic);
        }
        result[r * 4 + c] = std::max(floor, float(power));
    }
    result[0] = 1;
    return result;
}

inline std::array<float, 16> kernel_power_from_inverse_covariance(float xx, float xy,
                                                                float yy, float scale,
                                                                float floor) {
    const double determinant = double(xx) * yy - double(xy) * xy;
    if (!std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yy) ||
        !std::isfinite(scale) || xx <= 0 || yy <= 0 || determinant <= 0 || scale <= 0)
        throw std::invalid_argument("finite positive-definite precision required");
    const double factor = double(scale) * scale / determinant;
    return kernel_power_from_covariance(float(yy * factor), float(-xy * factor),
                                        float(xx * factor), floor);
}

namespace detail {
inline bool valid_psf(const SpatialPsfZone& z) {
    return z.valid && std::isfinite(z.cx) && std::isfinite(z.cy) &&
           std::isfinite(z.sigma_major_2x) && std::isfinite(z.sigma_minor_2x) &&
           std::isfinite(z.angle) && z.cx >= 0 && z.cx <= 1 && z.cy >= 0 && z.cy <= 1 &&
           z.sigma_major_2x > 0 && z.sigma_minor_2x > 0;
}
inline std::array<double, 3> psf_covariance(const std::vector<SpatialPsfZone>& zones,
                                          double y, double x) {
    std::array<double, 3> result{};
    double sum = 0;
    for (const auto& z : zones) {
        const double cs = std::cos(z.angle), sn = std::sin(z.angle);
        const double major = double(z.sigma_major_2x) * z.sigma_major_2x;
        const double minor = double(z.sigma_minor_2x) * z.sigma_minor_2x;
        const std::array<double, 3> cov = {cs * cs * major + sn * sn * minor,
                                         cs * sn * (major - minor),
                                         sn * sn * major + cs * cs * minor};
        const double distance = (x - z.cx) * (x - z.cx) + (y - z.cy) * (y - z.cy);
        if (distance < 1e-16) return cov;
        const double weight = 1 / distance;
        for (int k = 0; k < 3; ++k) result[k] += weight * cov[k];
        sum += weight;
    }
    for (double& value : result) value /= sum;
    return result;
}
// Gaussian kernels on a globally anchored 16-pixel grid; interpolating
// normalized kernels gives continuous weights independent of tile partition.
struct PsfGrid {
    int y0, x0, rows, cols, radius, taps;
    std::vector<float> kernels;
    PsfGrid(int top, int left, int bottom, int right, int h, int w, int rad,
            const std::vector<SpatialPsfZone>& zones)
        : y0((top / 16) * 16), x0((left / 16) * 16),
          rows((bottom - 1 - y0) / 16 + 2), cols((right - 1 - x0) / 16 + 2),
          radius(rad), taps((2 * rad + 1) * (2 * rad + 1)),
          kernels(std::size_t(rows) * cols * taps) {
        for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
            const double y = std::min(1.0, double(y0 + r * 16) / std::max(1, h - 1));
            const double x = std::min(1.0, double(x0 + c * 16) / std::max(1, w - 1));
            const auto cov = psf_covariance(zones, y, x);
            // A tiny eigenvalue floor represents subpixel delta-like axes.
            const double xx = cov[0] + 1e-8, xy = cov[1], yy = cov[2] + 1e-8;
            const double determinant = std::max(1e-16, xx * yy - xy * xy);
            const std::size_t base = (std::size_t(r) * cols + c) * taps;
            double sum = 0; int t = 0;
            for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
                const double exponent = -.5 * (yy * dx * dx - 2 * xy * dx * dy + xx * dy * dy) / determinant;
                const float value = float(std::exp(std::min(0.0, exponent)));
                kernels[base + t++] = value; sum += value;
            }
            for (int k = 0; k < taps; ++k) kernels[base + k] /= float(sum);
        }
    }
    struct View {
        const float* p[4];
        float weight[4];
        float operator()(int k) const {
            return weight[0] * p[0][k] + weight[1] * p[1][k] +
                   weight[2] * p[2][k] + weight[3] * p[3][k];
        }
    };
    View at(int y, int x) const {
        const int r = (y - y0) / 16, c = (x - x0) / 16;
        const float ty = float(y - y0 - r * 16) / 16, tx = float(x - x0 - c * 16) / 16;
        const std::size_t base = (std::size_t(r) * cols + c) * taps;
        return {{kernels.data() + base, kernels.data() + base + taps,
                 kernels.data() + base + std::size_t(cols) * taps,
                 kernels.data() + base + std::size_t(cols + 1) * taps},
                {(1 - ty) * (1 - tx), (1 - ty) * tx, ty * (1 - tx), ty * tx}};
    }
};
} // namespace detail

inline StageStats spatial_psf_deconvolve(float* rgb, int h, int w,
                                        const std::vector<SpatialPsfZone>& input_zones,
                                        const Noise& noise, int iterations, float strength,
                                        const Execution& execution) {
    detail::shape(h, w);
    if (!rgb) throw std::invalid_argument("RGB required");
    if (!std::isfinite(strength) || !std::isfinite(noise.read_variance) ||
        !std::isfinite(noise.shot) || noise.read_variance < 0 || noise.shot < 0)
        throw std::invalid_argument("finite nonnegative noise and strength required");
    StageStats result;
    std::vector<SpatialPsfZone> zones;
    float max_sigma = 0;
    for (const auto& zone : input_zones) {
        if (detail::valid_psf(zone)) {
            zones.push_back(zone);
            max_sigma = std::max(max_sigma, std::max(zone.sigma_major_2x, zone.sigma_minor_2x));
        } else ++result.rejected;
    }
    iterations = std::max(0, std::min(2, iterations));
    strength = detail::clamp01(strength);
    if (zones.empty() || !iterations || strength == 0) return result;
    const int radius = std::max(1, int(std::ceil(std::min(12.0f, 3 * max_sigma))));
    const int halo = (2 * iterations + 1) * radius;
    const int tile = std::max(16, std::min(256, execution.tile));
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    const int strip_rows = std::min(h, tile + 2 * halo);
    std::vector<float> strip(std::size_t(strip_rows) * w * 3);
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    int previous_top = 0, previous_bottom = 0;
    for (int tr = 0; tr < nr; ++tr) {
        const int core_top = tr * tile, core_bottom = std::min(h, core_top + tile);
        const int strip_top = std::max(0, core_top - halo);
        const int strip_bottom = std::min(h, core_bottom + halo);
        // Preserve original rows needed by the next stripe before output writes.
        const int overlap = std::max(0, previous_bottom - strip_top);
        if (overlap)
            std::memmove(strip.data(), strip.data() + std::size_t(strip_top - previous_top) * w * 3,
                         std::size_t(overlap) * w * 3 * sizeof(float));
        for (int y = strip_top + overlap; y < strip_bottom; ++y)
            std::copy_n(rgb + std::size_t(y) * w * 3, std::size_t(w) * 3,
                        strip.data() + std::size_t(y - strip_top) * w * 3);
        previous_top = strip_top; previous_bottom = strip_bottom;
        detail::run(nc, execution, [&](int first, int last) {
            for (int tc = first; tc < last; ++tc) {
                auto& stat = stats[std::size_t(tr) * nc + tc];
                const int core_left = tc * tile, core_right = std::min(w, core_left + tile);
                const int left = std::max(0, core_left - halo), right = std::min(w, core_right + halo);
                const int top = strip_top, bottom = strip_bottom, lh = bottom - top, lw = right - left;
                const std::size_t pixels = std::size_t(lh) * lw;
                std::vector<float> original(pixels * 3), current(pixels * 3),
                                   gradient(pixels * 3), mass(pixels), norm(pixels);
                for (int y = top; y < bottom; ++y)
                    std::copy_n(strip.data() + (std::size_t(y - top) * w + left) * 3, std::size_t(lw) * 3,
                                original.data() + std::size_t(y - top) * lw * 3);
                current = original;
                const detail::PsfGrid grid(top, left, bottom, right, h, w, radius, zones);
                auto local = [&](int y, int x) { return std::size_t(y - top) * lw + x - left; };
                for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                    const auto kernel = grid.at(y, x);
                    float sum = 0; int t = 0;
                    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx, ++t)
                        if (y + dy >= 0 && y + dy < h && x + dx >= 0 && x + dx < w) sum += kernel(t);
                    norm[local(y, x)] = 1 / std::max(1e-20f, sum);
                }
                auto project = [&](int y, int x, const detail::PsfGrid::View& kernel, float* predicted) {
                    predicted[0] = predicted[1] = predicted[2] = 0;
                    int t = 0;
                    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx, ++t) {
                        const int yy = y + dy, xx = x + dx;
                        if (yy < 0 || yy >= h || xx < 0 || xx >= w) continue;
                        const float weight = kernel(t) * norm[local(y, x)];
                        const auto p = local(yy, xx) * 3;
                        for (int k = 0; k < 3; ++k) predicted[k] += weight * current[p + k];
                    }
                };
                for (int y = core_top; y < core_bottom; ++y) for (int x = core_left; x < core_right; ++x) {
                    float prediction[3]; project(y, x, grid.at(y, x), prediction);
                    for (int k = 0; k < 3; ++k) {
                        const double residual = original[local(y, x) * 3 + k] - prediction[k];
                        stat.before += residual * residual;
                    }
                }
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    std::fill(gradient.begin(), gradient.end(), 0);
                    std::fill(mass.begin(), mass.end(), 0);
                    const int shrink = (2 * iteration + 1) * radius;
                    const int ft = top == 0 ? 0 : top + shrink, fb = bottom == h ? h : bottom - shrink;
                    const int fl = left == 0 ? 0 : left + shrink, fr = right == w ? w : right - shrink;
                    const int ut = top == 0 ? 0 : ft + radius, ub = bottom == h ? h : fb - radius;
                    const int ul = left == 0 ? 0 : fl + radius, ur = right == w ? w : fr - radius;
                    for (int y = ft; y < fb; ++y) for (int x = fl; x < fr; ++x) {
                        const auto kernel = grid.at(y, x);
                        float prediction[3]; project(y, x, kernel, prediction);
                        const auto source = local(y, x);
                        float residual[3];
                        for (int k = 0; k < 3; ++k) residual[k] = original[source * 3 + k] - prediction[k];
                        int t = 0;
                        for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx, ++t) {
                            const int yy = y + dy, xx = x + dx;
                            if (yy < ut || yy >= ub || xx < ul || xx >= ur) continue;
                            const auto target = local(yy, xx);
                            const float weight = kernel(t) * norm[source];
                            for (int k = 0; k < 3; ++k) gradient[target * 3 + k] += weight * residual[k];
                            mass[target] += weight;
                        }
                    }
                    for (int y = ut; y < ub; ++y) for (int x = ul; x < ur; ++x) {
                        const auto p = local(y, x);
                        const int ym = std::max(top, y - 1), yp = std::min(bottom - 1, y + 1);
                        const int xm = std::max(left, x - 1), xp = std::min(right - 1, x + 1);
                        float energy = 0, variance = 0;
                        for (int k = 0; k < 3; ++k) {
                            const float gx = .5f * (original[local(y, xp) * 3 + k] - original[local(y, xm) * 3 + k]);
                            const float gy = .5f * (original[local(yp, x) * 3 + k] - original[local(ym, x) * 3 + k]);
                            energy += (gx * gx + gy * gy) / 3;
                            variance += noise.variance(original[p * 3 + k]) / 3;
                        }
                        const float gate = std::max(0.0f, energy - 2 * variance) / (energy + 8 * variance);
                        const float limit = .5f * std::sqrt(energy) + 3 * std::sqrt(variance);
                        for (int k = 0; k < 3; ++k) {
                            const float update = strength * gate * gradient[p * 3 + k] / std::max(1e-20f, mass[p]);
                            current[p * 3 + k] += std::max(-limit, std::min(limit, update));
                        }
                    }
                }
                for (int y = core_top; y < core_bottom; ++y) for (int x = core_left; x < core_right; ++x) {
                    float prediction[3]; project(y, x, grid.at(y, x), prediction);
                    const auto p = local(y, x);
                    for (int k = 0; k < 3; ++k) {
                        const double residual = original[p * 3 + k] - prediction[k];
                        stat.after += residual * residual;
                        const double update = current[p * 3 + k] - original[p * 3 + k];
                        stat.rms_update += update * update;
                        if (update != 0) ++stat.changed;
                        ++stat.samples;
                        rgb[(std::size_t(y) * w + x) * 3 + k] = current[p * 3 + k];
                    }
                }
                stat.peak_scratch_bytes = (pixels * 11 + grid.kernels.size()) * sizeof(float);
            }
        });
    }
    for (const auto& s : stats) detail::add_stats(result, s);
    result.rms_update = std::sqrt(result.rms_update / std::max<std::size_t>(1, result.samples));
    // Shared frozen strip plus the largest per-worker tile allocation.
    result.peak_scratch_bytes += strip.size() * sizeof(float);
    result.iterations = iterations;
    return result;
}

} // namespace heraw_core
