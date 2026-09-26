"""Validate typed shape registration, exact ownership and startup rollback without timing work."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT, VC, Validation


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reuse-build',type=Path)
    args=parser.parse_args()
    v=Validation(args.configuration,args.output)
    sha=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
    inputs=ROOT/'logs/rendering/phase66/shape-registration/validation-inputs.json'
    hashes=json.loads(inputs.read_text())
    build_inputs={p:value['sha256'] for p,value in hashes.items() if not p.startswith('tools/')}
    try:
        if any(sha(ROOT/p)!=value['sha256'] for p,value in hashes.items()):raise RuntimeError('Candidate input drift before validation')
        v.report['build_inputs']=build_inputs
        if args.reuse_build:
            prior=json.loads(args.reuse_build.read_text())
            build=next(step for step in prior['steps'] if step['name']=='build')
            if build['result']!='PASS' or prior['configuration']!=args.configuration or prior['build_inputs']!=build_inputs:
                raise RuntimeError('Build reuse lacks exact input identity')
            if prior['engine_library_sha256']!=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib'):
                raise RuntimeError('Engine library changed before build reuse')
            v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
        else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report['engine_library_sha256']=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib');v.save()
        source=ROOT/'tools/shutdown_probe.cpp'
        v.compile('registration-header',[source],['/DSHAPE_REGISTRATION_SCHEMA_ONLY'],boundary=True)
        probe=v.compile('shutdown',[source])
        for mode in ['--shape-registration','--manager-failures','--cache-ownership','--lifetimes']:
            v.invoke('shutdown-'+mode[2:],[probe,mode],markers=['[PASS] shutdown '+mode],cwd=v.out/('shutdown-'+mode[2:]))
        obj=v.out/'glad-renamed.obj'
        v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
            '/MTd' if v.config=='Debug' else '/MT','/Od' if v.config=='Debug' else '/O2',
            '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),
            ROOT/'external/glad/src/glad.c','/Fo'+str(obj)])
        app=v.compile('GEngineEditor',[source,obj],['/DSHAPE_REGISTRATION_STARTUP_PROBE'])
        for mode in ['allocation-failure','null-failure','success']:
            markers=['[PASS] registration startup '+mode,'owner=1 root=0 SDL=0 TTF=0 teardown=1']
            diagnostic=None
            if mode=='allocation-failure':
                diagnostic='Application shape registration operation=ShapeManager::_GetShape code=2 name=SkyBox: Shape owner allocation failed'
                markers+=['exit=1 runs=0 renders=0 buffers=7/7 arrays=2/2']
            if mode=='null-failure':
                diagnostic='Application shape registration operation=ShapeManager::Register code=0 name=phase66-startup: Cannot register a null shape'
                markers+=['exit=1 runs=0 renders=0']
            if mode=='success':markers+=['exit=0 runs=1 renders=1']
            if diagnostic:markers.append(diagnostic)
            cwd=v.out/mode
            log=v.invoke(mode,[app,mode],expected=0 if mode=='success' else 1,markers=markers,cwd=cwd)
            if 'GEngine application failed:' in log:raise RuntimeError('Registration used legacy exception transport')
            if diagnostic and '[error] GENGINE: '+diagnostic not in (cwd/'GEngine.log').read_text(errors='replace'):
                raise RuntimeError('Full registration diagnostic absent from core file error sink')
        viewport=v.compile('viewport',[ROOT/'tools/viewport_probe.cpp'])
        for mode in ['startup-errors','asset-root-error']:
            v.invoke(mode,[viewport,'--'+mode],markers=['[PASS] '+mode],cwd=v.out/mode)
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        if any(sha(ROOT/p)!=value['sha256'] for p,value in hashes.items()):raise RuntimeError('Candidate input drift during validation')
        v.report.update(result='PASS',validation_input_sha256=sha(inputs))
        return 0
    except (OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
