# Suite Validation Record

**[English](#suite-validation-record) · [简体中文](#suite-验收记录)**

Environments: Apple M1 Max / macOS arm64; Windows X570 (Ryzen 7 5800X3D + RTX 4080 SUPER Vulkan); Ryzen AI 9 HX 365 (16Plus Strix Point); Core i5-8259U (nuc-8-sumie).

For single-input speed comparisons, Stage 0–6 optimizations, and revised runtime budgets, see [Performance Analysis](PERFORMANCE.md).

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

- Default Photo, 5/6 frames, full-res DNG + TIFF (2.0.1 baseline): Z8 **38.68 / 43.37 s**, A9 III **16.50 / 20.27 s**, X-H2S **22.48 / 24.91 s** (`Z8S_8636–8644.NEF` first 5/6 frames). Wave-2 (`2.1.0`) timings appear in [Performance Analysis](PERFORMANCE.md).
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

默认流程的同输入速度对照、Wave-2 优化及预算修订见 [性能回归分析](PERFORMANCE.md)。

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

- C++17 `-Wall -Wextra -Werror`；36 个 CLI/GPU/算法源文件 ASCII 检查。
- Vulkan 24 个 kernels：glslc、glslang、spirv-val。
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
