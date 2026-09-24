"""Phase 65 focused async measurements and numeric final-baseline comparison.

Does not rewrite the Phase 13/48 references or run multiple performance workloads
concurrently. See FINAL_PERFORMANCE_VALIDATION.md for fixed gates and scope.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import winreg

from rendering_baseline import APPS, TIMINGS, LAYOUT, environment, quantiles, samples, summarize
from rendering_validation import ROOT, toolchain


def run(command, directory, name, env, cwd=ROOT, timeout=180):
    command = list(map(str, command))
    with (directory / (name + '.log')).open('wb') as log:
        result = subprocess.run(command, cwd=cwd, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=timeout,
                                creationflags=subprocess.CREATE_NO_WINDOW)
    record = {'command': command, 'cwd': str(cwd), 'exit': result.returncode}
    (directory / (name + '.json')).write_text(json.dumps(record, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError('Command failed: ' + str(directory / (name + '.log')))
    return record


def stats(groups):
    medians = [statistics.median(g) for g in groups]
    center = statistics.median(medians)
    spread = (max(medians) - min(medians)) / center if center else 0
    return dict(**quantiles([x for group in groups for x in group]),
                run_medians=medians, run_median_spread=spread,
                quality='NOISY' if spread > .10 else 'STABLE')


def async_measure(out):
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'summary.json').exists():
        raise ValueError('Preserve completed evidence; choose a new output directory')
    msbuild, vc = toolchain()
    if vc.name != '14.44.35207':
        raise ValueError('Recorded toolset is unavailable')
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
        sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
    version = '10.0.26100.0'
    includes = [vc/'include', *(sdk/'Include'/version/p for p in ('ucrt','shared','um')),
                ROOT/'GEngine/include', ROOT/'GEngine/include/GEngine', ROOT/'GEngine/include/external',
                *(ROOT/'external'/p/'include' for p in ('sdl2','spdlog','glad','assimp','entt','tbb','fmod'))]
    libraries = [vc/'lib/x64', sdk/'Lib'/version/'ucrt/x64', sdk/'Lib'/version/'um/x64',
                 *(ROOT/'external'/p/'lib' for p in ('sdl2','tbb','assimp','fmod'))]
    exe = out/'async-performance.exe'
    command = [vc/'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W3', '/MT', '/O2',
               '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS', '/DGENGINE_CONFIG_RELEASE',
               '/DGENGINE_RENDER_COUNTERS=1', '/DGE_ENABLE_PHYSICS_PROFILING',
               *['/I'+str(p) for p in includes], '/external:W0', '/external:templates-',
               *['/external:I'+str(p) for p in (ROOT/'external',ROOT/'GEngine/include/external',vc/'include')],
               ROOT/'tools/final_performance_probe.cpp', '/Fo'+str(out)+os.sep, '/Fe'+str(exe),
               '/link', '/SUBSYSTEM:CONSOLE', *['/LIBPATH:'+str(p) for p in libraries],
               ROOT/'bin/Release/GEngine/GEngine.lib', ROOT/'external/glad/bin/Release/glad/glad.lib',
               'SDL2.lib','SDL2_ttf.lib','tbb12.lib','tbb12_debug.lib','tbb.lib','tbb_debug.lib',
               'assimp.lib','fmod64_vc.lib','fmodL64_vc.lib','fmodstudio64_vc.lib','fmodstudioL64_vc.lib']
    env = environment()
    env['Path'] = str(ROOT/'bin/Release/RigidBodySimulation') + os.pathsep + env['Path']
    env['GENGINE_ASSET_ROOT'] = str(ROOT/'bin/Release/assets')
    protocol = {'series':3,'warmups_per_asset':2,'samples_per_asset':8,
                'payloads':['Images/Sphere/wood_diffuse.png','Models/barrel.obj'],
                'limits':'AsyncTextureLimits/AsyncMeshLimits defaults; two workers; 64 MiB request/frame; 2 ms between-upload time budget',
                'frame_scope':'FrameScheduler with invisible scene, including normal window-state/present boundary and hidden-window swap; no simulation or scene drawing; not a complete application frame',
                'polling':'1 ms sleep outside measured scheduler CPU; included in ready latency',
                'cache':'fresh loader/registry per sample; warm OS file cache; no cold disk claim',
                'stall_ns':16667000,'noise_spread':.10,
                'attribution':'Private driver-call CPU wall spans accumulated per request; queries may charge earlier GPU work. No added wait or synchronization within samples.'}
    (out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n')
    run(command,out,'compile',env)
    captures=[]
    for series in range(3):
        directory=out/str(series); directory.mkdir(exist_ok=True)
        if (directory/'async.csv').exists(): raise ValueError('Preserve earlier sample files')
        run([exe],directory,'run',env,cwd=directory)
        if '[PASS] async-performance ' not in (directory/'run.log').read_text(errors='replace'):
            raise ValueError('Missing async completion marker')
        with (directory/'async.csv').open() as file:rows=list(csv.DictReader(file))
        with (directory/'async-frames.csv').open() as file:frames=list(csv.DictReader(file))
        with (directory/'idle-frames.csv').open() as file:idle=list(csv.DictReader(file))
        if len(rows)!=16 or len(idle)!=240:raise ValueError('Incomplete async samples')
        for kind in ('texture','mesh'):
            group=[r for r in rows if r['kind']==kind]
            if [int(r['sample']) for r in group]!=list(range(8)) or any(int(r['completed'])!=1 for r in group):
                raise ValueError('Async publication signature mismatch')
        captures.append({'rows':rows,'frames':frames,'idle':idle})
    summary={'protocol':protocol,'assets':{},'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
             'library_sha256':hashlib.sha256((ROOT/'bin/Release/GEngine/GEngine.lib').read_bytes()).hexdigest()}
    for kind in ('texture','mesh'):
        groups=[[r for r in c['rows'] if r['kind']==kind] for c in captures]
        framegroups=[[r for r in c['frames'] if r['kind']==kind] for c in captures]
        metrics={key:stats([[int(r[key]) for r in group] for group in groups])
                 for key in ('request_ns','ready_ns','max_scheduler_ns','names_ns','texture_upload_ns','mipmap_ns','image_query_ns','state_query_ns','buffer_upload_ns')}
        metrics['scheduler_ns']=stats([[int(r['scheduler_ns']) for r in group] for group in framegroups])
        metrics['stall_frames']=sum(int(r['stalls']) for g in groups for r in g)
        metrics['measured_frames']=sum(len(g) for g in framegroups)
        metrics['upload_bytes']=sorted({int(r['upload_bytes']) for g in groups for r in g})
        metrics['texture_calls']=sorted({int(r['texture_calls']) for g in groups for r in g})
        metrics['buffer_calls']=sorted({int(r['buffer_calls']) for g in groups for r in g})
        metrics['time_budget_frames']=sum(int(r['time_budget_reached']) for g in framegroups for r in g)
        summary['assets'][kind]=metrics
    summary['idle_scheduler_ns']=stats([[int(r['scheduler_ns']) for r in c['idle']] for c in captures])
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps({'result':'PASS','summary':str(out/'summary.json'),'stall_frames':{k:v['stall_frames'] for k,v in summary['assets'].items()}}))



def common_capture(out, compile_reference):
    """Replay the original Phase 13 authoring, independently of pass timing."""
    out.mkdir(parents=True, exist_ok=True)
    if (out/'summary.json').exists():
        raise ValueError('Preserve completed evidence; choose a new output directory')
    fixture=out/'editor-phase13';fixture.mkdir(exist_ok=True)
    source=(ROOT/'GEngineEditor/src/SceneApp.cpp').read_text()
    entry='void SceneApp::ImGuiRender()\n{'
    if source.count(entry)!=1:raise ValueError('Editor fixture adapter needs review')
    # Phase 13 BaseApp::Render explicitly commented out its virtual ImGuiRender
    # call. Reproduce that workload in a standalone validation binary only.
    adapted=source.replace(entry,entry+'\n    return; // Phase 13 historical workload: no editor UI authoring.\n',1)
    candidate=fixture/'SceneApp.cpp';candidate.write_text(adapted)
    command=json.loads(compile_reference.read_text())['command']
    rewritten=[]
    for arg in command:
        if arg.replace('\\','/').endswith('/tools/final_performance_probe.cpp'):
            rewritten.extend([str(candidate), *[str(p) for p in sorted((ROOT/'GEngineEditor/src').rglob('*.cpp')) if p.name!='SceneApp.cpp']])
        elif arg.startswith('/Fo'):rewritten.append('/Fo'+str(fixture)+os.sep)
        elif arg.startswith('/Fe'):rewritten.append('/Fe'+str(fixture/'GEngineEditor.exe'))
        else:rewritten.append(arg)
    rewritten.insert(rewritten.index('/link'),'/I'+str(ROOT/'GEngineEditor/src'))
    rewritten.insert(rewritten.index('/link'),'/DGENGINE_RENDER_BASELINE')
    rewritten.insert(rewritten.index('/link'),'/D_SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING')
    env=environment();env['Path']=str(ROOT/'bin/Release/GEngineEditor')+os.pathsep+env['Path']
    env['GENGINE_ASSET_ROOT']=str(ROOT/'bin/Release/assets')
    run(rewritten,fixture,'compile',env)
    (fixture/'adaptation.json').write_text(json.dumps({'source':'GEngineEditor/src/SceneApp.cpp',
        'source_sha256':hashlib.sha256(source.encode()).hexdigest(),
        'fixture_sha256':hashlib.sha256(adapted.encode()).hexdigest(),
        'reference':'render-refactor-phase-13-approved:GEngine/src/Core/BaseApp.cpp::Render',
        'adaptation':'Only suppress virtual UI authoring to reproduce the historical disabled ImGuiRender call; current engine/library unchanged.',
        'required_scene_extent':[1280,720],'required_scene_samples':16},indent=2)+'\n')
    for app in APPS:
        exe=fixture/'GEngineEditor.exe' if app=='GEngineEditor' else ROOT/'bin/Release'/app/(app+'.exe')
        for series in range(3):
            directory=out/app/str(series);directory.mkdir(parents=True,exist_ok=True)
            if (directory/'frames.csv').exists():raise ValueError('Preserve earlier samples')
            (directory/'imgui.ini').write_text(LAYOUT)
            child=dict(env,GENGINE_BASELINE_OUTPUT=str(directory),GENGINE_SHADOW_RESOLUTION='4096')
            if app=='GEngineEditor':child['GENGINE_BASELINE_SCENE_TARGET']='1'
            inputs={'binary_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
                    'layout_sha256':hashlib.sha256(LAYOUT.encode()).hexdigest(),
                    'pass_timing':False,'environment':{k:v for k,v in child.items() if k.startswith('GENGINE_')},
                    'editor_phase13_adapter':app=='GEngineEditor'}
            (directory/'inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
            run([exe],directory,'application',child,cwd=directory,timeout=300)
            samples(directory)
            print('[PASS] common',app,series,flush=True)
    summarize(out,3)


def compare_metric(before, after, pass_metric=False):
    if before is None or after is None:return {'status':'UNAVAILABLE','before':before,'after':after}
    median_floor=10000 if pass_metric else 100000
    tail_floor=20000 if pass_metric else 200000
    change={key:{'before':before[key],'after':after[key],
                 'delta':after[key]-before[key],
                 'percent':100*(after[key]/before[key]-1) if before[key] else None}
            for key in ('median','p95','p99')}
    noisy=before['quality']=='NOISY' or after['quality']=='NOISY'
    material=(change['median']['delta']>median_floor and (change['median']['percent'] or 0)>10) or (
              change['p95']['delta']>tail_floor and (change['p95']['percent'] or 0)>20)
    return {'status':'NOISY' if noisy else 'REVIEW' if material else 'WITHIN_THRESHOLD',
            'material_threshold_exceeded':material,'before_quality':before['quality'],'after_quality':after['quality'],**change}


def compare(out, capture, common):
    out.mkdir(parents=True,exist_ok=True)
    p13=json.loads((ROOT/'rendering-checkpoints/phase-13-baseline.json').read_text())
    p48=json.loads((ROOT/'rendering-checkpoints/phase-48-baseline.json').read_text())
    candidate=json.loads((capture/'phase-48-baseline.json').read_text())
    report={'common':{},'passes':{},'references':{'common':'phase-13-baseline.json','passes':'phase-48-baseline.json'},
            'comparability':'Numeric deltas require the accompanying workload/target/algorithm disposition; no automatic speedup claim.'}
    for app in APPS:
        runs=[samples(common/app/str(i))[0] for i in range(3)]
        timing={key:stats([[r[key] for r in group] for group in runs]) for key in TIMINGS}
        counts={key:sorted({r[key] for group in runs for r in group})
                for key in runs[0][0] if key not in TIMINGS and key!='sample'}
        old=p13['rendering']['applications'][app]
        report['common'][app]={'timing':{k:compare_metric(old['timing'][k],v) for k,v in timing.items()},
                              'candidate_timing':timing,'counter_values':counts,
                              'counter_changes':{k:{'before':old['counters'].get(k),'after':v} for k,v in counts.items()}}
    for app,after in candidate['applications'].items():
        before=p48['applications'][app]
        report['passes'][app]={}
        for label in sorted(set(before['passes'])|set(after['passes'])):
            old=before['passes'].get(label);new=after['passes'].get(label)
            if old is None or new is None:
                report['passes'][app][label]={'status':'ADDED_OR_ABSENT','before':old,'after':new};continue
            report['passes'][app][label]={key:compare_metric(old[key],new[key],True) for key in ('cpu_ns','gpu_ns')}
            report['passes'][app][label]['work']={key:{'before':old[key],'after':new[key]} for key in ('submitted_items','submitted_draws')}
    (out/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
    for app,x in report['common'].items():
        print(app,{k:{'status':v['status'],'median_ms':v['median']['after']/1e6,'delta_percent':v['median']['percent']} for k,v in x['timing'].items() if k in ('work_ns','update_ns','render_ns')})
    print('Wrote',out/'comparison.json')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode',choices=['async','common','compare'])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--common',type=Path)
    parser.add_argument('--compile-reference',type=Path)
    args=parser.parse_args()
    if args.mode=='async':async_measure(args.output.resolve())
    elif args.mode=='common':
        if args.compile_reference is None:parser.error('common requires --compile-reference from the matching async compilation')
        common_capture(args.output.resolve(),args.compile_reference.resolve())
    else:
        if args.capture is None or args.common is None:parser.error('compare requires separate --capture and --common')
        compare(args.output.resolve(),args.capture.resolve(),args.common.resolve())


if __name__=='__main__':
    main()
