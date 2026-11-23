# `NijiN.LiteBurstSuite` (`nijinLBS`) Core Algorithm & Physical Field Maps Technical Reference

**[English](#nijinliteburstsuite-nijinlbs-core-algorithm--physical-field-maps-technical-reference) · [简体中文](#nijinliteburstsuite-nijinlbs-核心算法与物理场图技术文档)**

This document covers the Unified V3 (`M0–M5`) zero-hallucination multi-frame mathematical architecture alongside `v1.6.4` and `v2.x` Metal/Vulkan parity kernels. For CFA handling, tiled scheduling, IBP, temporal/optics module mapping, see [Implementation Map](docs/IMPLEMENTATION.md) and [Specification Index](docs/README.md). Outputs default to lossless DNG/TIFF, and physical fields default to split 16-bit grayscale PNGs with optional TIFF named extra channels.

---

## 1. Design Objective & Sensor Sampling Geometry

### 1.1 Pure Physical Statistical Modeling
On high-resolution OLPF-less cameras (such as 45.7 MP Nikon Z8/Z9 or Sony A7R bodies), lens optical cut-off frequencies exceed the single-frame Nyquist limit, creating **optical aliasing** and color moiré.

`nijinLBS` operates purely on **optical sampling geometry and Poisson-Gaussian photon statistics** with zero generative neural synthesis: natural handheld subpixel shifts or shutter micro-shake are de-aliased back onto a **`2×` super-resolution grid** (or enhanced at **`1×` True-Sampled RGB**), ensuring every restored detail traces directly to measured sensor photons.

- **Purely Mathematical**: 100% closed-form / iterative optical sampling and statistical inverse estimation without neural weights or external models.
- **Strictly Deterministic**: Bit-for-bit invariant output per backend (thread-count-invariant row-ordered reductions on CPU and coordinate-hashed `SplitMix64` TPDF dithering without RNG state or timestamps; cross-backend CPU/Metal/Vulkan differences stem solely from IEEE-754 FMA/SFU rounding, `PSNR > 82–103 dB`).
- **Zero Learned / External Priors**: Zero training datasets, zero learned generative priors, and zero external dictionaries; uses only sensor physics ($\text{Var}(I)=\sigma_0^2+gI$, CFA/OLPF geometry) and image-internal local analytical signal regularizers (chroma-difference smoothness, local color-line linearity, empirical DCT/wavelet Wiener shrinkage).
- **Zero Hallucination**: Unobserved subpixel phases or depth parallax remain flagged via `_field_phase` and gate confidence fields rather than synthesized.

### 1.2 Bayer CFA Density & Minimum Frame Gates
For raw sensor dimensions $H_1 \times W_1$ ($H_h \times W_h = \frac{H_1}{2} \times \frac{W_1}{2}$ `2×2` `RGGB` Bayer quads) and target `2×` grid $H_2 \times W_2 = 2H_1 \times 2W_1$:
- **Green luma carrier (`G1` + `G2`)**: Quincunx diagonal grid at `1/2` sensor density (`1/8` density per frame on the `2×` lattice).
- **Red/Blue chroma carriers (`R` / `B`)**: Each at `1/4` sensor density (`1/16` density per frame on the `2×` lattice).
- **Algebraic & robustness gates**: Solving all four `2×2` subpixel phase quadrants $\{(0,0), (0,1), (1,0), (1,1)\}$ requires an algebraic minimum of **$N = 4$ frames** (`--policy force-all`), becomes over-determined for M-estimator outlier rejection and quarter-density `R/B` chroma at **$N = 5$ frames** (default `2×` SR gate; `1×` True-RGB minimum is **$N = 3$ frames**), and reaches its quality sweet spot at **$N = 6\text{–}8$ frames** (`80%–92%` subpixel quadrant fill and `+7.6 ~ +8.8 dB` SNR gain).

---

## 2. Six-Stage Core Physical Pipeline (Stage 0 – Stage 6)

```text
RAW Burst Sequence (.NEF / .NEV / .jxs / .DNG / .ARW / .CR3 / .RAF / .RW2 via nijinWVDC + LibRaw)
  │
  ▼ [Stage 0] Fast Header Scan · Full-ISO Black-Frame Filter · 5D Scene Diagnosis & ±4 EV AEB Bracket Recognition
  ▼ [Stage 1] Signed Pedestal G1/G2 Blind Bias Calibration · Cross-CFA Anti-Star-Eater Hot-Pixel Gate
  ▼ [Stage 2] Blind Poisson-Gaussian + Wavelet Quantization Calibration · Crop-Aware Temporal NNLS Refinement
  ▼ [Stage 3] Multi-Scale ZNCC Flow / Astro Dual-Domain Alignment · Anti-Hollow Deghosting · 3-Gate Rigid Patches
  ▼ [Stage 4] Unified V3 Anisotropic Green + 1x Chroma Huber Pyramid · ±4 EV AEB Native-Variance GLS [CPU / Metal / Vulkan]
  ▼ [Stage 5] Exact-Integral NLT-VST · 64×64 COLA 2D FFT 4-Octave SURE · Morozov M5-A GLS + 3×3 Color-Jacobian σ₂ · M5-B FFT Deconv
  ▼ [Stage 6] Color Matrix · Adaptive Tone · Signed DNG (BlackLevel=2048) / Lossless & HQ TIFF · Self-Eval Metrology (`--eval-self`) · EXIF/GPS
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
   - **2D Constructive Optical Peaks (`v3.0.0-preview2` 6-Tap Lagrange LOO Predictor)**: Candidates without a 1D ridge are tested along both horizontal and vertical same-color sublattice axes via the exact 5th-degree 6-tap central Lagrange leave-one-out predictor $P(q) = \frac{1}{20}[15(q_{-1}+q_{+1}) - 6(q_{-2}+q_{+2}) + (q_{-3}+q_{+3})]$, preserving smooth 2D interference maxima (`96.2%–98.7%`) while still rejecting isolated single-diode hot pixels (`S05`);
   - **Single-diode hot pixels / cosmic rays**: Isolated spike with background-level orthogonal neighbors → repaired symmetrically (fixed warm pixels appearing across $\ge \lceil(N+1)/2\rceil$ frames are cleaned across the whole burst).

---

### Stage 2: Blind Poisson-Gaussian & Temporal NNLS Noise Calibration

Linear sensor variance follows the affine photon + readout model plus optional lossy wavelet quantization variance:
$$\text{Var}(I) = \sigma_0^2 + g \cdot I + q_{\text{wv}} \cdot s_{\text{NLT}}^2(I)$$
Log-luminance binning extracts flat micro-patches to blindly regress readout variance $\sigma_0^2$ and shot gain $g$, then performs an adaptive-stride **inter-frame temporal-difference NNLS calibration** in Stage 3 after alignment (`stride = 1` on compact `< 0.5 MP` crops, `stride = 4` full-frame)—removing spatial-texture overestimate bias on small crops and spatial correlation bias in lossy compressed RAWs (e.g., Nikon `HE` / `HE*`).

---

### Stage 3: Homography Pre-Warp, Pyramid Tangential Trust-Region Flow, Full-GPU Sensor Cleanup & Rigid Micro-Patches

1. **Coarse-to-fine ZNCC parabolic subpixel flow with `Hm` pre-warp & tangential trust-region projection**:
   - Gaussian pyramid ZNCC matching (invariant to minor exposure jitter) with 2D parabolic subpixel fit down to `0.01` pixel accuracy; when global homography shear exceeds `0.003`, pre-warps using integer `CurIntegral` (`sat_hom`, `rad=4`) so coarse-level patches stay sharp without moiré warp bias.
   - Refines flow increments via `2×2` structure-tensor **tangential trust-region projection** + `median3x3_plane` cleanup, suppressing spurious orthogonal slip on clean edges while preserving subpixel motion.
2. **`Astro` dual-domain celestial + horizon alignment**:
   - **Sky domain**: Outliers-rejected least squares fit of **6-term 2nd-order polynomial** star flow:
     $$\Delta x(x,y) = a_0 + a_1 x + a_2 y + a_3 x^2 + a_4 xy + a_5 y^2, \quad \Delta y(x,y) = b_0 + b_1 x + b_2 y + b_3 x^2 + b_4 xy + b_5 y^2$$
   - **Horizon fusion**: Segments sky vs foreground ridges from star density and dual-flow residuals to lock pinpoint stars on the polynomial field and landscape on ZNCC optical flow.
3. **Full-GPU `C13` sensor residual cleanup (`run_silicon_cleanup_gpu`, 6 kernels)**:
   - Evaluates consensus scene reconstruction across aligned RAW frames, extracts `4`-phase radix-sorted row/column pattern residuals (`k_c13_med_4ph` + Gaussian-`97` high-pass `k_c13_hp97`), detects consensus warm pixels (`k_c13_hot_map`), and applies deterministic native RAW pattern removal in Stage 3 (`1.28s`/`1.32s` GPU vs `2.28s`/`2.19s` CPU at 45.7 MP).
4. **Dual-scale P-G Z-score deghosting, Anti-Hollow Closing, 3-Gate Rigid Patches & Temporal Gradient Coherence**:
   - Evaluates fine ($\sigma = 1.4$) and wide ($\sigma = 4.0$) scales with `1/4`-scale morphological closing and `160`-px cosine taper `m_wide`; restores rigid local patches passing **(1) `ZNCC >= 0.90`, (2) fine P-G residual, (3) wide P-G residual**; accumulates phase coherence $C \in [0, 1]$ from temporal vector gradient conservation.

---

### Stage 4: Curvature-Modulated Structure-Tensor Green Fusion, `±4 EV` AEB GLS & `1x` Huber Chroma Pyramid

1. **Smooth structure tensor, 4-DOF normal curvature & adaptive elliptical kernel (`2×` / `1×` Unified)**:
   - Computes $J = G_\rho * (\nabla I \nabla I^\top)$ eigenvectors, dominant tangent angle $\theta = \frac{1}{2}\arctan2(2J_{xy}, J_{xx}-J_{yy})$, and SNR-gated anisotropy $A \in [0, 1]$, stretching $\sigma_\parallel \to 0.64$ along edges and compressing $\sigma_\perp \to 0.28$ across edges, modulated by 4-DOF second-order normal curvature to stay tight around corners and fine line tips. For `2×` green reconstruction, a single-pass `2×2` subpixel batch evaluates all four quads simultaneously (`NEON` / `AVX2` / GPU) and outputs direct V3 `global_eta` without redundant post-pass recomputation.
2. **Automatic `±4 EV` AEB native-variance GLS weighting & raised-cosine highlight taper**: Whenever burst exposures vary (EXIF EV span or scene-median ratio), per-frame native measurement noise weights combine inverse variance with relative exposure ratio $e_n$:
   $$w_n^{\text{GLS}}(y) = \frac{e_n^2}{\sigma_{0,n}^2 + g_n \cdot \max(0, y / e_n) + \sigma_{q,n}^2(y / e_n)} \cdot \tau_{\text{sat}}(y_n)$$
   where $\tau_{\text{sat}}$ smoothly tapers frames entering `[0.82, 0.99] * white_n` via raised cosine, extracting highlight headroom from `-4 EV` brackets and deep shadows from `+4 EV` brackets seamlessly in linear space while keeping exact noise variance tracking $\eta_{\text{eff}} = \langle \sum \alpha_k^2 / (\sum \alpha_k)^2 \rangle$.
3. **`1×` sensor-grid chroma fusion + `L=3` odd-symmetric Huber pyramid**: Computes white-balanced $\Delta R = w_R R - G$, $\Delta B = w_B B - G$ directly on the `1×` native chroma carrier grid (cutting `2×` chroma fusion memory bandwidth by `4×`), smooths across `L = 3` odd-symmetric luma-edge-stopped Huber bilateral scales, and upsamples `2×` via signed dyadic interpolation before adding high-resolution $G_{\text{sr}}$, eliminating demosaicing zippering and false color.

---

### Stage 5: Exact-Integral NLT-VST · Parseval `64×64` SURE · `M5-A` Schur-Complement GLS + Color-Jacobian $\sigma_2$ · `M5-B` R2C FFT Deconv

1. **16 KB exact-integral Nonlinear Variance-Stabilizing Transform (NLT-VST, `CPU / Metal / Vulkan` Unified V3)**: Integrates $z(u) = \int_{u_{\min}}^{u} \frac{\mathrm{d}s}{\sqrt{\sigma_{\text{eff}}^2 + g_{\text{eff}} s_+ + \eta_q q_{\text{row}} s_{\text{NLT}}^2(s_+)}}$ via 4-point Gauss-Legendre quadrature over 16 rows (`16 KB` piecewise LUTs with analytic linear tails and $\sigma^2$-derivative Stein correction across all backends; legacy `math_v3_unified == false` retains rationalized Anscombe GAT parity). Negative linear black-level tails stay strictly signed throughout Stages 4–5.
2. **`64×64` COLA zero-mean 2D FFT Parseval-whitened 4-octave SURE + `tile_dv` energy conservation**: Transforms 50%-overlapping sine-windowed `64×64` tiles into frequency domain after subtracting each tile's COLA-weighted DC mean, normalizes bins by the Parseval-scaled splat PSD $\psi(\omega)$ with grain floor `g_floor` `[0.04, 0.36]`, applies closed-form 4-octave SURE shrinkage (`oct 0..3`), and tracks exact per-tile variance conservation `tile_dv` into inverse VST (raising real Z8 noise whiteness SFM from `0.440 -> 0.608` at `1×` and `0.330 -> 0.540` at `2×`).
3. **Per-tile DOF Morozov Forward GLS with Schur-complement opponent preconditioner, optical PSF blur & Color-Jacobian $\sigma_2(\mathbf{J}_{\mathbf{x}})$ (`M5-A`, `CPU / Metal / Vulkan`)**: Evaluates forward sensor projection residuals on native RAW frames (`scale=1` or `scale=2`) including optical PSF convolution (`inv_blur`, `ois` chromatic shifts, per-frame sharpness weights). Active tiles solve a closed-form **`3×3` bilateral Schur-complement opponent-chroma preconditioner** across `dCr_bar` / `dCb_bar` + `dG`, gated by in-sample Morozov threshold $\tau = \frac{N-1}{N} \tau_0$. Unobserved Fisher-complement pixels $(1 - \phi_{\text{Fisher}})$ receive firm-non-expansive closed-form `3×3` Color-Jacobian $\sigma_2(\mathbf{J}_{\mathbf{x}})$ orthogonal opponent shrinkage leaving `G` 100% invariant (cutting real Z8 `2×` chroma aliasing from `1.524` to `0.447`, **`3.41×`** cleaner).
4. **Single-pass `64×64` R2C/packed 2D FFT 3-channel + OLPF optical deconvolution (`M5-B`, `CPU / Metal / Vulkan`)**: Executes spectral Wiener + measured `3`-channel optical PSF + OLPF kernel inversion on `64×64` (`40×40` core + `12`-px halo) tiles (shared-memory packed `R2C` on Metal/Vulkan; packed `R+iB` on CPU), focus/noise gated and clamped to `5×5` local extrema (`[mn - 0.012*span - 1.5*sig, mx + 0.012*span + 1.5*sig]`, max boost `1 + 0.24*sqrt(iters)*str`), compressing 45.7 MP deconvolution from pre-V3 `34.80s` / `171.76s` down to **`0.157s` (`1×`, `222×` faster) / `0.474s` (`2×`, `362×` faster)** on GPU Metal (`2.38e-7` parity vs CPU).

---

### Stage 6: Signed DNG (`BlackLevel=2048`), Forward-Optics `EvalSelf` & L2-Fused Parallel TIFF Export

1. **Signed integer DNG shadow linearity (`BlackLevel = 2048`)**: Integer 16-bit DNG exports (`dng`, `dng-raw`, `dng-hq`) reserve `BlackLevel = 2048` digital pedestal so zero-mean deep-shadow Gaussian read noise remains signed and unbiased around zero linear light (`[-2048/63487, +1.0]`).
2. **Parallelized forward-optics self-evaluation metrology (`--eval-self [FILE]`)**: Reports 6 deterministic, inverse-crime-free burst diagnostics in stderr and JSON using the forward optical PSF + `ois` + sharpness model across worker threads (`0.43s` at `1×` / `1.05s` at `2×` on 6F Z8): held-out frame **stratified LOO $\chi^2$** (`flat=1.012` at `1×`, `1.007` at `2×`), **disjoint half-burst FRC**, **subpixel phase-diversity determinant**, **Brillouin-zone Welch noise SFM**, **achromatic chroma-aliasing ratio**, and **slanted-edge eSFR**.
3. **L2-fused `row_gen` strip export (`heraw_exif_io.hpp`) + Color matrix & TPDF dither**: Transforms to `D65 sRGB` or `ISO 12640-2 Adobe RGB (1998)` (embedded ICC v2.4), applies preset auto-exposure / shadow-anchored `asinh` (Astro) / hue-preserving `tanh` rolloff + `SplitMix64` TPDF dither directly inside cache-hot `row_gen` strips (eliminating redundant full-frame `u16` buffer allocation when `cam_flip == 0`, reusing per-worker `z_stream` via `deflateReset`, and issuing parallel POSIX `pwrite` for payloads `>= 64 MB`), exporting `16512×11008` `16-bit` Deflate TIFF (`TIFF Tag 317 Predictor=2 + Deflate`, `0.39×`), `tiff-hq`, `dng SOF3`, or `dng-hq` (`rgb-transform-quantize = 0.0011s`).

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

## 4. Unified V3 Multi-Backend Engine & Cross-Backend Parity (`CPU / Metal / Vulkan`)

Unified V3 (`M0–M5` + full-GPU `C13` + `M5-A`/`M5-B` + fused Stage 6) runs natively across **`CPU`** (`ARM64 NEON` / `x86_64 AVX2+FMA`), **macOS `Metal`** (`49` native MSL kernels), and **Windows/Linux `Vulkan 1.0`** (`49` embedded SPIR-V shaders) for both `1×` and `2×` reconstruction:
- **`C13` Sensor Cleanup (`6` kernels)**: Scene consensus median + radix-sorted `4`-phase row/column pattern removal + warm-pixel map (`1.28s`/`1.32s` GPU vs `2.28s`/`2.19s` CPU at 45.7 MP, `2.98e-8` parity).
- **Stage 4+5 V3 (`12` kernels)**: Curvature-modulated structure-tensor green fusion (`±4 EV` AEB GLS) + direct `global_eta`, `1×` `L=3` odd-symmetric Huber chroma pyramid, 16-band exact-integral NLT-VST, shared-memory `64×64` Hermitian half-plane COLA 2D FFT 4-octave SURE with `tile_dv` energy conservation (`~8e-6` Stage 4+5 MAE vs CPU), and `fisher_comp_out` export.
- **`M5-A` & `M5-B` (`6` kernels)**: `M5-A` Morozov GLS with `3×3` bilateral Schur-complement opponent preconditioner + optical PSF blur (`1.22s`/`2.75s` GPU vs `2.77s`/`5.10s` CPU) and `3×3` Color-Jacobian $\sigma_2$; `M5-B` packed `R2C` `64×64` (`40×40` core + `12`-px halo) 2D FFT optical deconvolution (`0.157s`/`0.474s` GPU vs `0.676s`/`2.443s` CPU, `2.38e-7` parity).
- **45.7 MP 5-frame Nikon Z8 benchmarks (Apple M1 Max, end-to-end wall)**:
  - **`1×` (`8256×5504`)**: GPU `8.45s` (`5.89×` faster than pre-V3 CPU `49.76s`); V3 CPU `19.98s`; GPU/CPU 16-bit TIFF parity **`PSNR = 71.02 dB`**.
  - **`2×` (`16512×11008`)**: GPU `13.30s` (`14.41×` faster than pre-V3 CPU `191.68s`); V3 CPU `30.19s`; GPU/CPU 16-bit TIFF parity **`PSNR = 66.03 dB`**.
  - Legacy `math_v3_unified == false` GPU kernels keep `PSNR > 82 dB` bit-exact regression compatibility.

### 4.1 Unified V3 Depth Metrology, Epipolar Light-Field & Closed-Loop Reconstruction
- **3-DOF Epipole & Homography Slanted-Plane De-Tilting (`D-1`)**: Estimates $(b_y, b_x, b_z)$ epipolar fields for calibrated lenses (`focal_length_mm > 0.5`), recovering axial push-in parallax, and regresses rotation- and breathing-invariant shear residuals from $K^{-1} H_k K$ to restore slanted reference planes.
- **1D Half-Set Occlusion Refinement & Subspace Motion-Parallax F-Test (`D-2`)**: Separates independent scene velocity from parallax on non-collinear timestamp/baseline subspaces and refines depth discontinuities along the better half-burst $\min(\Phi_{\mathcal{F}_-}, \Phi_{\mathcal{F}_+})$.
- **Tri-Modal Sign & Metric Diopter Calibration (`D-3`)**: Orients near/far polarity via kinetic occlusion crescent asymmetry, thin-lens CoC V-curve fitting $w^2(\rho) = a_0 + a_1(\rho - \rho_f)^2$ (calibrating metric baseline $\text{mm}$ and diopter span $\text{m}^{-1}$ when EXIF aperture/focus distance are valid), and axial LoCA/defocus slope cues.
- **3-Channel Opponent-RGB Thin-Plate Multigrid & Closed-Loop `[C4]` Unpeeling (`D-4`, `D-5`)**: Solves unclipped relative depth with `(G, R-G, B-G)` bilateral boundaries plus interior biharmonic $\nabla^2\rho=0$ smoothness, and feeds clean epipolar flows + unoccluded `mw_half` frame recovery directly into Stage 4+5.
- **Physical Aperture Bokeh, Multi-Regime Optical Refocusing, Two-Layer WebGL2 Inspector & Parabolic Focus Stacking (`D-6`, `heraw_suite_refocus.hpp`)**:
  - Replaces continuous-surface multiplicative Porter-Duff seams with sheet-clustered additive partition-of-unity linear tents ($\sum_s w_s \equiv 1$, zero constant-plane residual) and per-silhouette `revealed_bg_depth`;
  - Evaluates `$O(2R+1)$` 2nd-order scanline moment prefix sums (`P0, P1, P2`) with sub-pixel chord anti-aliasing, signed primary Zernike spherical aberration (`--bokeh-sa`), STF radial apodization (`--bokeh-stf`), rotated polygonal diaphragm blades, Scheimpflug focal plane tilt (`--tilt-plane`), native CoC quadrature (`--virtual-fstop`), PSF-clipped glint restoration, `64×64` COLA Bessel-$J_1$ shift-focus deconvolution, 4D Voronoi synthetic-aperture see-through refocusing (`--refocus-mode synthetic-aperture`), and 3-point parabolic `dff_depth`.
- **High-Frame-Count (`N = 30..512`) Streaming, `[C13-DustPRNU]` & `[C3-IsoLucky]`**:
  - Defaults `max_cluster_frames = 128` (`[8..512]`), runs bi-directional anchor-centered geodesic chain flow + $O(H_h W_h)$ bounded 2-pass Stage 3 deghosting (`N > 16`), automatic `tile_size_1x = 1040` Stage 4+5 tiling (`N > 16`), and full-rank `M5-A` GLS iterations (`N >= 28`);
  - Performs blind silicon dust & PRNU flat-field self-calibration (`[C13-DustPRNU]` when `N >= 10` and drift $\ge 12$ px) and telephoto `64×64` patch-wise isoplanatic lucky imaging (`[C3-IsoLucky]`).
- **Camera-Burst Subject Observer Matrix (`O1..O5`, `heraw_suite_observer.hpp`, `org.nijin.observer.v3`)**:
  - `O1`: $\mathbb{P}^3$ homogeneous color-line bundle intersection (illuminant $\hat{\mathbf{c}}_{\text{illum}}$, intrinsic diffuse `--emit-diffuse`, `metalness`, Koschmieder `veil_transmission`, parallax half-angle microfacet `roughness`);
  - `O2`: Rolling-shutter / temporal Lomb-Scargle optical lock-in (`100/120 Hz` & `120–2000 Hz` PWM, `ac_light_fraction`, `--emit-light-split`, red-vs-blue `phosphor_decay` lifetime [ms]);
  - `O3`: Moving rigid-body $SE(3)$ self-stereo (`moving_body_depth_half`), velocity-gradient kinematics (`ttc`, `vorticity`, `nonrigid_strain`), exposure-duty tachometry, and ballistic $g_0 = 9.80665\,\text{m/s}^2$ metric ranging;
  - `O4`: Helmholtz-gated quantitative Background-Oriented Schlieren (`schlieren_opd`) via 2D FFT Neumann Poisson solver (irrotational ratio $\ge 0.72$);
  - `O5`: Phase-uncoupled rolling-shutter Rank-2 modal vibrometry (`vibration_amp`, $f_0$, modal damping $\zeta$) + long-burst 14-bit $\mathbb{P}^3$-orthogonal vital perfusion (`vital_perfusion`, `BPM`, BCG).
- **V3.0.0 Native Container, Spectral-Probit Solvers, Optical Glass Cascades, Computational Shutter & Extended Observers (`Q1–Q10`, `R1–R12`, `F-A..F-H`)**:
  - **DxO ONE `.DXO`/`.4RW` Native Multi-IFD Ingestion & `P0-CLK` Rational Clock Provenance**: Parses multi-IFD CFA bursts (default 4-frame `2×` gate on native `.DXO` with Sony `IMX183CQJ-J` `12.1667 us/row` calibration) and variable-length DNG `LinearizationTable` (`50712`, clamped tails + exact quantization variance), retaining full `pts_ticks / timescale` sub-millisecond timing across `.NEV`/WVDC/LibRaw into `obs.json` (`clock_source`, `clock_tick_num`, `clock_tick_den`, `clock_row_calibrated`).
  - **Multi-Part Tiled OpenEXR 2.0 & Memory-Bounded Stripe Streaming (`F-E / F-A`)**: Dependency-free `zlib` OpenEXR 2.0 reader/writer emitting `beauty`, `fg` (premultiplied RGBA), `bg`, `scatter`, and `depth` (`half` or `float`, preserving negative linear noise) alongside two-pass sufficient-statistics stripe streaming (`--streaming`, `--mem-budget-gb`).
  - **Spectral `K(x)`, Probit Codec/Saturation & Continuous `1×` Tiers (`Q1–Q10`)**: Convex B-spline resolution operator $K(x)$ with single-use orthogonal budget across Stage 4/5/`M5-A`; exact box Probit likelihood with biorthogonal adjoint $\widetilde{L}^*$; upper-tail survival saturation `probit`; `--rgb-1x-tier <fast|sinc|joint>` with deterministic $\nu_r$ residual stopping; OkLab hue-preserving stage LED compression (`--highlight-mode oklab-stage`); and Fisher optimal next-shot planner (`--next-shot-plan`).
  - **Linear Optical Glass Diffusion, Dual-Motion $SO(3)$ Shutter & `O6–O10` (`§4–§7`)**: Linear-light optical cascades (`hdf`, `black-mist`, zero-halo pyramid `ultra-contrast`, `halation`, `uc+*`) with physical baffle polarity (`hooded < standard < wide-flare`); Schur-projected `SIM(2)` + 8-node row-timed $SO(3)$ rolling shutter + `F-C` computational shutters + `F-H` dual-window sinc lock-in + `§7.4` 3-hypothesis astro chirp/streak selector + clipped 2D Voronoi synthetic aperture (`--aperture-weight voronoi|dwell`) + `O6–O10` (`loca_sign`, `modal_wave`, `seeing`, `refraction`, `streak_astrometry`).

---

# `NijiN.LiteBurstSuite` (`nijinLBS`) 核心算法与物理场图技术文档

本文涵盖 Unified V3（`M0–M5`）零幻觉多帧统一数学重构管线与 `v1.6.4`/`v2.x` Metal/Vulkan 异构算子基准。Suite v2 的 CFA、分块调度、回投、时空与光学模块映射见 [实现说明](docs/IMPLEMENTATION.md)，设计分册见 [规格索引](docs/README.md)。输出默认采用无损 DNG/TIFF，场图默认独立灰度并支持 TIFF 命名通道。

---

## 1. 设计目标与传感器采样几何

### 1.1 纯物理统计建模原则
高像素无光学低通滤镜（OLPF-less）机身（如 45.7 MP Nikon Z 8 / Z 9、Sony A7R 系列等）的镜头光学截止频率往往高于传感器单帧奈奎斯特采样频率（Nyquist Frequency）。单帧拜耳（Bayer CFA）采样因此存在显著的**光学混叠（Aliasing）**与彩色摩尔纹。

`nijinLBS` 完全基于**光学采样几何与光子泊松-高斯统计建模**（零生成式神经网络脑补），利用手持连拍或快门微震带来的天然亚像素位移，将散落在连续帧上的混叠高频信号解混叠（De-aliasing）回 **`2×` 超分网格**（或 **`1×` 真采样 RGB**），保证每一处恢复出的纹理均源于真实的传感器光子测量。

- **纯数学建模**：100% 基于显式光学采样与统计反演计算，无神经网络权重或外部模型。
- **严格确定性**：固定输入与计算后端下输出逐比特可复现（CPU 并行归约按行固定累加顺序、与线程数和调度顺序无关，TPDF 抖动使用像素坐标 `SplitMix64` 确定性哈希；跨 CPU/Metal/Vulkan 仅由 IEEE-754 FMA/SFU 浮点舍入带来极微差异，`PSNR > 82–103 dB`）。
- **无训练/外部先验**：无训练数据集、无学习分布先验与外部字典；仅依托传感器第一性物理模型（$\text{Var}(I)=\sigma_0^2+gI$、CFA/OLPF 采样几何）与局部解析信号正则化（色差平滑性、局部色线模型、经验 DCT/小波 Wiener 收缩）。
- **零生成式幻觉**：当亚像素相位覆盖或极线视差物理不可观测时，经 `_field_phase` 与有效门控如实报告，绝不脑补合成虚构高频结构。

### 1.2 拜耳 CFA 密度与最低超分帧数门槛
设原始传感器分辨率为 $H_1 \times W_1$（包含 $H_h \times W_h = \frac{H_1}{2} \times \frac{W_1}{2}$ 个 `2×2` `RGGB` 拜耳单元），目标 `2×` 超分网格分辨率为 $H_2 \times W_2 = 2H_1 \times 2W_1$：
- **绿色亮度载波（`G1` + `G2`）**：在 `1×` 传感器上呈对角梅花阵（Quincunx）排布，占 `1/2` 像素密度；投影到 `2×` 网格后，每帧提供 `1/8` 密度的真实采样。
- **红/蓝色度载波（`R` / `B`）**：在 `1×` 传感器上各占 `1/4` 像素密度；投影到 `2×` 网格后，每帧提供 `1/16` 密度采样。
- **张数代数门槛**：要完整解出 `1×` 网格内 `2×2` 四个亚像素相位象限 $\{(0,0), (0,1), (1,0), (1,1)\}$ 的独立观测值，最少需要 **$N = 4$ 帧**；当 **$N = 5$ 帧**时，抗差 M-estimator 与 `R/B` 四分之一密度载波进入超定（Over-determined）状态（现行默认 `2×` 门槛 `>=5` 张，`1×` 真彩门槛 `>=3` 张）；**$N = 6\text{–}8$ 帧**为兼顾亚像素象限覆盖率（`80%–92%`）、坏帧剔除冗余与信噪比增益（`+7.6 ~ +8.8 dB`）的最佳甜点区间。

---

## 2. 六阶段核心算法管线（Stage 0 – Stage 6）

```text
RAW 连拍序列 (.NEF / .NEV / .jxs / .DNG / .ARW / .CR3 / .RAF / .RW2，由 nijinWVDC + LibRaw 解码)
  │
  ▼ [Stage 0] 快速头扫描 · 跨全 ISO 黑场过滤 · 五维场景诊断与 ±4 EV AEB 包围曝光识别
  ▼ [Stage 1] 符号化黑电平基底 (BlackLevel=2048) G1/G2 盲暗校准 · 跨通道“防吞星”热噪修复
  ▼ [Stage 2] 盲泊松-高斯 + 小波量化方差标定 · 小画幅自适应步长时域 NNLS 二次修正
  ▼ [Stage 3] 多尺度 ZNCC 亚像素光流 / 星野天地双域配准 · 防镂空去鬼影 · 三门控刚体微块捞取
  ▼ [Stage 4] Unified V3 各向异性双绿 + 1× 色度 Huber 金字塔 · ±4 EV AEB 原生方差 GLS  [CPU / Metal / Vulkan]
  ▼ [Stage 5] 16 KB 精确积分 NLT-VST · 64×64 COLA 2D FFT 四倍频程 SURE · Morozov M5-A + 3×3 色雅可比 σ₂ · M5-B FFT 反卷积
  ▼ [Stage 6] 色彩矩阵 · 自适应影调 · 符号化 DNG / 无损与 HQ TIFF · 自评估计量 (`--eval-self`) · EXIF/GPS 封装
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
   - **二维相长干涉光学峰（`v3.0.0-preview2` 六抽头 Lagrange 留一预测器）**：对无一维脊线的候选极值点，在同色子晶格水平/垂直两轴分别执行五次多项式精确六抽头对称留一预测 $P(q) = \frac{1}{20}[15(q_{-1}+q_{+1}) - 6(q_{-2}+q_{+2}) + (q_{-3}+q_{+3})]$，精准保留光滑二维干涉峰（`96.2%–98.7%`）并继续剔除单光电二极管电子热噪；
   - **传感器单光电二极管热噪点 / 宇宙射线**：仅单个像素电荷异常，正交邻居仍处于背景噪声水平，精准判定为热噪并以空间中值修复（跨 $\ge \lceil(N+1)/2\rceil$ 帧持续出现的固定热噪同步全组清除）。

---

### Stage 2：盲泊松-高斯噪声与自适应时域 NNLS 自标定

CMOS 传感器的线性信号噪声方差服从光子散粒噪声（Poisson）、电路读出噪声（Gaussian）及可选有损小波编码量化方差的仿射复合模型：
$$\text{Var}(I) = \sigma_0^2 + g \cdot I + q_{\text{wv}} \cdot s_{\text{NLT}}^2(I)$$
引擎在亮度分位中选取平坦微块拟合单帧 readout $\sigma_0^2$ 与 shot $g$，并在 Stage 3 对齐后对静态像素执行自适应步长**跨帧时域差分 NNLS 二次标定**（`< 0.5 MP` 紧凑裁切取 `stride = 1`，整帧取 `stride = 4`），直接以帧间独立散粒差消除小裁切场景纹理高估以及有损 RAW（如 Nikon `HE`/`HE*`）机内平滑导致的空间低估。

---

### Stage 3：`Hm` 单应性预扭曲、切向置信域光流、全 GPU 硅基残差清除与刚体微块超分

1. **`Hm` 单应性预扭曲与结构张量 `2×2` 切向置信域投影光流**：
   对每帧半尺寸双绿图构建高斯金字塔并执行**零均值归一化互相关（ZNCC）**抛物面亚像素拟合（`0.01` 像素精度）；当全局单应性剪切超过 `0.003` 时，利用积分图 `CurIntegral`（`sat_hom`, `rad=4`）预先扭曲特征块消除摩尔纹透视偏移；随后沿 `2×2` 结构张量切向投影光流增量并施以 `3×3` 中值净化，在锐利边缘守住亚像素跟踪精度。
2. **`Astro` 手持星野天地双域独立配准（Dual-Domain Celestial + Horizon Alignment）**：
   手持无赤道仪拍摄带地景的星空时，地球自转叠加广角镜头透视畸变使星空与地面山脊遵循两套截然不同的运动场：
   - **天球域**：提取各帧星点亚像素质心，以抗外点最小二乘拟合 **6 项二阶多项式流场**：
     $$\Delta x(x,y) = a_0 + a_1 x + a_2 y + a_3 x^2 + a_4 xy + a_5 y^2, \quad \Delta y(x,y) = b_0 + b_1 x + b_2 y + b_3 x^2 + b_4 xy + b_5 y^2$$
     二阶交叉项精确吸收超广角镜头中心与四角不同的星轨投影角速度；
   - **地景域与天际线融合**：根据星点空间分布密度与双流场对齐残差自适应分割天空与地景蒙版，天空锁定星点多项式场以聚合针尖星芒，地面锁定金字塔光流以保持山脊建筑清晰。
3. **全 GPU `C13` 硅基传感器残差清除（`run_silicon_cleanup_gpu`，`6` 个核函数）**：
   在 GPU（Metal / Vulkan）并行执行对齐栈中位场景估算、基数排序 `4` 相位行列条纹残差提取（`k_c13_med_4ph` + `97`-tap 高斯高通 `k_c13_hp97`）与跨帧共识温点定位（`k_c13_hot_map`），直接在原生拜耳坐标扣除固有读出图案噪声与固定热点（45.7 MP 实测耗时从 `2.28s`/`2.19s` 降至 `1.28s`/`1.32s`，与 CPU 偏差 `2.98e-8`）。
4. **双尺度防镂空去鬼影、三门控刚体微块捞取与时域梯度相干场**：
   以细/宽双尺度泊松-高斯 Z-score 配合 `1/4` 尺度形态学闭包填实动体内部空洞（`160`-px 余弦过渡带）；对同时通过 **(1) `ZNCC >= 0.90`、(2) 细尺度 P-G 残差、(3) 宽尺度 P-G 残差** 的局部刚体恢复多帧亚像素超分，非刚性目标平滑回退干净拜耳引导的 Hamilton-Adams 插值；矢量累加多帧梯度生成相干度场 $C \in [0, 1]$ 在频域定向保护微弱细节。

---

### Stage 4：四自由度曲率调制结构张量双绿融合、`±4 EV` AEB GLS 与 `1×` Huber 色度金字塔

1. **四自由度法向曲率调制结构张量椭圆核（`2×` / `1×` Unified）**：
   计算平滑结构张量 $J = G_\rho * (\nabla I \nabla I^\top)$ 的主特征方向与信噪比门控各向异性度 $A \in [0,1]$，沿边拉伸 $\sigma_\parallel \to 0.64$ 并沿法向压缩 $\sigma_\perp \to 0.28$，结合局部二阶法向曲率（`4-DOF`）在转角与细线尖端自动收敛核宽；`2×` 绿色重建在单一批处理核内同步聚合 `2×2` 四个亚像素象限（`NEON` / `AVX2` / GPU），并直接导出 V3 `global_eta` 消除二次遍历开销。
2. **`±4 EV` 包围曝光（AEB）原生方差广义最小二乘（GLS）权重与高光余弦包络**：
   当序列内帧间曝光存在差异（EXIF EV 跨度或景物亮度中位比触发）时，按每帧原生传感器方差叠加相对曝光比 $e_n$ 分配精确最优 GLS 权重：
   $$w_n^{\text{GLS}}(y) = \frac{e_n^2}{\sigma_{0,n}^2 + g_n \cdot \max(0, y / e_n) + \sigma_{q,n}^2(y / e_n)} \cdot \tau_{\text{sat}}(y_n)$$
   当某帧局部接近满阱（`[0.82, 0.99] · white_n`），升余弦饱和过渡权重 $\tau_{\text{sat}}$ 自动平滑接力切换至欠曝 `-4 EV` 帧守住高光细节，同时从 `+4 EV` 帧汲取高信噪比暗部光子并精确追踪有效噪声比 $\eta_{\text{eff}} = \langle \sum \alpha_k^2 / (\sum \alpha_k)^2 \rangle$。
3. **`1×` 传感器网格色度融合 + `L=3` 奇对称亮度边界 Huber 金字塔**：
   直接在 `1×` 原始载波网格聚合白平衡色差残差 $\Delta R = w_R R - G,\; \Delta B = w_B B - G$（内存带宽降低为 `1/4`），经 3 层奇对称亮度边停 Huber 双边滤波后双进符号化重采样至 `2×` 并回加高清亮度 $G_{\text{sr}}$，从数学结构根除边缘锯齿与彩色摩尔伪影。

---

### Stage 5：16 KB NLT-VST · Parseval 能量守恒 `64×64` SURE · `M5-A` Schur 补迭代 + 色雅可比 $\sigma_2$ · `M5-B` R2C FFT 反卷积

1. **16 KB 精确积分非线性方差稳定变换（NLT-VST，`CPU / Metal / Vulkan` Unified V3）**：
   沿 16 行条带用 4 点 Gauss-Legendre 求积构造精确片上查找表：
   $$z(u) = \int_{u_{\min}}^{u} \frac{\mathrm{d}s}{\sqrt{\sigma_{\text{eff}}^2 + g_{\text{eff}} s_+ + \eta_q q_{\text{row}} s_{\text{NLT}}^2(s_+)}}$$
   配合端点线性解析延拓与 $\sigma^2$ 一阶导数 Stein 偏置补偿，三端统一将泊松-高斯与非线性量化异方差噪声精确归零偏归一化为单位方差；Stage 4–5 全程保持深暗部符号化线性浮点区间（显式启用传统 `math_v3_unified == false` 路径时保留有理化 Anscombe GAT 回归模式）。
2. **`64×64` COLA 零均值 2D FFT Parseval 白化四倍频程 SURE 与 `tile_dv` 能量守恒**：
   对 50% 重叠正弦窗 `64×64` 块先减去精确 COLA 加权均值消除直流泄漏，再按各像素输出模式对应的 Parseval 归一化采样核功率谱 $\psi(\omega)$ 与粒 floor `[0.04, 0.36]` 缩放频域系数，对 `oct 0..3` 执行闭式 SURE 经验维纳收缩并将精确分块方差损耗 `tile_dv` 传递回线性光域逆变换（Z8 实拍噪声谱平坦度 SFM 从 `0.440 -> 0.608`（`1×`）与 `0.330 -> 0.540`（`2×`））。
3. **分块 Morozov 前向 GLS（含 Schur 补色度预条件与光学 PSF 前向模糊）及 Fisher 零空间 `3×3` 色雅可比 $\sigma_2(\mathbf{J}_{\mathbf{x}})$（`M5-A`，三端原生）**：
   以传感器原生帧前向投影残差（纳入光学 PSF 弥散 `inv_blur`、`ois` 色差偏移与帧锐度权重）在 `CPU / Metal / Vulkan` 同步执行迭代校准：仅当整块卡方或 `0.80×` 最锐 `8×8` 子块卡方超过 $\tau = \frac{N-1}{N}\tau_0$ 时才激活更新；在活性图块上求解闭式 **`3×3` 双边 Schur 补对立色度预条件子**（协同更新 `dCr_bar`/`dCb_bar` 与 `dG`），并对 Fisher 补集 $(1 - \phi_{\text{Fisher}})$ 施加非扩张 `3×3` 色雅可比 $\sigma_2$ 正交收缩且严格冻结 `G` 通道（Z8 实拍 `2×` 消色差高频色度混叠比从 `1.524` 降至 **`0.447`**，降低 **`3.41×`**）。
4. **单遍 `64×64` 紧致 R2C/打包复数 2D FFT 三通道 + OLPF 光学反卷积（`M5-B`，三端原生）**：
   在 `64×64`（`40×40` 核心 + `12` 像素保护环）图块上一遍完成 `R2C` 共轭打包 FFT（Metal/Vulkan 共享内存半平面核，CPU `R+iB` 复数打包）与三通道 PSF + OLPF 频域逆滤波，受对焦场、噪声门限与 `5×5` 极值包络严格保护；在 45.7 MP Nikon Z8 五帧实测下反卷积耗时由早期空间 RL 的 `34.80s` / `171.76s` 压降至 GPU **`0.157s`（`1×`，提速 `222×`）** 与 **`0.474s`（`2×`，提速 `362×`）**（与 CPU 精度差 `2.38e-7`）。

---

### Stage 6：符号化整型 DNG（`BlackLevel=2048`）、前向光学自评估计量（`--eval-self`）与 L2 融合条带 TIFF 导出

1. **整型 DNG 暗部符号化线性基底（`BlackLevel = 2048`）**：
   输出 `dng` / `dng-raw` / `dng-hq` 16-bit 整型 DNG 时自动设定 `BlackLevel = 2048`，将线性零点负向零均值读出噪声完整保留在数字偏置之上，彻底消除深暗部零点削顶抬升（Pedestal Bias）。
2. **前向光学模型与多线程闭环自评估计量套件（`--eval-self [FILE]`）**：
   在多线程 worker 内采用与重建一致的前向光学 PSF + `ois` + 锐度权重评估 6 项独立闭环指标并输出 JSON/stderr（6 帧 Z8 附加开销仅 `0.43s`（`1×`）/ `1.05s`（`2×`））：保留帧**分层留一卡方 $\chi^2_{\text{LOO}}$**（`flat = 1.012` (`1×`) / `1.007` (`2×`)）、**奇偶半组 FRC**、**亚像素相位多样性行列式**、**Brillouin 区 Welch 噪声 SFM**、**高频色度混叠比**，以及**倾斜刀口 eSFR**。
3. **L2 融合 `row_gen` 条带并行 TIFF 导出（`heraw_exif_io.hpp`）**：
   在 `cam_flip == 0` 路径直接由色彩矩阵 + 自适应影调（`asinh` / `tanh`）+ `SplitMix64` TPDF 抖动在 L2 缓存驻留的 `row_gen` 条带生成器内直出 16-bit 压缩条带（跳过整幅 `u16` 中间缓冲区分配），按 worker 槽位复用 `z_stream`（`deflateReset`），并对 `>= 64 MB` 输出以 POSIX 并行 `pwrite` 直写磁盘（`rgb-transform-quantize` 耗时 **`0.0011s`**），支持 `TIFF Predictor=2 + Deflate`（`0.39×`）、`tiff-hq`（`~0.19×`）、`dng SOF3` 与 `dng-hq`（`0.09×`）。

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

## 4. Unified V3 全端异构架构（`CPU / Metal / Vulkan`）

- **CPU（`ARM64 NEON` / `x86_64 AVX2+FMA`）**：执行 Unified V3（`M0–M5` + L2 融合条带编码）统一求解器——包含 `Hm` 单应性预扭曲与切向置信域光流、16 KB 精确积分 NLT-VST、`64×64` COLA 零均值 2D FFT Parseval 能量守恒四倍频程 SURE、`1×` 传感器网格 Huber 色度金字塔、Schur 补预条件 `M5-A` GLS 与 Fisher 零空间 `3×3` 色雅可比 $\sigma_2$、以及单遍 `64×64` 打包 `R+iB` 2D FFT `M5-B` 光学反卷积；在 45.7 MP Nikon Z8 五帧实测下端到端耗时 `19.98s`（`1×`）与 `30.19s`（`2×`，相比早期 `191.68s` 提速 **`6.35×`**）。
- **macOS `Metal` / Windows & Linux `Vulkan 1.0`（`--backend auto|gpu`）**：Stage 3 `C13` 硅基清除（`6` 个核）、Stage 4/5 Unified V3（`12` 个核）以及 `M5-A` / `M5-B`（`6` 个核）在 **`1×` 与 `2×` 模式下原生执行全部 `49` 个 GPU 算子核**（Hermitian 半平面共享内存 `64×64` COLA SURE 与 `R2C` `M5-B` FFT + `L=3` 奇对称 Huber + 双边 Schur 补 `M5-A`）。在 Apple M1 Max 45.7 MP 五帧 Nikon Z8 实测下，`C13` 压缩至 `1.28s`/`1.32s`，Stage 4/5 压缩至 `1.48s`/`2.21s`，`M5-B` 反卷积压缩至 **`0.157s`/`0.474s`**（提速 **`222×`–`362×`**），全管线端到端耗时 **`8.45s`**（`1×`，提速 **`5.89×`**）与 **`13.30s`**（`2×`，提速 **`14.41×`**），16-bit 成品图与 CPU V3 一致性达 **`71.02 dB`**（`1×`） / **`66.03 dB`**（`2×`）；传统 `math_v3_unified == false` 路径完整保留 `PSNR > 82 dB` 回归兼容性。
- **Unified V3 深度计量、极线光场与闭环反馈（`D-1..D-6`）**：
  - `D-1` 标定相机三自由度极线场 $(b_y, b_x, b_z)$ 与单应性斜平面旋转/呼吸不变剪切恢复；
  - `D-2` 子空间 $F$ 检验分离独立动体速度与静态视差，并沿双半组极线 $\min(\Phi_{\mathcal{F}_-}, \Phi_{\mathcal{F}_+})$ 细化遮挡台阶边缘；
  - `D-3` 动力学遮挡新月非对称定号、非单调薄透镜弥散圆 V 曲线 $w^2(\rho)=a_0+a_1(\rho-\rho_f)^2$（反演实测基线 $\text{mm}$ 与屈光度跨度 $\text{m}^{-1}$）与轴向 LoCA 三模定标；
  - `D-4` `(G, R-G, B-G)` 三通道拮抗边缘止扩与二阶薄板 $\nabla^2\rho=0$ 无裁剪变分多重网格求解；
  - `D-5` Stage 3 极线微扰收敛投影与 `[C4]` 半遮挡背景帧闭环剥离重用；
  - `D-6` 基于图层聚类的分割统一线性帐篷合成与逐轮廓真背景深度 `revealed_bg_depth` 叠加 `$O(2r+1)$` 二阶扫描线矩前缀和（支持有符号 Zernike 球差 `--bokeh-sa`、STF `--bokeh-stf`、任意旋转多边形叶片、`--tilt-plane` 沙姆倾角、`--virtual-fstop`、4D 合成孔径透视穿透 `--refocus-mode synthetic-aperture` 与 `J1` OTF 移焦反卷积）、双层 WebGL2 检视器及抛物面 sub-step `dff_depth` 景深合成（`--motion-mode focus-stack`）；
  - **长连拍流式架构（`N = 30..512`）与受试物理观测矩阵（`O1..O5`，`org.nijin.observer.v3`）**：默认 `max_cluster_frames = 128`、$O(H_h W_h)$ 双向锚点测地光流链与 Stage 4/5 自动分块、`[C13-DustPRNU]` 盲平场除尘自标定、`[C3-IsoLucky]` 等晕斑块幸运成像，以及 $\mathbb{P}^3$ 色线光度/粗糙度、光学锁相与荧光衰减、刚体 $SE(3)$ 自立体/连续体运动学/自由落体 $g_0$ 标尺、Helmholtz 旋度门控纹影 `schlieren_opd` 与卷帘快门模态测振/14-bit 微弱灌注（`--observer-report`）。
- **V3.0.0 原生 `.DXO`、多部件 OpenEXR、谱算子/Probit 求解、光学玻璃散射级联与 `O6–O10` 扩展矩阵（`Q1–Q10`、`R1–R12`、`F-A..F-H`）**：
  - **DxO ONE `.DXO`/`.4RW` 原生多 IFD 解包与 `P0-CLK` 有理数时钟溯源**：原生解析 `.DXO`/`.4RW` 多 IFD（Sony `IMX183CQJ-J` `12.1667 us/row` 行定标，支持 4 帧原生 `2×` 超分门限）及 `LinearizationTable`（`Tag 50712` 变长 LUT 尾端钳位与精确量化方差）；`.NEV`/WVDC/LibRaw 将完整 `pts_ticks/timescale` 有理数时间戳注入 `ClockProvenance` 并写入 `obs.json`（`clock_source`、`clock_tick_num`、`clock_tick_den`、`clock_row_calibrated`）。
  - **零依赖 Multi-Part OpenEXR 2.0 与 `O(1)` 内存两遍流式引擎（`F-E / F-A`）**：基于原生 `zlib` 编解码 `beauty`、`fg`（预乘 RGBA）、`bg`、`scatter` 与 `depth`（支持 `half`/`float`，严格保留负向读噪）；`--streaming` / `--mem-budget-gb` 自动切换零临时磁盘充分统计条带流水线。
  - **凸 B 样条谱算子 $K(x)$、Probit 似然与连续基三档 `1×` 求解（`Q1–Q10`）**：单一物理谱算子 $K(x)$ 正交约束 Stage 4/5/`M5-A`；TicoRAW/ARW6 区间 Probit 与双正交伴随 $\widetilde{L}^*$；上尾截顶生存似然；`--rgb-1x-tier <fast|sinc|joint>` 配对解析自由度 $\nu_r$ 早停；`OkLab` 等色相高光压缩与 Fisher 最优下一拍规划 `--next-shot-plan`。
  - **线性光学玻璃散射、双运动场 $SO(3)$ 快门与 `O6–O10`（`§4–§7`）**：于线性辐射域执行 `hdf`、`black-mist`、零光晕金字塔 `ultra-contrast`、`halation` 及 `uc+*` 级联（物理遮光罩极性 `hooded < standard < wide-flare`）；Schur 投影 `SIM(2)` + 8 节点滑窗 $SO(3)$ 滚快 + `C1–C3` 计算快门 + `F-H` 双窗 sinc + `§7.4` 天文三假设检验 + 裁剪 2D `Voronoi` 合成孔径 + `O6–O10`（`loca_sign`、`modal_wave`、`seeing`、`refraction`、`streak_astrometry`）。
