"""Bounded CPU regression checks for the actual RBS Spot guide emitter."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--config',choices=['Debug','Release'],required=True);p.add_argument('--label',default='01');args=p.parse_args()
out=root/f'logs/ui/spot-cone-guide-01/checks-{args.config}-{args.label}';out.mkdir(parents=True,exist_ok=False)
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207')
sdk=Path('C:/Program Files (x86)/Windows Kits/10')
exe=out/'checks.exe'
cmd=[vc/'bin/Hostx64/x64/cl.exe','/nologo','/std:c++20','/EHsc','/W4','/MTd' if args.config=='Debug' else '/MT','/Od' if args.config=='Debug' else '/O2',
 *['/I'+str(v) for v in [vc/'include',sdk/'Include/10.0.26100.0/ucrt',root/'GEngine/include/external',root/'RigidBodySimulation/include']],
 root/'tools/spot_cone_guide_checks.cpp','/Fo'+str(out)+os.sep,'/Fe'+str(exe),'/link',
 '/LIBPATH:'+str(vc/'lib/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/ucrt/x64'),'/LIBPATH:'+str(sdk/'Lib/10.0.26100.0/um/x64')]
cmd=list(map(str,cmd));build=subprocess.run(cmd,cwd=out,capture_output=True)
(out/'compile.log').write_bytes(build.stdout+build.stderr)
record=dict(command=cmd,compile_exit=build.returncode)
if build.returncode==0:
    run=subprocess.run([str(exe)],cwd=out,capture_output=True,timeout=30)
    (out/'run.log').write_bytes(run.stdout+run.stderr);record.update(run_exit=run.returncode,sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record));raise SystemExit(0 if record.get('run_exit')==0 else 1)
