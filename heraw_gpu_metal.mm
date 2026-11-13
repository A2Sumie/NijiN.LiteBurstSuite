// heraw_gpu_metal.mm -- Native macOS Metal Compute Backend for nijinLBS (Stage 4 + Stage 5)
#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "heraw_gpu_backend.hpp"

namespace heraw_gpu {

namespace {

static const char* kMslShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

inline float fast_exp_neg(float x) {
    return exp(-clamp(x, 0.0f, 40.0f));
}

struct PC_Zero { uint count; };
kernel void k_zero(
    device float* dst [[buffer(0)]],
    constant PC_Zero& pc [[buffer(6)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid < pc.count) dst[gid] = 0.0f;
}

struct PC_GreenAccum {
    int H1, W1, Hh, Wh, Ew, is_frame0, use_rob;
    float pg_sig0_sq, pg_gain;
};

inline float4 sample_flow_mw(device const float4* flow_mw, int Hh, int Wh, float y, float x) {
    y = clamp(y, 0.0f, float(Hh - 1));
    x = clamp(x, 0.0f, float(Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, Hh - 1), x1 = min(x0 + 1, Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(flow_mw[y0 * Wh + x0], flow_mw[y0 * Wh + x1], fx),
               mix(flow_mw[y1 * Wh + x0], flow_mw[y1 * Wh + x1], fx), fy);
}

kernel void k_green_accum(
    device const float* frame_n [[buffer(0)]],
    device const float4* flow_mw [[buffer(1)]],
    device const float4* icov_guide [[buffer(2)]],
    device float* acc_gv [[buffer(3)]],
    device float* acc_gw [[buffer(4)]],
    device float* acc_w2 [[buffer(5)]],
    constant PC_GreenAccum& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    float yh = 0.5f * float(Y) + 0.125f;
    float xh = 0.5f * float(X) + 0.125f;
    int yh_i = clamp(int(yh + 0.5f), 0, pc.Hh - 1);
    int xh_i = clamp(int(xh + 0.5f), 0, pc.Wh - 1);
    int h_idx = yh_i * pc.Wh + xh_i;

    float4 fm = sample_flow_mw(flow_mw, pc.Hh, pc.Wh, yh, xh);
    float mw = fm.z;
    if (mw < 0.02f) return;

    float dy1 = (pc.is_frame0 != 0) ? 0.0f : (2.0f * fm.x);
    float dx1 = (pc.is_frame0 != 0) ? 0.0f : (2.0f * fm.y);
    float yt0 = float(Y) + dy1;
    float xt0 = float(X) + dx1;
    if (yt0 < -0.5f || yt0 > float(pc.H1) - 0.5f || xt0 < -0.5f || xt0 > float(pc.W1) - 0.5f) return;

    float4 icg = icov_guide[h_idx];
    float axx = icg.x, axy = icg.y, ayy = icg.z, anchor = icg.w;
    float thr = 3.5f * sqrt(pc.pg_sig0_sq + pc.pg_gain * max(0.0f, anchor)) + 0.008f;

    float ws0 = 0.0f, ws1 = 0.0f, ws2 = 0.0f, ws3 = 0.0f;
    float vs0 = 0.0f, vs1 = 0.0f, vs2 = 0.0f, vs3 = 0.0f;
    float w2_0 = 0.0f;

    int rb = int(floor(yt0));
    int cb = int(floor(xt0));
    for (int rr = rb - 1; rr <= rb + 2; ++rr) {
        int r_cl = clamp(rr, 0, pc.H1 - 1);
        float dy_0 = float(rr) - yt0;
        float dy_1 = dy_0 - 0.5f;
        int cc_start = cb - 1;
        if (((r_cl + cc_start) & 1) == 0) ++cc_start;
        for (int cc = cc_start; cc <= cb + 2; cc += 2) {
            if (cc < 0 || cc >= pc.W1) continue;
            float val = frame_n[r_cl * pc.W1 + cc];
            float rob = mw;
            if (pc.use_rob != 0) {
                float ad = fabs(val - anchor);
                if (ad > thr) { float q = thr / ad; rob *= q * q * q; }
            }
            float dx_0 = float(cc) - xt0;
            float dx_1 = dx_0 - 0.5f;

            float d00 = axx * dx_0 * dx_0 + 2.0f * axy * dx_0 * dy_0 + ayy * dy_0 * dy_0;
            float d01 = axx * dx_1 * dx_1 + 2.0f * axy * dx_1 * dy_0 + ayy * dy_0 * dy_0;
            float d10 = axx * dx_0 * dx_0 + 2.0f * axy * dx_0 * dy_1 + ayy * dy_1 * dy_1;
            float d11 = axx * dx_1 * dx_1 + 2.0f * axy * dx_1 * dy_1 + ayy * dy_1 * dy_1;

            if (d00 <= 6.5f) { float w = rob * fast_exp_neg(0.5f * d00); ws0 += w; vs0 += w * val; w2_0 += w * w; }
            if (d01 <= 6.5f) { float w = rob * fast_exp_neg(0.5f * d01); ws1 += w; vs1 += w * val; }
            if (d10 <= 6.5f) { float w = rob * fast_exp_neg(0.5f * d10); ws2 += w; vs2 += w * val; }
            if (d11 <= 6.5f) { float w = rob * fast_exp_neg(0.5f * d11); ws3 += w; vs3 += w * val; }
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

struct PC_GreenFinal { int H1, W1, Hh, Wh, Ew; };
kernel void k_green_final(
    device const float* acc_gv [[buffer(0)]],
    device const float* acc_gw [[buffer(1)]],
    device float* acc_w2 [[buffer(2)]],
    device const float4* icov_guide [[buffer(3)]],
    device const float* g0_ha_1x [[buffer(4)]],
    device float* g_sr2x [[buffer(5)]],
    constant PC_GreenFinal& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    int yh_i = clamp(int(0.5f * float(Y) + 0.625f), 0, pc.Hh - 1);
    int xh_i = clamp(int(0.5f * float(X) + 0.625f), 0, pc.Wh - 1);
    float anchor = icov_guide[yh_i * pc.Wh + xh_i].w;

    int W2 = pc.W1 * 2;
    int r2_0 = (2 * Y) * W2 + (2 * X);
    int r2_1 = r2_0 + W2;

    float w0 = acc_gw[r2_0],     v0 = acc_gv[r2_0];
    float w1 = acc_gw[r2_0 + 1], v1 = acc_gv[r2_0 + 1];
    float w2 = acc_gw[r2_1],     v2 = acc_gv[r2_1];
    float w3 = acc_gw[r2_1 + 1], v3 = acc_gv[r2_1 + 1];

    float g0 = (w0 > 1e-8f) ? (v0 / w0) : anchor;
    float g1 = (w1 > 1e-8f) ? (v1 / w1) : anchor;
    float g2 = (w2 > 1e-8f) ? (v2 / w2) : anchor;
    float g3 = (w3 > 1e-8f) ? (v3 / w3) : anchor;

    float w_avg = 0.25f * (w0 + w1 + w2 + w3);
    float phase_def = clamp((0.72f - min(w0, w3) / max(max(w1, w2), 1e-6f)) / 0.28f, 0.0f, 0.48f);
    float alpha_ha = max(clamp((2.3f - w_avg) / 1.35f, 0.0f, 0.88f), phase_def);
    if (alpha_ha > 1e-3f) {
        int Xp = min(X + 1, pc.W1 - 1);
        int Yp = min(Y + 1, pc.H1 - 1);
        float h00 = g0_ha_1x[Y  * pc.W1 + X];
        float h01 = g0_ha_1x[Y  * pc.W1 + Xp];
        float h10 = g0_ha_1x[Yp * pc.W1 + X];
        float h11 = g0_ha_1x[Yp * pc.W1 + Xp];

        float w_nw = 1.0f / (fabs(h00 - h11) + 0.002f);
        float w_ne = 1.0f / (fabs(h01 - h10) + 0.002f);
        float diag = (w_nw * 0.5f * (h00 + h11) + w_ne * 0.5f * (h01 + h10)) / (w_nw + w_ne);

        g0 = mix(g0, h00, alpha_ha);
        g1 = mix(g1, 0.5f * (h00 + h01), alpha_ha);
        g2 = mix(g2, 0.5f * (h00 + h10), alpha_ha);
        g3 = mix(g3, diag, alpha_ha);
    }

    g_sr2x[r2_0]     = g0;
    g_sr2x[r2_0 + 1] = g1;
    g_sr2x[r2_1]     = g2;
    g_sr2x[r2_1 + 1] = g3;

    if (((Y | X) & 3) == 0) {
        int ei = (Y >> 2) * pc.Ew + (X >> 2);
        float w2_0 = acc_w2[ei];
        acc_w2[ei] = (w0 > 1e-8f) ? (w2_0 / (w0 * w0)) : -1.0f;
    }
}

struct PC_ChromaAccum {
    int H1, W1, Hh, Wh, is_frame0, use_rob;
    float pg_sig0_sq, pg_gain, wb_r, wb_b;
};

inline float bilerp_2d(device const float* img, int H, int W, float y, float x) {
    y = clamp(y, 0.0f, float(H - 1));
    x = clamp(x, 0.0f, float(W - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, H - 1), x1 = min(x0 + 1, W - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(img[y0 * W + x0], img[y0 * W + x1], fx),
               mix(img[y1 * W + x0], img[y1 * W + x1], fx), fy);
}

kernel void k_chroma_accum(
    device const float* frame_n [[buffer(0)]],
    device const float4* flow_mw [[buffer(1)]],
    device const float4* icov_guide [[buffer(2)]],
    device const float* g_sr2x [[buffer(3)]],
    device const float* c_anc_1x [[buffer(4)]],
    device float4* acc_chroma [[buffer(5)]],
    constant PC_ChromaAccum& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    int H2 = pc.H1 * 2, W2 = pc.W1 * 2;
    float2 add_r = float2(0.0f);
    float2 add_b = float2(0.0f);

    for (int is_b = 0; is_b < 2; ++is_b) {
        int r2 = 2 * Y + is_b;
        int c2 = 2 * X + is_b;
        float y1_ref = 0.5f * float(r2);
        float x1_ref = 0.5f * float(c2);
        float yh = 0.25f * float(r2);
        float xh = 0.25f * float(c2);

        float4 fm = sample_flow_mw(flow_mw, pc.Hh, pc.Wh, yh, xh);
        float mw = fm.z;
        if (mw < 0.02f) continue;

        float dy1 = (pc.is_frame0 != 0) ? 0.0f : (2.0f * fm.x);
        float dx1 = (pc.is_frame0 != 0) ? 0.0f : (2.0f * fm.y);
        float yt = y1_ref + dy1;
        float xt = x1_ref + dx1;
        if (yt < 0.0f || yt > float(pc.H1 - 1) || xt < 0.0f || xt > float(pc.W1 - 1)) continue;

        int yh_i = clamp(int(yh + 0.5f), 0, pc.Hh - 1);
        int xh_i = clamp(int(xh + 0.5f), 0, pc.Wh - 1);
        float4 icg = icov_guide[yh_i * pc.Wh + xh_i];
        float axx = 0.60f * icg.x, axy = 0.60f * icg.y, ayy = 0.60f * icg.z;

        int r1_anc = clamp(2 * yh_i + is_b, 0, pc.H1 - 1);
        int c1_anc = clamp(2 * xh_i + is_b, 0, pc.W1 - 1);
        float c_anc = c_anc_1x[r1_anc * pc.W1 + c1_anc];
        float thr = 4.0f * sqrt(pc.pg_sig0_sq + pc.pg_gain * max(0.0f, max(c_anc, icg.w))) + 0.01f;
        float wb = (is_b != 0) ? pc.wb_b : pc.wb_r;

        int qyc = int(floor((yt - float(is_b)) * 0.5f));
        int qxc = int(floor((xt - float(is_b)) * 0.5f));
        float d_sum = 0.0f, w_sum = 0.0f;

        for (int dq_y = 0; dq_y <= 1; ++dq_y) {
            int rr = clamp(2 * (qyc + dq_y) + is_b, is_b, pc.H1 - 2 + is_b);
            float dy = float(rr) - yt;
            for (int dq_x = 0; dq_x <= 1; ++dq_x) {
                int cc = clamp(2 * (qxc + dq_x) + is_b, is_b, pc.W1 - 2 + is_b);
                float dx = float(cc) - xt;
                float d2 = axx * dx * dx + 2.0f * axy * dx * dy + ayy * dy * dy;
                if (d2 > 7.5f) continue;

                float c_val = frame_n[rr * pc.W1 + cc] * wb;
                float g_at_q = bilerp_2d(g_sr2x, H2, W2, 2.0f * (float(rr) - dy1), 2.0f * (float(cc) - dx1));
                float diff_cg = c_val - g_at_q;

                float rob = 1.0f;
                if (pc.use_rob != 0) {
                    float ad = fabs(c_val - c_anc * wb);
                    float tw = thr * wb;
                    if (ad > tw) { float q = tw / ad; rob = q * q * q; }
                }
                float w = mw * rob * fast_exp_neg(0.5f * d2);
                w_sum += w;
                d_sum += w * diff_cg;
            }
        }
        if (is_b == 0) add_r = float2(d_sum, w_sum);
        else           add_b = float2(d_sum, w_sum);
    }

    acc_chroma[Y * pc.W1 + X] += float4(add_r, add_b);
}

struct PC_ChromaFinal {
    int H1, W1, Hh, Wh;
    float wb_r, wb_b, hl_start, hl_span;
};

kernel void k_chroma_final(
    device const float4* acc_chroma [[buffer(0)]],
    device const float* g_sr2x [[buffer(1)]],
    device const float* c_anc_1x [[buffer(2)]],
    device float* dR_1x [[buffer(3)]],
    device float* dB_1x [[buffer(4)]],
    constant PC_ChromaFinal& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    int W2 = pc.W1 * 2;
    int q_idx = Y * pc.W1 + X;
    float4 ac = acc_chroma[q_idx];

    for (int is_b = 0; is_b < 2; ++is_b) {
        int r2 = 2 * Y + is_b;
        int c2 = 2 * X + is_b;
        int yh_i = clamp(int(0.25f * float(r2) + 0.5f), 0, pc.Hh - 1);
        int xh_i = clamp(int(0.25f * float(c2) + 0.5f), 0, pc.Wh - 1);
        int r1_anc = clamp(2 * yh_i + is_b, 0, pc.H1 - 1);
        int c1_anc = clamp(2 * xh_i + is_b, 0, pc.W1 - 1);
        float c_anc = c_anc_1x[r1_anc * pc.W1 + c1_anc];
        float wb = (is_b != 0) ? pc.wb_b : pc.wb_r;
        float g_here = g_sr2x[r2 * W2 + c2];

        float d_sum = (is_b == 0) ? ac.x : ac.z;
        float w_sum = (is_b == 0) ? ac.y : ac.w;
        float dv = (w_sum > 1e-8f) ? (d_sum / w_sum) : (c_anc * wb - g_here);

        float hl_peak = max(g_here, c_anc);
        if (hl_peak > pc.hl_start) {
            float t = clamp((hl_peak - pc.hl_start) / pc.hl_span, 0.0f, 1.0f);
            dv *= (1.0f - t) * (1.0f - t);
        }
        if (is_b == 0) dR_1x[q_idx] = dv;
        else           dB_1x[q_idx] = dv;
    }
}

struct PC_GatFwd {
    uint count;
    float ped, g_eff, gat_c, sqrt_c;
};

kernel void k_gat_fwd(
    device const float* g_sr2x [[buffer(0)]],
    device float* z_gat [[buffer(1)]],
    device float* pyr0 [[buffer(2)]],
    constant PC_GatFwd& pc [[buffer(6)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= pc.count) return;
    float x = max(0.0f, g_sr2x[gid] + pc.ped);
    float zv = (2.0f * x) / (sqrt(pc.g_eff * x + pc.gat_c) + pc.sqrt_c);
    z_gat[gid] = zv;
    pyr0[gid]  = zv;
}

struct PC_BoxDown2x { int Hs, Ws, Hd, Wd; };
kernel void k_box_down2x(
    device const float* src [[buffer(0)]],
    device float* dst [[buffer(1)]],
    constant PC_BoxDown2x& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.Wd || r >= pc.Hd) return;
    int r0 = 2 * r, c0 = 2 * c;
    dst[r * pc.Wd + c] = 0.25f * (
        src[r0 * pc.Ws + c0] + src[r0 * pc.Ws + c0 + 1] +
        src[(r0 + 1) * pc.Ws + c0] + src[(r0 + 1) * pc.Ws + c0 + 1]
    );
}

struct PC_DctWiener {
    int H, W, Hh, Wh, is_wiener, scale_mul;
    float sigma;
};

inline void dct8_fwd_rows(thread float (&p)[8][8]) {
    const float c1 = 0.4903926402f, c2 = 0.4619397663f, c3 = 0.4157348062f, c4 = 0.3535533906f;
    const float c5 = 0.2777851165f, c6 = 0.1913417162f, c7 = 0.0975451610f;
    for (int i = 0; i < 8; ++i) {
        float s0 = p[i][0] + p[i][7], d7 = p[i][0] - p[i][7];
        float s1 = p[i][1] + p[i][6], d6 = p[i][1] - p[i][6];
        float s2 = p[i][2] + p[i][5], d5 = p[i][2] - p[i][5];
        float s3 = p[i][3] + p[i][4], d4 = p[i][3] - p[i][4];
        float e0 = s0 + s3, e3 = s0 - s3;
        float e1 = s1 + s2, e2 = s1 - s2;
        p[i][0] = (e0 + e1) * c4;
        p[i][4] = (e0 - e1) * c4;
        p[i][2] = e3 * c2 + e2 * c6;
        p[i][6] = e3 * c6 - e2 * c2;
        p[i][1] = d7 * c1 + d6 * c3 + d5 * c5 + d4 * c7;
        p[i][3] = d7 * c3 - d6 * c7 - d5 * c1 - d4 * c5;
        p[i][5] = d7 * c5 - d6 * c1 + d5 * c7 + d4 * c3;
        p[i][7] = d7 * c7 - d6 * c5 + d5 * c3 - d4 * c1;
    }
}

inline void dct8_fwd_cols(thread float (&p)[8][8]) {
    const float c1 = 0.4903926402f, c2 = 0.4619397663f, c3 = 0.4157348062f, c4 = 0.3535533906f;
    const float c5 = 0.2777851165f, c6 = 0.1913417162f, c7 = 0.0975451610f;
    for (int j = 0; j < 8; ++j) {
        float s0 = p[0][j] + p[7][j], d7 = p[0][j] - p[7][j];
        float s1 = p[1][j] + p[6][j], d6 = p[1][j] - p[6][j];
        float s2 = p[2][j] + p[5][j], d5 = p[2][j] - p[5][j];
        float s3 = p[3][j] + p[4][j], d4 = p[3][j] - p[4][j];
        float e0 = s0 + s3, e3 = s0 - s3;
        float e1 = s1 + s2, e2 = s1 - s2;
        p[0][j] = (e0 + e1) * c4;
        p[4][j] = (e0 - e1) * c4;
        p[2][j] = e3 * c2 + e2 * c6;
        p[6][j] = e3 * c6 - e2 * c2;
        p[1][j] = d7 * c1 + d6 * c3 + d5 * c5 + d4 * c7;
        p[3][j] = d7 * c3 - d6 * c7 - d5 * c1 - d4 * c5;
        p[5][j] = d7 * c5 - d6 * c1 + d5 * c7 + d4 * c3;
        p[7][j] = d7 * c7 - d6 * c5 + d5 * c3 - d4 * c1;
    }
}

inline void dct8_inv_cols(thread float (&p)[8][8]) {
    const float c1 = 0.4903926402f, c2 = 0.4619397663f, c3 = 0.4157348062f, c4 = 0.3535533906f;
    const float c5 = 0.2777851165f, c6 = 0.1913417162f, c7 = 0.0975451610f;
    for (int j = 0; j < 8; ++j) {
        float e0 = (p[0][j] + p[4][j]) * c4;
        float e1 = (p[0][j] - p[4][j]) * c4;
        float e3 = p[2][j] * c2 + p[6][j] * c6;
        float e2 = p[2][j] * c6 - p[6][j] * c2;
        float s0 = e0 + e3, s3 = e0 - e3;
        float s1 = e1 + e2, s2 = e1 - e2;
        float d7 = p[1][j] * c1 + p[3][j] * c3 + p[5][j] * c5 + p[7][j] * c7;
        float d6 = p[1][j] * c3 - p[3][j] * c7 - p[5][j] * c1 - p[7][j] * c5;
        float d5 = p[1][j] * c5 - p[3][j] * c1 + p[5][j] * c7 + p[7][j] * c3;
        float d4 = p[1][j] * c7 - p[3][j] * c5 + p[5][j] * c3 - p[7][j] * c1;
        p[0][j] = s0 + d7; p[7][j] = s0 - d7;
        p[1][j] = s1 + d6; p[6][j] = s1 - d6;
        p[2][j] = s2 + d5; p[5][j] = s2 - d5;
        p[3][j] = s3 + d4; p[4][j] = s3 - d4;
    }
}

inline void dct8_inv_rows(thread float (&p)[8][8]) {
    const float c1 = 0.4903926402f, c2 = 0.4619397663f, c3 = 0.4157348062f, c4 = 0.3535533906f;
    const float c5 = 0.2777851165f, c6 = 0.1913417162f, c7 = 0.0975451610f;
    for (int i = 0; i < 8; ++i) {
        float e0 = (p[i][0] + p[i][4]) * c4;
        float e1 = (p[i][0] - p[i][4]) * c4;
        float e3 = p[i][2] * c2 + p[i][6] * c6;
        float e2 = p[i][2] * c6 - p[i][6] * c2;
        float s0 = e0 + e3, s3 = e0 - e3;
        float s1 = e1 + e2, s2 = e1 - e2;
        float d7 = p[i][1] * c1 + p[i][3] * c3 + p[i][5] * c5 + p[i][7] * c7;
        float d6 = p[i][1] * c3 - p[i][3] * c7 - p[i][5] * c1 - p[i][7] * c5;
        float d5 = p[i][1] * c5 - p[i][3] * c1 + p[i][5] * c7 + p[i][7] * c3;
        float d4 = p[i][1] * c7 - p[i][3] * c5 + p[i][5] * c3 - p[i][7] * c1;
        p[i][0] = s0 + d7; p[i][7] = s0 - d7;
        p[i][1] = s1 + d6; p[i][6] = s1 - d6;
        p[i][2] = s2 + d5; p[i][5] = s2 - d5;
        p[i][3] = s3 + d4; p[i][4] = s3 - d4;
    }
}

kernel void k_dct_wiener(
    device const float* src_in [[buffer(0)]],
    device const float* guide_in [[buffer(1)]],
    device const float* snr_boost [[buffer(2)]],
    device float* dst_out [[buffer(3)]],
    constant PC_DctWiener& pc [[buffer(6)]],
    uint2 tid2 [[thread_position_in_threadgroup]],
    uint2 wgid [[threadgroup_position_in_grid]]
) {
    threadgroup float s_src[22][22];
    threadgroup float s_gde[22][22];
    threadgroup float s_blk[64][64];
    threadgroup float s_bw[64];
    threadgroup int   s_ry[64];
    threadgroup int   s_rx[64];

    int tx = int(tid2.x), ty = int(tid2.y);
    int tid = ty * 8 + tx;
    int out_x0 = int(wgid.x) * 10;
    int out_y0 = int(wgid.y) * 10;

    for (int k = tid; k < 22 * 22; k += 64) {
        int sy = k / 22, sx = k % 22;
        int gy = clamp(out_y0 - 6 + sy, 0, pc.H - 1);
        int gx = clamp(out_x0 - 6 + sx, 0, pc.W - 1);
        int gi = gy * pc.W + gx;
        s_src[sy][sx] = src_in[gi];
        if (pc.is_wiener != 0) s_gde[sy][sx] = guide_in[gi];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    int br = out_y0 - 6 + 2 * ty;
    int bc = out_x0 - 6 + 2 * tx;
    if ((pc.H & 1) != 0 && br == pc.H - 7) br = pc.H - 8;
    if ((pc.W & 1) != 0 && bc == pc.W - 7) bc = pc.W - 8;

    bool blk_ok = (br >= 0 && br <= pc.H - 8 && bc >= 0 && bc <= pc.W - 8);
    s_ry[tid] = br - out_y0;
    s_rx[tid] = bc - out_x0;

    if (blk_ok) {
        int sy0 = br - (out_y0 - 6);
        int sx0 = bc - (out_x0 - 6);
        int rh = clamp(((br + 4) * pc.scale_mul) >> 2, 0, pc.Hh - 1);
        int ch = clamp(((bc + 4) * pc.scale_mul) >> 2, 0, pc.Wh - 1);
        float sig = pc.sigma * snr_boost[rh * pc.Wh + ch];
        float p[8][8];
        float block_w = 1.0f;

        if (pc.is_wiener == 0) {
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j)
                    p[i][j] = s_src[sy0 + i][sx0 + j];
            dct8_fwd_rows(p);
            dct8_fwd_cols(p);
            float thr = 3.10f * sig;
            int nz = 1;
            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j) {
                    if (i == 0 && j == 0) continue;
                    if (fabs(p[i][j]) < thr) p[i][j] = 0.0f;
                    else ++nz;
                }
            }
            block_w = 1.0f / float(nz);
        } else {
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j)
                    p[i][j] = s_gde[sy0 + i][sx0 + j];
            dct8_fwd_rows(p);
            dct8_fwd_cols(p);

            float sig2 = 1.35f * sig * sig;
            float w2_sum = 1.0f;
            s_blk[tid][0] = 1.0f;
            for (int i = 0; i < 8; ++i) {
                for (int j = 0; j < 8; ++j) {
                    if (i == 0 && j == 0) continue;
                    float g2 = p[i][j] * p[i][j];
                    float gw = g2 / (g2 + sig2);
                    s_blk[tid][i * 8 + j] = gw;
                    w2_sum += gw * gw;
                }
            }
            block_w = 1.0f / w2_sum;

            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j)
                    p[i][j] = s_src[sy0 + i][sx0 + j];
            dct8_fwd_rows(p);
            dct8_fwd_cols(p);
            for (int i = 0; i < 8; ++i)
                for (int j = 0; j < 8; ++j)
                    p[i][j] *= s_blk[tid][i * 8 + j];
        }

        dct8_inv_cols(p);
        dct8_inv_rows(p);
        for (int i = 0; i < 8; ++i)
            for (int j = 0; j < 8; ++j)
                s_blk[tid][i * 8 + j] = block_w * p[i][j];
        s_bw[tid] = block_w;
    } else {
        s_bw[tid] = 0.0f;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (int k = tid; k < 100; k += 64) {
        int oy = k / 10, ox = k % 10;
        int gy = out_y0 + oy, gx = out_x0 + ox;
        if (gy >= pc.H || gx >= pc.W) continue;

        float acc = 0.0f, wgt = 0.0f;
        int by0 = max(0, oy >> 1), by1 = min(7, (oy + 7) >> 1);
        int bx0 = max(0, ox >> 1), bx1 = min(7, (ox + 7) >> 1);
        for (int by = by0; by <= by1; ++by) {
            for (int bx = bx0; bx <= bx1; ++bx) {
                int b = by * 8 + bx;
                float bw = s_bw[b];
                if (bw <= 0.0f) continue;
                int py = oy - s_ry[b];
                int px = ox - s_rx[b];
                if (uint(py) < 8u && uint(px) < 8u) {
                    acc += s_blk[b][py * 8 + px];
                    wgt += bw;
                }
            }
        }
        dst_out[gy * pc.W + gx] = (wgt > 1e-12f) ? (acc / wgt) : s_src[oy + 6][ox + 6];
    }
}

kernel void k_pyr_sub_down2x(
    device const float* den_cur [[buffer(0)]],
    device const float* den_coarse [[buffer(1)]],
    device float* diff_out [[buffer(2)]],
    constant PC_BoxDown2x& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.Wd || r >= pc.Hd) return;
    int r0 = 2 * r, c0 = 2 * c;
    float down = 0.25f * (
        den_cur[r0 * pc.Ws + c0] + den_cur[r0 * pc.Ws + c0 + 1] +
        den_cur[(r0 + 1) * pc.Ws + c0] + den_cur[(r0 + 1) * pc.Ws + c0 + 1]
    );
    diff_out[r * pc.Wd + c] = den_coarse[r * pc.Wd + c] - down;
}

struct PC_PyrLapAdd { int Hs, Ws, Hd, Wd; float tau2; };
kernel void k_pyr_lap_add(
    device const float* diff_coarse [[buffer(0)]],
    device float* den_cur [[buffer(1)]],
    constant PC_PyrLapAdd& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.Ws || r >= pc.Hs) return;
    float ys = (float(r) + 0.5f) * (float(pc.Hd) / float(pc.Hs)) - 0.5f;
    float xs = (float(c) + 0.5f) * (float(pc.Wd) / float(pc.Ws)) - 0.5f;
    float d = bilerp_2d(diff_coarse, pc.Hd, pc.Wd, ys, xs);
    float gate = pc.tau2 / (pc.tau2 + d * d);
    den_cur[r * pc.Ws + c] += 0.92f * gate * d;
}

struct PC_EdgePolish { int H, W, dir; };
kernel void k_edge_polish(
    device const float* src [[buffer(0)]],
    device float* dst [[buffer(1)]],
    constant PC_EdgePolish& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W || r >= pc.H) return;
    int roff = r * pc.W;
    int rm = max(r - 1, 0), rp = min(r + 1, pc.H - 1);
    int cm = max(c - 1, 0), cp = min(c + 1, pc.W - 1);
    float v0 = src[roff + c];
    float gx = src[roff + cp] - src[roff + cm];
    float gy = src[rp * pc.W + c] - src[rm * pc.W + c];
    if (gx * gx + gy * gy > 3.2f) {
        dst[roff + c] = v0;
        return;
    }
    const float k1d[5] = {1.0f/16.0f, 4.0f/16.0f, 6.0f/16.0f, 4.0f/16.0f, 1.0f/16.0f};
    const float inv_2s2 = 1.0f / (2.0f * 1.05f * 1.05f);
    float ws = 0.0f, vs = 0.0f;
    for (int k = -2; k <= 2; ++k) {
        float vk = (pc.dir == 0)
            ? src[roff + clamp(c + k, 0, pc.W - 1)]
            : src[clamp(r + k, 0, pc.H - 1) * pc.W + c];
        float d = vk - v0;
        float w = k1d[k + 2] * fast_exp_neg(d * d * inv_2s2);
        ws += w; vs += w * vk;
    }
    dst[roff + c] = vs / max(ws, 1e-12f);
}

struct PC_DvMap { int Hh, Wh, W2; };
kernel void k_dv_map(
    device const float* z_gat [[buffer(0)]],
    device const float* z_den [[buffer(1)]],
    device float* dv_map [[buffer(2)]],
    constant PC_DvMap& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.Wh || r >= pc.Hh) return;
    float s2 = 0.0f;
    for (int dy = 0; dy < 4; ++dy) {
        int ro = (4 * r + dy) * pc.W2 + 4 * c;
        for (int dx = 0; dx < 4; ++dx) {
            float d = z_gat[ro + dx] - z_den[ro + dx];
            s2 += d * d;
        }
    }
    dv_map[r * pc.Wh + c] = min(s2 * (1.0f / 16.0f), 1.25f);
}

struct PC_Gauss1D {
    int H, W, dir, radius;
    float w[25];
};

kernel void k_gauss_1d(
    device const float* src [[buffer(0)]],
    device float* dst [[buffer(1)]],
    constant PC_Gauss1D& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W || r >= pc.H) return;
    float acc = src[r * pc.W + c] * pc.w[0];
    if (pc.dir == 0) {
        int roff = r * pc.W;
        for (int k = 1; k <= pc.radius; ++k)
            acc += (src[roff + max(c - k, 0)] + src[roff + min(c + k, pc.W - 1)]) * pc.w[k];
    } else {
        for (int k = 1; k <= pc.radius; ++k)
            acc += (src[max(r - k, 0) * pc.W + c] + src[min(r + k, pc.H - 1) * pc.W + c]) * pc.w[k];
    }
    dst[r * pc.W + c] = acc;
}

struct PC_GatInvQuad {
    int H1, W1, Hh, Wh;
    float sqrt_c, g_eff, ped;
};

kernel void k_gat_inv_quad(
    device const float* z_den [[buffer(0)]],
    device const float* dv_map [[buffer(1)]],
    device float* g_sr2x [[buffer(2)]],
    device float* g_at_quad [[buffer(3)]],
    constant PC_GatInvQuad& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;
    int W2 = pc.W1 * 2;
    int rh = clamp(Y >> 1, 0, pc.Hh - 1);
    int ch = clamp(X >> 1, 0, pc.Wh - 1);
    float dv = dv_map[rh * pc.Wh + ch];

    int i00 = (2 * Y) * W2 + (2 * X);
    int i01 = i00 + 1;
    int i10 = i00 + W2;
    int i11 = i10 + 1;

    float z00 = z_den[i00], z01 = z_den[i01], z10 = z_den[i10], z11 = z_den[i11];
    float g00 = pc.sqrt_c * z00 + 0.25f * pc.g_eff * (z00 * z00 + dv) - pc.ped;
    float g01 = pc.sqrt_c * z01 + 0.25f * pc.g_eff * (z01 * z01 + dv) - pc.ped;
    float g10 = pc.sqrt_c * z10 + 0.25f * pc.g_eff * (z10 * z10 + dv) - pc.ped;
    float g11 = pc.sqrt_c * z11 + 0.25f * pc.g_eff * (z11 * z11 + dv) - pc.ped;

    g_sr2x[i00] = g00;
    g_sr2x[i01] = g01;
    g_sr2x[i10] = g10;
    g_sr2x[i11] = g11;
    g_at_quad[Y * pc.W1 + X] = 0.5f * (g01 + g10);
}

struct PC_Size2D { int H, W; };
#define PIX_SORT(a, b) { float t = min(a, b); b = max(a, b); a = t; }
kernel void k_median3x3(
    device const float* src [[buffer(0)]],
    device float* dst [[buffer(1)]],
    constant PC_Size2D& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
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

kernel void k_glint_desat(
    device const float* g_at_quad [[buffer(0)]],
    device const float* medG [[buffer(1)]],
    device float* dR [[buffer(2)]],
    device float* dB [[buffer(3)]],
    constant PC_Size2D& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W || r >= pc.H) return;
    int i = r * pc.W + c;
    float g = g_at_quad[i], g_bg = medG[i], dr = dR[i], db = dB[i];
    float glint_exc = max(0.0f, g - g_bg) + 0.5f * max(0.0f, max(dr, db));
    float hl_lvl = max(g, max(g + dr, g + db));
    float desat = max(clamp((glint_exc - 0.045f) / 0.12f, 0.0f, 0.92f),
                      clamp((hl_lvl - 0.32f) / 0.35f, 0.0f, 0.95f));
    if (desat > 0.0f) {
        dR[i] = dr * (1.0f - desat);
        dB[i] = db * (1.0f - desat);
    }
}

struct PC_Atrous1D {
    int H, W, Hh, Wh, dir, step;
    float inv_sr2, inv_sb2, inv_sg2;
};

kernel void k_atrous_1d(
    device const float* inR [[buffer(0)]],
    device const float* inB [[buffer(1)]],
    device const float* guide_G [[buffer(2)]],
    device const float* snr_boost [[buffer(3)]],
    device float* outR [[buffer(4)]],
    device float* outB [[buffer(5)]],
    constant PC_Atrous1D& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W || r >= pc.H) return;
    int i = r * pc.W + c;
    float boost = snr_boost[clamp(r >> 1, 0, pc.Hh - 1) * pc.Wh + clamp(c >> 1, 0, pc.Wh - 1)];
    float inv_b2 = 1.0f / (boost * boost);
    float isr2 = pc.inv_sr2 * inv_b2;
    float isb2 = pc.inv_sb2 * inv_b2;

    float cr0 = inR[i], cb0 = inB[i], cg0 = guide_G[i];
    const float k1d[5] = {1.0f/16.0f, 4.0f/16.0f, 6.0f/16.0f, 4.0f/16.0f, 1.0f/16.0f};
    float wr_s = 0.0f, vr_s = 0.0f, wb_s = 0.0f, vb_s = 0.0f;

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
    outR[i] = vr_s / max(wr_s, 1e-12f);
    outB[i] = vb_s / max(wb_s, 1e-12f);
}

struct PC_AssembleRgb {
    int H1, W1, H2, W2;
    float inv_wb_r, inv_wb_b;
};

kernel void k_assemble_rgb(
    device const float* g_sr2x [[buffer(0)]],
    device const float* dR_1x [[buffer(1)]],
    device const float* dB_1x [[buffer(2)]],
    device float* outR [[buffer(3)]],
    device float* outG [[buffer(4)]],
    device float* outB [[buffer(5)]],
    constant PC_AssembleRgb& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W2 || r >= pc.H2) return;
    int i = r * pc.W2 + c;
    float g = g_sr2x[i];
    float dr_wb = bilerp_2d(dR_1x, pc.H1, pc.W1, 0.5f * float(r), 0.5f * float(c));
    float db_wb = bilerp_2d(dB_1x, pc.H1, pc.W1, 0.5f * float(r - 1), 0.5f * float(c - 1));
    outR[i] = max(0.0f, (g + dr_wb) * pc.inv_wb_r);
    outG[i] = max(0.0f, g);
    outB[i] = max(0.0f, (g + db_wb) * pc.inv_wb_b);
}

kernel void k_rl_lum(
    device const float* R [[buffer(0)]],
    device const float* G [[buffer(1)]],
    device const float* B [[buffer(2)]],
    device float* lum0 [[buffer(3)]],
    constant PC_Zero& pc [[buffer(6)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= pc.count) return;
    lum0[gid] = max(1e-6f, 0.2126f * R[gid] + 0.7152f * G[gid] + 0.0722f * B[gid]);
}

struct PC_RlPrepH { int H, W, Hh, Wh; float g_thr2; };
kernel void k_rl_prep_h(
    device const float* lum0 [[buffer(0)]],
    device const float* conv [[buffer(1)]],
    device float* edge_gate [[buffer(2)]],
    device uint* hminmax_f16 [[buffer(3)]],
    device const uint* rl_mod_f16 [[buffer(4)]],
    constant PC_RlPrepH& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W || r >= pc.H) return;
    int rm = max(r - 1, 0), rp = min(r + 1, pc.H - 1);
    int cm = max(c - 1, 0), cp = min(c + 1, pc.W - 1);
    int i = r * pc.W + c;

    int rh = clamp(r >> 2, 0, pc.Hh - 1);
    int ch = clamp(c >> 2, 0, pc.Wh - 1);
    float2 rlm = float2(as_type<half2>(rl_mod_f16[rh * pc.Wh + ch]));

    float gx = 0.5f * (conv[r * pc.W + cp] - conv[r * pc.W + cm]);
    float gy = 0.5f * (conv[rp * pc.W + c] - conv[rm * pc.W + c]);
    float lap = conv[rm * pc.W + c] + conv[rp * pc.W + c] + conv[r * pc.W + cm] + conv[r * pc.W + cp] - 4.0f * conv[i];
    float se2 = gx * gx + gy * gy + 0.35f * lap * lap;
    edge_gate[i] = rlm.x * (se2 / (se2 + pc.g_thr2 * rlm.y));

    float hmin = lum0[i], hmax = lum0[i];
    int x0 = max(0, c - 4), x1 = min(pc.W - 1, c + 4);
    for (int cx = x0; cx <= x1; ++cx) {
        float v = lum0[r * pc.W + cx];
        hmin = min(hmin, v);
        hmax = max(hmax, v);
    }
    hminmax_f16[i] = as_type<uint>(half2(half(hmin), half(hmax)));
}

struct PC_RlPrepV { int H, W, is_astro; };
kernel void k_rl_prep_v(
    device const float* lum0 [[buffer(0)]],
    device const uint* hminmax_in [[buffer(1)]],
    device uint* bounds_out [[buffer(2)]],
    device float* est [[buffer(3)]],
    constant PC_RlPrepV& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.W || r >= pc.H) return;
    int y0 = max(0, r - 4), y1 = min(pc.H - 1, r + 4);
    float2 mm0 = float2(as_type<half2>(hminmax_in[y0 * pc.W + c]));
    float vmin = mm0.x, vmax = mm0.y;
    for (int ry = y0 + 1; ry <= y1; ++ry) {
        float2 mm = float2(as_type<half2>(hminmax_in[ry * pc.W + c]));
        vmin = min(vmin, mm.x);
        vmax = max(vmax, mm.y);
    }
    float span = vmax - vmin;
    int i = r * pc.W + c;
    float l0 = lum0[i];
    float lo_b = (pc.is_astro != 0) ? max(1e-6f, max(vmin, l0 - 0.12f * max(0.0f, l0 - vmin))) : max(1e-6f, vmin - 0.02f * span);
    float hi_b = (pc.is_astro != 0) ? (vmax + 0.06f * span) : (vmax + 0.02f * span);
    bounds_out[i] = as_type<uint>(half2(half(lo_b), half(hi_b)));
    est[i] = l0;
}

struct PC_RlRatio { uint count; float snr_knee, clamp_delta; };
kernel void k_rl_ratio(
    device const float* R [[buffer(0)]],
    device const float* G [[buffer(1)]],
    device const float* B [[buffer(2)]],
    device const float* edge_gate [[buffer(3)]],
    device float* conv [[buffer(4)]],
    constant PC_RlRatio& pc [[buffer(6)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= pc.count) return;
    float l0 = max(1e-6f, 0.2126f * R[gid] + 0.7152f * G[gid] + 0.0722f * B[gid]);
    float d = l0 / max(conv[gid], 1e-6f) - 1.0f;
    float snr_w = (l0 / (l0 + pc.snr_knee)) * edge_gate[gid];
    conv[gid] = 1.0f + clamp(d, -pc.clamp_delta, pc.clamp_delta) * snr_w;
}

kernel void k_rl_update(
    device float* est [[buffer(0)]],
    device const float* corr [[buffer(1)]],
    device const uint* bounds_f16 [[buffer(2)]],
    constant PC_Zero& pc [[buffer(6)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= pc.count) return;
    float2 b = float2(as_type<half2>(bounds_f16[gid]));
    est[gid] = clamp(est[gid] * corr[gid], b.x, b.y);
}

kernel void k_rl_apply(
    device float* R [[buffer(0)]],
    device float* G [[buffer(1)]],
    device float* B [[buffer(2)]],
    device const float* est [[buffer(3)]],
    constant PC_Zero& pc [[buffer(6)]],
    uint gid [[thread_position_in_grid]]
) {
    if (gid >= pc.count) return;
    float r = R[gid], g = G[gid], b = B[gid];
    float l0 = max(1e-6f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
    float gain = est[gid] / l0;
    R[gid] = max(0.0f, r * gain);
    G[gid] = max(0.0f, g * gain);
    B[gid] = max(0.0f, b * gain);
}
)MSL";

enum KernelId {
    K_ZERO = 0,
    K_GREEN_ACCUM,
    K_GREEN_FINAL,
    K_CHROMA_ACCUM,
    K_CHROMA_FINAL,
    K_GAT_FWD,
    K_BOX_DOWN2X,
    K_DCT_WIENER,
    K_PYR_SUB_DOWN2X,
    K_PYR_LAP_ADD,
    K_EDGE_POLISH,
    K_DV_MAP,
    K_GAUSS_1D,
    K_GAT_INV_QUAD,
    K_MEDIAN3X3,
    K_GLINT_DESAT,
    K_ATROUS_1D,
    K_ASSEMBLE_RGB,
    K_RL_LUM,
    K_RL_PREP_H,
    K_RL_PREP_V,
    K_RL_RATIO,
    K_RL_UPDATE,
    K_RL_APPLY,
    K_COUNT
};

static const char* kKernelNames[K_COUNT] = {
    "k_zero", "k_green_accum", "k_green_final", "k_chroma_accum", "k_chroma_final",
    "k_gat_fwd", "k_box_down2x", "k_dct_wiener", "k_pyr_sub_down2x", "k_pyr_lap_add",
    "k_edge_polish", "k_dv_map", "k_gauss_1d", "k_gat_inv_quad", "k_median3x3",
    "k_glint_desat", "k_atrous_1d", "k_assemble_rgb", "k_rl_lum", "k_rl_prep_h",
    "k_rl_prep_v", "k_rl_ratio", "k_rl_update", "k_rl_apply"
};

struct MetalContext {
    std::mutex mtx;
    bool inited = false;
    bool ok = false;
    std::string dev_name;
    std::string init_err;
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipes[K_COUNT] = {};

    bool ensure_init() {
        std::lock_guard<std::mutex> lk(mtx);
        if (inited) return ok;
        inited = true;

        @autoreleasepool {
            dev = MTLCreateSystemDefaultDevice();
            if (!dev) { init_err = "MTLCreateSystemDefaultDevice returned nil"; return false; }
            dev_name = dev.name ? [dev.name UTF8String] : "Apple Metal GPU";
            queue = [dev newCommandQueue];
            if (!queue) { init_err = "newCommandQueue failed"; return false; }

            MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
            NSError* err = nil;
            NSString* src = [NSString stringWithUTF8String:kMslShaderSource];
            id<MTLLibrary> lib = [dev newLibraryWithSource:src options:opts error:&err];
            if (!lib) {
                init_err = err ? [[err localizedDescription] UTF8String] : "MSL compile failed";
                return false;
            }
            for (int i = 0; i < K_COUNT; ++i) {
                id<MTLFunction> fn = [lib newFunctionWithName:[NSString stringWithUTF8String:kKernelNames[i]]];
                if (!fn) {
                    init_err = std::string("Missing MSL function: ") + kKernelNames[i];
                    return false;
                }
                pipes[i] = [dev newComputePipelineStateWithFunction:fn error:&err];
                if (!pipes[i]) {
                    init_err = err ? [[err localizedDescription] UTF8String] : "Pipeline creation failed";
                    return false;
                }
            }
            ok = true;
            return true;
        }
    }
};

static MetalContext& global_metal() {
    static MetalContext ctx;
    return ctx;
}

} // namespace

GpuDeviceInfo probe_gpu_device() {
    GpuDeviceInfo info;
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (dev) {
            info.available = true;
            info.api_name = "Metal";
            info.device_name = dev.name ? [dev.name UTF8String] : "Apple GPU";
        }
    }
    return info;
}

bool run_stage45_gpu(Stage45GpuJob& job) {
    @autoreleasepool {
        auto& ctx = global_metal();
        if (!ctx.ensure_init()) {
            job.out_err = ctx.init_err;
            return false;
        }

        const int H1 = job.H1, W1 = job.W1;
        const int Hh = H1 / 2, Wh = W1 / 2;
        const int H2 = H1 * 2, W2 = W1 * 2;
        const int Eh = (H1 + 3) / 4, Ew = (W1 + 3) / 4;
        const size_t n_half = (size_t)Hh * Wh;
        const size_t n_1x   = (size_t)H1 * W1;
        const size_t n_2x   = (size_t)H2 * W2;
        const size_t n_eta  = (size_t)Eh * Ew;

        const size_t max_single_bytes = n_2x * sizeof(float);
        if (ctx.dev.maxBufferLength < max_single_bytes) {
            job.out_err = "Device maxBufferLength smaller than 2x plane";
            return false;
        }

        auto alloc_buf = [&](size_t bytes) -> id<MTLBuffer> {
            return [ctx.dev newBufferWithLength:std::max<size_t>(16, bytes) options:MTLResourceStorageModeShared];
        };

        auto t_s4 = std::chrono::steady_clock::now();
        job.out_device_label = "Metal (" + ctx.dev_name + ")";

        // Core buffers
        id<MTLBuffer> b_frame   = alloc_buf(n_1x * sizeof(float));
        id<MTLBuffer> b_flow_mw = alloc_buf(n_half * sizeof(float) * 4);
        id<MTLBuffer> b_icov    = alloc_buf(n_half * sizeof(float) * 4);
        id<MTLBuffer> b_canc    = alloc_buf(n_1x * sizeof(float));
        id<MTLBuffer> b_g0ha    = alloc_buf(n_1x * sizeof(float));
        id<MTLBuffer> b_boost   = alloc_buf(n_half * sizeof(float));
        id<MTLBuffer> b_rlmod   = alloc_buf(n_half * sizeof(uint32_t));

        id<MTLBuffer> b_plane0  = alloc_buf(n_2x * sizeof(float)); // g_sr2x / outR
        id<MTLBuffer> b_plane1  = alloc_buf(n_2x * sizeof(float)); // acc_gv / z_gat / outG
        id<MTLBuffer> b_plane2  = alloc_buf(n_2x * sizeof(float)); // acc_gw / pyr0  / outB
        id<MTLBuffer> b_eta     = alloc_buf(n_eta * sizeof(float));
        id<MTLBuffer> b_dR      = alloc_buf(n_1x * sizeof(float));
        id<MTLBuffer> b_dB      = alloc_buf(n_1x * sizeof(float));

        if (!b_frame || !b_flow_mw || !b_icov || !b_canc || !b_g0ha || !b_boost || !b_rlmod ||
            !b_plane0 || !b_plane1 || !b_plane2 || !b_eta || !b_dR || !b_dB) {
            job.out_err = "Metal buffer allocation failed";
            return false;
        }

        // Pack icov_guide (vec4), c_anc_1x, g0_ha_1x, snr_boost, rl_mod_f16
        {
            float* dst = (float*)[b_icov contents];
            for (size_t i = 0; i < n_half; ++i) {
                dst[i * 4 + 0] = job.inv_cov[i * 3 + 0];
                dst[i * 4 + 1] = job.inv_cov[i * 3 + 1];
                dst[i * 4 + 2] = job.inv_cov[i * 3 + 2];
                dst[i * 4 + 3] = job.g_guide[i];
            }
            std::memcpy([b_canc contents], job.c_anc_1x, n_1x * sizeof(float));
            std::memcpy([b_g0ha contents], job.g0_ha_1x, n_1x * sizeof(float));
            std::memcpy([b_boost contents], job.snr_boost_half, n_half * sizeof(float));
            std::memcpy([b_rlmod contents], job.rl_mod_f16, n_half * sizeof(uint32_t));
        }

        auto enc_1d = [&](id<MTLComputeCommandEncoder> enc, KernelId kid,
                          std::initializer_list<id<MTLBuffer>> bufs, const void* pc, size_t pc_sz, uint32_t count) {
            [enc setComputePipelineState:ctx.pipes[kid]];
            int bi = 0;
            for (id<MTLBuffer> b : bufs) [enc setBuffer:b offset:0 atIndex:bi++];
            if (pc && pc_sz > 0) [enc setBytes:pc length:pc_sz atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((count + 255) / 256, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        };
        auto enc_2d = [&](id<MTLComputeCommandEncoder> enc, KernelId kid,
                          std::initializer_list<id<MTLBuffer>> bufs, const void* pc, size_t pc_sz, int W, int H) {
            [enc setComputePipelineState:ctx.pipes[kid]];
            int bi = 0;
            for (id<MTLBuffer> b : bufs) [enc setBuffer:b offset:0 atIndex:bi++];
            if (pc && pc_sz > 0) [enc setBytes:pc length:pc_sz atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((W + 15) / 16, (H + 15) / 16, 1)
                threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
        };

        // --- Stage 4A: 2x Quincunx Green Accumulation across N frames ---
        std::memset([b_plane1 contents], 0, n_2x * sizeof(float));
        std::memset([b_plane2 contents], 0, n_2x * sizeof(float));
        std::memset([b_eta contents], 0, n_eta * sizeof(float));

        for (int n = 0; n < job.N; ++n) {
            std::memcpy([b_frame contents], (*job.frames_lin)[n].data(), n_1x * sizeof(float));
            float* fmw = (float*)[b_flow_mw contents];
            const float* fy = (*job.flows_fy)[n].data();
            const float* fx = (*job.flows_fx)[n].data();
            const float* mw = (*job.mw_half)[n].data();
            for (size_t i = 0; i < n_half; ++i) {
                fmw[i * 4 + 0] = fy[i];
                fmw[i * 4 + 1] = fx[i];
                fmw[i * 4 + 2] = mw[i];
                fmw[i * 4 + 3] = 0.0f;
            }

            id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            PC_GreenAccum pc{H1, W1, Hh, Wh, Ew, (n == 0 ? 1 : 0), ((n > 0 || job.is_clean_plate) ? 1 : 0),
                             (float)job.pg_sig0_sq, (float)job.pg_gain};
            enc_2d(enc, K_GREEN_ACCUM, {b_frame, b_flow_mw, b_icov, b_plane1, b_plane2, b_eta}, &pc, sizeof(pc), W1, H1);
            if (n == job.N - 1) {
                PC_GreenFinal pcf{H1, W1, Hh, Wh, Ew};
                enc_2d(enc, K_GREEN_FINAL, {b_plane1, b_plane2, b_eta, b_icov, b_g0ha, b_plane0}, &pcf, sizeof(pcf), W1, H1);
            }
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }

        // Read back eta_eff from subsampled b_eta
        double eta_sum = 0.0;
        int64_t eta_cnt = 0;
        {
            const float* ep = (const float*)[b_eta contents];
            for (size_t i = 0; i < n_eta; ++i) {
                if (ep[i] >= 0.0f) { eta_sum += ep[i]; ++eta_cnt; }
            }
        }
        const double eta_eff = std::max(0.01, eta_sum / (double)std::max<int64_t>(1, eta_cnt));
        job.out_eta_eff = eta_eff;
        b_eta = nil;
        b_g0ha = nil;

        // --- Stage 4B: Chroma (dR_1x, dB_1x) Accumulation across N frames (reusing b_plane1 as vec4[n_1x]!) ---
        std::memset([b_plane1 contents], 0, n_1x * sizeof(float) * 4);
        for (int n = 0; n < job.N; ++n) {
            std::memcpy([b_frame contents], (*job.frames_lin)[n].data(), n_1x * sizeof(float));
            // Free 1x frame RAM immediately once copied into last pass!
            (*job.frames_lin)[n].clear();
            (*job.frames_lin)[n].shrink_to_fit();

            float* fmw = (float*)[b_flow_mw contents];
            const float* fy = (*job.flows_fy)[n].data();
            const float* fx = (*job.flows_fx)[n].data();
            const float* mw = (*job.mw_half)[n].data();
            for (size_t i = 0; i < n_half; ++i) {
                fmw[i * 4 + 0] = fy[i];
                fmw[i * 4 + 1] = fx[i];
                fmw[i * 4 + 2] = mw[i];
                fmw[i * 4 + 3] = 0.0f;
            }

            id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            PC_ChromaAccum pc{H1, W1, Hh, Wh, (n == 0 ? 1 : 0), ((n > 0 || job.is_clean_plate) ? 1 : 0),
                              (float)job.pg_sig0_sq, (float)job.pg_gain, job.wb_r, job.wb_b};
            enc_2d(enc, K_CHROMA_ACCUM, {b_frame, b_flow_mw, b_icov, b_plane0, b_canc, b_plane1}, &pc, sizeof(pc), W1, H1);
            if (n == job.N - 1) {
                PC_ChromaFinal pcf{H1, W1, Hh, Wh, job.wb_r, job.wb_b,
                                   job.is_astro ? 0.68f : 0.65f, job.is_astro ? 0.24f : 0.28f};
                enc_2d(enc, K_CHROMA_FINAL, {b_plane1, b_plane0, b_canc, b_dR, b_dB}, &pcf, sizeof(pcf), W1, H1);
            }
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }
        b_frame = nil;
        b_flow_mw = nil;
        b_icov = nil;
        b_canc = nil;

        double dt_s4 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_s4).count();
        if (job.on_stage4_done) job.on_stage4_done(eta_eff, dt_s4, job.out_device_label);

        // --- Stage 5A: 5-Level Multiscale 8x8 DCT Wiener on GPU ---
        const double g_eff = std::max(0.0, job.pg_gain * eta_eff);
        const double sig_eff_sq = std::max(1e-10, job.pg_sig0_sq * eta_eff);
        const double ped = 4.0 * std::sqrt(sig_eff_sq);
        const double gat_c = 0.375 * g_eff * g_eff + sig_eff_sq;
        const double sqrt_c = std::sqrt(gat_c);

        id<MTLBuffer> b_den0  = alloc_buf(n_2x * sizeof(float));
        id<MTLBuffer> b_pilot = alloc_buf(n_2x * sizeof(float));
        int Hs[5], Ws[5];
        Hs[0] = H2; Ws[0] = W2;
        id<MTLBuffer> b_pyr[5] = {};
        id<MTLBuffer> b_den[5] = {};
        b_pyr[0] = b_plane2;
        b_den[0] = b_den0;
        for (int s = 1; s < 5; ++s) {
            Hs[s] = Hs[s - 1] / 2;
            Ws[s] = Ws[s - 1] / 2;
            b_pyr[s] = alloc_buf((size_t)Hs[s] * Ws[s] * sizeof(float));
            b_den[s] = alloc_buf((size_t)Hs[s] * Ws[s] * sizeof(float));
        }
        id<MTLBuffer> b_dv1 = alloc_buf(n_half * sizeof(float));
        id<MTLBuffer> b_dv2 = alloc_buf(n_half * sizeof(float));
        id<MTLBuffer> b_gquad = alloc_buf(n_1x * sizeof(float));

        {
            id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            // Forward GAT: b_plane0 (g_sr2x) -> b_plane1 (z_gat) & b_pyr[0]
            PC_GatFwd pc_gf{(uint32_t)n_2x, (float)ped, (float)g_eff, (float)gat_c, (float)sqrt_c};
            enc_1d(enc, K_GAT_FWD, {b_plane0, b_plane1, b_pyr[0]}, &pc_gf, sizeof(pc_gf), (uint32_t)n_2x);

            if (job.denoise_strength > 1e-4f) {
                const float base_sig[5] = {1.35f, 1.15f, 0.78f, 0.48f, 0.28f};
                for (int s = 1; s < 5; ++s) {
                    PC_BoxDown2x pcd{Hs[s - 1], Ws[s - 1], Hs[s], Ws[s]};
                    enc_2d(enc, K_BOX_DOWN2X, {b_pyr[s - 1], b_pyr[s]}, &pcd, sizeof(pcd), Ws[s], Hs[s]);
                }
                for (int s = 4; s >= 0; --s) {
                    float sig_s = base_sig[s] * job.denoise_strength;
                    int tiles_x = (Ws[s] + 9) / 10;
                    int tiles_y = (Hs[s] + 9) / 10;

                    // Pass 1: Hard-threshold Pilot -> b_pilot
                    PC_DctWiener pcp{Hs[s], Ws[s], Hh, Wh, 0, (1 << s), sig_s};
                    [enc setComputePipelineState:ctx.pipes[K_DCT_WIENER]];
                    [enc setBuffer:b_pyr[s] offset:0 atIndex:0];
                    [enc setBuffer:b_pyr[s] offset:0 atIndex:1];
                    [enc setBuffer:b_boost  offset:0 atIndex:2];
                    [enc setBuffer:b_pilot  offset:0 atIndex:3];
                    [enc setBytes:&pcp length:sizeof(pcp) atIndex:6];
                    [enc dispatchThreadgroups:MTLSizeMake(tiles_x, tiles_y, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];

                    // Pass 2: Empirical Wiener -> b_den[s]
                    PC_DctWiener pcw{Hs[s], Ws[s], Hh, Wh, 1, (1 << s), sig_s};
                    [enc setBuffer:b_pyr[s] offset:0 atIndex:0];
                    [enc setBuffer:b_pilot  offset:0 atIndex:1];
                    [enc setBuffer:b_boost  offset:0 atIndex:2];
                    [enc setBuffer:b_den[s] offset:0 atIndex:3];
                    [enc setBytes:&pcw length:sizeof(pcw) atIndex:6];
                    [enc dispatchThreadgroups:MTLSizeMake(tiles_x, tiles_y, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];

                    if (s < 4) {
                        // Reuse b_pilot for diff_coarse
                        PC_BoxDown2x pcs{Hs[s], Ws[s], Hs[s + 1], Ws[s + 1]};
                        enc_2d(enc, K_PYR_SUB_DOWN2X, {b_den[s], b_den[s + 1], b_pilot}, &pcs, sizeof(pcs), Ws[s + 1], Hs[s + 1]);
                        float tau = std::max(1e-4f, 3.4f * sig_s);
                        PC_PyrLapAdd pcl{Hs[s], Ws[s], Hs[s + 1], Ws[s + 1], tau * tau};
                        enc_2d(enc, K_PYR_LAP_ADD, {b_pilot, b_den[s]}, &pcl, sizeof(pcl), Ws[s], Hs[s]);
                    }
                }
                if (job.edge_polish) {
                    PC_EdgePolish pce0{H2, W2, 0}, pce1{H2, W2, 1};
                    enc_2d(enc, K_EDGE_POLISH, {b_den[0], b_pilot}, &pce0, sizeof(pce0), W2, H2);
                    enc_2d(enc, K_EDGE_POLISH, {b_pilot, b_den[0]}, &pce1, sizeof(pce1), W2, H2);
                }
            } else {
                // Bypass luminance denoise: copy pyr[0] -> den[0] via edge_polish skipped; just use b_pyr[0]
                b_den[0] = b_pyr[0];
            }

            // dv_map + Gaussian(sigma=8.0) + exact GAT inverse -> b_plane0 (g_sr2x) & b_gquad
            PC_DvMap pcdv{Hh, Wh, W2};
            enc_2d(enc, K_DV_MAP, {b_plane1, b_den[0], b_dv1}, &pcdv, sizeof(pcdv), Wh, Hh);
            PC_Gauss1D g8h = make_gauss_pc(Hh, Wh, 0, 8.0f);
            PC_Gauss1D g8v = make_gauss_pc(Hh, Wh, 1, 8.0f);
            enc_2d(enc, K_GAUSS_1D, {b_dv1, b_dv2}, &g8h, sizeof(g8h), Wh, Hh);
            enc_2d(enc, K_GAUSS_1D, {b_dv2, b_dv1}, &g8v, sizeof(g8v), Wh, Hh);

            PC_GatInvQuad pcgi{H1, W1, Hh, Wh, (float)sqrt_c, (float)g_eff, (float)ped};
            enc_2d(enc, K_GAT_INV_QUAD, {b_den[0], b_dv1, b_plane0, b_gquad}, &pcgi, sizeof(pcgi), W1, H1);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }

        for (int s = 1; s < 5; ++s) { b_pyr[s] = nil; b_den[s] = nil; }
        b_den0 = nil;
        b_pilot = nil;
        b_dv1 = nil;
        b_dv2 = nil;

        // Compute exact mean_g_pos and g_edge_scale on CPU from unified-memory b_gquad (~1 ms)
        double mean_g_pos = 0.05;
        std::vector<float> g_sub;
        {
            const float* gq = (const float*)[b_gquad contents];
            double acc = 0.0;
            for (size_t i = 0; i < n_1x; i += 16) acc += std::max(0.0f, gq[i]);
            mean_g_pos = acc / (double)((n_1x + 15) / 16);
            g_sub.reserve((size_t)(H1 / 8 + 1) * (size_t)(W1 / 8 + 1));
            for (int r = 0; r < H1; r += 8) {
                for (int c = 0; c + 2 < W1; c += 8) {
                    g_sub.push_back(std::fabs(gq[(size_t)r * W1 + c + 2] - gq[(size_t)r * W1 + c]));
                }
            }
        }

        // --- Stage 5B: Chroma Median + Glint Desat + 6-Level Guided A-Trous + RGB Assembly (+ RL) ---
        id<MTLBuffer> b_tmpR = alloc_buf(n_1x * sizeof(float));
        id<MTLBuffer> b_tmpB = alloc_buf(n_1x * sizeof(float));
        id<MTLBuffer> b_outR = alloc_buf(n_2x * sizeof(float)); // planar R (keep b_plane0 safe during assemble!)
        if (!b_tmpR || !b_tmpB || !b_outR) {
            job.out_err = "Metal chroma/RGB buffer allocation failed";
            return false;
        }

        const float resid_noise_2x = 0.35f * (float)std::sqrt(sig_eff_sq + g_eff * mean_g_pos);
        job.out_resid_noise_2x = resid_noise_2x;

        {
            id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            if (job.chroma_denoise > 1e-4f) {
                PC_Size2D pcs1{H1, W1};
                enc_2d(enc, K_MEDIAN3X3, {b_dR, b_tmpR}, &pcs1, sizeof(pcs1), W1, H1);
                if (!job.is_astro) {
                    enc_2d(enc, K_MEDIAN3X3, {b_tmpR, b_dR}, &pcs1, sizeof(pcs1), W1, H1);
                } else {
                    std::swap(b_dR, b_tmpR);
                }
                enc_2d(enc, K_MEDIAN3X3, {b_dB, b_tmpB}, &pcs1, sizeof(pcs1), W1, H1);
                if (!job.is_astro) {
                    enc_2d(enc, K_MEDIAN3X3, {b_tmpB, b_dB}, &pcs1, sizeof(pcs1), W1, H1);
                } else {
                    std::swap(b_dB, b_tmpB);
                }

                if (job.glint_desat) {
                    PC_Gauss1D g25h = make_gauss_pc(H1, W1, 0, 2.5f);
                    PC_Gauss1D g25v = make_gauss_pc(H1, W1, 1, 2.5f);
                    enc_2d(enc, K_GAUSS_1D, {b_gquad, b_tmpR}, &g25h, sizeof(g25h), W1, H1);
                    enc_2d(enc, K_GAUSS_1D, {b_tmpR, b_tmpB}, &g25v, sizeof(g25v), W1, H1);
                    enc_2d(enc, K_GLINT_DESAT, {b_gquad, b_tmpB, b_dR, b_dB}, &pcs1, sizeof(pcs1), W1, H1);
                }

                float wb_max = std::max(job.wb_r, job.wb_b);
                float chroma_sig = job.chroma_denoise * (float)std::sqrt((1.0 + wb_max * wb_max) * eta_eff * (job.pg_sig0_sq + job.pg_gain * mean_g_pos));
                if (chroma_sig > 1e-6f) {
                    float sig_g = std::max(1e-4f, 0.5f * chroma_sig);
                    float g_edge_scale = std::max(job.is_astro ? 0.14f : (3.0f * sig_g), 2.2f * quick_pct_gpu_helper(g_sub, 0.80f) + 0.006f);
                    float inv_sg2 = 1.0f / (2.0f * g_edge_scale * g_edge_scale);
                    for (int lv = 0; lv < 6; ++lv) {
                        int step = 1 << lv;
                        float lv_dec = std::pow(0.56f, (float)lv);
                        float sr = 2.7f * chroma_sig * lv_dec + 0.0015f;
                        float inv_sr2 = 1.0f / (2.0f * sr * sr);
                        PC_Atrous1D pah{H1, W1, Hh, Wh, 0, step, inv_sr2, inv_sr2, inv_sg2};
                        PC_Atrous1D pav{H1, W1, Hh, Wh, 1, step, inv_sr2, inv_sr2, inv_sg2};
                        enc_2d(enc, K_ATROUS_1D, {b_dR, b_dB, b_gquad, b_boost, b_tmpR, b_tmpB}, &pah, sizeof(pah), W1, H1);
                        enc_2d(enc, K_ATROUS_1D, {b_tmpR, b_tmpB, b_gquad, b_boost, b_dR, b_dB}, &pav, sizeof(pav), W1, H1);
                    }
                }
            }

            // Assemble planar R (b_outR), G (b_plane1), B (b_plane2) from g_sr2x (b_plane0) + dR + dB
            PC_AssembleRgb pca{H1, W1, H2, W2, 1.0f / std::max(1e-4f, job.wb_r), 1.0f / std::max(1e-4f, job.wb_b)};
            enc_2d(enc, K_ASSEMBLE_RGB, {b_plane0, b_dR, b_dB, b_outR, b_plane1, b_plane2}, &pca, sizeof(pca), W2, H2);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
        }

        b_dR = nil; b_dB = nil; b_tmpR = nil; b_tmpB = nil; b_gquad = nil; b_boost = nil;

        // Optional PSF Richardson-Lucy Deconvolution (reusing b_plane0 as scratch!)
        if (job.psf_iters > 0) {
            id<MTLBuffer> b_est   = alloc_buf(n_2x * sizeof(float));
            id<MTLBuffer> b_conv  = alloc_buf(n_2x * sizeof(float));
            id<MTLBuffer> b_gate  = alloc_buf(n_2x * sizeof(float));
            id<MTLBuffer> b_bnds  = alloc_buf(n_2x * sizeof(uint32_t));
            if (b_est && b_conv && b_gate && b_bnds) {
                id<MTLCommandBuffer> cb = [ctx.queue commandBuffer];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

                // b_plane0 = lum0
                PC_Zero pcn{(uint32_t)n_2x};
                enc_1d(enc, K_RL_LUM, {b_outR, b_plane1, b_plane2, b_plane0}, &pcn, sizeof(pcn), (uint32_t)n_2x);

                PC_Gauss1D g065h = make_gauss_pc(H2, W2, 0, 0.65f);
                PC_Gauss1D g065v = make_gauss_pc(H2, W2, 1, 0.65f);
                enc_2d(enc, K_GAUSS_1D, {b_plane0, b_est}, &g065h, sizeof(g065h), W2, H2);
                enc_2d(enc, K_GAUSS_1D, {b_est, b_conv}, &g065v, sizeof(g065v), W2, H2);

                float g_thr = std::max(0.0009f, 2.2f * resid_noise_2x);
                PC_RlPrepH pcph{H2, W2, Hh, Wh, g_thr * g_thr};
                // Use b_est temporarily as uint32_t hminmax_f16
                enc_2d(enc, K_RL_PREP_H, {b_plane0, b_conv, b_gate, b_est, b_rlmod}, &pcph, sizeof(pcph), W2, H2);
                PC_RlPrepV pcpv{H2, W2, job.is_astro ? 1 : 0};
                // Reuse b_conv as est_out first, then swap!
                enc_2d(enc, K_RL_PREP_V, {b_plane0, b_est, b_bnds, b_conv}, &pcpv, sizeof(pcpv), W2, H2);
                std::swap(b_est, b_conv);

                // Blur edge_gate with sigma=1.0 using b_plane0 as temp
                PC_Gauss1D g10h = make_gauss_pc(H2, W2, 0, 1.0f);
                PC_Gauss1D g10v = make_gauss_pc(H2, W2, 1, 1.0f);
                enc_2d(enc, K_GAUSS_1D, {b_gate, b_plane0}, &g10h, sizeof(g10h), W2, H2);
                enc_2d(enc, K_GAUSS_1D, {b_plane0, b_gate}, &g10v, sizeof(g10v), W2, H2);

                PC_Gauss1D gpsfh = make_gauss_pc(H2, W2, 0, job.psf_sigma);
                PC_Gauss1D gpsfv = make_gauss_pc(H2, W2, 1, job.psf_sigma);
                float snr_knee = std::max(0.003f, 3.2f * resid_noise_2x);
                PC_RlRatio pcrr{(uint32_t)n_2x, snr_knee, 0.18f};

                for (int it = 0; it < job.psf_iters; ++it) {
                    enc_2d(enc, K_GAUSS_1D, {b_est, b_plane0}, &gpsfh, sizeof(gpsfh), W2, H2);
                    enc_2d(enc, K_GAUSS_1D, {b_plane0, b_conv}, &gpsfv, sizeof(gpsfv), W2, H2);
                    enc_1d(enc, K_RL_RATIO, {b_outR, b_plane1, b_plane2, b_gate, b_conv}, &pcrr, sizeof(pcrr), (uint32_t)n_2x);
                    enc_2d(enc, K_GAUSS_1D, {b_conv, b_plane0}, &gpsfh, sizeof(gpsfh), W2, H2);
                    enc_2d(enc, K_GAUSS_1D, {b_plane0, b_conv}, &gpsfv, sizeof(gpsfv), W2, H2);
                    enc_1d(enc, K_RL_UPDATE, {b_est, b_conv, b_bnds}, &pcn, sizeof(pcn), (uint32_t)n_2x);
                }
                enc_1d(enc, K_RL_APPLY, {b_outR, b_plane1, b_plane2, b_est}, &pcn, sizeof(pcn), (uint32_t)n_2x);

                [enc endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
            }
        }
        b_plane0 = nil;
        b_rlmod = nil;

        // Interleave planar R, G, B from unified memory into job.rgb_2x_out in parallel
        const float* pR = (const float*)[b_outR contents];
        const float* pG = (const float*)[b_plane1 contents];
        const float* pB = (const float*)[b_plane2 contents];
        const int nt = std::max(1, job.n_threads);
        std::vector<std::thread> workers;
        int rows_per = (H2 + nt - 1) / nt;
        for (int t = 0; t < nt; ++t) {
            int r0 = t * rows_per;
            int r1 = std::min(H2, r0 + rows_per);
            if (r0 >= r1) break;
            workers.emplace_back([=]() {
                for (int r = r0; r < r1; ++r) {
                    size_t i0 = (size_t)r * W2;
                    float* dst = job.rgb_2x_out + i0 * 3;
                    for (int c = 0; c < W2; ++c) {
                        dst[c * 3 + 0] = pR[i0 + c];
                        dst[c * 3 + 1] = pG[i0 + c];
                        dst[c * 3 + 2] = pB[i0 + c];
                    }
                }
            });
        }
        for (auto& th : workers) th.join();
        return true;
    }
}

} // namespace heraw_gpu

#endif // __APPLE__
