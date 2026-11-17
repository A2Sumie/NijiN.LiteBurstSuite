# `NijiN.LiteBurstSuite` (`nijinLBS`) Specification Index & Engineering Roadmap

**[English](#nijinliteburstsuite-nijinlbs-specification-index--engineering-roadmap) · [简体中文](#nijinliteburstsuite-nijinlbs-总体规格书索引与四阶段实施定稿)**

> **Milestone & Brand Note**: The project and GitHub repository (`https://github.com/A2Sumie/NijiN.LiteBurstSuite`) carry the **`NijiN.LiteBurstSuite`** name (`nijinLBS`). **`v2.1.0`** is the **Final `v2.x` Release**.

Live implementation entry points: [Implementation Mapping](IMPLEMENTATION.md), [Validation Record](VALIDATION.md), [CLI Reference](../CLI.md), and [Changelog](../CHANGELOG.md). The four-phase roadmap tables below preserve the design staging; authoritative live behavior, numeric domains, and test entry points are mapped to source and tests in `IMPLEMENTATION.md`.

This directory houses the architecture and interaction specifications for Suite. Performance budgets are realized per operator and dataflow: reusing fusion variance, tiling by actual dependency regions, sharing intermediate buffers, and SIMD normalization. Live implementations, measured costs, and trade-offs are documented in [Performance Analysis](PERFORMANCE.md) using real 5–6 frame full-resolution bursts.

---

## 1. Specification Set (Four Core Documents)

| Document | Core Scope & Audit Summary |
| :--- | :--- |
| **Live Release (`v2.1.0` Final `v2.x`)**<br>[`../README.md`](../README.md) · [`../ALGORITHM.md`](../ALGORITHM.md) · [`../CLI.md`](../CLI.md) | Quick start, 6-stage physical SR derivation, `1×` True-RGB, visually-lossless `tiff-hq`, `--no-tool-exif`, `.lng` multi-language UI, log export, and hardware topology scheduling. |
| **Book I (Core Physics & Latent Extraction)**<br>[**`ROADMAP_CORE_AND_LATENT_PHYSICS.md`**](ROADMAP_CORE_AND_LATENT_PHYSICS.md) | 1. Pixel aperture $\text{sinc}$ nulls, diffraction windows, and **`[C0]` streamed tile + `fp16` seamless canvas**;<br>2. Six sensor families (OLPF-less 45–102MP, global-shutter A9 III, 16-tap `GD-LPF` EOS R1, 4-tap weak-OLPF 20–24MP, 33MP CoW, Fuji `6×6` X-Trans);<br>3. Core pipeline `C0–C13` (including `C13` in-burst silicon warm-pixel/banding subtraction and `C6a–c` half-stack cross-spectral Wiener + KLT);<br>4. Latent fields `L1–L10` observability gates and the **Red-Team Audit downgrade ledger**. |
| **Book II (Creative Output & Pro Workflows)**<br>[**`ROADMAP_CREATIVE_AND_WORKFLOW_FEATURES.md`**](ROADMAP_CREATIVE_AND_WORKFLOW_FEATURES.md) | **`W1–W14` product capabilities** aligned to Book I physical gates: 4-mode temporal synthesis, glint suppress & wide-baseline reflection split, parallax-gated 2.5D light field & photon-domain bokeh refocus, RAW `8×` optics passport & `.lcp`, named TIFF channels, external denoiser relay `--relay-raw`, and **`W14` AF focus & Top-K triage**. |
| **Book III (Desktop UI & Interaction Spec)**<br>[**`UI_SPECIFICATION.md`**](UI_SPECIFICATION.md) | **`1280 × 800 pt` workspace ("Tab 1 direct controls + 4 synced deep-dive tabs")**: keeps frequent controls (presets, motion modes, `1×`/`2×` scale, format, EXIF tool-info switch, fields, transient layer, relay denoise) one click away on Tab 1 while syncing live with Tabs 2–4. |

---

## 2. Four-Phase Engineering Roadmap (Historical Staging)

| Phase | Core Pipeline Modules (`C0–C13`) | Physical Fields (`L1–L10`) | Creative & Workflow Deliverables (`W1–W14`) |
| :--- | :--- | :--- | :--- |
| **Phase 1**<br>*Streamed Core & Universal CFA* | `C0` Streamed GPU/CPU micro-tiles + `fp16` canvas<br>`C8` Phase-diverse subset/anchor selection<br>`C11` Fuji `6×6` X-Trans + dual-mode DNG<br>`C12a` EXIF OIS/shutter/AF telemetry<br>`C13` In-burst silicon hot/banding cleanup | `L9` Subpixel drift & shutter-shock ratio<br>`L10c` Sensor residual & per-pixel $\sigma(x,y)$ | `W10` IBIS & shutter-shock chart<br>`W11` TIFF named extra channels<br>`W13` External denoise relay (`--relay-raw`)<br>`W14` AF inspection & Top-K original export |
| **Phase 2**<br>*Peak Acutance & Noise Model* | `C1` Tile IBP (2 iterations)<br>`C2` Cross-CFA color-line refinement<br>`C5a & C12b` Blind 2D LCA + per-frame OIS shift<br>`C6a–c` Half-stack Wiener + kernel pre-whitening + 3D KLT<br>`C9` Sensor-specific OLPF kernels | `L6` Aliasing coherence field<br>`L10a` 2D lateral CA vector field | `W9` Reusable `.lcp` profile export<br>`W12` `Sci` paired self-supervised dataset tiles |
| **Phase 3**<br>*Temporal Synthesis, Rolling Shutter & Optics* | `C3` Directional lucky frame weighting<br>`C5b` Spatially varying elliptical PSF RL<br>`C7` Multi-exposure AEB normalization<br>`C10` Row-rolling-shutter shear spline | `L7` Subject velocity / rigid classification<br>`L10b` Linear RAW `8×` stellar ePSF / 5-zone slanted-edge MTF50 | `W1` Clean Plate<br>`W2` Handheld Virtual ND<br>`W3` Transient highlight extraction<br>`W4` Action Strobe trails<br>`W8` Dual-grade lens optics passport |
| **Phase 4**<br>*Micro-Baseline Light Field & Intrinsic Media* | `C4` Occlusion-boundary separation & completion | `L1` Epipolar inverse depth ($\Delta d_{\max} \ge 8\text{ px}$)<br>`L2` Signed defocus<br>`L3` Plane normals & occlusion $\alpha$<br>`L4/L5/L8` Glint / PWM flicker / Schlieren BOS | `W5` Glint suppress & wide-baseline reflection split<br>`W6` 2.5D interactive WebGL parallax viewer<br>`W7` 16-bit photon-domain bokeh refocus |

---

# `NijiN.LiteBurstSuite` (`nijinLBS`) 总体规格书索引与四阶段实施定稿

> **品牌与仓库升级说明**：项目与 GitHub 仓库（`https://github.com/A2Sumie/NijiN.LiteBurstSuite`）名称为 **`NijiN.LiteBurstSuite`**（简称 **`nijinLBS`**）。**`v2.1.0`** 为 **`v2.x` 系列最终收官版（Final v2.x Release）**。

当前实现入口：[实现映射与规格校准](IMPLEMENTATION.md)、[验收记录](VALIDATION.md)、[CLI](../CLI.md)、[变更记录](../CHANGELOG.md)。下面四阶段表保留设计分期；实际数值域、参数与验收入口由实现说明对应到源码及测试。

本目录保存 Suite 的架构与交互设计。性能预算按算子与数据流逐项落实：复用融合方差、按依赖域分块、共享中间结果及 SIMD 归一化。当前实现、实测开销和优化取舍见 [性能回归分析](PERFORMANCE.md)，主验收使用真实 5–6 帧连拍。

---

## 一、定稿文档体系（四件套）

| 文档 | 核心内容与审查定稿要点 |
| :--- | :--- |
| **v2 最终收官基线 (`v2.1.0`)**<br>[`../README.md`](../README.md) · [`../ALGORITHM.md`](../ALGORITHM.md) · [`../CLI.md`](../CLI.md) | 快速上手、六步物理超分推导、`1×` 真采样 RGB、视觉无损 `tiff-hq`、`--no-tool-exif`、外置 `.lng` 多语言、完整日志导出与异构调度策略。 |
| **演进第一册（底层核心与物理提取）**<br>[**`ROADMAP_CORE_AND_LATENT_PHYSICS.md`**](ROADMAP_CORE_AND_LATENT_PHYSICS.md) | 1. 像素孔径 $\text{sinc}$ 零点、光圈衍射窗口与 **`[C0]` GPU / CPU 微瓦片流式底座 + `fp16` 无缝画布**；<br>2. 六大传感器家族矩阵（无低通 45–102MP、全域快门 A9 III、16点 `GD-LPF` EOS R1、4点弱低通 20–24MP、33MP CoW A7 V、富士 `6×6` X-Trans）；<br>3. 核心重建 `C0–C13`（含带高通防泄漏门禁的 **`C13` 亮帧内硅片温点/行列条带自扣除** 与 **`C6a–c` 多块集平均半栈互谱维纳 + 核预白化 + 色空 KLT**）；<br>4. 潜在物理量 `L1–L10` 可观测门槛及 **第四章：8 项反过度乐观红队审查与降级理由台账**。 |
| **演进第二册（创意成片与工作流）**<br>[**`ROADMAP_CREATIVE_AND_WORKFLOW_FEATURES.md`**](ROADMAP_CREATIVE_AND_WORKFLOW_FEATURES.md) | 与底层严格分册的 **`W1–W14` 十四项产品能力**（一拍四出时间重编程、碎高光抑制与引导式宽基线消反光、视差门禁受控 2.5D 光场与光子域散景重对焦、原始 RAW `8×` 双级计量镜头护照与 `.lcp`、TIFF 原生内嵌命名通道、外部 AI 降噪白噪声直通接力 `--relay-raw`，以及 **`W14` 对焦点主体审片快筛台**）。 |
| **演进第三册（交互与 UI 定案）**<br>[**`UI_SPECIFICATION.md`**](UI_SPECIFICATION.md) | **`1280 × 800 pt`「首页高频直达 + 四标签深度展开（双向状态同步）」工作台**：首页直接保留常用输出、场景模式、`1×`/`2×` 切换、EXIF 印记控制及四处传送门，日常 90% 操作零翻页闭环，高阶调参与内联矢量图表在 Tab 2–4 铺开并实时回显首页。 |

---

## 二、定稿四阶段（Phase 1 → Phase 4）工程路线图

按 **「高确定性、高信噪比、高实用回报优先，严苛物理门禁兜底」** 原则排定：

| 阶段 | 底层核心模块 (`C0–C13`) | 同步提取物理信息 (`L1–L10`) | 交付创意与工作流能力 (`W1–W14`) |
| :--- | :--- | :--- | :--- |
| **Phase 1**<br>*双模态流式底座、全传感器通吃与高频刚需* | `C0` **GPU `2048²` / CPU `256²` L2 微瓦片流式底座 + `fp16` 无缝画布**<br>`C8` 30–120fps 四象限最大熵跳帧选相<br>`C11` **富士 `6×6` X-Trans 统一核 + 双模式 DNG**<br>`C12a` EXIF 防抖/快门/对焦点框遥测<br>`C13` **亮帧内实时硅片温点与行列条带自扣除** | `L9` 亚像素漂移轨迹与快门微震比<br>`L10c` 双坐标硅片本底图与逐像素误差棒 $\sigma(x,y)$ | `W10` 防抖与快门微震心电图<br>`W11` **默认 16-bit TIFF 原生内嵌命名物理通道**<br>`W13` **外部降噪直通接力 (`--relay-raw`)**<br>`W14` **对焦点主体审片与 Top-K 快筛台** |
| **Phase 2**<br>*极限解析力封顶与新噪声模型落地* | `C1` 瓦片内 2 步数据一致性回投（IBP）<br>`C2` 跨通道拜耳色线联合超分<br>`C5a & C12b` 盲二维倍率色差 + OIS 逐帧微补偿<br>`C6a–c` **集平均奇偶半栈互谱维纳 + 核协方差预白化 + 色空三维 KLT / `BM3D-Lite`**<br>`C9` 分型 OLPF（4点双折射 / 16点 `GD-LPF`）复合核 | `L6` 混叠载波频率 $\mathbf{f}_0$ 与微纹理相干场<br>`L10a` 二维色散矢量场 | `W9` `.lcp` 镜头色散校正档导出<br>`W12` `Sci` 机身专属高置信自监督真值切片器 |
| **Phase 3**<br>*时间重编程成片、抗果冻与双级光学计量* | `C3` 正交手震方向频域择优（Lucky Weighting）<br>`C5b` 空间可变椭圆 PSF 反卷积<br>`C7` 不等曝光 AEB 泊松最优 HDR 超分<br>`C10` 非堆栈电子快门一维行带果冻样条 | `L7` 动体速度/加速度/刚性流变分类<br>`L10b` 原始线性 RAW `8×` 星点 ePSF / 五门禁远场斜边 & 大位移暗角/畸变 | `W1` **零路人纯净空镜（Clean Plate）**<br>`W2` **手持免滤镜虚拟慢门 ND1000 流水**<br>`W3` **流星 / 萤火虫瞬态剥离回收层**<br>`W4` 高速运动频闪分身轨迹合成<br>`W8` **双级计量（星点金标准 / 远场五门禁）镜头体检护照** |
| **Phase 4**<br>*受控微基线三维光场与本征介质深水区* | `C4` 近景遮挡边缘双层剥离补全（Disocclusion）<br>物理场空间门控可选外部恢复插件接口 | `L1` 受控极线视差逆深度场（$\Delta d_{\max} \ge 8\text{ px}$）<br>`L2` 近摄/包围散焦符号场<br>`L3` 平面倾角与遮挡率 $\alpha$<br>`L4/L5/L8` 碎高光/频闪光/热浪纹影场（BOS） | `W5` 碎波光抑制 & 引导式宽基线展柜消反光<br>`W6` 2.5D 单文件 WebGL 光场视差包<br>`W7` 16-bit 线性光子域虚拟大光圈散景重对焦 |
