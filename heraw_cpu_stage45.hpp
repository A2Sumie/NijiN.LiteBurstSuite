// Include after the CPU image helpers and snr_gated_rl_deconv definition.
#pragma once

namespace heraw_cpu {

inline float weight_exp(float x) {
    return std::exp(-clamp_f(x, 0.0f, 40.0f));
}

inline float chroma_anchor(const heraw_gpu::Stage45GpuJob& j, int y, int x, int ch) {
    float sum = 0.0f, weights = 0.0f;
    for (int r = std::max(0, y - 3); r <= std::min(j.H1 - 1, y + 3); ++r)
        for (int c = std::max(0, x - 3); c <= std::min(j.W1 - 1, x + 3); ++c) {
            if (j.cfa6x6[r % 6][c % 6] != ch) continue;
            float w = 1.0f / (0.25f + float((r-y)*(r-y) + (c-x)*(c-x)));
            sum += w * j.c_anc_1x[size_t(r) * j.W1 + c];
            weights += w;
        }
    return sum / std::max(weights, 1e-12f);
}

inline float unpack_half(uint16_t h) {
    const int exponent = (h >> 10) & 31;
    const float mantissa = float(h & 1023);
    const float v = exponent == 0 ? std::ldexp(mantissa, -24) :
        exponent == 31 ? (mantissa ? std::numeric_limits<float>::quiet_NaN() :
                         std::numeric_limits<float>::infinity()) :
        std::ldexp(1.0f + mantissa / 1024.0f, exponent - 15);
    return (h & 32768) ? -v : v;
}

// Global edge statistics are shared across ROIs just like the GPU kernels.
inline void atrous_global(float* red, float* blue, const float* green,
                          const heraw_gpu::Stage45GpuJob& j, float sigma) {
    if (j.global_edge_p80 < 0.0f) {
        atrous_chroma_denoise(red, blue, green, j.H1, j.W1, sigma, j.n_threads,
                             j.snr_boost_half, j.H1/2, j.W1/2, j.is_astro);
        return;
    }
    if (sigma <= 1e-6f) return;
    const int H = j.H1, W = j.W1, Wh = W / 2;
    const float sig_c = std::max(1e-5f, sigma);
    const float sig_g = std::max(1e-4f, 0.5f * sigma);
    const float edge = std::max(j.is_astro ? 0.14f : 3.0f * sig_g,
                               2.2f * j.global_edge_p80 + 0.006f);
    const float inv_g = 1.0f / (2.0f * edge * edge);
    const float kernel[5] = {1.0f/16, 4.0f/16, 6.0f/16, 4.0f/16, 1.0f/16};
    std::vector<float> tr(size_t(H)*W), tb(size_t(H)*W);
    for (int lv = 0; lv < 6; ++lv) {
        const int step = 1 << lv;
        const float sc = 2.7f * sig_c * std::pow(0.56f, float(lv)) + 0.0015f;
        const float inv_c = 1.0f / (2.0f * sc * sc);
        for (int axis = 0; axis < 2; ++axis) {
            const float* sr = axis ? tr.data() : red;
            const float* sb = axis ? tb.data() : blue;
            float* dr = axis ? red : tr.data();
            float* db = axis ? blue : tb.data();
            const int rl_halo=j.psf_iters>0?
                j.psf_iters*heraw_gpu::make_olpf_psf_pc(1,1,0,j.psf_sigma,j.olpf_mode).radius+8:0;
            const int remaining=2*(64-(1<<(lv+1)));
            const int hx=remaining+rl_halo+3,hy=hx+(axis?0:2*step);
            const int rx0=j.core_x1>0?std::max(0,j.core_x0-hx):0;
            const int rx1=j.core_x1>0?std::min(W,j.core_x1+hx):W;
            const int ry0=j.core_y1>0?std::max(0,j.core_y0-hy):0;
            const int ry1=j.core_y1>0?std::min(H,j.core_y1+hy):H;
            parallel_for_rows(H, j.n_threads, [&](int y0, int y1, int) {
                for (int y = y0; y < y1; ++y) {
                  const size_t row=size_t(y)*W;
                  if(y<ry0||y>=ry1) {
                      std::copy_n(sr+row,W,dr+row);std::copy_n(sb+row,W,db+row);
                      continue;
                  }
                  std::copy_n(sr+row,rx0,dr+row);std::copy_n(sb+row,rx0,db+row);
                  std::copy_n(sr+row+rx1,W-rx1,dr+row+rx1);std::copy_n(sb+row+rx1,W-rx1,db+row+rx1);
                  for (int x = rx0; x < rx1; ++x) {
                    size_t i = size_t(y)*W+x;
                    float boost = j.snr_boost_half[size_t(y/2)*Wh+x/2];
                    float ic = inv_c / (boost * boost);
                    float wr = 0, vr = 0, wb = 0, vb = 0;
                    for (int k = -2; k <= 2; ++k) {
                        int yy = axis ? clamp_i(y+k*step, 0, H-1) : y;
                        int xx = axis ? x : clamp_i(x+k*step, 0, W-1);
                        size_t q = size_t(yy)*W+xx;
                        float dg = green[q]-green[i];
                        float wg = kernel[k+2] * weight_exp(dg*dg*inv_g);
                        float rd = sr[q]-sr[i], bd = sb[q]-sb[i];
                        float rw = wg * weight_exp(rd*rd*ic);
                        float bw = wg * weight_exp(bd*bd*ic);
                        wr += rw; vr += rw*sr[q]; wb += bw; vb += bw*sb[q];
                    }
                    dr[i] = vr/std::max(wr, 1e-12f);
                    db[i] = vb/std::max(wb, 1e-12f);
                  }
                }
            });
        }
    }
}

inline bool run_stage45_cpu_roi(heraw_gpu::Stage45GpuJob& j) {
    const auto start = std::chrono::steady_clock::now();
    auto cost_start=start;
    j.out_wall_seconds.fill(0); j.out_device_seconds.fill(0);
    auto cost_mark=[&](size_t phase) {
        const auto now=std::chrono::steady_clock::now();
        j.out_wall_seconds[phase]+=std::chrono::duration<double>(now-cost_start).count();
        cost_start=now;
    };
    const int H = j.H1, W = j.W1, Hh = H/2, Wh = W/2, H2 = H*2, W2 = W*2;
    const int nt = std::max(1, j.n_threads);
    const auto& frames = *j.frames_lin;
    const auto& fy = *j.flows_fy;
    const auto& fx = *j.flows_fx;
    const auto& motion = *j.mw_half;
    const float* cov = j.inv_cov;
    float* rgb = j.rgb_2x_out;
    const int cy1 = j.core_y1 > 0 ? j.core_y1 : H;
    const int cx1 = j.core_x1 > 0 ? j.core_x1 : W;
    std::vector<float> green(j.eta_only ? 0 : size_t(H2)*W2);
    std::vector<double> eta_sum(nt);
    std::vector<int64_t> eta_count(nt);
    parallel_for_rows(H, nt, [&](int y0, int y1, int tid) {
        for (int Y = y0; Y < y1; ++Y) for (int X = 0; X < W; ++X) {
            const bool eta_site = ((Y | X) & 3) == 0 && Y >= j.core_y0 &&
                Y < cy1 && X >= j.core_x0 && X < cx1;
            if (j.eta_only && !eta_site) continue;
            const float yh = 0.5f*Y+0.125f, xh = 0.5f*X+0.125f;
            const size_t hi = size_t(clamp_i(int(yh+0.5f),0,Hh-1))*Wh +
                clamp_i(int(xh+0.5f),0,Wh-1);
            const float axx = cov[hi*3], axy = cov[hi*3+1], ayy = cov[hi*3+2];
            const float anchor = j.g_guide[hi];
            const float threshold = 3.5f*std::sqrt(float(j.pg_sig0_sq)+
                float(j.pg_gain)*std::max(0.0f,anchor))+0.008f;
            float ws[4] = {}, vs[4] = {}, w2 = 0;
            for (int n = 0; n < j.N; ++n) {
                float mw = heraw_gpu::gpu_motion_weight(j,
                    bilerp_clamped(motion[n].data(),Hh,Wh,yh,xh)) *
                    heraw_gpu::gpu_frame_weight(j,n);
                if (mw < 0.02f) continue;
                float dy = n ? 2*bilerp_clamped(fy[n].data(),Hh,Wh,yh,xh) : 0;
                float dx = n ? 2*bilerp_clamped(fx[n].data(),Hh,Wh,yh,xh) : 0;
                float yt = (float(Y+j.origin_y)+dy)-float(j.origin_y);
                float xt = (float(X+j.origin_x)+dx)-float(j.origin_x);
                if (yt < -0.5f || yt > H-0.5f || xt < -0.5f || xt > W-0.5f) continue;
                int rb = int(std::floor(yt)), cb = int(std::floor(xt));
                float frame_ws[4] = {}, frame_vs[4] = {}, frame_w2 = 0;
                for (int rr = rb-1; rr <= rb+2; ++rr) {
                    int r = clamp_i(rr,0,H-1);
                    for (int c = cb-1; c <= cb+2; ++c) {
                        if (c < 0 || c >= W || j.cfa6x6[r%6][c%6] != 1) continue;
                        float v = frames[n][size_t(r)*W+c], rob = mw;
                        if (heraw_gpu::gpu_use_robust(j,n)) {
                            float ad = std::fabs(v-anchor);
                            if (ad > threshold) { float q = threshold/ad; rob *= q*q*q; }
                        }
                        for (int p = 0; p < (j.eta_only ? 1 : 4); ++p) {
                            float yy = float(rr)-yt-0.5f*(p/2);
                            float xx = float(c)-xt-0.5f*(p%2);
                            float d = axx*xx*xx+2*axy*xx*yy+ayy*yy*yy;
                            if (d > 6.5f) continue;
                            float w = rob*weight_exp(0.5f*d);
                            frame_ws[p] += w; frame_vs[p] += w*v;
                            if (p == 0) frame_w2 += w*w;
                        }
                    }
                }
                for (int p = 0; p < 4; ++p) { ws[p] += frame_ws[p]; vs[p] += frame_vs[p]; }
                w2 += frame_w2;
            }
            if (eta_site) {
                const float eta=ws[0]>1e-8f?w2/(ws[0]*ws[0]):-1.0f;
                if(j.eta_quarter_out) {
                    const int width=((j.full_W1>0?j.full_W1:j.W1)+3)/4;
                    j.eta_quarter_out[size_t((j.origin_y+Y)/4)*width+(j.origin_x+X)/4]=eta;
                }
                if(eta>=0) {eta_sum[tid]+=eta;++eta_count[tid];}
            }
            if (j.eta_only) continue;
            float g[4];
            for (int p = 0; p < 4; ++p) g[p] = ws[p] > 1e-8f ? vs[p]/ws[p] : anchor;
            float avg = 0.25f*(ws[0]+ws[1]+ws[2]+ws[3]);
            float phase = clamp_f((0.72f-std::min(ws[0],ws[3])/
                std::max(std::max(ws[1],ws[2]),1e-6f))/0.28f,0,0.48f);
            float alpha = std::max(clamp_f((2.3f-avg)/1.35f,0,0.88f),phase);
            if (alpha > 1e-3f) {
                int xp = std::min(X+1,W-1), yp = std::min(Y+1,H-1);
                float a = j.g0_ha_1x[size_t(Y)*W+X], b = j.g0_ha_1x[size_t(Y)*W+xp];
                float c = j.g0_ha_1x[size_t(yp)*W+X], d = j.g0_ha_1x[size_t(yp)*W+xp];
                float nw = 1/(std::fabs(a-d)+0.002f), ne = 1/(std::fabs(b-c)+0.002f);
                const float ha[4] = {a,0.5f*(a+b),0.5f*(a+c),
                    (nw*0.5f*(a+d)+ne*0.5f*(b+c))/(nw+ne)};
                for (int p = 0; p < 4; ++p) g[p] += alpha*(ha[p]-g[p]);
            }
            for (int p = 0; p < 4; ++p) green[size_t(Y*2+p/2)*W2+X*2+p%2] = g[p];
        }
    },16);
    j.out_eta_sum = 0; j.out_eta_count = 0;
    for (int t = 0; t < nt; ++t) {
        j.out_eta_sum += eta_sum[t]; j.out_eta_count += eta_count[t];
    }
    j.out_eta_eff = j.global_eta >= 0 ? j.global_eta :
        std::max(0.01,j.out_eta_sum/double(std::max<int64_t>(1,j.out_eta_count)));
    j.out_device_label = "CPU SIMD tiled";
    cost_mark(1);
    if (j.eta_only) return true;
    std::vector<float> red(size_t(H)*W), blue(size_t(H)*W), guide(size_t(H)*W);
    parallel_for_rows(H,nt,[&](int y0,int y1,int) {
        for (int Y = y0; Y < y1; ++Y) for (int X = 0; X < W; ++X)
            for (int b = 0; b < 2; ++b) {
                int r2 = 2*Y+b, c2 = 2*X+b;
                float yr = 0.5f*r2, xr = 0.5f*c2, yh = 0.25f*r2, xh = 0.25f*c2;
                int yi = clamp_i(int(yh+0.5f),0,Hh-1), xi = clamp_i(int(xh+0.5f),0,Wh-1);
                size_t hi = size_t(yi)*Wh+xi;
                float axx = 0.6f*cov[hi*3], axy = 0.6f*cov[hi*3+1], ayy = 0.6f*cov[hi*3+2];
                float anchor = chroma_anchor(j,clamp_i(2*yi+b,0,H-1),clamp_i(2*xi+b,0,W-1),b ? 2 : 0);
                float threshold = 4*std::sqrt(float(j.pg_sig0_sq)+float(j.pg_gain)*
                    std::max(0.0f,std::max(anchor,j.g_guide[hi])))+0.01f;
                float wb = b ? j.wb_b : j.wb_r;
                float weight = 0, value = 0;
                for (int n = 0; n < j.N; ++n) {
                    float mw = heraw_gpu::gpu_motion_weight(j,bilerp_clamped(motion[n].data(),Hh,Wh,yh,xh)) *
                        heraw_gpu::gpu_frame_weight(j,n);
                    if (mw < 0.02f) continue;
                    float dy = n ? 2*bilerp_clamped(fy[n].data(),Hh,Wh,yh,xh) : 0;
                    float dx = n ? 2*bilerp_clamped(fx[n].data(),Hh,Wh,yh,xh) : 0;
                    float cy = 0.5f*j.full_H1, cx = 0.5f*j.full_W1;
                    float ry = yr+j.origin_y-cy, rx = xr+j.origin_x-cx;
                    float inv_r = 1/std::max(1.0f,std::max(cy,cx));
                    float rn = (rx*inv_r)*(rx*inv_r)+(ry*inv_r)*(ry*inv_r);
                    float lca = (b ? j.lca_b_k1 : j.lca_r_k1)+(b ? j.lca_b_k2 : j.lca_r_k2)*rn;
                    dy += lca*ry; dx += lca*rx;
                    if (j.frame_ois_shifts && j.frame_ois_shifts->size() >= size_t(n+1)*4) {
                        dy += (*j.frame_ois_shifts)[size_t(n)*4+b*2];
                        dx += (*j.frame_ois_shifts)[size_t(n)*4+b*2+1];
                    }
                    float yt = ((yr+j.origin_y)+dy)-j.origin_y;
                    float xt = ((xr+j.origin_x)+dx)-j.origin_x;
                    if (yt < 0 || yt > H-1 || xt < 0 || xt > W-1) continue;
                    int qy = int(std::floor((yt-b)*0.5f)), qx = int(std::floor((xt-b)*0.5f));
                    float frame_weight = 0, frame_value = 0;
                    for (int ky = 0; ky < (j.is_xtrans ? 6 : 2); ++ky) {
                        int rr = j.is_xtrans ? int(std::floor(yt))-2+ky : clamp_i(2*(qy+ky)+b,b,H-2+b);
                        if (rr < 0 || rr >= H) continue;
                        for (int kx = 0; kx < (j.is_xtrans ? 6 : 2); ++kx) {
                            int cc = j.is_xtrans ? int(std::floor(xt))-2+kx : clamp_i(2*(qx+kx)+b,b,W-2+b);
                            if (cc < 0 || cc >= W || j.cfa6x6[rr%6][cc%6] != (b ? 2 : 0)) continue;
                            float yy = rr-yt, xx = cc-xt;
                            float d = (j.is_xtrans ? 0.55f : 1.0f)*(axx*xx*xx+2*axy*xx*yy+ayy*yy*yy);
                            if (d > (j.is_xtrans ? 9.5f : 7.5f)) continue;
                            float v = frames[n][size_t(rr)*W+cc]*wb;
                            float g = bilerp_clamped(green.data(),H2,W2,
                                2*((float(rr+j.origin_y)-dy)-j.origin_y),
                                2*((float(cc+j.origin_x)-dx)-j.origin_x));
                            float rob = 1;
                            if (heraw_gpu::gpu_use_robust(j,n)) {
                                float ad = std::fabs(v-anchor*wb), tw = threshold*wb;
                                if (ad > tw) { float q = tw/ad; rob = q*q*q; }
                            }
                            float w = mw*rob*weight_exp(0.5f*d);
                            frame_weight += w; frame_value += w*(v-g);
                        }
                    }
                    weight += frame_weight; value += frame_value;
                }
                float g = green[size_t(r2)*W2+c2];
                float diff = weight > 1e-8 ? float(value/weight) : anchor*wb-g;
                float peak = std::max(g,anchor), hl = j.is_astro ? 0.68f : 0.65f;
                if (peak > hl) {
                    float t = clamp_f((peak-hl)/(j.is_astro ? 0.24f : 0.28f),0,1);
                    diff *= (1-t)*(1-t);
                }
                (b ? blue : red)[size_t(Y)*W+X] = diff;
            }
    },16);
    if (j.on_stage4_done) {
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        j.on_stage4_done(j.out_eta_eff,seconds,j.out_device_label);
    }
    const double gain = std::max(0.0,j.pg_gain*j.out_eta_eff);
    cost_mark(2);
    const double sig2 = std::max(1e-10,j.pg_sig0_sq*j.out_eta_eff);
    const double ped = 4*std::sqrt(sig2), gat_c = 0.375*gain*gain+sig2, sqrt_c = std::sqrt(gat_c);
    UninitBuf<float> z(size_t(H2)*W2), den(size_t(H2)*W2);
    parallel_for_rows(H2,nt,[&](int y0,int y1,int) {
        for (size_t i = size_t(y0)*W2; i < size_t(y1)*W2; ++i) {
            double v = std::max(0.0,double(green[i])+ped);
            z[i] = den[i] = float(2*v/(std::sqrt(gain*v+gat_c)+sqrt_c));
        }
    });
    multiscale_dct_denoise_gat(den.data(),H2,W2,nt,j.denoise_strength,j.edge_polish,
                              j.snr_boost_half,Hh,Wh,&j);
    std::vector<float> variance(size_t(Hh)*Wh);
    parallel_for_rows(Hh,nt,[&](int y0,int y1,int) {
        for (int y = y0; y < y1; ++y) for (int x = 0; x < Wh; ++x) {
            double sum = 0;
            for (int dy = 0; dy < 4; ++dy) for (int dx = 0; dx < 4; ++dx) {
                size_t i = size_t(y*4+dy)*W2+x*4+dx;
                double diff = double(z[i])-den[i]; sum += diff*diff;
            }
            variance[size_t(y)*Wh+x] = std::min(float(sum/16),1.25f);
        }
    });
    sep_gaussian_2d(variance.data(),variance.data(),Hh,Wh,8.0f,nt);
    parallel_for_rows(H2,nt,[&](int y0,int y1,int) {
        for (int y = y0; y < y1; ++y) for (int x = 0; x < W2; ++x) {
            float lo = 1e30f, hi = -1e30f;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                float v = z[size_t(clamp_i(y+dy,0,H2-1))*W2+clamp_i(x+dx,0,W2-1)];
                lo = std::min(lo,v); hi = std::max(hi,v);
            }
            size_t i = size_t(y)*W2+x;
            double v = clamp_f(den[i],lo,hi), dv = variance[size_t(y/4)*Wh+x/4];
            green[i] = float(sqrt_c*v+0.25*gain*(v*v+dv)-ped);
        }
    });
    z.release(); den.release();
    parallel_for_rows(H,nt,[&](int y0,int y1,int) {
        for (int y = y0; y < y1; ++y) for (int x = 0; x < W; ++x)
            guide[size_t(y)*W+x] = 0.5f*(green[size_t(2*y)*W2+2*x+1]+green[size_t(2*y+1)*W2+2*x]);
    });
    double mean = j.global_mean_g;
    cost_mark(3);
    if (mean < 0) {
        mean = 0;
        for (size_t i = 0; i < guide.size(); i += 16) mean += std::max(0.0f,guide[i]);
        mean /= double((guide.size()+15)/16);
    }
    if (j.chroma_denoise > 1e-4f) {
        std::vector<float> mr(size_t(H)*W), mb(size_t(H)*W);
        median3x3_plane(red.data(),mr.data(),H,W,nt);
        median3x3_plane(blue.data(),mb.data(),H,W,nt);
        if (j.is_astro) { red.swap(mr); blue.swap(mb); }
        else {
            median3x3_plane(mr.data(),red.data(),H,W,nt);
            median3x3_plane(mb.data(),blue.data(),H,W,nt);
        }
        if (j.glint_desat) {
            std::vector<float> bg(size_t(H)*W);
            sep_gaussian_2d(guide.data(),bg.data(),H,W,2.5f,nt);
            parallel_for_rows(H,nt,[&](int y0,int y1,int) {
                for (size_t i = size_t(y0)*W; i < size_t(y1)*W; ++i) {
                    float excess = std::max(0.0f,guide[i]-bg[i])+0.5f*std::max(0.0f,std::max(red[i],blue[i]));
                    float peak = std::max(guide[i],std::max(guide[i]+red[i],guide[i]+blue[i]));
                    float ds = std::max(clamp_f((excess-0.045f)/0.12f,0,0.92f),
                                        clamp_f((peak-0.32f)/0.35f,0,0.95f));
                    red[i] *= 1-ds; blue[i] *= 1-ds;
                }
            });
        }
    }
    float wbmax = std::max(j.wb_r,j.wb_b);
    float sigma = j.chroma_denoise*float(std::sqrt((1+wbmax*wbmax)*j.out_eta_eff*(j.pg_sig0_sq+j.pg_gain*mean)));
    atrous_global(red.data(),blue.data(),guide.data(),j,sigma);
    parallel_for_rows(H2,nt,[&](int y0,int y1,int) {
        for (int y = y0; y < y1; ++y) for (int x = 0; x < W2; ++x) {
            size_t i = size_t(y)*W2+x;
            float r = bilerp_clamped(red.data(),H,W,0.5f*y,0.5f*x);
            float b = bilerp_clamped(blue.data(),H,W,0.5f*(y-1),0.5f*(x-1));
            rgb[i*3] = std::max(0.0f,(green[i]+r)/std::max(1e-4f,j.wb_r));
            rgb[i*3+1] = std::max(0.0f,green[i]);
            rgb[i*3+2] = std::max(0.0f,(green[i]+b)/std::max(1e-4f,j.wb_b));
        }
    });
    j.out_resid_noise_2x = 0.35f*float(std::sqrt(sig2+gain*mean));
    std::vector<float>().swap(green);
    std::vector<float>().swap(red);
    std::vector<float>().swap(blue);
    std::vector<float>().swap(guide);
    std::vector<float>().swap(variance);
    cost_mark(4);
    if (j.psf_iters > 0) {
        std::vector<float> rl_gain(size_t(Hh)*Wh), rl_thr(rl_gain.size());
        for (size_t i = 0; i < rl_gain.size(); ++i) {
            rl_gain[i] = unpack_half(uint16_t(j.rl_mod_f16[i]));
            rl_thr[i] = unpack_half(uint16_t(j.rl_mod_f16[i] >> 16));
        }
        RlWorkspace ws;
        if(j.core_y1>0 && j.core_x1>0) {
            const int halo=j.psf_iters*heraw_gpu::make_olpf_psf_pc(1,1,0,j.psf_sigma,j.olpf_mode).radius+8;
            const int y0=std::max(0,(j.core_y0-halo)/2*2),x0=std::max(0,(j.core_x0-halo)/2*2);
            const int y1=std::min(H,(j.core_y1+halo+1)/2*2),x1=std::min(W,(j.core_x1+halo+1)/2*2);
            const int h=y1-y0,w=x1-x0;
            auto patch=heraw_gpu::gpu_crop(rgb,W2,y0*2,x0*2,h*2,w*2,3);
            auto gain_patch=heraw_gpu::gpu_crop(rl_gain.data(),Wh,y0/2,x0/2,h/2,w/2);
            auto threshold_patch=heraw_gpu::gpu_crop(rl_thr.data(),Wh,y0/2,x0/2,h/2,w/2);
            snr_gated_rl_deconv(patch.data(),h*2,w*2,j.psf_iters,j.psf_sigma,0.18f,j.out_resid_noise_2x,nt,
                ws,gain_patch.data(),threshold_patch.data(),h/2,w/2,j.is_astro,j.olpf_mode);
            for(int y=j.core_y0*2;y<j.core_y1*2;++y)
                std::copy_n(patch.data()+(size_t(y-y0*2)*(w*2)+(j.core_x0-x0)*2)*3,
                    size_t(j.core_x1-j.core_x0)*6,rgb+(size_t(y)*W2+j.core_x0*2)*3);
        } else {
            snr_gated_rl_deconv(rgb,H2,W2,j.psf_iters,j.psf_sigma,0.18f,j.out_resid_noise_2x,nt,
                ws,rl_gain.data(),rl_thr.data(),Hh,Wh,j.is_astro,j.olpf_mode);
        }
    }
    cost_mark(5);
    return true;
}

inline bool run_stage45_cpu(heraw_gpu::Stage45GpuJob& job) {
    auto work = job;
    // Filter micro-tiles stay cache-local; a larger outer core shares their halo.
    if (work.tile_size_1x > 0) work.tile_size_1x = std::min(960,work.tile_size_1x);
    work.keep_frames_lin = true;
    work.n_threads = std::max(1,work.n_threads);
    bool ok = heraw_gpu::run_stage45_streamed(work,run_stage45_cpu_roi);
    job.out_err = work.out_err;
    job.out_device_label = work.out_device_label;
    job.out_eta_eff = work.out_eta_eff;
    job.out_resid_noise_2x = work.out_resid_noise_2x;
    job.out_peak_tile_pixels = work.out_peak_tile_pixels;
    job.out_tile_count = work.out_tile_count;
    job.out_tile_diagnostics=work.out_tile_diagnostics;
    job.out_wall_seconds=work.out_wall_seconds;
    job.out_device_seconds=work.out_device_seconds;
    job.out_has_device_timing=work.out_has_device_timing;
    job.out_plan_seconds=work.out_plan_seconds;
    job.out_crop_seconds=work.out_crop_seconds;
    job.out_stitch_seconds=work.out_stitch_seconds;
    return ok;
}

} // namespace heraw_cpu
