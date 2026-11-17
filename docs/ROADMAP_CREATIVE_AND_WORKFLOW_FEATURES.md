# Book II: Creative Synthesis, Optical Triage & Pro Workflow Specification (English Overview)

> **Executive Summary (Historical Design Book — Live `v2.1.0` Reference in [`../CLI.md`](../CLI.md) & [`IMPLEMENTATION.md`](IMPLEMENTATION.md))**: Strictly decoupled from Book I's core physics and observability gates, this volume defines the fourteen creative, optical metrology, and dataset deliverables (`W1–W14`): Clean Plate (`W1`), Handheld Virtual ND (`W2`), Transient Max-Hold (`W3`), Action Strobe (`W4`), Glint Suppression & Wide-Baseline Reflection Split (`W5`, $\ge 40\text{ px}$ 2× parallax), Interactive 2.5D Light-Field HTML (`W6`, $\Delta d_{\max} \ge 8\text{ px}$), Photon-Domain Bokeh Refocus (`W7`), Dual-Grade Optics Passport (`W8`), Adobe `.lcp` Export (`W9`), IBIS & Shutter-Shock Telemetry (`W10`), Embedded TIFF Named Fields (`W11`), Self-Supervised `Sci` Dataset Tile Factory (`W12`), External Denoiser Relay `--relay-raw` (`W13`), and AF Subject Triage & Top-K Export (`W14`). The full original Chinese specification follows below.

---

# 第二册：`NijiN.LiteBurstSuite` 衍生创意成片、选片体检与专业工作流规格书（红队审查定稿版）

> **文档定位**：本册与 [`ROADMAP_CORE_AND_LATENT_PHYSICS.md`](ROADMAP_CORE_AND_LATENT_PHYSICS.md) 严格分离，收录经可观测性与工程成本红队审查定稿的 **`W1–W14` 十四项衍生摄影成片、选片质检护照与专业交付工作流功能**。所有依赖条件均已对齐第一册物理门禁，杜绝不可靠输出。

---

## 一、`W1–W14` 定稿功能矩阵与物理门禁一览

| 编号 | 所属模块 | 功能名称 | 底层依赖 | 物理生效门禁（诚实边界） | 核心交付物与典型场景 |
| :---: | :--- | :--- | :---: | :--- | :--- |
| **`W1`** | 时间重编程 | **零路人纯净空镜（`Clean Plate`）** | `L7` + `C4` | $N \ge 5$ 且移动行人在各帧位置错开未长期驻留同一点 | `2×` 超分无路人 `.dng` / `.tif`（景区古建、城市街景） |
| **`W2`** | 时间重编程 | **手持免滤镜虚拟慢门 ND1000** | `L7` + Stage 4 | $N \ge 6$；静态区超分锐化 + 流体区全帧线性等权积分 | 礁石建筑刀锐 + 瀑布/海浪/流云丝绸雾化成片 |
| **`W3`** | 时间重编程 | **瞬态高光剥离回收层（`Transient Max-Hold`）** | Stage 3 残差 | 正向瞬态信噪比 $> 3.5\sigma$ | 星空去飞机卫星主图 + 独立 16-bit 真流星/萤火虫「变亮」叠回层 |
| **`W4`** | 时间重编程 | **运动频闪分身轨迹合成（`Action Strobe`）** | `L7` + 掩膜 | 刚体运动目标与静态背景可分（极契合 30–120fps 机型） | 飞鸟、滑板、体育动作序列分身超清合成 |
| **`W5`** | 空间本征分离 | **碎高光抑制 & 引导式宽基线展柜消反光** | `L1` + `L4` | 原地连拍仅做水面/树叶碎波光抑制；**整窗玻璃去反光强制要求横向平移 `5–15 cm`（双层视差差 $\ge 40\text{ px}$）且未过曝** | 通透漫反射主图 + 独立反射虚像层（博物馆展柜、车窗） |
| **`W6`** | 光场与重对焦 | **真物理 2.5D 光场视差交互包** | `C4` + `L1` | **全图极线视差跨度 $\Delta d_{\max} \ge 8\text{ px}$**（中长焦自然连拍或广角微横移 `2–5 cm`） | 单文件零依赖 WebGL `.html` / 微环绕视频 |
| **`W7`** | 光场与重对焦 | **16-bit 线性光子域虚拟大光圈散景重对焦** | `L1` (+ 近摄 `L2`) | 同上（$\Delta d_{\max} \ge 8\text{ px}$）；基于无量纲相对逆深度差卷积 | 小光圈全清合焦后在线性光子域渲染能量守恒圆盘散景 |
| **`W8`** | 选片与体检 | **双级计量镜头光学体检护照** | `L10` + `C12` | **原始线性 RAW `8×` 超采样**；星空标「绝对计量级」，日常五门禁通过标「远场估计级」，近景不等距直接拒检歪轴 | 中心/四角 MTF、像散椭圆、静态歪轴（自动剥离 OIS 瞬态偏心）可视化图卡 |
| **`W9`** | 选片与体检 | **可复用镜头色散与暗角校正档导出** | `C5a` + `L10` | LCA 任意硬边可提；暗角与畸变 $(k_1,k_2)$ 要求帧间漂移 $\ge 50\text{ px}$ | 标准 Adobe `.lcp` / `.json`（拍一组连拍反哺所有单张 RAW） |
| **`W10`** | 选片与体检 | **防抖（IBIS）与快门微震心电图** | `C8` + `L9` | $N \ge 2$ 即 100% 可提 | 毫秒级亚像素漂移轨迹、四象限覆盖热力图、首帧快门震动比 |
| **`W11`** | 交付与接力 | **默认 16-bit TIFF 原生内嵌命名物理通道** | 已选 `L#` 场图 | 零额外门槛，单文件封装 | 单张 `.tif` 内嵌 `ExtraSamples` 命名通道，PS/Affinity 打开即见通道蒙版 |
| **`W12`** | 交付与接力 | **`Sci` 机身专属自监督真值切片器** | `L6` + `L10` | 仅截取 `Phase > 0.95` 且 `Motion == 0` 黄金静态区 | 配对 `[1× 原始带噪块 ↔ 2× 线性无偏真值 + 逐像素 σ_map]` |
| **`W13`** | 交付与接力 | **外部第三方降噪直通接力（`--relay-raw`）** | Stage 1–4 + `C11d` | 旁路 Stage 5 保持原生白噪声功率谱（速度翻倍） | `2× RGGB` DNG 无损接力 Lightroom AI 降噪 / Topaz（含富士 X-Trans 借道解锁） |
| **`W14`** | 选片与体检 | **对焦点主体审片与 Top-K 原片快筛台** *(审查新增)* | `C12a` + Stage 0 | 无需跑超分，毫秒级读取 EXIF `AFPoint` 对焦点框局部锐度 | 高速连拍（如 120fps 打鸟/人像）秒级锁定**眼睛/对焦点最锐皇冠原片**并导出选片清单 |

---

## 二、红队审查后新增与精炼的三大高确定性亮点

1. **`W14` 对焦点主体精准审片器（`--inspect-af` / 智能向导集成）—— 零风险高频刚需**：
   传统全图平均锐度打分在大光圈人像/打鸟时极易被背景树林或前景草地欺骗。直接利用 `C12a` 提取的拍摄瞬间真实对焦点框（`FocusLocation / AFPointSelected`）计算主体局部高频能量与微动糊比值：既确保合成时 100% 拿主体眼睛最锐的一帧做定格锚点，又能单独作为 `30–120 fps` 海量连拍的毫秒级选片神器。

2. **`W13 + C13`「欠曝保高光连拍 + 实时硅片条带自扣除」单快门高动态工作流**：
   相比繁琐易糊片的慢速包围曝光，摄影师日常更常采用高速欠曝 `1–2 EV`（如富士 `DR400`、尼康亮部重点测光）保护高光。依托第一册 `C13` 亮帧内实时硅片温点与行列条带自扣除 + 多帧叠加，暗部硬提 `+2 ~ +3 EV` 彻底告别横纵条纹与彩噪，实现零失败率的手持单快门高动态超分。

3. **`W5 / W6 / W7`「被动微抖连拍」与「引导式微横移连拍（Guided Micro-Sway）」清晰分级**：
   在 GUI 智能向导与文档中明确区分两类拍摄手势：
   - **常规原地稳持连拍**：主攻极致 `2×` 超分、`W1–W4` 时间重编程、`W13` 外部降噪接力、`W10/W14` 选片心电图；
   - **刻意横向平移机身 `2–10 cm` 连拍**：一键满血解锁稠密真深度 `_field_depth`、`W6` 2.5D 光场视差、`W7` 光子域散景重对焦及 `W5` 展柜玻璃去反光。

---

## 三、UI 承载定案索引

所有衍生功能在桌面端的完整控件排布、内联矢量可视化看板与物理门禁示能规范已全部定案于第三册 [**`UI_SPECIFICATION.md`**](UI_SPECIFICATION.md)（采用 **`1280 × 800 pt` 左常驻连拍/帧检视底盘 + 右侧四标签工作区舞台 `[1 超分与主输出] · [2 时空与创意] · [3 物理场与数据] · [4 镜头与机身]`**，将 `W1–W14` 与 `L1–L10` 全部零弹窗舒展承载）。
