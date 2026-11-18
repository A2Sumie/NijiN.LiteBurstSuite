// heraw_suite_features.hpp -- Camera-linear temporal and relative-light-field tools.
// C++20, header-only, 7-bit ASCII. No white balance or display transform is applied.
#pragma once

#include "heraw_fs_utf8.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace heraw_suite {

using Planes = std::vector<std::vector<float>>;
using RowTask = std::function<void(int, int)>;
// Synchronous: invoke task on disjoint [begin,end) row ranges covering [0,rows),
// and join every task before returning. Each task owns its temporary vectors.
using ParallelRows = std::function<void(int rows, const RowTask& task)>;

// H1/W1 are RAW dimensions. Every other plane is (H1/2)*(W1/2).
// Flow is reference -> source in HALF-grid pixels. Frame zero is the reference.
// Raw samples and green planes are exposure-matched, black-subtracted, camera-linear.
// noise variance = noise_read_var + noise_shot * max(signal, 0), in this same domain.
struct BurstView {
    const Planes& frames_lin;
    const Planes& g_halfs;
    const Planes& flows_fy;
    const Planes& flows_fx;
    const Planes& mw_half;
    int H1, W1;
    const uint8_t (&cfa)[6][6];
    float noise_shot = 0.0f;
    float noise_read_var = 1e-6f;
    const std::vector<double>* times_seconds = nullptr;
    const std::vector<float>* focus_positions = nullptr;
    float exposure_seconds = 0.0f;
    float focal_length_mm = 0.0f;
    bool distant_scene = false;
    // Original input indices, kept in the current (possibly AF-anchor-first) order.
    const std::vector<size_t>* capture_order = nullptr;
    float focus_distance_m = 0.0f;
    float dof_near_m = 0.0f;
    float dof_far_m = 0.0f;
    float hyperfocal_m = 0.0f;
    bool focus_is_infinity = false;
    bool distant_by_optics = false;
};

struct Gate {
    bool observable = false;
    std::string reason;
    std::string units;
    double measured = 0.0;
    double threshold = 0.0;
    double confidence = 0.0;
};

// Normalized coordinates: x=(column-center)/scale, y=(row-center)/scale.
struct Homography {
    std::array<double, 9> h{{1, 0, 0, 0, 1, 0, 0, 0, 1}};
    double cx = 0, cy = 0, scale = 1;
    double rms_half_px = 0;
    size_t samples = 0;
    bool valid = false;
    std::array<float, 2> source(float y, float x) const {
        const double u = (x - cx) / scale, v = (y - cy) / scale;
        const double d = h[6] * u + h[7] * v + h[8];
        if (!std::isfinite(d) || std::fabs(d) < 1e-9)
            return {{std::numeric_limits<float>::quiet_NaN(), 0}};
        return {{static_cast<float>((h[3]*u+h[4]*v+h[5])/d*scale+cy),
                 static_cast<float>((h[0]*u+h[1]*v+h[2])/d*scale+cx)}};
    }
};

enum class TemporalMode { CleanPlate, VirtualND, Transient, Strobe, Glint, Reflection };
struct TemporalOptions {
    TemporalMode mode = TemporalMode::CleanPlate;
    float reject_sigma = 3.5f;
    size_t strobe_stride = 2;
    float strobe_opacity = 1.0f;
    // W5 reflection requires measured two-layer disparity, in 1x RAW pixels.
    float reflection_disparity_px = 0.0f;
    // Zero gives equal opacity; one attenuates the oldest selected frame by exp(-4).
    // Appended to retain the positional aggregate initialization of earlier APIs.
    float strobe_fade = 0.0f;
};
struct TemporalResult {
    int H = 0, W = 0;
    Planes weights;                 // Includes corrected reference-frame weights.
    std::vector<float> guide;       // Temporal median; ND uses equal mean. Camera-linear green.
    std::vector<float> activity;    // Fraction of temporal outliers.
    std::vector<Homography> camera;
    Gate gate;
    TemporalOptions options;
};
struct Image {
    int H = 0, W = 0;
    std::vector<float> rgb;         // Interleaved RGB, camera-linear.
};
struct TemporalImage {
    Image image;
    Image layer;                   // W3/W5 additive camera-linear RGB layer.
    std::vector<float> gray_layer; // Same output grid as image.
    Gate gate;
};
struct Geometry {
    std::vector<Homography> camera;
    std::vector<std::array<float, 2>> baseline; // Rank-one residual factors, y/x.
    float rho_low = 0, rho_high = 0;
    float disparity_span_px = 0;   // Robust 5--95% span, 1x RAW pixels.
    float rank1_coherence = 0;
    float support_fraction = 0;
    Gate gate;
    bool sign_calibrated = false;
    bool has_focus_plane = false;
    float focus_plane_depth = 0.5f;
};
struct FieldResult {
    int H = 0, W = 0;
    std::map<std::string, std::vector<float>> maps;
    std::map<std::string, Gate> gates;
    Geometry geometry;
};

namespace detail {
inline size_t pixels(int h, int w) {
    if (h <= 0 || w <= 0 || static_cast<size_t>(h) >
        std::numeric_limits<size_t>::max() / static_cast<size_t>(w) / 3)
        throw std::invalid_argument("Invalid image dimensions");
    return static_cast<size_t>(h) * static_cast<size_t>(w);
}
inline float clamp01(float x) { return std::max(0.0f, std::min(1.0f, x)); }
inline void validate(const BurstView& b) {
    const size_t p = pixels(b.H1, b.W1), q = pixels(b.H1/2, b.W1/2);
    const size_t n = b.frames_lin.size();
    if (n == 0 || b.g_halfs.size()!=n || b.flows_fy.size()!=n ||
        b.flows_fx.size()!=n || b.mw_half.size()!=n)
        throw std::invalid_argument("Burst plane count mismatch");
    for (size_t k=0; k<n; ++k)
        if (b.frames_lin[k].size()!=p || b.g_halfs[k].size()!=q ||
            b.flows_fy[k].size()!=q || b.flows_fx[k].size()!=q ||
            b.mw_half[k].size()!=q)
            throw std::invalid_argument("Burst plane dimensions mismatch");
    unsigned colors=0;
    for (const auto& row : b.cfa)
        for (uint8_t c : row) {
            if (c > 2) throw std::invalid_argument("CFA must contain RGB indices 0..2");
            colors|=1u<<c;
        }
    if(colors!=7) throw std::invalid_argument("CFA must sample all three RGB channels");
    if (!std::isfinite(b.noise_shot) || !std::isfinite(b.noise_read_var) ||
        b.noise_shot<0 || b.noise_read_var<0)
        throw std::invalid_argument("Invalid noise variance");
    if (b.times_seconds) {
        if (b.times_seconds->size()!=n) throw std::invalid_argument("Timestamp count mismatch");
        for (size_t k=0; k<n; ++k)
            if (!std::isfinite((*b.times_seconds)[k]))
                throw std::invalid_argument("Nonfinite timestamp");
    }
    if (b.focus_positions && b.focus_positions->size()!=n)
        throw std::invalid_argument("Focus position count mismatch");
    if (b.capture_order && b.capture_order->size()!=n)
        throw std::invalid_argument("Capture order count mismatch");
}
inline float variance(const BurstView& b, float v) {
    return std::max(1e-12f, b.noise_read_var+b.noise_shot*std::max(0.0f,v));
}
inline bool inside(float y, float x, int h, int w) {
    return std::isfinite(y) && std::isfinite(x) && y>=0 && x>=0 && y<=h-1 && x<=w-1;
}
inline float sample(const std::vector<float>& a, int h, int w, float y, float x) {
    if (!std::isfinite(y) || !std::isfinite(x))
        return std::numeric_limits<float>::quiet_NaN();
    y=std::max(0.0f,std::min(static_cast<float>(h-1),y));
    x=std::max(0.0f,std::min(static_cast<float>(w-1),x));
    const int iy=static_cast<int>(y), ix=static_cast<int>(x);
    const int jy=std::min(iy+1,h-1), jx=std::min(ix+1,w-1);
    const float fy=y-iy, fx=x-ix;
    const float a0=a[static_cast<size_t>(iy)*w+ix]*(1-fx)+a[static_cast<size_t>(iy)*w+jx]*fx;
    const float a1=a[static_cast<size_t>(jy)*w+ix]*(1-fx)+a[static_cast<size_t>(jy)*w+jx]*fx;
    return a0*(1-fy)+a1*fy;
}
inline float quantile(std::vector<float> a, float q) {
    a.erase(std::remove_if(a.begin(),a.end(),[](float x){return !std::isfinite(x);}),a.end());
    if (a.empty()) return 0;
    const size_t k=static_cast<size_t>(q*static_cast<float>(a.size()-1));
    std::nth_element(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(k),a.end());
    return a[k];
}
inline float median(std::vector<float>& a) {
    if (a.empty()) return 0;
    const size_t k=a.size()/2;
    std::nth_element(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(k),a.end());
    const float hi=a[k];
    if (a.size()%2) return hi;
    return 0.5f*(hi+*std::max_element(a.begin(),a.begin()+static_cast<std::ptrdiff_t>(k)));
}
inline bool solve8(double a[8][9], std::array<double,8>& x) {
    for (int k=0;k<8;++k) {
        int pivot=k;
        for (int r=k+1;r<8;++r) if(std::fabs(a[r][k])>std::fabs(a[pivot][k])) pivot=r;
        if (std::fabs(a[pivot][k])<1e-12) return false;
        for (int c=k;c<9;++c) std::swap(a[k][c],a[pivot][c]);
        const double d=a[k][k];
        for(int c=k;c<9;++c) a[k][c]/=d;
        for(int r=0;r<8;++r) if(r!=k) {
            const double f=a[r][k];
            for(int c=k;c<9;++c) a[r][c]-=f*a[k][c];
        }
    }
    for(int k=0;k<8;++k) { x[k]=a[k][8]; if(!std::isfinite(x[k])) return false; }
    return true;
}
inline float gradient_mag(const std::vector<float>& g,int h,int w,float y,float x) {
    const float gx1=sample(g,h,w,y,x+1)-sample(g,h,w,y,x-1);
    const float gy1=sample(g,h,w,y+1,x)-sample(g,h,w,y-1,x);
    const float gx2=0.5f*(sample(g,h,w,y,x+2)-sample(g,h,w,y,x-2));
    const float gy2=0.5f*(sample(g,h,w,y+2,x)-sample(g,h,w,y-2,x));
    return std::max(std::hypot(gx1,gy1),std::hypot(gx2,gy2));
}
} // namespace detail

// Fit image-derived camera homographies with bounded samples and robust IRLS.
inline std::vector<Homography> fit_camera(const BurstView& b) {
    detail::validate(b);
    const int h=b.H1/2,w=b.W1/2;
    const size_t p=detail::pixels(h,w), n=b.frames_lin.size();
    std::vector<Homography> result(n);
    const size_t stride=std::max<size_t>(1,(p+4095)/4096);
    for(size_t k=0;k<n;++k) {
        Homography& out=result[k];
        out.cx=(w-1)*0.5; out.cy=(h-1)*0.5; out.scale=std::max(h,w)*0.5;
        if(k==0) { out.valid=true; out.samples=p; continue; }
        struct Match { double x,y,u,v,weight; };
        std::vector<Match> matches;
        std::vector<float> dx,dy,dx_tex,dy_tex;
        for(size_t i=0;i<p;i+=stride) {
            const float fx=b.flows_fx[k][i],fy=b.flows_fy[k][i],mw=b.mw_half[k][i];
            if(!std::isfinite(fx)||!std::isfinite(fy)||!std::isfinite(mw)||mw<0.1f) continue;
            const int xi=static_cast<int>(i%w),yi=static_cast<int>(i/w);
            const float g0=b.g_halfs[0][i];
            const float nsd=std::sqrt(detail::variance(b,g0));
            const float grad=detail::gradient_mag(b.g_halfs[0],h,w,static_cast<float>(yi),static_cast<float>(xi));
            const bool tex=grad>1.5f*nsd;
            const double w_grad=tex ? 1.0 : 0.05;
            const double x=(static_cast<double>(xi)-out.cx)/out.scale;
            const double y=(static_cast<double>(yi)-out.cy)/out.scale;
            matches.push_back({x,y,x+fx/out.scale,y+fy/out.scale,detail::clamp01(mw)*w_grad});
            dx.push_back(fx); dy.push_back(fy);
            if(tex) { dx_tex.push_back(fx); dy_tex.push_back(fy); }
        }
        out.samples=matches.size();
        if(matches.empty()) continue;
        const bool use_tex=dx_tex.size()>=12;
        out.h[2]=detail::median(use_tex ? dx_tex : dx)/out.scale;
        out.h[5]=detail::median(use_tex ? dy_tex : dy)/out.scale;
        if(matches.size()<12) continue;
        out.valid=true;
        // Warm-up Cauchy IRLS followed by bounded Tukey biweight locks onto the
        // dominant rigid plane without averaging foreground parallax layers into H.
        std::vector<float> errors(matches.size());
        for(int iter=0;iter<8;++iter) {
            for(size_t j=0;j<matches.size();++j) {
                const auto& m=matches[j];
                const double d=out.h[6]*m.x+out.h[7]*m.y+1;
                const double ex=(out.h[0]*m.x+out.h[1]*m.y+out.h[2])/d-m.u;
                const double ey=(out.h[3]*m.x+out.h[4]*m.y+out.h[5])/d-m.v;
                errors[j]=static_cast<float>(std::hypot(ex,ey)*out.scale);
            }
            const float q_ref=iter<3 ? 0.5f : 0.4f;
            const float robust=std::max(0.03f,1.4826f*detail::quantile(errors,q_ref));
            const double cutoff=iter<3 ? 2.5*robust : std::min(1.5,std::max(0.09,3.0*robust));
            double normal[8][9]={};
            size_t active=0;
            for(size_t j=0;j<matches.size();++j) {
                const auto& m=matches[j];
                const double r=errors[j]/cutoff;
                double wt=0;
                if(iter<3) {
                    wt=m.weight/(1+r*r);
                    ++active;
                } else if(r<1.0) {
                    const double t=1.0-r*r;
                    wt=m.weight*t*t;
                    ++active;
                }
                if(wt<=0) continue;
                const double rows[2][9]={
                    {m.x,m.y,1,0,0,0,-m.u*m.x,-m.u*m.y,m.u},
                    {0,0,0,m.x,m.y,1,-m.v*m.x,-m.v*m.y,m.v}};
                for(const auto& row:rows)
                    for(int a=0;a<8;++a)
                        for(int c=0;c<9;++c) normal[a][c]+=wt*row[a]*row[c];
            }
            if(active<8) break;
            std::array<double,8> solution{};
            if(!detail::solve8(normal,solution)) break;
            std::copy(solution.begin(),solution.end(),out.h.begin());
            out.valid=true;
        }
        out.rms_half_px=detail::quantile(errors,0.5f);
    }
    return result;
}

// W1/W2/W4 use camera-only alignment, so foreground flow cannot pin a passer-by.
// Reconstruction callers may fill the existing flow buffers with this helper.
inline void materialize_camera_flows(const TemporalResult& t, Planes& fy, Planes& fx) {
    const size_t p=detail::pixels(t.H,t.W);
    fy.resize(t.camera.size()); fx.resize(t.camera.size());
    for(size_t n=0;n<t.camera.size();++n) {
        fy[n].resize(p); fx[n].resize(p);
        for(int y=0;y<t.H;++y) for(int x=0;x<t.W;++x) {
            const auto s=t.camera[n].source(static_cast<float>(y),static_cast<float>(x));
            const size_t i=static_cast<size_t>(y)*t.W+x;
            fy[n][i]=s[0]-y; fx[n][i]=s[1]-x;
        }
    }
}

inline TemporalResult prepare_temporal(const BurstView& b, const TemporalOptions& options = {}) {
    detail::validate(b);
    if(!std::isfinite(options.reject_sigma)||options.reject_sigma<=0 ||
        !std::isfinite(options.strobe_opacity)||options.strobe_opacity<0||options.strobe_opacity>1 ||
        !std::isfinite(options.strobe_fade)||options.strobe_fade<0||options.strobe_fade>1 ||
        options.strobe_stride==0 || !std::isfinite(options.reflection_disparity_px))
        throw std::invalid_argument("Invalid temporal options");
    TemporalResult out;
    out.H=b.H1/2; out.W=b.W1/2; out.options=options; out.camera=fit_camera(b);
    const size_t n=b.frames_lin.size(),p=detail::pixels(out.H,out.W);
    out.weights.assign(n,std::vector<float>(p,0));
    out.guide.resize(p); out.activity.resize(p);
    std::vector<float> values(n),valid;
    valid.reserve(n);
    size_t majority=0,nonfinite=0,saturated=0,usable=0;
    if(options.mode==TemporalMode::Reflection)
        for(const auto& frame:b.frames_lin)
            for(float v:frame) nonfinite+=!std::isfinite(v);
    for(int y=0;y<out.H;++y) for(int x=0;x<out.W;++x) {
        const size_t i=static_cast<size_t>(y)*out.W+x;
        valid.clear();
        for(size_t k=0;k<n;++k) {
            const auto s=out.camera[k].source(static_cast<float>(y),static_cast<float>(x));
            values[k]=std::numeric_limits<float>::quiet_NaN();
            if(!detail::inside(s[0],s[1],out.H,out.W)) continue;
            values[k]=detail::sample(b.g_halfs[k],out.H,out.W,s[0],s[1]);
            if(std::isfinite(values[k])) {
                valid.push_back(values[k]); ++usable;
                saturated+=values[k]>=0.995f;
            }
        }
        const float med=detail::median(valid);
        out.guide[i]=options.mode==TemporalMode::VirtualND && !valid.empty() ?
            std::accumulate(valid.begin(),valid.end(),0.0f)/valid.size() : med;
        const float sigma=std::sqrt(2*detail::variance(b,med));
        size_t inliers=0,available=0;
        for(size_t k=0;k<n;++k) {
            if(!std::isfinite(values[k])) continue;
            ++available;
            const float z=std::fabs(values[k]-med)/(options.reject_sigma*sigma);
            if(z<=1) ++inliers;
            // Re-estimate every frame, including frame zero. Old mw masks compare
            // against frame zero and would reject the newly revealed background.
            float wt=z<1 ? (1-z*z)*(1-z*z) : 0;
            if(options.mode==TemporalMode::VirtualND) wt=1;
            out.weights[k][i]=wt;
        }
        out.activity[i]=available ? 1-static_cast<float>(inliers)/available : 0;
        majority+=available>=3 && inliers>available/2;
    }
    const size_t minimum=(options.mode==TemporalMode::CleanPlate || options.mode==TemporalMode::VirtualND) ? 4 : 3;
    const size_t req_aligned=std::min<size_t>(minimum,std::max<size_t>(3,(n*3)/4));
    size_t aligned=0;
    for(const auto& camera:out.camera) aligned+=camera.valid;
    out.gate.observable=n>=minimum && aligned>=req_aligned;
    out.gate.measured=static_cast<double>(n); out.gate.threshold=static_cast<double>(minimum);
    out.gate.units="frames"; out.gate.confidence=static_cast<double>(majority)/p;
    out.gate.reason=out.gate.observable ? "camera-aligned temporal estimate; background requires majority visibility" :
        "additional reliably camera-aligned frames required";
    if(options.mode==TemporalMode::Reflection) {
        out.gate.measured=options.reflection_disparity_px; out.gate.threshold=20;
        out.gate.units="1x_px";
        const size_t max_sat=std::max<size_t>(4,usable/25);
        out.gate.observable=n>=3 && aligned>=3 && options.reflection_disparity_px>=20 &&
            nonfinite==0 && usable>0 && saturated<=max_sat;
        out.gate.reason=out.gate.observable ? "wide-baseline unsaturated lower-envelope separation" :
            "requires measured two-layer disparity >=20 1x pixels (40 2x pixels) and <=4% saturated samples";
    }
    return out;
}

namespace detail {
// Compact CFA-aware tent reconstruction, with a 6x6-period support for X-Trans.
inline std::array<float,3> raw_rgb(const BurstView& b,size_t k,float y,float x) {
    std::array<float,3> sum{{0,0,0}},weight{{0,0,0}};
    if(!inside(y,x,b.H1,b.W1)) return {{0,0,0}};
    const int iy=static_cast<int>(std::floor(y)),ix=static_cast<int>(std::floor(x));
    for(int r=std::max(0,iy-2);r<=std::min(b.H1-1,iy+3);++r)
        for(int c=std::max(0,ix-2);c<=std::min(b.W1-1,ix+3);++c) {
            const float wt=std::max(0.0f,3-std::fabs(y-r))*std::max(0.0f,3-std::fabs(x-c));
            const size_t ch=b.cfa[r%6][c%6];
            const float v=b.frames_lin[k][static_cast<size_t>(r)*b.W1+c];
            if(std::isfinite(v)) { sum[ch]+=wt*v; weight[ch]+=wt; }
        }
    for(size_t c=0;c<3;++c) sum[c]=weight[c]>0 ? sum[c]/weight[c] : 0;
    return sum;
}
} // namespace detail

// Requested H/W may be 1x or 2x; memory is output + O(frame_count * active_tasks).
inline TemporalImage render_temporal(const BurstView& b,const TemporalResult& t,
                                    int H=0,int W=0,const ParallelRows& parallel_rows = {}) {
    detail::validate(b);
    if(H==0) H=b.H1;
    if(W==0) W=b.W1;
    const size_t p=detail::pixels(H,W),n=b.frames_lin.size();
    if(t.H!=b.H1/2||t.W!=b.W1/2||t.weights.size()!=n||t.camera.size()!=n ||
        t.guide.size()!=detail::pixels(t.H,t.W))
        throw std::invalid_argument("Temporal preparation mismatch");
    for(const auto& plane:t.weights)
        if(plane.size()!=t.guide.size()) throw std::invalid_argument("Temporal weight dimensions mismatch");
    TemporalImage out;
    out.image={H,W,std::vector<float>(3*p)}; out.gate=t.gate;
    const bool layer=t.options.mode==TemporalMode::Transient ||
        t.options.mode==TemporalMode::Glint || t.options.mode==TemporalMode::Reflection;
    if(layer) { out.layer={H,W,std::vector<float>(3*p)}; out.gray_layer.resize(p); }
    std::vector<size_t> order;
    std::vector<float> strobe_alpha;
    if(t.options.mode==TemporalMode::Strobe) {
        if(t.options.strobe_stride==0||!std::isfinite(t.options.strobe_fade)||
            t.options.strobe_fade<0||t.options.strobe_fade>1)
            throw std::invalid_argument("Invalid prepared strobe options");
        order.resize(n); std::iota(order.begin(),order.end(),size_t{0});
        std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t c) {
            if(b.times_seconds && (*b.times_seconds)[a]!=(*b.times_seconds)[c])
                return (*b.times_seconds)[a]<(*b.times_seconds)[c];
            return b.capture_order ? (*b.capture_order)[a]<(*b.capture_order)[c] : a<c;
        });
        const size_t last=((n-1)/t.options.strobe_stride)*t.options.strobe_stride;
        const double first_time=b.times_seconds ? (*b.times_seconds)[order.front()] : 0;
        const double last_time=b.times_seconds ? (*b.times_seconds)[order[last]] : static_cast<double>(last);
        const bool timed=b.times_seconds && last_time>first_time;
        const auto ordinal=[&](size_t rank) {
            return b.capture_order ? (*b.capture_order)[order[rank]] : rank;
        };
        const size_t first_index=ordinal(0),last_index=ordinal(last);
        strobe_alpha.assign(n,0);
        for(size_t rank=0;rank<=last;rank+=t.options.strobe_stride) {
            const double age=timed ? (last_time-(*b.times_seconds)[order[rank]])/(last_time-first_time) :
                (last_index>first_index ? static_cast<double>(last_index-ordinal(rank))/(last_index-first_index) : 0);
            strobe_alpha[order[rank]]=t.options.strobe_opacity*
                static_cast<float>(std::exp(-4*t.options.strobe_fade*age));
        }
    }
    const RowTask render_rows=[&](int begin,int end) {
    if(begin<0||end<begin||end>H) throw std::invalid_argument("Invalid temporal row range");
    std::vector<std::array<float,3>> samples(n);
    std::vector<float> green(n),work;
    std::vector<uint8_t> valid(n);
    work.reserve(n);
    for(int y=begin;y<end;++y) for(int x=0;x<W;++x) {
        const size_t i=static_cast<size_t>(y)*W+x;
        const float hy=(y+0.5f)*b.H1/H*0.5f-0.5f;
        const float hx=(x+0.5f)*b.W1/W*0.5f-0.5f;
        const float cy=std::max(0.0f,std::min(static_cast<float>(t.H-1),hy));
        const float cx=std::max(0.0f,std::min(static_cast<float>(t.W-1),hx));
        for(size_t k=0;k<n;++k) {
            const auto s=t.camera[k].source(cy,cx);
            valid[k]=detail::inside(s[0],s[1],t.H,t.W);
            if(valid[k]) {
                // Half-grid green represents the center of a RAW 2x2 cell.
                samples[k]=detail::raw_rgb(b,k,2*s[0]+0.5f,2*s[1]+0.5f);
                green[k]=detail::sample(b.g_halfs[k],t.H,t.W,s[0],s[1]);
                valid[k]=std::isfinite(green[k]);
            }
        }
        std::array<float,3> base{{0,0,0}},hi{{0,0,0}};
        for(size_t c=0;c<3;++c) {
            work.clear();
            for(size_t k=0;k<n;++k) if(valid[k]) work.push_back(samples[k][c]);
            base[c]=detail::median(work);
        }
        const float med=detail::sample(t.guide,t.H,t.W,cy,cx);
        const float threshold=t.options.reject_sigma*std::sqrt(2*detail::variance(b,med));
        const auto mode=t.options.mode;
        if(mode==TemporalMode::CleanPlate || mode==TemporalMode::VirtualND ||
           mode==TemporalMode::Transient) {
            std::array<double,3> sum{{0,0,0}};
            double ws=0;
            for(size_t k=0;k<n;++k) if(valid[k]) {
                float wt=mode==TemporalMode::VirtualND ? 1 :
                    detail::sample(t.weights[k],t.H,t.W,cy,cx);
                if(mode!=TemporalMode::VirtualND)
                    for(size_t c=0;c<3;++c) {
                        const float z=std::fabs(samples[k][c]-base[c])/
                            (t.options.reject_sigma*std::sqrt(2*detail::variance(b,base[c])));
                        if(z>1) wt=0;
                    }
                ws+=wt;
                for(size_t c=0;c<3;++c) sum[c]+=wt*samples[k][c];
            }
            if(ws>1e-9) for(size_t c=0;c<3;++c) base[c]=static_cast<float>(sum[c]/ws);
        }
        if(mode==TemporalMode::Glint || mode==TemporalMode::Reflection) {
            // Mean of the lowest quartile; a finite-noise correction reduces
            // the downward bias of the order statistic in a constant scene.
            for(size_t c=0;c<3;++c) {
                work.clear();
                for(size_t k=0;k<n;++k) if(valid[k] && samples[k][c]<0.995f) work.push_back(samples[k][c]);
                if(work.empty()) {
                    for(size_t k=0;k<n;++k) if(valid[k]) work.push_back(samples[k][c]);
                }
                if(work.empty()) continue;
                std::sort(work.begin(),work.end());
                const size_t count=std::max<size_t>(1,(work.size()+3)/4);
                const float lower=std::accumulate(work.begin(),work.begin()+static_cast<std::ptrdiff_t>(count),0.0f)/count;
                base[c]=std::min(base[c],lower+1.0f*std::sqrt(detail::variance(b,base[c])));
            }
        }
        if(mode==TemporalMode::Strobe) {
            for(size_t k:order)
                if(strobe_alpha[k]>0 && valid[k] && std::fabs(green[k]-med)>threshold)
                    for(size_t c=0;c<3;++c)
                        base[c]=base[c]*(1-strobe_alpha[k])+samples[k][c]*strobe_alpha[k];
        }
        if(layer) {
            // Select a whole RGB triplet by positive channel SNR; per-channel max
            // would synthesize a color that was never recorded.
            size_t best=n;
            float peak=t.options.reject_sigma;
            for(size_t k=0;k<n;++k) if(valid[k]) {
                float z=0;
                for(size_t c=0;c<3;++c) z=std::max(z,(samples[k][c]-base[c])/
                    std::sqrt(2*detail::variance(b,base[c])));
                if(z>peak) { peak=z; best=k; }
            }
            if(best<n)
                for(size_t c=0;c<3;++c) hi[c]=std::max(0.0f,samples[best][c]-base[c]);
            for(size_t c=0;c<3;++c) out.layer.rgb[3*i+c]=hi[c];
            out.gray_layer[i]=(hi[0]+2*hi[1]+hi[2])*0.25f;
        }
        for(size_t c=0;c<3;++c) out.image.rgb[3*i+c]=base[c];
    }
    };
    if(parallel_rows) parallel_rows(H,render_rows);
    else render_rows(0,H);
    return out;
}

namespace detail {
inline std::array<float,2> residual(const BurstView& b,const Geometry& g,size_t n,size_t i) {
    const int w=b.W1/2;
    const float y=static_cast<float>(i/w),x=static_cast<float>(i%w);
    const auto s=g.camera[n].source(y,x);
    return {{b.flows_fy[n][i]-(s[0]-y),b.flows_fx[n][i]-(s[1]-x)}};
}
inline bool reliable(const BurstView& b,size_t n,size_t i) {
    const int h=b.H1/2,w=b.W1/2;
    const float y=static_cast<float>(i/w),x=static_cast<float>(i%w);
    const float fy=b.flows_fy[n][i],fx=b.flows_fx[n][i],v=b.g_halfs[0][i];
    if(!std::isfinite(v)||v>=0.995f||!std::isfinite(b.mw_half[n][i])||
        b.mw_half[n][i]<0.25f||!inside(y+fy,x+fx,h,w)) return false;
    const float v1=sample(b.g_halfs[n],h,w,y+fy,x+fx);
    const float grad=gradient_mag(b.g_halfs[0],h,w,y,x);
    const float noise=std::sqrt(variance(b,v));
    return std::isfinite(v1) && grad>1.5f*noise &&
        std::fabs(v1-v)<4.5f*noise+0.38f*grad;
}
inline void aperture_metric(const BurstView& b,size_t i,double& myy,double& myx,double& mxx,double& s_1d) {
    const int h=b.H1/2,w=b.W1/2;
    const int yi=static_cast<int>(i/static_cast<size_t>(w));
    const int xi=static_cast<int>(i%static_cast<size_t>(w));
    double jyy=0,jyx=0,jxx=0;
    for(int dy=-2;dy<=2;++dy) {
        const int r=std::max(0,std::min(h-1,yi+dy));
        const int rm=std::max(0,r-1),rp=std::min(h-1,r+1);
        for(int dx=-2;dx<=2;++dx) {
            const int c=std::max(0,std::min(w-1,xi+dx));
            const int cm=std::max(0,c-1),cp=std::min(w-1,c+1);
            const double gy=0.5*(static_cast<double>(b.g_halfs[0][static_cast<size_t>(rp)*w+c])-
                                 static_cast<double>(b.g_halfs[0][static_cast<size_t>(rm)*w+c]));
            const double gx=0.5*(static_cast<double>(b.g_halfs[0][static_cast<size_t>(r)*w+cp])-
                                 static_cast<double>(b.g_halfs[0][static_cast<size_t>(r)*w+cm]));
            jyy+=gy*gy; jyx+=gy*gx; jxx+=gx*gx;
        }
    }
    myy=1.0; myx=0.0; mxx=1.0; s_1d=0.0;
    const double j_tr=jyy+jxx;
    if(j_tr>1e-12) {
        const double disc=std::hypot(jxx-jyy,2.0*jyx);
        const double anis=disc/j_tr;
        s_1d=static_cast<double>(clamp01(static_cast<float>((anis-0.78)/0.18)));
        if(s_1d>0.0) {
            const double inv_tr=1.0/j_tr;
            myy=(1.0-s_1d)+s_1d*(jyy*inv_tr);
            myx=s_1d*(jyx*inv_tr);
            mxx=(1.0-s_1d)+s_1d*(jxx*inv_tr);
        }
    }
}
inline float rho_at(const BurstView& b,const Geometry& g,size_t i,float* quality=nullptr) {
    const int h=b.H1/2,w=b.W1/2;
    const int yi=static_cast<int>(i/static_cast<size_t>(w));
    const int xi=static_cast<int>(i%static_cast<size_t>(w));
    const float v0=b.g_halfs[0][i];
    if(!std::isfinite(v0)||v0>=0.995f||
        gradient_mag(b.g_halfs[0],h,w,static_cast<float>(yi),static_cast<float>(xi))<=1.5f*std::sqrt(variance(b,v0))) {
        if(quality) *quality=0;
        return 0;
    }
    double myy=1.0,myx=0.0,mxx=1.0,s_1d=0.0;
    aperture_metric(b,i,myy,myx,mxx,s_1d);

    double dot=0,norm=0,norm_b2=0,energy=0,wsum=0;
    size_t count=0;
    for(size_t n=1;n<g.baseline.size();++n) if(reliable(b,n,i)) {
        const auto r=residual(b,g,n,i);
        if(!std::isfinite(r[0])||!std::isfinite(r[1])) continue;
        const double mw=clamp01(b.mw_half[n][i]);
        const double by=g.baseline[n][0],bx=g.baseline[n][1];
        const double mby=myy*by+myx*bx,mbx=myx*by+mxx*bx;
        const double mry=myy*r[0]+myx*r[1],mrx=myx*r[0]+mxx*r[1];
        dot+=mw*(r[0]*mby+r[1]*mbx);
        norm+=mw*(by*mby+bx*mbx);
        norm_b2+=mw*(by*by+bx*bx);
        energy+=mw*std::max(0.0,r[0]*mry+r[1]*mrx);
        wsum+=mw; ++count;
    }
    const double ap_ratio=norm_b2>1e-12 ? (norm/norm_b2) : 0.0;
    if(norm<=1e-12 || (s_1d>0.5 && ap_ratio<0.06)) {
        if(quality) *quality=0;
        return 0;
    }
    if(quality) {
        if(count>=2) {
            const double par=dot*dot/norm;
            const double perp=std::max(0.0,energy-par);
            const double reg_floor=wsum*(0.12*0.12)*std::min(1.0,ap_ratio);
            const double excess=std::max(0.0,perp-reg_floor);
            const float coh=clamp01(static_cast<float>((par+reg_floor)/(par+reg_floor+excess)));
            const float ap_gate=s_1d>0.5 ? clamp01(static_cast<float>((ap_ratio-0.06)/0.14)) : 1.0f;
            *quality=coh*ap_gate;
        } else {
            *quality=0;
        }
    }
    return static_cast<float>(dot/norm);
}
inline void complete_depth_multiscale(const BurstView& b,int H,int W,
                                      const std::vector<float>& seed_depth,
                                      const std::vector<float>& seed_conf,
                                      std::vector<float>& out_depth,
                                      std::vector<float>& out_conf) {
    const size_t p0=static_cast<size_t>(H)*static_cast<size_t>(W);
    out_depth.assign(p0,0); out_conf.assign(p0,0);
    std::vector<float> guide_samples;
    guide_samples.reserve(4096);
    const size_t g_stride=std::max<size_t>(1,(p0+4095)/4096);
    for(size_t i=0;i<p0;i+=g_stride)
        if(std::isfinite(b.g_halfs[0][i])) guide_samples.push_back(b.g_halfs[0][i]);
    const float g_med=quantile(guide_samples,0.50f);
    const float g_span=std::max(1e-4f,quantile(guide_samples,0.95f)-quantile(guide_samples,0.05f));
    const float bw_base=std::max(2.5f*std::sqrt(variance(b,g_med)),0.06f*g_span);

    struct PyrLevel {
        int h=0,w=0;
        std::vector<float> guide,num,den,depth,conf;
    };
    std::vector<PyrLevel> pyr;
    pyr.reserve(9);
    PyrLevel lv0;
    lv0.h=H; lv0.w=W; lv0.guide=b.g_halfs[0];
    lv0.num.resize(p0); lv0.den.resize(p0);
    for(int y=0;y<H;++y) {
        float nb_vals[25];
        for(int x=0;x<W;++x) {
            const size_t i=static_cast<size_t>(y)*static_cast<size_t>(W)+static_cast<size_t>(x);
            float w=clamp01(seed_conf[i]);
            float d=clamp01(seed_depth[i]);
            if(w>1e-4f) {
                const float gi=std::isfinite(lv0.guide[i]) ? lv0.guide[i] : g_med;
                int cnt=0;
                for(int dy=-4;dy<=4;dy+=2) {
                    const int yy=y+dy;
                    if(yy<0||yy>=H) continue;
                    for(int dx=-4;dx<=4;dx+=2) {
                        const int xx=x+dx;
                        if(xx<0||xx>=W) continue;
                        const size_t q=static_cast<size_t>(yy)*static_cast<size_t>(W)+static_cast<size_t>(xx);
                        if(seed_conf[q]<0.15f) continue;
                        const float gq=std::isfinite(lv0.guide[q]) ? lv0.guide[q] : g_med;
                        if(std::fabs(gq-gi)<=2.5f*bw_base) nb_vals[cnt++]=clamp01(seed_depth[q]);
                    }
                }
                if(cnt>=5) {
                    std::nth_element(nb_vals,nb_vals+cnt/2,nb_vals+cnt);
                    const float med=nb_vals[cnt/2];
                    if(std::fabs(d-med)>0.22f) { d=med; w*=0.20f; }
                }
            }
            lv0.den[i]=w;
            lv0.num[i]=w*d;
        }
    }
    pyr.push_back(std::move(lv0));

    while(pyr.size()<9 && (pyr.back().h>16 || pyr.back().w>16)) {
        const auto& fine=pyr.back();
        PyrLevel coarse;
        coarse.h=std::max(1,(fine.h+1)/2);
        coarse.w=std::max(1,(fine.w+1)/2);
        const size_t cp=static_cast<size_t>(coarse.h)*static_cast<size_t>(coarse.w);
        coarse.guide.assign(cp,0); coarse.num.assign(cp,0); coarse.den.assign(cp,0);
        const float inv_bw2=1.0f/(bw_base*bw_base);
        for(int cy=0;cy<coarse.h;++cy) for(int cx=0;cx<coarse.w;++cx) {
            double gs=0; int gc=0;
            for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
                const int fy=std::min(fine.h-1,2*cy+dy),fx=std::min(fine.w-1,2*cx+dx);
                const float gv=fine.guide[static_cast<size_t>(fy)*fine.w+fx];
                if(std::isfinite(gv)) { gs+=gv; ++gc; }
            }
            const float gm=gc>0 ? static_cast<float>(gs/gc) : g_med;
            coarse.guide[static_cast<size_t>(cy)*coarse.w+cx]=gm;
            double ns=0,ds=0,max_c=0;
            for(int dy=-1;dy<=2;++dy) for(int dx=-1;dx<=2;++dx) {
                const int fy=2*cy+dy,fx=2*cx+dx;
                if(fy<0||fy>=fine.h||fx<0||fx>=fine.w) continue;
                const size_t fi=static_cast<size_t>(fy)*fine.w+fx;
                const float fc=fine.den[fi];
                if(fc<=1e-6f) continue;
                const float sp=((dy==0||dy==1)?1.0f:0.5f)*((dx==0||dx==1)?1.0f:0.5f);
                const float dg=fine.guide[fi]-gm;
                const float rw=1.0f/(1.0f+dg*dg*inv_bw2);
                const double wt=static_cast<double>(sp)*static_cast<double>(rw);
                ns+=wt*fine.num[fi]; ds+=wt*fc;
                if(fc>max_c) max_c=fc;
            }
            const size_t ci=static_cast<size_t>(cy)*coarse.w+cx;
            if(ds>1e-8) {
                const float d_avg=static_cast<float>(ns/ds);
                const float c_eff=static_cast<float>(std::min(1.0,0.92*max_c));
                coarse.den[ci]=c_eff;
                coarse.num[ci]=d_avg*c_eff;
            }
        }
        pyr.push_back(std::move(coarse));
    }

    {
        auto& top=pyr.back();
        const size_t tp=static_cast<size_t>(top.h)*static_cast<size_t>(top.w);
        top.depth.assign(tp,0.5f); top.conf.assign(tp,0);
        double global_num=0,global_den=0;
        for(size_t i=0;i<tp;++i) {
            if(top.den[i]>1e-6f) {
                top.depth[i]=clamp01(top.num[i]/top.den[i]);
                top.conf[i]=top.den[i];
                global_num+=top.num[i]; global_den+=top.den[i];
            }
        }
        const float fallback=global_den>1e-8 ? clamp01(static_cast<float>(global_num/global_den)) : 0.5f;
        for(size_t i=0;i<tp;++i) if(top.conf[i]<=1e-6f) top.depth[i]=fallback;
        const float inv_bw2=1.0f/(4.0f*bw_base*bw_base);
        std::vector<float> next_d(tp),next_c(tp);
        for(int pass=0;pass<24;++pass) {
            next_d=top.depth; next_c=top.conf;
            for(int y=0;y<top.h;++y) for(int x=0;x<top.w;++x) {
                const size_t i=static_cast<size_t>(y)*top.w+x;
                if(top.den[i]>=0.5f) continue;
                double ns=top.num[i],ws=top.den[i],cs=top.den[i]*top.den[i];
                const float gv=top.guide[i];
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    if(dy==0&&dx==0) continue;
                    const int yy=y+dy,xx=x+dx;
                    if(yy<0||yy>=top.h||xx<0||xx>=top.w) continue;
                    const size_t q=static_cast<size_t>(yy)*top.w+xx;
                    if(top.conf[q]<=1e-6f) continue;
                    const float dg=gv-top.guide[q];
                    const double rw=1.0/(1.0+dg*dg*inv_bw2);
                    const double sp=(dy==0||dx==0) ? 0.5 : 0.35;
                    const double wt=sp*rw*top.conf[q];
                    ns+=wt*top.depth[q]; ws+=wt; cs+=wt*(0.88*top.conf[q]);
                }
                if(ws>1e-8) {
                    next_d[i]=clamp01(static_cast<float>(ns/ws));
                    next_c[i]=clamp01(static_cast<float>(std::max<double>(top.conf[i],cs/ws)));
                }
            }
            top.depth.swap(next_d); top.conf.swap(next_c);
        }
    }

    for(int lv=static_cast<int>(pyr.size())-2;lv>=0;--lv) {
        auto& cur=pyr[static_cast<size_t>(lv)];
        const auto& up=pyr[static_cast<size_t>(lv+1)];
        const size_t cp=static_cast<size_t>(cur.h)*static_cast<size_t>(cur.w);
        cur.depth.resize(cp); cur.conf.resize(cp);
        const float inv_bw2=1.0f/(bw_base*bw_base);
        for(int y=0;y<cur.h;++y) for(int x=0;x<cur.w;++x) {
            const size_t i=static_cast<size_t>(y)*cur.w+x;
            const float gv=std::isfinite(cur.guide[i]) ? cur.guide[i] : g_med;
            const float uy=std::max(0.0f,std::min(static_cast<float>(up.h-1),(y+0.5f)*0.5f-0.5f));
            const float ux=std::max(0.0f,std::min(static_cast<float>(up.w-1),(x+0.5f)*0.5f-0.5f));
            const int y0=static_cast<int>(uy),x0=static_cast<int>(ux);
            const int y1=std::min(up.h-1,y0+1),x1=std::min(up.w-1,x0+1);
            const float fy=uy-y0,fx=ux-x0;
            const float bw4[4]={(1-fy)*(1-fx),(1-fy)*fx,fy*(1-fx),fy*fx};
            const size_t idx4[4]={
                static_cast<size_t>(y0)*up.w+x0,static_cast<size_t>(y0)*up.w+x1,
                static_cast<size_t>(y1)*up.w+x0,static_cast<size_t>(y1)*up.w+x1};
            double ps=0,pw=0,pc=0;
            for(int k=0;k<4;++k) {
                const size_t qi=idx4[k];
                const float dg=gv-up.guide[qi];
                const double rw=1.0/(1.0+dg*dg*inv_bw2);
                const double wt=static_cast<double>(bw4[k])*rw*std::max(1e-4f,up.conf[qi]);
                ps+=wt*up.depth[qi]; pw+=wt; pc+=wt*up.conf[qi];
            }
            const float d_prior=pw>1e-10 ? static_cast<float>(ps/pw) : up.depth[idx4[0]];
            const float c_prior=pw>1e-10 ? static_cast<float>(0.88*pc/pw) : 0.1f;
            const float data_w=cur.den[i];
            const float prior_w=(data_w>=0.85f ? 0.04f : 0.35f)*std::max(0.05f,c_prior);
            cur.depth[i]=clamp01((cur.num[i]+prior_w*d_prior)/std::max(1e-8f,data_w+prior_w));
            cur.conf[i]=clamp01(std::max(data_w,c_prior));
        }
        std::vector<float> smooth_d=cur.depth;
        for(int y=0;y<cur.h;++y) for(int x=0;x<cur.w;++x) {
            const size_t i=static_cast<size_t>(y)*cur.w+x;
            if(lv==0 && cur.den[i]>=0.80f) continue;
            const float gv=std::isfinite(cur.guide[i]) ? cur.guide[i] : g_med;
            double ns=cur.depth[i]*std::max(0.1f,cur.conf[i]),ws=std::max(0.1f,cur.conf[i]);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                if(dy==0&&dx==0) continue;
                const int yy=y+dy,xx=x+dx;
                if(yy<0||yy>=cur.h||xx<0||xx>=cur.w) continue;
                const size_t q=static_cast<size_t>(yy)*cur.w+xx;
                const float dg=gv-(std::isfinite(cur.guide[q]) ? cur.guide[q] : g_med);
                const double rw=1.0/(1.0+dg*dg*inv_bw2);
                const double sp=(dy==0||dx==0) ? 0.5 : 0.25;
                const double wt=sp*rw*std::max(0.05f,cur.conf[q]);
                ns+=wt*cur.depth[q]; ws+=wt;
            }
            smooth_d[i]=clamp01(static_cast<float>(ns/ws));
        }
        cur.depth.swap(smooth_d);
    }
    out_depth=std::move(pyr[0].depth);
    out_conf=std::move(pyr[0].conf);
}
} // namespace detail

// Relative inverse depth is defined modulo an additive reference plane and a
// global sign/scale. A homography alone cannot identify the plane at infinity.
// The rank-one translational model is a measured small-FOV approximation.
inline Geometry analyze_geometry(const BurstView& b) {
    detail::validate(b);
    Geometry g;
    g.camera=fit_camera(b);
    const size_t n=b.frames_lin.size(),p=detail::pixels(b.H1/2,b.W1/2);
    g.baseline.resize(n,{{0,0}});
    g.gate.units="1x_px"; g.gate.threshold=1.5;
    if(n<3) { g.gate.reason="relative depth requires >=3 frames"; return g; }
    const size_t stride=std::max<size_t>(1,(p+4095)/4096);
    std::vector<size_t> indices;
    for(size_t i=0;i<p;i+=stride) {
        size_t count=0;
        for(size_t k=1;k<n;++k) count+=detail::reliable(b,k,i);
        if(count>=2) indices.push_back(i);
    }
    g.support_fraction=static_cast<float>(indices.size())/static_cast<float>((p+stride-1)/stride);
    if(indices.size()<24) { g.gate.reason="insufficient textured photometrically consistent flow support"; return g; }
    // Principal direction of the strongest frame initializes alternating rank-one LS.
    size_t strongest=1;
    double max_energy=0;
    std::array<float,2> axis{{0,1}};
    for(size_t k=1;k<n;++k) {
        double xx=0,xy=0,yy=0;
        for(size_t i:indices) if(detail::reliable(b,k,i)) {
            const auto r=detail::residual(b,g,k,i);
            xx+=r[1]*r[1]; xy+=r[0]*r[1]; yy+=r[0]*r[0];
        }
        const double eigen=0.5*(xx+yy+std::hypot(xx-yy,2*xy));
        if(eigen>max_energy) {
            max_energy=eigen; strongest=k;
            const double angle=0.5*std::atan2(2*xy,xx-yy);
            axis={{static_cast<float>(std::sin(angle)),static_cast<float>(std::cos(angle))}};
        }
    }
    std::vector<float> rho(indices.size()), rho_q(indices.size(), 1.0f);
    for(size_t j=0;j<indices.size();++j) {
        const auto r=detail::residual(b,g,strongest,indices[j]);
        rho[j]=r[0]*axis[0]+r[1]*axis[1];
    }
    for(int iter=0;iter<8;++iter) {
        g.baseline[0]={{0,0}};
        for(size_t k=1;k<n;++k) {
            double a00=0,a01=0,a11=0,b0=0,b1=0;
            for(size_t j=0;j<indices.size();++j) if(detail::reliable(b,k,indices[j])) {
                const double qj=(iter==0)?1.0:static_cast<double>(rho_q[j]);
                if(qj<0.05) continue;
                double myy,myx,mxx,s_1d;
                detail::aperture_metric(b,indices[j],myy,myx,mxx,s_1d);
                const auto r=detail::residual(b,g,k,indices[j]);
                const double w=detail::clamp01(b.mw_half[k][indices[j]])*qj;
                const double w2=w*static_cast<double>(rho[j])*static_cast<double>(rho[j]);
                const double wr=w*static_cast<double>(rho[j]);
                a00+=w2*myy; a01+=w2*myx; a11+=w2*mxx;
                b0+=wr*(myy*r[0]+myx*r[1]);
                b1+=wr*(myx*r[0]+mxx*r[1]);
            }
            const double det=a00*a11-a01*a01;
            if(det>1e-12*(a00+a11)*(a00+a11)+1e-18)
                g.baseline[k]={{static_cast<float>((a11*b0-a01*b1)/det),static_cast<float>((a00*b1-a01*b0)/det)}};
            else if(a00+a11>1e-12)
                g.baseline[k]={{static_cast<float>(b0/(a00+a11)),static_cast<float>(b1/(a00+a11))}};
            else g.baseline[k]={{0,0}};
        }
        // Normalize by peak-to-peak camera sweep across the burst so disparity_span_px
        // is invariant to whether the sharpest anchor frame lies at an end or middle.
        float scale=0;
        for(size_t a=0;a<n;++a)
            for(size_t c=a+1;c<n;++c)
                scale=std::max(scale,std::hypot(g.baseline[a][0]-g.baseline[c][0],
                                                g.baseline[a][1]-g.baseline[c][1]));
        if(scale<1e-8f) break;
        for(auto& v:g.baseline) { v[0]/=scale; v[1]/=scale; }
        for(size_t j=0;j<indices.size();++j) rho[j]=detail::rho_at(b,g,indices[j],&rho_q[j]);
    }
    std::vector<float> obs_rho;
    obs_rho.reserve(indices.size());
    for(size_t j=0;j<indices.size();++j) if(rho_q[j]>=0.25f) obs_rho.push_back(rho[j]);
    if(obs_rho.size()<16) obs_rho=rho;
    g.rho_low=detail::quantile(obs_rho,0.03f); g.rho_high=detail::quantile(obs_rho,0.97f);
    const float rho_med=detail::quantile(obs_rho,0.50f);
    g.disparity_span_px=2*(g.rho_high-g.rho_low);
    double e_par=0,e_perp_excess=0,sp_num=0,sp_den_a=0,sp_den_b=0;
    const int hh=b.H1/2,hw=b.W1/2;
    for(size_t j=0;j<indices.size();++j) {
        if(rho_q[j]<0.25f) continue;
        const size_t idx=indices[j];
        double myy,myx,mxx,s_1d;
        detail::aperture_metric(b,idx,myy,myx,mxx,s_1d);
        for(size_t k=1;k<n;++k) if(detail::reliable(b,k,idx)) {
            const auto r=detail::residual(b,g,k,idx);
            const double mw=detail::clamp01(b.mw_half[k][idx])*static_cast<double>(rho_q[j]);
            const double py=rho[j]*g.baseline[k][0],px=rho[j]*g.baseline[k][1];
            const double ey=r[0]-py,ex=r[1]-px;
            const double par2=py*(myy*py+myx*px)+px*(myx*py+mxx*px);
            const double perp2=std::max(0.0,ey*(myy*ey+myx*ex)+ex*(myx*ey+mxx*ex));
            e_par+=mw*par2;
            e_perp_excess+=mw*std::max(0.0,perp2-0.28*0.28);
        }
        const int y=static_cast<int>(idx/static_cast<size_t>(hw));
        const int x=static_cast<int>(idx%static_cast<size_t>(hw));
        const int cand_dx[6]={1,2,0,0,-1,-2};
        const int cand_dy[6]={0,0,1,2,0,0};
        for(int d=0;d<6;++d) {
            const int ny=y+cand_dy[d],nx=x+cand_dx[d];
            if(ny<0||ny>=hh||nx<0||nx>=hw) continue;
            const size_t nb_idx=static_cast<size_t>(ny)*static_cast<size_t>(hw)+static_cast<size_t>(nx);
            float nb_q=0;
            const float nb_rho=detail::rho_at(b,g,nb_idx,&nb_q);
            if(nb_q>=0.25f) {
                const double w_sp=static_cast<double>(rho_q[j])*static_cast<double>(nb_q);
                const double da=static_cast<double>(rho[j]-rho_med);
                const double db=static_cast<double>(nb_rho-rho_med);
                sp_num+=w_sp*da*db; sp_den_a+=w_sp*da*da; sp_den_b+=w_sp*db*db;
                break;
            }
        }
    }
    const float sp_corr=(sp_den_a>1e-8 && sp_den_b>1e-8) ?
        detail::clamp01(static_cast<float>(sp_num/std::sqrt(sp_den_a*sp_den_b))) : 0.0f;
    const float raw_coh=e_par>1e-8 ? detail::clamp01(static_cast<float>(e_par/(e_par+e_perp_excess))) : 0.0f;
    g.rank1_coherence=raw_coh*std::min(1.0f,sp_corr/0.50f);
    g.gate.measured=g.disparity_span_px;
    g.gate.confidence=g.rank1_coherence*std::min(1.0f,g.support_fraction*2.0f);
    g.gate.observable=g.disparity_span_px>=1.5f && g.rank1_coherence>=0.55f &&
        sp_corr>=0.35f && g.support_fraction>=0.05f;
    g.gate.reason=g.gate.observable ?
        "relative inverse depth; reference-plane and near/far sign gauge; coherent residual parallax" :
        "requires >=1.5 1x pixels (3 2x pixels) robust disparity span, epipolar coherence >=0.55, textured support >=0.05";
    if (g.gate.observable &&
        (b.focus_distance_m > 0.0f || b.focus_is_infinity || b.distant_by_optics)) {
        struct SharpSample { float rho; float shf; bool border; };
        std::vector<SharpSample> edge_samples;
        edge_samples.reserve(indices.size());
        const float y0_b = 0.12f * static_cast<float>(hh), y1_b = 0.88f * static_cast<float>(hh);
        const float x0_b = 0.12f * static_cast<float>(hw), x1_b = 0.88f * static_cast<float>(hw);
        for (size_t j = 0; j < indices.size(); ++j) {
            if (rho_q[j] < 0.50f) continue;
            const size_t idx = indices[j];
            const int yi = static_cast<int>(idx / static_cast<size_t>(hw));
            const int xi = static_cast<int>(idx % static_cast<size_t>(hw));
            if (yi <= 0 || yi + 1 >= hh || xi <= 0 || xi + 1 >= hw) continue;
            const float v = b.g_halfs[0][idx];
            const float nv = detail::variance(b, v);
            const float nsd = std::sqrt(nv);
            const float gmag = detail::gradient_mag(b.g_halfs[0], hh, hw, static_cast<float>(yi), static_cast<float>(xi));
            if (!(gmag > 4.0f * nsd) || !std::isfinite(gmag)) continue;
            const float lap = b.g_halfs[0][static_cast<size_t>(yi) * hw + (xi - 1)] +
                              b.g_halfs[0][static_cast<size_t>(yi) * hw + (xi + 1)] +
                              b.g_halfs[0][static_cast<size_t>(yi - 1) * hw + xi] +
                              b.g_halfs[0][static_cast<size_t>(yi + 1) * hw + xi] - 4.0f * v;
            const float shf = std::fabs(lap) / (gmag + 3.0f * nsd);
            if (!std::isfinite(shf)) continue;
            const bool is_b = (xi < x0_b || xi > x1_b || yi < y0_b || yi > y1_b);
            edge_samples.push_back({rho[j], shf, is_b});
        }

        auto flip_depth_sign = [&]() {
            for (auto& bv : g.baseline) { bv[0] = -bv[0]; bv[1] = -bv[1]; }
            for (auto& es : edge_samples) es.rho = -es.rho;
            const float old_lo = g.rho_low;
            g.rho_low = -g.rho_high;
            g.rho_high = -old_lo;
        };

        if (edge_samples.size() >= 24u) {
            double mr = 0.0, ms = 0.0;
            for (const auto& es : edge_samples) { mr += es.rho; ms += es.shf; }
            mr /= static_cast<double>(edge_samples.size());
            ms /= static_cast<double>(edge_samples.size());
            double cov = 0.0, vr = 0.0, vs = 0.0;
            for (const auto& es : edge_samples) {
                const double dr = es.rho - mr, ds = es.shf - ms;
                cov += dr * ds; vr += dr * dr; vs += ds * ds;
            }
            const double corr = (vr > 1e-12 && vs > 1e-12) ? (cov / std::sqrt(vr * vs)) : 0.0;
            if (b.focus_is_infinity || b.distant_by_optics) {
                if (corr > 0.20) {
                    flip_depth_sign();
                    g.sign_calibrated = true;
                } else if (corr < -0.20) {
                    g.sign_calibrated = true;
                }
            } else if (b.hyperfocal_m > 0.0f && b.focus_distance_m > 0.0f &&
                       b.focus_distance_m < 0.35f * b.hyperfocal_m) {
                std::vector<float> int_shf, brd_shf;
                for (const auto& es : edge_samples) {
                    (es.border ? brd_shf : int_shf).push_back(es.shf);
                }
                if (brd_shf.size() >= 6u && int_shf.size() >= 12u) {
                    const float top_int = detail::quantile(int_shf, 0.75f);
                    const double mean_brd = std::accumulate(brd_shf.begin(), brd_shf.end(), 0.0) /
                                            static_cast<double>(brd_shf.size());
                    if (mean_brd < static_cast<double>(top_int) * 0.85 && std::fabs(corr) >= 0.25) {
                        if (corr < 0.0) flip_depth_sign();
                        g.sign_calibrated = true;
                    }
                }
            }
        }
        if (edge_samples.size() >= 16u) {
            std::vector<float> all_shf;
            all_shf.reserve(edge_samples.size());
            for (const auto& es : edge_samples) all_shf.push_back(es.shf);
            const float shf_thr = detail::quantile(all_shf, 0.75f);
            const float span = std::max(1e-6f, g.rho_high - g.rho_low);
            double wsum = 0.0, dsum = 0.0;
            for (const auto& es : edge_samples) {
                if (es.shf >= shf_thr) {
                    const float dnorm = detail::clamp01((es.rho - g.rho_low) / span);
                    const double w = static_cast<double>(es.shf - shf_thr) + 1e-4;
                    dsum += w * dnorm;
                    wsum += w;
                }
            }
            if (wsum > 0.0) {
                g.focus_plane_depth = detail::clamp01(static_cast<float>(dsum / wsum));
                g.has_focus_plane = true;
            }
        }
    }
    return g;
}

// Names: depth, depth_confidence, defocus, specular, sigma, coherence,
// velocity, velocity_x, velocity_y, flicker, turbulence.
// Advanced: normal_x, normal_y (relative graph normal, z=1), occlusion (reference alpha).
// Only requested map storage is allocated. Ineligible fields are zero with a gate.
inline FieldResult compute_fields(const BurstView& b,const std::vector<std::string>& requested,
                                  const ParallelRows& parallel_rows = {}) {
    detail::validate(b);
    FieldResult out;
    out.H=b.H1/2; out.W=b.W1/2;
    const size_t p=detail::pixels(out.H,out.W),n=b.frames_lin.size();
    const std::vector<std::string> names={"depth","depth_confidence","defocus","specular","sigma",
        "coherence","velocity","velocity_x","velocity_y","flicker","turbulence",
        "normal_x","normal_y","occlusion"};
    for(const auto& name:requested) {
        if(std::find(names.begin(),names.end(),name)==names.end())
            throw std::invalid_argument("Unknown suite field: "+name);
        out.maps.emplace(name,std::vector<float>(p,0));
        out.gates.emplace(name,Gate{});
    }
    if(requested.empty()) return out;
    const auto want=[&](const char* name){ return out.maps.count(name)!=0; };
    const bool normals=want("normal_x")||want("normal_y"),occlusion=want("occlusion");
    const bool depth=want("depth")||want("depth_confidence")||normals||occlusion;
    const bool velocity=want("velocity")||want("velocity_x")||want("velocity_y");
    const bool geo=depth||velocity||want("turbulence");
    if(geo) out.geometry=analyze_geometry(b);
    std::vector<float> depth_support,latent_depth,seed_depth,seed_conf;
    std::vector<float>* depth_values=nullptr;
    if(out.geometry.gate.observable && (want("depth")||normals||occlusion)) {
        depth_support.assign(p,0);
        if(normals||occlusion) { latent_depth.assign(p,0); depth_values=&latent_depth; }
        if(want("depth")) { seed_depth.assign(p,0); seed_conf.assign(p,0); }
    }
    const bool timed=b.times_seconds && n>=2 &&
        (*std::max_element(b.times_seconds->begin(),b.times_seconds->end())-
         *std::min_element(b.times_seconds->begin(),b.times_seconds->end())>1e-6);
    bool focused=b.focus_positions && n>=3;
    if(focused) {
        for(float f:*b.focus_positions) focused=focused&&std::isfinite(f);
        focused=focused&&(*std::max_element(b.focus_positions->begin(),b.focus_positions->end())-
                          *std::min_element(b.focus_positions->begin(),b.focus_positions->end())>1e-6f);
    }
    for(auto& pair:out.gates) {
        auto& gate=pair.second;
        gate.observable=n>=2; gate.measured=static_cast<double>(n); gate.threshold=2;
        gate.units="camera_linear"; gate.confidence=1;
        gate.reason="aligned burst statistic";
        if(pair.first=="depth"||pair.first=="depth_confidence") {
            gate=out.geometry.gate;
            gate.units=pair.first=="depth" ? "relative_inverse_depth_0_1_sign_gauge" : "unit_interval";
        } else if(pair.first=="normal_x"||pair.first=="normal_y") {
            gate.observable=false; gate.measured=0; gate.threshold=1; gate.confidence=0;
            gate.units="relative_inverse_depth_per_1x_pixel";
            gate.reason="relative graph normal (-depth gradient,z=1); >=1 textured conditioned local plane; reference-plane/sign gauge";
        } else if(pair.first=="occlusion") {
            gate.observable=false; gate.measured=0; gate.threshold=2; gate.confidence=0;
            gate.units="relative_foreground_mixing_alpha_0_1";
            gate.reason="reference alpha; camera-plane background, residual foreground; local separation >=2 1x pixels, visible endpoints and >6sigma contrast";
        } else if(pair.first=="defocus") {
            gate.observable=focused; gate.units="signed_HF_energy_per_focus_unit";
            gate.reason="signed local HF-energy slope requires varying calibrated focus positions";
        } else if(pair.first=="velocity"||pair.first=="velocity_x"||pair.first=="velocity_y") {
            gate.observable=n>=3; gate.threshold=3;
            gate.units=timed ? "1x_px_per_second" :
                (b.capture_order ? "1x_px_per_input_frame_index" : "1x_px_per_frame");
            gate.reason="linear camera-compensated image-motion slope; subtract observable parallax";
        } else if(pair.first=="flicker") {
            gate.observable=n>=4 && b.exposure_seconds>0 && b.exposure_seconds<=1.0f/99.0f;
            gate.threshold=1.0/99.0; gate.measured=b.exposure_seconds; gate.units="fractional_RMS";
            gate.reason="noise-corrected temporal modulation; frequency subject to sampling alias";
        } else if(pair.first=="turbulence") {
            gate.observable=n>=5 && b.focal_length_mm>=85 && b.distant_scene;
            gate.threshold=0.04; gate.units="1x_px_RMS";
            gate.reason="BOS residual RMS after camera, linear drift and observable parallax; floor 0.04px";
        } else if(pair.first=="coherence") {
            gate.observable=n>=4; gate.threshold=4; gate.units="gradient_direction_coherence";
            gate.reason="aligned gradient-vector coherence; subpixel phase coverage reported in confidence";
        } else if(pair.first=="specular") {
            gate.units="camera_linear_positive_excursion";
            gate.reason="positive temporal excess above lower quartile and 3.5 sigma";
        } else if(pair.first=="sigma") {
            gate.observable=true; gate.units="camera_linear_standard_deviation";
            gate.reason="independent raw-sample equivalent shot/read sigma of weighted mean";
        }
    }
    std::vector<size_t> row_phase_good(static_cast<size_t>(out.H),0);
    std::vector<float> row_max_turb(static_cast<size_t>(out.H),0);
    const RowTask eval_rows=[&](int begin,int end) {
        if(begin<0||end<begin||end>out.H) throw std::invalid_argument("Invalid field row range");
        std::vector<float> values,lap,focus,ts,ry,rx;
        values.reserve(n); lap.reserve(n); focus.reserve(n); ts.reserve(n); ry.reserve(n); rx.reserve(n);
        for(int yi=begin;yi<end;++yi) {
            size_t local_phase=0;
            float local_turb=0;
            for(int xi=0;xi<out.W;++xi) {
                const size_t i=static_cast<size_t>(yi)*out.W+xi;
                const float y=static_cast<float>(yi),x=static_cast<float>(xi);
                values.clear(); lap.clear(); focus.clear(); ts.clear(); ry.clear(); rx.clear();
                double wsum=0,varsum=0,gxsum=0,gysum=0,gnorm=0,noise_sum=0;
                unsigned phases=0;
                float rq=0,rho=0;
                if(geo && !out.geometry.baseline.empty())
                    rho=detail::rho_at(b,out.geometry,i,&rq);
                for(size_t k=0;k<n;++k) {
                    const float fy=b.flows_fy[k][i],fx=b.flows_fx[k][i];
                    if(!detail::inside(y+fy,x+fx,out.H,out.W)) continue;
                    const float v=detail::sample(b.g_halfs[k],out.H,out.W,y+fy,x+fx);
                    if(!std::isfinite(v)) continue;
                    values.push_back(v);
                    const float wt=std::isfinite(b.mw_half[k][i]) ? detail::clamp01(b.mw_half[k][i]) : 0;
                    const float nv=detail::variance(b,v);
                    wsum+=wt; varsum+=wt*wt*nv; noise_sum+=nv;
                    if(want("coherence") || (want("defocus")&&focused)) {
                        const float l=detail::sample(b.g_halfs[k],out.H,out.W,y+fy,x+fx-1);
                        const float r=detail::sample(b.g_halfs[k],out.H,out.W,y+fy,x+fx+1);
                        const float u=detail::sample(b.g_halfs[k],out.H,out.W,y+fy-1,x+fx);
                        const float d=detail::sample(b.g_halfs[k],out.H,out.W,y+fy+1,x+fx);
                        const float gx=(r-l)*0.5f,gy=(d-u)*0.5f;
                        gxsum+=wt*gx; gysum+=wt*gy; gnorm+=wt*std::hypot(gx,gy);
                        if(focused && want("defocus")) {
                            const float hf=l+r+u+d-4*v;
                            lap.push_back(std::max(0.0f,hf*hf-20*nv)); focus.push_back((*b.focus_positions)[k]);
                        }
                    }
                    if(want("coherence")) {
                        const float fx1=2.0f*fx,fy1=2.0f*fy;
                        const int px=static_cast<int>(std::floor(2*(fx1-std::floor(fx1))));
                        const int py=static_cast<int>(std::floor(2*(fy1-std::floor(fy1))));
                        phases|=1u<<static_cast<unsigned>(2*py+px);
                    }
                    if(velocity||want("turbulence")) {
                        auto r=detail::residual(b,out.geometry,k,i);
                        if(out.geometry.gate.observable && rq>=0.50f) {
                            r[0]-=rho*out.geometry.baseline[k][0]; r[1]-=rho*out.geometry.baseline[k][1];
                        }
                        if(std::isfinite(r[0])&&std::isfinite(r[1])) {
                            ry.push_back(2*r[0]); rx.push_back(2*r[1]);
                            float time=static_cast<float>(k);
                            if(timed) time=static_cast<float>((*b.times_seconds)[k]-(*b.times_seconds)[0]);
                            else if(b.capture_order) {
                                const size_t index=(*b.capture_order)[k],anchor=(*b.capture_order)[0];
                                time=index>=anchor ? static_cast<float>(index-anchor) : -static_cast<float>(anchor-index);
                            }
                            ts.push_back(time);
                        }
                    }
                }
                const size_t count=values.size();
                if(want("sigma")) out.maps["sigma"][i]=wsum>1e-9 ?
                    static_cast<float>(std::sqrt(varsum)/wsum) : std::sqrt(detail::variance(b,b.g_halfs[0][i]));
                if(depth && out.geometry.gate.observable && rq>=0.30f) {
                    const float relative=(rho-out.geometry.rho_low)/
                        std::max(1e-6f,out.geometry.rho_high-out.geometry.rho_low);
                    if(!seed_depth.empty()) {
                        const float g0_v=b.g_halfs[0][i];
                        const float g0_mag=detail::gradient_mag(b.g_halfs[0],out.H,out.W,y,x);
                        const float g0_sig=std::sqrt(detail::variance(b,g0_v));
                        const float crlb_w=detail::clamp01((g0_mag/std::max(1e-8f,g0_sig)-1.5f)/2.8f);
                        seed_depth[i]=detail::clamp01(relative);
                        seed_conf[i]=(rq>=0.85f ? rq : 0.65f*rq*rq)*crlb_w;
                    }
                    if(want("depth_confidence") && seed_depth.empty()) out.maps["depth_confidence"][i]=rq;
                    if(rq>=0.85f) {
                        // Local differential geometry uses the unclipped depth coordinate.
                        if(depth_values) (*depth_values)[i]=relative;
                        if(!depth_support.empty()) depth_support[i]=rq;
                    }
                }
                if(want("coherence")) {
                    const int bits=std::popcount(static_cast<unsigned>(phases&0xFu));
                    local_phase+=bits>=3;
                    const double nsd=std::sqrt(varsum);
                    if(out.gates["coherence"].observable && gnorm>1.2*nsd) {
                        const float gw=detail::clamp01(static_cast<float>((gnorm-1.2*nsd)/std::max(1e-12,1.6*nsd)));
                        out.maps["coherence"][i]=gw*detail::clamp01(static_cast<float>(std::hypot(gxsum,gysum)/gnorm));
                    }
                }
                if(want("specular")&&count>=2) {
                    const float lo=detail::quantile(values,0.25f);
                    const float hi=*std::max_element(values.begin(),values.end());
                    out.maps["specular"][i]=std::max(0.0f,hi-lo-3.5f*std::sqrt(2*detail::variance(b,lo)));
                }
                if(want("flicker")&&out.gates["flicker"].observable && count>=4) {
                    const double mean=std::accumulate(values.begin(),values.end(),0.0)/count;
                    double v=0;
                    for(float z:values) v+=(z-mean)*(z-mean);
                    v=std::max(0.0,v/(count-1)-noise_sum/count);
                    out.maps["flicker"][i]=mean>3*std::sqrt(noise_sum/count) ?
                        static_cast<float>(std::sqrt(v)/mean) : 0;
                }
                if(want("defocus")&&focused && lap.size()>=3) {
                    const double mf=std::accumulate(focus.begin(),focus.end(),0.0)/focus.size();
                    const double ml=std::accumulate(lap.begin(),lap.end(),0.0)/lap.size();
                    double cov=0,vf=0;
                    for(size_t j=0;j<lap.size();++j) { cov+=(focus[j]-mf)*(lap[j]-ml); vf+=(focus[j]-mf)*(focus[j]-mf); }
                    out.maps["defocus"][i]=vf>1e-12 ? static_cast<float>(cov/vf) : 0;
                }
                if((velocity||want("turbulence"))&&ts.size()>=3) {
                    const double mt=std::accumulate(ts.begin(),ts.end(),0.0)/ts.size();
                    const double my=std::accumulate(ry.begin(),ry.end(),0.0)/ry.size();
                    const double mx=std::accumulate(rx.begin(),rx.end(),0.0)/rx.size();
                    double vy=0,vx=0,den=0;
                    for(size_t j=0;j<ts.size();++j) {
                        vy+=(ts[j]-mt)*(ry[j]-my); vx+=(ts[j]-mt)*(rx[j]-mx); den+=(ts[j]-mt)*(ts[j]-mt);
                    }
                    if(den>1e-12) { vy/=den; vx/=den; } else { vy=0; vx=0; }
                    if(want("velocity")) out.maps["velocity"][i]=static_cast<float>(std::hypot(vx,vy));
                    if(want("velocity_y")) out.maps["velocity_y"][i]=static_cast<float>(vy);
                    if(want("velocity_x")) out.maps["velocity_x"][i]=static_cast<float>(vx);
                    if(want("turbulence")&&out.gates["turbulence"].observable && detail::reliable(b,0,i)) {
                        double e=0;
                        for(size_t j=0;j<ts.size();++j) {
                            const double ey=ry[j]-my-vy*(ts[j]-mt),ex=rx[j]-mx-vx*(ts[j]-mt);
                            e+=ey*ey+ex*ex;
                        }
                        const float rms=static_cast<float>(std::sqrt(std::max(0.0,e/(ts.size()-2)-0.04*0.04)));
                        out.maps["turbulence"][i]=rms; local_turb=std::max(local_turb,rms);
                    }
                }
            }
            row_phase_good[static_cast<size_t>(yi)]=local_phase;
            row_max_turb[static_cast<size_t>(yi)]=local_turb;
        }
    };
    if(parallel_rows) parallel_rows(out.H,eval_rows);
    else eval_rows(0,out.H);
    size_t phase_good=0;
    float max_turbulence=0;
    for(int yi=0;yi<out.H;++yi) {
        phase_good+=row_phase_good[static_cast<size_t>(yi)];
        max_turbulence=std::max(max_turbulence,row_max_turb[static_cast<size_t>(yi)]);
    }
    if(want("coherence")) out.gates["coherence"].confidence=static_cast<double>(phase_good)/p;
    if(normals && depth_values) {
        const auto& depths=*depth_values;
        const double rho_span_sq=std::max(1e-4,double(out.geometry.rho_high-out.geometry.rho_low)*
            double(out.geometry.rho_high-out.geometry.rho_low));
        size_t supported=0;
        double confidence=0;
        for(int y=2;y<out.H-2;++y) for(int x=2;x<out.W-2;++x) {
            const size_t i=static_cast<size_t>(y)*out.W+x;
            if(depth_support[i]<0.85f||!detail::reliable(b,0,i)) continue;
            // Weighted plane z=a+bx+cy on a 5x5 window. Coordinates are RAW
            // pixels, although the field samples lie on the half grid.
            double sw=0,sx=0,sy=0,sz=0,sxx=0,sxy=0,syy=0,sxz=0,syz=0,szz=0;
            size_t count=0;
            for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                const size_t j=static_cast<size_t>(y+dy)*out.W+x+dx;
                if(depth_support[j]<0.85f) continue;
                const double w=depth_support[j]/(1+dx*dx+dy*dy);
                const double u=2*dx,v=2*dy,z=depths[j];
                sw+=w; sx+=w*u; sy+=w*v; sz+=w*z;
                sxx+=w*u*u; sxy+=w*u*v; syy+=w*v*v;
                sxz+=w*u*z; syz+=w*v*z; szz+=w*z*z; ++count;
            }
            if(count<6||sw<=0) continue;
            const double xx=sxx-sx*sx/sw,xy=sxy-sx*sy/sw,yy=syy-sy*sy/sw;
            const double xz=sxz-sx*sz/sw,yz=syz-sy*sz/sw,zz=szz-sz*sz/sw;
            const double det=xx*yy-xy*xy;
            if(det<0.01*xx*yy||xx<1||yy<1) continue;
            const double gx=(xz*yy-yz*xy)/det,gy=(yz*xx-xz*xy)/det;
            const double error=std::max(0.0,(zz-gx*xz-gy*yz)/sw);
            const double max_err=std::max(0.0016,std::min(0.025,4.0*detail::variance(b,b.g_halfs[0][i])/rho_span_sq));
            // Reject depth discontinuities that a single local plane cannot fit.
            if(error>max_err) continue;
            if(want("normal_x")) out.maps["normal_x"][i]=static_cast<float>(-gx);
            if(want("normal_y")) out.maps["normal_y"][i]=static_cast<float>(-gy);
            ++supported;
            confidence+=depth_support[i]*(static_cast<double>(count)/25)*std::exp(-error/max_err);
        }
        for(const char* name:{"normal_x","normal_y"}) if(want(name)) {
            out.gates[name].observable=supported>0;
            out.gates[name].measured=static_cast<double>(supported);
            out.gates[name].confidence=confidence/p;
        }
    }
    if(occlusion && depth_values) {
        const auto& depths=*depth_values;
        const float rho_span=out.geometry.rho_high-out.geometry.rho_low;
        std::vector<float> camera_values;
        camera_values.reserve(n);
        size_t supported=0;
        double confidence=0;
        float max_separation=0;
        for(int y=0;y<out.H;++y) for(int x=0;x<out.W;++x) {
            const size_t i=static_cast<size_t>(y)*out.W+x;
            float near_rho=0,far_rho=std::numeric_limits<float>::max();
            for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
                const int yy=y+dy,xx=x+dx;
                if(yy<0||yy>=out.H||xx<0||xx>=out.W) continue;
                const size_t j=static_cast<size_t>(yy)*out.W+xx;
                if(depth_support[j]<0.85f) continue;
                const float magnitude=std::fabs(out.geometry.rho_low+depths[j]*rho_span);
                near_rho=std::max(near_rho,magnitude); far_rho=std::min(far_rho,magnitude);
            }
            const float separation=2*(near_rho-far_rho);
            if(separation<2) continue;
            const float split=0.5f*(near_rho+far_rho);
            double luminance[2]={},rho_sum[2]={},rho_sq[2]={},weight[2]={};
            size_t count[2]={};
            for(int dy=-4;dy<=4;++dy) for(int dx=-4;dx<=4;++dx) {
                const int yy=y+dy,xx=x+dx;
                if(yy<0||yy>=out.H||xx<0||xx>=out.W) continue;
                const size_t j=static_cast<size_t>(yy)*out.W+xx;
                const float v=b.g_halfs[0][j];
                if(depth_support[j]<0.85f||!std::isfinite(v)||v>=0.995f) continue;
                const float magnitude=std::fabs(out.geometry.rho_low+depths[j]*rho_span);
                const size_t layer=magnitude>split ? 1 : 0;
                const double w=depth_support[j]/(1+dx*dx+dy*dy);
                luminance[layer]+=w*v; rho_sum[layer]+=w*magnitude;
                rho_sq[layer]+=w*magnitude*magnitude; weight[layer]+=w; ++count[layer];
            }
            if(count[0]<3||count[1]<3) continue;
            const double spread0=std::max(0.0,rho_sq[0]/weight[0]-std::pow(rho_sum[0]/weight[0],2));
            const double spread1=std::max(0.0,rho_sq[1]/weight[1]-std::pow(rho_sum[1]/weight[1],2));
            const double gap=rho_sum[1]/weight[1]-rho_sum[0]/weight[0];
            if(std::sqrt(std::max(spread0,spread1))>0.2*gap) continue;
            const float reference=b.g_halfs[0][i];
            if(!std::isfinite(reference)||reference>=0.995f) continue;
            const float sigma=std::sqrt(detail::variance(b,reference));
            const double polarity=luminance[1]/weight[1]-luminance[0]/weight[0];
            if(std::fabs(polarity)<6*sigma) continue;
            camera_values.clear();
            for(size_t k=0;k<n;++k) {
                const auto s=out.geometry.camera[k].source(static_cast<float>(y),static_cast<float>(x));
                if(!detail::inside(s[0],s[1],out.H,out.W)) continue;
                const float v=detail::sample(b.g_halfs[k],out.H,out.W,s[0],s[1]);
                if(std::isfinite(v)&&v<0.995f) camera_values.push_back(v);
            }
            if(camera_values.size()<3) continue;
            const auto range=std::minmax_element(camera_values.begin(),camera_values.end());
            const float contrast=*range.second-*range.first;
            if(contrast<6*sigma || contrast<0.5*std::fabs(polarity)) continue;
            const float bg=polarity>0 ? *range.first : *range.second;
            const float fg=polarity>0 ? *range.second : *range.first;
            size_t visible=0,covered=0;
            const float tolerance=std::max(3.5f*sigma,0.1f*contrast);
            for(float v:camera_values) {
                visible+=std::fabs(v-bg)<=tolerance; covered+=std::fabs(v-fg)<=tolerance;
            }
            if(visible==0||covered==0) continue;
            out.maps["occlusion"][i]=detail::clamp01((reference-bg)/(fg-bg));
            ++supported; max_separation=std::max(max_separation,separation);
            confidence+=(contrast/(contrast+6*sigma))*
                (static_cast<double>(visible+covered)/camera_values.size());
        }
        out.gates["occlusion"].observable=supported>0;
        out.gates["occlusion"].measured=max_separation;
        out.gates["occlusion"].confidence=confidence/p;
    }
    if(want("depth") && !seed_depth.empty()) {
        std::vector<float> dense_depth,dense_conf;
        detail::complete_depth_multiscale(b,out.H,out.W,seed_depth,seed_conf,dense_depth,dense_conf);
        out.maps["depth"]=std::move(dense_depth);
        if(want("depth_confidence")) out.maps["depth_confidence"]=std::move(dense_conf);
    }
    if(want("turbulence")) {
        out.gates["turbulence"].measured=max_turbulence;
        out.gates["turbulence"].observable=out.gates["turbulence"].observable&&max_turbulence>0.04f;
        if(!out.gates["turbulence"].observable)
            std::fill(out.maps["turbulence"].begin(),out.maps["turbulence"].end(),0);
    }
    return out;
}

struct RefocusOptions {
    float focus_depth = 0.5f;       // Same relative inverse-depth coordinates as map.
    float radius_px = 12.0f;        // Output-pixel disk radius per unit depth difference.
    int max_radius = 64;
};

// Symmetric disk exchange is a doubly-stochastic linear operator: it preserves
// both a constant image and total photons, including at image edges. Local disk
// radius is linear in relative inverse-depth difference. No gamma-domain blur.
inline Image render_refocus(const Image& image,const std::vector<float>& depth,
                            int depth_h,int depth_w,const RefocusOptions& options = {},
                            const ParallelRows& parallel_rows = {}) {
    const size_t p=detail::pixels(image.H,image.W),dp=detail::pixels(depth_h,depth_w);
    if(image.rgb.size()!=3*p||depth.size()!=dp||!std::isfinite(options.focus_depth)||
        !std::isfinite(options.radius_px)||options.radius_px<0||options.max_radius<0||options.max_radius>256)
        throw std::invalid_argument("Invalid refocus input");
    for(float d:depth) if(!std::isfinite(d)) throw std::invalid_argument("Nonfinite relative depth");
    Image out=image;
    if(options.radius_px==0||options.max_radius==0) return out;
    // O(pixels) working storage; neighborhood cost is bounded by max_radius.
    std::vector<uint16_t> radius(p);
    std::vector<float> area(static_cast<size_t>(options.max_radius)+1,1);
    std::vector<std::vector<int>> extents(static_cast<size_t>(options.max_radius)+1);
    extents[0].assign(1, 0);
    for(int r=1;r<=options.max_radius;++r) {
        int count=0;
        extents[static_cast<size_t>(r)].resize(static_cast<size_t>(r)+1);
        for(int dy=0;dy<=r;++dy) {
            const int extent=static_cast<int>(std::floor(std::sqrt(r*r-dy*dy)));
            extents[static_cast<size_t>(r)][static_cast<size_t>(dy)]=extent;
            count+=(2*extent+1)*(dy==0 ? 1 : 2);
        }
        area[static_cast<size_t>(r)]=static_cast<float>(count);
    }
    for(int y=0;y<image.H;++y) for(int x=0;x<image.W;++x) {
        const float d=detail::sample(depth,depth_h,depth_w,
            (y+0.5f)*depth_h/image.H-0.5f,(x+0.5f)*depth_w/image.W-0.5f);
        const float r=std::min(static_cast<float>(options.max_radius),options.radius_px*std::fabs(d-options.focus_depth));
        radius[static_cast<size_t>(y)*image.W+x]=static_cast<uint16_t>(std::floor(r+0.5f));
    }
    if(parallel_rows) {
        // Same symmetric exchange operator in gather form: each worker reads
        // immutable image/radii and writes only its own output rows.
        const RowTask gather=[&](int begin,int end) {
            if(begin<0||end<begin||end>image.H) throw std::invalid_argument("Invalid refocus row range");
            for(int y=begin;y<end;++y) for(int x=0;x<image.W;++x) {
                const size_t i=static_cast<size_t>(y)*image.W+x;
                const int r=radius[i];
                std::array<double,3> sum{{image.rgb[3*i],image.rgb[3*i+1],image.rgb[3*i+2]}};
                for(int dy=std::max(-r,-y);dy<=std::min(r,image.H-1-y);++dy) {
                    const int extent=extents[static_cast<size_t>(r)][static_cast<size_t>(std::abs(dy))];
                    for(int dx=std::max(-extent,-x);dx<=std::min(extent,image.W-1-x);++dx) {
                        if(dx==0&&dy==0) continue;
                        const size_t j=static_cast<size_t>(y+dy)*image.W+x+dx;
                        if(dx*dx+dy*dy>radius[j]*radius[j]) continue;
                        const float wt=1/std::max(area[radius[i]],area[radius[j]]);
                        for(size_t c=0;c<3;++c) sum[c]+=wt*(image.rgb[3*j+c]-image.rgb[3*i+c]);
                    }
                }
                for(size_t c=0;c<3;++c) out.rgb[3*i+c]=static_cast<float>(sum[c]);
            }
        };
        parallel_rows(image.H,gather);
        return out;
    }
    for(int y=0;y<image.H;++y) for(int x=0;x<image.W;++x) {
        const size_t i=static_cast<size_t>(y)*image.W+x;
        const int r=radius[i];
        for(int dy=0;dy<=r;++dy) {
            const int extent=extents[static_cast<size_t>(r)][static_cast<size_t>(dy)];
            for(int dx=dy==0 ? 1 : -extent;dx<=extent;++dx) {
                const int yy=y+dy,xx=x+dx;
                if(yy>=image.H||xx<0||xx>=image.W) continue;
                const size_t j=static_cast<size_t>(yy)*image.W+xx;
                if(dx*dx+dy*dy>radius[j]*radius[j]) continue;
                const float wt=1/std::max(area[radius[i]],area[radius[j]]);
                for(size_t c=0;c<3;++c) {
                    const float delta=wt*(image.rgb[3*j+c]-image.rgb[3*i+c]);
                    out.rgb[3*i+c]+=delta; out.rgb[3*j+c]-=delta;
                }
            }
        }
    }
    return out;
}

inline Image render_refocus(const Image& image,const FieldResult& fields,
                            const RefocusOptions& options = {},const ParallelRows& parallel_rows = {}) {
    const auto depth=fields.maps.find("depth");
    const auto gate=fields.gates.find("depth");
    if(depth==fields.maps.end()||gate==fields.gates.end()||!gate->second.observable)
        throw std::invalid_argument("Refocus requires observable relative depth");
    return render_refocus(image,depth->second,fields.H,fields.W,options,parallel_rows);
}

namespace detail {
inline void be32(std::vector<uint8_t>& out,uint32_t v) {
    out.push_back(static_cast<uint8_t>(v>>24)); out.push_back(static_cast<uint8_t>(v>>16));
    out.push_back(static_cast<uint8_t>(v>>8)); out.push_back(static_cast<uint8_t>(v));
}
consteval std::array<uint32_t,256> make_crc32_lut() noexcept {
    std::array<uint32_t,256> t{};
    for(uint32_t i=0;i<256;++i) {
        uint32_t c=i;
        for(int bit=0;bit<8;++bit) c=(c>>1)^((c&1u) ? 0xedb88320u : 0u);
        t[i]=c;
    }
    return t;
}
inline constexpr auto kCrc32Lut=make_crc32_lut();
inline uint32_t crc32_bytes(std::span<const uint8_t> data) noexcept {
    uint32_t crc=0xffffffffu;
    for(uint8_t v:data) crc=(crc>>8)^kCrc32Lut[(crc^v)&0xffu];
    return crc^0xffffffffu;
}
inline void png_chunk(std::vector<uint8_t>& out,const char* tag,std::span<const uint8_t> data) {
    if(data.size()>std::numeric_limits<uint32_t>::max()) throw std::length_error("PNG chunk too large");
    be32(out,static_cast<uint32_t>(data.size()));
    const size_t start=out.size();
    for(int k=0;k<4;++k) out.push_back(static_cast<uint8_t>(tag[k]));
    out.insert(out.end(),data.begin(),data.end());
    be32(out,crc32_bytes({out.data()+start,4+data.size()}));
}
// PNG with zlib stored blocks: self-contained, deterministic, no library link.
inline std::vector<uint8_t> png_rgb8(std::span<const uint8_t> rgb,int h,int w) {
    const size_t p=pixels(h,w);
    if(rgb.size()!=3*p) throw std::invalid_argument("PNG RGB dimensions mismatch");
    std::vector<uint8_t> scan;
    scan.reserve(3*p+static_cast<size_t>(h));
    for(int y=0;y<h;++y) {
        scan.push_back(0);
        const auto first=rgb.begin()+static_cast<std::ptrdiff_t>(static_cast<size_t>(y)*w*3);
        scan.insert(scan.end(),first,first+static_cast<std::ptrdiff_t>(static_cast<size_t>(w)*3));
    }
    std::vector<uint8_t> z{0x78,0x01};
    uint32_t a=1,b=0;
    for(uint8_t v:scan) { a=(a+v)%65521u; b=(b+a)%65521u; }
    for(size_t pos=0;pos<scan.size();) {
        const size_t count=std::min<size_t>(65535,scan.size()-pos);
        z.push_back(pos+count==scan.size() ? 1 : 0);
        const uint16_t len=static_cast<uint16_t>(count),inv=static_cast<uint16_t>(~len);
        z.push_back(static_cast<uint8_t>(len)); z.push_back(static_cast<uint8_t>(len>>8));
        z.push_back(static_cast<uint8_t>(inv)); z.push_back(static_cast<uint8_t>(inv>>8));
        z.insert(z.end(),scan.begin()+static_cast<std::ptrdiff_t>(pos),
            scan.begin()+static_cast<std::ptrdiff_t>(pos+count));
        pos+=count;
    }
    be32(z,(b<<16)|a);
    std::vector<uint8_t> out{137,80,78,71,13,10,26,10},header;
    be32(header,static_cast<uint32_t>(w)); be32(header,static_cast<uint32_t>(h));
    header.insert(header.end(),{8,2,0,0,0});
    png_chunk(out,"IHDR",header); png_chunk(out,"IDAT",z); png_chunk(out,"IEND",{});
    return out;
}
inline std::string base64(std::span<const uint8_t> bytes) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3) {
        const uint32_t v=(static_cast<uint32_t>(bytes[i])<<16) |
            (i+1<bytes.size() ? static_cast<uint32_t>(bytes[i+1])<<8 : 0) |
            (i+2<bytes.size() ? bytes[i+2] : 0);
        out.push_back(alphabet[(v>>18)&63]); out.push_back(alphabet[(v>>12)&63]);
        out.push_back(i+1<bytes.size() ? alphabet[(v>>6)&63] : '=');
        out.push_back(i+2<bytes.size() ? alphabet[v&63] : '=');
    }
    return out;
}
} // namespace detail

// rgb8 is already display-rendered by the caller; the computational APIs stay
// camera-linear. PNG depth stores 16 bits as RG bytes to preserve texture values.
// HTML assets and scripts are inline; WebGL2 renders a depth-tested 2.5D mesh.
inline void write_webgl_html(const std::string& path,const std::vector<uint8_t>& rgb8,
                             int H,int W,const FieldResult& fields,float max_shift_px=24) {
    const size_t p=detail::pixels(H,W),dp=detail::pixels(fields.H,fields.W);
    const auto depth=fields.maps.find("depth");
    if(rgb8.size()!=3*p||depth==fields.maps.end()||depth->second.size()!=dp ||
        !std::isfinite(max_shift_px)||max_shift_px<0)
        throw std::invalid_argument("Invalid WebGL image/depth");
    const auto gate=fields.gates.find("depth");
    if(gate==fields.gates.end()||!gate->second.observable)
        throw std::invalid_argument("WebGL requires observable relative depth");
    std::vector<uint8_t> packed(3*dp);
    for(size_t i=0;i<dp;++i) {
        if(!std::isfinite(depth->second[i])) throw std::invalid_argument("Nonfinite WebGL depth");
        const auto v=static_cast<uint16_t>(std::lround(detail::clamp01(depth->second[i])*65535));
        packed[3*i]=static_cast<uint8_t>(v>>8); packed[3*i+1]=static_cast<uint8_t>(v);
    }
    const std::string color=detail::base64(detail::png_rgb8(rgb8,H,W));
    const std::string dep=detail::base64(detail::png_rgb8(packed,fields.H,fields.W));
    std::ofstream out(heraw_fs::from_utf8(path),std::ios::binary);
    if(!out) throw std::runtime_error("Cannot open WebGL output: "+path);
    out << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>NijiN.LiteBurstSuite relative light field</title>"
        "<style>html,body{margin:0;background:#17191c;color:#eee;font:14px system-ui;height:100%;}"
        "canvas{display:block;width:100%;height:calc(100% - 40px);object-fit:contain;touch-action:none}"
        "p{height:24px;margin:8px}button{float:right}</style>"
        "<p id=\"status\">"
        << (fields.geometry.sign_calibrated
                ? "Drag to orbit. Relative inverse-depth plane (near/far sign optically calibrated)."
                : "Drag to orbit. Relative inverse-depth plane; near/far sign is a gauge.")
        << "<button id=\"flip\">Flip depth sign</button></p><canvas id=\"view\"></canvas><script>\n"
        "'use strict';\nconst W=" << W << ",H=" << H << ",shift=" << max_shift_px << ";\n"
        "const canvas=document.getElementById('view'),status=document.getElementById('status');\n"
        "const gl=canvas.getContext('webgl2',{antialias:true});\n"
        "if(!gl){status.textContent='WebGL2 is required';throw Error('WebGL2 required');}\n"
        "canvas.width=W;canvas.height=H;\n"
        "const vs=`#version 300 es\nprecision highp float;\n"
        "uniform sampler2D depth;uniform vec2 orbit;uniform vec2 size;uniform float signDepth;\n"
        "out vec2 uv;\n"
        "float getDepth(vec2 q){vec2 v=texture(depth,q).rg;return (v.r*65280.0+v.g*255.0)/65535.0;}\n"
        "void main(){int corner=gl_VertexID%6;int cell=gl_VertexID/6;int x=cell%256,y=cell/256;\n"
        "vec2 offset=corner==0?vec2(0,0):corner==1?vec2(1,0):corner==2?vec2(0,1):"
        "corner==3?vec2(0,1):corner==4?vec2(1,0):vec2(1,1);\n"
        "uv=(vec2(x,y)+offset)/256.0;float d=(getDepth(uv)-0.5)*signDepth;\n"
        "vec2 q=uv+orbit*d/size;gl_Position=vec4(q.x*2.0-1.0,1.0-q.y*2.0,-d,1);}\n`;\n"
        "const fs=`#version 300 es\nprecision highp float;\nuniform sampler2D color;"
        "in vec2 uv;out vec4 frag;void main(){frag=vec4(texture(color,uv).rgb,1);}`;\n"
        "function shader(type,src){const s=gl.createShader(type);gl.shaderSource(s,src);gl.compileShader(s);"
        "if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;}\n"
        "const program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,vs));"
        "gl.attachShader(program,shader(gl.FRAGMENT_SHADER,fs));gl.linkProgram(program);"
        "if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));"
        "gl.useProgram(program);gl.bindVertexArray(gl.createVertexArray());\n"
        "async function texture(uri,unit,name){const image=new Image();image.src=uri;await image.decode();"
        "gl.activeTexture(gl.TEXTURE0+unit);gl.bindTexture(gl.TEXTURE_2D,gl.createTexture());"
        "gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,gl.NONE);"
        "gl.texImage2D(gl.TEXTURE_2D,0,gl.RGB,gl.RGB,gl.UNSIGNED_BYTE,image);"
        "gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);"
        "gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);"
        "gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);"
        "gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);"
        "gl.uniform1i(gl.getUniformLocation(program,name),unit);}\n"
        "Promise.all([texture('data:image/png;base64," << color << "',0,'color'),"
        "texture('data:image/png;base64," << dep << "',1,'depth')]).then(()=>{\n"
        "let ox=0,oy=0,sgn=1;gl.enable(gl.DEPTH_TEST);gl.clearColor(0.09,0.1,0.11,1);"
        "gl.uniform2f(gl.getUniformLocation(program,'size'),W,H);\n"
        "function draw(){gl.viewport(0,0,W,H);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);"
        "gl.uniform2f(gl.getUniformLocation(program,'orbit'),ox,oy);"
        "gl.uniform1f(gl.getUniformLocation(program,'signDepth'),sgn);gl.drawArrays(gl.TRIANGLES,0,256*256*6);}\n"
        "canvas.onpointermove=e=>{const r=canvas.getBoundingClientRect();"
        "ox=((e.clientX-r.left)/r.width-0.5)*2*shift;"
        "oy=((e.clientY-r.top)/r.height-0.5)*2*shift;draw();};"
        "canvas.onpointerleave=()=>{ox=0;oy=0;draw();};"
        "document.getElementById('flip').onclick=()=>{sgn=-sgn;draw();};draw();"
        "}).catch(e=>{status.textContent=e.message;});\n</script></html>\n";
    if(!out) throw std::runtime_error("Failed writing WebGL output: "+path);
}

} // namespace heraw_suite
