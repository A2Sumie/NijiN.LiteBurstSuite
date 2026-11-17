#ifndef HERAW_SUITE_OPTICS_HPP
#define HERAW_SUITE_OPTICS_HPP

// Camera-linear optical measurements. Coordinates and displacements are in
// active-area raw pixels; flow maps are reference-to-source in half pixels.
// LCP: photoshop:CameraProfiles, stCamera models (Adobe installed LCPs and
// ExifTool XMP.pm); coordinate equations corroborated by RawTherapee lcp.cc.
// NPY 1.0 / NPZ: numpy.org/doc/stable/reference/generated/numpy.lib.format.html.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace heraw_optics {

using Frames = std::vector<std::vector<float>>;
constexpr double pi = 3.14159265358979323846;
struct Input {
    const Frames& frames_lin;
    const Frames& g_halfs;
    const Frames& flows_fx;
    const Frames& flows_fy;
    const Frames& mw_half;
    const uint8_t* cfa6x6; // 36 row-major entries: R=0, G=1, B=2.
    int H1, W1;
    // Optional seconds, in the same order as frames (including reference).
    std::vector<double> time_seconds;
};
struct Metric {
    bool valid = false;
    double value = 0;
    size_t samples = 0;
    std::string status = "insufficient_samples";
};
struct CalibrationPoint {
    // Independent calibration target: ideal and measured source coordinates.
    double ideal_x = 0, ideal_y = 0, observed_x = 0, observed_y = 0;
};
struct FlatPoint {
    // Known flat radiance after exposure normalization, same linear RAW domain.
    double x = 0, y = 0, measured = 0, expected = 0;
};
struct Options {
    bool far_field = false, stars = false, ois = false;
    double noise_sigma = 0.001;
    size_t max_edges = 160, max_stars = 100;
    std::string camera, lens;
    double focal_mm = 0, aperture = 0, focus_m = 0, crop_factor = 1;
    // Independent target observations break scene geometry/illumination ambiguity.
    std::vector<CalibrationPoint> distortion_points;
    std::vector<FlatPoint> flat_points;
};
struct LcaChannel {
    bool valid = false;
    // dx=tx + x_norm*(scale + radial*r_norm^2), likewise dy.
    // x_norm=(x-W/2)/max(W,H); coefficients in raw pixels.
    std::array<double,4> coefficients{};
    double rms_px = 0;
    size_t samples = 0;
    std::string status = "insufficient_edges";
};
struct LcaModel {
    int width = 0, height = 0;
    std::array<LcaChannel,2> channels; // red, blue
    std::array<double,2> shift(int channel, double x, double y) const {
        if (channel == 1 || (channel != 0 && channel != 2)) return {0,0};
        const auto& c = channels[channel == 0 ? 0 : 1];
        if (!c.valid || width <= 0 || height <= 0) return {0,0};
        const double d = std::max(width,height);
        x = (x-width*0.5)/d; y = (y-height*0.5)/d;
        double s = c.coefficients[2]+c.coefficients[3]*(x*x+y*y);
        return {c.coefficients[0]+x*s,c.coefficients[1]+y*s};
    }
};
struct Zone {
    std::string name, method = "none", status = "insufficient_samples";
    size_t edges = 0, stars = 0, raw_samples = 0;
    Metric mtf50, mtf10, fwhm_major, fwhm_minor, angle_deg;
    std::vector<double> mtf; // 0..1 cycles/raw-pixel, step 1/128.
    std::vector<float> epsf; // 97x97, spacing 1/8 raw pixel; normalized sum.
    double epsf_coverage = 0;
};
struct Drift {
    Metric dx, dy, row_shear_x, row_shear_y, time_seconds, speed_px_s;
    std::array<Metric,4> lca_offset; // R dx/dy, B dx/dy relative to static fit.
};
struct RadialCalibration {
    bool valid = false;
    std::array<double,3> coefficients{};
    double rms = 0;
    size_t samples = 0;
    std::string status = "requires_25px_raw_drift";
    std::string rms_unit = "normalized_max_dimension";
    size_t inliers = 0;
    double condition_ratio = 0;
    bool log_model_valid = false;
    std::array<double,2> log_coefficients{};
};
struct OpticsReport {
    int width = 0, height = 0;
    size_t frames = 0;
    std::string status = "empty_input", camera, lens;
    double focal_mm = 0, aperture = 0, focus_m = 0, crop_factor = 1;
    bool ois = false;
    std::array<Zone,5> zones;
    LcaModel lca;
    std::vector<Drift> drift;
    std::array<size_t,4> phase_quadrants{};
    Metric drift_span, first_frame_shake_ratio, decenter_ratio;
    Metric max_drift_1x; // Maximum reference-to-source translation, raw pixels.
    RadialCalibration distortion, vignette;
};

namespace detail {
inline Metric measured(double v, size_t n, const std::string& s = "measured") {
    Metric m; m.valid=std::isfinite(v); m.value=m.valid?v:0; m.samples=n;
    m.status=m.valid?s:"nonfinite"; return m;
}
inline double median(std::vector<double> a) {
    if (a.empty()) return 0;
    size_t k=a.size()/2;
    std::nth_element(a.begin(),a.begin()+k,a.end());
    double v=a[k];
    if (!(a.size()&1)) v=(v+*std::max_element(a.begin(),a.begin()+k))*0.5;
    return v;
}
inline double sample(const std::vector<float>& a,int h,int w,double x,double y) {
    if (!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>w-1||y>h-1)
        return std::numeric_limits<double>::quiet_NaN();
    int ix=int(x),iy=int(y),jx=std::min(ix+1,w-1),jy=std::min(iy+1,h-1);
    double fx=x-ix,fy=y-iy;
    return (1-fy)*((1-fx)*a[size_t(iy)*w+ix]+fx*a[size_t(iy)*w+jx])
           +fy*((1-fx)*a[size_t(jy)*w+ix]+fx*a[size_t(jy)*w+jx]);
}
inline int zone(double x,double y,int w,int h) {
    if (std::abs(x/w-0.5)<0.25 && std::abs(y/h-0.5)<0.25) return 0;
    return 1+(x>=w*0.5?1:0)+(y>=h*0.5?2:0);
}
inline bool solve(std::vector<std::vector<double>> a,std::vector<double> b,
                  std::vector<double>& x) {
    size_t n=b.size(); double scale=0;
    for (size_t i=0;i<n;++i) scale=std::max(scale,std::abs(a[i][i]));
    if (!(scale>0)) return false;
    for (size_t i=0;i<n;++i) {
        size_t p=i;
        for (size_t j=i+1;j<n;++j) if(std::abs(a[j][i])>std::abs(a[p][i])) p=j;
        if(std::abs(a[p][i])<1e-9*scale) return false;
        std::swap(a[i],a[p]);std::swap(b[i],b[p]);
        double f=a[i][i];
        for(size_t k=i;k<n;++k)a[i][k]/=f;
        b[i]/=f;
        for(size_t j=0;j<n;++j) if(j!=i) {
            f=a[j][i];
            for(size_t k=i;k<n;++k)a[j][k]-=f*a[i][k];
            b[j]-=f*b[i];
        }
    }
    x=b; for(double v:x)if(!std::isfinite(v))return false;
    return true;
}
inline bool robust_fit(const std::vector<std::vector<double>>& rows,
                       const std::vector<double>& values,
                       std::vector<double>& fit,double& rms) {
    if(rows.empty()||rows.size()!=values.size()||rows.size()<rows[0].size()*3) return false;
    size_t p=rows[0].size(),n=rows.size();
    std::vector<double> weights(n,1);
    for(int iteration=0;iteration<6;++iteration) {
        std::vector<std::vector<double>> a(p,std::vector<double>(p));
        std::vector<double>b(p);
        for(size_t i=0;i<n;++i)for(size_t j=0;j<p;++j){
            b[j]+=weights[i]*rows[i][j]*values[i];
            for(size_t k=0;k<p;++k)a[j][k]+=weights[i]*rows[i][j]*rows[i][k];
        }
        if(!solve(a,b,fit))return false;
        std::vector<double> residual(n);
        for(size_t i=0;i<n;++i)
            residual[i]=std::abs(values[i]-std::inner_product(rows[i].begin(),rows[i].end(),fit.begin(),0.0));
        double s=std::max(1e-8,1.4826*median(residual));
        double ss=0,sw=0;
        for(size_t i=0;i<n;++i){
            weights[i]=std::min(1.0,1.5*s/std::max(1e-12,residual[i]));
            ss+=weights[i]*residual[i]*residual[i];sw+=weights[i];
        }
        rms=std::sqrt(ss/sw);
    }
    return true;
}
inline void validate(const Input& in) {
    if(in.H1<0||in.W1<0||in.H1>200000||in.W1>200000)
        throw std::invalid_argument("optics dimensions");
    if(in.frames_lin.empty())return;
    if(in.H1<4||in.W1<4||!in.cfa6x6)throw std::invalid_argument("optics CFA/dimensions");
    int counts[3]={};
    for(int i=0;i<36;++i){if(in.cfa6x6[i]>2)throw std::invalid_argument("optics CFA");++counts[in.cfa6x6[i]];}
    if(!counts[0]||!counts[1]||!counts[2])throw std::invalid_argument("optics CFA channels");
    size_t n=in.frames_lin.size(),full=size_t(in.H1)*in.W1,half=size_t(in.H1/2)*(in.W1/2);
    if(in.g_halfs.size()!=n||in.flows_fx.size()!=n||in.flows_fy.size()!=n||in.mw_half.size()!=n)
        throw std::invalid_argument("optics frame count");
    for(size_t f=0;f<n;++f)
        if(in.frames_lin[f].size()!=full||in.g_halfs[f].size()!=half||
           in.flows_fx[f].size()!=half||in.flows_fy[f].size()!=half||in.mw_half[f].size()!=half)
            throw std::invalid_argument("optics plane size");
}
struct Edge {
    double x=0,y=0,nx=0,ny=0;
    std::array<double,3> centroid{};
    std::array<bool,3> valid{};
    std::vector<double> mtf;
    double green_variance = 0;
    size_t samples=0,frame=0;
};
inline bool curve(const std::vector<double>& lsf, double spacing,
                  std::vector<double>& mtf, bool derivative) {
    double sum=std::accumulate(lsf.begin(),lsf.end(),0.0);
    if(!(sum>1e-9))return false;
    mtf.resize(129);
    for(int f=0;f<=128;++f){
        double re=0,im=0,omega=2*pi*(f/128.0)*spacing;
        for(size_t k=0;k<lsf.size();++k){
            re+=lsf[k]*std::cos(omega*k);im-=lsf[k]*std::sin(omega*k);
        }
        double correction=(derivative&&f)?omega/std::sin(omega):1;
        mtf[f]=std::hypot(re,im)/sum*correction;
    }
    return true;
}
inline Metric crossing(const std::vector<double>& a,double level,size_t n) {
    for(size_t i=1;i<a.size();++i)if(a[i]<=level&&a[i-1]>level)
        return measured((double(i-1)+(a[i-1]-level)/(a[i-1]-a[i]))/128.0,n);
    Metric m;m.samples=n;m.status="outside_measured_frequency_range";return m;
}
inline bool edge_at(const Input& in,const Options& opt,size_t f,int cx,int cy,Edge& e) {
    const int wh=in.W1/2,hh=in.H1/2;
    if(cx<8||cy<8||cx>=wh-8||cy>=hh-8)return false;
    const auto& g=in.g_halfs[f];
    double xx=0,xy=0,yy=0,sx=0,sy=0,sw=0;
    for(int y=cy-4;y<=cy+4;++y)for(int x=cx-4;x<=cx+4;++x){
        double dx=g[size_t(y)*wh+x+1]-g[size_t(y)*wh+x-1];
        double dy=g[size_t(y+1)*wh+x]-g[size_t(y-1)*wh+x];
        if(!std::isfinite(dx)||!std::isfinite(dy))return false;
        double w=dx*dx+dy*dy;xx+=dx*dx;xy+=dx*dy;yy+=dy*dy;
        sx+=w*(2*x+0.5);sy+=w*(2*y+0.5);sw+=w;
    }
    if(sw<std::max(1e-5,260.0*opt.noise_sigma*opt.noise_sigma))return false;
    double anis=std::hypot(xx-yy,2*xy)/(xx+yy);
    if(anis<0.78)return false;
    double angle=0.5*std::atan2(2*xy,xx-yy);
    e.nx=std::cos(angle);e.ny=std::sin(angle);e.x=sx/sw;e.y=sy/sw;e.frame=f;
    double abs_nx=std::abs(e.nx),abs_ny=std::abs(e.ny);
    double min_tan=std::min(abs_nx,abs_ny)/std::max(1e-12,std::max(abs_nx,abs_ny));
    if(min_tan<0.030||std::abs(min_tan-1.0)<0.025)return false;
    // The half-green tensor locates the edge; refit the line at actual raw green sites.
    std::vector<double> side_a,side_b;
    for(int y=std::max(0,int(e.y)-20);y<=std::min(in.H1-1,int(e.y)+20);++y)
      for(int x=std::max(0,int(e.x)-20);x<=std::min(in.W1-1,int(e.x)+20);++x){
        if(in.cfa6x6[y%6*6+x%6]!=1)continue;
        double d=(x-e.x)*e.nx+(y-e.y)*e.ny;
        double t=-(x-e.x)*e.ny+(y-e.y)*e.nx;
        if(std::abs(t)>16)continue;
        double v=in.frames_lin[f][size_t(y)*in.W1+x];
        if(!std::isfinite(v))return false;
        if(d<-4.5&&d>-10.5)side_a.push_back(v);
        if(d>4.5&&d<10.5)side_b.push_back(v);
    }
    if(side_a.size()<8||side_b.size()<8)return false;
    double med_a=median(side_a),med_b=median(side_b);
    double g_step=std::abs(med_b-med_a);
    if(g_step<std::max(0.008,8.0*opt.noise_sigma))return false;
    double threshold=(med_a+med_b)*.5;
    bool xnormal=std::abs(e.nx)>std::abs(e.ny);
    double center=xnormal?e.x:e.y,tcenter=xnormal?e.y:e.x;
    double est_slope=xnormal?(-e.ny/e.nx):(-e.nx/e.ny);
    std::vector<std::vector<double>> line_rows;std::vector<double> line_values,line_fit;
    for(int t=int(tcenter)-16;t<=int(tcenter)+16;++t){
        double expected_c=center+est_slope*(t-tcenter);
        double previous=0;int previous_k=-999999;
        bool found_best=false;double best_cross=0,best_dist=1e9;
        for(int k=int(expected_c)-9;k<=int(expected_c)+9;++k){
            int x=xnormal?k:t,y=xnormal?t:k;
            if(x<0||y<0||x>=in.W1||y>=in.H1)continue;
            if(in.cfa6x6[y%6*6+x%6]!=1)continue;
            double v=in.frames_lin[f][size_t(y)*in.W1+x]-threshold;
            if(previous_k>-900000&&v*previous<=0&&std::abs(v-previous)>std::max(opt.noise_sigma,0.08*g_step)){
                double crossing=previous_k+(k-previous_k)*(-previous)/(v-previous);
                double dist=std::abs(crossing-expected_c);
                if(dist<best_dist){best_dist=dist;best_cross=crossing;found_best=true;}
            }
            previous=v;previous_k=k;
        }
        if(found_best){
            line_rows.push_back({1.0,t-tcenter});
            line_values.push_back(best_cross-center);
        }
    }
    double line_rms=0;
    if(line_rows.size()<12||!robust_fit(line_rows,line_values,line_fit,line_rms)||line_rms>0.65)return false;
    double norm=std::hypot(1.0,line_fit[1]);
    if(xnormal){e.x=center+line_fit[0];e.nx=1/norm;e.ny=-line_fit[1]/norm;}
    else{e.y=center+line_fit[0];e.nx=-line_fit[1]/norm;e.ny=1/norm;}
    abs_nx=std::abs(e.nx);abs_ny=std::abs(e.ny);
    min_tan=std::min(abs_nx,abs_ny)/std::max(1e-12,std::max(abs_nx,abs_ny));
    if(min_tan<0.028||std::abs(min_tan-1.0)<0.022)return false;
    constexpr int bins=160;
    struct RawSample { double d,t,v; };
    std::array<std::vector<RawSample>,3> all_s,low_s,high_s;
    for(int y=std::max(0,int(e.y)-22);y<=std::min(in.H1-1,int(e.y)+22);++y)
      for(int x=std::max(0,int(e.x)-22);x<=std::min(in.W1-1,int(e.x)+22);++x){
        double d=(x-e.x)*e.nx+(y-e.y)*e.ny;
        double t=-(x-e.x)*e.ny+(y-e.y)*e.nx;
        if(std::abs(t)>16||std::abs(d)>=10)continue;
        double v=in.frames_lin[f][size_t(y)*in.W1+x];
        if(!std::isfinite(v)||v<-0.05||v>0.96)return false;
        int c=in.cfa6x6[(y%6)*6+x%6];
        all_s[c].push_back({d,t,v});
        if(d<-5.0)low_s[c].push_back({d,t,v});
        if(d>5.0)high_s[c].push_back({d,t,v});
        ++e.samples;
    }
    for(int c=0;c<3;++c){
        if(low_s[c].size()<8||high_s[c].size()<8)continue;
        std::vector<double> lv,hv;
        lv.reserve(low_s[c].size());hv.reserve(high_s[c].size());
        for(const auto& s:low_s[c])lv.push_back(s.v);
        for(const auto& s:high_s[c])hv.push_back(s.v);
        double lo=median(lv),hi=median(hv),contrast=hi-lo;
        // Detrend smooth illumination gradient along t/d on both shoulders before estimating noise
        double t_slope_lo=0,t_slope_hi=0;
        auto side_dev=[&](const std::vector<RawSample>& ss,double med_v,double& t_slope_out,std::vector<double>& dev_out){
            std::vector<std::vector<double>> srows;std::vector<double> svals,sfit;double srms=0;
            for(const auto& s:ss){srows.push_back({1.0,s.d,s.t});svals.push_back(s.v);}
            if(ss.size()>=8&&robust_fit(srows,svals,sfit,srms)){
                t_slope_out=sfit[2];
                for(const auto& s:ss)dev_out.push_back(std::abs(s.v-(sfit[0]+sfit[1]*s.d+sfit[2]*s.t)));
            }else{
                t_slope_out=0;
                for(const auto& s:ss)dev_out.push_back(std::abs(s.v-med_v));
            }
        };
        std::vector<double> dev;
        side_dev(low_s[c],lo,t_slope_lo,dev);
        side_dev(high_s[c],hi,t_slope_hi,dev);
        double noise=std::max(opt.noise_sigma,1.4826*median(dev));
        if(std::abs(contrast)<(c==1?10.0:8.0)*noise)continue;
        double t_slope=0.5*(t_slope_lo+t_slope_hi);

        std::vector<double> sum(bins,0),count(bins,0);
        for(const auto& s:all_s[c]){
            int b=int((s.d+10)*8);
            if(b<0||b>=bins)continue;
            sum[b]+=(s.v-t_slope*s.t);
            count[b]+=1;
        }
        std::vector<double> lv_c,hv_c;
        for(const auto& s:low_s[c])lv_c.push_back(s.v-t_slope*s.t);
        for(const auto& s:high_s[c])hv_c.push_back(s.v-t_slope*s.t);
        lo=median(lv_c);hi=median(hv_c);contrast=hi-lo;
        if(std::abs(contrast)<(c==1?10.0:8.0)*noise)continue;

        int occupied=0;
        std::vector<double> esf(bins,0);
        for(int k=0;k<bins;++k)if(count[k]>0){esf[k]=(sum[k]/count[k]-lo)/contrast;++occupied;}
        if(occupied<(c==1?int(bins*0.45):24))continue;
        // Bridge CFA lattice gaps: <= 8/8 px (1.0 px) on dense green MTF, <= 24/8 px (3.0 px) on sparse Bayer/X-Trans R/B centroids.
        const int max_gap=(c==1)?8:24;
        bool gaps=false;
        for(int k=0;k<bins;){
            if(count[k]){++k;continue;}
            int a=k-1;while(k<bins&&!count[k])++k;
            if(a<0){for(int j=0;j<k;++j)esf[j]=0;continue;}
            if(k==bins){for(int j=a+1;j<bins;++j)esf[j]=1;break;}
            if(k-a>max_gap){gaps=true;break;}
            for(int j=a+1;j<k;++j)esf[j]=esf[a]+(esf[k]-esf[a])*(j-a)/(k-a);
        }
        if(gaps)continue;

        // Evaluate macroscopic shoulder stability & step monotonicity at 1-raw-pixel (8-bin) scale
        // so 1/8-px single-sample photon shot noise does not false-reject clean camera edges.
        double esf_1px[20]={};
        for(int m=0;m<20;++m){
            double acc=0;
            for(int j=0;j<8;++j)acc+=esf[m*8+j];
            esf_1px[m]=acc*0.125;
        }
        double shoulder_drift=std::abs(esf_1px[1]-esf_1px[3])+std::abs(esf_1px[16]-esf_1px[18]);
        if(shoulder_drift>(c==1?0.16:0.22))continue;
        double mass_1px=esf_1px[18]-esf_1px[1],neg_1px=0;
        for(int m=1;m<18;++m){
            double dv=esf_1px[m+1]-esf_1px[m];
            if(dv<0)neg_1px-=dv;
        }
        if(mass_1px<0.76||mass_1px>1.24||neg_1px>(c==1?0.24:0.30))continue;

        // Smooth R/B ESF with a 5-tap (0.5-px) binomial kernel before centroiding; keep Green raw 8x for MTF.
        std::vector<double> esf_c=esf;
        if(c!=1){
            for(int k=2;k<bins-2;++k)
                esf_c[k]=(esf[k-2]+4*esf[k-1]+6*esf[k]+4*esf[k+1]+esf[k+2])/16.0;
        }
        std::vector<double> lsf(bins-2);
        double wmass_c=0,wmoment_c=0;
        for(int k=1;k<bins-1;++k){
            double v=(esf_c[k+1]-esf_c[k-1])*0.5;
            lsf[k-1]=v;
            double d=(k+0.5)/8.0-10.0;
            if(std::abs(d)<=6.5){
                double cw=(std::abs(d)<=4.5)?1.0:0.5*(1.0+std::cos(pi*(std::abs(d)-4.5)/2.0));
                wmass_c+=cw*v;
                wmoment_c+=cw*v*d;
            }
        }
        if(wmass_c<0.72||wmass_c>1.28)continue;
        e.centroid[c]=wmoment_c/wmass_c;
        if(std::abs(e.centroid[c])>3.5)continue;
        e.valid[c]=true;
        if(c==1){
            // ISO 12233 Tukey window around the measured centroid suppresses flat-shoulder shot noise in 8x MTF
            std::vector<double> lsf_win(bins-2,0.0);
            double variance=0,wmass=0;
            for(int k=1;k<bins-1;++k){
                double d=(k+.5)/8.0-10-e.centroid[c];
                double ad=std::abs(d);
                if(ad<=7.5){
                    double tuk=(ad<=4.5)?1.0:0.5*(1.0+std::cos(pi*(ad-4.5)/3.0));
                    lsf_win[k-1]=lsf[k-1]*tuk;
                }
                if(ad<=5.5){
                    double win=0.5*(1.0+std::cos(pi*d/5.5));
                    variance+=win*lsf[k-1]*d*d;
                    wmass+=win*lsf[k-1];
                }
            }
            if(!curve(lsf_win,0.125,e.mtf,true)){e.valid[1]=false;continue;}
            e.green_variance=wmass>0.25?variance/wmass:0;
        }
    }
    return e.valid[1];
}
inline void telemetry(const Input& in,OpticsReport& r) {
    const int w=in.W1/2,h=in.H1/2;
    std::vector<std::array<double,2>> positions;
    std::vector<double> speeds;
    bool times=in.time_seconds.size()==r.frames;
    for(size_t n=0;n<r.frames&&times;++n)
        times=std::isfinite(in.time_seconds[n]);
    // Time stamps can follow a reference-frame reorder; individual dt is used.
    for(size_t n=0;n<r.frames;++n){
        Drift d;std::vector<double>xs,ys;
        std::vector<std::vector<double>> rows;std::vector<double> xv,yv;
        int step=std::max(1,int(std::sqrt(double(w)*h/4096)));
        for(int y=0;y<h;y+=step)for(int x=0;x<w;x+=step){
            size_t i=size_t(y)*w+x;
            double dx=in.flows_fx[n][i]*2,dy=in.flows_fy[n][i]*2;
            if(in.mw_half[n][i]<0.7f||!std::isfinite(in.mw_half[n][i])||!std::isfinite(dx)||!std::isfinite(dy))continue;
            // Require local texture to make the flow observable.
            if(x<1||x>=w-1||y<1||y>=h-1)continue;
            double gx=in.g_halfs[0][i+1]-in.g_halfs[0][i-1];
            double gy=in.g_halfs[0][i+w]-in.g_halfs[0][i-w];
            if(!std::isfinite(gx)||!std::isfinite(gy)||std::hypot(gx,gy)<0.002)continue;
            xs.push_back(dx);ys.push_back(dy);
            rows.push_back({1.0,(double(y)*2-in.H1*0.5)/in.H1});xv.push_back(dx);yv.push_back(dy);
        }
        if(xs.size()>=12){
            d.dx=measured(median(xs),xs.size());d.dy=measured(median(ys),ys.size());
            positions.push_back({d.dx.value,d.dy.value});
            double rms=0;std::vector<double>fit;
            if(robust_fit(rows,xv,fit,rms))d.row_shear_x=measured(fit[1]/in.H1,xs.size(),"flow_row_slope_px_per_row");
            if(robust_fit(rows,yv,fit,rms))d.row_shear_y=measured(fit[1]/in.H1,ys.size(),"flow_row_slope_px_per_row");
            double px=d.dx.value-std::floor(d.dx.value),py=d.dy.value-std::floor(d.dy.value);
            ++r.phase_quadrants[(px>=0.5?1:0)+(py>=0.5?2:0)];
        } else {d.dx.status=d.dy.status="insufficient_static_texture";}
        if(times)d.time_seconds=measured(in.time_seconds[n],1,"metadata_seconds");
        else d.time_seconds.status="missing_time_metadata";
        if(n&&times&&d.dx.valid&&r.drift.back().dx.valid){
            double dt=std::abs(in.time_seconds[n]-in.time_seconds[n-1]);
            if(dt>0)d.speed_px_s=measured(std::hypot(d.dx.value-r.drift.back().dx.value,d.dy.value-r.drift.back().dy.value)/dt,2,"interframe_speed");
        }
        r.drift.push_back(d);
    }
    if(positions.size()>=2){
        double span=0,max_reference_drift=0;
        for(auto a:positions)for(auto b:positions)span=std::max(span,std::hypot(a[0]-b[0],a[1]-b[1]));
        for(auto a:positions)max_reference_drift=std::max(max_reference_drift,std::hypot(a[0],a[1]));
        r.drift_span=measured(span,positions.size());
        r.max_drift_1x=measured(max_reference_drift,positions.size());
    }
    r.first_frame_shake_ratio.status="requires_intra_exposure_motion_measurement";
}
inline void calibrate(const Options& opt,OpticsReport& r) {
    if(!r.max_drift_1x.valid||r.max_drift_1x.value<25)return;
    const double d=std::max(r.width,r.height);
    std::vector<std::vector<double>> rows;std::vector<double>values,fit;
    for(auto p:opt.distortion_points){
        double x=(p.ideal_x-r.width*.5)/d,y=(p.ideal_y-r.height*.5)/d,s=x*x+y*y;
        double dx=(p.observed_x-p.ideal_x)/d,dy=(p.observed_y-p.ideal_y)/d;
        if(!std::isfinite(s+dx+dy)||s>1||s<0.0001||
           p.ideal_x<0||p.ideal_y<0||p.ideal_x>=r.width||p.ideal_y>=r.height||
           p.observed_x<0||p.observed_y<0||p.observed_x>=r.width||p.observed_y>=r.height)continue;
        rows.push_back({x*s,x*s*s});values.push_back(dx);
        rows.push_back({y*s,y*s*s});values.push_back(dy);
    }
    r.distortion.samples=rows.size()/2;
    if(robust_fit(rows,values,fit,r.distortion.rms)&&r.distortion.rms*d<0.5){
        r.distortion.valid=true;r.distortion.status="calibrated_target";
        r.distortion.coefficients={fit[0],fit[1],0};
    }else r.distortion.status="insufficient_or_inconsistent_target";
    rows.clear();values.clear();
    for(auto p:opt.flat_points){
        double x=(p.x-r.width*.5)/d,y=(p.y-r.height*.5)/d,s=x*x+y*y;
        if(!std::isfinite(s+p.measured+p.expected)||p.measured<=0.05||p.measured>=0.72||p.expected<=0||s>1||
           p.x<0||p.y<0||p.x>=r.width||p.y>=r.height)continue;
        rows.push_back({s,s*s,s*s*s});values.push_back(p.measured/p.expected-1);
    }
    r.vignette.samples=rows.size();
    r.vignette.rms_unit="relative_radiance";
    if(robust_fit(rows,values,fit,r.vignette.rms)&&r.vignette.rms<0.03){
        bool physical=true;
        for(int i=0;i<=100;++i){
            double s=0.5*i/100.0,v=1+s*(fit[0]+s*(fit[1]+s*fit[2]));
            physical=physical&&v>0.05&&v<2;
        }
        if(physical){r.vignette.valid=true;r.vignette.status="calibrated_flat";std::copy(fit.begin(),fit.end(),r.vignette.coefficients.begin());}
        else r.vignette.status="nonphysical_calibration";
    }else r.vignette.status="insufficient_or_inconsistent_flat";
}
inline double condition2(double xx,double xy,double yy) {
    double trace=xx+yy,disc=std::hypot(xx-yy,2*xy);
    return trace>disc?(trace-disc)/(trace+disc):0;
}
inline void scene_vignette(const Input& in,const Options& opt,const OpticsReport& report,RadialCalibration& out) {
    out.status="insufficient_static_photometric_matches";out.rms_unit="log_radiance_ratio";
    const int w=in.W1/2,h=in.H1/2;
    const double d=std::max(in.W1,in.H1);
    int step=std::max(4,int(std::sqrt(double(w)*h/1600)));
    std::vector<std::vector<double>> rows;std::vector<double> values,fit;
    double xx=0,xy=0,yy=0;
    std::array<bool,4> sectors{};
    for(size_t f=1;f<in.frames_lin.size();++f){
        if(!report.drift[f].dx.valid)continue;
        for(int y=2;y<h-2;y+=step)for(int x=2;x<w-2;x+=step){
            size_t i=size_t(y)*w+x;
            double fx=in.flows_fx[f][i],fy=in.flows_fy[f][i];
            if(!(in.mw_half[f][i]>=.85f)||!std::isfinite(fx+fy))continue;
            double sx=x+fx,sy=y+fy;
            if(sx<2||sy<2||sx>w-3||sy>h-3)continue;
            double a=in.g_halfs[0][i],b=sample(in.g_halfs[f],h,w,sx,sy);
            if(!std::isfinite(a+b)||a<=.05||a>=.72||b<=.05||b>=.72)continue;
            double gx=in.g_halfs[0][i+1]-in.g_halfs[0][i-1],gy=in.g_halfs[0][i+w]-in.g_halfs[0][i-w];
            double bx=sample(in.g_halfs[f],h,w,sx+1,sy)-sample(in.g_halfs[f],h,w,sx-1,sy);
            double by=sample(in.g_halfs[f],h,w,sx,sy+1)-sample(in.g_halfs[f],h,w,sx,sy-1);
            // Interpolated same-point flux ratios use locally smooth, linear,
            // unsaturated samples to suppress CFA/registration interpolation bias.
            if(!std::isfinite(gx+gy+bx+by)||std::hypot(gx,gy)>.04*a||std::hypot(bx,by)>.04*b)continue;
            double rx=(2*x+.5-in.W1*.5)/d,ry=(2*y+.5-in.H1*.5)/d;
            double ux=(2*sx+.5-in.W1*.5)/d,uy=(2*sy+.5-in.H1*.5)/d;
            double r0=rx*rx+ry*ry,r1=ux*ux+uy*uy;
            double v0=r1-r0,v1=r1*r1-r0*r0;
            if(std::abs(v0)<1e-5)continue;
            rows.push_back({v0,v1});values.push_back(std::log(b/a));
            xx+=v0*v0;xy+=v0*v1;yy+=v1*v1;sectors[(rx>=0?1:0)+(ry>=0?2:0)]=true;
        }
    }
    out.samples=rows.size();out.condition_ratio=condition2(xx,xy,yy);
    if(rows.size()<120||std::count(sectors.begin(),sectors.end(),true)<3)return;
    if(out.condition_ratio<.002){out.status="unobservable_radial_photometric_rank";return;}
    if(!robust_fit(rows,values,fit,out.rms)){out.status="unobservable_radial_photometric_rank";return;}
    const double vig_rms_lim=std::max(.025,std::min(.055,3.5*opt.noise_sigma/0.12));
    const double vig_inl_lim=std::max(.038,1.8*vig_rms_lim);
    for(size_t i=0;i<rows.size();++i)
        if(std::abs(values[i]-rows[i][0]*fit[0]-rows[i][1]*fit[1])<vig_inl_lim)++out.inliers;
    if(out.rms>vig_rms_lim||out.inliers<rows.size()*.72){out.status="inconsistent_scene_photometry";return;}
    // Convert exp(a*r^2+b*r^4) to LCP's attenuation polynomial over the active
    // radius, with an explicit approximation-error gate.
    double max_r2=(double(in.W1)*in.W1+double(in.H1)*in.H1)/(4*d*d);
    std::vector<std::vector<double>> poly_rows;std::vector<double> poly_values,poly;
    for(int i=1;i<=100;++i){
        double s=max_r2*i/100.0,v=std::exp(fit[0]*s+fit[1]*s*s);
        if(!std::isfinite(v)||v<.05||v>2){out.status="nonphysical_scene_attenuation";return;}
        poly_rows.push_back({s,s*s,s*s*s});poly_values.push_back(v-1);
    }
    double poly_rms=0;
    if(!robust_fit(poly_rows,poly_values,poly,poly_rms)){out.status="unobservable_radial_photometric_rank";return;}
    for(size_t i=0;i<poly_rows.size();++i){
        double approximation=1+std::inner_product(poly_rows[i].begin(),poly_rows[i].end(),poly.begin(),0.0);
        if(std::abs(approximation/(poly_values[i]+1)-1)>.005){out.status="lcp_polynomial_approximation_limit";return;}
    }
    std::copy(poly.begin(),poly.end(),out.coefficients.begin());
    out.log_coefficients={fit[0],fit[1]};out.log_model_valid=true;out.valid=true;out.status="scene_estimate";
}
struct SceneMatch {double x,y,u,v;};
inline std::array<double,2> undistort(double x,double y,double k1,double k2) {
    double rd=std::hypot(x,y);
    if(rd<1e-12)return {x,y};
    double ru=rd;
    for(int i=0;i<12;++i){
        double r2=ru*ru,den=1+3*k1*r2+5*k2*r2*r2;
        if(den<=.1||!std::isfinite(den))return {NAN,NAN};
        double delta=(ru*(1+k1*r2+k2*r2*r2)-rd)/den;
        ru-=delta;if(ru<0||!std::isfinite(ru))return {NAN,NAN};
        if(std::abs(delta)<1e-12)break;
    }
    double r2=ru*ru;
    if(std::abs(ru*(1+k1*r2+k2*r2*r2)-rd)>1e-8)return {NAN,NAN};
    return {x*ru/rd,y*ru/rd};
}
inline bool scene_residual(const std::vector<std::vector<SceneMatch>>& frames,
                           const std::array<double,2>& k,std::vector<double>& residual) {
    residual.clear();
    for(const auto& frame:frames){
        std::vector<std::vector<double>> rows;std::vector<double> values,homography;
        std::vector<std::array<double,2>> reference;
        for(const auto& m:frame){
            auto a=undistort(m.x,m.y,k[0],k[1]),b=undistort(m.u,m.v,k[0],k[1]);
            if(!std::isfinite(a[0]+a[1]+b[0]+b[1]))return false;
            double x=a[0],y=a[1],u=b[0],v=b[1];reference.push_back(a);
            rows.push_back({x,y,1,0,0,0,-u*x,-u*y});values.push_back(u);
            rows.push_back({0,0,0,x,y,1,-v*x,-v*y});values.push_back(v);
        }
        double rms=0;
        if(!robust_fit(rows,values,homography,rms))return false;
        for(size_t i=0;i<frame.size();++i){
            double x=reference[i][0],y=reference[i][1],den=1+homography[6]*x+homography[7]*y;
            if(std::abs(den)<.1)return false;
            double u=(homography[0]*x+homography[1]*y+homography[2])/den;
            double v=(homography[3]*x+homography[4]*y+homography[5])/den;
            double r2=u*u+v*v,scale=1+k[0]*r2+k[1]*r2*r2;
            if(!std::isfinite(scale)||scale<=.1)return false;
            residual.push_back(u*scale-frame[i].u);residual.push_back(v*scale-frame[i].v);
        }
    }
    return !residual.empty();
}
inline void scene_distortion(const Input& in,const OpticsReport& report,RadialCalibration& out) {
    out.status="insufficient_planar_static_matches";
    const int w=in.W1/2,h=in.H1/2;const double d=std::max(in.W1,in.H1);
    int step=std::max(6,int(std::sqrt(double(w)*h/900)));
    std::vector<std::pair<double,size_t>> candidates;
    for(size_t f=1;f<in.frames_lin.size();++f)if(report.drift[f].dx.valid)
        candidates.push_back({std::hypot(report.drift[f].dx.value,report.drift[f].dy.value),f});
    std::sort(candidates.rbegin(),candidates.rend());
    std::vector<std::vector<SceneMatch>> frames;
    for(auto candidate:candidates){
        if(frames.size()==4)break;
        size_t f=candidate.second;std::vector<SceneMatch> matches;std::array<bool,16> cells{};
        for(int y=2;y<h-2;y+=step)for(int x=2;x<w-2;x+=step){
            size_t i=size_t(y)*w+x;
            double fx=in.flows_fx[f][i],fy=in.flows_fy[f][i],sx=x+fx,sy=y+fy;
            if(!(in.mw_half[f][i]>=.85f)||!std::isfinite(fx+fy)||sx<2||sy<2||sx>w-3||sy>h-3)continue;
            double a=in.g_halfs[0][i],b=sample(in.g_halfs[f],h,w,sx,sy);
            double gx=in.g_halfs[0][i+1]-in.g_halfs[0][i-1],gy=in.g_halfs[0][i+w]-in.g_halfs[0][i-w];
            if(!std::isfinite(a+b+gx+gy)||a<=.05||a>=.72||b<=.05||b>=.72||std::hypot(gx,gy)<.002)continue;
            matches.push_back({(2*x+.5-in.W1*.5)/d,(2*y+.5-in.H1*.5)/d,
                               (2*sx+.5-in.W1*.5)/d,(2*sy+.5-in.H1*.5)/d});
            cells[std::min(3,y*4/h)*4+std::min(3,x*4/w)]=true;
        }
        if(matches.size()>=120&&std::count(cells.begin(),cells.end(),true)>=10){
            out.samples+=matches.size();frames.push_back(std::move(matches));
        }
    }
    if(frames.empty())return;
    // Separable nonlinear least squares jointly estimates k1/k2 and one full
    // projective homography per frame. Re-solving H at every k evaluation
    // marginalizes its eight nuisance parameters in the finite-difference J.
    // Brown radial and projective equations: OpenCV calib3d.hpp documentation.
    std::array<double,2> k{};
    std::vector<double> residual;
    if(!scene_residual(frames,k,residual)){out.status="unobservable_homography_rank";return;}
    double information_xx=0,information_xy=0,information_yy=0;
    for(int iteration=0;iteration<18;++iteration){
        constexpr double epsilon=1e-5;
        auto kx=k,ky=k;kx[0]+=epsilon;ky[1]+=epsilon;
        std::vector<double> rx,ry;
        if(!scene_residual(frames,kx,rx)||!scene_residual(frames,ky,ry)){out.status="noninvertible_radial_model";return;}
        std::vector<double> absolute;
        for(size_t i=0;i<residual.size();i+=2)absolute.push_back(std::hypot(residual[i],residual[i+1]));
        double cutoff=std::max(.08/d,2.5*median(absolute));
        double xx=0,xy=0,yy=0,bx=0,by=0,loss=0;
        std::vector<double> weights(residual.size());
        for(size_t i=0;i<residual.size();++i){
            double weight=std::min(1.0,cutoff/std::max(1e-15,absolute[i/2]));
            double jx=(rx[i]-residual[i])/epsilon,jy=(ry[i]-residual[i])/epsilon;
            xx+=weight*jx*jx;xy+=weight*jx*jy;yy+=weight*jy*jy;
            bx-=weight*jx*residual[i];by-=weight*jy*residual[i];
            loss+=weight*residual[i]*residual[i];weights[i]=weight;
        }
        information_xx=xx;information_xy=xy;information_yy=yy;
        out.condition_ratio=condition2(xx,xy,yy);
        double det=xx*yy-xy*xy;
        if(out.condition_ratio<1e-5||det<1e-20){out.status="unobservable_radial_homography_rank";return;}
        double dx=(yy*bx-xy*by)/det,dy=(xx*by-xy*bx)/det;
        if(std::hypot(dx,dy)<1e-7)break;
        bool advanced=false;
        for(double rate=1;rate>=1.0/128;rate*=.5){
            std::array<double,2> candidate={k[0]+rate*dx,k[1]+rate*dy};
            if(std::abs(candidate[0])>4||std::abs(candidate[1])>8)continue;
            std::vector<double> next;
            if(!scene_residual(frames,candidate,next))continue;
            double next_loss=0;
            for(size_t i=0;i<next.size();++i)next_loss+=weights[i]*next[i]*next[i];
            if(next_loss<loss){
                k=candidate;residual=std::move(next);advanced=true;break;
            }
        }
        if(!advanced)break;
    }
    double sum=0;
    for(size_t i=0;i<residual.size();i+=2){
        double error=std::hypot(residual[i],residual[i+1]);
        if(error*d<=.75){sum+=error*error;++out.inliers;}
    }
    out.rms=out.inliers?std::sqrt(sum/out.inliers):0;
    if(out.inliers<out.samples*.72||out.rms*d>.62){out.status="inconsistent_planar_geometry";return;}
    // Unregularized, homography-marginalized information, with a registration
    // noise floor: even a tiny fitted residual must pass identifiability.
    double det=information_xx*information_yy-information_xy*information_xy;
    double noise=std::max(.04/d,out.rms);
    if(det<=0||noise*std::sqrt(information_yy/det)>.08||noise*std::sqrt(information_xx/det)>.35){
        out.status="weak_radial_homography_conditioning";return;
    }
    double max_r2=(double(in.W1)*in.W1+double(in.H1)*in.H1)/(4*d*d);
    for(int i=0;i<=100;++i){
        double s=max_r2*i/100.0;
        if(1+k[0]*s+k[1]*s*s<=.2||1+3*k[0]*s+5*k[1]*s*s<=.2){
            out.status="noninvertible_radial_model";return;
        }
    }
    out.coefficients={k[0],k[1],0};out.valid=true;out.status="scene_estimate";
}
inline void scene_calibrate(const Input& in,const Options& opt,OpticsReport& report) {
    if(!report.max_drift_1x.valid||report.max_drift_1x.value<25)return;
    if(opt.flat_points.empty())scene_vignette(in,opt,report,report.vignette);
    if(opt.distortion_points.empty())scene_distortion(in,report,report.distortion);
}
struct StarAtlas {
    std::vector<double> sum=std::vector<double>(97*97),weight=std::vector<double>(97*97);
    size_t stars=0,samples=0;
};
inline bool star_at(const Input& in,const Options& opt,size_t f,int cx,int cy,StarAtlas& atlas) {
    double x0=cx*2+0.5,y0=cy*2+0.5;
    std::vector<double> background;
    double peak=0;
    for(int y=int(y0)-8;y<=int(y0)+8;++y)for(int x=int(x0)-8;x<=int(x0)+8;++x){
        double v=in.frames_lin[f][size_t(y)*in.W1+x];
        if(!std::isfinite(v)||v>=0.72)return false;
        if(in.cfa6x6[y%6*6+x%6]!=1)continue;
        if(std::hypot(x-x0,y-y0)>6)background.push_back(v);
        peak=std::max(peak,v);
    }
    double bg=median(background);
    std::vector<double> dev;for(double v:background)dev.push_back(std::abs(v-bg));
    double noise=std::max(opt.noise_sigma,median(dev)*1.4826);
    if(peak-bg<18*noise||peak<std::max(0.008,12.0*opt.noise_sigma))return false;
    std::vector<std::vector<double>> rows;std::vector<double>values,fit;
    for(int y=int(y0)-5;y<=int(y0)+5;++y)for(int x=int(x0)-5;x<=int(x0)+5;++x){
        if(in.cfa6x6[y%6*6+x%6]!=1)continue;
        double v=in.frames_lin[f][size_t(y)*in.W1+x]-bg;
        if(v<4*noise)continue;
        double xx=x-x0,yy=y-y0;
        rows.push_back({1,xx,yy,xx*xx,xx*yy,yy*yy});values.push_back(std::log(v));
    }
    double rms=0;
    if(!robust_fit(rows,values,fit,rms)||rms>0.25)return false;
    double a=2*fit[3],b=fit[4],d=2*fit[5],det=a*d-b*b;
    if(a>=0||d>=0||det<1e-6)return false;
    double dx=(-d*fit[1]+b*fit[2])/det,dy=(b*fit[1]-a*fit[2])/det;
    if(std::hypot(dx,dy)>2.5)return false;
    double variance=-(a+d)/det;
    if(variance<0.5||variance>12)return false;
    double center_x=x0+dx,center_y=y0+dy;
    double amp=std::exp(fit[0]+fit[1]*dx+fit[2]*dy+fit[3]*dx*dx+fit[4]*dx*dy+fit[5]*dy*dy);
    if(!(amp>0)||amp+bg>=0.72)return false;
    // Scattered, un-demosaiced green samples. Gaussian kernel bandwidth 0.16
    // raw pixel is recorded by the method name; coordinates are on an 8x grid.
    for(int y=int(y0)-7;y<=int(y0)+7;++y)for(int x=int(x0)-7;x<=int(x0)+7;++x){
        if(in.cfa6x6[y%6*6+x%6]!=1)continue;
        double xx=(x-center_x)*8+48,yy=(y-center_y)*8+48;
        if(xx<0||yy<0||xx>=97||yy>=97)continue;
        double v=(in.frames_lin[f][size_t(y)*in.W1+x]-bg)/amp;
        for(int gy=std::max(0,int(yy)-3);gy<=std::min(96,int(yy)+3);++gy)
          for(int gx=std::max(0,int(xx)-3);gx<=std::min(96,int(xx)+3);++gx){
            double r2=(gx-xx)*(gx-xx)+(gy-yy)*(gy-yy);
            if(r2>9)continue;
            double w=std::exp(-r2/(2*1.28*1.28));
            atlas.sum[size_t(gy)*97+gx]+=v*w;atlas.weight[size_t(gy)*97+gx]+=w;
        }
        ++atlas.samples;
    }
    ++atlas.stars;return true;
}
inline bool finish_atlas(const StarAtlas& a,Zone& z) {
    z.stars=a.stars;z.raw_samples+=a.samples;
    size_t occupied=0,total=0;
    for(int y=0;y<97;++y)for(int x=0;x<97;++x)if(std::hypot(x-48,y-48)<=24){
        ++total;if(a.weight[size_t(y)*97+x]>0.01)++occupied;
    }
    z.epsf_coverage=double(occupied)/std::max<size_t>(1,total);
    if(a.stars<8||z.epsf_coverage<0.85){z.status="insufficient_star_phase_coverage";return false;}
    z.epsf.resize(97*97);
    double mass=0;
    for(size_t i=0;i<z.epsf.size();++i){
        z.epsf[i]=float(a.weight[i]>0.01?std::max(0.0,a.sum[i]/a.weight[i]):0);
        mass+=z.epsf[i];
    }
    if(!(mass>0)){z.epsf.clear();return false;}
    double mx=0,my=0;
    for(int y=0;y<97;++y)for(int x=0;x<97;++x){
        size_t i=size_t(y)*97+x;z.epsf[i]=float(z.epsf[i]/mass);
        mx+=z.epsf[i]*(x-48)/8.0;my+=z.epsf[i]*(y-48)/8.0;
    }
    double xx=0,xy=0,yy=0;
    std::vector<double> px(97),py(97);
    for(int y=0;y<97;++y)for(int x=0;x<97;++x){
        double v=z.epsf[size_t(y)*97+x],dx=(x-48)/8.0-mx,dy=(y-48)/8.0-my;
        xx+=v*dx*dx;xy+=v*dx*dy;yy+=v*dy*dy;px[x]+=v;py[y]+=v;
    }
    double disc=std::hypot(xx-yy,2*xy);
    z.fwhm_major=measured(2.354820045*std::sqrt(std::max(0.0,(xx+yy+disc)*.5)),a.stars,"moment_equivalent_fwhm");
    z.fwhm_minor=measured(2.354820045*std::sqrt(std::max(0.0,(xx+yy-disc)*.5)),a.stars,"moment_equivalent_fwhm");
    if(disc>0.01*(xx+yy))z.angle_deg=measured(std::atan2(2*xy,xx-yy)*90/pi,a.stars);
    else z.angle_deg.status="isotropic_psf";
    std::vector<double> mtx,mty;curve(px,.125,mtx,false);curve(py,.125,mty,false);
    z.mtf.resize(129);
    for(int k=0;k<129;++k)z.mtf[k]=(mtx[k]+mty[k])*0.5;
    z.mtf50=crossing(z.mtf,.5,a.stars);z.mtf10=crossing(z.mtf,.1,a.stars);
    z.method="raw_green_8x_stellar_epsf_kernel_sigma_0.16px";z.status="stellar_effective_psf";
    return true;
}
inline void stars(const Input& in,const Options& opt,OpticsReport& r) {
    if(!opt.stars||opt.max_stars==0)return;
    std::array<StarAtlas,5> combined;
    std::array<Zone,5> best;
    int w=in.W1/2,h=in.H1/2;
    const double min_peak=std::max(0.008,12.0*opt.noise_sigma);
    const size_t zone_cap=std::max<size_t>(24,(opt.max_stars+2)/3);
    struct StarCand { double v; int x, y; };
    for(size_t f=0;f<r.frames;++f){
        std::array<StarAtlas,5> frame;
        std::array<std::vector<StarCand>,5> zcands;
        const auto&g=in.g_halfs[f];
        for(int y=6;y<h-6;++y)for(int x=6;x<w-6;++x){
            double v=g[size_t(y)*w+x];
            if(!std::isfinite(v)||v<min_peak||v>=0.72)continue;
            bool maximum=true;
            for(int dy=-2;dy<=2&&maximum;++dy)for(int dx=-2;dx<=2;++dx)
                if((dx||dy)&&g[size_t(y+dy)*w+x+dx]>=v){maximum=false;break;}
            if(!maximum)continue;
            zcands[zone(x*2,y*2,in.W1,in.H1)].push_back({v,x,y});
        }
        size_t total_accepted=0;
        for(int z=0;z<5;++z){
            auto& cz=zcands[z];
            std::sort(cz.begin(),cz.end(),[](const StarCand& a,const StarCand& b){return a.v>b.v;});
            size_t z_acc=0;
            for(const auto& sc:cz){
                if(z_acc>=zone_cap||total_accepted>=opt.max_stars*2)break;
                if(star_at(in,opt,f,sc.x,sc.y,opt.ois?frame[z]:combined[z])){++z_acc;++total_accepted;}
            }
        }
        if(opt.ois)for(int z=0;z<5;++z){
            Zone q;
            if(finish_atlas(frame[z],q)&&q.mtf50.valid&&
               (!best[z].mtf50.valid||q.mtf50.value>best[z].mtf50.value))best[z]=std::move(q);
        }
    }
    for(int z=0;z<5;++z){
        if(opt.ois){
            if(best[z].mtf50.valid){
                std::string name=r.zones[z].name;r.zones[z]=std::move(best[z]);r.zones[z].name=name;
                r.zones[z].status="stellar_ois_sharpest_frame";
            }
        }else finish_atlas(combined[z],r.zones[z]);
    }
}
struct FlowHomography {
    bool valid=false,projective=false;
    std::array<double,8> h{{1,0,0,0,1,0,0,0}};
    std::array<double,2> predict_raw(double x_raw,double y_raw,int W1,int H1) const {
        const double d=std::max(W1,H1);
        const double xn=(x_raw-W1*.5)/d,yn=(y_raw-H1*.5)/d;
        if(projective){
            const double den=1+h[6]*xn+h[7]*yn;
            if(std::abs(den)<0.1)return {NAN,NAN};
            const double un=(h[0]*xn+h[1]*yn+h[2])/den;
            const double vn=(h[3]*xn+h[4]*yn+h[5])/den;
            return {(un-xn)*d,(vn-yn)*d};
        }
        return {h[0]+h[1]*(x_raw/W1)+h[2]*(y_raw/H1),
                h[3]+h[4]*(x_raw/W1)+h[5]*(y_raw/H1)};
    }
};
inline std::vector<FlowHomography> flow_models(const Input& in) {
    std::vector<FlowHomography> models(in.frames_lin.size());
    const int w=in.W1/2,h=in.H1/2;
    const double d=std::max(in.W1,in.H1);
    for(size_t f=0;f<models.size();++f){
        if(f==0){models[0].valid=true;models[0].projective=true;continue;}
        std::vector<std::vector<double>> arows,prows;
        std::vector<double> vx,vy,pvals,fit;
        int step=std::max(1,int(std::sqrt(double(w)*h/2048)));
        for(int y=1;y<h-1;y+=step)for(int x=1;x<w-1;x+=step){
            size_t i=size_t(y)*w+x;
            double dx=in.flows_fx[f][i]*2,dy=in.flows_fy[f][i]*2;
            if(!(in.mw_half[f][i]>=.75f)||!std::isfinite(dx+dy))continue;
            double gx=in.g_halfs[0][i+1]-in.g_halfs[0][i-1],gy=in.g_halfs[0][i+w]-in.g_halfs[0][i-w];
            if(!std::isfinite(gx+gy)||std::hypot(gx,gy)<.002)continue;
            arows.push_back({1.0,double(2*x+.5)/in.W1,double(2*y+.5)/in.H1});
            vx.push_back(dx);vy.push_back(dy);
            const double xn=(2*x+.5-in.W1*.5)/d,yn=(2*y+.5-in.H1*.5)/d;
            const double un=xn+dx/d,vn=yn+dy/d;
            prows.push_back({xn,yn,1,0,0,0,-un*xn,-un*yn});pvals.push_back(un);
            prows.push_back({0,0,0,xn,yn,1,-vn*xn,-vn*yn});pvals.push_back(vn);
        }
        double rms=0;
        if(prows.size()>=24&&robust_fit(prows,pvals,fit,rms)){
            std::copy(fit.begin(),fit.end(),models[f].h.begin());
            models[f].valid=true;models[f].projective=true;
        }else{
            std::vector<double> fx_fit,fy_fit;
            if(robust_fit(arows,vx,fx_fit,rms)&&robust_fit(arows,vy,fy_fit,rms)){
                std::copy(fx_fit.begin(),fx_fit.end(),models[f].h.begin());
                std::copy(fy_fit.begin(),fy_fit.end(),models[f].h.begin()+3);
                models[f].valid=true;models[f].projective=false;
            }
        }
    }
    return models;
}
inline bool static_far_edge(const Input& in,const Edge& e,
                            const std::vector<FlowHomography>& models,const OpticsReport& r) {
    const size_t n=in.frames_lin.size();
    if(n<2)return false;
    double x=e.x,y=e.y;
    if(e.frame){
        if(!models[e.frame].valid&&!r.drift[e.frame].dx.valid)return false;
        double rx=r.drift[e.frame].dx.valid?e.x-r.drift[e.frame].dx.value:e.x;
        double ry=r.drift[e.frame].dy.valid?e.y-r.drift[e.frame].dy.value:e.y;
        if(models[e.frame].valid){
            for(int it=0;it<3;++it){
                auto pred=models[e.frame].predict_raw(rx,ry,in.W1,in.H1);
                if(!std::isfinite(pred[0]+pred[1]))break;
                rx=e.x-pred[0];ry=e.y-pred[1];
            }
        }
        x=rx;y=ry;
        if(x<4||y<4||x>in.W1-5||y>in.H1-5)return false;
    }
    const size_t min_nonref=std::max<size_t>(1,(n-1+1)/2);
    size_t nonref_pass=0;
    for(size_t f=0;f<n;++f){
        if(!models[f].valid){if(f==0||f==e.frame)return false;continue;}
        auto pred=models[f].predict_raw(x,y,in.W1,in.H1);
        double fx=sample(in.flows_fx[f],in.H1/2,in.W1/2,x*.5,y*.5)*2;
        double fy=sample(in.flows_fy[f],in.H1/2,in.W1/2,x*.5,y*.5)*2;
        double mw=sample(in.mw_half[f],in.H1/2,in.W1/2,x*.5,y*.5);
        double dx=fx-pred[0],dy=fy-pred[1];
        const bool ok=std::isfinite(dx+dy+mw)&&mw>=.70&&std::hypot(dx,dy)<=.55;
        if(f==0||f==e.frame){if(!ok)return false;}
        if(f>0&&ok)++nonref_pass;
    }
    return nonref_pass>=min_nonref;
}
} // namespace detail

inline OpticsReport analyze(const Input& in,const Options& opt = {}) {
    detail::validate(in);
    if(!(opt.noise_sigma>0)||!std::isfinite(opt.noise_sigma))throw std::invalid_argument("optics noise_sigma");
    OpticsReport r;r.width=in.W1;r.height=in.H1;r.frames=in.frames_lin.size();
    r.camera=opt.camera;r.lens=opt.lens;r.focal_mm=opt.focal_mm;r.aperture=opt.aperture;
    r.focus_m=opt.focus_m;r.crop_factor=opt.crop_factor;r.ois=opt.ois;
    r.lca.width=in.W1;r.lca.height=in.H1;
    const char* names[]={"center","top_left","top_right","bottom_left","bottom_right"};
    for(int i=0;i<5;++i)r.zones[i].name=names[i];
    if(in.frames_lin.empty())return r;
    r.status="analyzed";
    detail::telemetry(in,r);
    auto flow_models=detail::flow_models(in);
    std::vector<detail::Edge> edges;
    const int w=in.W1/2,h=in.H1/2;
    if(opt.max_edges>0&&w>=20&&h>=20){
        struct EdgeCand { double g2; int x,y; };
        const int cell=std::max(8,int(std::sqrt(double(w)*h/std::max<size_t>(1,opt.max_edges*6))));
        const size_t zone_cap=std::max<size_t>(24,(opt.max_edges+2)/3);
        const double min_g2=std::max(1e-7,16.0*opt.noise_sigma*opt.noise_sigma);
        for(size_t f=0;f<r.frames;++f){
            const auto& g=in.g_halfs[f];
            std::array<std::vector<EdgeCand>,5> zcands;
            for(int y0=9;y0<h-9;y0+=cell){
                const int y1=std::min(h-9,y0+cell);
                for(int x0=9;x0<w-9;x0+=cell){
                    const int x1=std::min(w-9,x0+cell);
                    double best_g2=0;int bx=-1,by=-1;
                    const int stride=(cell>=28)?2:1;
                    for(int y=y0;y<y1;y+=stride)for(int x=x0;x<x1;x+=stride){
                        double dx=g[size_t(y)*w+x+1]-g[size_t(y)*w+x-1];
                        double dy=g[size_t(y+1)*w+x]-g[size_t(y-1)*w+x];
                        if(!std::isfinite(dx+dy))continue;
                        double g2=dx*dx+dy*dy;
                        if(g2<=best_g2||g2<min_g2)continue;
                        double ax=std::abs(dx),ay=std::abs(dy);
                        double mt=std::min(ax,ay)/std::max(1e-12,std::max(ax,ay));
                        if(mt<0.04||std::abs(mt-1.0)<0.03)continue;
                        best_g2=g2;bx=x;by=y;
                    }
                    if(bx>=0)zcands[detail::zone(bx*2,by*2,in.W1,in.H1)].push_back({best_g2,bx,by});
                }
            }
            size_t frame_edges=0;
            for(int z=0;z<5;++z){
                auto& cz=zcands[z];
                std::sort(cz.begin(),cz.end(),[](const EdgeCand& a,const EdgeCand& b){return a.g2>b.g2;});
                size_t z_acc=0;
                for(const auto& ec:cz){
                    if(z_acc>=zone_cap||frame_edges>=opt.max_edges*2)break;
                    detail::Edge e;
                    if(detail::edge_at(in,opt,f,ec.x,ec.y,e)){edges.push_back(e);++z_acc;++frame_edges;}
                }
            }
        }
    }
    for(int c=0;c<2;++c){
        int channel=c?2:0;
        auto try_lca_fit=[&](bool ref_only)->bool{
            std::vector<std::vector<double>> rows;std::vector<double>values,fit;
            for(const auto&e:edges)if((!ref_only||e.frame==0)&&e.valid[channel]){
                double x=(e.x-r.width*.5)/std::max(r.width,r.height);
                double y=(e.y-r.height*.5)/std::max(r.width,r.height);
                double radial=e.nx*x+e.ny*y;
                rows.push_back({e.nx,e.ny,radial,radial*(x*x+y*y)});
                values.push_back(e.centroid[channel]-e.centroid[1]);
            }
            auto& l=r.lca.channels[c];l.samples=rows.size();
            double xx=0,xy=0,yy=0;
            for(const auto& row:rows){xx+=row[0]*row[0];xy+=row[0]*row[1];yy+=row[1]*row[1];}
            bool orientation_diverse=rows.size()>=10&&xx*yy-xy*xy>0.025*(xx+yy)*(xx+yy);
            if(orientation_diverse&&detail::robust_fit(rows,values,fit,l.rms_px)&&l.rms_px<0.40){
                std::copy(fit.begin(),fit.end(),l.coefficients.begin());
                l.valid=true;l.status="measured_edge_normal_fit";
                return true;
            }
            l.status=rows.size()<10?"insufficient_edges":"degenerate_or_inconsistent_edges";
            return false;
        };
        if(!try_lca_fit(true)&&r.frames>1)try_lca_fit(false);
    }
    for(size_t f=0;f<r.frames;++f)for(int c=0;c<2;++c){
        if(!r.lca.channels[c].valid)continue;
        std::vector<std::vector<double>> rows;std::vector<double>values,fit;double rms=0;
        for(const auto&e:edges)if(e.frame==f&&e.valid[c?2:0]){
            auto s=r.lca.shift(c?2:0,e.x,e.y);
            rows.push_back({e.nx,e.ny});values.push_back(e.centroid[c?2:0]-e.centroid[1]-e.nx*s[0]-e.ny*s[1]);
        }
        if(detail::robust_fit(rows,values,fit,rms)&&rms<0.38)for(int k=0;k<2;++k)
            r.drift[f].lca_offset[c*2+k]=detail::measured(fit[k],rows.size(),"per_frame_color_translation_px");
    }
    for(int z=0;z<5;++z){
        auto& out=r.zones[z];
        std::vector<std::vector<double>> curves;
        std::vector<std::vector<std::vector<double>>> frame_curves(r.frames);
        std::vector<std::vector<double>> covariance_rows;std::vector<double> variances,covariance;
        for(const auto&e:edges)if(detail::zone(e.x,e.y,r.width,r.height)==z){
            ++out.edges;out.raw_samples+=e.samples;
            if(opt.far_field&&!e.mtf.empty()&&detail::static_far_edge(in,e,flow_models,r)){
                curves.push_back(e.mtf);
                if(e.frame<r.frames)frame_curves[e.frame].push_back(e.mtf);
                if((!opt.ois||e.frame==0)&&e.green_variance>0){
                    covariance_rows.push_back({e.nx*e.nx,2*e.nx*e.ny,e.ny*e.ny});
                    variances.push_back(e.green_variance);
                }
            }
        }
        if(!opt.far_field){out.status="requires_far_field_or_stars";continue;}
        if(curves.empty()){out.status="five_gate_rejection";continue;}
        out.mtf.resize(129);
        for(int k=0;k<129;++k){
            if(opt.ois){
                std::vector<double> fmeds;
                for(size_t f=0;f<r.frames;++f)if(!frame_curves[f].empty()){
                    std::vector<double> fa;fa.reserve(frame_curves[f].size());
                    for(const auto& v:frame_curves[f])fa.push_back(v[k]);
                    fmeds.push_back(detail::median(fa));
                }
                out.mtf[k]=*std::max_element(fmeds.begin(),fmeds.end());
            }else{
                std::vector<double>a;a.reserve(curves.size());
                for(const auto&v:curves)a.push_back(v[k]);
                out.mtf[k]=detail::median(a);
            }
        }
        out.mtf50=detail::crossing(out.mtf,.5,curves.size());out.mtf10=detail::crossing(out.mtf,.1,curves.size());
        out.method=opt.ois?"raw_8x_slanted_edge_ois_envelope":"raw_8x_slanted_edge";
        out.status="far_field_estimate";
        double rms=0;
        if(detail::robust_fit(covariance_rows,variances,covariance,rms)&&rms<.25){
            double xx=covariance[0],xy=covariance[1],yy=covariance[2],disc=std::hypot(xx-yy,2*xy);
            if(xx+yy>disc&&xx>0&&yy>0){
                out.fwhm_major=detail::measured(2.354820045*std::sqrt((xx+yy+disc)*.5),variances.size(),"directional_lsf_moment_equivalent_fwhm");
                out.fwhm_minor=detail::measured(2.354820045*std::sqrt((xx+yy-disc)*.5),variances.size(),"directional_lsf_moment_equivalent_fwhm");
                if(disc>.01*(xx+yy))out.angle_deg=detail::measured(std::atan2(2*xy,xx-yy)*90/pi,variances.size());
                else out.angle_deg.status="isotropic_psf";
            }
        }
    }
    if(opt.far_field&&in.time_seconds.size()==r.frames&&r.frames>=3){
        size_t first=0;bool valid=true;
        for(size_t f=0;f<r.frames;++f){
            valid=valid&&std::isfinite(in.time_seconds[f]);
            if(in.time_seconds[f]<in.time_seconds[first])first=f;
        }
        std::vector<double> first_mtf,other_mtf;
        for(const auto& e:edges)if(detail::static_far_edge(in,e,flow_models,r)){
            auto m=detail::crossing(e.mtf,.5,1);
            if(m.valid)(e.frame==first?first_mtf:other_mtf).push_back(m.value);
        }
        if(valid&&first_mtf.size()>=3&&other_mtf.size()>=6){
            double m=detail::median(first_mtf);
            if(m>0)r.first_frame_shake_ratio=detail::measured(detail::median(other_mtf)/m,first_mtf.size(),"chronological_first_frame_relative_psf_width");
        }
    }
    detail::stars(in,opt,r);
    bool corners=true;double min_mtf=1,max_mtf=0;
    for(int z=1;z<5;++z){
        corners=corners&&r.zones[z].mtf50.valid;
        if(r.zones[z].mtf50.valid){min_mtf=std::min(min_mtf,r.zones[z].mtf50.value);max_mtf=std::max(max_mtf,r.zones[z].mtf50.value);}
    }
    if(corners&&max_mtf>0)r.decenter_ratio=detail::measured(1-min_mtf/max_mtf,4,"corner_mtf_asymmetry");
    else r.decenter_ratio.status="requires_four_comparable_far_field_zones";
    detail::calibrate(opt,r);
    detail::scene_calibrate(in,opt,r);
    return r;
}

namespace detail {
inline std::string escape(const std::string& text,bool xml=false) {
    std::string s;
    s.reserve(text.size() + 8);
    for(unsigned char c:text) {
        if(xml){
            if(c=='&')s+="&amp;";else if(c=='<')s+="&lt;";else if(c=='>')s+="&gt;";
            else if(c=='\"')s+="&quot;";else if(c=='\'')s+="&apos;";
            else if(c>=32||c==9||c==10||c==13)s.push_back(char(c));
        }else{
            if(c=='\"'||c=='\\'){s.push_back('\\');s.push_back(char(c));}
            else if(c<32){char hex[8];std::snprintf(hex,sizeof(hex),"\\u%04x",int(c));s+=hex;}
            else s.push_back(char(c));
        }
    }
    return s;
}
inline std::string number(double v) {
    if(!std::isfinite(v))return "null";
    if(v==0.0)return "0";
    char buf[64];
    int len=std::snprintf(buf,sizeof(buf),"%.10g",v);
    for(int i=0;i<len;++i)if(buf[i]==',')buf[i]='.';
    return std::string(buf,len>0?size_t(len):0);
}
inline std::string metric(const Metric& m) {
    return "{\"value\":"+ (m.valid?number(m.value):"null")+",\"samples\":"+std::to_string(m.samples)+
           ",\"status\":\""+escape(m.status)+"\"}";
}
inline void save(const std::string& path,const std::vector<uint8_t>& bytes) {
    std::ofstream f(std::filesystem::u8path(path),std::ios::binary);
    if(!f)throw std::runtime_error("open failed: "+path);
    f.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    f.close();if(!f)throw std::runtime_error("write failed: "+path);
}
inline void save(const std::string& path,const std::string& text) {
    save(path,std::vector<uint8_t>(text.begin(),text.end()));
}
inline void le16(std::vector<uint8_t>& b,uint16_t v){b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
inline void le32(std::vector<uint8_t>& b,uint32_t v){for(int i=0;i<4;++i)b.push_back(uint8_t(v>>(8*i)));}
inline void be32(std::vector<uint8_t>& b,uint32_t v){for(int i=3;i>=0;--i)b.push_back(uint8_t(v>>(8*i)));}
inline uint32_t crc(const uint8_t* p,size_t n) {
    uint32_t c=0xffffffff;
    for(size_t i=0;i<n;++i){c^=p[i];for(int j=0;j<8;++j)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}
    return ~c;
}
inline void chunk(std::vector<uint8_t>& b,const char* type,const std::vector<uint8_t>& data){
    be32(b,uint32_t(data.size()));size_t p=b.size();
    for(int k=0;k<4;++k)b.push_back(uint8_t(type[k]));
    b.insert(b.end(),data.begin(),data.end());be32(b,crc(b.data()+p,data.size()+4));
}
inline std::vector<uint8_t> png(int w,int h,int channels,int bits,const std::vector<uint8_t>& pixels){
    if(w<=0||h<=0||(channels!=1&&channels!=3)||(bits!=8&&bits!=16)||
       pixels.size()!=size_t(w)*h*channels*(bits/8))throw std::invalid_argument("PNG shape");
    std::vector<uint8_t> scan;size_t stride=size_t(w)*channels*(bits/8);
    scan.reserve((stride+1)*h);
    for(int y=0;y<h;++y){scan.push_back(0);scan.insert(scan.end(),pixels.begin()+y*stride,pixels.begin()+(y+1)*stride);}
    // RFC 1950 zlib stream with RFC 1951 stored blocks: dependency-free.
    std::vector<uint8_t> compressed={0x78,0x01};
    uint32_t a=1,b=0;
    for(uint8_t v:scan){a=(a+v)%65521;b=(b+a)%65521;}
    for(size_t p=0;p<scan.size();){
        size_t n=std::min<size_t>(65535,scan.size()-p);compressed.push_back(p+n==scan.size()?1:0);
        le16(compressed,uint16_t(n));le16(compressed,uint16_t(~uint16_t(n)));
        compressed.insert(compressed.end(),scan.begin()+p,scan.begin()+p+n);p+=n;
    }
    be32(compressed,(b<<16)|a);
    std::vector<uint8_t> out={137,80,78,71,13,10,26,10},ihdr;
    be32(ihdr,uint32_t(w));be32(ihdr,uint32_t(h));ihdr.push_back(uint8_t(bits));
    ihdr.push_back(channels==1?0:2);ihdr.insert(ihdr.end(),{0,0,0});
    chunk(out,"IHDR",ihdr);chunk(out,"IDAT",compressed);chunk(out,"IEND",{});return out;
}
inline std::vector<uint8_t> npy(const std::vector<float>& v,const std::vector<size_t>& shape){
    size_t n=1;std::ostringstream s;
    s<<"{'descr': '<f4', 'fortran_order': False, 'shape': (";
    for(size_t d:shape){if(d&&n>std::numeric_limits<size_t>::max()/d)throw std::invalid_argument("NPY size");n*=d;s<<d<<", ";}
    s<<"), }";if(n!=v.size())throw std::invalid_argument("NPY shape");
    std::string h=s.str();h.append((64-(10+h.size()+1)%64)%64,' ');h+='\n';
    if(h.size()>65535)throw std::invalid_argument("NPY header size");
    std::vector<uint8_t>b={147,'N','U','M','P','Y',1,0};le16(b,uint16_t(h.size()));
    b.insert(b.end(),h.begin(),h.end());
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"IEEE float32 required");
    for(float f:v){uint32_t u;std::memcpy(&u,&f,4);le32(b,u);}return b;
}
inline std::vector<uint8_t> npz(const std::vector<std::pair<std::string,std::vector<uint8_t>>>& files){
    std::vector<uint8_t>b,dir;
    if(files.size()>65535)throw std::invalid_argument("NPZ entry count");
    for(const auto& f:files){
        if(f.first.size()>65535||f.second.size()>0xffffffffu||b.size()>0xffffffffu)throw std::invalid_argument("NPZ ZIP32 limit");
        uint32_t off=uint32_t(b.size()),sz=uint32_t(f.second.size()),c=crc(f.second.data(),f.second.size());
        le32(b,0x04034b50);le16(b,20);le16(b,0);le16(b,0);le16(b,0);le16(b,33);
        le32(b,c);le32(b,sz);le32(b,sz);le16(b,uint16_t(f.first.size()));le16(b,0);
        b.insert(b.end(),f.first.begin(),f.first.end());b.insert(b.end(),f.second.begin(),f.second.end());
        le32(dir,0x02014b50);le16(dir,20);le16(dir,20);le16(dir,0);le16(dir,0);le16(dir,0);le16(dir,33);
        le32(dir,c);le32(dir,sz);le32(dir,sz);le16(dir,uint16_t(f.first.size()));le16(dir,0);le16(dir,0);
        le16(dir,0);le16(dir,0);le32(dir,0);le32(dir,off);dir.insert(dir.end(),f.first.begin(),f.first.end());
    }
    if(b.size()+dir.size()>0xffffffffu)throw std::invalid_argument("NPZ ZIP32 limit");
    uint32_t start=uint32_t(b.size());b.insert(b.end(),dir.begin(),dir.end());
    le32(b,0x06054b50);le16(b,0);le16(b,0);le16(b,uint16_t(files.size()));le16(b,uint16_t(files.size()));
    le32(b,uint32_t(dir.size()));le32(b,start);le16(b,0);return b;
}
inline std::vector<uint8_t> tiff16(int w,int h,int channels,const std::vector<float>& values){
    if(w<=0||h<=0||(channels!=1&&channels!=3)||values.size()!=size_t(w)*h*channels)
        throw std::invalid_argument("TIFF shape");
    constexpr uint16_t count=11;const uint32_t extra=8+2+12*count+4,offset=extra+(channels==3?6:0);
    if(values.size()>((0xffffffffu-offset)/2))throw std::invalid_argument("TIFF32 limit");
    std::vector<uint8_t>b={'I','I',42,0};le32(b,8);le16(b,count);
    auto tag=[&](uint16_t t,uint16_t type,uint32_t n,uint32_t v){le16(b,t);le16(b,type);le32(b,n);le32(b,v);};
    tag(256,4,1,uint32_t(w));tag(257,4,1,uint32_t(h));tag(258,3,uint32_t(channels),channels==3?extra:16);
    tag(259,3,1,1);tag(262,3,1,channels==3?2:1);tag(273,4,1,offset);tag(277,3,1,uint32_t(channels));
    tag(278,4,1,uint32_t(h));tag(279,4,1,uint32_t(values.size()*2));tag(284,3,1,1);tag(339,3,1,1);le32(b,0);
    if(channels==3)for(int c=0;c<3;++c)le16(b,16);
    for(float v:values)le16(b,uint16_t(std::lround(std::clamp(double(v),0.0,1.0)*65535)));
    return b;
}
} // namespace detail

inline std::string report_json(const OpticsReport& r){
    std::ostringstream s;s.imbue(std::locale::classic());
    s<<"{\"schema\":\"org.nijin.optics.v1\",\"domain\":\"unWB_camera_linear_raw\","
      "\"coordinate_system\":\"active_raw_pixels_reference_to_source\","
      "\"mtf_frequency_unit\":\"cycles_per_raw_pixel\",\"oversampling\":8,"
      "\"width\":"<<r.width<<",\"height\":"<<r.height<<",\"frames\":"<<r.frames
      <<",\"status\":\""<<detail::escape(r.status)<<"\",\"camera\":\""<<detail::escape(r.camera)
      <<"\",\"lens\":\""<<detail::escape(r.lens)<<"\",\"ois\":"<<(r.ois?"true":"false")
      <<",\"drift_span_px\":"<<detail::metric(r.drift_span)
      <<",\"max_drift_1x\":"<<detail::metric(r.max_drift_1x)
      <<",\"first_frame_shake_ratio\":"<<detail::metric(r.first_frame_shake_ratio)
      <<",\"decenter_ratio\":"<<detail::metric(r.decenter_ratio)<<",\"phase_quadrants\":[";
    for(int i=0;i<4;++i)s<<(i?",":"")<<r.phase_quadrants[i];
    s<<"],\"lca\":{\"model\":\"translation_plus_radial_scale\",\"normalization\":\"max_width_height\",\"channels\":[";
    for(int c=0;c<2;++c){
        const auto& l=r.lca.channels[c];
        s<<(c?",":"")<<"{\"channel\":\""<<(c?"blue":"red")<<"\",\"status\":\""<<detail::escape(l.status)
         <<"\",\"samples\":"<<l.samples<<",\"rms_px\":"<<(l.valid?detail::number(l.rms_px):"null")<<",\"coefficients\":";
        if(l.valid){s<<"[";for(int k=0;k<4;++k)s<<(k?",":"")<<detail::number(l.coefficients[k]);s<<"]";}else s<<"null";
        s<<"}";
    }
    s<<"]},\"zones\":[";
    for(int z=0;z<5;++z){
        const auto& q=r.zones[z];
        s<<(z?",":"")<<"{\"name\":\""<<q.name<<"\",\"status\":\""<<detail::escape(q.status)
         <<"\",\"method\":\""<<q.method<<"\",\"edges\":"<<q.edges<<",\"stars\":"<<q.stars<<",\"raw_samples\":"<<q.raw_samples
         <<",\"mtf50\":"<<detail::metric(q.mtf50)<<",\"mtf10\":"<<detail::metric(q.mtf10)
         <<",\"fwhm_major_px\":"<<detail::metric(q.fwhm_major)<<",\"fwhm_minor_px\":"<<detail::metric(q.fwhm_minor)
         <<",\"angle_deg\":"<<detail::metric(q.angle_deg)<<",\"epsf_coverage\":"<<detail::number(q.epsf_coverage)<<",\"mtf\":[";
        for(size_t i=0;i<q.mtf.size();++i)s<<(i?",":"")<<detail::number(q.mtf[i]);
        s<<"],\"epsf_shape\":["<<(q.epsf.empty()?0:97)<<","<<(q.epsf.empty()?0:97)<<"],\"epsf\":[";
        for(size_t i=0;i<q.epsf.size();++i)s<<(i?",":"")<<detail::number(q.epsf[i]);
        s<<"]}";
    }
    s<<"],\"drift\":[";
    for(size_t n=0;n<r.drift.size();++n){
        const auto& d=r.drift[n];
        s<<(n?",":"")<<"{\"frame\":"<<n<<",\"dx_px\":"<<detail::metric(d.dx)<<",\"dy_px\":"<<detail::metric(d.dy)
         <<",\"row_shear_x\":"<<detail::metric(d.row_shear_x)<<",\"row_shear_y\":"<<detail::metric(d.row_shear_y)
         <<",\"time_seconds\":"<<detail::metric(d.time_seconds)<<",\"speed_px_s\":"<<detail::metric(d.speed_px_s)<<",\"lca_offset_px\":[";
        for(int k=0;k<4;++k)s<<(k?",":"")<<detail::metric(d.lca_offset[k]);
        s<<"]}";
    }
    s<<"]";
    auto radial=[&](const char* name,const RadialCalibration& c){
        s<<",\""<<name<<"\":{\"status\":\""<<detail::escape(c.status)<<"\",\"samples\":"<<c.samples
         <<",\"inliers\":"<<c.inliers<<",\"rms_unit\":\""<<c.rms_unit<<"\""
         <<",\"condition_ratio\":"<<(c.condition_ratio>0?detail::number(c.condition_ratio):"null")
         <<",\"rms\":"<<(c.valid?detail::number(c.rms):"null")<<",\"log_coefficients\":";
        if(c.log_model_valid)s<<"["<<detail::number(c.log_coefficients[0])<<","<<detail::number(c.log_coefficients[1])<<"]";
        else s<<"null";
        s<<",\"coefficients\":";
        if(c.valid){s<<"[";for(int k=0;k<3;++k)s<<(k?",":"")<<detail::number(c.coefficients[k]);s<<"]";}else s<<"null";s<<"}";
    };
    radial("distortion",r.distortion);radial("vignette",r.vignette);s<<"}\n";return s.str();
}
inline void write_json(const std::string& path,const OpticsReport& r){detail::save(path,report_json(r));}

inline std::string lcp_xml(const OpticsReport& r){
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(12);
    s<<"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description rdf:about=\"\" xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\" "
      "xmlns:stCamera=\"http://ns.adobe.com/photoshop/1.0/camera-profile\">"
      "<photoshop:CameraProfiles><rdf:Seq><rdf:li><rdf:Description>";
    auto tag=[&](const char* name,const std::string& value){s<<"<stCamera:"<<name<<">"<<detail::escape(value,true)<<"</stCamera:"<<name<<">";};
    tag("ProfileName","NijiN camera-linear measured profile");tag("CameraRawProfile","True");
    tag("Model",r.camera);tag("Lens",r.lens);
    tag("CameraPrettyName",r.camera);tag("LensPrettyName",r.lens);
    if(r.crop_factor>0&&std::isfinite(r.crop_factor))tag("SensorFormatFactor",detail::number(r.crop_factor));
    tag("ImageWidth",std::to_string(r.width));tag("ImageHeight",std::to_string(r.height));
    if(r.focal_mm>0&&std::isfinite(r.focal_mm))tag("FocalLength",detail::number(r.focal_mm));
    if(r.aperture>0&&std::isfinite(r.aperture))tag("ApertureValue",detail::number(2*std::log2(r.aperture)));
    if(r.focus_m>0&&std::isfinite(r.focus_m))tag("FocusDistance",detail::number(r.focus_m));
    // FocalLengthX/Y=1 sets normalization to max(width,height), matching fit.
    s<<"<stCamera:PerspectiveModel rdf:parseType=\"Resource\">";tag("Version","2");
    auto base=[&](){tag("FocalLengthX","1");tag("FocalLengthY","1");tag("ImageXCenter","0.5");tag("ImageYCenter","0.5");};
    base();
    if(r.distortion.valid){
        for(int k=0;k<3;++k)tag(("RadialDistortParam"+std::to_string(k+1)).c_str(),detail::number(r.distortion.coefficients[k]));
        tag("ResidualMeanError",detail::number(r.distortion.rms));
    }
    bool ca=r.lca.channels[0].valid||r.lca.channels[1].valid;
    if(ca&&r.width>0&&r.height>0){
        const char* names[]={"ChromaticRedGreenModel","ChromaticGreenModel","ChromaticBlueGreenModel"};
        for(int c=0;c<3;++c){
            s<<"<stCamera:"<<names[c]<<" rdf:parseType=\"Resource\">";
            double scale=1,radial=0,rms=0;
            double center_x=.5,center_y=.5;
            if(c!=1){const auto& l=r.lca.channels[c==0?0:1];if(l.valid){double d=std::max(r.width,r.height);
                scale+=l.coefficients[2]/d;radial=l.coefficients[3]/(d*scale);rms=l.rms_px/d;
                center_x+=l.coefficients[0]/r.width;center_y+=l.coefficients[1]/r.height;}}
            tag("FocalLengthX","1");tag("FocalLengthY","1");
            tag("ImageXCenter",detail::number(center_x));tag("ImageYCenter",detail::number(center_y));
            tag("ScaleFactor",detail::number(scale));tag("RadialDistortParam1",detail::number(radial));
            tag("RadialDistortParam2","0");tag("RadialDistortParam3","0");tag("ResidualMeanError",detail::number(rms));
            s<<"</stCamera:"<<names[c]<<">";
        }
    }
    if(r.vignette.valid){
        s<<"<stCamera:VignetteModel rdf:parseType=\"Resource\">";base();
        for(int k=0;k<3;++k)tag(("VignetteModelParam"+std::to_string(k+1)).c_str(),detail::number(r.vignette.coefficients[k]));
        tag("ResidualMeanError",detail::number(r.vignette.rms));s<<"</stCamera:VignetteModel>";
    }
    s<<"</stCamera:PerspectiveModel></rdf:Description></rdf:li></rdf:Seq></photoshop:CameraProfiles>"
      "</rdf:Description></rdf:RDF></x:xmpmeta>\n";
    return s.str();
}
inline void write_lcp(const std::string& path,const OpticsReport& r){detail::save(path,lcp_xml(r));}

// Fixed 5x7 bitmap font, owned here; no platform font/image dependency.
namespace detail {
inline std::array<uint8_t,7> glyph(char c){
    static const char* letters[]={
      "01110100011000111111100011000110001","11110100011000111110100011000111110",
      "01111100001000010000100001000001111","11110100011000110001100011000111110",
      "11111100001000011110100001000011111","11111100001000011110100001000010000",
      "01111100001000010111100011000101111","10001100011000111111100011000110001",
      "11111001000010000100001000010011111","00111000100001000010100101001001100",
      "10001100101010011000101001001010001","10000100001000010000100001000011111",
      "10001110111010110101100011000110001","10001110011010110011100011000110001",
      "01110100011000110001100011000101110","11110100011000111110100001000010000",
      "01110100011000110001101011001001101","11110100011000111110101001001010001",
      "01111100001000001110000010000111110","11111001000010000100001000010000100",
      "10001100011000110001100011000101110","10001100011000110001100010101000100",
      "10001100011000110101101011101110001","10001100010101000100010101000110001",
      "10001100010101000100001000010000100","11111000010001000100010001000011111"};
    static const char* digits[]={
      "01110100011001110101110011000101110","00100011000010000100001000010001110",
      "01110100010000100010001000100011111","11110000010000101110000010000111110",
      "00010001100101010010111110001000010","11111100001000011110000010000111110",
      "01110100001000011110100011000101110","11111000010001000100010000100001000",
      "01110100011000101110100011000101110","01110100011000101111000010000101110"};
    if(c>='a'&&c<='z')c=char(c-'a'+'A');
    const char* p=nullptr;if(c>='A'&&c<='Z')p=letters[c-'A'];if(c>='0'&&c<='9')p=digits[c-'0'];
    std::array<uint8_t,7>b{};
    if(p){for(int y=0;y<7;++y)for(int x=0;x<5;++x)b[y]|=uint8_t((p[y*5+x]=='1')<<(4-x));}
    else if(c=='.')b[6]=4;else if(c=='-')b[3]=14;else if(c=='/')b={1,2,2,4,8,8,16};
    else if(c==':'){b[2]=4;b[5]=4;}else if(c=='_')b[6]=31;
    return b;
}
struct Canvas{
    int w=1200,h=680;std::vector<uint8_t>p=std::vector<uint8_t>(size_t(w)*h*3,20);
    void dot(int x,int y,std::array<uint8_t,3>c){if(x<0||y<0||x>=w||y>=h)return;for(int k=0;k<3;++k)p[(size_t(y)*w+x)*3+k]=c[k];}
    void line(int x,int y,int xx,int yy,std::array<uint8_t,3>c){
        int n=std::max(std::abs(xx-x),std::abs(yy-y));
        for(int i=0;i<=n;++i)dot(x+(xx-x)*i/std::max(1,n),y+(yy-y)*i/std::max(1,n),c);
    }
    void text(int x,int y,const std::string&s,int scale=2){
        for(char c:s){
            auto g=glyph(c);
            for(int j=0;j<7;++j)for(int i=0;i<5;++i)if(g[j]&(1<<(4-i))){
                for(int a=0;a<scale;++a)for(int b=0;b<scale;++b)dot(x+i*scale+a,y+j*scale+b,{220,228,240});
            }
            x+=6*scale;if(x>w-12)break;
        }
    }
};
}
inline void write_passport_png(const std::string& path,const OpticsReport&r){
    detail::Canvas c;c.text(28,24,"NIJIN OPTICAL PASSPORT",3);
    c.text(28,60,"RAW CAMERA LINEAR - 8X SAMPLING - CYCLES/RAW PIXEL");
    c.text(28,88,r.camera+" / "+r.lens);
    for(int z=0;z<5;++z){
        int x=24+z*235,y=138;const auto&q=r.zones[z];
        c.text(x,y,q.name);c.text(x,y+25,q.mtf50.valid?"MTF50 "+detail::number(q.mtf50.value).substr(0,6):"MTF50 N/A");
        c.text(x,y+50,"SAMPLES "+std::to_string(q.raw_samples));
        c.text(x,y+76,q.status,1);
        c.line(x,y+110,x,y+260,{90,100,120});c.line(x,y+260,x+205,y+260,{90,100,120});
        for(size_t i=1;i<q.mtf.size();++i)c.line(x+int((i-1)*205/128),y+260-int(std::clamp(q.mtf[i-1],0.0,1.0)*145),
            x+int(i*205/128),y+260-int(std::clamp(q.mtf[i],0.0,1.0)*145),{60,210,180});
        if(q.fwhm_major.valid&&q.fwhm_minor.valid){
            double angle=q.angle_deg.valid?q.angle_deg.value*pi/180:0;
            double a=std::min(22.0,q.fwhm_major.value*5),b=std::min(22.0,q.fwhm_minor.value*5);
            for(int k=0;k<360;++k){
                double t=k*pi/180,u=a*std::cos(t),v=b*std::sin(t);
                c.dot(x+105+int(u*std::cos(angle)-v*std::sin(angle)),425+int(u*std::sin(angle)+v*std::cos(angle)),{250,180,80});
            }
        }
    }
    c.text(28,450,"DRIFT PX "+(r.drift_span.valid?detail::number(r.drift_span.value):"N/A"));
    c.text(28,480,"LCA RED "+r.lca.channels[0].status,1);c.text(28,502,"LCA BLUE "+r.lca.channels[1].status,1);
    c.text(28,530,"DISTORTION "+r.distortion.status,1);c.text(28,550,"VIGNETTE "+r.vignette.status,1);
    c.text(650,450,"PHASE QUADRANTS",1);
    for(int k=0;k<4;++k)c.text(650+(k%2)*65,475+(k/2)*35,std::to_string(r.phase_quadrants[k]));
    c.text(28,610,"FULL STATUS / SAMPLE COUNTS / UNITS IN JSON SIDECAR",2);
    if(r.drift_span.valid){
        double scale=130/std::max(1.0,r.drift_span.value);
        double ox=0,oy=0;
        for(const auto& d:r.drift)if(d.dx.valid){ox=d.dx.value;oy=d.dy.value;break;}
        auto px=[&](double x){return 930+int(std::clamp((x-ox)*scale,-200.0,200.0));};
        auto py=[&](double y){return 520+int(std::clamp((y-oy)*scale,-80.0,80.0));};
        for(size_t i=1;i<r.drift.size();++i)if(r.drift[i-1].dx.valid&&r.drift[i].dx.valid)
            c.line(px(r.drift[i-1].dx.value),py(r.drift[i-1].dy.value),
                   px(r.drift[i].dx.value),py(r.drift[i].dy.value),{250,180,80});
    }
    detail::save(path,detail::png(c.w,c.h,3,8,c.p));
}

struct DatasetInput {
    const std::vector<float>& noisy_raw;   // H1 x W1, active CFA mosaic.
    const std::vector<float>& target_rgb;  // (2H1) x (2W1) x 3, linear unWB.
    const std::vector<float>& sigma;       // (2H1) x (2W1), absolute linear SD.
    const std::vector<float>& phase_half;  // (H1/2) x (W1/2), coverage [0,1].
    const std::vector<float>& motion_half; // same shape; 0 means static.
    const uint8_t* cfa6x6;
    int H1,W1;
    bool target_linear_unbiased = false;
    bool reference_excluded_from_target = false;
    std::string camera, target_provenance;
    // Global 2x output coordinates; channel R=0, G=1, B=2. Used independently
    // when the corresponding vector is empty; samples stay tile-local.
    std::function<float(int y,int x,int channel)> target_sample = {};
    std::function<float(int y,int x)> sigma_sample = {};
};
struct DatasetOptions {
    int tile_size=64; // Raw-pixel units; target tile side is 2*tile_size.
    size_t max_tiles=128;
    bool png=true,tiff=true,npy=true;
    float min_conf=.95f,motion_threshold=.04f;
};
struct DatasetReport {size_t accepted=0,rejected=0;std::string status="no_eligible_tiles",manifest_path;};
inline DatasetReport export_dataset(const std::string& directory,const DatasetInput& in,const DatasetOptions& opt={}){
    DatasetReport report;
    if(in.H1<2||in.W1<2||in.H1>200000||in.W1>200000||opt.tile_size<2||opt.tile_size>2048||opt.tile_size%2||!in.cfa6x6)
        throw std::invalid_argument("dataset dimensions/tile/CFA");
    if(!std::isfinite(opt.min_conf)||opt.min_conf<0||opt.min_conf>1||
       !std::isfinite(opt.motion_threshold)||opt.motion_threshold<0)
        throw std::invalid_argument("dataset thresholds");
    size_t full=size_t(in.H1)*in.W1,half=size_t(in.H1/2)*(in.W1/2);
    bool target_ok=in.target_rgb.empty()?bool(in.target_sample):in.target_rgb.size()==full*12;
    bool sigma_ok=in.sigma.empty()?bool(in.sigma_sample):in.sigma.size()==full*4;
    if(in.noisy_raw.size()!=full||!target_ok||!sigma_ok||
       in.phase_half.size()!=half||in.motion_half.size()!=half)throw std::invalid_argument("dataset plane size");
    for(int i=0;i<36;++i)if(in.cfa6x6[i]>2)throw std::invalid_argument("dataset CFA");
    std::filesystem::create_directories(std::filesystem::u8path(directory));
    auto path=[&](const std::string&s){return (std::filesystem::u8path(directory)/s).u8string();};
    report.manifest_path=path("dataset_manifest.json");
    std::ostringstream manifest;manifest.imbue(std::locale::classic());
    manifest<<"{\"schema\":\"org.nijin.paired_raw.v1\",\"domain\":\"unWB_camera_linear\","
      "\"target_domain\":\"unWB_camera_linear\","
      "\"raw_dtype\":\"float32\",\"target_dtype\":\"float32\",\"target_scale\":2,"
      "\"sigma_unit\":\"absolute_linear_standard_deviation\","
      "\"array_keys\":{\"raw\":\"raw1x\",\"target\":\"gt2x\",\"sigma\":\"sigma2x\"},"
      "\"preview_encoding\":\"uint16_linear_clamped_0_1\",\"phase_threshold\":"<<detail::number(opt.min_conf)
      <<",\"phase_gate\":\"tile_mean_greater_equal\",\"motion_threshold\":"<<detail::number(opt.motion_threshold)
      <<",\"motion_gate\":\""<<(opt.motion_threshold==0?"tile_max_less_equal":"tile_max_less")
      <<"\",\"tile_size_unit\":\"raw_pixels\","
      "\"camera\":\""<<detail::escape(in.camera)<<"\",\"target_provenance\":\""<<detail::escape(in.target_provenance)
      <<"\",\"reference_excluded\":"<<(in.reference_excluded_from_target?"true":"false")<<",\"tiles\":[";
    int t=opt.tile_size,ww=in.W1*2;
    if(in.target_linear_unbiased&&in.reference_excluded_from_target){
      for(int y=0;y+t<=in.H1;y+=t)for(int x=0;x+t<=in.W1;x+=t){
        if(report.accepted>=opt.max_tiles)continue;
        bool valid=true;
        double phase_sum=0,max_motion=0;
        size_t phase_samples=0;
        for(int yy=y/2;yy<(y+t)/2&&valid;++yy)for(int xx=x/2;xx<(x+t)/2;++xx){
            size_t i=size_t(yy)*(in.W1/2)+xx;
            double phase=in.phase_half[i],motion=in.motion_half[i];
            if(!std::isfinite(phase)||!std::isfinite(motion)||phase<0||phase>1||motion<0){valid=false;break;}
            phase_sum+=phase;++phase_samples;max_motion=std::max(max_motion,motion);
        }
        double mean_phase=phase_samples?phase_sum/phase_samples:0;
        valid=valid&&phase_samples>0&&mean_phase>=opt.min_conf&&
              (opt.motion_threshold==0?max_motion<=0:max_motion<opt.motion_threshold);
        if(!valid){++report.rejected;continue;}
        std::vector<float> raw,target,sd;
        raw.reserve(size_t(t)*t);target.reserve(size_t(t)*t*12);sd.reserve(size_t(t)*t*4);
        for(int yy=y;yy<y+t;++yy)for(int xx=x;xx<x+t;++xx)raw.push_back(in.noisy_raw[size_t(yy)*in.W1+xx]);
        for(int yy=y*2;yy<(y+t)*2;++yy)for(int xx=x*2;xx<(x+t)*2;++xx){
            size_t i=size_t(yy)*ww+xx;
            sd.push_back(in.sigma.empty()?in.sigma_sample(yy,xx):in.sigma[i]);
            for(int k=0;k<3;++k)
                target.push_back(in.target_rgb.empty()?in.target_sample(yy,xx,k):in.target_rgb[i*3+k]);
        }
        for(float v:raw)valid=valid&&std::isfinite(v);
        for(float v:target)valid=valid&&std::isfinite(v);
        for(float v:sd)valid=valid&&std::isfinite(v)&&v>=0;
        if(!valid){++report.rejected;continue;}
        std::string stem="tile_"+std::to_string(report.accepted);
        auto rn=detail::npy(raw,{size_t(t),size_t(t)}),tn=detail::npy(target,{size_t(t*2),size_t(t*2),3}),
             sn=detail::npy(sd,{size_t(t*2),size_t(t*2)});
        detail::save(path(stem+".npz"),detail::npz({{"raw1x.npy",rn},{"gt2x.npy",tn},{"sigma2x.npy",sn}}));
        if(opt.npy){detail::save(path(stem+"_raw.npy"),rn);detail::save(path(stem+"_target.npy"),tn);detail::save(path(stem+"_sigma.npy"),sn);}
        if(opt.tiff){
            detail::save(path(stem+"_raw.tif"),detail::tiff16(t,t,1,raw));
            detail::save(path(stem+"_target.tif"),detail::tiff16(t*2,t*2,3,target));
            detail::save(path(stem+"_sigma.tif"),detail::tiff16(t*2,t*2,1,sd));
        }
        if(opt.png){
            auto png=[&](const std::vector<float>&v,int size,int channels,const std::string& suffix){
                std::vector<uint8_t>b;b.reserve(v.size()*2);
                for(float f:v){uint16_t u=uint16_t(std::lround(std::clamp(double(f),0.0,1.0)*65535));b.push_back(uint8_t(u>>8));b.push_back(uint8_t(u));}
                detail::save(path(stem+suffix),detail::png(size,size,channels,16,b));
            };
            png(raw,t,1,"_raw.png");png(target,t*2,3,"_target.png");png(sd,t*2,1,"_sigma.png");
        }
        manifest<<(report.accepted?",":"")<<"{\"id\":\""<<stem<<"\",\"x\":"<<x<<",\"y\":"<<y<<",\"raw_size\":"<<t
          <<",\"target_size\":"<<t*2<<",\"phase_mean\":"<<detail::number(mean_phase)<<",\"motion_max\":"<<detail::number(max_motion)
          <<",\"archive\":\""<<stem<<".npz\",\"cfa6x6\":[";
        for(int cy=0;cy<6;++cy)for(int cx=0;cx<6;++cx)manifest<<((cy||cx)?",":"")<<int(in.cfa6x6[((cy+y)%6)*6+(cx+x)%6]);
        manifest<<"]}";++report.accepted;
      }
    }else report.status="requires_linear_independent_target";
    if(report.accepted)report.status="exported";
    manifest<<"],\"accepted\":"<<report.accepted<<",\"rejected\":"<<report.rejected<<",\"status\":\""<<report.status<<"\"}\n";
    detail::save(report.manifest_path,manifest.str());return report;
}

} // namespace heraw_optics
#endif
