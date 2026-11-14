"""Native GUI contract, process lifecycle and four-workspace smoke tests."""
import argparse
import json
import pathlib
import platform
import struct
import subprocess


def run_gui(binary, *args):
    result = subprocess.run(
        [str(binary), *map(str, args)],
        capture_output=True, text=True, timeout=30, check=True,
    )
    print(result.stdout.strip() or f"PASS {' '.join(map(str, args))}")
    return result.stdout


def check_optics(binary, path):
    report = json.loads(path.read_text(encoding="utf-8"))
    output = run_gui(binary, "--test-optics", path)
    line = next(line for line in output.splitlines() if line.startswith("OPTICS_JSON:"))
    actual = json.loads(line.partition(":")[2])
    zones = {zone["name"]: zone for zone in report["zones"]}
    expected = [
        zones.get(name, {}).get("mtf50", {}).get("value")
        for name in ("center", "top_left", "top_right", "bottom_left", "bottom_right")
    ]
    assert actual["loaded"] and actual["mtf50"] == expected, actual
    assert actual["phase_quadrants"] == report["phase_quadrants"], actual
    expected_drift = [
        {"dx": frame["dx_px"]["value"], "dy": frame["dy_px"]["value"],
         "time": frame["time_seconds"]["value"]}
        for frame in report["drift"]
    ]
    assert actual["drift"] == expected_drift, actual
    channels = {channel["channel"]: channel for channel in report["lca"]["channels"]}
    assert actual["lca_valid"] == [
        isinstance(channels.get(color, {}).get("coefficients"), list)
        for color in ("red", "blue")
    ], actual
    passport = path.with_name(path.name.removesuffix("_telemetry.json") + "_passport.png")
    assert actual["passport"] == passport.is_file(), actual
    observability = report.get("field_observability", {})
    depth = observability.get("depth", report.get("depth_observable"))
    assert actual["field_observability"] == observability, actual
    assert actual["depth_observable"] == depth, actual
    assert actual["depth_enabled"] == (depth is not False), actual
    assert actual["parallax_measured"] == ("max_parallax_2x_px" in report), actual
    assert actual["max_parallax_2x_px"] == report.get("max_parallax_2x_px"), actual
    assert all(enabled == observability.get(name, True)
               for name, enabled in actual["field_enabled"].items()), actual
    silicon = {name: report.get(name)
               for name in ("sigma0", "gain_g", "sensor_temp_c", "warm_pixels")}
    assert actual["silicon"] == silicon, actual
    assert actual["silicon_text"].count("--") == sum(value is None for value in silicon.values()), actual
    assert "-999" not in actual["silicon_text"], actual


def check_metric_fixture(binary, output):
    def metric(value):
        return {"value": value, "status": "measured" if value is not None else "insufficient_samples",
                "samples": 12 if value is not None else 0}

    report = {
        "schema": "org.nijin.optics.v1", "width": 100, "height": 80, "frames": 2,
        "status": "analyzed", "ois": True, "phase_quadrants": [2, 0, 0, 3],
        "drift_span_px": metric(1.5),
        "zones": [
            {"name": "top_right", "status": "far_field_estimate", "mtf50": metric(0.2),
             "fwhm_major_px": metric(2.0), "fwhm_minor_px": metric(1.0)},
            {"name": "center", "status": "far_field_estimate", "mtf50": metric(0.3)},
            {"name": "top_left", "status": "five_gate_rejection", "mtf50": metric(None)},
        ],
        "drift": [
            {"frame": 0, "dx_px": metric(0.0), "dy_px": metric(0.0), "time_seconds": metric(None)},
            {"frame": 1, "dx_px": metric(1.25), "dy_px": metric(-0.5), "time_seconds": metric(0.01)},
        ],
        "lca": {"channels": [
            {"channel": "red", "status": "measured_edge_normal_fit", "coefficients": [1, 0, 0, 0]},
            {"channel": "blue", "status": "insufficient_edges", "coefficients": None},
        ]},
    }
    path = output.resolve() / "光学 Metric_telemetry.json"
    path.write_text(json.dumps(report, ensure_ascii=False), encoding="utf-8")
    check_optics(binary, path)
    for name, parallax, depth in (("blocked", 14.0, False), ("ready", 0.5, True)):
        report.update(
            max_parallax_2x_px=parallax,
            depth_observable=depth,
            field_observability={
                "depth": depth, "defocus": False, "turbulence": True,
                "flicker": False, "coherence": True, "velocity": True,
            },
        )
        path = output.resolve() / f"conditions-{name}_telemetry.json"
        path.write_text(json.dumps(report, ensure_ascii=False), encoding="utf-8")
        check_optics(binary, path)
    for name, sigma, gain, temperature, warm in (
        ("null", 0.00042, 0.00018, None, None),
        ("zero", 0.00042, 0.00018, 0, 0),
        ("partial", None, 0, None, None),
    ):
        report.update(sigma0=sigma, gain_g=gain, sensor_temp_c=temperature, warm_pixels=warm)
        path = output.resolve() / f"silicon-{name}_telemetry.json"
        path.write_text(json.dumps(report, ensure_ascii=False), encoding="utf-8")
        check_optics(binary, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--sample", type=pathlib.Path)
    parser.add_argument("--optics-report", type=pathlib.Path)
    parser.add_argument("--delta-only", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    binary = (args.binary or root / (
        "nijinLBS_gui.exe" if platform.system() == "Windows" else "nijinLBS_gui"
    )).resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    prefs = run_gui(binary, "--test-prefs")
    assert "REFOCUS_ARGV_TEST:PASS" in prefs, prefs
    check_metric_fixture(binary, args.output)
    if args.optics_report:
        check_optics(binary, args.optics_report.resolve())
    if args.delta_only:
        return
    run_gui(binary, "--test-cmd-q")
    if platform.system() == "Darwin":
        run_gui(binary, "--test-processes")
    for tab in range(4):
        target = args.output.resolve() / f"tab{tab}.png"
        inputs = [args.sample.resolve()] if args.sample else []
        if args.optics_report:
            inputs += ["--optics-json", args.optics_report.resolve()]
        run_gui(binary, "--screenshot", target, f"--tab{tab}", *inputs)
        data = target.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", target
        width, height = struct.unpack(">II", data[16:24])
        assert width >= 1140 and height >= 740, (target, width, height)
        print(f"SCREENSHOT {target} {width}x{height}")


if __name__ == "__main__":
    main()
