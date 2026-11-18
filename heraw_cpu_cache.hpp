// Runtime CPU cache-hierarchy, heterogeneous topology (AMD Zen5+Zen5c Strix Point,
// Intel P+E / P+E+LPE 3-tier, Apple Silicon P+E, X3D), thread affinity/QoS, and
// AVX-512 capability detection. C++20, header-only.
#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#elif defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#include <immintrin.h>
#endif
#define HERAW_ARCH_X86 1
#else
#define HERAW_ARCH_X86 0
#endif

// AVX-512 kernels are archived behind HERAW_ENABLE_ARCHIVED_AVX512; production x86 builds
// standardize on 256-bit AVX2+FMA (see docs/ARCHIVE_AVX512.md).
#if defined(HERAW_ENABLE_ARCHIVED_AVX512) && HERAW_ARCH_X86 && (defined(__GNUC__) || defined(__clang__))
#define HERAW_AVX512_ENABLED 1
#else
#define HERAW_AVX512_ENABLED 0
#endif

namespace heraw_cache {

enum class CoreTier : uint8_t {
    LOW_POWER = 0, // Intel LP-E (SoC island, often L3-less)
    EFF       = 1, // Intel E-core / AMD Zen 5c / Apple E-core
    PERF      = 2  // Intel P-core / AMD Zen 5 / Apple P-core / Homogeneous core
};

enum class CpuCorePolicy : uint8_t {
    AUTO          = 0, // Smart hybrid: excludes LP-E; Zen5(2T)+Zen5c(1T) on asymmetric CCX; P+E elsewhere
    PERF_ONLY     = 1, // P-cores / Zen 5 big cores only
    PHYSICAL_ONLY = 2, // 1 thread per physical core (P + E, no SMT siblings, no LP-E)
    EFF_ONLY      = 3, // E-cores / Zen 5c / Apple background E-cores only
    ALL_LOGICAL   = 4  // Every logical processor including SMT & LP-E
};

enum class TaskPriority : uint8_t {
    NORMAL       = 0, // Standard interactive/compute; disables Win11 EcoQoS throttling
    HIGH         = 1, // Above-normal / User-Initiated boost
    BELOW_NORMAL = 2, // Yields smoothly to foreground apps
    IDLE_ECO     = 3  // Background silent: Win11 EcoQoS / macOS QOS_CLASS_BACKGROUND / Linux nice 19
};

enum class SimdPolicy : uint8_t {
    AUTO   = 0,
    AVX512 = 1,
    AVX2   = 2
};

struct LogicalCpu {
    uint16_t group = 0;       // Windows processor group (0 on Linux/macOS)
    uint8_t  group_bit = 0;   // 0..63 within processor group (or Linux cpu_id & 63)
    uint16_t os_cpu_id = 0;   // Linear OS logical CPU index (for pthread_setaffinity_np)
    uint16_t core_id = 0;     // Physical core index
    uint8_t  smt_idx = 0;     // 0 = primary thread on physical core, >=1 = SMT sibling
    int8_t   llc_slice = -1;  // L3/CCX slice index (-1 if L3-less, e.g. Intel LP-E)
    CoreTier tier = CoreTier::PERF;
};

inline CpuCorePolicy parse_core_policy(const std::string& s, bool* ok = nullptr) {
    if (ok) *ok = true;
    if (s.empty() || s == "auto") return CpuCorePolicy::AUTO;
    if (s == "perf" || s == "p" || s == "big") return CpuCorePolicy::PERF_ONLY;
    if (s == "physical" || s == "phys" || s == "no-smt" || s == "1t") return CpuCorePolicy::PHYSICAL_ONLY;
    if (s == "eff" || s == "e" || s == "eco") return CpuCorePolicy::EFF_ONLY;
    if (s == "all" || s == "full") return CpuCorePolicy::ALL_LOGICAL;
    if (ok) *ok = false;
    return CpuCorePolicy::AUTO;
}

inline const char* core_policy_name(CpuCorePolicy p) {
    switch (p) {
        case CpuCorePolicy::AUTO:          return "auto";
        case CpuCorePolicy::PERF_ONLY:     return "perf";
        case CpuCorePolicy::PHYSICAL_ONLY: return "physical";
        case CpuCorePolicy::EFF_ONLY:      return "eff";
        case CpuCorePolicy::ALL_LOGICAL:   return "all";
    }
    return "auto";
}

inline TaskPriority parse_task_priority(const std::string& s, bool* ok = nullptr) {
    if (ok) *ok = true;
    if (s.empty() || s == "normal" || s == "default") return TaskPriority::NORMAL;
    if (s == "high" || s == "above") return TaskPriority::HIGH;
    if (s == "below" || s == "low") return TaskPriority::BELOW_NORMAL;
    if (s == "idle" || s == "eco" || s == "background") return TaskPriority::IDLE_ECO;
    if (ok) *ok = false;
    return TaskPriority::NORMAL;
}

inline const char* task_priority_name(TaskPriority p) {
    switch (p) {
        case TaskPriority::NORMAL:       return "normal";
        case TaskPriority::HIGH:         return "high";
        case TaskPriority::BELOW_NORMAL: return "below";
        case TaskPriority::IDLE_ECO:     return "idle";
    }
    return "normal";
}

inline SimdPolicy parse_simd_policy(const std::string& s, bool* ok = nullptr) {
    if (ok) *ok = true;
    if (s.empty() || s == "auto") return SimdPolicy::AUTO;
    if (s == "avx512") return SimdPolicy::AVX512;
    if (s == "avx2" || s == "neon" || s == "base") return SimdPolicy::AVX2;
    if (ok) *ok = false;
    return SimdPolicy::AUTO;
}

inline const char* simd_policy_name(SimdPolicy s) {
    switch (s) {
        case SimdPolicy::AUTO:   return "auto";
        case SimdPolicy::AVX512: return "avx512";
        case SimdPolicy::AVX2:   return "avx2";
    }
    return "auto";
}

struct CacheProfile {
    std::size_t l1d_bytes = 32u * 1024u;
    std::size_t l2_per_core_bytes = 512u * 1024u;
    std::size_t l3_llc_bytes = 16u * 1024u * 1024u;   // Smallest non-zero L3 slice (e.g. 8 MB on Strix Point CCX1)
    std::size_t l3_max_slice_bytes = 16u * 1024u * 1024u;
    std::size_t l3_total_bytes = 16u * 1024u * 1024u;
    int physical_cores = 4;
    int logical_threads = 8;
    int perf_cores = 4;
    int eff_cores = 0;
    int low_power_cores = 0;
    int llc_slices = 1;
    bool is_3d_vcache = false;
    bool is_hybrid = false;
    bool is_asymmetric_llc = false; // e.g. Strix Point 16MB+8MB or 7950X3D 96MB+32MB
    bool has_avx512 = false;        // AVX512F + VL + BW + DQ + CD + OS XCR0 enabled
    bool avx512_256_datapath = false; // Mobile Zen 5 (Strix Point) double-pumped 256b AVX-512
    char arch_tag[64] = "generic";
    std::vector<LogicalCpu> cpus;

    std::size_t l1_tile_bytes = 20u * 1024u;
    std::size_t l2_worker_bytes = 280u * 1024u;
    std::size_t l3_band_bytes = 10u * 1024u * 1024u;

    int stage4_x_tile() const {
        if (l2_per_core_bytes >= 1280u * 1024u) return 256; // Intel i5/Ultra P-core / Apple Silicon
        if (l2_per_core_bytes >= 960u * 1024u) return 192;  // Zen 4 / Zen 5 / Strix Point (1 MB L2)
        return 128;                                         // Zen 3 / Zen+ (512 KB L2)
    }

    int dct_tile_x() const {
        if (l2_per_core_bytes >= 960u * 1024u) return 512;
        return 256;
    }

    int conv_tile_x() const {
        if (l1d_bytes >= 64u * 1024u) return 512;
        if (l1d_bytes >= 48u * 1024u || l2_per_core_bytes >= 960u * 1024u) return 384; // Zen 5 / Intel 48KB L1D
        return 256;
    }

    int ibp_tile() const {
        return (l2_per_core_bytes >= 960u * 1024u) ? 96 : 64;
    }

    int rl_band_rows(int W, int active_threads = 0) const {
        if (W <= 0) return 256;
        const std::size_t bytes_per_row = std::size_t(W) * 24u;
        std::size_t budget = l3_band_bytes;
        // Only apply multi-slice L3 boost on SYMMETRIC multi-CCD desktops; on asymmetric CCXs
        // (e.g. Strix Point 16MB + 8MB), keep budget anchored to the smaller 8MB CCX1 slice.
        if (llc_slices > 1 && !is_asymmetric_llc && active_threads > physical_cores / llc_slices) {
            budget = std::min(l3_total_bytes / 2u, l3_band_bytes + l3_band_bytes / 2u);
        }
        const int total_rows = int(budget / std::max<std::size_t>(1u, bytes_per_row));
        const int core_rows = total_rows - 48;
        return std::max(128, std::min(1536, (core_rows / 32) * 32));
    }

    int c13_col_strip(int n_frames) const {
        const std::size_t nf = std::size_t(std::max(1, n_frames));
        const int raw = int(l2_worker_bytes / std::max<std::size_t>(64u, nf * 16u));
        return std::max(64, std::min(512, (raw / 64) * 64));
    }
};

struct PowerStatus {
    bool has_battery = false;
    bool on_battery = false;
    int battery_percent = -1;
};

inline PowerStatus query_power_status() noexcept {
    PowerStatus ps{};
    if (const char* btest = std::getenv("NIJIN_BATTERY_TEST")) {
        if (std::strcmp(btest, "1") == 0 || std::strcmp(btest, "on") == 0) {
            ps.has_battery = true;
            ps.on_battery = false;
            ps.battery_percent = 80;
            return ps;
        }
        if (std::strcmp(btest, "0") == 0 || std::strcmp(btest, "off") == 0) {
            ps.has_battery = false;
            ps.on_battery = false;
            return ps;
        }
    }
    if (const char* env = std::getenv("NIJIN_POWER_SOURCE")) {
        if (std::strcmp(env, "dc") == 0 || std::strcmp(env, "battery") == 0) {
            ps.has_battery = true;
            ps.on_battery = true;
            return ps;
        }
        if (std::strcmp(env, "ac") == 0) {
            ps.has_battery = true;
            ps.on_battery = false;
            return ps;
        }
    }
#if defined(_WIN32)
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        ps.has_battery = (sps.BatteryFlag != 128u && sps.BatteryFlag != 255u);
        ps.on_battery = ps.has_battery && (sps.ACLineStatus == 0u);
        if (sps.BatteryLifePercent <= 100u) ps.battery_percent = int(sps.BatteryLifePercent);
    }
#elif defined(__linux__)
    auto rd_int = [](const char* path) -> int {
        FILE* fp = std::fopen(path, "r");
        if (!fp) return -1;
        int v = -1;
        if (std::fscanf(fp, "%d", &v) != 1) v = -1;
        std::fclose(fp);
        return v;
    };
    const int cap0 = rd_int("/sys/class/power_supply/BAT0/capacity");
    const int cap1 = (cap0 < 0) ? rd_int("/sys/class/power_supply/BAT1/capacity") : cap0;
    if (cap1 >= 0) {
        ps.has_battery = true;
        ps.battery_percent = std::min(100, cap1);
        static const char* const ac_paths[] = {
            "/sys/class/power_supply/AC/online",
            "/sys/class/power_supply/AC0/online",
            "/sys/class/power_supply/ADP0/online",
            "/sys/class/power_supply/ADP1/online",
            "/sys/class/power_supply/ACAD/online"
        };
        for (const char* p : ac_paths) {
            const int v = rd_int(p);
            if (v == 0 || v == 1) {
                ps.on_battery = (v == 0);
                break;
            }
        }
    }
#endif
    return ps;
}

// Pure deterministic planner: orders and selects LogicalCpu slots for a given policy & thread cap.
inline std::vector<LogicalCpu> plan_worker_cpus(
    const CacheProfile& prof, CpuCorePolicy policy, int requested_threads = 0, bool on_battery = false
) {
    if (prof.cpus.empty()) {
        const int fallback_n = requested_threads > 0 ? requested_threads : std::max(1, prof.logical_threads);
        std::vector<LogicalCpu> dummy(static_cast<std::size_t>(fallback_n));
        for (int i = 0; i < fallback_n; ++i) {
            dummy[std::size_t(i)].os_cpu_id = uint16_t(i);
            dummy[std::size_t(i)].core_id = uint16_t(i);
        }
        return dummy;
    }

    auto order_key = [](const LogicalCpu& a, const LogicalCpu& b) {
        // 1. Primary physical thread (smt_idx == 0) before SMT siblings
        if ((a.smt_idx == 0) != (b.smt_idx == 0)) return (a.smt_idx == 0) > (b.smt_idx == 0);
        // 2. Higher core tier first (PERF > EFF > LOW_POWER)
        if (a.tier != b.tier) return uint8_t(a.tier) > uint8_t(b.tier);
        // 3. Group by LLC/CCX slice so contiguous worker IDs share L3 locality
        if (a.llc_slice != b.llc_slice) return a.llc_slice < b.llc_slice;
        if (a.smt_idx != b.smt_idx) return a.smt_idx < b.smt_idx;
        return a.os_cpu_id < b.os_cpu_id;
    };

    std::vector<LogicalCpu> pool;
    pool.reserve(prof.cpus.size());

    auto collect = [&](auto pred) {
        pool.clear();
        for (const auto& c : prof.cpus) {
            if (pred(c)) pool.push_back(c);
        }
    };

    switch (policy) {
        case CpuCorePolicy::PERF_ONLY:
            collect([](const LogicalCpu& c) { return c.tier == CoreTier::PERF; });
            break;
        case CpuCorePolicy::PHYSICAL_ONLY:
            collect([](const LogicalCpu& c) { return c.smt_idx == 0 && c.tier != CoreTier::LOW_POWER; });
            break;
        case CpuCorePolicy::EFF_ONLY:
            collect([](const LogicalCpu& c) { return c.tier == CoreTier::EFF; });
            if (pool.empty()) {
                collect([](const LogicalCpu& c) { return c.tier == CoreTier::LOW_POWER; });
            }
            break;
        case CpuCorePolicy::ALL_LOGICAL:
            pool = prof.cpus;
            break;
        case CpuCorePolicy::AUTO:
        default: {
            // On asymmetric-L3 hybrid APUs with small E-cluster L3 (e.g. Strix Point 16MB Zen5 + 8MB Zen5c),
            // run 2T SMT on PERF cores + 1T physical-only on EFF cores (protects the 8MB CCX1 L3!),
            // under DC battery PPT walls trim SMT siblings across all cores to preserve per-core clock/voltage,
            // and always exclude L3-less LOW_POWER (Intel LP-E) island cores.
            const bool small_eff_l3 = prof.is_hybrid && prof.is_asymmetric_llc &&
                                      prof.l3_llc_bytes <= 12u * 1024u * 1024u;
            collect([&](const LogicalCpu& c) {
                if (c.tier == CoreTier::LOW_POWER) return false;
                if (on_battery && c.smt_idx > 0) return false;
                if (small_eff_l3 && c.tier == CoreTier::EFF && c.smt_idx > 0) return false;
                return true;
            });
            break;
        }
    }

    // Graceful fallback if homogeneous CPU or single-tier system had no matching secondary tier
    if (pool.empty()) {
        collect([](const LogicalCpu& c) { return c.smt_idx == 0; });
        if (pool.empty()) pool = prof.cpus;
    }

    std::sort(pool.begin(), pool.end(), order_key);

    if (requested_threads > 0) {
        if (int(pool.size()) > requested_threads) {
            pool.resize(std::size_t(requested_threads));
        } else if (int(pool.size()) < requested_threads && policy == CpuCorePolicy::AUTO) {
            // If user explicitly asked for more threads than AUTO's pruned pool (e.g. --threads 20 on HX 365),
            // top up from remaining non-LP-E SMT siblings, then LP-E if still requested.
            std::vector<LogicalCpu> extras;
            for (const auto& c : prof.cpus) {
                bool used = false;
                for (const auto& u : pool) {
                    if (u.group == c.group && u.group_bit == c.group_bit && u.os_cpu_id == c.os_cpu_id) {
                        used = true;
                        break;
                    }
                }
                if (!used) extras.push_back(c);
            }
            std::sort(extras.begin(), extras.end(), order_key);
            for (const auto& e : extras) {
                if (int(pool.size()) >= requested_threads) break;
                pool.push_back(e);
            }
        }
    }

    // Re-group active workers by LLC slice so adjacent worker IDs live on the same CCX/CCD
    std::stable_sort(pool.begin(), pool.end(), [](const LogicalCpu& a, const LogicalCpu& b) {
        if (a.llc_slice != b.llc_slice) return a.llc_slice < b.llc_slice;
        if (a.tier != b.tier) return uint8_t(a.tier) > uint8_t(b.tier);
        return a.smt_idx < b.smt_idx;
    });
    return pool;
}

inline int recommended_thread_count(
    const CacheProfile& prof, CpuCorePolicy policy, int requested_threads = 0, bool on_battery = false
) {
    if (requested_threads > 0) return requested_threads;
    auto planned = plan_worker_cpus(prof, policy, 0, on_battery);
    return std::max(1, int(planned.size()));
}

namespace detail {

inline constexpr int popcount_u64(uint64_t x) noexcept {
    return std::popcount(x);
}

inline constexpr int ctz_u64(uint64_t x) noexcept {
    return std::countr_zero(x);
}

inline std::size_t clamp_sz(std::size_t v, std::size_t lo, std::size_t hi) {
    return std::max(lo, std::min(hi, v));
}

#if HERAW_ARCH_X86
inline void x86_cpuid_count(unsigned leaf, unsigned sub, unsigned& a, unsigned& b, unsigned& c, unsigned& d) {
#if defined(_MSC_VER) && !defined(__clang__)
    int regs[4] = {};
    __cpuidex(regs, int(leaf), int(sub));
    a = unsigned(regs[0]); b = unsigned(regs[1]); c = unsigned(regs[2]); d = unsigned(regs[3]);
#else
    __cpuid_count(leaf, sub, a, b, c, d);
#endif
}

inline uint64_t x86_xgetbv0() {
#if defined(_MSC_VER) && !defined(__clang__)
    return _xgetbv(0);
#else
    uint32_t eax = 0, edx = 0;
    __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
    return (uint64_t(edx) << 32) | eax;
#endif
}

inline void probe_x86_simd(bool& out_avx512, bool& out_256_dp) {
    out_avx512 = false;
    out_256_dp = false;
    unsigned a = 0, b = 0, c = 0, d = 0;
    x86_cpuid_count(0, 0, a, b, c, d);
    if (a < 7) return;
    const bool is_amd = (b == 0x68747541u && d == 0x69746E65u && c == 0x444D4163u); // "AuthenticAMD"
    x86_cpuid_count(1, 0, a, b, c, d);
    const unsigned fam_base = (a >> 8) & 0xFu;
    const unsigned fam = (fam_base == 0xFu) ? (fam_base + ((a >> 20) & 0xFFu)) : fam_base;
    const unsigned mod = ((a >> 4) & 0xFu) | (((a >> 16) & 0xFu) << 4);
    // OSXSAVE (ECX bit 27) + AVX (ECX bit 28)
    if ((c & ((1u << 27) | (1u << 28))) != ((1u << 27) | (1u << 28))) return;
    // XCR0 must enable XMM (bit 1), YMM (bit 2), opmask k0-k7 (bit 5), ZMM_Hi256 (bit 6), Hi16_ZMM (bit 7)
    const uint64_t xcr0 = x86_xgetbv0();
    if ((xcr0 & 0xE6ULL) != 0xE6ULL) return;

    x86_cpuid_count(7, 0, a, b, c, d);
    // EBX: AVX512F(16), AVX512DQ(17), AVX512CD(28), AVX512BW(30), AVX512VL(31)
    constexpr unsigned kNeedEbx = (1u << 16) | (1u << 17) | (1u << 28) | (1u << 30) | (1u << 31);
    if ((b & kNeedEbx) != kNeedEbx) return;
    out_avx512 = true;

    if (is_amd) {
        // Zen 4 (Family 0x19) and mobile Zen 5 Strix/Kraken Point (Family 0x1A Models 0x20..0x3F / 0x60..0x7F)
        // execute 512b AVX-512 double-pumped over 256b pipes; Granite Ridge / Turin (0x1A Models 0x00..0x1F, 0x40..0x4F) have 512b pipes.
        if (fam == 0x19u || (fam == 0x1Au && ((mod >= 0x20u && mod <= 0x3Fu) || (mod >= 0x60u && mod <= 0x7Fu)))) {
            out_256_dp = true;
        }
    }
}
#endif

inline void finalize_budgets(CacheProfile& p) {
    p.l1d_bytes = clamp_sz(p.l1d_bytes, 16u * 1024u, 512u * 1024u);
    p.l2_per_core_bytes = clamp_sz(p.l2_per_core_bytes, 128u * 1024u, 16u * 1024u * 1024u);
    p.l3_llc_bytes = clamp_sz(p.l3_llc_bytes, 2u * 1024u * 1024u, 256u * 1024u * 1024u);
    if (p.l3_max_slice_bytes < p.l3_llc_bytes) p.l3_max_slice_bytes = p.l3_llc_bytes;
    if (p.l3_total_bytes < p.l3_llc_bytes) p.l3_total_bytes = p.l3_llc_bytes;
    p.is_3d_vcache = (p.l3_max_slice_bytes >= 64u * 1024u * 1024u);
    p.is_asymmetric_llc = (p.llc_slices > 1 && p.l3_max_slice_bytes >= p.l3_llc_bytes + (p.l3_llc_bytes / 2u));

    p.l1_tile_bytes = (p.l1d_bytes * 3u) / 5u;
    const bool smt_active = (p.logical_threads >= p.physical_cores * 2 && p.physical_cores >= 2);
    const std::size_t eff_l2 = smt_active ? (p.l2_per_core_bytes * 3u) / 4u : p.l2_per_core_bytes;
    p.l2_worker_bytes = clamp_sz((eff_l2 * 3u) / 5u, 192u * 1024u, 1536u * 1024u);
    p.l3_band_bytes = clamp_sz((p.l3_llc_bytes * 3u) / 5u,
                               2u * 1024u * 1024u,
                               72u * 1024u * 1024u);
}

#if defined(__linux__)
inline long read_sysfs_long(const char* path, long fallback = -1) {
    FILE* f = std::fopen(path, "r");
    if (!f) return fallback;
    char buf[64] = {};
    const bool ok = (std::fgets(buf, sizeof(buf), f) != nullptr);
    std::fclose(f);
    if (!ok) return fallback;
    char* endp = nullptr;
    long v = std::strtol(buf, &endp, 10);
    if (endp == buf) return fallback;
    if (*endp == 'K' || *endp == 'k') v *= 1024L;
    else if (*endp == 'M' || *endp == 'm') v *= 1024L * 1024L;
    return v;
}
#endif

inline CacheProfile probe_host_cache() {
    CacheProfile p;
    const unsigned hc = std::thread::hardware_concurrency();
    p.logical_threads = hc > 0 ? int(hc) : 8;
    p.physical_cores = std::max(1, p.logical_threads / 2);
    p.perf_cores = p.physical_cores;

#if HERAW_ARCH_X86
    probe_x86_simd(p.has_avx512, p.avx512_256_datapath);
#endif

#if defined(_WIN32)
    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &len);
    if (len > 0) {
        std::vector<uint8_t> buf(len);
        if (GetLogicalProcessorInformationEx(RelationAll,
                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len)) {
            struct RawCore {
                uint16_t core_id;
                uint8_t eff_class;
                std::vector<std::pair<uint16_t, uint8_t>> threads; // (group, bit)
            };
            struct L3Slice {
                std::size_t bytes;
                std::vector<GROUP_AFFINITY> masks;
            };
            std::vector<RawCore> raw_cores;
            std::vector<L3Slice> l3_slices;
            std::size_t l1d_min = 0, l2_min = 0;
            uint8_t min_ec = 255, max_ec = 0;
            bool ec_seen[256] = {};
            int distinct_ec = 0;

            DWORD off = 0;
            while (off + sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX) <= len) {
                const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buf.data() + off);
                if (info->Size == 0) break;
                if (info->Relationship == RelationProcessorCore) {
                    RawCore rc{};
                    rc.core_id = uint16_t(raw_cores.size());
                    rc.eff_class = info->Processor.EfficiencyClass;
                    if (!ec_seen[rc.eff_class]) { ec_seen[rc.eff_class] = true; ++distinct_ec; }
                    min_ec = std::min(min_ec, rc.eff_class);
                    max_ec = std::max(max_ec, rc.eff_class);
                    for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
                        uint64_t m = uint64_t(info->Processor.GroupMask[g].Mask);
                        while (m) {
                            const int bit = ctz_u64(m);
                            rc.threads.push_back({uint16_t(info->Processor.GroupMask[g].Group), uint8_t(bit)});
                            m &= (m - 1ULL);
                        }
                    }
                    if (!rc.threads.empty()) raw_cores.push_back(std::move(rc));
                } else if (info->Relationship == RelationCache) {
                    const CACHE_RELATIONSHIP& c = info->Cache;
                    if (c.Type == CacheData || c.Type == CacheUnified) {
                        const std::size_t sz = std::size_t(c.CacheSize);
                        if (c.Level == 1 && sz > 0) {
                            l1d_min = (l1d_min == 0) ? sz : std::min(l1d_min, sz);
                        } else if (c.Level == 2 && sz > 0) {
                            const int sharing = std::max(1, popcount_u64(uint64_t(c.GroupMask.Mask)));
                            const std::size_t per_core_est = (sharing >= 4) ? (sz / std::size_t(sharing / 2)) : sz;
                            l2_min = (l2_min == 0) ? per_core_est : std::min(l2_min, per_core_est);
                        } else if (c.Level == 3 && sz > 0) {
                            L3Slice sl{sz, {}};
                            const WORD gc = c.GroupCount > 0 ? c.GroupCount : 1;
                            for (WORD g = 0; g < gc; ++g) sl.masks.push_back(c.GroupMasks[g]);
                            l3_slices.push_back(std::move(sl));
                        }
                    }
                }
                off += info->Size;
            }

            if (l1d_min > 0) p.l1d_bytes = l1d_min;
            if (l2_min > 0) p.l2_per_core_bytes = l2_min;
            if (!l3_slices.empty()) {
                std::size_t l3_min = l3_slices[0].bytes, l3_max = l3_slices[0].bytes, l3_sum = 0;
                for (const auto& sl : l3_slices) {
                    l3_min = std::min(l3_min, sl.bytes);
                    l3_max = std::max(l3_max, sl.bytes);
                    l3_sum += sl.bytes;
                }
                p.l3_llc_bytes = l3_min;
                p.l3_max_slice_bytes = l3_max;
                p.l3_total_bytes = l3_sum;
                p.llc_slices = int(l3_slices.size());
            }

            if (!raw_cores.empty()) {
                p.physical_cores = int(raw_cores.size());
                p.perf_cores = 0; p.eff_cores = 0; p.low_power_cores = 0;
                uint16_t os_idx = 0;
                const bool asym_strix_l3 = (distinct_ec == 1 && l3_slices.size() >= 2 &&
                                            p.l3_max_slice_bytes <= 32u * 1024u * 1024u &&
                                            p.l3_llc_bytes <= 12u * 1024u * 1024u &&
                                            p.l3_max_slice_bytes >= p.l3_llc_bytes * 2u);
                for (const auto& rc : raw_cores) {
                    const auto [g0, b0] = rc.threads[0];
                    int8_t slice_id = -1;
                    std::size_t slice_bytes = 0;
                    for (std::size_t si = 0; si < l3_slices.size(); ++si) {
                        for (const auto& gm : l3_slices[si].masks) {
                            if (gm.Group == g0 && ((uint64_t(gm.Mask) >> b0) & 1ULL)) {
                                slice_id = int8_t(si);
                                slice_bytes = l3_slices[si].bytes;
                                break;
                            }
                        }
                        if (slice_id >= 0) break;
                    }

                    CoreTier tier = CoreTier::PERF;
                    if (distinct_ec >= 3) {
                        if (rc.eff_class == max_ec) tier = CoreTier::PERF;
                        else if (rc.eff_class == min_ec) tier = CoreTier::LOW_POWER;
                        else tier = CoreTier::EFF;
                    } else if (distinct_ec == 2) {
                        if (rc.eff_class == max_ec) tier = CoreTier::PERF;
                        else tier = (!l3_slices.empty() && slice_id < 0) ? CoreTier::LOW_POWER : CoreTier::EFF;
                    } else if (asym_strix_l3 && slice_id >= 0) {
                        // Fallback if OS EfficiencyClass is uniform on asymmetric Strix/Kraken CCX (16MB vs 8MB)
                        tier = (slice_bytes == p.l3_max_slice_bytes) ? CoreTier::PERF : CoreTier::EFF;
                    } else if (!l3_slices.empty() && slice_id < 0) {
                        tier = CoreTier::LOW_POWER;
                    }

                    if (tier == CoreTier::PERF) ++p.perf_cores;
                    else if (tier == CoreTier::EFF) ++p.eff_cores;
                    else ++p.low_power_cores;

                    for (std::size_t ti = 0; ti < rc.threads.size(); ++ti) {
                        LogicalCpu lc{};
                        lc.group = rc.threads[ti].first;
                        lc.group_bit = rc.threads[ti].second;
                        lc.os_cpu_id = os_idx++;
                        lc.core_id = rc.core_id;
                        lc.smt_idx = uint8_t(ti);
                        lc.llc_slice = slice_id;
                        lc.tier = tier;
                        p.cpus.push_back(lc);
                    }
                }
                p.logical_threads = int(p.cpus.size());
                p.is_hybrid = (p.eff_cores > 0 || p.low_power_cores > 0);
            }
        }
    }
    std::snprintf(p.arch_tag, sizeof(p.arch_tag), "win-x64%s%s%s",
                  p.is_hybrid ? "-hybrid" : "",
                  (p.l3_max_slice_bytes >= 64u * 1024u * 1024u) ? "-x3d" : "",
                  (HERAW_AVX512_ENABLED && p.has_avx512) ? (p.avx512_256_datapath ? "-avx512dp" : "-avx512") : "-avx2");

#elif defined(__APPLE__)
    auto sysctl_u64 = [](const char* name) -> std::size_t {
        uint64_t val = 0;
        std::size_t sz = sizeof(val);
        if (sysctlbyname(name, &val, &sz, nullptr, 0) == 0 && sz > 0 && val > 0) {
            if (sz == 4) {
                uint32_t v32 = 0;
                std::memcpy(&v32, &val, 4);
                return std::size_t(v32);
            }
            return std::size_t(val);
        }
        return 0;
    };
    const std::size_t p_cores = sysctl_u64("hw.perflevel0.physicalcpu");
    const std::size_t e_cores = sysctl_u64("hw.perflevel1.physicalcpu");
    const std::size_t all_phys = sysctl_u64("hw.physicalcpu");
    const std::size_t all_log = sysctl_u64("hw.logicalcpu");
    if (all_phys > 0) p.physical_cores = int(all_phys);
    if (all_log > 0) p.logical_threads = int(all_log);
    p.perf_cores = p_cores > 0 ? int(p_cores) : p.physical_cores;
    p.eff_cores = int(e_cores);
    p.is_hybrid = (p.eff_cores > 0 && p.perf_cores > 0);

    const std::size_t l1d = sysctl_u64("hw.perflevel0.l1dcachesize");
    p.l1d_bytes = l1d > 0 ? l1d : std::max<std::size_t>(32u * 1024u, sysctl_u64("hw.l1dcachesize"));

    const std::size_t pl2 = sysctl_u64("hw.perflevel0.l2cachesize");
    const std::size_t l2_std = sysctl_u64("hw.l2cachesize");
    const std::size_t l3_std = sysctl_u64("hw.l3cachesize");
    const std::size_t cpus_per_l2 = std::max<std::size_t>(1u, sysctl_u64("hw.perflevel0.cpusperl2"));

    if (pl2 > 0) {
        p.l2_per_core_bytes = std::max<std::size_t>(1024u * 1024u, pl2 / cpus_per_l2);
        const std::size_t clusters = (p_cores > cpus_per_l2) ? ((p_cores + cpus_per_l2 - 1u) / cpus_per_l2) : 1u;
        p.llc_slices = int(clusters);
        p.l3_llc_bytes = pl2 * clusters;
        p.l3_max_slice_bytes = p.l3_llc_bytes;
        p.l3_total_bytes = p.l3_llc_bytes;
        std::snprintf(p.arch_tag, sizeof(p.arch_tag), "apple-silicon-%dp%de-%zub",
                      p.perf_cores, p.eff_cores, p.l3_llc_bytes >> 20);
    } else {
        if (l2_std > 0) p.l2_per_core_bytes = l2_std;
        if (l3_std > 0) { p.l3_llc_bytes = l3_std; p.l3_max_slice_bytes = l3_std; p.l3_total_bytes = l3_std; }
        std::snprintf(p.arch_tag, sizeof(p.arch_tag), "macos");
    }
    const bool mac_smt = (!p.is_hybrid && p.logical_threads >= p.physical_cores * 2 && p.physical_cores >= 2);
    for (int i = 0; i < p.logical_threads; ++i) {
        LogicalCpu lc{};
        lc.os_cpu_id = uint16_t(i);
        lc.core_id = mac_smt ? uint16_t(i / 2) : uint16_t(i);
        lc.smt_idx = mac_smt ? uint8_t(i & 1) : 0u;
        lc.llc_slice = 0;
        lc.tier = (i < p.perf_cores || !p.is_hybrid) ? CoreTier::PERF : CoreTier::EFF;
        p.cpus.push_back(lc);
    }

#elif defined(__linux__)
#if HERAW_ARCH_X86
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    char vendor[13] = {};
    x86_cpuid_count(0, 0, eax, ebx, ecx, edx);
    std::memcpy(vendor + 0, &ebx, 4);
    std::memcpy(vendor + 4, &edx, 4);
    std::memcpy(vendor + 8, &ecx, 4);
    const bool is_amd = (std::strcmp(vendor, "AuthenticAMD") == 0);
    unsigned leaf = 0x04u;
    if (is_amd) {
        unsigned ext_max = 0, b0 = 0, c0 = 0, d0 = 0;
        x86_cpuid_count(0x80000000u, 0, ext_max, b0, c0, d0);
        if (ext_max >= 0x8000001Du) leaf = 0x8000001Du;
    }
    for (unsigned sub = 0; sub < 16; ++sub) {
        x86_cpuid_count(leaf, sub, eax, ebx, ecx, edx);
        const unsigned ctype = eax & 0x1Fu;
        if (ctype == 0) break;
        if (ctype != 1 && ctype != 3) continue;
        const unsigned level = (eax >> 5) & 0x7u;
        const std::size_t line_sz = std::size_t(ebx & 0xFFFu) + 1u;
        const std::size_t parts = std::size_t((ebx >> 12) & 0x3FFu) + 1u;
        const std::size_t ways = std::size_t((ebx >> 22) & 0x3FFu) + 1u;
        const std::size_t sets = std::size_t(ecx) + 1u;
        const std::size_t sz = line_sz * parts * ways * sets;
        if (level == 1 && sz > 0) {
            p.l1d_bytes = sz;
            const unsigned l1_sharing = ((eax >> 14) & 0xFFFu) + 1u;
            p.physical_cores = std::max(1, int((unsigned(p.logical_threads) + l1_sharing - 1u) / l1_sharing));
        } else if (level == 2 && sz > 0) {
            p.l2_per_core_bytes = sz;
        } else if (level == 3 && sz > 0) {
            p.l3_llc_bytes = sz;
            p.l3_max_slice_bytes = sz;
            const unsigned sharing = ((eax >> 14) & 0xFFFu) + 1u;
            p.llc_slices = std::max(1, int((unsigned(p.logical_threads) + sharing - 1u) / sharing));
            p.l3_total_bytes = sz * std::size_t(p.llc_slices);
        }
    }
#endif
    // Enrich with Linux sysfs per-CPU core_id, max_freq/capacity, and L3 slice topology when available
    struct LinuxCpuRaw { int cpu; int core; long freq; long l3_bytes; int l3_first_cpu; };
    std::vector<LinuxCpuRaw> lraw;
    long max_freq = 0, min_freq = (1L << 60);
    for (int cpu = 0; cpu < std::min(512, p.logical_threads * 2 + 16); ++cpu) {
        char path[160];
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", cpu);
        const long cid = read_sysfs_long(path, -1);
        if (cid < 0) continue;
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
        long fq = read_sysfs_long(path, -1);
        if (fq <= 0) {
            std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpu_capacity", cpu);
            fq = read_sysfs_long(path, 1024);
        }
        if (fq > 0) { max_freq = std::max(max_freq, fq); min_freq = std::min(min_freq, fq); }
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index3/size", cpu);
        const long l3b = read_sysfs_long(path, -1);
        int l3_head = -1;
        std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cache/index3/shared_cpu_list", cpu);
        if (FILE* sf = std::fopen(path, "r")) {
            int first_id = -1;
            if (std::fscanf(sf, "%d", &first_id) == 1) l3_head = first_id;
            std::fclose(sf);
        }
        lraw.push_back({cpu, int(cid), fq, l3b, l3_head});
    }
    if (!lraw.empty()) {
        std::vector<int> slice_heads;
        std::size_t l3_min = 0, l3_max = 0, l3_sum = 0;
        for (const auto& r : lraw) {
            if (r.l3_first_cpu >= 0 && r.l3_bytes > 0 &&
                std::find(slice_heads.begin(), slice_heads.end(), r.l3_first_cpu) == slice_heads.end()) {
                slice_heads.push_back(r.l3_first_cpu);
                const std::size_t b = std::size_t(r.l3_bytes);
                l3_min = (l3_min == 0) ? b : std::min(l3_min, b);
                l3_max = std::max(l3_max, b);
                l3_sum += b;
            }
        }
        if (!slice_heads.empty()) {
            p.llc_slices = int(slice_heads.size());
            p.l3_llc_bytes = l3_min;
            p.l3_max_slice_bytes = l3_max;
            p.l3_total_bytes = l3_sum;
        }
        const bool freq_hybrid = (max_freq > 0 && min_freq > 0 && max_freq * 100 >= min_freq * 118);
        std::vector<int> seen_cores;
        std::vector<int> core_smt_cnt(4096, 0);
        p.perf_cores = 0; p.eff_cores = 0; p.low_power_cores = 0;
        for (const auto& r : lraw) {
            int8_t sl = -1;
            if (r.l3_first_cpu >= 0) {
                auto it = std::find(slice_heads.begin(), slice_heads.end(), r.l3_first_cpu);
                if (it != slice_heads.end()) sl = int8_t(it - slice_heads.begin());
            }
            CoreTier tier = CoreTier::PERF;
            if (!slice_heads.empty() && sl < 0) {
                tier = CoreTier::LOW_POWER;
            } else if (freq_hybrid && r.freq * 100 < max_freq * 88) {
                tier = CoreTier::EFF;
            } else if (!freq_hybrid && slice_heads.size() >= 2 && l3_max >= l3_min * 2 &&
                       l3_max <= 32u * 1024u * 1024u && r.l3_bytes > 0 && std::size_t(r.l3_bytes) == l3_min) {
                tier = CoreTier::EFF;
            }
            const int ckey = (r.core & 0xFFF);
            const int smt = core_smt_cnt[ckey]++;
            if (smt == 0) {
                seen_cores.push_back(r.core);
                if (tier == CoreTier::PERF) ++p.perf_cores;
                else if (tier == CoreTier::EFF) ++p.eff_cores;
                else ++p.low_power_cores;
            }
            LogicalCpu lc{};
            lc.os_cpu_id = uint16_t(r.cpu);
            lc.group_bit = uint8_t(r.cpu & 63);
            lc.core_id = uint16_t(r.core);
            lc.smt_idx = uint8_t(smt);
            lc.llc_slice = sl;
            lc.tier = tier;
            p.cpus.push_back(lc);
        }
        p.physical_cores = std::max(1, int(seen_cores.size()));
        p.logical_threads = int(p.cpus.size());
        p.is_hybrid = (p.eff_cores > 0 || p.low_power_cores > 0);
    }
    std::snprintf(p.arch_tag, sizeof(p.arch_tag), "linux%s%s",
                  p.is_hybrid ? "-hybrid" : "-x86",
                  (HERAW_AVX512_ENABLED && p.has_avx512) ? (p.avx512_256_datapath ? "-avx512dp" : "-avx512") : "-avx2");
#endif

    if (const char* env = std::getenv("NIJIN_CACHE_PROFILE")) {
        if (std::strcmp(env, "zen3") == 0) {
            p.l1d_bytes = 32u * 1024u;
            p.l2_per_core_bytes = 512u * 1024u;
            p.l3_llc_bytes = 32u * 1024u * 1024u;
            p.l3_max_slice_bytes = p.l3_llc_bytes;
            p.l3_total_bytes = p.l3_llc_bytes * std::size_t(std::max(1, p.llc_slices));
            std::snprintf(p.arch_tag, sizeof(p.arch_tag), "override-zen3");
        } else if (std::strcmp(env, "zen4") == 0) {
            p.l1d_bytes = 32u * 1024u;
            p.l2_per_core_bytes = 1024u * 1024u;
            p.l3_llc_bytes = 32u * 1024u * 1024u;
            p.l3_max_slice_bytes = p.l3_llc_bytes;
            p.l3_total_bytes = p.l3_llc_bytes * std::size_t(std::max(1, p.llc_slices));
            std::snprintf(p.arch_tag, sizeof(p.arch_tag), "override-zen4");
        } else if (std::strcmp(env, "x3d") == 0) {
            p.l1d_bytes = 32u * 1024u;
            p.l2_per_core_bytes = 1024u * 1024u;
            p.l3_llc_bytes = 96u * 1024u * 1024u;
            p.l3_max_slice_bytes = 96u * 1024u * 1024u;
            p.l3_total_bytes = 96u * 1024u * 1024u;
            p.llc_slices = 1;
            std::snprintf(p.arch_tag, sizeof(p.arch_tag), "override-x3d");
        } else if (std::strcmp(env, "i5") == 0) {
            p.l1d_bytes = 48u * 1024u;
            p.l2_per_core_bytes = 1280u * 1024u;
            p.l3_llc_bytes = 20u * 1024u * 1024u;
            p.l3_max_slice_bytes = 20u * 1024u * 1024u;
            p.l3_total_bytes = 20u * 1024u * 1024u;
            p.llc_slices = 1;
            std::snprintf(p.arch_tag, sizeof(p.arch_tag), "override-i5");
        } else if (std::strcmp(env, "strix") == 0 || std::strcmp(env, "hx365") == 0) {
            p.l1d_bytes = 48u * 1024u;
            p.l2_per_core_bytes = 1024u * 1024u;
            p.l3_llc_bytes = 8u * 1024u * 1024u;
            p.l3_max_slice_bytes = 16u * 1024u * 1024u;
            p.l3_total_bytes = 24u * 1024u * 1024u;
            p.llc_slices = 2;
            std::snprintf(p.arch_tag, sizeof(p.arch_tag), "override-strix");
        }
    }

    finalize_budgets(p);
    return p;
}

} // namespace detail

inline const CacheProfile& host_cache_profile() {
    static CacheProfile prof;
    static std::once_flag once;
    std::call_once(once, []() { prof = detail::probe_host_cache(); });
    return prof;
}

struct RuntimeExecConfig {
    CpuCorePolicy core_policy = CpuCorePolicy::AUTO;
    TaskPriority  priority    = TaskPriority::NORMAL;
    SimdPolicy    simd_policy = SimdPolicy::AUTO;
};

inline RuntimeExecConfig& runtime_exec_config() {
    static RuntimeExecConfig cfg;
    return cfg;
}

#if HERAW_AVX512_ENABLED
inline std::atomic<uint8_t>& avx512_fast_state() noexcept {
    static std::atomic<uint8_t> st{2u}; // 2 = uninitialized, 0 = off, 1 = on
    return st;
}

inline void refresh_avx512_state() noexcept {
    const auto& prof = host_cache_profile();
    bool allow = prof.has_avx512;
    if (allow) {
        if (const char* env = std::getenv("NIJIN_SIMD")) {
            if (std::strcmp(env, "avx2") == 0 || std::strcmp(env, "base") == 0) allow = false;
        }
    }
    if (runtime_exec_config().simd_policy == SimdPolicy::AVX2) allow = false;
    avx512_fast_state().store(allow ? 1u : 0u, std::memory_order_relaxed);
}

inline bool use_avx512() noexcept {
    const uint8_t s = avx512_fast_state().load(std::memory_order_relaxed);
    if (s <= 1u) [[likely]] return s != 0u;
    refresh_avx512_state();
    return avx512_fast_state().load(std::memory_order_relaxed) != 0u;
}
#else
inline void refresh_avx512_state() noexcept {}
inline constexpr bool use_avx512() noexcept { return false; }
#endif

// ---------------------------------------------------------------------------
// GPU Auto-Selection & APU / Pre-Xe2 iGPU Default-Off Policy (Windows/Linux)
// ---------------------------------------------------------------------------
inline bool ascii_ci_contains(const char* hay, const char* needle) noexcept {
    if (!hay || !needle || !*needle) return false;
    for (const char* h = hay; *h; ++h) {
        const char* p = h;
        const char* q = needle;
        while (*p && *q) {
            char cp = (*p >= 'A' && *p <= 'Z') ? char(*p + ('a' - 'A')) : *p;
            char cq = (*q >= 'A' && *q <= 'Z') ? char(*q + ('a' - 'A')) : *q;
            if (cp != cq) break;
            ++p; ++q;
        }
        if (!*q) return true;
    }
    return false;
}

inline bool is_intel_xe2_or_newer_igpu(uint32_t device_id, const char* dev_name) noexcept {
    // Lunar Lake Xe2-LPG (0x6400..0x64FF, e.g. Arc 130V/140V = 0x64A0/0x6420/0x64B0)
    // Panther Lake Xe3-LPG (0xB080..0xB0FF) & Wildcat/Nova Lake Xe3+ (0xFD80..0xFDFF)
    if ((device_id >= 0x6400u && device_id <= 0x64FFu) ||
        (device_id >= 0xB080u && device_id <= 0xB0FFu) ||
        (device_id >= 0xFD80u && device_id <= 0xFDFFu)) {
        return true;
    }
    if (dev_name && *dev_name) {
        if (ascii_ci_contains(dev_name, "130v") || ascii_ci_contains(dev_name, "140v") ||
            ascii_ci_contains(dev_name, "xe2")  || ascii_ci_contains(dev_name, "xe3")  ||
            ascii_ci_contains(dev_name, "lunar lake") || ascii_ci_contains(dev_name, "panther lake") ||
            ascii_ci_contains(dev_name, "celestial")) {
            return true;
        }
    }
    return false;
}

inline bool should_default_disable_igpu(
    bool is_discrete, uint32_t vendor_id, uint32_t device_id, const char* dev_name
) noexcept {
    if (is_discrete) return false;
    // AMD integrated APU / desktop Ryzen 2-CU Radeon Graphics: CPU SIMD matches or beats iGPU without UMA/PPT contention
    if (vendor_id == 0x1002u) return true;
    // Intel integrated GPUs: disable below Xe2 (UHD 6xx/7xx, Iris Plus/Xe, Meteor/Arrow Lake Xe-LPG)
    if (vendor_id == 0x8086u) return !is_intel_xe2_or_newer_igpu(device_id, dev_name);
    return true;
}

struct HostGpuPolicy {
    bool has_gpu = false;
    bool is_discrete = false;
    bool default_disable_gpu = false; // true when only an APU / <Xe2 iGPU (or no Vulkan GPU) is present
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    char device_name[256] = "";
};

#if defined(_WIN32)
inline HostGpuPolicy probe_win_gpu_policy() noexcept {
    HostGpuPolicy out{};
    if (const char* env = std::getenv("NIJIN_GPU_POLICY_TEST")) {
        if (std::strcmp(env, "apu") == 0) {
            out.has_gpu = true; out.is_discrete = false; out.default_disable_gpu = true;
            out.vendor_id = 0x1002u; out.device_id = 0x150Eu;
            std::snprintf(out.device_name, sizeof(out.device_name), "AMD Radeon 880M Graphics");
            return out;
        }
        if (std::strcmp(env, "intel_old") == 0) {
            out.has_gpu = true; out.is_discrete = false; out.default_disable_gpu = true;
            out.vendor_id = 0x8086u; out.device_id = 0x3EA5u;
            std::snprintf(out.device_name, sizeof(out.device_name), "Intel(R) Iris(R) Plus Graphics 655");
            return out;
        }
        if (std::strcmp(env, "intel_xe2") == 0) {
            out.has_gpu = true; out.is_discrete = false; out.default_disable_gpu = false;
            out.vendor_id = 0x8086u; out.device_id = 0x64A0u;
            std::snprintf(out.device_name, sizeof(out.device_name), "Intel(R) Arc(TM) 140V GPU");
            return out;
        }
        if (std::strcmp(env, "dgpu") == 0) {
            out.has_gpu = true; out.is_discrete = true; out.default_disable_gpu = false;
            out.vendor_id = 0x10DEu; out.device_id = 0x2782u;
            std::snprintf(out.device_name, sizeof(out.device_name), "NVIDIA GeForce RTX 4080 SUPER");
            return out;
        }
    }

    // Minimal self-contained Vulkan physical-device enumeration (no shader/device creation, ~2ms)
    HMODULE vk_lib = LoadLibraryW(L"vulkan-1.dll");
    if (!vk_lib) {
        out.default_disable_gpu = true;
        return out;
    }
    struct VkAppInfoMin {
        uint32_t sType; const void* pNext; const char* pAppName; uint32_t appVer;
        const char* pEngName; uint32_t engVer; uint32_t apiVer;
    };
    struct VkInstInfoMin {
        uint32_t sType; const void* pNext; uint32_t flags; const VkAppInfoMin* pApp;
        uint32_t layerCount; const char* const* ppLayers; uint32_t extCount; const char* const* ppExts;
    };
    struct alignas(8) VkPropsPrefix {
        uint32_t apiVersion;
        uint32_t driverVersion;
        uint32_t vendorID;
        uint32_t deviceID;
        uint32_t deviceType; // 1 = INTEGRATED_GPU, 2 = DISCRETE_GPU, 3 = VIRTUAL_GPU, 4 = CPU
        char     deviceName[256];
        uint8_t  pad[1024];  // generous room for VkPhysicalDeviceLimits + SparseProperties
    };
    struct VkQueueFamMin {
        uint32_t queueFlags;
        uint32_t queueCount;
        uint32_t timestampValidBits;
        uint32_t minImageTransferGranularity[3];
    };
    struct alignas(8) VkMemPropsMin {
        uint32_t memoryTypeCount;
        struct { uint32_t propertyFlags; uint32_t heapIndex; } memoryTypes[32];
        uint32_t memoryHeapCount;
        struct { uint64_t size; uint32_t flags; uint32_t reserved; } memoryHeaps[16];
    };
    using FnVoid = void(WINAPI*)();
    using FnGetProc = FnVoid(WINAPI*)(void*, const char*);
    using FnCreateInst = int32_t(WINAPI*)(const VkInstInfoMin*, const void*, void**);
    using FnDestroyInst = void(WINAPI*)(void*, const void*);
    using FnEnumDevs = int32_t(WINAPI*)(void*, uint32_t*, void**);
    using FnGetProps = void(WINAPI*)(void*, VkPropsPrefix*);
    using FnGetQF = void(WINAPI*)(void*, uint32_t*, VkQueueFamMin*);
    using FnGetMem = void(WINAPI*)(void*, VkMemPropsMin*);

    auto get_proc = reinterpret_cast<FnGetProc>(reinterpret_cast<void*>(GetProcAddress(vk_lib, "vkGetInstanceProcAddr")));
    if (get_proc) {
        auto create_inst = reinterpret_cast<FnCreateInst>(reinterpret_cast<void*>(get_proc(nullptr, "vkCreateInstance")));
        VkAppInfoMin app{0, nullptr, "nijinLBS_probe", 1, "nijinLBS", 1, (1u << 22)};
        VkInstInfoMin ici{1, nullptr, 0, &app, 0, nullptr, 0, nullptr};
        void* inst = nullptr;
        if (create_inst && create_inst(&ici, nullptr, &inst) == 0 && inst) {
            auto destroy_inst = reinterpret_cast<FnDestroyInst>(reinterpret_cast<void*>(get_proc(inst, "vkDestroyInstance")));
            auto enum_devs = reinterpret_cast<FnEnumDevs>(reinterpret_cast<void*>(get_proc(inst, "vkEnumeratePhysicalDevices")));
            auto get_props = reinterpret_cast<FnGetProps>(reinterpret_cast<void*>(get_proc(inst, "vkGetPhysicalDeviceProperties")));
            auto get_qf = reinterpret_cast<FnGetQF>(reinterpret_cast<void*>(get_proc(inst, "vkGetPhysicalDeviceQueueFamilyProperties")));
            auto get_mem = reinterpret_cast<FnGetMem>(reinterpret_cast<void*>(get_proc(inst, "vkGetPhysicalDeviceMemoryProperties")));
            uint32_t ndev = 0;
            if (enum_devs && get_props && enum_devs(inst, &ndev, nullptr) == 0 && ndev > 0 && ndev <= 32) {
                std::vector<void*> devs(ndev, nullptr);
                if (enum_devs(inst, &ndev, devs.data()) == 0) {
                    int best_score = -1;
                    for (uint32_t i = 0; i < ndev; ++i) {
                        VkPropsPrefix pr{};
                        get_props(devs[i], &pr);
                        if (pr.deviceType == 4u || pr.deviceType == 0u) continue; // skip CPU / OTHER
                        if (get_qf) {
                            uint32_t nqf = 0;
                            get_qf(devs[i], &nqf, nullptr);
                            if (nqf == 0 || nqf > 64) continue;
                            std::vector<VkQueueFamMin> qfs(nqf);
                            get_qf(devs[i], &nqf, qfs.data());
                            bool has_compute = false;
                            for (uint32_t q = 0; q < nqf; ++q) {
                                if (qfs[q].queueFlags & 0x2u /*VK_QUEUE_COMPUTE_BIT*/) { has_compute = true; break; }
                            }
                            if (!has_compute) continue;
                        }
                        uint64_t vram_mb = 0;
                        if (get_mem) {
                            VkMemPropsMin mp{};
                            get_mem(devs[i], &mp);
                            for (uint32_t h = 0; h < std::min<uint32_t>(mp.memoryHeapCount, 16u); ++h) {
                                if (mp.memoryHeaps[h].flags & 0x1u /*VK_MEMORY_HEAP_DEVICE_LOCAL_BIT*/) {
                                    vram_mb = std::max<uint64_t>(vram_mb, mp.memoryHeaps[h].size >> 20);
                                }
                            }
                        }
                        const bool disc = (pr.deviceType == 2u);
                        const bool weak_igpu = should_default_disable_igpu(disc, pr.vendorID, pr.deviceID, pr.deviceName);
                        // Desktop discrete GPUs strictly outrank all integrated GPUs regardless of BIOS display order
                        const int score = disc
                            ? (1000000 + int(std::min<uint64_t>(vram_mb, 500000ULL)))
                            : ((pr.deviceType == 1u)
                                ? ((weak_igpu ? 1000 : 20000) + int(std::min<uint64_t>(vram_mb, 8000ULL)))
                                : 500);
                        if (score > best_score) {
                            best_score = score;
                            out.has_gpu = true;
                            out.is_discrete = disc;
                            out.default_disable_gpu = weak_igpu;
                            out.vendor_id = pr.vendorID;
                            out.device_id = pr.deviceID;
                            std::snprintf(out.device_name, sizeof(out.device_name), "%s", pr.deviceName);
                        }
                    }
                }
            }
            if (destroy_inst) destroy_inst(inst, nullptr);
        }
    }
    FreeLibrary(vk_lib);
    if (!out.has_gpu) out.default_disable_gpu = true;
    return out;
}
#endif

inline void enable_x86_ftz_daz() {
#if HERAW_ARCH_X86
    _mm_setcsr(_mm_getcsr() | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#endif
}

inline void apply_process_priority(TaskPriority prio) {
    enable_x86_ftz_daz();
#if defined(_WIN32)
    DWORD cls = NORMAL_PRIORITY_CLASS;
    if (prio == TaskPriority::HIGH) cls = ABOVE_NORMAL_PRIORITY_CLASS;
    else if (prio == TaskPriority::BELOW_NORMAL) cls = BELOW_NORMAL_PRIORITY_CLASS;
    else if (prio == TaskPriority::IDLE_ECO) cls = IDLE_PRIORITY_CLASS;
    SetPriorityClass(GetCurrentProcess(), cls);
#elif defined(__APPLE__) || defined(__linux__)
    int niceness = 0;
    if (prio == TaskPriority::HIGH) niceness = -4;
    else if (prio == TaskPriority::BELOW_NORMAL) niceness = 8;
    else if (prio == TaskPriority::IDLE_ECO) niceness = 19;
    if (niceness != 0) (void)setpriority(PRIO_PROCESS, 0, niceness);
#endif
}

inline void apply_worker_thread_policy(
    const LogicalCpu* target_cpu, TaskPriority prio, CpuCorePolicy core_pol
) {
    enable_x86_ftz_daz();
#if defined(_WIN32)
    (void)core_pol;
    HANDLE ht = GetCurrentThread();
    if (target_cpu) {
        GROUP_AFFINITY ga{};
        ga.Group = target_cpu->group;
        ga.Mask = (KAFFINITY(1) << target_cpu->group_bit);
        SetThreadGroupAffinity(ht, &ga, nullptr);
    }
    int tprio = THREAD_PRIORITY_NORMAL;
    if (prio == TaskPriority::HIGH) tprio = THREAD_PRIORITY_ABOVE_NORMAL;
    else if (prio == TaskPriority::BELOW_NORMAL) tprio = THREAD_PRIORITY_BELOW_NORMAL;
    else if (prio == TaskPriority::IDLE_ECO) tprio = THREAD_PRIORITY_LOWEST;
    SetThreadPriority(ht, tprio);

    // Windows 11 EcoQoS / Power Throttling control: explicitly unthrottle compute workers in
    // NORMAL/HIGH (prevents inherited background E-core caging!) and enable EcoQoS in IDLE_ECO.
    struct LocalPowerThrottlingState {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };
    using SetThreadInfoFn = BOOL(WINAPI*)(HANDLE, int, PVOID, DWORD);
    static SetThreadInfoFn fn_set_info = reinterpret_cast<SetThreadInfoFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation")));
    if (fn_set_info) {
        constexpr ULONG kExecSpeed = 0x1u;
        LocalPowerThrottlingState pts{};
        pts.Version = 1;
        pts.ControlMask = kExecSpeed;
        pts.StateMask = (prio == TaskPriority::IDLE_ECO) ? kExecSpeed : 0u;
        fn_set_info(ht, 4 /*ThreadPowerThrottling*/, &pts, sizeof(pts));
    }
#elif defined(__APPLE__)
    (void)target_cpu;
    qos_class_t qc = QOS_CLASS_USER_INITIATED;
    int rel = 0;
    if (core_pol == CpuCorePolicy::EFF_ONLY || prio == TaskPriority::IDLE_ECO) {
        qc = QOS_CLASS_BACKGROUND;
    } else if (prio == TaskPriority::BELOW_NORMAL) {
        qc = QOS_CLASS_UTILITY;
    } else if (prio == TaskPriority::HIGH) {
        qc = QOS_CLASS_USER_INITIATED;
    } else {
        qc = QOS_CLASS_USER_INITIATED;
        rel = -2;
    }
    pthread_set_qos_class_self_np(qc, rel);
#elif defined(__linux__)
    (void)prio;
    (void)core_pol;
    if (target_cpu && target_cpu->os_cpu_id < CPU_SETSIZE) {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(int(target_cpu->os_cpu_id), &cs);
        pthread_setaffinity_np(pthread_self(), sizeof(cs), &cs);
    }
#endif
}

} // namespace heraw_cache
