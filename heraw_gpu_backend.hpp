// heraw_gpu_backend.hpp -- Cross-platform GPU compute interface (macOS Metal + Windows Vulkan)
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
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
    int32_t H1, W1, Hh, Wh, Ew, is_frame0, use_rob;
    float pg_sig0_sq, pg_gain;
};

struct PC_GreenFinal {
    int32_t H1, W1, Hh, Wh, Ew;
};

struct PC_ChromaAccum {
    int32_t H1, W1, Hh, Wh, is_frame0, use_rob;
    float pg_sig0_sq, pg_gain, wb_r, wb_b;
};

struct PC_ChromaFinal {
    int32_t H1, W1, Hh, Wh;
    float wb_r, wb_b, hl_start, hl_span;
};

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

inline uint16_t float_to_fp16_bits(float val) {
    uint32_t f = 0;
    std::memcpy(&f, &val, sizeof(f));
    uint32_t sign = (f >> 16) & 0x8000u;
    int32_t exp = ((int32_t)((f >> 23) & 0xFFu)) - 127 + 15;
    uint32_t mant = f & 0x007FFFFFu;
    if (exp <= 0) return (uint16_t)sign;
    if (exp >= 31) return (uint16_t)(sign | 0x7C00u);
    return (uint16_t)(sign | ((uint32_t)exp << 10) | ((mant + 0x1000u) >> 13));
}

inline uint32_t pack_half2_u32(float x, float y) {
    return (uint32_t)float_to_fp16_bits(x) | ((uint32_t)float_to_fp16_bits(y) << 16);
}

struct PC_RlRatio {
    uint32_t count;
    float snr_knee, clamp_delta;
};

inline PC_Gauss1D make_gauss_pc(int H, int W, int dir, float sigma) {
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
    bool is_astro = false;

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
    float wb_r = 1.0f;
    float wb_b = 1.0f;

    float denoise_strength = 1.0f;
    float chroma_denoise = 1.0f;
    int psf_iters = 4;
    float psf_sigma = 1.05f;
    bool edge_polish = true;
    bool glint_desat = true;

    std::function<void(double eta_eff, double dt_s4, const std::string& dev_label)> on_stage4_done;

    // Outputs
    float* rgb_2x_out = nullptr; // [H2 * W2 * 3] caller-allocated
    double out_eta_eff = 0.25;
    float out_resid_noise_2x = 0.0f;
    std::string out_device_label;
    std::string out_err;
};

GpuDeviceInfo probe_gpu_device();
bool run_stage45_gpu(Stage45GpuJob& job);

} // namespace heraw_gpu
