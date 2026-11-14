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

    run("ibp", [*base, "--ibp-iters", "1", "--out-rgb", str(output / "ibp.tif")])
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

    static = output / "static"
    static.mkdir(exist_ok=True)
    copies = [static / f"frame_{i}.dng" for i in range(8)]
    for path in copies:
        shutil.copyfile(files[0], path)
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
    if result.returncode == 3:
        assert "additional reliably camera-aligned frames required" in result.stderr
        run("transient-stationary", options, inputs=copies)
    with Image.open(output / "transient-main_transient.png") as image:
        assert image.size == (baseline.shape[1], baseline.shape[0])

    selected = output / "selected"
    run("topk", ["--export-topk", "3", "--out-dir", str(selected)])
    manifest = json.loads((selected / "selection_1.json").read_text())
    assert len(manifest["selected"]) == 3
    for item in manifest["selected"]:
        assert Path(item["source"]).read_bytes() == Path(item["output"]).read_bytes()

    run("optics", [*base, "--optics-only", "--optics-report", str(output / "optics")])
    optics = json.loads((output / "optics_telemetry.json").read_text())
    assert optics["frames"] == len(files) and len(optics["zones"]) == 5
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
    print(f"Suite CLI PASS: {len(files)} real crops, IBP delta={delta:.6f}, dataset={dataset['accepted']} tiles")


if __name__ == "__main__":
    main()
