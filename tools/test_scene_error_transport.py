"""Bounded Scene transport validation. No benchmark or timing loop is executed."""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
from test_runtime_failure import ROOT,VC,Validation

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();v=Validation(args.configuration,args.output)
    try:
        v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation','PhysicsTests','PhysicsBenchmark'])
        v.compile('scene-transport-header',[ROOT/'tools/scene_error_transport_probe.cpp'],['/DSCENE_TRANSPORT_SCHEMA_ONLY'],boundary=True)
        loader=v.out/'glad-renamed.obj'
        v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
            '/MTd'if args.configuration=='Debug'else'/MT','/Od'if args.configuration=='Debug'else'/O2',
            '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p)for p in v.includes),
            ROOT/'external/glad/src/glad.c','/Fo'+str(loader)])
        exe=v.compile('GEngineEditor',[ROOT/'tools/scene_error_transport_probe.cpp',loader])
        for mode in ['startup-foreign','startup-missing','startup-parent','runtime-failure','runtime-parent','success']:
            markers=['[PASS] scene transport '+mode,'owner=1 root=0 SDL=0 TTF=0 teardown=1']
            if mode!='success':markers+=['scene-fixture-operation','entity=37','full scene diagnostic: source=fixture; entity ownership unavailable']
            if mode.endswith('-parent'):markers+=['transform-code=7','transform-entity=37','transform-parent=41']
            log=v.invoke(mode,[exe,mode],expected=0 if mode=='success'else 1,markers=markers,cwd=v.out/mode)
            if 'GEngine application failed:'in log or 'phase66-filtered-scene'in log:raise RuntimeError('Legacy catch or diagnostic filtering failure')
            file_log=(v.out/mode/'GEngine.log').read_text(errors='replace')
            if 'phase66-filtered-scene'in file_log:raise RuntimeError('File sink ignored level filtering')
            if mode!='success' and not all(x in file_log for x in markers[2:]):raise RuntimeError('File sink lost complete diagnostics')
        runtime=v.compile('runtime-channel',[ROOT/'tools/runtime_failure_probe.cpp'])
        named=runtime.with_name('GEngineEditor.exe');shutil.copyfile(runtime,named);runtime=named
        for mode in ['success','close','input-failure','update-failure','render-failure']:
            v.invoke('runtime-'+mode,[runtime,mode],expected=1 if mode.endswith('failure')else 0,markers=['[PASS] runtime '+mode],cwd=v.out/('runtime-'+mode))
        # Existing async consumers compile against the enlarged scheduler variant.
        for probe in ['async_upload','async_texture','async_mesh']:
            v.compile(probe,[ROOT/('tools/'+probe+'_probe.cpp')])
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report.update(result='PASS',engine_library_sha256=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest(),benchmark='BUILT ONLY')
        return 0
    except (OSError,RuntimeError,subprocess.SubprocessError)as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()
if __name__=='__main__':sys.exit(main())
