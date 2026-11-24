# Suite Validation Record

**[English](#suite-validation-record) · [简体中文](#suite-验收记录)**

Environments: Apple M1 Max / macOS arm64; Windows X570 (Ryzen 7 5800X3D + RTX 4080 SUPER Vulkan); Ryzen AI 9 HX 365 (16Plus Strix Point); Core i5-8259U (nuc-8-sumie).

For single-input speed comparisons, Stage 0–6 optimizations, and revised runtime budgets, see [Performance Analysis](PERFORMANCE.md).

## 3.0.0-preview2 — Multi-Platform Audit Closure & Vulkan/Metal/CPU Parity (2026-11-23)

- **339-case stress suite (`--suite all-plus`) & 29-case photo lane**: `320 PASS / 0 FAIL / 19 INPUT_LIMITED / 0 ERROR` (`exit=3`: `boundary` 7, `optical` 1, `expanded` 11) and `29 / 29 PASS` (`21` singletons + `8` pairs) on the signed macOS bundle.
- **Cross-backend Vulkan (`RTX 4080 SUPER`) vs Windows CPU & Linux x64 parity**: AEB clipped-plateau `2×` EXR `RMS = 0.000215` (`max = 0.0265`, `0` pixels `> 0.10`), Sony A9 III 8-frame `2×` EXR `RMS = 0.00109` (`0` pixels `> 0.10`), and Ubuntu 22.04 WSL2 Linux x64 CLI vs Windows CPU max channel diff `2.7e-7` / `7.4e-5` with identical `bound_only` provenance masks.
- **Drift & consumer invariants (`T1..T8`, `ASan+UBSan`, `--crop-box`, `--from-picks`, `.nlens`, `.NEV` 14-bit DNG)**: `16 / 16 PASS` across Metal + CPU (`8 / 8 PASS` under `ASan+UBSan` + `VULKAN_LIFETIME_PASS`), `0` extra margin pixels on EXIF-oriented (`1..8`) DNG/TIFF/EXR crops, `0` cross-serial camera mixups in `--from-picks`, `0` out-of-hull RGB16 drift on local `.nlens` profiles, and `99.48%–99.68%` re-import linear scale parity on exported `14-bit` `.NEV` DNGs (`WhiteLevel=16383, BlackLevel=1008`).
- **Full-frame Nikon Z8 (`45.7 MP × 8` `.NEV` frames) solo Apple M1 Max Metal benchmark**: **`15.87 s`** (`1×`, peak RSS `9.22 GiB`) and **`41.75 s`** (`2×` `182.8 MP`, peak RSS `13.67 GiB`, `single-pass-auto`).
- **Same-viewpoint `50mm -> 150mm` Optical-Zoom Ground-Truth & `ISO 20,000–65,535` `1×` Photon-Fusion Benchmarks**: Across 6 `Sony FE 50-150mm F2 GM` (`50mm` handheld `5–20f` bursts vs `150mm` `2.40×–2.88×` native optical GT) and 3 high-ISO (`ISO 20,000–65,535`) scenes, `nijinLBS` (`photo` preset) achieved **`2×` `PSNR = 28.8–42.6 dB`** and **`Grad-NCC = 0.587–0.808`** (outperforming `Lanczos-3` `0.311–0.461`, single-frame `Commercial Demosaic` `0.470–0.595`, and `Real-ESRGAN x2plus` `0.204–0.509` without GAN stroke/texture hallucination), native **`1×` True-RGB `Grad-NCC = 0.792–0.885`** (vs `0.607–0.724` single-frame RAW), and preserved embossed wallpaper / oxidized metal micro-texture at `ISO 20,000–65,535` (`Grad-NCC 0.364–0.592` vs `0.290` single-frame denoiser).

## 3.0.0 — Unified V3 Release & External Math Audit Closure (2026-11-19)

- **External math audit oracles (`tests/test_suite_v3_audit_oracles.cpp`, ASan/UBSan)**:
  - `ALL V3 AUDIT ORACLES PASSED (E01-E12, Q1-Q10, R1-R12, Cross-Section 1-6, DXO1-4, F-A..F-H)`; verified exact `.DXO` `LinearizationTable` clamping & `11088x7388` `2×` geometry on `ref/DXO_0820.DXO` (`n_ifds=4`, `out=5544x3694`, `val[0..3]={366,371,371,355}`), integration-by-parts Gaussian-derivative `wblur` metric baseline calibration (`b50=10.0032 mm`, `b25=10.0032 mm`, `b40=10.0447 mm`), and `66x64` / `260x260` `dng-hq` (`BlackLevel=2048`) LibRaw signed dark-noise readback.
  - Validated `#ifndef HERAW_HAS_WVDC` header syntax (`clang++ -std=c++20 -x c++-header -fsyntax-only -Wall -Wextra -Werror heraw_wvdc_router.hpp`) and `qa_oracles.py` binary freshness (`6` targets) + `--inspect --plan-json` isolated entry oracle.
- **End-to-end M1 Max (`Metal`) & X570 (`RTX 4080 SUPER` Vulkan + Win32 GUI) causal deltas**:
  - **Sony A9 III `crops-a9m3` Warm-Glass, shutter envelopes & `Joint-1x` (`--dither off --rgb-scale 1x`)**: `--diffusion-mode black-mist --uc-grade 3 --uc-chroma warm-glass` minus `neutral` produced positive red lift and blue attenuation (`mean dR=+46.96, dG=+14.45, dB=-58.31`, `max_abs=189.0`); `black-mist --uc-grade 5` minus `1/8` measured `mean_abs=182.57, max_abs=1278.0`; `--shutter-mode deblur --intra-deblur on` minus `anchor` measured `max_abs=10140.0, mean_abs=361.65`; `--shutter-mode integrate --flow-kappa 0.8` minus `anchor` measured `max_abs=39005.0, mean_abs=4399.18`; and `--rgb-1x-tier joint` minus `sinc` applied Schur-profiled native-CFA 1× residual across `147,456` pixels (`max_abs=11554.0, mean_abs=304.46`).
  - **Quantizer/codec interval `Q4` & highlight saturation `Q5` censorship (`--policy force-all --preset sci --dither off --rgb-scale 1x`)**: on 3-frame Nikon `HE` `.NEF` bursts via Biorthogonal 5/3 lifting adjoint `L^*`, `--codec-constraint probit` vs `off` refined `1,074,020` codec-interval pixels (`max_abs=118.0, mean_abs=0.12, changed=619,965`); on 4-frame `768x768` Bayer DNGs with mixed saturated/unclipped highlights, `--sat-model probit` vs `hard` refined `764` upper-tail pixels (`max_abs=5398.0, mean_abs=0.82, changed=3,815`), while an all-clipped burst enforced zero upward extrapolation (`core_max_delta=0.00`) and emitted `--next-shot-plan` `ev_offset_stop=-2.5`; on `ref/DXO_0820.DXO`, `--codec-constraint box` vs `off` via normalized LUT Voronoi cells refined `15,082,484` codec-interval pixels (`max_abs=13389.0, mean_abs=67.21, changed=20,452,772`).
  - **Zero-temp-disk 2-pass stripe streaming (`--streaming on --mem-budget-gb 1` vs `off` on `ref/DXO_0820.DXO --rgb-scale 2x --dither off`)**: executed `tier=stripe-2pass` across `n_stripes=31` (`stripe_h1=120`, `peak_bytes=1575.99 MiB` split into `pix_work=1299.38 MiB + zstore=276.62 MiB`, `full_decode_calls=1`, `roi_reads=1468`, `completed_passes=2`), achieving `59.39 dB` CPU PSNR (`52.67 dB` Metal 4-stripe PSNR) on `(7388, 11088, 3)` with `0` leaked temporary disk files.
  - **DxO ONE `ref/DXO_0820.DXO` Multi-Part EXR & field fallback**: default EXR omits zero `scatter` when `--diffusion-mode off` (`4` parts `[beauty,fg,bg,depth]`) and includes `[scatter]` (`5` parts) when diffusion is active; explicit `--exr-parts beauty,scatter --diffusion-mode off --field-mode tiff-alpha --emit-rgb --emit-field` on `DXO_0820.DXO` emitted `68,150,606`-byte OpenEXR (`beauty min=-0.690918 max=1.42773`, zeroed `scatter max_abs=0`, verified via `exrinfo -s`) plus split grayscale PNG field maps (`_field_{focus,motion,phase}.png` + `_fields.json`), and `2×` `--field-mode tiff-alpha` recorded `(768, 768, 6)` TIFF with matching `_fields.json` `(768x768)`.
  - **Nikon `.NEV` rational clock provenance (`DSC_0007.NEV --nev-frames 0-3 --observers seeing --rgb-scale 1x`)**: auto-emitted `org.nijin.observer.v3` JSON verified `"clock_source": "pts_ticks"`, `"clock_tick_num": 3003`, `"clock_tick_den": 30000`, `"clock_row_calibrated": true`, `"clock_ambiguous": false`, `seeing_observable=true` (`seeing_r0_norm=0.583741`, `diff_tilt_rms_1x_px=1.71309`).
  - **Windows X570 (`RTX 4080 SUPER` Vulkan + Win32 GUI)**: `nijinLBS_gui.exe --test-prefs` passed (`V3_GUI_CONTROLS_TEST:PASS`, `V3_GUI_CLI_ARGS_TEST:PASS`, `GUI_CONTRACT:defaults=1 ui_memory_engine_isolation=1 roundtrip=1 fields=11`, `PREFS_TEST:PASS`); `nijinLBS.exe --version` confirmed `Vulkan: NVIDIA GeForce RTX 4080 SUPER`; Vulkan `.DXO` Multi-Part EXR (`--rgb-format exr --exr-parts beauty,scatter --diffusion-mode black-mist --uc-chroma warm-glass --highlight-mode oklab-stage --observers all`) finished in `18.70s` (`[4/6] 0.48s`, `[5/6] 0.23s`, `[Diffusion] 2456.60 ms`), emitting `158,346,666`-byte `.exr` and `1,986`-byte `*_observer.json` (`"clock_source": "makernote_dxo_ts"`, `"clock_tick_num": 161`, `"clock_tick_den": 1000`, `"clock_row_calibrated": true`).
- **Strict 6-target C++20 builds (`-Wall -Wextra -Werror`)**:
  - `tools/heraw-sr2x`: `make -j8 all asan && make -B nijinLBS_gui nijinLBS_gui.exe -j8` built `nijinLBS`, `nijinLBS_gui`, `nijinLBS.exe`, `nijinLBS_gui.exe`, `nijinLBS_linux`, and `nijinLBS_asan` (`0` warnings/errors).
  - 7-bit printable ASCII check passed (`ascii_checked=13 bad_bytes=0`) across `heraw_suite_{core,diffusion,exr_stream,features,measurement,motion_shutter,observer,optics,refocus,spectral_solver}.hpp`, `heraw_wvdc_router.hpp`, and `tests/test_suite_v3_{audit_oracles,observer_refocus}.cpp`.
- **All 11 C++ verification suites (`test_suite_*`, `-Wall -Wextra -Werror`)**:
  - `test_suite_v3_audit_oracles`: `ALL V3 AUDIT ORACLES PASSED (E01-E12, Q1-Q10, R1-R12, Cross-Section 1-6, DXO1-4, F-A..F-H)`.
  - `test_suite_v3_observer_refocus`: `[T1]`..`[T8]` passed (`[T1]` `const_err=2.98e-08`; `[T2]` `tilt/flat=2.53x`; `[T3]` `err=0.0101`, `1.66x`, `RMSE=0.0030`; `[T4]` `rmse 0.01053->0.00366` & `w_sharp=0.910 vs w_blur=0.150`; `[T5]` O1 `cos=1.0000` & O2 `f=100.0 Hz, tau_R=5.12 ms`; `[T6]` O3 `vort=0.546` & `Z=4.000 m`; `[T7]` O4 `plume=0.999 vs vortex=0.002` & O5 `vib=240.4 Hz, vital=73.5 BPM`; `[T8]` `ALL v3.0.0 OBSERVER + REFOCUS CAUSAL TESTS PASS`).
  - `test_suite_features`, `test_suite_core`, `test_suite_metrology`, `test_suite_optics` (`OPTICS_TEST:PASS`), `test_suite_io` (`Suite IO tests passed`, including real Sony `.ARW` crop readback), `test_cfa_metadata` (`ILCE-9M3 384x384 CFA=bayer` & `X-H2S 384x384 CFA=xtrans`), and `test_suite_alignment --selftest` (`celestial order=0/1 error=0`) all exited `0`.
  - `test_suite_cpu` (`-DCPU_TEST_METAL`, `CPU Stage45 PASS`) and `test_suite_gpu` (`SUITE_GPU_PASS`, Metal Apple M1 Max) passed cross-backend parity: `m5b_deconv sc=1 max=2.38419e-07`, `sc=2 max=2.68221e-07`, `m5a_gls sc=1 max=0.000200182`, `c13_full max=2.98023e-08`, `sub-stripe CPU/Metal (origin_y=24, phase_y=16/24)` max diff `1.06648e-04`–`2.51979e-04`, and `v3_seam xt=0 s=1/2` max diff `7.7486e-07`–`1.10269e-06`.
- **Python CLI (ASan/UBSan) & macOS GUI verification**:
  - `test_suite_cli.py ./nijinLBS_asan`: Sony A9 III (`8` real crops, `IBP delta=61.901213`, `9 tiles`) and Fujifilm X-H2S (`8` real crops, `IBP delta=9.121133`, `9 tiles`) passed.
  - `test_suite_gui.py --binary ./nijinLBS_gui`: passed `V3_GUI_CONTROLS_TEST:PASS`, `PREFS_TEST:PASS`, `GUI_CONTRACT:defaults=1 ui_memory_engine_isolation=1 roundtrip=1 sync=1 unicode=1 argv=1`, `REFOCUS_ARGV_TEST:PASS`, `LOG_EXPORT_TEST:PASS`, `--test-cmd-q`, `PROCESS_TEST:PASS`, and `8` bilingual `2560x1664` screenshots; `syntax_check.py` & `lint_check.py` reported `checked=2 failed=0`.

## 2.1.0 — Final v2.x Release Verification (2026-11-18)

- **`--no-tool-exif [on|off]` & ExifIFD retention**:
  - `test_suite_io.cpp` (ASan/UBSan + LibRaw on Sony A9 III `DSC00900.ARW`): verified empty `sw_stamp` omits `Tag 11 ProcessingSoftware`, keeps camera firmware `Tag 305` when present (omits `Tag 305` when absent), preserves `ExifIFD 34665`, and emits zero `"NijiN"`/`"nijinLBS"`/`"LiteBurst"` bytes across DNG, TIFF, PNG RGB/Gray (`eXIf`), and standalone EXIF blobs.
  - Full-resolution Nikon Z8 `1×` `tiff-hq` (`Z8S_8636–8638.NEF`): `--no-tool-exif on` omitted `Tag 11`, preserved Nikon firmware `"Ver.03.12"` verbatim in `Tag 305`, matched `ExifIFD 34665` and pixels bit-for-bit (`31,940,262` bytes), while default output carried `"NijiN.LiteBurstSuite (nijinLBS) 2.1.0"`.
- **`1×` True-Sampled RGB (`>=3` frames) & `tiff-hq` (~0.48× lossless size)**:
  - ASan CLI suite (`test_suite_cli.py`) and full-resolution Z8 `1×`/`2×` runs passed; 2× default minimum suitability gate tightened to 5 frames while 1× RGB passes with 3 frames.
- **Native macOS AppKit & Windows Win32 UI + X570 Remote Execution**:
  - `test_suite_gui.py`: `PREFS_TEST:PASS` (`GUI_CONTRACT:defaults=1 ui_memory_engine_isolation=1 roundtrip=1 sync=1 unicode=1 argv=1`), `.lng` hot-switching (`zh-CN` / `en-US`), session log export (`LOG_EXPORT_TEST:PASS`), and all 4-tab Chinese/English screenshots visually verified.
  - `X570` (`RTX 4080 SUPER`): `nijinLBS_gui.exe --test-prefs` (`PREFS_TEST:PASS`), Win32 multi-line English Frame Inspector / Tab 2 / Tab 3 layout verified across all 4 tabs (`win_tab0..3_en.png`, `win_tab0_zh.png`), plus native Vulkan `2×`/`1×` `tiff-hq` and `--no-tool-exif` outputs.
- **Battery, APU / iGPU & Desktop Discrete GPU Policy**:
  - Verified AC/DC SMT detection (`HX 365`: 14T AC vs 10T physical DC), iGPU auto-CPU fallback (`nuc-8-sumie` Iris Plus 655), desktop discrete GPU prioritization (`X570` RTX 4080 SUPER), and archived AVX-512 (`ARCHIVE_AVX512.md`).

## 2.0.1 Performance & Photo Acceptance

- Default Photo, 5/6 frames, full-res DNG + TIFF (2.0.1 baseline): Z8 **38.68 / 43.37 s**, A9 III **16.50 / 20.27 s**, X-H2S **22.48 / 24.91 s** (`Z8S_8636–8644.NEF` first 5/6 frames). Follow-up `2.1.0` timings appear in [Performance Analysis](PERFORMANCE.md).
- Six independent DNG/TIFF readbacks passed at **16560×11040**, **12024×8040**, and **12528×8352**.
- Real-crop 5/6-frame Photo CPU vs Metal ASan/UBSan workflow across three cameras: RGB PSNR **92.35–108.06 dB**.
- Metal C13 residual max diff vs CPU reference: **0**; horizontal/vertical tile seam max diff: **0**.
- Windows Vulkan tiled vs full-image parity with denoising enabled: max diff **4.18×10⁻⁷**; shared input halo fix at odd pyramid dimensions (e.g., 1762 → 881) reduced vertical diff from **2.26×10⁻⁴** to **2.09×10⁻⁷**.
- RGB banded RL vs full-frame operator under both standard and OLPF PSFs: max diff **0**.
- Independent half-stack separable sampling vs direct CFA tent integration: max diff **1.79×10⁻⁷**.

## 2.0.0 Native Entry & Real-Sample Parity

| Item | Observed Result |
|---|---|
| macOS CLI / AppKit GUI | C++17 `-Wall -Wextra -Werror`, prefs, Cmd-Q, 4-tab screenshots passed |
| Windows CLI / Win32 GUI | MinGW strict build, X570 native prefs/close/telemetry/4-tab screenshots passed |
| ASan / UBSan | 8-crop A9 III & X-H2S full CLI workflows passed |
| Full-frame A9 III | 12 select 4, **12024×8040** DNG/TIFF; recorded **113.19 s** (v2.0.0) |
| Full-frame X-H2S | 4 frames **12528×8352** LinearRaw/TIFF/sigma; recorded **70.28 s** (v2.0.0) |
| Z8 HE* Astro | ISO 25600, 4 frames, 10 stars detected; **16560×11040** DNG; recorded **99.91 s** |
| Independent Readers | LibRaw/rawpy, ExifTool, `tifffile`, NumPy/ZIP, Pillow, XML reader |

Cross-backend parity (`sci`, spatial denoise/RL/IBP off, dither off):

| Input | Windows CPU → Vulkan | Windows CPU → macOS Metal |
|---|---:|---:|
| A9 III | 125.05 dB | 125.58 dB |
| X-H2S | 126.12 dB | 126.98 dB |

Default Photo without dither (local CPU → Metal): A9 III **104.93 dB**, X-H2S **111.22 dB**.

## Reproducibility

```sh
make nijinLBS nijinLBS_gui
make asan
python3 tests/make_raw_crops.py /path/to/a9iii-or-fuji-burst /path/to/crops
python3 tests/test_suite_cli.py ./nijinLBS_asan /path/to/crops /path/to/results
```

---

# Suite 验收记录

日期：2026-11-14 至 2026-11-18。环境：Apple M1 Max / macOS arm64；Windows X570 / RTX 4080 SUPER；Ryzen AI 9 HX 365（16Plus）；Core i5-8259U（nuc-8-sumie）。

默认流程的同输入速度对照、`v2.1.0` 优化及预算修订见 [性能回归分析](PERFORMANCE.md)。

## 3.0.0-preview2 — 三平台定型复审闭环与 Vulkan/Metal/CPU 一致性验收（2026-11-23）

- **339 项全量压力套件（`--suite all-plus`）与 29 项照片通道**：正式签名包实测 **`320 PASS / 0 FAIL / 19 INPUT_LIMITED / 0 ERROR`**（`exit=3`：`boundary` 7、`optical` 1、`expanded` 11），照片管线 **`29 / 29 PASS`**（`21` 单例 + `8` 配对）。
- **跨后端 Vulkan（`RTX 4080 SUPER`）对比 Windows CPU 及 Linux x64 一致性**：AEB 截顶高光 `2×` EXR 全图 **`RMS = 0.000215`**（`max = 0.0265`，差 `> 0.10` 像素为 `0`），Sony A9 III 8 帧 `2×` EXR **`RMS = 0.00109`**（差 `> 0.10` 像素为 `0`），Ubuntu 22.04 WSL2 Linux x64 CLI 对比同机 Windows CPU 最大通道差 `2.7e-7` / `7.4e-5` 且 `bound_only` 标签完全一致。
- **漂移基准与消费者契约（`T1..T8`、`ASan+UBSan`、`--crop-box`、`--from-picks`、`.nlens`、`.NEV` 14-bit DNG）**：Metal + CPU **`16 / 16 PASS`**（`ASan+UBSan` **`8 / 8 PASS`** + `VULKAN_LIFETIME_PASS`）；EXIF 八方位 `1..8` 局部裁切 DNG/TIFF/EXR 冗余像素为 `0`；`--from-picks` 同型异机零混片；局部 `.nlens` 视场凸包外漂移 `0` 通道；`.NEV` 导出 `14-bit` DNG（`WhiteLevel=16383, BlackLevel=1008`）二次导入线性一致率 **`99.48%–99.68%`**。
- **全画幅 Nikon Z8（`45.7 MP × 8` 帧 `.NEV`）Apple M1 Max Metal 独占测速**：**`15.87 s`**（`1×`，峰值 RSS `9.22 GiB`）与 **`41.75 s`**（`2×` `182.8 MP`，峰值 RSS `13.67 GiB`，单遍自动流式）。
- **同机位 `50mm -> 150mm` 光学变焦真值与 `ISO 20,000–65,535` `1×` 多帧光子融合基准**：在 6 组 `Sony FE 50-150mm F2 GM`（`50mm` 手持 `5–20` 帧连拍对照 `150mm` `2.40×–2.88×` 原生光学真值）及 3 组极限高感（`ISO 20,000–65,535`）实拍场景下，`nijinLBS`（默认 `photo` 预设）在 `2×` 超分取得 **`PSNR = 28.8–42.6 dB`** 与 **`Grad-NCC = 0.587–0.808`**（全面领先 `Lanczos-3` `0.311–0.461`、单帧`商业 Demosaic` `0.470–0.595` 及存在笔画断裂与涂抹幻觉的 `Real-ESRGAN x2plus` `0.204–0.509`），原生 **`1×` 真彩融合 `Grad-NCC = 0.792–0.885`**（对比单帧直解 `0.607–0.724`），并在 `ISO 20,000–65,535` 下完整保留被单帧降噪抹平的压花壁纸经纬压纹与雨刷金属氧化肌理（`Grad-NCC 0.364–0.592` 对比单帧商业降噪 `0.290`）。

## 3.0.0 — 外部数学审计闭环与全量实测验收（2026-11-19）

- **外部数学审计 Oracle 全闭环（`tests/test_suite_v3_audit_oracles.cpp`，ASan/UBSan）**：
  - 打印 `ALL V3 AUDIT ORACLES PASSED (E01-E12, Q1-Q10, R1-R12, Cross-Section 1-6, DXO1-4, F-A..F-H)`；在真实 `ref/DXO_0820.DXO`（`n_ifds=4`，`out=5544x3694`，`val[0..3]={366,371,371,355}`）上通过 `LinearizationTable` 尾端钳位与流式边界校验，并通过分部积分高斯导 `wblur` 米制基线标定（`b50=10.0032 mm`、`b25=10.0032 mm`、`b40=10.0447 mm`）与 `66x64` / `260x260` `dng-hq`（`BlackLevel=2048`）LibRaw 有符号暗部噪声读回。
  - 验证 `#ifndef HERAW_HAS_WVDC` 下 `clang++ -std=c++20 -x c++-header -fsyntax-only -Wall -Wextra -Werror heraw_wvdc_router.hpp` 编译通过，且 `qa_oracles.py freshness`（6 个产物）与 `--inspect --plan-json` 隔离入口校验全部通过。
- **本机 Apple M1 Max（`Metal`）与远端 X570（`RTX 4080 SUPER` Vulkan + Win32 GUI）端到端因果实测**：
  - **Sony A9 III `crops-a9m3` Warm-Glass、快门包络与 `Joint-1x`（`--dither off --rgb-scale 1x`）**：`--diffusion-mode black-mist --uc-grade 3 --uc-chroma warm-glass` 相对 `neutral` 呈现正确暖色偏移（`mean dR=+46.96, dG=+14.45, dB=-58.31`，`max_abs=189.0`）；`black-mist --uc-grade 5` 相对 `1/8` 实测 `mean_abs=182.57, max_abs=1278.0`；`--shutter-mode deblur --intra-deblur on` 相对 `anchor` 实测 `max_abs=10140.0, mean_abs=361.65`；`--shutter-mode integrate --flow-kappa 0.8` 相对 `anchor` 实测 `max_abs=39005.0, mean_abs=4399.18`；`--rgb-1x-tier joint` 相对 `sinc` 在 `147,456` 像素上应用原生 CFA Schur 残差修正（`max_abs=11554.0, mean_abs=304.46`）。
  - **编码区间 `Q4` 与高光截尾 `Q5`（`--policy force-all --preset sci --dither off --rgb-scale 1x`）**：3 帧 Nikon `HE` `.NEF` 上经 Biorthogonal 5/3 提升伴随 `L^*`，`--codec-constraint probit` 相对 `off` 细化 `1,074,020` 个编码区间像素（`max_abs=118.0, mean_abs=0.12, changed=619,965`）；4 帧 `768x768` 混合饱和/未饱和 Bayer DNG 上 `--sat-model probit` 相对 `hard` 细化 `764` 个上尾截尾像素（`max_abs=5398.0, mean_abs=0.82, changed=3,815`），全截顶负例核区强制零向上外推（`core_max_delta=0.00`）并输出 `--next-shot-plan` `ev_offset_stop=-2.5`；`ref/DXO_0820.DXO` 上经归一化 LUT Voronoi 单元，`--codec-constraint box` 相对 `off` 细化 `15,082,484` 像素（`max_abs=13389.0, mean_abs=67.21, changed=20,452,772`）。
  - **零临时盘两遍条带流式调度（`ref/DXO_0820.DXO --rgb-scale 2x --dither off`，`--streaming on --mem-budget-gb 1` vs `off`）**：实测 `tier=stripe-2pass`（`n_stripes=31`、`stripe_h1=120`、`peak_bytes=1575.99 MiB` 拆报 `pix_work=1299.38 MiB + zstore=276.62 MiB`、`full_decode_calls=1`、`roi_reads=1468`、`completed_passes=2`），`(7388, 11088, 3)` CPU 多条带与非流式输出 `PSNR=59.39 dB`（Metal 四条带 `PSNR=52.67 dB`）且 `/tmp` 零临时文件残留。
  - **DxO ONE `ref/DXO_0820.DXO` Multi-Part EXR 与场图回落**：默认 EXR 在 `--diffusion-mode off` 下自动省略空 `scatter`（`4` 个 part `[beauty,fg,bg,depth]`），开启柔焦时包含 `[scatter]`（`5` 个 part）；显式 `--exr-parts beauty,scatter --diffusion-mode off --field-mode tiff-alpha --emit-rgb --emit-field` 写出 `68,150,606` 字节 OpenEXR（`beauty min=-0.690918 max=1.42773`，归零 `scatter max_abs=0`，通过 `exrinfo -s` 独立校验）并自动切换为拆分灰度 PNG 场图（`_field_{focus,motion,phase}.png` 与 `_fields.json`），且 `2×` `--field-mode tiff-alpha` 写出 `(768, 768, 6)` TIFF 与匹配的 `(768x768)` `_fields.json`。
  - **Nikon `.NEV` 有理数时钟溯源（`DSC_0007.NEV --nev-frames 0-3 --observers seeing --rgb-scale 1x`）**：自动写出 `org.nijin.observer.v3` 报告，实测输出 `"clock_source": "pts_ticks"`、`"clock_tick_num": 3003`、`"clock_tick_den": 30000`、`"clock_row_calibrated": true`、`"clock_ambiguous": false`、`seeing_observable=true`（`seeing_r0_norm=0.583741`，`diff_tilt_rms_1x_px=1.71309`）。
  - **Windows X570（`RTX 4080 SUPER` Vulkan + Win32 GUI）**：`nijinLBS_gui.exe --test-prefs` 通过（`V3_GUI_CONTROLS_TEST:PASS`、`V3_GUI_CLI_ARGS_TEST:PASS`、`GUI_CONTRACT:defaults=1 ui_memory_engine_isolation=1 roundtrip=1 fields=11`、`PREFS_TEST:PASS`）；`nijinLBS.exe --version` 报告 `Vulkan: NVIDIA GeForce RTX 4080 SUPER`；Vulkan `.DXO` Multi-Part EXR（`black-mist` + `warm-glass` + `oklab-stage` + `--observers all`）耗时 `18.70s`（`[4/6] 0.48s`、`[5/6] 0.23s`、`[Diffusion] 2456.60 ms`），写出 `158,346,666` 字节 `.exr` 与 `1,986` 字节 `*_observer.json`（`"clock_source": "makernote_dxo_ts"`、`"clock_tick_num": 161`、`"clock_tick_den": 1000`、`"clock_row_calibrated": true`）。
- **全平台 C++20 严格构建（`-Wall -Wextra -Werror`）**：
  - `tools/heraw-sr2x`：`make -j8 all asan && make -B nijinLBS_gui nijinLBS_gui.exe -j8` 零警告零错误编译 `nijinLBS`、`nijinLBS_gui`、`nijinLBS.exe`、`nijinLBS_gui.exe`、`nijinLBS_linux` 与 `nijinLBS_asan`。
  - `heraw_suite_{core,diffusion,exr_stream,features,measurement,motion_shutter,observer,optics,refocus,spectral_solver}.hpp`、`heraw_wvdc_router.hpp` 与 `tests/test_suite_v3_{audit_oracles,observer_refocus}.cpp` 通过纯 7-bit ASCII 字节检查（`ascii_checked=13 bad_bytes=0`）。
- **11 项 C++ 核心、审计与物理一致性回归测试全通过**：
  - `test_suite_v3_audit_oracles`：全量通过 `E01-E12, Q1-Q10, R1-R12, Cross-Section 1-6, DXO1-4, F-A..F-H`。
  - `test_suite_v3_observer_refocus`：`[T1]`..`[T8]` 全通过（`[T1]` `const_err=2.98e-08`；`[T2]` `tilt/flat=2.53x`；`[T3]` `err=0.0101`、`1.66x`、`RMSE=0.0030`；`[T4]` `rmse 0.01053->0.00366` 与 `w_sharp=0.910 vs w_blur=0.150`；`[T5]` O1 `cos=1.0000` 与 O2 `f=100.0 Hz, tau_R=5.12 ms`；`[T6]` O3 `vort=0.546` 与 `Z=4.000 m`；`[T7]` O4 `plume=0.999 vs vortex=0.002` 与 O5 `vib=240.4 Hz, vital=73.5 BPM`；`[T8]` `ALL v3.0.0 OBSERVER + REFOCUS CAUSAL TESTS PASS`）。
  - `test_suite_features`、`test_suite_core`、`test_suite_metrology`、`test_suite_optics`（`OPTICS_TEST:PASS`）、`test_suite_io`（`Suite IO tests passed`，含真实 Sony `.ARW` 裁片读回）、`test_cfa_metadata`（`ILCE-9M3 384x384 CFA=bayer` 与 `X-H2S 384x384 CFA=xtrans`）及 `test_suite_alignment --selftest`（`celestial order=0/1 error=0`）全部退出码 `0`。
  - `test_suite_cpu`（`-DCPU_TEST_METAL`，`CPU Stage45 PASS`）与 `test_suite_gpu`（`SUITE_GPU_PASS`，Metal Apple M1 Max）跨后端对照误差：`m5b_deconv sc=1 max=2.38419e-07`、`sc=2 max=2.68221e-07`、`m5a_gls sc=1 max=0.000200182`、`c13_full max=2.98023e-08`、`sub-stripe CPU/Metal (origin_y=24, phase_y=16/24)` 最大误差 `1.06648e-04`–`2.51979e-04`、`v3_seam xt=0 s=1/2` 最大误差 `7.7486e-07`–`1.10269e-06`。
- **Python CLI（ASan/UBSan）与 macOS GUI 全项实测**：
  - `test_suite_cli.py ./nijinLBS_asan`：Sony A9 III（8 裁片，`IBP delta=61.901213`，9 tiles）与 Fujifilm X-H2S（8 裁片，`IBP delta=9.121133`，9 tiles）通过。
  - `test_suite_gui.py --binary ./nijinLBS_gui`：通过 `V3_GUI_CONTROLS_TEST:PASS`、`PREFS_TEST:PASS`、`GUI_CONTRACT:defaults=1 ui_memory_engine_isolation=1 roundtrip=1 sync=1 unicode=1 argv=1`、`REFOCUS_ARGV_TEST:PASS`、`LOG_EXPORT_TEST:PASS`、`--test-cmd-q`、`PROCESS_TEST:PASS` 及 8 张 `2560x1664` 中英四页截图；`syntax_check.py` 与 `lint_check.py` 报告 `checked=2 failed=0`。

## 2.1.0 — v2 系列最终收官版实测验收（2026-11-18）

- **`--no-tool-exif [on|off]` 与相机原始元数据继承**：
  - `test_suite_io.cpp`（ASan/UBSan + LibRaw 读取 Sony A9 III `DSC00900.ARW`）：实测空工具印记下 DNG、TIFF、PNG RGB/Gray（`eXIf`）与独立 EXIF 块完全省略 `Tag 11 ProcessingSoftware`、有原固件时原样保留 `Tag 305`（无原固件时省略 `Tag 305`）、完整保留 `ExifIFD 34665` 且文件字节零 `"NijiN"` / `"nijinLBS"` / `"LiteBurst"`。
  - 全尺寸 Nikon Z8（`Z8S_8636–8638.NEF`，`1× RGB tiff-hq`）：开启 `--no-tool-exif on` 后 `Tag 11` 缺失、`Tag 305` 原样保留 `"Ver.03.12"`、`ExifIFD 34665` 与图像像素与默认输出比特级全等（`31,940,262` 字节）；默认模式则包含 `NijiN.LiteBurstSuite (nijinLBS) 2.1.0`。
- **`1×` 真采样 16-bit RGB（`>=3` 张）、`2×` 门槛（`>=5` 张）与 `tiff-hq`（约 `0.48×` 无损 TIFF 体积）**：
  - `test_suite_cli.py` 与真实 Z8 全分辨率 `1×`/`2×`/`both` 跑通，版本与输出容器读回通过。
- **macOS AppKit & Windows Win32 界面与 X570 原生运行**：
  - `test_suite_gui.py` 通过 `PREFS_TEST:PASS`（`GUI_CONTRACT:defaults=1 ... argv=1`）、外置 `.lng` 中英热切换与一键诊断日志导出（`LOG_EXPORT_TEST:PASS`）。
  - `X570`（`RTX 4080 SUPER`）通过 Win32 `--test-prefs`、全部英文四标签页与中文首页截图（多行居中 Frame Inspector 无省略号、Tab 2/3 长文案全宽无截断）以及原生 Vulkan 输出验收。
- **电池供电、APU/低阶核显与桌面独显策略**：
  - 完成 AC/DC 超线程策略切换（HX 365 AC 14T vs DC 10T）、iGPU 自动走纯 CPU（`nuc-8-sumie` Iris Plus 655）、桌面独显优先（`X570` RTX 4080 SUPER）及 AVX-512 归档（[`ARCHIVE_AVX512.md`](ARCHIVE_AVX512.md)）验证。

## 2.0.1 性能与照片验收

- 默认 Photo、5/6 帧、全尺寸 DNG + TIFF：Z8 **38.68/43.37 s**，A9 III **16.50/20.27 s**，X-H2S **22.48/24.91 s**。
  Z8 使用一般场景 `Z8S_8636–8644.NEF` 中前 5/6 张。
- 六组独立 DNG/TIFF 读回通过；输出尺寸分别为
  **16560×11040**、**12024×8040**、**12528×8352**。
- 三种相机真实裁片的 5/6 帧 Photo CPU/Metal ASan/UBSan 工作流通过；
  RGB 对照 PSNR **92.35–108.06 dB**。
- Metal C13 残差与 CPU 参考的最大差 **0**；
  Bayer/X-Trans 正常图像横纵分块接缝最大差 **0**。
- Windows Vulkan 的 Bayer/X-Trans 横纵整图/分块对照通过；
  开启降噪时最大差 **4.18×10⁻⁷**。1762 像素边长产生的 881 像素
  金字塔层覆盖奇数尺寸最后一个 DCT 块；共享输入窗口补齐前置一行/列后，
  纵向对照最大差由 **2.26×10⁻⁴** 降至 **2.09×10⁻⁷**。
- RGB 分带 RL 与全图算子在普通/OLPF 两种 PSF 下最大差 **0**。
- IBP 稀疏日志诊断保持图像更新结果一致；局部 η 变化产生预期的
  解析收缩变化；独立半栈可分离采样与直接 CFA tent 积分最大差
  **1.79×10⁻⁷**。
- macOS/Windows 严格构建通过。Windows 全幅瓦片规划使用 `const Tile&`，
  修正 MinGW AVX2 值参数临时副本的对齐问题。
- TIFF Deflate 1 的独立解压像素精确恢复；实样文件增加约 **1%**。

以下为 v2.0.0 发布验收记录。

## 原生入口与实样

| 项目 | 观测结果 |
|---|---|
| macOS CLI / AppKit GUI | C++17 严格构建、偏好、Cmd-Q、四页截图通过 |
| Windows CLI / Win32 GUI | MinGW 严格构建、X570 原生 prefs/关闭/遥测解析/四页截图通过 |
| ASan / UBSan | A9 III、X-H2S 各 8 张真实 CFA 裁片的完整 CLI 工作流通过 |
| 全幅 A9 III | 12 帧选 4，输出 **12024×8040** DNG/TIFF；端到端记录 **113.19 s** |
| 全幅 X-H2S | 4 帧输出 **12528×8352** 三分量 SOF3 LinearRaw/TIFF/sigma；记录 **70.28 s** |
| Z8 HE* 星空 | ISO 25600，4 帧，检测到 10 个候选星点；输出 **16560×11040** DNG；记录 **99.91 s** |
| 独立读取 | LibRaw/rawpy、ExifTool、tifffile、NumPy/ZIP、Pillow、XML reader |

耗时对应当次机器负载、帧选择、输出格式及启用功能。大漂移 A9 III 的流式验收保持输入 ROI 预算 **5,308,416 pixels**，实测最大 **4,825,600 pixels**、35 tiles；独立记录的整进程峰值 RSS 为 **6.67 GB**。全幅画布保留 float32。

## 数值一致性

真实四帧裁片、`sci`、空间降噪/RL/IBP 关闭、dither 关闭：

| 输入 | Windows CPU → Vulkan | Windows CPU → macOS Metal |
|---|---:|---:|
| A9 III | 125.05 dB | 125.58 dB |
| X-H2S | 126.12 dB | 126.98 dB |

默认 Photo 处理、dither 关闭，本机 CPU → Metal：A9 III **104.93 dB**，X-H2S **111.22 dB**。

同一后端的整图/分块测试覆盖 Bayer、X-Trans、横纵接缝、常量、零权重、失败回退和真实 RAW ROI。跨平台配准回归直接比较匹配点、flow、权重与噪声参数。

## 功能与反例

- IBP 在真实裁片产生前向残差改善和可测输出变化。
- 空镜包括参考帧前景去除；ND、分身、彩色瞬态层和相对深度/散景使用可控真值及实样测试。
- 4 个命名附加通道与 11 通道边界样本经独立 TIFF reader 读取。
- 配对数据集的受控静态场景生成 **9 个目标块**；文件集合 conservation 检查通过。
- Top-K 拷贝保留原始文件内容；skip/anchor 重映射与手工重排结果一致。
- 相同/缺失时间戳下，anchor-first 排列通过原始索引保序；非默认字段顺序的 packed RGB 与逐场灰度值对照通过。
- 同时使用 `--out-dir` 和显式路径时，两组输出使用不同全局组号。
- sigma 改用实际有限最大值定标；真实 A9 III/X-H2S 复跑的截顶比例均为 **0**。
- relay 的 RAW-only 与 RAW+RGB 输出，RAW 像素最大差 **0**。
- GUI 重对焦偏移 `-1/0/+1` 对应 CLI 平面 `0/.5/1`；相对散景强度 `1` 对应 12 个输出像素。
- Windows 默认 Photo/C13、多线程、锚点 #3、频闪：修正 MinGW AVX2 的 32-byte Noise 值参数临时副本对齐后，原触发命令连续 5 次通过；统计结果与 macOS 一致。

## 标准检查

- C++20 `-Wall -Wextra -Werror`；CLI/GPU/算法源文件 7-bit ASCII 检查。
- Vulkan 49 个 kernels：glslc、glslang、spirv-val。
- workspace freshness、isolated entry、conservation、numerical 检查。
- Python syntax/lint；原生平台/ARC 参数下的 clang-analyzer-core 和指定 bugprone 项为 0 条正确性诊断。
- 通用跨项目 clang-tidy 包含大量命名、括号、C-array 和 vendor 风格规则；验收沿项目原生编译、sanitizer、跨后端数值和实样路径执行。

## 复现

```sh
make nijinLBS nijinLBS_gui
make asan
python3 tests/make_raw_crops.py /path/to/a9iii-or-fuji-burst /path/to/crops
python3 tests/test_suite_cli.py ./nijinLBS_asan /path/to/crops /path/to/results
```

独立模块测试位于 `tests/`；CPU/Metal 同 job 对照由 `test_suite_cpu.cpp` 的 `CPU_TEST_METAL` 路径提供。光学数值、导出全码值、物理场及分块边界分别由对应模块测试覆盖。
