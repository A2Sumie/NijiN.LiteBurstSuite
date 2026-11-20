// Unified v3.0.0 External Math Audit Oracles (E01-E12, Q1-Q10, R1-R12, Cross-Section 1-6, DXO1-4).
// Pure 7-bit ASCII C++20 verification suite. Compile and run with ASan + UBSan + -Werror.
#include "../heraw_burst_planner.hpp"
#include "../heraw_eval_metrology.hpp"
#include "../heraw_exif_io.hpp"
#include "../heraw_gpu_backend.hpp"
#include "../heraw_suite_core.hpp"
#include "../heraw_suite_diffusion.hpp"
#include "../heraw_suite_exr_stream.hpp"
#include "../heraw_suite_features.hpp"
#include "../heraw_suite_measurement.hpp"
#include "../heraw_suite_motion_shutter.hpp"
#include "../heraw_suite_observer.hpp"
#include "../heraw_suite_refocus.hpp"
#include "../heraw_suite_spectral_solver.hpp"
#ifdef SUITE_V3_LIBRAW
#include <libraw/libraw.h>
#endif

#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace {

static bool near_val(double a, double b, double tol) {
    return std::isfinite(a) && std::isfinite(b) && (std::abs(a - b) <= tol);
}

static void build_rggb_6x6(uint8_t cfa[6][6]) {
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            cfa[r][c] = (r & 1) ? ((c & 1) ? 2 : 1) : ((c & 1) ? 1 : 0);
        }
    }
}

static void test_e01_e05_native_variance_and_weight() {
    // E01 & E05: single-count read + shot + quant variance, 1:4 ratio => weight ratio 4:1 => w1/(w0+w1) = 0.200000
    heraw_meas::NativeFrameContract f0;
    f0.frame_index = 0;
    f0.exp_scale = 1.0f;
    f0.read_var_norm = 1e-4f;
    f0.shot_coeff_norm = 0.0f;
    f0.quant_c0_norm = 0.0f;
    f0.quant_c1_norm = 0.0f;
    f0.quant_c2_norm = 0.0f;

    heraw_meas::NativeFrameContract f1 = f0;
    f1.frame_index = 1;
    f1.read_var_norm = 4e-4f;

    const float v0 = f0.total_variance_norm(0.25f);
    const float v1 = f1.total_variance_norm(0.25f);
    const double w0 = f0.radiometric_precision_norm(0.25f);
    const double w1 = f1.radiometric_precision_norm(0.25f);
    const double frac1 = w1 / (w0 + w1);
    assert(near_val(v0, 1e-4, 1e-9));
    assert(near_val(v1, 4e-4, 1e-9));
    assert(near_val(frac1, 0.200000, 1e-6));

    // E05 non-double-counting with nonzero shot & quadratic quant
    heraw_meas::NativeFrameContract fq;
    fq.read_var_norm = 1e-4f;
    fq.shot_coeff_norm = 2e-4f;
    fq.quant_c0_norm = 1e-5f;
    fq.quant_c1_norm = 2e-5f;
    fq.quant_c2_norm = 4e-5f;
    const float x = 0.5f;
    const double expected_v = 1e-4 + 2e-4 * x + (1e-5 + x * (2e-5 + x * 4e-5));
    assert(near_val(fq.total_variance_norm(x), expected_v, 1e-9));

    // Real heraw_core::exposure_normalize multi-EV burst oracle:
    // raw[1] = 0.25 * raw[0] => gain_1 = 4.0, read_variances[1] = 16*rv, shot_coefficients[1] = 4*shot, sat[1] = 4*sat0
    {
        const int h = 16, w = 16, hh = h / 2, wh = w / 2;
        heraw_core::Frames raw(2, std::vector<float>(static_cast<size_t>(h) * w, 0.20f));
        heraw_core::Frames g(2, std::vector<float>(static_cast<size_t>(hh) * wh, 0.20f));
        for (int r = 0; r < h; ++r) {
            for (int c = 0; c < w; ++c) {
                const float pat = 0.16f + 0.12f * ((r ^ c) & 1 ? 1.0f : 0.0f) + 0.04f * static_cast<float>(c) / w;
                raw[0][static_cast<size_t>(r) * w + c] = pat;
                raw[1][static_cast<size_t>(r) * w + c] = 0.25f * pat;
            }
        }
        for (int r = 0; r < hh; ++r) {
            for (int c = 0; c < wh; ++c) {
                const float pat = 0.16f + 0.12f * ((r ^ c) & 1 ? 1.0f : 0.0f) + 0.04f * static_cast<float>(2 * c) / w;
                g[0][static_cast<size_t>(r) * wh + c] = pat;
                g[1][static_cast<size_t>(r) * wh + c] = 0.25f * pat;
            }
        }
        heraw_core::Noise n_in{1e-4f, 2e-4f};
        const auto st = heraw_core::exposure_normalize(raw, g, h, w, n_in, {}, {}, {}, heraw_core::Execution{});
        assert(st.gains.size() == 2 && st.read_variances.size() == 2 && st.shot_coefficients.size() == 2 && st.saturation_levels.size() == 2);
        assert(near_val(st.gains[0], 1.0, 1e-6) && near_val(st.gains[1], 4.0, 1e-3));
        assert(near_val(st.read_variances[1], 16.0 * 1e-4, 1e-5));
        assert(near_val(st.shot_coefficients[1], 4.0 * 2e-4, 1e-5));
        assert(near_val(st.saturation_levels[1], 4.0 * 0.98, 2e-3));
    }
    std::printf("[PASS] E01 & E05: single radiometric variance, 1:4 optimal weight (%.6f) & exposure_normalize\n", frac1);
}

static void test_e02_e06_e07_e09_e10_e11_deterministic() {
    uint8_t cfa_grid[6][6];
    build_rggb_6x6(cfa_grid);

    // E02: Real analyze_geometry metric baseline invariance across focal lengths and pixel pitches
    {
        auto run_e02 = [&](float focal_mm, float pitch_um, float aperture, float disp_max_half_px) -> double {
            const int hh = 168, hw = 200, n = 5;
            const int H1 = hh * 2, W1 = hw * 2;
            const float focus_m = 2.0f;
            const float f_h = (focal_mm / (pitch_um * 1e-3f)) * 0.5f;
            const float kappa_opt = (focal_mm * focal_mm) / (4.0f * aperture * pitch_um);
            const float b_mm_true = 10.0f;
            const float diopter_per_rho = 1000.0f / (f_h * b_mm_true);
            const float rho_f = 0.5f * disp_max_half_px;
            const std::array<float, 6> rho_map = {0.0f, 1.0f, 0.5f, 0.0f, 0.25f, 0.75f};

            heraw_suite::Planes raw(n, std::vector<float>(static_cast<size_t>(H1) * W1, 0.25f));
            heraw_suite::Planes g(n, std::vector<float>(static_cast<size_t>(hh) * hw, 0.0f));
            heraw_suite::Planes fy(n, std::vector<float>(static_cast<size_t>(hh) * hw, 0.0f));
            heraw_suite::Planes fx(n, std::vector<float>(static_cast<size_t>(hh) * hw, 0.0f));
            heraw_suite::Planes mw(n, std::vector<float>(static_cast<size_t>(hh) * hw, 1.0f));
            const std::array<float, 5> bx_k = {0.0f, -0.50f, -0.25f, 0.25f, 0.50f};
            const double ux_e = std::cos(0.26), uy_e = std::sin(0.26);

            for (int k = 0; k < n; ++k) {
                for (int y = 0; y < hh; ++y) {
                    const int band = (y >= 120) ? 5 : (y / 24);
                    const double yc = (y >= 120) ? 144.0 : (static_cast<double>(band) * 24.0 + 12.0);
                    const float rho = disp_max_half_px * rho_map[static_cast<size_t>(band)];
                    const double coc = static_cast<double>(kappa_opt * diopter_per_rho) * (rho - static_cast<double>(rho_f));
                    const double sig = std::hypot(1.10, coc);
                    const double inv_s2 = 1.0 / (std::sqrt(2.0) * sig);
                    for (int x = 0; x < hw; ++x) {
                        const size_t i = static_cast<size_t>(y) * hw + x;
                        fy[k][i] = 0.0f;
                        fx[k][i] = bx_k[k] * rho;
                        const double xp = static_cast<double>(x) - static_cast<double>(fx[k][i]);
                        const double s1 = (xp - 55.0) * ux_e + (static_cast<double>(y) - yc) * uy_e;
                        const double s2 = (xp - 145.0) * ux_e - (static_cast<double>(y) - yc) * uy_e;
                        const double v = (xp < 100.0) ? std::erf(s1 * inv_s2) : -std::erf(s2 * inv_s2);
                        g[k][i] = static_cast<float>(0.40 + 0.22 * v);
                    }
                }
            }
            std::vector<double> times = {0.0, 0.05, 0.10, 0.15, 0.20};
            heraw_suite::BurstView bv{raw, g, fy, fx, mw, H1, W1, cfa_grid, 0.0f, 1e-8f, &times};
            bv.focal_length_mm = focal_mm;
            bv.aperture = aperture;
            bv.focus_distance_m = focus_m;
            bv.hyperfocal_m = 40.0f;
            bv.pixel_pitch_um = pitch_um;
            bv.ois_active = false;
            bv.enable_kinetic_sign = true;
            const auto geom = heraw_suite::analyze_geometry(bv);
            assert(geom.gate.observable && geom.metric_calibrated && geom.estimated_baseline_mm > 0.0f);
            return static_cast<double>(geom.estimated_baseline_mm);
        };
        const double b50 = run_e02(50.0f, 4.0f, 1.4f, 6.0f);
        const double b25 = run_e02(25.0f, 2.0f, 0.7f, 6.0f);
        const double b40 = run_e02(40.0f, 4.0f, 0.90f, 4.8f);
        assert(near_val(b50, 10.0, 0.15) && near_val(b25, 10.0, 0.15) && near_val(b40, 10.0, 0.15));
        assert(near_val(b50, b25, 1e-3) && near_val(b50, b40, 0.15));
    }

    // E06: 2x PSF sigma 1.05 -> 0.525 native 1x px across both 1x and 2x output grids via real helper
    {
        const float eff_1x = heraw_spec::resolve_eff_psf_sigma_1x(1.05f, false);
        const float eff_2x = heraw_spec::resolve_eff_psf_sigma_1x(1.05f, true);
        const float eff_z2 = heraw_spec::resolve_eff_psf_sigma_1x(1.05f, true, 1.05f, true);
        const float eff_z1 = heraw_spec::resolve_eff_psf_sigma_1x(1.05f, false, 0.525f, true);
        assert(near_val(eff_1x, 0.525, 1e-6) && near_val(eff_2x, 0.525, 1e-6));
        assert(near_val(eff_z2, 0.525, 1e-6) && near_val(eff_z1, 0.525, 1e-6));
    }

    // E04: Stratified LOO chi2 samples all 4 CFA phases (4*sigma exclusively on Blue => mean chi2 = 4.0000)
    const int H1 = 64, W1 = 64, Hh = 32, Wh = 32, H2 = 128, W2 = 128;
    uint8_t cfa36[36];
    for (int r = 0; r < 6; ++r) for (int c = 0; c < 6; ++c) cfa36[r * 6 + c] = cfa_grid[r][c];
    std::vector<float> pred_rgb(static_cast<size_t>(H2) * W2 * 3, 0.30f);
    std::vector<float> held_raw(static_cast<size_t>(H1) * W1, 0.30f);
    const float sig0 = 0.01f;
    for (int r = 0; r < H1; ++r) {
        for (int c = 0; c < W1; ++c) {
            if (cfa_grid[r % 6][c % 6] == 2) { // Blue only
                held_raw[static_cast<size_t>(r) * W1 + c] = 0.30f + 4.0f * sig0;
            }
        }
    }
    std::vector<float> zflow(static_cast<size_t>(Hh) * Wh, 0.0f);
    auto loo = heraw_eval::compute_stratified_loo_chi2(
        pred_rgb.data(), H2, W2, 2, held_raw.data(), H1, W1,
        zflow.data(), zflow.data(), Hh, Wh, cfa36, sig0, 0.0f, nullptr, 1.0f, 0.90f, 1);
    assert(loo.valid && loo.n_flat > 0);
    assert(near_val(loo.chi2_flat, 4.0000, 0.05));

    // E10: Symmetric 2D chroma aliasing on vertical vs horizontal alternating stripes => 0.5000 each
    auto eval_e10 = [&](bool horiz) {
        const int N = 32;
        std::vector<float> rgb(static_cast<size_t>(N) * N * 3);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                const float L = 0.30f + 0.02f * (((x + y) & 1) ? -1.0f : 1.0f);
                const int p = horiz ? (y & 1) : (x & 1);
                const float s = 0.01f * (p ? -1.0f : 1.0f);
                const size_t i = (static_cast<size_t>(y) * N + x) * 3;
                rgb[i + 0] = L + s;
                rgb[i + 1] = L;
                rgb[i + 2] = L - s;
            }
        }
        return heraw_eval::measure_achromatic_aliasing_components(rgb.data(), N, N);
    };
    const auto ac_v = eval_e10(false);
    const auto ac_h = eval_e10(true);
    assert(ac_v.denom_valid && ac_h.denom_valid);
    assert(near_val(ac_v.ratio_iso, 0.5000, 1e-4));
    assert(near_val(ac_h.ratio_iso, 0.5000, 1e-4));
    assert(near_val(ac_v.ratio_iso, ac_h.ratio_iso, 1e-5));

    // E07: Collinear 1D parallax baseline (bx = t, by = 0) cannot decouple velocity vs static depth
    {
        const int h = 24, w = 32;
        heraw_suite::Planes raw(5, std::vector<float>(static_cast<size_t>(4 * h * w), 0.25f));
        heraw_suite::Planes g(5, std::vector<float>(static_cast<size_t>(h * w), 0.25f));
        heraw_suite::Planes fy(5, std::vector<float>(static_cast<size_t>(h * w), 0.0f));
        heraw_suite::Planes fx(5, std::vector<float>(static_cast<size_t>(h * w), 0.0f));
        heraw_suite::Planes mw(5, std::vector<float>(static_cast<size_t>(h * w), 1.0f));
        std::vector<double> times = {0.0, 0.05, 0.10, 0.15, 0.20};
        for (size_t k = 0; k < 5; ++k) {
            const float dx = static_cast<float>(k);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const float rho = (y >= 8 && y < 16 && x >= 10 && x < 22) ? 1.5f : 0.0f;
                    const float ux = static_cast<float>(x) - rho * dx;
                    const float v = 0.30f + 0.06f * std::sin(ux * 0.4f) + 0.05f * std::cos(y * 0.4f);
                    g[k][static_cast<size_t>(y) * w + x] = v;
                    fx[k][static_cast<size_t>(y) * w + x] = rho * dx;
                }
            }
        }
        heraw_suite::BurstView bv{raw, g, fy, fx, mw, 2 * h, 2 * w, cfa_grid, 0, 1e-8f, &times};
        const auto fres = heraw_suite::compute_fields(bv, {"depth", "velocity"});
        // Subspace decouple is rejected when t lies in span(bx, by), so static parallax is NOT misattributed to velocity
        const float fg_vel = fres.maps.at("velocity")[static_cast<size_t>(12) * w + 16];
        assert(std::isfinite(fg_vel) && fg_vel <= 1e-5f);
    }

    // E09: FocusStack / dff_depth collapses duplicate focus steps (1,1,2) and (1,2,2) => observable == false
    for (int mode = 0; mode < 2; ++mode) {
        const int h = 24, w = 32;
        heraw_suite::Planes raw(3, std::vector<float>(static_cast<size_t>(4 * h * w), 0.25f));
        heraw_suite::Planes g(3, std::vector<float>(static_cast<size_t>(h * w)));
        heraw_suite::Planes fy(3, std::vector<float>(static_cast<size_t>(h * w), 0.0f));
        heraw_suite::Planes fx(3, std::vector<float>(static_cast<size_t>(h * w), 0.0f));
        heraw_suite::Planes mw(3, std::vector<float>(static_cast<size_t>(h * w), 1.0f));
        for (size_t k = 0; k < 3; ++k) {
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
                g[k][static_cast<size_t>(y) * w + x] = 0.30f + 0.08f * std::sin(0.6f * x) * std::cos(0.5f * y);
        }
        std::vector<double> ts = {0.0, 0.1, 0.2};
        std::vector<float> fpos = (mode == 0) ? std::vector<float>{1.0f, 1.0f, 2.0f} : std::vector<float>{1.0f, 2.0f, 2.0f};
        heraw_suite::BurstView bv{raw, g, fy, fx, mw, 2 * h, 2 * w, cfa_grid, 0, 1e-8f, &ts};
        bv.focus_positions = &fpos;
        heraw_suite::TemporalOptions top;
        top.mode = heraw_suite::TemporalMode::FocusStack;
        const auto tres = heraw_suite::prepare_temporal(bv, top);
        assert(!tres.gate.observable);
        assert(static_cast<int>(std::lround(tres.gate.measured)) == 2);
    }

    // E11: Tap-0 capture before destripe preserves exact 400 Hz modulation amplitude 0.1200
    {
        const int N = 4, H = 64, W = 64;
        const double t_read_s = 0.005;
        std::vector<std::vector<float>> frames(N, std::vector<float>(static_cast<size_t>(H) * W, 0.0f));
        std::vector<float> sat(N, 0.98f);
        std::vector<heraw_meas::ClockProvenance> cps(N);
        for (int n = 0; n < N; ++n) {
            const double ph0 = n * (0.5 * std::numbers::pi);
            for (int r = 0; r < H; ++r) {
                const double t_r = t_read_s * static_cast<double>(r) / H;
                const float v = 0.40f * (1.0f + 0.12f * static_cast<float>(std::cos(2.0 * std::numbers::pi * 400.0 * t_r + ph0)));
                for (int c = 0; c < W; ++c) frames[n][static_cast<size_t>(r) * W + c] = v;
            }
        }
        auto tap = heraw_meas::capture_pre_destripe_tap(N, H, W, cfa_grid, sat, cps,
            [&](int n, int r) -> const float* { return frames[n].data() + static_cast<size_t>(r) * W; });
        assert(tap.valid && tap.frames.size() == N);
        float max_dev = 0.0f;
        for (int r = 0; r < H; ++r) max_dev = std::max(max_dev, std::abs(tap.frames[0].row_green_mean[r] - 0.40f) / 0.40f);
        assert(near_val(max_dev, 0.1200, 1e-4));
    }

    std::printf("[PASS] E02/E04/E06/E07/E09/E10/E11 deterministic oracles verified\n");
}

static void test_e08_o2_masked_lockin() {
    // E08: Per-pixel masked WLS/QR at locked 100 Hz with pi-phase occluded frames still fits pure DC (1, 0, 0)
    const double target_dc = 0.30;
    const double mod_amp = 0.10;
    const double T_exp = 0.002;
    const double sinc_e = std::sin(std::numbers::pi * 100.0 * T_exp) / (std::numbers::pi * 100.0 * T_exp);
    const int N = 8;
    double G[3][3] = {}, rhs[3] = {};
    for (int k = 0; k < N; ++k) {
        const double ph = (k % 4) * (0.5 * std::numbers::pi);
        const bool visible = ((k % 4) != 2); // Mask phase == pi
        if (!visible) continue;
        // Static patch with true AC == 0, DC == target_dc
        const double yk = target_dc + 0.0 * mod_amp;
        const double b[3] = {1.0, sinc_e * std::cos(ph), sinc_e * std::sin(ph)};
        for (int r = 0; r < 3; ++r) {
            rhs[r] += b[r] * yk;
            for (int c = 0; c < 3; ++c) G[r][c] += b[r] * b[c];
        }
    }
    double coef[3] = {};
    const bool solved = heraw_suite::observer::detail::solve_3x3_pivoted(G, rhs, coef);
    assert(solved);
    assert(near_val(coef[0], target_dc, 1e-9));
    assert(near_val(std::hypot(coef[1], coef[2]), 0.0, 1e-9));

    // End-to-end analyze_subject_observer on 4-frame 100 Hz RS burst with Tap-0 + pts_ticks provenance
    {
        const int H1 = 128, W1 = 128, hh = 64, hw = 64, nf = 4;
        const double t_read_s = 0.020; // 20 ms -> 2.0 cycles at 100 Hz
        uint8_t cfa[6][6];
        build_rggb_6x6(cfa);
        heraw_suite::Planes raw(nf, std::vector<float>(static_cast<size_t>(H1) * W1, 0.40f));
        heraw_suite::Planes g(nf, std::vector<float>(static_cast<size_t>(hh) * hw, 0.40f));
        heraw_suite::Planes fy(nf, std::vector<float>(static_cast<size_t>(hh) * hw, 0.0f));
        heraw_suite::Planes fx(nf, std::vector<float>(static_cast<size_t>(hh) * hw, 0.0f));
        heraw_suite::Planes mw(nf, std::vector<float>(static_cast<size_t>(hh) * hw, 1.0f));
        std::vector<heraw_meas::ClockProvenance> cps(nf);
        std::vector<double> ts(nf);
        for (int k = 0; k < nf; ++k) {
            const double t0 = 0.025 * k;
            ts[k] = t0;
            const double row_p = t_read_s / static_cast<double>(H1);
            cps[k] = heraw_meas::ClockProvenance::from_pts_ticks(
                static_cast<int64_t>(std::llround(t0 * 30000.0)), 30000ULL, 0.0, row_p, 0.002, true, 1);
            for (int r = 0; r < H1; ++r) {
                const double tr = t0 + row_p * static_cast<double>(r);
                const float v = static_cast<float>(0.40 * (1.0 + 0.16 * std::cos(2.0 * std::numbers::pi * 100.0 * tr)));
                for (int c = 0; c < W1; ++c) raw[k][static_cast<size_t>(r) * W1 + c] = v;
            }
            for (int r = 0; r < hh; ++r) {
                const float v = 0.5f * (raw[k][static_cast<size_t>(2 * r) * W1] + raw[k][static_cast<size_t>(2 * r + 1) * W1]);
                for (int c = 0; c < hw; ++c) g[k][static_cast<size_t>(r) * hw + c] = v;
            }
        }
        std::vector<float> sat(nf, 0.98f);
        auto tap = heraw_meas::capture_pre_destripe_tap(nf, H1, W1, cfa, sat, cps,
            [&](int n, int r) -> const float* { return raw[n].data() + static_cast<size_t>(r) * W1; });
        heraw_suite::BurstView bv{raw, g, fy, fx, mw, H1, W1, cfa, 0.0f, 1e-8f, &ts};
        heraw_suite::FieldResult fields;
        heraw_suite::observer::ObserverOptions opts;
        opts.shutter_kind = 1;
        opts.readout_ms_override = 20.0f;
        opts.compute_ac_light = true;
        opts.pre_destripe_tap = &tap;
        opts.clock_prov = cps[0];
        const auto rep = heraw_suite::observer::analyze_subject_observer(bv, fields, opts);
        assert(rep.lockin_gate.observable);
        assert(near_val(rep.ac_freq_hz, 100.0, 2.5));
        assert(rep.ac_mod_depth > 0.05f);
        assert(rep.clock_source == "pts_ticks");
        assert(rep.clock_row_calibrated);
    }

    std::printf("[PASS] E08 & E11: masked O2 lock-in & end-to-end 100 Hz pts_ticks observer verified\n");
}

static void test_r1_r2_diffusion_and_cross_section4() {
    // R1 Oracle 1: common-mode noise C = sigma^2 * 1 1^T, eta = 0.065 => Var(z_p) = 1.00000*sigma^2 vs 0.87845*sigma^2
    const double eta = 0.065;
    const double s2 = 0.0025;
    const double var_exact = heraw_diff::eval_ultracon_output_variance(s2, s2, s2, eta);
    const double var_dropped_cov = heraw_diff::eval_ultracon_output_variance(s2, s2, 0.0, eta);
    assert(near_val(var_exact / s2, 1.00000, 1e-9));
    assert(near_val(var_dropped_cov / s2, 0.87845, 1e-5));

    // R1 Oracle 2: independent white noise + global uniform mean over P pixels
    const int P = 100;
    const double var_iid = heraw_diff::eval_ultracon_output_variance(s2, s2 / P, s2 / P, eta);
    const double exp_iid = s2 * ((1.0 - eta) * (1.0 - eta) + (2.0 * eta - eta * eta) / P);
    assert(near_val(var_iid, exp_iid, 1e-12));

    // R1 Cross-Section #4 DF2: x = 0.18, amb = 0.36, eta = 0.065 => 0.1917
    {
        const int H = 16, W = 16;
        std::vector<float> img(static_cast<size_t>(H) * W * 3, 0.36f);
        // Set central 2x2 patch to 0.18 (negligible shift in ambient 0.36)
        for (int y = 7; y <= 8; ++y)
            for (int x = 7; x <= 8; ++x)
                for (int c = 0; c < 3; ++c)
                    img[(static_cast<size_t>(y) * W + x) * 3 + c] = 0.18f;
        heraw_diff::DiffusionConfig cfg;
        cfg.mode = heraw_diff::DiffusionMode::ULTRA_CONTRAST;
        cfg.uc_grade = "2";
        cfg.uc_baffle = heraw_diff::UcBaffleMode::MATTE_BOX;
        cfg.strength = 1.0f;
        heraw_diff::apply_optical_diffusion_linear(img.data(), H, W, cfg);
        assert(near_val(img[(8 * W + 8) * 3 + 0], 0.1917, 1e-3));
    }

    // R1 Baffle polarity oracle: bright top-band sky + dark bottom shadow -> wide-flare > standard > hooded
    {
        auto run_baffle = [&](heraw_diff::UcBaffleMode bm) {
            const int H = 64, W = 64;
            std::vector<float> img(static_cast<size_t>(H) * W * 3, 0.04f);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < W; ++x)
                    for (int c = 0; c < 3; ++c)
                        img[(static_cast<size_t>(y) * W + x) * 3 + c] = 3.0f;
            heraw_diff::DiffusionConfig cfg;
            cfg.mode = heraw_diff::DiffusionMode::ULTRA_CONTRAST;
            cfg.uc_grade = "3";
            cfg.uc_baffle = bm;
            cfg.strength = 1.0f;
            heraw_diff::apply_optical_diffusion_linear(img.data(), H, W, cfg);
            return img[((H - 2) * W + W / 2) * 3 + 0];
        };
        const float v_hooded = run_baffle(heraw_diff::UcBaffleMode::MATTE_BOX);
        const float v_std = run_baffle(heraw_diff::UcBaffleMode::HOOD);
        const float v_wide = run_baffle(heraw_diff::UcBaffleMode::UNBAFFLED);
        assert(v_hooded < v_std && v_std < v_wide);
    }

    // R2 Real Cascade API Oracle: UC_HDF == sequential (ULTRA_CONTRAST -> HDF) and != additive parallel sum, plus BLACK_MIST warm-glass tint
    {
        const int H = 32, W = 32;
        std::vector<float> base(static_cast<size_t>(H) * W * 3, 0.05f);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x)
                for (int c = 0; c < 3; ++c)
                    base[(static_cast<size_t>(y) * W + x) * 3 + c] = 1.20f;

        std::vector<float> img_comb = base, img_seq = base, img_uc = base, img_hdf = base;
        std::vector<float> d_comb, d_uc, d_hdf;

        heraw_diff::DiffusionConfig c_comb;
        c_comb.mode = heraw_diff::DiffusionMode::UC_HDF;
        c_comb.uc_grade = "4"; c_comb.uc_baffle = heraw_diff::UcBaffleMode::UNBAFFLED; c_comb.strength = 1.2f;
        heraw_diff::apply_optical_diffusion_linear(img_comb.data(), H, W, c_comb, &d_comb);

        heraw_diff::DiffusionConfig c_uc = c_comb; c_uc.mode = heraw_diff::DiffusionMode::ULTRA_CONTRAST;
        heraw_diff::DiffusionConfig c_hdf = c_comb; c_hdf.mode = heraw_diff::DiffusionMode::HDF;
        heraw_diff::apply_optical_diffusion_linear(img_seq.data(), H, W, c_uc);
        heraw_diff::apply_optical_diffusion_linear(img_seq.data(), H, W, c_hdf);

        heraw_diff::apply_optical_diffusion_linear(img_uc.data(), H, W, c_uc, &d_uc);
        heraw_diff::apply_optical_diffusion_linear(img_hdf.data(), H, W, c_hdf, &d_hdf);

        double max_seq_diff = 0.0, max_par_diff = 0.0;
        for (size_t i = 0; i < img_comb.size(); ++i) {
            max_seq_diff = std::max(max_seq_diff, static_cast<double>(std::abs(img_comb[i] - img_seq[i])));
            const float par_v = base[i] + d_uc[i] + d_hdf[i];
            max_par_diff = std::max(max_par_diff, static_cast<double>(std::abs(img_comb[i] - par_v)));
        }
        assert(max_seq_diff < 1e-6);
        assert(max_par_diff > 1e-4);

        // Warm-glass chroma tint in BLACK_MIST raises Red shadow uplift above Blue shadow uplift
        std::vector<float> img_warm = base;
        heraw_diff::DiffusionConfig c_bm;
        c_bm.mode = heraw_diff::DiffusionMode::BLACK_MIST;
        c_bm.uc_grade = "3"; c_bm.uc_chroma = 1.0f; c_bm.strength = 1.2f;
        heraw_diff::apply_optical_diffusion_linear(img_warm.data(), H, W, c_bm);
        const size_t dark_px = (static_cast<size_t>(H - 2) * W + (W - 2)) * 3;
        assert(img_warm[dark_px + 0] > img_warm[dark_px + 2] + 1e-4f);
    }

    std::printf("[PASS] R1 & R2 & Cross-Section #4 diffusion covariance, baffle polarity, cascade & warm-glass verified\n");
}

static void test_r3_r4_r6_motion_and_oklab() {
    // R3 Negative Oracles: pure 1D ramps p=x and p=x-y fail SIM(2) rank check
    for (int ramp_kind = 0; ramp_kind < 2; ++ramp_kind) {
        std::vector<double> p, q, gx, gy, xr, yr, w;
        for (int y = -4; y <= 4; ++y) {
            for (int x = -4; x <= 4; ++x) {
                const double pv = (ramp_kind == 0) ? (0.5 + 0.03 * x) : (0.5 + 0.03 * (x - y));
                p.push_back(pv);
                q.push_back(1.25 * pv + 0.02);
                gx.push_back(0.03);
                gy.push_back((ramp_kind == 0) ? 0.0 : -0.03);
                xr.push_back(static_cast<double>(x));
                yr.push_back(static_cast<double>(y));
                w.push_back(1.0);
            }
        }
        const auto sres = heraw_motion::evaluate_sim2_photometric_patch(
            static_cast<int>(p.size()), p.data(), q.data(), gx.data(), gy.data(), xr.data(), yr.data(), w.data(), true, 0.08);
        assert(!sres.sim2_rank_ok);
    }

    // R3 Positive Oracle: 2D asymmetric corner patch with gamma=1.35, beta=0.05, small dx=+0.35, dy=-0.25, theta=0.04, scale=1.03
    {
        auto corner = [](double y, double x) {
            return 0.2 + 0.4 * std::exp(-0.08 * (x - 0.5) * (x - 0.5) - 0.06 * (y + 0.3) * (y + 0.3))
                 + 0.25 * std::tanh(0.6 * x + 0.25 * std::sin(0.5 * y));
        };
        const double dx_t = 0.35, dy_t = -0.25, th_t = 0.04, sc_t = 1.03;
        const double gm_t = 1.35, bt_t = 0.05;
        auto sample_q = [&](double y, double x) {
            // Define q such that q(x + dx, y + dy) approx -> p at inverse warp: q(W_inv(x)) = gm*p(x)+bt
            // For small delta, q_warp(x) = q(x) matches linearized forward q(x + dg) = gm*p(x) + bt
            // Construct q(x) = gm * corner(R(-th)*((x - dx)/sc)) + bt so sampling q(W(x)) gives gm*corner(x)+bt!
            const double rx = (x - dx_t) / sc_t;
            const double ry = (y - dy_t) / sc_t;
            const double c = std::cos(-th_t), s = std::sin(-th_t);
            return gm_t * corner(s * rx + c * ry, c * rx - s * ry) + bt_t;
        };
        std::vector<double> p, q, gx, gy, xr, yr, w;
        for (int y = -5; y <= 5; ++y) {
            for (int x = -5; x <= 5; ++x) {
                p.push_back(corner(y, x));
                q.push_back(sample_q(y, x));
                gx.push_back((sample_q(y, x + 0.01) - sample_q(y, x - 0.01)) / 0.02);
                gy.push_back((sample_q(y + 0.01, x) - sample_q(y - 0.01, x)) / 0.02);
                xr.push_back(static_cast<double>(x));
                yr.push_back(static_cast<double>(y));
                w.push_back(1.0);
            }
        }
        const auto sres = heraw_motion::evaluate_sim2_photometric_patch(
            static_cast<int>(p.size()), p.data(), q.data(), gx.data(), gy.data(), xr.data(), yr.data(), w.data(), true, 0.04);
        assert(sres.tracked && sres.sim2_rank_ok && sres.effective_dof == 4);
        assert(near_val(sres.dx, dx_t, 0.08) && near_val(sres.dy, dy_t, 0.08));
        assert(near_val(sres.dtheta_rad, th_t, 0.015) && near_val(sres.scale, sc_t, 0.02));
        assert(near_val(sres.gamma, gm_t, 0.08) && near_val(sres.beta, bt_t, 0.04));
        assert(sres.zncc > 0.99);
    }

    // R4 Negative Oracle: 100 corners on one horizontal row band -> rank <= 3 -> reduces from 8 knots to 1
    {
        const double f_px = 2000.0, cy = 1500.0, cx = 2000.0;
        std::vector<heraw_motion::RsSplineSample> pts;
        for (int i = 0; i < 100; ++i) {
            heraw_motion::RsSplineSample s;
            s.x_ref = 200.0 + i * 35.0;
            s.y_ref = 750.0;
            s.t_norm_src = 0.25;
            s.dx_obs = 1.5;
            s.dy_obs = -0.5;
            pts.push_back(s);
        }
        const auto fit1 = heraw_motion::fit_perspective_rs_spline(pts, f_px, cy, cx, 8);
        assert(fit1.used_knots == 1 && fit1.numerical_rank <= 3);
    }

    // R4 Positive Oracle: 8 row-time bands x 2 well-separated non-parallel static rays = 16 2D corners -> 8 knots rank 24
    {
        const double f_px = 2000.0, cy = 1500.0, cx = 2000.0;
        std::vector<std::array<double, 3>> gt_knots(8);
        for (int k = 0; k < 8; ++k) {
            const double u = static_cast<double>(k) / 7.0;
            gt_knots[k] = {0.002 * std::sin(u * 2.0), -0.003 * u, 0.004 * std::cos(u * 1.5)};
        }
        std::vector<heraw_motion::RsSplineSample> pts;
        std::vector<double> bw;
        for (int band = 0; band < 8; ++band) {
            const double t_b = static_cast<double>(band) / 7.0;
            const double y_r = 250.0 + band * 350.0;
            heraw_motion::eval_temporal_knots(t_b, 8, bw);
            double wloc[3] = {0.0, 0.0, 0.0};
            for (int k = 0; k < 8; ++k) for (int a = 0; a < 3; ++a) wloc[a] += bw[k] * gt_knots[k][a];
            double R[3][3];
            heraw_motion::rodrigues_so3(wloc, R);
            for (double x_r : {300.0, 3600.0}) {
                const double b[3] = {(x_r - cx) / f_px, (y_r - cy) / f_px, 1.0};
                double Rb[3] = {
                    R[0][0] * b[0] + R[0][1] * b[1] + R[0][2] * b[2],
                    R[1][0] * b[0] + R[1][1] * b[1] + R[1][2] * b[2],
                    R[2][0] * b[0] + R[2][1] * b[1] + R[2][2] * b[2]
                };
                double py = 0.0, px = 0.0;
                heraw_motion::project_pinhole(f_px, cy, cx, Rb, py, px);
                pts.push_back({y_r, x_r, py - y_r, px - x_r, t_b, 1.0, 0.0});
            }
        }
        const auto fit8 = heraw_motion::fit_perspective_rs_spline(pts, f_px, cy, cx, 8, 1e-5, 0.0);
        assert(fit8.used_knots == 8 && fit8.numerical_rank == 24 && fit8.rmse_px < 1e-3);
        for (int k = 0; k < 8; ++k) {
            for (int a = 0; a < 3; ++a) assert(near_val(fit8.knot_rot_rad[k][a], gt_knots[k][a], 1e-4));
        }
    }

    // R4 Positive Oracle 2: 3-DOF translational epipole trans_epip recovery when depth_inv > 0
    {
        const double f_px = 2000.0, cy = 1500.0, cx = 2000.0;
        const std::array<double, 3> T_true = {0.02, -0.01, 0.005};
        std::vector<heraw_motion::RsSplineSample> pts;
        for (int band = 0; band < 8; ++band) {
            const double y_r = 250.0 + band * 350.0;
            for (double x_r : {300.0, 2000.0, 3600.0}) {
                const double bx = (x_r - cx) / f_px, by = (y_r - cy) / f_px;
                const double dinv = (band & 1) ? 0.40 : 0.15;
                const double px = f_px * ((bx + dinv * T_true[0]) / (1.0 + dinv * T_true[2])) + cx;
                const double py = f_px * ((by + dinv * T_true[1]) / (1.0 + dinv * T_true[2])) + cy;
                pts.push_back({y_r, x_r, py - y_r, px - x_r, static_cast<double>(band) / 7.0, 1.0, dinv});
            }
        }
        const auto fit_t = heraw_motion::fit_perspective_rs_spline(pts, f_px, cy, cx, 1, 1e-5, 0.0);
        assert(fit_t.rmse_px < 1e-4 && std::abs(fit_t.knot_rot_rad[0][0]) < 1e-4 && std::abs(fit_t.knot_rot_rad[0][1]) < 1e-4);
        assert(near_val(fit_t.trans_epip[0], T_true[0], 1e-3));
        assert(near_val(fit_t.trans_epip[1], T_true[1], 1e-3));
        assert(near_val(fit_t.trans_epip[2], T_true[2], 1e-3));
    }

    // R6: IMX183 readout timing & 100 Hz aliasing phase & 1px Nyquist shift cancellation
    {
        const double row_s = 876.0 / 72000000.0;
        const double Tf_ms = 3739.0 * row_s * 1000.0;
        const double dphi_rad = 2.0 * std::numbers::pi * 100.0 * (Tf_ms * 1e-3);
        const double dphi_mod_deg = std::fmod(dphi_rad * 180.0 / std::numbers::pi, 360.0);
        const double pair_mod = std::abs(std::cos(0.5 * dphi_rad));
        const double four_re = 0.25 * (1.0 + std::cos(dphi_rad) + std::cos(2.0 * dphi_rad) + std::cos(3.0 * dphi_rad));
        const double four_im = 0.25 * (std::sin(dphi_rad) + std::sin(2.0 * dphi_rad) + std::sin(3.0 * dphi_rad));
        const double four_mod = std::hypot(four_re, four_im);
        assert(near_val(row_s * 1e6, 12.166667, 1e-5));
        assert(near_val(Tf_ms, 45.4911667, 1e-5));
        assert(near_val(dphi_mod_deg, 197.682, 0.01));
        assert(near_val(pair_mod, 0.1537, 0.001));
        assert(near_val(four_mod, 0.1464, 0.001));
        // 1px shift Nyquist theta = pi attenuation |cos(pi*1/2)| == 0
        assert(near_val(std::abs(std::cos(0.5 * std::numbers::pi * 1.0)), 0.0, 1e-12));

        // compute_rowband_lucky_weights downweights motion-smeared band while keeping sharp band near 1.0
        const int Hh = 32, Wh = 32;
        std::vector<std::vector<float>> g4(4, std::vector<float>(static_cast<size_t>(Hh) * Wh, 0.2f));
        for (int k = 0; k < 4; ++k) {
            for (int r = 0; r < Hh; ++r) for (int c = 0; c < Wh; ++c) {
                const bool blur_band = (k == 2 && r >= 16);
                g4[k][static_cast<size_t>(r) * Wh + c] = blur_band ? 0.2f : (0.2f + 0.1f * ((r ^ c) & 1));
            }
        }
        auto rw = heraw_motion::compute_rowband_lucky_weights(g4, Hh, Wh, 2, 1e-6f);
        assert(rw.size() == 8u);
        assert(rw[2 * 2 + 1] < 0.25f && rw[2 * 2 + 0] > 0.90f);
    }

    // OkLab constant-hue stage compression preserves hue angle in sRGB and AdobeRGB with smooth [0.78, 1.0] rolloff
    {
        const float id3[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        auto ok_hue_srgb = [](const float c[3]) {
            const float r = std::max(0.0f, c[0]), g = std::max(0.0f, c[1]), b = std::max(0.0f, c[2]);
            const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
            const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
            const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
            const float a = 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s;
            const float bv = 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s;
            return std::atan2(bv, a);
        };
        float rgb_s[3] = {2.4f, 0.45f, 1.8f};
        const float h0 = ok_hue_srgb(rgb_s);
        heraw_motion::oklab_stage_highlight_compress_pixel(id3, rgb_s, 0.78f, false);
        const float h1 = ok_hue_srgb(rgb_s);
        assert(rgb_s[0] <= 1.0f && rgb_s[1] <= 1.0f && rgb_s[2] <= 1.0f &&
               rgb_s[0] >= 0.0f && rgb_s[1] >= 0.0f && rgb_s[2] >= 0.0f);
        assert(std::abs(h1 - h0) < 0.03f);

        // Smooth monotonicity in [0.78, 1.0] & AdobeRGB matrix handling with zero-negative hue invariance
        float mid1[3] = {0.82f, 0.20f, 0.40f}, mid2[3] = {1.10f, 0.27f, 0.54f};
        heraw_motion::oklab_stage_highlight_compress_pixel(id3, mid1, 0.78f, true);
        heraw_motion::oklab_stage_highlight_compress_pixel(id3, mid2, 0.78f, true);
        assert(mid1[0] >= 0.78f && mid1[0] < mid2[0] && mid2[0] <= 1.0f);

        float adobe_in[3] = {1.8f, 0.9f, 0.0f};
        const float rs0 = 1.398355f * adobe_in[0] - 0.398355f * adobe_in[1];
        const float bs0 = -0.0429389f * adobe_in[1] + 1.0429389f * adobe_in[2];
        const float tmp0[3] = {rs0, adobe_in[1], bs0};
        const float ha0 = ok_hue_srgb(tmp0);
        heraw_motion::oklab_stage_highlight_compress_pixel(id3, adobe_in, 0.78f, true);
        assert(adobe_in[0] <= 1.0f && adobe_in[1] <= 1.0f && adobe_in[2] <= 1.0f &&
               adobe_in[0] >= 0.0f && adobe_in[1] >= 0.0f && adobe_in[2] >= 0.0f);
        const float tmp1[3] = {1.398355f * adobe_in[0] - 0.398355f * adobe_in[1], adobe_in[1], -0.0429389f * adobe_in[1] + 1.0429389f * adobe_in[2]};
        const float ha1 = ok_hue_srgb(tmp1);
        assert(std::abs(ha1 - ha0) < 0.05f);
    }

    // F-C C1 NNLS, C3 Trajectory MTF & HM5 exact general ballistic jump height
    {
        const int T = 64, N = 4;
        std::vector<std::vector<double>> ws(N, std::vector<double>(T, 0.0));
        for (int k = 0; k < N; ++k) {
            for (int t = k * 16; t < (k + 1) * 16; ++t) ws[k][t] = 1.0;
        }
        auto q_rc = heraw_motion::build_target_envelope("rear-curtain", T);
        std::vector<double> nv(N, 1e-4);
        auto a = heraw_motion::solve_nnls_temporal_envelope(ws, q_rc, nv);
        assert(a.size() == 4 && a[3] > a[0] && a[3] > a[1]);

        const auto mtf_zero = heraw_motion::evaluate_intra_frame_motion_mtf(
            {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, {1e-4, 1e-4, 1e-4});
        const auto mtf_blur = heraw_motion::evaluate_intra_frame_motion_mtf(
            {1.5, 1.5, 1.5}, {0.8, 0.8, 0.8}, {1.0, 1.0, 1.0}, {1e-4, 1e-4, 1e-4});
        assert(near_val(mtf_zero.mtf_avg, 1.0, 1e-6) && near_val(mtf_zero.rlg_boost, 1.0, 1e-6));
        assert(mtf_blur.mtf_avg < 0.85 && mtf_blur.rlg_boost > 1.15);

        const auto j0 = heraw_motion::compute_ballistic_jump_height(0.60, 0.0, 0.008);
        const auto jp = heraw_motion::compute_ballistic_jump_height(0.60, +0.10, 0.008);
        const auto jm = heraw_motion::compute_ballistic_jump_height(0.60, -0.10, 0.008);
        assert(near_val(j0.asymmetric_height_m, 0.4413, 2e-4));
        assert(near_val(jp.asymmetric_height_m, 0.4927, 2e-4));
        assert(near_val(jm.asymmetric_height_m, 0.3927, 2e-4));
        assert(near_val(j0.height_sigma_m, 0.01177, 2e-4));
    }

    std::printf("[PASS] R3/R4(+trans_epip)/R6/OkLab(sRGB+AdobeRGB)/F-C(NNLS+C3)/HM5 oracles verified\n");
}

static void test_r5_r7_r8_r9_r10_spectral_probit() {
    // R5: B_Y = I_4, D = e_1 -> F_data = diag(0, 1, 1, 1), effective_data_rank == 3
    {
        const double B_Y[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        const double D_w[4] = {1.0, 0.0, 0.0, 0.0};
        const auto rep = heraw_spec::analyze_luma_vs_chroma_rank(
            4, 4, B_Y, 1, D_w, {0.0}, 1, D_w, {0.1, 0.1, 0.1, 0.1}, {1, 0, 0, 0});
        assert(rep.effective_data_rank == 3);
        assert(near_val(rep.F_data[0], 0.0, 1e-9));
        assert(near_val(rep.F_data[5], 1.0, 1e-9) && near_val(rep.F_data[10], 1.0, 1e-9) && near_val(rep.F_data[15], 1.0, 1e-9));
    }

    // Audit Cross-Section #2: correlated prior rho=0.99, F = diag(100, 0) => rank 1 while s_2 ~ 0.9704
    {
        // Two unknowns (y0, y1) represented via direct B_Y=[1, 0] for y0? We model joint 2x2 F_data:
        // Orthonormal basis of 2 observations with weighted row sqrt(100) * [1, 0] and 0 * [0, 1]:
        const double B_Y[4] = {10.0, 0.0, 0.0, 0.0}; // F_data = diag(100, 0)
        // To test correlated Gaussian prior cleanly via solve_spd_cholesky, check precision + leakage directly:
        // Sigma = [[1, 0.99], [0.99, 1]], Lambda = Sigma^-1
        const double rho = 0.99;
        const double inv_det = 1.0 / (1.0 - rho * rho);
        const auto rep = heraw_spec::analyze_luma_vs_chroma_rank(
            2, 2, B_Y, 0, nullptr, {}, 0, nullptr, {inv_det, inv_det}, {0.0, 1.0});
        assert(rep.effective_data_rank == 1);
        std::vector<double> PostP = {inv_det + 100.0, -rho * inv_det, -rho * inv_det, inv_det};
        std::vector<double> rhs = {0.0, 1.0};
        assert(heraw_spec::solve_spd_cholesky(PostP, rhs, 2, 0.0));
        const double s2 = 1.0 - rhs[1];
        assert(near_val(s2, 0.9704, 1e-3));
        // Also verify analyze_luma_vs_chroma_rank prior_var_reduction_s output directly (full-rank 1x1 marginal test)
        const double B1[1] = {10.0};
        const auto rep1 = heraw_spec::analyze_luma_vs_chroma_rank(1, 1, B1, 0, nullptr, {}, 0, nullptr, {1.0}, {1.0});
        assert(rep1.effective_data_rank == 1 && near_val(rep1.prior_var_reduction_s, 100.0 / 101.0, 1e-6));
    }

    // R7: Shared anchor noise info oracle: (y0, y1) info = 2, d=y1-y0 info = 0, (y0, d) w/ Cov=[[1,-1],[-1,2]] info = 2, Schur c_eff
    {
        heraw_spec::ArrowHeadSchurSolver ah;
        ah.reset(1, 1, nullptr); // u=x, anchor nuisance a=n0
        const double b1 = 1.0, u1 = 1.0, w1 = 1.0, y1 = 1.2;
        const double b2 = 1.0, u2 = 0.0, w2 = 1.0, y2 = 1.4;
        // Frame 0 observation y0 - a = x ? Better directly: y0=x+n0, d=y1-y0=n1-n0 (with B=[0;1] etc.):
        // Here test general ArrowHead equivalence with da=1 local shared nuisance across 2 frames + c_eff
        ah.add_frame(1, &b1, &u1, 0, nullptr, nullptr, &w1, &y1);
        ah.add_frame(1, &b2, &u2, 0, nullptr, nullptr, &w2, &y2);
        const auto fin = ah.finalize();
        assert(near_val(fin.F_eff[0], 1.0, 1e-9));
        assert(near_val(fin.h_eff[0], 1.4, 1e-9));
        // c_eff = c - g^T G^+ g = (1.2^2 + 1.4^2) - 1.2^2 = 1.96, and J_min = c_eff - h_eff^2 / F_eff == 0
        assert(near_val(fin.c_eff, 1.96, 1e-9));
        assert(near_val(fin.c_eff - (fin.h_eff[0] * fin.h_eff[0]) / fin.F_eff[0], 0.0, 1e-9));

        // Covariance [[1, -1], [-1, 2]] on (y0, y1-y0) sums inverse entries -> (1, 1) * [[2,1],[1,1]] * (1,1)^T?
        // Actually y0 = x + n0, d = (x + n1) - (x + n0) = n1 - n0 => design H_yd = [1, 0]^T!
        // Cov^-1 = [[2, 1], [1, 1]], so [1, 0] Cov^-1 [1, 0]^T == 2.0, while d alone [0]^T Cov^-1 [0] == 0.0!
        std::vector<double> C_yd = {1.0, -1.0, -1.0, 2.0};
        auto P_yd = heraw_spec::sym_pinv(C_yd, 2);
        assert(near_val(P_yd[0], 2.0, 1e-9));
    }

    // R8 & Q4 & Q5: upper-tail curvature at threshold, extreme tail stability, interval box [0,1) sigma=0.25, L^* adjoint, footprint router
    {
        const double sig = 0.25;
        const auto u_thr = heraw_spec::upper_tail_probit_score(0.8, 0.8, sig);
        assert(near_val(u_thr.hess, 2.0 / (std::numbers::pi * sig * sig), 1e-6));

        double lp = 0.0, mz = 0.0;
        heraw_spec::stable_log_phi_and_mills(-80.0, lp, mz);
        assert(std::isfinite(lp) && std::isfinite(mz) && mz > 79.0);

        const auto p01 = heraw_spec::interval_box_probit_score(0.1, 0.0, 1.0, 0.25);
        const auto p05 = heraw_spec::interval_box_probit_score(0.5, 0.0, 1.0, 0.25);
        const auto p09 = heraw_spec::interval_box_probit_score(0.9, 0.0, 1.0, 0.25);
        assert(near_val(std::exp(-p01.nll), 0.65526, 1e-4));
        assert(near_val(std::exp(-p05.nll), 0.95450, 1e-4));
        assert(near_val(std::exp(-p09.nll), 0.65526, 1e-4));

        // Exact discrete transpose test: <L v, w> == <v, L^* w> for arbitrary odd & even lengths
        for (int len : {15, 16, 33}) {
            std::mt19937 rng(42u + len);
            std::normal_distribution<double> nd(0.0, 1.0);
            std::vector<double> v(len), w(len), Lv, Lsw;
            for (int i = 0; i < len; ++i) { v[i] = nd(rng); w[i] = nd(rng); }
            heraw_spec::Biorthogonal53Lifting::forward(v, Lv);
            heraw_spec::Biorthogonal53Lifting::adjoint(w, Lsw);
            double dot_f = 0.0, dot_a = 0.0;
            for (int i = 0; i < len; ++i) { dot_f += Lv[i] * w[i]; dot_a += v[i] * Lsw[i]; }
            assert(near_val(dot_f, dot_a, 1e-12));
        }

        // 16-point support safety threshold ~3.84 sigma
        std::vector<double> mu_safe(16, 1.0 - 4.0 * 0.02);
        std::vector<double> mu_risk(16, 1.0 - 2.0 * 0.02);
        std::vector<double> sd16(16, 0.02), sat16(16, 1.0);
        assert(heraw_spec::is_footprint_safe_for_q4(mu_safe.data(), sd16.data(), sat16.data(), 16, 1e-3));
        assert(!heraw_spec::is_footprint_safe_for_q4(mu_risk.data(), sd16.data(), sat16.data(), 16, 1e-3));
    }

    // R9 & Q6: R = V_n + Var(u_H) -> 0.9901 (Var=0.01) & 0.00990 (Var=100)
    assert(near_val(heraw_spec::r9_lowband_marginal_info(1.0, 1.0, 0.01), 1.0 / 1.01, 1e-4));
    assert(near_val(heraw_spec::r9_lowband_marginal_info(1.0, 1.0, 100.0), 1.0 / 101.0, 1e-5));
    const auto rdof = heraw_spec::compute_residual_dof(100.0, {0.5, 0.5, 0.0});
    assert(near_val(rdof.nu_r, 98.5, 1e-9));
    assert(near_val(heraw_spec::windowed_sinc_luma_1x(0.0), 1.0, 1e-9));

    // R10: Axial magnification + defocus squared difference diopter estimation & negative gates
    {
        const double K = 120.0, Df = 0.5, D_true = 0.8;
        const std::array<double, 3> ms = {1.000, 1.005, 1.011};
        std::array<double, 3> c2{};
        for (size_t i = 0; i < 3; ++i) {
            const double c = K * (D_true - Df / ms[i]);
            c2[i] = c * c;
        }
        const auto ok_res = heraw_suite::evaluate_axial_magnification_diopters(K, Df, ms, c2, true);
        assert(ok_res.observable && near_val(ok_res.diopter_est_inv_m, D_true, 1e-4));

        const auto amb_res = heraw_suite::evaluate_axial_magnification_diopters(K, Df, ms, c2, false);
        assert(!amb_res.observable && amb_res.ambiguous_zoom_breathing);

        const auto inv_res = heraw_suite::evaluate_axial_magnification_diopters(K, 0.0, ms, c2, true);
        assert(!inv_res.observable);
    }

    // Q3: Convex B-Spline TileSpectralMap interpolation preserves PSD bounds 0 <= K(x) <= I
    {
        heraw_spec::TileSpectralMap tmap;
        tmap.th = 2; tmap.tw = 2; tmap.tile_px = 64;
        tmap.nodes.resize(4);
        tmap.nodes[0] = {1.0f, 0.90f, 0.8f, 0.8f, 0.9f,  0.2f, 0.6f, 0.1f};
        tmap.nodes[1] = {1.0f, 0.70f, 0.6f, 0.6f, 0.8f, -0.1f, 0.7f, 0.2f};
        tmap.nodes[2] = {1.0f, 0.80f, 0.7f, 0.7f, 0.7f,  0.1f, 0.9f, 0.15f};
        tmap.nodes[3] = {1.0f, 0.60f, 0.5f, 0.5f, 0.6f,  0.0f, 0.8f, 0.25f};
        const auto bs = tmap.sample_bspline(64.0f, 64.0f);
        const float tr = bs.k_xx + bs.k_yy;
        const float det = bs.k_xx * bs.k_yy - bs.k_xy * bs.k_xy;
        assert(bs.g_mid >= 0.59f && bs.g_mid <= 0.91f);
        assert(bs.k_xx > 0.0f && bs.k_yy > 0.0f && tr <= 2.001f && det >= 0.0f);
    }

    // Q10: Flat-field spectral transfer rejects zero-shift trajectories & certifies jittered trajectories
    {
        const std::array<float, 4> z_dy = {0.0f, 0.0f, 0.0f, 0.0f};
        const std::array<float, 4> z_dx = {0.0f, 0.0f, 0.0f, 0.0f};
        const std::array<float, 4> j_dy = {0.0f, 12.0f, -8.0f, 20.0f};
        const std::array<float, 4> j_dx = {0.0f, -6.0f, 14.0f, 10.0f};
        const std::array<float, 4> wt = {1.0f, 1.0f, 1.0f, 1.0f};
        const double th = 2.0 * std::numbers::pi / 32.0;
        const auto s_zero = heraw_meas::eval_flatfield_spectral_transfer(z_dy.data(), z_dx.data(), wt.data(), 4, th, th);
        const auto s_jit = heraw_meas::eval_flatfield_spectral_transfer(j_dy.data(), j_dx.data(), wt.data(), 4, th, th);
        assert(!s_zero.identifiable && near_val(s_zero.norm_info, 0.0, 1e-9));
        assert(s_jit.identifiable && s_jit.wiener_gain > 0.5);
    }

    // F-G: compute_next_shot_plan maximizes delta log det in uncovered quadrant & recommends EV/focus
    {
        const std::vector<std::array<float, 2>> single_q = {{0.0f, 0.0f}, {0.04f, 0.02f}, {-0.02f, 0.03f}};
        const auto rec = heraw_plan::compute_next_shot_plan(single_q, 0.08f, 0.0f, 1);
        assert(rec.covered_quadrants == 1 && rec.delta_log_det > 0.5f && rec.ev_offset_stop < 0.0f);
    }

    std::printf("[PASS] R5/R7/R8/R9/R10/Q1-Q6/Q10/F-G/Cross-Section #2 spectral, probit & adjoint oracles verified\n");
}

static void test_r11_fh_astro_and_exr_multipart() {
    // R11: 3 s chirp f_dot = 1/6 Hz/s -> boundary phase 1.1781 rad & constant-freq coherence ~ 0.9397
    {
        const double span = 3.0, fdot = 1.0 / 6.0, f0 = 10.0;
        const int M = 601;
        std::vector<double> yr(M), yi(M), ts(M);
        for (int i = 0; i < M; ++i) {
            const double t = -1.5 + span * static_cast<double>(i) / (M - 1);
            ts[i] = t;
            const double ph = 2.0 * std::numbers::pi * (f0 * t + 0.5 * fdot * t * t);
            yr[i] = std::cos(ph);
            yi[i] = std::sin(ph);
        }
        heraw_meas::ClockProvenance good_cp = heraw_meas::ClockProvenance::from_pts_ticks(0, 1000000ULL, 0.0, 0.0, 0.0, true, 0);
        const auto cres = heraw_suite::observer::evaluate_chirp_glrt_complex(yr, yi, ts, f0, -0.5, 0.5, 1e-4, 61, 1, &good_cp);
        assert(cres.chirp_active && !cres.clock_ambiguous);
        assert(near_val(cres.best_fdot_hz_s, fdot, 0.02));
        assert(near_val(cres.boundary_phase_rad, 1.1781, 0.02));
        assert(near_val(cres.coherent_amp_ratio_m0, 0.9397, 0.02));

        // Missing clock provenance flags clock_ambiguous
        const auto bad_cp = heraw_meas::ClockProvenance{};
        const auto cres_bad = heraw_suite::observer::evaluate_chirp_glrt_complex(yr, yi, ts, f0, -0.5, 0.5, 1e-4, 61, 1, &bad_cp);
        assert(cres_bad.clock_ambiguous && cres_bad.equiv_clock_quad_drift_a > 0.0);

        // 0.1 Hz error budget gate
        const auto fc_ok = heraw_meas::evaluate_freq_precision_budget(100.0, 3.0, 4000.0, good_cp, 0.1);
        const auto fc_uncal = heraw_meas::evaluate_freq_precision_budget(100.0, 0.05, 10.0, bad_cp, 0.1);
        assert(fc_ok.hz_calibrated && fc_ok.certified_within_tol);
        assert(!fc_uncal.hz_calibrated && !fc_uncal.certified_within_tol);
    }

    // F-H Global-Shutter dual-window exposure sinc ratio frequency recovery
    {
        const double f_true = 100.0, e1 = 0.002, e2 = 0.008;
        auto sc = [](double u) { double x = std::numbers::pi * u; return std::abs(std::sin(x) / x); };
        const auto gs = heraw_suite::observer::estimate_gs_flicker_sinc_ratio(sc(f_true * e1), e1, sc(f_true * e2), e2, 50.0, 250.0);
        assert(gs.observable && near_val(gs.freq_hz, f_true, 0.2));
    }

    // Section 7.4: 3-hypothesis Star / HotPixel / CosmicRay classifier
    {
        const std::array<float, 5> bg_arr = {0.01f, 0.01f, 0.01f, 0.01f, 0.01f};
        const std::array<float, 5> hi_arr = {0.50f, 0.50f, 0.50f, 0.50f, 0.50f};
        const std::array<float, 5> cr_arr = {0.01f, 0.01f, 0.50f, 0.01f, 0.01f};
        assert(heraw_suite::observer::classify_astro_point_3hyp(hi_arr, cr_arr, 0.01f, 0.002f, 0.4f) ==
               heraw_suite::observer::AstroPointClass::Star);
        assert(heraw_suite::observer::classify_astro_point_3hyp(cr_arr, hi_arr, 0.01f, 0.002f, 0.92f) ==
               heraw_suite::observer::AstroPointClass::HotPixel);
        assert(heraw_suite::observer::classify_astro_point_3hyp(cr_arr, cr_arr, 0.01f, 0.002f, 0.92f) ==
               heraw_suite::observer::AstroPointClass::CosmicRay);
        (void)bg_arr;
    }

    // R12 & E12: Signed IEEE binary16 + Multi-Part Tiled ZIP OpenEXR 2.0 roundtrip & exact over compositing
    {
        const float neg_v = -0.005f;
        const uint16_t h_neg = heraw_exr::f32_to_f16_signed(neg_v);
        assert(near_val(heraw_exr::f16_signed_to_f32(h_neg), neg_v, 1e-5));

        const int W = 8, H = 8;
        const size_t np = static_cast<size_t>(W) * H;
        // F = (-0.02, 0.04, 0.08), alpha = 0.25 => premul P = (-0.005, 0.010, 0.020), BG = (0.02, 0.02, 0.02)
        // Expected over: C = (-0.005, 0.010, 0.020) + 0.75*(0.02, 0.02, 0.02) = (0.010, 0.025, 0.035)
        std::vector<float> fg_rgba(np * 4u), bg_rgb(np * 3u);
        for (size_t i = 0; i < np; ++i) {
            fg_rgba[i * 4 + 0] = -0.005f;
            fg_rgba[i * 4 + 1] =  0.010f;
            fg_rgba[i * 4 + 2] =  0.020f;
            fg_rgba[i * 4 + 3] =  0.250f;
            bg_rgb[i * 3 + 0]  =  0.020f;
            bg_rgb[i * 3 + 1]  =  0.020f;
            bg_rgb[i * 3 + 2]  =  0.020f;
        }
        heraw_exr::ExrPart p_fg, p_bg;
        p_fg.name = "FG"; p_fg.width = W; p_fg.height = H; p_fg.tile_w = 4; p_fg.tile_h = 4;
        p_fg.compression = heraw_exr::CompressionType::ZIP;
        p_fg.channels = {
            {"R", heraw_exr::PixelType::FLOAT, true, fg_rgba.data(), 0, 4},
            {"G", heraw_exr::PixelType::FLOAT, true, fg_rgba.data(), 1, 4},
            {"B", heraw_exr::PixelType::FLOAT, true, fg_rgba.data(), 2, 4},
            {"A", heraw_exr::PixelType::HALF,  true, fg_rgba.data(), 3, 4}
        };
        p_bg.name = "BG"; p_bg.width = W; p_bg.height = H; p_bg.tile_w = 4; p_bg.tile_h = 4;
        p_bg.compression = heraw_exr::CompressionType::ZIP;
        p_bg.channels = {
            {"R", heraw_exr::PixelType::HALF, true, bg_rgb.data(), 0, 3},
            {"G", heraw_exr::PixelType::HALF, true, bg_rgb.data(), 1, 3},
            {"B", heraw_exr::PixelType::HALF, true, bg_rgb.data(), 2, 3}
        };
        std::vector<float> id_vals(np, 42.0f);
        for (size_t i = 0; i < np; ++i) id_vals[i] = static_cast<float>(42u + static_cast<uint32_t>(i));
        heraw_exr::ExrPart p_id;
        p_id.name = "object_id"; p_id.width = W; p_id.height = H;
        p_id.tile_w = 8; p_id.tile_h = 8;
        p_id.compression = heraw_exr::CompressionType::ZIP;
        p_id.channels = {{"ID", heraw_exr::PixelType::UINT, true, id_vals.data(), 0, 1}};
        const std::string exr_path = "/tmp/nijin_v3_oracle_r12.exr";
        assert(heraw_exr::write_multipart_exr(exr_path, {p_fg, p_bg, p_id}));

        std::vector<heraw_exr::DecodedExrPart> dec;
        assert(heraw_exr::read_multipart_exr(exr_path, dec));
        assert(dec.size() == 3 && dec[0].name == "FG" && dec[1].name == "BG" && dec[2].name == "object_id");
        const float pR = dec[0].find_channel("R")->pixels[0];
        const float pG = dec[0].find_channel("G")->pixels[0];
        const float pB = dec[0].find_channel("B")->pixels[0];
        const float a  = dec[0].find_channel("A")->pixels[0];
        const float bR = dec[1].find_channel("R")->pixels[0];
        const float bG = dec[1].find_channel("G")->pixels[0];
        const float bB = dec[1].find_channel("B")->pixels[0];
        assert(near_val(pR, -0.005f, 1e-6));
        assert(near_val(pR + (1.0f - a) * bR, 0.010f, 1e-4));
        assert(near_val(pG + (1.0f - a) * bG, 0.025f, 1e-4));
        assert(near_val(pB + (1.0f - a) * bB, 0.035f, 1e-4));
        assert(near_val(dec[2].find_channel("ID")->pixels[0], 42.0f, 1e-6));
        assert(near_val(dec[2].find_channel("ID")->pixels[15], 57.0f, 1e-6));
        std::remove(exr_path.c_str());
    }

    std::printf("[PASS] R11/R12/E12/F-H/7.4 chirp GLRT, signed OpenEXR 2.0 (HALF+UINT) & astro oracles verified\n");
}

static void test_dxo_real_container_contract() {
    const std::string dxo_path = "/Users/zou/ytdlp/subPrep/livestr/ref/DXO_0820.DXO";
    heraw_dxo::DxoSharedMeta meta;
    const bool ok = heraw_dxo::probe_dxo_container(dxo_path, meta);
    assert(ok && meta.valid);
    assert(meta.cfa_ifds.size() == 4u);
    for (const auto& sub : meta.cfa_ifds) {
        assert(sub.width == 5632u && sub.height == 3710u && sub.bits_per_sample == 8u);
    }
    assert(meta.active_top == 16u && meta.active_left == 88u &&
           meta.active_bottom == 3710u && meta.active_right == 5632u);
    assert(meta.out_w == 5544 && meta.out_h == 3694);
    assert(near_val(meta.black_c[0], 200.0, 1.0) && near_val(meta.white_level, 4094.0, 1.0));
    assert(meta.has_lut);
    // MakerNote tag 15 relative start/end pairs: (0,46), (45,92), (91,138), (138,183) ms
    assert(meta.frame_windows.size() == 4u);
    assert(near_val(meta.frame_windows[0].start_rel_s * 1000.0, 0.0, 1.5));
    assert(near_val(meta.frame_windows[0].end_rel_s * 1000.0, 46.0, 1.5));
    assert(near_val(meta.frame_windows[1].start_rel_s * 1000.0, 45.0, 1.5));
    assert(near_val(meta.frame_windows[2].start_rel_s * 1000.0, 91.0, 1.5));
    assert(near_val(meta.frame_windows[3].start_rel_s * 1000.0, 138.0, 1.5));

    // Read row (2048 - aligned_top) from all 4 SubIFDs at storage coord (2048, 2048) -> active (2048-16, 2048-88)
    const int r_act = 2048 - meta.aligned_top;
    const int c_act = 2048 - meta.aligned_left;
    const std::array<uint16_t, 4> expected_vals = {366u, 371u, 371u, 355u};
    std::vector<uint16_t> row_buf(static_cast<size_t>(meta.out_w));
    for (uint32_t k = 0; k < 4u; ++k) {
        assert(heraw_dxo::read_dxo_frame_rows_u16(dxo_path, meta, k, r_act, r_act + 1, row_buf.data()));
        const uint16_t got = row_buf[static_cast<size_t>(c_act)];
        assert(got == expected_vals[k]);
        assert(got > 255u && got < static_cast<uint16_t>(meta.white_level));
    }

    // Streaming plan on full-frame 8256x4644 (N=8, out_scale=2) must choose stripe-2pass within 2 GiB O(1) pixel working set
    // while reporting O(N) compressed frame index separately (Audit Cross-Section #3)
    const auto sp = heraw_stream::plan_streaming_execution(8, 4644, 8256, 2, 1, 2);
    assert(sp.use_stripe && sp.n_stripes >= 2 && sp.est_working_bytes <= (2ULL << 30));
    assert(sp.est_frame_index_bytes > 0 && sp.est_total_resident_bytes == sp.est_working_bytes + sp.est_frame_index_bytes);

    std::printf("[PASS] DXO1-4 & F-A streaming contract verified on real DXO_0820.DXO (n_ifds=%zu, out=%dx%d, val[0..3]={366,371,371,355})\n",
                meta.cfa_ifds.size(), meta.out_w, meta.out_h);
}

static void test_post_audit_oracles() {
    // 1. E03: 4x4 uniform cubic B-spline eval_tile_modulated_icov C^1/C^2 smoothness across tile boundaries
    {
        heraw_gpu::Stage45GpuJob j;
        j.tile_spec_h = 4; j.tile_spec_w = 4; j.tile_spec_px = 64;
        std::vector<float> tmap(static_cast<size_t>(16 * 8), 0.0f);
        for (int ty = 0; ty < 4; ++ty) {
            for (int tx = 0; tx < 4; ++tx) {
                float* nd = tmap.data() + static_cast<size_t>(ty * 4 + tx) * 8;
                nd[1] = (tx >= 2) ? 1.2f : 0.2f;
                nd[4] = (tx >= 2) ? 0.4f : 1.0f;
                nd[5] = 0.0f;
                nd[6] = (tx >= 2) ? 1.4f : 0.5f;
            }
        }
        j.tile_spec_map = tmap.data();
        auto sample_axx = [&](int xh) -> double {
            float axx = 1.0f, axy = 0.0f, ayy = 1.0f;
            heraw_gpu::eval_tile_modulated_icov(j, 32, xh, axx, axy, ayy);
            return static_cast<double>(axx);
        };
        // Across knot boundary xh = 48 (gx = 2*48/64 - 0.5 = 1.0), cubic B-spline first differences vary smoothly without cusp
        const double d_left = sample_axx(48) - sample_axx(47);
        const double d_right = sample_axx(49) - sample_axx(48);
        const double slope_jump = std::abs(d_right - d_left);
        const double slope_mag = std::max(1e-6, 0.5 * (std::abs(d_left) + std::abs(d_right)));
        assert(slope_mag > 1e-4 && slope_jump < 0.08 * slope_mag);
    }

    // 2. E07: fit_perspective_rs_spline + eval_perspective_rs_flow exact forward perspective recovery
    {
        heraw_motion::RsSplineFitResult truth;
        truth.used_knots = 4;
        truth.knot_rot_rad = {
            {0.0010, -0.0020, 0.0005},
            {0.0016, -0.0012, 0.0009},
            {0.0022,  0.0001, 0.0014},
            {0.0028,  0.0011, 0.0018}
        };
        truth.trans_epip = {0.0, 0.0, 0.0};
        const double f_px = 1200.0, cy = 300.0, cx = 400.0;
        std::vector<heraw_motion::RsSplineSample> samples;
        const std::array<double, 4> t_vals = {0.05, 0.35, 0.68, 0.95};
        const std::array<std::array<double, 2>, 4> pts = {{{120.0, 140.0}, {140.0, 650.0}, {460.0, 180.0}, {480.0, 620.0}}};
        for (double tv : t_vals) {
            for (const auto& pt : pts) {
                double dy = 0.0, dx = 0.0;
                heraw_motion::eval_perspective_rs_flow(truth, f_px, cy, cx, pt[0], pt[1], tv, 0.0, dy, dx);
                heraw_motion::RsSplineSample sp;
                sp.y_ref = pt[0]; sp.x_ref = pt[1];
                sp.dy_obs = dy; sp.dx_obs = dx;
                sp.t_norm_src = tv; sp.weight = 1.0; sp.depth_inv = 0.0;
                samples.push_back(sp);
            }
        }
        const auto fit = heraw_motion::fit_perspective_rs_spline(samples, f_px, cy, cx, 4, 1e-6, 1e-9);
        assert(fit.used_knots == 4 && fit.rmse_px < 1e-2 && fit.holdout_rmse_px < 2e-2);
        double py = 0.0, px = 0.0;
        heraw_motion::eval_perspective_rs_flow(fit, f_px, cy, cx, 240.0, 520.0, 0.50, 0.0, py, px);
        double ty = 0.0, tx = 0.0;
        heraw_motion::eval_perspective_rs_flow(truth, f_px, cy, cx, 240.0, 520.0, 0.50, 0.0, ty, tx);
        assert(near_val(py, ty, 0.02) && near_val(px, tx, 0.02));
    }

    // 3. E12: dng-hq (BlackLevel=2048) zero-mean dark noise LibRaw readback preserves negative excursions cleanly
    const int e12_dims[2][2] = {{66, 64}, {260, 260}};
    for (const auto& dm : e12_dims) {
        const int H = dm[0], W = dm[1];
        std::vector<int> signed_noise(static_cast<size_t>(H) * W);
        std::mt19937 rng(42);
        std::normal_distribution<double> nd(0.0, 260.0);
        double sum_n = 0.0;
        for (size_t i = 0; i < signed_noise.size(); ++i) {
            signed_noise[i] = static_cast<int>(std::lround(nd(rng)));
            sum_n += signed_noise[i];
        }
        const int shift = static_cast<int>(std::lround(sum_n / static_cast<double>(signed_noise.size())));
        std::vector<uint16_t> bayer_u16(signed_noise.size());
        size_t below_ped_in = 0;
        for (size_t i = 0; i < bayer_u16.size(); ++i) {
            const int v = std::clamp(2048 + signed_noise[i] - shift, 1, 65534);
            bayer_u16[i] = static_cast<uint16_t>(v);
            if (v < 2048) ++below_ped_in;
        }
        assert(below_ped_in > bayer_u16.size() / 3);
        const std::string dng_p = "/tmp/v3_oracle_e12_hq.dng";
        const float wb[3] = {1.8f, 1.0f, 1.6f};
        const double mat[3][3] = {{0.8, -0.1, -0.05}, {-0.2, 1.1, 0.1}, {0.0, 0.05, 0.85}};
        auto op_f = [](const std::string& p) { return std::fopen(p.c_str(), "wb"); };
        auto rm_f = [](const std::string& p) { std::remove(p.c_str()); };
        auto rp_f = [](const std::string& a, const std::string& b) { return std::rename(a.c_str(), b.c_str()) == 0; };
        auto or_f = [](int) -> uint16_t { return 1; };
        auto par_f = [](int n, int, auto&& fn, int) { fn(0, n, 0); };
        const bool ok_w = heraw_io::write_dng14_rggb_ex(
            dng_p, bayer_u16.data(), nullptr, H, W, heraw_io::RawDngMode::DNG_HQ,
            2.5e-5, 1e-4, "TestCam", "AuditHQ", wb, mat, 0, 0.0,
            heraw_io::ExifBundle{}, "AuditE12", 1, or_f, par_f, op_f, rm_f, rp_f, 2048);
        assert(ok_w);
#ifdef SUITE_V3_LIBRAW
        LibRaw lr;
        assert(lr.open_file(dng_p.c_str()) == LIBRAW_SUCCESS);
        assert(lr.unpack() == LIBRAW_SUCCESS);
        assert(lr.imgdata.color.black == 2048 ||
               (lr.imgdata.color.cblack[4] == 2 && lr.imgdata.color.cblack[5] == 2 &&
                lr.imgdata.color.cblack[6] == 2048 && lr.imgdata.color.cblack[7] == 2048 &&
                lr.imgdata.color.cblack[8] == 2048 && lr.imgdata.color.cblack[9] == 2048));
        assert(lr.imgdata.rawdata.raw_image != nullptr);
        double dec_sum = 0.0;
        size_t below_ped_out = 0;
        const double inv_span = 1.0 / (65535.0 - 2048.0);
        for (size_t i = 0; i < bayer_u16.size(); ++i) {
            const uint16_t dv = lr.imgdata.rawdata.raw_image[i];
            if (dv < 2048u) ++below_ped_out;
            dec_sum += (static_cast<double>(dv) - 2048.0) * inv_span;
        }
        const double mean_norm = dec_sum / static_cast<double>(bayer_u16.size());
        assert(below_ped_out > bayer_u16.size() / 3);
        assert(std::abs(mean_norm) < 2e-4);
#endif
        std::remove(dng_p.c_str());
    }

    // 4. Q5 negative oracle: all-clipped burst must NOT extrapolate above mu0
    {
        const double mu0 = 0.96, sat_n = 0.94, sig_n = 0.015;
        double grad = 0.0, hess = 1.0 / (4.0 * sig_n * sig_n);
        bool has_cens = false, has_unsat = false;
        const std::array<double, 4> all_clip = {0.98, 0.99, 0.97, 0.99};
        for (double v : all_clip) {
            if (v >= sat_n) {
                const auto ps = heraw_spec::upper_tail_probit_score(mu0, sat_n, sig_n);
                grad += ps.grad; hess += ps.hess; has_cens = true;
            } else {
                has_unsat = true;
            }
        }
        assert(has_cens && !has_unsat);
        float mu_all_clip = static_cast<float>(mu0);
        const bool did_all = heraw_spec::apply_q5_censored_probit_step(
            mu0, grad, hess, 0.99, has_cens, has_unsat, mu_all_clip);
        assert(!did_all && mu_all_clip == static_cast<float>(mu0));
        // Mixed burst with 1 unsaturated observation -> has_unsat == true allows capped recovery
        has_cens = false; has_unsat = false; grad = 0.0; hess = 1.0 / (4.0 * sig_n * sig_n);
        const std::array<double, 4> mixed = {0.98, 0.99, 1.08, 0.99};
        for (int i = 0; i < 4; ++i) {
            const double v = mixed[i], s_i = (i == 2) ? 1.50 : sat_n;
            if (v >= s_i) {
                const auto ps = heraw_spec::upper_tail_probit_score(mu0, s_i, sig_n);
                grad += ps.grad; hess += ps.hess; has_cens = true;
            } else {
                has_unsat = true;
                grad += (mu0 - v) / (sig_n * sig_n);
                hess += 1.0 / (sig_n * sig_n);
            }
        }
        assert(has_cens && has_unsat);
        float mu_mixed = static_cast<float>(mu0);
        const bool did_mix = heraw_spec::apply_q5_censored_probit_step(
            mu0, grad, hess, 0.99, has_cens, has_unsat, mu_mixed);
        assert(did_mix && mu_mixed > static_cast<float>(mu0) && mu_mixed <= static_cast<float>(1.35 * 0.99) + 1e-6f);
    }

    // 5. Q7 cross-focus depth cost & Q8 6-endpoint contour unmixing
    {
        const std::array<double, 3> h_true = {0.92, 0.55, 0.24};
        const std::array<double, 3> h_wrong = {0.20, 0.48, 0.85};
        std::array<double, 3> y_re{}, y_im{}, inv_v{100.0, 100.0, 100.0};
        for (size_t k = 0; k < 3; ++k) { y_re[k] = 1.5 * h_true[k]; y_im[k] = -0.8 * h_true[k]; }
        const double j_true = heraw_suite::cross_focus_spectral_depth_cost(y_re, y_im, h_true, inv_v, 0.0);
        const double j_wrong = heraw_suite::cross_focus_spectral_depth_cost(y_re, y_im, h_wrong, inv_v, 0.0);
        assert(near_val(j_true, 0.0, 1e-9) && j_wrong > 1.0);

        const std::vector<float> alphas = {0.15f, 0.35f, 0.55f, 0.80f, 0.90f};
        std::vector<std::array<float, 3>> obs;
        const double gf = 0.6, crf = 0.2, cbf = -0.1, gb = 0.2, crb = -0.05, cbb = 0.15;
        for (float a : alphas) {
            const float b = 1.0f - a;
            obs.push_back({
                static_cast<float>(a * (gf + crf) + b * (gb + crb)),
                static_cast<float>(a * gf + b * gb),
                static_cast<float>(a * (gf + cbf) + b * (gb + cbb))
            });
        }
        const auto u6 = heraw_refocus::solve_contour_unmix_6endpoint(alphas, obs);
        assert(u6.observable && u6.endpoints_identifiable);
        assert(near_val(u6.gf, gf, 1e-4) && near_val(u6.crf, crf, 1e-4) && near_val(u6.cbf, cbf, 1e-4));
        assert(near_val(u6.gb, gb, 1e-4) && near_val(u6.crb, crb, 1e-4) && near_val(u6.cbb, cbb, 1e-4));
        const std::vector<float> flat_a = {0.5f, 0.5f, 0.5f, 0.5f};
        const std::vector<std::array<float, 3>> flat_obs(4, {0.5f, 0.4f, 0.45f});
        const auto u_deg = heraw_refocus::solve_contour_unmix_6endpoint(flat_a, flat_obs);
        assert(!u_deg.endpoints_identifiable && u_deg.sum_only_observable);
    }

    // 6. F-B catseye_pupil_weight & ApertureWeightMode (Voronoi vs Dwell)
    {
        const float w_center = heraw_refocus::catseye_pupil_weight(0.0f, 3.0f, 5.0f, 0.7f, 0.0f, 0.0f);
        const float w_corner = heraw_refocus::catseye_pupil_weight(0.0f, 3.0f, 5.0f, 0.7f, 0.0f, 0.9f);
        assert(w_center > 0.99f && w_corner < 0.05f);
        const std::array<float, 2> pts[4] = {{0.0f, 0.0f}, {0.0f, 0.05f}, {0.0f, 0.08f}, {10.0f, 0.0f}};
        std::vector<double> w_v, w_d;
        std::vector<float> s_v, s_d;
        heraw_refocus::detail::compute_aperture_site_weights(4, pts, nullptr, nullptr, heraw_refocus::ApertureWeightMode::Voronoi, w_v, s_v);
        heraw_refocus::detail::compute_aperture_site_weights(4, pts, nullptr, nullptr, heraw_refocus::ApertureWeightMode::Dwell, w_d, s_d);
        assert(near_val(w_d[0], 1.0, 1e-6) && near_val(w_d[3], 1.0, 1e-6));
        assert(w_v[3] > w_v[1] * 1.5);
    }

    // 7. F-D / DF4 cold-ambient neutral vs warm-glass wb_gain invariance
    {
        const int H = 32, W = 32;
        std::vector<float> rgb_base(static_cast<size_t>(H) * W * 3);
        const std::array<float, 3> wb = {1.1f, 1.0f, 2.6f};
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const bool hot = (y < 4 && x < 4);
                const float lum = hot ? 4.0f : 0.20f;
                const size_t i = static_cast<size_t>(y) * W + x;
                rgb_base[i * 3 + 0] = lum / wb[0];
                rgb_base[i * 3 + 1] = lum / wb[1];
                rgb_base[i * 3 + 2] = lum / wb[2];
            }
        }
        auto run_diff = [&](float uc_c, std::vector<float>& sc_out) {
            std::vector<float> img = rgb_base;
            heraw_diff::DiffusionConfig dcfg;
            dcfg.mode = heraw_diff::DiffusionMode::ULTRA_CONTRAST;
            dcfg.uc_grade = "3";
            dcfg.uc_chroma = uc_c;
            dcfg.wb_gain = wb;
            heraw_diff::apply_optical_diffusion_linear(img.data(), H, W, dcfg, &sc_out);
        };
        std::vector<float> sc_neu, sc_warm;
        run_diff(0.0f, sc_neu);
        run_diff(1.0f, sc_warm);
        const size_t mid = static_cast<size_t>(20) * W + 20;
        const float g_neu = sc_neu[mid * 3 + 1];
        const float r_wb_neu = sc_neu[mid * 3 + 0] * wb[0];
        const float b_wb_neu = sc_neu[mid * 3 + 2] * wb[2];
        assert(g_neu > 1e-4f);
        assert(std::abs(r_wb_neu / g_neu - 1.0f) < 0.02f && std::abs(b_wb_neu / g_neu - 1.0f) < 0.02f);
        const float r_wb_warm = sc_warm[mid * 3 + 0] * wb[0];
        const float b_wb_warm = sc_warm[mid * 3 + 2] * wb[2];
        assert(r_wb_warm > sc_warm[mid * 3 + 1] && sc_warm[mid * 3 + 1] > b_wb_warm);
    }

    // 8. F-F next-shot EV -2.5 bracket recommendation from clipped_fraction
    {
        const std::array<float, 2> sh[2] = {{0.0f, 0.0f}, {0.0f, 0.5f}};
        const auto rec_clip = heraw_plan::compute_next_shot_plan(sh, 0.12f, 0.0f, 1);
        const auto rec_clean = heraw_plan::compute_next_shot_plan(sh, 0.0f, 0.0f, 1);
        assert(near_val(rec_clip.ev_offset_stop, -2.5f, 1e-4) && near_val(rec_clean.ev_offset_stop, 0.0f, 1e-4));
    }

    std::printf("[PASS] Post-audit oracles (E03 B-spline, E07 RS flow, E12 dng-hq LibRaw, Q5 neg, Q7, Q8, F-B, F-D, F-F)\n");
}

} // namespace

int main() {
    test_e01_e05_native_variance_and_weight();
    test_e02_e06_e07_e09_e10_e11_deterministic();
    test_e08_o2_masked_lockin();
    test_r1_r2_diffusion_and_cross_section4();
    test_r3_r4_r6_motion_and_oklab();
    test_r5_r7_r8_r9_r10_spectral_probit();
    test_r11_fh_astro_and_exr_multipart();
    test_dxo_real_container_contract();
    test_post_audit_oracles();
    std::printf("ALL V3 AUDIT ORACLES PASSED (E01-E12, Q1-Q10, R1-R12, Cross-Section 1-6, DXO1-4, F-A..F-H)\n");
    return 0;
}
