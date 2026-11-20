import io
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from array import array

import pytest

SDK = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SDK))
import nib_wire as wire  # noqa: E402
from nijin_bus import (  # noqa: E402
    PROV_GENERATIVE,
    PROV_MEASURED,
    BusError,
    Chart,
    NijinClient,
    NijinSession,
    OperatorHost,
    _rle_encode_coco,
    default_host,
    dispatch,
    main as bus_main,
    reply,
)


def session(stage=2, frames=("a", "b")):
    return NijinSession.create(frames=frames, active_wh=[8, 6], geometry_id="crop.0", stage=stage)


def measurements(s):
    s.put_host_field("a", array("d", [2.]), semantic="sensor.mean@1", unit="DN", axes=["sample"])
    s.put_host_field("b", array("d", [5.]), semantic="sensor.reading@1", unit="DN", axes=["sample"])
    s.put_host_field("v", array("d", [4.]), semantic="sensor.variance@1", unit="DN^2", axes=["sample"])


def test_any_rank_empty_and_graph_pass_through(tmp_path):
    a, b = tmp_path / "a.nib", tmp_path / "b.nib"
    name = "台架.波长与视场.点扩散函数"
    axes = ("field_y", "field_x", "nm", "ky", "kx", "trial")
    with session() as s:
        s.put(name, array("f", range(24)), shape=(2, 2, 2, 1, 3, 1), axes=axes,
              semantic="other.lab.psf@11", unit="relative_energy", origin=1)
        s.put_json("scene.tree", {"ids": [1, 8], "edges": [[1, 8]]})
        s.put("empty", b"", dtype="i64", shape=(0, 2), axes=("sample", "xy"), semantic="custom.points@1")
        s.save(a)
    with NijinSession.open(a) as s:
        assert s[name].shape == (2, 2, 2, 1, 3, 1)
        assert s[name].meta["axes"] == list(axes)
        assert s[name].origin == 1 and s[name].origin_name == "co_calibrated"
        assert struct.unpack("<2f", s[name].read(11, 13)) == (11., 12.)
        assert s["empty"].read() == b""
        s.put("flag", b"\1", semantic="other.flag@2")
        s.save(b)
    with NijinSession.open(b) as t:
        assert t[name].read() == struct.pack("<24f", *range(24))
        assert t["scene.tree"].as_json()["edges"] == [[1, 8]]
        assert t["flag"].read() == b"\1"
        with pytest.raises(BusError, match="expected JSON"):
            t["flag"].as_json()


def test_numpy_and_buffer_rules(tmp_path):
    np = pytest.importorskip("numpy")
    p = tmp_path / "data.nib"
    a = np.arange(24, dtype="<i2").reshape(2, 3, 4)
    with session() as s:
        assert s.put("a", a, semantic="test.arr@1").numpy().tolist() == a.tolist()
        with pytest.raises(BusError, match="contiguous"):
            s.put("t", a.T, semantic="b")
        with pytest.raises(BusError, match="little-endian"):
            s.put("big", a.astype(">i2"), semantic="b")
        with pytest.raises(BusError, match="declared"):
            s.put("type", a, dtype="u16", semantic="b")
        with pytest.raises(BusError, match="length"):
            s.put("s", b"\x01", dtype="f32", shape=(2,), semantic="b")
        with pytest.raises(BusError, match="axis"):
            s.put("s", a, axes=["x", "x", "y"], semantic="b")
        s.save(p)
    with NijinSession.open(p) as s:
        mm = s["a"].numpy()
        assert isinstance(mm, np.memmap) and mm[1, 2, 3] == 23 and not mm.flags.writeable


def test_image_rows_and_independent_charts(tmp_path):
    p = tmp_path / "flow.nib"
    g = {"affine": (0., 2., 0., -3., 0., 12.), "kind": 1, "bbox": (1, 0, 3, 2)}
    with session() as s:
        real = array("f", [1., 2.] * 12)
        s.put("motion", real, shape=(2, 2, 3, 2), grid=g, semantic="geom.fwd@1", unit="external_px",
              origin=2, meta={"to_frame": 1, "to_affine": (0., 2., 10., -3., 0., 8.)})
        s.save(p)
    with NijinSession.open(p) as s:
        t = s["motion"]
        assert wire.active_flow(t.descriptor, t.meta, .5, .5, 1., 2.) == (14., -7.)
        assert wire.active_to_grid(t.descriptor, 1., 9.75) is None
        assert wire.active_to_grid(t.descriptor, 1., 8.7) == (1.5, .5)
        with p.open("rb") as f:
            assert struct.unpack("<6f", wire.read_rows(f, t.descriptor, 1, 2, batch=1)) == (1., 2.) * 3


def test_attach_atomicity_and_strict_mode(tmp_path):
    p = tmp_path / "b.nib"
    with session() as s, session() as replace:
        s.put_json("existing", 1)
        replace.put_json("new", 3)
        replace.put_json("existing", 2)
        replace.save(p)
        with pytest.raises(BusError, match="already exists"):
            s.attach(p)
        assert list(s.fields) == ["existing"] and s["existing"].as_json() == 1
        s.attach(p, replace=True)
        assert [s[n].as_json() for n in ("existing", "new")] == [2, 3]
    with session(frames=("b", "a")) as other, pytest.raises(ValueError, match="frames"):
        other.attach(p)
    with session(stage=1) as strict_other, pytest.raises(BusError, match="stage|expected"):
        strict_other.attach(p, strict_stage=True)


def test_register_third_party_operator_without_core_types():
    host = OperatorHost()

    def ids(fields, params, out, _sess):
        out.put_json("selected", fields["graph"].as_json()["ids"][params["index"]], semantic="lab.id@2")

    host.register("lab.pick@2", {"graph": "lab.graph@1"}, {"selected": "lab.id@2"}, ids,
                  stages=["S3_FUSE"], parameters={"index": "integer"})
    spec = host.describe()["operators"]["lab.pick@2"]
    with session("S3_FUSE") as s:
        s.put_json("caller.own_name", {"ids": [8, 13]}, semantic="lab.graph@1")
        with host.call("lab.pick@2", s, {"graph": "caller.own_name"}, {"index": 1}) as result:
            assert result["selected"].as_json() == 13
        assert spec["inputs"] == {"graph": "lab.graph@1"}
        with pytest.raises(BusError, match="accepted"):
            host.call("lab.pick@2", s, {"graph": "caller.own_name"}, {"wrong": "index"})
        with pytest.raises(BusError, match="required"):
            host.call("lab.pick@2", s, {"thing": "caller.own_name"})
    with session(1) as s, pytest.raises(BusError, match="S1_CALIB"):
        host.call("lab.pick@2", s, {"graph": "caller.own_name"})


def test_cross_process_capability_call_and_grad(tmp_path):
    src, dst = tmp_path / "input.nib", tmp_path / "output.nib"
    with session() as s:
        measurements(s)
        s.save(src)
    request = {"api": "nopb/1", "id": 17, "method": "call", "op": "nijin.sensor.gaussian_nll_grad@1",
               "input": str(src), "output": str(dst), "inputs": {"mu": "a", "y": "b", "variance": "v"}}
    lines = [{"api": "nopb/1", "method": "describe"}, dict(request, op="other@1"),
             dict(request, inputs={"mu": "b", "y": "a", "variance": "v"}), request]
    r = subprocess.run([sys.executable, str(SDK / "nijin_bus.py"), "serve"], cwd=tmp_path,
                       input="\n".join(map(json.dumps, lines)) + "\n", capture_output=True, text=True, check=True)
    discovery, unsupported, bad_semantic, acted = map(json.loads, r.stdout.splitlines())
    assert request["op"] in discovery["result"]["operators"]
    assert unsupported["error"]["code"] == "unsupported_operator" and bad_semantic["error"]["code"] == "semantic"
    assert acted["id"] == 17 and acted["result"]["fields"]["nll"]["shape"] == []
    with NijinSession.open(dst) as out:
        assert struct.unpack("<d", out["grad"].read()) == (-.75,)
        assert struct.unpack("<d", out["nll"].read())[0] == pytest.approx(.5 * (2.25 + math.log(8 * math.pi)))
    assert reply(default_host(), {"api": "nopb/2"})["error"]["code"] == "version"


def test_fixture_wire_offsets_and_corruption():
    b = io.BytesIO()
    wire.make_example(b)
    stage, manifest, index = wire.read_index(b)
    assert (stage, manifest["session"]["active_wh"], index["region"]["nbytes"]) == (2, [3, 2], 6)
    assert index["motion"]["offset"] == 768 and wire.DESC.size == 128
    with pytest.raises(ValueError, match="truncated|bounds"):
        wire.read_index(io.BytesIO(b.getvalue()[:-1]))


def test_gls_limits_provenance_and_point_operator():
    host = default_host()
    with session() as s:
        s.put_host_field("y", array("f", [5., 8., 0.]), semantic="sensor.reading@1", unit="DN")
        s.put("z", array("f", [9., 2., 4.]), semantic="sensor.candidate@1", unit="DN", origin=4)
        s.put_host_field("w", array("f", [1., 0., 3.]), semantic="math.precision@1", unit="DN^-2")
        s.put("h", array("f", [0., 4., 1.]), semantic="math.precision@1", unit="DN^-2", origin=3)
        ports = {"y": "y", "z": "z", "precision": "w", "penalty": "h"}
        with host.call("nijin.sensor.diagonal_gls_prox@1", s, ports) as out:
            assert struct.unpack("<3d", out["x"].read()) == (5., 2., 1.)
            prov = list(out["provenance"].read())
            assert prov == [PROV_MEASURED, PROV_GENERATIVE, PROV_MEASURED | PROV_GENERATIVE]
        with pytest.raises(BusError, match="positive"):
            host.call("nijin.sensor.diagonal_gls_prox@1", s, dict(ports, penalty="w"))
        c_ext = Chart((0, 2, 10, -3, 0, 8), (0, 0, 10, 10), (10, 10), name="rot_ext")
        s.register_chart("rot_ext", c_ext)
        s.put("track", array("f", [1., 2., -1., .5]), shape=(2, 2), axes=("track", "xy"),
              chart="rot_ext", semantic="geometry.external_xy@1", unit="external_px", origin=2)
        with host.call("nijin.geometry.map_points@1", s, {"points": "track"}) as out:
            assert out["points"].meta["unit"] == "active_px"
            assert struct.unpack("<4d", out["points"].read()) == (14., 5., 11., 11.)


def test_chart_algebra_sam_letterbox_rotation_and_cov2():
    root = Chart.active(600, 800)  # portrait W=600, H=800
    # CW 90 deg -> (800, 600), SAM top-left pad to 1024x1024 -> valid (0,0,1024,768), 0.25x logits -> (0,0,256,192)
    sam_logits = root.rotate90(1).letterbox(1024, align="top_left").scale(0.25, name="sam_logits")
    assert sam_logits.bbox == pytest.approx((0., 0., 256., 192.))
    # Top-right of rotated image (u=256, v=0) corresponds to top-left of portrait active (x=0, y=0)
    assert sam_logits.map_points([(256., 0.), (0., 0.), (256., 192.)]) == [
        pytest.approx((0., 0.)),
        pytest.approx((0., 800.)),
        pytest.approx((600., 0.)),
    ]
    # Near bottom edge of portrait (x=300, y=799.9) -> rotated u ~ 0.032 -> clamped to first center 0.5 (no pad bleed!)
    u_clamped, v_clamped = sam_logits.sample_at_active(300., 799.9)
    assert u_clamped == pytest.approx(0.5) and v_clamped == pytest.approx(96.0)
    # Right edge in active_1x beyond x=600 maps into bottom padded strip v >= 192 -> None (missing!)
    assert sam_logits.sample_at_active(610., 400.) is None
    # Flow vector (+4, 0) in rotated 1024 chart moves along -y in portrait active_1x:
    rot1024 = root.rotate90(1).letterbox(1024, align="top_left")
    dx_act, dy_act = Chart.map_flow(rot1024, rot1024, 100., 100., 4., 0.)
    assert dx_act == pytest.approx(0.) and dy_act == pytest.approx(-4. * (800. / 1024.))
    # Covariance pushforward A * diag(4, 9) * A^T swaps diagonal axes under 90 deg rotation:
    cxx, cxy, cyy = root.rotate90(1).map_cov2(4., 0., 9.)
    assert (cxx, cxy, cyy) == pytest.approx((9., 0., 4.))


def test_wild_external_formats_rle_5d_logits_nchw_flow_and_tracks(tmp_path):
    np = pytest.importorskip("numpy")
    nib_path = tmp_path / "wild.nib"
    with NijinSession.host_open(frames=["f0", "f1"], active_wh=[80, 60], geometry_id="raw:0") as s:
        sam_c = s.charts["active_1x"].letterbox(16, align="top_left")
        s.register_chart("sam16", sam_c)
        # 1. COCO compressed RLE mask on 16x16 padded chart
        mask_2d = np.zeros((16, 16), dtype=np.uint8)
        mask_2d[2:7, 3:11] = 1
        flat_f = mask_2d.flatten(order="F").tolist()
        runs, cur, cnt = [], 0, 0
        for px in flat_f:
            if px == cur:
                cnt += 1
            else:
                runs.append(cnt)
                cur ^= 1
                cnt = 1
        runs.append(cnt)
        coco_str = _rle_encode_coco(runs)
        s.put_rle_mask("sam.subject_rle", {"size": [16, 16], "counts": coco_str}, chart="sam16",
                       target_stage="S4_OPTICS")
        # 2. 5D multi-frame, multi-object, 3-candidate SAM float logits [T=2, M=2, K=3, H=8, W=8]
        logits_5d = np.linspace(-4., 4., 2 * 2 * 3 * 8 * 8, dtype=np.float32).reshape(2, 2, 3, 8, 8)
        s.put_mask("sam.multi_logits", logits_5d, chart=sam_c.scale(0.5, name="sam8"),
                   scores=[0.71, 0.96, 0.84], target_stage="S3_FUSE")
        # 3. PyTorch-style NCHW optical flow [1, 2, 16, 16]
        flow_nchw = np.ones((1, 2, 16, 16), dtype=np.float32)
        flow_nchw[:, 1] = -2.5
        s.put_flow("raft.flow01", flow_nchw, src_chart="sam16", dst_chart="sam16", layout="NCHW")
        # 4. CoTracker sparse point trajectories [T=2, P=5, 2] + visibility + WarpGrid
        tracks = np.zeros((2, 5, 2), dtype=np.float32)
        vis = np.ones((2, 5), dtype=np.uint8)
        s.put_sparse_tracks("cotracker.pts", tracks, visibility=vis, chart="sam16")
        wgrid = np.zeros((2, 4, 5, 2), dtype=np.float32)
        s.put_warp_grid("gyro.warp", wgrid, src_chart="active_1x")
        s.save(nib_path)

    with NijinSession.open(nib_path) as loaded:
        mh, mw, decoded = loaded["sam.subject_rle"].decode_rle_mask()
        assert (mh, mw) == (16, 16)
        assert np.frombuffer(decoded, dtype=np.uint8).reshape(16, 16).tolist() == mask_2d.tolist()
        assert loaded["sam.subject_rle"].descriptor["affine"] == pytest.approx(sam_c.affine)
        assert loaded["sam.multi_logits"].shape == (2, 2, 3, 8, 8)
        assert loaded["sam.multi_logits"].meta["candidate_scores"] == pytest.approx([0.71, 0.96, 0.84])
        assert loaded["raft.flow01"].shape == (1, 16, 16, 2)
        assert loaded["cotracker.pts.vis"].shape == (2, 5)
        assert loaded["gyro.warp"].shape == (2, 4, 5, 2)


def test_multi_mask_role_policies_and_cross_stage_dirty_dag(tmp_path):
    p_opt, p_align = tmp_path / "optics_patch.nib", tmp_path / "align_patch.nib"
    with NijinSession.host_open(frames=["f0", "f1", "f2"], active_wh=[32, 24],
                                geometry_id="raw:burst1", stage="S3_FUSE") as host_sess:
        assert host_sess.cached_stages() == ["S1_CALIB", "S2_ALIGN", "S3_FUSE"]
        # External script prepares S4_OPTICS patch with two distinct masks (subject vs streetlamps)
        with NijinSession.create(frames=["f0", "f1", "f2"], active_wh=[32, 24],
                                 geometry_id="raw:burst1", stage="EXT_INPUT") as ext:
            ext.put_rle_mask("mask.subject", [384, 384], mask_hw=(24, 32), target_stage="S4_OPTICS")
            ext.put_rle_mask("mask.lamps", [700, 68], mask_hw=(24, 32), target_stage="S4_OPTICS")
            ext.set_optics_policy(protect_mask="mask.subject", emitter_mask="mask.lamps",
                                  bokeh_de_nisen=0.85, spectral_ca="axial-aura", spectral_ca_power=6.0)
            ext.save(p_opt)

        delta1 = host_sess.attach(p_opt)
        # S1..S3 remain cached! Only S4..S5 need execution!
        assert delta1["cached_stages"] == ["S1_CALIB", "S2_ALIGN", "S3_FUSE"]
        assert delta1["invalidated_stages"] == ["S4_OPTICS", "S5_DELIVER"]
        assert host_sess.policy["optics"]["protect_mask"] == "mask.subject"
        assert host_sess.policy["optics"]["emitter_mask"] == "mask.lamps"

        host_sess.mark_executed_through("S5_DELIVER")
        assert host_sess.invalidated_stages() == []

        # Now external script attaches an S2_ALIGN motion field + S3_FUSE passerby exclusion
        with NijinSession.create(frames=["f0", "f1", "f2"], active_wh=[32, 24],
                                 geometry_id="raw:burst1", stage="EXT_INPUT") as ext2:
            ext2.put_rle_mask("mask.passerby", [500, 268], mask_hw=(24, 32), target_stage="S3_FUSE")
            ext2.put("flow.cand", array("f", [0.5, -0.25] * (24 * 32)), shape=(1, 24, 32, 2),
                     grid={"kind": 1}, chart="active_1x", frame=0, semantic="flow.fwd@1",
                     unit="external_px", origin=2, target_stage="S2_ALIGN",
                     meta={"to_frame": 1, "to_affine": list(wire.IDENTITY)})
            ext2.add_exclusion(mask="mask.passerby", frames=[1, 2])
            ext2.set_align_policy(flow="flow.cand", validation="chi2_gated")
            ext2.save(p_align)

        delta2 = host_sess.attach(p_align)
        assert delta2["cached_stages"] == ["S1_CALIB"]
        assert delta2["invalidated_stages"] == ["S2_ALIGN", "S3_FUSE", "S4_OPTICS", "S5_DELIVER"]
        assert host_sess.policy["fuse"]["exclusions"][0]["frames"] == [1, 2]


def test_origin_spoofing_blocked_and_live_client_generative_provenance(tmp_path):
    with session() as s:
        with pytest.raises(BusError, match="origin=0"):
            s.put("fake_sensor", array("f", [1.0]), semantic="sensor.reading@1", origin=0)

    # Host-side attach() must also reject an external bundle carrying origin=0!
    spoof_nib = tmp_path / "spoof.nib"
    with session() as spoof_writer:
        spoof_writer.put_host_field("forged", array("f", [1.0]), semantic="sensor.reading@1")
        spoof_writer.save(spoof_nib)
    with session() as receiver:
        with pytest.raises(BusError, match="origin=0"):
            receiver.attach(spoof_nib)

    # Unicode UTF-8 source paths work seamlessly via canonical_frame_ids + reference host:
    with NijinClient(backend="reference") as client:
        with client.open_burst(["/样本目录/DSC0001.ARW", "/样本目录/DSC0002.ARW"], active_wh=(4, 2)) as live:
            snap = live.get_fields(["host.proxy_rgb"])
            assert snap["host.proxy_rgb"].origin == 0
            assert snap.session_meta["source_paths"][0] == "/样本目录/DSC0001.ARW"
            # Reference host already holds host.proxy_rgb (origin=0, shape=(1, 2, 4, 3))!
            flux = array("f", [0.9, 0.5, 0.1] * 8)
            live.local.put_prior_rgb("flux.fill", flux, shape=(1, 2, 4, 3), chart="proxy_linear")
            hole_w = array("d", [0., 0., 1., 1., 0., 0., 1., 1.])  # F64 weight!
            live.local.put_mask("hole.w4", hole_w, shape=(2, 4),
                                chart="proxy_linear", semantic="mask.hole_weight@1")
            live.local.put_rle_mask("fg.rle", [2, 1, 5], mask_hw=(2, 4), order="C", chart="proxy_linear")
            live.local.set_optics_policy(protect_mask="fg.rle", bokeh_de_nisen=0.85)
            # Partial sync must preserve policy + chart closure!
            delta = live.sync_to_host(names=["flux.fill", "hole.w4", "fg.rle"])
            assert delta["policy"]["optics"]["protect_mask"] == "fg.rle"

            with live.call("nijin.optics.composite_prior@1", {
                "base_rgb": "host.proxy_rgb",
                "prior_rgb": "flux.fill",
                "weight": "hole.w4",
                "protect_mask": "fg.rle",
            }) as res:
                rgb = list(struct.iter_unpack("<3f", res["rgb"].read()))
                prov = list(res["provenance"].read())
                assert rgb[0] == pytest.approx((0.18, 0.18, 0.18)) and prov[0] == PROV_MEASURED
                assert rgb[2] == pytest.approx((0.18, 0.18, 0.18)) and prov[2] == PROV_MEASURED
                assert rgb[3] == pytest.approx((0.9, 0.5, 0.1)) and prov[3] == PROV_GENERATIVE
                assert rgb[6] == pytest.approx((0.9, 0.5, 0.1)) and prov[6] == PROV_GENERATIVE


def test_round2_audit_discriminators_sampling_and_state_machine(tmp_path):
    host = default_host()
    # 1. Astra §2.1 4x1 equivalence across U8, F32, and 180-deg rotated RLE + bytes COCO RLE:
    with NijinSession.host_open(frames=["f0"], active_wh=[4, 1], geometry_id="g0", stage="S4_OPTICS") as s:
        s.put_host_field("base", array("f", [0.2, 0.2, 0.2] * 4), shape=(1, 1, 4, 3),
                         grid={"kind": 0}, chart="active_1x", semantic="radiance.linear_rgb@1", unit="photon_linear")
        s.put_prior_rgb("rep", array("f", [0.8, 0.8, 0.8] * 4), shape=(1, 1, 4, 3), chart="active_1x")
        s.put_mask("m_u8", bytes([0, 1, 0, 1]), shape=(1, 4), chart="active_1x", semantic="mask.binary@1")
        s.put_mask("m_f32", array("f", [0., 1., 0., 1.]), shape=(1, 4), chart="active_1x")
        rot180 = s.charts["active_1x"].rotate90(2, name="rot180")
        s.register_chart("rot180", rot180)
        # First point in rotated grid is 1 -> active last point is protected!
        s.put_rle_mask("m_rle180", [0, 1, 3], mask_hw=(1, 4), order="C", chart="rot180")
        for mname, expected_r in (("m_u8", [0.8, 0.2, 0.8, 0.2]),
                                  ("m_f32", [0.8, 0.2, 0.8, 0.2]),
                                  ("m_rle180", [0.8, 0.8, 0.8, 0.2])):
            with host.call("nijin.optics.composite_prior@1", s,
                           {"base_rgb": "base", "prior_rgb": "rep", "protect_mask": mname}) as out:
                reds = [px[0] for px in struct.iter_unpack("<3f", out["rgb"].read())]
                assert reds == pytest.approx(expected_r)

        # Rotated 180-deg prior_rgb Chart resampling + stacked 0x12 provenance check:
        s.put_prior_rgb("rep_rot180", array("f", [1., 0., 0., 1., 0., 0., 0., 0., 0., 0., 0., 0.]),
                        shape=(1, 1, 4, 3), chart="rot180", origin=4)
        with host.call("nijin.optics.composite_prior@1", s,
                       {"base_rgb": "base", "prior_rgb": "rep_rot180"}) as out_r:
            reds_r = [px[0] for px in struct.iter_unpack("<3f", out_r["rgb"].read())]
            assert reds_r == pytest.approx([0., 0., 1., 1.])
            s.put("step1.rgb", out_r["rgb"].read(), dtype="f32", shape=(1, 1, 4, 3),
                  axes=("batch", "y", "x", "channel"), grid={"kind": 0}, chart="active_1x",
                  semantic="radiance.linear_rgb@1", unit="photon_linear", origin=4)
            s.put_host_field("step1.prov", out_r["provenance"].read(), dtype="u8", shape=(1, 1, 4, 1),
                             axes=("batch", "y", "x", "channel"), grid={"kind": 0}, chart="active_1x",
                             semantic="provenance.bitmask@1", unit="bitmask")
        s.put_prior_rgb("cal_p", array("f", [0.6] * 12), shape=(1, 1, 4, 3), chart="active_1x", origin=1)
        s.put_mask("w_half", array("f", [0.5] * 4), shape=(1, 4), chart="active_1x", semantic="mask.weight@1")
        with host.call("nijin.optics.composite_prior@1", s,
                       {"base_rgb": "step1.rgb", "base_provenance": "step1.prov",
                        "prior_rgb": "cal_p", "weight": "w_half"}) as out_s:
            assert list(out_s["provenance"].read()) == [0x12, 0x12, 0x12, 0x12]
            assert out_s["rgb"].origin == 4

        # pycocotools bytes counts=b"022" on 2x2:
        c22 = Chart.active(2, 2, name="c22")
        s.register_chart("c22", c22)
        s.put_rle_mask("coco_bytes", {"size": [2, 2], "counts": b"022"}, chart="c22", order="C")
        assert list(s["coco_bytes"].decode_rle_mask()[2]) == [1, 1, 0, 0]

    # 2. Fractional valid_bbox discrete center clamping (§2.1):
    frac_c = Chart(wire.IDENTITY, (0., 0., 2.4, 1.0), (3, 1), name="frac")
    assert frac_c.sample_at_active(2.3, 0.5) == pytest.approx((1.5, 0.5))  # NOT 1.9!
    empty_center_c = Chart(wire.IDENTITY, (0.8, 0., 1.2, 1.0), (2, 1), name="empty_c")
    assert empty_center_c.sample_at_active(1.0, 0.5) is None

    # 3. State machine (§2.2): S5 -> S2 update -> mark_executed_through(S2) keeps S3..S5 dirty!
    with NijinSession.host_open(frames=["f0", "f1"], active_wh=[4, 4], geometry_id="g1", stage="S5_DELIVER") as s:
        s.put_rle_mask("m1", [8, 8], mask_hw=(4, 4), target_stage="S4_OPTICS")
        s.set_optics_policy(protect_mask="m1", bokeh_de_nisen=1.0)
        # Patching bokeh_swirl must NOT clobber protect_mask or bokeh_de_nisen:
        s.set_optics_policy(bokeh_swirl=0.3)
        assert s.policy["optics"]["protect_mask"] == "m1"
        assert s.policy["optics"]["bokeh_de_nisen"] == pytest.approx(1.0)
        assert s.policy["optics"]["bokeh_swirl"] == pytest.approx(0.3)
        s.mark_executed_through("S5_DELIVER")
        assert s.invalidated_stages() == []

        # S2 update followed by partial S2 execution:
        s.put("f_s2", array("f", [0., 0.] * 16), shape=(1, 4, 4, 2), grid={"kind": 1},
              chart="active_1x", frame=0, semantic="flow.fwd@1", unit="external_px", origin=2,
              target_stage="S2_ALIGN", meta={"to_frame": 1, "to_affine": list(wire.IDENTITY)})
        assert s.cached_stages() == ["S1_CALIB"]
        s.mark_executed_through("S2_ALIGN")
        assert s.cached_stages() == ["S1_CALIB", "S2_ALIGN"]
        assert s.invalidated_stages() == ["S3_FUSE", "S4_OPTICS", "S5_DELIVER"]

    # 4. adopt_outputs rename preserved across export (§2.2):
    info = dispatch(host, {"api": "nopb/1", "method": "open_session", "frames": ["a", "b"],
                           "active_wh": [4, 4], "geometry_id": "g2"})
    sid = info["session_handle"]
    s_live = host.get_live_session(sid)
    measurements(s_live)
    dispatch(host, {"api": "nopb/1", "method": "call", "session_handle": sid,
                    "op": "nijin.sensor.gaussian_nll_grad@1",
                    "inputs": {"mu": "a", "y": "b", "variance": "v"},
                    "adopt_outputs": {"grad": "my.grad"}})
    exp_p = tmp_path / "adopted_exp.nib"
    dispatch(host, {"api": "nopb/1", "method": "export", "session_handle": sid,
                    "names": ["my.grad"], "output": str(exp_p)})
    with NijinSession.open(exp_p) as re_read:
        assert "my.grad" in re_read.fields
        assert struct.unpack("<d", re_read["my.grad"].read()) == (-.75,)
    from nijin_bus import _default_host_command
    assert _default_host_command("auto")[-1] in ("serve", "--bus-serve")
    assert _default_host_command("native")[-1] == "--bus-serve"
    with session("S4_OPTICS") as s_inv:
        c_mod = Chart.active(4, 4, name="mod")
        s_inv.register_chart("mod", c_mod)
        s_inv.put_mask("m_mod", array("f", [0., 1.] * 8), shape=(4, 4), chart="mod", target_stage="S4_OPTICS")
        s_inv.mark_executed_through("S5_DELIVER")
        assert s_inv.invalidated_stages() == []
        s_inv.register_chart("mod", c_mod.rotate90(1))
        assert s_inv.invalidated_stages() == ["S4_OPTICS", "S5_DELIVER"]
    dispatch(host, {"api": "nopb/1", "method": "close_session", "session_handle": sid})


@pytest.mark.skipif(shutil.which("clang++") is None, reason="clang++ not installed")
def test_cpp20_nib_v1_header_wire_roundtrip(tmp_path):
    nib_file = tmp_path / "cpp_check.nib"
    with NijinSession.host_open(frames=["r0", "r1"], active_wh=[64, 48], geometry_id="cfa:rggb") as s:
        c = s.charts["active_1x"].letterbox(32, align="top_left")
        s.register_chart("lb32", c)
        s.put_rle_mask("sam.rle", [100, 400, 524], mask_hw=(32, 32), chart="lb32")
        roi = s.charts["active_1x"].crop(10, 20, 13, 22, name="roi3x2")
        s.put_prior_rgb("gen.rgb", array("f", [0.25, 0.5, 0.75] * 6), shape=(1, 2, 3, 3), chart=roi)
        s.put("sam.pts", array("f", [12.5, 21.25, 11.0, 20.5]), shape=(2, 2), axes=("point", "xy"),
              chart=roi, frame=1, semantic="geometry.external_xy@1",
              unit="external_px", origin=2)
        s.save(nib_file)

    cpp_src = tmp_path / "reader.cpp"
    cpp_bin = tmp_path / "reader"
    cpp_src.write_text(
        '#include <cassert>\n'
        '#include <cmath>\n'
        '#include <cstdio>\n'
        '#include <fstream>\n'
        '#include <vector>\n'
        f'#include "{(SDK / "nib_v1.h").as_posix()}"\n'
        'int main(int argc, char** argv) {\n'
        '    assert(argc == 2);\n'
        '    std::ifstream f(argv[1], std::ios::binary);\n'
        '    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), {});\n'
        '    assert(buf.size() >= NIB1_HEADER_BYTES);\n'
        '    assert(memcmp(buf.data() + NIB1_H_MAGIC, "NIJBUS01", 8) == 0);\n'
        '    assert(nib1_u32(buf.data() + NIB1_H_VERSION) == 1);\n'
        '    assert(nib1_u32(buf.data() + NIB1_H_WIDTH) == 64);\n'
        '    assert(nib1_u32(buf.data() + NIB1_H_HEIGHT) == 48);\n'
        '    uint32_t count = nib1_u32(buf.data() + NIB1_H_COUNT);\n'
        '    assert(count == 3);\n'
        '    uint64_t dir = nib1_u64(buf.data() + NIB1_H_DIRECTORY);\n'
        '    const uint8_t* d0 = buf.data() + dir;\n'
        '    const uint8_t* d1 = d0 + NIB1_DESC_BYTES;\n'
        '    const uint8_t* d2 = d1 + NIB1_DESC_BYTES;\n'
        '    assert(nib1_u16(d0 + NIB1_D_KIND) == NIB1_ND);\n'
        '    assert(nib1_u16(d0 + NIB1_D_ORIGIN) == NIB1_ORIG_PARTITION_MASK);\n'
        '    assert(std::fabs(nib1_f32(d0 + NIB1_D_AFFINE) - 2.0f) < 1e-6f);\n'
        '    uint64_t off0 = nib1_u64(d0 + NIB1_D_OFFSET);\n'
        '    assert((off0 % NIB1_PAYLOAD_ALIGN) == 0);\n'
        '    assert(nib1_u32(buf.data() + off0 + 4) == 400u);\n'
        '    assert(nib1_u16(d1 + NIB1_D_ORIGIN) == NIB1_ORIG_GENERATIVE_PRIOR);\n'
        '    assert(std::fabs(nib1_f32(d1 + NIB1_D_AFFINE + 8) - 10.0f) < 1e-6f);\n'
        '    assert(std::fabs(nib1_f32(d1 + NIB1_D_AFFINE + 20) - 20.0f) < 1e-6f);\n'
        '    uint64_t off1 = nib1_u64(d1 + NIB1_D_OFFSET);\n'
        '    assert(std::fabs(nib1_f32(buf.data() + off1 + 8) - 0.75f) < 1e-6f);\n'
        '    assert(nib1_u16(d2 + NIB1_D_KIND) == NIB1_ND);\n'
        '    assert(static_cast<int32_t>(nib1_u32(d2 + NIB1_D_FRAME)) == 1);\n'
        '    uint64_t off2 = nib1_u64(d2 + NIB1_D_OFFSET);\n'
        '    assert(std::fabs(nib1_f32(buf.data() + off2 + 0) - 12.5f) < 1e-6f);\n'
        '    assert(std::fabs(nib1_f32(buf.data() + off2 + 4) - 21.25f) < 1e-6f);\n'
        '    assert(std::fabs(nib1_f32(buf.data() + off2 + 8) - 11.0f) < 1e-6f);\n'
        '    assert(std::fabs(nib1_f32(buf.data() + off2 + 12) - 20.5f) < 1e-6f);\n'
        '    return 0;\n'
        '}\n',
        encoding="ascii",
    )
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
                    str(cpp_src), "-o", str(cpp_bin)], check=True)
    subprocess.run([str(cpp_bin), str(nib_file)], check=True)


def test_inprocess_cli_and_rpc_coverage(tmp_path):
    ex_nib = tmp_path / "ex.nib"
    wire.main(["example", str(ex_nib)])
    wire.main(["inspect", str(ex_nib)])

    host = default_host()
    src, dst = tmp_path / "src.nib", tmp_path / "dst.nib"
    with session("S4_OPTICS") as s:
        measurements(s)
        c2x2 = s.charts["active_1x"].crop(0, 0, 2, 2, name="c2x2")
        s.put_prior_rgb("prior", array("f", [0.8, 0.7, 0.6] * 4), shape=(1, 2, 2, 3), chart=c2x2)
        s.put_host_field("base2", array("f", [0.1, 0.2, 0.3] * 4), shape=(1, 2, 2, 3),
                         axes=("batch", "y", "x", "channel"), grid={"kind": 0},
                         chart="c2x2", semantic="radiance.linear_rgb@1", unit="photon_linear")
        s.put_rle_mask("mask2", [1, 1, 2], mask_hw=(2, 2), order="C", chart="c2x2")
        s.save(src)

    info = dispatch(host, {"api": "nopb/1", "method": "open_session", "frames": ["a", "b"],
                           "active_wh": [8, 6], "geometry_id": "crop.0", "stage": "S4_OPTICS"})
    sid = info["session_handle"]
    dispatch(host, {"api": "nopb/1", "method": "attach", "session_handle": sid,
                    "input": str(src), "_trusted_host": True})
    assert "base2" in dispatch(host, {"api": "nopb/1", "method": "session_describe", "session_handle": sid})["fields"]
    assert "base2" in dispatch(host, {"api": "nopb/1", "method": "inspect", "session_handle": sid})["fields"]
    dispatch(host, {"api": "nopb/1", "method": "call", "session_handle": sid,
                    "op": "nijin.sensor.gaussian_nll_grad@1",
                    "inputs": {"mu": "a", "y": "b", "variance": "v"},
                    "adopt_outputs": {"grad": "saved_grad"}})
    dispatch(host, {"api": "nopb/1", "method": "call", "session_handle": sid,
                    "op": "nijin.optics.composite_prior@1",
                    "inputs": {"base_rgb": "base2", "prior_rgb": "prior", "protect_mask": "mask2"},
                    "output": str(dst)})
    exp = tmp_path / "exported.nib"
    dispatch(host, {"api": "nopb/1", "method": "export", "session_handle": sid, "output": str(exp)})
    from nijin_bus import _default_host_command
    assert _default_host_command("auto")[-1] in ("serve", "--bus-serve")
    assert _default_host_command("native")[-1] == "--bus-serve"
    with session("S4_OPTICS") as s_inv:
        c_mod = Chart.active(4, 4, name="mod")
        s_inv.register_chart("mod", c_mod)
        s_inv.put_mask("m_mod", array("f", [0., 1.] * 8), shape=(4, 4), chart="mod", target_stage="S4_OPTICS")
        s_inv.mark_executed_through("S5_DELIVER")
        assert s_inv.invalidated_stages() == []
        s_inv.register_chart("mod", c_mod.rotate90(1))
        assert s_inv.invalidated_stages() == ["S4_OPTICS", "S5_DELIVER"]
    dispatch(host, {"api": "nopb/1", "method": "close_session", "session_handle": sid})

    out_buf = io.StringIO()
    bus_main(["describe"], outstream=out_buf)
    bus_main(["inspect", str(dst)], outstream=out_buf)
    serve_in = io.StringIO("\nnot-json\n" + json.dumps({"api": "nopb/1", "id": 1, "method": "describe"}) + "\n")
    serve_out = io.StringIO()
    bus_main(["serve"], instream=serve_in, outstream=serve_out)
    lines = [json.loads(x) for x in serve_out.getvalue().splitlines()]
    assert lines[0]["ok"] is False and lines[1]["ok"] is True
    with NijinClient(backend="reference") as ref_cli:
        assert ref_cli.describe()["api"] == "nopb/1"

