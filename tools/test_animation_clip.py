"""Validate bounded animation clip import and its Editor startup ownership prerequisite."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT,VC,Validation


def editor_startup(v):
    source=ROOT/'tools/animation_clip_probe.cpp';obj=v.out/'glad-renamed.obj'
    v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
        '/MTd' if v.config=='Debug' else '/MT','/Od' if v.config=='Debug' else '/O2',
        '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),
        ROOT/'external/glad/src/glad.c','/Fo'+str(obj)])
    app=v.compile('GEngineEditor',[source,ROOT/'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp',obj],['/I'+str(ROOT/'GEngineEditor/src')])
    for mode in ('failure','success'):
        markers=['[PASS] animation clip Editor '+mode,'root=0 SDL=0 TTF=0','[PASS] Editor exact ownership','owner-context/thread=1']
        if mode=='failure':markers+=['Application model import operation=Animation::Create code=1 source=phase66-missing-clip.dae: ERROR::ASSIMP::']
        log=v.invoke('editor-'+mode,[app,mode],expected=1 if mode=='failure' else 0,markers=markers,cwd=v.out/('editor-'+mode))
        if 'GEngine application failed:' in log:raise RuntimeError('Clip failure used legacy catch transport')
        for tag in ('Editor Camera','barrel','Ambient Light',*(f'LampLight_{i}' for i in range(1,6))):
            if log.count(tag+' Destructor was called!')!=1:raise RuntimeError('Pending actor did not retire exactly once: '+tag)
        v.report['steps'].append({'name':'pending-actor-retirement-'+mode,'result':'PASS','exactly_once':['Editor Camera','barrel','Ambient Light',*(f'LampLight_{i}' for i in range(1,6))]})
        v.save()


def runtime_regression(v):
    runtime=Validation(v.config,v.out/'runtime-regression')
    try:
        app=runtime.compile('GEngineEditor',[ROOT/'tools/animation_key_probe.cpp',ROOT/'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp'],
            ['/DANIMATION_EDITOR','/I'+str(ROOT/'GEngineEditor/src')])
        for mode in ('failure','success'):
            runtime.invoke('animation-'+mode,[app,mode],expected=1 if mode=='failure' else 0,
                markers=['[PASS] animation caller '+mode],cwd=runtime.out/('animation-'+mode))
        runtime.report['result']='PASS'
    finally:runtime.save()
    v.report['steps'].append({'name':'actual-animation-runtime-regression','result':'PASS','evidence':str(runtime.out/'results.json')})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--mode',choices=['full','prerequisite','closure'],default='full')
    parser.add_argument('--reuse-build',type=Path)
    parser.add_argument('--reuse-leaf',type=Path,help='Passing leaf/header substeps only; caller records unchanged input proof')
    parser.add_argument('--prerequisite',type=Path)
    args=parser.parse_args();v=Validation(args.configuration,args.output)
    try:
        if args.mode=='closure':
            if not args.prerequisite:raise RuntimeError('Closure requires prerequisite evidence')
            prior=json.loads(args.prerequisite.read_text())
            actual=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest()
            if prior.get('result')!='PASS' or prior['configuration']!=args.configuration or prior['engine_library_sha256']!=actual:
                raise RuntimeError('Prerequisite identity mismatch')
            v.report['steps'].append({'name':'passing-ownership-prerequisite','result':'PASS','evidence':str(args.prerequisite.resolve())})
        else:
            if args.reuse_build:
                prior=json.loads(args.reuse_build.read_text());build=next(s for s in prior['steps'] if s['name']=='build')
                if build['result']!='PASS' or prior['configuration']!=args.configuration:raise RuntimeError('Invalid build reuse')
                v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
            else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
            if args.reuse_leaf:
                prior=json.loads(args.reuse_leaf.read_text())
                if prior['configuration']!=args.configuration:raise RuntimeError('Invalid leaf configuration')
                for name in ('compile-clip-header','compile-clip-leaf','leaf'):
                    step=next(s for s in prior['steps'] if s['name']==name)
                    if step['result']!='PASS':raise RuntimeError('Invalid leaf evidence')
                    v.report['steps'].append(dict(step,reuse=str(args.reuse_leaf.resolve())))
            else:
                source=ROOT/'tools/animation_clip_probe.cpp'
                v.compile('clip-header',[source],['/DANIMATION_CLIP_SCHEMA_ONLY'],boundary=True)
                leaf=v.compile('clip-leaf',[source],['/DANIMATION_CLIP_INTERNAL_PROBE'])
                v.invoke('leaf',[leaf],markers=['[PASS] animation clip payload/hierarchy/bones/typed-errors/transactional-metadata/ownership'],cwd=v.out/'leaf-run')
            editor_startup(v)
        if args.mode!='prerequisite':runtime_regression(v)
        if args.mode!='closure':v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report.update(result='PASS',engine_library_sha256=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest());return 0
    except(OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
