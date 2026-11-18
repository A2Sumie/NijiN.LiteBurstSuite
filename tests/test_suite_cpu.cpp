// Exercise the same CPU helpers and job entry point used by the application.
// CPU_TEST_METAL links the native Metal backend; --cli compares actual CLI jobs.
#ifdef CPU_TEST_METAL
#include "../heraw_gpu_backend.hpp"
namespace heraw_gpu { bool run_stage45_gpu_checked(Stage45GpuJob& job); }
#define run_stage45_gpu run_stage45_gpu_checked
#endif
#define main nijin_cli_main
#include "../heraw_sr2x.cpp"
#undef main
#ifdef CPU_TEST_METAL
#undef run_stage45_gpu
#endif
#include "../heraw_cpu_stage45.hpp"
#include <stdexcept>

static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

struct CpuFixture {
    heraw_gpu::Stage45GpuJob job;
    std::vector<std::vector<float>> frames, fy, fx, mw;
    std::vector<float> cov, guide, anchor, ha, boost, rgb;
    std::vector<uint32_t> rl;
    CpuFixture(int H, int W, bool xt, int N = 3)
        : frames(N,std::vector<float>(size_t(H)*W)),
          fy(N,std::vector<float>(size_t(H/2)*(W/2))), fx(fy),
          mw(N,std::vector<float>(size_t(H/2)*(W/2),1)),
          cov(size_t(H/2)*(W/2)*3), guide(size_t(H/2)*(W/2),0.25f),
          anchor(size_t(H)*W), ha(size_t(H)*W,0.25f),
          boost(size_t(H/2)*(W/2),1), rgb(size_t(H)*W*12),
          rl(size_t(H/2)*(W/2),heraw_gpu::pack_half2_u32(1,1)) {
        job.H1=H; job.W1=W; job.N=N; job.n_threads=2; job.is_xtrans=xt;
        job.frames_lin=&frames; job.flows_fy=&fy; job.flows_fx=&fx; job.mw_half=&mw;
        job.inv_cov=cov.data(); job.g_guide=guide.data(); job.c_anc_1x=anchor.data();
        job.g0_ha_1x=ha.data(); job.snr_boost_half=boost.data(); job.rl_mod_f16=rl.data();
        job.rgb_2x_out=rgb.data(); job.tile_size_1x=0;
        job.denoise_strength=0; job.chroma_denoise=0; job.psf_iters=0;
        job.pg_sig0_sq=1e-5; job.pg_gain=1e-4;
        if (xt) {
            const uint8_t cfa[6][6] = {
                {1,0,1,1,2,1},{2,1,2,0,1,0},{1,0,1,1,2,1},
                {1,2,1,1,0,1},{0,1,0,2,1,2},{1,2,1,1,0,1}};
            std::memcpy(job.cfa6x6,cfa,sizeof(cfa));
        }
        for(size_t i=0;i<guide.size();++i) { cov[3*i]=2; cov[3*i+2]=2; }
        flat();
    }
    void flat() {
        const float values[3]={0.35f,0.25f,0.15f};
        for(int y=0;y<job.H1;++y) for(int x=0;x<job.W1;++x)
            anchor[size_t(y)*job.W1+x]=values[job.cfa6x6[y%6][x%6]];
        for(auto& frame:frames) frame=anchor;
    }
    void pattern() {
        for(int y=0;y<job.H1;++y) for(int x=0;x<job.W1;++x) {
            size_t i=size_t(y)*job.W1+x;
            float g=0.22f+0.05f*std::sin(0.067f*x)+0.04f*std::cos(0.091f*y)
                +0.015f*std::sin(0.49f*x+0.19f*y);
            ha[i]=g;
            int ch=job.cfa6x6[y%6][x%6];
            anchor[i]=g+(ch==0 ? 0.06f : ch==2 ? -0.04f : 0);
            for(int n=0;n<job.N;++n)
                frames[n][i]=anchor[i]+0.002f*std::sin(float((i*17+size_t(n)*151)%199));
        }
        for(int y=0;y<job.H1/2;++y) for(int x=0;x<job.W1/2;++x) {
            size_t i=size_t(y)*(job.W1/2)+x;
            guide[i]=ha[size_t(y*2)*job.W1+x*2];
            for(int n=1;n<job.N;++n) { fy[n][i]=0.13f*n; fx[n][i]=0.19f*n+0.04f*std::sin(0.03f*x); }
        }
    }
    void run() {
        if(!heraw_cpu::run_stage45_cpu(job)) throw std::runtime_error(job.out_err);
        for(float v:rgb) require(std::isfinite(v),"finite CPU output");
        for(const auto& frame:frames) require(frame.size()==size_t(job.H1)*job.W1,"RAW retained");
    }
};

static double delta(const std::vector<float>& a,const std::vector<float>& b) {
    double d=0;
    for(size_t i=0;i<a.size();++i) d=std::max(d,double(std::fabs(a[i]-b[i])));
    return d;
}

#ifdef CPU_TEST_METAL
namespace heraw_gpu {
bool run_stage45_gpu_checked(Stage45GpuJob& job) {
    auto cpu = job, gpu = job;
    cpu.keep_frames_lin = gpu.keep_frames_lin = true;
    std::vector<float> reference(size_t(job.H1)*job.W1*12);
    cpu.rgb_2x_out = reference.data();
    if (!heraw_cpu::run_stage45_cpu(cpu)) { job.out_err=cpu.out_err; return false; }
    if (!run_stage45_gpu(gpu)) { job.out_err=gpu.out_err; return false; }
    double error=0, square=0, interior=0;
    for(int y=0;y<job.H1*2;++y) for(int x=0;x<job.W1*2;++x) for(int c=0;c<3;++c) {
        size_t i=(size_t(y)*job.W1*2+x)*3+c;
        require(std::isfinite(reference[i]) && std::isfinite(gpu.rgb_2x_out[i]),
                "CPU/Metal finite same-job output");
        double d=std::fabs(double(reference[i])-gpu.rgb_2x_out[i]);
        error=std::max(error,d); square+=d*d;
        if(y>16 && x>16 && y<job.H1*2-16 && x<job.W1*2-16) interior=std::max(interior,d);
    }
    const double eta=std::fabs(cpu.out_eta_eff-gpu.out_eta_eff);
    std::printf("CPU/Metal same-job CFA=%s max=%.9g interior=%.9g rmse=%.9g eta_delta=%.9g CPU_eta=%.9g GPU_eta=%.9g\n",
        job.is_xtrans ? "XTrans" : "Bayer",error,interior,std::sqrt(square/reference.size()),
        eta,cpu.out_eta_eff,gpu.out_eta_eff);
    require(eta<2e-7,"CPU/Metal same-job effective variance parity");
    require(error<2e-6,"CPU/Metal same-job linear RGB parity");
    job.out_eta_eff=gpu.out_eta_eff; job.out_resid_noise_2x=gpu.out_resid_noise_2x;
    job.out_device_label=gpu.out_device_label; job.out_err=gpu.out_err;
    return true;
}
}

static void metal_test(bool xt) {
    CpuFixture f(64,400,xt);
    f.pattern();
    f.job.lca_r_k1=0.0003f; f.job.lca_b_k2=-0.0002f;
    std::vector<float> ois(size_t(f.job.N)*4,0.08f), lucky={0.2f,1.3f,0.8f};
    f.job.frame_ois_shifts=&ois;
    f.job.frame_lucky_w=&lucky;
    for(int mode=0;mode<3;++mode) {
        f.job.is_clean_plate=mode==1; f.job.is_virtual_nd=mode==2;
        for(int tile:{0,80}) {
            f.job.tile_size_1x=tile;
            require(heraw_gpu::run_stage45_gpu_checked(f.job),"same-job Metal dispatch");
        }
    }
    f.job.is_virtual_nd=false; f.job.is_clean_plate=true;
    f.job.pg_sig0_sq=0; f.job.pg_gain=0;
    for(auto& weights:f.mw) std::fill(weights.begin(),weights.end(),0);
    require(heraw_gpu::run_stage45_gpu_checked(f.job),"same-job zero-noise anchor fallback");
}
#endif

static void flat_test(bool xt,bool rotated) {
    CpuFixture f(32,48,xt);
    if(rotated) {
        uint8_t old[6][6]; std::memcpy(old,f.job.cfa6x6,sizeof(old));
        for(int y=0;y<6;++y) for(int x=0;x<6;++x) f.job.cfa6x6[y][x]=old[(y+1)%6][(x+1)%6];
        f.flat();
    }
    for(int mode=0;mode<3;++mode) {
        if(mode==1) { f.job.pg_gain=0; f.job.pg_sig0_sq=0; }
        if(mode==2) {
            f.job.is_clean_plate=true;
            for(auto& w:f.mw) std::fill(w.begin(),w.end(),0);
        }
        f.run();
        const float expected[3]={0.35f,0.25f,0.15f};
        double error=0;
        for(size_t i=0;i<f.rgb.size();++i) error=std::max(error,double(std::fabs(f.rgb[i]-expected[i%3])));
        std::printf("flat xt=%d rotated=%d mode=%d error=%.9g\n",xt,rotated,mode,error);
        require(error<2e-5,"flat CFA identity / zero noise / weight collapse");
    }
}

static void modes_test(bool xt) {
    CpuFixture f(32,48,xt);
    for(float& v:f.frames[0]) v+=0.3f;
    f.run(); auto normal=f.rgb;
    f.job.is_clean_plate=true; f.run(); auto clean=f.rgb;
    f.job.is_virtual_nd=true; f.run(); auto nd=f.rgb;
    for(int n=1;n<f.job.N;++n) std::fill(f.mw[n].begin(),f.mw[n].end(),0);
    std::vector<float> lucky={0.1f,4,0.01f};
    f.job.frame_lucky_w=&lucky; f.run();
    require(delta(nd,f.rgb)==0,"ND uniform temporal weighting");
    double dc=delta(normal,clean), dn=delta(nd,clean);
    std::printf("modes xt=%d clean=%.9g nd=%.9g\n",xt,dc,dn);
    require(dc>0.02 && dn>0.02,"clean plate and virtual ND causal delta");
    f.job.tile_size_1x=80; f.job.max_tile_pixels=1;
    require(!heraw_cpu::run_stage45_cpu(f.job),"tile budget rejected");
    require(f.frames[0].size()==f.anchor.size(),"failed dispatch retains frames");
    f.job.max_tile_pixels=100000; f.fy[1][0]=std::numeric_limits<float>::quiet_NaN();
    require(!heraw_cpu::run_stage45_cpu(f.job),"NaN flow rejected");
}

static void compare_tiles(CpuFixture& f,bool vertical,const char* name) {
    f.job.tile_size_1x=0; f.run(); auto full=f.rgb;
    const double eta=f.job.out_eta_eff;
    const auto originals=f.frames;
    f.job.tile_size_1x=480; f.run();
    require(f.frames==originals,"input RAW values retained exactly");
    double error=delta(full,f.rgb), seam=0;
    for(int y=0;y<f.job.H1*2;++y) for(int x=0;x<f.job.W1*2;++x)
        if((vertical ? y : x)%960<8 || (vertical ? y : x)%960>952)
            for(int c=0;c<3;++c) {
                size_t i=(size_t(y)*f.job.W1*2+x)*3+c;
                seam=std::max(seam,double(std::fabs(full[i]-f.rgb[i])));
            }
    std::printf("%s xt=%d vertical=%d tiles=%d peak=%zu max=%.9g seam=%.9g eta=%.9g\n",
        name,f.job.is_xtrans,vertical,f.job.out_tile_count,f.job.out_peak_tile_pixels,
        error,seam,std::fabs(eta-f.job.out_eta_eff));
    require(f.job.out_tile_count==4,"four actual tile dispatches");
    require(f.job.out_peak_tile_pixels<size_t(f.job.H1)*f.job.W1,"bounded cropped scratch");
    require(error<8e-5 && seam<8e-5,"full vs tile seamless output");
    require(std::fabs(eta-f.job.out_eta_eff)<2e-7,"global eta tile independence");
}

static void seam_test(bool xt,bool denoise,bool vertical) {
    CpuFixture f(vertical ? 1762 : 64,vertical ? 64 : 1762,xt);
    f.pattern(); f.job.lca_r_k1=0.0003f; f.job.lca_b_k2=-0.0002f;
    std::vector<float> ois(size_t(f.job.N)*4,0.08f);
    f.job.frame_ois_shifts=&ois;
    f.job.denoise_strength=denoise ? 0.7f : 0; f.job.chroma_denoise=denoise ? 0.6f : 0;
    f.job.psf_iters=2; f.job.olpf_mode=1;
    compare_tiles(f,vertical,"seam");
    auto original=f.rgb;
    if(denoise) f.job.olpf_mode=2;
    else { f.job.lca_r_k1=0; f.job.lca_b_k2=0; }
    f.run();
    double d=delta(original,f.rgb);
    std::printf("causal %s xt=%d delta=%.9g\n",denoise ? "OLPF" : "LCA",xt,d);
    require(d>1e-6,"OLPF / optics causal effect");
}

static void raw_test(const char* path) {
    LibRaw raw;
    require(raw.open_file(path)==LIBRAW_SUCCESS,"RAW open");
    require(raw.unpack()==LIBRAW_SUCCESS,"RAW unpack");
    const auto& s=raw.imgdata.sizes;
    const auto& color=raw.imgdata.color;
    CpuFixture f(64,1762,raw.imgdata.idata.filters==9);
    require(s.height>=64 && s.width>=1762,"RAW crop extent");
    int y0=int(s.height)/2-32, x0=(int(s.width)/2-881)&~1;
    for(int y=0;y<6;++y) for(int x=0;x<6;++x)
        f.job.cfa6x6[y][x]=uint8_t(raw.COLOR(y+y0,x+x0)==3 ? 1 : raw.COLOR(y+y0,x+x0));
    for(int y=0;y<f.job.H1;++y) for(int x=0;x<f.job.W1;++x) {
        auto sample=raw.imgdata.rawdata.raw_image[size_t(y+y0+s.top_margin)*s.raw_width+x+x0+s.left_margin];
        f.anchor[size_t(y)*f.job.W1+x]=std::max(0.0f,(float(sample)-color.black)/float(color.maximum-color.black));
    }
    for(auto& frame:f.frames) frame=f.anchor;
    for(int y=0;y<f.job.H1;++y) for(int x=0;x<f.job.W1;++x)
        f.ha[size_t(y)*f.job.W1+x]=sample_cfa_channel(f.anchor.data(),f.job.H1,f.job.W1,y,x,1,f.job.cfa6x6);
    for(int y=0;y<f.job.H1/2;++y) for(int x=0;x<f.job.W1/2;++x)
        f.guide[size_t(y)*(f.job.W1/2)+x]=f.ha[size_t(y*2)*f.job.W1+x*2];
    f.job.denoise_strength=0.8f; f.job.chroma_denoise=0.8f; f.job.psf_iters=2;
    compare_tiles(f,false,"real-RAW");
}

static void rgb_band_test() {
    CpuFixture f(832,672,false);
    f.pattern();f.run();
    std::vector<float> gain(size_t(416)*336),threshold(gain.size(),1);
    for(size_t i=0;i<gain.size();++i) gain[i]=.6f+.3f*std::sin(float(i%336)*.01f);
    for(int olpf:{0,2}) {
        auto full=f.rgb,bands=f.rgb;
        RlWorkspace a,b;
        snr_gated_rl_deconv(full.data(),1664,1344,2,.88f,.13f,.001f,2,a,
            gain.data(),threshold.data(),416,336,false,olpf);
        snr_gated_rl_bands(bands.data(),1664,1344,2,.88f,.13f,.001f,2,b,
            gain.data(),threshold.data(),416,336,false,olpf);
        float peak=0;
        for(size_t i=0;i<full.size();++i) peak=std::max(peak,std::fabs(full[i]-bands[i]));
        std::printf("RGB band RL profile %d max=%g\n",olpf,peak);
        require(peak<1e-6f,"RGB band processing matches the full image");
    }
}

static void stage45_1x_test(bool xt) {
    CpuFixture flat(96, 128, xt, 4);
    for (auto& fr : flat.frames) std::fill(fr.begin(), fr.end(), 0.25f);
    std::fill(flat.anchor.begin(), flat.anchor.end(), 0.25f);
    std::fill(flat.cov.begin(), flat.cov.end(), 2.0f);
    for (size_t i = 1; i < flat.cov.size(); i += 3) flat.cov[i] = 0.0f;
    std::vector<float> out_1x(size_t(96) * 128 * 3, -1.0f);
    std::vector<float> eta_q(size_t(24) * 32, -1.0f);
    flat.job.rgb_2x_out = out_1x.data();
    flat.job.eta_quarter_out = eta_q.data();
    flat.job.wb_r = 1.0f; flat.job.wb_b = 1.0f;
    RlWorkspace ws;
    require(heraw_cpu::run_stage45_1x_cpu(flat.job, &ws), "1x CPU flat run");
    for (float v : out_1x) require(std::isfinite(v) && std::fabs(v - 0.25f) < 2e-3f, "1x flat gray conservation");

    CpuFixture pat(96, 128, xt, 3);
    pat.pattern();
    pat.job.rgb_2x_out = out_1x.data();
    pat.job.eta_quarter_out = eta_q.data();
    pat.job.psf_iters = 3;
    require(heraw_cpu::run_stage45_1x_cpu(pat.job, &ws), "1x CPU textured run");
    for (float v : out_1x) require(std::isfinite(v) && v >= 0.0f, "1x textured finite non-negative");
}

static void wavelet_parity_test() {
    auto apply_wavelet_s4 = [](CpuFixture& f) {
        f.job.q_luma_var = 2e-6f;
        f.job.q_chroma_var = 6e-6f;
        f.job.q_nlt_s0 = 0.2f;
        f.job.q_nlt_s1 = 2.1f;
        f.job.q_nlt_s2 = 3.4f;
        f.job.q_corr_gain = 0.18f;
    };
    auto apply_wavelet_full = [&](CpuFixture& f) {
        apply_wavelet_s4(f);
        const float dmul[5] = {0.95f, 1.15f, 1.35f, 1.45f, 1.50f};
        for (int s = 0; s < 5; ++s) f.job.dct_scale_mul[s] = dmul[s];
        f.job.denoise_strength = 0.9f;
        f.job.chroma_denoise = 0.9f;
        f.job.psf_iters = 2;
    };

    // Part A: Stage 4 exact parity (< 2e-6 max error, < 2e-7 eta) with wavelet NLT variance active
    CpuFixture f4(64, 400, false, 3);
    f4.pattern();
    apply_wavelet_s4(f4);
    for (int tile : {0, 80}) {
        f4.job.tile_size_1x = tile;
#ifdef CPU_TEST_METAL
        require(heraw_gpu::run_stage45_gpu_checked(f4.job), "wavelet Stage 4 CPU/Metal parity");
#else
        f4.run();
#endif
    }

    // Part B: Full Stage 4+5 wavelet GAT + DCT scale + A-Trous + RL causal delta, tile parity, & Metal parity
    CpuFixture f5(64, 400, false, 3);
    f5.pattern();
    f5.job.denoise_strength = 0.9f;
    f5.job.chroma_denoise = 0.9f;
    f5.job.psf_iters = 2;
    f5.job.tile_size_1x = 0;
    f5.run();
    const std::vector<float> rgb_no_wv = f5.rgb;

    apply_wavelet_full(f5);
    f5.job.tile_size_1x = 0;
    f5.run();
    const std::vector<float> cpu_untiled = f5.rgb;
    const double cpu_eta = f5.job.out_eta_eff;
    const double causal_2x = delta(rgb_no_wv, cpu_untiled);
    std::printf("Wavelet Stage 5 causal_2x_delta=%.9g\n", causal_2x);
    require(causal_2x > 1e-4, "wavelet parameters materially adjust Stage 5 denoising");

    f5.job.tile_size_1x = 80;
    f5.run();
    const double tile_err = delta(cpu_untiled, f5.rgb);
    std::printf("Wavelet Stage 5 CPU untiled vs tile=80 max=%.9g eta_delta=%.9g\n",
                tile_err, std::fabs(cpu_eta - f5.job.out_eta_eff));
    require(tile_err < 8e-5, "wavelet Stage 5 CPU untiled vs tiled parity");
    require(std::fabs(cpu_eta - f5.job.out_eta_eff) < 2e-7, "wavelet Stage 5 CPU eta tile parity");

#ifdef CPU_TEST_METAL
    for (int tile : {0, 80}) {
        auto gjob = f5.job;
        gjob.keep_frames_lin = true;
        gjob.tile_size_1x = tile;
        std::vector<float> gpu_rgb(cpu_untiled.size(), -1.0f);
        gjob.rgb_2x_out = gpu_rgb.data();
        require(heraw_gpu::run_stage45_gpu(gjob), "wavelet Stage 5 Metal dispatch");
        double max_err = 0.0, sq = 0.0;
        for (size_t i = 0; i < cpu_untiled.size(); ++i) {
            require(std::isfinite(gpu_rgb[i]), "wavelet Metal finite RGB");
            double d = std::fabs(double(cpu_untiled[i]) - double(gpu_rgb[i]));
            max_err = std::max(max_err, d);
            sq += d * d;
        }
        double rmse = std::sqrt(sq / double(cpu_untiled.size()));
        double ed = std::fabs(cpu_eta - gjob.out_eta_eff);
        std::printf("Wavelet Stage 5 CPU/Metal tile=%d max=%.9g rmse=%.9g eta_delta=%.9g\n",
                    tile, max_err, rmse, ed);
        require(ed < 2e-7, "wavelet Stage 5 eta CPU/Metal parity");
        require(rmse < 1e-5, "wavelet Stage 5 RMSE CPU/Metal >100 dB");
        require(max_err < 3e-4, "wavelet Stage 5 max error within binary16 RL bound precision");
    }
#endif

    // Part C: 1x True-RGB CPU wavelet causal delta & finite non-negative check
    CpuFixture f1(96, 128, false, 3);
    f1.pattern();
    f1.job.denoise_strength = 0.9f;
    f1.job.chroma_denoise = 0.9f;
    f1.job.psf_iters = 2;
    std::vector<float> out_1x_no_wv(size_t(96) * 128 * 3, -1.0f);
    f1.job.rgb_2x_out = out_1x_no_wv.data();
    RlWorkspace ws;
    require(heraw_cpu::run_stage45_1x_cpu(f1.job, &ws), "wavelet 1x baseline CPU run");

    apply_wavelet_full(f1);
    std::vector<float> out_1x_wv(size_t(96) * 128 * 3, -1.0f);
    f1.job.rgb_2x_out = out_1x_wv.data();
    require(heraw_cpu::run_stage45_1x_cpu(f1.job, &ws), "wavelet 1x CPU run");
    for (float v : out_1x_wv) require(std::isfinite(v) && v >= 0.0f, "wavelet 1x finite non-negative");
    double causal_1x = delta(out_1x_no_wv, out_1x_wv);
    std::printf("Wavelet 1x causal_delta=%.9g\n", causal_1x);
    require(causal_1x > 1e-4, "wavelet 1x parameters materially adjust Stage 5 denoising");
}

int main(int argc,char** argv) {
    rgb_band_test();
    try {
#ifdef CPU_TEST_METAL
        if(argc>1 && std::string(argv[1])=="--cli") return nijin_cli_main(argc-1,argv+1);
        for(bool xt:{false,true}) metal_test(xt);
#endif
        wavelet_parity_test();
        for(bool xt:{false,true}) {
            stage45_1x_test(xt);
            flat_test(xt,false); flat_test(xt,true); modes_test(xt);
            seam_test(xt,false,false); seam_test(xt,true,false); seam_test(xt,true,true);
            CpuFixture constant(64,1762,xt);
            constant.job.pg_sig0_sq=0; constant.job.pg_gain=0;
            constant.job.denoise_strength=0.7f; constant.job.chroma_denoise=0.6f;
            constant.job.psf_iters=2;
            compare_tiles(constant,false,"constant-zero-noise-denoise");
        }
        if(argc>1) raw_test(argv[1]);
        std::puts("CPU Stage45 PASS");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL: %s\n",e.what());
        return 1;
    }
}
