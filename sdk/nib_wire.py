"""NOPB v1 binary wire & mmap profile. Stdlib reference for field, chart, and policy exchange."""
import argparse
import hashlib
import json
import math
import os
import struct
import sys
from pathlib import Path

HEADER = struct.Struct("<8s6I4Q")
DESC = struct.Struct("<40s4H2i4I6f4I2Q")
MAGIC = b"NIJBUS01"
ITEM_BYTES = {1: 1, 2: 2, 3: 4, 4: 2, 5: 4, 6: 8, 8: 1, 9: 2, 10: 4, 11: 8, 12: 8, 13: 1}
MAX_FIELDS = 65536
MAX_JSON = 16 << 20
IDENTITY = (1., 0., 0., 0., 1., 0.)


def _check(ok, message):
    if not ok:
        raise ValueError(message)


def _align(n):
    return (n + 63) & ~63


def _ascii_str(x):
    return isinstance(x, str) and bool(x) and x.isascii()


def _u32_pos(x):
    return type(x) is int and 0 < x < 2**32


def _json(v):
    return json.dumps(v, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
                      allow_nan=False).encode("ascii")


def session_id(session):
    return hashlib.sha256(_json(session)).hexdigest()


def _quick(session):
    h = 0xcbf29ce484222325
    for b in _json(session):
        h = ((h ^ b) * 0x100000001b3) & ((1 << 64) - 1)
    return h


def _exact(fp, n):
    data = fp.read(n)
    _check(len(data) == n, "truncated bundle")
    return data


def _num(x):
    return isinstance(x, (int, float)) and math.isfinite(x)


def _affine(a):
    return len(a) == 6 and all(map(_num, a)) and abs(a[0] * a[4] - a[1] * a[3]) > 1e-12


def _bbox_f(b):
    return isinstance(b, (list, tuple)) and len(b) == 4 and all(map(_num, b)) and 0. <= b[0] < b[2] and 0. <= b[1] < b[3]


def _chart_spec(c):
    _check(isinstance(c, dict) and _affine(c.get("affine", ())) and _bbox_f(c.get("bbox", ())), "chart spec")
    _check(_ascii_str(c.get("space", "active_1x")), "chart space")


def _validate_charts(charts):
    for cid, cspec in charts.items():
        _check(_ascii_str(cid), "chart id")
        _chart_spec(cspec)


def _session(s):
    _check(isinstance(s, dict) and set(s) == {"active_wh", "anchor", "frames", "geometry_id"}, "session keys")
    wh, frames = s["active_wh"], s["frames"]
    _check(len(wh) == 2 and all(map(_u32_pos, wh)), "active_wh")
    _check(isinstance(frames, list) and _u32_pos(len(frames)) and all(map(_ascii_str, frames)), "frames")
    _check(type(s["anchor"]) is int and 0 <= s["anchor"] < len(frames) and _ascii_str(s["geometry_id"]), "anchor/geom")
    return len(frames), *wh


def _valid_wire_name(name):
    return _ascii_str(name) and len(name) <= 39 and all(c.isalnum() or c in "._-" for c in name)


def _check_chart_refs(meta, charts):
    for key in ("chart", "to_chart"):
        cid = meta.get(key)
        if cid is not None:
            _check(isinstance(charts, dict) and cid in charts, f"unknown {key} {cid!r}")


def _valid_dim(n):
    return type(n) is int and 0 <= n < 2**63


def _valid_axes(axes):
    return isinstance(axes, list) and all(_ascii_str(a) for a in axes) and len(set(axes)) == len(axes)


def _has_spatial_chart(d, meta):
    return meta.get("chart") is not None or tuple(d["bbox"]) != (0, 0, 0, 0) or tuple(d["affine"]) != IDENTITY


def _check_nd_field(d, meta):
    dims, axes = meta["dims"], meta["axes"]
    _check(tuple(d["shape"]) == (0, 0, 0, 0), "ND descriptor shape sentinel")
    if _has_spatial_chart(d, meta):
        x0, y0, x1, y1 = d["bbox"]
        _check(_affine(d["affine"]) and 0 <= x0 < x1 and 0 <= y0 < y1, "ND spatial chart")
    _check(isinstance(dims, list) and len(axes) == len(dims) and all(map(_valid_dim, dims)) and _valid_axes(axes),
           "ND shape/axes")
    return math.prod(dims) * ITEM_BYTES[d["dtype"]]


def _check_flow_endpoint(d, meta, nf, c):
    tf = meta.get("to_frame")
    ok_frame = type(tf) is int and -1 <= tf < nf and _affine(meta.get("to_affine", ()))
    _check(c == 2 and d["dtype"] in (4, 5, 6) and meta["unit"] == "external_px" and ok_frame, "flow endpoint")


def _check_kind_specific(d, meta, nf, c):
    if d["kind"] == 1:
        _check_flow_endpoint(d, meta, nf, c)
    elif d["kind"] == 2:
        _check(c == 3 and d["dtype"] in (4, 5, 6) and meta["unit"] == "external_px2", "covariance: expected xx,xy,yy")


def _check_image_field(d, meta, nf):
    n, h, w, c = d["shape"]
    x0, y0, x1, y1 = d["bbox"]
    _check(n > 0 and min(h, w, c) > 0 and _affine(d["affine"]), "image shape/affine")
    _check(0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h, "valid_bbox")
    if "valid_bbox_f" in meta:
        bf = meta["valid_bbox_f"]
        _check(_bbox_f(bf) and bf[2] <= w + 1e-6 and bf[3] <= h + 1e-6, "valid_bbox_f")
    _check_kind_specific(d, meta, nf, c)
    return n * h * w * c * ITEM_BYTES[d["dtype"]]


def _field(d, meta, nf, charts=None):
    _check(_valid_wire_name(d["name"]) and d["dtype"] in ITEM_BYTES and d["kind"] in range(4), "field/type")
    _check(d["origin"] in range(5) and -1 <= d["frame"] < nf and d["layer"] >= -1, "frame/layer/origin")
    _check(bool(meta.get("semantic")) and bool(meta.get("unit")), "semantic/unit")
    _check_chart_refs(meta, charts)
    return _check_nd_field(d, meta) if d["kind"] == 3 else _check_image_field(d, meta, nf)


class FileSlice:
    """A lazy payload in an already open, seekable source bundle or mmap file."""

    def __init__(self, fp, offset, size):
        self.fp, self.offset, self.nbytes = fp, offset, size

    def write_to(self, out):
        self.fp.seek(self.offset)
        left = self.nbytes
        while left:
            data = _exact(self.fp, min(left, 1 << 20))
            out.write(data)
            left -= len(data)


def _pack_directory(fields, nf, charts, start_pos):
    pos, directory, payloads = start_pos, [], []
    for d, m, payload in fields:
        nbytes = _field(d, m, nf, charts)
        buf = payload if isinstance(payload, FileSlice) else memoryview(payload).cast("B")
        _check(buf.nbytes == nbytes, "payload size")
        directory.append(DESC.pack(d["name"].encode("ascii"), d["dtype"], d["kind"], d["origin"],
                                   0, d["frame"], d["layer"], *d["shape"], *d["affine"],
                                   *d["bbox"], pos, nbytes))
        payloads.append((pos, buf))
        pos = _align(pos + nbytes)
    return directory, payloads


def _write_payloads(fp, payloads):
    for off, buf in payloads:
        fp.write(bytes(off - fp.tell()))
        if isinstance(buf, FileSlice):
            buf.write_to(fp)
        else:
            fp.write(buf)


def write_bundle(fp, session, stage, fields, *, charts=None, policy=None, session_meta=None):
    """Write at offset zero to a seekable binary stream. Each (descriptor, metadata, buffer) is contiguous."""
    nf, w, h = _session(session)
    _check(0 <= stage <= 5 and len(fields) <= MAX_FIELDS, "stage/count")
    charts = dict(charts or {})
    _validate_charts(charts)
    metadata = {d["name"]: meta for d, meta, _ in fields}
    _check(len(metadata) == len(fields), "duplicate field")
    doc = {"session": session, "session_id": session_id(session), "fields": metadata}
    if charts:
        doc["charts"] = charts
    if policy:
        doc["policy"] = dict(policy)
    if session_meta:
        doc["session_meta"] = dict(session_meta)
    manifest = _json(doc)
    _check(len(manifest) <= MAX_JSON, "manifest size")
    jo = HEADER.size + len(fields) * DESC.size
    directory, payloads = _pack_directory(fields, nf, charts, _align(jo + len(manifest)))
    fp.seek(0)
    fp.write(HEADER.pack(MAGIC, 1, stage, nf, w, h, len(fields), _quick(session), jo, len(manifest), HEADER.size))
    fp.writelines(directory)
    fp.write(manifest)
    _write_payloads(fp, payloads)
    fp.truncate()


def _verify_session_match(s, expected_session):
    if expected_session is not None and s != expected_session:
        diff = sorted(k for k in s.keys() | expected_session.keys() if s.get(k) != expected_session.get(k))
        raise ValueError(f"session mismatch: {', '.join(diff)}")


def _read_manifest_and_check(fp, end, expected_session):
    fp.seek(0)
    magic, ver, stage, nf, w, h, count, quick, jo, jb, directory = HEADER.unpack(_exact(fp, HEADER.size))
    _check((magic, ver) == (MAGIC, 1) and stage <= 5 and count <= MAX_FIELDS, "header/version")
    _check(directory == HEADER.size and jo == directory + count * DESC.size, "directory")
    _check(0 < jb <= MAX_JSON and jo + jb <= end, "manifest bounds")
    fp.seek(jo)
    manifest = json.loads(_exact(fp, jb).decode("utf-8"))
    s = manifest["session"]
    _validate_charts(manifest.get("charts", {}))
    _check(isinstance(manifest.get("policy", {}), dict), "policy")
    _check(isinstance(manifest.get("session_meta", {}), dict), "session_meta")
    _check(_session(s) == (nf, w, h) and manifest["session_id"] == session_id(s) and _quick(s) == quick, "session hash")
    _verify_session_match(s, expected_session)
    return stage, nf, count, jo, jb, directory, manifest


def _unpack_one_desc(fp, fields, charts, nf, index, next_offset, end):
    v = DESC.unpack(_exact(fp, DESC.size))
    text, nul, pad = v[0].partition(b"\0")
    _check(nul and not any(pad) and v[4] == 0, "name padding/compression")
    name = text.decode("ascii")
    _check(name not in index and name in fields, "field dictionary")
    d = dict(name=name, dtype=v[1], kind=v[2], origin=v[3], frame=v[5], layer=v[6],
             shape=v[7:11], affine=v[11:17], bbox=v[17:21], offset=v[21], nbytes=v[22])
    size = _field(d, fields[name], nf, charts)
    _check(size == d["nbytes"] and d["offset"] == next_offset and next_offset + size <= end, "payload bounds/layout")
    index[name] = d
    return _align(next_offset + size)


def read_index(fp, expected_session=None):
    """Read metadata only. expected_session is required when attaching, optional for inspection."""
    end = fp.seek(0, os.SEEK_END)
    stage, nf, count, jo, jb, directory, manifest = _read_manifest_and_check(fp, end, expected_session)
    fields, charts = manifest["fields"], manifest.get("charts", {})
    fp.seek(directory)
    index, next_offset = {}, _align(jo + jb)
    for _ in range(count):
        next_offset = _unpack_one_desc(fp, fields, charts, nf, index, next_offset, end)
    _check(index.keys() == fields.keys(), "unbound field metadata")
    return stage, manifest, index


def read_rows(fp, field, first, stop, batch=0):
    """Read an interval of rows; callers may also map [offset, offset+nbytes) read-only."""
    n, h, w, c = field["shape"]
    _check(field["kind"] != 3 and 0 <= batch < n and 0 <= first <= stop <= h, "image row interval")
    stride = w * c * ITEM_BYTES[field["dtype"]]
    fp.seek(field["offset"] + (batch * h + first) * stride)
    return _exact(fp, (stop - first) * stride)


def active_to_grid(field, x, y, meta=None):
    """Return valid discrete-bounded source centers to interpolate at, or None outside valid_bbox."""
    a, b, tx, c, d, ty = field["affine"]
    det = a * d - b * c
    u = (d * (x - tx) - b * (y - ty)) / det
    v = (a * (y - ty) - c * (x - tx)) / det
    x0, y0, x1, y1 = (meta or {}).get("valid_bbox_f") or field["bbox"]
    if not (x0 <= u < x1 and y0 <= v < y1):
        return None
    i_min, i_max = math.ceil(x0 - .5), math.ceil(x1 - .5) - 1
    j_min, j_max = math.ceil(y0 - .5), math.ceil(y1 - .5) - 1
    if i_min > i_max or j_min > j_max:
        return None
    return min(max(u, i_min + .5), i_max + .5), min(max(v, j_min + .5), j_max + .5)


def active_flow(field_or_src_affine, meta_or_dst_affine, u, v, du, dv):
    """Map displacement endpoints to their respective active grids: T_dst(u+du,v+dv) - T_src(u,v)."""
    a, b, tx, c, d, ty = (field_or_src_affine["affine"]
                          if isinstance(field_or_src_affine, dict) else field_or_src_affine)
    e, f, ox, g, h, oy = (meta_or_dst_affine["to_affine"]
                          if isinstance(meta_or_dst_affine, dict) else meta_or_dst_affine)
    return ((e - a) * u + (f - b) * v + e * du + f * dv + (ox - tx),
            (g - c) * u + (h - d) * v + g * du + h * dv + (oy - ty))


def active_cov2(field_or_affine, cxx, cxy, cyy):
    """Push 2x2 symmetric covariance forward to active_1x via A * Sigma * A^T."""
    a, b, _, c, d, _ = (field_or_affine["affine"]
                        if isinstance(field_or_affine, dict) else field_or_affine)
    axx, axy = a * cxx + b * cxy, a * cxy + b * cyy
    return axx * a + axy * b, axx * c + axy * d, (c * cxx + d * cxy) * c + (c * cxy + d * cyy) * d


def make_example(fp):
    session = {"active_wh": [3, 2], "anchor": 0, "frames": ["fixture:a", "fixture:b"],
               "geometry_id": "fixture:full-rggb-centers"}
    common = dict(layer=-1, bbox=(0, 0, 3, 2), affine=IDENTITY)
    mask = dict(common, name="region", dtype=1, kind=0, origin=3, frame=0, shape=(1, 2, 3, 1))
    flow = dict(common, name="motion", dtype=5, kind=1, origin=2, frame=0, shape=(1, 2, 3, 2))
    write_bundle(fp, session, 2, [
        (mask, {"semantic": "mask.binary", "unit": "1"}, bytes([0, 1, 0, 1, 0, 1])),
        (flow, {"semantic": "flow.fwd", "unit": "external_px", "to_frame": 1,
                "to_affine": list(IDENTITY)}, struct.pack("<12f", *([1., -2.] * 6)))])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("op", choices=("example", "inspect"))
    parser.add_argument("path", type=Path)
    args = parser.parse_args(argv)
    if args.op == "example":
        with args.path.open("xb") as fp:
            make_example(fp)
    with args.path.open("rb") as fp:
        stage, m, fields = read_index(fp)
        print(f"stage={stage} active={m['session']['active_wh']} session={m['session_id']}")
        for name, d in fields.items():
            print(f"{name}: {d['shape']} {m['fields'][name]['unit']} offset={d['offset']} bytes={d['nbytes']}")


if __name__ == "__main__":  # pragma: no cover
    try:
        main()
    except (ValueError, KeyError, TypeError, struct.error, OSError) as exc:
        print(f"nib: {exc}", file=sys.stderr)
        sys.exit(2)
