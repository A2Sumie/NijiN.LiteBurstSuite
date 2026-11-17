# Performance Budgets, Cache-Adaptive Tiling & Full-Stage Benchmark Measurements

**[English](#performance-budgets-cache-adaptive-tiling--full-stage-benchmark-measurements) · [简体中文](#性能预算自适应缓存层次与全阶段重构实测)**

Platforms:
- **Apple M1 Max** (`ARM64 NEON` + `Metal`, `8P+2E`, `128 KB` L1D, `12 MB` P-cluster L2, `24 MB` SLC)
- **AMD Ryzen 7 5800X3D (`X570`)** (`x86_64 AVX2/FMA` + `RTX 4080 SUPER Vulkan`, homogeneous `8C/16T` Zen 3, `32 KB` L1D, `512 KB` L2/core, **`96 MB` 3D V-Cache L3**)
- **AMD Ryzen AI 9 HX 365 (`16Plus`, Strix Point)** (`x86_64 AVX-512 + AVX2/FMA` + `Radeon 880M Vulkan` iGPU, asymmetric dual-CCX **`4×Zen 5` (`16 MB` L3 CCX0) + `6×Zen 5c` (`8 MB` L3 CCX1)**, `48 KB` L1D, `1 MB` L2/core, mobile PPT envelope & real-time AC/DC power awareness)

Main benchmarks use real **5–6 frame** full-resolution RAW bursts with all Photo stages active (`C10` row flow, `C13` silicon residual cleanup, `C3` directional lucky weights, `Stage 4` anisotropic fusion, `Stage 5` GAT-DCT Wiener + 6-level À-Trous chroma + PSF RL, `C1` compact IBP, `C6` KLT/nonlocal shrinkage, `C2` color-line refinement, `Stage 6` 2D-tiled RGB RL + dual lossless RGGB DNG + 16-bit Deflate TIFF writes).

## Reproducible Inputs

- **Nikon Z8** (45.7 MP Bayer): first 5/6 frames of `Z8S_8636–8641.NEF`; output **16560×11040 (182.8 MP)**.
- **Sony A9 III** (24.6 MP Bayer): `DSC00889, 00894, 00895, 00897, 00896.ARW`; output **12024×8040 (96.7 MP)**.
- **Fujifilm X-H2S** (26.1 MP X-Trans): `DSCF0349–0353.RAF`; output **12528×8352 (104.6 MP)**.

```sh
nijinLBS --backend cpu --cpu-cores auto --preset photo --policy force-all \
  --anchor-frame first --no-sort-time --dither off \
  --out-raw result.dng --out-rgb result.tif INPUTS...
```

---

## 1. Adaptive Cache Topology, Heterogeneous Core Affinity & Power Awareness (`heraw_cpu_cache.hpp`)

Probes Windows `GetLogicalProcessorInformationEx(RelationAll)` + `GetSystemPowerStatus` (detecting multi-CCD Zen 3/4/5, single-CCD/asymmetric 96 MB X3D, **AMD Strix Point asymmetric dual-CCX `Zen 5 + Zen 5c`**, **Intel 2-tier `P+E` and 3-tier `P+E+LP-E` low-power islands without L3**, plus live AC/DC battery state), Linux `sysfs` + CPUID (`0x8000001D` / `0x04` / `0x07` + `XGETBV` + `/sys/class/power_supply`), and macOS `sysctlbyname` (`hw.perflevel0/1.*`) to set tile geometry, thread affinity, and QoS automatically:

| Microarchitecture Profile | L1D / Core | Private L2 / Core | Shared L3 / Cluster | `s4_xt` (Stage 4 Tile) | `dct_xt` (DCT Tile) | `ibp_tile` (C1 Tile) | Luma RL 2D Tile |
|---|---:|---:|---:|---:|---:|---:|---:|
| **AMD Zen 3 (5600X–5950X)** | 32 KB | 512 KB | 32 MB | `128` | `256` | `64` | Thread-private `256×512` |
| **AMD Zen 3/4/5 X3D (5800X3D / 7800X3D / 9800X3D)** | 32–48 KB | 512 KB–1 MB | **96 MB** | `128` / `192` | `256` / `512` | `64` / `96` | Thread-private `256×512` + V-Cache resident |
| **AMD Strix Point (Ryzen AI 9 HX 365/370, Zen 5 + Zen 5c)** | 48 KB | 1 MB | **16 MB + 8 MB** | `192` | `512` | `96` | Fits `1 MB` L2/core (`897 KB`/plane) |
| **AMD Zen 4 / Zen 5 Desktop** | 32–48 KB | 1 MB | 32 MB | `192` | `512` | `96` | Fits `1 MB` private L2/core |
| **Intel Hybrid (`P+E` / `P+E+LP-E`)** | 48 KB | 1.25–2.5 MB | 18–36 MB | `192`–`256` | `512` | `96` | Excludes L3-less `LP-E` cores by default |
| **Apple Silicon (M1–M4 Pro/Max)** | 128–192 KB | P-cluster shared L2 | 24–48 MB SLC | `256` | `512` | `96` | P-cluster L2 / SLC resident |

---

## 2. End-to-End Benchmark Summary (2026-11-17)

### 2.1 Pure CPU Mode (`--backend cpu`) Wall Time & Parity

| Sample / Platform | 2.0.1 Initial | Pre-Refactor | **Wave-2 Refactored** | **Speedup vs 2.0.1** | **Speedup vs Pre** | TIFF Output Parity |
|---|---:|---:|---:|---:|---:|---:|
| **Z8 6F — Ryzen AI 9 HX 365 (`auto` 14T, AC)** | — | — | **17.77 s (AVX2) / 17.82 s (AVX-512)** | — | — | Byte-identical DNG, TIFF `RMSE=0` |
| **Z8 6F — Ryzen AI 9 HX 365 (`all` 20T / `physical` 10T / `perf` 8T)** | — | — | **19.75 / 20.85 / 27.40 s** | — | — | 548M 16-bit samples `max_diff=0` |
| **Z8 6F — Ryzen 7 5800X3D (16T AVX2)** | — | 75.49 s | **20.57 s** | — | **3.67× (-72.8%)** | Within `0.00003%` vs Vulkan |
| **Z8 6F — Ryzen 7 5800X3D (8 phys / 6T AVX2)** | — | 110.82 s (6T) | **25.01 s (8T) / 29.10 s (6T)** | — | **3.81× (6T, -73.7%)** | Sample mean diff `< 0.04%` |
| **Z8 6F — Apple M1 Max (10T / 6T NEON)** | 92.15 s (6T) | 51.15 s (6T) | **22.42 s (10T) / 29.20 s (6T)** | **3.16× (6T, -68.3%)** | **1.75× (6T, -42.9%)** | **103.06 dB** vs Metal (`RMSE 0.461`) |
| **Z8 5F — Apple M1 Max (6T NEON)** | 82.59 s | 46.19 s | **25.59 s** | **3.23× (-69.0%)** | **1.81× (-44.6%)** | **94.96 dB** (`RMSE 1.17`) |
| **X-H2S 5F — Apple M1 Max (6T NEON, X-Trans)** | 59.18 s | 31.94 s | **16.55 s** | **3.58× (-72.0%)** | **1.93× (-48.2%)** | **90.50 dB** (`RMSE 1.96`) |
| **A9 III 5F — Apple M1 Max (6T NEON)** | 31.51 s | 20.09 s | **11.13 s** | **2.83× (-64.7%)** | **1.81× (-44.6%)** | **89.30 dB** (`RSS -850 MB`) |

> On M1 Max `Z8 6F`, retired CPU instructions fell from `3108.55 B` to **`1635.15 B` (-47.4%)**; CPU vs Metal TIFF parity reaches **`103.06 dB` (`RMSE 0.461 / 65535`)**; `snr_gated_rl_bands` 2D luma tiling matches full-frame single-block reference with **`0.0`** max float difference.

### 2.2 GPU Hybrid Pipeline (`--backend gpu`, Sharing CPU Stage 1–3 / C1 / C6 / C2 / Stage 6)

| Sample / Platform | 2.0.1 Initial | Pre-Refactor | **Wave-2 Refactored** | **Total Speedup** |
|---|---:|---:|---:|---:|
| **Z8 6F — Ryzen 7 5800X3D + RTX 4080 SUPER (16T)** | 47.50 s | 30.32 s | **16.97 s** | **2.80× (-64.3%)** |
| **Z8 6F — Apple M1 Max Metal (6T)** | 43.37 s | 25.98 s | **18.72 s** | **2.32× (-56.8%)** |
| **Z8 6F — Ryzen AI 9 HX 365 + Radeon 880M Vulkan iGPU (14T)** | — | — | **21.29 s** | Under shared mobile PPT, pure CPU `auto` (**17.82 s**) outperforms iGPU tiling |

### 2.3 Strix Point (`Ryzen AI 9 HX 365`) Core & Power Matrix (`Z8 6F -> 182.8 MP`)

| Mode (CLI / GUI Menu) | Thread & Core Policy | Stage 3 + `C13` | Stage 4/5 | `Stage 5 RL` | `C1` IBP + `C6/C2` | `Stage 6 RGB RL` | **End-to-End** |
|---|---|---:|---:|---:|---:|---:|---:|
| **`cpu` + `auto` (Heterogeneous 14T, recommended)** | `4×2T` Zen 5 + `6×1T` Zen 5c (protects 8 MB CCX1 L3 & PPT boost) | **3.25 s** | **6.09 s** | **1.66 s** | **2.65 s** | **0.90 s** | **17.82 s (AVX2 17.77 s)** |
| **`cpu` + `all` (20T)** | All 20 logical SMT threads | 3.51 s | 7.06 s | 1.86 s | 2.74 s | 1.06 s | **19.75 s** |
| **`cpu` + `physical` (10T)** | 1 thread per physical core (`4×Zen 5 + 6×Zen 5c`) | 3.96 s | 6.96 s | **1.29 s** | 3.25 s | **0.69 s** | **20.85 s** |
| **`cpu` + `auto` (DC battery auto-10T)** | Battery probe automatically disables SMT (10T physical) | 3.96 s | 6.97 s | 1.35 s | 3.26 s | **0.69 s** | **20.87 s** |
| **`gpu` + `auto` (Radeon 880M Vulkan + 14T)** | iGPU Stage 4/5 + 14T CPU | 3.50 s | 8.25 s | 1.83 s | 2.95 s | 0.96 s | **21.29 s** |
| **`cpu` + `eff` + `idle` (Zen 5c E-only 12T)** | `6×Zen 5c` 12T + Win11 EcoQoS (leaves P-cluster idle) | 5.11 s | 9.41 s | 2.10 s | 3.98 s | 1.19 s | **26.32 s** |
| **`cpu` + `perf` (Zen 5 P-only 8T)** | `4×Zen 5` 8T | 5.33 s | 9.79 s | 1.75 s | 4.47 s | 0.94 s | **27.40 s** |

---

## 3. Stage-by-Stage Wall Cost Comparison (`Z8 6F -> 182.8 MP`)

| Pipeline Stage | 5800X3D 6T (Pre → Post) | 5800X3D 16T (Pre → Post) | HX 365 14T | M1 Max 6T / 10T | Key Optimizations |
|---|---:|---:|---:|---:|---|
| **Stage 1/2 Dark & Noise Cal** | 1.01 → **0.70 s** | 1.04 → **0.80 s** | **0.59 s** | **0.45 s** | Parallel bias estimation, Bayer `g_halfs`, deterministic row-sharded 12-bin `hh_bins` |
| **Stage 3 Pyramid & Patch Align** | 5.54 → **3.12 s** | 3.74 → **2.42 s** | **1.80 s** | **2.63 / 2.24 s** | Flattened 2D block search (`35/54/88`), 1-row `recover_rigid_motion_patches`, reused `CurIntegral` (`-2.75 GB` clears) |
| **Stage 3 `C13` Silicon Cleanup** | 6.95 → **2.80 s** | 3.86 → **1.64 s** | **1.39 s** | **2.47 / 1.85 s** | Plane finite check, 4-wide SIMD `inverse_flow4`, direct Bayer sampling, hot-pixel early exit |
| **Stage 4/5 Backend (`backend-stage45`)** | 70.88 → **9.94 s (7.13×)** | 49.84 → **6.96 s (7.16×)** | **6.09 s** | **11.97 / 8.80 s** | Direct <=80 MP bypass + cross-stage physical page handoff (avoids Windows `WorkingSetMutex` contention) |
| ↳ `Stage 4 Green` fusion | 19.93 → **2.48 s** | 12.52 → **1.41 s** | **1.09 s** | **3.63 / 2.85 s** | `s4_xt` L2 columns, hoisted `HalfAxis`, **AVX2 dual-tap `weight_exp8_avx2`** |
| ↳ `Stage 4 R/B` chroma fusion | 20.68 → **3.30 s** | 12.60 → **2.08 s** | **1.62 s** | **3.08 / 2.37 s** | Pre-normalized `chroma_anchor`, hoisted bilinear Bayer `2×2` green weights |
| ↳ `Stage 5 GAT + DCT Wiener` | 9.57 → **2.12 s** | 7.47 → **1.66 s** | **1.24 s** | **2.16 / 1.74 s** | SIMD GAT fwd/inv + `4×4` residual variance reduction, reused `dct_pilot`, ring-buffer `edge_polish` |
| ↳ `Stage 5 À-Trous` chroma | 11.02 → **0.96 s (11.5×)** | 7.36 → **0.74 s (9.99×)** | **0.45 s** | **1.17 / 0.82 s** | Physical page reuse (`-549 MB` page faults) + L1/L2 `(H+V)` ring buffer + single-struct Win64 ABI |
| ↳ `Stage 5 PSF RL` | 3.25 → **1.04 s** | 3.41 → **1.03 s** | **1.66 s** | **1.89 / 0.99 s** | **2-barrier thread-private 2D `256×512` luma tiles** (eliminated 2,088 CCX barriers & `7 GB` RGB copies) |
| **Stage 5 `C1` Compact IBP** | 10.33 → **3.48 s** | 5.55 → **2.08 s** | **1.63–1.80 s** | **3.74 / 2.67 s** | 2-step bound pruning, span coalescing, `inverse_flow4`, FMA `4×4` tent accumulation |
| **Stage 5 `C6` KLT/NL + `C2` Color-Line** | 4.35 → **1.68 s** | 2.27 → **1.08 s** | **0.81–0.85 s** | **1.32 / 1.04 s** | Vectorized `transform4` & `klt_basis`, joint 3-ch forward DCT, 16-wide SIMD `top8` |
| **Stage 6 DNG `SOF3` Encode** | 0.93 → **0.32 s** | 0.63 → **0.28 s** | **0.20 s** | **0.20 / 0.15 s** | Branchless quantization, `PSV=7` predictor, `4 MB` stdio buffer |
| **Stage 6 RGB RL + Color + TIFF** | 7.14 → **4.77 s** | 5.33 → **3.13 s** | **3.37 s** (`rgb-rl` `0.90 s`) | **4.71 / 3.73 s** | Reused `lum0` pages in 2D RL (`5800X3D` `rgb-rl` `0.52 s`), in-place TIFF strip Deflate (`-430 MB` copy) |

---

## 4. Numerical Consistency & Platform Fixes

1. **Direct Global $\eta$ Prescan & Cody-Waite Exp Parity**: Unified 6th-order Cody-Waite `weight_exp` and `x < 12.0f` truncation mask across `compute_global_eta_direct` and tiled CPU/GPU kernels, dropping full-frame vs tiled Z8 difference to **`8.94e-8` (`0` $\eta$ delta)**.
2. **`match_zncc_block` Bounds Fix**: Corrected lower/right search clamp to `H - bh + 1` / `W - bw + 1`, reaching **`90.50 dB`** on `X-H2S` 5F and **`94.98 dB`** on `Z8` 6F.
3. **Win64 Environment Lock & ABI Spill Elimination**: Replaced per-pixel `std::getenv` with relaxed `std::atomic<uint8_t>` and packed `atrous_v_span_avx512` 21 arguments into a single `%rcx` struct pointer (`HX 365`: `À-Trous` `12.40 s → 0.45 s`, `C6` `32.70 s → 0.81 s`, `C1` `>150 s → 1.63 s`).
4. **Dual-CCX Barrier Storm & Windows `WorkingSetMutex` Fix**: Switched `snr_gated_rl_bands` to 2-barrier thread-private 2D luma tiles and passed committed `sc.z` / `sc.mr` / `sc.mb` physical pages across stages (`HX 365` RL `9.37 s → 2.56 s`, `0.0` float diff vs reference).
5. **x86 Policy Convergence**: Standardized production x86_64 builds on AVX2+FMA ([`ARCHIVE_AVX512.md`](ARCHIVE_AVX512.md)), enabled battery SMT-off detection (`HX 365` DC `10T` `19.98 s` vs AC `14T` `17.77 s`), prioritized discrete Vulkan GPUs over desktop iGPUs (`X570 RTX 4080 SUPER` `17.19 s`), and defaulted AMD APUs / pre-Xe2 Intel iGPUs (`nuc-8-sumie Iris Plus 655` `1.37 s`) to pure CPU with a one-time 10s amber breath cue.

---

# 性能预算、自适应缓存层次与全阶段重构实测

平台：
- **Apple M1 Max**（ARM64 NEON + Metal，`8P+2E`，`128 KB` L1D，`12 MB` P-cluster L2，`24 MB` SLC）
- **AMD Ryzen 7 5800X3D (`X570`)**（x86_64 AVX2/FMA + RTX 4080 SUPER Vulkan，同构 `8C/16T` Zen 3，`32 KB` L1D，`512 KB` L2/core，**`96 MB` 3D V-Cache L3**）
- **AMD Ryzen AI 9 HX 365 (`16Plus`, Strix Point)**（x86_64 **AVX-512 + AVX2/FMA** + Radeon 880M Vulkan iGPU，双 CCX 大小核异构 **`4×Zen 5` (`16 MB` L3 CCX0) + `6×Zen 5c` (`8 MB` L3 CCX1)**，`48 KB` L1D，`1 MB` L2/core，移动 PPT 功耗墙与实时 AC/DC 供电感知）

主验收采用真实 **5–6 帧**全尺寸 RAW 连拍，Photo 默认全阶段开启（`C10` 行快门流平滑、`C13` 硅残差校准、`C3` 方向幸运权重、`Stage 4` 各向异性亚像素融合、`Stage 5` GAT-DCT Wiener + 6 级 À-Trous 色差 + PSF RL、`C1` 紧凑 IBP、`C6` KLT/非局部收缩、`C2` 色线细化、`Stage 6` 2D 瓦片 RGB RL + 无损 RGGB DNG + 16-bit Deflate TIFF 双文件写出）。

## 可复现输入

- **Nikon Z8**（45.7 MP Bayer）：`Z8S_8636–8641.NEF` 前 5/6 张；输出 **16560×11040（182.8 MP）**。
- **Sony A9 III**（24.6 MP Bayer）：`DSC00889, 00894, 00895, 00897, 00896.ARW`；输出 **12024×8040（96.7 MP）**。
- **Fujifilm X-H2S**（26.1 MP X-Trans）：`DSCF0349–0353.RAF`；输出 **12528×8352（104.6 MP）**。

```sh
nijinLBS --backend cpu --cpu-cores auto --preset photo --policy force-all \
  --anchor-frame first --no-sort-time --dither off \
  --out-raw result.dng --out-rgb result.tif INPUTS...
```

---

## 1. 运行时自适应缓存层次、异构大小核与移动 PPT 供电调度（`heraw_cpu_cache.hpp`）

通过 Windows `GetLogicalProcessorInformationEx(RelationAll)` + `GetSystemPowerStatus`（识别多 CCD Zen 3/4/5、96 MB 单 CCD/异构 X3D、**AMD Strix Point 双 CCX 非对称 L3 `Zen 5 + Zen 5c`**、**Intel 2 层 `P+E` 与 3 层 `P+E+LP-E` 无 L3 低功耗岛**，并实时感知 AC 交流供电与 DC 电池 PPT 功耗墙）、Linux `sysfs` + CPUID（`0x8000001D` / `0x04` / `0x07` + `XGETBV` + `/sys/class/power_supply`）、macOS `sysctlbyname`（`hw.perflevel0/1.*`）实时探测缓存层次、核心能效层级、供电状态与 AVX-512 硬件能力，自动设定分块几何、工作线程亲和性（`SetThreadGroupAffinity` / `pthread_setaffinity_np`）与系统能效 QoS（Windows 11 `ThreadPowerThrottling` EcoQoS 解限/省电、macOS `pthread_set_qos_class_self_np`）：

| 微架构画像 | L1D / 核 | 独享 L2 / 核 | 共享 L3 / 簇 | `s4_xt`（Stage 4 列块） | `dct_xt`（DCT 列块） | `ibp_tile`（C1 瓦片） | 亮度 RL 2D 瓦片 |
|---|---:|---:|---:|---:|---:|---:|---:|
| **AMD Zen 3（5600X–5950X）** | 32 KB | 512 KB | 32 MB | `128` | `256` | `64` | 线程私有 `256×512` |
| **AMD Zen 3/4/5 X3D（5800X3D / 7800X3D / 9800X3D）** | 32–48 KB | 512 KB–1 MB | **96 MB** | `128` / `192` | `256` / `512` | `64` / `96` | 线程私有 `256×512` + V-Cache 驻留 |
| **AMD Strix Point（Ryzen AI 9 HX 365/370，Zen 5 + Zen 5c）** | 48 KB | 1 MB | **16 MB + 8 MB** | `192` | `512` | `96` | 驻留每核 `1 MB` 独享 L2（`897 KB`/面） |
| **AMD Zen 4 / Zen 5 桌面** | 32–48 KB | 1 MB | 32 MB | `192` | `512` | `96` | 驻留每核 `1 MB` 独享 L2 |
| **Intel 大小核 / 大小小核（12th–14th / Core Ultra P+E+LP-E）** | 48 KB | 1.25–2.5 MB | 18–36 MB | `192`–`256` | `512` | `96` | 默认排除无 L3 的 `LP-E` 核 |
| **Apple Silicon（M1–M4 Pro/Max）** | 128–192 KB | P 簇共享 L2 | 24–48 MB SLC | `256` | `512` | `96` | 驻留 P 簇 L2 / SLC |

---

## 2. 端到端实测总览（2026-11-17）

### 2.1 纯 CPU 模式（`--backend cpu`）端到端总耗时与精度

| 样本 / 平台 | 2.0.1 初版 | 重构前基线 | **全阶段重构后（Wave-2）** | **相对 2.0.1 加速** | **相对重构前加速** | 输出 TIFF 对照精度 |
|---|---:|---:|---:|---:|---:|---:|
| **Z8 6F — Ryzen AI 9 HX 365（`auto` 14T，移动双 CCX AC）** | — | — | **17.77 s（AVX2）/ 17.82 s（AVX-512）** | — | — | AVX-512 vs AVX2 **DNG 字节级全等、TIFF `RMSE=0`** |
| **Z8 6F — Ryzen AI 9 HX 365（`all` 20T / `physical` 10T / `perf` 8T）** | — | — | **19.75 / 20.85 / 27.40 s** | — | — | 5.48 亿 16-bit 样本 `max_diff=0` |
| **Z8 6F — Ryzen 7 5800X3D（16 线程 AVX2）** | — | 75.49 s | **20.57 s** | — | **3.67×（-72.8%）** | 与 Vulkan GPU 差 `0.00003%` |
| **Z8 6F — Ryzen 7 5800X3D（8 物理核 / 6 线程 AVX2）** | — | 110.82 s（6T） | **25.01 s（8T）/ 29.10 s（6T）** | — | **3.81×（6T，-73.7%）** | 样本均值差 `< 0.04%` |
| **Z8 6F — Apple M1 Max（10 线程全核 / 6 线程 NEON）** | 92.15 s（6T） | 51.15 s（6T） | **22.42 s（10T）/ 29.20 s（6T）** | **3.16×（6T，-68.3%）** | **1.75×（6T，-42.9%）** | 与 Metal GPU 对照 **103.06 dB**（RMSE `0.461`） |
| **Z8 5F — Apple M1 Max（6 线程 NEON）** | 82.59 s | 46.19 s | **25.59 s** | **3.23×（-69.0%）** | **1.81×（-44.6%）** | **94.96 dB**（RMSE `1.17`） |
| **X-H2S 5F — Apple M1 Max（6 线程 NEON，X-Trans）** | 59.18 s | 31.94 s | **16.55 s** | **3.58×（-72.0%）** | **1.93×（-48.2%）** | **90.50 dB**（RMSE `1.96`） |
| **A9 III 5F — Apple M1 Max（6 线程 NEON）** | 31.51 s | 20.09 s | **11.13 s** | **2.83×（-64.7%）** | **1.81×（-44.6%）** | **89.30 dB**（RSS `-850 MB`） |

> M1 Max `Z8 6F` 整机退役指令数由 `3108.55 B` 降至 **`1635.15 B`（削减 47.4% CPU 指令量）**；同一作业 CPU 与 Metal GPU 成片 TIFF 对照达 **`103.06 dB`（RMSE `0.461 / 65535`，半个 16-bit 码值以内）**；`snr_gated_rl_bands` 2D 亮度瓦片与全图单块参考逐元素最大浮点差为 **`0.0`（位级全等）**。

### 2.2 GPU 混合管线同步收益（`--backend gpu`，共享 Stage 1–3 / C1 / C6 / C2 / Stage 6 CPU 加速）

| 样本 / 平台 | 2.0.1 初版 | 重构前基线 | **全阶段重构后（Wave-2）** | **总提速幅度** |
|---|---:|---:|---:|---:|
| **Z8 6F — Ryzen 7 5800X3D + RTX 4080 SUPER（16 线程）** | 47.50 s | 30.32 s | **16.97 s** | **2.80×（-64.3%）** |
| **Z8 6F — Apple M1 Max Metal（6 线程）** | 43.37 s | 25.98 s | **18.72 s** | **2.32×（-56.8%）** |
| **Z8 6F — Ryzen AI 9 HX 365 + Radeon 880M Vulkan iGPU（14 线程）** | — | — | **21.29 s** | 移动共享功耗墙下纯 CPU `auto`（**17.82 s**）免切块反超核显 |

### 2.3 Strix Point（`AMD Ryzen AI 9 HX 365`）异构核分配、功耗墙与供电感知实测矩阵（Z8 6F → 182.8 MP）

在 `16Plus`（高功耗墙模式，`4×Zen 5` `CCX0 16 MB` + `6×Zen 5c` `CCX1 8 MB` + `Radeon 880M`，全程 0.25 s 间隔 `GetSystemPowerStatus` 校验 AC 纯净供电）上对全部核分配档位实测如下：

| 模式（CLI / GUI 性能选单） | 线程与绑核策略 | Stage 3 配准+`C13` | Stage 4/5 后端 | `Stage 5 RL` | `C1` IBP + `C6/C2` | `Stage 6 RGB RL` | **端到端总耗时** |
|---|---|---:|---:|---:|---:|---:|---:|
| **`cpu` + `auto`（智能异构 14T，默认推荐）** | `4×2T` 大核 + `6×1T` 小核（保护 `CCX1` 8 MB L3 与移动 PPT 睿频） | **3.25 s** | **6.09 s** | **1.66 s** | **2.65 s** | **0.90 s** | **17.82 s（AVX2 17.77 s）** |
| **`cpu` + `all`（全部逻辑线程 20T）** | 10 核全 SMT 20T（受整包 PPT 功耗墙与 `CCX1` SMT 分摊限制） | 3.51 s | 7.06 s | 1.86 s | 2.74 s | 1.06 s | **19.75 s** |
| **`cpu` + `physical`（仅物理核 10T）** | 每物理核 1T 共 10T（`4×Zen 5 + 6×Zen 5c`） | 3.96 s | 6.96 s | **1.29 s** | 3.25 s | **0.69 s** | **20.85 s** |
| **`cpu` + `auto`（DC 电池供电自适应 10T）** | 检测到电池供电时自动裁除 SMT 跑 10T 物理核保能效比 | 3.96 s | 6.97 s | 1.35 s | 3.26 s | **0.69 s** | **20.87 s** |
| **`gpu` + `auto`（Radeon 880M Vulkan + 14T）** | 核显分块执行 Stage 4/5 + 14T CPU 执行其余阶段 | 3.50 s | 8.25 s | 1.83 s | 2.95 s | 0.96 s | **21.29 s** |
| **`cpu` + `eff` + `idle`（仅小核后台省电 12T）** | 仅 `CCX1` `6×Zen 5c` 12T + Win11 EcoQoS，让出全部 4 大核 | 5.11 s | 9.41 s | 2.10 s | 3.98 s | 1.19 s | **26.32 s** |
| **`cpu` + `perf`（仅大核 8T + 高优）** | 仅 `CCX0` `4×Zen 5` 8T（留空 6 小核） | 5.33 s | 9.79 s | 1.75 s | 4.47 s | 0.94 s | **27.40 s** |

---

## 3. 全阶段（Stage 0–6）逐项重构与实测阶段耗时对照

以 **Nikon Z8 六帧（45.7 MP × 6 → 182.8 MP `16560×11040`）** 为基准，对比 **5800X3D**（6T / 16T AVX2）、**HX 365**（`auto` 14T）与 **M1 Max**（6T / 10T NEON）每一阶段的实测 `[Cost]` 与 `[BackendCost]`：

| 管线阶段 | 5800X3D 6T（前 → 后） | 5800X3D 16T（前 → 后） | HX 365 14T（Wave-2） | M1 Max 6T / 10T（后） | 核心重构手段 |
|---|---:|---:|---:|---:|---|
| **Stage 1/2 标定与盲噪声** | 1.01 → **0.70 s**（1.44×） | 1.04 → **0.80 s** | **0.59 s** | **0.45 s** | 并行暗电平估计、Bayer `g_halfs` 专化、确定性行分片并行 12 桶 `hh_bins` 统计 |
| **Stage 3 金字塔与局部块配准** | 5.54 → **3.12 s**（1.78×） | 3.74 → **2.42 s**（1.55×） | **1.80 s** | **2.63 / 2.24 s** | 2D 展平金字塔块搜索（`35/54/88` 块）、单行动态均衡 `recover_rigid_motion_patches`、`CurIntegral` 仅首行清零并跨阶段复用（省 `2.75 GB` 扫描）、并行 `td_bins` |
| **Stage 3 `C13` 硅残差校准** | 6.95 → **2.80 s**（2.48×） | 3.86 → **1.64 s**（2.35×） | **1.39 s** | **2.47 / 1.85 s** | 平面级有限性预检、4 路向量逆光流 `inverse_flow4`、Bayer 棋盘直采、分条列中位数与热像素早退 |
| **Stage 4/5 后端总计（`backend-stage45`）** | 70.88 → **9.94 s（7.13×）** | 49.84 → **6.96 s（7.16×）** | **6.09 s** | **11.97 / 8.80 s** | `<=80 MP` 免外层 2D 切块直通 + 跨阶段物理页移交（消除 Windows `WorkingSetMutex` 缺页串行化） |
| ↳ `Stage 4 Green` 各向异性融合 | 19.93 → **2.48 s（8.02×）** | 12.52 → **1.41 s（8.86×）** | **1.09 s** | **3.63 / 2.85 s** | `s4_xt` L2 列块、`HalfAxis` 坐标外提、**AVX2 256-bit 双抽头联合 `weight_exp8_avx2`**（8 亚像素同算） |
| ↳ `Stage 4 R/B` 引导色差融合 | 20.68 → **3.30 s（6.26×）** | 12.60 → **2.08 s（6.05×）** | **1.62 s** | **3.08 / 2.37 s** | 预归一化 `chroma_anchor`、**Bayer `2×2` 绿引导双线性权重外提共享**（+4 整数偏移直采） |
| ↳ `Stage 5 GAT + 多尺度 DCT Wiener` | 9.57 → **2.12 s（4.52×）** | 7.47 → **1.66 s（4.50×）** | **1.24 s** | **2.16 / 1.74 s** | SIMD GAT 正逆变换 + `4×4` 残差方差 SIMD 归约、跨尺度复用 `dct_pilot`、并行残差合成、环形缓冲融合 `edge_polish` |
| ↳ `Stage 5 À-Trous` 6 级色差滤波 | 11.02 → **0.96 s（11.5×）** | 7.36 → **0.74 s（9.99×）** | **0.45 s** | **1.17 / 0.82 s** | 复用 `mr/mb` 已映射物理页（省 `549 MB` 缺页）+ 6 级 `(H+V)` 融合 L1/L2 环形缓冲 + 单指针 Win64 ABI |
| ↳ `Stage 5 PSF RL` 反卷积 | 3.25 → **1.04 s（3.12×）** | 3.41 → **1.03 s（3.31×）** | **1.66 s**（Wave-1 `6.44 s`） | **1.89 / 0.99 s** | **2 屏障线程私有 2D 亮度瓦片**（`256×512` 核驻留 `1 MB` L2，消除 `2,088` 次跨 CCX 屏障与 `7 GB` RGB 拷贝）+ `sc.z` 零缺页移交 |
| **Stage 5 `C1` 紧凑 IBP** | 10.33 → **3.48 s（2.97×）** | 5.55 → **2.08 s（2.67×）** | **1.63–1.80 s** | **3.74 / 2.67 s** | 2 步不动点区间剪枝 + 连续块合并 + `inverse_flow4` + 内联 FMA `4×4` tent 累加 + 线程局部统计归约 |
| **Stage 5 `C6` KLT/NL + `C2` 色线** | 4.35 → **1.68 s（2.59×）** | 2.27 → **1.08 s（2.09×）** | **0.81–0.85 s** | **1.32 / 1.04 s** | 向量化 `transform4`/倒数 `klt_basis`、3 通道联合前向 DCT、16 维 SIMD `top8` 与 4 频点并行 `h8_4` |
| **Stage 6 DNG 无损 SOF3 编码** | 0.93 → **0.32 s（2.87×）** | 0.63 → **0.28 s（2.28×）** | **0.20 s** | **0.20 / 0.15 s** | 无分支量化、`int16_t` 预测差、内部瓦片 `memcpy` + PSV=7 无分支编码 + `4 MB` `setvbuf` 流缓冲 |
| **Stage 6 RGB RL + 色彩矩阵 + TIFF** | 7.14 → **4.77 s（1.50×）** | 5.33 → **3.13 s（1.70×）** | **3.37 s**（`rgb-rl` `0.90 s`） | **4.71 / 3.73 s** | 复用 Stage 5 `lum0` 物理页走 2D 瓦片 RL（`5800X3D` `rgb-rl` **`0.52 s`**）、AVX2 广播置换增益写回、线性行指针量化、TIFF 条带原位压缩（省 `430 MB` 拷贝） |

---

## 4. 关键数值一致性与平台陷阱修复记录

1. **全局 `η` 直接预扫与瓦片路径多项式统一**：`heraw_gpu_backend.hpp` 的 `compute_global_eta_direct` 与 Stage 4 CPU/GPU 融合核统一采用 6 阶 Cody-Waite 多项式 `weight_exp`，并补齐 `ep_h_row`/`ep_v_row` 的 `x < 12.0f` 截断掩码，真实 Z8 RAW 全图与分块最大差降至 **`8.94e-8`（`eta` 差 `0`）**。
2. **`match_zncc_block` 5 步 Lucas-Kanade 下/右边界钳位修正**：修正有效上界为 `H - bh + 1` 与 `W - bw + 1` 后，`X-H2S` 五帧重构前后全图 TIFF 对照达 **`90.50 dB`（RMSE `1.957 / 65535`）**，`Z8` 六帧达 **`94.98 dB`（RMSE `1.169 / 65535`）**。
3. **Win64 `use_avx512()` 全局环境锁风暴与跨目标 XMM 栈溢出消除**：将热循环 `std::getenv("NIJIN_SIMD")` 替换为单字节 `std::atomic<uint8_t>` relaxed 读取，并将 `atrous_v_span_avx512` 21 参数签名收敛为单结构体指针 `%rcx` 传参，`HX 365` 上 `À-Trous` 由 `12.40 s` 降至 **`0.45 s`（27.3×）**、`C6` 由 `32.70 s` 降至 **`0.81 s`（40.4×）**、`C1` 由 `>150 s` 降至 **`1.63 s`（>92×）**。
4. **双 CCX 异构 L3 屏障风暴与 Windows `WorkingSetMutex` 缺页串行化根治**：将 `snr_gated_rl_bands` 从 `87×24=2,088` 次全局屏障的 1D 分带改为 2 次屏障的线程私有 `256×512` 2D 亮度瓦片，并跨阶段移交 `sc.z -> rl_ws.lum0` 与 `sc.mr/mb -> atrous_global` 已提交物理页，`HX 365` `Stage 5 RL + Stage 6 rgb-rl` 由 `9.37 s` 降至 **`2.56 s`（3.66×）**，全图与单块参考差 **`0.0`**。
5. **x86 默认策略收敛（AVX-512 归档、电池设备默认关 SMT、APU / `< Xe2` 核显默认关闭与桌面独显保底）**：
   - 生产构建统一收敛至 `256-bit AVX2 + FMA`（`HX 365` 上 `17.77 s` 持平并略优于 `AVX-512` `17.82 s`），`AVX-512` 内核归档至编译宏 `HERAW_ENABLE_ARCHIVED_AVX512`（见 [`ARCHIVE_AVX512.md`](ARCHIVE_AVX512.md)）。
   - Windows/Linux 启动时检测并持久化 `BatteryDevice`（Win32 性能选单提供 `是电池设备 (默认关闭超线程 SMT)` 开关并记忆用户核分配选择）：`16Plus` 裸跑默认启用 `cores=physical`（`10T`）+ 自动旁路 `Radeon 880M`，Z8 六帧耗时 **`19.98 s`**（手动切 `auto 14T` 为 **`18.65 s`**）。
   - Vulkan 物理设备按 `DISCRETE_GPU (1,000,000 + VRAM_MB)` 严格高于 `INTEGRATED_GPU` 排序并导出 `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`；仅存在 AMD 核显或 Intel `< Xe2` 核显时默认切至纯 CPU（首次启动顶部 `性能` 按钮暖黄呼吸 10 秒，点击或二次启动即止），已在 `nuc-8-sumie`（`Core i5-8259U + Iris Plus 655`，自动转 CPU `8T` `1.37 s`）与 `X570`（`RTX 4080 SUPER` 独显保持启用 `17.19 s`；`NIJIN_CACHE_PROFILE=zen3` `32 MB L3` 等效 `5600X` 缓存拓扑下 `12T` **`22.24 s`**、`6T` **`30.48 s`**）实机验证。
