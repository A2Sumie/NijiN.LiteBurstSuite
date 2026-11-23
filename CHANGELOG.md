# Changelog

**[English](#changelog) · [简体中文](#更新日志)**

## 3.0.0-preview2 — Hero-Pick Sidecar Harvesting, `.NEV` True Bit-Depth DNG Scale, Periodic CFA/Brillouin Subspace, `--crop-box`, Vulkan `M5-A` Parity & Speed Optimization

- **Multi-Card Hero-Pick Harvesting & `.NEV` DNG Linear Scale Parity (`P0/P1`)**: Added `--from-picks [auto|dop|xmp]` hero-frame sidecar harvesting across multi-card same-stem shoots (`cardA/DSC_0001` vs `cardB/DSC_0001` vs `cardC/DSC_0001`) with DCF rollover grouping (`145` -> `146`), strict camera-serial isolation (`ExifIFD:SerialNumber` / `IFD0:CameraSerialNumber`, zero cross-body frame mixing), negative `.xmp` rating veto over positive `.dop` picks, and collision-free per-card stem preservation across `--export-topk` and fusion; fixed Nikon `.NEV` TicoRAW DNG export (`nijinWVDC` & `nijinLBS --export-topk`) to derive true sensor bit depth (`14-bit`, `WhiteLevel = 16383`, `BlackLevel = 1008`) instead of container `16-bit` `65535` (`23.85%` -> `99.48%–99.68%` re-import linear parity).
- **EXIF-Oriented `--crop-box` Exact ROI Trim & Out-of-Hull `.nlens` Isolation**: Added display-oriented `--crop-box x,y,w,h` across all 8 EXIF orientations (`1..8` + Sony `ActiveArea` offsets) with exact margin-trimmed DNG (`DefaultCropSize` / `ActiveArea` `extra_px = 0`), TIFF (`1x`/`2x`/`both`), and Multi-Part OpenEXR parity; guaranteed bit-exact `off`-equivalent pixel behavior outside local `.nlens` 2D field calibration hulls (`0` out-of-hull RGB16 channel drift).
- **6-Tap Lagrange 2D Optical Peak Guard, Two-Short-View AEB Plateau Gate & Conditioned Censored Projection (`A / B+ / C1+C2`)**: Added a 6-tap 5th-degree central Lagrange leave-one-out sublattice predictor (`[1,-6,15,0,15,-6,1]/20`) in Stage 1 to protect constructive 2D interference maxima (`96.2%–98.7%` sum/difference preservation) without relaxing single-pixel hot-pixel cleanup; added a 2-independent-short-view Stage 3 AEB plateau evidence gate with raw-capture deduplication (`!same_raw_capture`) and companion-colour contradiction veto (`saw_colour && !contradicted`); conditioned AEB censored tent-footprint KKT projection (`denom >= 0.5 * full_norm` with `full_norm` step normalization) to eliminate `16×` corner overshoot spikes while preserving conservative lower-bound provenance (`bound_only`).
- **Stage 4C Brillouin Fine-Cell Gate, `M5-B` Achromatic Edge Clamp, Vulkan `M5-A` Parity & `2.38×` Full-Frame Speedup (`V1/V2`)**: Added `8×8` fine-cell spatial residual gating + single-mode `M=1` Brillouin subspace projection in Stage 4C, `3×3` collinear neutral edge clamping + $4\times 4$ Walsh-Hadamard spatial energy gating in `M5-B`, synchronized Vulkan `k_m5a_scatter` footprint span variance (`0.35 * (p_max - p_min)`) and `k_m5a_update` gated chroma slope (`vg_sig > max(6*nv, 1.5e-4)` & `cov < 0.45*vg_sig`) across all `49` embedded SPIR-V kernels (`RTX 4080 SUPER` Vulkan vs CPU `2×` EXR `RMS = 0.000215`, `0` pixels `> 0.10`), and accelerated full-frame Nikon Z8 8-frame Metal `2×` execution by `2.38×` (`95.29 s -> 40.09–41.75 s`).

## 3.0.0-preview-fix2 — Anti-Screen-Door 6-Tap Sampling, Chroma Fringe Guards, Complex-OTF Deterministic Sharpening, Open `.nlens` v1, Split-Half FRC Suitability & 17 RGB / 27 Field Formats

- **Resolution-Chart Anti-Screen-Door & Tangential-Stripe Suppression (`A1–A4`)**: Replaced single-frame Stage 4A fallback with a multi-frame `1×` 4-axis 8-directional CFA-aware RCD/Hamilton-Adams base (fixing GRBG/GBRG green parity), monotonic-clamped Keys cubic interpolation, `d < 9.0` $C^1$ Hermite anchor taper, subpixel oscillation slack, and 16-phase Bayer/X-Trans periodic completeness blending; moved `inv_cov` structure tensors onto full `1×` green with a 4-direction 5-tap monotonicity guard (`rho_nyq`) to suppress tangential stripes; replaced the 4-tap `M5-A` footprint (`±0.141 px` centroid jump) with a 6-tap $C^1$ non-negative moment-exact kernel (`1.45e-7 px` float32 centroid error on full interior support) and `1/N` Tobit deadzone scaling.
- **Highlight & Chromatic Fringe Elimination (`A5–A7`)**: Enabled unconditional non-AEB per-channel highlight saturation gates (`0.985`), bracketed short-exposure radiance ordering, concentric `(2r+0.5, 2c+0.5)` dual-saturation-tapered color-difference Stage 4B anchors, $2\times 2$ luminance-guided bilateral `1× -> 2×` chroma upsampling, `M5-A` white-balanced Cramér-Rao Schur variable transformation (`wb_r / wb_b`, eliminating un-WB `1:1:1` luminance/opponent-chroma coupling that caused tangential bar stripes and R/B/purple edge fringes) + highlight opponent-chroma freeze (`G > 0.80 * sat`), and radial-cubic + tangential LCA + per-frame OIS source-sensor shifts across Stage 4B, `Q4/Q5`, and `joint` `1×`.
- **Deterministic Information-Backed Sharpening & Open `.nlens` v1 (`A8–A13`)**: Removed the pure-`1×` `sinc` post-blur; unified `1×` and `2×` `M5-B` complex Wiener deconvolution with $C^1$ smooth frequency roll-off, per-tile `N_eff` / phase / 4-direction identifiability shelf gating across CPU, Metal, and Vulkan (`N=1` extra shelf strictly `0`), `M5-A` gain coupling, complex safety-disk projection, Sobolev curvature gating, 4-direction ridge/valley prominence anti-trough clamping, damped opponent-chroma transport, and `--relay-raw` parity; added vendor-neutral 3-tier `.nlens` v1 optical profiles (`--lens-profile auto|off|path.nlens`, `docs/NLENS_SPEC_AND_LLM_PROMPT.md`) with joint state `(focal, aperture, focus)` + 2D field convex-hull abstention (`off`-equivalent fallback outside calibrated hull), EXIF focal-plane pixel-pitch propagation, wide-skirt flare pre-subtraction, and transient 3-DOF focal-plane tilt correction.
- **Burst Suitability & Independent Leave-One-Out Metrology (`A14`)**: Added Modified Gram-Schmidt $4\times 4$ Brillouin-zone alias Schur completeness ($\Phi_L$), `4.763 MiB + <= 578 KiB` streaming 16-patch disjoint Even/Odd Split-Half `FRC` (with exact unequal `3+2` quadratic conversion and jackknife SE), conservative worst-axis passband $f_{\min}$ (`f_min/f_med/f_max`) & `MP_band`, independent `N - 1` leave-one-out `--eval-self` residual validation (`valid=false` for `N=1`), slanted-edge `eSFR MTF50`, nominal weight SNR gain (dB), overshoot %, R/B fringe (px), and 4-tier verdict (`INSUFFICIENT_EVIDENCE` / `RECOMMEND_1X` / `MARGINAL_2X` / `STRONG_2X`) across CLI `[Metrics]`, `*_metrics.json`, `--plan-json`, and native GUIs.
- **27 Physical Fields, All 8 EXIF Orientations & 17 Streaming RGB Formats (`A15–A16`)**: Added `--field-scale native|1x|2x` with full 8-orientation EXIF (`1..8`, including mirrored `2/4/5/7`) pixel, signed-vector (`normal_x/y`), and dual-scale (`--rgb-scale both`) sidecar affine parity, `--field-format auto|png|tiff|jxl|exr|webp|nib`, `--field-precision auto|u8|u16|f16|f32|log16|oct32` (`symmetric_z128` signed `u8`), `64×64` tile-callback streaming EXR writers, `--field-compression lossless|vis-lossless` with `F-A..F-D` companders, and expanded streaming `--rgb-format` to 17 practical formats (`tiff-deflate`, `tiff-hq`, `tiff-lzw`, `tiff-8`, `tiff-raw`, `psd`/auto-`psb`, `jxl`, `jxl-lossless`, `jp2`, `jp2-lossless`, `png`, `png-8`, `jpg` 4:4:4, `webp` VP8L, `exr`, `jxs`, `jxs-lossless`) across CLI and macOS/Windows GUIs.

## 3.0.0-preview-fix1 — Critical Vulkan Multi-Tile 2x Readback Fix

- Fixed a critical Vulkan `vkDestroyBuffer` access violation (`0xC0000005` / `SIGSEGV`) during multi-tile Stage 4/5 `2x` GPU execution when `core_y0 > 0` ([#9](https://github.com/A2Sumie/NijiN.LiteBurstSuite/issues/9)) by converting `VulkanEngine::GpuBuf` into a move-only RAII handle and eliminating raw-pointer `Cleanup` lists across `heraw_gpu_vulkan.hpp`, plus adding `vk.mtx` locking in `run_stage45_gpu_full` and ASan `stack-use-after-scope` + vertical multi-tile `2x` regression gates.

## 3.0.0-preview — Unified V3, nijinWVDC (.NEV / HE / ARW6 / .DXO), Handheld Optics (OP-O1..O3), NOPB/1 Bus & O1–O10 Observers

- Added Unified V3 (`M0–M5`) streaming reconstruction across CPU SIMD (`NEON`/`AVX2`), Apple `Metal`, and `Vulkan 1.0` (`49` SPIR-V kernels): 4-point Gauss-Legendre 16 KB `NLT-VST`, `64×64` COLA 2D FFT 4-Octave `SURE`, Morozov-gated `M5-A` Schur GLS + `3×3` Color-Jacobian $\sigma_2$ pull, and `M5-B` R2C FFT deconvolution (`--ibp-iters`, `--olpf-mode`), plus zero-temp-disk 2-pass compressed stripe streaming (`--mem-budget-gb`).
- Integrated static `nijinWVDC` RAW/container decoding (`--decoder auto|wvdc|libraw`, `--nev-frames <spec>`) for Nikon `.NEV` multi-frame RAW video containers (`#frame=K` virtual frames across CLI `--plan-json`, `--export-topk`, and GUIs), bare `.jxs`, Nikon HE/HE\*, Sony ARW6, and 4-IFD `.DXO` SuperRAW.
- Added handheld single-focus linear optical operators (`OP-O1` Sellmeier continuous spectral CA, `OP-O2` 3-disc zero-ringing positive STF + exact circle-strip integration, `OP-O3` signed chiral swirl/de-nisen/aspect/onion bokeh shaping with 2 px foreground erosion), multi-regime physical refocusing (`focus-stack`, `--virtual-fstop`, `--tilt-plane`, `--bokeh-sa`, `--see-through`), and `O1–O10` decoupled scientific observers (`--observers`, `--obs-pitch-deg`, `--plate-scale`, `27` selectable `--fields`).
- Added `NOPB/1` + `NIB/1` zero-copy binary sidecar bus (`--bus-serve`, `--bus-in`, `--bus-export`, Python/C SDK in `sdk/`), configurable depth/observer gates, and a redesigned 3-column × 3-workspace native desktop studio (`AppKit` & `Win32 GDI+`) with 100% engine parameter coverage, first-launch wizard, and live language switcher in the About dialog.

## 2.1.0 — Final v2.x Release

> Milestone note: `v2.1.0` is the final release of the `v2.x` architecture line prior to next-generation `v3` development.

- Added native `1×` True-Sampled 16-bit RGB reconstruction (`--rgb-scale 1x|2x|both`, minimum 3 frames) alongside `2×` super-resolution (tightened minimum gate to 5 frames) and `tiff-hq` visually-lossless 16-bit shot-noise / YCoCg Deflate TIFF (`~0.48×` lossless TIFF size).
- Added `--no-tool-exif [on|off]` (default `off`) and GUI checkbox (`Omit Tool Info in EXIF` / `不在 EXIF 内写入本工具信息`) to omit `Tag 11 ProcessingSoftware` and tool branding from `Tag 305 Software` across DNG, TIFF, PNG (`eXIf`), and JPEG XS (`Exif`) while preserving source camera firmware and full `ExifIFD` / `GPSInfo` / ICC metadata.
- Added external `.lng` language packs (`lang/zh-CN.lng`, `lang/en-US.lng`) with live UI hot-switching, `--log-file`, and one-click GUI session + CLI subprocess log export (`⌘L` / `Ctrl+L`), plus Win32 multi-line English layout polish.
- Added battery device detection (`--battery-device auto|on|off`, defaulting to physical cores / SMT-off on battery), discrete Vulkan GPU prioritization over desktop iGPUs, default CPU-preferred mode on APUs and pre-Xe2 Intel iGPUs with a one-time 10s first-launch indicator, and archived AVX-512 (`docs/ARCHIVE_AVX512.md`) while keeping Apple Metal system-default device selection untouched.
- Verified and fixed real-photo observability and output paths across optics probe, physical fields, depth/parallax HTML, bokeh refocus, and paired dataset tile export.
- Reorganized all repository and release documentation in bilingual English-above-Chinese format.

## 2.0.2 — Grouping controls and engine selection

- Added Auto grouping / Single group, target frame count, Regroup, and a visible
  grouping summary with the reasons for splitting.
- Processing follows the previewed group boundaries and checked frames;
  `--single-group` keeps an explicit input set in one burst.
- Fixed Suite inheriting a legacy Scaler CLI path and immediately exiting with
  code 2 on newer options.
- Version-scoped CLI and external LibRaw paths; ordinary processing preferences,
  output formats and window layout continue across versions.
- Fixed Windows queue/frame text clipping in captured and offset drawing contexts.
- Unified CLI, GUI preference keys and package versions under `heraw_version.hpp`.

## 2.0.1 — Pipeline reuse and performance

- Added per-stage wall/device timings and per-pass ROI work accounting.
- Indexed IBP source blocks, parallelized diagnostics, and used fixed sparse
  samples for residual telemetry while retaining all reconstruction samples.
- Matched DCT, chroma and RL work regions to their actual dependencies; enlarged
  outer tiles within device buffer and working-set limits.
- Reused sensor-coordinate residuals and added a Metal residual prepass.
- Reused the local fusion-variance map for C6 analytical fallback. Independent
  half-stack estimation remains active when its phase conditions are satisfied.
- Added separable tent reuse for dense half-stack sampling, shared nonlocal
  matches across color channels, and moved whitening normalization out of inner
  pixel/spectrum loops.
- Kept TIFF output lossless with faster Deflate; measured size increase is about
  1% on the comparison photos.
- Fixed MinGW AVX2 value-copy alignment in the full-frame tile planner.
- Fixed the Metal/Vulkan DCT shared-input halo for shifted final blocks at odd
  pyramid dimensions, preserving the existing block enumeration.
- Reduced RGB RL working memory with dependency-aware bands and delayed halo
  commits; full-image comparisons produce identical values.
- Full-resolution Photo, including lossless DNG + TIFF I/O, on M1 Max / Metal /
  6 CPU threads: Z8 5/6 frames **38.68/43.37 s**, A9 III **16.50/20.27 s**,
  X-H2S **22.48/24.91 s**. See [performance measurements](docs/PERFORMANCE.md).

## 2.0.0 — NijiN.LiteBurstSuite

- Four native workspaces with shared burst/frame inspection, AF ranking, anchor
  selection, homepage shortcuts, persistent settings, and Top-K original exports.
- Unified Bayer/X-Trans CFA and black-level handling, including X-Trans DNGs whose
  LibRaw absolute pattern table is empty.
- Shared streamed CPU/Metal/Vulkan reconstruction with directional source ROIs,
  adaptive tile splitting, global statistics, and matching kernel arithmetic.
- Exposure normalization, sensor-coordinate residual cleanup, directional frame
  weighting, RAW back-projection, half-stack/KLT shrinkage, and spatial PSF models.
- Clean plate, virtual ND, action strobe, transient highlights, reflection
  estimates, relative-depth WebGL previews, and photon-domain refocus.
- Eleven common physical fields plus advanced local normal/occlusion fields;
  named TIFF channels, grayscale PNGs, packed RGB, and dimensional metadata.
- RAW optical measurements, drift telemetry, reusable LCP models, and paired
  leave-one-out datasets with linear arrays and per-pixel sigma.
- Three-component lossless SOF3 LinearRaw DNG, corrected NoiseProfile tagging,
  RAF/CR3 metadata extraction, and robust file-write error propagation.
- Fixed IRLS residual indexing after percentile calculation, multi-group explicit
  output naming, and sigma-map display clipping.
- Preserved capture order with missing timestamps and fixed a MinGW AVX2
  aggregate-parameter alignment fault in multi-threaded sensor calibration.

## 1.6.4 — LiteBurstScaler foundation

- High-ISO sparse-star detection/alignment, discrete maximum-principle ringing
  control, and updated astro tone rendering.
- Lossless output defaults and split-gray physical fields retained in Suite.

---

# 更新日志

## 3.0.0-preview2 — 选片侧车多卡无冲突聚合、`.NEV` 真实位深 DNG 量纲对齐、周期核/CFA 子空间消混叠、`--crop-box`、Vulkan `M5-A` 对齐与全画幅加速

- **多卡同名选片机身隔离聚合与 `.NEV` 导出 DNG 线性量纲对齐（`P0/P1`）**：新增 `--from-picks [auto|dop|xmp]` 选片侧车锚定分簇，支持多卡同名文件（`cardA/DSC_0001` 等）独立成簇与无覆盖落盘、兼容 DCF 跨目录连号（`145` -> `146`）、按机身序列号严格隔离同型号双机拍摄（零跨机身混片），并支持负向 `.xmp` 星标/废片准确否决正向 `.dop`；修复 Nikon `.NEV` 导出 DNG 时误写容器 `65535` 白电平导致二次导入线性亮度仅剩 `23.85%` 的问题，精确推导真实传感器位深（Z8 N-RAW `WhiteLevel = 16383`、`BlackLevel = 1008`，二次导入线性一致率 `99.48%–99.68%`）。
- **EXIF 八方位 `--crop-box` 零冗余裁切与 `.nlens` 域外严格隔离**：新增遵循 EXIF 摆正坐标（覆盖方位 `1..8` 及 Sony `ActiveArea` 偏置）的 `--crop-box x,y,w,h` 局部重建，保证导出 DNG（`DefaultCropSize` / `ActiveArea` 冗余像素为 `0`）、TIFF（`1x`/`2x`/`both`）与 Multi-Part OpenEXR 几何严格对齐；确保局部 `.nlens` 视场标定凸包外像素与 `--lens-profile off` 逐码值一致（域外漂移通道数 `0`）。
- **六抽头 Lagrange 光学峰保护、双独立短曝平台证据门与受限足迹条件化投影（`A / B+ / C1+C2`）**：Stage 1 引入同色子晶格双轴五次多项式精确六抽头留一预测器（`[1,-6,15,0,15,-6,1]/20`），在不放宽单像素电子热噪抑制的前提下保留二维相长干涉光学峰（`96.2%–98.7%`）；Stage 3 新增带原始帧去重（`!same_raw_capture`）与伴随色矛盾否决（`saw_colour && !contradicted`）的双独立短曝高光平台证据门；对包围曝光高光边缘受限帐篷足迹启用条件化保守下界投影（`denom >= 0.5 * full_norm` 并以 `full_norm` 归一化步长），消除单角自由抽头 `16×` 过冲并完整传播逐通道 `bound_only` 下界溯源标签。
- **Stage 4C 布里渊细单元门控、`M5-B` 共线消色边钳位、Vulkan `M5-A` 双核对齐与 `2.38×` 全画幅加速（`V1/V2`）**：Stage 4C 新增 `8×8` 细单元空间残差门控与带内单模 `M=1` 投影，`M5-B` 新增 `3×3` 共线中性消边夹紧与 $4\times 4$ Walsh-Hadamard 空域门控；同步 Vulkan `k_m5a_scatter` 局部跨度方差项与 `k_m5a_update` 色度斜率双门控并重编内嵌全部 `49` 个 SPIR-V 着色器（`RTX 4080 SUPER` Vulkan 对比同机 CPU `2×` EXR `RMS = 0.000215`，差 `> 0.10` 像素为 `0`），同时将 Z8 全画幅 8 帧 Metal `2×` 耗时由 `95.29 s` 压降至 `40.09–41.75 s`（提速 `2.38×`）。

## 3.0.0-preview-fix2 — 抗纱窗六点精确质心核、高光伪紫边根治、复数 OTF 确定性锐化、开放 `.nlens` v1、分半 FRC 评估与 17 种 RGB / 27 物理场编码

- **分辨率卡纱窗纹、切向伪条纹与可分辨区受限修复（`A1–A4`）**：Stage 4A 升级为多帧 `1×` 四轴八向 CFA 感知 RCD/Hamilton-Adams 基底（同步修正 GRBG/GBRG 绿相位奇偶性）、单调钳位 Keys 三次核、移除 `0.02` 硬跳变分支、`d < 9.0` $C^1$ Hermite 锚点渐变、亚像素高频振荡松弛门限与 16 相位 Bayer/X-Trans 周期完备性混合；`inv_cov` 结构张量改在全分辨率 `1×` 绿导引上计算并加入四方向五点单调比守卫（`rho_nyq`）抑制切向伪条纹；`M5-A` 由存在 `±0.141 px` 质心跳变的四点核升级为非负六点 $C^1$ 精确一阶矩核（单精度内部完整支撑质心误差 `1.45e-7 px`），并在多帧下按 `1/N` 缩放小波 Tobit 死区。
- **多帧高光红/蓝/紫边伪色根治与包围短曝恢复（`A5–A7`）**：非包围曝光序列默认启用逐通道 `0.985` 饱和度门禁，包围曝光序列精确恢复短曝高光辐射单调性；Stage 4B 改为同心 `(2r+0.5, 2c+0.5)` 双重饱和衰减白平衡色差锚点与高绿通道正超额阻尼，Stage 5B 升级为以 `2×` 绿导引锚定的 $2\times 2$ 双边色度上采样，`M5-A`（CPU/Metal/Vulkan 三端）升级为白平衡空间精确 Cramér-Rao Schur 变量代换（消除未白平衡空间 `1:1:1` 耦合引发的竖向条纹横向伪纹与中性边缘红/蓝/紫边）并加入高光对立色度冻结（`G > 0.80 * sat`），同时在 Stage 4B、`Q4/Q5` 与 `joint` `1×` 全链路统一叠加径向三次 + 切向 LCA 与逐帧 OIS 源传感器偏移。
- **确定性信息量支撑锐化与开放 `.nlens` v1 光学模型（`A8–A13`）**：移除纯 `1×` `sinc` 分支的五点后置模糊；统一 `1×` 与 `2×` `M5-B` 复数 Wiener 反卷积（$C^1$ 平滑频域正则、CPU/Metal/Vulkan 三端同源逐块 `N_eff`/相位/四向可辨门控且单帧多帧额外增益严格归零、耦合 `M5-A` 收缩因子、复平面安全圆盘投影、二阶曲率门控、四向脊谷突出度防黑边/防白边钳位、阻尼对立色度传输与 `--relay-raw` 一致性）；新增完全中立的开放三档 `.nlens` v1 镜头校准模型（`--lens-profile auto|off|path.nlens`、[`docs/NLENS_SPEC_AND_LLM_PROMPT.md`](docs/NLENS_SPEC_AND_LLM_PROMPT.md)，含联合状态 `(焦距,光圈,屈光度)` 与二维视场凸包严格门禁、域外回退与 `off` 逐码一致、以及 EXIF 焦平面像元间距传导）、宽裙边眩光预扣除与连拍瞬态三自由度焦平面微倾角校正。
- **强化连拍适格性评估与独立留一帧计量（`A14`）**：新增 $4\times 4$ 布里渊区混叠 Modified Gram-Schmidt Schur 完备度 $\Phi_L$、`4.763 MiB + <= 578 KiB` 流式 16 块奇偶分半 `FRC`（支持奇数帧 `3+2` 不等方差二次闭式换算与删一法标准误）、最弱轴保守有效带宽 $f_{\min}$（同时输出 `f_min/f_med/f_max`）与 `MP_band`、排除检验帧的独立 `N - 1` 留一法 `--eval-self` 自评（单帧返回 `valid=false`）、斜边 `eSFR MTF50`、名义帧权信噪比增益（dB）、边缘过冲率、R/B 残余色边（px）及四档适格性结论（`INSUFFICIENT_EVIDENCE` / `RECOMMEND_1X` / `MARGINAL_2X` / `STRONG_2X`），在 CLI `[Metrics]`、`*_metrics.json`、`--plan-json` 与双端 GUI 同步呈现。
- **27 项物理场编码矩阵、八向 EXIF 同步与 17 种流式 RGB 实用格式（`A15–A16`）**：新增 `--field-scale native|1x|2x`（完整覆盖全部 8 种 EXIF 方位 `1..8` 含镜像 `2/4/5/7` 的场像素、有向法线分量及 `--rgb-scale both` 双倍率独立侧车映射，并消除冗余整幅场缓冲）、`--field-format auto|png|tiff|jxl|exr|webp|nib`、`--field-precision auto|u8|u16|f16|f32|log16|oct32`（含 `symmetric_z128` 有符号 `u8`）、`64×64` 分块回调流式 EXR 直写、`--field-compression lossless|vis-lossless` 及 `F-A..F-D` 压扩编码；流式 `--rgb-format` 扩展至 17 种跨平台易打开格式（`tiff-deflate`、`tiff-hq`、`tiff-lzw`、`tiff-8`、`tiff-raw`、`psd`/自动 `psb`、`jxl`、`jxl-lossless`、`jp2`、`jp2-lossless`、`png`、`png-8`、`jpg` 4:4:4、`webp` VP8L、`exr`、`jxs`、`jxs-lossless`），并在双端 GUI 完整覆盖。

## 3.0.0-preview-fix1 — Vulkan 多分块 2x 回读崩溃紧急修复

- 修复 Vulkan 后端在多分块 Stage 4/5 `2x` 超分非首行分块（`core_y0 > 0`）退出析构阶段因悬垂栈槽复用触发的 `vkDestroyBuffer` 访问违例崩溃（[#9](https://github.com/A2Sumie/NijiN.LiteBurstSuite/issues/9)）：将 `VulkanEngine::GpuBuf` 重构为移动语义 RAII 句柄并移除 `heraw_gpu_vulkan.hpp` 内全部原始指针析构列表，补齐 `run_stage45_gpu_full` 互斥锁，并新增 ASan `stack-use-after-scope` 与纵向多分块 `2x` 回归门禁。

## 3.0.0-preview — Unified V3 统一重构、nijinWVDC 解码、手持光学算子、NOPB/1 总线与 O1–O10 观测

- 上线跨 CPU SIMD（`NEON`/`AVX2`）、Apple `Metal` 与 `Vulkan 1.0`（`49` 个 SPIR-V 内核）的 Unified V3（`M0–M5`）统一流式重建管线与零临时盘两遍内存条带调度（`--mem-budget-gb`）：16 KB 4 点 Gauss-Legendre 精确积分 `NLT-VST`、`64×64` COLA 2D FFT 四倍频程 `SURE`、`M5-A` Schur GLS + `3×3` 色雅可比收缩与 `M5-B` R2C FFT 反卷积（`--ibp-iters`、`--olpf-mode`）。
- 内置静态 `nijinWVDC` 容器与 RAW 解码路由（`--decoder auto|wvdc|libraw`、`--nev-frames <spec>`），原生支持 Nikon `.NEV`（`#frame=K` 虚拟帧在 CLI/GUI 全链路贯通）、裸 `.jxs`、Nikon HE/HE\*、Sony ARW6 与 4-IFD `.DXO` SuperRAW。
- 新增手持单次对焦线性光学算子（`OP-O1` Sellmeier 连续光谱色差、`OP-O2` 三圆盘零振铃正性 STF 与精确圆条带积分、`OP-O3` 有向手性旋焦/去二线/拉伸/净洋葱圈散景整形 + 2 px 前景防泄色腐蚀）、多物理域重对焦（`focus-stack`、`--virtual-fstop`、`--tilt-plane`、`--bokeh-sa`、`--see-through`）以及可独立选测的 `O1–O10` 科学观测器与 27 项 `--fields` 物理场（含 `--obs-pitch-deg`、`--plate-scale`）。
- 新增 `NOPB/1 + NIB/1` 零拷贝侧挂控制总线（`--bus-serve`、`--bus-in`、`--bus-export` 与 `sdk/` Python/C 参考客户端），并在 macOS Cocoa 与 Windows Win32 双端落地三栏 × 三工作区全参数工作台、首次启动向导与「关于」窗内常驻语言切换器。

## 2.1.0 — v2 系列最终收官版

> **里程碑说明**：`2.1.0` 为 `v2.x` 架构系列的最后一个版本（Final v2.x Release），后续将进入下一代 `v3` 重大架构演进。

- 新增原生 **1× 真采样 16-bit RGB 重建**（`--rgb-scale 1x|2x|both`，门槛 `>=3` 张；`2×` 超分默认门槛收紧至 `>=5` 张）以及 **`tiff-hq` 16-bit 视觉无损自适应压缩**（散粒噪声步长 + YCoCg 差分 Deflate，约为无损 TIFF 的 `0.48×` 体积）。
- 新增 **EXIF 工具印记省略开关（`--no-tool-exif [on|off]`，默认关闭）** 与双端 GUI 勾选框（`不在 EXIF 内写入本工具信息` / `Omit Tool Info in EXIF`），开启后在 DNG、TIFF、PNG（`eXIf`）与 JPEG XS（`Exif`）中完整省略 `Tag 11 ProcessingSoftware` 及 `Tag 305` 工具后缀，原样保留原机固件与全部 `ExifIFD` / `GPSInfo` / ICC 元数据。
- 新增外置 `.lng` 多语言包（`lang/zh-CN.lng`、`lang/en-US.lng`）与实时切换、CLI `--log-file` 与双端一键导出完整会话诊断日志（`⌘L` / `Ctrl+L`），并优化 Windows 英文界面多行折行排版消除截断省略号。
- 新增电池设备检测（`--battery-device auto|on|off`，电池供电默认关闭 SMT 使用物理核）、桌面端 Vulkan 独显优先排序、APU 及 Xe2 以下核显默认走纯 CPU（配首次启动 10 秒呼吸提示），并将 AVX-512 归档至 [`docs/ARCHIVE_AVX512.md`](docs/ARCHIVE_AVX512.md)（保持 Apple Metal 默认设备选择不变）。
- 复测并修复真实 RAW 下光学体检、物理场可观测性、2.5D 视差网页、线性散景重对焦与训练数据集切片导出路径。
- 全套文档与发布说明按「英文在上、简体中文在下」双语结构重构。

## 2.0.2 — 分组控制与引擎隔离

- 新增自动分组/整组处理切换、目标张数、`重新分组` 按钮及可见分组摘要与切分依据；处理严格跟随队列预览边界与勾选项，`--single-group` 保持显式输入同组执行。
- 修复新版 GUI 继承旧版 Scaler 路径导致退出码 2 的问题；仅引擎与外部 LibRaw 路径按版本隔离，处理参数与窗口布局跨版本记忆。
- 修复 Windows 队列与单帧检视绘制裁剪；将 CLI、GUI 偏好键与包版本统一至 `heraw_version.hpp`。

## 2.0.1 — 管线复用与性能优化

- 增加分阶段耗时统计与按依赖域 ROI 计费；索引 IBP 源块、并行化诊断统计，固定稀疏样本监控残差并保持全量样本重建。
- 对齐 DCT / À-Trous 色度 / RL 依赖域并扩大外层瓦片；复用硅片残差与局部融合方差 $\eta$（满足相位条件时独立半栈仍保持激活）。
- Metal / Vulkan 奇数金字塔尺寸下 DCT halo 修正、RGB RL 分带省内存实现、MinGW AVX2 对齐修复与更高吞吐的无损 TIFF Deflate；M1 Max Metal 6T 下 Z8 5/6 帧 **38.68 / 43.37 s**、A9 III **16.50 / 20.27 s**、X-H2S **22.48 / 24.91 s**。

## 2.0.0 — NijiN.LiteBurstSuite 架构发布

- 推出四标签原生桌面端（macOS `AppKit` / Windows `Win32 GDI+`），统一 Bayer / Fuji 6×6 X-Trans CFA 管线与流式 CPU / Metal / Vulkan Stage 4–5 重建。
- 落地 `C1–C13` 核心算子、`11` 项物理场、`W1–W14` 创意/计量/数据集工作流与三通道 `LinearRaw` SOF3 DNG 导出。

## 1.6.4 — LiteBurstScaler 基础线

- 极高 ISO 稀疏星空识别与二阶多项式配准、`3×3` 离散极值防黑框守卫、无损 DNG/TIFF 默认编码及独立灰度物理场输出。
