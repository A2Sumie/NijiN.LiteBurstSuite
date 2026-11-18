// Capture actual CLI Stage45 inputs and replay its alignment inputs on both ISAs.
#include "../heraw_gpu_backend.hpp"
#ifndef __APPLE__
#include "../heraw_gpu_vulkan.hpp"
#endif
namespace heraw_gpu { bool alignment_capture(Stage45GpuJob& job); }
#define run_stage45_gpu alignment_capture
#define main nijin_cli_main
#include "../heraw_sr2x.cpp"
#undef main
#undef run_stage45_gpu
#include <fstream>
#include <stdexcept>

static std::string capture_directory;

static void require(bool ok,const std::string& message) {
    if(!ok) throw std::runtime_error(message);
}

static void dot_test() {
    float a[24],b[24];
    for(int i=0;i<24;++i) { a[i]=i%4==0 ? 1e8f : i%4==2 ? -1e8f : 1; b[i]=1; }
    require(dot_f32_row(a,b,24)==12.0,"Centered ZNCC dot retains low-amplitude terms");
}

static void celestial_test() {
    std::vector<StarPtHalf> reference,current;
    for(int y=0;y<5;++y) for(int x=0;x<5;++x) {
        StarPtHalf star{20.0f+30*y+float(x%2),20.0f+30*x+float(y%3),1,50};
        reference.push_back(star);
        star.x+=0.5f; star.y-=0.25f;
        if((y*5+x)==0 || (y*5+x)==8 || (y*5+x)==23) {
            star.x+=0.875f; star.y-=0.625f;
        }
        current.push_back(star);
    }
    for(int order=0;order<2;++order) {
        std::vector<float> fy,fx;
        require(fit_celestial_poly_flow(reference,current,192,192,fy,fx,2),
                "Celestial robust fit");
        double error=0;
        for(size_t i=0;i<fx.size();++i)
            error=std::max(error,std::max(std::fabs(double(fx[i])-0.5),std::fabs(double(fy[i])+0.25)));
        std::printf("celestial order=%d error=%.9g\n",order,error);
        require(error<2e-4,"Celestial IRLS rejects the corresponding outlier rows");
        std::reverse(reference.begin(),reference.end());
        std::reverse(current.begin(),current.end());
    }
}

template<class T>
static void compare_plane(const std::filesystem::path& a,const std::filesystem::path& b,double limit) {
    std::ifstream left(a,std::ios::binary|std::ios::ate),right(b,std::ios::binary|std::ios::ate);
    require(bool(left) && bool(right),"Missing comparison plane "+a.filename().string());
    const auto bytes=left.tellg();
    require(bytes==right.tellg() && bytes>0 && bytes%sizeof(T)==0,"Plane extent");
    std::vector<T> x(size_t(bytes)/sizeof(T)),y(x.size());
    left.seekg(0); right.seekg(0);
    left.read(reinterpret_cast<char*>(x.data()),bytes);
    right.read(reinterpret_cast<char*>(y.data()),bytes);
    double worst=0;
    for(size_t i=0;i<x.size();++i) {
        require(std::isfinite(x[i]) && std::isfinite(y[i]),"Finite comparison plane");
        worst=std::max(worst,std::fabs(double(x[i])-double(y[i])));
    }
    std::printf("%s max=%.9g limit=%.9g\n",a.filename().string().c_str(),worst,limit);
    require(worst<=limit,"Cross-platform "+a.filename().string());
}

static void compare_capture(const std::string& left,const std::string& right) {
    size_t count=0;
    for(const auto& entry:std::filesystem::directory_iterator(heraw_fs::from_utf8(left))) {
        const auto name=entry.path().filename().string();
        auto other=heraw_fs::from_utf8(right)/entry.path().filename();
        if(entry.path().extension()==".f64")
            compare_plane<double>(entry.path(),other,name=="noise.f64" ? 1e-10 : 5e-6);
        else if(entry.path().extension()==".f32")
            compare_plane<float>(entry.path(),other,name=="cov.f32" ? 5e-5 :
                name.find("-raw")!=std::string::npos || name.find("-green")!=std::string::npos ||
                name.find("-pre")!=std::string::npos ? 2e-7 : 5e-6);
        else continue;
        ++count;
    }
    require(count>=10,"Complete alignment capture");
}

template<class T>
static void save_plane(const std::string& name, const T* data, size_t count) {
    std::ofstream out(heraw_fs::from_utf8(capture_directory+"/"+name),std::ios::binary);
    out.write(reinterpret_cast<const char*>(data),std::streamsize(count*sizeof(T)));
    if(!out) throw std::runtime_error("Cannot save alignment plane "+name);
}

namespace heraw_gpu {
bool alignment_capture(Stage45GpuJob& job) {
    const int H=job.H1/2, W=job.W1/2;
    std::vector<std::vector<float>> green(job.N,std::vector<float>(size_t(H)*W)), pre=green;
    for(int n=0;n<job.N;++n) {
        const auto& frame=(*job.frames_lin)[n];
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            float sum=0; int count=0;
            for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
                int yy=2*y+dy, xx=2*x+dx;
                if(job.cfa6x6[yy%6][xx%6]==1) { sum+=frame[size_t(yy)*job.W1+xx]; ++count; }
            }
            green[n][size_t(y)*W+x]=count ? sum/count :
                sample_cfa_channel(frame.data(),job.H1,job.W1,2*y,2*x,1,job.cfa6x6);
        }
        sep_gaussian_2d(green[n].data(),pre[n].data(),H,W,0.5f,job.n_threads);
        const std::string stem=std::to_string(n);
        save_plane(stem+"-raw.f32",frame.data(),frame.size());
        save_plane(stem+"-green.f32",green[n].data(),green[n].size());
        save_plane(stem+"-pre.f32",pre[n].data(),pre[n].size());
        save_plane(stem+"-final-fy.f32",(*job.flows_fy)[n].data(),size_t(H)*W);
        save_plane(stem+"-final-fx.f32",(*job.flows_fx)[n].data(),size_t(H)*W);
        save_plane(stem+"-mw.f32",(*job.mw_half)[n].data(),size_t(H)*W);
        if(n==0) continue;
        std::vector<float> fy,fx;
        double quality=0;
        align_frame_pyramid(pre[0].data(),pre[n].data(),H,W,fy,fx,quality,job.n_threads,true);
        save_plane(stem+"-align-fy.f32",fy.data(),fy.size());
        save_plane(stem+"-align-fx.f32",fx.data(),fx.size());
        std::printf("alignment frame=%d quality=%.17g\n",n,quality);
        CurIntegral integral;
        integral.build(pre[n].data(),H,W);
        std::vector<double> matches;
        for(int y=16;y+24<H-16;y+=24) for(int x=16;x+24<W-16;x+=24) {
            BlockMatch match{};
            bool found=match_zncc_block(pre[0].data(),pre[n].data(),&integral,H,W,
                                      y,x,24,24,0,0,8,1,match);
            matches.insert(matches.end(),{double(found),match.x0,match.y0,match.x1,match.y1,match.ncc});
        }
        save_plane(stem+"-matches.f64",matches.data(),matches.size());
    }
    save_plane("cov.f32",job.inv_cov,size_t(H)*W*3);
    const double noise[2]={job.pg_sig0_sq,job.pg_gain};
    save_plane("noise.f64",noise,2);
    return heraw_cpu::run_stage45_cpu(job);
}
}

int main(int argc,char** argv) {
    try {
        dot_test();
        celestial_test();
        if(argc==2 && std::string(argv[1])=="--selftest") return 0;
        if(argc==4 && std::string(argv[1])=="--compare") {
            compare_capture(argv[2],argv[3]); return 0;
        }
        if(argc<3) throw std::runtime_error("Usage: test_suite_alignment OUTPUT_DIRECTORY [CLI arguments]");
        capture_directory=argv[1];
        std::filesystem::create_directories(heraw_fs::from_utf8(capture_directory));
        return nijin_cli_main(argc-1,argv+1);
    } catch(const std::exception& e) {
        std::fprintf(stderr,"FAIL: %s\n",e.what());
        return 1;
    }
}
