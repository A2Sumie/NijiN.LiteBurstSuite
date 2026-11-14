# Changelog

## 2.0.1 — Pipeline reuse and performance

- Added per-stage wall/device timings and per-pass ROI work accounting.
- Indexed IBP source blocks, parallelized diagnostics, and used fixed sparse
  samples for residual telemetry while retaining all reconstruction samples.
- Matched DCT, chroma and RL work regions to their actual dependencies; enlarged
  outer tiles within device buffer and working-set limits.
- Reused sensor-coordinate residuals and added a Metal residual prepass.
- Reused the local fusion-variance map for C6 analytical fallback. Independent
  half-stack estimation remains active when its phase conditions are satisfied.
- Added separable tent reuse for dense half-stack sampling, shared nonlocal
  matches across color channels, and moved whitening normalization out of inner
  pixel/spectrum loops.
- Kept TIFF output lossless with faster Deflate; measured size increase is about
  1% on the comparison photos.
- Fixed MinGW AVX2 value-copy alignment in the full-frame tile planner.
- Fixed the Metal/Vulkan DCT shared-input halo for shifted final blocks at odd
  pyramid dimensions, preserving the existing block enumeration.
- Reduced RGB RL working memory with dependency-aware bands and delayed halo
  commits; full-image comparisons produce identical values.
- Full-resolution Photo, including lossless DNG + TIFF I/O, on M1 Max / Metal /
  6 CPU threads: Z8 5/6 frames **38.68/43.37 s**, A9 III **16.50/20.27 s**,
  X-H2S **22.48/24.91 s**. See [performance measurements](docs/PERFORMANCE.md).

## 2.0.0 — NijiN.LiteBurstSuite

- Four native workspaces with shared burst/frame inspection, AF ranking, anchor
  selection, homepage shortcuts, persistent settings, and Top-K original exports.
- Unified Bayer/X-Trans CFA and black-level handling, including X-Trans DNGs whose
  LibRaw absolute pattern table is empty.
- Shared streamed CPU/Metal/Vulkan reconstruction with directional source ROIs,
  adaptive tile splitting, global statistics, and matching kernel arithmetic.
- Exposure normalization, sensor-coordinate residual cleanup, directional frame
  weighting, RAW back-projection, half-stack/KLT shrinkage, and spatial PSF models.
- Clean plate, virtual ND, action strobe, transient highlights, reflection
  estimates, relative-depth WebGL previews, and photon-domain refocus.
- Eleven common physical fields plus advanced local normal/occlusion fields;
  named TIFF channels, grayscale PNGs, packed RGB, and dimensional metadata.
- RAW optical measurements, drift telemetry, reusable LCP models, and paired
  leave-one-out datasets with linear arrays and per-pixel sigma.
- Three-component lossless SOF3 LinearRaw DNG, corrected NoiseProfile tagging,
  RAF/CR3 metadata extraction, and robust file-write error propagation.
- Fixed IRLS residual indexing after percentile calculation, multi-group explicit
  output naming, and sigma-map display clipping.
- Preserved capture order with missing timestamps and fixed a MinGW AVX2
  aggregate-parameter alignment fault in multi-threaded sensor calibration.

## 1.6.4 — LiteBurstScaler foundation

- High-ISO sparse-star detection/alignment, discrete maximum-principle ringing
  control, and updated astro tone rendering.
- Lossless output defaults and split-gray physical fields retained in Suite.
