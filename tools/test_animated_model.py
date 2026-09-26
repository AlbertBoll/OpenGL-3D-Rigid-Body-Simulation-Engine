"""Validate the bounded animated-model import, native boundary and caller ownership."""
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
    try:
        if args.reuse_build:
            prior=json.loads(args.reuse_build.read_text())
            build=next(s for s in prior['steps'] if s['name']=='build')
            if build['result']!='PASS' or prior['configuration']!=args.configuration:raise RuntimeError('Invalid build evidence')
            v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
        else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        source=ROOT/'tools/animated_model_probe.cpp'
        v.compile('animated-header',[source],['/DANIMATED_MODEL_SCHEMA_ONLY'],boundary=True)
        leaf=v.compile('animated-model',[source],['/DANIMATED_MODEL_INTERNAL_PROBE'])
        v.invoke('leaf',[leaf],markers=['[PASS] animated model payload/bones/errors/moves/partial/cache ownership'],cwd=v.out/'leaf-run')
        obj=v.out/'glad-renamed.obj'
        v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
            '/MTd' if v.config=='Debug' else '/MT','/Od' if v.config=='Debug' else '/O2',
            '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),
            ROOT/'external/glad/src/glad.c','/Fo'+str(obj)])
        startup=v.compile('GEngineEditor',[source,obj])
        for mode in ('failure','success'):
            markers=['[PASS] animated model startup '+mode,'runs='+('0' if mode=='failure' else '1')+' renders='+('0' if mode=='failure' else '1'),'root=0 SDL=0 TTF=0']
            if mode=='failure':markers+=['Platform operation='+str(Path(v.env['GENGINE_ASSET_ROOT'])/'AnimatedModels/phase66-missing-model.dae')+' code=9: Required platform asset is missing']
            log=v.invoke('startup-'+mode,[startup,mode],expected=1 if mode=='failure' else 0,markers=markers,cwd=v.out/('startup-'+mode))
            if 'GEngine application failed:' in log:raise RuntimeError('Startup used legacy exception transport')
        editor=Validation(args.configuration,v.out/'editor-regression')
        try:
            app=editor.compile('GEngineEditor',[ROOT/'tools/animation_key_probe.cpp',ROOT/'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp'],
                ['/DANIMATION_EDITOR','/I'+str(ROOT/'GEngineEditor/src')])
            for mode in ('failure','success'):
                log=editor.invoke('animation-'+mode,[app,mode],expected=1 if mode=='failure' else 0,
                    markers=['[PASS] animation caller '+mode],cwd=editor.out/('animation-'+mode))
                if 'GEngine application failed:' in log:raise RuntimeError('Editor used legacy exception transport')
            editor.report['result']='PASS'
        finally:editor.save()
        v.report['steps'].append({'name':'actual-editor-regression','result':'PASS','evidence':str(editor.out/'results.json')})
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report.update(result='PASS',engine_library_sha256=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest())
        return 0
    except(OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
