"""Bounded paused SphereLattice rendering cases; requires the isolated review build."""
import argparse
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
    args=parser.parse_args()
    output=args.output.resolve()
    if not output.is_relative_to(ROOT/'logs/ui/owner-corrections-03'):
        raise ValueError('Use this campaign evidence directory')
    output.mkdir(parents=True,exist_ok=False)
    seed=ROOT/'logs/ui/owner-corrections-02/handoff100/runtime/imgui.ini'
    shutil.copy2(seed,output/'imgui.ini')
    exe=ROOT/'bin/Release/RigidBodySimulation/RigidBodySimulation.exe'
    command=[str(exe),'--scene','sphere-lattice','--backend',args.backend]
    env=os.environ.copy();env['GENGINE_PASS_TIMING_OUTPUT']=str(output)
    identity=dict(command=command,binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  settings_sha256=hashlib.sha256(seed.read_bytes()).hexdigest(),
                  renderer_source_sha256=hashlib.sha256((ROOT/'GEngine/src/Renderer/FrameSubmission.cpp').read_bytes()).hexdigest(),
                  capture_mode=env.get('OWNER_REVIEW_CAPTURE')=='1',reference_composite=env.get('OWNER_REVIEW_REFERENCE_COMPOSITE')=='1',
                  pixels_per_dip=env.get('OWNER_REVIEW_PIXELS_PER_DIP','1'),
                  protocol='Paused Tick 0; fixed 1920x1080 client; camera pinned immediately before submission; 24 warm + 96 sample frames/case; delayed available GPU queries only. Capture mode is separate and includes explicit readbacks.')
    (output/'started.json').write_text(json.dumps(identity,indent=2)+'\n')
    started=time.monotonic()
    with (output/'stdout.log').open('wb') as stream:
        result=subprocess.run(command,cwd=output,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=240)
    identity.update(exit_code=result.returncode,seconds=time.monotonic()-started)
    (output/'result.json').write_text(json.dumps(identity,indent=2)+'\n')
    print(json.dumps(identity,indent=2))
    return result.returncode

if __name__=='__main__':
    raise SystemExit(main())
