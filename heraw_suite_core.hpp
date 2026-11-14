// Shared CPU/GPU camera-linear Suite stages. C++17, header-only.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace heraw_core {

using Frames = std::vector<std::vector<float>>;
using RangeTask = std::function<void(int, int)>;
// The callback must execute each index exactly once and join before returning.
struct Execution {
    int tile = 64;
    std::function<void(int, const RangeTask&)> parallel;
};
struct Noise {
    float read_variance = 1e-6f;
    float shot = 0.0f;
    // Optional N-element arrays after exposure normalization. Caller owns them.
    const float* frame_read_variances = nullptr;
    const float* frame_shot_coefficients = nullptr;
    const float* frame_saturation_levels = nullptr;
    float variance(float signal) const {
        return std::max(1e-12f, read_variance + shot * std::max(0.0f, signal));
    }
    float frame_variance(std::size_t n, float signal) const {
        const float read = frame_read_variances ? frame_read_variances[n] : read_variance;
        const float gain = frame_shot_coefficients ? frame_shot_coefficients[n] : shot;
        return std::max(1e-12f, read + gain * std::max(0.0f, signal));
    }
    bool unsaturated(std::size_t n, float signal) const {
        return !frame_saturation_levels || signal < frame_saturation_levels[n];
    }
};
struct ExposureStats {
    std::vector<float> gains; // Multiplier applied to each source, reference = 1.
    std::vector<float> variance_scales;
    std::vector<float> read_variances, shot_coefficients, saturation_levels;
    std::size_t samples = 0;
};
struct StageStats {
    std::size_t samples = 0, changed = 0, rejected = 0;
    double before = 0.0, after = 0.0, rms_update = 0.0;
    double noise_variance = 0.0, signal_power = 0.0;
    std::size_t peak_scratch_bytes = 0; // Per worker, excludes caller buffers.
    int iterations = 0;
};
struct HalfStackOptions {
    float strength = 1.0f;
    float effective_frames = 0.0f;
    bool phase_balanced = false;
    // Optional H2*W2 static/effective-count maps, in reference coordinates.
    const float* effective_count = nullptr;
    const float* static_weight = nullptr;
    // Optional 16-bin power response of the reconstruction kernel, DC first.
    const float* kernel_power = nullptr;
};
struct SpatialPsfZone {
    // Normalized image coordinates [0,1], sigma in RGB2x pixels, radians.
    float cx = .5f, cy = .5f;
    float sigma_major_2x = 0, sigma_minor_2x = 0, angle = 0;
    bool valid = false;
};

// Power transfer in the native 4x4 Walsh order, DC=1. Covariance units
// are squared output pixels. The inverse-covariance overload converts
// reconstruction precision (usually in RAW pixels) using pixel_scale=2.
std::array<float, 16> kernel_power_from_covariance(float cov_xx, float cov_xy,
                                                  float cov_yy,
                                                  float minimum_power = .02f);
std::array<float, 16> kernel_power_from_inverse_covariance(float inv_xx,
                                                          float inv_xy,
                                                          float inv_yy,
                                                          float pixel_scale = 2.0f,
                                                          float minimum_power = .02f);
// Compact measured Gaussian PSF, support ceil(3*sigma) capped at 12 pixels.
// Noise is the effective noise of rgb2x. Invalid/unmeasured zones are skipped.
// Additive matched residual descent preserves signed camera-linear values.
StageStats spatial_psf_deconvolve(float* rgb2x, int H2, int W2,
                                 const std::vector<SpatialPsfZone>& zones,
                                 Noise noise, int iterations = 1,
                                 float strength = .7f,
                                 const Execution& execution = {});

// H1/W1 are even RAW dimensions; RGB2x is interleaved camera-linear RGB,
// H2=2*H1, W2=2*W1. Signed samples are retained by all stages.
// fy/fx/mw have N planes of (H1/2)*(W1/2), or are empty (identity/unit).
// Flow is reference -> source, in half-resolution pixels. A RAW coordinate
// (r,c) reads flow at (r/2,c/2), and source=(r,c)+2*(fy,fx).
// CFA is the sensor-origin 6x6 repeating table, R=0 G=1 B=2.
ExposureStats exposure_normalize(Frames& raw, Frames& g_halfs, int H1, int W1,
                                Noise noise = {}, const Frames& fy = {},
                                const Frames& fx = {}, const Frames& mw = {},
                                const Execution& execution = {});
StageStats silicon_residual_cleanup(Frames& raw, int H1, int W1,
                                   const uint8_t* cfa6x6, const Frames& fy,
                                   const Frames& fx, const Frames& mw, Noise noise,
                                   const float* scene_rgb2x = nullptr,
                                   const Execution& execution = {});
StageStats row_rolling_shutter_smooth(Frames& fy, Frames& fx, const Frames& mw,
                                    int Hh, int Wh, float strength = 0.5f,
                                    const Execution& execution = {});
// Output N half-resolution planes with four interleaved directional weights.
StageStats directional_lucky_weights(const Frames& g_halfs, int Hh, int Wh,
                                     const Frames& fy, const Frames& fx,
                                     Noise noise, Frames& weights,
                                     const Execution& execution = {});
StageStats compact_ibp(const Frames& raw, int H1, int W1,
                       const uint8_t* cfa6x6, const Frames& fy,
                       const Frames& fx, const Frames& mw, Noise noise,
                       float* rgb2x, int iterations = 1,
                       const Execution& execution = {});
StageStats color_line_refine(float* rgb2x, int H2, int W2, Noise noise,
                            float strength = 0.25f,
                            const Execution& execution = {});
// Caller can provide independent reconstructed halves, or use the RAW overload
// below to reconstruct small half-stack tiles without N full RGB canvases.
StageStats half_stack_denoise(float* rgb2x, const float* half_a,
                             const float* half_b, int H2, int W2, Noise noise,
                             const HalfStackOptions& options = {},
                             const Execution& execution = {});
StageStats half_stack_denoise(const Frames& raw, int H1, int W1,
                             const uint8_t* cfa6x6, const Frames& fy,
                             const Frames& fx, const Frames& mw, Noise noise,
                             float* rgb2x, const HalfStackOptions& options = {},
                             const Execution& execution = {});

namespace detail {
inline int clamp(int x, int n) { return std::max(0, std::min(n - 1, x)); }
inline float clamp01(float x) { return std::max(0.0f, std::min(1.0f, x)); }
inline void run(int n, const Execution& e, const RangeTask& f) {
    if (e.parallel) e.parallel(n, f); else f(0, n);
}
inline void shape(int h, int w) {
    if (h < 1 || w < 1 || h > (1 << 25) || w > (1 << 25))
        throw std::invalid_argument("invalid image dimensions");
}
inline void planes(const Frames& p, std::size_t n, std::size_t size,
                   bool optional = true) {
    if (optional && p.empty()) return;
    if (p.size() != n) throw std::invalid_argument("plane count mismatch");
    for (const auto& v : p)
        if (v.size() != size) throw std::invalid_argument("plane size mismatch");
}
inline void validate(const Frames& raw, int h, int w, const uint8_t* cfa,
                     const Frames& fy, const Frames& fx, const Frames& mw) {
    shape(h, w);
    if ((h & 1) || (w & 1) || raw.empty())
        throw std::invalid_argument("RAW dimensions must be even and stack nonempty");
    planes(raw, raw.size(), std::size_t(h) * w, false);
    const auto half = std::size_t(h / 2) * (w / 2);
    planes(fy, raw.size(), half); planes(fx, raw.size(), half);
    planes(mw, raw.size(), half);
    if (fy.empty() != fx.empty()) throw std::invalid_argument("flow pair required");
    if (cfa) for (int k = 0; k < 36; ++k)
        if (cfa[k] > 2) throw std::invalid_argument("invalid CFA color");
}
inline float sample(const float* a, int h, int w, float y, float x,
                    int stride = 1, int channel = 0) {
    y = std::max(0.0f, std::min(float(h - 1), y));
    x = std::max(0.0f, std::min(float(w - 1), x));
    const int r = int(y), c = int(x), r1 = std::min(r + 1, h - 1),
              c1 = std::min(c + 1, w - 1);
    const float ty = y - r, tx = x - c;
    auto at = [&](int yy, int xx) { return a[(std::size_t(yy) * w + xx) * stride + channel]; };
    return (1 - ty) * ((1 - tx) * at(r, c) + tx * at(r, c1)) +
           ty * ((1 - tx) * at(r1, c) + tx * at(r1, c1));
}
inline float field(const Frames& p, std::size_t n, int h, int w,
                   float y, float x, float fallback = 0.0f) {
    return p.empty() ? fallback : sample(p[n].data(), h, w, y, x);
}
inline bool inside(float y, float x, int h, int w) {
    return std::isfinite(y) && std::isfinite(x) && y >= 0 && x >= 0 &&
           y <= h - 1 && x <= w - 1;
}
inline float median(std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    auto m = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), m, v.end());
    return *m;
}
inline void inverse_flow(const Frames& fy, const Frames& fx, std::size_t n,
                         int h, int w, float sy, float sx, float& ry, float& rx) {
    ry = sy; rx = sx;
    for (int k = 0; k < 5 && !fy.empty(); ++k) {
        const float dy = field(fy, n, h, w, ry * .5f, rx * .5f);
        const float dx = field(fx, n, h, w, ry * .5f, rx * .5f);
        ry = sy - 2 * dy; rx = sx - 2 * dx;
    }
}
inline float raw_color(const std::vector<float>& a, int h, int w,
                       const uint8_t* cfa, float y, float x, int color) {
    const int iy = int(std::floor(y)), ix = int(std::floor(x));
    float sum = 0, weight = 0;
    for (int r = std::max(0, iy - 2); r <= std::min(h - 1, iy + 3); ++r)
        for (int c = std::max(0, ix - 2); c <= std::min(w - 1, ix + 3); ++c)
            if (cfa[(r % 6) * 6 + c % 6] == color) {
                const float k = std::max(0.0f, 3.0f - std::fabs(r - y)) *
                                std::max(0.0f, 3.0f - std::fabs(c - x));
                const float value = a[std::size_t(r) * w + c];
                if (std::isfinite(value)) { sum += k * value; weight += k; }
            }
    return weight > 0 ? sum / weight : 0;
}
inline void add_stats(StageStats& a, const StageStats& b) {
    a.samples += b.samples; a.changed += b.changed; a.rejected += b.rejected;
    a.before += b.before; a.after += b.after;
    a.rms_update += b.rms_update; a.noise_variance += b.noise_variance;
    a.signal_power += b.signal_power;
    a.peak_scratch_bytes = std::max(a.peak_scratch_bytes, b.peak_scratch_bytes);
}
} // namespace detail

inline ExposureStats exposure_normalize(Frames& raw, Frames& g, int h, int w,
                                        Noise noise, const Frames& fy,
                                        const Frames& fx, const Frames& mw,
                                        const Execution& execution) {
    detail::validate(raw, h, w, nullptr, fy, fx, mw);
    const int hh = h / 2, wh = w / 2;
    detail::planes(g, raw.size(), std::size_t(hh) * wh, false);
    ExposureStats result;
    result.gains.assign(raw.size(), 1);
    result.variance_scales.assign(raw.size(), 1);
    result.read_variances.assign(raw.size(), noise.read_variance);
    result.shot_coefficients.assign(raw.size(), noise.shot);
    result.saturation_levels.assign(raw.size(), .98f);
    if (noise.frame_read_variances) result.read_variances[0] = noise.frame_read_variances[0];
    if (noise.frame_shot_coefficients) result.shot_coefficients[0] = noise.frame_shot_coefficients[0];
    if (noise.frame_saturation_levels) result.saturation_levels[0] = noise.frame_saturation_levels[0];
    std::vector<std::size_t> counts(raw.size());
    detail::run(int(raw.size()), execution, [&](int begin, int end) {
        for (int n = std::max(1, begin); n < end; ++n) {
            std::vector<float> ratios;
            const int step = std::max(1, int(std::sqrt(double(hh) * wh / 32768.0)));
            for (int r = 0; r < hh; r += step) for (int c = 0; c < wh; c += step) {
                const float y = r + detail::field(fy, n, hh, wh, float(r), float(c));
                const float x = c + detail::field(fx, n, hh, wh, float(r), float(c));
                if (!detail::inside(y, x, hh, wh) ||
                    detail::field(mw, n, hh, wh, float(r), float(c), 1) < .5f) continue;
                const float a = g[0][std::size_t(r) * wh + c];
                const float b = detail::sample(g[n].data(), hh, wh, y, x);
                const float low = 4 * std::sqrt(noise.read_variance > 0 ? noise.read_variance : 0);
                if (a > low && b > low && a < .85f && b < .85f && std::isfinite(a + b))
                    ratios.push_back(std::log(a / b));
            }
            counts[n] = ratios.size();
            if (ratios.size() >= 8) {
                const float center = detail::median(ratios);
                std::vector<float> dev(ratios.size());
                for (std::size_t i = 0; i < ratios.size(); ++i) dev[i] = std::fabs(ratios[i] - center);
                const float tolerance = std::max(.015f, 3 * 1.4826f * detail::median(dev));
                double sum = 0; int count = 0;
                for (float v : ratios) if (std::fabs(v - center) <= tolerance) { sum += v; ++count; }
                const float gain = std::exp(float(sum / std::max(1, count)));
                result.gains[n] = std::max(1.0f / 64, std::min(64.0f, gain));
                result.variance_scales[n] = result.gains[n] * result.gains[n];
            }
            const float read = noise.frame_read_variances ? noise.frame_read_variances[n] : noise.read_variance;
            const float shot = noise.frame_shot_coefficients ? noise.frame_shot_coefficients[n] : noise.shot;
            result.read_variances[n] = read * result.variance_scales[n];
            result.shot_coefficients[n] = shot * result.gains[n];
            result.saturation_levels[n] = (noise.frame_saturation_levels ? noise.frame_saturation_levels[n] : .98f) * result.gains[n];
            for (float& v : raw[n]) v *= result.gains[n];
            for (float& v : g[n]) v *= result.gains[n];
        }
    });
    result.samples = std::accumulate(counts.begin(), counts.end(), std::size_t(0));
    return result;
}

inline StageStats row_rolling_shutter_smooth(Frames& fy, Frames& fx, const Frames& mw,
                                            int h, int w, float strength,
                                            const Execution& execution) {
    detail::shape(h, w);
    detail::planes(fx, fy.size(), std::size_t(h) * w, false);
    detail::planes(fy, fy.size(), std::size_t(h) * w, false);
    detail::planes(mw, fy.size(), std::size_t(h) * w);
    strength = detail::clamp01(strength);
    std::vector<StageStats> stats(fy.size());
    detail::run(int(fy.size()), execution, [&](int begin, int end) {
        for (int n = begin; n < end; ++n) {
            for (Frames* flows : {&fy, &fx}) {
                std::vector<float> row(h), smooth(h), values;
                values.reserve(w);
                for (int r = 0; r < h; ++r) {
                    values.clear();
                    for (int c = 0; c < w; ++c) {
                        auto i = std::size_t(r) * w + c;
                        if ((mw.empty() || mw[n][i] >= .5f) && std::isfinite((*flows)[n][i]))
                            values.push_back((*flows)[n][i]);
                    }
                    row[r] = values.empty() ? (r ? row[r - 1] : 0) : detail::median(values);
                }
                // Cubic B-spline row model, preserving affine rolling shear.
                const int k[5] = {1, 4, 6, 4, 1};
                for (int r = 0; r < h; ++r) {
                    float sum = 0, den = 0;
                    for (int d = -2; d <= 2; ++d) if (r + d >= 0 && r + d < h) {
                        sum += k[d + 2] * row[r + d]; den += k[d + 2];
                    }
                    smooth[r] = (r < 2 || r >= h - 2) ? row[r] : sum / den;
                    const float delta = strength * (smooth[r] - row[r]);
                    for (int c = 0; c < w; ++c) {
                        const auto i = std::size_t(r) * w + c;
                        const float gate = mw.empty() ? 1 : detail::clamp01(mw[n][i]);
                        (*flows)[n][i] += gate * delta;
                        stats[n].rms_update += double(gate * delta) * (gate * delta);
                        ++stats[n].samples;
                        if (gate * delta != 0) ++stats[n].changed;
                    }
                }
                stats[n].peak_scratch_bytes = std::size_t(2 * h + w) * sizeof(float);
            }
        }
    });
    StageStats out; for (auto& s : stats) detail::add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.samples));
    return out;
}

inline StageStats directional_lucky_weights(const Frames& g, int h, int w,
                                             const Frames& fy, const Frames& fx,
                                             Noise noise, Frames& weights,
                                             const Execution& execution) {
    detail::shape(h, w);
    detail::planes(g, g.size(), std::size_t(h) * w, false);
    detail::planes(fy, g.size(), std::size_t(h) * w);
    detail::planes(fx, g.size(), std::size_t(h) * w);
    if (fy.empty() != fx.empty()) throw std::invalid_argument("flow pair required");
    weights.assign(g.size(), std::vector<float>(std::size_t(h) * w * 4, 1));
    const int dy[4] = {0, 1, 1, 1}, dx[4] = {1, 1, 0, -1};
    detail::run(h, execution, [&](int begin, int end) {
        for (int r = begin; r < end; ++r) for (int c = 0; c < w; ++c) {
            float best[4] = {};
            for (std::size_t n = 0; n < g.size(); ++n) {
                const float y = r + detail::field(fy, n, h, w, float(r), float(c));
                const float x = c + detail::field(fx, n, h, w, float(r), float(c));
                const float mid = detail::sample(g[n].data(), h, w, y, x);
                for (int d = 0; d < 4; ++d) {
                    float score = 0;
                    // Neighborhood energy avoids a single derivative zero crossing.
                    for (int t = -1; t <= 1; ++t) {
                        const float yy = y + t * dx[d], xx = x - t * dy[d];
                        const float v = detail::sample(g[n].data(), h, w, yy + dy[d], xx + dx[d]) +
                                        detail::sample(g[n].data(), h, w, yy - dy[d], xx - dx[d]) -
                                    2 * detail::sample(g[n].data(), h, w, yy, xx);
                        score += v * v / 3;
                    }
                    score = std::max(0.0f, score - 6 * noise.variance(mid));
                    weights[n][(std::size_t(r) * w + c) * 4 + d] = score;
                    best[d] = std::max(best[d], score);
                }
            }
            for (std::size_t n = 0; n < g.size(); ++n) for (int d = 0; d < 4; ++d) {
                float& v = weights[n][(std::size_t(r) * w + c) * 4 + d];
                v = best[d] > 1e-12f ? .25f + .75f * std::sqrt(v / best[d]) : 1;
            }
        }
    });
    StageStats out; out.samples = std::size_t(h) * w * g.size() * 4;
    for (const auto& a : weights) for (float v : a) {
        out.before += 1; out.after += v; if (v < .999f) ++out.changed;
    }
    return out;
}

namespace detail {
struct Footprint {
    std::array<std::size_t, 16> index{};
    std::array<float, 16> weight{};
    int count = 0;
};
// Pixel-aperture tent, width two RAW pixels, normalized at sensor boundaries.
// The very same coefficients are used in projection and its exact transpose.
inline Footprint footprint(float ry, float rx, int h2, int w2, int color) {
    Footprint a;
    const float y = 2 * ry, x = 2 * rx;
    const int r0 = int(std::floor(y)) - 1, c0 = int(std::floor(x)) - 1;
    float sum = 0;
    for (int r = r0; r < r0 + 4; ++r) for (int c = c0; c < c0 + 4; ++c) {
        if (r < 0 || c < 0 || r >= h2 || c >= w2) continue;
        const float k = std::max(0.0f, 1 - .5f * std::fabs(r - y)) *
                        std::max(0.0f, 1 - .5f * std::fabs(c - x));
        if (k <= 0) continue;
        a.index[a.count] = (std::size_t(r) * w2 + c) * 3 + color;
        a.weight[a.count++] = k; sum += k;
    }
    if (sum > 0) for (int k = 0; k < a.count; ++k) a.weight[k] /= sum;
    return a;
}
inline float project(const float* rgb, const Footprint& a) {
    float value = 0;
    for (int k = 0; k < a.count; ++k) value += a.weight[k] * rgb[a.index[k]];
    return value;
}
inline double forward_error(const Frames& raw, int h, int w, const uint8_t* cfa,
                            const Frames& fy, const Frames& fx, const Frames& mw,
                            Noise noise, const float* rgb, std::size_t& count) {
    double sum = 0; count = 0;
    for (std::size_t n = 0; n < raw.size(); ++n)
        for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
            float y, x;
            inverse_flow(fy, fx, n, h / 2, w / 2, float(r), float(c), y, x);
            if (!inside(y, x, h, w)) continue;
            const float v = raw[n][std::size_t(r) * w + c];
            if (!std::isfinite(v) || !noise.unsaturated(n, v)) continue;
            const float weight = clamp01(field(mw, n, h / 2, w / 2, .5f * y, .5f * x, 1));
            if (weight <= 0) continue;
            const auto a = footprint(y, x, 2 * h, 2 * w, cfa[(r % 6) * 6 + c % 6]);
            const double residual = v - project(rgb, a);
            sum += residual * residual * weight / noise.frame_variance(n, v); ++count;
        }
    return sum;
}
} // namespace detail

inline StageStats silicon_residual_cleanup(Frames& raw, int h, int w,
                                          const uint8_t* cfa, const Frames& fy,
                                          const Frames& fx, const Frames& mw,
                                          Noise noise, const float* scene_rgb,
                                          const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!cfa) throw std::invalid_argument("CFA required");
    StageStats out;
    if (raw.size() < 4 || fy.empty()) return out;
    float dither = 0;
    for (std::size_t n = 1; n < raw.size(); ++n)
        for (std::size_t i = 0; i < fy[n].size(); i += std::max<std::size_t>(1, fy[n].size() / 4096))
            dither = std::max(dither, 2 * std::hypot(fy[n][i], fx[n][i]));
    if (dither < 4) return out;
    const int hh = h / 2, wh = w / 2;
    std::vector<float> scene;
    if (!scene_rgb) {
        scene.resize(std::size_t(hh) * wh * 3);
        detail::run(hh, execution, [&](int first, int last) {
            std::vector<float> values; values.reserve(raw.size());
            for (int r = first; r < last; ++r) for (int c = 0; c < wh; ++c)
                for (int k = 0; k < 3; ++k) {
                    values.clear();
                    for (std::size_t n = 0; n < raw.size(); ++n) {
                        const float y = 2 * (r + detail::field(fy, n, hh, wh, float(r), float(c)));
                        const float x = 2 * (c + detail::field(fx, n, hh, wh, float(r), float(c)));
                        if (!detail::inside(y, x, h, w) ||
                            detail::field(mw, n, hh, wh, float(r), float(c), 1) < .7f) continue;
                        values.push_back(detail::raw_color(raw[n], h, w, cfa, y, x, k));
                    }
                    scene[(std::size_t(r) * wh + c) * 3 + k] =
                        values.empty() ? detail::raw_color(raw[0], h, w, cfa, float(2 * r), float(2 * c), k)
                                       : detail::median(values);
                }
        });
    }
    auto predicted = [&](float y, float x, int color) {
        if (scene_rgb)
            return detail::project(scene_rgb, detail::footprint(y, x, 2 * h, 2 * w, color));
        return detail::sample(scene.data(), hh, wh, y * .5f, x * .5f, 3, color);
    };
    auto residual = [&](std::size_t n, int r, int c, float& value) {
        float ry, rx;
        detail::inverse_flow(fy, fx, n, hh, wh, float(r), float(c), ry, rx);
        if (!detail::inside(ry, rx, h, w) ||
            detail::field(mw, n, hh, wh, .5f * ry, .5f * rx, 1) < .7f) return false;
        const int k = cfa[(r % 6) * 6 + c % 6];
        const float estimate = predicted(ry, rx, k);
        const float sigma = std::sqrt(noise.variance(estimate));
        const float gx = .5f * (predicted(ry, rx + 1, k) - predicted(ry, rx - 1, k));
        const float gy = .5f * (predicted(ry + 1, rx, k) - predicted(ry - 1, rx, k));
        const float v = raw[n][std::size_t(r) * w + c];
        if (!std::isfinite(v + estimate) || estimate >= .15f ||
            gx * gx + gy * gy >= 4 * sigma * sigma) return false;
        value = v - estimate;
        return true;
    };
    std::vector<std::vector<float>> row(raw.size(), std::vector<float>(h)),
                                    col(raw.size(), std::vector<float>(w));
    auto highpass = [](std::vector<float>& v, const std::vector<unsigned char>& valid) {
        const auto source = v;
        for (int i = 0; i < int(v.size()); ++i) {
            if (!valid[i]) { v[i] = 0; continue; }
            double sum = 0, den = 0;
            for (int d = -48; d <= 48; ++d) {
                const int j = i + d;
                if (j < 0 || j >= int(v.size()) || !valid[j]) continue;
                const double weight = std::exp(-.5 * d * d / (16 * 16));
                sum += weight * source[j]; den += weight;
            }
            v[i] = source[i] - float(sum / std::max(1e-20, den));
        }
    };
    detail::run(int(raw.size()), execution, [&](int first, int last) {
        for (int n = first; n < last; ++n) {
            std::vector<float> values; values.reserve(std::max(h, w));
            std::vector<unsigned char> valid(h);
            for (int r = 0; r < h; ++r) {
                values.clear();
                for (int c = 0; c < w; c += 2) {
                    float v;
                    if (residual(n, r, c, v)) values.push_back(v);
                }
                if (values.size() >= 8) { row[n][r] = detail::median(values); valid[r] = 1; }
            }
            highpass(row[n], valid);
            valid.assign(w, 0);
            for (int c = 0; c < w; ++c) {
                values.clear();
                for (int r = 0; r < h; r += 2) {
                    float v;
                    if (residual(n, r, c, v)) values.push_back(v - row[n][r]);
                }
                if (values.size() >= 8) { col[n][c] = detail::median(values); valid[c] = 1; }
            }
            highpass(col[n], valid);
        }
    });
    std::vector<StageStats> row_stats(h);
    detail::run(h, execution, [&](int first, int last) {
        std::vector<float> values; values.reserve(raw.size());
        for (int r = first; r < last; ++r) for (int c = 0; c < w; ++c) {
            values.clear();
            for (std::size_t n = 0; n < raw.size(); ++n) {
                float v;
                if (residual(n, r, c, v)) values.push_back(v - row[n][r] - col[n][c]);
            }
            float hot = 0;
            if (values.size() * 4 >= raw.size() * 3 && values.size() >= 3) {
                const float center = detail::median(values);
                const float sigma = std::sqrt(noise.variance(.075f));
                int agree = 0;
                for (float v : values)
                    if (v * center > 0 && std::fabs(v - center) < std::max(3 * sigma, .35f * std::fabs(center))) ++agree;
                if (std::fabs(center) > 4 * sigma && agree * 4 >= int(values.size()) * 3) hot = center;
            }
            for (std::size_t n = 0; n < raw.size(); ++n) {
                const float correction = row[n][r] + col[n][c] + hot;
                float v;
                if (residual(n, r, c, v)) {
                    row_stats[r].before += double(v) * v;
                    row_stats[r].after += double(v - correction) * (v - correction);
                    ++row_stats[r].samples;
                }
                raw[n][std::size_t(r) * w + c] -= correction;
                row_stats[r].rms_update += double(correction) * correction;
                if (correction != 0) ++row_stats[r].changed;
            }
        }
    });
    for (auto& s : row_stats) detail::add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, std::size_t(h) * w * raw.size()));
    out.peak_scratch_bytes = (scene.size() + raw.size() * (h + w + 1)) * sizeof(float);
    return out;
}

inline StageStats compact_ibp(const Frames& raw, int h, int w, const uint8_t* cfa,
                              const Frames& fy, const Frames& fx, const Frames& mw,
                              Noise noise, float* rgb, int iterations,
                              const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!rgb || !cfa) throw std::invalid_argument("RGB and CFA required");
    StageStats out;
    iterations = std::max(0, std::min(2, iterations));
    if (!iterations) return out;
    const int h2 = 2 * h, w2 = 2 * w, tile = std::max(16, std::min(256, execution.tile));
    const int nr = (h2 + tile - 1) / tile, nc = (w2 + tile - 1) / tile;
    std::vector<std::array<float, 4>> bounds(raw.size(), {0, 0, 0, 0});
    if (!fy.empty()) for (std::size_t n = 0; n < raw.size(); ++n)
        for (std::size_t i = 0; i < fy[n].size(); ++i) {
            if (!std::isfinite(fy[n][i]) || !std::isfinite(fx[n][i]))
                throw std::invalid_argument("nonfinite flow");
            bounds[n][0] = std::min(bounds[n][0], 2 * fy[n][i]);
            bounds[n][1] = std::max(bounds[n][1], 2 * fy[n][i]);
            bounds[n][2] = std::min(bounds[n][2], 2 * fx[n][i]);
            bounds[n][3] = std::max(bounds[n][3], 2 * fx[n][i]);
        }
    out.before = detail::forward_error(raw, h, w, cfa, fy, fx, mw, noise, rgb, out.samples);
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        // Four-color block coordinate descent keeps halo reads race-free.
        for (int color = 0; color < 4; ++color) detail::run(nr * nc, execution, [&](int first, int last) {
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != color) continue;
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h2 - r0), tw = std::min(tile, w2 - c0);
                std::vector<float> gradient(std::size_t(th) * tw * 3, 0), mass(gradient.size(), 0);
                for (std::size_t n = 0; n < raw.size(); ++n) {
                    const int sy0 = std::max(0, int(std::floor(.5f * (r0 - 2) + bounds[n][0])) - 1);
                    const int sy1 = std::min(h - 1, int(std::ceil(.5f * (r0 + th + 2) + bounds[n][1])) + 1);
                    const int sx0 = std::max(0, int(std::floor(.5f * (c0 - 2) + bounds[n][2])) - 1);
                    const int sx1 = std::min(w - 1, int(std::ceil(.5f * (c0 + tw + 2) + bounds[n][3])) + 1);
                    for (int r = sy0; r <= sy1; ++r) for (int c = sx0; c <= sx1; ++c) {
                        float ry, rx;
                        detail::inverse_flow(fy, fx, n, h / 2, w / 2, float(r), float(c), ry, rx);
                        if (!detail::inside(ry, rx, h, w) || 2 * ry < r0 - 2 ||
                            2 * ry > r0 + th + 1 || 2 * rx < c0 - 2 || 2 * rx > c0 + tw + 1) continue;
                        const float v = raw[n][std::size_t(r) * w + c];
                        if (!std::isfinite(v) || !noise.unsaturated(n, v)) continue;
                        const float gate = detail::clamp01(detail::field(mw, n, h / 2, w / 2, .5f * ry, .5f * rx, 1));
                        if (gate <= 0) continue;
                        const float iw = gate / noise.frame_variance(n, v);
                        const auto a = detail::footprint(ry, rx, h2, w2, cfa[(r % 6) * 6 + c % 6]);
                        const float residual = v - detail::project(rgb, a);
                        for (int k = 0; k < a.count; ++k) {
                            const std::size_t pixel = a.index[k] / 3;
                            const int y = int(pixel / w2) - r0, x = int(pixel % w2) - c0;
                            if (y < 0 || y >= th || x < 0 || x >= tw) continue;
                            const auto local = (std::size_t(y) * tw + x) * 3 + a.index[k] % 3;
                            gradient[local] += a.weight[k] * iw * residual;
                            // A positive row-normalized kernel gives this diagonal
                            // majorizer, so the step is a descent for weighted L2.
                            mass[local] += a.weight[k] * iw;
                        }
                    }
                }
                for (int r = 0; r < th; ++r) for (int c = 0; c < tw; ++c)
                    for (int channel = 0; channel < 3; ++channel) {
                        const auto i = (std::size_t(r) * tw + c) * 3 + channel;
                        if (mass[i] <= 0) continue;
                        const float delta = .9f * gradient[i] / mass[i];
                        rgb[(std::size_t(r + r0) * w2 + c + c0) * 3 + channel] += delta;
                        stats[ti].rms_update += double(delta) * delta;
                        if (delta != 0) ++stats[ti].changed;
                    }
                stats[ti].peak_scratch_bytes = gradient.size() * sizeof(float) * 2;
            }
        });
    }
    for (const auto& s : stats) detail::add_stats(out, s);
    std::size_t count = 0;
    out.after = detail::forward_error(raw, h, w, cfa, fy, fx, mw, noise, rgb, count);
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.changed));
    out.iterations = iterations;
    return out;
}

inline StageStats color_line_refine(float* rgb, int h, int w, Noise noise,
                                   float strength, const Execution& execution) {
    detail::shape(h, w);
    if (!rgb) throw std::invalid_argument("RGB required");
    strength = detail::clamp01(strength);
    const int tile = std::max(16, std::min(256, execution.tile));
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    for (int color = 0; color < 4; ++color)
        detail::run(nr * nc, execution, [&](int first, int last) {
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != color) continue;
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h - r0), tw = std::min(tile, w - c0);
                std::vector<float> output(std::size_t(th) * tw * 3);
                for (int r = 0; r < th; ++r) for (int c = 0; c < tw; ++c) {
                    const int y = r + r0, x = c + c0;
                    const auto p = (std::size_t(y) * w + x) * 3;
                    float mean[3] = {}, lap[3] = {};
                    float chroma_grad = 0, lum_grad = 0;
                    for (const auto& delta : {std::pair<int, int>{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                        const int yy = detail::clamp(y + delta.first, h), xx = detail::clamp(x + delta.second, w);
                        const auto q = (std::size_t(yy) * w + xx) * 3;
                        for (int k = 0; k < 3; ++k) mean[k] += .25f * rgb[q + k];
                        const float dg = rgb[q + 1] - rgb[p + 1];
                        lum_grad += dg * dg;
                        for (int k : {0, 2}) {
                            const float dc = rgb[q + k] - rgb[p + k] - dg;
                            chroma_grad += dc * dc;
                        }
                    }
                    const float variance = noise.variance(rgb[p + 1]);
                    const float gate = strength * (lum_grad / (lum_grad + 8 * variance)) *
                                       (8 * variance / (8 * variance + chroma_grad));
                    float shared = 0, precision = 0;
                    for (int k = 0; k < 3; ++k) {
                        lap[k] = rgb[p + k] - mean[k];
                        const float iw = 1 / noise.variance(rgb[p + k]);
                        shared += iw * lap[k]; precision += iw;
                    }
                    shared /= precision;
                    for (int k = 0; k < 3; ++k) {
                        const float d = gate * (shared - lap[k]);
                        output[(std::size_t(r) * tw + c) * 3 + k] = rgb[p + k] + d;
                        stats[ti].rms_update += double(d) * d;
                        ++stats[ti].samples; if (d != 0) ++stats[ti].changed;
                    }
                }
                for (int r = 0; r < th; ++r)
                    std::copy_n(output.data() + std::size_t(r) * tw * 3, std::size_t(tw) * 3,
                                rgb + (std::size_t(r0 + r) * w + c0) * 3);
                stats[ti].peak_scratch_bytes = output.size() * sizeof(float);
            }
        });
    StageStats out; for (auto& s : stats) detail::add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / std::max<std::size_t>(1, out.samples));
    return out;
}

namespace detail {
inline void hadamard(float* a, int n) {
    for (int width = 1; width < n; width *= 2)
        for (int base = 0; base < n; base += 2 * width)
            for (int k = 0; k < width; ++k) {
                const float u = a[base + k], v = a[base + k + width];
                a[base + k] = u + v; a[base + k + width] = u - v;
            }
    const float scale = 1 / std::sqrt(float(n));
    for (int i = 0; i < n; ++i) a[i] *= scale;
}
inline void transform4(float* a) {
    for (int r = 0; r < 4; ++r) hadamard(a + r * 4, 4);
    for (int c = 0; c < 4; ++c) {
        float column[4];
        for (int r = 0; r < 4; ++r) column[r] = a[r * 4 + c];
        hadamard(column, 4);
        for (int r = 0; r < 4; ++r) a[r * 4 + c] = column[r];
    }
}
inline std::array<float, 9> klt_basis(const std::vector<float>& rgb,
                                     const std::array<float, 3>& scale) {
    double mean[3] = {}, cov[3][3] = {};
    const std::size_t count = rgb.size() / 3;
    for (std::size_t i = 0; i < count; ++i)
        for (int c = 0; c < 3; ++c) mean[c] += rgb[i * 3 + c] / scale[c];
    for (double& v : mean) v /= double(std::max<std::size_t>(1, count));
    for (std::size_t i = 0; i < count; ++i)
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
            cov[r][c] += (rgb[i * 3 + r] / scale[r] - mean[r]) *
                         (rgb[i * 3 + c] / scale[c] - mean[c]) / double(count);
    std::array<float, 9> basis = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int sweep = 0; sweep < 12; ++sweep) {
        int p = 0, q = 1;
        if (std::fabs(cov[0][2]) > std::fabs(cov[p][q])) { p = 0; q = 2; }
        if (std::fabs(cov[1][2]) > std::fabs(cov[p][q])) { p = 1; q = 2; }
        if (std::fabs(cov[p][q]) < 1e-12) break;
        const double angle = .5 * std::atan2(2 * cov[p][q], cov[q][q] - cov[p][p]);
        const double cs = std::cos(angle), sn = std::sin(angle);
        for (int i = 0; i < 3; ++i) {
            const double a = cov[i][p], b = cov[i][q];
            cov[i][p] = cs * a - sn * b; cov[i][q] = sn * a + cs * b;
        }
        for (int i = 0; i < 3; ++i) {
            const double a = cov[p][i], b = cov[q][i];
            cov[p][i] = cs * a - sn * b; cov[q][i] = sn * a + cs * b;
            const float u = basis[i * 3 + p], v = basis[i * 3 + q];
            basis[i * 3 + p] = float(cs * u - sn * v);
            basis[i * 3 + q] = float(sn * u + cs * v);
        }
    }
    return basis;
}
// Fill produces two independent camera-linear estimates and measured N_eff.
template<class Fill>
inline StageStats denoise_tiles(float* rgb, int h, int w, Noise noise,
                                const HalfStackOptions& opt, const Execution& execution,
                                bool independent, Fill fill) {
    shape(h, w);
    if (!rgb) throw std::invalid_argument("RGB required");
    const int tile = 64; // A shared color KLT per 64x64 region.
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    const float strength = clamp01(opt.strength);
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    for (int checker = 0; checker < 4; ++checker)
        run(nr * nc, execution, [&](int first, int last) {
            for (int ti = first; ti < last; ++ti) {
                const int tr = ti / nc, tc = ti % nc;
                if ((tr % 2) * 2 + tc % 2 != checker) continue;
                const int r0 = tr * tile, c0 = tc * tile;
                const int th = std::min(tile, h - r0), tw = std::min(tile, w - c0);
                const int bh = (th + 3) / 4 + 4, bw = (tw + 3) / 4 + 4;
                const int ph = bh * 4, pw = bw * 4;
                const std::size_t pixels = std::size_t(ph) * pw;
                std::vector<float> input(pixels * 3), a(input.size()), b(input.size()), neff(pixels);
                std::array<float, 3> scales{};
                for (int r = 0; r < ph; ++r) for (int c = 0; c < pw; ++c) {
                    const int y = clamp(r0 + r - 8, h), x = clamp(c0 + c - 8, w);
                    const auto p = std::size_t(r) * pw + c;
                    for (int k = 0; k < 3; ++k) {
                        input[p * 3 + k] = rgb[(std::size_t(y) * w + x) * 3 + k];
                        scales[k] += noise.variance(input[p * 3 + k]) / float(pixels);
                    }
                    fill(y, x, a.data() + p * 3, b.data() + p * 3, neff[p]);
                }
                for (float& s : scales) s = std::sqrt(std::max(1e-12f, s));
                const auto basis = klt_basis(input, scales);
                std::vector<float> ci(input.size()), ca(input.size()), cb(input.size());
                auto forward = [&](const std::vector<float>& src, std::vector<float>& dst) {
                    for (int by = 0; by < bh; ++by) for (int bx = 0; bx < bw; ++bx)
                        for (int k = 0; k < 3; ++k) {
                            float block[16];
                            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                                const auto p = (std::size_t(by * 4 + y) * pw + bx * 4 + x) * 3;
                                float v = 0;
                                for (int j = 0; j < 3; ++j) v += basis[j * 3 + k] * src[p + j] / scales[j];
                                block[y * 4 + x] = v;
                            }
                            transform4(block);
                            std::copy_n(block, 16, dst.data() + (std::size_t(by) * bw + bx) * 48 + k * 16);
                        }
                };
                forward(input, ci);
                if (independent) { forward(a, ca); forward(b, cb); }
                std::vector<float> output(std::size_t(th) * tw * 3);
                for (int by = 2; by < bh - 2; ++by) for (int bx = 2; bx < bw - 2; ++bx) {
                    const auto block_index = std::size_t(by) * bw + bx;
                    float count = std::numeric_limits<float>::max(), gate = 1;
                    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                        const auto p = std::size_t(by * 4 + y) * pw + bx * 4 + x;
                        count = std::min(count, neff[p]);
                        const auto global = std::size_t(clamp(r0 + by * 4 + y - 8, h)) * w +
                                            clamp(c0 + bx * 4 + x - 8, w);
                        if (opt.effective_count) count = std::min(count, opt.effective_count[global]);
                        if (opt.static_weight) gate = std::min(gate, clamp01(opt.static_weight[global]));
                    }
                    const bool ensemble = independent && opt.phase_balanced && count >= 5 && gate >= .7f;
                    const float analytic = 1 / std::max(1.0f, count);
                    float channels[3][16];
                    for (int k = 0; k < 3; ++k) {
                        float filtered[16];
                        for (int f = 0; f < 16; ++f) {
                            const auto idx = block_index * 48 + k * 16 + f;
                            const float hp = opt.kernel_power ? std::max(.02f, opt.kernel_power[f]) : 1;
                            double signal = 0, nv = 0;
                            // Nine spatially distinct blocks, rather than a single
                            // noisy positive cross-product, set the spectral gain.
                            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                                const auto j = (std::size_t(by + dy) * bw + bx + dx) * 48 + k * 16 + f;
                                if (ensemble) {
                                    const double difference = ca[j] - cb[j];
                                    nv += .25 * difference * difference / hp;
                                    signal += double(ca[j]) * cb[j] / hp;
                                } else signal += double(ci[j]) * ci[j] / hp;
                            }
                            // Four effective samples of the known kernel-shaped
                            // analytical noise regularize nine empirical blocks.
                            // In whitened coordinates this is a flat noise prior.
                            nv = ensemble ? (nv + 4 * analytic) / 13 : analytic;
                            signal = std::max(0.0, signal / 9 - (ensemble ? 0.0 : nv));
                            const float gain = f == 0 ? 1 : float(signal / (signal + nv + 1e-20));
                            filtered[f] = ci[idx] * (1 - strength * gate * (1 - gain));
                            if (f) {
                                stats[ti].noise_variance += nv * hp * scales[k] * scales[k];
                                stats[ti].signal_power += signal * hp * scales[k] * scales[k];
                                ++stats[ti].samples;
                            }
                        }
                        // L2-only nonlocal grouping: 25 candidates, a shared
                        // sixteen-dimensional descriptor, Top-8 and WHT-8.
                        std::array<std::pair<float, std::size_t>, 25> match{};
                        int candidates = 0;
                        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                            const auto j = std::size_t(by + dy) * bw + bx + dx;
                            float distance = 0;
                            for (int f = 0; f < 16; ++f) {
                                const float d = ci[j * 48 + f] - ci[block_index * 48 + f];
                                const float hp = opt.kernel_power ? std::max(.02f, opt.kernel_power[f]) : 1;
                                distance += d * d / hp;
                            }
                            match[candidates++] = {distance, j};
                        }
                        std::partial_sort(match.begin(), match.begin() + 8, match.end());
                        if (match[7].first <= 64 * analytic && count >= 2) {
                            for (int f : {0, 1, 4, 5}) {
                                float group[8];
                                int central = -1;
                                for (int j = 0; j < 8; ++j) {
                                    group[j] = ci[match[j].second * 48 + k * 16 + f];
                                    if (match[j].second == block_index) central = j;
                                }
                                if (central < 0) continue;
                                hadamard(group, 8);
                                for (int j = 1; j < 8; ++j) {
                                    const float power = group[j] * group[j];
                                    const float hp = opt.kernel_power ? std::max(.02f, opt.kernel_power[f]) : 1;
                                    group[j] *= std::max(0.0f, 1 - analytic * hp / (power + 1e-20f));
                                }
                                hadamard(group, 8);
                                const float blend = .25f * strength * gate;
                                filtered[f] = (1 - blend) * filtered[f] + blend * group[central];
                            }
                        }
                        transform4(filtered);
                        std::copy_n(filtered, 16, channels[k]);
                    }
                    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                        const int ly = by * 4 + y - 8, lx = bx * 4 + x - 8;
                        if (ly >= th || lx >= tw) continue;
                        for (int k = 0; k < 3; ++k) {
                            float value = 0;
                            for (int j = 0; j < 3; ++j) value += basis[k * 3 + j] * channels[j][y * 4 + x];
                            value *= scales[k];
                            const float old = input[(std::size_t(by * 4 + y) * pw + bx * 4 + x) * 3 + k];
                            output[(std::size_t(ly) * tw + lx) * 3 + k] = value;
                            const double delta = value - old;
                            stats[ti].rms_update += delta * delta;
                            if (std::fabs(delta) > 1e-9) ++stats[ti].changed;
                        }
                    }
                    if (!ensemble) ++stats[ti].rejected;
                }
                for (int r = 0; r < th; ++r)
                    std::copy_n(output.data() + std::size_t(r) * tw * 3, std::size_t(tw) * 3,
                                rgb + (std::size_t(r0 + r) * w + c0) * 3);
                stats[ti].peak_scratch_bytes = (input.size() * 6 + neff.size() + output.size()) * sizeof(float);
            }
        });
    StageStats out; for (auto& s : stats) add_stats(out, s);
    out.rms_update = std::sqrt(out.rms_update / (double(h) * w * 3));
    out.noise_variance /= double(std::max<std::size_t>(1, out.samples));
    out.signal_power /= double(std::max<std::size_t>(1, out.samples));
    return out;
}
} // namespace detail

inline StageStats half_stack_denoise(float* rgb, const float* half_a, const float* half_b,
                                    int h, int w, Noise noise,
                                    const HalfStackOptions& options,
                                    const Execution& execution) {
    if ((half_a == nullptr) != (half_b == nullptr))
        throw std::invalid_argument("both independent half stacks required");
    return detail::denoise_tiles(rgb, h, w, noise, options, execution, half_a && half_b,
        [&](int y, int x, float* a, float* b, float& count) {
            const auto p = (std::size_t(y) * w + x) * 3;
            for (int k = 0; k < 3; ++k) {
                a[k] = half_a ? half_a[p + k] : rgb[p + k];
                b[k] = half_b ? half_b[p + k] : rgb[p + k];
            }
            count = std::max(1.0f, options.effective_frames);
        });
}

inline StageStats half_stack_denoise(const Frames& raw, int h, int w,
                                    const uint8_t* cfa, const Frames& fy,
                                    const Frames& fx, const Frames& mw, Noise noise,
                                    float* rgb, const HalfStackOptions& options,
                                    const Execution& execution) {
    detail::validate(raw, h, w, cfa, fy, fx, mw);
    if (!cfa) throw std::invalid_argument("CFA required");
    return detail::denoise_tiles(rgb, 2 * h, 2 * w, noise, options, execution, raw.size() >= 2,
        [&](int y, int x, float* a, float* b, float& count) {
            float sums[2][3] = {}, weights[2][3] = {}, total = 0, squared = 0;
            int contributed[2] = {};
            for (std::size_t n = 0; n < raw.size(); ++n) {
                const float weight = detail::clamp01(detail::field(mw, n, h / 2, w / 2, .25f * y, .25f * x, 1));
                const float yy = .5f * y + 2 * detail::field(fy, n, h / 2, w / 2, .25f * y, .25f * x);
                const float xx = .5f * x + 2 * detail::field(fx, n, h / 2, w / 2, .25f * y, .25f * x);
                if (weight <= .01f || !detail::inside(yy, xx, h, w)) continue;
                const int iy = int(std::floor(yy)), ix = int(std::floor(xx)), half = int(n % 2);
                float v[3] = {}, den[3] = {};
                for (int r = std::max(0, iy - 2); r <= std::min(h - 1, iy + 3); ++r)
                    for (int c = std::max(0, ix - 2); c <= std::min(w - 1, ix + 3); ++c) {
                        const int color = cfa[(r % 6) * 6 + c % 6];
                        const float k = std::max(0.0f, 3 - std::fabs(r - yy)) *
                                        std::max(0.0f, 3 - std::fabs(c - xx));
                        const float value = raw[n][std::size_t(r) * w + c];
                        if (std::isfinite(value) && noise.unsaturated(n, value)) { v[color] += k * value; den[color] += k; }
                    }
                bool valid = true;
                float precision = 0;
                for (int k = 0; k < 3; ++k) if (den[k] > 0) {
                    const float value = v[k] / den[k];
                    const float optimal = weight / noise.frame_variance(n, value);
                    sums[half][k] += optimal * value; weights[half][k] += optimal;
                    if (k == 1) precision = optimal;
                } else valid = false;
                if (valid) { total += precision; squared += precision * precision; ++contributed[half]; }
            }
            for (int k = 0; k < 3; ++k) {
                const float fallback = rgb[(std::size_t(y) * (2 * w) + x) * 3 + k];
                a[k] = weights[0][k] > 0 ? sums[0][k] / weights[0][k] : fallback;
                b[k] = weights[1][k] > 0 ? sums[1][k] / weights[1][k] : fallback;
            }
            count = squared > 0 ? total * total / squared : 1;
            if (contributed[0] < 2 || contributed[1] < 2) count = std::min(count, 4.0f);
            if (options.effective_frames > 0) count = std::min(count, options.effective_frames);
        });
}

inline std::array<float, 16> kernel_power_from_covariance(float xx, float xy,
                                                        float yy, float floor) {
    if (!std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yy) ||
        !std::isfinite(floor) || xx < 0 || yy < 0 ||
        double(xx) * yy < double(xy) * xy || floor < 0 || floor > 1)
        throw std::invalid_argument("finite positive-semidefinite covariance required");
    // WHT-4 natural-order rows: DC, Nyquist, quarter-cycle, quarter-cycle.
    // Real Walsh modes mix +/- Fourier frequencies. Average both signs,
    // including Nyquist, so an oblique covariance respects real symmetry.
    const double frequency[4] = {0, .5, .25, .25};
    constexpr double four_pi_squared = 39.478417604357434;
    std::array<float, 16> result{};
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) {
        double power = 0;
        for (int sy : {-1, 1}) for (int sx : {-1, 1}) {
            const double fx = sx * frequency[c], fy = sy * frequency[r];
            const double quadratic = std::max(0.0, xx * fx * fx + 2 * xy * fx * fy + yy * fy * fy);
            power += .25 * std::exp(-four_pi_squared * quadratic);
        }
        result[r * 4 + c] = std::max(floor, float(power));
    }
    result[0] = 1;
    return result;
}

inline std::array<float, 16> kernel_power_from_inverse_covariance(float xx, float xy,
                                                                float yy, float scale,
                                                                float floor) {
    const double determinant = double(xx) * yy - double(xy) * xy;
    if (!std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yy) ||
        !std::isfinite(scale) || xx <= 0 || yy <= 0 || determinant <= 0 || scale <= 0)
        throw std::invalid_argument("finite positive-definite precision required");
    const double factor = double(scale) * scale / determinant;
    return kernel_power_from_covariance(float(yy * factor), float(-xy * factor),
                                        float(xx * factor), floor);
}

namespace detail {
inline bool valid_psf(const SpatialPsfZone& z) {
    return z.valid && std::isfinite(z.cx) && std::isfinite(z.cy) &&
           std::isfinite(z.sigma_major_2x) && std::isfinite(z.sigma_minor_2x) &&
           std::isfinite(z.angle) && z.cx >= 0 && z.cx <= 1 && z.cy >= 0 && z.cy <= 1 &&
           z.sigma_major_2x > 0 && z.sigma_minor_2x > 0;
}
inline std::array<double, 3> psf_covariance(const std::vector<SpatialPsfZone>& zones,
                                          double y, double x) {
    std::array<double, 3> result{};
    double sum = 0;
    for (const auto& z : zones) {
        const double cs = std::cos(z.angle), sn = std::sin(z.angle);
        const double major = double(z.sigma_major_2x) * z.sigma_major_2x;
        const double minor = double(z.sigma_minor_2x) * z.sigma_minor_2x;
        const std::array<double, 3> cov = {cs * cs * major + sn * sn * minor,
                                         cs * sn * (major - minor),
                                         sn * sn * major + cs * cs * minor};
        const double distance = (x - z.cx) * (x - z.cx) + (y - z.cy) * (y - z.cy);
        if (distance < 1e-16) return cov;
        const double weight = 1 / distance;
        for (int k = 0; k < 3; ++k) result[k] += weight * cov[k];
        sum += weight;
    }
    for (double& value : result) value /= sum;
    return result;
}
// Gaussian kernels on a globally anchored 16-pixel grid; interpolating
// normalized kernels gives continuous weights independent of tile partition.
struct PsfGrid {
    int y0, x0, rows, cols, radius, taps;
    std::vector<float> kernels;
    PsfGrid(int top, int left, int bottom, int right, int h, int w, int rad,
            const std::vector<SpatialPsfZone>& zones)
        : y0((top / 16) * 16), x0((left / 16) * 16),
          rows((bottom - 1 - y0) / 16 + 2), cols((right - 1 - x0) / 16 + 2),
          radius(rad), taps((2 * rad + 1) * (2 * rad + 1)),
          kernels(std::size_t(rows) * cols * taps) {
        for (int r = 0; r < rows; ++r) for (int c = 0; c < cols; ++c) {
            const double y = std::min(1.0, double(y0 + r * 16) / std::max(1, h - 1));
            const double x = std::min(1.0, double(x0 + c * 16) / std::max(1, w - 1));
            const auto cov = psf_covariance(zones, y, x);
            // A tiny eigenvalue floor represents subpixel delta-like axes.
            const double xx = cov[0] + 1e-8, xy = cov[1], yy = cov[2] + 1e-8;
            const double determinant = std::max(1e-16, xx * yy - xy * xy);
            const std::size_t base = (std::size_t(r) * cols + c) * taps;
            double sum = 0; int t = 0;
            for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
                const double exponent = -.5 * (yy * dx * dx - 2 * xy * dx * dy + xx * dy * dy) / determinant;
                const float value = float(std::exp(std::min(0.0, exponent)));
                kernels[base + t++] = value; sum += value;
            }
            for (int k = 0; k < taps; ++k) kernels[base + k] /= float(sum);
        }
    }
    struct View {
        const float* p[4];
        float weight[4];
        float operator()(int k) const {
            return weight[0] * p[0][k] + weight[1] * p[1][k] +
                   weight[2] * p[2][k] + weight[3] * p[3][k];
        }
    };
    View at(int y, int x) const {
        const int r = (y - y0) / 16, c = (x - x0) / 16;
        const float ty = float(y - y0 - r * 16) / 16, tx = float(x - x0 - c * 16) / 16;
        const std::size_t base = (std::size_t(r) * cols + c) * taps;
        return {{kernels.data() + base, kernels.data() + base + taps,
                 kernels.data() + base + std::size_t(cols) * taps,
                 kernels.data() + base + std::size_t(cols + 1) * taps},
                {(1 - ty) * (1 - tx), (1 - ty) * tx, ty * (1 - tx), ty * tx}};
    }
};
} // namespace detail

inline StageStats spatial_psf_deconvolve(float* rgb, int h, int w,
                                        const std::vector<SpatialPsfZone>& input_zones,
                                        Noise noise, int iterations, float strength,
                                        const Execution& execution) {
    detail::shape(h, w);
    if (!rgb) throw std::invalid_argument("RGB required");
    if (!std::isfinite(strength) || !std::isfinite(noise.read_variance) ||
        !std::isfinite(noise.shot) || noise.read_variance < 0 || noise.shot < 0)
        throw std::invalid_argument("finite nonnegative noise and strength required");
    StageStats result;
    std::vector<SpatialPsfZone> zones;
    float max_sigma = 0;
    for (const auto& zone : input_zones) {
        if (detail::valid_psf(zone)) {
            zones.push_back(zone);
            max_sigma = std::max(max_sigma, std::max(zone.sigma_major_2x, zone.sigma_minor_2x));
        } else ++result.rejected;
    }
    iterations = std::max(0, std::min(2, iterations));
    strength = detail::clamp01(strength);
    if (zones.empty() || !iterations || strength == 0) return result;
    const int radius = std::max(1, int(std::ceil(std::min(12.0f, 3 * max_sigma))));
    const int halo = (2 * iterations + 1) * radius;
    const int tile = std::max(16, std::min(256, execution.tile));
    const int nr = (h + tile - 1) / tile, nc = (w + tile - 1) / tile;
    const int strip_rows = std::min(h, tile + 2 * halo);
    std::vector<float> strip(std::size_t(strip_rows) * w * 3);
    std::vector<StageStats> stats(std::size_t(nr) * nc);
    int previous_top = 0, previous_bottom = 0;
    for (int tr = 0; tr < nr; ++tr) {
        const int core_top = tr * tile, core_bottom = std::min(h, core_top + tile);
        const int strip_top = std::max(0, core_top - halo);
        const int strip_bottom = std::min(h, core_bottom + halo);
        // Preserve original rows needed by the next stripe before output writes.
        const int overlap = std::max(0, previous_bottom - strip_top);
        if (overlap)
            std::memmove(strip.data(), strip.data() + std::size_t(strip_top - previous_top) * w * 3,
                         std::size_t(overlap) * w * 3 * sizeof(float));
        for (int y = strip_top + overlap; y < strip_bottom; ++y)
            std::copy_n(rgb + std::size_t(y) * w * 3, std::size_t(w) * 3,
                        strip.data() + std::size_t(y - strip_top) * w * 3);
        previous_top = strip_top; previous_bottom = strip_bottom;
        detail::run(nc, execution, [&](int first, int last) {
            for (int tc = first; tc < last; ++tc) {
                auto& stat = stats[std::size_t(tr) * nc + tc];
                const int core_left = tc * tile, core_right = std::min(w, core_left + tile);
                const int left = std::max(0, core_left - halo), right = std::min(w, core_right + halo);
                const int top = strip_top, bottom = strip_bottom, lh = bottom - top, lw = right - left;
                const std::size_t pixels = std::size_t(lh) * lw;
                std::vector<float> original(pixels * 3), current(pixels * 3),
                                   gradient(pixels * 3), mass(pixels), norm(pixels);
                for (int y = top; y < bottom; ++y)
                    std::copy_n(strip.data() + (std::size_t(y - top) * w + left) * 3, std::size_t(lw) * 3,
                                original.data() + std::size_t(y - top) * lw * 3);
                current = original;
                const detail::PsfGrid grid(top, left, bottom, right, h, w, radius, zones);
                auto local = [&](int y, int x) { return std::size_t(y - top) * lw + x - left; };
                for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
                    const auto kernel = grid.at(y, x);
                    float sum = 0; int t = 0;
                    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx, ++t)
                        if (y + dy >= 0 && y + dy < h && x + dx >= 0 && x + dx < w) sum += kernel(t);
                    norm[local(y, x)] = 1 / std::max(1e-20f, sum);
                }
                auto project = [&](int y, int x, const detail::PsfGrid::View& kernel, float* predicted) {
                    predicted[0] = predicted[1] = predicted[2] = 0;
                    int t = 0;
                    for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx, ++t) {
                        const int yy = y + dy, xx = x + dx;
                        if (yy < 0 || yy >= h || xx < 0 || xx >= w) continue;
                        const float weight = kernel(t) * norm[local(y, x)];
                        const auto p = local(yy, xx) * 3;
                        for (int k = 0; k < 3; ++k) predicted[k] += weight * current[p + k];
                    }
                };
                for (int y = core_top; y < core_bottom; ++y) for (int x = core_left; x < core_right; ++x) {
                    float prediction[3]; project(y, x, grid.at(y, x), prediction);
                    for (int k = 0; k < 3; ++k) {
                        const double residual = original[local(y, x) * 3 + k] - prediction[k];
                        stat.before += residual * residual;
                    }
                }
                for (int iteration = 0; iteration < iterations; ++iteration) {
                    std::fill(gradient.begin(), gradient.end(), 0);
                    std::fill(mass.begin(), mass.end(), 0);
                    const int shrink = (2 * iteration + 1) * radius;
                    const int ft = top == 0 ? 0 : top + shrink, fb = bottom == h ? h : bottom - shrink;
                    const int fl = left == 0 ? 0 : left + shrink, fr = right == w ? w : right - shrink;
                    const int ut = top == 0 ? 0 : ft + radius, ub = bottom == h ? h : fb - radius;
                    const int ul = left == 0 ? 0 : fl + radius, ur = right == w ? w : fr - radius;
                    for (int y = ft; y < fb; ++y) for (int x = fl; x < fr; ++x) {
                        const auto kernel = grid.at(y, x);
                        float prediction[3]; project(y, x, kernel, prediction);
                        const auto source = local(y, x);
                        float residual[3];
                        for (int k = 0; k < 3; ++k) residual[k] = original[source * 3 + k] - prediction[k];
                        int t = 0;
                        for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx, ++t) {
                            const int yy = y + dy, xx = x + dx;
                            if (yy < ut || yy >= ub || xx < ul || xx >= ur) continue;
                            const auto target = local(yy, xx);
                            const float weight = kernel(t) * norm[source];
                            for (int k = 0; k < 3; ++k) gradient[target * 3 + k] += weight * residual[k];
                            mass[target] += weight;
                        }
                    }
                    for (int y = ut; y < ub; ++y) for (int x = ul; x < ur; ++x) {
                        const auto p = local(y, x);
                        const int ym = std::max(top, y - 1), yp = std::min(bottom - 1, y + 1);
                        const int xm = std::max(left, x - 1), xp = std::min(right - 1, x + 1);
                        float energy = 0, variance = 0;
                        for (int k = 0; k < 3; ++k) {
                            const float gx = .5f * (original[local(y, xp) * 3 + k] - original[local(y, xm) * 3 + k]);
                            const float gy = .5f * (original[local(yp, x) * 3 + k] - original[local(ym, x) * 3 + k]);
                            energy += (gx * gx + gy * gy) / 3;
                            variance += noise.variance(original[p * 3 + k]) / 3;
                        }
                        const float gate = std::max(0.0f, energy - 2 * variance) / (energy + 8 * variance);
                        const float limit = .5f * std::sqrt(energy) + 3 * std::sqrt(variance);
                        for (int k = 0; k < 3; ++k) {
                            const float update = strength * gate * gradient[p * 3 + k] / std::max(1e-20f, mass[p]);
                            current[p * 3 + k] += std::max(-limit, std::min(limit, update));
                        }
                    }
                }
                for (int y = core_top; y < core_bottom; ++y) for (int x = core_left; x < core_right; ++x) {
                    float prediction[3]; project(y, x, grid.at(y, x), prediction);
                    const auto p = local(y, x);
                    for (int k = 0; k < 3; ++k) {
                        const double residual = original[p * 3 + k] - prediction[k];
                        stat.after += residual * residual;
                        const double update = current[p * 3 + k] - original[p * 3 + k];
                        stat.rms_update += update * update;
                        if (update != 0) ++stat.changed;
                        ++stat.samples;
                        rgb[(std::size_t(y) * w + x) * 3 + k] = current[p * 3 + k];
                    }
                }
                stat.peak_scratch_bytes = (pixels * 11 + grid.kernels.size()) * sizeof(float);
            }
        });
    }
    for (const auto& s : stats) detail::add_stats(result, s);
    result.rms_update = std::sqrt(result.rms_update / std::max<std::size_t>(1, result.samples));
    // Shared frozen strip plus the largest per-worker tile allocation.
    result.peak_scratch_bytes += strip.size() * sizeof(float);
    result.iterations = iterations;
    return result;
}

} // namespace heraw_core
