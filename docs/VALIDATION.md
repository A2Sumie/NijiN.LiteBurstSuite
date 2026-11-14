# Suite 2.0.0 验收记录

日期：2026-11-14。环境：Apple M1 Max / macOS arm64；Windows X570 / RTX 4080 SUPER。

默认流程的同输入速度对照、优化及预算修订见 [性能回归分析](PERFORMANCE.md)。下表保留 v2.0.0 发布验收数据。

## 原生入口与实样

| 项目 | 观测结果 |
|---|---|
| macOS CLI / AppKit GUI | C++17 严格构建、偏好、Cmd-Q、四页截图通过 |
| Windows CLI / Win32 GUI | MinGW 严格构建、X570 原生 prefs/关闭/遥测解析/四页截图通过 |
| ASan / UBSan | A9 III、X-H2S 各 8 张真实 CFA 裁片的完整 CLI 工作流通过 |
| 全幅 A9 III | 12 帧选 4，输出 **12024×8040** DNG/TIFF；端到端记录 **113.19 s** |
| 全幅 X-H2S | 4 帧输出 **12528×8352** 三分量 SOF3 LinearRaw/TIFF/sigma；记录 **70.28 s** |
| Z8 HE* 星空 | ISO 25600，4 帧，检测到 10 个候选星点；输出 **16560×11040** DNG；记录 **99.91 s** |
| 独立读取 | LibRaw/rawpy、ExifTool、tifffile、NumPy/ZIP、Pillow、XML reader |

耗时对应当次机器负载、帧选择、输出格式及启用功能。大漂移 A9 III 的流式验收保持输入 ROI 预算 **5,308,416 pixels**，实测最大 **4,825,600 pixels**、35 tiles；独立记录的整进程峰值 RSS 为 **6.67 GB**。全幅画布保留 float32。

## 数值一致性

真实四帧裁片、`sci`、空间降噪/RL/IBP 关闭、dither 关闭：

| 输入 | Windows CPU → Vulkan | Windows CPU → macOS Metal |
|---|---:|---:|
| A9 III | 125.05 dB | 125.58 dB |
| X-H2S | 126.12 dB | 126.98 dB |

默认 Photo 处理、dither 关闭，本机 CPU → Metal：A9 III **104.93 dB**，X-H2S **111.22 dB**。

同一后端的整图/分块测试覆盖 Bayer、X-Trans、横纵接缝、常量、零权重、失败回退和真实 RAW ROI。跨平台配准回归直接比较匹配点、flow、权重与噪声参数。

## 功能与反例

- IBP 在真实裁片产生前向残差改善和可测输出变化。
- 空镜包括参考帧前景去除；ND、分身、彩色瞬态层和相对深度/散景使用可控真值及实样测试。
- 4 个命名附加通道与 11 通道边界样本经独立 TIFF reader 读取。
- 配对数据集的受控静态场景生成 **9 个目标块**；文件集合 conservation 检查通过。
- Top-K 拷贝保留原始文件内容；skip/anchor 重映射与手工重排结果一致。
- 相同/缺失时间戳下，anchor-first 排列通过原始索引保序；非默认字段顺序的 packed RGB 与逐场灰度值对照通过。
- 同时使用 `--out-dir` 和显式路径时，两组输出使用不同全局组号。
- sigma 改用实际有限最大值定标；真实 A9 III/X-H2S 复跑的截顶比例均为 **0**。
- relay 的 RAW-only 与 RAW+RGB 输出，RAW 像素最大差 **0**。
- GUI 重对焦偏移 `-1/0/+1` 对应 CLI 平面 `0/.5/1`；相对散景强度 `1` 对应 12 个输出像素。
- Windows 默认 Photo/C13、多线程、锚点 #3、频闪：修正 MinGW AVX2 的 32-byte Noise 值参数临时副本对齐后，原触发命令连续 5 次通过；统计结果与 macOS 一致。

## 标准检查

- C++17 `-Wall -Wextra -Werror`；36 个 CLI/GPU/算法源文件 ASCII 检查。
- Vulkan 24 个 kernels：glslc、glslang、spirv-val。
- workspace freshness、isolated entry、conservation、numerical 检查。
- Python syntax/lint；原生平台/ARC 参数下的 clang-analyzer-core 和指定 bugprone 项为 0 条正确性诊断。
- 通用跨项目 clang-tidy 包含大量命名、括号、C-array 和 vendor 风格规则；验收沿项目原生编译、sanitizer、跨后端数值和实样路径执行。

## 复现

```sh
make nijinLBS nijinLBS_gui
make asan
python3 tests/make_raw_crops.py /path/to/a9iii-or-fuji-burst /path/to/crops
python3 tests/test_suite_cli.py ./nijinLBS_asan /path/to/crops /path/to/results
```

独立模块测试位于 `tests/`；CPU/Metal 同 job 对照由 `test_suite_cpu.cpp` 的 `CPU_TEST_METAL` 路径提供。光学数值、导出全码值、物理场及分块边界分别由对应模块测试覆盖。
