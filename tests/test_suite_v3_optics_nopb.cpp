// V3.1 Causal Verification Suite: OP-O1 (Sellmeier Spectral CA), OP-O2 (Exact Strip-Area + 3-Disc Positive STF),
// OP-O3 (Pure-Background Support-Eroded Handheld Bokeh Shaping), and NOPB/1 + NIB/1 Native Bus Invariants.
#include "../heraw_suite_core.hpp"
#include "../heraw_suite_refocus.hpp"
#include "../heraw_suite_handheld_optics.hpp"
#include "../heraw_suite_nib_bus.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <random>
#include <string>
#include <vector>

namespace {

void test_op_o2_stf_positive_otf_and_strip_area() {
    const int N = 151;
    const int cx = N / 2;
    const int cy = N / 2;
    std::vector<float> img(static_cast<size_t>(N) * N * 3, 0.0f);
    std::vector<float> depth(static_cast<size_t>(N) * N, 1.0f);
    img[(static_cast<size_t>(cy) * N + cx) * 3 + 0] = 1.0f;
    img[(static_cast<size_t>(cy) * N + cx) * 3 + 1] = 1.0f;
    img[(static_cast<size_t>(cy) * N + cx) * 3 + 2] = 1.0f;

    // Zero-blur identity check
    heraw_refocus::RefocusParams p0;
    p0.focus_depth = 1.0f;
    p0.radius_px = 0.0f;
    p0.layered_occlusion = false;
    auto out0 = heraw_refocus::render_refocus(img.data(), N, N, depth.data(), N, N, p0);
    assert(std::memcmp(out0.data(), img.data(), img.size() * sizeof(float)) == 0);

    // Full positive 3-disc STF at R = 32 px
    heraw_refocus::RefocusParams rp;
    rp.focus_depth = 0.0f;
    rp.radius_px = 32.0f;
    rp.layered_occlusion = false;
    rp.aperture_blades = 0; // true circle strip-area chord
    rp.spherical_aberration = 0.0f;
    rp.stf_apodization = 1.0f;

    auto out = heraw_refocus::render_refocus(img.data(), N, N, depth.data(), N, N, rp);
    double sum_g = 0.0;
    for (int i = 0; i < N * N; ++i) {
        const float v = out[static_cast<size_t>(i) * 3 + 1];
        assert(v >= -1e-7f);
        sum_g += v;
    }
    assert(std::fabs(sum_g - 1.0) < 1e-4);

    // Continuous radial frequency scan across x = R * |omega| in [0.25, 16.0] along H, V, and 45-deg Diagonal
    // (matches Astra Round-2 §3 OP-O2 independent verification at R = 32, |omega| = x / R)
    double min_otf_h = 1.0, min_otf_v = 1.0, min_otf_d = 1.0;
    double otf_h_x16 = 0.0, otf_v_x16 = 0.0, otf_d_x16 = 0.0;
    const double inv_s2 = 1.0 / std::sqrt(2.0);
    for (int step = 1; step <= 64; ++step) {
        const double x_freq = 0.25 * static_cast<double>(step); // 0.25 .. 16.00
        const double omega = x_freq / 32.0;
        double rh = 0.0, rv = 0.0, rd = 0.0;
        for (int y = 0; y < N; ++y) {
            const double dy = static_cast<double>(y - cy);
            for (int x = 0; x < N; ++x) {
                const float w = out[(static_cast<size_t>(y) * N + x) * 3 + 1];
                if (w == 0.0f) continue;
                const double dx = static_cast<double>(x - cx);
                rh += static_cast<double>(w) * std::cos(omega * dx);
                rv += static_cast<double>(w) * std::cos(omega * dy);
                rd += static_cast<double>(w) * std::cos(omega * inv_s2 * (dx + dy));
            }
        }
        min_otf_h = std::min(min_otf_h, rh);
        min_otf_v = std::min(min_otf_v, rv);
        min_otf_d = std::min(min_otf_d, rd);
        if (step == 64) { // exact x = 16.0 (|omega| = 0.5 rad/px)
            otf_h_x16 = rh;
            otf_v_x16 = rv;
            otf_d_x16 = rd;
        }
    }
    assert(min_otf_h > -0.0005);
    assert(min_otf_v > -0.0005);
    assert(min_otf_d > -0.0005);

    // Verify 4-plane fast path (sa == 0) matches 12-plane path (sa = 1e-5) closely
    heraw_refocus::RefocusParams rp_sa = rp;
    rp_sa.spherical_aberration = 1e-5f;
    auto out_sa = heraw_refocus::render_refocus(img.data(), N, N, depth.data(), N, N, rp_sa);
    double max_sa_diff = 0.0;
    for (size_t i = 0; i < out.size(); ++i) {
        max_sa_diff = std::max<double>(max_sa_diff, std::fabs(out[i] - out_sa[i]));
    }
    assert(max_sa_diff < 5e-4);

    std::printf("[OP-O2] 3-Disc STF + Exact Circle Strip-Area PASS (sum=%.6f min_OTF_h/v/d=%.6f/%.6f/%.6f u75=%.6f/%.6f/%.6f)\n",
                sum_g, min_otf_h, min_otf_v, min_otf_d, otf_h_x16, otf_v_x16, otf_d_x16);
}

void test_op_o1_spectral_ca_invariants() {
    const int H = 48, W = 64;
    const size_t npx = static_cast<size_t>(H) * W;

    // 1. Zero-strength bit-exact identity (including negative dark-noise values)
    std::vector<float> noisy(npx * 3);
    std::mt19937 rng(42);
    std::normal_distribution<float> nd(0.15f, 0.08f);
    for (float& v : noisy) v = nd(rng);
    std::vector<float> work = noisy;

    heraw_hoptics::SpectralCaConfig p_off;
    p_off.mode = heraw_hoptics::SpectralCaMode::OFF;
    p_off.power_px = 8.0f;
    heraw_hoptics::apply_spectral_ca_linear(work.data(), H, W, p_off);
    assert(std::memcmp(work.data(), noisy.data(), noisy.size() * sizeof(float)) == 0);

    heraw_hoptics::SpectralCaConfig p_zero;
    p_zero.mode = heraw_hoptics::SpectralCaMode::LATERAL;
    p_zero.power_px = 0.0f;
    heraw_hoptics::apply_spectral_ca_linear(work.data(), H, W, p_zero);
    assert(std::memcmp(work.data(), noisy.data(), noisy.size() * sizeof(float)) == 0);

    // 2. Neutral & out-of-cone flat-field invariant (< 1e-6)
    for (int case_id = 0; case_id < 2; ++case_id) {
        const float r0 = (case_id == 0) ? 0.85f : -0.015f;
        const float g0 = (case_id == 0) ? 0.85f : 0.85f;
        const float b0 = (case_id == 0) ? 0.85f : 0.85f;
        std::vector<float> flat(npx * 3);
        for (size_t i = 0; i < npx; ++i) {
            flat[i * 3 + 0] = r0;
            flat[i * 3 + 1] = g0;
            flat[i * 3 + 2] = b0;
        }
        std::vector<float> flat_lat = flat;
        heraw_hoptics::SpectralCaConfig pl;
        pl.mode = heraw_hoptics::SpectralCaMode::LATERAL;
        pl.power_px = 10.0f;
        pl.radial = true;
        heraw_hoptics::apply_spectral_ca_linear(flat_lat.data(), H, W, pl);

        std::vector<float> flat_ax = flat;
        heraw_hoptics::SpectralCaConfig pa;
        pa.mode = heraw_hoptics::SpectralCaMode::AXIAL_AURA;
        pa.power_px = 8.0f;
        heraw_hoptics::apply_spectral_ca_linear(flat_ax.data(), H, W, pa);

        double max_drift = 0.0;
        for (size_t i = 0; i < flat.size(); ++i) {
            max_drift = std::max<double>(max_drift, std::fabs(flat_lat[i] - flat[i]));
            max_drift = std::max<double>(max_drift, std::fabs(flat_ax[i] - flat[i]));
        }
        assert(max_drift < 1e-5);
    }

    // 3. Single saturated red impulse (1, 0, 0) nonnegativity (no -5/27 undershoot!)
    std::vector<float> red_imp(npx * 3, 0.0f);
    red_imp[(static_cast<size_t>(H / 2) * W + (W / 2)) * 3 + 0] = 1.0f;
    heraw_hoptics::SpectralCaConfig p_red;
    p_red.mode = heraw_hoptics::SpectralCaMode::LATERAL;
    p_red.power_px = 6.0f;
    p_red.radial = false;
    p_red.angle_deg = 0.0f;
    p_red.dark_sigma = 0.0f;
    heraw_hoptics::apply_spectral_ca_linear(red_imp.data(), H, W, p_red);
    float min_red_out = 1.0f;
    for (float v : red_imp) min_red_out = std::min(min_red_out, v);
    assert(min_red_out >= -1e-6f);

    // 4. 1 px white vertical line at 16 px dispersion -> continuous rainbow without comb gaps
    std::vector<float> line_img(npx * 3, 0.0f);
    const int x_mid = W / 2;
    for (int y = 0; y < H; ++y) {
        const size_t idx = (static_cast<size_t>(y) * W + x_mid) * 3;
        line_img[idx + 0] = 2.0f;
        line_img[idx + 1] = 2.0f;
        line_img[idx + 2] = 2.0f;
    }
    heraw_hoptics::SpectralCaConfig p_line;
    p_line.mode = heraw_hoptics::SpectralCaMode::LATERAL;
    p_line.power_px = 16.0f;
    p_line.radial = false;
    p_line.angle_deg = 0.0f;
    heraw_hoptics::HandheldOpticsTelemetry tel;
    heraw_hoptics::apply_spectral_ca_linear(line_img.data(), H, W, p_line, &tel);
    assert(tel.spectral_applied && tel.max_step_px <= 0.601f);

    // Every integer column within [-5, +5] around x_mid must receive continuous positive energy (zero comb holes)
    const int y_probe = H / 2;
    float min_span_lum = 1e9f;
    for (int dx = -5; dx <= 5; ++dx) {
        const size_t idx = (static_cast<size_t>(y_probe) * W + (x_mid + dx)) * 3;
        const float lum = line_img[idx + 0] + line_img[idx + 1] + line_img[idx + 2];
        min_span_lum = std::min(min_span_lum, lum);
    }
    assert(min_span_lum > 1e-3f);
    // Verify spectral separation: blue shifts opposite to red across x_mid
    const size_t left_idx = (static_cast<size_t>(y_probe) * W + (x_mid - 4)) * 3;
    const size_t right_idx = (static_cast<size_t>(y_probe) * W + (x_mid + 4)) * 3;
    const float rb_asym = (line_img[left_idx + 0] - line_img[left_idx + 2]) *
                          (line_img[right_idx + 0] - line_img[right_idx + 2]);
    assert(rb_asym < -1e-6f);

    std::printf("[OP-O1] Sellmeier Spectral CA + Equal-Shift Quadrature (max_step=%.3fpx Q=%d) + Nonnegativity Cap PASS (min_red=%.2e)\n",
                tel.max_step_px, tel.spectral_nodes, min_red_out);
}

void test_op_o3_bokeh_shaping_and_foreground_isolation() {
    const int H = 64, W = 64;
    const size_t npx = static_cast<size_t>(H) * W;

    // 1. Pure-background support erosion on high-contrast foreground step edge (Round-2 §3.3)
    std::vector<float> step_img(npx * 3, 0.0f);
    std::vector<float> protect_mask(npx, 0.0f);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(y) * W + x;
            if (x < 30) {
                protect_mask[i] = 1.0f;
                step_img[i * 3 + 0] = step_img[i * 3 + 1] = step_img[i * 3 + 2] = 1.0f;
            } else if (x == 30) {
                // Mixed 0.5 boundary rim carrying bright foreground energy
                protect_mask[i] = 0.5f;
                step_img[i * 3 + 0] = step_img[i * 3 + 1] = step_img[i * 3 + 2] = 0.8f;
            } else {
                // Pure black out-of-focus background
                protect_mask[i] = 0.0f;
                step_img[i * 3 + 0] = step_img[i * 3 + 1] = step_img[i * 3 + 2] = 0.0f;
            }
        }
    }
    std::vector<float> step_out = step_img;
    heraw_hoptics::BokehShapeConfig bp;
    bp.de_nisen = 1.0f;
    bp.swirl = 0.7f;
    bp.aspect = 1.8f;
    bp.onion_clean = 1.0f;
    bp.fixed_defocus_px = 6.0f;
    bp.protect_mask = protect_mask.data();
    heraw_hoptics::apply_bokeh_shaping_linear(step_out.data(), H, W, bp);

    double max_fg_delta = 0.0;
    double max_bg_halo = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(y) * W + x;
            for (int c = 0; c < 3; ++c) {
                if (x < 30) {
                    max_fg_delta = std::max<double>(max_fg_delta, std::fabs(step_out[i * 3 + c] - step_img[i * 3 + c]));
                } else if (x >= 32) {
                    max_bg_halo = std::max<double>(max_bg_halo, std::fabs(step_out[i * 3 + c]));
                }
            }
        }
    }
    assert(max_fg_delta == 0.0);
    assert(max_bg_halo < 1e-6);

    // 2. De-Nisen ring double-line suppression + Anamorphic aspect stretch on background bokeh disc
    std::vector<float> ring_img(npx * 3, 0.0f);
    std::vector<float> bg_protect(npx, 0.0f);
    const float cy = 32.0f, cx = 32.0f;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float r = std::hypot(static_cast<float>(x) - cx, static_cast<float>(y) - cy);
            const size_t i = static_cast<size_t>(y) * W + x;
            if (r <= 7.5f) {
                // Harsh bright outer rim ("nisen-bokeh" soap-bubble ring)
                const float val = (r >= 5.5f) ? 2.0f : 0.35f;
                ring_img[i * 3 + 0] = ring_img[i * 3 + 1] = ring_img[i * 3 + 2] = val;
            }
        }
    }
    auto ring_roughness = [&](const std::vector<float>& buf) {
        double e = 0.0;
        for (int y = 2; y < H - 2; ++y) {
            for (int x = 2; x < W - 2; ++x) {
                const size_t i = static_cast<size_t>(y) * W + x;
                const float lap = 4.0f * buf[i * 3 + 1] - buf[(i - 1) * 3 + 1] - buf[(i + 1) * 3 + 1]
                                  - buf[(i - W) * 3 + 1] - buf[(i + W) * 3 + 1];
                e += static_cast<double>(lap) * lap;
            }
        }
        return e;
    };
    const double rough_before = ring_roughness(ring_img);
    std::vector<float> dn_out = ring_img;
    heraw_hoptics::BokehShapeConfig bp_dn;
    bp_dn.de_nisen = 0.90f;
    bp_dn.onion_clean = 0.80f;
    bp_dn.fixed_defocus_px = 6.0f;
    bp_dn.protect_mask = bg_protect.data();
    heraw_hoptics::apply_bokeh_shaping_linear(dn_out.data(), H, W, bp_dn);
    const double rough_after = ring_roughness(dn_out);
    assert(rough_after < 0.65 * rough_before);

    // Anamorphic vertical stretch check (aspect = 2.0)
    std::vector<float> ana_out = ring_img;
    heraw_hoptics::BokehShapeConfig bp_ana;
    bp_ana.aspect = 2.0f;
    bp_ana.fixed_defocus_px = 6.0f;
    bp_ana.protect_mask = bg_protect.data();
    heraw_hoptics::apply_bokeh_shaping_linear(ana_out.data(), H, W, bp_ana);
    double i_xx = 0.0, i_yy = 0.0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float w = ana_out[(static_cast<size_t>(y) * W + x) * 3 + 1];
            i_xx += static_cast<double>(w) * (x - cx) * (x - cx);
            i_yy += static_cast<double>(w) * (y - cy) * (y - cy);
        }
    }
    assert(i_yy > 1.30 * i_xx);

    // Chiral signed swirl check: off-axis impulse must produce opposite M_{r,theta} for +0.9 vs -0.9!
    std::vector<float> off_imp(static_cast<size_t>(H) * W * 3u, 0.0f);
    std::vector<float> zero_prot(static_cast<size_t>(H) * W, 0.0f);
    const int sx0 = cx + 28, sy0 = cy;
    off_imp[(static_cast<size_t>(sy0) * W + sx0) * 3 + 1] = 1.0f;
    auto eval_swirl_chiral = [&](float sw_val, std::vector<float>& out_buf) -> double {
        out_buf = off_imp;
        heraw_hoptics::BokehShapeConfig bs;
        bs.swirl = sw_val;
        bs.fixed_defocus_px = 14.0f;
        bs.protect_mask = zero_prot.data();
        heraw_hoptics::apply_bokeh_shaping_linear(out_buf.data(), H, W, bs);
        double sw_sum = 0.0, mr_sum = 0.0, mt_sum = 0.0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const double w = out_buf[(static_cast<size_t>(y) * W + x) * 3 + 1];
                assert(w >= -1e-7);
                if (w <= 0.0) continue;
                const double r = std::hypot(static_cast<double>(x - cx), static_cast<double>(y - cy));
                const double th = std::atan2(static_cast<double>(y - cy), static_cast<double>(x - cx));
                sw_sum += w;
                mr_sum += w * r;
                mt_sum += w * th;
            }
        }
        assert(sw_sum > 0.0);
        const double r_mean = mr_sum / sw_sum, t_mean = mt_sum / sw_sum;
        double cov_rt = 0.0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const double w = out_buf[(static_cast<size_t>(y) * W + x) * 3 + 1];
                if (w <= 0.0) continue;
                const double r = std::hypot(static_cast<double>(x - cx), static_cast<double>(y - cy));
                const double th = std::atan2(static_cast<double>(y - cy), static_cast<double>(x - cx));
                cov_rt += (w / sw_sum) * (r - r_mean) * (th - t_mean);
            }
        }
        return cov_rt;
    };
    std::vector<float> sw_pos, sw_neg;
    const double m_rt_pos = eval_swirl_chiral(+0.9f, sw_pos);
    const double m_rt_neg = eval_swirl_chiral(-0.9f, sw_neg);
    float max_sw_diff = 0.0f;
    for (size_t k = 0; k < sw_pos.size(); ++k) {
        max_sw_diff = std::max(max_sw_diff, std::fabs(sw_pos[k] - sw_neg[k]));
    }
    assert(m_rt_pos > 1e-3 && m_rt_neg < -1e-3);
    assert(0.5 < std::fabs(m_rt_pos / m_rt_neg) && std::fabs(m_rt_pos / m_rt_neg) < 2.0);
    assert(max_sw_diff > 0.02f);

    std::printf("[OP-O3] Bokeh Shaping + 2px Erosion + Chiral Swirl PASS (fg_delta=%.1e bg_halo=%.1e ana_ratio=%.3f M_rt=+%.4f/%.4f diff=%.4f)\n",
                max_fg_delta, max_bg_halo, i_yy / i_xx, m_rt_pos, m_rt_neg, max_sw_diff);
}

void test_nopb_bus_wire_and_resampler_invariants() {
    using heraw_bus::detail::JVal;

    heraw_bus::SessionIdentity sid;
    sid.active_w = 4;
    sid.active_h = 1;
    sid.anchor = 0;
    sid.frames = {"f0", "f1"};
    sid.geometry_id = "geom_unit_4x1";

    heraw_bus::NibBundle host;
    host.init_host(sid, 3);
    host.session_meta = JVal::make_obj({{"unknown_vendor_ext", JVal::make_str("preserve_me")}});

    const heraw_bus::ChartSpec c1x = host.charts.at("active_1x");

    // Rotated 1x4 chart mapping buffer (u in [0,1), v in [0,4)) -> active_1x (x = v, y = u)
    heraw_bus::ChartSpec crot;
    crot.affine = {0.0, 1.0, 0.0, 1.0, 0.0, 0.0};
    crot.bbox = {0.0, 0.0, 1.0, 4.0};
    crot.canvas_wh = {1.0, 4.0};
    host.charts["rot_1x4"] = crot;

    // Cropped 2x1 chart mapping buffer (u in [0,2), v in [0,1)) -> active_1x (x = u + 1, y = v)
    // Also test fractional valid bbox [0, 1.4) -> only pixel center u=0.5 (active x=1.5) is valid, NOT u=1.5 (active x=2.5)!
    heraw_bus::ChartSpec ccrop;
    ccrop.affine = {1.0, 0.0, 1.0, 0.0, 1.0, 0.0};
    ccrop.bbox = {0.0, 0.0, 1.4, 1.0};
    ccrop.canvas_wh = {2.0, 1.0};
    host.charts["crop_frac"] = ccrop;

    const std::vector<float> base_rgb(4 * 3, 0.2f);
    const std::vector<float> prior_rgb(4 * 3, 0.8f);
    host.put_tensor(heraw_bus::make_image_f32_tensor(
        "base.rgb", base_rgb.data(), 1u, 4u, 3u, NIB1_ORIG_NIJIN_MEASURED,
        "active_1x", c1x, "radiance.linear_rgb@1", "photon_linear"));
    host.put_tensor(heraw_bus::make_image_f32_tensor(
        "ext.prior", prior_rgb.data(), 1u, 4u, 3u, NIB1_ORIG_GENERATIVE_PRIOR,
        "active_1x", c1x, "radiance.linear_rgb@1", "photon_linear"));

    // Mask A: U8 binary mask [0, 1, 0, 1] on active_1x (protect_mask: 1 keeps base 0.2, 0 uses prior 0.8 -> [.8, .2, .8, .2])
    const uint8_t mu8[4] = {0, 1, 0, 1};
    host.put_tensor(heraw_bus::make_image_u8_tensor(
        "mask.u8", mu8, 1u, 4u, 1u, NIB1_ORIG_PARTITION_MASK,
        "active_1x", c1x, "mask.binary@1", "1"));

    // Mask B: F32 probability mask [0.0f, 1.0f, 0.0f, 1.0f] on active_1x
    const float mf32[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    host.put_tensor(heraw_bus::make_image_f32_tensor(
        "mask.f32", mf32, 1u, 4u, 1u, NIB1_ORIG_PARTITION_MASK,
        "active_1x", c1x, "mask.prob@1", "1"));

    // Mask C: RLE runs on rot_1x4 (H=4, W=1, runs=[1, 1, 1, 1] -> [0, 1, 0, 1] mapped via x=v to active_1x!)
    heraw_bus::NibTensor mrle;
    mrle.public_name = "mask.rle_rot";
    mrle.wire_key = heraw_bus::detail::wire_key_for_name(mrle.public_name);
    mrle.dtype = NIB1_U32;
    mrle.kind = NIB1_SCALAR;
    mrle.origin = NIB1_ORIG_PARTITION_MASK;
    mrle.shape = {4u, 1u, 1u, 1u};
    for (size_t i = 0; i < 6; ++i) mrle.affine[i] = static_cast<float>(crot.affine[i]);
    mrle.bbox = crot.int_bbox();
    mrle.meta = JVal::make_obj({
        {"encoding", JVal::make_str("rle_runs")},
        {"mask_hw", JVal::make_arr({JVal::make_int(4), JVal::make_int(1)})},
        {"rle_order", JVal::make_str("F")},
        {"semantic", JVal::make_str("mask.binary@1")},
        {"unit", JVal::make_str("1")},
        {"chart", JVal::make_str("rot_1x4")}
    });
    mrle.data.resize(16u);
    for (size_t r = 0; r < 4; ++r) heraw_bus::detail::write_le32(&mrle.data[r * 4u], 1u);
    host.put_tensor(std::move(mrle));

    // Mask D: F32 [1.0, 0.0] on crop_frac (bbox [0, 1.4) -> only u=0.5 / active x=1 is protected; x=2 must NOT get 0.6 bleed!)
    const float mcrop[2] = {1.0f, 0.0f};
    host.put_tensor(heraw_bus::make_image_f32_tensor(
        "mask.crop_frac", mcrop, 1u, 2u, 1u, NIB1_ORIG_PARTITION_MASK,
        "crop_frac", ccrop, "mask.prob@1", "1"));

    // Round-trip through NIB/1 binary file
    const std::string nib_path = "/tmp/nijin_unit_test_4x1.nib";
    heraw_bus::write_nib_file(nib_path, host);
    const auto decoded = heraw_bus::read_nib_file(nib_path, &sid);
    std::remove(nib_path.c_str());
    assert(decoded.session_meta.at("unknown_vendor_ext").as_str() == "preserve_me");

    auto run_comp = [&](const char* mask_name) {
        JVal inputs = JVal::make_obj({
            {"base_rgb", JVal::make_str("base.rgb")},
            {"prior_rgb", JVal::make_str("ext.prior")},
            {"protect_mask", JVal::make_str(mask_name)}
        });
        return heraw_bus::ops::run_operator("nijin.optics.composite_prior@1", decoded, inputs, JVal::make_obj());
    };

    const auto r_u8 = run_comp("mask.u8");
    const auto r_f32 = run_comp("mask.f32");
    const auto r_rle = run_comp("mask.rle_rot");

    const auto vu8 = r_u8.fields.at("rgb").unpack_doubles();
    const auto vf32 = r_f32.fields.at("rgb").unpack_doubles();
    const auto vrle = r_rle.fields.at("rgb").unpack_doubles();
    const double expected_px[4] = {0.8, 0.2, 0.8, 0.2};
    for (int px = 0; px < 4; ++px) {
        for (int c = 0; c < 3; ++c) {
            const size_t idx = static_cast<size_t>(px * 3 + c);
            assert(std::fabs(vu8[idx] - expected_px[px]) < 1e-6);
            assert(std::fabs(vf32[idx] - expected_px[px]) < 1e-6);
            assert(std::fabs(vrle[idx] - expected_px[px]) < 1e-6);
        }
    }

    // Check provenance bitmask: px 0,2 -> generative (0x10), px 1,3 -> pure measured (0x01)
    const auto& prov = r_rle.fields.at("provenance").data;
    assert(prov.size() == 4);
    assert((prov[0] & NIB1_PROV_GENERATIVE) != 0);
    assert(prov[1] == NIB1_PROV_MEASURED);
    assert((prov[2] & NIB1_PROV_GENERATIVE) != 0);
    assert(prov[3] == NIB1_PROV_MEASURED);

    // Check discrete valid-center clamp on crop_frac [0, 1.4): only active x=1 is protected (0.2), x=0,2,3 are 0.8!
    const auto r_crop = run_comp("mask.crop_frac");
    const auto vcrop = r_crop.fields.at("rgb").unpack_doubles();
    assert(std::fabs(vcrop[0 * 3 + 0] - 0.8) < 1e-6);
    assert(std::fabs(vcrop[1 * 3 + 0] - 0.2) < 1e-6);
    assert(std::fabs(vcrop[2 * 3 + 0] - 0.8) < 1e-6);
    assert(std::fabs(vcrop[3 * 3 + 0] - 0.8) < 1e-6);

    // Check rotated 180-deg prior_rgb Chart resampling + multi-layer provenance accumulation (0x10 | 0x02 == 0x12)
    heraw_bus::NibBundle rot_test = decoded;
    heraw_bus::ChartSpec c180;
    c180.affine = {-1.0, 0.0, 4.0, 0.0, -1.0, 1.0};
    c180.bbox = {0.0, 0.0, 4.0, 1.0};
    c180.canvas_wh = {4.0, 1.0};
    rot_test.charts["rot_180"] = c180;
    const std::array<float, 12> stripe_rot = {
        1.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f
    }; // left half = 1.0 in rotated chart -> right half = 1.0 on active_1x!
    rot_test.put_tensor(heraw_bus::make_image_f32_tensor(
        "ext.rot_prior", stripe_rot.data(), 1u, 4u, 3u, NIB1_ORIG_GENERATIVE_PRIOR,
        "rot_180", c180, "radiance.prior_rgb@1", "photon_linear"));
    const auto r_rot_p = heraw_bus::ops::run_operator("nijin.optics.composite_prior@1", rot_test,
        JVal::make_obj({{"base_rgb", JVal::make_str("base.rgb")}, {"prior_rgb", JVal::make_str("ext.rot_prior")}}),
        JVal::make_obj());
    const auto vrot_p = r_rot_p.fields.at("rgb").unpack_doubles();
    assert(std::fabs(vrot_p[0 * 3 + 0] - 0.0) < 1e-6);
    assert(std::fabs(vrot_p[1 * 3 + 0] - 0.0) < 1e-6);
    assert(std::fabs(vrot_p[2 * 3 + 0] - 1.0) < 1e-6);
    assert(std::fabs(vrot_p[3 * 3 + 0] - 1.0) < 1e-6);

    // Layer 2: blend calibrated prior (origin=1) at weight 0.5 on top of generative layer -> prov == 0x12!
    rot_test.put_tensor(r_rot_p.fields.at("rgb"), true);
    rot_test.fields.at("rgb").public_name = "layer1.rgb";
    rot_test.put_tensor(r_rot_p.fields.at("provenance"), true);
    const std::array<float, 12> cal_rgb = {
        0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f
    };
    const std::array<float, 4> half_w = {0.5f, 0.5f, 0.5f, 0.5f};
    rot_test.put_tensor(heraw_bus::make_image_f32_tensor(
        "ext.cal", cal_rgb.data(), 1u, 4u, 3u, NIB1_ORIG_CO_CALIBRATED,
        "active_1x", c1x, "radiance.prior_rgb@1", "photon_linear"));
    rot_test.put_tensor(heraw_bus::make_image_f32_tensor(
        "ext.half_w", half_w.data(), 1u, 4u, 1u, NIB1_ORIG_PARTITION_MASK,
        "active_1x", c1x, "mask.weight@1", "1"));
    const auto r_stack = heraw_bus::ops::run_operator("nijin.optics.composite_prior@1", rot_test,
        JVal::make_obj({
            {"base_rgb", JVal::make_str("rgb")},
            {"base_provenance", JVal::make_str("provenance")},
            {"prior_rgb", JVal::make_str("ext.cal")},
            {"weight", JVal::make_str("ext.half_w")}
        }), JVal::make_obj());
    const auto& stack_prov = r_stack.fields.at("provenance").data;
    assert(stack_prov[0] == (NIB1_PROV_GENERATIVE | NIB1_PROV_CALIBRATED));
    assert(r_stack.fields.at("rgb").origin == NIB1_ORIG_GENERATIVE_PRIOR);

    // Check BUS-O3 server-side origin=0 spoof rejection & BUS-O5 session lock mismatch
    heraw_bus::NibBundle spoof;
    spoof.init_host(sid, 0);
    spoof.put_tensor(heraw_bus::make_image_f32_tensor(
        "fake.host", base_rgb.data(), 1u, 4u, 3u, NIB1_ORIG_NIJIN_MEASURED,
        "active_1x", c1x, "radiance.linear_rgb@1", "photon_linear"));
    bool caught_origin = false;
    try {
        heraw_bus::NibBundle target = decoded;
        heraw_bus::attach_external_into_host(target, spoof, true, false);
    } catch (const heraw_bus::BusException& e) {
        caught_origin = (e.code == "origin");
    }
    assert(caught_origin);

    heraw_bus::SessionIdentity bad_sid = sid;
    std::swap(bad_sid.frames[0], bad_sid.frames[1]);
    bool caught_session = false;
    try {
        decoded.session.verify_matches(bad_sid);
    } catch (const heraw_bus::BusException& e) {
        caught_session = (e.code == "session");
    }
    assert(caught_session);

    std::printf("[BUS-O1..O5] NIB/1 Wire + 4x1 U8/F32/RotRLE Parity + Fractional Crop Clamp + Origin0/Session Lock PASS\n");
}

} // namespace

int main() {
    test_op_o2_stf_positive_otf_and_strip_area();
    test_op_o1_spectral_ca_invariants();
    test_op_o3_bokeh_shaping_and_foreground_isolation();
    test_nopb_bus_wire_and_resampler_invariants();
    std::printf("ALL V3.1 OPTICS & NOPB ORACLES PASSED.\n");
    return 0;
}
