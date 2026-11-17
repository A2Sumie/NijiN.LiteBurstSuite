#include "../heraw_suite_core.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <thread>
#include <type_traits>

#if defined(_WIN32) && defined(HERAW_CORE_CRASH_TRACE)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static LONG WINAPI core_crash_trace(EXCEPTION_POINTERS* fault) {
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "Windows exception=%08lx address=%p access=%llu target=%llx image=%llx\n",
                 fault->ExceptionRecord->ExceptionCode, fault->ExceptionRecord->ExceptionAddress,
                 static_cast<unsigned long long>(fault->ExceptionRecord->ExceptionInformation[0]),
                 static_cast<unsigned long long>(fault->ExceptionRecord->ExceptionInformation[1]),
                 static_cast<unsigned long long>(base));
    CONTEXT context = *fault->ContextRecord;
    for (int depth = 0; depth < 24 && context.Rip; ++depth) {
        std::fprintf(stderr, "  pc=%llx rva=%llx sp=%llx\n",
                     static_cast<unsigned long long>(context.Rip),
                     static_cast<unsigned long long>(context.Rip - base),
                     static_cast<unsigned long long>(context.Rsp));
        DWORD64 image = 0, frame = 0;
        void* handler_data = nullptr;
        const auto entry = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
        if (entry) RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, entry,
                                   &context, &handler_data, &frame, nullptr);
        else {
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += sizeof(DWORD64);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

using namespace heraw_core;

// MinGW AVX2 formerly emitted a 32-byte aligned store to a 16-byte-aligned
// outgoing Noise value temporary. Borrow the model across the shared API.
using SiliconCleanupSignature = StageStats (*)(
    Frames&, int, int, const uint8_t*, const Frames&, const Frames&,
    const Frames&, const Noise&, const float*, const Execution&);
static_assert(std::is_same<decltype(&silicon_residual_cleanup), SiliconCleanupSignature>::value,
              "shared noise model must be passed by reference");

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static std::array<uint8_t, 36> bayer() {
    std::array<uint8_t, 36> cfa{};
    for (int r = 0; r < 6; ++r) for (int c = 0; c < 6; ++c)
        cfa[r * 6 + c] = uint8_t((r % 2 == 0) ? ((c % 2 == 0) ? 0 : 1) : ((c % 2 == 0) ? 1 : 2));
    return cfa;
}
static Execution parallel_execution() {
    Execution e; e.tile = 32;
    e.parallel = [](int count, const RangeTask& task) {
        const int middle = count / 2;
        std::thread other([&] { task(0, middle); });
        task(middle, count); other.join();
    };
    return e;
}
static double mse(const std::vector<float>& a, const std::vector<float>& b) {
    require(a.size() == b.size(), "MSE shape");
    double sum = 0;
    for (std::size_t i = 0; i < a.size(); ++i) sum += double(a[i] - b[i]) * (a[i] - b[i]);
    return sum / double(a.size());
}
// Independent aperture renderer, deliberately separate from the core operator.
static float render(const std::vector<float>& rgb, int h, int w, float y, float x, int channel) {
    double sum = 0, mass = 0;
    for (int r = std::max(0, int(std::floor(2 * y)) - 2); r <= std::min(h - 1, int(std::ceil(2 * y)) + 2); ++r)
        for (int c = std::max(0, int(std::floor(2 * x)) - 2); c <= std::min(w - 1, int(std::ceil(2 * x)) + 2); ++c) {
            const double weight = std::max(0.0, 1 - std::fabs(r - 2.0 * y) / 2) *
                                  std::max(0.0, 1 - std::fabs(c - 2.0 * x) / 2);
            sum += weight * rgb[(std::size_t(r) * w + c) * 3 + channel]; mass += weight;
        }
    return mass > 0 ? float(sum / mass) : 0;
}
static void exposure_test() {
    constexpr int h = 32, w = 40;
    Frames raw(4, std::vector<float>(h * w)), green(4, std::vector<float>(h * w / 4));
    const float ratios[4] = {1, .25f, 2, .5f};
    for (int n = 0; n < 4; ++n) {
        for (std::size_t i = 0; i < raw[n].size(); ++i) raw[n][i] = ratios[n] * (i % 5 ? .12f : -.007f);
        for (std::size_t i = 0; i < green[n].size(); ++i) green[n][i] = ratios[n] * (.08f + .0001f * float(i));
        for (int i = 0; i < 9; ++i) green[n][i] = .95f;
    }
    auto stats = exposure_normalize(raw, green, h, w, {1e-8f, .001f}, {}, {}, {}, parallel_execution());
    for (int n = 0; n < 4; ++n) {
        require(std::fabs(stats.gains[n] * ratios[n] - 1) < 2e-5, "known exposure ratio");
        require(std::fabs(raw[n][0] + .007f) < 1e-6f, "exposure retains signed RAW");
        require(std::fabs(stats.variance_scales[n] - stats.gains[n] * stats.gains[n]) < 1e-5,
                "exposure variance scaling");
        require(std::fabs(stats.read_variances[n] - 1e-8f / (ratios[n] * ratios[n])) < 1e-12f,
                "read variance scales quadratically");
        require(std::fabs(stats.shot_coefficients[n] - .001f / ratios[n]) < 1e-8f,
                "Poisson coefficient scales linearly");
    }
    std::printf("exposure gains: %.6f %.6f %.6f %.6f; samples=%zu\n",
                stats.gains[0], stats.gains[1], stats.gains[2], stats.gains[3], stats.samples);
    bool threw = false;
    try { exposure_normalize(raw, green, h - 1, w); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "reject odd RAW dimensions");
}
static void row_and_lucky_test() {
    constexpr int h = 32, w = 48;
    Frames fy(2, std::vector<float>(h * w)), fx = fy, mw(2, std::vector<float>(h * w, 1));
    for (int n = 0; n < 2; ++n) for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
        fy[n][r * w + c] = .1f * r + ((r & 1) ? .2f : -.2f);
        fx[n][r * w + c] = .03f * r + .001f * c;
    }
    const auto fx_before = fx;
    auto stats = row_rolling_shutter_smooth(fy, fx, mw, h, w, 1, parallel_execution());
    require(std::fabs(fy[0][16 * w] - 1.6f) < 1e-5f, "row spline rejects alternating jitter");
    require(mse(fx[0], fx_before[0]) < 1e-12, "row spline preserves affine shear and local flow");
    require(stats.rms_update > .05, "row stats measured");
    Frames g(2, std::vector<float>(h * w));
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
        g[0][r * w + c] = .3f + .1f * std::sin(.7f * c) + .01f * std::cos(.4f * r);
        g[1][r * w + c] = .3f + .02f * std::sin(.7f * c) + .01f * std::cos(.4f * r);
    }
    Frames weights;
    directional_lucky_weights(g, h, w, {}, {}, {1e-8f, 0}, weights, parallel_execution());
    const int index = (16 * w + 23) * 4;
    require(weights[0][index] > weights[1][index] + .3f, "lucky weighting selects directional sharpness");
    require(std::fabs(weights[0][index + 2] - weights[1][index + 2]) < .01,
            "orthogonal direction retained");
    std::vector<uint8_t> directions(h*w);
    for(size_t i=0;i<directions.size();++i) directions[i]=uint8_t(i%4);
    Frames selected;
    directional_lucky_weights(g,h,w,{},{},{1e-8f,0},selected,parallel_execution(),directions.data());
    for(size_t n=0;n<g.size();++n) for(size_t i=0;i<directions.size();++i)
        require(std::fabs(selected[n][i]-weights[n][4*i+directions[i]])<1e-6f,
                "selected direction equals the corresponding full directional field");
}
static void silicon_test() {
    constexpr int h = 64, w = 80, nframes = 6;
    const auto cfa = bayer();
    std::vector<float> scene(h * 2 * w * 2 * 3);
    for (int r = 0; r < 2 * h; ++r) for (int c = 0; c < 2 * w; ++c)
        for (int k = 0; k < 3; ++k) scene[(r * 2 * w + c) * 3 + k] = .055f + .007f * k + .00003f * r + .00002f * c;
    Frames raw(nframes, std::vector<float>(h * w)), clean = raw;
    Frames fy(nframes, std::vector<float>(h * w / 4)), fx = fy, mw(nframes, std::vector<float>(h * w / 4, 1));
    for (int n = 0; n < nframes; ++n) {
        const float dy = n ? (n - 3) * 2.3f : 0, dx = n ? (n - 2) * 2.1f : 0;
        std::fill(fy[n].begin(), fy[n].end(), dy / 2);
        std::fill(fx[n].begin(), fx[n].end(), dx / 2);
        for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
            const float ry = std::max(0.0f, std::min(float(h - 1), r - dy));
            const float rx = std::max(0.0f, std::min(float(w - 1), c - dx));
            const float value = render(scene, 2 * h, 2 * w, ry, rx, cfa[(r % 6) * 6 + c % 6]);
            clean[n][r * w + c] = value;
            raw[n][r * w + c] = value + .003f * std::sin(1.7f * r) + .002f * std::cos(1.3f * c);
            if (r == 30 && c == 38) raw[n][r * w + c] += .025f;
        }
    }
    const auto before_raw = raw;
    double before = 0; for (int n = 0; n < nframes; ++n) before += mse(raw[n], clean[n]);
    const auto stats = silicon_residual_cleanup(raw, h, w, cfa.data(), fy, fx, mw, {1e-6f, 0}, scene.data(), parallel_execution());
    double after = 0; for (int n = 0; n < nframes; ++n) after += mse(raw[n], clean[n]);
    std::printf("silicon MSE %.9g -> %.9g; dark residual %.9g -> %.9g\n", before, after, stats.before, stats.after);
    require(after < before * .15, "sensor row/column offset cleanup preserves scene");
    require(std::fabs(raw[0][30 * w + 38] - clean[0][30 * w + 38]) < .002f, "persistent hot consensus");
    Frames identity = before_raw, zero = fy;
    for (auto& plane : zero) std::fill(plane.begin(), plane.end(), 0);
    const auto skip = silicon_residual_cleanup(identity, h, w, cfa.data(), zero, zero, mw, {1e-6f, 0});
    require(skip.changed == 0 && identity == before_raw, "dither observability gate");
    auto estimated = before_raw;
    const auto internal = silicon_residual_cleanup(estimated, h, w, cfa.data(), fy, fx, mw, {1e-6f, 0});
    double estimated_error = 0;
    for (int n = 0; n < nframes; ++n) estimated_error += mse(estimated[n], clean[n]);
    std::printf("silicon inferred scene MSE %.9g; samples=%zu\n", estimated_error, internal.samples);
    require(estimated_error < before * .65, "inferred static scene separates sensor error");
}
static void ibp_test() {
    constexpr int h = 32, w = 40, nframes = 6;
    const auto cfa = bayer();
    std::vector<float> truth(h * w * 4 * 3), estimate(truth.size());
    for (int r = 0; r < 2 * h; ++r) for (int c = 0; c < 2 * w; ++c)
        for (int k = 0; k < 3; ++k) {
            const auto p = (r * 2 * w + c) * 3 + k;
            truth[p] = .15f + .04f * k + .08f * std::sin(.31f * r) * std::cos(.23f * c);
            estimate[p] = truth[p] + .035f * std::cos(.17f * r + .21f * c + k);
        }
    Frames raw(nframes, std::vector<float>(h * w));
    Frames fy(nframes, std::vector<float>(h * w / 4)), fx = fy;
    Frames mw(nframes, std::vector<float>(h * w / 4, 1));
    for (int n = 0; n < nframes; ++n) {
        const float dy = .21f * n, dx = -.17f * n;
        std::fill(fy[n].begin(), fy[n].end(), dy / 2);
        std::fill(fx[n].begin(), fx[n].end(), dx / 2);
        for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c)
            raw[n][r * w + c] = render(truth, 2 * h, 2 * w, r - dy, c - dx, cfa[(r % 6) * 6 + c % 6]);
    }
    auto sequential = estimate;
    const auto stats = compact_ibp(raw, h, w, cfa.data(), fy, fx, mw, {1e-5f, .001f}, estimate.data(), 2, parallel_execution());
    Execution serial; serial.tile = 32;
    compact_ibp(raw, h, w, cfa.data(), fy, fx, mw, {1e-5f, .001f}, sequential.data(), 2, serial);
    std::printf("IBP forward residual %.9g -> %.9g; scratch=%zu bytes\n", stats.before, stats.after, stats.peak_scratch_bytes);
    require(stats.after < stats.before * .2, "IBP reduces independent sensor forward residual");
    require(mse(estimate, sequential) == 0, "parallel IBP race-free deterministic result");
    require(stats.peak_scratch_bytes <= 32 * 32 * 3 * 8, "IBP tile memory bound");
    for (int n = 0; n < nframes; ++n) for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c)
        raw[n][r * w + c] = -.04f + .07f * cfa[(r % 6) * 6 + c % 6];
    for (std::size_t i = 0; i < estimate.size(); ++i) estimate[i] = -.04f + .07f * float(i % 3);
    const auto constants = estimate;
    compact_ibp(raw, h, w, cfa.data(), fy, fx, mw, {1e-5f, .001f}, estimate.data(), 2);
    color_line_refine(estimate.data(), 2 * h, 2 * w, {1e-5f, .001f}, .8f, parallel_execution());
    require(mse(estimate, constants) < 1e-15, "signed constant color preserved through IBP and color-line");
    const auto no_step = compact_ibp(raw, h, w, cfa.data(), fy, fx, mw, {}, estimate.data(), 0);
    require(no_step.iterations == 0, "IBP zero iteration boundary");
    // Check the transpose numerically on a clipped, fractional sensor footprint.
    double lhs = 0, rhs = 0;
    std::vector<double> transpose(truth.size());
    for (int j = 0; j < 19; ++j) {
        const float y = .17f * j, x = .09f * j;
        const float residual = std::sin(float(j));
        const auto f = detail::footprint(y, x, 2 * h, 2 * w, j % 3);
        lhs += render(truth, 2 * h, 2 * w, y, x, j % 3) * residual;
        for (int k = 0; k < f.count; ++k) transpose[f.index[k]] += f.weight[k] * residual;
    }
    for (std::size_t i = 0; i < truth.size(); ++i) rhs += truth[i] * transpose[i];
    require(std::fabs(lhs - rhs) < 1e-6, "matched adjoint boundary inner product");
}
static void half_stack_test() {
    constexpr int h = 96, w = 96;
    std::mt19937 rng(78421);
    std::normal_distribution<float> gaussian(0, .01f);
    std::vector<float> truth(h * w * 3), a(truth.size()), b(a.size()), rgb(a.size());
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) for (int k = 0; k < 3; ++k) {
        const auto p = (r * w + c) * 3 + k;
        truth[p] = -.02f + .08f * k + .04f * std::sin(.29f * c) + .03f * std::cos(.22f * r);
        a[p] = truth[p] + gaussian(rng); b[p] = truth[p] + gaussian(rng);
        rgb[p] = .5f * (a[p] + b[p]);
    }
    HalfStackOptions opt; opt.effective_frames = 8; opt.phase_balanced = true;
    const double before = mse(rgb, truth);
    const auto original = rgb;
    const auto stats = half_stack_denoise(rgb.data(), a.data(), b.data(), h, w, {.0004f, 0}, opt, parallel_execution());
    const double after = mse(rgb, truth);
    std::printf("half-stack textured MSE %.9g -> %.9g; measured noise=%.9g; scratch=%zu\n",
                before, after, stats.noise_variance, stats.peak_scratch_bytes);
    require(after < before * .8, "independent half-stack removes noise while retaining texture");
    require(stats.noise_variance > .000025 && stats.noise_variance < .00009, "half-difference noise independently calibrated");
    require(stats.rejected == 0, "balanced overdetermined ensemble enabled");
    require(stats.peak_scratch_bytes < 600000, "half-stack scratch independent of image size");
    double dot = 0, energy = 0;
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
        const float target = .04f * std::sin(.29f * c) + .03f * std::cos(.22f * r);
        dot += (rgb[(r * w + c) * 3 + 1] - .06f) * target; energy += target * target;
    }
    require(dot / energy > .94, "cross-spectral common texture amplitude retained");
    auto fallback = original; opt.phase_balanced = false;
    auto fallback_stats = half_stack_denoise(fallback.data(), a.data(), b.data(), h, w, {.0004f, 0}, opt);
    require(fallback_stats.rejected > 0 && mse(fallback, original) > 0, "phase imbalance uses analytical fallback");
    opt.strength = 0; auto bypass = original;
    half_stack_denoise(bypass.data(), a.data(), b.data(), h, w, {.0004f, 0}, opt);
    require(mse(bypass, original) < 1e-14, "zero strength conserves image");
    for (std::size_t i = 0; i < rgb.size(); ++i) rgb[i] = a[i] = b[i] = -.03f + .08f * float(i % 3);
    const auto constant = rgb; opt.strength = 1; opt.phase_balanced = true;
    half_stack_denoise(rgb.data(), a.data(), b.data(), h, w, {.0004f, 0}, opt);
    require(mse(rgb, constant) < 1e-14, "KLT/Hadamard constant signed color preserved");
    // Tiny partial blocks, missing halves, and X-Trans are real public boundaries.
    const uint8_t xtrans[36] = {1,0,1,1,0,1, 2,1,2,0,1,0, 1,0,1,1,0,1,
                               1,2,1,1,2,1, 0,1,0,2,1,2, 1,2,1,1,2,1};
    Frames raw(6, std::vector<float>(12 * 14));
    for (auto& frame : raw) for (int r = 0; r < 12; ++r) for (int c = 0; c < 14; ++c)
        frame[r * 14 + c] = -.03f + .08f * xtrans[(r % 6) * 6 + c % 6];
    std::vector<float> small(24 * 28 * 3);
    for (std::size_t i = 0; i < small.size(); ++i) small[i] = -.03f + .08f * float(i % 3);
    const auto small_original = small;
    half_stack_denoise(raw, 12, 14, xtrans, {}, {}, {}, {.0004f, 0}, small.data(), opt);
    require(mse(small, small_original) < 1e-13, "RAW half-stack X-Trans and partial tiles constant color");
    half_stack_denoise(small.data(), nullptr, nullptr, 24, 28, {.0004f, 0}, opt);
    bool threw = false;
    try { half_stack_denoise(small.data(), small.data(), nullptr, 24, 28, {}); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "incomplete half pair rejected");
}
static void exposure_weighting_test() {
    const auto cfa = bayer();
    Frames raw(2, std::vector<float>(16 * 16, .2f));
    std::fill(raw[1].begin(), raw[1].end(), .5f);
    std::vector<float> image(32 * 32 * 3, .1f), uniform = image;
    float read[2] = {1e-5f, .01f}, clip[2] = {.98f, .4f};
    Noise model{1e-5f, 0};
    compact_ibp(raw, 16, 16, cfa.data(), {}, {}, {}, model, uniform.data(), 2);
    model.frame_read_variances = read;
    compact_ibp(raw, 16, 16, cfa.data(), {}, {}, {}, model, image.data(), 2);
    require(image[16 * 32 * 3 + 18 * 3 + 1] < uniform[16 * 32 * 3 + 18 * 3 + 1] - .1f,
            "heteroscedastic frame precision changes IBP causally");
    model.frame_saturation_levels = clip;
    std::fill(image.begin(), image.end(), .2f);
    const auto same = image;
    const auto stats = compact_ibp(raw, 16, 16, cfa.data(), {}, {}, {}, model, image.data(), 2);
    require(mse(image, same) < 1e-14 && stats.samples == 16 * 16,
            "normalized saturation excludes clipped source");
    Frames zero(2, std::vector<float>(8 * 8, 0));
    const auto gated = compact_ibp(raw, 16, 16, cfa.data(), {}, {}, zero, model, image.data(), 2);
    require(gated.samples == 0 && gated.changed == 0, "zero motion weights preserve image");
}
static std::vector<float> elliptical_blur(const std::vector<float>& src, int h, int w,
                                        float major, float minor, float angle) {
    const int radius = int(std::ceil(3 * std::max(major, minor)));
    const double cs = std::cos(angle), sn = std::sin(angle);
    std::vector<float> result(src.size());
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
        double sum[3] = {}, mass = 0;
        for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
            if (r + dy < 0 || r + dy >= h || c + dx < 0 || c + dx >= w) continue;
            const double u = (cs * dx + sn * dy) / major, v = (-sn * dx + cs * dy) / minor;
            const double weight = std::exp(-.5 * (u * u + v * v));
            for (int k = 0; k < 3; ++k) sum[k] += weight * src[((r + dy) * w + c + dx) * 3 + k];
            mass += weight;
        }
        for (int k = 0; k < 3; ++k) result[(r * w + c) * 3 + k] = float(sum[k] / mass);
    }
    return result;
}
static void spatial_psf_test() {
    constexpr int h = 56, w = 72;
    std::vector<float> truth(h * w * 3);
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) for (int k = 0; k < 3; ++k) {
        const float edge = (c > 20 + r / 3) ? .5f : -.04f;
        truth[(r * w + c) * 3 + k] = edge + .04f * k + .015f * std::sin(.4f * r);
    }
    const auto observed = elliptical_blur(truth, h, w, 1.1f, .65f, .35f);
    const auto initial_forward = elliptical_blur(observed, h, w, 1.1f, .65f, .35f);
    std::vector<SpatialPsfZone> zones = {
        {.5f, .5f, 1.1f, .65f, .35f, true},
        {0, 0, 1.1f, .65f, .35f, true}, {1, 0, 1.1f, .65f, .35f, true},
        {0, 1, 1.1f, .65f, .35f, true}, {1, 1, 1.1f, .65f, .35f, true}
    };
    auto result = observed;
    Execution tiny; tiny.tile = 16;
    const auto stats = spatial_psf_deconvolve(result.data(), h, w, zones, {1e-7f, 0}, 2, .8f, tiny);
    const auto final_forward = elliptical_blur(result, h, w, 1.1f, .65f, .35f);
    const double before = mse(initial_forward, observed), after = mse(final_forward, observed);
    std::printf("PSF forward MSE %.9g -> %.9g; latent edge MSE %.9g -> %.9g; scratch=%zu\n",
                before, after, mse(observed, truth), mse(result, truth), stats.peak_scratch_bytes);
    require(after < before * .8, "measured elliptical deconvolution reduces independently rendered residual");
    require(mse(result, truth) < mse(observed, truth), "deconvolution restores known edge");
    require(std::fabs(stats.after / double(result.size()) - after) < 2e-9,
            "deconvolution stats measure actual final forward residual");
    auto serial = observed, parallel = observed;
    zones[1].sigma_major_2x = 1.65f; zones[1].angle = -.4f;
    zones[2].sigma_minor_2x = .85f; zones[3].angle = 1.2f;
    zones[4].sigma_major_2x = .9f;
    spatial_psf_deconvolve(serial.data(), h, w, zones, {1e-7f, 0}, 2, .8f, tiny);
    spatial_psf_deconvolve(parallel.data(), h, w, zones, {1e-7f, 0}, 2, .8f, parallel_execution());
    double max_delta = 0, seam_delta = 0;
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) for (int k = 0; k < 3; ++k) {
        const auto i = (r * w + c) * 3 + k;
        max_delta = std::max(max_delta, double(std::fabs(serial[i] - parallel[i])));
        if (r % 16 == 0 || c % 16 == 0)
            seam_delta = std::max(seam_delta, double(std::fabs(serial[i] - parallel[i])));
        require(std::isfinite(serial[i]), "spatial PSF output finite");
    }
    std::printf("PSF varying-zone tile16/tile32 max delta=%.9g seam=%.9g\n", max_delta, seam_delta);
    require(max_delta < 1e-6 && seam_delta < 1e-6, "spatial PSF independent of tile partition and scheduling");
    require(mse(serial, result) > 1e-9, "measured field variation changes deconvolution causally");
    auto noisy = observed;
    const auto suppressed = spatial_psf_deconvolve(noisy.data(), h, w, zones, {.05f, 0}, 2, .8f, tiny);
    require(suppressed.changed == 0 && noisy == observed, "noise floor gates unsupported deconvolution");
    for (std::size_t i = 0; i < result.size(); ++i) result[i] = -.03f + .08f * float(i % 3);
    const auto constant = result;
    spatial_psf_deconvolve(result.data(), h, w, zones, {1e-7f, 0}, 2, 1, tiny);
    require(result == constant, "spatial varying PSF preserves signed constant color exactly");
    for (auto& zone : zones) zone.valid = false;
    zones[0].valid = true; zones[0].sigma_major_2x = std::numeric_limits<float>::quiet_NaN();
    const auto invalid = spatial_psf_deconvolve(result.data(), h, w, zones, {}, 2);
    require(invalid.changed == 0 && invalid.rejected == zones.size() && result == constant,
            "invalid measured zones bypass without a fabricated PSF");
    std::vector<float> pixel = {-.1f, .2f, .6f};
    zones = {{.5f, .5f, 2, 1, 0, true}};
    spatial_psf_deconvolve(pixel.data(), 1, 1, zones, {}, 2);
    require(pixel == std::vector<float>({-.1f, .2f, .6f}), "single-pixel PSF boundary");
}
// Independent exhaustive source traversal: no bounds or spatial index.
static void exhaustive_ibp(const Frames& raw, int h, int w, const uint8_t* cfa,
                           const Frames& fy, const Frames& fx, const Frames& mw,
                           const Noise& noise, std::vector<float>& rgb, int tile) {
    const int h2 = h * 2, w2 = w * 2;
    for (int iteration = 0; iteration < 2; ++iteration)
        for (int color = 0; color < 4; ++color)
            for (int y0 = 0; y0 < h2; y0 += tile) for (int x0 = 0; x0 < w2; x0 += tile) {
                if (((y0 / tile) % 2) * 2 + (x0 / tile) % 2 != color) continue;
                const int th = std::min(tile, h2 - y0), tw = std::min(tile, w2 - x0);
                std::vector<float> grad(th * tw * 3, 0), mass(grad.size(), 0);
                for (size_t n = 0; n < raw.size(); ++n)
                    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) {
                        float y = float(r), x = float(c);
                        // Fixed five-step reference, including the fixed-point tail.
                        for (int step = 0; step < 5; ++step) {
                            const float dy = detail::field(fy, n, h/2, w/2, y*.5f, x*.5f, 0);
                            const float dx = detail::field(fx, n, h/2, w/2, y*.5f, x*.5f, 0);
                            y = r - 2*dy; x = c - 2*dx;
                        }
                        const float v = raw[n][size_t(r)*w+c];
                        if (!detail::inside(y, x, h, w) || !std::isfinite(v) || !noise.unsaturated(n, v)) continue;
                        const float gate = detail::clamp01(detail::field(mw, n, h/2, w/2, y*.5f, x*.5f, 1));
                        if (gate <= 0) continue;
                        const auto footprint = detail::footprint(y, x, h2, w2, cfa[(r%6)*6+c%6]);
                        const float iw = gate / noise.frame_variance(n, v);
                        const float residual = v - detail::project(rgb.data(), footprint);
                        for (int k = 0; k < footprint.count; ++k) {
                            const size_t pixel = footprint.index[k] / 3;
                            const int yy = int(pixel/w2)-y0, xx = int(pixel%w2)-x0;
                            if (yy < 0 || yy >= th || xx < 0 || xx >= tw) continue;
                            const size_t i = (size_t(yy)*tw+xx)*3+footprint.index[k]%3;
                            grad[i] += footprint.weight[k]*iw*residual;
                            mass[i] += footprint.weight[k]*iw;
                        }
                    }
                for (int y = 0; y < th; ++y) for (int x = 0; x < tw; ++x) for (int c = 0; c < 3; ++c) {
                    const size_t i = (size_t(y)*tw+x)*3+c;
                    if (mass[i] > 0) rgb[(size_t(y+y0)*w2+x+x0)*3+c] += .9f*grad[i]/mass[i];
                }
            }
}
static void ibp_index_test() {
    constexpr int h = 50, w = 66, nframes = 3, tile = 16;
    const auto cfa = bayer();
    Frames raw(nframes, std::vector<float>(h*w));
    Frames fy(nframes, std::vector<float>(h*w/4)), fx = fy, mw = fy;
    std::vector<float> image(h*w*12);
    std::mt19937 rng(41);
    for (auto& frame : raw) for (float& v : frame) v = float(rng()%9000)*.0001f - .04f;
    for (float& v : image) v = float(rng()%6000)*.0001f;
    raw[1][37] = std::numeric_limits<float>::quiet_NaN();
    raw[2][29] = 1.2f;
    for (int mode = 0; mode < 3; ++mode) {
        for (int n = 0; n < nframes; ++n) for (int y = 0; y < h/2; ++y) for (int x = 0; x < w/2; ++x) {
            const size_t i = size_t(y)*(w/2)+x;
            fy[n][i] = mode == 0 ? 0 : (mode == 1 ? 7.25f*n : 22*std::sin(.07f*x+.12f*y+n));
            fx[n][i] = mode == 0 ? 0 : (mode == 1 ? -9.5f*n : 28*std::cos(.09f*x-.08f*y+n));
            mw[n][i] = (x+y+n)%3 ? .7f : 0;
        }
        auto expected = image, actual = image, parallel = image;
        const Noise noise{.0001f, .001f};
        exhaustive_ibp(raw, h, w, cfa.data(), fy, fx, mw, noise, expected, tile);
        Execution execution; execution.tile = tile;
        const auto stats = compact_ibp(raw, h, w, cfa.data(), fy, fx, mw, noise, actual.data(), 2, execution);
        auto threaded = parallel_execution(); threaded.tile = tile;
        compact_ibp(raw, h, w, cfa.data(), fy, fx, mw, noise, parallel.data(), 2, threaded);
        auto sampled=image;
        execution.diagnostic_stride=67;
        const auto sampled_stats=compact_ibp(raw,h,w,cfa.data(),fy,fx,mw,noise,sampled.data(),2,execution);
        require(sampled==actual && sampled_stats.samples<stats.samples,
                "sparse diagnostic telemetry preserves every reconstruction value");
        float error = 0;
        for (size_t i = 0; i < actual.size(); ++i) error = std::max(error, std::fabs(actual[i]-expected[i]));
        // O3 can contract different multiply/add pairs across the two loop
        // shapes. Eight float epsilons bound two iterations on this unit range.
        require(error <= 8*std::numeric_limits<float>::epsilon() && actual == parallel,
                "indexed/tight IBP matches exhaustive traversal within float contraction and is deterministic");
        if (mode == 2) require(stats.index_bytes > 0, "variable large flow exercises spatial index");
        else require(stats.index_bytes == 0, "small flow span uses tight bounds");
        const size_t exhaustive_candidates = size_t(nframes)*h*w*((2*h+tile-1)/tile)*((2*w+tile-1)/tile)*2;
        require(stats.candidate_samples < exhaustive_candidates/3, "IBP candidate work reduced at unchanged pixels");
        std::printf("IBP mode %d: %zu / %zu candidates, %zu index bytes; max delta %.9g\n",
                    mode, stats.candidate_samples, exhaustive_candidates, stats.index_bytes, error);
    }
}
static void inverse_cache_test() {
    constexpr int h=32,w=40,N=3;
    Frames fy(N,std::vector<float>(h*w)),fx=fy;
    for(int mode=0;mode<3;++mode) {
        std::vector<std::array<float,4>> bounds(N);
        for(int n=0;n<N;++n) {
            for(int r=0;r<h;++r) for(int c=0;c<w;++c) {
                fy[n][r*w+c]=n*.7f+(mode==1?.004f*r+.003f*c:mode==2?12*std::sin(.2f*c):0);
                fx[n][r*w+c]=n*.9f+(mode==1?.002f*r-.004f*c:mode==2?15*std::cos(.2f*r):0);
            }
            auto yy=std::minmax_element(fy[n].begin(),fy[n].end()),xx=std::minmax_element(fx[n].begin(),fx[n].end());
            bounds[n]={2**yy.first,2**yy.second,2**xx.first,2**xx.second};
        }
        const detail::InverseFlowCache cache{bounds,nullptr};
        float peak=0;
        for(int n=0;n<N;++n) for(int r=0;r<2*h;++r) for(int c=0;c<2*w;++c) {
            float ay,ax,by,bx;
            detail::inverse_flow(fy,fx,n,h,w,float(r),float(c),ay,ax);
            detail::cached_inverse_flow(fy,fx,n,h,w,float(r),float(c),by,bx,&cache);
            peak=std::max(peak,std::max(std::fabs(ay-by),std::fabs(ax-bx)));
        }
        std::printf("inverse cache mode %d max=%g raw pixels\n",mode,peak);
        require(peak<1e-4f,"Constant displacement shortcut preserves subpixel mapping");
    }
}
static void half_sampler_test() {
    constexpr int h=48,w=60,N=6,th=40,tw=48;
    auto cfa=bayer();
    Frames raw(N,std::vector<float>(h*w)),fy(N,std::vector<float>(h*w/4)),fx=fy,mw=fy;
    std::mt19937 rng(97);
    for(auto& plane:raw) for(float& v:plane) v=float(rng()%10000)*.0001f-.04f;
    raw[1][20]=std::numeric_limits<float>::quiet_NaN(); raw[2][21]=1.2f;
    std::vector<float> rgb(h*w*12,.17f),a(th*tw*3),b(a.size()),counts(th*tw);
    Noise noise{1e-5f,.001f};HalfStackOptions options;options.effective_frames=N;
    for(int mode=0;mode<2;++mode) {
        for(int n=0;n<N;++n) for(int y=0;y<h/2;++y) for(int x=0;x<w/2;++x) {
            size_t i=size_t(y)*(w/2)+x;
            fy[n][i]=mode?12*std::sin(.4f*x+n):.13f*n;
            fx[n][i]=mode?18*std::cos(.4f*y+n):.23f*n;
            mw[n][i]=((x+y+n)%7)?1:.5f;
        }
        detail::reconstruct_half_tile(raw,h,w,cfa.data(),fy,fx,mw,noise,rgb.data(),options,
            -4,-4,th,tw,a.data(),b.data(),counts.data());
        double peak=0,count_peak=0;
        for(int r=0;r<th;++r) for(int c=0;c<tw;++c) {
            const int y=detail::clamp(r-4,2*h),x=detail::clamp(c-4,2*w);
            float sum[2][3]={},weight[2][3]={},total=0,squared=0;int contributors[2]={};
            for(int n=0;n<N;++n) {
                float gate=detail::clamp01(detail::field(mw,n,h/2,w/2,y*.25f,x*.25f,1));
                float yy=y*.5f+2*detail::field(fy,n,h/2,w/2,y*.25f,x*.25f);
                float xx=x*.5f+2*detail::field(fx,n,h/2,w/2,y*.25f,x*.25f);
                if(gate<=.01f||!detail::inside(yy,xx,h,w)) continue;
                float v[3]={},den[3]={};
                for(int sy=std::max(0,int(std::floor(yy))-2);sy<=std::min(h-1,int(std::floor(yy))+3);++sy)
                    for(int sx=std::max(0,int(std::floor(xx))-2);sx<=std::min(w-1,int(std::floor(xx))+3);++sx) {
                        float value=raw[n][sy*w+sx];
                        if(!std::isfinite(value)||!noise.unsaturated(n,value)) continue;
                        int ch=cfa[(sy%6)*6+sx%6];
                        float k=std::max(0.0f,3-std::fabs(sy-yy))*std::max(0.0f,3-std::fabs(sx-xx));
                        v[ch]+=k*value;den[ch]+=k;
                    }
                bool valid=true;float precision=0;
                for(int ch=0;ch<3;++ch) {
                    if(den[ch]<=0) {valid=false;continue;}
                    float value=v[ch]/den[ch],iw=gate/noise.frame_variance(n,value);
                    sum[n%2][ch]+=iw*value;weight[n%2][ch]+=iw;
                    if(ch==1) precision=iw;
                }
                if(valid) {total+=precision;squared+=precision*precision;++contributors[n%2];}
            }
            const size_t i=size_t(r)*tw+c;
            for(int ch=0;ch<3;++ch) {
                float aa=weight[0][ch]>0?sum[0][ch]/weight[0][ch]:.17f;
                float bb=weight[1][ch]>0?sum[1][ch]/weight[1][ch]:.17f;
                peak=std::max(peak,double(std::max(std::fabs(a[3*i+ch]-aa),std::fabs(b[3*i+ch]-bb))));
            }
            float count=squared>0?total*total/squared:1;
            if(contributors[0]<2||contributors[1]<2) count=std::min(count,4.0f);
            count=std::min(count,float(N));count_peak=std::max(count_peak,double(std::fabs(count-counts[i])));
        }
        std::printf("half sampler mode %d max=%g count=%g\n",mode,peak,count_peak);
        require(peak<1e-6 && count_peak<1e-4,"separable/fallback half sampler matches independent CFA tent integration");
    }
}
static void kernel_power_test() {
    const auto identity = kernel_power_from_covariance(0, 0, 0);
    for (float value : identity) require(value == 1, "zero covariance unit transfer");
    const auto spectrum = kernel_power_from_covariance(.16f, .04f, .36f, 0);
    constexpr double pi2 = 9.869604401089358;
    require(spectrum[0] == 1 &&
            std::fabs(spectrum[1] - std::exp(-pi2 * .16)) < 1e-6 &&
            std::fabs(spectrum[4] - std::exp(-pi2 * .36)) < 1e-6 &&
            std::fabs(spectrum[2] - std::exp(-pi2 * .16 / 4)) < 1e-6,
            "Gaussian power uses exact covariance units and native Walsh frequency order");
    const auto precision = kernel_power_from_inverse_covariance(4, 0, 16);
    const auto expected = kernel_power_from_covariance(1, 0, .25f);
    require(precision == expected, "RAW precision converts to 2x covariance");
    for (float power : spectrum) require(std::isfinite(power) && power >= 0 && power <= 1, "Gaussian power finite bounded");
    bool threw = false;
    try { kernel_power_from_inverse_covariance(1, 2, 1); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "indefinite precision rejected");
    std::mt19937 rng(7858);
    std::normal_distribution<float> noise(0, .01f);
    constexpr int h = 32, w = 40;
    std::vector<float> a(h * w * 3), b(a.size()), white(a.size());
    for (int r = 0; r < h; ++r) for (int c = 0; c < w; ++c) for (int k = 0; k < 3; ++k) {
        const auto p = (r * w + c) * 3 + k;
        const float signal = .2f + .03f * std::sin(.7f * c) + .02f * k;
        a[p] = signal + noise(rng); b[p] = signal + noise(rng); white[p] = .5f * (a[p] + b[p]);
    }
    auto shaped = white;
    HalfStackOptions options; options.effective_frames = 8; options.phase_balanced = true;
    const auto white_stats = half_stack_denoise(white.data(), a.data(), b.data(), h, w, {.0004f, 0}, options);
    options.kernel_power = spectrum.data();
    const auto shaped_stats = half_stack_denoise(shaped.data(), a.data(), b.data(), h, w, {.0004f, 0}, options);
    std::printf("kernel power causal MSE=%.9g; noise %.9g -> %.9g\n",
                mse(white, shaped), white_stats.noise_variance, shaped_stats.noise_variance);
    require(mse(white, shaped) > 1e-9 && shaped_stats.noise_variance < white_stats.noise_variance,
            "known covariance actually changes ensemble whitening and shrinkage");
}
static void fusion_eta_test() {
    constexpr int h=64,w=80;
    std::vector<float> image(h*w*3),eta((h/8)*(w/8),.25f);
    std::mt19937 rng(447);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) for(int c=0;c<3;++c)
        image[(y*w+x)*3+c]=.2f+.03f*std::sin(.2f*x)+float(rng()%1000)*.00002f;
    auto explicit_count=image,propagated=image;
    HalfStackOptions options;options.effective_frames=4;
    const Noise noise{.0004f,0};
    half_stack_denoise(explicit_count.data(),nullptr,nullptr,h,w,noise,options);
    options.effective_frames=6;options.fusion_eta=eta.data();options.fusion_eta_h=h/8;options.fusion_eta_w=w/8;
    half_stack_denoise(propagated.data(),nullptr,nullptr,h,w,noise,options);
    require(explicit_count==propagated,"Local fusion eta .25 supplies exact analytical N_eff=4");
    auto changed=image;
    std::fill(eta.begin(),eta.end(),.5f);
    half_stack_denoise(changed.data(),nullptr,nullptr,h,w,noise,options);
    require(mse(changed,propagated)>1e-9,"Local fusion variance causally controls analytic shrinkage");
}
static void heterogeneous_topology_test() {
    using namespace heraw_cache;

    bool ok = true;
    (void)parse_core_policy("bogus", &ok);
    require(!ok, "invalid core policy rejected");
    (void)parse_task_priority("bogus", &ok);
    require(!ok, "invalid priority rejected");
    (void)parse_simd_policy("bogus", &ok);
    require(!ok, "invalid simd policy rejected");

    // 1. AMD Strix Point HX 365: 4x Zen 5 (16MB CCX0, 2T) + 6x Zen 5c (8MB CCX1, 2T) = 10C/20T
    CacheProfile strix{};
    strix.l1d_bytes = 48u * 1024u;
    strix.l2_per_core_bytes = 1024u * 1024u;
    strix.l3_llc_bytes = 8u * 1024u * 1024u;
    strix.l3_max_slice_bytes = 16u * 1024u * 1024u;
    strix.l3_total_bytes = 24u * 1024u * 1024u;
    strix.physical_cores = 10;
    strix.logical_threads = 20;
    strix.perf_cores = 4;
    strix.eff_cores = 6;
    strix.llc_slices = 2;
    strix.is_hybrid = true;
    heraw_cache::detail::finalize_budgets(strix);
    require(strix.is_asymmetric_llc, "Strix Point 16MB+8MB detected as asymmetric LLC");
    uint16_t os_id = 0;
    for (int c = 0; c < 10; ++c) {
        for (int t = 0; t < 2; ++t) {
            LogicalCpu lc{};
            lc.group = 0;
            lc.group_bit = uint8_t(os_id);
            lc.os_cpu_id = os_id++;
            lc.core_id = uint16_t(c);
            lc.smt_idx = uint8_t(t);
            lc.llc_slice = (c < 4) ? 0 : 1;
            lc.tier = (c < 4) ? CoreTier::PERF : CoreTier::EFF;
            strix.cpus.push_back(lc);
        }
    }
    auto strix_auto = plan_worker_cpus(strix, CpuCorePolicy::AUTO, 0);
    require(strix_auto.size() == 14, "Strix AUTO selects 4x2T Zen5 + 6x1T Zen5c = 14 workers");
    for (int i = 0; i < 8; ++i) require(strix_auto[i].llc_slice == 0 && strix_auto[i].tier == CoreTier::PERF, "Strix CCX0 grouped first");
    for (int i = 8; i < 14; ++i) require(strix_auto[i].llc_slice == 1 && strix_auto[i].smt_idx == 0, "Strix CCX1 1T/core");
    require(plan_worker_cpus(strix, CpuCorePolicy::PERF_ONLY, 0).size() == 8, "Strix PERF_ONLY = 8");
    require(plan_worker_cpus(strix, CpuCorePolicy::PHYSICAL_ONLY, 0).size() == 10, "Strix PHYSICAL_ONLY = 10");
    require(plan_worker_cpus(strix, CpuCorePolicy::EFF_ONLY, 0).size() == 12, "Strix EFF_ONLY = 12");
    require(plan_worker_cpus(strix, CpuCorePolicy::ALL_LOGICAL, 0).size() == 20, "Strix ALL_LOGICAL = 20");
    require(plan_worker_cpus(strix, CpuCorePolicy::AUTO, 20).size() == 20, "Strix AUTO explicit 20 threads tops up");
    require(strix.rl_band_rows(16560, 14) == strix.rl_band_rows(16560, 1), "Asymmetric L3 keeps band anchored to 8MB CCX1");

    // 2. Intel Meteor Lake / Arrow Lake-H 3-tier (P + E + LP-E): 6P(12T) + 8E(8T) + 2LPE(2T, L3-less) = 22T
    CacheProfile mtl{};
    mtl.l1d_bytes = 48u * 1024u;
    mtl.l2_per_core_bytes = 2048u * 1024u;
    mtl.l3_llc_bytes = 24u * 1024u * 1024u;
    mtl.l3_max_slice_bytes = 24u * 1024u * 1024u;
    mtl.l3_total_bytes = 24u * 1024u * 1024u;
    mtl.physical_cores = 16;
    mtl.logical_threads = 22;
    mtl.perf_cores = 6;
    mtl.eff_cores = 8;
    mtl.low_power_cores = 2;
    mtl.llc_slices = 1;
    mtl.is_hybrid = true;
    heraw_cache::detail::finalize_budgets(mtl);
    os_id = 0;
    for (int c = 0; c < 6; ++c) {
        for (int t = 0; t < 2; ++t) {
            mtl.cpus.push_back({0, uint8_t(os_id), os_id, uint16_t(c), uint8_t(t), 0, CoreTier::PERF});
            ++os_id;
        }
    }
    for (int c = 6; c < 14; ++c) {
        mtl.cpus.push_back({0, uint8_t(os_id), os_id, uint16_t(c), 0, 0, CoreTier::EFF});
        ++os_id;
    }
    for (int c = 14; c < 16; ++c) {
        mtl.cpus.push_back({0, uint8_t(os_id), os_id, uint16_t(c), 0, -1, CoreTier::LOW_POWER});
        ++os_id;
    }
    auto mtl_auto = plan_worker_cpus(mtl, CpuCorePolicy::AUTO, 0);
    require(mtl_auto.size() == 20, "Intel 3-tier AUTO excludes 2 L3-less LP-E cores -> 20 workers");
    for (const auto& c : mtl_auto) require(c.tier != CoreTier::LOW_POWER, "No LP-E in Intel AUTO pool");
    require(plan_worker_cpus(mtl, CpuCorePolicy::PERF_ONLY, 0).size() == 12, "Intel PERF_ONLY = 12");
    require(plan_worker_cpus(mtl, CpuCorePolicy::PHYSICAL_ONLY, 0).size() == 14, "Intel PHYSICAL_ONLY = 6P+8E = 14");
    require(plan_worker_cpus(mtl, CpuCorePolicy::EFF_ONLY, 0).size() == 8, "Intel EFF_ONLY = 8E");
    require(plan_worker_cpus(mtl, CpuCorePolicy::ALL_LOGICAL, 0).size() == 22, "Intel ALL_LOGICAL includes LP-E = 22");

    // 3. Intel Raptor Lake 2-tier (6P/12T + 8E/8T = 20T) & Apple Silicon (8P + 2E = 10T) & Homogeneous 8C/16T
    CacheProfile apple{};
    apple.physical_cores = 10; apple.logical_threads = 10; apple.perf_cores = 8; apple.eff_cores = 2; apple.is_hybrid = true;
    for (uint16_t i = 0; i < 10; ++i) apple.cpus.push_back({0, uint8_t(i), i, i, 0, 0, i < 8 ? CoreTier::PERF : CoreTier::EFF});
    require(recommended_thread_count(apple, CpuCorePolicy::AUTO, 0) == 10, "Apple AUTO = 10");
    require(recommended_thread_count(apple, CpuCorePolicy::PERF_ONLY, 0) == 8, "Apple PERF_ONLY = 8");
    require(recommended_thread_count(apple, CpuCorePolicy::EFF_ONLY, 0) == 2, "Apple EFF_ONLY = 2");

    CacheProfile x3d{};
    x3d.physical_cores = 8; x3d.logical_threads = 16; x3d.perf_cores = 8; x3d.is_hybrid = false;
    for (uint16_t c = 0; c < 8; ++c)
        for (uint8_t t = 0; t < 2; ++t)
            x3d.cpus.push_back({0, uint8_t(c * 2 + t), uint16_t(c * 2 + t), c, t, 0, CoreTier::PERF});
    require(plan_worker_cpus(x3d, CpuCorePolicy::EFF_ONLY, 0).size() == 8, "Homogeneous EFF_ONLY falls back to 8 physical cores");
    require(recommended_thread_count(x3d, CpuCorePolicy::AUTO, 0, true) == 8, "Battery/DC drops SMT on homogeneous 8C/16T -> 8");
    require(recommended_thread_count(strix, CpuCorePolicy::PHYSICAL_ONLY, 0, false) == 10, "Battery-device default PHYSICAL_ONLY on Strix = 10");

    // 4. Archived AVX-512 & GPU default-disable policy (APU + Intel <Xe2 off, Xe2+ & dGPU on)
    require(!heraw_cache::use_avx512(), "Production build archives AVX-512 (use_avx512 == false)");
    require(heraw_cache::should_default_disable_igpu(false, 0x1002u, 0x150Eu, "AMD Radeon 880M Graphics"), "AMD APU 880M default-off");
    require(heraw_cache::should_default_disable_igpu(false, 0x1002u, 0x164Eu, "AMD Radeon(TM) Graphics"), "Desktop Ryzen 2-CU iGPU default-off");
    require(heraw_cache::should_default_disable_igpu(false, 0x8086u, 0x3EA5u, "Intel(R) Iris(R) Plus Graphics 655"), "Intel Gen9.5 <Xe2 default-off");
    require(heraw_cache::should_default_disable_igpu(false, 0x8086u, 0xA780u, "Intel(R) UHD Graphics 770"), "Intel UHD 770 <Xe2 default-off");
    require(heraw_cache::should_default_disable_igpu(false, 0x8086u, 0x7D55u, "Intel(R) Arc(TM) Graphics"), "Meteor Lake Xe-LPG <Xe2 default-off");
    require(!heraw_cache::should_default_disable_igpu(false, 0x8086u, 0x64A0u, "Intel(R) Arc(TM) 140V GPU"), "Lunar Lake Xe2 Arc 140V stays ON");
    require(!heraw_cache::should_default_disable_igpu(false, 0x8086u, 0xB080u, "Intel(R) Graphics"), "Panther Lake Xe3 stays ON");
    require(!heraw_cache::should_default_disable_igpu(true,  0x10DEu, 0x2782u, "NVIDIA GeForce RTX 4080 SUPER"), "Desktop NVIDIA dGPU stays ON");
    require(!heraw_cache::should_default_disable_igpu(true,  0x1002u, 0x744Cu, "AMD Radeon RX 7900 XTX"), "Desktop AMD dGPU stays ON");
    require(!heraw_cache::should_default_disable_igpu(true,  0x8086u, 0xE20Bu, "Intel(R) Arc(TM) B580 Graphics"), "Desktop Intel Battlemage dGPU stays ON");
}
int main(int argc,char** argv) {
    const bool photo=argc>1 && std::string(argv[1])=="--photo";
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(_WIN32) && defined(HERAW_CORE_CRASH_TRACE)
    SetUnhandledExceptionFilter(core_crash_trace);
#endif
    heterogeneous_topology_test();
    exposure_test();
    row_and_lucky_test();
    silicon_test();
    ibp_test();
    ibp_index_test();
    half_stack_test();
    half_sampler_test();
    inverse_cache_test();
    exposure_weighting_test();
    if(!photo) { spatial_psf_test(); kernel_power_test(); }
    fusion_eta_test();
    std::puts("Suite core causal tests PASS");
}
