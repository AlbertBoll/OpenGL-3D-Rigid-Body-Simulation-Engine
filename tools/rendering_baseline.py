"""Phase 13 Windows Release baseline; opt-in instrumentation, frozen workloads, retained raw samples."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys

from rendering_validation import ROOT, toolchain

APPS = ('GEngineEditor', 'RigidBodySimulation', 'Breakout', 'RayTracing')
TIMINGS = ('frame_ns', 'work_ns', 'input_ns', 'update_ns', 'render_ns', 'physics_ns', 'readback_ns')
LAYOUT = ''.join(f'[Window][{name}]\nPos={x},{y}\nSize={w},{h}\nCollapsed=0\n\n'
                 for name, x, y, w, h in [('Viewport', 380, 30, 880, 660), ('Setting', 10, 30, 360, 260),
                                         ('Scene', 10, 300, 360, 390)])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def environment():
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'
           and not k.startswith(('GENGINE_', 'SDL_', 'ASAN_'))}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    return env


def invoke(command, out, label, env, cwd=ROOT, timeout=1200):
    command = list(map(str, command))
    record = dict(command=command, cwd=str(cwd), log=str(out / (label + '.log')))
    with Path(record['log']).open('wb') as log:
        try:
            result = subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            record['exit'] = result.returncode
        except subprocess.TimeoutExpired:
            record.update(exit=None, reason='Timeout; owned child terminated')
    record['result'] = 'PASS' if record['exit'] == 0 else 'FAIL'
    (out / (label + '.json')).write_text(json.dumps(record, indent=2) + '\n')
    print(f"[{record['result']}] {label}: exit={record['exit']}", flush=True)
    if record['exit'] != 0:
        raise RuntimeError('Failed command; see ' + record['log'])
    return record


def samples(directory):
    with (directory / 'frames.csv').open(newline='') as file:
        rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(file)]
    if len(rows) != 240 or [r['sample'] for r in rows] != list(range(240)):
        raise ValueError('Incomplete sample sequence: ' + str(directory))
    if any(any(value < 0 for value in row.values()) or row['physics_steps'] != 0
           or row['physics_ns'] != 0 or row['target_reallocations'] != 0 for row in rows):
        raise ValueError('Frozen workload/steady allocation contract failed: ' + str(directory))
    if any(r['frame_ns'] < r['work_ns'] or r['work_ns'] != r['input_ns'] + r['update_ns'] + r['render_ns'] for r in rows):
        raise ValueError('Timing decomposition failed: ' + str(directory))
    metadata = dict(line.split('=', 1) for line in (directory / 'runtime.txt').read_text().splitlines())
    for key, value in dict(width='1280', height='720', vsync='0', warmup='120', samples='240', completed_samples='240').items():
        if metadata.get(key) != value:
            raise ValueError('Runtime setting mismatch: ' + key)
    if not (directory / 'diagnostic.bmp').is_file():
        raise ValueError('Missing diagnostic image')
    return rows, metadata


def quantiles(values):
    ordered = sorted(values)
    return dict(median=statistics.median(ordered), p95=ordered[math.ceil(len(ordered) * .95) - 1],
                p99=ordered[math.ceil(len(ordered) * .99) - 1], minimum=ordered[0], maximum=ordered[-1])


def image_region(path):
    # Read the diagnostic BMP without an image-library dependency or modifying it.
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    if data[:2] != b'BM' or width != 1280 or abs(height) != 720 or bits not in (24, 32):
        raise ValueError('Unexpected diagnostic image format: ' + str(path))
    pixel_bytes = bits // 8
    stride = (width * pixel_bytes + 3) & ~3
    # Common interior of the seeded viewport layout; excludes UI controls/timing text.
    pixels = []
    for y in range(80, 650):
        row = offset + (719 - y if height > 0 else y) * stride
        pixels.extend(data[row + x * pixel_bytes:row + x * pixel_bytes + 3] for x in range(400, 1240))
    if len(set(pixels)) < 16:
        raise ValueError('Diagnostic viewport is blank or lacks a representative scene: ' + str(path))
    return pixels


def summarize(out, repeats, applications=APPS):
    report = dict(protocol='phase13-frozen-v1', repeats=repeats, warmup_frames=120, samples_per_run=240,
                  noise_policy='Run median spread >10% is NOISY; use exact workload counters as fallback. No speedup claim.',
                  common_reference='Phase 65 compares common metrics to Phase 13; Phase 48 establishes later per-pass metrics.',
                  applications={})
    for app in applications:
        runs = [samples(out / app / str(index)) for index in range(repeats)]
        hardware = [{k: v for k, v in meta.items() if k.startswith('GL')} for _, meta in runs]
        if any(item != hardware[0] for item in hardware):
            raise ValueError('Hardware mismatch across runs')
        counters = [key for key in runs[0][0][0] if key not in TIMINGS and key != 'sample']
        all_rows = [row for rows, _ in runs for row in rows]
        # Bind/readback frequency can be input dependent; any drift invalidates a frozen reference.
        signature = {key: all_rows[0][key] for key in counters}
        if any(any(row[key] != signature[key] for key in counters) for row in all_rows):
            raise ValueError('Non-timing workload drift: ' + app)
        image_name = 'scene.bmp' if app == 'GEngineEditor' else 'diagnostic.bmp'
        images = [image_region(out / app / str(index) / image_name) for index in range(repeats)]
        fractions = [sum(a != b for a, b in zip(images[0], current)) / len(current) for current in images]
        # Diagnostic stability only, on this one GPU/driver; never a future visual golden.
        if max(fractions) > .005:
            raise ValueError('Frozen viewport differs by more than 0.5% of pixels across processes: ' + app)
        timing = {}
        for key in TIMINGS:
            medians = [statistics.median(row[key] for row in rows) for rows, _ in runs]
            center = statistics.median(medians)
            spread = (max(medians) - min(medians)) / center if center else 0
            timing[key] = dict(**quantiles([row[key] for row in all_rows]), run_medians=medians,
                               run_median_spread=spread, quality='NOISY' if spread > .10 else 'STABLE')
        report['applications'][app] = dict(counters=signature, timing=timing, runtime=runs[0][1],
            diagnostic_region=[400, 80, 1240, 650], changed_pixel_fractions=fractions,
            scene_image=image_name,
            input_records=[str(out / app / str(index) / 'inputs.json') for index in range(repeats)])
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print('[PASS] complete frozen samples and exact counter signatures: ' + str(out / 'summary.json'), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['build', 'run', 'verify', 'physics'])
    parser.add_argument('--configuration', choices=['Debug', 'Release'], default='Release')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--apps', nargs='+', choices=APPS, default=list(APPS),
                        help='Capture only the affected applications; defaults to the original complete suite')
    args = parser.parse_args()
    if args.repeats < 2:
        parser.error('At least two repeated processes are required')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    env = environment()
    if args.mode == 'build':
        msbuild, vc = toolchain()
        invoke([ROOT / 'vendor/bin/premake/premake5.exe', '--render-baseline', 'vs2022'], out, 'generate', env)
        invoke([msbuild, ROOT / 'GEngine.sln', '/t:' + ';'.join((*APPS, 'PhysicsBenchmark', 'RenderingValidation')),
                '/m:1', '/nr:false', '/nologo', '/v:normal', '/p:Configuration=' + args.configuration,
                '/p:Platform=x64', '/p:VCToolsVersion=' + vc.name, '/bl:' + str(out / 'build.binlog')], out, 'build', env)
    elif args.mode == 'run':
        for app in args.apps:
            executable = ROOT / 'bin' / args.configuration / app / (app + '.exe')
            for index in range(args.repeats):
                directory = out / app / str(index)
                directory.mkdir(parents=True, exist_ok=True)
                if (directory / 'frames.csv').exists():
                    raise ValueError('Preserve earlier evidence; use a new output directory: ' + str(directory))
                (directory / 'imgui.ini').write_text(LAYOUT)
                inputs = dict(binary_sha256=digest(executable), layout_sha256=digest(directory / 'imgui.ini'),
                              configuration=args.configuration, workload='default startup scene; zero update delta; no injected input',
                              environment=dict(GENGINE_BASELINE_OUTPUT=str(directory), GENGINE_SHADOW_RESOLUTION='4096'),
                              runtime_dlls={p.name: digest(p) for p in executable.parent.glob('*.dll')})
                if app == 'GEngineEditor':
                    inputs['environment']['GENGINE_BASELINE_SCENE_TARGET'] = '1'
                (directory / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
                child = dict(env, **inputs['environment'])
                invoke([executable], directory, 'application', child, cwd=directory, timeout=300)
                samples(directory)
        summarize(out, args.repeats, args.apps)
    elif args.mode == 'verify':
        summarize(out, args.repeats, args.apps)
    else:
        exe = ROOT / 'bin' / args.configuration / 'PhysicsBenchmark/PhysicsBenchmark.exe'
        for index in range(args.repeats):
            invoke([exe, '--body-counts=50,200,1000', '--warmup=3', '--samples=10', '--dt=0.008333333'],
                   out, 'physics-' + str(index), env, timeout=300)
        (out / 'inputs.json').write_text(json.dumps(dict(binary_sha256=digest(exe), configuration=args.configuration,
            scope='Independent reset-per-sample Physics workload; never subtract from render samples'), indent=2) + '\n')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('[FAIL] ' + str(error), file=sys.stderr)
        sys.exit(1)
