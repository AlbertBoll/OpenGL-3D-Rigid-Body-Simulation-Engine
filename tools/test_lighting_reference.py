"""Phase 62: production lighting/material references, repeatability and defect controls."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = {"maximum_channel_codes": 2, "mean_absolute_channel_codes": .1,
             "minimum_local_luminance_ssim": .9995}


def ppm(path):
    header, dimensions, maximum, data = path.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if header != b"P6" or maximum != b"255" or len(data) != width * height * 3:
        raise ValueError("Invalid capture: " + str(path))
    return width, height, data


def png(path):
    width, height, data = ppm(path)
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
    scan = b"".join(b"\0" + data[y*width*3:(y+1)*width*3] for y in range(height))
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                                      + chunk(b"IDAT", zlib.compress(scan)) + chunk(b"IEND", b""))


def compare(left, right):
    w, h, a = ppm(left)
    rw, rh, b = ppm(right)
    if (w, h) != (rw, rh):
        raise ValueError("Capture dimensions differ")
    errors = [[abs(x-y) for x, y in zip(a[c::3], b[c::3])] for c in range(3)]
    maxima = [max(e) for e in errors]
    means = [sum(e)/len(e) for e in errors]
    luminance = lambda p: [(p[i]*.2126 + p[i+1]*.7152 + p[i+2]*.0722)/255 for i in range(0, len(p), 3)]
    la, lb = luminance(a), luminance(b)
    ssim = []
    for y in range(0, h, 8):
        for x in range(0, w, 8):
            indices = [j*w+i for j in range(y, min(y+8, h)) for i in range(x, min(x+8, w))]
            aa, bb = [la[i] for i in indices], [lb[i] for i in indices]
            ma, mb = sum(aa)/len(aa), sum(bb)/len(bb)
            va, vb = sum((v-ma)**2 for v in aa)/len(aa), sum((v-mb)**2 for v in bb)/len(bb)
            cov = sum((u-ma)*(v-mb) for u, v in zip(aa, bb))/len(aa)
            ssim.append(((2*ma*mb+.01**2)*(2*cov+.03**2))/((ma*ma+mb*mb+.01**2)*(va+vb+.03**2)))
    passed = max(maxima) <= TOLERANCE["maximum_channel_codes"] and max(means) <= TOLERANCE["mean_absolute_channel_codes"] and min(ssim) >= TOLERANCE["minimum_local_luminance_ssim"]
    return {"result": "PASS" if passed else "FAIL", "channel_max": maxima, "channel_mae": means,
            "minimum_local_luminance_ssim": min(ssim), "luminance_rmse": math.sqrt(sum((x-y)**2 for x, y in zip(la, lb))/len(la)),
            "left": str(left), "right": str(right)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=["Debug", "Release"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compare", type=Path, help="Other configuration's completed run-0 directory")
    parser.add_argument("--probe", type=Path, help="Development only: reuse a compiled candidate probe; no build/integration claim")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = {"result": "FAIL", "configuration": args.configuration, "tolerance": TOLERANCE, "steps": [], "comparisons": [], "development_probe": bool(args.probe)}
    def run(label, command, cwd, env, expected=None, timeout=240):
        command = list(map(str, command))
        with (out/(label+".log")).open("wb") as stream:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        log = (out/(label+".log")).read_text(errors="replace")
        passed = result.returncode == 0 and "[PASS]" in log if expected is None else result.returncode != 0 and expected in log
        report["steps"].append({"command": command, "cwd": str(cwd), "log": str(out/(label+".log")), "exit": result.returncode,
                                "expected_failure": expected, "result": "PASS" if passed else "FAIL"})
        print(f"[{'PASS' if passed else 'FAIL'}] {label}: exit={result.returncode}", flush=True)
        return passed
    try:
        env = dict(os.environ)
        env.pop("GENGINE_LIGHTING_REFERENCE", None)
        if not args.probe:
            if not run("integration", [sys.executable, ROOT/"tools/test_frame_submission.py", "--configuration", args.configuration,
                                       "--output", out/"integration", "--smoke"], ROOT, env, timeout=1800):
                return 1
            probe = out/"integration/frame-submission-probe.exe"
        else:
            probe = args.probe.resolve()
        report["source_sha256"] = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in
            (ROOT/"tools/frame_submission_probe.cpp", ROOT/"tools/test_lighting_reference.py", ROOT/"GEngine/src/Renderer/FrameSubmission.cpp",
             ROOT/"GEngine/src/Renderer/SubmissionGpuLayout.h", ROOT/"GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag")}
        report["probe_sha256"] = hashlib.sha256(probe.read_bytes()).hexdigest()
        assets = ROOT/"bin"/args.configuration/"assets"
        shader = assets/"Shaders/pbr_cascade_shadow.frag"
        source = ROOT/"GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag"
        if shader.read_bytes() != source.read_bytes():
            raise ValueError("Staged PBR shader differs from candidate")
        env["GENGINE_ASSET_ROOT"] = str(assets)
        env["GENGINE_LIGHTING_REFERENCE"] = "1"
        env["GENGINE_SHADOW_RESOLUTION"] = "256"
        path_key = next(k for k in env if k.lower() == "path")
        env[path_key] = str(ROOT/"bin"/args.configuration/"GEngineEditor") + os.pathsep + env[path_key]
        for index in range(3):
            directory = out/f"run-{index}"
            directory.mkdir(exist_ok=True)
            if not run(f"reference-{index}", [probe], directory, env):
                return 1
            for reference in ([out/"run-0"] if index else []) + ([args.compare] if index == 0 and args.compare else []):
                if (directory/"shadow-depth.f32").read_bytes() != (reference/"shadow-depth.f32").read_bytes():
                    raise ValueError("Same-GPU raw shadow depth differs between captures")
            captures = sorted(directory.glob("*.ppm"))
            if len(captures) < 25:
                raise ValueError("Incomplete capture set")
            for capture in captures:
                png(capture)
                if index:
                    report["comparisons"].append(compare(out/"run-0"/capture.name, capture))
                elif args.compare:
                    report["comparisons"].append(compare(args.compare/capture.name, capture))
        def private_assets(destination):
            # Link unchanged assets read-only; copy the one shader that controls mutate.
            def stage(source_path, target_path):
                if Path(source_path).name == "pbr_cascade_shadow.frag":
                    return shutil.copy2(source_path, target_path)
                os.link(source_path, target_path)
                return target_path
            shutil.copytree(assets, destination, copy_function=stage)
        compatibility = out/"prior-shader"
        private_assets(compatibility/"assets")
        prior = subprocess.check_output(["git", "show", "render-refactor-phase-61-approved:GEngine/include/GEngine/Assets/Shaders/pbr_cascade_shadow.frag"], cwd=ROOT)
        (compatibility/"assets/Shaders/pbr_cascade_shadow.frag").write_bytes(prior)
        prior_env = dict(env, GENGINE_ASSET_ROOT=str(compatibility/"assets"), GENGINE_LIGHTING_REFERENCE_COMPATIBILITY="1")
        if not run("prior-shader", [probe], compatibility, prior_env):
            return 1
        for name in ("ambient", "point", "directional"):
            result = compare(compatibility/(name+".ppm"), out/"run-0"/(name+".ppm"))
            if any(result["channel_max"]):
                result["result"] = "FAIL"
            report["comparisons"].append(result)
        report["prior_shader_sha256"] = hashlib.sha256(prior).hexdigest()
        text = source.read_text()
        controls = [
            ("missing-spot", text.replace("if (frameLights && frameSpot)", "if (false)"), "[FAIL] distinct contributing light references"),
            ("missing-cone", text.replace("float cone = spotInnerCos == spotOuterCos ? step(spotOuterCos, cosine)", "float cone = true ? 1.0"), "[FAIL] spot independent cone/range and point-BRDF oracle"),
            ("missing-range", text.replace("float rangeWeight = 1.0 - distanceToSpot / spotRange;", "float rangeWeight = 1.0;"), "[FAIL] spot independent cone/range and point-BRDF oracle")]
        for label, mutated, expected in controls:
            if mutated == text:
                raise ValueError("Control did not change shader")
            directory = out/"controls"/label
            private_assets(directory/"assets")
            (directory/"assets/Shaders/pbr_cascade_shadow.frag").write_text(mutated)
            control_env = dict(env, GENGINE_ASSET_ROOT=str(directory/"assets"))
            if not run(label, [probe], directory, control_env, expected):
                return 1
        # Comparator negative control: a brightness shift must fail the declared metrics.
        control = out/"comparison-control.ppm"
        w, h, raw = ppm(out/"run-0/spot.ppm")
        control.write_bytes(f"P6\n{w} {h}\n255\n".encode() + bytes(min(255, v+8) for v in raw))
        report["comparator_control"] = compare(out/"run-0/spot.ppm", control)
        if report["comparator_control"]["result"] != "FAIL":
            raise ValueError("Comparator accepted a brightness regression")
        images = sorted((out/"run-0").glob("*.png"))
        (out/"reference-gallery.html").write_text('<!doctype html><meta charset="utf-8"><title>Phase 62 references</title><style>body{background:#202124;color:#eee;font:16px sans-serif}main{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0;width:256px}figcaption{overflow-wrap:anywhere}img{width:256px}</style><h1>Phase 62 '+args.configuration+'</h1><p>Frozen production output. Camera, materials, lighting, driver and limits are in run-0/reference-metadata.txt.</p><main>'+''.join('<figure><img src="run-0/'+p.name+'"><figcaption>'+p.stem+'</figcaption></figure>' for p in images)+'</main>')
        report["images"] = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.glob("run-*/*")) if p.is_file()}
        report["result"] = "PASS" if all(c["result"] == "PASS" for c in report["comparisons"]) else "FAIL"
        return 0 if report["result"] == "PASS" else 1
    finally:
        (out/"results.json").write_text(json.dumps(report, indent=2)+"\n")
        print(f"[{report['result']}] {out/'results.json'}", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
