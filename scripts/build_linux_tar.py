#!/usr/bin/env python3
"""Package the standalone NijiN.LiteBurstSuite Linux x64 CLI archive."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import tarfile


ROOT = Path(__file__).resolve().parent.parent


def read_version() -> str:
    """Parse NIJIN_VERSION from heraw_version.hpp."""
    match = re.search(
        r'^#define NIJIN_VERSION "([^"]+)"$',
        (ROOT / "heraw_version.hpp").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if not match:
        raise RuntimeError("Could not parse NIJIN_VERSION from heraw_version.hpp")
    return match.group(1)


def add_file(archive: tarfile.TarFile, src: Path, arcname: str, executable: bool) -> None:
    """Add a file with deterministic root-folder path and POSIX permissions."""
    info = archive.gettarinfo(src, arcname=arcname)
    info.mode = 0o755 if executable else 0o644
    with src.open("rb") as handle:
        archive.addfile(info, handle)


def main() -> None:
    """Entry point."""
    source_version = read_version()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?", default=source_version)
    parser.add_argument("output", nargs="?")
    args = parser.parse_args()
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?", args.version):
        parser.error("version must use major.minor.patch")
    if args.version != source_version:
        parser.error("version must match heraw_version.hpp")

    pkg_name = f"nijinLBS-v{args.version}-Linux-x64"
    output = Path(args.output).resolve() if args.output else ROOT / f"{pkg_name}.tar.gz"
    binary = ROOT / "nijinLBS_linux"
    if not binary.is_file():
        parser.error("build nijinLBS_linux before packaging")

    doc_files = [
        ROOT / name
        for name in ["README.md", "CLI.md", "ALGORITHM.md", "CHANGELOG.md", "LICENSE", "NOTICE.md"]
    ]
    for path in doc_files:
        if not path.is_file():
            parser.error(f"missing required file {path.name}")
    extra_files: list[Path] = []
    for directory in ["licenses", "docs"]:
        if (ROOT / directory).is_dir():
            extra_files.extend(sorted(p for p in (ROOT / directory).rglob("*") if p.is_file()))
    if (ROOT / "sdk").is_dir():
        for p in sorted((ROOT / "sdk").rglob("*")):
            if p.is_file() and not any(part in {"__pycache__", ".pytest_cache", ".gauntlet"} for part in p.parts) and p.suffix != ".pyc":
                extra_files.append(p)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    total_files = 1 + len(doc_files) + len(extra_files)
    try:
        with tarfile.open(temporary, "w:gz") as archive:
            add_file(archive, binary, f"{pkg_name}/nijinLBS", executable=True)
            for path in doc_files + extra_files:
                add_file(archive, path, f"{pkg_name}/{path.relative_to(ROOT).as_posix()}", executable=False)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f"Built {output} ({total_files} files)")


if __name__ == "__main__":
    main()
