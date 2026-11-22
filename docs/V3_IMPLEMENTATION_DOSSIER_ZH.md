# `nijinLBS`（`NijiN.LiteBurstSuite`）全量实现技术卷宗（供外部独立审查）

> **文档性质**：源码级实现说明与外部审计对账卷宗（Implementation Dossier for External Audit）  
> **对应代码基线**：`NIJIN_VERSION = "3.0.0"`（含 Unified V3 全栈重建引擎、`nijinWVDC` 原生解码路由、`O1–O10` 科学观测器与 27 项物理场、物理光学柔焦与重对焦、`OP-O1..O3` 手持单次对焦光学算子、`NOPB/1 + NIB/1` 原生侧挂控制总线，以及 macOS Cocoa / Windows Win32 双端三栏三工作区全参数桌面工作台）  
> **关联架构与审阅记录**：[V3 统一总规范](V3_UNIFIED_MASTER_PROPOSAL_ZH.md) · [V3 外部数学审计](V3_EXTERNAL_MATH_AUDIT_ZH.md) · [手持光学与侧挂总线架构 Rev 2](V3_OPTICAL_LIMITS_AND_SIDECAR_ARCHITECTURE_ZH.md) · [第二轮冻结审查](V3_OPTICS_SIDECAR_REVIEW_ROUND2_ZH.md) · [实施卷宗独立审计](V3_IMPLEMENTATION_DOSSIER_AUDIT_ZH.md) · [SDK 对接说明](../sdk/README_ZH.md) · [实测验收台账](VALIDATION.md)

---

## §0 审查导读、硬边界契约与代码库模块全景

本卷宗面向外部独立代码与数学审查员，按实际编译进二进制的 C++20 / Objective-C++ / Metal / Vulkan SPIR-V / Python SDK 源码逐模块交代**数据结构、离散公式、边界防护、生命周期不变量、线协议字节偏移及可复现验收命令**。所有行号均锚定当前工作树实测文件。

### 0.1 已冻结硬边界契约

1. **输入假设**：光学与侧挂扩展模块（`OP-O1..O3`、`NOPB/1`）严格面向**普通手持、单次对焦 RAW 连拍**；不依赖外部单目深度网络、不依赖 Canon `DPRAW` 双核数据、不要求脚架对焦包围（Focus Stack 仅在用户显式启用时按实测散焦门禁运行）。
2. **零下游品牌分支耦合**：C++ 核心头文件与主程序（`heraw_*.hpp`、`heraw_sr2x.cpp`）不包含任何外部模型或软件品牌分支（无 `SAM` / `RAFT` / `Flux` / `LaMa` 等枚举特判）；一切外部协同统一收敛为 `Chart` 仿射坐标帧、通用张量描述符与四类物理角色策略（`align` / `fuse` / `optics` / 可调用算子）。
3. **编码与编译门禁**：所有核心头文件（`heraw_suite_*.hpp`、`heraw_wvdc_router.hpp`、`sdk/nib_v1.h`）保持严格 7-bit ASCII（非 ASCII 字节数为 `0`）；六个构建目标均在 `-Wall -Wextra -Werror` 下零警告通过。

### 0.2 构建产物矩阵（`Makefile:58–126`）

| 构建目标 | 平台与工具链 | 关键编译/链接特性 | 主要职责 |
|---|---|---|---|
| `nijinLBS` | macOS arm64 (`clang++ -std=c++20 -O3 -mcpu=apple-m1`) | Metal 计算后端 + 静态 `libnijinWVDC.a` / `libraw_r.a` / `libSvtJpegxs.a` / `liblcms2.a` | 主 CLI 重建引擎与 `--bus-serve` 原生宿主 |
| `nijinLBS_gui` | macOS arm64 (`clang++ -std=c++20 -fobjc-arc -O2`) | 原生 AppKit / Cocoa 单执行体 | macOS 三栏 × 三工作区全参数双语工作台与自动化自检 |
| `nijinLBS.exe` | Windows x64 (`x86_64-w64-mingw32-g++ -std=gnu++20 -O3 -mavx2 -mfma`) | Vulkan 动态装载 + 32 MiB 栈 + 全静态链接 | Windows CLI 重建引擎与 `--bus-serve` 原生宿主 |
| `nijinLBS_gui.exe` | Windows x64 (`x86_64-w64-mingw32-g++ -municode -mwindows`) | Win32 Unicode + GDI+ + DPI 感知全静态链接 | Windows 三栏 × 三工作区全参数双语工作台与自动化自检 |
| `nijinLBS_linux` | Linux x64 (`x86_64-unknown-linux-gnu-g++ -std=gnu++20 -O3 -mavx2 -mfma`) | Vulkan 动态装载 + `-static-libstdc++ -Wl,-z,noexecstack` | Linux 服务器/工作站无头批处理与总线宿主 |
| `nijinLBS_asan` | macOS / Linux (`-O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer`) | ASan + UBSan 全插桩（含 `build/wvdc_asan/libnijinWVDC.a`） | 内存越界、悬垂引用与未定义行为回归门禁 |

### 0.3 源码文件与职责总表

| 模块分组 | 源码路径 | 行数 | 关键命名空间 / 入口函数 |
|---|---|---:|---|
| 主编排与总线入口 | `heraw_sr2x.cpp` | 13,197 | `execute_sr_group`、`build_live_nib_session_from_raws`、`run_cli_impl` |
| 容器与解码路由 | `heraw_wvdc_router.hpp` | 675 | `heraw_wvdc::probe_header_via_wvdc` (`L332`)、`unpack_frame_via_wvdc` (`L387`) |
| 测量契约与 `.DXO` 解析 | `heraw_suite_measurement.hpp` | 868 | `heraw_meas::ClockProvenance` (`L32`)、`capture_pre_destripe_tap` (`L166`)、`heraw_dxo` (`L275`) |
| 连拍规划与选帧 | `heraw_burst_planner.hpp` | 2,598 | `heraw_plan::scan_raw_headers_fast`、`analyze_and_plan_cluster`、`compute_next_shot_plan` (`L2503`) |
| 核心流、硅片自洁与 IBP | `heraw_suite_core.hpp` | 5,874 | `heraw_core::exposure_normalize`、`row_rolling_shutter_smooth`、`silicon_flatfield_dust_prnu_cleanup`、`terminal_optical_deconvolve` |
| CPU Stage 4/5 与流式核 | `heraw_cpu_stage45.hpp` | 4,538 | `heraw_cpu::run_stage45_cpu_roi_ws` (`L849`)、`run_stage45_unified_v3` |
| GPU 调度与双后端 | `heraw_gpu_backend.hpp` / `heraw_gpu_metal.mm` / `heraw_gpu_vulkan.hpp` | 2,061 / 6,555 / 3,415 | `heraw_gpu::run_stage45_streamed`、`run_m5a_gls_gpu` (`L2093`)、`run_m5b_deconv_gpu` (`L2675`)；共 49 个 GPU kernel |
| 谱分析与 Probit 求解 | `heraw_suite_spectral_solver.hpp` | 876 | `heraw_spec::ArrowHeadSchurSolver` (`L160`)、`analyze_luma_vs_chroma_rank` (`L351`)、`interval_box_probit_score` (`L685`) |
| 卷帘样条与计算快门 | `heraw_suite_motion_shutter.hpp` | 873 | `heraw_motion::evaluate_sim2_photometric_patch` (`L40`)、`fit_perspective_rs_spline` (`L324`)、`solve_nnls_temporal_envelope` (`L733`) |
| 潜在物理场与时间合成 | `heraw_suite_features.hpp` | 3,625 | `heraw_suite::compute_fields` (`L2046`)、`compose_temporal`、分部积分高斯导 `wblur` 标定 |
| 科学观测器 `O1–O10` | `heraw_suite_observer.hpp` | 3,189 | `heraw_suite::observer::run_p3_radiometry` (`L365`)、`run_optical_lockin` (`L1176`)、`run_kinematics_and_self_stereo` (`L1665`) |
| 镜头光学护照与 `.lcp` | `heraw_suite_optics.hpp` | 1,702 | `heraw_optics::analyze`（8× 星点 ePSF、五分区斜边 MTF50、盲 2D LCA、暗角/畸变） |
| 线性光学柔焦滤镜 | `heraw_suite_diffusion.hpp` | 490 | `heraw_diff::apply_optical_diffusion_linear` (`L253`)、`eval_ultracon_output_variance` (`L114`) |
| 重对焦与 `OP-O2` 三圆盘 STF | `heraw_suite_refocus.hpp` | 1,715 | `heraw_refocus::render_refocus` (`L246`)、`render_synthetic_aperture` (`L1484`)、`solve_contour_unmix_6endpoint` (`L1148`) |
| 手持光学 `OP-O1` / `OP-O3` | `heraw_suite_handheld_optics.hpp` | 969 | `heraw_hoptics::apply_bokeh_shaping_linear` (`L526`)、`apply_spectral_ca_linear` (`L729`) |
| 原生 `NOPB/1 + NIB/1` 总线 | `heraw_suite_nib_bus.hpp` + `sdk/nib_v1.h` | 1,593 + 63 | `heraw_bus::read_nib_file`、`write_nib_file`、`sample_scalar_2d` (`L764`)、`ops::run_operator`、`run_bus_serve_loop` |
| Python 参考 SDK | `sdk/nib_wire.py` + `sdk/nijin_bus.py` | 348 + 1,420 | `Chart`、`NijinSession`、`NijinClient`、`LiveSessionHandle` |
| Multi-Part EXR 与 EXIF I/O | `heraw_suite_exr_stream.hpp` / `heraw_exif_io.hpp` | 779 / 2,688 | `heraw_exr::write_multipart_exr` (`L253`)、`heraw_io` (`SOF3` DNG、`tiff-hq`、无痕 EXIF) |
| 双平台 GUI 与本地化 | `heraw_gui_i18n.hpp` / `heraw_sr2x_gui_mac.mm` / `heraw_sr2x_gui_win.cpp` | 2,124 / 10,334 / 11,685 | 四路同步 `672` 词条（内建中/英 + `lang/zh-CN.lng` + `lang/en-US.lng`）与双端全参数工作台 |

---

## §1 阶段 0–1：多格式解码路由、测量底座 (`P0`) 与四级科学时钟

### 1.1 双通道解码路由与有效感光区裁剪原点一致性

1. **UTF-8 全链路文件访问**（`heraw_fs_utf8.hpp`、`heraw_wvdc_router.hpp:38`）：所有路径在内部统一以 UTF-8 `std::string` 流转，落地 I/O 前经 `heraw_fs::from_utf8` 转为 `std::filesystem::path`（Windows 下走宽字符 `_wfopen` / `CreateFileW`，macOS/Linux 走原生字节路径），中文/日文目录与虚拟帧选择器 `<clip.nev>::frame=<idx>`（`heraw_wvdc_router.hpp:61`）互不干扰。
2. **`nijinWVDC` 原生解码与裁剪原点保留**（`heraw_wvdc_router.hpp:387–598`、`heraw_sr2x.cpp:682–755`）：
   - 对 Nikon `.NEV`（TicoRAW 序列）、Nikon Z8/Z9 高效压缩 `HE` / `HE*` `.NEF`、Sony `ARW6` `.ARW` 及裸 JPEG-XS 码流优先路由至静态编译的 `nijinWVDC`。
   - **关键不变量**：`nijinWVDC` 解码器已在内部裁去光学黑边并输出有效区像素（例如 Nikon Z8 `8280×5520` 裁为 `8256×5504`），通过 `dec_info.offset_x = 12, dec_info.offset_y = 8` 报告真实传感器起点（`heraw_wvdc_router.hpp:459`）。后续调用 `enrich_wvdc_frame_from_libraw_data` 补充白平衡/色彩矩阵时，因 LibRaw 未解压缩像素数据，`S.left_margin` 与 `S.top_margin` 为 `0`；`heraw_sr2x.cpp:742` 仅在 `out.crop_left == 0 && out.crop_top == 0` 时回退至 LibRaw 边距，确保 `NOPB/1` `geometry_id` 精确记录 `sensor:W8256xH5504:crop12,8:bayer:0112`。
3. **DxO ONE SuperRAW（`.DXO` / `.4RW`）四帧契约修复**（`heraw_suite_measurement.hpp:275–620`）：
   - 独立解析 TIFF IFD 树，跳过 `IFD0`（`160×120` 缩略图）与预览 `SubIFD`，定位全部 4 个 `5632×3710` 单通道 CFA `SubIFD`；
   - 共享主 IFD 的 256 项 `LinearizationTable`、黑电平 `200`、白点 `4094` 与 `ActiveArea = [16, 88, 3710, 5632]`（有效输出 `5544×3694`），杜绝 LibRaw 对附帧将 `color.maximum` 误报为 `255` 导致的假饱和截顶；
   - 解析 MakerNote `id=15` 帧级时间戳窗口及陀螺采样，记录 `clock_source = "makernote_dxo_ts"`。

### 1.2 原生样本测量契约与 `Tap-0` 旁路（`heraw_suite_measurement.hpp:126–220`）

- **辐射方差与融合权重组装**：每帧曝光归一线性样本 $\tilde y_{kp} = (y_{kp}^{\mathrm{raw}} - b_{kp}) / e_k$ 的单次统计精度由 `Noise::frame_variance` 按 $w_{kp}^{\mathrm{rad}} = e_k^2 / (\sigma_{\mathrm{read},k}^2 + g_k \hat\mu_{kp} + \sigma_{\mathrm{quant},kp}^2)$ 计算；主流程配准可见性 `mw_align = m`、融合半栈权重 `mw_half = w_frame * m`（及高光饱和渐变）在融合装配阶段统一乘入，避免同一因子被重复平方（关闭审计 `E01/E05` 双重计权）。
- **去条带前线性绿色行统计（`Tap-0`）**：`capture_pre_destripe_tap`（`L166`）在线性化与温点替换之后、任何行去闪与 C13 平场条带自清洁运行之前截取线性绿通道行均值序列，优先供 `O2`（频闪光源锁相）消费，防止成片去条带滤波器抹除真实工频/PWM 谐波。

### 1.3 四级有理数科学时钟（`heraw_suite_measurement.hpp:32–124`、`heraw_wvdc_router.hpp:560–575`）

`ClockProvenance` 枚举（`pts_ticks / hw_exposure / makernote_dxo_ts / exif_subsec_ms / model_estimate`）显式保留有理数分子/分母 `pts_ticks / pts_timescale` 与基准秒 `base_sec`：
1. `pts_ticks` / `hw_exposure`（容器或硬件时钟，如 `.NEV` `3003 / 30000 s`，消除 `±500 us` 整毫秒截断抖动）；
2. `makernote_dxo_ts`（DxO MakerNote 帧定时）；
3. `exif_subsec_ms`（相机 EXIF 亚秒时钟）；
4. `model_estimate` 与帧序回退（缺失真实时间跨度时观测器回退为帧序号，自动禁止报告 `Hz` / `m/s` 绝对物理量纲）。  
`evaluate_freq_precision_budget`（`L96`）提供指定目标频率置信区间的精度校验证书。

---

## §2 阶段 2–3：亚像素配准、卷帘样条、计算快门与硅片自校准

### 2.1 金字塔 ZNCC、`SIM(2)` 光度正交门禁与透视卷帘样条

1. **多尺度配准与 `SIM(2)` 刚体补丁恢复**（`heraw_sr2x.cpp`、`heraw_suite_motion_shutter.hpp:40–218`）：
   - 半分辨率绿通道金字塔由粗到精搜索；局部刚体/运动主体补丁调用 `evaluate_sim2_photometric_patch`（`L40`），联合估计增益 $\hat\gamma$ 与偏置 $\hat\beta$，并在光度干扰空间 $D = W^{1/2}[p, \mathbf{1}]$ 的正交补 $(I - P_D) W^{1/2} J_g$ 上检验几何雅可比最小奇异值，防止纯斜坡亮度变化伪装成尺度缩放（关闭审计 `R3`）。
2. **8 节点透视 `SO(3)` 卷帘行样条**（`heraw_suite_motion_shutter.hpp:240–605`）：
   - 采用含齐次透视除法的精确针孔重投影 $\pi(K R(t) K^{-1}\tilde p) - \pi(\tilde p)$（`rodrigues_so3` `L248`、`eval_perspective_rs_flow` `L291`）；
   - 按行时刻配点矩阵与非平行光线几何自由度自适应降阶（8 节点 → 4 节点 → 常旋转 → 平移），并用背景锚点封顶动体区域权重（关闭审计 `R4`）。

### 2.2 帧内硅片温点与 `Q10` 平场灰尘/PRNU 可辨识度门禁

- 在调用 `silicon_flatfield_dust_prnu_cleanup`（C13）前，先通过 `heraw_meas::eval_flatfield_spectral_transfer`（`heraw_suite_measurement.hpp:233–248`）在四组探针空间频率上评估连拍归一化特征函数 $\chi(\omega) = \sum w_k e^{-i\omega\cdot\Delta_k}$ 的全局能量传递门禁 $1 - |\chi(\omega)|^2$；当全部探针方向的抖动幅度均不足以分离传感器固定图案与静止景物时直接跳过并输出诊断日志。

### 2.3 计算快门包络（`heraw_suite_motion_shutter.hpp:650–873`）

- `--shutter-mode anchor`：锁定锚点帧瞬时姿态；
- `--shutter-mode integrate` + `--flow-kappa <0..1>`：$\kappa$-拉格朗日轨迹积分（$\kappa=0$ 追随主体消背景运动，$\kappa=1$ 固定画幅生成丝滑慢门）；
- `--shutter-mode nnls-custom`：`solve_nnls_temporal_envelope`（`L733`）在 32 等分时间箱上对后帘目标包络求解带 `0.05` 保底下限的非负帧权，避免负权振铃伪影；
- `--shutter-mode deblur` + `--intra-deblur`：利用多帧微位移与模糊门禁恢复路径抑制单帧运动模糊。

---

## §3 阶段 4–5：CPU / Metal / Vulkan 统一超分核、两遍零临时盘流式引擎与 `Q4/Q5/Q6` 求解器

### 3.1 Unified V3 融合、色差金字塔与跨后端严格等价

1. **双尺度重建（`1×` 三档与 `2×` 超分）**：
   - `2×` 路径（默认需 $\ge 5$ 帧有效曝光，DxO SuperRAW 为 4 帧）：在 `heraw_cpu_stage45.hpp`、Metal（`heraw_gpu_metal.mm`）与 Vulkan（`heraw_gpu_vulkan.hpp` + 预编译 SPIR-V `heraw_gpu_spirv.hpp`）三套后端统一执行各向异性梅花格绿通道核回归、跨 CFA 锚点色差金字塔融合、16 带 Anscombe GAT-DCT 维纳滤波（相位充足处走奇偶半栈互谱方差，欠定处平滑退回解析 $\eta$）、6 层 À-Trous 边缘保持色度滤波，以及 `M5-A` 统一广义最小二乘（GLS）回投与 `M5-B` 分区椭圆 PSF 终端反卷积。
   - `1×` True-Sampled RGB 三档（`--rgb-1x-tier fast|sinc|joint`）：`sinc` 使用带限窗化 $\operatorname{sinc}$ 连续基（`heraw_suite_spectral_solver.hpp:854`）；`joint`（`heraw_sr2x.cpp:8775`）构造局部箭头型正规方程（`ArrowHeadSchurSolver` `L160`），对高频混叠子空间做显式 Schur 消元后回代原生 `1×` 网格。
   - **四帧 Bayer 色差干扰后亮度秩检验**（`analyze_luma_vs_chroma_rank` `heraw_suite_spectral_solver.hpp:351`）：在扣除高频色差干扰列投影 $P_D$ 后度量有效亮度信息 $F_{Y,\mathrm{data}} = B_Y^*(I - P_D)B_Y$（关闭审计 `R5`）。
2. **条带流式跨后端坐标对齐**（`heraw_gpu_backend.hpp`、`heraw_cpu_stage45.hpp`）：
   - `Stage45GpuJob` 显式分离全图行坐标 `origin_y` 与条带局部 Fisher 缓冲起点 `fcomp_origin_y`；
   - 16 带 VST 噪声分层纵坐标 `((y + origin_out_y) * 16) / full_H_out` 与 `64×64` COLA FFT 瓦片全局相位 `phase_y = origin_out_y & 31, phase_x = origin_out_x & 31` 在全图模式与任意条带切分下保持一致。实测 CPU / Metal 子条带最大绝对误差 `1.07e-4 .. 2.52e-4`。

### 3.2 零临时盘两遍内存条带流式调度（`--streaming auto|on|off`、`--mem-budget-gb`）

- `--mem-budget-gb` 定义 Stage 4/5 重建工作集的**条带调度目标**（非操作系统整进程 RSS 硬上限，整进程峰值同时受解码帧栈与全幅输出画布支配）：
  - **Pass 1**：从内存帧栈提取全局光度/噪声统计，并将每帧按 64 行切块压缩进内存 `zstore` 缓冲；
  - **Pass 2**：逐条带（含对齐流位移与滤波器支撑 halo）从内存 `zstore` 解压 ROI 完成 Stage 4/5、`M5-A`、`Q4/Q5` 与 `joint-1x` 重建，全程**不向磁盘写任何临时像素缓存文件**。实测 `DXO_0820.DXO` `2×`（`11088×7388`）在 `1 GiB` 条带目标下切分 31 条带（内部工作集估算 `1575.99 MiB`，macOS 整进程最大 RSS `2864.36 MiB`），与同场景全图 CPU 调度相比 `PSNR = 59.39 dB`，`/tmp` 零残留。

### 3.3 `Q4` 编解码量化一致性修正与 `Q5` 上尾截断似然（`heraw_suite_spectral_solver.hpp:614–810`）

- **稳定对数正态尾概率**（`stable_log_phi_and_mills` `L625`）：大负自变量采用渐近连分数展开计算 $\log\Phi(z)$ 与 Mills 比，避免下溢除零。
- **`Q4` 区间/代理一致性修正**（`--codec-constraint box|probit`）：对 `.DXO` 256 项非均匀 LUT 恢复精确 Voronoi 码字区间 $[lo, hi]$；对 Nikon HE 构造配准行观测上的一维双正交 5/3 小波提升量化感知代理区间（`Biorthogonal53Lifting` `L736` 严格伴随 $L^*$）。`box` 分支执行加权半步钳位，`probit` 分支计算 $-\log[\Phi((hi-\mu)/\sigma) - \Phi((lo-\mu)/\sigma)]$ 的精确梯度与曲率，并由 `is_footprint_safe_for_q4`（`L791`）隔离饱和邻域，防止与 `Q5` 重复计权（关闭审计 `R8`）。
- **`Q5` 高光截顶 Probit**（`--sat-model probit`）：在部分帧未饱和、部分帧触顶的混合高光像素上施加 $-\log\Phi((\mu - s_{\mathrm{sat}})/\sigma)$ 单侧约束；当所有帧同点全饱和时强制该步零向上外推（`core_max_delta == 0`），并由规划器依据 `clipped_fraction` 在 `--next-shot-plan` 给出负向曝光补偿建议。

---

## §4 27 项潜在物理场、双级光学计量与 `O1–O10` 科学观测器矩阵

### 4.1 潜在场门禁与分部积分高斯导 `wblur` 标定（`heraw_suite_features.hpp`）

- 支持按需提取全部 **27 项物理场**（核心 11 场：`motion, phase, focus, depth, defocus, specular, sigma, coherence, velocity, flicker, turbulence`；扩展几何与观测 16 场：`depth-conf, dff-depth, normal-x, normal-y, occlusion, o1-roughness, o1-veil, o1-metalness, o2-ac-frac, o2-phosphor, o3-ttc, o3-vorticity, o3-strain, o4-opd, o5-vib-amp, o5-vital`），每项均附带 `{observable, units, measured, threshold, confidence}` 元数据：
  - **视差逆深度 `depth`**：受 `--depth-gate <auto|relaxed|force>` 与 `--depth-min-disp / --depth-min-coh / --depth-min-support` 门控；在 `--metric-policy verified` 下仅当具备有效焦距/对焦几何标尺时输出物理长度单位，否则标记为归一化相对深度 `[0, 1]`。
  - **边缘散焦宽度 `wblur`**：沿边缘法向以 `ds = 0.5 px` 采样一阶差分增量并与解析高斯导核卷积（分部积分法），结合正交平方和噪声扣偏恢复模糊 $\sigma_0$（在审计 `E02` 含 `10.0 mm` 真值的三焦距合成几何预言机 `tests/test_suite_v3_audit_oracles.cpp:112` 上恢复 `10.0032 / 10.0032 / 10.0447 mm`）。

### 4.2 科学观测器 `O1–O10` 与解耦执行契约（`heraw_suite_observer.hpp`）

通过 `--observers <off|all|csv>` 独立选择计算集合，与报告输出路径 `--observer-report <path>` 完全解耦（JSON 显式写出 `requested_observers` 与 `executed_observers`；单独指定 `--observers`、`--eval-self` 或 `--bus-export` 均可直接运行，无需强绑 `--emit-rgb`）：
- **`O1` 漫反射/镜面辐射分解（`run_p3_radiometry` `L365`）**：由跨帧 RGB 光度模型分离漫反射反照率、微面粗糙度与介质幕光（支持 `--emit-diffuse` 导出本征图）；
- **`O2` 光学锁相频闪与余辉（`run_optical_lockin` `L1176`）**：消费 `Tap-0` 线性绿行统计，实现全域快门双窗 $\operatorname{sinc}$ 比值频率估计（`estimate_gs_flicker_sinc_ratio` `L1109`）、复数线性调频广义似然比检验 `evaluate_chirp_glrt_complex`（`L1043`，由 `O2` `L1542` 调用）与荧光余辉 $\tau_R$ 拟合（支持 `--emit-light-split` 导出交直流分离层）；
- **`O3` 刚体运动几何与流场涡度（`run_kinematics_and_self_stereo` `L1665`）** & **`O4` 热浪纹影（BOS 势流/旋流分解与光程差代理）**；
- **`O5` 滚快微震测振与微动灌注**：基于边缘位移、滚快行读出时间（`--readout-ms`）与阻尼模态门禁提取振动幅值与长序列微动信号；
- **`O6–O10` 扩展光学与天体观测**：`O6 loca_sign`（大光圈焦平面红蓝边缘对比度斜率定号）、`O7 modal_wave`（高置信光流图像运动速度代理 `wave_phase_vel_px_s`，单位 `px/s`）、`O8 seeing`（配对流差分倾斜 `diff_tilt_rms_1x_px` 与相对指标 `seeing_r0_norm`，支持 `--obs-pitch-deg` 俯仰角修正）、`O9 refraction`（相对流位移 RMS `refraction_rms_1x_px` 与对齐后中位底色）、`O10 streak_astrometry`（位移测速、差分光度及三假设判别 `classify_astro_point_3hyp` `L1154`，支持 `--plate-scale <arcsec/px>` 板尺度换算）。

---

## §5 阶段 4 线性光学渲染：柔焦滤镜、重对焦 (`OP-O2`) 与手持单次对焦光学 (`OP-O1` / `OP-O3`)

所有光学风格化与重对焦算子均工作在**相机线性辐射域**（Stage 5 重建与 `NOPB` 先验合成之后、高光色调映射与 OETF 之前），而 DNG 原始数字底片始终保持未加滤镜的纯净重建数据。

### 5.1 物理玻璃散射滤镜与舞台保色高光压缩

1. **光学散射引擎**（`heraw_suite_diffusion.hpp:253–488`）：
   - 支持 `hdf`、`black-mist`、`ultra-contrast`（基于 `16×P95` 抗离群渲染保护的全局环境幕光抬升，并由 `eval_ultracon_output_variance` `L114` 传播环境场交叉协方差，对齐审计 `R1`）、`halation`（胶片红层背散射），以及一次通过串联复合 `T_N T_U`（`uc+hdf` / `uc+mist` / `uc+halation`，对齐审计 `R2`）与 `warm-glass` 色温传递；散射增量单独保留以供 Multi-Part EXR `scatter` 层导出。
2. **OkLab 舞台灯光保色压缩**（`heraw_suite_motion_shutter.hpp:650`）：在 OkLab 感知均匀空间沿等色相射线压缩超白亮度，消除传统单通道截顶导致的舞台窄带 LED（青/品红/深蓝）发白相移。

### 5.2 `OP-O2`：三圆盘零振铃正性 STF 与精确圆条带积分（`heraw_suite_refocus.hpp`）

1. **三圆盘正性凸组合**（`heraw_suite_refocus.hpp:281–285, 513–605, 742–780`）：
   - 当 `stf_apodization` $u \in (0, 1]$、圆孔叶片数 `< 5` 且未开启球差环时，散景核切换为三个同心均匀正圆盘的凸组合，权重为 $w(u) = (0.220 u,\,0.406 u,\,1 - 0.626 u)$，$\alpha = (0.28,\,0.58,\,1.00)$。在满档 $u = 1$ 时：
     $$K_{\mathrm{STF}}(r; R) = \sum_{m=1}^{3} \frac{w_m}{\pi (\alpha_m R)^2}\,\mathbf{1}_{\{r \le \alpha_m R\}}, \qquad w(1) = (0.220,\,0.406,\,0.374)$$
     其解析傅里叶响应 $H(\omega) = \sum_{m=1}^3 w_m \frac{2 J_1(\alpha_m R|\omega|)}{\alpha_m R|\omega|}$ 在 $x = R|\omega| \le 16\ \mathrm{rad}$ 内消除负向相位反转（$0 < u < 1$ 为标准圆盘与满档三圆盘 STF 的连续插值）。
2. **像素方格精确圆面积原函数**（`circle_strip_antideriv` `L513`）：
   - 摒弃 `rp = R + 0.5` 半像元外扩启发式；对每个整数行条带 $[y_0, y_1] = [j - 0.5, j + 0.5] \cap [-r_m, r_m]$，使用精确原函数 $F(u) = \frac{1}{2}(u\sqrt{r_m^2 - u^2} + r_m^2 \arcsin(u/r_m))$ 求出行条带真值面积 $A_{\mathrm{row}} = 2[F(y_1) - F(y_0)]$，以等效半弦宽 $\bar x = A_{\mathrm{row}} / (2\Delta y)$ 与像素列区间 $[i - 0.5, i + 0.5]$ 求精确交长并按 $\pi r_m^2$ 归一。
   - 另含 2D 裁剪瞳 `Voronoi` / `Dwell` 合成孔径积分（`render_synthetic_aperture` `L1484`）与 6 端点边缘前景/背景解混（`solve_contour_unmix_6endpoint` `L1148`）。

### 5.3 `OP-O1`：一阶连续光谱色差合成（`heraw_suite_handheld_optics.hpp:92–356, 685–922`）

1. **Sellmeier 材料色散与等位移自适应求积**（`build_spectral_table` `L168`）：
   - 在 $\lambda \in [410, 680]\ \mathrm{nm}$ 采用双玻璃超色差透镜 Sellmeier 折射率差 $\Delta n(\lambda)$（以 $\lambda_0 = 555\ \mathrm{nm}$ 为零位移锚点，$g=435.84\ \mathrm{nm}$ 与 $C=656.27\ \mathrm{nm}$ 跨度归一：$\psi_g = +0.726764, \psi_C = -0.273236$，蓝红比 `2.65983`）。
   - **按位移坐标均分节点**（修复等波长分段在短波端跳步 `>1.5 px` 导致的彩虹断裂）：在 $[410, 555]$ 与 $[555, 680]$ 两单调支上分别取 $N_{\mathrm{blue}} = \lceil |\psi(410)|\cdot P_{\max} / 0.60 \rceil$ 与 $N_{\mathrm{red}} = \lceil |\psi(680)|\cdot P_{\max} / 0.60 \rceil$ 段，用 42 步二分法 `invert_hyperchromat_psi`（`L116`）精确反解波长节点 $\lambda_q$，并乘梯形波长测度权重 $\Delta\lambda_q$ 后按通道列和归一 $\sum_q t_{q,c} = 1$。在 $P_{\max} = 16\ \mathrm{px}$ 时生成 `Q = 38` 节点，最大相邻位移步长实测 `0.579 px < 0.65 px`。
2. **Lovász 连续光谱提升与逐像素非负能量帽**（`L243–356, 766–807`）：
   - 白色基 $b_0(\lambda_q) \equiv 1$ 严格恒等；其余六基（`R, G, B, C, M, Y`）由带二阶差分曲率惩罚 $\mu \|D_2 s\|_2^2$ 的加速投影梯度 NNLS（220 步 FISTA，`L266`）预计算非负光滑谱包络。
   - 对白平衡归一后的像素 $(r_+, g_+, b_+)$ 排序得到 $c_{\min} \le c_{\mathrm{mid}} \le c_{\max}$，分解为白光 + 二次色 + 主色三项非负凸组合 $s_q^{\mathrm{raw}} = c_{\min} b_{0,q} + (c_{\mathrm{mid}} - c_{\min}) b_{\mathrm{sec},q} + (c_{\max} - c_{\mathrm{mid}}) b_{\mathrm{pri},q}$。
   - **非负下冲硬防护（关闭第二轮审查反例一）**：计算重建三刺激 $\mathrm{rec}_c = \sum_q t_{q,c} s_q^{\mathrm{raw}}$，按源码 `heraw_suite_handheld_optics.hpp:837` 令乘性缩放因子为
     $$\alpha(x) = \mathrm{gate}(x)\cdot\min\!\left(1,\; \min_{c:\,\mathrm{rec}_c > c_+(x)} \frac{c_+(x)}{\mathrm{rec}_c}\right)$$
     由于差量形式为 $c + \sum_q t_q (K_q - I)(\alpha s_q^{\mathrm{raw}})$ 且正算子 $K_q \ge 0$，被移走的自项满足 $\sum_q t_{q,c} \alpha(x) s_q^{\mathrm{raw}}(x) = \alpha(x)\mathrm{rec}_c \le c_+(x)$，在任意极端饱和单像素脉冲下严格保证非负输入的输出通道无下冲（实测纯蓝脉冲红通道最小值 `0.00e+00`）。
3. **双空间算子与暗部/选区门控**（`L715–905`）：
   - `lateral`：保表面亮度的径向或指定棱镜角拉回插值 $s_q(x - \Delta x_q(x))$；由 `3 px` 低通先导亮度门控 `dark_sigma` 暗部散粒噪声；
   - `axial-aura`：仅对高于 `aura_threshold`（且通过可选 `emitter_mask`）的合焦发光体抽取光谱能量，施加半径 $R_q = P_{\max}|\psi_q|$ 的三尺度正核光晕扩散；`protect_mask` 处线性衰减差量至零。

### 5.4 `OP-O3`：手持单次对焦正核散景整形与 `2 px` 混叠边界支撑腐蚀（`heraw_suite_handheld_optics.hpp:408–682`）

1. **单次对焦局部散焦半径盲估计**（`estimate_defocus_radius_map` `L408`）：在半分辨率亮度图上比较细尺度梯度 $\|\nabla L_{\mathrm{fine}}\|$ 与附加参考高斯模糊 $\sigma_{\mathrm{ref}} = 3.2\ \mathrm{px}$ 后的梯度比 $\rho = \|\nabla L_{\mathrm{coarse}}\| / \|\nabla L_{\mathrm{fine}}\|$，反解局部边缘模糊宽度 $\hat R(x)$ 并经双遍箱式引导扩散至散景内部；结合合焦图 `field_focus` 与外部 `protect_mask` 构造前景保护权重 $f_{\mathrm{fg}}(x) \in [0, 1]$。
2. **`2 px` 背景卷积支撑腐蚀（关闭第二轮审查 §3 `OP-O3` 软边泄色反例）**（`L500–520`）：
   - 在 `50%` 抗锯齿前景/背景交界像元（$f_{\mathrm{fg}} = 0.5$）上，像素本身已混入一半亮前景光子；若直接将该像元纳入背景归一化卷积 $G*(m_{\mathrm{bg}} I)/(G*m_{\mathrm{bg}})$，大核会把前景能量拖入邻近纯黑背景形成亮边光晕。
   - 实现对任意 $\max_{\|y-x\|_\infty \le 2} f_{\mathrm{fg}}(y) > 0.05$ 的邻域强制置零纯背景卷积权重 `pure_bg_weight[x] = 0`，而前景像素（$f_{\mathrm{fg}} \ge 0.5$）直接保持逐位原值（实测 `50%` 混叠白边配纯黑背景 `fg_delta = 0.0`、`bg_halo = 0.0`）。
3. **四维正性核整形**（`apply_bokeh_shaping_linear` `L526`）：在纯背景支撑域上联合执行：① `de_nisen`（抑制硬边双线振铃的正性径向平滑）、② `swirl`（`[-1, +1]` 带正负手性的有向对数螺旋积分，正值顺时针 `CW` 与负值逆时针 `CCW` 产生相反切向彗尾旋转方向）、③ `aspect`（竖轴椭圆拉伸 `[1, 2]`，模拟变形宽银幕散景）、④ `onion_clean`（绕光斑局部梯度的切向小角度均值抛光，抹平非球面洋葱圈同心环纹同时保持圆盘外缘陡峭度）。

---

## §6 `NOPB/1` 宿主中心侧挂控制总线与 `NIB/1` 二进制线协议

### 6.1 `NIB/1` 二进制容器字节布局（`sdk/nib_v1.h`、`sdk/nib_wire.py`、`heraw_suite_nib_bus.hpp:850–1066`）

所有多字节整数与 IEEE-754 浮点数均为小端序（Little-Endian），JSON 清单与每个张量载荷起始偏移均按 **64 字节对齐**（零填充字节强制为 `0x00`）：

| 结构段 | 字节范围 | 字段名 | 类型 | 语义与硬校验规则 |
|---|---|---|---|---|
| **文件头** (`64 B`) | `0..7` | `magic` | `char[8]` | 固定 `"NIJBUS01"` |
|  | `8..11` | `version` | `u32` | 固定 `1` |
|  | `12..15` | `stage` | `u32` | `0=EXT_INPUT .. 5=S5_DELIVER` |
|  | `16..19` | `frames` | `u32` | 连拍帧数 `len(session.frames)` |
|  | `20..23` / `24..27` | `width` / `height` | `u32` / `u32` | `active_1x` 有效宽高（`EXT_INPUT` 未知时可为 `0`） |
|  | `28..31` | `count` | `u32` | 张量描述符条目数 |
|  | `32..39` | `quick_id` | `u64` | 规范化 `session` JSON 的 64-bit FNV-1a 快速指纹 |
|  | `40..47` / `48..55` | `meta_offset` / `meta_bytes` | `u64` / `u64` | UTF-8 规范化 JSON 清单偏移与字节长度 |
|  | `56..63` | `directory` | `u64` | 描述符表起始偏移（固定 `64`） |
| **张量描述符** (`128 B × count`) | `0..39` | `name` | `char[40]` | 7-bit ASCII 零终结短键；超 39 字节公有名称自动映射为 `id.<sha256_36>`（`heraw_suite_nib_bus.hpp:442`）并在 JSON `public_name` 保全原名 |
|  | `40..41` | `dtype` | `u16` | `1=U8, 2=U16, 3=I32, 4=F16, 5=F32, 6=F64, 8=I8, 9=I16, 10=U32, 11=I64, 12=U64, 13=BOOL` |
|  | `42..43` | `kind` | `u16` | `0=SCALAR(4D), 1=VECTOR(2ch), 2=COV2(3ch SPD), 3=ND(任意维)` |
|  | `44..45` | `origin` | `u16` | `0=nijin_measured`（**仅限宿主**）, `1=co_calibrated`, `2=testable_geom`, `3=partition_mask`, `4=generative_prior` |
|  | `46..47` | `codec` | `u16` | 固定 `0`（未压缩紧凑 C 序，可直接 `mmap`） |
|  | `48..51` / `52..55` | `frame` / `layer` | `i32` / `i32` | 帧索引（`-1` 为全序列/锚点，`0..N-1` 为单帧）与图层号 |
|  | `56..71` | `shape[4]` | `u32[4]` | 4D 栅格为 `[B, H, W, C]`；**当 `kind == NIB1_ND (3)` 时线描述符 `shape` 强制全零 `{0,0,0,0}`**，真实维度唯一存于 JSON `meta["dims"]` |
|  | `72..95` | `affine[6]` | `f32[6]` | 外部格 `(u, v)` 到宿主 `active_1x` 的仿射矩阵 `(a00, a01, b0, a10, a11, b1)` |
|  | `96..111` | `bbox[4]` | `u32[4]` | 有效框整数外包络 `(x0, y0, x1, y1)`，浮点精确边界存于 `meta["valid_bbox_f"]` |
|  | `112..119` / `120..127` | `offset` / `nbytes` | `u64` / `u64` | 64B 对齐载荷偏移与精确字节数（支持 `0` 字节空张量） |

### 6.2 `Chart` 坐标代数、防黑边分数中心钳位与统一重采样器

1. **链式防黑边变换**（`sdk/nijin_bus.py:194–285`、`heraw_suite_nib_bus.hpp:482–558`）：
   - 宿主发布 `active_1x`、`proxy_linear`（`--bus-serve`）及 `output_rgb` / `active_2x` / `half_1x`（`--bus-export`）；外部调用方无需手推逆矩阵，直接用 `chart.rotate90(k)`、`crop(x0,y0,x1,y1)`、`scale(sx,sy)`、`resize(w,h)`、`pad(...)`、`letterbox(...)` 派生新 `Chart`，自动维护有效框 `bbox = (u0, v0, u1, v1)` 排除 letterbox 黑边。
   - 双端点光流经线性雅可比 + 帧间基点差变换，2×2 对称协方差 `COV2 (sxx, sxy, syy)` 经 $J \Sigma J^\top$ 推前并校验半正定性。
2. **分数有效框半像元精确钳位**（`heraw_suite_nib_bus.hpp:503–519`、`sdk/nib_wire.py:308–323`）：
   - 像素中心定义为 `(i + 0.5, j + 0.5)`。当源有效框为分数区间 $[u_0, u_1) \times [v_0, v_1)$ 时，先求有效离散像素中心范围：
     $$i_{\min} = \lceil u_0 - 0.5 \rceil,\qquad i_{\max} = \lceil u_1 - 0.5 \rceil - 1$$
   - 若 $i_{\min} > i_{\max}$ 或逆映射落出 $[u_0, u_1)$ 则返回无效（不采样框外黑边）；否则将采样坐标 $(u, v)$ 严格钳位在 $[i_{\min} + 0.5,\, i_{\max} + 0.5]$ 内，杜绝双线性插值向框外无效像素中心泄漏权重（第二轮审查 §2.1 `[0, 2.4)` 判别式精确返回 `1.0`）。
3. **单一权威重采样器 `sample_scalar_2d`**（`heraw_suite_nib_bus.hpp:764–813`、`sdk/nijin_bus.py:1048–1070`）：
   - 按声明 `dtype` 与 `encoding` 统一解包 `U8 / I8 / U16 / I16 / I32 / U32 / I64 / U64 / BOOL / F16 / F32 / F64` 及 Fortran/C 序 `rle_runs` / `coco_rle`（同时兼容 `pycocotools` 返回的 `bytes` 与 `str` LEB128 游程串）；
   - 整数类型、`RLE` 与 `mask.binary*` 走最近邻中心采样；`mask.logit*` 双线性采样后按 `> 0` 阈值化；连续浮点权重走钳位双线性插值。同一物理掩膜无论编码为 `U8`、`F32` 还是旋转 `180°` `RLE`，最终消费像素逐点一致。

### 6.3 常驻原生会话 `--bus-serve` 与 CLI `--bus-in` / `--bus-export` 集成

1. **常驻 stdio-JSONL 宿主**（`nijinLBS --bus-serve`，`heraw_sr2x.cpp:11595–11740, 12268`、`heraw_suite_nib_bus.hpp:1401–1507`）：
   - `NijinClient()`（`sdk/nijin_bus.py:1220–1245`）启动时自动探测并优先拉起同目录原生 `nijinLBS --bus-serve`（`describe` 返回 `backend: "nijinLBS-native"`），仅在无原生二进制的纯脚本环境回退至 Python 参考夹具。
   - `open_session`：接收任意 UTF-8 源路径列表，将公开路径哈希为 ASCII 帧指纹 `sha256:<hex24>`（消除中文目录名触发 ASCII ID 报错），调用真实 `unpack_frame_batch` 解码光子并执行 `analyze_and_plan_cluster`，发布 `active_1x`、`proxy_linear`、相机白平衡线性代理图 `host.proxy_rgb`、实测 `1×` 对齐位移 `align.shifts_1x`（`NIB1_ND [N, 2]`）及 `session_meta`；文件不存在或损坏立即抛 `BusError("decode")`。
   - 四项原生算子（`ops::run_operator` `L1183–1356`）：
     1. `nijin.sensor.gaussian_nll_grad@1`：异方差高斯负对数似然与对均值 $\mu$ 的精确梯度；
     2. `nijin.sensor.diagonal_gls_prox@1`：带零精度/无穷精度极限分支保护的对角 GLS 近端算子，自动传播 `provenance`；
     3. `nijin.geometry.map_points@1`：将任意外部 `Chart` 上的稀疏点表映射到 `active_1x`；
     4. `nijin.optics.composite_prior@1`：校验 `base_rgb.origin == 0` 且 `prior_rgb.origin in {1, 4}`，对 `prior_rgb`、可选 `weight`、`protect_mask` 及 `hole_only` 支撑图统一按各自 `Chart` 仿射逆映射重采样合成；当混合比 $0 < \alpha < 1$ 时按位累积多层来源位（例如生成层与标定层半混保留 `0x10 | 0x02 = 0x12`），$\alpha = 1$ 全量替换时置为新来源位；支持 `adopt_outputs` 原地重命名收编回当前会话，且宿主接收端独立强拒外部注入 `origin == 0`。
2. **主重建管线 `--bus-in` / `--bus-export`**（`heraw_sr2x.cpp`）：
   - 入口执行 `BUS-O5` 会话锁校验与宿主侧 `origin == 0` 外部注入拦截（违例返回退出码 `2`）；单独指定 `--bus-export`（或 `DNG + --bus-export`）时自动激活线性画布依赖并确保写出可重读的 `.nib` 包，无需强绑 `--emit-rgb`；
   - Stage 3 消费 `policy.fuse.exclusions`：按帧衰减 `mw_half` 剔除外部掩膜标定的瞬态遮挡物；
   - Stage 4 消费 `policy.optics`：执行跨 `Chart` `composites[]` 线性先验融合（含 `hole_only` 实测低覆盖区门控与逐层 `bus_provenance` 按位累积）、`protect_mask` 主体保护、`emitter_mask` 发光体门控及散景/光谱色差参数覆盖；
   - Stage 4 末尾（手持光学之后、玻璃柔焦之前）原子写出（`.tmp_nib` → `rename`）`S4_OPTICS` 包，包含 `fused.rgb`、`fused.coverage`、`provenance`、`session_meta` 及第三方扩展字段与策略透传。

---

## §7 阶段 5–6：色彩管理、无第三方 Multi-Part OpenEXR 2.0 与无痕 EXIF I/O

- **DNG 数字底片**（`heraw_exif_io.hpp`）：支持双分量无损 JPEG `SOF3` `2×2 RGGB`（含 X-Trans 输入重排）与三分量 `LinearRaw`；针对 LibRaw `../libraw-heraw/src/decoders/dng.cpp:110` 在 `W == 4 * TW` 时的宽度翻倍陷阱自动折半瓦片宽 `TW`，`dng-hq` 采用 `BlackLevel = 2048` 保留有符号零均值暗部散粒噪声。
- **TIFF 与命名物理通道**：支持 16-bit Deflate、`tiff-hq`（散粒噪声自适应量化 RGB + 水平差分预测 + Deflate 视觉无损压缩，典型体积约 `0.48×`）及内嵌 ExtraSamples 命名物理场通道。
- **零外部库 Multi-Part OpenEXR 2.0**（`heraw_suite_exr_stream.hpp:253`）：仅利用 `zlib` 自研实现 OpenEXR 2.0 标准预测器与字节交织 ZIP 块压缩，按需分 part 写出 `beauty`、`fg`、`bg`、`scatter`、`depth`（支持 `FP16` 与 `FP32`）。
- **隐私无痕 EXIF**（`--no-tool-exif on`）：精确省略处理工具印记 `Tag 11 ProcessingSoftware`，原样保留源相机机身固件 `Tag 305 Software` 与 `ExifIFD 34665`。

---

## §8 双平台原生桌面工作台（macOS Cocoa & Windows Win32）与全参数覆盖

1. **三栏常驻骨架 × 中栏三工作区架构**（`heraw_sr2x_gui_mac.mm`、`heraw_sr2x_gui_win.cpp`）：
   - 采用统一雾灰湖蓝（Muted Lake-Blue `#4E76A8`）设计语言与 `1540 × 940 pt` 三栏协同布局：
     - **左栏（连拍队列、分簇调参弹窗与单帧检视）**：支持连拍分组、选帧/定格锚帧，以及 `分簇调参...`（覆盖 `--decoder`、`--nev-frames`、`--burst-gap-s`、`--min-rel-sharpness`、`--max-shift-px`、`--lucky-imaging`、`--max-cluster-frames` 全部 7 项入库参数）；
     - **中栏三大工作区**：**`1 显影与时空`**（预设 `Photo/Astro/Sci/Archive`、定格/空镜/慢门/频闪/焦点堆叠、计算快门、瞬态/反光分离、超分/RGB RL/`IBP 0-1-2`/双边抛光/码字与截顶似然）、**`2 光学与散景`**（线性玻璃柔焦、连续光谱色差含自定义棱镜角、手持有向旋焦/去二线/拉伸/净洋葱、2.5D 视差以及重对焦模式/自动主体/三圆盘 STF/球差 SA/虚拟 F 值/光圈叶片与转角/沙姆移轴 X·Y/遮挡透视）、**`3 计量与科学场`**（防抖轨迹与五区 8× MTF50 光学体检雷达、`.lcp`/图卡/遥测导出、条件门禁阈值、`核心 11 场 + 扩展几何与观测 16 场` 共 27 项物理场切换、`O1–O10` 独立勾选与 `--observer-gate`/`--readout-ms`/`--obs-pitch-deg`/`--plate-scale`/`--emit-diffuse`/`--emit-light-split`/`--eval-self`/`--next-shot-plan` 及训练切片工厂）；
     - **右栏（当前组门禁速览、按组独立隔离的 `NOPB/1` 外部侧挂槽、交付总控与实时文件回执）**：每个连拍组独立绑定外部 `.nib`，导出侧按单组 `session_bus.nib` / 多组 `session_bus_gNN.nib` 自动隔离命名，并支持 JPEG XS 目标码率 `bpp` 调节。
   - **首次启动性能向导与关于窗**：自动按系统区域选定初始界面语言，并在首次启动向导与「关于」窗口中均提供常驻 `界面语言 / Language` 即时切换器与重新打开性能向导入口。
2. **状态隔离与四路多语言契约**（`heraw_gui_i18n.hpp`、`lang/zh-CN.lng`、`lang/en-US.lng`）：
   - 切换摄影预设（`Photo / Astro / Sci / Archive`）不会篡改硬件性能配置、输出目录或组级外部总线挂载状态（`ui_memory_engine_isolation = 1`）；
   - 全部 `672` 项词条在 `populate_builtin_zh_cn`、`populate_builtin_en_us`、`lang/zh-CN.lng`、`lang/en-US.lng` 四处严格同步，通过 `FULL_PARAM_COVERAGE_TEST`、`WELCOME_ABOUT_TEST` 与 `--test-prefs` 自动化契约自检。

---

## §9 外审对账台账：第二轮意见闭环、显式工程边界与一键复现矩阵

### 9.1 第二轮冻结审查（`V3_OPTICS_SIDECAR_REVIEW_ROUND2_ZH.md`）逐项闭环表

| 审阅章节与问题点 | 源码修复落点 | 实测闭环结果 |
|---|---|---|
| **§1** 默认 `NijinClient()` 连到玩具服务、中文路径报 ASCII 错、假文件仍返回常数 `0.18` 预览 | `sdk/nijin_bus.py:79, 1220–1245`；`heraw_sr2x.cpp:11595–11740`；`heraw_wvdc_router.hpp:459` | 默认 `NijinClient()` 返回 `backend="nijinLBS-native"`；不存在 `.NEF` 抛 `BusError("decode")`；中文路径实拍 Z8 返回 `8256×5504`、`crop12,8`、实测光子 `host.proxy_rgb`（`std=0.1007`）与 `0.8602 px` 对齐位移 |
| **§2.1** `4×1` 掩膜在 `F32` / 旋转 `180° RLE` / `F64` 权重下采样错乱 | `heraw_suite_nib_bus.hpp:589–645, 764–813`；`sdk/nijin_bus.py:1048–1070` | `U8 [0,1,0,1]`、`F32 [0,1,0,1]` 均输出 `[0.8, 0.2, 0.8, 0.2]`；旋转 `180° RLE` 精确输出 `[0.8, 0.8, 0.8, 0.2]`；`F64` 权重正常解包 |
| **§2.1** 分数有效框 `[0, 2.4)` 双线性采样向框外 `2.5` 泄漏 40% 权重；COCO `bytes` 游程报错；C++ 缺稀疏点表测试 | `heraw_suite_nib_bus.hpp:503–519`；`sdk/nib_wire.py:308–323`；`sdk/nijin_bus.py:149–178`；`sdk/tests/test_interop.py:495` | 离散有效中心钳位 `[i_min+0.5, i_max+0.5]` 实测零泄漏；`b"022"` 与 `"022"` 等价解码；C++20 独立读者同时验证 RLE、RGB 与带 `frame=1` 的稀疏点表 |
| **§2.1 / §2.2** 接收端未拦截 `origin=0`；`mark_executed_through(S2)` 误清 `S3–S5` 脏位；`adopt_outputs` 丢改名；局部 `sync_to_host` 丢 `policy`；`set_optics_policy` 覆盖旧键 | `heraw_suite_nib_bus.hpp:1136, 1476–1484`；`sdk/nijin_bus.py:470–476, 489, 722–785, 1163, 1310` | 外部 `attach` / `--bus-in` 注入 `origin=0` 立即拦截；重算 `S2` 后 `dirty_from` 推进至 `S3_FUSE`；`adopt_outputs={"rgb":"my.adopted_comp"}` 按新名读回；`_UNSET` 补丁语义保留未传策略键 |
| **§3 `OP-O1`** 饱和蓝/红出现负下冲；等波长分段在短波跳步 `1.54 px` | `heraw_suite_handheld_optics.hpp:168–242, 793–806` | 位移均分求积（`16 px` 下 `Q=38, max_step=0.579 px`）+ 单像素非负帽 $\alpha(x)$，纯蓝脉冲红通道 `min = 0.00e+00` |
| **§3 `OP-O2`** 原生 `render_refocus` 仍用 `rp = r_f + 0.5` | `heraw_suite_refocus.hpp:281, 513–605` | 原生 `render_refocus` 接入三圆盘正核与精确圆条带原函数，`R=32` 中心脉冲 `OTF(x=16)` 实测 `+0.000633 / -0.000001 / +0.000206` |
| **§3 `OP-O3`** `50%` 亚像素混叠白边向纯黑背景泄漏 `1/6` 亮环 | `heraw_suite_handheld_optics.hpp:500–520` | `fg > 0.05` 外扩 `2 px` 腐蚀背景卷积支撑集，实测 `fg_delta = 0.0`、`bg_halo = 0.0` |
| **独立审计 P1/P2** 单出 `.nib`/报告缺产物；单选观测跑全套；`hole_only`、旋转 RGB 先验与多层 `0x12` 来源位；有向旋焦与双端全参数覆盖 | `heraw_sr2x.cpp`；`heraw_suite_nib_bus.hpp`；`heraw_suite_handheld_optics.hpp`；`heraw_sr2x_gui_{mac.mm,win.cpp}` | 单独 `--bus-export` / `--observers` / `--eval-self` 正常产出；`executed_observers` 严格匹配所选；跨 `Chart` RGB 重采样、`hole_only` 与 `0x12` 位或来源闭环；`swirl` 正负反向；双端 100% 参数覆盖通过 `FULL_PARAM_COVERAGE_TEST` |

### 9.2 当前显式工程边界（供外审精准区分「原生已执行」与「协议扩展槽」）

为避免任何实现口径歧义，以下三处边界按源码现状如实列出：

1. **常驻会话共享传输实现**：`LiveSessionHandle`（`sdk/nijin_bus.py:1298–1318`）与 `nijinLBS --bus-serve` 之间当前通过会话专属临时目录中的 64 字节对齐 `.nib` 文件交换数据，读取端由 `np.memmap` / `FileSlice` 零拷贝映射（原生 C++ 侧直接流式读写字节缓冲，无 Python 浮点列表展开，关闭 `8.8 GB` CPython 膨胀问题）；尚未启用匿名 POSIX `shm_open` 文件描述符传递。主 CLI 流式重建（`--streaming on`）本身完全在内存条带内运行，不产生临时 `.nib` 文件。
2. **`BUS-O2` 可调用物理算子粒度**：`nijinLBS --bus-serve` 原生实现了上述 4 个通用传感器/几何/光学算子（其中 `gaussian_nll_grad@1` 对传入的传感器期望场 $\mu$ 求异方差高斯似然梯度）；针对完整多帧原始 CFA 画布 `frames_lin` 的前向/伴随算子 $(A, A^*)$ 尚未单独暴露为交互式 RPC 算子，而是内置在主 CLI 的 `M5-A` GLS 与 `Q4/Q5` 求解器中执行。
3. **`policy.align`、`policy.fuse.layers` 与 `kernel_bank` 在主 CLI 中的消费范围**：`NijinSession` 与 `NIB/1` 清单完整校验并持久化 `policy.align`（外部 `flow` / `warp_grid` / `sparse_tracks`）、`policy.fuse.exclusions`、`policy.fuse.layers` 与 `policy.optics`（含 `kernel_bank`），且 `--bus-export` 透传外部字段、生效策略与 `Chart`。在当前 `execute_sr_group` 原生像素路径中，已直接执行的是 **`policy.fuse.exclusions`**（非锚帧可见性权重衰减）与 **`policy.optics` 核心合成/保护/发光体/散景/色差槽位**（含 `composites[]`、`hole_only`、`protect_mask`、`emitter_mask`）；Stage 2 配准与核选择默认仍以原生金字塔 + `SIM(2)` + 卷帘样条求解器为准。

### 9.3 一键复现与回归验证命令（全部实测 `EXIT 0`）

在仓库目录 `tools/heraw-sr2x` 下可直接执行：

```bash
# 1. 全平台 6 目标严格构建（-Wall -Wextra -Werror + ASan/UBSan）
make -j8 all nijinLBS_asan

# 2. 手持光学 OP-O1..O3 与 NOPB/1 BUS-O1..O5 C++20 定量预言机（ASan+UBSan）
clang++ -std=c++20 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -I. tests/test_suite_v3_optics_nopb.cpp -o /tmp/t_v31 && /tmp/t_v31

# 3. Unified V3 外部数学审计预言机（含 SUITE_V3_LIBRAW 回读与 DXO_0820.DXO）与观测器/重对焦预言机
clang++ -std=c++20 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Wall -Wextra -Werror -Wno-unused-function -Wno-unneeded-internal-declaration \
  -DHERAW_HAS_WVDC -DNIJINWVDC_STATIC -DSUITE_V3_LIBRAW \
  -I. -I../nraw-bayer-draft/include -I../libraw-heraw \
  tests/test_suite_v3_audit_oracles.cpp ../libraw-heraw/lib/.libs/libraw_r.a \
  build/wvdc_asan/libnijinWVDC.a /opt/homebrew/opt/little-cms2/lib/liblcms2.a \
  -lz -o /tmp/t_v30 && /tmp/t_v30
clang++ -std=c++20 -O2 -g -fsanitize=address,undefined -Wall -Wextra -Werror \
  -I. tests/test_suite_v3_observer_refocus.cpp -o /tmp/t_obs && /tmp/t_obs

# 4. Python/C SDK 跨语言互操作、CRAP 复杂度与分支覆盖门禁
python3 ../../.agent/scripts/gauntlet.py ./sdk \
  --changed nijin_bus.py --changed nib_wire.py --changed tests/test_interop.py

# 5. CLI 实片裁剪全链路回归与双语 GUI 全参数契约自检
uv run --with numpy --with pillow --with tifffile python3 tests/test_suite_cli.py \
  ./nijinLBS_asan ../../scratch/nijin-suite-validation/crops-a9m3 /tmp/nijin_cli_out
python3 tests/test_suite_gui.py --binary ./nijinLBS_gui --output /tmp/nijin_gui_out \
  --sample ../../scratch/nijin-suite-performance/z8-crops
```

---

<a id="gui-layering"></a>

## §10 GUI 功能分组与交互层级演进档案（`3.0.0` 已落地三栏 × 三工作区方案）

> [!NOTE]
> 本节原为四页工作台重构讨论稿；现已在 `3.0.0` 双端工作台（见 §8）正式落地为**常驻左栏（队列 + 分簇调参弹窗 + 单帧检视）× 中栏三工作区（显影与时空 / 光学与散景 / 计量与科学场）× 常驻右栏（门禁速览 + 组级独立 `.nib` 侧挂槽 + 实时交付总控）**，并将全部 27 项物理场、重对焦/分簇/观测器标定参数以及「关于」窗口内的常驻语言切换器完整补齐。下文保留原始设计推演脉络供历史对账参考。

**建议保留左侧连拍队列、帧检视和底部执行栏，把右侧四页改成「出片」「时空与光学」「测量与校准」「数据与外部协同」。** 首页回答“这几组要交付什么”；后三页分别回答“怎样改变成像”“从这组照片能量出什么”“怎样把数据交给别的程序”。例如，「模拟色散」改变成片，归第二页；「测量镜头色差」产生读数和校准档，归第三页。二者共享素材，操作和结果不同。

以下是产品选择建议。当前行为以 §10.1 为准；实现依据和会改变排期的问题见[本轮逐节审计](V3_IMPLEMENTATION_DOSSIER_AUDIT_ZH.md)。

### 10.1 今天打开软件，用户看到什么

| 现有区域（双端显示名） | 实际职责 | 为什么值得调整 |
|---|---|---|
| 常驻左栏与底栏 | 导入文件/目录、自动分组/整组处理/每簇精选/全库挑一组；勾选组、选帧、跳过、指定锚帧、处理散片；底部运行/停止、日志、打开输出目录 | 保留。**选中一组**决定正在检视谁，**勾选若干组**决定将运行谁；两种选择要一直看得出来。 |
| 1 超分与主输出 | 照片/星空/科学/归档、锚点与运动、DNG/RGB、场载体和瞬态层快捷项；同屏还有 OLPF、RL、码字/截顶、后端与预算 | 普通出片和调试求解器占用同一层。首页应保留出片闭环，把低频参数放到命名明确的折叠区。 |
| 2 时空与创意 | 定格/去人/慢门/频闪、计算快门、反光、柔焦、色散、手持散景、2.5D 和重对焦 | 同页已有两种“散景”：处理照片里已有的焦外光斑，或按深度另渲染一张。前者用保护/支持域，后者用几何；入口、门槛和产物要分开。 |
| 3 物理场与数据 | 11 个快捷场、封装载体、训练切片；下方同卡片放观测器、下一拍计划、`.nib` 输入/导出 | “要测哪个量”“写成哪种文件”“谁能改动宿主像素”是三个决定；选择观测器的用户无需经过数据集配置。 |
| 4 镜头与机身 | 轨迹、相位覆盖、五区光学测量、硅片读数，以及 `.lcp` / PNG / JSON 导出 | 这是测量仪表。应与场景观测并列，以“测量对象”分二级导航，保留直接打开镜头体检的入口。 |

落点可由[中文页签](../lang/zh-CN.lng#L163)、[英文页签](../lang/en-US.lng#L163)、[Cocoa 第 2–4 页](../heraw_sr2x_gui_mac.mm#L7543)、[Win32 色散](../heraw_sr2x_gui_win.cpp#L7085)和[Win32 侧挂控件](../heraw_sr2x_gui_win.cpp#L7198)交叉核对。两端页索引从 0 起：`page1` 是第二页，`page2` 是第三页。§8 的页名与“新增能力全部在 Tab 2”应据此勘误。当前两端侧挂导出名实际为 `session_bus.nib`，多组为 `session_bus_gNN.nib`。

### 10.2 建议的四页：每种设置只有一个主位置

| 新位置 | 从哪里迁入；在这里做什么 | 对应的动作或产物 |
|---|---|---|
| **A 出片 · 常用** | 原 1 的摄影预设、基础去噪/去鬼影、DNG/RGB、1×/2×/双出、输出目录。保留定格/去人/慢门、瞬态层、默认场图/内嵌 TIFF、降噪直通及镜头轨迹快捷入口 | 预览本批次的“底片、成片、附加产物”清单和命名；直接运行。点快捷项的标题跳到详情，开关直接改同一设置。 |
| **A 出片 · 重建与输出详情** | 原 1 的 1× `fast/sinc/joint`、OLPF、IBP、PSF、码字约束、截顶；DNG 编码、RGB 色域/高光/抖动、JXS 码率、EXR 精度/parts、EXIF 工具印记 | 展开哪种产物才显示其参数。`--auto-lca` 是重建校正，留在这里；`--spectral-ca` 是主动加的效果，归 B。 |
| **B 时空与光学 · 动作与图层** | 原 2 的定格/锚帧姿态、空镜去人、虚拟 ND、频闪、快门包络/去模糊、瞬态和反光分离 | 明示作用于主图还是另出图层；`--motion-mode` 与 `--shutter-mode` 是两项设置，按受支持组合展示。锚帧本身仍由左栏或同一快捷入口选择。 |
| **B · 成片光学；空间重渲染** | ① 玻璃扩散、横向色散/合焦辉光、已有散景的去二线/切向拉伸/宽高比/去洋葱；② 另一区放 2.5D HTML、重对焦、虚拟光圈/合成孔径 | ① 展示“作用于 RGB”及保护选区来源；② 展示几何门禁和额外文件。外部掩膜只在此显示摘要和跳转，挂载在 D。 |
| **C 测量与校准 · 场景与时间** | 原 3 的物理场选择、O1–O10、下一拍建议；原 2 的尺度政策。11 个 GUI 常用场优先，其余受支持 CLI 场在“全部字段”中按键名/物理量检索 | 选择**量与算法**：显示数值、单位、时钟/尺度来源、可观测性和原因。读数、观测报告 JSON、下一拍计划均能单独成为任务目标。场的**文件载体**由 D 设置。 |
| **C · 镜头与机身** | 原 4 整组迁入：轨迹/相位、8× RAW 精测、径向/切向 MTF、实测 LCA、硅片、校准档 | 保留“运行本组体检”独立命令 `--optics-only` 和 `.lcp` / 遥测 / 图卡入口。测量结果可被其他页引用；打开一份报告不改变重建或模拟参数。 |
| **D 数据与外部协同 · 交付与复用** | 原 3 的场载体（命名 TIFF 通道 / 独立灰度 PNG / 合并 RGB）、数据集切片与置信度、尺寸；汇总 A 的 EXR 和 C 的报告链接 | 决定“谁读取、用什么格网和单位、写到哪里”。EXR 编码只在 A 编辑；字段的名字与单位只在 C 定义。一个 DNG 或一张 TIFF 的日常任务无需打开 D。 |
| **D · 外部控制** | 原 3 的 `.nib` 移入独立卡片：读取本组会话 → 查看 `Chart`/可用字段 → 关联输入及作用角色 → 预览实际生效项 → 导出切面与来源图 | 按**保护选区、发光体、排除指定帧、先验合成、交换字段**分组。显示 `active_1x`、帧顺序/锚点、宿主阶段、线性空间、来源与目标文件。主界面以通用角色命名；任意调用方走同一 SDK / CLI。 |
| **全局 · 性能与引擎** | 原 1 的设备、线程、流式/预算、引擎路径；语言和日志入口仍常驻 | 顶栏显示当前设备和预算，点开内联设置。预算标作“条带调度目标”；运行后另列实测进程峰值和阶段估算。 |

**三个典型走法。** 摄影：拖入连拍 → 看左栏选片/锚点 → A 勾底片和成片 → 运行，全程一页。科研：选定组 → C 选视宁度或镜头体检 → 看单位与门禁 → 只导报告；需要逐像素数据时再去 D 选载体。外部协同：选定组 → D 读取会话、挂上带坐标的保护掩膜 → B 预览作用域 → A 看最终清单；换一个生成掩膜的工具，操作顺序相同。

### 10.3 首页、作用域与状态：把“改了谁、将写出什么”说清楚

1. **首页分三行。** 第一行是“当前组 / 已勾选 N 组 / 有效帧及建议尺度”；第二行是“摄影预设 / 运动意图 / 基础重建”；第三行是“DNG、RGB（1×/2×）、附加输出 / 路径”。原有常用动作保留单击入口，详情用具名跳转，如“已有散景 ▸”“镜头轨迹 ▸”。从详情返回仍指向原组、原控件；切页只是导航。
2. **设置按作用域合并。** 设备/语言/引擎属于机器；目录、命名、交付格式和摄影配方可作本批默认；锚帧/跳过、门禁结果和带会话锁的 `.nib` 属于具体组。建议顺序为“引擎默认 → 批次配方 → 显式组覆盖”。来源旁显示“继承批次”或“本组覆盖”；多组值不同显示“混合”，须显式选择“应用到已勾选组”。切摄影预设仅写摄影参数白名单；输出路径、预算和组绑定独立。重新分组/换锚点后按新指纹重新匹配旧绑定。
3. **双端从同一份有效配置产生参数。** 首页快开、专页编辑和摘要都读写同一语义键；运行前为每一组选取有效配置、校验、冻结为任务快照，再生成 argv 和预期文件。有输出偏好但本组不适用时保留意图，显示“本组跳过：原因”。队列运行中修改下一批参数，不回写已排队任务。
4. **状态用文字和单位，不靠一个圆点。** 分“待测 / 可用 / 相对量 / 条件不足 / 当前引擎仅透传”。例如深度行显示实测 `2× px` 及 `depth_observable`；已测 `false` 时保留选择、停用依赖该深度的动作并给出补拍建议；第一次打开的空报告显示“待测”。手持散景的半径估计走自身支持域，不继承重对焦的视差门槛。是否有时间跨度、是否有行时钟、是否可报告绝对量各自显示。
5. **文件也是完成条件。** 清单对每组区分原始底片、渲染成片、数据层、报告和侧挂切面；文件名、色域/尺度和字段来源在运行前可见。成功需回读请求产物。制作中的外部源路径可能随 `session_meta` 写入包，导出界面应写明；“不写工具 EXIF”属于照片元数据选项。

### 10.4 从当前代码迁移：先让控件的承诺等于引擎行为

| 当前可复现情形 | 下一版界面/引擎共同验收 |
|---|---|
| 只选 `.nib` 导出，或 DNG + `.nib`，CLI 可成功退出但缺包；GUI 已把 `--bus-export` 算作一种产物 | 把所选切面加入画布依赖；两种任务均得到可重新打开的 NIB。C 页单独选观测报告/下一拍计划也能执行，不靠额外勾 RGB 才过首页校验。 |
| 选单项观测时 GUI 同传 `--observers` 与 `--observer-report`；CLI 当前把显式报告路径解释为运行全套 | 把“计算哪些量”与“写到哪里”拆开。对同组分别只选 O7 和 O8，两次回读的**实际执行集合**分别只有各自所选及声明的必要依赖；选择 `all` 才全算。 |
| `hole_only` 和核库已可表达；实拍中“仅填洞”与整图替换相同，多层来源位还会丢失早先的贡献 | 角色列表按具体端口标“已生效 / 仅传输 / 暂不可用”；填洞需实测支持图，科学导出需保留每个有贡献图层的来源位。归属由 C/D 显示，不以“解析成功”点亮“已应用”。 |
| `fused.rgb` 取自手持光学之后、玻璃扩散之前；指标 `fused.coverage` 是半格相位/计数覆盖。旋焦 `+0.9/-0.9` 在测试素材逐点相同 | D 明示包的真实取样位置和格网；B 先把现有旋焦称为“切向拉伸强度”，方向控制以正负设置产生方向差异为开放条件。 |
| GUI 会把同一个输入包路径带给多个组；现有 `--bus-in` 验帧顺序、锚点与几何 | 一组一绑定。对 A/B 两组只给 A 挂包，两端任务预览都只在 A 出现 `--bus-in`；若用户要求套用给 B，列出失配项，提供重选/跳过。 |

实施顺序：**先梳理设置键、作用域和“产物 → 必需阶段”映射；再搬控件；最后调整排版。** 旧偏好按 `photo/astro/sci/arc` 等稳定值迁移；原 `BusInPath` 作“最近使用”供匹配后选用。新旧页的截图不要求像素相同；同一任务在 Cocoa / Win32 应得到等价的参数、诊断、命名和文件内容。

### 10.5 双端验收场景与待定案项

| 场景 | 可观察的通过条件 |
|---|---|
| 一组普通连拍出片 | 首页完成选组、1×/2×、DNG/RGB、目录、运行；`1×` 默认最少 3 张、普通 `2×` 默认 5 张、DxO SuperRAW 四帧单列。运行前清单与实际文件一一相符。 |
| 快捷入口双向 + 重启 | 首页改去人/瞬态/基础场，到专页继续改；返回显示同一值，argv 每个选项一次。切预设、热切中英文、重启，仍保留设备/目录；生产键不依赖文字或 Tab 序号。 |
| 两组混合、一次失配 | A 允许重对焦且挂有保护包，B 的深度门禁失败或帧顺序变化；切组可见原因，A 的参数和成片保持一致。已排队任务以各自快照执行。 |
| 按任务独立出结果 | 单选报告、单选 NIB、DNG + NIB、RGB + EXR `beauty/scatter`、字段载体与非 TIFF 组合各跑一次。扩散开关应改变 EXR 的 `scatter`；同配方 DNG 保持相同，载体回退明确列出文件。 |
| 专业功能可达 | 从首页一次切页能找到已有散景、镜头精测、科学观测和侧挂角色；11 个快捷场与完整字段列表可检索。检查空队列/有实拍/混合选择三态、中英四路词条、键盘焦点、缩放和 Windows DPI；双端分别保留参数快照及实机截图。 |

**请拍板的两点：** ① 首页常驻哪些操作？建议保留原规格的定格/去人/慢门、瞬态层、基础场、直通和镜头入口，光谱/散景参数留详情。② 科学测量和外部协同各占一个顶层页，是否符合你的使用频率？建议各占一页；若日常从挂包开始，可把“本组外部控制”固定到首页一行，继续复用 D 的设置。
