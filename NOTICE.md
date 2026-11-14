# Notice & Open-Source Attributions

**NijiN.LiteBurstSuite (`nijinLBS`)**
Copyright 2026 A2Sumie (NijiN Project — https://github.com/A2Sumie/NijiN.LiteBurstSuite)

All first-party source code in `NijiN.LiteBurstSuite` (`nijinLBS`) is licensed under the [**Apache License, Version 2.0 (`Apache-2.0`)**](LICENSE). Pursuant to Section 4 of the Apache License 2.0, any redistribution in source or binary form must retain the copyright notice, `LICENSE`, this `NOTICE.md` attribution, and prominent notices on modified files.

Suite 2.0 extends the LiteBurstScaler foundation with unified CFA processing,
streamed CPU/GPU reconstruction, temporal imaging, physical fields, optical
measurement, paired datasets, and four-workspace native interfaces.
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
