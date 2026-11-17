# Notice & Open-Source Attributions

**[English](#notice--open-source-attributions) · [简体中文](#声明与第三方开源组件署名)**

**NijiN.LiteBurstSuite (`nijinLBS`)**
Copyright 2026 A2Sumie (NijiN Project — https://github.com/A2Sumie/NijiN.LiteBurstSuite)

All first-party source code in `NijiN.LiteBurstSuite` (`nijinLBS`) is licensed under the [**Apache License, Version 2.0 (`Apache-2.0`)**](LICENSE). Pursuant to Section 4 of the Apache License 2.0, any redistribution in source or binary form must retain the copyright notice, `LICENSE`, this `NOTICE.md` attribution, and prominent notices on modified files.

Suite 2.0 extends the LiteBurstScaler foundation with unified CFA processing,
streamed CPU/GPU reconstruction, temporal imaging, physical fields, optical
measurement, paired datasets generation, and four-workspace native interfaces.
Bundled component license texts are retained in `licenses/`.

Prebuilt binary releases constitute a Larger Work incorporating or linking against the following open-source components under their respective permissive / file-level licenses:

1. **LibRaw (`0.23.0-Snapshot` with Nikon HE/HE\* support)**
   - License: Common Development and Distribution License 1.0 (`CDDL-1.0`, Section 3.6 Larger Works)
   - Upstream source: https://www.libraw.org / https://github.com/LibRaw/LibRaw
   - Runtime override: users may supply any external `libraw_r` shared library at runtime via `--libraw <path>` (or via the `LibRaw` picker in `nijinLBS_gui`).

2. **SVT-JPEG-XS (Scalable Video Technology for JPEG XS)**
   - License: BSD-2-Clause-Patent
   - Upstream: https://github.com/OpenVisualCloud/SVT-JPEG-XS

3. **Little CMS (`lcms2`)**
   - License: MIT License
   - Upstream: https://www.littlecms.com / https://github.com/mm2/Little-CMS

4. **zlib**
   - License: zlib License
   - Upstream: https://zlib.net

5. **Khronos Vulkan Core C Header (`third_party/vulkan/vulkan_core.h`)**
   - License: Apache License 2.0
   - Upstream: https://github.com/KhronosGroup/Vulkan-Headers

## Special Acknowledgments

Software engineering and implementation for this project were led by **Google Gemini 4 (Carbon/Orchestrator Carbon)**.

The **NijiN** project also benefited from:
- **Xiaomi MiMo-2.6-Pro**
- **MoonShot Kimi-3/Kimi-3.1-Pro**
- **OpenAI GPT-6 Astra/6.1 Sol**
- **Z.ai GLM-5.3-Flash**
- **DeepSeek V4 Pro-0813**
- **Sakana.ai Fugu/Fugu-Ultra 1.0**

---

# 声明与第三方开源组件署名

**NijiN.LiteBurstSuite (`nijinLBS`)**
Copyright 2026 A2Sumie (NijiN Project — https://github.com/A2Sumie/NijiN.LiteBurstSuite)

`NijiN.LiteBurstSuite`（`nijinLBS`）中所有第一方源码均采用 [**Apache License 2.0 (`Apache-2.0`)**](LICENSE) 授权。依据 Apache License 2.0 第 4 条规定，任何源码或二进制形式的再分发必须保留原始版权声明、`LICENSE`、本 `NOTICE.md` 署名文件以及对修改文件的显著变更说明。

Suite 2.0 在 LiteBurstScaler 基础之上扩展了统一 CFA 处理、流式 CPU/GPU 重建、时域成像、物理场提取、光学计量、配对数据集与四工作区原生桌面界面；随包附带的第三方组件许可全文均保存在 `licenses/` 目录。

预编译二进制发行包作为一个组合作品（Larger Work），按各自宽松或文件级开源许可静态链接或包含了以下开源组件：

1. **LibRaw（`0.23.0-Snapshot`，支持 Nikon HE/HE\*）**
   - 许可协议：`CDDL-1.0`（第 3.6 条 Larger Works）
   - 上游源码：https://www.libraw.org / https://github.com/LibRaw/LibRaw
   - 运行时替换：用户可通过 `--libraw <path>`（或 `nijinLBS_gui` 引擎设置中的 `LibRaw` 选择器）加载任意外部 `libraw_r` 动态库。

2. **SVT-JPEG-XS (Scalable Video Technology for JPEG XS)**
   - 许可协议：BSD-2-Clause-Patent
   - 上游源码：https://github.com/OpenVisualCloud/SVT-JPEG-XS

3. **Little CMS (`lcms2`)**
   - 许可协议：MIT License
   - 上游源码：https://www.littlecms.com / https://github.com/mm2/Little-CMS

4. **zlib**
   - 许可协议：zlib License
   - 上游源码：https://zlib.net

5. **Khronos Vulkan Core C Header (`third_party/vulkan/vulkan_core.h`)**
   - 许可协议：Apache License 2.0
   - 上游源码：https://github.com/KhronosGroup/Vulkan-Headers

## 特别鸣谢

该项目的编程由 **Google Gemini 4（Carbon/Orchestrator Carbon）** 主持进行。

**NijiN** 项目同样受益于：
- **Xiaomi MiMo-2.6-Pro**
- **MoonShot Kimi-3/Kimi-3.1-Pro**
- **OpenAI GPT-6 Astra/6.1 Sol**
- **Z.ai GLM-5.3-Flash**
- **DeepSeek V4 Pro-0813**
- **Sakana.ai Fugu/Fugu-Ultra 1.0**
