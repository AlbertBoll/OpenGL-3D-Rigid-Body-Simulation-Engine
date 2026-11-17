"""Phase27 bounded, create-only build/run evidence. No publication operations."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, stat, subprocess, time
import csv, math, statistics, collections, re
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'logs/pre_editor/phase-27'
MSBUILD = Path('C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe')
PROFILE = 'phase27-qualification'
PROFILE_EVIDENCE = BASE/'build-performance-remediation-01'

def safe(p):
    p = Path(os.path.abspath(p))
    assert p.is_relative_to(ROOT), p
    for a in (p, *p.parents):
        if a.exists(): assert not a.stat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT, a
        if a == ROOT: break
    return p

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def write(p, value):
    p = safe(p); p.parent.mkdir(parents=True, exist_ok=True)
    with p.open('x', encoding='utf-8', newline='\n') as f:
        f.write(value if isinstance(value, str) else json.dumps(value, indent=2) + '\n')

def env():
    e = os.environ.copy()
    temp = safe(ROOT/'runtime/tmp/phase27'); temp.mkdir(parents=True, exist_ok=True)
    e.update(TMP=str(temp), TEMP=str(temp), GIT_CONFIG_COUNT='1',
             GIT_CONFIG_KEY_0='safe.directory', GIT_CONFIG_VALUE_0=ROOT.as_posix())
    for k in list(e):
        if k.startswith(('GENGINE_', 'JPH_')): del e[k]
    return e

def profile_directory(config, kind='bin'):
    if config not in ('Debug', 'Release') or kind not in ('bin', 'bin-int', 'runtime'):
        raise ValueError('Invalid qualification configuration/directory kind')
    return safe(ROOT/(f'runtime/RigidBodySimulation/{PROFILE}/{config}' if kind=='runtime'
                      else f'{kind}/{PROFILE}/{config}'))


def profile_props():
    """Content-addressed prospective import; historical qualification inputs stay immutable."""
    for name, expected in {
        'build-Debug-01/qualification.props':'8e3e30f1323c9138adc8fce6ab1b3b284ef73fb96cfc39acea7d002e107cfb44',
        'candidate-04/qualification.props':'dd7b9210ef70e9680d70dafe826f337c7e234e36a060a5cad5a69c7bfdec65a3',
    }.items():
        if sha(BASE/name)!=expected: raise ValueError('Historical qualification import changed: '+name)
    root=escape(ROOT.as_posix()); historical=escape((BASE/'candidate-04/qualification.props').as_posix())
    content=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="{historical}"/>
  <PropertyGroup>
    <OutDir>{root}/bin/{PROFILE}/$(Configuration)/$(MSBuildProjectName)/</OutDir>
    <IntDir>{root}/bin-int/{PROFILE}/$(Configuration)/$(MSBuildProjectName)/</IntDir>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <ObjectFileName>$(IntDir)</ObjectFileName>
      <PrecompiledHeaderOutputFile>$(IntDir)$(TargetName).pch</PrecompiledHeaderOutputFile>
      <ProgramDataBaseFileName>$(IntDir)$(TargetName).compile.pdb</ProgramDataBaseFileName>
      <AssemblerListingLocation>$(IntDir)</AssemblerListingLocation>
    </ClCompile>
    <Link>
      <OutputFile>$(OutDir)$(TargetName)$(TargetExt)</OutputFile>
      <ProgramDatabaseFile>$(OutDir)$(TargetName).pdb</ProgramDatabaseFile>
      <MapFileName>$(OutDir)phase27.map</MapFileName>
    </Link>
    <Lib><OutputFile>$(OutDir)$(TargetName)$(TargetExt)</OutputFile></Lib>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'">
    <PostBuildEvent><Command>python "{root}/RigidBodySimulation/postbuild.py" config=$(Configuration) prj=RigidBodySimulation profile={PROFILE}</Command></PostBuildEvent>
  </ItemDefinitionGroup>
  <Target Name="ValidatePhase27Profile" BeforeTargets="PrepareForBuild">
    <Error Condition="'$(MSBuildProjectName)'!='RigidBodySimulation' and '$(MSBuildProjectName)'!='GEngine' and '$(MSBuildProjectName)'!='Jolt' and '$(MSBuildProjectName)'!='glad'" Text="Unexpected qualification project"/>
    <Error Condition="'$(Configuration)'!='Debug' and '$(Configuration)'!='Release'" Text="Unexpected qualification configuration"/>
    <Error Condition="'$(VCToolArchitecture)'!='Native64Bit'" Text="Qualification requires the accepted x64 compiler host"/>
  </Target>
</Project>
'''
    digest=hashlib.sha256(content.encode('utf-8')).hexdigest()
    path=safe(PROFILE_EVIDENCE/'profiles'/(digest+'.props'))
    if path.exists():
        if path.read_bytes()!=content.encode('utf-8'): raise ValueError('Profile import content mismatch')
    else: write(path,content)
    return path


def msbuild_arguments(config, props):
    profile_directory(config)
    return [MSBUILD,ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj',f'/p:Configuration={config}',
        '/p:Platform=x64','/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',
        '/p:PreferredToolArchitecture=x64',f'/p:ForceImportBeforeCppTargets={props}',
        '/m:2','/nr:false','/nologo']


def inputs():
    accepted = json.loads((ROOT/'docs/pre_editor/active/PHASE_27_ENTRY_ACCEPTANCE_01.json').read_text())
    paths = accepted['source_allowlist'] + accepted['supporting_allowlist'] + [
        'RigidBodySimulation/include/RbsPch.h', 'RigidBodySimulation/src/RbsPch.cpp',
        'premake5.lua', 'GEngine/GEngine.vcxproj', 'RigidBodySimulation/RigidBodySimulation.vcxproj',
        'external/jolt-build/Jolt.vcxproj', 'docs/pre_editor/active/PHASE_27_JOLT_ARENA_ACCEPTANCE_01.json',
        'logs/pre_editor/phase-27/candidate-01/acceptance-matrix.json',
        'docs/pre_editor/active/PHASE_27_G_CONTACT_VISUAL_ACCEPTANCE_01.json',
        'logs/pre_editor/phase-27/gc-contact-01/acceptance-matrix.json',
        'logs/pre_editor/phase-27/gc-contact-01/arena-source-bounds.json',
        'docs/pre_editor/active/PHASE_27_THUMBNAILS_ACCEPTANCE_01.json',
        'docs/pre_editor/active/PHASE_27_NATIVE_GC_REMEDIATION_AUTHORIZATION_01.json',
        'logs/pre_editor/phase-27/native-gc-remediation-01/entry-and-matrix.json',
        'tools/runtime_assets.json','tools/postbuild.py','RigidBodySimulation/postbuild.py',
        'external/glad/glad.vcxproj',
        'docs/pre_editor/active/BUILD_PERFORMANCE_REMEDIATION_ACCEPTANCE_01.json',
        'docs/pre_editor/active/PHASE_27_BUILD_PROFILE_OUTPUT_PATHS_01.json',
        'logs/pre_editor/phase-27/build-Debug-01/qualification.props',
        'logs/pre_editor/phase-27/candidate-04/qualification.props']
    paths += ['GEngine/include/GEngine/Assets/Images/Editor/Scenes/'+name+'.png' for name in
              ['ball-socket-s','ball-socket-p','ball-socket-g','ball-socket-g-contact']]
    # Tracked third-party DLLs are copied into the profile, never linked as ordinary build products.
    paths += [p.relative_to(ROOT).as_posix() for config in ('Debug','Release')
              for p in sorted((ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'))]
    return {p: sha(ROOT/p) for p in paths if (ROOT/p).is_file()}

def execute(command, directory, timeout, environment, cwd=ROOT):
    write(directory/'command.json', {'command': list(map(str,command)), 'cwd':str(cwd), 'timeout_seconds':timeout})
    start=time.monotonic(); print('START', directory.name, flush=True)
    with (directory/'process.log').open('xb') as log:
        process=subprocess.Popen(list(map(str,command)),cwd=cwd,env=environment,stdout=log,stderr=subprocess.STDOUT)
        timed_out=False
        try: code=process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out=True;process.kill();code=process.wait()
    result={'exit_code':code,'seconds':time.monotonic()-start,'timed_out':timed_out}
    write(directory/'process-result.json',result);print(json.dumps(result),flush=True)
    return result

def build(config, attempt, kind='incremental'):
    directory=safe(BASE/f'build-{config}-{attempt}'); directory.mkdir(parents=True,exist_ok=False)
    props=profile_props()
    if kind=='cold' and any(profile_directory(config,'bin-int').glob('**/*.obj')):
        raise ValueError('Cold population requires an unpopulated object cache')
    before=inputs();write(directory/'inputs.json',before)
    for p in before:
        dest=safe(directory/'source'/p);dest.parent.mkdir(parents=True,exist_ok=True)
        with dest.open('xb') as f:f.write((ROOT/p).read_bytes())
    write(directory/'profile.json',{'profile':PROFILE,'kind':kind,'props':str(props),'props_sha256':sha(props)})
    command=msbuild_arguments(config,props)+['/t:Build','/v:minimal','/clp:PerformanceSummary','/detailedSummary',
        f'/bl:{directory / "build.binlog"};ProjectImports=None',
        f'/flp:logfile={directory / "diagnostic.log"};verbosity=diagnostic;PerformanceSummary']
    result=execute(command,directory,60 if kind=='noop' else 1200,env())
    result['inputs_unchanged']=before==inputs()
    result['profile']=PROFILE;result['kind']=kind
    result['compiled_translation_units']=re.findall(r'^\s{2}([^\s:]+\.c(?:pp)?)\s*$',
        (directory/'process.log').read_text(errors='replace'),re.M)
    result['pch_creations']=result['compiled_translation_units'].count('gepch.cpp')
    result['products']={p.relative_to(ROOT).as_posix():sha(p) for p in [
        profile_directory(config)/'RigidBodySimulation/RigidBodySimulation.exe',
        *[profile_directory(config)/f'{name}/{name}.lib' for name in ('GEngine','Jolt','glad')]] if p.is_file()}
    result['noop_reused']=not result['compiled_translation_units'] if kind=='noop' else None
    write(directory/'result.json',result)
    return 0 if result['exit_code']==0 and result['inputs_unchanged'] and result['noop_reused'] is not False else 1

def run(config, case, backend, attempt, minimal=False, native_attribution=False):
    directory=safe(BASE/f'run-{config}-{case}-{backend}-{attempt}');directory.mkdir(parents=True,exist_ok=False)
    data=directory/'data';data.mkdir();e=env();e['GENGINE_PHASE27_OUTPUT']=str(data)
    e['GENGINE_ASSET_ROOT']=str(profile_directory(config)/'assets')
    if config=='Debug':e['GENGINE_PHASE27_DEBUG']='1'
    if minimal:e['GENGINE_PHASE27_MINIMAL']='1'
    if native_attribution:
        assert config=='Release' and backend=='native' and not minimal
        e['GENGINE_PHASE27_NATIVE_ALLOCATION']='1'
    exe=profile_directory(config)/'RigidBodySimulation/RigidBodySimulation.exe'
    write(directory/'inputs.json',{'sources':inputs(),'executable':{'path':str(exe),'sha256':sha(exe)},
                                  'native_allocation_attribution':native_attribution})
    cwd=profile_directory(config,'runtime');cwd.mkdir(parents=True,exist_ok=True)
    result=execute([exe,'--scene',('ball-socket-g-contact' if case=='G_C' else f'ball-socket-{case.lower()}'),'--backend',backend],directory,180,e,cwd)
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():
            with (directory/name).open('xb') as f:f.write((cwd/name).read_bytes())
    result['capture']=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    write(directory/'result.json',result)
    return 0 if result['exit_code']==0 and result['capture'] and result['capture']['complete'] else 1

def audit(config, case, backend, attempt):
    """Apply frozen numerical criteria to every exported authoritative state.

    Capture completion is deliberately separate from acceptance. Nonzero heap
    calls remain unclassified until a source/runtime attribution is established.
    """
    directory=safe(BASE/f'run-{config}-{case}-{backend}-{attempt}')
    def rows(name):
        with (directory/'data'/name).open(newline='') as f:
            return [{k:float(v) for k,v in r.items()} for r in csv.DictReader(f)]
    samples=rows('samples.csv'); bodies=rows('bodies.csv'); n=len(samples)-1
    assert config=='Release' and n==1200 and len(bodies)==2402
    assert all(int(s['tick'])==i for i,s in enumerate(samples))
    body=[[b for b in bodies if b['tick']==t] for t in range(n+1)]
    assert all(len(b)==2 and [x['body'] for x in b]==[0,1] for b in body)
    matrix=json.loads((BASE/'candidate-01/acceptance-matrix.json').read_text())
    if case=='G_C':matrix=json.loads((BASE/'gc-contact-01/acceptance-matrix.json').read_text())
    policy=matrix['numeric']; checks={}
    def check(name,ok,actual,limit): checks[name]={'result':'PASS' if ok else 'FAIL','actual':actual,'criterion':limit}
    finite=all(math.isfinite(v) for r in samples+bodies for v in r.values())
    check('finite',finite,finite,'all exported values finite')
    def vec(b,keys):return tuple(b[k] for k in keys)
    def norm(v):return math.sqrt(sum(x*x for x in v))
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def quiet(b):return b['inverse_mass']==0 or b['sleeping']==1 or (norm(vec(b,['vx','vy','vz']))<=.05 and norm(vec(b,['wx','wy','wz']))<=.02)
    dynamic=[i for i,b in enumerate(body[0]) if b['inverse_mass']>0]
    asleep=lambda t:all(body[t][i]['sleeping']==1 for i in dynamic)
    check('initial_sleep',asleep(120),asleep(120),'all dynamic bodies asleep by tick120')
    wakes=[t for t in range(121,301) if not asleep(t)]
    check('initial_no_uncommanded_wake',not wakes,wakes[:20],'all remain asleep ticks121..300')
    awake=all(body[301][i]['sleeping']==0 for i in dynamic)
    check('commanded_wake',awake,awake,'all connected dynamic bodies awake by tick301')
    if case=='G':
        wakes=[t for t in range(420,1201) if not asleep(t)]
        check('G_return_to_sleep',not wakes,{'failed_ticks':len(wakes),'first':wakes[:20]},'asleep by420 and through1200')
    if case=='G_C':
        wakes=[t for t in range(660,1201) if not asleep(t)]
        check('G_C_return_to_sleep',not wakes,{'failed_ticks':len(wakes),'first':wakes[:20]},'asleep by660 and through1200')
        check('G_C_commanded_wakes',all(not asleep(t) for t in [421,481,541]),[asleep(t) for t in [421,481,541]],'wake on each ordinary command')
    settled=[]; quiet_run=0
    for t in range(n+1):
        quiet_run=quiet_run+1 if all(quiet(b) for b in body[t]) else 0
        settled.append(t<=300 or (case=='G' and t>=420) or (case=='G_C' and t>=660) or quiet_run>=30)
    errors=[s['error'] for s in samples]
    bad=[t for t in range(1,n+1) if errors[t]>(.01125 if settled[t] else .0225)]
    check('anchor_residual',not bad,{'maximum_m':max(errors),'axis_max_m':[max(abs(s[k]) for s in samples) for k in ['rx','ry','rz']],'first_failed_ticks':bad[:20]},'settled/rest <=.01125m; transient <=.0225m')
    raw=[]; star=[]
    for bs in body:
        kinetic=sum((.5*sum(b[k]**2 for k in ['vx','vy','vz'])+.2*sum(b[k]**2 for k in ['wx','wy','wz']))/b['inverse_mass'] for b in bs if b['inverse_mass']>0)
        raw.append(kinetic+(0 if case=='P' else 12*sum(b['y']/b['inverse_mass'] for b in bs if b['inverse_mass']>0)))
        if case=='S':
            d=sub(vec(bs[1],['x','y','z']),vec(bs[0],['x','y','z']))
            star.append(kinetic+27*(1+d[1]/norm(d)))
        elif case in ('G','G_C'):star.append(kinetic+12*sum(max(b['y']-1.5,0)/b['inverse_mass'] for b in bs))
        else:star.append(kinetic)
    work=[s['work'] for s in samples]
    raw=[e-raw[0]-w for e,w in zip(raw,work)];star=[e-w for e,w in zip(star,work)]
    for name,values in [('raw_energy',raw),('intended_geometry_energy',star)]:
        bad=[t for t in range(1,n+1) if values[t]>(.00125 if t<=300 else .0128125)]
        bins=[statistics.mean(values[t:t+60]) for t in range(301,1201,60)]
        slope=sum((i-7)*v for i,v in enumerate(bins))/sum((i-7)**2 for i in range(15))
        check(name,not bad,{'rest_peak_J':max(values[1:301]),'loaded_peak_J':max(values[301:]),'first_failed_ticks':bad[:20]},'rest excess<=.00125J; loaded<=.0128125J; exact impulse work subtracted')
        check(name+'_growth',slope<=policy['energy_growth_max_J_s'],{'J_per_s':slope,'bin_means_J':bins},policy['energy_growth_max_J_s'])
    # Exact admitted static room boxes, full geometric distance without slop.
    boxes=[((0,0,0),(80,.5,80)),((-79.5,6.5,0),(.5,6,80)),((79.5,6.5,0),(.5,6,80)),((0,6.5,-79.5),(79,6,.5)),((0,6.5,79.5),(79,6,.5))]
    def clearance(b,box):
        center,half=box;d=[abs(v-c)-h for v,c,h in zip(vec(b,['x','y','z']),center,half)]
        return norm([max(x,0) for x in d])+min(max(d),0)-1
    depth=[max(0,max(-clearance(body[t][i],boxes[0]) for i in dynamic)) for t in range(n+1)]
    minimum_other=min(clearance(body[t][i],box) for t in range(n+1) for i in dynamic for box in boxes[1:])
    min_ground=min(clearance(body[t][i],boxes[0]) for t in range(n+1) for i in dynamic)
    check('contact_domain',minimum_other>0 and (case in ('G','G_C') or min_ground>0),{'non_ground_clearance_m':minimum_other,'ground_clearance_m':min_ground},'ground in G; pair and ground in G_C; strictly positive eligible clearance in S/P')
    bad=[t for t in range(1,n+1) if depth[t]>(.02 if settled[t] else .04)]
    check('geometric_overlap',not bad,{'peak_m':max(depth),'first_failed_ticks':bad[:20]},'settled<=.02m; transient<=.04m; no slop subtraction')
    episodes=[]; t=1
    while t<=n:
        if depth[t]<=.02:t+=1;continue
        start=t; recovered=next((u for u in range(t,min(t+4,n-2)+1) if all(depth[v]<=.02 for v in range(u,u+3))),None)
        episodes.append({'start':start,'recovered':recovered});t=(recovered+3 if recovered is not None else n+1)
    check('contact_recovery',all(e['recovered'] is not None for e in episodes),episodes,'<=.02m within4ticks then2confirmation samples; no pending episode')
    if case=='G_C':
        # Per-pair geometric tests; no subtraction of contact slop or solver residual.
        pairs={'sphere_pair':[max(0,2-norm(sub(vec(bs[0],['x','y','z']),vec(bs[1],['x','y','z'])))) for bs in body]}
        for i in dynamic:pairs[f'ground_{i}']=[max(0,-clearance(bs[i],boxes[0])) for bs in body]
        for name,depths in pairs.items():
            bad=[t for t in range(1,n+1) if depths[t]>(.02 if settled[t] else .04)]
            check(name+'_penetration',not bad,{'peak_m':max(depths),'peak_tick':depths.index(max(depths)),'first_failed_ticks':bad[:20],
                'single_impulse_peak_m':max(depths[421:481]),'repeated_impulse_peak_m':max(depths[481:])},'each pair settled<=.02m/transient<=.04m')
            episodes=[];t=1
            while t<=n:
                if depths[t]<=.02:t+=1;continue
                start=t;recovered=next((u for u in range(t,min(t+4,n-2)+1) if all(depths[v]<=.02 for v in range(u,u+3))),None)
                episodes.append({'start':start,'recovered':recovered});t=recovered+3 if recovered is not None else n+1
            check(name+'_recovery',all(e['recovered'] is not None for e in episodes),episodes,'4ticks +2confirmation; no pending episode')
        visual=rows('visual-states.csv');contacts=rows('visual-contacts.csv');impulses=rows('gc-impulses.csv')
        handles=[int(visual[i]['handle']) for i in range(2)];contact_counts=collections.Counter(tuple(sorted([int(c['handle_a']),int(c['handle_b'])])) for c in contacts)
        check('G_C_contacts',all(contact_counts[k]>0 for k in [tuple(sorted(handles)),tuple(sorted([handles[0],6])),tuple(sorted([handles[1],6]))]),
            {str(k):v for k,v in contact_counts.items()},'actual A/B and each sphere/ground contacts recorded')
        check('G_C_filter_and_shapes',len(visual)==2402 and all(v['collide_connected']==1 and v['radius']==1 for v in visual),len(visual),'two radius1 colliders, connected collisions enabled, all ticks')
        check('G_C_impulses',len(impulses)==4 and [r['consumed_tick'] for r in impulses]==[301,421,481,541] and
            all(r['target']==handles[0] for r in impulses) and all((r['jx'],r['jy'],r['jz'])==((0,0,.5) if i==0 else (5,0,0)) for i,r in enumerate(impulses)),impulses,'exact target and four consumed impulses, no invented clicks')
        def quat(b):return vec(b,['qw','qx','qy','qz'])
        def unit(q):
            length=norm(q);return tuple(v/length for v in q)
        def angle(a,b):return 2*math.acos(min(1,abs(sum(x*y for x,y in zip(unit(a),unit(b))))))
        def midpoint(a,b):
            a,b=unit(a),unit(b);sgn=1 if sum(x*y for x,y in zip(a,b))>=0 else -1
            return unit(tuple(x+sgn*y for x,y in zip(a,b)))
        metrics=[0.0]*6
        for lo,hi,ref in [(121,300,120),(661,1200,660)]:
            for i in dynamic:
                for t in range(lo,hi+1):
                    pos=vec(body[t][i],['x','y','z']);q=quat(body[t][i])
                    metrics[5]=max(metrics[5],norm(sub(pos,vec(body[ref][i],['x','y','z']))))
                    for u in range(t+1,min(t+60,hi+1)):
                        metrics[0]=max(metrics[0],norm(sub(pos,vec(body[u][i],['x','y','z']))))
                        metrics[1]=max(metrics[1],angle(q,quat(body[u][i])))
                    if lo<t<hi:
                        mid=tuple((x+y)*.5 for x,y in zip(vec(body[t-1][i],['x','y','z']),vec(body[t+1][i],['x','y','z'])))
                        metrics[2]=max(metrics[2],norm(sub(pos,mid)))
                        metrics[3]=max(metrics[3],angle(q,midpoint(quat(body[t-1][i]),quat(body[t+1][i]))))
        for t in range(301,1200):
            metrics[4]=max(metrics[4],norm(tuple(samples[t][k]-.5*(samples[t-1][k]+samples[t+1][k]) for k in ['rx','ry','rz'])))
        check('G_C_quiet_and_anchor_jitter',all(v<=lim for v,lim in zip(metrics,[.002,.002,.001,.001,.00225,.002])),metrics,
            'position peak .002m; orientation .002rad; position HF .001m; orientation HF .001rad; loaded anchor HF .00225m; quiet reference drift .002m; every sliding60 window')
    def stats(values):
        s=sorted(values);return {'count':len(s),'mean':statistics.mean(s),'median':statistics.median(s),'p95':s[math.ceil(.95*len(s))-1],'p99':s[math.ceil(.99*len(s))-1],'max':s[-1],'rolling60_mean_max':max(statistics.mean(values[i:i+60]) for i in range(len(values)-59))}
    costs={k:stats([s[k] for s in samples[1:]]) for k in ['inclusive_ns','joint_ns','contact_ns','observer_ns']}
    cpu=costs['inclusive_ns'];q=100
    ok=cpu['mean']+q<=1e9/120 and cpu['median']+q<=1e9/120 and cpu['p95']+q<=1e9/80 and cpu['rolling60_mean_max']+q<=1e9/80
    check('inclusive_CPU',ok,cpu,'all1200; +100ns quantum; mean/median<=dt/2; p95/rolling60<=.75dt; no observer subtraction')
    heaps={k:{'sum':sum(s[k] for s in samples[1:]),'max':max(s[k] for s in samples[1:]),'nonzero_ticks':[int(s['tick']) for s in samples[1:] if s[k]][:30]} for k in ['cpp_calls','cpp_bytes','heap_calls','heap_bytes']}
    result={'status':'FAIL' if any(c['result']=='FAIL' for c in checks.values()) else 'PARTIAL_PASS_REMAINING_COVERAGE_PENDING','checks':checks,'costs_ns':costs,'heap_actual_unattributed':heaps,'remaining':['arena runtime and heap attribution','quiet-window/jitter audit','observer controls','ordinary Editor D/E','F_OWNER PENDING'],'input_hashes':{p.name:sha(p) for p in (directory/'data').iterdir() if p.is_file()},'matrix_sha256':sha(BASE/('gc-contact-01/acceptance-matrix.json' if case=='G_C' else 'candidate-01/acceptance-matrix.json')),'auditor_sha256':sha(__file__)}
    write(directory/'audit-01.json',result)
    print(json.dumps({'status':result['status'],'failed':{k:v for k,v in checks.items() if v['result']=='FAIL'},'heaps':heaps,'cpu':cpu},indent=2))
    return 1 if result['status']=='FAIL' else 0

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('action',choices=['build','run','audit']);parser.add_argument('--config',choices=['Debug','Release'],required=True)
    parser.add_argument('--attempt',required=True);parser.add_argument('--case',choices=['S','P','G','G_C'],default='S')
    parser.add_argument('--backend',choices=['native','jolt'],default='native');parser.add_argument('--minimal',action='store_true')
    parser.add_argument('--native-attribution',action='store_true')
    parser.add_argument('--build-kind',choices=['incremental','cold','noop'],default='incremental')
    a=parser.parse_args()
    raise SystemExit(build(a.config,a.attempt,a.build_kind) if a.action=='build' else audit(a.config,a.case,a.backend,a.attempt) if a.action=='audit' else run(a.config,a.case,a.backend,a.attempt,a.minimal,a.native_attribution))
