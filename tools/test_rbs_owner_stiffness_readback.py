"""Read-only body observations during two existing bounded SphereLattice smokes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'logs/ui/owner-corrections-03/stiffness-readback'

def main():
    OUT.mkdir(exist_ok=False)
    exe=ROOT/'bin/Release/RigidBodySimulation/RigidBodySimulation.exe'
    env=os.environ.copy();env['OWNER_REVIEW_STIFFNESS']='1'
    results=[]
    for backend in ('native','jolt'):
        folder=OUT/backend;folder.mkdir()
        command=[str(exe),'--scene','sphere-lattice','--backend',backend,'--physics-smoke-ticks','120']
        with (folder/'stdout.log').open('wb') as log:
            result=subprocess.run(command,cwd=folder,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
        results.append(dict(backend=backend,command=command,exit_code=result.returncode,
                            binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                            semantics='Existing 120-tick smoke; read-only snapshots every 30 ticks; no overrides, contact listener, tuning or injected impulses'))
        (OUT/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(backend,result.returncode)
        if result.returncode:return result.returncode
    return 0

if __name__=='__main__':raise SystemExit(main())
