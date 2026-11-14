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
    for(float span:{3.98f,4.02f}) {
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
        assert(observed.gate.threshold==4 && observed.gate.units=="1x_px");
        close(observed.disparity_span_px,span,0.005f);
        assert(observed.gate.observable==(span>4));
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
    std::cout<<"PASS parallax: true two-plane separation; noisy/zero/flat flow gates\n";
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
    std::ifstream in(std::filesystem::u8path(path),std::ios::binary);
    const std::string html((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
    assert(html.find("data:image/png;base64,iVBORw0KGgo")!=std::string::npos);
    assert(html.find("webgl2")!=std::string::npos);
    assert(html.find("gl.DEPTH_TEST")!=std::string::npos);
    assert(html.find("http://")==std::string::npos);
    const std::string unicode=path+u8".\u6d4b\u8bd5.html";
    write_webgl_html(unicode,rgb,8,8,f);
    std::ifstream unicode_in(std::filesystem::u8path(unicode),std::ios::binary);
    assert(unicode_in.good());
    unicode_in.close();
    std::filesystem::remove(std::filesystem::u8path(unicode));
    f.gates["depth"].observable=false;
    throws([&] { write_webgl_html(path,rgb,8,8,f); });
    std::cout<<"PASS WebGL embedded PNG and observable-depth gate: "<<path<<'\n';
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
} // namespace

int main(int argc,char** argv) {
    test_camera_translation(); test_temporal(); test_strobe_and_parallel(); test_capture_order();
    test_geometry(); test_fields(); test_local_geometry(); test_projective_and_cfa(); test_bokeh();
    if(argc>1) test_webgl(argv[1]);
    if(argc>2) test_real_ppm(argv[2]);
    std::cout<<"All suite feature tests passed\n";
}
