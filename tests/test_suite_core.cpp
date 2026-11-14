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
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if defined(_WIN32) && defined(HERAW_CORE_CRASH_TRACE)
    SetUnhandledExceptionFilter(core_crash_trace);
#endif
    exposure_test();
    row_and_lucky_test();
    silicon_test();
    ibp_test();
    half_stack_test();
    exposure_weighting_test();
    spatial_psf_test();
    kernel_power_test();
    std::puts("Suite core causal tests PASS");
}
