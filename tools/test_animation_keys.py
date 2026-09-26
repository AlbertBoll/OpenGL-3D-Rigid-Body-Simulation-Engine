"""Validate typed Bone interval errors, private key conversion and actual Editor runtime propagation."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT, Validation


def audio_caller_regression(parent):
    # SceneApp::Update now has an animation result before its unchanged audio query.
    # Revalidate that changed caller; SoundEvent/Breakout leaf evidence stays reused.
    v=Validation(parent.config,parent.out/'audio-caller-regression')
    try:
        exe=v.compile('GEngineEditor',[ROOT/'tools/audio_playback_probe.cpp',ROOT/'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp'],
            ['/DAUDIO_EDITOR','/I'+str(ROOT/'GEngineEditor/src')])
        log=v.invoke('audio-failure',[exe,'failure'],expected=1,markers=['[PASS] audio caller failure',
            'subsystem=Audio code=QueryFailed operation=SoundEvent::GetPlayState'],cwd=v.out/'failure')
        if 'GEngine application failed:' in log:raise RuntimeError('Audio regression used legacy catch transport')
        v.report['result']='PASS'
    except (OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));raise
    finally:
        v.save()
        parent.report['steps'].append({'name':'audio-caller-regression','result':v.report.get('result','FAIL'),
            'evidence':str(v.out/'results.json')})
        parent.save()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reuse-build',type=Path,help='Completed build results; caller verifies unchanged production/build inputs')
    args=parser.parse_args()
    v=Validation(args.configuration,args.output)
    try:
        if args.reuse_build:
            prior=json.loads(args.reuse_build.read_text())
            build=next(s for s in prior['steps'] if s['name']=='build')
            if build['result']!='PASS' or prior['configuration']!=args.configuration:raise RuntimeError('Invalid completed build evidence')
            v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
        else:
            v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        source=ROOT/'tools/animation_key_probe.cpp'
        v.compile('animation-header',[source],['/DANIMATION_SCHEMA_ONLY'],boundary=True)
        unit=v.compile('animation-unit',[source])
        v.invoke('unit',[unit],markers=['[PASS] animation key payload/constant/interval/three-track-errors/diagnostics/transactional-transform'],cwd=v.out/'unit-run')
        app=v.compile('GEngineEditor',[source,ROOT/'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp'],
            ['/DANIMATION_EDITOR','/I'+str(ROOT/'GEngineEditor/src')])
        for mode in ('failure','success'):
            markers=['[PASS] animation caller '+mode]
            if mode=='failure':markers+=['subsystem=Animation code=MissingInterval operation=Bone::Get']
            log=v.invoke('caller-'+mode,[app,mode],expected=1 if mode=='failure' else 0,markers=markers,cwd=v.out/('caller-'+mode))
            if 'GEngine application failed:' in log:raise RuntimeError('Animation failure used legacy catch transport')
        audio_caller_regression(v)
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report['result']='PASS'
        v.report['engine_library_sha256']=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest()
        return 0
    except (OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
