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
