// Standalone Stage45 public-entry numerical tests. Link the native GPU backend.
#include "../heraw_gpu_backend.hpp"
#include "../heraw_suite_core.hpp"
#ifndef __APPLE__
#include "../heraw_gpu_vulkan.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#ifdef GPU_TEST_LIBRAW
#include "../../libraw-heraw/libraw/libraw.h"
#endif

using namespace heraw_gpu;

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct Fixture {
    Stage45GpuJob j;
    std::vector<std::vector<float>> frames, fy, fx, mw;
    std::vector<float> cov, guide, anchor, ha, boost, rgb;
    std::vector<uint32_t> rl;
    Fixture(int H, int W, bool xt, int N = 3)
        : frames(N, std::vector<float>(size_t(H) * W)),
          fy(N, std::vector<float>(size_t(H/2) * (W/2))),
          fx(fy), mw(N, std::vector<float>(size_t(H/2) * (W/2), 1.0f)),
          cov(size_t(H/2) * (W/2) * 3), guide(size_t(H/2) * (W/2), .25f),
          anchor(size_t(H) * W), ha(size_t(H) * W, .25f),
          boost(size_t(H/2) * (W/2), 1.0f), rgb(size_t(H) * W * 12),
          rl(size_t(H/2) * (W/2), pack_half2_u32(1.0f, 1.0f)) {
        j.H1=H; j.W1=W; j.N=N; j.is_xtrans=xt; j.keep_frames_lin=true;
        j.frames_lin=&frames; j.flows_fy=&fy; j.flows_fx=&fx; j.mw_half=&mw;
        j.inv_cov=cov.data(); j.g_guide=guide.data(); j.c_anc_1x=anchor.data();
        j.g0_ha_1x=ha.data(); j.snr_boost_half=boost.data(); j.rl_mod_f16=rl.data();
        j.rgb_2x_out=rgb.data(); j.tile_size_1x=0;
        j.denoise_strength=0; j.chroma_denoise=0; j.psf_iters=0;
        j.pg_sig0_sq=1e-5; j.pg_gain=1e-4;
        if (xt) {
            const uint8_t cfa[6][6] = {
                {1,0,1,1,2,1}, {2,1,2,0,1,0}, {1,0,1,1,2,1},
                {1,2,1,1,0,1}, {0,1,0,2,1,2}, {1,2,1,1,0,1}};
            std::memcpy(j.cfa6x6, cfa, sizeof(cfa));
        }
        for (size_t i=0; i<guide.size(); ++i) { cov[3*i]=2; cov[3*i+2]=2; }
        flat();
    }
    void flat() {
        const float v[3]={.35f,.25f,.15f};
        for (int y=0; y<j.H1; ++y) for (int x=0; x<j.W1; ++x)
            anchor[size_t(y)*j.W1+x]=v[j.cfa6x6[y%6][x%6]];
        for (auto& f:frames) f=anchor;
    }
    void pattern() {
        for (int y=0; y<j.H1; ++y) for (int x=0; x<j.W1; ++x) {
            size_t i=size_t(y)*j.W1+x;
            float g=.22f+.05f*std::sin(.067f*x)+.04f*std::cos(.091f*y)
                    +.015f*std::sin(.49f*x+.19f*y);
            ha[i]=g;
            int ch=j.cfa6x6[y%6][x%6];
            anchor[i]=g+(ch==0 ? .06f : ch==2 ? -.04f : 0.0f);
            for (int n=0;n<j.N;++n)
                frames[n][i]=anchor[i]+.002f*std::sin(float((i*17+size_t(n)*151)%199));
        }
        for (int y=0;y<j.H1/2;++y) for(int x=0;x<j.W1/2;++x) {
            size_t i=size_t(y)*(j.W1/2)+x;
            guide[i]=ha[size_t(2*y)*j.W1+2*x];
            for(int n=1;n<j.N;++n) {
                fy[n][i]=.13f*n;
                fx[n][i]=.19f*n + .04f*std::sin(.03f*x);
            }
        }
    }
    void run() {
        if (!run_stage45_gpu(j)) throw std::runtime_error(j.out_err);
        for (float f:rgb) require(std::isfinite(f), "Non-finite GPU output");
    }
};

static double max_delta(const std::vector<float>& a, const std::vector<float>& b) {
    double d=0;
    for(size_t i=0;i<a.size();++i) d=std::max(d,double(std::fabs(a[i]-b[i])));
    return d;
}

static void flat_test(bool xt, bool rotated) {
    Fixture f(64,80,xt);
    if (rotated) {
        uint8_t old[6][6]; std::memcpy(old,f.j.cfa6x6,sizeof(old));
        for(int y=0;y<6;++y) for(int x=0;x<6;++x) f.j.cfa6x6[y][x]=old[(y+1)%6][(x+1)%6];
        f.flat();
    }
    f.run();
    const float expected[3]={.35f,.25f,.15f};
    double err=0;
    for(size_t i=0;i<f.rgb.size();++i) err=std::max(err,double(std::fabs(f.rgb[i]-expected[i%3])));
    std::printf("flat xt=%d rotated=%d max_error=%.9g\n",xt,rotated,err);
    require(err<2e-5,"Flat-field CFA color identity");
    f.j.is_clean_plate=true;
    for(auto& w:f.mw) std::fill(w.begin(),w.end(),0.0f);
    f.run(); // All accumulation weights collapse: anchor fallback remains channel-correct.
    err=0;
    for(size_t i=0;i<f.rgb.size();++i) err=std::max(err,double(std::fabs(f.rgb[i]-expected[i%3])));
    std::printf("fallback xt=%d rotated=%d max_error=%.9g\n",xt,rotated,err);
    require(err<2e-5,"CFA-aware fallback anchor");
}

static void modes_test(bool xt) {
    Fixture f(48,64,xt);
    for(float& v:f.frames[0]) v+=.3f;
    f.run(); auto normal=f.rgb;
    f.j.is_clean_plate=true; f.run(); auto clean=f.rgb;
    f.j.is_virtual_nd=true; f.run(); auto nd=f.rgb;
    for(int n=1;n<f.j.N;++n) std::fill(f.mw[n].begin(),f.mw[n].end(),0.0f);
    std::vector<float> lucky={.1f,4.0f,.01f};
    f.j.frame_lucky_w=&lucky;
    f.run();
    require(max_delta(nd,f.rgb)==0,"Virtual ND uniform temporal weighting");
    double dc=max_delta(normal,clean), dn=max_delta(nd,clean);
    std::printf("modes xt=%d clean_delta=%.9g nd_delta=%.9g\n",xt,dc,dn);
    require(dc>.02 && dn>.02,"Clean plate / ND causal effect");
    f.j.keep_frames_lin=false;
    f.j.tile_size_1x=80;
    f.j.max_tile_pixels=1;
    require(!run_stage45_gpu(f.j),"Budget failure");
    require(f.frames[0].size()==size_t(f.j.H1)*f.j.W1,"CPU fallback retains frames");
    f.j.max_tile_pixels=100000;
    f.run();
    require(f.frames[0].empty(),"Successful explicit frame release");
}

static void failure_test() {
    Fixture f(64,400,false);
    f.j.tile_size_1x=80;
    f.j.keep_frames_lin=false;
    int actual=0;
    bool ok=run_stage45_streamed(f.j,[&](Stage45GpuJob& sub) {
        if(sub.eta_only) { sub.out_eta_sum=1; sub.out_eta_count=4; return true; }
        if(++actual==2) { sub.out_err="Injected second-tile failure"; return false; }
        std::fill_n(sub.rgb_2x_out,size_t(sub.H1)*sub.W1*12,.2f);
        return true;
    });
    require(!ok && actual==2,"Second-pass GPU failure propagated");
    for(const auto& frame:f.frames) require(frame.size()==25600,"Mid-stream fallback frame retention");
    f.fy[1][0]=std::numeric_limits<float>::quiet_NaN();
    require(!run_stage45_gpu(f.j),"Non-finite flow rejected");
    std::puts("failure retention=PASS nonfinite=PASS");
}

#ifdef __APPLE__
static void sensor_residual_test() {
    Fixture f(64,80,true,4);
    f.pattern();
    std::vector<float> scene(size_t(32)*40*3);
    for(int y=0;y<32;++y) for(int x=0;x<40;++x) for(int ch=0;ch<3;++ch)
        scene[(y*40+x)*3+ch]=(x>30?.2f:.04f)+.005f*ch+.00001f*y;
    for(auto& raw:f.frames) for(float& v:raw) v*=.2f;
    f.frames[2][17]=std::numeric_limits<float>::quiet_NaN();
    f.mw[1][120]=0;
    auto residual=f.frames;
    SensorResidualJob job{64,80,&f.frames,&f.fy,&f.fx,&f.mw,scene.data(),&f.j.cfa6x6[0][0],1e-6f,.0001f,&residual};
    require(run_sensor_residual_gpu(job),"Metal sensor residual prepass");
    double peak=0;
    for(size_t n=0;n<f.frames.size();++n) for(int r=0;r<64;++r) for(int c=0;c<80;++c) {
        float y,x;
        heraw_core::detail::inverse_flow(f.fy,f.fx,n,32,40,float(r),float(c),y,x);
        bool valid=heraw_core::detail::inside(y,x,64,80);
        float expected=0;
        if(valid) {
            const int ch=f.j.cfa6x6[r%6][c%6];
            auto sample=[&](float yy,float xx) {return heraw_core::detail::sample(scene.data(),32,40,yy*.5f,xx*.5f,3,ch);};
            float estimate=sample(y,x),v=f.frames[n][r*80+c];
            float sigma=std::sqrt(std::max(1e-12f,job.read_variance+job.shot*std::max(0.0f,estimate)));
            float gx=.5f*(sample(y,x+1)-sample(y,x-1)),gy=.5f*(sample(y+1,x)-sample(y-1,x));
            valid=heraw_core::detail::field(f.mw,n,32,40,y*.5f,x*.5f,1)>=.7f &&
                std::isfinite(v+estimate)&&estimate<.15f&&gx*gx+gy*gy<4*sigma*sigma;
            expected=v-estimate;
        }
        require(valid==std::isfinite(residual[n][r*80+c]),"Sensor gate agrees with CPU model");
        if(valid) peak=std::max(peak,double(std::fabs(expected-residual[n][r*80+c])));
    }
    require(peak<1e-6,"Sensor GPU residual agrees with CPU model");
    std::printf("sensor GPU/CPU max=%g\n",peak);
}
#endif

static void stats_roi_test() {
    Fixture f(160,1762,false);
    f.j.tile_size_1x=320;
    f.j.denoise_strength=1; f.j.chroma_denoise=1; f.j.psf_iters=4;
    size_t pixels[2]={};
    require(run_stage45_streamed(f.j,[&](Stage45GpuJob& sub) {
        const int pass=sub.eta_only?0:1;
        pixels[pass]+=size_t(sub.H1)*sub.W1;
        if(sub.eta_only) { sub.out_eta_sum=1; sub.out_eta_count=4; }
        else std::fill_n(sub.rgb_2x_out,size_t(sub.H1)*sub.W1*12,.2f);
        return true;
    }),"Statistics ROI dispatch");
    require(pixels[0]*2<pixels[1],"Statistics pass excludes denoise halo work");
    require(f.j.out_eta_eff==.25,"Statistics reduction preserved across smaller ROIs");
    std::printf("statistics ROI: %zu vs render %zu pixels\n",pixels[0],pixels[1]);
}
static void eta_transfer_test() {
    Fixture f(160,1762,true,4);f.pattern();
    std::vector<float> eta(size_t((f.j.H1+3)/4)*((f.j.W1+3)/4),-1);
    f.j.eta_quarter_out=eta.data();
    f.run();const auto reference=eta;
    std::fill(eta.begin(),eta.end(),-1);
    f.j.tile_size_1x=320;f.run();
    require(max_delta(reference,eta)<1e-6,"Tiled fusion exports the same global local-variance map");
    require(*std::min_element(eta.begin(),eta.end())>=0,"Fusion eta covers each global sample");
}
static void budget_test(bool xt) {
    Fixture f(160,1762,xt,4);
    f.pattern();
    f.j.denoise_strength=.7f; f.j.chroma_denoise=.6f; f.j.psf_iters=2;
    std::fill(f.fx[1].begin(),f.fx[1].end(),60.0f);
    std::fill(f.fx[2].begin(),f.fx[2].end(),100000.0f);
    std::fill(f.mw[2].begin(),f.mw[2].end(),0.0f);
    // One active, in-sensor vertex next to masked extreme flow. Only actual
    // bilinear query sites that land in the sensor can contribute.
    const size_t vertex=size_t(40)*(f.j.W1/2)+440;
    f.fx[2][vertex]=0.0f; f.fy[2][vertex]=0.0f; f.mw[2][vertex]=1.0f;
    std::fill(f.fx[3].begin(),f.fx[3].end(),100000.0f);
    f.run(); auto full=f.rgb;
    f.j.tile_size_1x=960;
    f.j.max_tile_pixels=size_t(160)*1440;
    f.run();
    double err=max_delta(full,f.rgb);
    std::printf("budget xt=%d max=%.9g peak=%zu limit=%zu %s\n",xt,err,
                f.j.out_peak_tile_pixels,f.j.max_tile_pixels,f.j.out_tile_diagnostics.c_str());
    require(f.j.out_tile_count>2,"Oversized cores adaptively split");
    require(f.j.out_peak_tile_pixels<=f.j.max_tile_pixels,"Actual ROI remains within fixed budget");
    require(err<8e-5,"Adaptive and masked/outside-flow tile seam");
    f.j.is_virtual_nd=true;
    f.j.tile_size_1x=0; f.run(); full=f.rgb;
    f.j.tile_size_1x=960; f.run();
    err=max_delta(full,f.rgb);
    std::printf("budget ND xt=%d max=%.9g peak=%zu\n",xt,err,f.j.out_peak_tile_pixels);
    require(err<8e-5,"Virtual ND contributing-flow bounds preserve tile seam");
}

static void seam_test(bool xt, bool denoise, bool vertical=false) {
    // 1762 produces an 881-pixel pyramid level: its last DCT block shifts
    // backward across the shared-input halo in either orientation.
    Fixture f(vertical ? 1762 : 160,vertical ? 160 : 1762,xt);
    f.pattern();
    f.j.lca_r_k1=.0003f; f.j.lca_b_k2=-.0002f;
    std::vector<float> ois(size_t(f.j.N)*4,.08f);
    f.j.frame_ois_shifts=&ois;
    f.j.denoise_strength=denoise ? .7f : 0;
    f.j.chroma_denoise=denoise ? .6f : 0;
    f.j.psf_iters=2; f.j.olpf_mode=1;
    f.run(); auto full=f.rgb;
    double eta=f.j.out_eta_eff;
    f.j.tile_size_1x=480;
    f.run();
    double err=max_delta(full,f.rgb), seam=0;
    for(int y=0;y<f.j.H1*2;++y) for(int x=0;x<f.j.W1*2;++x)
        if((vertical ? y : x)%960<8 || (vertical ? y : x)%960>952)
            for(int ch=0;ch<3;++ch) {
                size_t i=(size_t(y)*f.j.W1*2+x)*3+ch;
                seam=std::max(seam,double(std::fabs(full[i]-f.rgb[i])));
            }
    std::printf("seam xt=%d denoise=%d vertical=%d tiles=%d peak_pixels=%zu max=%.9g seam=%.9g eta_delta=%.9g\n",
                xt,denoise,vertical,f.j.out_tile_count,f.j.out_peak_tile_pixels,err,seam,std::fabs(eta-f.j.out_eta_eff));
    require(f.j.out_tile_count==4,"Actual streaming tile count");
    require(f.j.out_peak_tile_pixels<size_t(f.j.H1)*f.j.W1,"Actual cropped dispatch");
    require(err<8e-5 && seam<8e-5,"Tile/full numerical seam");
    require(std::fabs(eta-f.j.out_eta_eff)<2e-7,"Global eta independence");
    if(denoise) {
        f.j.tile_size_1x=0;
        f.j.olpf_mode=2; f.run();
        double d=max_delta(full,f.rgb);
        std::printf("olpf xt=%d delta=%.9g\n",xt,d);
        require(d>1e-6,"OLPF kernel causal effect");
    } else {
        f.j.tile_size_1x=0;
        f.j.lca_r_k1=0; f.j.lca_b_k2=0; f.run();
        double d=max_delta(full,f.rgb);
        std::printf("lca xt=%d delta=%.9g\n",xt,d);
        require(d>1e-5,"LCA channel displacement causal effect");
    }
}

#ifdef GPU_TEST_LIBRAW
static void raw_test(const char* path) {
    LibRaw raw;
    require(raw.open_file(path)==LIBRAW_SUCCESS,"RAW open");
    require(raw.unpack()==LIBRAW_SUCCESS,"RAW unpack");
    const auto& s=raw.imgdata.sizes;
    const auto& color=raw.imgdata.color;
    bool xt=raw.imgdata.idata.filters==9;
    Fixture f(160,1762,xt);
    const int y0=int(s.height)/2-80, x0=(int(s.width)/2-881)&~1;
    for(int y=0;y<6;++y) for(int x=0;x<6;++x)
        f.j.cfa6x6[y][x]=uint8_t(raw.COLOR(y+y0,x+x0)==3 ? 1 : raw.COLOR(y+y0,x+x0));
    for(int y=0;y<f.j.H1;++y) for(int x=0;x<f.j.W1;++x) {
        unsigned sample=raw.imgdata.rawdata.raw_image[size_t(y+y0+s.top_margin)*s.raw_width+x+x0+s.left_margin];
        f.anchor[size_t(y)*f.j.W1+x]=std::max(0.0f,(float(sample)-color.black)/float(color.maximum-color.black));
    }
    for(auto& frame:f.frames) frame=f.anchor;
    for(int y=0;y<f.j.H1;++y) for(int x=0;x<f.j.W1;++x) {
        float sum=0; int count=0;
        for(int yy=std::max(0,y-2);yy<=std::min(f.j.H1-1,y+2);++yy)
            for(int xx=std::max(0,x-2);xx<=std::min(f.j.W1-1,x+2);++xx)
                if(f.j.cfa6x6[yy%6][xx%6]==1) { sum+=f.anchor[size_t(yy)*f.j.W1+xx]; ++count; }
        f.ha[size_t(y)*f.j.W1+x]=sum/std::max(1,count);
    }
    for(int y=0;y<f.j.H1/2;++y) for(int x=0;x<f.j.W1/2;++x)
        f.guide[size_t(y)*(f.j.W1/2)+x]=f.ha[size_t(y*2)*f.j.W1+x*2];
    f.j.denoise_strength=.8f; f.j.chroma_denoise=.8f; f.j.psf_iters=2;
    f.run(); auto full=f.rgb;
    f.j.tile_size_1x=480; f.run();
    double err=max_delta(full,f.rgb);
    std::printf("raw camera=%s %s xt=%d tile_max=%.9g path=%s\n",
                raw.imgdata.idata.make,raw.imgdata.idata.model,xt,err,path);
    require(err<8e-5,"Real RAW tile/full seam");
}
#endif

static void v3_seam_and_modes_test(bool xt, bool is_1x, bool vertical = false, bool defer_psf = true) {
    Fixture f(vertical ? 1762 : 160, vertical ? 160 : 1762, xt, 4);
    f.pattern();
    f.j.math_v3_unified = true;
    f.j.is_1x_mode = is_1x;
    f.j.defer_optical_psf = defer_psf;
    f.j.psf_iters = defer_psf ? 0 : 2;
    f.j.lca_r_k1 = 0.0003f;
    f.j.lca_b_k2 = -0.0002f;
    std::vector<float> ois(size_t(f.j.N) * 4, 0.08f);
    f.j.frame_ois_shifts = &ois;
    f.j.denoise_strength = 0.85f;
    f.j.chroma_denoise = 0.80f;

    const int out_s = is_1x ? 1 : 2;
    const size_t n_out = size_t(f.j.H1) * f.j.W1 * out_s * out_s;
    const size_t n_eta = size_t((f.j.H1 + 3) / 4) * ((f.j.W1 + 3) / 4);
    f.rgb.resize(n_out * 3);
    std::vector<float> fc_full(n_out, -1.0f), eta_full(n_eta, -1.0f);
    f.j.rgb_2x_out = f.rgb.data();
    f.j.fisher_comp_out = fc_full.data();
    f.j.eta_quarter_out = eta_full.data();
    f.run();
    auto rgb_full = f.rgb;
    const double eta_eff = f.j.out_eta_eff;

    std::vector<float> fc_tile(n_out, -1.0f), eta_tile(n_eta, -1.0f);
    f.j.tile_size_1x = 480;
    f.j.fisher_comp_out = fc_tile.data();
    f.j.eta_quarter_out = eta_tile.data();
    f.run();

    const double err_rgb = max_delta(rgb_full, f.rgb);
    const double err_fc  = max_delta(fc_full, fc_tile);
    const double err_eta = max_delta(eta_full, eta_tile);
    const double err_g   = std::fabs(eta_eff - f.j.out_eta_eff);
    std::printf("v3_seam xt=%d s=%d vert=%d defer=%d tiles=%d peak=%zu rgb_max=%.9g fc_max=%.9g eta_q=%.9g eta=%.9g\n",
                xt, out_s, vertical, defer_psf, f.j.out_tile_count, f.j.out_peak_tile_pixels, err_rgb, err_fc, err_eta, err_g);
    require(f.j.out_tile_count == 4, "V3 streaming tile count");
    require(f.j.out_peak_tile_pixels < size_t(f.j.H1) * f.j.W1, "V3 cropped dispatch");
    require(err_g < 2e-7 && err_eta < 1e-6, "V3 eta independence");
    require(err_fc < 5e-5, "V3 Fisher complement seam parity");
    require(err_rgb < (defer_psf ? 1e-4 : 2.5e-4), "V3 tiled vs untiled RGB seam parity");
}

static void m5a_gls_gpu_test(int out_scale) {
    const int H1 = 64, W1 = 64, N = 5;
    const int Hh = H1 / 2, Wh = W1 / 2;
    const int H_out = H1 * out_scale, W_out = W1 * out_scale;
    uint8_t cfa[6][6];
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 6; ++c)
            cfa[r][c] = ((r & 1) == 0) ? ((c & 1) == 0 ? 0 : 1) : ((c & 1) == 0 ? 1 : 2);

    std::vector<float> gt((size_t)H_out * W_out * 3);
    for (int y = 0; y < H_out; ++y) {
        for (int x = 0; x < W_out; ++x) {
            const float fx = float(x) / float(W_out);
            const float fy = float(y) / float(H_out);
            const float edge = (std::sin(0.32f * x + 0.19f * y) > 0.0f) ? 0.28f : 0.08f;
            const size_t p = ((size_t)y * W_out + x) * 3;
            gt[p + 1] = edge + 0.12f * fx + 0.08f * std::sin(0.45f * x);
            gt[p + 0] = gt[p + 1] * (1.15f + 0.1f * fy);
            gt[p + 2] = gt[p + 1] * (0.85f + 0.1f * fx);
        }
    }

    std::vector<std::vector<float>> raw(N, std::vector<float>((size_t)H1 * W1, 0.0f));
    std::vector<std::vector<float>> fy(N, std::vector<float>((size_t)Hh * Wh, 0.0f));
    std::vector<std::vector<float>> fx(N, std::vector<float>((size_t)Hh * Wh, 0.0f));
    std::vector<std::vector<float>> mw(N, std::vector<float>((size_t)Hh * Wh, 1.0f));
    for (int n = 0; n < N; ++n) {
        const float dy = (n == 0) ? 0.0f : (0.18f * (n - 2));
        const float dx = (n == 0) ? 0.0f : (-0.15f * ((n & 1) ? 1.0f : -1.0f));
        std::fill(fy[n].begin(), fy[n].end(), 0.5f * dy);
        std::fill(fx[n].begin(), fx[n].end(), 0.5f * dx);
        for (int r = 0; r < H1; ++r) {
            for (int c = 0; c < W1; ++c) {
                const int ch = cfa[r % 6][c % 6];
                const int yr = std::max(0, std::min(H_out - 1, int(std::lround((float(r) - dy) * out_scale))));
                const int xc = std::max(0, std::min(W_out - 1, int(std::lround((float(c) - dx) * out_scale))));
                raw[n][(size_t)r * W1 + c] = gt[((size_t)yr * W_out + xc) * 3 + ch];
            }
        }
    }

    // Perturb initial RGB to create real forward residuals
    std::vector<float> rgb_cpu = gt;
    for (size_t i = 0; i < rgb_cpu.size(); ++i) rgb_cpu[i] *= 0.85f;
    std::vector<float> rgb_gpu = rgb_cpu;
    std::vector<float> fcomp((size_t)H_out * W_out, 0.45f);

    heraw_core::Noise noise{1e-6f, 2e-5f};
    heraw_core::UnifiedGlsOptions uo;
    uo.out_scale = out_scale;
    uo.iterations = 2;
    uo.morozov_target = 1.05f;
    uo.optical_psf_sigma_1x = 0.50f;
    uo.jacobian_prox_strength = 0.35f;
    uo.fisher_comp = fcomp.data();

    const auto cpu_st = heraw_core::unified_forward_gls(
        raw, H1, W1, &cfa[0][0], fy, fx, mw, noise, rgb_cpu.data(), uo);

    M5aGlsGpuJob job;
    job.H1 = H1; job.W1 = W1; job.N = N;
    job.out_scale = out_scale;
    job.iterations = uo.iterations;
    job.morozov_target = uo.morozov_target;
    job.optical_psf_sigma_1x = uo.optical_psf_sigma_1x;
    job.jacobian_prox_strength = uo.jacobian_prox_strength;
    job.read_variance = noise.read_variance;
    job.shot = noise.shot;
    job.quad = noise.quad;
    std::memcpy(job.cfa6x6, cfa, sizeof(cfa));
    job.raw = &raw; job.fy = &fy; job.fx = &fx; job.mw = &mw;
    job.fisher_comp = fcomp.data();
    job.rgb = rgb_gpu.data();
    require(run_m5a_gls_gpu(job), "run_m5a_gls_gpu returned false");

    const double err_rgb = max_delta(rgb_cpu, rgb_gpu);
    double sum_sq = 0.0;
    for (size_t i = 0; i < rgb_cpu.size(); ++i) {
        const double d = double(rgb_cpu[i]) - double(rgb_gpu[i]);
        sum_sq += d * d;
    }
    const double rms = std::sqrt(sum_sq / double(rgb_cpu.size()));
    std::printf("m5a_gls sc=%d cpu=%.2f->%.2f gpu=%.2f->%.2f max=%.6g rms=%.6g\n",
                out_scale, cpu_st.before, cpu_st.after, job.out_before, job.out_after, err_rgb, rms);
    require(cpu_st.after < cpu_st.before * 0.5, "M5-A CPU causal chi2 reduction");
    require(job.out_after < job.out_before * 0.5, "M5-A GPU causal chi2 reduction");
    require(err_rgb < 1.5e-3 && rms < 1.5e-4, "M5-A CPU/GPU parity");
}

static void m5b_deconv_gpu_test(int out_scale) {
    const int H = 96, W = 96;
    std::vector<float> sharp((size_t)H * W * 3), blur((size_t)H * W * 3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float e = 1.0f / (1.0f + std::exp(-((float(x) - 48.0f) + 0.25f * (float(y) - 48.0f)) * 2.5f));
            const float v = 0.08f + 0.52f * e;
            const size_t p = ((size_t)y * W + x) * 3;
            sharp[p + 0] = v * 1.1f; sharp[p + 1] = v; sharp[p + 2] = v * 0.9f;
        }
    }
    const float k3[3] = {0.22f, 0.56f, 0.22f};
    std::vector<float> tmp((size_t)H * W * 3);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int c = 0; c < 3; ++c) {
                float s = 0.0f;
                for (int d = -1; d <= 1; ++d) {
                    const int xx = std::max(0, std::min(W - 1, x + d));
                    s += k3[d + 1] * sharp[((size_t)y * W + xx) * 3 + c];
                }
                tmp[((size_t)y * W + x) * 3 + c] = s;
            }
        }
    }
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            for (int c = 0; c < 3; ++c) {
                float s = 0.0f;
                for (int d = -1; d <= 1; ++d) {
                    const int yy = std::max(0, std::min(H - 1, y + d));
                    s += k3[d + 1] * tmp[((size_t)yy * W + x) * 3 + c];
                }
                blur[((size_t)y * W + x) * 3 + c] = s;
            }
        }
    }
    auto edge_mse = [&](const std::vector<float>& img) {
        double e = 0.0; size_t cnt = 0;
        for (int y = 12; y < H - 12; ++y) {
            for (int x = 12; x < W - 12; ++x) {
                const size_t p = ((size_t)y * W + x) * 3;
                for (int c = 0; c < 3; ++c) {
                    const double d = double(img[p + c]) - double(sharp[p + c]);
                    e += d * d; ++cnt;
                }
            }
        }
        return e / double(cnt);
    };
    const double mse0 = edge_mse(blur);
    std::vector<float> rgb_cpu = blur, rgb_gpu = blur;
    std::vector<float> focus_half(size_t(H / 2) * (W / 2), 0.9f);
    heraw_core::OpticalZone3Ch cz{};
    cz.cx = 0.5f; cz.cy = 0.5f;
    cz.sigma_major_px = 1.25f; cz.sigma_minor_px = 1.05f; cz.angle_rad = 0.25f;
    cz.sigma_rgb_px[0] = 1.30f; cz.sigma_rgb_px[1] = 1.25f; cz.sigma_rgb_px[2] = 1.35f;
    cz.valid = true;

    heraw_core::TerminalDeconvOptions to;
    to.out_scale = out_scale; to.psf_iters = 4; to.strength = 0.9f;
    to.psf_sigma_2x = 1.25f; to.olpf_mode = 1;
    to.focus_half = focus_half.data(); to.focus_h = H / 2; to.focus_w = W / 2;
    const auto cpu_st = heraw_core::terminal_optical_deconvolve(
        rgb_cpu.data(), H, W, {cz}, {1e-7f, 1e-6f}, to);

    M5bDeconvGpuJob job;
    job.rgb = rgb_gpu.data(); job.H = H; job.W = W;
    job.out_scale = out_scale; job.psf_iters = to.psf_iters;
    job.strength = to.strength; job.psf_sigma_2x = to.psf_sigma_2x; job.olpf_mode = to.olpf_mode;
    job.read_variance = 1e-7f; job.shot = 1e-6f;
    job.focus_half = focus_half.data(); job.focus_h = H / 2; job.focus_w = W / 2;
    M5bZoneParam gz;
    gz.cx = cz.cx; gz.cy = cz.cy;
    gz.sigma_major_px = cz.sigma_major_px; gz.sigma_minor_px = cz.sigma_minor_px; gz.angle_rad = cz.angle_rad;
    gz.sigma_rgb_px[0] = cz.sigma_rgb_px[0]; gz.sigma_rgb_px[1] = cz.sigma_rgb_px[1]; gz.sigma_rgb_px[2] = cz.sigma_rgb_px[2];
    job.zones.push_back(gz);
    require(run_m5b_deconv_gpu(job), "run_m5b_deconv_gpu returned false");

    const double mse_cpu = edge_mse(rgb_cpu), mse_gpu = edge_mse(rgb_gpu);
    const double err_rgb = max_delta(rgb_cpu, rgb_gpu);
    std::printf("m5b_deconv sc=%d mse %.6g->cpu=%.6g gpu=%.6g chg=%zu max=%.6g\n",
                out_scale, mse0, mse_cpu, mse_gpu, job.out_changed, err_rgb);
    require(mse_cpu < mse0 && mse_gpu < mse0, "M5-B causal edge sharpness improvement");
    require(cpu_st.changed > 0 && job.out_changed > 0, "M5-B modified active edge pixels");
    require(err_rgb < 2e-4, "M5-B CPU/GPU parity");
}

static void silicon_cleanup_full_gpu_test() {
    const int H = 96, W = 96, N = 5;
    const int Hh = H / 2, Wh = W / 2;
    uint8_t cfa[6][6];
    for (int r = 0; r < 6; ++r)
        for (int c = 0; c < 6; ++c)
            cfa[r][c] = ((r & 1) == 0) ? ((c & 1) == 0 ? 0 : 1) : ((c & 1) == 0 ? 1 : 2);

    std::vector<std::vector<float>> raw_base(N, std::vector<float>((size_t)H * W, 0.0f));
    std::vector<std::vector<float>> fy(N, std::vector<float>((size_t)Hh * Wh, 0.0f));
    std::vector<std::vector<float>> fx(N, std::vector<float>((size_t)Hh * Wh, 0.0f));
    std::vector<std::vector<float>> mw(N, std::vector<float>((size_t)Hh * Wh, 1.0f));
    for (int n = 0; n < N; ++n) {
        const float sh_y = 0.5f * float(n) * 4.0f;
        const float sh_x = 0.5f * float(n) * 3.0f;
        std::fill(fy[n].begin(), fy[n].end(), sh_y);
        std::fill(fx[n].begin(), fx[n].end(), sh_x);
        for (int r = 0; r < H; ++r) {
            for (int c = 0; c < W; ++c) {
                const float sy = float(r) - 2.0f * sh_y, sx = float(c) - 2.0f * sh_x;
                float val = 0.05f + 0.02f * std::sin(0.05f * sx + 0.03f * sy);
                if (n == 1 && (r == 24 || r == 25)) val += 0.012f * ((c & 3) == 1 ? 1.0f : -0.5f);
                if (r == 40 && c == 40) val += 0.08f; // consensus hot pixel
                raw_base[n][(size_t)r * W + c] = val;
            }
        }
    }
    auto raw_cpu = raw_base, raw_gpu = raw_base;
    const heraw_core::Noise noise{1e-6f, 2e-5f};
    const auto cpu_st = heraw_core::silicon_residual_cleanup(
        raw_cpu, H, W, &cfa[0][0], fy, fx, mw, noise);

    SiliconCleanupGpuJob job;
    job.H = H; job.W = W; job.raw = &raw_gpu;
    job.fy = &fy; job.fx = &fx; job.mw = &mw; job.cfa = &cfa[0][0];
    job.read_variance = noise.read_variance; job.shot = noise.shot; job.quad = noise.quad;
    job.n_threads = 2;
    require(run_silicon_cleanup_gpu(job), "run_silicon_cleanup_gpu returned false");

    double max_diff = 0.0;
    for (int n = 0; n < N; ++n) max_diff = std::max(max_diff, max_delta(raw_cpu[n], raw_gpu[n]));
    std::printf("c13_full cpu_after=%.6g (before=%.6g) gpu_after=%.6g max=%.6g hot_gpu=%.5f\n",
                cpu_st.after, cpu_st.before, job.out_after, max_diff, raw_gpu[0][40 * W + 40]);
    require(cpu_st.after < cpu_st.before * 0.25 && job.out_after < job.out_before * 0.25, "C13 causal FPN/hot reduction");
    require(max_diff < 5e-5, "C13 full CPU/GPU parity");
}

int main(int argc,char** argv) {
    const bool photo=argc>1 && std::strcmp(argv[1],"--photo")==0;
#ifdef __APPLE__
    sensor_residual_test();
#endif
    stats_roi_test();
    eta_transfer_test();
    try {
        auto dev=probe_gpu_device();
        require(dev.available,"GPU device/runtime shader compile");
        std::printf("device=%s %s\n",dev.api_name.c_str(),dev.device_name.c_str());
        if(!photo) {
            failure_test();
            m5a_gls_gpu_test(1);
            m5a_gls_gpu_test(2);
            m5b_deconv_gpu_test(1);
            m5b_deconv_gpu_test(2);
            silicon_cleanup_full_gpu_test();
        }
        for(bool xt:{false,true}) {
            flat_test(xt,false); flat_test(xt,true); modes_test(xt);
            if(!photo) budget_test(xt);
            seam_test(xt,false); seam_test(xt,true); seam_test(xt,true,true);
            v3_seam_and_modes_test(xt,false,false,true);
            v3_seam_and_modes_test(xt,false,true,true);
            v3_seam_and_modes_test(xt,false,true,false);
            v3_seam_and_modes_test(xt,true,false,true);
            v3_seam_and_modes_test(xt,true,true,true);
        }
#ifdef GPU_TEST_LIBRAW
        for(int i=photo?2:1;i<argc;++i) raw_test(argv[i]);
#else
        (void)argc; (void)argv;
#endif
        std::puts("SUITE_GPU_PASS");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"SUITE_GPU_FAIL: %s\n",e.what());
        return 1;
    }
}
