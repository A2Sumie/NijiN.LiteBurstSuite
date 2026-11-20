#include "../heraw_eval_metrology.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

static void require(bool ok, const std::string& msg) {
    if (!ok) throw std::runtime_error(msg);
}

static void build_bayer_rggb6x6(uint8_t cfa6x6[36]) {
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 6; ++c) {
            int pr = r & 1, pc = c & 1;
            cfa6x6[r * 6 + c] = (pr == 0 && pc == 0) ? 0 : ((pr == 1 && pc == 1) ? 2 : 1);
        }
    }
}

static void test_stratified_loo_chi2() {
    const int H1 = 96, W1 = 96, Hh = H1 / 2, Wh = W1 / 2;
    const int H2 = H1 * 2, W2 = W1 * 2;
    uint8_t cfa6x6[36];
    build_bayer_rggb6x6(cfa6x6);

    auto eval_scene = [&](double ay, double ax) -> double {
        double flat = 0.08;
        double edge = 0.08 + 0.37 * 0.5 * (1.0 + std::tanh((ax - 48.0) / 0.65));
        double tex = 0.25 + 0.12 * std::sin(0.65 * ax) * std::cos(0.55 * ay);
        double w_t = 0.5 * (1.0 + std::tanh((ay - 36.0) / 2.0));
        double w_e = 0.5 * (1.0 + std::tanh((ay - 64.0) / 2.0));
        return (1.0 - w_t) * flat + (w_t - w_e) * tex + w_e * edge;
    };

    std::vector<float> rgb2((size_t)H2 * W2 * 3, 0.0f);
    std::vector<float> rgb2_blur((size_t)H2 * W2 * 3, 0.0f);
    for (int r = 0; r < H2; ++r) {
        double ay = 0.5 * r;
        for (int c = 0; c < W2; ++c) {
            double ax = 0.5 * c;
            float v = (float)eval_scene(ay, ax);
            float vb = (float)eval_scene(ay, std::floor(ax * 0.5) * 2.0);
            for (int ch = 0; ch < 3; ++ch) {
                rgb2[((size_t)r * W2 + c) * 3 + ch] = v;
                rgb2_blur[((size_t)r * W2 + c) * 3 + ch] = 0.5f * (v + vb);
            }
        }
    }

    const float dy_h = -0.35f, dx_h = 0.45f; // +0.9, -0.7 full-px shift
    std::vector<float> fy((size_t)Hh * Wh, dy_h), fx((size_t)Hh * Wh, dx_h);
    std::vector<float> held_out((size_t)H1 * W1, 0.0f);
    const float sig0 = 0.0015f, gain = 0.0008f;
    std::mt19937 rng(42);
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (int r = 0; r < H1; ++r) {
        double ay = (double)r - 2.0 * dy_h;
        for (int c = 0; c < W1; ++c) {
            double ax = (double)c - 2.0 * dx_h;
            double v = eval_scene(ay, ax);
            double sig = std::sqrt((double)sig0 * sig0 + (double)gain * std::max(0.0, v));
            held_out[(size_t)r * W1 + c] = (float)(v + sig * gauss(rng));
        }
    }

    auto rep_good = heraw_eval::compute_stratified_loo_chi2(
        rgb2.data(), H2, W2, 2, held_out.data(), H1, W1, fy.data(), fx.data(), Hh, Wh,
        cfa6x6, sig0, gain, nullptr, 1.0f, 0.78f, 1);
    require(rep_good.valid && rep_good.n_flat > 200 && rep_good.n_tex > 200 && rep_good.n_edge > 20,
            "LOO chi2 stratified counts");
    require(std::abs(rep_good.chi2_flat - 1.0) < 0.2, "LOO chi2_flat unbiased near 1.0");
    require(std::abs(rep_good.chi2_tex - 1.0) < 0.25, "LOO chi2_tex unbiased near 1.0");
    require(rep_good.chi2_edge < 2.0, "LOO chi2_edge well behaved for unbiased recon");

    auto rep_blur = heraw_eval::compute_stratified_loo_chi2(
        rgb2_blur.data(), H2, W2, 2, held_out.data(), H1, W1, fy.data(), fx.data(), Hh, Wh,
        cfa6x6, sig0, gain, nullptr, 1.0f, 0.78f, 1);
    require(rep_blur.chi2_tex > rep_good.chi2_tex + 1.0, "LOO chi2 detects texture smoothing");
}

static void test_frc_and_phase_diversity() {
    const int H = 128, W = 128;
    std::vector<float> base((size_t)H * W), a((size_t)H * W), b((size_t)H * W), c((size_t)H * W);
    std::mt19937 rng_sig(11), rng(7);
    std::normal_distribution<float> nsig(0.0f, 0.25f);
    std::normal_distribution<float> nrm(0.0f, 0.01f);
    for (int i = 0; i < H * W; ++i) base[i] = nsig(rng_sig);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            int xm = (x - 1 + W) % W, ym = (y - 1 + H) % H;
            float lp = 0.4f * base[(size_t)y * W + x] +
                       0.15f * (base[(size_t)y * W + xm] + base[(size_t)ym * W + x] +
                                base[(size_t)y * W + (x + 1) % W] + base[(size_t)((y + 1) % H) * W + x]);
            float sig = 0.5f + lp + 0.15f * std::sin(0.35f * x + 0.2f * y);
            a[(size_t)y * W + x] = sig + nrm(rng);
            b[(size_t)y * W + x] = sig + nrm(rng);
            c[(size_t)y * W + x] = 0.5f + 0.1f * nrm(rng);
        }
    }
    auto frc_corr = heraw_eval::compute_half_burst_frc(a.data(), b.data(), H, W, 128);
    auto frc_uncorr = heraw_eval::compute_half_burst_frc(a.data(), c.data(), H, W, 128);
    require(frc_corr.valid && frc_corr.frc_area > 0.7, "Correlated signal gives high FRC area");
    require(frc_corr.frc_50_nyq > frc_uncorr.frc_50_nyq + 0.25, "FRC discriminates real detail from noise");

    float dx_good[6] = {0.0f, 0.5f, 1.0f, 1.5f, 0.25f, 0.75f};
    float dy_good[6] = {0.0f, 1.0f, 0.5f, 0.0f, 0.75f, 1.25f};
    auto pd_good = heraw_eval::compute_phase_diversity_det(dx_good, dy_good, 6);

    float dx_deg[6] = {0.0f, 0.01f, -0.01f, 0.0f, 0.005f, -0.005f};
    float dy_deg[6] = {0.0f, 0.0f, 0.01f, -0.01f, 0.002f, -0.003f};
    auto pd_deg = heraw_eval::compute_phase_diversity_det(dx_deg, dy_deg, 6);
    require(pd_good.valid && pd_deg.valid, "Phase diversity valid");
    require(pd_good.det_norm > pd_deg.det_norm * 50.0, "Phase diversity ranks lattice sampling > degenerate");
}

static void test_physical_target_oracles() {
    // 1. eSFR slanted edge: compare sharp edge vs Gaussian blurred + overshooting edge
    const int H = 64, W = 64;
    std::vector<float> esfr_sharp((size_t)H * W), esfr_blur((size_t)H * W), esfr_ring((size_t)H * W);
    const double slope = std::tan(5.0 * 3.14159265358979323846 / 180.0);
    for (int r = 0; r < H; ++r) {
        double edge_c = 32.0 + slope * (r - 32.0);
        for (int c = 0; c < W; ++c) {
            double d = (c - edge_c) * std::cos(5.0 * 3.14159265358979323846 / 180.0);
            esfr_sharp[(size_t)r * W + c] = 0.2f + 0.6f * (float)(0.5 * (1.0 + std::tanh(d / 0.45)));
            esfr_blur[(size_t)r * W + c]  = 0.2f + 0.6f * (float)(0.5 * (1.0 + std::tanh(d / 1.8)));
            double r_osc = std::exp(-d * d / 2.2) * std::sin(d * 1.4) * 0.14;
            esfr_ring[(size_t)r * W + c]  = 0.2f + 0.6f * (float)(0.5 * (1.0 + std::tanh(d / 0.45)) + r_osc);
        }
    }
    auto m_sharp = heraw_eval::measure_esfr_mtf50(esfr_sharp.data(), H, W);
    auto m_blur  = heraw_eval::measure_esfr_mtf50(esfr_blur.data(), H, W);
    auto m_ring  = heraw_eval::measure_esfr_mtf50(esfr_ring.data(), H, W);
    require(m_sharp.valid && m_blur.valid && m_ring.valid, "eSFR valid");
    require(std::abs(m_sharp.edge_angle_deg - 5.0) < 0.75, "eSFR edge angle estimate within 0.75 deg");
    require(m_sharp.mtf50_cy_px > m_blur.mtf50_cy_px + 0.12, "eSFR MTF50 strictly higher for sharp edge");
    require(m_ring.overshoot_pct > 5.0 && m_sharp.overshoot_pct < 2.0, "eSFR catches overshoot ringing");

    // 2. Dead leaves transfer
    const int DN = 64;
    std::vector<float> ref_dl((size_t)DN * DN), low_dl((size_t)DN * DN);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> uni(-0.5f, 0.5f);
    for (int i = 0; i < DN * DN; ++i) ref_dl[i] = 0.5f + uni(rng);
    for (int y = 0; y < DN; ++y) {
        for (int x = 0; x < DN; ++x) {
            int xm = (x - 1 + DN) % DN, xp = (x + 1) % DN;
            low_dl[(size_t)y * DN + x] = 0.25f * ref_dl[(size_t)y * DN + xm] +
                                         0.50f * ref_dl[(size_t)y * DN + x] +
                                         0.25f * ref_dl[(size_t)y * DN + xp];
        }
    }
    auto dl = heraw_eval::measure_dead_leaves_transfer(low_dl.data(), ref_dl.data(), DN);
    require(dl.valid && dl.ratio_low_band > 0.7 && dl.ratio_high_band < dl.ratio_low_band,
            "Dead leaves PSD ratio reflects high-frequency attenuation");

    // 3. Achromatic aliasing & shadow signed bias & SFM
    const int AH = 32, AW = 32;
    std::vector<float> rgb_clean((size_t)AH * AW * 3), rgb_zip((size_t)AH * AW * 3);
    for (int y = 0; y < AH; ++y) {
        for (int x = 0; x < AW; ++x) {
            float l = 0.5f + 0.25f * ((x & 1) ? 1.0f : -1.0f);
            float zip = ((x & 1) ? 0.08f : -0.08f);
            rgb_clean[((size_t)y * AW + x) * 3 + 0] = l;
            rgb_clean[((size_t)y * AW + x) * 3 + 1] = l;
            rgb_clean[((size_t)y * AW + x) * 3 + 2] = l;
            rgb_zip[((size_t)y * AW + x) * 3 + 0] = l + zip;
            rgb_zip[((size_t)y * AW + x) * 3 + 1] = l - zip;
            rgb_zip[((size_t)y * AW + x) * 3 + 2] = l + zip;
        }
    }
    require(heraw_eval::measure_achromatic_aliasing_ratio(rgb_clean.data(), AH, AW) < 1e-6, "Clean grey has zero aliasing");
    require(heraw_eval::measure_achromatic_aliasing_ratio(rgb_zip.data(), AH, AW) > 0.05, "Zippered grey detected");

    std::mt19937 rng2(123);
    std::normal_distribution<float> nshadow(0.0f, 0.002f);
    std::vector<float> t_sh(2000), o_signed(2000), o_relu(2000);
    for (size_t i = 0; i < 2000; ++i) {
        float tv = 0.0005f + 0.0015f * ((float)i / 2000.0f);
        float noisy = tv + nshadow(rng2);
        t_sh[i] = tv;
        o_signed[i] = noisy;
        o_relu[i] = std::max(0.0f, noisy);
    }
    auto sb_signed = heraw_eval::measure_shadow_linearity_bias(o_signed.data(), t_sh.data(), 2000);
    auto sb_relu   = heraw_eval::measure_shadow_linearity_bias(o_relu.data(), t_sh.data(), 2000);
    require(sb_signed.valid && sb_relu.valid, "Shadow bias valid");
    require(std::abs(sb_signed.mean_signed_rel_bias) < 0.08, "Signed shadow samples stay unbiased");
    require(sb_relu.mean_signed_rel_bias > 0.25, "ReLU clamp creates severe positive shadow bias");

    // 4. Residual noise SFM (white vs smoothed)
    std::vector<float> wn((size_t)DN * DN), sn((size_t)DN * DN);
    for (int i = 0; i < DN * DN; ++i) wn[i] = nshadow(rng2);
    for (int y = 0; y < DN; ++y) {
        for (int x = 0; x < DN; ++x) {
            sn[(size_t)y * DN + x] = 0.25f * (wn[(size_t)y * DN + ((x + 1) % DN)] + wn[(size_t)y * DN + x] +
                                              wn[(size_t)((y + 1) % DN) * DN + x] + wn[(size_t)((y + 1) % DN) * DN + ((x + 1) % DN)]);
        }
    }
    double sfm_white = heraw_eval::measure_residual_noise_sfm(wn.data(), DN);
    double sfm_smooth = heraw_eval::measure_residual_noise_sfm(sn.data(), DN);
    require(sfm_white > 0.45 && sfm_white > sfm_smooth * 2.5, "SFM separates white vs smoothed noise");

    heraw_eval::SelfEvalReport json_rep;
    json_rep.mode_label = "unit-test";
    json_rep.scale_factor = 2;
    json_rep.frame_count = 6;
    json_rep.residual_noise_sfm = sfm_white;
    std::string jpath = (std::filesystem::temp_directory_path() / "nijin_test_eval.json").string();
    require(heraw_eval::write_self_eval_json(jpath, json_rep), "Write eval self JSON");
    std::error_code ec;
    std::filesystem::remove(jpath, ec);
}

int main() {
    test_stratified_loo_chi2();
    test_frc_and_phase_diversity();
    test_physical_target_oracles();
    std::printf("test_suite_metrology PASS\n");
    return 0;
}
