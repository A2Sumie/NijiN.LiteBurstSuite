# Suite 实现映射

## 核心重建

| 模块 | 实现入口 |
|---|---|
| 统一 Bayer / X-Trans、6×6 黑电平、AF/快门/防抖元数据 | `heraw_sr2x.cpp` 解包与校准；`heraw_burst_planner.hpp` |
| C0 分块 Stage 4/5 | `heraw_gpu_backend.hpp::run_stage45_streamed`，Metal/Vulkan 及 `heraw_cpu_stage45.hpp` |
| C1/C2 回投与色线 | `heraw_suite_core.hpp::compact_ibp` / `color_line_refine` |
| C3 方向择优、C6 半栈/KLT | `directional_lucky_weights` / `half_stack_denoise` |
| C7 曝光匹配、C10 行流、C13 硅片残差 | `exposure_normalize` / `row_rolling_shutter_smooth` / `silicon_residual_cleanup` |
| C5/C12 色差与防抖光轴 | `heraw_suite_optics.hpp::analyze` → 各后端 LCA/OIS 采样偏移 |
| C8 子集/锚点、C9 低通核 | `select_phase_diverse_subset` / `make_olpf_psf_pc` |
| C11 输出 | SOF3 RGGB 与三分量 LinearRaw；`heraw_exif_io.hpp` |

数据域为扣黑后的 camera-linear RAW，白平衡在重建色差域使用并在输出底片前还原。流为参考→源、half-resolution 像素单位；CFA 以 active-area 原点为基准。时间戳与帧数组同序，锚点可以位于时间序列中间。

分块调度按 CFA 与金字塔周期对齐 ROI，halo 包含流偏移及滤波支持域；全局统计先计算，再输出各 core。全幅 camera-linear 画布使用 float32，保持回投、计量和最终 16-bit 量化之前的数值精度；总内存由 RAW 帧、全幅结果、选中附加层及 tile scratch 组成，验收记录实际尺寸与峰值。仅导出物理场或数据块时直接使用对应数据流。

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
- `tests/test_suite_io.cpp`：DNG/TIFF 极值、全码值、条带/瓦片边界及写入失败。
- `tests/test_suite_gpu.cpp` / `tests/test_suite_cpu.cpp`：整图/分块与真实 RAW ROI 的接缝比较。
- GUI：偏好测试、Cmd-Q、四页截图与实际 CLI/遥测联动。

主流程通过 `--plan-json`、`--backend cpu|gpu` 和实际 RAW 输出执行；构建产物使用 workspace freshness/entry 检查。
