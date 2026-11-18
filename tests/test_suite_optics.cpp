#include "../heraw_suite_optics.hpp"
#include <iostream>
#include <iterator>
#include <random>

using namespace heraw_optics;
static void check(bool ok,const std::string& message) {
    if(!ok)throw std::runtime_error(message);
}
struct Fixture {
    int h,w;
    Frames raw,g,fx,fy,mw;
    std::array<uint8_t,36> cfa{};
    Fixture(int height,int width,size_t n,bool xtrans=false):h(height),w(width),
      raw(n,std::vector<float>(size_t(h)*w)),g(n,std::vector<float>(size_t(h/2)*(w/2))),
      fx(g),fy(g),mw(n,std::vector<float>(size_t(h/2)*(w/2),1)) {
        const uint8_t xt[36]={1,2,1,1,0,1,0,1,0,2,1,2,1,2,1,1,0,1,
                             1,0,1,1,2,1,2,1,2,0,1,0,1,0,1,1,2,1};
        for(int y=0;y<6;++y)for(int x=0;x<6;++x)cfa[y*6+x]=xtrans?xt[y*6+x]:uint8_t(y%2?(x%2?2:1):(x%2?1:0));
    }
    Input input() const {return {raw,g,fx,fy,mw,cfa.data(),h,w,{}};}
    void green() {
        for(size_t f=0;f<raw.size();++f)for(int y=0;y<h/2;++y)for(int x=0;x<w/2;++x){
            double sum=0;int n=0;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
                int yy=y*2+dy,xx=x*2+dx;
                if(cfa[yy%6*6+xx%6]==1){sum+=raw[f][size_t(yy)*w+xx];++n;}
            }
            g[f][size_t(y)*(w/2)+x]=float(sum/std::max(1,n));
        }
    }
};
static Fixture edges(double sigma,bool xtrans=false) {
    Fixture a(512,512,2,xtrans);
    for(size_t f=0;f<a.raw.size();++f)for(int y=0;y<a.h;++y)for(int x=0;x<a.w;++x){
        int bx=x/64,by=y/64;
        double angle=(bx+3*by)%2?0.20+0.033*((bx+by)%3):1.30-0.029*((bx+2*by)%4);
        double nx=std::cos(angle),ny=std::sin(angle);
        double sx=0,sy=0;
        int c=a.cfa[(y%6)*6+x%6];
        if(c!=1){
            double xn=(x-a.w*.5)/a.w,yn=(y-a.h*.5)/a.w;
            double scale=(c==0?.8:-.7)+(c==0?.4:-.3)*(xn*xn+yn*yn);
            sx=(c==0?.35:-.25)+xn*scale;sy=(c==0?-.22:.18)+yn*scale;
        }
        double d=nx*(x-(bx*64+31.3)-sx)+ny*(y-(by*64+32.7)-sy);
        a.raw[f][size_t(y)*a.w+x]=float(.15+.4*.5*(1+std::erf(d/(std::sqrt(2.0)*sigma))));
        if(f){a.fx[f][size_t(y/2)*(a.w/2)+x/2]=.35f;a.fy[f][size_t(y/2)*(a.w/2)+x/2]=-.2f;}
    }
    a.green();return a;
}
static void edge_tests(const std::string& dir) {
    auto a=edges(1.1);
    Options opt;opt.far_field=true;opt.noise_sigma=.0002;opt.max_edges=512;
    opt.camera="Test & Camera";opt.lens="Lens <50> \"prime\"";opt.focal_mm=50;opt.aperture=2.8;opt.focus_m=100;
    opt.focal_opt_mm=51.2;opt.focus_low_m=88;opt.focus_high_m=116;opt.dof_near_m=30;
    opt.dof_far_m=1000000;opt.hyperfocal_m=42.5;opt.infinity_coc_1x_px=1.2;opt.infinity_in_dof=true;
    auto input=a.input();input.time_seconds={0,.01};
    auto r=analyze(input,opt);
    const std::string jstr = report_json(r);
    check(jstr.find("\"focus_distance_m\":100")!=std::string::npos,"optics JSON focus_distance_m");
    check(jstr.find("\"focus_distance_range_m\":[88,116]")!=std::string::npos,"optics JSON focus_distance_range_m");
    check(jstr.find("\"hyperfocal_m\":42.5")!=std::string::npos,"optics JSON hyperfocal_m");
    check(jstr.find("\"infinity_in_dof\":true")!=std::string::npos,"optics JSON infinity_in_dof");
    std::cout<<"edge mtf50 ";
    for(const auto&z:r.zones)std::cout<<z.mtf50.value<<"("<<z.edges<<") ";
    std::cout<<"\nlca "<<r.lca.channels[0].status<<" "<<r.lca.channels[0].samples<<" rms "<<r.lca.channels[0].rms_px<<"\n";
    for(const auto&c:r.lca.channels){for(double v:c.coefficients)std::cout<<v<<" ";std::cout<<"\n";}
    check(r.zones[0].mtf50.valid,"slanted edge center MTF valid");
    double expected=std::sqrt(2*std::log(2.0))/(2*pi*1.1);
    check(std::abs(r.zones[0].mtf50.value-expected)<.025,"Gaussian edge analytic MTF50");
    check(r.lca.channels[0].valid&&r.lca.channels[1].valid,"LCA fit observable");
    auto sr=r.lca.shift(0,256,256),sb=r.lca.shift(2,256,256);
    check(std::abs(sr[0]-.35)<.10&&std::abs(sr[1]+.22)<.10,"red PSF shift direction/magnitude");
    check(std::abs(sb[0]+.25)<.10&&std::abs(sb[1]-.18)<.10,"blue PSF shift direction/magnitude");
    check(r.drift[1].dx.valid&&std::abs(r.drift[1].dx.value-.7)<1e-6,"half-flow to raw units");
    check(r.drift[1].speed_px_s.valid&&r.drift[1].speed_px_s.value>75,"metadata speed");
    check(!r.distortion.valid&&!r.vignette.valid,"small drift calibration gate");
    write_json(dir+"/optics.json",r);write_lcp(dir+"/optics.lcp",r);write_passport_png(dir+"/passport.png",r);
    auto b=edges(1.8);auto rb=analyze(b.input(),opt);
    check(rb.zones[0].mtf50.valid&&rb.zones[0].mtf50.value<r.zones[0].mtf50.value*.75,"causal blur delta");
    auto xt=edges(1.1,true);auto rx=analyze(xt.input(),opt);
    check(rx.zones[0].mtf50.valid&&std::abs(rx.zones[0].mtf50.value-expected)<.035,"X-Trans raw sampling");
    check(rx.lca.channels[0].valid,"X-Trans LCA");
    auto nofar=analyze(a.input());
    check(!nofar.zones[0].mtf50.valid,"near scene cannot publish MTF");
    check(nofar.lca.channels[0].valid,"LCA still observable near scene");
    auto parallax=a;
    for(int y=64;y<192;++y)for(int x=64;x<192;++x)parallax.fx[1][size_t(y)*256+x]+=2;
    auto rp=analyze(parallax.input(),opt);
    check(!rp.zones[0].mtf50.valid,"local parallax rejects far-field center MTF");
    auto chronology=b;
    chronology.raw.push_back(a.raw[0]);chronology.g.push_back(a.g[0]);
    chronology.fx.push_back(a.fx[0]);chronology.fy.push_back(a.fy[0]);chronology.mw.push_back(a.mw[0]);
    chronology.raw[1]=a.raw[1];chronology.g[1]=a.g[1];
    auto ci=chronology.input();ci.time_seconds={0,.01,.02};
    auto cr=analyze(ci,opt);
    check(cr.first_frame_shake_ratio.valid&&cr.first_frame_shake_ratio.value>1.4,"chronological first-frame optical blur ratio");
    // Calibrated target with deliberate outliers; >=50 raw-pixel drift.
    std::fill(a.fx[1].begin(),a.fx[1].end(),30);
    for(int y=20;y<500;y+=40)for(int x=20;x<500;x+=40){
        double xn=(x-256.0)/512,yn=(y-256.0)/512,r2=xn*xn+yn*yn;
        double k=.12*r2-.05*r2*r2;
        opt.distortion_points.push_back({double(x),double(y),x+(x-256)*k,y+(y-256)*k});
        opt.flat_points.push_back({double(x),double(y),.5*(1-.7*r2+.25*r2*r2-.04*r2*r2*r2),.5});
    }
    opt.distortion_points[7].observed_x+=30;opt.flat_points[3].measured=.6;
    auto calibrated=analyze(a.input(),opt);
    check(calibrated.distortion.valid&&calibrated.vignette.valid,"calibrated radial models");
    check(std::abs(calibrated.distortion.coefficients[0]-.12)<.005,"robust radial k1");
    check(std::abs(calibrated.vignette.coefficients[0]+.7)<.02,"robust flat attenuation");
    // Centered radial LCA for standards-reader checks.
    for(auto& c:calibrated.lca.channels){c.coefficients[0]=0;c.coefficients[1]=0;}
    write_json(dir+"/calibrated.json",calibrated);write_lcp(dir+"/calibrated.lcp",calibrated);
}
static void star_tests(const std::string& dir) {
    Fixture a(400,400,4,true);
    std::mt19937 gen(42);std::uniform_real_distribution<double>jitter(-3,3);
    std::vector<std::array<double,2>> positions;
    for(int y=24;y<390;y+=32)for(int x=24;x<390;x+=32)positions.push_back({x+jitter(gen),y+jitter(gen)});
    for(size_t f=0;f<a.raw.size();++f){
        std::fill(a.raw[f].begin(),a.raw[f].end(),.01f);
        for(auto p:positions){
            double cx=p[0]+f*.31,cy=p[1]-f*.19;
            for(int y=std::max(0,int(cy)-8);y<std::min(a.h,int(cy)+9);++y)
              for(int x=std::max(0,int(cx)-8);x<std::min(a.w,int(cx)+9);++x){
                double dx=x-cx,dy=y-cy;
                a.raw[f][size_t(y)*a.w+x]+=float(.4*std::exp(-.5*(dx*dx/(1.25*1.25)+dy*dy/(1.6*1.6))));
            }
        }
        std::fill(a.fx[f].begin(),a.fx[f].end(),float(f*.31*.5));
        std::fill(a.fy[f].begin(),a.fy[f].end(),float(-f*.19*.5));
    }
    a.green();Options opt;opt.stars=true;opt.noise_sigma=.0001;opt.max_stars=200;opt.max_edges=0;
    auto r=analyze(a.input(),opt);
    for(const auto&z:r.zones)std::cout<<"star "<<z.name<<" n="<<z.stars<<" coverage="<<z.epsf_coverage<<" mtf="<<z.mtf50.value<<" major="<<z.fwhm_major.value<<" minor="<<z.fwhm_minor.value<<"\n";
    check(r.zones[0].mtf50.valid,"star MTF valid");
    check(r.zones[0].epsf.size()==97*97,"8x ePSF grid");
    check(std::abs(r.zones[0].fwhm_major.value-2.35482*1.6)<.25,"stellar PSF major width");
    check(std::abs(r.zones[0].fwhm_minor.value-2.35482*1.25)<.25,"stellar PSF minor width");
    double sum=std::accumulate(r.zones[0].epsf.begin(),r.zones[0].epsf.end(),0.0);
    check(std::abs(sum-1)<1e-6,"ePSF energy conservation");
    write_json(dir+"/stars.json",r);
    opt.ois=true;auto envelope=analyze(a.input(),opt);
    check(envelope.zones[0].mtf50.valid,"OIS per-frame stellar envelope");
}
static void boundary_tests(const std::string& dir) {
    Frames empty;Input e{empty,empty,empty,empty,empty,nullptr,0,0,{}};
    auto r=analyze(e);check(r.status=="empty_input","empty status");
    write_json(dir+"/empty.json",r);write_lcp(dir+"/empty.lcp",r);
    Fixture flat(64,64,2);for(auto&f:flat.raw)std::fill(f.begin(),f.end(),.25f);flat.green();
    Options o;o.far_field=true;o.stars=true;auto q=analyze(flat.input(),o);
    check(!q.drift_span.valid&&!q.zones[0].mtf50.valid&&!q.lca.channels[0].valid,"low texture has null measurements");
    write_json(dir+"/flat.json",q);
    flat.fx[0].clear();bool failed=false;
    try{analyze(flat.input());}catch(const std::invalid_argument&){failed=true;}check(failed,"malformed flow rejected");
    auto ed=edges(1.1);for(auto& f:ed.mw)std::fill(f.begin(),f.end(),std::numeric_limits<float>::quiet_NaN());
    check(!analyze(ed.input()).drift_span.valid,"NaN confidence rejected");
    o.noise_sigma=0;failed=false;try{analyze(e,o);}catch(const std::invalid_argument&){failed=true;}
    check(failed,"zero noise rejected");
    failed=false;try{write_json(dir+"/missing/subdir/x.json",r);}catch(const std::runtime_error&){failed=true;}
    check(failed,"write failure propagated");
}
static void dataset_tests(const std::string& dir) {
    Fixture a(16,16,1,true);
    std::vector<float>target(16*16*12),sigma(16*16*4,.003f),phase(8*8,1),motion(8*8,0);
    for(size_t i=0;i<a.raw[0].size();++i)a.raw[0][i]=float(i)/256;
    for(size_t i=0;i<target.size();++i)target[i]=float(i%100)/99;
    target[0]=1.2f;target[1]=-.02f;
    DatasetInput in{a.raw[0],target,sigma,phase,motion,a.cfa.data(),16,16,true,true,"Camera","independent linear half stack"};
    DatasetOptions opt;opt.tile_size=8;
    phase[0]=.8f;phase[4]=0;motion[4*8]=1; sigma[16*32+16]=std::numeric_limits<float>::quiet_NaN();
    auto r=export_dataset(dir+"/dataset",in,opt);
    check(r.accepted==1&&r.rejected==3,"tile mean phase, max motion and finite sigma gates");
    check(heraw_fs::from_utf8(r.manifest_path).filename()=="dataset_manifest.json","returned dataset manifest path");
    std::vector<float> empty;
    DatasetInput streamed{a.raw[0],empty,empty,phase,motion,a.cfa.data(),16,16,true,true,"Camera","independent linear half stack"};
    size_t target_calls=0,sigma_calls=0;
    streamed.target_sample=[&](int y,int x,int c){
        check(y>=0&&y<32&&x>=0&&x<32&&c>=0&&c<3,"target callback global 2x coordinates");
        ++target_calls;return target[(size_t(y)*32+x)*3+c];
    };
    streamed.sigma_sample=[&](int y,int x){++sigma_calls;return sigma[size_t(y)*32+x];};
    auto sr=export_dataset(dir+"/dataset_callbacks",streamed,opt);
    check(sr.accepted==r.accepted&&sr.rejected==r.rejected,"callback/vector tile gate parity");
    check(target_calls==2*16*16*3&&sigma_calls==2*16*16,"callback allocation/work limited to eligible tiles");
    auto bytes=[](const std::filesystem::path& path){
        std::ifstream f(path,std::ios::binary);check(bool(f),"parity artifact open");
        return std::vector<char>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    };
    for(const auto& entry:std::filesystem::directory_iterator(heraw_fs::from_utf8(dir+"/dataset")))
        check(bytes(entry.path())==bytes(heraw_fs::from_utf8(dir+"/dataset_callbacks")/entry.path().filename()),
              "vector/callback byte-identical export "+entry.path().filename().string());
    in.target_sample=[](int,int,int)->float{throw std::runtime_error("vector must take precedence");};
    in.sigma_sample=[](int,int)->float{throw std::runtime_error("vector must take precedence");};
    check(export_dataset(dir+"/dataset_vector_priority",in,opt).accepted==1,"nonempty vectors override callbacks");
    // Float thresholds match the map precision: equality passes for phase and
    // fails for positive motion thresholds; zero means max_motion <= 0.
    for(int y=0;y<4;++y)for(int x=0;x<4;++x)phase[size_t(y)*8+x]=opt.min_conf;
    motion[0]=opt.motion_threshold;
    check(export_dataset(dir+"/motion_equal",in,opt).accepted==0,"strict positive motion boundary");
    motion[0]=0;
    opt.motion_threshold=0;
    check(export_dataset(dir+"/strict_static",in,opt).accepted==1,"phase equality and zero motion boundary");
    motion[0]=.001f;
    check(export_dataset(dir+"/strict_static_reject",in,opt).accepted==0,"zero threshold rejects positive motion");
    motion[0]=0;
    opt.min_conf=1;
    check(export_dataset(dir+"/higher_conf",in,opt).accepted==0,"configured phase causal delta");
    opt.min_conf=.9f;
    check(export_dataset(dir+"/lower_conf",in,opt).accepted==2,"configured lower phase admits extra tile");
    opt.min_conf=.95f;
    streamed.sigma_sample={};bool failed=false;
    try{export_dataset(dir+"/missing_sampler",streamed,opt);}catch(const std::invalid_argument&){failed=true;}
    check(failed,"empty sigma requires callback");
    opt.motion_threshold=std::numeric_limits<float>::quiet_NaN();failed=false;
    try{export_dataset(dir+"/invalid_threshold",in,opt);}catch(const std::invalid_argument&){failed=true;}
    check(failed,"nonfinite threshold rejected");opt.motion_threshold=.04f;
    in.reference_excluded_from_target=false;
    auto rejected=export_dataset(dir+"/correlated",in,opt);
    check(rejected.accepted==0&&rejected.status=="requires_linear_independent_target","independent target gate");
    opt.tile_size=7;failed=false;
    try{export_dataset(dir+"/invalid",in,opt);}catch(const std::invalid_argument&){failed=true;}
    check(failed,"odd tile rejected");
}
static Fixture radial_scene(double k1,double k2,double log_a,double log_b,double shift=32) {
    Fixture a(512,512,3);
    const std::array<std::array<double,2>,3> shifts={{{0,0},{shift,6},{-5,shift+4}}};
    // Independent generator: invert radius by bisection (estimator uses Newton).
    auto ideal=[&](double x,double y){
        double rd=std::hypot(x,y),lo=0,hi=1.5;
        for(int i=0;i<60;++i){
            double radius=(lo+hi)*.5,r2=radius*radius;
            if(radius*(1+k1*r2+k2*r2*r2)<rd)lo=radius;else hi=radius;
        }
        double scale=rd>0?(lo+hi)*.5/rd:1;
        return std::array<double,2>{x*scale,y*scale};
    };
    auto radiance=[](double x,double y){
        return .35+.018*std::sin(x*.075)+.015*std::cos(y*.065)+.008*std::sin((x+y)*.043);
    };
    for(size_t f=0;f<a.raw.size();++f){
        for(int y=0;y<a.h;++y)for(int x=0;x<a.w;++x){
            double xn=(x-256.)/512,yn=(y-256.)/512,r2=xn*xn+yn*yn;
            auto p=ideal(xn,yn);
            double signal=radiance(p[0]*512+256-shifts[f][0],p[1]*512+256-shifts[f][1]);
            a.raw[f][size_t(y)*a.w+x]=float(signal*std::exp(log_a*r2+log_b*r2*r2));
        }
        for(int y=0;y<256;++y)for(int x=0;x<256;++x){
            auto p=ideal((2*x+.5-256)/512,(2*y+.5-256)/512);
            double u=p[0]+shifts[f][0]/512,v=p[1]+shifts[f][1]/512,r2=u*u+v*v;
            double scale=1+k1*r2+k2*r2*r2,sx=u*scale*512+256,sy=v*scale*512+256;
            size_t i=size_t(y)*256+x;
            a.fx[f][i]=float((sx-(2*x+.5))*.5);a.fy[f][i]=float((sy-(2*y+.5))*.5);
            if(sx<4||sy<4||sx>=508||sy>=508)a.mw[f][i]=0;
        }
    }
    a.green();return a;
}
static void automatic_radial_tests(const std::string& dir) {
    auto a=radial_scene(.16,-.07,-.7,.25);
    Options opt;opt.max_edges=0;
    auto r=analyze(a.input(),opt);
    std::cout<<"scene distortion "<<r.distortion.status<<" n="<<r.distortion.samples
             <<" inliers="<<r.distortion.inliers<<" condition="<<r.distortion.condition_ratio
             <<" k="<<r.distortion.coefficients[0]<<","<<r.distortion.coefficients[1]<<" rms="<<r.distortion.rms<<"\n";
    std::cout<<"scene vignette "<<r.vignette.status<<" n="<<r.vignette.samples
             <<" a,b="<<r.vignette.log_coefficients[0]<<","<<r.vignette.log_coefficients[1]<<" rms="<<r.vignette.rms<<"\n";
    check(r.drift_span.valid&&r.drift_span.value>=25,"natural scene raw drift gate");
    check(r.distortion.valid&&r.distortion.status=="scene_estimate","automatic radial/homography observable");
    check(std::abs(r.distortion.coefficients[0]-.16)<.004&&std::abs(r.distortion.coefficients[1]+.07)<.015,
          "joint homography/distortion known coefficients");
    check(r.vignette.valid&&r.vignette.log_model_valid&&r.vignette.status=="scene_estimate","automatic aligned photometric ratios");
    check(std::abs(r.vignette.log_coefficients[0]+.7)<.02&&std::abs(r.vignette.log_coefficients[1]-.25)<.05,
          "same-point radial log ratio truth");
    write_json(dir+"/scene_radial.json",r);write_lcp(dir+"/scene_radial.lcp",r);
    auto zero=radial_scene(0,0,0,0);
    auto rz=analyze(zero.input(),opt);
    check(rz.distortion.valid&&std::abs(rz.distortion.coefficients[0])<.002,"zero distortion measurable causal control");
    check(rz.vignette.valid&&std::abs(rz.vignette.log_coefficients[0])<.02,"zero vignette causal control");
    // The exact revised gate: 25 raw pixels equals 50 output pixels.
    auto gate=zero;
    gate.raw.resize(2);gate.g.resize(2);gate.fx.resize(2);gate.fy.resize(2);gate.mw.resize(2);
    std::fill(gate.fx[1].begin(),gate.fx[1].end(),12.5f);std::fill(gate.fy[1].begin(),gate.fy[1].end(),0);
    auto at=analyze(gate.input(),opt);
    check(at.distortion.valid,"25 raw pixel boundary enables scene geometry");
    std::fill(gate.fx[1].begin(),gate.fx[1].end(),12.49f);
    auto below=analyze(gate.input(),opt);
    check(!below.distortion.valid&&below.distortion.status=="requires_25px_raw_drift","below 25 raw pixel gate");
    auto symmetric=zero;
    for(size_t f=1;f<3;++f){
        std::fill(symmetric.fx[f].begin(),symmetric.fx[f].end(),f==1?6.5f:-6.5f);
        std::fill(symmetric.fy[f].begin(),symmetric.fy[f].end(),0);
    }
    auto sym=analyze(symmetric.input(),opt);
    check(sym.drift_span.value==26&&sym.max_drift_1x.value==13&&!sym.distortion.valid,
          "gate uses max reference drift rather than peak-to-peak span");
    auto inconsistent=a;
    for(size_t f=1;f<inconsistent.fx.size();++f)for(int y=0;y<256;++y)for(int x=0;x<256;++x){
        size_t i=size_t(y)*256+x;
        inconsistent.fx[f][i]+=float(2*std::sin(x*.09)*std::cos(y*.071));
    }
    auto bad=analyze(inconsistent.input(),opt);
    check(!bad.distortion.valid,"nonplanar flow residual gate");
    auto sparse=a;
    for(size_t f=1;f<sparse.mw.size();++f)for(int y=0;y<256;++y)for(int x=0;x<256;++x)
        if(x<120||x>134)sparse.mw[f][size_t(y)*256+x]=0;
    check(!analyze(sparse.input(),opt).distortion.valid,"insufficient spatial support gate");
    // A rotation about the optical center commutes with any radial warp.
    // Exercise the joint solver directly so its rank gate is tested separately
    // from the public minimum-translation gate.
    auto rotation=gate;
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){
        double px=2*x+.5-256,py=2*y+.5-256;
        double rx=std::cos(.3)*px-std::sin(.3)*py,ry=std::sin(.3)*px+std::cos(.3)*py;
        size_t i=size_t(y)*256+x;rotation.fx[1][i]=float((rx-px)*.5);rotation.fy[1][i]=float((ry-py)*.5);
    }
    OpticsReport gauge;gauge.drift.resize(2);
    gauge.drift[1].dx=detail::measured(0,1);gauge.drift[1].dy=detail::measured(0,1);
    detail::scene_distortion(rotation.input(),gauge,gauge.distortion);
    check(!gauge.distortion.valid&&
          (gauge.distortion.status=="unobservable_radial_homography_rank"||
           gauge.distortion.status=="weak_radial_homography_conditioning"),"centered rotation radial gauge degeneracy");
    // Explicit observations keep priority over automatic scene information.
    opt.distortion_points={{0,0,0,0}};
    check(analyze(a.input(),opt).distortion.status=="insufficient_or_inconsistent_target","explicit calibration priority");
}
static void real_sample(const std::string& path,const std::string& dir) {
    // Portable fixture: little-endian u32 width/height, 36 CFA bytes, then f32
    // camera-linear raw. Decode/black-normalize with an independent RAW reader.
    std::ifstream f(heraw_fs::from_utf8(path),std::ios::binary);
    check(bool(f),"open real sample");
    auto u32=[&](){
        std::array<unsigned char,4>b{};f.read(reinterpret_cast<char*>(b.data()),4);
        check(bool(f),"real sample header");
        return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);
    };
    uint32_t w=u32(),h=u32();check(w>=64&&h>=64&&w<=12000&&h<=12000,"real sample shape");
    Fixture a(int(h),int(w),1);
    f.read(reinterpret_cast<char*>(a.cfa.data()),36);check(bool(f),"real sample CFA");
    for(float& v:a.raw[0]){uint32_t bits=u32();std::memcpy(&v,&bits,4);}
    a.green();Options opt;opt.camera="independently decoded real RAW";opt.max_edges=200;
    auto r=analyze(a.input(),opt);
    check(r.frames==1&&r.status=="analyzed","real sample entry");
    for(const auto&z:r.zones)check(!z.mtf50.valid,"real sample far-field declaration gate");
    check(r.lca.channels[0].samples>0,"real sample detects valid slanted edges");
    write_json(dir+"/real.json",r);write_lcp(dir+"/real.lcp",r);write_passport_png(dir+"/real.png",r);
    std::cout<<"REAL_RAW:PASS "<<w<<"x"<<h<<" edges="<<r.lca.channels[0].samples<<"\n";
}
int main(int argc,char**argv) {
    try{
        if(argc==4&&std::string(argv[1])=="--real"){
            std::filesystem::create_directories(heraw_fs::from_utf8(argv[3]));
            real_sample(argv[2],argv[3]);return 0;
        }
        std::string dir=argc>1?argv[1]:"optics_test_output";
        std::filesystem::create_directories(heraw_fs::from_utf8(dir));
        edge_tests(dir);star_tests(dir);boundary_tests(dir);dataset_tests(dir);automatic_radial_tests(dir);
        std::cout<<"OPTICS_TEST:PASS\n";return 0;
    }catch(const std::exception&e){std::cerr<<"OPTICS_TEST:FAIL "<<e.what()<<"\n";return 1;}
}
