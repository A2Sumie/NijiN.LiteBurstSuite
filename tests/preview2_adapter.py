#!/usr/bin/env python3
"""Thin binary marshaller from .npz / folder fixtures to compiled C++20 preview2_module_cli."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import numpy as np

HERE = Path(__file__).resolve().parent
BIN = HERE.parent / "build" / "preview2_module_cli"

MAGIC = b"NJP2\x01\x00\x00\x00"


def pack_bundle(npz_path: Path, dst_bin: Path) -> None:
    entries = []
    with np.load(npz_path, allow_pickle=False) as z:
        for name in z.files:
            arr = z[name]
            if arr.dtype.kind in ("U", "S", "O"):
                continue
            if arr.dtype == np.bool_:
                dtc = 6
                raw = np.ascontiguousarray(arr, dtype=np.uint8).tobytes()
            elif arr.dtype == np.uint8:
                dtc = 5
                raw = np.ascontiguousarray(arr, dtype=np.uint8).tobytes()
            elif arr.dtype == np.uint16:
                dtc = 4
                raw = np.ascontiguousarray(arr, dtype="<u2").tobytes()
            elif np.issubdtype(arr.dtype, np.integer):
                dtc = 3
                raw = np.ascontiguousarray(arr, dtype="<i8").tobytes()
            elif np.issubdtype(arr.dtype, np.complexfloating):
                dtc = 2
                raw = np.ascontiguousarray(arr, dtype="<c16").tobytes()
            else:
                dtc = 1
                raw = np.ascontiguousarray(arr, dtype="<f8").tobytes()
            entries.append((name.encode("utf-8"), dtc, arr.shape, raw))

    with dst_bin.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<I", len(entries)))
        for nb, dtc, shape, raw in entries:
            f.write(struct.pack("<I", len(nb)))
            f.write(nb)
            f.write(struct.pack("<BI", dtc, len(shape)))
            for d in shape:
                f.write(struct.pack("<q", int(d)))
            f.write(struct.pack("<Q", len(raw)))
            f.write(raw)


def unpack_bundle(src_bin: Path, dst_npz: Path) -> None:
    dtype_map = {
        1: np.dtype("<f8"),
        2: np.dtype("<c16"),
        3: np.dtype("<i8"),
        4: np.dtype("<u2"),
        5: np.dtype("u1"),
        6: np.dtype("?"),
    }
    out = {}
    data = src_bin.read_bytes()
    if data[:8] != MAGIC:
        raise ValueError("bad output bundle magic")
    pos = 8
    (count,) = struct.unpack_from("<I", data, pos)
    pos += 4
    for _ in range(count):
        (nlen,) = struct.unpack_from("<I", data, pos)
        pos += 4
        name = data[pos : pos + nlen].decode("utf-8")
        pos += nlen
        dtc, ndim = struct.unpack_from("<BI", data, pos)
        pos += 5
        shape = struct.unpack_from(f"<{ndim}q", data, pos) if ndim else ()
        pos += 8 * ndim
        (blen,) = struct.unpack_from("<Q", data, pos)
        pos += 8
        arr = np.frombuffer(data, dtype=dtype_map[dtc], count=blen // dtype_map[dtc].itemsize, offset=pos)
        pos += blen
        out[name] = arr.reshape(shape).copy() if ndim else arr.reshape(()).copy()
    np.savez_compressed(dst_npz, **out)


def main() -> int:
    if len(sys.argv) != 4:
        print("Usage: preview2_adapter.py <case_id> <input_dir> <output_file>", file=sys.stderr)
        return 2
    case_id = sys.argv[1]
    inp_dir = Path(sys.argv[2]).resolve()
    out_file = Path(sys.argv[3]).resolve()
    if not BIN.is_file():
        print(f"Missing compiled C++20 binary: {BIN}", file=sys.stderr)
        return 2

    if case_id in ("N1-picks", "N6-jpeg"):
        proc = subprocess.run([str(BIN), case_id, str(inp_dir), str(out_file)], check=False)
        return proc.returncode

    with tempfile.TemporaryDirectory(prefix="njp2-bin-") as tmp:
        tdir = Path(tmp)
        req = tdir / "req.bin"
        resp = tdir / "resp.bin"
        pack_bundle(inp_dir / "obs.npz", req)
        proc = subprocess.run([str(BIN), case_id, str(req), str(resp)], check=False)
        if proc.returncode != 0:
            return proc.returncode
        unpack_bundle(resp, out_file)
    return 0


if __name__ == "__main__":
    sys.exit(main())
