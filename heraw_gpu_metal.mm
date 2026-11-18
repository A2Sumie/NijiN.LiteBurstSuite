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
    int H1, W1, Hh, Wh, Ew, is_frame0, use_rob, is_xtrans;
    uint cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, frame_w_scale;
    int origin_y, origin_x;
    float q_luma_var, q_nlt_s0, q_nlt_s1, q_nlt_s2;
};

inline uint cfa_at(int r, int c, uint w0, uint w1, uint w2) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? w0 : ((idx < 32) ? w1 : w2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

inline float4 sample_flow_mw(device const float4* flow_mw, int Hh, int Wh, float y, float x) {
    y = clamp(y, 0.0f, float(Hh - 1));
    x = clamp(x, 0.0f, float(Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, Hh - 1), x1 = min(x0 + 1, Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    return mix(mix(flow_mw[y0 * Wh + x0], flow_mw[y0 * Wh + x1], fx),
               mix(flow_mw[y1 * Wh + x0], flow_mw[y1 * Wh + x1], fx), fy);
}

struct PC_SensorResidual {
    int H,W,Hh,Wh;
    float read_variance,shot;
    uint cfa0,cfa1,cfa2;
};
inline float sensor_scene(device const float* s,int H,int W,float y,float x,int ch) {
    y=clamp(y,0.0f,float(H-1));x=clamp(x,0.0f,float(W-1));
    int r=int(y),c=int(x),r1=min(r+1,H-1),c1=min(c+1,W-1);
    float ty=y-r,tx=x-c;
    return (1-ty)*((1-tx)*s[(r*W+c)*3+ch]+tx*s[(r*W+c1)*3+ch])+
        ty*((1-tx)*s[(r1*W+c)*3+ch]+tx*s[(r1*W+c1)*3+ch]);
}
kernel void k_sensor_residual(
    device const float* raw [[buffer(0)]],
    device const float4* flow [[buffer(1)]],
    device const float* scene [[buffer(2)]],
    device float* residual [[buffer(3)]],
    constant PC_SensorResidual& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]) {
    int Y=int(gid.y),X=int(gid.x);
    if(Y>=pc.H||X>=pc.W) return;
    int i=Y*pc.W+X;
    residual[i]=as_type<float>(0x7fc00000u);
    float y=float(Y),x=float(X);
    for(int k=0;k<5;++k) {
        float4 f=sample_flow_mw(flow,pc.Hh,pc.Wh,y*.5f,x*.5f);
        float yy=Y-2*f.x,xx=X-2*f.y;
        bool settled=yy==y&&xx==x;y=yy;x=xx;
        if(settled) break;
    }
    if(!isfinite(y)||!isfinite(x)||y<0||x<0||y>pc.H-1||x>pc.W-1) return;
    if(sample_flow_mw(flow,pc.Hh,pc.Wh,y*.5f,x*.5f).z<.7f) return;
    int ch=int(cfa_at(Y,X,pc.cfa0,pc.cfa1,pc.cfa2));
    float estimate=sensor_scene(scene,pc.Hh,pc.Wh,y*.5f,x*.5f,ch);
    float v=raw[i];
    if(!isfinite(v+estimate)||estimate>=.15f) return;
    float sigma=sqrt(max(1e-12f,pc.read_variance+pc.shot*max(0.0f,estimate)));
    float gx=.5f*(sensor_scene(scene,pc.Hh,pc.Wh,y*.5f,(x+1)*.5f,ch)-
                    sensor_scene(scene,pc.Hh,pc.Wh,y*.5f,(x-1)*.5f,ch));
    float gy=.5f*(sensor_scene(scene,pc.Hh,pc.Wh,(y+1)*.5f,x*.5f,ch)-
                    sensor_scene(scene,pc.Hh,pc.Wh,(y-1)*.5f,x*.5f,ch));
    if(gx*gx+gy*gy>=4*sigma*sigma) return;
    residual[i]=v-estimate;
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
    float mw = fm.z * pc.frame_w_scale;
    if (mw < 0.02f) return;

    float dy1 = (pc.is_frame0 != 0) ? 0.0f : (2.0f * fm.x);
    float dx1 = (pc.is_frame0 != 0) ? 0.0f : (2.0f * fm.y);
    float yt0 = (float(Y + pc.origin_y) + dy1) - float(pc.origin_y);
    float xt0 = (float(X + pc.origin_x) + dx1) - float(pc.origin_x);
    if (yt0 < -0.5f || yt0 > float(pc.H1) - 0.5f || xt0 < -0.5f || xt0 > float(pc.W1) - 0.5f) return;

    float4 icg = icov_guide[h_idx];
    float axx = icg.x, axy = icg.y, ayy = icg.z, anchor = icg.w;
    float a_pos = max(0.0f, anchor);
    float ax_cl = min(1.0f, a_pos);
    float qv = pc.q_luma_var * max(0.0f, pc.q_nlt_s0 + ax_cl * (pc.q_nlt_s1 + ax_cl * pc.q_nlt_s2));
    float thr = 3.5f * sqrt(pc.pg_sig0_sq + pc.pg_gain * a_pos + qv) + 0.008f;

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
        int cc_step = 1;
        if (pc.is_xtrans == 0) {
            if (((r_cl + cc_start) & 1) == 0) ++cc_start;
            cc_step = 2;
        }
        for (int cc = cc_start; cc <= cb + 2; cc += cc_step) {
            if (cc < 0 || cc >= pc.W1) continue;
            if (pc.is_xtrans != 0 && cfa_at(r_cl, cc, pc.cfa0, pc.cfa1, pc.cfa2) != 1u) continue;
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
    int H1, W1, Hh, Wh, is_frame0, use_rob, is_xtrans;
    uint cfa0, cfa1, cfa2;
    float pg_sig0_sq, pg_gain, wb_r, wb_b;
    float lca_r_k1, lca_r_k2, lca_b_k1, lca_b_k2;
    float ois_dr_y, ois_dr_x, ois_db_y, ois_db_x;
    int full_H1, full_W1, origin_y, origin_x;
    float q_chroma_var, q_nlt_s0, q_nlt_s1, q_nlt_s2;
};

inline float chroma_anchor(device const float* src, int H, int W, int y, int x,
                           uint ch, uint c0, uint c1, uint c2) {
    float sum = 0.0f, weights = 0.0f;
    for (int r = max(0, y - 3); r <= min(H - 1, y + 3); ++r)
        for (int c = max(0, x - 3); c <= min(W - 1, x + 3); ++c) {
            if (cfa_at(r, c, c0, c1, c2) != ch) continue;
            float w = 1.0f / (0.25f + float((r-y)*(r-y) + (c-x)*(c-x)));
            sum += w * src[r * W + c];
            weights += w;
        }
    return sum / max(weights, 1e-12f);
}

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

        float cy = 0.5f * float(pc.full_H1), cx = 0.5f * float(pc.full_W1);
        float inv_rmax = 1.0f / max(1.0f, max(cy, cx));
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
        float yt = ((y1_ref + float(pc.origin_y)) + tot_dy) - float(pc.origin_y);
        float xt = ((x1_ref + float(pc.origin_x)) + tot_dx) - float(pc.origin_x);
        if (yt < 0.0f || yt > float(pc.H1 - 1) || xt < 0.0f || xt > float(pc.W1 - 1)) continue;

        int yh_i = clamp(int(yh + 0.5f), 0, pc.Hh - 1);
        int xh_i = clamp(int(xh + 0.5f), 0, pc.Wh - 1);
        float4 icg = icov_guide[yh_i * pc.Wh + xh_i];
        float axx = 0.60f * icg.x, axy = 0.60f * icg.y, ayy = 0.60f * icg.z;

        int r1_anc = clamp(2 * yh_i + is_b, 0, pc.H1 - 1);
        int c1_anc = clamp(2 * xh_i + is_b, 0, pc.W1 - 1);
        float c_anc = chroma_anchor(c_anc_1x, pc.H1, pc.W1, r1_anc, c1_anc,
                                   is_b != 0 ? 2u : 0u, pc.cfa0, pc.cfa1, pc.cfa2);
        float c_pos = max(0.0f, max(c_anc, icg.w));
        float cx_cl = min(1.0f, c_pos);
        float qvc = pc.q_chroma_var * max(0.0f, pc.q_nlt_s0 + cx_cl * (pc.q_nlt_s1 + cx_cl * pc.q_nlt_s2));
        float thr = 4.0f * sqrt(pc.pg_sig0_sq + pc.pg_gain * c_pos + qvc) + 0.01f;
        float wb = (is_b != 0) ? pc.wb_b : pc.wb_r;

        float d_sum = 0.0f, w_sum = 0.0f;
        if (pc.is_xtrans == 0) {
            int qyc = int(floor((yt - float(is_b)) * 0.5f));
            int qxc = int(floor((xt - float(is_b)) * 0.5f));
            for (int dq_y = 0; dq_y <= 1; ++dq_y) {
                int rr = clamp(2 * (qyc + dq_y) + is_b, is_b, pc.H1 - 2 + is_b);
                float dy = float(rr) - yt;
                for (int dq_x = 0; dq_x <= 1; ++dq_x) {
                    int cc = clamp(2 * (qxc + dq_x) + is_b, is_b, pc.W1 - 2 + is_b);
                    float dx = float(cc) - xt;
                    float d2 = axx * dx * dx + 2.0f * axy * dx * dy + ayy * dy * dy;
                    if (d2 > 7.5f) continue;

                    float c_val = frame_n[rr * pc.W1 + cc] * wb;
                    float g_at_q = bilerp_2d(g_sr2x, H2, W2,
                        2.0f * ((float(rr + pc.origin_y) - tot_dy) - float(pc.origin_y)),
                        2.0f * ((float(cc + pc.origin_x) - tot_dx) - float(pc.origin_x)));
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
        } else {
            uint want_ch = (is_b != 0) ? 2u : 0u;
            int rb = int(floor(yt)), cb = int(floor(xt));
            for (int rr = rb - 2; rr <= rb + 3; ++rr) {
                if (rr < 0 || rr >= pc.H1) continue;
                float dy = float(rr) - yt;
                for (int cc = cb - 2; cc <= cb + 3; ++cc) {
                    if (cc < 0 || cc >= pc.W1) continue;
                    if (cfa_at(rr, cc, pc.cfa0, pc.cfa1, pc.cfa2) != want_ch) continue;
                    float dx = float(cc) - xt;
                    float d2 = 0.55f * (axx * dx * dx + 2.0f * axy * dx * dy + ayy * dy * dy);
                    if (d2 > 9.5f) continue;

                    float c_val = frame_n[rr * pc.W1 + cc] * wb;
                    float g_at_q = bilerp_2d(g_sr2x, H2, W2,
                        2.0f * ((float(rr + pc.origin_y) - tot_dy) - float(pc.origin_y)),
                        2.0f * ((float(cc + pc.origin_x) - tot_dx) - float(pc.origin_x)));
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
        }
        if (is_b == 0) add_r = float2(d_sum, w_sum);
        else           add_b = float2(d_sum, w_sum);
    }

    acc_chroma[Y * pc.W1 + X] += float4(add_r, add_b);
}

struct PC_ChromaFinal {
    int H1, W1, Hh, Wh;
    float wb_r, wb_b, hl_start, hl_span;
    uint cfa0, cfa1, cfa2;
    int is_xtrans;
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
        float c_anc = chroma_anchor(c_anc_1x, pc.H1, pc.W1, r1_anc, c1_anc,
                                   is_b != 0 ? 2u : 0u, pc.cfa0, pc.cfa1, pc.cfa2);
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
    int roi_y0, roi_x0, roi_y1, roi_x1;
};

inline void dct8_fwd_1d(thread float& p0, thread float& p1, thread float& p2, thread float& p3,
                        thread float& p4, thread float& p5, thread float& p6, thread float& p7) {
    const float c1 = 0.4903926402f, c2 = 0.4619397663f, c3 = 0.4157348062f, c4 = 0.3535533906f;
    const float c5 = 0.2777851165f, c6 = 0.1913417162f, c7 = 0.0975451610f;
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

inline void dct8_inv_1d(thread float& p0, thread float& p1, thread float& p2, thread float& p3,
                        thread float& p4, thread float& p5, thread float& p6, thread float& p7) {
    const float c1 = 0.4903926402f, c2 = 0.4619397663f, c3 = 0.4157348062f, c4 = 0.3535533906f;
    const float c5 = 0.2777851165f, c6 = 0.1913417162f, c7 = 0.0975451610f;
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

kernel void k_dct_wiener(
    device const float* src_in [[buffer(0)]],
    device const float* guide_in [[buffer(1)]],
    device const float* snr_boost [[buffer(2)]],
    device float* dst_out [[buffer(3)]],
    constant PC_DctWiener& pc [[buffer(6)]],
    uint tid_u [[thread_index_in_threadgroup]],
    uint2 wgid [[threadgroup_position_in_grid]]
) {
    // Cooperative 14-block x 8-lane zero-bank-conflict tile covering 20x10 output pixels (two 10x10 ROI subtiles).
    // Total threadgroup memory = 10,868 B (fits 3 workgroups = 384 threads per M1 GPU core).
    threadgroup float s_src[24][35];
    threadgroup float s_gde[24][35];
    threadgroup float s_work[14][8][9];
    threadgroup float s_bw[14];
    threadgroup int   s_rx[14];

    int tid = int(tid_u);
    int b_slot = tid >> 3;
    int lane = tid & 7;
    int out_x0 = int(wgid.x) * 20;
    int out_y0 = int(wgid.y) * 10;

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
    threadgroup_barrier(mem_flags::mem_threadgroup);

    bool active_x = (b_slot < 14) && (bc >= 0 && bc <= pc.W - 8);
    int sx0 = bc - (out_x0 - 7);
    int ch = active_x ? clamp(((bc + 4) * pc.scale_mul) >> 2, 0, pc.Wh - 1) : 0;

    float acc0 = 0.0f, wgt0 = 0.0f, acc1 = 0.0f, wgt1 = 0.0f;
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
        float sig = 0.0f;
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
            threadgroup_barrier(mem_flags::mem_threadgroup);
            float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f, q4 = 0.0f, q5 = 0.0f, q6 = 0.0f, q7 = 0.0f;
            int nz = 0;
            if (active_x) {
                q0 = s_work[b_slot][0][lane]; q1 = s_work[b_slot][1][lane];
                q2 = s_work[b_slot][2][lane]; q3 = s_work[b_slot][3][lane];
                q4 = s_work[b_slot][4][lane]; q5 = s_work[b_slot][5][lane];
                q6 = s_work[b_slot][6][lane]; q7 = s_work[b_slot][7][lane];
                dct8_fwd_1d(q0, q1, q2, q3, q4, q5, q6, q7);
                float thr = 3.10f * sig;
                if (lane == 0) nz = 1;
                else if (fabs(q0) < thr) q0 = 0.0f; else ++nz;
                if (fabs(q1) < thr) q1 = 0.0f; else ++nz;
                if (fabs(q2) < thr) q2 = 0.0f; else ++nz;
                if (fabs(q3) < thr) q3 = 0.0f; else ++nz;
                if (fabs(q4) < thr) q4 = 0.0f; else ++nz;
                if (fabs(q5) < thr) q5 = 0.0f; else ++nz;
                if (fabs(q6) < thr) q6 = 0.0f; else ++nz;
                if (fabs(q7) < thr) q7 = 0.0f; else ++nz;
                dct8_inv_1d(q0, q1, q2, q3, q4, q5, q6, q7);
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
            if (active_x) {
                s_work[b_slot][0][lane] = q0; s_work[b_slot][1][lane] = q1;
                s_work[b_slot][2][lane] = q2; s_work[b_slot][3][lane] = q3;
                s_work[b_slot][4][lane] = q4; s_work[b_slot][5][lane] = q5;
                s_work[b_slot][6][lane] = q6; s_work[b_slot][7][lane] = q7;
                s_work[b_slot][lane][8] = float(nz);
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
            if (active_x) {
                float tot_nz = s_work[b_slot][0][8] + s_work[b_slot][1][8] + s_work[b_slot][2][8] + s_work[b_slot][3][8]
                             + s_work[b_slot][4][8] + s_work[b_slot][5][8] + s_work[b_slot][6][8] + s_work[b_slot][7][8];
                float bw = 1.0f / tot_nz;
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
                s_bw[b_slot] = 0.0f;
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
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
            threadgroup_barrier(mem_flags::mem_threadgroup);
            float gw0 = 1.0f, gw1 = 0.0f, gw2 = 0.0f, gw3 = 0.0f, gw4 = 0.0f, gw5 = 0.0f, gw6 = 0.0f, gw7 = 0.0f;
            if (active_x) {
                float g0 = s_work[b_slot][0][lane], g1 = s_work[b_slot][1][lane];
                float g2 = s_work[b_slot][2][lane], g3 = s_work[b_slot][3][lane];
                float g4 = s_work[b_slot][4][lane], g5 = s_work[b_slot][5][lane];
                float g6 = s_work[b_slot][6][lane], g7 = s_work[b_slot][7][lane];
                dct8_fwd_1d(g0, g1, g2, g3, g4, g5, g6, g7);
                float sig2 = 1.35f * sig * sig;
                gw0 = (lane == 0) ? 1.0f : ((g0 * g0) / (g0 * g0 + sig2));
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
            threadgroup_barrier(mem_flags::mem_threadgroup);
            float bw = 0.0f;
            if (active_x) {
                float w2_sum = s_work[b_slot][0][8] + s_work[b_slot][1][8] + s_work[b_slot][2][8] + s_work[b_slot][3][8]
                             + s_work[b_slot][4][8] + s_work[b_slot][5][8] + s_work[b_slot][6][8] + s_work[b_slot][7][8];
                bw = 1.0f / w2_sum;
                int r = sy0 + lane;
                float p0 = s_src[r][sx0 + 0], p1 = s_src[r][sx0 + 1], p2 = s_src[r][sx0 + 2], p3 = s_src[r][sx0 + 3];
                float p4 = s_src[r][sx0 + 4], p5 = s_src[r][sx0 + 5], p6 = s_src[r][sx0 + 6], p7 = s_src[r][sx0 + 7];
                dct8_fwd_1d(p0, p1, p2, p3, p4, p5, p6, p7);
                s_work[b_slot][lane][0] = p0; s_work[b_slot][lane][1] = p1;
                s_work[b_slot][lane][2] = p2; s_work[b_slot][lane][3] = p3;
                s_work[b_slot][lane][4] = p4; s_work[b_slot][lane][5] = p5;
                s_work[b_slot][lane][6] = p6; s_work[b_slot][lane][7] = p7;
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
            float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f, q3 = 0.0f, q4 = 0.0f, q5 = 0.0f, q6 = 0.0f, q7 = 0.0f;
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
            threadgroup_barrier(mem_flags::mem_threadgroup);
            if (active_x) {
                s_work[b_slot][0][lane] = q0; s_work[b_slot][1][lane] = q1;
                s_work[b_slot][2][lane] = q2; s_work[b_slot][3][lane] = q3;
                s_work[b_slot][4][lane] = q4; s_work[b_slot][5][lane] = q5;
                s_work[b_slot][6][lane] = q6; s_work[b_slot][7][lane] = q7;
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
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
                s_bw[b_slot] = 0.0f;
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }

        int py0 = oy0 - ry;
        if (uint(py0) < 8u) {
            int bx0 = max(0, ox0 >> 1), bx1 = min(13, (ox0 + 7) >> 1);
            for (int bx = bx0; bx <= bx1; ++bx) {
                float bw = s_bw[bx];
                int px = ox0 - s_rx[bx];
                if (bw > 0.0f && uint(px) < 8u) {
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
                    if (bw > 0.0f && uint(px) < 8u) {
                        acc1 += s_work[bx][py1][px];
                        wgt1 += bw;
                    }
                }
            }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    int gy0 = out_y0 + oy0, gx0 = out_x0 + ox0;
    if (gy0 < pc.H && gx0 < pc.W) {
        bool sk = (ox0 < 10) ? skip0 : skip1;
        dst_out[gy0 * pc.W + gx0] = (!sk && wgt0 > 1e-12f) ? (acc0 / wgt0) : s_src[oy0 + 7][ox0 + 7];
    }
    if (k1 < 200) {
        int gy1 = out_y0 + oy1, gx1 = out_x0 + ox1;
        if (gy1 < pc.H && gx1 < pc.W) {
            bool sk = (ox1 < 10) ? skip0 : skip1;
            dst_out[gy1 * pc.W + gx1] = (!sk && wgt1 > 1e-12f) ? (acc1 / wgt1) : s_src[oy1 + 7][ox1 + 7];
        }
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
    float ys = (float(r) + 0.5f) * 0.5f - 0.5f;
    float xs = (float(c) + 0.5f) * 0.5f - 0.5f;
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
    device const float* z_gat [[buffer(4)]],
    constant PC_GatInvQuad& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;
    int H2 = pc.H1 * 2, W2 = pc.W1 * 2;
    int rh = clamp(Y >> 1, 0, pc.Hh - 1);
    int ch = clamp(X >> 1, 0, pc.Wh - 1);
    float dv = dv_map[rh * pc.Wh + ch];

    // Load 4x4 z_gat patch for 3x3 Discrete Maximum Principle Gibbs-ringing guard
    float zg[4][4];
    for (int dr = 0; dr < 4; ++dr) {
        int rr = clamp(2 * Y - 1 + dr, 0, H2 - 1);
        for (int dc = 0; dc < 4; ++dc) {
            int cc = clamp(2 * X - 1 + dc, 0, W2 - 1);
            zg[dr][dc] = z_gat[rr * W2 + cc];
        }
    }
    auto clamp_3x3 = [&](float z, int r0, int c0) -> float {
        float lo = zg[r0][c0], hi = lo;
        for (int dy = 0; dy < 3; ++dy) {
            for (int dx = 0; dx < 3; ++dx) {
                float v = zg[r0 + dy][c0 + dx];
                lo = min(lo, v);
                hi = max(hi, v);
            }
        }
        return clamp(z, lo, hi);
    };

    int i00 = (2 * Y) * W2 + (2 * X);
    int i01 = i00 + 1;
    int i10 = i00 + W2;
    int i11 = i10 + 1;

    float z00 = clamp_3x3(z_den[i00], 0, 0);
    float z01 = clamp_3x3(z_den[i01], 0, 1);
    float z10 = clamp_3x3(z_den[i10], 1, 0);
    float z11 = clamp_3x3(z_den[i11], 1, 1);
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
    float spike_g = max(0.0f, g - g_bg);
    float glint_exc = (spike_g > 0.015f) ? (spike_g + 0.35f * max(0.0f, max(dr, db))) : 0.0f;
    float hl_lvl = max(g, max(g + dr, g + db));
    float desat = max(clamp((glint_exc - 0.045f) / 0.12f, 0.0f, 0.92f),
                      clamp((hl_lvl - 0.94f) / 0.10f, 0.0f, 0.85f));
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
    float lo_b = (pc.is_astro != 0) ? max(1e-6f, max(vmin, l0 - 0.02f * max(0.0f, l0 - vmin))) : max(1e-6f, vmin - 0.02f * span);
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
    K_SENSOR_RESIDUAL,
    K_COUNT
};

static const char* kKernelNames[K_COUNT] = {
    "k_zero", "k_green_accum", "k_green_final", "k_chroma_accum", "k_chroma_final",
    "k_gat_fwd", "k_box_down2x", "k_dct_wiener", "k_pyr_sub_down2x", "k_pyr_lap_add",
    "k_edge_polish", "k_dv_map", "k_gauss_1d", "k_gat_inv_quad", "k_median3x3",
    "k_glint_desat", "k_atrous_1d", "k_assemble_rgb", "k_rl_lum", "k_rl_prep_h",
    "k_rl_prep_v", "k_rl_ratio", "k_rl_update", "k_rl_apply", "k_sensor_residual"
};

struct MetalContext {
    static constexpr int kArenaSlots = 28;
    std::mutex mtx;
    std::mutex run_mtx;
    bool inited = false;
    bool ok = false;
    std::string dev_name;
    std::string init_err;
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLComputePipelineState> pipes[K_COUNT] = {};
    id<MTLBuffer> stage45_arena[kArenaSlots] = {};

    id<MTLBuffer> acquire_buf(int slot, size_t bytes) {
        const size_t req = std::max<size_t>(16, bytes);
        id<MTLBuffer> cur = stage45_arena[slot];
        if (cur && cur.length >= req) return cur;
        stage45_arena[slot] = nil;
        id<MTLBuffer> nb = [dev newBufferWithLength:req options:MTLResourceStorageModeShared];
        stage45_arena[slot] = nb;
        return nb;
    }

    void clear_arena() {
        for (int i = 0; i < kArenaSlots; ++i) stage45_arena[i] = nil;
    }

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
            // Preserve global-coordinate rounding across rebased tile origins.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            opts.fastMathEnabled = NO;
#pragma clang diagnostic pop
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

static bool run_stage45_gpu_full(Stage45GpuJob& job) {
    @autoreleasepool {
        auto cost_start=std::chrono::steady_clock::now();
        job.out_wall_seconds.fill(0); job.out_device_seconds.fill(0);
        job.out_has_device_timing=true;
        auto cost_mark=[&](size_t phase) {
            const auto now=std::chrono::steady_clock::now();
            job.out_wall_seconds[phase]+=std::chrono::duration<double>(now-cost_start).count();
            cost_start=now;
        };
        auto device_cost=[&](id<MTLCommandBuffer> cb,size_t phase) {
            job.out_device_seconds[phase]+=std::max(0.0,cb.GPUEndTime-cb.GPUStartTime);
        };
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

        const size_t peak_1x   = std::max(n_1x, job.arena_peak_1x_pixels);
        const size_t peak_half = std::max(n_half, peak_1x / 4);
        const size_t peak_2x   = std::max(n_2x, peak_1x * 4);
        const size_t peak_eta  = std::max(n_eta, (peak_1x + 15) / 16 + 1024);

        auto t_s4 = std::chrono::steady_clock::now();
        job.out_device_label = "Metal (" + ctx.dev_name + ")";

        // Core buffers mapped onto non-overlapping arena slots (pre-sized to peak tile)
        id<MTLBuffer> b_frame[2]   = {ctx.acquire_buf(8,  peak_1x * sizeof(float)), ctx.acquire_buf(9,  peak_1x * sizeof(float))};
        id<MTLBuffer> b_flow_mw[2] = {ctx.acquire_buf(14, peak_half * sizeof(float) * 4), ctx.acquire_buf(15, peak_half * sizeof(float) * 4)};
        id<MTLBuffer> b_icov    = ctx.acquire_buf(16, peak_half * sizeof(float) * 4);
        id<MTLBuffer> b_canc    = ctx.acquire_buf(10, peak_1x * sizeof(float));
        id<MTLBuffer> b_g0ha    = ctx.acquire_buf(11, peak_1x * sizeof(float));
        id<MTLBuffer> b_boost   = ctx.acquire_buf(17, peak_half * sizeof(float));
        id<MTLBuffer> b_rlmod   = ctx.acquire_buf(18, peak_half * sizeof(uint32_t));

        id<MTLBuffer> b_plane0  = ctx.acquire_buf(0,  peak_2x * sizeof(float)); // g_sr2x / outR
        id<MTLBuffer> b_plane1  = ctx.acquire_buf(1,  peak_2x * sizeof(float)); // acc_gv / z_gat / outG
        id<MTLBuffer> b_plane2  = ctx.acquire_buf(2,  peak_2x * sizeof(float)); // acc_gw / pyr0  / outB
        id<MTLBuffer> b_eta     = ctx.acquire_buf(19, peak_eta * sizeof(float));
        id<MTLBuffer> b_dR      = ctx.acquire_buf(12, peak_1x * sizeof(float));
        id<MTLBuffer> b_dB      = ctx.acquire_buf(13, peak_1x * sizeof(float));

        if (!b_frame[0] || !b_frame[1] || !b_flow_mw[0] || !b_flow_mw[1] ||
            !b_icov || !b_canc || !b_g0ha || !b_boost || !b_rlmod ||
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
        cost_mark(0);
        std::memset([b_plane1 contents], 0, n_2x * sizeof(float));
        std::memset([b_plane2 contents], 0, n_2x * sizeof(float));
        std::memset([b_eta contents], 0, n_eta * sizeof(float));

        uint32_t cfa_w[3] = {0, 0, 0};
        pack_cfa6x6_bits(job.cfa6x6, cfa_w);

        auto pack_frame_slot = [&](int slot, int n, float mw_scale) {
            const float* src_f = (*job.frames_lin)[n].data();
            float* dst_f = (float*)[b_frame[slot] contents];
            float* fmw = (float*)[b_flow_mw[slot] contents];
            const float* fy = (*job.flows_fy)[n].data();
            const float* fx = (*job.flows_fx)[n].data();
            const float* mw = (*job.mw_half)[n].data();
            const int p_nt = (n_half >= 65536 && job.n_threads > 1) ? std::min(job.n_threads, 8) : 1;
            if (p_nt <= 1) {
                std::memcpy(dst_f, src_f, n_1x * sizeof(float));
                for (size_t i = 0; i < n_half; ++i) {
                    fmw[i * 4 + 0] = fy[i];
                    fmw[i * 4 + 1] = fx[i];
                    fmw[i * 4 + 2] = gpu_motion_weight(job, mw[i]) * mw_scale;
                    fmw[i * 4 + 3] = 0.0f;
                }
            } else {
                std::vector<std::thread> workers;
                workers.reserve(p_nt);
                const size_t ch_1x = (n_1x + p_nt - 1) / p_nt;
                const size_t ch_h  = (n_half + p_nt - 1) / p_nt;
                for (int t = 0; t < p_nt; ++t) {
                    workers.emplace_back([=, &job]() {
                        const size_t a0 = size_t(t) * ch_1x, a1 = std::min(n_1x, a0 + ch_1x);
                        if (a0 < a1) std::memcpy(dst_f + a0, src_f + a0, (a1 - a0) * sizeof(float));
                        const size_t h0 = size_t(t) * ch_h, h1 = std::min(n_half, h0 + ch_h);
                        for (size_t i = h0; i < h1; ++i) {
                            fmw[i * 4 + 0] = fy[i];
                            fmw[i * 4 + 1] = fx[i];
                            fmw[i * 4 + 2] = gpu_motion_weight(job, mw[i]) * mw_scale;
                            fmw[i * 4 + 3] = 0.0f;
                        }
                    });
                }
                for (auto& th : workers) th.join();
            }
        };

        id<MTLEvent> accum_ev = [ctx.dev newEvent];
        uint64_t ev_val = 0;
        id<MTLCommandBuffer> prev_cb[2] = {nil, nil};
        auto wait_slot = [&](int slot, int stage_idx, const char* label) -> bool {
            if (!prev_cb[slot]) return true;
            [prev_cb[slot] waitUntilCompleted];
            if (prev_cb[slot].status == MTLCommandBufferStatusError) {
                job.out_err = std::string(label) + ": " +
                    (prev_cb[slot].error ? prev_cb[slot].error.localizedDescription.UTF8String : "error");
                return false;
            }
            device_cost(prev_cb[slot], stage_idx);
            prev_cb[slot] = nil;
            return true;
        };

        for (int n = 0; n < job.N; ++n) {
            const int s = n & 1;
            if (!wait_slot(s, 1, "Metal green dispatch failed")) return false;
            pack_frame_slot(s, n, 1.0f);

            float fw_s = gpu_frame_weight(job, n);
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            if (ev_val > 0 && accum_ev) [cb encodeWaitForEvent:accum_ev value:ev_val];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            PC_GreenAccum pc{H1, W1, Hh, Wh, Ew, (n == 0 ? 1 : 0), gpu_use_robust(job, n) ? 1 : 0,
                             job.is_xtrans ? 1 : 0, cfa_w[0], cfa_w[1], cfa_w[2],
                             (float)job.pg_sig0_sq, (float)job.pg_gain, fw_s, job.origin_y, job.origin_x,
                             job.q_luma_var, job.q_nlt_s0, job.q_nlt_s1, job.q_nlt_s2};
            enc_2d(enc, K_GREEN_ACCUM, {b_frame[s], b_flow_mw[s], b_icov, b_plane1, b_plane2, b_eta}, &pc, sizeof(pc), W1, H1);
            if (n == job.N - 1) {
                PC_GreenFinal pcf{H1, W1, Hh, Wh, Ew};
                enc_2d(enc, K_GREEN_FINAL, {b_plane1, b_plane2, b_eta, b_icov, b_g0ha, b_plane0}, &pcf, sizeof(pcf), W1, H1);
            }
            [enc endEncoding];
            if (accum_ev) [cb encodeSignalEvent:accum_ev value:++ev_val];
            [cb commit];
            prev_cb[s] = cb;
        }
        if (!wait_slot(0, 1, "Metal green dispatch failed") ||
            !wait_slot(1, 1, "Metal green dispatch failed")) return false;

        // Read back eta_eff from subsampled b_eta when global_eta was not precomputed
        if (job.global_eta < 0.0 || job.eta_quarter_out)
            gpu_eta_stats(job, (const float*)[b_eta contents], Ew, Eh);
        const double eta_eff = job.global_eta >= 0.0 ? job.global_eta :
            std::max(0.01, job.out_eta_sum / double(std::max<int64_t>(1, job.out_eta_count)));
        job.out_eta_eff = eta_eff;
        cost_mark(1);
        if (job.eta_only) return true;
        b_eta = nil;
        b_g0ha = nil;

        // --- Stage 4B: Chroma (dR_1x, dB_1x) Accumulation across N frames (reusing b_plane1 as vec4[n_1x]!) ---
        std::memset([b_plane1 contents], 0, n_1x * sizeof(float) * 4);
        for (int n = 0; n < job.N; ++n) {
            const int s = n & 1;
            if (!wait_slot(s, 2, "Metal chroma dispatch failed")) return false;
            pack_frame_slot(s, n, gpu_frame_weight(job, n));

            float odry = 0.0f, odrx = 0.0f, odby = 0.0f, odbx = 0.0f;
            if (job.frame_ois_shifts && (size_t)(n * 4 + 3) < job.frame_ois_shifts->size()) {
                odry = (*job.frame_ois_shifts)[n * 4 + 0];
                odrx = (*job.frame_ois_shifts)[n * 4 + 1];
                odby = (*job.frame_ois_shifts)[n * 4 + 2];
                odbx = (*job.frame_ois_shifts)[n * 4 + 3];
            }

            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            if (ev_val > 0 && accum_ev) [cb encodeWaitForEvent:accum_ev value:ev_val];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            PC_ChromaAccum pc{H1, W1, Hh, Wh, (n == 0 ? 1 : 0), gpu_use_robust(job, n) ? 1 : 0,
                              job.is_xtrans ? 1 : 0, cfa_w[0], cfa_w[1], cfa_w[2],
                              (float)job.pg_sig0_sq, (float)job.pg_gain, job.wb_r, job.wb_b,
                              job.lca_r_k1, job.lca_r_k2, job.lca_b_k1, job.lca_b_k2,
                              odry, odrx, odby, odbx, job.full_H1, job.full_W1, job.origin_y, job.origin_x,
                              job.q_chroma_var, job.q_nlt_s0, job.q_nlt_s1, job.q_nlt_s2};
            enc_2d(enc, K_CHROMA_ACCUM, {b_frame[s], b_flow_mw[s], b_icov, b_plane0, b_canc, b_plane1}, &pc, sizeof(pc), W1, H1);
            if (n == job.N - 1) {
                const float hl_start = job.glint_desat ? (job.is_astro ? 0.88f : 0.85f) : 1.0e6f;
                PC_ChromaFinal pcf{H1, W1, Hh, Wh, job.wb_r, job.wb_b,
                                   hl_start, 0.14f,
                                   cfa_w[0], cfa_w[1], cfa_w[2], job.is_xtrans ? 1 : 0};
                enc_2d(enc, K_CHROMA_FINAL, {b_plane1, b_plane0, b_canc, b_dR, b_dB}, &pcf, sizeof(pcf), W1, H1);
            }
            [enc endEncoding];
            if (accum_ev) [cb encodeSignalEvent:accum_ev value:++ev_val];
            [cb commit];
            prev_cb[s] = cb;
        }
        if (!wait_slot(0, 2, "Metal chroma dispatch failed") ||
            !wait_slot(1, 2, "Metal chroma dispatch failed")) return false;
        b_frame[0] = nil; b_frame[1] = nil;
        cost_mark(2);
        b_flow_mw[0] = nil; b_flow_mw[1] = nil;
        b_icov = nil;
        b_canc = nil;

        double dt_s4 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_s4).count();
        if (job.on_stage4_done) job.on_stage4_done(eta_eff, dt_s4, job.out_device_label);

        // --- Stage 5A: 5-Level Multiscale 8x8 DCT Wiener on GPU ---
        double sig_eff_sq = 0.0, g_eff = 0.0;
        gpu_stage5_gat_params(job, eta_eff, sig_eff_sq, g_eff);
        const double ped = 4.0 * std::sqrt(sig_eff_sq);
        const double gat_c = 0.375 * g_eff * g_eff + sig_eff_sq;
        const double sqrt_c = std::sqrt(gat_c);

        id<MTLBuffer> b_den0  = ctx.acquire_buf(3, peak_2x * sizeof(float));
        id<MTLBuffer> b_pilot = ctx.acquire_buf(4, peak_2x * sizeof(float));
        int Hs[5], Ws[5];
        Hs[0] = H2; Ws[0] = W2;
        id<MTLBuffer> b_pyr[5] = {};
        id<MTLBuffer> b_den[5] = {};
        b_pyr[0] = b_plane2;
        b_den[0] = b_den0;
        for (int s = 1; s < 5; ++s) {
            Hs[s] = Hs[s - 1] / 2;
            Ws[s] = Ws[s - 1] / 2;
            const size_t peak_s = std::max((size_t)Hs[s] * Ws[s], peak_2x >> (2 * s));
            b_pyr[s] = ctx.acquire_buf(19 + s, peak_s * sizeof(float));
            b_den[s] = ctx.acquire_buf(23 + s, peak_s * sizeof(float));
            if (!b_pyr[s] || !b_den[s]) { job.out_err = "Metal pyramid allocation failed"; return false; }
        }
        id<MTLBuffer> b_dv1 = ctx.acquire_buf(14, peak_half * sizeof(float));
        id<MTLBuffer> b_dv2 = ctx.acquire_buf(15, peak_half * sizeof(float));
        id<MTLBuffer> b_gquad = ctx.acquire_buf(8, peak_1x * sizeof(float));
        if (!b_den0 || !b_pilot || !b_dv1 || !b_dv2 || !b_gquad) {
            job.out_err = "Metal denoise allocation failed"; return false;
        }

        {
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
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
                    float sig_s = base_sig[s] * job.denoise_strength * job.dct_scale_mul[s];
                    int tiles_x = (Ws[s] + 19) / 20;
                    int tiles_y = (Hs[s] + 9) / 10;

                    // Pass 1: Hard-threshold Pilot -> b_pilot
                    PC_DctWiener pcp{Hs[s], Ws[s], Hh, Wh, 0, (1 << s), sig_s};
                    dct_dependency_roi(pcp,job);
                    [enc setComputePipelineState:ctx.pipes[K_DCT_WIENER]];
                    [enc setBuffer:b_pyr[s] offset:0 atIndex:0];
                    [enc setBuffer:b_pyr[s] offset:0 atIndex:1];
                    [enc setBuffer:b_boost  offset:0 atIndex:2];
                    [enc setBuffer:b_pilot  offset:0 atIndex:3];
                    [enc setBytes:&pcp length:sizeof(pcp) atIndex:6];
                    [enc dispatchThreadgroups:MTLSizeMake(tiles_x, tiles_y, 1) threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];

                    // Pass 2: Empirical Wiener -> b_den[s]
                    PC_DctWiener pcw{Hs[s], Ws[s], Hh, Wh, 1, (1 << s), sig_s};
                    dct_dependency_roi(pcw,job);
                    [enc setBuffer:b_pyr[s] offset:0 atIndex:0];
                    [enc setBuffer:b_pilot  offset:0 atIndex:1];
                    [enc setBuffer:b_boost  offset:0 atIndex:2];
                    [enc setBuffer:b_den[s] offset:0 atIndex:3];
                    [enc setBytes:&pcw length:sizeof(pcw) atIndex:6];
                    [enc dispatchThreadgroups:MTLSizeMake(tiles_x, tiles_y, 1) threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];

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
            enc_2d(enc, K_GAT_INV_QUAD, {b_den[0], b_dv1, b_plane0, b_gquad, b_plane1}, &pcgi, sizeof(pcgi), W1, H1);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) { job.out_err = "Metal denoise dispatch failed"; return false; }
            device_cost(cb,3);
        }
        cost_mark(3);

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

        if (job.global_mean_g >= 0.0) mean_g_pos = job.global_mean_g;

        // --- Stage 5B: Chroma Median + Glint Desat + 6-Level Guided A-Trous + RGB Assembly (+ RL) ---
        id<MTLBuffer> b_tmpR = ctx.acquire_buf(9,  peak_1x * sizeof(float));
        id<MTLBuffer> b_tmpB = ctx.acquire_buf(10, peak_1x * sizeof(float));
        id<MTLBuffer> b_outR = ctx.acquire_buf(3,  peak_2x * sizeof(float)); // planar R (keep b_plane0 safe during assemble!)
        if (!b_tmpR || !b_tmpB || !b_outR) {
            job.out_err = "Metal chroma/RGB buffer allocation failed";
            return false;
        }

        const float resid_noise_2x = 0.35f * (float)std::sqrt(sig_eff_sq + g_eff * mean_g_pos);
        job.out_resid_noise_2x = resid_noise_2x;

        {
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
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

                float chroma_sig = gpu_stage5_chroma_sigma(job, eta_eff, mean_g_pos);
                if (chroma_sig > 1e-6f) {
                    float sig_g = std::max(1e-4f, 0.5f * chroma_sig);
                    float g_edge_scale = std::max(job.is_astro ? 0.14f : (3.0f * sig_g),
                        2.2f * (job.global_edge_p80 >= 0.0f ? job.global_edge_p80 : quick_pct_gpu_helper(g_sub, 0.80f)) + 0.006f);
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
            if (cb.status == MTLCommandBufferStatusError) { job.out_err = "Metal assembly dispatch failed"; return false; }
            device_cost(cb,4);
        }
        cost_mark(4);

        b_dR = nil; b_dB = nil; b_tmpR = nil; b_tmpB = nil; b_gquad = nil; b_boost = nil;

        // Optional PSF Richardson-Lucy Deconvolution (reusing b_plane0 as scratch!)
        if (job.psf_iters > 0) {
            id<MTLBuffer> b_est   = ctx.acquire_buf(4, peak_2x * sizeof(float));
            id<MTLBuffer> b_conv  = ctx.acquire_buf(5, peak_2x * sizeof(float));
            id<MTLBuffer> b_gate  = ctx.acquire_buf(6, peak_2x * sizeof(float));
            id<MTLBuffer> b_bnds  = ctx.acquire_buf(7, peak_2x * sizeof(uint32_t));
            if (b_est && b_conv && b_gate && b_bnds) {
                id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
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

                PC_Gauss1D gpsfh = make_olpf_psf_pc(H2, W2, 0, job.psf_sigma, job.olpf_mode);
                PC_Gauss1D gpsfv = make_olpf_psf_pc(H2, W2, 1, job.psf_sigma, job.olpf_mode);
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
                if (cb.status == MTLCommandBufferStatusError) { job.out_err = "Metal RL dispatch failed"; return false; }
                device_cost(cb,5);
            } else {
                job.out_err = "Metal RL allocation failed"; return false;
            }
        }
        b_plane0 = nil;
        cost_mark(5);
        b_rlmod = nil;

        // Interleave planar R, G, B from unified memory into output (direct core-only stitch when tiled)
        const float* pR = (const float*)[b_outR contents];
        const float* pG = (const float*)[b_plane1 contents];
        const float* pB = (const float*)[b_plane2 contents];
        const int sr0_base = job.direct_stitch_rgb2x ? (job.core_y0 * 2) : 0;
        const int sc0_base = job.direct_stitch_rgb2x ? (job.core_x0 * 2) : 0;
        const int s_rows   = job.direct_stitch_rgb2x ? ((job.core_y1 - job.core_y0) * 2) : H2;
        const int s_cols   = job.direct_stitch_rgb2x ? ((job.core_x1 - job.core_x0) * 2) : W2;
        float* out_base    = job.direct_stitch_rgb2x ? job.direct_stitch_rgb2x : job.rgb_2x_out;
        const int out_str  = job.direct_stitch_rgb2x ? job.direct_w2_stride : W2;
        const int out_y0   = job.direct_stitch_rgb2x ? job.direct_y2 : 0;
        const int out_x0   = job.direct_stitch_rgb2x ? job.direct_x2 : 0;
        const int nt = std::max(1, job.n_threads);
        if (s_rows > 0 && s_cols > 0 && out_base) {
            std::vector<std::thread> workers;
            int rows_per = (s_rows + nt - 1) / nt;
            for (int t = 0; t < nt; ++t) {
                int r0 = t * rows_per;
                int r1 = std::min(s_rows, r0 + rows_per);
                if (r0 >= r1) break;
                workers.emplace_back([=]() {
                    for (int r = r0; r < r1; ++r) {
                        size_t i0 = (size_t)(sr0_base + r) * W2 + sc0_base;
                        float* dst = out_base + ((size_t)(out_y0 + r) * out_str + out_x0) * 3;
                        for (int c = 0; c < s_cols; ++c) {
                            dst[c * 3 + 0] = pR[i0 + c];
                            dst[c * 3 + 1] = pG[i0 + c];
                            dst[c * 3 + 2] = pB[i0 + c];
                        }
                    }
                });
            }
            for (auto& th : workers) th.join();
        }
        cost_mark(6);
        return true;
    }
}

bool run_stage45_gpu(Stage45GpuJob& job) {
    @autoreleasepool {
        auto& ctx=global_metal();
        if(!ctx.ensure_init()) {job.out_err=ctx.init_err;return false;}
        std::lock_guard<std::mutex> run_lk(ctx.run_mtx);
        struct ArenaGuard {
            MetalContext& c;
            ~ArenaGuard() { @autoreleasepool { c.clear_arena(); } }
        } guard{ctx};
        const size_t device_budget=size_t(ctx.dev.recommendedMaxWorkingSetSize)/4;
        job.max_tile_pixels=std::min({job.max_tile_pixels,size_t(ctx.dev.maxBufferLength)/16,device_budget/128});
        job.can_direct_stitch = true;
        return run_stage45_streamed(job, run_stage45_gpu_full, true);
    }
}

bool run_sensor_residual_gpu(const SensorResidualJob& j) {
    @autoreleasepool {
        auto& ctx=global_metal();
        if(!ctx.ensure_init()) return false;
        std::lock_guard<std::mutex> run_lk(ctx.run_mtx);
        struct ArenaGuard {
            MetalContext& c;
            ~ArenaGuard() { @autoreleasepool { c.clear_arena(); } }
        } guard{ctx};
        const size_t pixels=size_t(j.H)*j.W,bytes=pixels*sizeof(float),half=pixels/4;
        const size_t scene_bytes = half * 3 * sizeof(float);
        if(bytes>ctx.dev.maxBufferLength || bytes*4>ctx.dev.recommendedMaxWorkingSetSize/4) return false;
        id<MTLBuffer> raw=ctx.acquire_buf(0, bytes);
        id<MTLBuffer> flow=ctx.acquire_buf(1, bytes);
        id<MTLBuffer> scene=ctx.acquire_buf(2, scene_bytes);
        id<MTLBuffer> residual=ctx.acquire_buf(3, bytes);
        if(!raw||!flow||!scene||!residual) return false;
        std::memcpy(scene.contents, j.scene, scene_bytes);
        struct Params {int32_t H,W,Hh,Wh;float read_variance,shot;uint32_t cfa[3];};
        Params pc{j.H,j.W,j.H/2,j.W/2,j.read_variance,j.shot,{0,0,0}};
        for(int i=0;i<36;++i) pc.cfa[i/16]|=uint32_t(j.cfa[i])<<((i%16)*2);
        const int p_nt = (half >= 65536) ? std::max(1, std::min<int>(8, std::thread::hardware_concurrency())) : 1;
        for(size_t n=0;n<j.raw->size();++n) {
            float* dst_r = (float*)raw.contents;
            const float* src_r = (*j.raw)[n].data();
            float* packed = (float*)flow.contents;
            const float* pfy = (*j.fy)[n].data();
            const float* pfx = (*j.fx)[n].data();
            const float* pmk = (*j.mask)[n].data();
            if (p_nt <= 1) {
                std::memcpy(dst_r, src_r, bytes);
                for(size_t i=0;i<half;++i) {
                    packed[i*4]=pfy[i];packed[i*4+1]=pfx[i];
                    packed[i*4+2]=pmk[i];packed[i*4+3]=0;
                }
            } else {
                std::vector<std::thread> workers;
                workers.reserve(p_nt);
                const size_t ch_p = (pixels + p_nt - 1) / p_nt;
                const size_t ch_h = (half + p_nt - 1) / p_nt;
                for (int t = 0; t < p_nt; ++t) {
                    workers.emplace_back([=]() {
                        const size_t a0 = size_t(t) * ch_p, a1 = std::min(pixels, a0 + ch_p);
                        if (a0 < a1) std::memcpy(dst_r + a0, src_r + a0, (a1 - a0) * sizeof(float));
                        const size_t h0 = size_t(t) * ch_h, h1 = std::min(half, h0 + ch_h);
                        for (size_t i = h0; i < h1; ++i) {
                            packed[i*4]=pfy[i];packed[i*4+1]=pfx[i];
                            packed[i*4+2]=pmk[i];packed[i*4+3]=0;
                        }
                    });
                }
                for (auto& th : workers) th.join();
            }
            id<MTLCommandBuffer> cb=[ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc=[cb computeCommandEncoder];
            [enc setComputePipelineState:ctx.pipes[K_SENSOR_RESIDUAL]];
            [enc setBuffer:raw offset:0 atIndex:0];[enc setBuffer:flow offset:0 atIndex:1];
            [enc setBuffer:scene offset:0 atIndex:2];[enc setBuffer:residual offset:0 atIndex:3];
            [enc setBytes:&pc length:sizeof(pc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((j.W+15)/16,(j.H+15)/16,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
            [enc endEncoding];[cb commit];[cb waitUntilCompleted];
            if(cb.status==MTLCommandBufferStatusError) return false;
            float* dst_res = (*j.residual)[n].data();
            const float* src_res = (const float*)residual.contents;
            if (p_nt <= 1) {
                std::memcpy(dst_res, src_res, bytes);
            } else {
                std::vector<std::thread> workers;
                workers.reserve(p_nt);
                const size_t ch_p = (pixels + p_nt - 1) / p_nt;
                for (int t = 0; t < p_nt; ++t) {
                    workers.emplace_back([=]() {
                        const size_t a0 = size_t(t) * ch_p, a1 = std::min(pixels, a0 + ch_p);
                        if (a0 < a1) std::memcpy(dst_res + a0, src_res + a0, (a1 - a0) * sizeof(float));
                    });
                }
                for (auto& th : workers) th.join();
            }
        }
        return true;
    }
}

} // namespace heraw_gpu

#endif // __APPLE__
