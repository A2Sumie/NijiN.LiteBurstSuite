// heraw_suite_motion_shutter.hpp -- Dual-Motion, Rolling-Shutter SO(3), Concert Optoelectronics & Shutter Engine:
//   - R3: Photometric Gain+Offset Invariant SIM(2) Tracker with 6x6 Projection Rank Gate (I - P_D) W^{1/2} J_g
//   - R4: Perspective-Exact 8-Knot Cubic B-Spline Row-Time SO(3) Solver with Rank-Adaptive Order Reduction
//   - R6: Unmixed Single-Frame Tracking Feature + Row-Band Lucky Weights & Action-Peak Selector
//   - OkLab Constant-Hue Stage Highlight Compressor + LED-Wall Pitch Notch + Volumetric Beam Split
//   - F-C (C1 NNLS Temporal Envelope, C2 Co-Moving Panning Frame, C3 Trajectory MTF) + HM5 Jump Kinematics
// Pure 7-bit ASCII C++20 header. Relies on heraw_suite_spectral_solver.hpp dense linear helpers.
#pragma once

#include "heraw_suite_spectral_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace heraw_motion {

// ---------------------------------------------------------------------------
// R3: Photometric Gain+Offset Invariant SIM(2) Patch Tracker + Rank Check
// ---------------------------------------------------------------------------
struct Sim2TrackResult {
    bool tracked = false;
    bool sim2_rank_ok = false;
    bool fell_back_to_trans = false;
    float dy = 0.0f, dx = 0.0f;
    float dtheta_rad = 0.0f;
    float scale = 1.0f;
    float gamma = 1.0f;   // multiplicative illumination ratio
    float beta = 0.0f;    // additive stray-light offset
    float zncc = 0.0f;
    float min_geom_sv = 0.0f;
    int effective_dof = 0; // 4=SIM(2), 2=Translation, 0=Untracked
};

// Analyze a warped patch q = P_k(W(p)) against template p = P_0 with weights w and EIV variances:
// Fits q = gamma * p + beta * 1 + eps via weighted de-meaned linear regression and tests
// whether (I - P_D) W^{1/2} J_g has full rank 4 across [tx, ty, theta, log_s].
inline Sim2TrackResult evaluate_sim2_photometric_patch(
    int n_pts,
    const double* p_tpl,   // length n_pts
    const double* q_warp,  // length n_pts
    const double* gx_q,    // length n_pts (d q / d x)
    const double* gy_q,    // length n_pts (d q / d y)
    const double* x_rel,   // length n_pts (centered x coordinate)
    const double* y_rel,   // length n_pts (centered y coordinate)
    const double* w_pts,   // length n_pts
    bool allow_beta = true,
    double sv_min_thresh = 0.08
) {
    Sim2TrackResult res;
    if (n_pts < 9 || !p_tpl || !q_warp || !gx_q || !gy_q || !x_rel || !y_rel) return res;

    double sw = 0.0, sp = 0.0, sq = 0.0;
    for (int i = 0; i < n_pts; ++i) {
        const double w = w_pts ? std::max(0.0, w_pts[i]) : 1.0;
        sw += w; sp += w * p_tpl[i]; sq += w * q_warp[i];
    }
    if (sw <= 1e-12) return res;
    const double mp = sp / sw;
    const double mq = sq / sw;

    double pp = 0.0, pq = 0.0, qq = 0.0;
    for (int i = 0; i < n_pts; ++i) {
        const double w = w_pts ? std::max(0.0, w_pts[i]) : 1.0;
        const double pc = allow_beta ? (p_tpl[i] - mp) : p_tpl[i];
        const double qc = allow_beta ? (q_warp[i] - mq) : q_warp[i];
        pp += w * pc * pc;
        pq += w * pc * qc;
        qq += w * qc * qc;
    }
    if (pp <= 1e-12 || qq <= 1e-12) return res;

    res.zncc = static_cast<float>(pq / std::sqrt(pp * qq));
    const double gamma_raw = pq / pp;
    const double gamma_c = std::clamp(gamma_raw, 0.55, 1.80);
    res.gamma = static_cast<float>(gamma_c);
    res.beta = allow_beta ? static_cast<float>(mq - gamma_c * mp) : 0.0f;

    // Build illumination basis D = W^{1/2} [p, 1] (or [p] if !allow_beta) and geometric Jacobian
    // J_g = W^{1/2} [dq/dx, dq/dy, x dq/dy - y dq/dx, x dq/dx + y dq/dy]
    const int d_col = allow_beta ? 2 : 1;
    std::vector<double> DtD(static_cast<size_t>(d_col) * d_col, 0.0);
    std::vector<double> DtJ(static_cast<size_t>(d_col) * 4, 0.0);
    std::vector<double> JtJ(16, 0.0);
    std::vector<double> Dtq(static_cast<size_t>(d_col), 0.0);
    std::vector<double> Jtq(4, 0.0);

    for (int i = 0; i < n_pts; ++i) {
        const double w = w_pts ? std::max(0.0, w_pts[i]) : 1.0;
        const double g4[4] = {
            gx_q[i],
            gy_q[i],
            x_rel[i] * gy_q[i] - y_rel[i] * gx_q[i], // rotation d/dtheta
            x_rel[i] * gx_q[i] + y_rel[i] * gy_q[i]  // isotropic log-scale d/d(log s)
        };
        const double dvec[2] = {p_tpl[i], 1.0};
        for (int a = 0; a < d_col; ++a) {
            for (int b = 0; b < d_col; ++b) DtD[static_cast<size_t>(a) * d_col + b] += w * dvec[a] * dvec[b];
            for (int c = 0; c < 4; ++c) DtJ[static_cast<size_t>(a) * 4 + c] += w * dvec[a] * g4[c];
            Dtq[static_cast<size_t>(a)] += w * dvec[a] * q_warp[i];
        }
        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) JtJ[static_cast<size_t>(a) * 4 + b] += w * g4[a] * g4[b];
            Jtq[static_cast<size_t>(a)] += w * g4[a] * q_warp[i];
        }
    }

    // M_perp = J_g^T W^{1/2} (I - P_D) W^{1/2} J_g = JtJ - DtJ^T (DtD)^+ DtJ
    auto DtDp_DtJ = heraw_spec::sym_pinv_mul(DtD, d_col, DtJ, 4);
    std::vector<double> M_perp = JtJ;
    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            double sub = 0.0;
            for (int k = 0; k < d_col; ++k) {
                sub += DtJ[static_cast<size_t>(k) * 4 + a] * DtDp_DtJ[static_cast<size_t>(k) * 4 + b];
            }
            const double v = M_perp[static_cast<size_t>(a) * 4 + b] - sub;
            M_perp[static_cast<size_t>(a) * 4 + b] = (a == b) ? std::max(0.0, v) : v;
        }
    }
    for (int a = 0; a < 4; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            const double sym = 0.5 * (M_perp[static_cast<size_t>(a) * 4 + b] + M_perp[static_cast<size_t>(b) * 4 + a]);
            M_perp[static_cast<size_t>(a) * 4 + b] = M_perp[static_cast<size_t>(b) * 4 + a] = sym;
        }
    }
    std::vector<double> evals, Vtmp;
    heraw_spec::sym_jacobi_eigen(M_perp, 4, evals, Vtmp);
    const double max_sv = std::sqrt(std::max(1e-18, evals[3]));
    const double min_sv4 = std::sqrt(std::max(0.0, evals[0]));
    res.min_geom_sv = static_cast<float>(min_sv4 / std::max(1e-6, max_sv));

    // Check 2x2 translation sub-block rank separately
    std::vector<double> M_trans = {
        M_perp[0], M_perp[1],
        M_perp[4], M_perp[5]
    };
    std::vector<double> ev2, V2;
    heraw_spec::sym_jacobi_eigen(M_trans, 2, ev2, V2);
    const double min_sv2 = std::sqrt(std::max(0.0, ev2[0])) / std::max(1e-6, std::sqrt(std::max(1e-18, ev2[1])));

    auto DtDp_Dtq = heraw_spec::sym_pinv_mul(DtD, d_col, Dtq, 1);
    std::vector<double> rhs_perp(4, 0.0);
    for (int a = 0; a < 4; ++a) {
        double sub = 0.0;
        for (int k = 0; k < d_col; ++k) {
            sub += DtJ[static_cast<size_t>(k) * 4 + a] * DtDp_Dtq[static_cast<size_t>(k)];
        }
        rhs_perp[static_cast<size_t>(a)] = -(Jtq[static_cast<size_t>(a)] - sub);
    }

    res.sim2_rank_ok = (res.min_geom_sv >= sv_min_thresh) && (min_sv4 > 1e-6);
    double dg[4] = {0.0, 0.0, 0.0, 0.0};
    if (res.sim2_rank_ok) {
        res.tracked = true;
        res.effective_dof = 4;
        auto delta4 = heraw_spec::sym_pinv_mul(M_perp, 4, rhs_perp, 1, 1e-8);
        dg[0] = delta4[0];
        dg[1] = delta4[1];
        dg[2] = delta4[2];
        dg[3] = delta4[3];
        res.dx = static_cast<float>(dg[0]);
        res.dy = static_cast<float>(dg[1]);
        res.dtheta_rad = static_cast<float>(dg[2]);
        res.scale = static_cast<float>(std::exp(std::clamp(dg[3], -0.5, 0.5)));
    } else if (min_sv2 >= sv_min_thresh && ev2[0] > 1e-8) {
        res.tracked = true;
        res.fell_back_to_trans = true;
        res.effective_dof = 2;
        std::vector<double> rhs2 = {rhs_perp[0], rhs_perp[1]};
        auto delta2 = heraw_spec::sym_pinv_mul(M_trans, 2, rhs2, 1, 1e-8);
        dg[0] = delta2[0];
        dg[1] = delta2[1];
        res.dx = static_cast<float>(dg[0]);
        res.dy = static_cast<float>(dg[1]);
        res.dtheta_rad = 0.0f;
        res.scale = 1.0f;
    }

    if (res.tracked) {
        double sq_c = 0.0;
        for (int i = 0; i < n_pts; ++i) {
            const double w = w_pts ? std::max(0.0, w_pts[i]) : 1.0;
            const double qc = q_warp[i]
                + gx_q[i] * dg[0]
                + gy_q[i] * dg[1]
                + (x_rel[i] * gy_q[i] - y_rel[i] * gx_q[i]) * dg[2]
                + (x_rel[i] * gx_q[i] + y_rel[i] * gy_q[i]) * dg[3];
            sq_c += w * qc;
        }
        const double mq_c = sq_c / sw;
        double pq_c = 0.0, qq_c = 0.0;
        for (int i = 0; i < n_pts; ++i) {
            const double w = w_pts ? std::max(0.0, w_pts[i]) : 1.0;
            const double qc_raw = q_warp[i]
                + gx_q[i] * dg[0]
                + gy_q[i] * dg[1]
                + (x_rel[i] * gy_q[i] - y_rel[i] * gx_q[i]) * dg[2]
                + (x_rel[i] * gx_q[i] + y_rel[i] * gy_q[i]) * dg[3];
            const double pc = allow_beta ? (p_tpl[i] - mp) : p_tpl[i];
            const double qc = allow_beta ? (qc_raw - mq_c) : qc_raw;
            pq_c += w * pc * qc;
            qq_c += w * qc * qc;
        }
        if (qq_c > 1e-12) {
            res.zncc = static_cast<float>(pq_c / std::sqrt(pp * qq_c));
            const double g_lin = std::clamp(pq_c / pp, 0.55, 1.80);
            res.gamma = static_cast<float>(g_lin);
            res.beta = allow_beta ? static_cast<float>(mq_c - g_lin * mp) : 0.0f;
        }
    }
    return res;
}

// ---------------------------------------------------------------------------
// R4: Perspective-Exact Row-Time Cubic B-Spline SO(3) Solver
// ---------------------------------------------------------------------------
struct RsSplineSample {
    double y_ref = 0.0, x_ref = 0.0;
    double dy_obs = 0.0, dx_obs = 0.0; // displacement: p_src - p_ref
    double t_norm_src = 0.0;           // row time normalized to [0, 1] inside frame k
    double weight = 1.0;
    double depth_inv = 0.0;            // 1/Z_i if parallax known, else 0 for pure distant rotation
};

struct RsSplineFitResult {
    int requested_knots = 8;
    int used_knots = 1;
    int numerical_rank = 0;
    bool converged = false;
    double rmse_px = 0.0;
    double holdout_rmse_px = 0.0;
    std::vector<std::array<double, 3>> knot_rot_rad; // used_knots x [wx, wy, wz]
    std::array<double, 3> trans_epip = {0.0, 0.0, 0.0};
};

// Perspective pinhole projection pi([X, Y, Z]^T) = (X/Z, Y/Z) with K = [f 0 cx; 0 f cy; 0 0 1].
inline void project_pinhole(double f_px, double cy, double cx,
                            const double ray_c[3], double& y_px, double& x_px) noexcept {
    const double iz = 1.0 / std::max(1e-6, ray_c[2]);
    x_px = f_px * (ray_c[0] * iz) + cx;
    y_px = f_px * (ray_c[1] * iz) + cy;
}

// Small-angle SO(3) Rodrigues matrix R(theta).
inline void rodrigues_so3(const double w[3], double R[3][3]) noexcept {
    const double th = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    if (th < 1e-10) {
        R[0][0] = 1.0;   R[0][1] = -w[2]; R[0][2] =  w[1];
        R[1][0] =  w[2]; R[1][1] = 1.0;   R[1][2] = -w[0];
        R[2][0] = -w[1]; R[2][1] =  w[0]; R[2][2] = 1.0;
        return;
    }
    const double ax = w[0] / th, ay = w[1] / th, az = w[2] / th;
    const double c = std::cos(th), s = std::sin(th), oc = 1.0 - c;
    R[0][0] = c + ax * ax * oc;      R[0][1] = ax * ay * oc - az * s; R[0][2] = ax * az * oc + ay * s;
    R[1][0] = ay * ax * oc + az * s; R[1][1] = c + ay * ay * oc;      R[1][2] = ay * az * oc - ax * s;
    R[2][0] = az * ax * oc - ay * s; R[2][1] = az * ay * oc + ax * s; R[2][2] = c + az * az * oc;
}

// Uniform cubic B-spline basis weights sum_j B_j(t) == 1 across n_knots in [0, 1].
inline void eval_temporal_knots(double t01, int n_knots, std::vector<double>& b_w) {
    const int nk = std::max(1, n_knots);
    b_w.assign(static_cast<size_t>(nk), 0.0);
    if (nk <= 1) { b_w[0] = 1.0; return; }
    const double tc = std::clamp(t01, 0.0, 1.0) * static_cast<double>(nk - 1);
    double wsum = 0.0;
    for (int j = 0; j < nk; ++j) {
        const double d = std::fabs(tc - static_cast<double>(j));
        double w = 0.0;
        if (d < 1.0) {
            w = (2.0 / 3.0) - d * d + 0.5 * d * d * d;
        } else if (d < 2.0) {
            const double t = 2.0 - d;
            w = (1.0 / 6.0) * t * t * t;
        }
        b_w[static_cast<size_t>(j)] = w;
        wsum += w;
    }
    if (wsum > 1e-12) {
        const double inv = 1.0 / wsum;
        for (double& w : b_w) w *= inv;
    } else {
        b_w[0] = 1.0;
    }
}

// Forward perspective evaluation of a fitted SO(3) + epipolar rolling-shutter spline model.
inline void eval_perspective_rs_flow(
    const RsSplineFitResult& fit,
    double f_px, double cy, double cx,
    double y_ref, double x_ref,
    double t_norm_src, double depth_inv,
    double& dy_out, double& dx_out
) {
    dy_out = 0.0;
    dx_out = 0.0;
    if (fit.used_knots < 1 || fit.knot_rot_rad.empty() || !(f_px > 1.0)) return;
    std::vector<double> bw;
    eval_temporal_knots(t_norm_src, fit.used_knots, bw);
    double w_vec[3] = {0.0, 0.0, 0.0};
    for (int j = 0; j < fit.used_knots; ++j) {
        w_vec[0] += bw[static_cast<size_t>(j)] * fit.knot_rot_rad[static_cast<size_t>(j)][0];
        w_vec[1] += bw[static_cast<size_t>(j)] * fit.knot_rot_rad[static_cast<size_t>(j)][1];
        w_vec[2] += bw[static_cast<size_t>(j)] * fit.knot_rot_rad[static_cast<size_t>(j)][2];
    }
    double R[3][3];
    rodrigues_so3(w_vec, R);
    const double b0[3] = {(x_ref - cx) / f_px, (y_ref - cy) / f_px, 1.0};
    const double Rb[3] = {
        R[0][0] * b0[0] + R[0][1] * b0[1] + R[0][2] * b0[2] + depth_inv * fit.trans_epip[0],
        R[1][0] * b0[0] + R[1][1] * b0[1] + R[1][2] * b0[2] + depth_inv * fit.trans_epip[1],
        R[2][0] * b0[0] + R[2][1] * b0[1] + R[2][2] * b0[2] + depth_inv * fit.trans_epip[2]
    };
    double py = y_ref, px = x_ref;
    project_pinhole(f_px, cy, cx, Rb, py, px);
    dy_out = py - y_ref;
    dx_out = px - x_ref;
}

// Solve perspective-exact SO(3) rolling-shutter model using row times t_norm_src and rank-adaptive knot count.
inline RsSplineFitResult fit_perspective_rs_spline(
    const std::vector<RsSplineSample>& samples,
    double f_px, double cy, double cx,
    int max_knots = 8,
    double rank_rtol = 1e-4,
    double prior_c1_smooth = 1e-4
) {
    RsSplineFitResult out;
    out.requested_knots = max_knots;
    if (samples.size() < 4 || !(f_px > 1.0)) return out;

    const std::array<int, 3> ladder = {std::max(1, max_knots), std::min(4, max_knots), 1};
    int chosen_knots = 1;
    std::vector<double> bw;

    // Rank audit against actual geometric ray layout & row timings (R4)
    for (int k_cand : ladder) {
        if (k_cand > max_knots) continue;
        const int dim = k_cand * 3;
        std::vector<double> H(static_cast<size_t>(dim) * dim, 0.0);
        for (const auto& s : samples) {
            eval_temporal_knots(s.t_norm_src, k_cand, bw);
            const double bx = (s.x_ref - cx) / f_px;
            const double by = (s.y_ref - cy) / f_px;
            // Exact pinhole ray rotation Jacobian G_i = D_pi(K * b) * K * (-[b]_x), [2 x 3]
            // Rows are dx/dw and dy/dw at b = [bx, by, 1]^T:
            const double Gx[3] = {-f_px * bx * by,  f_px * (1.0 + bx * bx), -f_px * by};
            const double Gy[3] = {-f_px * (1.0 + by * by), f_px * bx * by,  f_px * bx};
            for (int ja = 0; ja < k_cand; ++ja) {
                if (bw[ja] == 0.0) continue;
                for (int jb = 0; jb < k_cand; ++jb) {
                    if (bw[jb] == 0.0) continue;
                    const double ww = s.weight * bw[ja] * bw[jb];
                    for (int a = 0; a < 3; ++a) {
                        for (int b = 0; b < 3; ++b) {
                            H[static_cast<size_t>(ja * 3 + a) * dim + (jb * 3 + b)] +=
                                ww * (Gx[a] * Gx[b] + Gy[a] * Gy[b]);
                        }
                    }
                }
            }
        }
        std::vector<double> ev, Vtmp;
        heraw_spec::sym_jacobi_eigen(H, dim, ev, Vtmp);
        const double max_e = std::max(1e-18, ev.back());
        int rnk = 0;
        for (double v : ev) if (v > rank_rtol * max_e) ++rnk;
        out.numerical_rank = rnk;
        if (rnk == dim || k_cand == 1) {
            chosen_knots = k_cand;
            break;
        }
    }

    out.used_knots = chosen_knots;
    out.knot_rot_rad.assign(static_cast<size_t>(chosen_knots), {0.0, 0.0, 0.0});
    const int dim = chosen_knots * 3;
    int parallax_cnt = 0;
    double dmin_all = 1e30, dmax_all = 0.0;
    for (const auto& s : samples) {
        if (s.depth_inv > 1e-4) {
            ++parallax_cnt;
            dmin_all = std::min(dmin_all, s.depth_inv);
            dmax_all = std::max(dmax_all, s.depth_inv);
        } else {
            dmin_all = 0.0;
        }
    }
    const bool solve_joint_epip = (parallax_cnt >= 4) && ((dmax_all - dmin_all) > 1e-3);
    const int dim_tot = dim + (solve_joint_epip ? 3 : 0);
    std::vector<double> theta(static_cast<size_t>(dim_tot), 0.0);

    for (int iter = 0; iter < 8; ++iter) {
        std::vector<double> H(static_cast<size_t>(dim_tot) * dim_tot, 0.0);
        std::vector<double> rhs(static_cast<size_t>(dim_tot), 0.0);
        std::vector<double> rx_row(static_cast<size_t>(dim_tot), 0.0);
        std::vector<double> ry_row(static_cast<size_t>(dim_tot), 0.0);
        const double T_cur[3] = {
            solve_joint_epip ? theta[static_cast<size_t>(dim + 0)] : 0.0,
            solve_joint_epip ? theta[static_cast<size_t>(dim + 1)] : 0.0,
            solve_joint_epip ? theta[static_cast<size_t>(dim + 2)] : 0.0
        };
        double rss = 0.0, sw = 0.0;
        for (const auto& s : samples) {
            eval_temporal_knots(s.t_norm_src, chosen_knots, bw);
            double w_loc[3] = {0.0, 0.0, 0.0};
            for (int j = 0; j < chosen_knots; ++j) {
                w_loc[0] += bw[j] * theta[static_cast<size_t>(j * 3 + 0)];
                w_loc[1] += bw[j] * theta[static_cast<size_t>(j * 3 + 1)];
                w_loc[2] += bw[j] * theta[static_cast<size_t>(j * 3 + 2)];
            }
            double R[3][3];
            rodrigues_so3(w_loc, R);
            const double b[3] = {(s.x_ref - cx) / f_px, (s.y_ref - cy) / f_px, 1.0};
            const double Rb0[3] = {
                R[0][0] * b[0] + R[0][1] * b[1] + R[0][2] * b[2],
                R[1][0] * b[0] + R[1][1] * b[1] + R[1][2] * b[2],
                R[2][0] * b[0] + R[2][1] * b[1] + R[2][2] * b[2]
            };
            const double dinv = (solve_joint_epip && s.depth_inv > 1e-4) ? s.depth_inv : 0.0;
            const double P[3] = {
                Rb0[0] + dinv * T_cur[0],
                Rb0[1] + dinv * T_cur[1],
                Rb0[2] + dinv * T_cur[2]
            };
            double py = 0.0, px = 0.0;
            project_pinhole(f_px, cy, cx, P, py, px);
            const double err_x = (s.x_ref + s.dx_obs) - px;
            const double err_y = (s.y_ref + s.dy_obs) - py;
            rss += s.weight * (err_x * err_x + err_y * err_y);
            sw += s.weight;

            const double iz = 1.0 / std::max(1e-6, P[2]);
            const double ix = P[0] * iz;
            const double iy = P[1] * iz;
            const double c0 = f_px * iz;
            // Exact analytical Jacobian d(pi(K (R b + dinv*T)))/d(omega_left) = [f*iz 0 -f*ix*iz; 0 f*iz -f*iy*iz] * (-[Rb0]_x)
            const double Jx[3] = {
                c0 * (-ix * Rb0[1]),
                c0 * (Rb0[2] + ix * Rb0[0]),
                c0 * (-Rb0[1])
            };
            const double Jy[3] = {
                c0 * (-Rb0[2] - iy * Rb0[1]),
                c0 * (iy * Rb0[0]),
                c0 * (Rb0[0])
            };
            std::fill(rx_row.begin(), rx_row.end(), 0.0);
            std::fill(ry_row.begin(), ry_row.end(), 0.0);
            for (int ja = 0; ja < chosen_knots; ++ja) {
                if (bw[ja] == 0.0) continue;
                for (int a = 0; a < 3; ++a) {
                    rx_row[static_cast<size_t>(ja * 3 + a)] = bw[ja] * Jx[a];
                    ry_row[static_cast<size_t>(ja * 3 + a)] = bw[ja] * Jy[a];
                }
            }
            if (dinv > 0.0) {
                rx_row[static_cast<size_t>(dim + 0)] = dinv * c0;
                rx_row[static_cast<size_t>(dim + 1)] = 0.0;
                rx_row[static_cast<size_t>(dim + 2)] = -dinv * c0 * ix;
                ry_row[static_cast<size_t>(dim + 0)] = 0.0;
                ry_row[static_cast<size_t>(dim + 1)] = dinv * c0;
                ry_row[static_cast<size_t>(dim + 2)] = -dinv * c0 * iy;
            }
            for (int u = 0; u < dim_tot; ++u) {
                const double rxu = rx_row[static_cast<size_t>(u)];
                const double ryu = ry_row[static_cast<size_t>(u)];
                if (rxu == 0.0 && ryu == 0.0) continue;
                rhs[static_cast<size_t>(u)] += s.weight * (rxu * err_x + ryu * err_y);
                for (int v = u; v < dim_tot; ++v) {
                    const double rxv = rx_row[static_cast<size_t>(v)];
                    const double ryv = ry_row[static_cast<size_t>(v)];
                    if (rxv == 0.0 && ryv == 0.0) continue;
                    const double hv = s.weight * (rxu * rxv + ryu * ryv);
                    H[static_cast<size_t>(u) * dim_tot + v] += hv;
                    if (u != v) H[static_cast<size_t>(v) * dim_tot + u] += hv;
                }
            }
        }
        out.rmse_px = std::sqrt(rss / std::max(1e-12, sw));
        // C^1 2nd-difference temporal smoothness prior across consecutive knots
        if (chosen_knots >= 3 && prior_c1_smooth > 0.0) {
            for (int j = 1; j < chosen_knots - 1; ++j) {
                for (int a = 0; a < 3; ++a) {
                    const int i_m = (j - 1) * 3 + a, i_c = j * 3 + a, i_p = (j + 1) * 3 + a;
                    const double d2 = theta[i_m] - 2.0 * theta[i_c] + theta[i_p];
                    const double lam = prior_c1_smooth * sw;
                    H[static_cast<size_t>(i_m) * dim_tot + i_m] += lam;
                    H[static_cast<size_t>(i_c) * dim_tot + i_c] += 4.0 * lam;
                    H[static_cast<size_t>(i_p) * dim_tot + i_p] += lam;
                    H[static_cast<size_t>(i_m) * dim_tot + i_c] -= 2.0 * lam; H[static_cast<size_t>(i_c) * dim_tot + i_m] -= 2.0 * lam;
                    H[static_cast<size_t>(i_p) * dim_tot + i_c] -= 2.0 * lam; H[static_cast<size_t>(i_c) * dim_tot + i_p] -= 2.0 * lam;
                    H[static_cast<size_t>(i_m) * dim_tot + i_p] += lam;       H[static_cast<size_t>(i_p) * dim_tot + i_m] += lam;
                    rhs[i_m] -= lam * d2;
                    rhs[i_c] += 2.0 * lam * d2;
                    rhs[i_p] -= lam * d2;
                }
            }
        }
        if (solve_joint_epip) {
            const double t_reg = 1e-9 * std::max(1.0, sw);
            for (int a = 0; a < 3; ++a) {
                H[static_cast<size_t>(dim + a) * dim_tot + (dim + a)] += t_reg;
            }
        }
        auto delta = heraw_spec::sym_pinv_mul(H, dim_tot, rhs, 1, 1e-9);
        double step_norm = 0.0;
        for (int i = 0; i < dim_tot; ++i) {
            theta[static_cast<size_t>(i)] += std::clamp(delta[static_cast<size_t>(i)], -0.1, 0.1);
            step_norm += delta[static_cast<size_t>(i)] * delta[static_cast<size_t>(i)];
        }
        if (step_norm < 1e-12) { out.converged = true; break; }
    }
    for (int j = 0; j < chosen_knots; ++j) {
        out.knot_rot_rad[static_cast<size_t>(j)] = {
            theta[static_cast<size_t>(j * 3 + 0)],
            theta[static_cast<size_t>(j * 3 + 1)],
            theta[static_cast<size_t>(j * 3 + 2)]
        };
    }
    if (solve_joint_epip) {
        out.trans_epip = {
            theta[static_cast<size_t>(dim + 0)],
            theta[static_cast<size_t>(dim + 1)],
            theta[static_cast<size_t>(dim + 2)]
        };
    } else if (parallax_cnt >= 4) {
        // Fallback translational epipole estimate when depth_inv is nonzero yet nearly constant
        double GtG[9] = {}, Gtr[3] = {};
        for (const auto& s : samples) {
            if (!(s.depth_inv > 1e-4)) continue;
            eval_temporal_knots(s.t_norm_src, chosen_knots, bw);
            double w_loc[3] = {0.0, 0.0, 0.0};
            for (int j = 0; j < chosen_knots; ++j) {
                w_loc[0] += bw[j] * theta[static_cast<size_t>(j * 3 + 0)];
                w_loc[1] += bw[j] * theta[static_cast<size_t>(j * 3 + 1)];
                w_loc[2] += bw[j] * theta[static_cast<size_t>(j * 3 + 2)];
            }
            double R[3][3];
            rodrigues_so3(w_loc, R);
            const double bx = (s.x_ref - cx) / f_px, by = (s.y_ref - cy) / f_px;
            const double ray[3] = {bx, by, 1.0};
            const double Rb[3] = {
                R[0][0] * ray[0] + R[0][1] * ray[1] + R[0][2] * ray[2],
                R[1][0] * ray[0] + R[1][1] * ray[1] + R[1][2] * ray[2],
                R[2][0] * ray[0] + R[2][1] * ray[1] + R[2][2] * ray[2]
            };
            double py = 0.0, px = 0.0;
            project_pinhole(f_px, cy, cx, Rb, py, px);
            const double rx = (s.x_ref + s.dx_obs) - px;
            const double ry = (s.y_ref + s.dy_obs) - py;
            const double kz = s.depth_inv * f_px;
            const double jx[3] = {kz, 0.0, -kz * bx};
            const double jy[3] = {0.0, kz, -kz * by};
            for (int a = 0; a < 3; ++a) {
                Gtr[a] += s.weight * (jx[a] * rx + jy[a] * ry);
                for (int b_i = 0; b_i < 3; ++b_i) {
                    GtG[a * 3 + b_i] += s.weight * (jx[a] * jx[b_i] + jy[a] * jy[b_i]);
                }
            }
        }
        std::vector<double> H3(GtG, GtG + 9), rhs3(Gtr, Gtr + 3);
        auto tsol = heraw_spec::sym_pinv_mul(H3, 3, rhs3, 1, 1e-9);
        out.trans_epip = {tsol[0], tsol[1], tsol[2]};
    }
    double final_rss = 0.0, final_sw = 0.0;
    for (const auto& s : samples) {
        eval_temporal_knots(s.t_norm_src, chosen_knots, bw);
        double w_loc[3] = {0.0, 0.0, 0.0};
        for (int j = 0; j < chosen_knots; ++j) {
            w_loc[0] += bw[j] * theta[static_cast<size_t>(j * 3 + 0)];
            w_loc[1] += bw[j] * theta[static_cast<size_t>(j * 3 + 1)];
            w_loc[2] += bw[j] * theta[static_cast<size_t>(j * 3 + 2)];
        }
        double R[3][3];
        rodrigues_so3(w_loc, R);
        const double b[3] = {(s.x_ref - cx) / f_px, (s.y_ref - cy) / f_px, 1.0};
        const double Rb0[3] = {
            R[0][0] * b[0] + R[0][1] * b[1] + R[0][2] * b[2],
            R[1][0] * b[0] + R[1][1] * b[1] + R[1][2] * b[2],
            R[2][0] * b[0] + R[2][1] * b[1] + R[2][2] * b[2]
        };
        const double dinv = (solve_joint_epip && s.depth_inv > 1e-4) ? s.depth_inv : 0.0;
        const double P[3] = {
            Rb0[0] + dinv * out.trans_epip[0],
            Rb0[1] + dinv * out.trans_epip[1],
            Rb0[2] + dinv * out.trans_epip[2]
        };
        double py = 0.0, px = 0.0;
        project_pinhole(f_px, cy, cx, P, py, px);
        const double ex = (s.x_ref + s.dx_obs) - px;
        const double ey = (s.y_ref + s.dy_obs) - py;
        final_rss += s.weight * (ex * ex + ey * ey);
        final_sw += s.weight;
    }
    out.rmse_px = std::sqrt(final_rss / std::max(1e-12, final_sw));
    return out;
}

// ---------------------------------------------------------------------------
// R6: Row-Band Lucky Weights & Static-Horizon Unmixed Flicker Illumination Norm
// ---------------------------------------------------------------------------
// Compute per-band [n_frames x n_bands] sharpness lucky weights (relieves uniform whole-frame penalty).
inline std::vector<float> compute_rowband_lucky_weights(
    const std::vector<std::vector<float>>& g_halfs,
    int Hh, int Wh, int n_bands, float base_noise_var
) {
    const int N = static_cast<int>(g_halfs.size());
    const int nb = std::max(1, n_bands);
    std::vector<float> weights(static_cast<size_t>(N) * nb, 1.0f);
    if (N <= 1 || Hh < 8 || Wh < 8) return weights;
    for (int b = 0; b < nb; ++b) {
        const int r0 = (Hh * b) / nb;
        const int r1 = std::min(Hh - 1, (Hh * (b + 1)) / nb);
        std::vector<double> bscore(static_cast<size_t>(N), 0.0);
        for (int n = 0; n < N; ++n) {
            double acc = 0.0;
            size_t cnt = 0;
            for (int r = std::max(1, r0); r < r1; r += 2) {
                for (int c = 1; c < Wh - 1; c += 2) {
                    const size_t i = static_cast<size_t>(r) * Wh + c;
                    const float gx = g_halfs[n][i + 1] - g_halfs[n][i - 1];
                    const float gy = g_halfs[n][i + Wh] - g_halfs[n][i - Wh];
                    acc += std::max(0.0, static_cast<double>(gx * gx + gy * gy) - 2.0 * static_cast<double>(base_noise_var));
                    ++cnt;
                }
            }
            bscore[static_cast<size_t>(n)] = acc / std::max<size_t>(1u, cnt);
        }
        std::vector<double> tmp = bscore;
        std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
        const double med = std::max(1e-12, tmp[tmp.size() / 2]);
        for (int n = 0; n < N; ++n) {
            const double sr = bscore[static_cast<size_t>(n)] / med;
            const float w = (sr < 0.96) ? std::clamp(static_cast<float>(std::pow(sr, 3.0)), 0.15f, 1.0f) : 1.0f;
            weights[static_cast<size_t>(n) * nb + b] = w;
        }
    }
    return weights;
}

// ---------------------------------------------------------------------------
// Concert Optoelectronics: OkLab Constant-Hue Stage Highlight Compression
// ---------------------------------------------------------------------------
// Compresses highlights along perceptual L while preserving OkLab hue angle h = atan2(b, a)
// and gently rolling chroma inside target sRGB or AdobeRGB gamut (prevents neutral white bleaching of LEDs).
inline void oklab_stage_highlight_compress_pixel(
    const float cam_to_out[3][3],
    float lin[3],
    float knee = 0.78f,
    bool is_adobe_rgb = false
) noexcept {
    // 1) Transform camera RGB -> calibrated linear target space via cam_to_out
    float rgb[3] = {
        cam_to_out[0][0] * lin[0] + cam_to_out[0][1] * lin[1] + cam_to_out[0][2] * lin[2],
        cam_to_out[1][0] * lin[0] + cam_to_out[1][1] * lin[1] + cam_to_out[1][2] * lin[2],
        cam_to_out[2][0] * lin[0] + cam_to_out[2][1] * lin[1] + cam_to_out[2][2] * lin[2]
    };
    const float peak = std::max(rgb[0], std::max(rgb[1], rgb[2]));
    if (!(peak > knee)) return;

    const float t_range = std::max(1e-4f, 1.0f - knee);
    const float comp_peak = knee + t_range * std::tanh((peak - knee) / t_range);
    const float peak_ratio = std::clamp(comp_peak / std::max(1e-6f, peak), 0.0f, 1.0f);

    // 2) Convert target space -> linear sRGB for exact OkLab LMS coordinates
    auto clamp0 = [](float v) { return (v > 0.0f) ? v : 0.0f; };
    const float r0 = clamp0(rgb[0]), g0 = clamp0(rgb[1]), b0 = clamp0(rgb[2]);
    const float rs = is_adobe_rgb ? (1.398355f * r0 - 0.398355f * g0) : r0;
    const float gs = g0;
    const float bs = is_adobe_rgb ? (-0.0429389f * g0 + 1.0429389f * b0) : b0;

    const float l = 0.4122214708f * rs + 0.5363325363f * gs + 0.0514459929f * bs;
    const float m = 0.2119034982f * rs + 0.6806995451f * gs + 0.1073969566f * bs;
    const float s = 0.0883024619f * rs + 0.2817188376f * gs + 0.6299787005f * bs;
    const float l_ = std::cbrt(l), m_ = std::cbrt(m), s_ = std::cbrt(s);
    const float L  = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    const float a  = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    const float b_ = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    const float L_comp = std::min(1.0f, L * std::cbrt(peak_ratio));
    auto eval_work_rgb = [&](float c_sc, float& ro, float& go, float& bo) noexcept {
        const float a2 = a * c_sc;
        const float b2 = b_ * c_sc;
        const float l_c = L_comp + 0.3963377774f * a2 + 0.2158037573f * b2;
        const float m_c = L_comp - 0.1055613458f * a2 - 0.0638541728f * b2;
        const float s_c = L_comp - 0.0894841775f * a2 - 1.2914855480f * b2;
        const float L3 = l_c * l_c * l_c, M3 = m_c * m_c * m_c, S3 = s_c * s_c * s_c;
        const float xs = +4.0767416621f * L3 - 3.3077115913f * M3 + 0.2309699292f * S3;
        const float ys = -1.2684380046f * L3 + 2.6097574011f * M3 - 0.3413193965f * S3;
        const float zs = -0.0041960863f * L3 - 0.7034186147f * M3 + 1.7076147010f * S3;
        if (is_adobe_rgb) {
            ro = 0.715126f * xs + 0.284874f * ys;
            go = ys;
            bo = 0.041171f * ys + 0.958829f * zs;
        } else {
            ro = xs;
            go = ys;
            bo = zs;
        }
    };

    float sc_max = (peak > 1.0f) ? std::clamp(1.0f / std::pow(peak, 0.35f), 0.35f, 1.0f) : 1.0f;
    float ro = 0.0f, go = 0.0f, bo = 0.0f;
    eval_work_rgb(sc_max, ro, go, bo);
    if (std::min(ro, std::min(go, bo)) < 0.0f || std::max(ro, std::max(go, bo)) > 1.0f) {
        // Exact constant-hue gamut mapping along OkLab (a, b_) ray: zero primary hue twist
        float lo_s = 0.0f, hi_s = sc_max;
        for (int it = 0; it < 12; ++it) {
            const float mid_s = 0.5f * (lo_s + hi_s);
            float tr = 0.0f, tg = 0.0f, tb = 0.0f;
            eval_work_rgb(mid_s, tr, tg, tb);
            if (std::min(tr, std::min(tg, tb)) >= -1e-6f && std::max(tr, std::max(tg, tb)) <= 1.0f + 1e-6f) {
                lo_s = mid_s;
            } else {
                hi_s = mid_s;
            }
        }
        eval_work_rgb(lo_s, ro, go, bo);
    }
    lin[0] = std::clamp(ro, 0.0f, 1.0f);
    lin[1] = std::clamp(go, 0.0f, 1.0f);
    lin[2] = std::clamp(bo, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// F-C Computational Shutter (C1 NNLS, C2 Co-Moving Frame, C3 MTF) & HM5 Kinematics
// ---------------------------------------------------------------------------
// C1: Solve min_{a >= 0, sum a == 1} ||W_mat a - q||^2 + lambda sum a_k^2 V_k
inline std::vector<double> solve_nnls_temporal_envelope(
    const std::vector<std::vector<double>>& window_samples, // [N_frames][T_bins] normalized exposure windows
    const std::vector<double>& target_q,                    // [T_bins] desired temporal profile
    const std::vector<double>& noise_vars,                  // [N_frames] per-frame variance V_k
    double lambda_reg = 1e-4
) {
    const int N = static_cast<int>(window_samples.size());
    if (N <= 0) return {};
    const int T = static_cast<int>(target_q.size());
    std::vector<double> a(static_cast<size_t>(N), 1.0 / static_cast<double>(N));
    std::vector<double> H(static_cast<size_t>(N) * N, 0.0);
    std::vector<double> rhs(static_cast<size_t>(N), 0.0);
    for (int i = 0; i < N; ++i) {
        for (int t = 0; t < T && t < static_cast<int>(window_samples[i].size()); ++t) {
            rhs[i] += window_samples[i][t] * target_q[t];
            for (int j = 0; j < N; ++j) {
                if (t < static_cast<int>(window_samples[j].size()))
                    H[static_cast<size_t>(i) * N + j] += window_samples[i][t] * window_samples[j][t];
            }
        }
        const double vk = (i < static_cast<int>(noise_vars.size())) ? noise_vars[i] : 1.0;
        H[static_cast<size_t>(i) * N + i] += lambda_reg * std::max(0.0, vk);
    }
    double max_eig = 1.0;
    for (int i = 0; i < N; ++i) max_eig += H[static_cast<size_t>(i) * N + i];
    const double step = 1.0 / std::max(1.0, max_eig);
    auto project_simplex = [](std::vector<double>& v) {
        std::vector<double> u = v;
        std::sort(u.begin(), u.end(), std::greater<double>());
        double cssv = 0.0, theta = 0.0;
        int rho = -1;
        for (size_t j = 0; j < u.size(); ++j) {
            cssv += u[j];
            double t = (cssv - 1.0) / static_cast<double>(j + 1);
            if (u[j] - t > 0.0) rho = static_cast<int>(j);
        }
        cssv = 0.0;
        for (int j = 0; j <= rho; ++j) cssv += u[static_cast<size_t>(j)];
        theta = (cssv - 1.0) / static_cast<double>(rho + 1);
        for (double& x : v) x = std::max(0.0, x - theta);
    };
    for (int it = 0; it < 200; ++it) {
        std::vector<double> grad(static_cast<size_t>(N), 0.0);
        for (int i = 0; i < N; ++i) {
            double ha = 0.0;
            for (int j = 0; j < N; ++j) ha += H[static_cast<size_t>(i) * N + j] * a[j];
            grad[i] = ha - rhs[i];
        }
        for (int i = 0; i < N; ++i) a[i] -= step * grad[i];
        project_simplex(a);
    }
    return a;
}

// HM5 Ballistic Jump Height (Astra Cross-Section #5 exact general height formula):
// Given hang duration T_s = t_down - t_up, optional COM level change dy_m = y_land - y_takeoff:
//   h_max = (g * T / 2 + dy / T)^2 / (2 * g)   [strictly g*T^2/8 when dy == 0]
struct JumpKinematicsResult {
    double hang_time_s = 0.0;
    double equal_level_height_m = 0.0;
    double asymmetric_height_m = 0.0;
    double height_sigma_m = 0.0;
};

inline JumpKinematicsResult compute_ballistic_jump_height(
    double t_hang_s,
    double dy_land_minus_takeoff_m = 0.0,
    double sigma_t_s = 0.008
) noexcept {
    constexpr double kG = 9.80665;
    JumpKinematicsResult out;
    out.hang_time_s = std::max(0.0, t_hang_s);
    if (out.hang_time_s <= 1e-6) return out;
    out.equal_level_height_m = (kG * out.hang_time_s * out.hang_time_s) / 8.0;
    const double v0 = 0.5 * kG * out.hang_time_s + (dy_land_minus_takeoff_m / out.hang_time_s);
    out.asymmetric_height_m = (v0 * v0) / (2.0 * kG);
    out.height_sigma_m = (kG * out.hang_time_s * 0.25) * std::max(0.0, sigma_t_s);
    return out;
}

// C3 Trajectory MTF: H_k(theta) = sinc(theta . Delta_k / pi) over exposure blur excursion Delta_k.
struct IntraMotionMtfResult {
    double mtf_avg = 1.0;
    double rlg_boost = 1.0;
};

inline IntraMotionMtfResult evaluate_intra_frame_motion_mtf(
    const std::vector<double>& dy_blur_1x,
    const std::vector<double>& dx_blur_1x,
    const std::vector<double>& frame_weights,
    const std::vector<double>& noise_vars
) noexcept {
    IntraMotionMtfResult out;
    const size_t n = std::min(dy_blur_1x.size(), dx_blur_1x.size());
    if (n == 0) return out;
    static constexpr double kTheta[4][2] = {
        {1.5707963267948966, 0.0},
        {0.0, 1.5707963267948966},
        {1.1107207345395915, 1.1107207345395915},
        {-1.1107207345395915, 1.1107207345395915}
    };
    double info_sum = 0.0;
    double ref_sum = 0.0;
    for (int t = 0; t < 4; ++t) {
        const double ty = kTheta[t][0], tx = kTheta[t][1];
        for (size_t k = 0; k < n; ++k) {
            const double wk = (k < frame_weights.size()) ? std::max(0.0, frame_weights[k]) : 1.0;
            const double vk = (k < noise_vars.size()) ? std::max(1e-10, noise_vars[k]) : 1e-4;
            if (wk <= 0.0) continue;
            const double ph = ty * dy_blur_1x[k] + tx * dx_blur_1x[k];
            const double h = (std::abs(ph) < 1e-6) ? 1.0 : (std::sin(ph) / ph);
            const double iv = wk / vk;
            info_sum += iv * h * h;
            ref_sum += iv;
        }
    }
    if (ref_sum <= 1e-18) return out;
    out.mtf_avg = std::clamp(std::sqrt(info_sum / ref_sum), 0.15, 1.0);
    out.rlg_boost = std::clamp(1.0 / (0.15 + 0.85 * out.mtf_avg), 1.0, 1.85);
    return out;
}

// Evaluate preset C1 temporal weight profile string: "standard" | "rear-curtain" | "fade-in" | "fade-out".
inline std::vector<double> build_target_envelope(const std::string& mode, int n_bins = 64) {
    const int T = std::max(4, n_bins);
    std::vector<double> q(static_cast<size_t>(T), 0.0);
    double sum = 0.0;
    for (int t = 0; t < T; ++t) {
        const double u = static_cast<double>(t) / static_cast<double>(T - 1);
        double v = 1.0;
        if (mode == "rear-curtain") v = 0.15 + ((u > 0.80) ? (5.0 * (u - 0.80)) : 0.0);
        else if (mode == "fade-in") v = 0.1 + 0.9 * u;
        else if (mode == "fade-out") v = 1.0 - 0.9 * u;
        q[t] = v;
        sum += v;
    }
    for (double& v : q) v /= std::max(1e-12, sum);
    return q;
}

} // namespace heraw_motion
