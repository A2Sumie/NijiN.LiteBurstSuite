// Standalone Stage45 public-entry numerical tests. Link the native GPU backend.
#include "../heraw_gpu_backend.hpp"
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

int main(int argc,char** argv) {
    try {
        auto dev=probe_gpu_device();
        require(dev.available,"GPU device/runtime shader compile");
        std::printf("device=%s %s\n",dev.api_name.c_str(),dev.device_name.c_str());
        failure_test();
        for(bool xt:{false,true}) {
            flat_test(xt,false); flat_test(xt,true); modes_test(xt);
            budget_test(xt);
            seam_test(xt,false); seam_test(xt,true); seam_test(xt,true,true);
        }
#ifdef GPU_TEST_LIBRAW
        for(int i=1;i<argc;++i) raw_test(argv[i]);
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
