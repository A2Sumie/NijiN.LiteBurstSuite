#!/usr/bin/env python3
"""Exercise the Suite CLI on CFA-preserving real RAW crops."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

import numpy as np
from PIL import Image
import tifffile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("corpus", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    files = sorted(args.corpus.resolve().glob("*.dng"))
    assert len(files) >= 6

    def run(name: str, options: list[str], inputs=None, expected=0):
        command = [str(binary), *options, *map(str, files if inputs is None else inputs)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        (output / f"{name}.stdout").write_text(result.stdout)
        (output / f"{name}.stderr").write_text(result.stderr)
        allowed = (expected,) if isinstance(expected, int) else expected
        assert result.returncode in allowed, (name, result.returncode, result.stderr[-5000:])
        assert "runtime error:" not in result.stderr and "AddressSanitizer" not in result.stderr
        return result

    plan = json.loads(run("plan", ["--plan-json", "--threads", "2"]).stdout)
    assert sum(len(c["frames"]) for c in plan["clusters"]) == len(files)
    assert all("af_score" in f and Path(f["path"]).exists() for c in plan["clusters"] for f in c["frames"])
    run("bad-field", ["--fields", "made-up"], expected=2)
    run("bad-cpu-cores", ["--cpu-cores", "made-up", "--plan-json"], expected=2)
    run("bad-battery-device", ["--battery-device", "made-up", "--plan-json"], expected=2)
    run("bad-priority", ["--priority", "made-up", "--plan-json"], expected=2)
    run("bad-simd", ["--simd", "made-up", "--plan-json"], expected=2)
    run("bad-no-tool-exif", ["--no-tool-exif", "made-up", "--plan-json"], expected=2)
    run("skip-anchor", ["--skip-frames", "1", "--anchor-frame", "1", "--plan-json"], expected=2)

    base = ["--backend", "cpu", "--threads", "2", "--policy", "force-all",
            "--anchor-frame", "first", "--preset", "sci", "--denoise-strength", "0",
            "--chroma-denoise", "0", "--psf-iters", "0", "--rgb-psf-iters", "0",
            "--silicon-clean", "off", "--auto-lca", "off", "--dither", "off"]
    rgb = output / "rgb.tif"
    run("base", [*base, "--ibp-iters", "0", "--out-rgb", str(rgb),
                 "--out-raw", str(output / "linear.dng"), "--raw-format", "dng-linear"])
    baseline = tifffile.imread(rgb)
    assert baseline.ndim == 3 and baseline.shape[-1] == 3
    assert baseline.dtype == np.uint16 and np.std(baseline) > 10
    with tifffile.TiffFile(output / "linear.dng") as dng:
        page = dng.pages[0]
        assert page.photometric == 34892 and page.samplesperpixel == 3
        assert page.compression == 7 and 51041 in page.tags

    run("ibp", [*base, "--cpu-cores", "perf", "--priority", "below", "--simd", "avx2",
                "--ibp-iters", "1", "--out-rgb", str(output / "ibp.tif")])
    assert "cores=perf prio=below" in (output / "ibp.stderr").read_text()
    ibp = tifffile.imread(output / "ibp.tif")
    delta = np.mean(np.abs(ibp.astype(float) - baseline.astype(float)))
    assert delta > 0, "IBP must change this textured real sample"

    run("fields", [*base, "--ibp-iters", "0", "--out-rgb", str(output / "fields.tif"),
                   "--out-field", str(output / "field"), "--field-mode", "tiff-alpha",
                   "--fields", "motion,phase,focus,sigma"])
    with tifffile.TiffFile(output / "fields.tif") as field_tiff:
        page = field_tiff.pages[0]
        assert page.samplesperpixel == 7 and len(page.extrasamples) == 4
        assert all(int(v) == 0 for v in page.extrasamples)
    run("sigma", [*base, "--ibp-iters", "0", "--out-field", str(output / "sigma"),
                  "--fields", "sigma"])
    with Image.open(output / "sigma_sigma.png") as image:
        sigma_codes = np.asarray(image)
    sigma_meta = json.loads((output / "sigma_fields.json").read_text())
    sigma_scale = next(f["display_scale"] for f in sigma_meta["fields"] if f["name"] == "sigma")
    assert sigma_scale > 0 and np.mean(sigma_codes == 65535) < 0.01
    assert np.std(sigma_codes.astype(float) * sigma_scale / 65535) > 1e-8
    run("packed-order", [*base, "--ibp-iters", "0", "--out-field", str(output / "packed"),
                         "--field-mode", "rgb", "--fields", "focus,motion,phase"])
    run("split-order", [*base, "--ibp-iters", "0", "--out-field", str(output / "split"),
                        "--field-mode", "split", "--fields", "focus,motion,phase"])
    with Image.open(output / "packed.png") as image:
        packed = np.asarray(image).astype(int)
    for channel, name in enumerate(["focus", "motion", "phase"]):
        with Image.open(output / f"split_{name}.png") as image:
            gray = np.asarray(image).astype(int)
        assert np.max(np.abs(packed[:, :, channel] - (gray >> 8))) <= 1

    static = output / "static"
    static.mkdir(exist_ok=True)
    copies = [static / f"frame_{i}.dng" for i in range(8)]
    for path in copies:
        shutil.copyfile(files[0], path)
    ninth = static / "frame_8.dng"
    shutil.copyfile(files[0], ninth)
    grouping_inputs = [*copies, ninth]
    auto9 = json.loads(run("group-auto-9", ["--plan-json", "--policy", "multi-group", "--group-size", "8"],
                           inputs=grouping_inputs).stdout)
    assert auto9["total_groups"] == 1
    tenth = static / "frame_9.dng"
    shutil.copyfile(files[0], tenth)
    auto10 = json.loads(run("group-auto-10", ["--plan-json", "--policy", "multi-group", "--group-size", "8"],
                            inputs=[*grouping_inputs, tenth]).stdout)
    assert auto10["total_groups"] == 2
    single = json.loads(run("group-single", ["--plan-json", "--policy", "force-all", "--single-group",
                                             "--out-dir", str(output / "group-single")],
                            inputs=grouping_inputs).stdout)
    assert single["total_groups"] == 1 and len(single["clusters"]) == 1
    assert len(single["clusters"][0]["frames"]) == 9
    assert {frame["group"] for frame in single["clusters"][0]["frames"]} == {0}
    for mode in ["clean-plate", "virtual-nd", "strobe"]:
        (output / f"{mode}.tif").unlink(missing_ok=True)
        options = [*base, "--ibp-iters", "0", "--motion-mode", mode,
                   "--out-rgb", str(output / f"{mode}.tif")]
        result = run(mode, options, expected=(0, 3))
        if result.returncode == 3:
            # A tight crop of a long-lens pan can lose the shared camera view.
            assert "additional reliably camera-aligned frames required" in result.stderr
            assert not (output / f"{mode}.tif").exists()
            run(f"{mode}-stationary", options, inputs=copies)
        result = tifffile.imread(output / f"{mode}.tif")
        assert result.shape == baseline.shape and np.std(result) > 10
    options = [*base, "--ibp-iters", "0", "--emit-transient",
               "--out-rgb", str(output / "transient-main.tif")]
    result = run("transient", options, expected=(0, 3))
    if result.returncode == 3 or not (output / "transient-main_transient.png").exists():
        assert "additional reliably camera-aligned frames required" in result.stderr
        run("transient-stationary", options, inputs=copies)
    with Image.open(output / "transient-main_transient.png") as image:
        assert image.size == (baseline.shape[1], baseline.shape[0])

    # Verify 2x >= 5 suitability gate vs 1x >= 3 True-Sampled RGB + tiff-hq + --log-file
    run("2x-4frame-gate", ["--backend", "cpu", "--threads", "2", "--policy", "auto-prune",
                           "--out-rgb", str(output / "gate4.tif")], inputs=copies[:4], expected=3)
    log_path = output / "run_1x.log"
    rgb_1x_lossless = output / "rgb_1x_lossless.tif"
    rgb_1x_hq = output / "rgb_1x_hq.tif"
    run("1x-3frame-lossless", ["--backend", "cpu", "--threads", "2", "--policy", "auto-prune",
                               "--rgb-scale", "1x", "--rgb-format", "tiff-deflate",
                               "--log-file", str(log_path), "--out-rgb", str(rgb_1x_lossless)],
        inputs=copies[:3])
    log_txt = log_path.read_text(encoding="utf-8")
    assert "[Cache]" in log_txt and "[4/6]" in log_txt and "=== Exit Code: 0 ===" in log_txt
    img_1x = tifffile.imread(rgb_1x_lossless)
    assert img_1x.shape == (baseline.shape[0] // 2, baseline.shape[1] // 2, 3)
    assert img_1x.dtype == np.uint16 and np.std(img_1x) > 10

    run("1x-3frame-hq", ["--backend", "cpu", "--threads", "2", "--policy", "auto-prune",
                         "--rgb-scale", "1x", "--rgb-format", "tiff-hq",
                         "--out-rgb", str(rgb_1x_hq)], inputs=copies[:3])
    img_1x_hq = tifffile.imread(rgb_1x_hq)
    assert img_1x_hq.shape == img_1x.shape
    mse_hq = float(np.mean((img_1x.astype(np.float64) - img_1x_hq.astype(np.float64)) ** 2))
    assert mse_hq == 0.0 or (10.0 * np.log10((65535.0 ** 2) / mse_hq)) > 58.0
    assert rgb_1x_hq.stat().st_size < rgb_1x_lossless.stat().st_size

    both_2x = output / "both.tif"
    both_1x = output / "both_1x.tif"
    run("scale-both", [*base, "--rgb-scale", "both", "--rgb-format", "tiff-hq",
                       "--out-rgb", str(both_2x)], inputs=copies[:5])
    assert tifffile.imread(both_2x).shape == baseline.shape
    assert tifffile.imread(both_1x).shape == img_1x.shape

    selected = output / "selected"
    run("topk", ["--export-topk", "3", "--out-dir", str(selected)])
    manifest = json.loads((selected / "selection_1.json").read_text())
    assert len(manifest["selected"]) == 3
    for item in manifest["selected"]:
        assert Path(item["source"]).read_bytes() == Path(item["output"]).read_bytes()

    run("optics", [*base, "--optics-only", "--optics-report", str(output / "optics")])
    optics = json.loads((output / "optics_telemetry.json").read_text())
    assert optics["frames"] == len(files) and len(optics["zones"]) == 5
    for k in ("depth_sign_calibrated", "depth_sign_source", "depth_metric_calibrated",
              "estimated_baseline_mm", "diopter_span_inv_m", "focus_plane_depth"):
        assert k in optics, k
    fstack_gate = run("focus-stack-gate", [*base, "--motion-mode", "focus-stack",
                                           "--out-rgb", str(output / "fstack_blocked.tif")], expected=(0, 3))
    if fstack_gate.returncode == 3:
        assert "focus stacking requires" in fstack_gate.stderr
    with Image.open(output / "optics_passport.png") as image:
        assert image.size == (1200, 680)

    run("dataset", [*base, "--ibp-iters", "0", "--export-dataset-tiles", str(output / "dataset"),
                    "--tile-size", "256", "--tile-min-conf", "0"], inputs=copies)
    dataset = json.loads((output / "dataset" / "dataset_manifest.json").read_text())
    assert dataset["accepted"] > 0
    archive_path = output / "dataset" / dataset["tiles"][0]["archive"]
    with np.load(archive_path) as archive:
        assert archive["raw1x"].shape == (128, 128)
        assert archive["gt2x"].shape == (256, 256, 3)
        assert archive["sigma2x"].shape == (256, 256)
        assert np.isfinite(archive["gt2x"]).all()
    grouped = run("multi-output", [*base, "--ibp-iters", "0", "--policy", "multi-group",
                                  "--group-size", "4", "--out-dir", str(output / "groups"),
                                  "--out-raw", str(output / "shared.dng"), "--raw-format", "dng-raw",
                                  "--progress-json"], inputs=copies)
    done = [json.loads(line) for line in grouped.stderr.splitlines() if line.startswith("{")]
    paths = [event["out_raw"] for event in done if event.get("event") == "group_done"]
    assert len(paths) == 2 and len(set(paths)) == 2
    assert all(Path(path).is_file() for path in paths)

    # Verify --no-tool-exif causal delta across DNG, TIFF, and PNG while preserving camera ExifIFD
    run("exif-def", [*base, "--ibp-iters", "0", "--out-raw", str(output / "exif_def.dng"),
                     "--raw-format", "dng-raw", "--out-rgb", str(output / "exif_def.tif"),
                     "--rgb-format", "tiff-hq"])
    run("exif-def-png", [*base, "--ibp-iters", "0", "--out-rgb", str(output / "exif_def.png"),
                         "--rgb-format", "png"])
    run("exif-clean", [*base, "--ibp-iters", "0", "--no-tool-exif", "on",
                       "--out-raw", str(output / "exif_clean.dng"), "--raw-format", "dng-raw",
                       "--out-rgb", str(output / "exif_clean.tif"), "--rgb-format", "tiff-hq"])
    run("exif-clean-png", [*base, "--ibp-iters", "0", "--no-tool-exif", "on",
                           "--out-rgb", str(output / "exif_clean.png"), "--rgb-format", "png"])
    for ext in ("dng", "tif"):
        with tifffile.TiffFile(output / f"exif_def.{ext}") as td, tifffile.TiffFile(output / f"exif_clean.{ext}") as tc:
            td_tags, tc_tags = td.pages[0].tags, tc.pages[0].tags
            assert 11 in td_tags and "NijiN" in str(td_tags[11].value)
            assert 305 in td_tags and "NijiN" in str(td_tags[305].value)
            assert 11 not in tc_tags
            if 305 in tc_tags:
                assert "NijiN" not in str(tc_tags[305].value) and "nijinLBS" not in str(tc_tags[305].value)
            assert 34665 in tc_tags and td_tags[34665].value == tc_tags[34665].value
        clean_bytes = (output / f"exif_clean.{ext}").read_bytes()
        for tok in (b"NijiN", b"nijinLBS", b"LiteBurst"):
            assert tok not in clean_bytes
    assert b"NijiN" in (output / "exif_def.png").read_bytes()
    clean_png_bytes = (output / "exif_clean.png").read_bytes()
    assert b"eXIf" in clean_png_bytes
    for tok in (b"NijiN", b"nijinLBS", b"LiteBurst"):
        assert tok not in clean_png_bytes

    print(f"Suite CLI PASS: {len(files)} real crops, IBP delta={delta:.6f}, dataset={dataset['accepted']} tiles")


if __name__ == "__main__":
    main()
