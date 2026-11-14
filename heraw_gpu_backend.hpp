// heraw_gpu_backend.hpp -- Cross-platform GPU compute interface (macOS Metal + Windows Vulkan)
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
#include <new>
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
    int32_t H1, W1, Hh, Wh, Ew, is_frame0, use_rob, is_xtrans;
    uint32_t cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, frame_w_scale;
    int32_t origin_y, origin_x;
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
};

struct PC_ChromaFinal {
    int32_t H1, W1, Hh, Wh;
    float wb_r, wb_b, hl_start, hl_span;
    uint32_t cfa0, cfa1, cfa2;
    int32_t is_xtrans;
};

static_assert(sizeof(PC_GreenAccum) == 64, "Green push constant ABI");
static_assert(sizeof(PC_ChromaAccum) == 104, "Chroma push constant ABI");
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
    float wb_r = 1.0f;
    float wb_b = 1.0f;

    float denoise_strength = 1.0f;
    float chroma_denoise = 1.0f;
    int psf_iters = 4;
    float psf_sigma = 1.05f;
    bool edge_polish = true;
    bool glint_desat = true;

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
    double global_eta = -1.0, global_mean_g = -1.0;
    float global_edge_p80 = -1.0f;
    bool eta_only = false;
    int core_y0 = 0, core_x0 = 0, core_y1 = 0, core_x1 = 0;
    double out_eta_sum = 0.0;
    int64_t out_eta_count = 0;
    float* eta_quarter_out=nullptr; // Caller-owned global (ceil(H1/4),ceil(W1/4)).

    std::function<void(double eta_eff, double dt_s4, const std::string& dev_label)> on_stage4_done;

    // Outputs
    float* rgb_2x_out = nullptr; // [H2 * W2 * 3] caller-allocated
    double out_eta_eff = 0.25;
    float out_resid_noise_2x = 0.0f;
    std::string out_device_label;
    std::string out_err;
};

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
inline std::vector<T> gpu_crop(const T* src, int stride, int y, int x,
                               int h, int w, int channels = 1) {
    std::vector<T> dst(size_t(h) * w * channels);
    for (int r = 0; r < h; ++r)
        std::copy_n(src + (size_t(y + r) * stride + x) * channels,
                    size_t(w) * channels, dst.data() + size_t(r) * w * channels);
    return dst;
}

template<class Run>
bool run_stage45_streamed(Stage45GpuJob& job, Run run) {
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
        for (const auto* v : {job.flows_fy, job.flows_fx}) {
            for (float f : (*v)[n]) {
                if (!std::isfinite(f)) { job.out_err = "Non-finite Stage45 flow"; return false; }
                drift = std::max(drift, 2.0 * std::fabs(double(f)));
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
    global.full_H1 = H; global.full_W1 = W;
    global.origin_y = 0; global.origin_x = 0;
    global.keep_frames_lin = true;
    global.global_eta = -1.0;
    double mean = 0.0;
    for (size_t i = 0; i < pixels; i += 16) mean += std::max(0.0f, job.g0_ha_1x[i]);
    global.global_mean_g = mean / double((pixels + 15) / 16);
    std::vector<float> edges;
    for (int r = 0; r < H; r += 8) for (int c = 0; c + 2 < W; c += 8)
        edges.push_back(std::fabs(job.g0_ha_1x[size_t(r) * W + c + 2] -
                                  job.g0_ha_1x[size_t(r) * W + c]));
    global.global_edge_p80 = quick_pct_gpu_helper(edges, 0.80f);
    auto finish = [&]() {
        if (!job.keep_frames_lin)
            for (auto& frame : *job.frames_lin) std::vector<float>().swap(frame);
        return true;
    };
    try {
        if (job.tile_size_1x <= 0 ||
            (H <= job.tile_size_1x && W <= job.tile_size_1x && pixels <= job.max_tile_pixels)) {
            if (!run(global)) { job.out_err = global.out_err; return false; }
            job.out_eta_eff = global.out_eta_eff;
            job.out_resid_noise_2x = global.out_resid_noise_2x;
            job.out_device_label = global.out_device_label;
            job.out_peak_tile_pixels = pixels;
            job.out_tile_count = 1;
            job.out_wall_seconds=global.out_wall_seconds;
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
        if (job.denoise_strength > 1e-4f) halo += 7 * 2 * 16 / 2 + 32 + 48 + 4;
        if (job.chroma_denoise > 1e-4f) halo += 126 + 2 + (job.glint_desat ? 8 : 0);
        if (job.psf_iters > 0) halo += job.psf_iters * psf_radius + 8;
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
        // Source and reference share one ROI. Bound the union directionally:
        // all Stage5-dependent reference pixels, plus contributing Stage4 source
        // positions. Bilinear flow/mask extrema lie at the four cell corners;
        // a zero mask vertex can still affect interpolation in an active cell.
        auto bound_roi = [&](Tile& t, int dependency_halo) {
            const int by0 = std::max(0, t.y - dependency_halo), bx0 = std::max(0, t.x - dependency_halo);
            const int by1 = std::min(H, t.y + t.h + dependency_halo), bx1 = std::min(W, t.x + t.w + dependency_halo);
            double lo_y = by0 - optical_margin - 3, lo_x = bx0 - optical_margin - 3;
            double hi_y = by1 + optical_margin + 3, hi_x = bx1 + optical_margin + 3;
            const int Wh = W / 2, Hh = H / 2;
            for (int n = 1; n < job.N; ++n) {
                const float fw = gpu_frame_weight(job, n);
                if (fw <= 0.0f) continue;
                const auto& fy = (*job.flows_fy)[n];
                const auto& fx = (*job.flows_fx)[n];
                const auto& mw = (*job.mw_half)[n];
                for (int r = by0 / 2; r <= (by1 - 1) / 2; ++r)
                    for (int c = bx0 / 2; c <= (bx1 - 1) / 2; ++c) {
                        const size_t i00 = size_t(r) * Wh + c;
                        const size_t i01 = size_t(r) * Wh + std::min(c + 1, Wh - 1);
                        const size_t i10 = size_t(std::min(r + 1, Hh - 1)) * Wh + c;
                        const size_t i11 = size_t(std::min(r + 1, Hh - 1)) * Wh + std::min(c + 1, Wh - 1);
                        if (!job.is_virtual_nd &&
                            std::max({mw[i00], mw[i01], mw[i10], mw[i11]}) * fw < 0.02f) {
                            ++masked_cells;
                            continue;
                        }
                        // Green samples flow at (Y/2+.125,X/2+.125), whereas
                        // chroma uses (Y/2+is_b/4,X/2+is_b/4).
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
                            // Discontinuous/outlier flow can cross the entire
                            // sensor between vertices while every queried site
                            // lies outside. Refine only these exceptional cells
                            // at the actual Green/R/B shader sampling positions.
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
                            // Cover float interpolation/addition rounding in
                            // both native backends, including cancellation.
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
        // cores. Second pass uses that one value for every GAT/chroma/RL kernel.
        double eta_sum = 0.0;
        int64_t eta_count = 0;
        double dt_s4 = 0.0;
        size_t pass_pixels[2] = {};
        for (int pass = 0; pass < 2; ++pass) {
            if (pass == 1) global.global_eta = std::max(0.01, eta_sum / double(std::max<int64_t>(1, eta_count)));
            for (const Tile& planned : tiles) {
                const auto crop_start=std::chrono::steady_clock::now();
                Tile t = planned;
                // The statistics pass stops after green fusion. Its sources
                // need the fusion support, not the later DCT/chroma/RL halo.
                if (pass == 0) bound_roi(t, 12);
                pass_pixels[pass] += size_t(t.y1-t.y0)*(t.x1-t.x0);
                Stage45GpuJob sub = global;
                sub.H1 = t.y1 - t.y0; sub.W1 = t.x1 - t.x0;
                sub.origin_y = t.y0; sub.origin_x = t.x0;
                sub.eta_only = pass == 0;
                sub.core_y0 = t.y - t.y0; sub.core_x0 = t.x - t.x0;
                sub.core_y1 = sub.core_y0 + t.h; sub.core_x1 = sub.core_x0 + t.w;
                sub.on_stage4_done = [&](double, double dt, const std::string&) { dt_s4 += dt; };
                for (int r = 0; r < 6; ++r) for (int c = 0; c < 6; ++c)
                    sub.cfa6x6[r][c] = job.cfa6x6[(r + t.y0) % 6][(c + t.x0) % 6];
                std::vector<std::vector<float>> frames(job.N), fy(job.N), fx(job.N), mw(job.N);
                for (int n = 0; n < job.N; ++n) {
                    frames[n] = gpu_crop((*job.frames_lin)[n].data(), W, t.y0, t.x0, sub.H1, sub.W1);
                    fy[n] = gpu_crop((*job.flows_fy)[n].data(), W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2);
                    fx[n] = gpu_crop((*job.flows_fx)[n].data(), W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2);
                    mw[n] = gpu_crop((*job.mw_half)[n].data(), W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2);
                }
                auto cov = gpu_crop(job.inv_cov, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2, 3);
                auto gg = gpu_crop(job.g_guide, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2);
                auto boost = gpu_crop(job.snr_boost_half, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2);
                auto rl = gpu_crop(job.rl_mod_f16, W / 2, t.y0 / 2, t.x0 / 2, sub.H1 / 2, sub.W1 / 2);
                auto ca = gpu_crop(job.c_anc_1x, W, t.y0, t.x0, sub.H1, sub.W1);
                auto ha = gpu_crop(job.g0_ha_1x, W, t.y0, t.x0, sub.H1, sub.W1);
                std::vector<float> rgb(pass == 1 ? size_t(sub.H1) * sub.W1 * 12 : 0);
                sub.frames_lin = &frames; sub.flows_fy = &fy; sub.flows_fx = &fx; sub.mw_half = &mw;
                sub.inv_cov = cov.data(); sub.g_guide = gg.data(); sub.snr_boost_half = boost.data();
                sub.rl_mod_f16 = rl.data(); sub.c_anc_1x = ca.data(); sub.g0_ha_1x = ha.data();
                sub.rgb_2x_out = rgb.data();
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
                } else {
                    for (int r = 0; r < t.h * 2; ++r)
                        std::copy_n(rgb.data() + (size_t(sub.core_y0 * 2 + r) * sub.W1 * 2 + sub.core_x0 * 2) * 3,
                                    size_t(t.w) * 6, job.rgb_2x_out + (size_t(t.y * 2 + r) * W * 2 + t.x * 2) * 3);
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

GpuDeviceInfo probe_gpu_device();
bool run_stage45_gpu(Stage45GpuJob& job);
#ifdef __APPLE__
struct SensorResidualJob {
    int H=0,W=0;
    const std::vector<std::vector<float>> *raw=nullptr,*fy=nullptr,*fx=nullptr,*mask=nullptr;
    const float* scene=nullptr;
    const uint8_t* cfa=nullptr;
    float read_variance=0,shot=0;
    std::vector<std::vector<float>>* residual=nullptr;
};
bool run_sensor_residual_gpu(const SensorResidualJob& job);
#endif

} // namespace heraw_gpu
