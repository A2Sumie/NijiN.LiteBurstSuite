# NijiN.LiteBurstSuite (`nijinLBS`)

**[English](#nijinliteburstsuite-nijinlbs) · [简体中文](#nijinliteburstsuite-nijinlbs-简体中文)**

**Multi-Frame RAW Imaging Suite** (`Metal` · `Vulkan` · `ARM64 NEON` · `x86_64 AVX2`)

> **Current Release: `v2.1.0` — Final `v2.x` Release**

`NijiN.LiteBurstSuite` (**`nijinLBS`**) provides multi-frame **`2×` RAW/RGB super-resolution, native `1×` 16-bit True-Sampled RGB enhancement, temporal synthesis, 11 physical fields, `8×` linear RAW optics metrology, and paired training datasets**. Native desktop applications are built with `AppKit` (macOS) and `Win32 GDI+` (Windows), sharing four synchronized workspaces, a persistent Burst Queue & Frame AF Inspector, external `.lng` live language switching (`en-US` / `zh-CN`), one-click session diagnostic log export, and `--no-tool-exif` privacy control. Built-in `LibRaw 0.23-HE*` natively supports Bayer, Fujifilm `6×6` X-Trans, and Nikon Z8/Z9 HE/HE*.

<p align="center">
  <img src="assets/ui_macos.png" width="49%" alt="macOS Native AppKit UI" />
  <img src="assets/ui_windows.png" width="49%" alt="Windows Native Modern Win32 UI" />
</p>

## Suite Workspaces

| Workspace | Key Capabilities |
|---|---|
| Super-Res & Output | AF subject triage, locked anchor frame, lossless RGGB / 3-component `LinearRaw` DNG, `2×` SR (`>=5` frames) / `1×` True-RGB (`>=3` frames) / dual `2×+1×` 16-bit RGB (including visually-lossless `tiff-hq`), `--no-tool-exif`, CPU/GPU topology & battery controls |
| Temporal & Creative | Pedestrian-Free Clean Plate, Handheld Virtual ND, Action Strobe, Transient Highlights Layer, Specular Glint Suppression / Wide-Baseline Reflection Split, Interactive 2.5D Light-Field HTML & Linear Bokeh Refocus |
| Fields & Dataset | 11 physical fields (+ normal/occlusion geometry fields), split 16-bit gray PNG / packed RGB / TIFF named extra channels, paired `1×` noisy RAW + `2×` ground truth + `σ_map` tiles |
| Optics & Sensor | Subpixel drift trajectory, `2×2` phase quadrant coverage, `8×` linear RAW stellar ePSF / slanted-edge MTF50 radar, silicon readout telemetry, `.json`, passport PNG & Adobe `.lcp` |

See [Implementation Mapping](docs/IMPLEMENTATION.md) for source entry points and test targets, and [Specification Index](docs/README.md) for design volumes. Measurements report explicit units, sample counts, and validity gates; benchmarks in [Performance Analysis](docs/PERFORMANCE.md) record image sizes, frame counts, and active stages.

> 📖 **Documentation Index**: [**`ALGORITHM.md` (Core Physics & Physical Field Maps)**](ALGORITHM.md) ｜ [**`CLI.md` (CLI Options & Convergence Reference)**](CLI.md) ｜ [**`NOTICE.md` (Third-Party Attributions)**](NOTICE.md)

---

## Super-Resolution Foundation

Baseline convergence and compression ratios below reference fixed Nikon Z8 samples from `v1.6.4`; Suite stages are verified with independent boundary tests and real full-resolution RAW regressions (`docs/VALIDATION.md`).

- **Zero-Dependency Cross-Platform GPU + SIMD Acceleration (`--backend auto|gpu|cpu`)**
  - **macOS (`Metal`)**: Native `Metal.framework` unified zero-copy compute (`MTLResourceStorageModeShared`).
  - **Windows (`Vulkan`)**: Embeds 24 precompiled Vulkan 1.0 `SPIR-V` compute shaders using the GPU driver's native `vulkan-1.dll` (NVIDIA / AMD / Intel, zero external SDKs). On an `RTX 4080 SUPER` + `Ryzen 7 5800X3D`, 6-frame 45.7 MP Nikon Z8 HE* → **182.8 MP (`16560×11040`)** 16-bit Photo reconstruction completes end-to-end in **`16.97 s`**. Desktop discrete GPUs outrank integrated GPUs automatically; mobile APUs and pre-Xe2 Intel iGPUs default to optimized CPU execution unless GPU is selected.
  - **CPU fallback (`ARM64 NEON` / `x86_64 AVX2`)**: Cache-topology-aware streaming (`heraw_cpu_cache.hpp`), AC/DC battery detection (disabling SMT by default on battery), and cross-backend 16-bit parity exceeding **`PSNR > 82 dB`**.
- **Lossless-First Encoding & Visually-Lossless Lossy Modes (Full EXIF/GPS Retention)**
  - **`2×` RGGB Linear DNG 1.4**: Defaults to **`DNG 16-bit Lossless`** (`SOF3` 2D predictor, `0.39×` / `~135 MiB`), plus advanced lossy `dng-hq` (`0.09×` / `~30 MiB`), 16-bit half-float HDR `dng-fp16` (`0.29×`), 3-component `dng-linear`, and uncompressed `dng-raw` (`1.00×`).
  - **`2×` / `1×` Rendered RGB (`sRGB` / embedded ICC v2.4 `Adobe RGB`)**: Defaults to **`16-bit Lossless TIFF`** (`Predictor=2` word-differenced parallel `Deflate`, `0.39×` / `~410 MiB` — smaller and faster than 16-bit PNG), plus visually-lossless **`tiff-hq`** (`~0.48×` lossless TIFF size, `0.19×` uncompressed), `16-bit PNG` (`0.40×`), 14-bit **JPEG XS** (`jxs` `0.21×` / `jxs-lossless` `0.43×`, with `--decode-jxs`), and uncompressed TIFF (`1.00×`).
  - **Metadata Privacy (`--no-tool-exif [on|off]`)**: Off by default. When enabled, exported DNG, TIFF, PNG (`eXIf`), and JPEG XS (`Exif`) omit `Tag 11 ProcessingSoftware` and all tool branding while keeping source camera firmware `Tag 305` and full `ExifIFD` / `GPSInfo` / ICC metadata.
- **Four Scene Presets (`Photo` / `Sci` / `Arc` / `Astro`)**
  - **`Photo`**: Temporal gradient coherence preservation, 3-gate rigid micro-patch SR, optical PSF Richardson-Lucy deconvolution, adaptive auto-exposure, hue-preserving highlight rolloff, and flat-region edge polish.
  - **`Sci`**: Strictly radiometrically linear truth (RL deconvolution, auto-exposure, highlight rolloff, and edge polish disabled; conservative spatial denoise `0.40/0.55`, stricter deghosting `1.35`).
  - **`Arc`**: Neutral archive master without PSF sharpening or edge polish.
  - **`Astro`**: Built for **untracked handheld astrophotography / nightscapes** — cross-CFA neighbor Star-Eater gate, chroma preservation for stellar blackbody & emission nebulae, dual-domain celestial 6-term quadratic polynomial + horizon flow alignment, `ISO 64–102400` hot-pixel-immune lens-cap black-frame filtering, halo-bounded stellar RL, and shadow-anchored color-preserving `asinh` stretch.
- **Inline Diagnostics & AF Subject Triage**
  - Computes scene diagnosis, `2×2` quadrant coverage, sharpest crown anchor candidate, and per-frame AF/drift telemetry inside the GUI immediately after drop-in (plus interactive terminal `--wizard`).

---

## Core 6-Stage Physical Pipeline

`nijinLBS` relies strictly on **optical sampling geometry and Poisson-Gaussian photon statistics** with zero generative hallucination (complete equations in [**`ALGORITHM.md`**](ALGORITHM.md)):

1. **Stage 0 — 5D Scene Diagnosis & Hot-Pixel-Immune Burst Clustering**: Millisecond EXIF header scan, truncated/double-green `1/4`-scale matched filtering for `ISO 64–102400` dark-frame rejection and star detection, and crown frame selection.
2. **Stage 1 — Dark-Frame-Free `G1/G2` Bias Calibration & Cross-CFA Anti-Star-Eater Gate**: Symmetrically cancels `G1/G2` electronic offset using patch medians without separate dark frames; separates single-diode hot pixels from true optical point sources using orthogonal opposite-color neighbor leakage on the `1×` Bayer mosaic.
3. **Stage 2 — Blind Poisson-Gaussian Noise Calibration**: Regresses readout variance $\sigma_0^2$ and shot gain $g$ across log-luminance bins and refines via temporal frame differences (immune to lossy compressed RAW spatial smoothing).
4. **Stage 3 — Pyramid Subpixel Flow, Astro Dual-Domain Alignment & Anti-Hollow Deghosting**: Coarse-to-fine ZNCC parabolic optical flow (or 6-term stellar quadratic + horizon alignment in `Astro`), dual-scale Poisson-Gaussian Z-score rejection + morphological closing to prevent translucent moving-subject hollows, and 3-gate rigid micro-patch SR recovery.
5. **Stage 4 — Structure-Tensor Anisotropic Dual-Green + Color-Difference `R/B` Reconstruction**: Computes smooth structure tensors along/across edges to aggregate `G1/G2` samples via robust M-estimator weights and reconstructs white-balanced chromatic differences ($\Delta R, \Delta B$) with zero zippering (`2×` SR or native `1×` True-Sampled RGB).
6. **Stage 5 & Stage 6 — Rationalized Anscombe GAT Multi-Scale Wiener, Focus-Coupled RL & Lossless Encoding**: Normalizes noise variance without catastrophic cancellation, shrinks across a 5-level `8×8` DCT Wiener pyramid modulated by temporal gradient coherence, applies `3×3` Discrete Maximum Principle ringing guard + 6-level luminance-guided À-Trous chroma denoising + focus-coupled halo-bounded RL deconvolution, and writes DNG `SOF3` / word-differenced Deflate TIFF.

---

## 16-bit Physical Field Maps

Enabling GUI **Phys Fields** (`--out-field <prefix>` / `--emit-field`) exports pixel-registered 16-bit lossless maps (`[0, 65535]`). The default carrier (`--field-mode split`) writes independent 16-bit single-channel grayscale PNGs directly usable as Layer Masks or deep-learning weights:

| Default Gray PNG | Physical Quantity | Scale (`0 Black -> 65535 White`) | Primary Workflow Usage |
| :--- | :--- | :--- | :--- |
| **`*_field_motion.png`** *(or `_field_sky.png` in Astro)* | Anti-hollow moving object / sky matte | Standard: moving regions white, static black; `Astro`: sky white, ground black | Direct Photoshop / Affinity layer mask for isolated sky/subject edits |
| **`*_field_phase.png`** | `2×2` subpixel SR confidence | Higher value = fuller 4-quadrant phase sampling & valid frame stack | Per-pixel confidence weights in `Sci` model training (`L1`/MSE mask) |
| **`*_field_focus.png`** | 2nd-order Laplacian optical focus field | High-SNR in-focus structure white, defocus bokeh `0` | Invert for pure optical bokeh masking or corner field-curvature inspection |

Extended fields (`depth`, `defocus`, `specular`, `sigma`, `coherence`, `velocity`, `flicker`, `turbulence`) are selectable via `--fields` and can export via `split`, `rgb` packed PNGs, or `tiff-alpha` named channels (see [`CLI.md`](CLI.md) & [`ALGORITHM.md`](ALGORITHM.md)).

---

## Quick Start

### 1. Prebuilt Packages ([Releases](https://github.com/A2Sumie/NijiN.LiteBurstSuite/releases))

- **macOS (Apple Silicon `arm64`, `.dmg`)**:
  1. Open the `.dmg` and drag **`NijiN.LiteBurstSuite.app`** into **`Applications`** (bundles both AppKit GUI and `nijinLBS` CLI).
  2. **First-launch Gatekeeper clearance (unsigned ad-hoc bundle)**: Run once in Terminal if macOS reports an unverified developer:
     ```bash
     xattr -rd com.apple.quarantine /Applications/NijiN.LiteBurstSuite.app
     ```
     *(Or approve via **System Settings → Privacy & Security → Open Anyway**.)*
  3. CLI path inside bundle: `/Applications/NijiN.LiteBurstSuite.app/Contents/MacOS/nijinLBS --help`.
- **Windows (`x64`, `.zip`)**: Extract the release archive and launch **`nijinLBS_gui.exe`** or run **`nijinLBS.exe`** from terminal.

### 2. Common CLI Examples

```bash
# 1) Default 2× handheld burst -> lossless RGGB DNG (0.39x) + lossless 16-bit TIFF (0.39x)
./nijinLBS --out-raw out_2x.dng --out-rgb out_2x.tif Z8S_8551.NEF Z8S_8552.NEF Z8S_8553.NEF Z8S_8554.NEF Z8S_8555.NEF Z8S_8556.NEF

# 2) Native 1× True-Sampled 16-bit RGB (>=3 frames) + visually-lossless tiff-hq + omit tool EXIF branding
./nijinLBS --rgb-scale 1x --rgb-format tiff-hq --no-tool-exif on --out-rgb-1x out_1x.tif Z8S_8636.NEF Z8S_8637.NEF Z8S_8638.NEF

# 3) Untracked handheld nightscape: 5D wizard + split 16-bit field maps (_sky / _phase / _focus.png)
./nijinLBS --wizard --out-raw astro_2x.dng --out-rgb astro_2x.tif --out-field astro_field Z8S_8787.NEF Z8S_8788.NEF Z8S_8789.NEF Z8S_8790.NEF Z8S_8791.NEF Z8S_8792.NEF

# 4) Recursive card scan: EXIF auto-clustering + Adobe RGB 14-bit JPEG XS
./nijinLBS --scan-dir /Volumes/NIKON_Z8/DCIM/116NCZ_8 --policy multi-group --out-dir ./sr_out --emit-rgb --rgb-format jxs --jxs-bpp 10.0 --color-space adobergb
```

See [**`CLI.md`**](CLI.md) for all options and [**`ALGORITHM.md`**](ALGORITHM.md) for equations and PyTorch training examples.

---

## Building from Source

```bash
# Recompile 24 Vulkan SPIR-V shaders (requires glslc when modifying shaders/build_shaders.py)
python3 shaders/build_shaders.py

# Build macOS native Metal CLI + AppKit GUI
make nijinLBS nijinLBS_gui LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# Package macOS .app and .dmg
make dmg LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# Cross-compile Windows x64 static CLI + Modern GDI+ Win32 GUI (requires mingw-w64)
make nijinLBS.exe nijinLBS_gui.exe LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# Package standalone Windows release archive
make windows-zip
```

License: First-party source code is licensed under [**Apache License 2.0 (`Apache-2.0`)**](LICENSE); bundled third-party notices appear in [**`NOTICE.md`**](NOTICE.md).

---

# NijiN.LiteBurstSuite (`nijinLBS`) 简体中文

**Multi-Frame RAW Imaging Suite**（`Metal` · `Vulkan` · `ARM64 NEON` · `x86_64 AVX2`）

> **当前版本：`v2.1.0` — `v2.x` 系列最终收官版（Final v2.x Release）**

`NijiN.LiteBurstSuite`（简称 **`nijinLBS`**）提供连拍 **2× RAW/RGB 超分、原生 1× 16-bit 真采样 RGB 强化、时间重编程、物理场、光学诊断和配对数据导出**。原生桌面端采用 macOS `AppKit` 与 Windows `Win32 GDI+`，共享四工作区、常驻连拍/单帧检视栏、外置 `.lng` 多语言热切换（内置 `zh-CN` / `en-US`）、会话诊断日志一键导出与 `--no-tool-exif` 元数据开关。内置 `LibRaw 0.23-HE*`，支持 Bayer、Fuji 6×6 X-Trans 和 Nikon Z8/Z9 HE/HE*。

## Suite 工作区

| 工作区 | 常用操作 |
|---|---|
| 超分与主输出 | AF 主体选片、指定锚点、无损 RGGB / 三通道 LinearRaw DNG、2× 超分（≥5 张）/ 1× 真彩（≥3 张）/ 双出 16-bit RGB（含 `tiff-hq` 视觉无损 TIFF）、`--no-tool-exif` 隐私开关、CPU/GPU 与电池策略 |
| 时空与创意 | 去人空镜、虚拟慢门、频闪分身、瞬态高光层、碎高光/反射分离、相对深度视差网页与线性散景 |
| 物理场与数据 | 11 个可选场、独立 16-bit PNG / packed RGB / TIFF 命名通道、配对 RAW1×/RGB2×/sigma 数据块 |
| 镜头与机身 | 漂移轨迹、四象限覆盖、RAW 8× 星点/斜边测量、光学图卡、JSON 与 Adobe LCP |

实现入口和验收方法见 [Suite 实现说明](docs/IMPLEMENTATION.md)，完整设计分册见 [规格索引](docs/README.md)。测量结果附带单位、样本量与状态；性能按实际尺寸、帧数和启用功能记录。

> 📖 **完整文档导航**：[**`ALGORITHM.md`（核心算法与物理场图技术文档）**](ALGORITHM.md) ｜ [**`CLI.md`（命令行全参数与收敛参考）**](CLI.md) ｜ [**`NOTICE.md`（第三方组件声明）**](NOTICE.md)

---

## 超分基础

本节收敛与体积数字来自 v1.6.4 的固定 Nikon Z8 样本；Suite 的新增阶段与 Wave-2 优化通过独立边界测试与真实 RAW 回归验收（实测耗时详见 [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md)）。

- **零依赖跨平台 GPU + SIMD 加速（`--backend auto|gpu|cpu`）**
  - **macOS (`Metal`)**：原生调用 `Metal.framework` 统一内存零拷贝计算；
  - **Windows (`Vulkan`)**：内嵌 24 个预编译 Vulkan 1.0 `SPIR-V` 计算着色器，运行时动态加载显卡驱动自带 `vulkan-1.dll`（NVIDIA / AMD / Intel 通用，无需安装额外 SDK 或运行库）。在 `RTX 4080 SUPER` + `Ryzen 7 5800X3D` 上，6 张 45.7 MP Nikon Z 8 HE\* 合成 **1.83 亿像素（`16560×11040`）** 16-bit 无损 DNG + TIFF 端到端总耗时 **`16.97 s`**；桌面独显优先排序高于核显，APU 与 Xe2 以下核显默认走纯 CPU；
  - **CPU 后端（`ARM64 NEON` / `x86_64 AVX2`）**：运行时缓存拓扑与 AC/DC 电池供电感知（电池默认关超线程），跨端输出 16-bit 一致性达 **`PSNR > 82 dB`**。
- **默认采用最高效 16-bit 数学无损压缩（并支持高级有损压缩与完整 EXIF/GPS 继承）**
  - **2× RGGB 线性 DNG 1.4**：默认 **`DNG 16-bit 无损`**（`SOF3` 2D 同相位预测，体积 **`0.39×`** / `~135 MiB`），可选高级有损压缩（`0.09×` / `~30 MiB`）、16-bit 半精度浮点 HDR（`0.29×`）、三通道线性 `dng-linear` 与 16-bit 未压缩（`1.00×`）；
  - **2× / 1× 渲染 RGB（`sRGB` / 内嵌 ICC v2.4 `Adobe RGB`）**：默认 **`16-bit 无损 TIFF`**（字级 `Predictor=2` 水平差分 + 并行 `Deflate`，体积 **`0.39×`** / `~410 MiB`，在全 16-bit 无损格式中体积最小且编码最快），可选 **`16-bit 视觉无损 TIFF (tiff-hq)`**（约无损 TIFF 体积的 `0.48×`）、16-bit 无损 PNG（`0.40×`）、14-bit **JPEG XS**（`.jxs` 高级有损 `0.21×` / 无损 `0.43×`，附 `--decode-jxs` 回转工具）与 16-bit 未压缩 TIFF（`1.00×`）；
  - **EXIF 印记控制（`--no-tool-exif`）**：默认关闭；开启后导出 DNG / TIFF / PNG / JPEG XS 不写入 `Tag 11` 与任何工具名称，原样保留原机固件 `Tag 305` 及完整拍摄/GPS/ICC 信息。
- **四档场景预设（`Photo 照片` / `Sci 科学` / `Arc 归档` / `Astro 星空`）**
  - **`Photo`**：跨帧时域梯度相干保微纹理 + 三门控刚体微块超分 + 光学 PSF Richardson-Lucy 反卷积 + 自适应影调；
  - **`Sci`**：严格辐射度线性真值输出（关闭 PSF 反卷积、自动曝光、高光滚降与边缘抛光），适合科研测量与深度学习监督训练；
  - **`Arc`**：中性温和降噪与长期数字底片母版留存；
  - **`Astro`**：专为**免赤道仪、免脚架手持打星 / 手持星野**设计——跨通道拜耳邻域“防吞星”门控、恒星黑体与星云色彩保护、星点质心 6 项二阶多项式 + 天际线**天地双域独立对齐**、跨全感光度（`ISO 64–102400`）盖镜头黑场自动跳过，以及无黑环星形 RL 与夜空黑位锁定 `asinh` 柔和拉伸。
- **内联场景推荐与 AF 单帧检视**
  - 导入后显示场景、相位覆盖、皇冠帧和推荐配置；在队列中采纳配置、排除帧或指定锚点。CLI 保留 `--wizard` 供交互终端使用。

---

## 核心算法六步管线概览

`nijinLBS` 完全基于**光学采样几何与光子泊松-高斯统计建模**（零生成式脑补），通过六阶段管线将手持连拍亚像素微位移解混叠至 `2×`（或原生 `1×` 真彩）网格（完整数学推导详见 [**`ALGORITHM.md`**](ALGORITHM.md)）：

1. **Stage 0 — 五维场景诊断与抗热噪分簇**：毫秒级读取 EXIF 时序分簇；在 `1/4` 尺度提取双绿次大值 `gq_2nd`，免疫高感热噪实现跨全感光度（`ISO 64–102400`）黑场过滤与针尖星点计数，选举无首帧快门震动的皇冠最锐参考帧。
2. **Stage 1 — 免黑图 `G1/G2` 盲暗电平与跨通道“防吞星”门控**：利用同光谱响应双绿分块中位数盲消通道偏置并守卫极暗部负偏；通过检验 `1×` 拜耳正交异色四邻域的光学艾里斑能量泄漏，精准区分单像素电路热噪与 1–2 像素微弱恒星，根除“吞星（Star Eater）”。
3. **Stage 2 — 盲泊松-高斯（Poisson-Gaussian）噪声自标定**：分亮度分位拟合读出噪声 $\sigma_0^2$ 与散粒噪声增益 $g$，结合跨帧时域差分二次校准，免疫有损压缩 RAW 机内空间平滑干扰。
4. **Stage 3 — 金字塔亚像素配准、天地双域对齐与防镂空去鬼影**：由粗到精 ZNCC 抛物面亚像素光流（`Astro` 模式下自动启用星点 6 项二阶多项式天球场 + 天际线地景双域独立配准）；经细/宽双尺度去鬼影、`1/4` 尺度形态学闭包填实纯色动体内部空洞，并通过**三门控刚体微块捞取**回收局部刚性平移目标的多帧亚像素样本。
5. **Stage 4 — 结构张量各向异性梅花阵双绿 + 残差 R/B 重建**：在对齐导引图上分解结构张量构造沿边缘拉长、跨边缘收紧的自适应椭圆核，以抗差 M-estimator 聚合 `N` 帧 `G1/G2` 采样，并在白平衡色差域（$\Delta R, \Delta B$）无伪色重建全彩线性传感器图像。
6. **Stage 5 & 6 — 有理化 Anscombe GAT 多尺度维纳、对焦耦合 RL 与无损输出**：采用零浮点抵消的有理化 GAT 变换将异方差噪声归一化为单位高斯噪声，执行时域梯度相干保护的 **5 层滑动 `8×8` DCT 经验维纳收缩** + **6 层亮度引导 À-Trous 色度降噪** + **二阶拉普拉斯对焦场耦合防黑环 Richardson-Lucy 反卷积**，最终封装为无损 `SOF3` DNG、字级差分 `Predictor=2` 无损 TIFF 或视觉无损 `tiff-hq`。

---

## 16-bit 物理场图指南

勾选 GUI **「物理场图」**（或 CLI 加 `--out-field <prefix>` / `--emit-field`）后，默认**分离导出三张与 `2×` 主图同尺寸、逐像素对齐的 16-bit 单通道无损灰度 PNG**（`0 = 纯黑`，`65535 = 纯白`），也可通过 `--field-mode` 一键切换为 **合并 RGB** 或 **主 TIFF 命名附加通道 (`tiff-alpha`)**：

| 默认灰度场图文件 | 承载物理量 | 灰度含义（`0 黑 -> 65535 白`） | 典型工程与后期用途 |
| :--- | :--- | :--- | :--- |
| **`*_field_motion.png`**<br>*(星野为 **`*_field_sky.png`**)* | **防镂空运动掩膜场**<br>*(星野模式为**天空/地景蒙版**)* | 常规模式：越白代表连拍中局部移动的人物/车辆/树叶，纯黑代表静态背景；<br>`Astro` 星野模式：越白代表星空天球域，纯黑代表地面山脊前景。 | 直接拖入 PS / Affinity 作为**调整图层黑白蒙版**，零手绘单独提亮运动人物，或单独拉高夜空银河曲线而丝毫不伤地面山脊。 |
| **`*_field_phase.png`** | **`2×2` 亚像素超分置信度** | 越白代表手持微抖动踩满 `2×2` 四相位象限且有效帧数充足（真 `2×` 解混叠含金量最高）。 | 在 `Sci 科学` 模式训练深度学习超分/去噪模型时直接作为单通道 `uint16` 像素级 Loss 权重，屏蔽动体退化伪真值。 |
| **`*_field_focus.png`** | **二阶拉普拉斯光学对焦场** | 越白代表处于镜头景深合焦面且具真实高频结构；`0` 代表焦外散景虚化或纯色区。 | 反相即得**纯焦外散景蒙版**，或直观检视镜头全开光圈下的场曲与跑焦落点。 |

- **模式切换选项（`--field-mode`）**：`split`（**默认灰度 PNG**）、`rgb`（合并 RGB 场图）、`tiff-alpha`（主 TIFF 命名通道）及单场导出。详见 [**`ALGORITHM.md`**](ALGORITHM.md) 与 [**`CLI.md`**](CLI.md)。

---

## 快速上手

### 1. 开箱即用（Releases 预编译包）
前往 [**Releases**](https://github.com/A2Sumie/NijiN.LiteBurstSuite/releases) 下载对应平台安装包：

- **macOS (Apple Silicon `arm64` — `.dmg` 镜像安装包)**：
  1. 打开下载版本的 `.dmg`，将 **`NijiN.LiteBurstSuite.app`** 拖入 **`Applications`（应用程序）**（内置 GUI 与 CLI，支持拖入 RAW 文件/目录）；
  2. **macOS 首次打开“解冻”（解除 Gatekeeper 隔离属性）**：由于应用采用本地 Ad-hoc 签名而未挂载付费 Apple Developer ID 公证，若首次启动提示“已损坏”或“无法验证开发者”，在「终端 (Terminal)」执行：
     ```bash
     xattr -rd com.apple.quarantine /Applications/NijiN.LiteBurstSuite.app
     ```
     *（或在 **「系统设置 → 隐私与安全性」** 底部点击 **「仍要打开 (Open Anyway)」**。）*
  3. 终端调用：`/Applications/NijiN.LiteBurstSuite.app/Contents/MacOS/nijinLBS --help`。
- **Windows (`x64`)**：解压下载版本的 `.zip` 安装包，双击 **`nijinLBS_gui.exe`**，或终端调用 `nijinLBS.exe`。
- **偏好自动记忆**：双端 GUI 自动记忆上次选择的格式、尺度、语言、EXIF 印记开关与微调参数，下次启动自动恢复。

### 2. 常用命令行示例
```bash
# 1) 日常手持连拍：默认同时输出最高效无损格式 —— 2x 16-bit 无损 DNG (0.39x) 与 16-bit 无损 TIFF (0.39x)
./nijinLBS --out-raw out_2x.dng --out-rgb out_2x.tif Z8S_8551.NEF Z8S_8552.NEF Z8S_8553.NEF Z8S_8554.NEF Z8S_8555.NEF Z8S_8556.NEF

# 2) 原生 1× 真采样 16-bit RGB (>=3 张) + 视觉无损 tiff-hq + 不写工具 EXIF 印记
./nijinLBS --rgb-scale 1x --rgb-format tiff-hq --no-tool-exif on --out-rgb-1x out_1x.tif Z8S_8636.NEF Z8S_8637.NEF Z8S_8638.NEF

# 3) 免脚架手持打星 / 星野：启动五维智能向导并导出三张独立 16-bit 灰度物理场图 (_sky / _phase / _focus.png)
./nijinLBS --wizard --out-raw astro_2x.dng --out-rgb astro_2x.tif --out-field astro_field Z8S_8787.NEF Z8S_8788.NEF Z8S_8789.NEF Z8S_8790.NEF Z8S_8791.NEF Z8S_8792.NEF

# 4) 整卡目录批量扫描：自动按 EXIF 时间分簇、输出 Adobe RGB 14-bit JPEG XS
./nijinLBS --scan-dir /Volumes/NIKON_Z8/DCIM/116NCZ_8 --policy multi-group --out-dir ./sr_out --emit-rgb --rgb-format jxs --jxs-bpp 10.0 --color-space adobergb
```

完整参数表与收敛曲线详见 [**`CLI.md`**](CLI.md)，完整数学模型与场图 PyTorch/PS 示例详见 [**`ALGORITHM.md`**](ALGORITHM.md)。

---

## 从源码构建

```bash
# 重新编译 24 个 Vulkan SPIR-V 计算着色器（仅修改 shaders/build_shaders.py 时需要，需 glslc）
python3 shaders/build_shaders.py

# 构建 macOS 原生 Metal CLI + Cocoa GUI（传入静态 LibRaw 与 SVT-JPEG-XS 目录）
make nijinLBS nijinLBS_gui LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# 打包 macOS 原生 NijiN.LiteBurstSuite.app 与 .dmg 安装镜像
make dmg LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# 交叉编译 Windows x64 静态 CLI + 现代 GDI+ Win32 GUI（需 mingw-w64）
make nijinLBS.exe nijinLBS_gui.exe LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# 打包 Windows 自包含 CLI/GUI、文档与组件许可
make windows-zip
```

开源许可：本项目自有源码采用 [**Apache License 2.0 (`Apache-2.0`)**](LICENSE) 授权（允许自由商用与修改，须保留原作者署名、`LICENSE`、`NOTICE.md` 及修改说明）；预编译二进制涉及的第三方组件声明见 [**`NOTICE.md`**](NOTICE.md)。
