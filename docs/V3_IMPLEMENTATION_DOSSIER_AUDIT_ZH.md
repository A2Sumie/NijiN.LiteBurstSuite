# `nijinLBS` 3.0.0 实施卷宗独立审计：§0–§9

**裁决：主重建、原生 RAW 会话和三项手持光学已有可复现实现。优先修两条工作流：一是侧挂合成在正确位置改动正确像素，并如实交付来源图；二是用户选了哪个报告或文件，就执行对应任务并写出该文件。** 科学量命名、内存预算和几处数学表述也需按消费者实际行为订正。

基线：2026-11-20，`v3/wavelet-lossy`，HEAD `834df8c` **加当时工作树**，macOS arm64 / Apple M1 Max / Apple Clang 21。审查对象是[实施卷宗](V3_IMPLEMENTATION_DOSSIER_ZH.md)原第 1–328 行。其末尾的[§10 GUI 分组讨论稿](V3_IMPLEMENTATION_DOSSIER_ZH.md#gui-layering)供产品定案；§0–§9 保留被审原文。下列“实跑”来自本轮进程和文件，“源码”是调用链核对，“历史”特指[原验收台账](VALIDATION.md#L9)的记录。

## 1. 按影响排期

| 优先级 | 影响用户的事实 | 建议的完成条件 |
|---|---|---|
| **P1 · 生成式合成、坐标与来源** | 同一 A9 III 实拍组，`hole_only=true` 与全图替换逐像素相同；两层各占一半的外部先验被记录成“实测 + 标定”，丢了生成位。原生 RPC 对旋转过的同尺寸 RGB 先验取错边。 | CLI 和 RPC 各过同一份跨坐标格的有洞/无洞、旋转彩条和叠层来源用例，再开放对应角色。 |
| **P1 · 请求的文件缺失** | 单出 NIB、DNG + NIB 均返回 `0` 但目标 NIB 不存在；增加 RGB 输出后就有包。 | 从产物推导画布依赖；成功前检查每个请求文件存在、可打开、阶段正确。 |
| **P1 · 科学选择和物理量** | GUI 式“只选 O7”与“只选 O8”得到完全相同的全套 JSON。O7 输出图像流速，O9 输出位移和背景颜色，与“波前模态”“大气色散角/折射率”不同。 | 解耦算法选择与报告路径；公开实际执行集、单位和标定来源；相对指标按相对量显示。 |
| **P1 · 内存承诺** | DxO 四帧、2×、`1 GiB` 目标、31 条带：程序报 `1575.99 MiB`，操作系统最大 RSS **2864.36 MiB（2.797 GiB）**。实际 ROI 来自内存压缩块。 | GUI 改称“调度目标”，分别列整进程峰值、条带工作集与帧缓存；要提供硬限额时，以系统实测为门槛。 |
| **P1 · 科学求解口径** | Nikon HE 的 Q4 区间是配准样本经一维变换后估出来的；Q10 是四个探针中任一通过就放行整次清洁。 | 发布文案区分“代理区间/全局门禁”和“原始编码区间/逐频带约束”，增加独立码字和定向平场对照。 |
| **P2 · 文案、分组与复现** | §8 写错页签、侧挂落点和预设；§9.3 第三段原命令编译失败。旋焦的强度有效，正负号效果相同。 | 按 §10 迁移控件；旋焦先显示有效强度；用本文 §5 的编译方法或项目测试目标。 |

## 2. §0–§9 逐节对账

| 原卷宗 | 兑现的部分 | 需要订正或保留的精度边界 |
|---|---|---|
| **§0.1–0.3 基线、构建、模块** | 六个声明产物存在，`make -q all nijinLBS_asan` 返回 0；所列 `heraw_suite_*.hpp`、路由头、C 线协议头的非 ASCII 字节数为 0；Metal 源内 49 个 `kernel void`。普通手持输入可直接走光学与总线。 | [Makefile](../Makefile#L10) 按主机选目标：这台 Mac 的 `all` 有五项，另加 ASan；Linux 原生 `all` 取 Linux CLI。这里是依赖图即时状态。原生算子按字段/角色编排；把“核心完全没有外部名称”收窄为“无下游品牌分支”，GUI 本地化占位文案确实提到具体调用方。模块表的 `evaluate_fields` 对应现名 [`compute_fields`](../heraw_suite_features.hpp#L2046)。 |
| **§1.1 解码、有效区、DxO** | 中文路径 Z8 双帧原生服务和 `--inspect` 都给 `8256×5504`；几何含 `crop12,8`。DxO 四 CFA IFD、LUT、窗口和真实像元读回见 §5。WVDC [保留帧选择与容器时钟](../heraw_wvdc_router.hpp#L387)。 | `.jxs` 原生 RAW 路由校验 Bayer 码流；RGB JXS 有独立入口。会话代理与完整重建是两个消费者：X-Trans 代理的 2×2 采样会缺色，见 §3.1。 |
| **§1.2–1.3 样本权重、Tap-0、时钟** | 归一曝光的方差、`1:4` 权重用例与 O2 行均值旁路通过；PTS 原始分数和 DxO MakerNote 时序有独立字段。 | 主流程使用 `Noise::frame_variance` 和合成权重，Tap-0 晚于温点处理，O5/O7 也未读取该 tap。真实时钟名、序号回退与计量证书不是同一概念，详见 §4.1。 |
| **§2.1–2.3 配准、平场、快门** | [SIM(2) 补丁](../heraw_sr2x.cpp#L3162)、[SO(3) 行样条](../heraw_sr2x.cpp#L7896) 有主路径；R3/R4 合成斜坡/透视用例通过。快门模式确实影响融合。 | Q10 正确代码公式为 `1−abs(χ)²`。`nnls-custom` 在生产端用按帧索引等分的 32 箱和固定 `rear-curtain` 目标；是有作用的非负重加权，不是用户自定义任意实测曝光窗反演。 |
| **§3.1 重建、后端与 1×** | `fast/sinc/joint` 有分支和调用；[joint 在 16×16 块中选择 Schur 或回退](../heraw_sr2x.cpp#L8775)，[窗化 sinc](../heraw_sr2x.cpp#L10686) 进入 1× 输出。A9 八帧 IBP 对照的 16-bit 图平均绝对差为 `61.901197`。 | 同组 CPU / Metal 的 **1× 线性 NIB** 实测 `max_abs=0.00373364`，`RMSE=9.37e-5`，以 1 为峰值得 `80.57 dB`；“严格等价”应给量纲、容差和配置。卷内 `1.07e-4..2.52e-4` 是**子条带核**，不是最终输出的统一上界。四相秩函数确有消费；[生产装配](../heraw_sr2x.cpp#L8193)采用有限相位/色差基及正则，X-Trans 需自身 CFA 设计矩阵作为判据。 |
| **§3.2–3.3 条带、Q4/Q5** | 31 条带全幅运行成功；DxO 非均匀 LUT 区间、Probit 数值核、5/3 伴随、Q5 全饱和/混合边界用例通过。 | 源 RAW 重读、内存硬限与 Nikon 原始码字区间均需改写。`box` 分支取加权界、做半步钳位；`probit` 才算区间分数。Q5 的全饱和保护针对**该步更新**；下一拍 EV 由[规划器的 `clipped_fraction`](../heraw_sr2x.cpp#L12552)触发。 |
| **§4.1 潜在场与双级光学计量** | 几何、门禁、`wblur`、五区 MTF / 星点 ePSF、`.lcp` 具备[原生测量入口](../heraw_sr2x.cpp#L7057)；Cocoa 已能区分缺测值、零和明确拒检。 | `10.0032 / 10.0032 / 10.0447 mm` 来自有 10 mm 真值的[合成几何夹具](../tests/test_suite_v3_audit_oracles.cpp#L112)，不是实拍镜头标尺。`depth` 图、米制基线和近远距离要分别标单位；[尺度估计](../heraw_suite_features.hpp#L2010)含像元间距回退和代理政策。 |
| **§4.2 O1–O10** | 有观测器调度、`org.nijin.observer.v3` 报告、时钟/门禁字段；T5–T7 复现 `100 Hz`、运动几何、BOS 羽流/涡旋、受测振动/微动正负例。 | 应写明每个量来自光度、位移、尺度还是模型估计；“输出文件路径”会改变执行集，见 §4.2。 |
| **§5.1 玻璃和高光** | 黑柔/暖玻璃在真实照片 EXR 产生有符号 `scatter`，同时 DNG 字节保持一致。感知高光压缩在[显示色域变换后](../heraw_sr2x.cpp#L10182)有 OkLab 分支。 | [协方差函数](../heraw_suite_diffusion.hpp#L114)和 R1 用例成立；生产 `apply_optical_diffusion_linear` 的输出是图像、遥测及可选散射差量，没有逐像素协方差输出。渲染和不确定度传播分别验收。 |
| **§5.2–5.4 OP-O2 / O1 / O3** | 当前原生 `render_refocus` 已切三圆盘；等位移光谱节点和逐像素非负帽已实现；2 px 支持域腐蚀通过指定软边用例。 | “零振铃”、任意常色场、去色边和旋焦方向均有适用条件，见 §4.4；光学强度应标清 1× / 2× 画布尺度。 |
| **§6.1–6.3 容器、坐标、宿主与 CLI** | 64/128 B 布局、ND 维度、mmap 读取、分数中心钳位、取真实代理、缺源报错、四个通用算子、已绑定掩膜的逐像素消费有证据。 | 文件形式、有效采样、来源传播和执行阶段分别判定。`align`、`layers` 等可存储，`hole_only` 等无相应像素语义；原生在线算子与离线管线还有不同限制，见 §3。 |
| **§7 DNG / TIFF / EXR / EXIF** | 真实裁剪 TIFF 命名附加场与 3 通道 LinearRaw 通过；E12 的 LibRaw 带黑电平读回通过；独立 OpenEXR 读者证实 `beauty/scatter` 像素变化，见 §4.5。 | 支持 X-Trans **输入**，主输出的 CFA DNG 重排为 `2×2 RGGB`。`tiff-hq` 是按噪声量化 RGB、水平预测再 Deflate 的视觉有损路径，不是 YCoCg 无损模式；`0.48×` 是素材相关的体积记录。“不写工具 EXIF”与删除 GPS/来源路径是不同设置。 |
| **§8 工作台** | 两端控件构造和 argv 可定位；Mac 偏好/进程、8 张双语截图与门禁夹具通过。两份 `.lng` 的 516 个键集合一致；所查页名、手持光学、观测和总线的 29 项显示值与双语内建值吻合。 | 实际预设 `photo/astro/sci/arc`，侧挂在第三页；按组 argv 读取同一个挂包控件。当前检视/选中和批量执行有不同作用域。完整迁移与双端验收在[卷末 §10](V3_IMPLEMENTATION_DOSSIER_ZH.md#gui-layering)。 |
| **§9.1–9.3 闭环、边界与复现** | 上轮 Z8 常数代理、缺文件、4×1 U8/F32 与旋转 RLE、分数有效框、普通来源校验、重命名收编和 Python 增量状态的具名回归都有进展；本轮 SDK `15 passed, 0 skipped`。 | §9.2 正确列出临时 `.nib`、四个算子和完整 CFA 前向/伴随未作为 RPC 暴露；“光学全量槽位”“全部命令 EXIT 0”须订正。原生会话起于 S2，阶段脏位不是一次完整重建。Windows/Vulkan 本项采用历史实机记录。 |

## 3. 外部接口：三个不同的问题，三个最小反例

### 3.1 调用方收到哪一份图

- **上轮首要问题已闭环。** 默认 `NijinClient()` 现在选到 `nijinLBS-native`。中文目录中的 `Z8S_8636/8637.NEF` 返回 `S2_ALIGN`、`8256×5504`、`crop12,8:bayer:0112`；代理 `(1,341,512,3)`、标准差 `0.10071975`，第二帧位移 `(-0.807990,+0.295132)`、模 `0.860204 px`。同文件 `--inspect` 为 `0.86 px` / ZNCC `0.99`；缺失路径抛 `decode`。[双入口记录](../../../scratch/heraw-v3-dossier-audit-20261120/native_z8.log)
- **实拍解码正确与代理全彩是两项验收。** [宿主](../heraw_sr2x.cpp#L11672)对每个预览像素只看一个对齐的 2×2 CFA 块，缺某色就写 0。X-H2S 的 X-Trans 相位可以给全绿 2×2。在 384² 的首张实拍裁剪上，R、B 各有 `16,384` 个零；`G>0.02` 且 R 或 B 为零的像素有 **7,440**，同尺寸 A9 Bayer 为 **0**。[对照](../../../scratch/heraw-v3-dossier-audit-20261120/proxy_cfa.log)。建议按 CFA 周期/有效足迹聚合，另报缺测支持；导出完整 X-Trans 相位而非仅取左上四格作为几何签名。
- **切面的顺序决定数据含义。** [主路径](../heraw_sr2x.cpp#L10448)是外部合成 → 已有散景整形/光谱色散 → **NIB `fused.rgb`** → 玻璃扩散 → 重对焦等输出处理。DNG 在此前分出。`fused.coverage` 使用半格 `field_phase = 0.55·quad_cov + 0.45·cnt_cov`，其中锚帧自带覆盖；这是融合/相位支持评分，不能充当“这里没有任何实测光子”的布尔判据。建议给母片、效果切面、真实有效支持图分别命名并声明线性色域、坐标格及生效过的操作。

### 3.2 “仅填洞”、多层来源和缺文件

输入为同一组八张 Sony A9 III 实拍裁剪，固定第一帧作锚点，CPU、1×、关闭抖动。用宿主导出的身份创建外部包，先验图的 1×1 Chart 覆盖整个活动区；读回**主 CLI 写出的包**。可运行[四组对照脚本](../../../scratch/heraw-v3-dossier-audit-20261120/bus_crop_probe.py)及[结果](../../../scratch/heraw-v3-dossier-audit-20261120/bus_crops.log)：

| 只改变这一项 | 应观察到的区别 | 实际结果 |
|---|---|---|
| 源组 / 无先验，随后全图替换为 0.25 | 有纹理的底图变常数；适合做正对照 | 底图标准差 `0.043712`；全图替换平均绝对差 `0.177425`，来源 `0x10`。 |
| 将上述策略 `hole_only=false` 改成 `true` | 有支持的实拍区域应保留 | `192×192` 支持评分全部为正，两图却都是全幅 `0.25`，`max_abs=0`。[合成循环](../heraw_sr2x.cpp#L10403)没有取 `hole_only`。 |
| 先用生成先验 0.25 完全替换，再用标定先验 0.75 按 0.5 混合 | RGB `0.5`；保留生成和标定位，即 `0x10 OR 0x02 = 0x12` | 颜色正确，全部像素的来源却是 **`0x03`**（实测 + 标定）；整张 RGB 的 `origin` 仍为 4，与像素图冲突。`0<α<1` 时应合并**原有来源位**与新位，`α=1` 时才可丢掉旧贡献。 |
| `.nib` 是唯一产物 / DNG + `.nib` / RGB + `.nib` | 三种请求都交付侧挂包 | 前两种 exit `0` 且缺 `.nib`；第三种成功。[单出结果](../../../scratch/heraw-v3-dossier-audit-20261120/outputs/only-result.txt)；[组合结果](../../../scratch/heraw-v3-dossier-audit-20261120/outputs/results.log)。CLI 已[接受此目标](../heraw_sr2x.cpp#L12415)，但[外层 `finish_rgb_canvas`](../heraw_sr2x.cpp#L10282)只检查 RGB/重对焦/视差，包写在守卫内。 |

`caller.note` 在上述运行中载荷逐字节保留，输出包的 `policy` 却是 `{}`：[导出](../heraw_sr2x.cpp#L10508)复制字段/Chart，没有复制生效策略。可把输入政策和实际执行清单作为不同的元数据一起写出。来源图还需随跨像素光学传输扩散；用单点外部先验产生光晕时应能追到产生该光晕的贡献。

### 3.3 “能交换”和“能执行”的清单

| 能力 | 当前边界与修订 |
|---|---|
| NIB/1 格式及 SDK | 64 B 头、128 B 描述符、小端、64 B 对齐与 `public_name` 已成立。文中长名算法应为 [`id.` + SHA-256 前 36 个十六进制字符](../heraw_suite_nib_bus.hpp#L442)。ASCII 帧名原样保存；其余为 `src.` + 路径哈希前 24 字符，绑定的是**路径字符串**。两端身份校验要求宽高为正，包括 `EXT_INPUT`。SDK 示例改用 [`rotate90(k)`（四分之一圈）、`letterbox(...)`、`crop(x0,y0,x1,y1)`](../sdk/nijin_bus.py#L249)。 |
| 掩膜 / 点 / 协方差 / RGB 图格 | 标量按 dtype、角色和有效中心[重采样](../heraw_suite_nib_bus.hpp#L764)，4×1 反例已修。ND 可以传输；主采样口接受单平面或 `(1,H,W,1)`，多帧多候选应先选平面。**RGB 先验另有反例**：[原生 RPC](../heraw_suite_nib_bus.hpp#L1310)重采样的是权重/保护图，RGB 只检查 shape 后按存储下标混合。同尺寸 `rotate90(2)` 的左红条，活动格读回左 `1`、右 `0`，预期左 `0`、右 `1`。[实跑](../../../scratch/heraw-v3-dossier-audit-20261120/rpc_rgb_chart.log)。CLI 则对先验图做最近邻逆映射。两端宜共用按角色定义的坐标/色域/单位校验与采样。 |
| Stage 2/3 角色 | `policy.align`、`fuse.layers` 可描述/传递；主配准与分层仍走原生算法。[`exclusions`](../heraw_sr2x.cpp#L6732)更新非锚帧 `mw_half`，按阈值硬剔或软衰减；SDK 可传的第 0 帧不在这个循环，`feather_px` 也未参与这步计算。能力表应按角色/参数列有效范围。 |
| Stage 4 角色 | `protect_mask`、`emitter_mask`、实际合成权重、数项散景与色散覆盖已有像素消费者；`kernel_bank` 可绑定，`hole_only` 可保存，主渲染没有相应核选择/空洞限制。`--bus-export` 回传数据不等于按原策略再执行。 |
| 来源所有权、算子和缓存 | 普通 CLI / attach 接受外部包前有 `origin=0` 拒绝检查。原生[接收端的信任判定](../heraw_suite_nib_bus.hpp#L1433)还依赖请求侧提供的信息；要承诺“实测只能由宿主发布”，信任决定应归宿主，测量命名空间和计算产物分别标记。四个 RPC 是固定传入方差的高斯均值梯度、对角 GLS、点映射、先验合成；它们与完整 CFA 前向/伴随不同。`cached_stages`/`invalidated_stages` 描述状态，当前 JSONL 方法集没有执行完整 S3–S5 重建的命令。 |
| 传输与数据量 | 客户端用[临时 `.nib` 文件](../sdk/nijin_bus.py#L1298)，NumPy 可映射；C++ 侧读入张量 `data`，合成还可展开成 `double` 并分配输出。应分别报文件、内存峰值与 ROI 成本。SDK 的[能力描述](../sdk/README_ZH.md#L53)也要采用这份消费清单，供不同外部调用方自动适配。 |

## 4. 算法、科学读数与输出的关键差异

### 4.1 测量入口：时钟、权重与原始样本

1. **Tap 的位置。** [线性化/温点替换](../heraw_sr2x.cpp#L5638)在前，[采集行统计](../heraw_sr2x.cpp#L6884)在后，再进行行去闪和 C13。适合的名字是“去条带前的线性绿色行统计”。[O2](../heraw_suite_observer.hpp#L1211)优先消费它；[O5](../heraw_suite_observer.hpp#L2317)使用边缘/运动与滚快时序，[O7](../heraw_suite_observer.hpp#L2865)使用流。要承诺解码后一切修复前的观测量，采样点应移到首次温点替换之前。
2. **权重是不是独立保存。** 数学夹具确实验证了曝光归一读噪、散粒、量化精度及最优权重。生产中 `mw_align=m`、[`mw_half=w_frame*m`](../heraw_sr2x.cpp#L6545)，随后还有饱和 taper；因此“独立直到最后装配”的数据结构描述不准确。审计可按每个消费者列出其输入已经包含哪些因子；两帧等运动、不同曝光/清晰度的因子分离试验适合长期保留。
3. **时钟信任链。** [枚举](../heraw_suite_measurement.hpp#L32)是 `pts_ticks/hw_exposure/makernote_dxo_ts/exif_subsec_ms/model_estimate`；帧序回退是[取时间的函数](../heraw_suite_observer.hpp#L309)的行为。PTS 分子分母确有保留；部分 EXIF 入口以[整秒时间存在](../heraw_sr2x.cpp#L1058)设置亚秒来源，[函数](../heraw_sr2x.cpp#L394)给予 `0.5 ms` 不确定度。因此应分别传入“有整秒/有亚秒/有行标定”。[频率证书](../heraw_suite_measurement.hpp#L96)检验指定频率的 CI/目标公差，有 Oracle；生产观测报告主要用时间跨度、行标定与各局部门禁，未统一调用此证书。建议在报告中分列频率、单位、时钟来源、置信区间和认证状态。

### 4.2 O1–O10：界面应给用户看的量

| 观测器 | 可解释的输出或条件 |
|---|---|
| O1 辐射分解 | 由跨帧 RGB/光度模型估计照明、漫反射/镜面等项；“偏振”易让用户期望直接测得偏振方向或 Stokes 参数。 |
| O2 锁相；O5 振动/微动 | O2 的 Tap、全局快门双曝光窗与余辉估计有实现；[复数 chirp GLRT 调用](../heraw_suite_observer.hpp#L1542)在 **O2**，不是卷宗标注的 O5。O5 有自己的边缘位移、模态/阻尼与长序列微动门禁。 |
| O3 运动几何；O4 BOS | 涡度、TTC、自立体估计依赖空间支持、时间/尺度；BOS 区分势流/旋流分量，可在实际条件下给出 OPD 或代理单位。视宁度在 O8 单列。 |
| O6 LoCA | [实现](../heraw_suite_observer.hpp#L2820)在焦平面、深度图和大光圈条件下，以红蓝边缘对比对相对深度的斜率/相关估计符号，附条件显示。 |
| O7 `modal_wave` | 高置信采样的 `2·hypot(flow)/dt` 中位数（`px/s`）；刚体平移也能给非零值。建议“图像运动速度（波速代理）”，波前/驻波模式另设空间频谱验收。 |
| O8 `seeing` | [配对流的差分倾斜 RMS](../heraw_suite_observer.hpp#L2900)，单位 `1× px`；`seeing_r0_norm=1/max(0.05,RMS)` 是相对指标。以物理 Fried 长度发布需要波长、口径/观测几何和标定。 |
| O9 `refraction` | [相对流的 RMS 与对齐后中位底色](../heraw_suite_observer.hpp#L2932)，单位 `1× px`；无色散角或折射率求解。GUI 当前中文为“折射率”，应改。 |
| O10 `streak_astrometry` | 位移、差分光度及三假设分类；[实现](../heraw_suite_observer.hpp#L2976)按条件输出 `px/frame`、`px/s` 或 `arcsec/s`。角标尺可由焦距和像元间距估算；这是板尺度，不等于已有天球 WCS 解。 |

**单选为什么会跑全套。** 如同两端 GUI 为文件命名的方式，对同组 A9 III 分别传 `--observers seeing`、`--observers modal_wave`，两次都显式指定 `--observer-report`。两个 [O8 结果](../../../scratch/heraw-v3-dossier-audit-20261120/observer_select/seeing.json) / [O7 结果](../../../scratch/heraw-v3-dossier-audit-20261120/observer_select/modal_wave.json) 的 JSON **完全相同**，均给 `seeing_observable=true`、`refraction_rms_1x_px=50.8384`、`streak_count=7294`。原因是 [`all_obs = !cfg.observer_report.empty()`](../heraw_sr2x.cpp#L7340)。修订：报告路径只选择目的地，选择器决定计算集；输出 `requested/executed` 和必要依赖。场景未经天文认证时，不用有效计数充当真实星迹条数。

### 4.3 重建、内存与标定算子

- **条带是真，硬限额和“重读源 RAW”是另一种实现。** [一开始](../heraw_sr2x.cpp#L5640)分配 `N×H×W` 个 float。Pass 1 从在内存中的栈取全局统计；[每帧按 64 行压成 `zstore`](../heraw_sr2x.cpp#L9125)，Pass 2 从内存块解压并计 `roi_reads`，保留全幅 RGB。切片数据集、自评、合成孔径还会[恢复全帧栈](../heraw_sr2x.cpp#L9317)。`full_decode_calls=1` 是赋值，`peak_bytes` 是分项估算。实跑 DxO 得 `11088×7388`、31 条、`roi_reads=1468`、`pix_work=1299.38 MiB + zstore=276.62 MiB`；操作系统计 `3,003,498,496 B` 最大 RSS。[全幅日志](../../../scratch/heraw-v3-dossier-audit-20261120/dxo_rss.log)。历史全图/条带 CPU `59.39 dB` 是同场景两种调度之间的比较。准确表述为“重建条带阶段不用临时像素盘；峰值还受输出画布、帧数和后处理影响”。
- **Q4 要追问区间来自哪里。** [Nikon 分支](../heraw_sr2x.cpp#L8558)用位移、可见性、同色 CFA 邻点累加出 `row_obs`，无观测时用当前估计；对 RGB 行与观测行各作一维 5/3，再用 `floor(obs_c/Δ)·Δ` 构造上下界。改变配准权重即可改变这一区间，而同一 RAW 原始码字始终相同。应称“量化感知的代理一致性修正”；要升级为精确码字似然，需解码器提供按帧、子带、位置和缩放对应的界。DxO 的 [LUT 重复值和中点处理](../heraw_sr2x.cpp#L8504)、以及[像素域混合截顶](../heraw_sr2x.cpp#L8705)应单独保留。
- **Q10 数学和作用域各有一处差异。** [实现](../heraw_suite_measurement.hpp#L245)对归一化相位平均 `χ` 算 `1−abs(χ)²`；卷宗为 `abs(1−χ)²`。三帧等权相位 `0,π,π` 给 `χ=−1/3`，结果分别是 **8/9** 和 **16/9**。而[生产门禁](../heraw_sr2x.cpp#L7028)任一探针通过就执行整次 C13；纯横向抖动可打开横向探针，但纵向同频纹理仍不可分辨。建议先修公式和文案；逐方向/频带保护需把增益传入清洁器并测该方向场景纹理的保留量。
- **计算快门的可控范围。** [生产装配](../heraw_sr2x.cpp#L7958)按序号填 32 个箱，`nnls-custom` 指定后帘包络，所得权重还有 `0.05` 下限；`integrate` 的 κ 改变局部流与背景拟合的混合，`deblur` 有模糊门控及恢复路径。历史 A9 样本相对 anchor 有明确非零差异；若界面要画任意快门曲线，另需接入真实逐帧曝光窗和用户目标曲线。

### 4.4 手持光学：哪些指标可以写进控件说明

| 模块 | 本轮实跑和数学边界 |
|---|---|
| **OP-O1 色散** | `P=16`，`Q=38`、最大邻移 `0.579 px`；纯蓝冲激红通道最小值 `0`。卷宗正性帽写 `min(gate, ratio)`；[代码](../heraw_suite_handheld_optics.hpp#L793)是 `gate·min(1,ratio...)`（逐通道并留极小容差）。例如门控 0.5、上限比 0.4，两者为 `0.4/0.2`；实作更保守。非负输入的下冲保护与保留科学负噪声分别表述。常色恒等还要求抽取的谱场在该处恒定；空间变化的发光/保护选区会产生边界效应。 |
| **OP-O2 STF** | 原生 `R=32`、圆孔、无球差、强度 `u=1`、中心脉冲，水平/垂直/对角在 `Rω=16` 为 `+0.000633 / −0.000001 / +0.000206`。[分支](../heraw_suite_refocus.hpp#L281)要求叶片数 `<5`；[实际权重](../heraw_suite_refocus.hpp#L742)是 `(0.220u,0.406u,1−0.626u)`。在 `u=0.1、Rω=5.1356`，解析响应约 **−0.09776**。正空间核不保证全档、全频的 OTF 都非负。条带积分保整行面积，等效半弦分给每列是近似。 |
| **OP-O3 已有散景整形** | 指定 50% 混叠软边，`fg_delta=0, bg_halo=0`：2 px 腐蚀对该例有效，低支持处需要回退。真实 A9 `swirl=+0.9` 相对 `0` 的最大线性差为 **0.04943**、`341,808` 个通道值有变；`+0.9` 相对 `−0.9` 的最大差**为 0**。[测试结果](../../../scratch/heraw-v3-dossier-audit-20261120/swirl/result.log)。[切向积分](../heraw_suite_handheld_optics.hpp#L618)使用对称支撑与偶权重，变符号只是颠倒遍历次序。应先提供“切向强度”；若提供左右旋，需有带方向的几何与验收图。 |

### 4.5 输出：用读者回读，而非按扩展名判断

同一组 A9、关闭抖动、2×，同时请求 DNG、`beauty/scatter` EXR 和 `.nib`，只切换黑柔/暖玻璃。两个 **DNG 字节相同**；两个 NIB 的 `fused.rgb` **最大差 0**；独立 OpenEXR 读者读到 `768×768`，`beauty` 最大变化 **0.01293945**，`scatter` 从全零变为 **[−0.01296997,+0.00812531]**。[产物对照](../../../scratch/heraw-v3-dossier-audit-20261120/outputs/results.log) · [通道回读](../../../scratch/heraw-v3-dossier-audit-20261120/outputs/parts_stats.log)。这直接定位了 DNG、NIB、EXR 相对玻璃效果的切面，也给 GUI 的文件清单提供验收样例。

[DNG 写入](../heraw_sr2x.cpp#L10228)将重建 RGB 按 RGGB 排列，`LinearRaw` 则走三通道；HQ 的 2048 黑电平能容纳暗部负偏差，TIFF 堆栈里名称相似的 [`tiff-hq`](../heraw_exif_io.hpp#L1768)量化并保持各 RGB 通道后才水平差分/Deflate。`--no-tool-exif on` 的确切承诺是省略处理工具印记、按来源保留固件/EXIF；`.nib` 另有 `session_meta.source_paths`。导出数据交给外部程序时要说清它包含什么。

## 5. 复现凭据与建议执行顺序

| 范围 | 本轮结果 |
|---|---|
| C++ / 实拍容器 | [数学 Oracle](../../../scratch/heraw-v3-dossier-audit-20261120/oracle_audit_libraw.log)：ASan/UBSan 通过，含 E12 LibRaw 读回，`DXO_0820.DXO` 四 IFD、`5544×3694`、四像元 `{366,371,371,355}`。 [观测/重对焦](../../../scratch/heraw-v3-dossier-audit-20261120/oracle_observer.log)：T1–T8 通过。 [手持光学/总线](../../../scratch/heraw-v3-dossier-audit-20261120/oracle_optics_nopb.log)：OP-O1..3 / BUS-O1..5 受测用例通过。 |
| SDK / 原生消费者 | [完整 gauntlet](../../../scratch/heraw-v3-dossier-audit-20261120/sdk-full/gauntlet-report.json)：15/15、无跳过、行覆盖 95.7%，五个阶段 GREEN。[A9 公共 CLI](../../../scratch/heraw-v3-dossier-audit-20261120/cli_a9.log)：8 实拍裁剪、9 个数据瓦片。[Metal 对照](../../../scratch/heraw-v3-dossier-audit-20261120/metal-sample/parity.txt)：实际选中 Apple M1 Max。 |
| GUI 与其他平台 | [Cocoa 自动化](../../../scratch/heraw-v3-dossier-audit-20261120/gui_native.log)：偏好/argv/双语/进程通过，各语言四页 `2560×1664`；[当前控件截图总览](../../../scratch/heraw-v3-dossier-audit-20261120/gui/overview.jpg)。Win32：源码核对创建、同步、argv；实际 X570 / RTX 4080 SUPER / Vulkan、Win32 `--test-prefs` 成绩归入[历史台账](VALIDATION.md#L20)。Linux 是本机交叉构建产物，部署目标的动态 Vulkan 路径需由目标主机复验。 |

**§9.3 第三段原样编译返回 1。** [完整诊断](../../../scratch/heraw-v3-dossier-audit-20261120/oracle_audit.log)共六条，首条是 `heraw_exif_io.hpp:921` 的 `-Wunneeded-internal-declaration`，其余包括 `-Wunused-function`；这是独立测试翻译单元的头内函数告警。原命令也缺 `SUITE_V3_LIBRAW`，即使编过，[E12 的 LibRaw 回读](../tests/test_suite_v3_audit_oracles.cpp#L1155)仍会被条件编译移除。本机实际跑通的命令如下（由 `tools/heraw-sr2x` 执行；工程依赖和 DxO 样本在卷宗给定目录树内）：

```bash
O=../../scratch/heraw-v3-dossier-audit-20261120
clang++ -std=c++20 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -Wno-unused-function -Wno-unneeded-internal-declaration \
  -DHERAW_HAS_WVDC -DNIJINWVDC_STATIC -DSUITE_V3_LIBRAW \
  -I. -I../nraw-bayer-draft/include -I../libraw-heraw \
  tests/test_suite_v3_audit_oracles.cpp ../libraw-heraw/lib/.libs/libraw_r.a \
  build/wvdc_asan/libnijinWVDC.a /opt/homebrew/opt/little-cms2/lib/liblcms2.a \
  -lz -o "$O/t_audit_libraw" && "$O/t_audit_libraw"
python3 "$O/bus_crop_probe.py"
/usr/bin/time -l ./nijinLBS --backend cpu --threads 8 --policy force-all \
  --single-group --streaming on --mem-budget-gb 1 --dither off --rgb-scale 2x \
  --out-rgb "$O/dxo_stripe_2x.tif" ../../ref/DXO_0820.DXO
```

**交付顺序：** 先将 NIB 独立交付、单项报告选择、坐标/来源三个反例写入产品回归；同步收窄科学量与内存文案；再按[卷末 §10](V3_IMPLEMENTATION_DOSSIER_ZH.md#gui-layering)移动 Cocoa/Win32 控件。这样下一次验收可以从用户点的那一行一直核对到写出的文件和像素。
