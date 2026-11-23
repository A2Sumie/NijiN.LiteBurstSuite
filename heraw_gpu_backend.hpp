// heraw_gpu_backend.hpp -- Cross-platform GPU compute interface (macOS Metal + Windows Vulkan)
#pragma once

#include "heraw_fs_utf8.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace heraw_gpu {

enum class BackendPref {
    AUTO,
    GPU,
    CPU
};

struct GpuDeviceInfo {
    bool available = false;
    std::string api_name;
    std::string device_name;
};

// Exact std430 / scalar push-constant structs shared by Metal & Vulkan kernels
struct PC_Zero {
    uint32_t count;
};

struct PC_GreenAccum {
    int32_t H1, W1, Hh, Wh, Ew, is_frame0, use_rob, is_xtrans;
    uint32_t cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, frame_w_scale;
    int32_t origin_y, origin_x;
    float q_luma_var, q_nlt_s0, q_nlt_s1, q_nlt_s2;
};

struct PC_GreenFinal {
    int32_t H1, W1, Hh, Wh, Ew;
};

struct PC_ChromaAccum {
    int32_t H1, W1, Hh, Wh, is_frame0, use_rob, is_xtrans;
    uint32_t cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, wb_r, wb_b;
    float lca_r_k1, lca_r_k2, lca_b_k1, lca_b_k2;
    float ois_dr_y, ois_dr_x, ois_db_y, ois_db_x;
    int32_t full_H1, full_W1, origin_y, origin_x;
    float q_chroma_var, q_nlt_s0, q_nlt_s1, q_nlt_s2;
};

struct PC_ChromaFinal {
    int32_t H1, W1, Hh, Wh;
    float wb_r, wb_b, hl_start, hl_span;
    uint32_t cfa0, cfa1, cfa2;
    int32_t is_xtrans;
};

static_assert(sizeof(PC_GreenAccum) == 80, "Green push constant ABI");
static_assert(sizeof(PC_ChromaAccum) == 120, "Chroma push constant ABI");
static_assert(sizeof(PC_ChromaFinal) == 48, "Chroma final push constant ABI");

struct PC_RlPrepV {
    int32_t H, W, is_astro;
};

struct PC_GatFwd {
    uint32_t count;
    float ped, g_eff, gat_c, sqrt_c;
};

struct PC_BoxDown2x {
    int32_t Hs, Ws, Hd, Wd;
};

struct PC_DctWiener {
    int32_t H, W, Hh, Wh, is_wiener, scale_mul;
    float sigma;
    int32_t roi_y0=0, roi_x0=0, roi_y1=0, roi_x1=0;
};

struct PC_PyrLapAdd {
    int32_t Hs, Ws, Hd, Wd;
    float tau2;
};

struct PC_EdgePolish {
    int32_t H, W, dir;
};

struct PC_DvMap {
    int32_t Hh, Wh, W2;
};

struct PC_Gauss1D {
    int32_t H, W, dir, radius;
    float w[25];
};
static_assert(sizeof(PC_Gauss1D) <= 128, "Vulkan minimum push constant limit");

struct PC_GatInvQuad {
    int32_t H1, W1, Hh, Wh;
    float sqrt_c, g_eff, ped;
};

struct PC_Size2D {
    int32_t H, W;
};

struct PC_Atrous1D {
    int32_t H, W, Hh, Wh, dir, step;
    float inv_sr2, inv_sb2, inv_sg2;
};

struct PC_AssembleRgb {
    int32_t H1, W1, H2, W2;
    float inv_wb_r, inv_wb_b;
};

struct PC_RlPrepH {
    int32_t H, W, Hh, Wh;
    float g_thr2;
};

inline constexpr std::array<float, 6> kGpuAtrousLvDecay = {
    1.0f,
    0.56f,
    0.56f * 0.56f,
    0.56f * 0.56f * 0.56f,
    0.56f * 0.56f * 0.56f * 0.56f,
    0.56f * 0.56f * 0.56f * 0.56f * 0.56f,
};

inline constexpr uint16_t float_to_fp16_bits(float val) noexcept {
    const uint32_t f = std::bit_cast<uint32_t>(val);
    uint32_t sign = (f >> 16) & 0x8000u;
    int32_t exp = ((int32_t)((f >> 23) & 0xFFu)) - 127 + 15;
    uint32_t mant = f & 0x007FFFFFu;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | ((mant + 0x1000u) >> 13));
}

inline constexpr uint32_t pack_half2_u32(float x, float y) noexcept {
    return (uint32_t)float_to_fp16_bits(x) | ((uint32_t)float_to_fp16_bits(y) << 16);
}

inline uint32_t gpu_downsample_rlmod_q(const uint32_t* rl_mod_f16, int Hh, int Wh, int r, int c) noexcept {
    auto unpack_h = [](uint16_t h) -> float {
        const uint32_t sign = uint32_t(h & 0x8000u) << 16;
        const uint32_t exp  = (h >> 10) & 0x1Fu;
        const uint32_t mant = h & 0x03FFu;
        uint32_t f = sign;
        if (exp == 0) {
            if (mant != 0) {
                uint32_t m = mant, e = 127 - 14;
                while ((m & 0x0400u) == 0) { m <<= 1; --e; }
                m &= 0x03FFu;
                f = sign | (e << 23) | (m << 13);
            }
        } else if (exp == 31) {
            f = sign | 0x7F800000u | (mant << 13);
        } else {
            f = sign | ((exp + 127 - 15) << 23) | (mant << 13);
        }
        return std::bit_cast<float>(f);
    };
    float sg = 0.0f, st = 0.0f;
    for (int dy = 0; dy < 2; ++dy) {
        const int ry = std::max(0, std::min(Hh - 1, 2 * r + dy));
        for (int dx = 0; dx < 2; ++dx) {
            const int cx = std::max(0, std::min(Wh - 1, 2 * c + dx));
            const uint32_t pk = rl_mod_f16[size_t(ry) * std::max(1, Wh) + cx];
            sg += unpack_h(uint16_t(pk));
            st += unpack_h(uint16_t(pk >> 16));
        }
    }
    return pack_half2_u32(0.25f * sg, 0.25f * st);
}

struct PC_RlRatio {
    uint32_t count;
    float snr_knee, clamp_delta;
};

struct PC_V3PrepGuides {
    int32_t H1, W1, Hh, Wh, cov_off;
    float sig0_ref, gain_ref;
    uint32_t cfa0, cfa1, cfa2;
};

struct PC_V3Green {
    int32_t H1, W1, Hh, Wh, Ew, Eh, N, out_scale;
    int32_t origin_y, origin_x, is_bayer, is_clean_plate;
    int32_t is_virtual_nd, is_aeb, any_rob, fcomp_off_eta;
    uint32_t cfa0, cfa1, cfa2;
    int32_t disable_ha_fallback;
};

struct PC_V3Chroma {
    int32_t H1, W1, Hh, Wh, N, out_scale, full_H1, full_W1;
    int32_t origin_y, origin_x, is_bayer, is_xtrans;
    int32_t is_virtual_nd, is_aeb, any_rob, is_astro;
    uint32_t cfa0, cfa1, cfa2;
};

struct PC_V3VstFwd {
    int32_t H_out, W_out, full_H_out, origin_out_y;
    float u_min, u_max, inv_du, pad0;
};

struct PC_V3FftSure {
    int32_t H_out, W_out, Hh, Wh, eta_h, eta_w, eta_off, color;
    int32_t ntr_c, ntc_c, phase_y, phase_x, inv_out_s, eta_shift;
    float inv_mean_eta, oct_mul0, oct_mul1, oct_mul2, oct_mul3;
    int32_t nr_t, nc_t;
};

struct PC_V3DvHalf {
    int32_t H_out, W_out, Hv, Wv;
};

struct PC_V3VstInv {
    int32_t H_out, W_out, Hv, Wv, full_H_out, origin_out_y;
    float u_min, u_max, inv_du, pad0;
    int32_t phase_y, phase_x;
};

struct PC_V3ImpClamp {
    int32_t H, W;
    float imp_thr, pad0;
};

struct PC_V3Down2 {
    int32_t Hs, Ws, Hd, Wd;
};

struct PC_V3Up2Lap {
    int32_t Hs, Ws, Hd, Wd, mode;
};

struct PC_V3OddHuber {
    int32_t H, W;
    float delta, inv_2eg, thr_z;
};

struct PC_V3Assemble {
    int32_t H1, W1, H_out, W_out;
    int32_t out_scale, Hh, Wh, anc_off;
    float inv_wb_r, inv_wb_b, inv_2sig_g2, pad0;
};

static_assert(sizeof(PC_V3PrepGuides) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3Green) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3Chroma) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3VstFwd) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3FftSure) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3DvHalf) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3VstInv) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3ImpClamp) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3Down2) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3Up2Lap) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3OddHuber) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_V3Assemble) <= 128, "Vulkan push constant limit");

struct PC_M5aScatter {
    int32_t H1, W1, Hh, Wh;
    int32_t H_out, W_out, out_scale, tile;
    int32_t nr, nc, nr_band, tr_b0;
    int32_t band_y0, band_h, rgb_in_y0, rgb_in_h;
    int32_t sy0, sy_cnt, hy0, hy_cnt;
    int32_t frame_n, is_it0, zero_gm_only, pad0;
    float v_scale, quad, q_scale, pad2;
};

struct PC_M5aUpdate {
    int32_t H_out, W_out, out_scale, tile;
    int32_t nc, nr_band, tr_b0, band_y0;
    int32_t band_h, rgb_in_y0, wg_x;
    float q_scale;
    float tau_morozov, v_scale, rv_base, sh_base;
    float quad, k_pi_g, k_pi_c_coarse, k_pi_c;
    float sat0, wb_r, wb_b;
};

struct PC_M5aJacobian {
    int32_t H_out, W_out, band_y0, band_h;
    int32_t rgb_in_y0, rgb_in_h, has_fcomp, wg_x;
    float strength, rv_base, sh_base, quad;
};

struct PC_M5bFft {
    int32_t H, W, band_y0, band_h;
    int32_t in_y0, in_h, nr_band, nc;
    int32_t out_scale, olpf_mode, is_astro, n_zones;
    int32_t focus_h, focus_w, psf_iters, has_complex_otf;
    float str, psf_sigma_tile, rv, sh;
    float quad, max_boost, sig_tgt, m5a_gamma;
    float photo_shelf_k;
    int32_t includes_pix_ap;
    int32_t tr_b0, n_fb_zones;
    float wb_r, wb_b;
};

struct PC_M5bClamp {
    int32_t H, W, band_y0, band_h;
    int32_t in_y0, in_h, nr_band, nc;
    int32_t focus_h, focus_w, has_focus, wg_x;
    float rv, sh, quad, wb_r;
    float wb_b, sat0;
};

static_assert(sizeof(PC_M5aScatter) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_M5aUpdate) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_M5aJacobian) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_M5bFft) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_M5bClamp) <= 128, "Vulkan push constant limit");

struct PC_C13Warp {
    int32_t H1, W1, Hh, Wh;
    int32_t sy0, sy_cnt, hy0, hy_cnt;
    int32_t band_y0, band_h, frame_n, has_flow;
    int32_t has_mw, write_fallback, pad0, pad1;
    uint32_t cfa0, cfa1, cfa2, pad2;
};

struct PC_C13Scene {
    int32_t Wh, band_y0, band_h, N;
};

struct PC_C13Med4Ph {
    int32_t H1, W1, is_col, out_stride;
    int32_t res_off, row4_off, pad0, pad1;
};

struct PC_C13Hp97 {
    int32_t sz, is_col, out_off, pad0;
};

struct PC_C13SensorRes {
    int32_t H, W, Hh, Wh;
    int32_t band_y0, band_h, hy0, hy_cnt;
    int32_t out_off, raw_has_band, has_flow, has_mw;
    float read_variance, shot;
    uint32_t cfa0, cfa1, cfa2, pad0;
};

struct PC_C13HotMap {
    int32_t H1, W1, N, band_y0;
    int32_t band_h, wg_x, col_base_off, pad0;
    float hot_sigma, hot_thr, pad1, pad2;
};

static_assert(sizeof(PC_C13Warp) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_C13Scene) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_C13Med4Ph) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_C13Hp97) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_C13SensorRes) <= 128, "Vulkan push constant limit");
static_assert(sizeof(PC_C13HotMap) <= 128, "Vulkan push constant limit");

namespace detail_gpu {
consteval double cexp(double x) noexcept {
    constexpr double kLn2Hi = 0.693147180369123816490;
    constexpr double kLn2Lo = 1.90821492927058770002e-10;
    const int k = static_cast<int>(x >= 0.0 ? (x * std::numbers::log2e_v<double> + 0.5)
                                            : (x * std::numbers::log2e_v<double> - 0.5));
    const double kf = static_cast<double>(k);
    const double r = (x - kf * kLn2Hi) - kf * kLn2Lo;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 20; ++i) {
        term *= r / static_cast<double>(i);
        sum += term;
    }
    if (k >= 0) {
        for (int i = 0; i < k; ++i) sum *= 2.0;
    } else {
        for (int i = 0; i < -k; ++i) sum *= 0.5;
    }
    return sum;
}

consteval PC_Gauss1D make_gauss_weights_const(float sigma, int rad) noexcept {
    PC_Gauss1D pc{};
    pc.radius = rad;
    double ksum = 1.0;
    pc.w[0] = 1.0f;
    for (int k = 1; k <= rad; ++k) {
        const double wk = cexp(-0.5 * double(k * k) / (double(sigma) * double(sigma)));
        pc.w[k] = float(wk);
        ksum += 2.0 * wk;
    }
    const float inv_s = float(1.0 / ksum);
    for (int k = 0; k <= rad; ++k) pc.w[k] *= inv_s;
    return pc;
}

inline constexpr PC_Gauss1D kGauss065 = make_gauss_weights_const(0.65f, 2);
inline constexpr PC_Gauss1D kGauss100 = make_gauss_weights_const(1.0f, 3);
inline constexpr PC_Gauss1D kGauss250 = make_gauss_weights_const(2.5f, 8);
inline constexpr PC_Gauss1D kGauss800 = make_gauss_weights_const(8.0f, 24);
} // namespace detail_gpu

inline PC_Gauss1D make_gauss_pc(int H, int W, int dir, float sigma) {
    const PC_Gauss1D* preset = nullptr;
    if (sigma == 0.65f) preset = &detail_gpu::kGauss065;
    else if (sigma == 1.0f) preset = &detail_gpu::kGauss100;
    else if (sigma == 2.5f) preset = &detail_gpu::kGauss250;
    else if (sigma == 8.0f) preset = &detail_gpu::kGauss800;
    if (preset) [[likely]] {
        PC_Gauss1D pc = *preset;
        pc.H = H;
        pc.W = W;
        pc.dir = dir;
        return pc;
    }
    PC_Gauss1D pc{};
    pc.H = H;
    pc.W = W;
    pc.dir = dir;
    int rad = std::max(1, std::min(24, (int)std::ceil(3.0f * std::max(0.05f, sigma))));
    pc.radius = rad;
    double ksum = 1.0;
    pc.w[0] = 1.0f;
    for (int k = 1; k <= rad; ++k) {
        double wk = std::exp(-0.5 * (double)(k * k) / ((double)sigma * (double)sigma));
        pc.w[k] = (float)wk;
        ksum += 2.0 * wk;
    }
    const float inv_s = (float)(1.0 / ksum);
    for (int k = 0; k <= rad; ++k) pc.w[k] *= inv_s;
    return pc;
}

inline PC_Gauss1D make_olpf_psf_pc(int H, int W, int dir, float sigma, int olpf_mode) {
    if (olpf_mode == 2) {
        // 16-point Gaussian-Distribution LPF (e.g. Canon EOS R1): smooth composite Gaussian
        return make_gauss_pc(H, W, dir, std::hypot(sigma, 0.82f));
    }
    if (olpf_mode != 1) {
        return make_gauss_pc(H, W, dir, sigma);
    }
    // 4-spot birefringent Weak OLPF (split d ~= 0.71p_1x = 1.42p_2x -> half-split h = 0.71 in 2x px)
    PC_Gauss1D pc{};
    pc.H = H; pc.W = W; pc.dir = dir;
    const double s = std::max(0.25, (double)sigma);
    const double hs = 0.71;
    int rad = std::max(2, std::min(24, (int)std::ceil(3.0 * s + hs)));
    pc.radius = rad;
    auto eval_tap = [&](double x) -> double {
        double d1 = (x - hs) / s, d2 = (x + hs) / s;
        return 0.5 * (std::exp(-0.5 * d1 * d1) + std::exp(-0.5 * d2 * d2));
    };
    pc.w[0] = (float)eval_tap(0.0);
    double ksum = pc.w[0];
    for (int k = 1; k <= rad; ++k) {
        double wk = eval_tap((double)k);
        pc.w[k] = (float)wk;
        ksum += 2.0 * wk;
    }
    const float inv_s = (float)(1.0 / std::max(1e-12, ksum));
    for (int k = 0; k <= rad; ++k) pc.w[k] *= inv_s;
    return pc;
}

inline void pack_cfa6x6_bits(const uint8_t cfa[6][6], uint32_t out_w[3]) {
    out_w[0] = 0u; out_w[1] = 0u; out_w[2] = 0u;
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            int idx = r * 6 + c;
            out_w[idx >> 4] |= ((uint32_t)(cfa[r][c] & 3u)) << ((idx & 15) * 2);
        }
    }
}

inline float quick_pct_gpu_helper(std::vector<float>& v, float q) {
    if (v.empty()) return 0.0f;
    int64_t raw_i = (int64_t)std::llround(q * (double)(v.size() - 1));
    size_t idx = (size_t)std::max<int64_t>(0, std::min<int64_t>((int64_t)(v.size() - 1), raw_i));
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

struct Stage45GpuJob {
    int H1 = 0, W1 = 0, N = 0, n_threads = 1;
    bool is_clean_plate = false;
    // Clean plate anchors are caller-supplied aligned temporal medians in
    // g_guide, c_anc_1x and g0_ha_1x. Virtual ND integrates all aligned frames.
    bool is_virtual_nd = false;
    bool is_astro = false;
    bool is_xtrans = false;
    bool keep_frames_lin = false;
    int olpf_mode = 0; // 0=none, 1=weak4, 2=gauss16
    uint8_t cfa6x6[6][6] = {
        {0, 1, 0, 1, 0, 1}, {1, 2, 1, 2, 1, 2},
        {0, 1, 0, 1, 0, 1}, {1, 2, 1, 2, 1, 2},
        {0, 1, 0, 1, 0, 1}, {1, 2, 1, 2, 1, 2}
    };
    float lca_r_k1 = 0.0f, lca_r_k2 = 0.0f;
    float lca_b_k1 = 0.0f, lca_b_k2 = 0.0f;
    const std::vector<float>* frame_ois_shifts = nullptr; // optional [N * 4]: dr_y, dr_x, db_y, db_x
    const std::vector<float>* frame_lucky_w = nullptr;    // optional [N] frame directional weight

    std::vector<std::vector<float>>* frames_lin = nullptr;
    const std::vector<std::vector<float>>* flows_fy = nullptr;
    const std::vector<std::vector<float>>* flows_fx = nullptr;
    const std::vector<std::vector<float>>* mw_half = nullptr;
    const float* inv_cov = nullptr;        // [Hh * Wh * 3]
    const float* g_guide = nullptr;        // [Hh * Wh]
    const float* c_anc_1x = nullptr;       // [H1 * W1]
    const float* g0_ha_1x = nullptr;       // [H1 * W1] Hamilton-Adams directional 1x Green
    const float* snr_boost_half = nullptr; // [Hh * Wh]
    const uint32_t* rl_mod_f16 = nullptr;  // [Hh * Wh] packed half2(rl_gain, thr_mul)

    double pg_sig0_sq = 0.0;
    double pg_gain = 0.0;
    float q_luma_var = 0.0f;
    float q_chroma_var = 0.0f;
    float q_nlt_s0 = 1.0f;
    float q_nlt_s1 = 0.0f;
    float q_nlt_s2 = 0.0f;
    float q_corr_gain = 0.0f;
    float dct_scale_mul[5] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float wb_r = 1.0f;
    float wb_b = 1.0f;

    float denoise_strength = 1.0f;
    float chroma_denoise = 1.0f;
    int psf_iters = 4;
    float psf_sigma = 1.05f;
    bool edge_polish = true;
    bool glint_desat = true;

    // Unified v3 deterministic M3/M4 flags and per-frame/wavelet noise tables.
    bool math_v3_unified = false;
    bool defer_optical_psf = false;
    const float* frame_gains = nullptr;
    const float* frame_read_vars = nullptr;
    const float* frame_shot_coeffs = nullptr;
    const float* frame_sat_levels = nullptr;
    const float* qvar_luma_row = nullptr;
    const float* qvar_chroma_row = nullptr;
    const float* nlt_slope_lut256 = nullptr;
    const float* walsh16_norm = nullptr;
    int qvar_rows = 0;
    const float* tile_spec_map = nullptr; // optional packed TileSpectralMap [th * tw * 8]
    int tile_spec_h = 0;
    int tile_spec_w = 0;
    int tile_spec_px = 64;
    int highlight_mode = 0;               // 0=legacy, 1=oklab-stage, 2=hard-clip
    float* fisher_comp_out = nullptr;
    bool is_1x_mode = false;
    bool disable_ha_fallback = false;
    bool has_global_g_range = false;
    float global_min_g = 0.0f;
    float global_max_g = 1.0f;
    float* direct_stitch_fcomp = nullptr;

    // Core edge in 1x pixels; zero selects one full-image dispatch for comparison.
    int tile_size_1x = 2640;
    // Conservative live-buffer envelope, including Vulkan upload staging.
    size_t max_tile_pixels = size_t(1536) * 1024 * 1024 / 128;
    size_t out_peak_tile_pixels = 0;
    int out_tile_count = 0;
    std::string out_tile_diagnostics;
    // Setup, green, chroma fusion, GAT/DCT, chroma filtering, RL, readback.
    std::array<double,7> out_wall_seconds{}, out_device_seconds{};
    bool out_has_device_timing=false;
    double out_plan_seconds=0, out_crop_seconds=0, out_stitch_seconds=0;

    // Internal ROI/global statistics contract shared by the two backends.
    int full_H1 = 0, full_W1 = 0, origin_y = 0, origin_x = 0;
    int fcomp_origin_y = 0;
    double global_eta = -1.0, global_mean_g = -1.0;
    float global_edge_p80 = -1.0f;
    bool eta_only = false;
    int core_y0 = 0, core_x0 = 0, core_y1 = 0, core_x1 = 0;
    double out_eta_sum = 0.0;
    int64_t out_eta_count = 0;
    float* eta_quarter_out=nullptr; // Caller-owned global (ceil(H1/4),ceil(W1/4)).

    std::function<void(double eta_eff, double dt_s4, const std::string& dev_label)> on_stage4_done;

    // Outputs
    size_t arena_peak_1x_pixels = 0;
    bool can_direct_stitch = false;
    float* direct_stitch_rgb2x = nullptr;
    int direct_w2_stride = 0, direct_y2 = 0, direct_x2 = 0;
    float* rgb_2x_out = nullptr; // [H2 * W2 * 3] caller-allocated
    double out_eta_eff = 0.25;
    float out_resid_noise_2x = 0.0f;
    std::string out_device_label;
    std::string out_err;
};

inline float gpu_eval_nlt_sq(float s0, float s1, float s2, float iv) {
    const float x = std::max(0.0f, std::min(1.0f, iv));
    return std::max(0.0f, s0 + x * (s1 + x * s2));
}

inline void eval_tile_modulated_icov(const Stage45GpuJob& j, int yh, int xh,
                                     float& axx, float& axy, float& ayy) noexcept {
    if (!j.tile_spec_map || j.tile_spec_h <= 0 || j.tile_spec_w <= 0 || j.tile_spec_px <= 0) return;
    const float tile_px = static_cast<float>(j.tile_spec_px);
    const float gy = static_cast<float>(j.origin_y + 2 * yh) / tile_px - 0.5f;
    const float gx = static_cast<float>(j.origin_x + 2 * xh) / tile_px - 0.5f;
    const float cy = std::max(0.0f, std::min(static_cast<float>(j.tile_spec_h - 1), gy));
    const float cx = std::max(0.0f, std::min(static_cast<float>(j.tile_spec_w - 1), gx));
    const int iy = std::max(0, std::min(j.tile_spec_h - 1, static_cast<int>(std::floor(cy))));
    const int ix = std::max(0, std::min(j.tile_spec_w - 1, static_cast<int>(std::floor(cx))));
    const float ty = cy - static_cast<float>(iy);
    const float tx = cx - static_cast<float>(ix);
    auto bsp_w = [](float t, float out_w[4]) noexcept {
        const float omt = 1.0f - t;
        const float t2 = t * t;
        const float t3 = t2 * t;
        out_w[0] = (omt * omt * omt) * (1.0f / 6.0f);
        out_w[1] = (3.0f * t3 - 6.0f * t2 + 4.0f) * (1.0f / 6.0f);
        out_w[2] = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) * (1.0f / 6.0f);
        out_w[3] = t3 * (1.0f / 6.0f);
    };
    float wy[4], wx[4];
    bsp_w(ty, wy);
    bsp_w(tx, wx);
    float g_mid = 0.0f, kxx = 0.0f, kxy = 0.0f, kyy = 0.0f;
    for (int dy = -1; dy <= 2; ++dy) {
        const int yy = std::max(0, std::min(j.tile_spec_h - 1, iy + dy));
        const float w_y = wy[dy + 1];
        for (int dx = -1; dx <= 2; ++dx) {
            const int xx = std::max(0, std::min(j.tile_spec_w - 1, ix + dx));
            const float w = w_y * wx[dx + 1];
            const float* node = j.tile_spec_map + (static_cast<size_t>(yy) * j.tile_spec_w + xx) * 8;
            g_mid += w * node[1];
            kxx += w * node[4];
            kxy += w * node[5];
            kyy += w * node[6];
        }
    }
    const float support = std::max(0.25f, std::min(1.5f, 0.45f + 0.35f * g_mid + 0.25f * (kxx + kyy)));
    const float sum_ic = std::max(2e-4f, axx + ayy);
    const float tr_ic = 0.5f * sum_ic;
    const float anis_loc = std::min(1.0f, std::hypot(axx - ayy, 2.0f * axy) / sum_ic);
    const float w_tile = 0.25f * (1.0f - 0.85f * anis_loc);
    const float w_fine = 1.0f - w_tile;
    axx = w_fine * axx * support + w_tile * tr_ic * std::max(0.15f, kxx);
    axy = w_fine * axy * support + w_tile * tr_ic * kxy;
    ayy = w_fine * ayy * support + w_tile * tr_ic * std::max(0.15f, kyy);
}

inline void gpu_stage5_gat_params(
    const Stage45GpuJob& j, double eta_eff, double& out_sig_eff_sq, double& out_g_eff
) {
    if (j.q_luma_var <= 0.0f) {
        out_g_eff = std::max(0.0, j.pg_gain * eta_eff);
        out_sig_eff_sq = std::max(1e-10, j.pg_sig0_sq * eta_eff);
        return;
    }
    const double eta_q = eta_eff + double(j.q_corr_gain) * std::max(0.0, 1.0 - eta_eff);
    const double i_op = std::max(0.04, std::min(0.45, j.global_mean_g >= 0.0 ? j.global_mean_g : 0.12));
    const double q_s0 = std::max(0.0, double(j.q_nlt_s0));
    const double q_s1 = std::max(0.0, double(j.q_nlt_s1) + double(j.q_nlt_s2) * std::min(0.75, 0.25 + 1.2 * i_op));
    out_sig_eff_sq = std::max(1e-10, j.pg_sig0_sq * eta_eff + eta_q * double(j.q_luma_var) * q_s0);
    out_g_eff = std::max(0.0, j.pg_gain * eta_eff + eta_q * double(j.q_luma_var) * q_s1);
}

inline float gpu_stage5_chroma_sigma(const Stage45GpuJob& j, double eta_eff, double mean_g) {
    const double wbmax = std::max(j.wb_r, j.wb_b);
    if (j.q_chroma_var <= 0.0f && j.q_luma_var <= 0.0f) {
        return j.chroma_denoise * float(std::sqrt((1.0 + wbmax * wbmax) * eta_eff * (j.pg_sig0_sq + j.pg_gain * mean_g)));
    }
    const double eta_q = eta_eff + double(j.q_corr_gain) * std::max(0.0, 1.0 - eta_eff);
    const double nlt2 = double(gpu_eval_nlt_sq(j.q_nlt_s0, j.q_nlt_s1, j.q_nlt_s2, float(mean_g)));
    const double v_pg = (1.0 + wbmax * wbmax) * eta_eff * (j.pg_sig0_sq + j.pg_gain * mean_g);
    const double v_q = eta_q * nlt2 * (double(j.q_luma_var) + wbmax * wbmax * double(j.q_chroma_var));
    return j.chroma_denoise * float(std::sqrt(std::max(0.0, v_pg + v_q)));
}

inline bool gpu_use_robust(const Stage45GpuJob& j, int n) {
    return !j.is_virtual_nd && (n > 0 || j.is_clean_plate);
}

inline float gpu_motion_weight(const Stage45GpuJob& j, float w) {
    return j.is_virtual_nd ? 1.0f : w;
}

inline float gpu_frame_weight(const Stage45GpuJob& j, int n) {
    return !j.is_virtual_nd && j.frame_lucky_w && size_t(n) < j.frame_lucky_w->size()
        ? (*j.frame_lucky_w)[n] : 1.0f;
}

inline void dct_dependency_roi(PC_DctWiener& pc, const Stage45GpuJob& j) {
    if (j.core_y1<=0 || j.core_x1<=0) return;
    // From final RGB back to denoised green: chroma propagation, RL, variance
    // smoothing and local assembly. Coarse Laplacian corrections add <=4
    // pixels per level; the pilot must cover every Wiener block plus rounding
    // to its 10-pixel workgroup.
    int halo=12+48+4;
    if(j.chroma_denoise>1e-4f) halo+=126+2+(j.glint_desat?8:0);
    if(j.psf_iters>0) halo+=j.psf_iters*make_olpf_psf_pc(1,1,0,j.psf_sigma,j.olpf_mode).radius+8;
    const int margin=8+(pc.is_wiener?0:18);
    pc.roi_y0=std::max(0,int(std::floor(double(2*(j.core_y0-halo))/pc.scale_mul))-margin);
    pc.roi_x0=std::max(0,int(std::floor(double(2*(j.core_x0-halo))/pc.scale_mul))-margin);
    pc.roi_y1=std::min(pc.H,int(std::ceil(double(2*(j.core_y1+halo))/pc.scale_mul))+margin);
    pc.roi_x1=std::min(pc.W,int(std::ceil(double(2*(j.core_x1+halo))/pc.scale_mul))+margin);
}

inline void gpu_eta_stats(Stage45GpuJob& j, const float* eta, int Ew, int Eh) {
    j.out_eta_sum = 0.0;
    j.out_eta_count = 0;
    const int y1 = j.core_y1 > 0 ? j.core_y1 : j.H1;
    const int x1 = j.core_x1 > 0 ? j.core_x1 : j.W1;
    for (int r = 0; r < Eh; ++r) {
        if (r * 4 < j.core_y0 || r * 4 >= y1) continue;
        for (int c = 0; c < Ew; ++c) {
            if (c * 4 < j.core_x0 || c * 4 >= x1) continue;
            const float e = eta[size_t(r) * Ew + c];
            if(j.eta_quarter_out) {
                const int width=((j.full_W1>0?j.full_W1:j.W1)+3)/4;
                j.eta_quarter_out[size_t((j.origin_y+r*4)/4)*width+(j.origin_x+c*4)/4]=e;
            }
            if (std::isfinite(e) && e >= 0.0f) {
                j.out_eta_sum += e;
                ++j.out_eta_count;
            }
        }
    }
}

template<class T>
inline void gpu_crop_into(std::vector<T>& dst, const T* src, int stride, int y, int x,
                          int h, int w, int channels = 1, int n_threads = 1) {
    dst.resize(size_t(h) * w * channels);
    const int nt = (h >= 256 && n_threads > 1) ? std::min(n_threads, 8) : 1;
    if (nt <= 1) {
        for (int r = 0; r < h; ++r)
            std::copy_n(src + (size_t(y + r) * stride + x) * channels,
                        size_t(w) * channels, dst.data() + size_t(r) * w * channels);
        return;
    }
    std::vector<heraw_sync::JoiningThread> workers;
    workers.reserve(nt);
    const int rows_per = (h + nt - 1) / nt;
    for (int t = 0; t < nt; ++t) {
        const int r0 = t * rows_per, r1 = std::min(h, r0 + rows_per);
        if (r0 >= r1) break;
        workers.emplace_back([=, &dst]() {
            for (int r = r0; r < r1; ++r)
                std::copy_n(src + (size_t(y + r) * stride + x) * channels,
                            size_t(w) * channels, dst.data() + size_t(r) * w * channels);
        });
    }
}

template<class T>
inline std::vector<T> gpu_crop(const T* src, int stride, int y, int x,
                               int h, int w, int channels = 1) {
    std::vector<T> dst;
    gpu_crop_into(dst, src, stride, y, x, h, w, channels, 1);
    return dst;
}

inline double compute_global_eta_direct(Stage45GpuJob& j) {
    const int H = j.H1, W = j.W1, Hh = H / 2, Wh = W / 2;
    const int Eh = (H + 3) / 4, Ew = (W + 3) / 4;
    const int nt = std::max(1, std::min(j.n_threads, Eh));
    std::vector<double> eta_sum(nt, 0.0);
    std::vector<int64_t> eta_count(nt, 0);
    auto bilerp_h = [Hh, Wh](const float* p, float y, float x) -> float {
        y = std::max(0.0f, std::min(float(Hh - 1), y));
        x = std::max(0.0f, std::min(float(Wh - 1), x));
        const int y0 = int(y), x0 = int(x);
        const int y1 = std::min(y0 + 1, Hh - 1), x1 = std::min(x0 + 1, Wh - 1);
        const float fy = y - float(y0), fx = x - float(x0);
        return (1.0f - fy) * ((1.0f - fx) * p[size_t(y0) * Wh + x0] + fx * p[size_t(y0) * Wh + x1]) +
               fy * ((1.0f - fx) * p[size_t(y1) * Wh + x0] + fx * p[size_t(y1) * Wh + x1]);
    };
    auto eval_rows = [&](int er0, int er1, int tid) {
        double l_sum = 0.0;
        int64_t l_cnt = 0;
        for (int er = er0; er < er1; ++er) {
            const int Y = er * 4;
            const float yh = 0.5f * Y + 0.125f;
            const int rhi = std::max(0, std::min(Hh - 1, int(yh + 0.5f)));
            for (int ec = 0; ec < Ew; ++ec) {
                const int X = ec * 4;
                const float xh = 0.5f * X + 0.125f;
                const int chi = std::max(0, std::min(Wh - 1, int(xh + 0.5f)));
                const size_t hi = size_t(rhi) * Wh + chi;
                const float axx = j.inv_cov[hi * 3 + 0];
                const float axy = j.inv_cov[hi * 3 + 1];
                const float ayy = j.inv_cov[hi * 3 + 2];
                const float anchor = j.g_guide[hi];
                const float a_pos = std::max(0.0f, anchor);
                const float threshold = 3.5f * std::sqrt(float(j.pg_sig0_sq) +
                    float(j.pg_gain) * a_pos +
                    j.q_luma_var * gpu_eval_nlt_sq(j.q_nlt_s0, j.q_nlt_s1, j.q_nlt_s2, a_pos)) + 0.008f;
                float ws = 0.0f, w2 = 0.0f;
                for (int n = 0; n < j.N; ++n) {
                    const float mw = gpu_motion_weight(j,
                        bilerp_h((*j.mw_half)[n].data(), yh, xh)) * gpu_frame_weight(j, n);
                    if (mw < 0.02f) continue;
                    const float dy = n ? 2.0f * bilerp_h((*j.flows_fy)[n].data(), yh, xh) : 0.0f;
                    const float dx = n ? 2.0f * bilerp_h((*j.flows_fx)[n].data(), yh, xh) : 0.0f;
                    const float yt = float(Y) + dy, xt = float(X) + dx;
                    if (yt < -0.5f || yt > float(H) - 0.5f || xt < -0.5f || xt > float(W) - 0.5f) continue;
                    const int rb = int(std::floor(yt)), cb = int(std::floor(xt));
                    float frame_ws = 0.0f, frame_w2 = 0.0f;
                    const float* fptr = (*j.frames_lin)[n].data();
                    const bool use_rob = gpu_use_robust(j, n);
                    for (int rr = rb - 1; rr <= rb + 2; ++rr) {
                        const int r = std::max(0, std::min(H - 1, rr));
                        for (int c = cb - 1; c <= cb + 2; ++c) {
                            if (c < 0 || c >= W || j.cfa6x6[r % 6][c % 6] != 1) continue;
                            float rob = mw;
                            if (use_rob) {
                                const float ad = std::fabs(fptr[size_t(r) * W + c] - anchor);
                                if (ad > threshold) { const float q = threshold / ad; rob *= q * q * q; }
                            }
                            const float yy = float(rr) - yt, xx = float(c) - xt;
                            const float d = axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy;
                            if (d > 6.5f) continue;
                            const float t_exp = -std::max(0.0f, std::min(40.0f, 0.5f * d));
                            const int k_exp = int(t_exp * std::numbers::log2e_v<float> - 0.5f);
                            const float kf_exp = float(k_exp);
                            const float r_exp = (t_exp - kf_exp * 0.693145751953125f) - kf_exp * 1.4286067653301870e-6f;
                            float p_exp = 0.00138888889f;
                            p_exp = p_exp * r_exp + 0.00833333333f;
                            p_exp = p_exp * r_exp + 0.0416666667f;
                            p_exp = p_exp * r_exp + 0.166666667f;
                            p_exp = p_exp * r_exp + 0.5f;
                            p_exp = p_exp * r_exp + 1.0f;
                            p_exp = p_exp * r_exp + 1.0f;
                            const uint32_t bits_exp = uint32_t(127 + k_exp) << 23;
                            const float w = rob * (p_exp * std::bit_cast<float>(bits_exp));
                            frame_ws += w;
                            frame_w2 += w * w;
                        }
                    }
                    ws += frame_ws;
                    w2 += frame_w2;
                }
                const float eta = ws > 1e-8f ? w2 / (ws * ws) : -1.0f;
                if (j.eta_quarter_out) j.eta_quarter_out[size_t(er) * Ew + ec] = eta;
                if (eta >= 0.0f) { l_sum += eta; ++l_cnt; }
            }
        }
        eta_sum[tid] = l_sum;
        eta_count[tid] = l_cnt;
    };
    if (nt <= 1) {
        eval_rows(0, Eh, 0);
    } else {
        std::vector<heraw_sync::JoiningThread> workers;
        workers.reserve(nt);
        const int rows_per = (Eh + nt - 1) / nt;
        for (int t = 0; t < nt; ++t) {
            const int r0 = t * rows_per, r1 = std::min(Eh, r0 + rows_per);
            if (r0 >= r1) break;
            workers.emplace_back(eval_rows, r0, r1, t);
        }
    }
    double tot_sum = 0.0;
    int64_t tot_cnt = 0;
    for (int t = 0; t < nt; ++t) { tot_sum += eta_sum[t]; tot_cnt += eta_count[t]; }
    j.out_eta_sum = tot_sum;
    j.out_eta_count = tot_cnt;
    return std::max(0.01, tot_sum / double(std::max<int64_t>(1, tot_cnt)));
}

inline double compute_global_eta_direct_v3(Stage45GpuJob& j) {
    const int H = j.H1, W = j.W1, Hh = H / 2, Wh = W / 2;
    const int Eh = (H + 3) / 4, Ew = (W + 3) / 4;
    const int out_scale = j.is_1x_mode ? 1 : 2;
    const float scale_g = (out_scale == 2) ? 1.0f : 0.72f;
    const float max_ha_blend = (out_scale == 1) ? 0.85f : 0.65f;
    const float sig0_ref = float(j.pg_sig0_sq);
    const float gain_ref = float(j.pg_gain);
    const float sat0 = j.frame_sat_levels ? j.frame_sat_levels[0] : 1.0e6f;
    const bool is_aeb = (j.frame_gains != nullptr || j.frame_read_vars != nullptr || j.frame_shot_coeffs != nullptr);
    bool is_bayer = !j.is_xtrans;
    if (is_bayer) {
        for (int r = 0; r < 6 && is_bayer; ++r) {
            for (int c = 0; c < 6; ++c) {
                if (j.cfa6x6[r][c] != j.cfa6x6[r & 1][c & 1]) { is_bayer = false; break; }
            }
        }
    }
    const int g_col0 = (j.cfa6x6[0][0] == 1) ? 0 : 1;
    const int g_col1 = (j.cfa6x6[1][0] == 1) ? 0 : 1;
    const bool h_ge2 = (Hh >= 2 && Wh >= 2);
    const float* guide = j.g_guide;

    std::vector<float> fw_arr(j.N), rv_arr(j.N), sh_arr(j.N), sat_lo_arr(j.N), sat_ispan_arr(j.N);
    std::vector<uint8_t> rob_arr(j.N), unit_mw_arr(j.N), has_flow_arr(j.N);
    bool any_rob = false;
    for (int n = 0; n < j.N; ++n) {
        fw_arr[n] = gpu_frame_weight(j, n);
        const bool rob = gpu_use_robust(j, n);
        rob_arr[n] = rob ? 1 : 0;
        if (rob) any_rob = true;
        rv_arr[n] = j.frame_read_vars ? j.frame_read_vars[n] : sig0_ref;
        sh_arr[n] = j.frame_shot_coeffs ? j.frame_shot_coeffs[n] : gain_ref;
        const float sat = j.frame_sat_levels ? j.frame_sat_levels[n] : 1.0e6f;
        const float slo = 0.82f * sat;
        sat_lo_arr[n] = slo;
        sat_ispan_arr[n] = 1.0f / std::max(1e-6f, 0.99f * sat - slo);
        uint8_t umw = 1;
        if (!j.is_virtual_nd && n < int(j.mw_half->size()) && !(*j.mw_half)[n].empty()) {
            for (float m : (*j.mw_half)[n]) { if (m != 1.0f) { umw = 0; break; } }
        }
        unit_mw_arr[n] = umw;
        uint8_t hfl = 0;
        if (n < int(j.flows_fy->size()) && n < int(j.flows_fx->size())) {
            const auto& fyn = (*j.flows_fy)[n];
            const auto& fxn = (*j.flows_fx)[n];
            for (size_t i = 0; i < fyn.size(); ++i) {
                if (fyn[i] != 0.0f || fxn[i] != 0.0f) { hfl = 1; break; }
            }
        }
        has_flow_arr[n] = hfl;
    }

    auto fast_exp_neg = [](float x) -> float {
        const float t = -std::max(0.0f, std::min(40.0f, x));
        const int k = int(t * std::numbers::log2e_v<float> - 0.5f);
        const float kf = float(k);
        const float r = (t - kf * 0.693145751953125f) - kf * 1.4286067653301870e-6f;
        float p = 0.00138888889f;
        p = p * r + 0.00833333333f;
        p = p * r + 0.0416666667f;
        p = p * r + 0.166666667f;
        p = p * r + 0.5f;
        p = p * r + 1.0f;
        p = p * r + 1.0f;
        return p * std::bit_cast<float>(uint32_t(127 + k) << 23);
    };
    auto sat_taper_f = [](float v, float lo, float ispan) -> float {
        const float t = (v - lo) * ispan;
        if (t >= 1.0f) return 1e-4f;
        return std::max(1e-4f, 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * t)));
    };
    auto bilerp_h = [Hh, Wh, h_ge2](const float* p, float y1, float x1) -> float {
        if (!h_ge2) return p[0];
        const float yc = std::max(0.0f, std::min(float(Hh - 1), 0.5f * y1 - 0.25f));
        const float xc = std::max(0.0f, std::min(float(Wh - 1), 0.5f * x1 - 0.25f));
        const int y0 = std::max(0, std::min(Hh - 2, int(yc)));
        const int x0 = std::max(0, std::min(Wh - 2, int(xc)));
        const float fy = std::max(0.0f, std::min(1.0f, yc - float(y0)));
        const float fx = std::max(0.0f, std::min(1.0f, xc - float(x0)));
        const size_t i0 = size_t(y0) * Wh + x0;
        const size_t i1 = i0 + Wh;
        return (1.0f - fy) * ((1.0f - fx) * p[i0] + fx * p[i0 + 1]) +
               fy * ((1.0f - fx) * p[i1] + fx * p[i1 + 1]);
    };

    const int nt = std::max(1, std::min(j.n_threads, Eh));
    std::vector<double> eta_sum(nt, 0.0);
    std::vector<int64_t> eta_count(nt, 0);

    auto eval_rows = [&](int er0, int er1, int tid) {
        double l_sum = 0.0;
        int64_t l_cnt = 0;
        for (int er = er0; er < er1; ++er) {
            const int Y1 = er * 4;
            const int r = std::max(0, std::min(Hh - 1, Y1 >> 1));
            const int rm = std::max(0, r - 1), rp = std::min(Hh - 1, r + 1);
            for (int ec = 0; ec < Ew; ++ec) {
                const int X1 = ec * 4;
                const int c = std::max(0, std::min(Wh - 1, X1 >> 1));
                const int cm = std::max(0, c - 1), cp = std::min(Wh - 1, c + 1);
                const size_t hi = size_t(r) * Wh + c;
                const float anchor = guide[hi];
                const float gx_g = guide[size_t(r) * Wh + cp] - guide[size_t(r) * Wh + cm];
                const float gy_g = guide[size_t(rp) * Wh + c] - guide[size_t(rm) * Wh + c];
                const float a_pos = std::max(0.0f, anchor);
                const float v_loc = std::max(1e-10f, 2.0f * (sig0_ref + gain_ref * a_pos));
                const float snr2 = (0.25f * (gx_g * gx_g + gy_g * gy_g)) / v_loc;
                const float gamma = snr2 / (1.0f + snr2);
                const float icx = j.inv_cov[hi * 3 + 0], icy = j.inv_cov[hi * 3 + 1], icz = j.inv_cov[hi * 3 + 2];
                const float iso = 0.5f * (icx + icz);
                const float axx = scale_g * (iso + gamma * (icx - iso));
                const float axy = scale_g * (gamma * icy);
                const float ayy = scale_g * (iso + gamma * (icz - iso));
                const float v_ref = sig0_ref + gain_ref * a_pos;
                const float nlt_sq = (j.q_luma_var > 0.0f) ? gpu_eval_nlt_sq(j.q_nlt_s0, j.q_nlt_s1, j.q_nlt_s2, a_pos) : 0.0f;
                const float thr_base = (!is_aeb && any_rob)
                    ? (3.5f * std::sqrt(std::max(0.0f, v_ref + j.q_luma_var * nlt_sq)) + 0.008f) : 0.0f;

                const float tr_a = axx + ayy, df_a = axx - ayy;
                const float disc_a = std::sqrt(std::max(0.0f, df_a * df_a + 4.0f * axy * axy));
                const float anis = disc_a / std::max(1e-6f, tr_a);
                const bool use_curv = (j.N >= 3) && !j.is_clean_plate && !j.is_virtual_nd && (anis > 0.25f);
                float nx_c = 1.0f, ny_c = 0.0f;
                if (use_curv) {
                    const float vx = (df_a >= 0.0f) ? (df_a + disc_a) : (2.0f * axy);
                    const float vy = (df_a >= 0.0f) ? (2.0f * axy) : (disc_a - df_a);
                    const float inv_vn = 1.0f / std::sqrt(std::max(1e-18f, vx * vx + vy * vy));
                    nx_c = vx * inv_vn; ny_c = vy * inv_vn;
                }

                float S0 = 0.0f, Sx = 0.0f, Sy = 0.0f, Sxx = 0.0f, Sxy = 0.0f, Syy = 0.0f;
                float Sqx = 0.0f, Sqy = 0.0f, Sqq = 0.0f, Sw2 = 0.0f;
                bool has_nat = false;
                const float y1_f = float(Y1), x1_f = float(X1);
                for (int n = 0; n < j.N; ++n) {
                    const float fw = fw_arr[n];
                    if (fw <= 0.0f) continue;
                    const float mw_raw = unit_mw_arr[n] ? 1.0f : gpu_motion_weight(j, bilerp_h((*j.mw_half)[n].data(), y1_f, x1_f));
                    const float mw = (j.is_virtual_nd ? 1.0f : mw_raw) * fw;
                    if (mw < 0.01f) continue;
                    const float syv = y1_f + (has_flow_arr[n] ? 2.0f * bilerp_h((*j.flows_fy)[n].data(), y1_f, x1_f) : 0.0f);
                    const float sxv = x1_f + (has_flow_arr[n] ? 2.0f * bilerp_h((*j.flows_fx)[n].data(), y1_f, x1_f) : 0.0f);
                    if (syv < -0.5f || syv > float(H) - 0.5f || sxv < -0.5f || sxv > float(W) - 0.5f) continue;

                    const float* fptr = (*j.frames_lin)[n].data();
                    float inv_var_rel = 1.0f, inv_wvar = 1.0f;
                    const bool use_rob = (rob_arr[n] != 0);
                    float thr_n = thr_base;
                    if (is_aeb && !j.is_virtual_nd) {
                        const float v_n = rv_arr[n] + sh_arr[n] * a_pos;
                        if (v_ref > 1e-12f && v_n > 1e-12f) {
                            inv_var_rel = std::max(0.02f, std::min(32.0f, v_ref / v_n));
                            inv_wvar = 1.0f / inv_var_rel;
                        }
                        if (use_rob) thr_n = 3.5f * std::sqrt(std::max(0.0f, v_n + j.q_luma_var * nlt_sq)) + 0.008f;
                    }
                    auto accum_pt = [&](float yy, float xx, float v, bool check_nat) -> bool {
                        const float st = (v > sat_lo_arr[n]) ? sat_taper_f(v, sat_lo_arr[n], sat_ispan_arr[n]) : 1.0f;
                        float rob = 1.0f;
                        if (use_rob && (anchor < 0.82f * sat0 || v < 0.82f * sat0)) {
                            const float ad = std::fabs(v - anchor);
                            if (ad > thr_n) { const float q = thr_n / ad; rob = q * q * q; }
                        }
                        const float d = axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy;
                        if (!check_nat && d > 6.8f) return false;
                        const float w = mw * inv_var_rel * st * rob * fast_exp_neg(0.5f * d);
                        if (w <= 1e-12f) return check_nat;
                        S0 += w; Sx += w * xx; Sy += w * yy;
                        Sxx += w * xx * xx; Sxy += w * xx * yy; Syy += w * yy * yy;
                        if (use_curv) {
                            const float un = nx_c * xx + ny_c * yy;
                            const float q = 0.5f * un * un;
                            Sqx += w * q * xx; Sqy += w * q * yy; Sqq += w * q * q;
                        }
                        Sw2 += w * w * inv_wvar;
                        if (check_nat && st > 0.1f && rob > 0.1f) has_nat = true;
                        return true;
                    };
                    if (!j.is_clean_plate && !j.is_virtual_nd) {
                        const int ryi = int(std::floor(syv + 0.5f)), rxi = int(std::floor(sxv + 0.5f));
                        if (ryi >= 0 && ryi < H && rxi >= 0 && rxi < W && j.cfa6x6[ryi % 6][rxi % 6] == 1) {
                            const float yy = float(ryi) - syv, xx = float(rxi) - sxv;
                            if (std::fabs(yy) < 0.02f && std::fabs(xx) < 0.02f) {
                                accum_pt(yy, xx, fptr[size_t(ryi) * W + rxi], true);
                                continue;
                            }
                        }
                    }
                    const int rb = int(std::floor(syv)), cb = int(std::floor(sxv));
                    const int r_lo = std::max(0, rb - 1), r_hi = std::min(H - 1, rb + 2);
                    const int c_lo = std::max(0, cb - 1), c_hi = std::min(W - 1, cb + 2);
                    for (int rr = r_lo; rr <= r_hi; ++rr) {
                        int cc_start = c_lo, cc_step = 1;
                        if (is_bayer) {
                            const int par = (rr & 1) ? g_col1 : g_col0;
                            if ((cc_start & 1) != par) ++cc_start;
                            cc_step = 2;
                        }
                        for (int cc = cc_start; cc <= c_hi; cc += cc_step) {
                            if (!is_bayer && j.cfa6x6[rr % 6][cc % 6] != 1) continue;
                            accum_pt(float(rr) - syv, float(cc) - sxv, fptr[size_t(rr) * W + cc], false);
                        }
                    }
                }
                float eta_px = 1.0f;
                if (S0 > 1e-8f) {
                    const float inv_s0 = 1.0f / S0;
                    const float eta_nw = std::max(1e-4f, std::min(1.0f, Sw2 * inv_s0 * inv_s0));
                    const float mx = Sx * inv_s0, my = Sy * inv_s0;
                    const float F00 = std::max(0.0f, Sxx - Sx * mx);
                    const float F01 = Sxy - Sx * my;
                    const float F11 = std::max(0.0f, Syy - Sy * my);
                    const float tr_f = (F00 + F11) * inv_s0;
                    const float df_f = (F00 - F11) * inv_s0;
                    const float s01 = (2.0f * F01) * inv_s0;
                    const float disc = std::sqrt(std::max(0.0f, df_f * df_f + s01 * s01));
                    const float lam_min = std::max(0.0f, 0.5f * (tr_f - disc));
                    const float phi_fisher = std::max(0.0f, std::min(1.0f, lam_min / 0.16f)) *
                                             std::max(0.0f, std::min(1.0f, S0 / 3.0f));
                    const float reg = 0.02f * S0;
                    const float F00_r = F00 + reg, F11_r = F11 + reg;
                    const float det_r = std::max(1e-12f, F00_r * F11_r - F01 * F01);
                    const float q_lev = std::max(0.0f, S0 * (F11_r * mx * mx - 2.0f * F01 * mx * my + F00_r * my * my) / det_r);
                    const float lev_cap = (q_lev > 0.5f) ? std::sqrt(0.5f / q_lev) : 1.0f;
                    const float alpha_ll = phi_fisher * lev_cap;
                    float extra_var = alpha_ll * alpha_ll * q_lev;
                    if (use_curv && S0 > 2.0f) {
                        const float Sq = 0.5f * (nx_c * nx_c * Sxx + 2.0f * nx_c * ny_c * Sxy + ny_c * ny_c * Syy);
                        const float mu_q = Sq * inv_s0;
                        const float cqx = Sqx - mu_q * Sx, cqy = Sqy - mu_q * Sy;
                        const float cqq = std::max(0.0f, Sqq - mu_q * Sq);
                        const float gx_q = (F11_r * cqx - F01 * cqy) / det_r;
                        const float gy_q = (F00_r * cqy - F01 * cqx) / det_r;
                        const float Dq = std::max(0.0f, cqq - (gx_q * cqx + gy_q * cqy));
                        const float Dq_r = Dq + 0.04f * S0;
                        const float q0_orth = -mu_q + gx_q * mx + gy_q * my;
                        const float q_lev_q = std::max(0.0f, S0 * q0_orth * q0_orth / Dq_r);
                        const float lev_cap_q = (q_lev_q > 0.35f) ? std::sqrt(0.35f / q_lev_q) : 1.0f;
                        const float gate_q = std::max(0.0f, std::min(1.0f, (anis - 0.25f) * 2.0f)) *
                                             std::max(0.0f, std::min(1.0f, Dq / (0.06f * S0)));
                        const float alpha_q = alpha_ll * gate_q * lev_cap_q;
                        extra_var += alpha_q * alpha_q * q_lev_q;
                    }
                    const float a_ha = (has_nat || j.is_clean_plate || j.is_virtual_nd) ? 0.0f
                        : std::max(0.0f, std::min(max_ha_blend, (1.5f - S0) / 1.1f));
                    eta_px = (1.0f - a_ha) * (1.0f - a_ha) * eta_nw * (1.0f + extra_var) + a_ha * a_ha * 0.55f;
                }
                if (j.eta_quarter_out) j.eta_quarter_out[size_t(er) * Ew + ec] = eta_px;
                l_sum += eta_px;
                ++l_cnt;
            }
        }
        eta_sum[tid] = l_sum;
        eta_count[tid] = l_cnt;
    };
    if (nt <= 1) {
        eval_rows(0, Eh, 0);
    } else {
        std::vector<heraw_sync::JoiningThread> workers;
        workers.reserve(nt);
        const int rows_per = (Eh + nt - 1) / nt;
        for (int t = 0; t < nt; ++t) {
            const int r0 = t * rows_per, r1 = std::min(Eh, r0 + rows_per);
            if (r0 >= r1) break;
            workers.emplace_back(eval_rows, r0, r1, t);
        }
    }
    double tot_sum = 0.0;
    int64_t tot_cnt = 0;
    for (int t = 0; t < nt; ++t) { tot_sum += eta_sum[t]; tot_cnt += eta_count[t]; }
    j.out_eta_sum = tot_sum;
    j.out_eta_count = tot_cnt;
    return std::max(0.005, tot_sum / double(std::max<int64_t>(1, tot_cnt)));
}

template<class Run>
bool run_stage45_streamed(Stage45GpuJob& job, Run run, bool use_direct_eta = false) {
    const auto plan_start=std::chrono::steady_clock::now();
    job.out_wall_seconds.fill(0); job.out_device_seconds.fill(0);
    job.out_has_device_timing=false;
    job.out_plan_seconds=job.out_crop_seconds=job.out_stitch_seconds=0;
    job.out_err.clear();
    job.out_tile_count = 0;
    job.out_peak_tile_pixels = 0;
    job.out_tile_diagnostics.clear();
    const int H = job.H1, W = job.W1;
    if (H < 16 || W < 16 || (H & 1) || (W & 1) || H > 100000 || W > 100000 ||
        job.N < 1 || !job.frames_lin || !job.flows_fy || !job.flows_fx || !job.mw_half ||
        !job.inv_cov || !job.g_guide || !job.c_anc_1x || !job.g0_ha_1x ||
        !job.snr_boost_half || !job.rl_mod_f16 || !job.rgb_2x_out ||
        job.psf_iters < 0 || job.psf_iters > 128 ||
        !std::isfinite(job.psf_sigma) || job.psf_sigma <= 0.0f) {
        job.out_err = "Invalid Stage45 dimensions, planes or PSF parameters";
        return false;
    }
    const size_t pixels = size_t(H) * W, half = size_t(H / 2) * (W / 2);
    if (!std::isfinite(job.pg_sig0_sq) || job.pg_sig0_sq < 0.0 ||
        !std::isfinite(job.pg_gain) || job.pg_gain < 0.0 ||
        !std::isfinite(job.wb_r) || job.wb_r <= 0.0f ||
        !std::isfinite(job.wb_b) || job.wb_b <= 0.0f ||
        !std::isfinite(job.denoise_strength) || !std::isfinite(job.chroma_denoise) ||
        job.tile_size_1x > 100000 ||
        (job.tile_size_1x <= 0 && pixels > size_t(std::numeric_limits<int>::max() / 4))) {
        job.out_err = "Invalid Stage45 radiometric parameters or full-image extent"; return false;
    }
    for (float k : {job.lca_r_k1, job.lca_r_k2, job.lca_b_k1, job.lca_b_k2})
        if (!std::isfinite(k)) { job.out_err = "Non-finite LCA"; return false; }
    if (job.frame_ois_shifts) for (float f : *job.frame_ois_shifts)
        if (!std::isfinite(f)) { job.out_err = "Non-finite OIS"; return false; }
    if (job.frame_lucky_w) for (float f : *job.frame_lucky_w)
        if (!std::isfinite(f) || f < 0.0f) { job.out_err = "Invalid lucky weight"; return false; }
    const std::vector<std::vector<float>>* planes[] =
        {job.frames_lin, job.flows_fy, job.flows_fx, job.mw_half};
    for (const auto* v : planes) {
        if (v->size() < size_t(job.N)) {
            job.out_err = "Stage45 frame count mismatch"; return false;
        }
    }
    double drift = 0.0;
    for (int n = 0; n < job.N; ++n) {
        if ((*job.frames_lin)[n].size() != pixels || (*job.flows_fy)[n].size() != half ||
            (*job.flows_fx)[n].size() != half || (*job.mw_half)[n].size() != half) {
            job.out_err = "Stage45 frame/flow extent mismatch"; return false;
        }
    }
    {
        const int f_nt = (half >= 65536 && job.n_threads > 1) ? std::min(job.n_threads, 8) : 1;
        if (f_nt <= 1) {
            for (int n = 0; n < job.N; ++n) {
                for (const auto* v : {job.flows_fy, job.flows_fx}) {
                    for (float f : (*v)[n]) {
                        if (!std::isfinite(f)) { job.out_err = "Non-finite Stage45 flow"; return false; }
                        drift = std::max(drift, 2.0 * std::fabs(double(f)));
                    }
                }
            }
        } else {
            std::vector<double> thr_drift(f_nt, 0.0);
            std::vector<uint8_t> thr_bad(f_nt, 0);
            {
                std::vector<heraw_sync::JoiningThread> workers;
                workers.reserve(f_nt);
                const size_t ch = (half + f_nt - 1) / f_nt;
                for (int t = 0; t < f_nt; ++t) {
                    const size_t i0 = size_t(t) * ch, i1 = std::min(half, i0 + ch);
                    if (i0 >= i1) break;
                    workers.emplace_back([&, t, i0, i1]() {
                        double loc_d = 0.0;
                        for (int n = 0; n < job.N; ++n) {
                            const float* pfy = (*job.flows_fy)[n].data();
                            const float* pfx = (*job.flows_fx)[n].data();
                            for (size_t i = i0; i < i1; ++i) {
                                const float vy = pfy[i], vx = pfx[i];
                                if (!std::isfinite(vy) || !std::isfinite(vx)) { thr_bad[t] = 1; return; }
                                loc_d = std::max(loc_d, 2.0 * std::max(std::fabs(double(vy)), std::fabs(double(vx))));
                            }
                        }
                        thr_drift[t] = loc_d;
                    });
                }
            }
            for (int t = 0; t < f_nt; ++t) {
                if (thr_bad[t]) { job.out_err = "Non-finite Stage45 flow"; return false; }
                drift = std::max(drift, thr_drift[t]);
            }
        }
    }
    for (const auto& row : job.cfa6x6) for (uint8_t c : row)
        if (c > 2) { job.out_err = "Invalid CFA channel"; return false; }
    // Global statistics use the caller's full-resolution green anchor so that
    // signal/edge thresholds are identical for full and streamed execution.
    Stage45GpuJob global = job;
    for (int r = 0; r < 6; ++r) for (int c = 0; c < 6; ++c) {
        const uint8_t canonical = (r & 1) ? ((c & 1) ? 2 : 1) : ((c & 1) ? 1 : 0);
        if (job.cfa6x6[r][c] != canonical) global.is_xtrans = true;
    }
    const bool external_stripe = (job.full_H1 > 0 && job.full_W1 > 0);
    if (!external_stripe) {
        global.full_H1 = H; global.full_W1 = W;
        global.origin_y = 0; global.origin_x = 0;
        global.fcomp_origin_y = 0;
        global.global_eta = -1.0;
    } else if (global.global_eta < 0.0) {
        global.global_eta = -1.0;
    }
    global.keep_frames_lin = true;
    if (!external_stripe || global.global_mean_g < 0.0) {
        double mean = 0.0;
        for (size_t i = 0; i < pixels; i += 16) mean += std::max(0.0f, job.g0_ha_1x[i]);
        global.global_mean_g = mean / double((pixels + 15) / 16);
    }
    if (!external_stripe || global.global_edge_p80 < 0.0f) {
        std::vector<float> edges;
        for (int r = 0; r < H; r += 8) for (int c = 0; c + 2 < W; c += 8)
            edges.push_back(std::fabs(job.g0_ha_1x[size_t(r) * W + c + 2] -
                                      job.g0_ha_1x[size_t(r) * W + c]));
        global.global_edge_p80 = quick_pct_gpu_helper(edges, 0.80f);
    }
    auto finish = [&]() {
        if (!job.keep_frames_lin)
            for (auto& frame : *job.frames_lin) std::vector<float>().swap(frame);
        return true;
    };
    const int first_pass = use_direct_eta ? 1 : 0;
    try {
        double direct_eta_dt = 0.0;
        if (first_pass == 1 && global.global_eta < 0.0) {
            const auto eta_t0 = std::chrono::steady_clock::now();
            global.global_eta = job.math_v3_unified ? compute_global_eta_direct_v3(global)
                                                    : compute_global_eta_direct(global);
            global.eta_quarter_out = nullptr;
            direct_eta_dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - eta_t0).count();
        }
        if (job.math_v3_unified && !job.has_global_g_range) {
            float mn_f = (*job.frames_lin)[0][0], mx_f = mn_f;
            for (int n = 0; n < job.N; ++n) {
                const auto& fr = (*job.frames_lin)[n];
                for (size_t i = 0; i < fr.size(); i += 16) {
                    const float v = fr[i];
                    mn_f = std::min(mn_f, v);
                    mx_f = std::max(mx_f, v);
                }
            }
            for (size_t i = 0; i < pixels; i += 16) {
                const float v = job.g0_ha_1x[i];
                mn_f = std::min(mn_f, v);
                mx_f = std::max(mx_f, v);
            }
            global.global_min_g = mn_f;
            global.global_max_g = mx_f;
            global.has_global_g_range = true;
        }
        if (job.tile_size_1x <= 0 ||
            (H <= job.tile_size_1x && W <= job.tile_size_1x && pixels <= job.max_tile_pixels)) {
            if (!run(global)) { job.out_err = global.out_err; return false; }
            job.out_eta_eff = global.out_eta_eff;
            job.out_resid_noise_2x = global.out_resid_noise_2x;
            job.out_device_label = global.out_device_label;
            job.out_peak_tile_pixels = pixels;
            job.out_tile_count = 1;
            job.out_wall_seconds=global.out_wall_seconds;
            job.out_wall_seconds[1] += direct_eta_dt;
            job.out_device_seconds=global.out_device_seconds;
            job.out_has_device_timing=global.out_has_device_timing;
            return finish();
        }
        // Independent DCTs read the original pyramid: use the largest octave
        // support, plus pyramid interpolation and half-grid variance smoothing;
        // then six 5-tap a-trous levels (2 * sum(1,2,...,32)), median/glint;
        // finally two PSF convolutions per RL iteration at 2x resolution.
        const int psf_radius = make_olpf_psf_pc(1, 1, 0, job.psf_sigma, job.olpf_mode).radius;
        int halo = 12;
        if (job.math_v3_unified) {
            halo = 16;
            if (job.denoise_strength > 1e-4f) halo += 96;
            if (job.chroma_denoise > 1e-4f) halo += 68;
            if (!job.defer_optical_psf && job.psf_iters > 0) halo += job.psf_iters * psf_radius + 8;
        } else {
            if (job.denoise_strength > 1e-4f) halo += 7 * 2 * 16 / 2 + 32 + 48 + 4;
            if (job.chroma_denoise > 1e-4f) halo += 126 + 2 + (job.glint_desat ? 8 : 0);
            if (job.psf_iters > 0) halo += job.psf_iters * psf_radius + 8;
        }
        float optics = 0.0f;
        optics = 0.5f * std::max(H, W) *
            std::max(std::fabs(job.lca_r_k1) + 2.0f * std::fabs(job.lca_r_k2),
                     std::fabs(job.lca_b_k1) + 2.0f * std::fabs(job.lca_b_k2));
        float ois = 0.0f;
        if (job.frame_ois_shifts) for (float f : *job.frame_ois_shifts) {
            ois = std::max(ois, std::fabs(f));
        }
        // 80 input pixels align all five DCT 10-pixel workgroup lattices,
        // half-grid guides, eta samples and pyramid downsampling.
        const int core = std::max(80, (job.tile_size_1x / 80) * 80);
        struct Tile { int y, x, h, w, y0, x0, y1, x1; };
        std::vector<Tile> tiles;
        int min_core = core, peak_w = 0, peak_h = 0, splits = 0;
        size_t masked_cells = 0, outside_cells = 0, refined_cells = 0;
        const double optical_margin = double(optics) + ois;
        const int Wh = W / 2, Hh = H / 2;
        const int n_br = (Hh + 15) / 16, n_bc = (Wh + 15) / 16;
        struct RoiBlock16 {
            double lo_y = 0.0, lo_x = 0.0, hi_y = 0.0, hi_x = 0.0;
            size_t masked = 0, outside = 0;
            uint8_t has_active = 0, has_refine = 0;
        };
        std::vector<RoiBlock16> roi_blocks(size_t(n_br) * n_bc);
        if (job.N > 1) {
            const int b_nt = (n_br >= 8 && job.n_threads > 1) ? std::min(job.n_threads, 8) : 1;
            auto build_br_range = [&](int br_a, int br_b) {
                for (int br = br_a; br < br_b; ++br) {
                    const int r0 = br * 16, r1 = std::min(Hh - 1, r0 + 15);
                    for (int bc = 0; bc < n_bc; ++bc) {
                        const int c0 = bc * 16, c1 = std::min(Wh - 1, c0 + 15);
                        RoiBlock16 blk;
                        blk.lo_y = blk.lo_x = std::numeric_limits<double>::infinity();
                        blk.hi_y = blk.hi_x = -std::numeric_limits<double>::infinity();
                        for (int n = 1; n < job.N; ++n) {
                            const float fw = gpu_frame_weight(job, n);
                            if (fw <= 0.0f) continue;
                            const float* fy = (*job.flows_fy)[n].data();
                            const float* fx = (*job.flows_fx)[n].data();
                            const float* mw = (*job.mw_half)[n].data();
                            for (int r = r0; r <= r1; ++r) {
                                const int rn = std::min(r + 1, Hh - 1);
                                for (int c = c0; c <= c1; ++c) {
                                    const int cn = std::min(c + 1, Wh - 1);
                                    const size_t i00 = size_t(r) * Wh + c, i01 = size_t(r) * Wh + cn;
                                    const size_t i10 = size_t(rn) * Wh + c, i11 = size_t(rn) * Wh + cn;
                                    if (!job.is_virtual_nd &&
                                        std::max({mw[i00], mw[i01], mw[i10], mw[i11]}) * fw < 0.02f) {
                                        ++blk.masked;
                                        continue;
                                    }
                                    const double sy0 = std::min({2.0*r + 2.0*fy[i00], 2.0*r + 2.0*fy[i01],
                                        2.0*(r+1) + 2.0*fy[i10], 2.0*(r+1) + 2.0*fy[i11]}) - 0.25 - optical_margin;
                                    const double sx0 = std::min({2.0*c + 2.0*fx[i00], 2.0*(c+1) + 2.0*fx[i01],
                                        2.0*c + 2.0*fx[i10], 2.0*(c+1) + 2.0*fx[i11]}) - 0.25 - optical_margin;
                                    const double sy1 = std::max({2.0*r + 2.0*fy[i00], 2.0*r + 2.0*fy[i01],
                                        2.0*(r+1) + 2.0*fy[i10], 2.0*(r+1) + 2.0*fy[i11]}) + optical_margin;
                                    const double sx1 = std::max({2.0*c + 2.0*fx[i00], 2.0*(c+1) + 2.0*fx[i01],
                                        2.0*c + 2.0*fx[i10], 2.0*(c+1) + 2.0*fx[i11]}) + optical_margin;
                                    if (sy1 < -0.5 || sy0 > H - 0.5 || sx1 < -0.5 || sx0 > W - 0.5) {
                                        ++blk.outside;
                                        continue;
                                    }
                                    if (sy1 - sy0 > 80.0 + 2*optical_margin ||
                                        sx1 - sx0 > 80.0 + 2*optical_margin) {
                                        blk.has_refine = 1;
                                        continue;
                                    }
                                    blk.has_active = 1;
                                    blk.lo_y = std::min(blk.lo_y, std::max(-0.5, sy0) - 3);
                                    blk.lo_x = std::min(blk.lo_x, std::max(-0.5, sx0) - 3);
                                    blk.hi_y = std::max(blk.hi_y, std::min(H - 0.5, sy1) + 4);
                                    blk.hi_x = std::max(blk.hi_x, std::min(W - 0.5, sx1) + 4);
                                }
                            }
                        }
                        roi_blocks[size_t(br) * n_bc + bc] = blk;
                    }
                }
            };
            if (b_nt <= 1) {
                build_br_range(0, n_br);
            } else {
                std::vector<heraw_sync::JoiningThread> workers;
                workers.reserve(b_nt);
                const int rp = (n_br + b_nt - 1) / b_nt;
                for (int ti = 0; ti < b_nt; ++ti) {
                    const int a = ti * rp, b = std::min(n_br, a + rp);
                    if (a >= b) break;
                    workers.emplace_back(build_br_range, a, b);
                }
            }
        }
        // Source and reference share one ROI. Bound the union directionally:
        // all Stage5-dependent reference pixels, plus contributing Stage4 source
        // positions. Bilinear flow/mask extrema lie at the four cell corners;
        // a zero mask vertex can still affect interpolation in an active cell.
        auto bound_roi = [&](Tile& t, int dependency_halo) {
            const int by0 = std::max(0, t.y - dependency_halo), bx0 = std::max(0, t.x - dependency_halo);
            const int by1 = std::min(H, t.y + t.h + dependency_halo), bx1 = std::min(W, t.x + t.w + dependency_halo);
            double lo_y = by0 - optical_margin - 3, lo_x = bx0 - optical_margin - 3;
            double hi_y = by1 + optical_margin + 3, hi_x = bx1 + optical_margin + 3;
            if (job.N > 1 && by1 > by0 && bx1 > bx0) {
                const int r_beg = by0 / 2, r_end = (by1 - 1) / 2;
                const int c_beg = bx0 / 2, c_end = (bx1 - 1) / 2;
                const int br_beg = r_beg / 16, br_end = r_end / 16;
                const int bc_beg = c_beg / 16, bc_end = c_end / 16;
                for (int br = br_beg; br <= br_end; ++br) {
                    const int br0 = br * 16, br1 = std::min(Hh - 1, br0 + 15);
                    for (int bc = bc_beg; bc <= bc_end; ++bc) {
                        const int bc0 = bc * 16, bc1 = std::min(Wh - 1, bc0 + 15);
                        const auto& blk = roi_blocks[size_t(br) * n_bc + bc];
                        if (!blk.has_refine && br0 >= r_beg && br1 <= r_end && bc0 >= c_beg && bc1 <= c_end) {
                            masked_cells += blk.masked;
                            outside_cells += blk.outside;
                            if (blk.has_active) {
                                lo_y = std::min(lo_y, blk.lo_y);
                                lo_x = std::min(lo_x, blk.lo_x);
                                hi_y = std::max(hi_y, blk.hi_y);
                                hi_x = std::max(hi_x, blk.hi_x);
                            }
                            continue;
                        }
                        const int rr0 = std::max(br0, r_beg), rr1 = std::min(br1, r_end);
                        const int cc0 = std::max(bc0, c_beg), cc1 = std::min(bc1, c_end);
                        for (int n = 1; n < job.N; ++n) {
                            const float fw = gpu_frame_weight(job, n);
                            if (fw <= 0.0f) continue;
                            const auto& fy = (*job.flows_fy)[n];
                            const auto& fx = (*job.flows_fx)[n];
                            const auto& mw = (*job.mw_half)[n];
                            for (int r = rr0; r <= rr1; ++r)
                                for (int c = cc0; c <= cc1; ++c) {
                                    const size_t i00 = size_t(r) * Wh + c;
                                    const size_t i01 = size_t(r) * Wh + std::min(c + 1, Wh - 1);
                                    const size_t i10 = size_t(std::min(r + 1, Hh - 1)) * Wh + c;
                                    const size_t i11 = size_t(std::min(r + 1, Hh - 1)) * Wh + std::min(c + 1, Wh - 1);
                                    if (!job.is_virtual_nd &&
                                        std::max({mw[i00], mw[i01], mw[i10], mw[i11]}) * fw < 0.02f) {
                                        ++masked_cells;
                                        continue;
                                    }
                                    double sy0 = std::min({2.0*r + 2.0*fy[i00], 2.0*r + 2.0*fy[i01],
                                        2.0*(r+1) + 2.0*fy[i10], 2.0*(r+1) + 2.0*fy[i11]}) - 0.25 - optical_margin;
                                    double sx0 = std::min({2.0*c + 2.0*fx[i00], 2.0*(c+1) + 2.0*fx[i01],
                                        2.0*c + 2.0*fx[i10], 2.0*(c+1) + 2.0*fx[i11]}) - 0.25 - optical_margin;
                                    double sy1 = std::max({2.0*r + 2.0*fy[i00], 2.0*r + 2.0*fy[i01],
                                        2.0*(r+1) + 2.0*fy[i10], 2.0*(r+1) + 2.0*fy[i11]}) + optical_margin;
                                    double sx1 = std::max({2.0*c + 2.0*fx[i00], 2.0*(c+1) + 2.0*fx[i01],
                                        2.0*c + 2.0*fx[i10], 2.0*(c+1) + 2.0*fx[i11]}) + optical_margin;
                                    if (sy1 < -0.5 || sy0 > H - 0.5 || sx1 < -0.5 || sx0 > W - 0.5) {
                                        ++outside_cells;
                                        continue;
                                    }
                                    if (sy1 - sy0 > 80.0 + 2*optical_margin ||
                                        sx1 - sx0 > 80.0 + 2*optical_margin) {
                                        ++refined_cells;
                                        auto sample = [&](const std::vector<float>& v, float yy, float xx) {
                                            const double a = double(v[i00])*(1.0-xx) + double(v[i01])*xx;
                                            const double b = double(v[i10])*(1.0-xx) + double(v[i11])*xx;
                                            return a*(1.0-yy) + b*yy;
                                        };
                                        const double max_flow = std::max({
                                            std::fabs(double(fy[i00])), std::fabs(double(fy[i01])),
                                            std::fabs(double(fy[i10])), std::fabs(double(fy[i11])),
                                            std::fabs(double(fx[i00])), std::fabs(double(fx[i01])),
                                            std::fabs(double(fx[i10])), std::fabs(double(fx[i11]))});
                                        const double margin = optical_margin + 0.001 +
                                            16.0*std::numeric_limits<float>::epsilon()*(std::max(H,W)+max_flow);
                                        sy0 = sx0 = std::numeric_limits<double>::infinity();
                                        sy1 = sx1 = -std::numeric_limits<double>::infinity();
                                        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                                            for (int ch = 0; ch < 3; ++ch) {
                                                const int Y = 2*r+dy, X = 2*c+dx;
                                                if (Y < by0 || Y >= by1 || X < bx0 || X >= bx1) continue;
                                                const float frac = ch == 0 ? 0.125f : (ch == 2 ? 0.25f : 0.0f);
                                                const float ty = 0.5f*dy+frac, tx = 0.5f*dx+frac;
                                                if (!job.is_virtual_nd && sample(mw,ty,tx)*fw < 0.02-1e-6) continue;
                                                const double offset = ch == 2 ? 0.5 : 0.0;
                                                const double sy = Y+offset+2.0*sample(fy,ty,tx);
                                                const double sx = X+offset+2.0*sample(fx,ty,tx);
                                                if (sy+margin < -0.5 || sy-margin > H-0.5 ||
                                                    sx+margin < -0.5 || sx-margin > W-0.5) continue;
                                                sy0=std::min(sy0,sy-margin); sy1=std::max(sy1,sy+margin);
                                                sx0=std::min(sx0,sx-margin); sx1=std::max(sx1,sx+margin);
                                            }
                                        if (sy0 > sy1) { ++outside_cells; continue; }
                                    }
                                    lo_y = std::min(lo_y, std::max(-0.5, sy0) - 3);
                                    lo_x = std::min(lo_x, std::max(-0.5, sx0) - 3);
                                    hi_y = std::max(hi_y, std::min(H - 0.5, sy1) + 4);
                                    hi_x = std::max(hi_x, std::min(W - 0.5, sx1) + 4);
                                }
                        }
                    }
                }
            }
            t.y0 = int(std::max(0.0, lo_y)) / 80 * 80;
            t.x0 = int(std::max(0.0, lo_x)) / 80 * 80;
            t.y1 = std::min(H, (int(std::ceil(std::min(double(H), hi_y))) + 79) / 80 * 80);
            t.x1 = std::min(W, (int(std::ceil(std::min(double(W), hi_x))) + 79) / 80 * 80);
        };
        std::function<bool(const Tile&)> plan_tile = [&](const Tile& requested) {
            Tile t=requested;
            bound_roi(t, halo);
            size_t area = size_t(t.y1 - t.y0) * (t.x1 - t.x0);
            if (area > job.max_tile_pixels || area > size_t(std::numeric_limits<int>::max() / 4)) {
                const bool split_y = t.h > 80 && (t.h >= t.w || t.w <= 80);
                if (split_y || t.w > 80) {
                    ++splits;
                    const int len = split_y ? t.h : t.w;
                    const int split = std::max(80, (len / 2) / 80 * 80);
                    Tile a = t, b = t;
                    if (split_y) { a.h = split; b.y += split; b.h -= split; }
                    else { a.w = split; b.x += split; b.w -= split; }
                    return plan_tile(a) && plan_tile(b);
                }
                job.out_err = "Stage45 halo/flow exceeds tile memory budget: image=" +
                    std::to_string(W) + "x" + std::to_string(H) +
                    " core=" + std::to_string(t.w) + "x" + std::to_string(t.h) +
                    " spatial_halo=" + std::to_string(halo) +
                    " raw_drift=" + std::to_string(drift) + " optics=" + std::to_string(optics) +
                    " ois=" + std::to_string(ois) + " roi=" + std::to_string(t.x1 - t.x0) +
                    "x" + std::to_string(t.y1 - t.y0) + " pixels=" + std::to_string(area) +
                    " budget=" + std::to_string(job.max_tile_pixels);
                return false;
            }
            if (area > job.out_peak_tile_pixels) {
                job.out_peak_tile_pixels = area; peak_w = t.x1 - t.x0; peak_h = t.y1 - t.y0;
            }
            min_core = std::min(min_core, std::min(t.h, t.w));
            tiles.push_back(t);
            return true;
        };
        for (int y = 0; y < H; y += core) for (int x = 0; x < W; x += core) {
            if (!plan_tile(Tile{y, x, std::min(core, H - y), std::min(core, W - x), 0, 0, 0, 0}))
                return false;
        }
        const std::string tile_summary = "tiles=" + std::to_string(tiles.size()) + " core=" +
            std::to_string(min_core) + ".." + std::to_string(core) + " halo=" + std::to_string(halo) +
            " raw-drift=" + std::to_string(drift) + " peak=" + std::to_string(peak_w) + "x" +
            std::to_string(peak_h) + " splits=" + std::to_string(splits);
        job.out_tile_diagnostics = tile_summary + " masked=" + std::to_string(masked_cells) +
            " outside=" + std::to_string(outside_cells) + " refined=" + std::to_string(refined_cells);
        // First pass obtains exact global green effective variance over disjoint
        job.out_plan_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-plan_start).count();
        // cores. When use_direct_eta is enabled, exact (4r,4c) variance sites are
        // evaluated directly on the full input without cropping or GPU dispatch.
        double eta_sum = 0.0;
        int64_t eta_count = 0;
        double dt_s4 = direct_eta_dt;
        size_t pass_pixels[2] = {};
        if (first_pass == 1) job.out_wall_seconds[1] += direct_eta_dt;
        const int out_s = job.is_1x_mode ? 1 : 2;
        std::vector<std::vector<float>> frames(job.N), fy(job.N), fx(job.N), mw(job.N);
        std::vector<float> cov, gg, boost, ca, ha, rgb;
        std::vector<uint32_t> rl;
        if (!tiles.empty()) {
            const size_t max_1x = job.out_peak_tile_pixels;
            const size_t max_h = max_1x / 4;
            for (int n = 0; n < job.N; ++n) {
                frames[n].reserve(max_1x);
                fy[n].reserve(max_h); fx[n].reserve(max_h); mw[n].reserve(max_h);
            }
            cov.reserve(max_h * 3); gg.reserve(max_h); boost.reserve(max_h);
            rl.reserve(max_h); ca.reserve(max_1x); ha.reserve(max_1x);
            if (!job.can_direct_stitch) rgb.reserve(max_1x * size_t(3 * out_s * out_s));
        }
        for (int pass = first_pass; pass < 2; ++pass) {
            if (pass == 1 && first_pass == 0) {
                global.global_eta = std::max(job.math_v3_unified ? 0.005 : 0.01,
                    eta_sum / double(std::max<int64_t>(1, eta_count)));
                global.eta_quarter_out = nullptr;
            }
            for (const Tile& planned : tiles) {
                const auto crop_start=std::chrono::steady_clock::now();
                Tile t = planned;
                // The statistics pass stops after green fusion. Its sources
                // need the fusion support, not the later denoise/chroma/RL halo.
                if (pass == 0) bound_roi(t, 12);
                pass_pixels[pass] += size_t(t.y1-t.y0)*(t.x1-t.x0);
                Stage45GpuJob sub = global;
                sub.arena_peak_1x_pixels = job.out_peak_tile_pixels;
                sub.H1 = t.y1 - t.y0; sub.W1 = t.x1 - t.x0;
                sub.origin_y = global.origin_y + t.y0; sub.origin_x = global.origin_x + t.x0;
                sub.eta_only = pass == 0;
                sub.core_y0 = t.y - t.y0; sub.core_x0 = t.x - t.x0;
                sub.core_y1 = sub.core_y0 + t.h; sub.core_x1 = sub.core_x0 + t.w;
                sub.on_stage4_done = [&](double, double dt, const std::string&) { dt_s4 += dt; };
                for (int r = 0; r < 6; ++r) for (int c = 0; c < 6; ++c)
                    sub.cfa6x6[r][c] = job.cfa6x6[(r + t.y0) % 6][(c + t.x0) % 6];
                const int crop_nt = std::max(1, job.n_threads);
                for (int n = 0; n < job.N; ++n) {
                    gpu_crop_into(frames[n], (*job.frames_lin)[n].data(), W, t.y0, t.x0, sub.H1, sub.W1, 1, crop_nt);
                    gpu_crop_into(fy[n], (*job.flows_fy)[n].data(), W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 1, crop_nt);
                    gpu_crop_into(fx[n], (*job.flows_fx)[n].data(), W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 1, crop_nt);
                    gpu_crop_into(mw[n], (*job.mw_half)[n].data(), W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 1, crop_nt);
                }
                gpu_crop_into(cov, job.inv_cov, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 3, crop_nt);
                gpu_crop_into(gg, job.g_guide, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 1, crop_nt);
                gpu_crop_into(boost, job.snr_boost_half, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 1, crop_nt);
                gpu_crop_into(rl, job.rl_mod_f16, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 1, crop_nt);
                gpu_crop_into(ca, job.c_anc_1x, W, t.y0, t.x0, sub.H1, sub.W1, 1, crop_nt);
                gpu_crop_into(ha, job.g0_ha_1x, W, t.y0, t.x0, sub.H1, sub.W1, 1, crop_nt);
                sub.frames_lin = &frames; sub.flows_fy = &fy; sub.flows_fx = &fx; sub.mw_half = &mw;
                sub.inv_cov = cov.data(); sub.g_guide = gg.data(); sub.snr_boost_half = boost.data();
                sub.rl_mod_f16 = rl.data(); sub.c_anc_1x = ca.data(); sub.g0_ha_1x = ha.data();
                if (pass == 1 && job.can_direct_stitch) {
                    sub.direct_stitch_rgb2x = job.rgb_2x_out;
                    sub.direct_w2_stride = W * out_s;
                    sub.direct_y2 = t.y * out_s;
                    sub.direct_x2 = t.x * out_s;
                    sub.rgb_2x_out = nullptr;
                    sub.direct_stitch_fcomp = job.fisher_comp_out;
                    sub.fisher_comp_out = nullptr;
                } else {
                    rgb.resize(pass == 1 ? size_t(sub.H1) * sub.W1 * size_t(3 * out_s * out_s) : 0);
                    sub.rgb_2x_out = rgb.data();
                }
                job.out_crop_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-crop_start).count();
                if (!run(sub)) { job.out_err = sub.out_err; return false; }
                for(size_t i=0;i<job.out_wall_seconds.size();++i) {
                    job.out_wall_seconds[i]+=sub.out_wall_seconds[i];
                    job.out_device_seconds[i]+=sub.out_device_seconds[i];
                }
                job.out_has_device_timing=job.out_has_device_timing||sub.out_has_device_timing;
                const auto stitch_start=std::chrono::steady_clock::now();
                job.out_device_label = sub.out_device_label;
                if (pass == 0) {
                    eta_sum += sub.out_eta_sum; eta_count += sub.out_eta_count;
                } else if (sub.direct_stitch_rgb2x) {
                    job.out_resid_noise_2x = sub.out_resid_noise_2x;
                } else {
                    const int s_rows = t.h * out_s;
                    const int s_nt = (s_rows >= 256 && job.n_threads > 1) ? std::min(job.n_threads, 8) : 1;
                    if (s_nt <= 1) {
                        for (int r = 0; r < s_rows; ++r)
                            std::copy_n(rgb.data() + (size_t(sub.core_y0 * out_s + r) * sub.W1 * out_s + sub.core_x0 * out_s) * 3,
                                        size_t(t.w) * size_t(3 * out_s),
                                        job.rgb_2x_out + (size_t(t.y * out_s + r) * W * out_s + t.x * out_s) * 3);
                    } else {
                        std::vector<heraw_sync::JoiningThread> workers;
                        workers.reserve(s_nt);
                        const int rp = (s_rows + s_nt - 1) / s_nt;
                        for (int ti = 0; ti < s_nt; ++ti) {
                            const int r0 = ti * rp, r1 = std::min(s_rows, r0 + rp);
                            if (r0 >= r1) break;
                            workers.emplace_back([=, &rgb, &job]() {
                                for (int r = r0; r < r1; ++r)
                                    std::copy_n(rgb.data() + (size_t(sub.core_y0 * out_s + r) * sub.W1 * out_s + sub.core_x0 * out_s) * 3,
                                                size_t(t.w) * size_t(3 * out_s),
                                                job.rgb_2x_out + (size_t(t.y * out_s + r) * W * out_s + t.x * out_s) * 3);
                            });
                        }
                    }
                    job.out_resid_noise_2x = sub.out_resid_noise_2x;
                }
                job.out_stitch_seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-stitch_start).count();
            }
        }
        job.out_eta_eff = global.global_eta;
        job.out_tile_diagnostics += " eta-pixels=" + std::to_string(pass_pixels[0]) +
            " render-pixels=" + std::to_string(pass_pixels[1]);
        job.out_tile_count = int(tiles.size());
        if (job.on_stage4_done) job.on_stage4_done(job.out_eta_eff, dt_s4,
            job.out_device_label + "; " + tile_summary);
        return finish();
    } catch (const std::bad_alloc&) {
        job.out_err = "Stage45 host tile allocation failed";
        return false;
    }
}

namespace v3_host {

inline constexpr int kLutSize = 24900;
inline constexpr int kOffVstFwd = 0;          // 16 * 256 = 4096
inline constexpr int kOffVstInv = 4096;       // 16 * 256 = 4096
inline constexpr int kOffDsig2Du = 8192;      // 16 * 256 = 4096
inline constexpr int kOffBandMeta = 12288;    // 16 * 8 = 128
inline constexpr int kOffPsi = 12416;         // 4096
inline constexpr int kOffGFloor = 16512;      // 4096
inline constexpr int kOffOctF = 20608;        // 4096
inline constexpr int kOffTwCos = 24704;       // 64
inline constexpr int kOffTwSin = 24768;       // 64
inline constexpr int kOffSinWin = 24832;      // 64
inline constexpr int kOffOctCnt = 24896;      // 4

struct StaticTables64 {
    float psi_1x[4096]{};
    float psi_2x[4096]{};
    float oct_f[4096]{};
    float oct_cnt[4]{};
    float tw_cos[64]{};
    float tw_sin[64]{};
    float sin_win[64]{};
    StaticTables64() {
        const double two_pi = 2.0 * std::numbers::pi_v<double>;
        for (int k = 0; k < 64; ++k) {
            tw_cos[k] = float(std::cos(two_pi * double(k) / 64.0));
            tw_sin[k] = float(std::sin(two_pi * double(k) / 64.0));
            sin_win[k] = float(std::sin(std::numbers::pi_v<double> * (double(k) + 0.5) / 64.0));
        }
        double raw1[4096]{}, raw2[4096]{};
        double s1 = 0.0, s2 = 0.0;
        int cnt[4] = {};
        const double k_w = 2.0 * std::numbers::pi_v<double> / 64.0;
        for (int ky = 0; ky < 64; ++ky) {
            const int sy = (ky <= 32) ? ky : (ky - 64);
            for (int kx = 0; kx < 64; ++kx) {
                const int sx = (kx <= 32) ? kx : (kx - 64);
                const int r2 = sy * sy + sx * sx;
                const size_t idx = size_t(ky) * 64 + kx;
                if (r2 == 0) continue;
                int o = 3;
                if (r2 <= 16) o = 0;
                else if (r2 <= 64) o = 1;
                else if (r2 <= 256) o = 2;
                else o = 3;
                oct_f[idx] = float(o);
                ++cnt[o];
                const double w2 = k_w * k_w * double(r2);
                raw1[idx] = 0.85 * std::exp(-0.50 * 0.50 * w2) + 0.15;
                raw2[idx] = 0.985 * std::exp(-0.90 * 0.90 * w2) + 0.015;
                s1 += raw1[idx];
                s2 += raw2[idx];
            }
        }
        for (int o = 0; o < 4; ++o) oct_cnt[o] = float(cnt[o]);
        const double inv1 = 4095.0 / std::max(1e-12, s1);
        const double inv2 = 4095.0 / std::max(1e-12, s2);
        psi_1x[0] = 1.0f;
        psi_2x[0] = 1.0f;
        for (int k = 1; k < 4096; ++k) {
            psi_1x[k] = float(raw1[k] * inv1);
            psi_2x[k] = float(raw2[k] * inv2);
        }
    }
};

inline const StaticTables64& static_tables64() {
    static const StaticTables64 tbl;
    return tbl;
}

inline void build_v3_guides_header(
    const Stage45GpuJob& j, std::vector<float>& out_vec4,
    bool& out_is_aeb, bool& out_any_rob, float& out_sat0, bool& out_is_bayer
) {
    out_vec4.assign(size_t(4 + 3 * j.N) * 4, 0.0f);
    const float sig0_ref = float(j.pg_sig0_sq);
    const float gain_ref = float(j.pg_gain);
    out_sat0 = j.frame_sat_levels ? j.frame_sat_levels[0] : 1.0e6f;
    const float hl_lim = j.glint_desat ? (j.is_astro ? 0.88f : 0.85f) : 1.0e6f;
    out_is_aeb = (j.frame_gains != nullptr || j.frame_read_vars != nullptr || j.frame_shot_coeffs != nullptr);

    out_is_bayer = !j.is_xtrans;
    if (out_is_bayer) {
        for (int r = 0; r < 6 && out_is_bayer; ++r) {
            for (int c = 0; c < 6; ++c) {
                if (j.cfa6x6[r][c] != j.cfa6x6[r & 1][c & 1]) {
                    out_is_bayer = false;
                    break;
                }
            }
        }
    }

    // Header vec4[0..3]: V3JobConsts
    out_vec4[0] = sig0_ref;        out_vec4[1] = gain_ref;       out_vec4[2] = out_sat0;        out_vec4[3] = j.wb_r;
    out_vec4[4] = j.wb_b;          out_vec4[5] = hl_lim;         out_vec4[6] = j.lca_r_k1;      out_vec4[7] = j.lca_r_k2;
    out_vec4[8] = j.lca_b_k1;      out_vec4[9] = j.lca_b_k2;     out_vec4[10] = j.q_luma_var;   out_vec4[11] = j.q_chroma_var;
    out_vec4[12] = j.q_nlt_s0;     out_vec4[13] = j.q_nlt_s1;    out_vec4[14] = j.q_nlt_s2;     out_vec4[15] = 0.0f;

    out_any_rob = false;
    const auto& motion = *j.mw_half;
    const auto& fy = *j.flows_fy;
    const auto& fx = *j.flows_fx;
    for (int n = 0; n < j.N; ++n) {
        const float fw = gpu_frame_weight(j, n);
        const bool rob = gpu_use_robust(j, n);
        if (rob) out_any_rob = true;
        const float rv = j.frame_read_vars ? j.frame_read_vars[n] : sig0_ref;
        const float sh = j.frame_shot_coeffs ? j.frame_shot_coeffs[n] : gain_ref;
        const float sat = j.frame_sat_levels ? j.frame_sat_levels[n] : 1.0e6f;
        const float sat_lo = out_is_aeb ? (0.82f * sat) : (0.9998f * sat);
        const float sat_hi = out_is_aeb ? (0.99f * sat) : (1.0005f * sat);
        const float sat_ispan = 1.0f / std::max(1e-6f, sat_hi - sat_lo);

        float unit_mw = 1.0f;
        if (!j.is_virtual_nd && n < int(motion.size()) && !motion[n].empty()) {
            for (float m : motion[n]) {
                if (m != 1.0f) { unit_mw = 0.0f; break; }
            }
        }
        float has_flow = 0.0f;
        if (n < int(fy.size()) && n < int(fx.size()) && !fy[n].empty() && !fx[n].empty()) {
            for (size_t i = 0; i < fy[n].size(); ++i) {
                if (fy[n][i] != 0.0f || fx[n][i] != 0.0f) { has_flow = 1.0f; break; }
            }
        }
        float ois[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (j.frame_ois_shifts && j.frame_ois_shifts->size() >= size_t(n + 1) * 4) {
            for (int k = 0; k < 4; ++k) ois[k] = (*j.frame_ois_shifts)[size_t(n) * 4 + k];
        }
        const size_t base = size_t(4 + 3 * n) * 4;
        out_vec4[base + 0] = fw;       out_vec4[base + 1] = rv;                out_vec4[base + 2] = sh;       out_vec4[base + 3] = sat_lo;
        out_vec4[base + 4] = sat_ispan; out_vec4[base + 5] = rob ? 1.0f : 0.0f; out_vec4[base + 6] = unit_mw; out_vec4[base + 7] = has_flow;
        out_vec4[base + 8] = ois[0];   out_vec4[base + 9] = ois[1];            out_vec4[base + 10] = ois[2];  out_vec4[base + 11] = ois[3];
    }
}

inline void build_v3_stage5_lut_blob(
    const Stage45GpuJob& j, int out_scale, double eta_eff, float min_g, float max_g,
    double sig2_in, double gain_in, std::vector<float>& blob,
    float& out_u_min, float& out_u_max, float& out_inv_du, float out_oct_mul[4]
) {
    constexpr int B_BANDS = 16;
    constexpr int LUT_N = 256;
    blob.assign(size_t(kLutSize), 0.0f);

    const float sig0_total_mean = std::max(1e-12f, float(sig2_in));
    out_u_min = std::min(min_g - 0.01f, -4.0f * std::sqrt(sig0_total_mean) - 1e-4f);
    out_u_max = std::max(1.05f, max_g + 0.05f);
    const float du = (out_u_max - out_u_min) / float(LUT_N - 1);
    out_inv_du = 1.0f / du;

    const double eta_q = (j.q_luma_var > 0.0f)
        ? (eta_eff + double(j.q_corr_gain) * std::max(0.0, 1.0 - eta_eff))
        : 0.0;
    const float sig0_pg_scaled = float(j.pg_sig0_sq * eta_eff);
    const float gain_pg_scaled = float(j.pg_gain * eta_eff);

    auto eval_nlt_s = [&](float u) -> float {
        const float x = std::max(0.0f, std::min(1.0f, u));
        if (j.nlt_slope_lut256) {
            const float f = x * 255.0f;
            const int idx = std::min(254, int(f));
            const float t = f - float(idx);
            return j.nlt_slope_lut256[idx] + t * (j.nlt_slope_lut256[idx + 1] - j.nlt_slope_lut256[idx]);
        }
        return std::sqrt(gpu_eval_nlt_sq(j.q_nlt_s0, j.q_nlt_s1, j.q_nlt_s2, x));
    };

    for (int b = 0; b < B_BANDS; ++b) {
        float q_row_val = j.q_luma_var;
        if (j.qvar_luma_row && j.qvar_rows > 0) {
            const int yr0 = (b * j.qvar_rows) / B_BANDS;
            const int yr1 = std::max(yr0 + 1, ((b + 1) * j.qvar_rows) / B_BANDS);
            double qs = 0.0;
            for (int yr = yr0; yr < yr1; ++yr) qs += j.qvar_luma_row[yr];
            q_row_val = float(qs / double(yr1 - yr0));
        }
        auto var_u = [&](double u) -> double {
            const double up = std::max(0.0, u);
            const float ns = eval_nlt_s(float(up));
            const double v = double(sig0_pg_scaled) + double(gain_pg_scaled) * up
                           + eta_q * double(q_row_val) * double(ns) * double(ns);
            return std::max(1e-12, v);
        };

        float* fwd = blob.data() + kOffVstFwd + b * LUT_N;
        float* inv = blob.data() + kOffVstInv + b * LUT_N;
        float* ds2 = blob.data() + kOffDsig2Du + b * LUT_N;
        float* meta = blob.data() + kOffBandMeta + b * 8;

        fwd[0] = 0.0f;
        static constexpr double qp[4] = {0.06943184, 0.33000948, 0.66999052, 0.93056816};
        static constexpr double qw[4] = {0.17392742, 0.32607258, 0.32607258, 0.17392742};
        double acc_t = 0.0;
        for (int k = 1; k < LUT_N; ++k) {
            const double u0 = double(out_u_min) + double(k - 1) * double(du);
            double seg = 0.0;
            for (int q = 0; q < 4; ++q) {
                seg += qw[q] / std::sqrt(var_u(u0 + qp[q] * double(du)));
            }
            acc_t += seg * double(du);
            fwd[k] = float(acc_t);
        }
        for (int k = 0; k < LUT_N; ++k) {
            const double uk = double(out_u_min) + double(k) * double(du);
            if (uk <= 0.0) {
                ds2[k] = 0.0f;
            } else {
                const double hh = 1e-4;
                const double vm = var_u(std::max(0.0, uk - hh));
                const double vp = var_u(uk + hh);
                ds2[k] = float(std::max(0.0, (vp - vm) / (2.0 * hh + (uk < hh ? uk - hh : 0.0))));
            }
        }
        const float slo = float(1.0 / std::sqrt(var_u(double(out_u_min))));
        const float shi = float(1.0 / std::sqrt(var_u(double(out_u_max))));
        const float zmin = fwd[0];
        const float zmax = std::max(zmin + 1e-6f, fwd[LUT_N - 1]);
        const float idz = float(LUT_N - 1) / (zmax - zmin);
        meta[0] = slo;
        meta[1] = shi;
        meta[2] = zmin;
        meta[3] = zmax;
        meta[4] = idz;
        meta[5] = 1.0f / std::max(1e-12f, slo);
        meta[6] = 1.0f / std::max(1e-12f, shi);
        meta[7] = 0.0f;

        int cur_k = 0;
        for (int jz = 0; jz < LUT_N; ++jz) {
            const float zt = zmin + (zmax - zmin) * float(jz) / float(LUT_N - 1);
            while (cur_k + 2 < LUT_N && fwd[cur_k + 1] < zt) ++cur_k;
            const float z0 = fwd[cur_k];
            const float z1 = fwd[cur_k + 1];
            const float frac = (z1 > z0 + 1e-12f) ? std::max(0.0f, std::min(1.0f, (zt - z0) / (z1 - z0))) : 0.0f;
            inv[jz] = out_u_min + (float(cur_k) + frac) * du;
        }
    }

    float walsh_hi_mul = 1.0f;
    if (j.walsh16_norm) {
        const float w0 = std::max(0.25f, j.walsh16_norm[0]);
        double hi_s = 0.0;
        for (int k = 8; k < 16; ++k) hi_s += j.walsh16_norm[k] / w0;
        const float q_mid = j.q_luma_var * gpu_eval_nlt_sq(j.q_nlt_s0, j.q_nlt_s1, j.q_nlt_s2, 0.18f);
        const float pg_m = float(j.pg_sig0_sq + j.pg_gain * 0.18);
        const float fq = q_mid / (pg_m + q_mid + 1e-10f);
        walsh_hi_mul = (1.0f - fq) + fq * std::max(0.7f, std::min(1.8f, float(hi_s / 8.0)));
    }
    const float str2 = j.denoise_strength * j.denoise_strength;
    out_oct_mul[0] = str2 * j.dct_scale_mul[0];
    out_oct_mul[1] = str2 * j.dct_scale_mul[1];
    out_oct_mul[2] = str2 * j.dct_scale_mul[2];
    out_oct_mul[3] = str2 * 0.5f * (j.dct_scale_mul[3] + j.dct_scale_mul[4]) * walsh_hi_mul;

    const auto& st = static_tables64();
    const float* psi = (out_scale == 2) ? st.psi_2x : st.psi_1x;
    std::copy_n(psi, 4096, blob.data() + kOffPsi);
    const float eps_w = std::max(0.04f, std::min(0.15f, 0.10f / std::max(0.75f, j.denoise_strength)));
    float* gfl = blob.data() + kOffGFloor;
    gfl[0] = 0.0f;
    for (int k = 1; k < 4096; ++k) {
        gfl[k] = std::max(0.04f, std::min(0.36f, eps_w / std::sqrt(psi[k])));
    }
    std::copy_n(st.oct_f, 4096, blob.data() + kOffOctF);
    std::copy_n(st.tw_cos, 64, blob.data() + kOffTwCos);
    std::copy_n(st.tw_sin, 64, blob.data() + kOffTwSin);
    std::copy_n(st.sin_win, 64, blob.data() + kOffSinWin);
    std::copy_n(st.oct_cnt, 4, blob.data() + kOffOctCnt);
    (void)gain_in;
}

inline void compute_gamma_quad_half(const Stage45GpuJob& j, std::vector<float>& out_gamma) {
    const int Hh = j.H1 / 2, Wh = j.W1 / 2;
    const size_t n_half = std::max<size_t>(1, size_t(Hh) * size_t(Wh));
    if (j.is_clean_plate || j.is_virtual_nd || j.disable_ha_fallback) {
        out_gamma.assign(n_half, 1.0f);
        return;
    }
    if (j.N <= 1 || Hh <= 0 || Wh <= 0 || !j.flows_fy || !j.flows_fx || !j.mw_half) {
        out_gamma.assign(n_half, 0.0f);
        return;
    }
    bool is_bayer = !j.is_xtrans;
    if (is_bayer) {
        for (int r = 0; r < 6 && is_bayer; ++r)
            for (int c = 0; c < 6; ++c)
                if (j.cfa6x6[r][c] != j.cfa6x6[r & 1][c & 1]) { is_bayer = false; break; }
    }
    float g_sites_y[24]{}, g_sites_x[24]{};
    int n_gsites = 0;
    const int period = is_bayer ? 2 : 6;
    for (int r = 0; r < period; ++r) {
        for (int c = 0; c < period; ++c) {
            if (j.cfa6x6[r][c] == 1 && n_gsites < 24) {
                g_sites_y[n_gsites] = float(r);
                g_sites_x[n_gsites] = float(c);
                ++n_gsites;
            }
        }
    }
    if (n_gsites == 0) {
        out_gamma.assign(n_half, 0.0f);
        return;
    }
    std::vector<float> fw(size_t(j.N), 1.0f);
    for (int n = 0; n < j.N; ++n) fw[size_t(n)] = gpu_frame_weight(j, n);
    out_gamma.resize(n_half);
    const float per_f = float(period), inv_per = 1.0f / per_f, half_per = 0.5f * per_f;
    for (int r = 0; r < Hh; ++r) {
        const float by = float((2 * r + j.origin_y) % period);
        for (int c = 0; c < Wh; ++c) {
            const size_t i = size_t(r) * size_t(Wh) + size_t(c);
            const float bx = float((2 * c + j.origin_x) % period);
            float wg[16]{};
            for (int n = 0; n < j.N; ++n) {
                const float fwn = fw[size_t(n)];
                if (fwn <= 0.0f) continue;
                const float mw = (n < int(j.mw_half->size()) && !(*j.mw_half)[n].empty())
                    ? gpu_motion_weight(j, (*j.mw_half)[n][i]) * fwn : fwn;
                if (mw < 0.02f) continue;
                const float dy = (n < int(j.flows_fy->size()) && !(*j.flows_fy)[n].empty())
                    ? 2.0f * (*j.flows_fy)[n][i] : 0.0f;
                const float dx = (n < int(j.flows_fx->size()) && !(*j.flows_fx)[n].empty())
                    ? 2.0f * (*j.flows_fx)[n][i] : 0.0f;
                for (int q = 0; q < 16; ++q) {
                    const float oy = float(q >> 2) * 0.5f;
                    const float ox = float(q & 3) * 0.5f;
                    const float sy = by + oy + dy;
                    const float sx = bx + ox + dx;
                    const float uy = sy - per_f * std::floor(sy * inv_per);
                    const float ux = sx - per_f * std::floor(sx * inv_per);
                    float best_d2 = 4.0f;
                    for (int g = 0; g < n_gsites; ++g) {
                        float ddy = std::fabs(uy - g_sites_y[g]);
                        if (ddy > half_per) ddy = per_f - ddy;
                        float ddx = std::fabs(ux - g_sites_x[g]);
                        if (ddx > half_per) ddx = per_f - ddx;
                        best_d2 = std::min(best_d2, ddy * ddy + ddx * ddx);
                    }
                    const float k = std::max(0.0f, std::exp(-3.2f * best_d2) - 0.09071795f);
                    wg[q] += mw * k;
                }
            }
            float min_w = wg[0];
            for (int q = 1; q < 16; ++q) min_w = std::min(min_w, wg[q]);
            const float t = std::clamp((min_w - 0.18f) * (1.0f / 0.67f), 0.0f, 1.0f);
            out_gamma[i] = t * t * (3.0f - 2.0f * t);
        }
    }
    if (Hh >= 3 && Wh >= 3) {
        std::vector<float> sm(n_half);
        for (int r = 0; r < Hh; ++r) {
            const int rm = std::max(0, r - 1), rp = std::min(Hh - 1, r + 1);
            for (int c = 0; c < Wh; ++c) {
                const int cm = std::max(0, c - 1), cp = std::min(Wh - 1, c + 1);
                sm[size_t(r) * Wh + c] = 0.0625f * (
                    out_gamma[size_t(rm) * Wh + cm] + 2.0f * out_gamma[size_t(rm) * Wh + c] + out_gamma[size_t(rm) * Wh + cp] +
                    2.0f * out_gamma[size_t(r) * Wh + cm] + 4.0f * out_gamma[size_t(r) * Wh + c] + 2.0f * out_gamma[size_t(r) * Wh + cp] +
                    out_gamma[size_t(rp) * Wh + cm] + 2.0f * out_gamma[size_t(rp) * Wh + c] + out_gamma[size_t(rp) * Wh + cp]);
            }
        }
        out_gamma.swap(sm);
    }
}

} // namespace v3_host

GpuDeviceInfo probe_gpu_device();
bool run_stage45_gpu(Stage45GpuJob& job);
struct SensorResidualJob {
    int H=0,W=0;
    const std::vector<std::vector<float>> *raw=nullptr,*fy=nullptr,*fx=nullptr,*mask=nullptr;
    const float* scene=nullptr;
    const uint8_t* cfa=nullptr;
    float read_variance=0,shot=0;
    std::vector<std::vector<float>>* residual=nullptr;
};
bool run_sensor_residual_gpu(const SensorResidualJob& job);

struct M5aGlsGpuJob {
    int H1 = 0, W1 = 0, N = 0, out_scale = 2, n_threads = 1, tile = 64;
    int iterations = 1;
    float morozov_target = 1.05f;
    float lca_r_k1 = 0.0f, lca_r_k2 = 0.0f;
    float lca_b_k1 = 0.0f, lca_b_k2 = 0.0f;
    float optical_psf_sigma_1x = 0.0f;
    bool wavelet_tobit_deadzone = false;
    float wavelet_deadzone_halfwidth = 0.0f;
    float jacobian_prox_strength = 0.35f;
    float wb_r = 1.0f, wb_b = 1.0f;
    float read_variance = 1e-6f;
    float shot = 0.0f;
    float quad = 0.0f;
    uint8_t cfa6x6[6][6] = {};
    const std::vector<std::vector<float>>* raw = nullptr;
    const std::vector<std::vector<float>>* fy = nullptr;
    const std::vector<std::vector<float>>* fx = nullptr;
    const std::vector<std::vector<float>>* mw = nullptr;
    const float* frame_read_variances = nullptr;
    const float* frame_shot_coefficients = nullptr;
    const float* frame_saturation_levels = nullptr;
    const std::vector<float>* frame_ois_shifts = nullptr;
    const std::vector<float>* frame_sharp_scores = nullptr;
    const float* fisher_comp = nullptr;
    float* rgb = nullptr;
    int diagnostic_stride = 1;
    double out_before = 0.0, out_after = 0.0, out_rms_update = 0.0;
    size_t out_samples = 0, out_changed = 0;
    int out_iterations = 0;
    std::string out_err;
};
bool run_m5a_gls_gpu(M5aGlsGpuJob& job);

struct M5bZoneParam {
    float cx = 0.5f, cy = 0.5f;
    float sigma_major_px = 0.0f, sigma_minor_px = 0.0f, angle_rad = 0.0f;
    float sigma_rgb_px[3] = {0.0f, 0.0f, 0.0f};
    const float* otf_re64 = nullptr;
    const float* otf_im64 = nullptr;
};

struct M5bDeconvGpuJob {
    float* rgb = nullptr;
    int H = 0, W = 0, n_threads = 1;
    int olpf_mode = 0, out_scale = 2, psf_iters = 4;
    float psf_sigma_2x = 1.05f, strength = 1.0f;
    bool is_astro = false;
    bool includes_pix_ap = false;
    float m5a_gamma = 1.0f, photo_shelf_k = 0.0f;
    float n_eff_burst = -1.0f;
    float wb_r = 1.0f, wb_b = 1.0f, sat0 = 1.0e6f;
    float read_variance = 1e-6f, shot = 0.0f, quad = 0.0f;
    const float* focus_half = nullptr;
    const float* n_eff_half = nullptr;
    const float* phi_half = nullptr;
    int focus_h = 0, focus_w = 0;
    std::array<float, 4> dir_ident_gate{1.0f, 1.0f, 1.0f, 1.0f};
    const uint8_t* nlens_tile_mask = nullptr;
    std::vector<M5bZoneParam> zones;
    std::vector<M5bZoneParam> fallback_zones;
    double out_rms_update = 0.0;
    size_t out_changed = 0, out_samples = 0;
    std::string out_err;
};
bool run_m5b_deconv_gpu(M5bDeconvGpuJob& job);

struct SiliconCleanupGpuJob {
    int H = 0, W = 0;
    std::vector<std::vector<float>>* raw = nullptr;
    const std::vector<std::vector<float>>* fy = nullptr;
    const std::vector<std::vector<float>>* fx = nullptr;
    const std::vector<std::vector<float>>* mw = nullptr;
    const uint8_t* cfa = nullptr;
    float read_variance = 0.0f, shot = 0.0f, quad = 0.0f;
    int n_threads = 1;
    double out_before = 0.0, out_after = 0.0, out_rms_update = 0.0;
    size_t out_samples = 0, out_changed = 0, out_peak_scratch_bytes = 0;
    std::string out_err;
};
bool run_silicon_cleanup_gpu(SiliconCleanupGpuJob& job);

} // namespace heraw_gpu
