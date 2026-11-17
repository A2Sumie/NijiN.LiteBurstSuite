# Suite Implementation Map

**[English](#suite-implementation-map) · [简体中文](#suite-实现映射)**

## Core Reconstruction

| Module | Implementation Entry Point |
|---|---|
| Unified Bayer / X-Trans, 6×6 black level, AF / shutter / OIS metadata | `heraw_sr2x.cpp` unpacking & calibration; `heraw_burst_planner.hpp` |
| C0 tiled Stage 4/5 & `1×` True-Sampled RGB | `heraw_gpu_backend.hpp::run_stage45_streamed` (`1x` / `2x`), Metal / Vulkan kernels & `heraw_cpu_stage45.hpp` |
| C1/C2 back-projection & color-line | `heraw_suite_core.hpp::compact_ibp` / `color_line_refine` |
| C3 directional lucky weighting, C6 half-stack / KLT | `directional_lucky_weights` / `half_stack_denoise` |
| C7 exposure normalize, C10 row flow, C13 silicon residual | `exposure_normalize` / `row_rolling_shutter_smooth` / `silicon_residual_cleanup` |
| C5/C12 lateral CA & OIS shift | `heraw_suite_optics.hpp::analyze` → per-backend LCA/OIS sample offsets |
| C8 subset/anchor selection, C9 OLPF kernel | `select_phase_diverse_subset` / `make_olpf_psf_pc` |
| C11 output, `tiff-hq`, `--no-tool-exif` & EXIF/ICC | `SOF3` RGGB & 3-comp LinearRaw, `tiff-hq` shot-noise YCoCg Deflate, `upsert_software_tags` in `heraw_exif_io.hpp` |
| Hardware cache/topology, battery & discrete GPU policy | `heraw_cpu_cache.hpp`, Vulkan discrete vs iGPU scoring in `heraw_gpu_vulkan.hpp` |
| Bilingual UI & `.lng` i18n manager | `heraw_gui_i18n.hpp`, `lang/zh-CN.lng`, `lang/en-US.lng`, `heraw_sr2x_gui_mac.mm`, `heraw_sr2x_gui_win.cpp` |

The working domain is black-level-subtracted camera-linear RAW. White balance is applied inside chroma-difference reconstruction and inverted before writing RAW masters. Motion flows are reference-to-source in half-resolution units; CFA queries are indexed from active-area origins. Frame timestamps match input ordering, and the anchor frame may sit in the middle of the burst sequence.

Original input indices are preserved across anchor reordering; when subsecond timestamps are absent, strobe still strides by original frame index and velocity is reported per frame index. Packed RGB channels group fields in `--fields` order and record exact channel names inside each PNG.

Tile scheduling aligns ROIs to CFA and pyramid periods, with halos covering flow shifts and filter support. Global statistics are gathered first, then streamed across tile cores. Full-frame camera-linear canvases remain `float32` through back-projection, metrology, and final 16-bit quantization. Peak memory comprises RAW inputs, full-frame outputs, enabled extra layers, and per-tile scratch. Export-only runs for fields or dataset tiles bypass unrequested full-frame stages.

From `2.0.1` onward, outer dispatch cores and filter dependency halos are decoupled: GPU core ceiling is `2640`, CPU outer core ceiling is `960`, and inner DCT micro-tiles stay cache-resident; device buffer ceilings and working-set budgets govern actual splitting. DCT uses the coarsest pyramid support, while CPU chroma levels and RL consume exact dependency bands. Metal and Vulkan global statistics export local $\eta$; C6 runs independent half-stack denoising where phase diversity holds and falls back smoothly to analytical fusion variance elsewhere. Log residuals sample a fixed cross-CFA grid, while IBP updates consume all valid source samples.

## Physical Extraction & Creative Workflows

`heraw_suite_features.hpp` implements on-demand physical field evaluation, camera homography vs relative parallax, Clean Plate / Virtual ND / Strobe / highlight lower-envelope compositing, separate transient layers, interactive 2.5D WebGL HTML generation, and linear photon-domain bokeh refocus.

Every physical field records `observable / units / measured / threshold / confidence`. Observable relative inverse depth carries a global sign convention and interactive flip toggle in WebGL. Symmetrized bokeh exchange preserves constant images and total radiant energy. `sigma` carries native camera-linear standard deviation units, normalized for 16-bit PNG preview by its finite maximum (`display_scale`, recovered via `code / 65535 * display_scale`).

Optics metrology (`heraw_suite_optics.hpp`) provides CFA-aware `8×` linear RAW stellar ePSF / slanted-edge MTF50 (`cycles/raw-pixel` with sample counts, method, and status), LCA, drift, rolling-shutter shear, `.lcp` / JSON / passport card export, and paired self-supervised dataset tile generation.

## Specification Calibration

| Earlier Design Parameter | Live Engineering Definition & Acceptance |
|---|---|
| Fixed resolution multiplier / idealized Gaussian covariance | Validated against measured phase diversity, forward residual, noise fit, and per-sample stratification |
| Fixed cache hit rate / memory & ms estimates | Profiled via tile scratch, full working set, target hardware, and per-stage wall cost (`docs/PERFORMANCE.md`) |
| Fixed green count per 2×2 X-Trans cell | Per-site CFA query averaged by true count (`20G/8R/8B` over `6×6` period) |
| Megapixel-inferred OLPF model | Explicit camera model table + manual kernel flag (`gauss16` on EOS R1) |
| Qualitative optics/depth grade titles | Emits concrete method, units, sample counts, and validity conditions alongside controlled ground truth |
| External software compatibility | `SOF3` verified via LibRaw & `tifffile`/`imagecodecs`; named TIFF channels verified by independent readers |
| Initial 10-field copy | 11 common selectable fields (`motion/phase/focus` default) + advanced geometry fields |
| Strictly modal-free claim | Native open/save/engine pickers use OS dialogs; recommendations and execution telemetry stay inline |

## Test Entry Points

- `tests/test_cfa_metadata.cpp`: active CFA/black level, constant channels, AF metadata on real RAWs.
- `tests/test_suite_core.cpp`: exposure ratios, fixed banding, forward residuals, half-stack noise & texture preservation.
- `tests/test_suite_features.cpp`: moving foreground, anchor-frame person removal, homography/parallax, temporal ordering, energy conservation.
- `tests/test_suite_optics.cpp`: known MTF/LCA recovery, optics & dataset format readback.
- `tests/test_suite_io.cpp`: DNG/TIFF extrema, full code sweep, strip/tile boundaries, write failures, `--no-tool-exif` Tag 11/305 checks.
- `tests/test_suite_gpu.cpp` / `tests/test_suite_cpu.cpp`: full-frame vs tiled seam parity on real RAW ROIs.
- `tests/test_suite_cli.py` & `tests/test_suite_gui.py`: full ASan CLI regression (`1×`/`2×`/`both`/`tiff-hq`/`--no-tool-exif`), GUI preferences, `Cmd-Q`, `.lng` i18n, log export, and 4-tab screenshots.

---

# Suite 实现映射

## 核心重建

| 模块 | 实现入口 |
|---|---|
| 统一 Bayer / X-Trans、6×6 黑电平、AF/快门/防抖元数据 | `heraw_sr2x.cpp` 解包与校准；`heraw_burst_planner.hpp` |
| C0 分块 Stage 4/5 与 `1×` 真采样 RGB | `heraw_gpu_backend.hpp::run_stage45_streamed`（`1×` / `2×`），Metal/Vulkan 及 `heraw_cpu_stage45.hpp` |
| C1/C2 回投与色线 | `heraw_suite_core.hpp::compact_ibp` / `color_line_refine` |
| C3 方向择优、C6 半栈/KLT | `directional_lucky_weights` / `half_stack_denoise` |
| C7 曝光匹配、C10 行流、C13 硅片残差 | `exposure_normalize` / `row_rolling_shutter_smooth` / `silicon_residual_cleanup` |
| C5/C12 色差与防抖光轴 | `heraw_suite_optics.hpp::analyze` → 各后端 LCA/OIS 采样偏移 |
| C8 子集/锚点、C9 低通核 | `select_phase_diverse_subset` / `make_olpf_psf_pc` |
| C11 输出、`tiff-hq`、`--no-tool-exif` 与 EXIF/ICC | `SOF3` RGGB 与三分量 LinearRaw、`tiff-hq`、散粒噪声量化、`upsert_software_tags`（`heraw_exif_io.hpp`） |
| 缓存拓扑、电池与独显调度策略 | `heraw_cpu_cache.hpp`，`heraw_gpu_vulkan.hpp` 独显/核显优先级判断 |
| `.lng` 多语言引擎与双端界面 | `heraw_gui_i18n.hpp`、`lang/*.lng`、`heraw_sr2x_gui_mac.mm`、`heraw_sr2x_gui_win.cpp` |

数据域为扣黑后的 camera-linear RAW，白平衡在重建色差域使用并在输出底片前还原。流为参考→源、half-resolution 像素单位；CFA 以 active-area 原点为基准。时间戳与帧数组同序，锚点可以位于时间序列中间。

原始输入索引随锚点重排保留；缺少亚秒时间时，频闪仍按原序列抽帧，速度量纲为每输入帧索引。Packed RGB 按 `--fields` 的选择顺序分组，并在每张 PNG 中写明实际 R/G/B 字段。

分块调度按 CFA 与金字塔周期对齐 ROI，halo 包含流偏移及滤波支持域；全局统计先计算，再输出各 core。全幅 camera-linear 画布使用 float32，保持回投、计量和最终 16-bit 量化之前的数值精度；总内存由 RAW 帧、全幅结果、选中附加层及 tile scratch 组成，验收记录实际尺寸与峰值。仅导出物理场或数据块时直接使用对应数据流。

2.0.1 起将外层调度和滤波依赖域分开：GPU core 上限 2640，CPU 外层 core 上限 960，内部 DCT 微瓦片保持缓存局部性；设备 buffer 限制与工作集预算决定实际拆分。DCT 使用最粗层支撑域，CPU 色差各级和 RL 使用各自所需区域。Metal/Vulkan 全局统计同时导出局部 η，C6 在相位条件成立时使用独立半栈，其余区域沿解析方差路径使用融合 η。日志残差采用固定跨 CFA 样本，IBP 图像更新使用全部有效源样本。

## 物理提取与创意工作流

`heraw_suite_features.hpp` 提供按需字段计算、相机单应与相对视差、空镜/ND/分身/高光下包络、独立瞬态层、WebGL 和线性散景。

物理字段记录 `observable / units / measured / threshold / confidence`。可观测的相对逆深度含整体符号规约，WebGL 支持翻转方向。散景的对称交换算子保持常量图像及总光量。sigma 的原始量纲为 camera-linear 标准差，显示图以实际有限最大值为白点并记录 `display_scale`，按 `code / 65535 * display_scale` 还原。

光学模块提供 CFA-aware RAW 8× 星点/斜边测量、LCA、漂移与行剪切、LCP/JSON/图卡和配对数据导出。MTF 单位为 cycles/raw-pixel，测量同时保留取样数、方法和状态。

界面共享同一组选帧和输出状态；主页快捷入口直接切换对应工作区。选片导出保留原 RAW 字节并另写选片清单。

## 规格校准

| 历史设计参数 | 当前工程定义与验收方式 |
|---|---|
| 固定解析力倍数、严格高斯/已知协方差 | 使用实测相位、前向残差、噪声估计与样本分层数值比较 |
| 固定缓存命中、内存及毫秒预算 | 记录 tile scratch、完整工作集、机器和启用阶段耗时 |
| X-Trans 每个 2×2 固定绿像素数 | 逐位置查询 CFA 并按实际数量平均；36 格为 20G/8R/8B |
| 机型像素数推断 OLPF | 显式机型表与手动核选择；EOS R1 使用 gauss16 |
| 光学/深度精度称号 | 输出方法、量纲、采样数和有效条件；与可控真值对比 |
| 外部软件兼容 | SOF3 经 LibRaw 与 tifffile/imagecodecs 读回；命名 TIFF 经独立 reader 检查 |
| 10 场 UI 文案 | 11 个可选字段，默认三项 |
| 完全零弹窗 | 文件/另存等必要对话框；建议和执行状态内联 |

## 验收入口

- `tests/test_cfa_metadata.cpp`：真实 RAW 的 active CFA/黑电平、常量通道及 AF 元数据。
- `tests/test_suite_core.cpp`：曝光比、固定条带、前向残差、半栈噪声与纹理。
- `tests/test_suite_features.cpp`：移动前景、参考帧去人、单应/视差、时间排序、能量守恒。
- `tests/test_suite_optics.cpp`：已知 MTF/LCA、光学与数据格式独立读回。
- `tests/test_suite_io.cpp`：DNG/TIFF 极值、全码值、条带/瓦片边界、写入失败与 `--no-tool-exif` Tag 11/305 校验。
- `tests/test_suite_gpu.cpp` / `tests/test_suite_cpu.cpp`：整图/分块与真实 RAW ROI 的接缝比较。
- `tests/test_suite_cli.py` 与 `tests/test_suite_gui.py`：ASan 全链路测试（含 `1×`/`2×`/`both`/`tiff-hq`/`--no-tool-exif`）、偏好契约、Cmd-Q、`.lng` 多语言、日志导出及四页截图。

主流程通过 `--plan-json`、`--backend cpu|gpu` 和实际 RAW 输出执行；构建产物使用 workspace freshness/entry 检查。
