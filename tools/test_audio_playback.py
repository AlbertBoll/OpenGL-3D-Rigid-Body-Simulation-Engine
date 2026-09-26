"""Validate the four-file audio slice after its completed runtime prerequisite."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT, Validation


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reuse-build', type=Path, help='Completed build results; caller verifies unchanged build inputs')
    args = parser.parse_args()
    gate = json.loads((ROOT / 'logs/rendering/phase66/runtime/completion.json').read_text())
    if gate['status'] != 'COMPLETE BEFORE AUDIO EDITS': raise RuntimeError('Runtime prerequisite incomplete')
    for path, expected in gate['production_sha256'].items():
        if hashlib.sha256((ROOT / path).read_bytes()).hexdigest() != expected: raise RuntimeError('Prerequisite changed: ' + path)
    v = Validation(args.configuration, args.output)
    try:
        if args.reuse_build:
            prior = json.loads(args.reuse_build.read_text())
            build = next(s for s in prior['steps'] if s['name'] == 'build')
            if build['result'] != 'PASS' or prior['configuration'] != args.configuration: raise RuntimeError('Invalid build reuse')
            v.report['steps'].append(dict(build, reuse=str(args.reuse_build.resolve())))
        else:
            v.build(['GEngineEditor', 'Breakout'])
        source = ROOT / 'tools/audio_playback_probe.cpp'
        v.compile('audio-header', [source], ['/DAUDIO_SCHEMA_ONLY'], boundary=True)
        unit = v.compile('audio-unit', [source], ['/DAUDIO_UNIT'])
        v.invoke('audio-unit', [unit], markers=['[PASS] audio default/retired, five states, native failure and invalid-state diagnostics'], cwd=v.out / 'unit-run')
        for app in ['GEngineEditor', 'Breakout']:
            if app == 'GEngineEditor':
                sources = [source, ROOT / 'GEngineEditor/src/Panels/SceneHierarchyPanel.cpp']
                extra = ['/DAUDIO_EDITOR', '/I' + str(ROOT / 'GEngineEditor/src')]
            else:
                sources = [source, ROOT / 'Breakout/src/GameLevel.cpp', ROOT / 'Breakout/src/GameObject/BallObject.cpp']
                extra = ['/I' + str(ROOT / 'Breakout/include')]
            exe = v.compile(app, sources, extra)
            for mode in ['failure', 'playing', 'stopping']:
                markers = ['[PASS] audio caller ' + mode]
                if mode == 'failure': markers += ['subsystem=Audio code=QueryFailed operation=SoundEvent::GetPlayState context=event=1: FMOD playback state query failed (result=']
                log = v.invoke(app + '-' + mode, [exe, mode], expected=1 if mode == 'failure' else 0, markers=markers, cwd=v.out / (app + '-' + mode))
                if 'GEngine application failed:' in log: raise RuntimeError('Audio failure depended on a legacy catch')
        v.smoke(['GEngineEditor', 'Breakout'])
        v.report['result'] = 'PASS'
        return 0
    except (OSError, RuntimeError) as error:
        v.report.update(result='FAIL', reason=str(error)); print(str(error), flush=True); return 1
    finally: v.save()

if __name__ == '__main__': sys.exit(main())
