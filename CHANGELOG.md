# Changelog

**[English](#changelog) · [简体中文](#更新日志)**

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
