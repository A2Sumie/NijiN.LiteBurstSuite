# AVX-512 Archive Note

**[English](#avx-512-archive-note) · [简体中文](#avx-512-归档说明)**

x86_64 builds standardize on **256-bit AVX2 + FMA** (`HERAW_AVX512_ENABLED == 0` by default).

## Rationale
- Live benchmarks on **AMD Ryzen AI 9 HX 365** (`16Plus`, Strix Point double-pumped 256-bit AVX-512) measured `17.77 s` with AVX2 vs `17.82 s` with AVX-512 on the full 6x45.7 MP Nikon Z8 -> `16560x11040` (`182.8 MP`) pipeline, with byte-identical lossless SOF3 DNG and `RMSE = 0.0` across 548,467,200 16-bit RGB samples.
- Archiving AVX-512 removes cross-target function-call / Win64 `XMM6..XMM15` stack-spill surface while retaining every kernel (`heraw_suite_core.hpp`, `heraw_cpu_stage45.hpp`, `heraw_sr2x.cpp`, `heraw_exif_io.hpp`) intact behind `-DHERAW_ENABLE_ARCHIVED_AVX512` for future native 512-bit desktop/workstation evaluation.

---

# AVX-512 归档说明

x86_64 发布构建默认统一采用 **256-bit AVX2 + FMA**（`HERAW_AVX512_ENABLED == 0`）。

## 归档依据
- 在 **AMD Ryzen AI 9 HX 365**（`16Plus`，Strix Point 双泵 256-bit AVX-512）全尺寸 6 帧 45.7 MP Nikon Z8 → `16560×11040`（`182.8 MP`）端到端测试中，AVX2 耗时 **`17.77 s`**，AVX-512 耗时 **`17.82 s`**；二者产出的无损 `SOF3` DNG 字节级全等，548,467,200 个 16-bit RGB 样本差异 `RMSE = 0.0`。
- 归档 AVX-512 可移除跨平台调参及 Win64 `XMM6..XMM15` 非易失寄存器栈溢出风险面，同时完整保留 `heraw_suite_core.hpp`、`heraw_cpu_stage45.hpp`、`heraw_sr2x.cpp`、`heraw_exif_io.hpp` 内全部 AVX-512 内核于 `-DHERAW_ENABLE_ARCHIVED_AVX512` 开关之后，供未来原生 512-bit 桌面/工作站平台按需启用。
