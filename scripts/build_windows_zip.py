#!/usr/bin/env python3
"""Package the standalone NijiN.LiteBurstSuite Windows CLI and native GUI."""
import argparse
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parent.parent


def main() -> None:
    source_version = re.search(
        r'^#define NIJIN_VERSION "([^"]+)"$', (ROOT / "heraw_version.hpp").read_text(), re.MULTILINE
    ).group(1)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", default=source_version)
    parser.add_argument("output", nargs="?")
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?", args.version):
        parser.error("version must use major.minor.patch")
    if args.version != source_version:
        parser.error("version must match heraw_version.hpp")
    output = Path(args.output).resolve() if args.output else ROOT / f"nijinLBS-v{args.version}-Windows-x64.zip"
    files = [ROOT / name for name in ["nijinLBS.exe", "nijinLBS_gui.exe", "README.md", "CLI.md",
                                      "ALGORITHM.md", "CHANGELOG.md", "LICENSE", "NOTICE.md"]]
    for path in files:
        if not path.is_file():
            parser.error(f"build or supply {path.name} before packaging")
    for directory in ["licenses", "docs", "assets", "lang"]:
        if (ROOT / directory).is_dir():
            files.extend(sorted(p for p in (ROOT / directory).rglob("*") if p.is_file()))
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in files:
                archive.write(path, path.relative_to(ROOT))
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Built {output} ({len(files)} files)")


if __name__ == "__main__":
    main()
