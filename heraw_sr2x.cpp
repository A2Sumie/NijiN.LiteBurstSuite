// nijinLBS (heraw_sr2x.cpp) -- NijiN.LiteBurstScaler: Fast Multi-Frame Upscaler
// engine producing 16-bit RGGB Linear RAW (DNG 1.4) and 16-bit sRGB (TIFF).
//
// Linked statically against LibRaw 0.23.0-Snapshot202611 (native Nikon HE/HE* support).
// Zero external model files, zero Python dependencies, zero scene-specific tuning.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#endif
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif
#ifdef __aarch64__
#include <arm_neon.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

#include "../libraw-heraw/libraw/libraw.h"
#include "heraw_libraw_dyn.hpp"
#include "heraw_version.hpp"
#include "heraw_cpu_cache.hpp"
#include "heraw_exif_io.hpp"
#include "heraw_wvdc_router.hpp"
#include "heraw_burst_planner.hpp"
#include "heraw_gpu_backend.hpp"
#include "heraw_wavelet_noise.hpp"
#include "heraw_suite_core.hpp"
#include "heraw_suite_features.hpp"
#include "heraw_suite_optics.hpp"
#if !defined(__APPLE__)
#include "heraw_gpu_vulkan.hpp"
#endif

namespace {

static inline int clamp_i(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clamp_f(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

class RowThreadPool {
public:
    RowThreadPool() = default;
    ~RowThreadPool() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stop_ = true;
            ++gen_;
        }
        cv_start_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
    }

    void configure(heraw_cache::CpuCorePolicy core_pol, heraw_cache::TaskPriority prio, int expected_threads, bool on_battery = false) {
        std::lock_guard<std::mutex> lk(mtx_);
        planned_cpus_ = heraw_cache::plan_worker_cpus(
            heraw_cache::host_cache_profile(), core_pol, std::max(1, expected_threads), on_battery);
        core_pol_ = core_pol;
        prio_ = prio;
        ++policy_gen_;
    }

    void apply_thread_slot(int slot) {
        const heraw_cache::LogicalCpu* target = nullptr;
        heraw_cache::CpuCorePolicy pol;
        heraw_cache::TaskPriority pr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (planned_cpus_.empty()) {
                planned_cpus_ = heraw_cache::plan_worker_cpus(heraw_cache::host_cache_profile(), core_pol_, 0);
            }
            if (!planned_cpus_.empty()) {
                target = &planned_cpus_[std::size_t(std::max(0, slot)) % planned_cpus_.size()];
            }
            pol = core_pol_;
            pr = prio_;
        }
        heraw_cache::apply_worker_thread_policy(target, pr, pol);
    }

    template <typename Fn>
    void run(int total_rows, int n_threads, int chunk_rows, Fn&& fn) {
        if (total_rows <= 0) return;
        int nt = std::max(1, std::min(n_threads, total_rows));
        int chunk = chunk_rows;
        bool guided_even = false;
        if (chunk <= 0) {
            int target = total_rows / (nt * 6);
            chunk = clamp_i(target, 1, 64);
            if (chunk > 1) chunk = (chunk + 1) & ~1;
            guided_even = true;
        }
        if (nt == 1 || total_rows <= 1) {
            if (!in_worker_flag()) ensure_main_thread_policy();
            fn(0, total_rows, 0);
            return;
        }

        int n_bg_use = nt - 1;
        next_row_.store(0, std::memory_order_relaxed);
        total_rows_ = total_rows;
        chunk_rows_ = chunk;
        active_nt_ = nt;
        guided_even_ = guided_even;
        task_fn_ = +[](void* ctx, int r0, int r1, int tid) {
            (*static_cast<typename std::remove_reference<Fn>::type*>(ctx))(r0, r1, tid);
        };
        task_ctx_ = const_cast<void*>(static_cast<const void*>(&fn));

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (planned_cpus_.empty()) {
                planned_cpus_ = heraw_cache::plan_worker_cpus(heraw_cache::host_cache_profile(), core_pol_, nt);
                ++policy_gen_;
            }
            while ((int)workers_.size() < n_bg_use) {
                int tid = (int)workers_.size() + 1;
                uint64_t init_gen = gen_;
                workers_.emplace_back([this, tid, init_gen]() { worker_loop(tid, init_gen); });
            }
            n_bg_active_ = n_bg_use;
            active_workers_ = n_bg_use;
            ++gen_;
        }
        cv_start_.notify_all();

        ensure_main_thread_policy();
        drain_chunks(0);

        std::unique_lock<std::mutex> lk(mtx_);
        cv_done_.wait(lk, [this]() { return active_workers_ == 0; });
    }

private:
    static bool& in_worker_flag() noexcept {
        static thread_local bool flag = false;
        return flag;
    }

    void ensure_main_thread_policy() {
        const uint64_t pgen = policy_gen_.load(std::memory_order_acquire);
        if (main_policy_gen_ != pgen) {
            apply_thread_slot(0);
            main_policy_gen_ = pgen;
        }
    }

    void drain_chunks(int tid) {
        const int tot = total_rows_;
        const int max_chk = chunk_rows_;
        if (max_chk <= 1) {
            while (true) {
                int r0 = next_row_.fetch_add(1, std::memory_order_relaxed);
                if (r0 >= tot) break;
                task_fn_(task_ctx_, r0, r0 + 1, tid);
            }
            return;
        }
        const int denom = std::max(2, active_nt_ * 4);
        const bool even_align = guided_even_;
        int cur = next_row_.load(std::memory_order_relaxed);
        while (cur < tot) {
            const int rem = tot - cur;
            int step = clamp_i(rem / denom, 1, max_chk);
            if (even_align && step > 1 && step < rem) step = (step + 1) & ~1;
            const int r1 = std::min(tot, cur + step);
            if (next_row_.compare_exchange_weak(cur, r1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                task_fn_(task_ctx_, cur, r1, tid);
                cur = next_row_.load(std::memory_order_relaxed);
            }
        }
    }

    void worker_loop(int tid, uint64_t seen_gen) {
        in_worker_flag() = true;
        uint64_t seen_pgen = 0;
        while (true) {
            int bg_lim = 0;
            uint64_t cur_pgen = 0;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_start_.wait(lk, [&]() { return stop_ || gen_ != seen_gen; });
                if (stop_) return;
                seen_gen = gen_;
                bg_lim = n_bg_active_;
                cur_pgen = policy_gen_.load(std::memory_order_relaxed);
            }
            if (seen_pgen != cur_pgen) {
                apply_thread_slot(tid);
                seen_pgen = cur_pgen;
            }
            if (tid <= bg_lim) {
                drain_chunks(tid);
                std::lock_guard<std::mutex> lk(mtx_);
                if (--active_workers_ == 0) {
                    cv_done_.notify_one();
                }
            }
        }
    }

    std::mutex mtx_;
    std::condition_variable cv_start_;
    std::condition_variable cv_done_;
    std::vector<std::thread> workers_;
    std::vector<heraw_cache::LogicalCpu> planned_cpus_;
    heraw_cache::CpuCorePolicy core_pol_ = heraw_cache::CpuCorePolicy::AUTO;
    heraw_cache::TaskPriority prio_ = heraw_cache::TaskPriority::NORMAL;
    std::atomic<uint64_t> policy_gen_{1};
    uint64_t main_policy_gen_ = 0;
    std::atomic<int> next_row_{0};
    int total_rows_ = 0;
    int chunk_rows_ = 1;
    int active_nt_ = 1;
    bool guided_even_ = false;
    int n_bg_active_ = 0;
    int active_workers_ = 0;
    uint64_t gen_ = 0;
    bool stop_ = false;
    void (*task_fn_)(void*, int, int, int) = nullptr;
    void* task_ctx_ = nullptr;
};

static RowThreadPool g_row_pool;

template <typename Fn>
static void parallel_for_rows(int total_rows, int n_threads, Fn&& fn, int chunk_rows = 0) {
    g_row_pool.run(total_rows, n_threads, chunk_rows, std::forward<Fn>(fn));
}

template <typename T>
struct UninitBuf {
    std::unique_ptr<T[]> ptr;
    size_t n = 0;
    UninitBuf() = default;
    explicit UninitBuf(size_t count) : ptr(count ? new T[count] : nullptr), n(count) {}
    void resize(size_t count) {
        if (count > n) {
            ptr.reset(new T[count]);
            n = count;
        }
    }
    void release() { ptr.reset(); n = 0; }
    T* data() { return ptr.get(); }
    const T* data() const { return ptr.get(); }
    T& operator[](size_t i) { return ptr[i]; }
    const T& operator[](size_t i) const { return ptr[i]; }
};

static inline void release_free_heap_pages() {
#ifdef __APPLE__
    malloc_zone_pressure_relief(nullptr, 0);
#endif
}

static float quick_percentile(std::vector<float>& v, float q) {
    if (v.empty()) return 0.0f;
    size_t idx = (size_t)clamp_i((int)std::lrintf(q * (float)(v.size() - 1)), 0, (int)v.size() - 1);
    std::nth_element(v.begin(), v.begin() + idx, v.end());
    return v[idx];
}

// Deterministic SplitMix64 TPDF dither in [-1, 1]
static inline float tpdf_dither(uint64_t seed) {
    uint64_t z = seed + 0x9e3779b97f4a7c15ULL;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    z = z ^ (z >> 31);
    float u1 = (float)(z & 0xFFFFFFULL) * (1.0f / 16777216.0f);
    float u2 = (float)((z >> 24) & 0xFFFFFFULL) * (1.0f / 16777216.0f);
    return u1 - u2;
}

// ---------------------------------------------------------------------------
// 1. LibRaw Parallel Unpack & CFA Canonicalization to RGGB
// ---------------------------------------------------------------------------
struct UnpackedFrame {
    std::string path;
    int W = 0; // canonical even active width (RGGB aligned)
    int H = 0; // canonical even active height (RGGB aligned)
    int flip = 0;
    float black_c[4] = {0, 0, 0, 0}; // R, G1, G2, B
    float black_6x6[6][6] = {};
    uint8_t cfa6x6[6][6] = {};
    bool is_xtrans = false;
    int shutter_kind = -1; // unknown, mechanical=0, electronic=1, global=2
    bool has_ois = false;
    float af_norm_x = -1.0f, af_norm_y = -1.0f;
    float focal_mm = 0.0f, aperture = 0.0f, shutter_s = 0.0f;
    float iso = 0.0f, focus_distance_m = 0.0f, focus_step = -1.0f;
    float sensor_temp_c = -999.0f;
    double timestamp_s = 0.0;
    std::string lens;
    float white = 16383.0f;
    float wb[3] = {1.0f, 1.0f, 1.0f}; // R, G, B multipliers (G=1)
    double cam_xyz[3][3] = {{0}};
    double rgb_cam[3][3] = {{0}};
    std::string make;
    std::string model;
    std::vector<uint16_t> bayer; // canonical RGGB: (0,0)=R, (0,1)=G1, (1,0)=G2, (1,1)=B
    heraw_wvdc::RawDecoderPref decoder_pref = heraw_wvdc::RawDecoderPref::AUTO;
    int inner_threads = 1;
#ifdef HERAW_HAS_WVDC
    nijinwvdc::WaveletQuantProfile wvdc_quant;
#endif
    int status = 0;
    std::string err_msg;
};

template <typename ColorFn>
static void capture_frame_metadata(UnpackedFrame& uf, const libraw_data_t& d,
                                   int top, int left, ColorFn color_at) {
    uf.is_xtrans = d.idata.filters == 9;
    const auto& col = d.color;
    unsigned absolute_colors=0;
    for(const auto& row:d.idata.xtrans_abs) for(unsigned char c:row)
        if(c<3) absolute_colors|=1u<<c;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            const int ci = uf.is_xtrans ? (absolute_colors==7 ?
                d.idata.xtrans_abs[(top+y)%6][(left+x)%6] : d.idata.xtrans[y%6][x%6]) :
                (color_at(top + y, left + x) & 3);
            uf.cfa6x6[y][x] = (uint8_t)(ci == 3 ? 1 : ci);
            float black = (float)col.black + (float)col.cblack[ci];
            const unsigned bh = col.cblack[4], bw = col.cblack[5];
            if (bh && bw && (uint64_t)bh * bw <= LIBRAW_CBLACK_SIZE - 6)
                black += col.cblack[6 + ((top + y) % bh) * bw + (left + x) % bw];
            uf.black_6x6[y][x] = black;
            if (y < 2 && x < 2) uf.black_c[y * 2 + x] = black;
        }
    }
    uf.focal_mm = d.other.focal_len;
    uf.aperture = d.other.aperture;
    uf.shutter_s = d.other.shutter;
    uf.iso = d.other.iso_speed;
    if(d.makernotes.common.SensorTemperature>=-80 && d.makernotes.common.SensorTemperature<=160)
        uf.sensor_temp_c=d.makernotes.common.SensorTemperature;
    else if(d.makernotes.common.CameraTemperature>=-80 && d.makernotes.common.CameraTemperature<=160)
        uf.sensor_temp_c=d.makernotes.common.CameraTemperature;
    else if(d.makernotes.common.SensorTemperature2>=-80 && d.makernotes.common.SensorTemperature2<=160)
        uf.sensor_temp_c=d.makernotes.common.SensorTemperature2;
    const auto source_exif=heraw_io::extract_source_exif(uf.path);
    uf.timestamp_s = (double)d.other.timestamp + 0.001 * source_exif.subsec_ms;
    for(const auto& tag:source_exif.exif_keep) {
        if(tag.tag==37382 && tag.type==5 && tag.data.size()>=8) {
            const uint32_t num=heraw_io::rd_u32(tag.data.data(),false);
            const uint32_t den=heraw_io::rd_u32(tag.data.data()+4,false);
            if(num && den) uf.focus_distance_m=num==0xffffffffu ? 1000000.0f : (float)num/den;
        }
    }
    if (uf.focus_distance_m <= 0.0f && d.makernotes.olympus.FocusDistance > 0.0 &&
        std::isfinite(d.makernotes.olympus.FocusDistance)) {
        uf.focus_distance_m = (float)d.makernotes.olympus.FocusDistance;
    }
    if (d.makernotes.olympus.FocusStepCount > 0 && d.makernotes.olympus.FocusStepCount < 65535)
        uf.focus_step = (float)d.makernotes.olympus.FocusStepCount;
    else if (d.makernotes.panasonic.FocusStepCount > 0)
        uf.focus_step = (float)d.makernotes.panasonic.FocusStepCount;
    else if (d.makernotes.pentax.FocusPosition > 0 && d.makernotes.pentax.FocusPosition < 65535)
        uf.focus_step = (float)d.makernotes.pentax.FocusPosition;
    else if (d.makernotes.sony.FocusPosition > 0 && d.makernotes.sony.FocusPosition < 65535)
        uf.focus_step = (float)d.makernotes.sony.FocusPosition;
    uf.lens = d.lens.Lens;
    const int stab = d.shootinginfo.ImageStabilization;
    uf.has_ois = stab > 0 && stab != 0xffff;
    if (uf.model.find("ILCE-9M3") != std::string::npos) uf.shutter_kind = 2;
    else if (uf.model.find("Z 8") != std::string::npos || uf.model.find("Z8") != std::string::npos ||
             uf.model.find("Z 9") != std::string::npos || uf.model.find("Z9") != std::string::npos) {
        uf.shutter_kind = 1;
    }
    if (uf.make.find("Fuji") != std::string::npos || uf.make.find("FUJI") != std::string::npos) {
        const auto& f = d.makernotes.fuji;
        if (f.ShutterType <= 3) uf.shutter_kind = f.ShutterType == 0 || f.ShutterType == 3 ? 0 : 1;
        if (f.FocusPixel[0] != 65535 && f.FocusPixel[1] != 65535) {
            uf.af_norm_x = (float)f.FocusPixel[0] / std::max(1, uf.W);
            uf.af_norm_y = (float)f.FocusPixel[1] / std::max(1, uf.H);
        }
    }
    if (uf.make.find("Sony") != std::string::npos || uf.make.find("SONY") != std::string::npos) {
        const auto& f = d.makernotes.sony.FocusLocation;
        if (f[0] > 0 && f[0] < 65535 && f[1] > 0 && f[1] < 65535 &&
            f[2] < f[0] && f[3] < f[1]) {
            uf.af_norm_x = (float)f[2] / f[0];
            uf.af_norm_y = (float)f[3] / f[1];
        }
    }
    if (uf.shutter_kind < 0 && uf.shutter_s > 0.0f && uf.shutter_s < (1.0f / 8100.0f)) {
        uf.shutter_kind = 1;
    }
    if (!(uf.af_norm_x >= 0 && uf.af_norm_x <= 1 && uf.af_norm_y >= 0 && uf.af_norm_y <= 1))
        uf.af_norm_x = uf.af_norm_y = -1.0f;
}

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring(s.begin(), s.end());
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
#endif

static void ensure_parent_dir_utf8(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos || pos == 0) return;
    std::string dir = path.substr(0, pos);
#ifdef _WIN32
    std::wstring wdir = utf8_to_wide(dir);
    for (size_t i = 0; i < wdir.size(); ++i) {
        if ((wdir[i] == L'/' || wdir[i] == L'\\') && i > 0 && wdir[i - 1] != L':') {
            std::wstring sub = wdir.substr(0, i);
            CreateDirectoryW(sub.c_str(), nullptr);
        }
    }
    CreateDirectoryW(wdir.c_str(), nullptr);
#else
    for (size_t i = 1; i < dir.size(); ++i) {
        if (dir[i] == '/') {
            std::string sub = dir.substr(0, i);
            ::mkdir(sub.c_str(), 0755);
        }
    }
    ::mkdir(dir.c_str(), 0755);
#endif
}

static FILE* fopen_utf8_wb(const std::string& path) {
    ensure_parent_dir_utf8(path);
#ifdef _WIN32
    std::wstring wp = utf8_to_wide(path);
    FILE* fp = _wfopen(wp.c_str(), L"wb");
#else
    FILE* fp = std::fopen(path.c_str(), "wb");
#endif
    if (fp) std::setvbuf(fp, nullptr, _IOFBF, 16 * 1024 * 1024);
    return fp;
}

static void remove_utf8(const std::string& path) {
#ifdef _WIN32
    DeleteFileW(utf8_to_wide(path).c_str());
#else
    std::remove(path.c_str());
#endif
}

static bool atomic_replace_utf8(const std::string& tmp_path, const std::string& dst_path) {
#ifdef _WIN32
    std::wstring wtmp = utf8_to_wide(tmp_path);
    std::wstring wdst = utf8_to_wide(dst_path);
    return MoveFileExW(wtmp.c_str(), wdst.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(tmp_path.c_str(), dst_path.c_str()) == 0;
#endif
}

struct DiagLogMirror {
    FILE* fp = nullptr;
    std::mutex mtx;

    bool open(const std::string& path, const std::vector<std::string>& args) {
        std::lock_guard<std::mutex> lk(mtx);
        if (fp) { std::fclose(fp); fp = nullptr; }
        if (path.empty()) return true;
        ensure_parent_dir_utf8(path);
#ifdef _WIN32
        std::wstring wp = utf8_to_wide(path);
        fp = _wfopen(wp.c_str(), L"ab");
#else
        fp = std::fopen(path.c_str(), "ab");
#endif
        if (!fp) return false;
        std::time_t now_t = std::time(nullptr);
        char tbuf[64] = {};
        if (const std::tm* lt = std::localtime(&now_t)) {
            std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S %z", lt);
        }
        std::fprintf(fp, "=== nijinLBS " NIJIN_VERSION " Session [%s] ===\nCommand: nijinLBS", tbuf);
        for (const auto& a : args) {
            if (a.find(' ') != std::string::npos) std::fprintf(fp, " \"%s\"", a.c_str());
            else std::fprintf(fp, " %s", a.c_str());
        }
        std::fprintf(fp, "\n");
        std::fflush(fp);
        return true;
    }

    void write_raw(const char* data, size_t len) {
        if (!len) return;
        std::lock_guard<std::mutex> lk(mtx);
        if (fp) {
            std::fwrite(data, 1, len, fp);
            std::fflush(fp);
        }
    }

    void close(int exit_code) {
        std::lock_guard<std::mutex> lk(mtx);
        if (fp) {
            std::fprintf(fp, "=== Exit Code: %d ===\n\n", exit_code);
            std::fclose(fp);
            fp = nullptr;
        }
    }
};

static DiagLogMirror g_diag_log;

static void log_err(const char* fmt, ...) {
    char sbuf[2048];
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = std::vsnprintf(sbuf, sizeof(sbuf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        va_end(ap2);
        return;
    }
    if ((size_t)n < sizeof(sbuf)) {
        va_end(ap2);
        std::fwrite(sbuf, 1, (size_t)n, stderr);
        g_diag_log.write_raw(sbuf, (size_t)n);
    } else {
        std::string dyn((size_t)n + 1, '\0');
        std::vsnprintf(&dyn[0], dyn.size(), fmt, ap2);
        va_end(ap2);
        std::fwrite(dyn.data(), 1, (size_t)n, stderr);
        g_diag_log.write_raw(dyn.data(), (size_t)n);
    }
}

#ifdef HERAW_HAS_WVDC
template <typename ColorFn>
static void enrich_wvdc_frame_from_libraw_data(UnpackedFrame& out, const libraw_data_t& d, ColorFn color_at) {
    uint8_t saved_cfa[6][6];
    std::memcpy(saved_cfa, out.cfa6x6, sizeof(saved_cfa));
    float saved_black_c[4];
    float saved_black_6x6[6][6];
    std::memcpy(saved_black_c, out.black_c, sizeof(saved_black_c));
    std::memcpy(saved_black_6x6, out.black_6x6, sizeof(saved_black_6x6));

    const int rw = d.sizes.raw_width, rh = d.sizes.raw_height;
    int top = d.sizes.top_margin, left = d.sizes.left_margin;
    int vis_w = d.sizes.width, vis_h = d.sizes.height;
    if (top < 0 || left < 0 || top + vis_h > rh || left + vis_w > rw) {
        top = 0; left = 0;
    }
    int dy_r = 0, dx_r = 0;
    bool found_r = (d.idata.filters == 9);
    for (int dr = 0; dr < 2 && !found_r; ++dr) {
        for (int dc = 0; dc < 2 && !found_r; ++dc) {
            int c00 = color_at(top + dr, left + dc);
            int c01 = color_at(top + dr, left + dc + 1);
            int c10 = color_at(top + dr + 1, left + dc);
            int c11 = color_at(top + dr + 1, left + dc + 1);
            if (c00 == 0 && (c01 == 1 || c01 == 3) && (c10 == 1 || c10 == 3) && c11 == 2) {
                dy_r = dr; dx_r = dc; found_r = true;
            }
        }
    }
    top += dy_r; left += dx_r;

    if (d.idata.make[0]) out.make = d.idata.make;
    if (d.idata.model[0]) out.model = d.idata.model;
    if (d.sizes.flip != 0) out.flip = d.sizes.flip;
    if (d.color.maximum > 1023u) out.white = (float)d.color.maximum;

    const auto& col = d.color;
    float mr = col.cam_mul[0], mg = col.cam_mul[1], mb = col.cam_mul[2];
    if (mg <= 1e-5f || mr <= 1e-5f || mb <= 1e-5f) {
        mr = col.pre_mul[0]; mg = col.pre_mul[1]; mb = col.pre_mul[2];
    }
    if (mg > 1e-5f && mr > 1e-5f && mb > 1e-5f) {
        out.wb[0] = mr / mg; out.wb[1] = 1.0f; out.wb[2] = mb / mg;
    }
    bool has_cxyz = false, has_rgbcam = false;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (col.cam_xyz[r][c] != 0.0) has_cxyz = true;
            if (col.rgb_cam[r][c] != 0.0) has_rgbcam = true;
        }
    }
    if (has_cxyz) {
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out.cam_xyz[r][c] = col.cam_xyz[r][c];
    }
    if (has_rgbcam) {
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) out.rgb_cam[r][c] = col.rgb_cam[r][c];
    }

    const float saved_focal = out.focal_mm, saved_ap = out.aperture, saved_sh = out.shutter_s, saved_iso = out.iso;
    const double saved_ts = out.timestamp_s;
    const std::string saved_lens = out.lens;
    capture_frame_metadata(out, d, top, left, color_at);

    std::memcpy(out.cfa6x6, saved_cfa, sizeof(saved_cfa));
    out.is_xtrans = false;
    if (col.black == 0 && col.cblack[0] == 0 && saved_black_c[0] > 0.0f) {
        std::memcpy(out.black_c, saved_black_c, sizeof(saved_black_c));
        std::memcpy(out.black_6x6, saved_black_6x6, sizeof(saved_black_6x6));
    }
    if (out.focal_mm <= 0.0f) out.focal_mm = saved_focal;
    if (out.aperture <= 0.0f) out.aperture = saved_ap;
    if (out.shutter_s <= 0.0f) out.shutter_s = saved_sh;
    if (out.iso <= 0.0f) out.iso = saved_iso;
    if (d.other.timestamp <= 0) out.timestamp_s = saved_ts;
    if (out.lens.empty()) out.lens = saved_lens;
}
#endif

static void unpack_one_frame(UnpackedFrame* uf) {
#ifdef HERAW_HAS_WVDC
    const bool use_wvdc = (uf->decoder_pref == heraw_wvdc::RawDecoderPref::WVDC) ||
                          (uf->decoder_pref == heraw_wvdc::RawDecoderPref::AUTO &&
                           heraw_wvdc::can_decode_with_wvdc(uf->path));
    if (use_wvdc) {
        auto enrich_hdr = [](const std::string& phys_path, UnpackedFrame& out) {
            const auto& dyn = heraw_lr::global_dyn_libraw();
            if (dyn.active) {
                libraw_data_t* dlr = dyn.p_init(0);
                if (dlr) {
                    if (dyn.open_path(dlr, phys_path) == LIBRAW_SUCCESS) {
                        enrich_wvdc_frame_from_libraw_data(
                            out, *dlr, [&](int y, int x) { return dyn.p_COLOR(dlr, y, x); });
                    }
                    dyn.p_close(dlr);
                }
                return;
            }
            std::unique_ptr<LibRaw> lr(new LibRaw());
#ifdef _WIN32
            int rc = lr->open_file(utf8_to_wide(phys_path).c_str());
#else
            int rc = lr->open_file(phys_path.c_str());
#endif
            if (rc == LIBRAW_SUCCESS) {
                enrich_wvdc_frame_from_libraw_data(
                    out, lr->imgdata, [&](int y, int x) { return lr->COLOR(y, x); });
            }
        };
        if (heraw_wvdc::unpack_frame_via_wvdc(*uf, uf->inner_threads, enrich_hdr)) {
            return;
        }
        if (uf->decoder_pref == heraw_wvdc::RawDecoderPref::WVDC) {
            return;
        }
    }
#endif
    const auto& dyn = heraw_lr::global_dyn_libraw();
    if (dyn.active) {
        libraw_data_t* dlr = dyn.p_init(0);
        if (!dlr) { uf->status = -1; uf->err_msg = "libraw_init failed"; return; }
        int rc = dyn.open_path(dlr, uf->path);
        if (rc != LIBRAW_SUCCESS) {
            uf->status = rc;
            uf->err_msg = std::string("open_file failed: ") + (dyn.p_strerror ? dyn.p_strerror(rc) : "err");
            dyn.p_close(dlr);
            return;
        }
        rc = dyn.p_unpack(dlr);
        if (rc != LIBRAW_SUCCESS) {
            uf->status = rc;
            uf->err_msg = std::string("unpack failed: ") + (dyn.p_strerror ? dyn.p_strerror(rc) : "err");
            dyn.p_close(dlr);
            return;
        }
        if (dyn.ver_num == LIBRAW_VERSION && dlr->rawdata.raw_image) {
            // Exact matching ABI: use direct raw_image + color pedestal metadata
            const int rw = dlr->sizes.raw_width, rh = dlr->sizes.raw_height;
            int top = dlr->sizes.top_margin, left = dlr->sizes.left_margin;
            int vis_w = dlr->sizes.width, vis_h = dlr->sizes.height;
            if (top < 0 || left < 0 || top + vis_h > rh || left + vis_w > rw) { top = 0; left = 0; vis_w = rw; vis_h = rh; }
            int dy_r = 0, dx_r = 0; bool found_r = dlr->idata.filters == 9;
            for (int dr = 0; dr < 2 && !found_r; ++dr)
                for (int dc = 0; dc < 2 && !found_r; ++dc) {
                    int c00 = dyn.p_COLOR(dlr, top + dr, left + dc), c01 = dyn.p_COLOR(dlr, top + dr, left + dc + 1);
                    int c10 = dyn.p_COLOR(dlr, top + dr + 1, left + dc), c11 = dyn.p_COLOR(dlr, top + dr + 1, left + dc + 1);
                    if (c00 == 0 && (c01 == 1 || c01 == 3) && (c10 == 1 || c10 == 3) && c11 == 2) { dy_r = dr; dx_r = dc; found_r = true; }
                }
            top += dy_r; left += dx_r;
            int W = (std::min(vis_w - dx_r, rw - left)) & ~1, H = (std::min(vis_h - dy_r, rh - top)) & ~1;
            if (W < 64 || H < 64) { uf->status = -11; uf->err_msg = "Invalid dims"; dyn.p_close(dlr); return; }
            uf->W = W; uf->H = H; uf->flip = dlr->sizes.flip;
            uf->white = (float)std::max(1023u, dlr->color.maximum);
            uf->make = dlr->idata.make[0] ? dlr->idata.make : "Camera";
            uf->model = dlr->idata.model[0] ? dlr->idata.model : "Model";
            for (int py = 0; py < 2; ++py)
                for (int px = 0; px < 2; ++px) {
                    int r_a = top + py, c_a = left + px, ci = dyn.p_COLOR(dlr, r_a, c_a);
                    float b = (float)dlr->color.black + (float)dlr->color.cblack[ci & 3];
                    if (dlr->color.cblack[4] == 2 && dlr->color.cblack[5] == 2)
                        b += (float)dlr->color.cblack[6 + (r_a % 2) * 2 + (c_a % 2)];
                    uf->black_c[py * 2 + px] = b;
                }
            float mr = dlr->color.cam_mul[0], mg = dlr->color.cam_mul[1], mb = dlr->color.cam_mul[2];
            if (mg <= 1e-5f || mr <= 1e-5f || mb <= 1e-5f) { mr = dlr->color.pre_mul[0]; mg = dlr->color.pre_mul[1]; mb = dlr->color.pre_mul[2]; }
            if (mg <= 1e-5f) mg = 1.0f;
            if (mr <= 1e-5f) mr = 1.0f;
            if (mb <= 1e-5f) mb = 1.0f;
            uf->wb[0] = mr / mg; uf->wb[1] = 1.0f; uf->wb[2] = mb / mg;
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) { uf->cam_xyz[r][c] = dlr->color.cam_xyz[r][c]; uf->rgb_cam[r][c] = dlr->color.rgb_cam[r][c]; }
            uf->bayer.resize((size_t)H * (size_t)W);
            const uint16_t* src = dlr->rawdata.raw_image;
            for (int r = 0; r < H; ++r) std::memcpy(uf->bayer.data() + (size_t)r * W, src + (size_t)(top + r) * rw + left, (size_t)W * sizeof(uint16_t));
            capture_frame_metadata(*uf, *dlr, top, left, [&](int y, int x) { return dyn.p_COLOR(dlr, y, x); });
            uf->status = 0;
            dyn.p_close(dlr);
            return;
        }
        // Cross-version ABI-safe path via offset-0 dlr->image + C API getters
        rc = dyn.p_raw2image(dlr);
        if (rc != LIBRAW_SUCCESS || !dlr->image) { uf->status = -10; uf->err_msg = "raw2image failed"; dyn.p_close(dlr); return; }
        if (dyn.p_subtract_black) dyn.p_subtract_black(dlr);
        int iw = dyn.p_get_iwidth(dlr), ih = dyn.p_get_iheight(dlr);
        libraw_iparams_t* ip = dyn.p_get_iparams(dlr);
        int dy_r = 0, dx_r = 0; bool found_r = ip && ip->filters == 9;
        for (int dr = 0; dr < 2 && !found_r; ++dr)
            for (int dc = 0; dc < 2 && !found_r; ++dc) {
                int c00 = dyn.p_COLOR(dlr, dr, dc), c01 = dyn.p_COLOR(dlr, dr, dc + 1);
                int c10 = dyn.p_COLOR(dlr, dr + 1, dc), c11 = dyn.p_COLOR(dlr, dr + 1, dc + 1);
                if (c00 == 0 && (c01 == 1 || c01 == 3) && (c10 == 1 || c10 == 3) && c11 == 2) { dy_r = dr; dx_r = dc; found_r = true; }
            }
        int W = (iw - dx_r) & ~1, H = (ih - dy_r) & ~1;
        if (W < 64 || H < 64) { uf->status = -11; uf->err_msg = "Invalid dims"; dyn.p_close(dlr); return; }
        uf->W = W; uf->H = H; uf->flip = dlr->sizes.flip;
        uf->white = (float)std::max(1023, dyn.p_get_color_maximum ? dyn.p_get_color_maximum(dlr) : 16383);
        uf->make = (ip && ip->make[0]) ? ip->make : "Camera";
        uf->model = (ip && ip->model[0]) ? ip->model : "Model";
        uf->is_xtrans = ip && ip->filters == 9;
        if (auto* io = dyn.p_get_imgother(dlr)) {
            uf->focal_mm = io->focal_len; uf->aperture = io->aperture;
            uf->shutter_s = io->shutter; uf->iso = io->iso_speed;
            uf->timestamp_s = (double)io->timestamp + 0.001 * heraw_io::read_raw_subsec_ms(uf->path);
        }
        if (uf->model.find("ILCE-9M3") != std::string::npos) uf->shutter_kind = 2;
        else if (uf->model.find("Z 8") != std::string::npos || uf->model.find("Z8") != std::string::npos ||
                 uf->model.find("Z 9") != std::string::npos || uf->model.find("Z9") != std::string::npos ||
                 (uf->shutter_s > 0.0f && uf->shutter_s < (1.0f / 8100.0f))) {
            uf->shutter_kind = 1;
        }
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x) {
                int ci = dyn.p_COLOR(dlr, y + dy_r, x + dx_r) & 3;
                uf->cfa6x6[y][x] = (uint8_t)(ci == 3 ? 1 : ci);
                uf->black_6x6[y][x] = uf->black_c[(y & 1) * 2 + (x & 1)];
            }
        float mr = dyn.p_get_cam_mul ? dyn.p_get_cam_mul(dlr, 0) : 1.0f;
        float mg = dyn.p_get_cam_mul ? dyn.p_get_cam_mul(dlr, 1) : 1.0f;
        float mb = dyn.p_get_cam_mul ? dyn.p_get_cam_mul(dlr, 2) : 1.0f;
        if (mg <= 1e-5f || mr <= 1e-5f || mb <= 1e-5f) {
            mr = dyn.p_get_pre_mul ? dyn.p_get_pre_mul(dlr, 0) : 1.0f;
            mg = dyn.p_get_pre_mul ? dyn.p_get_pre_mul(dlr, 1) : 1.0f;
            mb = dyn.p_get_pre_mul ? dyn.p_get_pre_mul(dlr, 2) : 1.0f;
        }
        if (mg <= 1e-5f) mg = 1.0f;
        uf->wb[0] = (mr > 1e-5f ? mr : 1.0f) / mg; uf->wb[1] = 1.0f; uf->wb[2] = (mb > 1e-5f ? mb : 1.0f) / mg;
        if (dyn.p_get_rgb_cam) {
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) uf->rgb_cam[r][c] = dyn.p_get_rgb_cam(dlr, r, c);
        }
        uf->bayer.resize((size_t)H * (size_t)W);
        for (int r = 0; r < H; ++r) {
            for (int c = 0; c < W; ++c) {
                int sr = r + dy_r, sc = c + dx_r;
                int ci = dyn.p_COLOR(dlr, sr, sc) & 3;
                uf->bayer[(size_t)r * W + c] = dlr->image[(size_t)sr * iw + sc][ci];
            }
        }
        uf->status = 0;
        dyn.p_close(dlr);
        return;
    }

    std::unique_ptr<LibRaw> lr(new LibRaw());
#ifdef _WIN32
    std::wstring wpath = utf8_to_wide(uf->path);
    int rc = lr->open_file(wpath.c_str());
#else
    int rc = lr->open_file(uf->path.c_str());
#endif
    if (rc != LIBRAW_SUCCESS) {
        uf->status = rc;
        uf->err_msg = std::string("open_file failed: ") + libraw_strerror(rc);
        return;
    }
    rc = lr->unpack();
    if (rc != LIBRAW_SUCCESS) {
        uf->status = rc;
        uf->err_msg = std::string("unpack failed: ") + libraw_strerror(rc);
        return;
    }
    if (!lr->imgdata.rawdata.raw_image) {
        uf->status = -10;
        uf->err_msg = "Not a single-plane Bayer raw image";
        return;
    }

    const int rw = lr->imgdata.sizes.raw_width;
    const int rh = lr->imgdata.sizes.raw_height;
    int top = lr->imgdata.sizes.top_margin;
    int left = lr->imgdata.sizes.left_margin;
    int vis_w = lr->imgdata.sizes.width;
    int vis_h = lr->imgdata.sizes.height;
    if (top < 0 || left < 0 || top + vis_h > rh || left + vis_w > rw) {
        top = 0; left = 0; vis_w = rw; vis_h = rh;
    }

    // Shift (top, left) by 0 or 1 pixel if needed so (top, left) starts on Red (COLOR == 0)
    // and (top, left+1) is Green, guaranteeing canonical RGGB alignment!
    int dy_r = 0, dx_r = 0;
    bool found_r = lr->imgdata.idata.filters == 9;
    for (int dr = 0; dr < 2 && !found_r; ++dr) {
        for (int dc = 0; dc < 2 && !found_r; ++dc) {
            int c00 = lr->COLOR(top + dr, left + dc);
            int c01 = lr->COLOR(top + dr, left + dc + 1);
            int c10 = lr->COLOR(top + dr + 1, left + dc);
            int c11 = lr->COLOR(top + dr + 1, left + dc + 1);
            // LibRaw COLOR: 0=R, 1=G, 2=B, 3=G2
            if (c00 == 0 && (c01 == 1 || c01 == 3) && (c10 == 1 || c10 == 3) && c11 == 2) {
                dy_r = dr;
                dx_r = dc;
                found_r = true;
            }
        }
    }
    top += dy_r;
    left += dx_r;
    int W = (std::min(vis_w - dx_r, rw - left)) & ~1;
    int H = (std::min(vis_h - dy_r, rh - top)) & ~1;
    if (W < 64 || H < 64) {
        uf->status = -11;
        uf->err_msg = "Invalid image dimensions after RGGB alignment";
        return;
    }

    uf->W = W;
    uf->H = H;
    uf->flip = lr->imgdata.sizes.flip;
    uf->white = (float)std::max(1023u, lr->imgdata.color.maximum);
    uf->make = lr->imgdata.idata.make[0] ? lr->imgdata.idata.make : "Camera";
    uf->model = lr->imgdata.idata.model[0] ? lr->imgdata.idata.model : "Model";

    // Per-position black level in canonical RGGB order
    const auto& col = lr->imgdata.color;
    for (int py = 0; py < 2; ++py) {
        for (int px = 0; px < 2; ++px) {
            int r_abs = top + py;
            int c_abs = left + px;
            int c_idx = lr->COLOR(r_abs, c_abs);
            float b = (float)col.black + (float)col.cblack[c_idx & 3];
            if (col.cblack[4] == 2 && col.cblack[5] == 2) {
                b += (float)col.cblack[6 + (r_abs % 2) * 2 + (c_abs % 2)];
            }
            uf->black_c[py * 2 + px] = b;
        }
    }

    // White balance multipliers (R, G, B)
    float mr = col.cam_mul[0], mg = col.cam_mul[1], mb = col.cam_mul[2];
    if (mg <= 1e-5f || mr <= 1e-5f || mb <= 1e-5f) {
        mr = col.pre_mul[0]; mg = col.pre_mul[1]; mb = col.pre_mul[2];
    }
    if (mg <= 1e-5f) mg = 1.0f;
    if (mr <= 1e-5f) mr = 1.0f;
    if (mb <= 1e-5f) mb = 1.0f;
    uf->wb[0] = mr / mg;
    uf->wb[1] = 1.0f;
    uf->wb[2] = mb / mg;

    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            uf->cam_xyz[r][c] = col.cam_xyz[r][c];
            uf->rgb_cam[r][c] = col.rgb_cam[r][c];
        }
    }

    uf->bayer.resize((size_t)H * (size_t)W);
    const uint16_t* src = lr->imgdata.rawdata.raw_image;
    for (int r = 0; r < H; ++r) {
        std::memcpy(
            uf->bayer.data() + (size_t)r * W,
            src + (size_t)(top + r) * rw + left,
            (size_t)W * sizeof(uint16_t)
        );
    }
    capture_frame_metadata(*uf, lr->imgdata, top, left, [&](int y, int x) { return lr->COLOR(y, x); });
    uf->status = 0;
}

#ifndef _WIN32
struct UnpackBatchCtx {
    std::vector<UnpackedFrame>* uframes;
    std::atomic<int>* next_idx;
    std::atomic<int>* next_slot;
    int n_total;
};

static void* unpack_pthread_trampoline(void* arg) {
    auto* ctx = static_cast<UnpackBatchCtx*>(arg);
    const int slot = ctx->next_slot->fetch_add(1, std::memory_order_relaxed);
    g_row_pool.apply_thread_slot(slot);
    while (true) {
        int idx = ctx->next_idx->fetch_add(1, std::memory_order_relaxed);
        if (idx >= ctx->n_total) break;
        unpack_one_frame(&(*ctx->uframes)[idx]);
    }
    return nullptr;
}
#endif

// ---------------------------------------------------------------------------
// 2. Separable 2D Gaussian Blur & Median3x3 Helpers
// ---------------------------------------------------------------------------
static void sep_gaussian_2d(const float* src, float* dst, int H, int W, float sigma, int n_threads) {
    if (sigma <= 0.05f) {
        if (src != dst) std::memcpy(dst, src, (size_t)H * (size_t)W * sizeof(float));
        return;
    }
    const int rad = std::max(1, (int)std::ceil(3.0f * sigma));
    std::vector<float> kern(2 * rad + 1);
    float ksum = 0.0f;
    for (int k = -rad; k <= rad; ++k) {
        float w = std::exp(-0.5f * (float)(k * k) / (sigma * sigma));
        kern[k + rad] = w;
        ksum += w;
    }
    for (float& w : kern) w /= ksum;

    UninitBuf<float> inplace_copy;
    const float* eff_src = src;
    if (src == dst && rad <= 12) {
        inplace_copy.resize((size_t)H * (size_t)W);
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            std::memcpy(inplace_copy.data() + (size_t)r0 * W, src + (size_t)r0 * W,
                        (size_t)(r1 - r0) * W * sizeof(float));
        });
        eff_src = inplace_copy.data();
    }

    if (rad <= 12) {
        // Cache-adaptive L1/L2 2D tile fusion (57 KB/thread on Zen3 512KB L2, 114 KB on >=1MB L2):
        // eliminates full-frame intermediate buffer and halves DRAM traffic.
        const int TILE_Y = 32;
        const int TILE_X = heraw_cache::host_cache_profile().conv_tile_x();
        const int max_nt = std::max(1, n_threads);
        const int max_bh = TILE_Y + 2 * rad;
        std::vector<std::vector<float>> tls_tile(max_nt, std::vector<float>((size_t)max_bh * TILE_X));
        const float* __restrict kp = kern.data();

        parallel_for_rows(H, n_threads, [&](int r0, int r1, int tid) {
            float* __restrict tbuf = tls_tile[tid].data();
            for (int yb0 = r0; yb0 < r1; yb0 += TILE_Y) {
                const int yb1 = std::min(r1, yb0 + TILE_Y);
                const int yr0 = yb0 - rad;
                const int yr1 = yb1 + rad;
                const int th = yr1 - yr0;

                for (int xb0 = 0; xb0 < W; xb0 += TILE_X) {
                    const int xb1 = std::min(W, xb0 + TILE_X);
                    const int tw = xb1 - xb0;

                    for (int ti = 0; ti < th; ++ti) {
                        const int sr = clamp_i(yr0 + ti, 0, H - 1);
                        const float* __restrict srow = eff_src + (size_t)sr * W;
                        float* __restrict trow = tbuf + (size_t)ti * TILE_X;
                        const int c_in0 = std::max(xb0, rad);
                        const int c_in1 = std::min(xb1, W - rad);

                        for (int c = xb0; c < std::min(xb1, c_in0); ++c) {
                            float acc = 0.0f;
                            for (int k = -rad; k <= rad; ++k) acc += srow[clamp_i(c + k, 0, W - 1)] * kp[k + rad];
                            trow[c - xb0] = acc;
                        }
                        if (c_in1 > c_in0) {
                            const int off0 = c_in0 - xb0;
                            const int len = c_in1 - c_in0;
                            const float w0 = kp[0];
                            const float* __restrict s0 = srow + c_in0 - rad;
                            for (int i = 0; i < len; ++i) trow[off0 + i] = w0 * s0[i];
                            for (int k = -rad + 1; k <= rad; ++k) {
                                const float wk = kp[k + rad];
                                const float* __restrict sk = srow + c_in0 + k;
                                for (int i = 0; i < len; ++i) trow[off0 + i] += wk * sk[i];
                            }
                        }
                        for (int c = std::max(xb0, c_in1); c < xb1; ++c) {
                            float acc = 0.0f;
                            for (int k = -rad; k <= rad; ++k) acc += srow[clamp_i(c + k, 0, W - 1)] * kp[k + rad];
                            trow[c - xb0] = acc;
                        }
                    }

                    for (int r = yb0; r < yb1; ++r) {
                        float* __restrict drow = dst + (size_t)r * W + xb0;
                        const int base_ti = r - yr0;
                        const float w0 = kp[0];
                        const float* __restrict t0 = tbuf + (size_t)(base_ti - rad) * TILE_X;
                        for (int i = 0; i < tw; ++i) drow[i] = w0 * t0[i];
                        for (int k = -rad + 1; k <= rad; ++k) {
                            const float wk = kp[k + rad];
                            const float* __restrict tk = tbuf + (size_t)(base_ti + k) * TILE_X;
                            for (int i = 0; i < tw; ++i) drow[i] += wk * tk[i];
                        }
                    }
                }
            }
        }, TILE_Y);
        return;
    }

    UninitBuf<float> tmp_buf((size_t)H * (size_t)W);
    float* tmp_ptr = tmp_buf.data();

    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            const float* __restrict srow = src + (size_t)r * W;
            float* __restrict trow = tmp_ptr + (size_t)r * W;
            int left_end = std::min(W, rad);
            int right_start = std::max(left_end, W - rad);
            for (int c = 0; c < left_end; ++c) {
                float acc = 0.0f;
                for (int k = -rad; k <= rad; ++k) acc += srow[clamp_i(c + k, 0, W - 1)] * kern[k + rad];
                trow[c] = acc;
            }
            if (right_start > left_end) {
                const float w0 = kern[0];
                const float* __restrict s0 = srow - rad;
                for (int c = left_end; c < right_start; ++c) trow[c] = w0 * s0[c];
                for (int k = -rad + 1; k <= rad; ++k) {
                    const float wk = kern[k + rad];
                    const float* __restrict sk = srow + k;
                    for (int c = left_end; c < right_start; ++c) {
                        trow[c] += wk * sk[c];
                    }
                }
            }
            for (int c = right_start; c < W; ++c) {
                float acc = 0.0f;
                for (int k = -rad; k <= rad; ++k) acc += srow[clamp_i(c + k, 0, W - 1)] * kern[k + rad];
                trow[c] = acc;
            }
        }
    });

    // Cache-streaming row-wise vertical accumulation (auto-vectorizes to contiguous NEON/AVX2 FMA!)
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            float* __restrict drow = dst + (size_t)r * W;
            const float w0 = kern[0];
            const float* __restrict t0 = tmp_ptr + (size_t)clamp_i(r - rad, 0, H - 1) * W;
            for (int c = 0; c < W; ++c) drow[c] = w0 * t0[c];
            for (int k = -rad + 1; k <= rad; ++k) {
                const float wk = kern[k + rad];
                const float* __restrict tk = tmp_ptr + (size_t)clamp_i(r + k, 0, H - 1) * W;
                for (int c = 0; c < W; ++c) drow[c] += wk * tk[c];
            }
        }
    });
}

static inline float median9_vals(
    float v0, float v1, float v2,
    float v3, float v4, float v5,
    float v6, float v7, float v8
) {
    auto swp = [](float& x, float& y) {
        float lo = x < y ? x : y;
        float hi = x < y ? y : x;
        x = lo; y = hi;
    };
    swp(v1,v2); swp(v4,v5); swp(v7,v8); swp(v0,v1); swp(v3,v4); swp(v6,v7);
    swp(v1,v2); swp(v4,v5); swp(v7,v8); swp(v0,v3); swp(v5,v8); swp(v4,v7);
    swp(v3,v6); swp(v1,v4); swp(v2,v5); swp(v4,v7); swp(v4,v2); swp(v6,v4); swp(v4,v2);
    return v4;
}

#if HERAW_AVX512_ENABLED
__attribute__((always_inline, target("avx512f,avx512vl,fma")))
static inline void swp16_avx512(__m512& a, __m512& b) {
    const __m512 lo = _mm512_min_ps(a, b), hi = _mm512_max_ps(a, b);
    a = lo; b = hi;
}

__attribute__((target("avx512f,avx512vl,fma")))
static inline int median3x3_row_avx512(
    const float* __restrict rt, const float* __restrict rm, const float* __restrict rb,
    float* __restrict drow, int c_start, int W
) {
    int c = c_start;
    for (; c + 16 <= W - 1; c += 16) {
        __m512 v0 = _mm512_loadu_ps(rt + c - 1), v1 = _mm512_loadu_ps(rt + c), v2 = _mm512_loadu_ps(rt + c + 1);
        __m512 v3 = _mm512_loadu_ps(rm + c - 1), v4 = _mm512_loadu_ps(rm + c), v5 = _mm512_loadu_ps(rm + c + 1);
        __m512 v6 = _mm512_loadu_ps(rb + c - 1), v7 = _mm512_loadu_ps(rb + c), v8 = _mm512_loadu_ps(rb + c + 1);
        swp16_avx512(v1,v2); swp16_avx512(v4,v5); swp16_avx512(v7,v8); swp16_avx512(v0,v1); swp16_avx512(v3,v4); swp16_avx512(v6,v7);
        swp16_avx512(v1,v2); swp16_avx512(v4,v5); swp16_avx512(v7,v8); swp16_avx512(v0,v3); swp16_avx512(v5,v8); swp16_avx512(v4,v7);
        swp16_avx512(v3,v6); swp16_avx512(v1,v4); swp16_avx512(v2,v5); swp16_avx512(v4,v7); swp16_avx512(v4,v2); swp16_avx512(v6,v4); swp16_avx512(v4,v2);
        _mm512_storeu_ps(drow + c, v4);
    }
    return c;
}

__attribute__((target("avx512f,avx512vl,fma")))
static inline size_t rl_snr_gate_avx512(const float* __restrict lum0, float* __restrict edge_gate, size_t i0, size_t i1, float snr_knee) {
    const __m512 knee_v = _mm512_set1_ps(snr_knee);
    size_t i = i0;
    for (; i + 16 <= i1; i += 16) {
        const __m512 l0 = _mm512_loadu_ps(lum0 + i);
        _mm512_storeu_ps(edge_gate + i, _mm512_mul_ps(_mm512_div_ps(l0, _mm512_add_ps(l0, knee_v)), _mm512_loadu_ps(edge_gate + i)));
    }
    return i;
}

__attribute__((target("avx512f,avx512vl,fma")))
static inline size_t rl_ratio_avx512(
    const float* __restrict lum0, const float* __restrict conv, const float* __restrict edge_gate,
    float* __restrict ratio, size_t i0, size_t i1, float clamp_delta
) {
    const __m512 eps_v = _mm512_set1_ps(1e-6f), one_v = _mm512_set1_ps(1.0f);
    const __m512 lo_d = _mm512_set1_ps(-clamp_delta), hi_d = _mm512_set1_ps(clamp_delta);
    size_t i = i0;
    for (; i + 16 <= i1; i += 16) {
        const __m512 q = _mm512_sub_ps(_mm512_div_ps(_mm512_loadu_ps(lum0 + i), _mm512_max_ps(_mm512_loadu_ps(conv + i), eps_v)), one_v);
        const __m512 d = _mm512_min_ps(hi_d, _mm512_max_ps(lo_d, q));
        _mm512_storeu_ps(ratio + i, _mm512_fmadd_ps(d, _mm512_loadu_ps(edge_gate + i), one_v));
    }
    return i;
}

__attribute__((target("avx512f,avx512vl,fma")))
static inline size_t rl_update_avx512(
    float* __restrict est, const float* __restrict conv,
    const float* __restrict lmin_bound, const float* __restrict lmax_bound, size_t i0, size_t i1
) {
    size_t i = i0;
    for (; i + 16 <= i1; i += 16) {
        const __m512 v = _mm512_mul_ps(_mm512_loadu_ps(est + i), _mm512_loadu_ps(conv + i));
        _mm512_storeu_ps(est + i, _mm512_min_ps(_mm512_loadu_ps(lmax_bound + i), _mm512_max_ps(_mm512_loadu_ps(lmin_bound + i), v)));
    }
    return i;
}
#endif

static void median3x3_plane(const float* src, float* dst, int H, int W, int n_threads) {
    if (H <= 0 || W <= 0) return;
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            const float* __restrict rt = src + (size_t)clamp_i(r - 1, 0, H - 1) * W;
            const float* __restrict rm = src + (size_t)r * W;
            const float* __restrict rb = src + (size_t)clamp_i(r + 1, 0, H - 1) * W;
            float* __restrict drow = dst + (size_t)r * W;
            if (W == 1) {
                drow[0] = median9_vals(rt[0], rt[0], rt[0], rm[0], rm[0], rm[0], rb[0], rb[0], rb[0]);
                continue;
            }
            drow[0] = median9_vals(rt[0], rt[0], rt[1], rm[0], rm[0], rm[1], rb[0], rb[0], rb[1]);
            int c = 1;
#if defined(__aarch64__)
            auto swp4 = [](float32x4_t& a, float32x4_t& b) {
                const float32x4_t lo = vminq_f32(a, b), hi = vmaxq_f32(a, b);
                a = lo; b = hi;
            };
            for (; c + 4 <= W - 1; c += 4) {
                float32x4_t v0 = vld1q_f32(rt + c - 1), v1 = vld1q_f32(rt + c), v2 = vld1q_f32(rt + c + 1);
                float32x4_t v3 = vld1q_f32(rm + c - 1), v4 = vld1q_f32(rm + c), v5 = vld1q_f32(rm + c + 1);
                float32x4_t v6 = vld1q_f32(rb + c - 1), v7 = vld1q_f32(rb + c), v8 = vld1q_f32(rb + c + 1);
                swp4(v1,v2); swp4(v4,v5); swp4(v7,v8); swp4(v0,v1); swp4(v3,v4); swp4(v6,v7);
                swp4(v1,v2); swp4(v4,v5); swp4(v7,v8); swp4(v0,v3); swp4(v5,v8); swp4(v4,v7);
                swp4(v3,v6); swp4(v1,v4); swp4(v2,v5); swp4(v4,v7); swp4(v4,v2); swp4(v6,v4); swp4(v4,v2);
                vst1q_f32(drow + c, v4);
            }
#elif defined(__AVX2__)
#if HERAW_AVX512_ENABLED
            if (heraw_cache::use_avx512() && c + 16 <= W - 1) {
                c = median3x3_row_avx512(rt, rm, rb, drow, c, W);
            }
#endif
            auto swp8 = [](__m256& a, __m256& b) {
                const __m256 lo = _mm256_min_ps(a, b), hi = _mm256_max_ps(a, b);
                a = lo; b = hi;
            };
            for (; c + 8 <= W - 1; c += 8) {
                __m256 v0 = _mm256_loadu_ps(rt + c - 1), v1 = _mm256_loadu_ps(rt + c), v2 = _mm256_loadu_ps(rt + c + 1);
                __m256 v3 = _mm256_loadu_ps(rm + c - 1), v4 = _mm256_loadu_ps(rm + c), v5 = _mm256_loadu_ps(rm + c + 1);
                __m256 v6 = _mm256_loadu_ps(rb + c - 1), v7 = _mm256_loadu_ps(rb + c), v8 = _mm256_loadu_ps(rb + c + 1);
                swp8(v1,v2); swp8(v4,v5); swp8(v7,v8); swp8(v0,v1); swp8(v3,v4); swp8(v6,v7);
                swp8(v1,v2); swp8(v4,v5); swp8(v7,v8); swp8(v0,v3); swp8(v5,v8); swp8(v4,v7);
                swp8(v3,v6); swp8(v1,v4); swp8(v2,v5); swp8(v4,v7); swp8(v4,v2); swp8(v6,v4); swp8(v4,v2);
                _mm256_storeu_ps(drow + c, v4);
            }
#endif
            for (; c < W - 1; ++c) {
                drow[c] = median9_vals(
                    rt[c - 1], rt[c], rt[c + 1],
                    rm[c - 1], rm[c], rm[c + 1],
                    rb[c - 1], rb[c], rb[c + 1]
                );
            }
            drow[W - 1] = median9_vals(
                rt[W - 2], rt[W - 1], rt[W - 1],
                rm[W - 2], rm[W - 1], rm[W - 1],
                rb[W - 2], rb[W - 1], rb[W - 1]
            );
        }
    });
}

// Empirical white-noise std via lag-L Donoho-Johnstone HH wavelet MAD
// Using lag >= correlation length avoids underestimation on interpolated/gathered grids.
static float estimate_hh_sigma_lag(const float* img, int H, int W, int lag = 1, int stride_step = 2) {
    lag = std::max(1, lag);
    std::vector<float> diffs;
    diffs.reserve((size_t)(H / stride_step) * (size_t)(W / stride_step));
    for (int r = 0; r + lag < H; r += stride_step) {
        const float* r0 = img + (size_t)r * W;
        const float* r1 = img + (size_t)(r + lag) * W;
        for (int c = 0; c + lag < W; c += stride_step) {
            float hh = 0.5f * (r0[c] - r0[c + lag] - r1[c] + r1[c + lag]);
            diffs.push_back(std::fabs(hh));
        }
    }
    return quick_percentile(diffs, 0.50f) / 0.67448975f;
}

static inline float estimate_hh_sigma(const float* img, int H, int W, int stride_step = 2) {
    return estimate_hh_sigma_lag(img, H, W, 1, stride_step);
}

static inline float fast_neg_exp(float x) {
    // Accurate Pade/Taylor rational approximation to exp(-x) for x >= 0 (max abs error < 2e-3, SIMD friendly)
    if (x <= 0.0f) return 1.0f;
    if (x >= 12.0f) return 0.0f;
    float x2 = x * x;
    return 1.0f / (1.0f + x + 0.48f * x2 + 0.235f * x * x2);
}

// ---------------------------------------------------------------------------
// 3. Linear Algebra Solver (Small Dense Least Squares via Householder QR)
// ---------------------------------------------------------------------------
static bool solve_least_squares(int m, int n, std::vector<double>& A, std::vector<double>& b, std::vector<double>& x_out) {
    // A is m x n row-major, m >= n
    x_out.assign(n, 0.0);
    if (m < n) return false;
    std::vector<double> R = A;
    std::vector<double> rhs = b;

    for (int k = 0; k < n; ++k) {
        double norm2 = 0.0;
        for (int i = k; i < m; ++i) norm2 += R[i * n + k] * R[i * n + k];
        double norm = std::sqrt(norm2);
        if (norm < 1e-14) return false;
        double alpha = (R[k * n + k] >= 0.0) ? -norm : norm;
        double v0 = R[k * n + k] - alpha;
        double vnorm2 = v0 * v0;
        for (int i = k + 1; i < m; ++i) vnorm2 += R[i * n + k] * R[i * n + k];
        if (vnorm2 < 1e-28) continue;

        R[k * n + k] = v0;
        for (int j = k + 1; j < n; ++j) {
            double dot = 0.0;
            for (int i = k; i < m; ++i) dot += R[i * n + k] * R[i * n + j];
            double scale = 2.0 * dot / vnorm2;
            for (int i = k; i < m; ++i) R[i * n + j] -= scale * R[i * n + k];
        }
        double dot_b = 0.0;
        for (int i = k; i < m; ++i) dot_b += R[i * n + k] * rhs[i];
        double scale_b = 2.0 * dot_b / vnorm2;
        for (int i = k; i < m; ++i) rhs[i] -= scale_b * R[i * n + k];
        R[k * n + k] = alpha;
        for (int i = k + 1; i < m; ++i) R[i * n + k] = 0.0;
    }

    for (int k = n - 1; k >= 0; --k) {
        double s = rhs[k];
        for (int j = k + 1; j < n; ++j) s -= R[k * n + j] * x_out[j];
        if (std::fabs(R[k * n + k]) < 1e-14) return false;
        x_out[k] = s / R[k * n + k];
    }
    return true;
}

static bool invert_3x3(const double M[3][3], double inv[3][3]) {
    double det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
               - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
               + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    if (std::fabs(det) < 1e-12) return false;
    double id = 1.0 / det;
    inv[0][0] =  (M[1][1] * M[2][2] - M[1][2] * M[2][1]) * id;
    inv[0][1] = -(M[0][1] * M[2][2] - M[0][2] * M[2][1]) * id;
    inv[0][2] =  (M[0][1] * M[1][2] - M[0][2] * M[1][1]) * id;
    inv[1][0] = -(M[1][0] * M[2][2] - M[1][2] * M[2][0]) * id;
    inv[1][1] =  (M[0][0] * M[2][2] - M[0][2] * M[2][0]) * id;
    inv[1][2] = -(M[0][0] * M[1][2] - M[0][2] * M[1][0]) * id;
    inv[2][0] =  (M[1][0] * M[2][1] - M[1][1] * M[2][0]) * id;
    inv[2][1] = -(M[0][0] * M[2][1] - M[0][1] * M[2][0]) * id;
    inv[2][2] =  (M[0][0] * M[1][1] - M[0][1] * M[1][0]) * id;
    return true;
}

// ---------------------------------------------------------------------------
// 4. Coarse-to-Fine 3-Level Pyramid Block-ZNCC Homography + Polynomial Polish
// ---------------------------------------------------------------------------
static void box_downsample_2x(const float* src, int H, int W, float* dst, int n_threads) {
    int H2 = H / 2, W2 = W / 2;
    parallel_for_rows(H2, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            const float* s0 = src + (size_t)(2 * r) * W;
            const float* s1 = s0 + W;
            float* d = dst + (size_t)r * W2;
            for (int c = 0; c < W2; ++c) {
                d[c] = 0.25f * (s0[2 * c] + s0[2 * c + 1] + s1[2 * c] + s1[2 * c + 1]);
            }
        }
    });
}

static inline float bilerp_clamped(const float* img, int H, int W, float y, float x) {
    if (H < 2 || W < 2) return img[0];
    y = clamp_f(y, 0.0f, (float)(H - 1));
    x = clamp_f(x, 0.0f, (float)(W - 1));
    int y0 = clamp_i((int)y, 0, H - 2);
    int x0 = clamp_i((int)x, 0, W - 2);
    float fy = clamp_f(y - (float)y0, 0.0f, 1.0f);
    float fx = clamp_f(x - (float)x0, 0.0f, 1.0f);
    const float* r0 = img + (size_t)y0 * W + x0;
    const float* r1 = r0 + W;
    return (1.0f - fy) * ((1.0f - fx) * r0[0] + fx * r0[1]) +
           fy * ((1.0f - fx) * r1[0] + fx * r1[1]);
}

static float sample_cfa_channel(const float* raw, int H, int W, int y, int x,
                                int channel, const uint8_t cfa[6][6]) {
    y = clamp_i(y, 0, H - 1);
    x = clamp_i(x, 0, W - 1);
    if (cfa[y % 6][x % 6] == channel) return raw[(size_t)y * W + x];
    float sum = 0.0f, weights = 0.0f;
    for (int yy = std::max(0, y - 3); yy <= std::min(H - 1, y + 3); ++yy)
        for (int xx = std::max(0, x - 3); xx <= std::min(W - 1, x + 3); ++xx) {
            if (cfa[yy % 6][xx % 6] != channel) continue;
            float w = 1.0f / (float)((yy-y)*(yy-y) + (xx-x)*(xx-x));
            sum += w * raw[(size_t)yy * W + xx];
            weights += w;
        }
    return weights > 0.0f ? sum / weights : 0.0f;
}

struct BlockMatch {
    double x0, y0;
    double x1, y1;
    double ncc;
};

struct CurIntegral {
    int H = 0, W = 0;
    UninitBuf<double> sat1; // (H+1)*(W+1) sum(cur)
    UninitBuf<double> sat2; // (H+1)*(W+1) sum(cur^2)

    void build(const float* cur, int h, int w, int n_threads = 1) {
        H = h; W = w;
        const int S = W + 1;
        const size_t total = (size_t)(H + 1) * (size_t)S;
        sat1.resize(total);
        sat2.resize(total);
        std::memset(sat1.data(), 0, (size_t)S * sizeof(double));
        std::memset(sat2.data(), 0, (size_t)S * sizeof(double));
        if (n_threads <= 1 || (size_t)H * (size_t)W < 262144) {
            for (int r = 0; r < H; ++r) {
                double r1 = 0.0, r2 = 0.0;
                const float* crow = cur + (size_t)r * W;
                const double* p1 = sat1.data() + (size_t)r * S;
                const double* p2 = sat2.data() + (size_t)r * S;
                double* d1 = sat1.data() + (size_t)(r + 1) * S;
                double* d2 = sat2.data() + (size_t)(r + 1) * S;
                d1[0] = 0.0;
                d2[0] = 0.0;
                for (int c = 0; c < W; ++c) {
                    double v = (double)crow[c];
                    r1 += v;
                    r2 += v * v;
                    d1[c + 1] = p1[c + 1] + r1;
                    d2[c + 1] = p2[c + 1] + r2;
                }
            }
            return;
        }
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                double r1 = 0.0, r2 = 0.0;
                const float* __restrict crow = cur + (size_t)r * W;
                double* __restrict d1 = sat1.data() + (size_t)(r + 1) * S;
                double* __restrict d2 = sat2.data() + (size_t)(r + 1) * S;
                d1[0] = 0.0;
                d2[0] = 0.0;
                for (int c = 0; c < W; ++c) {
                    double v = (double)crow[c];
                    r1 += v;
                    r2 += v * v;
                    d1[c + 1] = r1;
                    d2[c + 1] = r2;
                }
            }
        });
        parallel_for_rows(W, n_threads, [&](int c0, int c1, int) {
            for (int r = 1; r <= H; ++r) {
                const double* __restrict p1 = sat1.data() + (size_t)(r - 1) * S;
                const double* __restrict p2 = sat2.data() + (size_t)(r - 1) * S;
                double* __restrict d1 = sat1.data() + (size_t)r * S;
                double* __restrict d2 = sat2.data() + (size_t)r * S;
                for (int c = c0; c < c1; ++c) {
                    d1[c + 1] += p1[c + 1];
                    d2[c + 1] += p2[c + 1];
                }
            }
        }, 64);
    }

    inline void box_stats(int y0, int x0, int bh, int bw, double& sum1, double& sum2) const {
        const int S = W + 1;
        const size_t tl = (size_t)y0 * S + x0;
        const size_t tr = tl + bw;
        const size_t bl = (size_t)(y0 + bh) * S + x0;
        const size_t br = bl + bw;
        sum1 = sat1[br] - sat1[tr] - sat1[bl] + sat1[tl];
        sum2 = sat2[br] - sat2[tr] - sat2[bl] + sat2[tl];
    }
};

static inline double dot_f32_row(const float* a, const float* b, int n) {
    int c = 0;
    double lanes[4] = {};
#if defined(__aarch64__)
    float64x2_t acc0 = vdupq_n_f64(0.0);
    float64x2_t acc1 = vdupq_n_f64(0.0);
    for (; c + 4 <= n; c += 4) {
        float32x4_t av = vld1q_f32(a+c), bv = vld1q_f32(b+c);
        acc0 = vfmaq_f64(acc0,vcvt_f64_f32(vget_low_f32(av)),vcvt_f64_f32(vget_low_f32(bv)));
        acc1 = vfmaq_f64(acc1,vcvt_f64_f32(vget_high_f32(av)),vcvt_f64_f32(vget_high_f32(bv)));
    }
    vst1q_f64(lanes,acc0); vst1q_f64(lanes+2,acc1);
#elif defined(__AVX2__)
    __m256d acc = _mm256_setzero_pd();
    for (; c + 4 <= n; c += 4) {
        acc = _mm256_fmadd_pd(_mm256_cvtps_pd(_mm_loadu_ps(a+c)),
                              _mm256_cvtps_pd(_mm_loadu_ps(b+c)),acc);
    }
    _mm256_storeu_pd(lanes,acc);
#else
    for (; c + 4 <= n; c += 4)
        for (int k = 0; k < 4; ++k) lanes[k] += double(a[c+k])*double(b[c+k]);
#endif
    double s = (lanes[0]+lanes[1])+(lanes[2]+lanes[3]);
    for (; c < n; ++c) s += double(a[c])*double(b[c]);
    return s;
}

static bool match_zncc_block(
    const float* ref, const float* cur, const CurIntegral* cur_sat, int H, int W,
    int by, int bx, int bh, int bw,
    int init_dy, int init_dx, int rad, int step,
    BlockMatch& out
) {
    if (by < 0 || bx < 0 || by + bh > H || bx + bw > W) return false;
    const int N = bh * bw;
    static thread_local std::vector<float> tls_a0, tls_gx, tls_gy, tls_res;
    if ((int)tls_a0.size() < N) tls_a0.resize(N);

    double sum_a = 0.0;
    for (int r = 0; r < bh; ++r) {
        const float* row = ref + (size_t)(by + r) * W + bx;
        for (int c = 0; c < bw; ++c) sum_a += row[c];
    }
    const float mean_a = (float)(sum_a / (double)N);
    double norm_a2 = 0.0;
    for (int r = 0; r < bh; ++r) {
        const float* row = ref + (size_t)(by + r) * W + bx;
        float* arow = tls_a0.data() + r * bw;
        for (int c = 0; c < bw; ++c) {
            float d = row[c] - mean_a;
            arow[c] = d;
            norm_a2 += (double)d * (double)d;
        }
    }
    if (norm_a2 < 1e-10) return false;
    const double norm_a = std::sqrt(norm_a2);

    // Since sum(a0) == 0, sum(a0 * (b - mean_b)) == sum(a0 * b)!
    // With O(1) SAT lookup for norm_b2 = sum(b^2) - (sum b)^2 / N, ZNCC reduces to 1 NEON dot product!
    auto eval_zncc = [&](int ty, int tx) -> double {
        int y0 = by + ty, x0 = bx + tx;
        if (y0 < 0 || x0 < 0 || y0 + bh > H || x0 + bw > W) return -2.0;
        double sum_b = 0.0, sum_b2 = 0.0;
        cur_sat->box_stats(y0, x0, bh, bw, sum_b, sum_b2);
        double norm_b2 = sum_b2 - (sum_b * sum_b) / (double)N;
        if (norm_b2 < 1e-10) return -2.0;
        double dot = 0.0;
        for (int r = 0; r < bh; ++r) {
            dot += (double)dot_f32_row(tls_a0.data() + r * bw, cur + (size_t)(y0 + r) * W + x0, bw);
        }
        return dot / (norm_a * std::sqrt(norm_b2) + 1e-12);
    };

    double best_v = -2.0;
    int best_ty = init_dy, best_tx = init_dx;
    for (int ty = init_dy - rad; ty <= init_dy + rad; ty += step) {
        for (int tx = init_dx - rad; tx <= init_dx + rad; tx += step) {
            double v = eval_zncc(ty, tx);
            if (v > best_v) {
                best_v = v;
                best_ty = ty;
                best_tx = tx;
            }
        }
    }
    if (step > 1) {
        int c_ty = best_ty, c_tx = best_tx;
        for (int ty = c_ty - step; ty <= c_ty + step; ++ty) {
            for (int tx = c_tx - step; tx <= c_tx + step; ++tx) {
                double v = eval_zncc(ty, tx);
                if (v > best_v) {
                    best_v = v;
                    best_ty = ty;
                    best_tx = tx;
                }
            }
        }
    }
    if (best_v < 0.25) return false;

    double nxm = eval_zncc(best_ty, best_tx - 1);
    double nxp = eval_zncc(best_ty, best_tx + 1);
    double nym = eval_zncc(best_ty - 1, best_tx);
    double nyp = eval_zncc(best_ty + 1, best_tx);

    double ddx = 0.0, ddy = 0.0;
    if (nxm > -1.5 && nxp > -1.5) {
        double denom = 2.0 * (nxm - 2.0 * best_v + nxp);
        if (std::fabs(denom) > 1e-9) ddx = clamp_f((float)((nxm - nxp) / denom), -0.6f, 0.6f);
    }
    if (nym > -1.5 && nyp > -1.5) {
        double denom = 2.0 * (nym - 2.0 * best_v + nyp);
        if (std::fabs(denom) > 1e-9) ddy = clamp_f((float)((nym - nyp) / denom), -0.6f, 0.6f);
    }

    out.x0 = (double)bx + 0.5 * (double)bw;
    out.y0 = (double)by + 0.5 * (double)bh;
    double cur_dx = (double)best_tx + ddx;
    double cur_dy = (double)best_ty + ddy;

    if (bw >= 12 && bh >= 12) {
        double h00 = 0.0, h01 = 0.0, h11 = 0.0, sum_gx = 0.0, sum_gy = 0.0;
        const int cw_in = bw - 2;
        const int n_in = (bh - 2) * cw_in;
        if ((int)tls_gx.size() < n_in) { tls_gx.resize(n_in); tls_gy.resize(n_in); }
        for (int r = 1; r < bh - 1; ++r) {
            const float* rm = ref + (size_t)(by + r - 1) * W + bx;
            const float* rc = ref + (size_t)(by + r)     * W + bx;
            const float* rp = ref + (size_t)(by + r + 1) * W + bx;
            for (int c = 1; c < bw - 1; ++c) {
                float gx = 0.5f * (rc[c + 1] - rc[c - 1]);
                float gy = 0.5f * (rp[c] - rm[c]);
                size_t k = (size_t)(r - 1) * cw_in + (c - 1);
                tls_gx[k] = gx; tls_gy[k] = gy;
                sum_gx += gx; sum_gy += gy;
                h00 += (double)gx * gx;
                h01 += (double)gx * gy;
                h11 += (double)gy * gy;
            }
        }
        double det = h00 * h11 - h01 * h01;
        if (det > 1e-10) {
            double ih00 = h11 / det, ih01 = -h01 / det, ih11 = h00 / det;
            double lk_dx = cur_dx, lk_dy = cur_dy;
            bool lk_valid = true;
            for (int it = 0; it < 5; ++it) {
                if (by + 1 + lk_dy < 0.0 || by + bh - 2 + lk_dy >= (double)(H - 1) ||
                    bx + 1 + lk_dx < 0.0 || bx + bw - 2 + lk_dx >= (double)(W - 1)) {
                    lk_valid = false;
                    break;
                }
                const float yq_base = (float)((double)(by + 1) + lk_dy);
                const float xq_base = (float)((double)(bx + 1) + lk_dx);
                const int y0_base = clamp_i((int)yq_base, 0, H - bh + 1);
                const int x0_base = clamp_i((int)xq_base, 0, W - bw + 1);
                const float fy = clamp_f(yq_base - (float)y0_base, 0.0f, 1.0f), fy0 = 1.0f - fy;
                const float fx = clamp_f(xq_base - (float)x0_base, 0.0f, 1.0f), fx0 = 1.0f - fx;
                const float w00 = fy0 * fx0, w01 = fy0 * fx, w10 = fy * fx0, w11 = fy * fx;
                double sum_r = 0.0, b0 = 0.0, b1 = 0.0;
                for (int r = 1; r < bh - 1; ++r) {
                    const float* __restrict rc = ref + (size_t)(by + r) * W + bx + 1;
                    const float* __restrict cr0 = cur + (size_t)(y0_base + r - 1) * W + x0_base;
                    const float* __restrict cr1 = cr0 + W;
                    const float* __restrict gxp = tls_gx.data() + (size_t)(r - 1) * cw_in;
                    const float* __restrict gyp = tls_gy.data() + (size_t)(r - 1) * cw_in;
                    int j = 0;
#if defined(__aarch64__)
                    const float32x4_t vw00 = vdupq_n_f32(w00), vw01 = vdupq_n_f32(w01);
                    const float32x4_t vw10 = vdupq_n_f32(w10), vw11 = vdupq_n_f32(w11);
                    float32x4_t sr_v = vdupq_n_f32(0.0f), b0_v = vdupq_n_f32(0.0f), b1_v = vdupq_n_f32(0.0f);
                    for (; j + 4 <= cw_in; j += 4) {
                        const float32x4_t cv = vfmaq_f32(
                            vfmaq_f32(vmulq_f32(vw00, vld1q_f32(cr0 + j)), vw01, vld1q_f32(cr0 + j + 1)),
                            vw10, vld1q_f32(cr1 + j));
                        const float32x4_t diff = vsubq_f32(vfmaq_f32(cv, vw11, vld1q_f32(cr1 + j + 1)), vld1q_f32(rc + j));
                        sr_v = vaddq_f32(sr_v, diff);
                        b0_v = vfmaq_f32(b0_v, vld1q_f32(gxp + j), diff);
                        b1_v = vfmaq_f32(b1_v, vld1q_f32(gyp + j), diff);
                    }
                    sum_r += (double)vaddvq_f32(sr_v);
                    b0 += (double)vaddvq_f32(b0_v);
                    b1 += (double)vaddvq_f32(b1_v);
#elif defined(__AVX2__)
                    const __m256 vw00 = _mm256_set1_ps(w00), vw01 = _mm256_set1_ps(w01);
                    const __m256 vw10 = _mm256_set1_ps(w10), vw11 = _mm256_set1_ps(w11);
                    __m256 sr_v = _mm256_setzero_ps(), b0_v = _mm256_setzero_ps(), b1_v = _mm256_setzero_ps();
                    for (; j + 8 <= cw_in; j += 8) {
                        const __m256 cv = _mm256_fmadd_ps(vw11, _mm256_loadu_ps(cr1 + j + 1),
                            _mm256_fmadd_ps(vw10, _mm256_loadu_ps(cr1 + j),
                            _mm256_fmadd_ps(vw01, _mm256_loadu_ps(cr0 + j + 1), _mm256_mul_ps(vw00, _mm256_loadu_ps(cr0 + j)))));
                        const __m256 diff = _mm256_sub_ps(cv, _mm256_loadu_ps(rc + j));
                        sr_v = _mm256_add_ps(sr_v, diff);
                        b0_v = _mm256_fmadd_ps(_mm256_loadu_ps(gxp + j), diff, b0_v);
                        b1_v = _mm256_fmadd_ps(_mm256_loadu_ps(gyp + j), diff, b1_v);
                    }
                    auto hsum8 = [](__m256 v) -> float {
                        __m128 s = _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1));
                        s = _mm_hadd_ps(s, s); s = _mm_hadd_ps(s, s);
                        return _mm_cvtss_f32(s);
                    };
                    sum_r += (double)hsum8(sr_v);
                    b0 += (double)hsum8(b0_v);
                    b1 += (double)hsum8(b1_v);
#endif
                    for (; j < cw_in; ++j) {
                        const float cv = w00 * cr0[j] + w01 * cr0[j + 1] + w10 * cr1[j] + w11 * cr1[j + 1];
                        const float diff = cv - rc[j];
                        sum_r += diff;
                        b0 += (double)(gxp[j] * diff);
                        b1 += (double)(gyp[j] * diff);
                    }
                }
                const double mean_r = sum_r / (double)n_in;
                b0 -= mean_r * sum_gx;
                b1 -= mean_r * sum_gy;
                double step_x = -(ih00 * b0 + ih01 * b1);
                double step_y = -(ih01 * b0 + ih11 * b1);
                lk_dx += clamp_f((float)step_x, -0.5f, 0.5f);
                lk_dy += clamp_f((float)step_y, -0.5f, 0.5f);
                if (std::hypot(step_x, step_y) < 2e-4) break;
            }
            if (lk_valid && std::hypot(lk_dx - cur_dx, lk_dy - cur_dy) < 1.0) {
                cur_dx = lk_dx;
                cur_dy = lk_dy;
            }
        }
    }

    out.x1 = out.x0 + cur_dx;
    out.y1 = out.y0 + cur_dy;
    out.ncc = best_v;
    return true;
}

static bool fit_homography_irls(const std::vector<BlockMatch>& pts, int H, int W, double Hm[3][3], double& inlier_frac) {
    if (pts.size() < 6) return false;
    // Normalize coordinates to [-1, 1] for optimal numerical conditioning!
    const double sx = 2.0 / (double)W;
    const double sy = 2.0 / (double)H;
    const size_t N = pts.size();
    std::vector<double> w(N, 1.0);
    for (size_t i = 0; i < N; ++i) w[i] = std::max(0.1, pts[i].ncc);

    double Hn[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    std::vector<double> err(N, 0.0);

    for (int iter = 0; iter < 5; ++iter) {
        std::vector<double> A;
        std::vector<double> b;
        A.reserve(2 * N * 8);
        b.reserve(2 * N);
        int active = 0;
        for (size_t i = 0; i < N; ++i) {
            if (w[i] <= 1e-6) continue;
            ++active;
            double x = pts[i].x0 * sx - 1.0;
            double y = pts[i].y0 * sy - 1.0;
            double u = pts[i].x1 * sx - 1.0;
            double v = pts[i].y1 * sy - 1.0;
            double wi = w[i];
            double r1[8] = {wi*x, wi*y, wi*1.0, 0, 0, 0, -wi*u*x, -wi*u*y};
            double r2[8] = {0, 0, 0, wi*x, wi*y, wi*1.0, -wi*v*x, -wi*v*y};
            A.insert(A.end(), r1, r1 + 8);
            b.push_back(wi * u);
            A.insert(A.end(), r2, r2 + 8);
            b.push_back(wi * v);
        }
        if (active < 5) return false;
        std::vector<double> h;
        if (!solve_least_squares((int)b.size(), 8, A, b, h)) return false;
        Hn[0][0] = h[0]; Hn[0][1] = h[1]; Hn[0][2] = h[2];
        Hn[1][0] = h[3]; Hn[1][1] = h[4]; Hn[1][2] = h[5];
        Hn[2][0] = h[6]; Hn[2][1] = h[7]; Hn[2][2] = 1.0;

        // Compute pixel residuals
        std::vector<float> ef(N);
        for (size_t i = 0; i < N; ++i) {
            double x = pts[i].x0 * sx - 1.0;
            double y = pts[i].y0 * sy - 1.0;
            double den = Hn[2][0] * x + Hn[2][1] * y + 1.0;
            double up = (Hn[0][0] * x + Hn[0][1] * y + Hn[0][2]) / den;
            double vp = (Hn[1][0] * x + Hn[1][1] * y + Hn[1][2]) / den;
            double px_u = (up + 1.0) / sx;
            double px_v = (vp + 1.0) / sy;
            err[i] = std::hypot(px_u - pts[i].x1, px_v - pts[i].y1);
            ef[i] = (float)err[i];
        }
        double med = std::max(0.15, (double)quick_percentile(ef, 0.50f) * 1.4826);
        for (size_t i = 0; i < N; ++i) {
            if (err[i] > 4.0 * med) w[i] = 0.0;
            else if (err[i] > 2.0 * med) {
                double q = (2.0 * med) / err[i];
                w[i] = pts[i].ncc * q * q;
            } else {
                w[i] = pts[i].ncc;
            }
        }
    }

    // Unnormalize: X_norm = T * X_px, X_px' = T^-1 * Hn * T * X_px
    double T[3][3] = {{sx, 0, -1.0}, {0, sy, -1.0}, {0, 0, 1.0}};
    double Ti[3][3] = {{1.0 / sx, 0, 1.0 / sx}, {0, 1.0 / sy, 1.0 / sy}, {0, 0, 1.0}};
    double tmp[3][3] = {{0}};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k)
                tmp[r][c] += Ti[r][k] * Hn[k][c];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            Hm[r][c] = 0.0;
            for (int k = 0; k < 3; ++k) Hm[r][c] += tmp[r][k] * T[k][c];
        }
    double s22 = Hm[2][2];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) Hm[r][c] /= s22;

    int kept = 0;
    for (size_t i = 0; i < N; ++i) if (w[i] > 1e-4) ++kept;
    inlier_frac = (double)kept / (double)N;
    return kept >= 5;
}

static void align_frame_pyramid(
    const float* g0_pre, const float* gi_pre, int Hh, int Wh,
    std::vector<float>& fy_out, std::vector<float>& fx_out,
    double& out_quality, int n_threads, bool allow_local_tiles = true,
    const float* pre_r1 = nullptr, const float* pre_r2 = nullptr,
    CurIntegral* l0_sat_out = nullptr, CurIntegral* tmp_sat_ws = nullptr
) {
    fy_out.assign((size_t)Hh * Wh, 0.0f);
    fx_out.assign((size_t)Hh * Wh, 0.0f);

    // Build 3-level pyramid: L0 (Hh x Wh), L1 (Hh/2 x Wh/2), L2 (Hh/4 x Wh/4)
    int H1p = Hh / 2, W1p = Wh / 2;
    int H2p = H1p / 2, W2p = W1p / 2;
    std::vector<float> r1_own, r2_own;
    const float* r1_p = pre_r1;
    const float* r2_p = pre_r2;
    if (!r1_p || !r2_p) {
        r1_own.resize((size_t)H1p * W1p);
        r2_own.resize((size_t)H2p * W2p);
        box_downsample_2x(g0_pre, Hh, Wh, r1_own.data(), n_threads);
        box_downsample_2x(r1_own.data(), H1p, W1p, r2_own.data(), n_threads);
        r1_p = r1_own.data();
        r2_p = r2_own.data();
    }
    std::vector<float> c1((size_t)H1p * W1p), c2((size_t)H2p * W2p);
    box_downsample_2x(gi_pre, Hh, Wh, c1.data(), n_threads);
    box_downsample_2x(c1.data(), H1p, W1p, c2.data(), n_threads);

    double Hm[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    out_quality = 0.0;

    struct PyrSpec { const float* ref; const float* cur; int H, W, nby, nbx, rad, step; double scale_from_prev; };
    PyrSpec specs[3] = {
        {r2_p,   c2.data(), H2p, W2p, 5, 7, 24, 2, 1.0},
        {r1_p,   c1.data(), H1p, W1p, 6, 9, 5,  1, 2.0},
        {g0_pre, gi_pre,    Hh,  Wh,  8, 11, 4, 1, 2.0},
    };

    CurIntegral sat_l1_local, sat_l0_local, sat_tmp_local;
    CurIntegral& sat_l1 = tmp_sat_ws ? *tmp_sat_ws : sat_tmp_local;
    CurIntegral& sat_l0 = l0_sat_out ? *l0_sat_out : sat_l0_local;
    for (int lv = 0; lv < 3; ++lv) {
        const auto& sp = specs[lv];
        if (sp.scale_from_prev != 1.0) {
            double s = sp.scale_from_prev;
            Hm[0][2] *= s; Hm[1][2] *= s;
            Hm[2][0] /= s; Hm[2][1] /= s;
        }
        if (sp.H < 96 || sp.W < 96) continue;
        CurIntegral& cur_sat = (lv == 2) ? sat_l0 : ((lv == 1) ? sat_l1 : sat_l1_local);
        cur_sat.build(sp.cur, sp.H, sp.W, n_threads);
        int margin = std::min(16, sp.H / 10);
        int bh = std::max(24, (sp.H - 2 * margin) / sp.nby);
        int bw = std::max(24, (sp.W - 2 * margin) / sp.nbx);
        const int n_blocks = sp.nby * sp.nbx;
        std::vector<BlockMatch> matches;
        std::vector<BlockMatch> thread_matches(n_blocks);
        std::vector<uint8_t> thread_ok(n_blocks, 0);

        parallel_for_rows(n_blocks, n_threads, [&](int bi0, int bi1, int) {
            for (int bi = bi0; bi < bi1; ++bi) {
                const int iy = bi / sp.nbx;
                const int ix = bi % sp.nbx;
                int by = margin + iy * std::max(0, (sp.H - 2 * margin - bh)) / std::max(1, sp.nby - 1);
                int bx = margin + ix * std::max(0, (sp.W - 2 * margin - bw)) / std::max(1, sp.nbx - 1);
                double cx = (double)bx + 0.5 * bw;
                double cy = (double)by + 0.5 * bh;
                double den = Hm[2][0] * cx + Hm[2][1] * cy + Hm[2][2];
                int pred_dx = (int)std::lrint((Hm[0][0] * cx + Hm[0][1] * cy + Hm[0][2]) / den - cx);
                int pred_dy = (int)std::lrint((Hm[1][0] * cx + Hm[1][1] * cy + Hm[1][2]) / den - cy);
                int eff_rad = std::min(sp.rad, std::min(sp.H, sp.W) / 4);
                BlockMatch bm{};
                if (match_zncc_block(sp.ref, sp.cur, &cur_sat, sp.H, sp.W, by, bx, bh, bw, pred_dy, pred_dx, eff_rad, sp.step, bm)) {
                    thread_matches[bi] = bm;
                    thread_ok[bi] = 1;
                }
            }
        }, 1);
        for (int k = 0; k < n_blocks; ++k) {
            if (thread_ok[k]) matches.push_back(thread_matches[k]);
        }
        double H_cand[3][3];
        double q = 0.0;
        if (fit_homography_irls(matches, sp.H, sp.W, H_cand, q)) {
            std::memcpy(Hm, H_cand, sizeof(Hm));
            out_quality = q;
        }

        if (lv == 2 && matches.size() >= 16) {
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    for (int c = 0; c < Wh; ++c) {
                        double den = Hm[2][0] * c + Hm[2][1] * r + Hm[2][2];
                        fx_out[(size_t)r * Wh + c] = (float)((Hm[0][0] * c + Hm[0][1] * r + Hm[0][2]) / den - (double)c);
                        fy_out[(size_t)r * Wh + c] = (float)((Hm[1][0] * c + Hm[1][1] * r + Hm[1][2]) / den - (double)r);
                    }
                }
            });

            if (!allow_local_tiles) {
                auto compute_mae = [&](const std::vector<float>& fy_t, const std::vector<float>& fx_t) -> double {
                    int bdr = std::min(128, std::min(Hh, Wh) / 8);
                    int rows_eval = std::max(1, (Hh - 2 * bdr + 1) / 2);
                    std::vector<double> rsum(n_threads, 0.0);
                    std::vector<int64_t> rcnt(n_threads, 0);
                    parallel_for_rows(rows_eval, n_threads, [&](int ri0, int ri1, int tid) {
                        double acc = 0.0;
                        int64_t cnt = 0;
                        for (int ri = ri0; ri < ri1; ++ri) {
                            int r = bdr + 2 * ri;
                            for (int c = bdr; c < Wh - bdr; c += 2) {
                                size_t idx = (size_t)r * Wh + c;
                                float wv = bilerp_clamped(gi_pre, Hh, Wh, (float)r + fy_t[idx], (float)c + fx_t[idx]);
                                acc += std::fabs((double)wv - (double)g0_pre[idx]);
                                ++cnt;
                            }
                        }
                        rsum[tid] += acc; rcnt[tid] += cnt;
                    });
                    double tot = 0.0; int64_t n = 0;
                    for (int t = 0; t < n_threads; ++t) { tot += rsum[t]; n += rcnt[t]; }
                    return tot / (double)std::max<int64_t>(1, n);
                };

                double mae_hom = compute_mae(fy_out, fx_out);
                const size_t nm = matches.size();
                std::vector<double> rx(nm), ry(nm), w_p(nm);
                for (size_t i = 0; i < nm; ++i) {
                    double den = Hm[2][0] * matches[i].x0 + Hm[2][1] * matches[i].y0 + Hm[2][2];
                    double hx = (Hm[0][0] * matches[i].x0 + Hm[0][1] * matches[i].y0 + Hm[0][2]) / den;
                    double hy = (Hm[1][0] * matches[i].x0 + Hm[1][1] * matches[i].y0 + Hm[1][2]) / den;
                    rx[i] = matches[i].x1 - hx;
                    ry[i] = matches[i].y1 - hy;
                    w_p[i] = matches[i].ncc;
                }
                std::vector<double> cx_c, cy_c;
                bool poly_ok = false;
                for (int it = 0; it < 3; ++it) {
                    std::vector<double> A, bx_v, by_v;
                    for (size_t i = 0; i < nm; ++i) {
                        if (w_p[i] <= 1e-5) continue;
                        double xn = matches[i].x0 / (0.5 * Wh) - 1.0;
                        double yn = matches[i].y0 / (0.5 * Hh) - 1.0;
                        double wi = w_p[i];
                        double row[6] = {wi, wi * xn, wi * yn, wi * xn * xn, wi * xn * yn, wi * yn * yn};
                        A.insert(A.end(), row, row + 6);
                        bx_v.push_back(wi * rx[i]);
                        by_v.push_back(wi * ry[i]);
                    }
                    if ((int)bx_v.size() < 10) { poly_ok = false; break; }
                    std::vector<double> A_copy = A;
                    if (!solve_least_squares((int)bx_v.size(), 6, A, bx_v, cx_c)) { poly_ok = false; break; }
                    if (!solve_least_squares((int)by_v.size(), 6, A_copy, by_v, cy_c)) { poly_ok = false; break; }
                    poly_ok = true;
                    std::vector<float> rmag(nm);
                    for (size_t i = 0; i < nm; ++i) {
                        double xn = matches[i].x0 / (0.5 * Wh) - 1.0;
                        double yn = matches[i].y0 / (0.5 * Hh) - 1.0;
                        double px = cx_c[0] + cx_c[1]*xn + cx_c[2]*yn + cx_c[3]*xn*xn + cx_c[4]*xn*yn + cx_c[5]*yn*yn;
                        double py = cy_c[0] + cy_c[1]*xn + cy_c[2]*yn + cy_c[3]*xn*xn + cy_c[4]*xn*yn + cy_c[5]*yn*yn;
                        rmag[i] = (float)std::hypot(rx[i] - px, ry[i] - py);
                    }
                    auto residual_sample = rmag;
                    double med = std::max(0.05, (double)quick_percentile(residual_sample, 0.50f) * 1.4826);
                    for (size_t i = 0; i < nm; ++i) {
                        w_p[i] = (rmag[i] < 2.5 * med) ? matches[i].ncc : 0.0;
                    }
                }
                if (poly_ok) {
                    std::vector<float> fy_cand = fy_out, fx_cand = fx_out;
                    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                        for (int r = r0; r < r1; ++r) {
                            double yn = (double)r / (0.5 * Hh) - 1.0;
                            for (int c = 0; c < Wh; ++c) {
                                double xn = (double)c / (0.5 * Wh) - 1.0;
                                size_t idx = (size_t)r * Wh + c;
                                fx_cand[idx] += (float)(cx_c[0] + cx_c[1]*xn + cx_c[2]*yn + cx_c[3]*xn*xn + cx_c[4]*xn*yn + cx_c[5]*yn*yn);
                                fy_cand[idx] += (float)(cy_c[0] + cy_c[1]*xn + cy_c[2]*yn + cy_c[3]*xn*xn + cy_c[4]*xn*yn + cy_c[5]*yn*yn);
                            }
                        }
                    });
                    if (compute_mae(fy_cand, fx_cand) < mae_hom) {
                        fy_out.swap(fy_cand);
                        fx_out.swap(fx_cand);
                    }
                }
                return;
            }

            // Coarse-to-Fine Self-Validating Local Tile Alignment (L1 +-56 1x px -> L0 +-68 1x px):
            // Preserves pure rigid camera Hm on sky/bokeh/flat walls while locking onto 3D parallax & rolling shutter!
            const int t_stride = 16;
            const int t_win = (std::min(Hh, Wh) < 160) ? 32 : 40;
            if (Hh >= t_win + 32 && Wh >= t_win + 32) {
                std::vector<float> med1_fy, med1_fx;
                int gH1 = 0, gW1 = 0;
                const int w1 = 24, s1 = 12;
                if (H1p >= w1 + 24 && W1p >= w1 + 24) {
                    if (sat_l1.H != H1p || sat_l1.W != W1p) sat_l1.build(c1.data(), H1p, W1p, n_threads);
                    gH1 = (H1p - w1) / s1 + 1;
                    gW1 = (W1p - w1) / s1 + 1;
                    std::vector<float> grid1_fy((size_t)gH1 * gW1), grid1_fx((size_t)gH1 * gW1);
                    parallel_for_rows(gH1, n_threads, [&](int gr0, int gr1, int) {
                        for (int gr = gr0; gr < gr1; ++gr) {
                            int by = gr * s1, cy2 = std::min(Hh - 1, 2 * (by + w1 / 2));
                            for (int gc = 0; gc < gW1; ++gc) {
                                int bx = gc * s1, cx2 = std::min(Wh - 1, 2 * (bx + w1 / 2));
                                float h_fy1 = 0.5f * fy_out[(size_t)cy2 * Wh + cx2];
                                float h_fx1 = 0.5f * fx_out[(size_t)cy2 * Wh + cx2];
                                int p_dy = (int)std::lrintf(h_fy1), p_dx = (int)std::lrintf(h_fx1);
                                int rad1 = std::min(14, std::min(H1p, W1p) / 4);
                                BlockMatch bm{};
                                if (match_zncc_block(r1_p, c1.data(), &sat_l1, H1p, W1p, by, bx, w1, w1, p_dy, p_dx, rad1, 2, bm) &&
                                    bm.ncc >= 0.55) {
                                    grid1_fy[(size_t)gr * gW1 + gc] = (float)(bm.y1 - bm.y0);
                                    grid1_fx[(size_t)gr * gW1 + gc] = (float)(bm.x1 - bm.x0);
                                } else {
                                    grid1_fy[(size_t)gr * gW1 + gc] = h_fy1;
                                    grid1_fx[(size_t)gr * gW1 + gc] = h_fx1;
                                }
                            }
                        }
                    }, 1);
                    med1_fy.resize((size_t)gH1 * gW1);
                    med1_fx.resize((size_t)gH1 * gW1);
                    median3x3_plane(grid1_fy.data(), med1_fy.data(), gH1, gW1, n_threads);
                    median3x3_plane(grid1_fx.data(), med1_fx.data(), gH1, gW1, n_threads);
                }

                const int gH = (Hh - t_win) / t_stride + 1;
                const int gW = (Wh - t_win) / t_stride + 1;
                std::vector<float> grid_fy((size_t)gH * gW, 0.0f);
                std::vector<float> grid_fx((size_t)gH * gW, 0.0f);
                std::vector<float> grid_g0((size_t)gH * gW, 0.0f);

                parallel_for_rows(gH, n_threads, [&](int gr0, int gr1, int) {
                    for (int gr = gr0; gr < gr1; ++gr) {
                        int by = gr * t_stride;
                        int cy = by + t_win / 2;
                        for (int gc = 0; gc < gW; ++gc) {
                            int bx = gc * t_stride;
                            int cx = bx + t_win / 2;
                            const size_t gi = (size_t)gr * gW + gc;
                            const float hom_fy = fy_out[(size_t)cy * Wh + cx];
                            const float hom_fx = fx_out[(size_t)cy * Wh + cx];
                            grid_g0[gi] = g0_pre[(size_t)cy * Wh + cx];

                            float prior_fy = hom_fy, prior_fx = hom_fx;
                            if (gH1 > 0 && gW1 > 0) {
                                float gy1 = (0.5f * (float)cy - 0.5f * (float)w1) / (float)s1;
                                float gx1 = (0.5f * (float)cx - 0.5f * (float)w1) / (float)s1;
                                prior_fy = 2.0f * bilerp_clamped(med1_fy.data(), gH1, gW1, gy1, gx1);
                                prior_fx = 2.0f * bilerp_clamped(med1_fx.data(), gH1, gW1, gy1, gx1);
                            }

                            int p_dy = (int)std::lrintf(prior_fy);
                            int p_dx = (int)std::lrintf(prior_fx);
                            BlockMatch bm{};
                            bool accept = false;
                            float cand_fy = hom_fy, cand_fx = hom_fx;
                            if (match_zncc_block(g0_pre, gi_pre, &cur_sat, Hh, Wh, by, bx, t_win, t_win, p_dy, p_dx, 6, 2, bm) &&
                                bm.ncc >= 0.55) {
                                cand_fy = (float)(bm.y1 - bm.y0);
                                cand_fx = (float)(bm.x1 - bm.x0);
                                const float dev = std::hypot(cand_fy - hom_fy, cand_fx - hom_fx);
                                double err_cand = 0.0, err_hom = 0.0, grad_sum = 0.0;
                                int cnt = 0;
                                const int c_yi = (int)std::floor(cand_fy), c_xi = (int)std::floor(cand_fx);
                                const bool cand_int = (by + 2 + c_yi >= 0 && by + t_win - 3 + c_yi + 1 < Hh &&
                                                       bx + 2 + c_xi >= 0 && bx + t_win - 3 + c_xi + 1 < Wh);
                                const float c_fy = cand_fy - (float)c_yi, c_fx = cand_fx - (float)c_xi;
                                const float cw00 = (1.0f - c_fy) * (1.0f - c_fx), cw01 = (1.0f - c_fy) * c_fx;
                                const float cw10 = c_fy * (1.0f - c_fx), cw11 = c_fy * c_fx;
                                for (int rr = by + 2; rr < by + t_win - 2; rr += 2) {
                                    for (int cc = bx + 2; cc < bx + t_win - 2; cc += 2) {
                                        size_t pi = (size_t)rr * Wh + cc;
                                        float ref_v = g0_pre[pi];
                                        float gx = g0_pre[pi + 1] - g0_pre[pi - 1];
                                        float gy = g0_pre[pi + Wh] - g0_pre[pi - Wh];
                                        grad_sum += (double)(std::fabs(gx) + std::fabs(gy));
                                        float wc;
                                        if (cand_int) {
                                            const float* gp = gi_pre + (size_t)(rr + c_yi) * Wh + (cc + c_xi);
                                            wc = cw00 * gp[0] + cw01 * gp[1] + cw10 * gp[Wh] + cw11 * gp[Wh + 1];
                                        } else {
                                            wc = bilerp_clamped(gi_pre, Hh, Wh, (float)rr + cand_fy, (float)cc + cand_fx);
                                        }
                                        float wh = bilerp_clamped(gi_pre, Hh, Wh, (float)rr + fy_out[pi], (float)cc + fx_out[pi]);
                                        err_cand += std::fabs((double)wc - (double)ref_v);
                                        err_hom  += std::fabs((double)wh - (double)ref_v);
                                        ++cnt;
                                    }
                                }
                                if (cnt > 0 && grad_sum > 1e-4 * (double)cnt) {
                                    if (dev <= 0.45f) {
                                        accept = (err_cand <= err_hom * 1.02 + 1e-5 * (double)cnt);
                                    } else {
                                        accept = (bm.ncc >= 0.62) && (err_cand <= err_hom * 0.97);
                                    }
                                }
                            }
                            grid_fy[gi] = accept ? cand_fy : hom_fy;
                            grid_fx[gi] = accept ? cand_fx : hom_fx;
                        }
                    }
                }, 1);

                std::vector<float> med_fy((size_t)gH * gW), med_fx((size_t)gH * gW);
                median3x3_plane(grid_fy.data(), med_fy.data(), gH, gW, n_threads);
                median3x3_plane(grid_fx.data(), med_fx.data(), gH, gW, n_threads);

                std::vector<float> dense_fy((size_t)Hh * Wh), dense_fx((size_t)Hh * Wh);
                const float inv_tau2 = 1.0f / (0.025f * 0.025f);
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        float gy_f = clamp_f(((float)r - 0.5f * (float)t_win) / (float)t_stride, 0.0f, (float)std::max(0, gH - 1));
                        int gy0 = clamp_i((int)gy_f, 0, std::max(0, gH - 2));
                        int gy1 = std::min(gH - 1, gy0 + 1);
                        float ty = clamp_f(gy_f - (float)gy0, 0.0f, 1.0f);
                        for (int c = 0; c < Wh; ++c) {
                            float gx_f = clamp_f(((float)c - 0.5f * (float)t_win) / (float)t_stride, 0.0f, (float)std::max(0, gW - 1));
                            int gx0 = clamp_i((int)gx_f, 0, std::max(0, gW - 2));
                            int gx1 = std::min(gW - 1, gx0 + 1);
                            float tx = clamp_f(gx_f - (float)gx0, 0.0f, 1.0f);
                            size_t idx = (size_t)r * Wh + c;
                            float g_pix = g0_pre[idx];

                            size_t i00 = (size_t)gy0 * gW + gx0, i01 = (size_t)gy0 * gW + gx1;
                            size_t i10 = (size_t)gy1 * gW + gx0, i11 = (size_t)gy1 * gW + gx1;
                            float d00 = g_pix - grid_g0[i00], d01 = g_pix - grid_g0[i01];
                            float d10 = g_pix - grid_g0[i10], d11 = g_pix - grid_g0[i11];
                            float w00 = (1.0f - ty) * (1.0f - tx) / (1.0f + d00 * d00 * inv_tau2);
                            float w01 = (1.0f - ty) * tx          / (1.0f + d01 * d01 * inv_tau2);
                            float w10 = ty          * (1.0f - tx) / (1.0f + d10 * d10 * inv_tau2);
                            float w11 = ty          * tx          / (1.0f + d11 * d11 * inv_tau2);
                            float inv_ws = 1.0f / std::max(1e-12f, w00 + w01 + w10 + w11);
                            dense_fy[idx] = (w00 * med_fy[i00] + w01 * med_fy[i01] + w10 * med_fy[i10] + w11 * med_fy[i11]) * inv_ws;
                            dense_fx[idx] = (w00 * med_fx[i00] + w01 * med_fx[i01] + w10 * med_fx[i10] + w11 * med_fx[i11]) * inv_ws;
                        }
                    }
                });
                fy_out.swap(dense_fy);
                fx_out.swap(dense_fx);
            }
            return;
        }
    }

    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < Wh; ++c) {
                double den = Hm[2][0] * c + Hm[2][1] * r + Hm[2][2];
                fx_out[(size_t)r * Wh + c] = (float)((Hm[0][0] * c + Hm[0][1] * r + Hm[0][2]) / den - (double)c);
                fy_out[(size_t)r * Wh + c] = (float)((Hm[1][0] * c + Hm[1][1] * r + Hm[1][2]) / den - (double)r);
            }
        }
    });
}

// Coarse 1/4-scale morphological closing (5x5 dilate -> 5x5 erode + soft feather):
// Fills interior "donut holes" inside moving subjects/coats/shadows without bloating exterior silhouettes.
static void morph_close_4x_field(
    const float* src_half, float* out_closed_half, int Hh, int Wh, int n_threads
) {
    const int Hq = std::max(8, Hh / 4);
    const int Wq = std::max(8, Wh / 4);
    std::vector<float> q0((size_t)Hq * Wq, 0.0f), q_dil((size_t)Hq * Wq, 0.0f), q_ero((size_t)Hq * Wq, 0.0f);

    parallel_for_rows(Hq, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            int sy0 = r * 4, sy1 = std::min(Hh, sy0 + 4);
            for (int c = 0; c < Wq; ++c) {
                int sx0 = c * 4, sx1 = std::min(Wh, sx0 + 4);
                float s = 0.0f;
                int cnt = 0;
                for (int yy = sy0; yy < sy1; ++yy) {
                    for (int xx = sx0; xx < sx1; ++xx) {
                        s += src_half[(size_t)yy * Wh + xx];
                        ++cnt;
                    }
                }
                float dens = (cnt > 0) ? (s / (float)cnt) : 0.0f;
                // Ignore sparse 1px subpixel-aliasing specks (<0.20 density); close genuine moving bands (>=0.25)
                q0[(size_t)r * Wq + c] = clamp_f((dens - 0.20f) / 0.40f, 0.0f, 1.0f);
            }
        }
    });

    // 5x5 Max-Dilation (= 20 half-res px = 80 2x px hole bridging)
    parallel_for_rows(Hq, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < Wq; ++c) {
                float vmax = 0.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    int rr = clamp_i(r + dy, 0, Hq - 1);
                    for (int dx = -2; dx <= 2; ++dx) {
                        vmax = std::max(vmax, q0[(size_t)rr * Wq + clamp_i(c + dx, 0, Wq - 1)]);
                    }
                }
                q_dil[(size_t)r * Wq + c] = vmax;
            }
        }
    });

    // 5x5 Min-Erosion (restores outer silhouette boundary while keeping interior holes filled)
    parallel_for_rows(Hq, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < Wq; ++c) {
                float vmin = 1.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    int rr = clamp_i(r + dy, 0, Hq - 1);
                    for (int dx = -2; dx <= 2; ++dx) {
                        vmin = std::min(vmin, q_dil[(size_t)rr * Wq + clamp_i(c + dx, 0, Wq - 1)]);
                    }
                }
                q_ero[(size_t)r * Wq + c] = vmin;
            }
        }
    });

    sep_gaussian_2d(q_ero.data(), q0.data(), Hq, Wq, 2.0f, n_threads);
    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            float ys = ((float)r + 0.5f) * ((float)Hq / (float)Hh) - 0.5f;
            for (int c = 0; c < Wh; ++c) {
                float xs = ((float)c + 0.5f) * ((float)Wq / (float)Wh) - 0.5f;
                out_closed_half[(size_t)r * Wh + c] = bilerp_clamped(q0.data(), Hq, Wq, ys, xs);
            }
        }
    });
}

// Hamilton-Adams 2nd-Order Cross-Channel Laplacian Directional Green on Anchor Frame 0 (H1 x W1):
// Recovers full 1x Nyquist directional sharpness at R/B sites on single-frame moving subjects.
static void build_hamilton_adams_green_1x(
    const float* bayer0, float* g_ha_out, int H1, int W1, int n_threads
) {
    parallel_for_rows(H1, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            int rm1 = clamp_i(r - 1, 0, H1 - 1), rp1 = clamp_i(r + 1, 0, H1 - 1);
            int rm2 = clamp_i(r - 2, 0, H1 - 1), rp2 = clamp_i(r + 2, 0, H1 - 1);
            for (int c = 0; c < W1; ++c) {
                size_t idx = (size_t)r * W1 + c;
                if (((r + c) & 1) == 1) {
                    g_ha_out[idx] = bayer0[idx];
                    continue;
                }
                int cm1 = clamp_i(c - 1, 0, W1 - 1), cp1 = clamp_i(c + 1, 0, W1 - 1);
                int cm2 = clamp_i(c - 2, 0, W1 - 1), cp2 = clamp_i(c + 2, 0, W1 - 1);

                float gL = bayer0[(size_t)r * W1 + cm1], gR = bayer0[(size_t)r * W1 + cp1];
                float gU = bayer0[(size_t)rm1 * W1 + c], gD = bayer0[(size_t)rp1 * W1 + c];
                float c0 = bayer0[idx];
                float lapH = 2.0f * c0 - bayer0[(size_t)r * W1 + cm2] - bayer0[(size_t)r * W1 + cp2];
                float lapV = 2.0f * c0 - bayer0[(size_t)rm2 * W1 + c] - bayer0[(size_t)rp2 * W1 + c];

                float dH = std::fabs(gL - gR) + std::fabs(lapH);
                float dV = std::fabs(gU - gD) + std::fabs(lapV);
                float gH = 0.5f * (gL + gR) + 0.25f * lapH;
                float gV = 0.5f * (gU + gD) + 0.25f * lapV;

                float wH = 1.0f / (dH * dH + 1e-5f);
                float wV = 1.0f / (dV * dV + 1e-5f);
                float g_est = (wH * gH + wV * gV) / (wH + wV);

                float gmin = std::min(std::min(gL, gR), std::min(gU, gD));
                float gmax = std::max(std::max(gL, gR), std::max(gU, gD));
                float span = gmax - gmin;
                g_ha_out[idx] = clamp_f(g_est, gmin - 0.08f * span, gmax + 0.08f * span);
            }
        }
    });
}

// --- Astro Mode: Subpixel Star-Centroid Extractor + 2nd-Order Celestial Polynomial + Sky/Ground Dual-Domain Flow ---
struct StarPtHalf {
    float y = 0.0f, x = 0.0f, flux = 0.0f, snr = 0.0f;
};

static std::vector<StarPtHalf> detect_stars_half(
    const float* g_half, int Hh, int Wh, float sig_1f, int n_threads
) {
    (void)sig_1f;
    std::vector<StarPtHalf> stars;
    if (Hh < 32 || Wh < 32) return stars;
    // 3x3 median wipes single-quad hot pixels / cosmic rays; sigma=1.8 matched filter locks onto optical star disks
    std::vector<float> g_med((size_t)Hh * Wh, 0.0f), g_sm((size_t)Hh * Wh, 0.0f), g_bg((size_t)Hh * Wh, 0.0f);
    median3x3_plane(g_half, g_med.data(), Hh, Wh, n_threads);
    sep_gaussian_2d(g_med.data(), g_sm.data(), Hh, Wh, 1.8f, n_threads);
    g_med.clear(); g_med.shrink_to_fit();
    sep_gaussian_2d(g_sm.data(), g_bg.data(), Hh, Wh, 12.0f, n_threads);

    std::vector<float> dsamp;
    dsamp.reserve((size_t)(Hh / 8 + 1) * (Wh / 8 + 1));
    for (int r = 8; r + 8 < Hh; r += 8) {
        for (int c = 8; c + 8 < Wh; c += 8) {
            dsamp.push_back(g_sm[(size_t)r * Wh + c] - g_bg[(size_t)r * Wh + c]);
        }
    }
    if (dsamp.size() < 16) return stars;
    float dp25 = quick_percentile(dsamp, 0.25f);
    float dp75 = quick_percentile(dsamp, 0.75f);
    float sig_sm = std::max(1e-5f, (dp75 - dp25) / 1.349f);

    for (int r = 8; r + 8 < Hh; ++r) {
        for (int c = 8; c + 8 < Wh; ++c) {
            size_t i = (size_t)r * Wh + c;
            float v0 = g_sm[i];
            float bg0 = g_bg[i];
            if (v0 - bg0 < 4.8f * sig_sm) continue;
            bool is_max = true;
            for (int dy = -3; dy <= 3 && is_max; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    if (dy == 0 && dx == 0) continue;
                    if (g_sm[(size_t)(r + dy) * Wh + (c + dx)] >= v0) { is_max = false; break; }
                }
            }
            if (!is_max) continue;

            float ring[8] = {
                g_sm[(size_t)(r - 7) * Wh + c], g_sm[(size_t)(r + 7) * Wh + c],
                g_sm[(size_t)r * Wh + (c - 7)], g_sm[(size_t)r * Wh + (c + 7)],
                g_sm[(size_t)(r - 5) * Wh + (c - 5)], g_sm[(size_t)(r - 5) * Wh + (c + 5)],
                g_sm[(size_t)(r + 5) * Wh + (c - 5)], g_sm[(size_t)(r + 5) * Wh + (c + 5)]
            };
            std::sort(ring, ring + 8);
            // Radial symmetry check: >=7 of 8 ring points must sit near local background (rejects terrestrial block corners!)
            if (ring[6] > bg0 + std::max(0.015f, 4.5f * sig_sm)) continue;
            float rmed = ring[3];
            float exc = v0 - rmed;
            float snr = exc / sig_sm;
            if (snr < 5.0f) continue;

            double sw = 0.0, sy = 0.0, sx = 0.0;
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    double w = std::max(0.0, (double)g_sm[(size_t)(r + dy) * Wh + (c + dx)] - (double)rmed);
                    sw += w; sy += w * (r + dy); sx += w * (c + dx);
                }
            }
            if (sw > 1e-6) {
                stars.push_back({(float)(sy / sw), (float)(sx / sw), exc, snr});
            }
        }
    }
    std::sort(stars.begin(), stars.end(), [](const StarPtHalf& a, const StarPtHalf& b) { return a.snr > b.snr; });
    if (stars.size() > 220) stars.resize(220);
    return stars;
}

// Fits celestial 6-term 2nd-order polynomial (dense), 3-term affine (medium), or rigid shift/roll (2..3-star telephoto)
static bool fit_celestial_poly_flow(
    const std::vector<StarPtHalf>& s0, const std::vector<StarPtHalf>& s1,
    int Hh, int Wh, std::vector<float>& fy_sky, std::vector<float>& fx_sky, int n_threads
) {
    if (s0.empty() || s1.empty()) return false;
    const size_t n0 = std::min<size_t>(s0.size(), 90);
    const size_t n1 = std::min<size_t>(s1.size(), 110);
    const float cy0 = 0.5f * (float)Hh, cx0 = 0.5f * (float)Wh;
    const float max_shift = std::min(320.0f, 0.28f * (float)std::max(Hh, Wh));

    // 2-star rigid rotation + translation RANSAC over brightest stars (pure translation when <=3 stars)
    const size_t kTop = std::min<size_t>(n0, 28);
    const size_t kCand = std::min<size_t>(n1, 36);
    const int max_a_step = (std::min(n0, n1) >= 4) ? 6 : ((std::min(n0, n1) >= 2) ? 2 : 0);
    int best_inl = 0;
    float best_snr_pair = 0.0f;
    double best_c = 1.0, best_s = 0.0, best_tx = 0.0, best_ty = 0.0;

    for (size_t i = 0; i < kTop; ++i) {
        for (size_t j = 0; j < kCand; ++j) {
            float dx0 = s1[j].x - s0[i].x;
            float dy0 = s1[j].y - s0[i].y;
            if (std::hypot(dx0, dy0) > max_shift) continue;
            for (int a_step = -max_a_step; a_step <= max_a_step; ++a_step) {
                double ang = (double)a_step * 0.0045; // +-1.55 deg covers up to ~6 min sidereal + handheld roll
                double ca = std::cos(ang), sa = std::sin(ang);
                double rx0 = s0[i].x - cx0, ry0 = s0[i].y - cy0;
                double tx = (s1[j].x - cx0) - (ca * rx0 - sa * ry0);
                double ty = (s1[j].y - cy0) - (sa * rx0 + ca * ry0);
                int inl = 0;
                float pair_snr = std::min(s0[i].snr, s1[j].snr);
                for (size_t p = 0; p < n0; ++p) {
                    double px = cx0 + ca * (s0[p].x - cx0) - sa * (s0[p].y - cy0) + tx;
                    double py = cy0 + sa * (s0[p].x - cx0) + ca * (s0[p].y - cy0) + ty;
                    for (size_t q = 0; q < n1; ++q) {
                        if (std::fabs(s1[q].x - (float)px) < 2.8f && std::fabs(s1[q].y - (float)py) < 2.8f) {
                            if (std::hypot(s1[q].x - (float)px, s1[q].y - (float)py) < 2.8f) {
                                ++inl;
                                pair_snr = std::max(pair_snr, std::min(s0[p].snr, s1[q].snr));
                                break;
                            }
                        }
                    }
                }
                if (inl > best_inl || (inl == best_inl && pair_snr > best_snr_pair)) {
                    best_inl = inl; best_snr_pair = pair_snr;
                    best_c = ca; best_s = sa; best_tx = tx; best_ty = ty;
                }
            }
        }
    }
    bool ransac_ok = (best_inl >= 4) ||
                     (best_inl >= 2 && best_snr_pair >= 11.0f) ||
                     (best_inl == 1 && best_snr_pair >= 24.0f && std::min(n0, n1) <= 2);
    if (!ransac_ok) return false;

    // Collect star correspondences & refine with IRLS 6-term quadratic (>=12), 3-term affine (4..11), or weighted rigid (1..3)
    struct StarCorr { double x0, y0, dx, dy, w; };
    std::vector<StarCorr> corrs;
    for (size_t p = 0; p < s0.size(); ++p) {
        double px = cx0 + best_c * (s0[p].x - cx0) - best_s * (s0[p].y - cy0) + best_tx;
        double py = cy0 + best_s * (s0[p].x - cx0) + best_c * (s0[p].y - cy0) + best_ty;
        int best_q = -1;
        double best_d = 3.2;
        for (size_t q = 0; q < s1.size(); ++q) {
            double d = std::hypot((double)s1[q].x - px, (double)s1[q].y - py);
            if (d < best_d) { best_d = d; best_q = (int)q; }
        }
        if (best_q >= 0) {
            corrs.push_back({s0[p].x, s0[p].y, (double)s1[best_q].x - s0[p].x, (double)s1[best_q].y - s0[p].y,
                             std::max(1.0, (double)std::min(s0[p].snr, s1[best_q].snr))});
        }
    }
    if (corrs.empty()) return false;

    fy_sky.resize((size_t)Hh * Wh);
    fx_sky.resize((size_t)Hh * Wh);

    if (corrs.size() < 4) {
        // Weighted translation (plus small RANSAC roll when >=2 wide-baseline stars matched)
        double sw = 0.0, sdx = 0.0, sdy = 0.0;
        for (const auto& sc : corrs) {
            double px = cx0 + best_c * (sc.x0 - cx0) - best_s * (sc.y0 - cy0) + best_tx - sc.x0;
            double py = cy0 + best_s * (sc.x0 - cx0) + best_c * (sc.y0 - cy0) + best_ty - sc.y0;
            sw += sc.w;
            sdx += sc.w * (sc.dx - px);
            sdy += sc.w * (sc.dy - py);
        }
        double rdx = (sw > 0.0) ? (sdx / sw) : 0.0;
        double rdy = (sw > 0.0) ? (sdy / sw) : 0.0;
        best_tx += rdx;
        best_ty += rdy;
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wh; ++c) {
                    size_t i = (size_t)r * Wh + c;
                    fx_sky[i] = (float)((cx0 + best_c * (c - cx0) - best_s * (r - cy0) + best_tx) - c);
                    fy_sky[i] = (float)((cy0 + best_s * (c - cx0) + best_c * (r - cy0) + best_ty) - r);
                }
            }
        });
        return true;
    }

    const int n_terms = (corrs.size() >= 12) ? 6 : 3;
    std::vector<double> cx_c(6, 0.0), cy_c(6, 0.0);
    bool fit_ok = false;
    std::vector<double> w_r(corrs.size(), 1.0);
    for (int it = 0; it < 2; ++it) {
        std::vector<double> A, bx, by;
        int n_act = 0;
        for (size_t i = 0; i < corrs.size(); ++i) {
            double wi = corrs[i].w * w_r[i];
            if (wi <= 1e-6) continue;
            double xn = corrs[i].x0 / (0.5 * Wh) - 1.0;
            double yn = corrs[i].y0 / (0.5 * Hh) - 1.0;
            if (n_terms == 6) {
                double row[6] = {wi, wi * xn, wi * yn, wi * xn * xn, wi * xn * yn, wi * yn * yn};
                A.insert(A.end(), row, row + 6);
            } else {
                double row[3] = {wi, wi * xn, wi * yn};
                A.insert(A.end(), row, row + 3);
            }
            bx.push_back(wi * corrs[i].dx);
            by.push_back(wi * corrs[i].dy);
            ++n_act;
        }
        if (n_act < n_terms) break;
        std::vector<double> A_cp = A, sx_c, sy_c;
        if (!solve_least_squares(n_act, n_terms, A, bx, sx_c) ||
            !solve_least_squares(n_act, n_terms, A_cp, by, sy_c)) break;
        for (int k = 0; k < n_terms; ++k) { cx_c[k] = sx_c[k]; cy_c[k] = sy_c[k]; }
        fit_ok = true;

        std::vector<float> res_d(corrs.size());
        for (size_t i = 0; i < corrs.size(); ++i) {
            double xn = corrs[i].x0 / (0.5 * Wh) - 1.0;
            double yn = corrs[i].y0 / (0.5 * Hh) - 1.0;
            double mx = cx_c[0] + cx_c[1]*xn + cx_c[2]*yn + cx_c[3]*xn*xn + cx_c[4]*xn*yn + cx_c[5]*yn*yn;
            double my = cy_c[0] + cy_c[1]*xn + cy_c[2]*yn + cy_c[3]*xn*xn + cy_c[4]*xn*yn + cy_c[5]*yn*yn;
            res_d[i] = (float)std::hypot(corrs[i].dx - mx, corrs[i].dy - my);
        }
        auto residual_sample = res_d;
        float mad = std::max(0.08f, quick_percentile(residual_sample, 0.50f) * 1.4826f);
        for (size_t i = 0; i < corrs.size(); ++i) w_r[i] = (res_d[i] < 2.2f * mad) ? 1.0 : 0.0;
    }

    if (fit_ok) {
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                double yn = (double)r / (0.5 * Hh) - 1.0;
                for (int c = 0; c < Wh; ++c) {
                    double xn = (double)c / (0.5 * Wh) - 1.0;
                    size_t i = (size_t)r * Wh + c;
                    fx_sky[i] = (float)(cx_c[0] + cx_c[1]*xn + cx_c[2]*yn + cx_c[3]*xn*xn + cx_c[4]*xn*yn + cx_c[5]*yn*yn);
                    fy_sky[i] = (float)(cy_c[0] + cy_c[1]*xn + cy_c[2]*yn + cy_c[3]*xn*xn + cy_c[4]*xn*yn + cy_c[5]*yn*yn);
                }
            }
        });
    } else {
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wh; ++c) {
                    size_t i = (size_t)r * Wh + c;
                    fx_sky[i] = (float)((cx0 + best_c * (c - cx0) - best_s * (r - cy0) + best_tx) - c);
                    fy_sky[i] = (float)((cy0 + best_s * (c - cx0) + best_c * (r - cy0) + best_ty) - r);
                }
            }
        });
    }
    return true;
}

// Segments sky vs terrestrial horizon silhouette and blends celestial & terrestrial flows seamlessly
static void blend_astro_dual_domain_flow(
    const float* g0_pre, const float* gi_pre, int Hh, int Wh,
    const std::vector<StarPtHalf>& stars0,
    const std::vector<float>& fy_sky, const std::vector<float>& fx_sky,
    std::vector<float>& fy_io, std::vector<float>& fx_io,
    std::vector<float>& sky_mask_accum, float sig_1f, int n_threads
) {
    const int bs = 16;
    const int gH = std::max(4, (Hh + bs - 1) / bs);
    const int gW = std::max(4, (Wh + bs - 1) / bs);
    std::vector<uint8_t> has_star((size_t)gH * gW, 0);
    for (const auto& st : stars0) {
        int gr = clamp_i((int)(st.y / bs), 0, gH - 1);
        int gc = clamp_i((int)(st.x / bs), 0, gW - 1);
        has_star[(size_t)gr * gW + gc] = 1;
    }

    // Per-column lowest star row gives initial upper bound on sky region
    std::vector<int> lowest_star_gr(gW, 0);
    for (int gc = 0; gc < gW; ++gc) {
        for (int gr = 0; gr < gH; ++gr) {
            if (has_star[(size_t)gr * gW + gc]) lowest_star_gr[gc] = gr;
        }
    }

    std::vector<float> blk_sky((size_t)gH * gW, 1.0f);
    int gnd_blocks = 0;
    for (int gr = 0; gr < gH; ++gr) {
        int y0 = gr * bs, y1 = std::min(Hh - 1, y0 + bs);
        for (int gc = 0; gc < gW; ++gc) {
            size_t gi = (size_t)gr * gW + gc;
            if (has_star[gi]) { blk_sky[gi] = 1.0f; continue; }
            int x0 = gc * bs, x1 = std::min(Wh - 1, x0 + bs);
            double e_sky = 0.0, e_gnd = 0.0, grad_s = 0.0;
            int cnt = 0;
            for (int r = std::max(1, y0); r < y1; r += 2) {
                for (int c = std::max(1, x0); c < x1; c += 2) {
                    size_t i = (size_t)r * Wh + c;
                    float v0 = g0_pre[i];
                    float vs = bilerp_clamped(gi_pre, Hh, Wh, (float)r + fy_sky[i], (float)c + fx_sky[i]);
                    float vg = bilerp_clamped(gi_pre, Hh, Wh, (float)r + fy_io[i],  (float)c + fx_io[i]);
                    e_sky += std::fabs(vs - v0);
                    e_gnd += std::fabs(vg - v0);
                    grad_s += std::hypot(g0_pre[i + 1] - g0_pre[i - 1], g0_pre[i + Wh] - g0_pre[i - Wh]);
                    ++cnt;
                }
            }
            if (cnt > 0) {
                e_sky /= cnt; e_gnd /= cnt; grad_s /= cnt;
                // Lower-frame structured region that aligns better to terrestrial camera motion than sidereal sky!
                if (gr >= gH / 3 && gr > lowest_star_gr[gc] &&
                    grad_s > 1.8 * (double)sig_1f && (e_gnd + 0.35 * (double)sig_1f < e_sky)) {
                    blk_sky[gi] = 0.0f;
                    ++gnd_blocks;
                }
            }
        }
    }

    if (gnd_blocks < std::max(3, (gH * gW) / 35)) {
        // Pure starfield / deep-sky frame: 100% celestial polynomial flow
        fy_io = fy_sky;
        fx_io = fx_sky;
        for (size_t i = 0; i < sky_mask_accum.size(); ++i) sky_mask_accum[i] += 1.0f;
        return;
    }

    // Enforce vertical horizon monotonicity: once ground silhouette starts in a column without stars below, stay ground
    for (int gc = 0; gc < gW; ++gc) {
        int gnd_run = 0;
        for (int gr = lowest_star_gr[gc] + 1; gr < gH; ++gr) {
            size_t gi = (size_t)gr * gW + gc;
            if (blk_sky[gi] < 0.5f) ++gnd_run;
            if (gnd_run >= 2 && !has_star[gi]) blk_sky[gi] = 0.0f;
        }
    }
    std::vector<float> blk_smooth((size_t)gH * gW, 1.0f);
    sep_gaussian_2d(blk_sky.data(), blk_smooth.data(), gH, gW, 1.4f, n_threads);

    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            float gy_f = ((float)r + 0.5f) / (float)bs - 0.5f;
            for (int c = 0; c < Wh; ++c) {
                float gx_f = ((float)c + 0.5f) / (float)bs - 0.5f;
                float m_sky = clamp_f(bilerp_clamped(blk_smooth.data(), gH, gW, gy_f, gx_f), 0.0f, 1.0f);
                size_t i = (size_t)r * Wh + c;
                fy_io[i] = m_sky * fy_sky[i] + (1.0f - m_sky) * fy_io[i];
                fx_io[i] = m_sky * fx_sky[i] + (1.0f - m_sky) * fx_io[i];
                sky_mask_accum[i] += m_sky;
            }
        }
    });
}

// --- Pure-Physical Detail Reconstruction: 3-Gate Confidence-Checked Rigid Micro-Patch Subpixel Recovery ---
static void recover_rigid_motion_patches(
    const float* g0_pre, const float* gi_pre, const float* g_half_n,
    const float* ref_f, const float* ref_w, const float* grad_f, const float* grad_w,
    int Hh, int Wh, double pg_sig0_sq, double pg_gain, float zf_lim, float zw_lim, float w_frame,
    std::vector<float>& fy_io, std::vector<float>& fx_io, std::vector<float>& mw_io, int n_threads,
    std::vector<float>* mw_align_io = nullptr, const CurIntegral* prebuilt_sat = nullptr,
    const heraw_wvn::BurstWaveletContext* wctx = nullptr
) {
    const int win = 16, stride = 12;
    if (Hh < win + 48 || Wh < win + 48) return;
    CurIntegral sat_local;
    const CurIntegral* sat_ptr = prebuilt_sat;
    if (!sat_ptr || sat_ptr->H != Hh || sat_ptr->W != Wh) {
        sat_local.build(gi_pre, Hh, Wh, n_threads);
        sat_ptr = &sat_local;
    }

    const int gH = (Hh - win) / stride + 1;
    const int gW = (Wh - win) / stride + 1;
    std::vector<float> p_fy((size_t)gH * gW, 0.0f), p_fx((size_t)gH * gW, 0.0f), p_ok((size_t)gH * gW, 0.0f);

    parallel_for_rows(gH, n_threads, [&](int gr0, int gr1, int) {
        for (int gr = gr0; gr < gr1; ++gr) {
            int by = gr * stride;
            for (int gc = 0; gc < gW; ++gc) {
                int bx = gc * stride;
                float mw_sum = 0.0f, gf_sum = 0.0f, iv_sum = 0.0f;
                for (int r = by; r < by + win; r += 2) {
                    for (int c = bx; c < bx + win; c += 2) {
                        size_t i = (size_t)r * Wh + c;
                        mw_sum += mw_io[i];
                        gf_sum += grad_f[i];
                        iv_sum += std::max(0.0f, ref_w[i]);
                    }
                }
                const float inv_n = 1.0f / 64.0f;
                if (mw_sum * inv_n > 0.40f) continue; // Already static-aligned
                const float iv_loc = iv_sum * inv_n;
                float q_loc = (wctx && wctx->active) ? wctx->eval_half_qvar_fine(by + win / 2, iv_loc) : 0.0f;
                float sf_loc = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv_loc) + q_loc) * 0.32f + 0.0012f;
                if (gf_sum * inv_n < 2.2f * sf_loc) continue; // Flat region: no rigid 2D corner/texture to lock safely

                size_t ci = (size_t)(by + win / 2) * Wh + (bx + win / 2);
                int b_dy = (int)std::lrintf(fy_io[ci]), b_dx = (int)std::lrintf(fx_io[ci]);
                BlockMatch bm{};
                // Gate 1: Strict structural correlation (ZNCC >= 0.90) within +-20 half-res px (+-80 2x px)
                if (!match_zncc_block(g0_pre, gi_pre, sat_ptr, Hh, Wh, by, bx, win, win, b_dy, b_dx, 20, 2, bm) || bm.ncc < 0.90) {
                    continue;
                }
                // Subpixel ZNCC peak already refined inside match_zncc_block
                float cand_fy = (float)(bm.y1 - bm.y0);
                float cand_fx = (float)(bm.x1 - bm.x0);

                // Gate 2: Strict Poisson-Gaussian photometric residual verification across the 16x16 patch
                int pass_px = 0, tot_px = 0;
                const int c_yi = (int)std::floor(cand_fy), c_xi = (int)std::floor(cand_fx);
                const bool cand_int = (by + 1 + c_yi >= 0 && by + win - 2 + c_yi + 1 < Hh &&
                                       bx + 1 + c_xi >= 0 && bx + win - 2 + c_xi + 1 < Wh);
                const float c_fy = cand_fy - (float)c_yi, c_fx = cand_fx - (float)c_xi;
                const float cw00 = (1.0f - c_fy) * (1.0f - c_fx), cw01 = (1.0f - c_fy) * c_fx;
                const float cw10 = c_fy * (1.0f - c_fx), cw11 = c_fy * c_fx;
                for (int r = by + 1; r < by + win - 1; ++r) {
                    for (int c = bx + 1; c < bx + win - 1; ++c) {
                        size_t i = (size_t)r * Wh + c;
                        float wv;
                        if (cand_int) {
                            const float* gp = g_half_n + (size_t)(r + c_yi) * Wh + (c + c_xi);
                            wv = cw00 * gp[0] + cw01 * gp[1] + cw10 * gp[Wh] + cw11 * gp[Wh + 1];
                        } else {
                            wv = bilerp_clamped(g_half_n, Hh, Wh, (float)r + cand_fy, (float)c + cand_fx);
                        }
                        float iv = std::max(0.0f, ref_w[i]);
                        float q_p = (wctx && wctx->active) ? wctx->eval_half_qvar(r, iv) : 0.0f;
                        float sig_p = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv) + q_p) + 0.0015f;
                        float z = (std::fabs(wv - ref_f[i]) - 0.65f * grad_f[i]) / sig_p;
                        float zw_r = (std::fabs(wv - ref_w[i]) - 0.55f * grad_w[i]) / sig_p;
                        if (z < std::min(2.3f, 0.72f * zf_lim) && zw_r < zw_lim) ++pass_px;
                        ++tot_px;
                    }
                }
                if (tot_px > 0 && pass_px * 100 >= tot_px * 88) {
                    size_t gi = (size_t)gr * gW + gc;
                    p_fy[gi] = cand_fy;
                    p_fx[gi] = cand_fx;
                    p_ok[gi] = clamp_f((float)((bm.ncc - 0.88) / 0.08), 0.0f, 1.0f);
                }
            }
        }
    }, 1);

    // Gate 3: Smooth raised-cosine patch handoff (disjoint 2x2 checkerboard since win=16 < 2*stride=24)
    float cos_w[16];
    for (int d = 0; d < win; ++d) {
        cos_w[d] = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * ((float)d + 0.5f) / (float)win));
    }
    for (int color = 0; color < 4; ++color) {
        const int py_par = color >> 1, px_par = color & 1;
        const int n_pr = (gH - py_par + 1) / 2;
        if (n_pr <= 0) continue;
        parallel_for_rows(n_pr, n_threads, [&](int pr0, int pr1, int) {
            for (int pr = pr0; pr < pr1; ++pr) {
                const int gr = py_par + 2 * pr;
                const int by = gr * stride;
                for (int gc = px_par; gc < gW; gc += 2) {
                    const size_t gi = (size_t)gr * gW + gc;
                    const float conf = p_ok[gi];
                    if (conf <= 0.05f) continue;
                    const int bx = gc * stride;
                    const float cfy = p_fy[gi], cfx = p_fx[gi];
                    const float target_mw = 0.85f * w_frame * conf;
                    for (int dy = 0; dy < win; ++dy) {
                        const float wy_c = conf * cos_w[dy];
                        for (int dx = 0; dx < win; ++dx) {
                            const float w_blend = wy_c * cos_w[dx];
                            if (w_blend < 0.12f) continue;
                            const size_t i = (size_t)(by + dy) * Wh + (bx + dx);
                            if (target_mw > mw_io[i] + 0.10f) {
                                const float dfy = cfy - fy_io[i], dfx = cfx - fx_io[i];
                                if (dfy * dfy + dfx * dfx > 0.75f * 0.75f) {
                                    if (w_blend >= 0.5f) {
                                        fy_io[i] = cfy;
                                        fx_io[i] = cfx;
                                        mw_io[i] = std::max(mw_io[i], w_blend * target_mw);
                                        if (mw_align_io) (*mw_align_io)[i] = std::max((*mw_align_io)[i], w_blend * 0.85f * conf);
                                    }
                                } else {
                                    fy_io[i] = (1.0f - w_blend) * fy_io[i] + w_blend * cfy;
                                    fx_io[i] = (1.0f - w_blend) * fx_io[i] + w_blend * cfx;
                                    mw_io[i] = std::max(mw_io[i], w_blend * target_mw);
                                    if (mw_align_io) (*mw_align_io)[i] = std::max((*mw_align_io)[i], w_blend * 0.85f * conf);
                                }
                            }
                        }
                    }
                }
            }
        });
    }
}

// ---------------------------------------------------------------------------
// 5. Multiscale Orthonormal 8x8 DCT Empirical Wiener Pyramid Denoiser (Lebrun et al.)
// ---------------------------------------------------------------------------
#if !defined(__aarch64__) && !defined(__AVX2__)
struct Dct8Table {
    float C[8][8];
    Dct8Table() {
        const double pi = 3.14159265358979323846;
        for (int k = 0; k < 8; ++k) {
            double a = (k == 0) ? (1.0 / std::sqrt(8.0)) : 0.5;
            for (int n = 0; n < 8; ++n) {
                C[k][n] = (float)(a * std::cos(pi * (2.0 * n + 1.0) * k / 16.0));
            }
        }
    }
};
static const Dct8Table g_dct8;
#endif

#if defined(__AVX2__)
static inline void transpose_8x8_avx2(__m256 r[8]) {
    __m256 t0 = _mm256_unpacklo_ps(r[0], r[1]);
    __m256 t1 = _mm256_unpackhi_ps(r[0], r[1]);
    __m256 t2 = _mm256_unpacklo_ps(r[2], r[3]);
    __m256 t3 = _mm256_unpackhi_ps(r[2], r[3]);
    __m256 t4 = _mm256_unpacklo_ps(r[4], r[5]);
    __m256 t5 = _mm256_unpackhi_ps(r[4], r[5]);
    __m256 t6 = _mm256_unpacklo_ps(r[6], r[7]);
    __m256 t7 = _mm256_unpackhi_ps(r[6], r[7]);

    __m256 s0 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 s1 = _mm256_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 2, 3, 2));
    __m256 s2 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 s3 = _mm256_shuffle_ps(t1, t3, _MM_SHUFFLE(3, 2, 3, 2));
    __m256 s4 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 s5 = _mm256_shuffle_ps(t4, t6, _MM_SHUFFLE(3, 2, 3, 2));
    __m256 s6 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(1, 0, 1, 0));
    __m256 s7 = _mm256_shuffle_ps(t5, t7, _MM_SHUFFLE(3, 2, 3, 2));

    r[0] = _mm256_permute2f128_ps(s0, s4, 0x20);
    r[1] = _mm256_permute2f128_ps(s1, s5, 0x20);
    r[2] = _mm256_permute2f128_ps(s2, s6, 0x20);
    r[3] = _mm256_permute2f128_ps(s3, s7, 0x20);
    r[4] = _mm256_permute2f128_ps(s0, s4, 0x31);
    r[5] = _mm256_permute2f128_ps(s1, s5, 0x31);
    r[6] = _mm256_permute2f128_ps(s2, s6, 0x31);
    r[7] = _mm256_permute2f128_ps(s3, s7, 0x31);
}

static inline void dct1d_cols8_avx2_fwd(__m256 x[8]) {
    const __m256 c1 = _mm256_set1_ps(0.4903926402f);
    const __m256 c2 = _mm256_set1_ps(0.4619397663f);
    const __m256 c3 = _mm256_set1_ps(0.4157348062f);
    const __m256 c4 = _mm256_set1_ps(0.3535533906f);
    const __m256 c5 = _mm256_set1_ps(0.2777851165f);
    const __m256 c6 = _mm256_set1_ps(0.1913417162f);
    const __m256 c7 = _mm256_set1_ps(0.0975451610f);

    __m256 s0 = _mm256_add_ps(x[0], x[7]), d7 = _mm256_sub_ps(x[0], x[7]);
    __m256 s1 = _mm256_add_ps(x[1], x[6]), d6 = _mm256_sub_ps(x[1], x[6]);
    __m256 s2 = _mm256_add_ps(x[2], x[5]), d5 = _mm256_sub_ps(x[2], x[5]);
    __m256 s3 = _mm256_add_ps(x[3], x[4]), d4 = _mm256_sub_ps(x[3], x[4]);

    __m256 e0 = _mm256_add_ps(s0, s3), e3 = _mm256_sub_ps(s0, s3);
    __m256 e1 = _mm256_add_ps(s1, s2), e2 = _mm256_sub_ps(s1, s2);

    x[0] = _mm256_mul_ps(_mm256_add_ps(e0, e1), c4);
    x[4] = _mm256_mul_ps(_mm256_sub_ps(e0, e1), c4);
    x[2] = _mm256_fmadd_ps(e3, c2, _mm256_mul_ps(e2, c6));
    x[6] = _mm256_fmsub_ps(e3, c6, _mm256_mul_ps(e2, c2));

    x[1] = _mm256_fmadd_ps(d7, c1, _mm256_fmadd_ps(d6, c3, _mm256_fmadd_ps(d5, c5, _mm256_mul_ps(d4, c7))));
    x[3] = _mm256_fmsub_ps(d7, c3, _mm256_fmadd_ps(d6, c7, _mm256_fmadd_ps(d5, c1, _mm256_mul_ps(d4, c5))));
    x[5] = _mm256_fmadd_ps(d5, c7, _mm256_fmadd_ps(d4, c3, _mm256_fmsub_ps(d7, c5, _mm256_mul_ps(d6, c1))));
    x[7] = _mm256_fmsub_ps(d5, c3, _mm256_fmsub_ps(d4, c1, _mm256_fmsub_ps(d7, c7, _mm256_mul_ps(d6, c5))));
}

static inline void dct1d_cols8_avx2_inv(__m256 Y[8]) {
    const __m256 c1 = _mm256_set1_ps(0.4903926402f);
    const __m256 c2 = _mm256_set1_ps(0.4619397663f);
    const __m256 c3 = _mm256_set1_ps(0.4157348062f);
    const __m256 c4 = _mm256_set1_ps(0.3535533906f);
    const __m256 c5 = _mm256_set1_ps(0.2777851165f);
    const __m256 c6 = _mm256_set1_ps(0.1913417162f);
    const __m256 c7 = _mm256_set1_ps(0.0975451610f);

    __m256 e0 = _mm256_mul_ps(_mm256_add_ps(Y[0], Y[4]), c4);
    __m256 e1 = _mm256_mul_ps(_mm256_sub_ps(Y[0], Y[4]), c4);
    __m256 e3 = _mm256_fmadd_ps(Y[2], c2, _mm256_mul_ps(Y[6], c6));
    __m256 e2 = _mm256_fmsub_ps(Y[2], c6, _mm256_mul_ps(Y[6], c2));

    __m256 s0 = _mm256_add_ps(e0, e3), s3 = _mm256_sub_ps(e0, e3);
    __m256 s1 = _mm256_add_ps(e1, e2), s2 = _mm256_sub_ps(e1, e2);

    __m256 d7 = _mm256_fmadd_ps(Y[1], c1, _mm256_fmadd_ps(Y[3], c3, _mm256_fmadd_ps(Y[5], c5, _mm256_mul_ps(Y[7], c7))));
    __m256 d6 = _mm256_fmsub_ps(Y[1], c3, _mm256_fmadd_ps(Y[3], c7, _mm256_fmadd_ps(Y[5], c1, _mm256_mul_ps(Y[7], c5))));
    __m256 d5 = _mm256_fmadd_ps(Y[5], c7, _mm256_fmadd_ps(Y[7], c3, _mm256_fmsub_ps(Y[1], c5, _mm256_mul_ps(Y[3], c1))));
    __m256 d4 = _mm256_fmsub_ps(Y[5], c3, _mm256_fmsub_ps(Y[7], c1, _mm256_fmsub_ps(Y[1], c7, _mm256_mul_ps(Y[3], c5))));

    Y[0] = _mm256_add_ps(s0, d7); Y[7] = _mm256_sub_ps(s0, d7);
    Y[1] = _mm256_add_ps(s1, d6); Y[6] = _mm256_sub_ps(s1, d6);
    Y[2] = _mm256_add_ps(s2, d5); Y[5] = _mm256_sub_ps(s2, d5);
    Y[3] = _mm256_add_ps(s3, d4); Y[4] = _mm256_sub_ps(s3, d4);
}
#endif

// Exact Orthonormal 8x8 2D DCT-II & DCT-III via ARM64 NEON 8-Wide Butterfly + In-Register Transpose
#ifdef __aarch64__
static inline void transpose_4x4_neon(float32x4_t& r0, float32x4_t& r1, float32x4_t& r2, float32x4_t& r3) {
    float32x4x2_t t01 = vtrnq_f32(r0, r1);
    float32x4x2_t t23 = vtrnq_f32(r2, r3);
    r0 = vcombine_f32(vget_low_f32(t01.val[0]), vget_low_f32(t23.val[0]));
    r1 = vcombine_f32(vget_low_f32(t01.val[1]), vget_low_f32(t23.val[1]));
    r2 = vcombine_f32(vget_high_f32(t01.val[0]), vget_high_f32(t23.val[0]));
    r3 = vcombine_f32(vget_high_f32(t01.val[1]), vget_high_f32(t23.val[1]));
}

static inline void transpose_8x8_neon(float32x4_t a[8], float32x4_t b[8]) {
    transpose_4x4_neon(a[0], a[1], a[2], a[3]);
    transpose_4x4_neon(b[0], b[1], b[2], b[3]);
    transpose_4x4_neon(a[4], a[5], a[6], a[7]);
    transpose_4x4_neon(b[4], b[5], b[6], b[7]);
    std::swap(b[0], a[4]);
    std::swap(b[1], a[5]);
    std::swap(b[2], a[6]);
    std::swap(b[3], a[7]);
}

static inline void dct1d_cols4_fwd(float32x4_t x[8]) {
    constexpr float c1 = 0.4903926402f; // 0.5 * cos(1*pi/16)
    constexpr float c2 = 0.4619397663f; // 0.5 * cos(2*pi/16)
    constexpr float c3 = 0.4157348062f; // 0.5 * cos(3*pi/16)
    constexpr float c4 = 0.3535533906f; // 1 / sqrt(8)
    constexpr float c5 = 0.2777851165f; // 0.5 * cos(5*pi/16)
    constexpr float c6 = 0.1913417162f; // 0.5 * cos(6*pi/16)
    constexpr float c7 = 0.0975451610f; // 0.5 * cos(7*pi/16)

    float32x4_t s0 = vaddq_f32(x[0], x[7]), d7 = vsubq_f32(x[0], x[7]);
    float32x4_t s1 = vaddq_f32(x[1], x[6]), d6 = vsubq_f32(x[1], x[6]);
    float32x4_t s2 = vaddq_f32(x[2], x[5]), d5 = vsubq_f32(x[2], x[5]);
    float32x4_t s3 = vaddq_f32(x[3], x[4]), d4 = vsubq_f32(x[3], x[4]);

    float32x4_t e0 = vaddq_f32(s0, s3), e3 = vsubq_f32(s0, s3);
    float32x4_t e1 = vaddq_f32(s1, s2), e2 = vsubq_f32(s1, s2);

    x[0] = vmulq_n_f32(vaddq_f32(e0, e1), c4);
    x[4] = vmulq_n_f32(vsubq_f32(e0, e1), c4);
    x[2] = vfmaq_n_f32(vmulq_n_f32(e3, c2), e2, c6);
    x[6] = vfmsq_n_f32(vmulq_n_f32(e3, c6), e2, c2);

    x[1] = vfmaq_n_f32(vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(d7, c1), d6, c3), d5, c5), d4, c7);
    x[3] = vfmsq_n_f32(vfmsq_n_f32(vfmsq_n_f32(vmulq_n_f32(d7, c3), d6, c7), d5, c1), d4, c5);
    x[5] = vfmaq_n_f32(vfmaq_n_f32(vfmsq_n_f32(vmulq_n_f32(d7, c5), d6, c1), d5, c7), d4, c3);
    x[7] = vfmsq_n_f32(vfmaq_n_f32(vfmsq_n_f32(vmulq_n_f32(d7, c7), d6, c5), d5, c3), d4, c1);
}

static inline void dct1d_cols4_inv(float32x4_t Y[8]) {
    constexpr float c1 = 0.4903926402f;
    constexpr float c2 = 0.4619397663f;
    constexpr float c3 = 0.4157348062f;
    constexpr float c4 = 0.3535533906f;
    constexpr float c5 = 0.2777851165f;
    constexpr float c6 = 0.1913417162f;
    constexpr float c7 = 0.0975451610f;

    float32x4_t e0 = vmulq_n_f32(vaddq_f32(Y[0], Y[4]), c4);
    float32x4_t e1 = vmulq_n_f32(vsubq_f32(Y[0], Y[4]), c4);
    float32x4_t e3 = vfmaq_n_f32(vmulq_n_f32(Y[2], c2), Y[6], c6);
    float32x4_t e2 = vfmsq_n_f32(vmulq_n_f32(Y[2], c6), Y[6], c2);

    float32x4_t s0 = vaddq_f32(e0, e3), s3 = vsubq_f32(e0, e3);
    float32x4_t s1 = vaddq_f32(e1, e2), s2 = vsubq_f32(e1, e2);

    float32x4_t d7 = vfmaq_n_f32(vfmaq_n_f32(vfmaq_n_f32(vmulq_n_f32(Y[1], c1), Y[3], c3), Y[5], c5), Y[7], c7);
    float32x4_t d6 = vfmsq_n_f32(vfmsq_n_f32(vfmsq_n_f32(vmulq_n_f32(Y[1], c3), Y[3], c7), Y[5], c1), Y[7], c5);
    float32x4_t d5 = vfmaq_n_f32(vfmaq_n_f32(vfmsq_n_f32(vmulq_n_f32(Y[1], c5), Y[3], c1), Y[5], c7), Y[7], c3);
    float32x4_t d4 = vfmsq_n_f32(vfmaq_n_f32(vfmsq_n_f32(vmulq_n_f32(Y[1], c7), Y[3], c5), Y[5], c3), Y[7], c1);

    Y[0] = vaddq_f32(s0, d7); Y[7] = vsubq_f32(s0, d7);
    Y[1] = vaddq_f32(s1, d6); Y[6] = vsubq_f32(s1, d6);
    Y[2] = vaddq_f32(s2, d5); Y[5] = vsubq_f32(s2, d5);
    Y[3] = vaddq_f32(s3, d4); Y[4] = vsubq_f32(s3, d4);
}
#endif

#if !defined(__aarch64__) && !defined(__AVX2__)
static inline void dct8x8_fwd(const float in[64], float out[64]) {
    float tmp[64];
    for (int r = 0; r < 8; ++r) {
        const float* x = in + r * 8;
        for (int k = 0; k < 8; ++k) {
            const float* ck = g_dct8.C[k];
            tmp[r * 8 + k] = x[0]*ck[0] + x[1]*ck[1] + x[2]*ck[2] + x[3]*ck[3]
                           + x[4]*ck[4] + x[5]*ck[5] + x[6]*ck[6] + x[7]*ck[7];
        }
    }
    for (int k = 0; k < 8; ++k) {
        const float* ck = g_dct8.C[k];
        for (int c = 0; c < 8; ++c) {
            out[k * 8 + c] = ck[0]*tmp[0*8+c] + ck[1]*tmp[1*8+c] + ck[2]*tmp[2*8+c] + ck[3]*tmp[3*8+c]
                           + ck[4]*tmp[4*8+c] + ck[5]*tmp[5*8+c] + ck[6]*tmp[6*8+c] + ck[7]*tmp[7*8+c];
        }
    }
}

static inline void dct8x8_inv(const float in[64], float out[64]) {
    float tmp[64];
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            tmp[r * 8 + c] = g_dct8.C[0][r]*in[0*8+c] + g_dct8.C[1][r]*in[1*8+c]
                           + g_dct8.C[2][r]*in[2*8+c] + g_dct8.C[3][r]*in[3*8+c]
                           + g_dct8.C[4][r]*in[4*8+c] + g_dct8.C[5][r]*in[5*8+c]
                           + g_dct8.C[6][r]*in[6*8+c] + g_dct8.C[7][r]*in[7*8+c];
        }
    }
    for (int r = 0; r < 8; ++r) {
        const float* t = tmp + r * 8;
        for (int n = 0; n < 8; ++n) {
            out[r * 8 + n] = t[0]*g_dct8.C[0][n] + t[1]*g_dct8.C[1][n]
                           + t[2]*g_dct8.C[2][n] + t[3]*g_dct8.C[3][n]
                           + t[4]*g_dct8.C[4][n] + t[5]*g_dct8.C[5][n]
                           + t[6]*g_dct8.C[6][n] + t[7]*g_dct8.C[7][n];
        }
    }
}
#endif

// Single-scale 2-pass (Hard-Threshold Pilot -> Empirical Wiener) Sliding 8x8 DCT Denoiser
static void dct_wiener_single_scale(
    const float* noisy, float* denoised, int H, int W, float sigma, int stride, int n_threads,
    const float* snr_boost_half = nullptr, int Hh = 0, int Wh = 0, int scale_mul = 1,
    const heraw_gpu::PC_DctWiener* roi = nullptr,
    UninitBuf<float>* ext_pilot = nullptr
) {
    if (sigma <= 1e-5f || H < 8 || W < 8) {
        if (noisy != denoised) std::memcpy(denoised, noisy, (size_t)H * W * sizeof(float));
        return;
    }

    std::vector<int> r_starts, c_starts;
    for (int r = 0; r <= H - 8; r += stride) r_starts.push_back(r);
    if (r_starts.empty() || r_starts.back() != H - 8) r_starts.push_back(H - 8);
    for (int c = 0; c <= W - 8; c += stride) c_starts.push_back(c);
    if (c_starts.empty() || c_starts.back() != W - 8) c_starts.push_back(W - 8);

    const int NR = (int)r_starts.size();
    const int NC = (int)c_starts.size();
    UninitBuf<float> own_pilot;
    UninitBuf<float>& pilot = ext_pilot ? *ext_pilot : own_pilot;
    pilot.resize((size_t)H * W);

    // 2D L1/L2 cache-resident padded tile accumulators adapted to host L2 size (256 on Zen3 512KB L2, 512 on >=1MB L2)
    const int BAND = 32;
    const int TILE_X = heraw_cache::host_cache_profile().dct_tile_x();
    const int PAD_X = 8;
    const int STRIDE_X = TILE_X + 2 * PAD_X;
    const int max_nt = std::max(1, n_threads);
    std::vector<std::vector<float>> tls_acc(max_nt, std::vector<float>((size_t)BAND * STRIDE_X));
    std::vector<std::vector<float>> tls_wgt(max_nt, std::vector<float>((size_t)BAND * STRIDE_X));

    auto run_pass = [&](bool is_wiener, const float* src_in, const float* guide_in, float* dst_out) {
        const float thr = 3.10f * sigma;
        const float sig2 = 1.35f * sigma * sigma;
        const int margin=is_wiener?0:18;

        parallel_for_rows(H, n_threads, [&](int y_t0, int y_t1, int tid) {
            float* __restrict b_acc = tls_acc[tid].data();
            float* __restrict b_wgt = tls_wgt[tid].data();
#if !defined(__aarch64__) && !defined(__AVX2__)
            float patch_n[64], patch_g[64], coeff_n[64], coeff_g[64], rec[64];
#endif

            for (int yb0 = y_t0; yb0 < y_t1; yb0 += BAND) {
                const int yb1 = std::min(y_t1, yb0 + BAND);
                const int bh = yb1 - yb0;
                const int ir_scan = (int)(std::upper_bound(r_starts.begin(), r_starts.end(), yb0 - 8) - r_starts.begin());

                for (int xb0 = 0; xb0 < W; xb0 += TILE_X) {
                    const int xb1 = std::min(W, xb0 + TILE_X);
                    const int tw = xb1 - xb0;
                    if(roi && roi->roi_y1>0 &&
                       (yb1<=roi->roi_y0-margin || yb0>=roi->roi_y1+margin ||
                        xb1<=roi->roi_x0-margin || xb0>=roi->roi_x1+margin)) {
                        for(int r=yb0;r<yb1;++r)
                            std::copy_n(src_in+(size_t)r*W+xb0,tw,dst_out+(size_t)r*W+xb0);
                        continue;
                    }
                    std::memset(b_acc, 0, (size_t)bh * STRIDE_X * sizeof(float));
                    std::memset(b_wgt, 0, (size_t)bh * STRIDE_X * sizeof(float));

                    const int ic_scan = (int)(std::upper_bound(c_starts.begin(), c_starts.end(), xb0 - 8) - c_starts.begin());

                    if (!is_wiener) {
#if defined(__AVX2__)
                        const __m256 abs_mask = _mm256_castsi256_ps(_mm256_set1_epi32(0x7fffffff));
                        const __m256 dc_force = _mm256_castsi256_ps(_mm256_setr_epi32(-1, 0, 0, 0, 0, 0, 0, 0));
#elif defined(__aarch64__)
                        const uint32x4_t dc_force = {0xFFFFFFFFu, 0u, 0u, 0u};
#endif
                        for (int ir = ir_scan; ir < NR; ++ir) {
                            const int r = r_starts[ir];
                            if (r >= yb1) break;
                            const int py0 = std::max(0, yb0 - r);
                            const int py1 = std::min(8, yb1 - r);
                            const int rh_b = (snr_boost_half && Hh > 0) ? clamp_i(((r + 4) * scale_mul) >> 2, 0, Hh - 1) : 0;

                            for (int ic = ic_scan; ic < NC; ++ic) {
                                const int c = c_starts[ic];
                                if (c >= xb1) break;
                                const int tx = c - xb0 + PAD_X;
                                float boost = 1.0f;
                                if (snr_boost_half && Hh > 0 && Wh > 0) {
                                    boost = snr_boost_half[(size_t)rh_b * Wh + clamp_i(((c + 4) * scale_mul) >> 2, 0, Wh - 1)];
                                }
                                const float thr_loc = thr * boost;
#if defined(__AVX2__)
                                const __m256 thr_v = _mm256_set1_ps(thr_loc);
                                __m256 rv[8];
                                for (int i = 0; i < 8; ++i) rv[i] = _mm256_loadu_ps(src_in + (size_t)(r + i) * W + c);
                                dct1d_cols8_avx2_fwd(rv);
                                transpose_8x8_avx2(rv);
                                dct1d_cols8_avx2_fwd(rv);

                                __m256 m0 = _mm256_or_ps(dc_force, _mm256_cmp_ps(_mm256_and_ps(rv[0], abs_mask), thr_v, _CMP_GE_OQ));
                                rv[0] = _mm256_and_ps(rv[0], m0);
                                __m256i cnt = _mm256_srli_epi32(_mm256_castps_si256(m0), 31);
                                for (int i = 1; i < 8; ++i) {
                                    __m256 mi = _mm256_cmp_ps(_mm256_and_ps(rv[i], abs_mask), thr_v, _CMP_GE_OQ);
                                    rv[i] = _mm256_and_ps(rv[i], mi);
                                    cnt = _mm256_add_epi32(cnt, _mm256_srli_epi32(_mm256_castps_si256(mi), 31));
                                }
                                __m128i c128 = _mm_add_epi32(_mm256_castsi256_si128(cnt), _mm256_extracti128_si256(cnt, 1));
                                c128 = _mm_add_epi32(c128, _mm_shuffle_epi32(c128, _MM_SHUFFLE(1, 0, 3, 2)));
                                c128 = _mm_add_epi32(c128, _mm_shuffle_epi32(c128, _MM_SHUFFLE(2, 3, 0, 1)));
                                float block_w = 1.0f / (float)_mm_cvtsi128_si32(c128);

                                dct1d_cols8_avx2_inv(rv);
                                transpose_8x8_avx2(rv);
                                dct1d_cols8_avx2_inv(rv);

                                __m256 bw_v = _mm256_set1_ps(block_w);
                                for (int py = py0; py < py1; ++py) {
                                    size_t boff = (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    _mm256_storeu_ps(b_acc + boff, _mm256_fmadd_ps(bw_v, rv[py], _mm256_loadu_ps(b_acc + boff)));
                                    _mm256_storeu_ps(b_wgt + boff, _mm256_add_ps(bw_v, _mm256_loadu_ps(b_wgt + boff)));
                                }
#elif defined(__aarch64__)
                                const float32x4_t thr_v = vdupq_n_f32(thr_loc);
                                float32x4_t lo[8], hi[8];
                                for (int i = 0; i < 8; ++i) {
                                    const float* srow = src_in + (size_t)(r + i) * W + c;
                                    lo[i] = vld1q_f32(srow);
                                    hi[i] = vld1q_f32(srow + 4);
                                }
                                dct1d_cols4_fwd(lo); dct1d_cols4_fwd(hi);
                                transpose_8x8_neon(lo, hi);
                                dct1d_cols4_fwd(lo); dct1d_cols4_fwd(hi);

                                uint32x4_t m0 = vorrq_u32(dc_force, vcgeq_f32(vabsq_f32(lo[0]), thr_v));
                                uint32x4_t h0 = vcgeq_f32(vabsq_f32(hi[0]), thr_v);
                                lo[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(lo[0]), m0));
                                hi[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(hi[0]), h0));
                                uint32x4_t cnt = vaddq_u32(vshrq_n_u32(m0, 31), vshrq_n_u32(h0, 31));
                                for (int i = 1; i < 8; ++i) {
                                    uint32x4_t ml = vcgeq_f32(vabsq_f32(lo[i]), thr_v);
                                    uint32x4_t mh = vcgeq_f32(vabsq_f32(hi[i]), thr_v);
                                    lo[i] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(lo[i]), ml));
                                    hi[i] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(hi[i]), mh));
                                    cnt = vaddq_u32(cnt, vaddq_u32(vshrq_n_u32(ml, 31), vshrq_n_u32(mh, 31)));
                                }
                                float block_w = 1.0f / (float)vaddvq_u32(cnt);

                                dct1d_cols4_inv(lo); dct1d_cols4_inv(hi);
                                transpose_8x8_neon(lo, hi);
                                dct1d_cols4_inv(lo); dct1d_cols4_inv(hi);

                                float32x4_t bw_v = vdupq_n_f32(block_w);
                                for (int py = py0; py < py1; ++py) {
                                    size_t boff = (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    vst1q_f32(b_acc + boff,     vfmaq_f32(vld1q_f32(b_acc + boff),     bw_v, lo[py]));
                                    vst1q_f32(b_acc + boff + 4, vfmaq_f32(vld1q_f32(b_acc + boff + 4), bw_v, hi[py]));
                                    vst1q_f32(b_wgt + boff,     vaddq_f32(vld1q_f32(b_wgt + boff),     bw_v));
                                    vst1q_f32(b_wgt + boff + 4, vaddq_f32(vld1q_f32(b_wgt + boff + 4), bw_v));
                                }
#else
                                for (int py = 0; py < 8; ++py) {
                                    const float* srow = src_in + (size_t)(r + py) * W + c;
                                    for (int px = 0; px < 8; ++px) patch_n[py * 8 + px] = srow[px];
                                }
                                dct8x8_fwd(patch_n, coeff_n);
                                int nz = 1;
                                for (int k = 1; k < 64; ++k) {
                                    if (std::fabs(coeff_n[k]) < thr_loc) coeff_n[k] = 0.0f;
                                    else ++nz;
                                }
                                float block_w = 1.0f / (float)nz;
                                dct8x8_inv(coeff_n, rec);
                                for (int py = py0; py < py1; ++py) {
                                    float* arow = b_acc + (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    float* wrow = b_wgt + (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    const float* rrow = rec + py * 8;
                                    for (int px = 0; px < 8; ++px) {
                                        arow[px] += block_w * rrow[px];
                                        wrow[px] += block_w;
                                    }
                                }
#endif
                            }
                        }
                    } else {
#if defined(__AVX2__)
                        const __m256 one_v = _mm256_set1_ps(1.0f);
                        const __m256 two_v = _mm256_set1_ps(2.0f);
#endif
                        for (int ir = ir_scan; ir < NR; ++ir) {
                            const int r = r_starts[ir];
                            if (r >= yb1) break;
                            const int py0 = std::max(0, yb0 - r);
                            const int py1 = std::min(8, yb1 - r);
                            const int rh_b = (snr_boost_half && Hh > 0) ? clamp_i(((r + 4) * scale_mul) >> 2, 0, Hh - 1) : 0;

                            for (int ic = ic_scan; ic < NC; ++ic) {
                                const int c = c_starts[ic];
                                if (c >= xb1) break;
                                const int tx = c - xb0 + PAD_X;
                                float boost = 1.0f;
                                if (snr_boost_half && Hh > 0 && Wh > 0) {
                                    boost = snr_boost_half[(size_t)rh_b * Wh + clamp_i(((c + 4) * scale_mul) >> 2, 0, Wh - 1)];
                                }
                                const float sig2_loc = sig2 * boost * boost;
#if defined(__AVX2__)
                                const __m256 sig2_v = _mm256_set1_ps(sig2_loc);
                                __m256 rv[8], gv[8];
                                for (int i = 0; i < 8; ++i) {
                                    size_t soff = (size_t)(r + i) * W + c;
                                    rv[i] = _mm256_loadu_ps(src_in + soff);
                                    gv[i] = _mm256_loadu_ps(guide_in + soff);
                                }
                                dct1d_cols8_avx2_fwd(rv); transpose_8x8_avx2(rv); dct1d_cols8_avx2_fwd(rv);
                                dct1d_cols8_avx2_fwd(gv); transpose_8x8_avx2(gv); dct1d_cols8_avx2_fwd(gv);

                                __m256 pg2_0 = _mm256_mul_ps(gv[0], gv[0]);
                                __m256 den0 = _mm256_add_ps(pg2_0, sig2_v);
                                __m256 rcp0 = _mm256_rcp_ps(den0);
                                rcp0 = _mm256_mul_ps(rcp0, _mm256_fnmadd_ps(den0, rcp0, two_v));
                                __m256 gw0 = _mm256_blend_ps(_mm256_mul_ps(pg2_0, rcp0), one_v, 0x01);
                                rv[0] = _mm256_mul_ps(rv[0], gw0);
                                __m256 w2_acc = _mm256_mul_ps(gw0, gw0);

                                for (int i = 1; i < 8; ++i) {
                                    __m256 pg2 = _mm256_mul_ps(gv[i], gv[i]);
                                    __m256 den = _mm256_add_ps(pg2, sig2_v);
                                    __m256 rcp = _mm256_rcp_ps(den);
                                    rcp = _mm256_mul_ps(rcp, _mm256_fnmadd_ps(den, rcp, two_v));
                                    __m256 gwi = _mm256_mul_ps(pg2, rcp);
                                    rv[i] = _mm256_mul_ps(rv[i], gwi);
                                    w2_acc = _mm256_fmadd_ps(gwi, gwi, w2_acc);
                                }
                                __m128 w4 = _mm_add_ps(_mm256_castps256_ps128(w2_acc), _mm256_extractf128_ps(w2_acc, 1));
                                w4 = _mm_hadd_ps(w4, w4);
                                w4 = _mm_hadd_ps(w4, w4);
                                float block_w = 1.0f / _mm_cvtss_f32(w4);

                                dct1d_cols8_avx2_inv(rv);
                                transpose_8x8_avx2(rv);
                                dct1d_cols8_avx2_inv(rv);

                                __m256 bw_v = _mm256_set1_ps(block_w);
                                for (int py = py0; py < py1; ++py) {
                                    size_t boff = (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    _mm256_storeu_ps(b_acc + boff, _mm256_fmadd_ps(bw_v, rv[py], _mm256_loadu_ps(b_acc + boff)));
                                    _mm256_storeu_ps(b_wgt + boff, _mm256_add_ps(bw_v, _mm256_loadu_ps(b_wgt + boff)));
                                }
#elif defined(__aarch64__)
                                const float32x4_t sig2_v = vdupq_n_f32(sig2_loc);
                                float32x4_t lo[8], hi[8], glo[8], ghi[8];
                                for (int i = 0; i < 8; ++i) {
                                    size_t soff = (size_t)(r + i) * W + c;
                                    lo[i]  = vld1q_f32(src_in + soff);
                                    hi[i]  = vld1q_f32(src_in + soff + 4);
                                    glo[i] = vld1q_f32(guide_in + soff);
                                    ghi[i] = vld1q_f32(guide_in + soff + 4);
                                }
                                dct1d_cols4_fwd(lo);  dct1d_cols4_fwd(hi);  transpose_8x8_neon(lo, hi);  dct1d_cols4_fwd(lo);  dct1d_cols4_fwd(hi);
                                dct1d_cols4_fwd(glo); dct1d_cols4_fwd(ghi); transpose_8x8_neon(glo, ghi); dct1d_cols4_fwd(glo); dct1d_cols4_fwd(ghi);

                                float32x4_t pgl0 = vmulq_f32(glo[0], glo[0]);
                                float32x4_t pgh0 = vmulq_f32(ghi[0], ghi[0]);
                                float32x4_t gwl0 = vsetq_lane_f32(1.0f, vdivq_f32(pgl0, vaddq_f32(pgl0, sig2_v)), 0);
                                float32x4_t gwh0 = vdivq_f32(pgh0, vaddq_f32(pgh0, sig2_v));
                                lo[0] = vmulq_f32(lo[0], gwl0);
                                hi[0] = vmulq_f32(hi[0], gwh0);
                                float32x4_t w2_acc = vfmaq_f32(vmulq_f32(gwl0, gwl0), gwh0, gwh0);

                                for (int i = 1; i < 8; ++i) {
                                    float32x4_t pgl = vmulq_f32(glo[i], glo[i]);
                                    float32x4_t pgh = vmulq_f32(ghi[i], ghi[i]);
                                    float32x4_t gwl = vdivq_f32(pgl, vaddq_f32(pgl, sig2_v));
                                    float32x4_t gwh = vdivq_f32(pgh, vaddq_f32(pgh, sig2_v));
                                    lo[i] = vmulq_f32(lo[i], gwl);
                                    hi[i] = vmulq_f32(hi[i], gwh);
                                    w2_acc = vfmaq_f32(vfmaq_f32(w2_acc, gwl, gwl), gwh, gwh);
                                }
                                float block_w = 1.0f / vaddvq_f32(w2_acc);

                                dct1d_cols4_inv(lo); dct1d_cols4_inv(hi);
                                transpose_8x8_neon(lo, hi);
                                dct1d_cols4_inv(lo); dct1d_cols4_inv(hi);

                                float32x4_t bw_v = vdupq_n_f32(block_w);
                                for (int py = py0; py < py1; ++py) {
                                    size_t boff = (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    vst1q_f32(b_acc + boff,     vfmaq_f32(vld1q_f32(b_acc + boff),     bw_v, lo[py]));
                                    vst1q_f32(b_acc + boff + 4, vfmaq_f32(vld1q_f32(b_acc + boff + 4), bw_v, hi[py]));
                                    vst1q_f32(b_wgt + boff,     vaddq_f32(vld1q_f32(b_wgt + boff),     bw_v));
                                    vst1q_f32(b_wgt + boff + 4, vaddq_f32(vld1q_f32(b_wgt + boff + 4), bw_v));
                                }
#else
                                for (int py = 0; py < 8; ++py) {
                                    const float* srow = src_in + (size_t)(r + py) * W + c;
                                    const float* grow = guide_in + (size_t)(r + py) * W + c;
                                    for (int px = 0; px < 8; ++px) {
                                        patch_n[py * 8 + px] = srow[px];
                                        patch_g[py * 8 + px] = grow[px];
                                    }
                                }
                                dct8x8_fwd(patch_n, coeff_n);
                                dct8x8_fwd(patch_g, coeff_g);
                                float w2_sum = 1.0f;
                                for (int k = 1; k < 64; ++k) {
                                    float pg2 = coeff_g[k] * coeff_g[k];
                                    float g_w = pg2 / (pg2 + sig2_loc);
                                    coeff_n[k] *= g_w;
                                    w2_sum += g_w * g_w;
                                }
                                float block_w = 1.0f / w2_sum;
                                dct8x8_inv(coeff_n, rec);
                                for (int py = py0; py < py1; ++py) {
                                    float* arow = b_acc + (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    float* wrow = b_wgt + (size_t)(r + py - yb0) * STRIDE_X + tx;
                                    const float* rrow = rec + py * 8;
                                    for (int px = 0; px < 8; ++px) {
                                        arow[px] += block_w * rrow[px];
                                        wrow[px] += block_w;
                                    }
                                }
#endif
                            }
                        }
                    }

                    for (int r = yb0; r < yb1; ++r) {
                        float* __restrict drow = dst_out + (size_t)r * W + xb0;
                        const float* __restrict arow = b_acc + (size_t)(r - yb0) * STRIDE_X + PAD_X;
                        const float* __restrict wrow = b_wgt + (size_t)(r - yb0) * STRIDE_X + PAD_X;
                        const float* __restrict srow = src_in + (size_t)r * W + xb0;
                        int i = 0;
#if defined(__AVX2__)
                        const __m256 eps_v = _mm256_set1_ps(1e-12f);
                        for (; i + 8 <= tw; i += 8) {
                            const __m256 wv = _mm256_loadu_ps(wrow + i);
                            const __m256 m = _mm256_cmp_ps(wv, eps_v, _CMP_GT_OQ);
                            const __m256 q = _mm256_div_ps(_mm256_loadu_ps(arow + i), _mm256_max_ps(wv, eps_v));
                            _mm256_storeu_ps(drow + i, _mm256_blendv_ps(_mm256_loadu_ps(srow + i), q, m));
                        }
#elif defined(__aarch64__)
                        const float32x4_t eps_v = vdupq_n_f32(1e-12f);
                        for (; i + 4 <= tw; i += 4) {
                            const float32x4_t wv = vld1q_f32(wrow + i);
                            const uint32x4_t m = vcgtq_f32(wv, eps_v);
                            const float32x4_t q = vdivq_f32(vld1q_f32(arow + i), vmaxq_f32(wv, eps_v));
                            vst1q_f32(drow + i, vbslq_f32(m, q, vld1q_f32(srow + i)));
                        }
#endif
                        for (; i < tw; ++i) {
                            drow[i] = (wrow[i] > 1e-12f) ? (arow[i] / wrow[i]) : srow[i];
                        }
                    }
                }
            }
        }, BAND);
    };

    run_pass(false, noisy, nullptr, pilot.data());
    run_pass(true, noisy, pilot.data(), denoised);
}

static inline void upsample_bilinear_2x_row(const float* src, int Hs, int Ws, int Hd, int Wd, int r, float* drow, float* vbuf) {
    if (Hd == 2 * Hs && Wd == 2 * Ws && Hs >= 2 && Ws >= 2) {
        const int hs0 = clamp_i((r - 1) >> 1, 0, Hs - 1);
        const int hs1 = clamp_i((r + 1) >> 1, 0, Hs - 1);
        const float wy0 = (r & 1) ? 0.75f : 0.25f;
        const float wy1 = 1.0f - wy0;
        const float* __restrict sr0 = src + (size_t)hs0 * Ws;
        const float* __restrict sr1 = src + (size_t)hs1 * Ws;
        int x = 0;
#if defined(__AVX2__)
        const __m256 wy0_v = _mm256_set1_ps(wy0), wy1_v = _mm256_set1_ps(wy1);
        for (; x + 8 <= Ws; x += 8) {
            _mm256_storeu_ps(vbuf + x, _mm256_fmadd_ps(wy0_v, _mm256_loadu_ps(sr0 + x), _mm256_mul_ps(wy1_v, _mm256_loadu_ps(sr1 + x))));
        }
#elif defined(__aarch64__)
        const float32x4_t wy0_v = vdupq_n_f32(wy0), wy1_v = vdupq_n_f32(wy1);
        for (; x + 4 <= Ws; x += 4) {
            vst1q_f32(vbuf + x, vfmaq_f32(vmulq_f32(wy1_v, vld1q_f32(sr1 + x)), wy0_v, vld1q_f32(sr0 + x)));
        }
#endif
        for (; x < Ws; ++x) vbuf[x] = wy0 * sr0[x] + wy1 * sr1[x];

        drow[0] = vbuf[0];
        int k = 0;
#if defined(__aarch64__)
        const float32x4_t w3 = vdupq_n_f32(0.75f), w1 = vdupq_n_f32(0.25f);
        for (; k + 4 <= Ws - 1; k += 4) {
            const float32x4_t a = vld1q_f32(vbuf + k);
            const float32x4_t b = vld1q_f32(vbuf + k + 1);
            float32x4x2_t p;
            p.val[0] = vfmaq_f32(vmulq_f32(w1, b), w3, a);
            p.val[1] = vfmaq_f32(vmulq_f32(w3, b), w1, a);
            vst2q_f32(drow + 2 * k + 1, p);
        }
#elif defined(__AVX2__)
        const __m256 w3 = _mm256_set1_ps(0.75f), w1 = _mm256_set1_ps(0.25f);
        for (; k + 8 <= Ws - 1; k += 8) {
            const __m256 a = _mm256_loadu_ps(vbuf + k);
            const __m256 b = _mm256_loadu_ps(vbuf + k + 1);
            const __m256 odd  = _mm256_fmadd_ps(w3, a, _mm256_mul_ps(w1, b));
            const __m256 even = _mm256_fmadd_ps(w1, a, _mm256_mul_ps(w3, b));
            const __m256 lo = _mm256_unpacklo_ps(odd, even);
            const __m256 hi = _mm256_unpackhi_ps(odd, even);
            _mm256_storeu_ps(drow + 2 * k + 1, _mm256_permute2f128_ps(lo, hi, 0x20));
            _mm256_storeu_ps(drow + 2 * k + 9, _mm256_permute2f128_ps(lo, hi, 0x31));
        }
#endif
        for (; k < Ws - 1; ++k) {
            drow[2 * k + 1] = 0.75f * vbuf[k] + 0.25f * vbuf[k + 1];
            drow[2 * k + 2] = 0.25f * vbuf[k] + 0.75f * vbuf[k + 1];
        }
        drow[Wd - 1] = vbuf[Ws - 1];
        return;
    }
    const float ys = ((float)r + 0.5f) * 0.5f - 0.5f;
    for (int c = 0; c < Wd; ++c) {
        const float xs = ((float)c + 0.5f) * 0.5f - 0.5f;
        drow[c] = bilerp_clamped(src, Hs, Ws, ys, xs);
    }
}

[[maybe_unused]] static void upsample_bilinear_2x(const float* src, int Hs, int Ws, float* dst, int Hd, int Wd, int n_threads) {
    const int max_nt = std::max(1, n_threads);
    std::vector<std::vector<float>> tls_v(max_nt, std::vector<float>((size_t)std::max(1, Ws)));
    parallel_for_rows(Hd, n_threads, [&](int r0, int r1, int tid) {
        float* vbuf = tls_v[tid].data();
        for (int r = r0; r < r1; ++r) {
            upsample_bilinear_2x_row(src, Hs, Ws, Hd, Wd, r, dst + (size_t)r * Wd, vbuf);
        }
    });
}

// 5-Level Multiscale DCT Empirical Wiener Pyramid + Edge-Stopped GAT Luminance Polish
static void multiscale_dct_denoise_gat(
    float* z_img, int H, int W, int n_threads, float denoise_strength = 1.0f, bool enable_edge_polish = true,
    const float* snr_boost_half = nullptr, int Hh = 0, int Wh = 0,
    const heraw_gpu::Stage45GpuJob* job = nullptr,
    const float* z_src_readonly = nullptr,
    UninitBuf<float>* ext_pilot = nullptr,
    int base_scale_mul = 1
) {
    if (denoise_strength <= 1e-4f) {
        if (z_src_readonly && z_src_readonly != z_img)
            std::memcpy(z_img, z_src_readonly, (size_t)H * W * sizeof(float));
        return;
    }
    const int L = 5;
    std::vector<int> Hs(L), Ws(L);
    std::vector<UninitBuf<float>> pyr(L), den(L);
    const float base_sigmas[5] = {1.35f, 1.15f, 0.78f, 0.48f, 0.28f};
    float sigmas[5];
    for (int i = 0; i < L; ++i) {
        sigmas[i] = base_sigmas[i] * denoise_strength * (job ? job->dct_scale_mul[i] : 1.0f);
    }

    Hs[0] = H; Ws[0] = W;
    const float* pyr0_ptr = z_src_readonly;
    if (!pyr0_ptr) {
        pyr[0].resize((size_t)H * W);
        std::memcpy(pyr[0].data(), z_img, (size_t)H * W * sizeof(float));
        pyr0_ptr = pyr[0].data();
    }

    for (int s = 1; s < L; ++s) {
        Hs[s] = Hs[s - 1] / 2;
        Ws[s] = Ws[s - 1] / 2;
        pyr[s].resize((size_t)Hs[s] * Ws[s]);
        const float* prev_p = (s == 1) ? pyr0_ptr : pyr[s - 1].data();
        box_downsample_2x(prev_p, Hs[s - 1], Ws[s - 1], pyr[s].data(), n_threads);
    }

    const int max_nt = std::max(1, n_threads);
    std::vector<std::vector<float>> tls_up(max_nt, std::vector<float>((size_t)W + (size_t)std::max(1, W / 2)));
    UninitBuf<float> down_cur, own_pilot;
    UninitBuf<float>& shared_pilot = ext_pilot ? *ext_pilot : own_pilot;

    for (int s = L - 1; s >= 0; --s) {
        float* dst_s = (s == 0) ? z_img : nullptr;
        if (s > 0) {
            den[s].resize((size_t)Hs[s] * Ws[s]);
            dst_s = den[s].data();
        }
        const float* src_s = (s == 0) ? pyr0_ptr : pyr[s].data();
        const int stride = 2;
        const int eff_scale_mul = std::max(1, base_scale_mul) << s;
        heraw_gpu::PC_DctWiener roi{Hs[s], Ws[s], Hh, Wh, 1, eff_scale_mul, sigmas[s]};
        if (job) heraw_gpu::dct_dependency_roi(roi, *job);
        dct_wiener_single_scale(src_s, dst_s, Hs[s], Ws[s], sigmas[s], stride, n_threads,
                                snr_boost_half, Hh, Wh, eff_scale_mul, job ? &roi : nullptr, &shared_pilot);
        if (s > 0) pyr[s].release();
        else       pyr[0].release();

        if (s < L - 1) {
            const size_t n_coarse = (size_t)Hs[s + 1] * Ws[s + 1];
            down_cur.resize(n_coarse);
            box_downsample_2x(dst_s, Hs[s], Ws[s], down_cur.data(), n_threads);
            const float* __restrict prev_den = den[s + 1].data();
            float* __restrict dc_ptr = down_cur.data();
            parallel_for_rows(Hs[s + 1], n_threads, [&](int r0, int r1, int) {
                const size_t i0 = (size_t)r0 * Ws[s + 1];
                const size_t i1 = (size_t)r1 * Ws[s + 1];
                for (size_t i = i0; i < i1; ++i) dc_ptr[i] = prev_den[i] - dc_ptr[i];
            });
            den[s + 1].release();

            const float edge_tau = std::max(1e-4f, 3.4f * sigmas[s]);
            const float tau2 = edge_tau * edge_tau;
            parallel_for_rows(Hs[s], n_threads, [&](int r0, int r1, int tid) {
                float* up_row = tls_up[tid].data();
                float* vbuf = up_row + Ws[s];
                for (int r = r0; r < r1; ++r) {
                    upsample_bilinear_2x_row(dc_ptr, Hs[s + 1], Ws[s + 1], Hs[s], Ws[s], r, up_row, vbuf);
                    float* __restrict drow = dst_s + (size_t)r * Ws[s];
                    int c = 0;
#if defined(__AVX2__)
                    const __m256 tau2_v = _mm256_set1_ps(tau2), gain_v = _mm256_set1_ps(0.92f * tau2);
                    for (; c + 8 <= Ws[s]; c += 8) {
                        const __m256 d = _mm256_loadu_ps(up_row + c);
                        const __m256 num = _mm256_mul_ps(gain_v, d);
                        const __m256 den_v = _mm256_fmadd_ps(d, d, tau2_v);
                        _mm256_storeu_ps(drow + c, _mm256_add_ps(_mm256_loadu_ps(drow + c), _mm256_div_ps(num, den_v)));
                    }
#elif defined(__aarch64__)
                    const float32x4_t tau2_v = vdupq_n_f32(tau2), gain_v = vdupq_n_f32(0.92f * tau2);
                    for (; c + 4 <= Ws[s]; c += 4) {
                        const float32x4_t d = vld1q_f32(up_row + c);
                        const float32x4_t num = vmulq_f32(gain_v, d);
                        const float32x4_t den_v = vfmaq_f32(tau2_v, d, d);
                        vst1q_f32(drow + c, vaddq_f32(vld1q_f32(drow + c), vdivq_f32(num, den_v)));
                    }
#endif
                    for (; c < Ws[s]; ++c) {
                        const float d = up_row[c];
                        const float gate = tau2 / (tau2 + d * d);
                        drow[c] += 0.92f * gate * d;
                    }
                }
            });
        }
    }
    down_cur.release();

    float* cur = z_img;
    if (enable_edge_polish && H >= 1 && W >= 1) {
        const float k1d[5] = {1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
        const float inv_2s2 = 1.0f / (2.0f * (1.05f * 1.05f));
        const int nt_ep = std::max(1, std::min(n_threads, (H + 15) / 16));
        const int rows_per = (H + nt_ep - 1) / nt_ep;
        int actual_nt = 0;
        for (int t = 0; t < nt_ep; ++t) if (t * rows_per < H) ++actual_nt;

        // Snapshot 3 top + 3 bottom boundary halo rows per thread strip (~200KB/thread)
        // so Pass 1 + Pass 2 fuse into a 5-row L2 ring buffer with zero 731MB tmp allocation.
        std::vector<std::vector<float>> halo_top(actual_nt), halo_bot(actual_nt);
        std::vector<int> top_y0(actual_nt, 0), bot_y0(actual_nt, H);
        for (int t = 0; t < actual_nt; ++t) {
            const int r0 = t * rows_per, r1 = std::min(H, r0 + rows_per);
            if (r0 > 0) {
                top_y0[t] = std::max(0, r0 - 3);
                const int cnt = r0 - top_y0[t];
                halo_top[t].resize((size_t)cnt * W);
                std::memcpy(halo_top[t].data(), cur + (size_t)top_y0[t] * W, (size_t)cnt * W * sizeof(float));
            }
            if (r1 < H) {
                bot_y0[t] = r1;
                const int cnt = std::min(H, r1 + 3) - r1;
                halo_bot[t].resize((size_t)cnt * W);
                std::memcpy(halo_bot[t].data(), cur + (size_t)r1 * W, (size_t)cnt * W * sizeof(float));
            }
        }
        std::vector<std::vector<float>> rings(actual_nt, std::vector<float>((size_t)5 * W));

        auto ep_h_row = [&](const float* rm, const float* r0p, const float* rp, float* dst) {
            auto scalar_px = [&](int c) {
                const int cm = clamp_i(c - 1, 0, W - 1), cp = clamp_i(c + 1, 0, W - 1);
                const float v0 = r0p[c];
                const float gx = r0p[cp] - r0p[cm];
                const float gy = rp[c] - rm[c];
                if (gx * gx + gy * gy > 3.2f) { dst[c] = v0; return; }
                float ws = 0.0f, vs = 0.0f;
                for (int k = -2; k <= 2; ++k) {
                    const float vk = r0p[clamp_i(c + k, 0, W - 1)];
                    const float d = vk - v0;
                    const float w = k1d[k + 2] * fast_neg_exp(d * d * inv_2s2);
                    ws += w; vs += w * vk;
                }
                dst[c] = vs / std::max(ws, 1e-12f);
            };
            for (int c = 0; c < std::min(2, W); ++c) scalar_px(c);
            int c = 2;
#if defined(__AVX2__)
            const __m256 thr_v = _mm256_set1_ps(3.2f), is2_v = _mm256_set1_ps(inv_2s2), eps_v = _mm256_set1_ps(1e-12f);
            const __m256 one_v = _mm256_set1_ps(1.0f), c2_v = _mm256_set1_ps(0.48f), c3_v = _mm256_set1_ps(0.235f), lim_v = _mm256_set1_ps(12.0f);
            const __m256 kw[5] = {_mm256_set1_ps(k1d[0]), _mm256_set1_ps(k1d[1]), _mm256_set1_ps(k1d[2]), _mm256_set1_ps(k1d[3]), _mm256_set1_ps(k1d[4])};
            for (; c + 8 <= W - 2; c += 8) {
                const __m256 v0 = _mm256_loadu_ps(r0p + c);
                const __m256 gx = _mm256_sub_ps(_mm256_loadu_ps(r0p + c + 1), _mm256_loadu_ps(r0p + c - 1));
                const __m256 gy = _mm256_sub_ps(_mm256_loadu_ps(rp + c), _mm256_loadu_ps(rm + c));
                const __m256 edge_m = _mm256_cmp_ps(_mm256_fmadd_ps(gx, gx, _mm256_mul_ps(gy, gy)), thr_v, _CMP_GT_OQ);
                if (_mm256_movemask_ps(edge_m) == 0xFF) {
                    _mm256_storeu_ps(dst + c, v0);
                    continue;
                }
                __m256 ws = kw[2];
                __m256 vs = _mm256_mul_ps(kw[2], v0);
                for (int k : {-2, -1, 1, 2}) {
                    const __m256 vk = _mm256_loadu_ps(r0p + c + k);
                    const __m256 d = _mm256_sub_ps(vk, v0);
                    const __m256 x = _mm256_mul_ps(_mm256_mul_ps(d, d), is2_v);
                    const __m256 den = _mm256_add_ps(one_v, _mm256_mul_ps(x, _mm256_add_ps(one_v, _mm256_mul_ps(x, _mm256_fmadd_ps(c3_v, x, c2_v)))));
                    const __m256 w = _mm256_and_ps(_mm256_cmp_ps(x, lim_v, _CMP_LT_OQ), _mm256_div_ps(kw[k + 2], den));
                    ws = _mm256_add_ps(ws, w);
                    vs = _mm256_fmadd_ps(w, vk, vs);
                }
                const __m256 sm = _mm256_div_ps(vs, _mm256_max_ps(ws, eps_v));
                _mm256_storeu_ps(dst + c, _mm256_blendv_ps(sm, v0, edge_m));
            }
#elif defined(__aarch64__)
            const float32x4_t thr_v = vdupq_n_f32(3.2f), is2_v = vdupq_n_f32(inv_2s2), eps_v = vdupq_n_f32(1e-12f);
            const float32x4_t one_v = vdupq_n_f32(1.0f), c2_v = vdupq_n_f32(0.48f), c3_v = vdupq_n_f32(0.235f);
            const float32x4_t lim_v = vdupq_n_f32(12.0f), zero_v = vdupq_n_f32(0.0f);
            const float32x4_t kw[5] = {vdupq_n_f32(k1d[0]), vdupq_n_f32(k1d[1]), vdupq_n_f32(k1d[2]), vdupq_n_f32(k1d[3]), vdupq_n_f32(k1d[4])};
            for (; c + 4 <= W - 2; c += 4) {
                const float32x4_t v0 = vld1q_f32(r0p + c);
                const float32x4_t gx = vsubq_f32(vld1q_f32(r0p + c + 1), vld1q_f32(r0p + c - 1));
                const float32x4_t gy = vsubq_f32(vld1q_f32(rp + c), vld1q_f32(rm + c));
                const uint32x4_t edge_m = vcgtq_f32(vfmaq_f32(vmulq_f32(gy, gy), gx, gx), thr_v);
                if (vminvq_u32(edge_m) == 0xFFFFFFFFu) {
                    vst1q_f32(dst + c, v0);
                    continue;
                }
                float32x4_t ws = kw[2];
                float32x4_t vs = vmulq_f32(kw[2], v0);
                for (int k : {-2, -1, 1, 2}) {
                    const float32x4_t vk = vld1q_f32(r0p + c + k);
                    const float32x4_t d = vsubq_f32(vk, v0);
                    const float32x4_t x = vmulq_f32(vmulq_f32(d, d), is2_v);
                    const float32x4_t den = vaddq_f32(one_v, vmulq_f32(x, vaddq_f32(one_v, vmulq_f32(x, vfmaq_f32(c2_v, c3_v, x)))));
                    const float32x4_t w = vbslq_f32(vcltq_f32(x, lim_v), vdivq_f32(kw[k + 2], den), zero_v);
                    ws = vaddq_f32(ws, w);
                    vs = vfmaq_f32(vs, w, vk);
                }
                const float32x4_t sm = vdivq_f32(vs, vmaxq_f32(ws, eps_v));
                vst1q_f32(dst + c, vbslq_f32(edge_m, v0, sm));
            }
#endif
            for (; c < W; ++c) scalar_px(c);
        };

        auto ep_v_row = [&](const float* tm2, const float* tm1, const float* t0, const float* tp1, const float* tp2, float* dst) {
            const float* trows[5] = {tm2, tm1, t0, tp1, tp2};
            auto scalar_px = [&](int c) {
                const int cm = clamp_i(c - 1, 0, W - 1), cp = clamp_i(c + 1, 0, W - 1);
                const float v0 = t0[c];
                const float gx = t0[cp] - t0[cm];
                const float gy = tp1[c] - tm1[c];
                if (gx * gx + gy * gy > 3.2f) { dst[c] = v0; return; }
                float ws = 0.0f, vs = 0.0f;
                for (int k = 0; k < 5; ++k) {
                    const float vk = trows[k][c];
                    const float d = vk - v0;
                    const float w = k1d[k] * fast_neg_exp(d * d * inv_2s2);
                    ws += w; vs += w * vk;
                }
                dst[c] = vs / std::max(ws, 1e-12f);
            };
            if (W > 0) scalar_px(0);
            int c = 1;
#if defined(__AVX2__)
            const __m256 thr_v = _mm256_set1_ps(3.2f), is2_v = _mm256_set1_ps(inv_2s2), eps_v = _mm256_set1_ps(1e-12f);
            const __m256 one_v = _mm256_set1_ps(1.0f), c2_v = _mm256_set1_ps(0.48f), c3_v = _mm256_set1_ps(0.235f), lim_v = _mm256_set1_ps(12.0f);
            const __m256 kw[5] = {_mm256_set1_ps(k1d[0]), _mm256_set1_ps(k1d[1]), _mm256_set1_ps(k1d[2]), _mm256_set1_ps(k1d[3]), _mm256_set1_ps(k1d[4])};
            for (; c + 8 <= W - 1; c += 8) {
                const __m256 v0 = _mm256_loadu_ps(t0 + c);
                const __m256 gx = _mm256_sub_ps(_mm256_loadu_ps(t0 + c + 1), _mm256_loadu_ps(t0 + c - 1));
                const __m256 gy = _mm256_sub_ps(_mm256_loadu_ps(tp1 + c), _mm256_loadu_ps(tm1 + c));
                const __m256 edge_m = _mm256_cmp_ps(_mm256_fmadd_ps(gx, gx, _mm256_mul_ps(gy, gy)), thr_v, _CMP_GT_OQ);
                if (_mm256_movemask_ps(edge_m) == 0xFF) {
                    _mm256_storeu_ps(dst + c, v0);
                    continue;
                }
                __m256 ws = kw[2];
                __m256 vs = _mm256_mul_ps(kw[2], v0);
                for (int k : {0, 1, 3, 4}) {
                    const __m256 vk = _mm256_loadu_ps(trows[k] + c);
                    const __m256 d = _mm256_sub_ps(vk, v0);
                    const __m256 x = _mm256_mul_ps(_mm256_mul_ps(d, d), is2_v);
                    const __m256 den = _mm256_add_ps(one_v, _mm256_mul_ps(x, _mm256_add_ps(one_v, _mm256_mul_ps(x, _mm256_fmadd_ps(c3_v, x, c2_v)))));
                    const __m256 w = _mm256_and_ps(_mm256_cmp_ps(x, lim_v, _CMP_LT_OQ), _mm256_div_ps(kw[k], den));
                    ws = _mm256_add_ps(ws, w);
                    vs = _mm256_fmadd_ps(w, vk, vs);
                }
                const __m256 sm = _mm256_div_ps(vs, _mm256_max_ps(ws, eps_v));
                _mm256_storeu_ps(dst + c, _mm256_blendv_ps(sm, v0, edge_m));
            }
#elif defined(__aarch64__)
            const float32x4_t thr_v = vdupq_n_f32(3.2f), is2_v = vdupq_n_f32(inv_2s2), eps_v = vdupq_n_f32(1e-12f);
            const float32x4_t one_v = vdupq_n_f32(1.0f), c2_v = vdupq_n_f32(0.48f), c3_v = vdupq_n_f32(0.235f);
            const float32x4_t lim_v = vdupq_n_f32(12.0f), zero_v = vdupq_n_f32(0.0f);
            const float32x4_t kw[5] = {vdupq_n_f32(k1d[0]), vdupq_n_f32(k1d[1]), vdupq_n_f32(k1d[2]), vdupq_n_f32(k1d[3]), vdupq_n_f32(k1d[4])};
            for (; c + 4 <= W - 1; c += 4) {
                const float32x4_t v0 = vld1q_f32(t0 + c);
                const float32x4_t gx = vsubq_f32(vld1q_f32(t0 + c + 1), vld1q_f32(t0 + c - 1));
                const float32x4_t gy = vsubq_f32(vld1q_f32(tp1 + c), vld1q_f32(tm1 + c));
                const uint32x4_t edge_m = vcgtq_f32(vfmaq_f32(vmulq_f32(gy, gy), gx, gx), thr_v);
                if (vminvq_u32(edge_m) == 0xFFFFFFFFu) {
                    vst1q_f32(dst + c, v0);
                    continue;
                }
                float32x4_t ws = kw[2];
                float32x4_t vs = vmulq_f32(kw[2], v0);
                for (int k : {0, 1, 3, 4}) {
                    const float32x4_t vk = vld1q_f32(trows[k] + c);
                    const float32x4_t d = vsubq_f32(vk, v0);
                    const float32x4_t x = vmulq_f32(vmulq_f32(d, d), is2_v);
                    const float32x4_t den = vaddq_f32(one_v, vmulq_f32(x, vaddq_f32(one_v, vmulq_f32(x, vfmaq_f32(c2_v, c3_v, x)))));
                    const float32x4_t w = vbslq_f32(vcltq_f32(x, lim_v), vdivq_f32(kw[k], den), zero_v);
                    ws = vaddq_f32(ws, w);
                    vs = vfmaq_f32(vs, w, vk);
                }
                const float32x4_t sm = vdivq_f32(vs, vmaxq_f32(ws, eps_v));
                vst1q_f32(dst + c, vbslq_f32(edge_m, v0, sm));
            }
#endif
            for (; c < W; ++c) scalar_px(c);
        };

        parallel_for_rows(actual_nt, actual_nt, [&](int t0_i, int t1_i, int) {
            for (int t = t0_i; t < t1_i; ++t) {
                const int r0 = t * rows_per, r1 = std::min(H, r0 + rows_per);
                float* ring = rings[t].data();
                auto get_src = [&](int y) -> const float* {
                    y = clamp_i(y, 0, H - 1);
                    if (y < r0) return halo_top[t].data() + (size_t)(y - top_y0[t]) * W;
                    if (y >= r1) return halo_bot[t].data() + (size_t)(y - bot_y0[t]) * W;
                    return cur + (size_t)y * W;
                };
                const int init_h0 = std::max(0, r0 - 2);
                const int init_h1 = std::min(H - 1, r0 + 1);
                for (int hr = init_h0; hr <= init_h1; ++hr) {
                    ep_h_row(get_src(hr - 1), get_src(hr), get_src(hr + 1), ring + (size_t)(hr % 5) * W);
                }
                int max_computed = init_h1;
                for (int r = r0; r < r1; ++r) {
                    const int need_hr = std::min(H - 1, r + 2);
                    while (max_computed < need_hr) {
                        ++max_computed;
                        ep_h_row(get_src(max_computed - 1), get_src(max_computed), get_src(max_computed + 1),
                                 ring + (size_t)(max_computed % 5) * W);
                    }
                    ep_v_row(ring + (size_t)(clamp_i(r - 2, 0, H - 1) % 5) * W,
                             ring + (size_t)(clamp_i(r - 1, 0, H - 1) % 5) * W,
                             ring + (size_t)(r % 5) * W,
                             ring + (size_t)(clamp_i(r + 1, 0, H - 1) % 5) * W,
                             ring + (size_t)(clamp_i(r + 2, 0, H - 1) % 5) * W,
                             cur + (size_t)r * W);
                }
            }
        }, 1);
    }
}

// 6-Level Separable Luminance-Guided Stationary A-Trous Chroma Denoiser on (R-G, B-G)
static void atrous_chroma_denoise(
    float* dR, float* dB, const float* guide_G, int H, int W, float chroma_noise_std, int n_threads,
    const float* snr_boost_half = nullptr, int Hh = 0, int Wh = 0, bool is_astro = false
) {
    if (chroma_noise_std <= 1e-6f) return;
    float sig_r = std::max(1e-5f, chroma_noise_std);
    float sig_b = std::max(1e-5f, chroma_noise_std);
    float sig_g = std::max(1e-4f, 0.5f * chroma_noise_std);
    std::vector<float> g_sub;
    for (int r = 0; r < H; r += 8) {
        for (int c = 0; c + 2 < W; c += 8) {
            g_sub.push_back(std::fabs(guide_G[(size_t)r * W + c + 2] - guide_G[(size_t)r * W + c]));
        }
    }
    float g_edge_scale = std::max(is_astro ? 0.14f : (3.0f * sig_g), 2.2f * quick_percentile(g_sub, 0.80f) + 0.006f);

    const float k1d[5] = {1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
    std::vector<float> tmpR((size_t)H * W), tmpB((size_t)H * W);

    for (int lv = 0; lv < 6; ++lv) {
        const int step = 1 << lv;
        // Noise std drops rapidly after fine levels -> tighten self-chroma edge stop at coarse scales to preserve isoluminant colors (e.g. gold on gray)!
        const float lv_decay = std::pow(0.56f, (float)lv);
        const float inv_sr2 = 1.0f / (2.0f * std::pow(2.7f * sig_r * lv_decay + 0.0015f, 2.0f));
        const float inv_sb2 = 1.0f / (2.0f * std::pow(2.7f * sig_b * lv_decay + 0.0015f, 2.0f));
        const float inv_sg2 = 1.0f / (2.0f * g_edge_scale * g_edge_scale);

        // Horizontal 1D guided pass: (dR, dB) -> (tmpR, tmpB)
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                const size_t roff = (size_t)r * W;
                const int rh = (snr_boost_half && Hh > 0) ? clamp_i(r >> 1, 0, Hh - 1) : 0;
                for (int c = 0; c < W; ++c) {
                    float inv_b2 = 1.0f;
                    if (snr_boost_half && Hh > 0 && Wh > 0) {
                        float b = snr_boost_half[(size_t)rh * Wh + clamp_i(c >> 1, 0, Wh - 1)];
                        inv_b2 = 1.0f / (b * b);
                    }
                    const float isr2 = inv_sr2 * inv_b2, isb2 = inv_sb2 * inv_b2;
                    float cr0 = dR[roff + c], cb0 = dB[roff + c], cg0 = guide_G[roff + c];
                    float wr_s = 0.0f, vr_s = 0.0f, wb_s = 0.0f, vb_s = 0.0f;
                    for (int kx = -2; kx <= 2; ++kx) {
                        int cc = clamp_i(c + kx * step, 0, W - 1);
                        float dg = guide_G[roff + cc] - cg0;
                        float wg = k1d[kx + 2] * fast_neg_exp(dg * dg * inv_sg2);
                        float vr = dR[roff + cc], dr = vr - cr0;
                        float wr = wg * fast_neg_exp(dr * dr * isr2);
                        wr_s += wr; vr_s += wr * vr;
                        float vb = dB[roff + cc], db = vb - cb0;
                        float wb = wg * fast_neg_exp(db * db * isb2);
                        wb_s += wb; vb_s += wb * vb;
                    }
                    tmpR[roff + c] = vr_s / std::max(wr_s, 1e-12f);
                    tmpB[roff + c] = vb_s / std::max(wb_s, 1e-12f);
                }
            }
        });

        // Vertical 1D guided pass: (tmpR, tmpB) -> (dR, dB)
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                const size_t roff = (size_t)r * W;
                const int rh = (snr_boost_half && Hh > 0) ? clamp_i(r >> 1, 0, Hh - 1) : 0;
                for (int c = 0; c < W; ++c) {
                    float inv_b2 = 1.0f;
                    if (snr_boost_half && Hh > 0 && Wh > 0) {
                        float b = snr_boost_half[(size_t)rh * Wh + clamp_i(c >> 1, 0, Wh - 1)];
                        inv_b2 = 1.0f / (b * b);
                    }
                    const float isr2 = inv_sr2 * inv_b2, isb2 = inv_sb2 * inv_b2;
                    float cr0 = tmpR[roff + c], cb0 = tmpB[roff + c], cg0 = guide_G[roff + c];
                    float wr_s = 0.0f, vr_s = 0.0f, wb_s = 0.0f, vb_s = 0.0f;
                    for (int ky = -2; ky <= 2; ++ky) {
                        int rr = clamp_i(r + ky * step, 0, H - 1);
                        size_t koff = (size_t)rr * W + c;
                        float dg = guide_G[koff] - cg0;
                        float wg = k1d[ky + 2] * fast_neg_exp(dg * dg * inv_sg2);
                        float vr = tmpR[koff], dr = vr - cr0;
                        float wr = wg * fast_neg_exp(dr * dr * isr2);
                        wr_s += wr; vr_s += wr * vr;
                        float vb = tmpB[koff], db = vb - cb0;
                        float wb = wg * fast_neg_exp(db * db * isb2);
                        wb_s += wb; vb_s += wb * vb;
                    }
                    dR[roff + c] = vr_s / std::max(wr_s, 1e-12f);
                    dB[roff + c] = vb_s / std::max(wb_s, 1e-12f);
                }
            }
        });
    }
}

struct RlWorkspace {
    UninitBuf<float> lum0, est, conv, ratio, edge_gate, lmin_bound, lmax_bound;
    void ensure(size_t npx) {
        lum0.resize(npx);
        est.resize(npx);
        conv.resize(npx);
        ratio.resize(npx);
        edge_gate.resize(npx);
        lmin_bound.resize(npx);
        lmax_bound.resize(npx);
    }
    void release() {
        lum0.release(); est.release(); conv.release(); ratio.release();
        edge_gate.release(); lmin_bound.release(); lmax_bound.release();
    }
};

// Halo-Bounded, Fine-Edge-Coherence & SNR-Gated Richardson-Lucy Optical PSF Restoration
static void sep_olpf_2d(const float* src, float* dst, int H, int W, float sigma,
                        int olpf_mode, int n_threads) {
    if (olpf_mode == 0) {
        sep_gaussian_2d(src, dst, H, W, sigma, n_threads);
        return;
    }
    if (H <= 0 || W <= 0) return;
    const auto pc = heraw_gpu::make_olpf_psf_pc(H, W, 0, sigma, olpf_mode);
    const int rad = pc.radius;
    float kern[33];
    for (int k = -rad; k <= rad; ++k) kern[k + rad] = pc.w[std::abs(k)];

    UninitBuf<float> inplace_copy;
    if (src == dst) {
        inplace_copy.resize((size_t)H * (size_t)W);
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            std::memcpy(inplace_copy.data() + (size_t)r0 * W,
                        src + (size_t)r0 * W, (size_t)(r1 - r0) * W * sizeof(float));
        });
        src = inplace_copy.data();
    }

    const int TILE_Y = 32;
    const int TILE_X = heraw_cache::host_cache_profile().conv_tile_x();
    const int max_nt = std::max(1, n_threads);
    const int th_rows = TILE_Y + 2 * rad;
    std::vector<std::vector<float>> tls_tmp(max_nt, std::vector<float>((size_t)th_rows * TILE_X));
    parallel_for_rows(H, n_threads, [&](int y0, int y1, int tid) {
        float* __restrict tbuf = tls_tmp[tid].data();
        for (int yb0 = y0; yb0 < y1; yb0 += TILE_Y) {
            const int yb1 = std::min(y1, yb0 + TILE_Y);
            const int yr0 = yb0 - rad;
            const int yr1 = yb1 + rad;
            const int nr = yr1 - yr0;
            for (int xb0 = 0; xb0 < W; xb0 += TILE_X) {
                const int xb1 = std::min(W, xb0 + TILE_X);
                const int tw = xb1 - xb0;
                const int left_end = std::max(0, std::min(tw, rad - xb0));
                const int right_start = std::max(left_end, std::min(tw, W - rad - xb0));

                for (int ti = 0; ti < nr; ++ti) {
                    const int sr = clamp_i(yr0 + ti, 0, H - 1);
                    const float* __restrict srow = src + (size_t)sr * W;
                    float* __restrict trow = tbuf + (size_t)ti * TILE_X;
                    for (int i = 0; i < left_end; ++i) {
                        const int c = xb0 + i;
                        float acc = 0.0f;
                        for (int k = -rad; k <= rad; ++k) acc += srow[clamp_i(c + k, 0, W - 1)] * kern[k + rad];
                        trow[i] = acc;
                    }
                    if (right_start > left_end) {
                        const float w0 = kern[0];
                        const float* __restrict s0 = srow + xb0 - rad;
                        for (int i = left_end; i < right_start; ++i) trow[i] = w0 * s0[i];
                        for (int k = -rad + 1; k <= rad; ++k) {
                            const float wk = kern[k + rad];
                            const float* __restrict sk = srow + xb0 + k;
                            for (int i = left_end; i < right_start; ++i) trow[i] += wk * sk[i];
                        }
                    }
                    for (int i = right_start; i < tw; ++i) {
                        const int c = xb0 + i;
                        float acc = 0.0f;
                        for (int k = -rad; k <= rad; ++k) acc += srow[clamp_i(c + k, 0, W - 1)] * kern[k + rad];
                        trow[i] = acc;
                    }
                }

                for (int r = yb0; r < yb1; ++r) {
                    float* __restrict drow = dst + (size_t)r * W + xb0;
                    const int base_ti = r - yr0;
                    const float w0 = kern[0];
                    const float* __restrict t0 = tbuf + (size_t)(base_ti - rad) * TILE_X;
                    for (int i = 0; i < tw; ++i) drow[i] = w0 * t0[i];
                    for (int k = -rad + 1; k <= rad; ++k) {
                        const float wk = kern[k + rad];
                        const float* __restrict tk = tbuf + (size_t)(base_ti + k) * TILE_X;
                        for (int i = 0; i < tw; ++i) drow[i] += wk * tk[i];
                    }
                }
            }
        }
    }, TILE_Y);
}

static inline void rgb_to_lum0_row(const float* __restrict srow, float* __restrict lrow, int W) {
    int c = 0;
#if defined(__aarch64__)
    const float32x4_t wr = vdupq_n_f32(0.2126f), wg = vdupq_n_f32(0.7152f), wb = vdupq_n_f32(0.0722f);
    const float32x4_t eps_v = vdupq_n_f32(1e-6f);
    for (; c + 4 <= W; c += 4) {
        const float32x4x3_t p = vld3q_f32(srow + c * 3);
        const float32x4_t lv = vfmaq_f32(vfmaq_f32(vmulq_f32(wr, p.val[0]), wg, p.val[1]), wb, p.val[2]);
        vst1q_f32(lrow + c, vmaxq_f32(eps_v, lv));
    }
#elif defined(__AVX2__)
    const __m256 wr = _mm256_set1_ps(0.2126f), wg = _mm256_set1_ps(0.7152f), wb = _mm256_set1_ps(0.0722f);
    const __m256 eps_v = _mm256_set1_ps(1e-6f);
    for (; c + 8 <= W; c += 8) {
        const float* p = srow + (size_t)c * 3;
        const __m256 r_v = _mm256_setr_ps(p[0], p[3], p[6], p[9],  p[12], p[15], p[18], p[21]);
        const __m256 g_v = _mm256_setr_ps(p[1], p[4], p[7], p[10], p[13], p[16], p[19], p[22]);
        const __m256 b_v = _mm256_setr_ps(p[2], p[5], p[8], p[11], p[14], p[17], p[20], p[23]);
        const __m256 lv = _mm256_fmadd_ps(wb, b_v, _mm256_fmadd_ps(wg, g_v, _mm256_mul_ps(wr, r_v)));
        _mm256_storeu_ps(lrow + c, _mm256_max_ps(eps_v, lv));
    }
#endif
    for (; c < W; ++c) {
        lrow[c] = std::max(1e-6f, 0.2126f * srow[c * 3 + 0] + 0.7152f * srow[c * 3 + 1] + 0.0722f * srow[c * 3 + 2]);
    }
}

static inline void apply_lum_gain_row(float* __restrict drow, const float* __restrict erow, const float* __restrict lrow, int W) {
    int c = 0;
#if defined(__aarch64__)
    const float32x4_t zero_v = vdupq_n_f32(0.0f);
    for (; c + 4 <= W; c += 4) {
        const float32x4_t g = vdivq_f32(vld1q_f32(erow + c), vld1q_f32(lrow + c));
        float32x4x3_t p = vld3q_f32(drow + c * 3);
        p.val[0] = vmaxq_f32(zero_v, vmulq_f32(p.val[0], g));
        p.val[1] = vmaxq_f32(zero_v, vmulq_f32(p.val[1], g));
        p.val[2] = vmaxq_f32(zero_v, vmulq_f32(p.val[2], g));
        vst3q_f32(drow + c * 3, p);
    }
#elif defined(__AVX2__)
    const __m256 zero_v = _mm256_setzero_ps();
    const __m256i shuf0 = _mm256_setr_epi32(0, 0, 0, 1, 1, 1, 2, 2);
    const __m256i shuf1 = _mm256_setr_epi32(2, 3, 3, 3, 4, 4, 4, 5);
    const __m256i shuf2 = _mm256_setr_epi32(5, 5, 6, 6, 6, 7, 7, 7);
    for (; c + 8 <= W; c += 8) {
        const __m256 g8 = _mm256_div_ps(_mm256_loadu_ps(erow + c), _mm256_loadu_ps(lrow + c));
        float* dp = drow + (size_t)c * 3;
        const __m256 g_a = _mm256_permutevar8x32_ps(g8, shuf0);
        const __m256 g_b = _mm256_permutevar8x32_ps(g8, shuf1);
        const __m256 g_c = _mm256_permutevar8x32_ps(g8, shuf2);
        _mm256_storeu_ps(dp + 0,  _mm256_max_ps(zero_v, _mm256_mul_ps(_mm256_loadu_ps(dp + 0),  g_a)));
        _mm256_storeu_ps(dp + 8,  _mm256_max_ps(zero_v, _mm256_mul_ps(_mm256_loadu_ps(dp + 8),  g_b)));
        _mm256_storeu_ps(dp + 16, _mm256_max_ps(zero_v, _mm256_mul_ps(_mm256_loadu_ps(dp + 16), g_c)));
    }
#endif
    for (; c < W; ++c) {
        const float g = erow[c] / lrow[c];
        drow[c * 3 + 0] = std::max(0.0f, drow[c * 3 + 0] * g);
        drow[c * 3 + 1] = std::max(0.0f, drow[c * 3 + 1] * g);
        drow[c * 3 + 2] = std::max(0.0f, drow[c * 3 + 2] * g);
    }
}

static void snr_gated_rl_core_1ch(
    int H, int W, int n_iters, float psf_sigma, float clamp_delta, float noise_floor, int n_threads,
    RlWorkspace& ws, const float* rl_gain_half = nullptr, const float* rl_thrmul_half = nullptr, int Hh = 0, int Wh = 0,
    bool is_astro = false, int olpf_mode = 0
) {
    float* __restrict lum0 = ws.lum0.data();
    float* __restrict est = ws.est.data();
    float* __restrict conv = ws.conv.data();
    float* __restrict ratio = ws.ratio.data();
    float* __restrict edge_gate = ws.edge_gate.data();
    float* __restrict lmin_bound = ws.lmin_bound.data();
    float* __restrict lmax_bound = ws.lmax_bound.data();

    // Separable 9-tap horizontal + vertical min/max + fine-scale structure gate
    sep_gaussian_2d(lum0, conv, H, W, 0.65f, n_threads);
    const float g_thr2 = std::pow(std::max(0.0009f, 2.2f * noise_floor), 2.0f);
    const bool has_mod = (rl_gain_half && rl_thrmul_half && Hh > 0 && Wh > 0);
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            const int rm = clamp_i(r - 1, 0, H - 1), rp = clamp_i(r + 1, 0, H - 1);
            const float* __restrict lrow = lum0 + (size_t)r * W;
            const float* __restrict cm_r = conv + (size_t)rm * W;
            const float* __restrict c0_r = conv + (size_t)r * W;
            const float* __restrict cp_r = conv + (size_t)rp * W;
            float* __restrict eg_r = edge_gate + (size_t)r * W;
            float* __restrict est_r = est + (size_t)r * W;
            float* __restrict hmax_r = lmax_bound + (size_t)r * W;
            const size_t hrow_off = has_mod ? (size_t)clamp_i(r >> 2, 0, Hh - 1) * Wh : 0;

            auto eval_scalar = [&](int c) {
                const int cm = clamp_i(c - 1, 0, W - 1), cp = clamp_i(c + 1, 0, W - 1);
                const float gx = 0.5f * (c0_r[cp] - c0_r[cm]);
                const float gy = 0.5f * (cp_r[c] - cm_r[c]);
                const float lap = cm_r[c] + cp_r[c] + c0_r[cm] + c0_r[cp] - 4.0f * c0_r[c];
                const float struct_e2 = gx * gx + gy * gy + 0.35f * lap * lap;
                float rlg = 1.0f, thm = 1.0f;
                if (has_mod) {
                    const size_t hi = hrow_off + clamp_i(c >> 2, 0, Wh - 1);
                    rlg = rl_gain_half[hi];
                    thm = rl_thrmul_half[hi];
                }
                eg_r[c] = rlg * (struct_e2 / (struct_e2 + g_thr2 * thm));
                float hmin = lrow[c], hmax = lrow[c];
                const int dx0 = std::max(0, c - 4), dx1 = std::min(W - 1, c + 4);
                for (int cx = dx0; cx <= dx1; ++cx) {
                    const float v = lrow[cx];
                    if (v < hmin) hmin = v;
                    if (v > hmax) hmax = v;
                }
                est_r[c] = hmin;
                hmax_r[c] = hmax;
            };

            for (int c = 0; c < std::min(4, W); ++c) eval_scalar(c);
            int c = 4;
#if defined(__AVX2__)
            const __m256 half_v = _mm256_set1_ps(0.5f), four_v = _mm256_set1_ps(4.0f);
            const __m256 c35_v = _mm256_set1_ps(0.35f), gthr_v = _mm256_set1_ps(g_thr2), one_v = _mm256_set1_ps(1.0f);
            for (; c < W - 4 && (c & 3); ++c) eval_scalar(c);
            for (; c + 8 <= W - 4; c += 8) {
                const __m256 vc = _mm256_loadu_ps(c0_r + c);
                const __m256 vl = _mm256_loadu_ps(c0_r + c - 1), vr = _mm256_loadu_ps(c0_r + c + 1);
                const __m256 vt = _mm256_loadu_ps(cm_r + c),     vb = _mm256_loadu_ps(cp_r + c);
                const __m256 gx = _mm256_mul_ps(half_v, _mm256_sub_ps(vr, vl));
                const __m256 gy = _mm256_mul_ps(half_v, _mm256_sub_ps(vb, vt));
                const __m256 lap = _mm256_fnmadd_ps(four_v, vc, _mm256_add_ps(_mm256_add_ps(vt, vb), _mm256_add_ps(vl, vr)));
                const __m256 se2 = _mm256_fmadd_ps(c35_v, _mm256_mul_ps(lap, lap), _mm256_fmadd_ps(gx, gx, _mm256_mul_ps(gy, gy)));
                __m256 rlg_v = one_v, thm_v = one_v;
                if (has_mod) {
                    const size_t hi0 = hrow_off + clamp_i(c >> 2, 0, Wh - 1);
                    const size_t hi1 = hrow_off + clamp_i((c + 4) >> 2, 0, Wh - 1);
                    rlg_v = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(rl_gain_half[hi0])), _mm_set1_ps(rl_gain_half[hi1]), 1);
                    thm_v = _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_set1_ps(rl_thrmul_half[hi0])), _mm_set1_ps(rl_thrmul_half[hi1]), 1);
                }
                _mm256_storeu_ps(eg_r + c, _mm256_mul_ps(rlg_v, _mm256_div_ps(se2, _mm256_fmadd_ps(gthr_v, thm_v, se2))));

                const __m256 l_m4 = _mm256_loadu_ps(lrow + c - 4), l_m3 = _mm256_loadu_ps(lrow + c - 3), l_m2 = _mm256_loadu_ps(lrow + c - 2);
                const __m256 l_m1 = _mm256_loadu_ps(lrow + c - 1), l_0  = _mm256_loadu_ps(lrow + c),     l_p1 = _mm256_loadu_ps(lrow + c + 1);
                const __m256 l_p2 = _mm256_loadu_ps(lrow + c + 2), l_p3 = _mm256_loadu_ps(lrow + c + 3), l_p4 = _mm256_loadu_ps(lrow + c + 4);
                const __m256 mn_a = _mm256_min_ps(l_m4, _mm256_min_ps(l_m3, l_m2));
                const __m256 mn_b = _mm256_min_ps(l_m1, _mm256_min_ps(l_0,  l_p1));
                const __m256 mn_c = _mm256_min_ps(l_p2, _mm256_min_ps(l_p3, l_p4));
                const __m256 mx_a = _mm256_max_ps(l_m4, _mm256_max_ps(l_m3, l_m2));
                const __m256 mx_b = _mm256_max_ps(l_m1, _mm256_max_ps(l_0,  l_p1));
                const __m256 mx_c = _mm256_max_ps(l_p2, _mm256_max_ps(l_p3, l_p4));
                _mm256_storeu_ps(est_r + c,  _mm256_min_ps(mn_a, _mm256_min_ps(mn_b, mn_c)));
                _mm256_storeu_ps(hmax_r + c, _mm256_max_ps(mx_a, _mm256_max_ps(mx_b, mx_c)));
            }
#elif defined(__aarch64__)
            const float32x4_t half_v = vdupq_n_f32(0.5f), four_v = vdupq_n_f32(4.0f);
            const float32x4_t c35_v = vdupq_n_f32(0.35f), gthr_v = vdupq_n_f32(g_thr2), one_v = vdupq_n_f32(1.0f);
            for (; c < W - 4 && (c & 3); ++c) eval_scalar(c);
            for (; c + 4 <= W - 4; c += 4) {
                const float32x4_t vc = vld1q_f32(c0_r + c);
                const float32x4_t vl = vld1q_f32(c0_r + c - 1), vr = vld1q_f32(c0_r + c + 1);
                const float32x4_t vt = vld1q_f32(cm_r + c),     vb = vld1q_f32(cp_r + c);
                const float32x4_t gx = vmulq_f32(half_v, vsubq_f32(vr, vl));
                const float32x4_t gy = vmulq_f32(half_v, vsubq_f32(vb, vt));
                const float32x4_t lap = vfmsq_f32(vaddq_f32(vaddq_f32(vt, vb), vaddq_f32(vl, vr)), four_v, vc);
                const float32x4_t se2 = vfmaq_f32(vfmaq_f32(vmulq_f32(gy, gy), gx, gx), c35_v, vmulq_f32(lap, lap));
                float32x4_t rlg_v = one_v, thm_v = one_v;
                if (has_mod) {
                    const size_t hi = hrow_off + clamp_i(c >> 2, 0, Wh - 1);
                    rlg_v = vdupq_n_f32(rl_gain_half[hi]);
                    thm_v = vdupq_n_f32(rl_thrmul_half[hi]);
                }
                vst1q_f32(eg_r + c, vmulq_f32(rlg_v, vdivq_f32(se2, vfmaq_f32(se2, gthr_v, thm_v))));

                const float32x4_t l_m4 = vld1q_f32(lrow + c - 4), l_m3 = vld1q_f32(lrow + c - 3), l_m2 = vld1q_f32(lrow + c - 2);
                const float32x4_t l_m1 = vld1q_f32(lrow + c - 1), l_0  = vld1q_f32(lrow + c),     l_p1 = vld1q_f32(lrow + c + 1);
                const float32x4_t l_p2 = vld1q_f32(lrow + c + 2), l_p3 = vld1q_f32(lrow + c + 3), l_p4 = vld1q_f32(lrow + c + 4);
                const float32x4_t mn_a = vminq_f32(l_m4, vminq_f32(l_m3, l_m2));
                const float32x4_t mn_b = vminq_f32(l_m1, vminq_f32(l_0,  l_p1));
                const float32x4_t mn_c = vminq_f32(l_p2, vminq_f32(l_p3, l_p4));
                const float32x4_t mx_a = vmaxq_f32(l_m4, vmaxq_f32(l_m3, l_m2));
                const float32x4_t mx_b = vmaxq_f32(l_m1, vmaxq_f32(l_0,  l_p1));
                const float32x4_t mx_c = vmaxq_f32(l_p2, vmaxq_f32(l_p3, l_p4));
                vst1q_f32(est_r + c,  vminq_f32(mn_a, vminq_f32(mn_b, mn_c)));
                vst1q_f32(hmax_r + c, vmaxq_f32(mx_a, vmaxq_f32(mx_b, mx_c)));
            }
#endif
            for (; c < W; ++c) eval_scalar(c);
        }
    });

    // Vertical 9-tap pass using est (horiz min) and lmax_bound (horiz max) -> conv as scratch for vmax
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        const float hi_k = is_astro ? 0.06f : 0.02f;
        for (int r = r0; r < r1; ++r) {
            const int dy0 = std::max(0, r - 4), dy1 = std::min(H - 1, r + 4);
            const size_t off = (size_t)r * W;
            int c = 0;
#if defined(__AVX2__)
            const __m256 eps_v = _mm256_set1_ps(1e-6f), zero_v = _mm256_setzero_ps();
            const __m256 k02_v = _mm256_set1_ps(0.02f), hik_v = _mm256_set1_ps(hi_k);
            for (; c + 8 <= W; c += 8) {
                __m256 vmin = _mm256_loadu_ps(est + (size_t)dy0 * W + c);
                __m256 vmax = _mm256_loadu_ps(lmax_bound + (size_t)dy0 * W + c);
                for (int ry = dy0 + 1; ry <= dy1; ++ry) {
                    vmin = _mm256_min_ps(vmin, _mm256_loadu_ps(est + (size_t)ry * W + c));
                    vmax = _mm256_max_ps(vmax, _mm256_loadu_ps(lmax_bound + (size_t)ry * W + c));
                }
                const __m256 span = _mm256_sub_ps(vmax, vmin);
                if (is_astro) {
                    const __m256 l0 = _mm256_loadu_ps(lum0 + off + c);
                    const __m256 exc = _mm256_max_ps(zero_v, _mm256_sub_ps(l0, vmin));
                    _mm256_storeu_ps(lmin_bound + off + c, _mm256_max_ps(eps_v, _mm256_max_ps(vmin, _mm256_fnmadd_ps(k02_v, exc, l0))));
                } else {
                    _mm256_storeu_ps(lmin_bound + off + c, _mm256_max_ps(eps_v, _mm256_fnmadd_ps(k02_v, span, vmin)));
                }
                _mm256_storeu_ps(conv + off + c, _mm256_fmadd_ps(hik_v, span, vmax));
            }
#elif defined(__aarch64__)
            const float32x4_t eps_v = vdupq_n_f32(1e-6f), zero_v = vdupq_n_f32(0.0f);
            const float32x4_t k02_v = vdupq_n_f32(0.02f), hik_v = vdupq_n_f32(hi_k);
            for (; c + 4 <= W; c += 4) {
                float32x4_t vmin = vld1q_f32(est + (size_t)dy0 * W + c);
                float32x4_t vmax = vld1q_f32(lmax_bound + (size_t)dy0 * W + c);
                for (int ry = dy0 + 1; ry <= dy1; ++ry) {
                    vmin = vminq_f32(vmin, vld1q_f32(est + (size_t)ry * W + c));
                    vmax = vmaxq_f32(vmax, vld1q_f32(lmax_bound + (size_t)ry * W + c));
                }
                const float32x4_t span = vsubq_f32(vmax, vmin);
                if (is_astro) {
                    const float32x4_t l0 = vld1q_f32(lum0 + off + c);
                    const float32x4_t exc = vmaxq_f32(zero_v, vsubq_f32(l0, vmin));
                    vst1q_f32(lmin_bound + off + c, vmaxq_f32(eps_v, vmaxq_f32(vmin, vfmsq_f32(l0, k02_v, exc))));
                } else {
                    vst1q_f32(lmin_bound + off + c, vmaxq_f32(eps_v, vfmsq_f32(vmin, k02_v, span)));
                }
                vst1q_f32(conv + off + c, vfmaq_f32(vmax, hik_v, span));
            }
#endif
            for (; c < W; ++c) {
                float vmin = est[(size_t)dy0 * W + c];
                float vmax = lmax_bound[(size_t)dy0 * W + c];
                for (int ry = dy0 + 1; ry <= dy1; ++ry) {
                    float v0 = est[(size_t)ry * W + c];
                    float v1 = lmax_bound[(size_t)ry * W + c];
                    if (v0 < vmin) vmin = v0;
                    if (v1 > vmax) vmax = v1;
                }
                float span = vmax - vmin;
                float l0 = lum0[off + c];
                lmin_bound[off + c] = is_astro
                    ? std::max(1e-6f, std::max(vmin, l0 - 0.02f * std::max(0.0f, l0 - vmin)))
                    : std::max(1e-6f, vmin - 0.02f * span);
                conv[off + c] = vmax + hi_k * span;
            }
        }
    });
    std::swap(ws.lmax_bound, ws.conv);
    lmax_bound = ws.lmax_bound.data();
    conv = ws.conv.data();

    sep_gaussian_2d(edge_gate, edge_gate, H, W, 1.0f, n_threads);
    const float snr_knee = std::max(0.003f, 3.2f * noise_floor);
    // Fuse est = lum0 copy with pre-baked snr_w = (lum0 / (lum0 + snr_knee)) * edge_gate
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        const size_t i0 = (size_t)r0 * W, i1 = (size_t)r1 * W;
        std::memcpy(est + i0, lum0 + i0, (i1 - i0) * sizeof(float));
        size_t i = i0;
#if defined(__AVX2__)
#if HERAW_AVX512_ENABLED
        if (heraw_cache::use_avx512() && i + 16 <= i1) {
            i = rl_snr_gate_avx512(lum0, edge_gate, i, i1, snr_knee);
        }
#endif
        const __m256 knee_v = _mm256_set1_ps(snr_knee);
        for (; i + 8 <= i1; i += 8) {
            const __m256 l0 = _mm256_loadu_ps(lum0 + i);
            const __m256 sw = _mm256_mul_ps(_mm256_div_ps(l0, _mm256_add_ps(l0, knee_v)), _mm256_loadu_ps(edge_gate + i));
            _mm256_storeu_ps(edge_gate + i, sw);
        }
#elif defined(__aarch64__)
        const float32x4_t knee_v = vdupq_n_f32(snr_knee);
        for (; i + 4 <= i1; i += 4) {
            const float32x4_t l0 = vld1q_f32(lum0 + i);
            const float32x4_t sw = vmulq_f32(vdivq_f32(l0, vaddq_f32(l0, knee_v)), vld1q_f32(edge_gate + i));
            vst1q_f32(edge_gate + i, sw);
        }
#endif
        for (; i < i1; ++i) {
            edge_gate[i] *= (lum0[i] / (lum0[i] + snr_knee));
        }
    });

    for (int it = 0; it < n_iters; ++it) {
        sep_olpf_2d(est, conv, H, W, psf_sigma, olpf_mode, n_threads);
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            size_t i = (size_t)r0 * W;
            const size_t i1 = (size_t)r1 * W;
#if defined(__AVX2__)
#if HERAW_AVX512_ENABLED
            if (heraw_cache::use_avx512() && i + 16 <= i1) {
                i = rl_ratio_avx512(lum0, conv, edge_gate, ratio, i, i1, clamp_delta);
            }
#endif
            const __m256 eps_v = _mm256_set1_ps(1e-6f), one_v = _mm256_set1_ps(1.0f);
            const __m256 lo_d = _mm256_set1_ps(-clamp_delta), hi_d = _mm256_set1_ps(clamp_delta);
            for (; i + 8 <= i1; i += 8) {
                const __m256 q = _mm256_sub_ps(_mm256_div_ps(_mm256_loadu_ps(lum0 + i), _mm256_max_ps(_mm256_loadu_ps(conv + i), eps_v)), one_v);
                const __m256 d = _mm256_min_ps(hi_d, _mm256_max_ps(lo_d, q));
                _mm256_storeu_ps(ratio + i, _mm256_fmadd_ps(d, _mm256_loadu_ps(edge_gate + i), one_v));
            }
#elif defined(__aarch64__)
            const float32x4_t eps_v = vdupq_n_f32(1e-6f), one_v = vdupq_n_f32(1.0f);
            const float32x4_t lo_d = vdupq_n_f32(-clamp_delta), hi_d = vdupq_n_f32(clamp_delta);
            for (; i + 4 <= i1; i += 4) {
                const float32x4_t q = vsubq_f32(vdivq_f32(vld1q_f32(lum0 + i), vmaxq_f32(vld1q_f32(conv + i), eps_v)), one_v);
                const float32x4_t d = vminq_f32(hi_d, vmaxq_f32(lo_d, q));
                vst1q_f32(ratio + i, vfmaq_f32(one_v, d, vld1q_f32(edge_gate + i)));
            }
#endif
            for (; i < i1; ++i) {
                const float d = clamp_f(lum0[i] / std::max(conv[i], 1e-6f) - 1.0f, -clamp_delta, clamp_delta) * edge_gate[i];
                ratio[i] = 1.0f + d;
            }
        });
        sep_olpf_2d(ratio, conv, H, W, psf_sigma, olpf_mode, n_threads);
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            size_t i = (size_t)r0 * W;
            const size_t i1 = (size_t)r1 * W;
#if defined(__AVX2__)
#if HERAW_AVX512_ENABLED
            if (heraw_cache::use_avx512() && i + 16 <= i1) {
                i = rl_update_avx512(est, conv, lmin_bound, lmax_bound, i, i1);
            }
#endif
            for (; i + 8 <= i1; i += 8) {
                const __m256 v = _mm256_mul_ps(_mm256_loadu_ps(est + i), _mm256_loadu_ps(conv + i));
                _mm256_storeu_ps(est + i, _mm256_min_ps(_mm256_loadu_ps(lmax_bound + i), _mm256_max_ps(_mm256_loadu_ps(lmin_bound + i), v)));
            }
#elif defined(__aarch64__)
            for (; i + 4 <= i1; i += 4) {
                const float32x4_t v = vmulq_f32(vld1q_f32(est + i), vld1q_f32(conv + i));
                vst1q_f32(est + i, vminq_f32(vld1q_f32(lmax_bound + i), vmaxq_f32(vld1q_f32(lmin_bound + i), v)));
            }
#endif
            for (; i < i1; ++i) {
                est[i] = clamp_f(est[i] * conv[i], lmin_bound[i], lmax_bound[i]);
            }
        });
    }
}

static void snr_gated_rl_deconv(
    float* rgb, int H, int W, int n_iters, float psf_sigma, float clamp_delta, float noise_floor, int n_threads,
    RlWorkspace& ws, const float* rl_gain_half = nullptr, const float* rl_thrmul_half = nullptr, int Hh = 0, int Wh = 0,
    bool is_astro = false, int olpf_mode = 0
) {
    if (n_iters <= 0 || H <= 0 || W <= 0) return;
    const size_t Npx = (size_t)H * (size_t)W;
    ws.ensure(Npx);
    float* __restrict lum0 = ws.lum0.data();
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            rgb_to_lum0_row(rgb + (size_t)r * W * 3, lum0 + (size_t)r * W, W);
        }
    });
    snr_gated_rl_core_1ch(H, W, n_iters, psf_sigma, clamp_delta, noise_floor, n_threads,
                          ws, rl_gain_half, rl_thrmul_half, Hh, Wh, is_astro, olpf_mode);
    const float* __restrict est = ws.est.data();
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            apply_lum_gain_row(rgb + (size_t)r * W * 3, est + (size_t)r * W, lum0 + (size_t)r * W, W);
        }
    });
}

static void snr_gated_rl_bands(float* rgb, int H, int W, int iterations, float sigma, float delta,
                               float noise, int threads, RlWorkspace& workspace,
                               const float* gain, const float* threshold, int Hh, int Wh,
                               bool astro = false, int olpf = 0) {
    if (iterations <= 0 || H <= 0 || W <= 0) return;
    const int radius = heraw_gpu::make_olpf_psf_pc(1, 1, 0, sigma, olpf).radius;
    const int halo = ((2 * iterations * radius + 19) / 4) * 4;
    constexpr int CORE_Y = 256;
    constexpr int CORE_X = 512;
    if ((H <= CORE_Y + 2 * halo && W <= CORE_X + 2 * halo) ||
        !gain || !threshold || Hh != H / 4 || Wh != W / 4) {
        snr_gated_rl_deconv(rgb, H, W, iterations, sigma, delta, noise, threads, workspace, gain, threshold, Hh, Wh, astro, olpf);
        return;
    }

    const size_t Npx = (size_t)H * (size_t)W;
    workspace.lum0.resize(Npx);
    float* __restrict full_lum = workspace.lum0.data();
    parallel_for_rows(H, threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            rgb_to_lum0_row(rgb + (size_t)r * W * 3, full_lum + (size_t)r * W, W);
        }
    });

    const int nr = (H + CORE_Y - 1) / CORE_Y;
    const int nc = (W + CORE_X - 1) / CORE_X;
    const int max_nt = std::max(1, threads);
    struct alignas(64) RlTileWorker {
        RlWorkspace ws;
        UninitBuf<float> g_p, t_p;
    };
    std::vector<RlTileWorker> tls(max_nt);

    parallel_for_rows(nr * nc, max_nt, [&](int t0, int t1, int tid) {
        auto& loc = tls[tid];
        for (int ti = t0; ti < t1; ++ti) {
            const int ty = ti / nc, tx = ti % nc;
            const int cy0 = ty * CORE_Y, cy1 = std::min(H, cy0 + CORE_Y);
            const int cx0 = tx * CORE_X, cx1 = std::min(W, cx0 + CORE_X);
            const int y0 = std::max(0, (cy0 - halo) & ~3);
            const int y1 = std::min(H, (cy1 + halo + 3) & ~3);
            const int x0 = std::max(0, (cx0 - halo) & ~3);
            const int x1 = std::min(W, (cx1 + halo + 3) & ~3);
            const int ph = y1 - y0, pw = x1 - x0;
            loc.ws.ensure((size_t)ph * (size_t)pw);
            for (int r = 0; r < ph; ++r) {
                std::memcpy(loc.ws.lum0.data() + (size_t)r * pw,
                            full_lum + (size_t)(y0 + r) * W + x0,
                            (size_t)pw * sizeof(float));
            }
            const int qy0 = y0 >> 2, qx0 = x0 >> 2;
            const int qh = std::max(1, (ph + 3) >> 2);
            const int qw = std::max(1, (pw + 3) >> 2);
            loc.g_p.resize((size_t)qh * (size_t)qw);
            loc.t_p.resize((size_t)qh * (size_t)qw);
            for (int qr = 0; qr < qh; ++qr) {
                const int sy = clamp_i(qy0 + qr, 0, Hh - 1);
                const float* gs = gain + (size_t)sy * Wh;
                const float* ts = threshold + (size_t)sy * Wh;
                float* gd = loc.g_p.data() + (size_t)qr * qw;
                float* td = loc.t_p.data() + (size_t)qr * qw;
                if (qx0 + qw <= Wh) {
                    std::memcpy(gd, gs + qx0, (size_t)qw * sizeof(float));
                    std::memcpy(td, ts + qx0, (size_t)qw * sizeof(float));
                } else {
                    for (int qc = 0; qc < qw; ++qc) {
                        const int sx = clamp_i(qx0 + qc, 0, Wh - 1);
                        gd[qc] = gs[sx];
                        td[qc] = ts[sx];
                    }
                }
            }
            snr_gated_rl_core_1ch(ph, pw, iterations, sigma, delta, noise, 1, loc.ws,
                                  loc.g_p.data(), loc.t_p.data(), qh, qw, astro, olpf);
            const int core_w = cx1 - cx0;
            const int dx0 = cx0 - x0;
            for (int r = cy0; r < cy1; ++r) {
                const size_t t_off = (size_t)(r - y0) * pw + dx0;
                apply_lum_gain_row(rgb + ((size_t)r * W + cx0) * 3,
                                   loc.ws.est.data() + t_off,
                                   loc.ws.lum0.data() + t_off,
                                   core_w);
            }
        }
    }, 1);
}

#include "heraw_cpu_stage45.hpp"

// ---------------------------------------------------------------------------
// 6. Standalone DNG 1.4 Writer & Uncompressed 16-bit RGB TIFF Writer
// ---------------------------------------------------------------------------
static uint16_t flip_to_exif_orientation(int flip) {
    switch (flip) {
        case 3: return 3; // 180 deg
        case 5: return 8; // 90 CCW
        case 6: return 6; // 90 CW
        default: return 1;
    }
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char ch : s) {
        if (ch == '"') out += "\\\"";
        else if (ch == '\\') out += "\\\\";
        else if (ch == '\n') out += "\\n";
        else if (ch == '\r') out += "\\r";
        else if (ch == '\t') out += "\\t";
        else if (ch < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)ch);
            out += buf;
        } else {
            out.push_back((char)ch);
        }
    }
    return out;
}

enum class PipelinePreset {
    PHOTO, // Photo (default photography output)
    SCI,   // Sci (scientific / ML ground-truth: PSF off, auto-exp off, rolloff off, polish off, strict linear)
    ARC,   // Arc (archival master: PSF off, polish off, balanced denoise, normal exposure)
    ASTRO  // Astro (handheld astrophotography & starry landscape: anti-star-eater, star color lock, sky/ground alignment)
};

struct PipelineConfig {
    PipelinePreset preset = PipelinePreset::PHOTO;
    std::string preset_name = "photo";
    int anchor_mode = 0; // 0 = auto sharpest, -1 = first frame, -2 = middle frame, >=1 = 1-based index
    bool wizard_mode = false;
    heraw_gpu::BackendPref backend = heraw_gpu::BackendPref::AUTO;
    float denoise_strength = 1.00f;
    float chroma_denoise = 1.00f;
    int psf_iters = 4;
    int rgb_psf_iters = 2;
    float psf_sigma = 1.05f;
    float deghost_strength = 1.00f;
    bool auto_exposure = true;
    bool highlight_rolloff = true;
    bool glint_desat = true;
    bool edge_polish = true;
    bool dither = true;
    float ev_offset = 0.0f;

    heraw_io::RawDngMode raw_mode = heraw_io::RawDngMode::DNG_LOSSLESS;
    heraw_io::RgbFormat rgb_format = heraw_io::RgbFormat::TIFF_DEFLATE;
    heraw_io::FieldExportMode field_mode = heraw_io::FieldExportMode::SPLIT_GRAY;
    heraw_io::ColorSpaceMode color_space = heraw_io::ColorSpaceMode::SRGB;
    int rgb_scale_mode = 2; // 2 = 2x SR RGB, 1 = 1x True-Sampled RGB, 3 = Both (1x + 2x)
    bool explicit_rgb_fmt = false;
    bool field_from_outdir = false;
    float jxs_bpp = 10.0f;

    bool sort_time = true;
    heraw_plan::ExecPolicy policy = heraw_plan::ExecPolicy::AUTO_PRUNE;
    int interactive_choice = 0;
    int group_size = 8;
    int max_frames = 0;
    double burst_gap_s = 3.5;
    float min_rel_sharpness = 0.72f;
    float max_shift_px = 120.0f;

    int n_threads = 1;
    heraw_cache::CpuCorePolicy cpu_cores = heraw_cache::CpuCorePolicy::AUTO;
    heraw_cache::TaskPriority priority = heraw_cache::TaskPriority::NORMAL;
    heraw_cache::SimdPolicy simd = heraw_cache::SimdPolicy::AUTO;
    bool progress_json = false;
    std::string motion_mode = "standard";
    std::string olpf_mode = "auto";
    std::string fields_csv = "motion,phase,focus";
    std::string reflection_mode = "off";
    std::string transient_color = "rgb";
    std::string optics_report, export_lcp, dataset_dir;
    std::string out_rgb_1x_path, suite_prefix_override, log_file_path;
    std::string lcp_fields = "lca,vignette,distortion";
    std::vector<int> skip_frames;
    bool relay_raw = false, no_tool_exif = false, silicon_clean = true, auto_lca = true;
    bool emit_transient = false, export_parallax_html = false, export_refocus = false;
    bool optics_only = false;
    int ibp_iters = 1, strobe_step = 2, tile_size = 256, export_topk = 0;
    float clean_sens = 1.0f, vnd_thresh = 0.65f, strobe_fade = 0.0f;
    float transient_sigma = 3.5f, parallax_amp = 1.0f;
    float refocus_pos = 0.5f, bokeh_strength = 12.0f, tile_min_conf = 0.75f;
    heraw_wvdc::RawDecoderPref decoder_pref = heraw_wvdc::RawDecoderPref::AUTO;
    std::string nev_frames_spec = "0-7";
};

static void apply_preset_defaults(PipelineConfig& cfg, PipelinePreset p) {
    cfg.preset = p;
    if (p == PipelinePreset::SCI) {
        cfg.preset_name = "sci";
        cfg.denoise_strength = 0.40f;
        cfg.chroma_denoise = 0.55f;
        cfg.psf_iters = 0;
        cfg.rgb_psf_iters = 0;
        cfg.psf_sigma = 1.05f;
        cfg.deghost_strength = 1.35f;
        cfg.auto_exposure = false;
        cfg.highlight_rolloff = false;
        cfg.glint_desat = false;
        cfg.edge_polish = false;
        cfg.dither = true;
        cfg.min_rel_sharpness = 0.82f;
    } else if (p == PipelinePreset::ARC) {
        cfg.preset_name = "arc";
        cfg.denoise_strength = 0.70f;
        cfg.chroma_denoise = 0.85f;
        cfg.psf_iters = 0;
        cfg.rgb_psf_iters = 0;
        cfg.psf_sigma = 1.05f;
        cfg.deghost_strength = 1.10f;
        cfg.auto_exposure = true;
        cfg.highlight_rolloff = true;
        cfg.glint_desat = true;
        cfg.edge_polish = false;
        cfg.dither = true;
        cfg.min_rel_sharpness = 0.75f;
    } else if (p == PipelinePreset::ASTRO) {
        cfg.preset_name = "astro";
        cfg.denoise_strength = 0.85f;
        cfg.chroma_denoise = 0.90f;
        cfg.psf_iters = 3;
        cfg.rgb_psf_iters = 1;
        cfg.psf_sigma = 0.95f;
        cfg.deghost_strength = 0.85f;
        cfg.auto_exposure = true;
        cfg.highlight_rolloff = true;
        cfg.glint_desat = false;
        cfg.edge_polish = false;
        cfg.dither = true;
        cfg.min_rel_sharpness = 0.70f;
    } else {
        cfg.preset_name = "photo";
        cfg.denoise_strength = 1.00f;
        cfg.chroma_denoise = 1.00f;
        cfg.psf_iters = 4;
        cfg.rgb_psf_iters = 2;
        cfg.psf_sigma = 1.05f;
        cfg.deghost_strength = 1.00f;
        cfg.auto_exposure = true;
        cfg.highlight_rolloff = true;
        cfg.glint_desat = true;
        cfg.edge_polish = true;
        cfg.dither = true;
        cfg.min_rel_sharpness = 0.72f;
    }
}

static heraw_io::RgbFormat infer_rgb_format_from_path(const std::string& path, heraw_io::RgbFormat fallback) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return fallback;
    std::string ext = path.substr(dot);
    for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
    if (ext == ".png") return heraw_io::RgbFormat::PNG16;
    if (ext == ".jxs" || ext == ".jxc") return heraw_io::RgbFormat::JXS_VIS;
    if (ext == ".tif" || ext == ".tiff") return heraw_io::RgbFormat::TIFF_DEFLATE;
    return fallback;
}

static const char* rgb_format_ext(heraw_io::RgbFormat fmt) {
    switch (fmt) {
        case heraw_io::RgbFormat::PNG16:        return ".png";
        case heraw_io::RgbFormat::JXS_VIS:      return ".jxs";
        case heraw_io::RgbFormat::JXS_LOSSLESS: return ".jxs";
        case heraw_io::RgbFormat::TIFF_DEFLATE: return ".tif";
        case heraw_io::RgbFormat::TIFF_HQ:      return ".tif";
        case heraw_io::RgbFormat::TIFF_RAW:     return ".tif";
    }
    return ".tif";
}

static void print_cli_help(const char* prog) {
    std::fprintf(stdout,
        "NijiN.LiteBurstSuite (nijinLBS) " NIJIN_VERSION " -- RAW Imaging Suite\n"
        "Built-in LibRaw 0.23.0-Snapshot202611 (Nikon HE/HE*) + Metal/Vulkan GPU Compute + SVT-JPEG-XS + zlib.\n\n"
        "Usage:\n"
        "  %s [options] <in1.NEF|folder1> [in2.NEF|folder2 ...]\n"
        "  %s --scan-dir <folder> [--recursive] --out-dir <out_folder> [--emit-raw] [--emit-rgb]\n"
        "  %s --decode-jxs <input.jxs> <output.png|output.tif>\n\n"
        "Input, Batch Discovery & Frame Policy Options:\n"
        "  --scan-dir <dir>           Scan directory for RAW files and auto-discover super-resolvable bursts\n"
        "  --recursive                Scan subdirectories recursively when scanning folders\n"
        "  --sort-time / --no-sort-time  Sort frames by EXIF DateTimeOriginal + SubSec + filename (default: ON)\n"
        "  --decoder <mode>           RAW decoder routing: auto (default: nijinWVDC for HE/HE*/.NEV/.jxs/ARW6) | wvdc | libraw\n"
        "  --nev-frames <spec>        Frame range/list for .NEV clips (default: 0-7; e.g. 0-7, 2,5-9, all)\n"
        "  --libraw <lib_path>        Load a custom external LibRaw shared library (.dll / .dylib / .so)\n"
        "  --policy <mode>            Suitability & sequence handling policy (default: auto-prune):\n"
        "                               auto-prune  : Reject blur/motion/exposure outliers; require >= 5 (2x) or >= 3 (1x) frames\n"
        "                               multi-group : Partition long sequences into multiple >=5 (2x) or >=3 (1x) burst groups\n"
        "                               force-all   : Force execution down to 1 frame (bypass min-frame & motion blocks)\n"
        "                               interactive : Prompt with at most 2 recommended options on anomalies/long bursts\n"
        "  --interactive-choice <1|2> Pre-select option 1 or 2 in --policy interactive (for GUI/scripts)\n"
        "  --group-size <N>           Target frame count per group in multi-group/batch mode (default: 8)\n"
        "  --max-frames <K>           Hard cap keeping K sharpest frames per group (0 = unlimited)\n"
        "  --burst-gap-s <sec>        Max EXIF timestamp gap within one candidate burst in batch scan (default: 3.5)\n\n"
        "On-Demand Output Format, Field Map & Color Space Options:\n"
        "  --out-raw <file.dng>       Output 2x 16-bit RGGB Linear RAW DNG path (default: lossless SOF3)\n"
        "  --out-rgb <file>           Output RGB path (.tif, .png, .jxs; 2x by default or 1x with --rgb-scale 1x)\n"
        "  --out-rgb-1x <file>        Output 1x True-Sampled 16-bit RGB path explicitly\n"
        "  --rgb-scale <2x|1x|both>   RGB synthesis resolution: 2x (default) | 1x (True-Sampled 16-bit RGB) | both (1x + 2x)\n"
        "  --out-field <prefix|file>  Output 16-bit Physical Field Map(s) (default: 3 separate grayscale PNGs)\n"
        "  --out-dir <dir>            Batch/multi-group output directory (names files <anchor_stem>_nijinLBS.<ext>)\n"
        "  --emit-raw                 Enable 2x RGGB DNG output when using --out-dir\n"
        "  --emit-rgb                 Enable RGB output when using --out-dir (respects --rgb-scale)\n"
        "  --emit-rgb-1x              Enable 1x True-Sampled RGB output (<stem>_1x.<ext>) when using --out-dir\n"
        "  --emit-field               Enable 16-bit Field Map output when using --out-dir\n"
        "  --field-mode <mode>        Physical Field Map layout (default: split):\n"
        "                               split      : Separate 16-bit Grayscale PNGs (_motion/_sky, _phase, _focus, ...) [Default]\n"
        "                               rgb        : Packed 16-bit RGB PNG(s) (3 fields per PNG)\n"
        "                               tiff-alpha : Embed named auxiliary fields inside the output 16-bit TIFF\n"
        "                               motion     : Only 16-bit Grayscale Motion / Astro Sky-Horizon mask\n"
        "                               phase      : Only 16-bit Grayscale 2x2 Subpixel Phase SR confidence map\n"
        "                               focus      : Only 16-bit Grayscale Optical Focus acutance map\n"
        "  --raw-format <fmt>         DNG RGGB encoding inside .dng (default: dng; size ratio vs uncompressed=1.00):\n"
        "                               dng      : 16-bit Lossless DNG (SOF3 2D, ~0.39x size / 2.6:1, Default)\n"
        "                               dng-linear: 3-channel camera-linear 16-bit lossless SOF3 DNG\n"
        "                               dng-hq   : Advanced Lossy DNG (14->16b + SOF3, ~0.09x size / 11.6:1)\n"
        "                               dng-fp16 : 16-bit IEEE Half-Float HDR DNG (Deflate, ~0.29x size / 3.5:1)\n"
        "                               dng-raw  : 16-bit Uncompressed DNG (1.00x baseline, ~349 MiB)\n"
        "  --rgb-format <fmt>         RGB encoding format (default: tiff-deflate; size ratio vs uncompressed=1.00):\n"
        "                               tiff-deflate : 16-bit Lossless TIFF (Deflate+Predictor2, ~0.39x size, Default)\n"
        "                               tiff-hq      : 16-bit Visually-Lossless TIFF (Noise-calibrated HQ, ~0.19x size)\n"
        "                               png          : 16-bit Lossless PNG + eXIf + sRGB/ICC (~0.40x size)\n"
        "                               jxs          : 14-bit JPEG XS Visually-Lossless (.jxs, ~0.21x size @10bpp)\n"
        "                               jxs-lossless : 14-bit JPEG XS Lossless (.jxs, ~0.43x size)\n"
        "                               tiff-raw     : 16-bit Uncompressed TIFF (1.00x baseline, ~1046 MiB)\n"
        "  --jxs-bpp <bpp>            Target bits-per-pixel for --rgb-format jxs (3.0..28.0, default: 10.0)\n"
        "  --color-space <cs>         RGB output color space: srgb (default) | adobergb (embeds ICC v2.4 profile)\n\n"
        "Mode Presets, Pose Anchor & Compute Backend:\n"
        "  --preset <name>            One-click mode preset (individual flags below override any preset knob):\n"
        "                               photo : Photo mode (PSF restore, auto-exposure, highlight rolloff)\n"
        "                               sci   : Scientific / ML mode (PSF off, auto-exp off, rolloff off, strict linear)\n"
        "                               arc   : Archival mode (PSF off, polish off, balanced denoise=0.70)\n"
        "                               astro : Handheld Astro / Starry Landscape (anti-star-eater, star color, sky/ground)\n"
        "  --wizard                   Launch interactive 5D Scene Diagnosis & Smart Prescription Wizard before fusion\n"
        "  --anchor-frame <sel>       Pose frame anchoring moving subjects: auto (sharpest, default) | first | middle | <1..N>\n"
        "  --backend <mode>           Compute acceleration backend: auto (default, GPU->CPU) | gpu | cpu\n"
        "  --denoise-strength <0..2>  Luminance GAT-DCT Wiener strength (0 = bypass spatial denoise)\n"
        "  --chroma-denoise <0..2>    Luminance-guided A-Trous chroma denoise strength\n"
        "  --psf-iters <0..10>        Linear RAW Richardson-Lucy PSF iterations (0 = disable PSF restore)\n"
        "  --rgb-psf-iters <0..6>     Additional RGB micro-contrast RL iterations (0 = disable)\n"
        "  --psf-sigma <0.4..2.5>     Optical PSF Gaussian sigma in 2x pixels (default: 1.05)\n"
        "  --deghost-strength <0.5..2.5> Motion/parallax rejection strictness (higher = stricter)\n"
        "  --auto-exposure <on|off>   Blind scene-percentile auto-exposure gain\n"
        "  --highlight-rolloff <on|off> Hue-preserving highlight compression & specular desaturation\n"
        "  --edge-polish <on|off>     Post-DCT flat-region bilateral polish\n"
        "  --dither <on|off>          16-bit TPDF quantization dither\n"
        "  --ev-offset <EV>           Additional manual exposure offset in EV [-4.0 .. +4.0]\n"
        "  --min-rel-sharpness <0..1> Relative acutance gate vs burst median for blur pruning (default: 0.72)\n"
        "  --max-shift-px <px>        Max 1x global shift before splitting/rejecting large motion (default: 120)\n\n"
        "Suite Workspaces:\n"
        "  --motion-mode <mode>       standard | clean-plate | virtual-nd | strobe\n"
        "  --clean-sens <value>       Clean-plate selection sensitivity (default: 1)\n"
        "  --vnd-thresh <0..1>        Fluid integration mask threshold (default: 0.65)\n"
        "  --strobe-step <N> --strobe-fade <0..1>  Action sampling and trail fade\n"
        "  --emit-transient --transient-sigma <s> --transient-color rgb|gray\n"
        "  --reflection-mode off|glint-suppress|split-layers\n"
        "  --export-parallax-html --parallax-amp <value>\n"
        "  --export-refocus --refocus-pos <0..1> --bokeh-strength <pixels>\n"
        "  --fields <csv>            motion,phase,focus,depth,defocus,specular,sigma,coherence,velocity,flicker,turbulence\n"
        "  --field-mode tiff-alpha   Embed named fields in the main TIFF\n"
        "  --optics-only --optics-report <prefix> --export-lcp <file.lcp>\n"
        "  --lcp-fields <csv>         lca,vignette,distortion model selection\n"
        "  --fields normal_x,normal_y,occlusion  Relative local geometry fields\n"
        "  --export-dataset-tiles <dir> --tile-size 256|512 --tile-min-conf <0..1>\n"
        "  --relay-raw on|off --silicon-clean on|off --auto-lca on|off\n"
        "  --ibp-iters <0..2> --olpf-mode auto|none|weak4|gauss16\n"
        "  --export-topk <K> --out-dir <dir>  Copy AF-ranked original RAWs and selection JSON\n"
        "  --skip-frames <csv>        1-based frame exclusions after time ordering\n\n"
        "Inspection, Utility & Automation Options:\n"
        "  --inspect                  Scan & analyze burst suitability/motion/groups and exit without rendering\n"
        "  --plan-json                Emit structured JSON suitability & <=2-option interactive plan on stdout\n"
        "  --single-group             Keep inputs in one burst; use --policy force-all to retain checked frames\n"
        "  --progress-json            Emit structured JSON progress/result lines on stderr\n"
        "  --log-file <file>          Mirror full diagnostic & stage log to UTF-8 file\n"
        "  --no-tool-exif [on|off]    Omit this tool's name/version from exported EXIF (default: off)\n"
        "  --threads <N>              Worker CPU threads (default: smart hybrid core plan)\n"
        "  --cpu-cores <mode>         CPU core allocation: auto (default) | perf | physical | eff | all\n"
        "  --battery-device <mode>    Battery-device mode: auto (default) | on (default no-SMT) | off\n"
        "  --priority <mode>          Process & worker QoS priority: normal (default) | high | below | idle\n"
        "  --simd <mode>              CPU SIMD dispatch: auto (default, AVX2/NEON) | avx2 | avx512(archived)\n"
        "  -v, --version              Print version banner and exit\n"
        "  -h, --help                 Show this help message and exit\n\n"
        "Burst Frame-Count Guide:\n"
        "  1x True-Sampled RGB : >= 3 valid light frames (true multi-frame RGB color + GAT-DCT/KLT 16-bit SNR)\n"
        "  2x Super-Resolution : >= 5 valid light frames (robust 2x2 subpixel phase coverage + +7.0 dB SNR)\n"
        "  Optimal sweet spot  : 6-8 handheld frames (80-92%% phase coverage, +7.6..8.8 dB SNR)\n"
        "  Low-light high-ISO  : 9-12 frames (extra shadow shot-noise reduction)\n\n"
        "Exit Codes:\n"
        "  0  Success (or user-confirmed clean cancel)\n"
        "  1  Runtime / decode / I/O error\n"
        "  2  Invalid CLI arguments\n"
        "  3  Suitability or physical observability gate rejected (< min suitable frames / unobservable standalone mode)\n",
        prog ? prog : "nijinLBS", prog ? prog : "nijinLBS", prog ? prog : "nijinLBS");
}

static int execute_sr_group(
    std::vector<UnpackedFrame>& uframes,
    const std::vector<int>& light_indices,
    bool free_all_uframes,
    const std::string& out_raw_path,
    const std::string& out_rgb_path,
    const std::string& out_rgb_1x_path,
    const std::string& out_field_path,
    const PipelineConfig& requested_cfg,
    int group_idx_1based,
    int total_groups,
    std::chrono::steady_clock::time_point t_wall0
) {
    PipelineConfig cfg = requested_cfg;
    if (cfg.relay_raw) {
        cfg.denoise_strength = cfg.chroma_denoise = 0.0f;
        cfg.psf_iters = 0;
        cfg.edge_polish = false;
    }
    const int n_threads = cfg.n_threads;
    auto elapsed_s = [&](auto t0) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    auto emit_stage = [&](int stg, int pct, double sec, const char* msg) {
        if (total_groups > 1) log_err("[Group %d/%d] %s\n", group_idx_1based, total_groups, msg);
        else                  log_err("%s\n", msg);
        if (cfg.progress_json) {
            int overall_pct = ((group_idx_1based - 1) * 100 + pct) / std::max(1, total_groups);
            log_err(
                "{\"event\":\"progress\",\"group\":%d,\"total_groups\":%d,\"stage\":%d,\"total_stages\":6,"
                "\"percent\":%d,\"stage_s\":%.3f,\"wall_s\":%.3f,\"message\":\"%s\"}\n",
                group_idx_1based, total_groups, stg, overall_pct, sec, elapsed_s(t_wall0), json_escape(msg).c_str());
        }
        std::fflush(stderr);
    };

    auto t0 = std::chrono::steady_clock::now();
    auto cost_start = t0;
    auto cost_mark = [&](const char* name) {
        const auto now = std::chrono::steady_clock::now();
        log_err("[Cost] %s %.6f s\n", name,
            std::chrono::duration<double>(now-cost_start).count());
        cost_start = now;
    };
    const int N = (int)light_indices.size();
    const int H1 = uframes[light_indices[0]].H;
    const int W1 = uframes[light_indices[0]].W;
    const int Hh = H1 / 2;
    const int Wh = W1 / 2;
    for (int idx : light_indices) {
        if (uframes[idx].H != H1 || uframes[idx].W != W1) {
            log_err("Error: mismatched dimensions across burst frames.\n");
            return 1;
        }
    }

    const UnpackedFrame& meta0 = uframes[light_indices[0]];
    const bool is_xtrans = meta0.is_xtrans;
    const int shutter_kind = meta0.shutter_kind;
    const float focal_mm = meta0.focal_mm, exposure_seconds = meta0.shutter_s;
    const float lens_aperture = meta0.aperture;
    const float cam_iso = meta0.iso;
    const float sensor_temp_c = meta0.sensor_temp_c;
    const bool ois_active = meta0.has_ois;
    const std::string lens_name = meta0.lens;
    const float focus_distance_m = meta0.focus_distance_m;
    std::vector<double> frame_times;
    frame_times.reserve(light_indices.size());
    double min_epoch_ts = std::numeric_limits<double>::infinity();
    bool all_epoch_ts = !light_indices.empty();
    for (int idx : light_indices) {
        const double ts = uframes[idx].timestamp_s;
        frame_times.push_back(ts);
        if (ts > 1e6 && std::isfinite(ts)) min_epoch_ts = std::min(min_epoch_ts, ts);
        else all_epoch_ts = false;
    }
    if (all_epoch_ts && std::isfinite(min_epoch_ts)) {
        for (double& ts : frame_times) ts -= min_epoch_ts;
    }
    const std::vector<size_t> capture_order(light_indices.begin(),light_indices.end());
    std::vector<float> focus_positions;
    for(int idx:light_indices) {
        if(uframes[idx].focus_distance_m<=0) { focus_positions.clear();break; }
        focus_positions.push_back(1.0f/uframes[idx].focus_distance_m);
    }
    if (focus_positions.empty()) {
        for (int idx : light_indices) {
            if (uframes[idx].focus_step <= 0.0f) { focus_positions.clear(); break; }
            focus_positions.push_back(uframes[idx].focus_step);
        }
    }
    bool bracket_exposures = false;
    const float reference_exposure = meta0.shutter_s * meta0.iso /
        (meta0.aperture>0?meta0.aperture*meta0.aperture:1.0f);
    for (int idx : light_indices) {
        const auto& frame=uframes[idx];
        const float exposure = frame.shutter_s * frame.iso /
            (frame.aperture>0?frame.aperture*frame.aperture:1.0f);
        if (reference_exposure > 0 && exposure > 0 &&
            std::fabs(std::log2(exposure/reference_exposure)) > 0.15f) bracket_exposures = true;
    }
    const auto& cache_prof = heraw_cache::host_cache_profile();
    heraw_core::Execution core_execution;
    core_execution.tile = cache_prof.ibp_tile();
    core_execution.diagnostic_stride=67;
    core_execution.parallel = [&](int count, const heraw_core::RangeTask& task) {
        const int max_chunk = clamp_i(count / (std::max(1, n_threads) * 4), 1, 64);
        parallel_for_rows(count, n_threads, [&](int begin, int end, int) { task(begin, end); }, max_chunk);
    };
    if (group_idx_1based == 1) {
        const auto pwr = heraw_cache::query_power_status();
        char pwr_buf[32];
        if (!pwr.has_battery) {
            std::snprintf(pwr_buf, sizeof(pwr_buf), "AC");
        } else if (pwr.battery_percent >= 0) {
            std::snprintf(pwr_buf, sizeof(pwr_buf), "%s(%d%%)", pwr.on_battery ? "DC" : "AC", pwr.battery_percent);
        } else {
            std::snprintf(pwr_buf, sizeof(pwr_buf), "%s", pwr.on_battery ? "DC" : "AC");
        }
        const char* active_simd = heraw_cache::use_avx512() ? "avx512"
            : (HERAW_ARCH_X86 ? "avx2" : heraw_cache::simd_policy_name(cfg.simd));
        log_err(
            "[Cache] arch=%s L1D=%zuKB L2=%zuKB/core L3=%zuMB (slices=%d) threads=%d cores=%s prio=%s simd=%s pwr=%s s4_xt=%d dct_xt=%d ibp_tile=%d rl_band=%d\n",
            cache_prof.arch_tag, cache_prof.l1d_bytes >> 10, cache_prof.l2_per_core_bytes >> 10,
            cache_prof.l3_total_bytes >> 20, cache_prof.llc_slices,
            n_threads, heraw_cache::core_policy_name(cfg.cpu_cores),
            heraw_cache::task_priority_name(cfg.priority),
            active_simd,
            pwr_buf,
            cache_prof.stage4_x_tile(), cache_prof.dct_tile_x(),
            cache_prof.ibp_tile(), cache_prof.rl_band_rows(W1 * 2, n_threads));
    }
    const std::string olpf_profile = cfg.olpf_mode == "auto" ?
        heraw_plan::detect_sensor_olpf_profile(meta0.make, meta0.model, W1, H1, is_xtrans) : cfg.olpf_mode;
    const int olpf_mode = olpf_profile == "gauss16" ? 2 : (olpf_profile == "weak4" ? 1 : 0);
    uint8_t cfa6x6[6][6];
    std::memcpy(cfa6x6, meta0.cfa6x6, sizeof(cfa6x6));
    std::vector<std::string> frame_paths(N);
    for (int n = 0; n < N; ++n) frame_paths[n] = uframes[light_indices[n]].path;
    std::string anchor_raw_path = frame_paths[0];
    auto raw_path_default_stem = [](const std::string& rp) -> std::string {
        std::string base;
        uint32_t fidx = 0u;
        bool has_frame = heraw_wvdc::split_virtual_raw_path(rp, base, fidx);
        std::string stem = std::filesystem::u8path(base).replace_extension().u8string();
        if (has_frame) {
            char fbuf[16];
            std::snprintf(fbuf, sizeof(fbuf), "_f%04u", fidx);
            stem += fbuf;
        }
        return stem;
    };
    const std::string anchor_default_stem = raw_path_default_stem(anchor_raw_path);
    const float nom_black = 0.25f * (meta0.black_c[0] + meta0.black_c[1] + meta0.black_c[2] + meta0.black_c[3]);
    const float inv_full_range = 1.0f / std::max(1.0f, meta0.white - nom_black);

    // Blind in-scene G1/G2 block-median pedestal self-calibration + negative pedestal drift guard
    float self_black_delta[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!is_xtrans) {
        const auto& uf0 = uframes[light_indices[0]];
        const int bsz = 32;
        const int nb_y = Hh / bsz, nb_x = Wh / bsz;
        if (nb_y >= 4 && nb_x >= 4) {
            const size_t n_blk = (size_t)nb_y * nb_x;
            std::array<std::vector<float>, 4> bm;
            for (int p = 0; p < 4; ++p) bm[p].resize(n_blk);

            parallel_for_rows(nb_y, n_threads, [&](int by0, int by1, int) {
                const double inv_n = 1.0 / (double)(bsz * bsz);
                for (int by = by0; by < by1; ++by) {
                    for (int bx = 0; bx < nb_x; ++bx) {
                        double s[4] = {0, 0, 0, 0};
                        for (int r = 0; r < bsz; ++r) {
                            const int r_abs = 2 * (by * bsz + r);
                            const uint16_t* row0 = uf0.bayer.data() + (size_t)r_abs * W1 + 2 * bx * bsz;
                            const uint16_t* row1 = row0 + W1;
                            for (int c = 0; c < bsz; ++c) {
                                s[0] += row0[2 * c];
                                s[1] += row0[2 * c + 1];
                                s[2] += row1[2 * c];
                                s[3] += row1[2 * c + 1];
                            }
                        }
                        const size_t bi = (size_t)by * nb_x + bx;
                        bm[0][bi] = (float)(s[0] * inv_n) - uf0.black_c[0];
                        bm[1][bi] = (float)(s[1] * inv_n) - uf0.black_c[1];
                        bm[2][bi] = (float)(s[2] * inv_n) - uf0.black_c[2];
                        bm[3][bi] = (float)(s[3] * inv_n) - uf0.black_c[3];
                    }
                }
            }, 1);

            std::vector<float> dg21_blocks;
            dg21_blocks.reserve(n_blk);
            const float half_span = 0.5f * (uf0.white - nom_black);
            for (size_t bi = 0; bi < n_blk; ++bi) {
                if (std::max(bm[1][bi], bm[2][bi]) < half_span) {
                    dg21_blocks.push_back(bm[2][bi] - bm[1][bi]);
                }
            }
            if (dg21_blocks.size() >= 16) {
                float dg21 = clamp_f(quick_percentile(dg21_blocks, 0.50f), -20.0f, 20.0f);
                self_black_delta[1] -= 0.5f * dg21;
                self_black_delta[2] += 0.5f * dg21;
            }
            for (int p = 0; p < 4; ++p) {
                float p005 = quick_percentile(bm[p], 0.005f);
                if (p005 < 0.0f) {
                    self_black_delta[p] += clamp_f(p005, -256.0f, 0.0f);
                }
            }
        }
    }

    heraw_wvn::BurstWaveletContext wctx;
    wctx.init_from_unpacked_frames(uframes, light_indices, self_black_delta, H1, W1);

    const bool is_astro = (cfg.preset == PipelinePreset::ASTRO);

    // Calibrate light frames into signed linear float [-0.1, 1.05] & repair persistent/extreme hot pixels
    // (In Astro mode & on pinpoint speculars, WB-normalized cross-channel 1x Bayer neighbor gate prevents "Star Eater" clipping!)
    std::vector<std::vector<float>> frames_lin(N, std::vector<float>((size_t)H1 * W1));
    size_t warm_pixel_count = 0;

    if (is_xtrans) {
        std::vector<std::atomic<size_t>> xtrans_hot_cnt(1);
        xtrans_hot_cnt[0].store(0, std::memory_order_relaxed);
        for (int n = 0; n < N; ++n) {
            const auto& uf = uframes[light_indices[n]];
            parallel_for_rows(H1, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r)
                    for (int c = 0; c < W1; ++c) {
                        const float b = uf.black_6x6[r % 6][c % 6];
                        frames_lin[n][(size_t)r * W1 + c] =
                            ((float)uf.bayer[(size_t)r * W1 + c] - b) / std::max(1.0f, uf.white - b);
                    }
            });
            float x_sig = std::max(1e-4f, estimate_hh_sigma(frames_lin[n].data(), H1, W1, 8));
            parallel_for_rows(H1 - 2, n_threads, [&](int r0, int r1, int) {
                size_t local_hot = 0;
                for (int rr = r0; rr < r1; ++rr) {
                    int r = rr + 1;
                    for (int c = 1; c < W1 - 1; ++c) {
                        size_t idx = (size_t)r * W1 + c;
                        float v = frames_lin[n][idx];
                        int ci = cfa6x6[r % 6][c % 6];
                        float w_self = std::max(0.25f, uf.wb[ci]);
                        float nb_max = -1.0f, nb_sum = 0.0f;
                        int same_cnt = 0; float same_sum = 0.0f;
                        for (int dy = -1; dy <= 1; ++dy) {
                            for (int dx = -1; dx <= 1; ++dx) {
                                if (dy == 0 && dx == 0) continue;
                                int nr = r + dy, nc = c + dx;
                                int nci = cfa6x6[nr % 6][nc % 6];
                                float nv = frames_lin[n][(size_t)nr * W1 + nc];
                                float nv_wb = nv * (std::max(0.25f, uf.wb[nci]) / w_self);
                                nb_max = std::max(nb_max, nv_wb);
                                nb_sum += nv_wb;
                                if (nci == ci) { same_sum += nv; ++same_cnt; }
                            }
                        }
                        float bg = 0.125f * nb_sum;
                        float diff = v - bg;
                        float eff_xsig = wctx.active
                            ? std::hypot(x_sig, wctx.eval_plane_qstd(r, std::max(0.0f, bg), ci != 1))
                            : x_sig;
                        if (diff > 12.0f * eff_xsig && (nb_max - bg) <= (is_astro ? 0.14f : 0.22f) * diff) {
                            frames_lin[n][idx] = (same_cnt > 0) ? (same_sum / (float)same_cnt) : bg;
                            ++local_hot;
                        }
                    }
                }
                if (local_hot > 0 && n == 0) xtrans_hot_cnt[0].fetch_add(local_hot, std::memory_order_relaxed);
            });
        }
        warm_pixel_count = xtrans_hot_cnt[0].load(std::memory_order_relaxed);
    } else {
        std::vector<float> plane_cur((size_t)Hh * Wh);
        std::vector<float> med_cur((size_t)Hh * Wh);
        std::vector<uint8_t> persist_cnt((size_t)Hh * Wh, 0);
        const int p_chan[4] = {0, 1, 1, 2};
        const int cross_chan[4] = {1, 0, 2, 1};

        for (int p = 0; p < 4; ++p) {
            int py = p >> 1, px = p & 1;
            std::fill(persist_cnt.begin(), persist_cnt.end(), 0);

            for (int n = 0; n < N; ++n) {
                const auto& uf = uframes[light_indices[n]];
                const float b_const = uf.black_c[p] + self_black_delta[p];
                const float b_avg_u = 0.25f * (uf.black_c[0] + uf.black_c[1] + uf.black_c[2] + uf.black_c[3]);
                const float wb_self = std::max(0.25f, uf.wb[p_chan[p]]);
                // Orthogonal neighbors of R/B are G1/G2; orthogonal neighbors of G1 are H:R, V:B; G2 are H:B, V:R
                const float wb_v = (p == 0 || p == 3) ? (1.0f / wb_self) : ((p == 1 ? uf.wb[2] : uf.wb[0]) / wb_self);
                const float wb_h = (p == 0 || p == 3) ? (1.0f / wb_self) : ((p == 1 ? uf.wb[0] : uf.wb[2]) / wb_self);
                (void)cross_chan;

                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        for (int c = 0; c < Wh; ++c) {
                            size_t hi = (size_t)r * Wh + c;
                            float raw_v = (float)uf.bayer[(size_t)(2 * r + py) * W1 + (2 * c + px)];
                            plane_cur[hi] = (raw_v - b_const) * inv_full_range;
                        }
                    }
                });
                median3x3_plane(plane_cur.data(), med_cur.data(), Hh, Wh, n_threads);
                float p_sig = std::max(1e-4f, estimate_hh_sigma(plane_cur.data(), Hh, Wh, 8));
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        int r1 = 2 * r + py;
                        int r1m = clamp_i(r1 - 1, 0, H1 - 1), r1p = clamp_i(r1 + 1, 0, H1 - 1);
                        for (int c = 0; c < Wh; ++c) {
                            size_t hi = (size_t)r * Wh + c;
                            float val = plane_cur[hi];
                            float med_v = med_cur[hi];
                            float diff = val - med_v;
                            float eff_psig = wctx.active
                                ? std::hypot(p_sig, wctx.eval_plane_qstd(r1, std::max(0.0f, med_v), (p == 0 || p == 3)))
                                : p_sig;
                            if (diff > 5.5f * eff_psig) {
                                int c1 = 2 * c + px;
                                int c1m = clamp_i(c1 - 1, 0, W1 - 1), c1p = clamp_i(c1 + 1, 0, W1 - 1);
                                float nb0 = ((float)uf.bayer[(size_t)r1m * W1 + c1] - b_avg_u) * inv_full_range * wb_v - med_v;
                                float nb1 = ((float)uf.bayer[(size_t)r1p * W1 + c1] - b_avg_u) * inv_full_range * wb_v - med_v;
                                float nb2 = ((float)uf.bayer[(size_t)r1 * W1 + c1m] - b_avg_u) * inv_full_range * wb_h - med_v;
                                float nb3 = ((float)uf.bayer[(size_t)r1 * W1 + c1p] - b_avg_u) * inv_full_range * wb_h - med_v;
                                float cross_max = std::max(std::max(nb0, nb1), std::max(nb2, nb3));
                                bool optical_pinpoint = cross_max > (is_astro ? 0.14f : 0.24f) * diff;
                                if (!optical_pinpoint) {
                                    persist_cnt[hi]++;
                                    if (diff > 9.5f * eff_psig) val = med_v;
                                }
                            }
                            frames_lin[n][(size_t)r1 * W1 + (2 * c + px)] = val;
                        }
                    }
                });
            }

            if (N >= 2) {
                const int min_persist = std::max(2, (N + 1) / 2);
                std::vector<size_t> warm_locs;
                for (size_t hi = 0; hi < (size_t)Hh * Wh; ++hi) {
                    if (persist_cnt[hi] >= min_persist) warm_locs.push_back(hi);
                }
                warm_pixel_count += warm_locs.size();
                if (N >= 3 && !warm_locs.empty()) {
                    for (int n = 0; n < N; ++n) {
                        std::vector<float> repl(warm_locs.size());
                        for (size_t wi = 0; wi < warm_locs.size(); ++wi) {
                            int r = (int)(warm_locs[wi] / (size_t)Wh);
                            int c = (int)(warm_locs[wi] % (size_t)Wh);
                            auto sp = [&](int rr, int cc) -> float {
                                rr = clamp_i(rr, 0, Hh - 1);
                                cc = clamp_i(cc, 0, Wh - 1);
                                return frames_lin[n][(size_t)(2 * rr + py) * W1 + (2 * cc + px)];
                            };
                            repl[wi] = median9_vals(
                                sp(r - 1, c - 1), sp(r - 1, c), sp(r - 1, c + 1),
                                sp(r,     c - 1), sp(r,     c), sp(r,     c + 1),
                                sp(r + 1, c - 1), sp(r + 1, c), sp(r + 1, c + 1));
                        }
                        for (size_t wi = 0; wi < warm_locs.size(); ++wi) {
                            int r = (int)(warm_locs[wi] / (size_t)Wh);
                            int c = (int)(warm_locs[wi] % (size_t)Wh);
                            frames_lin[n][(size_t)(2 * r + py) * W1 + (2 * c + px)] = repl[wi];
                        }
                    }
                }
            }
        }
    }

    // Free raw uint16 buffers now that calibrated float frames are built
    std::string cam_make = meta0.make;
    std::string cam_model = meta0.model;
    int cam_flip = meta0.flip;
    float cam_wb[3] = {meta0.wb[0], meta0.wb[1], meta0.wb[2]};
    double cam_xyz[3][3], rgb_cam[3][3];
    std::memcpy(cam_xyz, meta0.cam_xyz, sizeof(cam_xyz));
    std::memcpy(rgb_cam, meta0.rgb_cam, sizeof(rgb_cam));
    if (free_all_uframes) {
        uframes.clear();
        uframes.shrink_to_fit();
    } else {
        for (int idx : light_indices) {
            uframes[idx].bayer.clear();
            uframes[idx].bayer.shrink_to_fit();
        }
    }

    // Build half-res Green planes & select sharpest middle reference frame
    std::vector<std::vector<float>> g_halfs(N, std::vector<float>((size_t)Hh * Wh));
    std::vector<double> sharp_scores(N, 0.0);
    {
        UninitBuf<float> gb((size_t)Hh * Wh);
        const int g0_dx = (!is_xtrans && cfa6x6[0][1] == 1) ? 1 : 0;
        const int g1_dx = 1 - g0_dx;
        const bool fast_bayer_g = !is_xtrans && (cfa6x6[0][g0_dx] == 1 && cfa6x6[1][g1_dx] == 1);
        for (int n = 0; n < N; ++n) {
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                if (fast_bayer_g) {
                    for (int r = r0; r < r1; ++r) {
                        const float* row0 = frames_lin[n].data() + (size_t)(2 * r) * W1;
                        const float* row1 = row0 + W1;
                        float* dst = g_halfs[n].data() + (size_t)r * Wh;
                        for (int c = 0; c < Wh; ++c) {
                            dst[c] = 0.5f * (row0[2 * c + g0_dx] + row1[2 * c + g1_dx]);
                        }
                    }
                } else {
                    for (int r = r0; r < r1; ++r) {
                        for (int c = 0; c < Wh; ++c) {
                            float sum = 0.0f;
                            int count = 0;
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx) {
                                    int y = 2 * r + dy, x = 2 * c + dx;
                                    if (cfa6x6[y % 6][x % 6] == 1) {
                                        sum += frames_lin[n][(size_t)y * W1 + x];
                                        ++count;
                                    }
                                }
                            g_halfs[n][(size_t)r * Wh + c] = count ? sum / count :
                                sample_cfa_channel(frames_lin[n].data(), H1, W1, 2 * r, 2 * c, 1, cfa6x6);
                        }
                    }
                }
            });
            sep_gaussian_2d(g_halfs[n].data(), gb.data(), Hh, Wh, 1.1f, n_threads);
            const int sr0 = Hh / 5, sr1 = 4 * Hh / 5;
            const int sc0 = Wh / 5, sc1 = 4 * Wh / 5;
            const int n_sr = std::max(0, sr1 - sr0);
            const int64_t scnt = (int64_t)n_sr * std::max(0, sc1 - sc0);
            std::vector<double> row_se((size_t)n_sr, 0.0);
            if (scnt > 0) {
                const float* gbp = gb.data();
                parallel_for_rows(n_sr, n_threads, [&](int dr0, int dr1, int) {
                    for (int dr = dr0; dr < dr1; ++dr) {
                        const int r = sr0 + dr;
                        double rse = 0.0;
                        for (int c = sc0; c < sc1; ++c) {
                            float lap = -4.0f * gbp[(size_t)r * Wh + c]
                                      + gbp[(size_t)(r - 1) * Wh + c] + gbp[(size_t)(r + 1) * Wh + c]
                                      + gbp[(size_t)r * Wh + c - 1]   + gbp[(size_t)r * Wh + c + 1];
                            rse += (double)lap * (double)lap;
                        }
                        row_se[(size_t)dr] = rse;
                    }
                });
            }
            double se = 0.0;
            for (double v : row_se) se += v;
            sharp_scores[n] = se / (double)std::max<int64_t>(1, scnt);
        }
    }

    // The planner has placed the selected AF/explicit anchor at index zero.

    // Blind Poisson-Gaussian noise estimation on single-photodiode linear sensor scale:
    // Var_1px(I) = sig0^2 + g_shot * max(0, I)
    std::vector<float> g0_smooth((size_t)Hh * Wh);
    sep_gaussian_2d(g_halfs[0].data(), g0_smooth.data(), Hh, Wh, 2.5f, n_threads);
    const int NBINS = 12;
    std::vector<float> g_samples;
    g_samples.reserve(g0_smooth.size() / 17 + 1);
    for (size_t i = 0; i < g0_smooth.size(); i += 17) g_samples.push_back(g0_smooth[i]);
    std::vector<float> bin_edges(NBINS + 1);
    for (int b = 0; b <= NBINS; ++b) bin_edges[b] = quick_percentile(g_samples, (float)b / (float)NBINS);

    wctx.maybe_activate_blind_dng(anchor_raw_path, g_halfs[0].data(), g0_smooth.data(), bin_edges, Hh, Wh);
    double pg_sig0_sq = 1e-6, pg_gain = 1e-4;
    if (wctx.active) {
        auto par_wvn = [](int tot, int nt, auto&& fn, int chk) {
            parallel_for_rows(tot, nt, std::forward<decltype(fn)>(fn), chk);
        };
        wctx.estimate_sensor_pg(
            g_halfs[0].data(), g0_smooth.data(), bin_edges, Hh, Wh,
            cam_iso, n_threads, par_wvn, pg_sig0_sq, pg_gain);
        g0_smooth.clear(); g0_smooth.shrink_to_fit();
    } else {
        std::array<std::vector<float>, 12> hh_bins;
        std::array<double, 12> i_sums{};
    const size_t approx_per_bin = ((size_t)(Hh / 2) * (size_t)(Wh / 2)) / NBINS + 64;
    for (int b = 0; b < NBINS; ++b) hh_bins[b].reserve(approx_per_bin);
    {
        const int step = is_xtrans ? 6 : 2;
        const float* __restrict f0 = frames_lin[0].data();
        const int n_hh_rows = (Hh > 4) ? ((Hh - 3 - 2 + 1) / 2) : 0;
        const int hh_nt = std::max(1, std::min(n_threads, n_hh_rows));
        if (hh_nt <= 1) {
            for (int r = 2; r + 2 < Hh; r += 2) {
                const int yy = 2 * r;
                if (yy + step >= H1) continue;
                const int ym = yy % 6;
                for (int c = 2; c + 2 < Wh; c += 2) {
                    const float iv = g0_smooth[(size_t)r * Wh + c];
                    bool have_hh = false;
                    float hh_val = 0.0f;
                    const double iv_pos = std::max(0.0f, iv);
                    for (int b = 0; b < NBINS; ++b) {
                        if (iv >= bin_edges[b] && iv <= bin_edges[b + 1] + 1e-6f) {
                            if (!have_hh) {
                                int xx_use = 2 * c + 1;
                                if (is_xtrans) {
                                    while (xx_use + step < W1 && cfa6x6[ym][xx_use % 6] != 1) ++xx_use;
                                }
                                if (xx_use + step >= W1) break;
                                const float v00 = f0[(size_t)yy * W1 + xx_use];
                                const float v01 = f0[(size_t)yy * W1 + xx_use + step];
                                const float v10 = f0[(size_t)(yy + step) * W1 + xx_use];
                                const float v11 = f0[(size_t)(yy + step) * W1 + xx_use + step];
                                hh_val = std::fabs(0.5f * (v00 - v01 - v10 + v11));
                                have_hh = true;
                            }
                            hh_bins[b].push_back(hh_val);
                            i_sums[b] += iv_pos;
                        }
                    }
                }
            }
        } else {
            struct alignas(64) HhSlice {
                std::array<std::vector<float>, NBINS> bins;
                std::array<double, NBINS> sums{};
            };
            std::vector<HhSlice> slices(hh_nt);
            const int rows_per_slice = (n_hh_rows + hh_nt - 1) / hh_nt;
            parallel_for_rows(hh_nt, hh_nt, [&](int s0, int s1, int) {
                for (int si = s0; si < s1; ++si) {
                    auto& sl = slices[si];
                    const int ri0 = si * rows_per_slice;
                    const int ri1 = std::min(n_hh_rows, ri0 + rows_per_slice);
                    if (ri0 >= ri1) continue;
                    for (int b = 0; b < NBINS; ++b) sl.bins[b].reserve((size_t)(ri1 - ri0) * (size_t)(Wh / 2) / NBINS + 64);
                    for (int ri = ri0; ri < ri1; ++ri) {
                        const int r = 2 + 2 * ri;
                        const int yy = 2 * r;
                        if (yy + step >= H1) continue;
                        const int ym = yy % 6;
                        for (int c = 2; c + 2 < Wh; c += 2) {
                            const float iv = g0_smooth[(size_t)r * Wh + c];
                            bool have_hh = false;
                            float hh_val = 0.0f;
                            const double iv_pos = std::max(0.0f, iv);
                            for (int b = 0; b < NBINS; ++b) {
                                if (iv >= bin_edges[b] && iv <= bin_edges[b + 1] + 1e-6f) {
                                    if (!have_hh) {
                                        int xx_use = 2 * c + 1;
                                        if (is_xtrans) {
                                            while (xx_use + step < W1 && cfa6x6[ym][xx_use % 6] != 1) ++xx_use;
                                        }
                                        if (xx_use + step >= W1) break;
                                        const float v00 = f0[(size_t)yy * W1 + xx_use];
                                        const float v01 = f0[(size_t)yy * W1 + xx_use + step];
                                        const float v10 = f0[(size_t)(yy + step) * W1 + xx_use];
                                        const float v11 = f0[(size_t)(yy + step) * W1 + xx_use + step];
                                        hh_val = std::fabs(0.5f * (v00 - v01 - v10 + v11));
                                        have_hh = true;
                                    }
                                    sl.bins[b].push_back(hh_val);
                                    sl.sums[b] += iv_pos;
                                }
                            }
                        }
                    }
                }
            }, 1);
            for (int b = 0; b < NBINS; ++b) {
                size_t tot = 0;
                for (int si = 0; si < hh_nt; ++si) tot += slices[si].bins[b].size();
                hh_bins[b].reserve(tot);
                for (int si = 0; si < hh_nt; ++si) {
                    hh_bins[b].insert(hh_bins[b].end(), slices[si].bins[b].begin(), slices[si].bins[b].end());
                    i_sums[b] += slices[si].sums[b];
                }
            }
        }
    }
    std::vector<double> fit_A, fit_y;
    for (int b = 0; b < NBINS; ++b) {
        if (hh_bins[b].size() >= 64) {
            float s_est = quick_percentile(hh_bins[b], 0.15f) / 0.190f;
            double mean_i = i_sums[b] / (double)hh_bins[b].size();
            fit_A.push_back(1.0);
            fit_A.push_back(mean_i);
            fit_y.push_back((double)s_est * (double)s_est);
        }
    }
        g0_smooth.clear(); g0_smooth.shrink_to_fit();
        std::vector<double> pg_coef;
        if (fit_y.size() >= 3 && solve_least_squares((int)fit_y.size(), 2, fit_A, fit_y, pg_coef)) {
            pg_sig0_sq = std::max(1e-8, pg_coef[0]);
            pg_gain = std::max(1e-6, pg_coef[1]);
        } else {
            float s_all = estimate_hh_sigma(g_halfs[0].data(), Hh, Wh, 2) * std::sqrt(2.0f);
            pg_sig0_sq = (double)s_all * (double)s_all;
            pg_gain = 0.05 * std::sqrt(pg_sig0_sq);
        }
    }
    const float sig0_1px = (float)std::sqrt(pg_sig0_sq);
    {
        char msg2[240];
        double dt2 = elapsed_s(t0);
        if (wctx.active) {
            const char* mode_tag = wctx.from_wvdc ? (wctx.is_arw6 ? "ARW6-wv" : "Tico-wv") : "DNG-wv";
            std::snprintf(msg2, sizeof(msg2),
                "[2/6] Calibrated %d light frame(s) | %s PG: sig0=%.5f, gain=%.5f, qY=%.5f, qC=%.5f (%.2fs)",
                N, mode_tag, sig0_1px, (float)pg_gain,
                wctx.mean_q_luma_code_std, wctx.mean_q_chroma_code_std, dt2);
        } else {
            std::snprintf(msg2, sizeof(msg2),
                "[2/6] Calibrated %d light frame(s) | blind PG noise: sig0=%.5f, gain=%.5f (%.2fs)",
                N, sig0_1px, (float)pg_gain, dt2);
        }
        emit_stage(2, 26, dt2, msg2);
    }

    // --- Stage 3: Subpixel Pyramid Alignment + Motion/Parallax De-Ghosting ---
    cost_mark("calibrate-noise");
    t0 = std::chrono::steady_clock::now();
    heraw_core::Noise core_noise{(float)pg_sig0_sq, (float)pg_gain};
    heraw_core::ExposureStats exposure_stats;
    if (bracket_exposures) {
        exposure_stats = heraw_core::exposure_normalize(frames_lin, g_halfs, H1, W1, core_noise, {}, {}, {}, core_execution);
        core_noise.frame_read_variances = exposure_stats.read_variances.data();
        core_noise.frame_shot_coefficients = exposure_stats.shot_coefficients.data();
        core_noise.frame_saturation_levels = exposure_stats.saturation_levels.data();
        log_err("[C7] Exposure-normalized %d frames using %zu linear samples.\n", N, exposure_stats.samples);
    }
    cost_mark("exposure-normalize");
    std::vector<std::vector<float>> flows_fy(N), flows_fx(N), mw_half(N), mw_align(N);
    flows_fy[0].assign((size_t)Hh * Wh, 0.0f);
    flows_fx[0].assign((size_t)Hh * Wh, 0.0f);
    mw_half[0].assign((size_t)Hh * Wh, 1.0f);
    mw_align[0].assign((size_t)Hh * Wh, 1.0f);

    float pre_sigma = clamp_f(0.45f + 12.0f * sig0_1px, 0.5f, 1.6f);
    std::vector<float> g0_pre((size_t)Hh * Wh);
    sep_gaussian_2d(g_halfs[0].data(), g0_pre.data(), Hh, Wh, pre_sigma, n_threads);
    const int H1p_ref = Hh / 2, W1p_ref = Wh / 2;
    const int H2p_ref = H1p_ref / 2, W2p_ref = W1p_ref / 2;
    std::vector<float> g0_r1((size_t)std::max(1, H1p_ref * W1p_ref));
    std::vector<float> g0_r2((size_t)std::max(1, H2p_ref * W2p_ref));
    if (H1p_ref > 0 && W1p_ref > 0) box_downsample_2x(g0_pre.data(), Hh, Wh, g0_r1.data(), n_threads);
    if (H2p_ref > 0 && W2p_ref > 0) box_downsample_2x(g0_r1.data(), H1p_ref, W1p_ref, g0_r2.data(), n_threads);

    std::vector<double> sharp_sorted = sharp_scores;
    std::sort(sharp_sorted.begin(), sharp_sorted.end());
    double med_sharp = sharp_sorted[N / 2];

    std::vector<std::vector<float>> gi_pres(N);
    std::vector<CurIntegral> frame_l0_sats(N);
    CurIntegral align_l1_ws;
    std::vector<StarPtHalf> astro_stars0;
    bool stars0_probed = false;
    std::vector<float> astro_sky_accum;
    int astro_celestial_locked = 0;
    if (is_astro && N >= 2) {
        astro_stars0 = detect_stars_half(g_halfs[0].data(), Hh, Wh, sig0_1px, n_threads);
        stars0_probed = true;
        astro_sky_accum.assign((size_t)Hh * Wh, 0.0f);
    }

    for (int n = 1; n < N; ++n) {
        gi_pres[n].resize((size_t)Hh * Wh);
        sep_gaussian_2d(g_halfs[n].data(), gi_pres[n].data(), Hh, Wh, pre_sigma, n_threads);
        double q = 0.0;
        align_frame_pyramid(g0_pre.data(), gi_pres[n].data(), Hh, Wh, flows_fy[n], flows_fx[n], q, n_threads, !is_astro,
                            (H1p_ref > 0 && W1p_ref > 0) ? g0_r1.data() : nullptr,
                            (H2p_ref > 0 && W2p_ref > 0) ? g0_r2.data() : nullptr,
                            &frame_l0_sats[n], &align_l1_ws);
        if ((is_astro || q < 0.48) && N >= 2) {
            if (!stars0_probed) {
                astro_stars0 = detect_stars_half(g_halfs[0].data(), Hh, Wh, sig0_1px, n_threads);
                stars0_probed = true;
                if (astro_sky_accum.empty()) astro_sky_accum.assign((size_t)Hh * Wh, 0.0f);
            }
            bool ref_has_stars = (astro_stars0.size() >= 2) || (astro_stars0.size() == 1 && astro_stars0[0].snr >= 24.0f);
            if (ref_has_stars) {
                auto stars_n = detect_stars_half(g_halfs[n].data(), Hh, Wh, sig0_1px, n_threads);
                std::vector<float> fy_sky, fx_sky;
                if (fit_celestial_poly_flow(astro_stars0, stars_n, Hh, Wh, fy_sky, fx_sky, n_threads)) {
                    if (is_astro) {
                        blend_astro_dual_domain_flow(g0_pre.data(), gi_pres[n].data(), Hh, Wh, astro_stars0,
                                                     fy_sky, fx_sky, flows_fy[n], flows_fx[n], astro_sky_accum, sig0_1px, n_threads);
                    } else {
                        flows_fy[n].swap(fy_sky);
                        flows_fx[n].swap(fx_sky);
                    }
                    ++astro_celestial_locked;
                }
            }
        }
    }

    // Continuous 3-channel physical field maps on (Hh x Wh):
    //   field_motion[i] : [0..1] anti-hollow morphological motion matte (R channel of Field Map)
    //   field_phase[i]  : [0..1] multi-frame subpixel phase & SNR confidence (G channel of Field Map)
    //   field_focus[i]  : [0..1] optical focus / structure acutance field (B channel of Field Map)
    std::vector<float> field_motion((size_t)Hh * Wh, 0.0f);
    std::vector<float> field_phase((size_t)Hh * Wh, N == 1 ? 0.25f : 1.0f);
    std::vector<float> field_focus((size_t)Hh * Wh, 0.5f);
    std::vector<float> field_coherence((size_t)Hh * Wh, 0.0f);
    std::vector<float> m_wide_half((size_t)Hh * Wh, 0.0f);

    if (N >= 2) {
        std::vector<std::vector<float>> wg_fine(N, std::vector<float>((size_t)Hh * Wh));
        std::vector<std::vector<float>> wg_wide(N, std::vector<float>((size_t)Hh * Wh));
        sep_gaussian_2d(g_halfs[0].data(), wg_fine[0].data(), Hh, Wh, 1.4f, n_threads);
        sep_gaussian_2d(g_halfs[0].data(), wg_wide[0].data(), Hh, Wh, 4.0f, n_threads);

        {
            UninitBuf<float> warped((size_t)Hh * Wh);
            for (int n = 1; n < N; ++n) {
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        for (int c = 0; c < Wh; ++c) {
                            size_t idx = (size_t)r * Wh + c;
                            warped[idx] = bilerp_clamped(g_halfs[n].data(), Hh, Wh, (float)r + flows_fy[n][idx], (float)c + flows_fx[n][idx]);
                        }
                    }
                });
                sep_gaussian_2d(warped.data(), wg_fine[n].data(), Hh, Wh, 1.4f, n_threads);
                sep_gaussian_2d(warped.data(), wg_wide[n].data(), Hh, Wh, 4.0f, n_threads);
            }
        }

        std::vector<float> ref_f((size_t)Hh * Wh), ref_w((size_t)Hh * Wh);
        std::vector<float> grad_f((size_t)Hh * Wh), grad_w((size_t)Hh * Wh);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                int rm = clamp_i(r - 1, 0, Hh - 1), rp = clamp_i(r + 1, 0, Hh - 1);
                for (int c = 0; c < Wh; ++c) {
                    int cm = clamp_i(c - 1, 0, Wh - 1), cp = clamp_i(c + 1, 0, Wh - 1);
                    size_t i = (size_t)r * Wh + c;
                    float iv = std::max(0.0f, wg_wide[0][i]);
                    float qf_1 = wctx.active ? wctx.eval_half_qvar_fine(r, iv) : 0.0f;
                    float qw_1 = wctx.active ? wctx.eval_half_qvar_wide(r, iv) : 0.0f;
                    float sf_1 = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv) + qf_1) * 0.35f + 0.001f;
                    float sw_1 = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv) + qw_1) * 0.14f + 0.0006f;

                    float af = wg_fine[0][i], wf = 1.0f;
                    float aw = wg_wide[0][i], ww = 1.0f;
                    for (int n = 1; n < N; ++n) {
                        if (std::fabs(wg_fine[n][i] - wg_fine[0][i]) < 2.5f * sf_1) { af += wg_fine[n][i]; wf += 1.0f; }
                        if (std::fabs(wg_wide[n][i] - wg_wide[0][i]) < 2.5f * sw_1) { aw += wg_wide[n][i]; ww += 1.0f; }
                    }
                    ref_f[i] = af / wf;
                    ref_w[i] = aw / ww;
                    grad_f[i] = 0.5f * std::hypot(wg_fine[0][(size_t)r*Wh+cp] - wg_fine[0][(size_t)r*Wh+cm],
                                                  wg_fine[0][(size_t)rp*Wh+c] - wg_fine[0][(size_t)rm*Wh+c]);
                    grad_w[i] = 0.5f * std::hypot(wg_wide[0][(size_t)r*Wh+cp] - wg_wide[0][(size_t)r*Wh+cm],
                                                  wg_wide[0][(size_t)rp*Wh+c] - wg_wide[0][(size_t)rm*Wh+c]);
                }
            }
        });

        const float zf_lim = (is_astro ? 4.4f : 3.6f) / std::max(0.4f, cfg.deghost_strength);
        const float zw_lim = (is_astro ? 4.6f : 4.0f) / std::max(0.4f, cfg.deghost_strength);
        const float grad_tol_f = is_astro ? 0.90f : 0.65f;
        float w_frame_sum = 0.0f;
        {
            UninitBuf<float> bad((size_t)Hh * Wh), bad_clean((size_t)Hh * Wh), bad_closed((size_t)Hh * Wh), bad_blur((size_t)Hh * Wh);
            for (int n = 1; n < N; ++n) {
                double s_ratio = sharp_scores[n] / std::max(1e-12, med_sharp);
                float w_frame = (s_ratio < 0.97) ? clamp_f((float)std::pow(s_ratio, 4.0), 0.30f, 1.0f) : 1.0f;
                w_frame_sum += w_frame;

                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        for (int c = 0; c < Wh; ++c) {
                            size_t i = (size_t)r * Wh + c;
                            float iv = std::max(0.0f, ref_w[i]);
                            float base_v = 0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv);
                            float sf = (wctx.active
                                ? std::sqrt(base_v + wctx.eval_half_qvar_fine(r, iv))
                                : std::sqrt(base_v)) * 0.32f + 0.0012f;
                            float sw = (wctx.active
                                ? std::sqrt(base_v + wctx.eval_half_qvar_wide(r, iv))
                                : std::sqrt(base_v)) * 0.13f + 0.0007f;
                            float zf = (std::fabs(wg_fine[n][i] - ref_f[i]) - grad_tol_f * grad_f[i]) / sf;
                            float zw = (std::fabs(wg_wide[n][i] - ref_w[i]) - 0.55f * grad_w[i]) / sw;
                            bad[i] = (zf > zf_lim || zw > zw_lim) ? 1.0f : 0.0f;
                        }
                    }
                });
                median3x3_plane(bad.data(), bad_clean.data(), Hh, Wh, n_threads);
                if (!is_astro) {
                    morph_close_4x_field(bad_clean.data(), bad_closed.data(), Hh, Wh, n_threads);
                    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                        for (size_t i = (size_t)r0 * Wh; i < (size_t)r1 * Wh; ++i) {
                            bad_clean[i] = std::max(bad_clean[i], 0.85f * bad_closed[i]);
                        }
                    });
                }
                sep_gaussian_2d(bad_clean.data(), bad_blur.data(), Hh, Wh, is_astro ? 2.0f : 3.0f, n_threads);

                mw_half[n].resize((size_t)Hh * Wh);
                mw_align[n].resize((size_t)Hh * Wh);
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    float* __restrict mwa = mw_align[n].data();
                    float* __restrict mwh = mw_half[n].data();
                    const float* __restrict bb = bad_blur.data();
                    for (size_t i = (size_t)r0 * Wh; i < (size_t)r1 * Wh; ++i) {
                        float m = clamp_f(1.0f - 2.2f * bb[i], 0.0f, 1.0f);
                        mwa[i] = m;
                        mwh[i] = w_frame * m;
                    }
                });

                // 3-Gate Rigid Micro-Patch Subpixel Recovery on localized rigid moving subjects
                if (!is_astro && !gi_pres[n].empty()) {
                    recover_rigid_motion_patches(
                        g0_pre.data(), gi_pres[n].data(), g_halfs[n].data(),
                        ref_f.data(), ref_w.data(), grad_f.data(), grad_w.data(),
                        Hh, Wh, pg_sig0_sq, pg_gain, zf_lim, zw_lim, w_frame,
                        flows_fy[n], flows_fx[n], mw_half[n], n_threads, &mw_align[n], &frame_l0_sats[n], &wctx
                    );
                }
                frame_l0_sats[n].sat1.release();
                frame_l0_sats[n].sat2.release();
            }
        }
        align_l1_ws.sat1.release();
        align_l1_ws.sat2.release();
        gi_pres.clear(); gi_pres.shrink_to_fit();
        frame_l0_sats.clear(); frame_l0_sats.shrink_to_fit();

        // Single-pass 12-bucket temporal-difference broadband noise calibration
        std::array<std::vector<float>, 12> td_bins;
        std::array<double, 12> td_isums{};
        const int n_td_rows = (Hh > 16) ? ((Hh - 9 - 8) / 4 + 1) : 0;
        const int td_nt = std::max(1, std::min(n_threads, n_td_rows));
        if (td_nt <= 1) {
            for (int b = 0; b < NBINS; ++b) td_bins[b].reserve(((size_t)(Hh / 4) * (size_t)(Wh / 4)) / NBINS + 64);
            for (int r = 8; r + 8 < Hh; r += 4) {
                for (int c = 8; c + 8 < Wh; c += 4) {
                    size_t i = (size_t)r * Wh + c;
                    if (!(mw_align[1][i] > 0.85f && grad_f[i] < 0.015f)) continue;
                    float iv = ref_w[i];
                    bool have_td = false;
                    float td_val = 0.0f;
                    const double iv_pos = std::max(0.0f, iv);
                    for (int b = 0; b < NBINS; ++b) {
                        if (iv >= bin_edges[b] && iv <= bin_edges[b + 1] + 1e-6f) {
                            if (!have_td) {
                                float w1 = bilerp_clamped(g_halfs[1].data(), Hh, Wh, (float)r + flows_fy[1][i], (float)c + flows_fx[1][i]);
                                td_val = std::fabs(w1 - g_halfs[0][i]);
                                have_td = true;
                            }
                            td_bins[b].push_back(td_val);
                            td_isums[b] += iv_pos;
                        }
                    }
                }
            }
        } else {
            struct alignas(64) TdSlice {
                std::array<std::vector<float>, 12> bins;
                std::array<double, 12> sums{};
            };
            std::vector<TdSlice> slices(td_nt);
            const int rows_per_slice = (n_td_rows + td_nt - 1) / td_nt;
            parallel_for_rows(td_nt, td_nt, [&](int s0, int s1, int) {
                for (int si = s0; si < s1; ++si) {
                    auto& sl = slices[si];
                    const int ri0 = si * rows_per_slice;
                    const int ri1 = std::min(n_td_rows, ri0 + rows_per_slice);
                    if (ri0 >= ri1) continue;
                    for (int b = 0; b < NBINS; ++b) sl.bins[b].reserve((size_t)(ri1 - ri0) * (size_t)(Wh / 4) / NBINS + 32);
                    for (int ri = ri0; ri < ri1; ++ri) {
                        const int r = 8 + 4 * ri;
                        for (int c = 8; c + 8 < Wh; c += 4) {
                            size_t i = (size_t)r * Wh + c;
                            if (!(mw_align[1][i] > 0.85f && grad_f[i] < 0.015f)) continue;
                            float iv = ref_w[i];
                            bool have_td = false;
                            float td_val = 0.0f;
                            const double iv_pos = std::max(0.0f, iv);
                            for (int b = 0; b < NBINS; ++b) {
                                if (iv >= bin_edges[b] && iv <= bin_edges[b + 1] + 1e-6f) {
                                    if (!have_td) {
                                        float w1 = bilerp_clamped(g_halfs[1].data(), Hh, Wh, (float)r + flows_fy[1][i], (float)c + flows_fx[1][i]);
                                        td_val = std::fabs(w1 - g_halfs[0][i]);
                                        have_td = true;
                                    }
                                    sl.bins[b].push_back(td_val);
                                    sl.sums[b] += iv_pos;
                                }
                            }
                        }
                    }
                }
            }, 1);
            for (int b = 0; b < NBINS; ++b) {
                size_t tot = 0;
                for (int si = 0; si < td_nt; ++si) tot += slices[si].bins[b].size();
                td_bins[b].reserve(tot);
                for (int si = 0; si < td_nt; ++si) {
                    td_bins[b].insert(td_bins[b].end(), slices[si].bins[b].begin(), slices[si].bins[b].end());
                    td_isums[b] += slices[si].sums[b];
                }
            }
        }
        std::vector<double> t_A, t_y;
        for (int b = 0; b < NBINS; ++b) {
            if (td_bins[b].size() >= 64) {
                float s_td = quick_percentile(td_bins[b], 0.50f) / 0.67448975f;
                double mean_i = td_isums[b] / (double)td_bins[b].size();
                double v_1px = 1.25 * (double)s_td * (double)s_td;
                if (wctx.active) {
                    v_1px = std::max(1e-8, v_1px - wctx.eval_td_equiv_1px_qvar((float)mean_i));
                }
                t_A.push_back(1.0);
                t_A.push_back(mean_i);
                t_y.push_back(v_1px);
            }
        }
        std::vector<double> t_coef;
        if (t_y.size() >= 3 && solve_least_squares((int)t_y.size(), 2, t_A, t_y, t_coef)) {
            pg_sig0_sq = std::max(pg_sig0_sq, std::max(1e-8, t_coef[0]));
            pg_gain = std::max(pg_gain, std::max(1e-6, t_coef[1]));
        }

        // Build field_motion (R), field_phase (G), field_coherence, and wide 160-px anti-cutout field m_wide_half
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            auto round_parity = [](float x) -> int {
                const int q = (x >= 0.0f) ? (int)(x + 0.5f) : -(int)(-x + 0.5f);
                return q & 1;
            };
            for (int r = r0; r < r1; ++r) {
                int rm = clamp_i(r - 1, 0, Hh - 1), rp = clamp_i(r + 1, 0, Hh - 1);
                for (int c = 0; c < Wh; ++c) {
                    int cm = clamp_i(c - 1, 0, Wh - 1), cp = clamp_i(c + 1, 0, Wh - 1);
                    size_t i = (size_t)r * Wh + c;
                    float w_nb = 0.0f;
                    float q_occ[4] = {1.0f, 0.0f, 0.0f, 0.0f};
                    float gxs = wg_fine[0][(size_t)r * Wh + cp] - wg_fine[0][(size_t)r * Wh + cm];
                    float gys = wg_fine[0][(size_t)rp * Wh + c] - wg_fine[0][(size_t)rm * Wh + c];
                    for (int n = 1; n < N; ++n) {
                        float w = mw_half[n][i];
                        w_nb += w;
                        if (w > 0.1f) {
                            int qy = round_parity(flows_fy[n][i] * 4.0f);
                            int qx = round_parity(flows_fx[n][i] * 4.0f);
                            q_occ[qy * 2 + qx] = std::max(q_occ[qy * 2 + qx], w);
                            gxs += w * (wg_fine[n][(size_t)r * Wh + cp] - wg_fine[n][(size_t)r * Wh + cm]);
                            gys += w * (wg_fine[n][(size_t)rp * Wh + c] - wg_fine[n][(size_t)rm * Wh + c]);
                        }
                    }
                    float mot = clamp_f(1.0f - w_nb / std::max(1e-6f, w_frame_sum), 0.0f, 1.0f);
                    float quad_cov = 0.25f * (q_occ[0] + q_occ[1] + q_occ[2] + q_occ[3]);
                    float w_tot = 1.0f + w_nb;
                    float cnt_cov = clamp_f(w_tot / (float)N, 0.0f, 1.0f);
                    field_motion[i] = mot;
                    field_phase[i] = clamp_f(0.55f * quad_cov + 0.45f * cnt_cov, 0.0f, 1.0f);

                    // Cross-Frame Temporal Gradient Coherence: phase-consistent faint micro-textures accumulate
                    // coherently across frames while uncorrelated shot noise cancels as 1/w_tot!
                    float gx_bar = 0.5f * gxs / w_tot;
                    float gy_bar = 0.5f * gys / w_tot;
                    float coh_e2 = gx_bar * gx_bar + gy_bar * gy_bar;
                    float q_c2 = wctx.active
                        ? (0.32f / w_tot) * wctx.eval_half_qvar_fine(r, std::max(0.0f, ref_w[i]))
                        : 0.0f;
                    float n_g2 = (0.16f / w_tot) * ((float)pg_sig0_sq + (float)pg_gain * std::max(0.0f, ref_w[i])) + q_c2 + 1e-7f;
                    float t_multi = clamp_f((w_tot - 1.5f) / 2.0f, 0.0f, 1.0f);
                    field_coherence[i] = t_multi * clamp_f((coh_e2 - 1.05f * n_g2) / (coh_e2 + 2.0f * n_g2), 0.0f, 1.0f);
                }
            }
        });

        // Wide 160-2x-px smooth transition halo eliminating cutout / paste-on detachment boundaries
        const int Hq = std::max(8, Hh / 4), Wq = std::max(8, Wh / 4);
        std::vector<float> mq((size_t)Hq * Wq, 0.0f), mq_blur((size_t)Hq * Wq, 0.0f);
        parallel_for_rows(Hq, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wq; ++c) {
                    mq[(size_t)r * Wq + c] = field_motion[(size_t)std::min(Hh - 1, r * 4 + 2) * Wh + std::min(Wh - 1, c * 4 + 2)];
                }
            }
        });
        sep_gaussian_2d(mq.data(), mq_blur.data(), Hq, Wq, 10.0f, n_threads);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                float ys = ((float)r + 0.5f) * ((float)Hq / (float)Hh) - 0.5f;
                for (int c = 0; c < Wh; ++c) {
                    float xs = ((float)c + 0.5f) * ((float)Wq / (float)Wh) - 0.5f;
                    float halo = bilerp_clamped(mq_blur.data(), Hq, Wq, ys, xs);
                    size_t i = (size_t)r * Wh + c;
                    m_wide_half[i] = clamp_f(std::max(0.65f * field_motion[i], halo), 0.0f, 1.0f);
                }
            }
        });
    }

    cost_mark("alignment-motion-fields");
    if (shutter_kind == 1 && N > 1) {
        auto stats = heraw_core::row_rolling_shutter_smooth(flows_fy, flows_fx, mw_align, Hh, Wh, 0.5f, core_execution);
        log_err("[C10] Row-flow refinement RMS %.6f half-px.\n", stats.rms_update);
    }
    core_noise.read_variance = (float)pg_sig0_sq;
    core_noise.shot = (float)pg_gain;
    if (bracket_exposures) {
        for (int n = 0; n < N; ++n) {
            float inv_init = 1.0f / std::max(1e-6f, exposure_stats.gains[n]);
            if (std::fabs(inv_init - 1.0f) > 1e-6f) {
                for (float& v : frames_lin[n]) v *= inv_init;
                for (float& v : g_halfs[n]) v *= inv_init;
            }
        }
        core_noise.frame_read_variances = nullptr;
        core_noise.frame_shot_coefficients = nullptr;
        core_noise.frame_saturation_levels = nullptr;
        exposure_stats = heraw_core::exposure_normalize(frames_lin, g_halfs, H1, W1, core_noise,
                                                        flows_fy, flows_fx, mw_align, core_execution);
        core_noise.frame_read_variances = exposure_stats.read_variances.data();
        core_noise.frame_shot_coefficients = exposure_stats.shot_coefficients.data();
        core_noise.frame_saturation_levels = exposure_stats.saturation_levels.data();
        for (int n = 0; n < N; ++n)
            for (int y = 0; y < Hh; ++y)
                for (int x = 0; x < Wh; ++x) {
                    size_t i = (size_t)y*Wh+x;
                    float v = bilerp_clamped(g_halfs[n].data(), Hh, Wh, y+flows_fy[n][i], x+flows_fx[n][i]);
                    float best_variance = core_noise.variance(v);
                    float variance = core_noise.frame_variance(n, v);
                    float sat_w = core_noise.unsaturated(n, v) ? std::min(4.0f, best_variance/variance) : 0.0f;
                    mw_half[n][i] *= sat_w;
                    mw_align[n][i] = core_noise.unsaturated(n, v) ? mw_align[n][i] : 0.0f;
                }
    }
    cost_mark("row-flow-exposure-weights");
    bool gpu_auto_disabled = false;
    std::string gpu_auto_disabled_name;
#if !defined(__APPLE__)
    if (cfg.backend == heraw_gpu::BackendPref::AUTO && heraw_gpu::should_auto_disable_gpu(&gpu_auto_disabled_name)) {
        gpu_auto_disabled = true;
    }
#endif
    if (cfg.silicon_clean && N >= 3) {
        if(cfg.backend!=heraw_gpu::BackendPref::CPU && !gpu_auto_disabled && size_t(H1)*W1>=1024*1024)
            core_execution.sensor_residual=[&](const float* scene,heraw_core::Frames& residual) {
                heraw_gpu::SensorResidualJob job;
                job.H=H1;job.W=W1;job.raw=&frames_lin;job.fy=&flows_fy;job.fx=&flows_fx;job.mask=&mw_align;
                job.scene=scene;job.cfa=&cfa6x6[0][0];job.read_variance=core_noise.read_variance;
                job.shot=core_noise.shot;job.residual=&residual;
                return heraw_gpu::run_sensor_residual_gpu(job);
            };
        auto stats = heraw_core::silicon_residual_cleanup(frames_lin, H1, W1, &cfa6x6[0][0],
            flows_fy, flows_fx, mw_align, core_noise, nullptr, core_execution);
        if (stats.changed) {
            for (int n = 0; n < N; ++n)
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    if (!is_xtrans) {
                        for (int y = r0; y < r1; ++y) {
                            const float* row0 = frames_lin[n].data() + (size_t)(2 * y) * W1;
                            const float* row1 = row0 + W1;
                            float* dst = g_halfs[n].data() + (size_t)y * Wh;
                            for (int x = 0; x < Wh; ++x)
                                dst[x] = 0.5f * (row0[2 * x + 1] + row1[2 * x]);
                        }
                    } else {
                        for (int y = r0; y < r1; ++y) {
                            const int ym0 = (2 * y) % 6, ym1 = (2 * y + 1) % 6;
                            const float* row0 = frames_lin[n].data() + (size_t)(2 * y) * W1;
                            const float* row1 = row0 + W1;
                            float* dst = g_halfs[n].data() + (size_t)y * Wh;
                            int xm0 = 0, xm1 = 1;
                            for (int x = 0; x < Wh; ++x) {
                                float sum = 0.0f;
                                int count = 0;
                                if (cfa6x6[ym0][xm0] == 1) { sum += row0[2 * x]; ++count; }
                                if (cfa6x6[ym0][xm1] == 1) { sum += row0[2 * x + 1]; ++count; }
                                if (cfa6x6[ym1][xm0] == 1) { sum += row1[2 * x]; ++count; }
                                if (cfa6x6[ym1][xm1] == 1) { sum += row1[2 * x + 1]; ++count; }
                                dst[x] = sum / std::max(1, count);
                                xm0 = (xm0 + 2 >= 6) ? (xm0 - 4) : (xm0 + 2);
                                xm1 = (xm1 + 2 >= 6) ? (xm1 - 4) : (xm1 + 2);
                            }
                        }
                    }
                });
        }
        log_err("[C13] Sensor-coordinate correction: %zu samples, %zu updates, RMS %.7f.\n",
            stats.samples, stats.changed, stats.rms_update);
        release_free_heap_pages();
    }

    cost_mark("sensor-cleanup-green-rebuild");
    heraw_optics::OpticsReport optics;
    std::vector<float> frame_ois_shifts((size_t)N*4,0.0f);
    float lca_coeffs[4]={0,0,0,0};
    if (cfg.auto_lca || cfg.optics_only || !cfg.optics_report.empty() || !cfg.export_lcp.empty()) {
        heraw_optics::Input input{frames_lin,g_halfs,flows_fx,flows_fy,mw_align,&cfa6x6[0][0],H1,W1,frame_times};
        heraw_optics::Options opts;
        opts.camera=cam_make+" "+cam_model; opts.lens=lens_name;
        opts.focal_mm=focal_mm; opts.aperture=lens_aperture; opts.focus_m=focus_distance_m;
        opts.noise_sigma=std::sqrt(pg_sig0_sq); opts.ois=ois_active;
        opts.stars=is_astro; opts.far_field=!is_astro;
        optics=heraw_optics::analyze(input,opts);
        if (cfg.auto_lca) {
            const float norm=(float)std::max(H1,W1);
            for(int c=0;c<2;++c) {
                const auto& fit=optics.lca.channels[c];
                if(!fit.valid) continue;
                lca_coeffs[c*2]=(float)fit.coefficients[2]/norm;
                lca_coeffs[c*2+1]=(float)fit.coefficients[3]/(4.0f*norm);
                for(int n=0;n<N;++n) {
                    frame_ois_shifts[n*4+c*2]=(float)fit.coefficients[1];
                    frame_ois_shifts[n*4+c*2+1]=(float)fit.coefficients[0];
                    const auto& drift=optics.drift[n].lca_offset;
                    if(ois_active && drift[c*2].valid && drift[c*2+1].valid) {
                        frame_ois_shifts[n*4+c*2]+=(float)drift[c*2+1].value;
                        frame_ois_shifts[n*4+c*2+1]+=(float)drift[c*2].value;
                    }
                }
            }
        }
        if(cfg.optics_only || !cfg.optics_report.empty() || !cfg.export_lcp.empty()) {
            const std::string prefix=cfg.optics_report.empty() ?
                anchor_default_stem+"_optics" : cfg.optics_report;
            ensure_parent_dir_utf8(prefix+"_telemetry.json");
            heraw_suite::BurstView geometry_input{frames_lin,g_halfs,flows_fy,flows_fx,mw_align,H1,W1,
                cfa6x6,(float)pg_gain,(float)pg_sig0_sq};
            const auto geometry=heraw_suite::analyze_geometry(geometry_input);
            std::string telemetry=heraw_optics::report_json(optics);
            telemetry.erase(telemetry.find_last_of('}'));
            const bool has_focus_steps=focus_positions.size()==(size_t)N && N>=3 &&
                *std::max_element(focus_positions.begin(),focus_positions.end())-
                *std::min_element(focus_positions.begin(),focus_positions.end())>1e-6f;
            const bool dist_scene = (focus_distance_m > 50.0f) ||
                (focus_distance_m <= 0.0f && focal_mm >= 85.0f && (!geometry.gate.observable || geometry.disparity_span_px < 2.0f));
            telemetry+=",\"sigma0\":"+heraw_optics::detail::number(std::sqrt(pg_sig0_sq))+
                ",\"gain_g\":"+heraw_optics::detail::number(pg_gain)+
                ",\"sensor_temp_c\":"+(sensor_temp_c>=-80?heraw_optics::detail::number(sensor_temp_c):"null")+
                ",\"warm_pixels\":"+std::to_string(warm_pixel_count)+
                ",\"max_parallax_2x_px\":"+heraw_optics::detail::number(2.0*geometry.disparity_span_px)+
                ",\"depth_observable\":"+(geometry.gate.observable?"true":"false")+
                ",\"field_observability\":{\"depth\":"+(geometry.gate.observable?"true":"false")+
                ",\"defocus\":"+(has_focus_steps?"true":"false")+
                ",\"turbulence\":"+((N>=5&&focal_mm>=85.0f&&dist_scene)?"true":"false")+
                ",\"flicker\":"+((N>=4&&exposure_seconds>0.0f&&exposure_seconds<=1.0f/99.0f)?"true":"false")+
                ",\"coherence\":"+(N>=4?"true":"false")+",\"velocity\":"+(N>=3?"true":"false")+"}}\n";
            FILE* report=fopen_utf8_wb(prefix+"_telemetry.json");
            if(!report) {
                if (cfg.optics_only) return 1;
                log_err("[W8] Unable to write optical telemetry JSON: %s_telemetry.json\n", prefix.c_str());
            } else {
                bool written=std::fwrite(telemetry.data(),1,telemetry.size(),report)==telemetry.size();
                if((std::fclose(report)!=0 || !written) && cfg.optics_only) return 1;
            }
            try {
                heraw_optics::write_passport_png(prefix+"_passport.png",optics);
            } catch (const std::exception& ex) {
                if (cfg.optics_only) return 1;
                log_err("[W10] Optical passport warning: %s\n", ex.what());
            }
            if(!cfg.export_lcp.empty()) {
                const bool has_any_lca = optics.lca.channels[0].valid || optics.lca.channels[1].valid;
                if(!has_any_lca && !optics.distortion.valid && !optics.vignette.valid) {
                    log_err("[W9] Optical report saved; additional diverse edges are required for a correction profile.\n");
                    if(cfg.optics_only) return 3;
                } else {
                    ensure_parent_dir_utf8(cfg.export_lcp);
                    auto selected=optics;
                    const std::string fields=","+cfg.lcp_fields+",";
                    if(fields.find(",lca,")==std::string::npos)
                        for(auto& channel:selected.lca.channels) channel.valid=false;
                    if(fields.find(",vignette,")==std::string::npos) selected.vignette.valid=false;
                    if(fields.find(",distortion,")==std::string::npos) selected.distortion.valid=false;
                    const bool sel_any_lca = selected.lca.channels[0].valid || selected.lca.channels[1].valid;
                    if(!sel_any_lca && !selected.vignette.valid && !selected.distortion.valid) {
                        log_err("[W9] Selected correction models require additional calibration observations.\n");
                        if(cfg.optics_only) return 3;
                    } else {
                        try {
                            heraw_optics::write_lcp(cfg.export_lcp,selected);
                        } catch (const std::exception& ex) {
                            if (cfg.optics_only) return 1;
                            log_err("[W9] LCP write warning: %s\n", ex.what());
                        }
                    }
                }
            }
            log_err("[W8/W10] Optical telemetry and passport: %s\n",prefix.c_str());
        }
        if(cfg.optics_only) return 0;
    }

    cost_mark("optics-lca");
    heraw_suite::BurstView suite_view{frames_lin, g_halfs, flows_fy, flows_fx, mw_align,
        H1, W1, cfa6x6, (float)pg_gain, (float)pg_sig0_sq};
    suite_view.times_seconds = &frame_times;
    suite_view.capture_order = &capture_order;
    if(focus_positions.size()==(size_t)N) suite_view.focus_positions=&focus_positions;
    suite_view.exposure_seconds = exposure_seconds;
    suite_view.focal_length_mm = focal_mm;
    suite_view.distant_scene = (focus_distance_m > 50.0f) || (focus_distance_m <= 0.0f && focal_mm >= 85.0f);
    std::vector<std::string> wanted_fields;
    if (!out_field_path.empty()) {
        size_t begin=0;
        do {
            size_t end=cfg.fields_csv.find(',', begin);
            std::string name=cfg.fields_csv.substr(begin,end-begin);
            if (name!="motion" && name!="phase" && name!="focus") wanted_fields.push_back(name);
            if (end==std::string::npos) break;
            begin=end+1;
        } while (true);
    }
    if ((cam_flip == 3 || cam_flip == 5 || cam_flip == 6) &&
        (std::find(wanted_fields.begin(), wanted_fields.end(), "normal_x") != wanted_fields.end() ||
         std::find(wanted_fields.begin(), wanted_fields.end(), "normal_y") != wanted_fields.end())) {
        if (std::find(wanted_fields.begin(), wanted_fields.end(), "normal_x") == wanted_fields.end())
            wanted_fields.push_back("normal_x");
        if (std::find(wanted_fields.begin(), wanted_fields.end(), "normal_y") == wanted_fields.end())
            wanted_fields.push_back("normal_y");
    }
    if (cfg.export_refocus || cfg.export_parallax_html || cfg.reflection_mode=="split-layers")
        if (std::find(wanted_fields.begin(),wanted_fields.end(),"depth")==wanted_fields.end())
            wanted_fields.push_back("depth");
    if (cfg.reflection_mode!="off" && std::find(wanted_fields.begin(),wanted_fields.end(),"specular")==wanted_fields.end())
        wanted_fields.push_back("specular");
    auto suite_fields = heraw_suite::compute_fields(suite_view, wanted_fields, core_execution.parallel);
    auto temporal_view=suite_view;
    std::vector<std::pair<std::string,heraw_suite::Image>> extra_images;
    std::vector<float> temporal_anchor;
    heraw_suite::TemporalResult primary_temporal;
    heraw_suite::Image temporal_composite;
    heraw_suite::Image reflection_composite;
    bool aux_extra_skipped = false;
    const bool temporal_primary = cfg.motion_mode!="standard";
    if (temporal_primary) {
        heraw_suite::TemporalOptions opts;
        opts.mode = cfg.motion_mode=="clean-plate" ? heraw_suite::TemporalMode::CleanPlate :
            (cfg.motion_mode=="virtual-nd" ? heraw_suite::TemporalMode::VirtualND : heraw_suite::TemporalMode::Strobe);
        opts.reject_sigma = 3.5f/std::max(0.1f,cfg.clean_sens);
        opts.strobe_stride = (size_t)cfg.strobe_step;
        opts.strobe_opacity = 1.0f;
        opts.strobe_fade = cfg.strobe_fade;
        primary_temporal = heraw_suite::prepare_temporal(temporal_view, opts);
        log_err("[W] %s: %s (%g %s).\n", cfg.motion_mode.c_str(),
            primary_temporal.gate.reason.c_str(),primary_temporal.gate.measured,primary_temporal.gate.units.c_str());
        if (!primary_temporal.gate.observable) return 3;
    }
    if (cfg.emit_transient) {
        heraw_suite::TemporalOptions opts;
        opts.mode=heraw_suite::TemporalMode::Transient;
        opts.reject_sigma=cfg.transient_sigma;
        auto selection=heraw_suite::prepare_temporal(suite_view,opts);
        if (selection.gate.observable) {
            auto render=heraw_suite::render_temporal(suite_view,selection,H1,W1,core_execution.parallel);
            extra_images.emplace_back("transient",std::move(render.layer));
        } else {
            log_err("[W3] %s.\n",selection.gate.reason.c_str());
            aux_extra_skipped = true;
        }
    }
    if (cfg.reflection_mode!="off") {
        float max_cam_drift_1x = 0.0f;
        for (int n = 1; n < N; ++n) {
            for (int r = Hh / 8; r < 7 * Hh / 8; r += 16) {
                for (int c = Wh / 8; c < 7 * Wh / 8; c += 16) {
                    size_t hi = (size_t)r * Wh + c;
                    if (mw_align[n][hi] > 0.5f) {
                        max_cam_drift_1x = std::max(max_cam_drift_1x, 2.0f * std::hypot(flows_fx[n][hi], flows_fy[n][hi]));
                    }
                }
            }
        }
        heraw_suite::TemporalOptions opts;
        opts.mode=cfg.reflection_mode=="split-layers" ? heraw_suite::TemporalMode::Reflection : heraw_suite::TemporalMode::Glint;
        opts.reflection_disparity_px=std::max(suite_fields.geometry.disparity_span_px,
            max_cam_drift_1x >= 20.0f ? 0.5f * max_cam_drift_1x : 0.0f);
        auto selection=heraw_suite::prepare_temporal(suite_view,opts);
        if (selection.gate.observable) {
            auto render=heraw_suite::render_temporal(suite_view,selection,H1,W1,core_execution.parallel);
            reflection_composite=std::move(render.image);
            if (cfg.reflection_mode=="split-layers") extra_images.emplace_back("reflection",std::move(render.layer));
        } else {
            log_err("[W5] %s (%g %s).\n",selection.gate.reason.c_str(),
                selection.gate.measured,selection.gate.units.c_str());
            aux_extra_skipped = true;
        }
    }
    if (temporal_primary) {
        if (cfg.motion_mode=="clean-plate" || cfg.motion_mode=="strobe" || cfg.motion_mode=="virtual-nd") {
            auto render=heraw_suite::render_temporal(temporal_view,primary_temporal,H1,W1,core_execution.parallel);
            if (cfg.motion_mode == "clean-plate" || cfg.motion_mode == "strobe") {
                temporal_anchor.resize((size_t)H1*W1);
                for (int y=0;y<H1;++y) for (int x=0;x<W1;++x)
                    temporal_anchor[(size_t)y*W1+x]=render.image.rgb[((size_t)y*W1+x)*3+cfa6x6[y%6][x%6]];
            }
            if (cfg.motion_mode!="clean-plate") temporal_composite=std::move(render.image);
        }
        heraw_suite::materialize_camera_flows(primary_temporal,flows_fy,flows_fx);
        if (cfg.motion_mode=="virtual-nd") {
            for (int n=0;n<N;++n) for (size_t i=0;i<mw_half[n].size();++i) {
                float a=clamp_f((field_motion[i]-cfg.vnd_thresh)*8.0f,0.0f,1.0f);
                primary_temporal.weights[n][i]=mw_half[n][i]*(1-a)+primary_temporal.weights[n][i]*a;
            }
        }
        mw_half=std::move(primary_temporal.weights);
    }
    mw_align.clear(); mw_align.shrink_to_fit();

    // Build Hamilton-Adams 2nd-order Laplacian directional Green (using multi-frame denoised Bayer on zero-shift static pixels!)
    cost_mark("optional-fields-temporal");
    std::vector<float> g0_ha_1x((size_t)H1 * W1);
    if (is_xtrans) {
        parallel_for_rows(H1, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r)
                for (int c = 0; c < W1; ++c)
                    g0_ha_1x[(size_t)r * W1 + c] =
                        sample_cfa_channel(frames_lin[0].data(), H1, W1, r, c, 1, cfa6x6);
        });
    } else if (N >= 2) {
        std::vector<float> bayer_clean((size_t)H1 * W1);
        parallel_for_rows(H1, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                int rh = clamp_i(r >> 1, 0, Hh - 1);
                for (int c = 0; c < W1; ++c) {
                    int ch = clamp_i(c >> 1, 0, Wh - 1);
                    size_t idx = (size_t)r * W1 + c, hi = (size_t)rh * Wh + ch;
                    float vs = frames_lin[0][idx], ws = 1.0f;
                    for (int n = 1; n < N; ++n) {
                        float w = mw_half[n][hi];
                        if (w > 0.65f && std::fabs(flows_fy[n][hi]) < 0.14f && std::fabs(flows_fx[n][hi]) < 0.14f) {
                            vs += w * frames_lin[n][idx];
                            ws += w;
                        }
                    }
                    bayer_clean[idx] = vs / ws;
                }
            }
        });
        build_hamilton_adams_green_1x(bayer_clean.data(), g0_ha_1x.data(), H1, W1, n_threads);
    } else {
        build_hamilton_adams_green_1x(frames_lin[0].data(), g0_ha_1x.data(), H1, W1, n_threads);
    }
    if (!temporal_anchor.empty()) {
        parallel_for_rows(H1,n_threads,[&](int begin,int end,int) {
            for(int y=begin;y<end;++y) for(int x=0;x<W1;++x)
                g0_ha_1x[(size_t)y*W1+x]=sample_cfa_channel(temporal_anchor.data(),H1,W1,y,x,1,cfa6x6);
        });
    }

    {
        char msg3[240];
        double dt3 = elapsed_s(t0);
        if (is_astro && astro_celestial_locked > 0) {
            std::snprintf(msg3, sizeof(msg3),
                "[3/6] Astro star-centroid (%zu stars, %d/%d locked) + horizon alignment done in %.2fs",
                astro_stars0.size(), astro_celestial_locked, N - 1, dt3);
        } else {
            std::snprintf(msg3, sizeof(msg3),
                "[3/6] Subpixel alignment + rigid micro-patch SR + coherence field done in %.2fs (sig0=%.5f, gain=%.5f)",
                dt3, (float)std::sqrt(pg_sig0_sq), (float)pg_gain);
        }
        emit_stage(3, 42, dt3, msg3);
    }

    // --- Stage 4: Anisotropic Quincunx Green + Residual-Guided R/B Burst Reconstruction ---
    t0 = std::chrono::steady_clock::now();
    // Build motion-weighted temporal-mean Green guide for structure tensor
    cost_mark("anchor-green");
    std::vector<float> g_guide((size_t)Hh * Wh, 0.0f);
    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < Wh; ++c) {
                size_t i = (size_t)r * Wh + c;
                float w0 = mw_half[0][i];
                float acc = w0 * g_halfs[0][i], wsum = w0;
                for (int n = 1; n < N; ++n) {
                    float w = mw_half[n][i];
                    if (w > 0.05f) {
                        acc += w * bilerp_clamped(g_halfs[n].data(), Hh, Wh, (float)r + flows_fy[n][i], (float)c + flows_fx[n][i]);
                        wsum += w;
                    }
                }
                g_guide[i] = (wsum > 1e-6f) ? (acc / wsum) : g_halfs[0][i];
            }
        }
    });

    std::vector<float> g_guide_s((size_t)Hh * Wh);
    if (temporal_primary) g_guide = primary_temporal.guide;
    sep_gaussian_2d(g_guide.data(), g_guide_s.data(), Hh, Wh, 0.55f, n_threads);
    std::vector<float> jxx((size_t)Hh * Wh), jxy((size_t)Hh * Wh), jyy((size_t)Hh * Wh);
    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            int rm = clamp_i(r - 1, 0, Hh - 1), rp = clamp_i(r + 1, 0, Hh - 1);
            for (int c = 0; c < Wh; ++c) {
                int cm = clamp_i(c - 1, 0, Wh - 1), cp = clamp_i(c + 1, 0, Wh - 1);
                float gx = 0.5f * (g_guide_s[(size_t)r * Wh + cp] - g_guide_s[(size_t)r * Wh + cm]);
                float gy = 0.5f * (g_guide_s[(size_t)rp * Wh + c] - g_guide_s[(size_t)rm * Wh + c]);
                size_t i = (size_t)r * Wh + c;
                jxx[i] = gx * gx; jxy[i] = gx * gy; jyy[i] = gy * gy;
            }
        }
    });
    sep_gaussian_2d(jxx.data(), jxx.data(), Hh, Wh, 1.0f, n_threads);
    sep_gaussian_2d(jxy.data(), jxy.data(), Hh, Wh, 1.0f, n_threads);
    sep_gaussian_2d(jyy.data(), jyy.data(), Hh, Wh, 1.0f, n_threads);

    std::vector<float> inv_cov((size_t)Hh * Wh * 3);
    std::vector<float> focus_raw((size_t)Hh * Wh, 0.0f);
    std::vector<float> edge_pres_half((size_t)Hh * Wh, 0.0f);
    const float iso_rad = clamp_f(0.39f + 1.0f * sig0_1px / std::sqrt((float)N), 0.38f, 0.46f);
    const float* __restrict g0_raw_half = g_halfs[0].data();
    parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            int rm = clamp_i(r - 1, 0, Hh - 1), rp = clamp_i(r + 1, 0, Hh - 1);
            for (int c = 0; c < Wh; ++c) {
                int cm = clamp_i(c - 1, 0, Wh - 1), cp = clamp_i(c + 1, 0, Wh - 1);
                size_t i = (size_t)r * Wh + c;
                float tr = jxx[i] + jyy[i];
                float det = std::sqrt(std::max(0.0f, (jxx[i] - jyy[i]) * (jxx[i] - jyy[i]) + 4.0f * jxy[i] * jxy[i]));
                float lam1 = 0.5f * (tr + det);
                float lam2 = 0.5f * (tr - det);
                float iv = std::max(0.0f, g_guide[i]);
                float n_var_1f = 0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv)
                               + (wctx.active ? wctx.eval_half_qvar(r, iv) : 0.0f);
                float n_var = n_var_1f / (float)N;
                float edge_s = clamp_f((lam1 - 1.3f * n_var) / (5.0f * n_var + 1e-6f), 0.0f, 1.0f);
                float anis = clamp_f((lam1 - lam2) / (lam1 + lam2 + n_var + 1e-7f), 0.0f, 1.0f) * edge_s;

                float gx0 = 0.5f * (g0_pre[(size_t)r * Wh + cp] - g0_pre[(size_t)r * Wh + cm]);
                float gy0 = 0.5f * (g0_pre[(size_t)rp * Wh + c] - g0_pre[(size_t)rm * Wh + c]);
                float e0_sq = gx0 * gx0 + gy0 * gy0;
                edge_pres_half[i] = clamp_f((e0_sq - 1.5f * n_var_1f) / (8.0f * n_var_1f + 1e-6f), 0.0f, 1.0f);

                float lap0 = 4.0f * g0_raw_half[i]
                           - g0_raw_half[(size_t)rm * Wh + c] - g0_raw_half[(size_t)rp * Wh + c]
                           - g0_raw_half[(size_t)r * Wh + cm] - g0_raw_half[(size_t)r * Wh + cp];
                focus_raw[i] = lap0 * lap0;

                float ca2 = 1.0f, sa2 = 0.0f, casa = 0.0f;
                if (det > 1e-18f) {
                    const float inv_det = 1.0f / det;
                    ca2 = clamp_f(0.5f * (1.0f + (jxx[i] - jyy[i]) * inv_det), 0.0f, 1.0f);
                    sa2 = 1.0f - ca2;
                    casa = jxy[i] * inv_det;
                }
                float s_perp = iso_rad * (1.0f - anis) + 0.28f * anis;
                float s_para = iso_rad * (1.0f - anis) + 0.64f * anis;
                float ip = 1.0f / (s_perp * s_perp);
                float iq = 1.0f / (s_para * s_para);

                const float delta = ip - iq;
                inv_cov[i * 3 + 0] = iq + delta * ca2;
                inv_cov[i * 3 + 1] = delta * casa;
                inv_cov[i * 3 + 2] = iq + delta * sa2;
            }
        }
    });
    g0_pre.clear(); g0_pre.shrink_to_fit();
    g_guide_s.clear(); g_guide_s.shrink_to_fit();
    jxx.clear(); jxx.shrink_to_fit();
    jxy.clear(); jxy.shrink_to_fit();
    jyy.clear(); jyy.shrink_to_fit();
    {
        const int Hq = std::max(8, Hh / 4), Wq = std::max(8, Wh / 4);
        std::vector<float> fq0((size_t)Hq * Wq, 0.0f), fq_dil((size_t)Hq * Wq, 0.0f);
        parallel_for_rows(Hq, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                int sy0 = r * 4, sy1 = std::min(Hh, sy0 + 4);
                for (int c = 0; c < Wq; ++c) {
                    int sx0 = c * 4, sx1 = std::min(Wh, sx0 + 4);
                    float s_l2 = 0.0f, s_iv = 0.0f;
                    int cnt = 0;
                    for (int yy = sy0; yy < sy1; ++yy) {
                        for (int xx = sx0; xx < sx1; ++xx) {
                            size_t hi = (size_t)yy * Wh + xx;
                            s_l2 += focus_raw[hi];
                            s_iv += std::max(0.0f, g_guide[hi]);
                            ++cnt;
                        }
                    }
                    float inv_c = (cnt > 0) ? (1.0f / (float)cnt) : 0.0f;
                    float mean_l2 = s_l2 * inv_c;
                    float mean_iv = s_iv * inv_c;
                    float n_var_1f = 0.5f * ((float)pg_sig0_sq + (float)pg_gain * mean_iv)
                                   + (wctx.active ? (1.0f + 0.5f * wctx.luma_scale_psd[5]) * wctx.eval_half_qvar(std::min(Hh - 1, sy0), mean_iv) : 0.0f);
                    float hf_sig = std::max(0.0f, mean_l2 - 20.0f * n_var_1f);
                    fq0[(size_t)r * Wq + c] = clamp_f(hf_sig / (hf_sig + 16.0f * n_var_1f + 1.0e-6f), 0.0f, 1.0f);
                }
            }
        });
        parallel_for_rows(Hq, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wq; ++c) {
                    float mv = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        int rr = clamp_i(r + dy, 0, Hq - 1);
                        for (int dx = -1; dx <= 1; ++dx)
                            mv = std::max(mv, fq0[(size_t)rr * Wq + clamp_i(c + dx, 0, Wq - 1)]);
                    }
                    fq_dil[(size_t)r * Wq + c] = mv;
                }
            }
        });
        sep_gaussian_2d(fq_dil.data(), fq0.data(), Hq, Wq, 2.2f, n_threads);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                float ys = ((float)r + 0.5f) * ((float)Hq / (float)Hh) - 0.5f;
                for (int c = 0; c < Wh; ++c) {
                    float xs = ((float)c + 0.5f) * ((float)Wq / (float)Wh) - 0.5f;
                    field_focus[(size_t)r * Wh + c] = clamp_f(bilerp_clamped(fq0.data(), Hq, Wq, ys, xs), 0.0f, 1.0f);
                }
            }
        });
    }
    focus_raw.clear(); focus_raw.shrink_to_fit();
    cost_mark("guide-covariance-focus");
    if (N >= 2 && cfg.motion_mode == "standard") {
        heraw_core::Frames lucky;
        std::vector<uint8_t> directions((size_t)Hh*Wh);
        parallel_for_rows(Hh,n_threads,[&](int begin,int end,int) {
            for(size_t i=(size_t)begin*Wh;i<(size_t)end*Wh;++i) {
                const float dy = 2.0f * inv_cov[3 * i + 1];
                const float dx = inv_cov[3 * i] - inv_cov[3 * i + 2];
                const float ax = std::fabs(dx), ay = std::fabs(dy);
                if (std::fabs(ay - ax) <= 1e-5f * (ax + ay + 1e-20f)) {
                    const float angle = 0.5f * std::atan2(dy, dx);
                    directions[i] = (uint8_t)(((int)std::lround(angle * (4.0f / 3.14159265359f)) + 4) % 4);
                } else if (ay > ax) {
                    directions[i] = (dy >= 0.0f) ? 1u : 3u;
                } else {
                    directions[i] = (dx >= 0.0f) ? 0u : 2u;
                }
            }
        });
        heraw_core::directional_lucky_weights(g_halfs,Hh,Wh,flows_fy,flows_fx,core_noise,lucky,core_execution,directions.data());
        for (int n = 0; n < N; ++n) {
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                float* __restrict mwh = mw_half[n].data();
                const float* __restrict lk = lucky[n].data();
                for (size_t i = (size_t)r0 * Wh; i < (size_t)r1 * Wh; ++i) mwh[i] *= lk[i];
            });
        }
    }
    g_halfs.clear(); g_halfs.shrink_to_fit();

    cost_mark("directional-lucky");
    const bool need_cpu_rl_mod = (cfg.relay_raw && requested_cfg.psf_iters > 0) || cfg.rgb_psf_iters > 0;
    std::vector<float> snr_boost_half((size_t)Hh * Wh, 1.0f);
    std::vector<float> rl_gain_half(need_cpu_rl_mod ? (size_t)Hh * Wh : 0, 1.0f);
    std::vector<float> rl_thrmul_half(need_cpu_rl_mod ? (size_t)Hh * Wh : 0, 1.0f);
    std::vector<uint32_t> rl_mod_f16((size_t)Hh * Wh, 0);
    {
        std::vector<float> raw_boost((size_t)Hh * Wh, 1.0f);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wh; ++c) {
                    size_t i = (size_t)r * Wh + c;
                    float mw_w = m_wide_half[i];
                    float ep = edge_pres_half[i];
                    float coh_exempt = 1.0f - 0.52f * field_coherence[i];
                    raw_boost[i] = coh_exempt * (1.0f + 0.38f * mw_w * (1.0f - 0.85f * ep)) * wctx.precinct_boost_half(r);

                    float foc = field_focus[i];
                    float foc_t = clamp_f((foc - 0.04f) / 0.36f, 0.0f, 1.0f);
                    float foc_smooth = foc_t * foc_t * (3.0f - 2.0f * foc_t);
                    float tripod_bonus = (N >= 4 && field_motion[i] < 0.05f && field_phase[i] < 0.58f) ? 1.18f : 1.0f;
                    float rlg = tripod_bonus * (0.18f + 0.82f * foc_smooth) * (1.0f - 0.14f * mw_w);
                    float thm = 1.0f + 1.8f * field_motion[i];
                    if (need_cpu_rl_mod) {
                        rl_gain_half[i] = rlg;
                        rl_thrmul_half[i] = thm;
                    }
                    rl_mod_f16[i] = heraw_gpu::pack_half2_u32(rlg, thm);
                }
            }
        });
        sep_gaussian_2d(raw_boost.data(), snr_boost_half.data(), Hh, Wh, 2.0f, n_threads);
    }
    edge_pres_half.clear(); edge_pres_half.shrink_to_fit();
    field_coherence.clear(); field_coherence.shrink_to_fit();
    m_wide_half.clear(); m_wide_half.shrink_to_fit();
    release_free_heap_pages();

    const int H2 = H1 * 2;
    const int W2 = W1 * 2;
    const auto depth_gate_it = suite_fields.gates.find("depth");
    const bool depth_is_observable = (depth_gate_it != suite_fields.gates.end() && depth_gate_it->second.observable);
    bool depth_extra_skipped = false;
    if (cfg.export_refocus && !depth_is_observable) {
        log_err("[W7] Refocus skipped: %s.\n",
            depth_gate_it != suite_fields.gates.end() ? depth_gate_it->second.reason.c_str() : "relative parallax not observable");
        depth_extra_skipped = true;
    }
    if (cfg.export_parallax_html && !depth_is_observable) {
        log_err("[W6] Parallax HTML skipped: %s.\n",
            depth_gate_it != suite_fields.gates.end() ? depth_gate_it->second.reason.c_str() : "relative parallax not observable");
        depth_extra_skipped = true;
    }
    const bool do_refocus = cfg.export_refocus && depth_is_observable;
    const bool do_parallax_html = cfg.export_parallax_html && depth_is_observable;
    const bool pure_1x_pref = (cfg.rgb_scale_mode == 1) && out_raw_path.empty() && out_rgb_path.empty();
    const bool need_2x = !out_raw_path.empty() || !out_rgb_path.empty() || ((do_refocus || do_parallax_html) && !pure_1x_pref && out_rgb_1x_path.empty());
    const bool need_1x = !out_rgb_1x_path.empty() || ((do_refocus || do_parallax_html) && !need_2x);
    const bool reconstruct_any = need_2x || need_1x;

    bool is_phase_balanced = false;
    if (need_2x && !cfg.relay_raw && cfg.denoise_strength > 0 && N >= 6) {
        auto phase_count = [](unsigned m) { int cnt = 0; for (; m; m >>= 1) cnt += (m & 1); return cnt; };
        int balanced_pts = 0;
        for (int gy = 1; gy <= 3; ++gy) {
            for (int gx = 1; gx <= 3; ++gx) {
                size_t sp_i = (size_t)clamp_i((Hh * gy) / 4, 0, Hh - 1) * Wh + clamp_i((Wh * gx) / 4, 0, Wh - 1);
                unsigned ph[2] = {0, 0};
                for (int n = 0; n < N; ++n) {
                    int qx = ((int)std::lround(4 * flows_fx[n][sp_i])) & 1;
                    int qy = ((int)std::lround(4 * flows_fy[n][sp_i])) & 1;
                    ph[n & 1] |= 1u << (2 * qy + qx);
                }
                if (phase_count(ph[0]) >= 3 && phase_count(ph[1]) >= 3) ++balanced_pts;
            }
        }
        is_phase_balanced = (balanced_pts >= 5);
    }
    const bool need_sources_post_s45 = !cfg.dataset_dir.empty() ||
        (reconstruct_any && cfg.ibp_iters > 0) ||
        (need_2x && !cfg.relay_raw && cfg.denoise_strength > 0 && is_phase_balanced);

    UninitBuf<float> rgb_2x(need_2x ? (size_t)H2 * (size_t)W2 * 3 : 0);
    UninitBuf<float> rgb_1x((need_1x && !need_2x) ? (size_t)H1 * (size_t)W1 * 3 : 0);
    std::vector<float> fusion_eta((reconstruct_any && N >= (need_2x ? 5 : 3)) ? size_t((H1 + 3) / 4) * ((W1 + 3) / 4) : 0, -1.0f);
    RlWorkspace rl_ws;
    double eta_eff = 0.25, g_eff = 0.0, sig_eff_sq = 1e-10;
    float resid_noise_2x = 0.0f;
    int compute_tiles = 0;
    size_t compute_peak_pixels = 0;

    if (reconstruct_any) {
        cost_mark("modulation-canvas");
        heraw_gpu::Stage45GpuJob gjob;
        gjob.H1 = H1; gjob.W1 = W1; gjob.N = N; gjob.n_threads = n_threads;
        gjob.is_clean_plate = (cfg.motion_mode == "clean-plate" || cfg.motion_mode == "strobe");
        gjob.is_virtual_nd = false;
        gjob.is_astro = is_astro;
        gjob.is_xtrans = is_xtrans;
        std::memcpy(gjob.cfa6x6, cfa6x6, sizeof(cfa6x6));
        gjob.keep_frames_lin = need_sources_post_s45;
        gjob.olpf_mode = olpf_mode;
        gjob.lca_r_k1 = lca_coeffs[0]; gjob.lca_r_k2 = lca_coeffs[1];
        gjob.lca_b_k1 = lca_coeffs[2]; gjob.lca_b_k2 = lca_coeffs[3];
        gjob.frame_ois_shifts = &frame_ois_shifts;
        gjob.frames_lin = &frames_lin;
        gjob.flows_fy = &flows_fy;
        gjob.flows_fx = &flows_fx;
        gjob.mw_half = &mw_half;
        gjob.inv_cov = inv_cov.data();
        gjob.g_guide = g_guide.data();
        gjob.c_anc_1x = temporal_anchor.empty() ? frames_lin[0].data() : temporal_anchor.data();
        gjob.g0_ha_1x = g0_ha_1x.data();
        gjob.snr_boost_half = snr_boost_half.data();
        gjob.rl_mod_f16 = rl_mod_f16.data();
        gjob.pg_sig0_sq = pg_sig0_sq;
        gjob.pg_gain = pg_gain;
        if (wctx.active) {
            double fm_sum = 0.0;
            size_t fm_cnt = 0;
            for (size_t i = 0; i < field_motion.size(); i += 16) {
                fm_sum += field_motion[i];
                ++fm_cnt;
            }
            float mean_mot = (fm_cnt > 0) ? float(fm_sum / double(fm_cnt)) : 0.0f;
            wctx.fill_gpu_job_wavelet_params(gjob, pg_sig0_sq, pg_gain, mean_mot);
        }
        gjob.wb_r = cam_wb[0];
        gjob.wb_b = cam_wb[2];
        gjob.denoise_strength = cfg.denoise_strength;
        gjob.chroma_denoise = cfg.chroma_denoise;
        gjob.psf_iters = cfg.psf_iters;
        gjob.psf_sigma = cfg.psf_sigma;
        gjob.edge_polish = cfg.edge_polish;
        gjob.glint_desat = cfg.glint_desat;
        gjob.rgb_2x_out = need_2x ? rgb_2x.data() : rgb_1x.data();
        gjob.eta_quarter_out = fusion_eta.empty() ? nullptr : fusion_eta.data();

        const auto t_stage45 = std::chrono::steady_clock::now();
        double stage4_seconds = 0.0;
        gjob.on_stage4_done = [&](double eta_val, double dt4, const std::string& dev_lbl) {
            stage4_seconds = dt4;
            char msg4[240];
            if (need_2x) {
                std::snprintf(msg4, sizeof(msg4),
                    "[4/6] Anisotropic quincunx + residual 2x fusion (%dx%d) [%s] done in %.2fs (eta=%.4f)",
                    W2, H2, dev_lbl.c_str(), dt4, eta_val);
            } else {
                std::snprintf(msg4, sizeof(msg4),
                    "[4/6] Anisotropic multi-frame 1x true-RGB fusion (%dx%d) [%s] done in %.2fs (eta=%.4f)",
                    W1, H1, dev_lbl.c_str(), dt4, eta_val);
            }
            emit_stage(4, 65, dt4, msg4);
        };

        if (need_2x) {
            bool stage45_done = false;
            if (gpu_auto_disabled) {
                if (group_idx_1based == 1) {
                    log_err("[Backend] Integrated GPU (%s) default-off -> using CPU SIMD (pass --backend gpu to force)\n",
                        gpu_auto_disabled_name.c_str());
                }
            } else if (cfg.backend != heraw_gpu::BackendPref::CPU) {
                stage45_done = heraw_gpu::run_stage45_gpu(gjob);
                if (!stage45_done) {
                    if (cfg.backend == heraw_gpu::BackendPref::GPU) {
                        log_err("Error: GPU backend failed: %s\n", gjob.out_err.c_str());
                        return 1;
                    }
                    log_err("[GPU unavailable (%s) -> falling back to CPU SIMD]\n", gjob.out_err.c_str());
                }
            }
            if (!stage45_done) {
                if ((size_t)H1 * (size_t)W1 <= 80000000ULL) gjob.tile_size_1x = 0;
                if (!heraw_cpu::run_stage45_cpu(gjob, &rl_ws)) {
                    log_err("Error: CPU backend failed: %s\n", gjob.out_err.c_str());
                    return 1;
                }
            }
        } else {
            if (!heraw_cpu::run_stage45_1x_cpu(gjob, &rl_ws)) {
                log_err("Error: 1x CPU backend failed: %s\n", gjob.out_err.c_str());
                return 1;
            }
        }
        eta_eff = gjob.out_eta_eff;
        if (wctx.active && gjob.global_mean_g < 0.0) {
            double gm = 0.0;
            const size_t n1x = size_t(H1) * size_t(W1);
            for (size_t i = 0; i < n1x; i += 16) gm += std::max(0.0f, g0_ha_1x[i]);
            gjob.global_mean_g = gm / double((n1x + 15) / 16);
        }
        heraw_gpu::gpu_stage5_gat_params(gjob, eta_eff, sig_eff_sq, g_eff);
        resid_noise_2x = gjob.out_resid_noise_2x;
        compute_tiles = gjob.out_tile_count;
        compute_peak_pixels = gjob.out_peak_tile_pixels;
        if (!gjob.out_tile_diagnostics.empty()) log_err("[Tiles] %s\n", gjob.out_tile_diagnostics.c_str());
        const char* cost_names[] = {"setup", "green", "chroma-fusion", "GAT-DCT", "chroma-filter", "RL", "readback"};
        for (size_t i = 0; i < gjob.out_wall_seconds.size(); ++i) {
            log_err("[BackendCost] %s wall=%.6f", cost_names[i], gjob.out_wall_seconds[i]);
            if (gjob.out_has_device_timing) log_err(" device=%.6f", gjob.out_device_seconds[i]);
            log_err(" s\n");
        }
        log_err("[BackendCost] plan=%.6f crop=%.6f stitch=%.6f s\n",
            gjob.out_plan_seconds, gjob.out_crop_seconds, gjob.out_stitch_seconds);
        char msg5[240];
        double dt5 = std::max(0.0, elapsed_s(t_stage45) - stage4_seconds);
        std::snprintf(msg5, sizeof(msg5),
            "[5/6] GAT-DCT Wiener (%.2fx) + A-Trous chroma + PSF RL (%d iters) [%s] done in %.2fs",
            cfg.denoise_strength, cfg.psf_iters, gjob.out_device_label.c_str(), dt5);
        emit_stage(5, 88, dt5, msg5);
    }

    cost_mark("backend-stage45");
    auto release_burst_sources = [&]() {
        frames_lin.clear(); frames_lin.shrink_to_fit();
        g_halfs.clear(); g_halfs.shrink_to_fit();
        flows_fy.clear(); flows_fy.shrink_to_fit();
        flows_fx.clear(); flows_fx.shrink_to_fit();
        mw_half.clear(); mw_half.shrink_to_fit();
        release_free_heap_pages();
    };
    if (!need_sources_post_s45) {
        release_burst_sources();
    }
    double mean_icov[3]={0,0,0};
    size_t kernel_samples=0;
    for(size_t i=0;i<(size_t)Hh*Wh;i+=97) {
        for(int c=0;c<3;++c) mean_icov[c]+=inv_cov[i*3+c];
        ++kernel_samples;
    }
    auto kernel_power=heraw_core::kernel_power_from_inverse_covariance(
        (float)(mean_icov[0]/std::max<size_t>(1,kernel_samples)),
        (float)(mean_icov[1]/std::max<size_t>(1,kernel_samples)),
        (float)(mean_icov[2]/std::max<size_t>(1,kernel_samples)));
    if (wctx.active) {
        wctx.modulate_walsh_kernel_power(kernel_power, pg_sig0_sq, pg_gain);
    }
    inv_cov.clear(); inv_cov.shrink_to_fit();
    g_guide.clear(); g_guide.shrink_to_fit();
    snr_boost_half.clear(); snr_boost_half.shrink_to_fit();
    g0_ha_1x.clear(); g0_ha_1x.shrink_to_fit();
    rl_mod_f16.clear(); rl_mod_f16.shrink_to_fit();
    release_free_heap_pages();

    if (!cfg.dataset_dir.empty()) {
        const std::vector<float> empty;
        heraw_optics::DatasetInput input{frames_lin[0],empty,empty,field_phase,field_motion,
            &cfa6x6[0][0],H1,W1,true,N>=2,cam_make+" "+cam_model,
            "Leave-one-out linear CFA kernel; shared exposure, sensor calibration and image registration"};
        int cached_y=-1,cached_x=-1;
        std::array<float,4> cached{};
        auto gather=[&](int y,int x) {
            if(y==cached_y && x==cached_x) return;
            cached_y=y;cached_x=x;
            double sum[3]={},weight[3]={},variance[3]={};
            for(int n=1;n<N;++n) {
                const float yh = (y + 0.5f) * 0.25f - 0.5f;
                const float xh = (x + 0.5f) * 0.25f - 0.5f;
                const float mw = bilerp_clamped(mw_half[n].data(), Hh, Wh, yh, xh);
                if (mw < 0.10f) continue;
                float sy = 0.5f * y + 2.0f * bilerp_clamped(flows_fy[n].data(), Hh, Wh, yh, xh);
                float sx = 0.5f * x + 2.0f * bilerp_clamped(flows_fx[n].data(), Hh, Wh, yh, xh);
                if (sy < -0.5f || sy > (float)H1 - 0.5f || sx < -0.5f || sx > (float)W1 - 0.5f) continue;
                int iy=(int)std::floor(sy),ix=(int)std::floor(sx);
                for(int yy=std::max(0,iy-2);yy<=std::min(H1-1,iy+3);++yy)
                    for(int xx=std::max(0,ix-2);xx<=std::min(W1-1,ix+3);++xx) {
                        int c=cfa6x6[yy%6][xx%6];
                        float v=frames_lin[n][(size_t)yy*W1+xx];
                        float dy=yy-sy,dx=xx-sx;
                        double w = (double)mw * std::exp(-(dy*dy+dx*dx)/(2*0.6*0.6));
                        sum[c]+=w*v;weight[c]+=w;
                        variance[c]+=w*w*core_noise.frame_variance(n,v);
                    }
            }
            double sigma_sq=0;
            for(int c=0;c<3;++c) {
                cached[c]=weight[c]>1e-12 ? (float)(sum[c]/weight[c]) : std::numeric_limits<float>::quiet_NaN();
                sigma_sq+=variance[c]/std::max(1e-24,weight[c]*weight[c]);
            }
            cached[3]=(float)std::sqrt(sigma_sq/3.0);
        };
        input.target_sample=[&](int y,int x,int c) {gather(y,x);return cached[c];};
        input.sigma_sample=[&](int y,int x) {gather(y,x);return cached[3];};
        heraw_optics::DatasetOptions options;
        options.tile_size=cfg.tile_size/2;options.min_conf=cfg.tile_min_conf;
        const auto report=heraw_optics::export_dataset(cfg.dataset_dir,input,options);
        log_err("[W12] Paired dataset: %zu tiles, %zu rejected -> %s\n",
            report.accepted,report.rejected,report.manifest_path.c_str());
    }

    cost_mark("release-guides-dataset");
    float* active_rgb = need_2x ? rgb_2x.data() : rgb_1x.data();
    const int active_H = need_2x ? H2 : H1;
    const int active_W = need_2x ? W2 : W1;
    heraw_core::Noise c_noise_wv = core_noise;
    if (wctx.active) {
        c_noise_wv.read_variance += wctx.mean_q_luma_code_var * wctx.q_nlt_s0;
        c_noise_wv.shot += wctx.mean_q_luma_code_var * wctx.q_nlt_s1;
        c_noise_wv.quad += wctx.mean_q_luma_code_var * wctx.q_nlt_s2;
    }
    if (reconstruct_any && cfg.ibp_iters > 0) {
        const auto start=std::chrono::steady_clock::now();
        if (need_2x) {
            const auto stats = heraw_core::compact_ibp(frames_lin, H1, W1, &cfa6x6[0][0],
                flows_fy, flows_fx, mw_half, c_noise_wv, rgb_2x.data(), cfg.ibp_iters, core_execution);
            log_err("[C1] Sampled RAW mean residual %.6g -> %.6g (%zu samples, %d iterations, %.3fs, %zu candidates, %.2f MiB index).\n",
                stats.before/std::max<size_t>(1,stats.samples), stats.after/std::max<size_t>(1,stats.samples),
                stats.samples,stats.iterations,elapsed_s(start),stats.candidate_samples,
                stats.index_bytes/(1024.0*1024.0));
            log_err("[C1Cost] index=%.6f diagnostics=%.6f update=%.6f s\n",
                stats.worker_seconds[0],stats.worker_seconds[1],stats.worker_seconds[2]);
        } else {
            const auto stats = heraw_core::compact_ibp_1x(frames_lin, H1, W1, &cfa6x6[0][0],
                flows_fy, flows_fx, mw_half, c_noise_wv, rgb_1x.data(), cfg.ibp_iters, core_execution);
            log_err("[C1-1x] Sampled 1x RAW mean residual %.6g -> %.6g (%zu samples, %d iterations, %.3fs).\n",
                stats.before/std::max<size_t>(1,stats.samples), stats.after/std::max<size_t>(1,stats.samples),
                stats.samples, stats.iterations, elapsed_s(start));
        }
    }
    cost_mark("ibp");
    if (!is_phase_balanced) {
        release_burst_sources();
    }
    if (reconstruct_any && !cfg.relay_raw && cfg.denoise_strength > 0) {
        heraw_core::HalfStackOptions opts;
        opts.strength = cfg.denoise_strength;
        opts.effective_frames = (float)N;
        opts.kernel_power = kernel_power.data();
        opts.fusion_eta = fusion_eta.empty() ? nullptr : fusion_eta.data();
        opts.fusion_eta_h = (H1 + 3) / 4; opts.fusion_eta_w = (W1 + 3) / 4;
        opts.phase_balanced = need_2x && is_phase_balanced;
        if (N >= (need_2x ? 5 : 3)) {
            const auto start=std::chrono::steady_clock::now();
            const auto stats = opts.phase_balanced ?
                heraw_core::half_stack_denoise(frames_lin,H1,W1,&cfa6x6[0][0],flows_fy,flows_fx,mw_half,
                    c_noise_wv,rgb_2x.data(),opts,core_execution) :
                heraw_core::half_stack_denoise(active_rgb,nullptr,nullptr,active_H,active_W,c_noise_wv,opts,core_execution);
            log_err("[C6] %s KLT shrink: %zu samples, measured variance %.7g (%.3fs).\n",
                opts.phase_balanced?"Half-stack":"Fusion-eta analytic",stats.samples,stats.noise_variance,elapsed_s(start));
            log_err("[C6Cost] fill=%.6f transform=%.6f filter=%.6f worker-s; scratch=%zu bytes/worker; phase-balanced=%d\n",
                stats.worker_seconds[0],stats.worker_seconds[1],stats.worker_seconds[2],
                stats.peak_scratch_bytes,int(opts.phase_balanced));
        }
        release_burst_sources();
        fusion_eta.clear(); fusion_eta.shrink_to_fit();
        const auto color_start=std::chrono::steady_clock::now();
        heraw_core::color_line_refine(active_rgb, active_H, active_W,
            {(float)sig_eff_sq,(float)g_eff}, 0.25f, core_execution);
        log_err("[C2] Color-line refinement: %.3fs.\n",elapsed_s(color_start));
    }
    release_burst_sources();
    fusion_eta.clear(); fusion_eta.shrink_to_fit();
    cost_mark("half-stack-color-line");
    if (reconstruct_any && !cfg.relay_raw && cfg.psf_iters>0 && optics.frames>0) {
        std::vector<heraw_core::SpatialPsfZone> zones;
        const float centers[5][2]={{.5f,.5f},{.2f,.2f},{.8f,.2f},{.2f,.8f},{.8f,.8f}};
        const float psf_scale = need_2x ? 2.0f : 1.0f;
        for(size_t i=0;i<optics.zones.size();++i) {
            const auto& z=optics.zones[i];
            if(!z.mtf50.valid) continue;
            const float isotropic=(float)((0.18739 * psf_scale)/std::max(.03,z.mtf50.value));
            heraw_core::SpatialPsfZone model;
            model.cx=centers[i][0];model.cy=centers[i][1];
            model.sigma_major_2x=z.fwhm_major.valid ? (float)(z.fwhm_major.value*psf_scale/2.35482) : isotropic;
            model.sigma_minor_2x=z.fwhm_minor.valid ? (float)(z.fwhm_minor.value*psf_scale/2.35482) : isotropic;
            model.angle=z.angle_deg.valid ? (float)(z.angle_deg.value*3.14159265359/180) : 0.0f;
            model.valid=true;zones.push_back(model);
        }
        if(!zones.empty()) {
            auto stats=heraw_core::spatial_psf_deconvolve(active_rgb,active_H,active_W,zones,
                {(float)sig_eff_sq,(float)g_eff},1,0.25f,core_execution);
            log_err("[C5] Spatial PSF: %zu zones, residual %.6g -> %.6g.\n",zones.size(),stats.before,stats.after);
        }
    }
    cost_mark("spatial-psf");
    if (reconstruct_any && !temporal_composite.rgb.empty()) {
        const float scale_h = (float)Hh / (float)active_H;
        const float scale_1 = (float)H1 / (float)active_H;
        parallel_for_rows(active_H, n_threads, [&](int begin, int end, int) {
            for (int y = begin; y < end; ++y) {
                const float yh = ((float)y + 0.5f) * scale_h - 0.5f;
                const float y1 = clamp_f(((float)y + 0.5f) * scale_1 - 0.5f, 0.0f, (float)(H1 - 1));
                const int sy0 = clamp_i((int)std::floor(y1), 0, H1 - 1);
                const int sy1 = std::min(H1 - 1, sy0 + 1);
                const float fy = y1 - (float)sy0;
                for (int x = 0; x < active_W; ++x) {
                    const size_t p = (size_t)y * active_W + x;
                    const float xh = ((float)x + 0.5f) * scale_h - 0.5f;
                    float alpha = 1.0f;
                    if (cfg.motion_mode == "strobe")
                        alpha = clamp_f(bilerp_clamped(primary_temporal.activity.data(), Hh, Wh, yh, xh) * N, 0.0f, 1.0f);
                    else if (cfg.motion_mode == "virtual-nd")
                        alpha = clamp_f((bilerp_clamped(field_motion.data(), Hh, Wh, yh, xh) - cfg.vnd_thresh) * 8.0f, 0.0f, 1.0f);
                    const float x1 = clamp_f(((float)x + 0.5f) * scale_1 - 0.5f, 0.0f, (float)(W1 - 1));
                    const int sx0 = clamp_i((int)std::floor(x1), 0, W1 - 1);
                    const int sx1 = std::min(W1 - 1, sx0 + 1);
                    const float fx = x1 - (float)sx0;
                    for (int c = 0; c < 3; ++c) {
                        const float v = (1.0f - fy) * ((1.0f - fx) * temporal_composite.rgb[((size_t)sy0 * W1 + sx0) * 3 + c] +
                                                       fx * temporal_composite.rgb[((size_t)sy0 * W1 + sx1) * 3 + c]) +
                                        fy * ((1.0f - fx) * temporal_composite.rgb[((size_t)sy1 * W1 + sx0) * 3 + c] +
                                              fx * temporal_composite.rgb[((size_t)sy1 * W1 + sx1) * 3 + c]);
                        active_rgb[p * 3 + c] += (v - active_rgb[p * 3 + c]) * alpha;
                    }
                }
            }
        });
    }
    if (reconstruct_any && !reflection_composite.rgb.empty()) {
        const auto& specular = suite_fields.maps.at("specular");
        const float scale_h = (float)Hh / (float)active_H;
        const float scale_1 = (float)H1 / (float)active_H;
        parallel_for_rows(active_H, n_threads, [&](int begin, int end, int) {
            for (int y = begin; y < end; ++y) {
                const float yh = ((float)y + 0.5f) * scale_h - 0.5f;
                const float y1 = clamp_f(((float)y + 0.5f) * scale_1 - 0.5f, 0.0f, (float)(H1 - 1));
                const int sy0 = clamp_i((int)std::floor(y1), 0, H1 - 1);
                const int sy1 = std::min(H1 - 1, sy0 + 1);
                const float fy = y1 - (float)sy0;
                for (int x = 0; x < active_W; ++x) {
                    const size_t p = (size_t)y * active_W + x;
                    const float xh = ((float)x + 0.5f) * scale_h - 0.5f;
                    const float excursion = bilerp_clamped(specular.data(), Hh, Wh, yh, xh);
                    const float noise = std::sqrt((float)pg_sig0_sq + (float)pg_gain * std::max(0.0f, active_rgb[p * 3 + 1]));
                    const float alpha = clamp_f(excursion / (4.0f * noise + 0.001f), 0.0f, 1.0f);
                    const float x1 = clamp_f(((float)x + 0.5f) * scale_1 - 0.5f, 0.0f, (float)(W1 - 1));
                    const int sx0 = clamp_i((int)std::floor(x1), 0, W1 - 1);
                    const int sx1 = std::min(W1 - 1, sx0 + 1);
                    const float fx = x1 - (float)sx0;
                    for (int c = 0; c < 3; ++c) {
                        const float lower = (1.0f - fy) * ((1.0f - fx) * reflection_composite.rgb[((size_t)sy0 * W1 + sx0) * 3 + c] +
                                                           fx * reflection_composite.rgb[((size_t)sy0 * W1 + sx1) * 3 + c]) +
                                            fy * ((1.0f - fx) * reflection_composite.rgb[((size_t)sy1 * W1 + sx0) * 3 + c] +
                                                  fx * reflection_composite.rgb[((size_t)sy1 * W1 + sx1) * 3 + c]);
                        active_rgb[p * 3 + c] -= alpha * std::max(0.0f, active_rgb[p * 3 + c] - lower);
                    }
                }
            }
        });
    }
    frames_lin.clear(); frames_lin.shrink_to_fit();
    g_halfs.clear(); g_halfs.shrink_to_fit();
    flows_fy.clear(); flows_fy.shrink_to_fit();
    flows_fx.clear(); flows_fx.shrink_to_fit();
    mw_half.clear(); mw_half.shrink_to_fit();
    temporal_anchor.clear(); temporal_anchor.shrink_to_fit();
    temporal_composite.rgb.clear(); temporal_composite.rgb.shrink_to_fit();
    reflection_composite.rgb.clear(); reflection_composite.rgb.shrink_to_fit();

    // --- Stage 6: Color Space Matrix + Non-Destructive EXIF/GPS + On-Demand RGGB / RGB / Field Export ---
    cost_mark("compositing-source-release");
    t0 = std::chrono::steady_clock::now();
    float hdr_white=1.0f;
    if(bracket_exposures && !exposure_stats.gains.empty())
        hdr_white=std::max(1.0f,*std::max_element(exposure_stats.gains.begin(),exposure_stats.gains.end()));
    if(hdr_white>1.0f) {
        const float inv=1.0f/hdr_white;
        if(reconstruct_any) parallel_for_rows(active_H,n_threads,[&](int begin,int end,int) {
            for(size_t i=(size_t)begin*active_W*3;i<(size_t)end*active_W*3;++i) active_rgb[i]*=inv;
        });
        for(auto& image:extra_images) for(float& v:image.second.rgb) v*=inv;
        g_eff*=inv;sig_eff_sq*=inv*inv;resid_noise_2x*=inv;
        pg_gain*=inv;pg_sig0_sq*=inv*inv;
        auto sigma=suite_fields.maps.find("sigma");
        if(sigma!=suite_fields.maps.end()) for(float& v:sigma->second) v*=inv;
    }

    heraw_io::ExifBundle src_exif = heraw_io::extract_source_exif(anchor_raw_path);
    heraw_io::ExifBundle rot_exif = src_exif;
    if (cam_flip == 5 || cam_flip == 6) {
        heraw_io::swap_exif_focal_plane_xy(rot_exif);
    }
    char sw_buf[180];
    std::snprintf(sw_buf, sizeof(sw_buf), "NijiN.LiteBurstSuite (nijinLBS) " NIJIN_VERSION " (N=%d, %s)",
                  N, cfg.preset_name.c_str());
    const std::string sw_stamp = cfg.no_tool_exif ? std::string() : std::string(sw_buf);
    const std::string primary_output = !out_rgb_path.empty() ? out_rgb_path :
        (!out_rgb_1x_path.empty() ? out_rgb_1x_path :
        (!out_raw_path.empty() ? out_raw_path : (!out_field_path.empty() ? out_field_path :
        (!cfg.dataset_dir.empty() ? cfg.dataset_dir+"/summary" :
        anchor_default_stem+"_nijinLBS"))));
    const std::string suite_prefix = !cfg.suite_prefix_override.empty()
        ? cfg.suite_prefix_override
        : std::filesystem::u8path(primary_output).replace_extension().u8string();

    double cam2out_d[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    bool has_xyz = false;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            if (std::fabs(cam_xyz[r][c]) > 1e-5) has_xyz = true;

    const double srgb2xyz[3][3] = {
        {0.4124564, 0.3575761, 0.1804375},
        {0.2126729, 0.7151522, 0.0721750},
        {0.0193339, 0.1191920, 0.9503041},
    };
    const double adobergb2xyz[3][3] = {
        {0.5767309, 0.1855540, 0.1881852},
        {0.2973769, 0.6273491, 0.0752741},
        {0.0270343, 0.0706872, 0.9911085},
    };
    const auto& target2xyz = (cfg.color_space == heraw_io::ColorSpaceMode::ADOBE_RGB) ? adobergb2xyz : srgb2xyz;

    if (has_xyz) {
        double rgb2cam[3][3] = {{0}};
        for (int r = 0; r < 3; ++r) {
            double rsum = 0.0;
            for (int c = 0; c < 3; ++c) {
                for (int k = 0; k < 3; ++k) rgb2cam[r][c] += cam_xyz[r][k] * target2xyz[k][c];
                rsum += rgb2cam[r][c];
            }
            if (std::fabs(rsum) > 1e-8) {
                for (int c = 0; c < 3; ++c) rgb2cam[r][c] /= rsum;
            }
        }
        invert_3x3(rgb2cam, cam2out_d);
    } else {
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) cam2out_d[r][c] = rgb_cam[r][c];
        invert_3x3(srgb2xyz, cam_xyz);
    }

    float exp_gain = hdr_white;
    float astro_sky_ped = 0.0f;
    float astro_sky_sub = 0.0f;
    float astro_sky_iqr_sig = 0.0f;
    float astro_asinh_k = 3.2f;
    if (cfg.auto_exposure && reconstruct_any) {
        const int stride = need_2x ? 16 : 8;
        std::vector<float> lum_samples;
        lum_samples.reserve((size_t)(active_H / stride + 1) * (size_t)(active_W / stride + 1));
        for (int r = active_H / 12; r < 11 * active_H / 12; r += stride) {
            for (int c = active_W / 12; c < 11 * active_W / 12; c += stride) {
                size_t i = (size_t)r * active_W + c;
                float wr = active_rgb[i*3 + 0] * cam_wb[0];
                float wg = active_rgb[i*3 + 1] * cam_wb[1];
                float wb = active_rgb[i*3 + 2] * cam_wb[2];
                float lr = (float)(cam2out_d[0][0]*wr + cam2out_d[0][1]*wg + cam2out_d[0][2]*wb);
                float lg = (float)(cam2out_d[1][0]*wr + cam2out_d[1][1]*wg + cam2out_d[1][2]*wb);
                float lb = (float)(cam2out_d[2][0]*wr + cam2out_d[2][1]*wg + cam2out_d[2][2]*wb);
                lum_samples.push_back(std::max(0.0f, 0.2126f * lr + 0.7152f * lg + 0.0722f * lb));
            }
        }
        if (is_astro) {
            float p25_l = std::max(0.0f, quick_percentile(lum_samples, 0.25f));
            float p50_l = std::max(1e-5f, quick_percentile(lum_samples, 0.50f));
            float p75_l = std::max(p25_l, quick_percentile(lum_samples, 0.75f));
            float p9995_l = std::max(p50_l + 1e-4f, quick_percentile(lum_samples, 0.9995f));
            astro_sky_iqr_sig = std::max(resid_noise_2x, (p75_l - p25_l) / 1.349f);
            astro_sky_sub = 0.88f * p50_l;
            astro_sky_ped = std::max(0.0f, p50_l - astro_sky_sub);
            float sig0_sensor = (float)std::sqrt(std::max(0.0, pg_sig0_sq));
            float max_astro_gain = clamp_f(0.012f / std::max(0.0040f, sig0_sensor), 1.00f, 3.0f);
            exp_gain = clamp_f(0.028f / std::max(0.009f, p50_l), 0.90f, max_astro_gain);
            float max_asinh_k = (sig0_sensor > 0.012f) ? 4.8f : 8.5f;
            astro_asinh_k = clamp_f(0.14f / std::max(1e-4f, (p9995_l - p50_l) * exp_gain), 3.2f, max_asinh_k);
        } else {
            float p50_l = std::max(1e-4f, quick_percentile(lum_samples, 0.50f));
            float p995_l = std::max(1e-3f, quick_percentile(lum_samples, 0.995f));
            exp_gain = clamp_f(std::min(0.18f / p50_l, 0.92f / p995_l), 1.0f, 4.5f*hdr_white);
        }
    }
    if (std::fabs(cfg.ev_offset) > 1e-4f) {
        exp_gain = clamp_f(exp_gain * std::pow(2.0f, cfg.ev_offset), 0.125f, 16.0f*hdr_white);
    }
    double baseline_ev = std::log2((double)exp_gain);
    const float dither_amp = cfg.dither ? 0.5f : 0.0f;

    auto par_wrap = [](int tot, int nt, auto&& fn, int chk) {
        parallel_for_rows(tot, nt, std::forward<decltype(fn)>(fn), chk);
    };

    // Rotate directional normal vector components to upright output axes when cam_flip != 0
    if (cam_flip == 3 || cam_flip == 5 || cam_flip == 6) {
        auto nx_it = suite_fields.maps.find("normal_x");
        auto ny_it = suite_fields.maps.find("normal_y");
        if (nx_it != suite_fields.maps.end() && ny_it != suite_fields.maps.end() &&
            nx_it->second.size() == ny_it->second.size()) {
            auto& nx = nx_it->second;
            auto& ny = ny_it->second;
            for (size_t i = 0; i < nx.size(); ++i) {
                float ox = nx[i], oy = ny[i];
                if (cam_flip == 3) { nx[i] = -ox; ny[i] = -oy; }
                else if (cam_flip == 5) { nx[i] = oy; ny[i] = -ox; }
                else if (cam_flip == 6) { nx[i] = -oy; ny[i] = ox; }
            }
        }
    }

    std::vector<float> r_field_src = field_motion;
    bool is_astro_sky_mask = false;
    if (is_astro && astro_celestial_locked > 0 && astro_sky_accum.size() == (size_t)Hh * Wh) {
        double sky_sum = 0.0;
        float inv_lk = 1.0f / (float)astro_celestial_locked;
        for (size_t i = 0; i < astro_sky_accum.size(); ++i) sky_sum += astro_sky_accum[i] * inv_lk;
        double mean_sky = sky_sum / (double)std::max<size_t>(1, astro_sky_accum.size());
        if (mean_sky < 0.94) {
            for (size_t i = 0; i < r_field_src.size(); ++i) {
                r_field_src[i] = clamp_f(astro_sky_accum[i] * inv_lk, 0.0f, 1.0f);
            }
            is_astro_sky_mask = true;
        }
    }

    std::map<std::string,std::vector<float>> field_export_maps;
    std::map<std::string,float> field_export_scales;
    for (const auto& item : suite_fields.maps) {
        const auto& gate=suite_fields.gates.at(item.first);
        if (!gate.observable) {
            log_err("[Field %s] %s.\n",item.first.c_str(),gate.reason.c_str());
            continue;
        }
        float scale=1.0f;
        if(item.first=="sigma") {
            scale=0.0f;
            for(float value:item.second) if(std::isfinite(value)) scale=std::max(scale,value);
        }
        else if(item.first=="velocity" || item.first=="turbulence" || item.first=="defocus" ||
                item.first=="normal_x" || item.first=="normal_y") {
            std::vector<float> pos_vals;
            pos_vals.reserve(item.second.size() / 4 + 16);
            float max_abs = 0.0f;
            for (float value : item.second) {
                if (!std::isfinite(value)) continue;
                const float av = std::fabs(value);
                max_abs = std::max(max_abs, av);
                if (av > 1e-12f) pos_vals.push_back(av);
            }
            scale = (pos_vals.size() >= 16) ? quick_percentile(pos_vals, 0.995f) : max_abs;
        }
        scale=std::max(scale,1e-8f);
        field_export_scales[item.first]=scale;
        auto& dst=field_export_maps[item.first];
        dst.resize(item.second.size());
        for(size_t i=0;i<dst.size();++i) {
            const float raw_v = std::isfinite(item.second[i]) ? item.second[i] : 0.0f;
            const float mapped = (item.first=="defocus" || item.first=="normal_x" || item.first=="normal_y") ?
                (0.5f + 0.5f * raw_v / scale) : (raw_v / scale);
            dst[i] = clamp_f(mapped, 0.0f, 1.0f);
        }
    }
    auto field_selected = [&](const std::string& name) {
        return ("," + cfg.fields_csv + ",").find("," + name + ",") != std::string::npos;
    };
    std::vector<std::pair<std::string, const std::vector<float>*>> field_sources = {
        {"motion", &r_field_src}, {"phase", &field_phase}, {"focus", &field_focus}
    };
    for (const auto& item : field_export_maps) field_sources.emplace_back(item.first,&item.second);
    const std::string field_order=","+cfg.fields_csv+",";
    std::stable_sort(field_sources.begin(),field_sources.end(),[&](const auto& a,const auto& b) {
        return field_order.find(","+a.first+",")<field_order.find(","+b.first+",");
    });
    if (!out_field_path.empty()) {
        std::string fields_prefix = suite_prefix;
        if (cfg.field_mode != heraw_io::FieldExportMode::TIFF_ALPHA) {
            fields_prefix = out_field_path;
            size_t sl = fields_prefix.find_last_of("/\\");
            size_t dt = fields_prefix.find_last_of('.');
            if (dt != std::string::npos && (sl == std::string::npos || dt > sl)) {
                fields_prefix = fields_prefix.substr(0, dt);
            }
            if (cfg.field_from_outdir && fields_prefix.size() > 6 &&
                fields_prefix.substr(fields_prefix.size() - 6) == "_field") {
                fields_prefix.resize(fields_prefix.size() - 6);
            }
        }
        FILE* report=fopen_utf8_wb(fields_prefix+"_fields.json");
        if(!report) return 1;
        std::fprintf(report,"{\"domain\":\"camera_linear\",\"raw_width\":%d,\"raw_height\":%d,"
            "\"output_orientation\":1,\"sensor_orientation\":%d,\"selected\":\"%s\",\"parallax_2x_px\":%.6g,\"fields\":[",
            W1,H1,flip_to_exif_orientation(cam_flip),json_escape(cfg.fields_csv).c_str(),
            2.0*suite_fields.geometry.disparity_span_px);
        bool first=true;
        const char* core3[3] = {"motion", "phase", "focus"};
        const char* core3_desc[3] = {
            is_astro_sky_mask ? "celestial sky-horizon separation mask" : "multi-frame morphological motion matte",
            "subpixel 2x2 phase & SNR coverage",
            "anchor Laplacian optical focus envelope"
        };
        for (int ci = 0; ci < 3; ++ci) {
            if (!field_selected(core3[ci])) continue;
            std::fprintf(report,
                "%s{\"name\":\"%s\",\"observable\":true,\"units\":\"normalized\","
                "\"measured\":1,\"threshold\":0,\"confidence\":1,\"display_scale\":1,"
                "\"display_offset\":0,\"display_gain\":1,\"reason\":\"%s\"}",
                first ? "" : ",", core3[ci], core3_desc[ci]);
            first = false;
        }
        for(const auto& item:suite_fields.gates) {
            const auto& gate=item.second;
            float scale=field_export_scales.count(item.first)?field_export_scales.at(item.first):1.0f;
            const bool signed_field=item.first=="defocus" || item.first=="normal_x" || item.first=="normal_y";
            std::fprintf(report,"%s{\"name\":\"%s\",\"observable\":%s,\"units\":\"%s\","
                "\"measured\":%.9g,\"threshold\":%.9g,\"confidence\":%.9g,\"display_scale\":%.9g,"
                "\"display_offset\":%.9g,\"display_gain\":%.9g,\"reason\":\"%s\"}",
                first?"":",",item.first.c_str(),gate.observable?"true":"false",json_escape(gate.units).c_str(),
                gate.measured,gate.threshold,gate.confidence,scale,signed_field?0.5:0.0,
                (signed_field?0.5:1.0)/scale,json_escape(gate.reason).c_str());
            first=false;
        }
        std::fprintf(report,"]}\n");
        const bool failed=std::ferror(report)!=0;
        if(std::fclose(report)!=0 || failed) return 1;
    }
    auto sample_output_field = [&](const std::vector<float>& src, int y, int x, int cH, int cW) -> uint16_t {
        int r = y, c = x;
        if (cam_flip == 3) { r = cH - 1 - y; c = cW - 1 - x; }
        else if (cam_flip == 5) { r = x; c = cW - 1 - y; }
        else if (cam_flip == 6) { r = cH - 1 - x; c = y; }
        const float scale_h = (float)Hh / (float)std::max(1, cH);
        float value = bilerp_clamped(src.data(), Hh, Wh, (r + 0.5f) * scale_h - 0.5f, (c + 0.5f) * scale_h - 0.5f);
        return (uint16_t)std::lround(clamp_f(value, 0.0f, 1.0f)*65535.0f);
    };
    const int extra_cH = (pure_1x_pref && !need_2x) ? H1 : H2;
    const int extra_cW = (pure_1x_pref && !need_2x) ? W1 : W2;
    auto write_extra_image = [&](const heraw_suite::Image& image, const std::string& name) -> bool {
        const int cH = (name == "refocus") ? image.H : extra_cH;
        const int cW = (name == "refocus") ? image.W : extra_cW;
        const int out_H = (cam_flip == 5 || cam_flip == 6) ? cW : cH;
        const int out_W = (cam_flip == 5 || cam_flip == 6) ? cH : cW;
        const bool gray = name == "transient" && cfg.transient_color == "gray";
        const bool apply_astro_ped = is_astro && cfg.auto_exposure && (name == "refocus");
        const float astro_ped_s = astro_sky_ped * exp_gain;
        const float astro_sub_s = astro_sky_sub * exp_gain;
        const float astro_ng = astro_ped_s + 2.8f * std::max(resid_noise_2x, astro_sky_iqr_sig) * exp_gain;
        const float inv_ak = 1.0f / std::asinh(astro_asinh_k);
        std::vector<uint16_t> pixels((size_t)out_H*out_W*(gray?1:3));
        parallel_for_rows(cH,n_threads,[&](int begin,int end,int) {
            for(int y=begin;y<end;++y) for(int x=0;x<cW;++x) {
                const float sy=(y+0.5f)*image.H/cH-0.5f,sx=(x+0.5f)*image.W/cW-0.5f;
                const int y0=clamp_i((int)std::floor(sy),0,image.H-1),x0=clamp_i((int)std::floor(sx),0,image.W-1);
                const int y1=std::min(y0+1,image.H-1),x1=std::min(x0+1,image.W-1);
                const float fy=clamp_f(sy-y0,0,1),fx=clamp_f(sx-x0,0,1);
                float cam[3];
                for(int c=0;c<3;++c) cam[c]=
                    ((1-fy)*((1-fx)*image.rgb[((size_t)y0*image.W+x0)*3+c]+fx*image.rgb[((size_t)y0*image.W+x1)*3+c])+
                     fy*((1-fx)*image.rgb[((size_t)y1*image.W+x0)*3+c]+fx*image.rgb[((size_t)y1*image.W+x1)*3+c]))*cam_wb[c]*exp_gain;
                float lin[3];
                for(int c=0;c<3;++c) {
                    lin[c]=std::max(0.0f,(float)(cam2out_d[c][0]*cam[0]+cam2out_d[c][1]*cam[1]+cam2out_d[c][2]*cam[2]));
                }
                if (apply_astro_ped) {
                    for (int c = 0; c < 3; ++c) lin[c] = std::max(0.0f, lin[c] - astro_sub_s);
                    float lum = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
                    if (lum > astro_ng + 1e-6f) {
                        float exc = lum - astro_ng;
                        float stretched = astro_ng + (1.0f - astro_ng) * std::asinh(astro_asinh_k * exc) * inv_ak;
                        float s_a = stretched / lum;
                        lin[0] *= s_a; lin[1] *= s_a; lin[2] *= s_a;
                    }
                }
                if (cfg.highlight_rolloff) {
                    float peak = std::max(lin[0], std::max(lin[1], lin[2]));
                    if (peak > 0.78f) {
                        float comp = 0.78f + 0.22f * std::tanh((peak - 0.78f) / 0.22f);
                        float sc = comp / peak;
                        lin[0] *= sc; lin[1] *= sc; lin[2] *= sc;
                        if (!is_astro) {
                            float desat = clamp_f((peak - 0.85f) / 0.65f, 0.0f, 0.85f);
                            for (int c = 0; c < 3; ++c) lin[c] = lin[c] * (1.0f - desat) + comp * desat;
                        }
                    }
                }
                float rgb[3];
                for(int c=0;c<3;++c) {
                    float v = lin[c];
                    rgb[c]=cfg.color_space==heraw_io::ColorSpaceMode::ADOBE_RGB ?
                        std::pow(v,256.0f/563.0f) : (v<=0.0031308f?12.92f*v:1.055f*std::pow(v,1.0f/2.4f)-0.055f);
                }
                int dy=y,dx=x;
                if(cam_flip==3) {dy=cH-1-y;dx=cW-1-x;}
                else if(cam_flip==5) {dy=cW-1-x;dx=y;}
                else if(cam_flip==6) {dy=x;dx=cH-1-y;}
                size_t p=(size_t)dy*out_W+dx;
                if(gray) pixels[p]=(uint16_t)std::lround(clamp_f(0.2126f*rgb[0]+0.7152f*rgb[1]+0.0722f*rgb[2],0,1)*65535);
                else for(int c=0;c<3;++c) pixels[p*3+c]=(uint16_t)std::lround(clamp_f(rgb[c],0,1)*65535);
            }
        });
        const std::string path=suite_prefix+"_"+name+".png";
        if(gray) return heraw_io::write_png16_gray(path,pixels.data(),out_H,out_W,rot_exif,sw_stamp,
            n_threads,par_wrap,fopen_utf8_wb,remove_utf8,atomic_replace_utf8,false,cfg.color_space);
        return heraw_io::write_png16_rgb(path,pixels.data(),out_H,out_W,cfg.color_space,rot_exif,sw_stamp,
            n_threads,par_wrap,fopen_utf8_wb,remove_utf8,atomic_replace_utf8);
    };

    auto quant_u16 = [](float v) -> uint16_t {
        if (!(v > 0.0f)) return 0u;
        if (v >= 65535.0f) return 65535u;
        return (uint16_t)(v + 0.5f);
    };

    // 6a) On-demand Export: 2x RGGB Linear RAW DNG 1.4 (Lossless SOF3 / Visually-Lossless HQ / FP16 / Uncompressed)
    cost_mark("export-preparation");
    if (!out_raw_path.empty()) {
        UninitBuf<uint16_t> rggb_u16;
        if (cfg.raw_mode != heraw_io::RawDngMode::DNG_FP16 && cfg.raw_mode != heraw_io::RawDngMode::DNG_LINEAR) {
            const float raw_dith = (cfg.raw_mode == heraw_io::RawDngMode::DNG_HQ) ? 0.0f : dither_amp;
            rggb_u16.resize((size_t)H2 * (size_t)W2);
            parallel_for_rows(H2, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    const int ch_even = (r & 1) ? 1 : 0;
                    const int ch_odd  = (r & 1) ? 2 : 1;
                    uint16_t* __restrict drow = rggb_u16.data() + (size_t)r * W2;
                    const float* __restrict srow = rgb_2x.data() + (size_t)r * W2 * 3;
                    if (raw_dith == 0.0f) {
                        for (int c = 0; c + 1 < W2; c += 2) {
                            drow[c]     = quant_u16(srow[c * 3 + ch_even] * 65535.0f);
                            drow[c + 1] = quant_u16(srow[(c + 1) * 3 + ch_odd] * 65535.0f);
                        }
                        if (W2 & 1) drow[W2 - 1] = quant_u16(srow[(W2 - 1) * 3 + ch_even] * 65535.0f);
                    } else {
                        for (int c = 0; c < W2; ++c) {
                            size_t idx = (size_t)r * W2 + c;
                            int ch = (c & 1) ? ch_odd : ch_even;
                            drow[c] = quant_u16(srow[c * 3 + ch] * 65535.0f + raw_dith * tpdf_dither(idx * 4ULL + 1ULL));
                        }
                    }
                }
            });
        }
        cost_mark("raw-quantize");
        if (!heraw_io::write_dng14_rggb_ex(
                out_raw_path, rggb_u16.data(), rgb_2x.data(), H2, W2, cfg.raw_mode, sig_eff_sq, g_eff,
                cam_make, cam_model, cam_wb, cam_xyz, cam_flip, baseline_ev,
                src_exif, sw_stamp, n_threads, flip_to_exif_orientation, par_wrap,
                fopen_utf8_wb, remove_utf8, atomic_replace_utf8)) {
            log_err("Error writing DNG to %s\n", out_raw_path.c_str());
            return 1;
        }
    }

    for (auto& image : extra_images) {
        if (!write_extra_image(image.second,image.first)) {
            log_err("Error writing %s layer.\n",image.first.c_str()); return 1;
        }
        image.second.rgb.clear(); image.second.rgb.shrink_to_fit();
    }

    // 6b) Post-RAW RGB Finishing (Relay-RAW denoise/RL + RGB micro-contrast RL + Refocus + 2x/1x Rendered RGB Export)
    cost_mark("raw-encode-extra-images");
    const bool finish_rgb_canvas = !out_rgb_path.empty() || !out_rgb_1x_path.empty() || do_refocus || do_parallax_html;
    if (finish_rgb_canvas) {
        if (cfg.relay_raw && requested_cfg.denoise_strength > 0) {
            heraw_core::HalfStackOptions opts;
            opts.strength = requested_cfg.denoise_strength;
            opts.effective_frames = 1;
            opts.kernel_power = kernel_power.data();
            heraw_core::half_stack_denoise(active_rgb, nullptr, nullptr, active_H, active_W,
                {(float)sig_eff_sq, (float)g_eff}, opts, core_execution);
            heraw_core::color_line_refine(active_rgb, active_H, active_W,
                {(float)sig_eff_sq, (float)g_eff}, 0.25f * requested_cfg.chroma_denoise, core_execution);
        }
        if ((cfg.relay_raw && requested_cfg.psf_iters > 0) || cfg.rgb_psf_iters > 0) {
            std::vector<float> rlg_q, rlt_q;
            const float* rlg_ptr = rl_gain_half.data();
            const float* rlt_ptr = rl_thrmul_half.data();
            int mod_h = Hh, mod_w = Wh;
            if (!need_2x) {
                mod_h = std::max(1, H1 / 4);
                mod_w = std::max(1, W1 / 4);
                rlg_q.assign((size_t)mod_h * mod_w, 1.0f);
                rlt_q.assign((size_t)mod_h * mod_w, 1.0f);
                for (int r = 0; r < mod_h; ++r) {
                    for (int c = 0; c < mod_w; ++c) {
                        float sg = 0.0f, st = 0.0f;
                        for (int dy = 0; dy < 2; ++dy) {
                            const int ry = clamp_i(2 * r + dy, 0, std::max(0, Hh - 1));
                            for (int dx = 0; dx < 2; ++dx) {
                                const int cx = clamp_i(2 * c + dx, 0, std::max(0, Wh - 1));
                                sg += rl_gain_half[(size_t)ry * Wh + cx];
                                st += rl_thrmul_half[(size_t)ry * Wh + cx];
                            }
                        }
                        rlg_q[(size_t)r * mod_w + c] = 0.25f * sg;
                        rlt_q[(size_t)r * mod_w + c] = 0.25f * st;
                    }
                }
                rlg_ptr = rlg_q.data();
                rlt_ptr = rlt_q.data();
            }
            if (cfg.relay_raw && requested_cfg.psf_iters > 0) {
                const int it_use = need_2x ? requested_cfg.psf_iters : std::max(1, (requested_cfg.psf_iters + 1) / 2 + 1);
                const float sig_use = need_2x ? requested_cfg.psf_sigma : std::max(0.42f, 0.56f * requested_cfg.psf_sigma);
                snr_gated_rl_bands(active_rgb, active_H, active_W, it_use, sig_use,
                    0.18f, resid_noise_2x, n_threads, rl_ws, rlg_ptr, rlt_ptr, mod_h, mod_w, is_astro, olpf_mode);
            }
            if (cfg.rgb_psf_iters > 0) {
                const int it_use = need_2x ? cfg.rgb_psf_iters : std::max(1, (cfg.rgb_psf_iters + 1) / 2);
                const float sig_use = need_2x ? (0.88f * (cfg.psf_sigma / 1.05f)) : std::max(0.40f, 0.48f * (cfg.psf_sigma / 1.05f));
                snr_gated_rl_bands(active_rgb, active_H, active_W, it_use, sig_use,
                    0.13f, resid_noise_2x, n_threads, rl_ws, rlg_ptr, rlt_ptr, mod_h, mod_w, is_astro);
            }
        }
        rl_ws.release();
        cost_mark("rgb-rl");

        if (do_refocus) {
            heraw_suite::Image image{active_H, active_W, std::vector<float>(active_rgb, active_rgb + (size_t)active_H * active_W * 3)};
            heraw_suite::RefocusOptions opts;
            opts.focus_depth = cfg.refocus_pos;
            opts.radius_px = need_2x ? cfg.bokeh_strength : (0.5f * cfg.bokeh_strength);
            auto refocused = heraw_suite::render_refocus(image, suite_fields, opts, core_execution.parallel);
            image.rgb.clear(); image.rgb.shrink_to_fit();
            if (!write_extra_image(refocused, "refocus")) return 1;
        }

        if (need_2x && need_1x) {
            rgb_1x.resize((size_t)H1 * (size_t)W1 * 3);
            parallel_for_rows(H1, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    const float* __restrict s0 = rgb_2x.data() + (size_t)(2 * r) * W2 * 3;
                    const float* __restrict s1 = s0 + (size_t)W2 * 3;
                    float* __restrict dst = rgb_1x.data() + (size_t)r * W1 * 3;
                    for (int c = 0; c < W1; ++c) {
                        const int c2 = 2 * c;
                        for (int ch = 0; ch < 3; ++ch) {
                            dst[c * 3 + ch] = 0.25f * (s0[c2 * 3 + ch] + s0[(c2 + 1) * 3 + ch] +
                                                       s1[c2 * 3 + ch] + s1[(c2 + 1) * 3 + ch]);
                        }
                    }
                }
            });
        }

        static constexpr int OETF_LUT_N = 65536;
        std::vector<float> oetf_lut(OETF_LUT_N + 2);
        const bool is_adobe = (cfg.color_space == heraw_io::ColorSpaceMode::ADOBE_RGB);
        for (int i = 0; i <= OETF_LUT_N + 1; ++i) {
            double v = std::min(1.0, (double)i / (double)OETF_LUT_N);
            double s = is_adobe ? std::pow(v, 256.0 / 563.0)
                                : ((v <= 0.0031308) ? (12.92 * v) : (1.055 * std::pow(v, 1.0 / 2.4) - 0.055));
            oetf_lut[i] = (float)s;
        }
        const float* __restrict lut_p = oetf_lut.data();
        auto eval_oetf = [lut_p, is_adobe](float v) -> float {
            if (v <= 0.0f) return 0.0f;
            if (!is_adobe && v <= 0.0031308f) return 12.92f * v;
            if (is_adobe && v < 1.0f / OETF_LUT_N) return std::pow(v, 256.0f / 563.0f);
            if (v >= 1.0f) return 1.0f;
            float f = v * (float)OETF_LUT_N;
            int idx = (int)f;
            float frac = f - (float)idx;
            return lut_p[idx] + frac * (lut_p[idx + 1] - lut_p[idx]);
        };

        auto render_and_write_rgb = [&](const float* src_buf, int cH, int cW, const std::string& dst_path,
                                        double c_sig_sq, double c_g_eff, float c_resid_noise,
                                        bool emit_parallax_here, const std::string& stamp_sfx) -> bool {
            const float m00 = (float)cam2out_d[0][0], m01 = (float)cam2out_d[0][1], m02 = (float)cam2out_d[0][2];
            const float m10 = (float)cam2out_d[1][0], m11 = (float)cam2out_d[1][1], m12 = (float)cam2out_d[1][2];
            const float m20 = (float)cam2out_d[2][0], m21 = (float)cam2out_d[2][1], m22 = (float)cam2out_d[2][2];
            const float gr = cam_wb[0] * exp_gain;
            const float gg = cam_wb[1] * exp_gain;
            const float gb = cam_wb[2] * exp_gain;
            const float astro_ped_scaled = astro_sky_ped * exp_gain;
            const float astro_sub_scaled = astro_sky_sub * exp_gain;
            const int out_H = (cam_flip == 5 || cam_flip == 6) ? cW : cH;
            const int out_W = (cam_flip == 5 || cam_flip == 6) ? cH : cW;
            UninitBuf<uint16_t> out_rgb_u16((size_t)out_H * (size_t)out_W * 3);

            heraw_io::RgbFormat eff_fmt = cfg.explicit_rgb_fmt
                ? cfg.rgb_format
                : infer_rgb_format_from_path(dst_path, cfg.rgb_format);
            const float eff_dither = (eff_fmt == heraw_io::RgbFormat::TIFF_HQ) ? 0.0f : dither_amp;
            const bool do_rolloff = cfg.highlight_rolloff;
            const float asinh_k = astro_asinh_k;
            const float inv_asinh_k = 1.0f / std::asinh(asinh_k);
            const float astro_noise_gate = astro_ped_scaled + 2.8f * std::max(c_resid_noise, astro_sky_iqr_sig) * exp_gain;
            const float n00 = m00 * gr, n01 = m01 * gg, n02 = m02 * gb;
            const float n10 = m10 * gr, n11 = m11 * gg, n12 = m12 * gb;
            const float n20 = m20 * gr, n21 = m21 * gg, n22 = m22 * gb;
            const bool fast_photo_linear = (!is_astro && cam_flip == 0);

            parallel_for_rows(cH, n_threads, [&](int r0, int r1, int) {
                if (fast_photo_linear) {
                    for (int r = r0; r < r1; ++r) {
                        const float* __restrict srow = src_buf + (size_t)r * (size_t)cW * 3;
                        uint16_t* __restrict drow = out_rgb_u16.data() + (size_t)r * (size_t)cW * 3;
                        const uint64_t row_base = (uint64_t)((size_t)r * (size_t)cW) * 4ULL;
                        for (int c = 0; c < cW; ++c) {
                            const float r_in = srow[c * 3 + 0];
                            const float g_in = srow[c * 3 + 1];
                            const float b_in = srow[c * 3 + 2];
                            float lr = std::max(0.0f, n00 * r_in + n01 * g_in + n02 * b_in);
                            float lg = std::max(0.0f, n10 * r_in + n11 * g_in + n12 * b_in);
                            float lb = std::max(0.0f, n20 * r_in + n21 * g_in + n22 * b_in);
                            if (do_rolloff) {
                                const float peak = std::max(lr, std::max(lg, lb));
                                if (peak > 0.78f) {
                                    const float comp = 0.78f + 0.22f * std::tanh((peak - 0.78f) * (1.0f / 0.22f));
                                    const float scale = comp / peak;
                                    lr *= scale; lg *= scale; lb *= scale;
                                    const float desat = clamp_f((peak - 0.85f) * (1.0f / 0.65f), 0.0f, 0.85f);
                                    const float keep = 1.0f - desat, cd = comp * desat;
                                    lr = lr * keep + cd;
                                    lg = lg * keep + cd;
                                    lb = lb * keep + cd;
                                }
                            }
                            const float sr = eval_oetf(lr);
                            const float sg = eval_oetf(lg);
                            const float sb = eval_oetf(lb);
                            if (eff_dither == 0.0f) {
                                drow[c * 3 + 0] = quant_u16(sr * 65535.0f);
                                drow[c * 3 + 1] = quant_u16(sg * 65535.0f);
                                drow[c * 3 + 2] = quant_u16(sb * 65535.0f);
                            } else {
                                const uint64_t s4 = row_base + (uint64_t)c * 4ULL;
                                drow[c * 3 + 0] = quant_u16(sr * 65535.0f + eff_dither * tpdf_dither(s4 + 2ULL));
                                drow[c * 3 + 1] = quant_u16(sg * 65535.0f + eff_dither * tpdf_dither(s4 + 3ULL));
                                drow[c * 3 + 2] = quant_u16(sb * 65535.0f + eff_dither * tpdf_dither(s4 + 4ULL));
                            }
                        }
                    }
                    return;
                }
                for (int r = r0; r < r1; ++r) {
                    for (int c = 0; c < cW; ++c) {
                        size_t idx = (size_t)r * cW + c;
                        float wr = src_buf[idx * 3 + 0] * gr;
                        float wg = src_buf[idx * 3 + 1] * gg;
                        float wb = src_buf[idx * 3 + 2] * gb;

                        float lr = std::max(0.0f, m00 * wr + m01 * wg + m02 * wb);
                        float lg = std::max(0.0f, m10 * wr + m11 * wg + m12 * wb);
                        float lb = std::max(0.0f, m20 * wr + m21 * wg + m22 * wb);

                        if (is_astro && cfg.auto_exposure) {
                            lr = std::max(0.0f, lr - astro_sub_scaled);
                            lg = std::max(0.0f, lg - astro_sub_scaled);
                            lb = std::max(0.0f, lb - astro_sub_scaled);
                            float lum = 0.2126f * lr + 0.7152f * lg + 0.0722f * lb;
                            if (lum > astro_noise_gate + 1e-6f) {
                                float exc = lum - astro_noise_gate;
                                float stretched = astro_noise_gate + (1.0f - astro_noise_gate) * std::asinh(asinh_k * exc) * inv_asinh_k;
                                float s_asinh = stretched / lum;
                                lr *= s_asinh; lg *= s_asinh; lb *= s_asinh;
                            }
                        }

                        if (do_rolloff) {
                            float peak = std::max(lr, std::max(lg, lb));
                            if (peak > 0.78f) {
                                float comp = 0.78f + 0.22f * std::tanh((peak - 0.78f) / 0.22f);
                                float scale = comp / peak;
                                lr *= scale; lg *= scale; lb *= scale;
                                if (!is_astro) {
                                    float desat = clamp_f((peak - 0.85f) / 0.65f, 0.0f, 0.85f);
                                    lr = lr * (1.0f - desat) + comp * desat;
                                    lg = lg * (1.0f - desat) + comp * desat;
                                    lb = lb * (1.0f - desat) + comp * desat;
                                }
                            }
                        }

                        float sr = eval_oetf(lr);
                        float sg = eval_oetf(lg);
                        float sb = eval_oetf(lb);

                        uint16_t qr, qg, qb;
                        if (eff_dither == 0.0f) {
                            qr = quant_u16(sr * 65535.0f);
                            qg = quant_u16(sg * 65535.0f);
                            qb = quant_u16(sb * 65535.0f);
                        } else {
                            qr = quant_u16(sr * 65535.0f + eff_dither * tpdf_dither(idx * 4ULL + 2ULL));
                            qg = quant_u16(sg * 65535.0f + eff_dither * tpdf_dither(idx * 4ULL + 3ULL));
                            qb = quant_u16(sb * 65535.0f + eff_dither * tpdf_dither(idx * 4ULL + 4ULL));
                        }

                        size_t didx;
                        if (cam_flip == 0) {
                            didx = idx * 3;
                        } else {
                            int dr = r, dc = c;
                            if (cam_flip == 3) { dr = cH - 1 - r; dc = cW - 1 - c; }
                            else if (cam_flip == 5) { dr = cW - 1 - c; dc = r; }
                            else if (cam_flip == 6) { dr = c; dc = cH - 1 - r; }
                            didx = ((size_t)dr * (size_t)out_W + (size_t)dc) * 3;
                        }
                        out_rgb_u16[didx + 0] = qr;
                        out_rgb_u16[didx + 1] = qg;
                        out_rgb_u16[didx + 2] = qb;
                    }
                }
            });

            cost_mark("rgb-transform-quantize");
            bool rgb_ok = dst_path.empty();
            std::vector<heraw_io::NamedAlphaPlane> extra_planes;
            if (!out_field_path.empty() && cfg.field_mode == heraw_io::FieldExportMode::TIFF_ALPHA) {
                for (const auto& entry : field_sources) {
                    if (!field_selected(entry.first) || entry.second->empty()) continue;
                    heraw_io::NamedAlphaPlane plane;
                    plane.name = entry.first;
                    const auto* source = entry.second;
                    plane.sample = [&, source, cH, cW](int y, int x) { return sample_output_field(*source, y, x, cH, cW); };
                    extra_planes.push_back(std::move(plane));
                }
            }
            const std::string eff_stamp = sw_stamp.empty() ? std::string() : (stamp_sfx.empty() ? sw_stamp : (sw_stamp + " " + stamp_sfx));
            const double out_sig_sq = c_sig_sq * (double)exp_gain * (double)exp_gain;
            const double out_g_eff = c_g_eff * (double)exp_gain;
            if (!dst_path.empty()) switch (eff_fmt) {
                case heraw_io::RgbFormat::PNG16:
                    rgb_ok = heraw_io::write_png16_rgb(dst_path, out_rgb_u16.data(), out_H, out_W,
                        cfg.color_space, rot_exif, eff_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                    break;
                case heraw_io::RgbFormat::JXS_VIS:
                    rgb_ok = heraw_io::write_jxs_rgb(dst_path, out_rgb_u16.data(), out_H, out_W,
                        false, cfg.jxs_bpp, cfg.color_space, rot_exif, eff_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                    break;
                case heraw_io::RgbFormat::JXS_LOSSLESS:
                    rgb_ok = heraw_io::write_jxs_rgb(dst_path, out_rgb_u16.data(), out_H, out_W,
                        true, cfg.jxs_bpp, cfg.color_space, rot_exif, eff_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                    break;
                case heraw_io::RgbFormat::TIFF_DEFLATE:
                    rgb_ok = heraw_io::write_tiff16_rgb_with_alphas(dst_path, out_rgb_u16.data(), extra_planes, out_H, out_W,
                        true, cfg.color_space, rot_exif, eff_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                    break;
                case heraw_io::RgbFormat::TIFF_HQ:
                    rgb_ok = heraw_io::write_tiff16_rgb_with_alphas(dst_path, out_rgb_u16.data(), extra_planes, out_H, out_W,
                        true, cfg.color_space, rot_exif, eff_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8,
                        true, out_sig_sq, out_g_eff);
                    break;
                case heraw_io::RgbFormat::TIFF_RAW:
                    rgb_ok = heraw_io::write_tiff16_rgb_with_alphas(dst_path, out_rgb_u16.data(), extra_planes, out_H, out_W,
                        false, cfg.color_space, rot_exif, eff_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                    break;
            }
            if (!rgb_ok) {
                log_err("Error writing RGB output to %s\n", dst_path.c_str());
                return false;
            }
            if (emit_parallax_here && do_parallax_html) {
                auto depth = suite_fields.maps.find("depth");
                auto gate = suite_fields.gates.find("depth");
                if (depth != suite_fields.maps.end() && gate != suite_fields.gates.end() && gate->second.observable) {
                    const float scale = std::min(1.0f, 2048.0f / std::max(out_H, out_W));
                    const int ph = std::max(1, (int)(out_H * scale)), pw = std::max(1, (int)(out_W * scale));
                    std::vector<uint8_t> preview((size_t)ph * pw * 3);
                    heraw_suite::FieldResult preview_fields;
                    preview_fields.H = ph; preview_fields.W = pw;
                    preview_fields.gates["depth"] = gate->second;
                    auto& z = preview_fields.maps["depth"]; z.resize((size_t)ph * pw);
                    for (int y = 0; y < ph; ++y) for (int x = 0; x < pw; ++x) {
                        int yy = std::min(out_H - 1, (int)((y + 0.5f) / scale)), xx = std::min(out_W - 1, (int)((x + 0.5f) / scale));
                        size_t p = (size_t)y * pw + x, s = (size_t)yy * out_W + xx;
                        for (int c = 0; c < 3; ++c) preview[p * 3 + c] = (uint8_t)((out_rgb_u16[s * 3 + c] + 128u) / 257u);
                        z[p] = sample_output_field(depth->second, yy, xx, cH, cW) / 65535.0f;
                    }
                    try {
                        ensure_parent_dir_utf8(suite_prefix + "_parallax.html");
                        heraw_suite::write_webgl_html(suite_prefix + "_parallax.html", preview, ph, pw, preview_fields, 24.0f * cfg.parallax_amp);
                    } catch (const std::exception& ex) {
                        log_err("[W6] Parallax HTML error: %s\n", ex.what());
                        if (dst_path.empty()) return false;
                    }
                }
            }
            return true;
        };

        bool parallax_emitted = false;
        if (need_2x && (!out_rgb_path.empty() || (do_parallax_html && !need_1x))) {
            if (!render_and_write_rgb(rgb_2x.data(), H2, W2, out_rgb_path, sig_eff_sq, g_eff, resid_noise_2x, true, "")) {
                return 1;
            }
            parallax_emitted = true;
        }
        rgb_2x.release();
        release_free_heap_pages();

        if (need_1x && (!out_rgb_1x_path.empty() || (do_parallax_html && !parallax_emitted))) {
            const double sig_1x_sq = need_2x ? (0.45 * sig_eff_sq) : sig_eff_sq;
            const double g_1x = need_2x ? (0.45 * g_eff) : g_eff;
            const float rnoise_1x = need_2x ? (0.67f * resid_noise_2x) : resid_noise_2x;
            if (!render_and_write_rgb(rgb_1x.data(), H1, W1, out_rgb_1x_path, sig_1x_sq, g_1x, rnoise_1x, !parallax_emitted, "[1x TrueRGB]")) {
                return 1;
            }
        }
        rgb_1x.release();
        release_free_heap_pages();
    }

    // 6c) On-demand Export: 16-bit Physical Field Map(s)
    cost_mark("rgb-encode");
    size_t exported_field_count = 0;
    if (!out_field_path.empty() && cfg.field_mode == heraw_io::FieldExportMode::TIFF_ALPHA) {
        for (const auto& entry : field_sources) {
            if (field_selected(entry.first) && !entry.second->empty()) ++exported_field_count;
        }
    }
    if (!out_field_path.empty() && cfg.field_mode != heraw_io::FieldExportMode::TIFF_ALPHA) {
        const int fld_cH = (pure_1x_pref && !need_2x) ? H1 : H2;
        const int fld_cW = (pure_1x_pref && !need_2x) ? W1 : W2;
        const float fld_scale_h = (float)Hh / (float)std::max(1, fld_cH);
        int out_H = (cam_flip == 5 || cam_flip == 6) ? fld_cW : fld_cH;
        int out_W = (cam_flip == 5 || cam_flip == 6) ? fld_cH : fld_cW;

        auto derive_part_path = [&](const char* part_sfx, bool single_explicit) -> std::string {
            if (single_explicit && !cfg.field_from_outdir) {
                size_t sl = out_field_path.find_last_of("/\\");
                size_t dt = out_field_path.find_last_of('.');
                if (dt != std::string::npos && (sl == std::string::npos || dt > sl)) return out_field_path;
                return out_field_path + ".png";
            }
            std::string stem = out_field_path;
            size_t sl = stem.find_last_of("/\\");
            size_t dt = stem.find_last_of('.');
            if (dt != std::string::npos && (sl == std::string::npos || dt > sl)) {
                stem = stem.substr(0, dt);
            }
            return stem + "_" + part_sfx + ".png";
        };

        if (cfg.field_mode == heraw_io::FieldExportMode::PACKED_RGB) {
            std::vector<const std::vector<float>*> packed_fields;
            std::vector<std::string> packed_names;
            size_t tok_start = 0;
            do {
                size_t tok_end = cfg.fields_csv.find(',', tok_start);
                std::string req_name = cfg.fields_csv.substr(tok_start, tok_end - tok_start);
                const std::vector<float>* ptr = nullptr;
                if (req_name == "motion") ptr = &r_field_src;
                else if (req_name == "phase") ptr = &field_phase;
                else if (req_name == "focus") ptr = &field_focus;
                else {
                    auto it = field_export_maps.find(req_name);
                    if (it != field_export_maps.end() && !it->second.empty()) ptr = &it->second;
                }
                if (ptr) ++exported_field_count;
                packed_fields.push_back(ptr);
                packed_names.push_back(req_name == "motion" && is_astro_sky_mask ? "sky" :
                                       (ptr ? req_name : req_name + "(unobservable)"));
                if (tok_end == std::string::npos) break;
                tok_start = tok_end + 1;
            } while (true);
            if (exported_field_count > 0) {
                for(size_t group=0;group<packed_fields.size();group+=3) {
                    std::string rgb_fld_path = out_field_path;
                    if (packed_fields.size()>3)
                        rgb_fld_path=derive_part_path(("rgb"+std::to_string(group/3+1)).c_str(),false);
                    size_t sl = rgb_fld_path.find_last_of("/\\");
                    size_t dt = rgb_fld_path.find_last_of('.');
                    if (dt == std::string::npos || (sl != std::string::npos && dt < sl)) rgb_fld_path += ".png";

                    UninitBuf<uint16_t> fld_u16((size_t)out_H * (size_t)out_W * 3);
                    parallel_for_rows(fld_cH, n_threads, [&](int r0, int r1, int) {
                        for (int r = r0; r < r1; ++r) {
                            float yh = ((float)r + 0.5f) * fld_scale_h - 0.5f;
                            for (int c = 0; c < fld_cW; ++c) {
                                float xh = ((float)c + 0.5f) * fld_scale_h - 0.5f;
                                auto sample_channel=[&](size_t ch) {
                                    return (ch<packed_fields.size() && packed_fields[ch]) ?
                                        clamp_f(bilerp_clamped(packed_fields[ch]->data(),Hh,Wh,yh,xh),0,1) : 0.0f;
                                };
                                float fm=sample_channel(group),fp=sample_channel(group+1),ff=sample_channel(group+2);

                                int dr = r, dc = c;
                                if (cam_flip == 3) { dr = fld_cH - 1 - r; dc = fld_cW - 1 - c; }
                                else if (cam_flip == 5) { dr = fld_cW - 1 - c; dc = r; }
                                else if (cam_flip == 6) { dr = c; dc = fld_cH - 1 - r; }

                                size_t didx = ((size_t)dr * (size_t)out_W + (size_t)dc) * 3;
                                fld_u16[didx + 0] = quant_u16(fm * 65535.0f);
                                fld_u16[didx + 1] = quant_u16(fp * 65535.0f);
                                fld_u16[didx + 2] = quant_u16(ff * 65535.0f);
                            }
                        }
                    });
                    std::string channels;
                    for(size_t c=0;c<3;++c) {
                        channels+=std::string(c?" ":"")+std::string(1,"RGB"[c])+":"+
                            (group+c<packed_names.size()?packed_names[group+c]:"padding");
                    }
                    if (!heraw_io::write_png16_rgb(rgb_fld_path, fld_u16.data(), out_H, out_W,
                            heraw_io::ColorSpaceMode::SRGB, rot_exif,
                            sw_stamp.empty() ? std::string() : (sw_stamp + " [FieldMap "+channels+"]"),
                            n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8, true)) {
                        log_err("Error writing packed RGB Field Map PNG to %s\n", rgb_fld_path.c_str());
                        return 1;
                    }
                }
            }
        } else {
            UninitBuf<uint16_t> gray_u16((size_t)out_H * (size_t)out_W);
            auto emit_one_gray = [&](const float* src_half, const char* sfx, const char* tag_desc, bool single_exp) -> bool {
                std::string dst_p = derive_part_path(sfx, single_exp);
                parallel_for_rows(fld_cH, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        float yh = ((float)r + 0.5f) * fld_scale_h - 0.5f;
                        for (int c = 0; c < fld_cW; ++c) {
                            float xh = ((float)c + 0.5f) * fld_scale_h - 0.5f;
                            float fv = clamp_f(bilerp_clamped(src_half, Hh, Wh, yh, xh), 0.0f, 1.0f);
                            int dr = r, dc = c;
                            if (cam_flip == 3) { dr = fld_cH - 1 - r; dc = fld_cW - 1 - c; }
                            else if (cam_flip == 5) { dr = fld_cW - 1 - c; dc = r; }
                            else if (cam_flip == 6) { dr = c; dc = fld_cH - 1 - r; }
                            gray_u16[(size_t)dr * (size_t)out_W + (size_t)dc] = quant_u16(fv * 65535.0f);
                        }
                    }
                });
                if (!heraw_io::write_png16_gray(dst_p, gray_u16.data(), out_H, out_W,
                        rot_exif, sw_stamp.empty() ? std::string() : (sw_stamp + " " + tag_desc),
                        n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8)) {
                    log_err("Error writing Grayscale Field Map PNG to %s\n", dst_p.c_str());
                    return false;
                }
                ++exported_field_count;
                return true;
            };

            const char* mot_sfx = is_astro_sky_mask ? "sky" : "motion";
            const char* mot_tag = is_astro_sky_mask ? "[FieldMap: SkyHorizonMask]" : "[FieldMap: MotionMatte]";
            const bool is_split = (cfg.field_mode == heraw_io::FieldExportMode::SPLIT_GRAY);
            if ((is_split && field_selected("motion")) || cfg.field_mode == heraw_io::FieldExportMode::ONLY_MOTION) {
                if (!emit_one_gray(r_field_src.data(), mot_sfx, mot_tag, !is_split)) return 1;
            }
            if ((is_split && field_selected("phase")) || cfg.field_mode == heraw_io::FieldExportMode::ONLY_PHASE) {
                if (!emit_one_gray(field_phase.data(), "phase", "[FieldMap: PhaseSR]", !is_split)) return 1;
            }
            if ((is_split && field_selected("focus")) || cfg.field_mode == heraw_io::FieldExportMode::ONLY_FOCUS) {
                if (!emit_one_gray(field_focus.data(), "focus", "[FieldMap: OpticalFocus]", !is_split)) return 1;
            }
            if (is_split) {
                for (const auto& entry : field_sources) {
                    if (entry.first == "motion" || entry.first == "phase" || entry.first == "focus" ||
                        !field_selected(entry.first) || entry.second->empty()) continue;
                    if (!emit_one_gray(entry.second->data(), entry.first.c_str(), entry.first.c_str(), false)) return 1;
                }
            }
        }
    }

    const std::string& rep_rgb_path = !out_rgb_path.empty() ? out_rgb_path : out_rgb_1x_path;
    {
        char msg6[256];
        double dt6 = elapsed_s(t0);
        double wall = elapsed_s(t_wall0);
        std::snprintf(msg6, sizeof(msg6),
            "[6/6] Exported outputs (%s, gain=%.2f, %+.2f EV) in %.2fs | WALL: %.2fs",
            (cfg.color_space == heraw_io::ColorSpaceMode::ADOBE_RGB) ? "AdobeRGB" : "sRGB",
            exp_gain, baseline_ev, dt6, wall);
        emit_stage(6, 100, dt6, msg6);
        if (cfg.progress_json) {
            log_err(
                "{\"event\":\"group_done\",\"group\":%d,\"total_groups\":%d,\"wall_s\":%.3f,"
                "\"eta\":%.5f,\"gain\":%.3f,\"ev\":%.3f,\"tiles\":%d,\"peak_tile_pixels\":%zu,"
                "\"out_raw\":\"%s\",\"out_rgb\":\"%s\",\"out_rgb_1x\":\"%s\"}\n",
                group_idx_1based, total_groups, wall, eta_eff, exp_gain, baseline_ev, compute_tiles, compute_peak_pixels,
                json_escape(out_raw_path).c_str(), json_escape(rep_rgb_path).c_str(), json_escape(out_rgb_1x_path).c_str());
            std::fflush(stderr);
        }
    }
    const bool no_primary_images = out_raw_path.empty() && out_rgb_path.empty() && out_rgb_1x_path.empty() &&
        cfg.dataset_dir.empty() && cfg.optics_report.empty() && cfg.export_lcp.empty();
    if (!out_field_path.empty() && exported_field_count == 0 && no_primary_images &&
        !do_refocus && !do_parallax_html && extra_images.empty()) {
        return 3;
    }
    if ((depth_extra_skipped || aux_extra_skipped) && no_primary_images && out_field_path.empty() &&
        !do_refocus && !do_parallax_html && extra_images.empty()) {
        return 3;
    }
    return 0;
}

static std::string add_group_suffix(const std::string& path, int g_idx1, int g_total) {
    if (path.empty() || g_total <= 1) return path;
    size_t dot = path.find_last_of('.');
    size_t sl = path.find_last_of("/\\");
    char suf[24];
    std::snprintf(suf, sizeof(suf), "_grp%02d", g_idx1);
    if (dot != std::string::npos && (sl == std::string::npos || dot > sl)) {
        return path.substr(0, dot) + suf + path.substr(dot);
    }
    return path + suf;
}

static bool is_bool_token(const std::string& v, bool& out_val) {
    if (v == "1" || v == "on" || v == "true" || v == "yes" || v == "ON" || v == "TRUE") { out_val = true; return true; }
    if (v == "0" || v == "off" || v == "false" || v == "no" || v == "OFF" || v == "FALSE") { out_val = false; return true; }
    return false;
}

static void unpack_frame_batch(
    std::vector<UnpackedFrame>& uframes, const std::vector<std::string>& paths, int max_concurrency = 0,
    heraw_wvdc::RawDecoderPref decoder_pref = heraw_wvdc::RawDecoderPref::AUTO
) {
    const int n_total = (int)paths.size();
    uframes.resize(n_total);
    if (n_total <= 0) return;
    const int hw = (int)std::max(1u, std::thread::hardware_concurrency());
    const int tot_threads = (max_concurrency > 0) ? max_concurrency : hw;
    const int n_workers = std::max(1, std::min(n_total, tot_threads));
    const int inner_t = std::max(1, tot_threads / n_workers);
    for (int i = 0; i < n_total; ++i) {
        uframes[i].path = paths[i];
        uframes[i].decoder_pref = decoder_pref;
        uframes[i].inner_threads = inner_t;
    }
    std::atomic<int> next_idx{0};
#ifdef _WIN32
    std::vector<std::thread> workers;
    workers.reserve(n_workers);
    for (int w = 0; w < n_workers; ++w) {
        workers.emplace_back([&, w]() {
            g_row_pool.apply_thread_slot(w);
            while (true) {
                int idx = next_idx.fetch_add(1, std::memory_order_relaxed);
                if (idx >= n_total) break;
                unpack_one_frame(&uframes[idx]);
            }
        });
    }
    for (auto& th : workers) th.join();
#else
    std::atomic<int> next_slot{0};
    UnpackBatchCtx ctx{&uframes, &next_idx, &next_slot, n_total};
    std::vector<pthread_t> tids(n_workers);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32 * 1024 * 1024);
    for (int w = 0; w < n_workers; ++w) {
        pthread_create(&tids[w], &attr, unpack_pthread_trampoline, &ctx);
    }
    pthread_attr_destroy(&attr);
    for (int w = 0; w < n_workers; ++w) pthread_join(tids[w], nullptr);
#endif
}

} // namespace

static int run_cli_impl(int argc, char** argv) {
    auto t_wall0 = std::chrono::steady_clock::now();
    auto elapsed_s = [&](auto t0) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    PipelineConfig cfg;
    int explicit_threads = 0;
    bool explicit_cpu_cores = false;
    int battery_device_mode = -1; // -1 = auto-detect, 0 = non-battery, 1 = battery device
    cfg.n_threads = (int)std::max(1u, std::thread::hardware_concurrency());
    apply_preset_defaults(cfg, PipelinePreset::PHOTO);

    std::string out_raw_path, out_rgb_path, out_field_path, out_dir_path, custom_libraw_path;
    std::string dec_jxs_in, dec_jxs_out;
    bool emit_raw_in_dir = false, emit_rgb_in_dir = false, emit_rgb_1x_in_dir = false, emit_field_in_dir = false;
    bool inspect_only = false;
    bool plan_json = false;
    bool single_group = false;
    bool recursive_scan = false;
    std::vector<std::string> scan_dirs, raw_pos_args;

    // Deferred explicit knob overrides so --preset can appear anywhere on the command line
    bool ov_den = false, ov_chr = false, ov_psfi = false, ov_rgbpsfi = false, ov_psfs = false;
    bool ov_degh = false, ov_ae = false, ov_hl = false, ov_ep = false, ov_dith = false, ov_mrs = false;
    float v_den = 1.0f, v_chr = 1.0f, v_psfs = 1.05f, v_degh = 1.0f, v_mrs = 0.72f;
    int v_psfi = 4, v_rgbpsfi = 2;
    bool v_ae = true, v_hl = true, v_ep = true, v_dith = true;

    std::vector<std::string> cli_args;
#ifdef _WIN32
    {
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (wargv) {
            for (int i = 1; i < wargc; ++i) {
                int len = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) {
                    std::string s(len - 1, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, &s[0], len, nullptr, nullptr);
                    cli_args.push_back(std::move(s));
                } else {
                    cli_args.emplace_back();
                }
            }
            LocalFree(wargv);
        }
    }
    (void)argc;
#else
    for (int i = 1; i < argc; ++i) cli_args.emplace_back(argv[i]);
#endif

    // Pre-scan --log-file so even early CLI validation errors are mirrored into the requested log file
    for (size_t i = 0; i + 1 < cli_args.size(); ++i) {
        if (cli_args[i] == "--log-file") {
            cfg.log_file_path = cli_args[i + 1];
            if (!g_diag_log.open(cfg.log_file_path, cli_args)) {
                std::fprintf(stderr, "Error: unable to open --log-file '%s'\n", cfg.log_file_path.c_str());
                return 1;
            }
            break;
        }
    }

    for (size_t i = 0; i < cli_args.size(); ++i) {
        const std::string& a = cli_args[i];
        auto next_s = [&]() -> std::string { return (i + 1 < cli_args.size()) ? cli_args[++i] : std::string(); };
        auto parse_opt_bool = [&](bool& dst_val) -> bool {
            if (i + 1 >= cli_args.size()) { dst_val = true; return true; }
            const std::string& nx = cli_args[i + 1];
            if (is_bool_token(nx, dst_val)) { ++i; return true; }
            if (!nx.empty() && nx[0] == '-') { dst_val = true; return true; }
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::u8path(nx), ec)) { dst_val = true; return true; }
            log_err("Error: option %s expects on|off (got '%s')\n", a.c_str(), nx.c_str());
            return false;
        };
        if (a == "--out-raw" && i + 1 < cli_args.size()) out_raw_path = next_s();
        else if (a == "--out-rgb" && i + 1 < cli_args.size()) out_rgb_path = next_s();
        else if (a == "--out-rgb-1x" && i + 1 < cli_args.size()) cfg.out_rgb_1x_path = next_s();
        else if (a == "--out-field" && i + 1 < cli_args.size()) out_field_path = next_s();
        else if (a == "--out-dir" && i + 1 < cli_args.size()) out_dir_path = next_s();
        else if (a == "--log-file" && i + 1 < cli_args.size()) { cfg.log_file_path = next_s(); }
        else if (a == "--emit-raw") emit_raw_in_dir = true;
        else if (a == "--emit-rgb") emit_rgb_in_dir = true;
        else if (a == "--emit-rgb-1x") emit_rgb_1x_in_dir = true;
        else if (a == "--emit-field") emit_field_in_dir = true;
        else if ((a == "--rgb-scale" || a == "--scale") && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "2x" || v == "2") cfg.rgb_scale_mode = 2;
            else if (v == "1x" || v == "1") cfg.rgb_scale_mode = 1;
            else if (v == "both" || v == "1x+2x" || v == "2x+1x") cfg.rgb_scale_mode = 3;
            else { log_err("Error: unknown --rgb-scale '%s' (expected: 2x | 1x | both)\n", v.c_str()); return 2; }
        }
        else if (a == "--scan-dir" && i + 1 < cli_args.size()) scan_dirs.push_back(next_s());
        else if (a == "--recursive") recursive_scan = true;
        else if (a == "--sort-time") cfg.sort_time = true;
        else if (a == "--no-sort-time") cfg.sort_time = false;
        else if (a == "--libraw" && i + 1 < cli_args.size()) custom_libraw_path = next_s();
        else if (a == "--decoder" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto") cfg.decoder_pref = heraw_wvdc::RawDecoderPref::AUTO;
            else if (v == "wvdc" || v == "nijinwvdc") {
#ifndef HERAW_HAS_WVDC
                log_err("Error: this binary was built without nijinWVDC support.\n");
                return 2;
#endif
                cfg.decoder_pref = heraw_wvdc::RawDecoderPref::WVDC;
            } else if (v == "libraw") cfg.decoder_pref = heraw_wvdc::RawDecoderPref::LIBRAW;
            else {
                log_err("Error: unknown --decoder '%s' (expected: auto | wvdc | libraw)\n", v.c_str());
                return 2;
            }
        }
        else if (a == "--nev-frames" && i + 1 < cli_args.size()) cfg.nev_frames_spec = next_s();
        else if (a == "--no-dark") { /* legacy no-op: dark-frame-free is always active */ }
        else if (a == "--motion-mode" && i + 1 < cli_args.size()) cfg.motion_mode = next_s();
        else if (a == "--olpf-mode" && i + 1 < cli_args.size()) cfg.olpf_mode = next_s();
        else if (a == "--fields" && i + 1 < cli_args.size()) cfg.fields_csv = next_s();
        else if (a == "--reflection-mode" && i + 1 < cli_args.size()) cfg.reflection_mode = next_s();
        else if (a == "--transient-color" && i + 1 < cli_args.size()) cfg.transient_color = next_s();
        else if (a == "--relay-raw") { if (!parse_opt_bool(cfg.relay_raw)) return 2; }
        else if (a == "--no-tool-exif") { if (!parse_opt_bool(cfg.no_tool_exif)) return 2; }
        else if (a == "--silicon-clean") { if (!parse_opt_bool(cfg.silicon_clean)) return 2; }
        else if (a == "--auto-lca") { if (!parse_opt_bool(cfg.auto_lca)) return 2; }
        else if (a == "--ibp-iters" && i + 1 < cli_args.size()) cfg.ibp_iters = clamp_i(std::atoi(next_s().c_str()), 0, 2);
        else if (a == "--clean-sens" && i + 1 < cli_args.size()) cfg.clean_sens = clamp_f(std::atof(next_s().c_str()), 0.1f, 5.0f);
        else if (a == "--vnd-thresh" && i + 1 < cli_args.size()) cfg.vnd_thresh = clamp_f(std::atof(next_s().c_str()), 0.0f, 1.0f);
        else if (a == "--strobe-step" && i + 1 < cli_args.size()) cfg.strobe_step = std::max(1, std::atoi(next_s().c_str()));
        else if (a == "--strobe-fade" && i + 1 < cli_args.size()) cfg.strobe_fade = clamp_f(std::atof(next_s().c_str()), 0.0f, 1.0f);
        else if (a == "--transient-sigma" && i + 1 < cli_args.size()) cfg.transient_sigma = clamp_f(std::atof(next_s().c_str()), 0.5f, 20.0f);
        else if (a == "--emit-transient") cfg.emit_transient = true;
        else if (a == "--export-parallax-html" || a == "--export-parallax") cfg.export_parallax_html = true;
        else if (a == "--parallax-amp" && i + 1 < cli_args.size()) cfg.parallax_amp = clamp_f(std::atof(next_s().c_str()), 0.0f, 10.0f);
        else if (a == "--export-refocus") cfg.export_refocus = true;
        else if (a == "--refocus-pos" && i + 1 < cli_args.size()) cfg.refocus_pos = clamp_f(std::atof(next_s().c_str()), 0.0f, 1.0f);
        else if (a == "--bokeh-strength" && i + 1 < cli_args.size()) cfg.bokeh_strength = clamp_f(std::atof(next_s().c_str()), 0.0f, 64.0f);
        else if (a == "--optics-only") cfg.optics_only = true;
        else if (a == "--optics-report" && i + 1 < cli_args.size()) cfg.optics_report = next_s();
        else if (a == "--export-lcp" && i + 1 < cli_args.size()) cfg.export_lcp = next_s();
        else if (a == "--lcp-fields" && i + 1 < cli_args.size()) cfg.lcp_fields = next_s();
        else if (a == "--export-dataset-tiles" && i + 1 < cli_args.size()) cfg.dataset_dir = next_s();
        else if (a == "--tile-size" && i + 1 < cli_args.size()) cfg.tile_size = std::atoi(next_s().c_str());
        else if (a == "--tile-min-conf" && i + 1 < cli_args.size()) cfg.tile_min_conf = clamp_f(std::atof(next_s().c_str()), 0.0f, 1.0f);
        else if (a == "--export-topk" && i + 1 < cli_args.size()) cfg.export_topk = std::max(1, std::atoi(next_s().c_str()));
        else if (a == "--skip-frames" && i + 1 < cli_args.size()) {
            std::string csv = next_s();
            size_t start = 0;
            do {
                size_t end = csv.find(',', start);
                std::string token = csv.substr(start, end - start);
                char* stop = nullptr;
                long idx = std::strtol(token.c_str(), &stop, 10);
                if (token.empty() || *stop || idx < 1 || idx > 1000000) {
                    log_err("Error: --skip-frames expects 1-based indices.\n"); return 2;
                }
                cfg.skip_frames.push_back((int)idx);
                if (end == std::string::npos) break;
                start = end + 1;
            } while (true);
        }
        else if (a == "--policy" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto-prune") cfg.policy = heraw_plan::ExecPolicy::AUTO_PRUNE;
            else if (v == "multi-group") cfg.policy = heraw_plan::ExecPolicy::MULTI_GROUP;
            else if (v == "force-all") cfg.policy = heraw_plan::ExecPolicy::FORCE_ALL;
            else if (v == "interactive") cfg.policy = heraw_plan::ExecPolicy::INTERACTIVE;
            else { log_err("Error: unknown --policy '%s'\n", v.c_str()); return 2; }
        } else if (a == "--interactive-choice" && i + 1 < cli_args.size()) {
            cfg.interactive_choice = std::atoi(next_s().c_str());
        } else if (a == "--group-size" && i + 1 < cli_args.size()) {
            cfg.group_size = std::max(3, std::atoi(next_s().c_str()));
        } else if (a == "--max-frames" && i + 1 < cli_args.size()) {
            cfg.max_frames = std::max(0, std::atoi(next_s().c_str()));
        } else if (a == "--burst-gap-s" && i + 1 < cli_args.size()) {
            cfg.burst_gap_s = std::max(0.2, std::atof(next_s().c_str()));
        } else if (a == "--raw-format" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "dng" || v == "dng-lossless") cfg.raw_mode = heraw_io::RawDngMode::DNG_LOSSLESS;
            else if (v == "dng-linear") cfg.raw_mode = heraw_io::RawDngMode::DNG_LINEAR;
            else if (v == "dng-hq" || v == "dng-lossy") cfg.raw_mode = heraw_io::RawDngMode::DNG_HQ;
            else if (v == "dng-fp16") cfg.raw_mode = heraw_io::RawDngMode::DNG_FP16;
            else if (v == "dng-raw") cfg.raw_mode = heraw_io::RawDngMode::DNG_RAW;
            else { log_err("Error: unknown --raw-format '%s'\n", v.c_str()); return 2; }
        } else if (a == "--rgb-format" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            cfg.explicit_rgb_fmt = true;
            if (v == "png" || v == "png16") cfg.rgb_format = heraw_io::RgbFormat::PNG16;
            else if (v == "jxs") cfg.rgb_format = heraw_io::RgbFormat::JXS_VIS;
            else if (v == "jxs-lossless") cfg.rgb_format = heraw_io::RgbFormat::JXS_LOSSLESS;
            else if (v == "tiff-deflate" || v == "tiff") cfg.rgb_format = heraw_io::RgbFormat::TIFF_DEFLATE;
            else if (v == "tiff-hq" || v == "tiff-lossy") cfg.rgb_format = heraw_io::RgbFormat::TIFF_HQ;
            else if (v == "tiff-raw") cfg.rgb_format = heraw_io::RgbFormat::TIFF_RAW;
            else { log_err("Error: unknown --rgb-format '%s'\n", v.c_str()); return 2; }
        } else if (a == "--jxs-bpp" && i + 1 < cli_args.size()) {
            cfg.jxs_bpp = clamp_f((float)std::atof(next_s().c_str()), 3.0f, 28.0f);
        } else if (a == "--field-mode" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "split" || v == "3" || v == "gray" || v == "separate") cfg.field_mode = heraw_io::FieldExportMode::SPLIT_GRAY;
            else if (v == "tiff-alpha") cfg.field_mode = heraw_io::FieldExportMode::TIFF_ALPHA;
            else if (v == "rgb" || v == "packed" || v == "single") cfg.field_mode = heraw_io::FieldExportMode::PACKED_RGB;
            else if (v == "motion" || v == "sky") cfg.field_mode = heraw_io::FieldExportMode::ONLY_MOTION;
            else if (v == "phase") cfg.field_mode = heraw_io::FieldExportMode::ONLY_PHASE;
            else if (v == "focus") cfg.field_mode = heraw_io::FieldExportMode::ONLY_FOCUS;
            else { log_err("Error: unknown --field-mode '%s' (expected: split | rgb | tiff-alpha | motion | phase | focus)\n", v.c_str()); return 2; }
        } else if (a == "--color-space" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "srgb" || v == "sRGB") cfg.color_space = heraw_io::ColorSpaceMode::SRGB;
            else if (v == "adobergb" || v == "adobe-rgb" || v == "AdobeRGB") cfg.color_space = heraw_io::ColorSpaceMode::ADOBE_RGB;
            else { log_err("Error: unknown --color-space '%s'\n", v.c_str()); return 2; }
        } else if (a == "--preset" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "photo") apply_preset_defaults(cfg, PipelinePreset::PHOTO);
            else if (v == "sci" || v == "training" || v == "ml") apply_preset_defaults(cfg, PipelinePreset::SCI);
            else if (v == "arc" || v == "archival") apply_preset_defaults(cfg, PipelinePreset::ARC);
            else if (v == "astro" || v == "night" || v == "star") apply_preset_defaults(cfg, PipelinePreset::ASTRO);
            else { log_err("Error: unknown --preset '%s' (expected: photo | sci | arc | astro)\n", v.c_str()); return 2; }
        } else if (a == "--wizard") {
            cfg.wizard_mode = true;
            cfg.policy = heraw_plan::ExecPolicy::INTERACTIVE;
        } else if (a == "--anchor-frame" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto" || v == "sharpest") cfg.anchor_mode = 0;
            else if (v == "first") cfg.anchor_mode = -1;
            else if (v == "middle" || v == "mid") cfg.anchor_mode = -2;
            else if (std::atoi(v.c_str()) >= 1) cfg.anchor_mode = std::atoi(v.c_str());
            else { log_err("Error: unknown --anchor-frame '%s' (expected: auto | first | middle | <1..N>)\n", v.c_str()); return 2; }
        } else if (a == "--backend" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto") cfg.backend = heraw_gpu::BackendPref::AUTO;
            else if (v == "gpu" || v == "metal" || v == "vulkan") cfg.backend = heraw_gpu::BackendPref::GPU;
            else if (v == "cpu") cfg.backend = heraw_gpu::BackendPref::CPU;
            else { log_err("Error: unknown --backend '%s' (expected: auto | gpu | cpu)\n", v.c_str()); return 2; }
        } else if (a == "--denoise-strength" && i + 1 < cli_args.size()) { ov_den = true; v_den = clamp_f((float)std::atof(next_s().c_str()), 0.0f, 2.5f); }
        else if (a == "--chroma-denoise" && i + 1 < cli_args.size()) { ov_chr = true; v_chr = clamp_f((float)std::atof(next_s().c_str()), 0.0f, 2.5f); }
        else if (a == "--psf-iters" && i + 1 < cli_args.size()) { ov_psfi = true; v_psfi = clamp_i(std::atoi(next_s().c_str()), 0, 12); }
        else if (a == "--rgb-psf-iters" && i + 1 < cli_args.size()) { ov_rgbpsfi = true; v_rgbpsfi = clamp_i(std::atoi(next_s().c_str()), 0, 8); }
        else if (a == "--psf-sigma" && i + 1 < cli_args.size()) { ov_psfs = true; v_psfs = clamp_f((float)std::atof(next_s().c_str()), 0.35f, 3.0f); }
        else if (a == "--deghost-strength" && i + 1 < cli_args.size()) { ov_degh = true; v_degh = clamp_f((float)std::atof(next_s().c_str()), 0.4f, 3.0f); }
        else if (a == "--auto-exposure") { ov_ae = true; if (!parse_opt_bool(v_ae)) return 2; }
        else if (a == "--highlight-rolloff") { ov_hl = true; if (!parse_opt_bool(v_hl)) return 2; }
        else if (a == "--edge-polish") { ov_ep = true; if (!parse_opt_bool(v_ep)) return 2; }
        else if (a == "--dither") { ov_dith = true; if (!parse_opt_bool(v_dith)) return 2; }
        else if (a == "--ev-offset" && i + 1 < cli_args.size()) cfg.ev_offset = clamp_f((float)std::atof(next_s().c_str()), -4.0f, 4.0f);
        else if (a == "--min-rel-sharpness" && i + 1 < cli_args.size()) { ov_mrs = true; v_mrs = clamp_f((float)std::atof(next_s().c_str()), 0.1f, 0.99f); }
        else if (a == "--max-shift-px" && i + 1 < cli_args.size()) cfg.max_shift_px = clamp_f((float)std::atof(next_s().c_str()), 8.0f, 800.0f);
        else if (a == "--threads" && i + 1 < cli_args.size()) {
            int threads = std::atoi(next_s().c_str());
            explicit_threads = threads > 0 ? threads : 0;
        }
        else if (a == "--cpu-cores" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            bool ok = false;
            cfg.cpu_cores = heraw_cache::parse_core_policy(v, &ok);
            if (!ok) {
                log_err("Error: unknown --cpu-cores '%s' (expected: auto | perf | physical | eff | all)\n", v.c_str());
                return 2;
            }
            explicit_cpu_cores = true;
        }
        else if (a == "--battery-device" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto") battery_device_mode = -1;
            else if (v == "on" || v == "1" || v == "true") battery_device_mode = 1;
            else if (v == "off" || v == "0" || v == "false") battery_device_mode = 0;
            else {
                log_err("Error: unknown --battery-device '%s' (expected: auto | on | off)\n", v.c_str());
                return 2;
            }
        }
        else if (a == "--priority" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            bool ok = false;
            cfg.priority = heraw_cache::parse_task_priority(v, &ok);
            if (!ok) {
                log_err("Error: unknown --priority '%s' (expected: normal | high | below | idle)\n", v.c_str());
                return 2;
            }
        }
        else if (a == "--simd" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            bool ok = false;
            cfg.simd = heraw_cache::parse_simd_policy(v, &ok);
            if (!ok) {
                log_err("Error: unknown --simd '%s' (expected: auto | avx512 | avx2)\n", v.c_str());
                return 2;
            }
        }
        else if (a == "--inspect" || a == "--inspect-af") inspect_only = true;
        else if (a == "--plan-json") plan_json = true;
        else if (a == "--single-group") single_group = true;
        else if (a == "--progress-json") cfg.progress_json = true;
        else if (a == "--decode-jxs" && i + 2 < cli_args.size()) {
            dec_jxs_in = next_s();
            dec_jxs_out = next_s();
        } else if (a == "-v" || a == "--version") {
            auto ginfo = heraw_gpu::probe_gpu_device();
            std::string gpu_tag = ginfo.available ? (ginfo.api_name + ": " + ginfo.device_name) : "CPU-only";
            if (!custom_libraw_path.empty()) {
                std::string lerr;
                if (!heraw_lr::global_dyn_libraw().load(custom_libraw_path, lerr)) {
                    log_err("Error loading --libraw: %s\n", lerr.c_str());
                    return 1;
                }
                std::fprintf(stdout, "NijiN.LiteBurstSuite (nijinLBS) " NIJIN_VERSION " -- RAW Imaging Suite (Custom LibRaw %s [%s] | %s)\n",
                    heraw_lr::global_dyn_libraw().ver_str.c_str(), custom_libraw_path.c_str(), gpu_tag.c_str());
            } else {
                std::fprintf(stdout, "NijiN.LiteBurstSuite (nijinLBS) " NIJIN_VERSION " -- RAW Imaging Suite (Built-in LibRaw 0.23.0-Snapshot202611 | %s)\n",
                    gpu_tag.c_str());
            }
            return 0;
        } else if (a == "-h" || a == "--help") {
            print_cli_help(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] != '-') {
            raw_pos_args.push_back(a);
        } else {
            log_err("Error: unknown or incomplete option: %s (run --help for usage)\n", a.c_str());
            return 2;
        }
    }

    if (!custom_libraw_path.empty()) {
        std::string lerr;
        if (!heraw_lr::global_dyn_libraw().load(custom_libraw_path, lerr)) {
            log_err("Error loading custom LibRaw library: %s\n", lerr.c_str());
            return 1;
        }
    }

    if (ov_den) cfg.denoise_strength = v_den;
    if (ov_chr) cfg.chroma_denoise = v_chr;
    if (ov_psfi) cfg.psf_iters = v_psfi;
    if (ov_rgbpsfi) cfg.rgb_psf_iters = v_rgbpsfi;
    if (ov_psfs) cfg.psf_sigma = v_psfs;
    if (ov_degh) cfg.deghost_strength = v_degh;
    if (ov_ae) cfg.auto_exposure = v_ae;
    if (ov_hl) { cfg.highlight_rolloff = v_hl; cfg.glint_desat = v_hl && (cfg.preset != PipelinePreset::ASTRO); }
    if (ov_ep) cfg.edge_polish = v_ep;
    if (ov_dith) cfg.dither = v_dith;
    if (ov_mrs) cfg.min_rel_sharpness = v_mrs;

    {
        const auto& host_prof = heraw_cache::host_cache_profile();
        const auto pwr = heraw_cache::query_power_status();
#if !defined(__APPLE__)
        const bool is_bat_dev = (battery_device_mode >= 0) ? (battery_device_mode > 0) : pwr.has_battery;
        if (is_bat_dev && !explicit_cpu_cores && explicit_threads <= 0) {
            cfg.cpu_cores = heraw_cache::CpuCorePolicy::PHYSICAL_ONLY;
        }
        const bool drop_smt_dc = !explicit_cpu_cores && (is_bat_dev || pwr.on_battery);
#else
        (void)explicit_cpu_cores;
        (void)battery_device_mode;
        const bool drop_smt_dc = pwr.on_battery;
#endif
        cfg.n_threads = heraw_cache::recommended_thread_count(host_prof, cfg.cpu_cores, explicit_threads, drop_smt_dc);
        heraw_cache::runtime_exec_config().core_policy = cfg.cpu_cores;
        heraw_cache::runtime_exec_config().priority = cfg.priority;
        heraw_cache::runtime_exec_config().simd_policy = cfg.simd;
        heraw_cache::refresh_avx512_state();
        heraw_cache::apply_process_priority(cfg.priority);
        g_row_pool.configure(cfg.cpu_cores, cfg.priority, cfg.n_threads, drop_smt_dc);
    }

    // Standalone .jxs -> 16-bit PNG/TIFF decode utility
    if (!dec_jxs_in.empty()) {
        std::vector<uint16_t> rgb16;
        int dH = 0, dW = 0;
        heraw_io::ColorSpaceMode dcs = heraw_io::ColorSpaceMode::SRGB;
        if (!heraw_io::decode_jxs_file_to_rgb16(dec_jxs_in, rgb16, dH, dW, dcs, cfg.n_threads)) {
            log_err("Error: failed to decode JPEG XS file: %s\n", dec_jxs_in.c_str());
            return 1;
        }
        heraw_io::ExifBundle empty_ex;
        auto par_wrap = [](int tot, int nt, auto&& fn, int chk) {
            parallel_for_rows(tot, nt, std::forward<decltype(fn)>(fn), chk);
        };
        auto fmt = cfg.explicit_rgb_fmt ? cfg.rgb_format : infer_rgb_format_from_path(dec_jxs_out, heraw_io::RgbFormat::TIFF_DEFLATE);
        const std::string dec_stamp = cfg.no_tool_exif
            ? std::string()
            : std::string("NijiN.LiteBurstSuite (nijinLBS) " NIJIN_VERSION " JXS decoder");
        bool ok = (fmt == heraw_io::RgbFormat::TIFF_DEFLATE || fmt == heraw_io::RgbFormat::TIFF_HQ || fmt == heraw_io::RgbFormat::TIFF_RAW)
            ? heraw_io::write_tiff16_rgb_ex(dec_jxs_out, rgb16.data(), dH, dW, fmt != heraw_io::RgbFormat::TIFF_RAW,
                                            dcs, empty_ex, dec_stamp,
                                            cfg.n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8,
                                            fmt == heraw_io::RgbFormat::TIFF_HQ)
            : heraw_io::write_png16_rgb(dec_jxs_out, rgb16.data(), dH, dW,
                                        dcs, empty_ex, dec_stamp, cfg.n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
        if (!ok) {
            log_err("Error: failed to write decoded image to %s\n", dec_jxs_out.c_str());
            return 1;
        }
        log_err("Decoded %s (%dx%d) -> %s in %.2fs\n", dec_jxs_in.c_str(), dW, dH, dec_jxs_out.c_str(), elapsed_s(t_wall0));
        return 0;
    }

    if (cfg.motion_mode == "auto" || cfg.motion_mode == "first" || cfg.motion_mode == "middle") {
        cfg.anchor_mode = cfg.motion_mode == "first" ? -1 : (cfg.motion_mode == "middle" ? -2 : 0);
        cfg.motion_mode = "standard";
    }
    auto one_of = [](const std::string& value, std::initializer_list<const char*> choices) {
        for (const auto* choice : choices) if (value == choice) return true;
        return false;
    };
    if (!one_of(cfg.motion_mode, {"standard", "clean-plate", "virtual-nd", "strobe"}) ||
        !one_of(cfg.olpf_mode, {"auto", "none", "weak4", "gauss16"}) ||
        !one_of(cfg.reflection_mode, {"off", "glint-suppress", "split-layers"}) ||
        !one_of(cfg.transient_color, {"rgb", "gray"}) || (cfg.tile_size != 256 && cfg.tile_size != 512)) {
        log_err("Error: invalid Suite mode or dataset tile size (256|512).\n"); return 2;
    }
    if (cfg.fields_csv.empty()) { log_err("Error: --fields requires at least one field.\n"); return 2; }
    for (size_t start = 0; start < cfg.fields_csv.size();) {
        size_t end = cfg.fields_csv.find(',', start);
        if (!one_of(cfg.fields_csv.substr(start, end-start),
            {"motion", "phase", "focus", "depth", "defocus", "specular", "sigma", "coherence", "velocity", "flicker", "turbulence",
             "normal_x", "normal_y", "occlusion"})) {
            log_err("Error: unknown physical field in --fields.\n"); return 2;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if(cfg.lcp_fields.empty()) { log_err("Error: --lcp-fields requires a model.\n");return 2; }
    for(size_t start=0;start<cfg.lcp_fields.size();) {
        size_t end=cfg.lcp_fields.find(',',start);
        if(!one_of(cfg.lcp_fields.substr(start,end-start),{"lca","vignette","distortion"})) {
            log_err("Error: --lcp-fields expects lca,vignette,distortion.\n");return 2;
        }
        if(end==std::string::npos) break;
        start=end+1;
    }
    if (cfg.field_mode == heraw_io::FieldExportMode::TIFF_ALPHA && (emit_field_in_dir || !out_field_path.empty())) {
        if (cfg.rgb_format != heraw_io::RgbFormat::TIFF_RAW && cfg.rgb_format != heraw_io::RgbFormat::TIFF_HQ) {
            cfg.rgb_format = heraw_io::RgbFormat::TIFF_DEFLATE;
        }
        cfg.explicit_rgb_fmt = true;
        emit_rgb_in_dir = true;
        if (out_dir_path.empty() && out_rgb_path.empty() && cfg.out_rgb_1x_path.empty()) {
            out_rgb_path = std::filesystem::u8path(out_field_path).replace_extension(".tif").u8string();
        }
    }
    if (cfg.export_topk && out_dir_path.empty()) {
        log_err("Error: --export-topk requires --out-dir.\n"); return 2;
    }
    if (!out_dir_path.empty() && !emit_raw_in_dir && !emit_rgb_in_dir && !emit_rgb_1x_in_dir &&
        !emit_field_in_dir && !cfg.optics_only && !cfg.export_topk &&
        !cfg.export_parallax_html && !cfg.export_refocus && !cfg.emit_transient &&
        cfg.dataset_dir.empty() && cfg.optics_report.empty() && cfg.export_lcp.empty()) {
        emit_rgb_in_dir = true;
    }
    const bool has_output_target = !out_raw_path.empty() || !out_rgb_path.empty() || !cfg.out_rgb_1x_path.empty() ||
        !out_field_path.empty() || !out_dir_path.empty() || !cfg.optics_report.empty() || !cfg.export_lcp.empty() ||
        !cfg.dataset_dir.empty() || cfg.optics_only || cfg.export_parallax_html || cfg.export_refocus || cfg.emit_transient;

    bool saw_dir_input = false;
    std::vector<std::string> all_inputs = heraw_plan::expand_raw_inputs(raw_pos_args, scan_dirs, recursive_scan, saw_dir_input);
#ifdef HERAW_HAS_WVDC
    if (!all_inputs.empty()) {
        std::vector<std::string> expanded_nev;
        expanded_nev.reserve(all_inputs.size());
        for (const auto& p : all_inputs) {
            if (heraw_wvdc::is_nev_path(p)) {
                std::string nerr;
                if (!heraw_wvdc::expand_nev_clip_frames(p, cfg.nev_frames_spec, expanded_nev, nerr)) {
                    log_err("Error expanding NEV frames on %s: %s\n", p.c_str(), nerr.c_str());
                    return 2;
                }
            } else {
                expanded_nev.push_back(p);
            }
        }
        all_inputs.swap(expanded_nev);
    }
#endif
    if (all_inputs.empty() || (!inspect_only && !plan_json && !has_output_target)) {
        log_err(
            "Error: specify input RAW file(s) or folder(s) and at least one output (--out-raw, --out-rgb, --out-dir) or --inspect.\n"
            "Run '%s --help' for full CLI documentation.\n",
            argv[0] ? argv[0] : "nijinLBS");
        return 2;
    }

    if (cfg.progress_json) {
        log_err("{\"event\":\"start\",\"total_stages\":6,\"percent\":2,\"threads\":%d}\n", cfg.n_threads);
        std::fflush(stderr);
    }

    auto par_wrap = [](int tot, int nt, auto&& fn, int chk) {
        parallel_for_rows(tot, nt, std::forward<decltype(fn)>(fn), chk);
    };

    const int min_burst_frames = ((cfg.rgb_scale_mode == 1) && out_raw_path.empty() && !emit_raw_in_dir) ? 3 : 5;

    // --- Stage 0: Fast Header-Only Metadata Scan & Optional EXIF Time Sort ---
    auto t_scan0 = std::chrono::steady_clock::now();
    std::vector<heraw_plan::RawHeaderInfo> hdr_infos = heraw_plan::scan_raw_headers_fast(
        all_inputs, cfg.sort_time, cfg.n_threads, par_wrap, (int)cfg.decoder_pref
    );
    if (!cfg.skip_frames.empty()) {
        if (cfg.anchor_mode > 0 && std::find(cfg.skip_frames.begin(), cfg.skip_frames.end(), cfg.anchor_mode) != cfg.skip_frames.end()) {
            log_err("Error: selected anchor is in --skip-frames.\n"); return 2;
        }
        std::vector<heraw_plan::RawHeaderInfo> kept;
        int removed_before_anchor = 0;
        for (size_t i = 0; i < hdr_infos.size(); ++i) {
            if (std::find(cfg.skip_frames.begin(), cfg.skip_frames.end(), (int)i+1) == cfg.skip_frames.end())
                kept.push_back(hdr_infos[i]);
            else if ((int)i+1 < cfg.anchor_mode) ++removed_before_anchor;
        }
        hdr_infos.swap(kept);
        if (cfg.anchor_mode > 0) cfg.anchor_mode -= removed_before_anchor;
    }

    const bool is_batch_mode = saw_dir_input || !out_dir_path.empty() || (cfg.policy == heraw_plan::ExecPolicy::MULTI_GROUP);
    std::vector<std::vector<heraw_plan::RawHeaderInfo>> clusters = heraw_plan::cluster_headers_into_bursts(
        hdr_infos, is_batch_mode && !single_group, cfg.burst_gap_s
    );
    if (clusters.empty()) {
        log_err("Error: no readable RAW files found among %zu input path(s).\n", all_inputs.size());
        return 1;
    }

    int total_discovered_groups = 0;
    int skipped_small_clusters = 0;

    if (inspect_only || plan_json || cfg.export_topk) {
        int json_clusters = 0;
        int inspection_errors = 0;
        if (plan_json) std::fprintf(stdout, "{\"clusters\":[\n");
        for (size_t ci = 0; ci < clusters.size(); ++ci) {
            const auto& cl = clusters[ci];
            if (is_batch_mode && !plan_json && !cfg.export_topk && (int)cl.size() < min_burst_frames &&
                clusters.size() > 3 && cfg.policy != heraw_plan::ExecPolicy::FORCE_ALL) {
                ++skipped_small_clusters;
                continue;
            }
            if (plan_json && cl.size() == 1 && clusters.size() >= 4 && hdr_infos.size() >= 12 &&
                cfg.policy != heraw_plan::ExecPolicy::FORCE_ALL) {
                const auto& h0 = cl[0];
                std::string ext = h0.path;
                for (char& ch : ext) ch = (char)std::tolower((unsigned char)ch);
                const char* cfa_str = (ext.size() >= 4 && ext.substr(ext.size() - 4) == ".raf") ? "xtrans" : "bayer";
                const char* shut_str = (h0.model.find("9M3") != std::string::npos || h0.model.find("A9 III") != std::string::npos)
                    ? "global" : "mechanical";
                double est_mb = (double)h0.height * 2.0 * (double)h0.width * 2.0 * 2.0 * 0.39 / (1024.0 * 1024.0);
                std::fprintf(stdout,
                    "%s{\"cluster_index\":%zu,\"motion\":\"single_frame\",\"needs_prompt\":false,"
                    "\"condition\":\"single_shot_stray\",\"summary\":\"Isolated single shot (< %d frames).\",\"groups\":0,"
                    "\"scene\":\"single_shot\",\"rec_preset\":\"photo\",\"rec_anchor\":\"auto\",\"best_anchor\":1,"
                    "\"quad_coverage\":1,\"sr_score\":25,\"star_count\":0,"
                    "\"motion_pct\":0.0,\"ev_spread\":0.00,\"rec_denoise\":1.00,\"rec_deghost\":1.00,"
                    "\"rec_psf_iters\":4,\"est_dng_mb\":%.1f,\"shutter_shock\":false,\"rx_summary\":\"Isolated single shot\","
                    "\"make\":\"%s\",\"model\":\"%s\",\"width\":%d,\"height\":%d,\"iso\":%.1f,\"focal_len\":%.2f,"
                    "\"aperture\":%.2f,\"shutter_s\":%.8g,\"focus_distance_m\":0.0000,"
                    "\"cfa_type\":\"%s\",\"olpf_auto\":\"auto\",\"shutter_type\":\"%s\",\"has_ois\":false,"
                    "\"af_norm_x\":0.50000,\"af_norm_y\":0.50000,\"max_drift_2x_px\":0.0000,\"max_parallax_2x_px\":0.0000,"
                    "\"depth_observable\":false,\"field_observability\":{\"depth\":false,\"defocus\":false,"
                    "\"turbulence\":false,\"flicker\":false,\"coherence\":false,\"velocity\":false},\"frames\":["
                    "{\"idx\":1,\"group\":-1,\"flags\":\"OK\",\"acutance\":50.00,\"shift\":0.00,\"zncc\":1.00,"
                    "\"crown\":true,\"shock\":false,\"file\":\"%s\",\"path\":\"%s\",\"af_score\":50,"
                    "\"dx_1x\":0.0000,\"dy_1x\":0.0000}],\"options\":[]}\n",
                    (json_clusters++ > 0 ? ",\n" : ""), ci, min_burst_frames, est_mb,
                    json_escape(h0.make).c_str(), json_escape(h0.model).c_str(), h0.width, h0.height,
                    h0.iso, h0.focal_len, h0.aperture, h0.shutter, cfa_str, shut_str,
                    json_escape(heraw_plan::basename_of(h0.path)).c_str(), json_escape(h0.path).c_str());
                continue;
            }
            std::vector<std::string> paths;
            for (const auto& h : cl) paths.push_back(h.path);

            std::vector<UnpackedFrame> uframes;
            unpack_frame_batch(uframes, paths, cfg.n_threads, cfg.decoder_pref);
            bool bad_unpack = false;
            for (const auto& uf : uframes) if (uf.status != 0) bad_unpack = true;
            if (bad_unpack) {
                ++inspection_errors;
                for (const auto& uf : uframes) if (uf.status)
                    log_err("Unpack error on %s: %s\n", uf.path.c_str(), uf.err_msg.c_str());
                continue;
            }

            auto pr = heraw_plan::analyze_and_plan_cluster(
                uframes, cl, cfg.policy, is_batch_mode, cfg.group_size, cfg.max_frames,
                cfg.min_rel_sharpness, cfg.max_shift_px, quick_percentile, cfg.anchor_mode,
                cfg.preset_name, cfg.n_threads, min_burst_frames
            );
            total_discovered_groups += (int)pr.selected_groups.size();

            if (cfg.export_topk) {
                std::vector<int> order;
                for (size_t i = 0; i < pr.diags.size(); ++i)
                    if (!(pr.diags[i].flags & heraw_plan::SUIT_DARK)) order.push_back((int)i);
                std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return pr.diags[a].acutance > pr.diags[b].acutance; });
                if (order.size() > (size_t)cfg.export_topk) order.resize((size_t)cfg.export_topk);
                std::string manifest = out_dir_path + "/selection_" + std::to_string(ci+1) + ".json";
                FILE* f = fopen_utf8_wb(manifest);
                if (!f) return 1;
                std::fprintf(f, "{\"cluster\":%zu,\"selected\":[", ci+1);
                bool okay = true;
                for (size_t rank = 0; rank < order.size(); ++rank) {
                    const auto& d = pr.diags[order[rank]];
                    std::string dst = out_dir_path + "/" + std::to_string(ci+1) + "_" + heraw_plan::basename_of(d.path);
                    std::error_code ec;
                    std::filesystem::copy_file(std::filesystem::u8path(d.path), std::filesystem::u8path(dst),
                        std::filesystem::copy_options::overwrite_existing, ec);
                    if (ec) { log_err("Copy failed: %s\n", ec.message().c_str()); okay = false; break; }
                    std::fprintf(f, "%s{\"rank\":%zu,\"source\":\"%s\",\"output\":\"%s\",\"af_score\":%d,\"acutance\":%.6f}",
                        rank ? "," : "", rank+1, json_escape(d.path).c_str(), json_escape(dst).c_str(), d.af_score, d.acutance);
                }
                std::fprintf(f, "]}\n");
                if (std::ferror(f)) okay = false;
                if (std::fclose(f)) okay = false;
                if (!okay) return 1;
                log_err("Selected %zu original RAW file(s) -> %s\n", order.size(), manifest.c_str());
            } else if (plan_json) {
                const auto& rx = pr.rx;
                std::fprintf(stdout,
                    "%s{\"cluster_index\":%zu,\"motion\":\"%s\",\"needs_prompt\":%s,"
                    "\"condition\":\"%s\",\"summary\":\"%s\",\"groups\":%zu,"
                    "\"scene\":\"%s\",\"rec_preset\":\"%s\",\"rec_anchor\":\"%s\",\"best_anchor\":%d,"
                    "\"quad_coverage\":%d,\"sr_score\":%d,\"star_count\":%d,"
                    "\"motion_pct\":%.1f,\"ev_spread\":%.2f,\"rec_denoise\":%.2f,\"rec_deghost\":%.2f,"
                    "\"rec_psf_iters\":%d,\"est_dng_mb\":%.1f,\"shutter_shock\":%s,\"rx_summary\":\"%s\",",
                    (json_clusters++ > 0 ? ",\n" : ""), ci, heraw_plan::motion_regime_str(pr.overall_motion),
                    pr.needs_interactive_prompt ? "true" : "false",
                    json_escape(pr.condition_code).c_str(), json_escape(pr.condition_summary).c_str(),
                    pr.selected_groups.size(),
                    json_escape(rx.scene_code).c_str(), json_escape(rx.rec_preset).c_str(),
                    json_escape(rx.rec_anchor).c_str(), rx.best_anchor_1based,
                    rx.quad_coverage, rx.sr_score, rx.star_count,
                    rx.motion_area_pct, rx.max_ev_spread, rx.rec_denoise, rx.rec_deghost,
                    rx.rec_psf_iters, rx.est_dng_mb, rx.shutter_shock_first ? "true" : "false",
                    json_escape(rx.summary_en).c_str());
                const auto& um = uframes[0];
                const size_t n_cl = uframes.size();
                const bool plan_flicker = (n_cl >= 4 && rx.shutter_s > 0.0f && rx.shutter_s <= (1.0f / 99.0f));
                const bool plan_dist = (rx.focus_distance_m > 50.0f) ||
                    (rx.focus_distance_m <= 0.0f && um.focal_mm >= 85.0f && (!rx.depth_observable || rx.max_parallax_2x_px < 4.0f));
                const bool plan_turb = (n_cl >= 5 && um.focal_mm >= 85.0f && plan_dist);
                std::fprintf(stdout,
                    "\"make\":\"%s\",\"model\":\"%s\",\"width\":%d,\"height\":%d,\"iso\":%.1f,\"focal_len\":%.2f,"
                    "\"aperture\":%.2f,\"shutter_s\":%.8g,\"focus_distance_m\":%.4f,"
                    "\"cfa_type\":\"%s\",\"olpf_auto\":\"%s\",\"shutter_type\":\"%s\",\"has_ois\":%s,"
                    "\"af_norm_x\":%.5f,\"af_norm_y\":%.5f,\"max_drift_2x_px\":%.4f,\"max_parallax_2x_px\":%.4f,"
                    "\"depth_observable\":%s,\"field_observability\":{\"depth\":%s,\"defocus\":%s,"
                    "\"turbulence\":%s,\"flicker\":%s,\"coherence\":%s,\"velocity\":%s},\"frames\":[",
                    json_escape(um.make).c_str(), json_escape(um.model).c_str(), um.W, um.H, um.iso, um.focal_mm,
                    rx.aperture, rx.shutter_s, rx.focus_distance_m,
                    rx.cfa_type.c_str(), rx.olpf_auto.c_str(), rx.shutter_type.c_str(), rx.has_ois ? "true" : "false",
                    rx.af_norm_x, rx.af_norm_y, rx.max_drift_2x_px, rx.max_parallax_2x_px,
                    rx.depth_observable ? "true" : "false",
                    rx.depth_observable ? "true" : "false", rx.has_focus_steps ? "true" : "false",
                    plan_turb ? "true" : "false", plan_flicker ? "true" : "false",
                    n_cl >= 4 ? "true" : "false", n_cl >= 3 ? "true" : "false");
                for (size_t fi = 0; fi < pr.diags.size(); ++fi) {
                    const auto& fd = pr.diags[fi];
                    std::fprintf(stdout,
                        "%s{\"idx\":%d,\"group\":%d,\"flags\":\"%s\",\"acutance\":%.2f,"
                        "\"shift\":%.2f,\"zncc\":%.2f,\"crown\":%s,\"shock\":%s,\"file\":\"%s\","
                        "\"path\":\"%s\",\"af_score\":%d,\"dx_1x\":%.4f,\"dy_1x\":%.4f}",
                        (fi > 0 ? "," : ""), fd.idx + 1, fd.assigned_group,
                        json_escape(heraw_plan::flags_to_string(fd.flags)).c_str(),
                        fd.acutance, fd.shift_1x, fd.zncc,
                        fd.is_crown ? "true" : "false", fd.shutter_shock ? "true" : "false",
                        json_escape(heraw_plan::basename_of(fd.path)).c_str(),
                        json_escape(fd.path).c_str(), fd.af_score, fd.dx_1x, fd.dy_1x);
                }
                std::fprintf(stdout, "],\"options\":[");
                for (size_t oi = 0; oi < pr.options.size(); ++oi) {
                    const auto& op = pr.options[oi];
                    std::fprintf(stdout,
                        "%s{\"id\":%d,\"aborts\":%s,\"groups\":%zu,\"title\":\"%s\",\"detail\":\"%s\"}",
                        (oi > 0 ? "," : ""), op.id, op.aborts ? "true" : "false",
                        op.groups.size(), json_escape(op.title).c_str(), json_escape(op.detail).c_str());
                }
                std::fprintf(stdout, "]}\n");
            } else {
                const auto& u0 = uframes[0];
                const auto& rx = pr.rx;
                const int out_w_disp = (min_burst_frames == 3) ? u0.W : (u0.W * 2);
                const int out_h_disp = (min_burst_frames == 3) ? u0.H : (u0.H * 2);
                std::fprintf(stdout,
                    "=== Candidate Cluster %zu/%zu (%s %s, %dx%d -> %dx%d) ===\n"
                    "  5D Scene        : %s | SR Score: %d/100 (%d/4 subpixel quads) | MovArea: %.1f%% | Stars: %d\n"
                    "  Prescription    : preset=%s, anchor=%s (crown=#%d), denoise=%.2f, deghost=%.2f, rl=%d (~%.0f MiB DNG)\n"
                    "  Motion Regime   : %s\n"
                    "  Status          : [%s] %s\n"
                    "  Planned Groups  : %zu\n"
                    "  Idx  Grp  Role   Flags                Acutance  Shift1x   ZNCC   File\n",
                    ci + 1, clusters.size(), u0.make.c_str(), u0.model.c_str(), u0.W, u0.H, out_w_disp, out_h_disp,
                    rx.scene_code.c_str(), rx.sr_score, rx.quad_coverage, rx.motion_area_pct, rx.star_count,
                    rx.rec_preset.c_str(), rx.rec_anchor.c_str(), rx.best_anchor_1based,
                    rx.rec_denoise, rx.rec_deghost, rx.rec_psf_iters, rx.est_dng_mb,
                    heraw_plan::motion_regime_str(pr.overall_motion),
                    pr.condition_code.c_str(), pr.condition_summary.c_str(), pr.selected_groups.size());
                for (const auto& d : pr.diags) {
                    const char* role = d.is_crown ? "CROWN" : (d.shutter_shock ? "SHOCK" : "     ");
                    std::fprintf(stdout, "  %3d  %3d  %-5s  %-19s  %8.2f  %6.2fpx  %5.2f  %s\n",
                        d.idx + 1, d.assigned_group, role, heraw_plan::flags_to_string(d.flags).c_str(),
                        d.acutance, d.shift_1x, d.zncc, heraw_plan::basename_of(d.path).c_str());
                }
                if (pr.needs_interactive_prompt && !pr.options.empty()) {
                    std::fprintf(stdout, "  Interactive Recommendations (<= 2):\n");
                    for (const auto& op : pr.options) {
                        std::fprintf(stdout, "    [%d] %s -- %s\n", op.id, op.title.c_str(), op.detail.c_str());
                    }
                }
                std::fprintf(stdout, "\n");
            }
        }
        if (plan_json) {
            std::fprintf(stdout, "],\"total_groups\":%d}\n", total_discovered_groups);
        } else if (skipped_small_clusters > 0) {
            std::fprintf(stdout, "Skipped %d isolated single/pair cluster(s) (< %d frames) during batch scan.\n",
                skipped_small_clusters, min_burst_frames);
        }
        std::fflush(stdout);
        return inspection_errors ? 1 : 0;
    }

    int global_groups_done = 0;
    for (size_t ci = 0; ci < clusters.size(); ++ci) {
        const auto& cl = clusters[ci];
        if (is_batch_mode && clusters.size() > 1 && (int)cl.size() < min_burst_frames && cfg.policy != heraw_plan::ExecPolicy::FORCE_ALL) {
            ++skipped_small_clusters;
            continue;
        }

        auto t_u0 = std::chrono::steady_clock::now();
        std::vector<std::string> paths;
        for (const auto& h : cl) paths.push_back(h.path);

        std::vector<UnpackedFrame> uframes;
        unpack_frame_batch(uframes, paths, cfg.n_threads, cfg.decoder_pref);
        bool cluster_bad = false;
        for (const auto& uf : uframes) {
            if (uf.status != 0) {
                log_err("Unpack error on %s: %s\n", uf.path.c_str(), uf.err_msg.c_str());
                cluster_bad = true;
            }
        }
        if (cluster_bad) {
            if (is_batch_mode && clusters.size() > 1) continue;
            return 1;
        }
        if (cfg.anchor_mode > (int)uframes.size()) {
            log_err("Error: --anchor-frame exceeds the cluster's %zu frames.\n", uframes.size());
            return 2;
        }
        {
            double dt1 = elapsed_s(t_u0) + (ci == 0 ? elapsed_s(t_scan0) - elapsed_s(t_u0) : 0.0);
            const auto& dyn = heraw_lr::global_dyn_libraw();
            log_err("[1/6] Unpacked %zu RAW file(s) via %s (cluster %zu/%zu, sort_time=%s) in %.2fs\n",
                paths.size(), dyn.active ? ("Custom LibRaw " + dyn.ver_str).c_str() : "Built-in LibRaw 0.23-HE*",
                ci + 1, clusters.size(), cfg.sort_time ? "on" : "off", std::max(0.01, dt1));
            if (cfg.progress_json) {
                log_err("{\"event\":\"progress\",\"stage\":1,\"total_stages\":6,\"percent\":14,\"message\":\"Unpacked %zu RAW file(s)\"}\n", paths.size());
            }
            std::fflush(stderr);
        }

        PipelineConfig cluster_cfg = cfg;
        auto pr = heraw_plan::analyze_and_plan_cluster(
            uframes, cl, cluster_cfg.policy, is_batch_mode, cluster_cfg.group_size, cluster_cfg.max_frames,
            cluster_cfg.min_rel_sharpness, cluster_cfg.max_shift_px, quick_percentile, cluster_cfg.anchor_mode,
            cluster_cfg.preset_name, cluster_cfg.n_threads, min_burst_frames
        );

        auto apply_rx_to_cfg = [&](PipelineConfig& target_c, const heraw_plan::ScenePrescription& rx) {
            if (rx.rec_preset == "astro") apply_preset_defaults(target_c, PipelinePreset::ASTRO);
            else if (rx.rec_preset == "sci") apply_preset_defaults(target_c, PipelinePreset::SCI);
            else if (rx.rec_preset == "arc") apply_preset_defaults(target_c, PipelinePreset::ARC);
            else apply_preset_defaults(target_c, PipelinePreset::PHOTO);
            if (!ov_den) target_c.denoise_strength = rx.rec_denoise;
            if (!ov_degh) target_c.deghost_strength = rx.rec_deghost;
            if (!ov_psfi) target_c.psf_iters = rx.rec_psf_iters;
            if (rx.rec_anchor == "middle") target_c.anchor_mode = -2;
            else if (rx.rec_anchor == "first") target_c.anchor_mode = -1;
            else if (std::atoi(rx.rec_anchor.c_str()) >= 1) target_c.anchor_mode = std::atoi(rx.rec_anchor.c_str());
        };

        if (cluster_cfg.wizard_mode) {
            const auto& rx = pr.rx;
            log_err(
                "\n=== Smart Diagnosis Wizard [Cluster %zu/%zu] ===\n"
                "  Scene        : %s | SR Potential: %d/100 (%d/4 quads) | Motion Area: %.1f%% | Stars: %d\n"
                "  Crown Frame  : #%d (%s)%s\n"
                "  Prescription : preset=%s, anchor=%s, denoise=%.2f, deghost=%.2f, psf_iters=%d\n"
                "  Rationale    : %s\n"
                "  [1] Apply smart prescription & fuse (Recommended)\n"
                "  [2] Keep current CLI settings & fuse\n"
                "  [3] Pick custom pose anchor frame #1..%zu & apply prescription\n"
                "  [0] Cancel\n"
                "Wizard choice [Enter=1]: ",
                ci + 1, clusters.size(), rx.scene_code.c_str(), rx.sr_score, rx.quad_coverage,
                rx.motion_area_pct, rx.star_count, rx.best_anchor_1based,
                heraw_plan::basename_of(pr.diags[std::max(0, rx.best_anchor_1based - 1)].path).c_str(),
                rx.shutter_shock_first ? " [Note: Frame #1 has slight shutter shock]" : "",
                rx.rec_preset.c_str(), rx.rec_anchor.c_str(), rx.rec_denoise, rx.rec_deghost, rx.rec_psf_iters,
                rx.summary_en.c_str(), pr.diags.size());
            std::fflush(stderr);
            int wpick = (cluster_cfg.interactive_choice >= 1 && cluster_cfg.interactive_choice <= 2) ? cluster_cfg.interactive_choice : -1;
            if (wpick < 0) {
                char lbuf[64] = {0};
                if (std::fgets(lbuf, sizeof(lbuf), stdin)) {
                    if (lbuf[0] == '0') return 0;
                    int v = std::atoi(lbuf);
                    wpick = (v >= 1 && v <= 3) ? v : 1;
                } else {
                    wpick = 1;
                }
            }
            if (wpick == 3) {
                log_err("Enter anchor frame index [1..%zu]: ", pr.diags.size());
                std::fflush(stderr);
                char abuf[64] = {0};
                int aidx = rx.best_anchor_1based;
                if (std::fgets(abuf, sizeof(abuf), stdin)) {
                    int v = std::atoi(abuf);
                    if (v >= 1 && v <= (int)pr.diags.size()) aidx = v;
                }
                apply_rx_to_cfg(cluster_cfg, rx);
                cluster_cfg.anchor_mode = aidx;
                pr = heraw_plan::analyze_and_plan_cluster(
                    uframes, cl, heraw_plan::ExecPolicy::AUTO_PRUNE, is_batch_mode, cluster_cfg.group_size,
                    cluster_cfg.max_frames, cluster_cfg.min_rel_sharpness, cluster_cfg.max_shift_px,
                    quick_percentile, cluster_cfg.anchor_mode, cluster_cfg.preset_name, cluster_cfg.n_threads, min_burst_frames
                );
            } else if (wpick == 1) {
                apply_rx_to_cfg(cluster_cfg, rx);
                if (!pr.options.empty() && !pr.options[0].aborts) pr.selected_groups = pr.options[0].groups;
            }
        } else if (cluster_cfg.policy == heraw_plan::ExecPolicy::INTERACTIVE && pr.needs_interactive_prompt && !pr.options.empty()) {
            int pick = cluster_cfg.interactive_choice;
            if (pick != 1 && pick != 2) {
                log_err("\n[Suitability Notice: %s] %s\n", pr.condition_code.c_str(), pr.condition_summary.c_str());
                for (const auto& op : pr.options) {
                    log_err("  [%d] %s\n      %s\n", op.id, op.title.c_str(), op.detail.c_str());
                }
                log_err("Select option [1/%zu, Enter=1]: ", pr.options.size());
                std::fflush(stderr);
                char line_buf[64] = {0};
                if (std::fgets(line_buf, sizeof(line_buf), stdin)) {
                    int v = std::atoi(line_buf);
                    pick = (v == 2 && pr.options.size() >= 2) ? 2 : 1;
                } else {
                    pick = 1;
                }
            }
            const auto& chosen = (pick == 2 && pr.options.size() >= 2) ? pr.options[1] : pr.options[0];
            log_err("-> Selected option [%d]: %s\n", chosen.id, chosen.title.c_str());
            std::fflush(stderr);
            if (chosen.aborts) {
                log_err("Operation canceled cleanly per interactive selection.\n");
                return 0;
            }
            if (pick == 1 && pr.condition_code == "astro_scene_detected") {
                apply_rx_to_cfg(cluster_cfg, pr.rx);
            }
            pr.selected_groups = chosen.groups;
        }

        if (pr.selected_groups.empty()) {
            if (is_batch_mode && clusters.size() > 1) {
                ++skipped_small_clusters;
                continue;
            }
            log_err("Suitability check stopped execution [%s]: %s\n",
                pr.condition_code.c_str(), pr.condition_summary.c_str());
            return 3;
        }

        const int n_grp = (int)pr.selected_groups.size();
        for (int gi = 0; gi < n_grp; ++gi) {
            const auto& grp = pr.selected_groups[gi];
            if (grp.light_uframe_indices.empty()) continue;
            std::string g_raw = out_raw_path;
            std::string g_rgb = (cfg.rgb_scale_mode == 1) ? std::string() : out_rgb_path;
            std::string g_rgb_1x = !cfg.out_rgb_1x_path.empty() ? cfg.out_rgb_1x_path :
                ((cfg.rgb_scale_mode == 1) ? out_rgb_path : std::string());
            if (cfg.rgb_scale_mode == 3 && g_rgb_1x.empty() && !out_rgb_path.empty()) {
                size_t dot = out_rgb_path.find_last_of('.');
                size_t sl = out_rgb_path.find_last_of("/\\");
                if (dot != std::string::npos && (sl == std::string::npos || dot > sl))
                    g_rgb_1x = out_rgb_path.substr(0, dot) + "_1x" + out_rgb_path.substr(dot);
                else
                    g_rgb_1x = out_rgb_path + "_1x" + rgb_format_ext(cfg.rgb_format);
            }
            std::string g_fld = out_field_path;

            PipelineConfig group_cfg = cluster_cfg;
            if (!out_dir_path.empty()) {
                std::string stem = grp.naming_stem;
                if (n_grp > 1 || clusters.size() > 1) {
                    char sb[24]; std::snprintf(sb, sizeof(sb), "_grp%02d", global_groups_done + 1);
                    stem += sb;
                }
                stem += "_nijinLBS";
                char sep = (out_dir_path.find('\\') != std::string::npos) ? '\\' : '/';
                std::string base = out_dir_path;
                if (!base.empty() && base.back() != '/' && base.back() != '\\') base.push_back(sep);
                group_cfg.suite_prefix_override = base + stem;
                const char* ext = rgb_format_ext(cfg.rgb_format);
                if (emit_raw_in_dir && g_raw.empty()) g_raw = base + stem + ".dng";
                if (emit_rgb_in_dir) {
                    if (cfg.rgb_scale_mode != 1 && g_rgb.empty()) g_rgb = base + stem + ext;
                    if ((cfg.rgb_scale_mode == 1 || cfg.rgb_scale_mode == 3) && g_rgb_1x.empty())
                        g_rgb_1x = base + stem + "_1x" + ext;
                }
                if (emit_rgb_1x_in_dir && g_rgb_1x.empty()) g_rgb_1x = base + stem + "_1x" + ext;
                if (emit_field_in_dir && g_fld.empty()) {
                    g_fld = base + stem + "_field.png";
                    group_cfg.field_from_outdir = true;
                }
            }
            if (n_grp > 1 || clusters.size() > 1) {
                int tag_id = global_groups_done + 1;
                if (!out_raw_path.empty()) g_raw = add_group_suffix(g_raw, tag_id, std::max(2, n_grp));
                if (!g_rgb.empty() && !out_rgb_path.empty()) g_rgb = add_group_suffix(g_rgb, tag_id, std::max(2, n_grp));
                if (!g_rgb_1x.empty() && (!cfg.out_rgb_1x_path.empty() || !out_rgb_path.empty()))
                    g_rgb_1x = add_group_suffix(g_rgb_1x, tag_id, std::max(2, n_grp));
                if (!out_field_path.empty()) g_fld = add_group_suffix(g_fld, tag_id, std::max(2, n_grp));
            }

            bool is_last_in_cluster = (gi == n_grp - 1);
            if (n_grp > 1 || clusters.size() > 1) {
                const int tag = global_groups_done + 1;
                group_cfg.optics_report = add_group_suffix(group_cfg.optics_report, tag, 2);
                group_cfg.export_lcp = add_group_suffix(group_cfg.export_lcp, tag, 2);
                if (!group_cfg.dataset_dir.empty()) group_cfg.dataset_dir += "/group_" + std::to_string(tag);
            }
            if (group_cfg.optics_only && group_cfg.optics_report.empty() && !out_dir_path.empty())
                group_cfg.optics_report = out_dir_path + "/" + grp.naming_stem + "_optics";
            std::vector<int> selected = grp.light_uframe_indices;
            const int requested_anchor = group_cfg.anchor_mode > 0 ? group_cfg.anchor_mode - 1 :
                (group_cfg.anchor_mode == -1 ? 0 : (group_cfg.anchor_mode == -2 ? (int)uframes.size() / 2 : -1));
            if (n_grp == 1 && requested_anchor >= 0) {
                const int anchor = requested_anchor;
                auto pos = std::find(selected.begin(), selected.end(), anchor);
                if (pos != selected.end()) selected.erase(pos);
                else if (cfg.max_frames > 0 && (int)selected.size() >= cfg.max_frames) selected.pop_back();
                selected.insert(selected.begin(), anchor);
            }
            int rc = execute_sr_group(
                uframes, selected,
                is_last_in_cluster, g_raw, g_rgb, g_rgb_1x, g_fld, group_cfg, gi + 1, n_grp, t_wall0
            );
            if (rc != 0) return rc;
            ++global_groups_done;
        }
    }

    if (global_groups_done == 0) {
        log_err("Batch scan finished: 0 burst groups (>= %d valid frames) found (%d smaller/singleton shot(s) skipped).\n",
            min_burst_frames, skipped_small_clusters);
        return 3;
    }
    if (cfg.progress_json) {
        log_err("{\"event\":\"done\",\"groups_completed\":%d,\"wall_s\":%.3f}\n",
            global_groups_done, elapsed_s(t_wall0));
        std::fflush(stderr);
    }
    if (clusters.size() > 1 || global_groups_done > 1) {
        log_err("Batch completed: exported %d burst group(s) (%d non-burst cluster(s) skipped) in %.2fs.\n",
            global_groups_done, skipped_small_clusters, elapsed_s(t_wall0));
    }
    return 0;
}

static int run_cli(int argc, char** argv) {
    int rc = run_cli_impl(argc, argv);
    g_diag_log.close(rc);
    return rc;
}

int main(int argc, char** argv) {
    try {
        return run_cli(argc, argv);
    } catch (const std::exception& error) {
        log_err("Error: %s\n", error.what());
        g_diag_log.close(1);
        return 1;
    }
}
