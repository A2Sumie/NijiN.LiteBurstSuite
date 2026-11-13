# NijiN.LiteBurstScaler (`nijinLBS`)

**Fast Multi-Frame 2× RAW & RGB Super-Resolution Upscaler** (`Metal` · `Vulkan` · `ARM64 NEON` · `x86_64 AVX2`)

`NijiN.LiteBurstScaler`（简称 **`nijinLBS`**）是一款零外部运行库依赖的跨平台相机多帧 **2× 超分辨率**合成引擎与原生轻量桌面端 GUI（macOS `AppKit` + Windows `Win32 GDI+`），专为高像素无低通机身（原生内置 `LibRaw 0.23-HE*`，开箱直读 **Nikon Z 8 / Z 9 高效压缩 HE / HE\*** 及主流相机 `.NEF / .DNG / .ARW / .CR3 / .RAF / .RW2`）打造。

<p align="center">
  <img src="assets/ui_macos.png" width="49%" alt="macOS Native AppKit UI" />
  <img src="assets/ui_windows.png" width="49%" alt="Windows Native Modern Win32 UI" />
</p>

---

## 核心特性 / Key Features

- **零依赖跨平台 GPU + SIMD 加速（`--backend auto|gpu|cpu`）**
  - **macOS (`Metal`)**：原生调用 `Metal.framework` 统一内存零拷贝计算；
  - **Windows (`Vulkan`)**：内嵌 24 个预编译 Vulkan 1.0 `SPIR-V` 计算着色器，运行时动态加载显卡驱动自带 `vulkan-1.dll`（NVIDIA / AMD / Intel 通用，无需安装额外 SDK 或运行库）。在 `RTX 4080 SUPER` 上，7 张 45.7 MP Nikon Z 8 HE\* 合成 **1.83 亿像素（`16560×11040`）** 16-bit 无损 DNG 端到端总耗时约 **`9.0s`**（Stage 4+5 GPU 核耗时 **`1.9s`**）；
  - 无可用 GPU 环境自动无缝回退至纯 CPU `ARM64 NEON` / `x86_64 AVX2` 管线。
- **四档场景预设（`Photo 照片` / `Sci 科学` / `Arc 归档` / `Astro 星空`）**
  - **`Photo`**：跨帧时域梯度相干保微纹理 + 三门控刚体微块超分 + 光学 PSF Richardson-Lucy 反卷积 + 自适应影调；
  - **`Sci`**：严格辐射度线性真值输出（关闭 PSF 反卷积、自动曝光、高光滚降与边缘抛光），适合科研测量与深度学习监督训练；
  - **`Arc`**：中性温和降噪与长期数字底片母版留存；
  - **`Astro`**：专为**免赤道仪、免脚架手持打星 / 手持星野**设计——跨通道拜耳邻域“防吞星”门控、恒星黑体与星云色彩保护、星点质心 6 项二阶多项式 + 天际线**天地双域独立对齐**、跨全感光度（`ISO 64–102400`）盖镜头黑场自动跳过，以及无黑环星形 RL 与夜空黑位锁定 `asinh` 柔和拉伸。
- **动静耦合连续场与 16-bit 三通道物理场图（`--out-field` / `--emit-field`）**
  - 防镂空形态学鬼影闭包 + Hamilton-Adams 二阶跨通道方向插值 + 光学对焦场 RL 门控，防止背景锐度反超运动主体或边缘产生贴片脱离感；
  - 可按需导出与主图同尺寸像素级对齐的 16-bit PNG 物理场图（`R` = 运动掩膜 / 星野天际线蒙版，`G` = `2×2` 亚像素相位置信度，`B` = 二阶拉普拉斯光学对焦锐度场）。
- **五维智能向导（CLI `--wizard` / GUI「智能向导」）**
  - 毫秒级识别「手持星野 / 纯星空 / AEB 包围曝光 / 三脚架稳态 / 局部动体 / 高感弱光」，给出亚像素象限覆盖评分、皇冠最锐帧与参数处方，支持一键采纳或任选第 `#1..N` 张作为运动/地景定格帧。
- **多格式无损 / 高级有损输出与完整 EXIF/GPS 继承**
  - **2× RGGB 线性 DNG 1.4**：16-bit 无损 `SOF3`（`0.39×`）、高级有损压缩（`0.09×`）、16-bit 半精度浮点 HDR（`0.29×`）、16-bit 未压缩（`1.00×`）；
  - **2× 渲染 RGB（`sRGB` / 内嵌 ICC v2.4 `Adobe RGB`）**：16-bit 无损 PNG、14-bit **JPEG XS**（`.jxs` 高级有损 / 无损，附带 `--decode-jxs` 回转工具）、16-bit Deflate / 未压缩 TIFF。

---

## 快速上手 / Quick Start

### 1. 开箱即用（Releases 预编译包）
前往 [**Releases**](../../releases) 下载对应平台安装包：

- **macOS (Apple Silicon `arm64` — `.dmg` 镜像安装包)**：
  1. 双击打开 `nijinLBS-v1.6.1-macOS-arm64.dmg`，将 **`NijiN.LiteBurstScaler.app`** 拖入 **`Applications`（应用程序）**（内置完整 GUI 与 CLI，支持直接将 RAW 文件/目录拖到应用图标或窗口上）；
  2. **macOS 首次打开“解冻”（解除浏览器下载隔离属性 / Gatekeeper 拦截）**：由于应用采用本地 Ad-hoc 签名而未挂载付费 Apple Developer ID 公证，从浏览器下载首次打开若提示「已损坏，无法打开」或「无法验证开发者」，任选以下一种方式即可永久放行：
     - **方式 A（终端一行命令解冻，最快最彻底）**：打开「终端 (Terminal)」执行：
       ```bash
       sudo xattr -rd com.apple.quarantine /Applications/NijiN.LiteBurstScaler.app
       ```
       *(若放在其他目录，输入 `xattr -rd com.apple.quarantine ` 后将 `.app` 拖入终端回车即可；若单独下载 CLI 压缩包则执行 `xattr -rd com.apple.quarantine ./nijinLBS-macOS-cli`)*
     - **方式 B（系统设置图形界面放行）**：首次点击打开被拦截后，进入 **「系统设置 → 隐私与安全性」**，滑到底部点击 **「仍要打开 (Open Anyway)」** 并确认。
  3. 终端直接调用内置 CLI：`/Applications/NijiN.LiteBurstScaler.app/Contents/MacOS/nijinLBS --help`（Releases 同时提供独立 CLI 压缩包）。
- **Windows (`x64`)**：解压 `nijinLBS-v1.6.1-Windows-x64.zip` 到任意目录，双击 **`nijinLBS_gui.exe`** 启动图形界面，或终端调用 `nijinLBS.exe`。
- **偏好自动记忆**：双端 GUI 自动记忆上次选择的 **RGGB / RGB 压缩格式、JPEG XS BPP、色彩空间、输出场图开关、序列策略、场景预设与微调参数**，下次启动即刻恢复。

### 2. 常用命令行示例
```bash
# 1) 日常手持连拍：同时输出 2x 16-bit 无损 DNG 与 16-bit 无损 PNG
./nijinLBS --out-raw out_2x.dng --out-rgb out_2x.png Z8S_8551.NEF Z8S_8552.NEF Z8S_8553.NEF Z8S_8554.NEF Z8S_8555.NEF Z8S_8556.NEF

# 2) 免脚架手持打星 / 星野：启动五维智能向导并导出 16-bit 物理场图
./nijinLBS --wizard --out-raw astro_2x.dng --out-rgb astro_2x.png --out-field astro_field.png Z8S_8787.NEF Z8S_8788.NEF Z8S_8789.NEF Z8S_8790.NEF Z8S_8791.NEF Z8S_8792.NEF

# 3) 整卡目录批量扫描：自动按 EXIF 时间分簇、跳过黑场、输出 Adobe RGB 14-bit JPEG XS
./nijinLBS --scan-dir /Volumes/NIKON_Z8/DCIM/116NCZ_8 --policy multi-group --out-dir ./sr_out --emit-rgb --rgb-format jxs --jxs-bpp 10.0 --color-space adobergb
```

完整参数表、收敛曲线与场图说明详见 [**`CLI.md`**](CLI.md)。

---

## 从源码构建 / Building from Source

```bash
# 重新编译 24 个 Vulkan SPIR-V 计算着色器（仅修改 shaders/build_shaders.py 时需要，需 glslc）
python3 shaders/build_shaders.py

# 构建 macOS 原生 Metal CLI + Cocoa GUI（传入静态 LibRaw 与 SVT-JPEG-XS 目录）
make nijinLBS nijinLBS_gui LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# 打包 macOS 原生 NijiN.LiteBurstScaler.app 与 .dmg 安装镜像
make dmg LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs

# 交叉编译 Windows x64 静态 CLI + 现代 GDI+ Win32 GUI（需 mingw-w64）
make nijinLBS.exe nijinLBS_gui.exe LIBRAW_DIR=/path/to/libraw-heraw SVT_DIR=/path/to/svt-jpeg-xs
```

开源许可：本项目自有源码采用 [**Apache License 2.0 (`Apache-2.0`)**](LICENSE) 授权（允许自由商用与修改，须保留原作者署名、`LICENSE`、`NOTICE.md` 及修改说明）；预编译二进制涉及的第三方组件声明见 [**`NOTICE.md`**](NOTICE.md)。
