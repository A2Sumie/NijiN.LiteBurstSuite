#!/usr/bin/env python3
"""Compile GLSL 450 compute shaders via glslc into heraw_gpu_spirv.hpp (SPIR-V 1.0)."""

import subprocess
import struct
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT_HPP = ROOT.parent / "heraw_gpu_spirv.hpp"
GLSLC = shutil.which("glslc")
GLSLANG = shutil.which("glslangValidator") or shutil.which("glslang")
SPIRV_VAL = shutil.which("spirv-val")

COMMON = r"""#version 450
float fast_exp_neg(float x) {
    return exp(-clamp(x, 0.0, 40.0));
}
"""

SHADERS = {}

SHADERS["k_zero"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) buffer B0 { float dst[]; };
layout(push_constant) uniform PC { uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i < pc.count) dst[i] = 0.0;
}
"""

SHADERS["k_green_accum"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float frame_n[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flow_mw[]; };
layout(set = 0, binding = 2) readonly buffer B2 { vec4 icov_guide[]; };
layout(set = 0, binding = 3) buffer B3 { float acc_gv[]; };
layout(set = 0, binding = 4) buffer B4 { float acc_gw[]; };
layout(set = 0, binding = 5) buffer B5 { float acc_w2[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh, Ew, is_frame0, use_rob, is_xtrans;
    uint cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, frame_w_scale;
    int origin_y, origin_x;
    float q_luma_var, q_nlt_s0, q_nlt_s1, q_nlt_s2;
} pc;

uint cfa_at(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

vec4 sample_flow_mw(float y, float x) {
    y = clamp(y, 0.0, float(pc.Hh - 1));
    x = clamp(x, 0.0, float(pc.Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, pc.Hh - 1), x1 = min(x0 + 1, pc.Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    vec4 v00 = flow_mw[y0 * pc.Wh + x0];
    vec4 v01 = flow_mw[y0 * pc.Wh + x1];
    vec4 v10 = flow_mw[y1 * pc.Wh + x0];
    vec4 v11 = flow_mw[y1 * pc.Wh + x1];
    return mix(mix(v00, v01, fx), mix(v10, v11, fx), fy);
}

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    float yh = 0.5 * float(Y) + 0.125;
    float xh = 0.5 * float(X) + 0.125;
    int yh_i = clamp(int(yh + 0.5), 0, pc.Hh - 1);
    int xh_i = clamp(int(xh + 0.5), 0, pc.Wh - 1);
    int h_idx = yh_i * pc.Wh + xh_i;

    vec4 fm = sample_flow_mw(yh, xh);
    float mw = fm.z * pc.frame_w_scale;
    if (mw < 0.02) return;

    float dy1 = (pc.is_frame0 != 0) ? 0.0 : (2.0 * fm.x);
    float dx1 = (pc.is_frame0 != 0) ? 0.0 : (2.0 * fm.y);
    precise float yt0 = (float(Y + pc.origin_y) + dy1) - float(pc.origin_y);
    precise float xt0 = (float(X + pc.origin_x) + dx1) - float(pc.origin_x);
    if (yt0 < -0.5 || yt0 > float(pc.H1) - 0.5 || xt0 < -0.5 || xt0 > float(pc.W1) - 0.5) return;

    vec4 icg = icov_guide[h_idx];
    float axx = icg.x, axy = icg.y, ayy = icg.z, anchor = icg.w;
    float a_pos = max(0.0, anchor);
    float ax_cl = min(1.0, a_pos);
    float qv = pc.q_luma_var * max(0.0, pc.q_nlt_s0 + ax_cl * (pc.q_nlt_s1 + ax_cl * pc.q_nlt_s2));
    float thr = 3.5 * sqrt(pc.pg_sig0_sq + pc.pg_gain * a_pos + qv) + 0.008;

    float ws0 = 0.0, ws1 = 0.0, ws2 = 0.0, ws3 = 0.0;
    float vs0 = 0.0, vs1 = 0.0, vs2 = 0.0, vs3 = 0.0;
    float w2_0 = 0.0;

    int rb = int(floor(yt0));
    int cb = int(floor(xt0));
    for (int rr = rb - 1; rr <= rb + 2; ++rr) {
        int r_cl = clamp(rr, 0, pc.H1 - 1);
        float dy_0 = float(rr) - yt0;
        float dy_1 = dy_0 - 0.5;
        int cc_start = cb - 1;
        int cc_step = 1;
        if (pc.is_xtrans == 0) {
            if (((r_cl + cc_start) & 1) == 0) ++cc_start;
            cc_step = 2;
        }
        for (int cc = cc_start; cc <= cb + 2; cc += cc_step) {
            if (cc < 0 || cc >= pc.W1) continue;
            if (pc.is_xtrans != 0 && cfa_at(r_cl, cc) != 1u) continue;
            float val = frame_n[r_cl * pc.W1 + cc];
            float rob = mw;
            if (pc.use_rob != 0) {
                float ad = abs(val - anchor);
                if (ad > thr) { float q = thr / ad; rob *= q * q * q; }
            }
            float dx_0 = float(cc) - xt0;
            float dx_1 = dx_0 - 0.5;

            float d00 = axx * dx_0 * dx_0 + 2.0 * axy * dx_0 * dy_0 + ayy * dy_0 * dy_0;
            float d01 = axx * dx_1 * dx_1 + 2.0 * axy * dx_1 * dy_0 + ayy * dy_0 * dy_0;
            float d10 = axx * dx_0 * dx_0 + 2.0 * axy * dx_0 * dy_1 + ayy * dy_1 * dy_1;
            float d11 = axx * dx_1 * dx_1 + 2.0 * axy * dx_1 * dy_1 + ayy * dy_1 * dy_1;

            if (d00 <= 6.5) { float w = rob * fast_exp_neg(0.5 * d00); ws0 += w; vs0 += w * val; w2_0 += w * w; }
            if (d01 <= 6.5) { float w = rob * fast_exp_neg(0.5 * d01); ws1 += w; vs1 += w * val; }
            if (d10 <= 6.5) { float w = rob * fast_exp_neg(0.5 * d10); ws2 += w; vs2 += w * val; }
            if (d11 <= 6.5) { float w = rob * fast_exp_neg(0.5 * d11); ws3 += w; vs3 += w * val; }
        }
    }

    int W2 = pc.W1 * 2;
    int r2_0 = (2 * Y) * W2 + (2 * X);
    int r2_1 = r2_0 + W2;
    acc_gv[r2_0]     += vs0; acc_gw[r2_0]     += ws0;
    acc_gv[r2_0 + 1] += vs1; acc_gw[r2_0 + 1] += ws1;
    acc_gv[r2_1]     += vs2; acc_gw[r2_1]     += ws2;
    acc_gv[r2_1 + 1] += vs3; acc_gw[r2_1 + 1] += ws3;

    if (((Y | X) & 3) == 0) {
        acc_w2[(Y >> 2) * pc.Ew + (X >> 2)] += w2_0;
    }
}
"""

SHADERS["k_green_final"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float acc_gv[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float acc_gw[]; };
layout(set = 0, binding = 2) buffer B2 { float acc_w2[]; };
layout(set = 0, binding = 3) readonly buffer B3 { vec4 icov_guide[]; };
layout(set = 0, binding = 4) readonly buffer B4 { float g0_ha_1x[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float g_sr2x[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh, Ew;
} pc;

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    int yh_i = clamp(int(0.5 * float(Y) + 0.625), 0, pc.Hh - 1);
    int xh_i = clamp(int(0.5 * float(X) + 0.625), 0, pc.Wh - 1);
    vec4 icg = icov_guide[yh_i * pc.Wh + xh_i];
    // Note: icg.w stores g_guide, and sign/fract or separate? Wait: anchor is icg.w!

    int W2 = pc.W1 * 2;
    int r2_0 = (2 * Y) * W2 + (2 * X);
    int r2_1 = r2_0 + W2;

    float w0 = acc_gw[r2_0],     v0 = acc_gv[r2_0];
    float w1 = acc_gw[r2_0 + 1], v1 = acc_gv[r2_0 + 1];
    float w2 = acc_gw[r2_1],     v2 = acc_gv[r2_1];
    float w3 = acc_gw[r2_1 + 1], v3 = acc_gv[r2_1 + 1];

    float anchor = icg.w;
    float g0 = (w0 > 1e-8) ? (v0 / w0) : anchor;
    float g1 = (w1 > 1e-8) ? (v1 / w1) : anchor;
    float g2 = (w2 > 1e-8) ? (v2 / w2) : anchor;
    float g3 = (w3 > 1e-8) ? (v3 / w3) : anchor;

    // Smooth single-frame Hamilton-Adams directional detail recovery when multi-frame weight collapses (w -> 1)
    // OR when tripod/low-phase bursts lack Green photodiode coverage on R/B subpixel sites (phase_def)
    float w_avg = 0.25 * (w0 + w1 + w2 + w3);
    float phase_def = clamp((0.72 - min(w0, w3) / max(max(w1, w2), 1e-6)) / 0.28, 0.0, 0.48);
    float alpha_ha = max(clamp((2.3 - w_avg) / 1.35, 0.0, 0.88), phase_def);
    if (alpha_ha > 1e-3) {
        int Xp = min(X + 1, pc.W1 - 1);
        int Yp = min(Y + 1, pc.H1 - 1);
        float h00 = g0_ha_1x[Y  * pc.W1 + X];
        float h01 = g0_ha_1x[Y  * pc.W1 + Xp];
        float h10 = g0_ha_1x[Yp * pc.W1 + X];
        float h11 = g0_ha_1x[Yp * pc.W1 + Xp];

        float d_nw = abs(h00 - h11);
        float d_ne = abs(h01 - h10);
        float w_nw = 1.0 / (d_nw + 0.002);
        float w_ne = 1.0 / (d_ne + 0.002);
        float diag = (w_nw * 0.5 * (h00 + h11) + w_ne * 0.5 * (h01 + h10)) / (w_nw + w_ne);

        g0 = mix(g0, h00, alpha_ha);
        g1 = mix(g1, 0.5 * (h00 + h01), alpha_ha);
        g2 = mix(g2, 0.5 * (h00 + h10), alpha_ha);
        g3 = mix(g3, diag, alpha_ha);
    }

    g_sr2x[r2_0]     = g0;
    g_sr2x[r2_0 + 1] = g1;
    g_sr2x[r2_1]     = g2;
    g_sr2x[r2_1 + 1] = g3;

    if (((Y | X) & 3) == 0) {
        int ei = (Y >> 2) * pc.Ew + (X >> 2);
        float w2_0 = acc_w2[ei];
        acc_w2[ei] = (w0 > 1e-8) ? (w2_0 / (w0 * w0)) : -1.0;
    }
}
"""

SHADERS["k_chroma_accum"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float frame_n[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flow_mw[]; };
layout(set = 0, binding = 2) readonly buffer B2 { vec4 icov_guide[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float g_sr2x[]; };
layout(set = 0, binding = 4) readonly buffer B4 { float c_anc_1x[]; };
layout(set = 0, binding = 5) buffer B5 { vec4 acc_chroma[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh, is_frame0, use_rob, is_xtrans;
    uint cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, wb_r, wb_b;
    float lca_r_k1, lca_r_k2, lca_b_k1, lca_b_k2;
    float ois_dr_y, ois_dr_x, ois_db_y, ois_db_x;
    int full_H1, full_W1, origin_y, origin_x;
    float q_chroma_var, q_nlt_s0, q_nlt_s1, q_nlt_s2;
} pc;

uint cfa_at_c(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

float chroma_anchor(int y, int x, uint ch) {
    float sum = 0.0, weights = 0.0;
    for (int r = max(0, y - 3); r <= min(pc.H1 - 1, y + 3); ++r)
        for (int c = max(0, x - 3); c <= min(pc.W1 - 1, x + 3); ++c) {
            if (cfa_at_c(r, c) != ch) continue;
            float w = 1.0 / (0.25 + float((r-y)*(r-y) + (c-x)*(c-x)));
            sum += w * c_anc_1x[r * pc.W1 + c];
            weights += w;
        }
    return sum / max(weights, 1e-12);
}

vec4 sample_flow_mw(float y, float x) {
    y = clamp(y, 0.0, float(pc.Hh - 1));
    x = clamp(x, 0.0, float(pc.Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, pc.Hh - 1), x1 = min(x0 + 1, pc.Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(flow_mw[y0 * pc.Wh + x0], flow_mw[y0 * pc.Wh + x1], fx),
               mix(flow_mw[y1 * pc.Wh + x0], flow_mw[y1 * pc.Wh + x1], fx), fy);
}

float bilerp_g2x(float y, float x) {
    int H2 = pc.H1 * 2, W2 = pc.W1 * 2;
    y = clamp(y, 0.0, float(H2 - 1));
    x = clamp(x, 0.0, float(W2 - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, H2 - 1), x1 = min(x0 + 1, W2 - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(g_sr2x[y0 * W2 + x0], g_sr2x[y0 * W2 + x1], fx),
               mix(g_sr2x[y1 * W2 + x0], g_sr2x[y1 * W2 + x1], fx), fy);
}

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    vec2 add_r = vec2(0.0);
    vec2 add_b = vec2(0.0);

    for (int is_b = 0; is_b < 2; ++is_b) {
        int r2 = 2 * Y + is_b;
        int c2 = 2 * X + is_b;
        float y1_ref = 0.5 * float(r2);
        float x1_ref = 0.5 * float(c2);
        float yh = 0.25 * float(r2);
        float xh = 0.25 * float(c2);

        vec4 fm = sample_flow_mw(yh, xh);
        float mw = fm.z;
        if (mw < 0.02) continue;

        float dy1 = (pc.is_frame0 != 0) ? 0.0 : (2.0 * fm.x);
        float dx1 = (pc.is_frame0 != 0) ? 0.0 : (2.0 * fm.y);

        float cy = 0.5 * float(pc.full_H1), cx = 0.5 * float(pc.full_W1);
        float inv_rmax = 1.0 / max(1.0, max(cy, cx));
        float ry = (y1_ref + float(pc.origin_y)) - cy;
        float rx = (x1_ref + float(pc.origin_x)) - cx;
        float ry_n = ry * inv_rmax;
        float rx_n = rx * inv_rmax;
        float r2_n = rx_n * rx_n + ry_n * ry_n;
        float k1 = (is_b != 0) ? pc.lca_b_k1 : pc.lca_r_k1;
        float k2 = (is_b != 0) ? pc.lca_b_k2 : pc.lca_r_k2;
        float lca_s = k1 + k2 * r2_n;
        float ody = (is_b != 0) ? pc.ois_db_y : pc.ois_dr_y;
        float odx = (is_b != 0) ? pc.ois_db_x : pc.ois_dr_x;

        float tot_dy = dy1 + lca_s * ry + ody;
        float tot_dx = dx1 + lca_s * rx + odx;
        precise float yt = ((y1_ref + float(pc.origin_y)) + tot_dy) - float(pc.origin_y);
        precise float xt = ((x1_ref + float(pc.origin_x)) + tot_dx) - float(pc.origin_x);
        if (yt < 0.0 || yt > float(pc.H1 - 1) || xt < 0.0 || xt > float(pc.W1 - 1)) continue;

        int yh_i = clamp(int(yh + 0.5), 0, pc.Hh - 1);
        int xh_i = clamp(int(xh + 0.5), 0, pc.Wh - 1);
        vec4 icg = icov_guide[yh_i * pc.Wh + xh_i];
        float axx = 0.60 * icg.x, axy = 0.60 * icg.y, ayy = 0.60 * icg.z;

        int r1_anc = clamp(2 * yh_i + is_b, 0, pc.H1 - 1);
        int c1_anc = clamp(2 * xh_i + is_b, 0, pc.W1 - 1);
        float c_anc = chroma_anchor(r1_anc, c1_anc, is_b != 0 ? 2u : 0u);
        float c_pos = max(0.0, max(c_anc, icg.w));
        float cx_cl = min(1.0, c_pos);
        float qvc = pc.q_chroma_var * max(0.0, pc.q_nlt_s0 + cx_cl * (pc.q_nlt_s1 + cx_cl * pc.q_nlt_s2));
        float thr = 4.0 * sqrt(pc.pg_sig0_sq + pc.pg_gain * c_pos + qvc) + 0.01;
        float wb = (is_b != 0) ? pc.wb_b : pc.wb_r;

        float d_sum = 0.0, w_sum = 0.0;
        if (pc.is_xtrans == 0) {
            int qyc = int(floor((yt - float(is_b)) * 0.5));
            int qxc = int(floor((xt - float(is_b)) * 0.5));
            for (int dq_y = 0; dq_y <= 1; ++dq_y) {
                int rr = clamp(2 * (qyc + dq_y) + is_b, is_b, pc.H1 - 2 + is_b);
                float dy = float(rr) - yt;
                for (int dq_x = 0; dq_x <= 1; ++dq_x) {
                    int cc = clamp(2 * (qxc + dq_x) + is_b, is_b, pc.W1 - 2 + is_b);
                    float dx = float(cc) - xt;
                    float d2 = axx * dx * dx + 2.0 * axy * dx * dy + ayy * dy * dy;
                    if (d2 > 7.5) continue;

                    float c_val = frame_n[rr * pc.W1 + cc] * wb;
                    float g_at_q = bilerp_g2x(
                        2.0 * ((float(rr + pc.origin_y) - tot_dy) - float(pc.origin_y)),
                        2.0 * ((float(cc + pc.origin_x) - tot_dx) - float(pc.origin_x)));
                    float diff_cg = c_val - g_at_q;

                    float rob = 1.0;
                    if (pc.use_rob != 0) {
                        float ad = abs(c_val - c_anc * wb);
                        float tw = thr * wb;
                        if (ad > tw) { float q = tw / ad; rob = q * q * q; }
                    }
                    float w = mw * rob * fast_exp_neg(0.5 * d2);
                    w_sum += w;
                    d_sum += w * diff_cg;
                }
            }
        } else {
            uint want_ch = (is_b != 0) ? 2u : 0u;
            int rb = int(floor(yt)), cb = int(floor(xt));
            for (int rr = rb - 2; rr <= rb + 3; ++rr) {
                if (rr < 0 || rr >= pc.H1) continue;
                float dy = float(rr) - yt;
                for (int cc = cb - 2; cc <= cb + 3; ++cc) {
                    if (cc < 0 || cc >= pc.W1) continue;
                    if (cfa_at_c(rr, cc) != want_ch) continue;
                    float dx = float(cc) - xt;
                    float d2 = 0.55 * (axx * dx * dx + 2.0 * axy * dx * dy + ayy * dy * dy);
                    if (d2 > 9.5) continue;

                    float c_val = frame_n[rr * pc.W1 + cc] * wb;
                    float g_at_q = bilerp_g2x(
                        2.0 * ((float(rr + pc.origin_y) - tot_dy) - float(pc.origin_y)),
                        2.0 * ((float(cc + pc.origin_x) - tot_dx) - float(pc.origin_x)));
                    float diff_cg = c_val - g_at_q;

                    float rob = 1.0;
                    if (pc.use_rob != 0) {
                        float ad = abs(c_val - c_anc * wb);
                        float tw = thr * wb;
                        if (ad > tw) { float q = tw / ad; rob = q * q * q; }
                    }
                    float w = mw * rob * fast_exp_neg(0.5 * d2);
                    w_sum += w;
                    d_sum += w * diff_cg;
                }
            }
        }
        if (is_b == 0) add_r = vec2(d_sum, w_sum);
        else           add_b = vec2(d_sum, w_sum);
    }

    int q_idx = Y * pc.W1 + X;
    acc_chroma[q_idx] += vec4(add_r, add_b);
}
"""

SHADERS["k_chroma_final"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { vec4 acc_chroma[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float g_sr2x[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float c_anc_1x[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float dR_1x[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float dB_1x[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh;
    float wb_r, wb_b, hl_start, hl_span;
    uint cfa0, cfa1, cfa2;
    int is_xtrans;
} pc;

uint cfa_at_c(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

float chroma_anchor(int y, int x, uint ch) {
    float sum = 0.0, weights = 0.0;
    for (int r = max(0, y - 3); r <= min(pc.H1 - 1, y + 3); ++r)
        for (int c = max(0, x - 3); c <= min(pc.W1 - 1, x + 3); ++c) {
            if (cfa_at_c(r, c) != ch) continue;
            float w = 1.0 / (0.25 + float((r-y)*(r-y) + (c-x)*(c-x)));
            sum += w * c_anc_1x[r * pc.W1 + c];
            weights += w;
        }
    return sum / max(weights, 1e-12);
}

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    int W2 = pc.W1 * 2;
    int q_idx = Y * pc.W1 + X;
    vec4 ac = acc_chroma[q_idx];

    for (int is_b = 0; is_b < 2; ++is_b) {
        int r2 = 2 * Y + is_b;
        int c2 = 2 * X + is_b;
        int yh_i = clamp(int(0.25 * float(r2) + 0.5), 0, pc.Hh - 1);
        int xh_i = clamp(int(0.25 * float(c2) + 0.5), 0, pc.Wh - 1);
        int r1_anc = clamp(2 * yh_i + is_b, 0, pc.H1 - 1);
        int c1_anc = clamp(2 * xh_i + is_b, 0, pc.W1 - 1);
        float c_anc = chroma_anchor(r1_anc, c1_anc, is_b != 0 ? 2u : 0u);
        float wb = (is_b != 0) ? pc.wb_b : pc.wb_r;
        float g_here = g_sr2x[r2 * W2 + c2];

        float d_sum = (is_b == 0) ? ac.x : ac.z;
        float w_sum = (is_b == 0) ? ac.y : ac.w;
        float dv = (w_sum > 1e-8) ? (d_sum / w_sum) : (c_anc * wb - g_here);

        float hl_peak = max(g_here, c_anc);
        if (hl_peak > pc.hl_start) {
            float t = clamp((hl_peak - pc.hl_start) / pc.hl_span, 0.0, 1.0);
            dv *= (1.0 - t) * (1.0 - t);
        }
        if (is_b == 0) dR_1x[q_idx] = dv;
        else           dB_1x[q_idx] = dv;
    }
}
"""

SHADERS["k_gat_fwd"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float g_sr2x[]; };
layout(set = 0, binding = 1) writeonly buffer B1 { float z_gat[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float pyr0[]; };

layout(push_constant) uniform PC {
    uint count;
    float ped, g_eff, gat_c, sqrt_c;
} pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    float x = max(0.0, g_sr2x[i] + pc.ped);
    float zv = (2.0 * x) / (sqrt(pc.g_eff * x + pc.gat_c) + pc.sqrt_c);
    z_gat[i] = zv;
    pyr0[i]  = zv;
}
"""

SHADERS["k_box_down2x"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src[]; };
layout(set = 0, binding = 1) writeonly buffer B1 { float dst[]; };
layout(push_constant) uniform PC { int Hs, Ws, Hd, Wd; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.Wd || r >= pc.Hd) return;
    int r0 = 2 * r, c0 = 2 * c;
    dst[r * pc.Wd + c] = 0.25 * (
        src[r0 * pc.Ws + c0] + src[r0 * pc.Ws + c0 + 1] +
        src[(r0 + 1) * pc.Ws + c0] + src[(r0 + 1) * pc.Ws + c0 + 1]
    );
}
"""

SHADERS["k_dct_wiener"] = COMMON + r"""
layout(local_size_x = 128, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float guide_in[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float snr_boost[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float dst_out[]; };

layout(push_constant) uniform PC {
    int H, W, Hh, Wh, is_wiener, scale_mul;
    float sigma;
    int roi_y0, roi_x0, roi_y1, roi_x1;
} pc;

// Cooperative 14-block x 8-lane zero-bank-conflict tile covering 20x10 output pixels (two 10x10 ROI subtiles).
// Total shared memory = 10,868 B (fits 3 workgroups = 384 threads on 32 KB Apple GPU, up to 9 on desktop GPUs).
shared float s_src[24][35];
shared float s_gde[24][35];
shared float s_work[14][8][9];
shared float s_bw[14];
shared int   s_rx[14];

void dct8_fwd_1d(inout float p0, inout float p1, inout float p2, inout float p3,
                 inout float p4, inout float p5, inout float p6, inout float p7) {
    const float c1 = 0.4903926402, c2 = 0.4619397663, c3 = 0.4157348062, c4 = 0.3535533906;
    const float c5 = 0.2777851165, c6 = 0.1913417162, c7 = 0.0975451610;
    float s0 = p0 + p7, d7 = p0 - p7;
    float s1 = p1 + p6, d6 = p1 - p6;
    float s2 = p2 + p5, d5 = p2 - p5;
    float s3 = p3 + p4, d4 = p3 - p4;
    float e0 = s0 + s3, e3 = s0 - s3;
    float e1 = s1 + s2, e2 = s1 - s2;
    p0 = (e0 + e1) * c4;
    p4 = (e0 - e1) * c4;
    p2 = e3 * c2 + e2 * c6;
    p6 = e3 * c6 - e2 * c2;
    p1 = d7 * c1 + d6 * c3 + d5 * c5 + d4 * c7;
    p3 = d7 * c3 - d6 * c7 - d5 * c1 - d4 * c5;
    p5 = d7 * c5 - d6 * c1 + d5 * c7 + d4 * c3;
    p7 = d7 * c7 - d6 * c5 + d5 * c3 - d4 * c1;
}

void dct8_inv_1d(inout float p0, inout float p1, inout float p2, inout float p3,
                 inout float p4, inout float p5, inout float p6, inout float p7) {
    const float c1 = 0.4903926402, c2 = 0.4619397663, c3 = 0.4157348062, c4 = 0.3535533906;
    const float c5 = 0.2777851165, c6 = 0.1913417162, c7 = 0.0975451610;
    float e0 = (p0 + p4) * c4;
    float e1 = (p0 - p4) * c4;
    float e3 = p2 * c2 + p6 * c6;
    float e2 = p2 * c6 - p6 * c2;
    float s0 = e0 + e3, s3 = e0 - e3;
    float s1 = e1 + e2, s2 = e1 - e2;
    float d7 = p1 * c1 + p3 * c3 + p5 * c5 + p7 * c7;
    float d6 = p1 * c3 - p3 * c7 - p5 * c1 - p7 * c5;
    float d5 = p1 * c5 - p3 * c1 + p5 * c7 + p7 * c3;
    float d4 = p1 * c7 - p3 * c5 + p5 * c3 - p7 * c1;
    p0 = s0 + d7; p7 = s0 - d7;
    p1 = s1 + d6; p6 = s1 - d6;
    p2 = s2 + d5; p5 = s2 - d5;
    p3 = s3 + d4; p4 = s3 - d4;
}

void main() {
    int tid = int(gl_LocalInvocationID.x);
    int b_slot = tid >> 3;
    int lane = tid & 7;
    int out_x0 = int(gl_WorkGroupID.x) * 20;
    int out_y0 = int(gl_WorkGroupID.y) * 10;

    bool skip0 = (pc.roi_y1 > 0) && (out_y0 + 10 <= pc.roi_y0 || out_y0 >= pc.roi_y1 ||
                                     out_x0 + 10 <= pc.roi_x0 || out_x0 >= pc.roi_x1);
    bool skip1 = (out_x0 + 10 >= pc.W) ||
                 ((pc.roi_y1 > 0) && (out_y0 + 10 <= pc.roi_y0 || out_y0 >= pc.roi_y1 ||
                                      out_x0 + 20 <= pc.roi_x0 || out_x0 + 10 >= pc.roi_x1));
    if (skip0 && skip1) {
        for (int k = tid; k < 200; k += 128) {
            int y = out_y0 + k / 20, x = out_x0 + k % 20;
            if (y < pc.H && x < pc.W) dst_out[y * pc.W + x] = src_in[y * pc.W + x];
        }
        return;
    }

    for (int k = tid; k < 24 * 34; k += 128) {
        int sy = k / 34, sx = k % 34;
        int gy = clamp(out_y0 - 7 + sy, 0, pc.H - 1);
        int gx = clamp(out_x0 - 7 + sx, 0, pc.W - 1);
        int gi = gy * pc.W + gx;
        s_src[sy][sx] = src_in[gi];
        if (pc.is_wiener != 0) s_gde[sy][sx] = guide_in[gi];
    }
    int bc = out_x0 - 6 + 2 * b_slot;
    if ((pc.W & 1) != 0 && bc == pc.W - 7) bc = pc.W - 8;
    if (tid < 14) {
        int bc_t = out_x0 - 6 + 2 * tid;
        if ((pc.W & 1) != 0 && bc_t == pc.W - 7) bc_t = pc.W - 8;
        s_rx[tid] = bc_t - out_x0;
    }
    barrier();

    bool active_x = (b_slot < 14) && (bc >= 0 && bc <= pc.W - 8);
    int sx0 = bc - (out_x0 - 7);
    int ch = active_x ? clamp(((bc + 4) * pc.scale_mul) >> 2, 0, pc.Wh - 1) : 0;

    float acc0 = 0.0, wgt0 = 0.0, acc1 = 0.0, wgt1 = 0.0;
    int oy0 = tid / 20, ox0 = tid % 20;
    int k1 = tid + 128;
    int oy1 = k1 / 20, ox1 = k1 % 20;

    for (int by = 0; by < 9; ++by) {
        int br = out_y0 - 6 + 2 * by;
        if ((pc.H & 1) != 0 && br == pc.H - 7) br = pc.H - 8;
        if (br < 0) continue;
        int ry = br - out_y0;
        if (br > pc.H - 8 || ry >= 10) break;

        int sy0 = br - (out_y0 - 7);
        float sig = 0.0;
        if (active_x) {
            int rh = clamp(((br + 4) * pc.scale_mul) >> 2, 0, pc.Hh - 1);
            sig = pc.sigma * snr_boost[rh * pc.Wh + ch];
        }

        if (pc.is_wiener == 0) {
            if (active_x) {
                int r = sy0 + lane;
                float p0 = s_src[r][sx0 + 0], p1 = s_src[r][sx0 + 1], p2 = s_src[r][sx0 + 2], p3 = s_src[r][sx0 + 3];
                float p4 = s_src[r][sx0 + 4], p5 = s_src[r][sx0 + 5], p6 = s_src[r][sx0 + 6], p7 = s_src[r][sx0 + 7];
                dct8_fwd_1d(p0, p1, p2, p3, p4, p5, p6, p7);
                s_work[b_slot][lane][0] = p0; s_work[b_slot][lane][1] = p1;
                s_work[b_slot][lane][2] = p2; s_work[b_slot][lane][3] = p3;
                s_work[b_slot][lane][4] = p4; s_work[b_slot][lane][5] = p5;
                s_work[b_slot][lane][6] = p6; s_work[b_slot][lane][7] = p7;
            }
            barrier();
            float q0 = 0.0, q1 = 0.0, q2 = 0.0, q3 = 0.0, q4 = 0.0, q5 = 0.0, q6 = 0.0, q7 = 0.0;
            int nz = 0;
            if (active_x) {
                q0 = s_work[b_slot][0][lane]; q1 = s_work[b_slot][1][lane];
                q2 = s_work[b_slot][2][lane]; q3 = s_work[b_slot][3][lane];
                q4 = s_work[b_slot][4][lane]; q5 = s_work[b_slot][5][lane];
                q6 = s_work[b_slot][6][lane]; q7 = s_work[b_slot][7][lane];
                dct8_fwd_1d(q0, q1, q2, q3, q4, q5, q6, q7);
                float thr = 3.10 * sig;
                if (lane == 0) nz = 1;
                else if (abs(q0) < thr) q0 = 0.0; else ++nz;
                if (abs(q1) < thr) q1 = 0.0; else ++nz;
                if (abs(q2) < thr) q2 = 0.0; else ++nz;
                if (abs(q3) < thr) q3 = 0.0; else ++nz;
                if (abs(q4) < thr) q4 = 0.0; else ++nz;
                if (abs(q5) < thr) q5 = 0.0; else ++nz;
                if (abs(q6) < thr) q6 = 0.0; else ++nz;
                if (abs(q7) < thr) q7 = 0.0; else ++nz;
                dct8_inv_1d(q0, q1, q2, q3, q4, q5, q6, q7);
            }
            barrier();
            if (active_x) {
                s_work[b_slot][0][lane] = q0; s_work[b_slot][1][lane] = q1;
                s_work[b_slot][2][lane] = q2; s_work[b_slot][3][lane] = q3;
                s_work[b_slot][4][lane] = q4; s_work[b_slot][5][lane] = q5;
                s_work[b_slot][6][lane] = q6; s_work[b_slot][7][lane] = q7;
                s_work[b_slot][lane][8] = float(nz);
            }
            barrier();
            if (active_x) {
                float tot_nz = s_work[b_slot][0][8] + s_work[b_slot][1][8] + s_work[b_slot][2][8] + s_work[b_slot][3][8]
                             + s_work[b_slot][4][8] + s_work[b_slot][5][8] + s_work[b_slot][6][8] + s_work[b_slot][7][8];
                float bw = 1.0 / tot_nz;
                float p0 = s_work[b_slot][lane][0], p1 = s_work[b_slot][lane][1];
                float p2 = s_work[b_slot][lane][2], p3 = s_work[b_slot][lane][3];
                float p4 = s_work[b_slot][lane][4], p5 = s_work[b_slot][lane][5];
                float p6 = s_work[b_slot][lane][6], p7 = s_work[b_slot][lane][7];
                dct8_inv_1d(p0, p1, p2, p3, p4, p5, p6, p7);
                s_work[b_slot][lane][0] = bw * p0; s_work[b_slot][lane][1] = bw * p1;
                s_work[b_slot][lane][2] = bw * p2; s_work[b_slot][lane][3] = bw * p3;
                s_work[b_slot][lane][4] = bw * p4; s_work[b_slot][lane][5] = bw * p5;
                s_work[b_slot][lane][6] = bw * p6; s_work[b_slot][lane][7] = bw * p7;
                if (lane == 0) s_bw[b_slot] = bw;
            } else if (b_slot < 14 && lane == 0) {
                s_bw[b_slot] = 0.0;
            }
            barrier();
        } else {
            if (active_x) {
                int r = sy0 + lane;
                float p0 = s_gde[r][sx0 + 0], p1 = s_gde[r][sx0 + 1], p2 = s_gde[r][sx0 + 2], p3 = s_gde[r][sx0 + 3];
                float p4 = s_gde[r][sx0 + 4], p5 = s_gde[r][sx0 + 5], p6 = s_gde[r][sx0 + 6], p7 = s_gde[r][sx0 + 7];
                dct8_fwd_1d(p0, p1, p2, p3, p4, p5, p6, p7);
                s_work[b_slot][lane][0] = p0; s_work[b_slot][lane][1] = p1;
                s_work[b_slot][lane][2] = p2; s_work[b_slot][lane][3] = p3;
                s_work[b_slot][lane][4] = p4; s_work[b_slot][lane][5] = p5;
                s_work[b_slot][lane][6] = p6; s_work[b_slot][lane][7] = p7;
            }
            barrier();
            float gw0 = 1.0, gw1 = 0.0, gw2 = 0.0, gw3 = 0.0, gw4 = 0.0, gw5 = 0.0, gw6 = 0.0, gw7 = 0.0;
            if (active_x) {
                float g0 = s_work[b_slot][0][lane], g1 = s_work[b_slot][1][lane];
                float g2 = s_work[b_slot][2][lane], g3 = s_work[b_slot][3][lane];
                float g4 = s_work[b_slot][4][lane], g5 = s_work[b_slot][5][lane];
                float g6 = s_work[b_slot][6][lane], g7 = s_work[b_slot][7][lane];
                dct8_fwd_1d(g0, g1, g2, g3, g4, g5, g6, g7);
                float sig2 = 1.35 * sig * sig;
                gw0 = (lane == 0) ? 1.0 : ((g0 * g0) / (g0 * g0 + sig2));
                gw1 = (g1 * g1) / (g1 * g1 + sig2);
                gw2 = (g2 * g2) / (g2 * g2 + sig2);
                gw3 = (g3 * g3) / (g3 * g3 + sig2);
                gw4 = (g4 * g4) / (g4 * g4 + sig2);
                gw5 = (g5 * g5) / (g5 * g5 + sig2);
                gw6 = (g6 * g6) / (g6 * g6 + sig2);
                gw7 = (g7 * g7) / (g7 * g7 + sig2);
                s_work[b_slot][lane][8] = gw0*gw0 + gw1*gw1 + gw2*gw2 + gw3*gw3
                                        + gw4*gw4 + gw5*gw5 + gw6*gw6 + gw7*gw7;
            }
            barrier();
            float bw = 0.0;
            if (active_x) {
                float w2_sum = s_work[b_slot][0][8] + s_work[b_slot][1][8] + s_work[b_slot][2][8] + s_work[b_slot][3][8]
                             + s_work[b_slot][4][8] + s_work[b_slot][5][8] + s_work[b_slot][6][8] + s_work[b_slot][7][8];
                bw = 1.0 / w2_sum;
                int r = sy0 + lane;
                float p0 = s_src[r][sx0 + 0], p1 = s_src[r][sx0 + 1], p2 = s_src[r][sx0 + 2], p3 = s_src[r][sx0 + 3];
                float p4 = s_src[r][sx0 + 4], p5 = s_src[r][sx0 + 5], p6 = s_src[r][sx0 + 6], p7 = s_src[r][sx0 + 7];
                dct8_fwd_1d(p0, p1, p2, p3, p4, p5, p6, p7);
                s_work[b_slot][lane][0] = p0; s_work[b_slot][lane][1] = p1;
                s_work[b_slot][lane][2] = p2; s_work[b_slot][lane][3] = p3;
                s_work[b_slot][lane][4] = p4; s_work[b_slot][lane][5] = p5;
                s_work[b_slot][lane][6] = p6; s_work[b_slot][lane][7] = p7;
            }
            barrier();
            float q0 = 0.0, q1 = 0.0, q2 = 0.0, q3 = 0.0, q4 = 0.0, q5 = 0.0, q6 = 0.0, q7 = 0.0;
            if (active_x) {
                q0 = s_work[b_slot][0][lane]; q1 = s_work[b_slot][1][lane];
                q2 = s_work[b_slot][2][lane]; q3 = s_work[b_slot][3][lane];
                q4 = s_work[b_slot][4][lane]; q5 = s_work[b_slot][5][lane];
                q6 = s_work[b_slot][6][lane]; q7 = s_work[b_slot][7][lane];
                dct8_fwd_1d(q0, q1, q2, q3, q4, q5, q6, q7);
                q0 *= gw0; q1 *= gw1; q2 *= gw2; q3 *= gw3;
                q4 *= gw4; q5 *= gw5; q6 *= gw6; q7 *= gw7;
                dct8_inv_1d(q0, q1, q2, q3, q4, q5, q6, q7);
            }
            barrier();
            if (active_x) {
                s_work[b_slot][0][lane] = q0; s_work[b_slot][1][lane] = q1;
                s_work[b_slot][2][lane] = q2; s_work[b_slot][3][lane] = q3;
                s_work[b_slot][4][lane] = q4; s_work[b_slot][5][lane] = q5;
                s_work[b_slot][6][lane] = q6; s_work[b_slot][7][lane] = q7;
            }
            barrier();
            if (active_x) {
                float p0 = s_work[b_slot][lane][0], p1 = s_work[b_slot][lane][1];
                float p2 = s_work[b_slot][lane][2], p3 = s_work[b_slot][lane][3];
                float p4 = s_work[b_slot][lane][4], p5 = s_work[b_slot][lane][5];
                float p6 = s_work[b_slot][lane][6], p7 = s_work[b_slot][lane][7];
                dct8_inv_1d(p0, p1, p2, p3, p4, p5, p6, p7);
                s_work[b_slot][lane][0] = bw * p0; s_work[b_slot][lane][1] = bw * p1;
                s_work[b_slot][lane][2] = bw * p2; s_work[b_slot][lane][3] = bw * p3;
                s_work[b_slot][lane][4] = bw * p4; s_work[b_slot][lane][5] = bw * p5;
                s_work[b_slot][lane][6] = bw * p6; s_work[b_slot][lane][7] = bw * p7;
                if (lane == 0) s_bw[b_slot] = bw;
            } else if (b_slot < 14 && lane == 0) {
                s_bw[b_slot] = 0.0;
            }
            barrier();
        }

        int py0 = oy0 - ry;
        if (uint(py0) < 8u) {
            int bx0 = max(0, ox0 >> 1), bx1 = min(13, (ox0 + 7) >> 1);
            for (int bx = bx0; bx <= bx1; ++bx) {
                float bw = s_bw[bx];
                int px = ox0 - s_rx[bx];
                if (bw > 0.0 && uint(px) < 8u) {
                    acc0 += s_work[bx][py0][px];
                    wgt0 += bw;
                }
            }
        }
        if (k1 < 200) {
            int py1 = oy1 - ry;
            if (uint(py1) < 8u) {
                int bx0 = max(0, ox1 >> 1), bx1 = min(13, (ox1 + 7) >> 1);
                for (int bx = bx0; bx <= bx1; ++bx) {
                    float bw = s_bw[bx];
                    int px = ox1 - s_rx[bx];
                    if (bw > 0.0 && uint(px) < 8u) {
                        acc1 += s_work[bx][py1][px];
                        wgt1 += bw;
                    }
                }
            }
        }
        barrier();
    }

    int gy0 = out_y0 + oy0, gx0 = out_x0 + ox0;
    if (gy0 < pc.H && gx0 < pc.W) {
        bool sk = (ox0 < 10) ? skip0 : skip1;
        dst_out[gy0 * pc.W + gx0] = (!sk && wgt0 > 1e-12) ? (acc0 / wgt0) : s_src[oy0 + 7][ox0 + 7];
    }
    if (k1 < 200) {
        int gy1 = out_y0 + oy1, gx1 = out_x0 + ox1;
        if (gy1 < pc.H && gx1 < pc.W) {
            bool sk = (ox1 < 10) ? skip0 : skip1;
            dst_out[gy1 * pc.W + gx1] = (!sk && wgt1 > 1e-12) ? (acc1 / wgt1) : s_src[oy1 + 7][ox1 + 7];
        }
    }
}
"""

SHADERS["k_pyr_sub_down2x"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float den_cur[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float den_coarse[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float diff_out[]; };
layout(push_constant) uniform PC { int Hs, Ws, Hd, Wd; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.Wd || r >= pc.Hd) return;
    int r0 = 2 * r, c0 = 2 * c;
    float down = 0.25 * (
        den_cur[r0 * pc.Ws + c0] + den_cur[r0 * pc.Ws + c0 + 1] +
        den_cur[(r0 + 1) * pc.Ws + c0] + den_cur[(r0 + 1) * pc.Ws + c0 + 1]
    );
    diff_out[r * pc.Wd + c] = den_coarse[r * pc.Wd + c] - down;
}
"""

SHADERS["k_pyr_lap_add"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float diff_coarse[]; };
layout(set = 0, binding = 1) buffer B1 { float den_cur[]; };
layout(push_constant) uniform PC { int Hs, Ws, Hd, Wd; float tau2; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.Ws || r >= pc.Hs) return;

    float ys = clamp((float(r) + 0.5) * 0.5 - 0.5, 0.0, float(pc.Hd - 1));
    float xs = clamp((float(c) + 0.5) * 0.5 - 0.5, 0.0, float(pc.Wd - 1));
    int y0 = int(floor(ys)), x0 = int(floor(xs));
    int y1 = min(y0 + 1, pc.Hd - 1), x1 = min(x0 + 1, pc.Wd - 1);
    float fy = ys - float(y0), fx = xs - float(x0);
    float d = mix(mix(diff_coarse[y0 * pc.Wd + x0], diff_coarse[y0 * pc.Wd + x1], fx),
                  mix(diff_coarse[y1 * pc.Wd + x0], diff_coarse[y1 * pc.Wd + x1], fx), fy);
    float gate = pc.tau2 / (pc.tau2 + d * d);
    den_cur[r * pc.Ws + c] += 0.92 * gate * d;
}
"""

SHADERS["k_edge_polish"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src[]; };
layout(set = 0, binding = 1) writeonly buffer B1 { float dst[]; };
layout(push_constant) uniform PC { int H, W, dir; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;

    int roff = r * pc.W;
    int rm = max(r - 1, 0), rp = min(r + 1, pc.H - 1);
    int cm = max(c - 1, 0), cp = min(c + 1, pc.W - 1);
    float v0 = src[roff + c];
    float gx = src[roff + cp] - src[roff + cm];
    float gy = src[rp * pc.W + c] - src[rm * pc.W + c];
    if (gx * gx + gy * gy > 3.2) {
        dst[roff + c] = v0;
        return;
    }
    const float k1d[5] = float[5](1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0);
    const float inv_2s2 = 1.0 / (2.0 * 1.05 * 1.05);
    float ws = 0.0, vs = 0.0;
    for (int k = -2; k <= 2; ++k) {
        float vk = (pc.dir == 0)
            ? src[roff + clamp(c + k, 0, pc.W - 1)]
            : src[clamp(r + k, 0, pc.H - 1) * pc.W + c];
        float d = vk - v0;
        float w = k1d[k + 2] * fast_exp_neg(d * d * inv_2s2);
        ws += w; vs += w * vk;
    }
    dst[roff + c] = vs / max(ws, 1e-12);
}
"""

SHADERS["k_dv_map"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float z_gat[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float z_den[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float dv_map[]; };
layout(push_constant) uniform PC { int Hh, Wh, W2; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.Wh || r >= pc.Hh) return;
    float s2 = 0.0;
    for (int dy = 0; dy < 4; ++dy) {
        int ro = (4 * r + dy) * pc.W2 + 4 * c;
        for (int dx = 0; dx < 4; ++dx) {
            float d = z_gat[ro + dx] - z_den[ro + dx];
            s2 += d * d;
        }
    }
    dv_map[r * pc.Wh + c] = min(s2 * (1.0 / 16.0), 1.25);
}
"""

SHADERS["k_gauss_1d"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src[]; };
layout(set = 0, binding = 1) writeonly buffer B1 { float dst[]; };
layout(push_constant) uniform PC {
    int H, W, dir, radius;
    float w[25];
} pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;

    float acc = src[r * pc.W + c] * pc.w[0];
    if (pc.dir == 0) {
        int roff = r * pc.W;
        for (int k = 1; k <= pc.radius; ++k) {
            acc += (src[roff + max(c - k, 0)] + src[roff + min(c + k, pc.W - 1)]) * pc.w[k];
        }
    } else {
        for (int k = 1; k <= pc.radius; ++k) {
            acc += (src[max(r - k, 0) * pc.W + c] + src[min(r + k, pc.H - 1) * pc.W + c]) * pc.w[k];
        }
    }
    dst[r * pc.W + c] = acc;
}
"""

SHADERS["k_gat_inv_quad"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float z_den[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float dv_map[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float g_sr2x[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float g_at_quad[]; };
layout(set = 0, binding = 4) readonly buffer B4 { float z_gat[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh;
    float sqrt_c, g_eff, ped;
} pc;

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    int H2 = pc.H1 * 2, W2 = pc.W1 * 2;
    int rh = clamp(Y >> 1, 0, pc.Hh - 1);
    int ch = clamp(X >> 1, 0, pc.Wh - 1);
    float dv = dv_map[rh * pc.Wh + ch];

    float zg[4][4];
    for (int dr = 0; dr < 4; ++dr) {
        int rr = clamp(2 * Y - 1 + dr, 0, H2 - 1);
        for (int dc = 0; dc < 4; ++dc) {
            int cc = clamp(2 * X - 1 + dc, 0, W2 - 1);
            zg[dr][dc] = z_gat[rr * W2 + cc];
        }
    }

    int i00 = (2 * Y) * W2 + (2 * X);
    int i01 = i00 + 1;
    int i10 = i00 + W2;
    int i11 = i10 + 1;

    float lo00 = zg[0][0], hi00 = lo00;
    float lo01 = zg[0][1], hi01 = lo01;
    float lo10 = zg[1][0], hi10 = lo10;
    float lo11 = zg[1][1], hi11 = lo11;
    for (int dy = 0; dy < 3; ++dy) {
        for (int dx = 0; dx < 3; ++dx) {
            float v00 = zg[0 + dy][0 + dx]; lo00 = min(lo00, v00); hi00 = max(hi00, v00);
            float v01 = zg[0 + dy][1 + dx]; lo01 = min(lo01, v01); hi01 = max(hi01, v01);
            float v10 = zg[1 + dy][0 + dx]; lo10 = min(lo10, v10); hi10 = max(hi10, v10);
            float v11 = zg[1 + dy][1 + dx]; lo11 = min(lo11, v11); hi11 = max(hi11, v11);
        }
    }

    float z00 = clamp(z_den[i00], lo00, hi00);
    float z01 = clamp(z_den[i01], lo01, hi01);
    float z10 = clamp(z_den[i10], lo10, hi10);
    float z11 = clamp(z_den[i11], lo11, hi11);
    float g00 = pc.sqrt_c * z00 + 0.25 * pc.g_eff * (z00 * z00 + dv) - pc.ped;
    float g01 = pc.sqrt_c * z01 + 0.25 * pc.g_eff * (z01 * z01 + dv) - pc.ped;
    float g10 = pc.sqrt_c * z10 + 0.25 * pc.g_eff * (z10 * z10 + dv) - pc.ped;
    float g11 = pc.sqrt_c * z11 + 0.25 * pc.g_eff * (z11 * z11 + dv) - pc.ped;

    g_sr2x[i00] = g00;
    g_sr2x[i01] = g01;
    g_sr2x[i10] = g10;
    g_sr2x[i11] = g11;
    g_at_quad[Y * pc.W1 + X] = 0.5 * (g01 + g10);
}
"""

SHADERS["k_median3x3"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src[]; };
layout(set = 0, binding = 1) writeonly buffer B1 { float dst[]; };
layout(push_constant) uniform PC { int H, W; } pc;

#define PIX_SORT(a, b) { float t = min(a, b); b = max(a, b); a = t; }

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;

    int rm = max(r - 1, 0), rp = min(r + 1, pc.H - 1);
    int cm = max(c - 1, 0), cp = min(c + 1, pc.W - 1);

    float p0 = src[rm * pc.W + cm], p1 = src[rm * pc.W + c], p2 = src[rm * pc.W + cp];
    float p3 = src[r  * pc.W + cm], p4 = src[r  * pc.W + c], p5 = src[r  * pc.W + cp];
    float p6 = src[rp * pc.W + cm], p7 = src[rp * pc.W + c], p8 = src[rp * pc.W + cp];

    PIX_SORT(p1, p2); PIX_SORT(p4, p5); PIX_SORT(p7, p8);
    PIX_SORT(p0, p1); PIX_SORT(p3, p4); PIX_SORT(p6, p7);
    PIX_SORT(p1, p2); PIX_SORT(p4, p5); PIX_SORT(p7, p8);
    PIX_SORT(p0, p3); PIX_SORT(p5, p8); PIX_SORT(p4, p7);
    PIX_SORT(p3, p6); PIX_SORT(p1, p4); PIX_SORT(p2, p5);
    PIX_SORT(p4, p7); PIX_SORT(p4, p2); PIX_SORT(p6, p4);
    PIX_SORT(p4, p2);
    dst[r * pc.W + c] = p4;
}
"""

SHADERS["k_glint_desat"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float g_at_quad[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float medG[]; };
layout(set = 0, binding = 2) buffer B2 { float dR[]; };
layout(set = 0, binding = 3) buffer B3 { float dB[]; };
layout(push_constant) uniform PC { int H, W; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;
    int i = r * pc.W + c;
    float g = g_at_quad[i];
    float g_bg = medG[i];
    float dr = dR[i], db = dB[i];
    float spike_g = max(0.0, g - g_bg);
    float glint_exc = (spike_g > 0.015) ? (spike_g + 0.35 * max(0.0, max(dr, db))) : 0.0;
    float hl_lvl = max(g, max(g + dr, g + db));
    float desat = max(clamp((glint_exc - 0.045) / 0.12, 0.0, 0.92),
                      clamp((hl_lvl - 0.94) / 0.10, 0.0, 0.85));
    if (desat > 0.0) {
        dR[i] = dr * (1.0 - desat);
        dB[i] = db * (1.0 - desat);
    }
}
"""

SHADERS["k_atrous_1d"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float inR[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float inB[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float guide_G[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float snr_boost[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float outR[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float outB[]; };

layout(push_constant) uniform PC {
    int H, W, Hh, Wh, dir, step;
    float inv_sr2, inv_sb2, inv_sg2;
} pc;

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;

    int i = r * pc.W + c;
    float boost = snr_boost[clamp(r >> 1, 0, pc.Hh - 1) * pc.Wh + clamp(c >> 1, 0, pc.Wh - 1)];
    float inv_b2 = 1.0 / (boost * boost);
    float isr2 = pc.inv_sr2 * inv_b2;
    float isb2 = pc.inv_sb2 * inv_b2;

    float cr0 = inR[i], cb0 = inB[i], cg0 = guide_G[i];
    const float k1d[5] = float[5](1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0);
    float wr_s = 0.0, vr_s = 0.0, wb_s = 0.0, vb_s = 0.0;

    for (int k = -2; k <= 2; ++k) {
        int ki = (pc.dir == 0)
            ? (r * pc.W + clamp(c + k * pc.step, 0, pc.W - 1))
            : (clamp(r + k * pc.step, 0, pc.H - 1) * pc.W + c);
        float dg = guide_G[ki] - cg0;
        float wg = k1d[k + 2] * fast_exp_neg(dg * dg * pc.inv_sg2);
        float vr = inR[ki], dr = vr - cr0;
        float wr = wg * fast_exp_neg(dr * dr * isr2);
        wr_s += wr; vr_s += wr * vr;
        float vb = inB[ki], db = vb - cb0;
        float wb = wg * fast_exp_neg(db * db * isb2);
        wb_s += wb; vb_s += wb * vb;
    }
    outR[i] = vr_s / max(wr_s, 1e-12);
    outB[i] = vb_s / max(wb_s, 1e-12);
}
"""

SHADERS["k_assemble_rgb"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float g_sr2x[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float dR_1x[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float dB_1x[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float outR[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float outG[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float outB[]; };

layout(push_constant) uniform PC {
    int H1, W1, H2, W2;
    float inv_wb_r, inv_wb_b;
} pc;

float bilerp_d(int which_b, float y, float x) {
    y = clamp(y, 0.0, float(pc.H1 - 1));
    x = clamp(x, 0.0, float(pc.W1 - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, pc.H1 - 1), x1 = min(x0 + 1, pc.W1 - 1);
    float fy = y - float(y0), fx = x - float(x0);
    if (which_b == 0) {
        return mix(mix(dR_1x[y0 * pc.W1 + x0], dR_1x[y0 * pc.W1 + x1], fx),
                   mix(dR_1x[y1 * pc.W1 + x0], dR_1x[y1 * pc.W1 + x1], fx), fy);
    } else {
        return mix(mix(dB_1x[y0 * pc.W1 + x0], dB_1x[y0 * pc.W1 + x1], fx),
                   mix(dB_1x[y1 * pc.W1 + x0], dB_1x[y1 * pc.W1 + x1], fx), fy);
    }
}

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W2 || r >= pc.H2) return;

    int i = r * pc.W2 + c;
    float g = g_sr2x[i];
    float dr_wb = bilerp_d(0, 0.5 * float(r), 0.5 * float(c));
    float db_wb = bilerp_d(1, 0.5 * float(r - 1), 0.5 * float(c - 1));
    outR[i] = max(0.0, (g + dr_wb) * pc.inv_wb_r);
    outG[i] = max(0.0, g);
    outB[i] = max(0.0, (g + db_wb) * pc.inv_wb_b);
}
"""

SHADERS["k_rl_lum"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float R[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float G[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float B[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float lum0[]; };
layout(push_constant) uniform PC { uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    lum0[i] = max(1e-6, 0.2126 * R[i] + 0.7152 * G[i] + 0.0722 * B[i]);
}
"""

SHADERS["k_rl_prep_h"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float lum0[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float conv[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float edge_gate[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { uint hminmax_f16[]; };
layout(set = 0, binding = 4) readonly buffer B4 { uint rl_mod_f16[]; };
layout(push_constant) uniform PC { int H, W, Hh, Wh; float g_thr2; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;
    int rm = max(r - 1, 0), rp = min(r + 1, pc.H - 1);
    int cm = max(c - 1, 0), cp = min(c + 1, pc.W - 1);
    int i = r * pc.W + c;

    int rh = clamp(r >> 2, 0, pc.Hh - 1);
    int ch = clamp(c >> 2, 0, pc.Wh - 1);
    vec2 rlm = unpackHalf2x16(rl_mod_f16[rh * pc.Wh + ch]); // .x = rl_gain, .y = thr_mul

    float gx = 0.5 * (conv[r * pc.W + cp] - conv[r * pc.W + cm]);
    float gy = 0.5 * (conv[rp * pc.W + c] - conv[rm * pc.W + c]);
    float lap = conv[rm * pc.W + c] + conv[rp * pc.W + c] + conv[r * pc.W + cm] + conv[r * pc.W + cp] - 4.0 * conv[i];
    float se2 = gx * gx + gy * gy + 0.35 * lap * lap;
    edge_gate[i] = rlm.x * (se2 / (se2 + pc.g_thr2 * rlm.y));

    float hmin = lum0[i], hmax = lum0[i];
    int x0 = max(0, c - 4), x1 = min(pc.W - 1, c + 4);
    for (int cx = x0; cx <= x1; ++cx) {
        float v = lum0[r * pc.W + cx];
        hmin = min(hmin, v);
        hmax = max(hmax, v);
    }
    hminmax_f16[i] = packHalf2x16(vec2(hmin, hmax));
}
"""

SHADERS["k_rl_prep_v"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float lum0[]; };
layout(set = 0, binding = 1) readonly buffer B1 { uint hminmax_in[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { uint bounds_out[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float est[]; };
layout(push_constant) uniform PC { int H, W, is_astro; } pc;
void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.W || r >= pc.H) return;
    int y0 = max(0, r - 4), y1 = min(pc.H - 1, r + 4);
    vec2 mm0 = unpackHalf2x16(hminmax_in[y0 * pc.W + c]);
    float vmin = mm0.x, vmax = mm0.y;
    for (int ry = y0 + 1; ry <= y1; ++ry) {
        vec2 mm = unpackHalf2x16(hminmax_in[ry * pc.W + c]);
        vmin = min(vmin, mm.x);
        vmax = max(vmax, mm.y);
    }
    float span = vmax - vmin;
    int i = r * pc.W + c;
    float l0 = lum0[i];
    float lo_b = (pc.is_astro != 0) ? max(1e-6, max(vmin, l0 - 0.02 * max(0.0, l0 - vmin))) : max(1e-6, vmin - 0.02 * span);
    float hi_b = (pc.is_astro != 0) ? (vmax + 0.06 * span) : (vmax + 0.02 * span);
    bounds_out[i] = packHalf2x16(vec2(lo_b, hi_b));
    est[i] = l0;
}
"""

SHADERS["k_rl_ratio"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float R[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float G[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float B[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float edge_gate[]; };
layout(set = 0, binding = 4) buffer B4 { float conv[]; };
layout(push_constant) uniform PC { uint count; float snr_knee, clamp_delta; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    float l0 = max(1e-6, 0.2126 * R[i] + 0.7152 * G[i] + 0.0722 * B[i]);
    float d = l0 / max(conv[i], 1e-6) - 1.0;
    float snr_w = (l0 / (l0 + pc.snr_knee)) * edge_gate[i];
    conv[i] = 1.0 + clamp(d, -pc.clamp_delta, pc.clamp_delta) * snr_w;
}
"""

SHADERS["k_rl_update"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) buffer B0 { float est[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float corr[]; };
layout(set = 0, binding = 2) readonly buffer B2 { uint bounds_f16[]; };
layout(push_constant) uniform PC { uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    vec2 b = unpackHalf2x16(bounds_f16[i]);
    est[i] = clamp(est[i] * corr[i], b.x, b.y);
}
"""

SHADERS["k_rl_apply"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) buffer B0 { float R[]; };
layout(set = 0, binding = 1) buffer B1 { float G[]; };
layout(set = 0, binding = 2) buffer B2 { float B[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float est[]; };
layout(push_constant) uniform PC { uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    float r = R[i], g = G[i], b = B[i];
    float l0 = max(1e-6, 0.2126 * r + 0.7152 * g + 0.0722 * b);
    float gain = est[i] / l0;
    R[i] = max(0.0, r * gain);
    G[i] = max(0.0, g * gain);
    B[i] = max(0.0, b * gain);
}
"""

SHADERS["k_sensor_residual"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float raw_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flow_in[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float scene_in[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float residual_out[]; };

layout(push_constant) uniform PC {
    int H, W, Hh, Wh;
    float read_variance, shot;
    uint cfa0, cfa1, cfa2;
} pc;

uint cfa_at_sr(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

vec4 sample_flow_sr(float y, float x) {
    y = clamp(y, 0.0, float(pc.Hh - 1));
    x = clamp(x, 0.0, float(pc.Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, pc.Hh - 1), x1 = min(x0 + 1, pc.Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(flow_in[y0 * pc.Wh + x0], flow_in[y0 * pc.Wh + x1], fx),
               mix(flow_in[y1 * pc.Wh + x0], flow_in[y1 * pc.Wh + x1], fx), fy);
}

float sensor_scene(float y, float x, int ch) {
    y = clamp(y, 0.0, float(pc.Hh - 1));
    x = clamp(x, 0.0, float(pc.Wh - 1));
    int r = int(floor(y)), c = int(floor(x));
    int r1 = min(r + 1, pc.Hh - 1), c1 = min(c + 1, pc.Wh - 1);
    float ty = y - float(r), tx = x - float(c);
    return (1.0 - ty) * ((1.0 - tx) * scene_in[(r * pc.Wh + c) * 3 + ch] + tx * scene_in[(r * pc.Wh + c1) * 3 + ch]) +
           ty * ((1.0 - tx) * scene_in[(r1 * pc.Wh + c) * 3 + ch] + tx * scene_in[(r1 * pc.Wh + c1) * 3 + ch]);
}

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (Y >= pc.H || X >= pc.W) return;
    int i = Y * pc.W + X;
    residual_out[i] = uintBitsToFloat(0x7fc00000u);
    float y = float(Y), x = float(X);
    for (int k = 0; k < 5; ++k) {
        vec4 f = sample_flow_sr(y * 0.5, x * 0.5);
        float yy = float(Y) - 2.0 * f.x;
        float xx = float(X) - 2.0 * f.y;
        bool settled = (yy == y && xx == x);
        y = yy; x = xx;
        if (settled) break;
    }
    if (isnan(y) || isinf(y) || isnan(x) || isinf(x) ||
        y < 0.0 || x < 0.0 || y > float(pc.H - 1) || x > float(pc.W - 1)) return;
    if (sample_flow_sr(y * 0.5, x * 0.5).z < 0.7) return;
    int ch = int(cfa_at_sr(Y, X));
    float estimate = sensor_scene(y * 0.5, x * 0.5, ch);
    float v = raw_in[i];
    if (isnan(v + estimate) || isinf(v + estimate) || estimate >= 0.15) return;
    float sigma = sqrt(max(1e-12, pc.read_variance + pc.shot * max(0.0, estimate)));
    float gx = 0.5 * (sensor_scene(y * 0.5, (x + 1.0) * 0.5, ch) -
                      sensor_scene(y * 0.5, (x - 1.0) * 0.5, ch));
    float gy = 0.5 * (sensor_scene((y + 1.0) * 0.5, x * 0.5, ch) -
                      sensor_scene((y - 1.0) * 0.5, x * 0.5, ch));
    if (gx * gx + gy * gy >= 4.0 * sigma * sigma) return;
    residual_out[i] = v - estimate;
}
"""

SHADERS["k_v3_prep_guides"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { vec4 icov_guide[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float c_anc_1x[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float snr_boost[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { vec4 v3_guides[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh, cov_off;
    float sig0_ref, gain_ref;
    uint cfa0, cfa1, cfa2;
} pc;

uint cfa_at(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

float chroma_anchor(int y, int x, uint ch) {
    float sum = 0.0, weights = 0.0;
    int r0 = max(0, y - 3), r1 = min(pc.H1 - 1, y + 3);
    int c0 = max(0, x - 3), c1 = min(pc.W1 - 1, x + 3);
    for (int r = r0; r <= r1; ++r) {
        int dy2 = (r - y) * (r - y);
        for (int c = c0; c <= c1; ++c) {
            if (cfa_at(r, c) != ch) continue;
            float w = 1.0 / (0.25 + float(dy2 + (c - x) * (c - x)));
            sum += w * c_anc_1x[r * pc.W1 + c];
            weights += w;
        }
    }
    return sum / max(weights, 1e-12);
}

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int r = int(gl_GlobalInvocationID.y);
    if (c >= pc.Wh || r >= pc.Hh) return;

    int hi = r * pc.Wh + c;
    vec4 ic = icov_guide[hi];
    float gx = icov_guide[r * pc.Wh + min(c + 1, pc.Wh - 1)].w - icov_guide[r * pc.Wh + max(c - 1, 0)].w;
    float gy = icov_guide[min(r + 1, pc.Hh - 1) * pc.Wh + c].w - icov_guide[max(r - 1, 0) * pc.Wh + c].w;
    float grad_sq = 0.25 * (gx * gx + gy * gy);
    float v_loc = max(1e-10, 2.0 * (pc.sig0_ref + pc.gain_ref * max(0.0, ic.w)));
    float snr2 = grad_sq / v_loc;
    float gamma = snr2 / (1.0 + snr2);
    float iso = 0.5 * (ic.x + ic.z);
    v3_guides[pc.cov_off + hi] = vec4(iso + gamma * (ic.x - iso), gamma * ic.y, iso + gamma * (ic.z - iso), ic.w);

    float ar = chroma_anchor(min(2 * r, pc.H1 - 1), min(2 * c, pc.W1 - 1), 0u);
    float ab = chroma_anchor(min(2 * r + 1, pc.H1 - 1), min(2 * c + 1, pc.W1 - 1), 2u);
    v3_guides[pc.cov_off + pc.Hh * pc.Wh + hi] = vec4(ar, ab, snr_boost[hi], 0.0);
}
"""

SHADERS["k_v3_green"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float frames_all[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flows_all[]; };
layout(set = 0, binding = 2) readonly buffer B2 { vec4 v3_guides[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float g0_ha_1x[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float green_out[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float fcomp_eta_out[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh, Ew, Eh, N, out_scale;
    int origin_y, origin_x, is_bayer, is_clean_plate;
    int is_virtual_nd, is_aeb, any_rob, fcomp_off_eta;
    uint cfa0, cfa1, cfa2;
} pc;

uint cfa_at(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

float eval_nlt_sq(float s0, float s1, float s2, float iv) {
    float x = clamp(iv, 0.0, 1.0);
    return max(0.0, s0 + x * (s1 + x * s2));
}

float sat_taper(float v, float lo, float inv_span) {
    float t = (v - lo) * inv_span;
    if (t >= 1.0) return 1e-4;
    return max(1e-4, 0.5 * (1.0 + cos(3.14159265 * t)));
}

vec4 sample_half(int base, float y1, float x1) {
    if (pc.Hh < 2 || pc.Wh < 2) return flows_all[base];
    float yc = clamp(0.5 * y1 - 0.25, 0.0, float(pc.Hh - 1));
    float xc = clamp(0.5 * x1 - 0.25, 0.0, float(pc.Wh - 1));
    int y0 = clamp(int(yc), 0, pc.Hh - 2);
    int x0 = clamp(int(xc), 0, pc.Wh - 2);
    int y1i = min(y0 + 1, pc.Hh - 1);
    int x1i = min(x0 + 1, pc.Wh - 1);
    float fy = clamp(yc - float(y0), 0.0, 1.0);
    float fx = clamp(xc - float(x0), 0.0, 1.0);
    int idx0 = base + y0 * pc.Wh + x0;
    int idx1 = base + y1i * pc.Wh + x0;
    vec4 v0 = mix(flows_all[idx0], flows_all[idx0 + 1], fx);
    vec4 v1 = mix(flows_all[idx1], flows_all[idx1 + 1], fx);
    return mix(v0, v1, fy);
}

float bilerp_ha(float y, float x) {
    y = clamp(y, 0.0, float(pc.H1 - 1));
    x = clamp(x, 0.0, float(pc.W1 - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1i = min(y0 + 1, pc.H1 - 1), x1i = min(x0 + 1, pc.W1 - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(g0_ha_1x[y0 * pc.W1 + x0], g0_ha_1x[y0 * pc.W1 + x1i], fx),
               mix(g0_ha_1x[y1i * pc.W1 + x0], g0_ha_1x[y1i * pc.W1 + x1i], fx), fy);
}

void main() {
    int X1 = int(gl_GlobalInvocationID.x);
    int Y1 = int(gl_GlobalInvocationID.y);
    if (X1 >= pc.W1 || Y1 >= pc.H1) return;

    bool eta_only = (pc.fcomp_off_eta < 0);
    bool is_eta_site = (((Y1 | X1) & 3) == 0);
    if (eta_only && !is_eta_site) return;

    vec4 c0 = v3_guides[0];
    vec4 c2 = v3_guides[2];
    vec4 c3 = v3_guides[3];
    float sig0_ref = c0.x, gain_ref = c0.y, sat0 = c0.z;
    float q_luma_var = c2.z;
    float q_nlt_s0 = c3.x, q_nlt_s1 = c3.y, q_nlt_s2 = c3.z;

    int cov_off = 4 + 3 * pc.N;
    int hi = clamp(Y1 >> 1, 0, max(0, pc.Hh - 1)) * max(1, pc.Wh) + clamp(X1 >> 1, 0, max(0, pc.Wh - 1));
    vec4 cg = v3_guides[cov_off + hi];
    float scale_g = (pc.out_scale == 2) ? 1.0 : 0.72;
    float axx = scale_g * cg.x, axy = scale_g * cg.y, ayy = scale_g * cg.z;
    float tr_a = axx + ayy, df_a = axx - ayy;
    float disc_a = sqrt(max(0.0, df_a * df_a + 4.0 * axy * axy));
    float anis = disc_a / max(1e-6, tr_a);
    bool use_curv = (pc.N >= 3) && (pc.is_clean_plate == 0) && (pc.is_virtual_nd == 0) && (anis > 0.25);
    float nx_c = 1.0, ny_c = 0.0;
    if (use_curv) {
        float vx = (df_a >= 0.0) ? (df_a + disc_a) : (2.0 * axy);
        float vy = (df_a >= 0.0) ? (2.0 * axy) : (disc_a - df_a);
        float inv_vn = 1.0 / sqrt(max(1e-18, vx * vx + vy * vy));
        nx_c = vx * inv_vn; ny_c = vy * inv_vn;
    }
    float anchor = cg.w;
    float a_pos = max(0.0, anchor);
    float v_ref = sig0_ref + gain_ref * a_pos;
    float nlt_sq = (q_luma_var > 0.0) ? eval_nlt_sq(q_nlt_s0, q_nlt_s1, q_nlt_s2, a_pos) : 0.0;
    float thr_base = (pc.is_aeb == 0 && pc.any_rob != 0)
        ? (3.5 * sqrt(max(0.0, v_ref + q_luma_var * nlt_sq)) + 0.008) : 0.0;

    int g_col0 = (cfa_at(0, 0) == 1u) ? 0 : 1;
    int g_col1 = (cfa_at(1, 0) == 1u) ? 0 : 1;
    int p_cnt = (eta_only || pc.out_scale == 1) ? 1 : 4;
    float max_ha_blend = (pc.out_scale == 1) ? 0.85 : 0.65;

    float S0[4], Sx[4], Sy[4], Sxx[4], Sxy[4], Syy[4], T0[4], Tx[4], Ty[4], Sw2[4], vmin[4], vmax[4];
    float Sqx[4], Sqy[4], Sqq[4], Tq[4];
    bool has_nat[4];
    float y_base[4], x_base[4];
    for (int p = 0; p < 4; ++p) {
        S0[p] = 0.0; Sx[p] = 0.0; Sy[p] = 0.0;
        Sxx[p] = 0.0; Sxy[p] = 0.0; Syy[p] = 0.0;
        Sqx[p] = 0.0; Sqy[p] = 0.0; Sqq[p] = 0.0; Tq[p] = 0.0;
        T0[p] = 0.0; Tx[p] = 0.0; Ty[p] = 0.0;
        Sw2[p] = 0.0; vmin[p] = 1e30; vmax[p] = -1e30;
        has_nat[p] = false;
        y_base[p] = float(Y1) + ((p >= 2) ? 0.5 : 0.0);
        x_base[p] = float(X1) + (((p & 1) != 0) ? 0.5 : 0.0);
    }

    int n_1x = pc.H1 * pc.W1;
    int n_half = pc.Hh * pc.Wh;

    for (int n = 0; n < pc.N; ++n) {
        vec4 fp0 = v3_guides[4 + 3 * n + 0];
        vec4 fp1 = v3_guides[4 + 3 * n + 1];
        float fw = fp0.x;
        if (fw <= 0.0) continue;
        float rv_n = fp0.y, sh_n = fp0.z, sat_lo_n = fp0.w;
        float sat_ispan_n = fp1.x;
        bool use_rob = (fp1.y > 0.5);
        bool unit_m = (pc.is_virtual_nd != 0) || (fp1.z > 0.5);
        bool has_fl = (fp1.w > 0.5);

        int fbase = n * n_1x;
        int flbase = n * n_half;

        float sy[4], sx[4], mwp[4];
        uint act_mask = 0u;
        for (int p = 0; p < p_cnt; ++p) {
            vec4 fm = (unit_m && !has_fl) ? vec4(0.0, 0.0, 1.0, 0.0) : sample_half(flbase, y_base[p], x_base[p]);
            float mw = (unit_m ? 1.0 : fm.z) * fw;
            mwp[p] = mw;
            if (mw < 0.01) continue;
            float dy = has_fl ? (2.0 * fm.x) : 0.0;
            float dx = has_fl ? (2.0 * fm.y) : 0.0;
            precise float syv = (float(Y1 + pc.origin_y) + (y_base[p] - float(Y1)) + dy) - float(pc.origin_y);
            precise float sxv = (float(X1 + pc.origin_x) + (x_base[p] - float(X1)) + dx) - float(pc.origin_x);
            sy[p] = syv; sx[p] = sxv;
            if (syv >= -0.5 && syv <= float(pc.H1) - 0.5 && sxv >= -0.5 && sxv <= float(pc.W1) - 0.5) {
                act_mask |= (1u << uint(p));
            }
        }
        if (act_mask == 0u) continue;

        float inv_var_rel = 1.0, inv_wvar = 1.0;
        float thr_n = thr_base;
        if (pc.is_aeb != 0 && pc.is_virtual_nd == 0) {
            float v_n = rv_n + sh_n * a_pos;
            if (v_ref > 1e-12 && v_n > 1e-12) {
                inv_var_rel = clamp(v_ref / v_n, 0.004, 64.0);
                inv_wvar = 1.0 / inv_var_rel;
            }
            if (use_rob) thr_n = 3.5 * sqrt(max(0.0, v_n + q_luma_var * nlt_sq)) + 0.008;
        }

        if (pc.is_clean_plate == 0 && pc.is_virtual_nd == 0) {
            for (int p = 0; p < p_cnt; ++p) {
                if ((act_mask & (1u << uint(p))) == 0u) continue;
                int ryi = int(floor(sy[p] + 0.5));
                int rxi = int(floor(sx[p] + 0.5));
                if (ryi >= 0 && ryi < pc.H1 && rxi >= 0 && rxi < pc.W1) {
                    if (cfa_at(ryi, rxi) == 1u) {
                        float yy = float(ryi) - sy[p];
                        float xx = float(rxi) - sx[p];
                        if (abs(yy) < 0.02 && abs(xx) < 0.02) {
                            float v = frames_all[fbase + ryi * pc.W1 + rxi];
                            float st = (v > sat_lo_n) ? sat_taper(v, sat_lo_n, sat_ispan_n) : 1.0;
                            float rob = 1.0;
                            if (use_rob && (anchor < 0.82 * sat0 || v < 0.82 * sat0)) {
                                float ad = abs(v - anchor);
                                if (ad > thr_n) { float q = thr_n / ad; rob = q * q * q; }
                            }
                            float d = axx * xx * xx + 2.0 * axy * xx * yy + ayy * yy * yy;
                            float w = mwp[p] * inv_var_rel * st * rob * fast_exp_neg(0.5 * d);
                            if (w > 1e-12) {
                                S0[p] += w; Sx[p] += w * xx; Sy[p] += w * yy;
                                Sxx[p] += w * xx * xx; Sxy[p] += w * xx * yy; Syy[p] += w * yy * yy;
                                if (use_curv) {
                                    float un = nx_c * xx + ny_c * yy;
                                    float q = 0.5 * un * un;
                                    Sqx[p] += w * q * xx; Sqy[p] += w * q * yy;
                                    Sqq[p] += w * q * q; Tq[p] += w * q * v;
                                }
                                T0[p] += w * v; Tx[p] += w * xx * v; Ty[p] += w * yy * v;
                                Sw2[p] += w * w * inv_wvar;
                                vmin[p] = min(vmin[p], v);
                                vmax[p] = max(vmax[p], v);
                                if (st > 0.1 && rob > 0.1) has_nat[p] = true;
                            }
                            act_mask &= ~(1u << uint(p));
                        }
                    }
                }
            }
            if (act_mask == 0u) continue;
        }

        float min_sy = 1e9, max_sy = -1e9, min_sx = 1e9, max_sx = -1e9;
        for (int p = 0; p < p_cnt; ++p) {
            if ((act_mask & (1u << uint(p))) == 0u) continue;
            min_sy = min(min_sy, sy[p]); max_sy = max(max_sy, sy[p]);
            min_sx = min(min_sx, sx[p]); max_sx = max(max_sx, sx[p]);
        }
        int r_lo = max(0, int(floor(min_sy)) - 1);
        int r_hi = min(pc.H1 - 1, int(floor(max_sy)) + 2);
        int c_lo = max(0, int(floor(min_sx)) - 1);
        int c_hi = min(pc.W1 - 1, int(floor(max_sx)) + 2);

        for (int rr = r_lo; rr <= r_hi; ++rr) {
            int cc_start = c_lo;
            int cc_step = 1;
            if (pc.is_bayer != 0) {
                int par = ((rr & 1) == 0) ? g_col0 : g_col1;
                if ((cc_start & 1) != par) ++cc_start;
                cc_step = 2;
            }
            for (int cc = cc_start; cc <= c_hi; cc += cc_step) {
                if (pc.is_bayer == 0 && cfa_at(rr, cc) != 1u) continue;
                float v = frames_all[fbase + rr * pc.W1 + cc];
                float st = (v > sat_lo_n) ? sat_taper(v, sat_lo_n, sat_ispan_n) : 1.0;
                float rob = 1.0;
                if (use_rob && (anchor < 0.82 * sat0 || v < 0.82 * sat0)) {
                    float ad = abs(v - anchor);
                    if (ad > thr_n) { float q = thr_n / ad; rob = q * q * q; }
                }
                float w_base = inv_var_rel * st * rob;
                for (int p = 0; p < p_cnt; ++p) {
                    if ((act_mask & (1u << uint(p))) == 0u) continue;
                    float yy = float(rr) - sy[p];
                    float xx = float(cc) - sx[p];
                    float d = axx * xx * xx + 2.0 * axy * xx * yy + ayy * yy * yy;
                    if (d > 6.8) continue;
                    float w = mwp[p] * w_base * fast_exp_neg(0.5 * d);
                    if (w <= 1e-12) continue;
                    S0[p] += w; Sx[p] += w * xx; Sy[p] += w * yy;
                    Sxx[p] += w * xx * xx; Sxy[p] += w * xx * yy; Syy[p] += w * yy * yy;
                    if (use_curv) {
                        float un = nx_c * xx + ny_c * yy;
                        float q = 0.5 * un * un;
                        Sqx[p] += w * q * xx; Sqy[p] += w * q * yy;
                        Sqq[p] += w * q * q; Tq[p] += w * q * v;
                    }
                    T0[p] += w * v; Tx[p] += w * xx * v; Ty[p] += w * yy * v;
                    Sw2[p] += w * w * inv_wvar;
                    vmin[p] = min(vmin[p], v);
                    vmax[p] = max(vmax[p], v);
                }
            }
        }
    }

    int W_out = pc.W1 * pc.out_scale;
    for (int p = 0; p < p_cnt; ++p) {
        float phi_fisher = 0.0, eta_px = 1.0, g_val = 0.0;
        float ha_val = (pc.out_scale == 1) ? g0_ha_1x[Y1 * pc.W1 + X1] : bilerp_ha(y_base[p], x_base[p]);
        if (S0[p] > 1e-8) {
            float inv_s0 = 1.0 / S0[p];
            float g_nw = T0[p] * inv_s0;
            float eta_nw = clamp(Sw2[p] * inv_s0 * inv_s0, 1e-4, 1.0);
            float mx = Sx[p] * inv_s0, my = Sy[p] * inv_s0;
            float F00 = max(0.0, Sxx[p] - Sx[p] * mx);
            float F01 = Sxy[p] - Sx[p] * my;
            float F11 = max(0.0, Syy[p] - Sy[p] * my);
            float tr_f = (F00 + F11) * inv_s0;
            float df_f = (F00 - F11) * inv_s0;
            float s01 = (2.0 * F01) * inv_s0;
            float disc = sqrt(max(0.0, df_f * df_f + s01 * s01));
            float lam_min = max(0.0, 0.5 * (tr_f - disc));
            phi_fisher = clamp(lam_min / 0.16, 0.0, 1.0) * clamp(S0[p] / 3.0, 0.0, 1.0);

            float reg = 0.02 * S0[p];
            float F00_r = F00 + reg, F11_r = F11 + reg;
            float det_r = max(1e-12, F00_r * F11_r - F01 * F01);
            float rx_t = Tx[p] - mx * T0[p], ry_t = Ty[p] - my * T0[p];
            float bx = (F11_r * rx_t - F01 * ry_t) / det_r;
            float by = (F00_r * ry_t - F01 * rx_t) / det_r;
            float q_lev = max(0.0, S0[p] * (F11_r * mx * mx - 2.0 * F01 * mx * my + F00_r * my * my) / det_r);
            float lev_cap = (q_lev > 0.5) ? sqrt(0.5 / q_lev) : 1.0;
            float alpha_ll = phi_fisher * lev_cap;
            float delta_g = -alpha_ll * (bx * mx + by * my);
            float extra_var = alpha_ll * alpha_ll * q_lev;
            if (use_curv && S0[p] > 2.0) {
                float Sq = 0.5 * (nx_c * nx_c * Sxx[p] + 2.0 * nx_c * ny_c * Sxy[p] + ny_c * ny_c * Syy[p]);
                float mu_q = Sq * inv_s0;
                float cqx = Sqx[p] - mu_q * Sx[p], cqy = Sqy[p] - mu_q * Sy[p];
                float cqq = max(0.0, Sqq[p] - mu_q * Sq);
                float rq = Tq[p] - mu_q * T0[p];
                float gx_q = (F11_r * cqx - F01 * cqy) / det_r;
                float gy_q = (F00_r * cqy - F01 * cqx) / det_r;
                float Dq = max(0.0, cqq - (gx_q * cqx + gy_q * cqy));
                float Dq_r = Dq + 0.04 * S0[p];
                float rq_orth = rq - (bx * cqx + by * cqy);
                float bq = rq_orth / Dq_r;
                float q0_orth = -mu_q + gx_q * mx + gy_q * my;
                float q_lev_q = max(0.0, S0[p] * q0_orth * q0_orth / Dq_r);
                float lev_cap_q = (q_lev_q > 0.35) ? sqrt(0.35 / q_lev_q) : 1.0;
                float gate_q = clamp((anis - 0.25) * 2.0, 0.0, 1.0) * clamp(Dq / (0.06 * S0[p]), 0.0, 1.0);
                float alpha_q = alpha_ll * gate_q * lev_cap_q;
                delta_g += alpha_q * bq * q0_orth;
                extra_var += alpha_q * alpha_q * q_lev_q;
            }
            g_val = clamp(g_nw + delta_g, vmin[p], vmax[p]);

            float a_ha = (has_nat[p] || pc.is_clean_plate != 0 || pc.is_virtual_nd != 0)
                ? 0.0 : clamp((1.5 - S0[p]) / 1.1, 0.0, max_ha_blend);
            if (a_ha > 1e-3) g_val += a_ha * (ha_val - g_val);
            eta_px = (1.0 - a_ha) * (1.0 - a_ha) * eta_nw * (1.0 + extra_var) + a_ha * a_ha * 0.55;
        } else {
            phi_fisher = 0.0;
            eta_px = 1.0;
            if (pc.is_clean_plate != 0 || pc.is_virtual_nd != 0) {
                g_val = g0_ha_1x[clamp(int(y_base[p] + 0.5), 0, pc.H1 - 1) * pc.W1 + clamp(int(x_base[p] + 0.5), 0, pc.W1 - 1)];
            } else {
                g_val = ha_val;
            }
        }
        int Yo = (pc.out_scale == 2) ? (2 * Y1 + (p >> 1)) : Y1;
        int Xo = (pc.out_scale == 2) ? (2 * X1 + (p & 1)) : X1;
        int out_idx = Yo * W_out + Xo;
        if (!eta_only) {
            green_out[out_idx] = g_val;
            if (pc.fcomp_off_eta > 0) fcomp_eta_out[out_idx] = 1.0 - phi_fisher;
        }
        if (p == 0 && is_eta_site) {
            int eta_base = (pc.fcomp_off_eta > 0) ? pc.fcomp_off_eta : 0;
            int ey = clamp(Y1 >> 2, 0, pc.Eh - 1);
            int ex = clamp(X1 >> 2, 0, pc.Ew - 1);
            fcomp_eta_out[eta_base + ey * pc.Ew + ex] = eta_px;
        }
    }
}
"""

SHADERS["k_v3_chroma"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float frames_all[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flows_all[]; };
layout(set = 0, binding = 2) readonly buffer B2 { vec4 v3_guides[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float green_in[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float dR_1x[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float dB_1x[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh, N, out_scale, full_H1, full_W1;
    int origin_y, origin_x, is_bayer, is_xtrans;
    int is_virtual_nd, is_aeb, any_rob, is_astro;
    uint cfa0, cfa1, cfa2;
} pc;

uint cfa_at(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

float eval_nlt_sq(float s0, float s1, float s2, float iv) {
    float x = clamp(iv, 0.0, 1.0);
    return max(0.0, s0 + x * (s1 + x * s2));
}

float sat_taper(float v, float lo, float inv_span) {
    float t = (v - lo) * inv_span;
    if (t >= 1.0) return 1e-4;
    return max(1e-4, 0.5 * (1.0 + cos(3.14159265 * t)));
}

vec4 sample_half(int base, float y1, float x1) {
    if (pc.Hh < 2 || pc.Wh < 2) return flows_all[base];
    float yc = clamp(0.5 * y1 - 0.25, 0.0, float(pc.Hh - 1));
    float xc = clamp(0.5 * x1 - 0.25, 0.0, float(pc.Wh - 1));
    int y0 = clamp(int(yc), 0, pc.Hh - 2);
    int x0 = clamp(int(xc), 0, pc.Wh - 2);
    int y1i = min(y0 + 1, pc.Hh - 1);
    int x1i = min(x0 + 1, pc.Wh - 1);
    float fy = clamp(yc - float(y0), 0.0, 1.0);
    float fx = clamp(xc - float(x0), 0.0, 1.0);
    int idx0 = base + y0 * pc.Wh + x0;
    int idx1 = base + y1i * pc.Wh + x0;
    vec4 v0 = mix(flows_all[idx0], flows_all[idx0 + 1], fx);
    vec4 v1 = mix(flows_all[idx1], flows_all[idx1 + 1], fx);
    return mix(v0, v1, fy);
}

float bilerp_g(float y, float x) {
    int Ho = pc.H1 * pc.out_scale, Wo = pc.W1 * pc.out_scale;
    y = clamp(y, 0.0, float(Ho - 1));
    x = clamp(x, 0.0, float(Wo - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1i = min(y0 + 1, Ho - 1), x1i = min(x0 + 1, Wo - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(green_in[y0 * Wo + x0], green_in[y0 * Wo + x1i], fx),
               mix(green_in[y1i * Wo + x0], green_in[y1i * Wo + x1i], fx), fy);
}

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    vec4 c0 = v3_guides[0], c1 = v3_guides[1], c2 = v3_guides[2], c3 = v3_guides[3];
    float sig0_ref = c0.x, gain_ref = c0.y, sat0 = c0.z, wb_r = c0.w;
    float wb_b = c1.x, hl_lim = c1.y, lca_rk1 = c1.z, lca_rk2 = c1.w;
    float lca_bk1 = c2.x, lca_bk2 = c2.y, q_chroma_var = c2.w;
    float q_nlt_s0 = c3.x, q_nlt_s1 = c3.y, q_nlt_s2 = c3.z;

    int cov_off = 4 + 3 * pc.N;
    int anc_off = cov_off + pc.Hh * pc.Wh;
    int hi = clamp(Y >> 1, 0, max(0, pc.Hh - 1)) * max(1, pc.Wh) + clamp(X >> 1, 0, max(0, pc.Wh - 1));
    vec4 cg = v3_guides[cov_off + hi];
    vec4 ab = v3_guides[anc_off + hi];
    float axx = 0.55 * cg.x, axy = 0.55 * cg.y, ayy = 0.55 * cg.z, g_gd = cg.w;

    int Wo = pc.W1 * pc.out_scale;
    float g_here = green_in[(Y * pc.out_scale) * Wo + (X * pc.out_scale)];
    float out_s_f = float(pc.out_scale);

    float opt_cy = 0.5 * float(pc.full_H1), opt_cx = 0.5 * float(pc.full_W1);
    float inv_opt_r = 1.0 / max(1.0, max(opt_cy, opt_cx));
    float ry = (float(Y) + float(pc.origin_y)) - opt_cy;
    float rx = (float(X) + float(pc.origin_x)) - opt_cx;
    float rn = (rx * inv_opt_r) * (rx * inv_opt_r) + (ry * inv_opt_r) * (ry * inv_opt_r);

    int rad_c = (pc.is_xtrans != 0) ? 3 : 2;
    float xt_scale = (pc.is_xtrans != 0) ? 0.58 : 1.0;
    float dlim_c = (pc.is_xtrans != 0) ? 9.5 : 7.5;
    int n_1x = pc.H1 * pc.W1;
    int n_half = pc.Hh * pc.Wh;

    for (int b = 0; b < 2; ++b) {
        float anchor = (b != 0) ? ab.y : ab.x;
        float c_pos = max(0.0, max(anchor, g_gd));
        float v_ref = sig0_ref + gain_ref * c_pos;
        float nlt_sq = (q_chroma_var > 0.0) ? eval_nlt_sq(q_nlt_s0, q_nlt_s1, q_nlt_s2, c_pos) : 0.0;
        float wb = (b != 0) ? wb_b : wb_r;
        float anchor_wb = anchor * wb;
        float thr_base_wb = (pc.is_aeb == 0 && pc.any_rob != 0)
            ? (wb * (4.0 * sqrt(max(0.0, v_ref + q_chroma_var * nlt_sq)) + 0.01)) : 0.0;
        float lca = (b != 0) ? (lca_bk1 + lca_bk2 * rn) : (lca_rk1 + lca_rk2 * rn);
        float lca_dy = lca * ry, lca_dx = lca * rx;
        uint target_ch = (b != 0) ? 2u : 0u;

        int rb_ry = -1, rb_rx = -1;
        if (pc.is_bayer != 0) {
            int cnt = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    if (cfa_at(dy, dx) == target_ch) { rb_ry = dy; rb_rx = dx; ++cnt; }
                }
            }
            if (cnt != 1) rb_ry = -1;
        }

        float S0 = 0.0, Sx = 0.0, Sy = 0.0, Sxx = 0.0, Sxy = 0.0, Syy = 0.0, T0 = 0.0, Tx = 0.0, Ty = 0.0;
        float dmin = 1e30, dmax = -1e30;

        for (int n = 0; n < pc.N; ++n) {
            vec4 fp0 = v3_guides[4 + 3 * n + 0];
            vec4 fp1 = v3_guides[4 + 3 * n + 1];
            vec4 fp2 = v3_guides[4 + 3 * n + 2];
            float fw = fp0.x;
            if (fw <= 0.0) continue;
            float rv_n = fp0.y, sh_n = fp0.z, sat_lo_n = fp0.w;
            float sat_ispan_n = fp1.x;
            bool use_rob = (fp1.y > 0.5);
            bool unit_m = (pc.is_virtual_nd != 0) || (fp1.z > 0.5);
            bool has_fl = (fp1.w > 0.5);

            vec4 fm = (unit_m && !has_fl) ? vec4(0.0, 0.0, 1.0, 0.0) : sample_half(n * n_half, float(Y), float(X));
            float mw = (unit_m ? 1.0 : fm.z) * fw;
            if (mw < 0.01) continue;

            float ois_y = (b != 0) ? fp2.z : fp2.x;
            float ois_x = (b != 0) ? fp2.w : fp2.y;
            float dy = (has_fl ? (2.0 * fm.x) : 0.0) + lca_dy + ois_y;
            float dx = (has_fl ? (2.0 * fm.y) : 0.0) + lca_dx + ois_x;
            precise float yt = (float(Y + pc.origin_y) + dy) - float(pc.origin_y);
            precise float xt = (float(X + pc.origin_x) + dx) - float(pc.origin_x);
            if (yt < -0.5 || yt > float(pc.H1) - 0.5 || xt < -0.5 || xt > float(pc.W1) - 0.5) continue;

            float inv_var_rel = 1.0;
            float thr_wb = thr_base_wb;
            if (pc.is_aeb != 0 && pc.is_virtual_nd == 0) {
                float v_n = rv_n + sh_n * c_pos;
                if (v_ref > 1e-12 && v_n > 1e-12) inv_var_rel = clamp(v_ref / v_n, 0.02, 32.0);
                if (use_rob) thr_wb = wb * (4.0 * sqrt(max(0.0, v_n + q_chroma_var * nlt_sq)) + 0.01);
            }

            int r_lo = max(0, int(floor(yt)) - (rad_c - 1));
            int r_hi = min(pc.H1 - 1, int(floor(yt)) + rad_c);
            int c_lo = max(0, int(floor(xt)) - (rad_c - 1));
            int c_hi = min(pc.W1 - 1, int(floor(xt)) + rad_c);
            int fbase = n * n_1x;

            int rr_start = r_lo, rr_step = 1, cc_start0 = c_lo, cc_step = 1;
            if (rb_ry >= 0) {
                if ((rr_start & 1) != rb_ry) ++rr_start;
                rr_step = 2;
                if ((cc_start0 & 1) != rb_rx) ++cc_start0;
                cc_step = 2;
            }
            for (int rr = rr_start; rr <= r_hi; rr += rr_step) {
                float yy = float(rr) - yt;
                float gy_src = (float(rr) - dy) * out_s_f;
                for (int cc = cc_start0; cc <= c_hi; cc += cc_step) {
                    if (rb_ry < 0 && cfa_at(rr, cc) != target_ch) continue;
                    float xx = float(cc) - xt;
                    float d = xt_scale * (axx * xx * xx + 2.0 * axy * xx * yy + ayy * yy * yy);
                    if (d > dlim_c) continue;
                    float raw_v = frames_all[fbase + rr * pc.W1 + cc];
                    float st = (raw_v > sat_lo_n) ? sat_taper(raw_v, sat_lo_n, sat_ispan_n) : 1.0;
                    float v = raw_v * wb;
                    float g_ref = bilerp_g(gy_src, (float(cc) - dx) * out_s_f);
                    float dv = v - g_ref;
                    float rob = 1.0;
                    if (use_rob && (anchor < 0.82 * sat0 || raw_v < 0.82 * sat0)) {
                        float ad = abs(v - anchor_wb);
                        if (ad > thr_wb) { float q = thr_wb / ad; rob = q * q * q; }
                    }
                    float w = mw * inv_var_rel * st * rob * fast_exp_neg(0.5 * d);
                    if (w <= 1e-12) continue;
                    S0 += w; Sx += w * xx; Sy += w * yy;
                    Sxx += w * xx * xx; Sxy += w * xx * yy; Syy += w * yy * yy;
                    T0 += w * dv; Tx += w * xx * dv; Ty += w * yy * dv;
                    dmin = min(dmin, dv);
                    dmax = max(dmax, dv);
                }
            }
        }

        float anc_diff = anchor_wb - g_here;
        float diff = anc_diff;
        if (S0 > 1e-8) {
            float inv_s0 = 1.0 / S0;
            float nw_diff = T0 * inv_s0;
            float mx = Sx * inv_s0, my = Sy * inv_s0;
            float F00 = max(0.0, Sxx - Sx * mx);
            float F01 = Sxy - Sx * my;
            float F11 = max(0.0, Syy - Sy * my);
            float tr_f = (F00 + F11) * inv_s0;
            float df_f = (F00 - F11) * inv_s0;
            float s01 = (2.0 * F01) * inv_s0;
            float disc = sqrt(max(0.0, df_f * df_f + s01 * s01));
            float lam_min = max(0.0, 0.5 * (tr_f - disc));
            float phi_c = clamp(lam_min / 0.14, 0.0, 1.0) * clamp(S0 / 1.5, 0.0, 1.0);
            float reg = 0.02 * S0;
            float F00_r = F00 + reg, F11_r = F11 + reg;
            float det_r = max(1e-12, F00_r * F11_r - F01 * F01);
            float bx = (F11_r * (Tx - mx * T0) - F01 * (Ty - my * T0)) / det_r;
            float by = (F00_r * (Ty - my * T0) - F01 * (Tx - mx * T0)) / det_r;
            float q_lev = max(0.0, S0 * (F11_r * mx * mx - 2.0 * F01 * mx * my + F00_r * my * my) / det_r);
            float lev_cap = (q_lev > 0.35) ? sqrt(0.35 / q_lev) : 1.0;
            float alpha_ll = phi_c * lev_cap;
            float d_ll = clamp(nw_diff - alpha_ll * (bx * mx + by * my), dmin, dmax);
            float cov_w = clamp(S0 / 0.35, 0.0, 1.0);
            diff = cov_w * d_ll + (1.0 - cov_w) * anc_diff;
        }
        float peak = max(g_here, anchor);
        if (peak > hl_lim) {
            float t = clamp((peak - hl_lim) / 0.14, 0.0, 1.0);
            diff *= (1.0 - t) * (1.0 - t);
        }
        if (b == 0) dR_1x[Y * pc.W1 + X] = diff;
        else        dB_1x[Y * pc.W1 + X] = diff;
    }
}
"""

SHADERS["k_v3_vst_fwd"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float green_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float lut[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float z_out[]; };

layout(push_constant) uniform PC {
    int H_out, W_out, full_H_out, origin_out_y;
    float u_min, u_max, inv_du, pad0;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= pc.W_out || y >= pc.H_out) return;

    int b = clamp(((y + pc.origin_out_y) * 16) / pc.full_H_out, 0, 15);
    int fwd_base = b * 256;
    int meta_base = 12288 + b * 8;
    float slo = lut[meta_base + 0];
    float shi = lut[meta_base + 1];
    float zmax = lut[meta_base + 3];

    int i = y * pc.W_out + x;
    float u = green_in[i];
    float zv;
    if (u <= pc.u_min) {
        zv = (u - pc.u_min) * slo;
    } else if (u >= pc.u_max) {
        zv = zmax + (u - pc.u_max) * shi;
    } else {
        float f = (u - pc.u_min) * pc.inv_du;
        int idx = min(254, max(0, int(f)));
        float t = f - float(idx);
        zv = mix(lut[fwd_base + idx], lut[fwd_base + idx + 1], t);
    }
    z_out[i] = zv;
}
"""

SHADERS["k_v3_fft64_sure"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float z_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float lut[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float snr_boost[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float fcomp_eta[]; };
layout(set = 0, binding = 4) buffer B4 { float den_out[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float dv_tile_out[]; };

layout(push_constant) uniform PC {
    int H_out, W_out, Hh, Wh, eta_h, eta_w, eta_off, color;
    int ntr_c, ntc_c, phase_y, phase_x, inv_out_s, eta_shift;
    float inv_mean_eta, oct_mul0, oct_mul1, oct_mul2, oct_mul3;
    int nr_t, nc_t;
} pc;

shared float s_re[64][33];
shared float s_im[64][33];
shared float s_tmp[256];
shared float s_oct[4];
shared float s_goct[4];
shared uint  s_hist[128];

int reflect_coord(int i, int n) {
    if (n <= 1) return 0;
    if (i < 0) i = -i;
    while (i >= n) {
        i = (i >= 2 * n - 1) ? (i % max(1, 2 * n - 2)) : (2 * n - 2 - i);
        if (i < 0) i = -i;
    }
    return clamp(i, 0, n - 1);
}

void main() {
    int tid = int(gl_LocalInvocationID.x);
    int wgid = int(gl_WorkGroupID.x);
    if (wgid >= pc.ntr_c * pc.ntc_c) return;

    int tr = (wgid / pc.ntc_c) * 2 + (pc.color >> 1);
    int tc = (wgid % pc.ntc_c) * 2 + (pc.color & 1);
    int py = tr * 32 - 32 - pc.phase_y;
    int px = tc * 32 - 32 - pc.phase_x;

    int kOffPsi = 12416, kOffGFloor = 16512, kOffOctF = 20608;
    int kOffTwCos = 24704, kOffTwSin = 24768, kOffSinWin = 24832, kOffOctCnt = 24896;

    // 1. Load windowed 64x64 patch and reduce COLA mean
    float local_wz = 0.0;
    float lv0[8], lv1[8], lw0[8], lw1[8];
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int ry = reflect_coord(py + r, pc.H_out);
        int rx0 = reflect_coord(px + 2 * c, pc.W_out);
        int rx1 = reflect_coord(px + 2 * c + 1, pc.W_out);
        float wy = lut[kOffSinWin + r];
        float w0 = wy * lut[kOffSinWin + 2 * c];
        float w1 = wy * lut[kOffSinWin + 2 * c + 1];
        float v0 = z_in[ry * pc.W_out + rx0];
        float v1 = z_in[ry * pc.W_out + rx1];
        lv0[it] = v0; lv1[it] = v1; lw0[it] = w0; lw1[it] = w1;
        local_wz += w0 * w0 * v0 + w1 * w1 * v1;
    }
    s_tmp[tid] = local_wz;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
        barrier();
    }
    float mu_p = s_tmp[0] * (1.0 / 1024.0);
    if (tid < 128) s_hist[tid] = 0u;
    barrier();

    // 2. Pack zero-mean windowed pairs into bit-reversed 32-pt complex rows
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int cr = int(bitfieldReverse(uint(c)) >> 27);
        s_re[r][cr] = lw0[it] * (lv0[it] - mu_p);
        s_im[r][cr] = lw1[it] * (lv1[it] - mu_p);
    }
    barrier();

    // 3. Forward 32-pt Row FFT across 64 rows
    for (int s = 1; s <= 5; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int r = bf_id >> 4;
            int lbf = bf_id & 15;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = lut[kOffTwCos + widx];
            float wi = -lut[kOffTwSin + widx];
            float r1 = s_re[r][i1], m1 = s_im[r][i1];
            float tr = wr * r1 - wi * m1;
            float ti = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - tr; s_im[r][i1] = m0 - ti;
            s_re[r][i0] = r0 + tr; s_im[r][i0] = m0 + ti;
        }
        barrier();
    }

    // 4. Hermitian Unpack of 64 rows into col 0 = (F0, F32), cols 1..31
    {
        int r = tid >> 2;
        int sub = tid & 3;
        int ks[4];
        vec2 fa_arr[4], fb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float ar = s_re[r][k], ai = s_im[r][k];
                float pr = s_re[r][kc], pi = s_im[r][kc];
                vec2 E = 0.5 * vec2(ar + pr, ai - pi);
                vec2 O = 0.5 * vec2(ai + pi, pr - ar);
                float wr = lut[kOffTwCos + k], wi = -lut[kOffTwSin + k];
                fa_arr[p] = E + vec2(wr * O.x - wi * O.y, wr * O.y + wi * O.x);
                vec2 E2 = vec2(E.x, -E.y);
                vec2 O2 = vec2(O.x, -O.y);
                float wr2 = lut[kOffTwCos + kc], wi2 = -lut[kOffTwSin + kc];
                fb_arr[p] = E2 + vec2(wr2 * O2.x - wi2 * O2.y, wr2 * O2.y + wi2 * O2.x);
            }
        }
        float p0r = 0.0, p0i = 0.0, p16r = 0.0, p16i = 0.0;
        if (sub == 0) {
            p0r = s_re[r][0] + s_im[r][0];
            p0i = s_re[r][0] - s_im[r][0];
            p16r = s_re[r][16];
            p16i = -s_im[r][16];
        }
        barrier();
        for (int p = 0; p < 4; ++p) {
            int k = ks[p];
            if (k <= 15) {
                int kc = 32 - k;
                s_re[r][k] = fa_arr[p].x; s_im[r][k] = fa_arr[p].y;
                s_re[r][kc] = fb_arr[p].x; s_im[r][kc] = fb_arr[p].y;
            }
        }
        if (sub == 0) {
            s_re[r][0] = p0r; s_im[r][0] = p0i;
            s_re[r][16] = p16r; s_im[r][16] = p16i;
        }
        barrier();
    }

    // 5. Forward 64-pt Column FFT (bit-reversal + 6 stages across cols 0..31)
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int col = k & 31;
        int jr = int(bitfieldReverse(uint(r)) >> 26);
        if (r < jr) {
            float tr = s_re[r][col], ti = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = tr; s_im[jr][col] = ti;
        }
    }
    barrier();
    for (int s = 1; s <= 6; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int col = bf_id >> 5;
            int lbf = bf_id & 31;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = lut[kOffTwCos + widx];
            float wi = -lut[kOffTwSin + widx];
            float r1 = s_re[i1][col], m1 = s_im[i1][col];
            float tr = wr * r1 - wi * m1;
            float ti = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - tr; s_im[i1][col] = m0 - ti;
            s_re[i0][col] = r0 + tr; s_im[i0][col] = m0 + ti;
        }
        barrier();
    }

    // 6. Unpack 2D columns 0 and 32 from Z[ky] stored at col 0
    if (tid < 64) {
        int ky = tid;
        int kc = (64 - ky) & 63;
        float zax = s_re[ky][0], zay = s_im[ky][0];
        float zbx = s_re[kc][0], zby = s_im[kc][0];
        barrier();
        s_re[ky][0]  = 0.5 * (zax + zbx);
        s_im[ky][0]  = 0.5 * (zay - zby);
        s_re[ky][32] = 0.5 * (zay + zby);
        s_im[ky][32] = 0.5 * (zbx - zax);
    } else {
        barrier();
    }
    barrier();

    // 7. Compute nu_ana, 4-octave Parseval energies & oct3 median histogram
    int cy1 = clamp((py + 32) >> (1 + pc.inv_out_s), 0, max(0, pc.Hh - 1));
    int cx1 = clamp((px + 32) >> (1 + pc.inv_out_s), 0, max(0, pc.Wh - 1));
    float boost = snr_boost[cy1 * max(1, pc.Wh) + cx1];
    float eta_ratio = 1.0;
    if (pc.eta_w > 0 && pc.eta_h > 0) {
        int ey = clamp((py + 32) >> pc.eta_shift, 0, pc.eta_h - 1);
        int ex = clamp((px + 32) >> pc.eta_shift, 0, pc.eta_w - 1);
        float el = fcomp_eta[pc.eta_off + ey * pc.eta_w + ex];
        if (el >= 0.0) eta_ratio = clamp(el * pc.inv_mean_eta, 0.25, 3.0);
    }
    float nu_ana = 1024.0 * eta_ratio * boost * boost;
    float hist_lo = 0.5 * nu_ana * 0.6931472;
    float hist_span = max(1e-12, 1.5 * nu_ana * 0.6931472);
    float hist_inv_w = 127.0 / hist_span;

    float loc_ew0 = 0.0, loc_ew1 = 0.0, loc_ew2 = 0.0, loc_ew3 = 0.0;
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33;
        int kx = kh % 33;
        if (ky == 0 && kx == 0) continue;
        float w_mult = (kx == 0 || kx == 32) ? 1.0 : 2.0;
        int idx = ky * 64 + kx;
        float psi_k = lut[kOffPsi + idx];
        int o = int(lut[kOffOctF + idx] + 0.5);
        float p = s_re[ky][kx] * s_re[ky][kx] + s_im[ky][kx] * s_im[ky][kx];
        float pw = p / psi_k;
        float wpw = w_mult * pw;
        if (o == 0) loc_ew0 += wpw;
        else if (o == 1) loc_ew1 += wpw;
        else if (o == 2) loc_ew2 += wpw;
        else loc_ew3 += wpw;

        if (o == 3) {
            int hb = clamp(int(floor((pw - hist_lo) * hist_inv_w)), 0, 127);
            if ((idx & 3) == 0) atomicAdd(s_hist[hb], 1u);
            if (kx >= 1 && kx <= 31) {
                int ky_m = (64 - ky) & 63;
                int idx_m = ky_m * 64 + (64 - kx);
                if ((idx_m & 3) == 0) atomicAdd(s_hist[hb], 1u);
            }
        }
    }
    for (int o = 0; o < 4; ++o) {
        s_tmp[tid] = (o == 0) ? loc_ew0 : ((o == 1) ? loc_ew1 : ((o == 2) ? loc_ew2 : loc_ew3));
        barrier();
        for (int stride = 128; stride > 0; stride >>= 1) {
            if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
            barrier();
        }
        if (tid == 0) s_oct[o] = s_tmp[0];
        barrier();
    }

    if (tid == 0) {
        uint total_h = 0u;
        for (int b = 0; b < 128; ++b) total_h += s_hist[b];
        float nu_emp = nu_ana;
        if (total_h > 0u) {
            uint target = total_h / 2u + 1u;
            uint acc = 0u;
            int b_hit = 64;
            float frac = 0.5;
            for (int b = 0; b < 128; ++b) {
                uint c = s_hist[b];
                if (acc + c >= target) {
                    b_hit = b;
                    frac = float(target - acc) / max(1.0, float(c));
                    break;
                }
                acc += c;
            }
            float pw_med = hist_lo + (float(b_hit) + frac) / hist_inv_w;
            nu_emp = clamp(pw_med / 0.6931472, 0.5 * nu_ana, 2.0 * nu_ana);
        }
        float nu_eff = 0.65 * nu_ana + 0.35 * nu_emp;
        s_tmp[0] = nu_eff;
        float oct_m[4];
        oct_m[0] = pc.oct_mul0; oct_m[1] = pc.oct_mul1; oct_m[2] = pc.oct_mul2; oct_m[3] = pc.oct_mul3;
        for (int o = 0; o < 4; ++o) {
            float thr_o = nu_eff * oct_m[o];
            float M_dof = max(1.0, lut[kOffOctCnt + o] - 2.0);
            s_goct[o] = clamp(1.0 - (M_dof * thr_o) / max(1e-12, s_oct[o]), 0.0, 1.0);
        }
    }
    barrier();

    float nu_eff = s_tmp[0];
    float oct_m[4];
    oct_m[0] = pc.oct_mul0; oct_m[1] = pc.oct_mul1; oct_m[2] = pc.oct_mul2; oct_m[3] = pc.oct_mul3;

    // 8. Shrink 2D spectrum
    if (tid == 0) {
        s_re[0][0] *= s_goct[0];
        s_im[0][0] *= s_goct[0];
    }
    float loc_keep = 0.0;
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33;
        int kx = kh % 33;
        if (ky == 0 && kx == 0) {
            loc_keep += s_goct[0] * s_goct[0];
            continue;
        }
        int idx = ky * 64 + kx;
        float psi_k = lut[kOffPsi + idx];
        int o = int(lut[kOffOctF + idx] + 0.5);
        float gf = lut[kOffGFloor + idx];
        float p = s_re[ky][kx] * s_re[ky][kx] + s_im[ky][kx] * s_im[ky][kx];
        float nu_k = nu_eff * oct_m[o] * psi_k;
        float g_spike = max(0.0, p - 2.4 * nu_k) / (p + 1e-12);
        float gk = max(gf, max(s_goct[o], g_spike));
        s_re[ky][kx] *= gk;
        s_im[ky][kx] *= gk;
        float w_mult = (kx == 0 || kx == 32) ? 1.0 : 2.0;
        loc_keep += w_mult * gk * gk * psi_k;
    }
    s_tmp[tid] = loc_keep;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
        barrier();
    }
    if (tid == 0 && pc.nr_t > 0 && pc.nc_t > 0) {
        float rho_keep = clamp(s_tmp[0] * (1.0 / 4096.0), 0.0, 1.0);
        dv_tile_out[tr * pc.nc_t + tc] = clamp(eta_ratio * (1.0 - rho_keep), 0.0, eta_ratio);
    }
    barrier();

    // 9. Re-pack col0 + i*col32, Inverse 64-pt Column FFT
    if (tid < 64) {
        int ky = tid;
        float c0r = s_re[ky][0], c0i = s_im[ky][0];
        float c32r = s_re[ky][32], c32i = s_im[ky][32];
        s_re[ky][0] = c0r - c32i;
        s_im[ky][0] = c0i + c32r;
    }
    barrier();
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int col = k & 31;
        int jr = int(bitfieldReverse(uint(r)) >> 26);
        if (r < jr) {
            float tr = s_re[r][col], ti = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = tr; s_im[jr][col] = ti;
        }
    }
    barrier();
    for (int s = 1; s <= 6; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int col = bf_id >> 5;
            int lbf = bf_id & 31;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = lut[kOffTwCos + widx];
            float wi = lut[kOffTwSin + widx];
            float r1 = s_re[i1][col], m1 = s_im[i1][col];
            float tr = wr * r1 - wi * m1;
            float ti = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - tr; s_im[i1][col] = m0 - ti;
            s_re[i0][col] = r0 + tr; s_im[i0][col] = m0 + ti;
        }
        barrier();
    }

    // 10. Inverse Hermitian unpack F'[r, 0..31], F'[r, 32] -> P'[0..31]
    {
        int r = tid >> 2;
        int sub = tid & 3;
        int ks[4];
        vec2 pa_arr[4], pb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float far = s_re[r][k], fai = s_im[r][k];
                float fbr = s_re[r][kc], fbi = s_im[r][kc];
                vec2 E = 0.5 * vec2(far + fbr, fai - fbi);
                vec2 WO = 0.5 * vec2(far - fbr, fai + fbi);
                float wr = lut[kOffTwCos + k], wi = lut[kOffTwSin + k];
                vec2 O = vec2(wr * WO.x - wi * WO.y, wr * WO.y + wi * WO.x);
                pa_arr[p] = vec2(E.x - O.y, E.y + O.x);
                pb_arr[p] = vec2(E.x + O.y, O.x - E.y);
            }
        }
        float q0r = 0.0, q0i = 0.0, q16r = 0.0, q16i = 0.0;
        if (sub == 0) {
            float F0 = s_re[r][0], F32 = s_im[r][0];
            q0r = 0.5 * (F0 + F32);
            q0i = 0.5 * (F0 - F32);
            q16r = s_re[r][16];
            q16i = -s_im[r][16];
        }
        barrier();
        for (int p = 0; p < 4; ++p) {
            int k = ks[p];
            if (k <= 15) {
                int kc = 32 - k;
                s_re[r][k] = pa_arr[p].x; s_im[r][k] = pa_arr[p].y;
                s_re[r][kc] = pb_arr[p].x; s_im[r][kc] = pb_arr[p].y;
            }
        }
        if (sub == 0) {
            s_re[r][0] = q0r; s_im[r][0] = q0i;
            s_re[r][16] = q16r; s_im[r][16] = q16i;
        }
        barrier();
    }

    // Bit-reverse P'[0..31] along cols for inverse 32-pt Row FFT
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int jc = int(bitfieldReverse(uint(c)) >> 27);
        if (c < jc) {
            float tr = s_re[r][c], ti = s_im[r][c];
            s_re[r][c] = s_re[r][jc]; s_im[r][c] = s_im[r][jc];
            s_re[r][jc] = tr; s_im[r][jc] = ti;
        }
    }
    barrier();
    for (int s = 1; s <= 5; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int r = bf_id >> 4;
            int lbf = bf_id & 15;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = lut[kOffTwCos + widx];
            float wi = lut[kOffTwSin + widx];
            float r1 = s_re[r][i1], m1 = s_im[r][i1];
            float tr = wr * r1 - wi * m1;
            float ti = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - tr; s_im[r][i1] = m0 - ti;
            s_re[r][i0] = r0 + tr; s_im[r][i0] = m0 + ti;
        }
        barrier();
    }

    // 11. Sine synthesis window overlap-add into disjoint color den_out
    float inv_n = 1.0 / 2048.0;
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int oy = py + r;
        if (oy >= 0 && oy < pc.H_out) {
            float wy = lut[kOffSinWin + r];
            int c0 = 2 * c, c1 = 2 * c + 1;
            int ox0 = px + c0;
            if (ox0 >= 0 && ox0 < pc.W_out) {
                float w0c = wy * lut[kOffSinWin + c0];
                den_out[oy * pc.W_out + ox0] += w0c * (inv_n * s_re[r][c] + w0c * mu_p);
            }
            int ox1 = px + c1;
            if (ox1 >= 0 && ox1 < pc.W_out) {
                float w1c = wy * lut[kOffSinWin + c1];
                den_out[oy * pc.W_out + ox1] += w1c * (inv_n * s_im[r][c] + w1c * mu_p);
            }
        }
    }
}
"""

SHADERS["k_v3_dv_half"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float z_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float den_in[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float dv_out[]; };

layout(push_constant) uniform PC { int H_out, W_out, Hv, Wv; } pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= pc.Wv || y >= pc.Hv) return;
    float s = 0.0;
    int cnt = 0;
    for (int dy = 0; dy < 2 && 2 * y + dy < pc.H_out; ++dy) {
        int roff = (2 * y + dy) * pc.W_out;
        for (int dx = 0; dx < 2 && 2 * x + dx < pc.W_out; ++dx) {
            int i = roff + 2 * x + dx;
            float d = z_in[i] - den_in[i];
            s += d * d;
            ++cnt;
        }
    }
    dv_out[y * pc.Wv + x] = min((cnt > 0) ? (s / float(cnt)) : 0.0, 3.0);
}
"""

SHADERS["k_v3_vst_inv"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float den_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float z_in[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float dv_tile[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float lut[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float green_out[]; };

layout(push_constant) uniform PC {
    int H_out, W_out, Hv, Wv, full_H_out, origin_out_y;
    float u_min, u_max, inv_du, pad0;
    int phase_y, phase_x;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= pc.W_out || y >= pc.H_out) return;

    int b = clamp(((y + pc.origin_out_y) * 16) / pc.full_H_out, 0, 15);
    int inv_base = 4096 + b * 256;
    int dsig_base = 8192 + b * 256;
    int meta_base = 12288 + b * 8;
    float zmin = lut[meta_base + 2];
    float zmax = lut[meta_base + 3];
    float idz = lut[meta_base + 4];
    float inv_slo = lut[meta_base + 5];
    float inv_shi = lut[meta_base + 6];

    float lo = 1e30, hi = -1e30;
    for (int dy = -1; dy <= 1; ++dy) {
        int yy = clamp(y + dy, 0, pc.H_out - 1);
        int ro = yy * pc.W_out;
        for (int dx = -1; dx <= 1; ++dx) {
            int xx = clamp(x + dx, 0, pc.W_out - 1);
            float v = z_in[ro + xx];
            lo = min(lo, v);
            hi = max(hi, v);
        }
    }
    int i = y * pc.W_out + x;
    float zv = clamp(den_in[i], lo, hi);
    float u_est;
    if (zv <= zmin) {
        u_est = pc.u_min + (zv - zmin) * inv_slo;
    } else if (zv >= zmax) {
        u_est = pc.u_max + (zv - zmax) * inv_shi;
    } else {
        float f = (zv - zmin) * idz;
        int idx = min(254, max(0, int(f)));
        float t = f - float(idx);
        u_est = mix(lut[inv_base + idx], lut[inv_base + idx + 1], t);
    }
    if (u_est > pc.u_min && u_est < pc.u_max) {
        float fu = (u_est - pc.u_min) * pc.inv_du;
        int uidx = min(254, max(0, int(fu)));
        float ut = fu - float(uidx);
        float ds2 = mix(lut[dsig_base + uidx], lut[dsig_base + uidx + 1], ut);
        float ty = clamp(float(y + pc.phase_y) * (1.0 / 32.0), 0.0, float(pc.Hv - 1));
        float tx = clamp(float(x + pc.phase_x) * (1.0 / 32.0), 0.0, float(pc.Wv - 1));
        int ty0 = int(ty), ty1 = min(pc.Hv - 1, ty0 + 1);
        int tx0 = int(tx), tx1 = min(pc.Wv - 1, tx0 + 1);
        float fty = ty - float(ty0), ftx = tx - float(tx0);
        float d0 = mix(dv_tile[ty0 * pc.Wv + tx0], dv_tile[ty0 * pc.Wv + tx1], ftx);
        float d1 = mix(dv_tile[ty1 * pc.Wv + tx0], dv_tile[ty1 * pc.Wv + tx1], ftx);
        float dv_z = mix(d0, d1, fty);
        u_est += 0.25 * ds2 * dv_z;
    }
    green_out[i] = u_est;
}
"""

SHADERS["k_v3_impulse_clamp"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) buffer B0 { float dR[]; };
layout(set = 0, binding = 1) buffer B1 { float dB[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float mR[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float mB[]; };

layout(push_constant) uniform PC {
    uint count;
    float imp_thr, pad0, pad1;
} pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= pc.count) return;
    float vr = dR[i], mr = mR[i];
    if (abs(vr - mr) > pc.imp_thr) dR[i] = mr;
    float vb = dB[i], mb = mB[i];
    if (abs(vb - mb) > pc.imp_thr) dB[i] = mb;
}
"""

SHADERS["k_v3_down2"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src[]; };
layout(set = 0, binding = 1) writeonly buffer B1 { float dst[]; };

layout(push_constant) uniform PC { int Hs, Ws, Hd, Wd; } pc;

int reflect_coord(int i, int n) {
    if (n <= 1) return 0;
    if (i < 0) i = -i;
    while (i >= n) {
        i = (i >= 2 * n - 1) ? (i % max(1, 2 * n - 2)) : (2 * n - 2 - i);
        if (i < 0) i = -i;
    }
    return clamp(i, 0, n - 1);
}

void main() {
    int xd = int(gl_GlobalInvocationID.x);
    int yd = int(gl_GlobalInvocationID.y);
    if (xd >= pc.Wd || yd >= pc.Hd) return;
    const float kw[5] = float[5](1.0/16.0, 4.0/16.0, 6.0/16.0, 4.0/16.0, 1.0/16.0);
    int yc = min(pc.Hs - 1, 2 * yd);
    int xc = min(pc.Ws - 1, 2 * xd);
    int rx[5];
    for (int kx = 0; kx < 5; ++kx) rx[kx] = reflect_coord(xc + kx - 2, pc.Ws);
    float acc = 0.0;
    for (int ky = 0; ky < 5; ++ky) {
        int ry = reflect_coord(yc + ky - 2, pc.Hs);
        int roff = ry * pc.Ws;
        float row_s = 0.0;
        for (int kx = 0; kx < 5; ++kx) row_s += kw[kx] * src[roff + rx[kx]];
        acc += kw[ky] * row_s;
    }
    dst[yd * pc.Wd + xd] = acc;
}
"""

SHADERS["k_v3_up2_lap"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float cur[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float coarse[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float out_buf[]; };

layout(push_constant) uniform PC { int Hs, Ws, Hd, Wd, mode; } pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= pc.Ws || y >= pc.Hs) return;
    int i = y * pc.Ws + x;
    if (pc.mode == 2) {
        out_buf[i] = cur[(2 * y) * (2 * pc.Ws) + (2 * x)];
        return;
    }
    float ys = clamp(0.5 * float(y), 0.0, float(max(0, pc.Hd - 1)));
    float xs = clamp(0.5 * float(x), 0.0, float(max(0, pc.Wd - 1)));
    int r0 = clamp(int(ys), 0, max(0, pc.Hd - 2));
    int r1 = min(max(0, pc.Hd - 1), r0 + 1);
    int c0 = clamp(int(xs), 0, max(0, pc.Wd - 2));
    int c1 = min(max(0, pc.Wd - 1), c0 + 1);
    float fy = ys - float(r0), fx = xs - float(c0);
    float v0 = mix(coarse[r0 * pc.Wd + c0], coarse[r0 * pc.Wd + c1], fx);
    float v1 = mix(coarse[r1 * pc.Wd + c0], coarse[r1 * pc.Wd + c1], fx);
    float up = mix(v0, v1, fy);
    out_buf[i] = (pc.mode == 0) ? (cur[i] - up) : (up + cur[i]);
}
"""

SHADERS["k_v3_odd_huber"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float src[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float guide_g[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float dst[]; };

layout(push_constant) uniform PC {
    int H, W;
    float delta, inv_2eg, thr_z;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= pc.W || y >= pc.H) return;
    int i = y * pc.W + x;
    float dc = src[i];
    if (pc.H < 2 || pc.W < 2) {
        dst[i] = dc;
        return;
    }
    const float kw[9] = float[9](
        0.075, 0.125, 0.075,
        0.125, 0.200, 0.125,
        0.075, 0.125, 0.075
    );
    int ys[3] = int[3](max(0, y - 1), y, min(pc.H - 1, y + 1));
    int xs[3] = int[3](max(0, x - 1), x, min(pc.W - 1, x + 1));
    float gc = guide_g[i];
    float wsum = 0.200, step = 0.0;
    for (int dy = 0; dy < 3; ++dy) {
        int ro = ys[dy] * pc.W;
        for (int dx = 0; dx < 3; ++dx) {
            if (dy == 1 && dx == 1) continue;
            int idx = ro + xs[dx];
            float dg = guide_g[idx] - gc;
            float wl = kw[dy * 3 + dx] * fast_exp_neg(dg * dg * pc.inv_2eg);
            float diff = src[idx] - dc;
            step += wl * clamp(diff, -pc.delta, pc.delta);
            wsum += wl;
        }
    }
    float out_v = dc + 0.65 * (step / max(1e-12, wsum));
    if (pc.thr_z > 0.0) {
        float mag = abs(out_v);
        out_v = (mag <= pc.thr_z)
            ? (out_v * (mag / pc.thr_z) * 0.25)
            : (out_v - ((out_v >= 0.0) ? 1.0 : -1.0) * (0.75 * pc.thr_z));
    }
    dst[i] = out_v;
}
"""

SHADERS["k_v3_assemble_rgb"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float green_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float dR_1x[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float dB_1x[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float outR[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float outG[]; };
layout(set = 0, binding = 5) writeonly buffer B5 { float outB[]; };

layout(push_constant) uniform PC {
    int H1, W1, H_out, W_out, out_scale;
    float inv_wb_r, inv_wb_b;
} pc;

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= pc.W_out || y >= pc.H_out) return;
    int i = y * pc.W_out + x;
    float gv = green_in[i];
    float dr, db;
    if (pc.out_scale == 1) {
        int i1 = y * pc.W1 + x;
        dr = dR_1x[i1];
        db = dB_1x[i1];
    } else {
        int ry0 = clamp(y >> 1, 0, pc.H1 - 1);
        int ry1 = ((y & 1) != 0) ? min(pc.H1 - 1, ry0 + 1) : ry0;
        int cx0 = clamp(x >> 1, 0, pc.W1 - 1);
        int cx1 = ((x & 1) != 0) ? min(pc.W1 - 1, cx0 + 1) : cx0;
        int i00 = ry0 * pc.W1 + cx0, i01 = ry0 * pc.W1 + cx1;
        int i10 = ry1 * pc.W1 + cx0, i11 = ry1 * pc.W1 + cx1;
        dr = 0.25 * (dR_1x[i00] + dR_1x[i01] + dR_1x[i10] + dR_1x[i11]);
        db = 0.25 * (dB_1x[i00] + dB_1x[i01] + dB_1x[i10] + dB_1x[i11]);
    }
    outR[i] = (gv + dr) * pc.inv_wb_r;
    outG[i] = gv;
    outB[i] = (gv + db) * pc.inv_wb_b;
}
"""

SHADERS["k_m5a_zero_u32"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) writeonly buffer B0 { uint dst[]; };
layout(push_constant) uniform PC { uint count; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i < pc.count) dst[i] = 0u;
}
"""

SHADERS["k_m5a_scatter"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float raw_slice[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flow_mw_slice[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float rgb_band_in[]; };
layout(set = 0, binding = 3) buffer B3 { int gm_i32[]; };
layout(set = 0, binding = 4) buffer B4 { uint tile_stats[]; };
layout(set = 0, binding = 5) readonly buffer B5 { float params[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh;
    int H_out, W_out, out_scale, tile;
    int nr, nc, nr_band, tr_b0;
    int band_y0, band_h, rgb_in_y0, rgb_in_h;
    int sy0, sy_cnt, hy0, hy_cnt;
    int frame_n, is_it0, zero_gm_only, pad0;
    float v_scale, quad, q_scale, pad2;
} pc;

vec4 sample_fm(float y, float x) {
    int hy = clamp(int(floor(y)), 0, pc.Hh - 2);
    int hx = clamp(int(floor(x)), 0, pc.Wh - 2);
    int hy1 = min(hy + 1, pc.Hh - 1);
    int hx1 = min(hx + 1, pc.Wh - 1);
    float fy = clamp(y - float(hy), 0.0, 1.0);
    float fx = clamp(x - float(hx), 0.0, 1.0);
    int sy_0 = clamp(hy - pc.hy0, 0, pc.hy_cnt - 1);
    int sy_1 = clamp(hy1 - pc.hy0, 0, pc.hy_cnt - 1);
    vec4 v00 = flow_mw_slice[sy_0 * pc.Wh + hx];
    vec4 v01 = flow_mw_slice[sy_0 * pc.Wh + hx1];
    vec4 v10 = flow_mw_slice[sy_1 * pc.Wh + hx];
    vec4 v11 = flow_mw_slice[sy_1 * pc.Wh + hx1];
    return mix(mix(v00, v01, fx), mix(v10, v11, fx), fy);
}

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int Y = int(gl_GlobalInvocationID.y);
    if (c >= pc.W1 || Y >= pc.sy_cnt) return;
    int r = pc.sy0 + Y;

    int fp_off = 48 + pc.frame_n * 16;
    float rv_n = params[fp_off + 0];
    float sh_n = params[fp_off + 1];
    float sat_n = params[fp_off + 2];
    bool all_valid = (params[fp_off + 3] > 0.5);
    bool unit_mw = (params[fp_off + 4] > 0.5);
    bool has_flow = (params[fp_off + 5] > 0.5);
    bool const_shift = (params[fp_off + 6] > 0.5);
    float shift_y = params[fp_off + 7];
    float shift_x = params[fp_off + 8];
    float inv_blur = params[fp_off + 9];
    float half_tau2 = params[fp_off + 10];
    float ois_r0 = params[fp_off + 11], ois_r1 = params[fp_off + 12];
    float ois_b0 = params[fp_off + 13], ois_b1 = params[fp_off + 14];
    bool has_sat = (params[fp_off + 15] > 0.5);

    float v = raw_slice[Y * pc.W1 + c];
    if (!all_valid && (isnan(v) || isinf(v) || (has_sat && v >= sat_n))) return;

    float yb = float(r), xb = float(c);
    if (const_shift) {
        yb -= shift_y;
        xb -= shift_x;
    } else if (has_flow) {
        for (int k = 0; k < 5; ++k) {
            float y = clamp(yb * 0.5, 0.0, float(pc.Hh - 1));
            float x = clamp(xb * 0.5, 0.0, float(pc.Wh - 1));
            vec4 fm = sample_fm(y, x);
            float ny = fma(-2.0, fm.x, float(r));
            float nx = fma(-2.0, fm.y, float(c));
            bool settled = (abs(ny - yb) <= 1e-7 && abs(nx - xb) <= 1e-7);
            yb = ny; xb = nx;
            if (settled) break;
        }
    }
    if (isnan(yb) || isinf(yb) || isnan(xb) || isinf(xb) ||
        yb < 0.0 || yb > float(pc.H1 - 1) || xb < 0.0 || xb > float(pc.W1 - 1)) return;

    int ch = int(params[8 + (r % 6) * 6 + (c % 6)] + 0.5);
    float ry = yb, rx = xb;
    if (ch != 1) {
        float lca_rk1 = params[0], lca_rk2 = params[1];
        float lca_bk1 = params[2], lca_bk2 = params[3];
        float cy_opt = params[4], cx_opt = params[5], inv_ropt = params[6];
        float dyc = yb - cy_opt, dxc = xb - cx_opt;
        float rn = (dxc * inv_ropt) * (dxc * inv_ropt) + (dyc * inv_ropt) * (dyc * inv_ropt);
        float lca = (ch == 0) ? (lca_rk1 + lca_rk2 * rn) : (lca_bk1 + lca_bk2 * rn);
        ry -= lca * dyc + ((ch == 0) ? ois_r0 : ois_b0);
        rx -= lca * dxc + ((ch == 0) ? ois_r1 : ois_b1);
    }

    float yo = float(pc.out_scale) * ry;
    float xo = float(pc.out_scale) * rx;
    if (yo < float(pc.band_y0) - 2.0 || yo > float(pc.band_y0 + pc.band_h) + 1.0) return;

    float gate = (unit_mw || !has_flow) ? 1.0 : clamp(sample_fm(yb * 0.5, xb * 0.5).z, 0.0, 1.0);
    if (gate <= 0.0) return;

    float proj = 0.0;
    float ky[4], kx[4];
    int fr0 = 0, fc0 = 0;
    float wsum = 0.0;

    if (pc.out_scale == 2) {
        fr0 = int(floor(yo)) - 1;
        fc0 = int(floor(xo)) - 1;
        if (inv_blur >= 0.99 && fr0 >= 0 && fr0 + 3 < pc.H_out && fc0 >= 0 && fc0 + 3 < pc.W_out) {
            float fy_f = yo - float(fr0 + 1), fx_f = xo - float(fc0 + 1);
            ky[0] = 0.25 * (1.0 - fy_f); ky[1] = 0.5 - 0.25 * fy_f; ky[2] = 0.25 * (1.0 + fy_f); ky[3] = 0.25 * fy_f;
            kx[0] = 0.25 * (1.0 - fx_f); kx[1] = 0.5 - 0.25 * fx_f; kx[2] = 0.25 * (1.0 + fx_f); kx[3] = 0.25 * fx_f;
            for (int dr = 0; dr < 4; ++dr) {
                int lry = clamp((fr0 + dr) - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
                int base = lry * pc.W_out * 3 + fc0 * 3 + ch;
                proj += ky[dr] * (kx[0] * rgb_band_in[base + 0] + kx[1] * rgb_band_in[base + 3] +
                                  kx[2] * rgb_band_in[base + 6] + kx[3] * rgb_band_in[base + 9]);
            }
            wsum = 1.0;
        } else {
            for (int d = 0; d < 4; ++d) {
                ky[d] = max(0.0, 1.0 - 0.5 * inv_blur * abs(float(fr0 + d) - yo));
                kx[d] = max(0.0, 1.0 - 0.5 * inv_blur * abs(float(fc0 + d) - xo));
            }
            for (int dr = 0; dr < 4; ++dr) {
                int fr = fr0 + dr;
                if (fr < 0 || fr >= pc.H_out || ky[dr] <= 0.0) continue;
                int lry = clamp(fr - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
                int rbase = lry * pc.W_out * 3 + ch;
                for (int dc = 0; dc < 4; ++dc) {
                    int fc = fc0 + dc;
                    if (fc < 0 || fc >= pc.W_out) continue;
                    float k = ky[dr] * kx[dc];
                    if (k <= 0.0) continue;
                    proj += k * rgb_band_in[rbase + fc * 3];
                    wsum += k;
                }
            }
            if (wsum <= 0.0) return;
            proj /= wsum;
        }
    } else {
        float y_c = clamp(yo, 0.0, float(pc.H_out - 1));
        float x_c = clamp(xo, 0.0, float(pc.W_out - 1));
        fr0 = int(y_c); fc0 = int(x_c);
        int y1i = min(pc.H_out - 1, fr0 + 1), x1i = min(pc.W_out - 1, fc0 + 1);
        float ty = y_c - float(fr0), tx = x_c - float(fc0);
        ky[0] = 1.0 - ty; ky[1] = ty;
        kx[0] = 1.0 - tx; kx[1] = tx;
        int ly0 = clamp(fr0 - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
        int ly1 = clamp(y1i - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
        proj = ky[0] * (kx[0] * rgb_band_in[(ly0 * pc.W_out + fc0) * 3 + ch] + kx[1] * rgb_band_in[(ly0 * pc.W_out + x1i) * 3 + ch]) +
               ky[1] * (kx[0] * rgb_band_in[(ly1 * pc.W_out + fc0) * 3 + ch] + kx[1] * rgb_band_in[(ly1 * pc.W_out + x1i) * 3 + ch]);
    }

    float p_pos = max(0.0, proj);
    float fvar = max(1e-12, rv_n + p_pos * (sh_n + pc.quad * p_pos));
    float iw = gate / fvar;
    float raw_res = v - proj;
    float z2 = iw * raw_res * raw_res;

    uint q_tile = uint(clamp(z2 * 256.0, 0.0, 4.0e8));
    uint q_sub  = uint(clamp(z2 * 16.0, 0.0, 65535.0));

    int tr0_chk = max(pc.tr_b0, int(floor((yo - 1.5) / float(pc.tile))) - 1);
    int tr1_chk = min(pc.tr_b0 + pc.nr_band - 1, int(floor((yo + 2.5) / float(pc.tile))) + 1);
    int tc0_chk = max(0, int(floor((xo - 1.5) / float(pc.tile))) - 1);
    int tc1_chk = min(pc.nc - 1, int(floor((xo + 2.5) / float(pc.tile))) + 1);
    for (int tr = tr0_chk; tr <= tr1_chk; ++tr) {
        int r0 = tr * pc.tile;
        int th = min(pc.tile, pc.H_out - r0);
        if (yo < float(r0) - 2.0 || yo > float(r0 + th) + 1.0) continue;
        int sy_c = clamp(((int(yo) - r0) * 8) / max(1, th), 0, 7);
        for (int tc = tc0_chk; tc <= tc1_chk; ++tc) {
            int c0 = tc * pc.tile;
            int tw = min(pc.tile, pc.W_out - c0);
            if (xo < float(c0) - 2.0 || xo > float(c0 + tw) + 1.0) continue;
            int sx_c = clamp(((int(xo) - c0) * 8) / max(1, tw), 0, 7);
            int sb = (sy_c << 3) | sx_c;
            int tb = ((tr - pc.tr_b0) * pc.nc + tc) * 136;
            uint old_lo = atomicAdd(tile_stats[tb + 0], q_tile);
            if (old_lo > 0xFFFFFFFFu - q_tile) atomicAdd(tile_stats[tb + 1], 1u);
            atomicAdd(tile_stats[tb + 2], 1u);
            atomicAdd(tile_stats[tb + 6 + sb], q_sub);
            atomicAdd(tile_stats[tb + 70 + sb], 1u);
        }
    }

    if (pc.is_it0 != 0) {
        int oy_i = int(floor(yo));
        int ox_i = int(floor(xo));
        int own_tr = (oy_i <= 0) ? 0 : min(pc.nr - 1, oy_i / pc.tile);
        int own_tc = (ox_i <= 0) ? 0 : min(pc.nc - 1, ox_i / pc.tile);
        if (own_tr >= pc.tr_b0 && own_tr < pc.tr_b0 + pc.nr_band) {
            int tb_o = ((own_tr - pc.tr_b0) * pc.nc + own_tc) * 136;
            uint old_lo = atomicAdd(tile_stats[tb_o + 3], q_tile);
            if (old_lo > 0xFFFFFFFFu - q_tile) atomicAdd(tile_stats[tb_o + 4], 1u);
            atomicAdd(tile_stats[tb_o + 5], 1u);
        }
    }

    if (pc.zero_gm_only != 0) return;

    float res = raw_res;
    if (half_tau2 > 0.0) res *= (res * res) / (res * res + half_tau2);
    float w_tilde = clamp(iw * pc.v_scale, 0.0, 64.0);
    float inv_wsum = (pc.out_scale == 2 && wsum > 0.0) ? (1.0 / wsum) : 1.0;

    if (pc.out_scale == 2) {
        for (int dr = 0; dr < 4; ++dr) {
            int fr = fr0 + dr;
            if (fr < pc.band_y0 || fr >= pc.band_y0 + pc.band_h || ky[dr] <= 0.0) continue;
            int ro = (fr - pc.band_y0) * pc.W_out;
            for (int dc = 0; dc < 4; ++dc) {
                int fc = fc0 + dc;
                if (fc < 0 || fc >= pc.W_out || kx[dc] <= 0.0) continue;
                float wk = ky[dr] * kx[dc] * inv_wsum;
                int dg = int(round(wk * clamp(w_tilde * res, -16.0, 16.0) * (262144.0 * pc.q_scale)));
                int dm = int(round(wk * w_tilde * (16384.0 * pc.q_scale)));
                int pi = (ro + fc) * 6;
                if (dg != 0) atomicAdd(gm_i32[pi + ch], dg);
                if (dm != 0) atomicAdd(gm_i32[pi + 3 + ch], dm);
            }
        }
    } else {
        int frs[2] = {fr0, min(pc.H_out - 1, fr0 + 1)};
        int fcs[2] = {fc0, min(pc.W_out - 1, fc0 + 1)};
        for (int dr = 0; dr < 2; ++dr) {
            int fr = frs[dr];
            if (fr < pc.band_y0 || fr >= pc.band_y0 + pc.band_h || ky[dr] <= 0.0) continue;
            int ro = (fr - pc.band_y0) * pc.W_out;
            for (int dc = 0; dc < 2; ++dc) {
                int fc = fcs[dc];
                float wk = ky[dr] * kx[dc];
                if (wk <= 0.0) continue;
                int dg = int(round(wk * clamp(w_tilde * res, -16.0, 16.0) * (262144.0 * pc.q_scale)));
                int dm = int(round(wk * w_tilde * (16384.0 * pc.q_scale)));
                int pi = (ro + fc) * 6;
                if (dg != 0) atomicAdd(gm_i32[pi + ch], dg);
                if (dm != 0) atomicAdd(gm_i32[pi + 3 + ch], dm);
            }
        }
    }
}
"""

SHADERS["k_m5a_update"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float rgb_band_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { int gm_i32[]; };
layout(set = 0, binding = 2) readonly buffer B2 { uint tile_stats[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float rgb_band_out[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float wg_stats[]; };

layout(push_constant) uniform PC {
    int H_out, W_out, out_scale, tile;
    int nc, nr_band, tr_b0, band_y0;
    int band_h, rgb_in_y0, wg_x;
    float q_scale;
    float tau_morozov, v_scale, rv_base, sh_base;
    float quad, k_pi_g, k_pi_c_coarse, k_pi_c;
} pc;

shared float s_crg[18][18], s_cbg[18][18];
shared float s_crn[18][18], s_crm[18][18], s_cbn[18][18], s_cbm[18][18];
shared float s_rms[256], s_chg[256];

float noise_var(float v) {
    float s = max(0.0, v);
    return max(1e-10, pc.rv_base + s * (pc.sh_base + pc.quad * s));
}

void main() {
    int tid = int(gl_LocalInvocationIndex);
    int wgx = int(gl_WorkGroupID.x);
    int wgy = int(gl_WorkGroupID.y);
    int gx = wgx * 16 + int(gl_LocalInvocationID.x);
    int gy = wgy * 16 + int(gl_LocalInvocationID.y);

    int wx_base = wgx * 16;
    int wy_base = wgy * 16;
    float inv_g = 1.0 / (262144.0 * pc.v_scale * pc.q_scale);
    float inv_m = 1.0 / (16384.0 * pc.v_scale * pc.q_scale);

    for (int idx = tid; idx < 324; idx += 256) {
        int sy = idx / 18;
        int sx = idx % 18;
        int ny = clamp(wy_base + sy - 1, 0, pc.band_h - 1);
        int nx = clamp(wx_base + sx - 1, 0, pc.W_out - 1);
        int in_off = ((ny + pc.band_y0 - pc.rgb_in_y0) * pc.W_out + nx) * 3;
        float r_v = rgb_band_in[in_off + 0];
        float g_v = rgb_band_in[in_off + 1];
        float b_v = rgb_band_in[in_off + 2];
        s_crg[sy][sx] = r_v - g_v;
        s_cbg[sy][sx] = b_v - g_v;
        int p6 = (ny * pc.W_out + nx) * 6;
        float g0 = float(gm_i32[p6 + 0]) * inv_g;
        float g1 = float(gm_i32[p6 + 1]) * inv_g;
        float g2 = float(gm_i32[p6 + 2]) * inv_g;
        float m0 = float(gm_i32[p6 + 3]) * inv_m;
        float m1 = float(gm_i32[p6 + 4]) * inv_m;
        float m2 = float(gm_i32[p6 + 5]) * inv_m;
        float mtot = m0 + m1 + m2;
        if (mtot > 0.0) {
            float pi_g = pc.k_pi_g / noise_var(g_v);
            float dg0 = (g0 + g1 + g2) / (mtot + pi_g);
            s_crn[sy][sx] = g0 - m0 * dg0; s_crm[sy][sx] = m0;
            s_cbn[sy][sx] = g2 - m2 * dg0; s_cbm[sy][sx] = m2;
        } else {
            s_crn[sy][sx] = 0.0; s_crm[sy][sx] = 0.0;
            s_cbn[sy][sx] = 0.0; s_cbm[sy][sx] = 0.0;
        }
    }
    barrier();

    float pix_rms = 0.0, pix_chg = 0.0;
    if (gx < pc.W_out && gy < pc.band_h) {
        int tr_loc = gy / pc.tile;
        int tc = gx / pc.tile;
        int tb = (tr_loc * pc.nc + tc) * 136;
        uint t_cnt = tile_stats[tb + 2];
        float t_chi2 = 0.0;
        if (t_cnt > 0u) {
            float tot = float(tile_stats[tb + 1]) * 4294967296.0 + float(tile_stats[tb + 0]);
            t_chi2 = (tot / 256.0) / float(t_cnt);
        }
        float max_sub = 0.0;
        for (int sb = 0; sb < 64; ++sb) {
            uint sc = tile_stats[tb + 70 + sb];
            if (sc >= 32u) {
                max_sub = max(max_sub, (float(tile_stats[tb + 6 + sb]) / 16.0) / float(sc));
            }
        }
        float eff_chi2 = max(t_chi2, 0.80 * max_sub);
        int in_off = ((gy + pc.band_y0 - pc.rgb_in_y0) * pc.W_out + gx) * 3;
        int out_off = (gy * pc.W_out + gx) * 3;
        float cur_r = rgb_band_in[in_off + 0];
        float cur_g = rgb_band_in[in_off + 1];
        float cur_b = rgb_band_in[in_off + 2];

        int p6 = (gy * pc.W_out + gx) * 6;
        float g0 = float(gm_i32[p6 + 0]) * inv_g;
        float g1 = float(gm_i32[p6 + 1]) * inv_g;
        float g2 = float(gm_i32[p6 + 2]) * inv_g;
        float m0 = float(gm_i32[p6 + 3]) * inv_m;
        float m1 = float(gm_i32[p6 + 4]) * inv_m;
        float m2 = float(gm_i32[p6 + 5]) * inv_m;

        if (eff_chi2 <= pc.tau_morozov || (m0 + m1 + m2) <= 0.0) {
            rgb_band_out[out_off + 0] = cur_r;
            rgb_band_out[out_off + 1] = cur_g;
            rgb_band_out[out_off + 2] = cur_b;
        } else {
            float tile_gain = clamp(0.88 * (eff_chi2 - pc.tau_morozov) / max(0.25, eff_chi2), 0.25, 0.88);
            int sy_lo = max(0, tr_loc * pc.tile - wy_base + 1);
            int sy_hi = min(17, min(pc.band_h, (tr_loc + 1) * pc.tile) - wy_base);
            int sx_lo = max(0, tc * pc.tile - wx_base + 1);
            int sx_hi = min(17, min(pc.W_out, (tc + 1) * pc.tile) - wx_base);
            int lly = int(gl_LocalInvocationID.y) + 1;
            int llx = int(gl_LocalInvocationID.x) + 1;
            int ys[3] = int[3](max(sy_lo, lly - 1), lly, min(sy_hi, lly + 1));
            int xs[3] = int[3](max(sx_lo, llx - 1), llx, min(sx_hi, llx + 1));
            float wy3[3] = float[3](1.0, 2.0, 1.0);

            float nv = noise_var(cur_g);
            float thr9 = 9.0 * nv;
            float c_rg = cur_r - cur_g, c_bg = cur_b - cur_g;
            float srn = 0.0, srm = 0.0, sbn = 0.0, sbm = 0.0;
            for (int dy = 0; dy < 3; ++dy) {
                for (int dx = 0; dx < 3; ++dx) {
                    float kw = wy3[dy] * wy3[dx];
                    int yy = ys[dy], xx = xs[dx];
                    float dcr = s_crg[yy][xx] - c_rg;
                    float dcb = s_cbg[yy][xx] - c_bg;
                    float wcr = kw * ((dcr * dcr < thr9) ? 1.0 : (thr9 / (dcr * dcr + 1e-12)));
                    float wcb = kw * ((dcb * dcb < thr9) ? 1.0 : (thr9 / (dcb * dcb + 1e-12)));
                    srn += wcr * s_crn[yy][xx]; srm += wcr * s_crm[yy][xx];
                    sbn += wcb * s_cbn[yy][xx]; sbm += wcb * s_cbm[yy][xx];
                }
            }
            float pi_cc = pc.k_pi_c_coarse / nv;
            float dCr_bar = srn / (srm + pi_cc);
            float dCb_bar = sbn / (sbm + pi_cc);
            float pi_g = pc.k_pi_g / nv;
            float pi_c = pc.k_pi_c / nv;
            float ar = pi_c / (m0 + pi_c);
            float ab = pi_c / (m2 + pi_c);
            float gr_t = g0 - m0 * dCr_bar;
            float gb_t = g2 - m2 * dCb_bar;
            float dG = (g1 + ar * gr_t + ab * gb_t) / (m1 + pi_g + ar * m0 + ab * m2);
            float dCr = dCr_bar + (gr_t - m0 * dG) / (m0 + pi_c);
            float dCb = dCb_bar + (gb_t - m2 * dG) / (m2 + pi_c);
            float dr = tile_gain * (dG + dCr);
            float dg = tile_gain * dG;
            float db = tile_gain * (dG + dCb);
            rgb_band_out[out_off + 0] = cur_r + dr;
            rgb_band_out[out_off + 1] = cur_g + dg;
            rgb_band_out[out_off + 2] = cur_b + db;
            pix_rms = dr * dr + dg * dg + db * db;
            if (dr != 0.0) pix_chg += 1.0;
            if (dg != 0.0) pix_chg += 1.0;
            if (db != 0.0) pix_chg += 1.0;
        }
    }
    s_rms[tid] = pix_rms;
    s_chg[tid] = pix_chg;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) {
            s_rms[tid] += s_rms[tid + stride];
            s_chg[tid] += s_chg[tid + stride];
        }
        barrier();
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 2;
        wg_stats[wgi + 0] = s_rms[0];
        wg_stats[wgi + 1] = s_chg[0];
    }
}
"""

SHADERS["k_m5a_jacobian"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float rgb_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float fcomp[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float rgb_out[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float wg_stats[]; };

layout(push_constant) uniform PC {
    int H_out, W_out, band_y0, band_h;
    int rgb_in_y0, rgb_in_h, has_fcomp, wg_x;
    float strength, rv_base, sh_base, quad;
} pc;

shared float sr[18][18], sg[18][18], sb[18][18];
shared float s_rms[256], s_chg[256];

void main() {
    int tid = int(gl_LocalInvocationIndex);
    int wgx = int(gl_WorkGroupID.x), wgy = int(gl_WorkGroupID.y);
    int gx = wgx * 16 + int(gl_LocalInvocationID.x);
    int gy = wgy * 16 + int(gl_LocalInvocationID.y);
    int wx_base = wgx * 16, wy_base = wgy * 16;

    for (int idx = tid; idx < 324; idx += 256) {
        int sy = idx / 18, sx = idx % 18;
        int ny_g = clamp(pc.band_y0 + wy_base + sy - 1, 0, pc.H_out - 1);
        int nx_g = clamp(wx_base + sx - 1, 0, pc.W_out - 1);
        int in_off = ((ny_g - pc.rgb_in_y0) * pc.W_out + nx_g) * 3;
        sr[sy][sx] = rgb_in[in_off + 0];
        sg[sy][sx] = rgb_in[in_off + 1];
        sb[sy][sx] = rgb_in[in_off + 2];
    }
    barrier();

    float pix_rms = 0.0, pix_chg = 0.0;
    if (gx < pc.W_out && gy < pc.band_h) {
        int sy = int(gl_LocalInvocationID.y) + 1;
        int sx = int(gl_LocalInvocationID.x) + 1;
        float p0r = sr[sy][sx], p0g = sg[sy][sx], p0b = sb[sy][sx];
        float out_r = p0r, out_b = p0b;
        float fc = (pc.has_fcomp != 0) ? clamp(fcomp[gy * pc.W_out + gx], 0.0, 1.0) : 0.5;
        if (fc > 0.02) {
            float mr = 0.0, mg = 0.0, mb = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    mr += sr[sy + dy][sx + dx];
                    mg += sg[sy + dy][sx + dx];
                    mb += sb[sy + dy][sx + dx];
                }
            }
            mr *= (1.0 / 9.0); mg *= (1.0 / 9.0); mb *= (1.0 / 9.0);

            float vg = 0.0, crg = 0.0, cbg = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    float dr = sr[sy + dy][sx + dx] - mr;
                    float dg = sg[sy + dy][sx + dx] - mg;
                    float db = sb[sy + dy][sx + dx] - mb;
                    vg += dg * dg; crg += dr * dg; cbg += db * dg;
                }
            }
            float gr_x = 0.5 * (sr[sy][sx + 1] - sr[sy][sx - 1]);
            float gg_x = 0.5 * (sg[sy][sx + 1] - sg[sy][sx - 1]);
            float gb_x = 0.5 * (sb[sy][sx + 1] - sb[sy][sx - 1]);
            float gr_y = 0.5 * (sr[sy + 1][sx] - sr[sy - 1][sx]);
            float gg_y = 0.5 * (sg[sy + 1][sx] - sg[sy - 1][sx]);
            float gb_y = 0.5 * (sb[sy + 1][sx] - sb[sy - 1][sx]);
            float gxx = gr_x * gr_x + gg_x * gg_x + gb_x * gb_x;
            float gxy = gr_x * gr_y + gg_x * gg_y + gb_x * gb_y;
            float gyy = gr_y * gr_y + gg_y * gg_y + gb_y * gb_y;
            float df = gxx - gyy;
            float disc = sqrt(max(0.0, df * df + 4.0 * gxy * gxy));
            float sig2_sq = max(0.0, 0.5 * ((gxx + gyy) - disc));
            float mg_pos = max(0.0, mg);
            float nv = max(1e-12, pc.rv_base + mg_pos * (pc.sh_base + pc.quad * mg_pos));
            if (sig2_sq > 1e-12 || vg > 1e-12) {
                float slp_r = crg / (vg + 0.5 * nv + 1e-12);
                float slp_b = cbg / (vg + 0.5 * nv + 1e-12);
                float dg0 = p0g - mg;
                float res_r = p0r - (mr + slp_r * dg0);
                float res_b = p0b - (mb + slp_b * dg0);
                float ratio = sig2_sq / (sig2_sq + max(1e-10, 2.0 * nv));
                float pull = fc * pc.strength * ratio;
                float dr = -pull * res_r;
                float db = -pull * res_b;
                out_r = p0r + dr;
                out_b = p0b + db;
                pix_rms = dr * dr + db * db;
                if (dr != 0.0 || db != 0.0) pix_chg = 1.0;
            }
        }
        int out_off = (gy * pc.W_out + gx) * 3;
        rgb_out[out_off + 0] = out_r;
        rgb_out[out_off + 1] = p0g;
        rgb_out[out_off + 2] = out_b;
    }
    s_rms[tid] = pix_rms;
    s_chg[tid] = pix_chg;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) {
            s_rms[tid] += s_rms[tid + stride];
            s_chg[tid] += s_chg[tid + stride];
        }
        barrier();
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 2;
        wg_stats[wgi + 0] = s_rms[0];
        wg_stats[wgi + 1] = s_chg[0];
    }
}
"""

SHADERS["k_m5b_fft64"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float rgb_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float params[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float focus_half[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float rgb_dec[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float tile_act[]; };

layout(push_constant) uniform PC {
    int H, W, band_y0, band_h;
    int in_y0, in_h, nr_band, nc;
    int out_scale, olpf_mode, is_astro, n_zones;
    int focus_h, focus_w, psf_iters, pad0;
    float str, psf_sigma_tile, rv, sh;
    float quad, max_boost, sig_tgt, pad1;
} pc;

shared float s_re[64][33];
shared float s_im[64][33];
shared float s_tmp[256];

int reflect_clamp_i(int i, int n) {
    if (n <= 1) return 0;
    while (i < 0 || i >= n) {
        if (i < 0) i = -i;
        if (i >= n) i = 2 * n - 2 - i;
    }
    return i;
}

float load_patch_ch(int tile_y0, int tile_x0, int r, int c, int ch) {
    int gy = reflect_clamp_i(tile_y0 + r, pc.H);
    int gx = reflect_clamp_i(tile_x0 + c, pc.W);
    int sy = clamp(gy - pc.in_y0, 0, pc.in_h - 1);
    return rgb_in[(sy * pc.W + gx) * 3 + ch];
}

float fft64_fwd(int tid, int tile_y0, int tile_x0, int ch) {
    float sum_loc = 0.0;
    float v0[8], v1[8];
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        v0[it] = load_patch_ch(tile_y0, tile_x0, r, 2 * c, ch);
        v1[it] = load_patch_ch(tile_y0, tile_x0, r, 2 * c + 1, ch);
        sum_loc += v0[it] + v1[it];
    }
    s_tmp[tid] = sum_loc;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
        barrier();
    }
    float mu = s_tmp[0] * (1.0 / 4096.0);
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int cr = int(bitfieldReverse(uint(c)) >> 27);
        s_re[r][cr] = v0[it] - mu;
        s_im[r][cr] = v1[it] - mu;
    }
    barrier();

    for (int s = 1; s <= 5; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int r = bf_id >> 4;
            int lbf = bf_id & 15;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = params[widx], wi = -params[64 + widx];
            float r1 = s_re[r][i1], m1 = s_im[r][i1];
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - trv; s_im[r][i1] = m0 - tiv;
            s_re[r][i0] = r0 + trv; s_im[r][i0] = m0 + tiv;
        }
        barrier();
    }

    {
        int r = tid >> 2;
        int sub = tid & 3;
        int ks[4];
        vec2 fa_arr[4], fb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float ar = s_re[r][k], ai = s_im[r][k];
                float pr = s_re[r][kc], pi = s_im[r][kc];
                vec2 E = 0.5 * vec2(ar + pr, ai - pi);
                vec2 O = 0.5 * vec2(ai + pi, pr - ar);
                float wr = params[k], wi = -params[64 + k];
                fa_arr[p] = E + vec2(wr * O.x - wi * O.y, wr * O.y + wi * O.x);
                vec2 E2 = vec2(E.x, -E.y), O2 = vec2(O.x, -O.y);
                float wr2 = params[kc], wi2 = -params[64 + kc];
                fb_arr[p] = E2 + vec2(wr2 * O2.x - wi2 * O2.y, wr2 * O2.y + wi2 * O2.x);
            }
        }
        float p0r = 0.0, p0i = 0.0, p16r = 0.0, p16i = 0.0;
        if (sub == 0) {
            p0r = s_re[r][0] + s_im[r][0];
            p0i = s_re[r][0] - s_im[r][0];
            p16r = s_re[r][16];
            p16i = -s_im[r][16];
        }
        barrier();
        for (int p = 0; p < 4; ++p) {
            int k = ks[p];
            if (k <= 15) {
                int kc = 32 - k;
                s_re[r][k] = fa_arr[p].x; s_im[r][k] = fa_arr[p].y;
                s_re[r][kc] = fb_arr[p].x; s_im[r][kc] = fb_arr[p].y;
            }
        }
        if (sub == 0) {
            s_re[r][0] = p0r; s_im[r][0] = p0i;
            s_re[r][16] = p16r; s_im[r][16] = p16i;
        }
        barrier();
    }

    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int col = k & 31;
        int jr = int(bitfieldReverse(uint(r)) >> 26);
        if (r < jr) {
            float trv = s_re[r][col], tiv = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = trv; s_im[jr][col] = tiv;
        }
    }
    barrier();
    for (int s = 1; s <= 6; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int col = bf_id >> 5;
            int lbf = bf_id & 31;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = params[widx], wi = -params[64 + widx];
            float r1 = s_re[i1][col], m1 = s_im[i1][col];
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - trv; s_im[i1][col] = m0 - tiv;
            s_re[i0][col] = r0 + trv; s_im[i0][col] = m0 + tiv;
        }
        barrier();
    }
    {
        float zax = 0.0, zay = 0.0, zbx = 0.0, zby = 0.0;
        if (tid < 64) {
            int kc = (64 - tid) & 63;
            zax = s_re[tid][0]; zay = s_im[tid][0];
            zbx = s_re[kc][0]; zby = s_im[kc][0];
        }
        barrier();
        if (tid < 64) {
            s_re[tid][0]  = 0.5 * (zax + zbx);
            s_im[tid][0]  = 0.5 * (zay - zby);
            s_re[tid][32] = 0.5 * (zay + zby);
            s_im[tid][32] = 0.5 * (zbx - zax);
        }
        barrier();
    }
    return mu;
}

void fft64_inv_store(int tid, float mu, int core_top, int core_left, int core_h, int core_w, int ch) {
    if (tid < 64) {
        int ky = tid;
        float c0r = s_re[ky][0], c0i = s_im[ky][0];
        float c32r = s_re[ky][32], c32i = s_im[ky][32];
        s_re[ky][0] = c0r - c32i;
        s_im[ky][0] = c0i + c32r;
    }
    barrier();
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int col = k & 31;
        int jr = int(bitfieldReverse(uint(r)) >> 26);
        if (r < jr) {
            float trv = s_re[r][col], tiv = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = trv; s_im[jr][col] = tiv;
        }
    }
    barrier();
    for (int s = 1; s <= 6; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int col = bf_id >> 5;
            int lbf = bf_id & 31;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = params[widx], wi = params[64 + widx];
            float r1 = s_re[i1][col], m1 = s_im[i1][col];
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - trv; s_im[i1][col] = m0 - tiv;
            s_re[i0][col] = r0 + trv; s_im[i0][col] = m0 + tiv;
        }
        barrier();
    }
    {
        int r = tid >> 2;
        int sub = tid & 3;
        int ks[4];
        vec2 pa_arr[4], pb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float far = s_re[r][k], fai = s_im[r][k];
                float fbr = s_re[r][kc], fbi = s_im[r][kc];
                vec2 E = 0.5 * vec2(far + fbr, fai - fbi);
                vec2 WO = 0.5 * vec2(far - fbr, fai + fbi);
                float wr = params[k], wi = params[64 + k];
                vec2 O = vec2(wr * WO.x - wi * WO.y, wr * WO.y + wi * WO.x);
                pa_arr[p] = vec2(E.x - O.y, E.y + O.x);
                pb_arr[p] = vec2(E.x + O.y, O.x - E.y);
            }
        }
        float q0r = 0.0, q0i = 0.0, q16r = 0.0, q16i = 0.0;
        if (sub == 0) {
            float F0 = s_re[r][0], F32 = s_im[r][0];
            q0r = 0.5 * (F0 + F32);
            q0i = 0.5 * (F0 - F32);
            q16r = s_re[r][16];
            q16i = -s_im[r][16];
        }
        barrier();
        for (int p = 0; p < 4; ++p) {
            int k = ks[p];
            if (k <= 15) {
                int kc = 32 - k;
                s_re[r][k] = pa_arr[p].x; s_im[r][k] = pa_arr[p].y;
                s_re[r][kc] = pb_arr[p].x; s_im[r][kc] = pb_arr[p].y;
            }
        }
        if (sub == 0) {
            s_re[r][0] = q0r; s_im[r][0] = q0i;
            s_re[r][16] = q16r; s_im[r][16] = q16i;
        }
        barrier();
    }
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int jc = int(bitfieldReverse(uint(c)) >> 27);
        if (c < jc) {
            float trv = s_re[r][c], tiv = s_im[r][c];
            s_re[r][c] = s_re[r][jc]; s_im[r][c] = s_im[r][jc];
            s_re[r][jc] = trv; s_im[r][jc] = tiv;
        }
    }
    barrier();
    for (int s = 1; s <= 5; ++s) {
        int m = 1 << s;
        int mh = m >> 1;
        int step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int r = bf_id >> 4;
            int lbf = bf_id & 15;
            int k = (lbf / mh) * m;
            int j = lbf % mh;
            int i0 = k + j;
            int i1 = i0 + mh;
            int widx = j * step64;
            float wr = params[widx], wi = params[64 + widx];
            float r1 = s_re[r][i1], m1 = s_im[r][i1];
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - trv; s_im[r][i1] = m0 - tiv;
            s_re[r][i0] = r0 + trv; s_im[r][i0] = m0 + tiv;
        }
        barrier();
    }
    float inv_n = 1.0 / 2048.0;
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int lr = r - 12;
        if (lr >= 0 && lr < core_h) {
            int by = (core_top - pc.band_y0) + lr;
            int c0 = 2 * c - 12;
            int c1 = c0 + 1;
            if (c0 >= 0 && c0 < core_w) {
                rgb_dec[(by * pc.W + core_left + c0) * 3 + ch] = inv_n * s_re[r][c] + mu;
            }
            if (c1 >= 0 && c1 < core_w) {
                rgb_dec[(by * pc.W + core_left + c1) * 3 + ch] = inv_n * s_im[r][c] + mu;
            }
        }
    }
    barrier();
}

void main() {
    int tid = int(gl_LocalInvocationIndex);
    int ti = int(gl_WorkGroupID.x);
    int tr = ti / pc.nc;
    int tc = ti % pc.nc;
    if (tr >= pc.nr_band) return;

    int core_top = pc.band_y0 + tr * 40;
    int core_bot = min(pc.band_y0 + pc.band_h, core_top + 40);
    int core_left = tc * 40;
    int core_right = min(pc.W, core_left + 40);
    int core_h = core_bot - core_top;
    int core_w = core_right - core_left;
    int tile_y0 = core_top - 12;
    int tile_x0 = core_left - 12;

    float g_mean = fft64_fwd(tid, tile_y0, tile_x0, 1);

    float l_hf = 0.0, l_hc = 0.0;
    float l_b0 = 0.0, l_c0 = 0.0, l_b1 = 0.0, l_c1 = 0.0, l_b2 = 0.0, l_c2 = 0.0;
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33;
        int kx = kh % 33;
        if (ky == 0 && kx == 0) continue;
        float mult = (kx == 0 || kx == 32) ? 1.0 : 2.0;
        int sy = (ky <= 32) ? ky : (ky - 64);
        int sx = kx;
        int r2 = sy * sy + sx * sx;
        float p = s_re[ky][kx] * s_re[ky][kx] + s_im[ky][kx] * s_im[ky][kx];
        float mp = mult * p;
        if (r2 >= 36 && r2 <= 400) { l_hf += mp; l_hc += mult; }
        if (r2 <= 36) { l_b0 += mp; l_c0 += mult; }
        else if (r2 <= 196) { l_b1 += mp; l_c1 += mult; }
        else { l_b2 += mp; l_c2 += mult; }
    }
    float vals[8] = float[8](l_hf, l_hc, l_b0, l_c0, l_b1, l_c1, l_b2, l_c2);
    float sums[8];
    for (int q = 0; q < 8; ++q) {
        s_tmp[tid] = vals[q];
        barrier();
        for (int stride = 128; stride > 0; stride >>= 1) {
            if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
            barrier();
        }
        sums[q] = s_tmp[0];
        barrier();
    }

    float g_pos = max(0.0, g_mean);
    float var_g = max(1e-12, pc.rv + g_pos * (pc.sh + pc.quad * g_pos));
    float p_hf = (sums[1] > 0.0) ? (sums[0] / sums[1]) : 0.0;
    float p_nse = 4096.0 * var_g;
    float g_bokeh_spec = (pc.is_astro != 0) ? 1.0 : clamp((p_hf - 2.0 * p_nse) / (2.0 * p_nse + 1e-12), 0.0, 1.0);
    float alpha = pc.str * g_bokeh_spec;

    float focus_gate_max = 1.0;
    if (pc.focus_h > 0 && pc.focus_w > 0 && pc.is_astro == 0) {
        float fy_s = float(pc.focus_h) / float(pc.H), fx_s = float(pc.focus_w) / float(pc.W);
        int fy0 = max(0, int(floor((float(core_top) + 0.5) * fy_s - 0.5)));
        int fy1 = min(pc.focus_h - 1, int(floor((float(core_bot - 1) + 0.5) * fy_s - 0.5)) + 1);
        int fx0 = max(0, int(floor((float(core_left) + 0.5) * fx_s - 0.5)));
        int fx1 = min(pc.focus_w - 1, int(floor((float(core_right - 1) + 0.5) * fx_s - 0.5)) + 1);
        float mx_loc = 0.0;
        int ncells = (fy1 - fy0 + 1) * (fx1 - fx0 + 1);
        for (int idx = tid; idx < ncells; idx += 256) {
            int fyy = fy0 + idx / (fx1 - fx0 + 1);
            int fxx = fx0 + idx % (fx1 - fx0 + 1);
            float fv = focus_half[fyy * pc.focus_w + fxx];
            mx_loc = max(mx_loc, clamp((fv - 0.02) / 0.12, 0.0, 1.0));
        }
        s_tmp[tid] = mx_loc;
        barrier();
        for (int stride = 128; stride > 0; stride >>= 1) {
            if (tid < stride) s_tmp[tid] = max(s_tmp[tid], s_tmp[tid + stride]);
            barrier();
        }
        focus_gate_max = s_tmp[0];
    }

    if (alpha * focus_gate_max <= 1e-4) {
        if (tid == 0) tile_act[ti] = 0.0;
        return;
    }
    if (tid == 0) tile_act[ti] = alpha;

    float norm_cy = float(core_top + core_bot) * 0.5 / float(pc.H);
    float norm_cx = float(core_left + core_right) * 0.5 / float(pc.W);
    float sig_maj = pc.psf_sigma_tile;
    float sig_min = sig_maj, ang = 0.0;
    float sig_rgb[3] = float[3](sig_maj, sig_maj, sig_maj);
    if (pc.n_zones > 0) {
        float sw = 0.0, smaj = 0.0, smin = 0.0, sc2 = 0.0, ss2 = 0.0;
        float sc_ch[3] = float[3](0.0, 0.0, 0.0);
        for (int z = 0; z < pc.n_zones; ++z) {
            int zb = 132 + z * 8;
            float dx = norm_cx - params[zb + 0], dy = norm_cy - params[zb + 1];
            float wz = 1.0 / (dx * dx + dy * dy + 0.04);
            float zmaj = params[zb + 2], zmin = params[zb + 3], zang = params[zb + 4];
            sw += wz; smaj += wz * zmaj; smin += wz * zmin;
            sc2 += wz * cos(2.0 * zang); ss2 += wz * sin(2.0 * zang);
            for (int k = 0; k < 3; ++k) {
                float scv = (params[zb + 5 + k] > 0.0) ? params[zb + 5 + k] : zmaj;
                sc_ch[k] += wz * scv;
            }
        }
        float invw = 1.0 / sw;
        sig_maj = smaj * invw; sig_min = smin * invw; ang = 0.5 * atan(ss2, sc2);
        for (int k = 0; k < 3; ++k) sig_rgb[k] = sc_ch[k] * invw;
    }
    float s_maj_eff = sqrt(max(0.0, sig_maj * sig_maj - pc.sig_tgt * pc.sig_tgt));
    float s_min_eff = sqrt(max(0.0, sig_min * sig_min - pc.sig_tgt * pc.sig_tgt));
    float s_g_ref = max(0.01, sig_rgb[1]);

    float lam_b[3];
    for (int b = 0; b < 3; ++b) {
        float bs = (b == 0) ? sums[2] : ((b == 1) ? sums[4] : sums[6]);
        float bc = (b == 0) ? sums[3] : ((b == 1) ? sums[5] : sums[7]);
        float pb = (bc > 0.0) ? (bs / bc) : p_hf;
        float snr_b = clamp((pb - p_nse) / (10.0 * p_nse + 1e-12), 0.0, 1.0);
        lam_b[b] = 0.045 + (0.065 + 0.025 * float(b)) * (1.0 - snr_b);
    }

    float cs = cos(ang), sn = sin(ang);
    float olpf_c0 = (pc.olpf_mode == 1) ? 0.45 : ((pc.olpf_mode == 2) ? 0.15 : 1.0);
    float olpf_d = ((pc.olpf_mode == 1) ? 0.48 : 0.55) * float(pc.out_scale);
    float ratio_ch[3] = float[3](sig_rgb[0] / s_g_ref, 1.0, sig_rgb[2] / s_g_ref);
    float two_pi_64 = 6.28318530718 / 64.0;

    // 1. Apply Wiener + C2R IFFT for Green (spectrum already in s_re/s_im)
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33, kx = kh % 33;
        if (ky == 0 && kx == 0) continue;
        int sy = (ky <= 32) ? ky : (ky - 64), sx = kx;
        int r2 = sy * sy + sx * sx;
        float lam = lam_b[(r2 <= 36) ? 0 : ((r2 <= 196) ? 1 : 2)];
        float u = float(sy) * two_pi_64, v = float(sx) * two_pi_64;
        float ur = cs * v + sn * u, vr = -sn * v + cs * u;
        float s2_g = (ur * s_maj_eff) * (ur * s_maj_eff) + (vr * s_min_eff) * (vr * s_min_eff);
        float h_olpf = (pc.olpf_mode == 0) ? 1.0 : (olpf_c0 + (1.0 - olpf_c0) * 0.5 * (cos(u * olpf_d) + cos(v * olpf_d)));
        float hg = max(0.05, exp(-0.5 * s2_g) * h_olpf);
        float wg = 1.0 + alpha * (min(pc.max_boost, hg / (hg * hg + lam)) - 1.0);
        s_re[ky][kx] *= wg; s_im[ky][kx] *= wg;
    }
    barrier();
    fft64_inv_store(tid, g_mean, core_top, core_left, core_h, core_w, 1);

    // 2. Run R (ch=0) and B (ch=2) R2C -> Wiener -> C2R
    int rbs[2] = int[2](0, 2);
    for (int ci = 0; ci < 2; ++ci) {
        int ch = rbs[ci];
        float mu_c = fft64_fwd(tid, tile_y0, tile_x0, ch);
        float rc = ratio_ch[ch];
        float s_maj_c = sqrt(max(0.0, (sig_maj * rc) * (sig_maj * rc) - pc.sig_tgt * pc.sig_tgt));
        float s_min_c = sqrt(max(0.0, (sig_min * rc) * (sig_min * rc) - pc.sig_tgt * pc.sig_tgt));
        for (int kh = tid; kh < 64 * 33; kh += 256) {
            int ky = kh / 33, kx = kh % 33;
            if (ky == 0 && kx == 0) continue;
            int sy = (ky <= 32) ? ky : (ky - 64), sx = kx;
            int r2 = sy * sy + sx * sx;
            float lam = lam_b[(r2 <= 36) ? 0 : ((r2 <= 196) ? 1 : 2)];
            float u = float(sy) * two_pi_64, v = float(sx) * two_pi_64;
            float ur = cs * v + sn * u, vr = -sn * v + cs * u;
            float s2_c = (ur * s_maj_c) * (ur * s_maj_c) + (vr * s_min_c) * (vr * s_min_c);
            float h_olpf = (pc.olpf_mode == 0) ? 1.0 : (olpf_c0 + (1.0 - olpf_c0) * 0.5 * (cos(u * olpf_d) + cos(v * olpf_d)));
            float hc = max(0.05, exp(-0.5 * s2_c) * h_olpf);
            float wc = 1.0 + alpha * (min(pc.max_boost, hc / (hc * hc + lam)) - 1.0);
            s_re[ky][kx] *= wc; s_im[ky][kx] *= wc;
        }
        barrier();
        fft64_inv_store(tid, mu_c, core_top, core_left, core_h, core_w, ch);
    }
}
"""

SHADERS["k_m5b_clamp"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float rgb_in[]; };
layout(set = 0, binding = 1) buffer B1 { float rgb_dec[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float tile_act[]; };
layout(set = 0, binding = 3) readonly buffer B3 { float focus_half[]; };
layout(set = 0, binding = 4) writeonly buffer B4 { float wg_stats[]; };

layout(push_constant) uniform PC {
    int H, W, band_y0, band_h;
    int in_y0, in_h, nr_band, nc;
    int focus_h, focus_w, has_focus, wg_x;
    float rv, sh, quad, pad0;
} pc;

shared float sb0[20][20], sb1[20][20], sb2[20][20];
shared uint s_any_act;
shared float s_rms[256], s_chg[256];

void main() {
    int tid = int(gl_LocalInvocationIndex);
    int wgx = int(gl_WorkGroupID.x), wgy = int(gl_WorkGroupID.y);
    int gx = wgx * 16 + int(gl_LocalInvocationID.x);
    int gy = wgy * 16 + int(gl_LocalInvocationID.y);

    if (tid == 0) {
        int tr0 = clamp((wgy * 16) / 40, 0, pc.nr_band - 1);
        int tr1 = clamp((min(pc.band_h - 1, wgy * 16 + 15)) / 40, 0, pc.nr_band - 1);
        int tc0 = clamp((wgx * 16) / 40, 0, pc.nc - 1);
        int tc1 = clamp((min(pc.W - 1, wgx * 16 + 15)) / 40, 0, pc.nc - 1);
        uint aa = 0u;
        for (int tr = tr0; tr <= tr1; ++tr) {
            for (int tc = tc0; tc <= tc1; ++tc) {
                if (tile_act[tr * pc.nc + tc] > 1e-4) aa = 1u;
            }
        }
        s_any_act = aa;
    }
    barrier();

    if (s_any_act == 0u) {
        if (gx < pc.W && gy < pc.band_h) {
            int in_off = ((pc.band_y0 + gy - pc.in_y0) * pc.W + gx) * 3;
            int out_off = (gy * pc.W + gx) * 3;
            rgb_dec[out_off + 0] = rgb_in[in_off + 0];
            rgb_dec[out_off + 1] = rgb_in[in_off + 1];
            rgb_dec[out_off + 2] = rgb_in[in_off + 2];
        }
        if (tid == 0) {
            int wgi = (wgy * pc.wg_x + wgx) * 2;
            wg_stats[wgi + 0] = 0.0;
            wg_stats[wgi + 1] = 0.0;
        }
        return;
    }

    int wx_base = wgx * 16, wy_base = wgy * 16;
    for (int idx = tid; idx < 400; idx += 256) {
        int sy = idx / 20, sx = idx % 20;
        int ny_g = pc.band_y0 + wy_base + sy - 2;
        if (ny_g < 0) ny_g = -ny_g;
        if (ny_g >= pc.H) ny_g = max(0, 2 * pc.H - 2 - ny_g);
        int nx_g = wx_base + sx - 2;
        if (nx_g < 0) nx_g = -nx_g;
        if (nx_g >= pc.W) nx_g = max(0, 2 * pc.W - 2 - nx_g);
        int in_off = (clamp(ny_g - pc.in_y0, 0, pc.in_h - 1) * pc.W + nx_g) * 3;
        sb0[sy][sx] = rgb_in[in_off + 0];
        sb1[sy][sx] = rgb_in[in_off + 1];
        sb2[sy][sx] = rgb_in[in_off + 2];
    }
    barrier();

    float pix_rms = 0.0, pix_chg = 0.0;
    if (gx < pc.W && gy < pc.band_h) {
        int ti = (gy / 40) * pc.nc + (gx / 40);
        int sy = int(gl_LocalInvocationID.y) + 2;
        int sx = int(gl_LocalInvocationID.x) + 2;
        float o0 = sb0[sy][sx], o1 = sb1[sy][sx], o2 = sb2[sy][sx];
        int out_off = (gy * pc.W + gx) * 3;

        if (tile_act[ti] <= 1e-4) {
            rgb_dec[out_off + 0] = o0;
            rgb_dec[out_off + 1] = o1;
            rgb_dec[out_off + 2] = o2;
        } else {
            float d0 = rgb_dec[out_off + 0], d1 = rgb_dec[out_off + 1], d2 = rgb_dec[out_off + 2];
            float gx0 = 0.5 * (sb0[sy][sx + 1] - sb0[sy][sx - 1]), gy0 = 0.5 * (sb0[sy + 1][sx] - sb0[sy - 1][sx]);
            float gx1 = 0.5 * (sb1[sy][sx + 1] - sb1[sy][sx - 1]), gy1 = 0.5 * (sb1[sy + 1][sx] - sb1[sy - 1][sx]);
            float gx2 = 0.5 * (sb2[sy][sx + 1] - sb2[sy][sx - 1]), gy2 = 0.5 * (sb2[sy + 1][sx] - sb2[sy - 1][sx]);
            float energy = ((gx0 * gx0 + gy0 * gy0) + (gx1 * gx1 + gy1 * gy1) + (gx2 * gx2 + gy2 * gy2)) / 3.0;
            float p0 = max(0.0, o0), p1 = max(0.0, o1), p2 = max(0.0, o2);
            float variance = (max(1e-12, pc.rv + p0 * (pc.sh + pc.quad * p0)) +
                              max(1e-12, pc.rv + p1 * (pc.sh + pc.quad * p1)) +
                              max(1e-12, pc.rv + p2 * (pc.sh + pc.quad * p2))) / 3.0;
            float g_focus = 1.0;
            if (pc.has_focus != 0) {
                float fy_c = clamp((float(pc.band_y0 + gy) + 0.5) * (float(pc.focus_h) / float(pc.H)) - 0.5, 0.0, float(pc.focus_h - 1));
                float fx_c = clamp((float(gx) + 0.5) * (float(pc.focus_w) / float(pc.W)) - 0.5, 0.0, float(pc.focus_w - 1));
                int fy0 = int(fy_c), fx0 = int(fx_c);
                int fy1 = min(pc.focus_h - 1, fy0 + 1), fx1 = min(pc.focus_w - 1, fx0 + 1);
                float fyt = fy_c - float(fy0), fxt = fx_c - float(fx0);
                float fv = mix(mix(focus_half[fy0 * pc.focus_w + fx0], focus_half[fy0 * pc.focus_w + fx1], fxt),
                               mix(focus_half[fy1 * pc.focus_w + fx0], focus_half[fy1 * pc.focus_w + fx1], fxt), fyt);
                g_focus = clamp((fv - 0.02) / 0.12, 0.0, 1.0);
            }
            float gate = g_focus * (max(0.0, energy - 2.0 * variance) / (energy + 6.0 * variance + 1e-12));
            float sig = sqrt(variance);

            float mn0 = o0, mx0 = o0, mn1 = o1, mx1 = o1, mn2 = o2, mx2 = o2;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    float v0 = sb0[sy + dy][sx + dx], v1 = sb1[sy + dy][sx + dx], v2 = sb2[sy + dy][sx + dx];
                    mn0 = min(mn0, v0); mx0 = max(mx0, v0);
                    mn1 = min(mn1, v1); mx1 = max(mx1, v1);
                    mn2 = min(mn2, v2); mx2 = max(mx2, v2);
                }
            }
            float u0 = clamp(o0 + gate * (d0 - o0), mn0 - 0.012 * (mx0 - mn0) - 1.5 * sig, mx0 + 0.012 * (mx0 - mn0) + 1.5 * sig);
            float u1 = clamp(o1 + gate * (d1 - o1), mn1 - 0.012 * (mx1 - mn1) - 1.5 * sig, mx1 + 0.012 * (mx1 - mn1) + 1.5 * sig);
            float u2 = clamp(o2 + gate * (d2 - o2), mn2 - 0.012 * (mx2 - mn2) - 1.5 * sig, mx2 + 0.012 * (mx2 - mn2) + 1.5 * sig);
            rgb_dec[out_off + 0] = u0;
            rgb_dec[out_off + 1] = u1;
            rgb_dec[out_off + 2] = u2;
            float e0 = u0 - o0, e1 = u1 - o1, e2 = u2 - o2;
            pix_rms = e0 * e0 + e1 * e1 + e2 * e2;
            if (e0 != 0.0) pix_chg += 1.0;
            if (e1 != 0.0) pix_chg += 1.0;
            if (e2 != 0.0) pix_chg += 1.0;
        }
    }
    s_rms[tid] = pix_rms;
    s_chg[tid] = pix_chg;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) {
            s_rms[tid] += s_rms[tid + stride];
            s_chg[tid] += s_chg[tid + stride];
        }
        barrier();
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 2;
        wg_stats[wgi + 0] = s_rms[0];
        wg_stats[wgi + 1] = s_chg[0];
    }
}
"""

SHADERS["k_c13_warp_rgb"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float raw_slice[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flow_mw_slice[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float rgb_stack[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float fallback_rgb[]; };

layout(push_constant) uniform PC {
    int H1, W1, Hh, Wh;
    int sy0, sy_cnt, hy0, hy_cnt;
    int band_y0, band_h, frame_n, has_flow;
    int has_mw, write_fallback, pad0, pad1;
    uint cfa0, cfa1, cfa2, pad2;
} pc;

uint cfa_at_c13(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

vec3 eval_raw_rgb(float y, float x) {
    int iy = int(floor(y)), ix = int(floor(x));
    int r_lo = max(0, iy - 2), r_hi = min(pc.H1 - 1, iy + 3);
    int c_lo = max(0, ix - 2), c_hi = min(pc.W1 - 1, ix + 3);
    float sum[3] = float[3](0.0, 0.0, 0.0);
    float wt[3]  = float[3](0.0, 0.0, 0.0);
    float kx[6];
    int cw = c_hi - c_lo + 1;
    for (int dc = 0; dc < cw; ++dc) {
        kx[dc] = max(0.0, 3.0 - abs(float(c_lo + dc) - x));
    }
    for (int r = r_lo; r <= r_hi; ++r) {
        float ky = max(0.0, 3.0 - abs(float(r) - y));
        if (ky <= 0.0) continue;
        int sr = clamp(r - pc.sy0, 0, pc.sy_cnt - 1);
        int ro = sr * pc.W1 + c_lo;
        for (int dc = 0; dc < cw; ++dc) {
            float val = raw_slice[ro + dc];
            if (isnan(val) || isinf(val)) continue;
            uint ch = cfa_at_c13(r, c_lo + dc);
            float k = ky * kx[dc];
            sum[ch] += k * val;
            wt[ch] += k;
        }
    }
    return vec3(
        wt[0] > 0.0 ? sum[0] / wt[0] : 0.0,
        wt[1] > 0.0 ? sum[1] / wt[1] : 0.0,
        wt[2] > 0.0 ? sum[2] / wt[2] : 0.0
    );
}

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int yr = int(gl_GlobalInvocationID.y);
    if (c >= pc.Wh || yr >= pc.band_h) return;
    int r = pc.band_y0 + yr;
    float nan_v = uintBitsToFloat(0x7fc00000u);

    if (pc.write_fallback != 0) {
        vec3 fb = eval_raw_rgb(float(2 * r), float(2 * c));
        int foff = (yr * pc.Wh + c) * 3;
        fallback_rgb[foff + 0] = fb.x;
        fallback_rgb[foff + 1] = fb.y;
        fallback_rgb[foff + 2] = fb.z;
    }

    int hloc = clamp(r - pc.hy0, 0, pc.hy_cnt - 1) * pc.Wh + c;
    vec4 fm = (pc.has_flow != 0 || pc.has_mw != 0) ? flow_mw_slice[hloc] : vec4(0.0, 0.0, 1.0, 0.0);
    bool ok = (pc.has_mw == 0 || fm.z >= 0.7);
    float y = 2.0 * (float(r) + ((pc.has_flow != 0) ? fm.x : 0.0));
    float x = 2.0 * (float(c) + ((pc.has_flow != 0) ? fm.y : 0.0));
    if (isnan(y) || isinf(y) || isnan(x) || isinf(x) ||
        y < 0.0 || x < 0.0 || y > float(pc.H1 - 1) || x > float(pc.W1 - 1)) {
        ok = false;
    }

    int out_off = ((pc.frame_n * pc.band_h + yr) * pc.Wh + c) * 3;
    if (!ok) {
        rgb_stack[out_off + 0] = nan_v;
        rgb_stack[out_off + 1] = nan_v;
        rgb_stack[out_off + 2] = nan_v;
    } else {
        vec3 v = eval_raw_rgb(y, x);
        rgb_stack[out_off + 0] = v.x;
        rgb_stack[out_off + 1] = v.y;
        rgb_stack[out_off + 2] = v.z;
    }
}
"""

SHADERS["k_c13_scene"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float rgb_stack[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float fallback_rgb[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float scene_out[]; };

layout(push_constant) uniform PC {
    int Wh, band_y0, band_h, N;
} pc;

void main() {
    int c = int(gl_GlobalInvocationID.x);
    int yr = int(gl_GlobalInvocationID.y);
    if (c >= pc.Wh || yr >= pc.band_h) return;
    int px_loc = yr * pc.Wh + c;
    int frame_stride = pc.band_h * pc.Wh * 3;
    int dst_off = ((pc.band_y0 + yr) * pc.Wh + c) * 3;
    for (int ch = 0; ch < 3; ++ch) {
        float arr[16];
        int cnt = 0;
        for (int n = 0; n < pc.N; ++n) {
            float v = rgb_stack[n * frame_stride + px_loc * 3 + ch];
            if (!isnan(v)) arr[cnt++] = v;
        }
        if (cnt == 0) {
            scene_out[dst_off + ch] = fallback_rgb[px_loc * 3 + ch];
        } else {
            for (int i = 1; i < cnt; ++i) {
                float key = arr[i];
                int j = i - 1;
                while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; --j; }
                arr[j + 1] = key;
            }
            scene_out[dst_off + ch] = arr[cnt >> 1];
        }
    }
}
"""

SHADERS["k_c13_med_4ph"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float res[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float row4[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float out_ph[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { uint out_val[]; };

layout(push_constant) uniform PC {
    int H1, W1, is_col, out_stride;
    int res_off, row4_off, pad0, pad1;
} pc;

shared uint s_hist[256];
shared uint s_pref[256];
shared uint s_cnt_all;

uint float_to_sortkey(float v) {
    uint u = floatBitsToUint(v);
    return ((u & 0x80000000u) != 0u) ? (~u) : (u | 0x80000000u);
}

float sortkey_to_float(uint k) {
    uint u = ((k & 0x80000000u) != 0u) ? (k ^ 0x80000000u) : (~k);
    return uintBitsToFloat(u);
}

float load_val(int line_idx, int k) {
    if (pc.is_col == 0) {
        return res[pc.res_off + line_idx * pc.W1 + k];
    } else {
        float v = res[pc.res_off + k * pc.W1 + line_idx];
        return isnan(v) ? v : (v - row4[pc.row4_off + k * 4 + (line_idx & 3)]);
    }
}

void radix_select(int tid, int line_idx, int k0, int k_step, int line_len, out uint out_cnt, out float out_med) {
    uint prefix_val = 0u;
    uint rem = 0u;
    uint cnt = 0u;
    for (int pass = 0; pass < 4; ++pass) {
        int sh = (3 - pass) * 8;
        uint mask_hi = (pass == 0) ? 0u : (~((1u << (sh + 8)) - 1u));
        s_hist[tid] = 0u;
        if (pass == 0 && tid == 0) s_cnt_all = 0u;
        barrier();

        uint l_cnt = 0u;
        for (int k = k0 + tid * k_step; k < line_len; k += 256 * k_step) {
            float v = load_val(line_idx, k);
            if (isnan(v) || isinf(v)) continue;
            uint sk = float_to_sortkey(v);
            if (pass == 0) {
                ++l_cnt;
                atomicAdd(s_hist[(sk >> 24) & 0xFFu], 1u);
            } else if (((sk ^ prefix_val) & mask_hi) == 0u) {
                atomicAdd(s_hist[(sk >> sh) & 0xFFu], 1u);
            }
        }
        if (pass == 0 && l_cnt > 0u) atomicAdd(s_cnt_all, l_cnt);
        barrier();

        if (pass == 0) {
            cnt = s_cnt_all;
            if (cnt < 8u) {
                barrier();
                out_cnt = cnt;
                out_med = 0.0;
                return;
            }
            rem = cnt >> 1;
        }

        s_pref[tid] = s_hist[tid];
        barrier();
        for (int off = 1; off < 256; off <<= 1) {
            uint add_v = (tid >= off) ? s_pref[tid - off] : 0u;
            barrier();
            if (tid >= off) s_pref[tid] += add_v;
            barrier();
        }
        if (tid == 0) {
            uint found_b = 0u, before = 0u;
            for (uint b = 0u; b < 256u; ++b) {
                uint cend = s_pref[b];
                if (rem < cend) {
                    found_b = b;
                    before = (b == 0u) ? 0u : s_pref[b - 1u];
                    break;
                }
            }
            s_pref[0] = found_b;
            s_pref[1] = rem - before;
        }
        barrier();
        prefix_val |= (s_pref[0] << sh);
        rem = s_pref[1];
    }
    out_cnt = cnt;
    out_med = sortkey_to_float(prefix_val);
}

void main() {
    int tid = int(gl_LocalInvocationIndex);
    int line_idx = int(gl_WorkGroupID.x);
    int n_lines = (pc.is_col == 0) ? pc.H1 : pc.W1;
    int line_len = (pc.is_col == 0) ? pc.W1 : pc.H1;
    if (line_idx >= n_lines) return;

    float meds[4];
    uint cnts[4];
    bool need_pool = false;
    for (int ph = 0; ph < 4; ++ph) {
        radix_select(tid, line_idx, ph, 4, line_len, cnts[ph], meds[ph]);
        if (cnts[ph] < 8u) need_pool = true;
    }
    uint pool_cnt = 0u;
    float pool_med = 0.0;
    if (need_pool) {
        radix_select(tid, line_idx, 0, 2, line_len, pool_cnt, pool_med);
    }
    if (tid == 0) {
        bool have_pool = (pool_cnt >= 8u);
        for (int ph = 0; ph < 4; ++ph) {
            int oi = ph * pc.out_stride + line_idx;
            if (cnts[ph] >= 8u) {
                out_ph[oi] = meds[ph];
                out_val[oi] = 1u;
            } else if (have_pool) {
                out_ph[oi] = pool_med;
                out_val[oi] = 1u;
            } else {
                out_ph[oi] = 0.0;
                out_val[oi] = 0u;
            }
        }
    }
}
"""

SHADERS["k_c13_hp97"] = COMMON + r"""
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float in_ph[]; };
layout(set = 0, binding = 1) readonly buffer B1 { uint in_val[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float out_rc[]; };

layout(push_constant) uniform PC {
    int sz, is_col, out_off, pad0;
} pc;

void main() {
    uint g = gl_GlobalInvocationID.x;
    int total = pc.sz * 4;
    if (int(g) >= total) return;
    int ph = int(g) / pc.sz;
    int i  = int(g) % pc.sz;
    int base = ph * pc.sz;
    int dst_idx = pc.out_off + ((pc.is_col == 0) ? (i * 4 + ph) : (ph * pc.sz + i));
    if (in_val[base + i] == 0u) {
        out_rc[dst_idx] = 0.0;
        return;
    }
    int d0 = max(-48, -i), d1 = min(48, pc.sz - 1 - i);
    float sum = 0.0, den = 0.0;
    for (int d = d0; d <= d1; ++d) {
        int j = i + d;
        if (in_val[base + j] == 0u) continue;
        float w = exp(-0.5 * float(d * d) / 256.0);
        sum += w * in_ph[base + j];
        den += w;
    }
    out_rc[dst_idx] = in_ph[base + i] - sum / max(1e-20, den);
}
"""

SHADERS["k_c13_sensor_res"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float raw_in[]; };
layout(set = 0, binding = 1) readonly buffer B1 { vec4 flow_in[]; };
layout(set = 0, binding = 2) readonly buffer B2 { float scene_in[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float residual_out[]; };

layout(push_constant) uniform PC {
    int H, W, Hh, Wh;
    int band_y0, band_h, hy0, hy_cnt;
    int out_off, raw_has_band, has_flow, has_mw;
    float read_variance, shot;
    uint cfa0, cfa1, cfa2, pad0;
} pc;

uint cfa_at_sr_b(int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? pc.cfa0 : ((idx < 32) ? pc.cfa1 : pc.cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

vec4 sample_flow_sr_b(float y, float x) {
    y = clamp(y, 0.0, float(pc.Hh - 1));
    x = clamp(x, 0.0, float(pc.Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, pc.Hh - 1), x1 = min(x0 + 1, pc.Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    int sy0 = clamp(y0 - pc.hy0, 0, pc.hy_cnt - 1);
    int sy1 = clamp(y1 - pc.hy0, 0, pc.hy_cnt - 1);
    return mix(mix(flow_in[sy0 * pc.Wh + x0], flow_in[sy0 * pc.Wh + x1], fx),
               mix(flow_in[sy1 * pc.Wh + x0], flow_in[sy1 * pc.Wh + x1], fx), fy);
}

float sensor_scene_b(float y, float x, int ch) {
    y = clamp(y, 0.0, float(pc.Hh - 1));
    x = clamp(x, 0.0, float(pc.Wh - 1));
    int r = int(floor(y)), c = int(floor(x));
    int r1 = min(r + 1, pc.Hh - 1), c1 = min(c + 1, pc.Wh - 1);
    float ty = y - float(r), tx = x - float(c);
    return (1.0 - ty) * ((1.0 - tx) * scene_in[(r * pc.Wh + c) * 3 + ch] + tx * scene_in[(r * pc.Wh + c1) * 3 + ch]) +
           ty * ((1.0 - tx) * scene_in[(r1 * pc.Wh + c) * 3 + ch] + tx * scene_in[(r1 * pc.Wh + c1) * 3 + ch]);
}

void main() {
    int X = int(gl_GlobalInvocationID.x);
    int yr = int(gl_GlobalInvocationID.y);
    if (X >= pc.W || yr >= pc.band_h) return;
    int Y = pc.band_y0 + yr;
    int oi = pc.out_off + yr * pc.W + X;
    int ri = ((pc.raw_has_band != 0) ? yr : Y) * pc.W + X;
    residual_out[oi] = uintBitsToFloat(0x7fc00000u);

    float y = float(Y), x = float(X);
    if (pc.has_flow != 0) {
        for (int k = 0; k < 5; ++k) {
            vec4 f = sample_flow_sr_b(y * 0.5, x * 0.5);
            float yy = float(Y) - 2.0 * f.x;
            float xx = float(X) - 2.0 * f.y;
            bool settled = (yy == y && xx == x);
            y = yy; x = xx;
            if (settled) break;
        }
    }
    if (isnan(y) || isinf(y) || isnan(x) || isinf(x) ||
        y < 0.0 || x < 0.0 || y > float(pc.H - 1) || x > float(pc.W - 1)) return;
    if (pc.has_mw != 0 && sample_flow_sr_b(y * 0.5, x * 0.5).z < 0.7) return;
    int ch = int(cfa_at_sr_b(Y, X));
    float estimate = sensor_scene_b(y * 0.5, x * 0.5, ch);
    float v = raw_in[ri];
    if (isnan(v + estimate) || isinf(v + estimate) || estimate >= 0.15) return;
    float sigma = sqrt(max(1e-12, pc.read_variance + pc.shot * max(0.0, estimate)));
    float gx = 0.5 * (sensor_scene_b(y * 0.5, (x + 1.0) * 0.5, ch) -
                      sensor_scene_b(y * 0.5, (x - 1.0) * 0.5, ch));
    float gy = 0.5 * (sensor_scene_b((y + 1.0) * 0.5, x * 0.5, ch) -
                      sensor_scene_b((y - 1.0) * 0.5, x * 0.5, ch));
    if (gx * gx + gy * gy >= 4.0 * sigma * sigma) return;
    residual_out[oi] = v - estimate;
}
"""

SHADERS["k_c13_hot_map"] = COMMON + r"""
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
layout(set = 0, binding = 0) readonly buffer B0 { float res_band[]; };
layout(set = 0, binding = 1) readonly buffer B1 { float rc_atlas[]; };
layout(set = 0, binding = 2) writeonly buffer B2 { float hot_band[]; };
layout(set = 0, binding = 3) writeonly buffer B3 { float wg_stats[]; };

layout(push_constant) uniform PC {
    int H1, W1, N, band_y0;
    int band_h, wg_x, col_base_off, pad0;
    float hot_sigma, hot_thr, pad1, pad2;
} pc;

shared float s_b[256], s_a[256], s_r[256], s_s[256], s_c[256];

void main() {
    int tid = int(gl_LocalInvocationIndex);
    int wgx = int(gl_WorkGroupID.x), wgy = int(gl_WorkGroupID.y);
    int gx = wgx * 16 + int(gl_LocalInvocationID.x);
    int gy = wgy * 16 + int(gl_LocalInvocationID.y);

    float pb = 0.0, pa = 0.0, pr = 0.0, ps = 0.0, pchg = 0.0;
    if (gx < pc.W1 && gy < pc.band_h) {
        int r = pc.band_y0 + gy;
        int c_ph = gx & 3;
        int r_ph = r & 3;
        int frame_stride = pc.band_h * pc.W1;
        int row4_stride = pc.H1 * 4;
        int col4_stride = 4 * pc.W1;
        int px = gy * pc.W1 + gx;

        float cv[16], rc[16], dv_arr[16];
        bool ok[16];
        int valid_cnt = 0, above_thr = 0;
        for (int n = 0; n < pc.N; ++n) {
            float v = res_band[n * frame_stride + px];
            float cor0 = rc_atlas[(n * row4_stride + r * 4) + c_ph] +
                         rc_atlas[pc.col_base_off + n * col4_stride + r_ph * pc.W1 + gx];
            cv[n] = v;
            rc[n] = cor0;
            bool fin = (!isnan(v) && !isinf(v));
            ok[n] = fin;
            if (fin) {
                float dv = v - cor0;
                dv_arr[valid_cnt++] = dv;
                if (abs(dv) > pc.hot_thr) ++above_thr;
            }
        }

        float hot = 0.0;
        if (valid_cnt * 4 >= pc.N * 3 && valid_cnt >= 3 && above_thr >= (valid_cnt >> 1)) {
            for (int i = 1; i < valid_cnt; ++i) {
                float key = dv_arr[i];
                int j = i - 1;
                while (j >= 0 && dv_arr[j] > key) { dv_arr[j + 1] = dv_arr[j]; --j; }
                dv_arr[j + 1] = key;
            }
            float center = dv_arr[valid_cnt >> 1];
            float tol = max(3.0 * pc.hot_sigma, 0.35 * abs(center));
            int agree = 0;
            for (int i = 0; i < valid_cnt; ++i) {
                if (dv_arr[i] * center > 0.0 && abs(dv_arr[i] - center) < tol) ++agree;
            }
            if (abs(center) > pc.hot_thr && agree * 4 >= valid_cnt * 3) hot = center;
        }
        hot_band[px] = hot;

        for (int n = 0; n < pc.N; ++n) {
            float cor = rc[n] + hot;
            if (ok[n]) {
                float v = cv[n], d = v - cor;
                pb += v * v;
                pa += d * d;
                ps += 1.0;
            }
            pr += cor * cor;
            if (cor != 0.0) pchg += 1.0;
        }
    }

    s_b[tid] = pb; s_a[tid] = pa; s_r[tid] = pr; s_s[tid] = ps; s_c[tid] = pchg;
    barrier();
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) {
            s_b[tid] += s_b[tid + stride];
            s_a[tid] += s_a[tid + stride];
            s_r[tid] += s_r[tid + stride];
            s_s[tid] += s_s[tid + stride];
            s_c[tid] += s_c[tid + stride];
        }
        barrier();
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 5;
        wg_stats[wgi + 0] = s_b[0];
        wg_stats[wgi + 1] = s_a[0];
        wg_stats[wgi + 2] = s_r[0];
        wg_stats[wgi + 3] = s_s[0];
        wg_stats[wgi + 4] = s_c[0];
    }
}
"""

def main():
    if not (GLSLC or GLSLANG) or not SPIRV_VAL:
        raise SystemExit("Shader generation requires glslc/glslang and spirv-val on PATH")
    ROOT.mkdir(parents=True, exist_ok=True)
    out = [
        "// heraw_gpu_spirv.hpp -- Precompiled Vulkan 1.0 SPIR-V compute kernels for nijinLBS",
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace heraw_spirv {",
        ""
    ]
    total_bytes = 0
    for name, src in SHADERS.items():
        comp_path = ROOT / f"{name}.comp"
        spv_path = ROOT / f"{name}.spv"
        comp_path.write_text(src.strip() + "\n", encoding="ascii")
        command = ([GLSLC, "-fshader-stage=compute", "--target-env=vulkan1.0", "-O"] if GLSLC else
                   [GLSLANG, "-V", "--target-env", "vulkan1.0", "-S", "comp"])
        subprocess.run(command + [str(comp_path), "-o", str(spv_path)], check=True)
        subprocess.run([SPIRV_VAL, "--target-env", "vulkan1.0", str(spv_path)], check=True)
        raw = spv_path.read_bytes()
        assert len(raw) % 4 == 0
        words = struct.unpack(f"<{len(raw)//4}I", raw)
        total_bytes += len(raw)

        lines = []
        for i in range(0, len(words), 10):
            chunk = ", ".join(f"0x{w:08x}u" for w in words[i:i+10])
            lines.append("    " + chunk)
        out.append(f"static const uint32_t {name}_spv[{len(words)}] = {{")
        out.append(",\n".join(lines))
        out.append("};\n")
        print(f"Compiled {name:18s}: {len(words):5d} words ({len(raw):6d} B)")

    out.append("} // namespace heraw_spirv\n")
    OUT_HPP.write_text("\n".join(out), encoding="ascii")
    print(f"Generated {OUT_HPP} ({total_bytes // 1024} KB total SPIR-V across {len(SHADERS)} kernels)")

if __name__ == "__main__":
    main()
