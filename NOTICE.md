# License & Third-Party Open-Source Notices

All first-party source code in `NijiN.LiteBurstScaler` (`nijinLBS`) is released under the [**BSD Zero Clause License (`0BSD`)**](LICENSE) — an OSI-approved public-domain-equivalent license with zero attribution or redistribution conditions.

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
