"""Matched Owner corrections 04 camera cases. Requires the isolated review build."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--backend',choices=['native','jolt'],default='jolt')
    parser.add_argument('--mode',choices=['paused','running'],default='paused')
    parser.add_argument('--vsync',choices=['on','off'],default='on')
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to(ROOT/'logs/ui/owner-corrections-04'):
        raise ValueError('Use this campaign evidence directory')
    output.mkdir(parents=True,exist_ok=False)
    seed=ROOT/'logs/ui/owner-corrections-02/handoff100/runtime/imgui.ini'
    shutil.copy2(seed,output/'imgui.ini')
    exe=ROOT/'bin/Release/RigidBodySimulation/RigidBodySimulation.exe'
    command=[str(exe),'--scene','sphere-lattice','--backend',args.backend]
    env=os.environ.copy();env['GENGINE_PASS_TIMING_OUTPUT']=str(output)
    if args.mode=='running':env['OWNER_REVIEW_RUNNING']='1'
    if args.vsync=='off':env['OWNER_REVIEW_NO_VSYNC']='1'
    identity=dict(command=command,binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        settings_sha256=hashlib.sha256(seed.read_bytes()).hexdigest(),
        renderer_source_sha256=hashlib.sha256((ROOT/'GEngine/src/Renderer/FrameSubmission.cpp').read_bytes()).hexdigest(),
        mode=args.mode,vsync_requested=args.vsync,
        protocol='1920x1080 client, same saved Viewport; overhead/low poses pinned before render; 24 warm + 96 measured frames; available delayed GPU queries. Paused: same Tick 0 world. Running: reset per case, one ordinary 1/60 tick/frame, unchanged authored physics. VSync-off is a separate pacing diagnostic. No capture/readback except the explicitly named one-shot picking case.')
    (output/'started.json').write_text(json.dumps(identity,indent=2)+'\n')
    start=time.monotonic()
    with (output/'stdout.log').open('wb') as stream:
        result=subprocess.run(command,cwd=output,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=300)
    frames=list(csv.DictReader((output/'owner-review-frames.csv').open()))
    cases={r['case'] for r in frames}
    valid=result.returncode==0 and len(cases)==14 and len(frames)==14*120 and all(None not in r and all(v is not None for v in r.values()) for r in frames)
    if args.mode=='running':valid=valid and all(int(r['tick'])==int(r['local'])+1 and int(r['physics_ns'])>0 for r in frames)
    else:valid=valid and all(r['tick']=='0' and r['physics_ns']=='0' for r in frames)
    identity.update(exit_code=result.returncode,seconds=time.monotonic()-start,frames=len(frames),cases=len(cases),protocol_valid=valid)
    (output/'result.json').write_text(json.dumps(identity,indent=2)+'\n')
    print(json.dumps(identity,indent=2))
    return 0 if valid else 1


if __name__=='__main__':
    raise SystemExit(main())
