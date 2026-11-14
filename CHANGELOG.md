# Changelog

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

## 1.6.4 — LiteBurstScaler foundation

- High-ISO sparse-star detection/alignment, discrete maximum-principle ringing
  control, and updated astro tone rendering.
- Lossless output defaults and split-gray physical fields retained in Suite.
