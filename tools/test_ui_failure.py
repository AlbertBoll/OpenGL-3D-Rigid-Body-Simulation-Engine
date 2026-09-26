"""Bounded UI error frame retirement; no benchmark or timing workload."""
import argparse, hashlib, json, sys
from pathlib import Path
from test_runtime_failure import Validation, ROOT, VC

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--configuration',choices=['Debug','Release'],required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--before',action='store_true')
    p.add_argument('--loader',type=Path,help='Unchanged pinned test-loader object to reuse')
    args=p.parse_args();v=Validation(args.configuration,args.output)
    try:
        if not args.before:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation','PhysicsTests','PhysicsBenchmark'])
        extra=[] if args.before else ['/DUI_FAILURE_AFTER']
        v.compile('ui-header',[ROOT/'tools/ui_failure_probe.cpp'],[*extra,'/DUI_FAILURE_SCHEMA_ONLY'],boundary=True)
        loader=args.loader.resolve() if args.loader else v.out/'glad-renamed.obj'
        if args.loader:
            v.report['reused_loader']={'path':str(loader),'sha256':hashlib.sha256(loader.read_bytes()).hexdigest()}
        else:v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
            '/MTd' if args.configuration=='Debug' else '/MT','/Od' if args.configuration=='Debug' else '/O2',
            '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),ROOT/'external/glad/src/glad.c','/Fo'+str(loader)])
        exe=v.compile('GEngineEditor',[ROOT/'tools/ui_failure_probe.cpp',loader],extra)
        for mode in ['failure'] if args.before else ['failure','success','recovery']:
            log=v.invoke(mode,[exe,mode],expected=1 if mode=='failure' else 0,
                markers=['[PASS] UI failure retirement '+mode,'owner=1 root=0 SDL=0 TTF=0'],cwd=v.out/mode)
            if mode=='failure':
                for text in [log,(v.out/mode/'GEngine.log').read_text(errors='replace')]:
                    if not all(m in text for m in ['ui-fixture-authoring','entity=37','complete UI authoring failure: source=ray image; upload unavailable']):
                        raise RuntimeError('Complete UI error missing from console/file')
            if 'GEngine application failed:' in log:raise RuntimeError('UI failure used legacy catch')
        if not args.before:
            v.compile('viewport-caller',[ROOT/'tools/viewport_probe.cpp'])
            runtime=v.compile('runtime-channel',[ROOT/'tools/runtime_failure_probe.cpp'])
            import shutil
            entry=runtime.with_name('GEngineEditor.exe');shutil.copyfile(runtime,entry)
            for mode in ['success','close','input-failure','update-failure','render-failure']:
                v.invoke('runtime-'+mode,[entry,mode],expected=1 if mode.endswith('failure') else 0,markers=['[PASS] runtime '+mode],cwd=v.out/('runtime-'+mode))
            v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report.update(result='PASS',engine_library_sha256=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest())
        return 0
    except Exception as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()
if __name__=='__main__':sys.exit(main())
