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

inline uint v3_cfa_at(int r, int c, uint w0, uint w1, uint w2) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? w0 : ((idx < 32) ? w1 : w2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

inline float v3_eval_nlt_sq(float s0, float s1, float s2, float iv) {
    float x = clamp(iv, 0.0f, 1.0f);
    return max(0.0f, s0 + x * (s1 + x * s2));
}

inline float v3_sat_taper(float v, float lo, float inv_span) {
    float t = (v - lo) * inv_span;
    if (t >= 1.0f) return 1e-4f;
    return max(1e-4f, 0.5f * (1.0f + cos(3.14159265f * t)));
}

inline float4 v3_sample_half(device const float4* flows_all, int Hh, int Wh, int base, float y1, float x1) {
    if (Hh < 2 || Wh < 2) return flows_all[base];
    float yc = clamp(0.5f * y1 - 0.25f, 0.0f, float(Hh - 1));
    float xc = clamp(0.5f * x1 - 0.25f, 0.0f, float(Wh - 1));
    int y0 = clamp(int(yc), 0, Hh - 2);
    int x0 = clamp(int(xc), 0, Wh - 2);
    int y1i = min(y0 + 1, Hh - 1);
    int x1i = min(x0 + 1, Wh - 1);
    float fy = clamp(yc - float(y0), 0.0f, 1.0f);
    float fx = clamp(xc - float(x0), 0.0f, 1.0f);
    int idx0 = base + y0 * Wh + x0;
    int idx1 = base + y1i * Wh + x0;
    float4 v0 = mix(flows_all[idx0], flows_all[idx0 + 1], fx);
    float4 v1 = mix(flows_all[idx1], flows_all[idx1 + 1], fx);
    return mix(v0, v1, fy);
}

inline int v3_reflect_coord(int i, int n) {
    if (n <= 1) return 0;
    if (i < 0) i = -i;
    while (i >= n) {
        i = (i >= 2 * n - 1) ? (i % max(1, 2 * n - 2)) : (2 * n - 2 - i);
        if (i < 0) i = -i;
    }
    return clamp(i, 0, n - 1);
}

struct PC_V3PrepGuides {
    int H1, W1, Hh, Wh, cov_off;
    float sig0_ref, gain_ref;
    uint cfa0, cfa1, cfa2;
};

kernel void k_v3_prep_guides(
    device const float4* icov_guide [[buffer(0)]],
    device const float* c_anc_1x [[buffer(1)]],
    device const float* snr_boost [[buffer(2)]],
    device float4* v3_guides [[buffer(3)]],
    constant PC_V3PrepGuides& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), r = int(gid.y);
    if (c >= pc.Wh || r >= pc.Hh) return;

    int hi = r * pc.Wh + c;
    float4 ic = icov_guide[hi];
    float gx = icov_guide[r * pc.Wh + min(c + 1, pc.Wh - 1)].w - icov_guide[r * pc.Wh + max(c - 1, 0)].w;
    float gy = icov_guide[min(r + 1, pc.Hh - 1) * pc.Wh + c].w - icov_guide[max(r - 1, 0) * pc.Wh + c].w;
    float grad_sq = 0.25f * (gx * gx + gy * gy);
    float v_1px = pc.sig0_ref + pc.gain_ref * max(0.0f, ic.w);
    float v_loc = max(1e-10f, 2.0f * v_1px);
    float g_mn = 1e30f, g_mx = -1e30f;
    int g_cnt = 0;
    int y0_a = max(0, 2 * r - 1), y1_a = min(pc.H1 - 1, 2 * r + 2);
    int x0_a = max(0, 2 * c - 1), x1_a = min(pc.W1 - 1, 2 * c + 2);
    for (int yy = y0_a; yy <= y1_a; ++yy) {
        for (int xx = x0_a; xx <= x1_a; ++xx) {
            if (v3_cfa_at(yy, xx, pc.cfa0, pc.cfa1, pc.cfa2) == 1u) {
                float gv = c_anc_1x[yy * pc.W1 + xx];
                g_mn = min(g_mn, gv);
                g_mx = max(g_mx, gv);
                ++g_cnt;
            }
        }
    }
    float span_g = (g_cnt >= 2) ? (g_mx - g_mn) : 0.0f;
    float hf_sq = 0.25f * max(0.0f, span_g * span_g - 14.0f * v_1px);
    float snr2 = max(grad_sq, hf_sq) / v_loc;
    float gamma = snr2 / (1.0f + snr2);
    float iso = 0.5f * (ic.x + ic.z);
    v3_guides[pc.cov_off + hi] = float4(iso + gamma * (ic.x - iso), gamma * ic.y, iso + gamma * (ic.z - iso), ic.w);

    float4 hdr0 = v3_guides[0];
    float4 hdr1 = v3_guides[1];
    float4 fp0_0 = v3_guides[4];
    float4 fp1_0 = v3_guides[5];
    float wb_r = hdr0.w, wb_b = hdr1.x;
    float g_sat_lo = fp0_0.w, g_sat_ispan = fp1_0.x;
    int n_frames_pg = max(1, (pc.cov_off - 4) / 3);
    for (int fn = 1; fn < n_frames_pg; ++fn) {
        float4 fp0_n = v3_guides[4 + 3 * fn + 0];
        if (fp0_n.w > g_sat_lo) {
            g_sat_lo = fp0_n.w;
            g_sat_ispan = v3_guides[4 + 3 * fn + 1].x;
        }
    }
    auto local_g_anc = [&](int rr, int cc, float fallback_g) -> float {
        int rm = max(0, rr - 1), rp = min(pc.H1 - 1, rr + 1);
        int cm = max(0, cc - 1), cp = min(pc.W1 - 1, cc + 1);
        float gs = 0.0f, gc = 0.0f;
        int cy[4] = {rm, rp, rr, rr};
        int cx[4] = {cc, cc, cm, cp};
        for (int k = 0; k < 4; ++k) {
            if (v3_cfa_at(cy[k], cx[k], pc.cfa0, pc.cfa1, pc.cfa2) == 1u) {
                gs += c_anc_1x[cy[k] * pc.W1 + cx[k]];
                gc += 1.0f;
            }
        }
        return (gc > 0.0f) ? (gs / gc) : fallback_g;
    };
    auto anc_diff = [&](int yi, int xi, uint tch, float wb, float fallback_g, thread float& out_span) -> float {
        float yc = float(2 * yi) + 0.5f, xc = float(2 * xi) + 0.5f;
        int r0 = max(0, 2 * yi - 2), r1 = min(pc.H1 - 1, 2 * yi + 3);
        int c0 = max(0, 2 * xi - 2), c1 = min(pc.W1 - 1, 2 * xi + 3);
        float sum_d = 0.0f, sum_w = 0.0f;
        float d_mn1 = 1e30f, d_mn2 = 1e30f, d_mx1 = -1e30f, d_mx2 = -1e30f;
        float g_mn_loc = 1e30f, g_mx_loc = -1e30f;
        int d_cnt = 0;
        bool any_hi = false;
        for (int rr = r0; rr <= r1; ++rr) {
            float dy2 = (float(rr) - yc) * (float(rr) - yc);
            for (int cc = c0; cc <= c1; ++cc) {
                if (v3_cfa_at(rr, cc, pc.cfa0, pc.cfa1, pc.cfa2) != tch) continue;
                float raw_c = c_anc_1x[rr * pc.W1 + cc];
                float g_loc = local_g_anc(rr, cc, fallback_g);
                float dv_c = raw_c * wb - g_loc;
                float st_c = (raw_c > g_sat_lo) ? v3_sat_taper(raw_c, g_sat_lo, g_sat_ispan) : 1.0f;
                float st_g = (g_loc > g_sat_lo) ? v3_sat_taper(g_loc, g_sat_lo, g_sat_ispan) : 1.0f;
                if (raw_c >= 0.60f * g_sat_lo || g_loc >= 0.60f * g_sat_lo) any_hi = true;
                if (raw_c < g_sat_lo && g_loc < g_sat_lo) {
                    if (dv_c < d_mn1) { d_mn2 = d_mn1; d_mn1 = dv_c; } else if (dv_c < d_mn2) { d_mn2 = dv_c; }
                    if (dv_c > d_mx1) { d_mx2 = d_mx1; d_mx1 = dv_c; } else if (dv_c > d_mx2) { d_mx2 = dv_c; }
                    g_mn_loc = min(g_mn_loc, g_loc);
                    g_mx_loc = max(g_mx_loc, g_loc);
                    ++d_cnt;
                }
                float w = (st_c * st_g) / (0.25f + dy2 + (float(cc) - xc) * (float(cc) - xc));
                sum_d += w * dv_c;
                sum_w += w;
            }
        }
        out_span = (!any_hi && d_cnt >= 4 && d_mx2 > d_mn2) ? max(0.0f, (d_mx2 - d_mn2) - 4.0f * max(0.0f, g_mx_loc - g_mn_loc)) : 0.0f;
        return sum_d / (sum_w + 0.02f);
    };
    float sr = 0.0f, sb = 0.0f;
    float ar = anc_diff(r, c, 0u, wb_r, ic.w, sr);
    float ab = anc_diff(r, c, 2u, wb_b, ic.w, sb);
    float packed_span = as_type<float>(half2(half(sr), half(sb)));
    v3_guides[pc.cov_off + pc.Hh * pc.Wh + hi] = float4(ar, ab, snr_boost[hi], packed_span);
}

struct PC_V3Green {
    int H1, W1, Hh, Wh, Ew, Eh, N, out_scale;
    int origin_y, origin_x, is_bayer, is_clean_plate;
    int is_virtual_nd, is_aeb, any_rob, fcomp_off_eta;
    uint cfa0, cfa1, cfa2;
    int disable_ha_fallback;
};

kernel void k_v3_green(
    device const float* frames_all [[buffer(0)]],
    device const float4* flows_all [[buffer(1)]],
    device const float4* v3_guides [[buffer(2)]],
    device const float* g0_ha_1x [[buffer(3)]],
    device float* green_out [[buffer(4)]],
    device float* fcomp_eta_out [[buffer(5)]],
    constant PC_V3Green& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X1 = int(gid.x), Y1 = int(gid.y);
    if (X1 >= pc.W1 || Y1 >= pc.H1) return;

    bool eta_only = (pc.fcomp_off_eta < 0);
    bool is_eta_site = (((Y1 | X1) & 3) == 0);
    if (eta_only && !is_eta_site) return;

    float4 c0 = v3_guides[0];
    float4 c2 = v3_guides[2];
    float4 c3 = v3_guides[3];
    float sig0_ref = c0.x, gain_ref = c0.y, sat0 = c0.z;
    float q_luma_var = c2.z;
    float q_nlt_s0 = c3.x, q_nlt_s1 = c3.y, q_nlt_s2 = c3.z;

    auto ha_at = [&](int yy, int xx) -> float {
        return g0_ha_1x[clamp(yy, 0, pc.H1 - 1) * pc.W1 + clamp(xx, 0, pc.W1 - 1)];
    };
    float ha_c = ha_at(Y1, X1);
    float mn3 = ha_c, mx3 = ha_c;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            float hv = ha_at(Y1 + dy, X1 + dx);
            mn3 = min(mn3, hv);
            mx3 = max(mx3, hv);
        }
    }
    float c_loc = mx3 - mn3;
    float min_tv = max(0.012f, 8.0f * sqrt(max(1e-12f, sig0_ref + gain_ref * max(0.0f, ha_c))));
    auto mono5 = [&](float u0, float u1, float u2, float u3, float u4) -> float {
        float tv = abs(u1 - u0) + abs(u2 - u1) + abs(u3 - u2) + abs(u4 - u3);
        if (tv <= min_tv) return 0.0f;
        return clamp((tv - abs(u4 - u0)) / tv, 0.0f, 1.0f);
    };
    float gm2x = ha_at(Y1, X1 - 2), gm1x = ha_at(Y1, X1 - 1), gp1x = ha_at(Y1, X1 + 1), gp2x = ha_at(Y1, X1 + 2);
    float gm2y = ha_at(Y1 - 2, X1), gm1y = ha_at(Y1 - 1, X1), gp1y = ha_at(Y1 + 1, X1), gp2y = ha_at(Y1 + 2, X1);
    float rho_hv = max(mono5(gm2x, gm1x, ha_c, gp1x, gp2x), mono5(gm2y, gm1y, ha_c, gp1y, gp2y));
    float ha_keys[4] = {ha_c, ha_c, ha_c, ha_c};
    if (pc.out_scale == 2 && !eta_only) {
        float g01 = gp1x, g10 = gp1y, g11 = ha_at(Y1 + 1, X1 + 1);
        ha_keys[1] = clamp((-gm1x + 9.0f * ha_c + 9.0f * g01 - gp2x) * 0.0625f, min(ha_c, g01), max(ha_c, g01));
        ha_keys[2] = clamp((-gm1y + 9.0f * ha_c + 9.0f * g10 - gp2y) * 0.0625f, min(ha_c, g10), max(ha_c, g10));
        float rv4[4];
        for (int kr = -1; kr <= 2; ++kr) {
            rv4[kr + 1] = (-ha_at(Y1 + kr, X1 - 1) + 9.0f * ha_at(Y1 + kr, X1) +
                           9.0f * ha_at(Y1 + kr, X1 + 1) - ha_at(Y1 + kr, X1 + 2)) * 0.0625f;
        }
        float mn2 = min(min(ha_c, g01), min(g10, g11));
        float mx2 = max(max(ha_c, g01), max(g10, g11));
        ha_keys[3] = clamp((-rv4[0] + 9.0f * rv4[1] + 9.0f * rv4[2] - rv4[3]) * 0.0625f, mn2, mx2);
    }

    int cov_off = 4 + 3 * pc.N;
    int hi = clamp(Y1 >> 1, 0, max(0, pc.Hh - 1)) * max(1, pc.Wh) + clamp(X1 >> 1, 0, max(0, pc.Wh - 1));
    float4 cg = v3_guides[cov_off + hi];
    float anchor = cg.w;
    float a_pos = max(0.0f, ha_c);
    float v_ref = sig0_ref + gain_ref * a_pos;
    float gq_cell = (pc.out_scale == 2 && !eta_only)
        ? v3_sample_half(flows_all, pc.Hh, pc.Wh, 0, float(Y1), float(X1)).w : 0.0f;
    float snr_cell = max(0.0f, c_loc * c_loc - 16.0f * v_ref) / max(1e-10f, 64.0f * v_ref);
    float detail_g = snr_cell / (1.0f + snr_cell);
    float w_2x = (pc.out_scale == 2 && !eta_only) ? (gq_cell * detail_g) : 0.0f;
    float axx = (pc.out_scale == 2) ? (cg.x + w_2x * (22.0f - cg.x)) : (0.72f * cg.x);
    float axy = (pc.out_scale == 2) ? (cg.y * (1.0f - w_2x)) : (0.72f * cg.y);
    float ayy = (pc.out_scale == 2) ? (cg.z + w_2x * (22.0f - cg.z)) : (0.72f * cg.z);
    float area_norm = (pc.out_scale == 2 && !eta_only) ? max(1.0f, 0.5f * (axx + ayy) * (1.0f / 6.57f)) : 1.0f;
    float inv_area = 1.0f / max(1.0f, area_norm);
    float tr_a = axx + ayy, df_a = axx - ayy;
    float disc_a = sqrt(max(0.0f, df_a * df_a + 4.0f * axy * axy));
    float anis = disc_a / max(1e-6f, tr_a);
    bool use_curv = (pc.N >= 3) && (pc.is_clean_plate == 0) && (pc.is_virtual_nd == 0) && (anis > 0.25f) && (rho_hv <= 0.35f) && (w_2x < 0.45f);
    float nx_c = 1.0f, ny_c = 0.0f;
    if (use_curv) {
        float vx = (df_a >= 0.0f) ? (df_a + disc_a) : (2.0f * axy);
        float vy = (df_a >= 0.0f) ? (2.0f * axy) : (disc_a - df_a);
        float inv_vn = 1.0f / sqrt(max(1e-18f, vx * vx + vy * vy));
        nx_c = vx * inv_vn; ny_c = vy * inv_vn;
    }
    float nlt_sq = (q_luma_var > 0.0f) ? v3_eval_nlt_sq(q_nlt_s0, q_nlt_s1, q_nlt_s2, a_pos) : 0.0f;
    float thr_base = (pc.is_aeb == 0 && pc.any_rob != 0)
        ? (3.5f * sqrt(max(0.0f, v_ref + q_luma_var * nlt_sq)) + 0.008f) : 0.0f;
    float osc_slack_hi = 0.65f * c_loc + 0.35f * rho_hv * max(0.08f, c_loc);
    float osc_slack_lo = 0.15f * c_loc;

    int g_col0 = (v3_cfa_at(0, 0, pc.cfa0, pc.cfa1, pc.cfa2) == 1u) ? 0 : 1;
    int g_col1 = (v3_cfa_at(1, 0, pc.cfa0, pc.cfa1, pc.cfa2) == 1u) ? 0 : 1;
    int p_cnt = (eta_only || pc.out_scale == 1) ? 1 : 4;
    float max_ha_blend = (pc.out_scale == 1) ? 0.85f : 1.0f;

    float S0[4], S0_cov[4], Sx[4], Sy[4], Sxx[4], Sxy[4], Syy[4], T0[4], Tx[4], Ty[4], Sw2[4], vmin[4], vmax[4];
    float Sqx[4], Sqy[4], Sqq[4], Tq[4];
    float y_base[4], x_base[4];
    for (int p = 0; p < 4; ++p) {
        S0[p] = 0.0f; S0_cov[p] = 0.0f; Sx[p] = 0.0f; Sy[p] = 0.0f;
        Sxx[p] = 0.0f; Sxy[p] = 0.0f; Syy[p] = 0.0f;
        Sqx[p] = 0.0f; Sqy[p] = 0.0f; Sqq[p] = 0.0f; Tq[p] = 0.0f;
        T0[p] = 0.0f; Tx[p] = 0.0f; Ty[p] = 0.0f;
        Sw2[p] = 0.0f; vmin[p] = 1e30f; vmax[p] = -1e30f;
        y_base[p] = float(Y1) + ((p >= 2) ? 0.5f : 0.0f);
        x_base[p] = float(X1) + (((p & 1) != 0) ? 0.5f : 0.0f);
    }

    int n_1x = pc.H1 * pc.W1;
    int n_half = pc.Hh * pc.Wh;

    for (int n = 0; n < pc.N; ++n) {
        float4 fp0 = v3_guides[4 + 3 * n + 0];
        float4 fp1 = v3_guides[4 + 3 * n + 1];
        float fw = fp0.x;
        if (fw <= 0.0f) continue;
        float rv_n = fp0.y, sh_n = fp0.z, sat_lo_n = fp0.w;
        float sat_ispan_n = fp1.x;
        bool use_rob = (fp1.y > 0.5f);
        bool unit_m = (pc.is_virtual_nd != 0) || (fp1.z > 0.5f);
        bool has_fl = (fp1.w > 0.5f);

        int fbase = n * n_1x;
        int flbase = n * n_half;

        float sy[4], sx[4], mwp[4];
        uint act_mask = 0u;
        float max_mw_n = 0.0f;
        for (int p = 0; p < p_cnt; ++p) {
            float4 fm = (unit_m && !has_fl) ? float4(0.0f, 0.0f, 1.0f, 0.0f)
                                            : v3_sample_half(flows_all, pc.Hh, pc.Wh, flbase, y_base[p], x_base[p]);
            float mw = (unit_m ? 1.0f : fm.z) * fw;
            mwp[p] = mw;
            if (mw < 0.01f) continue;
            max_mw_n = max(max_mw_n, mw);
            float dy = has_fl ? (2.0f * fm.x) : 0.0f;
            float dx = has_fl ? (2.0f * fm.y) : 0.0f;
            float syv = (float(Y1 + pc.origin_y) + (y_base[p] - float(Y1)) + dy) - float(pc.origin_y);
            float sxv = (float(X1 + pc.origin_x) + (x_base[p] - float(X1)) + dx) - float(pc.origin_x);
            sy[p] = syv; sx[p] = sxv;
            if (syv >= -0.5f && syv <= float(pc.H1) - 0.5f && sxv >= -0.5f && sxv <= float(pc.W1) - 0.5f) {
                act_mask |= (1u << uint(p));
            }
        }
        if (act_mask == 0u) continue;

        float inv_var_rel = 1.0f, inv_wvar = 1.0f;
        float thr_n = thr_base;
        if (pc.is_aeb != 0 && pc.is_virtual_nd == 0) {
            float v_n = rv_n + sh_n * a_pos;
            if (v_ref > 1e-12f && v_n > 1e-12f) {
                inv_var_rel = clamp(v_ref / v_n, 0.004f, 64.0f);
                inv_wvar = 1.0f / inv_var_rel;
            }
            if (use_rob) thr_n = 3.5f * sqrt(max(0.0f, v_n + q_luma_var * nlt_sq)) + 0.008f;
        }

        float min_sy = 1e9f, max_sy = -1e9f, min_sx = 1e9f, max_sx = -1e9f;
        for (int p = 0; p < p_cnt; ++p) {
            if ((act_mask & (1u << uint(p))) == 0u) continue;
            min_sy = min(min_sy, sy[p]); max_sy = max(max_sy, sy[p]);
            min_sx = min(min_sx, sx[p]); max_sx = max(max_sx, sx[p]);
        }
        int r_lo = max(0, int(floor(min_sy)) - 2);
        int r_hi = min(pc.H1 - 1, int(floor(max_sy)) + 3);
        int c_lo = max(0, int(floor(min_sx)) - 2);
        int c_hi = min(pc.W1 - 1, int(floor(max_sx)) + 3);

        float nyq_slack = 0.0f;
        if (p_cnt == 4 && use_rob && gq_cell >= 0.25f && max_mw_n >= 0.75f * fw) {
            float g_mn1 = 1e30f, g_mn2 = 1e30f, g_mx1 = -1e30f, g_mx2 = -1e30f;
            int g_cnt_s = 0;
            for (int rr = r_lo; rr <= r_hi; ++rr) {
                int c_s = c_lo, c_st = 1;
                if (pc.is_bayer != 0) {
                    int par = ((rr & 1) == 0) ? g_col0 : g_col1;
                    if ((c_s & 1) != par) ++c_s;
                    c_st = 2;
                }
                for (int cc = c_s; cc <= c_hi; cc += c_st) {
                    if (pc.is_bayer == 0 && v3_cfa_at(rr, cc, pc.cfa0, pc.cfa1, pc.cfa2) != 1u) continue;
                    float gv = frames_all[fbase + rr * pc.W1 + cc];
                    if (gv >= sat_lo_n) continue;
                    if (gv < g_mn1) { g_mn2 = g_mn1; g_mn1 = gv; } else if (gv < g_mn2) { g_mn2 = gv; }
                    if (gv > g_mx1) { g_mx2 = g_mx1; g_mx1 = gv; } else if (gv > g_mx2) { g_mx2 = gv; }
                    ++g_cnt_s;
                }
            }
            if (g_cnt_s >= 4 && g_mx2 > g_mn2) {
                float stat_g = clamp((max_mw_n / fw - 0.75f) * (1.0f / 0.20f), 0.0f, 1.0f);
                nyq_slack = 0.90f * (g_mx2 - g_mn2) * stat_g * clamp((gq_cell - 0.25f) * 2.5f, 0.0f, 1.0f);
            }
        }
        float thr_eff = thr_n + max((max_mw_n >= 0.55f * fw) ? osc_slack_hi : osc_slack_lo, nyq_slack);

        for (int rr = r_lo; rr <= r_hi; ++rr) {
            int cc_start = c_lo;
            int cc_step = 1;
            if (pc.is_bayer != 0) {
                int par = ((rr & 1) == 0) ? g_col0 : g_col1;
                if ((cc_start & 1) != par) ++cc_start;
                cc_step = 2;
            }
            for (int cc = cc_start; cc <= c_hi; cc += cc_step) {
                if (pc.is_bayer == 0 && v3_cfa_at(rr, cc, pc.cfa0, pc.cfa1, pc.cfa2) != 1u) continue;
                float v = frames_all[fbase + rr * pc.W1 + cc];
                float st = (v > sat_lo_n) ? v3_sat_taper(v, sat_lo_n, sat_ispan_n) : 1.0f;
                float rob = 1.0f;
                if (use_rob && (anchor < 0.82f * sat0 && v < 0.82f * sat0)) {
                    float ad = abs(v - ha_c);
                    if (ad > thr_eff) { float q = thr_eff / ad; rob = q * q * q; }
                }
                float w_cov_base = st * rob;
                float w_base = inv_var_rel * w_cov_base;
                for (int p = 0; p < p_cnt; ++p) {
                    if ((act_mask & (1u << uint(p))) == 0u) continue;
                    float yy = float(rr) - sy[p];
                    float xx = float(cc) - sx[p];
                    float d = axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy;
                    if (d >= 9.0f) continue;
                    float tap_w = 1.0f;
                    if (d > 6.0f) {
                        float tt = (d - 6.0f) * (1.0f / 3.0f);
                        tap_w = 1.0f - tt * tt * (3.0f - 2.0f * tt);
                    }
                    float k_tap = fast_exp_neg(0.5f * d) * tap_w;
                    float w = mwp[p] * w_base * k_tap;
                    if (w <= 1e-12f) continue;
                    S0_cov[p] += mwp[p] * w_cov_base * k_tap;
                    S0[p] += w; Sx[p] += w * xx; Sy[p] += w * yy;
                    Sxx[p] += w * xx * xx; Sxy[p] += w * xx * yy; Syy[p] += w * yy * yy;
                    if (use_curv) {
                        float un = nx_c * xx + ny_c * yy;
                        float q = 0.5f * un * un;
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

    float g_s0_cell = 0.0f;
    if (p_cnt == 4) {
        float min_s0_cell = min(min(S0_cov[0], S0_cov[1]), min(S0_cov[2], S0_cov[3])) * area_norm;
        float ts_cell = clamp((min_s0_cell - 0.20f) * (1.0f / 0.65f), 0.0f, 1.0f);
        g_s0_cell = ts_cell * ts_cell * (3.0f - 2.0f * ts_cell);
    }

    int W_out = pc.W1 * pc.out_scale;
    for (int p = 0; p < p_cnt; ++p) {
        float phi_fisher = 0.0f, eta_px = 1.0f, g_val = 0.0f;
        float ha_val = ha_keys[p];
        float a_ha_blend = 0.0f;
        if (pc.out_scale == 2 && p_cnt == 4) {
            float gq = v3_sample_half(flows_all, pc.Hh, pc.Wh, 0, y_base[p], x_base[p]).w;
            a_ha_blend = 1.0f - gq * g_s0_cell;
        } else {
            a_ha_blend = (1.5f - S0_cov[p]) / 1.1f;
        }
        if (S0[p] > 1e-8f) {
            float inv_s0 = 1.0f / S0[p];
            float g_nw = T0[p] * inv_s0;
            float eta_nw = clamp(Sw2[p] * inv_s0 * inv_s0, 1e-4f, 1.0f);
            float mx = Sx[p] * inv_s0, my = Sy[p] * inv_s0;
            float F00 = max(0.0f, Sxx[p] - Sx[p] * mx);
            float F01 = Sxy[p] - Sx[p] * my;
            float F11 = max(0.0f, Syy[p] - Sy[p] * my);
            float tr_f = (F00 + F11) * inv_s0;
            float df_f = (F00 - F11) * inv_s0;
            float s01 = (2.0f * F01) * inv_s0;
            float disc = sqrt(max(0.0f, df_f * df_f + s01 * s01));
            float lam_min = max(0.0f, 0.5f * (tr_f - disc));
            float anis_scale = 1.0f - 0.55f * (use_curv ? anis : 0.0f);
            phi_fisher = clamp((lam_min * area_norm) / (0.16f * anis_scale), 0.0f, 1.0f) *
                         clamp((max(S0[p], S0_cov[p]) * area_norm) / (3.0f * anis_scale), 0.0f, 1.0f);

            float reg = (use_curv ? 0.008f : 0.02f) * S0[p] * inv_area;
            float F00_r = F00 + reg, F11_r = F11 + reg;
            float det_r = max(1e-12f, F00_r * F11_r - F01 * F01);
            float rx_t = Tx[p] - mx * T0[p], ry_t = Ty[p] - my * T0[p];
            float bx = (F11_r * rx_t - F01 * ry_t) / det_r;
            float by = (F00_r * ry_t - F01 * rx_t) / det_r;
            float q_lev = max(0.0f, S0[p] * (F11_r * mx * mx - 2.0f * F01 * mx * my + F00_r * my * my) / det_r);
            float lev_cap = (q_lev > 0.5f) ? sqrt(0.5f / q_lev) : 1.0f;
            float alpha_ll = phi_fisher * lev_cap;
            float delta_g = -alpha_ll * (bx * mx + by * my);
            float extra_var = alpha_ll * alpha_ll * q_lev;
            if (use_curv && S0[p] * area_norm > 0.85f) {
                float inv_area2 = inv_area * inv_area;
                float Sq = 0.5f * (nx_c * nx_c * Sxx[p] + 2.0f * nx_c * ny_c * Sxy[p] + ny_c * ny_c * Syy[p]);
                float mu_q = Sq * inv_s0;
                float cqx = Sqx[p] - mu_q * Sx[p], cqy = Sqy[p] - mu_q * Sy[p];
                float cqq = max(0.0f, Sqq[p] - mu_q * Sq);
                float rq = Tq[p] - mu_q * T0[p];
                float gx_q = (F11_r * cqx - F01 * cqy) / det_r;
                float gy_q = (F00_r * cqy - F01 * cqx) / det_r;
                float Dq = max(0.0f, cqq - (gx_q * cqx + gy_q * cqy));
                float Dq_r = Dq + 0.003f * S0[p] * inv_area2;
                float rq_orth = rq - (bx * cqx + by * cqy);
                float bq = rq_orth / Dq_r;
                float q0_orth = -mu_q + gx_q * mx + gy_q * my;
                float q_lev_q = max(0.0f, S0[p] * q0_orth * q0_orth / Dq_r);
                float lev_cap_q = (q_lev_q > 0.35f) ? sqrt(0.35f / q_lev_q) : 1.0f;
                float gate_q = clamp((anis - 0.25f) * 2.0f, 0.0f, 1.0f) * clamp(Dq / (0.004f * S0[p] * inv_area2), 0.0f, 1.0f);
                float alpha_q = alpha_ll * gate_q * lev_cap_q;
                delta_g += alpha_q * bq * q0_orth;
                extra_var += alpha_q * alpha_q * q_lev_q;
            }
            g_val = clamp(g_nw + delta_g, vmin[p], vmax[p]);

            float a_ha = (pc.is_clean_plate != 0 || pc.is_virtual_nd != 0 || pc.disable_ha_fallback != 0)
                ? 0.0f : clamp(a_ha_blend, 0.0f, max_ha_blend);
            if (a_ha > 1e-4f) g_val += a_ha * (ha_val - g_val);
            eta_px = (1.0f - a_ha) * (1.0f - a_ha) * eta_nw * (1.0f + extra_var) + a_ha * a_ha * 0.55f;
        } else {
            phi_fisher = 0.0f;
            eta_px = 1.0f;
            if (pc.is_clean_plate != 0 || pc.is_virtual_nd != 0) {
                g_val = g0_ha_1x[clamp(int(y_base[p] + 0.5f), 0, pc.H1 - 1) * pc.W1 + clamp(int(x_base[p] + 0.5f), 0, pc.W1 - 1)];
            } else {
                g_val = ha_val;
            }
        }
        int Yo = (pc.out_scale == 2) ? (2 * Y1 + (p >> 1)) : Y1;
        int Xo = (pc.out_scale == 2) ? (2 * X1 + (p & 1)) : X1;
        int out_idx = Yo * W_out + Xo;
        if (!eta_only) {
            green_out[out_idx] = g_val;
            if (pc.fcomp_off_eta > 0) fcomp_eta_out[out_idx] = 1.0f - phi_fisher;
        }
        if (p == 0 && is_eta_site) {
            int eta_base = (pc.fcomp_off_eta > 0) ? pc.fcomp_off_eta : 0;
            int ey = clamp(Y1 >> 2, 0, pc.Eh - 1);
            int ex = clamp(X1 >> 2, 0, pc.Ew - 1);
            fcomp_eta_out[eta_base + ey * pc.Ew + ex] = eta_px;
        }
    }
}

struct PC_V3Chroma {
    int H1, W1, Hh, Wh, N, out_scale, full_H1, full_W1;
    int origin_y, origin_x, is_bayer, is_xtrans;
    int is_virtual_nd, is_aeb, any_rob, is_astro;
    uint cfa0, cfa1, cfa2;
};

kernel void k_v3_chroma(
    device const float* frames_all [[buffer(0)]],
    device const float4* flows_all [[buffer(1)]],
    device const float4* v3_guides [[buffer(2)]],
    device const float* green_in [[buffer(3)]],
    device float* dR_1x [[buffer(4)]],
    device float* dB_1x [[buffer(5)]],
    constant PC_V3Chroma& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), Y = int(gid.y);
    if (X >= pc.W1 || Y >= pc.H1) return;

    float4 c0 = v3_guides[0], c1 = v3_guides[1], c2 = v3_guides[2], c3 = v3_guides[3];
    float sig0_ref = c0.x, gain_ref = c0.y, sat0 = c0.z, wb_r = c0.w;
    float wb_b = c1.x, hl_lim = c1.y, lca_rk1 = c1.z, lca_rk2 = c1.w;
    float lca_bk1 = c2.x, lca_bk2 = c2.y, q_chroma_var = c2.w;
    float q_nlt_s0 = c3.x, q_nlt_s1 = c3.y, q_nlt_s2 = c3.z;

    int cov_off = 4 + 3 * pc.N;
    int anc_off = cov_off + pc.Hh * pc.Wh;
    int hi = clamp(Y >> 1, 0, max(0, pc.Hh - 1)) * max(1, pc.Wh) + clamp(X >> 1, 0, max(0, pc.Wh - 1));
    float4 cg = v3_guides[cov_off + hi];
    float4 ab = v3_guides[anc_off + hi];
    half2 chr_spans = as_type<half2>(ab.w);
    float opp_t = clamp((max(float(chr_spans.x), float(chr_spans.y)) - 0.02f) * 10.0f, 0.0f, 1.0f);
    float scale_c_loc = 0.55f + 0.18f * opp_t;
    float area_c_norm = scale_c_loc * (1.0f / 0.55f);
    float axx = scale_c_loc * cg.x, axy = scale_c_loc * cg.y, ayy = scale_c_loc * cg.z, g_gd = cg.w;

    int Ho = pc.H1 * pc.out_scale, Wo = pc.W1 * pc.out_scale;
    float g_here = green_in[(Y * pc.out_scale) * Wo + (X * pc.out_scale)];
    float out_s_f = float(pc.out_scale);
    auto bilerp_g = [&](float y, float x) -> float {
        y = clamp(y, 0.0f, float(Ho - 1));
        x = clamp(x, 0.0f, float(Wo - 1));
        int y0 = int(floor(y)), x0 = int(floor(x));
        int y1i = min(y0 + 1, Ho - 1), x1i = min(x0 + 1, Wo - 1);
        float fy = y - float(y0), fx = x - float(x0);
        return mix(mix(green_in[y0 * Wo + x0], green_in[y0 * Wo + x1i], fx),
                   mix(green_in[y1i * Wo + x0], green_in[y1i * Wo + x1i], fx), fy);
    };

    float opt_cy = 0.5f * float(pc.full_H1), opt_cx = 0.5f * float(pc.full_W1);
    float inv_opt_r = 1.0f / max(1.0f, max(opt_cy, opt_cx));
    float ry = (float(Y) + float(pc.origin_y)) - opt_cy;
    float rx = (float(X) + float(pc.origin_x)) - opt_cx;
    float rn = (rx * inv_opt_r) * (rx * inv_opt_r) + (ry * inv_opt_r) * (ry * inv_opt_r);

    int rad_c = (pc.is_xtrans != 0) ? 3 : 2;
    float xt_scale = (pc.is_xtrans != 0) ? 0.58f : 1.0f;
    float dlim_c = (pc.is_xtrans != 0) ? 9.5f : 7.5f;
    int n_1x = pc.H1 * pc.W1;
    int n_half = pc.Hh * pc.Wh;

    float4 fp0_0 = v3_guides[4], fp1_0 = v3_guides[5];
    float g_sat_lo = fp0_0.w, g_sat_ispan = fp1_0.x;
    if (pc.is_aeb != 0) {
        for (int fn = 1; fn < pc.N; ++fn) {
            float4 fp0_n = v3_guides[4 + 3 * fn + 0];
            if (fp0_n.w > g_sat_lo) {
                g_sat_lo = fp0_n.w;
                g_sat_ispan = v3_guides[4 + 3 * fn + 1].x;
            }
        }
    }

    for (int b = 0; b < 2; ++b) {
        float anc_diff = (b != 0) ? ab.y : ab.x;
        float chr_span = (b != 0) ? float(chr_spans.y) : float(chr_spans.x);
        float wb = (b != 0) ? wb_b : wb_r;
        float anchor_raw = max(0.0f, (g_gd + anc_diff) / max(1e-4f, wb));
        float c_pos = max(0.0f, max(anchor_raw, g_gd));
        float v_ref = sig0_ref + gain_ref * c_pos;
        float nlt_sq = (q_chroma_var > 0.0f) ? v3_eval_nlt_sq(q_nlt_s0, q_nlt_s1, q_nlt_s2, c_pos) : 0.0f;
        float thr_base_wb = (pc.is_aeb == 0 && pc.any_rob != 0)
            ? (wb * (4.0f * sqrt(max(0.0f, v_ref + q_chroma_var * nlt_sq)) + 0.01f)) : 0.0f;
        float lca = (b != 0) ? (lca_bk1 + lca_bk2 * rn) : (lca_rk1 + lca_rk2 * rn);
        float lca_dy = lca * ry, lca_dx = lca * rx;
        uint target_ch = (b != 0) ? 2u : 0u;

        int rb_ry = -1, rb_rx = -1;
        if (pc.is_bayer != 0) {
            int cnt = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    if (v3_cfa_at(dy, dx, pc.cfa0, pc.cfa1, pc.cfa2) == target_ch) { rb_ry = dy; rb_rx = dx; ++cnt; }
                }
            }
            if (cnt != 1) rb_ry = -1;
        }

        float S0 = 0.0f, Sx = 0.0f, Sy = 0.0f, Sxx = 0.0f, Sxy = 0.0f, Syy = 0.0f;
        float T0 = 0.0f, Tx = 0.0f, Ty = 0.0f;
        float dmin = 1e30f, dmax = -1e30f;

        for (int n = 0; n < pc.N; ++n) {
            float4 fp0 = v3_guides[4 + 3 * n + 0];
            float4 fp1 = v3_guides[4 + 3 * n + 1];
            float4 fp2 = v3_guides[4 + 3 * n + 2];
            float fw = fp0.x;
            if (fw <= 0.0f) continue;
            float rv_n = fp0.y, sh_n = fp0.z, sat_lo_n = fp0.w;
            float sat_ispan_n = fp1.x;
            bool use_rob = (fp1.y > 0.5f);
            bool unit_m = (pc.is_virtual_nd != 0) || (fp1.z > 0.5f);
            bool has_fl = (fp1.w > 0.5f);

            float4 fm = (unit_m && !has_fl) ? float4(0.0f, 0.0f, 1.0f, 0.0f)
                                            : v3_sample_half(flows_all, pc.Hh, pc.Wh, n * n_half, float(Y), float(X));
            float mw = (unit_m ? 1.0f : fm.z) * fw;
            if (mw < 0.01f) continue;

            float ois_y = (b != 0) ? fp2.z : fp2.x;
            float ois_x = (b != 0) ? fp2.w : fp2.y;
            float dy = (has_fl ? (2.0f * fm.x) : 0.0f) + lca_dy + ois_y;
            float dx = (has_fl ? (2.0f * fm.y) : 0.0f) + lca_dx + ois_x;
            float yt = (float(Y + pc.origin_y) + dy) - float(pc.origin_y);
            float xt = (float(X + pc.origin_x) + dx) - float(pc.origin_x);
            if (yt < -0.5f || yt > float(pc.H1) - 0.5f || xt < -0.5f || xt > float(pc.W1) - 0.5f) continue;

            float inv_var_rel = 1.0f;
            float thr_wb = thr_base_wb;
            if (pc.is_aeb != 0 && pc.is_virtual_nd == 0) {
                float v_n = rv_n + sh_n * c_pos;
                if (v_ref > 1e-12f && v_n > 1e-12f) inv_var_rel = clamp(v_ref / v_n, 0.02f, 32.0f);
                if (use_rob) thr_wb = wb * (4.0f * sqrt(max(0.0f, v_n + q_chroma_var * nlt_sq)) + 0.01f);
            }
            if (use_rob && chr_span > 0.0f && mw >= 0.75f * fw) {
                thr_wb += 0.85f * chr_span * clamp((mw / fw - 0.75f) * (1.0f / 0.20f), 0.0f, 1.0f);
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
                    if (rb_ry < 0 && v3_cfa_at(rr, cc, pc.cfa0, pc.cfa1, pc.cfa2) != target_ch) continue;
                    float xx = float(cc) - xt;
                    float d = xt_scale * (axx * xx * xx + 2.0f * axy * xx * yy + ayy * yy * yy);
                    if (d > dlim_c) continue;
                    float raw_v = frames_all[fbase + rr * pc.W1 + cc];
                    float st_raw = (raw_v > sat_lo_n) ? v3_sat_taper(raw_v, sat_lo_n, sat_ispan_n) : 1.0f;
                    float v = raw_v * wb;
                    float g_ref = bilerp_g(gy_src, (float(cc) - dx) * out_s_f);
                    float st_g = (g_ref > g_sat_lo) ? v3_sat_taper(g_ref, g_sat_lo, g_sat_ispan) : 1.0f;
                    float dv = v - g_ref;
                    float rob = 1.0f;
                    if (use_rob && (g_gd < 0.82f * sat0 && raw_v < 0.82f * sat0)) {
                        float ad = abs(dv - anc_diff);
                        if (ad > thr_wb) { float q = thr_wb / ad; rob = q * q * q; }
                    }
                    float w = mw * inv_var_rel * (st_raw * st_g) * rob * fast_exp_neg(0.5f * d);
                    if (w <= 1e-12f) continue;
                    S0 += w; Sx += w * xx; Sy += w * yy;
                    Sxx += w * xx * xx; Sxy += w * xx * yy; Syy += w * yy * yy;
                    T0 += w * dv; Tx += w * xx * dv; Ty += w * yy * dv;
                    dmin = min(dmin, dv);
                    dmax = max(dmax, dv);
                }
            }
        }

        float diff = anc_diff;
        if (S0 > 1e-8f) {
            float inv_s0 = 1.0f / S0;
            float nw_diff = T0 * inv_s0;
            float mx = Sx * inv_s0, my = Sy * inv_s0;
            float F00 = max(0.0f, Sxx - Sx * mx);
            float F01 = Sxy - Sx * my;
            float F11 = max(0.0f, Syy - Sy * my);
            float tr_f = (F00 + F11) * inv_s0;
            float df_f = (F00 - F11) * inv_s0;
            float s01 = (2.0f * F01) * inv_s0;
            float disc = sqrt(max(0.0f, df_f * df_f + s01 * s01));
            float lam_min = max(0.0f, 0.5f * (tr_f - disc));
            float phi_c = clamp((lam_min * area_c_norm) / 0.14f, 0.0f, 1.0f) * clamp((S0 * area_c_norm) / 1.5f, 0.0f, 1.0f);
            float reg = (0.02f * S0) / area_c_norm;
            float F00_r = F00 + reg, F11_r = F11 + reg;
            float det_r = max(1e-12f, F00_r * F11_r - F01 * F01);
            float bx = (F11_r * (Tx - mx * T0) - F01 * (Ty - my * T0)) / det_r;
            float by = (F00_r * (Ty - my * T0) - F01 * (Tx - mx * T0)) / det_r;
            float q_lev = max(0.0f, S0 * (F11_r * mx * mx - 2.0f * F01 * mx * my + F00_r * my * my) / det_r);
            float lev_cap = (q_lev > 0.35f) ? sqrt(0.35f / q_lev) : 1.0f;
            float alpha_ll = phi_c * lev_cap;
            float d_ll = clamp(nw_diff - alpha_ll * (bx * mx + by * my), dmin, dmax);
            float cov_w = clamp(S0 / 0.35f, 0.0f, 1.0f);
            diff = cov_w * d_ll + (1.0f - cov_w) * anc_diff;
        }
        if (g_here > 0.82f * sat0 && diff > 0.0f) {
            float tg = clamp((g_here - 0.82f * sat0) / max(1e-5f, 0.16f * sat0), 0.0f, 1.0f);
            diff *= 1.0f - tg * tg * (3.0f - 2.0f * tg);
        }
        float peak = max(g_here, anchor_raw);
        if (peak > hl_lim) {
            float t = clamp((peak - hl_lim) / 0.14f, 0.0f, 1.0f);
            diff *= (1.0f - t) * (1.0f - t);
        }
        if (b == 0) dR_1x[Y * pc.W1 + X] = diff;
        else        dB_1x[Y * pc.W1 + X] = diff;
    }
}

struct PC_V3VstFwd {
    int H_out, W_out, full_H_out, origin_out_y;
    float u_min, u_max, inv_du, pad0;
};

kernel void k_v3_vst_fwd(
    device const float* green_in [[buffer(0)]],
    device const float* lut [[buffer(1)]],
    device float* z_out [[buffer(2)]],
    constant PC_V3VstFwd& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x), y = int(gid.y);
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

struct PC_V3FftSure {
    int H_out, W_out, Hh, Wh, eta_h, eta_w, eta_off, color;
    int ntr_c, ntc_c, phase_y, phase_x, inv_out_s, eta_shift;
    float inv_mean_eta, oct_mul0, oct_mul1, oct_mul2, oct_mul3;
    int nr_t, nc_t;
};

kernel void k_v3_fft64_sure(
    device const float* z_in [[buffer(0)]],
    device const float* lut [[buffer(1)]],
    device const float* snr_boost [[buffer(2)]],
    device const float* fcomp_eta [[buffer(3)]],
    device float* den_out [[buffer(4)]],
    device float* dv_tile_out [[buffer(5)]],
    constant PC_V3FftSure& pc [[buffer(6)]],
    uint tid_u [[thread_position_in_threadgroup]],
    uint wgid_u [[threadgroup_position_in_grid]]
) {
    threadgroup float s_re[64][33];
    threadgroup float s_im[64][33];
    threadgroup float s_tmp[256];
    threadgroup float s_oct[4];
    threadgroup float s_goct[4];
    threadgroup atomic_uint s_hist[128];

    int tid = int(tid_u);
    int wgid = int(wgid_u);
    if (wgid >= pc.ntr_c * pc.ntc_c) return;

    int tr = (wgid / pc.ntc_c) * 2 + (pc.color >> 1);
    int tc = (wgid % pc.ntc_c) * 2 + (pc.color & 1);
    int py = tr * 32 - 32 - pc.phase_y;
    int px = tc * 32 - 32 - pc.phase_x;

    int kOffPsi = 12416, kOffGFloor = 16512, kOffOctF = 20608;
    int kOffTwCos = 24704, kOffTwSin = 24768, kOffSinWin = 24832, kOffOctCnt = 24896;

    float local_wz = 0.0f;
    float lv0[8], lv1[8], lw0[8], lw1[8];
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int ry = v3_reflect_coord(py + r, pc.H_out);
        int rx0 = v3_reflect_coord(px + 2 * c, pc.W_out);
        int rx1 = v3_reflect_coord(px + 2 * c + 1, pc.W_out);
        float wy = lut[kOffSinWin + r];
        float w0 = wy * lut[kOffSinWin + 2 * c];
        float w1 = wy * lut[kOffSinWin + 2 * c + 1];
        float v0 = z_in[ry * pc.W_out + rx0];
        float v1 = z_in[ry * pc.W_out + rx1];
        lv0[it] = v0; lv1[it] = v1; lw0[it] = w0; lw1[it] = w1;
        local_wz += w0 * w0 * v0 + w1 * w1 * v1;
    }
    s_tmp[tid] = local_wz;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float mu_p = s_tmp[0] * (1.0f / 1024.0f);
    if (tid < 128) atomic_store_explicit(&s_hist[tid], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int cr = int(reverse_bits(uint(c)) >> 27);
        s_re[r][cr] = lw0[it] * (lv0[it] - mu_p);
        s_im[r][cr] = lw1[it] * (lv1[it] - mu_p);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

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
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - trv; s_im[r][i1] = m0 - tiv;
            s_re[r][i0] = r0 + trv; s_im[r][i0] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    {
        int r = tid >> 2;
        int sub = tid & 3;
        int ks[4];
        float2 fa_arr[4], fb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float ar = s_re[r][k], ai = s_im[r][k];
                float pr = s_re[r][kc], pi = s_im[r][kc];
                float2 E = 0.5f * float2(ar + pr, ai - pi);
                float2 O = 0.5f * float2(ai + pi, pr - ar);
                float wr = lut[kOffTwCos + k], wi = -lut[kOffTwSin + k];
                fa_arr[p] = E + float2(wr * O.x - wi * O.y, wr * O.y + wi * O.x);
                float2 E2 = float2(E.x, -E.y);
                float2 O2 = float2(O.x, -O.y);
                float wr2 = lut[kOffTwCos + kc], wi2 = -lut[kOffTwSin + kc];
                fb_arr[p] = E2 + float2(wr2 * O2.x - wi2 * O2.y, wr2 * O2.y + wi2 * O2.x);
            }
        }
        float p0r = 0.0f, p0i = 0.0f, p16r = 0.0f, p16i = 0.0f;
        if (sub == 0) {
            p0r = s_re[r][0] + s_im[r][0];
            p0i = s_re[r][0] - s_im[r][0];
            p16r = s_re[r][16];
            p16i = -s_im[r][16];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
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
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int col = k & 31;
        int jr = int(reverse_bits(uint(r)) >> 26);
        if (r < jr) {
            float trv = s_re[r][col], tiv = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = trv; s_im[jr][col] = tiv;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
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
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - trv; s_im[i1][col] = m0 - tiv;
            s_re[i0][col] = r0 + trv; s_im[i0][col] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    {
        float zax = 0.0f, zay = 0.0f, zbx = 0.0f, zby = 0.0f;
        if (tid < 64) {
            int kc = (64 - tid) & 63;
            zax = s_re[tid][0]; zay = s_im[tid][0];
            zbx = s_re[kc][0]; zby = s_im[kc][0];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (tid < 64) {
            s_re[tid][0]  = 0.5f * (zax + zbx);
            s_im[tid][0]  = 0.5f * (zay - zby);
            s_re[tid][32] = 0.5f * (zay + zby);
            s_im[tid][32] = 0.5f * (zbx - zax);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    int cy1 = clamp((py + 32) >> (1 + pc.inv_out_s), 0, max(0, pc.Hh - 1));
    int cx1 = clamp((px + 32) >> (1 + pc.inv_out_s), 0, max(0, pc.Wh - 1));
    float boost = snr_boost[cy1 * max(1, pc.Wh) + cx1];
    float eta_ratio = 1.0f;
    if (pc.eta_w > 0 && pc.eta_h > 0) {
        int ey = clamp((py + 32) >> pc.eta_shift, 0, pc.eta_h - 1);
        int ex = clamp((px + 32) >> pc.eta_shift, 0, pc.eta_w - 1);
        float el = fcomp_eta[pc.eta_off + ey * pc.eta_w + ex];
        if (el >= 0.0f) eta_ratio = clamp(el * pc.inv_mean_eta, 0.25f, 3.0f);
    }
    float nu_ana = 1024.0f * eta_ratio * boost * boost;
    float hist_lo = 0.5f * nu_ana * 0.6931472f;
    float hist_span = max(1e-12f, 1.5f * nu_ana * 0.6931472f);
    float hist_inv_w = 127.0f / hist_span;

    float loc_ew0 = 0.0f, loc_ew1 = 0.0f, loc_ew2 = 0.0f, loc_ew3 = 0.0f;
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33;
        int kx = kh % 33;
        if (ky == 0 && kx == 0) continue;
        float w_mult = (kx == 0 || kx == 32) ? 1.0f : 2.0f;
        int idx = ky * 64 + kx;
        float psi_k = lut[kOffPsi + idx];
        int o = int(lut[kOffOctF + idx] + 0.5f);
        float p = s_re[ky][kx] * s_re[ky][kx] + s_im[ky][kx] * s_im[ky][kx];
        float pw = p / psi_k;
        float wpw = w_mult * pw;
        if (o == 0) loc_ew0 += wpw;
        else if (o == 1) loc_ew1 += wpw;
        else if (o == 2) loc_ew2 += wpw;
        else loc_ew3 += wpw;

        if (o == 3) {
            int hb = clamp(int(floor((pw - hist_lo) * hist_inv_w)), 0, 127);
            if ((idx & 3) == 0) atomic_fetch_add_explicit(&s_hist[hb], 1u, memory_order_relaxed);
            if (kx >= 1 && kx <= 31) {
                int ky_m = (64 - ky) & 63;
                int idx_m = ky_m * 64 + (64 - kx);
                if ((idx_m & 3) == 0) atomic_fetch_add_explicit(&s_hist[hb], 1u, memory_order_relaxed);
            }
        }
    }
    for (int o = 0; o < 4; ++o) {
        s_tmp[tid] = (o == 0) ? loc_ew0 : ((o == 1) ? loc_ew1 : ((o == 2) ? loc_ew2 : loc_ew3));
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int stride = 128; stride > 0; stride >>= 1) {
            if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        if (tid == 0) s_oct[o] = s_tmp[0];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (tid == 0) {
        uint total_h = 0u;
        for (int b = 0; b < 128; ++b) total_h += atomic_load_explicit(&s_hist[b], memory_order_relaxed);
        float nu_emp = nu_ana;
        if (total_h > 0u) {
            uint target = total_h / 2u + 1u;
            uint acc = 0u;
            int b_hit = 64;
            float frac = 0.5f;
            for (int b = 0; b < 128; ++b) {
                uint c = atomic_load_explicit(&s_hist[b], memory_order_relaxed);
                if (acc + c >= target) {
                    b_hit = b;
                    frac = float(target - acc) / max(1.0f, float(c));
                    break;
                }
                acc += c;
            }
            float pw_med = hist_lo + (float(b_hit) + frac) / hist_inv_w;
            nu_emp = clamp(pw_med / 0.6931472f, 0.5f * nu_ana, 2.0f * nu_ana);
        }
        float nu_eff = 0.65f * nu_ana + 0.35f * nu_emp;
        s_tmp[0] = nu_eff;
        float oct_m[4] = {pc.oct_mul0, pc.oct_mul1, pc.oct_mul2, pc.oct_mul3};
        for (int o = 0; o < 4; ++o) {
            float thr_o = nu_eff * oct_m[o];
            float M_dof = max(1.0f, lut[kOffOctCnt + o] - 2.0f);
            s_goct[o] = clamp(1.0f - (M_dof * thr_o) / max(1e-12f, s_oct[o]), 0.0f, 1.0f);
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    float nu_eff = s_tmp[0];
    float oct_m[4] = {pc.oct_mul0, pc.oct_mul1, pc.oct_mul2, pc.oct_mul3};

    if (tid == 0) {
        s_re[0][0] *= s_goct[0];
        s_im[0][0] *= s_goct[0];
    }
    float loc_keep = 0.0f;
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33;
        int kx = kh % 33;
        if (ky == 0 && kx == 0) {
            loc_keep += s_goct[0] * s_goct[0];
            continue;
        }
        int idx = ky * 64 + kx;
        float psi_k = lut[kOffPsi + idx];
        int o = int(lut[kOffOctF + idx] + 0.5f);
        float gf = lut[kOffGFloor + idx];
        float p = s_re[ky][kx] * s_re[ky][kx] + s_im[ky][kx] * s_im[ky][kx];
        float nu_k = nu_eff * oct_m[o] * psi_k;
        float g_spike = max(0.0f, p - 2.4f * nu_k) / (p + 1e-12f);
        float gk = max(gf, max(s_goct[o], g_spike));
        s_re[ky][kx] *= gk;
        s_im[ky][kx] *= gk;
        float w_mult = (kx == 0 || kx == 32) ? 1.0f : 2.0f;
        loc_keep += w_mult * gk * gk * psi_k;
    }
    s_tmp[tid] = loc_keep;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0 && pc.nr_t > 0 && pc.nc_t > 0) {
        float rho_keep = clamp(s_tmp[0] * (1.0f / 4096.0f), 0.0f, 1.0f);
        dv_tile_out[tr * pc.nc_t + tc] = clamp(eta_ratio * (1.0f - rho_keep), 0.0f, eta_ratio);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    if (tid < 64) {
        int ky = tid;
        float c0r = s_re[ky][0], c0i = s_im[ky][0];
        float c32r = s_re[ky][32], c32i = s_im[ky][32];
        s_re[ky][0] = c0r - c32i;
        s_im[ky][0] = c0i + c32r;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int col = k & 31;
        int jr = int(reverse_bits(uint(r)) >> 26);
        if (r < jr) {
            float trv = s_re[r][col], tiv = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = trv; s_im[jr][col] = tiv;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
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
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - trv; s_im[i1][col] = m0 - tiv;
            s_re[i0][col] = r0 + trv; s_im[i0][col] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    {
        int r = tid >> 2;
        int sub = tid & 3;
        int ks[4];
        float2 pa_arr[4], pb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float far = s_re[r][k], fai = s_im[r][k];
                float fbr = s_re[r][kc], fbi = s_im[r][kc];
                float2 E = 0.5f * float2(far + fbr, fai - fbi);
                float2 WO = 0.5f * float2(far - fbr, fai + fbi);
                float wr = lut[kOffTwCos + k], wi = lut[kOffTwSin + k];
                float2 O = float2(wr * WO.x - wi * WO.y, wr * WO.y + wi * WO.x);
                pa_arr[p] = float2(E.x - O.y, E.y + O.x);
                pb_arr[p] = float2(E.x + O.y, O.x - E.y);
            }
        }
        float q0r = 0.0f, q0i = 0.0f, q16r = 0.0f, q16i = 0.0f;
        if (sub == 0) {
            float F0 = s_re[r][0], F32 = s_im[r][0];
            q0r = 0.5f * (F0 + F32);
            q0i = 0.5f * (F0 - F32);
            q16r = s_re[r][16];
            q16i = -s_im[r][16];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
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
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5;
        int c = k & 31;
        int jc = int(reverse_bits(uint(c)) >> 27);
        if (c < jc) {
            float trv = s_re[r][c], tiv = s_im[r][c];
            s_re[r][c] = s_re[r][jc]; s_im[r][c] = s_im[r][jc];
            s_re[r][jc] = trv; s_im[r][jc] = tiv;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
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
            float trv = wr * r1 - wi * m1;
            float tiv = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - trv; s_im[r][i1] = m0 - tiv;
            s_re[r][i0] = r0 + trv; s_im[r][i0] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    float inv_n = 1.0f / 2048.0f;
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

struct PC_V3DvHalf { int H_out, W_out, Hv, Wv; };
kernel void k_v3_dv_half(
    device const float* z_in [[buffer(0)]],
    device const float* den_in [[buffer(1)]],
    device float* dv_out [[buffer(2)]],
    constant PC_V3DvHalf& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x), y = int(gid.y);
    if (x >= pc.Wv || y >= pc.Hv) return;
    float s = 0.0f;
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
    dv_out[y * pc.Wv + x] = min((cnt > 0) ? (s / float(cnt)) : 0.0f, 3.0f);
}

struct PC_V3VstInv {
    int H_out, W_out, Hv, Wv, full_H_out, origin_out_y;
    float u_min, u_max, inv_du, pad0;
    int phase_y, phase_x;
};
kernel void k_v3_vst_inv(
    device const float* den_in [[buffer(0)]],
    device const float* z_in [[buffer(1)]],
    device const float* dv_tile [[buffer(2)]],
    device const float* lut [[buffer(3)]],
    device float* green_out [[buffer(4)]],
    constant PC_V3VstInv& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x), y = int(gid.y);
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

    float lo = 1e30f, hi = -1e30f;
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
        float ty = clamp(float(y + pc.phase_y) * (1.0f / 32.0f), 0.0f, float(pc.Hv - 1));
        float tx = clamp(float(x + pc.phase_x) * (1.0f / 32.0f), 0.0f, float(pc.Wv - 1));
        int ty0 = int(ty), ty1 = min(pc.Hv - 1, ty0 + 1);
        int tx0 = int(tx), tx1 = min(pc.Wv - 1, tx0 + 1);
        float fty = ty - float(ty0), ftx = tx - float(tx0);
        float d0 = mix(dv_tile[ty0 * pc.Wv + tx0], dv_tile[ty0 * pc.Wv + tx1], ftx);
        float d1 = mix(dv_tile[ty1 * pc.Wv + tx0], dv_tile[ty1 * pc.Wv + tx1], ftx);
        float dv_z = mix(d0, d1, fty);
        u_est += 0.25f * ds2 * dv_z;
    }
    green_out[i] = u_est;
}

struct PC_V3ImpClamp { uint count; float imp_thr, pad0, pad1; };
kernel void k_v3_impulse_clamp(
    device float* dR [[buffer(0)]],
    device float* dB [[buffer(1)]],
    device const float* mR [[buffer(2)]],
    device const float* mB [[buffer(3)]],
    constant PC_V3ImpClamp& pc [[buffer(6)]],
    uint i [[thread_position_in_grid]]
) {
    if (i >= pc.count) return;
    float vr = dR[i], mr = mR[i];
    if (abs(vr - mr) > pc.imp_thr) dR[i] = mr;
    float vb = dB[i], mb = mB[i];
    if (abs(vb - mb) > pc.imp_thr) dB[i] = mb;
}

struct PC_V3Down2 { int Hs, Ws, Hd, Wd; };
kernel void k_v3_down2(
    device const float* src [[buffer(0)]],
    device float* dst [[buffer(1)]],
    constant PC_V3Down2& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int xd = int(gid.x), yd = int(gid.y);
    if (xd >= pc.Wd || yd >= pc.Hd) return;
    const float kw[5] = {1.0f/16.0f, 4.0f/16.0f, 6.0f/16.0f, 4.0f/16.0f, 1.0f/16.0f};
    int yc = min(pc.Hs - 1, 2 * yd);
    int xc = min(pc.Ws - 1, 2 * xd);
    int rx[5];
    for (int kx = 0; kx < 5; ++kx) rx[kx] = v3_reflect_coord(xc + kx - 2, pc.Ws);
    float acc = 0.0f;
    for (int ky = 0; ky < 5; ++ky) {
        int ry = v3_reflect_coord(yc + ky - 2, pc.Hs);
        int roff = ry * pc.Ws;
        float row_s = 0.0f;
        for (int kx = 0; kx < 5; ++kx) row_s += kw[kx] * src[roff + rx[kx]];
        acc += kw[ky] * row_s;
    }
    dst[yd * pc.Wd + xd] = acc;
}

struct PC_V3Up2Lap { int Hs, Ws, Hd, Wd, mode; };
kernel void k_v3_up2_lap(
    device const float* cur [[buffer(0)]],
    device const float* coarse [[buffer(1)]],
    device float* out_buf [[buffer(2)]],
    constant PC_V3Up2Lap& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x), y = int(gid.y);
    if (x >= pc.Ws || y >= pc.Hs) return;
    int i = y * pc.Ws + x;
    if (pc.mode == 2) {
        out_buf[i] = cur[(2 * y) * (2 * pc.Ws) + (2 * x)];
        return;
    }
    float ys = clamp(0.5f * float(y), 0.0f, float(max(0, pc.Hd - 1)));
    float xs = clamp(0.5f * float(x), 0.0f, float(max(0, pc.Wd - 1)));
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

struct PC_V3OddHuber { int H, W; float delta, inv_2eg, thr_z; };
kernel void k_v3_odd_huber(
    device const float* src [[buffer(0)]],
    device const float* guide_g [[buffer(1)]],
    device float* dst [[buffer(2)]],
    constant PC_V3OddHuber& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x), y = int(gid.y);
    if (x >= pc.W || y >= pc.H) return;
    int i = y * pc.W + x;
    float dc = src[i];
    if (pc.H < 2 || pc.W < 2) {
        dst[i] = dc;
        return;
    }
    const float kw[9] = {
        0.075f, 0.125f, 0.075f,
        0.125f, 0.200f, 0.125f,
        0.075f, 0.125f, 0.075f
    };
    int ys[3] = {max(0, y - 1), y, min(pc.H - 1, y + 1)};
    int xs[3] = {max(0, x - 1), x, min(pc.W - 1, x + 1)};
    float gc = guide_g[i];
    float wsum = 0.200f, step = 0.0f;
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
    float out_v = dc + 0.65f * (step / max(1e-12f, wsum));
    if (pc.thr_z > 0.0f) {
        float mag = abs(out_v);
        out_v = (mag <= pc.thr_z)
            ? (out_v * (mag / pc.thr_z) * 0.25f)
            : (out_v - ((out_v >= 0.0f) ? 1.0f : -1.0f) * (0.75f * pc.thr_z));
    }
    dst[i] = out_v;
}

struct PC_V3Assemble {
    int H1, W1, H_out, W_out, out_scale;
    float inv_wb_r, inv_wb_b, inv_2sig_g2;
};
kernel void k_v3_assemble_rgb(
    device const float* green_in [[buffer(0)]],
    device const float* dR_1x [[buffer(1)]],
    device const float* dB_1x [[buffer(2)]],
    device float* outR [[buffer(3)]],
    device float* outG [[buffer(4)]],
    device float* outB [[buffer(5)]],
    constant PC_V3Assemble& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int x = int(gid.x), y = int(gid.y);
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
        float wy0 = ((y & 1) != 0) ? 0.5f : 1.0f;
        float wy1 = ((y & 1) != 0) ? 0.5f : 0.0f;
        float wx0 = ((x & 1) != 0) ? 0.5f : 1.0f;
        float wx1 = ((x & 1) != 0) ? 0.5f : 0.0f;
        float dg00 = green_in[(2 * ry0) * pc.W_out + 2 * cx0] - gv;
        float dg01 = green_in[(2 * ry0) * pc.W_out + 2 * cx1] - gv;
        float dg10 = green_in[(2 * ry1) * pc.W_out + 2 * cx0] - gv;
        float dg11 = green_in[(2 * ry1) * pc.W_out + 2 * cx1] - gv;
        float w00 = wy0 * wx0 * (fast_exp_neg(dg00 * dg00 * pc.inv_2sig_g2) + 1e-4f);
        float w01 = wy0 * wx1 * (fast_exp_neg(dg01 * dg01 * pc.inv_2sig_g2) + 1e-4f);
        float w10 = wy1 * wx0 * (fast_exp_neg(dg10 * dg10 * pc.inv_2sig_g2) + 1e-4f);
        float w11 = wy1 * wx1 * (fast_exp_neg(dg11 * dg11 * pc.inv_2sig_g2) + 1e-4f);
        float inv_ws = 1.0f / max(1e-12f, w00 + w01 + w10 + w11);
        int i00 = ry0 * pc.W1 + cx0, i01 = ry0 * pc.W1 + cx1;
        int i10 = ry1 * pc.W1 + cx0, i11 = ry1 * pc.W1 + cx1;
        dr = (w00 * dR_1x[i00] + w01 * dR_1x[i01] + w10 * dR_1x[i10] + w11 * dR_1x[i11]) * inv_ws;
        db = (w00 * dB_1x[i00] + w01 * dB_1x[i01] + w10 * dB_1x[i10] + w11 * dB_1x[i11]) * inv_ws;
    }
    outR[i] = (gv + dr) * pc.inv_wb_r;
    outG[i] = gv;
    outB[i] = (gv + db) * pc.inv_wb_b;
}

struct PC_M5aZeroU32 { uint count; };
kernel void k_m5a_zero_u32(
    device uint* dst [[buffer(0)]],
    constant PC_M5aZeroU32& pc [[buffer(6)]],
    uint i [[thread_position_in_grid]]
) {
    if (i < pc.count) dst[i] = 0u;
}

struct PC_M5aScatter {
    int H1, W1, Hh, Wh;
    int H_out, W_out, out_scale, tile;
    int nr, nc, nr_band, tr_b0;
    int band_y0, band_h, rgb_in_y0, rgb_in_h;
    int sy0, sy_cnt, hy0, hy_cnt;
    int frame_n, is_it0, zero_gm_only, pad0;
    float v_scale, quad, q_scale, pad2;
};

static inline float4 m5a_sample_fm(device const float4* flow_mw_slice, int Hh, int Wh, int hy0, int hy_cnt, float y, float x) {
    int hy = clamp(int(floor(y)), 0, Hh - 2);
    int hx = clamp(int(floor(x)), 0, Wh - 2);
    int hy1 = min(hy + 1, Hh - 1);
    int hx1 = min(hx + 1, Wh - 1);
    float fy = clamp(y - float(hy), 0.0f, 1.0f);
    float fx = clamp(x - float(hx), 0.0f, 1.0f);
    int sy_0 = clamp(hy - hy0, 0, hy_cnt - 1);
    int sy_1 = clamp(hy1 - hy0, 0, hy_cnt - 1);
    float4 v00 = flow_mw_slice[sy_0 * Wh + hx];
    float4 v01 = flow_mw_slice[sy_0 * Wh + hx1];
    float4 v10 = flow_mw_slice[sy_1 * Wh + hx];
    float4 v11 = flow_mw_slice[sy_1 * Wh + hx1];
    return mix(mix(v00, v01, fx), mix(v10, v11, fx), fy);
}

static inline float m5a_fast_erf(float x) {
    float ax = abs(x);
    float t = 1.0f / (1.0f + 0.3275911f * ax);
    float poly = (((((1.061405429f * t - 1.453152027f) * t) + 1.421413741f) * t - 0.284496736f) * t + 0.254829592f) * t;
    float val = 1.0f - poly * exp(-ax * ax);
    return (x >= 0.0f) ? val : -val;
}

static inline void m5a_a4_weights_6tap(float coord_2x, float inv_blur, thread int& out_f0, thread float w[6]) {
    int i0 = int(floor(coord_2x));
    out_f0 = i0 - 2;
    float u = coord_2x - float(i0);
    float ib = max(0.35f, inv_blur);
    float sig = max(0.32f, 0.90f * sqrt(max(0.0f, 1.0f / (ib * ib) - 0.75f)));
    float inv_s2 = 0.70710678f / sig;
    float w0[6], ts[6];
    float s0 = 0.0f;
    for (int d = 0; d < 6; ++d) {
        float t = float(d - 2) - u;
        ts[d] = t;
        float b = 0.5f * (m5a_fast_erf((t + 1.0f) * inv_s2) - m5a_fast_erf((t - 1.0f) * inv_s2));
        float at = abs(t);
        float win = 1.0f;
        if (at > 2.0f) {
            float st = min(1.0f, at - 2.0f);
            win = 1.0f - st * st * (3.0f - 2.0f * st);
        }
        w0[d] = max(0.0f, b * win);
        s0 += w0[d];
    }
    float inv_sum0 = 1.0f / max(1e-12f, s0);
    float m1 = 0.0f, m2 = 0.0f;
    for (int d = 0; d < 6; ++d) {
        w0[d] *= inv_sum0;
        m1 += w0[d] * ts[d];
        m2 += w0[d] * ts[d] * ts[d];
    }
    float alpha = m1 / max(1e-12f, m2);
    float sw = 0.0f;
    for (int d = 0; d < 6; ++d) {
        w[d] = max(0.0f, w0[d] * (1.0f - alpha * ts[d]));
        sw += w[d];
    }
    float inv_sw = 1.0f / max(1e-12f, sw);
    for (int d = 0; d < 6; ++d) w[d] *= inv_sw;
}

kernel void k_m5a_scatter(
    device const float* raw_slice [[buffer(0)]],
    device const float4* flow_mw_slice [[buffer(1)]],
    device const float* rgb_band_in [[buffer(2)]],
    device atomic_int* gm_i32 [[buffer(3)]],
    device atomic_uint* tile_stats [[buffer(4)]],
    device const float* params [[buffer(5)]],
    constant PC_M5aScatter& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x);
    int Y = int(gid.y);
    if (c >= pc.W1 || Y >= pc.sy_cnt) return;
    int r = pc.sy0 + Y;

    int fp_off = 48 + pc.frame_n * 16;
    float rv_n = params[fp_off + 0];
    float sh_n = params[fp_off + 1];
    float sat_n = params[fp_off + 2];
    bool all_valid = (params[fp_off + 3] > 0.5f);
    bool unit_mw = (params[fp_off + 4] > 0.5f);
    bool has_flow = (params[fp_off + 5] > 0.5f);
    bool const_shift = (params[fp_off + 6] > 0.5f);
    float shift_y = params[fp_off + 7];
    float shift_x = params[fp_off + 8];
    float inv_blur = params[fp_off + 9];
    float half_tau2 = params[fp_off + 10];
    float ois_r0 = params[fp_off + 11], ois_r1 = params[fp_off + 12];
    float ois_b0 = params[fp_off + 13], ois_b1 = params[fp_off + 14];
    bool has_sat = (params[fp_off + 15] > 0.5f);

    float v = raw_slice[Y * pc.W1 + c];
    if (!all_valid && (isnan(v) || isinf(v) || (has_sat && v >= sat_n))) return;

    float yb = float(r), xb = float(c);
    if (const_shift) {
        yb -= shift_y;
        xb -= shift_x;
    } else if (has_flow) {
        for (int k = 0; k < 5; ++k) {
            float y = clamp(yb * 0.5f, 0.0f, float(pc.Hh - 1));
            float x = clamp(xb * 0.5f, 0.0f, float(pc.Wh - 1));
            float4 fm = m5a_sample_fm(flow_mw_slice, pc.Hh, pc.Wh, pc.hy0, pc.hy_cnt, y, x);
            float ny = fma(-2.0f, fm.x, float(r));
            float nx = fma(-2.0f, fm.y, float(c));
            bool settled = (abs(ny - yb) <= 1e-7f && abs(nx - xb) <= 1e-7f);
            yb = ny; xb = nx;
            if (settled) break;
        }
    }
    if (isnan(yb) || isinf(yb) || isnan(xb) || isinf(xb) ||
        yb < 0.0f || yb > float(pc.H1 - 1) || xb < 0.0f || xb > float(pc.W1 - 1)) return;

    int ch = int(params[8 + (r % 6) * 6 + (c % 6)] + 0.5f);
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
    if (yo < float(pc.band_y0) - 3.0f || yo > float(pc.band_y0 + pc.band_h) + 2.0f) return;

    float gate = (unit_mw || !has_flow) ? 1.0f
        : clamp(m5a_sample_fm(flow_mw_slice, pc.Hh, pc.Wh, pc.hy0, pc.hy_cnt, yb * 0.5f, xb * 0.5f).z, 0.0f, 1.0f);
    if (gate <= 0.0f) return;

    float proj = 0.0f;
    float ky[6], kx[6];
    int fr0 = 0, fc0 = 0;

    if (pc.out_scale == 2) {
        m5a_a4_weights_6tap(yo, inv_blur, fr0, ky);
        m5a_a4_weights_6tap(xo, inv_blur, fc0, kx);
        for (int dr = 0; dr < 6; ++dr) {
            if (ky[dr] <= 0.0f) continue;
            int fr = clamp(fr0 + dr, 0, pc.H_out - 1);
            int lry = clamp(fr - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
            int rbase = lry * pc.W_out * 3 + ch;
            float row_acc = 0.0f;
            for (int dc = 0; dc < 6; ++dc) {
                int fc = clamp(fc0 + dc, 0, pc.W_out - 1);
                row_acc += kx[dc] * rgb_band_in[rbase + fc * 3];
            }
            proj += ky[dr] * row_acc;
        }
    } else {
        float y_c = clamp(yo, 0.0f, float(pc.H_out - 1));
        float x_c = clamp(xo, 0.0f, float(pc.W_out - 1));
        fr0 = int(y_c); fc0 = int(x_c);
        int y1i = min(pc.H_out - 1, fr0 + 1), x1i = min(pc.W_out - 1, fc0 + 1);
        float ty = y_c - float(fr0), tx = x_c - float(fc0);
        ky[0] = 1.0f - ty; ky[1] = ty;
        kx[0] = 1.0f - tx; kx[1] = tx;
        int ly0 = clamp(fr0 - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
        int ly1 = clamp(y1i - pc.rgb_in_y0, 0, pc.rgb_in_h - 1);
        proj = ky[0] * (kx[0] * rgb_band_in[(ly0 * pc.W_out + fc0) * 3 + ch] + kx[1] * rgb_band_in[(ly0 * pc.W_out + x1i) * 3 + ch]) +
               ky[1] * (kx[0] * rgb_band_in[(ly1 * pc.W_out + fc0) * 3 + ch] + kx[1] * rgb_band_in[(ly1 * pc.W_out + x1i) * 3 + ch]);
    }

    float p_pos = max(0.0f, proj);
    float fvar = max(1e-12f, rv_n + p_pos * (sh_n + pc.quad * p_pos));
    float iw = gate / fvar;
    float raw_res = v - proj;
    float z2 = iw * raw_res * raw_res;

    uint q_tile = uint(clamp(z2 * 256.0f, 0.0f, 4.0e8f));
    uint q_sub  = uint(clamp(z2 * 16.0f, 0.0f, 65535.0f));

    int tr0_chk = max(pc.tr_b0, int(floor((yo - 2.5f) / float(pc.tile))) - 1);
    int tr1_chk = min(pc.tr_b0 + pc.nr_band - 1, int(floor((yo + 3.5f) / float(pc.tile))) + 1);
    int tc0_chk = max(0, int(floor((xo - 2.5f) / float(pc.tile))) - 1);
    int tc1_chk = min(pc.nc - 1, int(floor((xo + 3.5f) / float(pc.tile))) + 1);
    for (int tr = tr0_chk; tr <= tr1_chk; ++tr) {
        int r0 = tr * pc.tile;
        int th = min(pc.tile, pc.H_out - r0);
        if (yo < float(r0) - 3.0f || yo > float(r0 + th) + 2.0f) continue;
        int sy_c = clamp(((int(yo) - r0) * 8) / max(1, th), 0, 7);
        for (int tc = tc0_chk; tc <= tc1_chk; ++tc) {
            int c0 = tc * pc.tile;
            int tw = min(pc.tile, pc.W_out - c0);
            if (xo < float(c0) - 3.0f || xo > float(c0 + tw) + 2.0f) continue;
            int sx_c = clamp(((int(xo) - c0) * 8) / max(1, tw), 0, 7);
            int sb = (sy_c << 3) | sx_c;
            int tb = ((tr - pc.tr_b0) * pc.nc + tc) * 136;
            uint old_lo = atomic_fetch_add_explicit(&tile_stats[tb + 0], q_tile, memory_order_relaxed);
            if (old_lo > 0xFFFFFFFFu - q_tile) atomic_fetch_add_explicit(&tile_stats[tb + 1], 1u, memory_order_relaxed);
            atomic_fetch_add_explicit(&tile_stats[tb + 2], 1u, memory_order_relaxed);
            atomic_fetch_add_explicit(&tile_stats[tb + 6 + sb], q_sub, memory_order_relaxed);
            atomic_fetch_add_explicit(&tile_stats[tb + 70 + sb], 1u, memory_order_relaxed);
        }
    }

    if (pc.is_it0 != 0) {
        int oy_i = int(floor(yo));
        int ox_i = int(floor(xo));
        int own_tr = (oy_i <= 0) ? 0 : min(pc.nr - 1, oy_i / pc.tile);
        int own_tc = (ox_i <= 0) ? 0 : min(pc.nc - 1, ox_i / pc.tile);
        if (own_tr >= pc.tr_b0 && own_tr < pc.tr_b0 + pc.nr_band) {
            int tb_o = ((own_tr - pc.tr_b0) * pc.nc + own_tc) * 136;
            uint old_lo = atomic_fetch_add_explicit(&tile_stats[tb_o + 3], q_tile, memory_order_relaxed);
            if (old_lo > 0xFFFFFFFFu - q_tile) atomic_fetch_add_explicit(&tile_stats[tb_o + 4], 1u, memory_order_relaxed);
            atomic_fetch_add_explicit(&tile_stats[tb_o + 5], 1u, memory_order_relaxed);
        }
    }

    if (pc.zero_gm_only != 0) return;

    float res = raw_res;
    if (half_tau2 > 0.0f) res *= (res * res) / (res * res + half_tau2);
    float w_tilde = clamp(iw * pc.v_scale, 0.0f, 64.0f);

    if (pc.out_scale == 2) {
        for (int dr = 0; dr < 6; ++dr) {
            if (ky[dr] <= 0.0f) continue;
            int fr = clamp(fr0 + dr, 0, pc.H_out - 1);
            if (fr < pc.band_y0 || fr >= pc.band_y0 + pc.band_h) continue;
            int ro = (fr - pc.band_y0) * pc.W_out;
            for (int dc = 0; dc < 6; ++dc) {
                if (kx[dc] <= 0.0f) continue;
                int fc = clamp(fc0 + dc, 0, pc.W_out - 1);
                float wk = ky[dr] * kx[dc];
                int dg = int(round(wk * clamp(w_tilde * res, -16.0f, 16.0f) * (262144.0f * pc.q_scale)));
                int dm = int(round(wk * w_tilde * (16384.0f * pc.q_scale)));
                int pi = (ro + fc) * 6;
                if (dg != 0) atomic_fetch_add_explicit(&gm_i32[pi + ch], dg, memory_order_relaxed);
                if (dm != 0) atomic_fetch_add_explicit(&gm_i32[pi + 3 + ch], dm, memory_order_relaxed);
            }
        }
    } else {
        int frs[2] = {fr0, min(pc.H_out - 1, fr0 + 1)};
        int fcs[2] = {fc0, min(pc.W_out - 1, fc0 + 1)};
        for (int dr = 0; dr < 2; ++dr) {
            int fr = frs[dr];
            if (fr < pc.band_y0 || fr >= pc.band_y0 + pc.band_h || ky[dr] <= 0.0f) continue;
            int ro = (fr - pc.band_y0) * pc.W_out;
            for (int dc = 0; dc < 2; ++dc) {
                int fc = fcs[dc];
                float wk = ky[dr] * kx[dc];
                if (wk <= 0.0f) continue;
                int dg = int(round(wk * clamp(w_tilde * res, -16.0f, 16.0f) * (262144.0f * pc.q_scale)));
                int dm = int(round(wk * w_tilde * (16384.0f * pc.q_scale)));
                int pi = (ro + fc) * 6;
                if (dg != 0) atomic_fetch_add_explicit(&gm_i32[pi + ch], dg, memory_order_relaxed);
                if (dm != 0) atomic_fetch_add_explicit(&gm_i32[pi + 3 + ch], dm, memory_order_relaxed);
            }
        }
    }
}

struct PC_M5aUpdate {
    int H_out, W_out, out_scale, tile;
    int nc, nr_band, tr_b0, band_y0;
    int band_h, rgb_in_y0, wg_x;
    float q_scale;
    float tau_morozov, v_scale, rv_base, sh_base;
    float quad, k_pi_g, k_pi_c_coarse, k_pi_c;
    float sat0, wb_r, wb_b;
};

kernel void k_m5a_update(
    device const float* rgb_band_in [[buffer(0)]],
    device const int* gm_i32 [[buffer(1)]],
    device const uint* tile_stats [[buffer(2)]],
    device float* rgb_band_out [[buffer(3)]],
    device float* wg_stats [[buffer(4)]],
    constant PC_M5aUpdate& pc [[buffer(6)]],
    uint2 tgp [[threadgroup_position_in_grid]],
    uint2 tid2 [[thread_position_in_threadgroup]],
    uint tid [[thread_index_in_threadgroup]],
    uint2 gid [[thread_position_in_grid]]
) {
    threadgroup float s_crg[18][18], s_cbg[18][18];
    threadgroup float s_crn[18][18], s_crm[18][18], s_cbn[18][18], s_cbm[18][18];
    threadgroup float s_gv[20][20], s_g1[20][20], s_m1[20][20];
    threadgroup float s_rms[256], s_chg[256];

    int wgx = int(tgp.x), wgy = int(tgp.y);
    int gx = int(gid.x), gy = int(gid.y);
    int wx_base = wgx * 16, wy_base = wgy * 16;
    float inv_g = 1.0f / (262144.0f * pc.v_scale * pc.q_scale);
    float inv_m = 1.0f / (16384.0f * pc.v_scale * pc.q_scale);
    float wbr = max(0.25f, pc.wb_r), wbb = max(0.25f, pc.wb_b);
    float inv_wbr = 1.0f / wbr, inv_wbb = 1.0f / wbb;
    float inv_wbr2 = inv_wbr * inv_wbr, inv_wbb2 = inv_wbb * inv_wbb;
    auto noise_var = [&](float v) -> float {
        float s = max(0.0f, v);
        return max(1e-10f, pc.rv_base + s * (pc.sh_base + pc.quad * s));
    };

    for (int idx = int(tid); idx < 324; idx += 256) {
        int sy = idx / 18, sx = idx % 18;
        int ny = clamp(wy_base + sy - 1, 0, pc.band_h - 1);
        int nx = clamp(wx_base + sx - 1, 0, pc.W_out - 1);
        int in_off = ((ny + pc.band_y0 - pc.rgb_in_y0) * pc.W_out + nx) * 3;
        float r_v = rgb_band_in[in_off + 0], g_v = rgb_band_in[in_off + 1], b_v = rgb_band_in[in_off + 2];
        s_crg[sy][sx] = wbr * r_v - g_v;
        s_cbg[sy][sx] = wbb * b_v - g_v;
        int p6 = (ny * pc.W_out + nx) * 6;
        float g0 = float(gm_i32[p6 + 0]) * inv_g * inv_wbr, g1 = float(gm_i32[p6 + 1]) * inv_g, g2 = float(gm_i32[p6 + 2]) * inv_g * inv_wbb;
        float m0 = float(gm_i32[p6 + 3]) * inv_m * inv_wbr2, m1 = float(gm_i32[p6 + 4]) * inv_m, m2 = float(gm_i32[p6 + 5]) * inv_m * inv_wbb2;
        float mtot = m0 + m1 + m2;
        if (mtot > 0.0f) {
            float pi_g = pc.k_pi_g / noise_var(g_v);
            float dg0 = (g0 + g1 + g2) / (mtot + pi_g);
            s_crn[sy][sx] = g0 - m0 * dg0; s_crm[sy][sx] = m0;
            s_cbn[sy][sx] = g2 - m2 * dg0; s_cbm[sy][sx] = m2;
        } else {
            s_crn[sy][sx] = 0.0f; s_crm[sy][sx] = 0.0f;
            s_cbn[sy][sx] = 0.0f; s_cbm[sy][sx] = 0.0f;
        }
    }
    if (pc.out_scale > 1) {
        for (int idx = int(tid); idx < 400; idx += 256) {
            int sy = idx / 20, sx = idx % 20;
            int ny = clamp(wy_base + sy - 2, 0, pc.band_h - 1);
            int nx = clamp(wx_base + sx - 2, 0, pc.W_out - 1);
            int in_off = ((ny + pc.band_y0 - pc.rgb_in_y0) * pc.W_out + nx) * 3;
            s_gv[sy][sx] = rgb_band_in[in_off + 1];
            int p6 = (ny * pc.W_out + nx) * 6;
            s_g1[sy][sx] = float(gm_i32[p6 + 1]) * inv_g;
            s_m1[sy][sx] = float(gm_i32[p6 + 4]) * inv_m;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    float pix_rms = 0.0f, pix_chg = 0.0f;
    if (gx < pc.W_out && gy < pc.band_h) {
        int tr_loc = gy / pc.tile;
        int tc = gx / pc.tile;
        int tb = (tr_loc * pc.nc + tc) * 136;
        uint t_cnt = tile_stats[tb + 2];
        float t_chi2 = 0.0f;
        if (t_cnt > 0u) {
            float tot = float(tile_stats[tb + 1]) * 4294967296.0f + float(tile_stats[tb + 0]);
            t_chi2 = (tot / 256.0f) / float(t_cnt);
        }
        float max_sub = 0.0f;
        for (int sb = 0; sb < 64; ++sb) {
            uint sc = tile_stats[tb + 70 + sb];
            if (sc >= 32u) max_sub = max(max_sub, (float(tile_stats[tb + 6 + sb]) / 16.0f) / float(sc));
        }
        float eff_chi2 = max(t_chi2, 0.80f * max_sub);
        int in_off = ((gy + pc.band_y0 - pc.rgb_in_y0) * pc.W_out + gx) * 3;
        int out_off = (gy * pc.W_out + gx) * 3;
        float cur_r = rgb_band_in[in_off + 0], cur_g = rgb_band_in[in_off + 1], cur_b = rgb_band_in[in_off + 2];
        int p6 = (gy * pc.W_out + gx) * 6;
        float g0 = float(gm_i32[p6 + 0]) * inv_g * inv_wbr, g1 = float(gm_i32[p6 + 1]) * inv_g, g2 = float(gm_i32[p6 + 2]) * inv_g * inv_wbb;
        float m0 = float(gm_i32[p6 + 3]) * inv_m * inv_wbr2, m1 = float(gm_i32[p6 + 4]) * inv_m, m2 = float(gm_i32[p6 + 5]) * inv_m * inv_wbb2;

        if (eff_chi2 <= pc.tau_morozov || (m0 + m1 + m2) <= 0.0f) {
            rgb_band_out[out_off + 0] = cur_r;
            rgb_band_out[out_off + 1] = cur_g;
            rgb_band_out[out_off + 2] = cur_b;
        } else {
            float tile_gain = clamp(0.88f * (eff_chi2 - pc.tau_morozov) / max(0.25f, eff_chi2), 0.25f, 0.88f);
            int sy_lo = max(0, tr_loc * pc.tile - wy_base + 1);
            int sy_hi = min(17, min(pc.band_h, (tr_loc + 1) * pc.tile) - wy_base);
            int sx_lo = max(0, tc * pc.tile - wx_base + 1);
            int sx_hi = min(17, min(pc.W_out, (tc + 1) * pc.tile) - wx_base);
            int lly = int(tid2.y) + 1, llx = int(tid2.x) + 1;
            int ys[3] = {max(sy_lo, lly - 1), lly, min(sy_hi, lly + 1)};
            int xs[3] = {max(sx_lo, llx - 1), llx, min(sx_hi, llx + 1)};
            const float wy3[3] = {1.0f, 2.0f, 1.0f};

            float nv = noise_var(cur_g);
            float thr9 = 9.0f * nv;
            float c_rg = wbr * cur_r - cur_g, c_bg = wbb * cur_b - cur_g;
            float srn = 0.0f, srm = 0.0f, sbn = 0.0f, sbm = 0.0f;
            for (int dy = 0; dy < 3; ++dy) {
                for (int dx = 0; dx < 3; ++dx) {
                    float kw = wy3[dy] * wy3[dx];
                    int yy = ys[dy], xx = xs[dx];
                    float dcr = s_crg[yy][xx] - c_rg, dcb = s_cbg[yy][xx] - c_bg;
                    float wcr = kw * ((dcr * dcr < thr9) ? 1.0f : (thr9 / (dcr * dcr + 1e-12f)));
                    float wcb = kw * ((dcb * dcb < thr9) ? 1.0f : (thr9 / (dcb * dcb + 1e-12f)));
                    srn += wcr * s_crn[yy][xx]; srm += wcr * s_crm[yy][xx];
                    sbn += wcb * s_cbn[yy][xx]; sbm += wcb * s_cbm[yy][xx];
                }
            }
            float g1_eff = g1, m1_eff = m1, dg_parity = 0.0f, coh_m5 = 0.0f, peak_g = cur_g, edge_g2 = 0.0f;
            if (pc.out_scale > 1) {
                int sy5_lo = max(0, tr_loc * pc.tile - wy_base + 2);
                int sy5_hi = min(19, min(pc.band_h, (tr_loc + 1) * pc.tile) - wy_base + 1);
                int sx5_lo = max(0, tc * pc.tile - wx_base + 2);
                int sx5_hi = min(19, min(pc.W_out, (tc + 1) * pc.tile) - wx_base + 1);
                int lly5 = int(tid2.y) + 2, llx5 = int(tid2.x) + 2;
                int ym1 = max(sy5_lo, lly5 - 1), yp1 = min(sy5_hi, lly5 + 1);
                int xm1 = max(sx5_lo, llx5 - 1), xp1 = min(sx5_hi, llx5 + 1);
                int ys5[5] = {max(sy5_lo, lly5 - 2), ym1, lly5, yp1, min(sy5_hi, lly5 + 2)};
                int xs5[5] = {max(sx5_lo, llx5 - 2), xm1, llx5, xp1, min(sx5_hi, llx5 + 2)};
                const float wy5[5] = {1.0f, 2.0f, 2.0f, 2.0f, 1.0f};
                float row_bar[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                float col_bar[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
                for (int dy5 = 0; dy5 < 5; ++dy5) {
                    int yy5 = ys5[dy5];
                    for (int dx5 = 0; dx5 < 5; ++dx5) {
                        float v5 = s_gv[yy5][xs5[dx5]];
                        peak_g = max(peak_g, v5);
                        row_bar[dy5] += wy5[dx5] * v5;
                        col_bar[dx5] += wy5[dy5] * v5;
                    }
                    row_bar[dy5] *= 0.125f;
                }
                for (int dx5 = 0; dx5 < 5; ++dx5) col_bar[dx5] *= 0.125f;
                float gx1 = 0.5f * (col_bar[3] - col_bar[1]);
                float gy1 = 0.5f * (row_bar[3] - row_bar[1]);
                float gxx = col_bar[1] - 2.0f * col_bar[2] + col_bar[3];
                float gyy = row_bar[1] - 2.0f * row_bar[2] + row_bar[3];
                float gxy = 0.25f * (s_gv[yp1][xp1] - s_gv[yp1][xm1] - s_gv[ym1][xp1] + s_gv[ym1][xm1]);
                float Txx = gx1 * gx1 + 0.5f * (gxx * gxx + gxy * gxy);
                float Tyy = gy1 * gy1 + 0.5f * (gxy * gxy + gyy * gyy);
                float Txy = gx1 * gy1 + 0.5f * (gxx + gyy) * gxy;
                float tr_T = Txx + Tyy, df_T = Txx - Tyy;
                float disc_T = sqrt(max(0.0f, df_T * df_T + 4.0f * Txy * Txy));
                float lam1 = 0.5f * (tr_T + disc_T);
                edge_g2 = gx1 * gx1 + gy1 * gy1;
                float rc0 = 0.5f * (row_bar[0] + row_bar[2]), rc1 = 0.5f * (row_bar[1] + row_bar[3]), rc2 = 0.5f * (row_bar[2] + row_bar[4]);
                float cc0 = 0.5f * (col_bar[0] + col_bar[2]), cc1 = 0.5f * (col_bar[1] + col_bar[3]), cc2 = 0.5f * (col_bar[2] + col_bar[4]);
                float gx_c = 0.5f * (cc2 - cc0), gy_c = 0.5f * (rc2 - rc0);
                float gxx_c = cc0 - 2.0f * cc1 + cc2, gyy_c = rc0 - 2.0f * rc1 + rc2;
                float Txx_c = gx_c * gx_c + 0.5f * gxx_c * gxx_c;
                float Tyy_c = gy_c * gy_c + 0.5f * gyy_c * gyy_c;
                float Txy_c = gx_c * gy_c;
                float disc_c = sqrt(max(0.0f, (Txx_c - Tyy_c) * (Txx_c - Tyy_c) + 4.0f * Txy_c * Txy_c));
                float lam2 = max(0.0f, 0.5f * ((Txx_c + Tyy_c) - disc_c));
                float nx_m = 1.0f, ny_m = 0.0f;
                if (disc_T > 1e-12f) {
                    float vx = (df_T >= 0.0f) ? (0.5f * (df_T + disc_T)) : Txy;
                    float vy = (df_T >= 0.0f) ? Txy : (0.5f * (-df_T + disc_T));
                    float vn = sqrt(max(1e-18f, vx * vx + vy * vy));
                    nx_m = vx / vn; ny_m = vy / vn;
                }
                coh_m5 = clamp((lam1 - lam2) / (lam1 + lam2 + 0.025f * nv + 1e-12f), 0.0f, 1.0f);
                float aniso_pen = 14.0f * max(0.0f, lam1 - lam2);
                float iso_pen = 10.0f * lam2;
                float thr_lg = 0.25f * nv + 0.40f * lam1 + 1e-12f;
                float dg_w = 1.0f - 0.94f * coh_m5;
                float sg1 = 0.0f, sm1 = 0.0f, sg_bar = 0.0f, swlg = 0.0f;
                for (int dy5 = 0; dy5 < 5; ++dy5) {
                    float oy = float(dy5 - 2);
                    int yy5 = ys5[dy5];
                    for (int dx5 = 0; dx5 < 5; ++dx5) {
                        float ox = float(dx5 - 2);
                        float kw5 = wy5[dy5] * wy5[dx5];
                        int xx5 = xs5[dx5];
                        float n_gv = s_gv[yy5][xx5];
                        float dg_l = n_gv - cur_g;
                        float un_m = ox * nx_m + oy * ny_m;
                        float d2_s = un_m * un_m * aniso_pen + (ox * ox + oy * oy) * iso_pen + dg_w * dg_l * dg_l;
                        float wlg = kw5 * ((d2_s < thr_lg) ? 1.0f : (thr_lg / (d2_s + 1e-12f)));
                        sg1 += wlg * s_g1[yy5][xx5];
                        sm1 += wlg * s_m1[yy5][xx5];
                        sg_bar += wlg * n_gv;
                        swlg += wlg;
                    }
                }
                float inv_swlg = 1.0f / max(1e-6f, swlg);
                g1_eff = sg1 * inv_swlg;
                m1_eff = sm1 * inv_swlg;
                dg_parity = 0.98f * coh_m5 * ((sg_bar * inv_swlg) - cur_g);
            } else {
                int gx_lo = tc * pc.tile, gx_hi = min(pc.W_out, (tc + 1) * pc.tile) - 1;
                int gy_lo = tr_loc * pc.tile, gy_hi = min(pc.band_h, (tr_loc + 1) * pc.tile) - 1;
                int x_m = max(gx_lo, gx - 1), x_p = min(gx_hi, gx + 1);
                int ry_m = max(gy_lo, gy - 1) + pc.band_y0 - pc.rgb_in_y0;
                int ry_c = gy + pc.band_y0 - pc.rgb_in_y0;
                int ry_p = min(gy_hi, gy + 1) + pc.band_y0 - pc.rgb_in_y0;
                int rys[3] = {ry_m, ry_c, ry_p};
                int rxs[3] = {x_m, gx, x_p};
                for (int dy3 = 0; dy3 < 3; ++dy3) {
                    int rbase = rys[dy3] * pc.W_out;
                    for (int dx3 = 0; dx3 < 3; ++dx3) {
                        peak_g = max(peak_g, rgb_band_in[(rbase + rxs[dx3]) * 3 + 1]);
                    }
                }
                float gx1 = 0.5f * (rgb_band_in[(ry_c * pc.W_out + x_p) * 3 + 1] - rgb_band_in[(ry_c * pc.W_out + x_m) * 3 + 1]);
                float gy1 = 0.5f * (rgb_band_in[(ry_p * pc.W_out + gx) * 3 + 1] - rgb_band_in[(ry_m * pc.W_out + gx) * 3 + 1]);
                edge_g2 = gx1 * gx1 + gy1 * gy1;
            }
            float edge_c_reg = 1.0f + ((pc.out_scale > 1) ? 0.35f : 0.12f) * (edge_g2 / nv);
            float pi_cc = (pc.k_pi_c_coarse * edge_c_reg) / nv;
            float dCr_bar = srn / (srm + pi_cc);
            float dCb_bar = sbn / (sbm + pi_cc);
            float pi_g = pc.k_pi_g / nv, pi_c = (pc.k_pi_c * edge_c_reg) / nv;
            float ar = pi_c / (m0 + pi_c), ab = pi_c / (m2 + pi_c);
            float gr_t = g0 - m0 * dCr_bar, gb_t = g2 - m2 * dCb_bar;
            float dG = (g1_eff + ar * gr_t + ab * gb_t) / (m1_eff + pi_g + ar * m0 + ab * m2);
            float dCr = dCr_bar + (gr_t - m0 * dG) / (m0 + pi_c);
            float dCb = dCb_bar + (gb_t - m2 * dG) / (m2 + pi_c);
            if (pc.sat0 < 1.0e5f && peak_g > 0.72f * pc.sat0) {
                float th = clamp((peak_g - 0.72f * pc.sat0) / max(1e-5f, 0.16f * pc.sat0), 0.0f, 1.0f);
                float freeze = 1.0f - th * th * (3.0f - 2.0f * th);
                float f2 = freeze * freeze;
                dCr *= f2;
                dCb *= f2;
                dg_parity *= f2;
            }
            float dg_tot = tile_gain * dG + dg_parity;
            float dr = (dg_tot + tile_gain * dCr) * inv_wbr, dg = dg_tot, db = (dg_tot + tile_gain * dCb) * inv_wbb;
            rgb_band_out[out_off + 0] = cur_r + dr;
            rgb_band_out[out_off + 1] = cur_g + dg;
            rgb_band_out[out_off + 2] = cur_b + db;
            pix_rms = dr * dr + dg * dg + db * db;
            if (dr != 0.0f) pix_chg += 1.0f;
            if (dg != 0.0f) pix_chg += 1.0f;
            if (db != 0.0f) pix_chg += 1.0f;
        }
    }
    s_rms[tid] = pix_rms;
    s_chg[tid] = pix_chg;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (int(tid) < stride) {
            s_rms[tid] += s_rms[tid + stride];
            s_chg[tid] += s_chg[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 2;
        wg_stats[wgi + 0] = s_rms[0];
        wg_stats[wgi + 1] = s_chg[0];
    }
}

struct PC_M5aJacobian {
    int H_out, W_out, band_y0, band_h;
    int rgb_in_y0, rgb_in_h, has_fcomp, wg_x;
    float strength, rv_base, sh_base, quad;
};

kernel void k_m5a_jacobian(
    device const float* rgb_in [[buffer(0)]],
    device const float* fcomp [[buffer(1)]],
    device float* rgb_out [[buffer(2)]],
    device float* wg_stats [[buffer(3)]],
    constant PC_M5aJacobian& pc [[buffer(6)]],
    uint2 tgp [[threadgroup_position_in_grid]],
    uint2 tid2 [[thread_position_in_threadgroup]],
    uint tid [[thread_index_in_threadgroup]],
    uint2 gid [[thread_position_in_grid]]
) {
    threadgroup float sr[18][18], sg[18][18], sb[18][18];
    threadgroup float s_rms[256], s_chg[256];
    int wgx = int(tgp.x), wgy = int(tgp.y);
    int gx = int(gid.x), gy = int(gid.y);
    int wx_base = wgx * 16, wy_base = wgy * 16;

    for (int idx = int(tid); idx < 324; idx += 256) {
        int sy = idx / 18, sx = idx % 18;
        int ny_g = clamp(pc.band_y0 + wy_base + sy - 1, 0, pc.H_out - 1);
        int nx_g = clamp(wx_base + sx - 1, 0, pc.W_out - 1);
        int in_off = ((ny_g - pc.rgb_in_y0) * pc.W_out + nx_g) * 3;
        sr[sy][sx] = rgb_in[in_off + 0];
        sg[sy][sx] = rgb_in[in_off + 1];
        sb[sy][sx] = rgb_in[in_off + 2];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    float pix_rms = 0.0f, pix_chg = 0.0f;
    if (gx < pc.W_out && gy < pc.band_h) {
        int sy = int(tid2.y) + 1, sx = int(tid2.x) + 1;
        float p0r = sr[sy][sx], p0g = sg[sy][sx], p0b = sb[sy][sx];
        float out_r = p0r, out_b = p0b;
        float fc = (pc.has_fcomp != 0) ? clamp(fcomp[gy * pc.W_out + gx], 0.0f, 1.0f) : 0.5f;
        if (fc > 0.02f) {
            float mr = 0.0f, mg = 0.0f, mb = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                mr += sr[sy + dy][sx + dx]; mg += sg[sy + dy][sx + dx]; mb += sb[sy + dy][sx + dx];
            }
            mr *= (1.0f / 9.0f); mg *= (1.0f / 9.0f); mb *= (1.0f / 9.0f);
            float vg = 0.0f, crg = 0.0f, cbg = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                float dr = sr[sy + dy][sx + dx] - mr, dg = sg[sy + dy][sx + dx] - mg, db = sb[sy + dy][sx + dx] - mb;
                vg += dg * dg; crg += dr * dg; cbg += db * dg;
            }
            float gr_x = 0.5f * (sr[sy][sx + 1] - sr[sy][sx - 1]), gg_x = 0.5f * (sg[sy][sx + 1] - sg[sy][sx - 1]), gb_x = 0.5f * (sb[sy][sx + 1] - sb[sy][sx - 1]);
            float gr_y = 0.5f * (sr[sy + 1][sx] - sr[sy - 1][sx]), gg_y = 0.5f * (sg[sy + 1][sx] - sg[sy - 1][sx]), gb_y = 0.5f * (sb[sy + 1][sx] - sb[sy - 1][sx]);
            float gxx = gr_x * gr_x + gg_x * gg_x + gb_x * gb_x;
            float gxy = gr_x * gr_y + gg_x * gg_y + gb_x * gb_y;
            float gyy = gr_y * gr_y + gg_y * gg_y + gb_y * gb_y;
            float df = gxx - gyy;
            float disc = sqrt(max(0.0f, df * df + 4.0f * gxy * gxy));
            float sig2_sq = max(0.0f, 0.5f * ((gxx + gyy) - disc));
            float mg_pos = max(0.0f, mg);
            float nv = max(1e-12f, pc.rv_base + mg_pos * (pc.sh_base + pc.quad * mg_pos));
            if (sig2_sq > 1e-12f || vg > 1e-12f) {
                float slp_r = crg / (vg + 0.5f * nv + 1e-12f);
                float slp_b = cbg / (vg + 0.5f * nv + 1e-12f);
                float dg0 = p0g - mg;
                float res_r = p0r - (mr + slp_r * dg0), res_b = p0b - (mb + slp_b * dg0);
                float ratio = sig2_sq / (sig2_sq + max(1e-10f, 2.0f * nv));
                float pull = fc * pc.strength * ratio;
                float dr = -pull * res_r, db = -pull * res_b;
                out_r = p0r + dr; out_b = p0b + db;
                pix_rms = dr * dr + db * db;
                if (dr != 0.0f || db != 0.0f) pix_chg = 1.0f;
            }
        }
        int out_off = (gy * pc.W_out + gx) * 3;
        rgb_out[out_off + 0] = out_r;
        rgb_out[out_off + 1] = p0g;
        rgb_out[out_off + 2] = out_b;
    }
    s_rms[tid] = pix_rms;
    s_chg[tid] = pix_chg;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (int(tid) < stride) {
            s_rms[tid] += s_rms[tid + stride];
            s_chg[tid] += s_chg[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 2;
        wg_stats[wgi + 0] = s_rms[0];
        wg_stats[wgi + 1] = s_chg[0];
    }
}

struct PC_M5bFft {
    int H, W, band_y0, band_h;
    int in_y0, in_h, nr_band, nc;
    int out_scale, olpf_mode, is_astro, n_zones;
    int focus_h, focus_w, psf_iters, has_complex_otf;
    float str, psf_sigma_tile, rv, sh;
    float quad, max_boost, sig_tgt, m5a_gamma;
    float photo_shelf_k;
    int includes_pix_ap;
    int tr_b0, n_fb_zones;
};

static inline int m5b_reflect_clamp(int i, int n) {
    if (n <= 1) return 0;
    while (i < 0 || i >= n) {
        if (i < 0) i = -i;
        if (i >= n) i = 2 * n - 2 - i;
    }
    return i;
}

static inline float m5b_load_patch(device const float* rgb_in, constant PC_M5bFft& pc,
                                   int tile_y0, int tile_x0, int r, int c, int ch) {
    int gy = m5b_reflect_clamp(tile_y0 + r, pc.H);
    int gx = m5b_reflect_clamp(tile_x0 + c, pc.W);
    int sy = clamp(gy - pc.in_y0, 0, pc.in_h - 1);
    return rgb_in[(sy * pc.W + gx) * 3 + ch];
}

static inline float m5b_fft64_fwd(
    int tid, threadgroup float s_re[64][33], threadgroup float s_im[64][33],
    threadgroup float s_tmp[256], device const float* rgb_in, device const float* params,
    constant PC_M5bFft& pc, int tile_y0, int tile_x0, int ch
) {
    float sum_loc = 0.0f;
    float v0[8], v1[8];
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5, c = k & 31;
        v0[it] = m5b_load_patch(rgb_in, pc, tile_y0, tile_x0, r, 2 * c, ch);
        v1[it] = m5b_load_patch(rgb_in, pc, tile_y0, tile_x0, r, 2 * c + 1, ch);
        sum_loc += v0[it] + v1[it];
    }
    s_tmp[tid] = sum_loc;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float mu = s_tmp[0] * (1.0f / 4096.0f);
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5, c = k & 31;
        int cr = int(reverse_bits(uint(c)) >> 27);
        s_re[r][cr] = v0[it] - mu;
        s_im[r][cr] = v1[it] - mu;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (int s = 1; s <= 5; ++s) {
        int m = 1 << s, mh = m >> 1, step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int r = bf_id >> 4, lbf = bf_id & 15;
            int k = (lbf / mh) * m, j = lbf % mh, i0 = k + j, i1 = i0 + mh, widx = j * step64;
            float wr = params[widx], wi = -params[64 + widx];
            float r1 = s_re[r][i1], m1 = s_im[r][i1];
            float trv = wr * r1 - wi * m1, tiv = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - trv; s_im[r][i1] = m0 - tiv;
            s_re[r][i0] = r0 + trv; s_im[r][i0] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    {
        int r = tid >> 2, sub = tid & 3;
        int ks[4];
        float2 fa_arr[4], fb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float ar = s_re[r][k], ai = s_im[r][k], pr = s_re[r][kc], pi = s_im[r][kc];
                float2 E = 0.5f * float2(ar + pr, ai - pi);
                float2 O = 0.5f * float2(ai + pi, pr - ar);
                float wr = params[k], wi = -params[64 + k];
                fa_arr[p] = E + float2(wr * O.x - wi * O.y, wr * O.y + wi * O.x);
                float2 E2 = float2(E.x, -E.y), O2 = float2(O.x, -O.y);
                float wr2 = params[kc], wi2 = -params[64 + kc];
                fb_arr[p] = E2 + float2(wr2 * O2.x - wi2 * O2.y, wr2 * O2.y + wi2 * O2.x);
            }
        }
        float p0r = 0.0f, p0i = 0.0f, p16r = 0.0f, p16i = 0.0f;
        if (sub == 0) {
            p0r = s_re[r][0] + s_im[r][0]; p0i = s_re[r][0] - s_im[r][0];
            p16r = s_re[r][16]; p16i = -s_im[r][16];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int p = 0; p < 4; ++p) {
            int k = ks[p];
            if (k <= 15) {
                int kc = 32 - k;
                s_re[r][k] = fa_arr[p].x; s_im[r][k] = fa_arr[p].y;
                s_re[r][kc] = fb_arr[p].x; s_im[r][kc] = fb_arr[p].y;
            }
        }
        if (sub == 0) { s_re[r][0] = p0r; s_im[r][0] = p0i; s_re[r][16] = p16r; s_im[r][16] = p16i; }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5, col = k & 31, jr = int(reverse_bits(uint(r)) >> 26);
        if (r < jr) {
            float trv = s_re[r][col], tiv = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = trv; s_im[jr][col] = tiv;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int s = 1; s <= 6; ++s) {
        int m = 1 << s, mh = m >> 1, step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int col = bf_id >> 5, lbf = bf_id & 31;
            int k = (lbf / mh) * m, j = lbf % mh, i0 = k + j, i1 = i0 + mh, widx = j * step64;
            float wr = params[widx], wi = -params[64 + widx];
            float r1 = s_re[i1][col], m1 = s_im[i1][col];
            float trv = wr * r1 - wi * m1, tiv = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - trv; s_im[i1][col] = m0 - tiv;
            s_re[i0][col] = r0 + trv; s_im[i0][col] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    {
        float zax = 0.0f, zay = 0.0f, zbx = 0.0f, zby = 0.0f;
        if (tid < 64) {
            int kc = (64 - tid) & 63;
            zax = s_re[tid][0]; zay = s_im[tid][0];
            zbx = s_re[kc][0]; zby = s_im[kc][0];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        if (tid < 64) {
            s_re[tid][0] = 0.5f * (zax + zbx); s_im[tid][0] = 0.5f * (zay - zby);
            s_re[tid][32] = 0.5f * (zay + zby); s_im[tid][32] = 0.5f * (zbx - zax);
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    return mu;
}

static inline void m5b_fft64_inv_store(
    int tid, float mu, threadgroup float s_re[64][33], threadgroup float s_im[64][33],
    device const float* params, device float* rgb_dec, constant PC_M5bFft& pc,
    int core_top, int core_left, int core_h, int core_w, int ch
) {
    if (tid < 64) {
        int ky = tid;
        float c0r = s_re[ky][0], c0i = s_im[ky][0], c32r = s_re[ky][32], c32i = s_im[ky][32];
        s_re[ky][0] = c0r - c32i;
        s_im[ky][0] = c0i + c32r;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5, col = k & 31, jr = int(reverse_bits(uint(r)) >> 26);
        if (r < jr) {
            float trv = s_re[r][col], tiv = s_im[r][col];
            s_re[r][col] = s_re[jr][col]; s_im[r][col] = s_im[jr][col];
            s_re[jr][col] = trv; s_im[jr][col] = tiv;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int s = 1; s <= 6; ++s) {
        int m = 1 << s, mh = m >> 1, step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int col = bf_id >> 5, lbf = bf_id & 31;
            int k = (lbf / mh) * m, j = lbf % mh, i0 = k + j, i1 = i0 + mh, widx = j * step64;
            float wr = params[widx], wi = params[64 + widx];
            float r1 = s_re[i1][col], m1 = s_im[i1][col];
            float trv = wr * r1 - wi * m1, tiv = wr * m1 + wi * r1;
            float r0 = s_re[i0][col], m0 = s_im[i0][col];
            s_re[i1][col] = r0 - trv; s_im[i1][col] = m0 - tiv;
            s_re[i0][col] = r0 + trv; s_im[i0][col] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    {
        int r = tid >> 2, sub = tid & 3;
        int ks[4];
        float2 pa_arr[4], pb_arr[4];
        for (int p = 0; p < 4; ++p) {
            int k = 1 + sub * 4 + p;
            ks[p] = k;
            if (k <= 15) {
                int kc = 32 - k;
                float far = s_re[r][k], fai = s_im[r][k], fbr = s_re[r][kc], fbi = s_im[r][kc];
                float2 E = 0.5f * float2(far + fbr, fai - fbi);
                float2 WO = 0.5f * float2(far - fbr, fai + fbi);
                float wr = params[k], wi = params[64 + k];
                float2 O = float2(wr * WO.x - wi * WO.y, wr * WO.y + wi * WO.x);
                pa_arr[p] = float2(E.x - O.y, E.y + O.x);
                pb_arr[p] = float2(E.x + O.y, O.x - E.y);
            }
        }
        float q0r = 0.0f, q0i = 0.0f, q16r = 0.0f, q16i = 0.0f;
        if (sub == 0) {
            float F0 = s_re[r][0], F32 = s_im[r][0];
            q0r = 0.5f * (F0 + F32); q0i = 0.5f * (F0 - F32);
            q16r = s_re[r][16]; q16i = -s_im[r][16];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int p = 0; p < 4; ++p) {
            int k = ks[p];
            if (k <= 15) {
                int kc = 32 - k;
                s_re[r][k] = pa_arr[p].x; s_im[r][k] = pa_arr[p].y;
                s_re[r][kc] = pb_arr[p].x; s_im[r][kc] = pb_arr[p].y;
            }
        }
        if (sub == 0) { s_re[r][0] = q0r; s_im[r][0] = q0i; s_re[r][16] = q16r; s_im[r][16] = q16i; }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5, c = k & 31, jc = int(reverse_bits(uint(c)) >> 27);
        if (c < jc) {
            float trv = s_re[r][c], tiv = s_im[r][c];
            s_re[r][c] = s_re[r][jc]; s_im[r][c] = s_im[r][jc];
            s_re[r][jc] = trv; s_im[r][jc] = tiv;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int s = 1; s <= 5; ++s) {
        int m = 1 << s, mh = m >> 1, step64 = 64 >> s;
        for (int b = 0; b < 4; ++b) {
            int bf_id = tid + b * 256;
            int r = bf_id >> 4, lbf = bf_id & 15;
            int k = (lbf / mh) * m, j = lbf % mh, i0 = k + j, i1 = i0 + mh, widx = j * step64;
            float wr = params[widx], wi = params[64 + widx];
            float r1 = s_re[r][i1], m1 = s_im[r][i1];
            float trv = wr * r1 - wi * m1, tiv = wr * m1 + wi * r1;
            float r0 = s_re[r][i0], m0 = s_im[r][i0];
            s_re[r][i1] = r0 - trv; s_im[r][i1] = m0 - tiv;
            s_re[r][i0] = r0 + trv; s_im[r][i0] = m0 + tiv;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    float inv_n = 1.0f / 2048.0f;
    for (int it = 0; it < 8; ++it) {
        int k = tid + it * 256;
        int r = k >> 5, c = k & 31, lr = r - 12;
        if (lr >= 0 && lr < core_h) {
            int by = (core_top - pc.band_y0) + lr;
            int c0 = 2 * c - 12, c1 = c0 + 1;
            if (c0 >= 0 && c0 < core_w) rgb_dec[(by * pc.W + core_left + c0) * 3 + ch] = inv_n * s_re[r][c] + mu;
            if (c1 >= 0 && c1 < core_w) rgb_dec[(by * pc.W + core_left + c1) * 3 + ch] = inv_n * s_im[r][c] + mu;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
}

kernel void k_m5b_fft64(
    device const float* rgb_in [[buffer(0)]],
    device const float* params [[buffer(1)]],
    device const float* focus_half [[buffer(2)]],
    device float* rgb_dec [[buffer(3)]],
    device float* tile_act [[buffer(4)]],
    constant PC_M5bFft& pc [[buffer(6)]],
    uint ti_u [[threadgroup_position_in_grid]],
    uint tid_u [[thread_index_in_threadgroup]]
) {
    threadgroup float s_re[64][33];
    threadgroup float s_im[64][33];
    threadgroup float s_tmp[256];
    int tid = int(tid_u), ti = int(ti_u);
    int tr = ti / pc.nc, tc = ti % pc.nc;
    if (tr >= pc.nr_band) return;

    int core_top = pc.band_y0 + tr * 40, core_bot = min(pc.band_y0 + pc.band_h, core_top + 40);
    int core_left = tc * 40, core_right = min(pc.W, core_left + 40);
    int core_h = core_bot - core_top, core_w = core_right - core_left;
    int tile_y0 = core_top - 12, tile_x0 = core_left - 12;

    float g_mean = m5b_fft64_fwd(tid, s_re, s_im, s_tmp, rgb_in, params, pc, tile_y0, tile_x0, 1);

    float l_hf = 0.0f, l_hc = 0.0f;
    float l_b0 = 0.0f, l_c0 = 0.0f, l_b1 = 0.0f, l_c1 = 0.0f, l_b2 = 0.0f, l_c2 = 0.0f;
    for (int kh = tid; kh < 64 * 33; kh += 256) {
        int ky = kh / 33, kx = kh % 33;
        if (ky == 0 && kx == 0) continue;
        float mult = (kx == 0 || kx == 32) ? 1.0f : 2.0f;
        int sy = (ky <= 32) ? ky : (ky - 64), sx = kx;
        int r2 = sy * sy + sx * sx;
        float p = s_re[ky][kx] * s_re[ky][kx] + s_im[ky][kx] * s_im[ky][kx];
        float mp = mult * p;
        if (r2 >= 36 && r2 <= 400) { l_hf += mp; l_hc += mult; }
        if (r2 <= 36) { l_b0 += mp; l_c0 += mult; }
        else if (r2 <= 196) { l_b1 += mp; l_c1 += mult; }
        else { l_b2 += mp; l_c2 += mult; }
    }
    float vals[8] = {l_hf, l_hc, l_b0, l_c0, l_b1, l_c1, l_b2, l_c2};
    float sums[8];
    for (int q = 0; q < 8; ++q) {
        s_tmp[tid] = vals[q];
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int stride = 128; stride > 0; stride >>= 1) {
            if (tid < stride) s_tmp[tid] += s_tmp[tid + stride];
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        sums[q] = s_tmp[0];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    float g_pos = max(0.0f, g_mean);
    float var_g = max(1e-12f, pc.rv + g_pos * (pc.sh + pc.quad * g_pos));
    float p_hf = (sums[1] > 0.0f) ? (sums[0] / sums[1]) : 0.0f;
    float p_nse = 4096.0f * var_g;
    float g_bokeh_spec = (pc.is_astro != 0) ? 1.0f : clamp((p_hf - 2.0f * p_nse) / (2.0f * p_nse + 1e-12f), 0.0f, 1.0f);
    float alpha = pc.str * g_bokeh_spec;

    float focus_gate_max = 1.0f;
    if (pc.focus_h > 0 && pc.focus_w > 0 && pc.is_astro == 0) {
        float fy_s = float(pc.focus_h) / float(pc.H), fx_s = float(pc.focus_w) / float(pc.W);
        int fy0 = max(0, int(floor((float(core_top) + 0.5f) * fy_s - 0.5f)));
        int fy1 = min(pc.focus_h - 1, int(floor((float(core_bot - 1) + 0.5f) * fy_s - 0.5f)) + 1);
        int fx0 = max(0, int(floor((float(core_left) + 0.5f) * fx_s - 0.5f)));
        int fx1 = min(pc.focus_w - 1, int(floor((float(core_right - 1) + 0.5f) * fx_s - 0.5f)) + 1);
        float mx_loc = 0.0f;
        int ncells = (fy1 - fy0 + 1) * (fx1 - fx0 + 1);
        for (int idx = tid; idx < ncells; idx += 256) {
            int fyy = fy0 + idx / (fx1 - fx0 + 1), fxx = fx0 + idx % (fx1 - fx0 + 1);
            float fv = focus_half[fyy * pc.focus_w + fxx];
            mx_loc = max(mx_loc, clamp((fv - 0.02f) / 0.12f, 0.0f, 1.0f));
        }
        s_tmp[tid] = mx_loc;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int stride = 128; stride > 0; stride >>= 1) {
            if (tid < stride) s_tmp[tid] = max(s_tmp[tid], s_tmp[tid + stride]);
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        focus_gate_max = s_tmp[0];
    }

    if (alpha * focus_gate_max <= 1e-4f) {
        if (tid == 0) tile_act[ti] = 0.0f;
        return;
    }

    int otf_base = 132 + pc.n_zones * 8;
    int fb_zone_base = otf_base + ((pc.has_complex_otf != 0) ? (pc.n_zones * (3 * 64 * 33 * 2)) : 0);
    int tile_meta_base = fb_zone_base + pc.n_fb_zones * 8;
    int g_ti = (pc.tr_b0 + tr) * pc.nc + tc;
    float eff_shelf_k = params[tile_meta_base + 2 * g_ti + 0];
    bool tile_use_primary = (params[tile_meta_base + 2 * g_ti + 1] > 0.5f);
    int act_nz = tile_use_primary ? pc.n_zones : pc.n_fb_zones;
    int act_zb0 = tile_use_primary ? 132 : fb_zone_base;
    bool use_complex_otf = tile_use_primary && (pc.has_complex_otf != 0) && (act_nz > 0);
    if (!tile_use_primary && act_nz == 0 && pc.olpf_mode == 0 && pc.psf_sigma_tile <= 0.0f) {
        if (tid == 0) tile_act[ti] = 0.0f;
        return;
    }
    if (tid == 0) tile_act[ti] = alpha;

    float norm_cy = float(core_top + core_bot) * 0.5f / float(pc.H);
    float norm_cx = float(core_left + core_right) * 0.5f / float(pc.W);
    float sig_maj = pc.psf_sigma_tile, sig_min = sig_maj, ang = 0.0f;
    float sig_rgb[3] = {sig_maj, sig_maj, sig_maj};
    if (act_nz > 0) {
        float sw = 0.0f, smaj = 0.0f, smin = 0.0f, sc2 = 0.0f, ss2 = 0.0f;
        float sc_ch[3] = {0.0f, 0.0f, 0.0f};
        for (int z = 0; z < act_nz; ++z) {
            int zb = act_zb0 + z * 8;
            float dx = norm_cx - params[zb + 0], dy = norm_cy - params[zb + 1];
            float wz = 1.0f / (dx * dx + dy * dy + 0.04f);
            float zmaj = params[zb + 2], zmin = params[zb + 3], zang = params[zb + 4];
            sw += wz; smaj += wz * zmaj; smin += wz * zmin;
            sc2 += wz * cos(2.0f * zang); ss2 += wz * sin(2.0f * zang);
            for (int k = 0; k < 3; ++k) {
                float scv = (params[zb + 5 + k] > 0.0f) ? params[zb + 5 + k] : zmaj;
                sc_ch[k] += wz * scv;
            }
        }
        float invw = 1.0f / sw;
        sig_maj = smaj * invw; sig_min = smin * invw; ang = 0.5f * atan2(ss2, sc2);
        for (int k = 0; k < 3; ++k) sig_rgb[k] = sc_ch[k] * invw;
    }
    float s_maj_eff = sqrt(max(0.0f, sig_maj * sig_maj - pc.sig_tgt * pc.sig_tgt));
    float s_min_eff = sqrt(max(0.0f, sig_min * sig_min - pc.sig_tgt * pc.sig_tgt));
    float s_g_ref = max(0.01f, sig_rgb[1]);

    float reg_mul = max(0.25f, pc.m5a_gamma);
    float lam_b[3];
    for (int b = 0; b < 3; ++b) {
        float bs = (b == 0) ? sums[2] : ((b == 1) ? sums[4] : sums[6]);
        float bc = (b == 0) ? sums[3] : ((b == 1) ? sums[5] : sums[7]);
        float pb = (bc > 0.0f) ? (bs / bc) : p_hf;
        float snr_b = clamp((pb - p_nse) / (10.0f * p_nse + 1e-12f), 0.0f, 1.0f);
        lam_b[b] = (0.045f + (0.065f + 0.025f * float(b)) * (1.0f - snr_b)) * reg_mul;
    }

    float cs = cos(ang), sn = sin(ang);
    float olpf_c0 = (pc.olpf_mode == 1) ? 0.45f : ((pc.olpf_mode == 2) ? 0.15f : 1.0f);
    float olpf_d = ((pc.olpf_mode == 1) ? 0.48f : 0.55f) * float(pc.out_scale);
    float ratio_ch[3] = {sig_rgb[0] / s_g_ref, 1.0f, sig_rgb[2] / s_g_ref};
    float two_pi_64 = 6.28318530718f / 64.0f;
    bool eff_inc_pix_ap = tile_use_primary && (pc.includes_pix_ap != 0);
    bool apply_pix_sinc = !eff_inc_pix_ap && (use_complex_otf || act_nz == 0);
    float pix_scale = (use_complex_otf ? 0.50f : 0.41f) * float(pc.out_scale);
    float r_disk = alpha * max(0.0f, pc.max_boost - 1.0f);
    float inv_sw_zones = 0.0f;
    if (use_complex_otf) {
        float swz = 0.0f;
        for (int z = 0; z < act_nz; ++z) {
            int zb = act_zb0 + z * 8;
            float dx = norm_cx - params[zb + 0], dy = norm_cy - params[zb + 1];
            swz += 1.0f / (dx * dx + dy * dy + 0.04f);
        }
        inv_sw_zones = 1.0f / max(1e-12f, swz);
    }
    auto sinc_1 = [](float t) -> float {
        float at = abs(t);
        return (at > 1e-4f) ? (sin(at) / at) : (1.0f - (1.0f / 6.0f) * at * at);
    };
    auto eval_lam = [&](int r2) -> float {
        float lr = log(1.0f + float(r2));
        if (lr <= 4.615121f) {
            float tl = clamp((lr - 2.833213f) * (1.0f / 1.781908f), 0.0f, 1.0f);
            return lam_b[0] + (tl * tl * (3.0f - 2.0f * tl)) * (lam_b[1] - lam_b[0]);
        }
        float tl = clamp((lr - 4.615121f) * (1.0f / 1.378840f), 0.0f, 1.0f);
        return lam_b[1] + (tl * tl * (3.0f - 2.0f * tl)) * (lam_b[2] - lam_b[1]);
    };
    auto apply_ch_spectrum = [&](int ch, float s_maj_c, float s_min_c) {
        for (int kh = tid; kh < 64 * 33; kh += 256) {
            int ky = kh / 33, kx = kh % 33;
            if (ky == 0 && kx == 0) continue;
            int sy = (ky <= 32) ? ky : (ky - 64), sx = kx;
            int r2 = sy * sy + sx * sx;
            float lam = eval_lam(r2);
            float u = float(sy) * two_pi_64, v = float(sx) * two_pi_64;
            float w_rad = sqrt(u * u + v * v);
            float t_shelf = 1.0f;
            if (eff_shelf_k > 0.0f) {
                float dir_g = 1.0f;
                if (pc.out_scale == 2 && w_rad > 1.5707963f) {
                    float ang_f = atan2(abs(u), abs(v));
                    if (u * v < 0.0f) ang_f = 3.14159265f - ang_f;
                    int sec = int(floor((ang_f + 0.3926991f) * (1.0f / 0.7853982f))) & 3;
                    dir_g = clamp(params[128 + sec], 0.0f, 1.0f);
                }
                float ts = clamp((w_rad - 0.376991f) * (1.0f / 1.822124f), 0.0f, 1.0f);
                float sb = sin(3.14159265f * ts);
                t_shelf = 1.0f + (eff_shelf_k * dir_g) * sb * sb;
            }
            float h_olpf = (pc.olpf_mode == 0) ? 1.0f : (olpf_c0 + (1.0f - olpf_c0) * 0.5f * (cos(u * olpf_d) + cos(v * olpf_d)));
            float h_pix = apply_pix_sinc ? max(0.0f, sinc_1(u * pix_scale) * sinc_1(v * pix_scale)) : 1.0f;
            float mod_ap = h_olpf * h_pix;
            float h_re = 0.0f, h_im = 0.0f;
            if (use_complex_otf) {
                for (int z = 0; z < act_nz; ++z) {
                    int zb = act_zb0 + z * 8;
                    float dx = norm_cx - params[zb + 0], dy = norm_cy - params[zb + 1];
                    float zw = inv_sw_zones / (dx * dx + dy * dy + 0.04f);
                    int didx = otf_base + (z * 3 + ch) * (64 * 33 * 2) + (ky * 33 + kx) * 2;
                    h_re += zw * params[didx + 0];
                    h_im += zw * params[didx + 1];
                }
                if ((ky == 0 || ky == 32) && (kx == 0 || kx == 32)) h_im = 0.0f;
                h_re *= mod_ap; h_im *= mod_ap;
                float mag = sqrt(h_re * h_re + h_im * h_im);
                float tgt_inv = exp(0.5f * (u * u + v * v) * pc.sig_tgt * pc.sig_tgt);
                float s_rel = (mag > 1e-5f) ? min(tgt_inv, 1.0f / mag) : tgt_inv;
                h_re *= s_rel; h_im *= s_rel;
            } else {
                float ur = cs * v + sn * u, vr = -sn * v + cs * u;
                float s2_c = (ur * s_maj_c) * (ur * s_maj_c) + (vr * s_min_c) * (vr * s_min_c);
                h_re = exp(-0.5f * s2_c) * mod_ap;
            }
            float h2 = h_re * h_re + h_im * h_im;
            float denom = max(1e-12f, h2 + lam);
            float w_raw_re = (h_re * t_shelf + lam) / denom;
            float w_raw_im = (-h_im * t_shelf) / denom;
            float dw_re = alpha * (w_raw_re - 1.0f);
            float dw_im = alpha * w_raw_im;
            float dw_mag = sqrt(dw_re * dw_re + dw_im * dw_im);
            if (dw_mag > r_disk && dw_mag > 1e-12f) {
                float sc = r_disk / dw_mag;
                dw_re *= sc;
                dw_im *= sc;
            }
            float wr = 1.0f + dw_re, wi = dw_im;
            float fre = s_re[ky][kx], fim = s_im[ky][kx];
            s_re[ky][kx] = fre * wr - fim * wi;
            s_im[ky][kx] = fre * wi + fim * wr;
        }
    };

    apply_ch_spectrum(1, s_maj_eff, s_min_eff);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    m5b_fft64_inv_store(tid, g_mean, s_re, s_im, params, rgb_dec, pc, core_top, core_left, core_h, core_w, 1);

    const int rbs[2] = {0, 2};
    for (int ci = 0; ci < 2; ++ci) {
        int ch = rbs[ci];
        float mu_c = m5b_fft64_fwd(tid, s_re, s_im, s_tmp, rgb_in, params, pc, tile_y0, tile_x0, ch);
        float rc = ratio_ch[ch];
        float s_maj_c = sqrt(max(0.0f, (sig_maj * rc) * (sig_maj * rc) - pc.sig_tgt * pc.sig_tgt));
        float s_min_c = sqrt(max(0.0f, (sig_min * rc) * (sig_min * rc) - pc.sig_tgt * pc.sig_tgt));
        apply_ch_spectrum(ch, s_maj_c, s_min_c);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        m5b_fft64_inv_store(tid, mu_c, s_re, s_im, params, rgb_dec, pc, core_top, core_left, core_h, core_w, ch);
    }
}

struct PC_M5bClamp {
    int H, W, band_y0, band_h;
    int in_y0, in_h, nr_band, nc;
    int focus_h, focus_w, has_focus, wg_x;
    float rv, sh, quad, wb_r;
    float wb_b, sat0;
};

kernel void k_m5b_clamp(
    device const float* rgb_in [[buffer(0)]],
    device float* rgb_dec [[buffer(1)]],
    device const float* tile_act [[buffer(2)]],
    device const float* focus_half [[buffer(3)]],
    device float* wg_stats [[buffer(4)]],
    constant PC_M5bClamp& pc [[buffer(6)]],
    uint2 tgp [[threadgroup_position_in_grid]],
    uint2 tid2 [[thread_position_in_threadgroup]],
    uint tid [[thread_index_in_threadgroup]],
    uint2 gid [[thread_position_in_grid]]
) {
    threadgroup float sb0[20][20], sb1[20][20], sb2[20][20], s_ywb[20][20];
    threadgroup uint s_any_act;
    threadgroup float s_rms[256], s_chg[256];
    int wgx = int(tgp.x), wgy = int(tgp.y);
    int gx = int(gid.x), gy = int(gid.y);

    if (tid == 0) {
        int tr0 = clamp((wgy * 16) / 40, 0, pc.nr_band - 1);
        int tr1 = clamp((min(pc.band_h - 1, wgy * 16 + 15)) / 40, 0, pc.nr_band - 1);
        int tc0 = clamp((wgx * 16) / 40, 0, pc.nc - 1);
        int tc1 = clamp((min(pc.W - 1, wgx * 16 + 15)) / 40, 0, pc.nc - 1);
        uint aa = 0u;
        for (int tr = tr0; tr <= tr1; ++tr)
            for (int tc = tc0; tc <= tc1; ++tc)
                if (tile_act[tr * pc.nc + tc] > 1e-4f) aa = 1u;
        s_any_act = aa;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

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
            wg_stats[wgi + 0] = 0.0f;
            wg_stats[wgi + 1] = 0.0f;
        }
        return;
    }

    float wb_r_eff = max(1e-4f, pc.wb_r);
    float wb_b_eff = max(1e-4f, pc.wb_b);
    float inv_wb_r = 1.0f / wb_r_eff;
    float inv_wb_b = 1.0f / wb_b_eff;

    int wx_base = wgx * 16, wy_base = wgy * 16;
    for (int idx = int(tid); idx < 400; idx += 256) {
        int sy = idx / 20, sx = idx % 20;
        int ny_g = pc.band_y0 + wy_base + sy - 2;
        if (ny_g < 0) ny_g = -ny_g;
        if (ny_g >= pc.H) ny_g = max(0, 2 * pc.H - 2 - ny_g);
        int nx_g = wx_base + sx - 2;
        if (nx_g < 0) nx_g = -nx_g;
        if (nx_g >= pc.W) nx_g = max(0, 2 * pc.W - 2 - nx_g);
        int in_off = (clamp(ny_g - pc.in_y0, 0, pc.in_h - 1) * pc.W + nx_g) * 3;
        float r_v = rgb_in[in_off + 0], g_v = rgb_in[in_off + 1], b_v = rgb_in[in_off + 2];
        sb0[sy][sx] = r_v;
        sb1[sy][sx] = g_v;
        sb2[sy][sx] = b_v;
        s_ywb[sy][sx] = 0.25f * (r_v * wb_r_eff + 2.0f * g_v + b_v * wb_b_eff);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    auto eval_prom = [&](int ry, int cx) -> float2 {
        float y0 = s_ywb[ry][cx];
        float yh0 = s_ywb[ry][cx - 1], yh1 = s_ywb[ry][cx + 1];
        float yv0 = s_ywb[ry - 1][cx], yv1 = s_ywb[ry + 1][cx];
        float yd0 = s_ywb[ry - 1][cx - 1], yd1 = s_ywb[ry + 1][cx + 1];
        float ya0 = s_ywb[ry - 1][cx + 1], ya1 = s_ywb[ry + 1][cx - 1];
        float p_h = min(y0 - yh0, y0 - yh1), v_h = min(yh0 - y0, yh1 - y0);
        float p_v = min(y0 - yv0, y0 - yv1), v_v = min(yv0 - y0, yv1 - y0);
        float p_d = min(y0 - yd0, y0 - yd1), v_d = min(yd0 - y0, yd1 - y0);
        float p_a = min(y0 - ya0, y0 - ya1), v_a = min(ya0 - y0, ya1 - y0);
        return float2(max(0.0f, max(max(p_h, p_v), max(p_d, p_a))),
                      max(0.0f, max(max(v_h, v_v), max(v_d, v_a))));
    };

    float pix_rms = 0.0f, pix_chg = 0.0f;
    if (gx < pc.W && gy < pc.band_h) {
        int ti = (gy / 40) * pc.nc + (gx / 40);
        int sy = int(tid2.y) + 2, sx = int(tid2.x) + 2;
        float o0 = sb0[sy][sx], o1 = sb1[sy][sx], o2 = sb2[sy][sx];
        int out_off = (gy * pc.W + gx) * 3;
        if (tile_act[ti] <= 1e-4f) {
            rgb_dec[out_off + 0] = o0; rgb_dec[out_off + 1] = o1; rgb_dec[out_off + 2] = o2;
        } else {
            float d0 = rgb_dec[out_off + 0], d1 = rgb_dec[out_off + 1], d2 = rgb_dec[out_off + 2];
            float gx0 = 0.5f * (sb0[sy][sx + 1] - sb0[sy][sx - 1]), gy0 = 0.5f * (sb0[sy + 1][sx] - sb0[sy - 1][sx]);
            float gx1 = 0.5f * (sb1[sy][sx + 1] - sb1[sy][sx - 1]), gy1 = 0.5f * (sb1[sy + 1][sx] - sb1[sy - 1][sx]);
            float gx2 = 0.5f * (sb2[sy][sx + 1] - sb2[sy][sx - 1]), gy2 = 0.5f * (sb2[sy + 1][sx] - sb2[sy - 1][sx]);
            float lx0 = 0.5f * (sb0[sy][sx + 1] - 2.0f * o0 + sb0[sy][sx - 1]), ly0 = 0.5f * (sb0[sy + 1][sx] - 2.0f * o0 + sb0[sy - 1][sx]);
            float lx1 = 0.5f * (sb1[sy][sx + 1] - 2.0f * o1 + sb1[sy][sx - 1]), ly1 = 0.5f * (sb1[sy + 1][sx] - 2.0f * o1 + sb1[sy - 1][sx]);
            float lx2 = 0.5f * (sb2[sy][sx + 1] - 2.0f * o2 + sb2[sy][sx - 1]), ly2 = 0.5f * (sb2[sy + 1][sx] - 2.0f * o2 + sb2[sy - 1][sx]);
            float energy = ((gx0 * gx0 + gy0 * gy0 + 0.5f * (lx0 * lx0 + ly0 * ly0)) +
                            (gx1 * gx1 + gy1 * gy1 + 0.5f * (lx1 * lx1 + ly1 * ly1)) +
                            (gx2 * gx2 + gy2 * gy2 + 0.5f * (lx2 * lx2 + ly2 * ly2))) / 3.0f;
            float p0_pos = max(0.0f, o0), p1_pos = max(0.0f, o1), p2_pos = max(0.0f, o2);
            float variance = (max(1e-12f, pc.rv + p0_pos * (pc.sh + pc.quad * p0_pos)) +
                              max(1e-12f, pc.rv + p1_pos * (pc.sh + pc.quad * p1_pos)) +
                              max(1e-12f, pc.rv + p2_pos * (pc.sh + pc.quad * p2_pos))) / 3.0f;
            float g_focus = 1.0f;
            if (pc.has_focus != 0) {
                float fy_c = clamp((float(pc.band_y0 + gy) + 0.5f) * (float(pc.focus_h) / float(pc.H)) - 0.5f, 0.0f, float(pc.focus_h - 1));
                float fx_c = clamp((float(gx) + 0.5f) * (float(pc.focus_w) / float(pc.W)) - 0.5f, 0.0f, float(pc.focus_w - 1));
                int fy0 = int(fy_c), fx0 = int(fx_c);
                int fy1 = min(pc.focus_h - 1, fy0 + 1), fx1 = min(pc.focus_w - 1, fx0 + 1);
                float fyt = fy_c - float(fy0), fxt = fx_c - float(fx0);
                float fv = mix(mix(focus_half[fy0 * pc.focus_w + fx0], focus_half[fy0 * pc.focus_w + fx1], fxt),
                               mix(focus_half[fy1 * pc.focus_w + fx0], focus_half[fy1 * pc.focus_w + fx1], fxt), fyt);
                g_focus = clamp((fv - 0.02f) / 0.12f, 0.0f, 1.0f);
            }
            float gate = g_focus * (max(0.0f, energy - 2.6f * variance) / (energy + 6.0f * variance + 1e-12f));
            float sig = sqrt(variance);
            float sig_d = 1.2247449f * sig;

            float y_o = s_ywb[sy][sx];
            float y_d = 0.25f * (d0 * wb_r_eff + 2.0f * d1 + d2 * wb_b_eff);
            float2 pv0 = eval_prom(sy, sx);
            float mn3_y = y_o, mx3_y = y_o, nb_p = 0.0f, nb_v = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dy == 0 && dx == 0) continue;
                    float ny = s_ywb[sy + dy][sx + dx];
                    mn3_y = min(mn3_y, ny);
                    mx3_y = max(mx3_y, ny);
                    float2 pvn = eval_prom(sy + dy, sx + dx);
                    nb_p = max(nb_p, pvn.x);
                    nb_v = max(nb_v, pvn.y);
                }
            }
            float lo_dy = 0.0f, hi_dy = 0.0f;
            if (pv0.x > 2.5f * sig_d && pv0.y <= 1.5f * sig_d) {
                lo_dy = 0.0f;
                hi_dy = 0.36f * pv0.x + 1.5f * sig;
            } else if (pv0.y > 2.5f * sig_d && pv0.x <= 1.5f * sig_d) {
                lo_dy = -(0.36f * pv0.y + 1.5f * sig);
                hi_dy = 0.0f;
            } else {
                lo_dy = (mn3_y - y_o) - 0.5f * sig;
                hi_dy = (mx3_y - y_o) + 0.5f * sig;
                if (nb_p > 3.0f * sig_d && pv0.y <= 2.5f * sig_d) lo_dy = max(lo_dy, -0.15f * sig);
                if (nb_v > 3.0f * sig_d && pv0.x <= 2.5f * sig_d) hi_dy = min(hi_dy, 0.15f * sig);
            }
            float dy_clamped = clamp(gate * (y_d - y_o), lo_dy, hi_dy);

            float t_hl = 1.0f;
            if (pc.sat0 < 1.0e5f) {
                float pk = max(o1, max(o0 * wb_r_eff, o2 * wb_b_eff));
                if (pk > 0.80f * pc.sat0) {
                    float th = clamp((pk - 0.80f * pc.sat0) / max(1e-5f, 0.14f * pc.sat0), 0.0f, 1.0f);
                    t_hl = 1.0f - th * th * (3.0f - 2.0f * th);
                }
            }
            float c_lim = (0.08f * (mx3_y - mn3_y) + 0.75f * sig) * (t_hl * t_hl);
            float dcr_cand = gate * ((d0 * wb_r_eff - d1) - (o0 * wb_r_eff - o1)) * (t_hl * t_hl);
            float dcb_cand = gate * ((d2 * wb_b_eff - d1) - (o2 * wb_b_eff - o1)) * (t_hl * t_hl);
            float dcr = clamp(dcr_cand, -c_lim, c_lim);
            float dcb = clamp(dcb_cand, -c_lim, c_lim);

            float dg_v = dy_clamped - 0.25f * (dcr + dcb);
            float dr_v = (dg_v + dcr) * inv_wb_r;
            float db_v = (dg_v + dcb) * inv_wb_b;
            float u0 = o0 + dr_v, u1 = o1 + dg_v, u2 = o2 + db_v;
            rgb_dec[out_off + 0] = u0; rgb_dec[out_off + 1] = u1; rgb_dec[out_off + 2] = u2;
            float e0 = dr_v, e1 = dg_v, e2 = db_v;
            pix_rms = e0 * e0 + e1 * e1 + e2 * e2;
            if (e0 != 0.0f) pix_chg += 1.0f;
            if (e1 != 0.0f) pix_chg += 1.0f;
            if (e2 != 0.0f) pix_chg += 1.0f;
        }
    }
    s_rms[tid] = pix_rms;
    s_chg[tid] = pix_chg;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (int(tid) < stride) {
            s_rms[tid] += s_rms[tid + stride];
            s_chg[tid] += s_chg[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 2;
        wg_stats[wgi + 0] = s_rms[0];
        wg_stats[wgi + 1] = s_chg[0];
    }
}

struct PC_C13Warp {
    int H1, W1, Hh, Wh;
    int sy0, sy_cnt, hy0, hy_cnt;
    int band_y0, band_h, frame_n, has_flow;
    int has_mw, write_fallback, pad0, pad1;
    uint cfa0, cfa1, cfa2, pad2;
};

static inline uint cfa_at_c13_msl(uint cfa0, uint cfa1, uint cfa2, int r, int c) {
    int idx = (r % 6) * 6 + (c % 6);
    uint w = (idx < 16) ? cfa0 : ((idx < 32) ? cfa1 : cfa2);
    return (w >> uint((idx & 15) * 2)) & 3u;
}

static inline float3 c13_eval_raw_rgb(device const float* raw_slice, constant PC_C13Warp& pc, float y, float x) {
    int iy = int(floor(y)), ix = int(floor(x));
    int r_lo = max(0, iy - 2), r_hi = min(pc.H1 - 1, iy + 3);
    int c_lo = max(0, ix - 2), c_hi = min(pc.W1 - 1, ix + 3);
    float sum[3] = {0.0f, 0.0f, 0.0f};
    float wt[3]  = {0.0f, 0.0f, 0.0f};
    float kx[6];
    int cw = c_hi - c_lo + 1;
    for (int dc = 0; dc < cw; ++dc) kx[dc] = max(0.0f, 3.0f - abs(float(c_lo + dc) - x));
    for (int r = r_lo; r <= r_hi; ++r) {
        float ky = max(0.0f, 3.0f - abs(float(r) - y));
        if (ky <= 0.0f) continue;
        int sr = clamp(r - pc.sy0, 0, pc.sy_cnt - 1);
        int ro = sr * pc.W1 + c_lo;
        for (int dc = 0; dc < cw; ++dc) {
            float val = raw_slice[ro + dc];
            if (isnan(val) || isinf(val)) continue;
            uint ch = cfa_at_c13_msl(pc.cfa0, pc.cfa1, pc.cfa2, r, c_lo + dc);
            float k = ky * kx[dc];
            sum[ch] += k * val;
            wt[ch] += k;
        }
    }
    return float3(wt[0] > 0.0f ? sum[0] / wt[0] : 0.0f,
                  wt[1] > 0.0f ? sum[1] / wt[1] : 0.0f,
                  wt[2] > 0.0f ? sum[2] / wt[2] : 0.0f);
}

kernel void k_c13_warp_rgb(
    device const float* raw_slice [[buffer(0)]],
    device const float4* flow_mw_slice [[buffer(1)]],
    device float* rgb_stack [[buffer(2)]],
    device float* fallback_rgb [[buffer(3)]],
    constant PC_C13Warp& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), yr = int(gid.y);
    if (c >= pc.Wh || yr >= pc.band_h) return;
    int r = pc.band_y0 + yr;
    float nan_v = as_type<float>(0x7fc00000u);

    if (pc.write_fallback != 0) {
        float3 fb = c13_eval_raw_rgb(raw_slice, pc, float(2 * r), float(2 * c));
        int foff = (yr * pc.Wh + c) * 3;
        fallback_rgb[foff + 0] = fb.x; fallback_rgb[foff + 1] = fb.y; fallback_rgb[foff + 2] = fb.z;
    }

    int hloc = clamp(r - pc.hy0, 0, pc.hy_cnt - 1) * pc.Wh + c;
    float4 fm = (pc.has_flow != 0 || pc.has_mw != 0) ? flow_mw_slice[hloc] : float4(0.0f, 0.0f, 1.0f, 0.0f);
    bool ok = (pc.has_mw == 0 || fm.z >= 0.7f);
    float y = 2.0f * (float(r) + ((pc.has_flow != 0) ? fm.x : 0.0f));
    float x = 2.0f * (float(c) + ((pc.has_flow != 0) ? fm.y : 0.0f));
    if (isnan(y) || isinf(y) || isnan(x) || isinf(x) ||
        y < 0.0f || x < 0.0f || y > float(pc.H1 - 1) || x > float(pc.W1 - 1)) ok = false;

    int out_off = ((pc.frame_n * pc.band_h + yr) * pc.Wh + c) * 3;
    if (!ok) {
        rgb_stack[out_off + 0] = nan_v; rgb_stack[out_off + 1] = nan_v; rgb_stack[out_off + 2] = nan_v;
    } else {
        float3 v = c13_eval_raw_rgb(raw_slice, pc, y, x);
        rgb_stack[out_off + 0] = v.x; rgb_stack[out_off + 1] = v.y; rgb_stack[out_off + 2] = v.z;
    }
}

struct PC_C13Scene { int Wh, band_y0, band_h, N; };
kernel void k_c13_scene(
    device const float* rgb_stack [[buffer(0)]],
    device const float* fallback_rgb [[buffer(1)]],
    device float* scene_out [[buffer(2)]],
    constant PC_C13Scene& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int c = int(gid.x), yr = int(gid.y);
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
                float key = arr[i]; int j = i - 1;
                while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; --j; }
                arr[j + 1] = key;
            }
            scene_out[dst_off + ch] = arr[cnt >> 1];
        }
    }
}

struct PC_C13Med4Ph {
    int H1, W1, is_col, out_stride;
    int res_off, row4_off, pad0, pad1;
};

static inline uint c13_f2sk(float v) {
    uint u = as_type<uint>(v);
    return ((u & 0x80000000u) != 0u) ? (~u) : (u | 0x80000000u);
}

static inline float c13_sk2f(uint k) {
    uint u = ((k & 0x80000000u) != 0u) ? (k ^ 0x80000000u) : (~k);
    return as_type<float>(u);
}

static inline void c13_radix_select(
    int tid, int line_idx, int k0, int k_step, int line_len,
    device const float* res, device const float* row4, constant PC_C13Med4Ph& pc,
    threadgroup atomic_uint s_hist[256], threadgroup uint s_pref[256], threadgroup atomic_uint* s_cnt_all,
    thread uint& out_cnt, thread float& out_med
) {
    uint prefix_val = 0u, rem = 0u, cnt = 0u;
    for (int pass = 0; pass < 4; ++pass) {
        int sh = (3 - pass) * 8;
        uint mask_hi = (pass == 0) ? 0u : (~((1u << (sh + 8)) - 1u));
        atomic_store_explicit(&s_hist[tid], 0u, memory_order_relaxed);
        if (pass == 0 && tid == 0) atomic_store_explicit(s_cnt_all, 0u, memory_order_relaxed);
        threadgroup_barrier(mem_flags::mem_threadgroup);

        uint l_cnt = 0u;
        for (int k = k0 + tid * k_step; k < line_len; k += 256 * k_step) {
            float v = (pc.is_col == 0)
                ? res[pc.res_off + line_idx * pc.W1 + k]
                : res[pc.res_off + k * pc.W1 + line_idx];
            if (pc.is_col != 0 && !isnan(v)) v -= row4[pc.row4_off + k * 4 + (line_idx & 3)];
            if (isnan(v) || isinf(v)) continue;
            uint sk = c13_f2sk(v);
            if (pass == 0) {
                ++l_cnt;
                atomic_fetch_add_explicit(&s_hist[(sk >> 24) & 0xFFu], 1u, memory_order_relaxed);
            } else if (((sk ^ prefix_val) & mask_hi) == 0u) {
                atomic_fetch_add_explicit(&s_hist[(sk >> sh) & 0xFFu], 1u, memory_order_relaxed);
            }
        }
        if (pass == 0 && l_cnt > 0u) atomic_fetch_add_explicit(s_cnt_all, l_cnt, memory_order_relaxed);
        threadgroup_barrier(mem_flags::mem_threadgroup);

        if (pass == 0) {
            cnt = atomic_load_explicit(s_cnt_all, memory_order_relaxed);
            if (cnt < 8u) {
                threadgroup_barrier(mem_flags::mem_threadgroup);
                out_cnt = cnt;
                out_med = 0.0f;
                return;
            }
            rem = cnt >> 1;
        }
        s_pref[tid] = atomic_load_explicit(&s_hist[tid], memory_order_relaxed);
        threadgroup_barrier(mem_flags::mem_threadgroup);
        for (int off = 1; off < 256; off <<= 1) {
            uint add_v = (tid >= off) ? s_pref[tid - off] : 0u;
            threadgroup_barrier(mem_flags::mem_threadgroup);
            if (tid >= off) s_pref[tid] += add_v;
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        if (tid == 0) {
            uint found_b = 0u, before = 0u;
            for (uint b = 0u; b < 256u; ++b) {
                uint cend = s_pref[b];
                if (rem < cend) { found_b = b; before = (b == 0u) ? 0u : s_pref[b - 1u]; break; }
            }
            s_pref[0] = found_b;
            s_pref[1] = rem - before;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        prefix_val |= (s_pref[0] << sh);
        rem = s_pref[1];
    }
    out_cnt = cnt;
    out_med = c13_sk2f(prefix_val);
}

kernel void k_c13_med_4ph(
    device const float* res [[buffer(0)]],
    device const float* row4 [[buffer(1)]],
    device float* out_ph [[buffer(2)]],
    device uint* out_val [[buffer(3)]],
    constant PC_C13Med4Ph& pc [[buffer(6)]],
    uint wgx [[threadgroup_position_in_grid]],
    uint tid_u [[thread_index_in_threadgroup]]
) {
    threadgroup atomic_uint s_hist[256];
    threadgroup uint s_pref[256];
    threadgroup atomic_uint s_cnt_all;
    int tid = int(tid_u), line_idx = int(wgx);
    int n_lines = (pc.is_col == 0) ? pc.H1 : pc.W1;
    int line_len = (pc.is_col == 0) ? pc.W1 : pc.H1;
    if (line_idx >= n_lines) return;

    float meds[4]; uint cnts[4];
    bool need_pool = false;
    for (int ph = 0; ph < 4; ++ph) {
        c13_radix_select(tid, line_idx, ph, 4, line_len, res, row4, pc, s_hist, s_pref, &s_cnt_all, cnts[ph], meds[ph]);
        if (cnts[ph] < 8u) need_pool = true;
    }
    uint pool_cnt = 0u; float pool_med = 0.0f;
    if (need_pool) c13_radix_select(tid, line_idx, 0, 2, line_len, res, row4, pc, s_hist, s_pref, &s_cnt_all, pool_cnt, pool_med);
    if (tid == 0) {
        bool have_pool = (pool_cnt >= 8u);
        for (int ph = 0; ph < 4; ++ph) {
            int oi = ph * pc.out_stride + line_idx;
            if (cnts[ph] >= 8u) { out_ph[oi] = meds[ph]; out_val[oi] = 1u; }
            else if (have_pool) { out_ph[oi] = pool_med; out_val[oi] = 1u; }
            else { out_ph[oi] = 0.0f; out_val[oi] = 0u; }
        }
    }
}

struct PC_C13Hp97 { int sz, is_col, out_off, pad0; };
kernel void k_c13_hp97(
    device const float* in_ph [[buffer(0)]],
    device const uint* in_val [[buffer(1)]],
    device float* out_rc [[buffer(2)]],
    constant PC_C13Hp97& pc [[buffer(6)]],
    uint g [[thread_position_in_grid]]
) {
    int total = pc.sz * 4;
    if (int(g) >= total) return;
    int ph = int(g) / pc.sz, i = int(g) % pc.sz, base = ph * pc.sz;
    int dst_idx = pc.out_off + ((pc.is_col == 0) ? (i * 4 + ph) : (ph * pc.sz + i));
    if (in_val[base + i] == 0u) { out_rc[dst_idx] = 0.0f; return; }
    int d0 = max(-48, -i), d1 = min(48, pc.sz - 1 - i);
    float sum = 0.0f, den = 0.0f;
    for (int d = d0; d <= d1; ++d) {
        int j = i + d;
        if (in_val[base + j] == 0u) continue;
        float w = exp(-0.5f * float(d * d) / 256.0f);
        sum += w * in_ph[base + j];
        den += w;
    }
    out_rc[dst_idx] = in_ph[base + i] - sum / max(1e-20f, den);
}

struct PC_C13SensorRes {
    int H, W, Hh, Wh;
    int band_y0, band_h, hy0, hy_cnt;
    int out_off, raw_has_band, has_flow, has_mw;
    float read_variance, shot;
    uint cfa0, cfa1, cfa2, pad0;
};

static inline float4 sample_flow_sr_b_msl(device const float4* flow_in, constant PC_C13SensorRes& pc, float y, float x) {
    y = clamp(y, 0.0f, float(pc.Hh - 1)); x = clamp(x, 0.0f, float(pc.Wh - 1));
    int y0 = int(floor(y)), x0 = int(floor(x));
    int y1 = min(y0 + 1, pc.Hh - 1), x1 = min(x0 + 1, pc.Wh - 1);
    float fy = y - float(y0), fx = x - float(x0);
    int sy0 = clamp(y0 - pc.hy0, 0, pc.hy_cnt - 1);
    int sy1 = clamp(y1 - pc.hy0, 0, pc.hy_cnt - 1);
    return mix(mix(flow_in[sy0 * pc.Wh + x0], flow_in[sy0 * pc.Wh + x1], fx),
               mix(flow_in[sy1 * pc.Wh + x0], flow_in[sy1 * pc.Wh + x1], fx), fy);
}

static inline float sensor_scene_b_msl(device const float* scene_in, int Hh, int Wh, float y, float x, int ch) {
    y = clamp(y, 0.0f, float(Hh - 1)); x = clamp(x, 0.0f, float(Wh - 1));
    int r = int(floor(y)), c = int(floor(x));
    int r1 = min(r + 1, Hh - 1), c1 = min(c + 1, Wh - 1);
    float ty = y - float(r), tx = x - float(c);
    return (1.0f - ty) * ((1.0f - tx) * scene_in[(r * Wh + c) * 3 + ch] + tx * scene_in[(r * Wh + c1) * 3 + ch]) +
           ty * ((1.0f - tx) * scene_in[(r1 * Wh + c) * 3 + ch] + tx * scene_in[(r1 * Wh + c1) * 3 + ch]);
}

kernel void k_c13_sensor_res(
    device const float* raw_in [[buffer(0)]],
    device const float4* flow_in [[buffer(1)]],
    device const float* scene_in [[buffer(2)]],
    device float* residual_out [[buffer(3)]],
    constant PC_C13SensorRes& pc [[buffer(6)]],
    uint2 gid [[thread_position_in_grid]]
) {
    int X = int(gid.x), yr = int(gid.y);
    if (X >= pc.W || yr >= pc.band_h) return;
    int Y = pc.band_y0 + yr;
    int oi = pc.out_off + yr * pc.W + X;
    int ri = ((pc.raw_has_band != 0) ? yr : Y) * pc.W + X;
    residual_out[oi] = as_type<float>(0x7fc00000u);

    float y = float(Y), x = float(X);
    if (pc.has_flow != 0) {
        for (int k = 0; k < 5; ++k) {
            float4 f = sample_flow_sr_b_msl(flow_in, pc, y * 0.5f, x * 0.5f);
            float yy = float(Y) - 2.0f * f.x, xx = float(X) - 2.0f * f.y;
            bool settled = (yy == y && xx == x);
            y = yy; x = xx;
            if (settled) break;
        }
    }
    if (isnan(y) || isinf(y) || isnan(x) || isinf(x) ||
        y < 0.0f || x < 0.0f || y > float(pc.H - 1) || x > float(pc.W - 1)) return;
    if (pc.has_mw != 0 && sample_flow_sr_b_msl(flow_in, pc, y * 0.5f, x * 0.5f).z < 0.7f) return;
    int ch = int(cfa_at_c13_msl(pc.cfa0, pc.cfa1, pc.cfa2, Y, X));
    float estimate = sensor_scene_b_msl(scene_in, pc.Hh, pc.Wh, y * 0.5f, x * 0.5f, ch);
    float v = raw_in[ri];
    if (isnan(v + estimate) || isinf(v + estimate) || estimate >= 0.15f) return;
    float sigma = sqrt(max(1e-12f, pc.read_variance + pc.shot * max(0.0f, estimate)));
    float gx = 0.5f * (sensor_scene_b_msl(scene_in, pc.Hh, pc.Wh, y * 0.5f, (x + 1.0f) * 0.5f, ch) -
                       sensor_scene_b_msl(scene_in, pc.Hh, pc.Wh, y * 0.5f, (x - 1.0f) * 0.5f, ch));
    float gy = 0.5f * (sensor_scene_b_msl(scene_in, pc.Hh, pc.Wh, (y + 1.0f) * 0.5f, x * 0.5f, ch) -
                       sensor_scene_b_msl(scene_in, pc.Hh, pc.Wh, (y - 1.0f) * 0.5f, x * 0.5f, ch));
    if (gx * gx + gy * gy >= 4.0f * sigma * sigma) return;
    residual_out[oi] = v - estimate;
}

struct PC_C13HotMap {
    int H1, W1, N, band_y0;
    int band_h, wg_x, col_base_off, pad0;
    float hot_sigma, hot_thr, pad1, pad2;
};

kernel void k_c13_hot_map(
    device const float* res_band [[buffer(0)]],
    device const float* rc_atlas [[buffer(1)]],
    device float* hot_band [[buffer(2)]],
    device float* wg_stats [[buffer(3)]],
    constant PC_C13HotMap& pc [[buffer(6)]],
    uint2 tgp [[threadgroup_position_in_grid]],
    uint tid_u [[thread_index_in_threadgroup]],
    uint2 gid [[thread_position_in_grid]]
) {
    threadgroup float s_b[256], s_a[256], s_r[256], s_s[256], s_c[256];
    int tid = int(tid_u), wgx = int(tgp.x), wgy = int(tgp.y);
    int gx = int(gid.x), gy = int(gid.y);
    float pb = 0.0f, pa = 0.0f, pr = 0.0f, ps = 0.0f, pchg = 0.0f;
    if (gx < pc.W1 && gy < pc.band_h) {
        int r = pc.band_y0 + gy, c_ph = gx & 3, r_ph = r & 3;
        int frame_stride = pc.band_h * pc.W1, row4_stride = pc.H1 * 4, col4_stride = 4 * pc.W1;
        int px = gy * pc.W1 + gx;
        float cv[16], rc[16], dv_arr[16]; bool ok[16];
        int valid_cnt = 0, above_thr = 0;
        for (int n = 0; n < pc.N; ++n) {
            float v = res_band[n * frame_stride + px];
            float cor0 = rc_atlas[(n * row4_stride + r * 4) + c_ph] +
                         rc_atlas[pc.col_base_off + n * col4_stride + r_ph * pc.W1 + gx];
            cv[n] = v; rc[n] = cor0;
            bool fin = (!isnan(v) && !isinf(v));
            ok[n] = fin;
            if (fin) {
                float dv = v - cor0;
                dv_arr[valid_cnt++] = dv;
                if (abs(dv) > pc.hot_thr) ++above_thr;
            }
        }
        float hot = 0.0f;
        if (valid_cnt * 4 >= pc.N * 3 && valid_cnt >= 3 && above_thr >= (valid_cnt >> 1)) {
            for (int i = 1; i < valid_cnt; ++i) {
                float key = dv_arr[i]; int j = i - 1;
                while (j >= 0 && dv_arr[j] > key) { dv_arr[j + 1] = dv_arr[j]; --j; }
                dv_arr[j + 1] = key;
            }
            float center = dv_arr[valid_cnt >> 1];
            float tol = max(3.0f * pc.hot_sigma, 0.35f * abs(center));
            int agree = 0;
            for (int i = 0; i < valid_cnt; ++i) if (dv_arr[i] * center > 0.0f && abs(dv_arr[i] - center) < tol) ++agree;
            if (abs(center) > pc.hot_thr && agree * 4 >= valid_cnt * 3) hot = center;
        }
        hot_band[px] = hot;
        for (int n = 0; n < pc.N; ++n) {
            float cor = rc[n] + hot;
            if (ok[n]) { float v = cv[n], d = v - cor; pb += v * v; pa += d * d; ps += 1.0f; }
            pr += cor * cor;
            if (cor != 0.0f) pchg += 1.0f;
        }
    }
    s_b[tid] = pb; s_a[tid] = pa; s_r[tid] = pr; s_s[tid] = ps; s_c[tid] = pchg;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (int stride = 128; stride > 0; stride >>= 1) {
        if (tid < stride) {
            s_b[tid] += s_b[tid + stride]; s_a[tid] += s_a[tid + stride];
            s_r[tid] += s_r[tid + stride]; s_s[tid] += s_s[tid + stride]; s_c[tid] += s_c[tid + stride];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0) {
        int wgi = (wgy * pc.wg_x + wgx) * 5;
        wg_stats[wgi + 0] = s_b[0]; wg_stats[wgi + 1] = s_a[0];
        wg_stats[wgi + 2] = s_r[0]; wg_stats[wgi + 3] = s_s[0]; wg_stats[wgi + 4] = s_c[0];
    }
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
    K_V3_PREP_GUIDES,
    K_V3_GREEN,
    K_V3_CHROMA,
    K_V3_VST_FWD,
    K_V3_FFT64_SURE,
    K_V3_DV_HALF,
    K_V3_VST_INV,
    K_V3_IMPULSE_CLAMP,
    K_V3_DOWN2,
    K_V3_UP2_LAP,
    K_V3_ODD_HUBER,
    K_V3_ASSEMBLE_RGB,
    K_M5A_ZERO_U32,
    K_M5A_SCATTER,
    K_M5A_UPDATE,
    K_M5A_JACOBIAN,
    K_M5B_FFT64,
    K_M5B_CLAMP,
    K_C13_WARP_RGB,
    K_C13_SCENE,
    K_C13_MED_4PH,
    K_C13_HP97,
    K_C13_SENSOR_RES,
    K_C13_HOT_MAP,
    K_COUNT
};

static const char* kKernelNames[K_COUNT] = {
    "k_zero", "k_green_accum", "k_green_final", "k_chroma_accum", "k_chroma_final",
    "k_gat_fwd", "k_box_down2x", "k_dct_wiener", "k_pyr_sub_down2x", "k_pyr_lap_add",
    "k_edge_polish", "k_dv_map", "k_gauss_1d", "k_gat_inv_quad", "k_median3x3",
    "k_glint_desat", "k_atrous_1d", "k_assemble_rgb", "k_rl_lum", "k_rl_prep_h",
    "k_rl_prep_v", "k_rl_ratio", "k_rl_update", "k_rl_apply", "k_sensor_residual",
    "k_v3_prep_guides", "k_v3_green", "k_v3_chroma", "k_v3_vst_fwd", "k_v3_fft64_sure",
    "k_v3_dv_half", "k_v3_vst_inv", "k_v3_impulse_clamp", "k_v3_down2", "k_v3_up2_lap",
    "k_v3_odd_huber", "k_v3_assemble_rgb",
    "k_m5a_zero_u32", "k_m5a_scatter", "k_m5a_update", "k_m5a_jacobian",
    "k_m5b_fft64", "k_m5b_clamp",
    "k_c13_warp_rgb", "k_c13_scene", "k_c13_med_4ph", "k_c13_hp97",
    "k_c13_sensor_res", "k_c13_hot_map"
};

struct MetalContext {
    static constexpr int kArenaSlots = 38;
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
                float axx = job.inv_cov[i * 3 + 0];
                float axy = job.inv_cov[i * 3 + 1];
                float ayy = job.inv_cov[i * 3 + 2];
                if (job.tile_spec_map) {
                    eval_tile_modulated_icov(job, int(i / size_t(std::max(1, Wh))), int(i % size_t(std::max(1, Wh))), axx, axy, ayy);
                }
                dst[i * 4 + 0] = axx;
                dst[i * 4 + 1] = axy;
                dst[i * 4 + 2] = ayy;
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
                std::vector<heraw_sync::JoiningThread> workers;
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
                        float lv_dec = kGpuAtrousLvDecay[(size_t)lv];
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
            std::vector<heraw_sync::JoiningThread> workers;
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
        }
        cost_mark(6);
        return true;
    }
}

static bool run_stage45_gpu_v3_full(Stage45GpuJob& job) {
    @autoreleasepool {
        auto cost_start = std::chrono::steady_clock::now();
        job.out_wall_seconds.fill(0);
        job.out_device_seconds.fill(0);
        job.out_has_device_timing = true;
        auto cost_mark = [&](size_t phase) {
            const auto now = std::chrono::steady_clock::now();
            job.out_wall_seconds[phase] += std::chrono::duration<double>(now - cost_start).count();
            cost_start = now;
        };
        auto device_cost = [&](id<MTLCommandBuffer> cb, size_t phase) {
            job.out_device_seconds[phase] += std::max(0.0, cb.GPUEndTime - cb.GPUStartTime);
        };
        auto& ctx = global_metal();
        if (!ctx.ensure_init()) {
            job.out_err = ctx.init_err;
            return false;
        }

        const int out_scale = job.is_1x_mode ? 1 : 2;
        const int H1 = job.H1, W1 = job.W1;
        const int Hh = H1 / 2, Wh = W1 / 2;
        const int H_out = H1 * out_scale, W_out = W1 * out_scale;
        const int Eh = (H1 + 3) / 4, Ew = (W1 + 3) / 4;
        const int eta_shift = (out_scale == 2) ? 3 : 2;
        const int inv_out_s = (out_scale == 2) ? 1 : 0;
        const int full_H1 = (job.full_H1 > 0) ? job.full_H1 : H1;
        const int full_W1 = (job.full_W1 > 0) ? job.full_W1 : W1;
        const size_t n_half = size_t(Hh) * Wh;
        const size_t n_1x   = size_t(H1) * W1;
        const size_t n_out  = size_t(H_out) * W_out;
        const size_t n_eta  = size_t(Eh) * Ew;

        const size_t peak_1x   = std::max(n_1x, job.arena_peak_1x_pixels);
        const size_t peak_half = std::max(n_half, peak_1x / 4);
        const size_t peak_out  = std::max(n_out, peak_1x * size_t(out_scale * out_scale));
        const size_t peak_eta  = std::max(n_eta, (peak_1x + 15) / 16 + 1024);

        const size_t frame_atlas_bytes = size_t(job.N) * peak_1x * sizeof(float);
        const size_t flow_atlas_bytes  = size_t(job.N) * std::max<size_t>(1, peak_half) * sizeof(float) * 4;
        const bool need_fcomp = (!job.eta_only) && (job.fisher_comp_out != nullptr || job.direct_stitch_fcomp != nullptr);
        const size_t fcomp_eta_bytes   = (need_fcomp ? (peak_out + peak_eta) : peak_eta) * sizeof(float);
        if (ctx.dev.maxBufferLength < std::max({frame_atlas_bytes, flow_atlas_bytes, fcomp_eta_bytes, peak_out * sizeof(float)})) {
            job.out_err = "Device maxBufferLength smaller than V3 atlas";
            return false;
        }

        auto t_s4 = std::chrono::steady_clock::now();
        job.out_device_label = "Metal (" + ctx.dev_name + ")";

        const size_t guides_elems = size_t(4 + 3 * job.N) + 2 * std::max<size_t>(1, peak_half);
        id<MTLBuffer> b_green0     = ctx.acquire_buf(0,  peak_out * sizeof(float));
        id<MTLBuffer> b_fcomp_eta  = ctx.acquire_buf(3,  fcomp_eta_bytes);
        id<MTLBuffer> b_frames_all = ctx.acquire_buf(1,  frame_atlas_bytes);
        id<MTLBuffer> b_flows_all  = ctx.acquire_buf(2,  flow_atlas_bytes);
        id<MTLBuffer> b_guides     = ctx.acquire_buf(4,  guides_elems * sizeof(float) * 4);
        id<MTLBuffer> b_icov       = ctx.acquire_buf(7,  std::max<size_t>(1, peak_half) * sizeof(float) * 4);
        id<MTLBuffer> b_canc       = ctx.acquire_buf(8,  peak_1x * sizeof(float));
        id<MTLBuffer> b_g0ha       = ctx.acquire_buf(9,  peak_1x * sizeof(float));
        id<MTLBuffer> b_boost      = ctx.acquire_buf(10, std::max<size_t>(1, peak_half) * sizeof(float));
        id<MTLBuffer> b_dR         = ctx.acquire_buf(11, peak_1x * sizeof(float));
        id<MTLBuffer> b_dB         = ctx.acquire_buf(12, peak_1x * sizeof(float));

        if (!b_green0 || !b_fcomp_eta || !b_frames_all || !b_flows_all ||
            !b_guides || !b_icov || !b_canc || !b_g0ha || !b_boost || !b_dR || !b_dB) {
            job.out_err = "Metal V3 buffer allocation failed";
            return false;
        }

        // Pack icov_guide, static planes, guide header, and N-frame atlas in unified memory
        std::vector<float> hdr_vec4;
        bool is_aeb = false, any_rob = false, is_bayer = false;
        float sat0 = 1.0e6f;
        v3_host::build_v3_guides_header(job, hdr_vec4, is_aeb, any_rob, sat0, is_bayer);
        (void)sat0;

        {
            float* dst_ic = (float*)[b_icov contents];
            for (size_t i = 0; i < n_half; ++i) {
                float axx = job.inv_cov[i * 3 + 0];
                float axy = job.inv_cov[i * 3 + 1];
                float ayy = job.inv_cov[i * 3 + 2];
                if (job.tile_spec_map) {
                    eval_tile_modulated_icov(job, int(i / size_t(std::max(1, Wh))), int(i % size_t(std::max(1, Wh))), axx, axy, ayy);
                }
                dst_ic[i * 4 + 0] = axx;
                dst_ic[i * 4 + 1] = axy;
                dst_ic[i * 4 + 2] = ayy;
                dst_ic[i * 4 + 3] = job.g_guide[i];
            }
            std::memcpy([b_canc contents], job.c_anc_1x, n_1x * sizeof(float));
            std::memcpy([b_g0ha contents], job.g0_ha_1x, n_1x * sizeof(float));
            std::memcpy([b_boost contents], job.snr_boost_half, n_half * sizeof(float));
            std::memcpy([b_guides contents], hdr_vec4.data(), hdr_vec4.size() * sizeof(float));

            std::vector<float> gamma_quad;
            if (out_scale == 2) v3_host::compute_gamma_quad_half(job, gamma_quad);
            float* dst_fr = (float*)[b_frames_all contents];
            float* dst_fl = (float*)[b_flows_all contents];
            const int p_nt = (n_1x * size_t(job.N) >= 131072 && job.n_threads > 1) ? std::min(job.n_threads, 8) : 1;
            auto pack_n = [&](int n) {
                std::memcpy(dst_fr + size_t(n) * n_1x, (*job.frames_lin)[n].data(), n_1x * sizeof(float));
                float* fmw = dst_fl + size_t(n) * n_half * 4;
                const float* fy = (*job.flows_fy)[n].data();
                const float* fx = (*job.flows_fx)[n].data();
                const float* mw = (*job.mw_half)[n].data();
                const float* gq_ptr = (n == 0 && gamma_quad.size() >= n_half) ? gamma_quad.data() : nullptr;
                for (size_t i = 0; i < n_half; ++i) {
                    fmw[i * 4 + 0] = fy[i];
                    fmw[i * 4 + 1] = fx[i];
                    fmw[i * 4 + 2] = gpu_motion_weight(job, mw[i]);
                    fmw[i * 4 + 3] = gq_ptr ? gq_ptr[i] : 0.0f;
                }
            };
            if (p_nt <= 1 || job.N <= 1) {
                for (int n = 0; n < job.N; ++n) pack_n(n);
            } else {
                std::vector<heraw_sync::JoiningThread> workers;
                const int nt_use = std::min(p_nt, job.N);
                workers.reserve(nt_use);
                for (int t = 0; t < nt_use; ++t) {
                    workers.emplace_back([&, t, nt_use]() {
                        for (int n = t; n < job.N; n += nt_use) pack_n(n);
                    });
                }
            }
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

        uint32_t cfa_w[3] = {0, 0, 0};
        pack_cfa6x6_bits(job.cfa6x6, cfa_w);
        const int fcomp_off_eta = job.eta_only ? -1 : (need_fcomp ? int(n_out) : 0);
        const size_t eta_elem_off = (fcomp_off_eta > 0) ? size_t(fcomp_off_eta) : 0;
        std::memset((float*)[b_fcomp_eta contents] + eta_elem_off, 0, n_eta * sizeof(float));

        cost_mark(0);

        // --- CB 1: Guide Prep + Stage 4A Green Fusion ---
        {
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            PC_V3PrepGuides pcpg{H1, W1, Hh, Wh, 4 + 3 * job.N,
                                 float(job.pg_sig0_sq), float(job.pg_gain),
                                 cfa_w[0], cfa_w[1], cfa_w[2]};
            enc_2d(enc, K_V3_PREP_GUIDES, {b_icov, b_canc, b_boost, b_guides}, &pcpg, sizeof(pcpg), Wh, Hh);

            PC_V3Green pcg{H1, W1, Hh, Wh, Ew, Eh, job.N, out_scale,
                           job.origin_y, job.origin_x, is_bayer ? 1 : 0, job.is_clean_plate ? 1 : 0,
                           job.is_virtual_nd ? 1 : 0, is_aeb ? 1 : 0, any_rob ? 1 : 0, fcomp_off_eta,
                           cfa_w[0], cfa_w[1], cfa_w[2], job.disable_ha_fallback ? 1 : 0};
            enc_2d(enc, K_V3_GREEN, {b_frames_all, b_flows_all, b_guides, b_g0ha, b_green0, b_fcomp_eta}, &pcg, sizeof(pcg), W1, H1);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal V3 green dispatch failed";
                return false;
            }
            device_cost(cb, 1);
        }

        b_icov = nil;
        b_canc = nil;
        b_g0ha = nil;

        if (job.global_eta < 0.0 || job.eta_quarter_out != nullptr)
            gpu_eta_stats(job, (const float*)[b_fcomp_eta contents] + eta_elem_off, Ew, Eh);
        const double eta_eff = (job.global_eta >= 0.0) ? job.global_eta
            : std::max(0.005, job.out_eta_sum / double(std::max<int64_t>(1, job.out_eta_count)));
        job.out_eta_eff = eta_eff;
        cost_mark(1);
        if (job.eta_only) return true;

        // --- CB 2: Stage 4B Chroma-Diff Fusion ---
        {
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            PC_V3Chroma pcc{H1, W1, Hh, Wh, job.N, out_scale, full_H1, full_W1,
                            job.origin_y, job.origin_x, is_bayer ? 1 : 0, job.is_xtrans ? 1 : 0,
                            job.is_virtual_nd ? 1 : 0, is_aeb ? 1 : 0, any_rob ? 1 : 0, job.is_astro ? 1 : 0,
                            cfa_w[0], cfa_w[1], cfa_w[2]};
            enc_2d(enc, K_V3_CHROMA, {b_frames_all, b_flows_all, b_guides, b_green0, b_dR, b_dB}, &pcc, sizeof(pcc), W1, H1);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal V3 chroma dispatch failed";
                return false;
            }
            device_cost(cb, 2);
        }
        b_frames_all = nil;
        b_flows_all = nil;
        b_guides = nil;
        cost_mark(2);

        const double dt_s4 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_s4).count();
        if (job.on_stage4_done) job.on_stage4_done(eta_eff, dt_s4, job.out_device_label);

        id<MTLBuffer> b_z    = ctx.acquire_buf(1, peak_out * sizeof(float));
        id<MTLBuffer> b_den  = ctx.acquire_buf(2, peak_out * sizeof(float));
        id<MTLBuffer> b_outR = ctx.acquire_buf(4, peak_out * sizeof(float));
        if (!b_z || !b_den || !b_outR) {
            job.out_err = "Metal V3 Stage 5 output buffer allocation failed";
            return false;
        }

        // --- CB 3: Stage 5A Exact-Integral NLT-VST + 64x64 COLA 2D FFT 4-Octave SURE ---
        Stage45GpuJob j5 = job;
        if (j5.global_mean_g < 0.0) {
            const float* pg = (const float*)[b_green0 contents];
            double m1 = 0.0;
            for (size_t i = 0; i < n_out; i += 16) m1 += std::max(0.0f, pg[i]);
            j5.global_mean_g = m1 / double((n_out + 15) / 16);
        }
        double sig2 = 0.0, gain = 0.0;
        gpu_stage5_gat_params(j5, eta_eff, sig2, gain);

        float min_g = 0.0f, max_g = 1.0f;
        if (job.has_global_g_range) {
            min_g = job.global_min_g;
            max_g = job.global_max_g;
        } else {
            const float* pg = (const float*)[b_green0 contents];
            min_g = pg[0]; max_g = pg[0];
            for (size_t i = 0; i < n_out; i += 8) {
                const float v = pg[i];
                min_g = std::min(min_g, v);
                max_g = std::max(max_g, v);
            }
        }

        const bool has_luma_noise = (sig2 + gain * std::max(0.0, j5.global_mean_g)) > 1e-16;
        if (job.denoise_strength > 1e-4f && has_luma_noise) {
            const int full_H_out = full_H1 * out_scale;
            const int origin_out_y = job.origin_y * out_scale;
            const int origin_out_x = job.origin_x * out_scale;
            const int phase_y = origin_out_y & 31;
            const int phase_x = origin_out_x & 31;
            const int nr_t = (H_out + phase_y + 63) / 32;
            const int nc_t = (W_out + phase_x + 63) / 32;
            id<MTLBuffer> b_lut = ctx.acquire_buf(13, size_t(v3_host::kLutSize) * sizeof(float));
            id<MTLBuffer> b_dv_tile = ctx.acquire_buf(14, std::max<size_t>(1, size_t(nr_t) * nc_t) * sizeof(float));
            if (!b_lut || !b_dv_tile) {
                job.out_err = "Metal V3 Stage 5A buffer allocation failed";
                return false;
            }
            std::vector<float> lut_vec;
            float u_min = 0.0f, u_max = 1.0f, inv_du = 1.0f, oct_mul[4] = {};
            v3_host::build_v3_stage5_lut_blob(job, out_scale, eta_eff, min_g, max_g, sig2, gain,
                                              lut_vec, u_min, u_max, inv_du, oct_mul);
            std::memcpy([b_lut contents], lut_vec.data(), lut_vec.size() * sizeof(float));

            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            PC_V3VstFwd pcvf{H_out, W_out, full_H_out, origin_out_y, u_min, u_max, inv_du, 0.0f};
            enc_2d(enc, K_V3_VST_FWD, {b_green0, b_lut, b_z}, &pcvf, sizeof(pcvf), W_out, H_out);

            PC_Zero pcz{uint32_t(n_out)};
            enc_1d(enc, K_ZERO, {b_den}, &pcz, sizeof(pcz), uint32_t(n_out));

            const float inv_mean_eta = float(1.0 / std::max(1e-4, eta_eff));
            for (int color = 0; color < 4; ++color) {
                const int ntr_c = (nr_t - (color >> 1) + 1) / 2;
                const int ntc_c = (nc_t - (color & 1) + 1) / 2;
                if (ntr_c <= 0 || ntc_c <= 0) continue;
                PC_V3FftSure pcff{H_out, W_out, Hh, Wh, Eh, Ew, int(eta_elem_off), color,
                                  ntr_c, ntc_c, phase_y, phase_x, inv_out_s, eta_shift,
                                  inv_mean_eta, oct_mul[0], oct_mul[1], oct_mul[2], oct_mul[3],
                                  nr_t, nc_t};
                [enc setComputePipelineState:ctx.pipes[K_V3_FFT64_SURE]];
                [enc setBuffer:b_z offset:0 atIndex:0];
                [enc setBuffer:b_lut offset:0 atIndex:1];
                [enc setBuffer:b_boost offset:0 atIndex:2];
                [enc setBuffer:b_fcomp_eta offset:0 atIndex:3];
                [enc setBuffer:b_den offset:0 atIndex:4];
                [enc setBuffer:b_dv_tile offset:0 atIndex:5];
                [enc setBytes:&pcff length:sizeof(pcff) atIndex:6];
                [enc dispatchThreadgroups:MTLSizeMake(size_t(ntr_c) * ntc_c, 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            }

            PC_V3VstInv pcvi{H_out, W_out, nr_t, nc_t, full_H_out, origin_out_y, u_min, u_max, inv_du, 0.0f,
                             phase_y, phase_x};
            enc_2d(enc, K_V3_VST_INV, {b_den, b_z, b_dv_tile, b_lut, b_green0}, &pcvi, sizeof(pcvi), W_out, H_out);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal V3 Stage 5A dispatch failed";
                return false;
            }
            device_cost(cb, 3);
        }
        b_boost = nil;

        const bool is_subtile = (job.direct_stitch_rgb2x != nullptr || job.origin_y != 0 || job.origin_x != 0 ||
                                 H1 != full_H1 || W1 != full_W1);
        double mean_g = 0.0;
        if (is_subtile && job.global_mean_g >= 0.0) {
            mean_g = job.global_mean_g;
        } else {
            const float* pg = (const float*)[b_green0 contents];
            double acc = 0.0;
            for (size_t i = 0; i < n_out; i += 16) acc += std::max(0.0f, pg[i]);
            mean_g = acc / double((n_out + 15) / 16);
        }
        cost_mark(3);

        // --- CB 4: Stage 5B Chroma Pyramid + Signed RGB Assembly ---
        const float sigma_c_base = gpu_stage5_chroma_sigma(job, eta_eff, mean_g);
        job.out_resid_noise_2x = 0.35f * float(std::sqrt(std::max(0.0, sig2 + gain * mean_g)));

        id<MTLBuffer> b_outG = b_z;
        id<MTLBuffer> b_outB = b_den;

        {
            const bool run_chroma = (job.chroma_denoise > 1e-4f && sigma_c_base > 1e-7f);
            id<MTLBuffer> b_mR = nil, b_mB = nil;
            id<MTLBuffer> b_gpyr[4] = {};
            id<MTLBuffer> b_rlap[4] = {}, b_blap[4] = {};
            id<MTLBuffer> b_r_aux[4] = {}, b_b_aux[4] = {};
            int lh[4] = {H1, 0, 0, 0}, lw[4] = {W1, 0, 0, 0};

            if (run_chroma) {
                b_mR = ctx.acquire_buf(14, peak_1x * sizeof(float));
                b_mB = ctx.acquire_buf(15, peak_1x * sizeof(float));
                b_gpyr[0] = (out_scale == 2) ? ctx.acquire_buf(17, peak_1x * sizeof(float)) : b_green0;
                for (int lv = 0; lv < 4; ++lv) {
                    if (lv > 0) {
                        lh[lv] = std::max(1, (lh[lv - 1] + 1) / 2);
                        lw[lv] = std::max(1, (lw[lv - 1] + 1) / 2);
                    }
                    const size_t sz_lv = std::max<size_t>(size_t(lh[lv]) * lw[lv], peak_1x >> (2 * lv));
                    if (lv > 0) b_gpyr[lv] = ctx.acquire_buf(17 + lv, sz_lv * sizeof(float));
                    b_rlap[lv]  = ctx.acquire_buf(21 + lv, sz_lv * sizeof(float));
                    b_blap[lv]  = ctx.acquire_buf(25 + lv, sz_lv * sizeof(float));
                    b_r_aux[lv] = ctx.acquire_buf(29 + lv, sz_lv * sizeof(float));
                    b_b_aux[lv] = ctx.acquire_buf(33 + lv, sz_lv * sizeof(float));
                    if (!b_gpyr[lv] || !b_rlap[lv] || !b_blap[lv] || !b_r_aux[lv] || !b_b_aux[lv]) {
                        job.out_err = "Metal V3 chroma pyramid allocation failed";
                        return false;
                    }
                }
                if (!b_mR || !b_mB) {
                    job.out_err = "Metal V3 chroma median allocation failed";
                    return false;
                }
            }

            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            if (run_chroma) {
                if (out_scale == 2) {
                    PC_V3Up2Lap pcext{H1, W1, H1, W1, 2};
                    enc_2d(enc, K_V3_UP2_LAP, {b_green0, b_green0, b_gpyr[0]}, &pcext, sizeof(pcext), W1, H1);
                }

                PC_Size2D pcs1{H1, W1};
                enc_2d(enc, K_MEDIAN3X3, {b_dR, b_mR}, &pcs1, sizeof(pcs1), W1, H1);
                enc_2d(enc, K_MEDIAN3X3, {b_dB, b_mB}, &pcs1, sizeof(pcs1), W1, H1);
                PC_V3ImpClamp pcic{uint32_t(n_1x), 3.5f * sigma_c_base + 0.002f, 0.0f, 0.0f};
                enc_1d(enc, K_V3_IMPULSE_CLAMP, {b_dR, b_dB, b_mR, b_mB}, &pcic, sizeof(pcic), uint32_t(n_1x));

                if (job.glint_desat) {
                    PC_Gauss1D g2h = make_gauss_pc(H1, W1, 0, 2.0f);
                    PC_Gauss1D g2v = make_gauss_pc(H1, W1, 1, 2.0f);
                    enc_2d(enc, K_GAUSS_1D, {b_gpyr[0], b_mR}, &g2h, sizeof(g2h), W1, H1);
                    enc_2d(enc, K_GAUSS_1D, {b_mR, b_mB}, &g2v, sizeof(g2v), W1, H1);
                    enc_2d(enc, K_GLINT_DESAT, {b_gpyr[0], b_mB, b_dR, b_dB}, &pcs1, sizeof(pcs1), W1, H1);
                }

                id<MTLBuffer> r_cur = b_dR;
                id<MTLBuffer> b_cur = b_dB;
                for (int lv = 0; lv < 3; ++lv) {
                    PC_V3Down2 pcd{lh[lv], lw[lv], lh[lv + 1], lw[lv + 1]};
                    enc_2d(enc, K_V3_DOWN2, {b_gpyr[lv], b_gpyr[lv + 1]}, &pcd, sizeof(pcd), lw[lv + 1], lh[lv + 1]);

                    id<MTLBuffer> r_next = (lv < 2) ? b_r_aux[lv + 1] : b_rlap[3];
                    id<MTLBuffer> b_next = (lv < 2) ? b_b_aux[lv + 1] : b_blap[3];
                    enc_2d(enc, K_V3_DOWN2, {r_cur, r_next}, &pcd, sizeof(pcd), lw[lv + 1], lh[lv + 1]);
                    enc_2d(enc, K_V3_DOWN2, {b_cur, b_next}, &pcd, sizeof(pcd), lw[lv + 1], lh[lv + 1]);

                    PC_V3Up2Lap pclap{lh[lv], lw[lv], lh[lv + 1], lw[lv + 1], 0};
                    enc_2d(enc, K_V3_UP2_LAP, {r_cur, r_next, b_rlap[lv]}, &pclap, sizeof(pclap), lw[lv], lh[lv]);
                    enc_2d(enc, K_V3_UP2_LAP, {b_cur, b_next, b_blap[lv]}, &pclap, sizeof(pclap), lw[lv], lh[lv]);
                    r_cur = r_next;
                    b_cur = b_next;
                }

                const float edge_base = (job.global_edge_p80 >= 0.0f)
                    ? std::max(job.is_astro ? 0.14f : 3.0f * std::max(1e-4f, 0.5f * sigma_c_base),
                               2.2f * job.global_edge_p80 + 0.006f)
                    : std::max(0.015f, 4.0f * sigma_c_base);

                for (int lv = 3; lv >= 0; --lv) {
                    if (lh[lv] >= 2 && lw[lv] >= 2) {
                        const float sig_lv = sigma_c_base * std::pow(0.55f, float(lv)) * 0.85f;
                        const float edge_lv = std::max(1e-4f, edge_base * std::pow(0.85f, float(lv)));
                        const float thr_z = (lv < 3) ? (1.25f * sig_lv) : 0.0f;
                        PC_V3OddHuber pcoh{lh[lv], lw[lv], std::max(1e-6f, 2.2f * sig_lv),
                                           1.0f / (2.0f * edge_lv * edge_lv), thr_z};
                        enc_2d(enc, K_V3_ODD_HUBER, {b_rlap[lv], b_gpyr[lv], b_r_aux[lv]}, &pcoh, sizeof(pcoh), lw[lv], lh[lv]);
                        enc_2d(enc, K_V3_ODD_HUBER, {b_blap[lv], b_gpyr[lv], b_b_aux[lv]}, &pcoh, sizeof(pcoh), lw[lv], lh[lv]);
                    } else {
                        b_r_aux[lv] = b_rlap[lv];
                        b_b_aux[lv] = b_blap[lv];
                    }
                }

                id<MTLBuffer> r_rec = b_r_aux[3];
                id<MTLBuffer> b_rec = b_b_aux[3];
                for (int lv = 2; lv >= 0; --lv) {
                    id<MTLBuffer> dst_r = (lv == 0) ? b_dR : b_rlap[lv];
                    id<MTLBuffer> dst_b = (lv == 0) ? b_dB : b_blap[lv];
                    PC_V3Up2Lap pcrec{lh[lv], lw[lv], lh[lv + 1], lw[lv + 1], 1};
                    enc_2d(enc, K_V3_UP2_LAP, {b_r_aux[lv], r_rec, dst_r}, &pcrec, sizeof(pcrec), lw[lv], lh[lv]);
                    enc_2d(enc, K_V3_UP2_LAP, {b_b_aux[lv], b_rec, dst_b}, &pcrec, sizeof(pcrec), lw[lv], lh[lv]);
                    r_rec = dst_r;
                    b_rec = dst_b;
                }
            }

            const float sig_g_up = std::max(0.018f, 3.5f * job.out_resid_noise_2x);
            PC_V3Assemble pca{H1, W1, H_out, W_out, out_scale,
                              1.0f / std::max(1e-4f, job.wb_r), 1.0f / std::max(1e-4f, job.wb_b),
                              0.5f / (sig_g_up * sig_g_up)};
            enc_2d(enc, K_V3_ASSEMBLE_RGB, {b_green0, b_dR, b_dB, b_outR, b_outG, b_outB}, &pca, sizeof(pca), W_out, H_out);

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal V3 assembly dispatch failed";
                return false;
            }
            device_cost(cb, 4);
        }
        b_dR = nil; b_dB = nil;
        cost_mark(4);

        // --- CB 5: Optional Non-Deferred PSF Richardson-Lucy Deconvolution ---
        if (!job.defer_optical_psf && job.psf_iters > 0 && job.rl_mod_f16) {
            id<MTLBuffer> b_est   = ctx.acquire_buf(5, peak_out * sizeof(float));
            id<MTLBuffer> b_conv  = ctx.acquire_buf(6, peak_out * sizeof(float));
            id<MTLBuffer> b_gate  = ctx.acquire_buf(7, peak_out * sizeof(float));
            id<MTLBuffer> b_bnds  = ctx.acquire_buf(8, peak_out * sizeof(uint32_t));
            const int Hq = std::max(1, H1 / 4), Wq = std::max(1, W1 / 4);
            const int mod_h = (out_scale == 2) ? Hh : Hq;
            const int mod_w = (out_scale == 2) ? Wh : Wq;
            id<MTLBuffer> b_rlmod = ctx.acquire_buf(10, std::max<size_t>(1, size_t(mod_h) * mod_w) * sizeof(uint32_t));
            if (!b_est || !b_conv || !b_gate || !b_bnds || !b_rlmod) {
                job.out_err = "Metal V3 RL allocation failed";
                return false;
            }
            if (out_scale == 2) {
                std::memcpy([b_rlmod contents], job.rl_mod_f16, n_half * sizeof(uint32_t));
            } else {
                uint32_t* dst_rl = (uint32_t*)[b_rlmod contents];
                for (int r = 0; r < Hq; ++r) {
                    for (int c = 0; c < Wq; ++c) {
                        dst_rl[size_t(r) * Wq + c] = gpu_downsample_rlmod_q(job.rl_mod_f16, Hh, Wh, r, c);
                    }
                }
            }

            const int it_use = (out_scale == 2) ? job.psf_iters : std::max(1, (job.psf_iters + 1) / 2 + 1);
            const float sig_use = (out_scale == 2) ? job.psf_sigma : std::max(0.42f, 0.56f * job.psf_sigma);
            const float rl_cap = (out_scale == 2) ? 0.18f : 0.15f;

            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            PC_Zero pcn{uint32_t(n_out)};
            enc_1d(enc, K_RL_LUM, {b_outR, b_outG, b_outB, b_green0}, &pcn, sizeof(pcn), uint32_t(n_out));

            PC_Gauss1D g065h = make_gauss_pc(H_out, W_out, 0, 0.65f);
            PC_Gauss1D g065v = make_gauss_pc(H_out, W_out, 1, 0.65f);
            enc_2d(enc, K_GAUSS_1D, {b_green0, b_est}, &g065h, sizeof(g065h), W_out, H_out);
            enc_2d(enc, K_GAUSS_1D, {b_est, b_conv}, &g065v, sizeof(g065v), W_out, H_out);

            float g_thr = std::max(0.0009f, 2.2f * job.out_resid_noise_2x);
            PC_RlPrepH pcph{H_out, W_out, mod_h, mod_w, g_thr * g_thr};
            enc_2d(enc, K_RL_PREP_H, {b_green0, b_conv, b_gate, b_est, b_rlmod}, &pcph, sizeof(pcph), W_out, H_out);
            PC_RlPrepV pcpv{H_out, W_out, job.is_astro ? 1 : 0};
            enc_2d(enc, K_RL_PREP_V, {b_green0, b_est, b_bnds, b_conv}, &pcpv, sizeof(pcpv), W_out, H_out);
            std::swap(b_est, b_conv);

            PC_Gauss1D g10h = make_gauss_pc(H_out, W_out, 0, 1.0f);
            PC_Gauss1D g10v = make_gauss_pc(H_out, W_out, 1, 1.0f);
            enc_2d(enc, K_GAUSS_1D, {b_gate, b_green0}, &g10h, sizeof(g10h), W_out, H_out);
            enc_2d(enc, K_GAUSS_1D, {b_green0, b_gate}, &g10v, sizeof(g10v), W_out, H_out);

            PC_Gauss1D gpsfh = make_olpf_psf_pc(H_out, W_out, 0, sig_use, job.olpf_mode);
            PC_Gauss1D gpsfv = make_olpf_psf_pc(H_out, W_out, 1, sig_use, job.olpf_mode);
            float snr_knee = std::max(0.003f, 3.2f * job.out_resid_noise_2x);
            PC_RlRatio pcrr{uint32_t(n_out), snr_knee, rl_cap};

            for (int it = 0; it < it_use; ++it) {
                enc_2d(enc, K_GAUSS_1D, {b_est, b_green0}, &gpsfh, sizeof(gpsfh), W_out, H_out);
                enc_2d(enc, K_GAUSS_1D, {b_green0, b_conv}, &gpsfv, sizeof(gpsfv), W_out, H_out);
                enc_1d(enc, K_RL_RATIO, {b_outR, b_outG, b_outB, b_gate, b_conv}, &pcrr, sizeof(pcrr), uint32_t(n_out));
                enc_2d(enc, K_GAUSS_1D, {b_conv, b_green0}, &gpsfh, sizeof(gpsfh), W_out, H_out);
                enc_2d(enc, K_GAUSS_1D, {b_green0, b_conv}, &gpsfv, sizeof(gpsfv), W_out, H_out);
                enc_1d(enc, K_RL_UPDATE, {b_est, b_conv, b_bnds}, &pcn, sizeof(pcn), uint32_t(n_out));
            }
            enc_1d(enc, K_RL_APPLY, {b_outR, b_outG, b_outB, b_est}, &pcn, sizeof(pcn), uint32_t(n_out));

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal V3 RL dispatch failed";
                return false;
            }
            device_cost(cb, 5);
        }
        cost_mark(5);

        // Interleave planar R, G, B (and copy fcomp if requested) with core-only stitch when tiled
        const float* pR = (const float*)[b_outR contents];
        const float* pG = (const float*)[b_outG contents];
        const float* pB = (const float*)[b_outB contents];
        const float* pFc = need_fcomp ? (const float*)[b_fcomp_eta contents] : nullptr;

        const int sr0_base = job.direct_stitch_rgb2x ? (job.core_y0 * out_scale) : 0;
        const int sc0_base = job.direct_stitch_rgb2x ? (job.core_x0 * out_scale) : 0;
        const int s_rows   = job.direct_stitch_rgb2x ? ((job.core_y1 - job.core_y0) * out_scale) : H_out;
        const int s_cols   = job.direct_stitch_rgb2x ? ((job.core_x1 - job.core_x0) * out_scale) : W_out;
        float* out_base    = job.direct_stitch_rgb2x ? job.direct_stitch_rgb2x : job.rgb_2x_out;
        float* fc_base     = job.direct_stitch_fcomp ? job.direct_stitch_fcomp : job.fisher_comp_out;
        const int out_str  = job.direct_stitch_rgb2x ? job.direct_w2_stride : W_out;
        const int out_y0   = job.direct_stitch_rgb2x ? job.direct_y2 : 0;
        const int out_x0   = job.direct_stitch_rgb2x ? job.direct_x2 : 0;
        const int nt = std::max(1, job.n_threads);
        if (s_rows > 0 && s_cols > 0 && out_base) {
            std::vector<heraw_sync::JoiningThread> workers;
            const int rows_per = (s_rows + nt - 1) / nt;
            for (int t = 0; t < nt; ++t) {
                const int r0 = t * rows_per;
                const int r1 = std::min(s_rows, r0 + rows_per);
                if (r0 >= r1) break;
                workers.emplace_back([=]() {
                    for (int r = r0; r < r1; ++r) {
                        const size_t i0 = size_t(sr0_base + r) * W_out + sc0_base;
                        float* dst = out_base + (size_t(out_y0 + r) * out_str + out_x0) * 3;
                        for (int c = 0; c < s_cols; ++c) {
                            dst[c * 3 + 0] = pR[i0 + c];
                            dst[c * 3 + 1] = pG[i0 + c];
                            dst[c * 3 + 2] = pB[i0 + c];
                        }
                        if (pFc && fc_base) {
                            std::copy_n(pFc + i0, s_cols, fc_base + size_t(out_y0 + r) * out_str + out_x0);
                        }
                    }
                });
            }
        }
        cost_mark(6);
        return true;
    }
}

bool run_stage45_gpu(Stage45GpuJob& job) {
    @autoreleasepool {
        auto& ctx = global_metal();
        if (!ctx.ensure_init()) { job.out_err = ctx.init_err; return false; }
        std::lock_guard<std::mutex> run_lk(ctx.run_mtx);
        struct ArenaGuard {
            MetalContext& c;
            ~ArenaGuard() { @autoreleasepool { c.clear_arena(); } }
        } guard{ctx};
        const size_t device_budget = size_t(ctx.dev.recommendedMaxWorkingSetSize) / (job.math_v3_unified ? 2 : 4);
        job.can_direct_stitch = true;
        if (job.math_v3_unified) {
            const size_t out_s2 = job.is_1x_mode ? 1 : 4;
            const size_t atlas_div = std::max<size_t>(16, size_t(job.N) * 4);
            const size_t per_px_budget = std::max<size_t>(16, size_t(job.N) * 8 + out_s2 * 16 + 24);
            job.max_tile_pixels = std::min({
                job.max_tile_pixels,
                size_t(ctx.dev.maxBufferLength) / atlas_div,
                device_budget / per_px_budget
            });
            if (job.tile_size_1x == 0 && size_t(job.H1) * size_t(job.W1) > job.max_tile_pixels) {
                job.tile_size_1x = 3600;
            }
            return run_stage45_streamed(job, run_stage45_gpu_v3_full, true);
        }
        job.max_tile_pixels = std::min({job.max_tile_pixels, size_t(ctx.dev.maxBufferLength) / 16, device_budget / 128});
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
                std::vector<heraw_sync::JoiningThread> workers;
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
                std::vector<heraw_sync::JoiningThread> workers;
                workers.reserve(p_nt);
                const size_t ch_p = (pixels + p_nt - 1) / p_nt;
                for (int t = 0; t < p_nt; ++t) {
                    workers.emplace_back([=]() {
                        const size_t a0 = size_t(t) * ch_p, a1 = std::min(pixels, a0 + ch_p);
                        if (a0 < a1) std::memcpy(dst_res + a0, src_res + a0, (a1 - a0) * sizeof(float));
                    });
                }
            }
        }
        return true;
    }
}

bool run_m5a_gls_gpu(M5aGlsGpuJob& job) {
    if (!job.rgb || !job.raw || job.raw->empty() || job.H1 < 2 || job.W1 < 2 ||
        (job.H1 & 1) || (job.W1 & 1) || job.N <= 0 || int(job.raw->size()) != job.N) {
        job.out_err = "Invalid M5-A GLS input dimensions";
        return false;
    }
    if (job.out_scale != 1 && job.out_scale != 2) {
        job.out_err = "Invalid M5-A out_scale";
        return false;
    }
    @autoreleasepool {
        auto& ctx = global_metal();
        if (!ctx.ensure_init()) {
            job.out_err = ctx.init_err;
            return false;
        }
        std::lock_guard<std::mutex> run_lk(ctx.run_mtx);

        const int H1 = job.H1, W1 = job.W1, N = job.N;
        const int Hh = H1 / 2, Wh = W1 / 2;
        const int scale = job.out_scale;
        const int H_out = H1 * scale, W_out = W1 * scale;
        const int tile = job.tile > 0 ? std::max(16, std::min(128, job.tile))
            : std::min(96, std::max(16, std::min(H_out, W_out) / 2));
        const int nr = (H_out + tile - 1) / tile;
        const int nc = (W_out + tile - 1) / tile;
        const int iterations = std::max(0, job.iterations);

        std::vector<std::array<float, 4>> bounds((size_t)N, {0.0f, 0.0f, 0.0f, 0.0f});
        std::vector<uint8_t> is_const_shift((size_t)N, 1);
        std::vector<uint8_t> frame_unit_mw((size_t)N, 1);
        std::vector<uint8_t> frame_all_valid((size_t)N, 0);
        std::vector<float> soft_mul((size_t)N, 1.0f);

        if (job.fy && !job.fy->empty() && job.fx && !job.fx->empty()) {
            for (int n = 0; n < N; ++n) {
                if (n >= int(job.fy->size()) || (*job.fy)[n].empty()) continue;
                const auto& fyn = (*job.fy)[n];
                const auto& fxn = (*job.fx)[n];
                float ymn = 2.0f * fyn[0], ymx = ymn, xmn = 2.0f * fxn[0], xmx = xmn;
                for (size_t i = 0; i < fyn.size(); ++i) {
                    const float dy = 2.0f * fyn[i], dx = 2.0f * fxn[i];
                    if (!std::isfinite(dy) || !std::isfinite(dx)) {
                        job.out_err = "Non-finite flow field in M5-A GPU";
                        return false;
                    }
                    ymn = std::min(ymn, dy); ymx = std::max(ymx, dy);
                    xmn = std::min(xmn, dx); xmx = std::max(xmx, dx);
                }
                bounds[n] = {ymn, ymx, xmn, xmx};
                is_const_shift[n] = (ymn == ymx && xmn == xmx) ? 1 : 0;
            }
        }
        if (job.frame_sharp_scores && int(job.frame_sharp_scores->size()) >= N) {
            float s_max = 0.0f;
            for (int n = 0; n < N; ++n) s_max = std::max(s_max, (*job.frame_sharp_scores)[n]);
            if (s_max > 1e-6f) {
                for (int n = 0; n < N; ++n) {
                    const float sn = std::max(1e-6f, (*job.frame_sharp_scores)[n]);
                    soft_mul[n] = std::max(1.0f, std::min(1.6f, s_max / sn));
                }
            }
        }
        if (job.optical_psf_sigma_1x > 0.12f) {
            const float opt_mul = std::min(2.2f, std::hypot(1.0f, 1.25f * job.optical_psf_sigma_1x));
            for (float& sm : soft_mul) sm *= opt_mul;
        }
        for (int n = 0; n < N; ++n) {
            if (job.mw && n < int(job.mw->size())) {
                for (float m : (*job.mw)[n]) {
                    if (!(m >= 1.0f)) { frame_unit_mw[n] = 0; break; }
                }
            }
            const float sat_n = job.frame_saturation_levels ? job.frame_saturation_levels[n]
                                                            : std::numeric_limits<float>::infinity();
            bool ok_all = true;
            const auto& rn = (*job.raw)[n];
            for (size_t i = 0; i < rn.size(); ++i) {
                if (!std::isfinite(rn[i]) || !(rn[i] < sat_n)) { ok_all = false; break; }
            }
            frame_all_valid[n] = ok_all ? 1 : 0;
        }

        float max_opt_shift = 0.0f;
        if (job.frame_ois_shifts) {
            for (float s : *job.frame_ois_shifts) max_opt_shift = std::max(max_opt_shift, std::fabs(s));
        }
        const float max_lca = std::max(std::fabs(job.lca_r_k1) + std::fabs(job.lca_r_k2),
                                       std::fabs(job.lca_b_k1) + std::fabs(job.lca_b_k2));
        const float margin_1x = 5.0f + max_opt_shift + 2.0f * max_lca * float(std::max(H1, W1));

        const size_t max_ssbo = std::min<size_t>(ctx.dev.maxBufferLength, 256ULL << 20);
        const int max_rows_gm = std::max(tile, int(max_ssbo / size_t(std::max(1, W_out * 24))));
        const int nr_band_cap = std::max(1, std::min({nr, 8, max_rows_gm / tile}));
        const int max_band_h = std::min(H_out, nr_band_cap * tile);
        const int max_rgb_in_h = std::min(H_out, max_band_h + 12);
        const size_t gm_bytes = size_t(max_band_h) * W_out * 6 * sizeof(int32_t);
        const size_t rgb_in_bytes = size_t(max_rgb_in_h) * W_out * 3 * sizeof(float);
        const size_t rgb_out_bytes = size_t(max_band_h) * W_out * 3 * sizeof(float);
        const size_t tile_stats_bytes = size_t(nr_band_cap) * nc * 136 * sizeof(uint32_t);
        const size_t params_bytes = size_t(48 + N * 16) * sizeof(float);
        const size_t raw_full_bytes = size_t(H1) * W1 * sizeof(float);
        const size_t flow_full_bytes = size_t(std::max(1, Hh)) * std::max(1, Wh) * 4 * sizeof(float);
        const int wg_x = (W_out + 15) / 16;
        const int max_wg_y = (max_band_h + 15) / 16;
        const size_t wg_stats_bytes = size_t(wg_x) * max_wg_y * 2 * sizeof(float);
        const size_t fcomp_bytes = size_t(max_band_h) * W_out * sizeof(float);

        if (gm_bytes > ctx.dev.maxBufferLength || rgb_in_bytes > ctx.dev.maxBufferLength ||
            raw_full_bytes > ctx.dev.maxBufferLength) {
            job.out_err = "Metal maxBufferLength too small for M5-A";
            return false;
        }

        id<MTLBuffer> b_params = ctx.acquire_buf(0, params_bytes);
        id<MTLBuffer> b_rgb_in = ctx.acquire_buf(1, rgb_in_bytes);
        id<MTLBuffer> b_rgb_out = ctx.acquire_buf(2, rgb_out_bytes);
        id<MTLBuffer> b_gm = ctx.acquire_buf(3, gm_bytes);
        id<MTLBuffer> b_tstats = ctx.acquire_buf(4, tile_stats_bytes);
        id<MTLBuffer> b_wstats = ctx.acquire_buf(5, wg_stats_bytes);
        id<MTLBuffer> b_fcomp = ctx.acquire_buf(6, fcomp_bytes);
        id<MTLBuffer> b_raw = ctx.acquire_buf(7, raw_full_bytes);
        id<MTLBuffer> b_flow = ctx.acquire_buf(8, flow_full_bytes);
        if (!b_params || !b_rgb_in || !b_rgb_out || !b_gm || !b_tstats ||
            !b_wstats || !b_fcomp || !b_raw || !b_flow) {
            job.out_err = "Metal M5-A buffer allocation failed";
            return false;
        }

        float min_rv = job.read_variance;
        if (job.frame_read_variances) {
            for (int n = 0; n < N; ++n) min_rv = std::min(min_rv, job.frame_read_variances[n]);
        }
        const float v_scale = std::max(1e-12f, min_rv + 0.02f * job.shot);
        uint32_t q_div = 1u;
        while (q_div * 16u < uint32_t(std::max(1, N))) q_div <<= 1;
        const float q_scale = 1.0f / float(q_div);
        const float cy_opt = 0.5f * float(H1), cx_opt = 0.5f * float(W1);
        const float inv_ropt = 1.0f / std::max(1.0f, std::max(cy_opt, cx_opt));

        {
            float* p = (float*)[b_params contents];
            std::memset(p, 0, params_bytes);
            p[0] = job.lca_r_k1; p[1] = job.lca_r_k2;
            p[2] = job.lca_b_k1; p[3] = job.lca_b_k2;
            p[4] = cy_opt; p[5] = cx_opt; p[6] = inv_ropt;
            for (int r = 0; r < 6; ++r)
                for (int c = 0; c < 6; ++c) p[8 + r * 6 + c] = float(job.cfa6x6[r][c]);
            for (int n = 0; n < N; ++n) {
                float* fp = p + 48 + n * 16;
                const float rv_n = job.frame_read_variances ? job.frame_read_variances[n] : job.read_variance;
                const float sh_n = job.frame_shot_coefficients ? job.frame_shot_coefficients[n] : job.shot;
                const bool has_sat = job.frame_saturation_levels && std::isfinite(job.frame_saturation_levels[n]);
                const float sat_n = has_sat ? job.frame_saturation_levels[n] : 0.0f;
                const bool has_fl = job.fy && job.fx && n < int(job.fy->size()) && !(*job.fy)[n].empty();
                const float tobit_n_scale = (N >= 2) ? (1.0f / float(N)) : 1.0f;
                const float tobit_tau = job.wavelet_tobit_deadzone
                    ? tobit_n_scale * (job.wavelet_deadzone_halfwidth > 0.0f
                          ? job.wavelet_deadzone_halfwidth
                          : 1.5f * std::sqrt(std::max(1e-12f, rv_n)))
                    : 0.0f;
                fp[0] = rv_n; fp[1] = sh_n; fp[2] = sat_n;
                fp[3] = frame_all_valid[n] ? 1.0f : 0.0f;
                fp[4] = frame_unit_mw[n] ? 1.0f : 0.0f;
                fp[5] = has_fl ? 1.0f : 0.0f;
                fp[6] = is_const_shift[n] ? 1.0f : 0.0f;
                fp[7] = bounds[n][0]; fp[8] = bounds[n][2];
                fp[9] = 1.0f / std::max(1.0f, soft_mul[n]);
                fp[10] = 0.5f * tobit_tau * tobit_tau;
                if (job.frame_ois_shifts && int(job.frame_ois_shifts->size()) >= (n + 1) * 4) {
                    fp[11] = (*job.frame_ois_shifts)[n * 4 + 0];
                    fp[12] = (*job.frame_ois_shifts)[n * 4 + 1];
                    fp[13] = (*job.frame_ois_shifts)[n * 4 + 2];
                    fp[14] = (*job.frame_ois_shifts)[n * 4 + 3];
                }
                fp[15] = has_sat ? 1.0f : 0.0f;
            }
        }

        const int n_batches = (nr + nr_band_cap - 1) / nr_band_cap;
        const size_t row_floats = size_t(W_out) * 3;
        std::vector<float> seam_prev4(6 * row_floats, 0.0f);

        auto load_rgb_band = [&](int band_y0, int rgb_in_y0, int rgb_in_h, int seam_rows) {
            float* dst = (float*)[b_rgb_in contents];
            const int seam_copy = band_y0 - rgb_in_y0;
            if (seam_copy > 0) {
                const int src_row = std::max(0, seam_rows - seam_copy);
                std::memcpy(dst, seam_prev4.data() + size_t(src_row) * row_floats, size_t(seam_copy) * row_floats * sizeof(float));
            }
            const int tail_rows = rgb_in_h - seam_copy;
            if (tail_rows > 0) {
                std::memcpy(dst + size_t(seam_copy) * row_floats,
                            job.rgb + size_t(band_y0) * row_floats,
                            size_t(tail_rows) * row_floats * sizeof(float));
            }
        };

        auto slice_bounds = [&](int n, int band_y0, int band_h, int& sy0, int& sy1, int& hy0, int& hy1) {
            const float r_lo = float(band_y0 - 3) / float(scale);
            const float r_hi = float(band_y0 + band_h + 2) / float(scale);
            const float yb_lo = r_lo - margin_1x, yb_hi = r_hi + margin_1x;
            hy0 = std::max(0, std::min(Hh - 1, int(std::floor(yb_lo * 0.5f)) - 2));
            hy1 = std::max(hy0, std::min(Hh - 1, int(std::ceil(yb_hi * 0.5f)) + 2));
            sy0 = std::max(0, std::min(H1 - 1, int(std::floor(yb_lo + bounds[n][0])) - 2));
            sy1 = std::max(sy0, std::min(H1 - 1, int(std::ceil(yb_hi + bounds[n][1])) + 2));
        };

        auto run_scatter_band = [&](int band_y0, int band_h, int tr_b0, int nr_band,
                                    int rgb_in_y0, int rgb_in_h, bool is_it0, bool zero_gm_only) -> bool {
            std::memset([b_tstats contents], 0, size_t(nr_band) * nc * 136 * sizeof(uint32_t));
            if (!zero_gm_only) std::memset([b_gm contents], 0, size_t(band_h) * W_out * 6 * sizeof(int32_t));

            for (int n = 0; n < N; ++n) {
                int sy0, sy1, hy0, hy1;
                slice_bounds(n, band_y0, band_h, sy0, sy1, hy0, hy1);
                const int sy_cnt = sy1 - sy0 + 1;
                const int hy_cnt = hy1 - hy0 + 1;
                std::memcpy([b_raw contents], (*job.raw)[n].data() + size_t(sy0) * W1, size_t(sy_cnt) * W1 * sizeof(float));

                const bool has_fl = job.fy && job.fx && n < int(job.fy->size()) && !(*job.fy)[n].empty();
                if (has_fl || !frame_unit_mw[n]) {
                    float* fdst = (float*)[b_flow contents];
                    const float* fy = has_fl ? (*job.fy)[n].data() : nullptr;
                    const float* fx = has_fl ? (*job.fx)[n].data() : nullptr;
                    const float* mw = (job.mw && n < int(job.mw->size()) && !(*job.mw)[n].empty()) ? (*job.mw)[n].data() : nullptr;
                    for (int r = 0; r < hy_cnt; ++r) {
                        const size_t src_off = size_t(hy0 + r) * Wh;
                        float* drow = fdst + size_t(r) * Wh * 4;
                        for (int c = 0; c < Wh; ++c) {
                            drow[c * 4 + 0] = fy ? fy[src_off + c] : 0.0f;
                            drow[c * 4 + 1] = fx ? fx[src_off + c] : 0.0f;
                            drow[c * 4 + 2] = mw ? mw[src_off + c] : 1.0f;
                            drow[c * 4 + 3] = 0.0f;
                        }
                    }
                }

                id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                [enc setComputePipelineState:ctx.pipes[K_M5A_SCATTER]];
                [enc setBuffer:b_raw offset:0 atIndex:0];
                [enc setBuffer:b_flow offset:0 atIndex:1];
                [enc setBuffer:b_rgb_in offset:0 atIndex:2];
                [enc setBuffer:b_gm offset:0 atIndex:3];
                [enc setBuffer:b_tstats offset:0 atIndex:4];
                [enc setBuffer:b_params offset:0 atIndex:5];
                PC_M5aScatter pc{
                    H1, W1, Hh, Wh, H_out, W_out, scale, tile,
                    nr, nc, nr_band, tr_b0, band_y0, band_h, rgb_in_y0, rgb_in_h,
                    sy0, sy_cnt, hy0, hy_cnt, n, is_it0 ? 1 : 0, zero_gm_only ? 1 : 0, 0,
                    v_scale, job.quad, q_scale, 0.0f
                };
                [enc setBytes:&pc length:sizeof(pc) atIndex:6];
                [enc dispatchThreadgroups:MTLSizeMake((W1 + 15) / 16, (sy_cnt + 15) / 16, 1)
                    threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                [enc endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                if (cb.status == MTLCommandBufferStatusError) return false;
            }
            return true;
        };

        const float dof_factor = (N >= 2) ? (float(N - 1) / float(N)) : 0.35f;
        const float tau_morozov = dof_factor * job.morozov_target;
        const float k_pi_g = (scale == 1) ? 0.12f : 0.10f;
        const float k_pi_c_coarse = (scale == 1) ? 0.08f : 0.03f;
        const float k_pi_c = (scale == 1) ? 0.45f : 0.20f;

        double total_rms_sq = 0.0;
        size_t total_chg = 0, before_cnt = 0;
        double before_chi2 = 0.0;
        int actual_iters = 0;

        const float sat0_m5a = (job.frame_saturation_levels && std::isfinite(job.frame_saturation_levels[0]) && job.frame_saturation_levels[0] > 0.0f)
            ? job.frame_saturation_levels[0] : 1.0e6f;

        if (iterations == 0) {
            for (int b = 0; b < n_batches; ++b) {
                const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
                const int nr_band = tr_b1 - tr_b0;
                const int band_y0 = tr_b0 * tile;
                const int band_h = std::min(H_out - band_y0, nr_band * tile);
                const int rgb_in_y0 = std::max(0, band_y0 - 6);
                const int rgb_in_h = std::min(H_out, band_y0 + band_h + 6) - rgb_in_y0;
                std::memcpy([b_rgb_in contents], job.rgb + size_t(rgb_in_y0) * row_floats, size_t(rgb_in_h) * row_floats * sizeof(float));
                if (!run_scatter_band(band_y0, band_h, tr_b0, nr_band, rgb_in_y0, rgb_in_h, true, true)) {
                    job.out_err = "Metal M5-A initial chi2 failed";
                    return false;
                }
                const uint32_t* ts = (const uint32_t*)[b_tstats contents];
                for (int t = 0; t < nr_band * nc; ++t) {
                    const uint32_t* tp = ts + t * 136;
                    if (tp[5] > 0) {
                        before_chi2 += (double(tp[4]) * 4294967296.0 + double(tp[3])) / 256.0;
                        before_cnt += tp[5];
                    }
                }
            }
            job.out_before = before_chi2;
            job.out_samples = before_cnt;
        } else {
            for (int it = 0; it < iterations; ++it) {
                int seam_rows = 0;
                size_t iter_chg = 0;
                for (int b = 0; b < n_batches; ++b) {
                    const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
                    const int nr_band = tr_b1 - tr_b0;
                    const int band_y0 = tr_b0 * tile;
                    const int band_h = std::min(H_out - band_y0, nr_band * tile);
                    const int rgb_in_y0 = std::max(0, band_y0 - 6);
                    const int rgb_in_h = std::min(H_out, band_y0 + band_h + 6) - rgb_in_y0;
                    const int wg_y = (band_h + 15) / 16;

                    load_rgb_band(band_y0, rgb_in_y0, rgb_in_h, seam_rows);
                    if (!run_scatter_band(band_y0, band_h, tr_b0, nr_band, rgb_in_y0, rgb_in_h, it == 0, false)) {
                        job.out_err = "Metal M5-A scatter failed";
                        return false;
                    }

                    id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
                    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                    [enc setComputePipelineState:ctx.pipes[K_M5A_UPDATE]];
                    [enc setBuffer:b_rgb_in offset:0 atIndex:0];
                    [enc setBuffer:b_gm offset:0 atIndex:1];
                    [enc setBuffer:b_tstats offset:0 atIndex:2];
                    [enc setBuffer:b_rgb_out offset:0 atIndex:3];
                    [enc setBuffer:b_wstats offset:0 atIndex:4];
                    PC_M5aUpdate u_pc{
                        H_out, W_out, scale, tile, nc, nr_band, tr_b0, band_y0, band_h, rgb_in_y0, wg_x, q_scale,
                        tau_morozov, v_scale, job.read_variance, job.shot, job.quad, k_pi_g, k_pi_c_coarse, k_pi_c,
                        sat0_m5a, job.wb_r, job.wb_b
                    };
                    [enc setBytes:&u_pc length:sizeof(u_pc) atIndex:6];
                    [enc dispatchThreadgroups:MTLSizeMake(wg_x, wg_y, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    if (cb.status == MTLCommandBufferStatusError) {
                        job.out_err = "Metal M5-A update failed";
                        return false;
                    }

                    const int next_seam = std::min(6, band_h);
                    std::memcpy(seam_prev4.data(),
                                job.rgb + size_t(band_y0 + band_h - next_seam) * row_floats,
                                size_t(next_seam) * row_floats * sizeof(float));
                    seam_rows = next_seam;
                    std::memcpy(job.rgb + size_t(band_y0) * row_floats, [b_rgb_out contents], size_t(band_h) * row_floats * sizeof(float));

                    const float* wgs = (const float*)[b_wstats contents];
                    for (int w = 0; w < wg_x * wg_y; ++w) {
                        total_rms_sq += double(wgs[w * 2 + 0]);
                        const size_t dc = size_t(wgs[w * 2 + 1] + 0.5f);
                        total_chg += dc;
                        iter_chg += dc;
                    }
                    if (it == 0) {
                        const uint32_t* ts = (const uint32_t*)[b_tstats contents];
                        for (int t = 0; t < nr_band * nc; ++t) {
                            const uint32_t* tp = ts + t * 136;
                            if (tp[5] > 0) {
                                before_chi2 += (double(tp[4]) * 4294967296.0 + double(tp[3])) / 256.0;
                                before_cnt += tp[5];
                            }
                        }
                    }
                }
                if (it == 0) {
                    job.out_before = before_chi2;
                    job.out_samples = before_cnt;
                }
                if (iter_chg == 0) break;
                ++actual_iters;
            }
        }

        if (job.jacobian_prox_strength > 0.0f && H_out >= 3 && W_out >= 3) {
            const int j_bh = std::min(128, max_band_h);
            const int j_nb = (H_out + j_bh - 1) / j_bh;
            std::vector<float> seam_prev1(row_floats, 0.0f);
            bool has_seam1 = false;
            for (int b = 0; b < j_nb; ++b) {
                const int band_y0 = b * j_bh;
                const int band_h = std::min(j_bh, H_out - band_y0);
                const int rgb_in_y0 = std::max(0, band_y0 - 1);
                const int rgb_in_h = std::min(H_out, band_y0 + band_h + 1) - rgb_in_y0;
                const int wg_y = (band_h + 15) / 16;

                float* st = (float*)[b_rgb_in contents];
                const int top_seam = band_y0 - rgb_in_y0;
                if (top_seam > 0 && has_seam1) std::memcpy(st, seam_prev1.data(), row_floats * sizeof(float));
                std::memcpy(st + size_t(top_seam) * row_floats,
                            job.rgb + size_t(band_y0) * row_floats,
                            size_t(rgb_in_h - top_seam) * row_floats * sizeof(float));
                if (job.fisher_comp) {
                    std::memcpy([b_fcomp contents], job.fisher_comp + size_t(band_y0) * W_out, size_t(band_h) * W_out * sizeof(float));
                }

                id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                [enc setComputePipelineState:ctx.pipes[K_M5A_JACOBIAN]];
                [enc setBuffer:b_rgb_in offset:0 atIndex:0];
                [enc setBuffer:b_fcomp offset:0 atIndex:1];
                [enc setBuffer:b_rgb_out offset:0 atIndex:2];
                [enc setBuffer:b_wstats offset:0 atIndex:3];
                PC_M5aJacobian jpc{
                    H_out, W_out, band_y0, band_h, rgb_in_y0, rgb_in_h, job.fisher_comp ? 1 : 0, wg_x,
                    job.jacobian_prox_strength, job.read_variance, job.shot, job.quad
                };
                [enc setBytes:&jpc length:sizeof(jpc) atIndex:6];
                [enc dispatchThreadgroups:MTLSizeMake(wg_x, wg_y, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                [enc endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                if (cb.status == MTLCommandBufferStatusError) {
                    job.out_err = "Metal M5-A jacobian failed";
                    return false;
                }

                std::memcpy(seam_prev1.data(), job.rgb + size_t(band_y0 + band_h - 1) * row_floats, row_floats * sizeof(float));
                has_seam1 = true;
                std::memcpy(job.rgb + size_t(band_y0) * row_floats, [b_rgb_out contents], size_t(band_h) * row_floats * sizeof(float));
                const float* wgs = (const float*)[b_wstats contents];
                for (int w = 0; w < wg_x * wg_y; ++w) {
                    total_rms_sq += double(wgs[w * 2 + 0]);
                    total_chg += size_t(wgs[w * 2 + 1] + 0.5f);
                }
            }
        }

        if (actual_iters == 0 && !(job.jacobian_prox_strength > 0.0f && total_chg > 0)) {
            job.out_after = job.out_before;
        } else {
            const int b_step = (size_t(H1) * W1 >= 262144 && n_batches >= 4) ? ((n_batches >= 8) ? 4 : 2) : 1;
            double after_chi2 = 0.0;
            size_t after_cnt = 0;
            for (int b = 0; b < n_batches; b += b_step) {
                const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
                const int nr_band = tr_b1 - tr_b0;
                const int band_y0 = tr_b0 * tile;
                const int band_h = std::min(H_out - band_y0, nr_band * tile);
                const int rgb_in_y0 = std::max(0, band_y0 - 6);
                const int rgb_in_h = std::min(H_out, band_y0 + band_h + 6) - rgb_in_y0;
                std::memcpy([b_rgb_in contents], job.rgb + size_t(rgb_in_y0) * row_floats, size_t(rgb_in_h) * row_floats * sizeof(float));
                if (!run_scatter_band(band_y0, band_h, tr_b0, nr_band, rgb_in_y0, rgb_in_h, true, true)) {
                    job.out_err = "Metal M5-A after chi2 failed";
                    return false;
                }
                const uint32_t* ts = (const uint32_t*)[b_tstats contents];
                for (int t = 0; t < nr_band * nc; ++t) {
                    const uint32_t* tp = ts + t * 136;
                    if (tp[5] > 0) {
                        after_chi2 += (double(tp[4]) * 4294967296.0 + double(tp[3])) / 256.0;
                        after_cnt += tp[5];
                    }
                }
            }
            job.out_after = (after_cnt > 0 && after_cnt != job.out_samples)
                ? after_chi2 * (double(job.out_samples) / double(after_cnt))
                : after_chi2;
        }

        job.out_changed = total_chg;
        job.out_rms_update = std::sqrt(total_rms_sq / std::max<size_t>(1, job.out_changed));
        job.out_iterations = actual_iters;
        return true;
    }
}

bool run_m5b_deconv_gpu(M5bDeconvGpuJob& job) {
    if (!job.rgb || job.H <= 0 || job.W <= 0 || !std::isfinite(job.strength) ||
        !std::isfinite(job.read_variance) || !std::isfinite(job.shot) ||
        job.read_variance < 0.0f || job.shot < 0.0f) {
        job.out_err = "Invalid M5-B parameters";
        return false;
    }
    const float str = std::max(0.0f, std::min(1.0f, job.strength));
    std::vector<M5bZoneParam> zones, fb_zones;
    for (const auto& z : job.zones) {
        if (std::isfinite(z.cx) && std::isfinite(z.cy) &&
            std::isfinite(z.sigma_major_px) && std::isfinite(z.sigma_minor_px) &&
            std::isfinite(z.angle_rad) && z.sigma_major_px > 0.0f && z.sigma_minor_px > 0.0f) {
            zones.push_back(z);
        }
    }
    for (const auto& z : job.fallback_zones) {
        if (std::isfinite(z.cx) && std::isfinite(z.cy) &&
            std::isfinite(z.sigma_major_px) && std::isfinite(z.sigma_minor_px) &&
            std::isfinite(z.angle_rad) && z.sigma_major_px > 0.0f && z.sigma_minor_px > 0.0f) {
            fb_zones.push_back(z);
        }
    }
    if (job.psf_iters <= 0 || str <= 0.0f || job.H < 8 || job.W < 8) return true;
    if (!job.zones.empty() && zones.empty() && job.olpf_mode == 0) return true;
    if (job.zones.empty() && job.olpf_mode == 0 && job.psf_sigma_2x <= 0.0f) return true;

    @autoreleasepool {
        auto& ctx = global_metal();
        if (!ctx.ensure_init()) {
            job.out_err = ctx.init_err;
            return false;
        }
        std::lock_guard<std::mutex> run_lk(ctx.run_mtx);

        const int H = job.H, W = job.W;
        constexpr int CORE = 40;
        const int nr = (H + CORE - 1) / CORE;
        const int nc = (W + CORE - 1) / CORE;
        const float sig_tgt = (job.out_scale == 2) ? 0.50f : 0.36f;
        const float psf_sigma_tile = (job.out_scale == 2) ? job.psf_sigma_2x : (0.5f * job.psf_sigma_2x);
        const float max_boost = 1.0f + 0.24f * std::sqrt(float(job.psf_iters)) * str;
        bool has_complex_otf = !zones.empty();
        for (const auto& z : zones) {
            if (!z.otf_re64 || !z.otf_im64) { has_complex_otf = false; break; }
        }
        constexpr size_t kZoneOtfFloats = 3 * 64 * 33 * 2;

        const size_t max_ssbo = std::min<size_t>(ctx.dev.maxBufferLength, 128ULL << 20);
        const int max_in_rows = std::max(64, int(max_ssbo / size_t(std::max(1, W * 12))));
        const int nr_band_cap = std::max(1, std::min({nr, 16, std::max(1, (max_in_rows - 24) / CORE)}));
        const int max_band_h = std::min(H, nr_band_cap * CORE);
        const int max_in_h = std::min(H, std::max(64, max_band_h + 24));
        const size_t row_floats = size_t(W) * 3;
        const size_t in_bytes = size_t(max_in_h) * row_floats * sizeof(float);
        const size_t out_bytes = size_t(max_band_h) * row_floats * sizeof(float);
        const size_t otf_floats = has_complex_otf ? zones.size() * kZoneOtfFloats : 0;
        const size_t fb_zone_off = 132 + zones.size() * 8 + otf_floats;
        const size_t tile_meta_off = fb_zone_off + fb_zones.size() * 8;
        const size_t params_bytes = (tile_meta_off + size_t(nr) * size_t(nc) * 2) * sizeof(float);
        const size_t focus_bytes = (job.focus_half && job.focus_h > 0 && job.focus_w > 0)
            ? size_t(job.focus_h) * job.focus_w * sizeof(float) : 64;
        const size_t tact_bytes = size_t(nr_band_cap) * nc * sizeof(float);
        const int wg_x = (W + 15) / 16, max_wg_y = (max_band_h + 15) / 16;
        const size_t wg_bytes = size_t(wg_x) * max_wg_y * 2 * sizeof(float);

        if (in_bytes > ctx.dev.maxBufferLength || out_bytes > ctx.dev.maxBufferLength || focus_bytes > ctx.dev.maxBufferLength) {
            job.out_err = "Metal maxBufferLength too small for M5-B";
            return false;
        }

        id<MTLBuffer> b_params = ctx.acquire_buf(0, params_bytes);
        id<MTLBuffer> b_focus  = ctx.acquire_buf(1, focus_bytes);
        id<MTLBuffer> b_in     = ctx.acquire_buf(2, in_bytes);
        id<MTLBuffer> b_dec    = ctx.acquire_buf(3, out_bytes);
        id<MTLBuffer> b_tact   = ctx.acquire_buf(4, tact_bytes);
        id<MTLBuffer> b_wstats = ctx.acquire_buf(5, wg_bytes);
        if (!b_params || !b_focus || !b_in || !b_dec || !b_tact || !b_wstats) {
            job.out_err = "Metal M5-B buffer allocation failed";
            return false;
        }

        {
            float* p = (float*)[b_params contents];
            constexpr double two_pi_64 = 6.283185307179586476925286766559 / 64.0;
            for (int k = 0; k < 64; ++k) {
                p[k] = float(std::cos(two_pi_64 * k));
                p[64 + k] = float(std::sin(two_pi_64 * k));
            }
            for (int k = 0; k < 4; ++k) p[128 + k] = job.dir_ident_gate[(size_t)k];
            for (size_t z = 0; z < zones.size(); ++z) {
                float* zp = p + 132 + z * 8;
                zp[0] = zones[z].cx; zp[1] = zones[z].cy;
                zp[2] = zones[z].sigma_major_px; zp[3] = zones[z].sigma_minor_px;
                zp[4] = zones[z].angle_rad;
                zp[5] = zones[z].sigma_rgb_px[0]; zp[6] = zones[z].sigma_rgb_px[1]; zp[7] = zones[z].sigma_rgb_px[2];
                if (has_complex_otf) {
                    float* dst_otf = p + 132 + zones.size() * 8 + z * kZoneOtfFloats;
                    for (int ch = 0; ch < 3; ++ch) {
                        const float* sre = zones[z].otf_re64 + ch * 4096;
                        const float* sim = zones[z].otf_im64 + ch * 4096;
                        float* dch = dst_otf + ch * (64 * 33 * 2);
                        for (int ky = 0; ky < 64; ++ky) {
                            for (int kx = 0; kx < 33; ++kx) {
                                const int sidx = ky * 64 + kx;
                                const int didx = (ky * 33 + kx) * 2;
                                dch[didx + 0] = sre[sidx];
                                dch[didx + 1] = sim[sidx];
                            }
                        }
                    }
                }
            }
            for (size_t z = 0; z < fb_zones.size(); ++z) {
                float* zp = p + fb_zone_off + z * 8;
                zp[0] = fb_zones[z].cx; zp[1] = fb_zones[z].cy;
                zp[2] = fb_zones[z].sigma_major_px; zp[3] = fb_zones[z].sigma_minor_px;
                zp[4] = fb_zones[z].angle_rad;
                zp[5] = fb_zones[z].sigma_rgb_px[0]; zp[6] = fb_zones[z].sigma_rgb_px[1]; zp[7] = fb_zones[z].sigma_rgb_px[2];
            }
            const bool has_half_grid = (job.focus_h > 0 && job.focus_w > 0);
            const float f_scale_y = has_half_grid ? (float(job.focus_h) / float(H)) : 0.0f;
            const float f_scale_x = has_half_grid ? (float(job.focus_w) / float(W)) : 0.0f;
            float* tm = p + tile_meta_off;
            for (int tr = 0; tr < nr; ++tr) {
                const int core_top = tr * CORE, core_bot = std::min(H, core_top + CORE);
                for (int tc = 0; tc < nc; ++tc) {
                    const int core_left = tc * CORE, core_right = std::min(W, core_left + CORE);
                    float tile_n_eff = (job.n_eff_burst >= 0.0f) ? job.n_eff_burst : 8.0f;
                    float tile_phi = 1.0f;
                    if (has_half_grid && (job.n_eff_half != nullptr || job.phi_half != nullptr)) {
                        double ne_sum = 0.0, ph_sum = 0.0;
                        int hm_cnt = 0;
                        const int fy0 = std::max(0, int(std::floor((float(core_top) + 0.5f) * f_scale_y - 0.5f)));
                        const int fy1 = std::min(job.focus_h - 1, int(std::floor((float(core_bot - 1) + 0.5f) * f_scale_y - 0.5f)) + 1);
                        const int fx0 = std::max(0, int(std::floor((float(core_left) + 0.5f) * f_scale_x - 0.5f)));
                        const int fx1 = std::min(job.focus_w - 1, int(std::floor((float(core_right - 1) + 0.5f) * f_scale_x - 0.5f)) + 1);
                        for (int fy_i = fy0; fy_i <= fy1; ++fy_i) {
                            const size_t h_roff = size_t(fy_i) * job.focus_w;
                            for (int fx_i = fx0; fx_i <= fx1; ++fx_i) {
                                if (job.n_eff_half) ne_sum += double(job.n_eff_half[h_roff + fx_i]);
                                if (job.phi_half) ph_sum += double(job.phi_half[h_roff + fx_i]);
                                ++hm_cnt;
                            }
                        }
                        if (hm_cnt > 0) {
                            if (job.n_eff_half) tile_n_eff = float(ne_sum / double(hm_cnt));
                            if (job.phi_half) tile_phi = float(ph_sum / double(hm_cnt));
                        }
                    }
                    const float ne_gate = (job.n_eff_burst >= 0.0f || job.n_eff_half != nullptr)
                        ? std::clamp((tile_n_eff - 1.05f) * (1.0f / 2.95f), 0.0f, 1.0f) : 1.0f;
                    const float ph_gate = (job.out_scale == 2 && job.phi_half != nullptr)
                        ? std::clamp(tile_phi * (1.0f / 0.35f), 0.0f, 1.0f) : 1.0f;
                    const size_t g_ti = size_t(tr) * size_t(nc) + size_t(tc);
                    tm[2 * g_ti + 0] = job.photo_shelf_k * ne_gate * ph_gate;
                    tm[2 * g_ti + 1] = (!job.nlens_tile_mask || job.nlens_tile_mask[g_ti] != 0) ? 1.0f : 0.0f;
                }
            }
            if (job.focus_half && job.focus_h > 0 && job.focus_w > 0) {
                std::memcpy([b_focus contents], job.focus_half, focus_bytes);
            }
        }

        const int has_focus = (job.focus_half && job.focus_h > 0 && job.focus_w > 0 && !job.is_astro) ? 1 : 0;
        const int n_batches = (nr + nr_band_cap - 1) / nr_band_cap;
        auto refl_y_fn = [&](int i) {
            if (H <= 1) return 0;
            while (i < 0 || i >= H) {
                if (i < 0) i = -i;
                if (i >= H) i = 2 * H - 2 - i;
            }
            return i;
        };
        std::vector<float> seam_prev52(52 * row_floats, 0.0f);
        int seam_rows = 0;
        double total_rms_sq = 0.0;
        size_t total_chg = 0;

        for (int b = 0; b < n_batches; ++b) {
            const int tr_b0 = b * nr_band_cap, tr_b1 = std::min(nr, tr_b0 + nr_band_cap);
            const int nr_band = tr_b1 - tr_b0;
            const int band_y0 = tr_b0 * CORE;
            const int band_h = std::min(H - band_y0, nr_band * CORE);
            const int last_tile_top = band_y0 + (nr_band - 1) * CORE;
            const int refl_y = (last_tile_top + 51 >= H) ? refl_y_fn(last_tile_top + 51) : band_y0;
            const int in_y0 = std::max(0, std::min(band_y0 - 12, refl_y));
            const int in_h = std::min(H, band_y0 + band_h + 12) - in_y0;
            const int wg_y = (band_h + 15) / 16;

            float* dst_in = (float*)[b_in contents];
            const int seam_copy = std::min(seam_rows, band_y0 - in_y0);
            if (seam_copy > 0) {
                const int src_row = std::max(0, seam_rows - seam_copy);
                std::memcpy(dst_in, seam_prev52.data() + size_t(src_row) * row_floats, size_t(seam_copy) * row_floats * sizeof(float));
            }
            const int tail_rows = in_h - seam_copy;
            if (tail_rows > 0) {
                std::memcpy(dst_in + size_t(seam_copy) * row_floats,
                            job.rgb + size_t(band_y0) * row_floats,
                            size_t(tail_rows) * row_floats * sizeof(float));
            }

            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:ctx.pipes[K_M5B_FFT64]];
            [enc setBuffer:b_in offset:0 atIndex:0];
            [enc setBuffer:b_params offset:0 atIndex:1];
            [enc setBuffer:b_focus offset:0 atIndex:2];
            [enc setBuffer:b_dec offset:0 atIndex:3];
            [enc setBuffer:b_tact offset:0 atIndex:4];
            PC_M5bFft fpc{
                H, W, band_y0, band_h, in_y0, in_h, nr_band, nc,
                job.out_scale, job.olpf_mode, job.is_astro ? 1 : 0, int(zones.size()),
                has_focus ? job.focus_h : 0, has_focus ? job.focus_w : 0, job.psf_iters, has_complex_otf ? 1 : 0,
                str, psf_sigma_tile, job.read_variance, job.shot, job.quad, max_boost, sig_tgt, job.m5a_gamma,
                job.photo_shelf_k, job.includes_pix_ap ? 1 : 0,
                tr_b0, int(fb_zones.size())
            };
            [enc setBytes:&fpc length:sizeof(fpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake(nr_band * nc, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];

            [enc setComputePipelineState:ctx.pipes[K_M5B_CLAMP]];
            [enc setBuffer:b_in offset:0 atIndex:0];
            [enc setBuffer:b_dec offset:0 atIndex:1];
            [enc setBuffer:b_tact offset:0 atIndex:2];
            [enc setBuffer:b_focus offset:0 atIndex:3];
            [enc setBuffer:b_wstats offset:0 atIndex:4];
            PC_M5bClamp cpc{
                H, W, band_y0, band_h, in_y0, in_h, nr_band, nc,
                has_focus ? job.focus_h : 0, has_focus ? job.focus_w : 0, has_focus, wg_x,
                job.read_variance, job.shot, job.quad, job.wb_r, job.wb_b, job.sat0
            };
            [enc setBytes:&cpc length:sizeof(cpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake(wg_x, wg_y, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal M5-B band dispatch failed";
                return false;
            }

            const int keep_old = std::max(0, std::min(seam_rows, 52 - band_h));
            if (keep_old > 0) {
                std::memmove(seam_prev52.data(),
                             seam_prev52.data() + size_t(seam_rows - keep_old) * row_floats,
                             size_t(keep_old) * row_floats * sizeof(float));
            }
            const int copy_cur = std::min(52 - keep_old, band_h);
            std::memcpy(seam_prev52.data() + size_t(keep_old) * row_floats,
                        job.rgb + size_t(band_y0 + band_h - copy_cur) * row_floats,
                        size_t(copy_cur) * row_floats * sizeof(float));
            seam_rows = keep_old + copy_cur;

            std::memcpy(job.rgb + size_t(band_y0) * row_floats, [b_dec contents], size_t(band_h) * row_floats * sizeof(float));
            const float* wgs = (const float*)[b_wstats contents];
            for (int w = 0; w < wg_x * wg_y; ++w) {
                total_rms_sq += double(wgs[w * 2 + 0]);
                total_chg += size_t(wgs[w * 2 + 1] + 0.5f);
            }
        }

        if (job.is_astro && H >= 12 && W >= 12) {
            double sky_acc = 0.0;
            size_t sky_n = 0;
            for (int y = 0; y < H; y += 8) {
                for (int x = 0; x < W; x += 8) {
                    sky_acc += double(job.rgb[(size_t(y) * W + x) * 3 + 1]);
                    ++sky_n;
                }
            }
            const float sky = float(sky_acc / std::max<size_t>(1, sky_n));
            const float ps = std::max(0.0f, sky);
            const float sig_sky = std::sqrt(std::max(1e-12f, job.read_variance + ps * (job.shot + job.quad * ps)));
            const float star_thr = sky + 6.0f * sig_sky;
            const float star_sig = std::max(0.45f, 0.5f * psf_sigma_tile);
            float k5[5][5], ksum = 0.0f;
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                k5[dy + 2][dx + 2] = std::exp(-0.5f * float(dy * dy + dx * dx) / (star_sig * star_sig));
                ksum += k5[dy + 2][dx + 2];
            }
            for (int dy = 0; dy < 5; ++dy) for (int dx = 0; dx < 5; ++dx) k5[dy][dx] /= ksum;
            std::vector<float> orig(job.rgb, job.rgb + size_t(H) * W * 3), cur = orig, next = orig;
            std::vector<float> rel_ratio(size_t(H) * W * 3, 1.0f);
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 2; y < H - 2; ++y) {
                    for (int x = 2; x < W - 2; ++x) {
                        const size_t p = (size_t(y) * W + x) * 3;
                        if (orig[p + 1] <= sky + 3.0f * sig_sky) {
                            rel_ratio[p + 0] = rel_ratio[p + 1] = rel_ratio[p + 2] = 1.0f;
                            continue;
                        }
                        for (int ch = 0; ch < 3; ++ch) {
                            float blur = 0.0f;
                            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                                blur += k5[dy + 2][dx + 2] * cur[(size_t(y + dy) * W + x + dx) * 3 + ch];
                            const float ratio = (orig[p + ch] - sky + 1e-4f) / std::max(1e-4f, blur - sky + 1e-4f);
                            rel_ratio[p + ch] = std::max(0.25f, std::min(4.0f, ratio));
                        }
                    }
                }
                for (int y = 2; y < H - 2; ++y) {
                    for (int x = 2; x < W - 2; ++x) {
                        const size_t p = (size_t(y) * W + x) * 3;
                        if (orig[p + 1] <= star_thr) continue;
                        for (int ch = 0; ch < 3; ++ch) {
                            float adj = 0.0f;
                            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                                adj += k5[dy + 2][dx + 2] * rel_ratio[(size_t(y + dy) * W + x + dx) * 3 + ch];
                            next[p + ch] = std::max(sky, sky + (cur[p + ch] - sky) * (1.0f + 0.35f * str * (adj - 1.0f)));
                        }
                    }
                }
                std::swap(cur, next);
            }
            std::copy_n(cur.data(), size_t(H) * W * 3, job.rgb);
        }

        job.out_samples = size_t(H) * W * 3;
        job.out_changed = total_chg;
        job.out_rms_update = std::sqrt(total_rms_sq / std::max<size_t>(1, job.out_samples));
        return true;
    }
}

bool run_silicon_cleanup_gpu(SiliconCleanupGpuJob& job) {
    if (!job.raw || !job.cfa || job.H < 64 || job.W < 64 || (job.H & 1) || (job.W & 1)) {
        job.out_err = "Invalid C13 GPU parameters";
        return false;
    }
    const int N = int(job.raw->size());
    if (N < 4 || N > 16) {
        job.out_err = "C13 GPU supports 4..16 frames";
        return false;
    }
    const int H1 = job.H, W1 = job.W, Hh = H1 / 2, Wh = W1 / 2;
    const size_t n_1x = size_t(H1) * W1;
    const size_t n_half = size_t(Hh) * Wh;
    const bool has_flow = job.fy && job.fx && !job.fy->empty() && !job.fx->empty();
    const bool has_mw = job.mw && !job.mw->empty();
    for (int n = 0; n < N; ++n) {
        if ((*job.raw)[n].size() != n_1x) { job.out_err = "C13 raw plane size mismatch"; return false; }
        if (has_flow && (n >= int(job.fy->size()) || (*job.fy)[n].size() != n_half || (*job.fx)[n].size() != n_half)) {
            job.out_err = "C13 flow plane size mismatch"; return false;
        }
        if (has_mw && (n >= int(job.mw->size()) || (*job.mw)[n].size() != n_half)) {
            job.out_err = "C13 mw plane size mismatch"; return false;
        }
    }

    std::vector<std::array<float, 2>> bounds_y((size_t)N, {0.0f, 0.0f});
    if (has_flow) {
        for (int n = 0; n < N; ++n) {
            float ymn = 2.0f * (*job.fy)[n][0], ymx = ymn;
            for (size_t i = 0; i < n_half; ++i) {
                const float fyv = (*job.fy)[n][i], fxv = (*job.fx)[n][i];
                if (!std::isfinite(fyv) || !std::isfinite(fxv)) {
                    job.out_err = "Non-finite flow in C13 GPU";
                    return false;
                }
                const float dy = 2.0f * fyv;
                ymn = std::min(ymn, dy);
                ymx = std::max(ymx, dy);
            }
            bounds_y[n] = {ymn, ymx};
        }
    }

    @autoreleasepool {
        auto& ctx = global_metal();
        if (!ctx.ensure_init()) {
            job.out_err = ctx.init_err;
            return false;
        }
        std::lock_guard<std::mutex> run_lk(ctx.run_mtx);

        const size_t max_ssbo = std::min<size_t>(ctx.dev.maxBufferLength, 256ULL << 20);
        const size_t scene_bytes = size_t(n_half) * 3 * sizeof(float);
        const size_t res_single_bytes = size_t(n_1x) * sizeof(float);
        const size_t flow_full_bytes = size_t(n_half) * 4 * sizeof(float);
        if (scene_bytes > ctx.dev.maxBufferLength || res_single_bytes > ctx.dev.maxBufferLength ||
            flow_full_bytes > ctx.dev.maxBufferLength) {
            job.out_err = "Metal maxBufferLength too small for C13";
            return false;
        }

        const int band_hh_cap = std::max(16, std::min(Hh, int(max_ssbo / std::max<size_t>(1, size_t(N) * Wh * 12))));
        const size_t warp_stack_bytes = size_t(N) * band_hh_cap * Wh * 3 * sizeof(float);
        const size_t fb_bytes = size_t(band_hh_cap) * Wh * 3 * sizeof(float);

        const bool single_pass = (size_t(N) * res_single_bytes <= max_ssbo);
        const int res_band_h_cap = single_pass
            ? H1
            : std::max(16, std::min(H1, int(max_ssbo / std::max<size_t>(1, size_t(N) * W1 * 4))));
        const size_t res_buf_bytes = single_pass
            ? size_t(N) * res_single_bytes
            : std::max(res_single_bytes, size_t(N) * res_band_h_cap * W1 * sizeof(float));

        const int max_hw = std::max(H1, W1);
        const size_t ph_bytes = size_t(4) * max_hw * sizeof(float);
        const size_t val_bytes = size_t(4) * max_hw * sizeof(uint32_t);
        const int row4_frame_floats = H1 * 4;
        const int col4_frame_floats = 4 * W1;
        const int col_base_off = N * row4_frame_floats;
        const size_t rc_atlas_bytes = size_t(N) * (row4_frame_floats + col4_frame_floats) * sizeof(float);
        const size_t hot_band_bytes = size_t(res_band_h_cap) * W1 * sizeof(float);
        const int wg_x = (W1 + 15) / 16;
        const int max_wg_y = (res_band_h_cap + 15) / 16;
        const size_t wg_bytes = size_t(wg_x) * max_wg_y * 5 * sizeof(float);

        id<MTLBuffer> b_scene = ctx.acquire_buf(0, scene_bytes);
        id<MTLBuffer> b_stack = ctx.acquire_buf(1, warp_stack_bytes);
        id<MTLBuffer> b_fb    = ctx.acquire_buf(2, fb_bytes);
        id<MTLBuffer> b_raw   = ctx.acquire_buf(3, res_single_bytes);
        id<MTLBuffer> b_flow  = ctx.acquire_buf(4, flow_full_bytes);
        id<MTLBuffer> b_res   = ctx.acquire_buf(5, res_buf_bytes);
        id<MTLBuffer> b_ph    = ctx.acquire_buf(6, ph_bytes);
        id<MTLBuffer> b_val   = ctx.acquire_buf(7, val_bytes);
        id<MTLBuffer> b_rc    = ctx.acquire_buf(8, rc_atlas_bytes);
        id<MTLBuffer> b_hot   = ctx.acquire_buf(9, hot_band_bytes);
        id<MTLBuffer> b_wg    = ctx.acquire_buf(10, wg_bytes);
        if (!b_scene || !b_stack || !b_fb || !b_raw || !b_flow ||
            !b_res || !b_ph || !b_val || !b_rc || !b_hot || !b_wg) {
            job.out_err = "Metal C13 buffer allocation failed";
            return false;
        }

        uint32_t cfa_w[3] = {0, 0, 0};
        for (int i = 0; i < 36; ++i) cfa_w[i / 16] |= uint32_t(job.cfa[i]) << ((i % 16) * 2);

        // Step A: Half-res median scene synthesis in vertical bands
        for (int by0 = 0; by0 < Hh; by0 += band_hh_cap) {
            const int bh = std::min(band_hh_cap, Hh - by0);
            for (int n = 0; n < N; ++n) {
                int sy0 = std::max(0, std::min(H1 - 1, int(std::floor(2.0f * by0 + bounds_y[n][0])) - 3));
                int sy1 = std::max(0, std::min(H1 - 1, int(std::ceil(2.0f * (by0 + bh - 1) + bounds_y[n][1])) + 4));
                if (n == 0) {
                    sy0 = std::min(sy0, std::max(0, 2 * by0 - 3));
                    sy1 = std::max(sy1, std::min(H1 - 1, 2 * (by0 + bh - 1) + 4));
                }
                if (sy1 < sy0) { sy0 = 0; sy1 = 0; }
                const int sy_cnt = sy1 - sy0 + 1;
                std::memcpy([b_raw contents], (*job.raw)[n].data() + size_t(sy0) * W1, size_t(sy_cnt) * W1 * sizeof(float));

                if (has_flow || has_mw) {
                    float* fdst = (float*)[b_flow contents];
                    const float* fy = has_flow ? (*job.fy)[n].data() + size_t(by0) * Wh : nullptr;
                    const float* fx = has_flow ? (*job.fx)[n].data() + size_t(by0) * Wh : nullptr;
                    const float* mw = has_mw ? (*job.mw)[n].data() + size_t(by0) * Wh : nullptr;
                    for (size_t i = 0; i < size_t(bh) * Wh; ++i) {
                        fdst[i * 4 + 0] = fy ? fy[i] : 0.0f;
                        fdst[i * 4 + 1] = fx ? fx[i] : 0.0f;
                        fdst[i * 4 + 2] = mw ? mw[i] : 1.0f;
                        fdst[i * 4 + 3] = 0.0f;
                    }
                }

                id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
                id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                [enc setComputePipelineState:ctx.pipes[K_C13_WARP_RGB]];
                [enc setBuffer:b_raw offset:0 atIndex:0];
                [enc setBuffer:b_flow offset:0 atIndex:1];
                [enc setBuffer:b_stack offset:0 atIndex:2];
                [enc setBuffer:b_fb offset:0 atIndex:3];
                PC_C13Warp wpc{
                    H1, W1, Hh, Wh, sy0, sy_cnt, by0, bh,
                    by0, bh, n, has_flow ? 1 : 0, has_mw ? 1 : 0, (n == 0) ? 1 : 0, 0, 0,
                    cfa_w[0], cfa_w[1], cfa_w[2], 0
                };
                [enc setBytes:&wpc length:sizeof(wpc) atIndex:6];
                [enc dispatchThreadgroups:MTLSizeMake((Wh + 15) / 16, (bh + 15) / 16, 1)
                    threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                [enc endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                if (cb.status == MTLCommandBufferStatusError) return false;
            }
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:ctx.pipes[K_C13_SCENE]];
            [enc setBuffer:b_stack offset:0 atIndex:0];
            [enc setBuffer:b_fb offset:0 atIndex:1];
            [enc setBuffer:b_scene offset:0 atIndex:2];
            PC_C13Scene spc{Wh, by0, bh, N};
            [enc setBytes:&spc length:sizeof(spc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((Wh + 15) / 16, (bh + 15) / 16, 1)
                threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) return false;
        }

        auto load_res_band = [&](int n, int band_y0, int band_h, int raw_has_band, int& hy0, int& hy_cnt) {
            const size_t r_sz = size_t(raw_has_band ? band_h : H1) * W1 * sizeof(float);
            std::memcpy([b_raw contents], (*job.raw)[n].data() + size_t(raw_has_band ? band_y0 : 0) * W1, r_sz);
            hy0 = 0; hy_cnt = 1;
            if (has_flow || has_mw) {
                const float yb_lo = float(band_y0) - bounds_y[n][1] - 4.0f;
                const float yb_hi = float(band_y0 + band_h - 1) - bounds_y[n][0] + 4.0f;
                const int hlo = has_flow
                    ? std::max(0, std::min(Hh - 1, int(std::floor(0.5f * std::min(float(band_y0), yb_lo))) - 2))
                    : std::max(0, std::min(Hh - 1, band_y0 / 2 - 2));
                const int hhi = has_flow
                    ? std::max(hlo, std::min(Hh - 1, int(std::ceil(0.5f * std::max(float(band_y0 + band_h - 1), yb_hi))) + 2))
                    : std::max(hlo, std::min(Hh - 1, (band_y0 + band_h) / 2 + 2));
                hy0 = hlo;
                hy_cnt = hhi - hlo + 1;
                float* fdst = (float*)[b_flow contents];
                const float* fy = has_flow ? (*job.fy)[n].data() + size_t(hy0) * Wh : nullptr;
                const float* fx = has_flow ? (*job.fx)[n].data() + size_t(hy0) * Wh : nullptr;
                const float* mw = has_mw ? (*job.mw)[n].data() + size_t(hy0) * Wh : nullptr;
                for (size_t i = 0; i < size_t(hy_cnt) * Wh; ++i) {
                    fdst[i * 4 + 0] = fy ? fy[i] : 0.0f;
                    fdst[i * 4 + 1] = fx ? fx[i] : 0.0f;
                    fdst[i * 4 + 2] = mw ? mw[i] : 1.0f;
                    fdst[i * 4 + 3] = 0.0f;
                }
            }
        };

        // Step B: Sensor residual + Row/Col 4-phase medians + 97-tap highpass
        for (int n = 0; n < N; ++n) {
            int hy0 = 0, hy_cnt = 1;
            load_res_band(n, 0, H1, 0, hy0, hy_cnt);
            const int res_off = single_pass ? int(size_t(n) * n_1x) : 0;
            const int row4_off = n * row4_frame_floats;
            const int col4_off = col_base_off + n * col4_frame_floats;

            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];

            [enc setComputePipelineState:ctx.pipes[K_C13_SENSOR_RES]];
            [enc setBuffer:b_raw offset:0 atIndex:0];
            [enc setBuffer:b_flow offset:0 atIndex:1];
            [enc setBuffer:b_scene offset:0 atIndex:2];
            [enc setBuffer:b_res offset:0 atIndex:3];
            PC_C13SensorRes srpc{
                H1, W1, Hh, Wh, 0, H1, hy0, hy_cnt, res_off, 0, has_flow ? 1 : 0, has_mw ? 1 : 0,
                job.read_variance, job.shot, cfa_w[0], cfa_w[1], cfa_w[2], 0
            };
            [enc setBytes:&srpc length:sizeof(srpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((W1 + 15) / 16, (H1 + 15) / 16, 1)
                threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];

            [enc setComputePipelineState:ctx.pipes[K_C13_MED_4PH]];
            [enc setBuffer:b_res offset:0 atIndex:0];
            [enc setBuffer:b_rc offset:0 atIndex:1];
            [enc setBuffer:b_ph offset:0 atIndex:2];
            [enc setBuffer:b_val offset:0 atIndex:3];
            PC_C13Med4Ph rmpc{H1, W1, 0, H1, res_off, 0, 0, 0};
            [enc setBytes:&rmpc length:sizeof(rmpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake(H1, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];

            [enc setComputePipelineState:ctx.pipes[K_C13_HP97]];
            [enc setBuffer:b_ph offset:0 atIndex:0];
            [enc setBuffer:b_val offset:0 atIndex:1];
            [enc setBuffer:b_rc offset:0 atIndex:2];
            PC_C13Hp97 rhpc{H1, 0, row4_off, 0};
            [enc setBytes:&rhpc length:sizeof(rhpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((H1 * 4 + 255) / 256, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];

            [enc setComputePipelineState:ctx.pipes[K_C13_MED_4PH]];
            [enc setBuffer:b_res offset:0 atIndex:0];
            [enc setBuffer:b_rc offset:0 atIndex:1];
            [enc setBuffer:b_ph offset:0 atIndex:2];
            [enc setBuffer:b_val offset:0 atIndex:3];
            PC_C13Med4Ph cmpc{H1, W1, 1, W1, res_off, row4_off, 0, 0};
            [enc setBytes:&cmpc length:sizeof(cmpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake(W1, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];

            [enc setComputePipelineState:ctx.pipes[K_C13_HP97]];
            [enc setBuffer:b_ph offset:0 atIndex:0];
            [enc setBuffer:b_val offset:0 atIndex:1];
            [enc setBuffer:b_rc offset:0 atIndex:2];
            PC_C13Hp97 chpc{W1, 1, col4_off, 0};
            [enc setBytes:&chpc length:sizeof(chpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake((W1 * 4 + 255) / 256, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];

            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) {
                job.out_err = "Metal C13 row/col FPN failed";
                return false;
            }
        }

        // Step C: Consensus hot pixels + statistics
        const float p_h = 0.075f;
        const float hot_sigma = std::sqrt(std::max(1e-12f, job.read_variance + p_h * (job.shot + job.quad * p_h)));
        const float hot_thr = 4.0f * hot_sigma;
        std::vector<float> hot_host(n_1x, 0.0f);
        double tot_before = 0.0, tot_after = 0.0, tot_rms = 0.0;
        size_t tot_samples = 0, tot_changed = 0;

        for (int band_y0 = 0; band_y0 < H1; band_y0 += res_band_h_cap) {
            const int band_h = std::min(res_band_h_cap, H1 - band_y0);
            if (!single_pass) {
                for (int n = 0; n < N; ++n) {
                    int hy0 = 0, hy_cnt = 1;
                    load_res_band(n, band_y0, band_h, 1, hy0, hy_cnt);
                    id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
                    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
                    [enc setComputePipelineState:ctx.pipes[K_C13_SENSOR_RES]];
                    [enc setBuffer:b_raw offset:0 atIndex:0];
                    [enc setBuffer:b_flow offset:0 atIndex:1];
                    [enc setBuffer:b_scene offset:0 atIndex:2];
                    [enc setBuffer:b_res offset:0 atIndex:3];
                    PC_C13SensorRes srpc{
                        H1, W1, Hh, Wh, band_y0, band_h, hy0, hy_cnt, n * band_h * W1, 1,
                        has_flow ? 1 : 0, has_mw ? 1 : 0, job.read_variance, job.shot, cfa_w[0], cfa_w[1], cfa_w[2], 0
                    };
                    [enc setBytes:&srpc length:sizeof(srpc) atIndex:6];
                    [enc dispatchThreadgroups:MTLSizeMake((W1 + 15) / 16, (band_h + 15) / 16, 1)
                        threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    if (cb.status == MTLCommandBufferStatusError) return false;
                }
            }
            const int wg_y = (band_h + 15) / 16;
            id<MTLCommandBuffer> cb = [ctx.queue commandBufferWithUnretainedReferences];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:ctx.pipes[K_C13_HOT_MAP]];
            [enc setBuffer:b_res offset:0 atIndex:0];
            [enc setBuffer:b_rc offset:0 atIndex:1];
            [enc setBuffer:b_hot offset:0 atIndex:2];
            [enc setBuffer:b_wg offset:0 atIndex:3];
            PC_C13HotMap hpc{H1, W1, N, band_y0, band_h, wg_x, col_base_off, 0, hot_sigma, hot_thr, 0.0f, 0.0f};
            [enc setBytes:&hpc length:sizeof(hpc) atIndex:6];
            [enc dispatchThreadgroups:MTLSizeMake(wg_x, wg_y, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            if (cb.status == MTLCommandBufferStatusError) return false;

            std::memcpy(hot_host.data() + size_t(band_y0) * W1, [b_hot contents], size_t(band_h) * W1 * sizeof(float));
            const float* wgs = (const float*)[b_wg contents];
            for (int w = 0; w < wg_x * wg_y; ++w) {
                tot_before += double(wgs[w * 5 + 0]);
                tot_after += double(wgs[w * 5 + 1]);
                tot_rms += double(wgs[w * 5 + 2]);
                tot_samples += size_t(wgs[w * 5 + 3] + 0.5f);
                tot_changed += size_t(wgs[w * 5 + 4] + 0.5f);
            }
        }

        // Step D: Multi-threaded host subtraction raw[n][pix] -= row4 + col4 + hot
        const float* rc_ptr = (const float*)[b_rc contents];
        const int p_nt = (job.n_threads > 1 && H1 >= 64) ? std::min(job.n_threads, 8) : 1;
        auto apply_rows = [&](int r0, int r1) {
            const float* row4_base = rc_ptr;
            const float* col4_base = rc_ptr + col_base_off;
            for (int n = 0; n < N; ++n) {
                float* raw_n = (*job.raw)[n].data();
                const float* row4_n = row4_base + size_t(n) * row4_frame_floats;
                const float* col4_n = col4_base + size_t(n) * col4_frame_floats;
                for (int r = r0; r < r1; ++r) {
                    const float* r4 = row4_n + size_t(r) * 4;
                    const float* c4 = col4_n + size_t(r & 3) * W1;
                    const float* ht = hot_host.data() + size_t(r) * W1;
                    float* dst = raw_n + size_t(r) * W1;
                    int c = 0;
                    for (; c + 4 <= W1; c += 4) {
                        dst[c + 0] -= (r4[0] + c4[c + 0] + ht[c + 0]);
                        dst[c + 1] -= (r4[1] + c4[c + 1] + ht[c + 1]);
                        dst[c + 2] -= (r4[2] + c4[c + 2] + ht[c + 2]);
                        dst[c + 3] -= (r4[3] + c4[c + 3] + ht[c + 3]);
                    }
                    for (; c < W1; ++c) dst[c] -= (r4[c & 3] + c4[c] + ht[c]);
                }
            }
        };
        if (p_nt <= 1) {
            apply_rows(0, H1);
        } else {
            std::vector<heraw_sync::JoiningThread> workers;
            workers.reserve(p_nt);
            const int ch = (H1 + p_nt - 1) / p_nt;
            for (int t = 0; t < p_nt; ++t) {
                const int r0 = t * ch, r1 = std::min(H1, r0 + ch);
                if (r0 < r1) workers.emplace_back([&, r0, r1]() { apply_rows(r0, r1); });
            }
        }

        job.out_before = tot_before;
        job.out_after = tot_after;
        job.out_samples = tot_samples;
        job.out_changed = tot_changed;
        job.out_rms_update = std::sqrt(tot_rms / std::max<size_t>(1, n_1x * N));
        job.out_peak_scratch_bytes = (size_t(n_half) * 3 + size_t(N) * (n_1x + 4 * H1 + 4 * W1) + n_1x) * sizeof(float);
        return true;
    }
}

} // namespace heraw_gpu

#endif // __APPLE__
