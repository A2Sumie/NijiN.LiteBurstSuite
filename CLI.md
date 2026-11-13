# `NijiN.LiteBurstScaler` (`nijinLBS`) — Fast Multi-Frame Upscaler (`v1.6.2`)

`NijiN.LiteBurstScaler`（简称 **`nijinLBS`**，*Fast Multi-Frame Upscaler*）提供跨平台 **GPU（macOS `Metal` + Windows `Vulkan`）与 CPU（ARM64 NEON / x86_64 AVX2）** 双引擎多帧 **2× 超分辨率** CLI 及原生桌面端 GUI（内置静态 `LibRaw 0.23.0-Snapshot202611` 支持 Nikon Z8/Z9 HE/HE\*，支持 `--libraw` 外部动态库覆盖，内置 `SVT-JPEG-XS` 与 `zlib`）。

> 完整的六阶段数学推导、泊松-高斯盲标定、跨通道防吞星门控与 16-bit 物理场图工作流详见 [**`ALGORITHM.md`**](ALGORITHM.md)。

---

## 1. 算法所需张数判定、免黑图自标定与运动物理规律

### 1.1 实测收敛曲线（Nikon Z 8 HE\* `8280×5520 -> 16560×11040`）

| 连拍张数 `N` | 有效噪声比 `eta` | 多帧信噪比增益 | 相对满收敛 `PSNR` | 同相位高频残差 `HF_RMS` | 物理与工程阶段判定 |
| :---: | :---: | :---: | :---: | :---: | :--- |
| **`1` 张** | `0.250` (空间核退化) | `0.0 dB` | `44.30 dB` | `4.369e-3` | **单帧退化模式**：仅做各向异性空间插值与 GAT-DCT 降噪 |
| **`2–3` 张** | `0.371 -> 0.303` | `+4.3 ~ +5.2 dB` | `47.66 ~ 50.55 dB` | `0.900 -> 0.760e-3` | **亚像素欠采样**：覆盖部分 `2×2` 相位象限 |
| **`4` 张** | **`0.234`** | **`+6.31 dB`** | **`56.70 dB`** | **`0.773e-3`** | **最低有效超分门槛（默认下限）**：满足 `2×2` 四象限亚像素代数锁定门槛 |
| **`5` 张** | **`0.208`** | **`+6.83 dB`** | **`66.16 dB`** | **`0.735e-3`** | **超定鲁棒门槛**：M-estimator 外点抑制与 `R/B` 四分之一密度色度载波同时超定 |
| **`6–8` 张** | **`0.173 -> 0.149`** | **`+7.6 ~ +8.8 dB`** | **`>= 70.0 dB`** | **`0.694e-3`** | **最佳推荐甜点区间**：`80%–92%` 亚像素覆盖，允许剔除 1–2 张微糊帧仍满血输出 |
| **`9–12` 张** | `< 0.13` | `+8.9 ~ +10.0 dB` | 收敛饱和 | `~0.69e-3` | **暗光高感增强**：进一步压低深暗部散粒噪声 |

### 1.2 免黑图 `G1/G2` 盲暗电平自标定与帧间运动判定
- **全自动免黑图自标定**：利用拜耳阵列中同光谱响应的 `G1/G2` 绿色通道分块中位数盲标定暗电平差值并抑制深暗部负偏漂移，配合跨帧中位数时域统计自动修复恒定热噪点，全程无需拍摄独立黑图。
- **帧适格与运动分类**：
  - **`MOTION_BLUR`（单帧抖动糊片）**：单帧高频梯度锐度 `acutance < min_rel_sharpness * 同场景中位锐度`（`photo` 预设 `0.72`，`sci` 预设 `0.82`，`arc` 预设 `0.75`）。
  - **`MOTION_TOO_LARGE`（运动过大 / 切镜 / 局部大面积非刚性运动）**：全局位移 `shift_1x > max_shift_px`（默认 `120` 1x 像素）、相关峰值 `ZNCC < 0.36`，或分块局部运动离散度 `> 38 px`。在目录扫描或多组模式下自动切分为新超分组；在单组模式下作为外点剔除。
  - **`MOTION_TOO_SMALL`（运动过小 / 三脚架死锁）**：`N >= 2` 且整组最大帧间位移 `< 0.15` 1x 像素。多帧时域降噪正常生效，交互模式会提示亚像素位移偏小。
  - **`EXPOSURE_OUTLIER` / `DIM_MISMATCH`（曝光异常或画幅不一）**：中位亮度偏离同组中位数超过 `~0.95 EV`、误拍全黑盖镜头帧，或传感器裁切画幅不一致。

---

## 2. 按需输出格式、倍率参考（未压缩 = `1.00`）与色彩空间

支持按需单独输出 **RGGB 线性 RAW（`.dng`）**、单独输出 **渲染 RGB（`.png` / `.jxs` / `.tif`）**，或一次合成同时输出两者。

### 2.1 2× RGGB 线性 RAW 输出（标准 DNG 1.4，`--raw-format`，以未压缩 = `1.00` 为基准）

| 参数值 | 位深与编码机制 | 倍率参考 (`未压缩 = 1.00`) | 实测体积 (`16560×11040`) | 适用场景与兼容性 |
| :--- | :--- | :---: | :---: | :--- |
| **`dng`** *(默认)* | **16-bit 无损 DNG**（`Compression=7` ISO/IEC 10918-1 `SOF3` 2D 同相位预测 `PSV=7`） | **`0.39`** *(压缩比 `1:2.6`)* | **~`135 MiB`** | **零误差无损**，兼容 Lightroom、ACR、Capture One、darktable、macOS 预览及 LibRaw |
| **`dng-hq`** | **高级有损压缩 DNG**（**14-bit → 16-bit** 残余散粒噪声自适应曲线 `Tag 50712` + `SOF3`） | **`0.09`** *(压缩比 `1:11.6`)* | **~`30 MiB`** | 量化步长低于超分后残余散粒/读出噪声（实测 `RAW PSNR = 66.27 dB`），通用 DNG 1.4 兼容 |
| **`dng-fp16`** | **16-bit 半精度浮点 HDR DNG**（`SampleFormat=3` + `Predictor=34894` + `Deflate`） | **`0.29`** *(压缩比 `1:3.5`)* | **~`100 MiB`** | 保留 30 档浮点动态范围，适合高动态后期与科学流程 |
| **`dng-raw`** | **16-bit 未压缩 DNG**（`Compression=1`） | **`1.00`** *(基准)* | **`349 MiB`** | 零解压 CPU 开销的底层直读基准 |

### 2.2 2× 渲染 RGB 输出（`--rgb-format`，以 16-bit 未压缩 TIFF = `1.00` 为基准）与色彩空间

| 参数值 | 位深与编码机制 | 倍率参考 (`未压缩 = 1.00`) | 实测体积 (`16560×11040`) | 适用场景与兼容性 |
| :--- | :--- | :---: | :---: | :--- |
| **`tiff-deflate`** *(默认)* | **16-bit 无损 TIFF**（多线程 32 行分条 `Deflate` + 16-bit 字级 `Predictor=2` 水平差分） | **`0.39`** *(压缩比 `1:2.6`)* | **~`410 MiB`** | **最高效全 16-bit 无损默认格式**（体积最小且编码最快），Photoshop、Capture One、Affinity、macOS 预览与 OpenCV/tifffile 原生直读 |
| **`png`** | **16-bit 无损 PNG**（多线程并行 Deflate + 字节级 `Sub` 预测 + `eXIf` 块） | **`0.40`** *(压缩比 `1:2.5`)* | **~`420 MiB`** | 网页浏览器、看图器、Photoshop 与 PyTorch/OpenCV 开箱直读 |
| **`jxs`** | **14-bit JPEG XS 高级有损压缩（`.jxs`）**（`SVT-JPEG-XS` 14-bit 平面 RGB + `RCT`，默认 `10 bpp`） | **`0.21`** *(压缩比 `1:4.8`)* | **~`218 MiB`** | ISO/IEC 21122-3 `.jxs` 容器内嵌 `Exif` 与 `colr` 盒；支持 `--decode-jxs` 转回 16-bit TIFF/PNG |
| **`jxs-lossless`** | **14-bit JPEG XS 无损（`.jxs`，`Fq=0`）** | **`0.43`** *(压缩比 `1:2.3`)* | **~`450 MiB`** | 14-bit 零损编码极速读写 |
| **`tiff-raw`** | **16-bit 未压缩 TIFF** | **`1.00`** *(基准)* | **`1046 MiB`** | 未压缩 RGB 基准格式 |

- **色彩空间（`--color-space`）**：
  - `srgb` *(默认)*：标准 D65 sRGB 矩阵与分段传递函数，写入标准 `sRGB` 标记。
  - `adobergb`：ISO 12640-2 **Adobe RGB (1998)** 广色域矩阵与精确 $\gamma = 563/256$ 传递函数，在 PNG（`iCCP`+`cHRM`+`gAMA`）、JPEG XS（`colr` `'prof'` box）与 TIFF（`Tag 34675`）中自动内嵌标准 **ICC v2.4** 配置文件。

---

## 3. EXIF / GPS 元数据保护策略

从每组参考帧（Anchor RAW）按原字节序提取 `IFD0`、`ExifIFD`（`34665`）与 `GPSInfo`（`34853`），写入 DNG、PNG（`eXIf` 块）、JPEG XS（`Exif` 盒）与 TIFF：
- **完整保留**：快门、光圈、ISO/感光度类型、曝光补偿、测光/闪光模式、拍摄时间与毫秒级 `SubSecTimeOriginal`、时区偏移 `OffsetTime*`、焦距与等效 35mm 焦距、机身与镜头型号/序列号、作者 `Artist`、版权 `Copyright`、全部 GPS 坐标与时间戳。
- **保留原始固件并追加处理记录**：组合为 `"<原相机固件> | NijiN.LiteBurstScaler (nijinLBS) 1.6.2 (...)"`，同步写入标准 `Tag 11 ProcessingSoftware`。
- **更新 2× 几何标签**：清理绑定 1× 原始字节偏移的 `MakerNote` 与 1× 缩略图偏移；将 `PixelXDimension` / `PixelYDimension` 与焦平面分辨率 `FocalPlaneX/YResolution` 精确更新至 `2×`。

---

## 4. 四档模式预设、纯物理深度重建、智能向导与跨平台 GPU 加速

### 4.1 四档模式预设（`Photo 照片` / `Sci 科学` / `Arc 归档` / `Astro 星空`）

| 预设名称 | CLI 参数 | 设计定位与自动联动策略 |
| :--- | :---: | :--- |
| **`Photo 照片`** *(默认)* | `--preset photo` | **日常摄影出片**：开启跨帧时域相干保微纹理、刚体微块超分、线性 RAW 4 次 + RGB 2 次光学 PSF 反卷积恢复、自适应分位数自动曝光、保色相高光滚降与平坦区边缘抛光。 |
| **`Sci 科学`** | `--preset sci` | **科研测量与模型训练真值**：一键关闭光学 PSF 反卷积（`0` 次）、自动曝光增益（固定 `1.0x` 辐射度线性对齐）、高光非线性滚降与平坦区抛光；降低空间降噪（`0.40/0.55`）保留原生微纹理与噪声功率谱，收紧运动去鬼影（`1.35`）与清晰度门槛（`0.82`）。 |
| **`Arc 归档`** | `--preset arc` | **数字底片中性归档**：关闭光学 PSF 反卷积与平坦区抛光，采用温和空间降噪（`0.70/0.85`）与标准影调映射，适合长期母版留存与二次精修。 |
| **`Astro 星空`** | `--preset astro` | **无赤道仪手持打星 / 手持星野**：启用跨通道拜耳邻域“防吞星”门控、关闭星点色度中值与紫边消色以完整保留恒星黑体与星云色彩、星点质心二阶多项式 + 天际线**天地双域独立对齐**（默认中间帧定格）、无黑环星形 RL 恢复与夜空黑位锚定 + 保色 `asinh` 星云拉伸。 |

### 4.2 纯物理深度细节重建与 16-bit 物理场图（`Field Map`）
1. **跨帧时域梯度相干门控（Temporal Gradient Coherence）**：逐像素统计多帧对齐梯度的矢量相干度，自动豁免多帧同相位的真实微弱纹理（羽毛、织物、暗部树叶、星云丝缕）在 5 层 8×8 GAT-DCT Wiener 中的收缩（CPU / Metal / Vulkan 三端同步生效）。
2. **三门控高置信刚体微块捞取（Rigid Micro-Patch SR）**：在局部运动被静态背景拒绝的纹理区，仅对同时通过高相关度（`ZNCC >= 0.90`）与泊松-高斯细/宽双尺度卡方残差检验的刚体微块捞取邻帧真实亚像素样本，非刚性形变无缝退回单帧多帧干净拜耳引导的 Hamilton-Adams 二阶方向插值。
3. **16-bit 无损三通道物理场图（`--out-field <file.png>` / `--emit-field`）**：
   导出一张与 2× 主图同尺寸、像素严丝合缝对齐的 16-bit 无损 PNG（`0.0 -> 0`, `1.0 -> 65535`）：
   - **`R` 红通道**：常规模式下输出防镂空形态学**运动掩膜场**（亮 = 局部移动物体，暗 = 静态背景）；`Astro` 星野模式下自动输出**天空/地景天际线分割蒙版**（亮 = 星空天球域，暗 = 地面山脊前景），可在 Photoshop / Affinity 通道面板一键载入选区分区调色；
   - **`G` 绿通道**：多帧 `2×2` 亚像素超分相位置信度（亮 = 四象限满覆盖高帧数真 2× 超分，可供 `Sci` 模式深度学习训练按像素加权 Loss）；
   - **`B` 蓝通道**：二阶拉普拉斯区域包络**光学对焦锐度场**（亮 = 合焦主体高频纹理，`0` = 焦外散景虚化，引擎内部同步以此压制散景 RL 锐化）；
   - **直观色彩速查**：**青绿色（Cyan）** = 静态合焦真 2× 超分区；**纯绿色（Green）** = 静态焦外散景/平滑天空；**品红色（Magenta）** = 合焦运动主体定格区（完整公式与 PyTorch/PS 用法见 [`ALGORITHM.md`](ALGORITHM.md)）。
4. **五维智能向导（`--wizard` / GUI「智能向导」）**：毫秒级识别「手持星野 / 纯星空 / AEB 包围曝光 / 三脚架稳态 / 局部动体 / 高感弱光」，给出超分潜力分、皇冠最锐帧、首帧快门微震提示与整套推荐参数处方，支持任选第 `1..N` 张作为姿态定格帧。

### 4.3 零依赖跨平台 GPU 计算加速（`--backend auto|gpu|cpu`）
- **Windows（`Vulkan`）**：内嵌 24 个预编译 Vulkan 1.0 `SPIR-V` 计算着色器，运行时动态加载显卡驱动自带 `vulkan-1.dll`（NVIDIA / AMD / Intel 独显与核显通用，零外部 SDK/DLL 依赖）。在 `RTX 4080 SUPER` 上，7 张 45.7 MP Nikon Z8 HE\* 合成 `16560×11040`（`1.83 亿像素`）的 Stage 4+5 计算耗时约 **`1.9s`**，端到端总耗时 **`~9.0s`**。
- **macOS（`Metal`）**：原生调用 `Metal.framework` 与 Apple Silicon `MTLResourceStorageModeShared` 统一内存零拷贝缓冲，与 CPU 参考管线的 16-bit DNG 一致性达 **`PSNR > 80 dB`**。
- **自动回退**：默认 `--backend auto` 优先使用 GPU，若运行环境无可用 GPU 则自动切换至纯 CPU NEON / AVX2 SIMD 管线。

---

## 5. CLI 命令参考

### 5.1 核心用法概览
```bash
# 1) 单组连拍：默认同时输出最高效无损组合 —— 16-bit 无损 RGGB DNG (0.39x) + 16-bit 无损 TIFF (0.39x)
nijinLBS \
  --out-raw sr2x_rggb.dng \
  --out-rgb sr2x_rgb.tif \
  Z8S_8462.NEF Z8S_8463.NEF Z8S_8464.NEF Z8S_8465.NEF Z8S_8466.NEF Z8S_8467.NEF

# 2) 大批量文件夹扫描：自动按 EXIF 时间排序、发现所有 >=4 张可超分组，输出 14-bit JPEG XS 高级有损压缩 (Adobe RGB)
nijinLBS --scan-dir /path/to/shoot_card --recursive \
  --sort-time --policy auto-prune \
  --out-dir /path/to/sr_outputs --emit-rgb --rgb-format jxs --jxs-bpp 10.0 --color-space adobergb

# 3) Sci 科学模式：严格辐射度线性输出无损 DNG 与无损 16-bit PNG
nijinLBS --preset sci \
  --scan-dir /path/to/raw_bursts \
  --out-dir /path/to/sci_dataset --emit-raw --emit-rgb --rgb-format png

# 4) 加载自定义外部 LibRaw 动态库执行解码与体检
nijinLBS --libraw /custom/path/libraw_r.dylib --inspect /path/to/shoot_card

# 5) 交互式诊断模式：遇到不足 4 张、糊片、三脚架零位移或长序列时，终端给出至多 2 个推荐选项确认
nijinLBS --policy interactive --out-rgb out.png *.NEF

# 6) 将 14-bit JPEG XS (.jxs) 解码转回 16-bit 无损 PNG 或 TIFF
nijinLBS --decode-jxs input_sr2x.jxs decoded_16bit.png
```

### 5.2 完整参数说明表

| 类别 | CLI 选项 | 默认值 | 说明 |
| :--- | :--- | :---: | :--- |
| **输入、解码与分组** | `--scan-dir <dir>` | - | 扫描目录中的 RAW 文件并自动识别可超分连拍组（位置参数亦可直接传目录） |
| | `--recursive` | `off` | 扫描文件夹时递归遍历子目录 |
| | `--sort-time` / `--no-sort-time` | `--sort-time` | 是否按 EXIF `DateTimeOriginal` + 毫秒 `SubSec` + 自然文件名自动排序 |
| | `--libraw <lib_path>` | 内置 `0.23-HE*` | 运行时动态加载外部自定义 `LibRaw` 共享库（`.dll` / `.dylib` / `.so`） |
| | `--policy <mode>` | `auto-prune` | `auto-prune`（剔除异常，需 `>=4` 张）、`multi-group`（长序列自动多组）、`force-all`（强制全部执行，最低 1 张）、`interactive`（异常/长序列提供 `<=2` 个推荐选项交互确认） |
| | `--interactive-choice <1\|2>` | `0` (询问) | 在 `interactive` 模式下预选方案 `1` 或 `2` |
| | `--group-size <N>` | `8` | `multi-group` 或批量长连拍切分时的目标每组帧数（下限 `4`） |
| | `--max-frames <K>` | `0` (不限) | 每组最多保留清晰度最高的前 `K` 张亮帧 |
| | `--burst-gap-s <sec>` | `3.5` | 批量目录扫描时判定同一候选连拍簇的最大相邻快门时间间隔（秒） |
| **输出与色彩** | `--out-raw <file.dng>` | - | 单组输出 2× RGGB DNG 文件路径 |
| | `--out-rgb <file>` | - | 单组输出 2× RGB 文件路径（自动按后缀识别 `.png`/`.jxs`/`.tif`） |
| | `--out-field <file.png>` | - | 单组输出 2× 16-bit RGB 物理场图（`R`=运动掩膜, `G`=超分相位置信度, `B`=对焦锐度场） |
| | `--out-dir <dir>` | - | 批量/多组输出目录（自动命名为 `<首帧名>_nijinLBS.<ext>`） |
| | `--emit-raw` / `--emit-rgb` / `--emit-field` | - | 配合 `--out-dir` 按需开启 RGGB DNG、RGB 与 16-bit 物理场图输出 |
| | `--raw-format <fmt>` | `dng` | `dng`（16-bit 无损，倍率 `0.39`，默认）、`dng-hq`（高级有损压缩，倍率 `0.09`）、`dng-fp16`（16-bit 半精度浮点，倍率 `0.29`）、`dng-raw`（16-bit 未压缩，基准 `1.00`） |
| | `--rgb-format <fmt>` | `tiff-deflate` | `tiff-deflate`（16-bit 无损 TIFF，倍率 `0.39`，默认）、`png`（16-bit 无损 PNG，倍率 `0.40`）、`jxs`（14-bit JPEG XS 高级有损压缩，倍率 `0.21`）、`jxs-lossless`（14-bit JPEG XS 无损，倍率 `0.43`）、`tiff-raw`（16-bit 未压缩 TIFF，基准 `1.00`） |
| | `--jxs-bpp <float>` | `10.0` | `jxs` 高级有损压缩模式的目标每像素比特数（范围 `3.0..28.0`） |
| | `--color-space <cs>` | `srgb` | `srgb` 或 `adobergb`（自动计算广色域矩阵并内嵌 ICC v2.4 配置文件） |
| **预设、定格与后端** | `--preset <name>` | `photo` | `photo`（Photo 照片）、`sci`（Sci 科学）、`arc`（Arc 归档）、`astro`（Astro 星空 / 手持星野） |
| | `--wizard` | `off` | 启动终端五维场景诊断与智能参数处方向导（支持一键采纳或任选定格帧） |
| | `--anchor-frame <sel>` | `auto` | 运动主体/天际线定格帧：`auto`（自动最锐帧）、`first`（首帧）、`middle`（中间帧）或 `1..N` 序号 |
| | `--backend <mode>` | `auto` | `auto`（GPU 优先，无 GPU 自动退回 CPU）、`gpu`（强制 Metal / Vulkan GPU）、`cpu`（纯 CPU SIMD） |
| | `--denoise-strength <0..2.5>` | 随预设 | 2× 亮度域 5 层 GAT-DCT Wiener 降噪倍率（`0` = 旁路空间亮度降噪） |
| | `--chroma-denoise <0..2.5>` | 随预设 | 亮度引导 6 层 À-Trous 色度降噪倍率 |
| | `--psf-iters <0..12>` | 随预设 | 线性 RAW 阶段光学 PSF RL 反卷积迭代次数（`0` = 关闭） |
| | `--rgb-psf-iters <0..8>` | 随预设 | RGB 导出阶段微反差 RL 迭代次数（`0` = 关闭） |
| | `--psf-sigma <0.35..3.0>` | `1.05` | 2× 网格上的光学 PSF 高斯半径 |
| | `--deghost-strength <0.4..3.0>` | 随预设 | 局部运动/视差去鬼影严格度（越大越严格） |
| | `--auto-exposure <on\|off>` | 随预设 | 盲分位数自动曝光增益开关 |
| | `--highlight-rolloff <on\|off>` | 随预设 | 高光软滚降与镜面高光色度抑制开关 |
| | `--edge-polish <on\|off>` | 随预设 | DCT 后平坦区域双边抛光开关 |
| | `--dither <on\|off>` | `on` | 16-bit 量化 TPDF 三角概率抖动开关 |
| | `--ev-offset <EV>` | `0.0` | 额外手动曝光偏移（`-4.0 .. +4.0 EV`） |
| | `--min-rel-sharpness <0.1..0.99>` | 随预设 | 自动剔除糊片的相对锐度比门槛 |
| | `--max-shift-px <px>` | `120.0` | 判定运动过大的 1× 全局位移上限（像素） |
| **诊断与集成** | `--inspect` | `off` | 仅执行快速扫描、运动与分组体检并退出 |
| | `--plan-json` | `off` | 向 `stdout` 输出结构化分组与交互推荐方案 JSON |
| | `--progress-json` | `off` | 向 `stderr` 实时输出单行 JSON 进度事件 |
| | `--decode-jxs <in> <out>` | - | 独立工具模式：将 `.jxs`/`.jxc` 解码为 16-bit `.png` 或 `.tif` |
| | `--threads <N>` | 全部逻辑核 | 并行 CPU 工作线程数 |

### 5.3 退出码（Exit Codes）
- `0`：执行成功（或交互模式下用户选择取消）。
- `1`：运行时错误（RAW 解码失败、动态库加载失败、磁盘写入失败等）。
- `2`：命令行参数语法错误。
- `3`：非交互策略下有效帧不足 4 张（或批量扫描目录中未发现任何 `>=4` 张的可超分组）。

---

## 6. 配套原生卡片式 GUI 说明

macOS 端通过 `.dmg` 拖入 **`NijiN.LiteBurstScaler.app`** 运行（原生支持 `Cmd+Q` 退出、`Cmd+W` 关窗、`Cmd+X/C/V/A` 编辑快捷键及 Dock 图标拖放导入），Windows 端双击 `nijinLBS_gui.exe`：
- **macOS 首次下载解冻（Gatekeeper 隔离属性移除）**：浏览器下载 `.dmg` 并拖入 `/Applications` 后，若首次启动提示「已损坏」或「无法验证开发者」，在终端执行 `sudo xattr -rd com.apple.quarantine /Applications/NijiN.LiteBurstScaler.app`（或在「系统设置 → 隐私与安全性」底部点「仍要打开」）即可永久放行。
- **格式与选项自动记忆**：自动持久化保存上次使用的 RGGB / RGB 压缩格式、JPEG XS BPP、色彩空间、输出场图开关、序列策略、预设及微调旋钮，下次打开自动恢复。
- **品牌与色彩设计**：采用略灰水蓝色系卡片布局；顶栏左侧为 `NijiN.LiteBurstScaler`（`nijinLBS`），顶栏右侧并列展示 **十字共星标**（位于 `NijiN` 左侧）与 **`NijiN` 割圆术组合标**，程序应用图标采用十字共星标。
- **右侧悬停说明小点（`i`）**：各卡片右上角及 `2× RGGB`、`2× RGB` 格式行右侧设有悬停小点，移入展示精简规则与以 `未压缩 = 1.00` 为基准的完整格式倍率参考。
- **引擎与 LibRaw 路径自定义**：
  - **CLI 路径**：启动时按「已保存路径 → 同目录 → 上级目录 → 工作目录 → 系统 `PATH`」自动搜索 `nijinLBS` / `nijinLBS.exe`；未命中时弹窗引导指定路径并记忆。
  - **LibRaw**：留空使用内置静态库 `LibRaw 0.23.0-Snapshot202611`（支持 Nikon HE/HE\*），可选任意外部 `.dylib`/`.so`/`.dll` 实时覆盖解码。
