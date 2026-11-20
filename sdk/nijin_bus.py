"""Host-centric NOPB/1 session, Chart geometry, wild-data adapters, policy DAG, and client/server SDK."""
import argparse
from array import array
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import struct
import sys
from tempfile import NamedTemporaryFile, TemporaryDirectory

import nib_wire as wire

DTYPES = {"u8": 1, "u16": 2, "i32": 3, "f16": 4, "f32": 5, "f64": 6,
          "i8": 8, "i16": 9, "u32": 10, "i64": 11, "u64": 12, "bool": 13}
FORMATS = {"B": "u8", "H": "u16", "i": "i32", "e": "f16", "f": "f32", "d": "f64",
           "b": "i8", "h": "i16", "I": "u32", "q": "i64", "Q": "u64", "?": "bool"}
UNPACK_FMT = {1: "<B", 2: "<H", 3: "<i", 4: "<e", 5: "<f", 6: "<d",
              8: "<b", 9: "<h", 10: "<I", 11: "<q", 12: "<Q", 13: "<?"}
NP_TYPES = {1: "u1", 2: "<u2", 3: "<i4", 4: "<f2", 5: "<f4", 6: "<f8", 8: "i1",
            9: "<i2", 10: "<u4", 11: "<i8", 12: "<u8", 13: "?"}
STAGES = ("EXT_INPUT", "S1_CALIB", "S2_ALIGN", "S3_FUSE", "S4_OPTICS", "S5_DELIVER")
ORIGINS = {
    0: "nijin_measured",
    1: "co_calibrated",
    2: "testable_geom",
    3: "partition_mask",
    4: "generative_prior",
}
ORIGIN_CODES = {v: k for k, v in ORIGINS.items()}
PROV_MEASURED = 0x01
PROV_CALIBRATED = 0x02
PROV_EXT_GEOMETRY = 0x04
PROV_EXT_ROUTING = 0x08
PROV_GENERATIVE = 0x10
_UNSET = object()


class BusError(ValueError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def _require(ok, code, message):
    if not ok:
        raise BusError(code, message)


def _stage(s):
    if type(s) is str:
        _require(s in STAGES, "stage", f"unknown stage {s!r}")
        return STAGES.index(s)
    _require(type(s) is int and 0 <= s < len(STAGES), "stage", f"unknown stage {s!r}")
    return s


def _origin(val, *, allow_measured=False):
    if isinstance(val, str):
        _require(val in ORIGIN_CODES, "origin", f"unknown origin {val!r}")
        val = ORIGIN_CODES[val]
    _require(type(val) is int and val in ORIGINS, "origin", f"unknown origin {val!r}")
    _require(allow_measured or val != 0, "origin",
             "origin=0 (nijin_measured) is reserved for nijinLBS host readouts; external inputs use 1..4")
    return val


def _key(name):
    _require(isinstance(name, str) and name, "field", "a public field name is required")
    if wire._valid_wire_name(name):
        return name
    return "id." + hashlib.sha256(name.encode()).hexdigest()[:36]


def canonical_frame_ids(frames):
    """Preserve ASCII IDs directly; hash UTF-8 file paths into deterministic ASCII frame IDs."""
    ids, raw_paths, has_unicode = [], [], False
    for f in frames:
        _require(isinstance(f, (str, os.PathLike)), "frames", "frame specifier must be str or Path")
        s = os.fspath(f)
        _require(bool(s), "frames", "empty frame specifier")
        raw_paths.append(s)
        if s.isascii():
            ids.append(s)
        else:
            has_unicode = True
            ids.append("src." + hashlib.sha256(s.encode("utf-8")).hexdigest()[:24])
    return ids, (raw_paths if has_unicode else None)


def _coerce_numpy_array(data):
    import numpy as np
    arr = np.asarray(data)
    if arr.dtype.byteorder == ">":
        arr = arr.astype(arr.dtype.newbyteorder("<"))
    return arr if arr.flags.c_contiguous else np.ascontiguousarray(arr)


def _unwrap_tensor_like(data, *, coerce=False):
    if hasattr(data, "detach") and hasattr(data, "cpu"):
        data = data.detach().cpu().numpy()
    if coerce and hasattr(data, "__array_interface__"):
        return _coerce_numpy_array(data)
    return data


def _infer_dtype(v, declared):
    fmt = v.format.lstrip("@=<>!")
    inferred = FORMATS.get(fmt)
    if fmt in ("l", "L") and v.itemsize in (4, 8):
        inferred = ("u" if fmt == "L" else "i") + str(8 * v.itemsize)
    dtype = inferred if declared is None else declared
    _require(dtype in DTYPES, "dtype", f"unsupported dtype {dtype!r}")
    return fmt, inferred, dtype, DTYPES[dtype]


def _little_endian_ok(v):
    if v.itemsize == 1 or v.format.startswith("<"):
        return True
    return sys.byteorder == "little" and not v.format.startswith((">", "!"))


def _is_raw_bytes(data, fmt, v):
    return isinstance(data, (bytes, bytearray)) or (fmt == "B" and v.ndim == 1)


def _dims_match_bytes(dims, code, nbytes):
    return all(wire._valid_dim(n) for n in dims) and math.prod(dims) * wire.ITEM_BYTES[code] == nbytes


def _buffer(data, dtype, shape, *, coerce=False):
    data = _unwrap_tensor_like(data, coerce=coerce)
    v = memoryview(data)
    fmt, inferred, dtype, code = _infer_dtype(v, dtype)
    _require(v.c_contiguous, "layout", "supply a C-contiguous buffer (e.g. ascontiguousarray)")
    if not _is_raw_bytes(data, fmt, v):
        _require(dtype == inferred and v.itemsize == wire.ITEM_BYTES[code],
                 "dtype", "buffer type differs from declared dtype")
        _require(_little_endian_ok(v), "endian", "supply little-endian elements")
    dims = tuple(v.shape if shape is None else shape)
    _require(_dims_match_bytes(dims, code, v.nbytes), "shape", "shape or dtype disagrees with payload length")
    return code, dims, v.cast("B") if v.nbytes else memoryview(b"")


def _rle_read_varint(counts, p, n):
    x, k, more = 0, 0, 1
    while more:
        _require(p < n, "rle", "truncated COCO RLE string")
        c = ord(counts[p]) - 48
        x |= (c & 0x1f) << (5 * k)
        more = c & 0x20
        p += 1
        k += 1
        if not more and (c & 0x10):
            x |= -1 << (5 * k)
    return x, p


def _rle_decode_counts(counts):
    if isinstance(counts, (bytes, bytearray)):
        counts = counts.decode("ascii")
    if not isinstance(counts, str):
        runs = [int(r) for r in counts]
        _require(all(r >= 0 for r in runs), "rle", "negative RLE run")
        return runs
    out, p, n = [], 0, len(counts)
    while p < n:
        x, p = _rle_read_varint(counts, p, n)
        if len(out) > 2:
            x += out[-2]
        _require(x >= 0, "rle", "negative RLE run")
        out.append(x)
    return out


def _rle_encode_coco(runs):
    out = []
    for i, x in enumerate(runs):
        val = x - runs[i - 2] if i > 2 else x
        more = True
        while more:
            c = val & 0x1f
            val >>= 5
            done = (val == 0 and (c & 0x10) == 0) or (val == -1 and (c & 0x10) != 0)
            more = not done
            out.append(chr((c | 0x20 if more else c) + 48))
    return "".join(out)


class Chart:
    """Composable pixel-edge coordinate chart mapping external grids/shapes to sensor active_1x."""

    def __init__(self, affine, bbox, canvas_wh=None, *, name="ext", space="active_1x"):
        affine = tuple(float(x) for x in affine)
        bbox = tuple(float(x) for x in bbox)
        _require(wire._affine(affine) and wire._bbox_f(bbox), "chart", "invalid affine or valid_bbox")
        cw, ch = (max(bbox[2], 1.0), max(bbox[3], 1.0)) if canvas_wh is None else (float(canvas_wh[0]), float(canvas_wh[1]))
        _require(cw >= bbox[2] - 1e-6 and ch >= bbox[3] - 1e-6, "chart", "bbox exceeds canvas")
        self.affine, self.bbox, self.canvas_wh = affine, bbox, (cw, ch)
        self.name, self.space = name, space

    @classmethod
    def active(cls, width, height, *, name="active_1x"):
        return cls(wire.IDENTITY, (0., 0., float(width), float(height)), (width, height), name=name)

    @classmethod
    def from_spec(cls, name, spec):
        b = spec["bbox"]
        cwh = spec.get("canvas_wh", (max(b[2], 1.0), max(b[3], 1.0)))
        return cls(spec["affine"], b, cwh, name=name, space=spec.get("space", "active_1x"))

    def to_spec(self):
        return {"affine": list(self.affine), "bbox": list(self.bbox),
                "canvas_wh": list(self.canvas_wh), "space": self.space}

    def int_bbox(self):
        x0, y0, x1, y1 = self.bbox
        ix0, iy0 = int(math.floor(x0 + 1e-6)), int(math.floor(y0 + 1e-6))
        return (ix0, iy0, max(ix0 + 1, int(math.ceil(x1 - 1e-6))), max(iy0 + 1, int(math.ceil(y1 - 1e-6))))

    def scale(self, sx, sy=None, *, name=None):
        sy = sx if sy is None else sy
        _require(sx > 0 and sy > 0, "chart", "scale factors must be positive")
        a00, a01, b0, a10, a11, b1 = self.affine
        x0, y0, x1, y1 = self.bbox
        cw, ch = self.canvas_wh
        return Chart((a00 / sx, a01 / sy, b0, a10 / sx, a11 / sy, b1),
                     (x0 * sx, y0 * sy, x1 * sx, y1 * sy), (cw * sx, ch * sy),
                     name=name or f"{self.name}.s", space=self.space)

    def resize(self, new_w, new_h, *, name=None):
        cw, ch = self.canvas_wh
        return self.scale(float(new_w) / cw, float(new_h) / ch, name=name or f"{self.name}.r")

    def pad(self, *, left=0., top=0., right=0., bottom=0., name=None):
        _require(min(left, top, right, bottom) >= 0, "chart", "padding must be nonnegative")
        a00, a01, b0, a10, a11, b1 = self.affine
        x0, y0, x1, y1 = self.bbox
        cw, ch = self.canvas_wh
        return Chart((a00, a01, b0 - a00 * left - a01 * top, a10, a11, b1 - a10 * left - a11 * top),
                     (x0 + left, y0 + top, x1 + left, y1 + top),
                     (cw + left + right, ch + top + bottom),
                     name=name or f"{self.name}.p", space=self.space)

    def letterbox(self, target_w, target_h=None, *, align="top_left", name=None):
        target_h = target_w if target_h is None else target_h
        cw, ch = self.canvas_wh
        s = min(float(target_w) / cw, float(target_h) / ch)
        rw, rh = max(1, int(round(cw * s))), max(1, int(round(ch * s)))
        px, py = float(target_w) - rw, float(target_h) - rh
        _require(px >= 0 and py >= 0 and align in ("top_left", "center"), "chart", "invalid letterbox")
        left, top = (0., 0.) if align == "top_left" else (math.floor(px * .5), math.floor(py * .5))
        return self.resize(rw, rh).pad(left=left, top=top, right=px - left, bottom=py - top,
                                       name=name or f"{self.name}.lb")

    def crop(self, cx0, cy0, cx1, cy1, *, name=None):
        _require(0 <= cx0 < cx1 and 0 <= cy0 < cy1, "chart", "invalid crop box")
        a00, a01, b0, a10, a11, b1 = self.affine
        x0, y0, x1, y1 = self.bbox
        vx0, vy0 = max(0., x0 - cx0), max(0., y0 - cy0)
        vx1, vy1 = min(float(cx1 - cx0), x1 - cx0), min(float(cy1 - cy0), y1 - cy0)
        _require(vx0 < vx1 and vy0 < vy1, "chart", "crop misses valid footprint")
        return Chart((a00, a01, b0 + a00 * cx0 + a01 * cy0, a10, a11, b1 + a10 * cx0 + a11 * cy0),
                     (vx0, vy0, vx1, vy1), (cx1 - cx0, cy1 - cy0),
                     name=name or f"{self.name}.c", space=self.space)

    def rotate90(self, k=1, *, name=None):
        c = self
        for _ in range(k % 4):
            a00, a01, b0, a10, a11, b1 = c.affine
            x0, y0, x1, y1 = c.bbox
            cw, ch = c.canvas_wh
            c = Chart((-a01, a00, b0 + a01 * ch, -a11, a10, b1 + a11 * ch),
                      (ch - y1, x0, ch - y0, x1), (ch, cw), name=c.name, space=c.space)
        c.name = name or f"{self.name}.rot{k % 4}"
        return c

    def sample_at_active(self, x, y):
        return wire.active_to_grid({"affine": self.affine, "bbox": self.int_bbox()}, x, y, {"valid_bbox_f": self.bbox})

    def map_points(self, xy_pairs):
        a00, a01, b0, a10, a11, b1 = self.affine
        return [(a00 * x + a01 * y + b0, a10 * x + a11 * y + b1) for x, y in xy_pairs]

    @staticmethod
    def map_flow(src_chart, dst_chart, u, v, du, dv):
        return wire.active_flow(src_chart.affine, dst_chart.affine, u, v, du, dv)

    def map_cov2(self, cxx, cxy, cyy):
        return wire.active_cov2(self.affine, cxx, cxy, cyy)


def _transpose_fortran_to_c(flat, h, w):
    out, idx = bytearray(h * w), 0
    for x in range(w):
        for y in range(h):
            out[y * w + x] = flat[idx]
            idx += 1
    return bytes(out)


class Tensor:
    def __init__(self, descriptor, metadata, payload):
        self.descriptor, self.meta, self.payload = descriptor, metadata, payload

    @property
    def shape(self):
        return tuple(self.meta["dims"] if self.descriptor["kind"] == 3 else self.descriptor["shape"])

    @property
    def origin(self):
        return self.descriptor["origin"]

    @property
    def origin_name(self):
        return ORIGINS[self.descriptor["origin"]]

    def read(self, first=0, stop=None):
        """Flat element interval in logical C order, useful for bounded-memory adapters."""
        count = math.prod(self.shape)
        stop = count if stop is None else stop
        _require(0 <= first <= stop <= count, "range", "element interval outside tensor")
        item = wire.ITEM_BYTES[self.descriptor["dtype"]]
        offset, size = first * item, (stop - first) * item
        if isinstance(self.payload, wire.FileSlice):
            self.payload.fp.seek(self.payload.offset + offset)
            return wire._exact(self.payload.fp, size)
        return bytes(self.payload[offset:offset + size])

    def unpack_floats(self):
        """Decode all elements as Python floats respecting declared wire dtype."""
        fmt = UNPACK_FMT[self.descriptor["dtype"]]
        raw = self.read()
        return [float(v[0]) for v in struct.iter_unpack(fmt, raw)] if raw else []

    def numpy(self):
        import numpy as np
        typ = NP_TYPES[self.descriptor["dtype"]]
        if isinstance(self.payload, wire.FileSlice) and self.payload.nbytes:
            return np.memmap(self.payload.fp, mode="r", dtype=typ, offset=self.payload.offset,
                             shape=self.shape, order="C")
        buf = b"" if isinstance(self.payload, wire.FileSlice) else self.payload.toreadonly()
        return np.frombuffer(buf, dtype=typ).reshape(self.shape)

    def as_json(self):
        _require(self.meta.get("media_type") == "application/json", "media_type", "expected JSON")
        return json.loads(self.read().decode("utf-8"))

    def decode_rle_mask(self):
        """Decode an RLE mask tensor to row-major (H, W, bytes) on its declared chart."""
        _require(self.meta.get("encoding") in ("rle_runs", "coco_rle"), "rle", "expected an RLE mask tensor")
        h, w = self.meta["mask_hw"]
        runs = [r[0] for r in struct.iter_unpack("<I", self.read())]
        _require(sum(runs) == h * w, "rle", "RLE run sum disagrees with mask_hw")
        flat, val = bytearray(), 0
        for r in runs:
            flat.extend(bytes([val]) * r)
            val ^= 1
        raw = bytes(flat) if self.meta.get("rle_order", "F") == "C" else _transpose_fortran_to_c(flat, h, w)
        return h, w, raw


def _stage_for_policy_section(sec):
    return {"align": "S2_ALIGN", "fuse": "S3_FUSE", "optics": "S4_OPTICS"}.get(sec, "S4_OPTICS")


def _min_stage(cur, st):
    return st if cur is None else min(cur, st)


def _normalize_chw_to_nhwc(arr, layout):
    if layout not in ("CHW", "NCHW"):
        return arr
    import numpy as np
    a = np.asarray(arr)
    return np.ascontiguousarray(a.transpose(1, 2, 0)[None] if layout == "CHW" else a.transpose(0, 2, 3, 1))


def _section_refs(d):
    refs = {d[k] for k in ("flow", "warp_grid", "sparse_tracks", "protect_mask", "emitter_mask", "kernel_bank") if d.get(k)}
    for comp in d.get("composites", ()):
        refs.update(comp[k] for k in ("rgb", "weight") if comp.get(k))
    return refs


def _policy_field_refs(policy):
    refs = _section_refs(policy.get("align", {})) | _section_refs(policy.get("optics", {}))
    fuse = policy.get("fuse", {})
    refs.update(ex["mask"] for ex in fuse.get("exclusions", ()) if ex.get("mask"))
    for lay in fuse.get("layers", ()):
        refs.update(lay[k] for k in ("mask", "motion") if lay.get(k))
    return refs


class NijinSession:
    def __init__(self, identity, stage="S1_CALIB", *, session_meta=None):
        wire._session(identity)
        self.identity, self.stage = identity, _stage(stage)
        self.session_meta = dict(session_meta or {})
        w, h = identity["active_wh"]
        self.charts = {"active_1x": Chart.active(w, h, name="active_1x")}
        self.policy = {}
        self.fields, self._files = {}, []
        self.dirty_from = None

    @classmethod
    def create(cls, *, frames, active_wh, geometry_id, anchor=0, stage="EXT_INPUT"):
        ids, raw_paths = canonical_frame_ids(frames)
        smeta = {"source_paths": raw_paths} if raw_paths else None
        return cls(dict(frames=ids, active_wh=list(active_wh), geometry_id=geometry_id, anchor=anchor),
                   stage, session_meta=smeta)

    @classmethod
    def host_open(cls, *, frames, active_wh, geometry_id, anchor=0, stage="S1_CALIB"):
        return cls.create(frames=frames, active_wh=active_wh, geometry_id=geometry_id, anchor=anchor, stage=stage)

    @classmethod
    def open(cls, path):
        fp = Path(path).open("rb")
        try:
            stage, m, idx = wire.read_index(fp)
            s = cls(m["session"], stage, session_meta=m.get("session_meta"))
            s._adopt(fp, m, idx, allow_cross_stage=False, trusted_host=True)
            return s
        except BaseException:
            fp.close()
            raise

    def _invalidate_chart_consumers(self, chart_name):
        for t in self.fields.values():
            if t.meta.get("chart") == chart_name or t.meta.get("to_chart") == chart_name:
                self._mark_dirty(t.meta.get("target_stage", STAGES[self.stage]))

    def register_chart(self, name, chart):
        _require(wire._ascii_str(name) and isinstance(chart, Chart), "chart", "valid ASCII name and Chart required")
        changed = name in self.charts and self.charts[name].to_spec() != chart.to_spec()
        cloned = copy.copy(chart)
        cloned.name = name
        self.charts[name] = cloned
        if changed:
            self._invalidate_chart_consumers(name)
        return cloned

    def chart(self, chart_or_name="active_1x"):
        if not isinstance(chart_or_name, Chart):
            _require(chart_or_name in self.charts, "chart", f"unknown chart {chart_or_name!r}")
            return chart_or_name, self.charts[chart_or_name]
        name = chart_or_name.name
        if name not in self.charts or self.charts[name].to_spec() != chart_or_name.to_spec():
            if name in self.charts:
                name = f"{name}.{hashlib.sha256(wire._json(chart_or_name.to_spec())).hexdigest()[:8]}"
            self.register_chart(name, chart_or_name)
        return name, self.charts[name]

    def _mark_dirty(self, target_stage):
        idx = max(1, _stage(target_stage))
        self.dirty_from = _min_stage(self.dirty_from, idx)

    def cached_stages(self):
        limit = self.stage if self.dirty_from is None else min(self.stage, self.dirty_from - 1)
        return [STAGES[i] for i in range(1, limit + 1)]

    def invalidated_stages(self):
        return [] if self.dirty_from is None else [STAGES[i] for i in range(max(1, self.dirty_from), len(STAGES))]

    def mark_executed_through(self, stage):
        k = _stage(stage)
        _require(self.dirty_from is None or k >= self.dirty_from - 1, "stage", "cannot skip earlier dirty stage")
        self.stage = max(self.stage, k)
        if self.dirty_from is not None and k >= self.dirty_from:
            self.dirty_from = None if k >= len(STAGES) - 1 and k >= self.stage else (k + 1 if k < self.stage else None)

    def _adopt_charts(self, manifest, replace):
        out = {}
        for cid, cspec in manifest.get("charts", {}).items():
            c = Chart.from_spec(cid, cspec)
            if cid in self.charts and not replace:
                _require(self.charts[cid].to_spec() == c.to_spec(), "chart", f"conflicting chart {cid!r}")
            out[cid] = c
        return out

    def _adopt_fields(self, fp, manifest, index, replace, trusted_host):
        incoming, earliest = {}, None
        for key, d in index.items():
            _require(trusted_host or d["origin"] != 0, "origin",
                     f"external attach cannot inject origin=0 field {key!r}")
            meta = manifest["fields"][key]
            name = meta.get("public_name", key)
            _require(_key(name) == key and name not in incoming, "field", "field alias mismatch")
            _require(replace or name not in self.fields, "exists", f"field already exists: {name}")
            incoming[name] = Tensor(d, meta, wire.FileSlice(fp, d["offset"], d["nbytes"]))
            t_stage = _stage(meta.get("target_stage", STAGES[self.stage]))
            if t_stage > 0:
                earliest = _min_stage(earliest, t_stage)
        return incoming, earliest

    def _merge_policy(self, incoming_policy, earliest):
        for sec, val in incoming_policy.items():
            earliest = _min_stage(earliest, _stage(_stage_for_policy_section(sec)))
            if isinstance(val, dict) and isinstance(self.policy.get(sec), dict):
                self.policy[sec].update(val)
            else:
                self.policy[sec] = val
        return earliest

    def _adopt(self, fp, manifest, index, replace=False, allow_cross_stage=True, trusted_host=False):
        incoming_charts = self._adopt_charts(manifest, replace)
        incoming, earliest = self._adopt_fields(fp, manifest, index, replace, trusted_host)
        for cid, cobj in incoming_charts.items():
            self.register_chart(cid, cobj)
        self.fields.update(incoming)
        self.session_meta.update(manifest.get("session_meta", {}))
        earliest = self._merge_policy(copy.deepcopy(manifest.get("policy", {})), earliest)
        if earliest is not None and allow_cross_stage:
            self._mark_dirty(earliest)
        self._files.append(fp)

    def attach(self, path, *, replace=False, strict_stage=False, trusted_host=False):
        """Attach external fields, charts, and policies for the same burst session."""
        fp = Path(path).open("rb")
        try:
            stage, m, idx = wire.read_index(fp, self.identity)
            if strict_stage:
                _require(stage == self.stage, "stage", f"expected {STAGES[self.stage]}, received {STAGES[stage]}")
            self._adopt(fp, m, idx, replace, allow_cross_stage=True, trusted_host=trusted_host)
            return {"invalidated_stages": self.invalidated_stages(), "cached_stages": self.cached_stages()}
        except BaseException:
            fp.close()
            raise

    def __getitem__(self, name):
        _require(name in self.fields, "field", f"missing field {name!r}")
        return self.fields[name]

    def _field_summary(self, t):
        return dict(t.meta, shape=t.shape, dtype=t.descriptor["dtype"], kind=t.descriptor["kind"],
                    origin=t.descriptor["origin"], origin_name=t.origin_name, frame=t.descriptor["frame"],
                    layer=t.descriptor["layer"], affine=t.descriptor["affine"], bbox=t.descriptor["bbox"])

    def describe(self):
        return {
            "api": "nopb/1",
            "stage": STAGES[self.stage],
            "cached_stages": self.cached_stages(),
            "invalidated_stages": self.invalidated_stages(),
            "session_id": wire.session_id(self.identity),
            "session": self.identity,
            "session_meta": copy.deepcopy(self.session_meta),
            "charts": {cid: c.to_spec() for cid, c in self.charts.items()},
            "policy": copy.deepcopy(self.policy),
            "fields": {name: self._field_summary(t) for name, t in self.fields.items()},
        }

    def _default_axes(self, dims, axes, grid):
        if axes is not None:
            return list(axes)
        return [f"d{i}" for i in range(len(dims))] if grid is None else ["batch", "y", "x", "channel"]

    def _prepare_put_meta(self, name, key, dims, axes, grid, semantic, unit, target_stage, chart, meta):
        ax_list = self._default_axes(dims, axes, grid)
        _require(len(ax_list) == len(dims) and wire._valid_axes(ax_list),
                 "axes", "supply one distinct name per logical axis")
        m = dict(meta or {}, semantic=semantic, unit=unit, axes=ax_list)
        if target_stage is not None:
            m["target_stage"] = STAGES[_stage(target_stage)]
        if key != name:
            m["public_name"] = name
        else:
            m.pop("public_name", None)
        if chart is None:
            return m, ax_list, None
        cid, cobj = self.chart(chart)
        m["chart"], m["valid_bbox_f"] = cid, list(cobj.bbox)
        return m, ax_list, cid

    def _resolve_put_geometry(self, dims, axes, grid, cid, m):
        if grid is None:
            m["dims"] = list(dims)
            if cid is None:
                return 3, (0, 0, 0, 0), wire.IDENTITY, (0, 0, 0, 0)
            cobj = self.charts[cid]
            return 3, (0, 0, 0, 0), cobj.affine, cobj.int_bbox()
        _require(len(dims) == 4 and tuple(axes)[1:3] == ("y", "x"), "grid",
                 "a 4D raster grid uses [batch,y,x,channel]; general spatial shapes pass chart=...")
        kind = grid.get("kind", 0)
        _require(kind in (0, 1, 2), "grid", "unknown geometric transform")
        if cid is not None:
            cobj = self.charts[cid]
            return kind, dims, cobj.affine, cobj.int_bbox()
        return kind, dims, grid.get("affine", wire.IDENTITY), grid.get("bbox", (0, 0, dims[2], dims[1]))

    def put(self, name, data, *, semantic, axes=None, unit="1", dtype=None, shape=None, grid=None,
            chart=None, frame=-1, layer=-1, origin=3, target_stage=None, meta=None,
            replace=False, coerce=False, _host_measured=False):
        """Add any dense tensor. Spatial tensors reference a Chart or explicit image grid."""
        key = _key(name)
        _require(replace or name not in self.fields, "exists", f"field already exists: {name}")
        orig_code = _origin(origin, allow_measured=_host_measured)
        code, dims, buf = _buffer(data, dtype, shape, coerce=coerce)
        m, axes, cid = self._prepare_put_meta(name, key, dims, axes, grid, semantic, unit, target_stage, chart, meta)
        kind, storage, affine, bbox = self._resolve_put_geometry(dims, axes, grid, cid, m)
        d = dict(name=key, dtype=code, frame=frame, layer=layer, origin=orig_code, shape=storage,
                 kind=kind, affine=affine, bbox=bbox)
        wire._field(d, m, len(self.identity["frames"]), self.charts)
        self.fields[name] = Tensor(d, m, buf)
        if target_stage is not None:
            self._mark_dirty(target_stage)
        return self.fields[name]

    def put_host_field(self, name, data, **kwargs):
        kwargs.setdefault("origin", 0)
        return self.put(name, data, _host_measured=True, **kwargs)

    def put_json(self, name, value, *, semantic="core.json@1", origin=3, **kwargs):
        meta = dict(kwargs.pop("meta", {}) or {}, media_type="application/json")
        return self.put(name, wire._json(value), semantic=semantic, axes=("byte",), origin=origin, meta=meta, **kwargs)

    def put_mask(self, name, data, *, shape=None, chart="active_1x", semantic="mask.spatial@1",
                 scores=None, target_stage=None, origin=3, replace=False):
        arr = _unwrap_tensor_like(data, coerce=True)
        dims = tuple(memoryview(arr).shape if shape is None else shape)
        _require(len(dims) >= 2, "shape", "mask/logit tensor needs at least (H,W)")
        meta = {"candidate_scores": [float(x) for x in scores]} if scores is not None else {}
        cid, _ = self.chart(chart)
        if len(dims) == 2:
            return self.put(name, arr, shape=(1, dims[0], dims[1], 1), axes=("batch", "y", "x", "channel"),
                            grid={"kind": 0}, chart=cid, semantic=semantic, unit="1", origin=origin,
                            target_stage=target_stage, meta=meta, replace=replace, coerce=True)
        presets = {3: ("candidate", "y", "x"), 4: ("batch", "candidate", "y", "x"),
                   5: ("frame_idx", "object", "candidate", "y", "x")}
        axes = presets.get(len(dims), tuple(f"d{i}" for i in range(len(dims) - 2)) + ("y", "x"))
        return self.put(name, arr, shape=dims, axes=axes, chart=cid, semantic=semantic, unit="1",
                        origin=origin, target_stage=target_stage, meta=meta, replace=replace, coerce=True)

    def put_rle_mask(self, name, rle, *, mask_hw=None, chart="active_1x", order="F",
                     semantic="mask.rle@1", target_stage=None, origin=3, replace=False):
        if isinstance(rle, dict):
            mask_hw, order, counts = tuple(rle["size"]), rle.get("order", order), rle["counts"]
            if isinstance(counts, (bytes, bytearray)):
                counts = counts.decode("ascii")
            enc = "coco_rle" if isinstance(counts, str) else "rle_runs"
        else:
            _require(mask_hw is not None, "rle", "supply mask_hw=(H,W) with raw run list")
            counts, enc = rle, "rle_runs"
        h, w = int(mask_hw[0]), int(mask_hw[1])
        _require(h > 0 and w > 0 and order in ("F", "C"), "rle", "invalid mask_hw or order")
        runs = _rle_decode_counts(counts)
        _require(sum(runs) == h * w, "rle", "RLE run lengths do not sum to H*W")
        buf = array("I", runs)
        if sys.byteorder != "little":
            buf.byteswap()
        meta = {"encoding": enc, "mask_hw": [h, w], "rle_order": order}
        if enc == "coco_rle":
            meta["coco_counts"] = counts
        return self.put(name, memoryview(buf).cast("B"), dtype="u32", shape=(len(runs),), axes=("run",),
                        chart=chart, semantic=semantic, unit="px_run", origin=origin,
                        target_stage=target_stage, meta=meta, replace=replace)

    def put_flow(self, name, data, *, shape=None, src_chart="active_1x", dst_chart=None, frame=0,
                 to_frame=1, layout="HWC", semantic="flow.fwd@1", target_stage="S2_ALIGN",
                 origin=2, replace=False):
        arr = _normalize_chw_to_nhwc(_unwrap_tensor_like(data, coerce=True), layout)
        raw_dims = tuple(memoryview(arr).shape if shape is None else shape)
        dims = (1, *raw_dims) if len(raw_dims) == 3 else raw_dims
        _require(len(dims) == 4 and dims[3] == 2, "flow", "expected flow with 2 channels (du, dv)")
        sc_id, sc = self.chart(src_chart)
        dc_id, dc = self.chart(sc if dst_chart is None else dst_chart)
        meta = {"to_frame": int(to_frame), "to_chart": dc_id, "to_affine": list(dc.affine)}
        return self.put(name, arr, shape=dims, axes=("batch", "y", "x", "uv"), grid={"kind": 1},
                        chart=sc_id, frame=frame, semantic=semantic, unit="external_px",
                        origin=origin, target_stage=target_stage, meta=meta, replace=replace, coerce=True)

    def put_sparse_tracks(self, name, xy, *, chart="active_1x", visibility=None, confidence=None,
                          semantic="track.sparse_xy@1", target_stage="S2_ALIGN", origin=2, replace=False):
        arr = _unwrap_tensor_like(xy, coerce=True)
        _require(len(memoryview(arr).shape) == 3 and memoryview(arr).shape[2] == 2, "tracks", "expected [T, P, 2]")
        meta = {}
        for suffix, val, sem in (("vis", visibility, "track.visibility@1"), ("conf", confidence, "track.confidence@1")):
            if val is not None:
                sub = f"{name}.{suffix}"
                self.put(sub, val, axes=("frame_idx", "point"), semantic=sem, unit="1",
                         origin=origin, target_stage=target_stage, replace=replace, coerce=True)
                meta[f"{suffix}_field"] = sub
        return self.put(name, arr, axes=("frame_idx", "point", "xy"), chart=chart, semantic=semantic,
                        unit="external_px", origin=origin, target_stage=target_stage,
                        meta=meta, replace=replace, coerce=True)

    def put_warp_grid(self, name, grid_xy, *, src_chart="active_1x", dst_chart=None,
                      interpolation="bicubic", semantic="warp.control_grid@1",
                      target_stage="S2_ALIGN", origin=2, replace=False):
        arr = _unwrap_tensor_like(grid_xy, coerce=True)
        dims = memoryview(arr).shape
        _require(len(dims) == 4 and dims[3] == 2, "warp", "expected [N, Gh, Gw, 2] control grid")
        sc_id, sc = self.chart(src_chart)
        dc_id, dc = self.chart(sc if dst_chart is None else dst_chart)
        meta = {"interpolation": interpolation, "to_chart": dc_id, "to_affine": list(dc.affine)}
        return self.put(name, arr, axes=("frame_idx", "grid_y", "grid_x", "xy"), chart=sc_id,
                        semantic=semantic, unit="external_px", origin=origin,
                        target_stage=target_stage, meta=meta, replace=replace, coerce=True)

    def put_prior_rgb(self, name, rgb, *, shape=None, chart="active_1x", layout="HWC",
                      color_space="scene_linear", semantic="radiance.prior_rgb@1",
                      target_stage="S4_OPTICS", origin=4, replace=False):
        _require(_origin(origin) in (1, 4), "origin", "prior_rgb requires generative_prior (4) or co_calibrated (1)")
        arr = _normalize_chw_to_nhwc(_unwrap_tensor_like(rgb, coerce=True), layout)
        raw_dims = tuple(memoryview(arr).shape if shape is None else shape)
        dims = (1, *raw_dims) if len(raw_dims) == 3 else raw_dims
        _require(len(dims) == 4 and dims[3] == 3, "rgb", "expected 3-channel RGB")
        return self.put(name, arr, shape=dims, axes=("batch", "y", "x", "channel"), grid={"kind": 0},
                        chart=chart, semantic=semantic, unit="photon_linear", origin=origin,
                        target_stage=target_stage, meta={"color_space": color_space}, replace=replace, coerce=True)

    def _require_fields(self, *refs):
        for ref in refs:
            if ref is not None and ref is not _UNSET:
                _require(ref in self.fields, "field", f"missing field {ref!r}")

    def set_align_policy(self, *, flow=_UNSET, warp_grid=_UNSET, sparse_tracks=_UNSET,
                         validation=_UNSET, chi2_scale=_UNSET, reset=False):
        self._require_fields(flow, warp_grid, sparse_tracks)
        cur = {} if reset else dict(self.policy.get("align", {"validation": "chi2_gated", "chi2_scale": 1.0}))
        for k, v in (("flow", flow), ("warp_grid", warp_grid), ("sparse_tracks", sparse_tracks)):
            if v is not _UNSET:
                cur[k] = v
        if validation is not _UNSET:
            _require(validation in ("chi2_gated", "prior_weighted"), "policy", "unknown validation mode")
            cur["validation"] = validation
        if chi2_scale is not _UNSET:
            cur["chi2_scale"] = float(chi2_scale)
        self.policy["align"] = cur
        self._mark_dirty("S2_ALIGN")

    def add_exclusion(self, *, mask, frames, threshold=0.0, feather_px=1.5):
        self._require_fields(mask)
        nf, f_list = len(self.identity["frames"]), [int(f) for f in frames]
        _require(all(0 <= f < nf for f in f_list), "frames", "exclusion frame index out of range")
        self.policy.setdefault("fuse", {}).setdefault("exclusions", []).append(
            {"mask": mask, "frames": f_list, "threshold": float(threshold), "feather_px": float(feather_px)}
        )
        self._mark_dirty("S3_FUSE")

    def add_layer(self, layer_name, *, mask, motion=None, chi2_scale=1.0, temporal_mode="sr_fuse"):
        self._require_fields(mask, motion)
        _require(temporal_mode in ("sr_fuse", "trajectory_integrate"), "policy", "unknown temporal_mode")
        self.policy.setdefault("fuse", {}).setdefault("layers", []).append(
            {"id": str(layer_name), "mask": mask, "motion": motion,
             "chi2_scale": float(chi2_scale), "temporal_mode": temporal_mode}
        )
        self._mark_dirty("S3_FUSE")

    def add_composite(self, *, rgb, weight=None, hole_only=False):
        self._require_fields(rgb, weight)
        orig = self.fields[rgb].origin
        _require(orig != 0, "origin", "composite prior cannot claim origin=0 nijin_measured")
        self.policy.setdefault("optics", {}).setdefault("composites", []).append(
            {"rgb": rgb, "weight": weight, "hole_only": bool(hole_only), "origin": orig}
        )
        self._mark_dirty("S4_OPTICS")

    def set_optics_policy(self, *, protect_mask=_UNSET, emitter_mask=_UNSET, kernel_bank=_UNSET,
                          bokeh_de_nisen=_UNSET, bokeh_swirl=_UNSET, bokeh_aspect=_UNSET,
                          bokeh_onion_clean=_UNSET, spectral_ca=_UNSET, spectral_ca_power=_UNSET, reset=False):
        self._require_fields(protect_mask, emitter_mask, kernel_bank)
        cur = {} if reset else dict(self.policy.get("optics", {
            "protect_mask": None, "emitter_mask": None, "kernel_bank": None,
            "bokeh_de_nisen": 0.0, "bokeh_swirl": 0.0, "bokeh_aspect": 1.0,
            "bokeh_onion_clean": 0.0, "spectral_ca": "off", "spectral_ca_power": 0.0,
        }))
        for k, v in (("protect_mask", protect_mask), ("emitter_mask", emitter_mask), ("kernel_bank", kernel_bank)):
            if v is not _UNSET:
                cur[k] = v
        for k, v in (("bokeh_de_nisen", bokeh_de_nisen), ("bokeh_swirl", bokeh_swirl),
                     ("bokeh_aspect", bokeh_aspect), ("bokeh_onion_clean", bokeh_onion_clean),
                     ("spectral_ca_power", spectral_ca_power)):
            if v is not _UNSET:
                cur[k] = float(v)
        if spectral_ca is not _UNSET:
            _require(spectral_ca in ("off", "lateral", "axial-aura"), "policy", "unknown spectral_ca mode")
            cur["spectral_ca"] = spectral_ca
        self.policy["optics"] = cur
        self._mark_dirty("S4_OPTICS")

    def _chart_Ids_from_metas(self, metas):
        used = {}
        for m in metas:
            for ck in ("chart", "to_chart"):
                cid = m.get(ck)
                if cid and cid in self.charts:
                    used[cid] = self.charts[cid].to_spec()
        return used

    def _collect_charts_for_save(self, fields, include_all, include_policy):
        if include_all:
            return {cid: cobj.to_spec() for cid, cobj in self.charts.items()}
        metas = [m for _, m, _ in fields]
        if include_policy:
            metas.extend(self.fields[r].meta for r in _policy_field_refs(self.policy) if r in self.fields)
        return self._chart_Ids_from_metas(metas)

    def save(self, path, *, names=None, include_policy=None):
        keys = tuple(self.fields if names is None else names)
        fields = [(self[k].descriptor, self[k].meta, self[k].payload) for k in keys]
        inc_pol = bool(self.policy) if include_policy is None else bool(include_policy)
        used_charts = self._collect_charts_for_save(fields, names is None, inc_pol)
        path = Path(path)
        with NamedTemporaryFile(dir=path.parent, prefix=".nib-", delete=False) as fp:
            tmp = Path(fp.name)
        try:
            with tmp.open("wb") as fp:
                wire.write_bundle(fp, self.identity, self.stage, fields, charts=used_charts,
                                  policy=(self.policy if inc_pol else None), session_meta=self.session_meta)
            os.replace(tmp, path)
        finally:
            tmp.unlink(missing_ok=True)

    def close(self):
        for fp in self._files:
            fp.close()
        self._files.clear()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def _semantic_matches(expected, actual):
    return True if expected == "*" else (actual.startswith(expected[:-1]) if expected.endswith("*") else actual == expected)


class OperatorHost:
    """Capability registry supporting required + optional ports, chart awareness, and provenance."""

    def __init__(self, *, backend="python-reference"):
        self.backend = backend
        self._ops = {}
        self._sessions = {}
        self._next_sid = 1

    def register(self, name, ports, outputs, handler, *, optional=None, stages=STAGES, parameters=None):
        _require("@" in name and name not in self._ops, "operator", f"invalid/duplicate ID {name}")
        req, opt = dict(ports), dict(optional or {})
        _require(req.keys().isdisjoint(opt.keys()), "ports", "duplicate required/optional port")
        self._ops[name] = ({"inputs": req, "optional_inputs": opt, "outputs": dict(outputs),
                            "stages": list(stages), "parameters": dict(parameters or {})}, handler)

    def describe(self):
        return {"api": "nopb/1", "backend": self.backend,
                "transports": ["nib-file", "mmap-slice", "live-session"],
                "stages": STAGES, "dtypes": DTYPES, "origins": ORIGINS,
                "operators": {k: v[0] for k, v in self._ops.items()}}

    def _bind_inputs(self, spec, session, inputs):
        req_keys, opt_keys, in_keys = set(spec["inputs"]), set(spec["optional_inputs"]), set(inputs)
        _require(req_keys <= in_keys <= (req_keys | opt_keys), "ports", f"required: {sorted(req_keys)}")
        tensors = {}
        for port, key in inputs.items():
            t = session[key]
            expected = spec["inputs"].get(port) or spec["optional_inputs"][port]
            _require(_semantic_matches(expected, t.meta["semantic"]), "semantic", f"{port}: {t.meta['semantic']}")
            tensors[port] = t
        return tensors

    def call(self, name, session, inputs, params=None):
        _require(name in self._ops, "unsupported_operator", name)
        spec, handler = self._ops[name]
        _require(STAGES[session.stage] in spec["stages"], "stage", f"{name} at {STAGES[session.stage]}")
        tensors = self._bind_inputs(spec, session, inputs)
        params = {} if params is None else params
        _require(isinstance(params, dict) and params.keys() <= spec["parameters"].keys(),
                 "parameters", f"accepted: {list(spec['parameters'])}")
        out = NijinSession(session.identity, session.stage, session_meta=session.session_meta)
        for cid, cobj in session.charts.items():
            out.register_chart(cid, cobj)
        handler(tensors, params, out, session)
        _require(set(spec["outputs"]).issubset(out.fields.keys()), "output_ports", name)
        for port, expected in spec["outputs"].items():
            _require(_semantic_matches(expected, out[port].meta["semantic"]), "output_semantic", port)
        return out

    def open_live_session(self, *, frames, active_wh=(8256, 4644), geometry_id="sensor:active-v1",
                          anchor=0, stage="S3_FUSE"):
        sid = f"sess-{self._next_sid}"
        self._next_sid += 1
        s = NijinSession.host_open(frames=frames, active_wh=active_wh, geometry_id=geometry_id,
                                   anchor=anchor, stage=stage)
        w, h = s.identity["active_wh"]
        pw, ph = min(w, 64), min(h, 48)
        s.register_chart("proxy_linear", s.charts["active_1x"].resize(pw, ph, name="proxy_linear"))
        proxy_buf = array("f", [0.18] * (pw * ph * 3))
        if sys.byteorder != "little":
            proxy_buf.byteswap()
        s.put_host_field("host.proxy_rgb", memoryview(proxy_buf).cast("B"), dtype="f32",
                         shape=(1, ph, pw, 3), axes=("batch", "y", "x", "channel"),
                         grid={"kind": 0}, chart="proxy_linear", frame=anchor,
                         semantic="radiance.proxy_rgb@1", unit="photon_linear",
                         meta={"color_space": "scene_linear", "transfer": "linear"})
        self._sessions[sid] = s
        return sid, s

    def get_live_session(self, sid):
        _require(sid in self._sessions, "session", f"unknown live session {sid!r}")
        return self._sessions[sid]

    def close_live_session(self, sid):
        s = self._sessions.pop(sid, None)
        _require(s is not None, "session", f"unknown live session {sid!r}")
        s.close()


def _same_domain(t, z):
    same_shape = z.shape == t.shape and z.meta.get("axes") == t.meta.get("axes")
    same_grid = all(z.descriptor[k] == t.descriptor[k] for k in ("kind", "frame", "layer", "affine", "bbox"))
    return same_shape and same_grid


def _samples(ts):
    t = ts[0]
    _require(all(_same_domain(t, z) for z in ts), "grid", "all ports must share shape/axes/grid")
    _require(t.descriptor["kind"] in (0, 3), "grid", "sensor readings are scalar samples")
    fmts = {5: "<f", 6: "<d"}
    _require(all(z.descriptor["dtype"] in fmts for z in ts), "dtype", "expected f32/f64")
    n = math.prod(t.shape)
    for start in range(0, n, 8192):
        end = min(start + 8192, n)
        batches = [struct.iter_unpack(fmts[z.descriptor["dtype"]], z.read(start, end)) for z in ts]
        for row in zip(*batches):
            values = tuple(v[0] for v in row)
            _require(all(map(math.isfinite, values)), "domain", "non-finite sample")
            yield values


def _like(out, port, result, t, semantic, unit, *, dtype="f64", origin=0):
    if sys.byteorder != "little" and result.itemsize > 1:
        result.byteswap()
    grid = None if t.descriptor["kind"] == 3 else {k: t.descriptor[k] for k in ("kind", "affine", "bbox")}
    out.put(port, memoryview(result).cast("B"), dtype=dtype, shape=t.shape,
            axes=t.meta.get("axes", ("batch", "y", "x", "channel")), semantic=semantic,
            unit=unit, frame=t.descriptor["frame"], layer=t.descriptor["layer"],
            grid=grid, chart=t.meta.get("chart"), origin=origin, _host_measured=(origin == 0))


def _gaussian(tensors, _params, out, _sess):
    ts = [tensors[k] for k in ("mu", "y", "variance")]
    unit = ts[0].meta["unit"]
    _require(ts[1].meta["unit"] == unit and ts[2].meta["unit"] == unit + "^2",
             "unit", "y and mu need the same unit; variance needs that unit squared")
    result, loss = array("d"), 0.
    for mu, y, var in _samples(ts):
        _require(var > 0, "domain", "variance must be positive")
        r = mu - y
        loss += .5 * (r * r / var + math.log(2 * math.pi * var))
        result.append(r / var)
    _like(out, "grad", result, ts[0], "sensor.mean_gradient@1", "1/" + unit, origin=0)
    out.put_host_field("nll", struct.pack("<d", loss), dtype="f64", shape=(), axes=(), semantic="math.nll@1")


def _prox_one(y, z, w, penalty, prior_flag):
    _require(min(w, penalty) >= 0 and max(w, penalty) > 0, "underdetermined",
             "expected nonnegative precisions with at least one positive")
    scale = max(w, penalty)
    a, b = w / scale, penalty / scale
    bits = (PROV_MEASURED if w > 0 else 0) | (prior_flag if penalty > 0 else 0)
    return (a / (a + b)) * y + (b / (a + b)) * z, bits


def _diagonal_prox(tensors, _params, out, _sess):
    ts = [tensors[k] for k in ("y", "z", "precision", "penalty")]
    unit = ts[0].meta["unit"]
    _require(ts[1].meta["unit"] == unit and all(v.meta["unit"] == unit + "^-2" for v in ts[2:]),
             "unit", "precision and penalty use the inverse squared signal unit")
    z_orig = tensors["z"].origin
    prior_flag = PROV_GENERATIVE if z_orig == 4 else PROV_CALIBRATED
    result, prov = array("d"), array("B")
    for y, z, w, penalty in _samples(ts):
        est, bits = _prox_one(y, z, w, penalty, prior_flag)
        result.append(est)
        prov.append(bits)
    out_orig = 4 if (z_orig == 4 and any(p & PROV_GENERATIVE for p in prov)) else 0
    _like(out, "x", result, ts[0], "sensor.estimate@1", unit, origin=out_orig)
    _like(out, "provenance", prov, ts[0], "provenance.bitmask@1", "bitmask", dtype="u8", origin=0)


def _resolve_points_affine(p, params, sess):
    a = params.get("affine")
    if a is not None:
        return a
    cid = p.meta.get("chart")
    _require(cid is not None and cid in sess.charts, "grid", "point field needs Chart or params['affine']")
    return sess.charts[cid].affine


def _map_points(tensors, params, out, sess):
    p = tensors["points"]
    a = _resolve_points_affine(p, params, sess)
    _require(wire._affine(a), "grid", "expected nonsingular six-element affine")
    _require(p.descriptor["kind"] == 3 and p.shape and p.shape[-1] == 2
             and p.meta["unit"] == "external_px", "grid", "expected an ND table ending in an (x,y) axis")
    result = array("d")
    rows = iter(_samples([p]))
    for (x,) in rows:
        (y,) = next(rows)
        result.extend((a[0] * x + a[1] * y + a[2], a[3] * x + a[4] * y + a[5]))
    if sys.byteorder != "little":
        result.byteswap()
    out.put_host_field("points", memoryview(result).cast("B"), dtype="f64", shape=p.shape,
                       axes=p.meta["axes"], chart="active_1x", semantic="geometry.active_xy@1", unit="active_px")


def _tensor_chart(sess, t):
    cid = t.meta.get("chart")
    if cid and cid in sess.charts:
        return sess.charts[cid]
    return Chart(t.descriptor["affine"], t.meta.get("valid_bbox_f") or t.descriptor["bbox"])


def _plane_hw(dims):
    if len(dims) == 4 and dims[0] == 1 and dims[3] == 1:
        return dims[1], dims[2]
    _require(len(dims) == 2, "shape", f"unsupported scalar plane shape {dims}")
    return dims


def _extract_2d_plane(t):
    if t.meta.get("encoding") in ("rle_runs", "coco_rle"):
        sh, sw, raw = t.decode_rle_mask()
        return sh, sw, [float(b) for b in raw], True
    sh, sw = _plane_hw(t.shape)
    discrete = t.descriptor["dtype"] in (1, 2, 3, 8, 9, 10, 11, 12, 13) or t.meta["semantic"].startswith("mask.binary")
    return sh, sw, t.unpack_floats(), discrete


def _bilerp_2d(vals, sh, sw, u, v):
    x0 = max(0, min(sw - 1, int(math.floor(u - .5))))
    y0 = max(0, min(sh - 1, int(math.floor(v - .5))))
    x1, y1 = min(sw - 1, x0 + 1), min(sh - 1, y0 + 1)
    fx, fy = max(0., min(1., u - (x0 + .5))), max(0., min(1., v - (y0 + .5)))
    v00, v01 = vals[y0 * sw + x0], vals[y0 * sw + x1]
    v10, v11 = vals[y1 * sw + x0], vals[y1 * sw + x1]
    return (1. - fy) * ((1. - fx) * v00 + fx * v01) + fy * ((1. - fx) * v10 + fx * v11)


def sample_scalar_2d(sess, t, dst_chart, dst_h, dst_w, *, default=0.0):
    """Sample any scalar/RLE field onto dst_chart (dst_h x dst_w) respecting dtype & discrete valid_bbox."""
    sh, sw, vals, discrete = _extract_2d_plane(t)
    src_chart = _tensor_chart(sess, t)
    is_logit = t.meta["semantic"].startswith("mask.logit")
    out = []
    for row in range(dst_h):
        for col in range(dst_w):
            x_act, y_act = dst_chart.map_points([(col + .5, row + .5)])[0]
            uv = src_chart.sample_at_active(x_act, y_act)
            if uv is None:
                out.append(default)
                continue
            u, v = uv
            if discrete:
                ix = max(0, min(sw - 1, int(math.floor(u))))
                iy = max(0, min(sh - 1, int(math.floor(v))))
                val = vals[iy * sw + ix]
            else:
                val = _bilerp_2d(vals, sh, sw, u, v)
            out.append((1. if val > 0. else 0.) if is_logit else val)
    return out


def _read_composite_alpha(sess, tensors, dst_chart, h, w):
    if "weight" in tensors:
        w_vals = [min(max(v, 0.), 1.) for v in sample_scalar_2d(sess, tensors["weight"], dst_chart, h, w, default=0.)]
    else:
        w_vals = [1.] * (h * w)
    if "protect_mask" not in tensors:
        return w_vals
    m_vals = sample_scalar_2d(sess, tensors["protect_mask"], dst_chart, h, w, default=0.)
    return [0. if mv >= .5 else wv for wv, mv in zip(w_vals, m_vals)]


def _sample_rgb_at(p_vals, ph, pw, u, v, discrete):
    if discrete:
        ix = min(max(int(math.floor(u)), 0), pw - 1)
        iy = min(max(int(math.floor(v)), 0), ph - 1)
        base_idx = (iy * pw + ix) * 3
        return p_vals[base_idx], p_vals[base_idx + 1], p_vals[base_idx + 2]
    xc = min(max(u - 0.5, 0.0), float(pw - 1))
    yc = min(max(v - 0.5, 0.0), float(ph - 1))
    ix0, iy0 = int(math.floor(xc)), int(math.floor(yc))
    ix1, iy1 = min(pw - 1, ix0 + 1), min(ph - 1, iy0 + 1)
    wx, wy = xc - ix0, yc - iy0
    out_c = []
    for c in range(3):
        v00 = p_vals[(iy0 * pw + ix0) * 3 + c]
        v01 = p_vals[(iy0 * pw + ix1) * 3 + c]
        v10 = p_vals[(iy1 * pw + ix0) * 3 + c]
        v11 = p_vals[(iy1 * pw + ix1) * 3 + c]
        out_c.append((v00 * (1.0 - wx) + v01 * wx) * (1.0 - wy) + (v10 * (1.0 - wx) + v11 * wx) * wy)
    return out_c[0], out_c[1], out_c[2]


def _resolve_base_provenance(tensors, base, npix):
    if "base_provenance" in tensors:
        bp = tensors["base_provenance"]
        raw = bp.read()
        _require(len(raw) == npix, "shape", "base_provenance shape mismatch")
        return list(raw)
    d = PROV_GENERATIVE if base.origin == 4 else (PROV_CALIBRATED if base.origin == 1 else PROV_MEASURED)
    return [d] * npix


def _sample_prior_pixel(p_vals, ph, pw, pr_discrete, dst_chart, pr_chart, col, row, alpha):
    if alpha <= 0.0:
        return 0.0, (0.0, 0.0, 0.0)
    x_act, y_act = dst_chart.map_points([(col + 0.5, row + 0.5)])[0]
    uv = pr_chart.sample_at_active(x_act, y_act)
    if uv is None:
        return 0.0, (0.0, 0.0, 0.0)
    return alpha, _sample_rgb_at(p_vals, ph, pw, uv[0], uv[1], pr_discrete)


def _blend_composite_chart(b_vals, p_vals, ph, pw, pr_discrete, dst_chart, pr_chart, h, w, w_vals, b_prov, prior_flag):
    out_rgb, prov = array("f"), array("B")
    for row in range(h):
        for col in range(w):
            i = row * w + col
            alpha, pc = _sample_prior_pixel(p_vals, ph, pw, pr_discrete, dst_chart, pr_chart, col, row, w_vals[i])
            for c in range(3):
                out_rgb.append((1.0 - alpha) * b_vals[3 * i + c] + alpha * pc[c])
            prov.append((b_prov[i] if alpha < 1.0 else 0) | (prior_flag if alpha > 0.0 else 0))
    if sys.byteorder != "little":
        out_rgb.byteswap()
    return out_rgb, prov


def _validate_composite_inputs(base, prior):
    _require(base.origin in (0, 1, 4) and prior.origin in (1, 4), "origin", "base_rgb must be 0/1/4; prior_rgb 1 or 4")
    _require(len(base.shape) == 4 and base.shape[0] == 1 and base.shape[3] == 3, "shape", "base_rgb must be [1,H,W,3]")
    _require(len(prior.shape) == 4 and prior.shape[0] == 1 and prior.shape[3] == 3, "shape", "prior_rgb must be [1,H,W,3]")


def _origin_from_prov_union(prov):
    union_prov = 0
    for pv_b in prov:
        union_prov |= pv_b
    if union_prov & PROV_GENERATIVE:
        return 4
    return 1 if (union_prov & PROV_CALIBRATED) else 0


def _composite_prior(tensors, _params, out, sess):
    base, prior = tensors["base_rgb"], tensors["prior_rgb"]
    _validate_composite_inputs(base, prior)
    b, h, w, _ = base.shape
    _, ph, pw, _ = prior.shape
    dst_chart, pr_chart = _tensor_chart(sess, base), _tensor_chart(sess, prior)
    w_vals = _read_composite_alpha(sess, tensors, dst_chart, h, w)
    b_prov = _resolve_base_provenance(tensors, base, h * w)
    b_vals, p_vals = base.unpack_floats(), prior.unpack_floats()
    pr_discrete = prior.descriptor["dtype"] not in (4, 5, 6)
    prior_flag = PROV_GENERATIVE if prior.origin == 4 else PROV_CALIBRATED
    out_rgb, prov = _blend_composite_chart(b_vals, p_vals, ph, pw, pr_discrete, dst_chart, pr_chart,
                                           h, w, w_vals, b_prov, prior_flag)
    out_orig = _origin_from_prov_union(prov)
    chart = base.meta.get("chart", "active_1x")
    out.put("rgb", memoryview(out_rgb).cast("B"), dtype="f32", shape=base.shape,
            axes=("batch", "y", "x", "channel"), grid={"kind": 0}, chart=chart,
            semantic="radiance.linear_rgb@1", unit=base.meta["unit"], origin=out_orig, _host_measured=(out_orig == 0))
    out.put_host_field("provenance", memoryview(prov).cast("B"), dtype="u8", shape=(b, h, w, 1),
                       axes=("batch", "y", "x", "channel"), grid={"kind": 0}, chart=chart,
                       semantic="provenance.bitmask@1", unit="bitmask")


def default_host():
    host = OperatorHost()
    host.register("nijin.sensor.gaussian_nll_grad@1",
                  {"mu": "sensor.mean@1", "y": "sensor.reading@1", "variance": "sensor.variance@1"},
                  {"nll": "math.nll@1", "grad": "sensor.mean_gradient@1"}, _gaussian)
    host.register("nijin.sensor.diagonal_gls_prox@1",
                  {"y": "sensor.reading@1", "z": "sensor.candidate@1", "precision": "math.precision@1",
                   "penalty": "math.precision@1"},
                  {"x": "sensor.estimate@1", "provenance": "provenance.bitmask@1"}, _diagonal_prox)
    host.register("nijin.geometry.map_points@1", {"points": "geometry.external_xy@1"},
                  {"points": "geometry.active_xy@1"}, _map_points,
                  parameters={"affine": "optional six numbers in row-major 2x3 order (defaults to tensor Chart)"})
    host.register("nijin.optics.composite_prior@1",
                  {"base_rgb": "radiance.*", "prior_rgb": "radiance.*"},
                  {"rgb": "radiance.linear_rgb@1", "provenance": "provenance.bitmask@1"},
                  _composite_prior,
                  optional={"weight": "mask.*", "protect_mask": "mask.*", "base_provenance": "provenance.bitmask@1"},
                  stages=("S3_FUSE", "S4_OPTICS"))
    return host


def _rpc_open(host, req):
    sid, s = host.open_live_session(
        frames=req["frames"], active_wh=req.get("active_wh", [8256, 4644]),
        geometry_id=req.get("geometry_id", "sensor:active-v1"),
        anchor=req.get("anchor", 0), stage=req.get("stage", "S3_FUSE"),
    )
    return {"session_handle": sid, **s.describe()}


def _rpc_attach(host, req):
    s = host.get_live_session(req["session_handle"])
    delta = s.attach(req["input"], replace=bool(req.get("replace", True)),
                     trusted_host=bool(req.get("_trusted_host", False)))
    return {**delta, **s.describe()}


def _rpc_export(host, req):
    s = host.get_live_session(req["session_handle"])
    s.save(req["output"], names=req.get("names"))
    return s.describe()


def _rpc_close(host, req):
    host.close_live_session(req["session_handle"])
    return {"closed": req["session_handle"]}


def _rename_adopted_tensor(t, dst_name):
    key = _key(dst_name)
    d = dict(t.descriptor, name=key)
    m = dict(t.meta)
    if key != dst_name:
        m["public_name"] = dst_name
    else:
        m.pop("public_name", None)
    return Tensor(d, m, bytes(t.read()))


def _rpc_call_on_session(host, s, req):
    with host.call(req["op"], s, req["inputs"], req.get("params")) as out:
        for out_port, dst_name in req.get("adopt_outputs", {}).items():
            s.fields[dst_name] = _rename_adopted_tensor(out[out_port], dst_name)
        if "output" in req:
            out.save(req["output"])
        return out.describe()


def _rpc_inspect_or_call(host, req):
    method = req["method"]
    if "session_handle" in req:
        s = host.get_live_session(req["session_handle"])
        return s.describe() if method == "inspect" else _rpc_call_on_session(host, s, req)
    with NijinSession.open(req["input"]) as s:
        return s.describe() if method == "inspect" else _rpc_call_on_session(host, s, req)


_RPC_HANDLERS = {
    "describe": lambda host, _req: host.describe(),
    "open_session": _rpc_open,
    "session_describe": lambda host, req: host.get_live_session(req["session_handle"]).describe(),
    "attach": _rpc_attach,
    "export": _rpc_export,
    "close_session": _rpc_close,
    "inspect": _rpc_inspect_or_call,
    "call": _rpc_inspect_or_call,
}


def dispatch(host, request):
    _require(request.get("api") == "nopb/1", "version", "expected nopb/1")
    method = request.get("method")
    _require(method in _RPC_HANDLERS, "method", str(method))
    return _RPC_HANDLERS[method](host, request)


def reply(host, req):
    ident = req.get("id") if isinstance(req, dict) else None
    try:
        _require(isinstance(req, dict), "request", "expected a JSON object")
        return {"id": ident, "ok": True, "result": dispatch(host, req)}
    except (ValueError, KeyError, TypeError, OSError, struct.error) as exc:
        return {"id": ident, "ok": False, "error": {"code": getattr(exc, "code", "input"), "message": str(exc)}}


def _native_supports_bus_serve(exe):
    if not (exe.is_file() and os.access(exe, os.X_OK)):
        return False
    help_out = subprocess.run([str(exe), "--help"], capture_output=True, text=True, timeout=3, check=False)
    return "--bus-serve" in help_out.stdout


def _default_host_command(backend):
    ref_cmd = [sys.executable, str(Path(__file__).resolve()), "serve"]
    if backend == "reference":
        return ref_cmd
    exe = Path(__file__).resolve().parents[1] / ("nijinLBS.exe" if platform.system() == "Windows" else "nijinLBS")
    if backend == "native":
        _require(exe.is_file(), "backend", f"native host binary not found: {exe}")
        return [str(exe), "--bus-serve"]
    return [str(exe), "--bus-serve"] if _native_supports_bus_serve(exe) else ref_cmd


class NijinClient:
    """Stateful stdio-JSONL + mmap/file client driving nijinLBS (native RAW host or reference fixture)."""

    def __init__(self, command=None, *, backend="auto"):
        if command is None:
            command = _default_host_command(backend)
        self._proc = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        self._tmpdir = TemporaryDirectory(prefix="nijin-shm-")
        self._req_id = 0

    def request(self, method, **kwargs):
        self._req_id += 1
        req = {"api": "nopb/1", "id": self._req_id, "method": method, **kwargs}
        self._proc.stdin.write(json.dumps(req, allow_nan=False) + "\n")
        self._proc.stdin.flush()
        line = self._proc.stdout.readline()
        _require(bool(line), "eof", "host process closed stdout")
        resp = json.loads(line)
        if not resp.get("ok"):
            err = resp.get("error", {})
            raise BusError(err.get("code", "remote"), err.get("message", "remote error"))
        return resp["result"]

    def describe(self):
        return self.request("describe")

    def open_burst(self, frames, *, active_wh=None, geometry_id=None, anchor=0):
        kw = {"frames": [os.fspath(f) for f in frames], "anchor": anchor}
        if active_wh is not None:
            kw["active_wh"] = list(active_wh)
        if geometry_id is not None:
            kw["geometry_id"] = geometry_id
        info = self.request("open_session", **kw)
        return LiveSessionHandle(self, info["session_handle"], info)

    def close(self):
        if self._proc.poll() is None:
            self._proc.stdin.close()
            self._proc.wait(timeout=5)
        self._proc.stdout.close()
        self._tmpdir.cleanup()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class LiveSessionHandle:
    """Ergonomic client handle over a resident host burst session with mmap slice readout."""

    def __init__(self, client, handle, info):
        self.client, self.handle = client, handle
        self.info = info
        self.local = NijinSession(info["session"], info["stage"], session_meta=info.get("session_meta"))
        for cid, cspec in info.get("charts", {}).items():
            self.local.register_chart(cid, Chart.from_spec(cid, cspec))
        self._seq = 0

    def _scratch_path(self, tag):
        self._seq += 1
        return Path(self.client._tmpdir.name) / f"{self.handle}-{self._seq}-{tag}.nib"

    def get_fields(self, names=None):
        p = self._scratch_path("out")
        self.client.request("export", session_handle=self.handle, output=str(p), names=names)
        return NijinSession.open(p)

    def sync_to_host(self, *, names=None, replace=True):
        p = self._scratch_path("in")
        ext_names = [k for k, t in self.local.fields.items() if t.origin != 0] if names is None else names
        self.local.save(p, names=ext_names, include_policy=bool(self.local.policy))
        return self.client.request("attach", session_handle=self.handle, input=str(p), replace=replace)

    def call(self, op, inputs, *, params=None, adopt_outputs=None):
        p = self._scratch_path("call")
        self.client.request("call", session_handle=self.handle, op=op, inputs=inputs,
                            params=params or {}, adopt_outputs=adopt_outputs or {}, output=str(p))
        return NijinSession.open(p)

    def close(self):
        self.client.request("close_session", session_handle=self.handle)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def _serve_stream(host, instream, outstream):
    for line in instream:
        if not line.strip():
            continue
        try:
            msg = json.loads(line)
            if isinstance(msg, dict):
                msg.pop("_trusted_host", None)
            result = reply(host, msg)
        except ValueError as exc:
            result = {"id": None, "ok": False, "error": {"code": "json", "message": str(exc)}}
        outstream.write(json.dumps(result, allow_nan=False) + "\n")
        outstream.flush()


def main(argv=None, *, instream=None, outstream=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("command", choices=("describe", "inspect", "serve"))
    p.add_argument("path", nargs="?")
    args = p.parse_args(argv)
    host = default_host()
    out = sys.stdout if outstream is None else outstream
    if args.command == "serve":
        _serve_stream(host, sys.stdin if instream is None else instream, out)
    else:
        out.write(json.dumps(dispatch(host, {"api": "nopb/1", "method": args.command, "input": args.path}),
                             indent=2, ensure_ascii=False) + "\n")


if __name__ == "__main__":  # pragma: no cover
    main()
