#!/usr/bin/env python3
"""Build NijiN.LiteBurstScaler.app and NijiN.LiteBurstScaler.dmg on macOS."""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_NAME = "NijiN.LiteBurstScaler"
VERSION = sys.argv[1] if len(sys.argv) > 1 else "1.6.2"
DMG_OUT = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else (ROOT / f"nijinLBS-v{VERSION}-macOS-arm64.dmg")


def extract_app_icon_png(hdr_path: Path, dst_png: Path) -> None:
    text = hdr_path.read_text(encoding="ascii")
    m = re.search(r"kAppIconPng\[\d+\]\s*=\s*\{([^}]+)\}", text)
    if not m:
        raise RuntimeError("kAppIconPng array not found in heraw_brand_assets.hpp")
    hex_vals = re.findall(r"0x([0-9a-fA-F]{2})", m.group(1))
    dst_png.write_bytes(bytes(int(h, 16) for h in hex_vals))


def build_icns(base_png: Path, dst_icns: Path, workdir: Path) -> None:
    iconset = workdir / "AppIcon.iconset"
    iconset.mkdir(parents=True, exist_ok=True)
    sizes = [
        (16, "icon_16x16.png"),
        (32, "icon_16x16@2x.png"),
        (32, "icon_32x32.png"),
        (64, "icon_32x32@2x.png"),
        (128, "icon_128x128.png"),
        (256, "icon_128x128@2x.png"),
        (256, "icon_256x256.png"),
        (512, "icon_256x256@2x.png"),
        (512, "icon_512x512.png"),
        (1024, "icon_512x512@2x.png"),
    ]
    for px, name in sizes:
        subprocess.run(
            ["sips", "-z", str(px), str(px), str(base_png), "--out", str(iconset / name)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(dst_icns)], check=True)


def main() -> None:
    cli_bin = ROOT / "nijinLBS"
    gui_bin = ROOT / "nijinLBS_gui"
    if not cli_bin.is_file() or not gui_bin.is_file():
        raise RuntimeError("Build nijinLBS and nijinLBS_gui first via `make nijinLBS nijinLBS_gui`")

    app_dir = ROOT / f"{APP_NAME}.app"
    if app_dir.exists():
        shutil.rmtree(app_dir)

    macos_dir = app_dir / "Contents" / "MacOS"
    res_dir = app_dir / "Contents" / "Resources"
    macos_dir.mkdir(parents=True)
    res_dir.mkdir(parents=True)

    shutil.copy2(gui_bin, macos_dir / "nijinLBS_gui")
    shutil.copy2(cli_bin, macos_dir / "nijinLBS")
    os.chmod(macos_dir / "nijinLBS_gui", 0o755)
    os.chmod(macos_dir / "nijinLBS", 0o755)

    with tempfile.TemporaryDirectory(prefix="nijin_dmg_") as td:
        tdir = Path(td)
        base_png = tdir / "icon_base.png"
        extract_app_icon_png(ROOT / "heraw_brand_assets.hpp", base_png)
        build_icns(base_png, res_dir / "AppIcon.icns", tdir)

        info_plist = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>zh_CN</string>
    <key>CFBundleDisplayName</key>
    <string>{APP_NAME}</string>
    <key>CFBundleExecutable</key>
    <string>nijinLBS_gui</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleIdentifier</key>
    <string>com.nijin.liteburstscaler</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>{APP_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>{VERSION}</string>
    <key>CFBundleVersion</key>
    <string>{VERSION}</string>
    <key>LSMinimumSystemVersion</key>
    <string>12.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>CFBundleDocumentTypes</key>
    <array>
        <dict>
            <key>CFBundleTypeExtensions</key>
            <array>
                <string>nef</string>
                <string>nrw</string>
                <string>dng</string>
                <string>arw</string>
                <string>cr2</string>
                <string>cr3</string>
                <string>raf</string>
                <string>orf</string>
                <string>rw2</string>
            </array>
            <key>CFBundleTypeName</key>
            <string>Camera RAW Image</string>
            <key>CFBundleTypeRole</key>
            <string>Viewer</string>
        </dict>
    </array>
</dict>
</plist>
"""
        (app_dir / "Contents" / "Info.plist").write_text(info_plist, encoding="utf-8")

        subprocess.run(["xattr", "-cr", str(app_dir)], check=True)
        subprocess.run(["codesign", "--force", "--deep", "--sign", "-", str(app_dir)], check=True)

        stage = tdir / "dmg_stage"
        stage.mkdir(parents=True)
        shutil.copytree(app_dir, stage / f"{APP_NAME}.app", symlinks=True)
        (stage / "Applications").symlink_to("/Applications")
        for doc in ["README.md", "ALGORITHM.md", "CLI.md", "LICENSE", "NOTICE.md"]:
            p = ROOT / doc
            if p.is_file():
                shutil.copy2(p, stage / doc)

        if DMG_OUT.exists():
            DMG_OUT.unlink()
        subprocess.run(
            [
                "hdiutil",
                "create",
                "-volname",
                APP_NAME,
                "-srcfolder",
                str(stage),
                "-ov",
                "-format",
                "UDZO",
                str(DMG_OUT),
            ],
            check=True,
        )

    print(f"Built {app_dir} and {DMG_OUT}")


if __name__ == "__main__":
    main()
