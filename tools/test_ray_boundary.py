"""Validate bounded Ray/Image error transport with the pinned Phase66 toolchain."""
import argparse, hashlib, sys
from pathlib import Path
from test_runtime_failure import Validation, ROOT, VC

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--loader',type=Path,required=True,help='Unchanged pinned test-loader object')
    args=parser.parse_args();v=Validation(args.configuration,args.output)
    try:
        v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation','PhysicsTests','PhysicsBenchmark'])
        v.compile('image-error-header',[ROOT/'tools/ray_boundary_probe.cpp'],['/DRAY_ERROR_SCHEMA_ONLY'],boundary=True)
        loader=args.loader.resolve()
        v.report['reused_loader']={'path':str(loader),'sha256':hashlib.sha256(loader.read_bytes()).hexdigest()}
        exe=v.compile('GEngineEditor',[ROOT/'tools/ray_boundary_probe.cpp',loader])
        for mode in ['failure','success','recovery']:
            log=v.invoke(mode,[exe,mode],expected=1 if mode=='failure' else 0,
                markers=['[PASS] image transport '+mode,'owner=1 root=0 SDL=0 TTF=0'],cwd=v.out/mode)
            if mode=='failure':
                for text in [log,(v.out/mode/'GEngine.log').read_text(errors='replace')]:
                    markers=['image code=6','image-fixture-operation','size=37x41','format=2','source-size=43x47',
                        'expected-elements=1517','actual-elements=2021','backend=1285',
                        'complete image diagnostic: source=ray fixture; upload unavailable']
                    if not all(marker in text for marker in markers):raise RuntimeError('Complete ImageError missing from console/file')
            if 'GEngine application failed:' in log:raise RuntimeError('Image failure used legacy catch')
        v.compile('existing-ray-caller',[ROOT/'tools/ray_tracing_probe.cpp'])
        runtime=v.compile('runtime-channel',[ROOT/'tools/runtime_failure_probe.cpp'])
        import shutil
        entry=runtime.with_name('GEngineEditor.exe');shutil.copyfile(runtime,entry)
        for mode in ['success','close','input-failure','update-failure','render-failure']:
            v.invoke('runtime-'+mode,[entry,mode],expected=1 if mode.endswith('failure') else 0,
                markers=['[PASS] runtime '+mode],cwd=v.out/('runtime-'+mode))
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report.update(result='PASS',engine_library_sha256=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest(),benchmark='BUILT ONLY')
        return 0
    except Exception as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()
if __name__=='__main__':sys.exit(main())
