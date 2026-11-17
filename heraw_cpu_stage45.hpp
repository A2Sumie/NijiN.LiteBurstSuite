// Include after the CPU image helpers and snr_gated_rl_deconv definition.
#pragma once

#include "heraw_cpu_cache.hpp"

namespace heraw_cpu {

inline float weight_exp(float x) {
    const float t = -clamp_f(x, 0.0f, 40.0f);
    const int k = int(t * 1.4426950408889634f - 0.5f);
    const float kf = float(k);
    const float r = (t - kf * 0.693145751953125f) - kf * 1.4286067653301870e-6f;
    float p = 0.00138888889f;
    p = p * r + 0.00833333333f;
    p = p * r + 0.0416666667f;
    p = p * r + 0.166666667f;
    p = p * r + 0.5f;
    p = p * r + 1.0f;
    p = p * r + 1.0f;
    const uint32_t bits = uint32_t(127 + k) << 23;
    float scale;
    std::memcpy(&scale, &bits, sizeof(scale));
    return p * scale;
}

#if defined(__aarch64__)
inline float32x4_t weight_exp4_neon(float32x4_t x) {
    const float32x4_t t = vnegq_f32(vminq_f32(vdupq_n_f32(40.0f), vmaxq_f32(vdupq_n_f32(0.0f), x)));
    const int32x4_t k_i = vcvtq_s32_f32(vfmaq_n_f32(vdupq_n_f32(-0.5f), t, 1.4426950408889634f));
    const float32x4_t kf = vcvtq_f32_s32(k_i);
    const float32x4_t r = vfmsq_n_f32(vfmsq_n_f32(t, kf, 0.693145751953125f), kf, 1.4286067653301870e-6f);
    float32x4_t p = vdupq_n_f32(0.00138888889f);
    p = vfmaq_f32(vdupq_n_f32(0.00833333333f), p, r);
    p = vfmaq_f32(vdupq_n_f32(0.0416666667f), p, r);
    p = vfmaq_f32(vdupq_n_f32(0.166666667f), p, r);
    p = vfmaq_f32(vdupq_n_f32(0.5f), p, r);
    p = vfmaq_f32(vdupq_n_f32(1.0f), p, r);
    p = vfmaq_f32(vdupq_n_f32(1.0f), p, r);
    const float32x4_t scale = vreinterpretq_f32_s32(vshlq_n_s32(vaddq_s32(k_i, vdupq_n_s32(127)), 23));
    return vmulq_f32(p, scale);
}
#elif defined(__AVX2__)
inline __m128 weight_exp4_avx2(__m128 x) {
    const __m128 zero = _mm_setzero_ps();
    const __m128 forty = _mm_set1_ps(40.0f);
    const __m128 t = _mm_sub_ps(zero, _mm_min_ps(forty, _mm_max_ps(zero, x)));
    const __m128i k_i = _mm_cvttps_epi32(_mm_fmadd_ps(t, _mm_set1_ps(1.4426950408889634f), _mm_set1_ps(-0.5f)));
    const __m128 kf = _mm_cvtepi32_ps(k_i);
    const __m128 r = _mm_fnmadd_ps(kf, _mm_set1_ps(1.4286067653301870e-6f),
                     _mm_fnmadd_ps(kf, _mm_set1_ps(0.693145751953125f), t));
    __m128 p = _mm_set1_ps(0.00138888889f);
    p = _mm_fmadd_ps(p, r, _mm_set1_ps(0.00833333333f));
    p = _mm_fmadd_ps(p, r, _mm_set1_ps(0.0416666667f));
    p = _mm_fmadd_ps(p, r, _mm_set1_ps(0.166666667f));
    p = _mm_fmadd_ps(p, r, _mm_set1_ps(0.5f));
    p = _mm_fmadd_ps(p, r, _mm_set1_ps(1.0f));
    p = _mm_fmadd_ps(p, r, _mm_set1_ps(1.0f));
    const __m128 scale = _mm_castsi128_ps(_mm_slli_epi32(_mm_add_epi32(k_i, _mm_set1_epi32(127)), 23));
    return _mm_mul_ps(p, scale);
}

inline __m256 weight_exp8_avx2(__m256 x) {
    const __m256 zero = _mm256_setzero_ps();
    const __m256 forty = _mm256_set1_ps(40.0f);
    const __m256 t = _mm256_sub_ps(zero, _mm256_min_ps(forty, _mm256_max_ps(zero, x)));
    const __m256i k_i = _mm256_cvttps_epi32(_mm256_fmadd_ps(t, _mm256_set1_ps(1.4426950408889634f), _mm256_set1_ps(-0.5f)));
    const __m256 kf = _mm256_cvtepi32_ps(k_i);
    const __m256 r = _mm256_fnmadd_ps(kf, _mm256_set1_ps(1.4286067653301870e-6f),
                     _mm256_fnmadd_ps(kf, _mm256_set1_ps(0.693145751953125f), t));
    __m256 p = _mm256_set1_ps(0.00138888889f);
    p = _mm256_fmadd_ps(p, r, _mm256_set1_ps(0.00833333333f));
    p = _mm256_fmadd_ps(p, r, _mm256_set1_ps(0.0416666667f));
    p = _mm256_fmadd_ps(p, r, _mm256_set1_ps(0.166666667f));
    p = _mm256_fmadd_ps(p, r, _mm256_set1_ps(0.5f));
    p = _mm256_fmadd_ps(p, r, _mm256_set1_ps(1.0f));
    p = _mm256_fmadd_ps(p, r, _mm256_set1_ps(1.0f));
    const __m256 scale = _mm256_castsi256_ps(_mm256_slli_epi32(_mm256_add_epi32(k_i, _mm256_set1_epi32(127)), 23));
    return _mm256_mul_ps(p, scale);
}

#if HERAW_AVX512_ENABLED
__attribute__((always_inline, target("avx512f,avx512vl,avx512bw,avx512dq,fma")))
inline __m512 weight_exp16_avx512(__m512 x) {
    const __m512 zero = _mm512_setzero_ps();
    const __m512 forty = _mm512_set1_ps(40.0f);
    const __m512 t = _mm512_sub_ps(zero, _mm512_min_ps(forty, _mm512_max_ps(zero, x)));
    const __m512i k_i = _mm512_cvttps_epi32(_mm512_fmadd_ps(t, _mm512_set1_ps(1.4426950408889634f), _mm512_set1_ps(-0.5f)));
    const __m512 kf = _mm512_cvtepi32_ps(k_i);
    const __m512 r = _mm512_fnmadd_ps(kf, _mm512_set1_ps(1.4286067653301870e-6f),
                     _mm512_fnmadd_ps(kf, _mm512_set1_ps(0.693145751953125f), t));
    __m512 p = _mm512_set1_ps(0.00138888889f);
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(0.00833333333f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(0.0416666667f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(0.166666667f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(0.5f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f));
    p = _mm512_fmadd_ps(p, r, _mm512_set1_ps(1.0f));
    return _mm512_scalef_ps(p, kf);
}
#endif
#endif

inline float chroma_anchor(const heraw_gpu::Stage45GpuJob& j, int y, int x, int ch) {
    float sum = 0.0f, weights = 0.0f;
    const int r0 = std::max(0, y - 3), r1 = std::min(j.H1 - 1, y + 3);
    const int c0 = std::max(0, x - 3), c1 = std::min(j.W1 - 1, x + 3);
    int rmod = r0 % 6;
    const int cmod0 = c0 % 6;
    for (int r = r0; r <= r1; ++r) {
        const uint8_t* rcfa = j.cfa6x6[rmod];
        if (++rmod == 6) rmod = 0;
        const float* arow = j.c_anc_1x + size_t(r) * j.W1;
        const int dy2 = (r - y) * (r - y);
        int cmod = cmod0;
        for (int c = c0; c <= c1; ++c) {
            const uint8_t cc = rcfa[cmod];
            if (++cmod == 6) cmod = 0;
            if (cc != ch) continue;
            const float w = 1.0f / (0.25f + float(dy2 + (c - x) * (c - x)));
            sum += w * arow[c];
            weights += w;
        }
    }
    return sum / std::max(weights, 1e-12f);
}

struct AnchorTapList {
    int count = 0;
    int8_t dy[49]{};
    int8_t dx[49]{};
    float nw[49]{}; // Normalized weights (sum to 1.0 when count > 0)
};

inline float unpack_half(uint16_t h) {
    const uint32_t exp = (h >> 10) & 31u;
    const uint32_t mant = h & 1023u;
    if (exp > 0u && exp < 31u) {
        const uint32_t bits = (uint32_t(h & 0x8000u) << 16) | ((exp + 112u) << 23) | (mant << 13);
        float out;
        std::memcpy(&out, &bits, sizeof(out));
        return out;
    }
    const float v = exp == 0u ? std::ldexp(float(mant), -24) :
        (mant ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity());
    return (h & 0x8000u) ? -v : v;
}

struct CpuStage45Scratch {
    UninitBuf<float> green, red, blue, guide, z, den, variance, dct_pilot;
    UninitBuf<float> mr, mb, bg, tr, tb, anc_r, anc_b;
    UninitBuf<float> rl_gain, rl_thr, patch, gain_p, thr_p;
    RlWorkspace rl_ws;
};

namespace detail_s45 {

#if HERAW_AVX512_ENABLED
__attribute__((target("avx512f,avx512vl,avx512bw,avx512dq,fma")))
inline int atrous_h_span_avx512(
    const float* __restrict g_row, const float* __restrict sr_row, const float* __restrict sb_row,
    const float* __restrict b_row, float* __restrict dr_out, float* __restrict db_out,
    int x_start, int x0, int x1, int step, int step2, float inv_g, float inv_c
) {
    constexpr float k0 = 6.0f / 16.0f, k1 = 4.0f / 16.0f, k2 = 1.0f / 16.0f;
    const __m512 k0_v = _mm512_set1_ps(k0), k1_v = _mm512_set1_ps(k1), k2_v = _mm512_set1_ps(k2);
    const __m512 ig_v = _mm512_set1_ps(inv_g), ic_base = _mm512_set1_ps(inv_c), eps_v = _mm512_set1_ps(1e-12f);
    const __m512i dup_idx = _mm512_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7);
    int x = x_start;
    for (; x + 16 <= x1; x += 16) {
        const __m512 bh8 = _mm512_castps256_ps512(_mm256_loadu_ps(b_row + (x >> 1)));
        const __m512 b16 = _mm512_permutexvar_ps(dup_idx, bh8);
        const __m512 ic = _mm512_div_ps(ic_base, _mm512_mul_ps(b16, b16));
        const __m512 g0 = _mm512_loadu_ps(g_row + x), r0 = _mm512_loadu_ps(sr_row + x), b0 = _mm512_loadu_ps(sb_row + x);
        __m512 wr = k0_v, vr = _mm512_mul_ps(k0_v, r0), wb = k0_v, vb = _mm512_mul_ps(k0_v, b0);
        const int offs[4] = {-step2, -step, step, step2};
        const __m512 kws[4] = {k2_v, k1_v, k1_v, k2_v};
        for (int t = 0; t < 4; ++t) {
            const int off = offs[t];
            const __m512 gq = _mm512_loadu_ps(g_row + x + off);
            const __m512 rq = _mm512_loadu_ps(sr_row + x + off);
            const __m512 bq = _mm512_loadu_ps(sb_row + x + off);
            const __m512 dg = _mm512_sub_ps(gq, g0);
            const __m512 wg = _mm512_mul_ps(kws[t], weight_exp16_avx512(_mm512_mul_ps(_mm512_mul_ps(dg, dg), ig_v)));
            const __m512 rd = _mm512_sub_ps(rq, r0), bd = _mm512_sub_ps(bq, b0);
            const __m512 rw = _mm512_mul_ps(wg, weight_exp16_avx512(_mm512_mul_ps(_mm512_mul_ps(rd, rd), ic)));
            const __m512 bw = _mm512_mul_ps(wg, weight_exp16_avx512(_mm512_mul_ps(_mm512_mul_ps(bd, bd), ic)));
            wr = _mm512_add_ps(wr, rw); vr = _mm512_fmadd_ps(rw, rq, vr);
            wb = _mm512_add_ps(wb, bw); vb = _mm512_fmadd_ps(bw, bq, vb);
        }
        _mm512_storeu_ps(dr_out + (x - x0), _mm512_div_ps(vr, _mm512_max_ps(wr, eps_v)));
        _mm512_storeu_ps(db_out + (x - x0), _mm512_div_ps(vb, _mm512_max_ps(wb, eps_v)));
    }
    return x;
}

struct AtrousVSpanArgs {
    const float* gp[4];
    const float* rp[4];
    const float* bp[4];
    const float* g0p;
    const float* r0p;
    const float* b0p;
    const float* b_row_full;
    float* dr_out;
    float* db_out;
    int x0, i_start, len;
    float inv_g, inv_c;
};

__attribute__((target("avx512f,avx512vl,avx512bw,avx512dq,fma")))
inline int atrous_v_span_avx512(const AtrousVSpanArgs& a) {
    constexpr float k0 = 6.0f / 16.0f, k1 = 4.0f / 16.0f, k2 = 1.0f / 16.0f;
    const __m512 k0_v = _mm512_set1_ps(k0), k1_v = _mm512_set1_ps(k1), k2_v = _mm512_set1_ps(k2);
    const __m512 ig_v = _mm512_set1_ps(a.inv_g), ic_base = _mm512_set1_ps(a.inv_c), eps_v = _mm512_set1_ps(1e-12f);
    const __m512i dup_idx = _mm512_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7);
    const __m512 kws[4] = {k2_v, k1_v, k1_v, k2_v};
    int i = a.i_start;
    for (; i + 16 <= a.len; i += 16) {
        const __m512 bh8 = _mm512_castps256_ps512(_mm256_loadu_ps(a.b_row_full + ((a.x0 + i) >> 1)));
        const __m512 b16 = _mm512_permutexvar_ps(dup_idx, bh8);
        const __m512 ic = _mm512_div_ps(ic_base, _mm512_mul_ps(b16, b16));
        const __m512 g0 = _mm512_loadu_ps(a.g0p + i), r0 = _mm512_loadu_ps(a.r0p + i), b0 = _mm512_loadu_ps(a.b0p + i);
        __m512 wr = k0_v, vr = _mm512_mul_ps(k0_v, r0), wb = k0_v, vb = _mm512_mul_ps(k0_v, b0);
        for (int t = 0; t < 4; ++t) {
            const __m512 gq = _mm512_loadu_ps(a.gp[t] + i);
            const __m512 rq = _mm512_loadu_ps(a.rp[t] + i);
            const __m512 bq = _mm512_loadu_ps(a.bp[t] + i);
            const __m512 dg = _mm512_sub_ps(gq, g0);
            const __m512 wg = _mm512_mul_ps(kws[t], weight_exp16_avx512(_mm512_mul_ps(_mm512_mul_ps(dg, dg), ig_v)));
            const __m512 rd = _mm512_sub_ps(rq, r0), bd = _mm512_sub_ps(bq, b0);
            const __m512 rw = _mm512_mul_ps(wg, weight_exp16_avx512(_mm512_mul_ps(_mm512_mul_ps(rd, rd), ic)));
            const __m512 bw = _mm512_mul_ps(wg, weight_exp16_avx512(_mm512_mul_ps(_mm512_mul_ps(bd, bd), ic)));
            wr = _mm512_add_ps(wr, rw); vr = _mm512_fmadd_ps(rw, rq, vr);
            wb = _mm512_add_ps(wb, bw); vb = _mm512_fmadd_ps(bw, bq, vb);
        }
        _mm512_storeu_ps(a.dr_out + i, _mm512_div_ps(vr, _mm512_max_ps(wr, eps_v)));
        _mm512_storeu_ps(a.db_out + i, _mm512_div_ps(vb, _mm512_max_ps(wb, eps_v)));
    }
    return i;
}

__attribute__((target("avx512f,avx512vl,fma")))
inline std::size_t gat_fwd_span_avx512(
    const float* __restrict green, float* __restrict z,
    std::size_t i_start, std::size_t i_end,
    float ped_f, float gain_f, float gat_c_f, float sqrt_c_f
) {
    const __m512 zero_v = _mm512_setzero_ps(), two_v = _mm512_set1_ps(2.0f);
    const __m512 ped_v = _mm512_set1_ps(ped_f), gain_v = _mm512_set1_ps(gain_f);
    const __m512 gatc_v = _mm512_set1_ps(gat_c_f), sqrtc_v = _mm512_set1_ps(sqrt_c_f);
    std::size_t i = i_start;
    for (; i + 16 <= i_end; i += 16) {
        const __m512 v = _mm512_max_ps(zero_v, _mm512_add_ps(_mm512_loadu_ps(green + i), ped_v));
        const __m512 den_s = _mm512_add_ps(_mm512_sqrt_ps(_mm512_fmadd_ps(gain_v, v, gatc_v)), sqrtc_v);
        _mm512_storeu_ps(z + i, _mm512_div_ps(_mm512_mul_ps(two_v, v), den_s));
    }
    return i;
}

__attribute__((target("avx512f,avx512vl,fma")))
inline int gat_inv_row_avx512(
    const float* __restrict zm, const float* __restrict z0, const float* __restrict zp,
    const float* __restrict dn, const float* __restrict vr, float* __restrict gr,
    int x_start, int W2, float sqrt_c_f, float q_gain, float ped_f
) {
    const __m512 sc_v = _mm512_set1_ps(sqrt_c_f), qg_v = _mm512_set1_ps(q_gain), ped_v = _mm512_set1_ps(ped_f);
    const __m512i idx_4x4 = _mm512_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
    int x = x_start;
    for (; x + 16 <= W2 - 1; x += 16) {
        const __m512 m_l = _mm512_loadu_ps(zm + x - 1), m_c = _mm512_loadu_ps(zm + x), m_r = _mm512_loadu_ps(zm + x + 1);
        const __m512 c_l = _mm512_loadu_ps(z0 + x - 1), c_c = _mm512_loadu_ps(z0 + x), c_r = _mm512_loadu_ps(z0 + x + 1);
        const __m512 p_l = _mm512_loadu_ps(zp + x - 1), p_c = _mm512_loadu_ps(zp + x), p_r = _mm512_loadu_ps(zp + x + 1);
        const __m512 lo = _mm512_min_ps(_mm512_min_ps(_mm512_min_ps(m_l, m_c), _mm512_min_ps(m_r, c_l)),
                                        _mm512_min_ps(_mm512_min_ps(c_c, c_r), _mm512_min_ps(p_l, _mm512_min_ps(p_c, p_r))));
        const __m512 hi = _mm512_max_ps(_mm512_max_ps(_mm512_max_ps(m_l, m_c), _mm512_max_ps(m_r, c_l)),
                                        _mm512_max_ps(_mm512_max_ps(c_c, c_r), _mm512_max_ps(p_l, _mm512_max_ps(p_c, p_r))));
        const __m512 v = _mm512_min_ps(hi, _mm512_max_ps(lo, _mm512_loadu_ps(dn + x)));
        const __m512 dv = _mm512_permutexvar_ps(idx_4x4, _mm512_castps128_ps512(_mm_loadu_ps(vr + (x >> 2))));
        _mm512_storeu_ps(gr + x, _mm512_sub_ps(_mm512_fmadd_ps(qg_v, _mm512_fmadd_ps(v, v, dv), _mm512_mul_ps(sc_v, v)), ped_v));
    }
    return x;
}
#endif

inline void atrous_h_span_simd(
    const float* __restrict g_row, const float* __restrict sr_row, const float* __restrict sb_row,
    const float* __restrict b_row, float* __restrict dr_out, float* __restrict db_out,
    int x0, int x1, int step, int step2, float inv_g, float inv_c
) {
    constexpr float k0 = 6.0f / 16.0f, k1 = 4.0f / 16.0f, k2 = 1.0f / 16.0f;
    int x = x0;
    if ((x & 1) && x < x1) {
        const float boost = b_row[x >> 1];
        const float ic = inv_c / (boost * boost);
        const float g0 = g_row[x], r0 = sr_row[x], b0 = sb_row[x];
        float wr = k0, vr = k0 * r0, wb = k0, vb = k0 * b0;
        const int qs[4] = {x - step2, x - step, x + step, x + step2};
        const float kws[4] = {k2, k1, k1, k2};
        for (int t = 0; t < 4; ++t) {
            const int q = qs[t];
            const float dg = g_row[q] - g0;
            const float wg = kws[t] * weight_exp(dg * dg * inv_g);
            const float rq = sr_row[q], bq = sb_row[q];
            const float rd = rq - r0, bd = bq - b0;
            const float rw = wg * weight_exp(rd * rd * ic);
            const float bw = wg * weight_exp(bd * bd * ic);
            wr += rw; vr += rw * rq;
            wb += bw; vb += bw * bq;
        }
        dr_out[x - x0] = vr / std::max(wr, 1e-12f);
        db_out[x - x0] = vb / std::max(wb, 1e-12f);
        ++x;
    }
#if defined(__aarch64__)
    const float32x4_t k0_v = vdupq_n_f32(k0), k1_v = vdupq_n_f32(k1), k2_v = vdupq_n_f32(k2);
    const float32x4_t ig_v = vdupq_n_f32(inv_g), ic_base = vdupq_n_f32(inv_c), eps_v = vdupq_n_f32(1e-12f);
    for (; x + 4 <= x1; x += 4) {
        const float32x2_t bh2 = vld1_f32(b_row + (x >> 1));
        const float32x4_t b4 = vzip1q_f32(vcombine_f32(bh2, bh2), vcombine_f32(bh2, bh2));
        const float32x4_t ic = vdivq_f32(ic_base, vmulq_f32(b4, b4));
        const float32x4_t g0 = vld1q_f32(g_row + x), r0 = vld1q_f32(sr_row + x), b0 = vld1q_f32(sb_row + x);
        float32x4_t wr = k0_v, vr = vmulq_f32(k0_v, r0), wb = k0_v, vb = vmulq_f32(k0_v, b0);
        auto htap = [&](int off, float32x4_t kw) {
            const float32x4_t gq = vld1q_f32(g_row + x + off);
            const float32x4_t rq = vld1q_f32(sr_row + x + off);
            const float32x4_t bq = vld1q_f32(sb_row + x + off);
            const float32x4_t dg = vsubq_f32(gq, g0);
            const float32x4_t wg = vmulq_f32(kw, weight_exp4_neon(vmulq_f32(vmulq_f32(dg, dg), ig_v)));
            const float32x4_t rd = vsubq_f32(rq, r0), bd = vsubq_f32(bq, b0);
            const float32x4_t rw = vmulq_f32(wg, weight_exp4_neon(vmulq_f32(vmulq_f32(rd, rd), ic)));
            const float32x4_t bw = vmulq_f32(wg, weight_exp4_neon(vmulq_f32(vmulq_f32(bd, bd), ic)));
            wr = vaddq_f32(wr, rw); vr = vfmaq_f32(vr, rw, rq);
            wb = vaddq_f32(wb, bw); vb = vfmaq_f32(vb, bw, bq);
        };
        htap(-step2, k2_v); htap(-step, k1_v); htap(step, k1_v); htap(step2, k2_v);
        vst1q_f32(dr_out + (x - x0), vdivq_f32(vr, vmaxq_f32(wr, eps_v)));
        vst1q_f32(db_out + (x - x0), vdivq_f32(vb, vmaxq_f32(wb, eps_v)));
    }
#elif defined(__AVX2__)
#if HERAW_AVX512_ENABLED
    if (heraw_cache::use_avx512() && x + 16 <= x1) {
        x = atrous_h_span_avx512(g_row, sr_row, sb_row, b_row, dr_out, db_out, x, x0, x1, step, step2, inv_g, inv_c);
    }
#endif
    const __m256 k0_v = _mm256_set1_ps(k0), k1_v = _mm256_set1_ps(k1), k2_v = _mm256_set1_ps(k2);
    const __m256 ig_v = _mm256_set1_ps(inv_g), ic_base = _mm256_set1_ps(inv_c), eps_v = _mm256_set1_ps(1e-12f);
    const __m256i dup_idx = _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3);
    for (; x + 8 <= x1; x += 8) {
        const __m256 bh4 = _mm256_castps128_ps256(_mm_loadu_ps(b_row + (x >> 1)));
        const __m256 b8 = _mm256_permutevar8x32_ps(bh4, dup_idx);
        const __m256 ic = _mm256_div_ps(ic_base, _mm256_mul_ps(b8, b8));
        const __m256 g0 = _mm256_loadu_ps(g_row + x), r0 = _mm256_loadu_ps(sr_row + x), b0 = _mm256_loadu_ps(sb_row + x);
        __m256 wr = k0_v, vr = _mm256_mul_ps(k0_v, r0), wb = k0_v, vb = _mm256_mul_ps(k0_v, b0);
        auto htap = [&](int off, __m256 kw) {
            const __m256 gq = _mm256_loadu_ps(g_row + x + off);
            const __m256 rq = _mm256_loadu_ps(sr_row + x + off);
            const __m256 bq = _mm256_loadu_ps(sb_row + x + off);
            const __m256 dg = _mm256_sub_ps(gq, g0);
            const __m256 wg = _mm256_mul_ps(kw, weight_exp8_avx2(_mm256_mul_ps(_mm256_mul_ps(dg, dg), ig_v)));
            const __m256 rd = _mm256_sub_ps(rq, r0), bd = _mm256_sub_ps(bq, b0);
            const __m256 rw = _mm256_mul_ps(wg, weight_exp8_avx2(_mm256_mul_ps(_mm256_mul_ps(rd, rd), ic)));
            const __m256 bw = _mm256_mul_ps(wg, weight_exp8_avx2(_mm256_mul_ps(_mm256_mul_ps(bd, bd), ic)));
            wr = _mm256_add_ps(wr, rw); vr = _mm256_fmadd_ps(rw, rq, vr);
            wb = _mm256_add_ps(wb, bw); vb = _mm256_fmadd_ps(bw, bq, vb);
        };
        htap(-step2, k2_v); htap(-step, k1_v); htap(step, k1_v); htap(step2, k2_v);
        _mm256_storeu_ps(dr_out + (x - x0), _mm256_div_ps(vr, _mm256_max_ps(wr, eps_v)));
        _mm256_storeu_ps(db_out + (x - x0), _mm256_div_ps(vb, _mm256_max_ps(wb, eps_v)));
    }
#endif
    for (; x < x1; ++x) {
        const float boost = b_row[x >> 1];
        const float ic = inv_c / (boost * boost);
        const float g0 = g_row[x], r0 = sr_row[x], b0 = sb_row[x];
        float wr = k0, vr = k0 * r0, wb = k0, vb = k0 * b0;
        const int qs[4] = {x - step2, x - step, x + step, x + step2};
        const float kws[4] = {k2, k1, k1, k2};
        for (int t = 0; t < 4; ++t) {
            const int q = qs[t];
            const float dg = g_row[q] - g0;
            const float wg = kws[t] * weight_exp(dg * dg * inv_g);
            const float rq = sr_row[q], bq = sb_row[q];
            const float rd = rq - r0, bd = bq - b0;
            const float rw = wg * weight_exp(rd * rd * ic);
            const float bw = wg * weight_exp(bd * bd * ic);
            wr += rw; vr += rw * rq;
            wb += bw; vb += bw * bq;
        }
        dr_out[x - x0] = vr / std::max(wr, 1e-12f);
        db_out[x - x0] = vb / std::max(wb, 1e-12f);
    }
}

// Evaluates vertical 5-tap guided pass for x in [x0, x1), where rm2p..bp2p point to the start of [x0, x1)
// (stride 0 offset from x0), while gm2..gp2 and dr_row/db_row are indexed at + (x - x0).
inline void atrous_v_span_simd(
    const float* __restrict gm2, const float* __restrict gm1, const float* __restrict g0p,
    const float* __restrict gp1, const float* __restrict gp2,
    const float* __restrict rm2p, const float* __restrict rm1p, const float* __restrict r0p,
    const float* __restrict rp1p, const float* __restrict rp2p,
    const float* __restrict bm2p, const float* __restrict bm1p, const float* __restrict b0p,
    const float* __restrict bp1p, const float* __restrict bp2p,
    const float* __restrict b_row_full, int x0, int len,
    float* __restrict dr_out, float* __restrict db_out, float inv_g, float inv_c
) {
    constexpr float k0 = 6.0f / 16.0f, k1 = 4.0f / 16.0f, k2 = 1.0f / 16.0f;
    int i = 0;
    if (((x0 + i) & 1) && i < len) {
        const float boost = b_row_full[(x0 + i) >> 1];
        const float ic = inv_c / (boost * boost);
        const float g0 = g0p[i], r0 = r0p[i], b0 = b0p[i];
        float wr = k0, vr = k0 * r0, wb = k0, vb = k0 * b0;
        auto vtap = [&](float gq, float rq, float bq, float kw) {
            const float dg = gq - g0;
            const float wg = kw * weight_exp(dg * dg * inv_g);
            const float rd = rq - r0, bd = bq - b0;
            const float rw = wg * weight_exp(rd * rd * ic);
            const float bw = wg * weight_exp(bd * bd * ic);
            wr += rw; vr += rw * rq;
            wb += bw; vb += bw * bq;
        };
        vtap(gm2[i], rm2p[i], bm2p[i], k2);
        vtap(gm1[i], rm1p[i], bm1p[i], k1);
        vtap(gp1[i], rp1p[i], bp1p[i], k1);
        vtap(gp2[i], rp2p[i], bp2p[i], k2);
        dr_out[i] = vr / std::max(wr, 1e-12f);
        db_out[i] = vb / std::max(wb, 1e-12f);
        ++i;
    }
#if defined(__aarch64__)
    const float32x4_t k0_v = vdupq_n_f32(k0), k1_v = vdupq_n_f32(k1), k2_v = vdupq_n_f32(k2);
    const float32x4_t ig_v = vdupq_n_f32(inv_g), ic_base = vdupq_n_f32(inv_c), eps_v = vdupq_n_f32(1e-12f);
    for (; i + 4 <= len; i += 4) {
        const float32x2_t bh2 = vld1_f32(b_row_full + ((x0 + i) >> 1));
        const float32x4_t b4 = vzip1q_f32(vcombine_f32(bh2, bh2), vcombine_f32(bh2, bh2));
        const float32x4_t ic = vdivq_f32(ic_base, vmulq_f32(b4, b4));
        const float32x4_t g0 = vld1q_f32(g0p + i), r0 = vld1q_f32(r0p + i), b0 = vld1q_f32(b0p + i);
        float32x4_t wr = k0_v, vr = vmulq_f32(k0_v, r0), wb = k0_v, vb = vmulq_f32(k0_v, b0);
        auto vtap4 = [&](const float* gp, const float* rp, const float* bp, float32x4_t kw) {
            const float32x4_t gq = vld1q_f32(gp + i), rq = vld1q_f32(rp + i), bq = vld1q_f32(bp + i);
            const float32x4_t dg = vsubq_f32(gq, g0);
            const float32x4_t wg = vmulq_f32(kw, weight_exp4_neon(vmulq_f32(vmulq_f32(dg, dg), ig_v)));
            const float32x4_t rd = vsubq_f32(rq, r0), bd = vsubq_f32(bq, b0);
            const float32x4_t rw = vmulq_f32(wg, weight_exp4_neon(vmulq_f32(vmulq_f32(rd, rd), ic)));
            const float32x4_t bw = vmulq_f32(wg, weight_exp4_neon(vmulq_f32(vmulq_f32(bd, bd), ic)));
            wr = vaddq_f32(wr, rw); vr = vfmaq_f32(vr, rw, rq);
            wb = vaddq_f32(wb, bw); vb = vfmaq_f32(vb, bw, bq);
        };
        vtap4(gm2, rm2p, bm2p, k2_v);
        vtap4(gm1, rm1p, bm1p, k1_v);
        vtap4(gp1, rp1p, bp1p, k1_v);
        vtap4(gp2, rp2p, bp2p, k2_v);
        vst1q_f32(dr_out + i, vdivq_f32(vr, vmaxq_f32(wr, eps_v)));
        vst1q_f32(db_out + i, vdivq_f32(vb, vmaxq_f32(wb, eps_v)));
    }
#elif defined(__AVX2__)
#if HERAW_AVX512_ENABLED
    if (heraw_cache::use_avx512() && i + 16 <= len) {
        const AtrousVSpanArgs va{{gm2, gm1, gp1, gp2}, {rm2p, rm1p, rp1p, rp2p}, {bm2p, bm1p, bp1p, bp2p},
                                 g0p, r0p, b0p, b_row_full, dr_out, db_out, x0, i, len, inv_g, inv_c};
        i = atrous_v_span_avx512(va);
    }
#endif
    const __m256 k0_v = _mm256_set1_ps(k0), k1_v = _mm256_set1_ps(k1), k2_v = _mm256_set1_ps(k2);
    const __m256 ig_v = _mm256_set1_ps(inv_g), ic_base = _mm256_set1_ps(inv_c), eps_v = _mm256_set1_ps(1e-12f);
    const __m256i dup_idx = _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3);
    for (; i + 8 <= len; i += 8) {
        const __m256 bh4 = _mm256_castps128_ps256(_mm_loadu_ps(b_row_full + ((x0 + i) >> 1)));
        const __m256 b8 = _mm256_permutevar8x32_ps(bh4, dup_idx);
        const __m256 ic = _mm256_div_ps(ic_base, _mm256_mul_ps(b8, b8));
        const __m256 g0 = _mm256_loadu_ps(g0p + i), r0 = _mm256_loadu_ps(r0p + i), b0 = _mm256_loadu_ps(b0p + i);
        __m256 wr = k0_v, vr = _mm256_mul_ps(k0_v, r0), wb = k0_v, vb = _mm256_mul_ps(k0_v, b0);
        auto vtap8 = [&](const float* gp, const float* rp, const float* bp, __m256 kw) {
            const __m256 gq = _mm256_loadu_ps(gp + i), rq = _mm256_loadu_ps(rp + i), bq = _mm256_loadu_ps(bp + i);
            const __m256 dg = _mm256_sub_ps(gq, g0);
            const __m256 wg = _mm256_mul_ps(kw, weight_exp8_avx2(_mm256_mul_ps(_mm256_mul_ps(dg, dg), ig_v)));
            const __m256 rd = _mm256_sub_ps(rq, r0), bd = _mm256_sub_ps(bq, b0);
            const __m256 rw = _mm256_mul_ps(wg, weight_exp8_avx2(_mm256_mul_ps(_mm256_mul_ps(rd, rd), ic)));
            const __m256 bw = _mm256_mul_ps(wg, weight_exp8_avx2(_mm256_mul_ps(_mm256_mul_ps(bd, bd), ic)));
            wr = _mm256_add_ps(wr, rw); vr = _mm256_fmadd_ps(rw, rq, vr);
            wb = _mm256_add_ps(wb, bw); vb = _mm256_fmadd_ps(bw, bq, vb);
        };
        vtap8(gm2, rm2p, bm2p, k2_v);
        vtap8(gm1, rm1p, bm1p, k1_v);
        vtap8(gp1, rp1p, bp1p, k1_v);
        vtap8(gp2, rp2p, bp2p, k2_v);
        _mm256_storeu_ps(dr_out + i, _mm256_div_ps(vr, _mm256_max_ps(wr, eps_v)));
        _mm256_storeu_ps(db_out + i, _mm256_div_ps(vb, _mm256_max_ps(wb, eps_v)));
    }
#endif
    for (; i < len; ++i) {
        const float boost = b_row_full[(x0 + i) >> 1];
        const float ic = inv_c / (boost * boost);
        const float g0 = g0p[i], r0 = r0p[i], b0 = b0p[i];
        float wr = k0, vr = k0 * r0, wb = k0, vb = k0 * b0;
        auto vtap = [&](float gq, float rq, float bq, float kw) {
            const float dg = gq - g0;
            const float wg = kw * weight_exp(dg * dg * inv_g);
            const float rd = rq - r0, bd = bq - b0;
            const float rw = wg * weight_exp(rd * rd * ic);
            const float bw = wg * weight_exp(bd * bd * ic);
            wr += rw; vr += rw * rq;
            wb += bw; vb += bw * bq;
        };
        vtap(gm2[i], rm2p[i], bm2p[i], k2);
        vtap(gm1[i], rm1p[i], bm1p[i], k1);
        vtap(gp1[i], rp1p[i], bp1p[i], k1);
        vtap(gp2[i], rp2p[i], bp2p[i], k2);
        dr_out[i] = vr / std::max(wr, 1e-12f);
        db_out[i] = vb / std::max(wb, 1e-12f);
    }
}

} // namespace detail_s45

// Global edge statistics are shared across ROIs just like the GPU kernels.
inline void atrous_global(float* red, float* blue, const float* green,
                          const heraw_gpu::Stage45GpuJob& j, float sigma,
                          UninitBuf<float>& tr_buf, UninitBuf<float>& tb_buf) {
    if (j.global_edge_p80 < 0.0f) {
        atrous_chroma_denoise(red, blue, green, j.H1, j.W1, sigma, j.n_threads,
                             j.snr_boost_half, j.H1/2, j.W1/2, j.is_astro);
        return;
    }
    if (sigma <= 1e-6f) return;
    const int H = j.H1, W = j.W1, Wh = W / 2;
    const float sig_c = std::max(1e-5f, sigma);
    const float sig_g = std::max(1e-4f, 0.5f * sigma);
    const float edge = std::max(j.is_astro ? 0.14f : 3.0f * sig_g,
                               2.2f * j.global_edge_p80 + 0.006f);
    const float inv_g = 1.0f / (2.0f * edge * edge);
    constexpr float k0 = 6.0f / 16.0f, k1 = 4.0f / 16.0f, k2 = 1.0f / 16.0f;
    tr_buf.resize(size_t(H) * W);
    tb_buf.resize(size_t(H) * W);
    float* tr = tr_buf.data();
    float* tb = tb_buf.data();

    // Full-frame untiled fast path: fuse Pass H + Pass V per level inside a private L1/L2 ring buffer
    // (<= 132..264 KB/thread even at step=32), ping-ponging (red,blue) <-> (tr,tb) across the 6 levels (even count).
    // Cuts full-frame DRAM passes from 12 down to 6 with 0% halo recomputation!
    if (j.core_x1 <= 0 && j.core_y1 <= 0 && H >= 2 && W >= 2) {
        const int x_tile = (heraw_cache::host_cache_profile().l2_per_core_bytes >= 1024u * 1024u) ? 256 : 128;
        const int n_xtiles = (W + x_tile - 1) / x_tile;
        const int max_nt = std::max(1, j.n_threads);
        const int xt_chunk = std::max(1, (n_xtiles + max_nt * 2 - 1) / (max_nt * 2));
        struct alignas(64) AtrousRingSlot { std::vector<float> r, b; };
        std::vector<AtrousRingSlot> tls_ring(max_nt);

        for (int lv = 0; lv < 6; ++lv) {
            const int step = 1 << lv;
            const int step2 = 2 * step;
            const float sc = 2.7f * sig_c * std::pow(0.56f, float(lv)) + 0.0015f;
            const float inv_c = 1.0f / (2.0f * sc * sc);
            const float* __restrict src_r = (lv & 1) ? tr : red;
            const float* __restrict src_b = (lv & 1) ? tb : blue;
            float* __restrict dst_r = (lv & 1) ? red : tr;
            float* __restrict dst_b = (lv & 1) ? blue : tb;
            const int ring_rows = 4 * step + 1;

            parallel_for_rows(n_xtiles, max_nt, [&](int xt0, int xt1, int tid) {
                auto& rr_vec = tls_ring[tid].r;
                auto& rb_vec = tls_ring[tid].b;
                const size_t need_sz = size_t(ring_rows) * size_t(x_tile);
                if (rr_vec.size() < need_sz) { rr_vec.resize(need_sz); rb_vec.resize(need_sz); }
                float* __restrict ring_r = rr_vec.data();
                float* __restrict ring_b = rb_vec.data();

                for (int xt = xt0; xt < xt1; ++xt) {
                    const int xb0 = xt * x_tile;
                    const int xb1 = std::min(W, xb0 + x_tile);
                    const int tw = xb1 - xb0;
                    const int xin0 = std::max(xb0, step2);
                    const int xin1 = std::min(xb1, W - step2);

                    auto compute_h_row = [&](int hy) {
                        const size_t row = size_t(hy) * W;
                        const float* __restrict g_row = green + row;
                        const float* __restrict sr_row = src_r + row;
                        const float* __restrict sb_row = src_b + row;
                        const float* __restrict b_row = j.snr_boost_half + size_t(hy >> 1) * Wh;
                        const size_t slot_off = size_t(hy % ring_rows) * size_t(x_tile);
                        float* __restrict dr_s = ring_r + slot_off;
                        float* __restrict db_s = ring_b + slot_off;

                        auto eval_clamped = [&](int x) {
                            const float boost = b_row[x >> 1];
                            const float ic = inv_c / (boost * boost);
                            const float g0 = g_row[x], r0 = sr_row[x], b0 = sb_row[x];
                            float wr = k0, vr = k0 * r0, wb = k0, vb = k0 * b0;
                            const int qs[4] = {clamp_i(x - step2, 0, W - 1), clamp_i(x - step, 0, W - 1),
                                               clamp_i(x + step, 0, W - 1),  clamp_i(x + step2, 0, W - 1)};
                            const float kws[4] = {k2, k1, k1, k2};
                            for (int t = 0; t < 4; ++t) {
                                const int q = qs[t];
                                const float dg = g_row[q] - g0;
                                const float wg = kws[t] * weight_exp(dg * dg * inv_g);
                                const float rq = sr_row[q], bq = sb_row[q];
                                const float rd = rq - r0, bd = bq - b0;
                                const float rw = wg * weight_exp(rd * rd * ic);
                                const float bw = wg * weight_exp(bd * bd * ic);
                                wr += rw; vr += rw * rq;
                                wb += bw; vb += bw * bq;
                            }
                            dr_s[x - xb0] = vr / std::max(wr, 1e-12f);
                            db_s[x - xb0] = vb / std::max(wb, 1e-12f);
                        };

                        if (xin0 >= xin1) {
                            for (int x = xb0; x < xb1; ++x) eval_clamped(x);
                        } else {
                            for (int x = xb0; x < xin0; ++x) eval_clamped(x);
                            detail_s45::atrous_h_span_simd(
                                g_row, sr_row, sb_row, b_row,
                                dr_s + (xin0 - xb0), db_s + (xin0 - xb0),
                                xin0, xin1, step, step2, inv_g, inv_c);
                            for (int x = xin1; x < xb1; ++x) eval_clamped(x);
                        }
                    };

                    const int prime_end = std::min(H - 1, step2 - 1);
                    for (int hy = 0; hy <= prime_end; ++hy) compute_h_row(hy);

                    for (int y = 0; y < H; ++y) {
                        if (y + step2 < H) compute_h_row(y + step2);
                        const int ym2 = clamp_i(y - step2, 0, H - 1);
                        const int ym1 = clamp_i(y - step,  0, H - 1);
                        const int yp1 = clamp_i(y + step,  0, H - 1);
                        const int yp2 = clamp_i(y + step2, 0, H - 1);

                        const size_t s_m2 = size_t(ym2 % ring_rows) * size_t(x_tile);
                        const size_t s_m1 = size_t(ym1 % ring_rows) * size_t(x_tile);
                        const size_t s_0  = size_t(y   % ring_rows) * size_t(x_tile);
                        const size_t s_p1 = size_t(yp1 % ring_rows) * size_t(x_tile);
                        const size_t s_p2 = size_t(yp2 % ring_rows) * size_t(x_tile);

                        detail_s45::atrous_v_span_simd(
                            green + size_t(ym2) * W + xb0,
                            green + size_t(ym1) * W + xb0,
                            green + size_t(y)   * W + xb0,
                            green + size_t(yp1) * W + xb0,
                            green + size_t(yp2) * W + xb0,
                            ring_r + s_m2, ring_r + s_m1, ring_r + s_0, ring_r + s_p1, ring_r + s_p2,
                            ring_b + s_m2, ring_b + s_m1, ring_b + s_0, ring_b + s_p1, ring_b + s_p2,
                            j.snr_boost_half + size_t(y >> 1) * Wh,
                            xb0, tw,
                            dst_r + size_t(y) * W + xb0,
                            dst_b + size_t(y) * W + xb0,
                            inv_g, inv_c);
                    }
                }
            }, xt_chunk);
        }
        return;
    }

    const int rl_halo = j.psf_iters > 0 ?
        j.psf_iters * heraw_gpu::make_olpf_psf_pc(1, 1, 0, j.psf_sigma, j.olpf_mode).radius + 8 : 0;

    for (int lv = 0; lv < 6; ++lv) {
        const int step = 1 << lv;
        const int step2 = 2 * step;
        const float sc = 2.7f * sig_c * std::pow(0.56f, float(lv)) + 0.0015f;
        const float inv_c = 1.0f / (2.0f * sc * sc);
        const int remaining = 2 * (64 - (1 << (lv + 1)));
        const int hx = remaining + rl_halo + 3;

        // Pass 0: Horizontal (red, blue -> tr, tb)
        {
            const int hy = hx + step2;
            const int rx0 = j.core_x1 > 0 ? std::max(0, j.core_x0 - hx) : 0;
            const int rx1 = j.core_x1 > 0 ? std::min(W, j.core_x1 + hx) : W;
            const int ry0 = j.core_y1 > 0 ? std::max(0, j.core_y0 - hy) : 0;
            const int ry1 = j.core_y1 > 0 ? std::min(H, j.core_y1 + hy) : H;
            const int xin0 = std::max(rx0, step2);
            const int xin1 = std::min(rx1, W - step2);

            parallel_for_rows(H, j.n_threads, [&](int y0, int y1, int) {
                for (int y = y0; y < y1; ++y) {
                    const size_t row = size_t(y) * W;
                    if (y < ry0 || y >= ry1) {
                        std::copy_n(red + row, W, tr + row);
                        std::copy_n(blue + row, W, tb + row);
                        continue;
                    }
                    if (rx0 > 0) {
                        std::copy_n(red + row, rx0, tr + row);
                        std::copy_n(blue + row, rx0, tb + row);
                    }
                    if (rx1 < W) {
                        std::copy_n(red + row + rx1, W - rx1, tr + row + rx1);
                        std::copy_n(blue + row + rx1, W - rx1, tb + row + rx1);
                    }
                    const float* __restrict g_row = green + row;
                    const float* __restrict sr_row = red + row;
                    const float* __restrict sb_row = blue + row;
                    float* __restrict dr_row = tr + row;
                    float* __restrict db_row = tb + row;
                    const float* __restrict b_row = j.snr_boost_half + size_t(y >> 1) * Wh;

                    auto eval_x = [&](int x, int xm2, int xm1, int xp1, int xp2) {
                        const float boost = b_row[x >> 1];
                        const float ic = inv_c / (boost * boost);
                        const float g0 = g_row[x], r0 = sr_row[x], b0 = sb_row[x];
                        float wr = k0, vr = k0 * r0, wb = k0, vb = k0 * b0;
                        auto tap = [&](int q, float kw) {
                            const float dg = g_row[q] - g0;
                            const float wg = kw * weight_exp(dg * dg * inv_g);
                            const float rq = sr_row[q], bq = sb_row[q];
                            const float rd = rq - r0, bd = bq - b0;
                            const float rw = wg * weight_exp(rd * rd * ic);
                            const float bw = wg * weight_exp(bd * bd * ic);
                            wr += rw; vr += rw * rq;
                            wb += bw; vb += bw * bq;
                        };
                        tap(xm2, k2); tap(xm1, k1); tap(xp1, k1); tap(xp2, k2);
                        dr_row[x] = vr / std::max(wr, 1e-12f);
                        db_row[x] = vb / std::max(wb, 1e-12f);
                    };

                    if (xin0 >= xin1) {
                        for (int x = rx0; x < rx1; ++x)
                            eval_x(x, clamp_i(x - step2, 0, W - 1), clamp_i(x - step, 0, W - 1),
                                   clamp_i(x + step, 0, W - 1), clamp_i(x + step2, 0, W - 1));
                    } else {
                        for (int x = rx0; x < xin0; ++x)
                            eval_x(x, clamp_i(x - step2, 0, W - 1), clamp_i(x - step, 0, W - 1),
                                   clamp_i(x + step, 0, W - 1), clamp_i(x + step2, 0, W - 1));
                        detail_s45::atrous_h_span_simd(
                            g_row, sr_row, sb_row, b_row,
                            dr_row + xin0, db_row + xin0,
                            xin0, xin1, step, step2, inv_g, inv_c);
                        for (int x = xin1; x < rx1; ++x)
                            eval_x(x, clamp_i(x - step2, 0, W - 1), clamp_i(x - step, 0, W - 1),
                                   clamp_i(x + step, 0, W - 1), clamp_i(x + step2, 0, W - 1));
                    }
                }
            });
        }

        // Pass 1: Vertical (tr, tb -> red, blue)
        {
            const int hy = hx;
            const int rx0 = j.core_x1 > 0 ? std::max(0, j.core_x0 - hx) : 0;
            const int rx1 = j.core_x1 > 0 ? std::min(W, j.core_x1 + hx) : W;
            const int ry0 = j.core_y1 > 0 ? std::max(0, j.core_y0 - hy) : 0;
            const int ry1 = j.core_y1 > 0 ? std::min(H, j.core_y1 + hy) : H;

            parallel_for_rows(H, j.n_threads, [&](int y0, int y1, int) {
                for (int y = y0; y < y1; ++y) {
                    const size_t row = size_t(y) * W;
                    if (y < ry0 || y >= ry1) {
                        std::copy_n(tr + row, W, red + row);
                        std::copy_n(tb + row, W, blue + row);
                        continue;
                    }
                    if (rx0 > 0) {
                        std::copy_n(tr + row, rx0, red + row);
                        std::copy_n(tb + row, rx0, tb_buf.data() == tb ? blue + row : blue + row);
                    }
                    if (rx1 < W) {
                        std::copy_n(tr + row + rx1, W - rx1, red + row + rx1);
                        std::copy_n(tb + row + rx1, W - rx1, blue + row + rx1);
                    }
                    if (rx1 > rx0) {
                        const size_t rm2 = size_t(clamp_i(y - step2, 0, H - 1)) * W + rx0;
                        const size_t rm1 = size_t(clamp_i(y - step,  0, H - 1)) * W + rx0;
                        const size_t r0o = row + rx0;
                        const size_t rp1 = size_t(clamp_i(y + step,  0, H - 1)) * W + rx0;
                        const size_t rp2 = size_t(clamp_i(y + step2, 0, H - 1)) * W + rx0;
                        detail_s45::atrous_v_span_simd(
                            green + rm2, green + rm1, green + r0o, green + rp1, green + rp2,
                            tr + rm2, tr + rm1, tr + r0o, tr + rp1, tr + rp2,
                            tb + rm2, tb + rm1, tb + r0o, tb + rp1, tb + rp2,
                            j.snr_boost_half + size_t(y >> 1) * Wh,
                            rx0, rx1 - rx0,
                            red + r0o, blue + r0o, inv_g, inv_c);
                    }
                }
            });
        }
    }
}

inline bool run_stage45_cpu_roi_ws(heraw_gpu::Stage45GpuJob& j, CpuStage45Scratch& sc) {
    const auto start = std::chrono::steady_clock::now();
    auto cost_start = start;
    j.out_wall_seconds.fill(0); j.out_device_seconds.fill(0);
    auto cost_mark = [&](size_t phase) {
        const auto now = std::chrono::steady_clock::now();
        j.out_wall_seconds[phase] += std::chrono::duration<double>(now - cost_start).count();
        cost_start = now;
    };
    const int H = j.H1, W = j.W1, Hh = H / 2, Wh = W / 2, H2 = H * 2, W2 = W * 2;
    const int nt = std::max(1, j.n_threads);
    const auto& cache_prof = heraw_cache::host_cache_profile();
    const int s4_xtile = cache_prof.stage4_x_tile();
    const auto& frames = *j.frames_lin;
    const auto& fy = *j.flows_fy;
    const auto& fx = *j.flows_fx;
    const auto& motion = *j.mw_half;
    const float* cov = j.inv_cov;
    const int cy1 = j.core_y1 > 0 ? j.core_y1 : H;
    const int cx1 = j.core_x1 > 0 ? j.core_x1 : W;

    bool is_bayer = !j.is_xtrans;
    if (is_bayer) {
        for (int r = 0; r < 6 && is_bayer; ++r)
            for (int c = 0; c < 6; ++c)
                if (j.cfa6x6[r][c] != j.cfa6x6[r & 1][c & 1]) { is_bayer = false; break; }
        if (is_bayer) {
            const bool g0 = (j.cfa6x6[0][0] == 1) != (j.cfa6x6[0][1] == 1);
            const bool g1 = (j.cfa6x6[1][0] == 1) != (j.cfa6x6[1][1] == 1);
            if (!g0 || !g1) is_bayer = false;
        }
    }
    const int g_col_par[2] = {j.cfa6x6[0][1] == 1 ? 1 : 0, j.cfa6x6[1][1] == 1 ? 1 : 0};
    const uint8_t cfa2x2[2][2] = {{j.cfa6x6[0][0], j.cfa6x6[0][1]}, {j.cfa6x6[1][0], j.cfa6x6[1][1]}};

    // Precompute sparse X-Trans 4x4 Green tap offsets (dr in [-1..2], dc in [-1..2]) per (rb%6, cb%6)
    struct G4Tap { int8_t dr, dc; };
    struct G4TapList { int count = 0; G4Tap taps[16]{}; };
    G4TapList xt_g_taps[6][6];
    if (!is_bayer) {
        for (int rm = 0; rm < 6; ++rm) {
            for (int cm = 0; cm < 6; ++cm) {
                auto& lst = xt_g_taps[rm][cm];
                for (int dr = -1; dr <= 2; ++dr) {
                    const uint8_t* rcfa = j.cfa6x6[(rm + dr + 6) % 6];
                    for (int dc = -1; dc <= 2; ++dc) {
                        if (rcfa[(cm + dc + 6) % 6] == 1) {
                            lst.taps[lst.count++] = {int8_t(dr), int8_t(dc)};
                        }
                    }
                }
            }
        }
    }

    std::vector<float> fw_arr(j.N);
    std::vector<uint8_t> rob_arr(j.N), unit_mw_arr(j.N, 1);
    std::vector<std::array<float, 4>> ois_arr(j.N, {0.0f, 0.0f, 0.0f, 0.0f});
    for (int n = 0; n < j.N; ++n) {
        fw_arr[n] = heraw_gpu::gpu_frame_weight(j, n);
        rob_arr[n] = heraw_gpu::gpu_use_robust(j, n) ? 1 : 0;
        if (j.frame_ois_shifts && j.frame_ois_shifts->size() >= size_t(n + 1) * 4) {
            for (int k = 0; k < 4; ++k) ois_arr[n][k] = (*j.frame_ois_shifts)[size_t(n) * 4 + k];
        }
        if (!j.is_virtual_nd) {
            for (float m : motion[n])
                if (m != 1.0f) { unit_mw_arr[n] = 0; break; }
        }
    }

    if (!j.eta_only) sc.green.resize(size_t(H2) * W2);
    float* green = j.eta_only ? nullptr : sc.green.data();
    std::vector<double> eta_sum(nt, 0.0);
    std::vector<int64_t> eta_count(nt, 0);
    const bool need_eta = j.eta_only || j.global_eta < 0.0 || j.eta_quarter_out != nullptr;
    const bool h_ge2 = (Hh >= 2 && Wh >= 2);

    // Precompute half-grid bilinear coordinates along Y and X so Stage 4 Green
    // avoids 3*N redundant bilerp_clamped index/clamp evaluations per pixel.
    struct HalfAxis { int i0; float f0, f1; };
    std::vector<HalfAxis> gy_axis(H), gx_axis(W);
    if (h_ge2) {
        for (int Y = 0; Y < H; ++Y) {
            const float yc = clamp_f(0.5f * Y + 0.125f, 0.0f, float(Hh - 1));
            const int y0 = clamp_i(int(yc), 0, Hh - 2);
            const float fy_v = clamp_f(yc - float(y0), 0.0f, 1.0f);
            gy_axis[Y] = {y0, 1.0f - fy_v, fy_v};
        }
        for (int X = 0; X < W; ++X) {
            const float xc = clamp_f(0.5f * X + 0.125f, 0.0f, float(Wh - 1));
            const int x0 = clamp_i(int(xc), 0, Wh - 2);
            const float fx_v = clamp_f(xc - float(x0), 0.0f, 1.0f);
            gx_axis[X] = {x0, 1.0f - fx_v, fx_v};
        }
    }

    parallel_for_rows(H, nt, [&](int y0, int y1, int tid) {
        for (int xb = 0; xb < W; xb += s4_xtile) {
            const int xe = std::min(W, xb + s4_xtile);
            for (int Y = y0; Y < y1; ++Y) {
                const float yh = 0.5f * Y + 0.125f;
                const int yi_h = clamp_i(int(yh + 0.5f), 0, Hh - 1);
                const size_t hrow_off = size_t(yi_h) * Wh;
                const HalfAxis hay = h_ge2 ? gy_axis[Y] : HalfAxis{0, 1.0f, 0.0f};
                const size_t bi_row0 = size_t(hay.i0) * Wh;

                for (int X = xb; X < xe; ++X) {
                    const bool eta_site = need_eta && (((Y | X) & 3) == 0 && Y >= j.core_y0 &&
                        Y < cy1 && X >= j.core_x0 && X < cx1);
                    if (j.eta_only && !eta_site) continue;
                    const float xh = 0.5f * X + 0.125f;
                    const size_t hi = hrow_off + clamp_i(int(xh + 0.5f), 0, Wh - 1);
                    const float axx = cov[hi * 3], axy = cov[hi * 3 + 1], ayy = cov[hi * 3 + 2];
                    const float anchor = j.g_guide[hi];
                    const float threshold = 3.5f * std::sqrt(float(j.pg_sig0_sq) +
                        float(j.pg_gain) * std::max(0.0f, anchor)) + 0.008f;

                    const HalfAxis hax = h_ge2 ? gx_axis[X] : HalfAxis{0, 1.0f, 0.0f};
                    const size_t b00 = bi_row0 + hax.i0;
                    const size_t b10 = b00 + Wh;
                    auto sample_half_fast = [&](const float* p) -> float {
                        if (!h_ge2) return p[0];
                        return hay.f0 * (hax.f0 * p[b00] + hax.f1 * p[b00 + 1]) +
                               hay.f1 * (hax.f0 * p[b10] + hax.f1 * p[b10 + 1]);
                    };

#if defined(__aarch64__)
                    float32x4_t ws_v = vdupq_n_f32(0.0f), vs_v = vdupq_n_f32(0.0f);
                    const float32x4_t dlim_v = vdupq_n_f32(6.5f), half_v = vdupq_n_f32(0.5f), zero_v = vdupq_n_f32(0.0f);
#elif defined(__AVX2__)
                    __m128 ws_v = _mm_setzero_ps(), vs_v = _mm_setzero_ps();
                    const __m128 dlim_v = _mm_set1_ps(6.5f), half_v = _mm_set1_ps(0.5f);
                    const __m256 dlim8_v = _mm256_set1_ps(6.5f), half8_v = _mm256_set1_ps(0.5f);
                    const __m256 axx8_v = _mm256_set1_ps(axx), axy2_8_v = _mm256_set1_ps(2.0f * axy), ayy8_v = _mm256_set1_ps(ayy);
#else
                    float ws[4] = {}, vs[4] = {};
#endif
                    float ws0_eta = 0.0f, vs0_eta = 0.0f, w2 = 0.0f;

                    for (int n = 0; n < j.N; ++n) {
                        const float fw = fw_arr[n];
                        if (fw <= 0.0f) continue;
                        const float mw_raw = unit_mw_arr[n] ? 1.0f : sample_half_fast(motion[n].data());
                        const float mw = (j.is_virtual_nd ? 1.0f : mw_raw) * fw;
                        if (mw < 0.02f) continue;
                        const float dy = n ? 2.0f * sample_half_fast(fy[n].data()) : 0.0f;
                        const float dx = n ? 2.0f * sample_half_fast(fx[n].data()) : 0.0f;
                        const float yt = (float(Y + j.origin_y) + dy) - float(j.origin_y);
                        const float xt = (float(X + j.origin_x) + dx) - float(j.origin_x);
                        if (yt < -0.5f || yt > H - 0.5f || xt < -0.5f || xt > W - 0.5f) continue;
                        const int rb = int(std::floor(yt)), cb = int(std::floor(xt));
                        const float* f_ptr = frames[n].data();
                        const bool use_rob = rob_arr[n] != 0;

#if defined(__aarch64__)
                        float32x4_t f_ws = vdupq_n_f32(0.0f), f_vs = vdupq_n_f32(0.0f);
#elif defined(__AVX2__)
                        __m128 f_ws = _mm_setzero_ps(), f_vs = _mm_setzero_ps();
#else
                        float frame_ws[4] = {}, frame_vs[4] = {};
#endif
                        float frame_ws0 = 0.0f, frame_vs0 = 0.0f, frame_w2 = 0.0f;

                        auto accum_green_sample = [&](float v, float yy0, float xx0) {
                            float rob = mw;
                            if (use_rob) {
                                const float ad = std::fabs(v - anchor);
                                if (ad > threshold) { const float q = threshold / ad; rob *= q * q * q; }
                            }
                            if (j.eta_only) {
                                const float d = axx * xx0 * xx0 + 2.0f * axy * xx0 * yy0 + ayy * yy0 * yy0;
                                if (d <= 6.5f) {
                                    const float w = rob * weight_exp(0.5f * d);
                                    frame_ws0 += w; frame_vs0 += w * v;
                                    frame_w2 += w * w;
                                }
                            } else {
                                const float yy1 = yy0 - 0.5f, xx1 = xx0 - 0.5f;
                                const float ax0 = axx * xx0 * xx0, ax1 = axx * xx1 * xx1;
                                const float ay0 = ayy * yy0 * yy0, ay1 = ayy * yy1 * yy1;
                                const float d_arr[4] = {
                                    ax0 + 2.0f * axy * xx0 * yy0 + ay0,
                                    ax1 + 2.0f * axy * xx1 * yy0 + ay0,
                                    ax0 + 2.0f * axy * xx0 * yy1 + ay1,
                                    ax1 + 2.0f * axy * xx1 * yy1 + ay1
                                };
#if defined(__aarch64__)
                                const float32x4_t dv = vld1q_f32(d_arr);
                                const uint32x4_t mask = vcleq_f32(dv, dlim_v);
                                if (vmaxvq_u32(mask) != 0) {
                                    const float32x4_t wv = vbslq_f32(mask,
                                        vmulq_n_f32(weight_exp4_neon(vmulq_f32(dv, half_v)), rob), zero_v);
                                    f_ws = vaddq_f32(f_ws, wv);
                                    f_vs = vfmaq_n_f32(f_vs, wv, v);
                                    if (eta_site) {
                                        const float w0 = vgetq_lane_f32(wv, 0);
                                        frame_ws0 += w0;
                                        frame_w2 += w0 * w0;
                                    }
                                }
#elif defined(__AVX2__)
                                const __m128 dv = _mm_loadu_ps(d_arr);
                                const __m128 mask = _mm_cmp_ps(dv, dlim_v, _CMP_LE_OQ);
                                if (_mm_movemask_ps(mask) != 0) {
                                    const __m128 wv = _mm_and_ps(mask,
                                        _mm_mul_ps(weight_exp4_avx2(_mm_mul_ps(dv, half_v)), _mm_set1_ps(rob)));
                                    f_ws = _mm_add_ps(f_ws, wv);
                                    f_vs = _mm_fmadd_ps(wv, _mm_set1_ps(v), f_vs);
                                    if (eta_site) {
                                        const float w0 = _mm_cvtss_f32(wv);
                                        frame_ws0 += w0;
                                        frame_w2 += w0 * w0;
                                    }
                                }
#else
                                if (std::min({d_arr[0], d_arr[1], d_arr[2], d_arr[3]}) <= 6.5f) {
                                    for (int p = 0; p < 4; ++p) {
                                        const float w = (d_arr[p] <= 6.5f) ? rob * weight_exp(0.5f * d_arr[p]) : 0.0f;
                                        frame_ws[p] += w;
                                        frame_vs[p] += w * v;
                                        if (eta_site && p == 0) frame_w2 += w * w;
                                    }
                                }
#endif
                            }
                        };

                        if (is_bayer) {
                            if (rb >= 1 && rb + 2 < H && cb >= 1 && cb + 2 < W) {
#if defined(__AVX2__)
                                if (!j.eta_only) {
                                    __m256 f_ws8 = _mm256_setzero_ps(), f_vs8 = _mm256_setzero_ps();
                                    for (int rr = rb - 1; rr <= rb + 2; ++rr) {
                                        const float yy0 = float(rr) - yt, yy1 = yy0 - 0.5f;
                                        const float* r_ptr = f_ptr + size_t(rr) * W;
                                        int c0 = cb - 1;
                                        if ((c0 & 1) != g_col_par[rr & 1]) ++c0;
                                        const float vA = r_ptr[c0], vB = r_ptr[c0 + 2];
                                        float robA = mw, robB = mw;
                                        if (use_rob) {
                                            const float adA = std::fabs(vA - anchor);
                                            if (adA > threshold) { const float q = threshold / adA; robA *= q * q * q; }
                                            const float adB = std::fabs(vB - anchor);
                                            if (adB > threshold) { const float q = threshold / adB; robB *= q * q * q; }
                                        }
                                        const float xxA0 = float(c0) - xt, xxA1 = xxA0 - 0.5f;
                                        const float xxB0 = xxA0 + 2.0f,    xxB1 = xxB0 - 0.5f;
                                        const __m256 xx_v = _mm256_setr_ps(xxA0, xxA1, xxA0, xxA1, xxB0, xxB1, xxB0, xxB1);
                                        const __m256 yy_v = _mm256_setr_ps(yy0,  yy0,  yy1,  yy1,  yy0,  yy0,  yy1,  yy1);
                                        const __m256 dv8 = _mm256_fmadd_ps(axx8_v, _mm256_mul_ps(xx_v, xx_v),
                                                           _mm256_fmadd_ps(axy2_8_v, _mm256_mul_ps(xx_v, yy_v),
                                                                           _mm256_mul_ps(ayy8_v, _mm256_mul_ps(yy_v, yy_v))));
                                        const __m256 mask8 = _mm256_cmp_ps(dv8, dlim8_v, _CMP_LE_OQ);
                                        const int m8 = _mm256_movemask_ps(mask8);
                                        if (m8 != 0) {
                                            const __m256 rob8 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(robA)), _mm_set1_ps(robB), 1);
                                            const __m256 val8 = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(vA)), _mm_set1_ps(vB), 1);
                                            const __m256 wv8 = _mm256_and_ps(mask8,
                                                _mm256_mul_ps(weight_exp8_avx2(_mm256_mul_ps(dv8, half8_v)), rob8));
                                            f_ws8 = _mm256_add_ps(f_ws8, wv8);
                                            f_vs8 = _mm256_fmadd_ps(wv8, val8, f_vs8);
                                            if (eta_site) {
                                                if (m8 & 1) {
                                                    const float w0 = _mm256_cvtss_f32(wv8);
                                                    frame_ws0 += w0; frame_w2 += w0 * w0;
                                                }
                                                if (m8 & 16) {
                                                    const float w4 = _mm_cvtss_f32(_mm256_extractf128_ps(wv8, 1));
                                                    frame_ws0 += w4; frame_w2 += w4 * w4;
                                                }
                                            }
                                        }
                                    }
                                    f_ws = _mm_add_ps(_mm256_castps256_ps128(f_ws8), _mm256_extractf128_ps(f_ws8, 1));
                                    f_vs = _mm_add_ps(_mm256_castps256_ps128(f_vs8), _mm256_extractf128_ps(f_vs8, 1));
                                } else
#endif
                                {
                                    for (int rr = rb - 1; rr <= rb + 2; ++rr) {
                                        const float yy0 = float(rr) - yt;
                                        const float* r_ptr = f_ptr + size_t(rr) * W;
                                        int c0 = cb - 1;
                                        if ((c0 & 1) != g_col_par[rr & 1]) ++c0;
                                        accum_green_sample(r_ptr[c0],     yy0, float(c0)     - xt);
                                        accum_green_sample(r_ptr[c0 + 2], yy0, float(c0 + 2) - xt);
                                    }
                                }
                            } else {
                                const int c_hi = std::min(W - 1, cb + 2);
                                for (int rr = rb - 1; rr <= rb + 2; ++rr) {
                                    const int r = clamp_i(rr, 0, H - 1);
                                    const float yy0 = float(rr) - yt;
                                    const float* r_ptr = f_ptr + size_t(r) * W;
                                    int c_lo = std::max(0, cb - 1);
                                    if ((c_lo & 1) != g_col_par[r & 1]) ++c_lo;
                                    for (int c = c_lo; c <= c_hi; c += 2)
                                        accum_green_sample(r_ptr[c], yy0, float(c) - xt);
                                }
                            }
                        } else {
                            if (rb >= 1 && rb + 2 < H && cb >= 1 && cb + 2 < W) {
                                const auto& lst = xt_g_taps[((rb % 6) + 6) % 6][((cb % 6) + 6) % 6];
                                for (int t = 0; t < lst.count; ++t) {
                                    const int rr = rb + lst.taps[t].dr;
                                    const int cc = cb + lst.taps[t].dc;
                                    accum_green_sample(f_ptr[size_t(rr) * W + cc], float(rr) - yt, float(cc) - xt);
                                }
                            } else {
                                const int c_lo = std::max(0, cb - 1), c_hi = std::min(W - 1, cb + 2);
                                const int cm0 = c_lo % 6;
                                for (int rr = rb - 1; rr <= rb + 2; ++rr) {
                                    const int r = clamp_i(rr, 0, H - 1);
                                    const uint8_t* rcfa = j.cfa6x6[r % 6];
                                    const float yy0 = float(rr) - yt;
                                    const float* r_ptr = f_ptr + size_t(r) * W;
                                    int cm = cm0;
                                    for (int c = c_lo; c <= c_hi; ++c) {
                                        const uint8_t cc = rcfa[cm];
                                        if (++cm == 6) cm = 0;
                                        if (cc == 1) accum_green_sample(r_ptr[c], yy0, float(c) - xt);
                                    }
                                }
                            }
                        }
                        if (j.eta_only) {
                            ws0_eta += frame_ws0; vs0_eta += frame_vs0;
                        } else {
                            if (eta_site) ws0_eta += frame_ws0;
#if defined(__aarch64__)
                            ws_v = vaddq_f32(ws_v, f_ws); vs_v = vaddq_f32(vs_v, f_vs);
#elif defined(__AVX2__)
                            ws_v = _mm_add_ps(ws_v, f_ws); vs_v = _mm_add_ps(vs_v, f_vs);
#else
                            for (int p = 0; p < 4; ++p) { ws[p] += frame_ws[p]; vs[p] += frame_vs[p]; }
#endif
                        }
                        w2 += frame_w2;
                    }
                    (void)vs0_eta;
#if defined(__aarch64__) || defined(__AVX2__)
                    float ws[4], vs[4];
                    if (j.eta_only) {
                        ws[0] = ws0_eta;
                    } else {
#if defined(__aarch64__)
                        vst1q_f32(ws, ws_v); vst1q_f32(vs, vs_v);
#else
                        _mm_storeu_ps(ws, ws_v); _mm_storeu_ps(vs, vs_v);
#endif
                    }
                    const float ws_eta_denom = j.eta_only ? ws0_eta : ws0_eta;
#else
                    if (j.eta_only) ws[0] = ws0_eta;
                    const float ws_eta_denom = j.eta_only ? ws0_eta : ws[0];
#endif
                    if (eta_site) {
                        const float eta = ws_eta_denom > 1e-8f ? w2 / (ws_eta_denom * ws_eta_denom) : -1.0f;
                        if (j.eta_quarter_out) {
                            const int width = ((j.full_W1 > 0 ? j.full_W1 : j.W1) + 3) / 4;
                            j.eta_quarter_out[size_t((j.origin_y + Y) / 4) * width + (j.origin_x + X) / 4] = eta;
                        }
                        if (eta >= 0.0f) { eta_sum[tid] += eta; ++eta_count[tid]; }
                    }
                    if (j.eta_only) continue;
                    float g[4];
                    for (int p = 0; p < 4; ++p) g[p] = ws[p] > 1e-8f ? vs[p] / ws[p] : anchor;
                    const float avg = 0.25f * (ws[0] + ws[1] + ws[2] + ws[3]);
                    const float phase = clamp_f((0.72f - std::min(ws[0], ws[3]) /
                        std::max(std::max(ws[1], ws[2]), 1e-6f)) / 0.28f, 0.0f, 0.48f);
                    const float alpha = std::max(clamp_f((2.3f - avg) / 1.35f, 0.0f, 0.88f), phase);
                    if (alpha > 1e-3f) {
                        const int xp = std::min(X + 1, W - 1), yp = std::min(Y + 1, H - 1);
                        const float a = j.g0_ha_1x[size_t(Y) * W + X], b = j.g0_ha_1x[size_t(Y) * W + xp];
                        const float c = j.g0_ha_1x[size_t(yp) * W + X], d = j.g0_ha_1x[size_t(yp) * W + xp];
                        const float nw = 1.0f / (std::fabs(a - d) + 0.002f), ne = 1.0f / (std::fabs(b - c) + 0.002f);
                        const float ha[4] = {a, 0.5f * (a + b), 0.5f * (a + c),
                            (nw * 0.5f * (a + d) + ne * 0.5f * (b + c)) / (nw + ne)};
                        for (int p = 0; p < 4; ++p) g[p] += alpha * (ha[p] - g[p]);
                    }
                    float* g_r0 = green + size_t(Y * 2) * W2 + X * 2;
                    float* g_r1 = g_r0 + W2;
                    g_r0[0] = g[0]; g_r0[1] = g[1];
                    g_r1[0] = g[2]; g_r1[1] = g[3];
                }
            }
        }
    }, 16);

    j.out_eta_sum = 0; j.out_eta_count = 0;
    for (int t = 0; t < nt; ++t) {
        j.out_eta_sum += eta_sum[t]; j.out_eta_count += eta_count[t];
    }
    j.out_eta_eff = j.global_eta >= 0 ? j.global_eta :
        std::max(0.01, j.out_eta_sum / double(std::max<int64_t>(1, j.out_eta_count)));
    j.out_device_label = "CPU SIMD tiled";
    cost_mark(1);
    if (j.eta_only) return true;

    sc.red.resize(size_t(H) * W);
    sc.blue.resize(size_t(H) * W);
    sc.guide.resize(size_t(H) * W);
    sc.anc_r.resize(size_t(Hh) * Wh);
    sc.anc_b.resize(size_t(Hh) * Wh);
    float* red = sc.red.data();
    float* blue = sc.blue.data();
    float* guide = sc.guide.data();
    float* anc_r = sc.anc_r.data();
    float* anc_b = sc.anc_b.data();

    // Precompute sparse normalized [-3..3]x[-3..3] chroma_anchor tap tables per (y%6, x%6, ch in {R,B})
    AnchorTapList anc_taps[2][6][6];
    for (int b = 0; b < 2; ++b) {
        const uint8_t target = b ? 2 : 0;
        for (int ym = 0; ym < 6; ++ym) {
            for (int xm = 0; xm < 6; ++xm) {
                auto& lst = anc_taps[b][ym][xm];
                float wsum = 0.0f;
                for (int dy = -3; dy <= 3; ++dy) {
                    const uint8_t* rcfa = j.cfa6x6[(ym + dy + 6) % 6];
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (rcfa[(xm + dx + 6) % 6] == target) {
                            const float w = 1.0f / (0.25f + float(dy * dy + dx * dx));
                            lst.dy[lst.count] = int8_t(dy);
                            lst.dx[lst.count] = int8_t(dx);
                            lst.nw[lst.count] = w;
                            ++lst.count;
                            wsum += w;
                        }
                    }
                }
                const float inv_w = 1.0f / std::max(wsum, 1e-12f);
                for (int k = 0; k < lst.count; ++k) lst.nw[k] *= inv_w;
            }
        }
    }

    parallel_for_rows(Hh, nt, [&](int r0, int r1, int) {
        for (int yi = r0; yi < r1; ++yi) {
            const int ay0 = clamp_i(2 * yi, 0, H - 1);
            const int ay1 = clamp_i(2 * yi + 1, 0, H - 1);
            float* ar = anc_r + size_t(yi) * Wh;
            float* ab = anc_b + size_t(yi) * Wh;
            const bool y_in = (ay0 >= 3 && ay1 + 3 < H);
            for (int xi = 0; xi < Wh; ++xi) {
                const int ax0 = clamp_i(2 * xi, 0, W - 1);
                const int ax1 = clamp_i(2 * xi + 1, 0, W - 1);
                if (y_in && ax0 >= 3 && ax1 + 3 < W) {
                    const auto& lr = anc_taps[0][ay0 % 6][ax0 % 6];
                    float sr = 0.0f;
                    const float* base_r = j.c_anc_1x + size_t(ay0) * W + ax0;
                    for (int k = 0; k < lr.count; ++k)
                        sr += lr.nw[k] * base_r[ptrdiff_t(lr.dy[k]) * W + lr.dx[k]];
                    ar[xi] = sr;

                    const auto& lb = anc_taps[1][ay1 % 6][ax1 % 6];
                    float sb = 0.0f;
                    const float* base_b = j.c_anc_1x + size_t(ay1) * W + ax1;
                    for (int k = 0; k < lb.count; ++k)
                        sb += lb.nw[k] * base_b[ptrdiff_t(lb.dy[k]) * W + lb.dx[k]];
                    ab[xi] = sb;
                } else {
                    ar[xi] = chroma_anchor(j, ay0, ax0, 0);
                    ab[xi] = chroma_anchor(j, ay1, ax1, 2);
                }
            }
        }
    });

    // Precompute sparse X-Trans 6x6 R/B tap lists (exact 8 taps out of 36 per 6x6 cell!)
    struct RBTapList { int count = 0; int8_t ky[12]{}; int8_t kx[12]{}; };
    RBTapList xt_rb_taps[2][6][6];
    if (j.is_xtrans) {
        for (int b = 0; b < 2; ++b) {
            const uint8_t target = b ? 2 : 0;
            for (int ym = 0; ym < 6; ++ym) {
                for (int xm = 0; xm < 6; ++xm) {
                    auto& lst = xt_rb_taps[b][ym][xm];
                    for (int ky = 0; ky < 6; ++ky) {
                        const uint8_t* rcfa = j.cfa6x6[(ym + ky) % 6];
                        for (int kx = 0; kx < 6; ++kx) {
                            if (rcfa[(xm + kx) % 6] == target && lst.count < 12) {
                                lst.ky[lst.count] = int8_t(ky);
                                lst.kx[lst.count] = int8_t(kx);
                                ++lst.count;
                            }
                        }
                    }
                }
            }
        }
    }

    // Precompute half-grid bilinear coordinates for Stage 4 R (b=0) and B (b=1)
    std::vector<HalfAxis> cy_axis[2], cx_axis[2];
    if (h_ge2) {
        for (int b = 0; b < 2; ++b) {
            cy_axis[b].resize(H);
            cx_axis[b].resize(W);
            for (int Y = 0; Y < H; ++Y) {
                const float yc = clamp_f(0.25f * float(2 * Y + b), 0.0f, float(Hh - 1));
                const int y0 = clamp_i(int(yc), 0, Hh - 2);
                const float fy_v = clamp_f(yc - float(y0), 0.0f, 1.0f);
                cy_axis[b][Y] = {y0, 1.0f - fy_v, fy_v};
            }
            for (int X = 0; X < W; ++X) {
                const float xc = clamp_f(0.25f * float(2 * X + b), 0.0f, float(Wh - 1));
                const int x0 = clamp_i(int(xc), 0, Wh - 2);
                const float fx_v = clamp_f(xc - float(x0), 0.0f, 1.0f);
                cx_axis[b][X] = {x0, 1.0f - fx_v, fx_v};
            }
        }
    }

    const float opt_cy = 0.5f * j.full_H1, opt_cx = 0.5f * j.full_W1;
    const float inv_opt_r = 1.0f / std::max(1.0f, std::max(opt_cy, opt_cx));
    const float hl_lim = j.glint_desat ? (j.is_astro ? 0.88f : 0.85f) : 1.0e6f;
    const bool bayer_rb_direct[2] = {is_bayer && cfa2x2[0][0] == 0, is_bayer && cfa2x2[1][1] == 2};

    auto sample_green_2x = [&](float gy_s, float gx_s) -> float {
        if (gy_s >= 0.0f && gy_s < float(H2 - 1) && gx_s >= 0.0f && gx_s < float(W2 - 1)) {
            const int y0 = int(gy_s), x0 = int(gx_s);
            const float fy_v = gy_s - float(y0), fx_v = gx_s - float(x0);
            const float* r0 = green + size_t(y0) * W2 + x0;
            const float* r1 = r0 + W2;
            return (1.0f - fy_v) * ((1.0f - fx_v) * r0[0] + fx_v * r0[1]) +
                   fy_v * ((1.0f - fx_v) * r1[0] + fx_v * r1[1]);
        }
        return bilerp_clamped(green, H2, W2, gy_s, gx_s);
    };

    parallel_for_rows(H, nt, [&](int y0, int y1, int) {
        for (int xb = 0; xb < W; xb += s4_xtile) {
            const int xe = std::min(W, xb + s4_xtile);
            for (int Y = y0; Y < y1; ++Y) {
                const int yi = clamp_i((Y + 1) >> 1, 0, Hh - 1);
                const size_t hrow = size_t(yi) * Wh;
                const HalfAxis hay0 = h_ge2 ? cy_axis[0][Y] : HalfAxis{0, 1.0f, 0.0f};
                const HalfAxis hay1 = h_ge2 ? cy_axis[1][Y] : HalfAxis{0, 1.0f, 0.0f};

                for (int X = xb; X < xe; ++X) {
                    const int xi = clamp_i((X + 1) >> 1, 0, Wh - 1);
                    const size_t hi = hrow + xi;
                    const float axx = 0.6f * cov[hi * 3], axy = 0.6f * cov[hi * 3 + 1], ayy = 0.6f * cov[hi * 3 + 2];
                    const float g_gd = j.g_guide[hi];

                    for (int b = 0; b < 2; ++b) {
                        const int r2 = 2 * Y + b, c2 = 2 * X + b;
                        const float yr = 0.5f * r2, xr = 0.5f * c2;
                        const float anchor = b ? anc_b[hi] : anc_r[hi];
                        const float threshold = 4.0f * std::sqrt(float(j.pg_sig0_sq) + float(j.pg_gain) *
                            std::max(0.0f, std::max(anchor, g_gd))) + 0.01f;
                        const float wb = b ? j.wb_b : j.wb_r;
                        const float anchor_wb = anchor * wb, thr_wb = threshold * wb;
                        const float ry = yr + j.origin_y - opt_cy, rx = xr + j.origin_x - opt_cx;
                        const float rn = (rx * inv_opt_r) * (rx * inv_opt_r) + (ry * inv_opt_r) * (ry * inv_opt_r);
                        const float lca = (b ? j.lca_b_k1 : j.lca_r_k1) + (b ? j.lca_b_k2 : j.lca_r_k2) * rn;
                        const float lca_dy = lca * ry, lca_dx = lca * rx;
                        const uint8_t target_ch = b ? 2 : 0;

                        const HalfAxis& hay = b ? hay1 : hay0;
                        const HalfAxis hax = h_ge2 ? cx_axis[b][X] : HalfAxis{0, 1.0f, 0.0f};
                        const size_t b00 = size_t(hay.i0) * Wh + hax.i0;
                        const size_t b10 = b00 + Wh;
                        auto sample_half_c = [&](const float* p) -> float {
                            if (!h_ge2) return p[0];
                            return hay.f0 * (hax.f0 * p[b00] + hax.f1 * p[b00 + 1]) +
                                   hay.f1 * (hax.f0 * p[b10] + hax.f1 * p[b10 + 1]);
                        };

                        float weight = 0.0f, value = 0.0f;
                        for (int n = 0; n < j.N; ++n) {
                            const float fw = fw_arr[n];
                            if (fw <= 0.0f) continue;
                            const float mw_raw = unit_mw_arr[n] ? 1.0f : sample_half_c(motion[n].data());
                            const float mw = (j.is_virtual_nd ? 1.0f : mw_raw) * fw;
                            if (mw < 0.02f) continue;
                            const float dy = (n ? 2.0f * sample_half_c(fy[n].data()) : 0.0f) + lca_dy + ois_arr[n][b * 2];
                            const float dx = (n ? 2.0f * sample_half_c(fx[n].data()) : 0.0f) + lca_dx + ois_arr[n][b * 2 + 1];
                            const float yt = ((yr + j.origin_y) + dy) - j.origin_y;
                            const float xt = ((xr + j.origin_x) + dx) - j.origin_x;
                            if (yt < 0.0f || yt > H - 1 || xt < 0.0f || xt > W - 1) continue;
                            const float* f_ptr = frames[n].data();
                            const bool use_rob = rob_arr[n] != 0;
                            float frame_weight = 0.0f, frame_value = 0.0f;

                            if (!j.is_xtrans) {
                                const int qy = int(std::floor((yt - b) * 0.5f)), qx = int(std::floor((xt - b) * 0.5f));
                                const int rr0 = 2 * qy + b, rr1 = rr0 + 2;
                                const int cc0 = 2 * qx + b, cc1 = cc0 + 2;
                                if (bayer_rb_direct[b] && rr0 >= b && rr1 <= H - 2 + b && rr1 < H &&
                                    cc0 >= b && cc1 <= W - 2 + b && cc1 < W) {
                                    const float yy0 = float(rr0) - yt, yy1 = float(rr1) - yt;
                                    const float xx0 = float(cc0) - xt, xx1 = float(cc1) - xt;
                                    const float ax0 = axx * xx0 * xx0, ax1 = axx * xx1 * xx1;
                                    const float ay0 = ayy * yy0 * yy0, ay1 = ayy * yy1 * yy1;
                                    const float d4[4] = {
                                        ax0 + 2.0f * axy * xx0 * yy0 + ay0,
                                        ax1 + 2.0f * axy * xx1 * yy0 + ay0,
                                        ax0 + 2.0f * axy * xx0 * yy1 + ay1,
                                        ax1 + 2.0f * axy * xx1 * yy1 + ay1
                                    };
                                    float w_exp4[4];
#if defined(__aarch64__)
                                    vst1q_f32(w_exp4, weight_exp4_neon(vmulq_n_f32(vld1q_f32(d4), 0.5f)));
#elif defined(__AVX2__)
                                    _mm_storeu_ps(w_exp4, weight_exp4_avx2(_mm_mul_ps(_mm_loadu_ps(d4), _mm_set1_ps(0.5f))));
#else
                                    for (int l = 0; l < 4; ++l) w_exp4[l] = (d4[l] <= 7.5f) ? weight_exp(0.5f * d4[l]) : 0.0f;
#endif
                                    const int rrs[2] = {rr0, rr1}, ccs[2] = {cc0, cc1};
                                    const float gy_src0 = 2.0f * ((float(rr0 + j.origin_y) - dy) - j.origin_y);
                                    const float gx_src0 = 2.0f * ((float(cc0 + j.origin_x) - dx) - j.origin_x);
                                    if (gy_src0 >= 0.0f && gy_src0 + 4.0f < float(H2 - 1) &&
                                        gx_src0 >= 0.0f && gx_src0 + 4.0f < float(W2 - 1)) {
                                        const int y0_g = int(gy_src0), x0_g = int(gx_src0);
                                        if (y0_g + 5 < H2 && x0_g + 5 < W2) {
                                            const float fy_g = gy_src0 - float(y0_g), fx_g = gx_src0 - float(x0_g);
                                            const float w00 = (1.0f - fy_g) * (1.0f - fx_g);
                                            const float w01 = (1.0f - fy_g) * fx_g;
                                            const float w10 = fy_g * (1.0f - fx_g);
                                            const float w11 = fy_g * fx_g;
                                            const float* g_base = green + size_t(y0_g) * W2 + x0_g;
                                            for (int ky = 0; ky < 2; ++ky) {
                                                const float* f_row = f_ptr + size_t(rrs[ky]) * W;
                                                const float* g_ky = g_base + size_t(4 * ky) * W2;
                                                for (int kx = 0; kx < 2; ++kx) {
                                                    const int idx4 = ky * 2 + kx;
                                                    if (d4[idx4] > 7.5f) continue;
                                                    const float v = f_row[ccs[kx]] * wb;
                                                    const float* gp = g_ky + 4 * kx;
                                                    const float g = w00 * gp[0] + w01 * gp[1] + w10 * gp[W2] + w11 * gp[W2 + 1];
                                                    float rob = 1.0f;
                                                    if (use_rob) {
                                                        const float ad = std::fabs(v - anchor_wb);
                                                        if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                                    }
                                                    const float w = mw * rob * w_exp4[idx4];
                                                    frame_weight += w; frame_value += w * (v - g);
                                                }
                                            }
                                        } else {
                                            for (int ky = 0; ky < 2; ++ky) {
                                                const int rr = rrs[ky];
                                                const float gy_src = 2.0f * ((float(rr + j.origin_y) - dy) - j.origin_y);
                                                const float* f_row = f_ptr + size_t(rr) * W;
                                                for (int kx = 0; kx < 2; ++kx) {
                                                    const int idx4 = ky * 2 + kx;
                                                    if (d4[idx4] > 7.5f) continue;
                                                    const int cc = ccs[kx];
                                                    const float v = f_row[cc] * wb;
                                                    const float gx_src = 2.0f * ((float(cc + j.origin_x) - dx) - j.origin_x);
                                                    const float g = sample_green_2x(gy_src, gx_src);
                                                    float rob = 1.0f;
                                                    if (use_rob) {
                                                        const float ad = std::fabs(v - anchor_wb);
                                                        if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                                    }
                                                    const float w = mw * rob * w_exp4[idx4];
                                                    frame_weight += w; frame_value += w * (v - g);
                                                }
                                            }
                                        }
                                    } else {
                                        for (int ky = 0; ky < 2; ++ky) {
                                            const int rr = rrs[ky];
                                            const float gy_src = 2.0f * ((float(rr + j.origin_y) - dy) - j.origin_y);
                                            const float* f_row = f_ptr + size_t(rr) * W;
                                            for (int kx = 0; kx < 2; ++kx) {
                                                const int idx4 = ky * 2 + kx;
                                                if (d4[idx4] > 7.5f) continue;
                                                const int cc = ccs[kx];
                                                const float v = f_row[cc] * wb;
                                                const float gx_src = 2.0f * ((float(cc + j.origin_x) - dx) - j.origin_x);
                                                const float g = sample_green_2x(gy_src, gx_src);
                                                float rob = 1.0f;
                                                if (use_rob) {
                                                    const float ad = std::fabs(v - anchor_wb);
                                                    if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                                }
                                                const float w = mw * rob * w_exp4[idx4];
                                                frame_weight += w; frame_value += w * (v - g);
                                            }
                                        }
                                    }
                                } else {
                                    for (int ky = 0; ky < 2; ++ky) {
                                        const int rr = clamp_i(2 * (qy + ky) + b, b, H - 2 + b);
                                        if (rr < 0 || rr >= H) continue;
                                        const float yy = float(rr) - yt;
                                        const float gy_src = 2.0f * ((float(rr + j.origin_y) - dy) - j.origin_y);
                                        for (int kx = 0; kx < 2; ++kx) {
                                            const int cc = clamp_i(2 * (qx + kx) + b, b, W - 2 + b);
                                            if (cc < 0 || cc >= W) continue;
                                            const uint8_t ch_here = is_bayer ? cfa2x2[rr & 1][cc & 1] : j.cfa6x6[rr % 6][cc % 6];
                                            if (ch_here != target_ch) continue;
                                            const float xx = float(cc) - xt;
                                            const float d = axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy;
                                            if (d > 7.5f) continue;
                                            const float v = f_ptr[size_t(rr) * W + cc] * wb;
                                            const float gx_src = 2.0f * ((float(cc + j.origin_x) - dx) - j.origin_x);
                                            const float g = sample_green_2x(gy_src, gx_src);
                                            float rob = 1.0f;
                                            if (use_rob) {
                                                const float ad = std::fabs(v - anchor_wb);
                                                if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                            }
                                            const float w = mw * rob * weight_exp(0.5f * d);
                                            frame_weight += w; frame_value += w * (v - g);
                                        }
                                    }
                                }
                            } else {
                                const int by = int(std::floor(yt)) - 2, bx = int(std::floor(xt)) - 2;
                                if (by >= 0 && by + 5 < H && bx >= 0 && bx + 5 < W) {
                                    const auto& lst = xt_rb_taps[b][by % 6][bx % 6];
                                    float d_buf[12];
                                    for (int t = 0; t < lst.count; ++t) {
                                        const float yy = float(by + lst.ky[t]) - yt;
                                        const float xx = float(bx + lst.kx[t]) - xt;
                                        d_buf[t] = 0.55f * (axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy);
                                    }
                                    for (int t = lst.count; t < 12; ++t) d_buf[t] = 80.0f;
                                    float w_buf[12];
#if defined(__aarch64__)
                                    for (int t = 0; t < lst.count; t += 4)
                                        vst1q_f32(w_buf + t, weight_exp4_neon(vmulq_n_f32(vld1q_f32(d_buf + t), 0.5f)));
#elif defined(__AVX2__)
                                    if (lst.count <= 8) {
                                        _mm256_storeu_ps(w_buf, weight_exp8_avx2(_mm256_mul_ps(_mm256_loadu_ps(d_buf), _mm256_set1_ps(0.5f))));
                                    } else {
                                        for (int t = 0; t < lst.count; t += 4)
                                            _mm_storeu_ps(w_buf + t, weight_exp4_avx2(_mm_mul_ps(_mm_loadu_ps(d_buf + t), _mm_set1_ps(0.5f))));
                                    }
#else
                                    for (int t = 0; t < lst.count; ++t)
                                        w_buf[t] = (d_buf[t] <= 9.5f) ? weight_exp(0.5f * d_buf[t]) : 0.0f;
#endif
                                    for (int t = 0; t < lst.count; ++t) {
                                        if (d_buf[t] > 9.5f) continue;
                                        const int rr = by + lst.ky[t], cc = bx + lst.kx[t];
                                        const float v = f_ptr[size_t(rr) * W + cc] * wb;
                                        const float gy_src = 2.0f * ((float(rr + j.origin_y) - dy) - j.origin_y);
                                        const float gx_src = 2.0f * ((float(cc + j.origin_x) - dx) - j.origin_x);
                                        const float g = sample_green_2x(gy_src, gx_src);
                                        float rob = 1.0f;
                                        if (use_rob) {
                                            const float ad = std::fabs(v - anchor_wb);
                                            if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                        }
                                        const float w = mw * rob * w_buf[t];
                                        frame_weight += w; frame_value += w * (v - g);
                                    }
                                } else {
                                    for (int ky = 0; ky < 6; ++ky) {
                                        const int rr = by + ky;
                                        if (rr < 0 || rr >= H) continue;
                                        const uint8_t* rcfa = j.cfa6x6[rr % 6];
                                        const float yy = float(rr) - yt;
                                        const float gy_src = 2.0f * ((float(rr + j.origin_y) - dy) - j.origin_y);
                                        for (int kx = 0; kx < 6; ++kx) {
                                            const int cc = bx + kx;
                                            if (cc < 0 || cc >= W || rcfa[cc % 6] != target_ch) continue;
                                            const float xx = float(cc) - xt;
                                            const float d = 0.55f * (axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy);
                                            if (d > 9.5f) continue;
                                            const float v = f_ptr[size_t(rr) * W + cc] * wb;
                                            const float gx_src = 2.0f * ((float(cc + j.origin_x) - dx) - j.origin_x);
                                            const float g = sample_green_2x(gy_src, gx_src);
                                            float rob = 1.0f;
                                            if (use_rob) {
                                                const float ad = std::fabs(v - anchor_wb);
                                                if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                            }
                                            const float w = mw * rob * weight_exp(0.5f * d);
                                            frame_weight += w; frame_value += w * (v - g);
                                        }
                                    }
                                }
                            }
                            weight += frame_weight; value += frame_value;
                        }
                        const float g = green[size_t(r2) * W2 + c2];
                        float diff = weight > 1e-8f ? float(value / weight) : anchor_wb - g;
                        const float peak = std::max(g, anchor);
                        if (peak > hl_lim) {
                            const float t = clamp_f((peak - hl_lim) / 0.14f, 0.0f, 1.0f);
                            diff *= (1.0f - t) * (1.0f - t);
                        }
                        (b ? blue : red)[size_t(Y) * W + X] = diff;
                    }
                }
            }
        }
    }, 16);

    const bool untiled_full = !(j.core_y1 > 0 && j.core_x1 > 0);
    if (untiled_full) {
        sc.anc_r.release();
        sc.anc_b.release();
    }

    if (j.on_stage4_done) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        j.on_stage4_done(j.out_eta_eff, seconds, j.out_device_label);
    }
    const double gain = std::max(0.0, j.pg_gain * j.out_eta_eff);
    cost_mark(2);
    const double sig2 = std::max(1e-10, j.pg_sig0_sq * j.out_eta_eff);
    const double ped = 4.0 * std::sqrt(sig2), gat_c = 0.375 * gain * gain + sig2, sqrt_c = std::sqrt(gat_c);
    const float ped_f = float(ped), gain_f = float(gain), gat_c_f = float(gat_c), sqrt_c_f = float(sqrt_c);

    sc.z.resize(size_t(H2) * W2);
    float* z = sc.z.data();
    parallel_for_rows(H2, nt, [&](int y0, int y1, int) {
        size_t i = size_t(y0) * W2;
        const size_t i_end = size_t(y1) * W2;
#if defined(__aarch64__)
        const float32x4_t zero_v = vdupq_n_f32(0.0f), two_v = vdupq_n_f32(2.0f);
        const float32x4_t ped_v = vdupq_n_f32(ped_f), gain_v = vdupq_n_f32(gain_f);
        const float32x4_t gatc_v = vdupq_n_f32(gat_c_f), sqrtc_v = vdupq_n_f32(sqrt_c_f);
        for (; i + 4 <= i_end; i += 4) {
            const float32x4_t v = vmaxq_f32(zero_v, vaddq_f32(vld1q_f32(green + i), ped_v));
            const float32x4_t den_s = vaddq_f32(vsqrtq_f32(vfmaq_f32(gatc_v, gain_v, v)), sqrtc_v);
            vst1q_f32(z + i, vdivq_f32(vmulq_f32(two_v, v), den_s));
        }
#elif defined(__AVX2__)
#if HERAW_AVX512_ENABLED
        if (heraw_cache::use_avx512() && i + 16 <= i_end) {
            i = detail_s45::gat_fwd_span_avx512(green, z, i, i_end, ped_f, gain_f, gat_c_f, sqrt_c_f);
        }
#endif
        const __m256 zero_v = _mm256_setzero_ps(), two_v = _mm256_set1_ps(2.0f);
        const __m256 ped_v = _mm256_set1_ps(ped_f), gain_v = _mm256_set1_ps(gain_f);
        const __m256 gatc_v = _mm256_set1_ps(gat_c_f), sqrtc_v = _mm256_set1_ps(sqrt_c_f);
        for (; i + 8 <= i_end; i += 8) {
            const __m256 v = _mm256_max_ps(zero_v, _mm256_add_ps(_mm256_loadu_ps(green + i), ped_v));
            const __m256 den_s = _mm256_add_ps(_mm256_sqrt_ps(_mm256_fmadd_ps(gain_v, v, gatc_v)), sqrtc_v);
            _mm256_storeu_ps(z + i, _mm256_div_ps(_mm256_mul_ps(two_v, v), den_s));
        }
#endif
        for (; i < i_end; ++i) {
            const float v = std::max(0.0f, green[i] + ped_f);
            z[i] = 2.0f * v / (std::sqrt(gain_f * v + gat_c_f) + sqrt_c_f);
        }
    });

    const float* den = z;
    if (j.denoise_strength > 1e-4f) {
        sc.den.resize(size_t(H2) * W2);
        multiscale_dct_denoise_gat(sc.den.data(), H2, W2, nt, j.denoise_strength, j.edge_polish,
                                   j.snr_boost_half, Hh, Wh, &j, z, &sc.dct_pilot);
        den = sc.den.data();
    }

    sc.variance.resize(size_t(Hh) * Wh);
    float* variance = sc.variance.data();
    parallel_for_rows(Hh, nt, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y) {
            float* __restrict vrow = variance + size_t(y) * Wh;
            for (int x = 0; x < Wh; ++x) {
                const size_t base = size_t(y * 4) * W2 + size_t(x * 4);
#if defined(__AVX2__)
                const __m128 d0 = _mm_sub_ps(_mm_loadu_ps(z + base), _mm_loadu_ps(den + base));
                const __m128 d1 = _mm_sub_ps(_mm_loadu_ps(z + base + W2), _mm_loadu_ps(den + base + W2));
                const __m128 d2 = _mm_sub_ps(_mm_loadu_ps(z + base + 2 * W2), _mm_loadu_ps(den + base + 2 * W2));
                const __m128 d3 = _mm_sub_ps(_mm_loadu_ps(z + base + 3 * W2), _mm_loadu_ps(den + base + 3 * W2));
                const __m128 s01 = _mm_fmadd_ps(d1, d1, _mm_mul_ps(d0, d0));
                const __m128 s23 = _mm_fmadd_ps(d3, d3, _mm_mul_ps(d2, d2));
                __m128 s = _mm_add_ps(s01, s23);
                s = _mm_hadd_ps(s, s);
                s = _mm_hadd_ps(s, s);
                vrow[x] = std::min(_mm_cvtss_f32(s) * 0.0625f, 1.25f);
#elif defined(__aarch64__)
                const float32x4_t d0 = vsubq_f32(vld1q_f32(z + base), vld1q_f32(den + base));
                const float32x4_t d1 = vsubq_f32(vld1q_f32(z + base + W2), vld1q_f32(den + base + W2));
                const float32x4_t d2 = vsubq_f32(vld1q_f32(z + base + 2 * W2), vld1q_f32(den + base + 2 * W2));
                const float32x4_t d3 = vsubq_f32(vld1q_f32(z + base + 3 * W2), vld1q_f32(den + base + 3 * W2));
                const float32x4_t s = vaddq_f32(vfmaq_f32(vmulq_f32(d0, d0), d1, d1),
                                                vfmaq_f32(vmulq_f32(d2, d2), d3, d3));
                vrow[x] = std::min(vaddvq_f32(s) * 0.0625f, 1.25f);
#else
                float sum = 0.0f;
                for (int dy = 0; dy < 4; ++dy) {
                    const size_t roff = base + size_t(dy) * W2;
                    for (int dx = 0; dx < 4; ++dx) {
                        const float diff = z[roff + dx] - den[roff + dx];
                        sum += diff * diff;
                    }
                }
                vrow[x] = std::min(sum * 0.0625f, 1.25f);
#endif
            }
        }
    });
    sep_gaussian_2d(variance, variance, Hh, Wh, 8.0f, nt);
    parallel_for_rows(H2, nt, [&](int y0, int y1, int) {
        const float q_gain = 0.25f * gain_f;
        for (int y = y0; y < y1; ++y) {
            const float* __restrict zm = z + size_t(clamp_i(y - 1, 0, H2 - 1)) * W2;
            const float* __restrict z0 = z + size_t(y) * W2;
            const float* __restrict zp = z + size_t(clamp_i(y + 1, 0, H2 - 1)) * W2;
            const float* __restrict dn = den + size_t(y) * W2;
            const float* __restrict vr = variance + size_t(y >> 2) * Wh;
            float* __restrict gr = green + size_t(y) * W2;

            auto eval_inv = [&](int x, int xm, int xp) {
                const float lo = std::min({zm[xm], zm[x], zm[xp], z0[xm], z0[x], z0[xp], zp[xm], zp[x], zp[xp]});
                const float hi = std::max({zm[xm], zm[x], zm[xp], z0[xm], z0[x], z0[xp], zp[xm], zp[x], zp[xp]});
                const float v = clamp_f(dn[x], lo, hi);
                const float dv = vr[x >> 2];
                gr[x] = sqrt_c_f * v + q_gain * (v * v + dv) - ped_f;
            };
            eval_inv(0, 0, std::min(1, W2 - 1));
            int x = 1;
#if defined(__aarch64__)
            const float32x4_t sc_v = vdupq_n_f32(sqrt_c_f), qg_v = vdupq_n_f32(q_gain), ped_v = vdupq_n_f32(ped_f);
            for (; x < W2 - 1 && (x & 3); ++x) eval_inv(x, x - 1, x + 1);
            for (; x + 4 <= W2 - 1; x += 4) {
                const float32x4_t m_l = vld1q_f32(zm + x - 1), m_c = vld1q_f32(zm + x), m_r = vld1q_f32(zm + x + 1);
                const float32x4_t c_l = vld1q_f32(z0 + x - 1), c_c = vld1q_f32(z0 + x), c_r = vld1q_f32(z0 + x + 1);
                const float32x4_t p_l = vld1q_f32(zp + x - 1), p_c = vld1q_f32(zp + x), p_r = vld1q_f32(zp + x + 1);
                const float32x4_t lo = vminq_f32(vminq_f32(vminq_f32(m_l, m_c), vminq_f32(m_r, c_l)),
                                                 vminq_f32(vminq_f32(c_c, c_r), vminq_f32(p_l, vminq_f32(p_c, p_r))));
                const float32x4_t hi = vmaxq_f32(vmaxq_f32(vmaxq_f32(m_l, m_c), vmaxq_f32(m_r, c_l)),
                                                 vmaxq_f32(vmaxq_f32(c_c, c_r), vmaxq_f32(p_l, vmaxq_f32(p_c, p_r))));
                const float32x4_t v = vminq_f32(hi, vmaxq_f32(lo, vld1q_f32(dn + x)));
                const float32x4_t dv = vdupq_n_f32(vr[x >> 2]);
                vst1q_f32(gr + x, vsubq_f32(vfmaq_f32(vmulq_f32(sc_v, v), qg_v, vfmaq_f32(dv, v, v)), ped_v));
            }
#elif defined(__AVX2__)
            for (; x < W2 - 1 && (x & 3); ++x) eval_inv(x, x - 1, x + 1);
#if HERAW_AVX512_ENABLED
            if (heraw_cache::use_avx512() && x + 16 <= W2 - 1) {
                x = detail_s45::gat_inv_row_avx512(zm, z0, zp, dn, vr, gr, x, W2, sqrt_c_f, q_gain, ped_f);
            }
#endif
            const __m256 sc_v = _mm256_set1_ps(sqrt_c_f), qg_v = _mm256_set1_ps(q_gain), ped_v = _mm256_set1_ps(ped_f);
            for (; x + 8 <= W2 - 1; x += 8) {
                const __m256 m_l = _mm256_loadu_ps(zm + x - 1), m_c = _mm256_loadu_ps(zm + x), m_r = _mm256_loadu_ps(zm + x + 1);
                const __m256 c_l = _mm256_loadu_ps(z0 + x - 1), c_c = _mm256_loadu_ps(z0 + x), c_r = _mm256_loadu_ps(z0 + x + 1);
                const __m256 p_l = _mm256_loadu_ps(zp + x - 1), p_c = _mm256_loadu_ps(zp + x), p_r = _mm256_loadu_ps(zp + x + 1);
                const __m256 lo = _mm256_min_ps(_mm256_min_ps(_mm256_min_ps(m_l, m_c), _mm256_min_ps(m_r, c_l)),
                                                _mm256_min_ps(_mm256_min_ps(c_c, c_r), _mm256_min_ps(p_l, _mm256_min_ps(p_c, p_r))));
                const __m256 hi = _mm256_max_ps(_mm256_max_ps(_mm256_max_ps(m_l, m_c), _mm256_max_ps(m_r, c_l)),
                                                _mm256_max_ps(_mm256_max_ps(c_c, c_r), _mm256_max_ps(p_l, _mm256_max_ps(p_c, p_r))));
                const __m256 v = _mm256_min_ps(hi, _mm256_max_ps(lo, _mm256_loadu_ps(dn + x)));
                const __m128 dv0 = _mm_set1_ps(vr[x >> 2]), dv1 = _mm_set1_ps(vr[(x + 4) >> 2]);
                const __m256 dv = _mm256_insertf128_ps(_mm256_castps128_ps256(dv0), dv1, 1);
                _mm256_storeu_ps(gr + x, _mm256_sub_ps(_mm256_fmadd_ps(qg_v, _mm256_fmadd_ps(v, v, dv), _mm256_mul_ps(sc_v, v)), ped_v));
            }
#endif
            for (; x < W2 - 1; ++x) eval_inv(x, x - 1, x + 1);
            if (W2 > 1) eval_inv(W2 - 1, W2 - 2, W2 - 1);
        }
    });
    if (untiled_full) {
        if (j.psf_iters > 0) {
            std::swap(sc.rl_ws.lum0, sc.z);
        }
        sc.z.release();
        sc.den.release();
        sc.dct_pilot.release();
        sc.variance.release();
    }

    parallel_for_rows(H, nt, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y) {
            const float* g0 = green + size_t(2 * y) * W2;
            const float* g1 = g0 + W2;
            float* gd = guide + size_t(y) * W;
            for (int x = 0; x < W; ++x)
                gd[x] = 0.5f * (g0[2 * x + 1] + g1[2 * x]);
        }
    });
    double mean = j.global_mean_g;
    cost_mark(3);
    if (mean < 0) {
        mean = 0;
        for (size_t i = 0; i < size_t(H) * W; i += 16) mean += std::max(0.0f, guide[i]);
        mean /= double((size_t(H) * W + 15) / 16);
    }
    if (j.chroma_denoise > 1e-4f) {
        sc.mr.resize(size_t(H) * W);
        sc.mb.resize(size_t(H) * W);
        median3x3_plane(red, sc.mr.data(), H, W, nt);
        median3x3_plane(blue, sc.mb.data(), H, W, nt);
        if (j.is_astro) {
            std::swap(sc.red, sc.mr);
            std::swap(sc.blue, sc.mb);
            red = sc.red.data(); blue = sc.blue.data();
        } else {
            median3x3_plane(sc.mr.data(), red, H, W, nt);
            median3x3_plane(sc.mb.data(), blue, H, W, nt);
        }
        if (j.glint_desat) {
            float* bg = sc.mr.data();
            sep_gaussian_2d(guide, bg, H, W, 2.5f, nt);
            parallel_for_rows(H, nt, [&](int y0, int y1, int) {
                for (size_t i = size_t(y0) * W; i < size_t(y1) * W; ++i) {
                    const float spike_g = std::max(0.0f, guide[i] - bg[i]);
                    const float excess = spike_g > 0.015f ? (spike_g + 0.35f * std::max(0.0f, std::max(red[i], blue[i]))) : 0.0f;
                    const float peak = std::max(guide[i], std::max(guide[i] + red[i], guide[i] + blue[i]));
                    const float ds = std::max(clamp_f((excess - 0.045f) / 0.12f, 0.0f, 0.92f),
                                              clamp_f((peak - 0.94f) / 0.10f, 0.0f, 0.85f));
                    red[i] *= 1.0f - ds; blue[i] *= 1.0f - ds;
                }
            });
        }
    }
    const float wbmax = std::max(j.wb_r, j.wb_b);
    const float sigma = j.chroma_denoise * float(std::sqrt((1.0 + wbmax * wbmax) * j.out_eta_eff * (j.pg_sig0_sq + j.pg_gain * mean)));
    if (j.chroma_denoise > 1e-4f) {
        atrous_global(red, blue, guide, j, sigma, sc.mr, sc.mb);
    } else {
        atrous_global(red, blue, guide, j, sigma, sc.tr, sc.tb);
    }

    const float inv_wbr = 1.0f / std::max(1e-4f, j.wb_r);
    const float inv_wbb = 1.0f / std::max(1e-4f, j.wb_b);
    auto assemble_region = [&](float* dst_base, int dst_stride_px, int ry0_2, int ry1_2, int rx0_2, int rx1_2) {
        const int rows_2 = ry1_2 - ry0_2;
        parallel_for_rows(rows_2, nt, [&](int dr0, int dr1, int) {
            for (int dr = dr0; dr < dr1; ++dr) {
                const int y = ry0_2 + dr;
                const float* __restrict gr = green + size_t(y) * W2;
                float* __restrict dst = dst_base + size_t(dr) * dst_stride_px * 3;
                if (H >= 2 && W >= 2 && (rx0_2 & 1) == 0 && rx1_2 >= rx0_2 + 4) {
                    // Exact 2x2 dyadic upsampling: red sampled at (0.5*y, 0.5*x), blue at (0.5*(y-1), 0.5*(x-1)).
                    const int yr0 = clamp_i(y >> 1, 0, H - 1);
                    const int yr1 = clamp_i((y >> 1) + (y & 1), 0, H - 1);
                    const int yb0 = clamp_i((y - 1) >> 1, 0, H - 1);
                    const int yb1 = clamp_i(((y - 1) >> 1) + ((y - 1) & 1), 0, H - 1);
                    const float* __restrict rr0 = red + size_t(yr0) * W;
                    const float* __restrict rr1 = red + size_t(yr1) * W;
                    const float* __restrict br0 = blue + size_t(yb0) * W;
                    const float* __restrict br1 = blue + size_t(yb1) * W;
                    int x = rx0_2;
                    // Leftmost pair (x == 0 has x-1 = -1 clamped to 0 for blue)
                    if (x == 0) {
                        for (int s = 0; s < 2 && x < rx1_2; ++s, ++x) {
                            const float gv = gr[x];
                            const float rv = (s == 0)
                                ? 0.5f * (rr0[0] + rr1[0])
                                : 0.25f * ((rr0[0] + rr0[1]) + (rr1[0] + rr1[1]));
                            const float bv = 0.5f * (br0[0] + br1[0]);
                            const int dx = x - rx0_2;
                            dst[dx * 3 + 0] = std::max(0.0f, (gv + rv) * inv_wbr);
                            dst[dx * 3 + 1] = std::max(0.0f, gv);
                            dst[dx * 3 + 2] = std::max(0.0f, (gv + bv) * inv_wbb);
                        }
                    }
                    const int x_pair_end = std::min(rx1_2 - 1, (W - 1) * 2);
                    for (; x < x_pair_end; x += 2) {
                        const int hx = x >> 1;
                        const float r_e = 0.5f * (rr0[hx] + rr1[hx]);
                        const float r_n = 0.5f * (rr0[hx + 1] + rr1[hx + 1]);
                        const float r_o = 0.5f * (r_e + r_n);
                        const float b_p = 0.5f * (br0[hx - 1] + br1[hx - 1]);
                        const float b_o = 0.5f * (br0[hx] + br1[hx]);
                        const float b_e = 0.5f * (b_p + b_o);

                        const float g0 = gr[x], g1 = gr[x + 1];
                        float* d = dst + (x - rx0_2) * 3;
                        d[0] = std::max(0.0f, (g0 + r_e) * inv_wbr);
                        d[1] = std::max(0.0f, g0);
                        d[2] = std::max(0.0f, (g0 + b_e) * inv_wbb);
                        d[3] = std::max(0.0f, (g1 + r_o) * inv_wbr);
                        d[4] = std::max(0.0f, g1);
                        d[5] = std::max(0.0f, (g1 + b_o) * inv_wbb);
                    }
                    for (; x < rx1_2; ++x) {
                        const float gv = gr[x];
                        const float rv = bilerp_clamped(red, H, W, 0.5f * y, 0.5f * x);
                        const float bv = bilerp_clamped(blue, H, W, 0.5f * (y - 1), 0.5f * (x - 1));
                        const int dx = x - rx0_2;
                        dst[dx * 3 + 0] = std::max(0.0f, (gv + rv) * inv_wbr);
                        dst[dx * 3 + 1] = std::max(0.0f, gv);
                        dst[dx * 3 + 2] = std::max(0.0f, (gv + bv) * inv_wbb);
                    }
                    continue;
                }
                for (int x = rx0_2; x < rx1_2; ++x) {
                    const float gv = gr[x];
                    const float rv = bilerp_clamped(red, H, W, 0.5f * y, 0.5f * x);
                    const float bv = bilerp_clamped(blue, H, W, 0.5f * (y - 1), 0.5f * (x - 1));
                    const int dx = x - rx0_2;
                    dst[dx * 3 + 0] = std::max(0.0f, (gv + rv) * inv_wbr);
                    dst[dx * 3 + 1] = std::max(0.0f, gv);
                    dst[dx * 3 + 2] = std::max(0.0f, (gv + bv) * inv_wbb);
                }
            }
        });
    };

    j.out_resid_noise_2x = 0.35f * float(std::sqrt(sig2 + gain * mean));
    const bool direct_out = (j.direct_stitch_rgb2x != nullptr);
    const int c_rows2 = (cy1 - j.core_y0) * 2;
    const int c_cols2 = (cx1 - j.core_x0) * 2;

    if (j.psf_iters <= 0) {
        if (direct_out) {
            float* dst = j.direct_stitch_rgb2x + (size_t(j.direct_y2) * j.direct_w2_stride + j.direct_x2) * 3;
            assemble_region(dst, j.direct_w2_stride, j.core_y0 * 2, cy1 * 2, j.core_x0 * 2, cx1 * 2);
        } else {
            assemble_region(j.rgb_2x_out, W2, 0, H2, 0, W2);
        }
        cost_mark(4);
        cost_mark(5);
        return true;
    }

    if (!direct_out) {
        assemble_region(j.rgb_2x_out, W2, 0, H2, 0, W2);
    }
    cost_mark(4);

    sc.rl_gain.resize(size_t(Hh) * Wh);
    sc.rl_thr.resize(size_t(Hh) * Wh);
    float* rl_gain = sc.rl_gain.data();
    float* rl_thr = sc.rl_thr.data();
    parallel_for_rows(Hh, nt, [&](int r0, int r1, int) {
        for (size_t i = size_t(r0) * Wh; i < size_t(r1) * Wh; ++i) {
            const uint32_t pk = j.rl_mod_f16[i];
            rl_gain[i] = unpack_half(uint16_t(pk));
            rl_thr[i] = unpack_half(uint16_t(pk >> 16));
        }
    });

    if (j.core_y1 > 0 && j.core_x1 > 0) {
        const int halo = j.psf_iters * heraw_gpu::make_olpf_psf_pc(1, 1, 0, j.psf_sigma, j.olpf_mode).radius + 8;
        const int y0 = std::max(0, (j.core_y0 - halo) / 2 * 2), x0 = std::max(0, (j.core_x0 - halo) / 2 * 2);
        const int y1 = std::min(H, (j.core_y1 + halo + 1) / 2 * 2), x1 = std::min(W, (j.core_x1 + halo + 1) / 2 * 2);
        const int h = y1 - y0, w = x1 - x0;
        sc.patch.resize(size_t(h * 2) * (w * 2) * 3);
        if (direct_out) {
            assemble_region(sc.patch.data(), w * 2, y0 * 2, y1 * 2, x0 * 2, x1 * 2);
        } else {
            for (int r = 0; r < h * 2; ++r)
                std::copy_n(j.rgb_2x_out + (size_t(y0 * 2 + r) * W2 + x0 * 2) * 3,
                            size_t(w * 2) * 3, sc.patch.data() + size_t(r) * (w * 2) * 3);
        }
        sc.gain_p.resize(size_t(h / 2) * (w / 2));
        sc.thr_p.resize(size_t(h / 2) * (w / 2));
        for (int r = 0; r < h / 2; ++r) {
            std::copy_n(rl_gain + size_t(y0 / 2 + r) * Wh + x0 / 2, size_t(w / 2), sc.gain_p.data() + size_t(r) * (w / 2));
            std::copy_n(rl_thr  + size_t(y0 / 2 + r) * Wh + x0 / 2, size_t(w / 2), sc.thr_p.data()  + size_t(r) * (w / 2));
        }
        snr_gated_rl_deconv(sc.patch.data(), h * 2, w * 2, j.psf_iters, j.psf_sigma, 0.18f, j.out_resid_noise_2x, nt,
            sc.rl_ws, sc.gain_p.data(), sc.thr_p.data(), h / 2, w / 2, j.is_astro, j.olpf_mode);
        if (direct_out) {
            parallel_for_rows(c_rows2, nt, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    const float* src_p = sc.patch.data() + (size_t(j.core_y0 * 2 - y0 * 2 + r) * (w * 2) + (j.core_x0 * 2 - x0 * 2)) * 3;
                    float* dst_p = j.direct_stitch_rgb2x + (size_t(j.direct_y2 + r) * j.direct_w2_stride + j.direct_x2) * 3;
                    std::copy_n(src_p, size_t(c_cols2) * 3, dst_p);
                }
            });
        } else {
            for (int y = j.core_y0 * 2; y < j.core_y1 * 2; ++y)
                std::copy_n(sc.patch.data() + (size_t(y - y0 * 2) * (w * 2) + (j.core_x0 - x0) * 2) * 3,
                    size_t(j.core_x1 - j.core_x0) * 6, j.rgb_2x_out + (size_t(y) * W2 + j.core_x0 * 2) * 3);
        }
    } else {
        // Untiled full-frame CPU path: stream RL in worker-local 2D luminance tiles!
        sc.green.release();
        sc.red.release();
        sc.blue.release();
        sc.guide.release();
        sc.mr.release();
        sc.mb.release();
        sc.tr.release();
        sc.tb.release();
        snr_gated_rl_bands(j.rgb_2x_out, H2, W2, j.psf_iters, j.psf_sigma, 0.18f, j.out_resid_noise_2x, nt,
            sc.rl_ws, rl_gain, rl_thr, Hh, Wh, j.is_astro, j.olpf_mode);
    }
    cost_mark(5);
    return true;
}

inline bool run_stage45_cpu_roi(heraw_gpu::Stage45GpuJob& j) {
    CpuStage45Scratch sc;
    return run_stage45_cpu_roi_ws(j, sc);
}

inline bool run_stage45_cpu(heraw_gpu::Stage45GpuJob& job, RlWorkspace* out_rl_ws = nullptr) {
    auto work = job;
    if (work.tile_size_1x > 0) work.tile_size_1x = std::min(1920, work.tile_size_1x);
    work.keep_frames_lin = true;
    work.can_direct_stitch = true;
    work.n_threads = std::max(1, work.n_threads);
    const size_t pixels = size_t(work.H1) * size_t(work.W1);
    const bool will_tile = !(work.tile_size_1x <= 0 ||
        (work.H1 <= work.tile_size_1x && work.W1 <= work.tile_size_1x && pixels <= work.max_tile_pixels));
    CpuStage45Scratch scratch;
    bool ok = heraw_gpu::run_stage45_streamed(work, [&](heraw_gpu::Stage45GpuJob& sub) {
        return run_stage45_cpu_roi_ws(sub, scratch);
    }, will_tile);
    if (out_rl_ws && scratch.rl_ws.lum0.n > out_rl_ws->lum0.n) {
        std::swap(*out_rl_ws, scratch.rl_ws);
    }
    job.out_err = work.out_err;
    job.out_device_label = work.out_device_label;
    job.out_eta_eff = work.out_eta_eff;
    job.out_resid_noise_2x = work.out_resid_noise_2x;
    job.out_peak_tile_pixels = work.out_peak_tile_pixels;
    job.out_tile_count = work.out_tile_count;
    job.out_tile_diagnostics = work.out_tile_diagnostics;
    job.out_wall_seconds = work.out_wall_seconds;
    job.out_device_seconds = work.out_device_seconds;
    job.out_has_device_timing = work.out_has_device_timing;
    job.out_plan_seconds = work.out_plan_seconds;
    job.out_crop_seconds = work.out_crop_seconds;
    job.out_stitch_seconds = work.out_stitch_seconds;
    return ok;
}

inline bool run_stage45_1x_cpu(heraw_gpu::Stage45GpuJob& j, RlWorkspace* out_rl_ws = nullptr) {
    const auto start = std::chrono::steady_clock::now();
    auto cost_start = start;
    j.out_wall_seconds.fill(0);
    j.out_device_seconds.fill(0);
    j.out_has_device_timing = false;
    j.out_plan_seconds = j.out_crop_seconds = j.out_stitch_seconds = 0.0;
    auto cost_mark = [&](size_t phase) {
        const auto now = std::chrono::steady_clock::now();
        j.out_wall_seconds[phase] += std::chrono::duration<double>(now - cost_start).count();
        cost_start = now;
    };

    const int H = j.H1, W = j.W1, Hh = H / 2, Wh = W / 2;
    if (H < 2 || W < 2 || !j.frames_lin || j.frames_lin->empty() || !j.rgb_2x_out) {
        j.out_err = "invalid 1x Stage45 dimensions or buffers";
        return false;
    }
    j.full_H1 = H;
    j.full_W1 = W;
    j.origin_y = 0;
    j.origin_x = 0;
    j.core_y0 = 0; j.core_y1 = 0;
    j.core_x0 = 0; j.core_x1 = 0;
    j.out_tile_count = 1;
    j.out_peak_tile_pixels = size_t(H) * size_t(W);

    const int nt = std::max(1, j.n_threads);
    const int s4_xtile = heraw_cache::host_cache_profile().stage4_x_tile();
    const auto& frames = *j.frames_lin;
    const auto& fy = *j.flows_fy;
    const auto& fx = *j.flows_fx;
    const auto& motion = *j.mw_half;
    const float* cov = j.inv_cov;
    const bool h_ge2 = (Hh >= 2 && Wh >= 2);

    bool is_bayer = !j.is_xtrans;
    if (is_bayer) {
        for (int r = 0; r < 6 && is_bayer; ++r)
            for (int c = 0; c < 6; ++c)
                if (j.cfa6x6[r][c] != j.cfa6x6[r & 1][c & 1]) { is_bayer = false; break; }
    }
    const uint8_t cfa2x2[2][2] = {{j.cfa6x6[0][0], j.cfa6x6[0][1]}, {j.cfa6x6[1][0], j.cfa6x6[1][1]}};

    std::vector<float> fw_arr(j.N);
    std::vector<uint8_t> rob_arr(j.N), unit_mw_arr(j.N, 1);
    std::vector<std::array<float, 4>> ois_arr(j.N, {0.0f, 0.0f, 0.0f, 0.0f});
    for (int n = 0; n < j.N; ++n) {
        fw_arr[n] = heraw_gpu::gpu_frame_weight(j, n);
        rob_arr[n] = heraw_gpu::gpu_use_robust(j, n) ? 1 : 0;
        if (j.frame_ois_shifts && j.frame_ois_shifts->size() >= size_t(n + 1) * 4) {
            for (int k = 0; k < 4; ++k) ois_arr[n][k] = (*j.frame_ois_shifts)[size_t(n) * 4 + k];
        }
        if (!j.is_virtual_nd) {
            for (float m : motion[n])
                if (m != 1.0f) { unit_mw_arr[n] = 0; break; }
        }
    }

    struct HalfAxis { int i0; float f0, f1; };
    std::vector<HalfAxis> hy_axis(H), hx_axis(W);
    if (h_ge2) {
        for (int Y = 0; Y < H; ++Y) {
            const float yc = clamp_f(0.5f * float(Y) - 0.25f, 0.0f, float(Hh - 1));
            const int y0 = clamp_i(int(yc), 0, Hh - 2);
            const float fv = clamp_f(yc - float(y0), 0.0f, 1.0f);
            hy_axis[Y] = {y0, 1.0f - fv, fv};
        }
        for (int X = 0; X < W; ++X) {
            const float xc = clamp_f(0.5f * float(X) - 0.25f, 0.0f, float(Wh - 1));
            const int x0 = clamp_i(int(xc), 0, Wh - 2);
            const float fv = clamp_f(xc - float(x0), 0.0f, 1.0f);
            hx_axis[X] = {x0, 1.0f - fv, fv};
        }
    }
    cost_mark(0);

    CpuStage45Scratch sc;
    sc.green.resize(size_t(H) * W);
    float* green = sc.green.data();
    std::vector<double> eta_sum(nt, 0.0);
    std::vector<int64_t> eta_count(nt, 0);
    const int eta_w = (W + 3) / 4;

    // 1. Native 1x Anisotropic Multi-Frame Green Fusion (true sensor-sampled Green + HA fallback on fast motion)
    parallel_for_rows(H, nt, [&](int y0, int y1, int tid) {
        for (int xb = 0; xb < W; xb += s4_xtile) {
            const int xe = std::min(W, xb + s4_xtile);
            for (int Y = y0; Y < y1; ++Y) {
                const int yi_h = clamp_i(Y >> 1, 0, std::max(0, Hh - 1));
                const size_t hrow_off = size_t(yi_h) * std::max(1, Wh);
                const HalfAxis hay = h_ge2 ? hy_axis[Y] : HalfAxis{0, 1.0f, 0.0f};
                const size_t bi_row0 = size_t(hay.i0) * std::max(1, Wh);
                for (int X = xb; X < xe; ++X) {
                    const size_t hi = hrow_off + clamp_i(X >> 1, 0, std::max(0, Wh - 1));
                    // Scale 2x precision by 0.72 so 1x Nyquist aperture prevents subpixel aliasing without softening
                    const float axx = 0.72f * cov[hi * 3 + 0];
                    const float axy = 0.72f * cov[hi * 3 + 1];
                    const float ayy = 0.72f * cov[hi * 3 + 2];
                    const float anchor = j.g_guide[hi];
                    const float threshold = 3.5f * std::sqrt(float(j.pg_sig0_sq) +
                        float(j.pg_gain) * std::max(0.0f, anchor)) + 0.008f;
                    const HalfAxis hax = h_ge2 ? hx_axis[X] : HalfAxis{0, 1.0f, 0.0f};
                    const size_t b00 = bi_row0 + hax.i0;
                    const size_t b10 = b00 + std::max(1, Wh);
                    auto sample_half_fast = [&](const float* p) -> float {
                        if (!h_ge2) return p[0];
                        return hay.f0 * (hax.f0 * p[b00] + hax.f1 * p[b00 + 1]) +
                               hay.f1 * (hax.f0 * p[b10] + hax.f1 * p[b10 + 1]);
                    };

                    const bool eta_site = (((Y | X) & 3) == 0);
                    float ws = 0.0f, vs = 0.0f, w2 = 0.0f;
                    for (int n = 0; n < j.N; ++n) {
                        const float fw = fw_arr[n];
                        if (fw <= 0.0f) continue;
                        const float mw_raw = unit_mw_arr[n] ? 1.0f : sample_half_fast(motion[n].data());
                        const float mw = (j.is_virtual_nd ? 1.0f : mw_raw) * fw;
                        if (mw < 0.01f) continue;
                        const float dy = n ? 2.0f * sample_half_fast(fy[n].data()) : 0.0f;
                        const float dx = n ? 2.0f * sample_half_fast(fx[n].data()) : 0.0f;
                        const float sy = float(Y) + dy, sx = float(X) + dx;
                        if (sy < 0.0f || sy > float(H - 1) || sx < 0.0f || sx > float(W - 1)) continue;
                        const float* f_ptr = frames[n].data();
                        const bool use_rob = rob_arr[n] != 0;
                        const int rb = int(std::floor(sy)), cb = int(std::floor(sx));
                        const int r_lo = std::max(0, rb - 1), r_hi = std::min(H - 1, rb + 2);
                        const int c_lo = std::max(0, cb - 1), c_hi = std::min(W - 1, cb + 2);
                        for (int rr = r_lo; rr <= r_hi; ++rr) {
                            const uint8_t* rcfa = j.cfa6x6[rr % 6];
                            const float* f_row = f_ptr + size_t(rr) * W;
                            const float yy = float(rr) - sy;
                            for (int cc = c_lo; cc <= c_hi; ++cc) {
                                const uint8_t ch = is_bayer ? cfa2x2[rr & 1][cc & 1] : rcfa[cc % 6];
                                if (ch != 1) continue;
                                const float xx = float(cc) - sx;
                                const float d = axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy;
                                if (d > 7.0f) continue;
                                const float v = f_row[cc];
                                float rob = 1.0f;
                                if (use_rob) {
                                    const float ad = std::fabs(v - anchor);
                                    if (ad > threshold) { const float q = threshold / ad; rob = q * q * q; }
                                }
                                const float w = mw * rob * weight_exp(0.5f * d);
                                ws += w;
                                vs += w * v;
                                if (eta_site) w2 += w * w;
                            }
                        }
                    }
                    float g_val = (ws > 1e-8f) ? (vs / ws) : j.g0_ha_1x[size_t(Y) * W + X];
                    const float alpha = clamp_f((1.85f - ws) / 1.25f, 0.0f, 0.85f);
                    if (alpha > 1e-3f) {
                        g_val += alpha * (j.g0_ha_1x[size_t(Y) * W + X] - g_val);
                    }
                    green[size_t(Y) * W + X] = g_val;
                    if (eta_site) {
                        const float eta_px = (ws > 1e-8f) ? (w2 / (ws * ws)) : 1.0f;
                        const float eff = (1.0f - alpha) * (1.0f - alpha) * eta_px + alpha * alpha * 0.55f;
                        eta_sum[tid] += double(eff);
                        ++eta_count[tid];
                        if (j.eta_quarter_out) {
                            j.eta_quarter_out[size_t(Y >> 2) * eta_w + (X >> 2)] = eff;
                        }
                    }
                }
            }
        }
    }, 16);

    j.out_eta_sum = 0; j.out_eta_count = 0;
    for (int t = 0; t < nt; ++t) {
        j.out_eta_sum += eta_sum[t];
        j.out_eta_count += eta_count[t];
    }
    j.out_eta_eff = std::max(0.01, j.out_eta_sum / double(std::max<int64_t>(1, j.out_eta_count)));
    j.out_device_label = "CPU SIMD 1x-TrueRGB";
    cost_mark(1);

    // 2. Native 1x Subpixel Auto-LCA True-Sampled R/B Color-Difference Fusion
    sc.red.resize(size_t(H) * W);
    sc.blue.resize(size_t(H) * W);
    sc.anc_r.resize(size_t(std::max(1, Hh)) * std::max(1, Wh));
    sc.anc_b.resize(size_t(std::max(1, Hh)) * std::max(1, Wh));
    float* red = sc.red.data();
    float* blue = sc.blue.data();
    float* anc_r = sc.anc_r.data();
    float* anc_b = sc.anc_b.data();

    AnchorTapList anc_taps[2][6][6];
    for (int b = 0; b < 2; ++b) {
        const uint8_t target = b ? 2 : 0;
        for (int ym = 0; ym < 6; ++ym) {
            for (int xm = 0; xm < 6; ++xm) {
                auto& lst = anc_taps[b][ym][xm];
                float wsum = 0.0f;
                for (int dy = -3; dy <= 3; ++dy) {
                    const uint8_t* rcfa = j.cfa6x6[(ym + dy + 6) % 6];
                    for (int dx = -3; dx <= 3; ++dx) {
                        if (rcfa[(xm + dx + 6) % 6] == target) {
                            const float w = 1.0f / (0.25f + float(dy * dy + dx * dx));
                            lst.dy[lst.count] = int8_t(dy);
                            lst.dx[lst.count] = int8_t(dx);
                            lst.nw[lst.count] = w;
                            ++lst.count;
                            wsum += w;
                        }
                    }
                }
                const float inv_w = 1.0f / std::max(wsum, 1e-12f);
                for (int k = 0; k < lst.count; ++k) lst.nw[k] *= inv_w;
            }
        }
    }
    parallel_for_rows(std::max(1, Hh), nt, [&](int r0, int r1, int) {
        for (int yi = r0; yi < r1; ++yi) {
            const int ay0 = clamp_i(2 * yi, 0, H - 1);
            const int ay1 = clamp_i(2 * yi + 1, 0, H - 1);
            float* ar = anc_r + size_t(yi) * std::max(1, Wh);
            float* ab = anc_b + size_t(yi) * std::max(1, Wh);
            const bool y_in = (ay0 >= 3 && ay1 + 3 < H);
            for (int xi = 0; xi < std::max(1, Wh); ++xi) {
                const int ax0 = clamp_i(2 * xi, 0, W - 1);
                const int ax1 = clamp_i(2 * xi + 1, 0, W - 1);
                if (y_in && ax0 >= 3 && ax1 + 3 < W) {
                    const auto& lr = anc_taps[0][ay0 % 6][ax0 % 6];
                    float sr = 0.0f;
                    const float* base_r = j.c_anc_1x + size_t(ay0) * W + ax0;
                    for (int k = 0; k < lr.count; ++k) sr += lr.nw[k] * base_r[ptrdiff_t(lr.dy[k]) * W + lr.dx[k]];
                    ar[xi] = sr;
                    const auto& lb = anc_taps[1][ay1 % 6][ax1 % 6];
                    float sb = 0.0f;
                    const float* base_b = j.c_anc_1x + size_t(ay1) * W + ax1;
                    for (int k = 0; k < lb.count; ++k) sb += lb.nw[k] * base_b[ptrdiff_t(lb.dy[k]) * W + lb.dx[k]];
                    ab[xi] = sb;
                } else {
                    ar[xi] = chroma_anchor(j, ay0, ax0, 0);
                    ab[xi] = chroma_anchor(j, ay1, ax1, 2);
                }
            }
        }
    });

    const float opt_cy = 0.5f * H, opt_cx = 0.5f * W;
    const float inv_opt_r = 1.0f / std::max(1.0f, std::max(opt_cy, opt_cx));
    const float hl_lim = j.glint_desat ? (j.is_astro ? 0.88f : 0.85f) : 1.0e6f;
    auto sample_green_1x = [&](float gy_s, float gx_s) -> float {
        if (gy_s >= 0.0f && gy_s < float(H - 1) && gx_s >= 0.0f && gx_s < float(W - 1)) {
            const int y0 = int(gy_s), x0 = int(gx_s);
            const float fy_v = gy_s - float(y0), fx_v = gx_s - float(x0);
            const float* r0 = green + size_t(y0) * W + x0;
            const float* r1 = r0 + W;
            return (1.0f - fy_v) * ((1.0f - fx_v) * r0[0] + fx_v * r0[1]) +
                   fy_v * ((1.0f - fx_v) * r1[0] + fx_v * r1[1]);
        }
        return bilerp_clamped(green, H, W, gy_s, gx_s);
    };

    parallel_for_rows(H, nt, [&](int y0, int y1, int) {
        for (int xb = 0; xb < W; xb += s4_xtile) {
            const int xe = std::min(W, xb + s4_xtile);
            for (int Y = y0; Y < y1; ++Y) {
                const int yi = clamp_i(Y >> 1, 0, std::max(0, Hh - 1));
                const size_t hrow = size_t(yi) * std::max(1, Wh);
                const HalfAxis hay = h_ge2 ? hy_axis[Y] : HalfAxis{0, 1.0f, 0.0f};
                const size_t bi_row0 = size_t(hay.i0) * std::max(1, Wh);
                for (int X = xb; X < xe; ++X) {
                    const int xi = clamp_i(X >> 1, 0, std::max(0, Wh - 1));
                    const size_t hi = hrow + xi;
                    const float axx = 0.55f * cov[hi * 3 + 0];
                    const float axy = 0.55f * cov[hi * 3 + 1];
                    const float ayy = 0.55f * cov[hi * 3 + 2];
                    const float g_gd = j.g_guide[hi];
                    const float g_here = green[size_t(Y) * W + X];
                    const HalfAxis hax = h_ge2 ? hx_axis[X] : HalfAxis{0, 1.0f, 0.0f};
                    const size_t b00 = bi_row0 + hax.i0;
                    const size_t b10 = b00 + std::max(1, Wh);
                    auto sample_half_c = [&](const float* p) -> float {
                        if (!h_ge2) return p[0];
                        return hay.f0 * (hax.f0 * p[b00] + hax.f1 * p[b00 + 1]) +
                               hay.f1 * (hax.f0 * p[b10] + hax.f1 * p[b10 + 1]);
                    };

                    const float ry = float(Y) - opt_cy, rx = float(X) - opt_cx;
                    const float rn = (rx * inv_opt_r) * (rx * inv_opt_r) + (ry * inv_opt_r) * (ry * inv_opt_r);
                    for (int b = 0; b < 2; ++b) {
                        const float anchor = b ? anc_b[hi] : anc_r[hi];
                        const float threshold = 4.0f * std::sqrt(float(j.pg_sig0_sq) + float(j.pg_gain) *
                            std::max(0.0f, std::max(anchor, g_gd))) + 0.01f;
                        const float wb = b ? j.wb_b : j.wb_r;
                        const float anchor_wb = anchor * wb, thr_wb = threshold * wb;
                        const float lca = (b ? j.lca_b_k1 : j.lca_r_k1) + (b ? j.lca_b_k2 : j.lca_r_k2) * rn;
                        const float lca_dy = lca * ry, lca_dx = lca * rx;
                        const uint8_t target_ch = b ? 2 : 0;

                        float weight = 0.0f, value = 0.0f;
                        for (int n = 0; n < j.N; ++n) {
                            const float fw = fw_arr[n];
                            if (fw <= 0.0f) continue;
                            const float mw_raw = unit_mw_arr[n] ? 1.0f : sample_half_c(motion[n].data());
                            const float mw = (j.is_virtual_nd ? 1.0f : mw_raw) * fw;
                            if (mw < 0.02f) continue;
                            const float dy = (n ? 2.0f * sample_half_c(fy[n].data()) : 0.0f) + lca_dy + ois_arr[n][b * 2];
                            const float dx = (n ? 2.0f * sample_half_c(fx[n].data()) : 0.0f) + lca_dx + ois_arr[n][b * 2 + 1];
                            const float yt = float(Y) + dy, xt = float(X) + dx;
                            if (yt < 0.0f || yt > float(H - 1) || xt < 0.0f || xt > float(W - 1)) continue;
                            const float* f_ptr = frames[n].data();
                            const bool use_rob = rob_arr[n] != 0;
                            const int rad = j.is_xtrans ? 3 : 2;
                            const int r_lo = std::max(0, int(std::floor(yt)) - (rad - 1));
                            const int r_hi = std::min(H - 1, int(std::floor(yt)) + rad);
                            const int c_lo = std::max(0, int(std::floor(xt)) - (rad - 1));
                            const int c_hi = std::min(W - 1, int(std::floor(xt)) + rad);
                            for (int rr = r_lo; rr <= r_hi; ++rr) {
                                const uint8_t* rcfa = j.cfa6x6[rr % 6];
                                const float* f_row = f_ptr + size_t(rr) * W;
                                const float yy = float(rr) - yt;
                                const float gy_src = float(rr) - dy;
                                for (int cc = c_lo; cc <= c_hi; ++cc) {
                                    const uint8_t ch = is_bayer ? cfa2x2[rr & 1][cc & 1] : rcfa[cc % 6];
                                    if (ch != target_ch) continue;
                                    const float xx = float(cc) - xt;
                                    const float d = (j.is_xtrans ? 0.58f : 1.0f) * (axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy);
                                    if (d > (j.is_xtrans ? 9.5f : 7.5f)) continue;
                                    const float v = f_row[cc] * wb;
                                    const float g_ref = sample_green_1x(gy_src, float(cc) - dx);
                                    float rob = 1.0f;
                                    if (use_rob) {
                                        const float ad = std::fabs(v - anchor_wb);
                                        if (ad > thr_wb) { const float q = thr_wb / ad; rob = q * q * q; }
                                    }
                                    const float w = mw * rob * weight_exp(0.5f * d);
                                    weight += w;
                                    value += w * (v - g_ref);
                                }
                            }
                        }
                        const float anc_diff = anchor_wb - g_here;
                        const float cov_w = clamp_f(weight / 0.35f, 0.0f, 1.0f);
                        float diff = (weight > 1e-8f) ? (cov_w * (value / weight) + (1.0f - cov_w) * anc_diff) : anc_diff;
                        const float peak = std::max(g_here, anchor);
                        if (peak > hl_lim) {
                            const float t = clamp_f((peak - hl_lim) / 0.14f, 0.0f, 1.0f);
                            diff *= (1.0f - t) * (1.0f - t);
                        }
                        (b ? blue : red)[size_t(Y) * W + X] = diff;
                    }
                }
            }
        }
    }, 16);
    sc.anc_r.release();
    sc.anc_b.release();

    if (j.on_stage4_done) {
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        j.on_stage4_done(j.out_eta_eff, seconds, j.out_device_label);
    }
    cost_mark(2);

    // 3. 1x Poisson-Gaussian GAT + Multiscale DCT Empirical Wiener + Unbiased Inverse GAT
    const double gain = std::max(0.0, j.pg_gain * j.out_eta_eff);
    const double sig2 = std::max(1e-10, j.pg_sig0_sq * j.out_eta_eff);
    const double ped = 4.0 * std::sqrt(sig2), gat_c = 0.375 * gain * gain + sig2, sqrt_c = std::sqrt(gat_c);
    const float ped_f = float(ped), gain_f = float(gain), gat_c_f = float(gat_c), sqrt_c_f = float(sqrt_c);

    sc.z.resize(size_t(H) * W);
    float* z = sc.z.data();
    parallel_for_rows(H, nt, [&](int y0, int y1, int) {
        for (size_t i = size_t(y0) * W; i < size_t(y1) * W; ++i) {
            const float v = std::max(0.0f, green[i] + ped_f);
            z[i] = 2.0f * v / (std::sqrt(gain_f * v + gat_c_f) + sqrt_c_f);
        }
    });
    const float* den = z;
    if (j.denoise_strength > 1e-4f) {
        sc.den.resize(size_t(H) * W);
        multiscale_dct_denoise_gat(sc.den.data(), H, W, nt, j.denoise_strength, j.edge_polish,
                                   j.snr_boost_half, Hh, Wh, nullptr, z, &sc.dct_pilot, 2);
        den = sc.den.data();
    }
    const int Hv = std::max(1, Hh), Wv = std::max(1, Wh);
    sc.variance.resize(size_t(Hv) * Wv);
    float* variance = sc.variance.data();
    parallel_for_rows(Hv, nt, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y) {
            float* vrow = variance + size_t(y) * Wv;
            for (int x = 0; x < Wv; ++x) {
                float sum = 0.0f; int cnt = 0;
                for (int dy = 0; dy < 2 && 2 * y + dy < H; ++dy) {
                    const size_t roff = size_t(2 * y + dy) * W;
                    for (int dx = 0; dx < 2 && 2 * x + dx < W; ++dx) {
                        const float diff = z[roff + 2 * x + dx] - den[roff + 2 * x + dx];
                        sum += diff * diff;
                        ++cnt;
                    }
                }
                vrow[x] = std::min((cnt > 0 ? sum / float(cnt) : 0.0f), 1.25f);
            }
        }
    });
    sep_gaussian_2d(variance, variance, Hv, Wv, 6.0f, nt);
    parallel_for_rows(H, nt, [&](int y0, int y1, int) {
        const float q_gain = 0.25f * gain_f;
        for (int y = y0; y < y1; ++y) {
            const float* zm = z + size_t(clamp_i(y - 1, 0, H - 1)) * W;
            const float* z0 = z + size_t(y) * W;
            const float* zp = z + size_t(clamp_i(y + 1, 0, H - 1)) * W;
            const float* dn = den + size_t(y) * W;
            const float* vr = variance + size_t(clamp_i(y >> 1, 0, Hv - 1)) * Wv;
            float* gr = green + size_t(y) * W;
            for (int x = 0; x < W; ++x) {
                const int xm = clamp_i(x - 1, 0, W - 1), xp = clamp_i(x + 1, 0, W - 1);
                const float lo = std::min({zm[xm], zm[x], zm[xp], z0[xm], z0[x], z0[xp], zp[xm], zp[x], zp[xp]});
                const float hi = std::max({zm[xm], zm[x], zm[xp], z0[xm], z0[x], z0[xp], zp[xm], zp[x], zp[xp]});
                const float v = clamp_f(dn[x], lo, hi);
                const float dv = vr[clamp_i(x >> 1, 0, Wv - 1)];
                gr[x] = sqrt_c_f * v + q_gain * (v * v + dv) - ped_f;
            }
        }
    });
    if (j.psf_iters > 0) std::swap(sc.rl_ws.lum0, sc.z);
    sc.z.release();
    sc.den.release();
    sc.dct_pilot.release();
    sc.variance.release();

    double mean = 0.0;
    for (size_t i = 0; i < size_t(H) * W; i += 16) mean += std::max(0.0f, green[i]);
    mean /= double((size_t(H) * W + 15) / 16);
    cost_mark(3);

    // 4. 1x Luminance-Guided A-Trous Chroma Denoising (preserves true-sampled fine color detail)
    if (j.chroma_denoise > 1e-4f) {
        sc.mr.resize(size_t(H) * W);
        sc.mb.resize(size_t(H) * W);
        median3x3_plane(red, sc.mr.data(), H, W, nt);
        median3x3_plane(blue, sc.mb.data(), H, W, nt);
        std::swap(sc.red, sc.mr);
        std::swap(sc.blue, sc.mb);
        red = sc.red.data();
        blue = sc.blue.data();
        if (j.glint_desat) {
            float* bg = sc.mr.data();
            sep_gaussian_2d(green, bg, H, W, 2.0f, nt);
            parallel_for_rows(H, nt, [&](int y0, int y1, int) {
                for (size_t i = size_t(y0) * W; i < size_t(y1) * W; ++i) {
                    const float spike_g = std::max(0.0f, green[i] - bg[i]);
                    const float excess = spike_g > 0.015f ? (spike_g + 0.35f * std::max(0.0f, std::max(red[i], blue[i]))) : 0.0f;
                    const float peak = std::max(green[i], std::max(green[i] + red[i], green[i] + blue[i]));
                    const float ds = std::max(clamp_f((excess - 0.045f) / 0.12f, 0.0f, 0.92f),
                                              clamp_f((peak - 0.94f) / 0.10f, 0.0f, 0.85f));
                    red[i] *= 1.0f - ds;
                    blue[i] *= 1.0f - ds;
                }
            });
        }
        const float wbmax = std::max(j.wb_r, j.wb_b);
        const float sigma_c = 0.85f * j.chroma_denoise * float(std::sqrt((1.0 + wbmax * wbmax) * j.out_eta_eff * (j.pg_sig0_sq + j.pg_gain * mean)));
        atrous_global(red, blue, green, j, sigma_c, sc.mr, sc.mb);
    }

    const float inv_wbr = 1.0f / std::max(1e-4f, j.wb_r);
    const float inv_wbb = 1.0f / std::max(1e-4f, j.wb_b);
    parallel_for_rows(H, nt, [&](int y0, int y1, int) {
        for (int y = y0; y < y1; ++y) {
            const size_t roff = size_t(y) * W;
            float* dst = j.rgb_2x_out + roff * 3;
            for (int x = 0; x < W; ++x) {
                const float gv = green[roff + x];
                dst[x * 3 + 0] = std::max(0.0f, (gv + red[roff + x]) * inv_wbr);
                dst[x * 3 + 1] = std::max(0.0f, gv);
                dst[x * 3 + 2] = std::max(0.0f, (gv + blue[roff + x]) * inv_wbb);
            }
        }
    });
    sc.green.release();
    sc.red.release();
    sc.blue.release();
    sc.mr.release();
    sc.mb.release();
    j.out_resid_noise_2x = 0.35f * float(std::sqrt(sig2 + gain * mean));
    cost_mark(4);

    // 5. 1x Focus-Coupled OLPF RL Deconvolution
    if (j.psf_iters > 0 && j.rl_mod_f16) {
        const int Hq = std::max(1, H / 4), Wq = std::max(1, W / 4);
        std::vector<float> rlg_q(size_t(Hq) * Wq, 1.0f), rlt_q(size_t(Hq) * Wq, 1.0f);
        parallel_for_rows(Hq, nt, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wq; ++c) {
                    float sg = 0.0f, st = 0.0f; int cnt = 0;
                    for (int dy = 0; dy < 2; ++dy) {
                        const int ry = clamp_i(2 * r + dy, 0, std::max(0, Hh - 1));
                        for (int dx = 0; dx < 2; ++dx) {
                            const int cx = clamp_i(2 * c + dx, 0, std::max(0, Wh - 1));
                            const uint32_t pk = j.rl_mod_f16[size_t(ry) * std::max(1, Wh) + cx];
                            sg += unpack_half(uint16_t(pk));
                            st += unpack_half(uint16_t(pk >> 16));
                            ++cnt;
                        }
                    }
                    rlg_q[size_t(r) * Wq + c] = sg / float(std::max(1, cnt));
                    rlt_q[size_t(r) * Wq + c] = st / float(std::max(1, cnt));
                }
            }
        });
        const int iters_1x = std::max(1, (j.psf_iters + 1) / 2 + 1);
        const float sigma_1x = std::max(0.42f, 0.56f * j.psf_sigma);
        snr_gated_rl_bands(j.rgb_2x_out, H, W, iters_1x, sigma_1x, 0.15f, j.out_resid_noise_2x, nt,
                           sc.rl_ws, rlg_q.data(), rlt_q.data(), Hq, Wq, j.is_astro, j.olpf_mode);
        if (out_rl_ws && sc.rl_ws.lum0.n > out_rl_ws->lum0.n) {
            std::swap(*out_rl_ws, sc.rl_ws);
        }
    }
    cost_mark(5);
    return true;
}

} // namespace heraw_cpu
