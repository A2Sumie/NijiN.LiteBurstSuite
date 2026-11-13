# Third-Party Open-Source Notices

`NijiN.LiteBurstScaler` (`nijinLBS`) incorporates or links against the following open-source components in its prebuilt binary releases:

1. **LibRaw (`0.23.0-Snapshot` with Nikon HE/HE\* support)**
   - License: Common Development and Distribution License 1.0 (`CDDL-1.0`) or GNU Lesser General Public License v2.1 (`LGPL-2.1`)
   - Upstream: https://www.libraw.org / https://github.com/LibRaw/LibRaw
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
