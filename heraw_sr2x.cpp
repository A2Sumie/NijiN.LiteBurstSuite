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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#else
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#endif
#ifdef __aarch64__
#include <arm_neon.h>
#endif
#ifdef __AVX2__
#include <immintrin.h>
#endif

#include "../libraw-heraw/libraw/libraw.h"
#include "heraw_libraw_dyn.hpp"
#include "heraw_exif_io.hpp"
#include "heraw_burst_planner.hpp"
#include "heraw_gpu_backend.hpp"
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

    template <typename Fn>
    void run(int total_rows, int n_threads, int chunk_rows, Fn&& fn) {
        if (total_rows <= 0) return;
        int nt = std::max(1, std::min(n_threads, total_rows));
        int chunk = chunk_rows;
        if (chunk <= 0) {
            int target = total_rows / (nt * 8);
            chunk = clamp_i(target, 1, 64);
            if (chunk > 1) chunk = (chunk + 1) & ~1;
        }
        if (nt == 1 || total_rows <= chunk) {
            fn(0, total_rows, 0);
            return;
        }

        int n_bg_use = nt - 1;
        next_row_.store(0, std::memory_order_relaxed);
        total_rows_ = total_rows;
        chunk_rows_ = chunk;
        task_fn_ = +[](void* ctx, int r0, int r1, int tid) {
            (*static_cast<typename std::remove_reference<Fn>::type*>(ctx))(r0, r1, tid);
        };
        task_ctx_ = const_cast<void*>(static_cast<const void*>(&fn));

        {
            std::lock_guard<std::mutex> lk(mtx_);
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

        drain_chunks(0);

        std::unique_lock<std::mutex> lk(mtx_);
        cv_done_.wait(lk, [this]() { return active_workers_ == 0; });
    }

private:
    void drain_chunks(int tid) {
        const int tot = total_rows_;
        const int chk = chunk_rows_;
        while (true) {
            int r0 = next_row_.fetch_add(chk, std::memory_order_relaxed);
            if (r0 >= tot) break;
            int r1 = std::min(tot, r0 + chk);
            task_fn_(task_ctx_, r0, r1, tid);
        }
    }

    void worker_loop(int tid, uint64_t seen_gen) {
        while (true) {
            int bg_lim = 0;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_start_.wait(lk, [&]() { return stop_ || gen_ != seen_gen; });
                if (stop_) return;
                seen_gen = gen_;
                bg_lim = n_bg_active_;
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
    std::atomic<int> next_row_{0};
    int total_rows_ = 0;
    int chunk_rows_ = 1;
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
    float iso = 0.0f, focus_distance_m = 0.0f;
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
    const auto source_exif=heraw_io::extract_source_exif(uf.path);
    uf.timestamp_s = (double)d.other.timestamp + 0.001 * source_exif.subsec_ms;
    for(const auto& tag:source_exif.exif_keep) {
        if(tag.tag==37382 && tag.type==5 && tag.data.size()>=8) {
            const uint32_t num=heraw_io::rd_u32(tag.data.data(),false);
            const uint32_t den=heraw_io::rd_u32(tag.data.data()+4,false);
            if(num && den) uf.focus_distance_m=num==0xffffffffu ? 1000000.0f : (float)num/den;
        }
    }
    uf.lens = d.lens.Lens;
    const int stab = d.shootinginfo.ImageStabilization;
    uf.has_ois = stab > 0 && stab != 0xffff;
    if (uf.model.find("ILCE-9M3") != std::string::npos) uf.shutter_kind = 2;
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

static void unpack_one_frame(UnpackedFrame* uf) {
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
static void* unpack_pthread_trampoline(void* arg) {
    unpack_one_frame(static_cast<UnpackedFrame*>(arg));
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

    if (src != dst && rad <= 12) {
        // L2-resident 2D tile fusion (224 KB/thread max): zero 731MB DRAM intermediate buffer on non-X3D CPUs!
        const int TILE_Y = 32;
        const int TILE_X = 1024;
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
                        const float* __restrict srow = src + (size_t)sr * W;
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

    static thread_local std::vector<float> tls_tmp;
    if (tls_tmp.size() < (size_t)H * (size_t)W) {
        tls_tmp.resize((size_t)H * (size_t)W);
    }
    float* tmp_ptr = tls_tmp.data();

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
            for (int c = 1; c < W - 1; ++c) {
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
    std::vector<double> sat1; // (H+1)*(W+1) sum(cur)
    std::vector<double> sat2; // (H+1)*(W+1) sum(cur^2)

    void build(const float* cur, int h, int w) {
        H = h; W = w;
        const int S = W + 1;
        sat1.assign((size_t)(H + 1) * S, 0.0);
        sat2.assign((size_t)(H + 1) * S, 0.0);
        for (int r = 0; r < H; ++r) {
            double r1 = 0.0, r2 = 0.0;
            const float* crow = cur + (size_t)r * W;
            const double* p1 = sat1.data() + (size_t)r * S;
            const double* p2 = sat2.data() + (size_t)r * S;
            double* d1 = sat1.data() + (size_t)(r + 1) * S;
            double* d2 = sat2.data() + (size_t)(r + 1) * S;
            for (int c = 0; c < W; ++c) {
                double v = (double)crow[c];
                r1 += v;
                r2 += v * v;
                d1[c + 1] = p1[c + 1] + r1;
                d2[c + 1] = p2[c + 1] + r2;
            }
        }
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
        double h00 = 0.0, h01 = 0.0, h11 = 0.0;
        const int n_in = (bh - 2) * (bw - 2);
        if ((int)tls_gx.size() < n_in) { tls_gx.resize(n_in); tls_gy.resize(n_in); tls_res.resize(n_in); }
        for (int r = 1; r < bh - 1; ++r) {
            const float* rm = ref + (size_t)(by + r - 1) * W + bx;
            const float* rc = ref + (size_t)(by + r)     * W + bx;
            const float* rp = ref + (size_t)(by + r + 1) * W + bx;
            for (int c = 1; c < bw - 1; ++c) {
                float gx = 0.5f * (rc[c + 1] - rc[c - 1]);
                float gy = 0.5f * (rp[c] - rm[c]);
                size_t k = (size_t)(r - 1) * (bw - 2) + (c - 1);
                tls_gx[k] = gx; tls_gy[k] = gy;
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
                double sum_r = 0.0;
                for (int r = 1; r < bh - 1; ++r) {
                    const float* rc = ref + (size_t)(by + r) * W + bx;
                    float yq = (float)((double)(by + r) + lk_dy);
                    int y0 = clamp_i((int)yq, 0, H - 2);
                    float fy = yq - (float)y0, fy0 = 1.0f - fy;
                    const float* cr0 = cur + (size_t)y0 * W;
                    const float* cr1 = cr0 + W;
                    for (int c = 1; c < bw - 1; ++c) {
                        float xq = (float)((double)(bx + c) + lk_dx);
                        int x0 = clamp_i((int)xq, 0, W - 2);
                        float fx = xq - (float)x0, fx0 = 1.0f - fx;
                        float cv = fy0 * (fx0 * cr0[x0] + fx * cr0[x0 + 1]) + fy * (fx0 * cr1[x0] + fx * cr1[x0 + 1]);
                        float diff = cv - rc[c];
                        tls_res[(r - 1) * (bw - 2) + (c - 1)] = diff;
                        sum_r += diff;
                    }
                }
                float mean_r = (float)(sum_r / (double)n_in);
                double b0 = 0.0, b1 = 0.0;
                for (int k = 0; k < n_in; ++k) {
                    float rv = tls_res[k] - mean_r;
                    b0 += (double)(tls_gx[k] * rv);
                    b1 += (double)(tls_gy[k] * rv);
                }
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
    double& out_quality, int n_threads, bool allow_local_tiles = true
) {
    fy_out.assign((size_t)Hh * Wh, 0.0f);
    fx_out.assign((size_t)Hh * Wh, 0.0f);

    // Build 3-level pyramid: L0 (Hh x Wh), L1 (Hh/2 x Wh/2), L2 (Hh/4 x Wh/4)
    int H1p = Hh / 2, W1p = Wh / 2;
    int H2p = H1p / 2, W2p = W1p / 2;
    std::vector<float> r1((size_t)H1p * W1p), c1((size_t)H1p * W1p);
    std::vector<float> r2((size_t)H2p * W2p), c2((size_t)H2p * W2p);
    box_downsample_2x(g0_pre, Hh, Wh, r1.data(), n_threads);
    box_downsample_2x(gi_pre, Hh, Wh, c1.data(), n_threads);
    box_downsample_2x(r1.data(), H1p, W1p, r2.data(), n_threads);
    box_downsample_2x(c1.data(), H1p, W1p, c2.data(), n_threads);

    double Hm[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    out_quality = 0.0;

    struct PyrSpec { const float* ref; const float* cur; int H, W, nby, nbx, rad, step; double scale_from_prev; };
    PyrSpec specs[3] = {
        {r2.data(), c2.data(), H2p, W2p, 5, 7, 24, 2, 1.0},
        {r1.data(), c1.data(), H1p, W1p, 6, 9, 5,  1, 2.0},
        {g0_pre,    gi_pre,    Hh,  Wh,  8, 11, 4, 1, 2.0},
    };

    for (int lv = 0; lv < 3; ++lv) {
        const auto& sp = specs[lv];
        if (sp.scale_from_prev != 1.0) {
            double s = sp.scale_from_prev;
            Hm[0][2] *= s; Hm[1][2] *= s;
            Hm[2][0] /= s; Hm[2][1] /= s;
        }
        if (sp.H < 96 || sp.W < 96) continue;
        CurIntegral cur_sat;
        cur_sat.build(sp.cur, sp.H, sp.W);
        int margin = std::min(16, sp.H / 10);
        int bh = std::max(24, (sp.H - 2 * margin) / sp.nby);
        int bw = std::max(24, (sp.W - 2 * margin) / sp.nbx);
        std::vector<BlockMatch> matches;
        std::vector<BlockMatch> thread_matches(sp.nby * sp.nbx);
        std::vector<uint8_t> thread_ok(sp.nby * sp.nbx, 0);

        parallel_for_rows(sp.nby, n_threads, [&](int by0, int by1, int) {
            for (int iy = by0; iy < by1; ++iy) {
                int by = margin + iy * std::max(0, (sp.H - 2 * margin - bh)) / std::max(1, sp.nby - 1);
                for (int ix = 0; ix < sp.nbx; ++ix) {
                    int bx = margin + ix * std::max(0, (sp.W - 2 * margin - bw)) / std::max(1, sp.nbx - 1);
                    double cx = (double)bx + 0.5 * bw;
                    double cy = (double)by + 0.5 * bh;
                    double den = Hm[2][0] * cx + Hm[2][1] * cy + Hm[2][2];
                    int pred_dx = (int)std::lrint((Hm[0][0] * cx + Hm[0][1] * cy + Hm[0][2]) / den - cx);
                    int pred_dy = (int)std::lrint((Hm[1][0] * cx + Hm[1][1] * cy + Hm[1][2]) / den - cy);
                    int eff_rad = std::min(sp.rad, std::min(sp.H, sp.W) / 4);
                    BlockMatch bm{};
                    if (match_zncc_block(sp.ref, sp.cur, &cur_sat, sp.H, sp.W, by, bx, bh, bw, pred_dy, pred_dx, eff_rad, sp.step, bm)) {
                        thread_matches[iy * sp.nbx + ix] = bm;
                        thread_ok[iy * sp.nbx + ix] = 1;
                    }
                }
            }
        });
        for (int k = 0; k < sp.nby * sp.nbx; ++k) {
            if (thread_ok[k]) matches.push_back(thread_matches[k]);
        }
        double H_cand[3][3];
        double q = 0.0;
        if (fit_homography_irls(matches, sp.H, sp.W, H_cand, q)) {
            std::memcpy(Hm, H_cand, sizeof(Hm));
            out_quality = q;
        }

        // At finest level L0, also fit smooth 2nd-order residual polynomial (captures rolling shutter + OS shift)
        // and accept strictly if full-frame L1 residual improves!
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

            // Fit 6-term quadratic residual to block displacements relative to Hm
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

            // Dense Local Tile Alignment (Wronski et al. SIGGRAPH 2019):
            // Disabled in clean-plate mode so global camera geometry stays rigid underneath Frame-0 transient occluders!
            const int t_stride = 16;
            const int t_win = 48;
            if (allow_local_tiles && Hh >= t_win + 32 && Wh >= t_win + 32) {
                const int gH = (Hh - t_win) / t_stride + 1;
                const int gW = (Wh - t_win) / t_stride + 1;
                std::vector<float> grid_fy((size_t)gH * gW, 0.0f);
                std::vector<float> grid_fx((size_t)gH * gW, 0.0f);

                parallel_for_rows(gH, n_threads, [&](int gr0, int gr1, int) {
                    for (int gr = gr0; gr < gr1; ++gr) {
                        int by = gr * t_stride;
                        int cy = by + t_win / 2;
                        for (int gc = 0; gc < gW; ++gc) {
                            int bx = gc * t_stride;
                            int cx = bx + t_win / 2;
                            float base_fy = fy_out[(size_t)cy * Wh + cx];
                            float base_fx = fx_out[(size_t)cy * Wh + cx];
                            int p_dy = (int)std::lrintf(base_fy);
                            int p_dx = (int)std::lrintf(base_fx);
                            BlockMatch bm{};
                            // Search +-6 half-res px (+-24 2x px) around global model to lock onto local 3D parallax!
                            if (match_zncc_block(g0_pre, gi_pre, &cur_sat, Hh, Wh, by, bx, t_win, t_win, p_dy, p_dx, 6, 2, bm) && bm.ncc > 0.38) {
                                grid_fx[(size_t)gr * gW + gc] = (float)(bm.x1 - bm.x0);
                                grid_fy[(size_t)gr * gW + gc] = (float)(bm.y1 - bm.y0);
                            } else {
                                grid_fx[(size_t)gr * gW + gc] = base_fx;
                                grid_fy[(size_t)gr * gW + gc] = base_fy;
                            }
                        }
                    }
                });

                // 3x3 median filter on dense tile flow grid to reject isolated ambiguous patches while keeping crisp 3D depth edges
                std::vector<float> med_fy((size_t)gH * gW), med_fx((size_t)gH * gW);
                median3x3_plane(grid_fy.data(), med_fy.data(), gH, gW, n_threads);
                median3x3_plane(grid_fx.data(), med_fx.data(), gH, gW, n_threads);

                std::vector<float> dense_fy((size_t)Hh * Wh), dense_fx((size_t)Hh * Wh);
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        float gy_f = ((float)r - 0.5f * (float)t_win) / (float)t_stride;
                        for (int c = 0; c < Wh; ++c) {
                            float gx_f = ((float)c - 0.5f * (float)t_win) / (float)t_stride;
                            size_t idx = (size_t)r * Wh + c;
                            dense_fy[idx] = bilerp_clamped(med_fy.data(), gH, gW, gy_f, gx_f);
                            dense_fx[idx] = bilerp_clamped(med_fx.data(), gH, gW, gy_f, gx_f);
                        }
                    }
                });

                if (compute_mae(dense_fy, dense_fx) <= mae_hom * 1.01) {
                    fy_out.swap(dense_fy);
                    fx_out.swap(dense_fx);
                }
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
    std::vector<float>& fy_io, std::vector<float>& fx_io, std::vector<float>& mw_io, int n_threads
) {
    const int win = 16, stride = 12;
    if (Hh < win + 48 || Wh < win + 48) return;
    CurIntegral sat;
    sat.build(gi_pre, Hh, Wh);

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
                float sf_loc = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * (iv_sum * inv_n))) * 0.32f + 0.0012f;
                if (gf_sum * inv_n < 2.2f * sf_loc) continue; // Flat region: no rigid 2D corner/texture to lock safely

                size_t ci = (size_t)(by + win / 2) * Wh + (bx + win / 2);
                int b_dy = (int)std::lrintf(fy_io[ci]), b_dx = (int)std::lrintf(fx_io[ci]);
                BlockMatch bm{};
                // Gate 1: Strict structural correlation (ZNCC >= 0.90) within +-20 half-res px (+-80 2x px)
                if (!match_zncc_block(g0_pre, gi_pre, &sat, Hh, Wh, by, bx, win, win, b_dy, b_dx, 20, 2, bm) || bm.ncc < 0.90) {
                    continue;
                }
                // Subpixel parabolic refinement around integer ZNCC peak
                int idy = bm.y1 - bm.y0, idx = bm.x1 - bm.x0;
                BlockMatch mxm{}, mxp{}, mym{}, myp{};
                double sub_x = 0.0, sub_y = 0.0;
                if (match_zncc_block(g0_pre, gi_pre, &sat, Hh, Wh, by, bx, win, win, idy, idx - 1, 0, 1, mxm) &&
                    match_zncc_block(g0_pre, gi_pre, &sat, Hh, Wh, by, bx, win, win, idy, idx + 1, 0, 1, mxp)) {
                    double den = mxm.ncc - 2.0 * bm.ncc + mxp.ncc;
                    if (std::fabs(den) > 1e-6) sub_x = clamp_f((float)(0.5 * (mxm.ncc - mxp.ncc) / den), -0.65f, 0.65f);
                }
                if (match_zncc_block(g0_pre, gi_pre, &sat, Hh, Wh, by, bx, win, win, idy - 1, idx, 0, 1, mym) &&
                    match_zncc_block(g0_pre, gi_pre, &sat, Hh, Wh, by, bx, win, win, idy + 1, idx, 0, 1, myp)) {
                    double den = mym.ncc - 2.0 * bm.ncc + myp.ncc;
                    if (std::fabs(den) > 1e-6) sub_y = clamp_f((float)(0.5 * (mym.ncc - myp.ncc) / den), -0.65f, 0.65f);
                }
                float cand_fy = (float)idy + (float)sub_y;
                float cand_fx = (float)idx + (float)sub_x;

                // Gate 2: Strict Poisson-Gaussian photometric residual verification across the 16x16 patch
                int pass_px = 0, tot_px = 0;
                for (int r = by + 1; r < by + win - 1; ++r) {
                    for (int c = bx + 1; c < bx + win - 1; ++c) {
                        size_t i = (size_t)r * Wh + c;
                        float wv = bilerp_clamped(g_half_n, Hh, Wh, (float)r + cand_fy, (float)c + cand_fx);
                        float iv = std::max(0.0f, ref_w[i]);
                        float sig_p = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv)) + 0.0015f;
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
    });

    // Gate 3: Smooth raised-cosine patch handoff (zero block seams!)
    for (int gr = 0; gr < gH; ++gr) {
        int by = gr * stride;
        for (int gc = 0; gc < gW; ++gc) {
            size_t gi = (size_t)gr * gW + gc;
            float conf = p_ok[gi];
            if (conf <= 0.05f) continue;
            int bx = gc * stride;
            float cfy = p_fy[gi], cfx = p_fx[gi];
            for (int dy = 0; dy < win; ++dy) {
                float wy = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * ((float)dy + 0.5f) / (float)win));
                for (int dx = 0; dx < win; ++dx) {
                    float wx = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * ((float)dx + 0.5f) / (float)win));
                    float w_blend = conf * wy * wx;
                    if (w_blend < 0.12f) continue;
                    size_t i = (size_t)(by + dy) * Wh + (bx + dx);
                    float target_mw = 0.85f * w_frame * conf;
                    if (target_mw > mw_io[i] + 0.10f) {
                        fy_io[i] = (1.0f - w_blend) * fy_io[i] + w_blend * cfy;
                        fx_io[i] = (1.0f - w_blend) * fx_io[i] + w_blend * cfx;
                        mw_io[i] = std::max(mw_io[i], w_blend * target_mw);
                    }
                }
            }
        }
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
    const float* snr_boost_half = nullptr, int Hh = 0, int Wh = 0, int scale_mul = 1
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
    UninitBuf<float> pilot((size_t)H * W);

    // 2D L1/L2 cache-resident padded tile accumulators (32 x 272 x 8B = 69.6 KB/thread -> fits ANY non-X3D 256KB+ L2!)
    const int BAND = 32;
    const int TILE_X = 256;
    const int PAD_X = 8;
    const int STRIDE_X = TILE_X + 2 * PAD_X; // 272 floats (avoids 4K cache set aliasing + zero X-clipping branches!)
    const int max_nt = std::max(1, n_threads);
    std::vector<std::vector<float>> tls_acc(max_nt, std::vector<float>((size_t)BAND * STRIDE_X));
    std::vector<std::vector<float>> tls_wgt(max_nt, std::vector<float>((size_t)BAND * STRIDE_X));

    auto run_pass = [&](bool is_wiener, const float* src_in, const float* guide_in, float* dst_out) {
        const float thr = 3.10f * sigma;
        const float sig2 = 1.35f * sigma * sigma;

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
                        for (int i = 0; i < tw; ++i) {
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

static void upsample_bilinear_2x(const float* src, int Hs, int Ws, float* dst, int Hd, int Wd, int n_threads) {
    parallel_for_rows(Hd, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            float ys = ((float)r + 0.5f) * 0.5f - 0.5f;
            float* drow = dst + (size_t)r * Wd;
            for (int c = 0; c < Wd; ++c) {
                float xs = ((float)c + 0.5f) * 0.5f - 0.5f;
                drow[c] = bilerp_clamped(src, Hs, Ws, ys, xs);
            }
        }
    });
}

// 5-Level Multiscale DCT Empirical Wiener Pyramid + Edge-Stopped GAT Luminance Polish
static void multiscale_dct_denoise_gat(
    float* z_img, int H, int W, int n_threads, float denoise_strength = 1.0f, bool enable_edge_polish = true,
    const float* snr_boost_half = nullptr, int Hh = 0, int Wh = 0
) {
    if (denoise_strength <= 1e-4f) return;
    const int L = 5;
    std::vector<int> Hs(L), Ws(L);
    std::vector<std::vector<float>> pyr(L), den(L);
    // Correlated 2x gather-grid + wavelet subband noise profile across 5 pyramid octaves (up to 128x128 px)
    const float base_sigmas[5] = {1.35f, 1.15f, 0.78f, 0.48f, 0.28f};
    float sigmas[5];
    for (int i = 0; i < L; ++i) sigmas[i] = base_sigmas[i] * denoise_strength;

    Hs[0] = H; Ws[0] = W;
    pyr[0].assign(z_img, z_img + (size_t)H * W);

    for (int s = 1; s < L; ++s) {
        Hs[s] = Hs[s - 1] / 2;
        Ws[s] = Ws[s - 1] / 2;
        pyr[s].resize((size_t)Hs[s] * Ws[s]);
        box_downsample_2x(pyr[s - 1].data(), Hs[s - 1], Ws[s - 1], pyr[s].data(), n_threads);
    }

    for (int s = L - 1; s >= 0; --s) {
        den[s].resize((size_t)Hs[s] * Ws[s]);
        int stride = 2; // 16x overcomplete on all scales for crisp micro-detail and zero block-grid ripple
        dct_wiener_single_scale(pyr[s].data(), den[s].data(), Hs[s], Ws[s], sigmas[s], stride, n_threads,
                                snr_boost_half, Hh, Wh, (1 << s));

        if (s < L - 1) {
            std::vector<float> down_cur((size_t)Hs[s + 1] * Ws[s + 1]);
            box_downsample_2x(den[s].data(), Hs[s], Ws[s], down_cur.data(), n_threads);
            for (size_t i = 0; i < down_cur.size(); ++i) {
                down_cur[i] = den[s + 1][i] - down_cur[i];
            }
            std::vector<float> up_diff((size_t)Hs[s] * Ws[s]);
            upsample_bilinear_2x(down_cur.data(), Hs[s + 1], Ws[s + 1], up_diff.data(), Hs[s], Ws[s], n_threads);

            const float edge_tau = std::max(1e-4f, 3.4f * sigmas[s]);
            parallel_for_rows(Hs[s], n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    size_t off = (size_t)r * Ws[s];
                    for (int c = 0; c < Ws[s]; ++c) {
                        float d = up_diff[off + c];
                        float gate = edge_tau * edge_tau / (edge_tau * edge_tau + d * d);
                        den[s][off + c] += 0.92f * gate * d;
                    }
                }
            });
        }
    }

    float* cur = den[0].data();
    if (enable_edge_polish) {
        // Single fine-scale edge-protected polish on strictly flat regions only (preserves 100% of 2px micro-bars/serifs!)
        const float k1d[5] = {1.0f / 16.0f, 4.0f / 16.0f, 6.0f / 16.0f, 4.0f / 16.0f, 1.0f / 16.0f};
        std::vector<float> tmp((size_t)H * W);
        const float inv_2s2 = 1.0f / (2.0f * (1.05f * 1.05f));
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                size_t roff = (size_t)r * W;
                int rm = clamp_i(r - 1, 0, H - 1), rp = clamp_i(r + 1, 0, H - 1);
                for (int c = 0; c < W; ++c) {
                    int cm = clamp_i(c - 1, 0, W - 1), cp = clamp_i(c + 1, 0, W - 1);
                    float v0 = cur[roff + c];
                    float gx = cur[roff + cp] - cur[roff + cm];
                    float gy = cur[(size_t)rp * W + c] - cur[(size_t)rm * W + c];
                    if (gx * gx + gy * gy > 3.2f) {
                        tmp[roff + c] = v0;
                        continue;
                    }
                    float ws = 0.0f, vs = 0.0f;
                    for (int k = -2; k <= 2; ++k) {
                        float vk = cur[roff + clamp_i(c + k, 0, W - 1)];
                        float d = vk - v0;
                        float w = k1d[k + 2] * fast_neg_exp(d * d * inv_2s2);
                        ws += w; vs += w * vk;
                    }
                    tmp[roff + c] = vs / std::max(ws, 1e-12f);
                }
            }
        });
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                size_t roff = (size_t)r * W;
                int rm = clamp_i(r - 1, 0, H - 1), rp = clamp_i(r + 1, 0, H - 1);
                for (int c = 0; c < W; ++c) {
                    int cm = clamp_i(c - 1, 0, W - 1), cp = clamp_i(c + 1, 0, W - 1);
                    float v0 = tmp[roff + c];
                    float gx = tmp[roff + cp] - tmp[roff + cm];
                    float gy = tmp[(size_t)rp * W + c] - tmp[(size_t)rm * W + c];
                    if (gx * gx + gy * gy > 3.2f) {
                        cur[roff + c] = v0;
                        continue;
                    }
                    float ws = 0.0f, vs = 0.0f;
                    for (int k = -2; k <= 2; ++k) {
                        float vk = tmp[(size_t)clamp_i(r + k, 0, H - 1) * W + c];
                        float d = vk - v0;
                        float w = k1d[k + 2] * fast_neg_exp(d * d * inv_2s2);
                        ws += w; vs += w * vk;
                    }
                    cur[roff + c] = vs / std::max(ws, 1e-12f);
                }
            }
        });
    }

    std::memcpy(z_img, cur, (size_t)H * W * sizeof(float));
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
    UninitBuf<float> lum0, est, conv, edge_gate, lmin_bound, lmax_bound;
    void ensure(size_t npx) {
        lum0.resize(npx);
        est.resize(npx);
        conv.resize(npx);
        edge_gate.resize(npx);
        lmin_bound.resize(npx);
        lmax_bound.resize(npx);
    }
    void release() {
        lum0.release(); est.release(); conv.release();
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
    const auto pc = heraw_gpu::make_olpf_psf_pc(H, W, 0, sigma, olpf_mode);
    std::vector<float> temp((size_t)H * W);
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int y = r0; y < r1; ++y)
            for (int x = 0; x < W; ++x) {
                float sum = pc.w[0] * src[(size_t)y*W+x];
                for (int k = 1; k <= pc.radius; ++k)
                    sum += pc.w[k] * (src[(size_t)y*W+std::max(0,x-k)] + src[(size_t)y*W+std::min(W-1,x+k)]);
                temp[(size_t)y*W+x] = sum;
            }
    });
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int y = r0; y < r1; ++y)
            for (int x = 0; x < W; ++x) {
                float sum = pc.w[0] * temp[(size_t)y*W+x];
                for (int k = 1; k <= pc.radius; ++k)
                    sum += pc.w[k] * (temp[(size_t)std::max(0,y-k)*W+x] + temp[(size_t)std::min(H-1,y+k)*W+x]);
                dst[(size_t)y*W+x] = sum;
            }
    });
}

static void snr_gated_rl_deconv(
    float* rgb, int H, int W, int n_iters, float psf_sigma, float clamp_delta, float noise_floor, int n_threads,
    RlWorkspace& ws, const float* rl_gain_half = nullptr, const float* rl_thrmul_half = nullptr, int Hh = 0, int Wh = 0,
    bool is_astro = false, int olpf_mode = 0
) {
    if (n_iters <= 0) return;
    const size_t Npx = (size_t)H * (size_t)W;
    ws.ensure(Npx);
    float* __restrict lum0 = ws.lum0.data();
    float* __restrict est = ws.est.data();
    float* __restrict conv = ws.conv.data();
    float* __restrict edge_gate = ws.edge_gate.data();
    float* __restrict lmin_bound = ws.lmin_bound.data();
    float* __restrict lmax_bound = ws.lmax_bound.data();

    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < W; ++c) {
                size_t i = (size_t)r * W + c;
                lum0[i] = std::max(1e-6f, 0.2126f * rgb[i*3+0] + 0.7152f * rgb[i*3+1] + 0.0722f * rgb[i*3+2]);
            }
        }
    });

    // Separable 9-tap horizontal + vertical min/max (18 ops vs 81 ops!) + fine-scale structure gate
    sep_gaussian_2d(lum0, conv, H, W, 0.65f, n_threads);
    const float g_thr2 = std::pow(std::max(0.0009f, 2.2f * noise_floor), 2.0f);
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            int rm = clamp_i(r - 1, 0, H - 1), rp = clamp_i(r + 1, 0, H - 1);
            const float* __restrict lrow = lum0 + (size_t)r * W;
            for (int c = 0; c < W; ++c) {
                int cm = clamp_i(c - 1, 0, W - 1), cp = clamp_i(c + 1, 0, W - 1);
                float gx = 0.5f * (conv[(size_t)r * W + cp] - conv[(size_t)r * W + cm]);
                float gy = 0.5f * (conv[(size_t)rp * W + c] - conv[(size_t)rm * W + c]);
                float lap = conv[(size_t)rm * W + c] + conv[(size_t)rp * W + c]
                          + conv[(size_t)r * W + cm] + conv[(size_t)r * W + cp] - 4.0f * conv[(size_t)r * W + c];
                float struct_e2 = gx * gx + gy * gy + 0.35f * lap * lap;
                float rlg = 1.0f, thm = 1.0f;
                if (rl_gain_half && rl_thrmul_half && Hh > 0 && Wh > 0) {
                    size_t hi = (size_t)clamp_i(r >> 2, 0, Hh - 1) * Wh + clamp_i(c >> 2, 0, Wh - 1);
                    rlg = rl_gain_half[hi];
                    thm = rl_thrmul_half[hi];
                }
                edge_gate[(size_t)r * W + c] = rlg * (struct_e2 / (struct_e2 + g_thr2 * thm));

                float hmin = lrow[c], hmax = lrow[c];
                int dx0 = std::max(0, c - 4), dx1 = std::min(W - 1, c + 4);
                for (int cx = dx0; cx <= dx1; ++cx) {
                    float v = lrow[cx];
                    if (v < hmin) hmin = v;
                    if (v > hmax) hmax = v;
                }
                est[(size_t)r * W + c] = hmin;
                lmax_bound[(size_t)r * W + c] = hmax;
            }
        }
    });
    // Vertical 9-tap pass using est (horiz min) and lmax_bound (horiz max) -> conv as scratch for vmax
    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            int dy0 = std::max(0, r - 4), dy1 = std::min(H - 1, r + 4);
            size_t off = (size_t)r * W;
            for (int c = 0; c < W; ++c) {
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
                conv[off + c] = is_astro ? (vmax + 0.06f * span) : (vmax + 0.02f * span);
            }
        }
    });
    std::swap(ws.lmax_bound.ptr, ws.conv.ptr);
    lmax_bound = ws.lmax_bound.data();
    conv = ws.conv.data();

    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        std::memcpy(est + (size_t)r0 * W, lum0 + (size_t)r0 * W, (size_t)(r1 - r0) * W * sizeof(float));
    });
    sep_gaussian_2d(edge_gate, edge_gate, H, W, 1.0f, n_threads);

    const float snr_knee = std::max(0.003f, 3.2f * noise_floor);
    for (int it = 0; it < n_iters; ++it) {
        sep_olpf_2d(est, conv, H, W, psf_sigma, olpf_mode, n_threads);
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < W; ++c) {
                    size_t i = (size_t)r * W + c;
                    float d = lum0[i] / std::max(conv[i], 1e-6f) - 1.0f;
                    float snr_w = (lum0[i] / (lum0[i] + snr_knee)) * edge_gate[i];
                    d = clamp_f(d, -clamp_delta, clamp_delta) * snr_w;
                    conv[i] = 1.0f + d;
                }
            }
        });
        sep_olpf_2d(conv, conv, H, W, psf_sigma, olpf_mode, n_threads);
        parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < W; ++c) {
                    size_t i = (size_t)r * W + c;
                    est[i] = clamp_f(est[i] * conv[i], lmin_bound[i], lmax_bound[i]);
                }
            }
        });
    }

    parallel_for_rows(H, n_threads, [&](int r0, int r1, int) {
        for (int r = r0; r < r1; ++r) {
            for (int c = 0; c < W; ++c) {
                size_t i = (size_t)r * W + c;
                float g = est[i] / lum0[i];
                rgb[i*3 + 0] = std::max(0.0f, rgb[i*3 + 0] * g);
                rgb[i*3 + 1] = std::max(0.0f, rgb[i*3 + 1] * g);
                rgb[i*3 + 2] = std::max(0.0f, rgb[i*3 + 2] * g);
            }
        }
    });
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
    bool progress_json = false;
    std::string motion_mode = "standard";
    std::string olpf_mode = "auto";
    std::string fields_csv = "motion,phase,focus";
    std::string reflection_mode = "off";
    std::string transient_color = "rgb";
    std::string optics_report, export_lcp, dataset_dir;
    std::string lcp_fields = "lca,vignette,distortion";
    std::vector<int> skip_frames;
    bool relay_raw = false, silicon_clean = true, auto_lca = true;
    bool emit_transient = false, export_parallax_html = false, export_refocus = false;
    bool optics_only = false;
    int ibp_iters = 1, strobe_step = 2, tile_size = 256, export_topk = 0;
    float clean_sens = 1.0f, vnd_thresh = 0.65f, strobe_fade = 0.0f;
    float transient_sigma = 3.5f, parallax_amp = 1.0f;
    float refocus_pos = 0.5f, bokeh_strength = 12.0f, tile_min_conf = 0.95f;
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
        case heraw_io::RgbFormat::TIFF_RAW:     return ".tif";
    }
    return ".tif";
}

static void print_cli_help(const char* prog) {
    std::fprintf(stdout,
        "NijiN.LiteBurstSuite (nijinLBS) 2.0.0 -- RAW Imaging Suite\n"
        "Built-in LibRaw 0.23.0-Snapshot202611 (Nikon HE/HE*) + Metal/Vulkan GPU Compute + SVT-JPEG-XS + zlib.\n\n"
        "Usage:\n"
        "  %s [options] <in1.NEF|folder1> [in2.NEF|folder2 ...]\n"
        "  %s --scan-dir <folder> [--recursive] --out-dir <out_folder> [--emit-raw] [--emit-rgb]\n"
        "  %s --decode-jxs <input.jxs> <output.png|output.tif>\n\n"
        "Input, Batch Discovery & Frame Policy Options:\n"
        "  --scan-dir <dir>           Scan directory for RAW files and auto-discover super-resolvable bursts\n"
        "  --recursive                Scan subdirectories recursively when scanning folders\n"
        "  --sort-time / --no-sort-time  Sort frames by EXIF DateTimeOriginal + SubSec + filename (default: ON)\n"
        "  --libraw <lib_path>        Load a custom external LibRaw shared library (.dll / .dylib / .so)\n"
        "  --policy <mode>            Suitability & sequence handling policy (default: auto-prune):\n"
        "                               auto-prune  : Reject blur/motion/exposure outliers; require >= 4 frames\n"
        "                               multi-group : Partition long sequences into multiple >=4-frame SR groups\n"
        "                               force-all   : Force execution down to 1 frame (bypass <4 & motion blocks)\n"
        "                               interactive : Prompt with at most 2 recommended options on anomalies/long bursts\n"
        "  --interactive-choice <1|2> Pre-select option 1 or 2 in --policy interactive (for GUI/scripts)\n"
        "  --group-size <N>           Target frame count per group in multi-group/batch mode (default: 8, min: 4)\n"
        "  --max-frames <K>           Hard cap keeping K sharpest frames per group (0 = unlimited)\n"
        "  --burst-gap-s <sec>        Max EXIF timestamp gap within one candidate burst in batch scan (default: 3.5)\n\n"
        "On-Demand Output Format, Field Map & Color Space Options:\n"
        "  --out-raw <file.dng>       Output 2x 16-bit RGGB Linear RAW DNG path (default: lossless SOF3)\n"
        "  --out-rgb <file>           Output 2x RGB path (.tif, .png, .jxs; default format is 16-bit lossless TIFF)\n"
        "  --out-field <prefix|file>  Output 2x 16-bit Physical Field Map(s) (default: 3 separate grayscale PNGs)\n"
        "  --out-dir <dir>            Batch/multi-group output directory (names files <anchor_stem>_nijinLBS.<ext>)\n"
        "  --emit-raw                 Enable 2x RGGB DNG output when using --out-dir\n"
        "  --emit-rgb                 Enable 2x RGB output when using --out-dir\n"
        "  --emit-field               Enable 2x 16-bit Field Map output when using --out-dir\n"
        "  --field-mode <mode>        Physical Field Map layout (default: split):\n"
        "                               split  : 3 separate 16-bit Grayscale PNGs (_motion/_sky, _phase, _focus) [Default]\n"
        "                               rgb    : 1 packed 16-bit RGB PNG (R=Motion/Sky, G=PhaseSR, B=Focus)\n"
        "                               motion : Only 16-bit Grayscale Motion / Astro Sky-Horizon mask\n"
        "                               phase  : Only 16-bit Grayscale 2x2 Subpixel Phase SR confidence map\n"
        "                               focus  : Only 16-bit Grayscale Optical Focus acutance map\n"
        "  --raw-format <fmt>         DNG RGGB encoding inside .dng (default: dng; size ratio vs uncompressed=1.00):\n"
        "                               dng      : 16-bit Lossless DNG (SOF3 2D, ~0.39x size / 2.6:1, Default)\n"
        "                               dng-linear: 3-channel camera-linear 16-bit lossless SOF3 DNG\n"
        "                               dng-hq   : Advanced Lossy DNG (14->16b + SOF3, ~0.09x size / 11.6:1)\n"
        "                               dng-fp16 : 16-bit IEEE Half-Float HDR DNG (Deflate, ~0.29x size / 3.5:1)\n"
        "                               dng-raw  : 16-bit Uncompressed DNG (1.00x baseline, ~349 MiB)\n"
        "  --rgb-format <fmt>         RGB encoding format (default: tiff-deflate; size ratio vs uncompressed=1.00):\n"
        "                               tiff-deflate : 16-bit Lossless TIFF (Deflate+Predictor2, ~0.39x size, Default)\n"
        "                               png          : 16-bit Lossless PNG + eXIf + sRGB/ICC (~0.40x size)\n"
        "                               jxs          : 14-bit JPEG XS Advanced Lossy (.jxs, ~0.21x size @10bpp)\n"
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
        "  --progress-json            Emit structured JSON progress/result lines on stderr\n"
        "  --threads <N>              Worker CPU threads (default: all logical cores)\n"
        "  -v, --version              Print version banner and exit\n"
        "  -h, --help                 Show this help message and exit\n\n"
        "Burst Frame-Count Guide:\n"
        "  Minimum threshold : 4 valid light frames (2x2 subpixel phase lock, +6.3 dB SNR)\n"
        "  Optimal sweet spot: 6-8 handheld frames (80-92%% phase coverage, +7.6..8.8 dB SNR)\n"
        "  Low-light high-ISO: 9-12 frames (extra shadow shot-noise reduction)\n\n"
        "Exit Codes:\n"
        "  0  Success (or user-confirmed clean cancel)\n"
        "  1  Runtime / decode / I/O error\n"
        "  2  Invalid CLI arguments\n"
        "  3  Suitability policy rejected burst (< 4 suitable frames in non-interactive mode)\n",
        prog ? prog : "nijinLBS", prog ? prog : "nijinLBS", prog ? prog : "nijinLBS");
}

static int execute_sr_group(
    std::vector<UnpackedFrame>& uframes,
    const std::vector<int>& light_indices,
    bool free_all_uframes,
    const std::string& out_raw_path,
    const std::string& out_rgb_path,
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
        if (total_groups > 1) std::fprintf(stderr, "[Group %d/%d] %s\n", group_idx_1based, total_groups, msg);
        else                  std::fprintf(stderr, "%s\n", msg);
        if (cfg.progress_json) {
            int overall_pct = ((group_idx_1based - 1) * 100 + pct) / std::max(1, total_groups);
            std::fprintf(stderr,
                "{\"event\":\"progress\",\"group\":%d,\"total_groups\":%d,\"stage\":%d,\"total_stages\":6,"
                "\"percent\":%d,\"stage_s\":%.3f,\"wall_s\":%.3f,\"message\":\"%s\"}\n",
                group_idx_1based, total_groups, stg, overall_pct, sec, elapsed_s(t_wall0), json_escape(msg).c_str());
        }
        std::fflush(stderr);
    };

    auto t0 = std::chrono::steady_clock::now();
    const int N = (int)light_indices.size();
    const int H1 = uframes[light_indices[0]].H;
    const int W1 = uframes[light_indices[0]].W;
    const int Hh = H1 / 2;
    const int Wh = W1 / 2;
    for (int idx : light_indices) {
        if (uframes[idx].H != H1 || uframes[idx].W != W1) {
            std::fprintf(stderr, "Error: mismatched dimensions across burst frames.\n");
            return 1;
        }
    }

    const UnpackedFrame& meta0 = uframes[light_indices[0]];
    const bool is_xtrans = meta0.is_xtrans;
    const int shutter_kind = meta0.shutter_kind;
    const float focal_mm = meta0.focal_mm, exposure_seconds = meta0.shutter_s;
    const float lens_aperture = meta0.aperture;
    const float sensor_temp_c = meta0.sensor_temp_c;
    const bool ois_active = meta0.has_ois;
    const std::string lens_name = meta0.lens;
    const float focus_distance_m = meta0.focus_distance_m;
    std::vector<double> frame_times;
    for (int idx : light_indices) frame_times.push_back(uframes[idx].timestamp_s);
    std::vector<float> focus_positions;
    for(int idx:light_indices) {
        if(uframes[idx].focus_distance_m<=0) { focus_positions.clear();break; }
        focus_positions.push_back(1.0f/uframes[idx].focus_distance_m);
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
    heraw_core::Execution core_execution;
    core_execution.parallel = [&](int count, const heraw_core::RangeTask& task) {
        parallel_for_rows(count, n_threads, [&](int begin, int end, int) { task(begin, end); }, 1);
    };
    const bool keep_sources = cfg.ibp_iters > 0 || cfg.denoise_strength > 0 || cfg.auto_lca || cfg.emit_transient ||
        cfg.export_refocus || cfg.export_parallax_html || !cfg.dataset_dir.empty() ||
        cfg.motion_mode != "standard" || cfg.reflection_mode != "off" ||
        !out_field_path.empty() || cfg.optics_only || !cfg.optics_report.empty() || !cfg.export_lcp.empty();
    const std::string olpf_profile = cfg.olpf_mode == "auto" ?
        heraw_plan::detect_sensor_olpf_profile(meta0.make, meta0.model, W1, H1, is_xtrans) : cfg.olpf_mode;
    const int olpf_mode = olpf_profile == "gauss16" ? 2 : (olpf_profile == "weak4" ? 1 : 0);
    uint8_t cfa6x6[6][6];
    std::memcpy(cfa6x6, meta0.cfa6x6, sizeof(cfa6x6));
    std::vector<std::string> frame_paths(N);
    for (int n = 0; n < N; ++n) frame_paths[n] = uframes[light_indices[n]].path;
    std::string anchor_raw_path = frame_paths[0];
    const float nom_black = 0.25f * (meta0.black_c[0] + meta0.black_c[1] + meta0.black_c[2] + meta0.black_c[3]);
    const float inv_full_range = 1.0f / std::max(1.0f, meta0.white - nom_black);

    // Blind in-scene G1/G2 block-median pedestal self-calibration + negative pedestal drift guard
    float self_black_delta[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!is_xtrans) {
        const auto& uf0 = uframes[light_indices[0]];
        const int bsz = 32;
        const int nb_y = Hh / bsz, nb_x = Wh / bsz;
        if (nb_y >= 4 && nb_x >= 4) {
            std::array<std::vector<float>, 4> bm;
            for (int p = 0; p < 4; ++p) bm[p].reserve((size_t)nb_y * nb_x);
            std::vector<float> dg21_blocks;
            dg21_blocks.reserve((size_t)nb_y * nb_x);

            for (int by = 0; by < nb_y; ++by) {
                for (int bx = 0; bx < nb_x; ++bx) {
                    double s[4] = {0, 0, 0, 0};
                    for (int r = 0; r < bsz; ++r) {
                        int r_abs = 2 * (by * bsz + r);
                        const uint16_t* row0 = uf0.bayer.data() + (size_t)r_abs * W1 + 2 * bx * bsz;
                        const uint16_t* row1 = row0 + W1;
                        for (int c = 0; c < bsz; ++c) {
                            s[0] += row0[2 * c];
                            s[1] += row0[2 * c + 1];
                            s[2] += row1[2 * c];
                            s[3] += row1[2 * c + 1];
                        }
                    }
                    const double inv_n = 1.0 / (double)(bsz * bsz);
                    float m0 = (float)(s[0] * inv_n) - uf0.black_c[0];
                    float m1 = (float)(s[1] * inv_n) - uf0.black_c[1];
                    float m2 = (float)(s[2] * inv_n) - uf0.black_c[2];
                    float m3 = (float)(s[3] * inv_n) - uf0.black_c[3];
                    bm[0].push_back(m0); bm[1].push_back(m1); bm[2].push_back(m2); bm[3].push_back(m3);
                    if (std::max(m1, m2) < 0.5f * (uf0.white - nom_black)) {
                        dg21_blocks.push_back(m2 - m1);
                    }
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

    const bool is_astro = (cfg.preset == PipelinePreset::ASTRO);

    // Calibrate light frames into signed linear float [-0.1, 1.05] & repair persistent/extreme hot pixels
    // (In Astro mode & on pinpoint speculars, cross-channel 1x Bayer neighbor gate prevents "Star Eater" clipping!)
    std::vector<std::vector<float>> frames_lin(N, std::vector<float>((size_t)H1 * W1));
    std::vector<std::vector<float>> planes_tmp(N, std::vector<float>((size_t)Hh * Wh));
    std::vector<std::vector<float>> meds_tmp(N, std::vector<float>((size_t)Hh * Wh));
    std::vector<uint8_t> persist_cnt((size_t)Hh * Wh, 0);

    if (is_xtrans) {
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
        }
    } else for (int p = 0; p < 4; ++p) {
        int py = p >> 1, px = p & 1;
        std::fill(persist_cnt.begin(), persist_cnt.end(), 0);

        for (int n = 0; n < N; ++n) {
            const auto& uf = uframes[light_indices[n]];
            const float b_const = uf.black_c[p] + self_black_delta[p];
            const float b_avg_u = 0.25f * (uf.black_c[0] + uf.black_c[1] + uf.black_c[2] + uf.black_c[3]);
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    for (int c = 0; c < Wh; ++c) {
                        size_t hi = (size_t)r * Wh + c;
                        float raw_v = (float)uf.bayer[(size_t)(2 * r + py) * W1 + (2 * c + px)];
                        planes_tmp[n][hi] = (raw_v - b_const) * inv_full_range;
                    }
                }
            });
            median3x3_plane(planes_tmp[n].data(), meds_tmp[n].data(), Hh, Wh, n_threads);
            float p_sig = std::max(1e-4f, estimate_hh_sigma(planes_tmp[n].data(), Hh, Wh, 8));
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    int r1 = 2 * r + py;
                    int r1m = clamp_i(r1 - 1, 0, H1 - 1), r1p = clamp_i(r1 + 1, 0, H1 - 1);
                    for (int c = 0; c < Wh; ++c) {
                        size_t hi = (size_t)r * Wh + c;
                        float med_v = meds_tmp[n][hi];
                        float diff = planes_tmp[n][hi] - med_v;
                        if (diff <= 5.5f * p_sig) continue;

                        // Check 4 orthogonal 1x cross-channel Bayer neighbors: real optical stars illuminate neighbors,
                        // whereas single-photodiode sensor hot pixels / cosmic rays leave neighbors at background!
                        int c1 = 2 * c + px;
                        int c1m = clamp_i(c1 - 1, 0, W1 - 1), c1p = clamp_i(c1 + 1, 0, W1 - 1);
                        float nb0 = ((float)uf.bayer[(size_t)r1m * W1 + c1] - b_avg_u) * inv_full_range - med_v;
                        float nb1 = ((float)uf.bayer[(size_t)r1p * W1 + c1] - b_avg_u) * inv_full_range - med_v;
                        float nb2 = ((float)uf.bayer[(size_t)r1 * W1 + c1m] - b_avg_u) * inv_full_range - med_v;
                        float nb3 = ((float)uf.bayer[(size_t)r1 * W1 + c1p] - b_avg_u) * inv_full_range - med_v;
                        float cross_max = std::max(std::max(nb0, nb1), std::max(nb2, nb3));
                        bool optical_pinpoint = cross_max > (is_astro ? 0.14f : 0.24f) * diff;
                        if (optical_pinpoint) continue;

                        persist_cnt[hi]++;
                        if (diff > 9.5f * p_sig) planes_tmp[n][hi] = med_v;
                    }
                }
            });
        }

        int min_persist = std::max(2, (N + 1) / 2);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            size_t i0 = (size_t)r0 * Wh, i1 = (size_t)r1 * Wh;
            for (size_t hi = i0; hi < i1; ++hi) {
                if (N >= 3 && persist_cnt[hi] >= min_persist) {
                    for (int n = 0; n < N; ++n) planes_tmp[n][hi] = meds_tmp[n][hi];
                }
            }
        });

        for (int n = 0; n < N; ++n) {
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    for (int c = 0; c < Wh; ++c) {
                        frames_lin[n][(size_t)(2 * r + py) * W1 + (2 * c + px)] = planes_tmp[n][(size_t)r * Wh + c];
                    }
                }
            });
        }
    }
    planes_tmp.clear(); planes_tmp.shrink_to_fit();
    meds_tmp.clear(); meds_tmp.shrink_to_fit();
    persist_cnt.clear(); persist_cnt.shrink_to_fit();

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
    for (int n = 0; n < N; ++n) {
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wh; ++c) {
                    float sum = 0.0f;
                    int count = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            int y = 2*r+dy, x = 2*c+dx;
                            if (cfa6x6[y % 6][x % 6] == 1) {
                                sum += frames_lin[n][(size_t)y * W1 + x];
                                ++count;
                            }
                        }
                    g_halfs[n][(size_t)r * Wh + c] = count ? sum / count :
                        sample_cfa_channel(frames_lin[n].data(), H1, W1, 2*r, 2*c, 1, cfa6x6);
                }
            }
        });
        std::vector<float> gb((size_t)Hh * Wh);
        sep_gaussian_2d(g_halfs[n].data(), gb.data(), Hh, Wh, 1.1f, n_threads);
        double se = 0.0;
        int64_t scnt = 0;
        for (int r = Hh / 5; r < 4 * Hh / 5; ++r) {
            for (int c = Wh / 5; c < 4 * Wh / 5; ++c) {
                float lap = -4.0f * gb[(size_t)r * Wh + c]
                          + gb[(size_t)(r - 1) * Wh + c] + gb[(size_t)(r + 1) * Wh + c]
                          + gb[(size_t)r * Wh + c - 1]   + gb[(size_t)r * Wh + c + 1];
                se += (double)lap * (double)lap;
                ++scnt;
            }
        }
        sharp_scores[n] = se / (double)std::max<int64_t>(1, scnt);
    }

    // The planner has placed the selected AF/explicit anchor at index zero.

    // Blind Poisson-Gaussian noise estimation on single-photodiode linear sensor scale:
    // Var_1px(I) = sig0^2 + g_shot * max(0, I)
    std::vector<float> g0_smooth((size_t)Hh * Wh);
    sep_gaussian_2d(g_halfs[0].data(), g0_smooth.data(), Hh, Wh, 2.5f, n_threads);
    const int NBINS = 12;
    std::vector<float> g_samples;
    for (size_t i = 0; i < g0_smooth.size(); i += 17) g_samples.push_back(g0_smooth[i]);
    std::vector<float> bin_edges(NBINS + 1);
    for (int b = 0; b <= NBINS; ++b) bin_edges[b] = quick_percentile(g_samples, (float)b / (float)NBINS);

    std::vector<double> fit_A, fit_y;
    for (int b = 0; b < NBINS; ++b) {
        float lo = bin_edges[b], hi = bin_edges[b + 1] + 1e-6f;
        std::vector<float> hh_bin;
        double i_sum = 0.0;
        for (int r = 2; r + 2 < Hh; r += 2) {
            for (int c = 2; c + 2 < Wh; c += 2) {
                float iv = g0_smooth[(size_t)r * Wh + c];
                if (iv >= lo && iv <= hi) {
                    // Single-photodiode G1 HH difference
                    int yy = 2*r, xx = 2*c+1, step = is_xtrans ? 6 : 2;
                    if (is_xtrans) {
                        while (xx + step < W1 && cfa6x6[yy % 6][xx % 6] != 1) ++xx;
                    }
                    if (yy + step >= H1 || xx + step >= W1) continue;
                    float v00 = frames_lin[0][(size_t)yy * W1 + xx];
                    float v01 = frames_lin[0][(size_t)yy * W1 + xx+step];
                    float v10 = frames_lin[0][(size_t)(yy+step) * W1 + xx];
                    float v11 = frames_lin[0][(size_t)(yy+step) * W1 + xx+step];
                    hh_bin.push_back(std::fabs(0.5f * (v00 - v01 - v10 + v11)));
                    i_sum += std::max(0.0f, iv);
                }
            }
        }
        if (hh_bin.size() >= 64) {
            // Lower 15th percentile converted to Gaussian std avoids high-frequency textured regions (Foi 2008)
            float s_est = quick_percentile(hh_bin, 0.15f) / 0.190f;
            double mean_i = i_sum / (double)hh_bin.size();
            fit_A.push_back(1.0);
            fit_A.push_back(mean_i);
            fit_y.push_back((double)s_est * (double)s_est);
        }
    }
    double pg_sig0_sq = 1e-6, pg_gain = 1e-4;
    std::vector<double> pg_coef;
    if (fit_y.size() >= 3 && solve_least_squares((int)fit_y.size(), 2, fit_A, fit_y, pg_coef)) {
        pg_sig0_sq = std::max(1e-8, pg_coef[0]);
        pg_gain = std::max(1e-6, pg_coef[1]);
    } else {
        float s_all = estimate_hh_sigma(g_halfs[0].data(), Hh, Wh, 2) * std::sqrt(2.0f);
        pg_sig0_sq = (double)s_all * (double)s_all;
        pg_gain = 0.05 * std::sqrt(pg_sig0_sq);
    }
    const float sig0_1px = (float)std::sqrt(pg_sig0_sq);
    {
        char msg2[220];
        double dt2 = elapsed_s(t0);
        std::snprintf(msg2, sizeof(msg2),
            "[2/6] Calibrated %d light frame(s) | blind PG noise: sig0=%.5f, gain=%.5f (%.2fs)",
            N, sig0_1px, (float)pg_gain, dt2);
        emit_stage(2, 26, dt2, msg2);
    }

    // --- Stage 3: Subpixel Pyramid Alignment + Motion/Parallax De-Ghosting ---
    t0 = std::chrono::steady_clock::now();
    heraw_core::Noise core_noise{(float)pg_sig0_sq, (float)pg_gain};
    heraw_core::ExposureStats exposure_stats;
    if (bracket_exposures) {
        exposure_stats = heraw_core::exposure_normalize(frames_lin, g_halfs, H1, W1, core_noise, {}, {}, {}, core_execution);
        core_noise.frame_read_variances = exposure_stats.read_variances.data();
        core_noise.frame_shot_coefficients = exposure_stats.shot_coefficients.data();
        core_noise.frame_saturation_levels = exposure_stats.saturation_levels.data();
        std::fprintf(stderr, "[C7] Exposure-normalized %d frames using %zu linear samples.\n", N, exposure_stats.samples);
    }
    std::vector<std::vector<float>> flows_fy(N), flows_fx(N), mw_half(N);
    flows_fy[0].assign((size_t)Hh * Wh, 0.0f);
    flows_fx[0].assign((size_t)Hh * Wh, 0.0f);
    mw_half[0].assign((size_t)Hh * Wh, 1.0f);

    float pre_sigma = clamp_f(0.45f + 12.0f * sig0_1px, 0.5f, 1.6f);
    std::vector<float> g0_pre((size_t)Hh * Wh);
    sep_gaussian_2d(g_halfs[0].data(), g0_pre.data(), Hh, Wh, pre_sigma, n_threads);

    std::vector<double> sharp_sorted = sharp_scores;
    std::sort(sharp_sorted.begin(), sharp_sorted.end());
    double med_sharp = sharp_sorted[N / 2];

    std::vector<std::vector<float>> gi_pres(N);
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
        align_frame_pyramid(g0_pre.data(), gi_pres[n].data(), Hh, Wh, flows_fy[n], flows_fx[n], q, n_threads, !is_astro);
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

        for (int n = 1; n < N; ++n) {
            std::vector<float> warped((size_t)Hh * Wh);
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

        std::vector<float> ref_f((size_t)Hh * Wh), ref_w((size_t)Hh * Wh);
        std::vector<float> grad_f((size_t)Hh * Wh), grad_w((size_t)Hh * Wh);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                int rm = clamp_i(r - 1, 0, Hh - 1), rp = clamp_i(r + 1, 0, Hh - 1);
                for (int c = 0; c < Wh; ++c) {
                    int cm = clamp_i(c - 1, 0, Wh - 1), cp = clamp_i(c + 1, 0, Wh - 1);
                    size_t i = (size_t)r * Wh + c;
                    float iv = std::max(0.0f, wg_wide[0][i]);
                    float sf_1 = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv)) * 0.35f + 0.001f;
                    float sw_1 = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv)) * 0.14f + 0.0006f;

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
        for (int n = 1; n < N; ++n) {
            double s_ratio = sharp_scores[n] / std::max(1e-12, med_sharp);
            float w_frame = (s_ratio < 0.97) ? clamp_f((float)std::pow(s_ratio, 4.0), 0.30f, 1.0f) : 1.0f;
            w_frame_sum += w_frame;

            std::vector<float> bad((size_t)Hh * Wh, 0.0f);
            parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    for (int c = 0; c < Wh; ++c) {
                        size_t i = (size_t)r * Wh + c;
                        float iv = std::max(0.0f, ref_w[i]);
                        float sf = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv)) * 0.32f + 0.0012f;
                        float sw = std::sqrt(0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv)) * 0.13f + 0.0007f;
                        float zf = (std::fabs(wg_fine[n][i] - ref_f[i]) - grad_tol_f * grad_f[i]) / sf;
                        float zw = (std::fabs(wg_wide[n][i] - ref_w[i]) - 0.55f * grad_w[i]) / sw;
                        if (zf > zf_lim || zw > zw_lim) bad[i] = 1.0f;
                    }
                }
            });
            std::vector<float> bad_clean((size_t)Hh * Wh), bad_closed((size_t)Hh * Wh), bad_blur((size_t)Hh * Wh);
            median3x3_plane(bad.data(), bad_clean.data(), Hh, Wh, n_threads);
            if (!is_astro) {
                // Morphological 1/4-scale closing eliminates hollow "donut" leakage inside moving coats/trains/shadows
                morph_close_4x_field(bad_clean.data(), bad_closed.data(), Hh, Wh, n_threads);
                for (size_t i = 0; i < (size_t)Hh * Wh; ++i) {
                    bad_clean[i] = std::max(bad_clean[i], 0.85f * bad_closed[i]);
                }
            }
            sep_gaussian_2d(bad_clean.data(), bad_blur.data(), Hh, Wh, is_astro ? 2.0f : 3.0f, n_threads);

            mw_half[n].resize((size_t)Hh * Wh);
            for (size_t i = 0; i < (size_t)Hh * Wh; ++i) {
                float m = clamp_f(1.0f - 2.2f * bad_blur[i], 0.0f, 1.0f);
                mw_half[n][i] = w_frame * m;
            }

            // 3-Gate Rigid Micro-Patch Subpixel Recovery on localized rigid moving subjects
            if (!is_astro && !gi_pres[n].empty()) {
                recover_rigid_motion_patches(
                    g0_pre.data(), gi_pres[n].data(), g_halfs[n].data(),
                    ref_f.data(), ref_w.data(), grad_f.data(), grad_w.data(),
                    Hh, Wh, pg_sig0_sq, pg_gain, zf_lim, zw_lim, w_frame,
                    flows_fy[n], flows_fx[n], mw_half[n], n_threads
                );
            }
        }
        gi_pres.clear(); gi_pres.shrink_to_fit();

        // Temporal-difference broadband noise calibration (immune to lossy RAW spatial wavelet correlation!)
        std::vector<double> t_A, t_y;
        for (int b = 0; b < NBINS; ++b) {
            float lo = bin_edges[b], hi = bin_edges[b + 1] + 1e-6f;
            std::vector<float> td_bin;
            double i_sum = 0.0;
            for (int r = 8; r + 8 < Hh; r += 4) {
                for (int c = 8; c + 8 < Wh; c += 4) {
                    size_t i = (size_t)r * Wh + c;
                    float iv = ref_w[i];
                    if (iv >= lo && iv <= hi && mw_half[1][i] > 0.85f && grad_f[i] < 0.015f) {
                        float w1 = bilerp_clamped(g_halfs[1].data(), Hh, Wh, (float)r + flows_fy[1][i], (float)c + flows_fx[1][i]);
                        td_bin.push_back(std::fabs(w1 - g_halfs[0][i]));
                        i_sum += std::max(0.0f, iv);
                    }
                }
            }
            if (td_bin.size() >= 64) {
                float s_td = quick_percentile(td_bin, 0.50f) / 0.67448975f;
                double v_1px = 1.25 * (double)s_td * (double)s_td;
                t_A.push_back(1.0);
                t_A.push_back(i_sum / (double)td_bin.size());
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
                            int qy = ((int)std::llround(flows_fy[n][i] * 4.0f)) & 1;
                            int qx = ((int)std::llround(flows_fx[n][i] * 4.0f)) & 1;
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
                    float n_g2 = (0.16f / w_tot) * ((float)pg_sig0_sq + (float)pg_gain * std::max(0.0f, ref_w[i])) + 1e-7f;
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

    if (shutter_kind == 1 && N > 1) {
        auto stats = heraw_core::row_rolling_shutter_smooth(flows_fy, flows_fx, mw_half, Hh, Wh, 0.5f, core_execution);
        std::fprintf(stderr, "[C10] Row-flow refinement RMS %.6f half-px.\n", stats.rms_update);
    }
    core_noise.read_variance = (float)pg_sig0_sq;
    core_noise.shot = (float)pg_gain;
    if (bracket_exposures) {
        for (int n=0; n<N; ++n) {
            float gain=exposure_stats.gains[n];
            exposure_stats.read_variances[n]=(float)pg_sig0_sq*gain*gain;
            exposure_stats.shot_coefficients[n]=(float)pg_gain*gain;
        }
    }
    if (bracket_exposures) {
        for (int n = 0; n < N; ++n)
            for (int y = 0; y < Hh; ++y)
                for (int x = 0; x < Wh; ++x) {
                    size_t i = (size_t)y*Wh+x;
                    float v = bilerp_clamped(g_halfs[n].data(), Hh, Wh, y+flows_fy[n][i], x+flows_fx[n][i]);
                    float best_variance = core_noise.variance(v);
                    float variance = core_noise.frame_variance(n, v);
                    mw_half[n][i] *= core_noise.unsaturated(n, v) ? std::min(4.0f, best_variance/variance) : 0.0f;
                }
    }
    if (cfg.silicon_clean && N >= 3) {
        auto stats = heraw_core::silicon_residual_cleanup(frames_lin, H1, W1, &cfa6x6[0][0],
            flows_fy, flows_fx, mw_half, core_noise, nullptr, core_execution);
        if (stats.changed) {
            for (int n = 0; n < N; ++n)
                parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
                    for (int y = r0; y < r1; ++y)
                        for (int x = 0; x < Wh; ++x) {
                            float sum = 0; int count = 0;
                            for (int dy = 0; dy < 2; ++dy)
                                for (int dx = 0; dx < 2; ++dx) {
                                    int yy=2*y+dy, xx=2*x+dx;
                                    if (cfa6x6[yy%6][xx%6] == 1) { sum += frames_lin[n][(size_t)yy*W1+xx]; ++count; }
                                }
                            g_halfs[n][(size_t)y*Wh+x] = sum/std::max(1,count);
                        }
                });
        }
        std::fprintf(stderr, "[C13] Sensor-coordinate correction: %zu samples, %zu updates, RMS %.7f.\n",
            stats.samples, stats.changed, stats.rms_update);
    }

    heraw_optics::OpticsReport optics;
    std::vector<float> frame_ois_shifts((size_t)N*4,0.0f);
    float lca_coeffs[4]={0,0,0,0};
    if (cfg.auto_lca || cfg.optics_only || !cfg.optics_report.empty() || !cfg.export_lcp.empty()) {
        heraw_optics::Input input{frames_lin,g_halfs,flows_fx,flows_fy,mw_half,&cfa6x6[0][0],H1,W1,frame_times};
        heraw_optics::Options opts;
        opts.camera=cam_make+" "+cam_model; opts.lens=lens_name;
        opts.focal_mm=focal_mm; opts.aperture=lens_aperture; opts.focus_m=focus_distance_m;
        opts.noise_sigma=std::sqrt(pg_sig0_sq); opts.ois=ois_active;
        opts.stars=is_astro || astro_stars0.size()>=3; opts.far_field=!opts.stars;
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
                std::filesystem::u8path(anchor_raw_path).replace_extension().u8string()+"_optics" : cfg.optics_report;
            ensure_parent_dir_utf8(prefix+"_telemetry.json");
            heraw_suite::BurstView geometry_input{frames_lin,g_halfs,flows_fy,flows_fx,mw_half,H1,W1,
                cfa6x6,(float)pg_gain,(float)pg_sig0_sq};
            const auto geometry=heraw_suite::analyze_geometry(geometry_input);
            std::string telemetry=heraw_optics::report_json(optics);
            telemetry.erase(telemetry.find_last_of('}'));
            const bool has_focus_steps=focus_positions.size()==(size_t)N && N>=3 &&
                *std::max_element(focus_positions.begin(),focus_positions.end())-
                *std::min_element(focus_positions.begin(),focus_positions.end())>1e-6f;
            telemetry+=",\"sigma0\":"+heraw_optics::detail::number(std::sqrt(pg_sig0_sq))+
                ",\"gain_g\":"+heraw_optics::detail::number(pg_gain)+
                ",\"sensor_temp_c\":"+(sensor_temp_c>=-80?std::to_string(sensor_temp_c):"null")+
                ",\"max_parallax_2x_px\":"+std::to_string(2*geometry.disparity_span_px)+
                ",\"depth_observable\":"+(geometry.gate.observable?"true":"false")+
                ",\"field_observability\":{\"depth\":"+(geometry.gate.observable?"true":"false")+
                ",\"defocus\":"+(has_focus_steps?"true":"false")+
                ",\"turbulence\":"+((N>=8&&focal_mm>=85&&focus_distance_m>50)?"true":"false")+
                ",\"flicker\":"+((N>=4&&exposure_seconds>0&&exposure_seconds<1.0f/125)?"true":"false")+
                ",\"coherence\":"+(N>=4?"true":"false")+",\"velocity\":"+(N>=3?"true":"false")+"}}\n";
            FILE* report=fopen_utf8_wb(prefix+"_telemetry.json");
            if(!report) return 1;
            bool written=std::fwrite(telemetry.data(),1,telemetry.size(),report)==telemetry.size();
            if(std::fclose(report)!=0 || !written) return 1;
            heraw_optics::write_passport_png(prefix+"_passport.png",optics);
            if(!cfg.export_lcp.empty()) {
                if(!(optics.lca.channels[0].valid && optics.lca.channels[1].valid) &&
                    !optics.distortion.valid && !optics.vignette.valid) {
                    std::fprintf(stderr,"[W9] Optical report saved; additional diverse edges are required for a correction profile.\n");
                    if(cfg.optics_only) return 3;
                } else {
                    ensure_parent_dir_utf8(cfg.export_lcp);
                    auto selected=optics;
                    const std::string fields=","+cfg.lcp_fields+",";
                    if(fields.find(",lca,")==std::string::npos)
                        for(auto& channel:selected.lca.channels) channel.valid=false;
                    if(fields.find(",vignette,")==std::string::npos) selected.vignette.valid=false;
                    if(fields.find(",distortion,")==std::string::npos) selected.distortion.valid=false;
                    if(!(selected.lca.channels[0].valid && selected.lca.channels[1].valid) &&
                        !selected.vignette.valid && !selected.distortion.valid) {
                        std::fprintf(stderr,"[W9] Selected correction models require additional calibration observations.\n");
                        if(cfg.optics_only) return 3;
                    } else heraw_optics::write_lcp(cfg.export_lcp,selected);
                }
            }
            std::fprintf(stderr,"[W8/W10] Optical telemetry and passport: %s\n",prefix.c_str());
        }
        if(cfg.optics_only) return 0;
    }

    heraw_suite::BurstView suite_view{frames_lin, g_halfs, flows_fy, flows_fx, mw_half,
        H1, W1, cfa6x6, (float)pg_gain, (float)pg_sig0_sq};
    suite_view.times_seconds = &frame_times;
    if(focus_positions.size()==(size_t)N) suite_view.focus_positions=&focus_positions;
    suite_view.exposure_seconds = exposure_seconds;
    suite_view.focal_length_mm = focal_mm;
    suite_view.distant_scene = focus_distance_m > 50.0f;
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
    if (cfg.export_refocus || cfg.export_parallax_html || cfg.reflection_mode=="split-layers")
        if (std::find(wanted_fields.begin(),wanted_fields.end(),"depth")==wanted_fields.end())
            wanted_fields.push_back("depth");
    if (cfg.reflection_mode!="off" && std::find(wanted_fields.begin(),wanted_fields.end(),"specular")==wanted_fields.end())
        wanted_fields.push_back("specular");
    if (!cfg.dataset_dir.empty() && std::find(wanted_fields.begin(),wanted_fields.end(),"sigma")==wanted_fields.end())
        wanted_fields.push_back("sigma");
    auto suite_fields = heraw_suite::compute_fields(suite_view, wanted_fields);
    std::vector<std::pair<std::string,heraw_suite::Image>> extra_images;
    std::vector<float> temporal_anchor;
    heraw_suite::TemporalResult primary_temporal;
    heraw_suite::Image temporal_composite;
    heraw_suite::Image reflection_composite;
    const bool temporal_primary = cfg.motion_mode!="standard";
    if (temporal_primary) {
        heraw_suite::TemporalOptions opts;
        opts.mode = cfg.motion_mode=="clean-plate" ? heraw_suite::TemporalMode::CleanPlate :
            (cfg.motion_mode=="virtual-nd" ? heraw_suite::TemporalMode::VirtualND : heraw_suite::TemporalMode::Strobe);
        opts.reject_sigma = 3.5f/std::max(0.1f,cfg.clean_sens);
        opts.strobe_stride = (size_t)cfg.strobe_step;
        opts.strobe_opacity = 1.0f;
        opts.strobe_fade = cfg.strobe_fade;
        primary_temporal = heraw_suite::prepare_temporal(suite_view, opts);
        std::fprintf(stderr, "[W] %s: %s (%g %s).\n", cfg.motion_mode.c_str(),
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
            std::fprintf(stderr,"[W3] %s.\n",selection.gate.reason.c_str()); return 3;
        }
    }
    if (cfg.reflection_mode!="off") {
        heraw_suite::TemporalOptions opts;
        opts.mode=cfg.reflection_mode=="split-layers" ? heraw_suite::TemporalMode::Reflection : heraw_suite::TemporalMode::Glint;
        opts.reflection_disparity_px=suite_fields.geometry.disparity_span_px;
        auto selection=heraw_suite::prepare_temporal(suite_view,opts);
        if (selection.gate.observable) {
            auto render=heraw_suite::render_temporal(suite_view,selection,H1,W1,core_execution.parallel);
            reflection_composite=std::move(render.image);
            if (cfg.reflection_mode=="split-layers") extra_images.emplace_back("reflection",std::move(render.layer));
        } else {
            std::fprintf(stderr,"[W5] %s (%g %s).\n",selection.gate.reason.c_str(),
                selection.gate.measured,selection.gate.units.c_str()); return 3;
        }
    }
    if (temporal_primary) {
        if (cfg.motion_mode=="clean-plate" || cfg.motion_mode=="strobe" || cfg.motion_mode=="virtual-nd") {
            auto render=heraw_suite::render_temporal(suite_view,primary_temporal,H1,W1,core_execution.parallel);
            if (cfg.motion_mode!="clean-plate") temporal_composite=std::move(render.image);
            else {
                temporal_anchor.resize((size_t)H1*W1);
                for (int y=0;y<H1;++y) for (int x=0;x<W1;++x)
                    temporal_anchor[(size_t)y*W1+x]=render.image.rgb[((size_t)y*W1+x)*3+cfa6x6[y%6][x%6]];
            }
        }
        if (cfg.motion_mode!="strobe") {
            heraw_suite::materialize_camera_flows(primary_temporal,flows_fy,flows_fx);
            if (cfg.motion_mode=="virtual-nd") {
                for (int n=0;n<N;++n) for (size_t i=0;i<mw_half[n].size();++i) {
                    float a=clamp_f((field_motion[i]-cfg.vnd_thresh)*8.0f,0.0f,1.0f);
                    primary_temporal.weights[n][i]=mw_half[n][i]*(1-a)+primary_temporal.weights[n][i]*a;
                }
            }
            mw_half=std::move(primary_temporal.weights);
        }
    }

    // Build Hamilton-Adams 2nd-order Laplacian directional Green (using multi-frame denoised Bayer on zero-shift static pixels!)
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

    // --- Stage 4: Anisotropic Quincunx Green + Residual-Guided R/B 2x Burst Reconstruction ---
    t0 = std::chrono::steady_clock::now();
    // Build motion-weighted temporal-mean Green guide for structure tensor
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
    if (temporal_primary && cfg.motion_mode!="strobe") g_guide=primary_temporal.guide;
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
                float n_var_1f = 0.5f * ((float)pg_sig0_sq + (float)pg_gain * iv);
                float n_var = n_var_1f / (float)N;
                float edge_s = clamp_f((lam1 - 1.3f * n_var) / (5.0f * n_var + 1e-6f), 0.0f, 1.0f);
                float anis = clamp_f((lam1 - lam2) / (lam1 + lam2 + n_var + 1e-7f), 0.0f, 1.0f) * edge_s;

                // Optical Focus Field on Anchor Frame 0 via 2nd-order Laplacian high-frequency SNR
                // (Smooth bokeh shading waves have near-zero 2nd derivative; crisp optical edges/textures spike!)
                float gx0 = 0.5f * (g0_pre[(size_t)r * Wh + cp] - g0_pre[(size_t)r * Wh + cm]);
                float gy0 = 0.5f * (g0_pre[(size_t)rp * Wh + c] - g0_pre[(size_t)rm * Wh + c]);
                float e0_sq = gx0 * gx0 + gy0 * gy0;
                edge_pres_half[i] = clamp_f((e0_sq - 1.5f * n_var_1f) / (8.0f * n_var_1f + 1e-6f), 0.0f, 1.0f);

                float lap0 = 4.0f * g0_pre[i]
                           - g0_pre[(size_t)rm * Wh + c] - g0_pre[(size_t)rp * Wh + c]
                           - g0_pre[(size_t)r * Wh + cm] - g0_pre[(size_t)r * Wh + cp];
                focus_raw[i] = lap0 * lap0;

                float ang = 0.5f * std::atan2(2.0f * jxy[i], jxx[i] - jyy[i]);
                float ca = std::cos(ang), sa = std::sin(ang);
                float s_perp = iso_rad * (1.0f - anis) + 0.28f * anis;
                float s_para = iso_rad * (1.0f - anis) + 0.64f * anis;
                float ip = 1.0f / (s_perp * s_perp);
                float iq = 1.0f / (s_para * s_para);

                const float delta = ip - iq;
                inv_cov[i * 3 + 0] = iq + delta * ca * ca;
                inv_cov[i * 3 + 1] = delta * ca * sa;
                inv_cov[i * 3 + 2] = iq + delta * sa * sa;
            }
        }
    });
    g0_pre.clear(); g0_pre.shrink_to_fit();
    {
        // Regional Focus Envelope (1/4-scale 4x4 block-averaged Laplacian energy -> noise gate -> 3x3 dilation):
        // 4x4 averaging kills Gaussian chi-square tail noise on bokeh while preserving in-focus optical structures.
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
                    float n_var_1f = 0.5f * ((float)pg_sig0_sq + (float)pg_gain * (s_iv * inv_c));
                    float hf_sig = std::max(0.0f, mean_l2 - 20.0f * n_var_1f);
                    fq0[(size_t)r * Wq + c] = clamp_f(hf_sig / (hf_sig + 16.0f * n_var_1f + 1.0e-4f), 0.0f, 1.0f);
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
    if (N >= 2 && cfg.motion_mode == "standard") {
        heraw_core::Frames lucky;
        heraw_core::directional_lucky_weights(g_halfs, Hh, Wh, flows_fy, flows_fx, core_noise, lucky, core_execution);
        for (int n = 0; n < N; ++n)
            for (size_t i = 0; i < (size_t)Hh*Wh; ++i) {
                float angle = 0.5f * std::atan2(2.0f*inv_cov[3*i+1], inv_cov[3*i]-inv_cov[3*i+2]);
                int direction = ((int)std::lround(angle*(4.0f/3.14159265359f))+4)%4;
                mw_half[n][i] *= lucky[n][4*i+direction];
            }
    }

    // Coupled Field Modulation Maps (preventing background over-sharpening & cutout detachment):
    // 1) Edge-protected wide-ramp snr_boost_half: 1.00 on subject edges, gentle <=1.38x on flat moving areas
    // 2) Focus-coupled RL gate: suppresses RL on out-of-focus bokeh while preserving crisp RL on in-focus subjects
    std::vector<float> snr_boost_half((size_t)Hh * Wh, 1.0f);
    std::vector<float> rl_gain_half((size_t)Hh * Wh, 1.0f);
    std::vector<float> rl_thrmul_half((size_t)Hh * Wh, 1.0f);
    std::vector<uint32_t> rl_mod_f16((size_t)Hh * Wh, 0);
    {
        std::vector<float> raw_boost((size_t)Hh * Wh, 1.0f);
        parallel_for_rows(Hh, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < Wh; ++c) {
                    size_t i = (size_t)r * Wh + c;
                    float mw_w = m_wide_half[i];
                    float ep = edge_pres_half[i];
                    // Exempt multi-frame phase-coherent micro-textures from GAT-DCT Wiener over-smoothing
                    float coh_exempt = 1.0f - 0.52f * field_coherence[i];
                    raw_boost[i] = coh_exempt * (1.0f + 0.38f * mw_w * (1.0f - 0.85f * ep));

                    float foc = field_focus[i];
                    float foc_t = clamp_f((foc - 0.04f) / 0.36f, 0.0f, 1.0f);
                    float foc_smooth = foc_t * foc_t * (3.0f - 2.0f * foc_t);
                    // High-N low-phase tripod bonus: exploit clean multi-frame SNR for extra optical PSF recovery
                    float tripod_bonus = (N >= 4 && field_motion[i] < 0.05f && field_phase[i] < 0.58f) ? 1.18f : 1.0f;
                    // Bokeh stays unsharpened (0.18); in-focus static & moving subjects stay crisp (0.86..1.18)
                    float rlg = tripod_bonus * (0.18f + 0.82f * foc_smooth) * (1.0f - 0.14f * mw_w);
                    float thm = 1.0f + 1.8f * field_motion[i];
                    rl_gain_half[i] = rlg;
                    rl_thrmul_half[i] = thm;
                    rl_mod_f16[i] = heraw_gpu::pack_half2_u32(rlg, thm);
                }
            }
        });
        sep_gaussian_2d(raw_boost.data(), snr_boost_half.data(), Hh, Wh, 2.0f, n_threads);
    }
    edge_pres_half.clear(); edge_pres_half.shrink_to_fit();
    field_coherence.clear(); field_coherence.shrink_to_fit();
    m_wide_half.clear(); m_wide_half.shrink_to_fit();

    const int H2 = H1 * 2;
    const int W2 = W1 * 2;
    const bool reconstruct_rgb = !out_raw_path.empty() || !out_rgb_path.empty() ||
        cfg.export_refocus || cfg.export_parallax_html;
    UninitBuf<float> rgb_2x(reconstruct_rgb ? (size_t)H2 * (size_t)W2 * 3 : 0);
    RlWorkspace rl_ws;
    double eta_eff = 0.25, g_eff = 0.0, sig_eff_sq = 1e-10;
    float resid_noise_2x = 0.0f;
    int compute_tiles=0;
    size_t compute_peak_pixels=0;
    if (reconstruct_rgb) {
        heraw_gpu::Stage45GpuJob gjob;
        gjob.H1 = H1; gjob.W1 = W1; gjob.N = N; gjob.n_threads = n_threads;
        gjob.is_clean_plate = cfg.motion_mode == "clean-plate";
        gjob.is_virtual_nd = false;
        gjob.is_astro = is_astro;
        gjob.is_xtrans = is_xtrans;
        std::memcpy(gjob.cfa6x6, cfa6x6, sizeof(cfa6x6));
        gjob.keep_frames_lin = true;
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
        gjob.wb_r = cam_wb[0];
        gjob.wb_b = cam_wb[2];
        gjob.denoise_strength = cfg.denoise_strength;
        gjob.chroma_denoise = cfg.chroma_denoise;
        gjob.psf_iters = cfg.psf_iters;
        gjob.psf_sigma = cfg.psf_sigma;
        gjob.edge_polish = cfg.edge_polish;
        gjob.glint_desat = cfg.glint_desat;
        gjob.rgb_2x_out = rgb_2x.data();

        const auto t_stage45 = std::chrono::steady_clock::now();
        double stage4_seconds = 0.0;
        gjob.on_stage4_done = [&](double eta_val, double dt4, const std::string& dev_lbl) {
            stage4_seconds = dt4;
            char msg4[240];
            std::snprintf(msg4, sizeof(msg4),
                "[4/6] Anisotropic quincunx + residual 2x fusion (%dx%d) [%s] done in %.2fs (eta=%.4f)",
                W2, H2, dev_lbl.c_str(), dt4, eta_val);
            emit_stage(4, 65, dt4, msg4);
        };

        bool stage45_done = false;
        if (cfg.backend != heraw_gpu::BackendPref::CPU) {
            stage45_done = heraw_gpu::run_stage45_gpu(gjob);
            if (!stage45_done) {
                if (cfg.backend == heraw_gpu::BackendPref::GPU) {
                    std::fprintf(stderr, "Error: GPU backend failed: %s\n", gjob.out_err.c_str());
                    return 1;
                }
                std::fprintf(stderr, "[GPU unavailable (%s) -> falling back to CPU SIMD]\n", gjob.out_err.c_str());
            }
        }
        if (!stage45_done && !heraw_cpu::run_stage45_cpu(gjob)) {
            std::fprintf(stderr, "Error: CPU backend failed: %s\n", gjob.out_err.c_str());
            return 1;
        }
        eta_eff = gjob.out_eta_eff;
        g_eff = std::max(0.0, pg_gain * eta_eff);
        sig_eff_sq = std::max(1e-10, pg_sig0_sq * eta_eff);
        resid_noise_2x = gjob.out_resid_noise_2x;
        compute_tiles=gjob.out_tile_count;
        compute_peak_pixels=gjob.out_peak_tile_pixels;
        if(!gjob.out_tile_diagnostics.empty()) std::fprintf(stderr,"[Tiles] %s\n",gjob.out_tile_diagnostics.c_str());
        char msg5[240];
        double dt5 = std::max(0.0, elapsed_s(t_stage45) - stage4_seconds);
        std::snprintf(msg5, sizeof(msg5),
            "[5/6] GAT-DCT Wiener (%.2fx) + A-Trous chroma + PSF RL (%d iters) [%s] done in %.2fs",
            cfg.denoise_strength, cfg.psf_iters, gjob.out_device_label.c_str(), dt5);
        emit_stage(5, 88, dt5, msg5);
    }

    if (!keep_sources) {
        frames_lin.clear(); frames_lin.shrink_to_fit();
        g_halfs.clear(); g_halfs.shrink_to_fit();
        flows_fy.clear(); flows_fy.shrink_to_fit();
        flows_fx.clear(); flows_fx.shrink_to_fit();
        mw_half.clear(); mw_half.shrink_to_fit();
    }
    double mean_icov[3]={0,0,0};
    size_t kernel_samples=0;
    for(size_t i=0;i<(size_t)Hh*Wh;i+=97) {
        for(int c=0;c<3;++c) mean_icov[c]+=inv_cov[i*3+c];
        ++kernel_samples;
    }
    const auto kernel_power=heraw_core::kernel_power_from_inverse_covariance(
        (float)(mean_icov[0]/std::max<size_t>(1,kernel_samples)),
        (float)(mean_icov[1]/std::max<size_t>(1,kernel_samples)),
        (float)(mean_icov[2]/std::max<size_t>(1,kernel_samples)));
    inv_cov.clear(); inv_cov.shrink_to_fit();
    g_guide.clear(); g_guide.shrink_to_fit();
    snr_boost_half.clear(); snr_boost_half.shrink_to_fit();
    g0_ha_1x.clear(); g0_ha_1x.shrink_to_fit();
    rl_mod_f16.clear(); rl_mod_f16.shrink_to_fit();

    if (!cfg.dataset_dir.empty()) {
        const std::vector<float> empty;
        heraw_optics::DatasetInput input{frames_lin[0],empty,empty,field_phase,field_motion,
            &cfa6x6[0][0],H1,W1,true,N>=5,cam_make+" "+cam_model,
            "Leave-one-out linear CFA kernel; shared exposure, sensor calibration and image registration"};
        int cached_y=-1,cached_x=-1;
        std::array<float,4> cached{};
        auto gather=[&](int y,int x) {
            if(y==cached_y && x==cached_x) return;
            cached_y=y;cached_x=x;
            double sum[3]={},weight[3]={},variance[3]={};
            for(int n=1;n<N;++n) {
                float sy=0.5f*y+2*bilerp_clamped(flows_fy[n].data(),Hh,Wh,0.25f*y,0.25f*x);
                float sx=0.5f*x+2*bilerp_clamped(flows_fx[n].data(),Hh,Wh,0.25f*y,0.25f*x);
                int iy=(int)std::floor(sy),ix=(int)std::floor(sx);
                for(int yy=std::max(0,iy-2);yy<=std::min(H1-1,iy+3);++yy)
                    for(int xx=std::max(0,ix-2);xx<=std::min(W1-1,ix+3);++xx) {
                        int c=cfa6x6[yy%6][xx%6];
                        float v=frames_lin[n][(size_t)yy*W1+xx];
                        float dy=yy-sy,dx=xx-sx;
                        double w=std::exp(-(dy*dy+dx*dx)/(2*0.6*0.6));
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
        std::fprintf(stderr,"[W12] Paired dataset: %zu tiles, %zu rejected -> %s\n",
            report.accepted,report.rejected,report.manifest_path.c_str());
    }

    if (reconstruct_rgb && cfg.ibp_iters > 0) {
        const auto stats = heraw_core::compact_ibp(frames_lin, H1, W1, &cfa6x6[0][0],
            flows_fy, flows_fx, mw_half, core_noise, rgb_2x.data(), cfg.ibp_iters, core_execution);
        std::fprintf(stderr, "[C1] RAW forward residual %.6g -> %.6g (%d iterations).\n",
            stats.before, stats.after, stats.iterations);
    }
    if (reconstruct_rgb && !cfg.relay_raw && cfg.denoise_strength > 0) {
        heraw_core::HalfStackOptions opts;
        opts.strength = cfg.denoise_strength;
        opts.effective_frames = (float)N;
        opts.kernel_power=kernel_power.data();
        unsigned phases[2] = {0,0};
        for (int n=0; n<N; ++n) {
            size_t center=(size_t)(Hh/2)*Wh+Wh/2;
            int qx=((int)std::lround(4*flows_fx[n][center]))&1;
            int qy=((int)std::lround(4*flows_fy[n][center]))&1;
            phases[n&1] |= 1u << (2*qy+qx);
        }
        auto phase_count=[](unsigned m) { int n=0; for (;m;m>>=1) n+=(m&1); return n; };
        opts.phase_balanced = phase_count(phases[0])>=3 && phase_count(phases[1])>=3;
        if (N >= 5) {
            const auto stats = heraw_core::half_stack_denoise(frames_lin, H1, W1, &cfa6x6[0][0],
                flows_fy, flows_fx, mw_half, core_noise, rgb_2x.data(), opts, core_execution);
            std::fprintf(stderr, "[C6] Half-stack KLT shrink: %zu samples, measured variance %.7g.\n",
                stats.samples, stats.noise_variance);
        }
        heraw_core::color_line_refine(rgb_2x.data(), H2, W2,
            {(float)sig_eff_sq,(float)g_eff}, 0.25f, core_execution);
    }
    if (reconstruct_rgb && !cfg.relay_raw && cfg.psf_iters>0 && optics.frames>0) {
        std::vector<heraw_core::SpatialPsfZone> zones;
        const float centers[5][2]={{.5f,.5f},{.2f,.2f},{.8f,.2f},{.2f,.8f},{.8f,.8f}};
        for(size_t i=0;i<optics.zones.size();++i) {
            const auto& z=optics.zones[i];
            if(!z.mtf50.valid) continue;
            const float isotropic=(float)(0.37478/std::max(.03,z.mtf50.value));
            heraw_core::SpatialPsfZone model;
            model.cx=centers[i][0];model.cy=centers[i][1];
            model.sigma_major_2x=z.fwhm_major.valid ? (float)(z.fwhm_major.value*2/2.35482) : isotropic;
            model.sigma_minor_2x=z.fwhm_minor.valid ? (float)(z.fwhm_minor.value*2/2.35482) : isotropic;
            model.angle=z.angle_deg.valid ? (float)(z.angle_deg.value*3.14159265359/180) : 0.0f;
            model.valid=true;zones.push_back(model);
        }
        if(!zones.empty()) {
            auto stats=heraw_core::spatial_psf_deconvolve(rgb_2x.data(),H2,W2,zones,
                {(float)sig_eff_sq,(float)g_eff},1,0.25f,core_execution);
            std::fprintf(stderr,"[C5] Spatial PSF: %zu zones, residual %.6g -> %.6g.\n",zones.size(),stats.before,stats.after);
        }
    }
    if (reconstruct_rgb && !temporal_composite.rgb.empty()) {
        parallel_for_rows(H2,n_threads,[&](int begin,int end,int) {
            for(int y=begin;y<end;++y) for(int x=0;x<W2;++x) {
                size_t p=(size_t)y*W2+x;
                float alpha=1.0f;
                if(cfg.motion_mode=="strobe")
                    alpha=clamp_f(bilerp_clamped(primary_temporal.activity.data(),Hh,Wh,y*0.25f,x*0.25f)*N,0.0f,1.0f);
                else if(cfg.motion_mode=="virtual-nd")
                    alpha=clamp_f((bilerp_clamped(field_motion.data(),Hh,Wh,y*0.25f,x*0.25f)-cfg.vnd_thresh)*8.0f,0.0f,1.0f);
                int sy=std::min(H1-1,y/2),sx=std::min(W1-1,x/2);
                int sy1=std::min(H1-1,sy+1),sx1=std::min(W1-1,sx+1);
                float fy=(y&1)*0.5f,fx=(x&1)*0.5f;
                for(int c=0;c<3;++c) {
                    float v=(1-fy)*((1-fx)*temporal_composite.rgb[((size_t)sy*W1+sx)*3+c]+
                        fx*temporal_composite.rgb[((size_t)sy*W1+sx1)*3+c])+
                        fy*((1-fx)*temporal_composite.rgb[((size_t)sy1*W1+sx)*3+c]+
                        fx*temporal_composite.rgb[((size_t)sy1*W1+sx1)*3+c]);
                    rgb_2x[p*3+c]+=(v-rgb_2x[p*3+c])*alpha;
                }
            }
        });
    }
    if(reconstruct_rgb && !reflection_composite.rgb.empty()) {
        const auto& specular=suite_fields.maps.at("specular");
        parallel_for_rows(H2,n_threads,[&](int begin,int end,int) {
            for(int y=begin;y<end;++y) for(int x=0;x<W2;++x) {
                size_t p=(size_t)y*W2+x;
                float excursion=bilerp_clamped(specular.data(),Hh,Wh,y*0.25f,x*0.25f);
                float noise=std::sqrt((float)pg_sig0_sq+(float)pg_gain*std::max(0.0f,rgb_2x[p*3+1]));
                float alpha=clamp_f(excursion/(4*noise+0.001f),0,1);
                int sy=std::min(H1-1,y/2),sx=std::min(W1-1,x/2);
                for(int c=0;c<3;++c) {
                    float lower=reflection_composite.rgb[((size_t)sy*W1+sx)*3+c];
                    rgb_2x[p*3+c]-=alpha*std::max(0.0f,rgb_2x[p*3+c]-lower);
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
    t0 = std::chrono::steady_clock::now();
    float hdr_white=1.0f;
    if(bracket_exposures && !exposure_stats.gains.empty())
        hdr_white=std::max(1.0f,*std::max_element(exposure_stats.gains.begin(),exposure_stats.gains.end()));
    if(hdr_white>1.0f) {
        const float inv=1.0f/hdr_white;
        if(reconstruct_rgb) parallel_for_rows(H2,n_threads,[&](int begin,int end,int) {
            for(size_t i=(size_t)begin*W2*3;i<(size_t)end*W2*3;++i) rgb_2x[i]*=inv;
        });
        for(auto& image:extra_images) for(float& v:image.second.rgb) v*=inv;
        g_eff*=inv;sig_eff_sq*=inv*inv;resid_noise_2x*=inv;
        pg_gain*=inv;pg_sig0_sq*=inv*inv;
        auto sigma=suite_fields.maps.find("sigma");
        if(sigma!=suite_fields.maps.end()) for(float& v:sigma->second) v*=inv;
    }

    heraw_io::ExifBundle src_exif = heraw_io::extract_source_exif(anchor_raw_path);
    char sw_buf[180];
    std::snprintf(sw_buf, sizeof(sw_buf), "NijiN.LiteBurstSuite (nijinLBS) 2.0.0 (N=%d, %s)",
                  N, cfg.preset_name.c_str());
    const std::string sw_stamp = sw_buf;
    const std::string primary_output = !out_rgb_path.empty() ? out_rgb_path :
        (!out_raw_path.empty() ? out_raw_path : (!out_field_path.empty() ? out_field_path :
        (!cfg.dataset_dir.empty() ? cfg.dataset_dir+"/summary" :
        std::filesystem::u8path(anchor_raw_path).replace_extension().u8string()+"_nijinLBS")));
    const std::string suite_prefix = std::filesystem::u8path(primary_output).replace_extension().u8string();

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
    if (cfg.auto_exposure && reconstruct_rgb) {
        std::vector<float> lum_samples;
        lum_samples.reserve((size_t)(H2 / 16) * (size_t)(W2 / 16));
        for (int r = H2 / 12; r < 11 * H2 / 12; r += 16) {
            for (int c = W2 / 12; c < 11 * W2 / 12; c += 16) {
                size_t i = (size_t)r * W2 + c;
                float wr = rgb_2x[i*3 + 0] * cam_wb[0];
                float wg = rgb_2x[i*3 + 1] * cam_wb[1];
                float wb = rgb_2x[i*3 + 2] * cam_wb[2];
                float lr = (float)(cam2out_d[0][0]*wr + cam2out_d[0][1]*wg + cam2out_d[0][2]*wb);
                float lg = (float)(cam2out_d[1][0]*wr + cam2out_d[1][1]*wg + cam2out_d[1][2]*wb);
                float lb = (float)(cam2out_d[2][0]*wr + cam2out_d[2][1]*wg + cam2out_d[2][2]*wb);
                lum_samples.push_back(std::max(0.0f, 0.2126f * lr + 0.7152f * lg + 0.0722f * lb));
            }
        }
        if (is_astro) {
            // Sky-Background Pedestal Pinning + sensor-noise-bounded gain for ultra-high-ISO sparse starfields
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

    std::map<std::string,std::vector<float>> field_export_maps;
    std::map<std::string,float> field_export_scales;
    for (const auto& item : suite_fields.maps) {
        const auto& gate=suite_fields.gates.at(item.first);
        if (!gate.observable) {
            std::fprintf(stderr,"[Field %s] %s.\n",item.first.c_str(),gate.reason.c_str());
            continue;
        }
        float scale=1.0f;
        if(item.first=="sigma") {
            scale=0.0f;
            for(float value:item.second) if(std::isfinite(value)) scale=std::max(scale,value);
        }
        else if(item.first=="velocity" || item.first=="turbulence" || item.first=="defocus" ||
                item.first=="normal_x" || item.first=="normal_y") {
            scale=0.0f;
            for(float value:item.second) if(std::isfinite(value)) scale=std::max(scale,std::fabs(value));
        }
        scale=std::max(scale,1e-8f);
        field_export_scales[item.first]=scale;
        auto& dst=field_export_maps[item.first];
        dst.resize(item.second.size());
        for(size_t i=0;i<dst.size();++i)
            dst[i]=(item.first=="defocus" || item.first=="normal_x" || item.first=="normal_y") ?
                0.5f+0.5f*item.second[i]/scale : item.second[i]/scale;
    }
    auto field_selected = [&](const std::string& name) {
        return ("," + cfg.fields_csv + ",").find("," + name + ",") != std::string::npos;
    };
    std::vector<std::pair<std::string, const std::vector<float>*>> field_sources = {
        {"motion", &field_motion}, {"phase", &field_phase}, {"focus", &field_focus}
    };
    for (const auto& item : field_export_maps) field_sources.emplace_back(item.first,&item.second);
    if (!out_field_path.empty() || !suite_fields.maps.empty()) {
        FILE* report=fopen_utf8_wb(suite_prefix+"_fields.json");
        if(!report) return 1;
        std::fprintf(report,"{\"domain\":\"camera_linear\",\"raw_width\":%d,\"raw_height\":%d,"
            "\"output_orientation\":%d,\"selected\":\"%s\",\"parallax_2x_px\":%.6g,\"fields\":[",
            W1,H1,flip_to_exif_orientation(cam_flip),json_escape(cfg.fields_csv).c_str(),
            2.0*suite_fields.geometry.disparity_span_px);
        bool first=true;
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
    auto sample_output_field = [&](const std::vector<float>& src, int y, int x) -> uint16_t {
        int r = y, c = x;
        if (cam_flip == 3) { r = H2-1-y; c = W2-1-x; }
        else if (cam_flip == 5) { r = x; c = W2-1-y; }
        else if (cam_flip == 6) { r = H2-1-x; c = y; }
        float value = bilerp_clamped(src.data(), Hh, Wh, (r+0.5f)*0.25f-0.5f, (c+0.5f)*0.25f-0.5f);
        return (uint16_t)std::lround(clamp_f(value, 0.0f, 1.0f)*65535.0f);
    };
    auto write_extra_image = [&](const heraw_suite::Image& image, const std::string& name) -> bool {
        const int out_H=(cam_flip==5||cam_flip==6)?W2:H2;
        const int out_W=(cam_flip==5||cam_flip==6)?H2:W2;
        const bool gray=name=="transient" && cfg.transient_color=="gray";
        std::vector<uint16_t> pixels((size_t)out_H*out_W*(gray?1:3));
        parallel_for_rows(H2,n_threads,[&](int begin,int end,int) {
            for(int y=begin;y<end;++y) for(int x=0;x<W2;++x) {
                const float sy=(y+0.5f)*image.H/H2-0.5f,sx=(x+0.5f)*image.W/W2-0.5f;
                const int y0=clamp_i((int)std::floor(sy),0,image.H-1),x0=clamp_i((int)std::floor(sx),0,image.W-1);
                const int y1=std::min(y0+1,image.H-1),x1=std::min(x0+1,image.W-1);
                const float fy=clamp_f(sy-y0,0,1),fx=clamp_f(sx-x0,0,1);
                float cam[3];
                for(int c=0;c<3;++c) cam[c]=
                    ((1-fy)*((1-fx)*image.rgb[((size_t)y0*image.W+x0)*3+c]+fx*image.rgb[((size_t)y0*image.W+x1)*3+c])+
                     fy*((1-fx)*image.rgb[((size_t)y1*image.W+x0)*3+c]+fx*image.rgb[((size_t)y1*image.W+x1)*3+c]))*cam_wb[c]*exp_gain;
                float rgb[3];
                for(int c=0;c<3;++c) {
                    float v=std::max(0.0f,(float)(cam2out_d[c][0]*cam[0]+cam2out_d[c][1]*cam[1]+cam2out_d[c][2]*cam[2]));
                    rgb[c]=cfg.color_space==heraw_io::ColorSpaceMode::ADOBE_RGB ?
                        std::pow(v,256.0f/563.0f) : (v<=0.0031308f?12.92f*v:1.055f*std::pow(v,1.0f/2.4f)-0.055f);
                }
                int dy=y,dx=x;
                if(cam_flip==3) {dy=H2-1-y;dx=W2-1-x;}
                else if(cam_flip==5) {dy=W2-1-x;dx=y;}
                else if(cam_flip==6) {dy=x;dx=H2-1-y;}
                size_t p=(size_t)dy*out_W+dx;
                if(gray) pixels[p]=(uint16_t)std::lround(clamp_f(0.2126f*rgb[0]+0.7152f*rgb[1]+0.0722f*rgb[2],0,1)*65535);
                else for(int c=0;c<3;++c) pixels[p*3+c]=(uint16_t)std::lround(clamp_f(rgb[c],0,1)*65535);
            }
        });
        const std::string path=suite_prefix+"_"+name+".png";
        if(gray) return heraw_io::write_png16_gray(path,pixels.data(),out_H,out_W,src_exif,sw_stamp,
            n_threads,par_wrap,fopen_utf8_wb,remove_utf8,atomic_replace_utf8);
        return heraw_io::write_png16_rgb(path,pixels.data(),out_H,out_W,cfg.color_space,src_exif,sw_stamp,
            n_threads,par_wrap,fopen_utf8_wb,remove_utf8,atomic_replace_utf8);
    };

    // 6a) On-demand Export: 2x RGGB Linear RAW DNG 1.4 (Lossless SOF3 / Visually-Lossless HQ / FP16 / Uncompressed)
    if (!out_raw_path.empty()) {
        UninitBuf<uint16_t> rggb_u16;
        if (cfg.raw_mode != heraw_io::RawDngMode::DNG_FP16 && cfg.raw_mode != heraw_io::RawDngMode::DNG_LINEAR) {
            const float raw_dith = (cfg.raw_mode == heraw_io::RawDngMode::DNG_HQ) ? 0.0f : dither_amp;
            rggb_u16.resize((size_t)H2 * (size_t)W2);
            parallel_for_rows(H2, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    int ro = r & 1;
                    for (int c = 0; c < W2; ++c) {
                        int co = c & 1;
                        size_t idx = (size_t)r * W2 + c;
                        int ch = (ro == 0 && co == 0) ? 0 : ((ro == 1 && co == 1) ? 2 : 1);
                        float v = rgb_2x[idx * 3 + ch] * 65535.0f + raw_dith * tpdf_dither(idx * 4ULL + 1ULL);
                        rggb_u16[idx] = (uint16_t)clamp_i((int)std::lrintf(v), 0, 65535);
                    }
                }
            });
        }
        if (!heraw_io::write_dng14_rggb_ex(
                out_raw_path, rggb_u16.data(), rgb_2x.data(), H2, W2, cfg.raw_mode, sig_eff_sq, g_eff,
                cam_make, cam_model, cam_wb, cam_xyz, cam_flip, baseline_ev,
                src_exif, sw_stamp, n_threads, flip_to_exif_orientation, par_wrap,
                fopen_utf8_wb, remove_utf8, atomic_replace_utf8)) {
            std::fprintf(stderr, "Error writing DNG to %s\n", out_raw_path.c_str());
            return 1;
        }
    }

    for (auto& image : extra_images) {
        if (!write_extra_image(image.second,image.first)) {
            std::fprintf(stderr,"Error writing %s layer.\n",image.first.c_str()); return 1;
        }
        image.second.rgb.clear(); image.second.rgb.shrink_to_fit();
    }
    if (cfg.export_refocus) {
        const auto gate=suite_fields.gates.find("depth");
        if(gate==suite_fields.gates.end() || !gate->second.observable) {
            std::fprintf(stderr,"[W7] Refocus requires measurable relative parallax.\n"); return 3;
        }
        heraw_suite::Image image{H2,W2,std::vector<float>(rgb_2x.data(),rgb_2x.data()+(size_t)H2*W2*3)};
        heraw_suite::RefocusOptions opts;
        opts.focus_depth=cfg.refocus_pos;opts.radius_px=cfg.bokeh_strength;
        auto refocused=heraw_suite::render_refocus(image,suite_fields,opts,core_execution.parallel);
        image.rgb.clear();image.rgb.shrink_to_fit();
        if(!write_extra_image(refocused,"refocus")) return 1;
    }

    // 6b) On-demand Export: 2x Rendered RGB (16-bit PNG / 14-bit JPEG XS / 16-bit Deflate TIFF, sRGB or Adobe RGB)
    if (!out_rgb_path.empty() || cfg.export_parallax_html) {
        if (cfg.relay_raw && requested_cfg.denoise_strength>0) {
            heraw_core::HalfStackOptions opts;
            opts.strength=requested_cfg.denoise_strength;
            opts.effective_frames=1;
            opts.kernel_power=kernel_power.data();
            heraw_core::half_stack_denoise(rgb_2x.data(),nullptr,nullptr,H2,W2,
                {(float)sig_eff_sq,(float)g_eff},opts,core_execution);
            heraw_core::color_line_refine(rgb_2x.data(),H2,W2,
                {(float)sig_eff_sq,(float)g_eff},0.25f*requested_cfg.chroma_denoise,core_execution);
        }
        if (cfg.relay_raw && requested_cfg.psf_iters>0) {
            snr_gated_rl_deconv(rgb_2x.data(),H2,W2,requested_cfg.psf_iters,requested_cfg.psf_sigma,
                0.18f,resid_noise_2x,n_threads,rl_ws,rl_gain_half.data(),rl_thrmul_half.data(),Hh,Wh,is_astro,olpf_mode);
        }
        if (cfg.rgb_psf_iters > 0) {
            snr_gated_rl_deconv(rgb_2x.data(), H2, W2, cfg.rgb_psf_iters, 0.88f * (cfg.psf_sigma / 1.05f),
                                0.13f, resid_noise_2x, n_threads, rl_ws,
                                rl_gain_half.data(), rl_thrmul_half.data(), Hh, Wh, is_astro);
        }
        rl_ws.release();

        const float m00 = (float)cam2out_d[0][0], m01 = (float)cam2out_d[0][1], m02 = (float)cam2out_d[0][2];
        const float m10 = (float)cam2out_d[1][0], m11 = (float)cam2out_d[1][1], m12 = (float)cam2out_d[1][2];
        const float m20 = (float)cam2out_d[2][0], m21 = (float)cam2out_d[2][1], m22 = (float)cam2out_d[2][2];
        const float gr = cam_wb[0] * exp_gain;
        const float gg = cam_wb[1] * exp_gain;
        const float gb = cam_wb[2] * exp_gain;
        const float astro_ped_scaled = astro_sky_ped * exp_gain;
        const float astro_sub_scaled = astro_sky_sub * exp_gain;

        int out_H = (cam_flip == 5 || cam_flip == 6) ? W2 : H2;
        int out_W = (cam_flip == 5 || cam_flip == 6) ? H2 : W2;
        UninitBuf<uint16_t> out_rgb_u16((size_t)out_H * (size_t)out_W * 3);

        static constexpr int OETF_LUT_N = 65536;
        std::vector<float> oetf_lut(OETF_LUT_N + 2);
        const bool is_adobe = (cfg.color_space == heraw_io::ColorSpaceMode::ADOBE_RGB);
        for (int i = 0; i <= OETF_LUT_N + 1; ++i) {
            double v = std::min(1.0, (double)i / (double)OETF_LUT_N);
            double s = 0.0;
            if (is_adobe) {
                // ISO 12640-2 Adobe RGB (1998) exact gamma 563/256 = 2.19921875 with linear slope near zero
                s = std::pow(v, 256.0 / 563.0);
            } else {
                s = (v <= 0.0031308) ? (12.92 * v) : (1.055 * std::pow(v, 1.0 / 2.4) - 0.055);
            }
            oetf_lut[i] = (float)s;
        }
        const float* __restrict lut_p = oetf_lut.data();
        auto eval_oetf = [lut_p, is_adobe](float v) -> float {
            if (v <= 0.0f) return 0.0f;
            if (!is_adobe && v <= 0.0031308f) return 12.92f * v;
            if (is_adobe && v < 1.0f/OETF_LUT_N) return std::pow(v,256.0f/563.0f);
            if (v >= 1.0f) return 1.0f;
            float f = v * (float)OETF_LUT_N;
            int idx = (int)f;
            float frac = f - (float)idx;
            return lut_p[idx] + frac * (lut_p[idx + 1] - lut_p[idx]);
        };

        const bool do_rolloff = cfg.highlight_rolloff;
        const float asinh_k = astro_asinh_k;
        const float inv_asinh_k = 1.0f / std::asinh(asinh_k);
        const float astro_noise_gate = astro_ped_scaled + 2.8f * std::max(resid_noise_2x, astro_sky_iqr_sig) * exp_gain;
        parallel_for_rows(H2, n_threads, [&](int r0, int r1, int) {
            for (int r = r0; r < r1; ++r) {
                for (int c = 0; c < W2; ++c) {
                    size_t idx = (size_t)r * W2 + c;
                    float wr = rgb_2x[idx * 3 + 0] * gr;
                    float wg = rgb_2x[idx * 3 + 1] * gg;
                    float wb = rgb_2x[idx * 3 + 2] * gb;

                    float lr = std::max(0.0f, m00 * wr + m01 * wg + m02 * wb);
                    float lg = std::max(0.0f, m10 * wr + m11 * wg + m12 * wb);
                    float lb = std::max(0.0f, m20 * wr + m21 * wg + m22 * wb);

                    if (is_astro && cfg.auto_exposure) {
                        lr = std::max(0.0f, lr - astro_sub_scaled);
                        lg = std::max(0.0f, lg - astro_sub_scaled);
                        lb = std::max(0.0f, lb - astro_sub_scaled);
                        // Gentle noise-gated color-preserving asinh stretch above dark sky pedestal
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

                    uint16_t qr = (uint16_t)clamp_i((int)std::lrintf(sr * 65535.0f + dither_amp * tpdf_dither(idx * 4ULL + 2ULL)), 0, 65535);
                    uint16_t qg = (uint16_t)clamp_i((int)std::lrintf(sg * 65535.0f + dither_amp * tpdf_dither(idx * 4ULL + 3ULL)), 0, 65535);
                    uint16_t qb = (uint16_t)clamp_i((int)std::lrintf(sb * 65535.0f + dither_amp * tpdf_dither(idx * 4ULL + 4ULL)), 0, 65535);

                    int dr = r, dc = c;
                    if (cam_flip == 3) { dr = H2 - 1 - r; dc = W2 - 1 - c; }
                    else if (cam_flip == 5) { dr = W2 - 1 - c; dc = r; }
                    else if (cam_flip == 6) { dr = c; dc = H2 - 1 - r; }

                    size_t didx = ((size_t)dr * (size_t)out_W + (size_t)dc) * 3;
                    out_rgb_u16[didx + 0] = qr;
                    out_rgb_u16[didx + 1] = qg;
                    out_rgb_u16[didx + 2] = qb;
                }
            }
        });
        if (cfg.export_parallax_html) {
            auto depth=suite_fields.maps.find("depth");
            auto gate=suite_fields.gates.find("depth");
            if(depth==suite_fields.maps.end() || gate==suite_fields.gates.end() || !gate->second.observable) {
                std::fprintf(stderr,"[W6] Parallax export requires measurable relative parallax.\n"); return 3;
            }
            const float scale=std::min(1.0f,2048.0f/std::max(out_H,out_W));
            const int ph=std::max(1,(int)(out_H*scale)),pw=std::max(1,(int)(out_W*scale));
            std::vector<uint8_t> preview((size_t)ph*pw*3);
            heraw_suite::FieldResult preview_fields;
            preview_fields.H=ph;preview_fields.W=pw;
            preview_fields.gates["depth"]=gate->second;
            auto& z=preview_fields.maps["depth"];z.resize((size_t)ph*pw);
            for(int y=0;y<ph;++y) for(int x=0;x<pw;++x) {
                int yy=std::min(out_H-1,(int)((y+0.5f)/scale)),xx=std::min(out_W-1,(int)((x+0.5f)/scale));
                size_t p=(size_t)y*pw+x,s=(size_t)yy*out_W+xx;
                for(int c=0;c<3;++c) preview[p*3+c]=(uint8_t)((out_rgb_u16[s*3+c]+128u)/257u);
                z[p]=sample_output_field(depth->second,yy,xx)/65535.0f;
            }
            ensure_parent_dir_utf8(suite_prefix+"_parallax.html");
            heraw_suite::write_webgl_html(suite_prefix+"_parallax.html",preview,ph,pw,preview_fields,24.0f*cfg.parallax_amp);
        }
        rgb_2x.release();

        heraw_io::RgbFormat eff_fmt = cfg.explicit_rgb_fmt
            ? cfg.rgb_format
            : infer_rgb_format_from_path(out_rgb_path, cfg.rgb_format);

        bool rgb_ok = out_rgb_path.empty();
        std::vector<heraw_io::NamedAlphaPlane> extra_planes;
        if (!out_field_path.empty() && cfg.field_mode == heraw_io::FieldExportMode::TIFF_ALPHA) {
            for (const auto& entry : field_sources) {
                if (!field_selected(entry.first) || entry.second->empty()) continue;
                heraw_io::NamedAlphaPlane plane;
                plane.name = entry.first;
                const auto* source = entry.second;
                plane.sample = [&, source](int y, int x) { return sample_output_field(*source, y, x); };
                extra_planes.push_back(std::move(plane));
            }
        }
        if (!out_rgb_path.empty()) switch (eff_fmt) {
            case heraw_io::RgbFormat::PNG16:
                rgb_ok = heraw_io::write_png16_rgb(out_rgb_path, out_rgb_u16.data(), out_H, out_W,
                    cfg.color_space, src_exif, sw_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                break;
            case heraw_io::RgbFormat::JXS_VIS:
                rgb_ok = heraw_io::write_jxs_rgb(out_rgb_path, out_rgb_u16.data(), out_H, out_W,
                    false, cfg.jxs_bpp, cfg.color_space, src_exif, sw_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                break;
            case heraw_io::RgbFormat::JXS_LOSSLESS:
                rgb_ok = heraw_io::write_jxs_rgb(out_rgb_path, out_rgb_u16.data(), out_H, out_W,
                    true, cfg.jxs_bpp, cfg.color_space, src_exif, sw_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                break;
            case heraw_io::RgbFormat::TIFF_DEFLATE:
                rgb_ok = heraw_io::write_tiff16_rgb_with_alphas(out_rgb_path, out_rgb_u16.data(), extra_planes, out_H, out_W,
                    true, cfg.color_space, src_exif, sw_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                break;
            case heraw_io::RgbFormat::TIFF_RAW:
                rgb_ok = heraw_io::write_tiff16_rgb_with_alphas(out_rgb_path, out_rgb_u16.data(), extra_planes, out_H, out_W,
                    false, cfg.color_space, src_exif, sw_stamp, n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
                break;
        }
        if (!rgb_ok) {
            std::fprintf(stderr, "Error writing RGB output to %s\n", out_rgb_path.c_str());
            return 1;
        }
    }

    // 6c) On-demand Export: 2x 16-bit Physical Field Map(s)
    //     Default (SPLIT_GRAY): 3 separate 16-bit single-channel Grayscale PNGs (_motion/_sky, _phase, _focus)
    //     Optional (PACKED_RGB / single-plane): packed R/G/B PNG or individual 16-bit Grayscale PNG
    if (!out_field_path.empty() && cfg.field_mode != heraw_io::FieldExportMode::TIFF_ALPHA) {
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
        int out_H = (cam_flip == 5 || cam_flip == 6) ? W2 : H2;
        int out_W = (cam_flip == 5 || cam_flip == 6) ? H2 : W2;

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
            for (const auto& entry:field_sources) {
                if (!field_selected(entry.first) || entry.second->empty()) continue;
                packed_fields.push_back(entry.first=="motion" ? &r_field_src : entry.second);
            }
            for(size_t group=0;group<packed_fields.size();group+=3) {
            std::string rgb_fld_path = out_field_path;
            if (packed_fields.size()>3)
                rgb_fld_path=derive_part_path(("rgb"+std::to_string(group/3+1)).c_str(),false);
            size_t sl = rgb_fld_path.find_last_of("/\\");
            size_t dt = rgb_fld_path.find_last_of('.');
            if (dt == std::string::npos || (sl != std::string::npos && dt < sl)) rgb_fld_path += ".png";

            UninitBuf<uint16_t> fld_u16((size_t)out_H * (size_t)out_W * 3);
            parallel_for_rows(H2, n_threads, [&](int r0, int r1, int) {
                for (int r = r0; r < r1; ++r) {
                    float yh = ((float)r + 0.5f) * 0.25f - 0.5f;
                    for (int c = 0; c < W2; ++c) {
                        float xh = ((float)c + 0.5f) * 0.25f - 0.5f;
                        auto sample_channel=[&](size_t ch) {
                            return ch<packed_fields.size() ?
                                clamp_f(bilerp_clamped(packed_fields[ch]->data(),Hh,Wh,yh,xh),0,1) : 0.0f;
                        };
                        float fm=sample_channel(group),fp=sample_channel(group+1),ff=sample_channel(group+2);

                        int dr = r, dc = c;
                        if (cam_flip == 3) { dr = H2 - 1 - r; dc = W2 - 1 - c; }
                        else if (cam_flip == 5) { dr = W2 - 1 - c; dc = r; }
                        else if (cam_flip == 6) { dr = c; dc = H2 - 1 - r; }

                        size_t didx = ((size_t)dr * (size_t)out_W + (size_t)dc) * 3;
                        fld_u16[didx + 0] = (uint16_t)clamp_i((int)std::lrintf(fm * 65535.0f), 0, 65535);
                        fld_u16[didx + 1] = (uint16_t)clamp_i((int)std::lrintf(fp * 65535.0f), 0, 65535);
                        fld_u16[didx + 2] = (uint16_t)clamp_i((int)std::lrintf(ff * 65535.0f), 0, 65535);
                    }
                }
            });
            if (!heraw_io::write_png16_rgb(rgb_fld_path, fld_u16.data(), out_H, out_W,
                    heraw_io::ColorSpaceMode::SRGB, src_exif, sw_stamp + " [Packed physical fields: "+cfg.fields_csv+"]",
                    n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8)) {
                std::fprintf(stderr, "Error writing packed RGB Field Map PNG to %s\n", rgb_fld_path.c_str());
                return 1;
            }
            }
        } else {
            UninitBuf<uint16_t> gray_u16((size_t)out_H * (size_t)out_W);
            auto emit_one_gray = [&](const float* src_half, const char* sfx, const char* tag_desc, bool single_exp) -> bool {
                std::string dst_p = derive_part_path(sfx, single_exp);
                parallel_for_rows(H2, n_threads, [&](int r0, int r1, int) {
                    for (int r = r0; r < r1; ++r) {
                        float yh = ((float)r + 0.5f) * 0.25f - 0.5f;
                        for (int c = 0; c < W2; ++c) {
                            float xh = ((float)c + 0.5f) * 0.25f - 0.5f;
                            float fv = clamp_f(bilerp_clamped(src_half, Hh, Wh, yh, xh), 0.0f, 1.0f);
                            int dr = r, dc = c;
                            if (cam_flip == 3) { dr = H2 - 1 - r; dc = W2 - 1 - c; }
                            else if (cam_flip == 5) { dr = W2 - 1 - c; dc = r; }
                            else if (cam_flip == 6) { dr = c; dc = H2 - 1 - r; }
                            gray_u16[(size_t)dr * (size_t)out_W + (size_t)dc] =
                                (uint16_t)clamp_i((int)std::lrintf(fv * 65535.0f), 0, 65535);
                        }
                    }
                });
                if (!heraw_io::write_png16_gray(dst_p, gray_u16.data(), out_H, out_W,
                        src_exif, sw_stamp + " " + tag_desc,
                        n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8)) {
                    std::fprintf(stderr, "Error writing Grayscale Field Map PNG to %s\n", dst_p.c_str());
                    return false;
                }
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
            std::fprintf(stderr,
                "{\"event\":\"group_done\",\"group\":%d,\"total_groups\":%d,\"wall_s\":%.3f,"
                "\"eta\":%.5f,\"gain\":%.3f,\"ev\":%.3f,\"tiles\":%d,\"peak_tile_pixels\":%zu,\"out_raw\":\"%s\",\"out_rgb\":\"%s\"}\n",
                group_idx_1based, total_groups, wall, eta_eff, exp_gain, baseline_ev,compute_tiles,compute_peak_pixels,
                json_escape(out_raw_path).c_str(), json_escape(out_rgb_path).c_str());
            std::fflush(stderr);
        }
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

static bool parse_bool_arg(const std::string& v) {
    return (v == "1" || v == "on" || v == "true" || v == "yes" || v == "ON" || v == "TRUE");
}

static void unpack_frame_batch(std::vector<UnpackedFrame>& uframes, const std::vector<std::string>& paths) {
    const int n_total = (int)paths.size();
    uframes.resize(n_total);
#ifdef _WIN32
    std::vector<std::thread> workers;
    workers.reserve(n_total);
    for (int i = 0; i < n_total; ++i) {
        uframes[i].path = paths[i];
        workers.emplace_back([&, i]() { unpack_one_frame(&uframes[i]); });
    }
    for (auto& th : workers) th.join();
#else
    std::vector<pthread_t> tids(n_total);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 32 * 1024 * 1024);
    for (int i = 0; i < n_total; ++i) {
        uframes[i].path = paths[i];
        pthread_create(&tids[i], &attr, unpack_pthread_trampoline, &uframes[i]);
    }
    pthread_attr_destroy(&attr);
    for (int i = 0; i < n_total; ++i) pthread_join(tids[i], nullptr);
#endif
}

} // namespace

static int run_cli(int argc, char** argv) {
    auto t_wall0 = std::chrono::steady_clock::now();
    auto elapsed_s = [&](auto t0) {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    PipelineConfig cfg;
    cfg.n_threads = (int)std::max(1u, std::thread::hardware_concurrency());
    apply_preset_defaults(cfg, PipelinePreset::PHOTO);

    std::string out_raw_path, out_rgb_path, out_field_path, out_dir_path, custom_libraw_path;
    std::string dec_jxs_in, dec_jxs_out;
    bool emit_raw_in_dir = false, emit_rgb_in_dir = false, emit_field_in_dir = false;
    bool inspect_only = false;
    bool plan_json = false;
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

    for (size_t i = 0; i < cli_args.size(); ++i) {
        const std::string& a = cli_args[i];
        auto next_s = [&]() -> std::string { return (i + 1 < cli_args.size()) ? cli_args[++i] : std::string(); };
        if (a == "--out-raw" && i + 1 < cli_args.size()) out_raw_path = next_s();
        else if (a == "--out-rgb" && i + 1 < cli_args.size()) out_rgb_path = next_s();
        else if (a == "--out-field" && i + 1 < cli_args.size()) out_field_path = next_s();
        else if (a == "--out-dir" && i + 1 < cli_args.size()) out_dir_path = next_s();
        else if (a == "--emit-raw") emit_raw_in_dir = true;
        else if (a == "--emit-rgb") emit_rgb_in_dir = true;
        else if (a == "--emit-field") emit_field_in_dir = true;
        else if (a == "--scan-dir" && i + 1 < cli_args.size()) scan_dirs.push_back(next_s());
        else if (a == "--recursive") recursive_scan = true;
        else if (a == "--sort-time") cfg.sort_time = true;
        else if (a == "--no-sort-time") cfg.sort_time = false;
        else if (a == "--libraw" && i + 1 < cli_args.size()) custom_libraw_path = next_s();
        else if (a == "--no-dark") { /* legacy no-op: dark-frame-free is always active */ }
        else if (a == "--motion-mode" && i + 1 < cli_args.size()) cfg.motion_mode = next_s();
        else if (a == "--olpf-mode" && i + 1 < cli_args.size()) cfg.olpf_mode = next_s();
        else if (a == "--fields" && i + 1 < cli_args.size()) cfg.fields_csv = next_s();
        else if (a == "--reflection-mode" && i + 1 < cli_args.size()) cfg.reflection_mode = next_s();
        else if (a == "--transient-color" && i + 1 < cli_args.size()) cfg.transient_color = next_s();
        else if (a == "--relay-raw" && i + 1 < cli_args.size()) cfg.relay_raw = parse_bool_arg(next_s());
        else if (a == "--silicon-clean" && i + 1 < cli_args.size()) cfg.silicon_clean = parse_bool_arg(next_s());
        else if (a == "--auto-lca" && i + 1 < cli_args.size()) cfg.auto_lca = parse_bool_arg(next_s());
        else if (a == "--ibp-iters" && i + 1 < cli_args.size()) cfg.ibp_iters = clamp_i(std::atoi(next_s().c_str()), 0, 2);
        else if (a == "--clean-sens" && i + 1 < cli_args.size()) cfg.clean_sens = clamp_f(std::atof(next_s().c_str()), 0.1f, 5.0f);
        else if (a == "--vnd-thresh" && i + 1 < cli_args.size()) cfg.vnd_thresh = clamp_f(std::atof(next_s().c_str()), 0.0f, 1.0f);
        else if (a == "--strobe-step" && i + 1 < cli_args.size()) cfg.strobe_step = std::max(1, std::atoi(next_s().c_str()));
        else if (a == "--strobe-fade" && i + 1 < cli_args.size()) cfg.strobe_fade = clamp_f(std::atof(next_s().c_str()), 0.0f, 1.0f);
        else if (a == "--transient-sigma" && i + 1 < cli_args.size()) cfg.transient_sigma = clamp_f(std::atof(next_s().c_str()), 0.5f, 20.0f);
        else if (a == "--emit-transient") cfg.emit_transient = true;
        else if (a == "--export-parallax-html") cfg.export_parallax_html = true;
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
                    std::fprintf(stderr, "Error: --skip-frames expects 1-based indices.\n"); return 2;
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
            else { std::fprintf(stderr, "Error: unknown --policy '%s'\n", v.c_str()); return 2; }
        } else if (a == "--interactive-choice" && i + 1 < cli_args.size()) {
            cfg.interactive_choice = std::atoi(next_s().c_str());
        } else if (a == "--group-size" && i + 1 < cli_args.size()) {
            cfg.group_size = std::max(4, std::atoi(next_s().c_str()));
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
            else { std::fprintf(stderr, "Error: unknown --raw-format '%s'\n", v.c_str()); return 2; }
        } else if (a == "--rgb-format" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            cfg.explicit_rgb_fmt = true;
            if (v == "png" || v == "png16") cfg.rgb_format = heraw_io::RgbFormat::PNG16;
            else if (v == "jxs") cfg.rgb_format = heraw_io::RgbFormat::JXS_VIS;
            else if (v == "jxs-lossless") cfg.rgb_format = heraw_io::RgbFormat::JXS_LOSSLESS;
            else if (v == "tiff-deflate" || v == "tiff") cfg.rgb_format = heraw_io::RgbFormat::TIFF_DEFLATE;
            else if (v == "tiff-raw") cfg.rgb_format = heraw_io::RgbFormat::TIFF_RAW;
            else { std::fprintf(stderr, "Error: unknown --rgb-format '%s'\n", v.c_str()); return 2; }
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
            else { std::fprintf(stderr, "Error: unknown --field-mode '%s' (expected: split | rgb | motion | phase | focus)\n", v.c_str()); return 2; }
        } else if (a == "--color-space" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "srgb" || v == "sRGB") cfg.color_space = heraw_io::ColorSpaceMode::SRGB;
            else if (v == "adobergb" || v == "adobe-rgb" || v == "AdobeRGB") cfg.color_space = heraw_io::ColorSpaceMode::ADOBE_RGB;
            else { std::fprintf(stderr, "Error: unknown --color-space '%s'\n", v.c_str()); return 2; }
        } else if (a == "--preset" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "photo") apply_preset_defaults(cfg, PipelinePreset::PHOTO);
            else if (v == "sci" || v == "training" || v == "ml") apply_preset_defaults(cfg, PipelinePreset::SCI);
            else if (v == "arc" || v == "archival") apply_preset_defaults(cfg, PipelinePreset::ARC);
            else if (v == "astro" || v == "night" || v == "star") apply_preset_defaults(cfg, PipelinePreset::ASTRO);
            else { std::fprintf(stderr, "Error: unknown --preset '%s' (expected: photo | sci | arc | astro)\n", v.c_str()); return 2; }
        } else if (a == "--wizard") {
            cfg.wizard_mode = true;
            cfg.policy = heraw_plan::ExecPolicy::INTERACTIVE;
        } else if (a == "--anchor-frame" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto" || v == "sharpest") cfg.anchor_mode = 0;
            else if (v == "first") cfg.anchor_mode = -1;
            else if (v == "middle" || v == "mid") cfg.anchor_mode = -2;
            else if (std::atoi(v.c_str()) >= 1) cfg.anchor_mode = std::atoi(v.c_str());
            else { std::fprintf(stderr, "Error: unknown --anchor-frame '%s' (expected: auto | first | middle | <1..N>)\n", v.c_str()); return 2; }
        } else if (a == "--backend" && i + 1 < cli_args.size()) {
            std::string v = next_s();
            if (v == "auto") cfg.backend = heraw_gpu::BackendPref::AUTO;
            else if (v == "gpu" || v == "metal" || v == "vulkan") cfg.backend = heraw_gpu::BackendPref::GPU;
            else if (v == "cpu") cfg.backend = heraw_gpu::BackendPref::CPU;
            else { std::fprintf(stderr, "Error: unknown --backend '%s' (expected: auto | gpu | cpu)\n", v.c_str()); return 2; }
        } else if (a == "--denoise-strength" && i + 1 < cli_args.size()) { ov_den = true; v_den = clamp_f((float)std::atof(next_s().c_str()), 0.0f, 2.5f); }
        else if (a == "--chroma-denoise" && i + 1 < cli_args.size()) { ov_chr = true; v_chr = clamp_f((float)std::atof(next_s().c_str()), 0.0f, 2.5f); }
        else if (a == "--psf-iters" && i + 1 < cli_args.size()) { ov_psfi = true; v_psfi = clamp_i(std::atoi(next_s().c_str()), 0, 12); }
        else if (a == "--rgb-psf-iters" && i + 1 < cli_args.size()) { ov_rgbpsfi = true; v_rgbpsfi = clamp_i(std::atoi(next_s().c_str()), 0, 8); }
        else if (a == "--psf-sigma" && i + 1 < cli_args.size()) { ov_psfs = true; v_psfs = clamp_f((float)std::atof(next_s().c_str()), 0.35f, 3.0f); }
        else if (a == "--deghost-strength" && i + 1 < cli_args.size()) { ov_degh = true; v_degh = clamp_f((float)std::atof(next_s().c_str()), 0.4f, 3.0f); }
        else if (a == "--auto-exposure" && i + 1 < cli_args.size()) { ov_ae = true; v_ae = parse_bool_arg(next_s()); }
        else if (a == "--highlight-rolloff" && i + 1 < cli_args.size()) { ov_hl = true; v_hl = parse_bool_arg(next_s()); }
        else if (a == "--edge-polish" && i + 1 < cli_args.size()) { ov_ep = true; v_ep = parse_bool_arg(next_s()); }
        else if (a == "--dither" && i + 1 < cli_args.size()) { ov_dith = true; v_dith = parse_bool_arg(next_s()); }
        else if (a == "--ev-offset" && i + 1 < cli_args.size()) cfg.ev_offset = clamp_f((float)std::atof(next_s().c_str()), -4.0f, 4.0f);
        else if (a == "--min-rel-sharpness" && i + 1 < cli_args.size()) { ov_mrs = true; v_mrs = clamp_f((float)std::atof(next_s().c_str()), 0.1f, 0.99f); }
        else if (a == "--max-shift-px" && i + 1 < cli_args.size()) cfg.max_shift_px = clamp_f((float)std::atof(next_s().c_str()), 8.0f, 800.0f);
        else if (a == "--threads" && i + 1 < cli_args.size()) {
            int threads = std::atoi(next_s().c_str());
            cfg.n_threads = threads > 0 ? threads : (int)std::max(1u, std::thread::hardware_concurrency());
        }
        else if (a == "--inspect" || a == "--inspect-af") inspect_only = true;
        else if (a == "--plan-json") plan_json = true;
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
                    std::fprintf(stderr, "Error loading --libraw: %s\n", lerr.c_str());
                    return 1;
                }
                std::fprintf(stdout, "NijiN.LiteBurstSuite (nijinLBS) 2.0.0 -- RAW Imaging Suite (Custom LibRaw %s [%s] | %s)\n",
                    heraw_lr::global_dyn_libraw().ver_str.c_str(), custom_libraw_path.c_str(), gpu_tag.c_str());
            } else {
                std::fprintf(stdout, "NijiN.LiteBurstSuite (nijinLBS) 2.0.0 -- RAW Imaging Suite (Built-in LibRaw 0.23.0-Snapshot202611 | %s)\n",
                    gpu_tag.c_str());
            }
            return 0;
        } else if (a == "-h" || a == "--help") {
            print_cli_help(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] != '-') {
            raw_pos_args.push_back(a);
        } else {
            std::fprintf(stderr, "Error: unknown or incomplete option: %s (run --help for usage)\n", a.c_str());
            return 2;
        }
    }

    if (!custom_libraw_path.empty()) {
        std::string lerr;
        if (!heraw_lr::global_dyn_libraw().load(custom_libraw_path, lerr)) {
            std::fprintf(stderr, "Error loading custom LibRaw library: %s\n", lerr.c_str());
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
    if (ov_hl) { cfg.highlight_rolloff = v_hl; cfg.glint_desat = v_hl; }
    if (ov_ep) cfg.edge_polish = v_ep;
    if (ov_dith) cfg.dither = v_dith;
    if (ov_mrs) cfg.min_rel_sharpness = v_mrs;

    // Standalone .jxs -> 16-bit PNG/TIFF decode utility
    if (!dec_jxs_in.empty()) {
        std::vector<uint16_t> rgb16;
        int dH = 0, dW = 0;
        heraw_io::ColorSpaceMode dcs = heraw_io::ColorSpaceMode::SRGB;
        if (!heraw_io::decode_jxs_file_to_rgb16(dec_jxs_in, rgb16, dH, dW, dcs, cfg.n_threads)) {
            std::fprintf(stderr, "Error: failed to decode JPEG XS file: %s\n", dec_jxs_in.c_str());
            return 1;
        }
        heraw_io::ExifBundle empty_ex;
        auto par_wrap = [](int tot, int nt, auto&& fn, int chk) {
            parallel_for_rows(tot, nt, std::forward<decltype(fn)>(fn), chk);
        };
        auto fmt = infer_rgb_format_from_path(dec_jxs_out, heraw_io::RgbFormat::TIFF_DEFLATE);
        bool ok = (fmt == heraw_io::RgbFormat::TIFF_DEFLATE || fmt == heraw_io::RgbFormat::TIFF_RAW)
            ? heraw_io::write_tiff16_rgb_ex(dec_jxs_out, rgb16.data(), dH, dW, fmt == heraw_io::RgbFormat::TIFF_DEFLATE,
                                            dcs, empty_ex, "NijiN.LiteBurstSuite (nijinLBS) 2.0.0 JXS decoder", cfg.n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8)
            : heraw_io::write_png16_rgb(dec_jxs_out, rgb16.data(), dH, dW,
                                        dcs, empty_ex, "NijiN.LiteBurstSuite (nijinLBS) 2.0.0 JXS decoder", cfg.n_threads, par_wrap, fopen_utf8_wb, remove_utf8, atomic_replace_utf8);
        if (!ok) {
            std::fprintf(stderr, "Error: failed to write decoded image to %s\n", dec_jxs_out.c_str());
            return 1;
        }
        std::fprintf(stderr, "Decoded %s (%dx%d) -> %s in %.2fs\n", dec_jxs_in.c_str(), dW, dH, dec_jxs_out.c_str(), elapsed_s(t_wall0));
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
        std::fprintf(stderr, "Error: invalid Suite mode or dataset tile size (256|512).\n"); return 2;
    }
    if (cfg.fields_csv.empty()) { std::fprintf(stderr, "Error: --fields requires at least one field.\n"); return 2; }
    for (size_t start = 0; start < cfg.fields_csv.size();) {
        size_t end = cfg.fields_csv.find(',', start);
        if (!one_of(cfg.fields_csv.substr(start, end-start),
            {"motion", "phase", "focus", "depth", "defocus", "specular", "sigma", "coherence", "velocity", "flicker", "turbulence",
             "normal_x", "normal_y", "occlusion"})) {
            std::fprintf(stderr, "Error: unknown physical field in --fields.\n"); return 2;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if(cfg.lcp_fields.empty()) { std::fprintf(stderr,"Error: --lcp-fields requires a model.\n");return 2; }
    for(size_t start=0;start<cfg.lcp_fields.size();) {
        size_t end=cfg.lcp_fields.find(',',start);
        if(!one_of(cfg.lcp_fields.substr(start,end-start),{"lca","vignette","distortion"})) {
            std::fprintf(stderr,"Error: --lcp-fields expects lca,vignette,distortion.\n");return 2;
        }
        if(end==std::string::npos) break;
        start=end+1;
    }
    if (cfg.field_mode == heraw_io::FieldExportMode::TIFF_ALPHA && (emit_field_in_dir || !out_field_path.empty())) {
        cfg.rgb_format = heraw_io::RgbFormat::TIFF_DEFLATE;
        cfg.explicit_rgb_fmt = true;
        emit_rgb_in_dir = true;
        if (out_dir_path.empty() && out_rgb_path.empty()) {
            out_rgb_path = std::filesystem::u8path(out_field_path).replace_extension(".tif").u8string();
        }
    }
    if (cfg.export_topk && out_dir_path.empty()) {
        std::fprintf(stderr, "Error: --export-topk requires --out-dir.\n"); return 2;
    }
    if (!out_dir_path.empty() && !emit_raw_in_dir && !emit_rgb_in_dir && !emit_field_in_dir && !cfg.optics_only && !cfg.export_topk) {
        emit_rgb_in_dir = true;
    }
    const bool has_output_target = !out_raw_path.empty() || !out_rgb_path.empty() || !out_field_path.empty() ||
        !out_dir_path.empty() || !cfg.optics_report.empty() || !cfg.export_lcp.empty() || !cfg.dataset_dir.empty();

    bool saw_dir_input = false;
    std::vector<std::string> all_inputs = heraw_plan::expand_raw_inputs(raw_pos_args, scan_dirs, recursive_scan, saw_dir_input);
    if (all_inputs.empty() || (!inspect_only && !plan_json && !has_output_target)) {
        std::fprintf(stderr,
            "Error: specify input RAW file(s) or folder(s) and at least one output (--out-raw, --out-rgb, --out-dir) or --inspect.\n"
            "Run '%s --help' for full CLI documentation.\n",
            argv[0] ? argv[0] : "nijinLBS");
        return 2;
    }

    if (cfg.progress_json) {
        std::fprintf(stderr, "{\"event\":\"start\",\"total_stages\":6,\"percent\":2,\"threads\":%d}\n", cfg.n_threads);
        std::fflush(stderr);
    }

    auto par_wrap = [](int tot, int nt, auto&& fn, int chk) {
        parallel_for_rows(tot, nt, std::forward<decltype(fn)>(fn), chk);
    };

    // --- Stage 0: Fast Header-Only Metadata Scan & Optional EXIF Time Sort ---
    auto t_scan0 = std::chrono::steady_clock::now();
    std::vector<heraw_plan::RawHeaderInfo> hdr_infos = heraw_plan::scan_raw_headers_fast(
        all_inputs, cfg.sort_time, cfg.n_threads, par_wrap
    );
    if (!cfg.skip_frames.empty()) {
        if (cfg.anchor_mode > 0 && std::find(cfg.skip_frames.begin(), cfg.skip_frames.end(), cfg.anchor_mode) != cfg.skip_frames.end()) {
            std::fprintf(stderr, "Error: selected anchor is in --skip-frames.\n"); return 2;
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
        hdr_infos, is_batch_mode, cfg.burst_gap_s
    );
    if (clusters.empty()) {
        std::fprintf(stderr, "Error: no readable RAW files found among %zu input path(s).\n", all_inputs.size());
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
            if (is_batch_mode && !plan_json && cl.size() < 4 && clusters.size() > 3 && cfg.policy != heraw_plan::ExecPolicy::FORCE_ALL) {
                ++skipped_small_clusters;
                continue;
            }
            std::vector<std::string> paths;
            for (const auto& h : cl) paths.push_back(h.path);

            std::vector<UnpackedFrame> uframes;
            unpack_frame_batch(uframes, paths);
            bool bad_unpack = false;
            for (const auto& uf : uframes) if (uf.status != 0) bad_unpack = true;
            if (bad_unpack) {
                ++inspection_errors;
                for (const auto& uf : uframes) if (uf.status)
                    std::fprintf(stderr, "Unpack error on %s: %s\n", uf.path.c_str(), uf.err_msg.c_str());
                continue;
            }

            auto pr = heraw_plan::analyze_and_plan_cluster(
                uframes, cl, cfg.policy, is_batch_mode, cfg.group_size, cfg.max_frames,
                cfg.min_rel_sharpness, cfg.max_shift_px, quick_percentile, cfg.anchor_mode, cfg.preset_name
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
                    if (ec) { std::fprintf(stderr, "Copy failed: %s\n", ec.message().c_str()); okay = false; break; }
                    std::fprintf(f, "%s{\"rank\":%zu,\"source\":\"%s\",\"output\":\"%s\",\"af_score\":%d,\"acutance\":%.6f}",
                        rank ? "," : "", rank+1, json_escape(d.path).c_str(), json_escape(dst).c_str(), d.af_score, d.acutance);
                }
                std::fprintf(f, "]}\n");
                if (std::ferror(f)) okay = false;
                if (std::fclose(f)) okay = false;
                if (!okay) return 1;
                std::fprintf(stderr, "Selected %zu original RAW file(s) -> %s\n", order.size(), manifest.c_str());
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
                std::fprintf(stdout,
                    "\"make\":\"%s\",\"model\":\"%s\",\"width\":%d,\"height\":%d,\"iso\":%.1f,\"focal_len\":%.2f,"
                    "\"cfa_type\":\"%s\",\"olpf_auto\":\"%s\",\"shutter_type\":\"%s\",\"has_ois\":%s,"
                    "\"af_norm_x\":%.5f,\"af_norm_y\":%.5f,\"max_drift_2x_px\":%.4f,\"max_parallax_2x_px\":%.4f,\"frames\":[",
                    json_escape(um.make).c_str(), json_escape(um.model).c_str(), um.W, um.H, um.iso, um.focal_mm,
                    rx.cfa_type.c_str(), rx.olpf_auto.c_str(), rx.shutter_type.c_str(), rx.has_ois ? "true" : "false",
                    rx.af_norm_x, rx.af_norm_y, rx.max_drift_2x_px, rx.max_parallax_2x_px);
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
                std::fprintf(stdout,
                    "=== Candidate Cluster %zu/%zu (%s %s, %dx%d -> %dx%d) ===\n"
                    "  5D Scene        : %s | SR Score: %d/100 (%d/4 subpixel quads) | MovArea: %.1f%% | Stars: %d\n"
                    "  Prescription    : preset=%s, anchor=%s (crown=#%d), denoise=%.2f, deghost=%.2f, rl=%d (~%.0f MiB DNG)\n"
                    "  Motion Regime   : %s\n"
                    "  Status          : [%s] %s\n"
                    "  Planned Groups  : %zu\n"
                    "  Idx  Grp  Role   Flags                Acutance  Shift1x   ZNCC   File\n",
                    ci + 1, clusters.size(), u0.make.c_str(), u0.model.c_str(), u0.W, u0.H, u0.W * 2, u0.H * 2,
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
            std::fprintf(stdout, "Skipped %d isolated single/pair cluster(s) (< 4 frames) during batch scan.\n", skipped_small_clusters);
        }
        std::fflush(stdout);
        return inspection_errors ? 1 : 0;
    }

    int global_groups_done = 0;
    for (size_t ci = 0; ci < clusters.size(); ++ci) {
        const auto& cl = clusters[ci];
        if (is_batch_mode && clusters.size() > 1 && cl.size() < 4 && cfg.policy != heraw_plan::ExecPolicy::FORCE_ALL) {
            ++skipped_small_clusters;
            continue;
        }

        auto t_u0 = std::chrono::steady_clock::now();
        std::vector<std::string> paths;
        for (const auto& h : cl) paths.push_back(h.path);

        std::vector<UnpackedFrame> uframes;
        unpack_frame_batch(uframes, paths);
        bool cluster_bad = false;
        for (const auto& uf : uframes) {
            if (uf.status != 0) {
                std::fprintf(stderr, "Unpack error on %s: %s\n", uf.path.c_str(), uf.err_msg.c_str());
                cluster_bad = true;
            }
        }
        if (cluster_bad) {
            if (is_batch_mode && clusters.size() > 1) continue;
            return 1;
        }
        if (cfg.anchor_mode > (int)uframes.size()) {
            std::fprintf(stderr, "Error: --anchor-frame exceeds the cluster's %zu frames.\n", uframes.size());
            return 2;
        }
        {
            double dt1 = elapsed_s(t_u0) + (ci == 0 ? elapsed_s(t_scan0) - elapsed_s(t_u0) : 0.0);
            const auto& dyn = heraw_lr::global_dyn_libraw();
            std::fprintf(stderr, "[1/6] Unpacked %zu RAW file(s) via %s (cluster %zu/%zu, sort_time=%s) in %.2fs\n",
                paths.size(), dyn.active ? ("Custom LibRaw " + dyn.ver_str).c_str() : "Built-in LibRaw 0.23-HE*",
                ci + 1, clusters.size(), cfg.sort_time ? "on" : "off", std::max(0.01, dt1));
            if (cfg.progress_json) {
                std::fprintf(stderr, "{\"event\":\"progress\",\"stage\":1,\"total_stages\":6,\"percent\":14,\"message\":\"Unpacked %zu RAW file(s)\"}\n", paths.size());
            }
            std::fflush(stderr);
        }

        PipelineConfig cluster_cfg = cfg;
        auto pr = heraw_plan::analyze_and_plan_cluster(
            uframes, cl, cluster_cfg.policy, is_batch_mode, cluster_cfg.group_size, cluster_cfg.max_frames,
            cluster_cfg.min_rel_sharpness, cluster_cfg.max_shift_px, quick_percentile, cluster_cfg.anchor_mode, cluster_cfg.preset_name
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
            std::fprintf(stderr,
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
                std::fprintf(stderr, "Enter anchor frame index [1..%zu]: ", pr.diags.size());
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
                    quick_percentile, cluster_cfg.anchor_mode, cluster_cfg.preset_name
                );
            } else if (wpick == 1) {
                apply_rx_to_cfg(cluster_cfg, rx);
                if (!pr.options.empty() && !pr.options[0].aborts) pr.selected_groups = pr.options[0].groups;
            }
        } else if (cluster_cfg.policy == heraw_plan::ExecPolicy::INTERACTIVE && pr.needs_interactive_prompt && !pr.options.empty()) {
            int pick = cluster_cfg.interactive_choice;
            if (pick != 1 && pick != 2) {
                std::fprintf(stderr, "\n[Suitability Notice: %s] %s\n", pr.condition_code.c_str(), pr.condition_summary.c_str());
                for (const auto& op : pr.options) {
                    std::fprintf(stderr, "  [%d] %s\n      %s\n", op.id, op.title.c_str(), op.detail.c_str());
                }
                std::fprintf(stderr, "Select option [1/%zu, Enter=1]: ", pr.options.size());
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
            std::fprintf(stderr, "-> Selected option [%d]: %s\n", chosen.id, chosen.title.c_str());
            std::fflush(stderr);
            if (chosen.aborts) {
                std::fprintf(stderr, "Operation canceled cleanly per interactive selection.\n");
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
            std::fprintf(stderr, "Suitability check stopped execution [%s]: %s\n",
                pr.condition_code.c_str(), pr.condition_summary.c_str());
            return 3;
        }

        const int n_grp = (int)pr.selected_groups.size();
        for (int gi = 0; gi < n_grp; ++gi) {
            const auto& grp = pr.selected_groups[gi];
            if (grp.light_uframe_indices.empty()) continue;
            std::string g_raw = out_raw_path;
            std::string g_rgb = out_rgb_path;
            std::string g_fld = out_field_path;

            if (!out_dir_path.empty()) {
                std::string stem = grp.naming_stem;
                if (n_grp > 1 || clusters.size()>1) {
                    char sb[24]; std::snprintf(sb, sizeof(sb), "_grp%02d", global_groups_done + 1);
                    stem += sb;
                }
                stem += "_nijinLBS";
                char sep = (out_dir_path.find('\\') != std::string::npos) ? '\\' : '/';
                std::string base = out_dir_path;
                if (!base.empty() && base.back() != '/' && base.back() != '\\') base.push_back(sep);
                if (emit_raw_in_dir && g_raw.empty()) g_raw = base + stem + ".dng";
                if (emit_rgb_in_dir && g_rgb.empty()) g_rgb = base + stem + rgb_format_ext(cfg.rgb_format);
                if (emit_field_in_dir && g_fld.empty()) {
                    g_fld = base + stem + "_field.png";
                    cluster_cfg.field_from_outdir = true;
                }
            }
            if (n_grp > 1 || clusters.size() > 1) {
                int tag_id = global_groups_done + 1;
                if(!out_raw_path.empty()) g_raw = add_group_suffix(g_raw, tag_id, std::max(2, n_grp));
                if(!out_rgb_path.empty()) g_rgb = add_group_suffix(g_rgb, tag_id, std::max(2, n_grp));
                if(!out_field_path.empty()) g_fld = add_group_suffix(g_fld, tag_id, std::max(2, n_grp));
            }

            bool is_last_in_cluster = (gi == n_grp - 1);
            PipelineConfig group_cfg=cluster_cfg;
            if (n_grp>1 || clusters.size()>1) {
                const int tag=global_groups_done+1;
                group_cfg.optics_report=add_group_suffix(group_cfg.optics_report,tag,2);
                group_cfg.export_lcp=add_group_suffix(group_cfg.export_lcp,tag,2);
                if(!group_cfg.dataset_dir.empty()) group_cfg.dataset_dir+="/group_"+std::to_string(tag);
            }
            if (group_cfg.optics_only && group_cfg.optics_report.empty() && !out_dir_path.empty())
                group_cfg.optics_report=out_dir_path+"/"+grp.naming_stem+"_optics";
            std::vector<int> selected = grp.light_uframe_indices;
            const int requested_anchor=group_cfg.anchor_mode>0 ? group_cfg.anchor_mode-1 :
                (group_cfg.anchor_mode==-1 ? 0 : (group_cfg.anchor_mode==-2 ? (int)uframes.size()/2 : -1));
            if (n_grp == 1 && requested_anchor>=0) {
                const int anchor = requested_anchor;
                auto pos = std::find(selected.begin(), selected.end(), anchor);
                if (pos != selected.end()) selected.erase(pos);
                else if (cfg.max_frames > 0 && (int)selected.size() >= cfg.max_frames) selected.pop_back();
                selected.insert(selected.begin(), anchor);
            }
            int rc = execute_sr_group(
                uframes, selected,
                is_last_in_cluster, g_raw, g_rgb, g_fld, group_cfg, gi + 1, n_grp, t_wall0
            );
            if (rc != 0) return rc;
            ++global_groups_done;
        }
    }

    if (global_groups_done == 0) {
        std::fprintf(stderr, "Batch scan finished: 0 super-resolvable burst groups (>= 4 valid frames) found (%d smaller/singleton shot(s) skipped).\n",
            skipped_small_clusters);
        return 3;
    }
    if (cfg.progress_json) {
        std::fprintf(stderr, "{\"event\":\"done\",\"groups_completed\":%d,\"wall_s\":%.3f}\n",
            global_groups_done, elapsed_s(t_wall0));
        std::fflush(stderr);
    }
    if (clusters.size() > 1 || global_groups_done > 1) {
        std::fprintf(stderr, "Batch completed: exported %d super-resolved group(s) (%d non-burst cluster(s) skipped) in %.2fs.\n",
            global_groups_done, skipped_small_clusters, elapsed_s(t_wall0));
    }
    return 0;
}

int main(int argc, char** argv) {
    try {
        return run_cli(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Error: %s\n", error.what());
        return 1;
    }
}
