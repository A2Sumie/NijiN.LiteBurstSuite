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

int main(int argc,char** argv) {
    try {
#ifdef CPU_TEST_METAL
        if(argc>1 && std::string(argv[1])=="--cli") return nijin_cli_main(argc-1,argv+1);
        for(bool xt:{false,true}) metal_test(xt);
#endif
        for(bool xt:{false,true}) {
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
