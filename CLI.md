# `NijiN.LiteBurstSuite` (`nijinLBS`) — RAW Imaging Suite (`v2.1.0` · Final `v2.x` Release)

**[English](#nijinliteburstsuite-nijinlbs--raw-imaging-suite-v210--final-v2x-release) · [简体中文](#nijinliteburstsuite-nijinlbs--raw-imaging-suite-v210--v2-系列最终版)**

`NijiN.LiteBurstSuite` provides cross-platform Metal / Vulkan / CPU multi-frame RAW processing and native desktop GUIs. Built-in static LibRaw `0.23-HE*`, SVT-JPEG-XS, and zlib are bundled, with optional external dynamic library loading via `--libraw`.

## Suite Options & Examples

```sh
# Fujifilm X-Trans: 3-component lossless LinearRaw DNG + TIFF named physical channels
./nijinLBS --out-dir output --emit-raw --emit-rgb --emit-field \
  --raw-format dng-linear --field-mode tiff-alpha --fields motion,phase,focus,sigma burst/

# Pedestrian-free Clean Plate (>=5 frames) / Handheld Virtual ND (>=6 frames via --motion-mode virtual-nd)
./nijinLBS --motion-mode clean-plate --clean-sens 1 --out-dir output burst/

# Transient highlight layer + equal-stride Action Strobe
./nijinLBS --motion-mode strobe --strobe-step 2 --strobe-fade 0.5 \
  --emit-transient --transient-sigma 3.5 --out-dir output burst/

# Native 1× 16-bit True-Sampled RGB (>=3 frames) + visually-lossless tiff-hq + diagnostic log file
./nijinLBS --rgb-scale 1x --rgb-format tiff-hq --log-file output/diag.log \
  --out-rgb-1x output/true1x.tif burst/

# Linear RAW 8× optical metrology & Top-K original RAW triage
./nijinLBS --optics-only --optics-report output/lens burst/
./nijinLBS --export-topk 3 --out-dir selected burst/
```

| Option | Values / Purpose |
|---|---|
| `--rgb-scale` | `2x` (default, gate `≥5` frames), `1x` (native `1×` 16-bit true-sampled RGB + P-G KLT noise refinement, gate `≥3` frames), `both` (emit `2×` and `1×` in one pass) |
| `--out-rgb-1x` / `--emit-rgb-1x` | Explicit `1×` RGB output path / auto `_1x.<ext>` emission under `--out-dir` |
| `--rgb-format` | `tiff-deflate` (default 16-bit lossless), `tiff-hq` (visually-lossless 16-bit shot-noise adaptive quantization TIFF, `~0.48×` lossless size), `png`, `jxs`, `jxs-lossless`, `tiff-raw` |
| `--log-file` | Mirror full CLI flags and complete execution diagnostics into a log file |
| `--motion-mode` | `standard`, `clean-plate`, `virtual-nd`, `strobe` |
| `--clean-sens` / `--vnd-thresh` | Clean-plate sensitivity / fluid detection threshold |
| `--strobe-step` / `--strobe-fade` | Time-ordered strobe stride / fade factor `0..1` |
| `--emit-transient` | Emit `_transient.png`; paired with `--transient-color rgb\|gray`, `--transient-sigma` |
| `--reflection-mode` | `off`, `glint-suppress`, `split-layers` |
| `--export-parallax-html` / `--parallax-amp` | Standalone WebGL 2.5D parallax viewer and orbit amplitude |
| `--export-refocus` / `--refocus-pos` / `--bokeh-strength` | Linear photon-domain bokeh refocus, normalized plane `0..1`, output pixel radius |
| `--fields` | Comma-separated subset of `motion,phase,focus,depth,defocus,specular,sigma,coherence,velocity,flicker,turbulence` |
| `--field-mode` | `split` (default independent 16-bit gray PNGs); `rgb` (3 fields packed per PNG); `tiff-alpha` (named TIFF extra channels); legacy `motion\|phase\|focus` |
| `--raw-format dng-linear` | Three-component 16-bit `SOF3` (`PhotometricInterpretation=34892`) |
| `--relay-raw on\|off` | Bypass spatial denoise/sharpening on RAW master output while keeping rendered RGB processed |
| `--no-tool-exif [on\|off]` | Default `off`; when `on`, omits `Tag 11 ProcessingSoftware` and all tool branding across DNG/TIFF/PNG/JXS while retaining camera firmware (`Tag 305` when non-empty) and full `ExifIFD` |
| `--silicon-clean on\|off` / `--auto-lca on\|off` | Sensor-coordinate warm-pixel/banding cleanup / blind lateral CA compensation |
| `--ibp-iters 0..2` / `--olpf-mode` | RAW iterative back-projection / `auto\|none\|weak4\|gauss16` |
| `--optics-only` / `--optics-report PREFIX` | Emit `PREFIX_telemetry.json` and `PREFIX_passport.png` |
| `--export-lcp FILE` / `--lcp-fields CSV` | Export Adobe `.lcp` lens profile (`lca,vignette,distortion`) |
| `--export-dataset-tiles DIR` | Paired training tiles (`1×` RAW ↔ `2×` GT + `σ_map`) & manifest; `--tile-size 256\|512`, `--tile-min-conf` |
| `--inspect-af` / `--export-topk K` | AF sharpness ranking / copy top-`K` original RAWs with selection JSON |
| `--skip-frames CSV` | 1-based time-ordered exclusion list; anchor indices remap automatically |
| `--threads 0` | Automatic CPU worker count from cache/core topology |
| `--cpu-cores` | `auto` (excludes Intel LP-E & protects Strix Point Zen5c 8 MB L3), `perf`, `physical` (default on battery), `eff`, `all` |
| `--battery-device` | `auto` (system power probe), `on` (defaults `--cpu-cores` to `physical` unless overridden), `off` |
| `--priority` | `normal` (default, avoids Win11 throttling), `high`, `below`, `idle` (Win11 EcoQoS / macOS background QoS) |
| `--simd` | `auto` (x86 AVX2+FMA / arm64 NEON), `avx2`, `avx512` (archived; see `docs/ARCHIVE_AVX512.md`) |

Relative depth/parallax and reflection thresholds use output pixels (`2×` UI gates at **8 / 40 px**). Fields and optics outputs record validity conditions and scales alongside metadata.

Advanced local geometry fields are selectable via `--fields normal_x,normal_y,occlusion` and write `_fields.json` describing units, display scaling, and observability bounds.

Convergence curves and reference container sizes below retain `v1.6.4` fixed Nikon Z8 baselines.

> Full six-stage equations, blind Poisson-Gaussian calibration, cross-CFA anti-star-eater gating, and 16-bit physical field workflows appear in [**`ALGORITHM.md`**](ALGORITHM.md).

---

## 1. Frame Count Convergence, Dark-Frame-Free Calibration & Motion Physics

### 1.1 Measured Convergence Curve (Nikon Z8 HE\* `8280×5520 -> 16560×11040`)

| Burst Frames `N` | Effective Noise Ratio `eta` | Multi-Frame SNR Gain | PSNR vs Full Convergence | In-Phase HF Residual `HF_RMS` | Physical & Engineering Regime |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **`1` frame** | `0.250` | `0.0 dB` | `44.30 dB` | `4.369e-3` | **Single-frame fallback**: anisotropic spatial interpolation + GAT-DCT denoise only |
| **`2–3` frames** | `0.371 -> 0.303` | `+4.3 ~ +5.2 dB` | `47.66 ~ 50.55 dB` | `0.900 -> 0.760e-3` | **Subpixel undersampled**: partial `2×2` phase coverage (`1×` True-RGB minimum is `3` frames) |
| **`4` frames** | **`0.234`** | **`+6.31 dB`** | **`56.70 dB`** | **`0.773e-3`** | **Algebraic lower bound**: executable with `--policy force-all`, no outlier margin |
| **`5` frames** | **`0.208`** | **`+6.83 dB`** | **`66.16 dB`** | **`0.735e-3`** | **Default `2×` SR gate**: M-estimator rejection + `R/B` quarter-density chroma become over-determined |
| **`6–8` frames** | **`0.173 -> 0.149`** | **`+7.6 ~ +8.8 dB`** | **`>= 70.0 dB`** | **`0.694e-3`** | **Recommended sweet spot**: `80%–92%` subpixel coverage with headroom to reject 1–2 blurred frames |
| **`9–12` frames** | `< 0.13` | `+8.9 ~ +10.0 dB` | Saturated | `~0.69e-3` | **Low-light deep boost**: further suppresses deep-shadow shot noise |

### 1.2 Blind Dark Calibration & Motion Classification
- **Dark-frame-free `G1/G2` bias cancellation**: Subdivides `G1` and `G2` channels into patch medians to remove electronic bias and protect near-black linearity without separate dark frames; multi-frame temporal medians clean persistent hot pixels.
- **Per-frame suitability classes**:
  - `MOTION_BLUR`: High-frequency gradient acutance `< min_rel_sharpness * median` (`0.72` `photo`, `0.82` `sci`, `0.75` `arc`).
  - `MOTION_TOO_LARGE`: Global shift `> max_shift_px` (default `120` 1x px), `ZNCC < 0.36`, or block motion dispersion `> 38 px` (splits groups in scan/multi-group mode or rejects in single-group mode).
  - `MOTION_TOO_SMALL`: `N >= 2` with max drift `< 0.15` 1x px (temporal denoise still runs; wizard flags tripod lock).
  - `EXPOSURE_OUTLIER` / `DIM_MISMATCH`: Exposure offset beyond `~0.95 EV`, dark/capped frames, or cropped-sensor mismatch.

---

## 2. Output Formats, Compression Ratios (`Uncompressed = 1.00`) & Color Spaces

### 2.1 `2×` RGGB Linear RAW Output (DNG 1.4, `--raw-format`)

| Value | Encoding & Bit Depth | Size Ratio (`Raw = 1.00`) | Measured (`16560×11040`) | Compatibility & Notes |
| :--- | :--- | :---: | :---: | :--- |
| **`dng`** *(default)* | **16-bit Lossless DNG** (`Compression=7` `SOF3`, 2D planar `PSV=7`) | **`0.39`** (`1:2.6`) | **~`135 MiB`** | Zero-loss master compatible with Lightroom, ACR, Capture One, darktable, macOS Preview, LibRaw |
| **`dng-linear`** | **16-bit 3-Component `LinearRaw` DNG** (`SOF3`, `Photometric=34892`) | - | - | Demosaiced 3-channel linear DNG (Capture One compatible) |
| **`dng-hq`** | **Visually-Lossless Compressed DNG** (`Tag 50712` noise-step curve + `SOF3`) | **`0.09`** (`1:11.6`) | **~`30 MiB`** | Quantization step below residual shot/read noise (`RAW PSNR = 66.27 dB`) |
| **`dng-fp16`** | **16-bit Half-Float HDR DNG** (`SampleFormat=3`, `Predictor=34894`, Deflate) | **`0.29`** (`1:3.5`) | **~`100 MiB`** | Preserves 30-stop floating-point linear headroom |
| **`dng-raw`** | **16-bit Uncompressed DNG** (`Compression=1`) | **`1.00`** | **`349 MiB`** | Zero decompression CPU overhead |

### 2.2 Rendered RGB Output (`--rgb-format`) & Color Spaces

| Value | Encoding & Bit Depth | Size Ratio (`Raw = 1.00`) | Measured (`16560×11040`) | Compatibility & Notes |
| :--- | :--- | :---: | :---: | :--- |
| **`tiff-deflate`** *(default)* | **16-bit Lossless TIFF** (32-row strips, word `Predictor=2` + `Deflate`) | **`0.39`** (`1:2.6`) | **~`410 MiB`** | Smallest and fastest lossless 16-bit RGB; natively opened everywhere |
| **`tiff-hq`** | **16-bit Visually-Lossless TIFF (HQ)** (shadow step `8` / mid `16` / highlight `28` + YCoCg `Predictor=2` + TPDF + `Deflate`) | **`0.19`** (`~0.48×` lossless TIFF) | **~`198 MiB`** | 100% standard 16-bit TIFF compatibility, `PSNR ≈ 60 dB` vs lossless, no 8-bit banding |
| **`png`** | **16-bit Lossless PNG** (`Sub` predictor + `eXIf`) | **`0.40`** (`1:2.5`) | **~`420 MiB`** | Direct web, browser, PS, and PyTorch/OpenCV compatibility |
| **`jxs`** | **14-bit Lossy JPEG XS (`.jxs`)** (`SVT-JPEG-XS`, default `10 bpp`) | **`0.21`** (`1:4.8`) | **~`218 MiB`** | ISO/IEC 21122-3 `.jxs` container with `Exif` + `colr`; decode via `--decode-jxs` |
| **`jxs-lossless`** | **14-bit Lossless JPEG XS (`.jxs`, `Fq=0`)** | **`0.43`** (`1:2.3`) | **~`450 MiB`** | Fast 14-bit lossless codec |
| **`tiff-raw`** | **16-bit Uncompressed TIFF** | **`1.00`** | **`1046 MiB`** | Uncompressed RGB baseline |

- **Color Spaces (`--color-space`)**: `srgb` (default D65 matrix + sRGB curve) and `adobergb` (ISO 12640-2 matrix, $\gamma = 563/256$, auto-embedded **ICC v2.4** profile in TIFF `34675`, PNG `iCCP`, and JXS `colr`).

---

## 3. EXIF / GPS Metadata Preservation & Privacy Switch

Extracts original-endian `IFD0`, `ExifIFD` (`34665`), and `GPSInfo` (`34853`) from each group's Anchor RAW and writes them across DNG, PNG (`eXIf`), JPEG XS (`Exif`), and TIFF:
- **Full retention**: Shutter, aperture, ISO, exposure bias, metering/flash, `DateTimeOriginal` + `SubSecTimeOriginal` + `OffsetTime*`, focal length & 35mm equiv, body/lens make/model/serial, `Artist`, `Copyright`, and full GPS tags.
- **Firmware & processing stamp**: Default writes `"<Camera Firmware> | NijiN.LiteBurstSuite (nijinLBS) 2.1.0 (...)"` plus standard `Tag 11 ProcessingSoftware`. Passing `--no-tool-exif on` omits `Tag 11` completely and writes `Tag 305 Software` only when the source RAW provides an original camera firmware string (otherwise omits `Tag 305`).
- **`2×` geometry update**: Strips 1× byte-offset-bound `MakerNote` / thumbnail offsets and scales `PixelX/YDimension` + `FocalPlaneX/YResolution` accurately (`2×` or `1×` matching emitted scale).

---

## 4. Presets, Physical Detail Reconstruction, Field Maps & GPU Compute

### 4.1 Four Scene Presets

| Preset | Flag | Tuning & Automatic Policy |
| :--- | :---: | :--- |
| **`Photo`** *(default)* | `--preset photo` | Temporal gradient coherence, rigid micro-patch SR, 4-iter RAW + 2-iter RGB PSF RL deconvolution, auto-exposure, hue-preserving highlight rolloff, flat-region polish |
| **`Sci`** | `--preset sci` | Radiometrically linear output (`0` RL iterations, `1.0x` fixed exposure, no rolloff or polish), light denoise (`0.40/0.55`), tight deghosting (`1.35`) & sharpness gate (`0.82`) |
| **`Arc`** | `--preset arc` | Neutral archival master (`0` RL, no polish, mild denoise `0.70/0.85`) |
| **`Astro`** | `--preset astro` | Cross-CFA anti-star-eater gate, chroma preservation, dual-domain celestial 6-term quadratic + horizon alignment (middle frame anchor), halo-bounded star RL, shadow-anchored `asinh` stretch |

### 4.2 Field Maps & Cross-Platform GPU Acceleration
- **Physical Field Maps**: Default `--field-mode split` exports `*_field_motion.png` (`*_field_sky.png` in Astro), `*_field_phase.png`, and `*_field_focus.png`; extendable up to 11+ fields in `split`, `rgb`, or `tiff-alpha` modes.
- **Zero-Dependency GPUs (`--backend auto|gpu|cpu`)**: Windows **Vulkan** embeds 24 SPIR-V shaders using driver `vulkan-1.dll`; macOS **Metal** uses `MTLResourceStorageModeShared` unified zero-copy buffers (`> 80 dB` CPU parity); automatic CPU NEON/AVX2 fallback.

---

## 5. Complete CLI Reference

### 5.1 Core Examples
```bash
# 1) Default single burst -> 16-bit lossless RGGB DNG + 16-bit lossless TIFF
nijinLBS --out-raw sr2x_rggb.dng --out-rgb sr2x_rgb.tif Z8S_8462.NEF Z8S_8463.NEF Z8S_8464.NEF Z8S_8465.NEF Z8S_8466.NEF

# 2) Recursive folder scan -> auto-sorted bursts, Adobe RGB 14-bit JPEG XS
nijinLBS --scan-dir /path/to/shoot_card --recursive --sort-time --policy auto-prune \
  --out-dir /path/to/sr_outputs --emit-rgb --rgb-format jxs --jxs-bpp 10.0 --color-space adobergb

# 3) Sci linear dataset export
nijinLBS --preset sci --scan-dir /path/to/raw_bursts --out-dir /path/to/sci_dataset --emit-raw --emit-rgb --rgb-format png

# 4) Custom external LibRaw inspection
nijinLBS --libraw /custom/path/libraw_r.dylib --inspect /path/to/shoot_card

# 5) Interactive CLI troubleshooting & JXS decoding
nijinLBS --policy interactive --out-rgb out.png *.NEF
nijinLBS --decode-jxs input_sr2x.jxs decoded_16bit.png
```

### 5.2 Complete CLI Reference Table

| Group | Option | Default | Description |
| :--- | :--- | :---: | :--- |
| **Input & Grouping** | `--scan-dir <dir>` | - | Scan directory for RAW bursts (positional dirs also accepted) |
| | `--recursive` | `off` | Recurse into subdirectories |
| | `--sort-time` / `--no-sort-time` | `--sort-time` | Sort by EXIF `DateTimeOriginal` + `SubSec` + natural filename |
| | `--libraw <lib_path>` | Built-in `0.23-HE*` | Load custom shared `LibRaw` library at runtime |
| | `--policy <mode>` | `auto-prune` | `auto-prune` (`>=5` for 2×, `>=3` for 1×), `multi-group`, `force-all`, `interactive` |
| | `--interactive-choice <1\|2>` | `0` | Preselect interactive choice `1` or `2` |
| | `--group-size <N>` | `8` | Target cluster size (`>=5` for 2×, `>=3` for 1×) |
| | `--single-group` | `off` | Disable auto clustering; keep checked GUI/explicit sequence in one burst |
| | `--max-frames <K>` | `0` (all) | Keep top `K` sharpest bright frames per burst |
| | `--burst-gap-s <sec>` | `3.5` | Max inter-shot gap (seconds) for auto burst grouping |
| **Outputs & Color** | `--out-raw <file.dng>` | - | Single-burst RAW DNG output path |
| | `--out-rgb <file>` | - | Single-burst 2× RGB path (`.png`/`.jxs`/`.tif`) |
| | `--out-rgb-1x <file>` | - | Single-burst native 1× 16-bit RGB path |
| | `--rgb-scale <2x\|1x\|both>` | `2x` | Output resolution scale (`2x` `>=5` frames, `1x` `>=3` frames, or `both`) |
| | `--out-field <prefix\|file>` | - | Single-burst physical field output prefix/file |
| | `--out-dir <dir>` | - | Multi-burst output folder (`<stem>_nijinLBS.<ext>`, 1× `_1x.<ext>`) |
| | `--emit-raw` / `--emit-rgb` / `--emit-rgb-1x` / `--emit-field` | - | Enable outputs inside `--out-dir` mode |
| | `--field-mode <mode>` | `split` | `split`, `rgb`, `tiff-alpha`, `motion`, `phase`, `focus` |
| | `--raw-format <fmt>` | `dng` | `dng` (`0.39`), `dng-linear`, `dng-hq` (`0.09`), `dng-fp16` (`0.29`), `dng-raw` (`1.00`) |
| | `--rgb-format <fmt>` | `tiff-deflate` | `tiff-deflate` (`0.39`), `tiff-hq` (`0.19`), `png` (`0.40`), `jxs` (`0.21`), `jxs-lossless` (`0.43`), `tiff-raw` (`1.00`) |
| | `--jxs-bpp <float>` | `10.0` | Target bits-per-pixel for lossy JPEG XS (`3.0..28.0`) |
| | `--color-space <cs>` | `srgb` | `srgb` or `adobergb` (auto-embeds ICC v2.4) |
| **Presets & Compute** | `--preset <name>` | `photo` | `photo`, `sci`, `arc`, `astro` |
| | `--wizard` | `off` | Terminal 5D scene diagnosis & interactive preset wizard |
| | `--anchor-frame <sel>` | `auto` | `auto`, `first`, `middle`, or `1..N` |
| | `--backend <mode>` | `auto` | `auto`, `gpu` (Metal/Vulkan), `cpu` (NEON/AVX2) |
| | `--denoise-strength <0..2.5>` | Preset | 5-level GAT-DCT Wiener strength (`0` bypasses luma spatial denoise) |
| | `--chroma-denoise <0..2.5>` | Preset | 6-level luminance-guided À-Trous chroma multiplier |
| | `--psf-iters <0..12>` / `--rgb-psf-iters <0..8>` | Preset | Linear RAW / rendered RGB optical PSF RL iterations |
| | `--psf-sigma <0.35..3.0>` | `1.05` | Optical PSF radius on `2×` grid |
| | `--deghost-strength <0.4..3.0>` | Preset | Motion/parallax deghosting strictness |
| | `--auto-exposure` / `--highlight-rolloff` / `--edge-polish` / `--dither` | Preset (`dither=on`) | Toggle adaptive exposure, highlight rolloff, flat edge polish, 16-bit TPDF dither |
| | `--ev-offset <EV>` | `0.0` | Manual exposure offset (`-4.0 .. +4.0 EV`) |
| | `--min-rel-sharpness` / `--max-shift-px` | Preset / `120.0` | Sharpness rejection ratio / max `1×` alignment shift |
| **Diagnostics** | `--inspect` / `--plan-json` / `--progress-json` | `off` | Fast scan / JSON grouping plan / real-time stderr progress JSON |
| | `--log-file <file>` | - | Write complete flags and diagnostic logs to file |
| | `--no-tool-exif [on\|off]` | `off` | Omit `Tag 11` and tool strings from exported EXIF |
| | `--decode-jxs <in> <out>` | - | Decode `.jxs`/`.jxc` back to 16-bit `.png` or `.tif` |
| | `--threads <N>` | Auto topology | Worker thread count |

### 5.3 Exit Codes
- `0`: Success (or canceled cleanly in interactive mode).
- `1`: Runtime I/O, LibRaw decode, or disk write failure.
- `2`: Invalid CLI flag or value syntax.
- `3`: Insufficient valid frames (`<5` for `2×`, `<3` for `1×`-only) or unmet physical gate for requested conditional export.

---

## 6. Native Card-Based Desktop GUI

Launch **`NijiN.LiteBurstSuite.app`** on macOS or **`nijinLBS_gui.exe`** on Windows (hot-switchable `zh-CN` / `en-US` and external `.lng` packs, `⌘L` / `Ctrl+L` log export, automatic persistent preferences scoped for CLI/LibRaw paths):
- **macOS Gatekeeper clearance**: If blocked on first launch from a browser download, run `xattr -rd com.apple.quarantine /Applications/NijiN.LiteBurstSuite.app` (or click **Open Anyway** in **System Settings → Privacy & Security**).
- **Custom Engine / LibRaw paths**: Auto-discovers bundled `nijinLBS` binary first, with optional override for external shared `LibRaw`.

---

# `NijiN.LiteBurstSuite` (`nijinLBS`) — RAW Imaging Suite (`v2.1.0` · v2 系列最终版)

`NijiN.LiteBurstSuite` 提供跨平台 Metal/Vulkan/CPU 多帧 RAW 处理及原生桌面 GUI。内置静态 LibRaw、SVT-JPEG-XS 与 zlib，支持 `--libraw` 外部动态库。

## Suite 参数与示例

```sh
# Fuji X-Trans，三通道无损 LinearRaw + TIFF 内嵌物理通道
./nijinLBS --out-dir output --emit-raw --emit-rgb --emit-field \
  --raw-format dng-linear --field-mode tiff-alpha --fields motion,phase,focus,sigma burst/

# 五帧以上空镜；六帧以上虚拟慢门使用 --motion-mode virtual-nd
./nijinLBS --motion-mode clean-plate --clean-sens 1 --out-dir output burst/

# 瞬态高光层和等间隔分身
./nijinLBS --motion-mode strobe --strobe-step 2 --strobe-fade 0.5 \
  --emit-transient --transient-sigma 3.5 --out-dir output burst/

# 原生 1× 16-bit 真采样 RGB 强化 + 视觉无损高级压缩 TIFF (HQ) + 会话诊断日志
./nijinLBS --rgb-scale 1x --rgb-format tiff-hq --log-file output/diag.log \
  --out-rgb-1x output/true1x.tif burst/

# 光学测量及原片快筛
./nijinLBS --optics-only --optics-report output/lens burst/
./nijinLBS --export-topk 3 --out-dir selected burst/
```

| 参数 | 值 / 用途 |
|---|---|
| `--rgb-scale` | `2x`（默认，门槛 `≥5` 张）、`1x`（原生 1× 16-bit 真采样色彩与噪声模型强化，门槛 `≥3` 张）、`both`（单次重建同步输出 2× 与 1×） |
| `--out-rgb-1x` / `--emit-rgb-1x` | 单组指定 1× RGB 输出路径 / 批量目录模式下导出 `_1x.<ext>` |
| `--rgb-format` | `tiff-deflate`（默认 16-bit 无损）、`tiff-hq`（16-bit 视觉无损自适应步长量化 TIFF，`~0.48×`）、`png`、`jxs`、`jxs-lossless`、`tiff-raw` |
| `--log-file` | 将完整命令行参数与全部运行诊断输出同步写入指定日志文件 |
| `--motion-mode` | `standard`、`clean-plate`、`virtual-nd`、`strobe` |
| `--clean-sens` / `--vnd-thresh` | 空镜灵敏度 / 流体区域阈值 |
| `--strobe-step` / `--strobe-fade` | 按时间排序抽帧步长 / `0..1` 渐隐 |
| `--emit-transient` | 额外输出 `_transient.png`；`--transient-color rgb|gray`、`--transient-sigma` |
| `--reflection-mode` | `off`、`glint-suppress`、`split-layers` |
| `--export-parallax-html` / `--parallax-amp` | 单文件 WebGL 视差预览与幅度 |
| `--export-refocus` / `--refocus-pos` / `--bokeh-strength` | 线性光域散景、相对焦平面 `0..1`、输出像素半径 |
| `--fields` | `motion,phase,focus,depth,defocus,specular,sigma,coherence,velocity,flicker,turbulence` 的逗号列表 |
| `--field-mode` | `split` 默认独立灰度；`rgb` 每三场打包；`tiff-alpha` 主 TIFF 命名附加通道；兼容 `motion|phase|focus` |
| `--raw-format dng-linear` | 三分量 16-bit SOF3，PhotometricInterpretation=34892 |
| `--relay-raw on|off` | RAW 接力输出旁路空间降噪/锐化；RGB 单独使用成片处理 |
| `--no-tool-exif [on|off]` | 默认 `off`；开启后仅保留源相机 EXIF，不写入 `Tag 11 ProcessingSoftware` 与本工具印记 |
| `--silicon-clean on|off` / `--auto-lca on|off` | 传感器坐标温点/条带估计 / 图像拟合色差补偿 |
| `--ibp-iters 0..2` / `--olpf-mode` | RAW 残差回投 / `auto|none|weak4|gauss16` |
| `--optics-only` / `--optics-report PREFIX` | 输出 `PREFIX_telemetry.json`、`PREFIX_passport.png` |
| `--export-lcp FILE` / `--lcp-fields CSV` | 导出有效光学校正模型；选择 `lca,vignette,distortion` |
| `--export-dataset-tiles DIR` | 配对数据块与 manifest；`--tile-size 256|512` 为 2× 目标边长，`--tile-min-conf` 默认 0.95 |
| `--inspect-af` / `--export-topk K` | AF 排序检查 / 拷贝 K 份原片及选片 JSON |
| `--skip-frames CSV` | 时间排序后 1-based 排除列表；指定锚点随排除列表重新映射 |
| `--threads 0` | 自动按异构拓扑选择 CPU 线程数 |
| `--cpu-cores` | `auto`（桌面默认异构推荐：排除 Intel LP-E 并保护 Strix Point Zen5c 8MB L3）、`perf`、`physical`（电池设备默认关超线程）、`eff`、`all` |
| `--battery-device` | `auto`（默认检测系统电池）、`on`（未显式指定 `--cpu-cores` 时默认关 SMT）、`off` |
| `--priority` | `normal`（默认，防 Win11 后台降频）、`high`、`below`、`idle`（Win11 EcoQoS / macOS E 核静默） |
| `--simd` | `auto`（默认 x86 AVX2+FMA / arm64 NEON）、`avx2`、`avx512`（已归档，参见 `docs/ARCHIVE_AVX512.md`） |

相对深度/视差与反射的距离以报告单位读取；UI 门槛为 **8 / 40 个 2× 像素**。字段与光学结果记录有效条件和标度，便于继续处理。

高级局部几何字段可通过 `--fields normal_x,normal_y,occlusion` 选择：前两项是相对逆深度平面的梯度法向代理，后一项是前景混合 α。场图伴随 `_fields.json` 记录量纲、显示标度和有效条件。

以下收敛和压缩体积表保留 v1.6.4 固定 Z8 样本参考。

> 完整的六阶段数学推导、泊松-高斯盲标定、跨通道防吞星门控与 16-bit 物理场图工作流详见 [**`ALGORITHM.md`**](ALGORITHM.md)。

---

## 1. 算法所需张数判定、免黑图自标定与运动物理规律

### 1.1 实测收敛曲线（Nikon Z 8 HE\* `8280×5520 -> 16560×11040`）

| 连拍张数 `N` | 有效噪声比 `eta` | 多帧信噪比增益 | 相对满收敛 `PSNR` | 同相位高频残差 `HF_RMS` | 物理与工程阶段判定 |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **`1` 张** | `0.250` (空间核退化) | `0.0 dB` | `44.30 dB` | `4.369e-3` | **单帧退化模式**：仅做各向异性空间插值与 GAT-DCT 降噪 |
| **`2–3` 张** | `0.371 -> 0.303` | `+4.3 ~ +5.2 dB` | `47.66 ~ 50.55 dB` | `0.900 -> 0.760e-3` | **亚像素欠采样**：覆盖部分 `2×2` 相位象限 |
| **`4` 张** | **`0.234`** | **`+6.31 dB`** | **`56.70 dB`** | **`0.773e-3`** | **理想微位移代数下限**：`--policy force-all` 可强制执行，但无外点冗余 |
| **`5` 张** | **`0.208`** | **`+6.83 dB`** | **`66.16 dB`** | **`0.735e-3`** | **标准 2× 默认下限**：M-estimator 外点抑制与 `R/B` 四分之一密度色度载波同时超定（纯 `1× RGB` 默认下限为 `3` 张） |
| **`6–8` 张** | **`0.173 -> 0.149`** | **`+7.6 ~ +8.8 dB`** | **`>= 70.0 dB`** | **`0.694e-3`** | **最佳推荐甜点区间**：`80%–92%` 亚像素覆盖，允许剔除 1–2 张微糊帧仍满血输出 |
| **`9–12` 张** | `< 0.13` | `+8.9 ~ +10.0 dB` | 收敛饱和 | `~0.69e-3` | **暗光高感增强**：进一步压低深暗部散粒噪声 |

### 1.2 免黑图 `G1/G2` 盲暗电平自标定与帧间运动判定
- **全自动免黑图自标定**：利用拜耳阵列中同光谱响应的 `G1/G2` 绿色通道分块中位数盲标定暗电平差值并抑制深暗部负偏漂移，配合跨帧中位数时域统计自动修复恒定热噪点，全程无需拍摄独立黑图。
- **帧适格与运动分类**：
  - **`MOTION_BLUR`（单帧抖动糊片）**：单帧高频梯度锐度 `acutance < min_rel_sharpness * 同场景中位锐度`（`photo` 预设 `0.72`，`sci` 预设 `0.82`，`arc` 预设 `0.75`）。
  - **`MOTION_TOO_LARGE`（运动过大 / 切镜 / 局部大面积非刚性运动）**：全局位移 `shift_1x > max_shift_px`（默认 `120` 1x 像素）、相关峰值 `ZNCC < 0.36`，或分块局部运动离散度 `> 38 px`。在目录扫描或多组模式下自动切分为新超分组；在单组模式下作为外点剔除。
  - **`MOTION_TOO_SMALL`（运动过小 / 三脚架死锁）**：`N >= 2` 且整组最大帧间位移 `< 0.15` 1x 像素。多帧时域降噪正常生效，交互模式会提示亚像素位移偏小。
  - **`EXPOSURE_OUTLIER` / `DIM_MISMATCH`（曝光异常或画幅不一）**：中位亮度偏离同组中位数超过 `~0.95 EV`、误拍全黑盖镜头帧，或传感器裁切画幅不一致。

---

## 2. 按需输出格式、倍率参考（未压缩 = `1.00`）与色彩空间

支持按需单独输出 **RGGB 线性 RAW（`.dng`）**、单独输出 **渲染 RGB（`.png` / `.jxs` / `.tif`）**，或一次合成同时输出两者。

### 2.1 2× RGGB 线性 RAW 输出（标准 DNG 1.4，`--raw-format`，以未压缩 = `1.00` 为基准）

| 参数值 | 位深与编码机制 | 倍率参考 (`未压缩 = 1.00`) | 实测体积 (`16560×11040`) | 适用场景与兼容性 |
| :--- | :--- | :---: | :---: | :--- |
| **`dng`** *(默认)* | **16-bit 无损 DNG**（`Compression=7` ISO/IEC 10918-1 `SOF3` 2D 同相位预测 `PSV=7`） | **`0.39`** *(压缩比 `1:2.6`)* | **~`135 MiB`** | **零误差无损**，兼容 Lightroom、ACR、Capture One、darktable、macOS 预览及 LibRaw |
| **`dng-hq`** | **高级有损压缩 DNG**（**14-bit → 16-bit** 残余散粒噪声自适应曲线 `Tag 50712` + `SOF3`） | **`0.09`** *(压缩比 `1:11.6`)* | **~`30 MiB`** | 量化步长低于超分后残余散粒/读出噪声（实测 `RAW PSNR = 66.27 dB`），通用 DNG 1.4 兼容 |
| **`dng-fp16`** | **16-bit 半精度浮点 HDR DNG**（`SampleFormat=3` + `Predictor=34894` + `Deflate`） | **`0.29`** *(压缩比 `1:3.5`)* | **~`100 MiB`** | 保留 30 档浮点动态范围，适合高动态后期与科学流程 |
| **`dng-raw`** | **16-bit 未压缩 DNG**（`Compression=1`） | **`1.00`** *(基准)* | **`349 MiB`** | 零解压 CPU 开销的底层直读基准 |

### 2.2 2× 渲染 RGB 输出（`--rgb-format`，以 16-bit 未压缩 TIFF = `1.00` 为基准）与色彩空间

| 参数值 | 位深与编码机制 | 倍率参考 (`未压缩 = 1.00`) | 实测体积 (`16560×11040`) | 适用场景与兼容性 |
| :--- | :--- | :---: | :---: | :--- |
| **`tiff-deflate`** *(默认)* | **16-bit 无损 TIFF**（多线程 32 行分条 `Deflate` + 16-bit 字级 `Predictor=2` 水平差分） | **`0.39`** *(压缩比 `1:2.6`)* | **~`410 MiB`** | **全 16-bit 无损默认格式**，Photoshop、Capture One、Affinity、macOS 预览与 OpenCV/tifffile 原生直读 |
| **`tiff-hq`** | **16-bit 视觉无损高级有损 TIFF**（阴影 `step=8` / 中间调 `16` / 亮部 `28` 散粒噪声包络下自适应步长 + TPDF 抖动 + `Predictor=2` `Deflate`） | **`0.19`** *(约为无损 TIFF 的 `0.48×`)* | **~`198 MiB`** | 与 `dng-hq` 同源噪声包络原理，标准 16-bit TIFF 100% 通用直读，相对无损 `PSNR ≈ 60 dB` 且消除 8-bit 条带 |
| **`png`** | **16-bit 无损 PNG**（多线程并行 Deflate + 字节级 `Sub` 预测 + `eXIf` 块） | **`0.40`** *(压缩比 `1:2.5`)* | **~`420 MiB`** | 网页浏览器、看图器、Photoshop 与 PyTorch/OpenCV 开箱直读 |
| **`jxs`** | **14-bit JPEG XS 高级有损压缩（`.jxs`）**（`SVT-JPEG-XS` 14-bit 平面 RGB + `RCT`，默认 `10 bpp`） | **`0.21`** *(压缩比 `1:4.8`)* | **~`218 MiB`** | ISO/IEC 21122-3 `.jxs` 容器内嵌 `Exif` 与 `colr` 盒；支持 `--decode-jxs` 转回 16-bit TIFF/PNG |
| **`jxs-lossless`** | **14-bit JPEG XS 无损（`.jxs`，`Fq=0`）** | **`0.43`** *(压缩比 `1:2.3`)* | **~`450 MiB`** | 14-bit 零损编码极速读写 |
| **`tiff-raw`** | **16-bit 未压缩 TIFF** | **`1.00`** *(基准)* | **`1046 MiB`** | 未压缩 RGB 基准格式 |

- **色彩空间（`--color-space`）**：
  - `srgb` *(默认)*：标准 D65 sRGB 矩阵与分段传递函数，写入标准 `sRGB` 标记。
  - `adobergb`：ISO 12640-2 **Adobe RGB (1998)** 广色域矩阵与精确 $\gamma = 563/256$ 传递函数，在 PNG（`iCCP`+`cHRM`+`gAMA`）、JPEG XS（`colr` `'prof'` box）与 TIFF（`Tag 34675`）中自动内嵌标准 **ICC v2.4** 配置文件。

---

## 3. EXIF / GPS 元数据保护策略

从每组参考帧（Anchor RAW）按原字节序提取 `IFD0`、`ExifIFD`（`34665`）与 `GPSInfo`（`34853`），写入 DNG、PNG（`eXIf` 块）、JPEG XS（`Exif` 盒）与 TIFF：
- **完整保留**：快门、光圈、ISO/感光度类型、曝光补偿、测光/闪光模式、拍摄时间与毫秒级 `SubSecTimeOriginal`、时区偏移 `OffsetTime*`、焦距与等效 35mm 焦距、机身与镜头型号/序列号、作者 `Artist`、版权 `Copyright`、全部 GPS 坐标与时间戳。
- **保留原始固件并追加处理记录**：默认组合为 `"<原相机固件> | NijiN.LiteBurstSuite (nijinLBS) 2.1.0 (...)"`，同步写入标准 `Tag 11 ProcessingSoftware`；若指定 `--no-tool-exif`（`on`），则完全省略 `Tag 11` 且不在 `Tag 305 Software` 追加任何本工具标识（仅在原片含 `Software` 时原样保留相机固件，否则省略 `Tag 305`）。
- **更新 2× 几何标签**：清理绑定 1× 原始字节偏移的 `MakerNote` 与 1× 缩略图偏移；将 `PixelXDimension` / `PixelYDimension` 与焦平面分辨率 `FocalPlaneX/YResolution` 精确更新至 `2×`。

---

## 4. 四档模式预设、纯物理深度重建、智能向导与跨平台 GPU 加速

### 4.1 四档模式预设（`Photo 照片` / `Sci 科学` / `Arc 归档` / `Astro 星空`）

| 预设名称 | CLI 参数 | 设计定位与自动联动策略 |
| :--- | :---: | :--- |
| **`Photo 照片`** *(默认)* | `--preset photo` | **日常摄影出片**：开启跨帧时域相干保微纹理、刚体微块超分、线性 RAW 4 次 + RGB 2 次光学 PSF 反卷积恢复、自适应分位数自动曝光、保色相高光滚降与平坦区边缘抛光。 |
| **`Sci 科学`** | `--preset sci` | **科研测量与模型训练真值**：一键关闭光学 PSF 反卷积（`0` 次）、自动曝光增益（固定 `1.0x` 辐射度线性对齐）、高光非线性滚降与平坦区抛光；降低空间降噪（`0.40/0.55`）保留原生微纹理与噪声功率谱，收紧运动去鬼影（`1.35`）与清晰度门槛（`0.82`）。 |
| **`Arc 归档`** | `--preset arc` | **数字底片中性归档**：关闭光学 PSF 反卷积与平坦区抛光，采用温和空间降噪（`0.70/0.85`）与标准影调映射，适合长期母版留存与二次精修。 |
| **`Astro 星空`** | `--preset astro` | **无赤道仪手持打星 / 手持星野**：启用跨通道拜耳邻域“防吞星”门控、关闭星点色度中值与紫边消色以完整保留恒星黑体与星云色彩、星点质心二阶多项式 + 天际线**天地双域独立对齐**（默认中间帧定格）、无黑环星形 RL 恢复与夜空黑位锚定 + 保色 `asinh` 星云拉伸。 |

### 4.2 纯物理深度细节重建与 16-bit 物理场图（`Field Map`）
1. **跨帧时域梯度相干门控（Temporal Gradient Coherence）**：逐像素统计多帧对齐梯度的矢量相干度，自动豁免多帧同相位的真实微弱纹理（羽毛、织物、暗部树叶、星云丝缕）在 5 层 8×8 GAT-DCT Wiener 中的收缩（CPU / Metal / Vulkan 三端同步生效）。
2. **三门控高置信刚体微块捞取（Rigid Micro-Patch SR）**：在局部运动被静态背景拒绝的纹理区，仅对同时通过高相关度（`ZNCC >= 0.90`）与泊松-高斯细/宽双尺度卡方残差检验的刚体微块捞取邻帧真实亚像素样本，非刚性形变无缝退回单帧多帧干净拜耳引导的 Hamilton-Adams 二阶方向插值。
3. **16-bit 无损物理场图（`--out-field <prefix>` / `--emit-field` + `--field-mode`）**：
   导出与 2× 主图同尺寸、像素严丝合缝对齐的 16-bit 无损 PNG（`0.0 -> 0 黑`, `1.0 -> 65535 白`），**默认分离输出三张单通道灰度图（`--field-mode split`）**：
   - **`*_field_motion.png`（`Astro` 星野模式下自动为 `*_field_sky.png`）**：防镂空局部动体蒙版，或星野天空/地景天际线分割蒙版（可直接贴入图层蒙版分区调色）；
   - **`*_field_phase.png`**：多帧 `2×2` 亚像素超分相位置信度（可供 `Sci` 模式深度学习训练按像素加权 Loss）；
   - **`*_field_focus.png`**：二阶拉普拉斯区域包络**光学对焦锐度场**（白 = 合焦主体高频纹理，`0` = 焦外散景虚化）；
   - **可选导出模式（`--field-mode` / GUI 下拉框）**：`split`（默认三张灰度）、`rgb`（合并为单张 R/G/B 场图）、`tiff-alpha`（主 TIFF 命名通道）、`motion`、`phase`、`focus`。完整公式与 PyTorch/PS 用法见 [`ALGORITHM.md`](ALGORITHM.md)。
4. **五维智能向导（`--wizard` / GUI「智能向导」）**：毫秒级识别「手持星野 / 纯星空 / AEB 包围曝光 / 三脚架稳态 / 局部动体 / 高感弱光」，给出超分潜力分、皇冠最锐帧、首帧快门微震提示与整套推荐参数处方，支持任选第 `1..N` 张作为姿态定格帧。

### 4.3 零依赖跨平台 GPU 计算加速（`--backend auto|gpu|cpu`）
- **Windows（`Vulkan`）**：内嵌 24 个预编译 Vulkan 1.0 `SPIR-V` 计算着色器，运行时动态加载显卡驱动自带 `vulkan-1.dll`（NVIDIA / AMD / Intel 独显与核显通用，零外部 SDK/DLL 依赖）。在 `RTX 4080 SUPER` 上，6 张 45.7 MP Nikon Z8 HE\* 合成 `16560×11040`（`1.83 亿像素`）端到端耗时 **`16.97 s`**。
- **macOS（`Metal`）**：原生调用 `Metal.framework` 与 Apple Silicon `MTLResourceStorageModeShared` 统一内存零拷贝缓冲，与 CPU 参考管线的 16-bit DNG 一致性达 **`PSNR > 80 dB`**。
- **自动回退**：默认 `--backend auto` 优先使用独显 GPU，APU / Xe2 以下核显与无 GPU 环境自动切换至纯 CPU NEON / AVX2 SIMD 管线。

---

## 5. CLI 命令参考

### 5.1 核心用法概览
```bash
# 1) 单组连拍：默认同时输出最高效无损组合 —— 16-bit 无损 RGGB DNG (0.39x) + 16-bit 无损 TIFF (0.39x)
nijinLBS \
  --out-raw sr2x_rggb.dng \
  --out-rgb sr2x_rgb.tif \
  Z8S_8462.NEF Z8S_8463.NEF Z8S_8464.NEF Z8S_8465.NEF Z8S_8466.NEF Z8S_8467.NEF

# 2) 大批量文件夹扫描：自动按 EXIF 时间排序、发现所有 >=5 张可超分组，输出 14-bit JPEG XS 高级有损压缩 (Adobe RGB)
nijinLBS --scan-dir /path/to/shoot_card --recursive \
  --sort-time --policy auto-prune \
  --out-dir /path/to/sr_outputs --emit-rgb --rgb-format jxs --jxs-bpp 10.0 --color-space adobergb

# 3) Sci 科学模式：严格辐射度线性输出无损 DNG 与无损 16-bit PNG
nijinLBS --preset sci \
  --scan-dir /path/to/raw_bursts \
  --out-dir /path/to/sci_dataset --emit-raw --emit-rgb --rgb-format png

# 4) 加载自定义外部 LibRaw 动态库执行解码与体检
nijinLBS --libraw /custom/path/libraw_r.dylib --inspect /path/to/shoot_card

# 5) 交互式诊断模式：遇到不足 5 张、糊片、三脚架零位移或长序列时，终端给出至多 2 个推荐选项确认
nijinLBS --policy interactive --out-rgb out.png *.NEF

# 6) 将 14-bit JPEG XS (.jxs) 解码转回 16-bit 无损 PNG 或 TIFF
nijinLBS --decode-jxs input_sr2x.jxs decoded_16bit.png
```

### 5.2 完整参数说明表

| 类别 | CLI 选项 | 默认值 | 说明 |
| :--- | :--- | :---: | :--- |
| **输入、解码与分组** | `--scan-dir <dir>` | - | 扫描目录中的 RAW 文件并自动识别可超分连拍组（位置参数亦可直接传目录） |
| | `--recursive` | `off` | 扫描文件夹时递归遍历子目录 |
| | `--sort-time` / `--no-sort-time` | `--sort-time` | 是否按 EXIF `DateTimeOriginal` + 毫秒 `SubSec` + 自然文件名自动排序 |
| | `--libraw <lib_path>` | 内置 `0.23-HE*` | 运行时动态加载外部自定义 `LibRaw` 共享库（`.dll` / `.dylib` / `.so`） |
| | `--policy <mode>` | `auto-prune` | `auto-prune`（剔除异常，标准 2× 需 `>=5` 张，纯 1× RGB 需 `>=3` 张）、`multi-group`（长序列自动多组）、`force-all`（强制全部执行，最低 1 张）、`interactive`（异常/长序列提供 `<=2` 个推荐选项交互确认） |
| | `--interactive-choice <1\|2>` | `0` (询问) | 在 `interactive` 模式下预选方案 `1` 或 `2` |
| | `--group-size <N>` | `8` | `multi-group` 或批量长连拍切分时的目标每组帧数（标准 2× 下限 `5`，纯 1× RGB 下限 `3`） |
| | `--single-group` | `off` | 将输入视为同一连拍，关闭时间/元数据自动分簇；配合 `--policy force-all` 按勾选帧处理整组。GUI 对已预览的每组使用此组合，保持组边界。 |
| | `--max-frames <K>` | `0` (不限) | 每组最多保留清晰度最高的前 `K` 张亮帧 |
| | `--burst-gap-s <sec>` | `3.5` | 批量目录扫描时判定同一候选连拍簇的最大相邻快门时间间隔（秒） |
| **输出与色彩** | `--out-raw <file.dng>` | - | 单组输出 2× RGGB DNG 文件路径 |
| | `--out-rgb <file>` | - | 单组输出 2× RGB 文件路径（自动按后缀识别 `.png`/`.jxs`/`.tif`） |
| | `--out-rgb-1x <file>` | - | 单组输出原生 1× 16-bit RGB 文件路径 |
| | `--rgb-scale <2x\|1x\|both>` | `2x` | RGB 输出倍率：`2x`（超分，门槛 `>=5` 张）、`1x`（真采样 16-bit 强化，门槛 `>=3` 张）、`both`（单次合成同步双出） |
| | `--out-field <prefix\|file>` | - | 单组输出 2× 16-bit 物理场图路径/前缀（默认分离输出 `_motion`/`_sky`、`_phase`、`_focus` 三张灰度 PNG） |
| | `--out-dir <dir>` | - | 批量/多组输出目录（自动命名为 `<首帧名>_nijinLBS.<ext>`，1× 为 `_1x.<ext>`） |
| | `--emit-raw` / `--emit-rgb` / `--emit-rgb-1x` / `--emit-field` | - | 配合 `--out-dir` 按需开启 RGGB DNG、2× RGB、1× RGB 与 16-bit 物理场图输出 |
| | `--field-mode <mode>` | `split` | `split`（**默认**：分离三张 16-bit 灰度 PNG）、`rgb`（合并单张 16-bit RGB PNG）、`tiff-alpha`（主 TIFF 命名附加通道）、`motion`、`phase`、`focus` |
| | `--raw-format <fmt>` | `dng` | `dng`（16-bit 无损，倍率 `0.39`，默认）、`dng-linear`（三通道线性）、`dng-hq`（高级有损压缩，倍率 `0.09`）、`dng-fp16`（16-bit 半精度浮点，倍率 `0.29`）、`dng-raw`（16-bit 未压缩，基准 `1.00`） |
| | `--rgb-format <fmt>` | `tiff-deflate` | `tiff-deflate`（16-bit 无损 TIFF，倍率 `0.39`，默认）、`tiff-hq`（16-bit 视觉无损自适应步长 TIFF，约无损体积的 `0.48×`）、`png`（16-bit 无损 PNG，倍率 `0.40`）、`jxs`（14-bit JPEG XS 高级有损压缩，倍率 `0.21`）、`jxs-lossless`（14-bit JPEG XS 无损，倍率 `0.43`）、`tiff-raw`（16-bit 未压缩 TIFF，基准 `1.00`） |
| | `--jxs-bpp <float>` | `10.0` | `jxs` 高级有损压缩模式的目标每像素比特数（范围 `3.0..28.0`） |
| | `--color-space <cs>` | `srgb` | `srgb` 或 `adobergb`（自动计算广色域矩阵并内嵌 ICC v2.4 配置文件） |
| **预设、定格与后端** | `--preset <name>` | `photo` | `photo`（Photo 照片）、`sci`（Sci 科学）、`arc`（Arc 归档）、`astro`（Astro 星空 / 手持星野） |
| | `--wizard` | `off` | 启动终端五维场景诊断与智能参数处方向导（支持一键采纳或任选定格帧） |
| | `--anchor-frame <sel>` | `auto` | 运动主体/天际线定格帧：`auto`（自动最锐帧）、`first`（首帧）、`middle`（中间帧）或 `1..N` 序号 |
| | `--backend <mode>` | `auto` | `auto`（GPU 优先，无 GPU 自动退回 CPU）、`gpu`（强制 Metal / Vulkan GPU）、`cpu`（纯 CPU SIMD） |
| | `--denoise-strength <0..2.5>` | 随预设 | 2× 亮度域 5 层 GAT-DCT Wiener 降噪倍率（`0` = 旁路空间亮度降噪） |
| | `--chroma-denoise <0..2.5>` | 随预设 | 亮度引导 6 层 À-Trous 色度降噪倍率 |
| | `--psf-iters <0..12>` | 随预设 | 线性 RAW 阶段光学 PSF RL 反卷积迭代次数（`0` = 关闭） |
| | `--rgb-psf-iters <0..8>` | 随预设 | RGB 导出阶段微反差 RL 迭代次数（`0` = 关闭） |
| | `--psf-sigma <0.35..3.0>` | `1.05` | 2× 网格上的光学 PSF 高斯半径 |
| | `--deghost-strength <0.4..3.0>` | 随预设 | 局部运动/视差去鬼影严格度（越大越严格） |
| | `--auto-exposure <on\|off>` | 随预设 | 盲分位数自动曝光增益开关 |
| | `--highlight-rolloff <on\|off>` | 随预设 | 高光软滚降与镜面高光色度抑制开关 |
| | `--edge-polish <on\|off>` | 随预设 | DCT 后平坦区域双边抛光开关 |
| | `--dither <on\|off>` | `on` | 16-bit 量化 TPDF 三角概率抖动开关 |
| | `--ev-offset <EV>` | `0.0` | 额外手动曝光偏移（`-4.0 .. +4.0 EV`） |
| | `--min-rel-sharpness <0.1..0.99>` | 随预设 | 自动剔除糊片的相对锐度比门槛 |
| | `--max-shift-px <px>` | `120.0` | 判定运动过大的 1× 全局位移上限（像素） |
| **诊断与集成** | `--inspect` | `off` | 仅执行快速扫描、运动与分组体检并退出 |
| | `--plan-json` | `off` | 向 `stdout` 输出结构化分组与交互推荐方案 JSON |
| | `--progress-json` | `off` | 向 `stderr` 实时输出单行 JSON 进度事件 |
| | `--log-file <file>` | - | 将完整命令行参数与运行诊断日志写入文件 |
| | `--no-tool-exif [on\|off]` | `off` | 开启后在导出 EXIF 中省略 `Tag 11` 与本工具印记 |
| | `--decode-jxs <in> <out>` | - | 独立工具模式：将 `.jxs`/`.jxc` 解码为 16-bit `.png` 或 `.tif` |
| | `--threads <N>` | 自动拓扑 | 并行 CPU 工作线程数 |

### 5.3 退出码（Exit Codes）
- `0`：执行成功（或交互模式下用户选择取消）。
- `1`：运行时错误（RAW 解码失败、动态库加载失败、磁盘写入失败等）。
- `2`：命令行参数语法错误。
- `3`：非交互策略下有效帧不足下限（标准 2× 默认 `<5` 张，纯 1× RGB 默认 `<3` 张），或已选条件导出项的实测门控不满足。

---

## 6. 配套原生卡片式 GUI 说明

macOS 端通过 `.dmg` 拖入 **`NijiN.LiteBurstSuite.app`** 运行（支持 `Cmd+Q` 退出、`Cmd+W` 关窗、`Cmd+L` 导出日志、Dock 拖放与中英文 `.lng` 热切换），Windows 端双击 `nijinLBS_gui.exe`：
- **macOS 首次下载解冻（Gatekeeper 隔离属性移除）**：若首次启动提示“已损坏”或“无法验证开发者”，在终端执行 `xattr -rd com.apple.quarantine /Applications/NijiN.LiteBurstSuite.app`（或在「系统设置 → 隐私与安全性」底部点「仍要打开」）即可永久放行。
- **格式与选项自动记忆**：自动持久化保存格式、缩放倍率、语言、EXIF 印记开关、CPU/GPU 拓扑策略及微调旋钮，下次打开自动恢复。
- **引擎与 LibRaw 路径自定义**：自动发现同包内嵌 `nijinLBS` / `nijinLBS.exe` 与内置 `LibRaw 0.23.0-Snapshot202611`（支持 Nikon HE/HE\*），也可按需指定外部动态库覆盖。
