# `nijinLBS v3.0.0` 下一代计算光学与多帧 RAW 重建总架构与规范

> **文档状态**：`nijinLBS v3.0.0` 已冻结并完成单轮全量交付（物理模型—数学求解—算法工程统一规范，经 `V3_EXTERNAL_MATH_AUDIT_ZH.md` `E01–E12`、`Q1–Q10`、`R1–R12` 与跨模块联审 `1–6` 全项闭环验证，双端 C++20/Metal/Vulkan/GUI 全量落地）。  
> **版本定位**：`nijinLBS v3.0.0`（[heraw_version.hpp](../heraw_version.hpp) 中 `NIJIN_VERSION = "3.0.0"`，全部核心求解器与 CLI/macOS Cocoa/Win32 GUI 显式控件一次性全量交付）。  
> **整合范围**：  
> 1. 第一轮全管线审计与修复项：[ALGORITHM_PROPOSALS_ZH.md](ALGORITHM_PROPOSALS_ZH.md)（`§1–§9`、`E01–E12`、`§7.1–§7.5`）；  
> 2. 第二轮数学求解与流式架构深挖：[ALGORITHM_DEEP_DIVE_ROUND2_ZH.md](ALGORITHM_DEEP_DIVE_ROUND2_ZH.md)（`§0`、`Q1–Q10`、`A–H`、`O6–O10`）；  
> 3. 用户已拍板的产品、精度、存储与双端 GUI 显式档位契约；  
> 4. 专题拓展 I：统一物理光学散射滤镜引擎（**Ricoh HDF** / **Black Pro-Mist** / **Tiffen Ultra Contrast** / **Film Halation** 及双片遮光斗叠用）；  
> 5. 专题拓展 II：无防抖手持抖动 + 运动/演唱会双运动场与舞台光电增强引擎；  
> 6. 专题拓展 III：原生 4 帧无防抖 **DxO ONE SuperRAW**（`1j4r_tnr` `.DXO` / `IMX183CQJ-J`）极限挖掘方案。

---

## §0 文档定位、统一观测公理、代码库映射与已拍板契约

### 0.1 统一连续前向观测模型与坐标/时钟公理

为消除各模块间的单位歧义，全系统固定以下连续前向模型与坐标系（原生 CFA 感光点间距定义为 $1$ ，原生空间角频率为 $\theta \in [-\pi, \pi)^2\ \mathrm{rad/native\text{-}px}$， $2\times$ 输出数字角频率为 $\omega = \theta / 2$ ，定义 $\operatorname{sinc}(x) \equiv \sin(x)/x$ ）。单帧 $k$ 在原生感光点 $p=(r,c)$ 处的期望线性读数 $\mu_{kp}$ 为：

$$
\mu_{kp} = b_{kp} + e_k\, s_p\, M_{c(p)}\, P_p\!\left[\frac{1}{T_k}\int_{-T_k/2}^{T_k/2} H_{kc}(t)\, W_k(t)\, X\, d\tau\right], \qquad t = t_{k,p} + \tau
$$

其中：
- $X(x, \lambda, t)$ 为物方/入瞳后经过归一化定标的连续场景光谱辐照度场；
- $W_k(t)$ 为第 $k$ 帧时变几何变形/透视投影算子（含全局姿态、滚快行内轨迹与非刚性运动流），时刻 $t_{k,p} = t_k + (r_{\rm src} - r_*)\tau_{\rm row}$ 对齐到源帧实际感光行曝光窗中心，$T_k$ 为真实曝光脉宽；
- $H_{kc}(t)$ 为按波长通道 $c \in \{R,G,B\}$ 与瞬时曝光位移积得的光学+运动点扩散核（含衍射、球差、横向/轴向色差与帧内运动核 $H_{\rm mot}(\theta) = T_k^{-1}\int_{-T_k/2}^{T_k/2} e^{-i\theta\cdot\Delta_k(t)}\,d\tau$ ）；
- $P_p$ 为感光点有效像元孔径面积响应（Micro-lens + Active Photodiode Footprint），$M_{c(p)}$ 为 CFA 色彩滤波掩膜（RGGB 2×2 或 X-Trans 6×6）；
- $s_p$ 为传感器固定平场/像素增益，$e_k$ 统一收纳曝光时间与 ISO 模拟/数字增益，$b_{kp}$ 为暗电流/黑电平偏置；
- 在传感器采样后叠加散粒噪声 + 读出噪声（Poisson-Gaussian 参数 $(\sigma_{{\rm read},k}^2, g_k)$ ）、满阱/ADC 上截顶 $s^{\rm sat}_{k,c}$ ，以及因格式而异的非线性映射/小波/整数熵码字量化（如 Nikon HE/HE* TicoRAW 与 Sony ARW6）。

### 0.2 物理源与实测代码库映射索引（供外部审查精确定位）

| 代码/资料文件 | 当前角色与本方案改造入口 | 已核实锚点（行号/偏移） |
|---|---|---|
| [heraw_version.hpp](../heraw_version.hpp) | 主版本号定义 | `L3`: `NIJIN_VERSION "3.0.0"` |
| [heraw_sr2x.cpp](../heraw_sr2x.cpp) | CLI 编排、`frames_lin` 分配、刚体补丁追踪、曝光/权重与 DNG/PNG/EXR 导出 | `L3051–3140` (`recover_rigid_motion_patches`)、`L5556` (`frames_lin`)、`L6228` (`stream_deghost`)、`L6414` (`w_frame`)、`L9181 / L9651` (`highlight_rolloff`)、`L9231` (`dng_ped`) |
| [heraw_suite_core.hpp](../heraw_suite_core.hpp) | 卷帘行平滑、幸运成像权重、CPU M5-A / M5-B 求解器 | `L807–893` (`row_rolling_shutter_smooth`) |
| [heraw_cpu_stage45.hpp](../heraw_cpu_stage45.hpp) | Stage 4 核回归、Stage 5 FFT64、2× 重建融合 | `L2227–2628`, `L3941–4029` |
| [heraw_gpu_backend.hpp](../heraw_gpu_backend.hpp) / [heraw_gpu_metal.mm](../heraw_gpu_metal.mm) / [heraw_gpu_vulkan.hpp](../heraw_gpu_vulkan.hpp) / [build_shaders.py](../shaders/build_shaders.py) | Metal / Vulkan `49` 个计算内核调度与瓦片带宽控制 | SPIR-V `k_m5a_update_spv` ([heraw_gpu_vulkan.hpp:135](../heraw_gpu_vulkan.hpp#L135))；Metal Stage 4/5 与 Tile 统计消费者 |
| [heraw_wvdc_router.hpp](../heraw_wvdc_router.hpp) | `.NEV` / `.ARW` / `.NEF` / 原生多 IFD `.DXO/.4RW` 路由解码 | `L115–153` (`parse_exif_datetime_utc_sec`)、`L560–570` (`pts_ticks / pts_timescale` 有理数溯源) |
| [heraw_libraw_dyn.hpp](../heraw_libraw_dyn.hpp) / [heraw_exif_io.hpp](../heraw_exif_io.hpp) / [heraw_suite_exr_stream.hpp](../heraw_suite_exr_stream.hpp) | LibRaw 动态加载（`open_file`, `unpack`）与 DNG/PNG/TIFF/Multi-Part EXR 写出 | `heraw_libraw_dyn.hpp:41,90,145` |
| [heraw_suite_features.hpp](../heraw_suite_features.hpp) | D-1..D-4 深度/视差、V-curve CoC 标定、D-2 运动解耦 | `L1832–1983` (`vcurve`, `estimated_baseline_mm`)、D-2 运动解耦 |
| [heraw_suite_observer.hpp](../heraw_suite_observer.hpp) | `O1–O5` 与 `O6–O10` 科学观测器、传感器行读出表 | `L130–200` (`lookup_sensor_readout_ms`)、`run_optical_lockin` (O2) |
| [heraw_suite_optics.hpp](../heraw_suite_optics.hpp) / [heraw_suite_refocus.hpp](../heraw_suite_refocus.hpp) | 光学 PSF 分区反演、合成散景与重对焦渲染 | [heraw_suite_optics.hpp:892](../heraw_suite_optics.hpp#L892) |
| [heraw_eval_metrology.hpp](../heraw_eval_metrology.hpp) | 留出预测残差、色混叠、FRC 等内建物理计量 | `L385, 860–881, 973–1099` |
| [heraw_sr2x_gui_mac.mm](../heraw_sr2x_gui_mac.mm) / [heraw_sr2x_gui_win.cpp](../heraw_sr2x_gui_win.cpp) / [heraw_gui_i18n.hpp](../heraw_gui_i18n.hpp) | 原生 macOS Cocoa + Win32 GDI+ 4-Tab 工作台及多语 i18n | `mac.mm` 8029 行 / `win.cpp` 8873 行 |
| `tools/nraw-bayer-draft/src/` (`session_api.cpp`, `ticoraw_dec.cpp`, `arw6_dec.cpp`) | WVDC 会话硬件时间戳与 HE/HE*、ARW6 解码内部接口 | `session_api.cpp:615`, `ticoraw_dec.cpp:1017,2494`, `arw6_dec.cpp:2176` |
| `dxo_one_re/extracted/android/rtos_raw.bin` / `dxo_one_re/refdocs/` | DxO ONE 原厂 RTOS 二进制定时表与镜头规格 | `0x2540c` (`1j4r_tnr`), `0x2544c` (`bias_3j3r`), `0xce1cc` (`72MHz / HMAX=876 / VMAX=3739 / 22000/1001`); `openone_dxo_com_archived.txt:35,55` (`11.9mm`, `108g`) |

---

### 0.3 已拍板 `v3.0.0` 产品与双端 GUI 显式档位契约

用户明确要求**保留 `3.0.0` 主版本号**，并将**所有算法档位与 `O1–O10` 科学观测器以显式独立控件形式同时暴露在 CLI、macOS Cocoa GUI 与 Windows Win32 GUI**，供进阶用户逐项手动切换与交叉对比：

| 开关/档位族 | CLI 标志与枚举值 | 默认策略与核心语义 | 双端 GUI 落点 |
|---|---|---|---|
| **1× True-RGB 重建档** | `--rgb-1x-tier <fast\|sinc\|joint>` | 默认 `sinc`；`fast`：低成本加权平均；`sinc`：Q6 中档带限窗化 `sinc` 连续基；`joint`：Q6 高档联合 $2\times$ 物理求解 + 高带 Schur 消元后窗化 `sinc` 降采样 | Tab 1 主输出下拉框（三档显式可选） |
| **EXR 多层科学主底片** | `--rgb-format exr` + `--exr-precision <half\|float>` + `--exr-parts <beauty,fg,bg,scatter,depth>` | 零第三方库依赖（复用 `zlib` + 自研分块 ZIP Multi-Part OpenEXR 2.0 写出/读取器）；默认 `half` (`FP16 + ZIP`)，科学无损可选 `float` (`FP32`)；分 part 输出 `beauty`、`fg`（含 `A`）、`bg`、`scatter` 与 `depth` | Tab 1 导出格式面板（精度与 `EXR:16f/32f` 下拉） |
| **大栈流式两遍引擎** | `--streaming <auto\|on\|off>` + `--mem-budget-gb <G>` | 默认 `auto` + `4 GiB`；预估全栈超预算时自动切换为两遍 $(H,h,c)$ 充分统计条带流式引擎（零临时磁盘缓冲，高疑义局部按帧从 `.NEV`/RAW 二次解码重放） | Tab 1 性能/内存档位控件（`Stream:Auto/On/Off` + `Mem(G)`） |
| **编码码字精确似然** | `--codec-constraint <off\|box\|probit>` | `off`：标量高码率近似；`box`：整数码字区间边界门控；`probit`：TicoRAW/ARW6 精确前像箱形 Probit $-\log[\Phi(b)-\Phi(a)]$ 与双正交严格伴随 $\widetilde{L}^*$（含 Q4 $\alpha_b$ 平滑过渡） | Tab 1 / CLI 高级解码重构参数 |
| **高光模型与舞台保色** | `--sat-model <hard\|probit>`、`--highlight-mode <legacy\|oklab-stage\|hard-clip>` | `probit`：Q5 上尾截断似然 $-\log\Phi((\mu-s)/\sigma)$；`oklab-stage`：OkLab 等色相恒定压缩+窄带灯光色调保护，防止舞台青蓝/品红 LED 发白；`hard-clip` 线性直接限幅 | Tab 1 色彩/高光保护控件（`HL:Legacy/OkLab/Clip`） |
| **深度门槛与孔径计权** | `--depth-gate <auto\|relaxed\|force>`、`--metric-policy <verified\|allow-proxy>`、`--aperture-weight <voronoi\|dwell>` | 严格分离相对深度 `[0,1]` / 焦前焦后定号 / 米制 `mm/m`（`verified` 需真实对焦步进/$m_k$ 标尺/外部基线）；合成孔径默认 2D 裁剪瞳 `voronoi` 均匀几何面积，可选停留时间 `dwell` | Tab 2 重对焦与 Tab 4 光学视差面板 |
| **计算快门与运动剖析** | `--shutter-mode <anchor\|integrate\|nnls-custom\|deblur>`、`--flow-kappa <0..1>`、`--intra-deblur <auto\|on\|off>`、`--motion-profile <auto\|standard\|concert\|sports\|dxo-superraw>` | `nnls-custom`：C1 NNLS 非负曝光包络；`integrate`：$\kappa$-拉格朗日随动拖影（0=锁定主体追拍，1=固定画幅积分）；`deblur`：C3 多曝光轨迹零点互补去模糊；手持/演唱会/体育/DxO ONE 四档专属运动配置 | Tab 1 `Profile` + Tab 2 运动/计算快门工作室 |
| **物理光学散射滤镜** | `--diffusion-mode <off\|hdf\|black-mist\|ultra-contrast\|halation\|uc+hdf\|uc+mist\|uc+halation>`、`--diffusion-strength <f>`、`--uc-grade <1/8\|1/4\|1/2\|1\|2\|3\|4\|5>`、`--uc-baffle <standard\|hooded\|wide-flare>`、`--uc-chroma <neutral\|warm-glass>` | 于传感器原生线性辐射域施加：**HDF / Black Mist** 近域多尺度高光漫射；**Tiffen Ultra Contrast** 基于局部加权全局低频环境光的阴影抬升（近零局部光晕）；**Halation** 胶片红层背散射；支持 `uc+*` 级联叠用与 `scatter` part 导出 | Tab 2 光学柔焦滤镜区（模式/强度/UC 档位/遮光罩/色偏） |
| **科学观测器矩阵** | `--observers <off\|all\|loca_sign,modal_wave,seeing,refraction,streak_astrometry>`、`--observer-gate <auto\|relaxed\|force>`、`--next-shot-plan [json]` | 默认按需启用 `org.nijin.observer.v3`（含 `O1–O5` 与 `O6–O10` 及 `pts_ticks` 时钟溯源），并在缺少独立标尺时诚实标注退化告警；可选导出 Fisher 最优下一拍规划 `--next-shot-plan` | Tab 3 扩展观测下拉与 `.json` 规划勾选框 |

---

## §1 测量底座重构与确定性修正（`P0` 物理测量底座）

### 1.1 `P0-F`：固定原生样本记录，解耦几何可见性与辐射精度

当前管线重构已关闭原两处底层混淆：一是将 [heraw_sr2x.cpp:5556](../heraw_sr2x.cpp#L5556) `frames_lin` 原生方差与 [heraw_cpu_stage45.hpp:2599](../heraw_cpu_stage45.hpp#L2599) 解耦，消除帧间运动权重与方差倒数重乘（**E01**）；二是在去条带算子之前即通过 Tap-0 把线性原始行缓冲分流给 `O2/O5/O7` Observer（**E11**）。

**架构重构规范：**
在解码与曝光归一化边界处建立只读结构 `NativeSampleFrameView`，强制将**物理测量事实**与**算法推断权重**隔离为两个正交通道：

1. **物理测量层（严禁成片后处理算子覆写）**：
   - 原生 CFA 阵列几何与活动裁剪原点 $(r_0, c_0)$ ；
   - 原生归一化线性能量 $\tilde{y}_{kp} = (y_{kp}^{\rm raw} - b_{kp}) / e_k$ 与原始行号 $r_{\rm src}$ ；
   - **原生单次总方差与统计精度**：在源帧本域统一构造
     $$V_{kp}^{\rm native} = \sigma_{{\rm read},k}^2 + g_k \hat{\mu}_{kp} + \sigma_{{\rm quant},kp}^2, \qquad w_{kp}^{\rm rad} \equiv \frac{e_k^2}{V_{kp}^{\rm native}}$$
     严禁再于编排层外部重复乘以 `1/var_ratio` 或同时累加全局常数项与逐帧小波表（一次修齐 **E01** 与 **E05**）；
   - 原生满阱/ADC 截顶掩膜 `sat_flag` 及（当开启 `--codec-constraint box|probit` 时）解压码字整数区间 $[a_{kp}, b_{kp}]$ ；
   - **原始行时序旁路（Tap-0）**：在任意行条纹清理/成片滤波之前直接分流给 `O2`（频闪/AC）与 `O5/O7`（模态震动），确保开关“成片去条带”保持原始谐波幅度与信噪比测量完全不变。
2. **推断可见性层（按求解器消费者消费）**：
   - 运动遮挡权重 `visibility` $v_{kp} \in [0, 1]$ 、幸运成像锐度权重、前景分层概率与饱和渐变 taper 各自保持独立命名张量，仅在最终融合核或正规方程装配处按约定代数乘入一次。

### 1.2 `P0-CLK`：高精度 4 级科学时钟契约（解决 `.NEV` 毫秒截断瓶颈）

[heraw_wvdc_router.hpp:138-153, 560-570](../heraw_wvdc_router.hpp#L138-L153) 已升级为直接保留 `.NEV` 容器 `pts_ticks / pts_timescale` 有理数分母与 `base_sec`，彻底消除此前 `parse_exif_datetime_utc(..., ms)` 整毫秒舍入造成的 $\pm 500\ \mu\mathrm{s}$ 随机相位抖动。在 `Q9` 高频测振中（例 $f=2500\ \mathrm{Hz}$ 时 $0.1\ \mathrm{rad}$ 相位预算对应 $6.37\ \mu\mathrm{s}$ 定时精度），有理数溯源是跨帧相干锁相的必要基础。

新增 `ClockProvenance` 元数据结构：

| 时钟层级 | 数据源与保留精度 | 典型适用场景 | 频率证书允许上限 |
|---|---|---|---|
| `Tier-1: PTS_TICKS` | 直接读取容器有理数 `int64 tick_num` / `uint64 timescale` + 已知传感器 $\tau_{\rm row}$ | `.NEV` (`1001/60000` $=16.683333\ \mathrm{ms}$)、高精度视频/电子快门连拍 | 亚 Hz 精化 + 跨帧相位整数圈数解缠 |
| `Tier-2: HW_EXPOSURE` | EXIF / MakerNote 硬件曝光计时器微秒级间隔 + 硬件行读出表 | DxO ONE RTOS `IMX183` (`72 MHz` 主频)、Nikon Z8/Z9、A9 III、X-H2S | 跨行与窄带脉冲可辨率标定 |
| `Tier-3: EXIF_SUBSEC_MS` | 标准静态照片 `SubSecDateTimeOriginal`（$\pm 0.5\text{ ms}$ 量子化不确定度） | 无有理数容器的 NEF/ARW/RAFBurst | 受 $\delta t \approx 0.5\text{ ms}$ 约束，$f_{\max}\delta t < 1/2$ 以外报周期等价集 |
| `Tier-4: MODEL_ESTIMATE` | 未知读出时间按典型堆叠/非堆叠模型估算，并显式标 `clock_uncalibrated=true` | 未知第三方面板机身 | 对输出强制标注 `cycles/readout` 无量纲单位，禁用绝对 Hz 定号 |

### 1.3 `E01–E12` 确定性缺陷与公式统一修复规范表

| 编号 | 缺陷本质与实证反例 | `v3.0.0` 统一修复规范与入口 | 验收门禁（Oracle） |
|---|---|---|---|
| **E01** | 编排层与 `heraw_cpu_stage45.hpp:2599` 重复计权（1:4 方差比下第 2 帧实测权重由最优 $0.2000$ 错变为 $0.05883$ ） | 见 `§1.1 P0-F`：统一由归一化精度接口传参，GPU/CPU 融合核内只乘一次 `vis * w_rad` | 对静态均方差 1:4 合成帧，第 2 帧贡献严格收敛于 $0.2000 \pm 10^{-4}$ |
| **E02** | [heraw_suite_features.hpp:1947-1952](../heraw_suite_features.hpp#L1947-L1952) 视差米制基线多乘 $2 \times p_{\rm mm}$ ，$50\text{ mm}$ 镜头 $f_h=5000, k=0.02$ 时错算为 $2.5\text{ mm}$（真值 $10.0\text{ mm}$ ） | 修正为针孔几何一致公式：$B_{\rm mm} = \frac{1000}{f_{h,\rm px}\cdot k_{\rm diopter/px}}$ （其中 $f_h$ 已为半分辨率像素焦距），并受 `§2.7` 三级深度门控保护 | 合成纯横移 $B=10.0\text{ mm}$ 场景下反演误差 $< 1\%$ |
| **E03** | 文档将 5 帧 R/B 超定归因于像素方程数高于 $4P$ 的秩证明 | 更新理论阐述为 **Bayer 4 陪集线性独立性 + 强色差连续先验（`§2.2 Q2`）**；5 帧仅提供 $5P/4$ 个独立红/蓝样本 | 文档与 `ALGORITHM.md` / GUI 帮助同步完成定量说明 |
| **E04** | [heraw_eval_metrology.hpp:385,990](../heraw_eval_metrology.hpp#L990) 组内采样固定起点 `(8,8)`、步长 2 只落红点且将目标帧留在重建内（蓝色 `+4σ` 异常时指标错报 `0` 而全 CFA 均值 `4.0` ） | 拆为两条评测通道：**样本内快速诊断**（步长 1 覆盖全 CFA 相位）与 **严格留一帧交叉预测** $\chi^2_{\rm LOO}$ （剔除帧 $k_{\rm hold}$ 重建，分母计入预测本身方差 $\sigma_{\rm pred}^2 + \sigma_{k_{\rm hold}}^2$ ） | 单一颜色通道异常（B $+4\sigma$）下，LOO 残差按理论比例准确反映且不受步长相位盲区影响 |
| **E05** | 开启小波量化与 AEB 时，消费者优先读逐帧 PG 表，丢失 M5-A 常数与一次量化项 | 见 `§1.1 P0-F`：全部通道/曝光缩放前在原生本域合并 `read + shot + quant_full(mu)`，M5-A / CPU / Metal / Vulkan 共用单一张量 | 固定量化方差正偏置与参考等曝光帧，M5-A 残差分母精确包含全三项量化系数 |
| **E06** | `--psf-sigma`（单位 2× px）未区分配准网格地直接与 `1×` fallback 混用，导致 1× 得 `1.05` 而 2× 得 `0.525` | 配置读取瞬间统一换算为 **原生 1× px 基准单位** `eff_psf_sigma_1x`，并在所有分区测量与 M5-B 算子处按目标网格倍率精确缩放 | 同一配置切换 `1×/2×` 时物理 PSF 半径（微米）严格不变 |
| **E07** | [heraw_suite_features.hpp:2072-2089](../heraw_suite_features.hpp#L2072-L2089) D-2 在 $b_x=t$ 共线秩 1 退化时伴随阵分子仍零，剩余能量比错报为 `1.0` | 在实际有效帧行空间上做带列主元 / 秩揭示 QR 分解计算投影残差，并联合检查设计矩阵最小奇异值 $\sigma_{\min}$ | 精确共线输入 $(b_x = t, b_y = 0)$ 下剩余能量比输出 $\le 10^{-5}$ 并标记不可解耦 |
| **E08** | [heraw_suite_observer.hpp:1096-1141](../heraw_suite_observer.hpp#L1096-L1141) O2 使用全局 $3\times 3$ Gram 矩阵配合局部缺帧右端，纯 DC 局部因每轮 $\pi$ 相位遮挡错报交流调制度 `0.5` | 全面替换为基于有效未饱和可见样本掩膜的 **逐点/同轨迹共享分块加权 QR**（`Q9` / `§7.5`），静态零调制返回 $(1,0,0)$ | $\pi$ 相位周期性遮挡下的纯 DC 区返回交流幅度 $< 10^{-5}$ |
| **E09** | [heraw_suite_features.hpp:413,517,2625](../heraw_suite_features.hpp#L413) 仅凭帧数与 `max-min` 焦距差开门，输入重复焦位 `(1,1,2)` vs `(1,2,2)` 无真实模糊变化时 `observable=true` 且深度漂移 `0.333 -> 0.667` | **对同焦位输入先行组内聚合去噪**；三点亚步峰严格要求 $\ge 3$ 个非简并焦位、峰值两侧有真实单调下降支撑与显著负曲率；更一般情形按 `§2.7` 变量投影代价间隔发门 | 同纹理静态重复 `(1,1,2)` 输入必须返回 `observable=false`（深水定度标记缺失） |
| **E10** | [heraw_eval_metrology.hpp:860-881](../heraw_eval_metrology.hpp#L860-L881) 色混叠指标分子仅沿 $x$ 做二阶差分，旋转 $90^\circ$ 条纹得 `0` | 改为 **二维各向同性带通 + 水平/垂直独立分量与合成报告**；当亮度高频分母低于阈值时单列无支撑提示 | $s = 0.01(-1)^x$ 与 $s = 0.01(-1)^y$ 给出完全对称的数值结果（`0.500 vs 0.500`） |
| **E11** | [heraw_sr2x.cpp:7243](../heraw_sr2x.cpp#L7243) `analyze_subject_observer` 已挂接于 Tap-0 原生采样与原始行时序，独立于成片去条带算子开关 | 参见 `§1.1 Tap-0`：Observer 严格读取线性化原始样本流 | 开启/关闭去条带时，成片条纹被压制而 Observer 读取的载频与调制度保持 `0.1200` 恒定 |
| **E12** | [heraw_sr2x.cpp:9231](../heraw_sr2x.cpp#L9231) 对 `dng-hq` 与 `dng` 保留 **14-bit 等效暗部偏置踏板 IFD0 `BlackLevel = 2048`**，防止负阴影读噪硬截断至零 | 在编码 LUT 与 EXR 线性浮点链路内保留零均值负噪声尾分布 | `dng-raw` 与 `dng-hq` 对零照度暗场均完整保留负半轴采样与正态均零统计 |

### 1.4 `Q10` 轨迹特征函数驱动的平场/固定残差（`C13`）频谱可辨识门控

针对在场景运动过程中离线分离传感器固定图案增益 $a(\theta)$（log-gain 域对数加性模型 $z_k(p) \simeq a(p) + x(p-d_k) + \epsilon_k$ ），在已知各帧有效位移分布 $d_k$ 与可见权重 $w_k$ 时，**无需依赖粗略几何凸包估计**，可直接定义位移驻留分布的**特征函数**：

$$
S_w = \sum_k w_k, \qquad \chi_d(\theta) = S_w^{-1} \sum_k w_k e^{-i\theta\cdot d_k}, \qquad F_a(\theta) = S_w \big[1 - |\chi_d(\theta)|^2\big]
$$

- 在小位移长波极限下 $1 - |\chi_d(\theta)|^2 \simeq \theta^\top \operatorname{Cov}_w(d)\,\theta$ ，明确刻画：**对任何位移正交方向（如纯水平抖动时的纯垂直条纹）以及步进整数周期谐波 $\theta\cdot(d_k-d_j) \in 2\pi\mathbb{Z}$ ，信息量严格为零**；
- 由 Wiener 收缩 $T_a(\theta) = F_a(\theta) / [F_a(\theta) + \lambda_a(\theta)]$ 实施谱截断，凡低于安全门槛的频率强制置零并做全局频谱子空间投影；
- **标准反例 Oracle**：等权双帧水平相差 $12\ \mathrm{px}$ 时，波长 $\lambda$ 的归一化可辨信息精确为 $\sin^2(\pi\cdot 12 / \lambda)$（$\lambda=12\to 0$，$\lambda=24\to 1$，$\lambda=120\to 0.0955$，$\lambda=1200\to 0.000987$ ），取代单一阈值门控。

---

## §2 信息论核心重建与标定求解器（`Core` 多帧求解矩阵）

### 2.1 `Q1` 双重 Schur 补联消与“坐标锚 vs 画质锚”正交拆分

以第 0 帧构造差分残差 $d_k = y_k - y_0$ 会把锚帧噪声与几何插值误差转化为全体差分帧之间精确 rank-$r_a$ 的共有误差相关性 $C = D + U\Sigma_a U^*$ 。简单对角加权将高估共模信息 $25\times\sim 50\times$ ，而直接用对角元素则低估差分对比度方向。

**线性时间 Arrow-Head 双重 Schur 补求解器：**
在任意小块构造联合线性模型 $d_k = B_k u + J_k \xi_k + U_k a + \epsilon_k$ （$u$ 为场景局部系数，$\xi_k$ 为单帧局部对齐/光度漂移 nuisance，$a$ 为锚帧共享误差，$W_k = D_k^{-1}$ ）。逐帧先消去局部 $\xi_k$ ：

$$
Q_k = W_k - W_k J_k \big(J_k^* W_k J_k + \Lambda_k\big)^\dagger J_k^* W_k
$$

随后以固定常驻标量开销累加四个小块充分统计量：$H = \sum B_k^* Q_k B_k$ ，$L = \sum B_k^* Q_k U_k$ ，$G = \Lambda_a + \sum U_k^* Q_k U_k$ ，$h = \sum B_k^* Q_k d_k$ ，$g = \sum U_k^* Q_k d_k$ 。汇总结束仅需执行一次低维 Schur 补即可完全消除锚帧共模耦合：

$$
F_{\rm eff} = H - L G^\dagger L^*, \qquad h_{\rm eff} = h - L G^\dagger g
$$

**几何基准与画质基准分离：** 全局几何坐标原点固定在最稳定/最锐利参考姿态，但在辐射重构层中所有帧（含基准帧自身）平权按各自真实 OTF $H_k$、曝光 $e_k$ 与噪声方差 $V_k$ 联合解 $A_k X = Y_k$ 。当帧 0 某频率处恰落在运动模糊零点 $H_0(\omega)=0$ 而帧 1 清晰 $H_1(\omega)=1$ 时，输出证据全部平滑由帧 1 接管，互不干扰。

### 2.2 `Q2` Bayer 四相与 X-Trans 陪集基建模及独立数据占比 $s_\ell$

- **Bayer 4 相全模信息矩阵**：在 2× 输出格内以原生采样相位直接计算 $4\times 4$ 亮度（及按需扩展彩色）Hermitian 信息阵，直接度量各频率在横/纵/双对角线四个方向上的混叠分离能力。
- **X-Trans（$6\times 6$ 非周期直觉纠正与陪集分解）**：全彩 $6\times 6$ 格在 2× 输出对应 $12\times 12 = 144$ 折叠频位，因真实三色滤波矩阵在原生平移 $(3,3)$ 下周期不变（原胞面积 $18$ ），全彩解耦按 $i+j$ 奇偶拆为两个 coset，每色独立未知数精确为 **$72$ 个复位置**（绿通道在 $(3,0),(0,3)$ 下不变，原胞折叠 **$36$ 个位置**）。计数定律揭示：即便相位采样完全理想，X-Trans 无先验全彩反混叠至少需要 **8 帧（全带绿）与 18 帧（全带红蓝）**。
- **实时工程 6 列 / 10 列 $(Y, C_+, C_-)$ 载波基**：由归一化 6 点 2D DFT 推导得色差通道支撑中心位于 $(\pm 2,0),(0,\pm 2),(\pm 2,\pm 2)$（$C_+=(R+B)/2-G$ ）及 $(\pm 1,3),(3,\pm 1)$（$C_-=(R-B)/2$ ）。选帧与实时谱图评估起步采用 **6 列基**（亮度 4 相位列 + 色差 $C_\pm$ 主带 2 列），对强色纹理块按主奇异值增设一对高频色差载波形成 **10 列候选**，在离线高阶分支用完整 72 位置基定量评估模型截断残余。
- **色线先验的独立记账与数据份额指标 $s_\ell$**：由可靠低频邻域估出局部色差斜率矩阵 $\Lambda_C = L_C^* W_C L_C$ ，与适度先验 $\Lambda_0$ 合成总先验 $\Lambda$ 。对任一待评估对比度方向或色彩模态 $\ell$ ，定义后验协方差 $C_{\rm post} = (F + \Lambda)^{-1}$ 与**纯数据贡献比例**：

  $$
  s_\ell = 1 - \frac{\ell^* C_{\rm post} \ell}{\ell^* \Lambda^{-1} \ell}
  $$

  当 $s_\ell \ge 0.8$ 时标示“实测多帧数据直接支撑色差”，彻底区分“真实测得灰边”与“先验强行压平色彩”。

### 2.3 `Q3` 凸 B 样条 Tile 谱图 $K(x)$ 与三消费者单次预算协议

在统一白化基底下对每控制块构造标准化数据分辨率算子 $K = V \operatorname{diag}(h_j) V^*$ （$h_j = \lambda_j / (1 + \lambda_j)$ ，$0 \preceq K \preceq I$ ）。全图插值直接在矩阵算子空间执行非负单位分解的线性或三次 B 样条插值 $K(x) = \sum_t b_t(x) K_t$（$b_t \ge 0, \sum b_t = 1$ ），天然保半正定性与谱上界，并在特征值简并处完全规避轴翻转奇点；物体遮挡边界则按分片可见域归一化以防跨物“借支撑”。

在实现层中，该同一物理谱图由三个核心算子严格正交消费，杜绝多重过度衰减（如 `0.8 × 0.8 × 0.8 = 0.512` 退化反例）：
1. **Stage 4 核回归足迹**（CPU / Metal / Vulkan）：按方向可辨张量自适应压窄可观测高频轴的回归核 $\Sigma_{\rm kern} = \sigma_{\rm wide}^2 I - (\sigma_{\rm wide}^2 - \sigma_{\rm narrow}^2) T$ ，独立于结构张量边缘切向延展约束；
2. **Stage 5 FFT64 频带收缩**：在高频噪底估得的 Wiener 收缩下限中按 $g_q = v_q^* K v_q$ 抬升具有实测依据的高 SNR 方向增益；
3. **M5-A 正则先验**：于白化基下引入子空间惩罚 $\lambda_U (I - K)$，高数据支撑方向自然撤销平滑惩罚，零观测方向平滑退回先验。

### 2.4 `Q4` 有损 RAW 压缩（TicoRAW HE/HE* & ARW6）精确码字似然与真实伴随

现有解压缩链路在 `ticoraw_dec.cpp:2494` 将系数级量化步长转为空间域近似高斯方差，难以利用零码死区与整数区间截断结构。`--codec-constraint <off|box|probit>` 的 `box` 与 `probit` 档位启用系数域精确观测模型：

- **可微区间 Probit 与确切二阶曲率**：令正向整型量化区间映射到连续边界 $[\ell, u]$ ，系数域传播电子噪声为 $\sigma_b$ ，令标准化边界 $a = (\ell - c)/\sigma_b$ 、 $b = (u - c)/\sigma_b$ ，则精确码字概率 $P_I = \Phi(b) - \Phi(a)$ 的负对数似然梯度与 Hessian 曲率为：

  $$
  \ell'_I = \frac{\phi(b) - \phi(a)}{\sigma_b P_I}, \qquad \ell''_I = \frac{1 - \operatorname{Var}(Z \mid a < Z < b)}{\sigma_b^2}, \qquad Z \sim \mathcal{N}(0, 1)
  $$

  宽死区内部曲率自动趋零（由其余帧亚像素观测主导定位）；到达码字台阶边界时，信赖域步长内比对真整数前向 $L_{\mathbb Z}f(A_k X)$ 实际损失以保证单调下降；
- **双正交小波真实伴随校验**：所有 lifting 提升与边界对称扩展反向转置 $\widetilde{L}^* \ne \widetilde{L}^{-1}$ ，在单元测试层用随机边界脉冲强制核验内积恒等式 $\langle Jv, w\rangle = \langle v, J^*w\rangle$ ；
- **高码率与死区无偏切换 $\alpha_b$**：依据量化比 $r_b = \Delta_b / \sigma_b$ 、信号步长方差比 $q_b$ 及先导零码率 $p_{0b}$ 构建平滑过渡系数 $\alpha_b = 1 - [1 - \mathrm{smoothstep}(0.5, 2, r_b)][1 - p_{0b}(1 - \mathrm{smoothstep}(1, 9, q_b))]$ ，严禁同一样本同时走连续量化方差与整数区间似然两条通道造成的双倍记账。

### 2.5 `Q5` AEB 高光上尾生存似然、双色线索与二阶 GLS 偏差控制

- **上截顶精确符号定义**：传感器满阱或 ADC 在阈值 $s$ 处截断，观测事件为 $Y \ge s$ ，其负对数生存似然为 $-\log[1 - \Phi((s-\mu)/\sigma)] = -\log \Phi(z)$ ，其中 **$z \equiv (\mu - s)/\sigma$** 。令逆 Mills 比 $m(z) = \phi(z)/\Phi(z)$ ，在冻结 $\sigma$ 下有解析一阶/二阶导数：

  $$
  \partial_\mu \ell = -\frac{m(z)}{\sigma} \le 0, \qquad \partial_\mu^2 \ell = \frac{m(z)[z + m(z)]}{\sigma^2} > 0
  $$

  当均值显著高于阈值 $s$ 时信息量自动饱和趋平（与仅作单调增长不等式一致；**Oracle**：当所有短曝/长曝 G 通道均高于 $s+3\sigma$ 提升至 $s+20\sigma$ 时，解绝不被反向压暗），而存在未截顶短曝光帧时立即精准闭合亮端上界；
- **Feasible GLS 二阶权重偏差抑制**：当使用同块带噪样本估计方差 $V(\hat\mu)$ 构造精度 $\hat{w} = 1/V(\hat\mu)$ 时，二阶展开产生权重相对偏差 $E\hat w - w \simeq -\frac{V'}{V^2}b + (\frac{V'^2}{V^3} - \frac{V''}{2V^2})(t^2 + b^2)$ 及协方差项 $-V'\operatorname{Cov}(\delta,\epsilon)/V^2$ 。因此在求解第一轮使用 **低通交叉先导（Cross/Low-pass Pilot）方差并冻结权重**，仅在高曲率与跨曝光交替边界补做一轮轻量更新；
- **全截顶交付铁律**：全曝光均截顶且无短曝支援时，`sci` / EXR 科学层严格写入统计下界区间与可行色相域；标准显示出口采用平滑双色保持滚降，将色彩推断明确标注于显示派生层。

### 2.6 `Q6` 三档 `1× True-RGB` 连续基与确定性残差自由度 $\nu_r$ 早停

针对已锁定的 `--rgb-1x-tier <fast|sinc|joint>` 三档设计：
- **低档 (`fast`)**：只消费由 `Q3` 谱图确认 OTF 已衰减高频混叠的区域作快速反投影融合，剩余混叠计入输出不确定度；
- **中档 (`sinc`)** *(默认)*：定义目标输出为连续物方场经 **窗化带限 `sinc` 基函数** 与 `P_pix * H_opt` 一次性卷积积分的 1× 投影，把高于 1× Nyquist 的高带信号项 $B_H u_H$ 在小块内显式当作干扰列做 `Q1` Schur 边缘化或赋予高频功率谱先验协方差 $C + B_H \Sigma_H B_H^*$ （`r9_lowband_marginal_info`），消除低通下采样前的强折返伪影；
- **高档 (`joint`)**：完整 2× 物理求解联合高带 Schur 消元，再经过精确抗混叠窗化 `sinc` 滤波器降采样到 1× True-RGB；
- **无随机迹早停自由度 $\nu_r$**：在局部白化独立观测数 $m$ 与平滑矩阵非零特征值 $h_j = \lambda_j / (1 + \lambda_j)$ 下，解析闭合期望残差噪声自由度为：

  $$
  \nu_r = m - 2\sum_j h_j + \sum_j h_j^2, \qquad \operatorname{Var}(\text{RSS}_{\rm noise}) \simeq 2\left[(m-r) + \sum_{j=1}^r (1-h_j)^4\right]
  $$

  当实际白化残差连同模型/先验偏差首次落入置信区间内立即停止 PCG 迭代（**Oracle**：$m=4$ 纯等权均值 $h=1 \implies \nu_r/m = 3/4$ ；若 $h=0.5 \implies 3.25/4$ ；无更新 $h=0 \implies 1$ ）。

### 2.7 `Q7 & §7.2` 跨焦位频谱消元、自然轴移放大率标尺与 3 级深度契约

- **消除未知纹理的跨焦位变量投影**：在各自对焦候选位置 $z$ ，消除未知表面高频纹理 $S_\omega$ 后对所有焦位帧直接评估跨帧投影一致性：

  $$
  \hat{S}_\omega(z) = \frac{\sum_k w_{k\omega} H_{k\omega}^*(z) Y_{k\omega}}{\sum_k w_{k\omega} |H_{k\omega}(z)|^2 + \lambda}, \qquad z^\star = \arg\min_z \sum_\omega \left[\sum_k w_{k\omega} |Y_{k\omega} - H_{k\omega}(z)\hat{S}_\omega(z)|^2 + \lambda |\hat{S}_\omega(z)|^2\right]
  $$

  要求严格区分独立纹理支撑与正则偏好（若各候选响应向量彼此成共线倍数，差异被 $\hat{S}_\omega$ 完全吸收，直接判为“深度不可辨”）；
- **无须移动对焦环的自标定特例（前重投影放大率 $m_k$ 标尺）**：对固定对焦屈光度 $D_f > 0$（$\mathrm{m}^{-1}$ ）、透镜常数 $K = f^2 / [n_f p(1 - f D_f)]$ 下的手持自然微前移 $s_k$ ，先在**补偿全局/局部呼吸倍率之前**保留实测放大率 $m_k = Z / (Z - s_k) \ne 1$ 。拉回参考平面 $x_{\rm src} = m_k x_{\rm ref}$ 后参考格 CoC 直径变为 $\tilde{c}_k = c_k / m_k = K(D_0 - D_f / m_k)$ （其中 $D_0 = 1/Z$ 为物距屈光度），由低频 OTF 对数比测得 $\Delta \tilde{c}_k^2 = \tilde{c}_k^2 - \tilde{c}_0^2$ 后得线性可辨定尺方程：

  $$
  \frac{\Delta\tilde{c}_k^2}{K^2(1 - 1/m_k)} = 2 D_0 D_f - D_f^2\left(1 + \frac{1}{m_k}\right)
  $$

  在已知有界 $D_f > 0$ 与 $m_k \ne 1$ 且多深度区校验分离呼吸/OIS 后，即可绕开自由尺度与近远对称 $(c_0,a)\leftrightarrow(-c_0,-a)$ 歧义直接解得物理物距 $D_0$ ；结合 Fisher 可检测度 $I_a = 2\sum_\omega |S_\omega|^2 \|P_\perp W_\omega^{1/2}\partial_a h_\omega\|^2$ 确立最小可辨模糊变化阈值；
- **三级深度契约输出规范**：严格区分为 `relative_depth`（归一化 `[0,1]` 视差/锐度排序）、`sign_calibrated`（经几何膨胀定号或跨焦非对称 V-curve 定号后的焦前/焦后符号）与 `metric_calibrated`（仅当 `§7.2` 标定焦步、$m_k$ + $D_f>0$ 标尺或外部视差基线三者至少具备其一时输出 `mm/m` ）。

### 2.8 `Q8 & §7.3` 共用轮廓六端点亚像素反混叠与裁剪 2D Voronoi 合成孔径

在已知深度断裂窄带内，不再分色独立抠图，而是令前景/背景由共享标量符号距离函数 $d(x; \vartheta) = 0$ 界定几何分界面（仅含法向偏移与斜率等低维几何参数），然后经每通道独立光学 PSF 与像元积分构造分色覆盖率 $\alpha_{kpc}(\vartheta) = [P_p H_{kc} W_k \mathbf{1}_{d>0}]$ （天然精确建模横向色差造成的同一物体边界红绿蓝不同扩散过渡带）。设前景与背景的共享 6 维端点颜色参数 $\beta = (G_F, C_{RF}, C_{BF}, G_B, C_{RB}, C_{BB})$ ，构造含纯端点正则的联合线性方程：

$$
(D^* W D + L^* C_e^{-1} L)\hat{\beta} = D^* W y + L^* C_e^{-1} m
$$

通过变量投影先消去 $\beta$ 再用信赖域步微调几何 $\vartheta$ 。其核心物理判据为覆盖率权重方差 $\det = s^2 \mathrm{Var}_w(\alpha)$：**当全部帧处于相同半透明重叠比例（如 $\alpha \equiv 0.5$ ）时无论堆叠多少帧，背景均处于数据零空间**，输出“背景未显露/仅前沿合成”标记；而在合成孔径重对焦端，与 Stage 3 共享同一可见性几何，默认按目标多边形光瞳内视点分布的 **裁剪 2D Voronoi 均匀几何面积** 积分（并保留 `--aperture-weight dwell` 供偏好停留时间加权的用户切换），确保在静止机位连拍多张而不增加视点角分布时不会误将某机位重量反复拉高（修正了 `ALGORITHM_PROPOSALS_ZH.md §8` 中的近似最近邻代理示例）。

### 2.9 `Q9 & §7.5` 行内与帧间联合谐波似然及单音唯一性证书

针对光学/交流调制（O2/PWM）与表面谐波测振（O5/O7），统一按实际源行中心时刻 $t_i = t_k + (r_{\rm src} - r_*)\tau_{\rm row}$ 与真实曝光核响应 $g_i(f) = \operatorname{sinc}(\pi f T_i)$ 装配设计矩阵 $D_f$ （含缓变 DC/几何基 $b_i^\top \eta$ 与同相正余弦两列），直接在原始样本空间执行单次变量投影最小残差搜索 $E(f) = \min_{\eta,a,b}\|W^{1/2}(y - D_f[\eta,a,b])\|^2$ 。对粗频候选附近所有帧间整圈数 $n_{kl} = \mathrm{round}[f_0 \Delta t_{kl} - (\hat\phi_k - \hat\phi_l)/(2\pi)]$ 进行严格邻域枚举。当仅存在等间隔全局快门或行内相位完全自由时，$f$ 与 $\pm f + m F_s$ 彻底同像；唯有借助真实非公度采样时序或跨行相干模态约束、且满足白化正交基子空间夹角条件 $A\sqrt{1 - \gamma_{fg}^2} > 2\epsilon$（其中 $\gamma_{fg} = \|Q_f^* Q_g\|_2$ ）时颁发“唯一主峰证书”，否则客观列示物理等价的多峰候选区间。

---

## §3 流式架构、局部光场、计算快门与科学交付

### 3.1 `F-A` 内存恒定双遍流式充分统计引擎（Zero Temp-Disk Streaming）

当预估全帧线性堆栈 $> 3\text{ GiB}$ 或 $N > 20$ 时（可通过 `--streaming <auto|on|off>` 手动覆盖，内存上限通过 `--mem-budget-gb` 在 `2/4/8/16 GiB` 切换）：
1. **Pass 1（先导图谱与压缩索引归档）**：顺序逐帧轻量解码获取缩略图、曝光倍率、全局/行时序运动轨迹及高频可疑残差位置索引，在流式模式下立即释放源帧大数组，仅常驻轻量级每帧参数；
2. **Pass 2（分条带高精度充分统计累加）**：按条带顺序解码单帧，精确投影回参考网格并实时累加正规方程充分统计三元组 $(H = \sum B_k^* Q_k B_k,\ h = \sum B_k^* Q_k d_k,\ c = \sum d_k^* Q_k d_k)$ 、Q1 共享协方差消元阵 $(L, G, g)$ 及逐色截顶计数；工作集完全由输出尺寸与分块带宽度决定，与输入帧数 $N$ 完全脱钩（$O(1)$ 内存开销 wrt $N$ ）；
3. **高疑义局部分窗二次回放**：对于动态遮挡窄带或强非刚性边界小块，依据容器内部压缩块偏移索引直接按帧序号原地重读小 ROI 并施以多帧精确对齐与稳健迭代，全程不产生任何临时转存大文件。此外严守动态范围诚实计量规则：等曝光同方差噪声增益严格汇报为 $\Delta\mathrm{DR} = \frac{1}{2}\log_2 N_{\rm eff}$ 档（$N_{\rm eff} = 1 / \sum \bar{w}_k^2$ ），明确区分短曝光提供的“高光上限扩展档位”与长曝光/多帧提供的“暗部底噪压缩档位”。

### 3.2 `F-B` 紧凑分层局部光场（EPI + Cat’s-eye + 穿网透视）

立足手持连拍已经过物理大光圈逐帧积分的现实事实，将局部时空场建模为分层极线光场 $L(x, a) \simeq \sum_\ell V_\ell(x, a) \sum_{j=0}^{r_\ell} c_{\ell j}(x + d_\ell a)\psi_j(a)$ ，仅在前/后分层遮挡与视角依赖反光（如金属漆、水波高光）区激活二阶角度基 $\psi_j(a)$ 。引入实拍点源标定或预设几何驱动的离轴口径蚀 **Cat’s-eye** 裁剪多边形瞳模型 $P_{k,x}$ ，单独分离几何遮挡孔径积分、总通光透过率与预览亮度补偿。针对铁丝网/栅格遮挡，仅在真实可见背景视角支撑下反解 `rear` 清晰图像并同步写出 `front`, `rear`, `coverage` 与 `angular-support` 供深度/合成交互；若手持轨迹严格退化为 1D 共线直线（$a_{k,y} = 0$ ），引擎主动降阶仅导出线内视角变化及 1D 线段孔径重对焦。

### 3.3 `F-C` 计算快门三件套（`C1` 包络 / `C2` $\kappa$-随动 / `C3` 联合去模糊）

- **`C1` 可编程时间包络**：求解非负权重约束最小二乘 $\min_{a_k \ge 0} \|\sum a_k w_k - q\|^2 + \lambda \sum a_k^2 V_k\ \mathrm{s.t.}\ \sum a_k = 1$ ，以安全无负通量放大的方式实现任意数字前/后帘（前段弱尾+末帧锐利主峰）、渐入、渐出与均匀时间积分；
- **`C2` $\kappa$-拉格朗日随动长曝**：沿时间连续流迹线 $\Phi_t(q)$ 提取相机线性辐射量，并按材质层自主可调的随动系数 $\kappa \in [0, 1]$ 将粒子辐射以面积覆盖权重精准散布至轨迹坐标 $q + \kappa[\Phi_t(q) - q]$ —— $\kappa = 0$ 将飞驰的车辆/舞者死死钉在传感器中心绝对锐利，$\kappa = 1$ 则把稳定背景拉出高度自然的动态速度线（支持主体与背景分设独立 $\kappa$ 值）；
- **`C3` 多曝光时长与方向互补联合去模糊**：从陀螺仪及相邻帧跨帧微流联合推导各帧内运动模糊传递核 $H_k(\theta) = \int w_k(t)e^{-i\theta\cdot\Delta_k(t)}\,dt$ 并代入公共 M5 正向模型。在高速与常规曝光混拍或双方向手抖下，各帧传递函数不同的 $\operatorname{sinc}$ 零点自然互为填充，信息增益正比于 $\sum |H_k(\theta)|^2 / V_k$ 。

### 3.4 `F-E` 零第三方依赖 Multi-Part Tiled OpenEXR 主底片交付规范

在 `heraw_suite_exr_stream.hpp` 内复用工程既有的 `zlib` 库自主封装无外部 SDK 依赖的标准分块多 Part OpenEXR（默认 `HALF FP16 + ZIP`，`sci` 支持 `FLOAT32` 及无符号整型 `UINT` 精确编解码）：
- **Part 0（Camera-Linear Master Negative）**：未经过非线性压扩与色彩空间钳位的原始相机原生线性 RGB（完整保留 `E12` 负值阴影散粒涨落与 $>1.0$ 恢复高光），内嵌 DNG/EXIF 标定的 `chromaticities`, `CameraToXYZ`, `whitePoint` 与曝光标定常数 `expGain`；
- **Part 1（Compositing Foreground/Background Layers）**：`FG.rgba`（严格标准 Premultiplied Alpha，满足 $C = F_{\rm premult} + (1 - \alpha) B$ 工业标准 `over` 合成定律）、`BG.rgb`、`FG.coverage` 与局部视差/运动速度向量 `MV.xy`；
- **Part 2（Scientific Uncertainty & Dynamic Range Metrology）**：每像素或半/块网格对称 $3\times 3$ RGB 协方差 6 独立分量 $C = [C_{rr}, C_{rg}, C_{rb}, C_{gg}, C_{gb}, C_{bb}]$ （严格分开命名高斯后验 $(F+\Lambda)^{-1}$ 与频率估计重复方差 $(F+\Lambda)^{-1}F(F+\Lambda)^{-1}$ ）、任意光度矢量 $l$ 对应 $\sigma_L = \sqrt{l^\top C l}$ 与 $\mathrm{SNR}_L$ 、可用剩余曝光亮端余量 stops（$\log_2(\max_{k\in{\rm vis}} L_{kpc}^{\rm sat} / \hat{L}_{pc})$ ）、全截顶置信下界掩膜及真实有效采样帧数 $N_{\rm eff}$ ；
- **Part 3（HDR GainMap）**：标准对数显示增益层 $G = \log_2[(L_{\rm HDR} + \epsilon)/(L_{\rm SDR} + \epsilon)]$ 与基准白/增益范围元数据；
- **Part 4（Optical ScatterField，启用光学散射滤镜时附加）**：独立存放纯粹由 `§4` 生成的散射光增量场 $\Delta X_{\rm scatter}$ ，便于后期在 DaVinci/Nuke 中无损微调滤镜浓度与叠加方式。

### 3.5 `F-G` 下一张拍什么主动信息规划器 & `F-H` 双曝光窗频谱消歧

- **`F-G` 边缘信息增益规划器**（复用 [heraw_burst_planner.hpp](../heraw_burst_planner.hpp)）：在当前连拍预检草图的四相信息谱基础上，对可选机身操作 $a$（横向补微移、短曝抑高光截顶、Focus Step 跨越）实时估算净增量收益 $\Delta(a) = \sum_{t,b} v_{tb}[\log\det(\Lambda + F_{tb} + \Delta F_{tb}(a)) - \log\det(\Lambda + F_{tb})] - c(a)$ ，立即提醒拍摄者“当前缺竖直 0.5 px 亚像素相位”或“当前高光全截顶亟需 $-3\ \mathrm{EV}$ 补拍”。
- **`F-H` 广义全域快门双窗鉴频**：在 A9 III 等无行延迟机身下，$f$ 与 $mF_s \pm f$ 在单窗采样中物理全等；但在两次不同已知脉宽 $T_1, T_2$ 下捕获同一稳定谐波光源时，可借助幅度响应比 $r = |\operatorname{sinc}(\pi f T_2) / \operatorname{sinc}(\pi f T_1)|$ 实现整数倍采样率镜像解耦（例 $120\ \mathrm{fps}$ 下 $50\ \mathrm{Hz}$ vs $70\ \mathrm{Hz}$ 在 $T_1=1\text{ ms}, T_2=8\text{ ms}$ 对应响应比 **`0.760` vs `0.563`** ，高信噪比下可信分辨）。

### 3.6 `§7.4` 三假设天文点源护航与 `O6–O10` 扩展科学观测器

- **天文点源与传感器坏点三假设裁决 (`§7.4`)**：取代单帧 $9.5\sigma$ 孤点硬中值抹杀策略，首遍仅登记极端孤立候选点，在完成天空星表高精度亚像素对齐后于同一原始 CFA 采样点联合比对三个零误差基准模型：$H_s$（随天体几何平移的点状星象 PSF ＋ 物理非负自由通道通量）、$H_h$（死锁在固定感光点行/列的常偏置热噪点）、$H_t$（仅现身极少帧的流星/卫星/宇宙射线瞬变体）。仅当证据压倒性支持 $H_h$ 时标记坏点，彻底杜绝将锐利窄谱红星当作坏点削平；
- **`O6` LoCA 横/轴向色差自动定焦前焦后符号**：校正横向 LCA 后，利用先验标定的不同距离/光圈下 R/B 离焦 OTF 响应差消去公共表面纹理 $S(\omega)$ ，在对称单波段无从定号的场景利用跨波段差异直接定出客观正负离焦符号（无色散复消色差镜头或纯色目标客观退回不可定号）；
- **`O7` 物体表面微米驻波/声学模态成像**：在高对比边缘将小位移线性化为 $\delta I \simeq \nabla I \cdot u$ ，联合 `Q9` 行时序拟合空间固有振动模态轮廓、节线位置与相对振幅相角；
- **`O8` 大气视宁度（$r_0$ 与角相干尺度）**：对齐消除望远镜/机身共同 tip/tilt 抖动后，对长时间平均星像 OTF 拟合 Kolmogorov 模型 $H_{\rm atm}(\nu) = \exp[-3.44(\lambda \nu / r_0)^{5/3}]$ 并监测多恒星之间的差分视宁度漂移；
- **`O9` 流动水体下清晰水底重构**：利用多帧联合交替估计共同平稳河床纹理 $S$ 与低维光滑折射位移流 $\mathbf{u}_k(x)$ （以中位映射固定规范 norm-gauge 防整体坐标系漂移），输出绝对透明的水底共识图像与瞬时折射矢量场；
- **`O10` 暗弱流星/人造卫星线段积分测光与角速度反演**：对齐恒星背景实现 WCS 定位，在减背景的原生 CFA 上沿轨迹拟合曝光积分后的线段 PSF，高精度导出分段流量变化、色指数与视角速度 $\omega$（预拍期同样进入积分记录，无双站视差时仅汇报无量纲角速度）。

---

## §4 专题深挖 I：统一物理光学散射滤镜引擎（Ricoh HDF / Black Pro-Mist / Tiffen Ultra Contrast / Film Halation）

本节在图像进入非线性色调曲线与显示编码之前的 **纯物理线性辐射域（反畸变、多帧 TNR / 超分融合完成之后，且在任意 Highlight Rolloff / Gamma 映射之前）** 建立统一两态光学传输模型，将物理实景拍摄中四类截然不同的玻璃滤镜与胶片光路效应统一到同一可扩展算子中，并给出专门用于 `Tiffen Ultra Contrast` 的数学与降噪分析。

### 4.1 物理机制差异：从入瞳相位散射到腔体广域环境光重分配

真实光学玻璃与胶片乳剂造成的画面辉光与反差控制源自两套互不重叠的物理光路机制：

1. **近域微粒相位/乳浊微散射（`Ricoh HDF`, `Black Pro-Mist`, `Film Halation`）**：
   - 入瞳处微米级透明聚合物漫射粒子形成微相位屏 $t(u) = \exp(i\phi_{\rm micro}(u))\cdot m_{\rm absorb}(u)$ （Black Mist 内掺杂微量黑色吸光微粒降低暗场反向弥散）。其傅里叶衍射核 $|\mathcal{F}\{t\}|^2$ 具有强烈的多尺度 **锐利核峰 + 近域平方反比拖尾（核心半径 $2\sim 120\ \mathrm{px}$ ）** ；
   - 因此强点光源与高亮高反差边缘周围会在短中距离内生长出清晰可见的 **局部光晕（Halo / Bloom）** ，同时伴随轻微高频调制传递函数衰减 $1 - \eta_{\rm near}$ ；
   - `Film Halation`（胶片光晕）则物理对应光子穿透银盐感蓝/感绿层与片基后，于背层反射并经感红层二次吸收（兼有微弱感绿溢出），形成极短截径（$3\sim 28\ \mathrm{px}$ ）的高饱和橙红边缘晕环。
2. **广域环境光重分配抑反差（`Tiffen Ultra Contrast` / UltraCon）**：
   - 获奥斯卡技术成就奖的 Tiffen Ultra Contrast 滤镜具备本质不同的微结构设计：**不从强点光源抽取能量在局部画圈形成 Halo/Glare，而是近似各向同性地捕获全画幅视野及入瞳广角范围内的环境光（Ambient Light）平均辐照度，将其几乎平坦或超低频地弥散至整块感光元件**；
   - 其散射点扩散函数的特征尺度跨越传感器全对角线级别（ $\sigma_{\rm wide} \sim 0.45\,D_{\rm diag}$ 配合大尺度均匀常数底分量），对微小点光源的面积稀释率达到 $A_{\rm spot} / A_{\rm frame} \sim 10^{-6}$ 量级！结果表现为：**高光边缘锐利度完整保留（$\mathrm{MTF}_{\rm norm}(\theta) \approx 1$ ）、几乎零局部亮环光晕，而整张画面的最深暗部沿着真实环境光的物理色温被平滑抬升 1～2.5 档动态细节**。

### 4.2 统一两域线性辐射散射方程与四类滤镜参数族

在相机原生线性白平衡归一空间（及导出端线性 sRGB / 线性 AdobeRGB 工作空间）记已完成无剪裁复原的输入辐射场为 $X_c(p)$ ，引入两类预处理场：超饱和高光恢复辐射 $\widetilde{X}_{{\rm rad},c}$ 与固角限幅广域环境光采样 $\widetilde{X}_{{\rm amb},c}$ 。定义统一散射输出算子：

$$
X_{{\rm out},c}(p) = \big(1 - \eta_{{\rm near},c} - \eta_{{\rm uc},c}\big)\, X_c(p) + \eta_{{\rm near},c} \sum_{m=1}^{5} w_{m,c}\, \big(G_{\sigma_m} * \widetilde{X}_{{\rm rad},c}\big)(p) + \eta_{{\rm uc},c}\, \mathcal{S}_{{\rm uc},c}\big[\widetilde{X}_{\rm amb}\big](p)
$$

其中 $\eta_{\rm near}, \eta_{\rm uc}$ 分别为近域漫射散射分数与广域 UltraCon 环境光抽取比例，$\sum_{m=1}^{5} w_{m,c} = 1$ 为以对角线归一化尺度 $s_0 = D_{\rm diag} / 4000$ 标定的 5 级多尺度高斯基底权值。四类经典滤镜对应参数与物理特征如下：

| 滤镜模式 `--diffusion-mode` | $\eta_{\rm near}$ 范围与特征尺度 $\sigma_{1..5}$ | $\eta_{\rm uc}$ 范围与广域参数 | 色彩耦合与核心物理属性 |
|---|---|---|---|
| `hdf`（理光 HDF 开关模拟） | $\eta_{\rm near} \in [0.06, 0.22]$，尺度 $\{2.5, 9, 28, 78, 190\}\,s_0\ \mathrm{px}$，仅对高辐射尾软阈值区激活近域爆发式辉光 | $\eta_{\rm uc} = 0$ | 近乎中性白偏微暖；局部强烈软焦圣光感与高光丝滑溢出，中间调/阴影对比保留硬朗黑位 |
| `black-mist`（黑柔 1/8–1） | $\eta_{\rm near} \in [0.025, 0.14]$，尺度 $\{1.8, 6, 18, 52, 135\}\,s_0\ \mathrm{px}$，含黑色碳粉吸收因子 $m_{\rm abs}$ 限制过长拖尾 | $\eta_{\rm uc} \in [0.004, 0.020]$ 微量全局黑位铺底 | 保持中间调锐利，黑位温和轻浮，高光呈均匀柔美光晕（电影人像经典） |
| `ultra-contrast`（**Tiffen Ultra Contrast `1/8..5`**） | **$\eta_{\rm near} \equiv 0$**（严格禁用局部短中程高斯卷积核） | **$\eta_{\rm uc}(g) \in \{0.008, 0.015, 0.026, 0.042, 0.065, 0.090, 0.118, 0.150\}$** 对应 Tiffen 实拍档位 `1/8, 1/4, 1/2, 1, 2, 3, 4, 5` | 极低高频调制损耗与 **零点光源光晕**；严格依真实场景环境光色温填充最深阴影动态细节 |
| `halation`（胶片背反射红晕） | 仅红通道主导与微绿渗透：$w_{\rm hal,R}=0.82,\, w_{\rm hal,G}=0.18,\, w_{\rm hal,B}=0.00$ ，短截径 $\{1.5, 4.5, 11, 24\}\,s_0\ \mathrm{px}$ | $\eta_{\rm uc} = 0$ | 由阈值之上过曝光源触发橙红色边缘弥散，模拟无防光晕层高速电影胶片物理回弹 |

### 4.3 `Tiffen Ultra Contrast` 数学建模、面积稀释证明与四项工程护城河

#### (1) 广域环境光积分泛函与零光晕面积稀释证明

定义 Ultra Contrast 广域散射场 $\mathcal{S}_{{\rm uc},c}$ 为大高斯平滑与全画幅立体角加权直流的凸组合：

$$
\widetilde{E}_{{\rm uc},c}(p) = (1 - \rho_{\rm dc})\,\big(G_{\sigma_{\rm wide}} * \widetilde{X}_{{\rm amb},c}^{(\beta_{\rm edge})}\big)(p) + \rho_{\rm dc}\,\overline{X}_{{\rm amb},c}^{(\beta_{\rm edge})}, \qquad \sigma_{\rm wide} = 0.45\, D_{\rm diag},\quad \rho_{\rm dc} = 0.65
$$

$$\mathcal{S}_{{\rm uc},c}\big[\widetilde{X}_{\rm amb}\big](p) = \Big((1 - \chi_{\rm warm}) + \chi_{\rm warm}\, w_{{\rm warm},c}\Big)\,\widetilde{E}_{{\rm uc},c}(p), \qquad w_{\rm warm} = \{1.06,\, 1.00,\, 0.90\}$$

其中 `--uc-chroma <neutral|warm-glass>`（默认 `neutral` 保留中性/环境固有色散射响应；设为 `warm-glass` 时叠加光学暖色玻璃光谱透射系数 $w_{\rm warm} = \{1.06, 1.00, 0.90\}$ 并在 `UltraCon` / `Black Mist` 中同步温和提升红蓝倾角）。

**零点光源光晕与高频零折损的定量证明：**
假设暗室场景平均背景 $\overline{E} = 0.01$ ，画面中含一盏面积占总像素百万分之一（$A_0/A_{\rm frame} = 10^{-6}$ ）、极高辐照度 $E_0 = 80$ 的过曝车灯，开启 **UltraCon #2** ($\eta_{\rm uc} = 0.065$ )：
- 对车灯旁像素：$\rho_{\rm dc} \cdot \eta_{\rm uc} \cdot (A_0/A_{\rm frame})\cdot E_0 = 0.65 \times 0.065 \times 10^{-6} \times 80 \approx \mathbf{3.38 \times 10^{-6}}$ ；大核卷积高斯肩峰值亦低于 **$1.5 \times 10^{-5}$**，人眼与 16-bit 量化彻底不可觉，车灯边缘锐度保留率 $(1-\eta_{\rm uc}) = \mathbf{93.5\%}$ ，高频归一化调制传递 $\mathrm{MTF}_{\rm norm}(\theta) = 1.000$ ；
- 反之，开启 **HDF / Black Mist #1** 仅需 $\eta_{\rm near}=0.10, w_2=0.3$ 即可在车灯外围 10 px 处投射高达 **$\mathbf{+0.026}$** 的璀璨发光光环（与 UltraCon 差距达 **$4$ 个数量级**）；
- 切换到严酷日光逆光户外：全局平均环境光 $\overline{E}_{\rm amb} = 0.18$ ，原本深死黑处阴影值 $X = 0.004$ 经过 **UltraCon #2** 后线性抬升为：
  $$X_{\rm out} = 0.935 \times 0.004 + 0.065 \times 0.18 = 0.00374 + 0.01170 = \mathbf{0.01544}$$
  即深暗部瞬间被干净无损地提升 **$+1.95\ \mathrm{stops}$** ，而同画面 $0.18$ 中灰处（$0.935\times 0.18 + 0.065\times 0.18 = 0.1800$ ）曝光量保持 **`0.00 EV`** 零偏移！

#### (2) 四大核心数值难点与针对性物理对策

| 物理/数值陷阱 | 在朴素数字实现中的后果 | `nijinLBS` 精确解决方案 |
|---|---|---|
| **1. 高光硬截顶与奇异点反冲** | 单纯截顶在 `1.0` 会严重低估夜景霓虹/白天的实际积分总光通量；而若仅按点扩展反演把一个焊点抬到 `10^4` 又会使整帧平白发灰 | 由 `Q5` 上尾似然和饱和翼形轮廓恢复真实过曝亮度 $\widetilde{X}_{\rm rad}$ 后，在构建环境场 $\widetilde{X}_{\rm amb}$ 时施加**入瞳立体角微积分上限（Winsorized Solid-Angle Cap $\tau_{\rm sa} \approx 16 \cdot \overline{E}_{95\%}$ ）**，使点源在广域微积分中服从有限入瞳光学面积守恒 |
| **2. 遮光斗与画外杂散天光边界 (`--uc-baffle`)** | 物理镜头无遮光斗时，画幅外的明亮天空光也会斜射入 Tiffen UltraCon 滤镜散射层；若数字滤波简单使用零填充或边缘镜像就会歪曲实际边角环境光 | 显式设置三档物理边界条件 $\beta_{\rm edge}$：<br>• `hooded`（兼容别名 `matte-box`，$\beta_{\rm edge}=0$）：等效加装电影遮光斗，采用渐近背景均衡延拓；<br>• `standard`（兼容别名 `hood`，$\beta_{\rm edge}=0.35$，默认）：常规遮光罩，按顶部边缘 10% 天光亮度加权外延 25% 虚边；<br>• `wide-flare`（兼容别名 `unbaffled`，$\beta_{\rm edge}=0.70$）：裸镜外光渗透，上半球加强漫射补偿 |
| **3. 光学真玻璃 vs 单帧拉曲线 vs 多帧融合的信噪比差异** | **物理真实滤镜**的光子散射发生于传感前，因此会在暗部额外注入一份等效光学散粒噪声 $\Delta\sigma^2_{\rm glass} = g^{-1} \eta_{\rm uc} \overline{E}_{\rm amb}$；而**单帧后期拉阴影曲线**会把传感器读噪 $\sigma_{\rm read}^2$ 放大 $(T'(0))^2 \sim 25\times$ | 在 **`nijinLBS` 多帧线性合并 + `E12` 负半轴无偏保留之后** 再运行数学 UltraCon，其输出像素总协方差严格满足：<br>$$C_{\rm out}(p) \simeq (1-\eta_{\rm uc})^2 C_{\rm TNR}(p) + \eta_{\rm uc}^2 C_{\rm amb}$$<br>多帧融合已经将原本受限于读噪的暗部方差除以 $N_{\rm eff}$（$4\sim 100\times$ ），且无额外前置光学散粒噪声污染与单帧非线性拉升带来的彩噪块放大，实现**全链路最优暗部 $\mathrm{SNR}_L$ 与纯净微反差**！ |
| **4. 遮光斗双滤镜物理级联叠加** | 好莱坞实拍常在遮光斗内将 **Ultra Contrast + Black Pro-Mist / HDF / Halation** 前后共轴串接；简单线性相加会破坏总光通量守恒（能量不守恒） | 严格采用非交换物理级联乘法算子（先近景/环境大散射后乳剂/窄域光晕，亦支持全量程守恒重整）：<br>$$X_{\rm final} = \mathcal{O}_{\rm hal} \circ \mathcal{O}_{\rm near} \circ \mathcal{O}_{\rm uc}(X_{\rm in})$$<br>并可在 Part 4 `ScatterField` EXR 独立交付各分量净增量 $\Delta X$ 供调色师实时微调配比 |

#### (3) `< 8 ms` 高效实现（6 级降采样高斯金字塔 + 亚像素双线性升采样）与检验 Oracle

对于 $\sigma_{\rm wide} \sim 2000\ \mathrm{px}$ 级别超大半径，空域或满画幅直接 FFT 耗时惊人。在 CPU / Metal / Vulkan 上实现统一的 6 层 $2\times$ 减采样金字塔（仅需将主图像按 $1/2 \to 1/4 \to 1/8 \to 1/16 \to 1/32 \to 1/64$ 做抗混叠降采样，仅于最小 $1/32$ 与 $1/64$ 低分辨率层执行可分离 $7\times 7$ 高斯卷积并累加全画幅三通道均值，最后双三次顺滑上采样线性融回主缓冲），在 `45 MP` M1 Max 与 RTX 4080 上单帧全流水散射开销均 **低于 $8\ \mathrm{ms}$** 。

- **`Oracle-DF1`（能量精确守恒）**：任意全均匀辐射场 $X \equiv c_0$ 下，四类滤镜输出严格等于 $c_0$（精度误差 $< 10^{-5}$ ）；
- **`Oracle-DF2`（中灰零位移不变性）**：均匀背景 $\overline{E}_{\rm amb} = 0.18$ 场景上，中灰测试块经 UltraCon `#1..#5` 前后曝光量变化精确等于 $0.000\ \mathrm{EV}$ ；
- **`Oracle-DF3`（零光晕分离验证）**：于 `0.005` 暗场中心放一个半径 4 px、亮度 $50$ 的理想硬边圆盘光源，距光源边缘 6 px 处 UltraCon #3 的净亮环增量严格 $< 10^{-4}$ ，而相同对比度抬升量下 HDF/Black Mist 的 6 px 光晕 $> 10^{-2}$（超 2 个数量级差距）；
- **`Oracle-DF4`（环境光色温真实传色）**：在蓝色冷调高环境光场景下启用 UltraCon（$\chi_{\rm color} = 1$ ），死黑区域阴影矢量严格沿环境光色向偏移，而不产生中性灰浊雾；
- **`Oracle-DF5`（多帧降噪后串联噪声验证）**：检验独立多帧合并后 UltraCon 输出暗部方差满足 $\mathrm{Var}_{\rm out} \le (1-\eta_{\rm uc})^2 \mathrm{Var}_{\rm TNR} + O(\eta_{\rm uc}^2 P^{-1})$ ，杜绝额外散粒噪声误生成或非线性方差畸变。

---

## §5 专题深挖 II：无防抖手持抖动 + 运动/演唱会双运动场分析与增强引擎

在演唱会现场、体育抓拍与便携无防抖相机弱光拍摄时，观测过程被“**手持六自由度高频微震（随快门扫频）** + **主体局部大位移非刚性关节/姿态形变** + **极端窄带 LED 光电频闪与舞台激光雾气**”三重叠加效应统治。

### 5.1 实测代码库四大瓶颈精准定位

通过对现有管线代码（[heraw_sr2x.cpp](../heraw_sr2x.cpp)、[heraw_suite_core.hpp](../heraw_suite_core.hpp)）逐行核验，定位到以下四处根本性瓶颈：

| 实测代码锚点 | 现有代码实际行为 | 运动/演唱会场景下的物理失效表现 |
|---|---|---|
| [heraw_sr2x.cpp:3051–3140](../heraw_sr2x.cpp#L3051) (`recover_rigid_motion_patches`) | 原基线：仅以静态背景流 `(fy_io, fx_io)` 为中心做单帧 **$\pm 20\text{ half-px}$** 无递推搜索，直接跳跃对齐 `k` 与参考帧 `0` | 演员跨出 1–2 个身位或挥手累积偏移超过 $40\text{ native-px}$ 易丢失；v3 通过 `SIM(2)` + 链式外推修复 |
| [heraw_sr2x.cpp:3051](../heraw_sr2x.cpp#L3051) | **Gate 1** 原硬编码 `bm.ncc >= 0.90`；**Gate 2** 比对缺乏本地光照增益校准的绝对光度残差 | 高 ISO 噪声补丁的理论 ZNCC 天花板 $\rho_{\max} = \sigma_{\rm sig}^2 / (\sigma_{\rm sig}^2 + \sigma_n^2)$ 在 $\mathrm{SNR} < 2.85$ 时小于 `0.90`；v3 按噪声上限自适应门控 |
| [heraw_suite_core.hpp:833](../heraw_suite_core.hpp#L833) (`row_rolling_shutter_smooth`) | 对每一帧的每行计算横向中位数 `detail::median(values)` 做一维均值平滑 | **手腕原地滚转抖动（Thumb-pivot Roll） $\omega_z(t_r)$** 引发反对称垂直流场，横向中位数相消；v3 由 8 节点联合 $SO(3)$ + `trans_epip` 样条处理 |
| [heraw_sr2x.cpp:6414](../heraw_sr2x.cpp#L6414) & [L9181 / L9651](../heraw_sr2x.cpp#L9181) | **1)** 帧级全局质量罚因子 `w_frame = pow(s_ratio, 4.0)` 一刀切降低整帧权值；<br>**2)** 原 `highlight_rolloff` 线性混白造成窄带 LED 漂白 | **1)** 改由 16 行带 `compute_rowband_lucky_weights` 局部救援锐利行带；<br>**2)** `--highlight-mode oklab-stage` 于 OkLab 等色相轨线压缩纯蓝/品红 LED |

### 5.2 五大数学建模与核心增强引擎升级 (`--motion-profile <auto|standard|concert|sports|dxo-superraw>`)

#### 升级 A：物理噪顶自适应轨迹链接与局部 4-DOF $SIM(2)$ 运动追随（解决丢失与过严淘汰）

1. **噪声上限自适应相关性门控**：按实测局部信号方差 $\hat{\sigma}_{\rm sig}^2 = \max(0, \mathrm{Var}(P_0) - \hat{\sigma}_n^2)$ 实时计算当前纹理噪声允许理论最高 ZNCC：
   $$\hat{\rho}_{\max} = \frac{\hat{\sigma}_{\rm sig}^2}{\hat{\sigma}_{\rm sig}^2 + \hat{\sigma}_n^2}, \qquad \tau_{\rm ZNCC, eff} = \max\big(0.62,\ 0.92\,\hat{\rho}_{\max}\big)$$
2. **时序链式追踪递推 ($k-1 \to k$) + $SIM(2)$ 局部配准**：对前一帧成功追踪的刚性/微关节微块，以相邻帧速度外推预测 $\hat{u}_{0\to k} = u_{0\to k-1} + v_{k-1}$ 为搜寻原点向邻帧链式衔接再与锚帧复核，追踪极限位移由 `40 px` 扩展至 **`> 300 px`** 。对追踪补丁引入本地闭式线性最优照明乘子与局部旋转/微缩（4 自由度 $tx, ty, \delta\theta, \delta s$ ）：
   $$\hat{\gamma}_k = \operatorname{clip}\!\left(\frac{\langle P_0, P_k\rangle_W}{\|P_0\|_W^2},\ 0.55,\ 1.80\right), \qquad r_k = P_k(W_{SIM(2)}(\cdot)) - \hat{\gamma}_k P_0$$
   在剧烈闪灯或人物微仰面侧身瞬间依然稳健实现主体跨帧多帧降噪与亚像素超分。

#### 升级 B：8 节点行时间三次 B 样条 $SO(3)$ 卷帘果冻/滚转消去

将 `row_rolling_shutter_smooth` 升级为物理相机行时间 $t_r = t_k + (r - H/2)\tau_{\rm row}$ 参数化的 8 节点（每帧 8 个时间控制点、跨相邻帧保持 $C^1$ 连续）三轴旋转微平移模型：

$$
\mathbf{u}_k(r, c) = \mathbf{K}\big[\mathbf{R}(\boldsymbol{\theta}(t_{k,r})) - \mathbf{I}\big]\mathbf{K}^{-1}\begin{bmatrix}c\\ r\\ 1\end{bmatrix} + \frac{\mathbf{t}_{\rm epip}(t_{k,r})}{Z(r, c)}
$$

在所有静态纹理内点上极小化鲁棒重投影残差求解各节点的微旋转 $(\theta_x, \theta_y, \theta_z)$ ，一次性同时提取并抵消了横移（Yaw）、俯仰（Pitch）、**拇指压按快门导致的面内滚转 $\omega_z(t_r)$** 与远近景不同倍率的果冻切变，大幅减轻 Stage 3 稠密光流负担与局部伪影。

#### 升级 C：Action-Peak 参考帧优选与行带级幸运加权 $w_{\rm luck}(k, b_y)$

- **Action-Peak 锚点自选**：告别固定取第 0 帧。联合每个帧的主体表面瞬时静止度（跨相邻帧光流绝对速度极小值 $\min_k \|v_{\rm subj}(t_k)\|$ ：即跳跃最高点、转身停顿、口型清晰咬字瞬间）与背景局部结构清晰度得分选择最佳画质锚 $k^\star$ （坐标仍支持映射回首帧）；
- **行带幸运救援**：按传感器读取时序均匀分为 8–16 条行带 $b_y$ ，分别计算局域无偏梯度尖锐度并与噪声底解耦后给出行带幸运权值 $w_{\rm luck}(k, b_y)$ —— 手持抖动即使划伤某帧的上半幅，其依然绝对锐利的下半幅主体（舞台乐手与手指等）照样全权参投并大幅拉升主体信噪比。

#### 升级 D：演唱会光电四件套（OkLab 舞台灯保色 + 多域 PWM 去频闪 + LED 墙抑制 + 激光体光拆分）

1. **OkLab 恒定色相高光压缩 + `Q5` 饱和色向量外推 (`--highlight-mode oklab-stage`)**：当主蓝色/品红/翠绿舞台 LED 主导通道先触顶而另两通道处于正常区间时，由未饱和帧和相邻高光边缘点推定真实激光光源色度向量 $\hat{c}_{\rm LED}$ ，配合 `Q5` 上截顶生存似然防止高光过早变平发白；于导出显示层（[heraw_sr2x.cpp:9184, 9665](../heraw_sr2x.cpp#L9184) ）在知觉均匀 **OkLab 空间仅沿明度 $L$ 压缩而保持色相角 $h = \mathrm{atan2}(b, a)$ 绝对恒定并循 sRGB/AdobeRGB 色域边界柔和退让**，再现纯净透亮而不溃散断层的激光与霓虹质感；
2. **多照明空间聚类 PWM 解耦 (`Q9` 扩展)**：舞台同时存在 `50/60 Hz` 市电聚光灯、`250/500 Hz` 追光灯与 `1.2–4 kHz` 恒流驱动大屏与灯条。先在原行旁路按灯影辐射分簇提取空间分区各自的载波基 $\{f_m\}$，对不同空间照明区域独立运行 `Q9` 带曝光窗同步拟合，同时保留背景大频段纯净、防止误将主唱面部正常表情动态当成舞台灯光波纹抹平；
3. **舞台大屏 LED 点阵陷波抗假超分**：剧场舞台大屏拥有强亚像素周期点阵物理栅格；在 `Q2/Q3` 谱图诊断中若探测到单调规律的高能窄带点阵峰值（$\exists \theta_{\rm wall}$ 呈现严格周期锐峰），对该处屏幕内容局部降权抑制高阶超分增益以防止出现刺耳失真的网格莫尔伪纹；
4. **空气体光束（Tyndall Beam）低秩分离降噪**：干冰雾气下的追光激光束属于低频光滑各向异性连续场，利用稳健多尺度分离分解 $X = X_{\rm stage\_sharp} + B_{\rm beam\_lowrank}$ ：对极平滑的体光部分 $B_{\rm beam}$ 执行大跨度多帧全域融合彻底扫平高 ISO 粒子噪点，仅对穿透其中的歌手发丝、乐器弦细节 $X_{\rm stage\_sharp}$ 保守保持高频锐度。

#### 升级 E：无尺度先验信息提取（生物力学腾空高度）与 $\kappa$-计算追焦摇拍

- **零距离标尺绝对跳跃高度测算（Scale-Free Kinematics）**：在已知准确 `PTS_TICKS` 或硬件行频的连拍（如 A9 III `120 fps`、`Z8 60p/120p` ）中，通过精细跟踪舞台歌手跃起离地瞬态（第 $k_1$ 帧相应行时刻 $t_{\rm up}$ ）与最高点至落地重触瞬态（第 $k_2$ 帧对应行时刻 $t_{\rm down}$ ），不依赖任何物距与焦距标定，仅凭地球重力加速度即可直接高精度测出纯粹物理滞空垂直起跳高度：

  $$
  h_{\rm jump} = \frac{1}{8}\, g_0\, (\Delta t_{\rm hang})^2, \qquad g_0 = 9.80665\ \mathrm{m/s}^2
  $$

  以典型起跳 $\Delta t_{\rm hang} = 0.60\ \mathrm{s}$（对应真实起跳高度 $44.1\ \mathrm{cm}$ ）为例，即使时间定位存在 $\pm 8\ \mathrm{ms}$ 半帧行读出误差，身高反算高度绝对精度依然高达 **$\pm 1.2\ \mathrm{cm}$** ；配合 `120 Hz` 全域快门甚至精确至 **$\pm 0.5\ \mathrm{cm}$** ；
- **乐器微振动/嗓音颤音频谱分析**：利用 `O5/O7` 提取吉他琴弦或鼓皮在多帧跨行时间流中的微观节点位移，输出无量纲颤振频率曲线；
- **手持防抖摇拍动态拉丝增强 (`§3.3 C2`)**：把原本可能拍糊了的手持舞台/赛场抓拍自动通过前景对象分割锁定 $\kappa_{\rm fg} = 0$ 并设背景 $\kappa_{\rm bg} \in [0.4, 1.0]$ ，直接输出媲美光学摇拍慢门、主角纤毫毕现而舞美背景丝滑流动的计算追焦成片。

---

## §6 专题深挖 III：原生 4 帧无防抖 `DxO ONE SuperRAW` (`.DXO`) 极限挖掘方案

遵循用户明确边界：**保持 DxO ONE 原厂无改造固件的 4 帧同曝光连拍模式 (`1j4r_tnr` `.DXO` 多 IFD DNG）与机身 108 g 零光学/机身防抖物理结构**，直接立足从原机 RTOS 二进制固件与逆向剖析证实的一手硬参数建立专用适配分支 `--motion-profile dxo-superraw`。

### 6.1 固件实测底层硬件与现有 PhotoLab `DxOTNR` 能力边界

由 `dxo_one_re/extracted/android/rtos_raw.bin` 及公开镜头档案实测确认：
- **硬件与成像参数**：Sony `IMX183CQJ-J` 1 英寸背照式 CMOS，全画幅物理尺寸 $5472 \times 3648$ ，有效 DNG 输出裁剪尺寸 **$5216 \times 3472$** ，像元间距 $p = 2.4\ \mu\mathrm{m}$ （12-bit 线性 RAW），搭配等效 32 mm 实际焦距 **$f = 11.9\ \mathrm{mm}$** $f/1.8\text{–}11$ 定焦镜头（原生像素焦距 $f_{1x} \approx \mathbf{4958.3\ \mathrm{px}}$ ）、超轻量 **$108\ \mathrm{g}$** 机身，**完全没有任何 OIS 光学或 IBIS 传感器位移防抖**；
- **实测传感器时序常数 (`rtos_raw.bin @ 0xce1cc IMX183ModeInfoList`)**：
  - 像素主时钟 `INCK = 72,000,000 Hz` ；
  - 行总时钟周数 `HMAX = 876` $\implies$ 单行精准读出间隔 $\tau_{\rm row} = 876 / 72\times 10^6 = \mathbf{12.1667\ \mu\mathrm{s}}$ ；
  - 帧总行数 `VMAX = 3739` $\implies$ 全帧读出与连拍周期 $T_{\rm frame} = 3739 \times 12.1667\ \mu\mathrm{s} = \mathbf{45.491\ \mathrm{ms}}$ （标称帧率 **$22000/1001 = 21.978\ \mathrm{fps}$** ）；
  - 全高 3648 物理行活性扫描时间 **$T_{\rm scan, full} = 44.38\ \mathrm{ms}$** ，对应 3472 DNG 裁剪区实际滚快扫描时间 **$T_{\rm read} = 42.24\ \mathrm{ms}$** ；
  - **50 Hz 市电下天然相消发现**：在 $100\ \mathrm{Hz}$ 交流灯光闪烁周期内，相邻两帧帧间的精准时间差 $45.491\ \mathrm{ms}$ 造成相邻帧市电交流相位精确步进：
    $$\Delta\phi_{100} = 2\pi \times 100 \times 0.045491\ \mathrm{rad} = \mathbf{197.7^\circ \approx \pi}$$
    换言之，奇帧 `(1, 3)` 与偶帧 `(0, 2)` 天然落在交流灯光亮度波的几乎反相位置！
- **与原厂 PhotoLab `DxOTNR` 的工程差距诊断**：PhotoLab 将 4 帧（`1j4r_tnr` @ `0x2540c` ，独立于包围模式 `bias_3j3r` @ `0x2544c` ）硬编码锚定在 **IFD 0（恰恰最容易受按下机身实体快门刹那的下压拇指抖动污染！）** ，仅利用单一 G 通道做全局刚性仿射配准 + 抗鬼影并做 **$1\times$ Bayer 均权降噪**（向 PRIME 送出 `TNRNoiseAttenuationMap = -ln(N_eff)` ），完全舍弃行时序卷帘畸变补偿，也无高频可辨域的亚像素 $2\times$ 超分重建能力。

### 6.2 `DxO ONE` 4 帧极客榨干五项定制化策略

1. **原生 `.DXO` / `.4RW` 多 IFD 容器解析器与硬件时序参数注入（`heraw_suite_measurement.hpp`）**：
   - 经内置 [heraw_suite_measurement.hpp](../heraw_suite_measurement.hpp) 中的 `probe_dxo_container` + `read_dxo_frame_rows_u16` 原生解析 TIFF/DNG 子 IFD 链表（支持 4 帧内部 RAW IFD 展开、`MakerNote Tag 15` 每帧曝光起止时间窗及 `147–252` 变长 `LinearizationTable` `Tag 50712` 与尾端步长方差注入），直接将 `IMX183CQJ-J` 硬件实测常数 $\tau_{\rm row} = 12.1667\ \mu\mathrm{s}$ 、$T_{\rm frame} = 45.491\ \mathrm{ms}$ 、$f_{1x} = 4958.3\ \mathrm{px}$ 绑定至 `P0-CLK` 时钟、逐簇 4 帧 `2×` 门槛（`cluster_min_burst_frames`）与 `lookup_sensor_readout_ms`；
2. **智能最锐锚点替补（击碎“按下快门第 0 帧必糊”宿命）**：
   - 无防抖超轻小相机在食指下按瞬间第 0 帧常常出现明显纵向下压角速度尖峰；利用 **8 行带清晰度加权方差 + 行间局部切变幅值最小原则** 在 4 帧中遴选最佳帧 $k^\star \in \{0,1,2,3\}$ （实践中通常为帧 2 或帧 3）确立为画质和参考重心锚；
3. **42.24 ms 全程 8 节点 $SO(3)$ 滚快矫正 + 8 行带幸运救援**：
   - `42.24 ms` 的跨度相当于极易晃出果冻扭曲的长扫帧。由于仅 $108\ \mathrm{g}$ 机身无防抖手振频率通常集中在 $4\sim 14\ \mathrm{Hz}$ ，正好落在 $45\ \mathrm{ms}$ 帧扫读周期半周附近！采用 `§5.2 升级 B` 的 **8 节点 B 样条 $SO(3)$ 模型** 按行精准去除垂直斜切、局部扭曲与面内拇指滚转后，再通过 **8 条带级幸运融合** 从“局部偶发抖动”的帧中救回其余清晰行带，使有效样本利用率从传统全局退帧的 `< 50%` 提升至 **`> 85%`** ；
4. **严格按“4 帧真实物理秩边界”执行自适应 `1× ↔ 2×` 分辨率开关（`Q2 + Q6` 合流）**：
   - 由 `E03` 独立计数公理直面物理现实：若单帧含 $P$ 个原生 Bayer 感光点（即 $P/4$ 个 $2\times 2$ 单元），则在 $2\times$ 输出格（共 $4P$ 个像素，即每单元 $4\times 4=16$ 像素）上，三色完全独立共需 **$12P$ 个未知数（每通道 $4P$ 个）**；而原厂 **4 帧合集总计仅提供 $4P$ 个标量观测（$2P$ 绿 $+ P$ 红 $+ P$ 蓝，即每单元 16 个标量方程）**。纯无先验独立 $2\times$ R/B 分别呈现 $1:4$ 重度欠定（至少需 16 帧），纯独立 $2\times$ G 亦需 8 帧。唯有联合全彩光度（由 $C_+, C_-$ 色差先验耦合后提供 $4P$ 条亮度关联方程对应 $4P$ 个 $2\times$ 亮度未知数 $Y$ ），在理想四重亚像素分相条件下方正好触及 **`2×` 亮度临界满秩线**！
   - 因此在 `--motion-profile dxo-superraw` 下建立诚实且最优化的分工策略：
     - **亮度高分辨 ($Y$)**：通过全 4 帧共 $4P$ 采样在色线先验支持下求解 $4\times 4$ 亮度四相矩阵，凡经 `Q1` 扣除锚帧共享误差并在 `Q3` 谱图测得局部 $\lambda_{\min}(F_Y) > \tau$ 且非高 ISO 噪崩区时，正大光明解锁真实 $2\times$ 无伪影细节超级分辨（对比 PhotoLab 的纯 $1\times$ Bayer 均权获得真实光学分辨力跃迁！）；
     - **色度稳定性 ($C_+, C_-$)**：完全遵循 `Q2` 由可靠低频邻域标定的色线约束 $\Lambda_C$ ，并逐区记录数据支撑度 $s_\ell$ 防止弱秩下的彩色高频噪斑爆炸；
     - **智能回退 `Q6 Bandlimited 1× True-RGB`**：一旦某张片子因连拍手震轨迹过于共线导致横/纵任意一轴欠定（条件数超限），系统优雅、局域地通过 `Q3` 谱图切入 `Bandlimited 1× True-RGB` ，完整发挥全 4 帧等价 **$+1.00\ \mathrm{EV}$ ($6.02\ \mathrm{dB}$) 降噪与读噪压半红利**；
5. **100 Hz 交流灯光奇偶天然对消 pre-stabilization**：
   - 在配准光流启动之前，率先检查偶帧 `(0,2)` 对奇帧 `(1,3)` 的行间低频光度波纹残差；当侦测到接近 $\Delta\phi_{100} \approx \pi$ 反相波纹的特征性条带闪烁，先由 `(0+1)/2` 与 `(2+3)/2` 构建虚拟抗频闪平滑光流参考面执行亚像素追踪，随后用逐行 `Q9` 谐波估计精准恢复每帧真实瞬时照明系数 $g_k(r)$ 代入联合方程，从根源消灭室内白炽/LED灯下因明暗扫条导致的 Stage 3 误判鬼影丢帧！

---

## §7 单轮全量交付架构矩阵、双端验证与全量 Oracle 门禁

### 7.1 模块交付矩阵与双端验证协议

全部核心数学模块与 GUI 控件一次性合入，并于本机 **Apple M1 Max (`Metal` + Cocoa GUI)** 与远端 **X570 (`RTX 4080 SUPER` + `Vulkan` + Win32 GUI)** 双端完成严格 C++20 `-Wall -Wextra -Werror` 构建、`ASan/UBSan` 内存与数学 Oracle 验证以及真实机身样本对照（`Nikon Z8 NEF`、`DSC_0007.NEV`、`DxO ONE .DXO`）：

| 模块分层 | 核心头文件与实现入口 | 双端核心交付与实测校验点 |
|---|---|---|
| **测量与高精度时钟 (`P0 / E01–E12`)** | [heraw_suite_measurement.hpp](../heraw_suite_measurement.hpp)、[heraw_wvdc_router.hpp](../heraw_wvdc_router.hpp)、[heraw_exif_io.hpp](../heraw_exif_io.hpp) | `P0-CLK` 有理数 `pts_ticks/timescale` 溯源并入 `.NEV`/LibRaw/`obs.json`；`dng-hq` 保留 `BlackLevel=2048` 零点；`.DXO` 原生多 IFD+变长 LUT 线性化解包；`E01–E12` 与 `Q10` Oracle 全绿 |
| **频谱与概率反演 (`Q1–Q10`)** | [heraw_suite_spectral_solver.hpp](../heraw_suite_spectral_solver.hpp)、[heraw_suite_core.hpp](../heraw_suite_core.hpp) | `Q1` 双重 Schur 联消、`Q2` X-Trans 6/10 列陪集与数据占比 $s_\ell$、`Q3` 凸 B 样条 $K(x)$ 单次消费、`Q4` TicoRAW/ARW6 箱形 Probit 与 $\widetilde{L}^*$ 伴随、`Q5` 上尾似然、`Q6` 三档 `1× True-RGB` 与 $\nu_r$ 早停、`Q9` 联合谐波 |
| **EXR 科学交付与双遍流式 (`F-A / F-E`)** | [heraw_suite_exr_stream.hpp](../heraw_suite_exr_stream.hpp) | 零依赖 `zlib` Multi-Part 分块 OpenEXR 2.0（`beauty,fg,bg,scatter,depth`，严格保留负半轴噪声）；`--streaming` / `--mem-budget-gb` O(1) 条带充分统计流水 |
| **物理光学散射滤镜 (`§4`)** | [heraw_suite_diffusion.hpp](../heraw_suite_diffusion.hpp) | 线性辐射域 **HDF** / **Black Mist** / **Tiffen Ultra Contrast** / **Halation** 及 `uc+*` 级联；`hooded / standard / wide-flare` 物理遮光罩极性与无局部光晕阴影抬升 |
| **双运动场、滚快、光场与观测器 (`§5, §7, O6–O10`)** | [heraw_suite_motion_shutter.hpp](../heraw_suite_motion_shutter.hpp)、[heraw_suite_observer.hpp](../heraw_suite_observer.hpp)、[heraw_suite_refocus.hpp](../heraw_suite_refocus.hpp) | `SIM(2)` + 8 节点 $SO(3)$ 滚快、`OkLab` 舞台 LED 保色、`C1–C3` 计算快门、`F-G` Planner、`F-H` 双窗解、`§7.4` 天文 3 假设、`Voronoi` 裁剪孔径、`O6–O10` 全矩阵 |
| **双端原生 GUI 与 CLI 契约** | [heraw_sr2x.cpp](../heraw_sr2x.cpp)、[heraw_sr2x_gui_mac.mm](../heraw_sr2x_gui_mac.mm)、[heraw_sr2x_gui_win.cpp](../heraw_sr2x_gui_win.cpp)、[heraw_gui_i18n.hpp](../heraw_gui_i18n.hpp) | `macOS AppKit` 与 `Win32 GDI+` 4-Tab 全控件显式映射、`tiff-alpha`/非 TIFF 互斥切换、`zh-CN`/`en-US` 双语截图与 `--test-prefs` 合同全通 |

### 7.2 全量可证伪数学/物理 Oracle 验收总表（核心抽检门禁）

| Oracle 编号 | 输入构造 / 测试向量 | 预期量化结果（必须精确成立） |
|---|---|---|
| **Oracle-P0 (E01–E12)** | 静态 `1:4` 方差比合成帧；`B=10 mm` 横移；B 通道 `+4σ`；$(1,1,2)$ 重复焦位；$400\text{ Hz}$ 去条带前后对比；零输入 `dng-hq` 导出 | 第 2 帧权 **$0.2000$**；基线反演 **$10.0\text{ mm}$**；LOO 正确检出异常；重复焦位 `observable=false`；去条带不改变 O2 的 `0.1200` 幅度；`dng-hq` 保留负半轴采样与 `BlackLevel=2048` |
| **Oracle-Q1 / Q2** | $\sigma=\sigma_a=1, N=50 / 108$ 等方差纯静态堆；X-Trans 理想 5 帧静态 + 不同色线先验强弱 | 共模总信息严格等于 **$49/50 = 0.98$** 与 **$107/108$**（绝非 `24.5` 或 `53.5` ）；同一先验加深时数据份额 $s_\ell$ 准确递减以揭示“先验贡献非真数据” |
| **Oracle-Q3 / Q6** | 毗邻块 $K_1=\mathrm{diag}(1,0)$ 与 $K_2=\mathrm{diag}(0,1)$；$m=4$ 等权拟合 $h\in\{0, 0.5, 1\}$ | 交界中心 $K=\mathrm{diag}(0.5, 0.5)$ ，三级无连乘衰减 (`0.512`)；残差比 $\nu_r/m$ 精确匹配 $1, 3.25/4, 3/4$ |
| **Oracle-Q4 / Q5** | $\lfloor c\rfloor = 0$ 单观测区间；全 G 截顶信号从 $s+3\sigma$ 升至 $s+20\sigma$ ；短曝未饱和 G 接入 | 单区间下 `0.1` 与 `0.9` 等概率绝不被偏心拉回 `0.5`；升高过曝强度绝对严禁压暗解；短曝介入即时闭合上界 |
| **Oracle-Q7 / Q8 / Q9** | 正负符号共轭组 $(c_0,a)$ vs $(-c_0,-a)$；绿覆盖 `(0.2,0.8)` & 红全 `0.5`；`120 fps` 双窗 `1ms/8ms` 下 `50/70 Hz` 区分；`12 px` 水平两帧平场 | 无定号支持时共轭解同分，引入 $m_k\ne 1, D_f>0$ 即破对称；红差处于零空间；双窗幅度比精准测出 **`0.760 / 0.563`** ；$\lambda=12, 24, 120$ 平场权重为 `0, 1, 0.0955` |
| **Oracle-DF1..DF5 (光学滤镜)** | 常辐射场 $c_0$；中灰 `0.18` 块；暗场 `0.005` 含 $4\text{ px}$ 半径 `50` 强点源；冷色环境光下死黑阴影；多帧 TNR 后串联噪声协方差核验 | 各模式下 $c_0$ 不变；UltraCon 下中灰 **`0.000 EV`** 变动；点源 6 px 处 UltraCon 环 **`< 10^-4`** vs HDF **`> 10^-2`**；阴影真实染冷环境色温且暗部方差优于单帧曲线与前散射物理极限 |
| **Oracle-HM1..HM5 (运动/舞台)** | 高 ISO ($\hat{\rho}_{\max}=0.75$) 随动大偏移 `150 px` 方块；含 1.5° 滚转与亮度 $\gamma=1.4$ 渐变；高纯度蓝色 LED $(0.02,0.08,1.5)$ ；$0.60\text{ s}$ 腾空合成序列 | 链式+自适应 ZNCC 全程稳健锁定，无一例误判丢弃；8 节点 $SO(3)$ 消尽滚转斜切残差；OkLab 输出保持蓝光高纯色相零漂白；$h_{\rm jump}$ 精确反算 **`0.441 m ± 0.015 m`** |
| **Oracle-DXO1..DXO4 (`DxO ONE`)** | 合成 4 帧 `.DXO` 多 IFD 输入（真值位移 `12.1667 us/row`, 第 0 帧加 `0.6 px` 模糊且含 $100\text{ Hz}$ 反相条纹 + 次帧锐利亚像素分相） | 自动弃用糊化 IFD0 重定最佳锚点；有效抑制 $\Delta\phi_{100}=197.7^\circ$ 闪烁不引爆误鬼影；$Y$ 轴实测达到物理 $2\times$ MTF50 提升而弱相区域优雅滑入 `Bandlimited 1×` $+1.00\ \mathrm{EV}$ 降噪 |

---

## §8 Astra 外部数学审计闭环摘要（`R1–R12` 与跨模块联审 `1–6`）

全部 12 项边界校核与 6 项跨模块联审结论已在 [V3_EXTERNAL_MATH_AUDIT_ZH.md](V3_EXTERNAL_MATH_AUDIT_ZH.md) 中定量化确立，并在 `heraw_suite_*.hpp` 与 [test_suite_v3_audit_oracles.cpp](../tests/test_suite_v3_audit_oracles.cpp) 中以 ASan/UBSan 确定性 Oracle 验证通过：

1. **光学玻璃散射与级联线性化（`R1`, `R2`, `Cross-Section #4`）**：`Ultra Contrast` 采用大半径低频加权环境光混合并修正局部加权协方差（验证均值交叉项 $O(\eta_{\rm uc}/P)$ 退化界）；`uc+*` 级联按遮光斗物理传递顺序先施全域环境散射再施近域漫射/红层背散射；`--uc-baffle` 按 `hooded/matte-box (0.0) < standard (0.35) < wide-flare (0.70)` 单调增加广域杂散比重。
2. **双运动场、$SIM(2)$、$SO(3)$ 与 OkLab 保色（`R3`, `R4`, `R6`, `HM5`）**：局部 `M_perp` PSD 修正按本征特征值/对角安全截断维持非对角剪切分量；Schur 投影 Gauss-Newton 联合求解 `(dx, dy, dtheta_rad, scale)` 并绑定 AC 能量门控与 8 节点滑窗行时序 $SO(3)$ 滚快约束；高纯 LED 光源在 OkLab 等色相轨线上压缩亮部防止饱和漂白。
3. **DxO ONE 4 帧 RGGB、`Q1–Q6` 信息求解与 `O2`（`R5`, `R7–R10`, `DXO1–4`）**：`.DXO` 解析变长 `LinearizationTable`（含 147–252 表长并严格钳位尾端码字及量化方差）；4 相亮度 $Y$ `2×` 超分与色差 `Lambda_C` 联合消元防止强饱和彩边泄露；奇偶半混合抗 `100 Hz` 反相频闪与逐行 `Q9` 照明还原并行；`Q4`/`Q5` 联合二阶曲率与 `M_r`/`nu_r` 早停、“仅参考坐标原点不插值相减”的 `Q1` 零空间保正定理全部锁定。
4. **流式充分统计、Multi-Part EXR 2.0、长序列 Chirp 与天文三假设（`R11`, `R12`, `F-A..F-H`, `§7.4`）**：两遍条带流式与高疑义局部重放解除帧数内存耦合；EXR 严格保留负向读噪、预乘 `fg`/`bg` 安全钳位与可逆 `scatter` 场；`.NEV`/容器 `pts_ticks/timescale` 有理数溯源进入 `ClockProvenance` 与 `obs.json`；`§7.4` 对静止/飘移/轨迹点源实现三假设精确模型选择。

---

## §9 核心文献与基准索引

1. Wronski B. et al., *Handheld Multi-Frame Super-Resolution*, ACM TOG / SIGGRAPH 2019 (局部对齐、核回归合并与多相分析基准).
2. Mäkitalo M. & Foi A., *Optimal Inversion of the Generalized Anscombe Transformation for Poisson-Gaussian Noise*, IEEE TIP 2013 (低光子计数域无偏方差稳定与去偏).
3. Tiffen Optical Engineering Notes & Academy Technical Achievement Award Documentation — *Ultra Contrast Filter Ambient-Light Contrast Management* (广域非光晕环境光再分配控制原理).
4. 本仓库前期直接实证日志与底层解码文件：`scratch/heraw-model-audit-20261119/`（round-1 对照证据）、[ALGORITHM.md](../ALGORITHM.md)、[IMPLEMENTATION.md](IMPLEMENTATION.md)、[VALIDATION.md](VALIDATION.md)、[2026-08-12-dxo-superraw-tnr.md](../../../.agent/reports/2026-08-12-dxo-superraw-tnr.md)、[2026-08-12-dt-superraw.md](../../../.agent/reports/2026-08-12-dt-superraw.md)。
