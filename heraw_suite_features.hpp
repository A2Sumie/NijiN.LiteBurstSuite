// heraw_suite_features.hpp -- Camera-linear temporal and relative-light-field tools.
// C++20, header-only, 7-bit ASCII. No white balance or display transform is applied.
#pragma once

#include "heraw_fs_utf8.hpp"
#include "heraw_suite_refocus.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
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

enum class DepthGateMode { Auto = 0, Relaxed = 1, Force = 2 };
enum class MetricPolicy { Verified = 0, AllowProxy = 1 };
struct DepthGateConfig {
    DepthGateMode mode = DepthGateMode::Auto;
    MetricPolicy metric_policy = MetricPolicy::Verified;
    float min_disparity_px = -1.0f; // <0 uses mode default (Auto=1.5, Relaxed=0.65, Force=0.0)
    float min_coherence = -1.0f;    // <0 uses mode default (Auto=0.55, Relaxed=0.32, Force=0.0)
    float min_support = -1.0f;      // <0 uses mode default (Auto=0.05 w/ shallow-DoF 0.012, Relaxed=0.01, Force=0.0)
};

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
    float aperture = 0.0f;
    float pixel_pitch_um = 0.0f;
    bool enable_kinetic_sign = false;
    bool ois_active = false;
    bool metric_scale_verified = false;
    DepthGateConfig depth_gate{};
};

struct Gate {
    bool observable = false;
    std::string reason;
    std::string units;
    double measured = 0.0;
    double threshold = 0.0;
    double confidence = 0.0;
    bool forced = false;
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

enum class TemporalMode { CleanPlate, VirtualND, Transient, Strobe, Glint, Reflection, FocusStack };
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
    std::vector<float> activity;    // Fraction of temporal outliers, or FocusStack peak index [0,1].
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
    // Unified V3 epipolar / metric calibration extensions:
    std::vector<float> baseline_z;                  // Axial FOE component (half-px per unit norm coord).
    std::array<float, 2> plane_tilt_rho{{0.0f, 0.0f}}; // Slanted reference plane relief (beta_y, beta_x).
    bool plane_detilted = false;
    bool metric_calibrated = false;
    bool metric_is_proxy = false;
    float estimated_baseline_mm = 0.0f;
    float diopter_span_inv_m = 0.0f;
    float near_distance_m = 0.0f;
    float far_distance_m = 0.0f;
    std::string sign_source = "uncalibrated_gauge";
    float native_coc_slope_1x_px = 0.0f;
    bool physically_observable = false;
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
inline bool solve3(double a[3][4], std::array<double,3>& x) {
    for (int k=0;k<3;++k) {
        int pivot=k;
        for (int r=k+1;r<3;++r) if(std::fabs(a[r][k])>std::fabs(a[pivot][k])) pivot=r;
        if (std::fabs(a[pivot][k])<1e-14) return false;
        for (int c=k;c<4;++c) std::swap(a[k][c],a[pivot][c]);
        const double d=a[k][k];
        for(int c=k;c<4;++c) a[k][c]/=d;
        for(int r=0;r<3;++r) if(r!=k) {
            const double f=a[r][k];
            for(int c=k;c<4;++c) a[r][c]-=f*a[k][c];
        }
    }
    for(int k=0;k<3;++k) { x[k]=a[k][3]; if(!std::isfinite(x[k])) return false; }
    return true;
}
inline float half_focal_px(const BurstView& b) {
    if (b.focal_length_mm <= 0.5f) return 0.0f;
    const float diag = std::hypot(static_cast<float>(b.W1), static_cast<float>(b.H1));
    const float pitch_mm = (b.pixel_pitch_um > 0.2f) ? (b.pixel_pitch_um * 1e-3f) : (43.266615f / std::max(1.0f, diag));
    return 0.5f * b.focal_length_mm / std::max(1e-4f, pitch_mm);
}
inline std::array<float,2> epipolar_vec(const BurstView& b, const Geometry& g, size_t n, float y, float x) {
    const float f_h = half_focal_px(b);
    if (f_h > 0.0f && n < g.baseline_z.size() && std::fabs(g.baseline_z[n]) > 1e-7f) {
        const float cy = 0.5f * static_cast<float>(b.H1 / 2 - 1);
        const float cx = 0.5f * static_cast<float>(b.W1 / 2 - 1);
        const float ny = (y - cy) / f_h;
        const float nx = (x - cx) / f_h;
        return {{g.baseline[n][0] - g.baseline_z[n] * ny, g.baseline[n][1] - g.baseline_z[n] * nx}};
    }
    return g.baseline[n];
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
        if(out.rms_half_px>4.0) out.valid=false;
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
    if(options.mode==TemporalMode::FocusStack) {
        const bool has_fpos=b.focus_positions && b.focus_positions->size()==n;
        float f_min=0.0f,f_max=0.0f;
        bool f_ok=has_fpos;
        int distinct_focus_steps = 0;
        if(f_ok) {
            std::vector<float> u_fpos;
            f_min=(*b.focus_positions)[0]; f_max=f_min;
            for(float fp:*b.focus_positions) {
                if(!std::isfinite(fp)) { f_ok=false; break; }
                f_min=std::min(f_min,fp); f_max=std::max(f_max,fp);
                bool seen = false;
                for (float uf : u_fpos) {
                    if (std::fabs(fp - uf) <= 1e-4f) { seen = true; break; }
                }
                if (!seen) u_fpos.push_back(fp);
            }
            distinct_focus_steps = static_cast<int>(u_fpos.size());
            // E09 fix: require >= 3 genuinely distinct focus steps (rejects duplicates like (1,1,2)).
            if(f_max-f_min<=1e-6f || distinct_focus_steps < 3) f_ok=false;
        }
        size_t aligned=0;
        for(const auto& camera:out.camera) aligned+=camera.valid;
        const size_t minimum=3,req_aligned=std::min<size_t>(minimum,std::max<size_t>(3,(n*3)/4));
        if(n<minimum||aligned<req_aligned||!f_ok) {
            out.gate.observable=false;
            out.gate.measured=static_cast<double>(distinct_focus_steps);
            out.gate.threshold=static_cast<double>(minimum);
            out.gate.units="distinct_focus_steps";
            out.gate.confidence=0.0;
            out.gate.reason="focus stacking requires >=3 camera-aligned frames with >=3 distinct focus positions";
            return out;
        }
        std::vector<size_t> focus_order(n);
        std::iota(focus_order.begin(), focus_order.end(), size_t{0});
        std::stable_sort(focus_order.begin(), focus_order.end(), [&](size_t a, size_t c) {
            return (*b.focus_positions)[a] < (*b.focus_positions)[c];
        });
        std::vector<size_t> pos_in_sorted(n, 0);
        for (size_t r = 0; r < n; ++r) pos_in_sorted[focus_order[r]] = r;

        std::vector<std::vector<float>> sharp(n,std::vector<float>(p,0.0f));
        std::vector<std::vector<float>> warped(n,std::vector<float>(p,std::numeric_limits<float>::quiet_NaN()));
        std::vector<float> tmp_s(p,0.0f);
        for(size_t k=0;k<n;++k) {
            std::fill(tmp_s.begin(), tmp_s.end(), 0.0f);
            for(int y=0;y<out.H;++y) for(int x=0;x<out.W;++x) {
                const auto s=out.camera[k].source(static_cast<float>(y),static_cast<float>(x));
                if(detail::inside(s[0],s[1],out.H,out.W)) {
                    const float gv=detail::sample(b.g_halfs[k],out.H,out.W,s[0],s[1]);
                    if(std::isfinite(gv)) warped[k][static_cast<size_t>(y)*out.W+x]=gv;
                }
            }
            for(int y=0;y<out.H;++y) for(int x=0;x<out.W;++x) {
                const size_t i=static_cast<size_t>(y)*out.W+x;
                const float c=warped[k][i];
                if(!std::isfinite(c)) continue;
                const int ym=std::max(0,y-1),yp=std::min(out.H-1,y+1);
                const int xm=std::max(0,x-1),xp=std::min(out.W-1,x+1);
                const float l=warped[k][static_cast<size_t>(y)*out.W+xm];
                const float r=warped[k][static_cast<size_t>(y)*out.W+xp];
                const float u=warped[k][static_cast<size_t>(ym)*out.W+x];
                const float d=warped[k][static_cast<size_t>(yp)*out.W+x];
                const float gl=std::isfinite(l)?l:c,gr=std::isfinite(r)?r:c;
                const float gu=std::isfinite(u)?u:c,gd=std::isfinite(d)?d:c;
                const float lap=gl+gr+gu+gd-4.0f*c;
                const float gx=0.5f*(gr-gl),gy=0.5f*(gd-gu);
                const float nv=detail::variance(b,c);
                tmp_s[i]=std::max(0.0f,lap*lap+0.35f*(gx*gx+gy*gy)-10.0f*nv);
            }
            // Separable 5-tap binomial [1 4 6 4 1]/16 smoothing across neighborhoods.
            std::vector<float> hbuf(p,0.0f);
            for(int y=0;y<out.H;++y) for(int x=0;x<out.W;++x) {
                static constexpr float kw[5]={1.0f/16,4.0f/16,6.0f/16,4.0f/16,1.0f/16};
                double acc=0.0;
                for(int dx=-2;dx<=2;++dx) {
                    const int xx=std::max(0,std::min(out.W-1,x+dx));
                    acc+=kw[dx+2]*tmp_s[static_cast<size_t>(y)*out.W+xx];
                }
                hbuf[static_cast<size_t>(y)*out.W+x]=static_cast<float>(acc);
            }
            for(int y=0;y<out.H;++y) for(int x=0;x<out.W;++x) {
                static constexpr float kw[5]={1.0f/16,4.0f/16,6.0f/16,4.0f/16,1.0f/16};
                double acc=0.0;
                for(int dy=-2;dy<=2;++dy) {
                    const int yy=std::max(0,std::min(out.H-1,y+dy));
                    acc+=kw[dy+2]*hbuf[static_cast<size_t>(yy)*out.W+x];
                }
                sharp[k][static_cast<size_t>(y)*out.W+x]=static_cast<float>(acc);
            }
        }
        size_t sharp_winner_cnt=0, modulated_cnt=0;
        const float f_span=std::max(1e-6f,f_max-f_min);
        for(size_t i=0;i<p;++i) {
            float max_s=0.0f, min_s=1e30f; size_t best_k=0; size_t val_cnt=0;
            for(size_t k=0;k<n;++k) {
                if(std::isfinite(warped[k][i])) {
                    ++val_cnt;
                    min_s = std::min(min_s, sharp[k][i]);
                    if(sharp[k][i]>max_s) { max_s=sharp[k][i]; best_k=k; }
                }
            }
            if(val_cnt==0) continue;
            if(max_s>1e-12f && max_s > 1.06f * min_s) ++modulated_cnt;
            if(max_s>1e-12f) {
                double wsum=0.0,gsum=0.0,fsum=0.0;
                const float inv_max=1.0f/max_s;
                for(size_t k=0;k<n;++k) if(std::isfinite(warped[k][i])) {
                    const float ratio=sharp[k][i]*inv_max;
                    const float wk=ratio*ratio*ratio;
                    out.weights[k][i]=wk;
                    wsum+=wk;
                    gsum+=wk*warped[k][i];
                    fsum+=wk*((*b.focus_positions)[k]-f_min)/f_span;
                }
                if(wsum>1e-12) {
                    const float inv_w=static_cast<float>(1.0/wsum);
                    for(size_t k=0;k<n;++k) out.weights[k][i]*=inv_w;
                    out.guide[i]=static_cast<float>(gsum*inv_w);
                    float d_peak = static_cast<float>(fsum * inv_w);
                    // Sub-step non-uniform 3-point parabolic log-sharpness refinement across sorted focus steps
                    const float f1 = (*b.focus_positions)[best_k];
                    const float f_eps = 1e-5f * f_span;
                    const size_t sr = pos_in_sorted[best_k];
                    size_t k_lo = n, k_hi = n;
                    for (size_t r = sr; r > 0; --r) {
                        const size_t cand = focus_order[r - 1];
                        if ((*b.focus_positions)[cand] < f1 - f_eps && std::isfinite(warped[cand][i])) {
                            k_lo = cand;
                            break;
                        }
                    }
                    for (size_t r = sr + 1; r < n; ++r) {
                        const size_t cand = focus_order[r];
                        if ((*b.focus_positions)[cand] > f1 + f_eps && std::isfinite(warped[cand][i])) {
                            k_hi = cand;
                            break;
                        }
                    }
                    if (k_lo < n && k_hi < n) {
                        const float f0 = (*b.focus_positions)[k_lo];
                        const float f2 = (*b.focus_positions)[k_hi];
                        const float h_minus = f1 - f0;
                        const float h_plus = f2 - f1;
                        if (h_minus > 1e-7f && h_plus > 1e-7f) {
                            const float s0 = std::max(1e-12f, sharp[k_lo][i]);
                            const float s1 = std::max(1e-12f, sharp[best_k][i]);
                            const float s2 = std::max(1e-12f, sharp[k_hi][i]);
                            const float l0 = std::log(s0), l1 = std::log(s1), l2 = std::log(s2);
                            const float d_minus = l1 - l0;
                            const float d_plus = l1 - l2;
                            const float denom = h_plus * d_minus + h_minus * d_plus;
                            if (denom > 1e-6f * (h_minus + h_plus)) {
                                const float df = 0.5f * (h_plus * h_plus * d_minus - h_minus * h_minus * d_plus) / denom;
                                const float df_clamped = std::max(-0.65f * h_minus, std::min(0.65f * h_plus, df));
                                const float d_para = (f1 + df_clamped - f_min) / f_span;
                                if (std::isfinite(d_para)) d_peak = 0.35f * d_peak + 0.65f * detail::clamp01(d_para);
                            }
                        }
                    }
                    out.activity[i]=detail::clamp01(d_peak);
                    ++sharp_winner_cnt;
                }
            } else {
                const float eq=1.0f/static_cast<float>(val_cnt);
                double gsum=0.0;
                for(size_t k=0;k<n;++k) if(std::isfinite(warped[k][i])) {
                    out.weights[k][i]=eq;
                    gsum+=eq*warped[k][i];
                }
                out.guide[i]=static_cast<float>(gsum);
                out.activity[i]=detail::clamp01(((*b.focus_positions)[best_k]-f_min)/f_span);
            }
        }
        // E09 fix: Identical unmodulated static frames must fail observable gate even with >=3 labels.
        const bool has_focus_mod = modulated_cnt >= std::max<size_t>(16u, p / 500u);
        out.gate.observable = has_focus_mod;
        out.gate.measured = static_cast<double>(modulated_cnt);
        out.gate.threshold = static_cast<double>(std::max<size_t>(16u, p / 500u));
        out.gate.units = "modulated_sites";
        out.gate.confidence = has_focus_mod ? static_cast<double>(sharp_winner_cnt) / static_cast<double>(p) : 0.0;
        out.gate.reason = has_focus_mod
            ? "focus-breathing compensated sharpness envelope fusion"
            : "insufficient cross-focus sharpness modulation across distinct focus steps";
        return out;
    }
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
    if(t.options.mode==TemporalMode::FocusStack) {
        out.gray_layer.resize(p, 0.0f);
        // 4-level Burt-Adelson Laplacian pyramid blending in camera-linear RGB.
        constexpr int LEVELS = 4;
        std::array<int, LEVELS> lh{}, lw{};
        lh[0]=H; lw[0]=W;
        for(int l=1;l<LEVELS;++l) { lh[l]=std::max(1,(lh[l-1]+1)/2); lw[l]=std::max(1,(lw[l-1]+1)/2); }
        std::array<std::vector<float>, LEVELS> acc_rgb;
        std::array<std::vector<float>, LEVELS> acc_w;
        for(int l=0;l<LEVELS;++l) {
            const size_t lp=static_cast<size_t>(lh[l])*static_cast<size_t>(lw[l]);
            acc_rgb[l].assign(lp*3u, 0.0f);
            acc_w[l].assign(lp, 0.0f);
        }
        auto pyr_down = [](const std::vector<float>& src, int sh, int sw, int ch) {
            const int dh=std::max(1,(sh+1)/2), dw=std::max(1,(sw+1)/2);
            std::vector<float> tmp(static_cast<size_t>(sh)*dw*ch, 0.0f);
            std::vector<float> dst(static_cast<size_t>(dh)*dw*ch, 0.0f);
            static constexpr float k5[5]={0.05f,0.25f,0.40f,0.25f,0.05f};
            for(int y=0;y<sh;++y) for(int cx=0;cx<dw;++cx) {
                for(int c=0;c<ch;++c) {
                    float s=0.0f;
                    for(int dx=-2;dx<=2;++dx) {
                        const int sx=std::max(0,std::min(sw-1,2*cx+dx));
                        s+=k5[dx+2]*src[(static_cast<size_t>(y)*sw+sx)*ch+c];
                    }
                    tmp[(static_cast<size_t>(y)*dw+cx)*ch+c]=s;
                }
            }
            for(int cy=0;cy<dh;++cy) for(int cx=0;cx<dw;++cx) {
                for(int c=0;c<ch;++c) {
                    float s=0.0f;
                    for(int dy=-2;dy<=2;++dy) {
                        const int sy=std::max(0,std::min(sh-1,2*cy+dy));
                        s+=k5[dy+2]*tmp[(static_cast<size_t>(sy)*dw+cx)*ch+c];
                    }
                    dst[(static_cast<size_t>(cy)*dw+cx)*ch+c]=s;
                }
            }
            return dst;
        };
        auto pyr_up_to = [](const std::vector<float>& src, int sh, int sw, int dh, int dw, int ch) {
            std::vector<float> dst(static_cast<size_t>(dh)*dw*ch, 0.0f);
            for(int y=0;y<dh;++y) {
                const float fy=std::max(0.0f,std::min(static_cast<float>(sh-1),0.5f*static_cast<float>(y)));
                const int y0=static_cast<int>(fy), y1=std::min(sh-1,y0+1);
                const float wy=fy-y0;
                for(int x=0;x<dw;++x) {
                    const float fx=std::max(0.0f,std::min(static_cast<float>(sw-1),0.5f*static_cast<float>(x)));
                    const int x0=static_cast<int>(fx), x1=std::min(sw-1,x0+1);
                    const float wx=fx-x0;
                    for(int c=0;c<ch;++c) {
                        const float v00=src[(static_cast<size_t>(y0)*sw+x0)*ch+c];
                        const float v01=src[(static_cast<size_t>(y0)*sw+x1)*ch+c];
                        const float v10=src[(static_cast<size_t>(y1)*sw+x0)*ch+c];
                        const float v11=src[(static_cast<size_t>(y1)*sw+x1)*ch+c];
                        dst[(static_cast<size_t>(y)*dw+x)*ch+c]=(1-wy)*((1-wx)*v00+wx*v01)+wy*((1-wx)*v10+wx*v11);
                    }
                }
            }
            return dst;
        };
        std::vector<float> fall_rgb(p*3u, 0.0f), fall_w(p, 0.0f);
        for(size_t k=0;k<n;++k) {
            std::vector<float> g_cur(p*3u, 0.0f), w_cur(p, 0.0f);
            std::vector<uint8_t> in_cur(p, 0u);
            const RowTask warp_k=[&](int begin,int end) {
                for(int y=begin;y<end;++y) {
                    const float cy=std::max(0.0f,std::min(static_cast<float>(t.H-1),(y+0.5f)*b.H1/H*0.5f-0.5f));
                    for(int x=0;x<W;++x) {
                        const float cx=std::max(0.0f,std::min(static_cast<float>(t.W-1),(x+0.5f)*b.W1/W*0.5f-0.5f));
                        const size_t i=static_cast<size_t>(y)*W+x;
                        const auto s=t.camera[k].source(cy,cx);
                        const bool is_in=detail::inside(s[0],s[1],t.H,t.W);
                        const float sy=std::isfinite(s[0]) ? std::max(0.0f,std::min(static_cast<float>(t.H-1),s[0])) : cy;
                        const float sx=std::isfinite(s[1]) ? std::max(0.0f,std::min(static_cast<float>(t.W-1),s[1])) : cx;
                        const auto rgb=detail::raw_rgb(b,k,2*sy+0.5f,2*sx+0.5f);
                        const float wt=is_in ? std::max(0.0f,detail::sample(t.weights[k],t.H,t.W,cy,cx)) : 0.0f;
                        g_cur[3*i+0]=rgb[0]; g_cur[3*i+1]=rgb[1]; g_cur[3*i+2]=rgb[2];
                        w_cur[i]=wt;
                        in_cur[i]=is_in ? 1u : 0u;
                    }
                }
            };
            if(parallel_rows) parallel_rows(H,warp_k); else warp_k(0,H);
            for(size_t i=0;i<p;++i) {
                if(!in_cur[i]) continue;
                fall_w[i]+=1.0f;
                for(size_t c=0;c<3;++c) fall_rgb[3*i+c]+=g_cur[3*i+c];
            }
            for(int l=0;l<LEVELS;++l) {
                const size_t lp=static_cast<size_t>(lh[l])*static_cast<size_t>(lw[l]);
                if(l==LEVELS-1) {
                    for(size_t i=0;i<lp;++i) {
                        const float wi=w_cur[i];
                        acc_w[l][i]+=wi;
                        for(size_t c=0;c<3;++c) acc_rgb[l][3*i+c]+=wi*g_cur[3*i+c];
                    }
                } else {
                    auto g_next=pyr_down(g_cur,lh[l],lw[l],3);
                    auto w_next=pyr_down(w_cur,lh[l],lw[l],1);
                    auto g_up=pyr_up_to(g_next,lh[l+1],lw[l+1],lh[l],lw[l],3);
                    for(size_t i=0;i<lp;++i) {
                        const float wi=w_cur[i];
                        acc_w[l][i]+=wi;
                        for(size_t c=0;c<3;++c) acc_rgb[l][3*i+c]+=wi*(g_cur[3*i+c]-g_up[3*i+c]);
                    }
                    g_cur.swap(g_next);
                    w_cur.swap(w_next);
                }
            }
        }
        std::vector<float> cur_recon(static_cast<size_t>(lh[LEVELS-1])*lw[LEVELS-1]*3u, 0.0f);
        for(size_t i=0;i<cur_recon.size()/3u;++i) {
            const double wi=acc_w[LEVELS-1][i];
            if(wi>1e-12) for(size_t c=0;c<3;++c) cur_recon[3*i+c]=static_cast<float>(acc_rgb[LEVELS-1][3*i+c]/wi);
        }
        for(int l=LEVELS-2;l>=0;--l) {
            auto up=pyr_up_to(cur_recon,lh[l+1],lw[l+1],lh[l],lw[l],3);
            const size_t lp=static_cast<size_t>(lh[l])*static_cast<size_t>(lw[l]);
            for(size_t i=0;i<lp;++i) {
                const double wi=acc_w[l][i];
                for(size_t c=0;c<3;++c) {
                    const float band=wi>1e-12 ? static_cast<float>(acc_rgb[l][3*i+c]/wi) : 0.0f;
                    up[3*i+c]+=band;
                }
            }
            cur_recon.swap(up);
        }
        for(size_t i=0;i<p;++i) {
            const bool ok=acc_w[0][i]>1e-9;
            for(size_t c=0;c<3;++c)
                out.image.rgb[3*i+c]=ok ? cur_recon[3*i+c] : (fall_w[i]>0 ? fall_rgb[3*i+c]/fall_w[i] : 0.0f);
            const int y=static_cast<int>(i/static_cast<size_t>(W)),x=static_cast<int>(i%static_cast<size_t>(W));
            const float cy=std::max(0.0f,std::min(static_cast<float>(t.H-1),(y+0.5f)*b.H1/H*0.5f-0.5f));
            const float cx=std::max(0.0f,std::min(static_cast<float>(t.W-1),(x+0.5f)*b.W1/W*0.5f-0.5f));
            out.gray_layer[i]=detail::clamp01(detail::sample(t.activity,t.H,t.W,cy,cx));
        }
        return out;
    }
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
    return std::isfinite(v1) && grad>2.2f*noise &&
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
    const float yf=static_cast<float>(yi),xf=static_cast<float>(xi);
    for(size_t n=1;n<g.baseline.size();++n) if(reliable(b,n,i)) {
        const auto r=residual(b,g,n,i);
        if(!std::isfinite(r[0])||!std::isfinite(r[1])) continue;
        const double mw=clamp01(b.mw_half[n][i]);
        const auto ev=epipolar_vec(b,g,n,yf,xf);
        const double by=ev[0],bx=ev[1];
        const double mby=myy*by+myx*bx,mbx=myx*by+mxx*bx;
        const double mry=myy*r[0]+myx*r[1],mrx=myx*r[0]+mxx*r[1];
        dot+=mw*(r[0]*mby+r[1]*mbx);
        norm+=mw*(by*mby+bx*mbx);
        norm_b2+=mw*(by*by+bx*bx);
        energy+=mw*std::max(0.0,r[0]*mry+r[1]*mrx);
        wsum+=mw; ++count;
    }
    const double ap_ratio=norm_b2>1e-12 ? (norm/norm_b2) : 0.0;
    if(norm<=1e-12 || (s_1d>0.5 && ap_ratio<0.16)) {
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
            const float ap_gate=s_1d>0.5 ? clamp01(static_cast<float>((ap_ratio-0.16)/0.24)) : 1.0f;
            *quality=coh*ap_gate;
        } else {
            *quality=0;
        }
    }
    const double reg_ap=(s_1d>0.5 && ap_ratio<0.30) ? (0.04*s_1d*(0.30-ap_ratio)*norm_b2) : 0.0;
    double rho_val=dot/(norm+reg_ap);
    if(g.plane_detilted) {
        const float f_h=half_focal_px(b);
        if(f_h>0.0f) {
            const float ny=(yf-0.5f*static_cast<float>(h-1))/f_h;
            const float nx=(xf-0.5f*static_cast<float>(w-1))/f_h;
            rho_val+=static_cast<double>(g.plane_tilt_rho[0]*ny+g.plane_tilt_rho[1]*nx);
        }
    }
    return static_cast<float>(rho_val);
}
// Unclipped 3-channel opponent-RGB anisotropic 1st+2nd order thin-plate multigrid completion.
inline void complete_depth_multiscale(const BurstView& b,int H,int W,
                                      const std::vector<float>& seed_depth,
                                      const std::vector<float>& seed_conf,
                                      std::vector<float>& out_depth,
                                      std::vector<float>& out_conf,
                                      const ParallelRows& parallel_rows = {}) {
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
    const float c_bw=std::max(bw_base*1.15f, 3.0f*std::sqrt(variance(b,g_med)));
    const float inv_gbw2=1.0f/(bw_base*bw_base);
    const float inv_cbw2=1.0f/(c_bw*c_bw);

    struct PyrLevel {
        int h=0,w=0;
        std::vector<float> g,rg,bg,num,den,depth,conf;
    };
    std::vector<PyrLevel> pyr;
    pyr.reserve(11);
    PyrLevel lv0;
    lv0.h=H; lv0.w=W;
    lv0.g=b.g_halfs[0];
    lv0.rg.assign(p0, 0.0f);
    lv0.bg.assign(p0, 0.0f);
    lv0.num.resize(p0); lv0.den.resize(p0);
    // Pass 1: Extract alias-free 1x opponent chroma cues (R-G, B-G) from anchor CFA frame 0.
    const auto chroma_rows=[&](int r0,int r1) {
        for(int y=r0;y<r1;++y) {
            const float yf=2.0f*static_cast<float>(y)+0.5f;
            for(int x=0;x<W;++x) {
                const size_t i=static_cast<size_t>(y)*static_cast<size_t>(W)+static_cast<size_t>(x);
                const auto c0=raw_rgb(b,0,yf,2.0f*static_cast<float>(x)+0.5f);
                lv0.rg[i]=c0[0]-c0[1];
                lv0.bg[i]=c0[2]-c0[1];
            }
        }
    };
    if(parallel_rows && H>=64) parallel_rows(H,chroma_rows); else chroma_rows(0,H);

    // Pass 2: Read-only opponent chroma filtering of seed depths across neighborhood offsets.
    const auto extract_row0=[&](int r0,int r1) {
        for(int y=r0;y<r1;++y) {
            float nb_vals[25], all_vals[25];
            for(int x=0;x<W;++x) {
                const size_t i=static_cast<size_t>(y)*static_cast<size_t>(W)+static_cast<size_t>(x);
                const float gi=std::isfinite(lv0.g[i]) ? lv0.g[i] : g_med;
                float w=clamp01(seed_conf[i]);
                float d=std::isfinite(seed_depth[i]) ? seed_depth[i] : 0.5f;
                if(w>1e-4f) {
                    int cnt=0, all_cnt=0, close_peers=0;
                    for(int dy=-6;dy<=6;dy+=3) {
                        const int yy=y+dy;
                        if(yy<0||yy>=H) continue;
                        for(int dx=-6;dx<=6;dx+=3) {
                            if(dy==0 && dx==0) continue;
                            const int xx=x+dx;
                            if(xx<0||xx>=W) continue;
                            const size_t q=static_cast<size_t>(yy)*static_cast<size_t>(W)+static_cast<size_t>(xx);
                            if(seed_conf[q]<0.18f) continue;
                            all_vals[all_cnt++]=seed_depth[q];
                            if(std::fabs(seed_depth[q]-d)<=0.25f) ++close_peers;
                            const float gq=std::isfinite(lv0.g[q]) ? lv0.g[q] : g_med;
                            const float drg=lv0.rg[i]-lv0.rg[q],dbg=lv0.bg[i]-lv0.bg[q];
                            if((gq-gi)*(gq-gi)*inv_gbw2 + (drg*drg+dbg*dbg)*inv_cbw2 <= 6.25f)
                                nb_vals[cnt++]=seed_depth[q];
                        }
                    }
                    if(cnt>=4) {
                        std::nth_element(nb_vals,nb_vals+cnt/2,nb_vals+cnt);
                        const float med=nb_vals[cnt/2];
                        if(std::fabs(d-med)>0.18f) { d=med; w*=0.20f; }
                    } else if(all_cnt>=4 && close_peers==0) {
                        std::nth_element(all_vals,all_vals+all_cnt/2,all_vals+all_cnt);
                        d=all_vals[all_cnt/2];
                        w*=0.12f;
                    }
                }
                lv0.den[i]=w;
                lv0.num[i]=w*d;
            }
        }
    };
    if(parallel_rows && H>=64) parallel_rows(H,extract_row0); else extract_row0(0,H);
    pyr.push_back(std::move(lv0));

    auto edge_aff=[&](const PyrLevel& lv,size_t i,size_t q) -> float {
        const float dg=lv.g[i]-lv.g[q];
        const float drg=lv.rg[i]-lv.rg[q];
        const float dbg=lv.bg[i]-lv.bg[q];
        return 1.0f/(1.0f + dg*dg*inv_gbw2 + 0.85f*(drg*drg+dbg*dbg)*inv_cbw2);
    };

    while(pyr.size()<11 && (pyr.back().h>6 || pyr.back().w>6)) {
        const auto& fine=pyr.back();
        PyrLevel coarse;
        coarse.h=std::max(1,(fine.h+1)/2);
        coarse.w=std::max(1,(fine.w+1)/2);
        const size_t cp=static_cast<size_t>(coarse.h)*static_cast<size_t>(coarse.w);
        coarse.g.assign(cp,0); coarse.rg.assign(cp,0); coarse.bg.assign(cp,0);
        coarse.num.assign(cp,0); coarse.den.assign(cp,0);
        for(int cy=0;cy<coarse.h;++cy) for(int cx=0;cx<coarse.w;++cx) {
            double gs=0,rgs=0,bgs=0; int gc=0;
            for(int dy=0;dy<2;++dy) for(int dx=0;dx<2;++dx) {
                const int fy=std::min(fine.h-1,2*cy+dy),fx=std::min(fine.w-1,2*cx+dx);
                const size_t fi=static_cast<size_t>(fy)*fine.w+fx;
                const float gv=fine.g[fi];
                if(std::isfinite(gv)) { gs+=gv; rgs+=fine.rg[fi]; bgs+=fine.bg[fi]; ++gc; }
            }
            const size_t ci=static_cast<size_t>(cy)*coarse.w+cx;
            const float gm=gc>0 ? static_cast<float>(gs/gc) : g_med;
            const float rgm=gc>0 ? static_cast<float>(rgs/gc) : 0.0f;
            const float bgm=gc>0 ? static_cast<float>(bgs/gc) : 0.0f;
            coarse.g[ci]=gm; coarse.rg[ci]=rgm; coarse.bg[ci]=bgm;
            double ns=0,ds=0,max_c=0;
            for(int dy=-1;dy<=2;++dy) for(int dx=-1;dx<=2;++dx) {
                const int fy=2*cy+dy,fx=2*cx+dx;
                if(fy<0||fy>=fine.h||fx<0||fx>=fine.w) continue;
                const size_t fi=static_cast<size_t>(fy)*fine.w+fx;
                const float fc=fine.den[fi];
                if(fc<=1e-6f) continue;
                const float sp=((dy==0||dy==1)?1.0f:0.5f)*((dx==0||dx==1)?1.0f:0.5f);
                const float dg=fine.g[fi]-gm,drg=fine.rg[fi]-rgm,dbg=fine.bg[fi]-bgm;
                const float rw=1.0f/(1.0f+dg*dg*inv_gbw2+0.85f*(drg*drg+dbg*dbg)*inv_cbw2);
                const double wt=static_cast<double>(sp)*static_cast<double>(rw);
                ns+=wt*fine.num[fi]; ds+=wt*fc;
                if(fc>max_c) max_c=fc;
            }
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
                top.depth[i]=top.num[i]/top.den[i];
                top.conf[i]=top.den[i];
                global_num+=top.num[i]; global_den+=top.den[i];
            }
        }
        const float fallback=global_den>1e-8 ? static_cast<float>(global_num/global_den) : 0.5f;
        for(size_t i=0;i<tp;++i) if(top.conf[i]<=1e-6f) top.depth[i]=fallback;
        std::vector<float> next_d(tp),next_c(tp);
        for(int pass=0;pass<48;++pass) {
            next_d=top.depth; next_c=top.conf;
            for(int y=0;y<top.h;++y) for(int x=0;x<top.w;++x) {
                const size_t i=static_cast<size_t>(y)*top.w+x;
                if(top.den[i]>=0.55f) continue;
                const bool empty_cell=(top.den[i]<=1e-4f);
                double ns=empty_cell ? 0.0 : top.num[i];
                double ws=empty_cell ? 0.0 : top.den[i];
                double cs=empty_cell ? 0.0 : (top.den[i]*top.den[i]);
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    if(dy==0&&dx==0) continue;
                    const int yy=y+dy,xx=x+dx;
                    if(yy<0||yy>=top.h||xx<0||xx>=top.w) continue;
                    const size_t q=static_cast<size_t>(yy)*top.w+xx;
                    if(top.conf[q]<=1e-6f) continue;
                    const float dg=top.g[i]-top.g[q],drg=top.rg[i]-top.rg[q],dbg=top.bg[i]-top.bg[q];
                    const double rw=std::max(0.04, 1.0/(1.0+0.25*(dg*dg*inv_gbw2+0.85*(drg*drg+dbg*dbg)*inv_cbw2)));
                    const double sp=(dy==0||dx==0) ? 0.5 : 0.35;
                    const double wt=sp*rw*std::max(0.08f,top.conf[q]);
                    ns+=wt*top.depth[q]; ws+=wt; cs+=wt*(0.88*top.conf[q]);
                }
                if(ws>1e-8) {
                    next_d[i]=static_cast<float>(ns/ws);
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
        const auto prolong_rows=[&](int r0,int r1) {
            for(int y=r0;y<r1;++y) for(int x=0;x<cur.w;++x) {
                const size_t i=static_cast<size_t>(y)*cur.w+x;
                const float gv=std::isfinite(cur.g[i]) ? cur.g[i] : g_med;
                const float rgv=cur.rg[i],bgv=cur.bg[i];
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
                    const float dg=gv-up.g[qi],drg=rgv-up.rg[qi],dbg=bgv-up.bg[qi];
                    const double rw=1.0/(1.0+dg*dg*inv_gbw2+0.85*(drg*drg+dbg*dbg)*inv_cbw2);
                    const double wt=static_cast<double>(bw4[k])*rw*std::max(1e-4f,up.conf[qi]);
                    ps+=wt*up.depth[qi]; pw+=wt; pc+=wt*up.conf[qi];
                }
                const float d_prior=pw>1e-10 ? static_cast<float>(ps/pw) : up.depth[idx4[0]];
                const float c_prior=pw>1e-10 ? static_cast<float>(0.88*pc/pw) : 0.1f;
                const float data_w=cur.den[i];
                if(data_w<=1e-4f) {
                    cur.depth[i]=d_prior;
                } else {
                    const float prior_w=(data_w>=0.85f ? 0.10f : 0.40f)*std::max(0.05f,c_prior);
                    cur.depth[i]=(cur.num[i]+prior_w*d_prior)/(data_w+prior_w);
                }
                cur.conf[i]=clamp01(std::max(data_w,c_prior));
            }
        };
        if(parallel_rows && cur.h>=64) parallel_rows(cur.h,prolong_rows); else prolong_rows(0,cur.h);

        // Edge-stopped 1st-order + interior thin-plate 2nd-order variational relaxation.
        std::vector<float> smooth_d(cp);
        const float aff_floor=(lv>=4) ? 0.035f : (lv>=2 ? 0.012f : 0.0015f);
        const int n_relax=(lv>=4) ? 18 : (lv>=2 ? 10 : (lv==1 ? 5 : 3));
        for(int it=0;it<n_relax;++it) {
            const auto relax_rows=[&](int r0,int r1) {
                for(int y=r0;y<r1;++y) for(int x=0;x<cur.w;++x) {
                    const size_t i=static_cast<size_t>(y)*cur.w+x;
                    double ns=cur.num[i]+1e-4*cur.depth[i];
                    double ws=cur.den[i]+1e-4;
                    float ax_w[4]={0,0,0,0};
                    float ax_d[4]={cur.depth[i],cur.depth[i],cur.depth[i],cur.depth[i]};
                    const int oy[4]={-1,1,0,0},ox[4]={0,0,-1,1};
                    for(int k=0;k<4;++k) {
                        const int yy=y+oy[k],xx=x+ox[k];
                        if(yy<0||yy>=cur.h||xx<0||xx>=cur.w) continue;
                        const size_t q=static_cast<size_t>(yy)*cur.w+xx;
                        const float aff=std::max(aff_floor,edge_aff(cur,i,q));
                        ax_w[k]=aff; ax_d[k]=cur.depth[q];
                        const double nconf=(lv>=2) ? 1.0 : static_cast<double>(std::max(0.15f,cur.conf[q]));
                        const double wt=0.65*static_cast<double>(aff)*nconf;
                        ns+=wt*cur.depth[q]; ws+=wt;
                    }
                    // Second-order thin-plate interior term preserves planar slant without staircasing.
                    const float gamma=ax_w[0]*ax_w[1]*ax_w[2]*ax_w[3];
                    if(y>0 && y+1<cur.h && x>0 && x+1<cur.w && gamma>0.06f) {
                        const double planar_est=0.25*(ax_d[0]+ax_d[1]+ax_d[2]+ax_d[3]);
                        const double tp_w=0.45*static_cast<double>(gamma);
                        ns+=tp_w*planar_est; ws+=tp_w;
                    }
                    smooth_d[i]=static_cast<float>(ns/ws);
                }
            };
            if(parallel_rows && cur.h>=64) parallel_rows(cur.h,relax_rows); else relax_rows(0,cur.h);
            cur.depth.swap(smooth_d);
        }
    }
    out_depth.resize(p0);
    for(size_t i=0;i<p0;++i) out_depth[i]=clamp01(pyr[0].depth[i]);
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
    g.baseline_z.assign(n,0.0f);
    g.gate.units="1x_px";
    g.gate.threshold=(b.depth_gate.mode==DepthGateMode::Force)?0.0:(b.depth_gate.mode==DepthGateMode::Relaxed?0.5:1.5);
    g.gate.forced=(b.depth_gate.mode==DepthGateMode::Force);
    if(n<2 || (n<3 && b.depth_gate.mode!=DepthGateMode::Force)) {
        g.gate.reason="relative depth requires >=3 frames";
        return g;
    }
    const size_t min_k_cnt=std::min<size_t>(2u, n-1);
    size_t stride=std::max<size_t>(1,(p+4095)/4096);
    std::vector<size_t> indices;
    auto collect_support=[&](size_t s_step) {
        indices.clear();
        for(size_t i=0;i<p;i+=s_step) {
            size_t count=0;
            for(size_t k=1;k<n;++k) count+=detail::reliable(b,k,i);
            if(count>=min_k_cnt) indices.push_back(i);
        }
        g.support_fraction=static_cast<float>(indices.size())/static_cast<float>((p+s_step-1)/s_step);
    };
    collect_support(stride);
    if(indices.size()<96u && stride>1u) {
        stride=std::max<size_t>(1u, stride/8u);
        collect_support(stride);
    }
    const size_t min_support_pts=(b.depth_gate.mode==DepthGateMode::Auto)?24u:8u;
    if(indices.size()<min_support_pts && b.depth_gate.mode!=DepthGateMode::Force) {
        g.gate.reason="insufficient textured photometrically consistent flow support";
        return g;
    }

    const int hh=b.H1/2,hw=b.W1/2;
    const float f_h=detail::half_focal_px(b);
    const float cy=0.5f*static_cast<float>(hh-1),cx=0.5f*static_cast<float>(hw-1);
    const float fov_rad_eff=(f_h>0.0f) ? (0.35f*std::hypot(static_cast<float>(hw),static_cast<float>(hh))/f_h) : 0.0f;

    // Principal direction (or radial FOE when axial motion dominates) initializes IRLS.
    size_t strongest=1;
    double max_energy=0;
    std::array<float,2> axis{{0,1}};
    bool init_radial=false;
    for(size_t k=1;k<n;++k) {
        double xx=0,xy=0,yy=0,e_rad=0;
        for(size_t i:indices) if(detail::reliable(b,k,i)) {
            const auto r=detail::residual(b,g,k,i);
            xx+=r[1]*r[1]; xy+=r[0]*r[1]; yy+=r[0]*r[0];
            if(f_h>0.0f) {
                const float yi=static_cast<float>(i/static_cast<size_t>(hw));
                const float xi=static_cast<float>(i%static_cast<size_t>(hw));
                const float ny=(yi-cy)/f_h,nx=(xi-cx)/f_h;
                const float nrm=std::hypot(ny,nx);
                if(nrm>0.05f) {
                    const double rp=(r[0]*ny+r[1]*nx)/nrm;
                    e_rad+=rp*rp;
                }
            }
        }
        const double eigen=0.5*(xx+yy+std::hypot(xx-yy,2*xy));
        if(eigen>max_energy) {
            max_energy=eigen; strongest=k;
            const double angle=0.5*std::atan2(2*xy,xx-yy);
            axis={{static_cast<float>(std::sin(angle)),static_cast<float>(std::cos(angle))}};
            init_radial=(f_h>0.0f && eigen>1e-12 && e_rad > 1.35*eigen);
        }
    }
    std::vector<float> rho(indices.size()), rho_q(indices.size(), 1.0f);
    for(size_t j=0;j<indices.size();++j) {
        const auto r=detail::residual(b,g,strongest,indices[j]);
        if(init_radial && f_h>0.0f) {
            const float yi=static_cast<float>(indices[j]/static_cast<size_t>(hw));
            const float xi=static_cast<float>(indices[j]%static_cast<size_t>(hw));
            const float ny=(yi-cy)/f_h,nx=(xi-cx)/f_h;
            const float d2=ny*ny+nx*nx+1e-4f;
            rho[j]=-(r[0]*ny+r[1]*nx)/std::sqrt(d2);
        } else {
            rho[j]=r[0]*axis[0]+r[1]*axis[1];
        }
    }
    for(int iter=0;iter<8;++iter) {
        g.baseline[0]={{0,0}};
        g.baseline_z[0]=0.0f;
        for(size_t k=1;k<n;++k) {
            if(f_h>0.0f) {
                double m3[3][4]={};
                double w2_sum=0.0;
                for(size_t j=0;j<indices.size();++j) if(detail::reliable(b,k,indices[j])) {
                    const double qj=(iter==0)?1.0:static_cast<double>(rho_q[j]);
                    if(qj<0.05) continue;
                    double myy,myx,mxx,s_1d;
                    detail::aperture_metric(b,indices[j],myy,myx,mxx,s_1d);
                    const auto r=detail::residual(b,g,k,indices[j]);
                    const double w=detail::clamp01(b.mw_half[k][indices[j]])*qj;
                    const double rj=static_cast<double>(rho[j]);
                    const double w2=w*rj*rj, wr=w*rj;
                    const float yi=static_cast<float>(indices[j]/static_cast<size_t>(hw));
                    const float xi=static_cast<float>(indices[j]%static_cast<size_t>(hw));
                    const double ny=static_cast<double>((yi-cy)/f_h), nx=static_cast<double>((xi-cx)/f_h);
                    // Jacobian cols for e = [by - bz*ny, bx - bz*nx]^T:
                    // d_e/d_by = [1, 0], d_e/d_bx = [0, 1], d_e/d_bz = [-ny, -nx]
                    const double J[3][2]={{1.0, 0.0}, {0.0, 1.0}, {-ny, -nx}};
                    for(int a=0;a<3;++a) {
                        const double ma0=myy*J[a][0]+myx*J[a][1];
                        const double ma1=myx*J[a][0]+mxx*J[a][1];
                        for(int c=a;c<3;++c) m3[a][c]+=w2*(ma0*J[c][0]+ma1*J[c][1]);
                        m3[a][3]+=wr*(ma0*r[0]+ma1*r[1]);
                    }
                    w2_sum+=w2*(myy+mxx);
                }
                m3[1][0]=m3[0][1]; m3[2][0]=m3[0][2]; m3[2][1]=m3[1][2];
                // Regularize bz proportional to focal ratio so telephoto noise never leaks into FOE.
                const double fov_scale=std::max(1.0,static_cast<double>(f_h)/static_cast<double>(std::max(hh,hw)));
                const double reg_z=0.04*w2_sum*fov_scale*fov_scale+1e-12;
                m3[2][2]+=reg_z;
                m3[0][0]+=1e-14; m3[1][1]+=1e-14;
                std::array<double,3> sol{};
                if(detail::solve3(m3,sol)) {
                    g.baseline[k]={{static_cast<float>(sol[0]),static_cast<float>(sol[1])}};
                    g.baseline_z[k]=static_cast<float>(sol[2]);
                } else {
                    g.baseline[k]={{0,0}}; g.baseline_z[k]=0.0f;
                }
            } else {
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
        }
        // Normalize by peak-to-peak camera sweep across the burst (lateral + axial FOV term).
        float scale=0;
        for(size_t a=0;a<n;++a)
            for(size_t c=a+1;c<n;++c) {
                const float dlat=std::hypot(g.baseline[a][0]-g.baseline[c][0],
                                            g.baseline[a][1]-g.baseline[c][1]);
                const float dax=fov_rad_eff*std::fabs(g.baseline_z[a]-g.baseline_z[c]);
                scale=std::max(scale,dlat+dax);
            }
        if(scale<1e-8f) break;
        for(size_t k=0;k<n;++k) {
            g.baseline[k][0]/=scale; g.baseline[k][1]/=scale;
            g.baseline_z[k]/=scale;
        }
        for(size_t j=0;j<indices.size();++j) rho[j]=detail::rho_at(b,g,indices[j],&rho_q[j]);
    }

    // D-1: Homography Slanted-Plane Relief De-Tilting via rotation- and breathing-invariant shear.
    if(f_h>0.0f && n>=4) {
        double h_ata[2][2]={}, h_atb[2]={};
        std::vector<std::array<double,4>> samples_tilt; // d1, d2, bx, by
        for(size_t k=1;k<n;++k) {
            if(!g.camera[k].valid || g.camera[k].rms_half_px>0.9) continue;
            const auto& h=g.camera[k].h;
            if(std::fabs(h[8])<1e-9) continue;
            const double alpha=static_cast<double>(f_h)/g.camera[k].scale;
            const double inv_h8=1.0/h[8];
            const double G00=h[0]*inv_h8, G01=h[1]*inv_h8, G02=h[2]*inv_h8/alpha;
            const double G10=h[3]*inv_h8, G11=h[4]*inv_h8, G12=h[5]*inv_h8/alpha;
            const double G20=h[6]*inv_h8*alpha, G21=h[7]*inv_h8*alpha;
            const double wy=0.5*(G02-G20), wx=0.5*(G21-G12);
            const double d1=static_cast<double>(f_h)*((G00-G11)-0.5*(wx*wx-wy*wy));
            const double d2=static_cast<double>(f_h)*((G01+G10)-wx*wy);
            const double bx=g.baseline[k][1], by=g.baseline[k][0];
            if(std::hypot(bx,by)<0.12) continue;
            // d1 = bx*beta_x - by*beta_y; d2 = by*beta_x + bx*beta_y
            h_ata[0][0]+=bx*bx+by*by;
            h_atb[0]+=bx*d1+by*d2;
            h_atb[1]+=-by*d1+bx*d2;
            samples_tilt.push_back({d1,d2,bx,by});
        }
        if(samples_tilt.size()>=3u && h_ata[0][0]>0.25) {
            const double beta_x=h_atb[0]/h_ata[0][0];
            const double beta_y=h_atb[1]/h_ata[0][0];
            double m_d1=0.0, m_d2=0.0;
            for(const auto& s:samples_tilt) { m_d1+=s[0]; m_d2+=s[1]; }
            m_d1/=static_cast<double>(samples_tilt.size());
            m_d2/=static_cast<double>(samples_tilt.size());
            double ss_tot=0.0, ss_tot_c=0.0, ss_res=0.0;
            for(const auto& s:samples_tilt) {
                const double p1=s[2]*beta_x-s[3]*beta_y;
                const double p2=s[3]*beta_x+s[2]*beta_y;
                ss_tot+=s[0]*s[0]+s[1]*s[1];
                ss_tot_c+=(s[0]-m_d1)*(s[0]-m_d1)+(s[1]-m_d2)*(s[1]-m_d2);
                ss_res+=(s[0]-p1)*(s[0]-p1)+(s[1]-p2)*(s[1]-p2);
            }
            const double r2=(ss_tot>1e-8) ? (1.0-ss_res/ss_tot) : 0.0;
            const double span_across=std::hypot(beta_x,beta_y)*(static_cast<double>(std::max(hh,hw))/static_cast<double>(f_h));
            if(r2>=0.78 && ss_tot_c>1e-8 && ss_res<0.22*ss_tot_c &&
               span_across>=0.25 && span_across<=80.0 &&
               std::isfinite(beta_x) && std::isfinite(beta_y)) {
                g.plane_tilt_rho={{static_cast<float>(beta_y),static_cast<float>(beta_x)}};
                g.plane_detilted=true;
                for(size_t j=0;j<indices.size();++j) {
                    const float yi=static_cast<float>(indices[j]/static_cast<size_t>(hw));
                    const float xi=static_cast<float>(indices[j]%static_cast<size_t>(hw));
                    rho[j]+=g.plane_tilt_rho[0]*((yi-cy)/f_h)+g.plane_tilt_rho[1]*((xi-cx)/f_h);
                }
            }
        }
    }

    std::vector<float> obs_rho;
    obs_rho.reserve(indices.size());
    for(size_t j=0;j<indices.size();++j) if(rho_q[j]>=0.25f) obs_rho.push_back(rho[j]);
    if(obs_rho.size()<16) obs_rho=rho;
    if(obs_rho.empty()) obs_rho.push_back(0.0f);
    g.rho_low=detail::quantile(obs_rho,0.03f); g.rho_high=detail::quantile(obs_rho,0.97f);
    const float rho_med=detail::quantile(obs_rho,0.50f);
    g.disparity_span_px=2*(g.rho_high-g.rho_low);
    double e_par=0,e_perp_excess=0,sp_num=0,sp_den_a=0,sp_den_b=0;
    size_t n_active=0, n_2d=0;
    for(size_t j=0;j<indices.size();++j) {
        if(rho_q[j]<0.25f) continue;
        const size_t idx=indices[j];
        double myy,myx,mxx,s_1d;
        detail::aperture_metric(b,idx,myy,myx,mxx,s_1d);
        ++n_active;
        if(s_1d<0.65) ++n_2d;
        const float yf=static_cast<float>(idx/static_cast<size_t>(hw));
        const float xf=static_cast<float>(idx%static_cast<size_t>(hw));
        const float plane_bias=(g.plane_detilted && f_h>0.0f) ?
            (g.plane_tilt_rho[0]*((yf-cy)/f_h)+g.plane_tilt_rho[1]*((xf-cx)/f_h)) : 0.0f;
        const double rho_raw=static_cast<double>(rho[j]-plane_bias);
        for(size_t k=1;k<n;++k) if(detail::reliable(b,k,idx)) {
            const auto r=detail::residual(b,g,k,idx);
            const auto ev=detail::epipolar_vec(b,g,k,yf,xf);
            const double mw=detail::clamp01(b.mw_half[k][idx])*static_cast<double>(rho_q[j]);
            const double py=rho_raw*ev[0],px=rho_raw*ev[1];
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
    const bool multi_frame_1d_locked=(n>=4u && raw_coh>=0.88f && sp_corr>=0.70f);
    const bool stripe_aliased=(n_active>=24u && n_2d<std::max<size_t>(4u,n_active/20u) && !multi_frame_1d_locked);
    g.rank1_coherence=raw_coh*std::min(1.0f,sp_corr/0.50f)*(stripe_aliased ? 0.45f : 1.0f);
    const bool shallow_dof=(b.aperture>0.0f && b.aperture<=2.4f && b.focal_length_mm>=50.0f) ||
                           (b.dof_far_m>b.dof_near_m && b.focus_distance_m>0.0f &&
                            (b.dof_far_m-b.dof_near_m)<0.35f*b.focus_distance_m);
    const float def_disp=(b.depth_gate.mode==DepthGateMode::Auto) ? 1.50f :
                         (b.depth_gate.mode==DepthGateMode::Relaxed ? 0.65f : 0.0f);
    const float def_coh=(b.depth_gate.mode==DepthGateMode::Auto) ? 0.55f :
                        (b.depth_gate.mode==DepthGateMode::Relaxed ? 0.32f : 0.0f);
    const float def_sup=(b.depth_gate.mode==DepthGateMode::Auto) ? (shallow_dof ? 0.012f : 0.05f) :
                        (b.depth_gate.mode==DepthGateMode::Relaxed ? 0.008f : 0.0f);
    const float def_sp=(b.depth_gate.mode==DepthGateMode::Auto) ? 0.35f :
                       (b.depth_gate.mode==DepthGateMode::Relaxed ? 0.18f : 0.0f);
    const float eff_disp=(b.depth_gate.min_disparity_px>=0.0f) ? b.depth_gate.min_disparity_px : def_disp;
    const float eff_coh=(b.depth_gate.min_coherence>=0.0f) ? b.depth_gate.min_coherence : def_coh;
    const float eff_sup=(b.depth_gate.min_support>=0.0f) ? b.depth_gate.min_support : def_sup;
    const float eff_sp=(b.depth_gate.mode==DepthGateMode::Force) ? 0.0f : def_sp;
    g.physically_observable=g.disparity_span_px>=1.50f && g.rank1_coherence>=0.55f &&
        sp_corr>=0.35f && g.support_fraction>=(shallow_dof ? 0.012f : 0.05f) && !stripe_aliased;
    const bool gate_pass=(b.depth_gate.mode==DepthGateMode::Force) ||
        (g.disparity_span_px>=eff_disp && g.rank1_coherence>=eff_coh &&
         sp_corr>=eff_sp && g.support_fraction>=eff_sup && !stripe_aliased);
    g.gate.measured=g.disparity_span_px;
    g.gate.threshold=eff_disp;
    g.gate.confidence=g.rank1_coherence*std::min(1.0f,g.support_fraction*2.0f);
    g.gate.observable=gate_pass;
    g.gate.forced=(b.depth_gate.mode==DepthGateMode::Force && !g.physically_observable);
    if(g.gate.forced)
        g.gate.reason="forced relative depth beyond physical gate (depth-gate=force)";
    else if(stripe_aliased && !g.gate.observable)
        g.gate.reason="periodic 1D stripe aperture risk; insufficient 2D corner anchors";
    else
        g.gate.reason=g.gate.observable ?
            "relative inverse depth; reference-plane and near/far sign gauge; coherent residual parallax" :
            "requires >=1.5 1x pixels (3 2x pixels) robust disparity span, epipolar coherence >=0.55, textured support >=0.05";

    const bool allow_optical_sign=(b.focus_distance_m > 0.0f || b.focus_is_infinity || b.distant_by_optics);
    const bool allow_kinetic_sign=(b.enable_kinetic_sign || b.focal_length_mm > 0.5f);
    if (g.gate.observable && (allow_optical_sign || allow_kinetic_sign)) {
        struct SharpSample { float rho; float shf; float wblur; float r_vs_b; bool is_ridge; bool border; };
        std::vector<SharpSample> edge_samples;
        edge_samples.reserve(indices.size());
        const float y0_b = 0.12f * static_cast<float>(hh), y1_b = 0.88f * static_cast<float>(hh);
        const float x0_b = 0.12f * static_cast<float>(hw), x1_b = 0.88f * static_cast<float>(hw);
        for (size_t j = 0; j < indices.size(); ++j) {
            if (rho_q[j] < 0.50f) continue;
            const size_t idx = indices[j];
            const int yi = static_cast<int>(idx / static_cast<size_t>(hw));
            const int xi = static_cast<int>(idx % static_cast<size_t>(hw));
            if (yi <= 1 || yi + 2 >= hh || xi <= 1 || xi + 2 >= hw) continue;
            const float fy_c = b.flows_fy[strongest][idx], fx_c = b.flows_fx[strongest][idx];
            float max_fstep = 0.0f;
            for (int ly = yi - 1; ly <= yi + 1; ++ly) {
                for (int lx = xi - 1; lx <= xi + 1; ++lx) {
                    const size_t li = static_cast<size_t>(ly) * hw + static_cast<size_t>(lx);
                    const float df = std::hypot(b.flows_fy[strongest][li] - fy_c, b.flows_fx[strongest][li] - fx_c);
                    if (std::isfinite(df)) max_fstep = std::max(max_fstep, df);
                }
            }
            if (max_fstep > 0.5f) continue;
            const float v = b.g_halfs[0][idx];
            const float nv = detail::variance(b, v);
            const float nsd = std::sqrt(nv);
            const float gx = 0.5f * (b.g_halfs[0][idx + 1] - b.g_halfs[0][idx - 1]);
            const float gy = 0.5f * (b.g_halfs[0][idx + hw] - b.g_halfs[0][idx - hw]);
            const float gmag = std::hypot(gx, gy);
            if (!(gmag > 4.0f * nsd) || !std::isfinite(gmag)) continue;
            const float lap_c = b.g_halfs[0][idx - 1] + b.g_halfs[0][idx + 1] +
                                b.g_halfs[0][idx - hw] + b.g_halfs[0][idx + hw] - 4.0f * v;
            if (!std::isfinite(lap_c)) continue;
            const float shf = std::fabs(lap_c) / (gmag + 3.0f * nsd);
            if (!std::isfinite(shf)) continue;
            const float ux = gx / gmag, uy = gy / gmag;
            const bool normal_inside =
                (static_cast<float>(yi) - 8.0f * std::fabs(uy) > 0.5f) &&
                (static_cast<float>(yi) + 8.0f * std::fabs(uy) < static_cast<float>(hh) - 1.5f) &&
                (static_cast<float>(xi) - 8.0f * std::fabs(ux) > 0.5f) &&
                (static_cast<float>(xi) + 8.0f * std::fabs(ux) < static_cast<float>(hw) - 1.5f);
            bool ray_flow_smooth = normal_inside;
            if (ray_flow_smooth) {
                for (int rk : {-8, -4, 4, 8}) {
                    const int ryi = std::clamp(static_cast<int>(std::lround(static_cast<float>(yi) + static_cast<float>(rk) * uy)), 0, hh - 1);
                    const int rxi = std::clamp(static_cast<int>(std::lround(static_cast<float>(xi) + static_cast<float>(rk) * ux)), 0, hw - 1);
                    const size_t rli = static_cast<size_t>(ryi) * hw + static_cast<size_t>(rxi);
                    if (std::hypot(b.flows_fy[strongest][rli] - fy_c, b.flows_fx[strongest][rli] - fx_c) > 0.5f) {
                        ray_flow_smooth = false;
                        break;
                    }
                }
            }
            constexpr int kR = 32;
            constexpr float kDs = 0.5f;
            constexpr float kTau = 1.15f;
            constexpr float kInvTau2 = 1.0f / (kTau * kTau);
            float prof[2 * kR + 1];
            for (int m = -kR; m <= kR; ++m) {
                const float s = static_cast<float>(m) * kDs;
                prof[m + kR] = detail::sample(b.g_halfs[0], hh, hw,
                    static_cast<float>(yi) + s * uy, static_cast<float>(xi) + s * ux);
            }
            float dp[2 * kR];
            for (int m = 0; m < 2 * kR; ++m) dp[m] = prof[m + 1] - prof[m];
            float s1_arr[81], s2_arr[81];
            float max_d1 = 0.0f, u_peak = 0.0f;
            int q_peak = 0;
            for (int q = -40; q <= 40; ++q) {
                const float u = static_cast<float>(q) * 0.25f;
                float s1 = 0.0f, s2 = 0.0f;
                for (int m = -kR; m < kR; ++m) {
                    const float sm = (static_cast<float>(m) + 0.5f) * kDs;
                    const float du = u - sm;
                    const float w = std::exp(-0.5f * du * du * kInvTau2);
                    s1 += dp[m + kR] * w;
                    s2 += dp[m + kR] * (-du * kInvTau2) * w;
                }
                s1_arr[q + 40] = std::isfinite(s1) ? std::fabs(s1) : 0.0f;
                s2_arr[q + 40] = std::isfinite(s2) ? std::fabs(s2) : 0.0f;
                if (std::fabs(u) <= 3.0f && s1_arr[q + 40] > max_d1) {
                    max_d1 = s1_arr[q + 40];
                    u_peak = u;
                    q_peak = q;
                }
            }
            const bool is_ridge = ray_flow_smooth && (std::fabs(u_peak) <= 1.75f) &&
                                  (q_peak > -36 && q_peak < 36) &&
                                  (max_d1 >= s1_arr[q_peak + 40 - 4] * 1.02f) &&
                                  (max_d1 >= s1_arr[q_peak + 40 + 4] * 1.02f);
            float max_d2 = 0.0f;
            for (int q = -40; q <= 40; ++q) {
                const float u = static_cast<float>(q) * 0.25f;
                if (std::fabs(u - u_peak) <= 7.5f) max_d2 = std::max(max_d2, s2_arr[q + 40]);
            }
            constexpr float kInvSqrtE = 0.60653066f;
            const float s_eff = kInvSqrtE * std::hypot(max_d1, 0.5f * nsd) /
                                (std::hypot(max_d2, 0.5f * nsd * kInvTau2) + 1e-7f);
            const float wblur = std::min(12.0f,
                std::sqrt(std::max(0.04f, s_eff * s_eff - kTau * kTau - 0.145f)));
            float rb_diff = 0.0f;
            if (b.aperture > 0.0f && b.aperture <= 2.8f) {
                auto ch_grad = [&](uint8_t ch) -> float {
                    float acc_g = 0.0f; int cnt_g = 0;
                    for (int sy = 2 * yi - 1; sy <= 2 * yi + 2; ++sy) {
                        if (sy <= 0 || sy + 1 >= b.H1) continue;
                        for (int sx = 2 * xi - 1; sx <= 2 * xi + 2; ++sx) {
                            if (sx <= 0 || sx + 1 >= b.W1 || b.cfa[sy % 6][sx % 6] != ch) continue;
                            const float dc = std::fabs(b.frames_lin[0][static_cast<size_t>(sy) * b.W1 + sx] -
                                                       b.frames_lin[0][static_cast<size_t>(sy) * b.W1 + std::min(b.W1 - 1, sx + 2)]);
                            const float dr = std::fabs(b.frames_lin[0][static_cast<size_t>(sy) * b.W1 + sx] -
                                                       b.frames_lin[0][static_cast<size_t>(std::min(b.H1 - 1, sy + 2)) * b.W1 + sx]);
                            acc_g += std::max(dc, dr); ++cnt_g;
                        }
                    }
                    return cnt_g > 0 ? acc_g / static_cast<float>(cnt_g) : 0.0f;
                };
                rb_diff = ch_grad(0) - ch_grad(2);
            }
            const bool is_b = (xi < x0_b || xi > x1_b || yi < y0_b || yi > y1_b);
            edge_samples.push_back({rho[j], shf, wblur, rb_diff, is_ridge, is_b});
        }

        double vcurve_a1 = 0.0, vcurve_r2 = 0.0;
        float vcurve_rho_f = 0.5f * (g.rho_low + g.rho_high);
        auto flip_depth_sign = [&]() {
            for (auto& bv : g.baseline) { bv[0] = -bv[0]; bv[1] = -bv[1]; }
            for (auto& bz : g.baseline_z) bz = -bz;
            g.plane_tilt_rho[0] = -g.plane_tilt_rho[0];
            g.plane_tilt_rho[1] = -g.plane_tilt_rho[1];
            for (auto& es : edge_samples) es.rho = -es.rho;
            vcurve_rho_f = -vcurve_rho_f;
            const float old_lo = g.rho_low;
            g.rho_low = -g.rho_high;
            g.rho_high = -old_lo;
        };

        // D-3 Mode 1: Kinetic Occlusion Crescent Asymmetry across sharp disparity steps.
        if (allow_kinetic_sign && n >= 4) {
            double occ_vote = 0.0, occ_w2 = 0.0;
            size_t occ_pairs = 0;
            const int step_scan = std::max(2, std::min(hh, hw) / 84);
            for (int y = 4; y < hh - 4; y += step_scan) {
                for (int x = 4; x < hw - 4; x += step_scan) {
                    const size_t idx = static_cast<size_t>(y) * hw + x;
                    const float gx = 0.5f * (b.g_halfs[0][idx + 1] - b.g_halfs[0][idx - 1]);
                    const float gy = 0.5f * (b.g_halfs[0][idx + hw] - b.g_halfs[0][idx - hw]);
                    const float gmag = std::hypot(gx, gy);
                    const float nsd = std::sqrt(detail::variance(b, b.g_halfs[0][idx]));
                    if (!(gmag > 5.0f * nsd) || !std::isfinite(gmag)) continue;
                    const float ux = gx / gmag, uy = gy / gmag;
                    const float d_probe = std::max(2.5f, std::min(5.5f, 2.5f + 0.35f * g.disparity_span_px));
                    const int ya = std::max(1, std::min(hh - 2, static_cast<int>(std::lround(y + d_probe * uy))));
                    const int xa = std::max(1, std::min(hw - 2, static_cast<int>(std::lround(x + d_probe * ux))));
                    const int yb = std::max(1, std::min(hh - 2, static_cast<int>(std::lround(y - d_probe * uy))));
                    const int xb = std::max(1, std::min(hw - 2, static_cast<int>(std::lround(x - d_probe * ux))));
                    float qa = 0.0f, qb = 0.0f;
                    const float rho_a = detail::rho_at(b, g, static_cast<size_t>(ya) * hw + xa, &qa);
                    const float rho_b = detail::rho_at(b, g, static_cast<size_t>(yb) * hw + xb, &qb);
                    if (qa < 0.45f || qb < 0.45f) continue;
                    const float dr = rho_a - rho_b;
                    const float min_dr = std::max(0.25f, 0.32f * (g.rho_high - g.rho_low));
                    if (std::fabs(dr) < min_dr) continue;
                    const float va = b.g_halfs[0][static_cast<size_t>(ya) * hw + xa];
                    const float vb = b.g_halfs[0][static_cast<size_t>(yb) * hw + xb];
                    const float contrast = std::fabs(va - vb);
                    if (contrast < 5.0f * nsd) continue;
                    auto eval_crescent = [&](int cy0, int cx0, float r_side, float v_self, float v_opp,
                                             int& vis_cnt, int& occ_cnt) {
                        vis_cnt = 0; occ_cnt = 0;
                        const float yf = static_cast<float>(cy0), xf = static_cast<float>(cx0);
                        const float pb = (g.plane_detilted && f_h > 0.0f) ?
                            (g.plane_tilt_rho[0] * ((yf - cy) / f_h) + g.plane_tilt_rho[1] * ((xf - cx) / f_h)) : 0.0f;
                        const float rr = r_side - pb;
                        const float thr = std::max(3.5f * nsd, 0.25f * contrast);
                        for (size_t k = 1; k < n; ++k) {
                            const auto s = g.camera[k].source(yf, xf);
                            const auto ev = detail::epipolar_vec(b, g, k, yf, xf);
                            const float sy = s[0] + rr * ev[0], sx = s[1] + rr * ev[1];
                            if (!detail::inside(sy, sx, hh, hw)) continue;
                            const float vk = detail::sample(b.g_halfs[k], hh, hw, sy, sx);
                            if (!std::isfinite(vk)) continue;
                            if (std::fabs(vk - v_self) <= thr) ++vis_cnt;
                            else if (std::fabs(vk - v_opp) <= thr) ++occ_cnt;
                        }
                    };
                    int vis_a = 0, occ_a = 0, vis_b = 0, occ_b = 0;
                    eval_crescent(ya, xa, rho_a, va, vb, vis_a, occ_a);
                    eval_crescent(yb, xb, rho_b, vb, va, vis_b, occ_b);
                    // Occluding foreground (NEAR) stays visible across frames while occluded background
                    // loses frames to foreground color (occ_opp > 0 while self occ == 0).
                    if ((occ_b > occ_a && vis_b >= 1 && occ_a == 0) ||
                        (occ_a > occ_b && vis_a >= 1 && occ_b == 0)) {
                        const double dir = (occ_b > occ_a) ? (dr > 0 ? 1.0 : -1.0) : (dr < 0 ? 1.0 : -1.0);
                        const double wt = std::min(5.0, static_cast<double>(std::abs(occ_b - occ_a))) *
                                          std::min(1.0, static_cast<double>(contrast / (8.0f * nsd)));
                        occ_vote += dir * wt;
                        occ_w2 += wt * wt;
                        ++occ_pairs;
                    }
                }
            }
            if (occ_pairs >= 4u && occ_w2 > 1e-6) {
                const double occ_z = occ_vote / std::sqrt(occ_w2);
                if (std::fabs(occ_z) >= 2.2) {
                    if (occ_z < 0.0) flip_depth_sign();
                    g.sign_calibrated = true;
                    g.sign_source = "kinetic_occlusion";
                }
            }
        }

        // D-3 Mode 2: Non-Monotonic Thin-Lens CoC V-Curve w^2(rho) = a0 + a1*(rho - rho_f)^2 + Metric Baseline.
        vcurve_rho_f = 0.5f * (g.rho_low + g.rho_high);
        std::vector<const SharpSample*> vc_pts;
        vc_pts.reserve(edge_samples.size());
        for (const auto& es : edge_samples) if (es.is_ridge) vc_pts.push_back(&es);
        float vc_rlo = 1e30f, vc_rhi = -1e30f;
        for (const auto* ep : vc_pts) { vc_rlo = std::min(vc_rlo, ep->rho); vc_rhi = std::max(vc_rhi, ep->rho); }
        if (vc_pts.size() < 16u || (vc_rhi - vc_rlo) < 0.35f * std::max(1e-4f, g.rho_high - g.rho_low)) {
            vc_pts.clear();
            for (const auto& es : edge_samples) vc_pts.push_back(&es);
        }
        if (vc_pts.size() >= 16u) {
            const float r0_g = g.rho_low - 0.15f * std::max(1e-4f, g.rho_high - g.rho_low);
            const float r1_g = g.rho_high + 0.15f * std::max(1e-4f, g.rho_high - g.rho_low);
            double mean_y = 0.0;
            for (const auto* ep : vc_pts) mean_y += static_cast<double>(ep->wblur) * ep->wblur;
            mean_y /= static_cast<double>(vc_pts.size());
            double ss_tot = 0.0;
            for (const auto* ep : vc_pts) {
                const double dy = static_cast<double>(ep->wblur) * ep->wblur - mean_y;
                ss_tot += dy * dy;
            }
            double best_sse = 1e30, best_a1 = 0.0;
            float best_rf = vcurve_rho_f;
            for (int gi = 0; gi <= 32; ++gi) {
                const float cand_rf = r0_g + (r1_g - r0_g) * (static_cast<float>(gi) / 32.0f);
                double s00 = 0, s01 = 0, s11 = 0, t0 = 0, t1 = 0;
                for (const auto* ep : vc_pts) {
                    const double x1 = static_cast<double>(ep->rho - cand_rf) * static_cast<double>(ep->rho - cand_rf);
                    const double yv = static_cast<double>(ep->wblur) * ep->wblur;
                    s00 += 1.0; s01 += x1; s11 += x1 * x1;
                    t0 += yv; t1 += x1 * yv;
                }
                const double det = s00 * s11 - s01 * s01;
                if (det <= 1e-12) continue;
                const double a0 = (s11 * t0 - s01 * t1) / det;
                const double a1 = (s00 * t1 - s01 * t0) / det;
                if (a0 <= 0.0 || a1 < 0.0) continue;
                double sse = 0.0;
                for (const auto* ep : vc_pts) {
                    const double x1 = static_cast<double>(ep->rho - cand_rf) * static_cast<double>(ep->rho - cand_rf);
                    const double err = (a0 + a1 * x1) - static_cast<double>(ep->wblur) * ep->wblur;
                    sse += err * err;
                }
                if (sse < best_sse) {
                    best_sse = sse; best_a1 = a1; best_rf = cand_rf;
                }
            }
            if (ss_tot > 1e-8 && best_sse < ss_tot) {
                vcurve_a1 = best_a1;
                vcurve_rho_f = best_rf;
                vcurve_r2 = 1.0 - best_sse / ss_tot;
            }
        }

        if (!g.sign_calibrated && edge_samples.size() >= 24u) {
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
                    g.sign_source = "optical_defocus_slope";
                } else if (corr < -0.20) {
                    g.sign_calibrated = true;
                    g.sign_source = "optical_defocus_slope";
                }
            } else if (b.hyperfocal_m > 0.0f && b.focus_distance_m > 0.0f &&
                       b.focus_distance_m < 0.35f * b.hyperfocal_m &&
                       !(vcurve_r2 >= 0.15 &&
                         vcurve_rho_f > g.rho_low + 0.25f * (g.rho_high - g.rho_low) &&
                         vcurve_rho_f < g.rho_high - 0.25f * (g.rho_high - g.rho_low))) {
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
                        g.sign_source = "optical_defocus_slope";
                    }
                }
            }
        }
        // D-3 Mode 3: Axial LoCA tie-breaker on fast lenses when focus plane is localized.
        if (!g.sign_calibrated && b.aperture > 0.0f && b.aperture <= 2.8f && edge_samples.size() >= 24u) {
            double c_rb = 0.0, v_r = 0.0, v_rb = 0.0;
            for (const auto& es : edge_samples) {
                const double dr = es.rho - vcurve_rho_f;
                c_rb += dr * es.r_vs_b;
                v_r += dr * dr;
                v_rb += static_cast<double>(es.r_vs_b) * es.r_vs_b;
            }
            const double rb_corr = (v_r > 1e-12 && v_rb > 1e-12) ? (c_rb / std::sqrt(v_r * v_rb)) : 0.0;
            if (std::fabs(rb_corr) >= 0.30) {
                // Under-corrected LoCA focuses red behind blue (rb_corr < 0 is aligned).
                if (rb_corr > 0.0) flip_depth_sign();
                g.sign_calibrated = true;
                g.sign_source = "loca_chromatic";
            }
        }

        if (edge_samples.size() >= 16u) {
            const float span = std::max(1e-6f, g.rho_high - g.rho_low);
            if (vcurve_r2 >= 0.12 && vcurve_a1 > 1e-4) {
                const float rf_oriented = vcurve_rho_f;
                g.focus_plane_depth = detail::clamp01((rf_oriented - g.rho_low) / span);
                g.has_focus_plane = true;
            } else {
                std::vector<float> all_shf;
                all_shf.reserve(edge_samples.size());
                for (const auto& es : edge_samples) all_shf.push_back(es.shf);
                const float shf_thr = detail::quantile(all_shf, 0.75f);
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
            g.metric_is_proxy = (b.ois_active && !b.metric_scale_verified);
            if (b.focal_length_mm > 0.5f && b.aperture > 0.1f &&
                b.focus_distance_m > 0.05f && !b.focus_is_infinity && vcurve_a1 > 1e-4 && vcurve_r2 >= 0.10) {
                const float diag = std::hypot(static_cast<float>(b.W1), static_cast<float>(b.H1));
                const float pitch_mm = (b.pixel_pitch_um > 0.2f) ? (b.pixel_pitch_um * 1e-3f)
                                                                 : (43.266615f / std::max(1.0f, diag));
                // Half-px blur proxy slope per m^-1 diopter offset: k_opt = f_mm^2 / (4 * N_f * p_mm * 1000)
                const float kappa_opt = (b.focal_length_mm * b.focal_length_mm) /
                                        std::max(1e-3f, 4.0f * b.aperture * pitch_mm * 1000.0f);
                const float diopter_per_rho = std::sqrt(static_cast<float>(vcurve_a1)) / std::max(1e-4f, kappa_opt);
                const float rho_span = std::max(1e-4f, g.rho_high - g.rho_low);
                g.diopter_span_inv_m = diopter_per_rho * rho_span;
                if (diopter_per_rho > 1e-4f && diopter_per_rho < 20.0f && f_h > 0.0f) {
                    // f_h is in half-px and diopter_per_rho in m^-1 / half-px -> B_mm = 1000 / (f_h * diopter_per_rho)
                    g.estimated_baseline_mm = 1000.0f / (f_h * diopter_per_rho);
                    const float d_focus = 1.0f / b.focus_distance_m;
                    const float d_near = std::max(1e-3f, d_focus + g.diopter_span_inv_m * (1.0f - g.focus_plane_depth));
                    const float d_far = std::max(1e-4f, d_focus - g.diopter_span_inv_m * g.focus_plane_depth);
                    g.near_distance_m = 1.0f / d_near;
                    g.far_distance_m = (d_far > 1e-3f) ? (1.0f / d_far) : 1000.0f;
                    g.metric_calibrated = !(g.metric_is_proxy && b.depth_gate.metric_policy == MetricPolicy::Verified);
                    g.native_coc_slope_1x_px = (b.focal_length_mm * b.focal_length_mm * g.diopter_span_inv_m) /
                        std::max(1e-3f, 2.0f * b.aperture * pitch_mm * 1000.0f);
                }
            }
            if (g.native_coc_slope_1x_px <= 1e-6f && vcurve_a1 > 1e-4 && vcurve_r2 >= 0.10) {
                g.native_coc_slope_1x_px = 2.0f * static_cast<float>(std::sqrt(vcurve_a1)) * (g.rho_high - g.rho_low);
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
    const std::vector<std::string> names={"depth","depth_confidence","dff_depth","defocus","specular","sigma",
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
        } else if(pair.first=="dff_depth") {
            gate.observable=focused; gate.units="relative_focus_stack_depth_0_1";
            gate.reason="parabolic focus-stack depth-from-focus requires >=3 distinct focus positions";
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
    const int hh_f=out.H, hw_f=out.W;
    const float f_h_f=detail::half_focal_px(b);
    const float cy_f=0.5f*static_cast<float>(hh_f-1), cx_f=0.5f*static_cast<float>(hw_f-1);
    std::vector<float> frame_ts(n, 0.0f);
    for(size_t k=0;k<n;++k) {
        if(timed) frame_ts[k]=static_cast<float>((*b.times_seconds)[k]-(*b.times_seconds)[0]);
        else if(b.capture_order) {
            const size_t index=(*b.capture_order)[k],anchor=(*b.capture_order)[0];
            frame_ts[k]=index>=anchor ? static_cast<float>(index-anchor) : -static_cast<float>(anchor-index);
        } else frame_ts[k]=static_cast<float>(k);
    }
    bool subspace_decoupled = false;
    if (geo && n >= 5 && out.geometry.gate.observable && !out.geometry.baseline.empty()) {
        double tt = 0.0, bb_yy = 0.0, bb_xx = 0.0, bb_yx = 0.0, tb_y = 0.0, tb_x = 0.0;
        for (size_t k = 1; k < n; ++k) {
            const double tk = frame_ts[k], by = out.geometry.baseline[k][0], bx = out.geometry.baseline[k][1];
            tt += tk * tk; bb_yy += by * by; bb_xx += bx * bx; bb_yx += by * bx;
            tb_y += tk * by; tb_x += tk * bx;
        }
        const double tr = bb_yy + bb_xx;
        if (tt > 1e-8 && tr > 0.02) {
            const double diff = bb_yy - bb_xx;
            const double disc = std::hypot(diff, 2.0 * bb_yx);
            const double lam1 = 0.5 * (tr + disc);
            const double lam2 = std::max(0.0, 0.5 * (tr - disc));
            double u1y = 1.0, u1x = 0.0;
            if (disc > 1e-14 * std::max(1.0, tr)) {
                if (diff >= 0.0) {
                    u1y = 0.5 * (diff + disc);
                    u1x = bb_yx;
                } else {
                    u1y = bb_yx;
                    u1x = 0.5 * (-diff + disc);
                }
                const double un = std::hypot(u1y, u1x);
                if (un > 1e-18) { u1y /= un; u1x /= un; }
            }
            const double u2y = -u1x, u2x = u1y;
            const double c1 = u1y * tb_y + u1x * tb_x;
            const double c2 = u2y * tb_y + u2x * tb_x;
            double py = (lam1 > 1e-12) ? ((c1 / lam1) * u1y) : 0.0;
            double px = (lam1 > 1e-12) ? ((c1 / lam1) * u1x) : 0.0;
            if (lam2 > 1e-4 * std::max(1.0, lam1)) {
                py += (c2 / lam2) * u2y;
                px += (c2 / lam2) * u2x;
            }
            double e_perp = 0.0;
            for (size_t k = 1; k < n; ++k) {
                const double res_t = frame_ts[k] - (py * out.geometry.baseline[k][0] + px * out.geometry.baseline[k][1]);
                e_perp += res_t * res_t;
            }
            if (e_perp / tt >= 0.08) subspace_decoupled = true;
        }
    }
    const float t_span_all=(n>=2) ?
        (*std::max_element(frame_ts.begin(),frame_ts.end())-*std::min_element(frame_ts.begin(),frame_ts.end())) : 1.0f;

    const RowTask eval_rows=[&](int begin,int end) {
        if(begin<0||end<begin||end>out.H) throw std::invalid_argument("Invalid field row range");
        std::vector<float> values,lap,focus,ts,ry,rx,evy,evx;
        values.reserve(n); lap.reserve(n); focus.reserve(n);
        ts.reserve(n); ry.reserve(n); rx.reserve(n); evy.reserve(n); evx.reserve(n);
        for(int yi=begin;yi<end;++yi) {
            size_t local_phase=0;
            float local_turb=0;
            for(int xi=0;xi<out.W;++xi) {
                const size_t i=static_cast<size_t>(yi)*out.W+xi;
                const float y=static_cast<float>(yi),x=static_cast<float>(xi);
                values.clear(); lap.clear(); focus.clear(); ts.clear(); ry.clear(); rx.clear(); evy.clear(); evx.clear();
                double wsum=0,varsum=0,gxsum=0,gysum=0,gnorm=0,noise_sum=0;
                unsigned phases=0;
                float rq=0,rho=0;
                if(geo && !out.geometry.baseline.empty())
                    rho=detail::rho_at(b,out.geometry,i,&rq);
                const float plane_bias=(geo && out.geometry.plane_detilted && f_h_f>0.0f) ?
                    (out.geometry.plane_tilt_rho[0]*((y-cy_f)/f_h_f)+out.geometry.plane_tilt_rho[1]*((x-cx_f)/f_h_f)) : 0.0f;
                const float rho_raw=rho-plane_bias;
                double a_ss[3][4]={};
                double e_static_raw=0.0, j_s0=0.0, j_ss=0.0;
                size_t sub_cnt=0;
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
                    if(velocity||want("turbulence")||(subspace_decoupled&&depth)) {
                        auto r=detail::residual(b,out.geometry,k,i);
                        if(subspace_decoupled && k>0 && detail::reliable(b,k,i) && std::isfinite(r[0]) && std::isfinite(r[1])) {
                            const auto ev=detail::epipolar_vec(b,out.geometry,k,y,x);
                            const double tk=frame_ts[k];
                            // Solve r_y = rho_r*ey + vy*t, r_x = rho_r*ex + vx*t simultaneously.
                            const double J1[3]={ev[0], tk, 0.0};
                            const double J2[3]={ev[1], 0.0, tk};
                            for(int a=0;a<3;++a) {
                                for(int c=0;c<3;++c) a_ss[a][c]+=J1[a]*J1[c]+J2[a]*J2[c];
                                a_ss[a][3]+=J1[a]*r[0]+J2[a]*r[1];
                            }
                            j_s0+=ev[0]*r[0]+ev[1]*r[1];
                            j_ss+=ev[0]*ev[0]+ev[1]*ev[1];
                            e_static_raw+=r[0]*r[0]+r[1]*r[1];
                            ++sub_cnt;
                        }
                        if(out.geometry.gate.observable && rq>=0.50f) {
                            const auto ev=detail::epipolar_vec(b,out.geometry,k,y,x);
                            r[0]-=rho_raw*ev[0]; r[1]-=rho_raw*ev[1];
                        }
                        if(std::isfinite(r[0])&&std::isfinite(r[1])) {
                            ry.push_back(2*r[0]); rx.push_back(2*r[1]);
                            ts.push_back(frame_ts[k]);
                        }
                    }
                }
                bool has_independent_motion=false;
                double joint_vy=0.0, joint_vx=0.0;
                if(subspace_decoupled && sub_cnt>=4u && j_ss>1e-12) {
                    const double e_stat=std::max(0.0,e_static_raw-(j_s0*j_s0)/j_ss);
                    std::array<double,3> sol{};
                    double m3c[3][4];
                    std::memcpy(m3c,a_ss,sizeof(m3c));
                    m3c[0][0]+=1e-10; m3c[1][1]+=1e-10; m3c[2][2]+=1e-10;
                    if(detail::solve3(m3c,sol)) {
                        double e_joint=0.0;
                        const double r_j=sol[0], vy_h=sol[1], vx_h=sol[2];
                        // Compute exact residual of joint fit across the same residual energy:
                        // ||y||^2 - sol^T (A^T y) for valid normal eqs:
                        double rhs_dot=sol[0]*a_ss[0][3]+sol[1]*a_ss[1][3]+sol[2]*a_ss[2][3];
                        e_joint=std::max(0.0,e_static_raw-rhs_dot);
                        const double df_d=std::max(1.0,static_cast<double>(2*sub_cnt-3));
                        const double f_stat=((e_stat-e_joint)*0.5)/(e_joint/df_d+0.015);
                        const double v_disp=std::hypot(2.0*vy_h,2.0*vx_h)*std::max(1.0f,t_span_all);
                        if(f_stat>6.0 && v_disp>1.0 && std::fabs(r_j)<1e4) {
                            has_independent_motion=true;
                            joint_vy=2.0*vy_h; joint_vx=2.0*vx_h;
                        }
                    }
                }
                const size_t count=values.size();
                if(want("sigma")) out.maps["sigma"][i]=wsum>1e-9 ?
                    static_cast<float>(std::sqrt(varsum)/wsum) : std::sqrt(detail::variance(b,b.g_halfs[0][i]));
                const float rq_floor=(b.depth_gate.mode==DepthGateMode::Auto && out.geometry.disparity_span_px>=1.2f) ? 0.30f : 0.12f;
                if(depth && out.geometry.gate.observable && rq>=rq_floor) {
                    const float relative=(rho-out.geometry.rho_low)/
                        std::max(1e-6f,out.geometry.rho_high-out.geometry.rho_low);
                    if(!seed_depth.empty()) {
                        const float g0_v=b.g_halfs[0][i];
                        const float g0_mag=detail::gradient_mag(b.g_halfs[0],out.H,out.W,y,x);
                        const float g0_sig=std::sqrt(detail::variance(b,g0_v));
                        const float crlb_w=detail::clamp01((g0_mag/std::max(1e-8f,g0_sig)-1.5f)/2.8f);
                        // Leave unclipped during variational completion to avoid slant boundary compression.
                        seed_depth[i]=relative;
                        float cw=(rq>=0.85f ? rq : 0.65f*rq*rq)*crlb_w;
                        if(has_independent_motion) cw*=0.10f;
                        seed_conf[i]=cw;
                    }
                    if(want("depth_confidence") && seed_depth.empty()) out.maps["depth_confidence"][i]=rq;
                    if(rq>=0.85f && !has_independent_motion) {
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
                    double vy=0,vx=0,den=0,mt=0,my=0,mx=0;
                    if(has_independent_motion) {
                        vy=joint_vy; vx=joint_vx;
                        mt=std::accumulate(ts.begin(),ts.end(),0.0)/ts.size();
                        my=std::accumulate(ry.begin(),ry.end(),0.0)/ry.size();
                        mx=std::accumulate(rx.begin(),rx.end(),0.0)/rx.size();
                    } else {
                        mt=std::accumulate(ts.begin(),ts.end(),0.0)/ts.size();
                        my=std::accumulate(ry.begin(),ry.end(),0.0)/ry.size();
                        mx=std::accumulate(rx.begin(),rx.end(),0.0)/rx.size();
                        for(size_t j=0;j<ts.size();++j) {
                            vy+=(ts[j]-mt)*(ry[j]-my); vx+=(ts[j]-mt)*(rx[j]-mx); den+=(ts[j]-mt)*(ts[j]-mt);
                        }
                        if(den>1e-12) { vy/=den; vx/=den; } else { vy=0; vx=0; }
                    }
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

    // D-2: Direct 1D Epipolar Half-Set / Asymmetric Photometric Refinement & Outlier Demotion.
    if(want("depth") && !seed_depth.empty() && out.geometry.gate.observable && n>=4) {
        const float fov_r=(f_h_f>0.0f) ? (0.5f*static_cast<float>(std::hypot(out.H,out.W))/f_h_f) : 0.0f;
        std::vector<size_t> f_pos, f_neg, active_f;
        double sxx=0, syy=0, sxy=0, szz=0;
        for(size_t k=1;k<n;++k) {
            const float by=out.geometry.baseline[k][0], bx=out.geometry.baseline[k][1];
            const float bz=(k<out.geometry.baseline_z.size()) ? (fov_r*out.geometry.baseline_z[k]) : 0.0f;
            syy+=by*by; sxx+=bx*bx; sxy+=by*bx; szz+=bz*bz;
            if(std::hypot(by,bx)+std::fabs(bz)>=0.15f) active_f.push_back(k);
        }
        const bool axial_dom=(szz>(sxx+syy));
        const double ang=0.5*std::atan2(2*sxy,sxx-syy);
        const float ax_y=static_cast<float>(std::sin(ang)), ax_x=static_cast<float>(std::cos(ang));
        std::vector<std::pair<float,size_t>> proj_list;
        proj_list.reserve(active_f.size());
        for(size_t k:active_f) {
            const float bz=(k<out.geometry.baseline_z.size()) ? (fov_r*out.geometry.baseline_z[k]) : 0.0f;
            const float proj=axial_dom ? bz : (out.geometry.baseline[k][0]*ax_y+out.geometry.baseline[k][1]*ax_x);
            proj_list.emplace_back(proj,k);
            if(proj>0.10f) f_pos.push_back(k);
            else if(proj<-0.10f) f_neg.push_back(k);
        }
        std::vector<size_t> set_A, set_B;
        if(!f_pos.empty() && !f_neg.empty()) {
            set_A=f_pos; set_B=f_neg;
        } else if(proj_list.size()>=2u) {
            std::sort(proj_list.begin(),proj_list.end(),[](const auto& a,const auto& b){
                return std::fabs(a.first)<std::fabs(b.first);
            });
            const size_t n_in=std::max<size_t>(1u,proj_list.size()/2u);
            for(size_t idx_p=0;idx_p<n_in;++idx_p) set_A.push_back(proj_list[idx_p].second);
            for(const auto& pr:proj_list) set_B.push_back(pr.second);
        }
        const float span=std::max(1e-6f, out.geometry.rho_high-out.geometry.rho_low);
        const float d2_disp_floor=(b.depth_gate.mode==DepthGateMode::Force) ? 0.15f :
            std::max(0.40f, (b.depth_gate.min_disparity_px>=0.0f) ? b.depth_gate.min_disparity_px : 0.50f);
        if(!set_A.empty() && !set_B.empty() && out.geometry.disparity_span_px>=d2_disp_floor) {
            std::vector<float> ref_d=seed_depth, ref_c=seed_conf;
            const RowTask refine_epi=[&](int r0,int r1) {
                for(int y=std::max(1,r0);y<std::min(out.H-1,r1);++y) {
                    for(int x=1;x<out.W-1;++x) {
                        const size_t i=static_cast<size_t>(y)*out.W+x;
                        const float v0=b.g_halfs[0][i];
                        if(!std::isfinite(v0)||v0>=0.995f) continue;
                        const float nsd=std::sqrt(detail::variance(b,v0));
                        const float gmag=detail::gradient_mag(b.g_halfs[0],out.H,out.W,static_cast<float>(y),static_cast<float>(x));
                        if(gmag<=2.2f*nsd) continue;
                        float d_lo=1e9f,d_hi=-1e9f; int hi_cnt=0;
                        for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                            const int yy=std::max(0,std::min(out.H-1,y+dy)), xx=std::max(0,std::min(out.W-1,x+dx));
                            const size_t q=static_cast<size_t>(yy)*out.W+xx;
                            if(seed_conf[q]>=0.50f) {
                                d_lo=std::min(d_lo,seed_depth[q]);
                                d_hi=std::max(d_hi,seed_depth[q]);
                                ++hi_cnt;
                            }
                        }
                        if(hi_cnt==0) { d_lo=0.0f; d_hi=1.0f; }
                        const float d_spread=(hi_cnt>=2) ? (d_hi-d_lo) : 1.0f;
                        const bool need_search=(seed_conf[i]<0.80f) || (d_spread>0.18f) ||
                                               (seed_depth[i]<0.08f) || (seed_depth[i]>0.92f);
                        if(!need_search) continue;
                        const float p_bias=(out.geometry.plane_detilted && f_h_f>0.0f) ?
                            (out.geometry.plane_tilt_rho[0]*((static_cast<float>(y)-cy_f)/f_h_f)+
                             out.geometry.plane_tilt_rho[1]*((static_cast<float>(x)-cx_f)/f_h_f)) : 0.0f;
                        const float hub=std::max(3.0f*nsd, 0.0025f+0.14f*gmag);
                        auto halfset_cost=[&](const std::vector<size_t>& frames,float rel_d) -> double {
                            const float r_test=out.geometry.rho_low+rel_d*span-p_bias;
                            double tot=0.0, w_tot=0.0;
                            static constexpr int oy[5]={0,-1,1,0,0}, ox[5]={0,0,0,-1,1};
                            for(size_t k:frames) {
                                const auto ev0=detail::epipolar_vec(b,out.geometry,k,static_cast<float>(y),static_cast<float>(x));
                                const double wk=static_cast<double>(ev0[0])*ev0[0]+static_cast<double>(ev0[1])*ev0[1];
                                if(wk<=1e-6) continue;
                                double fsum=0.0; int fc=0;
                                for(int t=0;t<5;++t) {
                                    const float sy0=static_cast<float>(y+oy[t]), sx0=static_cast<float>(x+ox[t]);
                                    const auto hs=out.geometry.camera[k].source(sy0,sx0);
                                    const auto ev=detail::epipolar_vec(b,out.geometry,k,sy0,sx0);
                                    const float qy=hs[0]+r_test*ev[0], qx=hs[1]+r_test*ev[1];
                                    if(!detail::inside(qy,qx,out.H,out.W)) continue;
                                    const float vg=detail::sample(b.g_halfs[k],out.H,out.W,qy,qx);
                                    const float vr=b.g_halfs[0][static_cast<size_t>(y+oy[t])*out.W+x+ox[t]];
                                    if(!std::isfinite(vg)||!std::isfinite(vr)) continue;
                                    const float ad=std::fabs(vg-vr);
                                    fsum+=(ad<=hub) ? (0.5*ad*ad/(hub*hub)) : (ad/hub-0.5);
                                    ++fc;
                                }
                                if(fc>=3) { tot+=wk*(fsum/static_cast<double>(fc)); w_tot+=wk; }
                            }
                            return w_tot>1e-8 ? (tot/w_tot) : 1e9;
                        };
                        const float rel_d_zero=detail::clamp01((p_bias-out.geometry.rho_low)/span);
                        float cands[13];
                        cands[0]=seed_depth[i];
                        cands[1]=rel_d_zero;
                        cands[2]=detail::clamp01(d_lo);
                        cands[3]=detail::clamp01(d_hi);
                        for(int m=0;m<9;++m) cands[4+m]=-0.05f+1.10f*(static_cast<float>(m)/8.0f);
                        float best_d=rel_d_zero;
                        double best_c=1e9, c_seed=1e9, c_zero=1e9;
                        bool best_use_A=true;
                        for(int ci=0;ci<13;++ci) {
                            const double ca=halfset_cost(set_A,cands[ci]);
                            const double cb=halfset_cost(set_B,cands[ci]);
                            const bool use_a=(ca<=cb);
                            const double cc=std::min(ca,cb);
                            if(ci==0) c_seed=cc;
                            if(ci==1) c_zero=cc;
                            const bool allow_seed=(ci!=0) ||
                                ((cands[0]>=-0.10f && cands[0]<=1.10f) || (cc<0.45 && cc<c_zero*0.55));
                            if(allow_seed && cc<best_c) { best_c=cc; best_d=cands[ci]; best_use_A=use_a; }
                        }
                        if(best_c<1e8) {
                            const auto& win_f=best_use_A ? set_A : set_B;
                            const float step_rel=0.12f;
                            const double c0=best_c;
                            const double cm=halfset_cost(win_f,best_d-step_rel);
                            const double cp=halfset_cost(win_f,best_d+step_rel);
                            const double den_p=cm-2.0*c0+cp;
                            if(den_p>1e-6) {
                                const double delta=0.5*(cm-cp)/den_p;
                                if(std::fabs(delta)<1.2) {
                                    const float refined=best_d+static_cast<float>(delta)*step_rel;
                                    const double cr=halfset_cost(win_f,refined);
                                    if(cr<best_c) { best_c=cr; best_d=refined; }
                                }
                            }
                            const bool out_of_range=(seed_depth[i]<-0.08f || seed_depth[i]>1.08f);
                            const bool off_plane_mismatch=(std::fabs(seed_depth[i]-rel_d_zero)>0.20f) &&
                                                          (c_seed>std::max(0.85,c_zero*1.80));
                            const bool oor_verified=(c_seed<0.45 && c_seed<c_zero*0.60 && den_p>5e-5);
                            if((out_of_range && !oor_verified) || off_plane_mismatch) {
                                if(best_c<1.25 && den_p>3e-5 && best_c<c_seed*0.75) {
                                    ref_d[i]=detail::clamp01(best_d);
                                    ref_c[i]=std::min(0.65f,static_cast<float>(std::sqrt(std::max(0.0,den_p))*1.8));
                                } else {
                                    ref_d[i]=rel_d_zero;
                                    ref_c[i]=0.0f;
                                }
                            } else if(den_p<=1e-5 && c0>0.85) {
                                ref_c[i]=std::min(seed_conf[i],0.08f);
                            } else if(best_c<1.10 && den_p>3e-5 && (best_c<c_seed*0.80 || seed_conf[i]<0.40f)) {
                                ref_d[i]=best_d;
                                const float curv_conf=static_cast<float>(std::min(0.88,std::sqrt(std::max(0.0,den_p))*2.2));
                                ref_c[i]=detail::clamp01(std::max(seed_conf[i]*0.85f,curv_conf));
                            }
                        }
                    }
                }
            };
            if(parallel_rows) parallel_rows(out.H,refine_epi); else refine_epi(0,out.H);
            seed_depth.swap(ref_d);
            seed_conf.swap(ref_c);
        }
    }
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
                const float pb_occ=(out.geometry.plane_detilted && f_h_f>0.0f) ?
                    (out.geometry.plane_tilt_rho[0]*((static_cast<float>(yy)-cy_f)/f_h_f)+
                     out.geometry.plane_tilt_rho[1]*((static_cast<float>(xx)-cx_f)/f_h_f)) : 0.0f;
                const float magnitude=std::fabs(out.geometry.rho_low+depths[j]*rho_span-pb_occ);
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
                const float pb_occ=(out.geometry.plane_detilted && f_h_f>0.0f) ?
                    (out.geometry.plane_tilt_rho[0]*((static_cast<float>(yy)-cy_f)/f_h_f)+
                     out.geometry.plane_tilt_rho[1]*((static_cast<float>(xx)-cx_f)/f_h_f)) : 0.0f;
                const float magnitude=std::fabs(out.geometry.rho_low+depths[j]*rho_span-pb_occ);
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
        if(b.depth_gate.mode==DepthGateMode::Force) {
            size_t valid_seeds=0;
            for(float c:seed_conf) if(c>0.05f) ++valid_seeds;
            if(valid_seeds<32u) {
                std::vector<float> sh_vals;
                sh_vals.reserve(4096);
                const int step_s=std::max(1, static_cast<int>(std::sqrt(static_cast<double>(p)/4096.0)));
                for(int sy=1;sy<out.H-1;sy+=step_s) for(int sx=1;sx<out.W-1;sx+=step_s) {
                    const size_t idx=static_cast<size_t>(sy)*out.W+sx;
                    const float v0=b.g_halfs[0][idx];
                    if(!std::isfinite(v0)||v0>=0.995f) continue;
                    const float nsd=std::sqrt(detail::variance(b,v0));
                    const float gm=detail::gradient_mag(b.g_halfs[0],out.H,out.W,static_cast<float>(sy),static_cast<float>(sx));
                    if(!(gm>3.5f*nsd)) continue;
                    const float lap=std::fabs(b.g_halfs[0][idx-1]+b.g_halfs[0][idx+1]+
                                              b.g_halfs[0][idx-out.W]+b.g_halfs[0][idx+out.W]-4.0f*v0);
                    sh_vals.push_back(lap/(gm+3.0f*nsd));
                }
                if(!sh_vals.empty()) {
                    const float s_lo=detail::quantile(sh_vals,0.10f);
                    const float s_hi=std::max(s_lo+1e-4f,detail::quantile(sh_vals,0.92f));
                    for(int sy=1;sy<out.H-1;++sy) for(int sx=1;sx<out.W-1;++sx) {
                        const size_t idx=static_cast<size_t>(sy)*out.W+sx;
                        if(seed_conf[idx]>0.05f) continue;
                        const float v0=b.g_halfs[0][idx];
                        if(!std::isfinite(v0)||v0>=0.995f) continue;
                        const float nsd=std::sqrt(detail::variance(b,v0));
                        const float gm=detail::gradient_mag(b.g_halfs[0],out.H,out.W,static_cast<float>(sy),static_cast<float>(sx));
                        if(!(gm>3.5f*nsd)) continue;
                        const float lap=std::fabs(b.g_halfs[0][idx-1]+b.g_halfs[0][idx+1]+
                                                  b.g_halfs[0][idx-out.W]+b.g_halfs[0][idx+out.W]-4.0f*v0);
                        const float sh=lap/(gm+3.0f*nsd);
                        const float crlb=detail::clamp01((gm/std::max(1e-8f,nsd)-1.5f)/2.8f);
                        seed_depth[idx]=0.15f+0.75f*detail::clamp01((sh-s_lo)/(s_hi-s_lo));
                        seed_conf[idx]=0.25f*crlb;
                    }
                }
            }
        }
        std::vector<float> dense_depth,dense_conf;
        detail::complete_depth_multiscale(b,out.H,out.W,seed_depth,seed_conf,dense_depth,dense_conf,parallel_rows);
        out.maps["depth"]=std::move(dense_depth);
        if(want("depth_confidence")) out.maps["depth_confidence"]=std::move(dense_conf);
    }
    if(want("turbulence")) {
        out.gates["turbulence"].measured=max_turbulence;
        out.gates["turbulence"].observable=out.gates["turbulence"].observable&&max_turbulence>0.04f;
        if(!out.gates["turbulence"].observable)
            std::fill(out.maps["turbulence"].begin(),out.maps["turbulence"].end(),0);
    }
    if(want("dff_depth")) {
        TemporalOptions fopts;
        fopts.mode = TemporalMode::FocusStack;
        auto ft = prepare_temporal(b, fopts);
        out.gates["dff_depth"] = ft.gate;
        if (ft.gate.observable && ft.activity.size() == p) {
            std::vector<float> seed_c(p, 0.0f);
            const auto& guide_lum = (ft.guide.size() == p) ? ft.guide : b.g_halfs[0];
            for (size_t i = 0; i < p; ++i) {
                float wmax = 0.0f;
                for (size_t k = 0; k < n; ++k) wmax = std::max(wmax, ft.weights[k][i]);
                const float gmag = detail::gradient_mag(guide_lum, out.H, out.W,
                    static_cast<float>(i / out.W), static_cast<float>(i % out.W));
                const float nsd = std::sqrt(detail::variance(b, guide_lum[i]));
                seed_c[i] = (wmax > 0.25f) ? detail::clamp01((gmag / std::max(1e-7f, nsd) - 2.0f) / 4.0f) : 0.0f;
            }
            std::vector<float> dense_dff, dense_dff_c;
            detail::complete_depth_multiscale(b, out.H, out.W, ft.activity, seed_c, dense_dff, dense_dff_c, parallel_rows);
            out.maps["dff_depth"] = std::move(dense_dff);
        }
    }
    return out;
}

// Reconstruct real camera-linear RGB photons revealed behind foreground silhouettes
// using multi-frame epipolar background reprojection across unoccluded half-sets.
inline Image extract_disoccluded_background(const BurstView& b,const FieldResult& fields,
                                            const ParallelRows& parallel_rows = {},
                                            std::vector<float>* out_bg_depth = nullptr) {
    detail::validate(b);
    const int H=b.H1/2, W=b.W1/2;
    const size_t p=detail::pixels(H,W), n=b.frames_lin.size();
    const auto dep_it=fields.maps.find("depth");
    const auto gate_it=fields.gates.find("depth");
    Image bg{H, W, std::vector<float>(3*p, 0.0f)};
    const RowTask extract_rows=[&](int r0,int r1) {
        for(int y=r0;y<r1;++y) for(int x=0;x<W;++x) {
            const size_t i=static_cast<size_t>(y)*W+x;
            const auto base_c=detail::raw_rgb(b,0,2.0f*y+0.5f,2.0f*x+0.5f);
            for(size_t c=0;c<3;++c) bg.rgb[3*i+c]=base_c[c];
        }
    };
    if(parallel_rows) parallel_rows(H,extract_rows); else extract_rows(0,H);
    if(dep_it==fields.maps.end()||gate_it==fields.gates.end()||!gate_it->second.observable ||
       dep_it->second.size()!=p || fields.geometry.camera.size()!=n)
        return bg;

    const auto& depth=dep_it->second;
    const float rho_span=std::max(1e-6f, fields.geometry.rho_high-fields.geometry.rho_low);
    const float f_h=detail::half_focal_px(b);
    const float cy=0.5f*static_cast<float>(H-1), cx=0.5f*static_cast<float>(W-1);
    std::vector<uint8_t> is_fg_sil(p, 0);
    std::vector<float> far_bg_rho(p, 0.0f);
    std::vector<float> far_bg_g(p, 0.0f);
    const RowTask mark_sil=[&](int r0,int r1) {
        for(int y=r0;y<r1;++y) for(int x=0;x<W;++x) {
            const size_t i=static_cast<size_t>(y)*W+x;
            const float d0=depth[i];
            float d_min=d0, d_max=d0;
            int min_y=y, min_x=x;
            for(int dy=-12;dy<=12;dy+=2) for(int dx=-12;dx<=12;dx+=2) {
                const int yy=std::max(0,std::min(H-1,y+dy)), xx=std::max(0,std::min(W-1,x+dx));
                const float dq=depth[static_cast<size_t>(yy)*W+xx];
                if(dq<d_min) { d_min=dq; min_y=yy; min_x=xx; }
                if(dq>d_max) d_max=dq;
            }
            if(d0-d_min>=0.12f && (d_max-d_min)*rho_span>=0.65f) {
                is_fg_sil[i]=1;
                far_bg_rho[i]=fields.geometry.rho_low+d_min*rho_span;
                far_bg_g[i]=b.g_halfs[0][static_cast<size_t>(min_y)*W+min_x];
            }
        }
    };
    if(parallel_rows) parallel_rows(H,mark_sil); else mark_sil(0,H);
    if(out_bg_depth) {
        out_bg_depth->resize(p);
        for(size_t i=0;i<p;++i) {
            (*out_bg_depth)[i]=is_fg_sil[i] ?
                detail::clamp01((far_bg_rho[i]-fields.geometry.rho_low)/rho_span) : depth[i];
        }
    }

    // Collect real unoccluded photons from frames where foreground has shifted clear.
    std::vector<uint8_t> has_real(p, 0);
    const RowTask sample_bg=[&](int r0,int r1) {
        for(int y=r0;y<r1;++y) for(int x=0;x<W;++x) {
            const size_t i=static_cast<size_t>(y)*W+x;
            if(!is_fg_sil[i]) { has_real[i]=1; continue; }
            const float yf=static_cast<float>(y), xf=static_cast<float>(x);
            const float p_bias=(fields.geometry.plane_detilted && f_h>0.0f) ?
                (fields.geometry.plane_tilt_rho[0]*((yf-cy)/f_h)+fields.geometry.plane_tilt_rho[1]*((xf-cx)/f_h)) : 0.0f;
            const float r_bg=far_bg_rho[i]-p_bias;
            const float d_fg=depth[i];
            const float d_bg=detail::clamp01((far_bg_rho[i]-fields.geometry.rho_low)/rho_span);
            const float g_fg=b.g_halfs[0][i];
            const auto fg_rgb=detail::raw_rgb(b,0,2.0f*yf+0.5f,2.0f*xf+0.5f);
            const float nsd=std::sqrt(detail::variance(b,g_fg));
            double sum_rgb[3]={0,0,0}; double wsum=0.0;
            for(size_t k=1;k<n;++k) {
                const auto s=fields.geometry.camera[k].source(yf,xf);
                const auto ev=detail::epipolar_vec(b,fields.geometry,k,yf,xf);
                const float sy=s[0]+r_bg*ev[0], sx=s[1]+r_bg*ev[1];
                if(!detail::inside(sy,sx,H,W)) continue;
                const float rho_gap=d_fg*rho_span+fields.geometry.rho_low-far_bg_rho[i];
                const float shift_rel=std::hypot(ev[0],ev[1])*rho_gap;
                if(shift_rel<0.75f) continue;
                const float gk=detail::sample(b.g_halfs[k],H,W,sy,sx);
                if(!std::isfinite(gk)||gk>=0.995f) continue;
                const float y_occ=yf-rho_gap*ev[0], x_occ=xf-rho_gap*ev[1];
                bool cleared_geom=false;
                if(detail::inside(y_occ,x_occ,H,W)) {
                    const float d_occ=detail::sample(depth,H,W,y_occ,x_occ);
                    if(std::isfinite(d_occ) && d_occ<=d_bg+0.12f) cleared_geom=true;
                }
                const auto rgb_k=detail::raw_rgb(b,k,2.0f*sy+0.5f,2.0f*sx+0.5f);
                const float err_bg=std::fabs(gk-far_bg_g[i]);
                const float err_rgb_fg=std::max({std::fabs(rgb_k[0]-fg_rgb[0]),
                                                 std::fabs(rgb_k[1]-fg_rgb[1]),
                                                 std::fabs(rgb_k[2]-fg_rgb[2])});
                const float err_fg=std::max(std::fabs(gk-g_fg),err_rgb_fg);
                if(cleared_geom || (err_fg>3.0f*nsd && err_bg<=err_fg+2.5f*nsd)) {
                    const double wt=1.0/(1.0+static_cast<double>(err_bg/std::max(1e-5f,nsd)));
                    for(size_t c=0;c<3;++c) sum_rgb[c]+=wt*rgb_k[c];
                    wsum+=wt;
                }
            }
            if(wsum>1e-6) {
                const float inv_w=static_cast<float>(1.0/wsum);
                for(size_t c=0;c<3;++c) bg.rgb[3*i+c]=static_cast<float>(sum_rgb[c])*inv_w;
                has_real[i]=1;
            }
        }
    };
    if(parallel_rows) parallel_rows(H,sample_bg); else sample_bg(0,H);

    // Fill remaining interior foreground band softly from real/adjacent background pixels.
    std::vector<float> next_rgb=bg.rgb;
    std::vector<uint8_t> next_real=has_real;
    for(int pass=0;pass<12;++pass) {
        const RowTask fill_rows=[&](int r0,int r1) {
            for(int y=r0;y<r1;++y) for(int x=0;x<W;++x) {
                const size_t i=static_cast<size_t>(y)*W+x;
                if(has_real[i]) {
                    for(size_t c=0;c<3;++c) next_rgb[3*i+c]=bg.rgb[3*i+c];
                    next_real[i]=1;
                    continue;
                }
                double s_rgb[3]={0,0,0}; double ws=0.0;
                for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                    if(dy==0&&dx==0) continue;
                    const int yy=y+dy, xx=x+dx;
                    if(yy<0||yy>=H||xx<0||xx>=W) continue;
                    const size_t q=static_cast<size_t>(yy)*W+xx;
                    if(!has_real[q]) continue;
                    const double w=std::exp(-2.0*static_cast<double>(depth[q]))/(1.0+dy*dy+dx*dx);
                    for(size_t c=0;c<3;++c) s_rgb[c]+=w*bg.rgb[3*q+c];
                    ws+=w;
                }
                if(ws>1e-8) {
                    const float inv_w=static_cast<float>(1.0/ws);
                    for(size_t c=0;c<3;++c) next_rgb[3*i+c]=static_cast<float>(s_rgb[c])*inv_w;
                    next_real[i]=1;
                }
            }
        };
        if(parallel_rows) parallel_rows(H,fill_rows); else fill_rows(0,H);
        bg.rgb.swap(next_rgb);
        has_real.swap(next_real);
    }
    return bg;
}

// D-5: Closed-loop Stage 3 epipolar projection & [C4] semi-occluded frame unpeeling.
// Returns total (pixel, frame) pairs recovered onto Stage 4 fusion weights.
inline size_t apply_epipolar_stage3_feedback(const BurstView& b,const Geometry& g,
                                             Planes& flows_fy,Planes& flows_fx,Planes& mw_half,
                                             const ParallelRows& parallel_rows = {}) {
    detail::validate(b);
    const size_t n=b.frames_lin.size();
    const int H=b.H1/2, W=b.W1/2;
    const size_t p=detail::pixels(H,W);
    if(g.gate.forced || !g.gate.observable || g.disparity_span_px<1.5f ||
       g.disparity_span_px>0.35f*static_cast<float>(std::max(b.H1,b.W1)) ||
       g.rank1_coherence<0.65f || n<4 ||
       g.baseline.size()!=n || g.camera.size()!=n ||
       flows_fy.size()!=n || flows_fx.size()!=n || mw_half.size()!=n)
        return 0;
    for(size_t k=0;k<n;++k) {
        if(flows_fy[k].size()!=p || flows_fx[k].size()!=p || mw_half[k].size()!=p)
            return 0;
    }
    const float f_h=detail::half_focal_px(b);
    const float cy=0.5f*static_cast<float>(H-1), cx=0.5f*static_cast<float>(W-1);
    // Pass 1 (read-only) avoids data races when b.flows_* / b.mw_half alias mutable planes.
    std::vector<float> pre_rho(p, 0.0f), pre_rq(p, 0.0f);
    const RowTask pre_rows=[&](int r0,int r1) {
        for(int y=r0;y<r1;++y) for(int x=0;x<W;++x) {
            const size_t i=static_cast<size_t>(y)*W+x;
            pre_rho[i]=detail::rho_at(b,g,i,&pre_rq[i]);
        }
    };
    if(parallel_rows) parallel_rows(H,pre_rows); else pre_rows(0,H);
    std::vector<size_t> row_unpeeled(static_cast<size_t>(H), 0);
    const RowTask feedback_rows=[&](int r0,int r1) {
        std::vector<float> nbr_rhos;
        nbr_rhos.reserve(25);
        for(int y=r0;y<r1;++y) {
            size_t local_cnt=0;
            for(int x=0;x<W;++x) {
                const size_t i=static_cast<size_t>(y)*W+x;
                const float v0=b.g_halfs[0][i];
                if(!std::isfinite(v0)||v0>=0.995f) continue;
                const float nsd=std::sqrt(detail::variance(b,v0));
                const float yf=static_cast<float>(y), xf=static_cast<float>(x);
                const float p_bias=(g.plane_detilted && f_h>0.0f) ?
                    (g.plane_tilt_rho[0]*((yf-cy)/f_h)+g.plane_tilt_rho[1]*((xf-cx)/f_h)) : 0.0f;
                const float rq=pre_rq[i];
                const float rho=pre_rho[i];
                size_t n_vis=0, n_sup=0;
                for(size_t k=1;k<n;++k) {
                    if(mw_half[k][i]>=0.25f) ++n_vis;
                    else if(mw_half[k][i]<0.20f) ++n_sup;
                }
                const float rho_raw=rho-p_bias;

                // Part 1: Monotonic photometric-guarded projection of orthogonal jitter onto 1D epipole.
                if(n_vis>=2 && rq>=0.50f) {
                    for(size_t k=1;k<n;++k) {
                        if(mw_half[k][i]<0.25f) continue;
                        const auto s=g.camera[k].source(yf,xf);
                        const auto ev=detail::epipolar_vec(b,g,k,yf,xf);
                        const float cur_fy=flows_fy[k][i], cur_fx=flows_fx[k][i];
                        const float epi_fy=s[0]-yf+rho_raw*ev[0], epi_fx=s[1]-xf+rho_raw*ev[1];
                        const float dperp=std::hypot(cur_fy-epi_fy,cur_fx-epi_fx);
                        if(dperp<=0.70f && dperp>=0.04f &&
                           detail::inside(yf+cur_fy,xf+cur_fx,H,W) &&
                           detail::inside(yf+epi_fy,xf+epi_fx,H,W)) {
                            const float new_fy=0.45f*cur_fy+0.55f*epi_fy;
                            const float new_fx=0.45f*cur_fx+0.55f*epi_fx;
                            const float e_cur=std::fabs(detail::sample(b.g_halfs[k],H,W,yf+cur_fy,xf+cur_fx)-v0);
                            const float e_new=std::fabs(detail::sample(b.g_halfs[k],H,W,yf+new_fy,xf+new_fx)-v0);
                            if(e_new<=e_cur+0.20f*nsd) {
                                flows_fy[k][i]=new_fy; flows_fx[k][i]=new_fx;
                            }
                        }
                    }
                }

                // Part 2: [C4] Semi-occluded frame unpeeling via clean-neighbor epipolar flow + RAW color gate.
                if(n_sup>=1 && n_sup<=n-2 && b.mw_half[0][i]>=0.5f) {
                    float rho_use=rho_raw;
                    bool has_rho=(rq>=0.55f);
                    if(!has_rho) {
                        nbr_rhos.clear();
                        for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
                            const int yy=y+dy, xx=x+dx;
                            if(yy<0||yy>=H||xx<0||xx>=W) continue;
                            const size_t q=static_cast<size_t>(yy)*W+xx;
                            if(std::fabs(b.g_halfs[0][q]-v0)>3.0f*nsd) continue;
                            const float nq=pre_rq[q];
                            const float nr=pre_rho[q];
                            if(nq>=0.65f) {
                                const float nb_b=(g.plane_detilted && f_h>0.0f) ?
                                    (g.plane_tilt_rho[0]*((yy-cy)/f_h)+g.plane_tilt_rho[1]*((xx-cx)/f_h)) : 0.0f;
                                nbr_rhos.push_back(nr-nb_b);
                            }
                        }
                        if(nbr_rhos.size()>=4u) {
                            rho_use=detail::median(nbr_rhos);
                            has_rho=true;
                        }
                    }
                    if(has_rho) {
                        const auto ref_rgb=detail::raw_rgb(b,0,2.0f*yf+0.5f,2.0f*xf+0.5f);
                        for(size_t k=1;k<n;++k) {
                            if(mw_half[k][i]>=0.20f) continue;
                            const auto s=g.camera[k].source(yf,xf);
                            const auto ev=detail::epipolar_vec(b,g,k,yf,xf);
                            const float qy=s[0]+rho_use*ev[0], qx=s[1]+rho_use*ev[1];
                            if(qy<1.0f || qy>static_cast<float>(H-2) || qx<1.0f || qx>static_cast<float>(W-2)) continue;
                            static constexpr int oy[5]={0,-1,1,0,0}, ox[5]={0,0,0,-1,1};
                            float e_sum=0.0f; int e_cnt=0;
                            float center_diff=1e9f;
                            for(int t=0;t<5;++t) {
                                const int ry=y+oy[t], rx=x+ox[t];
                                if(ry<0||ry>=H||rx<0||rx>=W) continue;
                                const float gv=detail::sample(b.g_halfs[k],H,W,qy+oy[t],qx+ox[t]);
                                const float g0=b.g_halfs[0][static_cast<size_t>(ry)*W+rx];
                                if(!std::isfinite(gv)||!std::isfinite(g0)) continue;
                                const float d=std::fabs(gv-g0);
                                if(t==0) center_diff=d;
                                e_sum+=d; ++e_cnt;
                            }
                            if(e_cnt<3 || center_diff>2.4f*nsd || (e_sum/e_cnt)>2.1f*nsd) continue;
                            const auto k_rgb=detail::raw_rgb(b,k,2.0f*qy+0.5f,2.0f*qx+0.5f);
                            const float max_rgb_d=std::max({std::fabs(k_rgb[0]-ref_rgb[0]),
                                                            std::fabs(k_rgb[1]-ref_rgb[1]),
                                                            std::fabs(k_rgb[2]-ref_rgb[2])});
                            if(max_rgb_d<=3.0f*nsd) {
                                flows_fy[k][i]=qy-yf;
                                flows_fx[k][i]=qx-xf;
                                mw_half[k][i]=0.72f;
                                ++local_cnt;
                            }
                        }
                    }
                }
            }
            row_unpeeled[static_cast<size_t>(y)]=local_cnt;
        }
    };
    if(parallel_rows) parallel_rows(H,feedback_rows); else feedback_rows(0,H);
    size_t total=0;
    for(size_t c:row_unpeeled) total+=c;
    (void)p;
    return total;
}

struct RefocusOptions {
    float focus_depth = 0.5f;       // Same relative inverse-depth coordinates as map.
    float radius_px = 12.0f;        // Output-pixel disk radius per unit depth difference.
    int max_radius = 64;
    bool layered_occlusion = false; // Physical front-to-back layered aperture compositing.
    int aperture_blades = 0;        // 0 = circular disk; >=5 = regular polygonal stop.
    float catseye_vignette = 0.0f;  // [0..1] radial optical vignetting factor.
    const Image* revealed_bg = nullptr; // Optional burst-revealed background photons (same H, W).
    // Unified v3.0.0 optical refocus extensions (all default to inactive/compatible values):
    const std::vector<float>* revealed_bg_depth = nullptr;
    int revealed_bg_depth_h = 0;
    int revealed_bg_depth_w = 0;
    float tilt_x = 0.0f, tilt_y = 0.0f; // 3D Scheimpflug focal plane tilt
    float spherical_aberration = 0.0f;  // [-0.5, +0.5] Zernike Z20 front/back bokeh asymmetry
    float stf_apodization = 0.0f;       // [0, 1] STF smooth radial roll-off
    float blade_angle_deg = 0.0f;       // Aperture polygon rotation angle
    float native_coc_slope_px = 0.0f;   // Native captured CoC slope in output px per unit depth
    float native_focus_depth = 0.5f;
    bool restore_glints = false;
    const std::array<float, 3>* illuminant_rgb = nullptr;
};

inline Image render_refocus(const Image& image,const std::vector<float>& depth,
                            int depth_h,int depth_w,const RefocusOptions& options = {},
                            const ParallelRows& parallel_rows = {}) {
    const size_t p=detail::pixels(image.H,image.W),dp=detail::pixels(depth_h,depth_w);
    if(image.rgb.size()!=3*p||depth.size()!=dp)
        throw std::invalid_argument("Invalid refocus input");
    if(options.revealed_bg && (options.revealed_bg->H!=image.H||options.revealed_bg->W!=image.W||
        options.revealed_bg->rgb.size()!=3*p))
        throw std::invalid_argument("Invalid revealed_bg dimensions for refocus");
    heraw_refocus::RefocusParams params;
    params.focus_depth = options.focus_depth;
    params.radius_px = options.radius_px;
    params.max_radius = options.max_radius;
    params.layered_occlusion = options.layered_occlusion;
    params.aperture_blades = options.aperture_blades;
    params.catseye_vignette = options.catseye_vignette;
    params.revealed_bg_rgb = options.revealed_bg ? options.revealed_bg->rgb.data() : nullptr;
    if (options.revealed_bg_depth && !options.revealed_bg_depth->empty()) {
        params.revealed_bg_depth = options.revealed_bg_depth->data();
        params.revealed_bg_depth_h = options.revealed_bg_depth_h > 0 ? options.revealed_bg_depth_h : depth_h;
        params.revealed_bg_depth_w = options.revealed_bg_depth_w > 0 ? options.revealed_bg_depth_w : depth_w;
    }
    params.tilt_x = options.tilt_x;
    params.tilt_y = options.tilt_y;
    params.spherical_aberration = options.spherical_aberration;
    params.stf_apodization = options.stf_apodization;
    params.blade_angle_deg = options.blade_angle_deg;
    params.native_coc_slope_px = options.native_coc_slope_px;
    params.native_focus_depth = options.native_focus_depth;
    params.restore_glints = options.restore_glints;
    params.illuminant_rgb = options.illuminant_rgb;
    Image out{image.H, image.W, {}};
    out.rgb = heraw_refocus::render_refocus(image.rgb.data(), image.H, image.W, depth.data(), depth_h, depth_w, params, parallel_rows);
    return out;
}

inline Image render_refocus(const Image& image,const FieldResult& fields,
                            const RefocusOptions& options = {},const ParallelRows& parallel_rows = {}) {
    const auto depth=fields.maps.find("depth");
    const auto gate=fields.gates.find("depth");
    if(depth!=fields.maps.end()&&gate!=fields.gates.end()&&gate->second.observable)
        return render_refocus(image,depth->second,fields.H,fields.W,options,parallel_rows);
    const auto dff=fields.maps.find("dff_depth");
    const auto dff_gate=fields.gates.find("dff_depth");
    if(dff!=fields.maps.end()&&dff_gate!=fields.gates.end()&&dff_gate->second.observable)
        return render_refocus(image,dff->second,fields.H,fields.W,options,parallel_rows);
    throw std::invalid_argument("Refocus requires observable relative depth");
}

inline Image render_synthetic_aperture_refocus(const Planes& frames_lin, int H1, int W1, const uint8_t (&cfa)[6][6],
                                               float focal_length_mm, float pixel_pitch_um,
                                               float noise_shot, float noise_read_var,
                                               const FieldResult& fields, float focus_depth, float tilt_x, float tilt_y,
                                               bool see_through, int out_H, int out_W,
                                               const ParallelRows& parallel_rows = {},
                                               heraw_refocus::ApertureWeightMode weight_mode = heraw_refocus::ApertureWeightMode::Voronoi) {
    detail::pixels(H1, W1);
    const auto depth = fields.maps.find("depth");
    const auto gate = fields.gates.find("depth");
    if (depth == fields.maps.end() || gate == fields.gates.end() || !gate->second.observable ||
        frames_lin.size() < 2 ||
        fields.geometry.camera.size() != frames_lin.size() ||
        fields.geometry.baseline.size() != frames_lin.size())
        throw std::invalid_argument("Synthetic-aperture refocus requires calibrated epipolar geometry and depth");
    std::vector<std::array<double, 9>> cam_h(fields.geometry.camera.size());
    for (size_t k = 0; k < cam_h.size(); ++k) cam_h[k] = fields.geometry.camera[k].h;
    const float diag = std::hypot(static_cast<float>(W1), static_cast<float>(H1));
    const float pitch_mm = (pixel_pitch_um > 0.2f) ? (pixel_pitch_um * 1e-3f) : (43.266615f / std::max(1.0f, diag));
    const float hf_px = (focal_length_mm > 0.5f) ? (0.5f * focal_length_mm / std::max(1e-4f, pitch_mm)) : 0.0f;
    heraw_refocus::SyntheticApertureInput in;
    in.H1 = H1; in.W1 = W1;
    in.H_half = fields.H; in.W_half = fields.W;
    in.N = static_cast<int>(frames_lin.size());
    in.frames_lin = frames_lin.data();
    in.cfa6x6 = &cfa[0][0];
    in.depth_half = depth->second.data();
    in.rho_low = fields.geometry.rho_low;
    in.rho_high = fields.geometry.rho_high;
    in.half_focal_px = hf_px;
    in.plane_detilted = fields.geometry.plane_detilted;
    in.tilt_rho_y = fields.geometry.plane_tilt_rho[0];
    in.tilt_rho_x = fields.geometry.plane_tilt_rho[1];
    in.cam_h = cam_h.data();
    in.cam_cx = fields.geometry.camera[0].cx;
    in.cam_cy = fields.geometry.camera[0].cy;
    in.cam_scale = fields.geometry.camera[0].scale;
    in.base_yx = fields.geometry.baseline.data();
    in.base_z = fields.geometry.baseline_z.size() == frames_lin.size() ? fields.geometry.baseline_z.data() : nullptr;
    in.noise_shot = noise_shot;
    in.noise_read_var = noise_read_var;
    in.weight_mode = weight_mode;
    Image out{out_H, out_W, {}};
    out.rgb = heraw_refocus::render_synthetic_aperture(in, focus_depth, tilt_x, tilt_y, see_through, out_H, out_W, parallel_rows);
    return out;
}

inline Image render_synthetic_aperture_refocus(const BurstView& b, const FieldResult& fields,
                                               float focus_depth, float tilt_x, float tilt_y,
                                               bool see_through, int out_H, int out_W,
                                               const ParallelRows& parallel_rows = {},
                                               heraw_refocus::ApertureWeightMode weight_mode = heraw_refocus::ApertureWeightMode::Voronoi) {
    detail::validate(b);
    return render_synthetic_aperture_refocus(b.frames_lin, b.H1, b.W1, b.cfa, b.focal_length_mm,
                                             b.pixel_pitch_um, b.noise_shot, b.noise_read_var,
                                             fields, focus_depth, tilt_x, tilt_y, see_through,
                                             out_H, out_W, parallel_rows, weight_mode);
}

inline void deconvolve_shift_focus_bessel(Image& image, const std::vector<float>& depth, int depth_h, int depth_w,
                                          float cam_focus_depth, float target_focus_depth,
                                          float tilt_x, float tilt_y, float native_slope_px,
                                          float noise_var, const ParallelRows& parallel_rows = {}) {
    heraw_refocus::deconvolve_shift_focus_bessel(
        image.rgb, image.H, image.W, depth.data(), depth_h, depth_w,
        cam_focus_depth, target_focus_depth, tilt_x, tilt_y, native_slope_px, noise_var, parallel_rows);
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
// PNG with adaptive row filtering + RFC 1951 fixed-Huffman LZ77 deflate: self-contained, zero external libs.
inline std::vector<uint8_t> png_rgb8(std::span<const uint8_t> rgb,int h,int w) {
    const size_t p=pixels(h,w), stride=static_cast<size_t>(w)*3u;
    if(rgb.size()!=3*p) throw std::invalid_argument("PNG RGB dimensions mismatch");
    std::vector<uint8_t> scan;
    scan.reserve(p*3u+static_cast<size_t>(h));
    std::vector<uint8_t> row0(stride), row1(stride), row2(stride);
    for(int y=0;y<h;++y) {
        const size_t off=static_cast<size_t>(y)*stride;
        long s0=0, s1=0, s2=0;
        for(size_t x=0;x<stride;++x) {
            const uint8_t cur=rgb[off+x];
            const uint8_t left=(x>=3u) ? rgb[off+x-3u] : 0;
            const uint8_t up=(y>0) ? rgb[off-stride+x] : 0;
            row0[x]=cur;
            row1[x]=static_cast<uint8_t>(cur-left);
            row2[x]=static_cast<uint8_t>(cur-up);
            s0+=std::abs(static_cast<int>(static_cast<int8_t>(row0[x])));
            s1+=std::abs(static_cast<int>(static_cast<int8_t>(row1[x])));
            s2+=std::abs(static_cast<int>(static_cast<int8_t>(row2[x])));
        }
        uint8_t ftype=0; const std::vector<uint8_t>* pick=&row0;
        if(s1<=s0 && s1<=s2) { ftype=1; pick=&row1; }
        else if(s2<s0 && s2<s1) { ftype=2; pick=&row2; }
        scan.push_back(ftype);
        scan.insert(scan.end(),pick->begin(),pick->end());
    }
    uint32_t adl_a=1, adl_b=0;
    for(uint8_t v:scan) { adl_a=(adl_a+v)%65521u; adl_b=(adl_b+adl_a)%65521u; }
    std::vector<uint8_t> z{0x78,0x01};
    z.reserve(scan.size()/2u+64u);
    uint64_t bitbuf=0; int bitcnt=0;
    auto push_lsb=[&](uint32_t val,int nbits) {
        bitbuf|=static_cast<uint64_t>(val & ((1u<<nbits)-1u))<<bitcnt;
        bitcnt+=nbits;
        while(bitcnt>=8) { z.push_back(static_cast<uint8_t>(bitbuf & 0xFFu)); bitbuf>>=8; bitcnt-=8; }
    };
    auto push_huff=[&](uint32_t code,int nbits) {
        for(int bi=nbits-1;bi>=0;--bi) push_lsb((code>>bi)&1u,1);
    };
    auto emit_lit=[&](uint8_t lit) {
        if(lit<=143) push_huff(0x30u+lit,8);
        else push_huff(0x190u+(lit-144u),9);
    };
    static constexpr int base_len[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static constexpr int ext_len[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static constexpr int base_dist[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static constexpr int ext_dist[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    push_lsb(1u,1); // BFINAL=1
    push_lsb(1u,2); // BTYPE=01 (Fixed Huffman)
    constexpr size_t HT_SIZE=4096u;
    std::vector<int32_t> head(HT_SIZE,-1);
    auto hash4=[&](size_t idx) -> size_t {
        uint32_t v=(static_cast<uint32_t>(scan[idx])<<24)|(static_cast<uint32_t>(scan[idx+1])<<16)|
                   (static_cast<uint32_t>(scan[idx+2])<<8)|static_cast<uint32_t>(scan[idx+3]);
        return ((v*2654435761u)>>20) & (HT_SIZE-1u);
    };
    const size_t nbytes=scan.size();
    for(size_t i=0;i<nbytes;) {
        size_t best_len=0, best_dist=0;
        if(i+4u<=nbytes) {
            const size_t hkey=hash4(i);
            const int32_t prev=head[hkey];
            head[hkey]=static_cast<int32_t>(i);
            if(prev>=0 && (i-static_cast<size_t>(prev))<=32768u) {
                const size_t pr=static_cast<size_t>(prev);
                const size_t maxl=std::min<size_t>(258u,nbytes-i);
                size_t ml=0;
                while(ml<maxl && scan[pr+ml]==scan[i+ml]) ++ml;
                if(ml>=4u) { best_len=ml; best_dist=i-pr; }
            }
        }
        if(best_len>=4u) {
            int lk=28;
            if(best_len<258u) {
                lk=0;
                while(lk<27 && static_cast<size_t>(base_len[lk+1])<=best_len) ++lk;
            }
            const int sym=257+lk;
            if(sym<=279) push_huff(static_cast<uint32_t>(sym-256),7);
            else push_huff(0xC0u+static_cast<uint32_t>(sym-280),8);
            if(ext_len[lk]>0) push_lsb(static_cast<uint32_t>(best_len-static_cast<size_t>(base_len[lk])),ext_len[lk]);
            int dk=29;
            while(dk>0 && static_cast<size_t>(base_dist[dk])>best_dist) --dk;
            push_huff(static_cast<uint32_t>(dk),5);
            if(ext_dist[dk]>0) push_lsb(static_cast<uint32_t>(best_dist-static_cast<size_t>(base_dist[dk])),ext_dist[dk]);
            const size_t end_m=i+best_len;
            for(size_t m=i+1;m<end_m && m+4u<=nbytes;++m) head[hash4(m)]=static_cast<int32_t>(m);
            i=end_m;
        } else {
            emit_lit(scan[i]);
            ++i;
        }
    }
    push_huff(0u,7); // EOB symbol 256
    if(bitcnt>0) z.push_back(static_cast<uint8_t>(bitbuf & 0xFFu));
    be32(z,(adl_b<<16)|adl_a);
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

// rgb8 (and optional burst-revealed bg_rgb8) are display-rendered by caller;
// computational APIs stay camera-linear. PNG depth stores 16 bits as RG bytes.
// HTML assets and scripts are inline; WebGL2 renders a two-layer disoccluded mesh.
inline void write_webgl_html(const std::string& path,const std::vector<uint8_t>& rgb8,
                             int H,int W,const FieldResult& fields,float max_shift_px=24,
                             const std::vector<uint8_t>* bg_rgb8 = nullptr) {
    const size_t p=detail::pixels(H,W),dp=detail::pixels(fields.H,fields.W);
    const auto depth=fields.maps.find("depth");
    if(rgb8.size()!=3*p||depth==fields.maps.end()||depth->second.size()!=dp ||
        !std::isfinite(max_shift_px)||max_shift_px<0)
        throw std::invalid_argument("Invalid WebGL image/depth");
    if(bg_rgb8 && bg_rgb8->size()!=3*p)
        throw std::invalid_argument("Invalid WebGL revealed background dimensions");
    const auto gate=fields.gates.find("depth");
    if(gate==fields.gates.end()||!gate->second.observable)
        throw std::invalid_argument("WebGL requires observable relative depth");
    std::vector<uint8_t> packed(3*dp);
    for(size_t i=0;i<dp;++i) {
        if(!std::isfinite(depth->second[i])) throw std::invalid_argument("Nonfinite WebGL depth");
        const auto v=static_cast<uint16_t>(std::lround(detail::clamp01(depth->second[i])*65535));
        const size_t y=i/static_cast<size_t>(fields.W), x=i%static_cast<size_t>(fields.W);
        float dmin=depth->second[i];
        for(int dy=-6;dy<=6;dy+=2) for(int dx=-6;dx<=6;dx+=2) {
            const int yy=std::max(0,std::min(fields.H-1,static_cast<int>(y)+dy));
            const int xx=std::max(0,std::min(fields.W-1,static_cast<int>(x)+dx));
            dmin=std::min(dmin,depth->second[static_cast<size_t>(yy)*fields.W+xx]);
        }
        const auto vmin=static_cast<uint8_t>(std::lround(detail::clamp01(dmin)*255.0f));
        packed[3*i]=static_cast<uint8_t>(v>>8);
        packed[3*i+1]=static_cast<uint8_t>(v);
        packed[3*i+2]=vmin; // B channel stores 8-bit minimum local background depth.
    }
    const std::string color=detail::base64(detail::png_rgb8(rgb8,H,W));
    const std::string bg_color=bg_rgb8 ? detail::base64(detail::png_rgb8(*bg_rgb8,H,W)) : color;
    const std::string dep=detail::base64(detail::png_rgb8(packed,fields.H,fields.W));
    std::ofstream out(heraw_fs::from_utf8(path),std::ios::binary);
    if(!out) throw std::runtime_error("Cannot open WebGL output: "+path);
    const char* sign_label = fields.geometry.sign_calibrated
        ? (fields.geometry.sign_source.empty() ? "calibrated" : fields.geometry.sign_source.c_str())
        : "gauge";
    out << "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>NijiN.LiteBurstSuite V3 Light-Field Metrology Inspector</title>"
        "<style>html{height:100%;}body{margin:0;display:flex;flex-direction:column;height:100vh;min-height:520px;"
        "background:#121417;color:#e6eaef;font:12.5px/1.4 ui-monospace,SFMono-Regular,system-ui;overflow:hidden;}"
        "#bar{display:flex;align-items:center;gap:8px;flex-wrap:wrap;padding:6px 12px;background:#1a1d22;"
        "border-bottom:1px solid #2b3038;flex:0 0 auto;}"
        "#status{flex:1;min-width:220px;margin:0;color:#cfd6e0;}"
        "#probe{color:#7dd3fc;min-width:140px;font-variant-numeric:tabular-nums;}"
        ".pill{background:#232830;border:1px solid #353c47;color:#e6eaef;border-radius:4px;padding:3px 9px;cursor:pointer;font:inherit;}"
        ".pill.on{background:#34495e;border-color:#5a7d9e;color:#fff;}"
        "#stage{flex:1 1 auto;min-height:0;display:flex;align-items:center;justify-content:center;background:#121417;}"
        "canvas{max-width:100%;max-height:100%;width:100%;height:100%;object-fit:contain;display:block;touch-action:none;cursor:grab;}"
        "</style><body><div id=\"bar\"><p id=\"status\">"
        << "V3 Metrology | span=" << fields.geometry.disparity_span_px << " 1x px"
        << " | coh=" << fields.geometry.rank1_coherence
        << " | sign=" << sign_label;
    if (fields.geometry.metric_calibrated) {
        out << " | baseline=" << fields.geometry.estimated_baseline_mm << " mm"
            << " | span=" << fields.geometry.diopter_span_inv_m << " m^-1";
    }
    out << "</p><span id=\"probe\"></span>"
        "<button class=\"pill on\" id=\"m_lf\">Light-Field</button>"
        "<button class=\"pill\" id=\"m_dep\">Depth 16b</button>"
        "<button class=\"pill\" id=\"m_seam\">Tear Mask</button>"
        "<button class=\"pill on\" id=\"orbit\">Auto-Orbit</button>"
        "<button class=\"pill\" id=\"reset\">Reset</button>"
        "<button class=\"pill\" id=\"flip\">Flip Sign</button></div>"
        "<div id=\"stage\"><canvas id=\"view\"></canvas></div><script>\n"
        "'use strict';\nconst W=" << W << ",H=" << H << ",shift=" << max_shift_px << ",GRID=320;\n"
        "let canvas=document.getElementById('view');const status=document.getElementById('status'),probe=document.getElementById('probe');\n"
        "canvas.width=W;canvas.height=H;\n"
        "function decodePng(uri){\n"
        "  const b64=uri.slice(uri.indexOf(',')+1),bin=atob(b64),len=bin.length,u8=new Uint8Array(len);\n"
        "  for(let i=0;i<len;++i)u8[i]=bin.charCodeAt(i);\n"
        "  const blob=new Blob([u8],{type:'image/png'}),url=URL.createObjectURL(blob);\n"
        "  return new Promise((res,rej)=>{\n"
        "    const im=new Image();\n"
        "    im.onload=()=>{URL.revokeObjectURL(url);res(im);};\n"
        "    im.onerror=()=>{URL.revokeObjectURL(url);const fb=new Image();fb.onload=()=>res(fb);fb.onerror=e=>rej(e);fb.src=uri;};\n"
        "    im.src=url;\n"
        "  });\n"
        "}\n"
        "const vs=`#version 300 es\nprecision highp float;precision highp int;\n"
        "uniform sampler2D depth;uniform vec2 orbit;uniform vec2 size;uniform float signDepth;uniform int layer;uniform int grid;\n"
        "out vec2 uv;out float vDepth;out float vTear;\n"
        "float getDepth(vec2 q){vec2 v=textureLod(depth,q,0.0).rg;return (v.r*65280.0+v.g*255.0)/65535.0;}\n"
        "float getBgDepth(vec2 q){if(signDepth>=0.0)return textureLod(depth,q,0.0).b;"
        "vec2 d=vec2(3.0)/vec2(textureSize(depth,0));float m=getDepth(q);"
        "m=max(m,getDepth(q+vec2(d.x,0.0)));m=max(m,getDepth(q-vec2(d.x,0.0)));"
        "m=max(m,getDepth(q+vec2(0.0,d.y)));m=max(m,getDepth(q-vec2(0.0,d.y)));return m;}\n"
        "void main(){int corner=gl_VertexID%6;int cell=gl_VertexID/6;int x=cell%grid,y=cell/grid;\n"
        "vec2 offset=corner==0?vec2(0,0):corner==1?vec2(1,0):corner==2?vec2(0,1):"
        "corner==3?vec2(0,1):corner==4?vec2(1,0):vec2(1,1);\n"
        "float g=float(grid);uv=(vec2(x,y)+offset)/g;\n"
        "float dRaw=layer==0?getDepth(uv):getBgDepth(uv);\n"
        "vDepth=dRaw;vTear=0.0;\n"
        "if(layer==0){vec2 b0=vec2(x,y)/g,b1=vec2(x+1,y+1)/g;\n"
        "float d00=getDepth(b0),d10=getDepth(vec2(b1.x,b0.y)),d01=getDepth(vec2(b0.x,b1.y)),d11=getDepth(b1);\n"
        "float jump=max(max(abs(d00-d10),abs(d00-d01)),max(abs(d11-d10),abs(d11-d01)));\n"
        "vTear=step(0.075,jump);}\n"
        "float d=(dRaw-0.5)*signDepth;\n"
        "vec2 q=uv+orbit*d/size;gl_Position=vec4(q.x*2.0-1.0,1.0-q.y*2.0,-d+(layer==1?0.03:0.0),1);}\n`;\n"
        "const fs=`#version 300 es\nprecision highp float;precision highp int;\nuniform sampler2D color;uniform sampler2D bgColor;uniform int layer;uniform int viewMode;uniform float uSignDepth;\n"
        "in vec2 uv;in float vDepth;in float vTear;out vec4 frag;\n"
        "vec3 turbo(float t){\n"
        "  float x=clamp(t,0.0,1.0);\n"
        "  vec4 r=vec4(0.1357,4.6154,-42.6603,132.1311)*vec4(1.0,x,x*x,x*x*x);\n"
        "  vec4 g=vec4(0.0914,2.1942,4.8430,-14.1850)*vec4(1.0,x,x*x,x*x*x);\n"
        "  vec4 b=vec4(0.1067,12.6419,-60.5820,110.3628)*vec4(1.0,x,x*x,x*x*x);\n"
        "  float x4=x*x*x*x,x5=x4*x;\n"
        "  return clamp(vec3(dot(r,vec4(1.0))-152.9424*x4+59.2864*x5,"
        "dot(g,vec4(1.0))+4.2773*x4+2.8296*x5,"
        "dot(b,vec4(1.0))-89.9031*x4+27.3482*x5),0.0,1.0);}\n"
        "void main(){if(layer==0 && vTear>0.5 && viewMode!=2) discard;\n"
        "if(viewMode==1){float dv=uSignDepth>=0.0?vDepth:(1.0-vDepth);frag=vec4(turbo(dv),1);return;}\n"
        "if(viewMode==2){frag=vTear>0.5?vec4(0.95,0.35,0.15,1):vec4(texture(color,uv).rgb*0.75,1);return;}\n"
        "vec3 c=(layer==1)?texture(bgColor,uv).rgb:texture(color,uv).rgb;\n"
        "frag=vec4(c,1);}`;\n"
        "const U_FG='data:image/png;base64," << color << "';\n"
        "const U_DEP='data:image/png;base64," << dep << "';\n"
        "const U_BG='data:image/png;base64," << bg_color << "';\n"
        "Promise.all([decodePng(U_FG),decodePng(U_DEP),decodePng(U_BG)]).then(([imFg,imDep,imBg])=>{\n"
        "const dCv=document.createElement('canvas');dCv.width=imDep.width;dCv.height=imDep.height;\n"
        "const dCtx=dCv.getContext('2d');dCtx.drawImage(imDep,0,0);\n"
        "const dPx=dCtx.getImageData(0,0,imDep.width,imDep.height).data;\n"
        "let ox=0,oy=0,sgn=1,mode=0,autoOrb=true,t0=performance.now();\n"
        "const gl=canvas.getContext('webgl2',{antialias:true});\n"
        "let drawFn=null;\n"
        "if(gl){\n"
        "  try{\n"
        "    const sh=(tp,src)=>{const s=gl.createShader(tp);gl.shaderSource(s,src);gl.compileShader(s);"
        "if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s;};\n"
        "    const prog=gl.createProgram();gl.attachShader(prog,sh(gl.VERTEX_SHADER,vs));\n"
        "    gl.attachShader(prog,sh(gl.FRAGMENT_SHADER,fs));gl.linkProgram(prog);\n"
        "    if(!gl.getProgramParameter(prog,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(prog));\n"
        "    gl.useProgram(prog);gl.bindVertexArray(gl.createVertexArray());\n"
        "    const bindTex=(im,u,nm,flt)=>{gl.activeTexture(gl.TEXTURE0+u);gl.bindTexture(gl.TEXTURE_2D,gl.createTexture());\n"
        "      gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,gl.NONE);\n"
        "      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,im);\n"
        "      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,flt);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,flt);\n"
        "      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);\n"
        "      gl.uniform1i(gl.getUniformLocation(prog,nm),u);};\n"
        "    bindTex(imFg,0,'color',gl.LINEAR);bindTex(imDep,1,'depth',gl.NEAREST);bindTex(imBg,2,'bgColor',gl.LINEAR);\n"
        "    gl.enable(gl.DEPTH_TEST);gl.clearColor(0.07,0.08,0.09,1);\n"
        "    gl.uniform2f(gl.getUniformLocation(prog,'size'),W,H);\n"
        "    gl.uniform1i(gl.getUniformLocation(prog,'grid'),GRID);\n"
        "    const uOrb=gl.getUniformLocation(prog,'orbit'),uSgn=gl.getUniformLocation(prog,'signDepth'),uFSgn=gl.getUniformLocation(prog,'uSignDepth'),\n"
        "    uLay=gl.getUniformLocation(prog,'layer'),uMod=gl.getUniformLocation(prog,'viewMode');\n"
        "    drawFn=()=>{gl.viewport(0,0,W,H);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);\n"
        "      gl.uniform2f(uOrb,ox,oy);gl.uniform1f(uSgn,sgn);gl.uniform1f(uFSgn,sgn);gl.uniform1i(uMod,mode);\n"
        "      if(mode==0){gl.uniform1i(uLay,1);gl.drawArrays(gl.TRIANGLES,0,GRID*GRID*6);}\n"
        "      gl.uniform1i(uLay,0);gl.drawArrays(gl.TRIANGLES,0,GRID*GRID*6);};\n"
        "  }catch(e){drawFn=null;}\n"
        "}\n"
        "if(!drawFn){\n"
        "  const fb=document.createElement('canvas');fb.id='view';fb.width=W;fb.height=H;\n"
        "  canvas.replaceWith(fb);canvas=fb;const c2=fb.getContext('2d');\n"
        "  drawFn=()=>{if(!c2)return;c2.clearRect(0,0,W,H);c2.drawImage(mode===1?imDep:imFg,0,0,W,H);};\n"
        "}\n"
        "const orbBtn=document.getElementById('orbit');\n"
        "function tick(now){if(autoOrb){const t=(now-t0)*0.0016;ox=Math.cos(t)*0.55*shift;oy=Math.sin(t)*0.40*shift;drawFn();requestAnimationFrame(tick);}}\n"
        "canvas.onpointermove=e=>{autoOrb=false;orbBtn.classList.remove('on');const r=canvas.getBoundingClientRect();\n"
        "  const u=Math.max(0,Math.min(1,(e.clientX-r.left)/Math.max(1,r.width))),v=Math.max(0,Math.min(1,(e.clientY-r.top)/Math.max(1,r.height)));\n"
        "  ox=(u-0.5)*2*shift;oy=(v-0.5)*2*shift;drawFn();\n"
        "  const dx=Math.min(imDep.width-1,Math.floor(u*imDep.width)),dy=Math.min(imDep.height-1,Math.floor(v*imDep.height));\n"
        "  const di=(dy*imDep.width+dx)*4,rv=(dPx[di]*256+dPx[di+1])/65535.0;\n"
        "  probe.textContent='rho='+(sgn>=0?rv:(1-rv)).toFixed(3);};\n"
        "canvas.onpointerleave=()=>{probe.textContent='';ox=0;oy=0;drawFn();};\n"
        "const btns=['m_lf','m_dep','m_seam'];\n"
        "btns.forEach((id,idx)=>{document.getElementById(id).onclick=()=>{mode=idx;\n"
        "  btns.forEach((b,j)=>document.getElementById(b).classList.toggle('on',j===idx));drawFn();};});\n"
        "orbBtn.onclick=()=>{autoOrb=!autoOrb;orbBtn.classList.toggle('on',autoOrb);if(autoOrb)requestAnimationFrame(tick);};\n"
        "document.getElementById('reset').onclick=()=>{autoOrb=false;orbBtn.classList.remove('on');ox=0;oy=0;sgn=1;drawFn();};\n"
        "document.getElementById('flip').onclick=()=>{sgn=-sgn;drawFn();};\n"
        "drawFn();requestAnimationFrame(tick);\n"
        "}).catch(e=>{status.textContent=e.message||'Image decode failed';});\n</script></body></html>\n";
    if(!out) throw std::runtime_error("Failed writing WebGL output: "+path);
}

// R10: Axial magnification + defocus squared difference diopter estimator.
// Solves c_tilde_k^2 = K^2 * (D - D_f / m_k)^2 with D_f > 0 & breathing/multi-depth gates.
struct AxialMagDiopterResult {
    bool observable = false;
    bool ambiguous_zoom_breathing = false;
    double diopter_est_inv_m = 0.0;
    double fisher_info = 0.0;
    double residual_rms = 0.0;
    std::string reason;
};

inline AxialMagDiopterResult evaluate_axial_magnification_diopters(
    double K_px_m, double D_f_inv_m,
    std::span<const double> m_k, std::span<const double> c_tilde_sq_k,
    bool has_multi_depth_or_telemetry = true) {
    AxialMagDiopterResult res;
    const size_t n = std::min(m_k.size(), c_tilde_sq_k.size());
    if (n < 2 || !(K_px_m > 1e-9)) {
        res.reason = "insufficient_frames_or_K";
        return res;
    }
    if (!(D_f_inv_m > 1e-4)) {
        res.reason = "D_f == 0 invariant pullback";
        return res;
    }
    if (!has_multi_depth_or_telemetry) {
        res.ambiguous_zoom_breathing = true;
        res.reason = "ambiguous_zoom_breathing_without_telemetry_or_multidepth";
        return res;
    }
    std::vector<double> r(n), y(n);
    double r_bar = 0.0, r2_bar = 0.0, y_bar = 0.0;
    int valid = 0;
    for (size_t k = 0; k < n; ++k) {
        if (!(m_k[k] > 1e-4) || !std::isfinite(c_tilde_sq_k[k])) continue;
        r[static_cast<size_t>(valid)] = D_f_inv_m / m_k[k];
        y[static_cast<size_t>(valid)] = c_tilde_sq_k[k] / (K_px_m * K_px_m);
        r_bar += r[static_cast<size_t>(valid)];
        r2_bar += r[static_cast<size_t>(valid)] * r[static_cast<size_t>(valid)];
        y_bar += y[static_cast<size_t>(valid)];
        ++valid;
    }
    if (valid < 2) {
        res.reason = "insufficient_valid_magnification_samples";
        return res;
    }
    r_bar /= valid;
    r2_bar /= valid;
    y_bar /= valid;
    double num = 0.0, den = 0.0;
    for (int k = 0; k < valid; ++k) {
        const double dr = r[static_cast<size_t>(k)] - r_bar;
        const double dz = (r[static_cast<size_t>(k)] * r[static_cast<size_t>(k)] - r2_bar) -
                          (y[static_cast<size_t>(k)] - y_bar);
        num += dr * dz;
        den += 2.0 * dr * dr;
    }
    if (den <= 1e-12) {
        res.reason = "zero_magnification_baseline";
        return res;
    }
    double D = num / den;
    // Gauss-Newton refinement on sum_k (y_k - (D - r_k)^2)^2
    for (int it = 0; it < 6; ++it) {
        double h = 0.0, g = 0.0;
        for (int k = 0; k < valid; ++k) {
            const double d = D - r[static_cast<size_t>(k)];
            const double err = d * d - y[static_cast<size_t>(k)];
            const double j = 2.0 * d;
            g += j * err;
            h += j * j;
        }
        if (h <= 1e-14) break;
        const double step = g / h;
        D -= step;
        if (std::fabs(step) < 1e-12) break;
    }
    double rss = 0.0, finfo = 0.0;
    for (int k = 0; k < valid; ++k) {
        const double d = D - r[static_cast<size_t>(k)];
        const double err = K_px_m * K_px_m * (d * d - y[static_cast<size_t>(k)]);
        rss += err * err;
        const double j = 2.0 * K_px_m * K_px_m * d;
        finfo += j * j;
    }
    res.diopter_est_inv_m = D;
    res.fisher_info = finfo;
    res.residual_rms = std::sqrt(rss / valid);
    res.observable = std::isfinite(D);
    res.reason = res.observable ? "ok" : "nonfinite";
    return res;
}

// Q7: Cross-focus variable projection spectral depth cost:
// J(d) = sum_k w_k |Y_k|^2 - |sum_k w_k conj(H_k) Y_k|^2 / (sum_k w_k |H_k|^2 + reg)
inline double cross_focus_spectral_depth_cost(
    std::span<const double> y_re, std::span<const double> y_im,
    std::span<const double> h_re, std::span<const double> h_im,
    std::span<const double> inv_var, double reg = 1e-8) {
    const size_t n = std::min({y_re.size(), y_im.size(), h_re.size(), h_im.size(), inv_var.size()});
    if (n == 0) return 0.0;
    double y_e = 0.0, hy_re = 0.0, hy_im = 0.0, h_e = reg;
    for (size_t k = 0; k < n; ++k) {
        const double w = std::max(0.0, inv_var[k]);
        const double yr = y_re[k], yi = y_im[k];
        const double hr = h_re[k], hi = h_im[k];
        y_e += w * (yr * yr + yi * yi);
        hy_re += w * (hr * yr + hi * yi);
        hy_im += w * (hr * yi - hi * yr);
        h_e += w * (hr * hr + hi * hi);
    }
    const double proj_e = (hy_re * hy_re + hy_im * hy_im) / std::max(1e-18, h_e);
    return std::max(0.0, y_e - proj_e);
}

inline double cross_focus_spectral_depth_cost(
    std::span<const double> y_re, std::span<const double> y_im,
    std::span<const double> h_otf_real, std::span<const double> inv_var, double reg = 1e-8) {
    const size_t n = std::min({y_re.size(), y_im.size(), h_otf_real.size(), inv_var.size()});
    std::vector<double> zeros(n, 0.0);
    return cross_focus_spectral_depth_cost(y_re, y_im, h_otf_real.first(n), zeros, inv_var, reg);
}

} // namespace heraw_suite
