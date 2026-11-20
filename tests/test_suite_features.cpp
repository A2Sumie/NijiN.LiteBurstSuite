#include "../heraw_suite_features.hpp"
#include <cassert>
#include <cstdio>
#include <exception>
#include <iostream>
#include <random>
#include <thread>

using namespace heraw_suite;

namespace {
struct Fixture {
    int h,w;
    Planes raw,green,fy,fx,mw;
    uint8_t cfa[6][6]{};
    std::vector<double> times;
    Fixture(size_t n,int hh=32,int ww=48):h(hh),w(ww),
        raw(n,std::vector<float>(static_cast<size_t>(4*hh*ww),0.2f)),
        green(n,std::vector<float>(static_cast<size_t>(hh*ww),0.2f)),
        fy(n,std::vector<float>(static_cast<size_t>(hh*ww),0)),
        fx(n,std::vector<float>(static_cast<size_t>(hh*ww),0)),
        mw(n,std::vector<float>(static_cast<size_t>(hh*ww),1)),times(n) {
        for(int y=0;y<6;++y) for(int x=0;x<6;++x) cfa[y][x]=y%2 ? (x%2 ? 2 : 1) : (x%2 ? 1 : 0);
        for(size_t k=0;k<n;++k) times[k]=k*0.02;
    }
    BurstView view() const { return {raw,green,fy,fx,mw,2*h,2*w,cfa,0,1e-8f,&times}; }
    static float texture(float y,float x) {
        return 0.3f+0.07f*std::sin(x*0.41f)+0.06f*std::cos(y*0.37f)+0.04f*std::sin(x*0.23f+y*0.29f);
    }
    void translated() {
        for(size_t k=0;k<raw.size();++k) {
            const float dx=static_cast<float>(k),dy=static_cast<float>(k%2);
            std::fill(fx[k].begin(),fx[k].end(),dx);
            std::fill(fy[k].begin(),fy[k].end(),dy);
            for(int y=0;y<h;++y) for(int x=0;x<w;++x)
                green[k][static_cast<size_t>(y)*w+x]=texture(y-dy,x-dx);
            for(int y=0;y<2*h;++y) for(int x=0;x<2*w;++x)
                raw[k][static_cast<size_t>(y)*2*w+x]=texture((y-0.5f)/2-dy,(x-0.5f)/2-dx);
        }
    }
    void square(size_t k,int y0,int x0,int size,float value) {
        for(int y=y0;y<y0+size;++y) for(int x=x0;x<x0+size;++x) {
            green[k][static_cast<size_t>(y)*w+x]=value;
            for(int yy=2*y;yy<2*y+2;++yy) for(int xx=2*x;xx<2*x+2;++xx)
                raw[k][static_cast<size_t>(yy)*2*w+xx]=value;
        }
    }
};
void close(float a,float b,float eps=1e-4f) {
    if(std::fabs(a-b)>eps) { std::cerr<<"Expected "<<b<<", got "<<a<<'\n'; std::abort(); }
}
template<class F> void throws(F f) {
    bool caught=false;
    try { f(); } catch(const std::invalid_argument&) { caught=true; }
    assert(caught);
}
void parallel_rows(int rows,const RowTask& task) {
    std::vector<std::thread> threads;
    std::array<std::exception_ptr,4> errors{};
    for(int k=0;k<4;++k) threads.emplace_back([&,k] {
        try { task(rows*k/4,rows*(k+1)/4); }
        catch(...) { errors[static_cast<size_t>(k)]=std::current_exception(); }
    });
    for(auto& thread:threads) thread.join();
    for(const auto& error:errors) if(error) std::rethrow_exception(error);
}
void test_camera_translation() {
    Fixture f(7,48,72); f.translated();
    auto view=f.view();
    const auto camera=fit_camera(view);
    for(size_t k=0;k<camera.size();++k) {
        assert(camera[k].valid);
        const auto p=camera[k].source(20,30);
        close(p[0],20+static_cast<float>(k%2)); close(p[1],30+static_cast<float>(k));
    }
    const auto g=analyze_geometry(view);
    assert(!g.gate.observable); assert(g.disparity_span_px<0.001f);
    TemporalOptions options;
    auto temporal=prepare_temporal(view,options);
    close(temporal.guide[20*72+30],Fixture::texture(20,30));
    Planes fy,fx;
    materialize_camera_flows(temporal,fy,fx);
    close(fx[6][20*72+30],6);
    const auto fields=compute_fields(view,{"depth","sigma","velocity","turbulence"});
    assert(!fields.gates.at("depth").observable);
    assert(fields.gates.at("sigma").observable);
    close(fields.maps.at("sigma")[20*72+30],std::sqrt(1e-8f/7),1e-7f);
    close(fields.maps.at("velocity")[20*72+30],0,0.002f);
    assert(!fields.gates.at("turbulence").observable);
    assert(compute_fields(view,{}).maps.empty());
    std::cout<<"PASS translated textured camera: zero residual depth and motion\n";
}
void test_temporal() {
    Fixture f(9,32,96);
    for(size_t k=0;k<9;++k) f.square(k,12,4+static_cast<int>(k)*10,6,0.8f);
    const size_t q=14*96+6;
    // Simulate legacy frame-zero deghost masks rejecting newly revealed background.
    for(size_t k=1;k<9;++k) f.mw[k][q]=0;
    auto clean=prepare_temporal(f.view());
    assert(clean.gate.observable);
    close(clean.guide[q],0.2f); close(clean.weights[0][q],0);
    for(size_t k=1;k<9;++k) close(clean.weights[k][q],1);
    auto image=render_temporal(f.view(),clean);
    const size_t p=29*192+13;
    close(image.image.rgb[3*p+1],0.2f);
    TemporalOptions opt; opt.mode=TemporalMode::VirtualND;
    const auto nd=prepare_temporal(f.view(),opt);
    for(size_t k=0;k<9;++k) close(nd.weights[k][q],1);
    close(nd.guide[q],0.2f+0.6f/9);
    const auto ni=render_temporal(f.view(),nd);
    close(ni.image.rgb[3*p+1],0.2f+0.6f/9,0.002f);
    opt.mode=TemporalMode::Strobe; opt.strobe_stride=2;
    auto strobe=render_temporal(f.view(),prepare_temporal(f.view(),opt));
    close(strobe.image.rgb[3*p+1],0.8f,0.002f);
    opt.mode=TemporalMode::Transient;
    auto transient=render_temporal(f.view(),prepare_temporal(f.view(),opt));
    close(transient.image.rgb[3*p+1],0.2f);
    close(transient.layer.rgb[3*p+1],0.6f,0.002f);
    close(transient.gray_layer[p],0.6f,0.002f);
    opt.mode=TemporalMode::Glint;
    auto glint=render_temporal(f.view(),prepare_temporal(f.view(),opt));
    close(glint.image.rgb[3*p+1],0.2f);
    opt.mode=TemporalMode::Reflection;
    assert(!prepare_temporal(f.view(),opt).gate.observable);
    opt.reflection_disparity_px=19.999f;
    assert(!prepare_temporal(f.view(),opt).gate.observable);
    opt.reflection_disparity_px=20;
    assert(prepare_temporal(f.view(),opt).gate.observable);
    assert(prepare_temporal(f.view(),opt).gate.threshold==20);
    f.raw[0][0]=1;
    assert(prepare_temporal(f.view(),opt).gate.observable);
    f.raw[0][0]=std::numeric_limits<float>::quiet_NaN();
    assert(!prepare_temporal(f.view(),opt).gate.observable);
    f.raw[0][0]=0.2f;
    for(size_t i=0;i<f.green[0].size()/2;++i) f.green[0][i]=1.0f;
    assert(!prepare_temporal(f.view(),opt).gate.observable);
    // A red-only transient is invisible to green-only max-hold selection.
    Fixture red(7,16,16);
    for(int y=8;y<24;++y) for(int x=8;x<24;++x)
        if(red.cfa[y%6][x%6]==0) red.raw[3][static_cast<size_t>(y)*32+x]=0.9f;
    opt.mode=TemporalMode::Transient;
    auto red_layer=render_temporal(red.view(),prepare_temporal(red.view(),opt));
    close(red_layer.image.rgb[3*(16*32+16)],0.2f);
    close(red_layer.layer.rgb[3*(16*32+16)],0.7f,0.002f);
    close(red_layer.layer.rgb[3*(16*32+16)+1],0);
    std::cout<<"PASS temporal: frame0 removal, equal ND, whole-color max layer, strobe, lower envelope\n";
}
void test_strobe_and_parallel() {
    Fixture f(7,24,48);
    // Storage index zero is the latest exposure; the oldest action is index one.
    f.times={6,0,1,2,3,4,5};
    f.square(0,8,4,6,0.8f); f.square(1,8,4,6,0.5f);
    f.square(1,8,24,6,0.8f);
    TemporalOptions opt; opt.mode=TemporalMode::Strobe; opt.strobe_stride=1;
    const size_t overlap=21*96+13,old_only=21*96+53;
    auto prepared=prepare_temporal(f.view(),opt);
    const auto flat=render_temporal(f.view(),prepared);
    close(flat.image.rgb[3*overlap+1],0.8f);
    close(flat.image.rgb[3*old_only+1],0.8f);
    opt.strobe_fade=1;
    prepared=prepare_temporal(f.view(),opt);
    const auto faded=render_temporal(f.view(),prepared);
    close(faded.image.rgb[3*overlap+1],0.8f);
    close(faded.image.rgb[3*old_only+1],0.2f+0.6f*std::exp(-4.0f),0.002f);
    for(auto mode:{TemporalMode::CleanPlate,TemporalMode::VirtualND,TemporalMode::Transient,
                   TemporalMode::Strobe,TemporalMode::Glint,TemporalMode::Reflection}) {
        opt.mode=mode;
        prepared=prepare_temporal(f.view(),opt);
        const auto serial=render_temporal(f.view(),prepared,48,96);
        const auto threaded=render_temporal(f.view(),prepared,48,96,parallel_rows);
        assert(serial.image.rgb==threaded.image.rgb);
        assert(serial.layer.rgb==threaded.layer.rgb);
        assert(serial.gray_layer==threaded.gray_layer);
    }
    // Stride selects chronological ranks, so the earliest stored index is not
    // implicitly selected. Here ranks 0 and 4 have storage indices 1 and 5.
    opt.mode=TemporalMode::Strobe; opt.strobe_stride=4; opt.strobe_fade=0;
    prepared=prepare_temporal(f.view(),opt);
    close(render_temporal(f.view(),prepared).image.rgb[3*overlap+1],0.5f);
    opt.strobe_fade=1.01f;
    throws([&] { prepare_temporal(f.view(),opt); });
    opt.strobe_fade=-0.01f;
    throws([&] { prepare_temporal(f.view(),opt); });
    throws([&] {
        render_temporal(f.view(),prepared,48,96,[](int rows,const RowTask& task){ task(-1,rows); });
    });
    std::cout<<"PASS chronological strobe, fade endpoints, stride and bit-exact parallel temporal rows\n";
}
void test_capture_order() {
    Fixture f(7,24,48);
    // Original indices have gaps after frame pruning; crown index 16 moved first.
    const std::vector<size_t> capture{16,1,3,5,8,10,12};
    for(size_t k=0;k<capture.size();++k) f.times[k]=static_cast<double>(capture[k]);
    f.square(0,8,4,6,0.8f); f.square(1,8,4,6,0.5f);
    f.square(1,8,24,6,0.8f); f.square(3,8,34,6,0.7f);
    std::vector<double> equal(7,123),tied(7);
    for(size_t k=0;k<7;++k) tied[k]=static_cast<double>(capture[k]/4);
    auto correct=f.view(); correct.capture_order=&capture;
    auto coarse=correct; coarse.times_seconds=&equal;
    auto missing=correct; missing.times_seconds=nullptr;
    TemporalOptions opt; opt.mode=TemporalMode::Strobe;
    for(size_t stride:{size_t{1},size_t{2},size_t{4}}) for(float fade:{0.0f,1.0f}) {
        opt.strobe_stride=stride; opt.strobe_fade=fade;
        const auto expected=render_temporal(correct,prepare_temporal(correct,opt));
        assert(render_temporal(coarse,prepare_temporal(coarse,opt)).image.rgb==expected.image.rgb);
        assert(render_temporal(missing,prepare_temporal(missing,opt)).image.rgb==expected.image.rgb);
        if(fade==0) {
            auto partial=correct; partial.times_seconds=&tied;
            assert(render_temporal(partial,prepare_temporal(partial,opt)).image.rgb==expected.image.rgb);
        }
    }
    opt.strobe_stride=1; opt.strobe_fade=1;
    const auto faded=render_temporal(missing,prepare_temporal(missing,opt));
    // Index 5 is 11 of 15 input-frame intervals older than the latest exposure.
    close(faded.image.rgb[3*(21*96+73)+1],0.2f+0.5f*std::exp(-4.0f*11/15),0.002f);
    opt.strobe_fade=0;
    const auto equal_strength=render_temporal(missing,prepare_temporal(missing,opt));
    close(equal_strength.image.rgb[3*(21*96+73)+1],0.7f,0.002f);
    std::vector<double> reverse(7);
    for(size_t k=0;k<7;++k) reverse[k]=-static_cast<double>(capture[k]);
    auto seconds_first=correct; seconds_first.times_seconds=&reverse;
    close(render_temporal(seconds_first,prepare_temporal(seconds_first,opt)).image.rgb[3*(21*96+13)+1],0.5f);

    Fixture motion(7,24,48);
    for(size_t k=0;k<7;++k) {
        motion.times[k]=static_cast<double>(capture[k]);
        for(int y=8;y<14;++y) for(int x=4;x<10;++x) {
            const size_t i=static_cast<size_t>(y)*48+x;
            motion.fx[k][i]=0.125f*(static_cast<float>(capture[k])-capture[0]);
            motion.mw[k][i]=0;
        }
    }
    auto timed=motion.view(); timed.capture_order=&capture;
    const auto per_second=compute_fields(timed,{"velocity_x"});
    const size_t center=10*48+6;
    close(per_second.maps.at("velocity_x")[center],0.25f);
    assert(per_second.gates.at("velocity_x").units=="1x_px_per_second");
    for(const auto* timestamps:{&equal,static_cast<std::vector<double>*>(nullptr)}) {
        auto ordinal=timed; ordinal.times_seconds=timestamps;
        const auto per_index=compute_fields(ordinal,{"velocity_x"});
        close(per_index.maps.at("velocity_x")[center],0.25f);
        assert(per_index.gates.at("velocity_x").units=="1x_px_per_input_frame_index");
    }
    std::vector<size_t> bad{0,1};
    missing.capture_order=&bad;
    throws([&] { prepare_temporal(missing,opt); });
    std::cout<<"PASS anchor-first capture-order fallback: equal/missing/tied timestamps, stride/fade and velocity input-index units\n";
}
void test_geometry() {
    Fixture f(5,64,96);
    // Textured near rectangle translates against a textured stationary far plane.
    // Exact reference->source flow is known; boundaries are photometrically gated.
    for(size_t k=0;k<5;++k) {
        for(int y=0;y<f.h;++y) for(int x=0;x<f.w;++x) {
            const int dx=static_cast<int>(2*k);
            const bool src_near=y>=15&&y<46&&x>=29+dx&&x<53+dx;
            const bool ref_near=y>=15&&y<46&&x>=29&&x<53;
            f.green[k][static_cast<size_t>(y)*f.w+x]=Fixture::texture(y,src_near ? x-dx : x);
            f.fx[k][static_cast<size_t>(y)*f.w+x]=ref_near ? static_cast<float>(dx) : 0;
        }
    }
    auto fields=compute_fields(f.view(),{"depth","depth_confidence"});
    const auto& g=fields.geometry;
    std::cout<<"parallax span="<<g.disparity_span_px<<" coherence="<<g.rank1_coherence
             <<" support="<<g.support_fraction<<'\n';
    assert(g.gate.observable); assert(g.disparity_span_px>12); assert(g.rank1_coherence>0.95);
    const float fg=fields.maps.at("depth")[30*96+40],bg=fields.maps.at("depth")[30*96+10];
    assert(std::fabs(fg-bg)>0.85f);
    for(float span:{1.48f,1.52f}) {
        Fixture boundary(5,64,96);
        for(size_t k=0;k<5;++k) for(int y=0;y<64;++y) for(int x=0;x<96;++x) {
            const float dx=static_cast<float>(k)*span/8;
            const bool src_near=y>=15&&y<46&&x>=29+dx&&x<53+dx;
            const bool ref_near=y>=15&&y<46&&x>=29&&x<53;
            const size_t i=static_cast<size_t>(y)*96+x;
            boundary.green[k][i]=Fixture::texture(static_cast<float>(y),src_near ? x-dx : x);
            boundary.fx[k][i]=ref_near ? dx : 0;
        }
        const auto observed=analyze_geometry(boundary.view());
        assert(observed.gate.threshold==1.5 && observed.gate.units=="1x_px");
        close(observed.disparity_span_px,span,0.005f);
        assert(observed.gate.observable==(span>1.5f));
    }
    // Textureless observations carry no support, despite a large invented flow.
    for(auto& a:f.green) std::fill(a.begin(),a.end(),0.2f);
    assert(!analyze_geometry(f.view()).gate.observable);
    Fixture noise(8,40,56); noise.translated();
    std::mt19937 rng(7);
    std::normal_distribution<float> normal(0,5);
    for(size_t k=1;k<8;++k) for(size_t i=0;i<noise.fx[k].size();++i) {
        noise.fx[k][i]=normal(rng); noise.fy[k][i]=normal(rng);
    }
    assert(!analyze_geometry(noise.view()).gate.observable);
    Fixture zero(5); assert(!analyze_geometry(zero.view()).gate.observable);

    // Anchor-in-middle sweep (-2, -1, 0, +1, +2 half-px -> 8.0 1x px peak-to-peak)
    // plus realistic subpixel ZNCC jitter, low-key camera-linear exposure, w_frame=0.35,
    // and a large 24x24 untextured interior inside the foreground subject.
    Fixture realistic(5,96,128);
    const std::array<float,5> sweep{{0.0f,-2.0f,-1.0f,1.0f,2.0f}};
    std::normal_distribution<float> jitter(0.0f,0.06f);
    for(size_t k=0;k<5;++k) {
        for(int y=0;y<96;++y) for(int x=0;x<128;++x) {
            const float dx=sweep[k];
            const bool ref_fg=y>=20&&y<76&&x>=36&&x<92;
            const bool ref_hole=y>=36&&y<60&&x>=52&&x<76;
            const bool src_fg=y>=20&&y<76&&(x-dx)>=36&&(x-dx)<92;
            const bool src_hole=y>=36&&y<60&&(x-dx)>=52&&(x-dx)<76;
            const float ux=src_fg ? x-dx : static_cast<float>(x);
            const float val=src_hole ? 0.035f : (src_fg ?
                0.035f+0.008f*std::sin(ux*0.45f)+0.007f*std::cos(y*0.41f) :
                0.012f+0.003f*std::sin(ux*0.37f)+0.003f*std::cos(y*0.33f));
            const size_t i=static_cast<size_t>(y)*128+x;
            realistic.green[k][i]=val;
            realistic.fx[k][i]=(ref_fg ? dx : 0.0f)+(k>0&&!ref_hole ? jitter(rng) : 0.0f);
            realistic.fy[k][i]=k>0&&!ref_hole ? jitter(rng) : 0.0f;
            realistic.mw[k][i]=k==0 ? 1.0f : 0.35f;
        }
    }
    auto real_view=realistic.view();
    real_view.noise_read_var=1e-6f; real_view.noise_shot=2e-5f;
    const auto serial_real=compute_fields(real_view,{"depth","depth_confidence","sigma","coherence"});
    const auto par_real=compute_fields(real_view,{"depth","depth_confidence","sigma","coherence"},parallel_rows);
    assert(serial_real.geometry.gate.observable);
    assert(serial_real.geometry.disparity_span_px>7.8f && serial_real.geometry.disparity_span_px<8.6f);
    const float hole_depth=serial_real.maps.at("depth")[48*128+64];
    const float bg_depth=serial_real.maps.at("depth")[8*128+8];
    assert(std::fabs(hole_depth-bg_depth)>0.70f);
    assert(serial_real.maps==par_real.maps);

    // 1D vertical-stripe edge texture with normal-only optical flow along a 45-deg baseline
    // reproduces the aperture effect on real photos (tangential parallax unobserved in flow).
    Fixture edge1d(5,96,128);
    const std::array<float,5> b45{{0.0f,-2.0f,-1.0f,1.0f,2.0f}};
    for(size_t k=0;k<5;++k) {
        const float d=b45[k];
        for(int y=0;y<96;++y) for(int x=0;x<128;++x) {
            const bool fg=y>=18&&y<78&&x>=34&&x<94;
            const bool src_fg=y>=18&&y<78&&(x-d)>=34&&(x-d)<94;
            const float ux=src_fg ? static_cast<float>(x)-d : static_cast<float>(x);
            const size_t i=static_cast<size_t>(y)*128+x;
            edge1d.green[k][i]=0.25f+0.08f*std::sin(ux*0.42f);
            // Normal-only LK observes horizontal dx but zero tangential dy along vertical stripes!
            edge1d.fx[k][i]=fg ? d : 0.0f;
            edge1d.fy[k][i]=0.0f;
        }
    }
    const auto g1d=analyze_geometry(edge1d.view());
    assert(g1d.gate.observable && g1d.rank1_coherence>0.85f);
    assert(g1d.disparity_span_px>7.5f && g1d.disparity_span_px<8.5f);

    std::cout<<"PASS parallax: true two-plane separation; middle-anchor gauge; dense hole fill; noisy/zero/flat flow gates\n";
}
void test_fields() {
    Fixture f(8,24,32); f.translated();
    auto b=f.view(); b.exposure_seconds=0.001f;
    const auto a=compute_fields(b,{"defocus","specular","coherence","flicker"});
    assert(!a.gates.at("defocus").observable);
    assert(a.gates.at("flicker").observable);
    close(a.maps.at("flicker")[10*32+12],0,0.001f);
    for(size_t k=0;k<8;++k)
        for(float& v:f.green[k]) v*=k%2 ? 1.1f : 0.9f;
    const auto mod=compute_fields(b,{"flicker"});
    assert(mod.maps.at("flicker")[10*32+12]>0.09f);
    throws([&] { compute_fields(b,{"unknown"}); });
    f.fx[0].pop_back();
    throws([&] { compute_fields(b,{"sigma"}); });
    std::cout<<"PASS lazy fields: shot/read sigma, metadata gates and modulation causal delta\n";
}
void test_local_geometry() {
    Fixture slope(7,64,96);
    const float ax=0.12f,ay=0.08f;
    for(size_t k=0;k<7;++k) for(int y=0;y<64;++y) for(int x=0;x<96;++x) {
        const float t=static_cast<float>(k)/6;
        const float intercept=6-ax*29+ay*(y-15);
        const float source_reference=(x-t*intercept)/(1+t*ax);
        const bool source_near=y>=15&&y<46&&source_reference>=29&&source_reference<53;
        const bool reference_near=y>=15&&y<46&&x>=29&&x<53;
        const size_t i=static_cast<size_t>(y)*96+x;
        slope.green[k][i]=Fixture::texture(static_cast<float>(y),source_near ? source_reference : x);
        slope.fx[k][i]=reference_near ? t*(6+ax*(x-29)+ay*(y-15)) : 0;
    }
    const auto normal=compute_fields(slope.view(),{"normal_x","normal_y"});
    assert(normal.maps.size()==2);
    assert(normal.geometry.gate.observable);
    assert(normal.gates.at("normal_x").observable && normal.gates.at("normal_y").observable);
    assert(normal.gates.at("normal_x").units=="relative_inverse_depth_per_1x_pixel");
    const float scale=normal.geometry.rho_high-normal.geometry.rho_low;
    const size_t center=25*96+35;
    close(normal.maps.at("normal_x")[center],-ax/(2*scale),0.0002f);
    close(normal.maps.at("normal_y")[center],-ay/(2*scale),0.0002f);
    // The upper surface lies beyond the display depth's 95th-percentile clamp;
    // its differential geometry still retains the real plane slope.
    close(normal.maps.at("normal_x")[40*96+48],-ax/(2*scale),0.0002f);
    close(normal.maps.at("normal_y")[40*96+48],-ay/(2*scale),0.0002f);
    const auto exported=compute_fields(slope.view(),{"depth","normal_x","normal_y"});
    assert(exported.maps.at("normal_x")==normal.maps.at("normal_x"));
    assert(exported.maps.at("normal_y")==normal.maps.at("normal_y"));
    assert(normal.gates.at("normal_x").confidence>0);

    Fixture layers(7,64,96);
    const std::array<int,7> shifts{{0,-4,-2,2,4,6,8}};
    const auto bg=[](int y,float x){ return 0.2f+0.012f*std::sin(x*0.31f)+0.012f*std::cos(y*0.27f); };
    const auto fg=[](int y,float x){ return 0.75f+0.012f*std::sin(x*0.31f)+0.012f*std::cos(y*0.27f); };
    for(size_t k=0;k<7;++k) for(int y=0;y<64;++y) for(int x=0;x<96;++x) {
        const int dx=shifts[k];
        const bool source_near=y>=15&&y<46&&x>=29+dx&&x<53+dx;
        const bool reference_near=y>=15&&y<46&&x>=29&&x<53;
        const size_t i=static_cast<size_t>(y)*96+x;
        layers.green[k][i]=source_near ? fg(y,static_cast<float>(x-dx)) : bg(y,static_cast<float>(x));
        layers.fx[k][i]=reference_near ? static_cast<float>(dx) : 0;
        if(k==0&&x==30&&y>=15&&y<46)
            layers.green[k][i]=0.5f*(bg(y,static_cast<float>(x))+fg(y,static_cast<float>(x)));
    }
    const auto visible=compute_fields(layers.view(),{"occlusion","depth","depth_confidence"});
    assert(visible.geometry.gate.observable);
    assert(visible.gates.at("occlusion").observable);
    assert(visible.gates.at("occlusion").confidence>0);
    assert(visible.gates.at("occlusion").units=="relative_foreground_mixing_alpha_0_1");
    close(visible.maps.at("occlusion")[30*96+30],0.5f,0.03f);
    close(visible.maps.at("occlusion")[30*96+31],1.0f,0.03f);
    close(visible.maps.at("occlusion")[30*96+28],0,0.03f);
    // A textured point on the dominant camera plane has valid zero-depth support.
    assert(visible.maps.at("depth_confidence")[30*96+10]>0.85f);
    const auto advanced_only=compute_fields(layers.view(),{"occlusion"});
    assert(advanced_only.maps.size()==1);
    assert(advanced_only.maps.at("occlusion")==visible.maps.at("occlusion"));
    // Invert foreground/background brightness: alpha is geometric occupancy,
    // not a bright-transient mask.
    for(auto& image:layers.green) for(float& v:image) v=1-v;
    const auto dark_foreground=compute_fields(layers.view(),{"occlusion"});
    assert(dark_foreground.gates.at("occlusion").observable);
    close(dark_foreground.maps.at("occlusion")[30*96+30],0.5f,0.03f);
    Fixture flat(7,64,96); flat.translated();
    const auto unavailable=compute_fields(flat.view(),{"normal_x","normal_y","occlusion"});
    for(const auto& map:unavailable.maps) {
        assert(!unavailable.gates.at(map.first).observable);
        for(float value:map.second) close(value,0);
    }
    std::cout<<"PASS local slanted-plane gradient, bright/dark foreground alpha, revealed background and zero-parallax gates\n";
}
void test_projective_and_cfa() {
    Fixture f(4,48,64);
    Homography truth;
    truth.cx=31.5; truth.cy=23.5; truth.scale=32;
    truth.h={{1.01,0.025,0.04,-0.02,0.99,-0.03,0.004,-0.008,1}};
    for(size_t k=1;k<4;++k) for(int y=0;y<f.h;++y) for(int x=0;x<f.w;++x) {
        const size_t i=static_cast<size_t>(y)*f.w+x;
        const auto s=truth.source(static_cast<float>(y),static_cast<float>(x));
        f.fy[k][i]=s[0]-y; f.fx[k][i]=s[1]-x;
        if(x>40&&x<50&&y>20&&y<30) f.fx[k][i]+=15;
    }
    const auto fit=fit_camera(f.view());
    for(size_t k=1;k<4;++k) {
        const auto actual=fit[k].source(20,30),expected=truth.source(20,30);
        close(actual[0],expected[0],0.005f); close(actual[1],expected[1],0.005f);
    }
    static const uint8_t xtrans[6][6]={{1,2,1,1,0,1},{0,1,0,2,1,2},{1,2,1,1,0,1},
        {1,0,1,1,2,1},{2,1,2,0,1,0},{1,0,1,1,2,1}};
    Fixture color(5,12,12);
    for(int y=0;y<6;++y) for(int x=0;x<6;++x) color.cfa[y][x]=xtrans[y][x];
    const std::array<float,3> rgb{{0.12f,0.24f,0.48f}};
    for(size_t k=0;k<5;++k) {
        std::fill(color.green[k].begin(),color.green[k].end(),rgb[1]);
        for(int y=0;y<24;++y) for(int x=0;x<24;++x)
            color.raw[k][static_cast<size_t>(y)*24+x]=rgb[color.cfa[y%6][x%6]];
    }
    const auto image=render_temporal(color.view(),prepare_temporal(color.view()));
    for(size_t i=0;i<image.image.rgb.size();++i) close(image.image.rgb[i],rgb[i%3],1e-6f);
    const auto parallel_image=render_temporal(color.view(),prepare_temporal(color.view()),24,24,parallel_rows);
    assert(parallel_image.image.rgb==image.image.rgb);
    std::cout<<"PASS IRLS projective+outlier and X-Trans camera-linear color invariance\n";
}
void test_bokeh() {
    Image a{19,23,std::vector<float>(19*23*3,0.37f)};
    std::vector<float> depth(19*23);
    for(size_t i=0;i<depth.size();++i) depth[i]=static_cast<float>(i%23)/22;
    RefocusOptions options; options.radius_px=9;
    auto result=render_refocus(a,depth,19,23,options);
    for(float v:result.rgb) close(v,0.37f,1e-6f);
    auto gathered=render_refocus(a,depth,19,23,options,parallel_rows);
    for(float v:gathered.rgb) close(v,0.37f,1e-6f);
    std::fill(a.rgb.begin(),a.rgb.end(),0);
    a.rgb[3*(9*23+11)]=1; a.rgb[3*(0*23+0)+1]=2;
    std::fill(depth.begin(),depth.end(),1);
    result=render_refocus(a,depth,19,23,options);
    for(size_t c=0;c<3;++c) {
        double input=0,output=0;
        for(size_t i=0;i<depth.size();++i) { input+=a.rgb[3*i+c]; output+=result.rgb[3*i+c]; }
        assert(std::fabs(input-output)<2e-5);
    }
    assert(result.rgb[3*(9*23+11)]<0.1f);
    assert(result.rgb[3*(9*23+11+4)]>0);
    assert(result.rgb[3*(9*23+11+6)]==0);
    // Nonconstant image and variable radius exercise reciprocal exchange across
    // tile/worker boundaries and near a zero-radius in-focus region.
    for(size_t i=0;i<depth.size();++i) {
        depth[i]=static_cast<float>(i%23)/22;
        for(size_t c=0;c<3;++c) a.rgb[3*i+c]=0.1f+0.03f*static_cast<float>((i+7*c)%19);
    }
    result=render_refocus(a,depth,19,23,options);
    gathered=render_refocus(a,depth,19,23,options,parallel_rows);
    for(size_t i=0;i<a.rgb.size();++i) close(result.rgb[i],gathered.rgb[i],1e-6f);
    for(size_t c=0;c<3;++c) {
        double before=0,after=0;
        for(size_t i=0;i<depth.size();++i) { before+=a.rgb[3*i+c]; after+=gathered.rgb[3*i+c]; }
        assert(std::fabs(before-after)<2e-5);
    }
    options.radius_px=0;
    assert(render_refocus(a,depth,19,23,options).rgb==a.rgb);
    FieldResult unavailable;
    throws([&] { render_refocus(a,unavailable); });
    depth[0]=std::numeric_limits<float>::quiet_NaN();
    throws([&] { render_refocus(a,depth,19,23); });
    std::cout<<"PASS disk bokeh: DC and global photon conservation, edge impulse, radius support\n";
}
void test_webgl(const std::string& path) {
    FieldResult f; f.H=8; f.W=8;
    f.maps["depth"]=std::vector<float>(64);
    for(size_t i=0;i<64;++i) f.maps["depth"][i]=static_cast<float>(i%8)/7;
    f.gates["depth"].observable=true;
    std::vector<uint8_t> rgb(64*3,128);
    write_webgl_html(path,rgb,8,8,f);
    std::ifstream in(heraw_fs::from_utf8(path),std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    assert(html.find("data:image/png;base64,iVBORw0KGgo")!=std::string::npos);
    assert(html.find("webgl2")!=std::string::npos);
    assert(html.find("gl.DEPTH_TEST")!=std::string::npos);
    assert(html.find("http://")==std::string::npos);
    assert(html.find("https://")==std::string::npos);
    write_webgl_html(path,rgb,8,8,f,24.0f,&rgb);
    const std::string unicode=path+".\xe6\xb5\x8b\xe8\xaf\x95.html";
    write_webgl_html(unicode,rgb,8,8,f);
    std::ifstream unicode_in(heraw_fs::from_utf8(unicode),std::ios::binary);
    assert(unicode_in.good());
    unicode_in.close();
    std::filesystem::remove(heraw_fs::from_utf8(unicode));
    f.gates["depth"].observable=false;
    throws([&] { write_webgl_html(path,rgb,8,8,f); });
    std::cout<<"PASS WebGL embedded PNG and observable-depth gate: "<<path<<'\n';
}
void test_optical_focus_sign_calibration() {
    auto run_sign_case = [](float sign, float focus_m, bool is_inf, float hyperfocal_m, bool sharp_bg) {
        Fixture f(5, 96, 128);
        const std::array<float, 5> sweep{{0.0f, -2.0f * sign, -1.0f * sign, 1.0f * sign, 2.0f * sign}};
        for (size_t k = 0; k < 5; ++k) {
            const float dx = sweep[k];
            for (int y = 0; y < 96; ++y) {
                for (int x = 0; x < 128; ++x) {
                    const bool ref_fg = (y >= 24 && y < 72 && x >= 36 && x < 92);
                    const bool src_fg = (y >= 24 && y < 72 && (x - dx) >= 36 && (x - dx) < 92);
                    const float ux = src_fg ? (static_cast<float>(x) - dx) : static_cast<float>(x);
                    const float sharp_v = 0.25f + 0.08f * std::sin(ux * 1.15f) + 0.07f * std::cos(y * 1.05f);
                    const float soft_v = 0.25f + 0.04f * std::sin(ux * 0.25f) + 0.04f * std::cos(y * 0.23f);
                    const bool is_sharp = (src_fg != sharp_bg);
                    const size_t i = static_cast<size_t>(y) * 128 + x;
                    f.green[k][i] = is_sharp ? sharp_v : soft_v;
                    f.fx[k][i] = ref_fg ? dx : 0.0f;
                    f.mw[k][i] = (k == 0) ? 1.0f : 0.90f;
                }
            }
        }
        BurstView bv = f.view();
        bv.noise_read_var = 1e-6f;
        bv.noise_shot = 1e-5f;
        bv.focus_distance_m = focus_m;
        bv.hyperfocal_m = hyperfocal_m;
        bv.focus_is_infinity = is_inf;
        bv.distant_by_optics = is_inf;
        return compute_fields(bv, {"depth"});
    };

    // Uncalibrated gauge (no optical focus metadata): preserves raw parallax sign
    const auto g_pos = run_sign_case(+1.0f, 0.0f, false, 0.0f, true);
    const auto g_neg = run_sign_case(-1.0f, 0.0f, false, 0.0f, true);
    assert(!g_pos.geometry.sign_calibrated && !g_neg.geometry.sign_calibrated);
    const float diff_pos = g_pos.maps.at("depth")[48 * 128 + 64] - g_pos.maps.at("depth")[6 * 128 + 6];
    const float diff_neg = g_neg.maps.at("depth")[48 * 128 + 64] - g_neg.maps.at("depth")[6 * 128 + 6];
    assert(diff_pos * diff_neg < -0.5f);

    // Infinity/hyperfocal calibration: near blurred foreground must orient > sharp infinity background
    const auto cal_pos = run_sign_case(+1.0f, 1000000.0f, true, 20.0f, true);
    const auto cal_neg = run_sign_case(-1.0f, 1000000.0f, true, 20.0f, true);
    assert(cal_pos.geometry.sign_calibrated && cal_neg.geometry.sign_calibrated);
    assert(cal_pos.maps.at("depth")[48 * 128 + 64] > cal_pos.maps.at("depth")[6 * 128 + 6] + 0.5f);
    assert(cal_neg.maps.at("depth")[48 * 128 + 64] > cal_neg.maps.at("depth")[6 * 128 + 6] + 0.5f);
    assert(cal_pos.geometry.has_focus_plane && cal_pos.geometry.focus_plane_depth < 0.35f);

    // Close-focus: sharp foreground + blurred border background puts focus_plane_depth near 1.0
    const auto near_pos = run_sign_case(+1.0f, 1.5f, false, 30.0f, false);
    const auto near_neg = run_sign_case(-1.0f, 1.5f, false, 30.0f, false);
    assert(near_pos.geometry.sign_calibrated && near_neg.geometry.sign_calibrated);
    assert(near_pos.geometry.has_focus_plane && near_pos.geometry.focus_plane_depth > 0.65f);
    assert(near_neg.geometry.has_focus_plane && near_neg.geometry.focus_plane_depth > 0.65f);
    std::cout << "PASS optical focus sign calibration and autofocus plane localization\n";
}
void test_real_ppm(const std::string& path) {
    // A camera-linear P6 from LibRaw supplies measured scene texture and color.
    // Controlled contamination then has an independently known clean target.
    std::ifstream in(path,std::ios::binary);
    std::string magic;
    int w=0,h=0,maximum=0;
    in>>magic>>w>>h>>maximum;
    assert(in&&magic=="P6"&&w>=192&&h>=64&&maximum==65535);
    in.get();
    std::vector<uint8_t> bytes(static_cast<size_t>(w)*h*6);
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    assert(in);
    Fixture f(6,32,96);
    const int y0=(h-64)/2,x0=(w-192)/2;
    for(int y=0;y<64;++y) for(int x=0;x<192;++x) {
        const size_t c=f.cfa[y%6][x%6],j=(static_cast<size_t>(y+y0)*w+x+x0)*6+2*c;
        const float v=(static_cast<unsigned>(bytes[j])*256+bytes[j+1])/65535.0f;
        for(auto& frame:f.raw) frame[static_cast<size_t>(y)*192+x]=v;
    }
    for(int y=0;y<32;++y) for(int x=0;x<96;++x) {
        const size_t i=static_cast<size_t>(y)*96+x;
        const float v=(f.raw[0][static_cast<size_t>(2*y)*192+2*x+1]+
                       f.raw[0][static_cast<size_t>(2*y+1)*192+2*x])*0.5f;
        for(auto& green:f.green) green[i]=v;
    }
    const auto original=render_temporal(f.view(),prepare_temporal(f.view()));
    for(size_t k=0;k<6;++k) f.square(k,12,4+static_cast<int>(k)*14,6,1);
    const auto clean=render_temporal(f.view(),prepare_temporal(f.view()));
    const size_t pixel=29*192+13;
    for(size_t c=0;c<3;++c) close(clean.image.rgb[3*pixel+c],original.image.rgb[3*pixel+c],1e-5f);
    std::vector<float> depth(32*96,1);
    RefocusOptions options; options.radius_px=6;
    const auto bokeh=render_refocus(original.image,depth,32,96,options);
    for(size_t c=0;c<3;++c) {
        double a=0,b=0;
        for(size_t i=0;i<original.image.rgb.size()/3;++i) {
            a+=original.image.rgb[3*i+c]; b+=bokeh.rgb[3*i+c];
        }
        assert(std::fabs(a-b)<std::max(1e-5,1e-6*a));
    }
    std::cout<<"PASS real camera-linear scene: controlled foreground removal and photon conservation: "<<path<<'\n';
}

void test_v3_epipolar_and_plane_detilt() {
    // 1. Axial dolly push-in (tz != 0, tx = ty = 0) with calibrated focal length.
    Fixture ax(5, 64, 96);
    const std::array<float, 5> dz{{0.0f, -0.04f, -0.02f, 0.02f, 0.04f}};
    const float cx = 47.5f, cy = 31.5f;
    for (size_t k = 0; k < 5; ++k) {
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
            const bool fg = (y >= 20 && y < 44 && x >= 52 && x < 76);
            const float r_scale = fg ? (1.0f + dz[k]) : 1.0f;
            const float ux = cx + (static_cast<float>(x) - cx) / r_scale;
            const float uy = cy + (static_cast<float>(y) - cy) / r_scale;
            const size_t i = static_cast<size_t>(y) * 96 + x;
            ax.green[k][i] = Fixture::texture(uy, ux);
            if (fg) {
                ax.fx[k][i] = -(static_cast<float>(x) - cx) * dz[k];
                ax.fy[k][i] = -(static_cast<float>(y) - cy) * dz[k];
            }
        }
    }
    BurstView bav = ax.view();
    bav.focal_length_mm = 24.0f;
    const auto ga = compute_fields(bav, {"depth"});
    assert(ga.geometry.gate.observable);
    float max_bz = 0.0f;
    for (float bz : ga.geometry.baseline_z) max_bz = std::max(max_bz, std::fabs(bz));
    assert(max_bz > 0.1f);
    assert(std::fabs(ga.maps.at("depth")[32 * 96 + 64] - ga.maps.at("depth")[10 * 96 + 10]) > 0.65f);

    // 2. Slanted reference plane absorbed into homography should be restored via SVD shear regression.
    Fixture tilt(5, 64, 96);
    const std::array<float, 5> bx{{0.0f, -2.5f, -1.2f, 1.2f, 2.5f}};
    for (size_t k = 0; k < 5; ++k) {
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
            const float plane_shift = bx[k] * (0.04f * (static_cast<float>(x) - 47.5f) - 0.02f * (static_cast<float>(y) - 31.5f));
            const bool obj = (y >= 22 && y < 40 && x >= 38 && x < 56);
            const float tot_dx = plane_shift + (obj ? bx[k] : 0.0f);
            const size_t i = static_cast<size_t>(y) * 96 + x;
            tilt.green[k][i] = Fixture::texture(static_cast<float>(y), static_cast<float>(x) - tot_dx);
            tilt.fx[k][i] = tot_dx;
        }
    }
    BurstView btv = tilt.view();
    btv.focal_length_mm = 28.0f;
    btv.aperture = 2.8f;
    btv.focus_distance_m = 3.0f;
    btv.hyperfocal_m = 10.0f;
    btv.enable_kinetic_sign = false;
    const auto gt = compute_fields(btv, {"depth"});
    assert(gt.geometry.gate.observable && gt.geometry.plane_detilted);
    assert(std::fabs(gt.geometry.plane_tilt_rho[1]) > 0.05f);
    assert(std::fabs(gt.maps.at("depth")[10 * 96 + 86] - gt.maps.at("depth")[10 * 96 + 10]) > 0.25f);
    std::cout << "PASS V3 D-1: 3-DOF axial FOE and homography slanted-plane shear de-tilting\n";
}

void test_v3_kinetic_sign_and_vcurve_metric() {
    // 1. Kinetic Occlusion Sign Calibration with ZERO focus metadata & flipped parallax input polarity.
    for (float sgn : {1.0f, -1.0f}) {
        Fixture f(5, 96, 128);
        const std::array<float, 5> sweep{{0.0f, -2.5f * sgn, -1.25f * sgn, 1.25f * sgn, 2.5f * sgn}};
        for (size_t k = 0; k < 5; ++k) {
            const float dx = sweep[k];
            for (int y = 0; y < 96; ++y) for (int x = 0; x < 128; ++x) {
                const bool ref_fg = (y >= 24 && y < 72 && x >= 36 && x < 92);
                const bool src_fg = (y >= 24 && y < 72 && (x - dx) >= 36 && (x - dx) < 92);
                const float ux = src_fg ? (static_cast<float>(x) - dx) : static_cast<float>(x);
                const size_t i = static_cast<size_t>(y) * 128 + x;
                const float tex_fg = 0.55f + 0.08f * std::sin(ux * 0.55f) + 0.06f * std::cos(y * 0.50f);
                const float tex_bg = 0.15f + 0.03f * std::sin(static_cast<float>(x) * 0.45f) + 0.03f * std::cos(y * 0.42f);
                f.green[k][i] = src_fg ? tex_fg : tex_bg;
                f.fx[k][i] = ref_fg ? dx : 0.0f;
            }
        }
        BurstView bv = f.view();
        bv.enable_kinetic_sign = true;
        const auto res = compute_fields(bv, {"depth"});
        assert(res.geometry.gate.observable);
        assert(res.geometry.sign_calibrated);
        assert(res.geometry.sign_source == "kinetic_occlusion");
        assert(res.maps.at("depth")[48 * 128 + 64] > res.maps.at("depth")[10 * 128 + 10] + 0.6f);
    }

    // 2. Non-monotonic thin-lens CoC V-Curve with blurred Near (d=1), sharp Mid (d=0.5), blurred Far (d=0).
    Fixture vc(5, 96, 128);
    const std::array<float, 5> sweep{{0.0f, -2.0f, -1.0f, 1.0f, 2.0f}};
    for (size_t k = 0; k < 5; ++k) {
        const float b = sweep[k];
        for (int y = 0; y < 96; ++y) for (int x = 0; x < 128; ++x) {
            const float ref_rho = (y < 32) ? 0.0f : ((y < 64) ? 1.0f : 2.0f);
            const float x_back = static_cast<float>(x) - ref_rho * b;
            const float rho_src = ref_rho;
            // Mid band (rho=1) has crisp high-freq edges (wblur small), Near/Far have soft edges.
            const bool is_mid = (std::fabs(rho_src - 1.0f) < 0.25f);
            const float freq = is_mid ? 1.35f : 0.28f;
            const float val = 0.30f + 0.12f * std::sin(x_back * freq) + 0.08f * std::cos(y * freq);
            const size_t i = static_cast<size_t>(y) * 128 + x;
            vc.green[k][i] = val;
            vc.fx[k][i] = ref_rho * b;
        }
    }
    BurstView vcv = vc.view();
    vcv.focal_length_mm = 50.0f;
    vcv.aperture = 2.0f;
    vcv.focus_distance_m = 2.5f;
    vcv.hyperfocal_m = 40.0f;
    vcv.enable_kinetic_sign = true;
    const auto vg = analyze_geometry(vcv);
    assert(vg.gate.observable && vg.has_focus_plane);
    assert(vg.focus_plane_depth > 0.25f && vg.focus_plane_depth < 0.75f);
    assert(vg.metric_calibrated && vg.estimated_baseline_mm > 0.0f && vg.diopter_span_inv_m > 0.0f);
    std::cout << "PASS V3 D-3: kinetic occlusion sign and thin-lens CoC V-curve metric calibration\n";
}

void test_v3_rgb_thinplate_and_subspace() {
    // 1. Iso-luminance red-on-green step (G channel constant 0.25, only R/B chroma jump + near center texture).
    Fixture iso(5, 64, 96);
    const std::array<float, 5> bx{{0.0f, -2.0f, -1.0f, 1.0f, 2.0f}};
    for (size_t k = 0; k < 5; ++k) {
        const float dx = bx[k];
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
            const bool ref_fg = (y >= 16 && y < 48 && x >= 24 && x < 72);
            const bool src_fg = (y >= 16 && y < 48 && (x - dx) >= 24 && (x - dx) < 72);
            const float tex = ((y >= 24 && y < 40 && x >= 32 && x < 64) || y < 8 || y >= 56)
                ? 0.02f * std::sin((src_fg ? (x - dx) : static_cast<float>(x)) * 0.7f) : 0.0f;
            const size_t hi = static_cast<size_t>(y) * 96 + x;
            iso.green[k][hi] = 0.25f + tex;
            iso.fx[k][hi] = ref_fg ? dx : 0.0f;
            for (int dy = 0; dy < 2; ++dy) for (int dxr = 0; dxr < 2; ++dxr) {
                const int yr = 2 * y + dy, xr = 2 * x + dxr;
                const uint8_t ch = iso.cfa[yr % 6][xr % 6];
                float v = 0.25f + tex;
                if (ch == 0) v = src_fg ? 0.55f : 0.10f;
                else if (ch == 2) v = src_fg ? 0.10f : 0.55f;
                iso.raw[k][static_cast<size_t>(yr) * 192 + xr] = v;
            }
        }
    }
    const auto r_iso = compute_fields(iso.view(), {"depth"});
    assert(r_iso.geometry.gate.observable);
    assert(std::fabs(r_iso.maps.at("depth")[20 * 96 + 26] - r_iso.maps.at("depth")[20 * 96 + 21]) > 0.50f);

    // 2. Subspace motion-parallax decoupling: non-collinear baseline & timestamps separate moving square from parallax.
    Fixture sub(6, 64, 96);
    const std::array<float, 6> b_osc{{0.0f, 2.0f, -2.0f, 2.0f, -2.0f, 2.0f}};
    for (size_t k = 0; k < 6; ++k) {
        sub.times[k] = static_cast<double>(k) * 0.1;
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
            const bool stat_fg = (y >= 10 && y < 30 && x >= 10 && x < 34);
            const bool mover = (y >= 36 && y < 56 && x >= 52 && x < 76);
            const float dx = stat_fg ? b_osc[k] : (mover ? 1.5f * static_cast<float>(k) : 0.0f);
            const size_t i = static_cast<size_t>(y) * 96 + x;
            sub.green[k][i] = Fixture::texture(static_cast<float>(y), static_cast<float>(x) - dx);
            sub.fx[k][i] = dx;
            sub.mw[k][i] = mover ? 0.0f : 0.9f;
        }
    }
    const auto r_sub = compute_fields(sub.view(), {"depth", "velocity"});
    assert(r_sub.geometry.gate.observable);
    assert(r_sub.maps.at("velocity")[46 * 96 + 64] > 10.0f);
    assert(r_sub.maps.at("velocity")[20 * 96 + 22] < 1.0f);
    std::cout << "PASS V3 D-2/D-4: 3-channel opponent-RGB completion and subspace motion-parallax decoupling\n";
}

void test_v3_layered_bokeh_focus_stack_and_c4() {
    // 1. Layered occlusion bokeh with polygonal stop & revealed background.
    Image img{32, 48, std::vector<float>(32 * 48 * 3, 0.25f)};
    Image bg_rev{32, 48, std::vector<float>(32 * 48 * 3, 0.90f)};
    std::vector<float> depth(32 * 48, 0.1f);
    for (int y = 10; y < 22; ++y) for (int x = 16; x < 32; ++x) {
        depth[static_cast<size_t>(y) * 48 + x] = 0.95f;
        img.rgb[(static_cast<size_t>(y) * 48 + x) * 3 + 0] = 0.10f;
        img.rgb[(static_cast<size_t>(y) * 48 + x) * 3 + 1] = 0.10f;
        img.rgb[(static_cast<size_t>(y) * 48 + x) * 3 + 2] = 0.10f;
    }
    Image flat_const{24, 32, std::vector<float>(24 * 32 * 3, 0.42f)};
    RefocusOptions lo;
    lo.layered_occlusion = true;
    lo.aperture_blades = 7;
    lo.catseye_vignette = 0.25f;
    lo.focus_depth = 0.95f;
    lo.radius_px = 10.0f;
    const auto dc = render_refocus(flat_const, std::vector<float>(24 * 32, 0.1f), 24, 32, lo);
    for (float v : dc.rgb) close(v, 0.42f, 1e-5f);
    // Focus on near FG over defocused bright BG: FG center must receive zero bright-BG bleed.
    lo.revealed_bg = &bg_rev;
    const auto sharp_fg = render_refocus(img, depth, 32, 48, lo);
    close(sharp_fg.rgb[(16 * 48 + 24) * 3 + 0], 0.10f, 1e-5f);
    // Focus on far BG over defocused dark FG: FG bokeh halo softens onto surrounding BG pixels.
    lo.focus_depth = 0.1f;
    const auto blur_fg = render_refocus(img, depth, 32, 48, lo);
    const auto blur_fg_par = render_refocus(img, depth, 32, 48, lo, parallel_rows);
    assert(blur_fg.rgb[(16 * 48 + 13) * 3 + 0] < 0.24f);
    for (size_t i = 0; i < blur_fg.rgb.size(); ++i) close(blur_fg.rgb[i], blur_fg_par.rgb[i], 1e-6f);

    // 2. Focus-Stack with focus breathing zoom across 4 focus distances.
    Fixture fs(4, 32, 48);
    std::vector<float> fpos{0.2f, 0.5f, 0.8f, 1.1f};
    for (size_t k = 0; k < 4; ++k) {
        const float zoom = 1.0f + 0.015f * static_cast<float>(k);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
            const float cy_1 = 31.5f, cx_1 = 47.5f;
            const float zy = cy_1 + (y - cy_1) / zoom, zx = cx_1 + (x - cx_1) / zoom;
            const bool left_zone = (zx < 48.0f);
            const float sharp_sig = 0.25f + 0.10f * std::sin(zx * 0.9f) * std::cos(zy * 0.85f);
            const float blur_sig = 0.25f + 0.02f * std::sin(zx * 0.2f);
            const bool in_foc = (left_zone && k == 0) || (!left_zone && k == 3);
            fs.raw[k][static_cast<size_t>(y) * 96 + x] = in_foc ? sharp_sig : blur_sig;
        }
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 48; ++x) {
            const float cy_h = 15.5f, cx_h = 23.5f;
            const float zy = cy_h + (y - cy_h) / zoom, zx = cx_h + (x - cx_h) / zoom;
            const bool left_zone = (zx < 24.0f);
            const float sharp_sig = 0.25f + 0.10f * std::sin(2.0f * zx * 0.9f) * std::cos(2.0f * zy * 0.85f);
            const float blur_sig = 0.25f + 0.02f * std::sin(2.0f * zx * 0.2f);
            const bool in_foc = (left_zone && k == 0) || (!left_zone && k == 3);
            const size_t hi = static_cast<size_t>(y) * 48 + x;
            fs.green[k][hi] = in_foc ? sharp_sig : blur_sig;
            fs.fx[k][hi] = (x - cx_h) * (1.0f / zoom - 1.0f);
            fs.fy[k][hi] = (y - cy_h) * (1.0f / zoom - 1.0f);
        }
    }
    BurstView fsv = fs.view();
    fsv.focus_positions = &fpos;
    TemporalOptions fopt;
    fopt.mode = TemporalMode::FocusStack;
    const auto fprep = prepare_temporal(fsv, fopt);
    assert(fprep.gate.observable);
    const auto fimg = render_temporal(fsv, fprep, 64, 96);
    assert(fprep.weights[0][16 * 48 + 10] > fprep.weights[3][16 * 48 + 10] + 0.3f);
    assert(fprep.weights[3][16 * 48 + 38] > fprep.weights[0][16 * 48 + 38] + 0.3f);
    assert(fimg.gray_layer.size() == 64u * 96u);

    // 3. D-5 [C4] Disocclusion Unpeeling recovery on suppressed frame.
    Fixture c4(5, 64, 96);
    const std::array<float, 5> bx5{{0.0f, -2.0f, -1.0f, 1.0f, 2.0f}};
    for (size_t k = 0; k < 5; ++k) {
        const float dx = bx5[k];
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 96; ++x) {
            const bool ref_fg = (y >= 16 && y < 48 && x >= 28 && x < 68);
            const bool src_fg = (y >= 16 && y < 48 && (x - dx) >= 28 && (x - dx) < 68);
            const float ux = src_fg ? (static_cast<float>(x) - dx) : static_cast<float>(x);
            const size_t i = static_cast<size_t>(y) * 96 + x;
            const float val = Fixture::texture(static_cast<float>(y), ux);
            c4.green[k][i] = val;
            c4.fx[k][i] = ref_fg ? dx : 0.0f;
            for (int dy = 0; dy < 2; ++dy) for (int dxr = 0; dxr < 2; ++dxr)
                c4.raw[k][static_cast<size_t>(2 * y + dy) * 192 + 2 * x + dxr] =
                    Fixture::texture(static_cast<float>(y) + 0.5f * dy, ux + 0.5f * dxr);
        }
    }
    const auto g_c4 = analyze_geometry(c4.view());
    assert(g_c4.gate.observable);
    const size_t target_idx = 32 * 96 + 48;
    c4.fx[2][target_idx] = 4.0f;
    c4.mw[2][target_idx] = 0.0f;
    const size_t recovered = apply_epipolar_stage3_feedback(c4.view(), g_c4, c4.fy, c4.fx, c4.mw);
    assert(recovered >= 1u);
    assert(c4.mw[2][target_idx] >= 0.7f);
    close(c4.fx[2][target_idx], -1.0f, 0.25f);
    std::cout << "PASS V3 D-5/D-6: layered polygon bokeh, breathing focus-stack, and [C4] unpeeling\n";
}
} // namespace

int main(int argc,char** argv) {
    test_camera_translation(); test_temporal(); test_strobe_and_parallel(); test_capture_order();
    test_geometry(); test_fields(); test_local_geometry(); test_projective_and_cfa(); test_bokeh();
    test_optical_focus_sign_calibration();
    test_v3_epipolar_and_plane_detilt();
    test_v3_kinetic_sign_and_vcurve_metric();
    test_v3_rgb_thinplate_and_subspace();
    test_v3_layered_bokeh_focus_stack_and_c4();
    if(argc>1) test_webgl(argv[1]);
    if(argc>2) test_real_ppm(argv[2]);
    std::cout<<"All suite feature tests passed\n";
}
