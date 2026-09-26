"""Validate bounded static-model import and caller error/ownership cleanup."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT,VC,Validation


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reuse-build',type=Path)
    parser.add_argument('--mode',choices=['full','platform-prerequisite','static-closure'],default='full')
    parser.add_argument('--prerequisite',type=Path)
    args=parser.parse_args()
    v=Validation(args.configuration,args.output)
    try:
        if args.mode=='static-closure':
            if not args.prerequisite:raise RuntimeError('Closure requires passing prerequisite evidence')
            prior=json.loads(args.prerequisite.read_text())
            actual=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest()
            if prior['result']!='PASS' or prior['configuration']!=args.configuration or prior['engine_library_sha256']!=actual:
                raise RuntimeError('Prerequisite identity does not match closure')
            v.report['steps'].append({'name':'passing-platform-prerequisite','result':'PASS','evidence':str(args.prerequisite.resolve())})
        else:
            if args.reuse_build:
                previous=json.loads(args.reuse_build.read_text())
                build=next(step for step in previous['steps'] if step['name']=='build')
                if build['result']!='PASS' or previous['configuration']!=args.configuration:raise RuntimeError('Invalid build reuse')
                v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
            else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
            source=ROOT/'tools/shutdown_probe.cpp'
            if args.mode=='full':
                v.compile('model-header',[source],['/DRAW_MODEL_SCHEMA_ONLY'],boundary=True)
                probe=v.compile('shutdown',[source],['/DRAW_MODEL_INTERNAL_PROBE'])
                for mode in ('--static-models','--manager-failures','--cache-ownership','--lifetimes'):
                    v.invoke('shutdown-'+mode[2:],[probe,mode],markers=['[PASS] shutdown '+mode],cwd=v.out/('shutdown-'+mode[2:]))
            sources=[source];extra=['/DRAW_MODEL_STARTUP_PROBE']
            if args.mode=='platform-prerequisite':
                obj=v.out/'glad-renamed.obj'
                v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
                    '/MTd' if v.config=='Debug' else '/MT','/Od' if v.config=='Debug' else '/O2',
                    '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),
                    ROOT/'external/glad/src/glad.c','/Fo'+str(obj)])
                sources.append(obj);extra.append('/DPLATFORM_STARTUP_OWNERSHIP')
            app=v.compile('GEngineEditor',sources,extra)
            for mode in ('failure','success'):
                markers=['[PASS] static model startup '+mode,'owner=1 root=0 SDL=0 TTF=0 renders='+('0' if mode=='failure' else '1')]
                diagnostic='Platform operation='+str(Path(v.env['GENGINE_ASSET_ROOT'])/'Models/phase66-missing-model.obj')+' code=9: Required platform asset is missing'
                if mode=='failure':markers.append(diagnostic)
                cwd=v.out/('startup-'+mode)
                log=v.invoke('startup-'+mode,[app,mode],expected=1 if mode=='failure' else 0,markers=markers,cwd=cwd)
                file_log=(cwd/'GEngine.log').read_text(errors='replace')
                if mode=='failure' and '[error] GENGINE: '+diagnostic not in file_log:raise RuntimeError('File sink lost platform severity/category/diagnostic')
                if 'phase66-filtered-' in log+file_log:raise RuntimeError('Platform reporter bypassed logger filtering')
                if 'GEngine application failed:' in log:raise RuntimeError('Static model startup used legacy catch')
        if args.mode!='platform-prerequisite':
            editor=Validation(args.configuration,v.out/'editor-regression')
            try:
                app=editor.compile('GEngineEditor',[ROOT/'tools/animation_key_probe.cpp',ROOT/'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp'],
                    ['/DANIMATION_EDITOR','/I'+str(ROOT/'GEngineEditor/src')])
                for mode in ('failure','success'):
                    editor.invoke('animation-'+mode,[app,mode],expected=1 if mode=='failure' else 0,
                        markers=['[PASS] animation caller '+mode],cwd=editor.out/('animation-'+mode))
                editor.report['result']='PASS'
            finally:editor.save()
            v.report['steps'].append({'name':'actual-editor-regression','result':'PASS','evidence':str(editor.out/'results.json')})
        if args.mode!='static-closure':v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report.update(result='PASS',engine_library_sha256=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest())
        return 0
    except(OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
