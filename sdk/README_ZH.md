# NOPB/1 控制与协同指南：让任意外部复杂工具直接驾驭 `nijinLBS`

**主客体边界明确：`nijinLBS` 是有状态的确定性物理宿主，外部五花八门的工具（含分割、跟踪、生成式修补、逆问题优化等带幻觉或非规整输出的工具）是调用方与控制方。**  
接入新玩法**不在 `nijinLBS` C++ 内核里加模型名、业务分支或特殊格式解析**，而是通过三层分工完成协同：

1. **宿主导出权威会话与坐标系（Host-First Session & `Chart`）**：由 `nijinLBS` 解码标定连拍 RAW，锁定活动区几何 `session = {active_wh, anchor, frames, geometry_id}`，导出带原生 `Chart` 的参考预览图（Proxy）、噪声统计与散焦估计；外部脚本**无需猜 `active_wh`，也无需手推 $2\times 3$ 仿射矩阵**。
2. **外围 SDK 消化「乱七八糟」的数据形态（Wild-Data Adapters）**：无论是 `SAM 2/3` 的多候选浮点 Logits `[T, M, K, H, W]`、COCO 压缩 `RLE` 游程字典、`PyTorch` 的 `NCHW` 光流、`CoTracker` 稀疏点轨迹 `[T, P, 2]`、`Gyroflow` 双三次控制网格 `WarpGrid`，还是 `Flux/SDXL` 局部修补图，均绑定同一个具名 `Chart` 自动映射回传感器 `active_1x`。
3. **数据名词与控制动词解耦（Role Policies + 跨切面 Dirty-DAG）**：同一张图里的多张掩膜不再只靠静态类型区分，而是显式绑定到 `align`（流场 $\chi^2$ 门控）、`fuse`（路人帧剔除 `exclusions` / 多运动图层 `layers`）与 `optics`（主体保护 `protect_mask` / 发光体选区 `emitter_mask` / 外部生成先验混合 `composites`）。修改 `S4_OPTICS` 策略时，宿主自动保留 `S1..S3` 已算好的线性融合底图，仅毫秒级重算下游切面。

---

## 直接上手：一次会话联动多形态外部数据与策略槽

在项目根目录运行 `python3 -m pip install ./sdk`（或直接在 `sdk/` 目录 `import nijin_bus`）。以下示例展示：从宿主会话获取基准 `Chart`、用链式变换对齐竖拍旋转 + `SAM` 补黑边 `256×256` Logits 与 COCO `RLE`、配置跨切面多掩膜策略，并在常驻子进程中完成带像素级溯源（`provenance`）的生成式补洞合成：

```python
from array import array
from nijin_bus import Chart, NijinClient, _rle_encode_coco

with NijinClient() as client:
    # 1. 宿主打开连拍并导出权威几何与参考预览图 (origin=0 nijin_measured)
    with client.open_burst(["DSC0001.ARW", "DSC0002.ARW"], active_wh=(600, 800)) as live:
        s = live.local
        # 2. 链式构造外部工具坐标系：顺时针转 90° -> SAM 左上对齐补边 1024 -> 0.25x Logits (零手推矩阵)
        sam_1024 = s.charts["active_1x"].rotate90(1).letterbox(1024, align="top_left", name="sam1024")
        sam_256 = sam_1024.scale(0.25, name="sam256")
        s.register_chart("sam1024", sam_1024)
        s.register_chart("sam256", sam_256)

        # 3. 直接喂入异构外部数据：COCO RLE 掩膜、多候选 Logits、稀疏轨迹
        coco_counts = _rle_encode_coco([1024 * 200, 1024 * 300, 1024 * 524])
        s.put_rle_mask("sam.subject", {"size": [1024, 1024], "counts": coco_counts},
                       chart="sam1024", target_stage="S4_OPTICS")
        s.put_rle_mask("sam.passerby", [256 * 100, 256 * 40, 256 * 116], mask_hw=(256, 256),
                       chart="sam256", target_stage="S3_FUSE")

        # 4. 显式绑定控制策略（区分「路人剔除」与「散景主体保护」），宿主自动计算脏切面范围
        s.add_exclusion(mask="sam.passerby", frames=[1])
        s.set_optics_policy(protect_mask="sam.subject", bokeh_de_nisen=0.85,
                            spectral_ca="axial-aura", spectral_ca_power=6.0)
        delta = live.sync_to_host()
        print("Invalidated stages:", delta["invalidated_stages"])
```

---

## 核心能力速查表

| 模块 | 调用入口与契约 |
|---|---|
| **链式坐标系 `Chart`** | `Chart.active(w, h)` 派生 `.resize(w, h)`、`.scale(sx, sy)`、`.pad(...)`、`.letterbox(1024, align="top_left"\|"center")`、`.crop(x0,y0,x1,y1)`、`.rotate90(k)`。`chart.sample_at_active(x, y)` 在 `valid_bbox` 外返回 `None`（保留真实裁剪缺测），框内夹紧到有效像素中心（阻断 `SAM` 补零黑边渗漏）；`Chart.map_flow(src, dst, u, v, du, dv)` 执行双图位移换基 $T_t(u+v)-T_s(u)$；`chart.map_cov2(...)` 执行 $A\Sigma A^\top$ 合同推前。 |
| **异构外部数据适配器** | `put_mask`（支持 `2D/3D/4D/5D` 多帧多目标多候选掩膜与浮点 Logits + `scores`）、`put_rle_mask` / `decode_rle_mask`（原生支持 COCO 压缩字符串与游程数组）、`put_flow`（支持 `HWC/NHWC/CHW/NCHW`）、`put_sparse_tracks`（`[T, P, 2]` + 可见性/置信度）、`put_warp_grid`（`[N, Gh, Gw, 2]`）、`put_prior_rgb`（生成式线性 RGB）、`put_json` 与任意阶 `put`。自动接纳 `PyTorch` CPU/GPU 张量与 `NumPy` 切片，读取端 `s[name].numpy()` 使用只读 `mmap`。 |
| **控制策略与跨切面增量重算** | `set_align_policy`（候选流/网格描述与透传；主配准默认由宿主金字塔 + `SIM(2)` + 卷帘样条执行）、`add_exclusion`（Stage 3 按帧剔除遮挡物以重建干净背景，并在全帧剔除处暴露 `fused.support == 0` 实测空洞；`fused.coverage` 则记录半格相位/帧数评分）、`add_layer`（多运动图层描述与透传）、`add_composite` + `set_optics_policy`（Stage 4 像素路径原生消费：`hole_only` 依据 `fused.support` 仅填无实测光子空洞、跨 `Chart` RGB/掩膜重采样、正负旋焦散景整形、光谱色散、`protect_mask` / `emitter_mask`）。`attach()` 支持跨切面挂载并按 `S1->S5` 依赖图返回 `cached_stages / invalidated_stages`（常驻会话起于 `S2_ALIGN`，完整 `S3–S5` 重建走主 CLI `--bus-in` / `--bus-export`）。 |
| **严格幻觉隔离与像素溯源** | `origin = 0 (nijin_measured)` 仅限宿主内部实测场写入；无论 CLI `--bus-in` 还是常驻 RPC `attach` 注入 `origin=0` 均由宿主直接拦截（`BusError("origin")`）。多层合成在部分混合（`0 < alpha < 1`）时按位并集累积历史来源位（如生成先验 `0x10` 与标定层 `0x02` 半透明叠加精确输出 `0x12`），仅在完全替换（`alpha == 1`）时覆盖旧位。 |

---

## 跨语言控制面（`nopb/1`：有状态常驻会话 + 离线 `.nib` 双模）

每行一个 UTF-8 JSON 请求，大张量走只读映射的 `.nib` 或共享内存切片，不走 Base64。既支持离线批处理文件调用，也支持 `open_session -> attach -> call -> export -> close_session` 常驻会话（避免 45MP 连拍反复解码 RAW 或每步全量写盘）：

```json
{"api":"nopb/1","id":1,"method":"describe"}
{"api":"nopb/1","id":2,"method":"open_session","frames":["DSC0001.ARW","DSC0002.ARW"],"active_wh":[8256,4644],"geometry_id":"sensor:active-v1"}
{"api":"nopb/1","id":3,"method":"attach","session_handle":"sess-1","input":"external_sam_and_flux.nib"}
{"api":"nopb/1","id":4,"method":"call","session_handle":"sess-1","op":"nijin.optics.composite_prior@1","inputs":{"base_rgb":"fused.rgb","prior_rgb":"flux.fill","weight":"hole.w4","protect_mask":"fg.rle"},"output":"composited.nib"}
```

算子端口显式区分必填 `inputs` 与可选 `optional_inputs`（缺省时走宿主原生路径，无需凑齐伪造张量），语义标签支持前缀通配（如 `mask.*`、`radiance.*`）。随 SDK 内置 4 个跨语言契约验证算子：
- `nijin.sensor.gaussian_nll_grad@1`：独立高斯似然 NLL 与均值梯度（`mu=2, y=5, variance=4 -> grad=-0.75`）；
- `nijin.sensor.diagonal_gls_prox@1`：对角 GLS 近端步并自动传播 `provenance` 位掩码；
- `nijin.geometry.map_points@1`：自动读取张量绑定的 `Chart`（无需额外手工传 `affine`）将任意阶 `(..., 2)` 点集映射至 `active_1x`；
- `nijin.optics.composite_prior@1`：结合 `protect_mask`（含 RLE）混合实测线性底图与外部生成先验，逐像素输出 `PROV_MEASURED (0x01)` / `PROV_GENERATIVE (0x10)` 溯源图。

---

## 二进制线协议（`NIB/1`）与五切面对接表

C11 / C++20 原生只读头见 [nib_v1.h](nib_v1.h)，参考线编解码见 [nib_wire.py](nib_wire.py)：
- **固定布局**：`64 B` Header（`<8s6I4Q`，含魔数 `NIJBUS01`、快速指纹 `FNV-1a 64`）+ `128 B` Descriptor（`<40s4H2i4I6f4I2Q`，`40 B` ASCII 短键；超长或 Unicode 字段名自动哈希映射并在 UTF-8 manifest 保留 `public_name`）+ `64 B` 边界对齐连续小端 payload。
- **统一几何与任意阶支持**：四维空间栅格使用 `[batch, y, x, channel]`；任意阶或非栅格数据（`5D` 多候选 Logits、`RLE` 游程、稀疏轨迹、`WarpGrid`、`6D` 光谱核库、JSON 树）设 `kind = 3 (NIB1_ND)`、`shape = (0,0,0,0)`，真实 `dims/axes` 存于 manifest——**若绑定了空间 `Chart`，其描述符与 manifest 同步携带 `affine / bbox / valid_bbox_f`**，不再丢失坐标系。

| 逻辑切面 | 宿主导出（`origin=0` 物理真值） | 外部可注入控制项与数据（`origin=1..4`） |
|---|---|---|
| `S1_CALIB` | 去黑边活动区几何、`Chart("active_1x")`、锚点预览图、噪声模型、曝光时钟 | 传感器/光谱/镜头外部标定表（`co_calibrated`） |
| `S2_ALIGN` | 原生配准流、残差图、遮挡图、散焦宽度估计 `wblur` | 外部稠密流（`HWC/NCHW`）、`CoTracker` 稀疏轨迹、`WarpGrid` + `chi2_gated` 策略 |
| `S3_FUSE` | 带符号线性超分 RGB、后验标准差 `sigma`、真实覆盖帧数 `coverage` | `exclusions`（帧级路人/遮挡剔除）、`layers`（多层掩膜与独立运动超分）、物理似然 `nll_grad / prox` 迭代查询 |
| `S4_OPTICS` | 线性光学渲染结果（`De-Nisen` 散景整形、三圆盘 STF、连续光谱色散） | `composites`（`Flux` 空洞修补线性层）、`protect_mask`、`emitter_mask`、外部 `KernelBank` |
| `S5_DELIVER` | 最终 EXR / TIFF / DNG 及逐像素 `provenance` 溯源通道 | 附加元数据与自定义命名空间透传字段 |
