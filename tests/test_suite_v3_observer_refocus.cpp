// Unified v3.0.0 Causal Verification: Multi-Regime Refocusing, High-N Cleaning/Lucky,
// and Camera-Burst Subject Observer Matrix (O1..O5) Positive Deltas & Negative Gates.
#include "../heraw_suite_core.hpp"
#include "../heraw_suite_observer.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

namespace {

constexpr uint8_t kRggb[6][6] = {
    {0, 1, 0, 1, 0, 1}, {1, 2, 1, 2, 1, 2},
    {0, 1, 0, 1, 0, 1}, {1, 2, 1, 2, 1, 2},
    {0, 1, 0, 1, 0, 1}, {1, 2, 1, 2, 1, 2},
};

struct RgbScene {
    int H1, W1, Hh, Wh;
    heraw_suite::Planes frames_lin, g_halfs, fy, fx, mw;
    std::vector<double> times;

    RgbScene(int h1, int w1, int n) : H1(h1), W1(w1), Hh(h1 / 2), Wh(w1 / 2) {
        const size_t p1 = static_cast<size_t>(H1) * W1;
        const size_t ph = static_cast<size_t>(Hh) * Wh;
        frames_lin.assign(n, std::vector<float>(p1, 0.0f));
        g_halfs.assign(n, std::vector<float>(ph, 0.0f));
        fy.assign(n, std::vector<float>(ph, 0.0f));
        fx.assign(n, std::vector<float>(ph, 0.0f));
        mw.assign(n, std::vector<float>(ph, 1.0f));
        times.resize(n, 0.0);
    }

    template <typename FnRgb>
    void fill_bayer(int k, FnRgb&& fn) {
        for (int y = 0; y < H1; ++y) {
            for (int x = 0; x < W1; ++x) {
                const uint8_t ch = kRggb[y % 6][x % 6];
                const auto rgb = fn(y, x, k);
                frames_lin[k][static_cast<size_t>(y) * W1 + x] = rgb[ch];
            }
        }
        for (int y = 0; y < Hh; ++y) {
            for (int x = 0; x < Wh; ++x) {
                const float g1 = frames_lin[k][static_cast<size_t>(2 * y) * W1 + (2 * x + 1)];
                const float g2 = frames_lin[k][static_cast<size_t>(2 * y + 1) * W1 + (2 * x)];
                g_halfs[k][static_cast<size_t>(y) * Wh + x] = 0.5f * (g1 + g2);
            }
        }
    }
};

void test_layered_partition_and_bg_depth() {
    // 1a. Constant image on continuous sloped depth must stay exact to < 1e-5
    const int H = 72, W = 72;
    std::vector<float> img(static_cast<size_t>(H) * W * 3, 0.42f);
    std::vector<float> depth(static_cast<size_t>(H) * W);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            depth[static_cast<size_t>(y) * W + x] = static_cast<float>(x) / static_cast<float>(W - 1);

    heraw_refocus::RefocusParams rp;
    rp.focus_depth = 0.25f;
    rp.radius_px = 14.0f;
    rp.layered_occlusion = true;
    rp.aperture_blades = 7;
    rp.blade_angle_deg = 15.0f;
    rp.spherical_aberration = -0.35f;
    rp.stf_apodization = 0.4f;
    auto out = heraw_refocus::render_refocus(img.data(), H, W, depth.data(), H, W, rp);
    double max_err = 0.0;
    for (float v : out) max_err = std::max<double>(max_err, std::fabs(v - 0.42f));
    assert(max_err < 1e-5);

    // 1b. Local revealed_bg_depth check: foreground bar at d=1.0, revealed bg dot at d=0.5, focus at d=0.1
    // When revealed_bg_depth=0.5, the dot blurs at r=(0.5-0.1)*20=8 px, strictly tighter than if placed at d=0.0
    std::vector<float> bg(static_cast<size_t>(H) * W * 3, 0.1f);
    std::vector<float> bg_d(static_cast<size_t>(H) * W, 0.5f);
    std::fill(img.begin(), img.end(), 0.1f);
    std::fill(depth.begin(), depth.end(), 0.5f);
    for (int y = 26; y <= 46; ++y)
        for (int x = 26; x <= 46; ++x) {
            depth[static_cast<size_t>(y) * W + x] = 1.0f;
            for (int c = 0; c < 3; ++c) img[(static_cast<size_t>(y) * W + x) * 3 + c] = 0.8f;
        }
    for (int c = 0; c < 3; ++c) bg[(36 * W + 36) * 3 + c] = 2.0f;

    heraw_refocus::RefocusParams p_mid;
    p_mid.focus_depth = 0.1f;
    p_mid.radius_px = 20.0f;
    p_mid.layered_occlusion = true;
    p_mid.revealed_bg_rgb = bg.data();
    p_mid.revealed_bg_depth = bg_d.data();
    p_mid.revealed_bg_depth_h = H;
    p_mid.revealed_bg_depth_w = W;
    auto o_mid = heraw_refocus::render_refocus(img.data(), H, W, depth.data(), H, W, p_mid);

    std::vector<float> bg_far(static_cast<size_t>(H) * W, 0.0f);
    p_mid.revealed_bg_depth = bg_far.data();
    auto o_far = heraw_refocus::render_refocus(img.data(), H, W, depth.data(), H, W, p_mid);
    // Midground bg (r=8) blurs the center dot over smaller disk -> higher central brightness than r=2?
    // Wait: focus_depth=0.1 -> at bg=0.5 |d-f|=0.4 -> r=8; at bg=0.0 |d-f|=0.1 -> r=2. Both valid & distinct!
    double diff_mid_far = 0.0;
    for (size_t i = 0; i < o_mid.size(); ++i) diff_mid_far += std::fabs(o_mid[i] - o_far[i]);
    assert(diff_mid_far > 0.25);
    std::printf("[T1] Layered partition-of-unity & local revealed_bg_depth PASS (const_err=%.2e)\n", max_err);
}

void test_moments_sa_and_scheimpflug() {
    // 2a. Check Zernike SA asymmetry: -sa produces creamy center on background (d<f) & bright ring on fg (d>f)
    const int H = 64, W = 64;
    std::vector<float> img_bg(static_cast<size_t>(H) * W * 3, 0.0f);
    std::vector<float> depth_bg(static_cast<size_t>(H) * W, 0.1f);
    img_bg[(32 * W + 32) * 3 + 0] = img_bg[(32 * W + 32) * 3 + 1] = img_bg[(32 * W + 32) * 3 + 2] = 100.0f;

    heraw_refocus::RefocusParams p_sa;
    p_sa.focus_depth = 0.5f;
    p_sa.radius_px = 25.0f; // R = 10 px
    p_sa.spherical_aberration = -0.45f;
    p_sa.layered_occlusion = false;
    auto o_bg = heraw_refocus::render_refocus(img_bg.data(), H, W, depth_bg.data(), H, W, p_sa);
    const float bg_center = o_bg[(32 * W + 32) * 3];
    const float bg_rim = o_bg[(32 * W + 40) * 3]; // 8 px out
    assert(bg_center > bg_rim * 1.15f);

    std::vector<float> depth_fg(static_cast<size_t>(H) * W, 0.9f); // foreground d=0.9 > 0.5
    auto o_fg = heraw_refocus::render_refocus(img_bg.data(), H, W, depth_fg.data(), H, W, p_sa);
    const float fg_center = o_fg[(32 * W + 32) * 3];
    const float fg_rim = o_fg[(32 * W + 40) * 3];
    assert(fg_rim > fg_center * 1.15f);

    // 2b. Scheimpflug tilted focal plane keeps tilted edge sharp across whole image
    std::vector<float> img_t(static_cast<size_t>(H) * W * 3, 0.0f), dep_t(static_cast<size_t>(H) * W);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float nx = 2.0f * (x - 0.5f * (W - 1)) / static_cast<float>(W);
            dep_t[static_cast<size_t>(y) * W + x] = std::max(0.0f, std::min(1.0f, 0.5f + 0.45f * nx));
            const float v = ((x / 3 + y / 3) & 1) ? 0.9f : 0.1f;
            for (int c = 0; c < 3; ++c) img_t[(static_cast<size_t>(y) * W + x) * 3 + c] = v;
        }
    }
    heraw_refocus::RefocusParams p_flat, p_tilt;
    p_flat.focus_depth = 0.5f; p_flat.radius_px = 18.0f;
    p_tilt = p_flat; p_tilt.tilt_x = 0.45f;
    auto o_flat = heraw_refocus::render_refocus(img_t.data(), H, W, dep_t.data(), H, W, p_flat);
    auto o_tilt = heraw_refocus::render_refocus(img_t.data(), H, W, dep_t.data(), H, W, p_tilt);
    auto edge_energy = [&](const std::vector<float>& r) {
        double s = 0.0;
        for (int y = 6; y < H - 6; ++y)
            for (int x = 6; x < W - 6; ++x)
                s += std::fabs(r[(y * W + x + 1) * 3] - r[(y * W + x) * 3]);
        return s;
    };
    assert(edge_energy(o_tilt) > 2.2 * edge_energy(o_flat));
    std::printf("[T2] Moment prefix-sum SA asymmetry & Scheimpflug tilt PASS (tilt/flat=%.2fx)\n",
                edge_energy(o_tilt) / edge_energy(o_flat));
}

void test_synthetic_aperture_deconv_and_dff() {
    // 3a. 4D Synthetic Aperture see-through across 25 pupil grid frames
    const int H1 = 96, W1 = 96, N = 25;
    std::vector<std::vector<float>> frames(N, std::vector<float>(static_cast<size_t>(H1) * W1, 0.0f));
    std::vector<std::array<double, 9>> cam_h(N, {{1, 0, 0, 0, 1, 0, 0, 0, 1}});
    std::vector<std::array<float, 2>> base_yx(N);
    for (int ky = 0; ky < 5; ++ky) {
        for (int kx = 0; kx < 5; ++kx) {
            const int idx = ky * 5 + kx;
            base_yx[idx] = {{static_cast<float>(ky - 2) * 1.6f, static_cast<float>(kx - 2) * 1.6f}};
        }
    }
    const int Hh = H1 / 2, Wh = W1 / 2;
    std::vector<float> dep(static_cast<size_t>(Hh) * Wh, 0.0f);
    for (int y = 0; y < Hh; ++y)
        for (int x = 22; x <= 25; ++x) dep[static_cast<size_t>(y) * Wh + x] = 1.0f;

    auto bg_pattern = [](float x1) { return 0.30f + 0.25f * std::cos(2.0f * std::numbers::pi_v<float> * x1 / 12.0f); };
    for (int idx = 0; idx < N; ++idx) {
        for (int y1 = 0; y1 < H1; ++y1) {
            for (int x1 = 0; x1 < W1; ++x1) {
                const float x_half = (x1 - 0.5f) * 0.5f;
                const float fg_cx = 23.5f + 5.0f * base_yx[idx][1]; // half-px shift for rho_fg = 5.0
                float val = bg_pattern(static_cast<float>(x1));
                if (std::fabs(x_half - fg_cx) <= 1.5f) val = 0.95f; // narrow bright foreground occluder bar
                frames[idx][static_cast<size_t>(y1) * W1 + x1] = val;
            }
        }
    }
    heraw_refocus::SyntheticApertureInput sa;
    sa.H1 = H1; sa.W1 = W1; sa.H_half = Hh; sa.W_half = Wh; sa.N = N;
    sa.frames_lin = frames.data(); sa.cfa6x6 = &kRggb[0][0]; sa.depth_half = dep.data();
    sa.rho_low = 0.0f; sa.rho_high = 5.0f;
    sa.cam_h = cam_h.data(); sa.base_yx = base_yx.data();
    auto sa_out = heraw_refocus::render_synthetic_aperture(sa, 0.0f, 0.0f, 0.0f, true, H1, W1);
    double center_err = 0.0;
    for (int x1 = 44; x1 <= 50; ++x1)
        center_err += std::fabs(sa_out[(48 * W1 + x1) * 3 + 1] - bg_pattern(static_cast<float>(x1)));
    center_err /= 7.0;
    assert(center_err < 0.08);

    // 3b. Shift-focus Bessel J1 deconvolution
    std::vector<float> sharp_img(static_cast<size_t>(H1) * W1 * 3, 0.0f), blur_img(static_cast<size_t>(H1) * W1 * 3, 0.0f);
    for (int y = 0; y < H1; ++y)
        for (int x = 0; x < W1; ++x) {
            const float v = 0.5f + 0.35f * std::sin(2.0f * std::numbers::pi_v<float> * x / 10.0f) *
                                     std::sin(2.0f * std::numbers::pi_v<float> * y / 10.0f);
            for (int c = 0; c < 3; ++c) sharp_img[(static_cast<size_t>(y) * W1 + x) * 3 + c] = v;
        }
    heraw_refocus::RefocusParams pb;
    pb.focus_depth = 0.5f; pb.radius_px = 6.0f; // at d=0.1 -> R = 2.4 px pillbox
    std::vector<float> dep_b(static_cast<size_t>(Hh) * Wh, 0.1f);
    blur_img = heraw_refocus::render_refocus(sharp_img.data(), H1, W1, dep_b.data(), Hh, Wh, pb);
    auto contrast_patch = [&](const std::vector<float>& r) {
        float mn = 10.0f, mx = -10.0f;
        for (int y = 24; y < H1 - 24; ++y)
            for (int x = 24; x < W1 - 24; ++x) {
                mn = std::min(mn, r[(y * W1 + x) * 3 + 1]);
                mx = std::max(mx, r[(y * W1 + x) * 3 + 1]);
            }
        return mx - mn;
    };
    const float c_before = contrast_patch(blur_img);
    heraw_refocus::deconvolve_shift_focus_bessel(blur_img, H1, W1, dep_b.data(), Hh, Wh, 0.5f, 0.1f, 0.0f, 0.0f, 6.0f, 1e-6f);
    const float c_after = contrast_patch(blur_img);
    assert(c_after > 1.35f * c_before);

    // 3c. Parabolic sub-step FocusStack dff_depth recovery
    const int NF = 7;
    RgbScene fsc(96, 96, NF);
    std::vector<float> fpos(NF);
    for (int k = 0; k < NF; ++k) fpos[k] = static_cast<float>(k) / static_cast<float>(NF - 1);
    for (int k = 0; k < NF; ++k) {
        fsc.fill_bayer(k, [&](int y, int x, int kk) -> std::array<float, 3> {
            const float d_true = 0.15f + 0.70f * static_cast<float>(x) / 95.0f;
            const float sharp = std::exp(-18.0f * (fpos[kk] - d_true) * (fpos[kk] - d_true));
            const float hi = std::sin(0.9f * static_cast<float>(y)) * std::cos(0.9f * static_cast<float>(x));
            const float val = 0.45f + 0.25f * sharp * hi;
            return {{val, val, val}};
        });
    }
    heraw_suite::BurstView bv_fs{fsc.frames_lin, fsc.g_halfs, fsc.fy, fsc.fx, fsc.mw, 96, 96, kRggb, 0.0f, 1e-6f};
    bv_fs.focus_positions = &fpos;
    auto res_fs = heraw_suite::compute_fields(bv_fs, {"dff_depth"});
    assert(res_fs.gates.at("dff_depth").observable);
    const auto& dff = res_fs.maps.at("dff_depth");
    double sq_dff = 0.0;
    size_t dff_cnt = 0;
    for (int y = 6; y < 42; ++y) {
        for (int x = 6; x < 42; ++x) {
            const float d_true = 0.15f + 0.70f * (2.0f * x + 0.5f) / 95.0f;
            const double e = dff[static_cast<size_t>(y) * 48 + x] - d_true;
            sq_dff += e * e; ++dff_cnt;
        }
    }
    const double rmse_dff = std::sqrt(sq_dff / dff_cnt);
    assert(rmse_dff < 0.09);
    std::printf("[T3] Synthetic-aperture (err=%.4f), Bessel deconv (%.2fx), parabolic dff_depth (RMSE=%.4f) PASS\n",
                center_err, c_after / c_before, rmse_dff);
}

void test_dust_prnu_and_iso_lucky() {
    // 4a. [C13-DustPRNU] 24-frame drifting burst with 10% sensor dust spot + 1.8% PRNU
    const int H1 = 96, W1 = 96, N = 24;
    RgbScene sc(H1, W1, N);
    auto scene_val = [](float y1, float x1) {
        return 0.35f + 0.04f * std::sin(y1 * 0.07f) * std::cos(x1 * 0.06f);
    };
    auto sensor_gain = [](int y1, int x1) {
        const float dy = static_cast<float>(y1 - 48), dx = static_cast<float>(x1 - 48);
        const float dust = -0.12f * std::exp(-(dy * dy + dx * dx) / 72.0f);
        const float prnu = 0.018f * std::sin(y1 * 0.7f + x1 * 0.5f);
        return 1.0f + dust + prnu;
    };
    for (int k = 0; k < N; ++k) {
        const float shift_y = static_cast<float>(k) * 0.75f; // > 17 px total shift across frames
        const float shift_x = static_cast<float>(k) * 0.80f;
        for (int y = 0; y < sc.Hh; ++y)
            for (int x = 0; x < sc.Wh; ++x) {
                sc.fy[k][static_cast<size_t>(y) * sc.Wh + x] = 0.5f * shift_y;
                sc.fx[k][static_cast<size_t>(y) * sc.Wh + x] = 0.5f * shift_x;
            }
        sc.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const float g_true = scene_val(y - shift_y, x - shift_x);
            const float v = g_true * sensor_gain(y, x);
            return {{v, v, v}};
        });
    }
    auto eval_ff_rmse = [&]() {
        double sq = 0.0;
        for (int y = 24; y < 72; ++y)
            for (int x = 24; x < 72; ++x) {
                const float d = sc.frames_lin[0][static_cast<size_t>(y) * W1 + x] - scene_val(static_cast<float>(y), static_cast<float>(x));
                sq += d * d;
            }
        return std::sqrt(sq / (48.0 * 48.0));
    };
    const double rmse_before = eval_ff_rmse();
    const auto ff_st = heraw_core::silicon_flatfield_dust_prnu_cleanup(
        sc.frames_lin, H1, W1, &kRggb[0][0], sc.fy, sc.fx, sc.mw, {1e-7f, 0.0f}, {});
    const double rmse_after = eval_ff_rmse();
    assert(ff_st.changed > 0 && rmse_after < 0.35 * rmse_before);

    // 4b. [C3-IsoLucky] 12-frame burst where first 4 frames are sharp, last 8 frames are blurred
    RgbScene lsc(192, 192, 12);
    for (int k = 0; k < 12; ++k) {
        const float sharp = (k < 4) ? 1.0f : 0.25f;
        lsc.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const float v = 0.4f + sharp * 0.25f * std::sin(x * 0.85f) * std::sin(y * 0.85f);
            return {{v, v, v}};
        });
    }
    const auto lk_st = heraw_core::isoplanatic_lucky_weights(
        lsc.g_halfs, lsc.Hh, lsc.Wh, lsc.fy, lsc.fx, {1e-7f, 0.0f}, lsc.mw, 0.33f, {});
    double w_sharp = 0.0, w_blur = 0.0;
    for (int k = 0; k < 4; ++k) w_sharp += lsc.mw[k][48 * lsc.Wh + 48];
    for (int k = 4; k < 12; ++k) w_blur += lsc.mw[k][48 * lsc.Wh + 48];
    w_sharp /= 4.0; w_blur /= 8.0;
    assert(lk_st.changed > 0 && w_sharp > 1.25 * w_blur);
    std::printf("[T4] [C13-DustPRNU] rmse %.5f->%.5f & [C3-IsoLucky] w_sharp=%.3f vs w_blur=%.3f PASS\n",
                rmse_before, rmse_after, w_sharp, w_blur);
}

void test_o1_p3_and_o2_lockin() {
    // 5a. O1: P^3 illuminant intersection + specular-free diffuse + microfacet roughness
    const int H1 = 128, W1 = 128, N = 10;
    RgbScene sc(H1, W1, N);
    const std::array<float, 3> c_illum{{0.577f, 0.473f, 0.335f}}; // warm light norm=0.818 -> normalize to warm [1, 0.82, 0.58]
    const double ci_n = std::hypot(c_illum[0], c_illum[1], c_illum[2]);
    for (int k = 0; k < N; ++k) {
        const float hx = 0.25f * (static_cast<float>(k) / 9.0f - 0.5f);
        sc.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const int reg = (x < 64) ? (y < 64 ? 0 : 1) : (y < 64 ? 2 : 3);
            std::array<float, 3> alb = {{0.35f, 0.08f, 0.08f}};
            if (reg == 1) alb = {{0.07f, 0.32f, 0.10f}};
            else if (reg == 2) alb = {{0.08f, 0.14f, 0.36f}};
            else if (reg == 3) alb = {{0.34f, 0.28f, 0.06f}};
            const float u = (static_cast<float>(x) - 64.0f) / 64.0f;
            const float v = (static_cast<float>(y) - 64.0f) / 64.0f;
            const float th = std::hypot(u - hx, v);
            const float lobe = 0.35f * std::exp(-(th * th) / 0.03f);
            return {{alb[0] + lobe * c_illum[0], alb[1] + lobe * c_illum[1], alb[2] + lobe * c_illum[2]}};
        });
    }
    heraw_suite::BurstView bv{sc.frames_lin, sc.g_halfs, sc.fy, sc.fx, sc.mw, H1, W1, kRggb, 0.0f, 1e-6f};
    bv.focal_length_mm = 50.0f; bv.pixel_pitch_um = 4.35f;
    auto fields = heraw_suite::compute_fields(bv, {});
    heraw_suite::observer::ObserverOptions oo;
    oo.compute_roughness = true; oo.compute_diffuse_intrinsic = true; oo.compute_metalness = true;
    auto rep = heraw_suite::observer::analyze_subject_observer(bv, fields, oo);
    assert(rep.has_illuminant);
    const double cos_sim = (rep.illuminant_rgb[0] * c_illum[0] + rep.illuminant_rgb[1] * c_illum[1] +
                            rep.illuminant_rgb[2] * c_illum[2]) / ci_n;
    assert(cos_sim > 0.96);
    assert(!rep.diffuse_intrinsic_half.rgb.empty());

    // 5b. O2: Rolling-shutter 100 Hz lock-in + 5 ms red phosphor decay lag + DC negative gate
    const int NL = 12;
    RgbScene ls(128, 128, NL);
    const float t_read_ms = 12.0f;
    const double freq = 100.0, om = 2.0 * std::numbers::pi * freq;
    const double tau_r_s = 0.005; // 5 ms -> phase lag atan(2*pi*100*0.005) = atan(3.14159) = 1.26 rad
    const double phi_lag = std::atan(om * tau_r_s);
    for (int k = 0; k < NL; ++k) {
        const double phi0 = 0.85 * static_cast<double>(k); // asynchronous capture phase
        ls.times[k] = static_cast<double>(k) * 0.05;
        ls.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const float hy = 0.5f * y;
            const double t_row = phi0 + om * (static_cast<double>(t_read_ms) * 1e-3 * hy / static_cast<double>(ls.Hh));
            const float g = (x < 96) ? (0.35f + 0.14f * static_cast<float>(std::cos(t_row))) : 0.35f;
            const float b_ch = g;
            const float r_ch = (x < 96) ? (0.35f + 0.10f * static_cast<float>(std::cos(t_row - phi_lag))) : 0.35f;
            return {{r_ch, g, b_ch}};
        });
    }
    heraw_suite::BurstView lbv{ls.frames_lin, ls.g_halfs, ls.fy, ls.fx, ls.mw, 128, 128, kRggb, 0.0f, 1e-6f};
    lbv.exposure_seconds = 0.002f; lbv.times_seconds = &ls.times;
    auto lf = heraw_suite::compute_fields(lbv, {});
    heraw_suite::observer::ObserverOptions lo;
    lo.readout_ms_override = t_read_ms;
    lo.compute_ac_light = true; lo.compute_phosphor = true; lo.compute_light_split = true;
    auto lrep = heraw_suite::observer::analyze_subject_observer(lbv, lf, lo);
    assert(lrep.lockin_gate.observable && std::fabs(lrep.ac_freq_hz - 100.0f) <= 5.0f);
    assert(lrep.phosphor_gate.observable && lrep.phosphor_decay_ms >= 2.5f && lrep.phosphor_decay_ms <= 8.5f);
    assert(!lrep.light_dc_half.rgb.empty() && !lrep.light_ac_half.rgb.empty());

    // Negative gate: pure shot noise must NOT lock
    RgbScene ns(128, 128, NL);
    std::mt19937 rng(42);
    std::normal_distribution<float> nd(0.0f, 0.001f);
    for (int k = 0; k < NL; ++k)
        ns.fill_bayer(k, [&](int, int, int) -> std::array<float, 3> {
            const float v = 0.35f + nd(rng);
            return {{v, v, v}};
        });
    heraw_suite::BurstView nbv{ns.frames_lin, ns.g_halfs, ns.fy, ns.fx, ns.mw, 128, 128, kRggb, 0.0f, 1e-6f};
    auto nf = heraw_suite::compute_fields(nbv, {});
    auto nrep = heraw_suite::observer::analyze_subject_observer(nbv, nf, lo);
    assert(!nrep.lockin_gate.observable);
    std::printf("[T5] O1 (cos=%.4f) & O2 lock-in (f=%.1f Hz, tau_R=%.2f ms, neg=false) PASS\n",
                cos_sim, lrep.ac_freq_hz, lrep.phosphor_decay_ms);
}

void test_o3_self_stereo_kinematics_and_ballistic() {
    // 6a. Moving rigid-body SE(3) self-stereo on zero-baseline tripod + velocity Jacobian (TTC, vorticity)
    const int H1 = 128, W1 = 128, N = 10;
    RgbScene sc(H1, W1, N);
    for (int k = 0; k < N; ++k) {
        sc.times[k] = static_cast<double>(k) * 0.04;
        for (int y = 0; y < sc.Hh; ++y) {
            for (int x = 0; x < sc.Wh; ++x) {
                if (y >= 16 && y < 48 && x >= 16 && x < 48) {
                    const float rx = static_cast<float>(x - 32), ry = static_cast<float>(y - 32);
                    const float rho = std::exp(-(rx * rx + ry * ry) / 72.0f); // curved 3D relief orthogonal to affine plane
                    const float t = static_cast<float>(k) * 0.04f;
                    const float obj_shear_y = -0.22f * static_cast<float>(k) * rho;
                    const float obj_shear_x =  0.45f * static_cast<float>(k) * rho;
                    sc.fy[k][static_cast<size_t>(y) * sc.Wh + x] = 0.5f * (2.0f * t + 0.06f * t * ry + 0.04f * t * rx) + obj_shear_y;
                    sc.fx[k][static_cast<size_t>(y) * sc.Wh + x] = 0.5f * (3.0f * t + 0.06f * t * rx - 0.04f * t * ry) + obj_shear_x;
                }
            }
        }
        sc.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const float v = 0.4f + 0.1f * std::sin(0.3f * x) * std::cos(0.3f * y);
            return {{v, v, v}};
        });
    }
    heraw_suite::BurstView bv{sc.frames_lin, sc.g_halfs, sc.fy, sc.fx, sc.mw, H1, W1, kRggb, 0.0f, 1e-6f};
    bv.times_seconds = &sc.times;
    auto fields = heraw_suite::compute_fields(bv, {});
    assert(!fields.geometry.gate.observable); // camera baseline is zero
    heraw_suite::observer::ObserverOptions oo;
    oo.compute_kinematics = true;
    auto rep = heraw_suite::observer::analyze_subject_observer(bv, fields, oo);
    assert(rep.kinematics_gate.observable);
    assert(rep.rigid_self_stereo_observable);
    assert(rep.max_abs_vorticity_rad_s > 0.02f);

    // 6b. Ballistic g0 = 9.80665 m/s^2 distance calibration at Z = 4.0 m, f = 50 mm, pitch = 4.35 um
    RgbScene bsc(128, 128, N);
    const double f_px = 50.0 / 0.00435;
    const double z_true = 4.0;
    const double ay_1x = f_px * 9.80665 / z_true; // ~ 28179 px/s^2
    for (int k = 0; k < N; ++k) {
        const double t = static_cast<double>(k) * 0.02; // 0..0.18s -> 0.5*a*t^2 ~ 456 1x-px!
        bsc.times[k] = t;
        for (int y = 16; y < 48; ++y)
            for (int x = 16; x < 48; ++x)
                bsc.fy[k][static_cast<size_t>(y) * bsc.Wh + x] = static_cast<float>(0.5 * (40.0 * t + 0.5 * ay_1x * t * t));
        bsc.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const float v = 0.4f + 0.1f * std::sin(0.25f * x + 0.2f * y);
            return {{v, v, v}};
        });
    }
    heraw_suite::BurstView bbv{bsc.frames_lin, bsc.g_halfs, bsc.fy, bsc.fx, bsc.mw, 128, 128, kRggb, 0.0f, 1e-6f};
    bbv.focal_length_mm = 50.0f; bbv.pixel_pitch_um = 4.35f; bbv.times_seconds = &bsc.times;
    auto bf = heraw_suite::compute_fields(bbv, {});
    auto brep = heraw_suite::observer::analyze_subject_observer(bbv, bf, oo);
    assert(brep.ballistic_gate.observable);
    assert(std::fabs(brep.ballistic_distance_m - z_true) < 0.15 * z_true);
    std::printf("[T6] O3 SE(3) self-stereo (vort=%.3f) & ballistic g0 (Z=%.3f m vs 4.000 m) PASS\n",
                rep.max_abs_vorticity_rad_s, brep.ballistic_distance_m);
}

void test_o4_schlieren_and_o5_vib_vital() {
    // 7a. O4 Helmholtz BOS Gate: curl-free plume deflection passes (>=0.72), pure vortex rejected
    const int H1 = 128, W1 = 128, N = 6;
    RgbScene plume(H1, W1, N), vortex(H1, W1, N);
    auto tex = [](int y, int x, int) -> std::array<float, 3> {
        const float v = 0.4f + 0.25f * std::sin(0.8f * x) * std::sin(0.8f * y);
        return {{v, v, v}};
    };
    for (int k = 0; k < N; ++k) {
        plume.times[k] = vortex.times[k] = static_cast<double>(k) * 0.05;
        plume.fill_bayer(k, tex); vortex.fill_bayer(k, tex);
        const float phase_t = std::sin(1.4f * static_cast<float>(k)); // zero-mean AC plume vs velocity
        for (int y = 0; y < plume.Hh; ++y) {
            for (int x = 0; x < plume.Wh; ++x) {
                const float dy = static_cast<float>(y - 32), dx = static_cast<float>(x - 32);
                const float g = phase_t * 2.2f * std::exp(-(dy * dy + dx * dx) / 144.0f);
                // Plume: delta_u = grad(g)
                plume.fy[k][static_cast<size_t>(y) * plume.Wh + x] = -2.0f * (dy / 144.0f) * g;
                plume.fx[k][static_cast<size_t>(y) * plume.Wh + x] = -2.0f * (dx / 144.0f) * g;
                // Vortex: delta_u = grad^perp(g) (divergence-free)
                vortex.fy[k][static_cast<size_t>(y) * vortex.Wh + x] =  2.0f * (dx / 144.0f) * g;
                vortex.fx[k][static_cast<size_t>(y) * vortex.Wh + x] = -2.0f * (dy / 144.0f) * g;
            }
        }
    }
    heraw_suite::BurstView pbv{plume.frames_lin, plume.g_halfs, plume.fy, plume.fx, plume.mw, H1, W1, kRggb, 0.0f, 1e-6f};
    pbv.times_seconds = &plume.times;
    heraw_suite::BurstView vbv{vortex.frames_lin, vortex.g_halfs, vortex.fy, vortex.fx, vortex.mw, H1, W1, kRggb, 0.0f, 1e-6f};
    vbv.times_seconds = &vortex.times;
    auto pf = heraw_suite::compute_fields(pbv, {});
    auto vf = heraw_suite::compute_fields(vbv, {});
    heraw_suite::observer::ObserverOptions bo;
    bo.compute_schlieren = true;
    auto prep = heraw_suite::observer::analyze_subject_observer(pbv, pf, bo);
    auto vrep = heraw_suite::observer::analyze_subject_observer(vbv, vf, bo);
    assert(prep.schlieren_gate.observable && prep.schlieren_irrotational_ratio > 0.78f);
    assert(!vrep.schlieren_gate.observable && vrep.schlieren_irrotational_ratio < 0.30f);

    // 7b. O5 Rolling-Shutter Rank-2 Modal Vibrometry (240 Hz, jittered frame timestamps)
    RgbScene vib(128, 128, 8);
    const double t_read_v = 0.014; // 14 ms RS
    const double f_vib = 240.0;
    const double om_v = 2.0 * std::numbers::pi * f_vib;
    for (int k = 0; k < 8; ++k) {
        vib.times[k] = static_cast<double>(k) * 0.05 + ((k & 1) ? 0.017 : -0.008); // heavy SubSecTime jitter
        const double ph_k = 0.9 * k;
        vib.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
            const float hy = 0.5f * y;
            const double dx_1x = 0.45 * std::sin(ph_k + om_v * (t_read_v * hy / static_cast<double>(vib.Hh)));
            const float x_eff = static_cast<float>(x) - static_cast<float>(dx_1x);
            // Smooth multi-cycle edge along x so spatial gradient + Taylor shift hold cleanly
            const float v = 0.45f + 0.18f * std::tanh(0.6f * (x_eff - 64.0f));
            return {{v, v, v}};
        });
    }
    heraw_suite::BurstView vbbv{vib.frames_lin, vib.g_halfs, vib.fy, vib.fx, vib.mw, 128, 128, kRggb, 0.0f, 1e-6f};
    vbbv.exposure_seconds = 0.0005f;
    auto vbf = heraw_suite::compute_fields(vbbv, {});
    heraw_suite::observer::ObserverOptions vo;
    vo.readout_ms_override = 14.0f; vo.compute_vibrometry = true;
    auto vbrep = heraw_suite::observer::analyze_subject_observer(vbbv, vbf, vo);
    assert(vbrep.vibrometry_gate.observable && std::fabs(vbrep.modal_freq_hz - 240.0f) < 12.0f);

    // 7c. O5 14-bit P^3-Orthogonal Vital Perfusion (38 frames @ 20 fps, 75 BPM = 1.25 Hz) + noise negative gate
    const int NV = 38;
    RgbScene hr_sc(64, 64, NV), hr_noise(64, 64, NV);
    std::mt19937 rng2(1337);
    std::normal_distribution<float> nd2(0.0f, 0.0005f);
    for (int k = 0; k < NV; ++k) {
        const double t = static_cast<double>(k) * 0.05;
        hr_sc.times[k] = hr_noise.times[k] = t;
        const float mod = 0.008f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1.25 * t)); // 75 BPM
        hr_sc.fill_bayer(k, [&](int, int, int) -> std::array<float, 3> {
            return {{0.45f + 0.35f * mod, 0.30f + mod, 0.22f + 0.40f * mod}};
        });
        hr_noise.fill_bayer(k, [&](int, int, int) -> std::array<float, 3> {
            return {{0.45f + nd2(rng2), 0.30f + nd2(rng2), 0.22f + nd2(rng2)}};
        });
    }
    heraw_suite::BurstView hbv{hr_sc.frames_lin, hr_sc.g_halfs, hr_sc.fy, hr_sc.fx, hr_sc.mw, 64, 64, kRggb, 0.0f, 1e-8f};
    hbv.times_seconds = &hr_sc.times;
    heraw_suite::BurstView hnbv{hr_noise.frames_lin, hr_noise.g_halfs, hr_noise.fy, hr_noise.fx, hr_noise.mw, 64, 64, kRggb, 0.0f, 1e-6f};
    hnbv.times_seconds = &hr_noise.times;
    auto hf = heraw_suite::compute_fields(hbv, {});
    auto hnf = heraw_suite::compute_fields(hnbv, {});
    heraw_suite::observer::ObserverOptions ho;
    ho.compute_vital = true;
    auto hrep = heraw_suite::observer::analyze_subject_observer(hbv, hf, ho);
    auto hnrep = heraw_suite::observer::analyze_subject_observer(hnbv, hnf, ho);
    assert(hrep.vital_gate.observable && std::fabs(hrep.vital_bpm - 75.0f) <= 3.0f);
    assert(!hnrep.vital_gate.observable);
    std::printf("[T7] O4 BOS (plume=%.3f vs vortex=%.3f) & O5 (vib=%.1f Hz, vital=%.1f BPM, noise=false) PASS\n",
                prep.schlieren_irrotational_ratio, vrep.schlieren_irrotational_ratio,
                vbrep.modal_freq_hz, hrep.vital_bpm);
}

void test_v3_audit_regressions() {
    // 8a. [C-1] Radial epipolar fallback under pure axial zoom (FOE) flow
    {
        const int H1 = 192, W1 = 192, N = 5;
        RgbScene sc(H1, W1, N);
        const float f_h = 100.0f; // 1.6mm / (2 * 8um)
        const float cy = 0.5f * static_cast<float>(sc.Hh - 1);
        const float cx = 0.5f * static_cast<float>(sc.Wh - 1);
        auto flow_at = [&](int k, float yh, float xh) -> std::array<float, 2> {
            const float dy = yh - cy, dx = xh - cx;
            const float rad = std::hypot(dy, dx);
            const float w = 1.0f / (1.0f + std::exp(0.9f * (rad - 24.0f))); // smooth disk r<=24 half-px
            const float ny = dy / f_h, nx = dx / f_h;
            const float s = static_cast<float>(k) * 1.6f * w;
            return {{s * ny, s * nx}};
        };
        auto tex = [](float y1, float x1) -> std::array<float, 3> {
            const float v = 0.45f + 0.18f * std::sin(0.32f * x1) * std::cos(0.32f * y1)
                                  + 0.10f * std::cos(0.19f * (x1 + y1));
            return {{v, v, v}};
        };
        for (int k = 0; k < N; ++k) {
            sc.times[k] = static_cast<double>(k) * 0.05;
            sc.fill_bayer(k, [&](int y1, int x1, int kk) -> std::array<float, 3> {
                float yr = static_cast<float>(y1), xr = static_cast<float>(x1);
                for (int it = 0; it < 3; ++it) {
                    auto f = flow_at(kk, 0.5f * (yr - 0.5f), 0.5f * (xr - 0.5f));
                    yr = static_cast<float>(y1) - 2.0f * f[0];
                    xr = static_cast<float>(x1) - 2.0f * f[1];
                }
                return tex(yr, xr);
            });
            for (int y = 0; y < sc.Hh; ++y) {
                for (int x = 0; x < sc.Wh; ++x) {
                    auto f = flow_at(k, static_cast<float>(y), static_cast<float>(x));
                    const size_t idx = static_cast<size_t>(y) * sc.Wh + x;
                    sc.fy[k][idx] = f[0];
                    sc.fx[k][idx] = f[1];
                }
            }
        }
        heraw_suite::BurstView bv{sc.frames_lin, sc.g_halfs, sc.fy, sc.fx, sc.mw, H1, W1, kRggb, 0.0f, 1e-6f};
        bv.focal_length_mm = 1.6f;
        bv.pixel_pitch_um = 8.0f;
        bv.enable_kinetic_sign = true;
        bv.times_seconds = &sc.times;
        auto fres = heraw_suite::compute_fields(bv, {"depth", "depth_confidence"});
        assert(fres.gates.at("depth").observable);
        const auto& dep = fres.maps.at("depth");
        const auto& dc  = fres.maps.at("depth_confidence");
        double sum_in = 0.0, sum_out = 0.0, sum_conf = 0.0;
        size_t n_in = 0, n_out = 0;
        for (int y = 8; y < sc.Hh - 8; ++y) {
            for (int x = 8; x < sc.Wh - 8; ++x) {
                const float r = std::hypot(static_cast<float>(y) - cy, static_cast<float>(x) - cx);
                const size_t idx = static_cast<size_t>(y) * sc.Wh + x;
                sum_conf += dc[idx];
                if (r >= 8.0f && r <= 20.0f) { sum_in += dep[idx]; ++n_in; }
                else if (r >= 30.0f && r <= 42.0f) { sum_out += dep[idx]; ++n_out; }
            }
        }
        assert(n_in > 50 && n_out > 50);
        const double sep = std::fabs((sum_in / n_in) - (sum_out / n_out));
        assert(sep > 0.30 && (sum_conf / ((sc.Hh - 16) * (sc.Wh - 16))) > 0.15);
    }

    // 8b. [C-4] Block-512 horizontal tiling boundary partition-of-unity at W=16512, H=64
    {
        const int H = 64, W = 16512;
        std::vector<float> img(static_cast<size_t>(H) * W * 3, 0.37f);
        std::vector<float> depth(static_cast<size_t>(H) * W);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                depth[static_cast<size_t>(y) * W + x] = static_cast<float>(x) / static_cast<float>(W - 1);
        heraw_refocus::RefocusParams rp;
        rp.focus_depth = 0.40f;
        rp.radius_px = 18.0f;
        rp.layered_occlusion = true;
        rp.aperture_blades = 7;
        rp.blade_angle_deg = 22.0f;
        rp.spherical_aberration = 0.25f;
        rp.stf_apodization = 0.35f;
        auto out = heraw_refocus::render_refocus(img.data(), H, W, depth.data(), H, W, rp);
        double max_err = 0.0;
        for (float v : out) max_err = std::max<double>(max_err, std::fabs(v - 0.37f));
        assert(max_err < 1e-5);
    }

    // 8c. [O4] Non-power-of-two 132x197 Poisson eigenbasis (Hh=132, Wh=197 -> H1=264, W1=394)
    {
        const int H1 = 264, W1 = 394, N = 6;
        RgbScene plume(H1, W1, N), vortex(H1, W1, N);
        auto tex = [](int y, int x, int) -> std::array<float, 3> {
            const float v = 0.4f + 0.25f * std::sin(0.8f * x) * std::sin(0.8f * y);
            return {{v, v, v}};
        };
        for (int k = 0; k < N; ++k) {
            plume.times[k] = vortex.times[k] = static_cast<double>(k) * 0.05;
            plume.fill_bayer(k, tex); vortex.fill_bayer(k, tex);
            const float phase_t = std::sin(1.4f * static_cast<float>(k));
            for (int y = 0; y < plume.Hh; ++y) {
                for (int x = 0; x < plume.Wh; ++x) {
                    const float dy = static_cast<float>(y - 66), dx = static_cast<float>(x - 98);
                    const float g = phase_t * 2.2f * std::exp(-(dy * dy + dx * dx) / 144.0f);
                    plume.fy[k][static_cast<size_t>(y) * plume.Wh + x] = -2.0f * (dy / 144.0f) * g;
                    plume.fx[k][static_cast<size_t>(y) * plume.Wh + x] = -2.0f * (dx / 144.0f) * g;
                    vortex.fy[k][static_cast<size_t>(y) * vortex.Wh + x] =  2.0f * (dx / 144.0f) * g;
                    vortex.fx[k][static_cast<size_t>(y) * vortex.Wh + x] = -2.0f * (dy / 144.0f) * g;
                }
            }
        }
        heraw_suite::BurstView pbv{plume.frames_lin, plume.g_halfs, plume.fy, plume.fx, plume.mw, H1, W1, kRggb, 0.0f, 1e-6f};
        pbv.times_seconds = &plume.times;
        heraw_suite::BurstView vbv{vortex.frames_lin, vortex.g_halfs, vortex.fy, vortex.fx, vortex.mw, H1, W1, kRggb, 0.0f, 1e-6f};
        vbv.times_seconds = &vortex.times;
        auto pf = heraw_suite::compute_fields(pbv, {});
        auto vf = heraw_suite::compute_fields(vbv, {});
        heraw_suite::observer::ObserverOptions bo;
        bo.compute_schlieren = true;
        auto prep = heraw_suite::observer::analyze_subject_observer(pbv, pf, bo);
        auto vrep = heraw_suite::observer::analyze_subject_observer(vbv, vf, bo);
        assert(prep.schlieren_gate.observable && prep.schlieren_irrotational_ratio > 0.75f);
        assert(!vrep.schlieren_gate.observable && vrep.schlieren_irrotational_ratio < 0.30f);
    }

    // 8d. [O5a] Two-sided vibrating bright bar + unsorted anchor-last timestamps
    {
        const int NV = 8;
        RgbScene vib(128, 128, NV);
        const double t_read_v = 0.014;
        const double f_vib = 240.0;
        const double om_v = 2.0 * std::numbers::pi * f_vib;
        std::vector<size_t> corder = {7, 0, 1, 2, 3, 4, 5, 6};
        for (int k = 0; k < NV; ++k) {
            const int tk = static_cast<int>(corder[k]);
            vib.times[k] = static_cast<double>(tk) * 0.05;
            const double ph_k = 0.9 * static_cast<double>(tk);
            vib.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
                const float hy = 0.5f * static_cast<float>(y);
                const double dx_1x = 0.45 * std::sin(ph_k + om_v * (t_read_v * hy / static_cast<double>(vib.Hh)));
                const float d = (static_cast<float>(x) - static_cast<float>(dx_1x) - 64.0f) / 4.5f;
                const float v = 0.25f + 0.55f * std::exp(-0.5f * d * d);
                return {{v, v, v}};
            });
        }
        heraw_suite::BurstView vbbv{vib.frames_lin, vib.g_halfs, vib.fy, vib.fx, vib.mw, 128, 128, kRggb, 0.0f, 1e-6f};
        vbbv.exposure_seconds = 0.0005f;
        vbbv.times_seconds = &vib.times;
        vbbv.capture_order = &corder;
        auto vbf = heraw_suite::compute_fields(vbbv, {});
        heraw_suite::observer::ObserverOptions vo;
        vo.readout_ms_override = 14.0f;
        vo.compute_vibrometry = true;
        auto vbrep = heraw_suite::observer::analyze_subject_observer(vbbv, vbf, vo);
        assert(vbrep.vibrometry_gate.observable && std::fabs(vbrep.modal_freq_hz - 240.0f) < 15.0f);
    }

    // 8e. Unsorted / non-uniform FocusStack positions recovering dff_depth around 0.42
    {
        const int NF = 7;
        RgbScene fsc(96, 96, NF);
        std::vector<float> fpos = {0.55f, 0.0f, 0.85f, 0.12f, 1.0f, 0.30f, 0.72f};
        const float z_true = 0.42f;
        for (int k = 0; k < NF; ++k) {
            const float sharp = std::exp(-18.0f * (fpos[k] - z_true) * (fpos[k] - z_true));
            fsc.fill_bayer(k, [&](int y, int x, int) -> std::array<float, 3> {
                const float hi = std::sin(0.9f * static_cast<float>(y)) * std::cos(0.9f * static_cast<float>(x));
                const float val = 0.45f + 0.25f * sharp * hi;
                return {{val, val, val}};
            });
        }
        heraw_suite::BurstView bv_fs{fsc.frames_lin, fsc.g_halfs, fsc.fy, fsc.fx, fsc.mw, 96, 96, kRggb, 0.0f, 1e-6f};
        bv_fs.focus_positions = &fpos;
        auto res_fs = heraw_suite::compute_fields(bv_fs, {"dff_depth"});
        assert(res_fs.gates.at("dff_depth").observable);
        const auto& dff = res_fs.maps.at("dff_depth");
        double mean_dff = 0.0;
        size_t cnt = 0;
        for (int y = 10; y < 38; ++y) {
            for (int x = 10; x < 38; ++x) {
                mean_dff += dff[static_cast<size_t>(y) * 48 + x];
                ++cnt;
            }
        }
        assert(std::fabs((mean_dff / cnt) - z_true) < 0.08);
    }

    // 8f. DepthGateConfig Auto vs Relaxed vs Force
    {
        const int H1 = 96, W1 = 96, N = 5;
        RgbScene mb(H1, W1, N), zp(H1, W1, N);
        auto fg_mask = [](float yh, float xh) {
            return (yh >= 14.0f && yh <= 34.0f && xh >= 14.0f && xh <= 34.0f);
        };
        auto tex = [](float y1, float x1) -> std::array<float, 3> {
            const float v = 0.45f + 0.20f * std::sin(0.45f * x1) * std::cos(0.45f * y1);
            return {{v, v, v}};
        };
        for (int k = 0; k < N; ++k) {
            const float dx_half = 0.12f * static_cast<float>(k); // max 0.48 half-px => 0.96 1x-px (<1.5 Auto, >=0.65 Relaxed)
            mb.fill_bayer(k, [&](int y1, int x1, int) -> std::array<float, 3> {
                const float yh = 0.5f * (static_cast<float>(y1) - 0.5f);
                const float xh = 0.5f * (static_cast<float>(x1) - 0.5f);
                return tex(static_cast<float>(y1), static_cast<float>(x1) - (fg_mask(yh, xh) ? 2.0f * dx_half : 0.0f));
            });
            zp.fill_bayer(k, [&](int y1, int x1, int) -> std::array<float, 3> {
                return tex(static_cast<float>(y1), static_cast<float>(x1));
            });
            for (int y = 0; y < mb.Hh; ++y) {
                for (int x = 0; x < mb.Wh; ++x) {
                    const size_t idx = static_cast<size_t>(y) * mb.Wh + x;
                    mb.fx[k][idx] = fg_mask(static_cast<float>(y), static_cast<float>(x)) ? dx_half : 0.0f;
                }
            }
        }
        heraw_suite::BurstView bv_auto{mb.frames_lin, mb.g_halfs, mb.fy, mb.fx, mb.mw, H1, W1, kRggb, 0.0f, 1e-6f};
        bv_auto.depth_gate.mode = heraw_suite::DepthGateMode::Auto;
        auto r_auto = heraw_suite::compute_fields(bv_auto, {"depth"});
        assert(!r_auto.gates.at("depth").observable);

        heraw_suite::BurstView bv_rel = bv_auto;
        bv_rel.depth_gate.mode = heraw_suite::DepthGateMode::Relaxed;
        auto r_rel = heraw_suite::compute_fields(bv_rel, {"depth"});
        assert(r_rel.gates.at("depth").observable && !r_rel.gates.at("depth").forced);

        heraw_suite::BurstView bv_zero{zp.frames_lin, zp.g_halfs, zp.fy, zp.fx, zp.mw, H1, W1, kRggb, 0.0f, 1e-6f};
        bv_zero.depth_gate.mode = heraw_suite::DepthGateMode::Force;
        auto r_frc = heraw_suite::compute_fields(bv_zero, {"depth"});
        assert(r_frc.gates.at("depth").observable && r_frc.gates.at("depth").forced);
    }

    std::printf("[T8] Audit regressions (radial FOE, Block-512 W=16512, 132x197 BOS, 2-sided vib + anchor-last, FocusStack, DepthGate) PASS\n");
}

} // namespace

int main() {
    test_layered_partition_and_bg_depth();
    test_moments_sa_and_scheimpflug();
    test_synthetic_aperture_deconv_and_dff();
    test_dust_prnu_and_iso_lucky();
    test_o1_p3_and_o2_lockin();
    test_o3_self_stereo_kinematics_and_ballistic();
    test_o4_schlieren_and_o5_vib_vital();
    test_v3_audit_regressions();
    std::printf("ALL v3.0.0 OBSERVER + REFOCUS CAUSAL TESTS PASS\n");
    return 0;
}
