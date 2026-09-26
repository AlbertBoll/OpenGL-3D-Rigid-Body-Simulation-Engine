"""Validate bounded typed sphere/box leaf factories without migrating legacy callers."""
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
    parser.add_argument('--reuse-leaf',type=Path)
    parser.add_argument('--reuse-scene-core',type=Path)
    parser.add_argument('--reuse-scene-startup',type=Path)
    parser.add_argument('--startup-only',action='store_true')
    parser.add_argument('--mode',choices=['factories','startup-transport','polymorphic-preflight','polymorphic-retirement','scene-startup','constructors'],default='factories')
    args=parser.parse_args();v=Validation(args.configuration,args.output)
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    package='physics-constructor-retirement' if args.mode=='constructors' else 'physics-scene-startup' if args.mode=='scene-startup' else ('physics-polymorphic-ownership' if args.mode.startswith('polymorphic-') else ('physics-startup' if args.mode=='startup-transport' else 'physics-shape-factories'))
    path=ROOT/'logs/rendering/phase66'/package/'validation-inputs.json';inputs=json.loads(path.read_text())
    build_inputs={p:r['sha256'] for p,r in inputs.items() if not p.startswith('tools/')}
    try:
        if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift before validation')
        v.report['build_inputs']=build_inputs
        if args.reuse_build:
            prior=json.loads(args.reuse_build.read_text());build=next(s for s in prior['steps'] if s['name']=='build')
            if build['result']!='PASS' or prior['configuration']!=args.configuration or prior['build_inputs']!=build_inputs:
                raise RuntimeError('Build reuse input mismatch')
            if prior['engine_library_sha256']!=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib'):raise RuntimeError('Library mismatch')
            v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
        else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation','PhysicsTests']+(['PhysicsBenchmark'] if args.mode=='constructors' else []))
        if args.mode=='constructors':v.report['benchmark']='COMPILED ONLY; not run'
        v.report['engine_library_sha256']=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib');v.save()
        source=ROOT/'tools/physics_shape_probe.cpp'
        if args.mode=='scene-startup':
            def reuse_steps(previous_path,names):
                previous=json.loads(previous_path.read_text())
                if previous['configuration']!=args.configuration or previous['engine_library_sha256']!=v.report['engine_library_sha256']:
                    raise RuntimeError('Scene reuse configuration/library mismatch')
                for name in names:
                    step=next(s for s in previous['steps'] if s['name']==name)
                    if step['result']!='PASS':raise RuntimeError('Cannot reuse failed Scene step')
                    v.report['steps'].append(dict(step,reuse=str(previous_path.resolve())))
                v.save()
            core_names=['compile-scene-startup-header','compile-scene-startup','scene-startup']
            if args.reuse_scene_startup:
                previous=json.loads(args.reuse_scene_startup.read_text())
                if previous.get('result')!='PASS' or previous.get('validation_input_sha256')!=sha(path):
                    raise RuntimeError('Startup prerequisite reuse requires exact finalized inputs and PASS')
                reuse_steps(args.reuse_scene_startup,core_names+['compile-test-loader','compile-RigidBodySimulation','rbs-failure','rbs-success'])
            else:
                if args.reuse_scene_core:
                    before=json.loads((path.parent/'validation-inputs-initial.json').read_text())
                    allowed={'RigidBodySimulation/include/RigidBodySimulation.h','RigidBodySimulation/src/RigidBodySimulation.cpp','tools/physics_shape_probe.cpp','tools/test_physics_shapes.py'}
                    if any(sha(ROOT/p)!=r['sha256'] for p,r in before.items() if p not in allowed):
                        raise RuntimeError('Scene core source/build input drift')
                    def without_rbs(text):
                        first=text.index('#elif defined(PHYSICS_SHAPE_RBS_PROBE)')
                        last=text.index('#elif !defined(PHYSICS_SHAPE_SCHEMA_ONLY)',first)
                        return text[:first]+text[last:]
                    if without_rbs(source.read_text())!=without_rbs((path.parent/'probe-before-diagnostic.cpp').read_text()):
                        raise RuntimeError('Scene core preprocessor branch changed')
                    reuse_steps(args.reuse_scene_core,core_names)
                else:
                    v.compile('scene-startup-header',[source],['/DPHYSICS_SHAPE_SCENE_SCHEMA_ONLY'],boundary=True)
                    probe=v.compile('scene-startup',[source],['/DPHYSICS_SHAPE_SCENE_PROBE'])
                    v.invoke('scene-startup',[probe,'--scene-startup'],markers=['[PASS] scene physics startup diagnostics/rollback/callbacks/retry/ownership cases=4 live=0'])
                obj=v.out/'glad-renamed.obj'
                v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
                    '/MTd' if v.config=='Debug' else '/MT','/Od' if v.config=='Debug' else '/O2',
                    '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),
                    ROOT/'external/glad/src/glad.c','/Fo'+str(obj)])
                app=v.compile('RigidBodySimulation',[source,ROOT/'RigidBodySimulation/src/main.cpp',obj],
                    ['/DPHYSICS_SHAPE_RBS_PROBE','/I'+str(ROOT/'RigidBodySimulation/include')])
                for mode in ['failure','success']:
                    cwd=v.out/('rbs-'+mode)
                    log=v.invoke('rbs-'+mode,[app,mode],expected=1 if mode=='failure' else 0,
                        markers=['[PASS] actual RBS physics startup '+mode,'owner=1 concrete=0 root=0 SDL=0 TTF=0',
                            'exit=1 runs=0 renders=0' if mode=='failure' else 'exit=0 runs=1 renders=1'],cwd=cwd)
                    if 'GEngine application failed:' in log:raise RuntimeError('Actual RBS used legacy exception transport')
                    if mode=='failure':
                        diagnostic=next(line.removeprefix('[EXPECTED] ') for line in log.splitlines() if line.startswith('[EXPECTED] '))
                        if '[error] GENGINE: '+diagnostic not in (cwd/'GEngine.log').read_text(errors='replace'):
                            raise RuntimeError('Actual RBS lost complete core file diagnostic')
            if args.startup_only:
                if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during prerequisite')
                v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
            physics=ROOT/'bin'/args.configuration/'PhysicsTests/PhysicsTests.exe'
            v.invoke('runtime-transform',[physics,'--runtime-transform'],markers=['Runtime-transform regression: 57 checks passed'],cwd=v.out/'runtime-transform')
            v.invoke('absolute-scaling',[physics,'--absolute-scaling'],markers=['Absolute-scaling regression:'],cwd=v.out/'absolute-scaling')
            interp=v.compile('interpolation',[ROOT/'tools/interpolation_probe.cpp'])
            for mode in ['cpu','gl']:
                v.invoke('interpolation-'+mode,[interp,'--'+mode],markers=['[PASS] interpolation --'+mode],cwd=v.out/('interpolation-'+mode))
            v.compile('input-control',[ROOT/'tools/input_control_probe.cpp'])
            v.compile('submission-header',[ROOT/'tools/frame_submission_probe.cpp'],['/DSUBMISSION_SCHEMA_ONLY'],boundary=True)
            v.compile('frame-submission',[ROOT/'tools/frame_submission_probe.cpp',ROOT/'RigidBodySimulation/src/main.cpp'],['/I'+str(ROOT/'RigidBodySimulation/include')])
            state=v.compile('render-state',[ROOT/'tools/render_state_probe.cpp']);v.invoke('render-state',[state],markers=['[PASS] render-state checks='])
            v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
            if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during validation')
            v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
        if args.mode.startswith('polymorphic-'):
            if args.mode=='polymorphic-retirement':
                v.compile('physics-owner-header',[source],['/DPHYSICS_SHAPE_POLYMORPHIC_SCHEMA_ONLY'],boundary=True)
            probe=v.compile('physics-owner',[source])
            v.invoke('polymorphic-retirement',[probe,'--polymorphic-retirement'],
                markers=['[PASS] polymorphic shape retirement cycles=2 concrete=6/6','live=0'])
            if args.mode=='polymorphic-preflight':
                v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
            v.invoke('physics-factories',[probe],markers=['[PASS] physics factories diagnostics/analytic-payload/source/move/retirement cycles=2'])
            physics=ROOT/'bin'/args.configuration/'PhysicsTests/PhysicsTests.exe'
            for mode,marker in [('sphere-validity',None),('box-features','Box contact features:'),('absolute-scaling','Absolute-scaling regression:'),('runtime-transform','Runtime-transform regression:')]:
                v.invoke(mode,[physics,'--'+mode],markers=[marker] if marker else [],cwd=v.out/mode)
            v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
            if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during validation')
            v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
        if args.mode=='startup-transport':
            v.compile('physics-startup-header',[source],['/DPHYSICS_SHAPE_STARTUP_SCHEMA_ONLY'],boundary=True)
            obj=v.out/'glad-renamed.obj'
            v.invoke('compile-test-loader',[VC/'bin/Hostx64/x64/cl.exe','/nologo','/c','/TC',
                '/MTd' if v.config=='Debug' else '/MT','/Od' if v.config=='Debug' else '/O2',
                '/DgladLoadGL=Phase66RealGladLoadGL',*('/I'+str(p) for p in v.includes),
                ROOT/'external/glad/src/glad.c','/Fo'+str(obj)])
            app=v.compile('GEngineEditor',[source,obj],['/DPHYSICS_SHAPE_STARTUP_PROBE'])
            for mode in ['radius-failure','points-failure','success']:
                diagnostic=None
                if mode=='radius-failure':diagnostic='Application physics shape operation=ShapeSphere::Create code=0 entity=37 radius=-2.5 points=0: ShapeSphere requires a finite positive radius'
                if mode=='points-failure':diagnostic='Application physics shape operation=ShapeBox::Create code=1 entity=41 radius=0 points=3: ShapeBox requires finite points with non-zero extents'
                markers=['[PASS] physics startup '+mode,'buffers=108/108 arrays=30/30 owner=1 root=0 SDL=0 TTF=0 teardown=1',
                    'exit=0 runs=1 renders=1' if mode=='success' else 'exit=1 runs=0 renders=0']
                if diagnostic:markers.append(diagnostic)
                cwd=v.out/mode
                log=v.invoke(mode,[app,mode],expected=0 if mode=='success' else 1,markers=markers,cwd=cwd)
                file_log=(cwd/'GEngine.log').read_text(errors='replace')
                if 'GEngine application failed:' in log:raise RuntimeError('Physics startup used legacy exception transport')
                if 'phase66-filtered-' in log+file_log:raise RuntimeError('Physics reporter bypassed filtering')
                if diagnostic and '[error] GENGINE: '+diagnostic not in file_log:
                    raise RuntimeError('Complete physics startup diagnostic absent from core file error sink')
            v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
            if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during validation')
            v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
        if args.reuse_leaf:
            previous=json.loads(args.reuse_leaf.read_text())
            before=json.loads((args.reuse_leaf.parent.parent/'validation-inputs-initial.json').read_text())
            if previous['configuration']!=args.configuration or previous['engine_library_sha256']!=v.report['engine_library_sha256']:
                raise RuntimeError('Leaf reuse configuration/library mismatch')
            if any(sha(ROOT/p)!=r['sha256'] for p,r in before.items() if p!='tools/test_physics_shapes.py'):
                raise RuntimeError('Leaf reuse source/build input drift')
            for name in ['compile-physics-shape-header','compile-physics-shapes','physics-shapes','sphere-validity']:
                step=next(s for s in previous['steps'] if s['name']==name)
                if step['result']!='PASS':raise RuntimeError('Cannot reuse failed leaf step')
                v.report['steps'].append(dict(step,reuse=str(args.reuse_leaf.resolve())))
        else:
            v.compile('physics-shape-header',[source],['/DPHYSICS_SHAPE_SCHEMA_ONLY'],boundary=True)
            probe=v.compile('physics-shapes',[source]);v.invoke('physics-shapes',[probe],markers=['[PASS] physics factories diagnostics/analytic-payload/source/move/retirement cycles=2'])
        physics=ROOT/'bin'/args.configuration/'PhysicsTests/PhysicsTests.exe'
        for mode,marker in [('sphere-validity',None),('box-features','Box contact features:'),('absolute-scaling','Absolute-scaling regression:')]:
            if mode=='sphere-validity' and args.reuse_leaf:continue
            v.invoke(mode,[physics,'--'+mode],markers=[marker] if marker else [],cwd=v.out/mode)
        v.report['physics_tests_sha256']=sha(physics)
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during validation')
        v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
    except(OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
