# `.nlens` v1 Open Physical Optical Model Specification & Authoring Guide

`NijiN.LiteBurstSuite` (`nijinLBS`) supports a vendor-neutral, plain-JSON optical calibration format (`nijin.nlens.v1`) for deterministic distortion, lateral chromatic aberration (LCA), mechanical/microlens vignetting, complex optical transfer functions (OTF), wide-angle spherical glare skirts, and sensor coverglass thickness transfer.

Profiles placed in `~/.config/nijin/lenses/*.nlens` (macOS/Linux), `%APPDATA%\nijin\lenses\*.nlens` (Windows), `<exe_dir>/lenses/*.nlens`, `../Resources/lenses/*.nlens`, or `./lenses/*.nlens` are matched deterministically against EXIF camera body (`Make + Model` vs `target_sensor` / `mount`), `LensModel`, focal length, focus diopter, and full `[open, max]` aperture range when `--lens-profile auto` (the default) is active, or loaded explicitly via `--lens-profile <path.nlens>`. Both the compact radial `states[]` (`"schema": "nijin.nlens.v1"`) syntax and the expanded 2D `nodes[]` (`"version": 1`) syntax are supported natively, with strict calibration-hull abstention (`convex_within_calibration_hull_else_abstain`) and convention `image_x_right_y_down;fft_negative;pupil_exp_positive;lambda_vacuum_nm`.

---

## 1. Three Calibration Tiers

| Tier | `"tier"` Token | Required Data | Physical Capabilities |
|---|---|---|---|
| **Tier 1** | `"datasheet_mtf"` | Manufacturer or bench MTF curves (`10/30 lp/mm` or denser S/T curves), optional distortion & vignetting | Anisotropic sagittal/tangential amplitude OTF (`zero phase`), radial LCA & vignetting |
| **Tier 2** | `"lab_mtf_asymmetric"` | Multi-frequency S/T MTF curves + optional off-axis cubic coma phase (`coma_c31_waves`) + wide-open `spherical_skirt` | Restores off-axis comatic flare symmetry via conjugate phase $e^{-i\Phi(\boldsymbol{\omega})}$ and removes wide-skirt spherical haze without tile wrap-around |
| **Tier 3** | `"raytrace_wavefront"` | Exit-pupil position/NA, `0..2` mechanical stops (cat's-eye clipping), Zernike wavefront (`n <= 8`, in $\mu\text{m}$), design vs target coverglass thickness | Exact `128x128` 2D FFT linear pupil autocorrelation complex OTF + non-paraxial scalar angular-spectrum coverglass transfer |

---

## 2. Single-Owner Physical Factor Ledger (`includes`)

Every `.nlens` v1 file declares an `includes` object so `nijinLBS` never double-compensates pixel aperture, silicon diffusion, or lateral chromatic shift:

```json
"includes": {
  "pixel_aperture": false,
  "silicon_diffusion": false,
  "chief_ray_lca_in_warp": true,
  "geometry_tilt_removed": true
}
```

- Set `"pixel_aperture": false` when MTF values come from optical ray-tracing or pure lens-bench measurements (the engine multiplies by $\operatorname{sinc}(s\omega_x/2)\operatorname{sinc}(s\omega_y/2)$ automatically). Set `true` only if MTF was measured directly on the target sensor via raw slanted-edge eSFR without dividing out pixel pitch.
- Set `"chief_ray_lca_in_warp": true` when R/B chief-ray lateral shifts are handled by Stage 4B/M5-A CFA coordinate warping (`lca_red_ppm` / `lca_blue_ppm`), keeping OTF centered at zero linear phase tilt.

---

## 3. Compact Authoring Example (`< 55` Lines)

```json
{
  "schema": "nijin.nlens.v1",
  "tier": "datasheet_mtf",
  "authority": "manufacturer_chart",
  "lens": {
    "name": "50mm F1.4 Reference Prime",
    "mount": "Full-Frame",
    "match_aliases": ["50mm F1.4", "50mm f/1.4"],
    "focal_range_mm": [50.0, 50.0],
    "aperture_range_fnum": [1.4, 16.0],
    "image_circle_mm": 43.27
  },
  "sensor_calibration": {
    "target_sensor": "Full-Frame BSI 45.7MP",
    "pixel_pitch_um": 4.35,
    "coverglass_mm": 1.20,
    "design_coverglass_mm": 1.20,
    "coverglass_index_d": 1.5168
  },
  "includes": {
    "pixel_aperture": false,
    "silicon_diffusion": false,
    "chief_ray_lca_in_warp": true,
    "geometry_tilt_removed": true
  },
  "states": [
    {
      "focal_mm": 50.0,
      "focus_m": 1000.0,
      "aperture_fnum": 1.4,
      "exit_pupil_z_mm": 78.0,
      "radii_mm": [0.0, 5.0, 10.0, 15.0, 18.5, 21.63],
      "vignetting_ev": [0.0, -0.18, -0.55, -1.05, -1.48, -1.85],
      "distortion_pct": [0.0, -0.12, -0.45, -0.88, -1.15, -1.32],
      "lca_red_ppm": [0.0, 45.0, 95.0, 140.0, 165.0, 180.0],
      "lca_blue_ppm": [0.0, -60.0, -130.0, -195.0, -230.0, -255.0],
      "mtf_lp_mm": [10.0, 30.0],
      "mtf_s": [[0.94, 0.76], [0.93, 0.74], [0.90, 0.69], [0.86, 0.62], [0.82, 0.56], [0.77, 0.49]],
      "mtf_t": [[0.94, 0.76], [0.92, 0.72], [0.87, 0.63], [0.80, 0.52], [0.73, 0.43], [0.66, 0.35]],
      "mtf_se": 0.02
    }
  ]
}
```

---

## 4. Ready-to-Use LLM Prompt for Generating `.nlens` Profiles from Public Lens Data or Patents

Copy the prompt below into any LLM alongside manufacturer MTF charts, optical bench measurements, or patent prescription tables (e.g. from Photonstophotos Optical Bench):

```text
Convert the provided optical data (manufacturer 10/30 lp/mm S/T MTF chart, optical bench ray-trace prescription, or lab eSFR report) into a valid `nijin.nlens.v1` JSON file:
1. Set `"schema": "nijin.nlens.v1"`. Use `"tier": "raytrace_wavefront"` ONLY if exit-pupil wavefront Zernike coefficients (in micrometers) are explicitly available; use `"lab_mtf_asymmetric"` if multi-frequency MTF or off-axis coma/spherical skirt data are available; otherwise use `"datasheet_mtf"`.
2. Sample `radii_mm` from `0.0` to `21.63` (full-frame) or `14.2` (APS-C). Ensure `mtf_s[r_idx]` and `mtf_t[r_idx]` match the exact length of `mtf_lp_mm` with values strictly in `(0, 1]`.
3. If the lens is adapted across mounts with different sensor coverglass thicknesses (e.g., 2.0 mm design stack onto a 1.2 mm target body), record both `"design_coverglass_mm"` and `"coverglass_mm"` accurately under `"sensor_calibration"`.
4. Set `"includes.pixel_aperture": false` for optical MTF charts and ray-traced prescriptions; set `true` only for uncompensated sensor-level slanted-edge MTF. Never invent non-zero phase coefficients (`coma_c31_waves`) unless directional asymmetric PSF or ray-trace coma data are present.
```
