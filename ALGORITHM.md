# `NijiN.LiteBurstSuite` (`nijinLBS`) Core Algorithm & Physical Field Maps Technical Reference

**[English](#nijinliteburstsuite-nijinlbs-core-algorithm--physical-field-maps-technical-reference) · [简体中文](#nijinliteburstsuite-nijinlbs-核心算法与物理场图技术文档)**

This document preserves the core `v1.6.4` multi-frame mathematical derivations alongside `v2.x` updates. For Suite v2 CFA handling, tiled scheduling, IBP, temporal/optics module mapping, see [Implementation Map](docs/IMPLEMENTATION.md) and [Specification Index](docs/README.md). Outputs default to lossless DNG/TIFF, and physical fields default to split 16-bit grayscale PNGs with optional TIFF named extra channels.

---

## 1. Design Objective & Sensor Sampling Geometry

### 1.1 Pure Physical Statistical Modeling
On high-resolution OLPF-less cameras (such as 45.7 MP Nikon Z8/Z9 or Sony A7R bodies), lens optical cut-off frequencies exceed the single-frame Nyquist limit, creating **optical aliasing** and color moiré.

`nijinLBS` operates purely on **optical sampling geometry and Poisson-Gaussian photon statistics** with zero generative neural synthesis: natural handheld subpixel shifts or shutter micro-shake are de-aliased back onto a **`2×` super-resolution grid** (or enhanced at **`1×` True-Sampled RGB**), ensuring every restored detail traces directly to measured sensor photons.

### 1.2 Bayer CFA Density & Minimum Frame Gates
For raw sensor dimensions $H_1 \times W_1$ ($H_h \times W_h = \frac{H_1}{2} \times \frac{W_1}{2}$ `2×2` `RGGB` Bayer quads) and target `2×` grid $H_2 \times W_2 = 2H_1 \times 2W_1$:
- **Green luma carrier (`G1` + `G2`)**: Quincunx diagonal grid at `1/2` sensor density (`1/8` density per frame on the `2×` lattice).
- **Red/Blue chroma carriers (`R` / `B`)**: Each at `1/4` sensor density (`1/16` density per frame on the `2×` lattice).
- **Algebraic & robustness gates**: Solving all four `2×2` subpixel phase quadrants $\{(0,0), (0,1), (1,0), (1,1)\}$ requires an algebraic minimum of **$N = 4$ frames** (`--policy force-all`), becomes over-determined for M-estimator outlier rejection and quarter-density `R/B` chroma at **$N = 5$ frames** (default `2×` SR gate; `1×` True-RGB minimum is **$N = 3$ frames**), and reaches its quality sweet spot at **$N = 6\text{–}8$ frames** (`80%–92%` subpixel quadrant fill and `+7.6 ~ +8.8 dB` SNR gain).

---

## 2. Six-Stage Core Physical Pipeline (Stage 0 – Stage 6)

```text
RAW Burst Sequence (.NEF / .DNG / .ARW / .CR3 / .RAF / .RW2)
  │
  ▼ [Stage 0] Fast Header Scan · Full-ISO Black-Frame Filter · 5D Scene Diagnosis & Burst Clustering
  ▼ [Stage 1] Dark-Frame-Free G1/G2 Blind Bias Calibration · Cross-CFA Anti-Star-Eater Hot-Pixel Gate
  ▼ [Stage 2] Blind Poisson-Gaussian Read/Shot Noise Calibration
  ▼ [Stage 3] Multi-Scale ZNCC Flow / Astro Dual-Domain Alignment · Anti-Hollow Deghosting · 3-Gate Rigid Patches
  ▼ [Stage 4] Structure-Tensor Anisotropic Dual-Green Fusion · Residual R/B Reconstruction  [CPU SIMD / Metal / Vulkan]
  ▼ [Stage 5] Rationalized Anscombe GAT 5-Level 8×8 DCT Wiener · 6-Level À-Trous Chroma · Focus-Coupled Bounded RL
  ▼ [Stage 6] Color Matrix · Adaptive Tone · Lossless/HQ Encoding (DNG SOF3 / TIFF Predictor=2 / tiff-hq) · EXIF/GPS
```

---

### Stage 0: 5D Scene Diagnosis & Clustering (`heraw_burst_planner.hpp`)

1. **Millisecond header scan & time sorting**: Parses TIFF/EXIF headers without unpacking RAW image data, filters AppleDouble `._*` sidecars, and sorts by `DateTimeOriginal` + `SubSecTimeOriginal` + natural filename order with configurable gap clustering (`3.5s` default).
2. **`1/4`-scale trimmed double-green `gtrim` + `5×5` PSF matched filtering**: Within each `4×4` raw cell (4 double-green pairs), trims min and max to reject cosmic-ray/hot single pixels (`gtrim`), then applies separable Gaussian PSF matched filtering `gpsf` ($\sigma \approx 4$ `1x` pixels):
   - Cleanly separates capped black frames (`maxSNR <= 6.2`, zero `>= 6.5σ` stars) from high-ISO sparse-star telephoto shots (`ISO 64–102400`, e.g., `2–4` stars at `maxSNR = 55–95`);
   - Automatically picks dense 6-term quadratic, medium 3-term affine, or sparse 1–3 star rigid translation/roll alignment whenever pyramid `ZNCC < 0.48`.
3. **5D scene prescription & crown anchor selection**: Evaluates **(1) global drift distribution, (2) `2×2` quadrant coverage, (3) moving-subject area fraction, (4) EV span, (5) star count & horizon edge energy** across 7 physical shooting regimes and chooses the sharpest crown anchor free of first-frame shutter shock.

---

### Stage 1: Dark-Frame-Free `G1/G2` Bias Calibration & Cross-CFA Anti-Star-Eater Gate

1. **Symmetric `G1/G2` blind bias cancellation (no dark frames required)**:
   Computes `32×32`-cell patch medians across `R / G1 / G2 / B`:
   - Subtracts $\Delta_{G21} = \text{median}(m_{G2} - m_{G1})$ to remove channel-to-channel green electronic bias (preventing checkerboard artifacts);
   - Anchors black drift at the `0.5%` dark percentile while preserving signed linear floating-point values $[-0.10, 1.05]$ so zero-mean Gaussian shadow noise never acquires positive truncation bias.
2. **Cross-CFA Opposite-Color Neighbor Gate**:
   Computes per-plane `3×3` median $m$ and diagonal wavelet std $\hat{\sigma}_{\text{HH}}$. Pixels exceeding $m + 5.5\hat{\sigma}_{\text{HH}}$ are tested against their **4 orthogonal opposite-color `1×` Bayer neighbors**:
   $$\Delta_{\text{cross}} = \max_{k \in \{N,S,W,E\}} \big(I_{1\times}^{(k)} - m\big)$$
   - **True optical stars / specular glints**: Airy diffraction + microlens crosstalk leak photons into orthogonal CFA neighbors so $\Delta_{\text{cross}} > \tau \cdot (I - m)$ (`Astro` $\tau = 0.14$, standard $\tau = 0.24$) → **preserved intact**;
   - **Single-diode hot pixels / cosmic rays**: Isolated spike with background-level orthogonal neighbors → repaired symmetrically (fixed warm pixels appearing across $\ge \lceil(N+1)/2\rceil$ frames are cleaned across the whole burst).

---

### Stage 2: Blind Poisson-Gaussian Noise Calibration

Linear sensor variance follows the affine photon + readout model:
$$\text{Var}(I) = \sigma_0^2 + g \cdot I$$
Log-luminance binning extracts flat micro-patches to blindly regress readout variance $\sigma_0^2$ and shot gain $g$, then performs a secondary **inter-frame temporal-difference calibration** on aligned static pixels—rendering calibration immune to in-camera spatial smoothing in lossy compressed RAWs (e.g., Nikon `HE` / `HE*`).

---

### Stage 3: Pyramid Flow, Astro Dual-Domain Alignment, Anti-Hollow Deghosting & Rigid Micro-Patches

1. **Coarse-to-fine ZNCC parabolic subpixel flow**: Gaussian pyramid ZNCC matching (invariant to minor exposure jitter) with 2D parabolic subpixel fit down to `0.01` pixel accuracy.
2. **`Astro` dual-domain celestial + horizon alignment**:
   - **Sky domain**: Outliers-rejected least squares fit of **6-term 2nd-order polynomial** star flow:
     $$\Delta x(x,y) = a_0 + a_1 x + a_2 y + a_3 x^2 + a_4 xy + a_5 y^2, \quad \Delta y(x,y) = b_0 + b_1 x + b_2 y + b_3 x^2 + b_4 xy + b_5 y^2$$
   - **Horizon fusion**: Segments sky vs foreground ridges from star density and dual-flow residuals to lock pinpoint stars on the polynomial field and landscape on ZNCC optical flow.
3. **Dual-scale P-G Z-score deghosting & Anti-Hollow Morphological Closing**: Evaluates aligned neighbors at fine ($\sigma = 1.4$) and wide ($\sigma = 4.0$) scales with gradient tolerance; applies `1/4`-scale large-radius morphological closing to fill interior untextured regions of moving subjects (preventing translucent "donut" ghosts) plus a 160-px cosine-tapered blend `m_wide`.
4. **3-Gate Rigid Micro-Patch SR**: Locally translating rigid patches rejected by the static background undergo local search; those passing **(1) `ZNCC >= 0.90`, (2) fine P-G residual, and (3) wide P-G residual** regain multi-frame subpixel fusion, while non-rigid motion smoothly falls back to clean-Bayer-guided Hamilton-Adams interpolation.
5. **Temporal Gradient Coherence field**: Accumulates weighted gradient vectors $\bar{\mathbf{g}} = \frac{1}{W}\sum_n w_n \nabla I_n$ to compute phase coherence $C \in [0, 1]$, shielding subtle multi-frame textures from frequency-domain shrinkage in Stage 5.

---

### Stage 4: Structure-Tensor Anisotropic Dual-Green + Color-Difference `R/B` Reconstruction

1. **Smooth structure tensor & adaptive elliptical kernel**: Computes $J = G_\rho * (\nabla I \nabla I^\top)$ eigenvectors, dominant tangent angle $\theta = \frac{1}{2}\arctan2(2J_{xy}, J_{xx}-J_{yy})$, and SNR-gated anisotropy $A \in [0, 1]$, stretching $\sigma_\parallel \to 0.64$ along edges and compressing $\sigma_\perp \to 0.28$ across edges.
2. **Robust M-estimator `G1/G2` fusion & noise variance ratio**: Aggregates sensor green samples at every target site while accumulating the local effective noise ratio:
   $$\eta_{\text{eff}} = \left\langle \frac{\sum_k \alpha_k^2}{\left(\sum_k \alpha_k\right)^2} \right\rangle$$
3. **Residual color-difference `R/B` reconstruction**: Aggregates white-balanced $\Delta R = w_R R - G_{\text{sr}}$ and $\Delta B = w_B B - G_{\text{sr}}$ isotropically and adds back high-resolution $G_{\text{sr2x}}$ (or `1×` True-Sampled grid), eliminating demosaicing zippering and false color.

---

### Stage 5: Cancellation-Free Rationalized Anscombe GAT + Multiscale DCT Wiener + À-Trous + Bounded RL

1. **Rationalized Generalized Anscombe Transform (GAT)**: Post-fusion residual noise follows $(g_{\text{eff}} = \eta_{\text{eff}} g,\; \sigma_{\text{eff}}^2 = \eta_{\text{eff}} \sigma_0^2)$. To avoid floating-point cancellation when $g_{\text{eff}} \to 0$, `nijinLBS` uses the numerator-rationalized identity:
   $$z(x) = \frac{2(x + p)}{\sqrt{g_{\text{eff}}(x + p) + c} + \sqrt{c}}, \qquad c = \frac{3}{8}g_{\text{eff}}^2 + \sigma_{\text{eff}}^2$$
   smoothly converging to $(x+p)/\sigma_{\text{eff}}$ as $g_{\text{eff}} \to 0$ and stabilizing noise across the entire dynamic range to unit variance $\sigma_z = 1$.
2. **5-level sliding `8×8` DCT Wiener & asymptotically unbiased inverse**: Shrinks coefficients on a 5-level pyramid modulated by $(1 - 0.52 C)$, enforces **`3×3` Discrete Maximum Principle ringing guard** ($\hat{z} \leftarrow \text{clamp}(\hat{z}, \min_{3\times 3} z, \max_{3\times 3} z)$), and inverts using `4×4` variance loss $\Delta v = \min\!\big(\frac{1}{16}\sum (z - \hat{z})^2,\; 1.25\big)$:
   $$\hat{x} = \sqrt{c}\,\hat{z} + \frac{1}{4}g_{\text{eff}}\big(\hat{z}^2 + \Delta v\big) - p$$
3. **Luminance-guided 6-level À-Trous chroma denoise & selective glint suppression**: Joint bilateral wavelet filter over $\Delta R, \Delta B$ at strides $2^0 \dots 2^5$ guided by clean luma; automatically preserves chromaticity in `Astro`.
4. **Focus-coupled & halo-bounded Richardson-Lucy deconvolution**: Scales RL updates by `field_focus` (`0.18` in smooth bokeh vs `0.86–1.18` on sharp in-focus detail) and enforces a lower-bound multiplicative clamp to prevent dark rings around stars.

---

### Stage 6: Color Transforms, Tone Mapping & Lossless-First Encoding

1. **Color matrix, adaptive tone & TPDF dither**: Transforms to `D65 sRGB` or `ISO 12640-2 Adobe RGB (1998)` (embedded ICC v2.4), applies preset auto-exposure / shadow-anchored `asinh` (Astro) / hue-preserving `tanh` rolloff, and adds 64-bit hashed **TPDF dither**.
2. **Why `16-bit Lossless TIFF` (`0.39×`) beats `16-bit Lossless PNG` (`0.40×`) in both size and speed**:
   TIFF `Tag 317 Predictor=2` computes horizontal differences modulo `65536` directly on **16-bit unsigned words**:
   $$\Delta_{r,c}^{(ch)} = \big(I_{r,c}^{(ch)} - I_{r,c-1}^{(ch)}\big) \bmod 65536$$
   PNG `Sub` filtering operates bytewise on high and low 8-bit bytes separately, causing carry discontinuities at multiples of `256` and extra big-endian byte swapping. On RGB bursts, `tiff-deflate` is both smaller (`~410 MiB` vs `~420 MiB`) and faster; **`tiff-hq`** further quantizes residuals inside the shot-noise envelope ($\approx 0.48\times$ lossless TIFF size) while staying 100% standard 16-bit TIFF readable.

---

## 3. 16-bit Physical Field Maps Usage Guide

### 3.1 Field Definitions

| Default Split Filename | Packed RGB Ch | Physical Field | Value Meaning (`0 Black -> 65535 White`) | Computation & Physical Source |
| :--- | :---: | :--- | :---: | :--- |
| **`_field_motion.png`** *(`_field_sky.png` in Astro)* | `R` | Anti-hollow motion mask / Sky-horizon matte | `0` = static / ground<br>`65535` = moving subject / night sky | $1 - \frac{\sum_{n=1}^{N-1} w_n}{\sum_{n=1}^{N-1} w_{\text{frame},n}}$ + `1/4`-scale closing; sky/ground matte in Astro dual-domain |
| **`_field_phase.png`** | `G` | `2×2` subpixel SR confidence | `0` = single-frame fallback<br>`65535` = full 4-quadrant true `2×` SR | $0.55 \cdot Q_{2\times 2} + 0.45 \cdot \frac{1 + \sum w_n}{N}$ |
| **`_field_focus.png`** | `B` | Optical focus sharpness | `0` = defocus bokeh / flat sky<br>`65535` = in-focus high-frequency structure | Shot-noise-gated 2nd-order Laplacian energy envelope driving Stage 5 RL |

### 3.2 Packed RGB Quick Legend (`--field-mode rgb`)
- **Cyan (low `R`, high `G`, high `B`)**: Static + full `2×` SR + sharp focus.
- **Pure Green (low `R`, high `G`, low `B`)**: Static + multi-frame denoise + smooth bokeh/sky (RL suppressed).
- **Magenta (high `R`, low `G`, high `B`)**: Sharp moving subject frozen on anchor frame.

### 3.3 Workflows (Photoshop Mask, PyTorch Training Loss & Lens Audit)

```python
import cv2, numpy as np, torch

gt_rgb   = cv2.cvtColor(cv2.imread("sample_nijinLBS.tif", cv2.IMREAD_UNCHANGED), cv2.COLOR_BGR2RGB)
f_motion = cv2.imread("sample_nijinLBS_field_motion.png", cv2.IMREAD_UNCHANGED)
f_phase  = cv2.imread("sample_nijinLBS_field_phase.png",  cv2.IMREAD_UNCHANGED)

gt       = torch.from_numpy(gt_rgb.astype(np.float32) / 65535.0).permute(2, 0, 1)
w_phase  = torch.from_numpy(f_phase.astype(np.float32) / 65535.0)
w_static = 1.0 - torch.from_numpy(f_motion.astype(np.float32) / 65535.0)

pixel_weight = (w_phase * w_static).unsqueeze(0)
loss = (torch.abs(pred - gt) * pixel_weight).sum() / (pixel_weight.sum() * 3.0 + 1e-6)
```

---

## 4. Cross-Backend Numerical Parity (CPU / Metal / Vulkan)

CPU (`NEON` / `AVX2`), macOS `Metal` (`heraw_gpu_metal.mm`), and Windows `Vulkan` (`heraw_gpu_vulkan.hpp` + 24 embedded SPIR-V kernels) share identical structure-tensor kernels, rationalized GAT math, sliding DCT shrinkage thresholds, variance-loss clamp ($\Delta v \le 1.25$), and focus-coupled RL equations, achieving **`PSNR > 82 dB`** across 45.7 MP Nikon Z8 16-bit masters (differences well below the 16-bit LSB thermal floor).

---

# `NijiN.LiteBurstSuite` (`nijinLBS`) 核心算法与物理场图技术文档

本文保留 v1.6.4 超分基础数学推导。Suite v2 的 CFA、分块调度、回投、时空与光学模块映射见 [实现说明](docs/IMPLEMENTATION.md)，设计分册见 [规格索引](docs/README.md)。输出默认采用无损 DNG/TIFF，场图默认独立灰度并支持 TIFF 命名通道。

---

## 1. 设计目标与传感器采样几何

### 1.1 纯物理统计建模原则
高像素无光学低通滤镜（OLPF-less）机身（如 45.7 MP Nikon Z 8 / Z 9、Sony A7R 系列等）的镜头光学截止频率往往高于传感器单帧奈奎斯特采样频率（Nyquist Frequency）。单帧拜耳（Bayer CFA）采样因此存在显著的**光学混叠（Aliasing）**与彩色摩尔纹。

`nijinLBS` 完全基于**光学采样几何与光子泊松-高斯统计建模**（零生成式神经网络脑补），利用手持连拍或快门微震带来的天然亚像素位移，将散落在连续帧上的混叠高频信号解混叠（De-aliasing）回 **`2×` 超分网格**（或 **`1×` 真采样 RGB**），保证每一处恢复出的纹理均源于真实的传感器光子测量。

### 1.2 拜耳 CFA 密度与最低超分帧数门槛
设原始传感器分辨率为 $H_1 \times W_1$（包含 $H_h \times W_h = \frac{H_1}{2} \times \frac{W_1}{2}$ 个 `2×2` `RGGB` 拜耳单元），目标 `2×` 超分网格分辨率为 $H_2 \times W_2 = 2H_1 \times 2W_1$：
- **绿色亮度载波（`G1` + `G2`）**：在 `1×` 传感器上呈对角梅花阵（Quincunx）排布，占 `1/2` 像素密度；投影到 `2×` 网格后，每帧提供 `1/8` 密度的真实采样。
- **红/蓝色度载波（`R` / `B`）**：在 `1×` 传感器上各占 `1/4` 像素密度；投影到 `2×` 网格后，每帧提供 `1/16` 密度采样。
- **张数代数门槛**：要完整解出 `1×` 网格内 `2×2` 四个亚像素相位象限 $\{(0,0), (0,1), (1,0), (1,1)\}$ 的独立观测值，最少需要 **$N = 4$ 帧**；当 **$N = 5$ 帧**时，抗差 M-estimator 与 `R/B` 四分之一密度载波进入超定（Over-determined）状态（现行默认 `2×` 门槛 `>=5` 张，`1×` 真彩门槛 `>=3` 张）；**$N = 6\text{–}8$ 帧**为兼顾亚像素象限覆盖率（`80%–92%`）、坏帧剔除冗余与信噪比增益（`+7.6 ~ +8.8 dB`）的最佳甜点区间。

---

## 2. 六阶段核心算法管线（Stage 0 – Stage 6）

```text
RAW 连拍序列 (.NEF / .DNG / .ARW / .CR3 / .RAF / .RW2)
  │
  ▼ [Stage 0] 快速头扫描 · 跨全 ISO 黑场过滤 · 五维场景诊断与智能分簇规划
  ▼ [Stage 1] 免黑图 G1/G2 盲暗电平自标定 · 跨通道异色邻域“防吞星”热噪修复
  ▼ [Stage 2] 盲泊松-高斯 (Poisson-Gaussian) 传感器读出/散粒噪声模型拟合
  ▼ [Stage 3] 多尺度 ZNCC 亚像素光流 / 星野天地双域配准 · 防镂空去鬼影 · 三门控刚体微块捞取
  ▼ [Stage 4] 结构张量各向异性梅花阵双绿超分 · 残差引导 R/B 重建  [CPU SIMD / Metal / Vulkan]
  ▼ [Stage 5] 有理化 Anscombe GAT 五层 8×8 DCT 维纳 · 6 层 À-Trous 色度 · 对焦耦合防黑环 RL
  ▼ [Stage 6] 色彩矩阵 · 自适应影调 · 最高效无损 / tiff-hq 编码 (DNG SOF3 / TIFF Predictor=2) · EXIF/GPS 封装
```

---

### Stage 0：五维场景诊断与连拍分簇规划（`heraw_burst_planner.hpp`）

1. **毫秒级元数据扫描与时序归一化**：仅解析 TIFF/EXIF 头部（跳过 RAW 主数据解码），过滤 macOS AppleDouble `._*` 隐藏副文件，按 `DateTimeOriginal` + 毫秒级 `SubSecTimeOriginal` + 自然文件名顺序排定连拍时序，并根据快门时间间隔（默认 `3.5s`）自动切分候选连拍簇。
2. **`1/4` 尺度截尾双绿 `gtrim` + `5×5` 光学 PSF 匹配滤波抗热噪黑场与极高感星点检测**：
   在每个 `4×4` 原始像素块（含 4 对拜耳双绿采样）内剔除最大与最小极值，取中间两对均值构成抗单点热噪/宇宙射线的 `gtrim`，再施加 `5×5` 可分离高斯匹配滤波 `gpsf`（$\sigma \approx 4$ `1x` 像素，匹配真实恒星光学弥散斑并压低非相关极高感读出噪声）：
   - 在 **`ISO 64–102400` 全感光度范围**将盖镜头全黑帧（`maxSNR <= 6.2`，`0` 颗 `>= 6.5σ` 星点）与超短快门中长焦极高感星空（如 `ISO 102400`、`1/640s`、`120mm` 视场内仅 `2–4` 颗星但 `maxSNR = 55–95`）彻底分离；
   - 支持稠密广角星野（`6` 项二阶多项式）、中密度星场（`3` 项仿射）与中长焦稀疏星场（`1–3` 颗亮星刚体平移/微滚转，且当常规模式金字塔 `ZNCC < 0.48` 时自动启用星点救援配准）。
3. **五维特征智能处方与皇冠锚定帧选举**：
   综合 **(1) 帧间全局位移分布、(2) `2×2` 亚像素四象限覆盖数、(3) 局部动体面积占比、(4) 组内曝光跨度（EV）、(5) 星点数与地景高频能量** 五个维度，自动归类为 7 种物理拍摄形态（手持常规、三脚架稳态、局部动体、高感弱光、纯星空、手持星野、AEB 包围曝光），选举出无首帧快门震动的**皇冠最锐帧（Crown Frame）**并给出对应预设参数处方。

---

### Stage 1：免黑图 `G1/G2` 盲暗电平自标定与跨通道“防吞星”热噪门控

1. **`G1/G2` 同光谱盲暗电平校准（无需拍摄黑场 Dark Frame）**：
   将参考帧划分为 `32×32` 拜耳单元网格，统计 `R / G1 / G2 / B` 四个相位的分块均值：
   - 利用同具绿色滤光片响应的 `G1` 与 `G2` 在非饱和区块的中位数差值 $\Delta_{G21} = \text{median}(m_{G2} - m_{G1})$，对称消除双绿通道间的微弱电子偏置（消除棋盘格伪影根源）；
   - 取四相位分块均值的 `0.5%` 极暗部分位数守卫相机标称黑电平漂移，保留符号化线性浮点区间 $[-0.10, 1.05]$，使极暗部零均值高斯噪声不因硬截断产生正向偏置（Pedestal Bias）。
2. **跨通道异色邻域“防吞星”热噪修复（Cross-CFA Neighbor Gate）**：
   对每个拜耳同色子平面计算 `3×3` 空间中值 $m$ 与对角高频小波噪声标准差 $\hat{\sigma}_{\text{HH}}$。当某像素超出中值 $5.5\hat{\sigma}_{\text{HH}}$ 时，传统单通道中值滤波会误把 1–2 像素的微弱暗星当成热噪抹平（即相机固件常见的“吞星 Star Eater”现象）。
   `nijinLBS` 进一步读取该像素在 **`1×` 原始拜耳棋盘上的上下左右 4 个正交异色邻居**：
   $$\Delta_{\text{cross}} = \max_{k \in \{N,S,W,E\}} \big(I_{1\times}^{(k)} - m\big)$$
   - **真实光学针尖星点 / 镜面高光**：受镜头衍射艾里斑（Airy Disk）与微透镜串扰影响，光子必按光学 PSF 比例泄漏至正交相邻异色像素，满足 $\Delta_{\text{cross}} > \tau \cdot (I - m)$（`Astro` 模式 $\tau = 0.14$，常规模式 $\tau = 0.24$），**完整放行保护**；
   - **传感器单光电二极管热噪点 / 宇宙射线**：仅单个像素电荷异常，正交邻居仍处于背景噪声水平，精准判定为热噪并以空间中值修复（跨 $\ge \lceil(N+1)/2\rceil$ 帧持续出现的固定热噪同步全组清除）。

---

### Stage 2：盲泊松-高斯（Poisson-Gaussian）噪声模型自标定

CMOS 传感器的线性信号噪声方差服从光子散粒噪声（Poisson）与电路读出噪声（Gaussian）的仿射关系：
$$\text{Var}(I) = \sigma_0^2 + g \cdot I$$
引擎将线性亮度域划分为多个对数分位区间，在每个亮度分位内选取梯度最低的平坦微块估计局部噪声方差，通过加权线性回归盲解出单帧**读出噪声方差 $\sigma_0^2$** 与**散粒噪声增益斜率 $g$**。随后在 Stage 3 完成亚像素配准后，进一步利用静态像素的**跨帧时域差分（Temporal Difference）**二次校准 $(\sigma_0^2, g)$——由于帧间散粒噪声相互独立，时域差分天然免疫有损压缩 RAW（如 Nikon `HE` / `HE*`）机内空间小波预处理造成的空间相关性低估。

---

### Stage 3：金字塔亚像素光流、星野天地双域配准、防镂空去鬼影与刚体微块超分

1. **多尺度由粗到精 ZNCC 稠密亚像素光流**：
   对每帧半尺寸双绿图构建高斯金字塔，由顶层至底层执行分块**零均值归一化互相关（ZNCC）**匹配（天然免疫帧间微弱快门曝光波动），在相关曲面上以二维抛物面闭合拟合出 `0.01` 像素精度的连续位移场 $(u_n, v_n)$。
2. **`Astro` 手持星野天地双域独立配准（Dual-Domain Celestial + Horizon Alignment）**：
   手持无赤道仪拍摄带地景的星空时，地球自转叠加广角镜头透视畸变使星空与地面山脊遵循两套截然不同的运动场：
   - **天球域**：提取各帧星点亚像素质心，以抗外点最小二乘拟合 **6 项二阶多项式流场**：
     $$\Delta x(x,y) = a_0 + a_1 x + a_2 y + a_3 x^2 + a_4 xy + a_5 y^2, \quad \Delta y(x,y) = b_0 + b_1 x + b_2 y + b_3 x^2 + b_4 xy + b_5 y^2$$
     二阶交叉项精确吸收超广角镜头中心与四角不同的星轨投影角速度；
   - **地景域与天际线融合**：根据星点空间分布密度与双流场对齐残差自适应分割天空与地景蒙版，天空锁定星点多项式场以聚合针尖星芒，地面锁定金字塔光流以保持山脊建筑清晰。
3. **双尺度泊松-高斯去鬼影与防镂空形态学闭包（Anti-Hollow Closing）**：
   对齐后的邻帧在细尺度（$\sigma = 1.4$）与宽尺度（$\sigma = 4.0$）同时接受带局部梯度容差的泊松-高斯 Z-score 检验。针对走动人物大衣、车厢等“边缘有反差而内部纯色”的动体（纯梯度差分只在轮廓报警，导致内部混入背景产生“甜甜圈”半透明镂空鬼影），在 `1/4` 尺度执行大半径**形态学闭运算（Morphological Closing）**填实动体内部，并生成 **160 像素宽余弦高斯过渡带 `m_wide`** 使动静边界自然衔接。
4. **三门控高置信刚体微块捞取（3-Gate Rigid Micro-Patch SR）**：
   对于画面中局部平移的刚体目标（如缓行的船只、头部微动的宠物），虽与静态背景流场不一致，但局部自身仍保持刚性。引擎对其执行局部精细二次搜索，凡同时通过 **(1) `ZNCC >= 0.90`、(2) 细尺度泊松-高斯残差检验、(3) 宽尺度泊松-高斯残差检验** 三道门控的微块，重新赋予局部亚像素流场纳入真多帧超分；非刚性形变（水波、飘动发丝）则平滑退回由多帧零位移干净拜耳引导的 **Hamilton-Adams 二阶拉普拉斯方向插值**。
5. **跨帧时域梯度相干场（Temporal Gradient Coherence）**：
   在每个位置矢量累加多帧加权梯度 $\bar{\mathbf{g}} = \frac{1}{W}\sum_{n} w_n \nabla I_n$。多帧相位一致的真实微弱纹理（羽毛、远山林木、暗星云丝缕）梯度同向叠加能量守恒，而帧间非相关散粒噪声方差按 $1/W$ 衰减，据此计算相干度 $C \in [0, 1]$，在后续频域滤波中定向保护微弱真细节。

---

### Stage 4：结构张量各向异性梅花阵双绿超分 + 残差引导 R/B 重建（CPU / Metal / Vulkan）

1. **平滑结构张量与自适应椭圆高斯核**：
   在多帧对齐导引图上计算局部结构张量 $J = G_\rho * (\nabla I \nabla I^\top)$，求其特征值 $\lambda_1 \ge \lambda_2$、切向主方向角 $\theta = \frac{1}{2}\arctan2(2J_{xy}, J_{xx}-J_{yy})$ 及信噪比门控各向异性度 $A \in [0,1]$。
   据此构造自适应二维协方差逆矩阵 $\Sigma^{-1}$：在平坦区退化为紧凑各向同性核（半径 $\approx 0.39\text{–}0.46$），在锐利边缘沿切向拉长（$\sigma_\parallel \to 0.64$）、法向收紧（$\sigma_\perp \to 0.28$），实现跨边缘超锐利、沿边缘零锯齿。
2. **梅花阵双绿抗差 M-Estimator 聚合**：
   遍历目标网格（`2×` 超分或 `1×` 真采样），收集所有对齐帧邻近的真实 `G1/G2` 传感器采样点，结合核权重、去鬼影权重 $w_n$ 与泊松-高斯抗差 Huber 权重加权融合为 $G_{\text{sr}}$，同时精确累计有效噪声方差缩减比：
   $$\eta_{\text{eff}} = \left\langle \frac{\sum_k \alpha_k^2}{\left(\sum_k \alpha_k\right)^2} \right\rangle$$
3. **残差引导色差域 `R/B` 重建**：
   利用自然图像红/绿/蓝高频相位高度一致的物理先验，在 `1×` 原始 `R` 与 `B` 采样点计算白平衡域色差残差 $\Delta R = w_R R - G_{\text{sr}}, \Delta B = w_B B - G_{\text{sr}}$ 进行跨帧各向同性聚合。平滑的色差场叠加回高分辨率 $G_{\text{sr}}$ 后，从根源上消除拜耳解马赛克常见的红蓝伪色与边缘拉链（Zippering）。

---

### Stage 5：无抵消有理化 Anscombe GAT 多尺度 DCT 维纳 + À-Trous 色度 + 对焦耦合 RL

1. **无灾难性抵消的有理化广义 Anscombe 变换（Rationalized GAT）**：
   多帧融合后的残余噪声仍服从有效参数为 $(g_{\text{eff}} = \eta_{\text{eff}} g,\; \sigma_{\text{eff}}^2 = \eta_{\text{eff}} \sigma_0^2)$ 的泊松-高斯分布。经典 Anscombe 公式 $\frac{2}{g}(\sqrt{gx + c} - \sqrt{c})$ 在低感或高帧数叠加（$g_{\text{eff}} \to 0$）时存在严重的浮点相减有效位数丢失。`nijinLBS` 采用分子有理化恒等式：
   $$z(x) = \frac{2(x + p)}{\sqrt{g_{\text{eff}}(x + p) + c} + \sqrt{c}}, \qquad c = \frac{3}{8}g_{\text{eff}}^2 + \sigma_{\text{eff}}^2$$
   该形式对任意 $g_{\text{eff}} \ge 0$ 严格光滑有界（当 $g_{\text{eff}} \to 0$ 时平滑收敛至 $(x+p)/\sigma_{\text{eff}}$），将全画幅从极暗部到高光的异方差噪声精确归一化为单位方差 $\sigma_z = 1$ 的平稳高斯白噪声。
2. **5 层多尺度 `8×8` 滑动 DCT 经验维纳收缩与渐进无偏逆变换**：
   在 GAT 稳定域构建 5 层高斯-拉普拉斯金字塔（覆盖微纹理至宽幅暗部低频噪斑），每层执行重叠步长滑动 `8×8` 二维离散余弦变换（DCT）经验 Wiener 硬/软两步收缩，并乘以 Stage 3 算得的时域相干豁免因子 $(1 - 0.52 C)$。
   逆变换前施加 **`3×3` 离散极值原理（Discrete Maximum Principle）吉布斯振铃守卫**（$\hat{z} \leftarrow \text{clamp}(\hat{z}, \min_{3\times 3} z, \max_{3\times 3} z)$，根除满阱亮星旁 `8×8`/`16×16` 频域截断暗框），再通过局部 `4×4` 块正交维纳方差损失 $\Delta v = \min\!\big(\frac{1}{16}\sum (z - \hat{z})^2,\; 1.25\big)$ 闭式无偏还原：
   $$\hat{x} = \sqrt{c}\,\hat{z} + \frac{1}{4}g_{\text{eff}}\big(\hat{z}^2 + \Delta v\big) - p$$
3. **亮度引导 6 层 À-Trous 小波色度降噪与选择性高光消色**：
   以干净的 $G_{\text{sr}}$ 作为边缘停止引导图，对 $\Delta R, \Delta B$ 执行孔洞步长 $2^0 \dots 2^5$ 的 6 层 À-Trous 双边小波滤波，彻底抹除暗部大块红绿彩噪而严守物体色界；常规摄影模式下自动压制金属/水面高光边缘的轴向色差紫边，`Astro` 模式下自动关闭消色以保留恒星 O-B-A-F-G-K-M 黑体色温与 H-alpha 星云品红色彩。
4. **光学对焦场耦合 & 防黑环（Halo-Bounded）Richardson-Lucy 反卷积**：
   - **对焦耦合门控**：计算参考帧二阶拉普拉斯高频能量与单帧散粒噪声的信噪比区域包络 `field_focus`。虚化散景（Bokeh）虽可能有柔和明暗过渡（一阶梯度非零），但二阶导数趋近于零，因此其 RL 迭代增益被自动压制在 `0.18`，而合焦主体保持 `0.86–1.18` 满血光学 PSF 恢复，确保背景散景绝不反超前景主体；
   - **防黑环下界保护**：针对亮星或深色背景上的高反差亮边缘，限制 RL 乘性修正因子对暗侧背景的下探幅度，根除传统 RL 常见的星周“黑眼圈”（Dark Ringing）。

---

### Stage 6：色彩空间、影调映射与最高效无损压缩编码

1. **色彩矩阵、自适应影调与 TPDF 量化抖动**：
   由相机专属 XYZ 色彩矩阵精确变换至线性 `D65 sRGB` 或 `ISO 12640-2 Adobe RGB (1998)`（自动生成并内嵌标准 **ICC v2.4** 配置文件）；按预设执行盲分位数自动曝光、`Astro` 夜空黑位锚定保色 `asinh` 拉伸与 `0.78` 阈值以上的保色相 `tanh` 高光软滚降；最后叠加 64-bit 哈希 **TPDF（三角概率密度函数）抖动**消除 16-bit 天空平滑渐变色带。
2. **默认最高效无损压缩格式与 `tiff-hq` 视觉无损压缩（以各自 16-bit 未压缩体积 = `1.00` 为基准）**：
   - **2× RGGB 默认：`dng`（`DNG 16-bit 无损`，倍率 `0.39×` / 压缩比 `1:2.6`）**
     采用 DNG 1.4 标准 `Compression=7`（ISO/IEC 10918-1 `SOF3` 无损 JPEG），按 `256×256` 分块多线程并行编码，对 `R/G1/G2/B` 四个同色拜耳平面分别施加二维平面预测器 `PSV=7`（$\hat{X} = (A + B)/2$）与自适应霍夫曼熵编码。1.83 亿像素母版仅约 **`135 MiB`**（未压缩 `349 MiB`），且兼容所有主流商业与开源 RAW 软件。
   - **RGB 默认：`tiff-deflate`（`16-bit 无损 TIFF`，倍率 `0.39×` / 压缩比 `1:2.6`）与视觉无损 `tiff-hq`**
     采用 32 行独立分条多线程并行编码，结合 **TIFF `Tag 317 Predictor=2`（16-bit 字级水平差分预测）** 与 `zlib Deflate`：
     $$\Delta_{r,c}^{(ch)} = \big(I_{r,c}^{(ch)} - I_{r,c-1}^{(ch)}\big) \bmod 65536$$
     由于 PNG `Sub` 在 **8-bit 字节流** 上无法跨字节借位，TIFF `Predictor=2` 在 **16-bit 字** 上模 `65536` 差分在体积（`0.39×` vs `0.40×`）与编码速度上同时领先；`tiff-hq` 进一步在散粒噪声包络内自适应量化残差并配合 YCoCg 差分 Deflate，保持 100% 标准 16-bit TIFF 兼容性并将体积压缩至无损 TIFF 的 **`~0.48×`**。

---

## 3. 16-bit 物理场图（默认独立单通道灰度图）详解与实战指南

开启 CLI `--out-field <prefix>` / `--emit-field` 或勾选 GUI **「物理场图」** 后，引擎会额外导出与主图分辨率一致、逐像素对齐的 16-bit 无损 PNG 物理场图（将 $[0.0, 1.0]$ 线性量化至 $[0, 65535]$）。

### 3.1 三张默认物理场图定义表

| 默认灰度图后缀 | 合并模式通道 | 承载物理场 | 灰度数值范围 (`0 黑 -> 65535 白`) | 物理计算来源与含义 |
| :--- | :---: | :--- | :---: | :--- |
| **`_field_motion.png`**<br>*(星野自动为 **`_field_sky.png`**)* | `R` | **防镂空运动掩膜场**<br>或<br>**天空 / 地景分割蒙版** | `0` = 纯静态背景 / 地面前景<br>`65535` = 局部运动物体 / 星空天球域 | **常规模式**：$1 - \frac{\sum_{n=1}^{N-1} w_n}{\sum_{n=1}^{N-1} w_{\text{frame},n}}$，经 `1/4` 尺度形态学闭包填实内部空洞，运动的人物、车辆、晃动树叶呈高亮实心。<br>**`Astro` 星野模式**：当锁定星点天球流场且存在地景时，自动输出多帧累积的天空/天际线概率蒙版（夜空纯白、山脊建筑纯黑）。 |
| **`_field_phase.png`** | `G` | **`2×2` 亚像素超分置信度** | `0` = 单帧或零超分退化<br>`65535` = 四象限满覆盖高帧数真 `2×` 超分 | $0.55 \cdot Q_{2\times 2} + 0.45 \cdot \frac{1 + \sum w_n}{N}$，综合评估该像素是否同时踩满 `2×2` 四个亚像素相位象限以及有效叠加帧数占比。 |
| **`_field_focus.png`** | `B` | **光学对焦锐度场** | `0` = 焦外散景虚化 / 纯色区<br>`65535` = 镜头合焦面高频纹理 | 参考帧二阶拉普拉斯能量经单帧散粒噪声门控、分块包络平均与膨胀平滑后的光学合焦响应（即驱动 Stage 5 对焦耦合 RL 的同一张物理场）。 |

### 3.2 合并 RGB 模式（`--field-mode rgb`）直观色彩速查
若选择将三张场图打包为单张 `_field.png`（`R/G/B` 三通道），直接查看时的复合颜色对应关系如下：
- **青绿色（Cyan，低 `R` + 高 `G` + 高 `B`）**：**静态 + 满血真 2× 超分 + 精准合焦**（如静态建筑细节、岩石纹理、合焦静物）。
- **纯绿色（Green，低 `R` + 高 `G` + 低 `B`）**：**静态 + 多帧深度降噪 + 焦外虚化散景或平滑天空**（多帧信噪比增益已拉满，且 RL 反卷积已被自动压制保护散景奶油感）。
- **品红色 / 紫红色（Magenta，高 `R` + 低 `G` + 高 `B`）**：**合焦的局部运动主体**（如走动的人物、飞鸟，已自动切换至定格帧防重影保护并保留合焦锐度）。

### 3.3 三大实战工作流

#### 用法一：Photoshop / Affinity Photo 零手绘物理级图层蒙版
1. 将 `_field_motion.png`（或星野 `_field_sky.png`）直接粘贴到调整图层蒙版即可秒级分离动静主体或天地；
2. 将 `_field_focus.png` 载入蒙版并反相（`Cmd/Ctrl + I`），即可单独对焦外散景调色柔化。

#### 用法二：`Sci 科学` 模式训练 AI 超分 / 去噪模型的像素级 Loss 权重

```python
import cv2
import numpy as np
import torch

gt_rgb = cv2.cvtColor(cv2.imread("sample_nijinLBS.tif", cv2.IMREAD_UNCHANGED), cv2.COLOR_BGR2RGB)
f_motion = cv2.imread("sample_nijinLBS_field_motion.png", cv2.IMREAD_UNCHANGED)
f_phase  = cv2.imread("sample_nijinLBS_field_phase.png",  cv2.IMREAD_UNCHANGED)

gt = torch.from_numpy(gt_rgb.astype(np.float32) / 65535.0).permute(2, 0, 1)
w_phase  = torch.from_numpy(f_phase.astype(np.float32) / 65535.0)
w_static = 1.0 - torch.from_numpy(f_motion.astype(np.float32) / 65535.0)

pixel_weight = (w_phase * w_static).unsqueeze(0)
loss = (torch.abs(pred - gt) * pixel_weight).sum() / (pixel_weight.sum() * 3.0 + 1e-6)
```

#### 用法三：镜头光学素质与手持连拍体检
- 点开 **`_field_phase.png`** 检验快门速度与手持微抖动四象限覆盖情况；
- 点开 **`_field_focus.png`** 检视全开光圈下的镜头场曲与实际景深落点。

---

## 4. CPU / Metal / Vulkan 异构计算精度一致性

`nijinLBS` 的 Stage 4 与 Stage 5 同时实现了三套严格对齐的算子后端：
- **CPU 参考与 SIMD 后端**：`ARM64 NEON` 与 `x86_64 AVX2 + FMA` 矢量化实现；
- **macOS `Metal` 后端（`heraw_gpu_metal.mm`）**：基于 Apple Silicon `MTLResourceStorageModeShared` 统一内存架构实现零拷贝调度；
- **Windows `Vulkan` 后端（`heraw_gpu_vulkan.hpp` + `shaders/build_shaders.py`）**：内嵌 24 个预编译 Vulkan 1.0 `SPIR-V` 计算着色器，运行时动态绑定显卡驱动自带 `vulkan-1.dll`（零外部 SDK 依赖）。

三套后端共享完全相同的结构张量核矩阵、有理化 Anscombe GAT 参数、滑动 `8×8` DCT 频域收缩阈值、方差损失钳位（$\Delta v \le 1.25$）与对焦耦合防黑环 RL 迭代方程；同一组 45.7 MP Nikon Z 8 连拍在 CPU、Metal 与 Vulkan 后端输出的 16-bit 母版一致性实测达 **`PSNR > 82 dB`**（差异远低于 16-bit 最低有效位热噪声水平）。
