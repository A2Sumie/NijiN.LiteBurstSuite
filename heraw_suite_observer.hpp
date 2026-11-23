// heraw_suite_observer.hpp -- Camera-Burst Spatiotemporal-Radiometric Subject Observer Matrix.
// Five physical deterministic modules (P^3 bundle + roughness, optical lock-in + phosphor decay,
// object SE(3)/kinematics/ballistic g0, Helmholtz-gated BOS, and Rank-2 modal vibrometry / 14-bit rPPG).
// C++20, header-only, strictly 7-bit ASCII.
#pragma once

#include "heraw_suite_features.hpp"
#include "heraw_suite_measurement.hpp"
#include "heraw_version.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <numbers>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace heraw_suite::observer {

enum class ObserverGateMode { Auto = 0, Relaxed = 1, Force = 2 };

struct ObserverOptions {
    std::string camera_make;
    std::string camera_model;
    float readout_ms_override = -1.0f;       // >= 0 overrides camera readout table; 0 = global shutter
    float pitch_deg = 0.0f;                  // Camera pitch relative to horizon
    bool compute_roughness = false;
    bool compute_veil = false;
    bool compute_metalness = false;
    bool compute_diffuse_intrinsic = false;
    bool compute_ac_light = false;
    bool compute_phosphor = false;
    bool compute_light_split = false;
    bool compute_kinematics = false;         // ttc, vorticity, nonrigid_strain, moving-body stereo
    bool compute_schlieren = false;          // schlieren_opd
    bool compute_vibrometry = false;         // vibration_amp, modal freq & damping
    bool compute_vital = false;              // vital_perfusion, BPM, BCG
    int cam_flip = 0;                        // 0=none, 3=180, 5=90 CCW (EXIF8), 6=90 CW (EXIF6)
    int shutter_kind = -1;                   // 0=mech/EFCS, 1=electronic RS, 2=global, <0=unknown
    std::array<float, 3> cam_wb{{1.0f, 1.0f, 1.0f}};
    ObserverGateMode gate_mode = ObserverGateMode::Auto;
    const heraw_meas::PreDestripeTapSet* pre_destripe_tap = nullptr;
    heraw_meas::ClockProvenance clock_prov{};
    bool compute_loca_sign = false;          // O6
    bool compute_modal_wave = false;         // O7
    bool compute_seeing = false;             // O8
    bool compute_refraction = false;         // O9
    bool compute_streak = false;             // O10
    float plate_scale_arcsec_px = 0.0f;      // Optional WCS scale for O10 arcsec/s
    std::vector<double> frame_exposures_s;   // Per-frame exposure duration for F-H dual-window GS estimator
};

struct ObserverReport {
    // O1: P^3 Projective Radiometry & Scattering
    Gate p3_gate;
    std::array<float, 3> illuminant_rgb{{0.0f, 0.0f, 0.0f}};
    bool has_illuminant = false;
    std::array<float, 3> airlight_rgb{{0.0f, 0.0f, 0.0f}};
    bool has_airlight = false;
    float p3_eigen_ratio = 0.0f;
    Gate roughness_gate;
    float median_roughness_rad = 0.0f;
    float angular_sweep_rad = 0.0f;
    float mean_metalness = 0.0f;
    float mean_veil_transmission = 1.0f;

    // O2: Optical Lock-In Light-Transport & Phosphor Decay
    Gate lockin_gate;
    float readout_ms_used = 0.0f;
    bool readout_self_calibrated = false;
    float ac_freq_hz = 0.0f;
    float ac_mod_depth = 0.0f;
    float ac_light_fraction_mean = 0.0f;
    Gate phosphor_gate;
    float phosphor_decay_ms = 0.0f;
    float red_blue_phase_lag_deg = 0.0f;
    float chirp_glrt_delta = 0.0f;
    float chirp_f_dot_hz_s = 0.0f;
    bool chirp_active = false;
    bool clock_ambiguous = false;
    std::string clock_source = "model_estimate";
    int64_t clock_tick_num = 0;
    uint64_t clock_tick_den = 1;
    bool clock_row_calibrated = false;
    float gs_dual_window_hz = 0.0f;
    std::array<int, 3> astro_hyp_counts{{0, 0, 0}};

    // O3: Object-Centric SE(3), Continuum Kinematics & Ballistic g0
    Gate kinematics_gate;
    int moving_body_count = 0;
    bool rigid_self_stereo_observable = false;
    float min_ttc_s = 0.0f;
    float max_abs_vorticity_rad_s = 0.0f;
    float median_nonrigid_strain_px = 0.0f;
    Gate tachometry_gate;
    float estimated_frame_dt_s = 0.0f;
    bool velocity_diversity_deblur_ready = false;
    Gate ballistic_gate;
    float ballistic_distance_m = 0.0f;
    float ballistic_accel_1x_px_s2 = 0.0f;

    // O4: Helmholtz-Gated Quantitative BOS
    Gate schlieren_gate;
    float schlieren_irrotational_ratio = 0.0f;
    float max_schlieren_opd = 0.0f;
    std::string schlieren_units = "px*rad";

    // O5: Rolling-Shutter Modal Vibrometry, Damping & 14-bit Perfusion
    Gate vibrometry_gate;
    float modal_freq_hz = 0.0f;
    float modal_max_amp_1x_px = 0.0f;
    float modal_snr = 0.0f;
    float modal_damping_zeta = 0.0f;
    bool damping_observable = false;
    Gate vital_gate;
    float vital_bpm = 0.0f;
    float vital_snr = 0.0f;
    float bcg_amp_1x_px = 0.0f;

    // O6..O10: Extended Scientific Observers
    Gate loca_gate;
    float loca_slope_px_per_rho = 0.0f;
    int defocus_sign = 0;
    Gate wave_gate;
    float wave_phase_vel_px_s = 0.0f;
    Gate seeing_gate;
    float seeing_r0_norm = 0.0f;
    float diff_tilt_rms_1x_px = 0.0f;
    Gate refraction_gate;
    float refraction_rms_1x_px = 0.0f;
    Image bottom_albedo_half;
    Gate streak_gate;
    int streak_count = 0;
    float streak_ang_vel = 0.0f;
    std::string streak_units = "px/s";
    float diff_photometry_rms = 0.0f;

    std::vector<std::string> requested_observers;
    std::vector<std::string> executed_observers;
    std::map<std::string, std::vector<float>> maps;
    std::map<std::string, Gate> gates;
    Image diffuse_intrinsic_half;
    Image light_dc_half;
    Image light_ac_half;
    std::vector<float> moving_body_depth_half;
    std::vector<float> moving_body_conf_half;
};

namespace detail {

inline std::string to_upper_ascii(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

inline float lookup_sensor_readout_ms(const std::string& make, const std::string& model,
                                      int H1 = 0, int shutter_kind = -1,
                                      bool* out_known_cal = nullptr) {
    auto set_cal = [&](bool v) { if (out_known_cal) *out_known_cal = v; };
    const std::string m = to_upper_ascii(make + " " + model);
    if (shutter_kind == 2 || m.find("9M3") != std::string::npos ||
        m.find("A9 III") != std::string::npos || m.find("A9III") != std::string::npos) {
        set_cal(true);
        return 0.0f;
    }
    if (shutter_kind == 0) {
        set_cal(false);
        return 3.60f;
    }
    if (m.find("Z 8") != std::string::npos || m.find("Z8") != std::string::npos ||
        m.find("Z 9") != std::string::npos || m.find("Z9") != std::string::npos ||
        m.find("N-RAW") != std::string::npos) {
        set_cal(true);
        const float aspect_sc = (H1 > 0 && H1 < 5200) ? (static_cast<float>(H1) / 5504.0f) : 1.0f;
        return 3.70f * aspect_sc;
    }
    if (m.find("DXO ONE") != std::string::npos || m.find("DXO_ONE") != std::string::npos ||
        m.find("IMX183") != std::string::npos) {
        set_cal(true);
        const int rows = (H1 > 0) ? H1 : 3694;
        return 0.01216667f * static_cast<float>(rows);
    }
    if (m.find("ILCE-1") != std::string::npos || m.find("SONY A1") != std::string::npos) {
        set_cal(true);
        return (H1 > 0 && H1 <= 4000) ? (2.55f * (static_cast<float>(H1) / 3744.0f)) : 3.85f;
    }
    if (m.find("X-H2S") != std::string::npos || m.find("XH2S") != std::string::npos) {
        set_cal(true);
        return 5.50f;
    }
    if (m.find("EOS R3") != std::string::npos || m.find(" R3") != std::string::npos) {
        set_cal(true);
        return 5.00f;
    }
    if (m.find("R5M2") != std::string::npos || m.find("R5 MARK II") != std::string::npos) {
        set_cal(true);
        return 6.30f;
    }
    if ((m.find("EOS R5") != std::string::npos || m.find(" R5") != std::string::npos) &&
        m.find("R50") == std::string::npos) {
        set_cal(true);
        return 16.30f;
    }
    if (m.find("Z6_3") != std::string::npos || m.find("Z6III") != std::string::npos ||
        m.find("Z 6III") != std::string::npos || m.find("NIKON ZR") != std::string::npos ||
        m.find(" ZR") != std::string::npos) {
        set_cal(true);
        return (H1 > 0 && H1 < 3800) ? 9.50f : 14.50f;
    }
    if (m.find("Z50_2") != std::string::npos || m.find("Z5_2") != std::string::npos) {
        set_cal(true);
        return 29.0f;
    }
    if (m.find("Z F") != std::string::npos || m.find("Z6_2") != std::string::npos ||
        m.find("Z 6_2") != std::string::npos || m.find("Z 6") != std::string::npos ||
        m.find("Z6") != std::string::npos) {
        set_cal(true);
        return 26.5f;
    }
    if (m.find("Z 7") != std::string::npos || m.find("Z7") != std::string::npos) {
        set_cal(true);
        return 48.0f;
    }
    if (m.find("7M4") != std::string::npos || m.find("A7 IV") != std::string::npos) {
        set_cal(true);
        return 67.0f;
    }
    if (m.find("7RM5") != std::string::npos || m.find("A7R V") != std::string::npos) {
        set_cal(true);
        return 99.0f;
    }
    set_cal(false);
    return 15.0f;
}

// Deterministic symmetric Jacobi eigensolver for small dense D x D matrices (ascending eigenvalues).
template <int D>
inline void jacobi_eig_sym(std::array<std::array<double, D>, D> A,
                           std::array<double, D>& evals,
                           std::array<std::array<double, D>, D>& evecs) {
    for (int i = 0; i < D; ++i)
        for (int j = 0; j < D; ++j)
            evecs[i][j] = (i == j) ? 1.0 : 0.0;
    for (int sweep = 0; sweep < 40; ++sweep) {
        int p = 0, q = 1;
        double mx = 0.0;
        for (int i = 0; i < D; ++i)
            for (int j = i + 1; j < D; ++j)
                if (std::fabs(A[i][j]) > mx) { mx = std::fabs(A[i][j]); p = i; q = j; }
        if (mx < 1e-13) break;
        const double app = A[p][p], aqq = A[q][q], apq = A[p][q];
        const double tau = (aqq - app) / (2.0 * apq);
        const double t = (tau >= 0.0) ? (1.0 / (tau + std::sqrt(1.0 + tau * tau)))
                                      : (-1.0 / (-tau + std::sqrt(1.0 + tau * tau)));
        const double c = 1.0 / std::sqrt(1.0 + t * t), s = t * c;
        A[p][p] = app - t * apq;
        A[q][q] = aqq + t * apq;
        A[p][q] = A[q][p] = 0.0;
        for (int r = 0; r < D; ++r) {
            if (r == p || r == q) continue;
            const double arp = A[r][p], arq = A[r][q];
            A[r][p] = A[p][r] = c * arp - s * arq;
            A[r][q] = A[q][r] = s * arp + c * arq;
        }
        for (int r = 0; r < D; ++r) {
            const double vp = evecs[r][p], vq = evecs[r][q];
            evecs[r][p] = c * vp - s * vq;
            evecs[r][q] = s * vp + c * vq;
        }
    }
    std::array<int, D> idx{};
    for (int i = 0; i < D; ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) { return A[a][a] < A[b][b]; });
    std::array<std::array<double, D>, D> ev_copy = evecs;
    for (int j = 0; j < D; ++j) {
        evals[j] = A[idx[j]][idx[j]];
        for (int r = 0; r < D; ++r) evecs[r][j] = ev_copy[r][idx[j]];
    }
}

inline void fft_1d_c(std::vector<double>& re, std::vector<double>& im, bool inverse) {
    heraw_refocus::detail::fft_1d(re, im, inverse);
}

inline void fft_2d_rect(std::vector<double>& re, std::vector<double>& im, int H, int W, bool inverse) {
    std::vector<double> tr(static_cast<size_t>(W)), ti(static_cast<size_t>(W));
    for (int y = 0; y < H; ++y) {
        const size_t off = static_cast<size_t>(y) * W;
        std::copy_n(re.data() + off, W, tr.data());
        std::copy_n(im.data() + off, W, ti.data());
        fft_1d_c(tr, ti, inverse);
        std::copy_n(tr.data(), W, re.data() + off);
        std::copy_n(ti.data(), W, im.data() + off);
    }
    std::vector<double> cr(static_cast<size_t>(H)), ci(static_cast<size_t>(H));
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) { cr[y] = re[static_cast<size_t>(y) * W + x]; ci[y] = im[static_cast<size_t>(y) * W + x]; }
        fft_1d_c(cr, ci, inverse);
        for (int y = 0; y < H; ++y) { re[static_cast<size_t>(y) * W + x] = cr[y]; im[static_cast<size_t>(y) * W + x] = ci[y]; }
    }
}

inline double frame_time_sec(const BurstView& b, size_t k, bool& has_real_time) {
    if (b.times_seconds && b.times_seconds->size() == b.frames_lin.size()) {
        double lo = *std::min_element(b.times_seconds->begin(), b.times_seconds->end());
        double hi = *std::max_element(b.times_seconds->begin(), b.times_seconds->end());
        if (hi - lo > 1e-6) {
            has_real_time = true;
            return (*b.times_seconds)[k] - (*b.times_seconds)[0];
        }
    }
    has_real_time = false;
    if (b.capture_order && b.capture_order->size() == b.frames_lin.size()) {
        return static_cast<double>((*b.capture_order)[k]) - static_cast<double>((*b.capture_order)[0]);
    }
    return static_cast<double>(k);
}

inline void sorted_time_stats(const std::vector<double>& ts, double& out_span, double& out_min_dt) {
    out_span = 0.0;
    out_min_dt = 0.0;
    if (ts.size() < 2) return;
    std::vector<double> st = ts;
    std::sort(st.begin(), st.end());
    out_span = std::max(0.0, st.back() - st.front());
    double min_dt = 1e9;
    for (size_t k = 1; k < st.size(); ++k) {
        const double d = st[k] - st[k - 1];
        if (d > 1e-4 && d < min_dt) min_dt = d;
    }
    out_min_dt = (min_dt < 1e8) ? min_dt : out_span;
}

inline std::array<double, 2> gravity_dir(int cam_flip) noexcept {
    switch (cam_flip) {
        case 3:
        case 4: return {{-1.0, 0.0}};
        case 5:
        case 8: return {{0.0, -1.0}};
        case 6:
        case 7: return {{0.0, 1.0}};
        default: return {{1.0, 0.0}};
    }
}

// Build unoccluded frame index mapping using camera homography + residual parallax flow
inline void aligned_source_coords(const BurstView& b, const FieldResult& fields, size_t k,
                                  int yr, int xc, float& sy_1x, float& sx_1x, bool& inside_ok) {
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hi = static_cast<size_t>(yr) * Wh + xc;
    const float qy = static_cast<float>(yr) + b.flows_fy[k][hi];
    const float qx = static_cast<float>(xc) + b.flows_fx[k][hi];
    (void)fields;
    inside_ok = (qy >= 0.5f && qx >= 0.5f && qy <= static_cast<float>(Hh - 2) && qx <= static_cast<float>(Wh - 2));
    sy_1x = 2.0f * qy + 0.5f;
    sx_1x = 2.0f * qx + 0.5f;
}

} // namespace detail

// O1: P^3 Homogeneous Color-Line Bundle, Specular Intrinsic Layer, Scattering Veil & Angular Roughness
inline void run_p3_radiometry(const BurstView& b, const FieldResult& fields,
                              const ObserverOptions& opts, ObserverReport& rep,
                              const ParallelRows& parallel_rows) {
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hp = static_cast<size_t>(Hh) * Wh, n = b.frames_lin.size();
    const bool need_any = opts.compute_roughness || opts.compute_veil || opts.compute_metalness || opts.compute_diffuse_intrinsic;
    if (!need_any || n < 3) {
        rep.p3_gate.reason = "P^3 radiometry requires >=3 frames and requested output";
        return;
    }
    const auto& occ_map = fields.maps.count("occlusion") ? &fields.maps.at("occlusion") : nullptr;
    const auto& depth_map = fields.maps.count("depth") ? &fields.maps.at("depth") : nullptr;

    struct ActiveLine {
        std::array<float, 3> p0;
        std::array<float, 3> dir;
        std::array<float, 4> q1;
        std::array<float, 4> q2;
        float excursion;
        float weight;
        float fit_err;
        size_t hi;
        bool nonneg_dir = false;
    };
    const int stride = (hp > 1500000) ? 2 : 1;
    std::vector<ActiveLine> lines;
    lines.reserve(8192);

    std::vector<std::array<float, 3>> pix_cols;
    std::vector<float> pix_w;
    pix_cols.reserve(n);
    pix_w.reserve(n);

    for (int yr = 1; yr + 1 < Hh; yr += stride) {
        for (int xc = 1; xc + 1 < Wh; xc += stride) {
            const size_t hi = static_cast<size_t>(yr) * Wh + xc;
            if (occ_map && (*occ_map)[hi] > 0.08f && (*occ_map)[hi] < 0.92f) continue;
            float glo = 1e9f, ghi = -1e9f;
            int vis_cnt = 0;
            for (size_t k = 0; k < n; ++k) {
                if (b.mw_half[k][hi] < 0.35f) continue;
                const float qy = yr + b.flows_fy[k][hi], qx = xc + b.flows_fx[k][hi];
                if (qy < 0.5f || qx < 0.5f || qy > static_cast<float>(Hh - 2) || qx > static_cast<float>(Wh - 2)) continue;
                const float g = heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, qy, qx);
                if (!std::isfinite(g) || g >= 0.96f) continue;
                glo = std::min(glo, g); ghi = std::max(ghi, g); ++vis_cnt;
            }
            if (vis_cnt < 3) continue;
            const float nsd = std::sqrt(heraw_suite::detail::variance(b, 0.5f * (glo + ghi)));
            if ((ghi - glo) < 5.0f * nsd) continue;
            const float gmag_loc = heraw_suite::detail::gradient_mag(
                b.g_halfs[0], Hh, Wh, static_cast<float>(yr), static_cast<float>(xc));

            pix_cols.clear(); pix_w.clear();
            std::array<double, 3> cmean{{0.0, 0.0, 0.0}};
            double wsum = 0.0;
            for (size_t k = 0; k < n; ++k) {
                if (b.mw_half[k][hi] < 0.35f) continue;
                float sy1, sx1; bool ok = false;
                detail::aligned_source_coords(b, fields, k, yr, xc, sy1, sx1, ok);
                if (!ok) continue;
                const auto rgb = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                if (rgb[0] >= 0.96f || rgb[1] >= 0.96f || rgb[2] >= 0.96f) continue;
                if (rgb[0] < -0.05f || rgb[1] < -0.05f || rgb[2] < -0.05f) continue;
                const float wt = b.mw_half[k][hi];
                pix_cols.push_back(rgb);
                pix_w.push_back(wt);
                for (int c = 0; c < 3; ++c) cmean[c] += wt * rgb[c];
                wsum += wt;
            }
            if (pix_cols.size() < 3 || wsum <= 1e-6) continue;
            std::array<float, 3> p0{};
            for (int c = 0; c < 3; ++c) p0[c] = static_cast<float>(cmean[c] / wsum);

            std::array<std::array<double, 3>, 3> cov{};
            for (size_t i = 0; i < pix_cols.size(); ++i) {
                const double wt = pix_w[i];
                for (int r = 0; r < 3; ++r) {
                    const double dr = pix_cols[i][r] - p0[r];
                    for (int c = r; c < 3; ++c) cov[r][c] += wt * dr * (pix_cols[i][c] - p0[c]);
                }
            }
            for (int r = 0; r < 3; ++r) for (int c = 0; c < r; ++c) cov[r][c] = cov[c][r];
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) cov[r][c] /= wsum;

            std::array<double, 3> ev{};
            std::array<std::array<double, 3>, 3> vec{};
            detail::jacobi_eig_sym<3>(cov, ev, vec);
            const double rank1_var = ev[2];
            const double orth_rms = std::sqrt(std::max(0.0, ev[0] + ev[1]));
            const float exc = static_cast<float>(std::sqrt(std::max(0.0, rank1_var)));
            if (exc < 4.5f * nsd || orth_rms > std::max(0.015, 0.30 * exc)) continue;

            std::array<float, 3> dir{{static_cast<float>(vec[0][2]), static_cast<float>(vec[1][2]), static_cast<float>(vec[2][2])}};
            if (dir[0] + dir[1] + dir[2] < 0.0f) for (float& d : dir) d = -d;
            const bool is_nonneg = (dir[0] >= -0.02f && dir[1] >= -0.02f && dir[2] >= -0.02f);
            if (!is_nonneg && exc < 0.55f * gmag_loc) continue;

            // Construct orthonormal basis {q1, q2} of projective line span([p0; 1], [dir; 0]) in R^4
            std::array<double, 4> u1{{p0[0], p0[1], p0[2], 1.0}};
            const double n1 = std::sqrt(u1[0]*u1[0] + u1[1]*u1[1] + u1[2]*u1[2] + u1[3]*u1[3]);
            for (double& v : u1) v /= n1;
            std::array<double, 4> u2{{dir[0], dir[1], dir[2], 0.0}};
            const double dot12 = u1[0]*u2[0] + u1[1]*u2[1] + u1[2]*u2[2] + u1[3]*u2[3];
            for (int j = 0; j < 4; ++j) u2[j] -= dot12 * u1[j];
            const double n2 = std::sqrt(u2[0]*u2[0] + u2[1]*u2[1] + u2[2]*u2[2] + u2[3]*u2[3]);
            if (n2 < 1e-6) continue;
            for (double& v : u2) v /= n2;

            ActiveLine al{};
            al.p0 = p0; al.dir = dir;
            for (int j = 0; j < 4; ++j) { al.q1[j] = static_cast<float>(u1[j]); al.q2[j] = static_cast<float>(u2[j]); }
            al.excursion = exc;
            al.weight = std::min(10.0f, exc / std::max(1e-5f, nsd));
            al.fit_err = static_cast<float>(orth_rms);
            al.hi = hi;
            al.nonneg_dir = is_nonneg;
            lines.push_back(al);
        }
    }

    auto solve_bundle = [&](const std::vector<size_t>& sub_idx, std::array<float, 4>& v_out, float& eig_ratio) {
        std::array<std::array<double, 4>, 4> M{};
        for (size_t idx : sub_idx) {
            const auto& ln = lines[idx];
            const double w = ln.weight;
            for (int r = 0; r < 4; ++r) {
                for (int c = r; c < 4; ++c) {
                    const double p_perp = ((r == c) ? 1.0 : 0.0) -
                        static_cast<double>(ln.q1[r]) * ln.q1[c] - static_cast<double>(ln.q2[r]) * ln.q2[c];
                    M[r][c] += w * p_perp;
                }
            }
        }
        for (int r = 0; r < 4; ++r) for (int c = 0; c < r; ++c) M[r][c] = M[c][r];
        std::array<double, 4> evals{};
        std::array<std::array<double, 4>, 4> evecs{};
        detail::jacobi_eig_sym<4>(M, evals, evecs);
        for (int r = 0; r < 4; ++r) v_out[r] = static_cast<float>(evecs[r][0]);
        eig_ratio = static_cast<float>(evals[1] / std::max(1e-9, evals[0]));
    };

    auto line_dist = [](const ActiveLine& ln, const std::array<float, 4>& v) -> float {
        const float d1 = ln.q1[0]*v[0] + ln.q1[1]*v[1] + ln.q1[2]*v[2] + ln.q1[3]*v[3];
        const float d2 = ln.q2[0]*v[0] + ln.q2[1]*v[1] + ln.q2[2]*v[2] + ln.q2[3]*v[3];
        return std::sqrt(std::max(0.0f, 1.0f - d1 * d1 - d2 * d2));
    };

    const double er_min = (opts.gate_mode == ObserverGateMode::Auto) ? 6.0 :
                          ((opts.gate_mode == ObserverGateMode::Relaxed) ? 3.5 : 1.0);
    if (lines.size() >= 8) {
        // Test deterministic pairs with diverse base hues to separate illuminant (v_W ~ 0) vs airlight (v_W > 0.1)
        const size_t step = std::max<size_t>(1, lines.size() / 40);
        std::vector<size_t> best_glint_inl, best_air_inl;
        std::array<float, 4> best_glint_v{}, best_air_v{};

        std::vector<size_t> cand_all;
        for (size_t i = 0; i < lines.size(); i += step) {
            const auto& lk = lines[i];
            const float p0n = std::hypot(lk.p0[0], lk.p0[1], lk.p0[2]);
            const float p_dot = (p0n > 1e-4f) ? ((lk.p0[0]*lk.dir[0] + lk.p0[1]*lk.dir[1] + lk.p0[2]*lk.dir[2]) / p0n) : 1.0f;
            if (std::fabs(p_dot) > 0.995f) continue;
            cand_all.push_back(i);
        }
        for (size_t a = 0; a < cand_all.size(); ++a) {
            for (size_t b_i = a + 1; b_i < cand_all.size(); ++b_i) {
                const auto& l0 = lines[cand_all[a]];
                const auto& l1 = lines[cand_all[b_i]];
                const float dot_p = (l0.p0[0]*l1.p0[0] + l0.p0[1]*l1.p0[1] + l0.p0[2]*l1.p0[2]) /
                    std::max(1e-7f, std::hypot(l0.p0[0], l0.p0[1], l0.p0[2]) * std::hypot(l1.p0[0], l1.p0[1], l1.p0[2]));
                if (dot_p > 0.975f) continue; // Need chromatic hue diversity >= 12 deg
                std::array<float, 4> v{};
                float er = 0.0f;
                solve_bundle({cand_all[a], cand_all[b_i]}, v, er);
                if (er < 4.0f) continue;
                const float vrgb_norm = std::hypot(v[0], v[1], v[2]);
                if (std::fabs(v[3]) < 0.09f && vrgb_norm > 0.5f) {
                    std::array<float, 3> cd{{v[0]/vrgb_norm, v[1]/vrgb_norm, v[2]/vrgb_norm}};
                    if (cd[0] + cd[1] + cd[2] < 0.0f) { for (float& x : cd) x = -x; for (float& x : v) x = -x; }
                    if (cd[0] < -0.05f || cd[1] < -0.05f || cd[2] < -0.05f) continue;
                    std::vector<size_t> inl;
                    for (size_t k = 0; k < lines.size(); ++k) {
                        const auto& lk = lines[k];
                        if (!lk.nonneg_dir) continue;
                        const float p0n = std::hypot(lk.p0[0], lk.p0[1], lk.p0[2]);
                        const float p_dot = (p0n > 1e-4f) ? ((lk.p0[0]*lk.dir[0] + lk.p0[1]*lk.dir[1] + lk.p0[2]*lk.dir[2]) / p0n) : 1.0f;
                        if (std::fabs(p_dot) > 0.995f) continue;
                        if (line_dist(lk, v) < 0.12f) inl.push_back(k);
                    }
                    if (inl.size() > best_glint_inl.size()) {
                        best_glint_inl.swap(inl);
                        best_glint_v = v;
                    }
                } else if (std::fabs(v[3]) >= 0.12f && vrgb_norm > 0.05f) {
                    if (v[3] < 0.0f) for (float& x : v) x = -x;
                    std::array<float, 3> ainf{{v[0] / v[3], v[1] / v[3], v[2] / v[3]}};
                    if (ainf[0] < 0.04f || ainf[1] < 0.04f || ainf[2] < 0.04f ||
                        ainf[0] > 1.5f || ainf[1] > 1.5f || ainf[2] > 1.5f) continue;
                    std::vector<size_t> inl;
                    for (size_t k = 0; k < lines.size(); ++k) {
                        const auto& lk = lines[k];
                        const float p0n = std::hypot(lk.p0[0], lk.p0[1], lk.p0[2]);
                        const float p_dot = (p0n > 1e-4f) ? ((lk.p0[0]*lk.dir[0] + lk.p0[1]*lk.dir[1] + lk.p0[2]*lk.dir[2]) / p0n) : 1.0f;
                        if (std::fabs(p_dot) > 0.995f) continue;
                        if (line_dist(lk, v) < 0.10f) inl.push_back(k);
                    }
                    if (inl.size() > best_air_inl.size()) {
                        best_air_inl.swap(inl);
                        best_air_v = v;
                    }
                }
            }
        }
        if (best_glint_inl.size() >= 6) {
            std::array<float, 4> vf{};
            float er = 0.0f;
            solve_bundle(best_glint_inl, vf, er);
            if (vf[0] + vf[1] + vf[2] < 0.0f) for (float& x : vf) x = -x;
            const float gn = std::hypot(vf[0], vf[1], vf[2]);
            if (er >= er_min && std::fabs(vf[3]) < 0.12f && gn > 0.5f &&
                vf[0] >= 0.0f && vf[1] >= 0.0f && vf[2] >= 0.0f) {
                rep.has_illuminant = true;
                rep.p3_eigen_ratio = er;
                rep.illuminant_rgb = {{vf[0] / gn, vf[1] / gn, vf[2] / gn}};
            }
        }
        // Dichromatic plane-normal intersection on S^2 for curved surfaces with mixed shading + specular variation.
        if (!rep.has_illuminant && lines.size() >= 6) {
            std::array<std::array<double, 3>, 3> N_cov{};
            size_t n_planes = 0;
            for (const auto& lk : lines) {
                if (!lk.nonneg_dir) continue;
                const double nx = lk.p0[1] * lk.dir[2] - lk.p0[2] * lk.dir[1];
                const double ny = lk.p0[2] * lk.dir[0] - lk.p0[0] * lk.dir[2];
                const double nz = lk.p0[0] * lk.dir[1] - lk.p0[1] * lk.dir[0];
                const double nn = std::hypot(nx, ny, nz);
                const float p0n = std::hypot(lk.p0[0], lk.p0[1], lk.p0[2]);
                if (nn < 0.04 * std::max(1e-4f, p0n)) continue;
                const double ux[3] = {nx / nn, ny / nn, nz / nn};
                const double w = lk.weight;
                for (int r = 0; r < 3; ++r)
                    for (int c = r; c < 3; ++c) N_cov[r][c] += w * ux[r] * ux[c];
                ++n_planes;
            }
            if (n_planes >= 6) {
                for (int r = 0; r < 3; ++r) for (int c = 0; c < r; ++c) N_cov[r][c] = N_cov[c][r];
                std::array<double, 3> ev3{};
                std::array<std::array<double, 3>, 3> vec3{};
                detail::jacobi_eig_sym<3>(N_cov, ev3, vec3);
                const float er3 = static_cast<float>(ev3[1] / std::max(1e-9, ev3[0]));
                std::array<float, 3> cl{{static_cast<float>(vec3[0][0]), static_cast<float>(vec3[1][0]), static_cast<float>(vec3[2][0])}};
                if (cl[0] + cl[1] + cl[2] < 0.0f) for (float& v : cl) v = -v;
                if (er3 >= er_min && cl[0] >= 0.0f && cl[1] >= 0.0f && cl[2] >= 0.0f) {
                    rep.has_illuminant = true;
                    rep.p3_eigen_ratio = std::max(rep.p3_eigen_ratio, er3);
                    rep.illuminant_rgb = cl;
                }
            }
        }
        if (best_air_inl.size() >= 6) {
            std::array<float, 4> va{};
            float er = 0.0f;
            solve_bundle(best_air_inl, va, er);
            if (va[3] < 0.0f) for (float& x : va) x = -x;
            if (er >= er_min && va[3] > 0.10f) {
                std::array<float, 3> ac{{va[0] / va[3], va[1] / va[3], va[2] / va[3]}};
                if (ac[0] >= 0.04f && ac[1] >= 0.04f && ac[2] >= 0.04f &&
                    ac[0] <= 1.5f && ac[1] <= 1.5f && ac[2] <= 1.5f) {
                    rep.has_airlight = true;
                    rep.airlight_rgb = ac;
                    rep.p3_eigen_ratio = std::max(rep.p3_eigen_ratio, er);
                }
            }
        }
        rep.p3_gate.observable = rep.has_illuminant || rep.has_airlight;
        rep.p3_gate.measured = rep.p3_eigen_ratio;
        rep.p3_gate.threshold = er_min;
        rep.p3_gate.confidence = rep.p3_gate.observable ? std::min(1.0, rep.p3_eigen_ratio / 24.0) : 0.0;
        rep.p3_gate.units = "eigen_ratio";
        rep.p3_gate.forced = (opts.gate_mode == ObserverGateMode::Force && rep.p3_eigen_ratio < 6.0f && rep.p3_gate.observable);
        rep.p3_gate.reason = rep.p3_gate.observable ? "projective P^3 color-line bundle intersection locked" :
            "insufficient multi-hue specular or scattering variation (>=6 diversely colored pixels needed)";
    } else {
        rep.p3_gate.reason = "insufficient temporal color variation for P^3 line bundle";
    }

    auto neutral_wb_dir = [&]() -> std::array<float, 3> {
        const float r = 1.0f / std::max(0.05f, opts.cam_wb[0]);
        const float g = 1.0f / std::max(0.05f, opts.cam_wb[1]);
        const float bl = 1.0f / std::max(0.05f, opts.cam_wb[2]);
        const float nrm = std::max(1e-6f, std::hypot(r, g, bl));
        return {{r / nrm, g / nrm, bl / nrm}};
    };

    if (opts.compute_diffuse_intrinsic && (rep.has_illuminant || opts.gate_mode == ObserverGateMode::Force)) {
        rep.diffuse_intrinsic_half = {Hh, Wh, std::vector<float>(3 * hp, 0.0f)};
        const std::array<float, 3> ci = rep.has_illuminant ? rep.illuminant_rgb : neutral_wb_dir();
        const RowTask diff_rows = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) for (int x = 0; x < Wh; ++x) {
                const size_t i = static_cast<size_t>(y) * Wh + x;
                const auto ref_rgb = heraw_suite::detail::raw_rgb(b, 0, 2.0f * y + 0.5f, 2.0f * x + 0.5f);
                float min_proj = ref_rgb[0]*ci[0] + ref_rgb[1]*ci[1] + ref_rgb[2]*ci[2];
                std::array<float, 3> min_rgb = ref_rgb;
                for (size_t k = 1; k < n; ++k) {
                    if (b.mw_half[k][i] < 0.35f) continue;
                    float sy1, sx1; bool ok = false;
                    detail::aligned_source_coords(b, fields, k, y, x, sy1, sx1, ok);
                    if (!ok) continue;
                    const auto kr = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                    const float p = kr[0]*ci[0] + kr[1]*ci[1] + kr[2]*ci[2];
                    if (std::isfinite(p) && p < min_proj) { min_proj = p; min_rgb = kr; }
                }
                const float ref_proj = ref_rgb[0]*ci[0] + ref_rgb[1]*ci[1] + ref_rgb[2]*ci[2];
                const float exc = std::max(0.0f, ref_proj - min_proj);
                const float nsd = std::sqrt(2.0f * heraw_suite::detail::variance(b, std::max(0.0f, ref_rgb[1])));
                const float orth_err = std::hypot(
                    (ref_rgb[0] - min_rgb[0]) - exc * ci[0],
                    (ref_rgb[1] - min_rgb[1]) - exc * ci[1],
                    (ref_rgb[2] - min_rgb[2]) - exc * ci[2]);
                const float ms = (exc > 2.5f * nsd && orth_err <= 3.5f * nsd) ? (exc - 1.5f * nsd) : 0.0f;
                for (int c = 0; c < 3; ++c)
                    rep.diffuse_intrinsic_half.rgb[3 * i + c] = std::max(0.0f, ref_rgb[c] - ms * ci[c]);
            }
        };
        if (parallel_rows) parallel_rows(Hh, diff_rows); else diff_rows(0, Hh);
    }

    if (opts.compute_metalness) {
        std::vector<float> met(hp, 0.0f);
        Gate mg = rep.p3_gate;
        mg.units = "chromatic_conductor_score_0_1";
        double met_sum = 0.0;
        size_t met_cnt = 0, spec_cnt = 0;
        if ((rep.has_illuminant || opts.gate_mode == ObserverGateMode::Force) && !rep.has_airlight) {
            const std::array<float, 3> ci = rep.has_illuminant ? rep.illuminant_rgb : neutral_wb_dir();
            for (const auto& ln : lines) {
                if (!ln.nonneg_dir) continue;
                const float c_dot = std::fabs(ln.dir[0]*ci[0] + ln.dir[1]*ci[1] + ln.dir[2]*ci[2]);
                const float p0n = std::hypot(ln.p0[0], ln.p0[1], ln.p0[2]);
                const float p_dot = (p0n > 1e-4f) ? std::fabs((ln.p0[0]*ln.dir[0] + ln.p0[1]*ln.dir[1] + ln.p0[2]*ln.dir[2]) / p0n) : 1.0f;
                if (p_dot > 0.985f) continue;
                ++spec_cnt;
                if (c_dot < 0.92f) {
                    const float m = std::max(0.0f, std::min(1.0f, (0.92f - c_dot) / 0.25f));
                    met[ln.hi] = m;
                    met_sum += m;
                    ++met_cnt;
                }
            }
        }
        rep.mean_metalness = spec_cnt > 0 ? static_cast<float>(met_sum / spec_cnt) : 0.0f;
        rep.maps["metalness"] = std::move(met);
        const size_t min_met = (opts.gate_mode == ObserverGateMode::Auto) ? 16u :
                               ((opts.gate_mode == ObserverGateMode::Relaxed) ? 4u : 1u);
        mg.observable = (rep.has_illuminant || opts.gate_mode == ObserverGateMode::Force) &&
                        !rep.has_airlight && (met_cnt >= min_met);
        mg.forced = (opts.gate_mode == ObserverGateMode::Force && !rep.has_illuminant && mg.observable);
        mg.measured = rep.mean_metalness;
        mg.reason = mg.observable ? "specular line vector deviates from dielectric illuminant and shading ray" :
            "no non-dielectric metallic specular excursion detected above illuminant/airlight gate";
        rep.gates["metalness"] = std::move(mg);
    }

    if (opts.compute_veil) {
        std::vector<float> veil(hp, 1.0f);
        Gate vg;
        vg.units = "transmission_0_1";
        vg.threshold = 0.96;
        if (rep.has_airlight) {
            const auto& A = rep.airlight_rgb;
            double min_t = 1.0, sum_t = 0.0;
            size_t cnt_t = 0;
            const RowTask veil_rows = [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y) for (int x = 0; x < Wh; ++x) {
                    const size_t i = static_cast<size_t>(y) * Wh + x;
                    const auto ref_rgb0 = heraw_suite::detail::raw_rgb(b, 0, 2.0f * y + 0.5f, 2.0f * x + 0.5f);
                    const float d0 = std::hypot(ref_rgb0[0] - A[0], ref_rgb0[1] - A[1], ref_rgb0[2] - A[2]);
                    if (!std::isfinite(d0)) continue;
                    float max_d = d0, cur_d = d0;
                    int ok_n = 1;
                    for (size_t k = 1; k < n; ++k) {
                        if (b.mw_half[k][i] < 0.35f) continue;
                        float sy1, sx1; bool ok = false;
                        detail::aligned_source_coords(b, fields, k, y, x, sy1, sx1, ok);
                        if (!ok) continue;
                        const auto kr = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                        const float d = std::hypot(kr[0] - A[0], kr[1] - A[1], kr[2] - A[2]);
                        if (!std::isfinite(d)) continue;
                        max_d = std::max(max_d, d);
                        ++ok_n;
                    }
                    if (ok_n >= 3 && max_d > 0.03f) {
                        const float nsd = std::sqrt(heraw_suite::detail::variance(b, 0.5f));
                        if (max_d - cur_d > 4.0f * nsd) {
                            veil[i] = std::max(0.05f, std::min(1.0f, cur_d / max_d));
                        }
                    }
                }
            };
            if (parallel_rows) parallel_rows(Hh, veil_rows); else veil_rows(0, Hh);
            for (float v : veil) { sum_t += v; min_t = std::min<double>(min_t, v); if (v < 0.96f) ++cnt_t; }
            rep.mean_veil_transmission = static_cast<float>(sum_t / hp);
            vg.observable = cnt_t >= 16;
            vg.measured = min_t;
            vg.confidence = std::min(1.0, static_cast<double>(cnt_t) / 256.0);
            vg.reason = vg.observable ? "Koschmieder dynamic scattering airlight transmission recovered" :
                "scene free of dynamic scattering haze/plume";
        } else {
            vg.reason = "requires P^3 finite airlight intersection";
        }
        rep.maps["veil_transmission"] = std::move(veil);
        rep.gates["veil_transmission"] = std::move(vg);
    }

    if (opts.compute_roughness) {
        std::vector<float> rough(hp, 0.0f);
        const float f_h = heraw_suite::detail::half_focal_px(b);
        double max_sweep = 0.0;
        for (size_t k = 1; k < n; ++k) {
            if (k < fields.geometry.baseline.size() && f_h > 0.0f) {
                const float bh = std::hypot(fields.geometry.baseline[k][0], fields.geometry.baseline[k][1]);
                const float rspan = fields.geometry.rho_high - fields.geometry.rho_low;
                max_sweep = std::max(max_sweep, static_cast<double>(bh * std::max(std::fabs(fields.geometry.rho_high), std::fabs(rspan)) / f_h));
            }
        }
        rep.angular_sweep_rad = static_cast<float>(max_sweep);
        std::vector<float> alpha_vals;
        const double sweep_thr = (opts.gate_mode == ObserverGateMode::Auto) ? 0.0020 :
                                 ((opts.gate_mode == ObserverGateMode::Relaxed) ? 0.0010 : 0.0);
        if (depth_map && f_h > 0.0f && max_sweep >= sweep_thr && !fields.geometry.baseline.empty()) {
            const float rlow = fields.geometry.rho_low;
            const float rspan = std::max(1e-6f, fields.geometry.rho_high - fields.geometry.rho_low);
            const std::array<float, 3> ci = rep.has_illuminant ? rep.illuminant_rgb : neutral_wb_dir();
            for (const auto& ln : lines) {
                if (!ln.nonneg_dir) continue;
                if (std::fabs(ln.dir[0]*ci[0] + ln.dir[1]*ci[1] + ln.dir[2]*ci[2]) < 0.85f) continue;
                const int yr = static_cast<int>(ln.hi / static_cast<size_t>(Wh)), xc = static_cast<int>(ln.hi % static_cast<size_t>(Wh));
                const float rho = rlow + (*depth_map)[ln.hi] * rspan;
                if (std::fabs(rho) < 1e-4f) continue;
                std::vector<float> ms_list(n, -1.0f), ty(n, 0.0f), tx(n, 0.0f);
                float min_s = 1e9f, max_s = -1e9f; size_t peak_k = 0;
                for (size_t k = 0; k < n; ++k) {
                    if (b.mw_half[k][ln.hi] < 0.35f) continue;
                    float sy1, sx1; bool ok = false;
                    detail::aligned_source_coords(b, fields, k, yr, xc, sy1, sx1, ok);
                    if (!ok) continue;
                    const auto kr = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                    if (kr[0] >= 0.96f || kr[1] >= 0.96f || kr[2] >= 0.96f) continue;
                    const float proj = kr[0]*ci[0] + kr[1]*ci[1] + kr[2]*ci[2];
                    ms_list[k] = proj;
                    min_s = std::min(min_s, proj);
                    if (proj > max_s) { max_s = proj; peak_k = k; }
                    if (k < fields.geometry.baseline.size()) {
                        const auto ev = heraw_suite::detail::epipolar_vec(b, fields.geometry, k, static_cast<float>(yr), static_cast<float>(xc));
                        ty[k] = 0.5f * rho * ev[0] / f_h;
                        tx[k] = 0.5f * rho * ev[1] / f_h;
                    }
                }
                const float exc = max_s - min_s;
                const float nsd = std::sqrt(heraw_suite::detail::variance(b, 0.5f * (max_s + min_s)));
                if (exc < 6.0f * nsd) continue;
                double s_xx = 0.0, s_xy = 0.0, s_x = 0.0, s_y = 0.0;
                int cnt_k = 0;
                for (size_t k = 0; k < n; ++k) {
                    if (ms_list[k] < 0.0f) continue;
                    const float mk = ms_list[k] - min_s;
                    if (mk <= 2.5f * nsd) continue;
                    const double dth2 = static_cast<double>(ty[k] - ty[peak_k]) * (ty[k] - ty[peak_k]) +
                                        static_cast<double>(tx[k] - tx[peak_k]) * (tx[k] - tx[peak_k]);
                    const double lny = std::log(static_cast<double>(mk) / static_cast<double>(exc));
                    s_x += dth2; s_y += lny; s_xx += dth2 * dth2; s_xy += dth2 * lny; ++cnt_k;
                }
                if (cnt_k >= 4) {
                    const double den = cnt_k * s_xx - s_x * s_x;
                    if (den > 1e-14) {
                        const double slope = (cnt_k * s_xy - s_x * s_y) / den; // = -1 / (2 * alpha^2)
                        if (slope < -1.0) {
                            const float alpha = static_cast<float>(std::max(0.002, std::min(0.25, std::sqrt(-0.5 / slope))));
                            rough[ln.hi] = alpha;
                            alpha_vals.push_back(alpha);
                        }
                    }
                }
            }
        }
        Gate rg;
        rg.units = "half_angle_rad";
        rg.threshold = sweep_thr;
        rg.measured = alpha_vals.empty() ? max_sweep : static_cast<double>(alpha_vals.size());
        rg.observable = (alpha_vals.size() >= (opts.gate_mode == ObserverGateMode::Auto ? 4u : 1u)) &&
                        (opts.gate_mode == ObserverGateMode::Force || max_sweep >= sweep_thr);
        rg.forced = (opts.gate_mode == ObserverGateMode::Force && max_sweep < 0.0020 && rg.observable);
        if (rg.observable) {
            rep.median_roughness_rad = heraw_suite::detail::quantile(alpha_vals, 0.5f);
            rg.measured = rep.median_roughness_rad;
            rg.confidence = std::min(1.0, static_cast<double>(alpha_vals.size()) / 32.0);
            rg.reason = "distance-free microfacet angular lobe width regressed from parallax half-angles";
        } else {
            rg.reason = "requires >=0.002 rad viewpoint sweep over specular highlights and calibrated depth";
        }
        rep.maps["roughness"] = std::move(rough);
        rep.gates["roughness"] = rg;
        rep.roughness_gate = rg;
    }
}

// P^3 helper + R11 + F-H + Sec 7.4 kernels
struct ChirpGlrtResult {
    bool chirp_active = false;
    bool clock_ambiguous = false;
    double best_f0_hz = 0.0;
    double best_fdot_hz_s = 0.0;
    double rss0 = 0.0;
    double rss1 = 0.0;
    double delta_glrt = 0.0;
    double threshold_chi2 = 6.63;
    double boundary_phase_rad = 0.0;
    double coherent_amp_ratio_m0 = 1.0;
    double equiv_clock_quad_drift_a = 0.0;
};

namespace detail {

inline bool solve_3x3_pivoted(double A[3][3], double rhs[3], double out[3]) noexcept {
    double M[3][4] = {};
    double max_norm = 0.0;
    for (int r = 0; r < 3; ++r) {
        double rsum = 0.0;
        for (int c = 0; c < 3; ++c) {
            M[r][c] = A[r][c];
            rsum += std::fabs(A[r][c]);
        }
        max_norm = std::max(max_norm, rsum);
        M[r][3] = rhs[r];
    }
    if (max_norm <= 1e-14) return false;
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r) {
            if (std::fabs(M[r][col]) > std::fabs(M[piv][col])) piv = r;
        }
        if (piv != col) {
            for (int k = col; k < 4; ++k) std::swap(M[col][k], M[piv][k]);
        }
        if (std::fabs(M[col][col]) <= 1e-10 * max_norm) return false;
        const double inv_p = 1.0 / M[col][col];
        for (int k = col; k < 4; ++k) M[col][k] *= inv_p;
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            const double f = M[r][col];
            for (int k = col; k < 4; ++k) M[r][k] -= f * M[col][k];
        }
    }
    for (int r = 0; r < 3; ++r) out[r] = M[r][3];
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

inline void integrated_chirp_basis(double t_rel, double t_exp_s, double f0_hz, double fdot_hz_s,
                                   double& out_cos, double& out_sin) noexcept {
    const double nonlin = std::numbers::pi * std::fabs(fdot_hz_s) * t_exp_s * t_exp_s * 0.25;
    if (t_exp_s <= 1e-7 || nonlin < 0.05) {
        const double phi = 2.0 * std::numbers::pi * (f0_hz * t_rel + 0.5 * fdot_hz_s * t_rel * t_rel);
        const double arg = std::numbers::pi * (f0_hz + fdot_hz_s * t_rel) * t_exp_s;
        const double sc = (std::fabs(arg) < 1e-6) ? 1.0 : (std::sin(arg) / arg);
        out_cos = sc * std::cos(phi);
        out_sin = sc * std::sin(phi);
        return;
    }
    // 5-point Gauss-Legendre quadrature across [-Texp/2, +Texp/2]
    static constexpr double gl_x[5] = {-0.90617985, -0.53846931, 0.0, 0.53846931, 0.90617985};
    static constexpr double gl_w[5] = {0.23692689, 0.47862867, 0.56888889, 0.47862867, 0.23692689};
    double c = 0.0, s = 0.0;
    for (int q = 0; q < 5; ++q) {
        const double tq = t_rel + 0.5 * t_exp_s * gl_x[q];
        const double phi = 2.0 * std::numbers::pi * (f0_hz * tq + 0.5 * fdot_hz_s * tq * tq);
        c += 0.5 * gl_w[q] * std::cos(phi);
        s += 0.5 * gl_w[q] * std::sin(phi);
    }
    out_cos = c;
    out_sin = s;
}

} // namespace detail

// R11: Nested constant-frequency (M0) vs linear-chirp (M1) weighted GLRT.
inline ChirpGlrtResult evaluate_chirp_glrt_1d(
    std::span<const double> y,
    std::span<const double> t_s,
    double t_exp_s,
    std::span<const double> noise_var,
    double f0_init_hz,
    double fdot_min_hz_s,
    double fdot_max_hz_s,
    int n_fdot_grid = 25,
    int multiplicity_trials = 1,
    const heraw_meas::ClockProvenance* clock_prov = nullptr) {
    ChirpGlrtResult res;
    const size_t n = std::min({y.size(), t_s.size(), noise_var.size()});
    res.threshold_chi2 = 6.63 + 2.0 * std::log(static_cast<double>(std::max(1, multiplicity_trials)));
    res.clock_ambiguous = !(clock_prov &&
        (clock_prov->source == heraw_meas::ClockSource::PtsTicks ||
         clock_prov->source == heraw_meas::ClockSource::HwExposure ||
         clock_prov->source == heraw_meas::ClockSource::MakerNoteDxoTs ||
         clock_prov->source == heraw_meas::ClockSource::ExifSubsecMs) &&
        clock_prov->row_calibrated);
    if (n < 5) return res;

    double tmin = t_s[0], tmax = t_s[0];
    for (size_t k = 1; k < n; ++k) {
        tmin = std::min(tmin, t_s[k]);
        tmax = std::max(tmax, t_s[k]);
    }
    const double tmid = 0.5 * (tmin + tmax);
    const double span = std::max(1e-3, tmax - tmin);
    const double f0_lo = std::max(0.1, f0_init_hz - 1.5 / span);
    const double f0_hi = f0_init_hz + 1.5 / span;
    constexpr int NF0 = 25;

    auto fit_rss = [&](double f0, double fdot, double& out_rss, double& out_amp) {
        double G[3][3] = {}, rhs[3] = {};
        double yy = 0.0;
        for (size_t k = 0; k < n; ++k) {
            const double w = 1.0 / std::max(1e-18, noise_var[k]);
            double cb = 0.0, sb = 0.0;
            detail::integrated_chirp_basis(t_s[k] - tmid, t_exp_s, f0, fdot, cb, sb);
            const double b[3] = {1.0, cb, sb};
            const double yk = y[k];
            yy += w * yk * yk;
            for (int r = 0; r < 3; ++r) {
                rhs[r] += w * b[r] * yk;
                for (int c = 0; c < 3; ++c) G[r][c] += w * b[r] * b[c];
            }
        }
        double coef[3] = {};
        if (!detail::solve_3x3_pivoted(G, rhs, coef)) {
            out_rss = yy; out_amp = 0.0; return;
        }
        double explained = 0.0;
        for (int r = 0; r < 3; ++r) explained += coef[r] * rhs[r];
        out_rss = std::max(0.0, yy - explained);
        out_amp = std::hypot(coef[1], coef[2]);
    };

    double best_rss0 = std::numeric_limits<double>::infinity();
    double best_f0_m0 = f0_init_hz, best_amp_m0 = 0.0;
    for (int i = 0; i < NF0; ++i) {
        const double f0 = f0_lo + (f0_hi - f0_lo) * (static_cast<double>(i) / (NF0 - 1));
        double rss = 0.0, amp = 0.0;
        fit_rss(f0, 0.0, rss, amp);
        if (rss < best_rss0) { best_rss0 = rss; best_f0_m0 = f0; best_amp_m0 = amp; }
    }

    double best_rss1 = best_rss0;
    double best_f0_m1 = best_f0_m0, best_fdot_m1 = 0.0, best_amp_m1 = best_amp_m0;
    const int nfd = std::max(3, n_fdot_grid);
    for (int j = 0; j < nfd; ++j) {
        const double fd = fdot_min_hz_s + (fdot_max_hz_s - fdot_min_hz_s) * (static_cast<double>(j) / (nfd - 1));
        for (int i = 0; i < NF0; ++i) {
            const double f0 = f0_lo + (f0_hi - f0_lo) * (static_cast<double>(i) / (NF0 - 1));
            double rss = 0.0, amp = 0.0;
            fit_rss(f0, fd, rss, amp);
            if (rss < best_rss1) { best_rss1 = rss; best_f0_m1 = f0; best_fdot_m1 = fd; best_amp_m1 = amp; }
        }
    }

    res.best_f0_hz = best_f0_m1;
    res.best_fdot_hz_s = best_fdot_m1;
    res.rss0 = best_rss0;
    res.rss1 = best_rss1;
    res.delta_glrt = std::max(0.0, best_rss0 - best_rss1);
    res.chirp_active = (res.delta_glrt >= res.threshold_chi2);
    res.boundary_phase_rad = std::numbers::pi * std::fabs(best_fdot_m1) * (0.5 * span) * (0.5 * span);
    res.coherent_amp_ratio_m0 = (best_amp_m1 > 1e-12) ? std::min(1.0, best_amp_m0 / best_amp_m1) : 1.0;
    res.equiv_clock_quad_drift_a = (res.clock_ambiguous && res.best_f0_hz > 1e-6)
        ? (res.best_fdot_hz_s / (2.0 * res.best_f0_hz)) : 0.0;
    return res;
}

inline ChirpGlrtResult evaluate_chirp_glrt_complex(
    std::span<const double> y_re, std::span<const double> y_im,
    std::span<const double> t_s, double f0_init_hz,
    double fdot_min_hz_s, double fdot_max_hz_s,
    double noise_var_per_sample = 1e-4, int n_fdot_grid = 25,
    int multiplicity_trials = 1,
    const heraw_meas::ClockProvenance* clock_prov = nullptr) {
    ChirpGlrtResult res;
    const size_t n = std::min({y_re.size(), y_im.size(), t_s.size()});
    res.threshold_chi2 = 6.63 + 2.0 * std::log(static_cast<double>(std::max(1, multiplicity_trials)));
    res.clock_ambiguous = !(clock_prov &&
        (clock_prov->source == heraw_meas::ClockSource::PtsTicks ||
         clock_prov->source == heraw_meas::ClockSource::HwExposure ||
         clock_prov->source == heraw_meas::ClockSource::MakerNoteDxoTs) &&
        clock_prov->row_calibrated);
    if (n < 5) return res;
    double tmin = t_s[0], tmax = t_s[0];
    for (size_t k = 1; k < n; ++k) { tmin = std::min(tmin, t_s[k]); tmax = std::max(tmax, t_s[k]); }
    const double tmid = 0.5 * (tmin + tmax), span = std::max(1e-3, tmax - tmin);
    const double nv = std::max(1e-12, noise_var_per_sample);
    double norm_y = 0.0;
    for (size_t k = 0; k < n; ++k) norm_y += y_re[k] * y_re[k] + y_im[k] * y_im[k];
    if (norm_y <= 1e-18) return res;

    auto eval_proj = [&](double f0, double fdot) -> double {
        double ar = 0.0, ai = 0.0;
        for (size_t k = 0; k < n; ++k) {
            const double tr = t_s[k] - tmid;
            const double phi = 2.0 * std::numbers::pi * (f0 * tr + 0.5 * fdot * tr * tr);
            const double cs = std::cos(phi), sn = std::sin(phi);
            ar += y_re[k] * cs + y_im[k] * sn;
            ai += y_im[k] * cs - y_re[k] * sn;
        }
        return std::hypot(ar, ai) / static_cast<double>(n);
    };

    const double coh0 = eval_proj(f0_init_hz, 0.0);
    double best_coh1 = coh0, best_fd = 0.0;
    const int nfd = std::max(3, n_fdot_grid);
    for (int j = 0; j < nfd; ++j) {
        const double fd = fdot_min_hz_s + (fdot_max_hz_s - fdot_min_hz_s) * (static_cast<double>(j) / (nfd - 1));
        const double c = eval_proj(f0_init_hz, fd);
        if (c > best_coh1) { best_coh1 = c; best_fd = fd; }
    }
    const double mean_amp = std::sqrt(norm_y / static_cast<double>(n));
    res.best_f0_hz = f0_init_hz;
    res.best_fdot_hz_s = best_fd;
    res.coherent_amp_ratio_m0 = coh0 / std::max(1e-12, mean_amp);
    res.boundary_phase_rad = std::numbers::pi * std::fabs(best_fd) * (0.5 * span) * (0.5 * span);
    res.rss0 = std::max(0.0, norm_y - static_cast<double>(n) * coh0 * coh0) / nv;
    res.rss1 = std::max(0.0, norm_y - static_cast<double>(n) * best_coh1 * best_coh1) / nv;
    res.delta_glrt = std::max(0.0, res.rss0 - res.rss1);
    res.chirp_active = (res.delta_glrt >= res.threshold_chi2);
    res.equiv_clock_quad_drift_a = (res.clock_ambiguous && f0_init_hz > 1e-6)
        ? (best_fd / (2.0 * f0_init_hz)) : 0.0;
    return res;
}

// F-H: Global Shutter dual-window exposure sinc ratio frequency estimator:
// Solves |sinc(f * T2) / sinc(f * T1)| = r_obs across candidate AC frequencies.
struct GsDualWindowResult {
    bool observable = false;
    double freq_hz = 0.0;
    double fit_residual = 0.0;
};

inline GsDualWindowResult estimate_gs_flicker_sinc_ratio(
    double amp1, double exp1_s, double amp2, double exp2_s,
    double f_min_hz = 20.0, double f_max_hz = 1000.0) {
    GsDualWindowResult res;
    if (!(amp1 > 1e-9) || !(amp2 >= 0.0) || !(exp1_s > 1e-6) || !(exp2_s > 1e-6) ||
        std::fabs(exp1_s - exp2_s) < 1e-6) {
        return res;
    }
    const double r_obs = amp2 / amp1;
    auto sinc_abs = [](double u) noexcept {
        const double x = std::numbers::pi * u;
        return (std::fabs(x) < 1e-7) ? 1.0 : std::fabs(std::sin(x) / x);
    };
    double best_err = 1e18, best_f = 0.0;
    for (double f = f_min_hz; f <= f_max_hz; f += 0.25) {
        const double s1 = sinc_abs(f * exp1_s);
        if (s1 < 0.08) continue;
        const double s2 = sinc_abs(f * exp2_s);
        const double err = std::fabs((s2 / s1) - r_obs);
        if (err < best_err) { best_err = err; best_f = f; }
    }
    if (best_f > 0.0 && best_err < 0.05) {
        double lo = std::max(f_min_hz, best_f - 0.5), hi = std::min(f_max_hz, best_f + 0.5);
        for (int it = 0; it < 30; ++it) {
            const double m1 = lo + (hi - lo) / 3.0;
            const double m2 = hi - (hi - lo) / 3.0;
            auto eval_f = [&](double f) {
                const double s1 = sinc_abs(f * exp1_s);
                return (s1 < 0.05) ? 1e6 : std::fabs((sinc_abs(f * exp2_s) / s1) - r_obs);
            };
            if (eval_f(m1) < eval_f(m2)) hi = m2; else lo = m1;
        }
        best_f = 0.5 * (lo + hi);
        const double s1 = sinc_abs(best_f * exp1_s);
        best_err = (s1 > 0.05) ? std::fabs((sinc_abs(best_f * exp2_s) / s1) - r_obs) : 1.0;
        res.freq_hz = best_f;
        res.fit_residual = best_err;
        res.observable = (best_err < 0.02);
    }
    return res;
}

// Section 7.4: 3-hypothesis astro point classifier (Star / HotPixel / CosmicRay).
enum class AstroPointClass : uint8_t { Star = 0, HotPixel = 1, CosmicRay = 2 };

inline AstroPointClass classify_astro_point_3hyp(
    std::span<const float> world_vals,
    std::span<const float> sensor_vals,
    float bg_med, float bg_mad,
    float patch_center_frac) noexcept {
    const size_t n = std::min(world_vals.size(), sensor_vals.size());
    const float sigma = std::max(1e-5f, bg_mad);
    const float thr = bg_med + 4.5f * sigma;
    int act_world = 0, act_sensor = 0;
    for (size_t k = 0; k < n; ++k) {
        if (world_vals[k] > thr) ++act_world;
        if (sensor_vals[k] > thr) ++act_sensor;
    }
    const int min_multi = std::max(2, static_cast<int>(n / 2));
    if (act_sensor >= min_multi && act_world <= 1) return AstroPointClass::HotPixel;
    if (act_world <= 1 && act_sensor <= 1) return AstroPointClass::CosmicRay;
    if (patch_center_frac >= 0.88f && act_world <= 1) return AstroPointClass::CosmicRay;
    if (patch_center_frac >= 0.85f && act_sensor >= min_multi) return AstroPointClass::HotPixel;
    return AstroPointClass::Star;
}

// O2: Optical Lock-In Light-Transport Separation & Phosphor Decay Spectroscopy
inline void run_optical_lockin(const BurstView& b, const FieldResult& fields,
                               const ObserverOptions& opts, ObserverReport& rep,
                               const ParallelRows& parallel_rows) {
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hp = static_cast<size_t>(Hh) * Wh, n = b.frames_lin.size();
    const bool need_any = opts.compute_ac_light || opts.compute_phosphor || opts.compute_light_split;
    if (opts.compute_phosphor) {
        rep.phosphor_gate.units = "ms";
        rep.phosphor_gate.threshold = 0.06;
        rep.phosphor_gate.reason = "requires locked optical AC carrier (lockin_gate)";
    }
    if (!need_any || n < 4) {
        rep.lockin_gate.units = "Hz";
        rep.lockin_gate.threshold = 6.0;
        rep.lockin_gate.reason = "Optical lock-in requires >=4 frames";
        if (opts.compute_phosphor) rep.gates["phosphor_decay"] = rep.phosphor_gate;
        return;
    }

    bool known_cal = false;
    float t_read_ms = (opts.readout_ms_override >= 0.0f)
        ? opts.readout_ms_override
        : detail::lookup_sensor_readout_ms(opts.camera_make, opts.camera_model, b.H1, opts.shutter_kind, &known_cal);
    rep.readout_ms_used = t_read_ms;

    std::vector<double> ts(n, 0.0);
    bool has_real_t = false;
    for (size_t k = 0; k < n; ++k) ts[k] = detail::frame_time_sec(b, k, has_real_t);

    // 1. Compute per-frame sensor-row mean relative deviations (prefer Tap-0 PreDestripeTapSet when available: E11)
    std::vector<double> row_dev(n * static_cast<size_t>(Hh), 0.0);
    std::vector<double> row_var_noise(n * static_cast<size_t>(Hh), 1e-8);
    double total_sig_var = 0.0, total_nse_var = 0.0;
    size_t row_obs_cnt = 0;

    const bool use_tap0 = opts.pre_destripe_tap && opts.pre_destripe_tap->valid &&
                          opts.pre_destripe_tap->frames.size() == n &&
                          opts.pre_destripe_tap->H1 == b.H1;
    if (use_tap0) {
        const auto& tap = *opts.pre_destripe_tap;
        for (int r = 0; r < Hh; ++r) {
            const int r0 = std::min(tap.H1 - 1, 2 * r);
            const int r1 = std::min(tap.H1 - 1, 2 * r + 1);
            std::vector<double> rvals;
            rvals.reserve(n);
            for (size_t k = 0; k < n; ++k) {
                const auto& tf = tap.frames[k];
                const uint32_t vc = tf.row_green_valid[r0] + tf.row_green_valid[r1];
                if (vc >= 8u) {
                    const double s = tf.row_green_mean[r0] * tf.row_green_valid[r0] +
                                     tf.row_green_mean[r1] * tf.row_green_valid[r1];
                    rvals.push_back(s / static_cast<double>(vc));
                }
            }
            if (rvals.size() < 4) continue;
            std::sort(rvals.begin(), rvals.end());
            const double rmed = rvals[rvals.size() / 2];
            if (rmed < 0.015 || rmed > 0.90) continue;
            for (size_t k = 0; k < n; ++k) {
                const auto& tf = tap.frames[k];
                const uint32_t vc = tf.row_green_valid[r0] + tf.row_green_valid[r1];
                if (vc >= 8u) {
                    const double mk = (tf.row_green_mean[r0] * tf.row_green_valid[r0] +
                                       tf.row_green_mean[r1] * tf.row_green_valid[r1]) / static_cast<double>(vc);
                    const double mu = mk - rmed;
                    const double nv = heraw_suite::detail::variance(b, static_cast<float>(rmed)) / static_cast<double>(vc);
                    row_dev[k * static_cast<size_t>(Hh) + r] = mu;
                    row_var_noise[k * static_cast<size_t>(Hh) + r] = nv;
                    total_sig_var += mu * mu;
                    total_nse_var += nv;
                    ++row_obs_cnt;
                }
            }
        }
    } else {
        std::vector<float> ref_mean(hp, 0.0f);
        const RowTask build_mean = [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y) for (int x = 0; x < Wh; ++x) {
                const size_t i = static_cast<size_t>(y) * Wh + x;
                double s = 0.0, sw = 0.0;
                for (size_t k = 0; k < n; ++k) {
                    if (b.mw_half[k][i] < 0.4f) continue;
                    const float qy = y + b.flows_fy[k][i], qx = x + b.flows_fx[k][i];
                    if (qy < 0.0f || qx < 0.0f || qy > static_cast<float>(Hh - 1) || qx > static_cast<float>(Wh - 1)) continue;
                    const float g = heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, qy, qx);
                    if (std::isfinite(g) && g < 0.94f) { s += b.mw_half[k][i] * g; sw += b.mw_half[k][i]; }
                }
                ref_mean[i] = (sw > 1e-4) ? static_cast<float>(s / sw) : b.g_halfs[0][i];
            }
        };
        if (parallel_rows) parallel_rows(Hh, build_mean); else build_mean(0, Hh);

        const int col_step = std::max(1, Wh / 96);
        for (size_t k = 0; k < n; ++k) {
            for (int r = 0; r < Hh; ++r) {
                double ds = 0.0, vs = 0.0;
                int cnt = 0;
                for (int c = 1; c + 1 < Wh; c += col_step) {
                    const size_t hi = static_cast<size_t>(r) * Wh + c;
                    if (b.mw_half[k][hi] < 0.6f) continue;
                    const float rm = ref_mean[hi];
                    if (rm < 0.02f || rm > 0.88f) continue;
                    const float dy = b.flows_fy[k][hi], dx = b.flows_fx[k][hi];
                    if (std::fabs(dy) > 12.0f) continue;
                    const float g = heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, r + dy, c + dx);
                    if (!std::isfinite(g) || g > 0.90f) continue;
                    ds += static_cast<double>(g - rm);
                    vs += heraw_suite::detail::variance(b, rm);
                    ++cnt;
                }
                if (cnt >= 8) {
                    const double mu = ds / cnt;
                    const double nv = (vs / cnt) / cnt;
                    row_dev[k * static_cast<size_t>(Hh) + r] = mu;
                    row_var_noise[k * static_cast<size_t>(Hh) + r] = nv;
                    total_sig_var += mu * mu;
                    total_nse_var += nv;
                    ++row_obs_cnt;
                }
            }
        }
    }

    // 2. Scan candidate modulation frequencies (rank-2 profile likelihood across frames over rolling row axis)
    double best_freq = 0.0, best_snr = 0.0;
    std::vector<double> best_c(n, 1.0), best_s(n, 0.0);
    std::vector<double> null_powers;

    const bool use_rolling = (t_read_ms > 0.25f && Hh >= 16);
    if (use_rolling && row_obs_cnt >= n * 8) {
        const double t_read_s = static_cast<double>(t_read_ms) * 1e-3;
        std::vector<double> freqs;
        for (double f = 75.0; f <= 135.0; f += 1.0) freqs.push_back(f);
        for (double f = 140.0; f <= 1800.0; f += 10.0) freqs.push_back(f);

        double best_score = 0.0, best_noise_snr = 0.0, best_fstat = 0.0;
        for (double f : freqs) {
            if (b.exposure_seconds > 0.0f) {
                const double x = std::numbers::pi * f * static_cast<double>(b.exposure_seconds);
                const double sinc_e = (x < 1e-4) ? 1.0 : std::fabs(std::sin(x) / x);
                if (sinc_e < 0.14) continue;
            }
            const double omega_r = 2.0 * std::numbers::pi * f * t_read_s / std::max(1, Hh);
            if (omega_r * Hh < 0.35) continue;

            double C2 = 0.0, S2 = 0.0, CS = 0.0;
            for (int r = 0; r < Hh; ++r) {
                const double ph = omega_r * r;
                const double c = std::cos(ph), s = std::sin(ph);
                C2 += c * c; S2 += s * s; CS += c * s;
            }
            const double det = C2 * S2 - CS * CS;
            if (det < 1e-6) continue;
            const double inv_det = 1.0 / det;

            double explained = 0.0, residual = 0.0, n_tot = 0.0;
            std::vector<double> ck(n, 0.0), sk(n, 0.0);
            for (size_t k = 0; k < n; ++k) {
                double yc = 0.0, ys = 0.0, yy = 0.0, nsum = 0.0;
                for (int r = 0; r < Hh; ++r) {
                    const double y = row_dev[k * static_cast<size_t>(Hh) + r];
                    const double ph = omega_r * r;
                    yc += y * std::cos(ph);
                    ys += y * std::sin(ph);
                    yy += y * y;
                    nsum += row_var_noise[k * static_cast<size_t>(Hh) + r];
                }
                const double a = (S2 * yc - CS * ys) * inv_det;
                const double b_s = (-CS * yc + C2 * ys) * inv_det;
                ck[k] = a;
                sk[k] = b_s;
                const double e_k = std::max(0.0, a * yc + b_s * ys);
                explained += e_k;
                residual += std::max(0.0, yy - e_k);
                n_tot += nsum;
            }
            const double noise_snr = 0.5 * explained / std::max(1e-12, n_tot);
            const double dof_res = std::max(1.0, static_cast<double>(n) * std::max(1, Hh - 2));
            const double dof_mod = std::max(1.0, 2.0 * static_cast<double>(n));
            const double f_stat = (explained / dof_mod) / std::max(1e-12, residual / dof_res);
            const double score = explained;
            if (score > best_score) {
                best_score = score;
                best_freq = f;
                best_noise_snr = noise_snr;
                best_fstat = f_stat;
                best_c = ck;
                best_s = sk;
            }
        }
        best_snr = std::min(best_noise_snr, 1.5 * best_fstat);
        double amp_sq = 0.0;
        for (size_t k = 0; k < n; ++k) amp_sq += best_c[k] * best_c[k] + best_s[k] * best_s[k];
        const double amp_rms = std::sqrt(amp_sq / std::max<size_t>(1, n));
        if (amp_rms > 1e-8) {
            for (size_t k = 0; k < n; ++k) {
                best_c[k] /= amp_rms;
                best_s[k] /= amp_rms;
            }
        }
        if (best_snr >= 6.0 && opts.readout_ms_override < 0.0f) {
            const double cal_tol = known_cal ? 0.04 : 0.15;
            if (best_freq >= 100.0 * (1.0 - cal_tol) && best_freq <= std::min(109.5, 100.0 * (1.0 + cal_tol))) {
                rep.readout_ms_used = static_cast<float>(t_read_ms * (best_freq / 100.0));
                rep.readout_self_calibrated = true;
                best_freq = 100.0;
            } else if (best_freq > std::max(109.5, 120.0 * (1.0 - cal_tol)) &&
                       best_freq <= 120.0 * (1.0 + cal_tol)) {
                rep.readout_ms_used = static_cast<float>(t_read_ms * (best_freq / 120.0));
                rep.readout_self_calibrated = true;
                best_freq = 120.0;
            }
        }
    } else if (has_real_t && n >= 6) {
        // Global-shutter (t_read == 0) temporal Lomb-Scargle frequency scan over timestamps
        double t_span = 0.0, min_dt = 0.0;
        detail::sorted_time_stats(ts, t_span, min_dt);
        t_span = std::max(1e-3, t_span);
        const double f_max = (min_dt > 1e-4 && min_dt < 1.0) ? (0.48 / min_dt) : 60.0;
        const double df = 0.25 / t_span;
        std::vector<double> m_frame(n, 0.0), n_frame(n, 0.0);
        double mean_m = 0.0;
        for (size_t k = 0; k < n; ++k) {
            double m = 0.0, nv = 0.0;
            for (int r = 0; r < Hh; ++r) {
                m += row_dev[k * static_cast<size_t>(Hh) + r];
                nv += row_var_noise[k * static_cast<size_t>(Hh) + r];
            }
            m_frame[k] = m / std::max(1, Hh);
            n_frame[k] = nv / (static_cast<double>(std::max(1, Hh)) * static_cast<double>(std::max(1, Hh)));
            mean_m += m_frame[k];
        }
        mean_m /= static_cast<double>(n);
        double best_pow = 0.0;
        for (double f = std::max(1.0, df); f <= std::min(500.0, f_max); f += df) {
            const double om = 2.0 * std::numbers::pi * f;
            double re = 0.0, im = 0.0, nvar = 0.0;
            for (size_t k = 0; k < n; ++k) {
                const double mc = m_frame[k] - mean_m;
                re += mc * std::cos(om * ts[k]);
                im -= mc * std::sin(om * ts[k]);
                nvar += n_frame[k];
            }
            const double pw = (re * re + im * im) / std::max(1e-12, nvar * 0.5);
            null_powers.push_back(pw);
            if (pw > best_pow) {
                best_pow = pw; best_freq = f;
                for (size_t k = 0; k < n; ++k) {
                    const double ph = om * ts[k];
                    best_c[k] = std::cos(ph); best_s[k] = std::sin(ph);
                }
            }
        }
        if (null_powers.size() >= 6) {
            std::vector<double> sp = null_powers;
            std::sort(sp.begin(), sp.end());
            best_snr = best_pow / std::max(1e-6, sp[sp.size() / 2]);
        }
    }

    const double rms_sig = std::sqrt(total_sig_var / std::max<size_t>(1, row_obs_cnt));
    const double rms_nse = std::sqrt(total_nse_var / std::max<size_t>(1, row_obs_cnt));
    const double snr_thr = (opts.gate_mode == ObserverGateMode::Auto) ? 6.0 :
                           ((opts.gate_mode == ObserverGateMode::Relaxed) ? 3.5 : 0.0);
    const double ratio_thr = (opts.gate_mode == ObserverGateMode::Auto) ? 3.0 :
                             ((opts.gate_mode == ObserverGateMode::Relaxed) ? 1.8 : 0.0);
    const bool phys_lockin = (best_freq > 1.0 && best_snr >= 6.0 && rms_sig >= 3.0 * rms_nse);
    const bool lockin_ok = (opts.gate_mode == ObserverGateMode::Force && best_freq > 0.0) ||
                           (best_freq > 1.0 && best_snr >= snr_thr && rms_sig >= ratio_thr * rms_nse);

    Gate lg;
    lg.observable = lockin_ok;
    lg.measured = best_freq;
    lg.threshold = snr_thr;
    lg.units = "Hz";
    lg.forced = (opts.gate_mode == ObserverGateMode::Force && !phys_lockin && lockin_ok);
    lg.confidence = lockin_ok ? std::min(1.0, best_snr / 24.0) : 0.0;
    lg.reason = lockin_ok ? "coherent AC/PWM row-phase optical lock-in carrier detected" :
        "no coherent temporal light-source ripple above shot/read floor";
    rep.lockin_gate = lg;
    rep.ac_freq_hz = static_cast<float>(best_freq);

    std::vector<float> ac_frac(hp, 0.0f);
    std::vector<float> lag_ms_map(hp, 0.0f);
    if (opts.compute_light_split) {
        rep.light_dc_half = {Hh, Wh, std::vector<float>(3 * hp, 0.0f)};
        rep.light_ac_half = {Hh, Wh, std::vector<float>(3 * hp, 0.0f)};
    }

    if (lockin_ok) {
        const double t_read_eff_s = static_cast<double>(rep.readout_ms_used) * 1e-3;
        const double omega_r = 2.0 * std::numbers::pi * best_freq * t_read_eff_s / std::max(1, Hh);
        const double omega_hz = 2.0 * std::numbers::pi * best_freq;
        std::vector<std::array<float, 3>> z_re(hp), z_im(hp), i_mean(hp);
        std::vector<float> amp_ratio_samples;
        amp_ratio_samples.reserve(hp / 16 + 16);

        const RowTask lock_rows = [&](int y0, int y1) {
            std::vector<double> cbasis(n), sbasis(n);
            for (int y = y0; y < y1; ++y) {
                for (size_t k = 0; k < n; ++k) {
                    if (use_rolling) {
                        const double ph = omega_r * y;
                        cbasis[k] = best_c[k] * std::cos(ph) + best_s[k] * std::sin(ph);
                        sbasis[k] = -best_s[k] * std::cos(ph) + best_c[k] * std::sin(ph);
                    } else {
                        cbasis[k] = best_c[k];
                        sbasis[k] = best_s[k];
                    }
                }
                // E08: Solve PER-PIXEL 3x3 design matrix [1, cbasis, sbasis] strictly over valid visible unsaturated frames
                for (int x = 0; x < Wh; ++x) {
                    const size_t hi = static_cast<size_t>(y) * Wh + x;
                    double Gp[3][3] = {};
                    double rhs[3][3] = {};
                    int ok_c = 0;
                    for (size_t k = 0; k < n; ++k) {
                        if (b.mw_half[k][hi] < 0.4f) continue;
                        float sy1, sx1; bool ok = false;
                        detail::aligned_source_coords(b, fields, k, y, x, sy1, sx1, ok);
                        if (!ok) continue;
                        const auto rgb = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                        if (!std::isfinite(rgb[1]) || rgb[1] >= 0.96f) continue;
                        const double bf[3] = {1.0, cbasis[k], sbasis[k]};
                        for (int r = 0; r < 3; ++r)
                            for (int c = 0; c < 3; ++c) Gp[r][c] += bf[r] * bf[c];
                        for (int ch = 0; ch < 3; ++ch)
                            for (int p = 0; p < 3; ++p) rhs[ch][p] += bf[p] * static_cast<double>(rgb[ch]);
                        ++ok_c;
                    }
                    if (ok_c < 4) continue;
                    for (int ch = 0; ch < 3; ++ch) {
                        double coef[3] = {};
                        if (!detail::solve_3x3_pivoted(Gp, rhs[ch], coef)) break;
                        i_mean[hi][ch] = static_cast<float>(std::max(0.0, coef[0]));
                        z_re[hi][ch] = static_cast<float>(coef[1]);
                        z_im[hi][ch] = static_cast<float>(coef[2]);
                    }
                }
            }
        };
        if (parallel_rows) parallel_rows(Hh, lock_rows); else lock_rows(0, Hh);

        // R11: Test nested constant-frequency vs linear chirp (both 1D frame-mean and complex row-phasor series)
        if (has_real_t && n >= 6) {
            std::vector<double> m_f(n, 0.0), v_f(n, 1e-8), c_re(n, 0.0), c_im(n, 0.0);
            const double tr_s = 1e-3 * static_cast<double>(t_read_ms) / static_cast<double>(std::max(1, Hh));
            double c_nv = 0.0;
            for (size_t k = 0; k < n; ++k) {
                double s = 0.0, nv = 0.0, re = 0.0, im = 0.0;
                for (int r = 0; r < Hh; ++r) {
                    const double val = row_dev[k * static_cast<size_t>(Hh) + r];
                    const double nvr = row_var_noise[k * static_cast<size_t>(Hh) + r];
                    const double ph = 2.0 * std::numbers::pi * best_freq * static_cast<double>(r) * tr_s;
                    s += val;
                    nv += nvr;
                    re += val * std::cos(ph);
                    im -= val * std::sin(ph);
                }
                const double inv_h = 1.0 / static_cast<double>(std::max(1, Hh));
                m_f[k] = s * inv_h;
                v_f[k] = nv * inv_h * inv_h;
                c_re[k] = re * inv_h;
                c_im[k] = im * inv_h;
                c_nv += v_f[k];
            }
            const auto cg1 = evaluate_chirp_glrt_1d(m_f, ts, static_cast<double>(b.exposure_seconds), v_f,
                                                    best_freq, -5.0, 5.0, 21, 1, &opts.clock_prov);
            const auto cgc = evaluate_chirp_glrt_complex(c_re, c_im, ts, best_freq, -5.0, 5.0,
                                                         std::max(1e-10, c_nv / static_cast<double>(n)),
                                                         21, 1, &opts.clock_prov);
            const auto& cg = (cgc.delta_glrt > cg1.delta_glrt) ? cgc : cg1;
            rep.chirp_glrt_delta = static_cast<float>(cg.delta_glrt);
            rep.chirp_f_dot_hz_s = static_cast<float>(cg.best_fdot_hz_s);
            rep.chirp_active = cg.chirp_active;
            rep.clock_ambiguous = cg.clock_ambiguous;
            if (opts.shutter_kind == 2 && opts.frame_exposures_s.size() == n) {
                double t1 = opts.frame_exposures_s[0], t2 = t1;
                for (size_t k = 1; k < n; ++k) {
                    if (std::fabs(opts.frame_exposures_s[k] - t1) > 1e-4) {
                        t2 = opts.frame_exposures_s[k];
                        break;
                    }
                }
                if (std::fabs(t2 - t1) > 1e-4) {
                    double s1 = 0.0, s2 = 0.0;
                    int n1 = 0, n2 = 0;
                    for (size_t k = 0; k < n; ++k) {
                        if (std::fabs(opts.frame_exposures_s[k] - t1) <= 1e-4) { s1 += std::fabs(m_f[k]); ++n1; }
                        else if (std::fabs(opts.frame_exposures_s[k] - t2) <= 1e-4) { s2 += std::fabs(m_f[k]); ++n2; }
                    }
                    if (n1 > 0 && n2 > 0) {
                        const auto fh = estimate_gs_flicker_sinc_ratio(s1 / n1, t1, s2 / n2, t2);
                        if (fh.observable) rep.gs_dual_window_hz = static_cast<float>(fh.freq_hz);
                    }
                }
            }
        } else {
            rep.clock_ambiguous = !opts.clock_prov.row_calibrated;
        }

        // Estimate scalar modulation depth eta_eff from top 98th-percentile active regions
        for (size_t i = 0; i < hp; i += 4) {
            const float mg = i_mean[i][1];
            const float amp = std::hypot(z_re[i][1], z_im[i][1]);
            const float nsd = std::sqrt(heraw_suite::detail::variance(b, mg) / std::max<size_t>(1, n));
            if (mg > 0.03f && amp > 4.0f * nsd) amp_ratio_samples.push_back(amp / mg);
        }
        const float mod_depth = (!amp_ratio_samples.empty())
            ? std::max(0.04f, std::min(1.0f, heraw_suite::detail::quantile(amp_ratio_samples, 0.98f)))
            : 1.0f;
        rep.ac_mod_depth = mod_depth;

        double frac_sum = 0.0;
        std::vector<float> lag_samples;
        lag_samples.reserve(hp / 16 + 16);
        for (size_t i = 0; i < hp; ++i) {
            const float mg = std::max(1e-5f, i_mean[i][1]);
            const float amp_g = std::hypot(z_re[i][1], z_im[i][1]);
            const float nsd = std::sqrt(heraw_suite::detail::variance(b, mg) / std::max<size_t>(1, n));
            const float ac_g = (amp_g > 2.5f * nsd) ? std::min(mg, amp_g / mod_depth) : 0.0f;
            ac_frac[i] = heraw_suite::detail::clamp01(ac_g / mg);
            frac_sum += ac_frac[i];
            if (opts.compute_light_split) {
                for (int c = 0; c < 3; ++c) {
                    const float ampc = std::hypot(z_re[i][c], z_im[i][c]);
                    const float ac_c = (amp_g > 2.5f * nsd) ? std::min(std::max(0.0f, i_mean[i][c]), ampc / mod_depth) : 0.0f;
                    rep.light_ac_half.rgb[3 * i + c] = ac_c;
                    rep.light_dc_half.rgb[3 * i + c] = std::max(0.0f, i_mean[i][c] - ac_c);
                }
            }
            if (opts.compute_phosphor) {
                const float ab = std::hypot(z_re[i][2], z_im[i][2]);
                const float ar = std::hypot(z_re[i][0], z_im[i][0]);
                const float ns_r = std::sqrt(heraw_suite::detail::variance(b, std::max(0.0f, i_mean[i][0])) / n);
                const float ns_b = std::sqrt(heraw_suite::detail::variance(b, std::max(0.0f, i_mean[i][2])) / n);
                if (ab > 5.0f * ns_b && ar > 5.0f * ns_r) {
                    // Causal phosphor RC delay: Z_B = A_B*e^{-j*dB}, Z_R = A_R*e^{-j*(dB - lag)}
                    // => Z_B * conj(Z_R) = A_B*A_R*e^{-j*lag}, so -cross_im > 0 marks R lagging B.
                    const double cross_re = z_re[i][2]*z_re[i][0] + z_im[i][2]*z_im[i][0];
                    const double cross_im = z_im[i][2]*z_re[i][0] - z_re[i][2]*z_im[i][0];
                    const double lag_rad = std::atan2(-cross_im, cross_re);
                    if (lag_rad >= 0.06 && lag_rad <= 1.48) {
                        const float tau_ms = static_cast<float>(std::tan(lag_rad) / omega_hz * 1000.0);
                        lag_ms_map[i] = tau_ms;
                        if ((i & 3u) == 0) lag_samples.push_back(tau_ms);
                    }
                }
            }
        }
        rep.ac_light_fraction_mean = static_cast<float>(frac_sum / hp);

        if (opts.compute_phosphor) {
            Gate pg;
            pg.units = "ms";
            pg.threshold = 0.06;
            const size_t min_lag_cnt = (opts.gate_mode == ObserverGateMode::Auto) ? 8u :
                                       ((opts.gate_mode == ObserverGateMode::Relaxed) ? 3u : 1u);
            pg.observable = (lag_samples.size() >= min_lag_cnt);
            pg.forced = (opts.gate_mode == ObserverGateMode::Force && lag_samples.size() < 8u && pg.observable);
            if (pg.observable) {
                rep.phosphor_decay_ms = heraw_suite::detail::quantile(lag_samples, 0.5f);
                rep.red_blue_phase_lag_deg = static_cast<float>(std::atan(rep.phosphor_decay_ms * 1e-3 * omega_hz) * (180.0 / std::numbers::pi));
                pg.measured = rep.phosphor_decay_ms;
                pg.confidence = std::min(1.0, static_cast<double>(lag_samples.size()) / 64.0);
                pg.reason = "inter-channel lock-in phase lag tau_R = tan(dphi)/omega calibrated";
            } else {
                pg.reason = "insufficient modulated RGB emitters with resolvable R-B decay lag";
            }
            rep.phosphor_gate = pg;
        }
    } else if (opts.compute_light_split) {
        for (size_t i = 0; i < hp; ++i) {
            const auto rgb = heraw_suite::detail::raw_rgb(b, 0, 2.0f * (i / Wh) + 0.5f, 2.0f * (i % Wh) + 0.5f);
            for (int c = 0; c < 3; ++c) rep.light_dc_half.rgb[3 * i + c] = std::max(0.0f, rgb[c]);
        }
    }
    if (opts.compute_ac_light) {
        rep.maps["ac_light_fraction"] = std::move(ac_frac);
        rep.gates["ac_light_fraction"] = rep.lockin_gate;
    }
    if (opts.compute_phosphor) {
        rep.maps["phosphor_decay"] = std::move(lag_ms_map);
        rep.gates["phosphor_decay"] = rep.phosphor_gate;
    }
}

// O3: Moving Rigid Body SE(3) Self-Stereo, Continuum Kinematics (TTC, Vorticity, Strain),
// Exposure-Duty Optical Tachometry & Ballistic g0 Metric Distance Calibration
inline void run_kinematics_and_self_stereo(const BurstView& b, const FieldResult& fields,
                                           const ObserverOptions& opts, ObserverReport& rep,
                                           const ParallelRows& parallel_rows) {
    if (!opts.compute_kinematics) return;
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hp = static_cast<size_t>(Hh) * Wh, n = b.frames_lin.size();
    std::vector<float> ttc_map(hp, 0.0f), vort_map(hp, 0.0f), strain_map(hp, 0.0f);
    if (n < 3) {
        rep.kinematics_gate.reason = "Kinematics requires >=3 aligned frames";
        rep.maps["ttc"] = std::move(ttc_map);
        rep.maps["vorticity"] = std::move(vort_map);
        rep.maps["nonrigid_strain"] = std::move(strain_map);
        return;
    }

    const auto cam = fit_camera(b);
    bool has_real_t = false;
    std::vector<double> ts(n, 0.0);
    for (size_t k = 0; k < n; ++k) ts[k] = detail::frame_time_sec(b, k, has_real_t);
    double t_span_raw = 0.0, min_dt_t = 0.0;
    detail::sorted_time_stats(ts, t_span_raw, min_dt_t);
    const double t_span = std::max(1e-4, t_span_raw);
    const float rlow = fields.geometry.rho_low;
    const float rspan = fields.geometry.rho_high - fields.geometry.rho_low;
    const auto* depth_map = fields.maps.count("depth") ? &fields.maps.at("depth") : nullptr;
    const bool have_epi = fields.geometry.gate.observable && depth_map && depth_map->size() == hp;

    // 1. Extract camera- and parallax-subtracted residual linear velocity (vy, vx) [1x px/s] + accel
    std::vector<float> vx_map(hp, 0.0f), vy_map(hp, 0.0f);
    std::vector<uint8_t> active_mot(hp, 0);
    std::vector<double> t_mean_per_px;
    double tm_all = 0.0, t_v = 0.0;
    for (size_t k = 0; k < n; ++k) tm_all += ts[k];
    tm_all /= static_cast<double>(n);
    for (size_t k = 0; k < n; ++k) t_v += (ts[k] - tm_all) * (ts[k] - tm_all);

    const float vel_thr = has_real_t ? std::max(1.5f, 1.2f / static_cast<float>(t_span)) : 0.25f;
    const RowTask comp_vel = [&](int y0, int y1) {
        std::vector<double> resy(n), resx(n);
        for (int y = y0; y < y1; ++y) for (int x = 0; x < Wh; ++x) {
            const size_t i = static_cast<size_t>(y) * Wh + x;
            const float rho = have_epi ? (rlow + (*depth_map)[i] * rspan) : 0.0f;
            double sy = 0.0, sx = 0.0;
            for (size_t k = 0; k < n; ++k) {
                const auto s = cam[k].source(static_cast<float>(y), static_cast<float>(x));
                double dy = (static_cast<double>(y) + b.flows_fy[k][i]) - s[0];
                double dx = (static_cast<double>(x) + b.flows_fx[k][i]) - s[1];
                if (have_epi && k < fields.geometry.baseline.size()) {
                    const auto ev = heraw_suite::detail::epipolar_vec(b, fields.geometry, k, static_cast<float>(y), static_cast<float>(x));
                    dy -= rho * ev[0]; dx -= rho * ev[1];
                }
                resy[k] = 2.0 * dy; resx[k] = 2.0 * dx;
                sy += (ts[k] - tm_all) * resy[k];
                sx += (ts[k] - tm_all) * resx[k];
            }
            const float vy = (t_v > 1e-10) ? static_cast<float>(sy / t_v) : 0.0f;
            const float vx = (t_v > 1e-10) ? static_cast<float>(sx / t_v) : 0.0f;
            vy_map[i] = vy; vx_map[i] = vx;
            const float spd = std::hypot(vx, vy);
            if (spd >= vel_thr) {
                active_mot[i] = 1;
            }
        }
    };
    if (parallel_rows) parallel_rows(Hh, comp_vel); else comp_vel(0, Hh);

    // 2. Local affine Jacobian fit on active moving regions -> TTC, Vorticity & Traceless Non-Rigid Shear
    std::vector<float> strain_vals;
    strain_vals.reserve(4096);
    float min_ttc = 1e9f, max_vort = 0.0f;
    size_t kin_cnt = 0;
    const int rad_k = 4;
    for (int y = rad_k; y < Hh - rad_k; ++y) {
        for (int x = rad_k; x < Wh - rad_k; ++x) {
            const size_t i = static_cast<size_t>(y) * Wh + x;
            if (!active_mot[i]) continue;
            // Least-squares fit v(dx, dy) = v0 + J * [dx; dy] with coordinates in 1x px (= 2 * half_px)
            double A[3][3] = {}, ry[3] = {}, rx[3] = {};
            int cnt = 0;
            for (int dy = -rad_k; dy <= rad_k; ++dy) for (int dx = -rad_k; dx <= rad_k; ++dx) {
                const size_t q = static_cast<size_t>(y + dy) * Wh + (x + dx);
                if (!active_mot[q]) continue;
                const double u = 2.0 * dx, v = 2.0 * dy;
                const double bvec[3] = {1.0, u, v};
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) A[r][c] += bvec[r] * bvec[c];
                    rx[r] += bvec[r] * vx_map[q];
                    ry[r] += bvec[r] * vy_map[q];
                }
                ++cnt;
            }
            if (cnt < 18) continue;
            double m_aug_x[3][4] = {
                {A[0][0], A[0][1], A[0][2], rx[0]},
                {A[1][0], A[1][1], A[1][2], rx[1]},
                {A[2][0], A[2][1], A[2][2], rx[2]}
            };
            double m_aug_y[3][4] = {
                {A[0][0], A[0][1], A[0][2], ry[0]},
                {A[1][0], A[1][1], A[1][2], ry[1]},
                {A[2][0], A[2][1], A[2][2], ry[2]}
            };
            std::array<double, 3> sx_sol{}, sy_sol{};
            if (!heraw_suite::detail::solve3(m_aug_x, sx_sol) || !heraw_suite::detail::solve3(m_aug_y, sy_sol)) continue;
            const double dvx_dx = sx_sol[1], dvx_dy = sx_sol[2];
            const double dvy_dx = sy_sol[1], dvy_dy = sy_sol[2];
            const double div = dvx_dx + dvy_dy;
            const double vort = 0.5 * (dvy_dx - dvx_dy);
            const double s_xx = dvx_dx - 0.5 * div, s_xy = 0.5 * (dvx_dy + dvy_dx), s_yy = dvy_dy - 0.5 * div;
            const float shear_rate = static_cast<float>(std::sqrt(s_xx * s_xx + 2.0 * s_xy * s_xy + s_yy * s_yy));
            if (std::fabs(div) >= 0.04) {
                const float ttc_val = std::max(-120.0f, std::min(120.0f, static_cast<float>(2.0 / div)));
                ttc_map[i] = ttc_val;
                if (div >= 0.04 && ttc_val > 0.0f && ttc_val < min_ttc) min_ttc = ttc_val;
            }
            vort_map[i] = static_cast<float>(vort);
            max_vort = std::max(max_vort, static_cast<float>(std::fabs(vort)));
            strain_map[i] = shear_rate * static_cast<float>(t_span) * static_cast<float>(2 * rad_k);
            if ((i & 3u) == 0) strain_vals.push_back(strain_map[i]);
            ++kin_cnt;
        }
    }

    // 3. Connected Component Extraction -> Moving Rigid Body SE(3) Self-Stereo & Ballistic g0 Distance
    std::vector<int> comp_id(hp, -1);
    std::vector<std::vector<size_t>> comps;
    for (size_t i = 0; i < hp; ++i) {
        if (!active_mot[i] || comp_id[i] >= 0) continue;
        const int cid = static_cast<int>(comps.size());
        std::vector<size_t> queue{i};
        comp_id[i] = cid;
        for (size_t head = 0; head < queue.size(); ++head) {
            const size_t p = queue[head];
            const int py = static_cast<int>(p / static_cast<size_t>(Wh)), px = static_cast<int>(p % static_cast<size_t>(Wh));
            const int oy[4] = {-1, 1, 0, 0}, ox[4] = {0, 0, -1, 1};
            for (int d = 0; d < 4; ++d) {
                const int ny = py + oy[d], nx = px + ox[d];
                if (ny < 0 || ny >= Hh || nx < 0 || nx >= Wh) continue;
                const size_t q = static_cast<size_t>(ny) * Wh + nx;
                if (active_mot[q] && comp_id[q] < 0) { comp_id[q] = cid; queue.push_back(q); }
            }
        }
        if (queue.size() >= 196) comps.push_back(std::move(queue));
    }
    rep.moving_body_count = static_cast<int>(comps.size());

    if (!comps.empty()) {
        // Pick largest active moving component for SE(3) self-stereo + ballistic fit
        size_t best_ci = 0;
        for (size_t ci = 1; ci < comps.size(); ++ci)
            if (comps[ci].size() > comps[best_ci].size()) best_ci = ci;
        const auto& cpts = comps[best_ci];

        // 3a. Object-centric SE(3) affine base + SVD rank-1 parallax relief inside moving component
        std::vector<double> obj_res_y(cpts.size() * n, 0.0), obj_res_x(cpts.size() * n, 0.0);
        const double scale_o = std::max(1.0, 0.5 * std::max(Hh, Wh));
        double cy_o = 0.0, cx_o = 0.0;
        for (size_t p : cpts) { cy_o += p / static_cast<size_t>(Wh); cx_o += p % static_cast<size_t>(Wh); }
        cy_o /= cpts.size(); cx_o /= cpts.size();

        for (size_t k = 1; k < n; ++k) {
            double Ay[3][3] = {}, ry[3] = {}, rx[3] = {};
            for (size_t p : cpts) {
                const double py = (p / static_cast<size_t>(Wh) - cy_o) / scale_o;
                const double px = (p % static_cast<size_t>(Wh) - cx_o) / scale_o;
                const double b3[3] = {1.0, px, py};
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) Ay[r][c] += b3[r] * b3[c];
                    rx[r] += b3[r] * b.flows_fx[k][p];
                    ry[r] += b3[r] * b.flows_fy[k][p];
                }
            }
            double mx[3][4] = {{Ay[0][0],Ay[0][1],Ay[0][2],rx[0]},{Ay[1][0],Ay[1][1],Ay[1][2],rx[1]},{Ay[2][0],Ay[2][1],Ay[2][2],rx[2]}};
            double my[3][4] = {{Ay[0][0],Ay[0][1],Ay[0][2],ry[0]},{Ay[1][0],Ay[1][1],Ay[1][2],ry[1]},{Ay[2][0],Ay[2][1],Ay[2][2],ry[2]}};
            std::array<double, 3> sx{}, sy{};
            if (!heraw_suite::detail::solve3(mx, sx) || !heraw_suite::detail::solve3(my, sy)) continue;
            for (size_t j = 0; j < cpts.size(); ++j) {
                const size_t p = cpts[j];
                const double py = (p / static_cast<size_t>(Wh) - cy_o) / scale_o;
                const double px = (p % static_cast<size_t>(Wh) - cx_o) / scale_o;
                obj_res_x[j * n + k] = b.flows_fx[k][p] - (sx[0] + sx[1] * px + sx[2] * py);
                obj_res_y[j * n + k] = b.flows_fy[k][p] - (sy[0] + sy[1] * px + sy[2] * py);
            }
        }
        // Check SVD rank-1 epipolar coherence across (2*(n-1)) residual matrix on component pixels
        const int M_k = 2 * static_cast<int>(n - 1);
        std::vector<double> G_obj(static_cast<size_t>(M_k) * M_k, 0.0);
        const size_t step_j = std::max<size_t>(1, cpts.size() / 512);
        for (size_t j = 0; j < cpts.size(); j += step_j) {
            for (size_t k1 = 1; k1 < n; ++k1) {
                const double r1y = obj_res_y[j * n + k1], r1x = obj_res_x[j * n + k1];
                for (size_t k2 = k1; k2 < n; ++k2) {
                    const double r2y = obj_res_y[j * n + k2], r2x = obj_res_x[j * n + k2];
                    const int i1y = 2 * static_cast<int>(k1 - 1), i1x = i1y + 1;
                    const int i2y = 2 * static_cast<int>(k2 - 1), i2x = i2y + 1;
                    G_obj[static_cast<size_t>(i1y) * M_k + i2y] += r1y * r2y;
                    G_obj[static_cast<size_t>(i1y) * M_k + i2x] += r1y * r2x;
                    G_obj[static_cast<size_t>(i1x) * M_k + i2y] += r1x * r2y;
                    G_obj[static_cast<size_t>(i1x) * M_k + i2x] += r1x * r2x;
                }
            }
        }
        for (int a = 0; a < M_k; ++a) for (int c_i = 0; c_i < a; ++c_i) G_obj[static_cast<size_t>(a) * M_k + c_i] = G_obj[static_cast<size_t>(c_i) * M_k + a];
        // Power iteration for dominant eigenvector of object parallax baseline
        std::vector<double> b_vec(static_cast<size_t>(M_k), 1.0 / std::sqrt(static_cast<double>(M_k))), b_nx(static_cast<size_t>(M_k), 0.0);
        double tr_obj = 0.0;
        for (int a = 0; a < M_k; ++a) tr_obj += G_obj[static_cast<size_t>(a) * M_k + a];
        double lam1 = 0.0;
        for (int it = 0; it < 20; ++it) {
            std::fill(b_nx.begin(), b_nx.end(), 0.0);
            for (int r = 0; r < M_k; ++r)
                for (int c_i = 0; c_i < M_k; ++c_i) b_nx[r] += G_obj[static_cast<size_t>(r) * M_k + c_i] * b_vec[c_i];
            double nm = 0.0;
            for (double v : b_nx) nm += v * v;
            nm = std::sqrt(std::max(1e-24, nm));
            lam1 = nm;
            for (int r = 0; r < M_k; ++r) b_vec[r] = b_nx[r] / nm;
        }
        const double coh_obj = (tr_obj > 1e-8) ? (lam1 / tr_obj) : 0.0;
        std::vector<float> rho_obj(cpts.size(), 0.0f);
        for (size_t j = 0; j < cpts.size(); ++j) {
            double s = 0.0;
            for (size_t k = 1; k < n; ++k)
                s += obj_res_y[j * n + k] * b_vec[2 * (k - 1)] + obj_res_x[j * n + k] * b_vec[2 * (k - 1) + 1];
            rho_obj[j] = static_cast<float>(2.0 * s); // 1x px
        }
        const int best_cid = static_cast<int>(best_ci);
        std::vector<float> rho_sorted = rho_obj;
        float r_lo = heraw_suite::detail::quantile(rho_sorted, 0.05f);
        float r_hi = heraw_suite::detail::quantile(rho_sorted, 0.95f);
        if (coh_obj >= 0.68 && (r_hi - r_lo) >= 1.5f) {
            // Disambiguate eigenvector +-1 sign via boundary kinetic occlusion asymmetry inside cpts.
            std::vector<int32_t> pt_lut(hp, -1);
            for (size_t j = 0; j < cpts.size(); ++j) pt_lut[cpts[j]] = static_cast<int32_t>(j);
            double sign_vote = 0.0;
            const size_t step_b = std::max<size_t>(1u, cpts.size() / 512u);
            const float r_mid = 0.5f * (r_lo + r_hi);
            for (size_t j = 0; j < cpts.size(); j += step_b) {
                const size_t p = cpts[j];
                const int py = static_cast<int>(p / static_cast<size_t>(Wh)), px = static_cast<int>(p % static_cast<size_t>(Wh));
                if (py < 3 || py + 3 >= Hh || px < 3 || px + 3 >= Wh) continue;
                const float dr = rho_obj[j] - r_mid;
                if (std::fabs(dr) < 0.25f * (r_hi - r_lo)) continue;
                const float g0 = b.g_halfs[0][p];
                const float gx = 0.5f * (b.g_halfs[0][p + 1] - b.g_halfs[0][p - 1]);
                const float gy = 0.5f * (b.g_halfs[0][p + Wh] - b.g_halfs[0][p - Wh]);
                const float gm = std::hypot(gx, gy);
                const float nsd = std::sqrt(heraw_suite::detail::variance(b, g0));
                if (gm < 4.0f * nsd) continue;
                const int ny = std::max(1, std::min(Hh - 2, static_cast<int>(std::lround(py + 3.0f * (gy / gm)))));
                const int nx = std::max(1, std::min(Wh - 2, static_cast<int>(std::lround(px + 3.0f * (gx / gm)))));
                const int my = std::max(1, std::min(Hh - 2, static_cast<int>(std::lround(py - 3.0f * (gy / gm)))));
                const int mx = std::max(1, std::min(Wh - 2, static_cast<int>(std::lround(px - 3.0f * (gx / gm)))));
                const int32_t jn = pt_lut[static_cast<size_t>(ny) * Wh + nx];
                const int32_t jm = pt_lut[static_cast<size_t>(my) * Wh + mx];
                if (jn < 0 || jm < 0) continue;
                const float rn = rho_obj[static_cast<size_t>(jn)], rm = rho_obj[static_cast<size_t>(jm)];
                if (std::fabs(rn - rm) < 0.25f * (r_hi - r_lo)) continue;
                const float vn = b.g_halfs[0][static_cast<size_t>(ny) * Wh + nx];
                const float vm = b.g_halfs[0][static_cast<size_t>(my) * Wh + mx];
                const float con = std::fabs(vn - vm);
                if (con < 4.5f * nsd) continue;
                int vis_n = 0, vis_m = 0;
                const float thr = std::max(3.5f * nsd, 0.25f * con);
                for (size_t k = 1; k < n; ++k) {
                    const size_t pn = cpts[static_cast<size_t>(jn)], pm = cpts[static_cast<size_t>(jm)];
                    const float syn = static_cast<float>(ny) + b.flows_fy[k][pn], sxn = static_cast<float>(nx) + b.flows_fx[k][pn];
                    const float sym = static_cast<float>(my) + b.flows_fy[k][pm], sxm = static_cast<float>(mx) + b.flows_fx[k][pm];
                    if (heraw_suite::detail::inside(syn, sxn, Hh, Wh) &&
                        std::fabs(heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, syn, sxn) - vn) <= thr) ++vis_n;
                    if (heraw_suite::detail::inside(sym, sxm, Hh, Wh) &&
                        std::fabs(heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, sym, sxm) - vm) <= thr) ++vis_m;
                }
                if (vis_n != vis_m) {
                    sign_vote += (vis_n > vis_m) ? ((rn > rm) ? 1.0 : -1.0) : ((rm > rn) ? 1.0 : -1.0);
                }
            }
            if (sign_vote < -1.5) {
                for (float& r : rho_obj) r = -r;
                const float tmp = r_lo; r_lo = -r_hi; r_hi = -tmp;
            }
            rep.rigid_self_stereo_observable = true;
            rep.moving_body_depth_half.assign(hp, 0.0f);
            rep.moving_body_conf_half.assign(hp, 0.0f);
            const float inv_sp = 1.0f / std::max(1e-5f, r_hi - r_lo);
            for (size_t j = 0; j < cpts.size(); ++j) {
                rep.moving_body_depth_half[cpts[j]] = 0.15f + 0.85f * heraw_suite::detail::clamp01((rho_obj[j] - r_lo) * inv_sp);
                rep.moving_body_conf_half[cpts[j]] = static_cast<float>(coh_obj);
            }
        }

        // 3b. Ballistic g0 = 9.80665 m/s^2 Metric Distance Calibration from free-fall parabolic centroid trace
        const float f_px = 2.0f * heraw_suite::detail::half_focal_px(b);
        if (has_real_t && n >= 4 && f_px > 0.0f) {
            const auto gdir = detail::gravity_dir(opts.cam_flip);
            const std::array<double, 2> tdir{{-gdir[1], gdir[0]}};
            std::vector<double> yc(n, 0.0), xc_p(n, 0.0), tc(n, 0.0);
            for (size_t k = 0; k < n; ++k) {
                double sy_k = 0.0, sx_k = 0.0;
                for (size_t p : cpts) {
                    const int py = static_cast<int>(p / static_cast<size_t>(Wh));
                    const int px = static_cast<int>(p % static_cast<size_t>(Wh));
                    const auto s = cam[k].source(static_cast<float>(py), static_cast<float>(px));
                    const double dy = 2.0 * (static_cast<double>(py) + b.flows_fy[k][p] - s[0]);
                    const double dx = 2.0 * (static_cast<double>(px) + b.flows_fx[k][p] - s[1]);
                    sy_k += gdir[0] * dy + gdir[1] * dx;
                    sx_k += tdir[0] * dy + tdir[1] * dx;
                }
                yc[k] = sy_k / cpts.size();
                xc_p[k] = sx_k / cpts.size();
                tc[k] = ts[k] - ts[0];
            }
            double A[3][3] = {}, rhs[3] = {}, rhs_x[3] = {};
            for (size_t k = 0; k < n; ++k) {
                const double t = tc[k], t2 = 0.5 * t * t;
                const double r3[3] = {1.0, t, t2};
                for (int r = 0; r < 3; ++r) {
                    for (int c_i = 0; c_i < 3; ++c_i) A[r][c_i] += r3[r] * r3[c_i];
                    rhs[r] += r3[r] * yc[k];
                    rhs_x[r] += r3[r] * xc_p[k];
                }
            }
            double M3[3][4] = {{A[0][0],A[0][1],A[0][2],rhs[0]},{A[1][0],A[1][1],A[1][2],rhs[1]},{A[2][0],A[2][1],A[2][2],rhs[2]}};
            double MX3[3][4] = {{A[0][0],A[0][1],A[0][2],rhs_x[0]},{A[1][0],A[1][1],A[1][2],rhs_x[1]},{A[2][0],A[2][1],A[2][2],rhs_x[2]}};
            std::array<double, 3> psol{}, pxsol{};
            if (heraw_suite::detail::solve3(M3, psol) && heraw_suite::detail::solve3(MX3, pxsol)) {
                const double ay_px_s2 = psol[2];
                const double ax_px_s2 = pxsol[2];
                double y_mean = std::accumulate(yc.begin(), yc.end(), 0.0) / n;
                double ss_tot = 0.0, ss_res = 0.0;
                for (size_t k = 0; k < n; ++k) {
                    const double pred = psol[0] + psol[1] * tc[k] + 0.5 * ay_px_s2 * tc[k] * tc[k];
                    ss_tot += (yc[k] - y_mean) * (yc[k] - y_mean);
                    ss_res += (yc[k] - pred) * (yc[k] - pred);
                }
                const double r2 = (ss_tot > 1e-6) ? (1.0 - ss_res / ss_tot) : 0.0;
                const double linear_only_res = [&]() {
                    double sy_t = 0.0, st2 = 0.0, mt = std::accumulate(tc.begin(), tc.end(), 0.0) / n;
                    for (size_t k = 0; k < n; ++k) { sy_t += (tc[k] - mt) * (yc[k] - y_mean); st2 += (tc[k] - mt) * (tc[k] - mt); }
                    const double sl = (st2 > 1e-12) ? (sy_t / st2) : 0.0;
                    double er = 0.0;
                    for (size_t k = 0; k < n; ++k) {
                        const double d = yc[k] - (y_mean + sl * (tc[k] - mt));
                        er += d * d;
                    }
                    return er;
                }();
                if (ay_px_s2 >= 40.0 && std::fabs(ax_px_s2) <= 0.25 * ay_px_s2 &&
                    r2 >= 0.92 && ss_res < 0.25 * linear_only_res) {
                    const double cos_p = std::max(0.1, std::cos(static_cast<double>(opts.pitch_deg) * (std::numbers::pi / 180.0)));
                    rep.ballistic_accel_1x_px_s2 = static_cast<float>(ay_px_s2);
                    rep.ballistic_distance_m = static_cast<float>(f_px * 9.80665 * cos_p / ay_px_s2);
                    rep.ballistic_gate.observable = true;
                    rep.ballistic_gate.measured = rep.ballistic_distance_m;
                    rep.ballistic_gate.threshold = 0.92;
                    rep.ballistic_gate.confidence = r2;
                    rep.ballistic_gate.units = "m";
                    rep.ballistic_gate.reason = "free-fall ballistic trajectory curvature calibrated metric distance";
                }
            }
        }

        // 4. Optical Tachometry (SubSecTime-free dt estimate) + Velocity-Diversity Multi-Frame Condition Number Check
        if (b.exposure_seconds > 0.0f) {
            std::vector<double> speeds;
            speeds.reserve(cpts.size());
            double v_min = 1e9, v_max = 0.0;
            for (size_t p : cpts) {
                const double s = std::hypot(vx_map[p], vy_map[p]);
                speeds.push_back(s);
                v_min = std::min(v_min, s);
                v_max = std::max(v_max, s);
            }
            if (!speeds.empty()) {
                std::nth_element(speeds.begin(), speeds.begin() + speeds.size() / 2, speeds.end());
                const float v0 = static_cast<float>(speeds[speeds.size() / 2]);
                double mvx = 0.0, mvy = 0.0;
                for (size_t p : cpts) { mvx += vx_map[p]; mvy += vy_map[p]; }
                const double vm = std::hypot(mvx, mvy);
                if (vm > 1e-6 && cpts.size() >= 196) {
                    const double ux = mvx / vm, uy = mvy / vm;
                    double e_par = 0.0, e_perp = 0.0;
                    for (size_t j = 0; j < cpts.size(); j += 2) {
                        const size_t idx = cpts[j];
                        const int y = static_cast<int>(idx / static_cast<size_t>(Wh));
                        const int x = static_cast<int>(idx % static_cast<size_t>(Wh));
                        if (y <= 1 || y + 1 >= Hh || x <= 1 || x + 1 >= Wh) continue;
                        if (comp_id[idx] != best_cid) continue;
                        const float gx = 0.5f * (b.g_halfs[0][idx + 1] - b.g_halfs[0][idx - 1]);
                        const float gy = 0.5f * (b.g_halfs[0][idx + Wh] - b.g_halfs[0][idx - Wh]);
                        const double gp = gx * ux + gy * uy;
                        const double gt = -gx * uy + gy * ux;
                        e_par += gp * gp; e_perp += gt * gt;
                    }
                    if (e_perp > 1.35 * std::max(1e-12, e_par)) {
                        const float ratio = static_cast<float>(std::sqrt(e_perp / std::max(1e-12, e_par)));
                        const float est_blur_1x = std::max(2.0f, std::min(40.0f, 1.8f * ratio));
                        if (!has_real_t && v0 > 2.0f && (v0 * b.exposure_seconds) < 8.0f) {
                            rep.estimated_frame_dt_s = b.exposure_seconds * (v0 / est_blur_1x);
                            rep.tachometry_gate.observable = true;
                            rep.tachometry_gate.measured = rep.estimated_frame_dt_s;
                            rep.tachometry_gate.units = "s_per_frame";
                            rep.tachometry_gate.reason = "exposure-duty tachometry recovered dt from directional blur ratio";
                        }
                    }
                }
                rep.velocity_diversity_deblur_ready = (n >= 4) && ((v_max - v_min) >= std::max(2.0, 0.25 * v0));
            }
        }
    }

    rep.kinematics_gate.observable = (kin_cnt >= 16);
    rep.kinematics_gate.measured = static_cast<double>(kin_cnt);
    rep.kinematics_gate.threshold = 16.0;
    rep.kinematics_gate.confidence = std::min(1.0, static_cast<double>(kin_cnt) / 512.0);
    rep.kinematics_gate.units = "active_pixels";
    rep.kinematics_gate.reason = rep.kinematics_gate.observable
        ? "affine velocity Jacobian (TTC, vorticity, strain) calibrated"
        : "insufficient coherent moving-subject support for velocity tensor";
    if (min_ttc < 1e8f) rep.min_ttc_s = min_ttc;
    rep.max_abs_vorticity_rad_s = max_vort;
    if (!strain_vals.empty()) rep.median_nonrigid_strain_px = heraw_suite::detail::quantile(strain_vals, 0.5f);

    rep.maps["ttc"] = std::move(ttc_map);
    rep.maps["vorticity"] = std::move(vort_map);
    rep.maps["nonrigid_strain"] = std::move(strain_map);
    rep.gates["ttc"] = rep.kinematics_gate;
    rep.gates["ttc"].units = has_real_t ? "seconds" : "frames";
    rep.gates["vorticity"] = rep.kinematics_gate;
    rep.gates["vorticity"].units = has_real_t ? "rad_per_s" : "rad_per_frame";
    rep.gates["nonrigid_strain"] = rep.kinematics_gate;
    rep.gates["nonrigid_strain"].units = "1x_px";
}

// O4: Helmholtz-Gated Quantitative Background-Oriented Schlieren (Any Focal Length / Macro Compatible)
inline void run_helmholtz_schlieren(const BurstView& b, const FieldResult& fields,
                                    const ObserverOptions& opts, ObserverReport& rep) {
    if (!opts.compute_schlieren) return;
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hp = static_cast<size_t>(Hh) * Wh, n = b.frames_lin.size();
    std::vector<float> opd_map(hp, 0.0f);
    Gate sg;
    sg.threshold = 0.72;
    sg.units = (b.focus_distance_m > 0.05f && !b.focus_is_infinity && b.focal_length_mm > 0.5f) ? "um" : "px*rad";
    rep.schlieren_units = sg.units;
    if (n < 3 || Hh < 16 || Wh < 16) {
        sg.reason = "Schlieren requires >=3 frames and >=16x16 half-grid dimensions";
        rep.schlieren_gate = sg;
        rep.maps["schlieren_opd"] = std::move(opd_map);
        rep.gates["schlieren_opd"] = sg;
        return;
    }

    const auto cam = fit_camera(b);
    const float rlow = fields.geometry.rho_low;
    const float rspan = fields.geometry.rho_high - fields.geometry.rho_low;
    const auto* depth_map = fields.maps.count("depth") ? &fields.maps.at("depth") : nullptr;
    const bool have_epi = fields.geometry.gate.observable && depth_map && depth_map->size() == hp;
    bool has_real_t = false;
    std::vector<double> ts(n, 0.0);
    for (size_t k = 0; k < n; ++k) ts[k] = detail::frame_time_sec(b, k, has_real_t);

    // Exact power-of-two grid so PH = 2 * gh, PW = 2 * gw with zero FFT moat.
    const int gh = (Hh >= 128) ? 128 : ((Hh >= 64) ? 64 : ((Hh >= 32) ? 32 : 16));
    const int gw = (Wh >= 256) ? 256 : ((Wh >= 128) ? 128 : ((Wh >= 64) ? 64 : ((Wh >= 32) ? 32 : 16)));
    const int PH = 2 * gh, PW = 2 * gw;
    const double step_y = static_cast<double>(Hh) / static_cast<double>(gh);
    const double step_x = static_cast<double>(Wh) / static_cast<double>(gw);
    const double dy_1x = 2.0 * step_y, dx_1x = 2.0 * step_x;

    std::vector<float> res_y(n * static_cast<size_t>(gh) * gw, 0.0f), res_x(n * static_cast<size_t>(gh) * gw, 0.0f);
    std::vector<uint8_t> g_valid(static_cast<size_t>(gh) * gw, 0);
    for (int gy = 0; gy < gh; ++gy) {
        const int y = std::max(0, std::min(Hh - 1, static_cast<int>(std::floor((gy + 0.5) * step_y))));
        for (int gx = 0; gx < gw; ++gx) {
            const int x = std::max(0, std::min(Wh - 1, static_cast<int>(std::floor((gx + 0.5) * step_x))));
            const size_t i = static_cast<size_t>(y) * Wh + x;
            const size_t gi = static_cast<size_t>(gy) * gw + gx;
            const float nsd = std::sqrt(heraw_suite::detail::variance(b, b.g_halfs[0][i]));
            const float gmag = heraw_suite::detail::gradient_mag(b.g_halfs[0], Hh, Wh, static_cast<float>(y), static_cast<float>(x));
            if (gmag < 2.2f * nsd) continue;
            g_valid[gi] = 1;
            const float rho = have_epi ? (rlow + (*depth_map)[i] * rspan) : 0.0f;
            double mt = 0.0, my = 0.0, mx = 0.0;
            std::vector<double> rky(n), rkx(n);
            for (size_t k = 0; k < n; ++k) {
                const auto s = cam[k].source(static_cast<float>(y), static_cast<float>(x));
                double dy = (static_cast<double>(y) + b.flows_fy[k][i]) - s[0];
                double dx = (static_cast<double>(x) + b.flows_fx[k][i]) - s[1];
                if (have_epi && k < fields.geometry.baseline.size()) {
                    const auto ev = heraw_suite::detail::epipolar_vec(b, fields.geometry, k, static_cast<float>(y), static_cast<float>(x));
                    dy -= rho * ev[0]; dx -= rho * ev[1];
                }
                rky[k] = 2.0 * dy; rkx[k] = 2.0 * dx;
                mt += ts[k]; my += rky[k]; mx += rkx[k];
            }
            mt /= n; my /= n; mx /= n;
            double sy = 0.0, sx = 0.0, tv = 0.0;
            for (size_t k = 0; k < n; ++k) {
                sy += (ts[k] - mt) * (rky[k] - my);
                sx += (ts[k] - mt) * (rkx[k] - mx);
                tv += (ts[k] - mt) * (ts[k] - mt);
            }
            const double vy = (tv > 1e-10) ? (sy / tv) : 0.0;
            const double vx = (tv > 1e-10) ? (sx / tv) : 0.0;
            for (size_t k = 0; k < n; ++k) {
                res_y[k * static_cast<size_t>(gh) * gw + gi] = static_cast<float>(rky[k] - my - vy * (ts[k] - mt));
                res_x[k * static_cast<size_t>(gh) * gw + gi] = static_cast<float>(rkx[k] - mx - vx * (ts[k] - mt));
            }
        }
    }

    const size_t pp = static_cast<size_t>(PH) * PW;
    std::vector<double> d_re(pp), d_im(pp), c_re(pp), c_im(pp);
    std::vector<double> phi_sq(static_cast<size_t>(gh) * gw, 0.0);
    std::vector<double> cell_e_phi(static_cast<size_t>(gh) * gw, 0.0);
    double e_phi = 0.0, e_psi = 0.0;
    const double inv_dx2 = 1.0 / (dx_1x * dx_1x), inv_dy2 = 1.0 / (dy_1x * dy_1x);

    for (size_t k = 0; k < n; ++k) {
        std::fill(d_re.begin(), d_re.end(), 0.0);
        std::fill(d_im.begin(), d_im.end(), 0.0);
        std::fill(c_re.begin(), c_re.end(), 0.0);
        std::fill(c_im.begin(), c_im.end(), 0.0);
        const float* uy = res_y.data() + k * static_cast<size_t>(gh) * gw;
        const float* ux = res_x.data() + k * static_cast<size_t>(gh) * gw;
        for (int gy = 0; gy < gh; ++gy) {
            const int gy1 = std::min(gh - 1, gy + 1);
            for (int gx = 0; gx < gw; ++gx) {
                const size_t gi = static_cast<size_t>(gy) * gw + gx;
                if (!g_valid[gi]) continue;
                const int gx1 = std::min(gw - 1, gx + 1);
                const size_t gir = static_cast<size_t>(gy) * gw + gx1;
                const size_t gib = static_cast<size_t>(gy1) * gw + gx;
                const double dux_dx = static_cast<double>(ux[gir] - ux[gi]) / dx_1x;
                const double duy_dy = static_cast<double>(uy[gib] - uy[gi]) / dy_1x;
                const double duy_dx = static_cast<double>(uy[gir] - uy[gi]) / dx_1x;
                const double dux_dy = static_cast<double>(ux[gib] - ux[gi]) / dy_1x;
                const double div_v = dux_dx + duy_dy;
                const double curl_v = duy_dx - dux_dy;
                const int ry = PH - 1 - gy, rx = PW - 1 - gx;
                d_re[static_cast<size_t>(gy) * PW + gx] = div_v;
                d_re[static_cast<size_t>(gy) * PW + rx] = div_v;
                d_re[static_cast<size_t>(ry) * PW + gx] = div_v;
                d_re[static_cast<size_t>(ry) * PW + rx] = div_v;
                c_re[static_cast<size_t>(gy) * PW + gx] = curl_v;
                c_re[static_cast<size_t>(gy) * PW + rx] = -curl_v;
                c_re[static_cast<size_t>(ry) * PW + gx] = -curl_v;
                c_re[static_cast<size_t>(ry) * PW + rx] = curl_v;
            }
        }
        detail::fft_2d_rect(d_re, d_im, PH, PW, false);
        detail::fft_2d_rect(c_re, c_im, PH, PW, false);
        for (int ky = 0; ky < PH; ++ky) {
            const double sy_k = std::sin(std::numbers::pi * ky / PH);
            for (int kx = 0; kx < PW; ++kx) {
                const size_t fi = static_cast<size_t>(ky) * PW + kx;
                if (ky == 0 && kx == 0) { d_re[0] = d_im[0] = c_re[0] = c_im[0] = 0.0; continue; }
                const double sx_k = std::sin(std::numbers::pi * kx / PW);
                const double lap = 4.0 * (sx_k * sx_k * inv_dx2 + sy_k * sy_k * inv_dy2);
                const double inv_lap = -1.0 / std::max(1e-12, lap);
                d_re[fi] *= inv_lap; d_im[fi] *= inv_lap;
                c_re[fi] *= inv_lap; c_im[fi] *= inv_lap;
            }
        }
        detail::fft_2d_rect(d_re, d_im, PH, PW, true);
        detail::fft_2d_rect(c_re, c_im, PH, PW, true);
        for (int gy = 0; gy < gh; ++gy) {
            const int gy1 = std::min(PH - 1, gy + 1);
            for (int gx = 0; gx < gw; ++gx) {
                const int gx1 = std::min(PW - 1, gx + 1);
                const size_t pi0 = static_cast<size_t>(gy) * PW + gx;
                const size_t pir = static_cast<size_t>(gy) * PW + gx1;
                const size_t pib = static_cast<size_t>(gy1) * PW + gx;
                const double dpx = (d_re[pir] - d_re[pi0]) / dx_1x;
                const double dpy = (d_re[pib] - d_re[pi0]) / dy_1x;
                const double cpx = (c_re[pir] - c_re[pi0]) / dx_1x;
                const double cpy = (c_re[pib] - c_re[pi0]) / dy_1x;
                const double gp2 = dpx * dpx + dpy * dpy;
                e_phi += gp2;
                e_psi += cpx * cpx + cpy * cpy;
                cell_e_phi[static_cast<size_t>(gy) * gw + gx] += gp2;
                const double ph0 = d_re[pi0];
                phi_sq[static_cast<size_t>(gy) * gw + gx] += ph0 * ph0;
            }
        }
    }

    const double tot_e = e_phi + e_psi;
    const double irrot_ratio = (tot_e > 1e-12) ? (e_phi / tot_e) : 0.0;
    const float f_px = 2.0f * heraw_suite::detail::half_focal_px(b);
    const double scale_opd = (sg.units == "um" && f_px > 0.0f)
        ? (1e6 / (static_cast<double>(f_px) * static_cast<double>(f_px))) * static_cast<double>(b.focus_distance_m)
        : (1.0 / std::max(1.0, static_cast<double>(f_px > 0.0f ? f_px : 1000.0f)));

    double max_opd = 0.0;
    std::vector<float> phi_grid(static_cast<size_t>(gh) * gw, 0.0f);
    for (size_t gi = 0; gi < phi_grid.size(); ++gi) {
        const float rms_phi = static_cast<float>(std::sqrt(phi_sq[gi] / n) * scale_opd);
        phi_grid[gi] = rms_phi;
        max_opd = std::max<double>(max_opd, rms_phi);
    }

    double max_cell_e_phi = 0.0;
    for (double ce : cell_e_phi) max_cell_e_phi = std::max(max_cell_e_phi, ce);
    const double mean_g_energy = std::sqrt(e_phi / std::max<size_t>(1, n * gh * gw));
    const double peak_g_energy = std::sqrt(max_cell_e_phi / std::max<size_t>(1, n));
    const double irrot_thr = (opts.gate_mode == ObserverGateMode::Auto) ? 0.72 :
                             ((opts.gate_mode == ObserverGateMode::Relaxed) ? 0.55 : 0.0);
    const double energy_thr = (opts.gate_mode == ObserverGateMode::Auto) ? 0.035 :
                              ((opts.gate_mode == ObserverGateMode::Relaxed) ? 0.015 : 0.0);
    const double peak_thr = (opts.gate_mode == ObserverGateMode::Auto) ? 0.08 :
                            ((opts.gate_mode == ObserverGateMode::Relaxed) ? 0.04 : 0.0);
    const bool phys_bos = (tot_e > 1e-8 && irrot_ratio >= 0.72 && (mean_g_energy >= 0.035 || peak_g_energy >= 0.08));
    sg.threshold = irrot_thr;
    sg.observable = (opts.gate_mode == ObserverGateMode::Force && tot_e > 0.0) ||
                    (tot_e > 1e-8 && irrot_ratio >= irrot_thr && (mean_g_energy >= energy_thr || peak_g_energy >= peak_thr));
    sg.forced = (opts.gate_mode == ObserverGateMode::Force && !phys_bos && sg.observable);
    sg.measured = irrot_ratio;
    sg.confidence = sg.observable ? std::min(1.0, std::max(0.0, (irrot_ratio - 0.50) / 0.45)) : 0.0;
    sg.reason = sg.observable
        ? "Helmholtz decomposition confirmed curl-free refractive OPD plume (irrotational >= 0.72)"
        : "flow field fails Helmholtz curl-free gate (pure noise or rotational vortex/rigid twist)";

    if (sg.observable) {
        for (int y = 0; y < Hh; ++y) {
            const float fgy = (y + 0.5f) / static_cast<float>(step_y) - 0.5f;
            for (int x = 0; x < Wh; ++x) {
                opd_map[static_cast<size_t>(y) * Wh + x] =
                    heraw_refocus::detail::sample_bilinear(phi_grid.data(), gh, gw, fgy, (x + 0.5f) / static_cast<float>(step_x) - 0.5f);
            }
        }
        rep.max_schlieren_opd = static_cast<float>(max_opd);
    }
    rep.schlieren_irrotational_ratio = static_cast<float>(irrot_ratio);
    rep.schlieren_gate = sg;
    rep.maps["schlieren_opd"] = std::move(opd_map);
    rep.gates["schlieren_opd"] = std::move(sg);
}

// O5: Rolling-Shutter Rank-2 Modal Vibrometry, Damping Ratio & Long-Burst 14-bit Perfusion/BCG
inline void run_vibrometry_and_vital(const BurstView& b, const FieldResult& fields,
                                     const ObserverOptions& opts, ObserverReport& rep) {
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hp = static_cast<size_t>(Hh) * Wh, n = b.frames_lin.size();
    bool has_real_t = false;
    std::vector<double> ts(n, 0.0);
    for (size_t k = 0; k < n; ++k) ts[k] = detail::frame_time_sec(b, k, has_real_t);
    double t_span = 0.0, min_dt_sorted = 0.0;
    detail::sorted_time_stats(ts, t_span, min_dt_sorted);
    const auto cam = fit_camera(b);

    // 5a. Rolling-Shutter Rank-2 Modal Vibrometry + Damping Ratio zeta
    if (opts.compute_vibrometry) {
        std::vector<float> vib_map(hp, 0.0f);
        Gate vg;
        vg.threshold = (opts.gate_mode == ObserverGateMode::Auto) ? 6.0 :
                       ((opts.gate_mode == ObserverGateMode::Relaxed) ? 3.5 : 0.0);
        vg.units = "1x_px";
        float t_read_ms = (opts.readout_ms_override >= 0.0f)
            ? opts.readout_ms_override
            : detail::lookup_sensor_readout_ms(opts.camera_make, opts.camera_model, b.H1, opts.shutter_kind);
        if (rep.readout_self_calibrated && opts.readout_ms_override < 0.0f) t_read_ms = rep.readout_ms_used;

        if (!(n >= 4 && Hh >= 16 && Wh >= 16)) {
            vg.reason = "vibrometry requires >=4 frames and >=16x16 half-grid dimensions";
        } else {
            struct EdgePt { int y, x; float gx, gy, gnorm, nsd, w; };
            std::vector<EdgePt> epts;
            epts.reserve(4096);
            const int estep = std::max(1, static_cast<int>(std::sqrt(static_cast<double>(hp) / 4096.0)));
            std::vector<float> ref_g(hp, 0.0f);
            for (int y = 1; y + 1 < Hh; y += estep) {
                for (int x = 1; x + 1 < Wh; x += estep) {
                    const size_t i = static_cast<size_t>(y) * Wh + x;
                    double s = 0.0; int cnt = 0;
                    for (size_t k = 0; k < n; ++k) {
                        const auto cs = cam[k].source(static_cast<float>(y), static_cast<float>(x));
                        if (!heraw_suite::detail::inside(cs[0], cs[1], Hh, Wh)) continue;
                        const float g = heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, cs[0], cs[1]);
                        if (std::isfinite(g) && g < 0.96f) { s += g; ++cnt; }
                    }
                    if (cnt >= 3) ref_g[i] = static_cast<float>(s / cnt);
                }
            }
            int min_ey = Hh, max_ey = 0;
            for (int y = 2; y < Hh - 2; y += estep) {
                for (int x = 2; x < Wh - 2; x += estep) {
                    const size_t i = static_cast<size_t>(y) * Wh + x;
                    const float c = b.g_halfs[0][i];
                    const float nsd = std::sqrt(heraw_suite::detail::variance(b, c));
                    const float gx = 0.5f * (b.g_halfs[0][i + 1] - b.g_halfs[0][i - 1]);
                    const float gy = 0.5f * (b.g_halfs[0][i + Wh] - b.g_halfs[0][i - Wh]);
                    const float gm = std::hypot(gx, gy);
                    if (gm > 4.5f * nsd && gm < 0.8f) {
                        epts.push_back({y, x, gx / gm, gy / gm, gm, nsd, 1.0f});
                        min_ey = std::min(min_ey, y); max_ey = std::max(max_ey, y);
                    }
                }
            }
            if (epts.size() < 16) {
                vg.reason = "insufficient high-SNR edges (<16) for modal vibrometry";
            } else {
                const size_t M = epts.size();
                std::vector<double> shifts(n * M, 0.0);
                std::vector<double> shifts_y(n * M, 0.0), shifts_x(n * M, 0.0);
                std::vector<double> mean_shift_pt(M, 0.0);
                double tot_e2 = 0.0;
                for (size_t m = 0; m < M; ++m) {
                    const auto& ep = epts[m];
                    const size_t eidx = static_cast<size_t>(ep.y) * Wh + ep.x;
                    const float refv = (ref_g[eidx] > 0.0f) ? ref_g[eidx] : b.g_halfs[0][eidx];
                    for (size_t k = 0; k < n; ++k) {
                        const auto cs = cam[k].source(static_cast<float>(ep.y), static_cast<float>(ep.x));
                        if (!heraw_suite::detail::inside(cs[0], cs[1], Hh, Wh)) continue;
                        const float gk = heraw_suite::detail::sample(b.g_halfs[k], Hh, Wh, cs[0], cs[1]);
                        if (!std::isfinite(gk)) continue;
                        const double dx_1x = -2.0 * static_cast<double>(gk - refv) / std::max(1e-4f, ep.gnorm);
                        shifts[k * M + m] = dx_1x;
                        mean_shift_pt[m] += dx_1x;
                    }
                    mean_shift_pt[m] /= static_cast<double>(n);
                    for (size_t k = 0; k < n; ++k) {
                        const double s_c = std::max(-1.8, std::min(1.8, shifts[k * M + m] - mean_shift_pt[m]));
                        shifts[k * M + m] = s_c;
                        shifts_y[k * M + m] = s_c * ep.gy;
                        shifts_x[k * M + m] = s_c * ep.gx;
                        tot_e2 += s_c * s_c;
                    }
                }

                double best_f = 0.0, best_snr = 0.0, best_pow = 0.0;
                std::vector<double> best_ck(n, 0.0), best_sk(n, 0.0), p_spectrum;
                bool pick_is_rs = false;
                auto exposure_sinc_ok = [&](double f_hz) -> bool {
                    if (b.exposure_seconds <= 0.0f) return true;
                    const double ex = std::numbers::pi * f_hz * b.exposure_seconds;
                    return (ex <= 1e-4) || (std::fabs(std::sin(ex) / ex) >= 0.16);
                };
                const double y_frac = std::max(0.1, static_cast<double>(max_ey - min_ey) / std::max(1, Hh));
                const bool do_rs = (t_read_ms > 0.25f) && ((max_ey - min_ey) >= 8);
                if (do_rs) {
                    const double t_read_s = static_cast<double>(t_read_ms) * 1e-3;
                    const double f_lo = std::max(20.0, 0.45 / std::max(1e-5, t_read_s * y_frac));
                    const double df = std::max(4.0, f_lo * 0.15);
                    auto eval_rs_freq = [&](double f, std::vector<double>& ckv, std::vector<double>& skv) -> double {
                        const double om_r = 2.0 * std::numbers::pi * f * t_read_s / std::max(1, Hh);
                        double px_f = 0.0, py_f = 0.0;
                        std::vector<double> ck_x(n), sk_x(n), ck_y(n), sk_y(n);
                        for (size_t k = 0; k < n; ++k) {
                            double rex = 0.0, imx = 0.0, rey = 0.0, imy = 0.0;
                            for (size_t m = 0; m < M; ++m) {
                                const double ph = om_r * epts[m].y;
                                const double cs = std::cos(ph), sn = std::sin(ph);
                                rex += shifts_x[k * M + m] * cs; imx -= shifts_x[k * M + m] * sn;
                                rey += shifts_y[k * M + m] * cs; imy -= shifts_y[k * M + m] * sn;
                            }
                            ck_x[k] = 2.0 * rex / M; sk_x[k] = -2.0 * imx / M;
                            ck_y[k] = 2.0 * rey / M; sk_y[k] = -2.0 * imy / M;
                            px_f += ck_x[k] * ck_x[k] + sk_x[k] * sk_x[k];
                            py_f += ck_y[k] * ck_y[k] + sk_y[k] * sk_y[k];
                        }
                        const bool use_y = (py_f >= px_f);
                        ckv = use_y ? std::move(ck_y) : std::move(ck_x);
                        skv = use_y ? std::move(sk_y) : std::move(sk_x);
                        return (px_f + py_f) * (0.5 * static_cast<double>(M)) / std::max(1e-12, tot_e2);
                    };
                    for (double f = f_lo; f <= 1600.0; f += df) {
                        if (!exposure_sinc_ok(f)) continue;
                        std::vector<double> ckv, skv;
                        const double norm_p = eval_rs_freq(f, ckv, skv);
                        p_spectrum.push_back(norm_p);
                        if (norm_p > best_pow) {
                            best_pow = norm_p; best_f = f; pick_is_rs = true;
                            best_ck = std::move(ckv); best_sk = std::move(skv);
                        }
                    }
                    if (best_f > 0.0) {
                        for (double f = std::max(f_lo, best_f - df); f <= best_f + df; f += df * 0.1) {
                            if (!exposure_sinc_ok(f)) continue;
                            std::vector<double> ckv, skv;
                            const double norm_p = eval_rs_freq(f, ckv, skv);
                            if (norm_p > best_pow) {
                                best_pow = norm_p; best_f = f; pick_is_rs = true;
                                best_ck = std::move(ckv); best_sk = std::move(skv);
                            }
                        }
                    }
                }
                if (has_real_t && n >= 8 && t_span >= 0.3 && min_dt_sorted > 1e-4) {
                    // Low-frequency inter-frame Lomb-Scargle scan with 2D vector mode energy
                    const double f_nyq = 0.48 / min_dt_sorted;
                    const double df_t = 0.25 / t_span;
                    for (double f = std::max(0.8, 1.5 / t_span); f <= std::min(60.0, f_nyq); f += df_t) {
                        if (!exposure_sinc_ok(f)) continue;
                        const double om = 2.0 * std::numbers::pi * f;
                        double e_mode = 0.0;
                        std::vector<double> ckv(n), skv(n);
                        for (size_t k = 0; k < n; ++k) { ckv[k] = std::cos(om * ts[k]); skv[k] = std::sin(om * ts[k]); }
                        const double t_read_lf = do_rs ? (static_cast<double>(t_read_ms) * 1e-3) : 0.0;
                        double rx = 0.0, ix = 0.0, ry = 0.0, iy = 0.0;
                        for (size_t m = 0; m < M; ++m) {
                            const double ph_m = om * (t_read_lf * epts[m].y / std::max(1, Hh));
                            const double cm = std::cos(ph_m), sm_r = std::sin(ph_m);
                            for (size_t k = 0; k < n; ++k) {
                                const double c_km = ckv[k] * cm - skv[k] * sm_r;
                                const double s_km = skv[k] * cm + ckv[k] * sm_r;
                                rx += shifts_x[k * M + m] * c_km; ix += shifts_x[k * M + m] * s_km;
                                ry += shifts_y[k * M + m] * c_km; iy += shifts_y[k * M + m] * s_km;
                            }
                        }
                        e_mode = (rx * rx + ix * ix + ry * ry + iy * iy) * (2.0 / (static_cast<double>(n) * static_cast<double>(M)));
                        const double norm_p = e_mode / std::max(1e-12, tot_e2);
                        p_spectrum.push_back(norm_p);
                        if (norm_p > best_pow) {
                            best_pow = norm_p; best_f = f; pick_is_rs = false;
                            best_ck = ckv; best_sk = skv;
                        }
                    }
                    // Extract instantaneous sliding-window envelope on low-frequency peak for damping ratio zeta
                    if (!pick_is_rs && best_f > 0.0) {
                        const double om = 2.0 * std::numbers::pi * best_f;
                        std::vector<double> wx(M, 0.0), wy(M, 0.0), s_mode(n, 0.0);
                        for (size_t m = 0; m < M; ++m) {
                            double axc = 0.0, axs = 0.0, ayc = 0.0, ays = 0.0;
                            for (size_t k = 0; k < n; ++k) {
                                const double c = std::cos(om * ts[k]), s = std::sin(om * ts[k]);
                                axc += shifts_x[k * M + m] * c; axs += shifts_x[k * M + m] * s;
                                ayc += shifts_y[k * M + m] * c; ays += shifts_y[k * M + m] * s;
                            }
                            wx[m] = axc * 2.0 / n; wy[m] = ayc * 2.0 / n;
                            if (std::hypot(axs, ays) > std::hypot(axc, ayc)) { wx[m] = axs * 2.0 / n; wy[m] = ays * 2.0 / n; }
                        }
                        double wn = 0.0;
                        for (size_t m = 0; m < M; ++m) wn += wx[m] * wx[m] + wy[m] * wy[m];
                        if (wn > 1e-12) {
                            for (size_t k = 0; k < n; ++k) {
                                double sm = 0.0;
                                for (size_t m = 0; m < M; ++m)
                                    sm += shifts_x[k * M + m] * wx[m] + shifts_y[k * M + m] * wy[m];
                                s_mode[k] = sm / wn;
                            }
                            const size_t hw_k = std::max<size_t>(2u, n / 8u);
                            for (size_t k = 0; k < n; ++k) {
                                const size_t k0 = (k > hw_k) ? (k - hw_k) : 0u;
                                const size_t k1 = std::min(n - 1, k + hw_k);
                                double G2[2][3] = {};
                                for (size_t j = k0; j <= k1; ++j) {
                                    const double c = std::cos(om * ts[j]), s = std::sin(om * ts[j]);
                                    G2[0][0] += c * c; G2[0][1] += c * s; G2[1][1] += s * s;
                                    G2[0][2] += s_mode[j] * c; G2[1][2] += s_mode[j] * s;
                                }
                                G2[1][0] = G2[0][1];
                                const double d2 = G2[0][0] * G2[1][1] - G2[0][1] * G2[0][1];
                                double env = 1.0;
                                if (d2 > 1e-8) {
                                    const double ac = (G2[0][2] * G2[1][1] - G2[1][2] * G2[0][1]) / d2;
                                    const double as = (G2[0][0] * G2[1][2] - G2[0][1] * G2[0][2]) / d2;
                                    env = std::hypot(ac, as);
                                }
                                best_ck[k] = env * std::cos(om * ts[k]);
                                best_sk[k] = env * std::sin(om * ts[k]);
                            }
                        }
                    }
                }

                if (p_spectrum.size() >= 6) {
                    std::vector<double> ps = p_spectrum;
                    std::sort(ps.begin(), ps.end());
                    best_snr = best_pow / std::max(1e-8, ps[ps.size() / 2]);
                } else if (opts.gate_mode == ObserverGateMode::Force && best_pow > 0.0) {
                    best_snr = std::max(best_snr, best_pow);
                }
                const double t_read_s = static_cast<double>(t_read_ms) * 1e-3;
                const bool is_rs_pick = pick_is_rs && do_rs && best_f >= std::max(20.0, 0.45 / std::max(1e-5, t_read_s * y_frac));
                double max_amp = 0.0;
                if (best_f > 0.0 && best_snr >= vg.threshold) {
                    const double sinc_e = (b.exposure_seconds > 0.0f)
                        ? [&]() { double x = std::numbers::pi * best_f * b.exposure_seconds; return (x < 1e-4) ? 1.0 : std::fabs(std::sin(x) / x); }()
                        : 1.0;
                    const double inv_sinc = 1.0 / std::max(0.16, sinc_e);
                    double rms_sq = 0.0;
                    for (size_t k = 0; k < n; ++k) rms_sq += best_ck[k] * best_ck[k] + best_sk[k] * best_sk[k];
                    const double ck_rms = std::sqrt(rms_sq / std::max<size_t>(1, n));
                    std::vector<double> norm_ck = best_ck, norm_sk = best_sk;
                    if (ck_rms > 1e-8) {
                        for (size_t k = 0; k < n; ++k) { norm_ck[k] /= ck_rms; norm_sk[k] /= ck_rms; }
                    }
                    for (size_t m = 0; m < M; ++m) {
                        double c2 = 0.0, s2 = 0.0, cs = 0.0, dc = 0.0, ds = 0.0;
                        const double ph_row = is_rs_pick ? (2.0 * std::numbers::pi * best_f * t_read_s * epts[m].y / std::max(1, Hh)) : 0.0;
                        const double cr = std::cos(ph_row), sr = std::sin(ph_row);
                        for (size_t k = 0; k < n; ++k) {
                            const double b1 = norm_ck[k] * cr + norm_sk[k] * sr;
                            const double b2 = -norm_sk[k] * cr + norm_ck[k] * sr;
                            const double yv = shifts[k * M + m];
                            c2 += b1 * b1; s2 += b2 * b2; cs += b1 * b2;
                            dc += yv * b1; ds += yv * b2;
                        }
                        const double det = c2 * s2 - cs * cs;
                        if (det > 1e-12) {
                            const double ac = (dc * s2 - ds * cs) / det;
                            const double as = (ds * c2 - dc * cs) / det;
                            const double amp_raw = std::hypot(ac, as);
                            const float amp = static_cast<float>(amp_raw * inv_sinc);
                            const float nsd_px = 2.0f * std::sqrt(2.0f) * epts[m].nsd / std::max(1e-4f, epts[m].gnorm);
                            if (amp_raw > 2.5 * nsd_px && amp <= 1.8f) {
                                max_amp = std::max<double>(max_amp, amp);
                                for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                                    const int yy = epts[m].y + dy, xx = epts[m].x + dx;
                                    if (yy >= 0 && yy < Hh && xx >= 0 && xx < Wh) {
                                        float& dst = vib_map[static_cast<size_t>(yy) * Wh + xx];
                                        dst = std::max(dst, amp);
                                    }
                                }
                            }
                        }
                    }
                    // Exponential damping fit when N >= 16 and timed
                    if (has_real_t && n >= 16 && t_span >= 0.8 && max_amp > 0.0) {
                        double st = 0.0, se = 0.0, stt = 0.0, ste = 0.0;
                        int dc_cnt = 0;
                        for (size_t k = 0; k < n; ++k) {
                            const double env = std::hypot(best_ck[k], best_sk[k]);
                            if (env > 1e-5) {
                                const double le = std::log(env);
                                st += ts[k]; se += le; stt += ts[k] * ts[k]; ste += ts[k] * le; ++dc_cnt;
                            }
                        }
                        if (dc_cnt >= 8) {
                            const double den = dc_cnt * stt - st * st;
                            const double slope = (den > 1e-12) ? ((dc_cnt * ste - st * se) / den) : 0.0;
                            if (slope < -0.02) {
                                rep.modal_damping_zeta = static_cast<float>(-slope / (2.0 * std::numbers::pi * best_f));
                                rep.damping_observable = true;
                            }
                        }
                    }
                }
                const bool phys_vib = (best_snr >= 6.0 && max_amp > 0.01);
                vg.observable = (opts.gate_mode == ObserverGateMode::Force && best_f > 0.0 && max_amp > 0.0) ||
                                (best_snr >= vg.threshold && max_amp > 0.01);
                vg.forced = (opts.gate_mode == ObserverGateMode::Force && !phys_vib && vg.observable);
                vg.measured = vg.observable ? max_amp : best_snr;
                vg.confidence = vg.observable ? std::min(1.0, best_snr / 24.0) : 0.0;
                vg.reason = vg.observable
                    ? "rank-2 nuisance-phase vibrometry locked modal frequency & subpixel amplitude"
                    : "no spatially coherent subpixel oscillation above photon/registration floor";
                if (vg.observable) {
                    rep.modal_freq_hz = static_cast<float>(best_f);
                    rep.modal_max_amp_1x_px = static_cast<float>(max_amp);
                    rep.modal_snr = static_cast<float>(best_snr);
                }
            }
        }
        rep.vibrometry_gate = vg;
        rep.maps["vibration_amp"] = std::move(vib_map);
        rep.gates["vibration_amp"] = std::move(vg);
    }

    // 5b. Long-Burst 14-bit Linear P^3-Orthogonal Vital Perfusion (rPPG) & Ballistocardiography (BCG)
    if (opts.compute_vital) {
        std::vector<float> vit_map(hp, 0.0f);
        Gate rg;
        rg.threshold = (opts.gate_mode == ObserverGateMode::Auto) ? 7.0 :
                       ((opts.gate_mode == ObserverGateMode::Relaxed) ? 4.5 : 0.0);
        rg.units = "BPM";
        if (!has_real_t || n < 24 || t_span < 1.8) {
            rg.reason = "14-bit vital perfusion requires >=24 frames spanning >=1.8s with timestamps";
        } else {
            const int BSTEP = 16;
            const int mh = std::max(1, Hh / BSTEP), mw_grid = std::max(1, Wh / BSTEP);
            const auto wb_n = [&]() -> std::array<float, 3> {
                const float r = 1.0f / std::max(0.05f, opts.cam_wb[0]);
                const float g = 1.0f / std::max(0.05f, opts.cam_wb[1]);
                const float bl = 1.0f / std::max(0.05f, opts.cam_wb[2]);
                const float nrm = std::max(1e-6f, std::hypot(r, g, bl));
                return {{r / nrm, g / nrm, bl / nrm}};
            }();
            const std::array<float, 3> ci = rep.has_illuminant ? rep.illuminant_rgb : wb_n;
            const auto gdir = detail::gravity_dir(opts.cam_flip);
            double best_hr = 0.0, best_hr_snr = 0.0;
            const double min_dt = (min_dt_sorted > 1e-4) ? min_dt_sorted : t_span;
            const double f_hi = std::min(2.85, 0.45 / min_dt);

            std::vector<double> freqs;
            for (double f = 0.85; f <= f_hi; f += 0.025) freqs.push_back(f);
            std::vector<double> global_spec(freqs.size(), 0.0);
            struct MacroBlock { int my, mx; double amp, bcg, fpk, snr; };
            std::vector<MacroBlock> active_blocks;

            std::vector<std::array<double, 3>> blk_rgb(n);
            std::vector<double> blk_y(n);
            for (int by = 0; by < mh; ++by) {
                for (int bx = 0; bx < mw_grid; ++bx) {
                    bool ok_blk = true;
                    std::array<double, 3> mean_c{{0.0, 0.0, 0.0}};
                    double y_mean = 0.0;
                    for (size_t k = 0; k < n; ++k) {
                        std::array<double, 3> cs{{0.0, 0.0, 0.0}};
                        double dy_acc = 0.0;
                        int cnt = 0;
                        for (int dy = 2; dy < BSTEP - 1; dy += 3) {
                            const int ry = by * BSTEP + dy;
                            if (ry >= Hh) continue;
                            for (int dx = 2; dx < BSTEP - 1; dx += 3) {
                                const int rx = bx * BSTEP + dx;
                                if (rx >= Wh) continue;
                                const size_t hi = static_cast<size_t>(ry) * Wh + rx;
                                if (b.mw_half[k][hi] < 0.45f) continue;
                                float sy1, sx1; bool ok = false;
                                detail::aligned_source_coords(b, fields, k, ry, rx, sy1, sx1, ok);
                                if (!ok) continue;
                                const auto kr = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                                if (kr[0] < 0.01f || kr[1] < 0.01f || kr[2] < 0.01f || kr[1] > 0.95f) continue;
                                for (int ch = 0; ch < 3; ++ch) cs[ch] += kr[ch];
                                const auto s_cam = cam[k].source(static_cast<float>(ry), static_cast<float>(rx));
                                const double vy = 2.0 * (static_cast<double>(ry) + b.flows_fy[k][hi] - s_cam[0]);
                                const double vx = 2.0 * (static_cast<double>(rx) + b.flows_fx[k][hi] - s_cam[1]);
                                dy_acc += gdir[0] * vy + gdir[1] * vx;
                                ++cnt;
                            }
                        }
                        if (cnt < 4) { ok_blk = false; break; }
                        for (int ch = 0; ch < 3; ++ch) {
                            blk_rgb[k][ch] = cs[ch] / cnt;
                            mean_c[ch] += blk_rgb[k][ch];
                        }
                        blk_y[k] = dy_acc / cnt;
                        y_mean += blk_y[k];
                    }
                    if (!ok_blk) continue;
                    for (int ch = 0; ch < 3; ++ch) mean_c[ch] /= static_cast<double>(n);
                    y_mean /= static_cast<double>(n);

                    // Double-orthogonal projection axis p_hem perp span(mean_c, ci) -> pure hemoglobin modulation
                    const double nm = std::hypot(mean_c[0], mean_c[1], mean_c[2]);
                    if (nm < 0.015) continue;
                    std::array<double, 3> u1{{mean_c[0] / nm, mean_c[1] / nm, mean_c[2] / nm}};
                    std::array<double, 3> p_hem{{
                        u1[1]*ci[2] - u1[2]*ci[1],
                        u1[2]*ci[0] - u1[0]*ci[2],
                        u1[0]*ci[1] - u1[1]*ci[0]
                    }};
                    double ph_n = std::hypot(p_hem[0], p_hem[1], p_hem[2]);
                    if (ph_n < 0.05) {
                        p_hem = {{-u1[1], u1[0] - u1[2], u1[1]}};
                        const double d = p_hem[0]*u1[0] + p_hem[1]*u1[1] + p_hem[2]*u1[2];
                        for (int c = 0; c < 3; ++c) p_hem[c] -= d * u1[c];
                        ph_n = std::hypot(p_hem[0], p_hem[1], p_hem[2]);
                    }
                    if (ph_n < 1e-6) continue;
                    for (int c = 0; c < 3; ++c) p_hem[c] /= ph_n;
                    if (p_hem[1] < 0.0) for (double& v : p_hem) v = -v;

                    std::vector<double> sig(n, 0.0);
                    double s_mean = 0.0;
                    for (size_t k = 0; k < n; ++k) {
                        sig[k] = ((blk_rgb[k][0] - mean_c[0])*p_hem[0] +
                                  (blk_rgb[k][1] - mean_c[1])*p_hem[1] +
                                  (blk_rgb[k][2] - mean_c[2])*p_hem[2]) / nm;
                        s_mean += sig[k];
                    }
                    s_mean /= static_cast<double>(n);
                    for (double& v : sig) v -= s_mean;

                    double b_best_p = 0.0, b_best_f = 0.0, b_bcg = 0.0;
                    std::vector<double> b_spec(freqs.size(), 0.0);
                    for (size_t fi = 0; fi < freqs.size(); ++fi) {
                        const double om = 2.0 * std::numbers::pi * freqs[fi];
                        double re = 0.0, im = 0.0, yr = 0.0, yi = 0.0;
                        for (size_t k = 0; k < n; ++k) {
                            const double cs = std::cos(om * ts[k]), sn = std::sin(om * ts[k]);
                            re += sig[k] * cs; im += sig[k] * sn;
                            yr += (blk_y[k] - y_mean) * cs; yi += (blk_y[k] - y_mean) * sn;
                        }
                        const double pw = 4.0 * (re * re + im * im) / (n * n);
                        b_spec[fi] = pw;
                        if (pw > b_best_p) {
                            b_best_p = pw; b_best_f = freqs[fi];
                            b_bcg = 2.0 * std::hypot(yr, yi) / n;
                        }
                    }
                    if (b_spec.size() >= 6) {
                        std::vector<double> b_sorted = b_spec;
                        std::sort(b_sorted.begin(), b_sorted.end());
                        const double snr_b = b_best_p / std::max(1e-14, b_sorted[b_sorted.size() / 2]);
                        if (snr_b >= 6.5) {
                            for (size_t fi = 0; fi < freqs.size(); ++fi) global_spec[fi] += b_spec[fi];
                            active_blocks.push_back({by, bx, std::sqrt(b_best_p), b_bcg, b_best_f, snr_b});
                        }
                    }
                }
            }
            if (!freqs.empty() && !active_blocks.empty()) {
                size_t best_fi = 0;
                for (size_t fi = 1; fi < freqs.size(); ++fi)
                    if (global_spec[fi] > global_spec[best_fi]) best_fi = fi;
                std::vector<double> gs = global_spec;
                std::sort(gs.begin(), gs.end());
                best_hr = freqs[best_fi];
                best_hr_snr = global_spec[best_fi] / std::max(1e-14, gs[gs.size() / 2]);
                double max_bcg = 0.0;
                size_t coh_blk = 0;
                for (const auto& ab : active_blocks) {
                    if (std::fabs(ab.fpk - best_hr) <= 0.12) {
                        ++coh_blk;
                        max_bcg = std::max(max_bcg, ab.bcg);
                        for (int dy = 0; dy < BSTEP; ++dy) {
                            const int ry = ab.my * BSTEP + dy;
                            if (ry >= Hh) continue;
                            for (int dx = 0; dx < BSTEP; ++dx) {
                                const int rx = ab.mx * BSTEP + dx;
                                if (rx < Wh) vit_map[static_cast<size_t>(ry) * Wh + rx] = static_cast<float>(ab.amp);
                            }
                        }
                    }
                }
                const size_t min_blk = (opts.gate_mode == ObserverGateMode::Auto) ? 3u : 1u;
                const bool phys_vital = (best_hr_snr >= 7.0 && coh_blk >= 3u);
                if (best_hr_snr >= rg.threshold && coh_blk >= min_blk) {
                    rg.observable = true;
                    rg.forced = (opts.gate_mode == ObserverGateMode::Force && !phys_vital);
                    rep.vital_bpm = static_cast<float>(best_hr * 60.0);
                    rep.vital_snr = static_cast<float>(best_hr_snr);
                    rep.bcg_amp_1x_px = static_cast<float>(max_bcg);
                    rg.measured = rep.vital_bpm;
                    rg.confidence = std::min(1.0, best_hr_snr / 24.0);
                    rg.reason = "14-bit P^3-orthogonal rPPG & BCG cardiac oscillation locked";
                } else {
                    rg.reason = "periodic biological perfusion SNR below coherent block gate";
                }
            } else {
                rg.reason = "insufficient spatially consistent perfused tissue blocks";
            }
        }
        rep.vital_gate = rg;
        rep.maps["vital_perfusion"] = std::move(vit_map);
        rep.gates["vital_perfusion"] = std::move(rg);
    }
}

// O6..O10: Extended Scientific Observers
inline void run_extended_observers_o6_to_o10(const BurstView& b, const FieldResult& fields,
                                           const ObserverOptions& opts, ObserverReport& rep) {
    const int Hh = b.H1 / 2, Wh = b.W1 / 2;
    const size_t hp = static_cast<size_t>(Hh) * Wh, n = b.frames_lin.size();
    std::vector<double> ts(n, 0.0);
    bool has_real_t = false;
    for (size_t k = 0; k < n; ++k) ts[k] = detail::frame_time_sec(b, k, has_real_t);
    double t_span = 0.0, min_dt = 0.0;
    detail::sorted_time_stats(ts, t_span, min_dt);

    // O6: Axial LoCA defocus polarity & front/back slope
    if (opts.compute_loca_sign) {
        Gate lg;
        lg.units = "px/rho";
        lg.threshold = 0.15;
        auto dep_it = fields.maps.find("depth");
        if (n >= 4 && dep_it != fields.maps.end() && dep_it->second.size() == hp &&
            fields.geometry.has_focus_plane && b.aperture > 0.0f && b.aperture <= 4.0f) {
            const float rho_f = fields.geometry.rho_low +
                fields.geometry.focus_plane_depth * (fields.geometry.rho_high - fields.geometry.rho_low);
            double c_rb = 0.0, v_dr = 0.0, v_rb = 0.0;
            int cnt = 0;
            for (int y = 4; y < Hh - 4; y += 3) for (int x = 4; x < Wh - 4; x += 3) {
                const size_t i = static_cast<size_t>(y) * Wh + x;
                const auto rgb_l = heraw_suite::detail::raw_rgb(b, 0, 2.0f * y + 0.5f, 2.0f * (x - 1) + 0.5f);
                const auto rgb_r = heraw_suite::detail::raw_rgb(b, 0, 2.0f * y + 0.5f, 2.0f * (x + 1) + 0.5f);
                const float gdiff = std::fabs(rgb_r[1] - rgb_l[1]);
                if (gdiff < 0.03f) continue;
                const float rb_edge = (std::fabs(rgb_r[0] - rgb_l[0]) - std::fabs(rgb_r[2] - rgb_l[2])) / std::max(1e-4f, gdiff);
                const float rho = fields.geometry.rho_low + dep_it->second[i] * (fields.geometry.rho_high - fields.geometry.rho_low);
                const double dr = static_cast<double>(rho - rho_f);
                c_rb += dr * rb_edge;
                v_dr += dr * dr;
                v_rb += static_cast<double>(rb_edge) * rb_edge;
                ++cnt;
            }
            const double slope = (v_dr > 1e-6) ? (c_rb / v_dr) : 0.0;
            const double corr = (v_dr > 1e-6 && v_rb > 1e-6) ? (c_rb / std::sqrt(v_dr * v_rb)) : 0.0;
            if (cnt >= 24 && (std::fabs(corr) >= 0.15 || opts.gate_mode == ObserverGateMode::Force)) {
                lg.observable = true;
                rep.loca_slope_px_per_rho = static_cast<float>(slope);
                rep.defocus_sign = (slope >= 0.0) ? 1 : -1;
                lg.measured = std::fabs(corr);
                lg.confidence = std::min(1.0, std::fabs(corr) * 2.0);
                lg.reason = "axial LoCA red-blue gradient slope localized defocus sign";
            } else {
                lg.reason = "insufficient fast-aperture LoCA edge slope";
            }
        } else {
            lg.reason = "requires depth map, focus plane, and <=f/4 lens";
        }
        rep.loca_gate = lg;
        rep.gates["loca_sign"] = lg;
    }

    // O7: Modal capillary/surface wave dispersion speed
    if (opts.compute_modal_wave) {
        Gate wg;
        wg.units = "px/s";
        wg.threshold = 0.08;
        if (n >= 4 && has_real_t && min_dt > 1e-5) {
            std::vector<double> speeds;
            for (size_t k = 1; k < n; ++k) {
                const float dt_k = static_cast<float>(ts[k] - ts[0]);
                if (!(dt_k > 1e-6f)) continue;
                for (int y = 4; y < Hh - 4; y += 6) for (int x = 4; x < Wh - 4; x += 6) {
                    const size_t i = static_cast<size_t>(y) * Wh + x;
                    if (b.mw_half[k][i] > 0.5f) {
                        const float sp = 2.0f * std::hypot(b.flows_fy[k][i], b.flows_fx[k][i]) / dt_k;
                        if (sp > 1.0f && sp < 5000.0f) speeds.push_back(sp);
                    }
                }
            }
            if (speeds.size() >= 16 || (opts.gate_mode == ObserverGateMode::Force && !speeds.empty())) {
                std::sort(speeds.begin(), speeds.end());
                rep.wave_phase_vel_px_s = static_cast<float>(speeds[speeds.size() / 2]);
                wg.observable = (rep.wave_phase_vel_px_s > 0.5f || opts.gate_mode == ObserverGateMode::Force);
                wg.measured = rep.wave_phase_vel_px_s;
                wg.confidence = std::min(1.0, static_cast<double>(speeds.size()) / 128.0);
                wg.reason = wg.observable ? "phase-velocity wave propagation tracked" : "no coherent surface wave speed";
            } else {
                wg.reason = "insufficient spatiotemporal wave samples";
            }
        } else {
            wg.reason = "requires real burst timestamps";
        }
        rep.wave_gate = wg;
        rep.gates["modal_wave"] = wg;
    }

    // O8: Differential-tilt-preserving Kolmogorov seeing (without per-star centroid stripping bias)
    if (opts.compute_seeing) {
        Gate sg;
        sg.units = "1x-px";
        sg.threshold = 0.05;
        std::vector<double> d_tilts;
        for (size_t k = 1; k < n; ++k) {
            for (int y = 8; y < Hh - 8; y += 8) for (int x = 8; x < Wh - 8; x += 8) {
                const size_t i = static_cast<size_t>(y) * Wh + x;
                const size_t j = static_cast<size_t>(y) * Wh + std::min(Wh - 1, x + 8);
                if (b.mw_half[k][i] > 0.6f && b.mw_half[k][j] > 0.6f) {
                    const double dy = 2.0 * (b.flows_fy[k][i] - b.flows_fy[k][j]);
                    const double dx = 2.0 * (b.flows_fx[k][i] - b.flows_fx[k][j]);
                    d_tilts.push_back(dy * dy + dx * dx);
                }
            }
        }
        if (d_tilts.size() >= 12 || (opts.gate_mode == ObserverGateMode::Force && !d_tilts.empty())) {
            const double ms = std::accumulate(d_tilts.begin(), d_tilts.end(), 0.0) / static_cast<double>(d_tilts.size());
            rep.diff_tilt_rms_1x_px = static_cast<float>(std::sqrt(ms));
            rep.seeing_r0_norm = 1.0f / std::max(0.05f, rep.diff_tilt_rms_1x_px);
            sg.observable = (rep.diff_tilt_rms_1x_px >= 0.01f || opts.gate_mode == ObserverGateMode::Force);
            sg.measured = rep.diff_tilt_rms_1x_px;
            sg.confidence = std::min(1.0, static_cast<double>(d_tilts.size()) / 64.0);
            sg.reason = "differential image-motion seeing measured without tip/tilt removal bias";
        } else {
            sg.reason = "insufficient paired differential star/point tracks";
        }
        rep.seeing_gate = sg;
        rep.gates["seeing"] = sg;
    }

    // O9: Fixed-gauge refractive flow & stationary bottom consensus albedo
    if (opts.compute_refraction) {
        Gate rg;
        rg.units = "1x-px";
        rg.threshold = 0.15;
        double e2 = 0.0;
        size_t cnt = 0;
        rep.bottom_albedo_half = {Hh, Wh, std::vector<float>(3 * hp, 0.0f)};
        for (int y = 0; y < Hh; ++y) for (int x = 0; x < Wh; ++x) {
            const size_t i = static_cast<size_t>(y) * Wh + x;
            std::array<std::vector<float>, 3> cols;
            for (size_t k = 0; k < n; ++k) {
                if (b.mw_half[k][i] < 0.5f) continue;
                if (k > 0) {
                    const double d = 2.0 * std::hypot(b.flows_fy[k][i], b.flows_fx[k][i]);
                    e2 += d * d;
                    ++cnt;
                }
                float sy1, sx1; bool ok = false;
                detail::aligned_source_coords(b, fields, k, y, x, sy1, sx1, ok);
                if (!ok) continue;
                const auto rgb = heraw_suite::detail::raw_rgb(b, k, sy1, sx1);
                for (int c = 0; c < 3; ++c) cols[c].push_back(rgb[c]);
            }
            for (int c = 0; c < 3; ++c) {
                if (!cols[c].empty()) {
                    std::nth_element(cols[c].begin(), cols[c].begin() + cols[c].size() / 2, cols[c].end());
                    rep.bottom_albedo_half.rgb[3 * i + c] = cols[c][cols[c].size() / 2];
                }
            }
        }
        if (cnt >= 64) {
            rep.refraction_rms_1x_px = static_cast<float>(std::sqrt(e2 / static_cast<double>(cnt)));
            rg.observable = (rep.refraction_rms_1x_px >= 0.15f || opts.gate_mode == ObserverGateMode::Force);
            rg.measured = rep.refraction_rms_1x_px;
            rg.confidence = std::min(1.0, static_cast<double>(rep.refraction_rms_1x_px) / 2.0);
            rg.reason = "relative refractive flow & median bottom albedo computed under fixed coordinate gauge";
        } else {
            rg.reason = "insufficient aligned samples for refractive bottom separation";
        }
        rep.refraction_gate = rg;
        rep.gates["refraction"] = rg;
    }

    // O10: Celestial / streak kinematics with WCS + clock provenance unit gating & differential photometry
    if (opts.compute_streak) {
        Gate stg;
        const bool has_wcs = (opts.plate_scale_arcsec_px > 0.0f);
        const bool cal_clk = (has_real_t && t_span > 1e-4 && opts.clock_prov.row_calibrated);
        rep.streak_units = (has_wcs && cal_clk) ? "arcsec/s" : (has_real_t ? "px/s" : "px/frame");
        stg.units = rep.streak_units;
        stg.threshold = 0.5;

        // Require dark-sky / point-source background unless gate_mode == Force so terrestrial texture
        // edges are never miscounted as celestial streaks.
        std::vector<float> bg_samp;
        bg_samp.reserve(static_cast<size_t>((Hh / 8) + 1) * static_cast<size_t>((Wh / 8) + 1));
        for (int y = 0; y < Hh; y += 8) {
            for (int x = 0; x < Wh; x += 8) bg_samp.push_back(b.g_halfs[0][static_cast<size_t>(y) * Wh + x]);
        }
        float bg_med = 0.5f;
        if (!bg_samp.empty()) {
            std::nth_element(bg_samp.begin(), bg_samp.begin() + bg_samp.size() / 2, bg_samp.end());
            bg_med = bg_samp[bg_samp.size() / 2];
        }
        const bool dark_sky_ok = (bg_med <= 0.12f) || (opts.gate_mode == ObserverGateMode::Force);

        std::vector<double> streak_vels;
        std::vector<double> diff_mags;
        std::vector<float> w_vals(n, 0.0f), s_vals(n, 0.0f);
        if (dark_sky_ok) {
            for (int y = 4; y < Hh - 4; y += 4) for (int x = 4; x < Wh - 4; x += 4) {
                const size_t i = static_cast<size_t>(y) * Wh + x;
                const float g0 = b.g_halfs[0][i];
                if (g0 < std::max(0.05f, bg_med + 0.035f) || g0 >= 0.90f) continue;
                bool had_track = false;
                for (size_t k = 1; k < n; ++k) {
                    if (b.mw_half[k][i] < 0.35f) {
                        const float gk = b.g_halfs[k][i];
                        if (gk > 0.05f && std::fabs(gk - g0) > 0.03f) {
                            const float disp = 2.0f * std::hypot(b.flows_fy[k][i], b.flows_fx[k][i]);
                            if (disp > 0.5f) {
                                const double dt = (has_real_t && ts[k] > ts[0]) ? (ts[k] - ts[0]) : static_cast<double>(k);
                                double v = disp / dt;
                                if (has_wcs && cal_clk) v *= static_cast<double>(opts.plate_scale_arcsec_px);
                                streak_vels.push_back(v);
                                diff_mags.push_back(std::fabs(gk - g0) / std::max(0.05f, g0));
                                had_track = true;
                            }
                        }
                    }
                }
                if (had_track) {
                    for (size_t k = 0; k < n; ++k) {
                        float sy1 = 0.0f, sx1 = 0.0f; bool ok = false;
                        detail::aligned_source_coords(b, fields, k, y, x, sy1, sx1, ok);
                        w_vals[k] = b.g_halfs[k][i];
                        s_vals[k] = ok ? heraw_suite::detail::raw_rgb(b, k, sy1, sx1)[1] : g0;
                    }
                    const auto cls = classify_astro_point_3hyp(w_vals, s_vals, 0.03f, 0.01f, 0.7f);
                    rep.astro_hyp_counts[static_cast<size_t>(cls)] += 1;
                }
            }
        }
        rep.streak_count = static_cast<int>(streak_vels.size());
        if (!dark_sky_ok) {
            stg.reason = "terrestrial non-dark-sky scene (celestial streak gating requires dark sky or --observer-gate force)";
        } else if (!streak_vels.empty()) {
            std::sort(streak_vels.begin(), streak_vels.end());
            rep.streak_ang_vel = static_cast<float>(streak_vels[streak_vels.size() / 2]);
            double dm2 = 0.0;
            for (double v : diff_mags) dm2 += v * v;
            rep.diff_photometry_rms = static_cast<float>(std::sqrt(dm2 / diff_mags.size()));
            stg.observable = (rep.streak_count >= 2 || opts.gate_mode == ObserverGateMode::Force);
            stg.measured = rep.streak_ang_vel;
            stg.confidence = std::min(1.0, static_cast<double>(rep.streak_count) / 16.0);
            stg.reason = "streak kinematics & differential photometry evaluated";
        } else {
            stg.reason = "no transient moving point/streak outliers detected";
        }
        rep.streak_gate = stg;
        rep.gates["streak_astrometry"] = stg;
    }
}

inline ObserverReport analyze_subject_observer(const BurstView& b, const FieldResult& fields,
                                             const ObserverOptions& opts,
                                             const ParallelRows& parallel_rows = {}) {
    heraw_suite::detail::validate(b);
    ObserverReport rep;
    rep.clock_source = heraw_meas::clock_source_name(opts.clock_prov.source);
    rep.clock_tick_num = opts.clock_prov.tick_num;
    rep.clock_tick_den = opts.clock_prov.tick_den;
    rep.clock_row_calibrated = opts.clock_prov.row_calibrated;
    if (opts.compute_roughness || opts.compute_veil || opts.compute_metalness || opts.compute_diffuse_intrinsic)
        rep.executed_observers.push_back("o1_radiometry");
    if (opts.compute_ac_light || opts.compute_phosphor || opts.compute_light_split)
        rep.executed_observers.push_back("o2_optical_lockin");
    if (opts.compute_kinematics)
        rep.executed_observers.push_back("o3_kinematics");
    if (opts.compute_schlieren)
        rep.executed_observers.push_back("o4_schlieren_bos");
    if (opts.compute_vibrometry || opts.compute_vital)
        rep.executed_observers.push_back("o5_vibrometry_vital");
    if (opts.compute_loca_sign) rep.executed_observers.push_back("o6_loca_sign");
    if (opts.compute_modal_wave) rep.executed_observers.push_back("o7_modal_wave");
    if (opts.compute_seeing) rep.executed_observers.push_back("o8_seeing");
    if (opts.compute_refraction) rep.executed_observers.push_back("o9_refraction");
    if (opts.compute_streak) rep.executed_observers.push_back("o10_streak_astrometry");
    run_p3_radiometry(b, fields, opts, rep, parallel_rows);
    run_optical_lockin(b, fields, opts, rep, parallel_rows);
    run_kinematics_and_self_stereo(b, fields, opts, rep, parallel_rows);
    run_helmholtz_schlieren(b, fields, opts, rep);
    run_vibrometry_and_vital(b, fields, opts, rep);
    run_extended_observers_o6_to_o10(b, fields, opts, rep);
    return rep;
}

inline bool write_observer_report_json(const std::string& path, const ObserverReport& rep) {
    std::ofstream out(heraw_fs::from_utf8(path), std::ios::binary);
    if (!out) return false;
    auto bstr = [](bool v) { return v ? "true" : "false"; };
    auto num = [](double v) -> std::string {
        if (!std::isfinite(v)) return "0";
        char b[64];
        std::snprintf(b, sizeof(b), "%.6g", v);
        std::string s(b);
        for (char& c : s) if (c == ',') c = '.';
        return s;
    };
    auto sarr = [](const std::vector<std::string>& v) -> std::string {
        std::string r = "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) r += ", ";
            r += "\"" + v[i] + "\"";
        }
        r += "]";
        return r;
    };
    out << "{\n  \"schema\": \"org.nijin.observer.v3\",\n"
        << "  \"version\": \"" NIJIN_VERSION "\",\n"
        << "  \"requested_observers\": " << sarr(rep.requested_observers) << ",\n"
        << "  \"executed_observers\": " << sarr(rep.executed_observers) << ",\n";
    out << "  \"p3_radiometry\": {\"observable\": " << bstr(rep.p3_gate.observable)
        << ", \"eigen_ratio\": " << num(rep.p3_eigen_ratio)
        << ", \"has_illuminant\": " << bstr(rep.has_illuminant)
        << ", \"illuminant_rgb\": [" << num(rep.illuminant_rgb[0]) << ", " << num(rep.illuminant_rgb[1]) << ", " << num(rep.illuminant_rgb[2]) << "]"
        << ", \"has_airlight\": " << bstr(rep.has_airlight)
        << ", \"airlight_rgb\": [" << num(rep.airlight_rgb[0]) << ", " << num(rep.airlight_rgb[1]) << ", " << num(rep.airlight_rgb[2]) << "]"
        << ", \"roughness_observable\": " << bstr(rep.roughness_gate.observable)
        << ", \"median_roughness_rad\": " << num(rep.median_roughness_rad)
        << ", \"angular_sweep_rad\": " << num(rep.angular_sweep_rad)
        << ", \"mean_metalness\": " << num(rep.mean_metalness)
        << ", \"mean_veil_transmission\": " << num(rep.mean_veil_transmission) << "},\n";
    out << "  \"optical_lockin\": {\"observable\": " << bstr(rep.lockin_gate.observable)
        << ", \"readout_ms\": " << num(rep.readout_ms_used)
        << ", \"readout_self_calibrated\": " << bstr(rep.readout_self_calibrated)
        << ", \"ac_freq_hz\": " << num(rep.ac_freq_hz)
        << ", \"ac_mod_depth\": " << num(rep.ac_mod_depth)
        << ", \"ac_light_fraction_mean\": " << num(rep.ac_light_fraction_mean)
        << ", \"chirp_active\": " << bstr(rep.chirp_active)
        << ", \"chirp_glrt_delta\": " << num(rep.chirp_glrt_delta)
        << ", \"chirp_f_dot_hz_s\": " << num(rep.chirp_f_dot_hz_s)
        << ", \"clock_source\": \"" << rep.clock_source << "\""
        << ", \"clock_tick_num\": " << rep.clock_tick_num
        << ", \"clock_tick_den\": " << rep.clock_tick_den
        << ", \"clock_row_calibrated\": " << bstr(rep.clock_row_calibrated)
        << ", \"clock_ambiguous\": " << bstr(rep.clock_ambiguous)
        << ", \"gs_dual_window_hz\": " << num(rep.gs_dual_window_hz)
        << ", \"astro_hyp_counts\": [" << rep.astro_hyp_counts[0] << ", " << rep.astro_hyp_counts[1] << ", " << rep.astro_hyp_counts[2] << "]"
        << ", \"phosphor_observable\": " << bstr(rep.phosphor_gate.observable)
        << ", \"phosphor_decay_ms\": " << num(rep.phosphor_decay_ms)
        << ", \"red_blue_phase_lag_deg\": " << num(rep.red_blue_phase_lag_deg) << "},\n";
    out << "  \"kinematics_se3\": {\"observable\": " << bstr(rep.kinematics_gate.observable)
        << ", \"moving_body_count\": " << rep.moving_body_count
        << ", \"rigid_self_stereo\": " << bstr(rep.rigid_self_stereo_observable)
        << ", \"min_ttc_s\": " << num(rep.min_ttc_s)
        << ", \"max_abs_vorticity_rad_s\": " << num(rep.max_abs_vorticity_rad_s)
        << ", \"median_nonrigid_strain_px\": " << num(rep.median_nonrigid_strain_px)
        << ", \"tachometry_observable\": " << bstr(rep.tachometry_gate.observable)
        << ", \"estimated_frame_dt_s\": " << num(rep.estimated_frame_dt_s)
        << ", \"velocity_diversity_ready\": " << bstr(rep.velocity_diversity_deblur_ready)
        << ", \"ballistic_observable\": " << bstr(rep.ballistic_gate.observable)
        << ", \"ballistic_distance_m\": " << num(rep.ballistic_distance_m)
        << ", \"ballistic_accel_1x_px_s2\": " << num(rep.ballistic_accel_1x_px_s2) << "},\n";
    out << "  \"schlieren_bos\": {\"observable\": " << bstr(rep.schlieren_gate.observable)
        << ", \"irrotational_ratio\": " << num(rep.schlieren_irrotational_ratio)
        << ", \"max_opd\": " << num(rep.max_schlieren_opd)
        << ", \"units\": \"" << rep.schlieren_units << "\"},\n";
    out << "  \"vibrometry_vital\": {\"vibrometry_observable\": " << bstr(rep.vibrometry_gate.observable)
        << ", \"modal_freq_hz\": " << num(rep.modal_freq_hz)
        << ", \"modal_max_amp_1x_px\": " << num(rep.modal_max_amp_1x_px)
        << ", \"modal_snr\": " << num(rep.modal_snr)
        << ", \"damping_observable\": " << bstr(rep.damping_observable)
        << ", \"modal_damping_zeta\": " << num(rep.modal_damping_zeta)
        << ", \"vital_observable\": " << bstr(rep.vital_gate.observable)
        << ", \"vital_bpm\": " << num(rep.vital_bpm)
        << ", \"vital_snr\": " << num(rep.vital_snr)
        << ", \"bcg_amp_1x_px\": " << num(rep.bcg_amp_1x_px) << "},\n";
    out << "  \"extended_o6_o10\": {\"loca_observable\": " << bstr(rep.loca_gate.observable)
        << ", \"loca_slope_px_per_rho\": " << num(rep.loca_slope_px_per_rho)
        << ", \"defocus_sign\": " << rep.defocus_sign
        << ", \"wave_observable\": " << bstr(rep.wave_gate.observable)
        << ", \"wave_phase_vel_px_s\": " << num(rep.wave_phase_vel_px_s)
        << ", \"seeing_observable\": " << bstr(rep.seeing_gate.observable)
        << ", \"seeing_r0_norm\": " << num(rep.seeing_r0_norm)
        << ", \"diff_tilt_rms_1x_px\": " << num(rep.diff_tilt_rms_1x_px)
        << ", \"refraction_observable\": " << bstr(rep.refraction_gate.observable)
        << ", \"refraction_rms_1x_px\": " << num(rep.refraction_rms_1x_px)
        << ", \"streak_observable\": " << bstr(rep.streak_gate.observable)
        << ", \"streak_count\": " << rep.streak_count
        << ", \"streak_ang_vel\": " << num(rep.streak_ang_vel)
        << ", \"streak_units\": \"" << rep.streak_units << "\""
        << ", \"diff_photometry_rms\": " << num(rep.diff_photometry_rms) << "}\n}\n";
    return static_cast<bool>(out);
}

} // namespace heraw_suite::observer
