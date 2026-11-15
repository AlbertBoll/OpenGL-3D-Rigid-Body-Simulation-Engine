"""Bounded Phase26 build/run orchestration; create-only evidence, no publication."""
from pathlib import Path
import argparse, datetime, hashlib, importlib.util, json, os, shutil, stat, subprocess, time, math, struct, statistics, csv

ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'logs/pre_editor/phase-26/solver-correction-01'
MSBUILD=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe')

def safe(p):
    p=Path(os.path.abspath(p));assert p.is_relative_to(ROOT),p
    for a in (p,*p.parents):
        if a.exists():assert not getattr(a.lstat(),'st_file_attributes',0)&stat.FILE_ATTRIBUTE_REPARSE_POINT,a
        if a==ROOT:break
    return p

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(p,v):
    p=safe(p);p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf-8',newline='\n') as f:f.write(v if isinstance(v,str) else json.dumps(v,indent=2)+'\n')

def authority():
    a=json.loads((ROOT/'docs/pre_editor/active/PHASE_26_ENTRY_ACCEPTANCE_01.json').read_text())
    b=json.loads((ROOT/'docs/pre_editor/active/PHASE_26_STARTUP_ENERGY_ACCEPTANCE_01.json').read_text())
    assert a['execution_command']=='START PRE_EDITOR PHASE 26' and b['status']=='OWNER_ACCEPTED_ACTIVE_PROSPECTIVE'
    assert sha(ROOT/b['exact_proposal']['path'])==b['exact_proposal']['sha256']
    assert sha(ROOT/a['frozen_OD08']['path'])==a['frozen_OD08']['sha256']
    correction=json.loads((BASE/'authorization.json').read_text())
    assert correction['status']=='OWNER_AUTHORIZED_ACTIVE_BOUNDED_PRODUCTION_CORRECTION'
    assert sha(ROOT/correction['controlling_failure_report']['path'])==correction['controlling_failure_report']['sha256']
    return a,b

def witness_authority():
    path=ROOT/'docs/pre_editor/active/PHASE_26_WITNESS_CAPTURE_SLOT_ACCEPTANCE_01.json'
    decision=json.loads(path.read_text())
    assert decision['status']=='OWNER_ACCEPTED_ACTIVE_BOUNDED_SLOT_REASSIGNMENT'
    assert sha(ROOT/decision['proposal']['path'])==decision['proposal']['sha256']
    assert decision==json.loads((BASE/'witness-slot-acceptance-01.json').read_text())
    for p,h in decision['production_hashes'].items():assert sha(ROOT/p)==h,p
    return decision

def build(config,attempt,evidence_base=BASE,presentation=False):
    a,b=authority();d=safe(evidence_base/f'build-{config}-{attempt}');assert not d.exists()
    if evidence_base==IMPULSE:a=impulse_authority()
    if evidence_base==G8IMPULSE:a=g8_impulse_authority()
    existing=list(evidence_base.glob(f'build-{config}-*'))
    if config=='Release' and attempt=='03':
        decision=witness_authority()
        assert decision['release_build']=={'identity':'build-Release-03','count':1,'wall_seconds':1200,'reassigned_from':'unused isolated Release/contact-regression build slot'}
        assert {p.name for p in existing}=={'build-Release-01','build-Release-02'}
        previous=json.loads((BASE/'build-Release-02/result.json').read_text())
        assert previous['exit_code']==0 and previous['inputs_unchanged']
        assert sha(BASE/'candidate-02-stop-01/tested-RigidBodySimulation-Release.exe')==previous['products']['bin/Release/Phase26Qualification/RigidBodySimulation/RigidBodySimulation.exe']
    elif existing:
        # Explicit resume permits the corrected Release closure after interrupted01.
        resume=json.loads((BASE/'resume-01.json').read_text())
        assert config=='Release' and attempt=='02' and existing==[BASE/'build-Release-01']
        assert resume['status']=='BUILD_RELEASE_01_INTERRUPTED_NO_COMPLETION_OR_LINKED_RBS'
        assert not (BASE/'build-Release-01/result.json').exists()
    d.mkdir()
    product='Phase26Presentation' if presentation else 'Phase26Qualification'
    defines='GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS' if presentation else 'GENGINE_PHASE26_QUALIFICATION'
    out=safe(ROOT/f'bin/{config}/{product}');inter=safe(ROOT/f'bin-int/{config}/{product}')
    temp=safe(ROOT/'runtime/tmp/phase26');temp.mkdir(parents=True,exist_ok=True)
    props=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{out.as_posix()}/$(MSBuildProjectName)/</OutDir><IntDir>{inter.as_posix()}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>
<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>{defines};GE_ENABLE_PHYSICS_PROFILING;%(PreprocessorDefinitions)</PreprocessorDefinitions><UndefinePreprocessorDefinitions>GENGINE_PHASE25_QUALIFICATION;GENGINE_PHASE25_ISOLATED_BUILD;GENGINE_PHASE25_ALLOCATIONS;GENGINE_RENDER_BASELINE;GENGINE_PHASE24_QUALIFICATION;GENGINE_PHASE24_OUTLINE_QUALIFICATION;GENGINE_PHASE24_REPLACEMENT_MEASUREMENT;GENGINE_PHASE24_COLLIDER_VALIDATION;%(UndefinePreprocessorDefinitions)</UndefinePreprocessorDefinitions><ShowIncludes>true</ShowIncludes></ClCompile></ItemDefinitionGroup>
</Project>
'''
    write(d/'isolated.props',props)
    # Reuse the existing dependency scanner, not the old diagnostic execution.
    spec=importlib.util.spec_from_file_location('entry_inputs',ROOT/'tools/pre_editor_phase26_entry_diagnostic.py')
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    inputs=m.source_inputs()
    for p in a['source_allowlist']+a['supporting_allowlist']:
        if (ROOT/p).exists():inputs['hashes'][p]=sha(ROOT/p)
    write(d/'inputs.json',inputs)
    for p in a['source_allowlist']+a['supporting_allowlist']:
        if (ROOT/p).exists():
            dest=safe(d/'source'/p);dest.parent.mkdir(parents=True,exist_ok=True)
            with dest.open('xb') as f:f.write((ROOT/p).read_bytes())
    command=[str(MSBUILD),str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build',f'/p:Configuration={config}','/p:Platform=x64','/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}','/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command)
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp))
    start=time.monotonic();print(f'BUILD {config} {attempt} started',flush=True)
    with (d/'build.log').open('xb') as log:
        p=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=p.wait(timeout=1200)
        except subprocess.TimeoutExpired:p.kill();code=p.wait();write(d/'timeout.json',{'timeout_seconds':1200})
    unchanged=all(sha(ROOT/p)==h for p,h in inputs['hashes'].items())
    result={'exit_code':code,'seconds':time.monotonic()-start,'inputs_unchanged':unchanged,'configuration':config,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
    if code==0:
        result['products']={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.exe','.lib')}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    return 0 if code==0 and unchanged else 1

def run(config,mode,case,attempt):
    authority()
    admitted={('Debug','debug','C4'),('Release','ordinary','ordinary')}
    admitted|={('Release','full',c) for c in ('C4','C8','C16','G8')}
    admitted|={('Release','minimal',c) for c in ('C16','G8')}
    assert (config,mode,case) in admitted
    witness_capture=(config,mode,case,attempt)==('Release','full','G8','02')
    if witness_capture:
        decision=witness_authority()
        assert decision['native']['identity']=='run-Release-G8-full-02'
        assert list(BASE.glob('run-Release-G8-full-*'))==[BASE/'run-Release-G8-full-01']
        assert len(list(BASE.glob('run-*')))==8,'exact final slot only'
        build_result=json.loads((BASE/'build-Release-03/result.json').read_text())
        assert build_result['exit_code']==0 and build_result['inputs_unchanged']
        inputs=json.loads((BASE/'build-Release-03/inputs.json').read_text())
        assert all(sha(ROOT/p)==h for p,h in inputs['hashes'].items())
        assert all(sha(ROOT/p)==h for p,h in build_result['products'].items())
        assert json.loads((BASE/'release-prelaunch-03.json').read_text())['status']=='PASS_BUILD_INPUTS_MEMBERSHIP_LINK_AND_BINARY'
    else:
        assert not list(BASE.glob(f'run-{config}-{case}-{mode}-*')),'no repeat authorized'
    if config=='Release':
        previous=json.loads((BASE/'run-Debug-C4-debug-01/result.json').read_text())
        assert previous['exit_code']==0 and previous['native_result']['complete']
        assert json.loads((BASE/'debug-disposition-01.json').read_text())['status']=='DEBUG_APPLICABLE_GATES_PASS'
    assert len(list(BASE.glob('run-*')))<(9 if witness_capture else 8),'no additional native slot authorized'
    d=safe(BASE/f'run-{config}-{case}-{mode}-{attempt}');assert not d.exists();d.mkdir()
    out=safe(ROOT/f'bin/{config}/Phase26Qualification/RigidBodySimulation')
    exe=out/'RigidBodySimulation.exe';assert exe.is_file()
    for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        dest=out/dll.name
        if not dest.exists():shutil.copyfile(dll,dest)
        assert sha(dest)==sha(dll)
    data=d/'data';data.mkdir()
    scene='sphere-box-stack' if case=='ordinary' else 'distance-chain-contact' if case=='G8' else 'distance-chain'
    command=[str(exe),'--scene',scene]
    if case!='ordinary':command+=['--links',case[1:]]
    command+=['--phase26-output',str(data),'--phase26-mode',mode]
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');cwd.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['GENGINE_ASSET_ROOT']=str(ROOT/f'bin/{config}/assets')
    temp=safe(ROOT/'runtime/tmp/phase26');temp.mkdir(parents=True,exist_ok=True);env.update(TMP=str(temp),TEMP=str(temp))
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'deadline_seconds':180,'runtime_asset_root':env['GENGINE_ASSET_ROOT']})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'RUN {config} {case} {mode} started',flush=True)
    with (d/'process.log').open('xb') as log:
        p=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=p.wait(timeout=180)
        except subprocess.TimeoutExpired:p.kill();code=p.wait();write(d/'timeout.json',{'timeout_seconds':180})
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None}
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    write(d/'result.json',result);print(json.dumps(result),flush=True);return 0 if code==0 else 1

def decoded_run(directory):
    meta=json.loads((directory/'data/result.json').read_text());raw=(directory/'data/samples.bin').read_bytes()
    size=meta['sample_bytes'];o=meta['offsets'];assert len(raw)==size*(meta['ticks']+1)
    records=[]
    for tick in range(meta['ticks']+1):
        at=tick*size
        def values(fmt,offset):return struct.unpack_from('<'+fmt,raw,at+offset)
        bodies=[]
        if meta['body_count']<=18:
            for i in range(meta['body_count']):
                b=i*o['body_stride']
                bodies.append({key:values(fmt,b+o[key]) for key,fmt in [('position','3f'),('velocity','3f'),('angular','3f'),('orientation','4f'),('sleeping','?')]})
        rec=dict(zip(('energy','fidelity','geometry','witness','error'),values('5d',o['raw_energy'])))
        rec.update(dict(zip(('inclusive','joint','contact_time'),values('3Q',o['inclusive']))))
        rec.update(dict(zip(('update','observation','allocations','allocation_bytes','joint_allocations'),values('5Q',o['update']))))
        rec.update(tick=tick,bodies=bodies,errors=values('16f',o['errors'])[:meta['links']],
                   impulses=values('16f',o['impulses'])[:meta['links']],work=values('12Q',o['executed_work']),
                   asleep=values('I',o['asleep'])[0])
        rec['contacts'],rec['non_ground_contacts']=values('2I',o['asleep']+4)
        if 'witnesses' in o and meta['body_count']<=18:
            assert rec['contacts']<=32
            rec['witnesses']=[values('dHH',o['witnesses']+i*o['witness_stride']) for i in range(rec['contacts'])]
        records.append(rec)
    return meta,records

def audit(config,mode,case,attempt,evidence_base=BASE):
    """Evaluate fixed OD08 gates from authoritative states; no fitting or new runs."""
    authority();d=evidence_base/f'run-{config}-{case}-{mode}-{attempt}';meta,rows=decoded_run(d)
    failures=[]
    def check(ok,gate,tick=None):
        if not ok and not any(x['gate']==gate for x in failures):failures.append({'gate':gate,'first_tick':tick})
    check(meta['complete'] and meta['clean_shutdown'],'native complete/clean shutdown')
    check(meta['ticks']==meta['target'],'all samples present')
    check(meta['recorder_bytes']<=8388608,'recorder cap')
    def norm(v):return math.sqrt(sum(x*x for x in v))
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def angle(a,b):
        cosine=abs(sum(x*y for x,y in zip(a,b)))/(norm(a)*norm(b))
        return 2*math.acos(min(1.,cosine))
    n=meta['links'];contact=meta['contact'];ordinary=case=='ordinary';work=meta['work']
    expected_count=n+1+int(contact)
    max_energy_excess=-math.inf;max_error=0.;max_displacement=0.;max_hf=0.
    recovery=[None]*n;quiet_count=0;signed=[]
    if not ordinary:
        check(meta['body_count']==expected_count,'deterministic body membership')
        for i,b in enumerate(rows[0]['bodies']):
            expected=(0.,0.,0.) if i==n+1 else (2.25*i,1.5,0.) if contact else (0.,1.5+2.25*(n-i),0.)
            check(b['position']==expected and b['velocity']==(0.,0.,0.) and b['angular']==(0.,0.,0.),'deterministic initial states')
        for r in rows[1:]:
            tick=r['tick'];bodies=r['bodies'];applied=work if tick>300 else 0.;allowance=.00125 if tick<=300 else .0128125
            check(all(math.isfinite(x) for b in bodies for k in ('position','velocity','angular','orientation') for x in b[k]),'finite states',tick)
            quiet=all(b['sleeping'][0] or norm(b['velocity'])<=.05 and norm(b['angular'])<=.02 for b in bodies[1:n+1])
            quiet_count=quiet_count+1 if quiet else 0
            rest=tick<=300 or contact and tick>=420 or quiet_count>=30
            threshold=.01125 if rest else .0225
            errors=[norm(sub(bodies[i]['position'],bodies[i-1]['position']))-2.25 for i in range(1,n+1)]
            signed.append(errors);max_error=max(max_error,max(map(abs,errors)))
            check(max(map(abs,errors))<=threshold,'joint per-row error',tick)
            check(sum(map(abs,errors))<=threshold*n,'aggregate joint drift',tick)
            kinetic=sum(.5*sum(x*x for x in b['velocity'])+.2*sum(x*x for x in b['angular']) for b in bodies[1:n+1])
            energy=kinetic+12*sum(b['position'][1] for b in bodies[1:n+1])
            excess=energy-rows[0]['energy']-applied;max_energy_excess=max(max_energy_excess,excess)
            check(abs(energy-r['energy'])<=1e-9,'raw energy observer agreement',tick)
            check(excess<=allowance,'raw energy peak',tick)
            if contact:fidelity=kinetic+12*sum(max(b['position'][1]-1.5,0.) for b in bodies[1:n+1])
            else:
                fidelity=kinetic+27*sum((n-i+1)*(1+sub(bodies[i]['position'],bodies[i-1]['position'])[1]/norm(sub(bodies[i]['position'],bodies[i-1]['position']))) for i in range(1,n+1))
            check(abs(fidelity-r['fidelity'])<=1e-9,'fidelity observer agreement',tick)
            if contact or tick>120:check(fidelity-applied<=allowance,'fidelity peak',tick)
            if 120<=tick<=300 or contact and tick>=420:check(r['asleep']==n,'sleep deadline/uncommanded wakes',tick)
            if tick==301:check(r['asleep']==0,'commanded connected wake',tick)
            for i in range(1,n+1):
                position=bodies[i]['position'];displacement=norm(sub(position,rows[0]['bodies'][i]['position']))
                max_displacement=max(max_displacement,displacement)
                if tick<=300:check(displacement<=(.02 if contact else .01125*i),'initial displacement',tick)
                reference=120 if tick<=300 else 420
                if 121<=tick<=300 or contact and tick>=421:
                    check(norm(sub(position,rows[reference]['bodies'][i]['position']))<=.002,'settled reference drift',tick)
                if contact:
                    check(bodies[-1]['position']==(0.,0.,0.) and bodies[-1]['orientation']==rows[0]['bodies'][-1]['orientation'],'unchanged ground pose',tick)
                    q=(abs(position[0])-50,abs(position[1])-.5,abs(position[2])-50)
                    depth=max(0.,1.-(norm(tuple(max(x,0.) for x in q))+min(max(q),0.)))
                    settled=121<=tick<=300 or tick>=421 or quiet_count>=30
                    check(depth<=(.02 if settled else .04),'actual geometric penetration',tick)
                    episode=recovery[i-1]
                    if depth>.02:
                        if episode is None:episode=[tick,None]
                        episode[1]=None
                    elif episode is not None:
                        if episode[1] is None:episode[1]=tick
                        if tick>=episode[1]+2:episode=None
                    if episode is not None and tick>episode[0]+4:
                        check(episode[1] is not None and episode[1]<=episode[0]+4,'geometric recovery deadline',tick)
                    recovery[i-1]=episode
                    if tick>300:
                        delta=sub(position,rows[300]['bodies'][i]['position'])
                        check(math.hypot(delta[0],delta[2])<=.25,'G8 loaded domain displacement',tick)
            for i in range(n+1):
                for j in range(i+2,n+1):check(norm(sub(bodies[i]['position'],bodies[j]['position']))>2,'nonadjacent clearance',tick)
            check(r['joint_allocations']==0,'joint hot allocation',tick)
            check(r['non_ground_contacts']==0,'recorded contact domain',tick)
            if 'witnesses' in r:
                for depth,a,b in r['witnesses']:
                    check(math.isfinite(depth) and 0<=a<expected_count and 0<=b<expected_count and a!=b,'witness pair validity',tick)
                    check(contact and (a==n+1 or b==n+1),'witness ground-pair identity',tick)
                check(max([0.]+[x[0] for x in r['witnesses']])==r['witness'],'per-pair witness aggregate agreement',tick)
        check(all(x is None for x in recovery),'no pending penetration recovery')
        for t in range(301,min(meta['ticks'],1200)):
            residual=max(abs(signed[t-1][j]-.5*(signed[t-2][j]+signed[t][j])) for j in range(n))
            max_hf=max(max_hf,residual);check(residual<=.00225,'loaded joint high-frequency residual',t)
        # Union of every complete 60-tick quiet window; peak-to-peak only between
        # pairs that coexist in such a window. Stationary intervals shortcut exactly.
        for lo,hi in [(121,min(300,meta['ticks']))]+([(421,meta['ticks'])] if contact else []):
            if hi-lo+1<60:continue
            for i in range(1,n+1):
                for t in range(lo,hi+1):
                    p=rows[t]['bodies'][i]['position'];q=rows[t]['bodies'][i]['orientation']
                    for u in range(max(lo,t-59),t):
                        old=rows[u]['bodies'][i]
                        check(norm(sub(p,old['position']))<=.002,'quiet position peak-to-peak',t)
                        if q!=old['orientation']:check(angle(q,old['orientation'])<=.002,'quiet angular peak-to-peak',t)
                    if lo<t<hi:
                        before=rows[t-1]['bodies'][i];after=rows[t+1]['bodies'][i]
                        check(norm(tuple(p[k]-.5*(before['position'][k]+after['position'][k]) for k in range(3)))<=.001,'quiet positional high frequency',t)
                        a=before['orientation'];b=after['orientation'];sgn=1 if sum(x*y for x,y in zip(a,b))>=0 else -1
                        midpoint=tuple(x+sgn*y for x,y in zip(a,b))
                        check(angle(q,midpoint)<=.001,'quiet angular high frequency',t)
        if meta['ticks']==1200:
            for key in ('energy','fidelity'):
                bins=[statistics.mean(r[key] for r in rows[301+j*60:361+j*60]) for j in range(15)]
                slope=sum((j-7)*value for j,value in enumerate(bins))/280
                check(slope<=.00125/14,key+' loaded growth')
    def stats(values):
        ordered=sorted(values);count=len(values)
        return {'mean_ns':statistics.mean(values),'median_ns':statistics.median(values),
            'p95_ns':ordered[math.ceil(.95*count)-1],'p99_ns':ordered[math.ceil(.99*count)-1],'max_ns':max(values),
            'max_rolling60_ns':max((sum(values[i:i+60])/60 for i in range(count-59)),default=0)}
    costs={k:stats([r[k] for r in rows[1:]]) for k in ('inclusive','update','joint','contact_time','observation')} if meta['ticks'] else {}
    if config=='Release' and meta['ticks']==1200:
        quantum=json.loads((BASE/'reference-hardware.json').read_text())['clock_quantum_ns']
        check(0<quantum<=1000,'clock admission')
        for key in ('mean_ns','median_ns'):check(costs['inclusive'][key]+quantum<=1e9/120,'inclusive CPU safety '+key)
        for key in ('p95_ns','max_rolling60_ns'):check(costs['inclusive'][key]+quantum<=1e9/80,'inclusive CPU safety '+key)
    result={'status':'PASS' if not failures else 'FAIL','scope':f'{config} {case} {mode}; only measured windows; no human acceptance',
        'failures':failures,'ticks':meta['ticks'],'maximum_raw_energy_excess_J':None if ordinary else max_energy_excess,
        'maximum_joint_error_m':max_error,'maximum_joint_HF_m':max_hf,'costs':costs,
        'allocations':sum(r['allocations'] for r in rows[1:]),'joint_hot_allocations':sum(r['joint_allocations'] for r in rows[1:]),
        'allocation_disposition':'All Update allocations require attribution; zero hot-row count alone is not full incremental graph/allocation PASS',
        'input_hashes':{str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in [d/'data/result.json',d/'data/samples.bin',Path(__file__)]}}
    write(d/'audit-01.json',result);print(json.dumps(result,indent=2));return int(bool(failures))

STARTUP=ROOT/'logs/pre_editor/phase-26/startup-selection-01'
IMPULSE=ROOT/'logs/pre_editor/phase-26/impulse-control-01'
G8IMPULSE=ROOT/'logs/pre_editor/phase-26/g8-impulse-control-01'

def g8_impulse_authority():
    decision=json.loads((G8IMPULSE/'authorization.json').read_text())
    assert decision==json.loads((ROOT/'docs/pre_editor/active/PHASE_26_G8_IMPULSE_CONTROL_AUTHORIZATION_01.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_G8_PRESENTATION_IMPULSE_EXTENSION'
    for p in decision['unchanged_physics']:assert sha(ROOT/p)==decision['before_hashes'][p],p
    assert sha(ROOT/decision['previous_spec'])==decision['previous_spec_sha256']
    frozen=json.loads((G8IMPULSE/'frozen-inputs.json').read_text())
    assert all(sha(ROOT/p)==h for p,h in frozen['owned_hashes'].items())
    return decision

def g8_impulse_build(config):
    g8_impulse_authority()
    return build(config,'01',G8IMPULSE,presentation=config=='Release')

def g8_impulse_run(config):
    assert config=='Release'
    g8_impulse_authority()
    d=safe(G8IMPULSE/'run-Release-G8-presentation-01');assert not list(G8IMPULSE.glob('run-*'))
    for c in ['Debug','Release']:
        result=json.loads((G8IMPULSE/f'build-{c}-01/result.json').read_text())
        assert result['exit_code']==0 and result['inputs_unchanged'] and result['seconds']<=1200
        inputs=json.loads((G8IMPULSE/f'build-{c}-01/inputs.json').read_text())
        assert all(sha(ROOT/p)==h for p,h in inputs['hashes'].items())
        assert all(sha(ROOT/p)==h for p,h in result['products'].items())
    d.mkdir();out=safe(ROOT/'bin/Release/Phase26Presentation/RigidBodySimulation');exe=out/'RigidBodySimulation.exe'
    for dll in (ROOT/'bin/Release/RigidBodySimulation').glob('*.dll'):
        dest=out/dll.name
        if not dest.exists():shutil.copyfile(dll,dest)
        assert sha(dest)==sha(dll)
    data=d/'data';data.mkdir()
    command=[str(exe),'--phase26-impulse-output',str(data)]
    cwd=safe(ROOT/'runtime/RigidBodySimulation/Release')
    env=os.environ.copy();env['GENGINE_ASSET_ROOT']=str(ROOT/'bin/Release/assets')
    temp=safe(ROOT/'runtime/tmp/phase26');temp.mkdir(parents=True,exist_ok=True);env.update(TMP=str(temp),TEMP=str(temp))
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'deadline_seconds':180,'maximum_ticks':120,'maximum_ui_frames':1800,'runtime_asset_root':env['GENGINE_ASSET_ROOT'],'purpose':'G8 presentation control, real ground contacts and natural connected wake; no qualification/performance sample replacement'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print('G8 IMPULSE Release presentation started',flush=True)
    with (d/'process.log').open('xb') as log:
        p=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=p.wait(timeout=180)
        except subprocess.TimeoutExpired:p.kill();code=p.wait();write(d/'timeout.json',{'timeout_seconds':180})
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':native}
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    log=(d/'process.log').read_text(errors='replace')
    passed=bool(code==0 and native and native['presentation_only'] and native['pass'] and native['g8'] and
        native['ticks']<=120 and native['frames']<=1800 and native['panels']>0 and native['applied_count']==1 and
        0<native['sleep_tick']<native['kick_tick']<120 and native['wake_tick']==native['kick_tick'] and
        native['all_awake_at_wake'] and native['all_awake_after_kick'] and native['post_kick_ground_contacts']>0 and
        native['boundaries']>=30 and 'static-ground/pause/single-shot/close PASS' in log and 'Scene Destructor was called!' in log)
    write(d/'audit-01.json',{'status':'PASS' if passed else 'FAIL','presentation_only':True,'native':native,
        'clean_shutdown':'exit0 and scene destructor' if passed else 'not established','no_physics_or_performance_qualification':True})
    return 0 if passed else 1

def impulse_authority():
    decision=json.loads((IMPULSE/'authorization.json').read_text())
    assert decision==json.loads((ROOT/'docs/pre_editor/active/PHASE_26_IMPULSE_CONTROL_AUTHORIZATION_01.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_PRESENTATION_IMPULSE_SCOPE_AMENDMENT'
    for p in decision['unchanged_physics']:assert sha(ROOT/p)==decision['before_hashes'][p],p
    assert sha(ROOT/decision['previous_spec'])==decision['previous_spec_sha256']
    frozen=json.loads((IMPULSE/'frozen-inputs.json').read_text())
    assert all(sha(ROOT/p)==h for p,h in frozen['owned_hashes'].items())
    return decision

def impulse_build(config):
    impulse_authority()
    return build(config,'01',IMPULSE,presentation=config=='Release')

def impulse_run(config):
    impulse_authority()
    mode,case=('debug','C4') if config=='Debug' else ('presentation','C16')
    d=safe(IMPULSE/f'run-{config}-{case}-{mode}-01');assert not d.exists()
    assert len(list(IMPULSE.glob('run-*')))<2,'presentation revision native cap'
    result=json.loads((IMPULSE/f'build-{config}-01/result.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged'] and result['seconds']<=1200
    inputs=json.loads((IMPULSE/f'build-{config}-01/inputs.json').read_text())
    assert all(sha(ROOT/p)==h for p,h in inputs['hashes'].items())
    assert all(sha(ROOT/p)==h for p,h in result['products'].items())
    if config=='Release':assert json.loads((IMPULSE/'run-Debug-C4-debug-01/audit-01.json').read_text())['status']=='PASS'
    d.mkdir();product='Phase26Qualification' if config=='Debug' else 'Phase26Presentation'
    out=safe(ROOT/f'bin/{config}/{product}/RigidBodySimulation');exe=out/'RigidBodySimulation.exe'
    for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        dest=out/dll.name
        if not dest.exists():shutil.copyfile(dll,dest)
        assert sha(dest)==sha(dll)
    data=d/'data';data.mkdir()
    command=[str(exe)]+(['--scene','distance-chain','--links','4','--phase26-output',str(data),'--phase26-mode','debug']
        if config=='Debug' else ['--phase26-impulse-output',str(data)])
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');cwd.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['GENGINE_ASSET_ROOT']=str(ROOT/f'bin/{config}/assets')
    temp=safe(ROOT/'runtime/tmp/phase26');temp.mkdir(parents=True,exist_ok=True);env.update(TMP=str(temp),TEMP=str(temp))
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'deadline_seconds':180,'runtime_asset_root':env['GENGINE_ASSET_ROOT'],'purpose':'Presentation controls and qualification isolation only; no replacement performance samples'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'IMPULSE RUN {config} {case} started',flush=True)
    with (d/'process.log').open('xb') as log:
        p=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=p.wait(timeout=180)
        except subprocess.TimeoutExpired:p.kill();code=p.wait();write(d/'timeout.json',{'timeout_seconds':180})
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':native}
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=code==0 and native and (native['complete'] and native['clean_shutdown'] and native['ticks']==120
        if config=='Debug' else native['presentation_only'] and native['pass'] and native['ticks']<=120 and native['frames']<=240 and native['panels']>0 and native['applied_count']==1)
    if config=='Release':
        log=(d/'process.log').read_text(errors='replace')
        passed=passed and 'Scene Destructor was called!' in log
        write(d/'audit-01.json',{'status':'PASS' if passed else 'FAIL','presentation_only':True,'clean_shutdown':'exit0 and scene destructor' if passed else 'not established','native':native,'no_physics_or_performance_qualification':True})
    return 0 if passed else 1

def impulse_audit(config):
    assert config=='Debug'
    impulse_authority()
    return audit(config,'debug','C4','01',IMPULSE)

def startup_authority():
    decision=json.loads((ROOT/'docs/pre_editor/active/PHASE_26_DUAL_SCENE_SELECTION_AUTHORIZATION_01.json').read_text())
    assert decision==json.loads((STARTUP/'authorization.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_DUAL_ENUM_AND_COMMAND_LINE_STARTUP_REVISION'
    for p,h in decision['unchanged_physics_and_fixture_hashes'].items():assert sha(ROOT/p)==h,p
    assert sha(ROOT/decision['previous_ready']['path'])==decision['previous_ready']['sha256']
    frozen=json.loads((STARTUP/'frozen-inputs.json').read_text())
    assert all(sha(ROOT/p)==h for p,h in frozen['owned_hashes'].items())
    return decision

def startup_build(config):
    startup_authority()
    return build(config,'01',STARTUP)

def startup_run(config):
    startup_authority()
    mode,case=('debug','C4') if config=='Debug' else ('ordinary','ordinary')
    d=safe(STARTUP/f'run-{config}-{case}-{mode}-01');assert not d.exists()
    assert len(list(STARTUP.glob('run-*')))<2,'startup-only native cap'
    result=json.loads((STARTUP/f'build-{config}-01/result.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged'] and result['seconds']<=1200
    inputs=json.loads((STARTUP/f'build-{config}-01/inputs.json').read_text())
    assert all(sha(ROOT/p)==h for p,h in inputs['hashes'].items())
    assert all(sha(ROOT/p)==h for p,h in result['products'].items())
    if config=='Release':assert json.loads((STARTUP/'run-Debug-C4-debug-01/audit-01.json').read_text())['status']=='PASS'
    d.mkdir();out=safe(ROOT/f'bin/{config}/Phase26Qualification/RigidBodySimulation');exe=out/'RigidBodySimulation.exe'
    for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        dest=out/dll.name
        if not dest.exists():shutil.copyfile(dll,dest)
        assert sha(dest)==sha(dll)
    data=d/'data';data.mkdir()
    # Debug exercises an actual command-line override. Release supplies no scene
    # arguments and therefore exercises the compiled Owner default.
    command=[str(exe)]+(['--scene','distance-chain','--links','4'] if config=='Debug' else [])
    command+=['--phase26-output',str(data),'--phase26-mode',mode]
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');cwd.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['GENGINE_ASSET_ROOT']=str(ROOT/f'bin/{config}/assets')
    temp=safe(ROOT/'runtime/tmp/phase26');temp.mkdir(parents=True,exist_ok=True);env.update(TMP=str(temp),TEMP=str(temp))
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'deadline_seconds':180,'runtime_asset_root':env['GENGINE_ASSET_ROOT'],'purpose':'Affected startup selection only; original physics/performance qualification remains historical controlling evidence'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'STARTUP RUN {config} {case} started',flush=True)
    with (d/'process.log').open('xb') as log:
        p=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=p.wait(timeout=180)
        except subprocess.TimeoutExpired:p.kill();code=p.wait();write(d/'timeout.json',{'timeout_seconds':180})
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':native}
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    return 0 if code==0 and native and native['complete'] and native['clean_shutdown'] and native['ticks']==(120 if config=='Debug' else 420) else 1

def startup_audit(config):
    startup_authority()
    return audit(config,'debug' if config=='Debug' else 'ordinary','C4' if config=='Debug' else 'ordinary','01',STARTUP)

def witness_compare():
    """Complete the accepted witness gap without replacing historical timings."""
    witness_authority()
    old=BASE/'run-Release-G8-full-01';new=BASE/'run-Release-G8-full-02'
    ma,ra=decoded_run(old);mb,rb=decoded_run(new)
    ba=(old/'data/samples.bin').read_bytes();bb=(new/'data/samples.bin').read_bytes()
    failures=[]
    def check(ok,why,tick=None):
        if not ok and not any(f['gate']==why for f in failures):failures.append({'gate':why,'first_tick':tick})
    check(ma['ticks']==mb['ticks']==1200 and ma['body_count']==mb['body_count']==10,'complete G8 membership')
    check(mb['complete'] and mb['clean_shutdown'],'native capture completion')
    check(json.loads((new/'audit-01.json').read_text())['status']=='PASS','unchanged numerical audit')
    check(mb['total_recorder_bytes']==mb['recorder_bytes']+mb['allocation_recorder_bytes']<8388608,'combined recorder below 8 MiB')
    for key in ma['offsets']:check(ma['offsets'][key]==mb['offsets'][key],'unchanged original sample offsets')
    layout=mb['identity_layout'];stride=mb['offsets']['body_stride']
    # These ranges exclude only the MSVC x64 alignment holes. Layout is emitted
    # by the actual compiler; the unchanged PhysicsBodyState provider is pinned.
    check(stride==272 and layout=={'handle':0,'index':0,'generation':8,'registry':16,'type':24,'static_type':0,'dynamic_type':1},'authoritative state ABI')
    def identity(raw,meta,tick,ordinal):
        at=tick*meta['sample_bytes']+ordinal*stride
        handle=tuple(struct.unpack_from('<'+fmt,raw,at+layout['handle']+layout[key])[0]
                     for key,fmt in [('index','I'),('generation','Q'),('registry','Q')])
        return handle,struct.unpack_from('<i',raw,at+layout['type'])[0]
    initial=[identity(bb,mb,0,i) for i in range(10)]
    pairs=set();count=0;max_contacts=0;peak=0.
    output=safe(new/'contact-witnesses-01.csv')
    with output.open('x',encoding='utf-8',newline='') as f:
        writer=csv.writer(f);writer.writerow(['tick','contact','a_ordinal','a_index','a_generation','a_registry','a_role','b_ordinal','b_index','b_generation','b_registry','b_role','retained_depth_m'])
        for tick,(a,b) in enumerate(zip(ra,rb)):
            handles=[identity(bb,mb,tick,i) for i in range(10)]
            check(handles==initial,'stable live body handles/types',tick)
            check(len(set(h for h,t in handles))==10 and len(set(h[2] for h,t in handles))==1,'unique world-domain identities',tick)
            for i,(h,typ) in enumerate(handles):
                check(h[0]!=0xffffffff and h[1]>0 and h[2]>0,'valid full body handle',tick)
                check(typ==(layout['static_type'] if i in (0,9) else layout['dynamic_type']),'static root/ground and dynamic links',tick)
                check(identity(ba,ma,tick,i)==(h,typ),'historical handle identity agreement',tick)
                old_at=tick*ma['sample_bytes']+i*stride;new_at=tick*mb['sample_bytes']+i*stride
                for lo,hi in ((0,4),(8,236),(240,265)):
                    check(ba[old_at+lo:old_at+hi]==bb[new_at+lo:new_at+hi],'every semantic body-state byte agrees',tick)
            for key in ('errors','impulses','work','energy','fidelity','geometry','witness','error','asleep','contacts','non_ground_contacts','allocations','allocation_bytes','joint_allocations'):
                check(a[key]==b[key],'historical '+key+' trace agreement',tick)
            check(b['contacts']==len(b['witnesses'])<=32 and b['non_ground_contacts']==0,'complete bounded contact capture',tick)
            max_contacts=max(max_contacts,b['contacts'])
            depths=[]
            for j,(depth,ia,ib) in enumerate(b['witnesses']):
                valid=0<=ia<10 and 0<=ib<10 and ia!=ib and math.isfinite(depth)
                check(valid,'valid finite witness endpoints/depth',tick)
                if not valid:continue
                ha,ta=handles[ia];hb,tb=handles[ib]
                check((ia==9 and 1<=ib<=8) or (ib==9 and 1<=ia<=8),'exact ground/dynamic contact classification',tick)
                check(ha!=hb and ha[2]==hb[2],'pair identity consistency',tick)
                writer.writerow([tick,j,ia,*ha,'ground' if ia==9 else 'dynamic',ib,*hb,'ground' if ib==9 else 'dynamic',repr(depth)])
                pairs.add(tuple(sorted((ha,hb))));count+=1;depths.append(depth)
            maximum=max([0.]+depths);peak=max(peak,maximum)
            check(maximum==a['witness']==b['witness'],'exact per-tick aggregate witness agreement',tick)
    check(count==sum(r['contacts'] for r in rb) and count>0 and len(pairs)==8,'complete admitted witness population')
    report={'status':'PASS' if not failures else 'FAIL','failures':failures,'completed_ticks':1200,'initial_sample_also_compared':True,
        'witness_records':count,'distinct_authoritative_pairs':len(pairs),'maximum_contacts_per_tick':max_contacts,'maximum_retained_depth_m':peak,
        'recorder_bytes':mb['recorder_bytes'],'allocation_recorder_bytes':mb['allocation_recorder_bytes'],'total_recorder_bytes':mb['total_recorder_bytes'],
        'historical_contact_coverage':'Old run retained count/domain/aggregate only; these agree at every tick. Individual associations are new evidence, not reconstructed historical observations.',
        'performance':'Original full/minimal results and conclusions remain controlling. New capture timing is diagnostic only, never a replacement sample. Qualified-reference goal remains INCONCLUSIVE.',
        'inputs':{str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in [old/'data/result.json',old/'data/samples.bin',new/'data/result.json',new/'data/samples.bin',new/'audit-01.json',output,Path(__file__)]}}
    write(new/'witness-comparison-01.json',report);print(json.dumps(report,indent=2));return int(bool(failures))

def coff_address_symbols(exe, addresses, image_base, config):
    """Read-only symbol recovery: match complete COFF code, masking relocations.

    Release has no PDB. No relink, disassembly heuristic, or guessed address map.
    Every accepted section must match all non-relocation instruction bytes.
    """
    import ctypes as c
    data=exe.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
    section_count=struct.unpack_from('<H',data,pe+6)[0]
    section_at=pe+24+struct.unpack_from('<H',data,pe+20)[0]
    regions=[]
    for i in range(section_count):
        at=section_at+40*i;virtual_size,rva,size,offset=struct.unpack_from('<4I',data,at+8)
        flags=struct.unpack_from('<I',data,at+36)[0]
        if flags&0x20:regions.append((rva,data[offset:offset+size]))
    wanted={x-image_base for x in addresses};found={};evidence=[]
    dbg=c.WinDLL('dbghelp');dbg.UnDecorateSymbolName.argtypes=[c.c_char_p,c.c_char_p,c.c_ulong,c.c_ulong]
    def undecorate(name):
        buf=c.create_string_buffer(4096)
        return buf.value.decode(errors='replace') if dbg.UnDecorateSymbolName(name.encode(),buf,len(buf),0) else name
    names={'PhysicsSystem','Broadphase','ConstraintPenetration','Manifold','Intersect','Intersections','GJK','EPA','main','RbsFrame','_Scene','BaseApp'}
    objects=[p for p in (ROOT/f'bin-int/{config}/Phase26Qualification').rglob('*.obj') if p.stem in names]
    for obj in objects:
        raw=obj.read_bytes();big=raw[:4]==b'\0\0\xff\xff'
        if big:count,table,symbols=struct.unpack_from('<III',raw,44);header=56;stride=20
        else:count=struct.unpack_from('<H',raw,2)[0];table,symbols=struct.unpack_from('<II',raw,8);header=20+struct.unpack_from('<H',raw,16)[0];stride=18
        strings=table+symbols*stride;by_section={};i=0
        while i<symbols:
            at=table+i*stride;namebytes=raw[at:at+8]
            if namebytes[:4]==b'\0'*4:
                start=strings+struct.unpack_from('<I',namebytes,4)[0];end=raw.find(b'\0',start);name=raw[start:end].decode(errors='replace')
            else:name=namebytes.split(b'\0')[0].decode(errors='replace')
            value=struct.unpack_from('<I',raw,at+8)[0]
            section=struct.unpack_from('<i' if big else '<h',raw,at+12)[0]
            typ=struct.unpack_from('<H',raw,at+(16 if big else 14))[0];aux=raw[at+stride-1]
            if typ&0x20 and section>0:by_section.setdefault(section,[]).append((value,name))
            i+=1+aux
        matched=[]
        for section,syms in by_section.items():
            at=header+(section-1)*40;size,offset,relocation=struct.unpack_from('<III',raw,at+16)
            reloc_count=struct.unpack_from('<H',raw,at+32)[0];flags=struct.unpack_from('<I',raw,at+36)[0]
            if not flags&0x20 or size<16:continue
            code=raw[offset:offset+size];masked=set();relocs=[]
            for j in range(reloc_count):
                pos,_,typ=struct.unpack_from('<IIH',raw,relocation+10*j)
                width=8 if typ==1 else 2 if typ==10 else 4
                masked.update(range(pos,pos+width));relocs.append((pos,width))
            runs=[];start=0
            for pos in sorted(masked):
                if pos>start:runs.append((start,pos))
                start=pos+1
            if start<size:runs.append((start,size))
            if not runs:continue
            a,z=max(runs,key=lambda x:x[1]-x[0]);anchor=code[a:min(z,a+32)]
            if len(anchor)<12:continue
            for rva,region in regions:
                search=0
                while True:
                    pos=region.find(anchor,search)
                    if pos<0:break
                    search=pos+1;base=pos-a;lo=rva+base;hi=lo+size
                    targets=[x for x in wanted if lo<=x<hi]
                    if base<0 or base+size>len(region) or not targets:continue
                    if not all(code[x:y]==region[base+x:base+y] for x,y in runs):continue
                    matched.append({'section':section,'rva':lo,'bytes':size,'symbols':[undecorate(n) for _,n in syms],
                        'code_sha256':hashlib.sha256(code).hexdigest(),'relocation_masks':relocs})
                    ordered=sorted(syms)
                    for target in targets:
                        eligible=[(v,n) for v,n in ordered if v<=target-lo]
                        if eligible:
                            v,n=eligible[-1];found.setdefault(target+image_base,[]).append({'name':undecorate(n),
                                'object':str(obj.relative_to(ROOT)).replace('\\','/'),'offset':target-lo-v})
        if matched:evidence.append({'object':str(obj.relative_to(ROOT)).replace('\\','/'),'sha256':sha(obj),'matches':matched})
    return found,evidence

def symbolize_allocations(directory,config):
    """Resolve recorded allocation stacks against the actual local tested image/PDB."""
    import ctypes as c
    trace=json.loads((directory/'data/allocations.json').read_text())
    exe=ROOT/f'bin/{config}/Phase26Qualification/RigidBodySimulation/RigidBodySimulation.exe'
    invocation=json.loads((directory/'invocation.json').read_text())
    assert sha(exe)==invocation['exe_sha256']
    class Symbol(c.Structure):
        _fields_=[('size',c.c_ulong),('type',c.c_ulong),('reserved',c.c_ulonglong*2),
            ('index',c.c_ulong),('length',c.c_ulong),('module',c.c_ulonglong),('flags',c.c_ulong),
            ('value',c.c_ulonglong),('address',c.c_ulonglong),('register',c.c_ulong),('scope',c.c_ulong),
            ('tag',c.c_ulong),('nameLength',c.c_ulong),('maxName',c.c_ulong),('name',c.c_char*1)]
    class Line(c.Structure):
        _fields_=[('size',c.c_ulong),('key',c.c_void_p),('number',c.c_ulong),('file',c.c_char_p),('address',c.c_ulonglong)]
    kernel=c.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=c.c_void_p
    process=kernel.GetCurrentProcess();dbg=c.WinDLL('dbghelp',use_last_error=True)
    dbg.SymInitializeW.argtypes=[c.c_void_p,c.c_wchar_p,c.c_int]
    dbg.SymCleanup.argtypes=[c.c_void_p]
    assert dbg.SymInitializeW(process,str(exe.parent),False),c.get_last_error()
    try:
        dbg.SymSetOptions(0x12)
        dbg.SymLoadModuleExW.argtypes=[c.c_void_p,c.c_void_p,c.c_wchar_p,c.c_wchar_p,c.c_ulonglong,c.c_ulong,c.c_void_p,c.c_ulong]
        dbg.SymLoadModuleExW.restype=c.c_ulonglong
        assert dbg.SymLoadModuleExW(process,None,str(exe),None,trace['image_base'],0,None,0),c.get_last_error()
        dbg.SymFromAddr.argtypes=[c.c_void_p,c.c_ulonglong,c.POINTER(c.c_ulonglong),c.POINTER(Symbol)]
        dbg.SymGetLineFromAddr64.argtypes=[c.c_void_p,c.c_ulonglong,c.POINTER(c.c_ulong),c.POINTER(Line)]
        cache={}
        def resolve(address):
            if address in cache:return cache[address]
            buffer=c.create_string_buffer(c.sizeof(Symbol)+2048);p=c.cast(buffer,c.POINTER(Symbol))
            p.contents.size=c.sizeof(Symbol);p.contents.maxName=2048;displacement=c.c_ulonglong()
            value={'address':address}
            if dbg.SymFromAddr(process,address,c.byref(displacement),p):
                value['name']=c.string_at(c.addressof(buffer)+Symbol.name.offset,p.contents.nameLength).decode(errors='replace')
            else:value['unresolved_error']=c.get_last_error()
            line=Line();line.size=c.sizeof(Line);offset=c.c_ulong()
            if dbg.SymGetLineFromAddr64(process,address,c.byref(offset),c.byref(line)):
                value.update(file=line.file.decode(errors='replace'),line=line.number)
            cache[address]=value;return value
        events=[]
        for event in trace['events']:
            entry={k:v for k,v in event.items() if k!='stack'}
            entry['stack']=[resolve(x) for x in event['stack']];events.append(entry)
        meta,rows=decoded_run(directory)
        assert sum(e.get('occurrences',1) for e in events)==sum(x['allocations'] for x in rows[1:])
        assert sum(e.get('occurrences',1)*e['bytes'] for e in events)==sum(x['allocation_bytes'] for x in rows[1:])
        pdb=exe.with_suffix('.pdb');coff=[]
        if not pdb.exists():
            recovered,coff=coff_address_symbols(exe,{x for e in trace['events'] for x in e['stack']},trace['image_base'],config)
            for event in events:
                for frame in event['stack']:
                    if frame['address'] in recovered:frame['verified_coff_symbols']=recovered[frame['address']]
        result={'status':'COMPLETE_RECORDED_COUNTS_REQUIRE_ATTRIBUTION','events':events,
            'exe_sha256':sha(exe),'pdb_sha256':sha(pdb) if pdb.exists() else None,'coff_evidence':coff,
            'trace_sha256':sha(directory/'data/allocations.json')}
        write(directory/'allocation-symbols-02.json',result)
        return result
    finally:dbg.SymCleanup(process)

def summarize():
    authority();failures=[];reports={};parity={}
    quantum=json.loads((BASE/'reference-hardware.json').read_text())['clock_quantum_ns']
    for case in ('C4','C8','C16','G8'):
        d=BASE/f'run-Release-{case}-full-01';meta,rows=decoded_run(d)
        audit_result=json.loads((d/'audit-01.json').read_text());assert audit_result['status']=='PASS'
        transitions={str(i):[(t,rows[t]['bodies'][i]['sleeping'][0]) for t in range(1,1201)
            if rows[t]['bodies'][i]['sleeping']!=rows[t-1]['bodies'][i]['sleeping']] for i in range(1,meta['links']+1)}
        slopes={}
        for key in ('energy','fidelity'):
            bins=[statistics.mean(x[key] for x in rows[301+j*60:361+j*60]) for j in range(15)]
            slopes[key]=sum((j-7)*v for j,v in enumerate(bins))/280
        windows={}
        for name,lo,hi in [('initial',1,120),('sleep',121,300),('loaded',301,1200)]:
            windows[name]={key:statistics.mean(x[key] for x in rows[lo:hi+1]) for key in ('inclusive','joint','contact_time')}
        reports[case]={'audit_sha256':sha(d/'audit-01.json'),'sleep_transitions':transitions,
            'max_geometric_overlap_m':max(x['geometry'] for x in rows),
            'max_retained_witness_depth_m':max(x['witness'] for x in rows),
            'loaded_growth_J_per_s':slopes,'window_mean_ns':windows,
            'inclusive_mean_ns_per_link':audit_result['costs']['inclusive']['mean_ns']/meta['links'],
            'all_tick_costs':audit_result['costs'],'joint_storage_bytes':meta['joint_storage_bytes'],
            'steady_after_tick1_allocations':sum(x['allocations'] for x in rows[2:])}
        symbolize_allocations(d,'Release')
    for case in ('C16','G8'):
        directories=[BASE/f'run-Release-{case}-{mode}-01' for mode in ('full','minimal')]
        decoded=[decoded_run(d) for d in directories];metas=[x[0] for x in decoded]
        assert metas[0]['offsets']==metas[1]['offsets'] and all(x['ticks']==1200 for x in metas)
        assert len({json.loads((d/'invocation.json').read_text())['exe_sha256'] for d in directories})==1
        raws=[(d/'data/samples.bin').read_bytes() for d in directories];o=metas[0]['offsets'];size=metas[0]['sample_bytes']
        mismatches=[]
        for tick in range(1201):
            for i in range(metas[0]['body_count']):
                base=tick*size+i*o['body_stride']
                # Handle index/generation/domain, body fields, SleepSettings and
                # inactivity/sleep state; exclude only known C++ alignment padding.
                for start,end in ((0,4),(8,236),(240,265)):
                    if raws[0][base+start:base+end]!=raws[1][base+start:base+end]:mismatches.append((tick,i,start));break
            for key in ('errors','impulses','work','energy','fidelity','geometry','witness','asleep'):
                if decoded[0][1][tick][key]!=decoded[1][1][tick][key]:mismatches.append((tick,key))
        costs=[json.loads((d/'audit-01.json').read_text())['costs']['inclusive'] for d in directories]
        added={key:costs[0][key]-costs[1][key]+2*quantum for key in ('mean_ns','median_ns','p95_ns','max_rolling60_ns')}
        passed=not mismatches and all(added[k]<=1e9/6000 for k in ('mean_ns','median_ns')) and all(added[k]<=1e9/3000 for k in ('p95_ns','max_rolling60_ns'))
        if not passed:failures.append(case+' observer parity/cost')
        parity[case]={'status':'PASS' if passed else 'FAIL','semantic_mismatches':mismatches[:20],
            'compared_completed_ticks':1200,'initial_compared':True,'full_minus_minimal_plus_2q_ns':added}
    result={'status':'PASS' if not failures else 'FAIL','failures':failures,'cases':reports,'observer_controls':parity,
        'performance_goal':'INCONCLUSIVE reference/external-load qualification; local observations satisfy safety envelope',
        'scope':'Numerical/stability/timing/parity evidence only; allocation attribution and retained-witness pair coverage need separate review; human pending',
        'evaluator_sha256':sha(Path(__file__))}
    write(BASE/'summary-01.json',result)
    print(json.dumps({'status':result['status'],'failures':failures,'observer_controls':parity,
        'witness_depths':{c:reports[c]['max_retained_witness_depth_m'] for c in reports}},indent=2))
    return int(bool(failures))

SANDBOX=ROOT/'logs/pre_editor/phase-26/sandbox-01'

def sandbox_authority(attempt='01'):
    decision=json.loads((SANDBOX/'authorization.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_PRESENTATION_AND_SCENE_LIFECYCLE_CAMPAIGN'
    assert decision['owner_directive']=='EXECUTE PRE_EDITOR PHASE 26 UNIFIED RBS PHYSICS SANDBOX AND INTERACTIVE SCENE CONTROL'
    for path in decision['source_allowlist'][:6]:
        assert sha(ROOT/path)==decision['before_hashes'][path],path
    assert sha(ROOT/decision['previous_ready']['path'])==decision['previous_ready']['sha256']
    suffix='' if attempt=='01' else '-'+attempt
    if suffix:
        correction=json.loads((SANDBOX/f'correction-{attempt}.json').read_text())
        assert correction['status']=='ADMITTED_BOUNDED_CORRECTION'
        assert correction['attempt']==attempt and correction['preserved_failure']
        if attempt=='05':
            assert correction['preserved_failure']=='logs/pre_editor/phase-26/sandbox-01/run-Debug-sandbox-04/result.json'
            failed=json.loads((ROOT/correction['preserved_failure']).read_text())
            assert failed['exit_code']!=0 and not failed['native_result']['pass']
            assert failed['native_result']['failure']=='ordinary wall contact not observed within four steps'
            plan=correction['technical_plan_adjustment']
            assert plan['original_planning_build_count']==5 and plan['max_actual_builds']==6
            assert plan['max_each_build_wall_seconds']==1200 and plan['max_aggregate_build_wall_seconds']==6000
            assert plan['native_process_cap_unchanged']==4 and plan['process_seconds_unchanged']==180
            assert plan['actual_builds_used']==3 and plan['remaining_planned_builds']==[
                'Debug sandbox05','Release sandbox05','Debug qualification05']
            decision['_sandbox_correction']={'path':f'logs/pre_editor/phase-26/sandbox-01/correction-{attempt}.json','plan':plan}
    frozen=json.loads((SANDBOX/f'frozen-inputs{suffix}.json').read_text())
    assert all(sha(ROOT/path)==digest for path,digest in frozen['owned_hashes'].items())
    return decision

def sandbox_source_inputs(decision):
    """Current normal RBS projects and literal includes; no historical execution adapter.

    The previous READY supplies proven dependency/toolchain seeds, never substituted
    translation units or authority. Current project, include and asset membership
    is recorded independently so additions are detected before a native launch.
    """
    import re, sys, xml.etree.ElementTree as ET
    ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'}
    projects=('GEngine/GEngine.vcxproj','external/glad/glad.vcxproj',
              'RigidBodySimulation/RigidBodySimulation.vcxproj')
    ready_path=ROOT/decision['previous_ready']['path']
    ready=json.loads(ready_path.read_text())
    closure_path=ROOT/ready['input_closure']['path']
    assert sha(closure_path)==ready['input_closure']['sha256']
    prior=json.loads(closure_path.read_text())
    def absolute(p):return Path(os.path.abspath(ROOT/Path(p)))
    def key(p):return p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else p.as_posix()
    paths={absolute(p) for p in (*prior['repository_inputs'],*prior['external_inputs'],
        *decision['source_allowlist'],*decision['supporting_allowlist'],*projects,
        'premake5.lua','tools/postbuild.py','RigidBodySimulation/postbuild.py',
        'docs/pre_editor/active/OUTPUT_PATHS.json','logs/pre_editor/phase-26/sandbox-01/authorization.json')}
    paths.update((ready_path,closure_path))
    if '_sandbox_correction' in decision:paths.add(absolute(decision['_sandbox_correction']['path']))
    paths.update(absolute(p) for p in ready['contract_hashes'])
    includes=[];library_dirs=[];native_libraries=set();seeds=[];compilation_sources={};project_membership={}
    for relative in projects:
        project=safe(ROOT/relative);tree=ET.parse(project)
        compiled=[absolute(project.parent/node.attrib['Include']) for node in tree.findall('.//m:ClCompile',ns) if 'Include' in node.attrib]
        assert len(compiled)==len(set(compiled)),relative
        compilation_sources[project.stem]=sorted(key(p) for p in compiled)
        members=[]
        for tag in ('ClCompile','ClInclude'):
            members.extend(absolute(project.parent/node.attrib['Include']) for node in tree.findall(f'.//m:{tag}',ns) if 'Include' in node.attrib)
        project_membership[relative]=sorted({key(p) for p in members});seeds.extend(members)
        for tag,dest in (('IncludePath',includes),('AdditionalIncludeDirectories',includes),('AdditionalLibraryDirectories',library_dirs)):
            for node in tree.findall(f'.//m:{tag}',ns):
                for item in (node.text or '').split(';'):
                    if item and '$' not in item and '%' not in item:
                        path=absolute(project.parent/item)
                        if path not in dest:dest.append(path)
        for node in tree.findall('.//m:AdditionalDependencies',ns):
            native_libraries.update(item for item in (node.text or '').split(';') if item.lower().endswith('.lib'))
    normal=compilation_sources['RigidBodySimulation']
    assert normal.count('RigidBodySimulation/src/main.cpp')==1 and normal.count('RigidBodySimulation/src/RbsFrame.cpp')==1
    assert not any('Phase26Entry' in p for p in normal),'diagnostic translation-unit substitution is forbidden'
    for p in prior['include_search_order']:
        path=absolute(p)
        if path not in includes:includes.append(path)
    seeds.extend(absolute(p) for p in decision['source_allowlist']+decision['supporting_allowlist'] if Path(p).suffix.lower() in ('.h','.hpp','.cpp','.c'))
    visited=set();unresolved=set();requests={}
    pattern=re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]',re.MULTILINE)
    while seeds:
        source=seeds.pop()
        if source in visited:continue
        assert source.is_file(),source
        visited.add(source)
        for name in pattern.findall(source.read_text(encoding='utf-8-sig',errors='replace')):
            found=next((absolute(directory/name) for directory in (source.parent,*includes) if (directory/name).is_file()),None)
            requests[key(source)+'|'+name]=key(found) if found else None
            if found is None:unresolved.add(name)
            else:seeds.append(found)
    paths.update(visited)
    for name in native_libraries:
        found=next((d/name for d in library_dirs if (d/name).is_file()),None)
        assert found is not None,('unresolved native library',name)
        paths.add(found)
    # External libraries/compiler-injected headers are already exact actual-use
    # seeds from READY, including intrin0.h and the x86/x64 compiler auxiliaries.
    paths.update((MSBUILD,Path(sys.executable)))
    runtime=[];assets=[];asset_membership={};presence={}
    for config in ('Debug','Release'):
        dlls=sorted(safe(ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'))
        assert dlls,('missing runtime DLLs',config)
        runtime.extend(dlls)
        asset_root=safe(ROOT/f'bin/{config}/assets')
        # These are RBS's existing runtime discovery families, including the
        # gallery presets now covered by this explicitly authorized campaign.
        for family in ('Audio','Fonts','Images','Models','Shaders'):
            directory=asset_root/family;presence[key(directory)]=directory.is_dir()
            assert directory.is_dir(),directory
            members=sorted(p for p in directory.rglob('*') if p.is_file())
            asset_membership[key(directory)]=[key(p) for p in members];assets.extend(members)
    paths.update(runtime+assets)
    optional={}
    for project in projects:
        user=safe(ROOT/(project+'.user'));optional[key(user)]=user.is_file()
        if user.is_file():paths.add(user)
    user=Path(os.environ.get('LOCALAPPDATA',''))/'Microsoft/MSBuild/v4.0/Microsoft.Cpp.x64.user.props'
    optional[key(user)]=user.is_file()
    if user.is_file():paths.add(user)
    assert all(p.is_file() for p in paths),'missing current source/dependency seed'
    return {'hashes':{key(p):sha(p) for p in sorted(paths)},'project_members':sorted(key(p) for p in visited),
        'compilation_sources':compilation_sources,'project_membership':project_membership,
        'runtime_dlls':[key(p) for p in runtime],'assets':[key(p) for p in assets],
        'asset_membership':asset_membership,'required_presence':presence,'optional_user_props':optional,
        'include_search':[key(p) for p in includes],'literal_include_resolution':dict(sorted(requests.items())),
        'unresolved_conditional_includes':sorted(unresolved),'dependency_seed_record':key(closure_path),
        'dependency_seed_sha256':sha(closure_path)}

def sandbox_library_reuse(config,mode,inputs,decision):
    """Return exact existing providers only when their actual-use closure matches."""
    if (config,mode) not in (('Release','sandbox'),('Debug','qualification')):return None
    ready=json.loads((ROOT/decision['previous_ready']['path']).read_text())
    ledger_path=ROOT/ready['evidence_ledger']['path']
    assert sha(ledger_path)==ready['evidence_ledger']['sha256']
    ledger=json.loads(ledger_path.read_text())['new_evidence']
    identity_path=f'logs/pre_editor/phase-26/g8-impulse-control-01/build-identity-{config}.json'
    assert sha(ROOT/identity_path)==ledger[identity_path]
    identity=json.loads((ROOT/identity_path).read_text())
    assert identity['status']=='PASS' and identity['qualification_macro']==(mode=='qualification')
    prior=json.loads((ROOT/ready['input_closure']['path']).read_text())
    provider={p:v['sha256'] for p,v in prior['repository_inputs'].items() if p.startswith(('GEngine/','external/'))}
    provider.update({p:v['sha256'] for p,v in prior['external_inputs'].items()})
    current={p.casefold():v for p,v in inputs['hashes'].items()}
    if any(current.get(p.casefold())!=digest for p,digest in provider.items()):return None
    for project,name in (('GEngine/GEngine.vcxproj','GEngine'),('external/glad/glad.vcxproj','glad')):
        old_members=sorted(Path(os.path.abspath((ROOT/project).parent/p)).relative_to(ROOT).as_posix()
            for p in prior['project_compile_membership'][project])
        if old_members!=inputs['compilation_sources'][name]:return None
    # The only new/removed application diagnostic define has no engine/glad
    # consumer. QUALIFICATION is ABI-affecting and must match the provider lane.
    for path in provider:
        if path.startswith(('GEngine/','external/glad/')) and Path(path).suffix.lower() in ('.h','.hpp','.cpp','.c'):
            source=(ROOT/path).read_text(errors='replace')
            if 'GENGINE_PHASE26_SANDBOX_CHECKS' in source or 'GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS' in source:return None
    records={p:h for p,h in identity['compiler_records'].items() if '/GEngine/' in p or '/glad/' in p}
    for path,digest in records.items():assert sha(ROOT/path)==digest,path
    for path in records:
        if path.endswith('/CL.command.1.tlog'):
            flags=(ROOT/path).read_text(encoding='utf-16').upper()
            assert ('GENGINE_PHASE26_QUALIFICATION' in flags)==(mode=='qualification')
            assert 'GE_ENABLE_PHYSICS_PROFILING' in flags
    libraries={p:h for p,h in identity['products'].items() if p.endswith('.lib')}
    if len(libraries)!=2 or any(not (ROOT/p).is_file() or sha(ROOT/p)!=h for p,h in libraries.items()):return None
    return {'status':'MATCHED_PROVIDER_REUSE','identity_record':identity_path,'identity_record_sha256':ledger[identity_path],
        'compiler_records':records,'provider_input_hashes':provider,'libraries':libraries,
        'qualification_macro':mode=='qualification','impact':'Identical engine/glad source, project membership, toolchain and consumed dependencies; sandbox/presentation defines have no provider consumer.'}

def sandbox_build(config,mode,attempt):
    decision=sandbox_authority(attempt)
    assert mode in ('sandbox','qualification') and (mode=='sandbox' or config=='Debug')
    d=safe(SANDBOX/f'build-{config}-{mode}-{attempt}');assert not d.exists()
    commands=sorted(SANDBOX.glob('build-*-*-*/command.json'))
    max_builds=3 if attempt=='01' else 5
    if attempt=='05':
        plan=decision['_sandbox_correction']['plan'];max_builds=plan['max_actual_builds']
        assert f'{config} {mode}{attempt}' in plan['remaining_planned_builds']
    assert len(commands)<max_builds
    prior_results=[]
    for command_record in commands:
        result_path=command_record.with_name('result.json')
        prior=json.loads(result_path.read_text());seconds=float(prior['seconds'])
        assert math.isfinite(seconds) and seconds>=0,result_path
        prior_results.append({'path':result_path.relative_to(ROOT).as_posix(),'sha256':sha(result_path),'seconds':seconds})
    used_seconds=sum(item['seconds'] for item in prior_results)
    if attempt=='05':
        baseline_results=[item for item in prior_results if not Path(item['path']).parent.name.endswith('-05')]
        assert len(baseline_results)==plan['actual_builds_used']
        assert abs(sum(item['seconds'] for item in baseline_results)-plan['actual_build_wall_seconds_used'])<.001
    # Reserve a complete existing per-build deadline before each launch. No
    # automatic retry and no reuse of earlier campaigns' elapsed-time budgets.
    assert used_seconds+1200<=6000,'sandbox aggregate build wall-time budget exhausted'
    d.mkdir()
    write(d/'budget-before.json',{'actual_builds_before':len(commands),'max_actual_builds':max_builds,
        'actual_build_wall_seconds_before':used_seconds,'max_each_build_wall_seconds':1200,
        'max_aggregate_build_wall_seconds':6000,'automatic_reruns':False,'prior_results':prior_results,
        'planning_revision':decision.get('_sandbox_correction',{}).get('path')})
    product='Phase26Sandbox' if mode=='sandbox' else 'Phase26SandboxQualification'
    define='GENGINE_PHASE26_SANDBOX_CHECKS' if mode=='sandbox' else 'GENGINE_PHASE26_QUALIFICATION'
    out=safe(ROOT/f'bin/{config}/{product}');inter=safe(ROOT/f'bin-int/{config}/{product}')
    temp=safe(ROOT/'runtime/tmp/phase26-sandbox');temp.mkdir(parents=True,exist_ok=True)
    sandbox_includes=(f'''<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'"><ClCompile><AdditionalIncludeDirectories>{(ROOT/'external/glad/include').as_posix()};{(ROOT/'external/sdl2/include').as_posix()};%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>'''
        if mode=='sandbox' else '')
    props=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{out.as_posix()}/$(MSBuildProjectName)/</OutDir><IntDir>{inter.as_posix()}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild></PropertyGroup>
<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>{define};GE_ENABLE_PHYSICS_PROFILING;%(PreprocessorDefinitions)</PreprocessorDefinitions><UndefinePreprocessorDefinitions>GENGINE_PHASE25_QUALIFICATION;GENGINE_PHASE25_ISOLATED_BUILD;GENGINE_PHASE25_ALLOCATIONS;GENGINE_RENDER_BASELINE;GENGINE_PHASE24_QUALIFICATION;GENGINE_PHASE24_OUTLINE_QUALIFICATION;GENGINE_PHASE24_REPLACEMENT_MEASUREMENT;GENGINE_PHASE24_COLLIDER_VALIDATION;GENGINE_PHASE26_IMPULSE_PRESENTATION_CHECKS;%(UndefinePreprocessorDefinitions)</UndefinePreprocessorDefinitions><ShowIncludes>true</ShowIncludes></ClCompile></ItemDefinitionGroup>
{sandbox_includes}
</Project>
'''
    write(d/'isolated.props',props)
    inputs=sandbox_source_inputs(decision)
    for path in decision['source_allowlist']+decision['supporting_allowlist']:
        if (ROOT/path).is_file():
            inputs['hashes'][path]=sha(ROOT/path)
            target=safe(d/'source'/path);target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as stream:stream.write((ROOT/path).read_bytes())
    write(d/'inputs.json',inputs)
    command=[str(MSBUILD),str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build',f'/p:Configuration={config}','/p:Platform=x64','/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}','/nr:false','/m:2','/nologo','/v:normal']
    reuse=sandbox_library_reuse(config,mode,inputs,decision)
    if reuse:
        for source,digest in reuse['libraries'].items():
            target=safe(out/Path(source).parent.name/Path(source).name);target.parent.mkdir(parents=True,exist_ok=True)
            if not target.exists():shutil.copyfile(ROOT/source,target)
            assert sha(target)==digest,target
        write(d/'provider-reuse.json',reuse)
        command.append('/p:BuildProjectReferences=false')
    write(d/'command.json',command)
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp))
    start=time.monotonic();print(f'SANDBOX BUILD {config} {mode} {attempt}',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=1200)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT)
            code=process.wait();write(d/'timeout.json',{'timeout_seconds':1200})
    unchanged=sandbox_source_inputs(decision)==inputs
    elapsed=time.monotonic()-start
    result={'exit_code':code,'seconds':elapsed,'inputs_unchanged':unchanged,'configuration':config,'mode':mode,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'aggregate_build_wall_seconds':used_seconds+elapsed,'aggregate_budget_within':used_seconds+elapsed<=6000}
    if code==0:result['products']={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.exe','.lib')}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    return 0 if code==0 and unchanged and result['aggregate_budget_within'] and not (d/'timeout.json').exists() else 1

def sandbox_run(config,mode,attempt):
    decision=sandbox_authority(attempt)
    assert mode in ('sandbox','qualification') and (mode=='sandbox' or config=='Debug')
    d=safe(SANDBOX/(f'run-{config}-sandbox-{attempt}' if mode=='sandbox' else f'run-Debug-C4-debug-{attempt}'))
    assert not d.exists() and len(list(SANDBOX.glob('run-*/invocation.json')))<(3 if attempt=='01' else 4)
    build_dir=SANDBOX/f'build-{config}-{mode}-{attempt}'
    result=json.loads((build_dir/'result.json').read_text());inputs=json.loads((build_dir/'inputs.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged'] and result['seconds']<=1200
    assert sandbox_source_inputs(decision)==inputs,'source/dependency/discovery membership drift'
    assert all(sha(ROOT/path)==digest for path,digest in result['products'].items())
    d.mkdir();data=d/'data';data.mkdir()
    product='Phase26Sandbox' if mode=='sandbox' else 'Phase26SandboxQualification'
    out=safe(ROOT/f'bin/{config}/{product}/RigidBodySimulation');exe=out/'RigidBodySimulation.exe'
    for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        target=out/dll.name
        if not target.exists():shutil.copyfile(dll,target)
        assert sha(target)==sha(dll)
    command=[str(exe)]+(['--phase26-sandbox-output',str(data)] if mode=='sandbox' else
        ['--scene','distance-chain','--links','4','--phase26-output',str(data),'--phase26-mode','debug'])
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');cwd.mkdir(parents=True,exist_ok=True)
    temp=safe(ROOT/'runtime/tmp/phase26-sandbox');temp.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),GENGINE_ASSET_ROOT=str(ROOT/f'bin/{config}/assets'))
    env.pop('GENGINE_BASELINE_OUTPUT',None)
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'deadline_seconds':180,
        'runtime_asset_root':env['GENGINE_ASSET_ROOT'],'purpose':'Bounded presentation/lifecycle validation; no reference performance qualification'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'SANDBOX RUN {config} {mode} {attempt}',flush=True)
    with (d/'process.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill();code=process.wait();write(d/'timeout.json',{'timeout_seconds':180})
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':native}
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    if mode=='qualification':return audit(config,'debug','C4',attempt,SANDBOX) if code==0 and native else 1
    cap=300 if config=='Debug' else 1200
    passed=bool(code==0 and native and native['pass'] and native['clean_shutdown'] and native['completed_ticks']<=cap and
        native['successful_transitions']>=20 and native['preset_mask']==127 and native['g8_sleep_wake'] and
        native['invalid_request_rejected'] and native['construction_rollback'] and native['paused_ticks']==0 and native['screenshots']>=1)
    write(d/'audit-01.json',{'status':'PASS' if passed else 'FAIL','presentation_only':True,'native':native,
        'performance_goal':'INCONCLUSIVE; local counters only, no qualified-reference performance claim'})
    return 0 if passed else 1

MATERIALS=SANDBOX/'material-restore-01'

def material_authority(attempt):
    decision=json.loads((MATERIALS/'authorization.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_BOUNDED_PRESENTATION_CORRECTION'
    assert sha(ROOT/decision['previous_ready']['path'])==decision['previous_ready']['sha256']
    frozen=json.loads((MATERIALS/f'frozen-inputs-{attempt}.json').read_text())
    assert all(sha(ROOT/p)==digest for p,digest in frozen['owned_hashes'].items())
    for p,digest in decision['before_hashes'].items():
        if p not in decision['changed_scope']:assert sha(ROOT/p)==digest,p
    for p,digest in decision['protected_hashes'].items():assert sha(ROOT/p)==digest,p
    if attempt!='01':
        correction=json.loads((MATERIALS/f'correction-{attempt}.json').read_text())
        assert correction['status']=='ADMITTED_BOUNDED_CORRECTION' and correction['preserved_failure']
    return decision

def material_inputs(decision,attempt):
    inputs=sandbox_source_inputs(decision)
    for p in [MATERIALS/'authorization.json',MATERIALS/'material-plan-01.json',
              MATERIALS/f'frozen-inputs-{attempt}.json',
              ROOT/'docs/pre_editor/active/PHASE_26_SANDBOX_TEXTURE_RESTORE_AUTHORIZATION_01.json']:
        inputs['hashes'][p.relative_to(ROOT).as_posix()]=sha(p)
    return inputs

def material_provider_reuse(config,inputs,decision):
    ready=json.loads((ROOT/decision['previous_ready']['path']).read_text())
    path=ROOT/ready['input_closure']['path'];assert sha(path)==ready['input_closure']['sha256']
    previous=json.loads(path.read_text())
    provider={p:v['sha256'] for p,v in previous['repository_inputs'].items() if p.startswith(('GEngine/','external/'))}
    provider.update({p:v['sha256'] for p,v in previous['external_inputs'].items()})
    current={p.casefold():v for p,v in inputs['hashes'].items()}
    assert all(current.get(p.casefold())==digest for p,digest in provider.items()),'provider input drift'
    for name in ('GEngine','glad'):
        assert inputs['compilation_sources'][name]==previous['project_compile_membership'][name]
    identity_path=SANDBOX/f'build-identity-{config}-sandbox-05.json'
    ledger=json.loads((ROOT/ready['evidence_ledger']['path']).read_text())['new_evidence']
    assert sha(identity_path)==ledger[identity_path.relative_to(ROOT).as_posix()]
    identity=json.loads(identity_path.read_text());assert not identity['qualification_macro']
    records={p:digest for p,digest in identity['compiler_records'].items() if '/GEngine/' in p or '/glad/' in p}
    for p,digest in records.items():
        assert sha(ROOT/p)==digest,p
        if p.endswith('/CL.command.1.tlog'):
            flags=(ROOT/p).read_text(encoding='utf-16').upper()
            assert 'GENGINE_PHASE26_QUALIFICATION' not in flags and 'GE_ENABLE_PHYSICS_PROFILING' in flags
    libraries={p:digest for p,digest in ready['new_tested_products'][f'{config}-sandbox'].items() if p.endswith('.lib')}
    assert len(libraries)==2 and all(sha(ROOT/p)==digest for p,digest in libraries.items())
    return {'status':'MATCHED_PROVIDER_REUSE','identity_record':identity_path.relative_to(ROOT).as_posix(),
        'identity_record_sha256':sha(identity_path),'compiler_records':records,'provider_input_hashes':provider,
        'libraries':libraries,'qualification_macro':False,'impact':'Exact unchanged engine/glad source, compilation membership, toolchain and dependencies; only RBS presentation and supporting capture code changed.'}

def material_build(config,attempt):
    decision=material_authority(attempt);plan=decision['validation_plan']
    d=safe(MATERIALS/f'build-{config}-{attempt}');assert not d.exists()
    commands=list(MATERIALS.glob('build-*/command.json'));assert len(commands)<plan['max_total_builds']
    used=sum(json.loads(p.with_name('result.json').read_text())['seconds'] for p in commands)
    assert used+plan['max_build_seconds']<=plan['max_build_wall_seconds']
    d.mkdir();inputs=material_inputs(decision,attempt);reuse=material_provider_reuse(config,inputs,decision)
    out=safe(ROOT/f'bin/{config}/Phase26SandboxMaterials');inter=safe(ROOT/f'bin-int/{config}/Phase26SandboxMaterials')
    temp=safe(ROOT/'runtime/tmp/phase26-materials');temp.mkdir(parents=True,exist_ok=True)
    # Same accepted sandbox diagnostic flags and normal translation units; only
    # isolated destinations change. Matching engine providers are never recompiled.
    props=(SANDBOX/f'build-{config}-sandbox-05/isolated.props').read_text()
    props=props.replace(f'bin/{config}/Phase26Sandbox/',f'bin/{config}/Phase26SandboxMaterials/').replace(
        f'bin-int/{config}/Phase26Sandbox/',f'bin-int/{config}/Phase26SandboxMaterials/')
    write(d/'isolated.props',props);write(d/'inputs.json',inputs);write(d/'provider-reuse.json',reuse)
    write(d/'budget-before.json',{'compiler_launches_before':len(commands),'seconds_before':used,'limits':plan,'automatic_reruns':False})
    for p in decision['source_allowlist']+decision['supporting_allowlist']:
        target=safe(d/'source'/p);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((ROOT/p).read_bytes())
    for p,digest in reuse['libraries'].items():
        target=safe(out/Path(p).parent.name/Path(p).name);target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():shutil.copyfile(ROOT/p,target)
        assert sha(target)==digest
    command=[str(MSBUILD),str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build',
        f'/p:Configuration={config}','/p:Platform=x64','/p:VCToolsVersion=14.44.35207',
        '/p:WindowsTargetPlatformVersion=10.0.26100.0',f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}',
        '/p:BuildProjectReferences=false','/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp))
    start=time.monotonic();print(f'MATERIAL BUILD {config} {attempt}',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=plan['max_build_seconds'])
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT)
            code=process.wait();write(d/'timeout.json',{'timeout_seconds':plan['max_build_seconds']})
    elapsed=time.monotonic()-start;unchanged=material_inputs(decision,attempt)==inputs
    result={'exit_code':code,'seconds':elapsed,'inputs_unchanged':unchanged,'configuration':config,
        'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'aggregate_build_seconds':used+elapsed}
    if code==0:result['products']={p.relative_to(ROOT).as_posix():sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.lib','.exe')}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    return 0 if code==0 and unchanged and elapsed<=plan['max_build_seconds'] and not (d/'timeout.json').exists() else 1

def material_run(config,attempt):
    decision=material_authority(attempt);plan=decision['validation_plan']
    d=safe(MATERIALS/f'run-{config}-{attempt}');assert not d.exists()
    assert len(list(MATERIALS.glob('run-*/invocation.json')))<plan['max_total_native']
    build_dir=MATERIALS/f'build-{config}-{attempt}';result=json.loads((build_dir/'result.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged']
    assert material_inputs(decision,attempt)==json.loads((build_dir/'inputs.json').read_text())
    assert all(sha(ROOT/p)==digest for p,digest in result['products'].items())
    d.mkdir();data=d/'data';data.mkdir();out=safe(ROOT/f'bin/{config}/Phase26SandboxMaterials/RigidBodySimulation')
    for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        target=out/dll.name
        if not target.exists():shutil.copyfile(dll,target)
        assert sha(target)==sha(dll)
    exe=out/'RigidBodySimulation.exe';command=[str(exe),'--phase26-sandbox-output',str(data),'--phase26-sandbox-materials']
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');temp=safe(ROOT/'runtime/tmp/phase26-materials')
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),GENGINE_ASSET_ROOT=str(ROOT/f'bin/{config}/assets'))
    env.pop('GENGINE_BASELINE_OUTPUT',None)
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),
        'deadline_seconds':plan['max_process_seconds'],'runtime_asset_root':env['GENGINE_ASSET_ROOT'],
        'purpose':'Focused material/camera capture and playback smoke; no Physics or reference-performance requalification'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'MATERIAL RUN {config} {attempt}',flush=True)
    with (d/'process.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=plan['max_process_seconds'])
        except subprocess.TimeoutExpired:
            process.kill();code=process.wait();write(d/'timeout.json',{'timeout_seconds':plan['max_process_seconds']})
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':native}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=bool(code==0 and native and native['pass'] and native['clean_shutdown'] and
        native['completed_ticks']<=plan['max_completed_ticks_per_process'] and native['frames']<=plan['max_UI_frames_per_process'] and
        result['seconds']<=plan['max_process_seconds'] and not (d/'timeout.json').exists())
    write(d/'audit-01.json',{'status':'PASS' if passed else 'FAIL','presentation_only':True,'native':native,
        'visual_acceptance':'Actual captures require separate inspection; handles alone do not establish visual quality.',
        'qualified_reference_performance':'INCONCLUSIVE; no new performance samples replace previous qualification'})
    return 0 if passed else 1

VISUAL=SANDBOX/'visual-refinement-01'

def visual_authority(mode,attempt):
    assert mode in ('compare','final')
    decision=json.loads((VISUAL/'authorization.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_BOUNDED_VISUAL_UI_REFINEMENT'
    assert sha(ROOT/decision['previous_ready']['path'])==decision['previous_ready']['sha256']
    frozen=json.loads((VISUAL/f'frozen-inputs-{mode}-{attempt}.json').read_text())
    assert all(sha(ROOT/p)==digest for p,digest in frozen['owned_hashes'].items())
    for p,digest in decision['before_hashes'].items():
        if p not in decision['changed_scope']:assert sha(ROOT/p)==digest,p
    for p,digest in decision['protected_hashes'].items():assert sha(ROOT/p)==digest,p
    if attempt!='01':
        correction=json.loads((VISUAL/f'correction-{mode}-{attempt}.json').read_text())
        assert correction['status']=='ADMITTED_BOUNDED_CORRECTION' and correction['preserved_failure']
    return decision

def visual_inputs(decision,mode,attempt):
    inputs=sandbox_source_inputs(decision)
    paths=[VISUAL/'authorization.json',VISUAL/'comparison-plan-01.json',
        VISUAL/f'frozen-inputs-{mode}-{attempt}.json',
        ROOT/'docs/pre_editor/active/PHASE_26_SANDBOX_VISUAL_REFINEMENT_AUTHORIZATION_01.json']
    if mode=='final':paths.append(VISUAL/'selection-01.json')
    for p in paths:inputs['hashes'][p.relative_to(ROOT).as_posix()]=sha(p)
    return inputs

def visual_provider_reuse(config,inputs,decision):
    ready=json.loads((ROOT/decision['previous_ready']['path']).read_text())
    path=ROOT/ready['input_closure']['path'];assert sha(path)==ready['input_closure']['sha256']
    previous=json.loads(path.read_text())
    provider={p:v['sha256'] for p,v in previous['repository_inputs'].items() if p.startswith(('GEngine/','external/'))}
    provider.update({p:v['sha256'] for p,v in previous['external_inputs'].items()})
    current={p.casefold():v for p,v in inputs['hashes'].items()}
    assert all(current.get(p.casefold())==digest for p,digest in provider.items()),'provider input drift'
    for name in ('GEngine','glad'):
        assert inputs['compilation_sources'][name]==previous['project_compile_membership'][name]
    identity_path=MATERIALS/f'build-identity-{config}-01.json'
    ledger=json.loads((ROOT/ready['evidence_ledger']['path']).read_text())['files']
    assert sha(identity_path)==ledger[identity_path.relative_to(ROOT).as_posix()]
    identity=json.loads(identity_path.read_text());assert not identity['qualification_macro']
    records={p:digest for p,digest in identity['compiler_records'].items() if '/GEngine/' in p or '/glad/' in p}
    for p,digest in records.items():
        assert sha(ROOT/p)==digest,p
        if p.endswith('/CL.command.1.tlog'):
            flags=(ROOT/p).read_text(encoding='utf-16').upper()
            assert 'GENGINE_PHASE26_QUALIFICATION' not in flags and 'GE_ENABLE_PHYSICS_PROFILING' in flags
    libraries={p:digest for p,digest in ready['new_tested_products'][config].items() if p.endswith('.lib')}
    assert len(libraries)==2 and all(sha(ROOT/p)==digest for p,digest in libraries.items())
    return {'status':'MATCHED_PROVIDER_REUSE','identity_record':identity_path.relative_to(ROOT).as_posix(),
        'identity_record_sha256':sha(identity_path),'compiler_records':records,'provider_input_hashes':provider,
        'libraries':libraries,'qualification_macro':False,'impact':'Exact unchanged engine/glad source, compilation membership, toolchain and dependencies; only RBS presentation and supporting capture code changed.'}

def visual_identity(config,mode,attempt):
    import re
    d=VISUAL/f'build-{config}-{mode}-{attempt}'
    result=json.loads((d/'result.json').read_text())
    inputs=json.loads((d/'inputs.json').read_text())
    reuse=json.loads((d/'provider-reuse.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged']
    inter=ROOT/f'bin-int/{config}/Phase26SandboxVisual-{mode}-{attempt}/RigidBodySimulation'
    commands=list(inter.rglob('CL.command.1.tlog'));assert len(commands)==1
    archived=d/'compiler-records/RigidBodySimulation';archived.mkdir(parents=True)
    records=dict(reuse['compiler_records'])
    for p in commands[0].parent.glob('*.tlog'):
        q=archived/p.name;shutil.copyfile(p,q);records[q.relative_to(ROOT).as_posix()]=sha(q)
    flags=(archived/'CL.command.1.tlog').read_text(encoding='utf-16')
    units={p.replace('\\','/').casefold() for line in flags.splitlines() if line.startswith('^')
        for p in line[1:].split('|')}
    expected={str((ROOT/p).resolve()).replace('\\','/').casefold()
        for p in inputs['compilation_sources']['RigidBodySimulation']}
    assert units==expected and len(units)==8
    upper=flags.upper()
    assert '/D GENGINE_PHASE26_SANDBOX_CHECKS' in upper and '/D GE_ENABLE_PHYSICS_PROFILING' in upper
    assert '/D GENGINE_PHASE26_QUALIFICATION' not in upper
    assert f'/D GENGINE_CONFIG_{config.upper()}' in upper
    assert '/MTD' in upper if config=='Debug' else '/MT ' in upper and '/MTD' not in upper
    known={str((ROOT/p).resolve()).replace('\\','/').casefold() for p in inputs['hashes']}
    known.update(str((ROOT/p).resolve()).replace('\\','/').casefold() for p in result['products'])
    dependencies=set()
    for p in archived.glob('*.read.*.tlog'):
        for line in p.read_text(encoding='utf-16').splitlines():
            for value in line.lstrip('^').split('|'):
                if len(value)>2 and value[1]==':':dependencies.add(value.replace('\\','/').casefold())
    unknown=[p for p in dependencies-known if not(any(x in p for x in ('/bin/','/bin-int/')) and
        Path(p).suffix in ('.obj','.pch','.pdb','.ilk','.exe','.lib','.res','.tlog'))]
    assert not unknown,unknown
    assert any(p.endswith('/rigidbodysimulation/tests/sandboxchecks.h') for p in dependencies)
    assert (archived/'link.command.1.tlog').is_file()
    log=(d/'build.log').read_text(errors='replace')
    warnings={code:len(re.findall(r'warning '+code+r'\b',log)) for code in set(re.findall(r'warning (C\d+)\b',log))}
    record={'status':'PASS','configuration':config,'mode':mode,'result_sha256':sha(d/'result.json'),
        'products':result['products'],'compiler_records':records,'compiled_RBS_translation_units':len(units),
        'actual_dependency_paths':len(dependencies),'actual_dependencies_covered':True,
        'provider_reuse':{'path':(d/'provider-reuse.json').relative_to(ROOT).as_posix(),'sha256':sha(d/'provider-reuse.json')},
        'warnings_by_code_occurrences':warnings,'sandbox_macro':True,'qualification_macro':False}
    write(VISUAL/f'build-identity-{config}-{mode}-{attempt}.json',record)
    return record

def visual_build(config,mode,attempt):
    decision=visual_authority(mode,attempt);plan=decision['validation_plan']
    d=safe(VISUAL/f'build-{config}-{mode}-{attempt}');assert not d.exists()
    commands=list(VISUAL.glob('build-*/command.json'));assert len(commands)<plan['max_total_builds']
    used=sum(json.loads(p.with_name('result.json').read_text())['seconds'] for p in commands)
    assert used+plan['max_build_seconds']<=plan['max_build_wall_seconds']
    d.mkdir();inputs=visual_inputs(decision,mode,attempt);reuse=visual_provider_reuse(config,inputs,decision)
    out=safe(ROOT/f'bin/{config}/Phase26SandboxVisual-{mode}-{attempt}');inter=safe(ROOT/f'bin-int/{config}/Phase26SandboxVisual-{mode}-{attempt}')
    temp=safe(ROOT/'runtime/tmp/phase26-visual');temp.mkdir(parents=True,exist_ok=True)
    # Same accepted sandbox diagnostic flags and normal translation units; only
    # isolated destinations change. Matching engine providers are never recompiled.
    props=(SANDBOX/f'build-{config}-sandbox-05/isolated.props').read_text()
    props=props.replace(f'bin/{config}/Phase26Sandbox/',f'bin/{config}/Phase26SandboxVisual-{mode}-{attempt}/').replace(
        f'bin-int/{config}/Phase26Sandbox/',f'bin-int/{config}/Phase26SandboxVisual-{mode}-{attempt}/')
    write(d/'isolated.props',props);write(d/'inputs.json',inputs);write(d/'provider-reuse.json',reuse)
    write(d/'budget-before.json',{'compiler_launches_before':len(commands),'seconds_before':used,'limits':plan,'automatic_reruns':False})
    for p in decision['source_allowlist']+decision['supporting_allowlist']:
        target=safe(d/'source'/p);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((ROOT/p).read_bytes())
    for p,digest in reuse['libraries'].items():
        target=safe(out/Path(p).parent.name/Path(p).name);target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():shutil.copyfile(ROOT/p,target)
        assert sha(target)==digest
    command=[str(MSBUILD),str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build',
        f'/p:Configuration={config}','/p:Platform=x64','/p:VCToolsVersion=14.44.35207',
        '/p:WindowsTargetPlatformVersion=10.0.26100.0',f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}',
        '/p:BuildProjectReferences=false','/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp))
    start=time.monotonic();print(f'VISUAL BUILD {config} {attempt}',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=plan['max_build_seconds'])
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT)
            code=process.wait();write(d/'timeout.json',{'timeout_seconds':plan['max_build_seconds']})
    elapsed=time.monotonic()-start;unchanged=visual_inputs(decision,mode,attempt)==inputs
    result={'exit_code':code,'seconds':elapsed,'inputs_unchanged':unchanged,'configuration':config,'mode':mode,
        'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'aggregate_build_seconds':used+elapsed}
    if code==0:result['products']={p.relative_to(ROOT).as_posix():sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.lib','.exe')}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=code==0 and unchanged and elapsed<=plan['max_build_seconds'] and not (d/'timeout.json').exists()
    if passed:visual_identity(config,mode,attempt)
    return 0 if passed else 1

def visual_run(config,mode,attempt):
    decision=visual_authority(mode,attempt);plan=decision['validation_plan']
    d=safe(VISUAL/f'run-{config}-{mode}-{attempt}');assert not d.exists()
    assert len(list(VISUAL.glob('run-*/invocation.json')))<plan['max_total_native']
    correction_path=VISUAL/f'correction-{mode}-{attempt}.json'
    correction=json.loads(correction_path.read_text()) if correction_path.exists() else {}
    build_attempt=correction.get('reuse_build_attempt',attempt)
    build_dir=VISUAL/f'build-{config}-{mode}-{build_attempt}';result=json.loads((build_dir/'result.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged']
    identity_path=VISUAL/f'build-identity-{config}-{mode}-{build_attempt}.json'
    identity=json.loads(identity_path.read_text())
    assert identity['status']=='PASS' and identity['result_sha256']==sha(build_dir/'result.json')
    current_inputs=visual_inputs(decision,mode,attempt)
    compiled_inputs=json.loads((build_dir/'inputs.json').read_text())
    if build_attempt!=attempt:
        assert correction['kind']=='RUNNER_STAGING_ONLY' and mode=='compare'
        assert sha(build_dir/'result.json')==correction['reused_build_result_sha256']
        assert sha(identity_path)==correction['reused_build_identity_sha256']
        old_key=(VISUAL/f'frozen-inputs-{mode}-{build_attempt}.json').relative_to(ROOT).as_posix()
        new_key=(VISUAL/f'frozen-inputs-{mode}-{attempt}.json').relative_to(ROOT).as_posix()
        before=json.loads(json.dumps(compiled_inputs));after=json.loads(json.dumps(current_inputs))
        assert before['hashes'].pop('tools/pre_editor_phase26_qualification.py')==correction['prior_runner_sha256']
        after['hashes'].pop('tools/pre_editor_phase26_qualification.py')
        before['hashes'].pop(old_key);after['hashes'].pop(new_key)
        assert before==after,'compiled behavior inputs changed during staging correction'
    else:assert current_inputs==compiled_inputs
    assert all(sha(ROOT/p)==digest for p,digest in result['products'].items())
    d.mkdir();data=d/'data';data.mkdir();out=safe(ROOT/f'bin/{config}/Phase26SandboxVisual-{mode}-{attempt}/RigidBodySimulation')
    out.mkdir(parents=True,exist_ok=True)
    original_exe=next(ROOT/p for p in result['products'] if p.endswith('/RigidBodySimulation.exe'))
    if not (out/'RigidBodySimulation.exe').exists():shutil.copyfile(original_exe,out/'RigidBodySimulation.exe')
    assert sha(out/'RigidBodySimulation.exe')==sha(original_exe)
    if build_attempt!=attempt:
        write(d/'compiled-input-reuse.json',{'status':'PASS','build_identity':identity,
            'current_inputs':current_inputs,'correction':correction,'compiled_inputs_unchanged':True,
            'staged_executable_sha256':sha(original_exe),'new_compilation':False})
    for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        target=out/dll.name
        if not target.exists():shutil.copyfile(dll,target)
        assert sha(target)==sha(dll)
    exe=out/'RigidBodySimulation.exe';command=[str(exe),'--phase26-sandbox-output',str(data),f'--phase26-sandbox-visual-{mode}']
    if mode=='final':
        selected=json.loads((VISUAL/'selection-01.json').read_text())
        command.extend(['--phase26-sandbox-visual-density',str(selected['floor_repeats'])])
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');temp=safe(ROOT/'runtime/tmp/phase26-visual')
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),GENGINE_ASSET_ROOT=str(ROOT/f'bin/{config}/assets'))
    env.pop('GENGINE_BASELINE_OUTPUT',None)
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),
        'deadline_seconds':plan['max_process_seconds'],'runtime_asset_root':env['GENGINE_ASSET_ROOT'],
        'purpose':'Bounded visual comparison or final UI/material/lifecycle checks; no Physics or reference-performance requalification'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'VISUAL RUN {config} {attempt}',flush=True)
    with (d/'process.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=plan['max_process_seconds'])
        except subprocess.TimeoutExpired:
            process.kill();code=process.wait();write(d/'timeout.json',{'timeout_seconds':plan['max_process_seconds']})
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    result={'exit_code':code,'seconds':time.monotonic()-start,'native_result':native}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=bool(code==0 and native and native['pass'] and native['clean_shutdown'] and
        native['completed_ticks']<=plan['max_completed_ticks_per_process'] and native['frames']<=plan['max_UI_frames_per_process'] and
        result['seconds']<=plan['max_process_seconds'] and not (d/'timeout.json').exists())
    write(d/'audit-01.json',{'status':'PASS' if passed else 'FAIL','presentation_only':True,'native':native,
        'visual_acceptance':'Actual captures require separate inspection; handles alone do not establish visual quality.',
        'qualified_reference_performance':'INCONCLUSIVE; no new performance samples replace previous qualification'})
    return 0 if passed else 1

JITTER=ROOT/'logs/pre_editor/phase-26/sphereboxstack-jitter-01'
JITTER_CASES=('sandbox-direct','sandbox-switch','qualification-direct','box-stack','sandbox-cadence')

def jitter_authority(attempt):
    assert len(attempt)==2 and attempt.isdecimal(),attempt
    decision=json.loads((JITTER/'authorization.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_DIAGNOSTIC_SPHEREBOXSTACK_JITTER'
    assert decision['limits']=={'builds':2,'build_wall_seconds':1200,'native_runs':6,
        'native_wall_seconds':300,'completed_ticks_per_run':2400}
    preserved=json.loads((JITTER/'preservation.json').read_text())
    ready_path=VISUAL/'ready/ready.json';ready_key=ready_path.relative_to(ROOT).as_posix()
    assert sha(ready_path)==preserved['source_and_records'][ready_key]['sha256']
    ready=json.loads(ready_path.read_text())
    plan=json.loads((JITTER/'plan.json').read_text())
    assert plan['status']=='FROZEN_DIAGNOSTIC_PLAN_BEFORE_NATIVE_RESULTS'
    assert all(plan['limits'][p]==v for p,v in decision['limits'].items())
    assert plan['limits']['ui_frames_per_run']==6000
    scope=set(plan['supporting_scope']+plan['production_instrumentation_scope'])
    assert set(decision['diagnostic_support_scope'])==set(plan['supporting_scope'])
    assert scope=={'RigidBodySimulation/tests/SandboxChecks.h','tools/pre_editor_phase26_qualification.py',
        'RigidBodySimulation/src/RbsLaunchConfig.cpp'}
    frozen=json.loads((JITTER/f'frozen-inputs-{attempt}.json').read_text())
    owned=ready['owned_production']+ready['owned_supporting']
    assert set(frozen['owned_hashes'])==set(owned)
    for p in owned:
        assert sha(ROOT/p)==frozen['owned_hashes'][p],p
        if p not in scope:assert sha(ROOT/p)==preserved['source_and_records'][p]['sha256'],p
    assert {case['name'] for case in plan['cases']}==set(JITTER_CASES) and len(plan['cases'])==5
    assert all(type(case['target_ticks']) is int and 0<case['target_ticks']<=2400 for case in plan['cases'])
    if attempt!='01':
        correction=json.loads((JITTER/f'correction-{attempt}.json').read_text())
        assert correction['status']=='ADMITTED_BOUNDED_CORRECTION'
        failed=correction['preserved_failure']
        assert sha(ROOT/failed['path'])==failed['sha256']
    decision['previous_ready']={'path':ready_key,'sha256':sha(ready_path)}
    decision['source_allowlist']=ready['owned_production']
    decision['supporting_allowlist']=ready['owned_supporting']
    decision['_plan']=plan
    return decision

def jitter_inputs(decision,attempt):
    inputs=sandbox_source_inputs(decision)
    paths=[JITTER/'authorization.json',JITTER/'preservation.json',JITTER/'plan.json',
        JITTER/'owner-clarification-01.json',JITTER/'plan-supplement-01.json',
        JITTER/f'frozen-inputs-{attempt}.json',
        ROOT/'docs/pre_editor/active/PHASE_26_SPHEREBOXSTACK_JITTER_AUTHORIZATION_01.json',
        VISUAL/'build-Debug-final-01/isolated.props',VISUAL/'build-Release-final-01/isolated.props']
    if attempt!='01':paths.append(JITTER/f'correction-{attempt}.json')
    for p in paths:inputs['hashes'][p.relative_to(ROOT).as_posix()]=sha(p)
    return inputs

def jitter_provider_reuse(config,inputs,decision):
    ready=json.loads((ROOT/decision['previous_ready']['path']).read_text())
    path=ROOT/ready['input_closure']['path'];assert sha(path)==ready['input_closure']['sha256']
    previous=json.loads(path.read_text())
    provider={p:v['sha256'] for p,v in previous['repository_inputs'].items() if p.startswith(('GEngine/','external/'))}
    provider.update({p:v['sha256'] for p,v in previous['external_inputs'].items()})
    current={p.casefold():v for p,v in inputs['hashes'].items()}
    assert all(current.get(p.casefold())==digest for p,digest in provider.items()),'provider input drift'
    for name in ('GEngine','glad'):
        assert inputs['compilation_sources'][name]==previous['project_compile_membership'][name]
    ledger_path=ROOT/ready['evidence_ledger']['path'];assert sha(ledger_path)==ready['evidence_ledger']['sha256']
    ledger=json.loads(ledger_path.read_text())['files']
    identity_path=VISUAL/f'build-identity-{config}-final-01.json'
    assert sha(identity_path)==ledger[identity_path.relative_to(ROOT).as_posix()]
    props=VISUAL/f'build-{config}-final-01/isolated.props'
    assert sha(props)==ledger[props.relative_to(ROOT).as_posix()]
    identity=json.loads(identity_path.read_text());assert identity['status']=='PASS' and not identity['qualification_macro']
    records={p:digest for p,digest in identity['compiler_records'].items() if '/GEngine/' in p or '/glad/' in p}
    assert records and all(sha(ROOT/p)==digest for p,digest in records.items())
    for p in records:
        if p.endswith('/CL.command.1.tlog'):
            flags=(ROOT/p).read_text(encoding='utf-16').upper()
            assert 'GENGINE_PHASE26_QUALIFICATION' not in flags and 'GE_ENABLE_PHYSICS_PROFILING' in flags
    products=ready['new_tested_products'][config]
    assert all(sha(ROOT/p)==digest for p,digest in products.items()),'preserved final product drift'
    libraries={p:digest for p,digest in products.items() if p.endswith('.lib')}
    assert len(libraries)==2
    return {'status':'MATCHED_PROVIDER_REUSE','identity_record':identity_path.relative_to(ROOT).as_posix(),
        'identity_record_sha256':sha(identity_path),'compiler_records':records,'provider_input_hashes':provider,
        'libraries':libraries,'qualification_macro':False,'preserved_final_products':products,
        'impact':'Only diagnostic RBS observer/launch handoff changes; exact engine/glad inputs, membership, toolchain, compiler records and archives reused.'}

def jitter_identity(config,attempt):
    d=JITTER/f'build-{config}-{attempt}';result=json.loads((d/'result.json').read_text())
    inputs=json.loads((d/'inputs.json').read_text());reuse=json.loads((d/'provider-reuse.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged']
    inter=ROOT/f'bin-int/{config}/Phase26SandboxJitter-{attempt}/RigidBodySimulation'
    commands=list(inter.rglob('CL.command.1.tlog'));assert len(commands)==1
    archived=d/'compiler-records/RigidBodySimulation';archived.mkdir(parents=True)
    records=dict(reuse['compiler_records'])
    for p in commands[0].parent.glob('*.tlog'):
        q=archived/p.name;shutil.copyfile(p,q);records[q.relative_to(ROOT).as_posix()]=sha(q)
    flags=(archived/'CL.command.1.tlog').read_text(encoding='utf-16')
    units={p.replace('\\','/').casefold() for line in flags.splitlines() if line.startswith('^') for p in line[1:].split('|')}
    expected={str((ROOT/p).resolve()).replace('\\','/').casefold() for p in inputs['compilation_sources']['RigidBodySimulation']}
    assert units==expected and len(units)==8
    upper=flags.upper()
    assert '/D GENGINE_PHASE26_SANDBOX_CHECKS' in upper and '/D GE_ENABLE_PHYSICS_PROFILING' in upper
    assert '/D GENGINE_PHASE26_QUALIFICATION' not in upper and f'/D GENGINE_CONFIG_{config.upper()}' in upper
    assert '/MTD' in upper if config=='Debug' else '/MT ' in upper and '/MTD' not in upper
    known={str((ROOT/p).resolve()).replace('\\','/').casefold() for p in inputs['hashes']}
    known.update(str((ROOT/p).resolve()).replace('\\','/').casefold() for p in result['products'])
    dependencies=set()
    for p in archived.glob('*.read.*.tlog'):
        for line in p.read_text(encoding='utf-16').splitlines():
            for value in line.lstrip('^').split('|'):
                if len(value)>2 and value[1]==':':dependencies.add(value.replace('\\','/').casefold())
    unknown=[p for p in dependencies-known if not(any(x in p for x in ('/bin/','/bin-int/')) and
        Path(p).suffix in ('.obj','.pch','.pdb','.ilk','.exe','.lib','.res','.tlog'))]
    assert not unknown,unknown
    assert any(p.endswith('/rigidbodysimulation/tests/sandboxchecks.h') for p in dependencies)
    assert (archived/'link.command.1.tlog').is_file()
    record={'status':'PASS','role':'DIAGNOSTIC_CAPTURE_BUILD_IDENTITY','configuration':config,
        'result_sha256':sha(d/'result.json'),'products':result['products'],'compiler_records':records,
        'compiled_RBS_translation_units':len(units),'actual_dependency_paths':len(dependencies),
        'actual_dependencies_covered':True,'sandbox_macro':True,'qualification_macro':False,
        'provider_reuse':{'path':(d/'provider-reuse.json').relative_to(ROOT).as_posix(),'sha256':sha(d/'provider-reuse.json')}}
    write(JITTER/f'build-identity-{config}-{attempt}.json',record)
    return record

def jitter_build(config,attempt):
    decision=jitter_authority(attempt);limits=decision['limits']
    d=safe(JITTER/f'build-{config}-{attempt}');assert not d.exists()
    commands=list(JITTER.glob('build-*/command.json'));assert len(commands)<limits['builds']
    inputs=jitter_inputs(decision,attempt);reuse=jitter_provider_reuse(config,inputs,decision)
    out=safe(ROOT/f'bin/{config}/Phase26SandboxJitter-{attempt}')
    inter=safe(ROOT/f'bin-int/{config}/Phase26SandboxJitter-{attempt}')
    assert not out.exists() and not inter.exists(),'isolated product destination already exists'
    d.mkdir();temp=safe(ROOT/'runtime/tmp/phase26-jitter');temp.mkdir(parents=True,exist_ok=True)
    props=(VISUAL/f'build-{config}-final-01/isolated.props').read_text()
    props=props.replace(f'bin/{config}/Phase26SandboxVisual-final-01/',f'bin/{config}/Phase26SandboxJitter-{attempt}/').replace(
        f'bin-int/{config}/Phase26SandboxVisual-final-01/',f'bin-int/{config}/Phase26SandboxJitter-{attempt}/')
    write(d/'isolated.props',props);write(d/'inputs.json',inputs);write(d/'provider-reuse.json',reuse)
    write(d/'budget-before.json',{'compiler_launches_before':len(commands),'limits':limits,'automatic_reruns':False})
    for p in decision['source_allowlist']+decision['supporting_allowlist']:
        target=safe(d/'source'/p);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((ROOT/p).read_bytes())
    for p,digest in reuse['libraries'].items():
        target=safe(out/Path(p).parent.name/Path(p).name);target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/p,target);assert sha(target)==digest
    command=[str(MSBUILD),str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build',
        f'/p:Configuration={config}','/p:Platform=x64','/p:VCToolsVersion=14.44.35207',
        '/p:WindowsTargetPlatformVersion=10.0.26100.0',f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}',
        '/p:BuildProjectReferences=false','/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp))
    start=time.monotonic();print(f'JITTER BUILD {config} {attempt}',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=limits['build_wall_seconds'])
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT)
            code=process.wait();write(d/'timeout.json',{'timeout_seconds':limits['build_wall_seconds']})
    elapsed=time.monotonic()-start;unchanged=jitter_inputs(decision,attempt)==inputs
    result={'exit_code':code,'seconds':elapsed,'inputs_unchanged':unchanged,'configuration':config,
        'diagnostic_only':True,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
    if code==0:result['products']={p.relative_to(ROOT).as_posix():sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.lib','.exe')}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=code==0 and unchanged and elapsed<=limits['build_wall_seconds'] and not (d/'timeout.json').exists()
    if passed:jitter_identity(config,attempt)
    return 0 if passed else 1

def jitter_run(config,case,attempt):
    decision=jitter_authority(attempt);limits=decision['limits'];assert case in JITTER_CASES
    target_ticks=next(value['target_ticks'] for value in decision['_plan']['cases'] if value['name']==case)
    d=safe(JITTER/f'run-{config}-{case}-{attempt}');assert not d.exists()
    invocations=list(JITTER.glob('run-*/invocation.json'));assert len(invocations)<limits['native_runs']
    build_dir=JITTER/f'build-{config}-{attempt}';build=json.loads((build_dir/'result.json').read_text())
    identity_path=JITTER/f'build-identity-{config}-{attempt}.json';identity=json.loads(identity_path.read_text())
    assert build['exit_code']==0 and build['inputs_unchanged'] and identity['status']=='PASS'
    assert identity['result_sha256']==sha(build_dir/'result.json')
    inputs=jitter_inputs(decision,attempt);assert inputs==json.loads((build_dir/'inputs.json').read_text())
    assert all(sha(ROOT/p)==digest for p,digest in build['products'].items())
    assert all(sha(ROOT/p)==digest for p,digest in identity['compiler_records'].items())
    out=safe(ROOT/f'bin/{config}/Phase26SandboxJitter-{attempt}/RigidBodySimulation')
    exe=out/'RigidBodySimulation.exe';assert build['products'][exe.relative_to(ROOT).as_posix()]==sha(exe)
    runtime={}
    for p in inputs['runtime_dlls']:
        if not p.startswith(f'bin/{config}/RigidBodySimulation/'):continue
        dll=ROOT/p;target=out/dll.name
        if not target.exists():shutil.copyfile(dll,target)
        assert sha(target)==inputs['hashes'][p]
        runtime[target.relative_to(ROOT).as_posix()]=sha(target)
    assert runtime
    assert {p.name for p in out.glob('*.dll')}=={Path(p).name for p in runtime},'unexpected isolated runtime DLL'
    d.mkdir();data=d/'data';data.mkdir()
    command=[str(exe),'--phase26-sandbox-output',str(data),'--phase26-sandbox-jitter',case,
        '--phase26-sandbox-jitter-ticks',str(target_ticks)]
    if case!='sandbox-switch':command.extend(['--scene','box-stack' if case=='box-stack' else 'sphere-box-stack'])
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');temp=safe(ROOT/'runtime/tmp/phase26-jitter')
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),GENGINE_ASSET_ROOT=str(ROOT/f'bin/{config}/assets'))
    env.pop('GENGINE_BASELINE_OUTPUT',None)
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'case':case,
        'build_identity':{'path':identity_path.relative_to(ROOT).as_posix(),'sha256':sha(identity_path)},
        'runtime_dlls':runtime,'deadline_seconds':limits['native_wall_seconds'],'requested_ticks':target_ticks,
        'runtime_asset_root':env['GENGINE_ASSET_ROOT'],'native_launches_before':len(invocations),
        'purpose':'Read-only physics/presentation jitter diagnosis; pass means capture validity only, never physics acceptance.'})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'JITTER RUN {config} {case} {attempt}',flush=True)
    with (d/'process.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=limits['native_wall_seconds'])
        except subprocess.TimeoutExpired:
            process.kill();code=process.wait();write(d/'timeout.json',{'timeout_seconds':limits['native_wall_seconds']})
    elapsed=time.monotonic()-start
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    unchanged=jitter_inputs(decision,attempt)==inputs and sha(exe)==build['products'][exe.relative_to(ROOT).as_posix()]
    unchanged=unchanged and all(sha(ROOT/p)==digest for p,digest in runtime.items())
    result={'exit_code':code,'seconds':elapsed,'native_result':native,'inputs_unchanged':unchanged,'diagnostic_only':True}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=bool(code==0 and unchanged and native and native['diagnostic_only'] and native['case']==case and
        native['pass'] and native['clean_shutdown'] and native['completed_ticks']==target_ticks and
        native['frames']<=decision['_plan']['limits']['ui_frames_per_run'] and
        elapsed<=limits['native_wall_seconds'] and not (d/'timeout.json').exists())
    write(d/'audit-01.json',{'status':'CAPTURE_VALID' if passed else 'CAPTURE_INVALID','native':native,
        'physics_acceptance':'NOT_ASSESSED; human acceptance remains blocked pending diagnosis and Owner decision.',
        'qualified_reference_performance':'NOT_MEASURED','automatic_reruns':False})
    return 0 if passed else 1

CONTACT=ROOT/'logs/pre_editor/phase-26/contact-correction-01'

def contact_authority(attempt):
    assert len(attempt)==2 and attempt.isdecimal()
    decision=json.loads((CONTACT/'authorization.json').read_text())
    assert decision['status']=='OWNER_AUTHORIZED_BOUNDED_CONTACT_RESPONSE_CORRECTION'
    assert decision['limits']=={'build_attempts':3,'build_seconds':1200,'native_processes':10,
        'native_seconds':300,'ticks_per_process':2400,'aggregate_ticks':18000}
    assert sha(ROOT/decision['accepted_review']['path'])==decision['accepted_review']['sha256']
    plan=json.loads((CONTACT/'plan.json').read_text());preserved=json.loads((CONTACT/'preservation.json').read_text())
    assert plan['status']=='FROZEN_CONTACT_CORRECTION_ACCEPTANCE_PLAN'
    assert set(plan['changed_support'])<=set(plan['supporting_allowlist'])
    assert all(not p.startswith('GEngine/') for p in plan['changed_support'])
    allowed=set(decision['production_scope']+plan['changed_support'])
    owned=plan['source_allowlist']+plan['supporting_allowlist']
    assert len(owned)==len(set(owned)) and set(decision['production_scope'])<=set(owned)
    frozen=json.loads((CONTACT/f'frozen-inputs-{attempt}.json').read_text())
    assert set(frozen['owned_hashes'])==set(owned)
    for p,digest in frozen['owned_hashes'].items():assert sha(ROOT/p)==digest,p
    for p,value in preserved['source_and_records'].items():
        if p.startswith(('GEngine/','RigidBodySimulation/','tools/')) and p not in allowed:
            assert sha(ROOT/p)==value['sha256'],p
    ready_path=VISUAL/'ready/ready.json';ready_key=ready_path.relative_to(ROOT).as_posix()
    addendum=json.loads((CONTACT/'preservation-addendum-01.json').read_text())
    assert addendum['role']=='ADDITIVE_PRESERVATION_BINDING_BEFORE_BUILD'
    assert addendum['initial_preservation_unchanged'] is True
    assert sha(ready_path)==addendum['source_and_records'][ready_key]['sha256']
    cases=plan['cases'];assert len({case['name'] for case in cases})==len(cases)
    for case in cases:
        assert (case['config'],case['build_mode']) in (('Debug','sandbox'),('Release','sandbox'),('Release','qualification'))
        assert type(case['tick_cap']) is int and 0<case['tick_cap']<=2400
        assert 0<case.get('expected_ticks',case['tick_cap'])<=case['tick_cap']
        assert 0<case.get('process_seconds',300)<=300
        assert case['result_schema'] in ('sandbox','qualification','contact-regression')
        assert isinstance(case['argv'],list) and '{output}' in case['argv']
        assert all(isinstance(v,str) for v in case['argv'])
    assert len(cases)<=10 and sum(case['tick_cap'] for case in cases)<=18000
    if attempt!='01':
        correction=json.loads((CONTACT/f'correction-{attempt}.json').read_text())
        assert correction['status']=='ADMITTED_BOUNDED_CORRECTION'
        failed=correction['preserved_failure'];assert sha(ROOT/failed['path'])==failed['sha256']
    decision['previous_ready']={'path':ready_key,'sha256':sha(ready_path)}
    decision['source_allowlist']=plan['source_allowlist'];decision['supporting_allowlist']=plan['supporting_allowlist']
    decision['_plan']=plan
    return decision

def contact_inputs(decision,attempt):
    inputs=sandbox_source_inputs(decision)
    paths=[CONTACT/'authorization.json',CONTACT/'preservation.json',CONTACT/'preservation-addendum-01.json',CONTACT/'plan.json',
        CONTACT/f'frozen-inputs-{attempt}.json',ROOT/decision['accepted_review']['path'],
        VISUAL/'build-Debug-final-01/isolated.props',VISUAL/'build-Release-final-01/isolated.props']
    paths.extend(ROOT/p for p in decision['_plan'].get('input_records',[]))
    if attempt!='01':paths.append(CONTACT/f'correction-{attempt}.json')
    for p in paths:inputs['hashes'][p.relative_to(ROOT).as_posix()]=sha(p)
    return inputs

def contact_glad_reuse(config,inputs,decision):
    ready=json.loads((ROOT/decision['previous_ready']['path']).read_text())
    closure=ROOT/ready['input_closure']['path'];assert sha(closure)==ready['input_closure']['sha256']
    prior=json.loads(closure.read_text());current={p.casefold():v for p,v in inputs['hashes'].items()}
    provider={p:v['sha256'] for p,v in prior['repository_inputs'].items() if p.startswith('external/glad/')}
    provider.update({p:v['sha256'] for p,v in prior['external_inputs'].items()})
    assert all(current.get(p.casefold())==digest for p,digest in provider.items())
    assert inputs['compilation_sources']['glad']==prior['project_compile_membership']['glad']
    # GLAD's generated C has no engine qualification/profiling feature branches.
    for p in inputs['project_membership']['external/glad/glad.vcxproj']:
        if Path(p).suffix.lower() in ('.c','.h'):
            source=(ROOT/p).read_text(errors='replace')
            assert 'GENGINE_PHASE26' not in source and 'GE_ENABLE_PHYSICS_PROFILING' not in source
    ledger_path=ROOT/ready['evidence_ledger']['path'];assert sha(ledger_path)==ready['evidence_ledger']['sha256']
    ledger=json.loads(ledger_path.read_text())['files'];path=VISUAL/f'build-identity-{config}-final-01.json'
    assert sha(path)==ledger[path.relative_to(ROOT).as_posix()]
    template=VISUAL/f'build-{config}-final-01/isolated.props';assert sha(template)==ledger[template.relative_to(ROOT).as_posix()]
    record=json.loads(path.read_text());assert record['status']=='PASS'
    records={p:digest for p,digest in record['compiler_records'].items() if '/glad/' in p}
    assert records and all(sha(ROOT/p)==digest for p,digest in records.items())
    libraries={p:digest for p,digest in ready['new_tested_products'][config].items() if p.endswith('/glad/glad.lib')}
    assert len(libraries)==1 and all(sha(ROOT/p)==digest for p,digest in libraries.items())
    return {'status':'MATCHED_GLAD_REUSE_ONLY','configuration':config,'provider_input_hashes':provider,
        'compiler_records':records,'libraries':libraries,'engine_reused':False,
        'identity_record':{'path':path.relative_to(ROOT).as_posix(),'sha256':sha(path)},
        'impact':'GLAD C sources/membership/toolchain unchanged; added engine qualification macro is absent from GLAD source. GEngine is rebuilt for each configuration/macro identity.'}

def contact_identity(config,mode,attempt):
    d=CONTACT/f'build-{config}-{mode}-{attempt}';result=json.loads((d/'result.json').read_text())
    inputs=json.loads((d/'inputs.json').read_text());reuse=json.loads((d/'glad-reuse.json').read_text())
    assert result['exit_code']==0 and result['inputs_unchanged']
    records=dict(reuse['compiler_records']);counts={};dependencies=set()
    for project in ('GEngine','RigidBodySimulation'):
        inter=ROOT/f'bin-int/{config}/Phase26ContactCorrection-{mode}-{attempt}/{project}'
        commands=list(inter.rglob('CL.command.1.tlog'));assert len(commands)==1,(project,commands)
        archived=d/'compiler-records'/project;archived.mkdir(parents=True)
        for p in commands[0].parent.glob('*.tlog'):
            q=archived/p.name;shutil.copyfile(p,q);records[q.relative_to(ROOT).as_posix()]=sha(q)
        flags=(archived/'CL.command.1.tlog').read_text(encoding='utf-16')
        units={p.replace('\\','/').casefold() for line in flags.splitlines() if line.startswith('^') for p in line[1:].split('|')}
        expected={str((ROOT/p).resolve()).replace('\\','/').casefold() for p in inputs['compilation_sources'][project]}
        assert units==expected,(project,units^expected);counts[project]=len(units)
        upper=flags.upper();assert '/D GE_ENABLE_PHYSICS_PROFILING' in upper
        assert ('/D GENGINE_PHASE26_QUALIFICATION' in upper)==(mode=='qualification')
        assert ('/D GENGINE_PHASE26_SANDBOX_CHECKS' in upper)==(mode=='sandbox')
        assert f'/D GENGINE_CONFIG_{config.upper()}' in upper
        assert '/MTD' in upper if config=='Debug' else '/MT ' in upper and '/MTD' not in upper
        command_units=[]
        for line in flags.splitlines():
            if line.startswith('^'):command_units=[p.replace('\\','/').casefold() for p in line[1:].split('|')]
            elif line.strip():
                implementation='/D GENGINE_CONTACT_CORRECTION_ALLOC_IMPLEMENTATION' in line.upper()
                for unit in command_units:
                    assert implementation==(mode=='sandbox' and project=='RigidBodySimulation' and unit.endswith('/src/main.cpp')),(unit,implementation)
        for p in archived.glob('*.read.*.tlog'):
            for line in p.read_text(encoding='utf-16').splitlines():
                for value in line.lstrip('^').split('|'):
                    if len(value)>2 and value[1]==':':dependencies.add(value.replace('\\','/').casefold())
    assert counts['RigidBodySimulation']==8
    assert any('/physics/manifold.cpp' in p for p in dependencies)
    assert any(p.endswith('/rigidbodysimulation/tests/contactcorrectionmemory.h') for p in dependencies)==(mode=='sandbox')
    assert (d/'compiler-records/RigidBodySimulation/link.command.1.tlog').is_file()
    link_reads=set()
    for p in (d/'compiler-records/RigidBodySimulation').glob('link.read.*.tlog'):
        for line in p.read_text(encoding='utf-16').splitlines():
            link_reads.update(value.replace('\\','/').casefold() for value in line.lstrip('^').split('|') if len(value)>2 and value[1]==':')
    for project,library in (('GEngine','GEngine.lib'),('glad','glad.lib')):
        expected=str((ROOT/f'bin/{config}/Phase26ContactCorrection-{mode}-{attempt}/{project}/{library}').resolve()).replace('\\','/').casefold()
        actual={p for p in link_reads if p.endswith('/'+library.casefold())}
        assert actual=={expected},('actual linked provider mismatch',library,actual,expected)
    known={str((ROOT/p).resolve()).replace('\\','/').casefold() for p in inputs['hashes']}
    known.update(str((ROOT/p).resolve()).replace('\\','/').casefold() for p in result['products'])
    unknown=[p for p in dependencies-known if not(any(x in p for x in ('/bin/','/bin-int/')) and
        Path(p).suffix in ('.obj','.pch','.pdb','.ilk','.exe','.lib','.res','.tlog'))]
    assert not unknown,unknown
    record={'status':'PASS','configuration':config,'mode':mode,'result_sha256':sha(d/'result.json'),
        'products':result['products'],'compiler_records':records,'compiled_translation_units':counts,
        'actual_dependency_paths':len(dependencies),'actual_dependencies_covered':True,
        'qualification_macro':mode=='qualification','sandbox_macro':mode=='sandbox','engine_rebuilt':True,
        'generated_build_inputs':{p.relative_to(ROOT).as_posix():sha(p) for p in (d/'isolated.props',d/'projects.proj')},
        'glad_reuse':{'path':(d/'glad-reuse.json').relative_to(ROOT).as_posix(),'sha256':sha(d/'glad-reuse.json')}}
    write(CONTACT/f'build-identity-{config}-{mode}-{attempt}.json',record)
    return record

def contact_build(config,mode,attempt):
    assert (config,mode) in (('Debug','sandbox'),('Release','sandbox'),('Release','qualification'))
    decision=contact_authority(attempt);limits=decision['limits']
    d=safe(CONTACT/f'build-{config}-{mode}-{attempt}');assert not d.exists()
    commands=list(CONTACT.glob('build-*/command.json'));assert len(commands)<limits['build_attempts']
    inputs=contact_inputs(decision,attempt);reuse=contact_glad_reuse(config,inputs,decision)
    out=safe(ROOT/f'bin/{config}/Phase26ContactCorrection-{mode}-{attempt}')
    inter=safe(ROOT/f'bin-int/{config}/Phase26ContactCorrection-{mode}-{attempt}')
    assert not out.exists() and not inter.exists()
    d.mkdir();temp=safe(ROOT/'runtime/tmp/phase26-contact-correction');temp.mkdir(parents=True,exist_ok=True)
    props=(VISUAL/f'build-{config}-final-01/isolated.props').read_text()
    props=props.replace(f'bin/{config}/Phase26SandboxVisual-final-01/',f'bin/{config}/Phase26ContactCorrection-{mode}-{attempt}/').replace(
        f'bin-int/{config}/Phase26SandboxVisual-final-01/',f'bin-int/{config}/Phase26ContactCorrection-{mode}-{attempt}/')
    if mode=='qualification':props=props.replace('GENGINE_PHASE26_SANDBOX_CHECKS','GENGINE_PHASE26_QUALIFICATION')
    else:
        props=props.replace('</Project>','''<ItemGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'">
<ClCompile Update="src\\main.cpp"><PreprocessorDefinitions>GENGINE_CONTACT_CORRECTION_ALLOC_IMPLEMENTATION;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile>
</ItemGroup></Project>''')
    from xml.sax.saxutils import escape
    # One bounded build attempt: normal GEngine then normal RBS, sharing one
    # exact GLAD provider; no old translation-unit substitutions or extra app.
    projects=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003" DefaultTargets="Build">
<ItemGroup><CurrentProject Include="{escape(str(ROOT/'GEngine/GEngine.vcxproj'))}" />
<CurrentProject Include="{escape(str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'))}" /></ItemGroup>
<Target Name="Build"><MSBuild Projects="@(CurrentProject)" Targets="Build" BuildInParallel="false" StopOnFirstFailure="true" /></Target>
</Project>'''
    write(d/'isolated.props',props);write(d/'projects.proj',projects)
    write(d/'inputs.json',inputs);write(d/'glad-reuse.json',reuse)
    write(d/'budget-before.json',{'build_attempts_before':len(commands),'limits':limits,'automatic_reruns':False})
    for p in decision['source_allowlist']+decision['supporting_allowlist']:
        target=safe(d/'source'/p);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((ROOT/p).read_bytes())
    for p,digest in reuse['libraries'].items():
        target=safe(out/'glad'/Path(p).name);target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/p,target);assert sha(target)==digest
    command=[str(MSBUILD),str(d/'projects.proj'),'/t:Build',f'/p:Configuration={config}','/p:Platform=x64',
        '/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',
        f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}','/p:BuildProjectReferences=false',
        '/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp))
    start=time.monotonic();print(f'CONTACT BUILD {config} {mode} {attempt}',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=limits['build_seconds'])
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT)
            code=process.wait();write(d/'timeout.json',{'timeout_seconds':limits['build_seconds']})
    elapsed=time.monotonic()-start;unchanged=contact_inputs(decision,attempt)==inputs
    result={'exit_code':code,'seconds':elapsed,'inputs_unchanged':unchanged,'configuration':config,'mode':mode,
        'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
    if code==0:result['products']={p.relative_to(ROOT).as_posix():sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.lib','.exe')}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    passed=code==0 and unchanged and elapsed<=limits['build_seconds'] and not (d/'timeout.json').exists()
    if passed:contact_identity(config,mode,attempt)
    return 0 if passed else 1

def contact_native_budget():
    launches=list(CONTACT.glob('run-*/invocation.json'));known=reserved=0;entries={}
    for path in launches:
        invocation=json.loads(path.read_text());cap=invocation['reserved_tick_cap'];result_path=path.with_name('result.json')
        actual=None
        if result_path.exists():
            result=json.loads(result_path.read_text())
            if result.get('complete_count_verified'):actual=result['charged_completed_ticks']
        if actual is None:reserved+=cap
        else:known+=actual
        entries[path.parent.name]={'verified_completed_ticks':actual,'charged_ticks':cap if actual is None else actual}
    return {'native_processes':len(launches),'verified_completed_ticks':known,
        'unverified_reserved_ticks':reserved,'charged_ticks':known+reserved,'processes':entries}

def contact_run(config,case_name,attempt):
    decision=contact_authority(attempt);limits=decision['limits']
    case=next(case for case in decision['_plan']['cases'] if case['name']==case_name and case['config']==config)
    mode=case['build_mode'];cap=case['tick_cap'];budget=contact_native_budget()
    assert budget['native_processes']<limits['native_processes']
    assert budget['charged_ticks']+cap<=limits['aggregate_ticks']
    d=safe(CONTACT/f'run-{config}-{case_name}-{attempt}');assert not d.exists()
    build_dir=CONTACT/f'build-{config}-{mode}-{attempt}';build=json.loads((build_dir/'result.json').read_text())
    identity_path=CONTACT/f'build-identity-{config}-{mode}-{attempt}.json';identity=json.loads(identity_path.read_text())
    assert build['exit_code']==0 and build['inputs_unchanged'] and identity['status']=='PASS'
    assert identity['result_sha256']==sha(build_dir/'result.json')
    inputs=contact_inputs(decision,attempt);assert inputs==json.loads((build_dir/'inputs.json').read_text())
    for mapping in (build['products'],identity['compiler_records'],identity['generated_build_inputs']):
        assert all(sha(ROOT/p)==digest for p,digest in mapping.items())
    out=safe(ROOT/f'bin/{config}/Phase26ContactCorrection-{mode}-{attempt}/RigidBodySimulation')
    exe=out/'RigidBodySimulation.exe';assert build['products'][exe.relative_to(ROOT).as_posix()]==sha(exe)
    dlls={}
    for p in inputs['runtime_dlls']:
        if not p.startswith(f'bin/{config}/RigidBodySimulation/'):continue
        dll=ROOT/p;target=out/dll.name
        if not target.exists():shutil.copyfile(dll,target)
        assert sha(target)==inputs['hashes'][p];dlls[target.relative_to(ROOT).as_posix()]=sha(target)
    assert dlls and {p.name for p in out.glob('*.dll')}=={Path(p).name for p in dlls}
    d.mkdir();data=d/'data';data.mkdir();request=d/'d26-entry-request.txt'
    if case['result_schema']=='contact-regression':
        write(request,'D26-ENTRY-02 entry26 stack60 60 1 1200 wake60 60 1 600 1800 30 180 8388608 67108864 67108864 END\n')
        assert cap==1800 and case.get('process_seconds',300)<=180
    replacements={'{output}':str(data),'{request}':str(request)}
    command=[str(exe)]+[replacements.get(value,value) for value in case['argv']]
    cwd=safe(ROOT/f'runtime/RigidBodySimulation/{config}');temp=safe(ROOT/'runtime/tmp/phase26-contact-correction')
    env=os.environ.copy();env.update(TMP=str(temp),TEMP=str(temp),GENGINE_ASSET_ROOT=str(ROOT/f'bin/{config}/assets'))
    for name in ('GENGINE_BASELINE_OUTPUT','GENGINE_D26_ENTRY_REQUEST','GENGINE_P21_REQUEST','GENGINE_P20_CONDITIONAL_FOCUSED_REQUEST'):env.pop(name,None)
    overrides={key:replacements.get(value,value) for key,value in case.get('environment',{}).items()}
    env.update(overrides);deadline=case.get('process_seconds',limits['native_seconds'])
    write(d/'budget-before.json',{'prior':budget,'reserved_next_ticks':cap,'limits':limits,'automatic_reruns':False})
    write(d/'invocation.json',{'command':command,'cwd':str(cwd),'exe_sha256':sha(exe),'case':case_name,
        'build_identity':{'path':identity_path.relative_to(ROOT).as_posix(),'sha256':sha(identity_path)},
        'runtime_dlls':dlls,'environment_overrides':overrides,'runtime_asset_root':env['GENGINE_ASSET_ROOT'],
        'deadline_seconds':deadline,'reserved_tick_cap':cap,'native_launches_before':budget['native_processes'],
        'aggregate_charged_before':budget['charged_ticks'],'purpose':case.get('purpose','Affected bounded contact correction validation')})
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('before-'+name))
    start=time.monotonic();print(f'CONTACT RUN {config} {case_name} {attempt}',flush=True)
    with (d/'process.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        try:code=process.wait(timeout=deadline)
        except subprocess.TimeoutExpired:
            process.kill();code=process.wait();write(d/'timeout.json',{'timeout_seconds':deadline})
    elapsed=time.monotonic()-start;native=json.loads((data/'result.json').read_text()) if (data/'result.json').exists() else None
    for name in ('GEngine.log','imgui.ini'):
        if (cwd/name).exists():shutil.copyfile(cwd/name,d/('after-'+name))
    unchanged=contact_inputs(decision,attempt)==inputs and all(sha(ROOT/p)==digest for p,digest in dlls.items())
    unchanged=unchanged and sha(exe)==build['products'][exe.relative_to(ROOT).as_posix()]
    schema=case['result_schema'];tick_key='completed_ticks' if schema=='sandbox' else 'ticks'
    complete_key='pass' if schema=='sandbox' else 'complete';clean_key='shutdown' if schema=='contact-regression' else 'clean_shutdown'
    ticks=native.get(tick_key) if native else None
    complete=bool(native and native.get(complete_key) and native.get(clean_key) and type(ticks) is int and 0<=ticks<=cap)
    adapter=None
    if schema=='contact-regression':
        adapter_path=data/'adapter-result.json'
        adapter=json.loads(adapter_path.read_text()) if adapter_path.exists() else None
        complete=bool(complete and adapter and adapter.get('pass') and adapter.get('clean_shutdown') and adapter.get('completed_ticks')==1800)
    if 'expected_ticks' in case:complete=complete and ticks==case['expected_ticks']
    if 'ui_frame_cap' in case:complete=bool(complete and native and native.get('frames',case['ui_frame_cap']+1)<=case['ui_frame_cap'])
    passed=bool(code==0 and unchanged and complete and elapsed<=deadline and not (d/'timeout.json').exists())
    result={'exit_code':code,'seconds':elapsed,'native_result':native,'adapter_result':adapter,'inputs_unchanged':unchanged,
        'complete_count_verified':passed,'charged_completed_ticks':ticks if passed else cap,
        'status':'NATIVE_COMPLETE_PENDING_APPLICABLE_AUDIT' if passed else 'FAIL',
        'automatic_reruns':False,'qualified_reference_performance':'INCONCLUSIVE until applicable same-binary gates are audited'}
    write(d/'result.json',result);write(d/'budget-after.json',contact_native_budget());print(json.dumps(result),flush=True)
    return 0 if passed else 1

def contact_coff_symbols(exe, addresses, image_base, config, mode, attempt):
    """Read-only symbol recovery: match complete COFF code, masking relocations.

    Release has no PDB. No relink, disassembly heuristic, or guessed address map.
    Every accepted section must match all non-relocation instruction bytes.
    """
    import ctypes as c
    data=exe.read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
    section_count=struct.unpack_from('<H',data,pe+6)[0]
    section_at=pe+24+struct.unpack_from('<H',data,pe+20)[0]
    regions=[]
    for i in range(section_count):
        at=section_at+40*i;virtual_size,rva,size,offset=struct.unpack_from('<4I',data,at+8)
        flags=struct.unpack_from('<I',data,at+36)[0]
        if flags&0x20:regions.append((rva,data[offset:offset+size]))
    wanted={x-image_base for x in addresses};found={};evidence=[]
    dbg=c.WinDLL('dbghelp');dbg.UnDecorateSymbolName.argtypes=[c.c_char_p,c.c_char_p,c.c_ulong,c.c_ulong]
    def undecorate(name):
        buf=c.create_string_buffer(4096)
        return buf.value.decode(errors='replace') if dbg.UnDecorateSymbolName(name.encode(),buf,len(buf),0) else name
    names={'PhysicsSystem','Broadphase','ConstraintPenetration','Manifold','Intersect','Intersections','GJK','EPA','main','RbsFrame','_Scene','BaseApp'}
    objects=[p for p in (ROOT/f'bin-int/{config}/Phase26ContactCorrection-{mode}-{attempt}').rglob('*.obj') if p.stem in names]
    for obj in objects:
        raw=obj.read_bytes();big=raw[:4]==b'\0\0\xff\xff'
        if big:count,table,symbols=struct.unpack_from('<III',raw,44);header=56;stride=20
        else:count=struct.unpack_from('<H',raw,2)[0];table,symbols=struct.unpack_from('<II',raw,8);header=20+struct.unpack_from('<H',raw,16)[0];stride=18
        strings=table+symbols*stride;by_section={};i=0
        while i<symbols:
            at=table+i*stride;namebytes=raw[at:at+8]
            if namebytes[:4]==b'\0'*4:
                start=strings+struct.unpack_from('<I',namebytes,4)[0];end=raw.find(b'\0',start);name=raw[start:end].decode(errors='replace')
            else:name=namebytes.split(b'\0')[0].decode(errors='replace')
            value=struct.unpack_from('<I',raw,at+8)[0]
            section=struct.unpack_from('<i' if big else '<h',raw,at+12)[0]
            typ=struct.unpack_from('<H',raw,at+(16 if big else 14))[0];aux=raw[at+stride-1]
            if typ&0x20 and section>0:by_section.setdefault(section,[]).append((value,name))
            i+=1+aux
        matched=[]
        for section,syms in by_section.items():
            at=header+(section-1)*40;size,offset,relocation=struct.unpack_from('<III',raw,at+16)
            reloc_count=struct.unpack_from('<H',raw,at+32)[0];flags=struct.unpack_from('<I',raw,at+36)[0]
            if not flags&0x20 or size<16:continue
            code=raw[offset:offset+size];masked=set();relocs=[]
            for j in range(reloc_count):
                pos,_,typ=struct.unpack_from('<IIH',raw,relocation+10*j)
                width=8 if typ==1 else 2 if typ==10 else 4
                masked.update(range(pos,pos+width));relocs.append((pos,width))
            runs=[];start=0
            for pos in sorted(masked):
                if pos>start:runs.append((start,pos))
                start=pos+1
            if start<size:runs.append((start,size))
            if not runs:continue
            a,z=max(runs,key=lambda x:x[1]-x[0]);anchor=code[a:min(z,a+32)]
            if len(anchor)<12:continue
            for rva,region in regions:
                search=0
                while True:
                    pos=region.find(anchor,search)
                    if pos<0:break
                    search=pos+1;base=pos-a;lo=rva+base;hi=lo+size
                    targets=[x for x in wanted if lo<=x<hi]
                    if base<0 or base+size>len(region) or not targets:continue
                    if not all(code[x:y]==region[base+x:base+y] for x,y in runs):continue
                    matched.append({'section':section,'rva':lo,'bytes':size,'symbols':[undecorate(n) for _,n in syms],
                        'code_sha256':hashlib.sha256(code).hexdigest(),'relocation_masks':relocs})
                    ordered=sorted(syms)
                    for target in targets:
                        eligible=[(v,n) for v,n in ordered if v<=target-lo]
                        if eligible:
                            v,n=eligible[-1];found.setdefault(target+image_base,[]).append({'name':undecorate(n),
                                'object':str(obj.relative_to(ROOT)).replace('\\','/'),'offset':target-lo-v})
        if matched:evidence.append({'object':str(obj.relative_to(ROOT)).replace('\\','/'),'sha256':sha(obj),'matches':matched})
    return found,evidence


def contact_symbolize_allocations(directory,config,mode,attempt):
    """Resolve recorded allocation stacks against the actual local tested image/PDB."""
    import ctypes as c
    trace=json.loads((directory/'data/allocations.json').read_text())
    exe=ROOT/f'bin/{config}/Phase26ContactCorrection-{mode}-{attempt}/RigidBodySimulation/RigidBodySimulation.exe'
    invocation=json.loads((directory/'invocation.json').read_text())
    assert sha(exe)==invocation['exe_sha256']
    class Symbol(c.Structure):
        _fields_=[('size',c.c_ulong),('type',c.c_ulong),('reserved',c.c_ulonglong*2),
            ('index',c.c_ulong),('length',c.c_ulong),('module',c.c_ulonglong),('flags',c.c_ulong),
            ('value',c.c_ulonglong),('address',c.c_ulonglong),('register',c.c_ulong),('scope',c.c_ulong),
            ('tag',c.c_ulong),('nameLength',c.c_ulong),('maxName',c.c_ulong),('name',c.c_char*1)]
    class Line(c.Structure):
        _fields_=[('size',c.c_ulong),('key',c.c_void_p),('number',c.c_ulong),('file',c.c_char_p),('address',c.c_ulonglong)]
    kernel=c.WinDLL('kernel32',use_last_error=True);kernel.GetCurrentProcess.restype=c.c_void_p
    process=kernel.GetCurrentProcess();dbg=c.WinDLL('dbghelp',use_last_error=True)
    dbg.SymInitializeW.argtypes=[c.c_void_p,c.c_wchar_p,c.c_int]
    dbg.SymCleanup.argtypes=[c.c_void_p]
    assert dbg.SymInitializeW(process,str(exe.parent),False),c.get_last_error()
    try:
        dbg.SymSetOptions(0x12)
        dbg.SymLoadModuleExW.argtypes=[c.c_void_p,c.c_void_p,c.c_wchar_p,c.c_wchar_p,c.c_ulonglong,c.c_ulong,c.c_void_p,c.c_ulong]
        dbg.SymLoadModuleExW.restype=c.c_ulonglong
        assert dbg.SymLoadModuleExW(process,None,str(exe),None,trace['image_base'],0,None,0),c.get_last_error()
        dbg.SymFromAddr.argtypes=[c.c_void_p,c.c_ulonglong,c.POINTER(c.c_ulonglong),c.POINTER(Symbol)]
        dbg.SymGetLineFromAddr64.argtypes=[c.c_void_p,c.c_ulonglong,c.POINTER(c.c_ulong),c.POINTER(Line)]
        cache={}
        def resolve(address):
            if address in cache:return cache[address]
            buffer=c.create_string_buffer(c.sizeof(Symbol)+2048);p=c.cast(buffer,c.POINTER(Symbol))
            p.contents.size=c.sizeof(Symbol);p.contents.maxName=2048;displacement=c.c_ulonglong()
            value={'address':address}
            if dbg.SymFromAddr(process,address,c.byref(displacement),p):
                value['name']=c.string_at(c.addressof(buffer)+Symbol.name.offset,p.contents.nameLength).decode(errors='replace')
            else:value['unresolved_error']=c.get_last_error()
            line=Line();line.size=c.sizeof(Line);offset=c.c_ulong()
            if dbg.SymGetLineFromAddr64(process,address,c.byref(offset),c.byref(line)):
                value.update(file=line.file.decode(errors='replace'),line=line.number)
            cache[address]=value;return value
        events=[]
        for event in trace['events']:
            entry={k:v for k,v in event.items() if k!='stack'}
            entry['stack']=[resolve(x) for x in event['stack']];events.append(entry)
        meta,rows=decoded_run(directory)
        assert sum(e.get('occurrences',1) for e in events)==sum(x['allocations'] for x in rows[1:])
        assert sum(e.get('occurrences',1)*e['bytes'] for e in events)==sum(x['allocation_bytes'] for x in rows[1:])
        pdb=exe.with_suffix('.pdb');coff=[]
        if not pdb.exists():
            recovered,coff=contact_coff_symbols(exe,{x for e in trace['events'] for x in e['stack']},trace['image_base'],config,mode,attempt)
            for event in events:
                for frame in event['stack']:
                    if frame['address'] in recovered:frame['verified_coff_symbols']=recovered[frame['address']]
        result={'status':'COMPLETE_RECORDED_COUNTS_REQUIRE_ATTRIBUTION','events':events,
            'exe_sha256':sha(exe),'pdb_sha256':sha(pdb) if pdb.exists() else None,'coff_evidence':coff,
            'trace_sha256':sha(directory/'data/allocations.json')}
        write(directory/'allocation-symbols-02.json',result)
        return result
    finally:dbg.SymCleanup(process)


def contact_summary(attempt):
    contact_authority(attempt)
    quantum=json.loads((CONTACT/'reference-hardware.json').read_text())['clock_quantum_ns']
    assert 0<quantum<=1000
    directories=[CONTACT/f'run-Release-G8-{mode}-{attempt}' for mode in ('full','minimal')]
    decoded=[decoded_run(d) for d in directories];metas=[x[0] for x in decoded]
    assert metas[0]['offsets']==metas[1]['offsets'] and all(x['ticks']==1200 for x in metas)
    assert len({json.loads((d/'invocation.json').read_text())['exe_sha256'] for d in directories})==1
    audits=[json.loads((d/'audit-01.json').read_text()) for d in directories]
    assert all(a['status']=='PASS' for a in audits)
    raws=[(d/'data/samples.bin').read_bytes() for d in directories];o=metas[0]['offsets'];size=metas[0]['sample_bytes']
    mismatches=[]
    for tick in range(1201):
        for i in range(metas[0]['body_count']):
            base=tick*size+i*o['body_stride']
            for start,end in ((0,4),(8,236),(240,265)):
                if raws[0][base+start:base+end]!=raws[1][base+start:base+end]:mismatches.append((tick,i,start));break
        for key in ('errors','impulses','work','energy','fidelity','geometry','witness','asleep'):
            if decoded[0][1][tick][key]!=decoded[1][1][tick][key]:mismatches.append((tick,key))
    costs=[a['costs']['inclusive'] for a in audits]
    added={key:costs[0][key]-costs[1][key]+2*quantum for key in ('mean_ns','median_ns','p95_ns','max_rolling60_ns')}
    passed=not mismatches and all(added[k]<=1e9/6000 for k in ('mean_ns','median_ns')) and all(added[k]<=1e9/3000 for k in ('p95_ns','max_rolling60_ns'))
    for directory in directories:contact_symbolize_allocations(directory,'Release','qualification',attempt)
    result={'status':'PASS' if passed else 'FAIL','scope':'G8 same-binary full/minimal physical parity and unchanged observer cost limits',
        'semantic_mismatches':mismatches[:20],'compared_completed_ticks':1200,'initial_compared':True,
        'full_minus_minimal_plus_2q_ns':added,'inclusive_costs':costs,'clock_quantum_ns':quantum,
        'audits':{d.relative_to(ROOT).as_posix():sha(d/'audit-01.json') for d in directories},
        'allocation_attribution':'New executable/PDB stack symbolization retained per run; full Update allocation attribution still requires review.',
        'performance_goal':'INCONCLUSIVE reference/external-load qualification; no generic hardware/FPS claim',
        'native_budget':contact_native_budget(),'evaluator_sha256':sha(Path(__file__))}
    write(CONTACT/f'qualification-summary-{attempt}.json',result);print(json.dumps(result),flush=True)
    return int(not passed)

def contact_audit(config,mode,case,attempt):
    """Evaluate fixed OD08 gates from authoritative states; no fitting or new runs."""
    contact_authority(attempt);assert config=='Release' and case=='G8' and mode in ('full','minimal');d=CONTACT/f'run-{config}-{case}-{mode}-{attempt}';meta,rows=decoded_run(d)
    failures=[]
    def check(ok,gate,tick=None):
        if not ok and not any(x['gate']==gate for x in failures):failures.append({'gate':gate,'first_tick':tick})
    check(meta['complete'] and meta['clean_shutdown'],'native complete/clean shutdown')
    check(meta['ticks']==meta['target'],'all samples present')
    check(meta['recorder_bytes']<=8388608,'recorder cap')
    def norm(v):return math.sqrt(sum(x*x for x in v))
    def sub(a,b):return tuple(x-y for x,y in zip(a,b))
    def angle(a,b):
        cosine=abs(sum(x*y for x,y in zip(a,b)))/(norm(a)*norm(b))
        return 2*math.acos(min(1.,cosine))
    n=meta['links'];contact=meta['contact'];ordinary=case=='ordinary';work=meta['work']
    expected_count=n+1+int(contact)
    max_energy_excess=-math.inf;max_error=0.;max_displacement=0.;max_hf=0.
    recovery=[None]*n;quiet_count=0;signed=[]
    if not ordinary:
        check(meta['body_count']==expected_count,'deterministic body membership')
        for i,b in enumerate(rows[0]['bodies']):
            expected=(0.,0.,0.) if i==n+1 else (2.25*i,1.5,0.) if contact else (0.,1.5+2.25*(n-i),0.)
            check(b['position']==expected and b['velocity']==(0.,0.,0.) and b['angular']==(0.,0.,0.),'deterministic initial states')
        for r in rows[1:]:
            tick=r['tick'];bodies=r['bodies'];applied=work if tick>300 else 0.;allowance=.00125 if tick<=300 else .0128125
            check(all(math.isfinite(x) for b in bodies for k in ('position','velocity','angular','orientation') for x in b[k]),'finite states',tick)
            quiet=all(b['sleeping'][0] or norm(b['velocity'])<=.05 and norm(b['angular'])<=.02 for b in bodies[1:n+1])
            quiet_count=quiet_count+1 if quiet else 0
            rest=tick<=300 or contact and tick>=420 or quiet_count>=30
            threshold=.01125 if rest else .0225
            errors=[norm(sub(bodies[i]['position'],bodies[i-1]['position']))-2.25 for i in range(1,n+1)]
            signed.append(errors);max_error=max(max_error,max(map(abs,errors)))
            check(max(map(abs,errors))<=threshold,'joint per-row error',tick)
            check(sum(map(abs,errors))<=threshold*n,'aggregate joint drift',tick)
            kinetic=sum(.5*sum(x*x for x in b['velocity'])+.2*sum(x*x for x in b['angular']) for b in bodies[1:n+1])
            energy=kinetic+12*sum(b['position'][1] for b in bodies[1:n+1])
            excess=energy-rows[0]['energy']-applied;max_energy_excess=max(max_energy_excess,excess)
            check(abs(energy-r['energy'])<=1e-9,'raw energy observer agreement',tick)
            check(excess<=allowance,'raw energy peak',tick)
            if contact:fidelity=kinetic+12*sum(max(b['position'][1]-1.5,0.) for b in bodies[1:n+1])
            else:
                fidelity=kinetic+27*sum((n-i+1)*(1+sub(bodies[i]['position'],bodies[i-1]['position'])[1]/norm(sub(bodies[i]['position'],bodies[i-1]['position']))) for i in range(1,n+1))
            check(abs(fidelity-r['fidelity'])<=1e-9,'fidelity observer agreement',tick)
            if contact or tick>120:check(fidelity-applied<=allowance,'fidelity peak',tick)
            if 120<=tick<=300 or contact and tick>=420:check(r['asleep']==n,'sleep deadline/uncommanded wakes',tick)
            if tick==301:check(r['asleep']==0,'commanded connected wake',tick)
            for i in range(1,n+1):
                position=bodies[i]['position'];displacement=norm(sub(position,rows[0]['bodies'][i]['position']))
                max_displacement=max(max_displacement,displacement)
                if tick<=300:check(displacement<=(.02 if contact else .01125*i),'initial displacement',tick)
                reference=120 if tick<=300 else 420
                if 121<=tick<=300 or contact and tick>=421:
                    check(norm(sub(position,rows[reference]['bodies'][i]['position']))<=.002,'settled reference drift',tick)
                if contact:
                    check(bodies[-1]['position']==(0.,0.,0.) and bodies[-1]['orientation']==rows[0]['bodies'][-1]['orientation'],'unchanged ground pose',tick)
                    q=(abs(position[0])-50,abs(position[1])-.5,abs(position[2])-50)
                    depth=max(0.,1.-(norm(tuple(max(x,0.) for x in q))+min(max(q),0.)))
                    settled=121<=tick<=300 or tick>=421 or quiet_count>=30
                    check(depth<=(.02 if settled else .04),'actual geometric penetration',tick)
                    episode=recovery[i-1]
                    if depth>.02:
                        if episode is None:episode=[tick,None]
                        episode[1]=None
                    elif episode is not None:
                        if episode[1] is None:episode[1]=tick
                        if tick>=episode[1]+2:episode=None
                    if episode is not None and tick>episode[0]+4:
                        check(episode[1] is not None and episode[1]<=episode[0]+4,'geometric recovery deadline',tick)
                    recovery[i-1]=episode
                    if tick>300:
                        delta=sub(position,rows[300]['bodies'][i]['position'])
                        check(math.hypot(delta[0],delta[2])<=.25,'G8 loaded domain displacement',tick)
            for i in range(n+1):
                for j in range(i+2,n+1):check(norm(sub(bodies[i]['position'],bodies[j]['position']))>2,'nonadjacent clearance',tick)
            check(r['joint_allocations']==0,'joint hot allocation',tick)
            check(r['non_ground_contacts']==0,'recorded contact domain',tick)
            if 'witnesses' in r:
                for depth,a,b in r['witnesses']:
                    check(math.isfinite(depth) and 0<=a<expected_count and 0<=b<expected_count and a!=b,'witness pair validity',tick)
                    check(contact and (a==n+1 or b==n+1),'witness ground-pair identity',tick)
                check(max([0.]+[x[0] for x in r['witnesses']])==r['witness'],'per-pair witness aggregate agreement',tick)
        check(all(x is None for x in recovery),'no pending penetration recovery')
        for t in range(301,min(meta['ticks'],1200)):
            residual=max(abs(signed[t-1][j]-.5*(signed[t-2][j]+signed[t][j])) for j in range(n))
            max_hf=max(max_hf,residual);check(residual<=.00225,'loaded joint high-frequency residual',t)
        # Union of every complete 60-tick quiet window; peak-to-peak only between
        # pairs that coexist in such a window. Stationary intervals shortcut exactly.
        for lo,hi in [(121,min(300,meta['ticks']))]+([(421,meta['ticks'])] if contact else []):
            if hi-lo+1<60:continue
            for i in range(1,n+1):
                for t in range(lo,hi+1):
                    p=rows[t]['bodies'][i]['position'];q=rows[t]['bodies'][i]['orientation']
                    for u in range(max(lo,t-59),t):
                        old=rows[u]['bodies'][i]
                        check(norm(sub(p,old['position']))<=.002,'quiet position peak-to-peak',t)
                        if q!=old['orientation']:check(angle(q,old['orientation'])<=.002,'quiet angular peak-to-peak',t)
                    if lo<t<hi:
                        before=rows[t-1]['bodies'][i];after=rows[t+1]['bodies'][i]
                        check(norm(tuple(p[k]-.5*(before['position'][k]+after['position'][k]) for k in range(3)))<=.001,'quiet positional high frequency',t)
                        a=before['orientation'];b=after['orientation'];sgn=1 if sum(x*y for x,y in zip(a,b))>=0 else -1
                        midpoint=tuple(x+sgn*y for x,y in zip(a,b))
                        check(angle(q,midpoint)<=.001,'quiet angular high frequency',t)
        if meta['ticks']==1200:
            for key in ('energy','fidelity'):
                bins=[statistics.mean(r[key] for r in rows[301+j*60:361+j*60]) for j in range(15)]
                slope=sum((j-7)*value for j,value in enumerate(bins))/280
                check(slope<=.00125/14,key+' loaded growth')
    def stats(values):
        ordered=sorted(values);count=len(values)
        return {'mean_ns':statistics.mean(values),'median_ns':statistics.median(values),
            'p95_ns':ordered[math.ceil(.95*count)-1],'p99_ns':ordered[math.ceil(.99*count)-1],'max_ns':max(values),
            'max_rolling60_ns':max((sum(values[i:i+60])/60 for i in range(count-59)),default=0)}
    costs={k:stats([r[k] for r in rows[1:]]) for k in ('inclusive','update','joint','contact_time','observation')} if meta['ticks'] else {}
    if config=='Release' and meta['ticks']==1200:
        quantum=json.loads((CONTACT/'reference-hardware.json').read_text())['clock_quantum_ns']
        check(0<quantum<=1000,'clock admission')
        for key in ('mean_ns','median_ns'):check(costs['inclusive'][key]+quantum<=1e9/120,'inclusive CPU safety '+key)
        for key in ('p95_ns','max_rolling60_ns'):check(costs['inclusive'][key]+quantum<=1e9/80,'inclusive CPU safety '+key)
    result={'status':'PASS' if not failures else 'FAIL','scope':f'{config} {case} {mode}; only measured windows; no human acceptance',
        'failures':failures,'ticks':meta['ticks'],'maximum_raw_energy_excess_J':None if ordinary else max_energy_excess,
        'maximum_joint_error_m':max_error,'maximum_joint_HF_m':max_hf,'costs':costs,
        'allocations':sum(r['allocations'] for r in rows[1:]),'joint_hot_allocations':sum(r['joint_allocations'] for r in rows[1:]),
        'allocation_disposition':'All Update allocations require attribution; zero hot-row count alone is not full incremental graph/allocation PASS',
        'input_hashes':{str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in [d/'data/result.json',d/'data/samples.bin',Path(__file__)]}}
    write(d/'audit-01.json',result);print(json.dumps(result,indent=2));return int(bool(failures))


# The canonical Jolt observation profile is deliberately separate from every
# historical runner mode and destination above. No old campaign is resumed.
OBS=ROOT/'logs/pre_editor/phase-26/qualification-observation-01/implementation-01'
OBS_RUNTIME=ROOT/'runtime/tmp/phase26-jolt-observation-01'
OBS_SDK=OBS_RUNTIME/'jolt'
OBS_ACTIVE=ROOT/'docs/pre_editor/active/PHASE_26_JOLT_INTEGRATION_01'
OBS_MACRO='JPH_GENGINE_PHASE26_OBSERVATION'
OBS_OWNED=('GEngine/src/Physics/JoltBackend.cpp','RigidBodySimulation/tests/DistanceJointChecks.h',
    'RigidBodySimulation/tests/JoltResistanceChecks.h','tools/pre_editor_phase26_qualification.py')
OBS_SDK_MEMBERS=('Jolt/Physics/QualificationObservation.h','Jolt/Physics/PhysicsSystem.cpp',
    'Jolt/Physics/Constraints/ContactConstraintManager.cpp','Jolt/Physics/Constraints/ConstraintManager.cpp',
    'Jolt/Physics/IslandBuilder.cpp')

def observation_authority():
    accepted=json.loads((OBS_ACTIVE/'PHASE_26_JOLT_OBSERVABILITY_SCOPE_ACCEPTANCE_01.json').read_text())
    assert accepted['status']=='OWNER_ACCEPTED_EXACT_DIAGNOSTIC_SCOPE_AND_CONDITIONAL_QUALIFICATION'
    for key in ('proposal','input_manifest','canonical_source_manifest','owner_human_acceptance','accepted_CPU_residual'):
        item=accepted[key];assert sha(ROOT/item['path'])==item['sha256'],item['path']
    amendment=json.loads((OBS_ACTIVE/'PHASE_26_JOLT_ARENA_POLICY_AMENDMENT_01.json').read_text())
    assert amendment['status']=='OWNER_ACCEPTED_ACTIVE_PROSPECTIVE'
    assert amendment['phase']=='26' and amendment['backend']=='Jolt'
    assert amendment['heap']['incremental_joint_induced_calls_per_steady_tick']==0
    assert amendment['heap']['incremental_joint_induced_bytes_per_steady_tick']==0
    assert amendment['arena']['capacity_bytes']==33554432
    bounds=amendment['source_derived_bounds'];assert sha(ROOT/bounds['path'])==bounds['sha256']
    env={**os.environ,'GIT_OPTIONAL_LOCKS':'0','GIT_NO_LAZY_FETCH':'1'}
    command=['git','-c','safe.directory='+ROOT.as_posix(),'-C',str(ROOT)]
    assert subprocess.check_output(command+['branch','--show-current'],env=env,text=True).strip()=='pre-editor/refactor'
    assert subprocess.check_output(command+['rev-parse','HEAD'],env=env,text=True).strip()=='ff5fe799dc7acb0db55eb53c5c4d9877eab62b32'
    assert subprocess.check_output(command+['remote','get-url','origin'],env=env,text=True).strip()=='https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git'
    before=json.loads((OBS/'reproduction.json').read_text())['indexes_before']
    for label,root in (('canonical',ROOT),('sdk',ROOT/'external/jolt')):
        args=['git','-c','safe.directory='+root.as_posix(),'-C',str(root)]
        index=Path(subprocess.check_output(args+['rev-parse','--path-format=absolute','--git-path','index'],env=env,text=True).strip())
        assert sha(index)==before[label]['raw'],('Index drift',label)
        assert hashlib.sha256(subprocess.check_output(args+['ls-files','--stage','-z'],env=env)).hexdigest()==before[label]['logical_stage_z'],label
    return accepted,amendment

def observation_environment(config=None):
    env={k:v for k,v in os.environ.items() if not k.startswith(('GENGINE_','PRE_EDITOR_'))}
    for key in ('CL','_CL_','LINK','_LINK_','MSBuildProjectExtensionsPath','ForceImportBeforeCppTargets'):
        env.pop(key,None)
    temporary=safe(OBS_RUNTIME/'temp');temporary.mkdir(parents=True,exist_ok=True)
    env.update(TMP=str(temporary),TEMP=str(temporary),PYTHONDONTWRITEBYTECODE='1',GIT_OPTIONAL_LOCKS='0',GIT_NO_LAZY_FETCH='1')
    if config:env['GENGINE_ASSET_ROOT']=str(safe(ROOT/f'bin/{config}/assets'))
    return env

def observation_inputs():
    """Reuse the admitted canonical RBS dependency closure, plus this exact delta."""
    import re
    import xml.etree.ElementTree as ET
    accepted,amendment=observation_authority()
    admission=json.loads((ROOT/'logs/pre_editor/phase-26/canonical-integration-01/admission.json').read_text())
    paths={Path(p) for p in admission['inputs']}
    # Ordinary built products are preservation targets, not source inputs.
    paths={p for p in paths if not (any(part in ('bin','bin-int') for part in p.parts) and p.suffix.lower() in ('.exe','.lib','.obj','.pch','.pdb','.ilk','.log'))}
    paths.update(ROOT/p for p in OBS_OWNED)
    paths.update(p for p in OBS_SDK.rglob('*') if p.is_file())
    paths.update(OBS_ACTIVE/p for p in ('PHASE_26_JOLT_OBSERVABILITY_SCOPE_ACCEPTANCE_01.json','PHASE_26_JOLT_ARENA_POLICY_AMENDMENT_01.json'))
    paths.add(ROOT/amendment['source_derived_bounds']['path'])
    paths.add(OBS/'observer-cardinality-02.json')
    paths.update(ROOT/f'external/jolt-patches/phase26-qualification-observation-01.{ext}' for ext in ('patch','json'))
    # Follow newly introduced literal includes as well as the admitted closure.
    vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207')
    sdk=Path('C:/Program Files (x86)/Windows Kits/10')
    roots=[OBS_SDK,ROOT/'GEngine/include',ROOT/'GEngine/include/GEngine',ROOT/'GEngine/include/external',
        ROOT/'RigidBodySimulation/include',ROOT/'GEngine/src/Physics',ROOT/'RigidBodySimulation/tests']
    roots += [ROOT/f'external/{name}/include' for name in ('sdl2','glad','rttr','tbb','reflection','spdlog','entt','assimp','fmod')]
    roots += [vc/'include',sdk/'Include/10.0.26100.0/ucrt',sdk/'Include/10.0.26100.0/shared',sdk/'Include/10.0.26100.0/um',sdk/'Include/10.0.26100.0/winrt']
    ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'};membership={}
    for name,path in [('Jolt',ROOT/'external/jolt-build/Jolt.vcxproj'),('GEngine',ROOT/'GEngine/GEngine.vcxproj'),('RigidBodySimulation',ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj')]:
        paths.add(path);tree=ET.parse(path);members=[]
        for node in tree.findall('.//m:ClCompile',ns):
            if 'Include' not in node.attrib:continue
            member=(path.parent/node.attrib['Include']).resolve()
            if name=='Jolt':member=OBS_SDK/member.relative_to(ROOT/'external/jolt')
            assert member.is_file(),member;paths.add(member);members.append(member.as_posix())
        membership[name]=sorted(members)
    pending=[p for p in paths if p.suffix.lower() in ('.h','.hpp','.inl','.c','.cpp')];seen=set();unresolved=set()
    pattern=re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]',re.M)
    while pending:
        p=pending.pop()
        if p in seen:continue
        seen.add(p)
        for name in pattern.findall(p.read_text(encoding='utf-8-sig',errors='replace')):
            found=next((q.resolve() for directory in [p.parent,*roots] if (q:=directory/name).is_file()),None)
            if found is None:unresolved.add(name);continue
            if found not in paths:paths.add(found);pending.append(found)
    for name in ('cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe'):
        p=vc/'bin/Hostx64/x64'/name
        if p.is_file():paths.add(p)
    paths.add(MSBUILD);paths.add(vc/'include/intrin0.h')
    hashes={p.as_posix():sha(p) for p in sorted(paths)}
    return {'hashes':hashes,'compilation_sources':membership,'include_roots':[p.as_posix() for p in roots],
        'unresolved_conditional_includes':sorted(unresolved),'canonical_admission_sha256':sha(ROOT/'logs/pre_editor/phase-26/canonical-integration-01/admission.json')}

def observation_assert_inputs(inputs):
    for path,digest in inputs['hashes'].items():assert sha(Path(path))==digest,path
    return True

def observation_projects(config,d):
    from xml.sax.saxutils import escape
    import re
    out=safe(OBS_RUNTIME/f'bin/{config}');inter=safe(OBS_RUNTIME/f'bin-int/{config}')
    assert not out.exists() and not inter.exists(),'Isolated build destination is already used'
    if globals().get('RECOVERY_ACTIVE',False) and config=='Release':
        glad_record=recovery_release_glad()
    else:
        reuse_record=ROOT/f'logs/pre_editor/phase-26/canonical-integration-01/build-{config}-01/result.json'
        reuse=json.loads(reuse_record.read_text());assert reuse['passed'] and reuse['source_and_index_preservation']=='PASS'
        libraries={p:h for p,h in reuse['products'].items() if p.replace('\\','/').endswith('/glad/glad.lib')}
        assert len(libraries)==1
        glad_path,glad_hash=next(iter(libraries.items()));assert sha(Path(glad_path))==glad_hash
        admission=json.loads((ROOT/'logs/pre_editor/phase-26/canonical-integration-01/admission.json').read_text())
        glad_inputs={p:h for p,h in admission['inputs'].items() if '/external/glad/' in p.replace('\\','/')}
        assert glad_inputs and all(sha(Path(p))==h for p,h in glad_inputs.items())
        glad_record={'status':'PASS_MATCHED_PROVIDER_REUSE','source_record':reuse_record.as_posix(),
            'source_record_sha256':sha(reuse_record),'inputs':glad_inputs,'library':glad_path,'sha256':glad_hash}
    glad_path,glad_hash=glad_record['library'],glad_record['sha256']
    write(d/'glad-reuse.json',glad_record)
    sdk_project=(ROOT/'external/jolt-build/Jolt.vcxproj').read_text(encoding='utf-8-sig')
    sdk_project=sdk_project.replace('..\\jolt',escape(str(OBS_SDK)))
    sdk_project=re.sub(r'<PreBuildEvent>.*?</PreBuildEvent>','',sdk_project,flags=re.S)
    write(d/'Jolt.vcxproj',sdk_project)
    escaped=lambda path:escape(str(path))
    linked=';'.join(escaped(p) for p in [out/'GEngine/GEngine.lib',out/'Jolt/Jolt.lib',Path(glad_path)])
    rbs_debug='GENGINE_G8_RESISTANCE_CHECKS;' if config=='Debug' else ''
    release_sdk='JPH_NO_DEBUG;' if config=='Release' else ''
    props=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup><OutDir>{escaped(out)}/$(MSBuildProjectName)/</OutDir><IntDir>{escaped(inter)}/$(MSBuildProjectName)/</IntDir><PreBuildEventUseInBuild>false</PreBuildEventUseInBuild><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild><BuildProjectReferences>false</BuildProjectReferences></PropertyGroup>
<ItemGroup><ProjectReference Remove="@(ProjectReference)" /></ItemGroup>
<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>{OBS_MACRO};GENGINE_WITH_JOLT;JPH_CROSS_PLATFORM_DETERMINISTIC;JPH_OBJECT_LAYER_BITS=16;{release_sdk}%(PreprocessorDefinitions)</PreprocessorDefinitions><AdditionalIncludeDirectories>{escaped(OBS_SDK)}</AdditionalIncludeDirectories><ShowIncludes>true</ShowIncludes><AdditionalOptions>/external:I"{escaped(OBS_SDK)}" %(AdditionalOptions)</AdditionalOptions></ClCompile><PreBuildEvent><Command></Command></PreBuildEvent><PostBuildEvent><Command></Command></PostBuildEvent></ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='GEngine' Or '$(MSBuildProjectName)'=='RigidBodySimulation'"><ClCompile><PreprocessorDefinitions>GENGINE_PHASE26_QUALIFICATION;GE_ENABLE_PHYSICS_PROFILING;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='RigidBodySimulation'"><ClCompile><PreprocessorDefinitions>{rbs_debug}%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile><Link><AdditionalDependencies>{linked};%(AdditionalDependencies)</AdditionalDependencies><GenerateMapFile>true</GenerateMapFile><MapFileName>$(OutDir)RigidBodySimulation.map</MapFileName></Link></ItemDefinitionGroup>
</Project>
'''
    write(d/'isolated.props',props)
    reused=globals().get('RECOVERY_PROVIDERS',{}) if globals().get('RECOVERY_ACTIVE',False) else {}
    project_paths=[('Jolt',d/'Jolt.vcxproj'),('GEngine',ROOT/'GEngine/GEngine.vcxproj'),('RigidBodySimulation',ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj')]
    project_items=''.join(f'<CurrentProject Include="{escaped(path)}" />' for name,path in project_paths if name not in reused)
    projects=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003" DefaultTargets="Build">
<ItemGroup>{project_items}</ItemGroup>
<Target Name="Build"><MSBuild Projects="@(CurrentProject)" Targets="Build" BuildInParallel="false" StopOnFirstFailure="true" /></Target></Project>'''
    write(d/'projects.proj',projects)
    return out,inter

def observation_freeze():
    import difflib
    accepted,amendment=observation_authority()
    assert not (OBS/'frozen-inputs.json').exists(),'Already frozen; no refresh of a used candidate'
    reproduction=json.loads((OBS/'reproduction.json').read_text())
    originals={row['path']:row['sha256'] for row in reproduction['source_members']}
    actual={p.relative_to(OBS_SDK).as_posix():sha(p) for p in OBS_SDK.rglob('*') if p.is_file()}
    delta={p for p in set(originals)|set(actual) if originals.get(p)!=actual.get(p)}
    assert delta==set(OBS_SDK_MEMBERS),('SDK overlay scope',delta)
    canonical=json.loads((ROOT/accepted['canonical_source_manifest']['path']).read_text())
    for row in canonical['rows']:
        if row['path'] in OBS_OWNED:continue
        p=ROOT/row['path'];assert (sha(p) if p.is_file() else None)==row.get('canonical_after_sha256'),row['path']
    patch=[];members=[];preimages={}
    for name in OBS_SDK_MEMBERS:
        old=ROOT/'external/jolt'/name;new=OBS_SDK/name
        old_bytes=old.read_bytes().replace(b'\r\n',b'\n') if old.exists() else None
        new_bytes=new.read_bytes().replace(b'\r\n',b'\n');preimages[name]=old_bytes
        patch.append(f'diff --git a/{name} b/{name}\n')
        if old_bytes is None:patch.append('new file mode 100644\n')
        patch.extend(difflib.unified_diff((old_bytes or b'').decode().splitlines(keepends=True),new_bytes.decode().splitlines(keepends=True),
            fromfile='a/'+name if old_bytes is not None else '/dev/null',tofile='b/'+name))
        members.append({'path':name,'base_raw_sha256':sha(old) if old.exists() else None,'result_raw_sha256':sha(new),
            'result_LF_sha256':hashlib.sha256(new_bytes).hexdigest(),'mode':'100644'})
    patch_bytes=''.join(patch).encode()
    spec=importlib.util.spec_from_file_location('jolt_patch_reproducer',ROOT/'tools/setup_jolt.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    rebuilt=module.apply_text_patch(patch_bytes,preimages)
    assert all(rebuilt[name]==(OBS_SDK/name).read_bytes().replace(b'\r\n',b'\n') for name in OBS_SDK_MEMBERS)
    patch_path=ROOT/'external/jolt-patches/phase26-qualification-observation-01.patch'
    write(patch_path,patch_bytes.decode())
    write(ROOT/'external/jolt-patches/phase26-qualification-observation-01.json',{
        'schema':1,'id':'phase26-qualification-observation-01','profile_macro':OBS_MACRO,'diagnostic_only':True,
        'upstream_commit':reproduction['upstream'],'layer_after':'gengine-contact-resistance-01',
        'ordinary_setup_or_pin_changed':False,'patch':patch_path.relative_to(ROOT).as_posix(),'patch_sha256':sha(patch_path),
        'members':members,'scope_acceptance':accepted['proposal'],'arena_policy_sha256':sha(OBS_ACTIVE/'PHASE_26_JOLT_ARENA_POLICY_AMENDMENT_01.json')})
    inputs=observation_inputs()
    write(OBS/'frozen-inputs.json',inputs)
    for relative in OBS_OWNED:
        target=safe(OBS/'source-frozen'/relative);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((ROOT/relative).read_bytes())
    for name in OBS_SDK_MEMBERS:
        target=safe(OBS/'source-frozen/diagnostic-sdk'/name);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((OBS_SDK/name).read_bytes())
    write(OBS/'freeze.json',{'status':'FROZEN_DIAGNOSTIC_SOURCE_NOT_BUILD_OR_RUNTIME_PASS','inputs_sha256':sha(OBS/'frozen-inputs.json'),
        'patch_sha256':sha(patch_path),'sdk_members':len(members),'production_algorithm_changes':False,
        'ordinary_token_proof_required_before_build':True,'builds_consumed':0,'processes_consumed':0})
    return 0

def observation_build(config):
    import re
    observation_authority();inputs=json.loads((OBS/'frozen-inputs.json').read_text());observation_assert_inputs(inputs)
    token_proof=json.loads((OBS/'ordinary-token-proof.json').read_text());assert token_proof['status']=='PASS'
    assert token_proof['frozen_inputs_sha256']==sha(OBS/'frozen-inputs.json')
    d=safe(OBS/f'build-{config}-01');assert not d.exists(),'No build retry authorized'
    if config=='Release':
        debug=json.loads((OBS/'build-Debug-01/result.json').read_text());assert debug['status']=='PASS'
    d.mkdir();out,inter=observation_projects(config,d)
    write(d/'inputs.json',inputs)
    command=[str(MSBUILD),str(d/'projects.proj'),'/t:Build',f'/p:Configuration={config}','/p:Platform=x64',
        '/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',
        f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}','/p:BuildProjectReferences=false',
        '/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);write(d/'budget-before.json',{'attempt':1,'maximum_attempts':1,'seconds_max':1200,'automatic_retry':False})
    start=time.monotonic();timed_out=False
    print(f'OBSERVATION {config} BUILD START',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=observation_environment(),stdout=log,stderr=subprocess.STDOUT)
        write(d/'process-started.json',{'pid':process.pid,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
        try:code=process.wait(timeout=1200)
        except subprocess.TimeoutExpired:
            timed_out=True;subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT);code=process.wait()
    elapsed=time.monotonic()-start
    unchanged=all(Path(p).is_file() and sha(Path(p))==digest for p,digest in inputs['hashes'].items())
    log_text=(d/'build.log').read_text(errors='replace');warnings=[]
    for line in log_text.splitlines():
        if not re.search(r'\bwarning (C\d+|LNK\d+|MSB\d+)',line):continue
        normalized=line.replace('\\','/').lower()
        vendor=any(part in normalized for part in ('/include/external/','/external/','/vc/tools/msvc/','/windows kits/'))
        warnings.append({'line':line,'owner':'vendor_or_system' if vendor else 'first_party_or_link'})
    products={p.as_posix():sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.lib','.exe')}
    expected=[out/'Jolt/Jolt.lib',out/'GEngine/GEngine.lib',out/'RigidBodySimulation/RigidBodySimulation.exe']
    passed=code==0 and not timed_out and elapsed<=1200 and unchanged and all(p.is_file() for p in expected) and not any(w['owner']=='first_party_or_link' for w in warnings)
    result={'status':'PASS' if passed else 'FAIL','exit_code':code,'seconds':elapsed,'timeout':timed_out,
        'inputs_unchanged':unchanged,'products':products,'warnings':warnings,'configuration':config,'attempt':1,
        'generated_build_inputs':{p.name:sha(p) for p in (d/'Jolt.vcxproj',d/'isolated.props',d/'projects.proj')}}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    if passed:observation_identity(config)
    return 0 if passed else 1

def observation_identity(config):
    observation_authority();d=OBS/f'build-{config}-01';result=json.loads((d/'result.json').read_text());assert result['status']=='PASS'
    inputs=json.loads((d/'inputs.json').read_text());observation_assert_inputs(inputs)
    out=OBS_RUNTIME/f'bin/{config}';inter=OBS_RUNTIME/f'bin-int/{config}';records={};dependencies=set();project_providers={}
    reused=json.loads((d/'provider-reuse.json').read_text())['providers'] if (d/'provider-reuse.json').is_file() else {}
    for provider in reused.values():recovery_validate_provider(provider)
    for name in ('Jolt','GEngine','RigidBodySimulation'):
        command_root=Path(reused[name]['compiler_records_directory']) if name in reused else inter/name
        commands=list(command_root.rglob('CL.command.1.tlog'));assert len(commands)==1,(name,commands)
        archive=safe(d/'compiler-records'/name);assert not archive.exists();archive.mkdir(parents=True)
        for source in commands[0].parent.glob('*.tlog'):
            target=archive/source.name;shutil.copyfile(source,target);records[target.as_posix()]=sha(target)
        text=(archive/'CL.command.1.tlog').read_text(encoding='utf-16');upper=text.upper()
        units={p.replace('\\','/').casefold() for line in text.splitlines() if line.startswith('^') for p in line[1:].split('|')}
        assert units=={p.casefold() for p in inputs['compilation_sources'][name]},(name,'compile membership')
        assert '/D '+OBS_MACRO in upper and '/STD:C++23' in upper and '/W3' in upper
        assert ('/D JPH_NO_DEBUG' in upper)==(config=='Release')
        assert '/MTD' in upper if config=='Debug' else '/MT ' in upper and '/MTD' not in upper
        assert ('/D GENGINE_G8_RESISTANCE_CHECKS' in upper)==(config=='Debug' and name=='RigidBodySimulation')
        assert '/D GENGINE_JOLT_FINAL_AUDIT' not in upper
        assert ('/D GENGINE_PHASE26_QUALIFICATION' in upper)==(name!='Jolt')
        project_dependencies=set()
        for path in archive.glob('*.read.*.tlog'):
            for line in path.read_text(encoding='utf-16').splitlines():
                project_dependencies.update(p.replace('\\','/').casefold() for p in line.lstrip('^').split('|') if len(p)>2 and p[1]==':')
        dependencies.update(project_dependencies)
        source_inputs={p:sha(Path(p)) for p in project_dependencies if Path(p).suffix.lower() in ('.h','.hpp','.hxx','.inl','.c','.cpp','.cxx')}
        project_providers[name]={'compiler_records_directory':archive.as_posix(),
            'compiler_record_hashes':{p.as_posix():sha(p) for p in archive.glob('*.tlog')},
            'object_root':reused[name]['object_root'] if name in reused else (inter/name).as_posix(),
            'source_inputs':source_inputs,'compilation_sources':inputs['compilation_sources'][name],
            'library':(out/name/(name+'.lib')).as_posix() if name!='RigidBodySimulation' else None,
            'library_sha256':sha(out/name/(name+'.lib')) if name!='RigidBodySimulation' else None,
            'configuration':config,'reused':name in reused}
    ordinary=(ROOT/'external/jolt').as_posix().casefold()+'/'
    assert not any(p.startswith(ordinary) and Path(p).suffix.lower() in ('.h','.hpp','.inl','.cpp') for p in dependencies),'Mixed ordinary SDK headers'
    assert (OBS_SDK/'Jolt/Physics/QualificationObservation.h').as_posix().casefold() in dependencies
    known={p.casefold() for p in inputs['hashes']}
    unknown_sources=[p for p in dependencies-known if Path(p).suffix.lower() in ('.h','.hpp','.hxx','.inl','.c','.cpp','.cxx')]
    assert not unknown_sources,('Actual compiler inputs outside frozen closure',unknown_sources)
    reads=set()
    for path in (d/'compiler-records/RigidBodySimulation').glob('link.read.*.tlog'):
        reads.update(p.replace('\\','/').casefold() for line in path.read_text(encoding='utf-16').splitlines() for p in line.lstrip('^').split('|') if len(p)>2 and p[1]==':')
    for name in ('Jolt','GEngine'):
        expected=(out/name/(name+'.lib')).as_posix().casefold()
        assert {p for p in reads if p.endswith('/'+name.lower()+'.lib')}=={expected},('Link provider',name)
    glad=json.loads((d/'glad-reuse.json').read_text());assert Path(glad['library']).as_posix().casefold() in reads
    assert all(sha(Path(p))==digest for p,digest in result['products'].items())
    sdk_objects=[p for p in Path(project_providers['Jolt']['object_root']).rglob('*.obj') if p.stem.casefold()=='physicssystem'];assert len(sdk_objects)==1,sdk_objects
    sizes=observation_coff_layout(sdk_objects[0])
    heap_bounds=observation_coff_heap_bounds(sdk_objects[0])
    assert heap_bounds['base_job_pool_bytes']==heap_bounds['base_job_slot_bytes']*2048
    assert heap_bounds['broadphase_node_bytes']==4 and heap_bounds['broadphase_center_bytes']==16
    heap_bounds.update({'schema':1,'base_job_slots':2048,'source':'Exact compiler-emitted SDK constants; first unconditional job page and one broadphase layer rebuild per substep.',
        'capacity_records_additional_max':32,'storage_record_capacity':2048,'admitted_body_slots':32})
    rounded=lambda x:(x+15)//16*16
    U,D,C,alignment,body_id,body_pair,constraint_pointer,island_index,candidate_slot,candidate_slots=sizes
    assert alignment==16 and 480<=C<=796 and (body_id,body_pair,constraint_pointer,island_index)==(4,8,8,4)
    assert candidate_slots==4096 and candidate_slot<=96
    canonical=rounded(16*U)+rounded(16384*C)+rounded(16*D)+328576
    direct=rounded(16*U)+rounded(128*C)+rounded(16*D)+133376
    assert max(canonical,direct)<=33554432
    extension_increment=(C-480)*16384+16*4096
    assert extension_increment<=5*1024*1024
    record={'status':'PASS','configuration':config,'products':result['products'],'compiler_records':records,'project_providers':project_providers,
        'actual_dependency_count':len(dependencies),'diagnostic_SDK_headers_and_libraries_only':True,
        'frozen_inputs_sha256':sha(OBS/'frozen-inputs.json'),'layout':{'Step':U,'CCDBody':D,'contact_max':C,'alignment':alignment,
            'BodyID':body_id,'BodyPair':body_pair,'ConstraintPointer':constraint_pointer,'island_index':island_index,
            'CCD_candidate_slot':candidate_slot,'CCD_candidate_slots':candidate_slots,'CCD_recorder_bytes':candidate_slot*candidate_slots},
        'layout_object':{'path':sdk_objects[0].as_posix(),'sha256':sha(sdk_objects[0])},'heap_source_bounds':heap_bounds,
        'pre_run_source_formula_caps':{'canonical_bytes':canonical,'direct_bytes':direct,'capacity_bytes':33554432},
        'Extension01_contact_storage':{'incremental_bytes':extension_increment,'limit_bytes':5242880,
            'formula':'(actual cMaxConstraintSize - 480)*16384 + (80 - 64)*4096',
            'unchanged_source_sha256':sha(OBS_SDK/'Jolt/Physics/Constraints/ContactConstraintManager.h')},
        'actual_dependencies':sorted(dependencies)}
    write(d/'identity.json',record);print(json.dumps({k:v for k,v in record.items() if k not in ('compiler_records','actual_dependencies','project_providers')}),flush=True)
    return record

def observation_coff_layout(path):
    """Read a compiler-emitted constant; never launch an extra SDK program."""
    data=Path(path).read_bytes()
    if data[:4]==b'\x00\x00\xff\xff':
        section_count,symbol_offset,symbol_count=struct.unpack_from('<III',data,44);header=56;symbol_size=20
        section_value=lambda at:struct.unpack_from('<i',data,at+12)[0]
    else:
        machine,section_count,_,symbol_offset,symbol_count,optional,_=struct.unpack_from('<HHIIIHH',data)
        assert machine==0x8664 and optional==0;header=20;symbol_size=18
        section_value=lambda at:struct.unpack_from('<h',data,at+12)[0]
    strings=symbol_offset+symbol_count*symbol_size;found=[];index=0
    while index<symbol_count:
        at=symbol_offset+index*symbol_size;raw=data[at:at+8]
        if raw[:4]==b'\0'*4:
            offset=struct.unpack_from('<I',raw,4)[0];end=data.index(b'\0',strings+offset);name=data[strings+offset:end].decode(errors='replace')
        else:name=raw.rstrip(b'\0').decode(errors='replace')
        value=struct.unpack_from('<I',data,at+8)[0];section=section_value(at);aux=data[at+symbol_size-1]
        if 'kObservationTypeSizes' in name and section>0:
            entry=header+(section-1)*40;size,raw_offset=struct.unpack_from('<II',data,entry+16)
            assert value+40<=size;found.append(struct.unpack_from('<10I',data,raw_offset+value))
        index+=1+aux
    assert len(found)==1,('Missing/duplicate diagnostic type layout constant',found)
    return found[0]

def observation_coff_heap_bounds(path):
    """Bind the four typed heap formulas before runtime, without an SDK process."""
    data=Path(path).read_bytes()
    if data[:4]==b'\x00\x00\xff\xff':
        section_count,symbol_offset,symbol_count=struct.unpack_from('<III',data,44);header=56;symbol_size=20
        section_value=lambda at:struct.unpack_from('<i',data,at+12)[0]
    else:
        machine,section_count,_,symbol_offset,symbol_count,optional,_=struct.unpack_from('<HHIIIHH',data)
        assert machine==0x8664 and optional==0;header=20;symbol_size=18
        section_value=lambda at:struct.unpack_from('<h',data,at+12)[0]
    names={'kBaseJobPoolBytes':'base_job_pool_bytes','kBaseJobSlotBytes':'base_job_slot_bytes',
        'kBroadPhaseNodeBytes':'broadphase_node_bytes','kBroadPhaseCenterBytes':'broadphase_center_bytes'}
    strings=symbol_offset+symbol_count*symbol_size;found={};index=0
    while index<symbol_count:
        at=symbol_offset+index*symbol_size;raw=data[at:at+8]
        if raw[:4]==b'\0'*4:
            offset=struct.unpack_from('<I',raw,4)[0];end=data.index(b'\0',strings+offset);name=data[strings+offset:end].decode(errors='replace')
        else:name=raw.rstrip(b'\0').decode(errors='replace')
        value=struct.unpack_from('<I',data,at+8)[0];section=section_value(at);aux=data[at+symbol_size-1]
        for symbol,key in names.items():
            if name.startswith('?'+symbol+'@QualificationObservation@JPH@@') and section>0:
                assert key not in found and section<=section_count,('Duplicate or invalid SDK heap bound',name)
                entry=header+(section-1)*40;size,raw_offset=struct.unpack_from('<II',data,entry+16)
                assert value+8<=size;found[key]=struct.unpack_from('<Q',data,raw_offset+value)[0]
        index+=1+aux
    assert set(found)==set(names.values()) and all(value>0 for value in found.values()),('Missing compiled SDK heap bound',found)
    return found


def observation_ordinary_tokens(source):
    """Partially preprocess only the new macro, leaving old conditions symbolic.

    Identical residual preprocessing tokens prove equality for every ordinary
    combination of the unchanged old macros, without compiling another target.
    """
    import re
    lines=source.splitlines(keepends=True)
    directive=re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')
    compact=lambda value:' '.join(re.findall(r'defined|[A-Za-z_]\w*|\d+|&&|\|\||==|!=|<=|>=|[^\s]',value))
    def expression(value):
        value=re.sub(r'defined\s*(?:\(\s*'+OBS_MACRO+r'\s*\)|\b'+OBS_MACRO+r'\b)','0',value.strip())
        value=re.sub(r'\b'+OBS_MACRO+r'\b','0',value)
        def strip(v):
            v=v.strip()
            while v.startswith('(') and v.endswith(')'):
                level=0;whole=True
                for index,char in enumerate(v):
                    level+=(char=='(')-(char==')')
                    if not level and index!=len(v)-1:whole=False;break
                if not whole:break
                v=v[1:-1].strip()
            return v
        def evaluate(v):
            v=strip(v)
            for operator in ('||','&&'):
                depth=0;parts=[];start=0;index=0
                while index<len(v):
                    char=v[index];depth+=(char=='(')-(char==')')
                    if not depth and v.startswith(operator,index):parts.append(v[start:index]);index+=2;start=index;continue
                    index+=1
                if parts:
                    parts.append(v[start:]);values=[evaluate(p) for p in parts]
                    if operator=='&&':
                        if False in values:return False
                        values=[x for x in values if x is not True]
                    else:
                        if True in values:return True
                        values=[x for x in values if x is not False]
                    if not values:return operator=='&&'
                    if len(values)==1:return values[0]
                    return (' '+operator+' ').join('('+str(x)+')' for x in values)
            if v in ('0','1'):return v=='1'
            if v.startswith('!') and not v.startswith('!='):
                answer=evaluate(v[1:]);return not answer if isinstance(answer,bool) else '! ('+answer+')'
            return compact(v)
        return evaluate(value)
    def sequence(index,stops):
        output=[]
        while index<len(lines):
            match=directive.match(lines[index])
            if match and match[1] in stops:return ''.join(output),index
            if match and match[1] in ('if','ifdef','ifndef'):
                kind,value=match.groups()
                if kind!='if':value=('!' if kind=='ifndef' else '')+'defined('+value.strip()+')'
                branches=[];condition=expression(value);index+=1
                while True:
                    body,index=sequence(index,{'elif','else','endif'});branches.append((condition,body))
                    assert index<len(lines),'Unterminated conditional';end=directive.match(lines[index]);index+=1
                    if end[1]=='endif':break
                    if end[1]=='else':
                        body,index=sequence(index,{'endif'});assert index<len(lines);index+=1;branches.append((True,body));break
                    condition=expression(end[2])
                residual=[]
                for cond,body in branches:
                    if cond is False:continue
                    if cond is True:
                        if not residual:output.append(body)
                        else:residual.append((True,body))
                        break
                    residual.append((cond,body))
                if residual:
                    for n,(cond,body) in enumerate(residual):
                        output.append(('#else\n' if cond is True else ('#if ' if n==0 else '#elif ')+cond+'\n')+body)
                    output.append('#endif\n')
            else:output.append(lines[index]);index+=1
        return ''.join(output),index
    residual,_=sequence(0,set())
    # New diagnostic call-site helpers have an empty replacement list in the
    # ordinary branch. Expand precisely those helpers before token comparison.
    helpers=re.findall(r'^\s*#\s*define\s+(JPH_GENGINE_OBSERVE_\w+)\([^\n]*?\)\s*$',residual,re.M)
    for helper in helpers:
        residual=re.sub(r'^\s*#\s*define\s+'+helper+r'\([^\n]*?\)\s*$', '',residual,flags=re.M)
        residual=re.sub(r'^\s*#\s*undef\s+'+helper+r'\s*$', '',residual,flags=re.M)
        pattern=re.compile(r'\b'+helper+r'\s*\(')
        while (match:=pattern.search(residual)) is not None:
            end=match.end();depth=1
            while end<len(residual) and depth:
                depth+=(residual[end]=='(')-(residual[end]==')');end+=1
            assert depth==0,helper
            residual=residual[:match.start()]+residual[end:]
    # Comments/whitespace are not preprocessing tokens. Keep literals intact.
    token=re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|R"([^ ()\\\t\n]{0,16})\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|(?:\d[\w.]*)|##|::|->|\+\+|--|&&|\|\||==|!=|<=|>=|<<|>>|\+=|-=|\*=|/=|[^\s]')
    return [m.group() for m in token.finditer(residual) if not m.group().startswith(('//','/*'))]

def observation_token_proof():
    import ast
    observation_authority();inputs=json.loads((OBS/'frozen-inputs.json').read_text());observation_assert_inputs(inputs)
    records=[]
    pairs=[(ROOT/'external/jolt'/name,OBS_SDK/name) for name in OBS_SDK_MEMBERS if name!='Jolt/Physics/QualificationObservation.h']
    pairs += [(OBS/'source-before'/name,ROOT/name) for name in OBS_OWNED if name.endswith(('.cpp','.h'))]
    for old,new in pairs:
        before=observation_ordinary_tokens(old.read_text());after=observation_ordinary_tokens(new.read_text())
        if before!=after:
            import difflib
            write(OBS/'ordinary-token-failure.json',{'status':'FAIL','source':new.as_posix(),'before_sha256':sha(old),'after_sha256':sha(new),
                'token_diff':list(difflib.unified_diff([x+'\n' for x in before],[x+'\n' for x in after]))})
            raise AssertionError('Ordinary preprocessing token mismatch: '+str(new))
        records.append({'source':new.as_posix(),'baseline':old.as_posix(),'baseline_sha256':sha(old),'current_sha256':sha(new),'tokens':len(after),'status':'PASS'})
    assert not observation_ordinary_tokens((OBS_SDK/'Jolt/Physics/QualificationObservation.h').read_text().replace('#pragma once',''))
    old_tree=ast.parse((OBS/'source-before/tools/pre_editor_phase26_qualification.py').read_text());new_tree=ast.parse(Path(__file__).read_text())
    old_functions={x.name:ast.dump(x,include_attributes=False) for x in old_tree.body if isinstance(x,(ast.FunctionDef,ast.ClassDef))}
    new_functions={x.name:ast.dump(x,include_attributes=False) for x in new_tree.body if isinstance(x,(ast.FunctionDef,ast.ClassDef))}
    assert all(new_functions[name]==value for name,value in old_functions.items()),'Historical runner function modified'
    record={'status':'PASS','method':'Symbolically eliminate only the diagnostic macro; exact residual preprocessing tokens including existing conditions and literals. All untouched includes have frozen identities. No extra compile/build/process.',
        'records':records,'historical_runner_functions_unchanged':len(old_functions),'frozen_inputs_sha256':sha(OBS/'frozen-inputs.json'),
        'source_location_metadata':'Line offsets are documented separately; no __LINE__/__FILE__ change is used to claim Physics behavior equivalence.'}
    write(OBS/'ordinary-token-proof.json',record);print(json.dumps({'status':'PASS','members':len(records),'historical_functions':len(old_functions)}));return 0

def observation_timeout_termination(process,d,seconds,start,log=None,tree=False):
    """Preserve deadline failure, then bound termination/reaping without refunding it."""
    write(d/'timeout.json',{'status':'FAIL_DEADLINE_EXCEEDED','pid':process.pid,'timeout_seconds':seconds,
        'elapsed_at_timeout':time.monotonic()-start,'tree_termination_required':tree})
    record={'pid':process.pid,'timeout_sha256':sha(d/'timeout.json'),'tree_termination_required':tree,'taskkill_exit_code':None,
        'taskkill_timeout':False,'handle_termination_attempted':False,'parent_reaped':False,'errors':[]}
    if tree:
        try:
            killed=subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],stdout=log,stderr=subprocess.STDOUT,timeout=5)
            record['taskkill_exit_code']=killed.returncode
        except subprocess.TimeoutExpired:record['taskkill_timeout']=True
        except OSError as error:record['errors'].append('taskkill: '+str(error))
    record['handle_termination_attempted']=True
    try:process.kill()
    except OSError as error:record['errors'].append('creation-handle termination: '+str(error))
    try:code=process.wait(timeout=5);record['parent_reaped']=True
    except subprocess.TimeoutExpired:
        code=process.poll();record['parent_reaped']=code is not None
        record['errors'].append('Parent was not reaped within the bounded five-second cleanup wait')
    except OSError as error:
        code=None;record['errors'].append('bounded parent reap: '+str(error))
    record['exit_code']=code;record['elapsed_after_cleanup']=time.monotonic()-start
    record['tree_termination']='TASKKILL_SUCCESS' if tree and record['taskkill_exit_code']==0 else ('UNPROVEN' if tree else 'NO_CHILD_SIMULATION')
    record['contained']=record['parent_reaped'] and (not tree or record['taskkill_exit_code']==0)
    record['status']='FAIL_TIMEOUT_CONTAINED' if record['contained'] else 'FAIL_TIMEOUT_CONTAINMENT_UNPROVEN'
    write(d/'termination.json',record)
    return code,record


def observation_run(config,case,mode):
    import ctypes
    import platform
    if RECOVERY_ACTIVE:recovery_assert_containment()
    observation_authority();inputs=json.loads((OBS/'frozen-inputs.json').read_text());observation_assert_inputs(inputs)
    identity=json.loads((OBS/f'build-{config}-01/identity.json').read_text());assert identity['status']=='PASS'
    for path,digest in identity['products'].items():assert sha(Path(path))==digest,path
    order=[('C4','full'),('C8','full'),('C16','full'),('G8','full'),('C16','minimal'),('G8','minimal')]
    if config=='Debug':
        assert (case,mode)==('observer','correctness')
    else:
        assert (case,mode) in order
        observer=json.loads((OBS/'run-Debug-observer-correctness-01/result.json').read_text());assert observer['status']=='PASS'
        admission=json.loads((OBS/'observer-admission.json').read_text());assert admission['status']=='PASS'
        for earlier_case,earlier_mode in order[:order.index((case,mode))]:
            prior=OBS/f'run-Release-{earlier_case}-{earlier_mode}-01'
            assert json.loads((prior/'result.json').read_text())['status']=='PASS'
            assert json.loads((prior/'jolt-audit-01.json').read_text())['status']=='PASS'
    d=safe(OBS/f'run-{config}-{case}-{mode}-01');assert not d.exists(),'No runtime retry authorized'
    d.mkdir();data=safe(d/'data');data.mkdir()
    cwd=safe(OBS_RUNTIME/f'runtime/{config}-{case}-{mode}');assert not cwd.exists();cwd.mkdir(parents=True)
    out=OBS_RUNTIME/f'bin/{config}/RigidBodySimulation';exe=out/'RigidBodySimulation.exe'
    dependencies={}
    for source in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):
        dest=safe(out/source.name)
        if not dest.exists():shutil.copyfile(source,dest)
        assert sha(source)==sha(dest);dependencies[dest.as_posix()]=sha(dest)
    env=observation_environment(config)
    if config=='Debug':
        command=[str(exe),'--backend-checks'];env['GENGINE_PHASE26_OBSERVATION_OUTPUT']=str(data)
    else:
        scene='distance-chain-contact' if case=='G8' else 'distance-chain'
        command=[str(exe),'--scene',scene,'--links',case[1:],'--backend','jolt','--phase26-output',str(data),'--phase26-mode',mode]
    frequency=ctypes.c_longlong();assert ctypes.windll.kernel32.QueryPerformanceFrequency(ctypes.byref(frequency))
    quantum=math.ceil(1e9/frequency.value);assert 0<quantum<=1000
    if not (OBS/'reference-hardware.json').exists():
        write(OBS/'reference-hardware.json',{'clock_quantum_ns':quantum,'QPC_frequency_Hz':frequency.value,
            'platform':platform.uname()._asdict(),'processor':os.environ.get('PROCESSOR_IDENTIFIER'),
            'protocol':'All completed ticks; inclusive Update + full-only Evaluate; common canonicalization outside both boundaries; no trimming, pooling or observer subtraction.'})
    else:assert json.loads((OBS/'reference-hardware.json').read_text())['clock_quantum_ns']==quantum
    write(d/'invocation.json',{'command':command,'cwd':cwd.as_posix(),'executable_sha256':sha(exe),'dependencies':dependencies,
        'identity_sha256':sha(OBS/f'build-{config}-01/identity.json'),'asset_root':env['GENGINE_ASSET_ROOT'],
        'deadline_seconds':180,'maximum_updates':120 if config=='Debug' else 1200,'automatic_retries':0,
        'source_inputs_sha256':sha(OBS/'frozen-inputs.json')})
    start=time.monotonic();timed_out=False;termination=None
    print(f'OBSERVATION RUN {config} {case} {mode}',flush=True)
    with (d/'process.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT)
        write(d/'process-started.json',{'pid':process.pid,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
        try:code=process.wait(timeout=max(0,start+180-time.monotonic()))
        except subprocess.TimeoutExpired:
            timed_out=True;code,termination=observation_timeout_termination(process,d,180,start)
    elapsed=time.monotonic()-start
    native=json.loads((data/'result.json').read_text()) if (data/'result.json').is_file() else None
    exported=sum(p.stat().st_size for p in d.rglob('*') if p.is_file())+sum(p.stat().st_size for p in cwd.rglob('*') if p.is_file())
    unchanged=all(Path(p).is_file() and sha(Path(p))==digest for p,digest in inputs['hashes'].items())
    passed=code==0 and not timed_out and elapsed<=180 and unchanged and exported<=67108864 and bool(native and native.get('complete'))
    if config=='Debug':
        required={'direct','ccd_arbitration','concurrent_contacts_joints','lifecycle','synthetic','parity','witness','allocation','teardown'}
        passed=passed and native.get('updates')==88 and native.get('status')=='PASS' and required<=set(native.get('gates',{})) and all(native.get('gates',{}).values()) and native.get('combined_recorder_bytes',8388609)<=8388608
    else:passed=passed and native.get('ticks')==1200 and native.get('clean_shutdown') and native.get('total_recorder_bytes',8388609)<=8388608
    result={'status':'PASS' if passed else 'FAIL','exit_code':code,'seconds':elapsed,'timeout':timed_out,'inputs_unchanged':unchanged,
        'export_bytes':exported,'native_result':native,'process_budget_consumed':1,'termination':termination,
        'updates_consumed':native.get('updates',native.get('ticks')) if native else 'UNKNOWN_PRESERVE_PROCESS_LOG'}
    write(d/'result.json',result);print(json.dumps(result),flush=True)
    if config=='Debug' and passed:
        required={'direct','ccd_arbitration','concurrent_contacts_joints','lifecycle','synthetic','parity','witness','allocation','teardown'}
        assert required<=set(native['gates']) and all(native['gates'][k] for k in required)
        assert native['updates']==88 and native['combined_recorder_bytes']<=8388608
        raw_audit=observation_debug_audit(d)
        write(OBS/'observer-admission.json',{'status':raw_audit['status'],'build_identity_sha256':sha(OBS/'build-Debug-01/identity.json'),
            'result_sha256':sha(d/'result.json'),'updates':native['updates'],'gates':native['gates'],
            'raw_audit_sha256':sha(d/'observer-raw-audit-01.json'),
            'scope':'New observer correctness only; historical suites not rerun; Release physical qualification still required.'})
        passed=raw_audit['status']=='PASS'
    return 0 if passed else 1

def observation_decoded(directory):
    # Preserve all original numeric field meanings; the diagnostic profile
    # supplies compact body offsets and a typed Jolt work-field count.
    import ast
    import inspect
    tree=ast.parse(inspect.getsource(decoded_run));tree.body[0].name='_decode_observation'
    class WorkCount(ast.NodeTransformer):
        def visit_Constant(self,node):
            if node.value=='12Q':return ast.parse("str(meta['jolt_observation_layout']['work_kind_count']*3)+'Q'",mode='eval').body
            return node
    tree=ast.fix_missing_locations(WorkCount().visit(tree));scope=dict(globals());exec(compile(tree,'<unchanged-field-decoder-with-Jolt-work-count>','exec'),scope)
    meta,records=scope['_decode_observation'](directory)
    raw=(directory/'data/samples.bin').read_bytes();o=meta['offsets'];identity=meta['identity_layout']
    for record in records:
        for i,body in enumerate(record['bodies']):
            at=record['tick']*meta['sample_bytes']+i*o['body_stride'];h=at+identity['handle']
            body['handle']=(struct.unpack_from('<I',raw,h+identity['index'])[0],
                struct.unpack_from('<Q',raw,h+identity['generation'])[0],struct.unpack_from('<Q',raw,h+identity['registry'])[0])
            body['type']=struct.unpack_from('<I',raw,at+identity['type'])[0]
            body['inverse_mass']=struct.unpack_from('<f',raw,at+o['inverse_mass'])[0]
            body['center_of_mass']=struct.unpack_from('<3f',raw,at+o['center_of_mass'])
    return meta,records

def observation_numeric_audit(config,mode,case):
    """Reuse original formulas verbatim; replace only authority/destination/schema."""
    import ast
    import inspect
    tree=ast.parse(inspect.getsource(audit));tree.body[0].name='_observation_numeric'
    class Route(ast.NodeTransformer):
        def visit_Name(self,node):
            mapping={'authority':'observation_authority','BASE':'OBS','decoded_run':'observation_decoded'}
            if node.id in mapping:return ast.copy_location(ast.Name(id=mapping[node.id],ctx=node.ctx),node)
            return node
        def visit_Constant(self,node):
            if node.value=='native complete/clean shutdown':return ast.copy_location(ast.Constant('Jolt complete/clean shutdown'),node)
            return node
    tree=ast.fix_missing_locations(Route().visit(tree));scope=dict(globals());exec(compile(tree,'<original-OD08-formulas-Jolt-data>','exec'),scope)
    return scope['_observation_numeric'](config,mode,case,'01',OBS)

def observation_enums():
    import re
    source=(OBS_SDK/'Jolt/Physics/QualificationObservation.h').read_text()
    result={}
    for name in ('EventKind','Phase','ArenaSite','WorkKind','StorageAction','LifeKind','HeapAction','AnchorSpace','IdentityAction'):
        match=re.search(r'enum class '+name+r'\s*:\s*\w+\s*\{([^}]+)\}',source,re.S);assert match,name
        items=[x.strip() for x in match[1].split(',') if x.strip()]
        assert all(re.fullmatch(r'\w+',x) for x in items),(name,items)
        result[name]=dict(enumerate(items))
    return result

def observation_frames(directory):
    """Decode the frozen x64 POD ABI, validating every extent before a read."""
    meta=json.loads((directory/'data/result.json').read_text());layout=meta['jolt_observation_layout'];enums=observation_enums()
    assert layout['schema']==2 and layout['frame_header_bytes']==80 and layout['kind_bytes']==4 and layout['worker_id_bytes']==4
    expected_layouts={'context_layout':{'bytes':32,'world':0,'epoch':8,'update':16,'substep':24,'phase':28,'site':30},
        'point_offsets':{'body1':32,'subshape1':36,'body2':40,'subshape2':44,'ordinal':48,'space':52,'anchor1':56,'anchor2':68,'cached_normal':80,'solver_normal':92,'lambda':104},
        'identity_offsets':{'sdk_body_id':32,'managed_index':36,'managed_generation':40,'managed_registry':48,'action':56},
        'storage_offsets':{'pointer':32,'requested':40,'aligned':48,'usage_after':56,'count':64,'action':68},
        'heap_offsets':{'pointer':32,'old_pointer':40,'requested':48,'old_bytes':56,'action':64}}
    for name,expected in expected_layouts.items():
        assert layout[name]==expected,(name,layout[name])
    assert layout['frame_kind_offset']==40 and layout['frame_counts_offset']==44
    assert (layout['work_entry_bytes'],layout['work_count_offset'],layout['work_changed_offset'],layout['work_roots_offset'])==(24,0,8,16)
    formats={'point':('6I13f',112),'ccd':('4I9fI',88),'lifetime':('7I',64),'work':('4I',48),
        'phase':('H2xIQ',48),'storage':('4Q2I',72),'heap':('4QI',72),'failure':('IIQ',48),'identity':('IIQQI',64)}
    for key,(_,size) in formats.items():assert layout[key+'_bytes']==size,(key,layout[key+'_bytes'],size)
    raw=(directory/'data'/layout['file']).read_bytes();at=0;frames=[]
    while at<len(raw):
        assert at+80<=len(raw),'Truncated frame header'
        header=struct.unpack_from('<3Q14I',raw,at);at+=80
        magic,epoch,update,schema,substeps,workers,valid,kind,*counts=header
        assert magic==0x3142534F36325048 and schema==2 and epoch>0 and valid==1 and len(counts)==9 and kind in range(4)
        frame={'epoch':epoch,'update':update,'substeps':substeps,'worker_threads':workers,'kind':kind}
        for (key,(fmt,size)),count in zip(formats.items(),counts):
            assert count<=layout[key+'_capacity'] and at+size*count<=len(raw),(key,count)
            records=[]
            for i in range(count):
                base=at+i*size;world,ep,tick,step,phase,site=struct.unpack_from('<3QIHH',raw,base)
                assert phase in enums['Phase'] and site in enums['ArenaSite']
                records.append({'world':world,'epoch':ep,'update':tick,'substep':step,
                    'phase':enums['Phase'][phase],'site':enums['ArenaSite'][site],
                    'value':struct.unpack_from('<'+fmt,raw,base+32)})
            at+=size*count
            if key in ('point','ccd'):
                assert at+4*count<=len(raw)
                kinds=struct.unpack_from('<'+str(count)+'I',raw,at);at+=4*count
                for record,kind in zip(records,kinds):record['event']=enums['EventKind'][kind]
            assert at+4*count<=len(raw)
            ids=struct.unpack_from('<'+str(count)+'I',raw,at);at+=4*count
            for record,worker in zip(records,ids):record['worker']=worker
            frame[key]=records
        frames.append(frame)
    assert at==len(raw)
    return meta,frames,enums

def observation_nonjoint_heap_admission(meta,frames,identity,body_counts,check):
    """Admit only two exact source-owned heap paths; all other shared work stays unresolved."""
    compiled=identity['heap_source_bounds'];reported=meta['heap_source_bounds']
    keys=('schema','base_job_pool_bytes','base_job_slot_bytes','base_job_slots','broadphase_node_bytes','broadphase_center_bytes')
    check(all(reported.get(key)==compiled[key] for key in keys),'compiled SDK heap source bounds match runtime metadata')
    pool=compiled['base_job_pool_bytes'];node=compiled['broadphase_node_bytes'];center=compiled['broadphase_center_bytes']
    check(pool==2048*compiled['base_job_slot_bytes'] and compiled['base_job_slots']==2048 and (node,center)==(4,16),'exact source-derived job and broadphase types')
    admitted=set();base_first={};base_allocations={};base_live={};base_calls=0;broad_calls=0;broad_bytes=0;slot_max=0
    def context(event):return tuple(event[key] for key in ('world','epoch','update','substep','phase','worker'))
    for frame in frames:
        tick=frame['update'];epoch=frame['epoch'];steps=frame['substeps'];capacity={};broad_groups={};broad_live={}
        if tick:
            live_bodies=body_counts[epoch,tick]
            for site in ('BaseJobPool','BroadPhaseBodyStorage'):
                entries=[e for e in frame['storage'] if e['site']==site]
                check(len(entries)<=steps,'source-bound heap capacity record multiplicity',{'epoch':epoch,'tick':tick,'site':site})
                for event in entries:
                    pointer,requested,aligned,usage,count,action=event['value'];key=context(event)
                    common=pointer==usage==0 and action==2 and event['world']!=0 and event['epoch']==epoch and event['update']==tick and event['worker']!=0
                    if site=='BaseJobPool':
                        good=common and event['phase']=='Update' and event['substep']==0 and count==2048 and requested==aligned==pool
                        base_first.setdefault((event['world'],epoch),tick)
                    else:
                        good=common and event['phase']=='SharedBroadPhase' and 0<=event['substep']<steps and live_bodies<=count<=compiled['admitted_body_slots'] and requested==aligned==count*(node+center)
                        slot_max=max(slot_max,count)
                        check((site,key) not in capacity,'one exact broadphase Prepare capacity per context',key)
                    check(good,'typed heap capacity has exact source bytes count and context',event)
                    if good:capacity[site,key]=event
        for event in frame['heap']:
            pointer,old,amount,old_bytes,action=event['value'];site=event['site'];key=context(event)
            if action in (3,4) and pointer in base_live:
                allocation=base_live.pop(pointer)
                good=action==4 and old==amount==old_bytes==0 and frame['kind']==3 and event['update']==0 and event['phase']=='Lifetime' and event['substep']==0xffffffff
                good=good and all(event[name]==allocation[name] for name in ('world','epoch','worker'))
                check(good,'base job page exact aligned teardown release',event)
                continue
            if site not in ('BaseJobPool','BroadPhaseBodyStorage'):continue
            check(bool(tick) and (site,key) in capacity,'typed heap event has matching actual SDK capacity scope',event)
            if not tick or (site,key) not in capacity:continue
            if site=='BaseJobPool':
                owner=(event['world'],epoch);base_calls+=action in (0,1,2)
                good=action==2 and pointer!=0 and pointer%64==0 and old==old_bytes==0 and amount==pool
                good=good and owner not in base_allocations and tick==base_first.get(owner)
                check(good,'single source-bound job page at first actual job',event)
                if good:
                    base_allocations[owner]=pointer;base_live[pointer]=event;admitted.add(id(event))
            elif action in (0,1,2):
                group=broad_groups.setdefault(key,[]);ordinal=len(group)
                width=node if ordinal==0 else center
                maximum=capacity[site,key]['value'][4] if ordinal==0 else group[0]['value'][2]//node
                count=amount//width
                good=action==0 and old==old_bytes==0 and pointer!=0 and pointer not in broad_live and ordinal<2 and amount%width==0
                good=good and 0<count<=maximum and (ordinal==0 or count>1) and pointer%width==0
                check(good,'exact ordered broadphase NodeID and optional Vec3 arrays',event)
                group.append(event);broad_calls+=1;broad_bytes+=amount
                if good:broad_live[pointer]=event;admitted.add(id(event))
            elif action in (3,4):
                allocation=broad_live.pop(pointer,None)
                good=action==3 and allocation is not None and old==amount==old_bytes==0
                good=good and context(allocation)==key if allocation is not None else False
                check(good,'broadphase temporary exact same-worker context release',event)
            else:check(False,'typed heap action known',event)
        check(not broad_live,'all broadphase temporary arrays released inside Update',{'epoch':epoch,'tick':tick})
        if tick:
            calls=sum(len(group) for group in broad_groups.values());requested=sum(e['value'][2] for group in broad_groups.values() for e in group)
            slots=max((e['value'][4] for (site,_),e in capacity.items() if site=='BroadPhaseBodyStorage'),default=0)
            check(calls<=2*steps and requested<=slots*(node+center)*steps,'broadphase request and byte source bound per Update',{'epoch':epoch,'tick':tick})
    check(not base_live,'all observed base job pages released by owning world teardown')
    check(reported.get('base_job_pool_requests')==base_calls,'base job request metadata agrees with raw events')
    check(reported.get('body_slot_count_max')==slot_max,'body-slot upper bound agrees with actual SDK capacity records')
    check(reported.get('ownership_validated_updates')==sum(bool(f['update']) for f in frames) and reported.get('ownership_failures')==0,'all observed updates passed precise native ownership validation')
    return admitted,{'base_job_pool_requests':base_calls,'broadphase_body_requests':broad_calls,'broadphase_requested_bytes':broad_bytes,'body_slot_count_max':slot_max}


def observation_debug_audit(directory):
    """Audit the one admitted Debug process from preserved raw POD streams."""
    failures=[];metrics={}
    def check(ok,gate,detail=None):
        if not ok and not any(x['gate']==gate for x in failures):failures.append({'gate':gate,'detail':detail})
    try:
        meta,frames,enums=observation_frames(directory);updates=[f for f in frames if f['kind']==1]
        check(len(updates)==44,'44 observed Physics updates')
        raw_export=meta['raw_export'];check(raw_export['valid'] and raw_export['observer_frames']==44 and raw_export['parity_frames']==88 and raw_export['synthetic_frames']==6,'complete raw export')
        layout=meta['parity_layout'];assert layout['schema']==1 and layout['frame_header_bytes']==56
        assert (layout['physical_state_bytes'],layout['impulse_state_bytes'],layout['joint_entry_bytes'])==(80,128,4)
        assert layout['physical_offsets']=={'id':0,'motion':4,'active':8,'center':16,'motion_bits':40}
        assert layout['impulse_offsets']=={'manifold':0,'body1':8,'body2':12,'point':16,'ccd':20,'roots':24,'floats':28,'accumulators':80}
        raw=(directory/'data'/layout['file']).read_bytes();at=0;parity={};on={};counts={}
        while at<len(raw):
            assert at+56<=len(raw);magic,epoch,schema,fixture,case,arm,tick,substeps,body_count,impulse_count,joint_count=struct.unpack_from('<2Q9I',raw,at);at+=56
            assert magic==0x3159503632504850 and schema==1 and arm in (0,1) and substeps in (1,2,16)
            assert body_count<=24 and impulse_count<=4096 and joint_count<=8
            extra=80*body_count+128*impulse_count+8*joint_count;assert at+extra<=len(raw)
            bodies=[struct.unpack_from('<3I4x3Q10I',raw,at+i*80) for i in range(body_count)];at+=80*body_count
            impulses=[struct.unpack_from('<Q17I4x6Q',raw,at+i*128) for i in range(impulse_count)];at+=128*impulse_count
            joints=struct.unpack_from('<'+str(2*joint_count)+'I',raw,at);at+=8*joint_count
            key=(fixture,case,tick,substeps);check((key,arm) not in parity,'unique off/on parity frame')
            parity[key,arm]={'bodies':bodies,'impulses':impulses,'joints':joints,'joint_count':joint_count}
            counts[case,arm]=counts.get((case,arm),0)+1
            if arm:check((epoch,tick) not in on,'unique observed epoch/update');on[epoch,tick]=(case,substeps,parity[key,arm])
        check(at==len(raw) and len(parity)==88,'88 complete physical and old-impulse frames')
        expected={**{i:2 for i in range(1,13)},101:4,102:4,116:4,201:2,301:6}
        check(counts=={(case,arm):count for case,count in expected.items() for arm in (0,1)},'exact frozen observer fixture coverage')
        for (key,arm),value in parity.items():
            if arm:check(parity.get((key,0))==value,'exact off/on physical impulse activation joint parity',key)
        identity=json.loads((OBS/'build-Debug-01/identity.json').read_text());cap=identity['pre_run_source_formula_caps']['direct_bytes']
        nonjoint,heap_ownership=observation_nonjoint_heap_admission(meta,frames,identity,{key:len(value[2]['bodies']) for key,value in on.items()},check)
        live={};heap_peak=0;heap_bytes=0;reserve_calls=release_calls=reserve_bytes=release_bytes=0;arena_peak=0
        for frame in frames:
            for event in frame['heap']:
                pointer,old,amount,old_amount,action=event['value']
                if action in (0,1,2) and pointer:
                    if action==1 and old:
                        check(old in live,'Debug reallocation ownership')
                        if old in live:heap_bytes-=live.pop(old)
                    check(pointer not in live,'Debug heap exclusive ownership');live[pointer]=amount;heap_bytes+=amount;heap_peak=max(heap_peak,heap_bytes)
                elif action in (3,4) and pointer:
                    check(pointer in live,'Debug exact heap free lifetime')
                    if pointer in live:heap_bytes-=live.pop(pointer)
            if frame['kind']!=1:
                for key in ('storage','heap','identity','lifetime'):
                    for event in frame[key]:check(event['epoch']==frame['epoch'] and event['update']==0 and event['phase']=='Lifetime' and event['substep']==0xffffffff and event['worker']!=0,'Debug lifetime context ownership')
                continue
            key=(frame['epoch'],frame['update']);check(key in on,'raw observer frame joins physical state')
            if key not in on:continue
            case,substeps,physical=on[key];check(frame['substeps']==substeps and not frame['failure'] and frame['worker_threads']<=3,'Debug actual substeps workers and no observer failure')
            check(len(frame['work'])<=1776 and len(frame['phase'])<=960 and len(frame['point'])<=256 and len(frame['ccd'])<=4,'Debug source-derived record cardinality')
            for section in ('point','ccd','lifetime','work','phase','storage','heap','identity'):
                for event in frame[section]:check(event['world']!=0 and event['epoch']==frame['epoch'] and event['update']==frame['update'] and event['substep']<substeps and event['worker']!=0 and event['phase'] not in ('None','Lifetime'),'Debug worker/phase/world/update ownership',key)
            stack=[];usage=joint=0;joint_peak=0;joint_calls=joint_aligned=0;used=set()
            named=[e for e in frame['storage'] if e['site']!='Unknown' and e['value'][-1]!=2]
            for event in [e for e in frame['storage'] if e['site']=='Unknown' and e['value'][-1]!=2]:
                pointer,requested,aligned,after,count,action=event['value'];check(aligned==(requested+15)//16*16 and count==1,'Debug actual Arena alignment')
                matches=[i for i,e in enumerate(named) if i not in used and e['value'][:3]==event['value'][:3] and e['value'][-1]==action and e['substep']==event['substep'] and e['worker']==event['worker']]
                check(bool(matches),'Debug complete actual Arena site ownership')
                site=named[matches[0]]['site'] if matches else 'Unknown'
                if matches:used.add(matches[0])
                owned=site in ('ActiveConstraints','IslandJointLinks','IslandJointIslands','IslandJointIslandEnds')
                if action==0:
                    reserve_calls+=1;reserve_bytes+=aligned
                    if requested:
                        check(pointer and pointer%16==0 and all(x[0]!=pointer for x in stack),'Debug Arena exclusive aligned pointer')
                        stack.append((pointer,requested,aligned,site));usage+=aligned
                        if owned:joint+=aligned;joint_calls+=1;joint_aligned+=aligned
                elif action==1:
                    release_calls+=1;release_bytes+=aligned
                    if requested:
                        check(bool(stack) and stack[-1]==(pointer,requested,aligned,site),'Debug Arena exact LIFO lifetime')
                        if stack:stack.pop()
                        usage-=aligned
                        if owned:joint-=aligned
                else:check(False,'Debug Arena action')
                check(usage==after and 0<=usage<=cap,'Debug source-derived combined Arena bound')
                arena_peak=max(arena_peak,usage);joint_peak=max(joint_peak,joint)
            check(not stack and usage==0 and joint==0 and len(used)==len(named),'Debug no leaked/double/unclassified Arena operation')
            check(joint_peak<=208 and joint_calls<=1+3*substeps and joint_aligned<=64+substeps*144,'Debug frozen eight-joint storage bound')
            for event in frame['heap']:
                if event['value'][-1] in (0,1,2):
                    contact=event['phase'].startswith('Contact') or event['phase'] in ('CCDPreparation','CCDRejection','CCDAcceptance') or event['site']=='ContactConstraints'
                    check(contact or id(event) in nonjoint,'Debug zero joint/shared-unattributed steady heap requests',event)
            if case in (101,102,116):check(physical['joint_count']==8,'eight actual concurrent joint states')
        check(not live and heap_bytes==0,'Debug all observed heap lifetimes retired')
        check(reserve_bytes==release_bytes and arena_peak<=cap,'Debug source-derived bounded Arena final balance')
        check(meta['metrics']['heap_live_bytes']==0 and meta['metrics']['unmatched_lifetime_frees']==0,'Debug native heap lifetime agreement')
        # Expected rejection probes are a separate stream, never Physics updates.
        sl=meta['synthetic_layout'];assert sl['schema']==1 and sl['frame_header_bytes']==48 and sl['work_bytes']==sl['failure_bytes']==48
        raw=(directory/'data'/sl['file']).read_bytes();at=0;synthetic=[]
        while at<len(raw):
            assert at+48<=len(raw);magic,schema,case,tick,workers,valid,overflow,invalid,point_count,wc,fc=struct.unpack_from('<Q10I',raw,at);at+=48
            assert magic==0x3159533632504850 and schema==1 and wc<=meta['jolt_observation_layout']['work_capacity'] and fc<=64
            assert at+wc*52+fc*52<=len(raw)
            work_rows=[struct.unpack_from('<3QIHH4I',raw,at+i*48) for i in range(wc)];at+=wc*48
            ids=struct.unpack_from('<'+str(wc)+'I',raw,at);at+=wc*4
            errors=[struct.unpack_from('<3QIHHIIQ',raw,at+i*48) for i in range(fc)];at+=fc*48+fc*4
            synthetic.append(case)
            if case==1:check(valid==1 and not overflow and not invalid and wc==2 and fc==0 and workers==2 and len(set(ids))==2 and sorted(x[7] for x in work_rows)==[2016,6112] and all(x[8:]==(64,64) for x in work_rows),'synthetic two-worker publication preserves all accumulated fields')
            elif case==2:check(valid==0 and overflow==1 and point_count==meta['jolt_observation_layout']['point_capacity']+1,'synthetic overflow rejection')
            elif case in (3,4,5):check(valid==0 and any(x[6]=={3:2,4:0,5:3}[case] for x in errors),'synthetic precise late/owner/substep rejection',case)
            elif case==6:check(valid==1 and not overflow and not invalid and point_count==wc==fc==0,'synthetic clean reset')
        check(synthetic==list(range(1,7)) and at==len(raw),'six separately labeled zero-update synthetic outcomes')
        metrics={'raw_update_frames':len(updates),'physical_parity_frames':len(parity),'synthetic_outcomes':synthetic,
            'arena_peak_bytes':arena_peak,'arena_source_bound_bytes':cap,'arena_reserve_calls':reserve_calls,'arena_release_calls':release_calls,
            'arena_reserved_aligned_bytes':reserve_bytes,'arena_released_aligned_bytes':release_bytes,'heap_live_bytes':heap_bytes,'heap_peak_bytes':heap_peak,'nonjoint_heap_ownership':heap_ownership}
    except Exception as error:
        failures.append({'gate':'raw observer schema and admission','detail':type(error).__name__+': '+str(error)})
    result={'status':'PASS' if not failures else 'FAIL','failures':failures,'metrics':metrics,
        'evidence':{p.name:sha(p) for p in (directory/'data').iterdir() if p.is_file()},'evaluator_sha256':sha(Path(__file__))}
    write(directory/'observer-raw-audit-01.json',result);print(json.dumps(result),flush=True);return result

def observation_jolt_audit(config,mode,case):
    """Independent ownership/lifetime audit of actual typed SDK records."""
    observation_authority();assert config=='Release' and case in ('C4','C8','C16','G8') and mode in ('full','minimal')
    directory=OBS/f'run-{config}-{case}-{mode}-01';assert json.loads((directory/'result.json').read_text())['status']=='PASS'
    numeric=json.loads((directory/'audit-01.json').read_text());assert numeric['status']=='PASS'
    meta,frames,enums=observation_frames(directory);_,samples=observation_decoded(directory)
    identity=json.loads((OBS/f'build-{config}-01/identity.json').read_text());cap=identity['pre_run_source_formula_caps']['canonical_bytes']
    bounds=json.loads((ROOT/json.loads((OBS_ACTIVE/'PHASE_26_JOLT_ARENA_POLICY_AMENDMENT_01.json').read_text())['source_derived_bounds']['path']).read_text())
    failures=[]
    def check(ok,gate,tick=None,detail=None):
        if not ok and not any(x['gate']==gate for x in failures):failures.append({'gate':gate,'first_tick':tick,'detail':detail})
    nonjoint,heap_ownership=observation_nonjoint_heap_admission(meta,frames,identity,{(f['epoch'],f['update']):meta['body_count'] for f in frames if f['update']},
        lambda ok,gate,detail=None:check(ok,gate,detail=detail))
    live_heap={};live_bytes=0;heap_peak=0;life_heap_calls=heap_requests=free_calls=free_bytes=0
    managed={};capacity={};identity_at_update={};heap_at_update={}
    for frame in frames:
        tick=frame['update'];check(not frame['failure'],'no recorded observation failure in any lifetime',tick)
        if not tick:
            for key in ('point','ccd','lifetime','work','phase','storage','heap','failure','identity'):
                for event in frame[key]:
                    check(event['epoch']==frame['epoch'] and event['update']==0 and event['phase']=='Lifetime' and event['substep']==0xffffffff and event['worker']!=0,
                        'construction interstice teardown record context',detail={'kind':key,'event':event})
        for event in frame['heap']:
            pointer,old,amount,old_amount,action=event['value']
            check(action in enums['HeapAction'],'heap lifetime action identifier',tick)
            if not tick:check(event['update']==0 and event['phase']=='Lifetime','construction/teardown heap lifetime context',tick)
            def release(address):
                nonlocal live_bytes,free_bytes
                if not address:return
                check(address in live_heap,'heap free has exact observed allocation',tick,address)
                if address in live_heap:
                    size=live_heap.pop(address)[0];live_bytes-=size;free_bytes+=size
            if action in (0,1,2):
                life_heap_calls+=1;heap_requests+=amount
                if pointer:
                    if action==1:
                        check(not old or old not in live_heap or live_heap[old][0]==old_amount,'reallocation old requested size',tick)
                        release(old)
                    check(pointer not in live_heap,'heap concurrent pointer ownership',tick,pointer)
                    live_heap[pointer]=(amount,event['site']);live_bytes+=amount;heap_peak=max(heap_peak,live_bytes)
                elif action==1 and amount==0 and old:check(False,'ambiguous zero-size reallocation lifetime',tick)
            elif action in (3,4):free_calls+=1;release(pointer)
        for event in frame['storage']:
            if event['value'][-1]==2:
                p,requested,aligned,_,count,_=event['value'];record=capacity.setdefault(event['site'],{'requested_bytes':0,'aligned_bytes':0,'count':0})
                for key,value in [('requested_bytes',requested),('aligned_bytes',aligned),('count',count)]:record[key]=max(record[key],value)
        for event in frame['identity']:
            sdk,index,generation,registry,action=event['value'];key=(event['world'],sdk);handle=(index,generation,registry)
            check(action in enums['IdentityAction'] and generation>0 and registry>0,'full SDK managed identity record',tick)
            name=enums['IdentityAction'].get(action,'INVALID')
            if name=='Create':
                check(key not in managed,'SDK full BodyID creation uniqueness',tick);managed[key]=handle
            elif name=='Destroy':
                check(managed.get(key)==handle,'exact BodyID retirement mapping',tick)
                managed.pop(key,None)
            elif name in ('Validate','Snapshot'):
                check(managed.get(key)==handle,'current BodyID generation registry mapping',tick)
        if tick:
            identity_at_update[tick]=dict(managed);heap_at_update[tick]={'live_bytes':live_bytes,'peak_bytes':heap_peak}
    check(not live_heap and live_bytes==0,'all observed heap allocations retired without leaks')
    check(not managed,'all managed SDK body identities retired')
    layout=meta['jolt_observation_layout']
    for key,value in [('heap_live_bytes',live_bytes),('heap_peak_bytes',heap_peak),('heap_allocation_calls',life_heap_calls),
        ('heap_allocation_bytes',heap_requests),('heap_free_calls',free_calls),('heap_free_bytes',free_bytes)]:
        check(layout.get(key)==value,'raw heap lifetime summary agreement '+key,detail={'raw':value,'reported':layout.get(key)})
    updates=[f for f in frames if f['update']];lifetime=[f for f in frames if not f['update']]
    check([f['update'] for f in updates]==list(range(1,1201)),'complete ordered Jolt update export')
    check(bool(frames) and frames[0]['kind']==0 and frames[-1]['kind']==3 and all(f['kind']==1 for f in updates),'construction and teardown lifetime export')
    check(meta['jolt_observation_layout']['recorder_valid'] and meta['jolt_observation_layout']['sink_detached'],'recorder and sink teardown')
    check(meta['total_recorder_bytes']<=8388608,'combined recorder storage')
    joint_sites={'ActiveConstraints','IslandJointLinks','IslandJointIslands','IslandJointIslandEnds'}
    shared_sites={'IslandSort'}
    allowed_sites={'UpdateSteps','BodyPairs','ContactConstraints','ContactIndexMap','IslandContactLinks',
        'IslandBodyIslands','IslandBodyIslandEnds','IslandContactIslands','IslandContactIslandEnds',
        'IslandPositionSteps','CCD'}|joint_sites|shared_sites
    peaks={'combined':0,'joint':0,'shared_sort':0};totals={'joint_reservations':0,'joint_requested':0,'joint_aligned':0,
        'joint_releases':0,'joint_heap_calls':0,'joint_heap_bytes':0,'shared_unattributed_heap_calls':0,
        'all_heap_calls':0,'all_heap_bytes':0,'accepted_discrete_points':0,'ccd_candidates':0,'ccd_accepted':0,'ccd_rejected':0}
    phase_totals={};work_totals={};per_update=[];worlds=set();workers=set();parity=[];cache_history=set();retirements=[]
    for frame in updates:
        tick=frame['update'];s=frame['substeps'];check(s==1,'canonical collision-substep identity',tick)
        check(frame['worker_threads']<=3,'source-bound owner plus two SDK workers',tick)
        check(not frame['failure'],'no SDK observer failures',tick)
        allrecords=[e for key in ('point','ccd','lifetime','work','phase','storage','heap','failure','identity') for e in frame[key]]
        for event in allrecords:
            boundary=event['substep']==0xffffffff and event['phase']=='SharedStepWork' and event in frame['heap']+frame['storage']+frame.get('identity',[])
            check(event['world']!=0 and event['epoch']==frame['epoch'] and event['update']==tick and (0<=event['substep']<s or boundary),
                'exact SDK world epoch update substep ownership',tick)
            check(event['worker']!=0 and event['phase'] not in ('None','Lifetime'),'worker phase ownership',tick)
            worlds.add(event['world']);workers.add(event['worker'])
        check(len({x['worker'] for x in allrecords})==frame['worker_threads'],'actual worker participation',tick)
        work=[[0,0,0] for _ in enums['WorkKind']]
        for event in frame['work']:
            kind,count,changed,iterations=event['value'];check(kind in enums['WorkKind'],'actual work identifier',tick)
            if kind in enums['WorkKind']:
                for j,value in enumerate((count,changed,iterations)):work[kind][j]+=value
        check(tuple(x for item in work for x in item)==samples[tick]['work'],'raw executed work aggregate agreement',tick)
        named_work={name:work[k] for k,name in enums['WorkKind'].items()}
        for name,value in named_work.items():
            target=work_totals.setdefault(name,[0,0,0])
            for j,v in enumerate(value):target[j]+=v
        phases={}
        for event in frame['phase']:
            kind,calls,ns=event['value'];check(kind in enums['Phase'] and calls>0,'actual phase timing record',tick)
            name=enums['Phase'].get(kind,'INVALID');value=phases.setdefault(name,[0,0]);value[0]+=calls;value[1]+=ns
            total=phase_totals.setdefault(name,[0,0]);total[0]+=calls;total[1]+=ns
        for work_name,phase_name in [('JointFilterDispatch','JointFilter'),('JointGraphDispatch','JointGraph'),
            ('JointSortItem','JointSort'),('JointSetupDispatch','JointPreparation'),('JointWarmDispatch','JointWarmStart'),
            ('JointVelocityDispatch','JointVelocitySolve'),('JointPositionDispatch','JointPositionSolve'),
            ('ContactCachedDispatch','ContactCachedPreparation'),('ContactNewDispatch','ContactNewPreparation'),
            ('ContactWarmDispatch','ContactWarmStart'),('ContactVelocityDispatch','ContactVelocitySolve'),
            ('ContactPositionDispatch','ContactPositionSolve'),('ContactPublication','ContactPublication')]:
            check(not named_work.get(work_name,[0])[0] or phases.get(phase_name,[0])[0]>0,'phase coverage '+phase_name,tick)
        # A physical TempAllocator operation is counted once. Its separately
        # emitted SDK site record supplies ownership and is matched one-to-one.
        actual=[e for e in frame['storage'] if e['site']=='Unknown' and e['value'][-1]!=2]
        named=[e for e in frame['storage'] if e['site']!='Unknown' and e['value'][-1]!=2]
        n=meta['links'];u=identity['layout']['Step'];c=identity['layout']['contact_max'];ccd_size=identity['layout']['CCDBody']
        site_bounds={'UpdateSteps':(16*u,1),'BodyPairs':(131072,1),'ActiveConstraints':(8*n,1),
            'ContactConstraints':(16384*c,1),'ContactIndexMap':(65536,1),'IslandContactLinks':(65536,1),
            'IslandJointLinks':(4*n,1),'IslandBodyIslands':(4*n,1),'IslandBodyIslandEnds':(4*(n+1),1),
            'IslandJointIslands':(4*n,1),'IslandJointIslandEnds':(4*(n+1),1),
            'IslandContactIslands':(65536,1),'IslandContactIslandEnds':(4*(n+1),1),
            'IslandSort':(4*n,2),'IslandPositionSteps':(n,1),'CCD':(n*max(ccd_size,8),3)}
        for site,(maximum,multiplicity) in site_bounds.items():
            reserved=[e for e in named if e['site']==site and e['value'][-1]==0]
            check(len(reserved)<=multiplicity and all(e['value'][1]<=maximum for e in reserved),'source-derived site bound '+site,tick)
        site_widths={'UpdateSteps':(u,),'BodyPairs':(8,),'ActiveConstraints':(8,),'ContactConstraints':(c,),
            'ContactIndexMap':(4,),'IslandContactLinks':(4,),'IslandJointLinks':(4,),'IslandBodyIslands':(4,),
            'IslandBodyIslandEnds':(4,),'IslandJointIslands':(4,),'IslandJointIslandEnds':(4,),
            'IslandContactIslands':(4,),'IslandContactIslandEnds':(4,),'IslandSort':(4,),'IslandPositionSteps':(1,),'CCD':(ccd_size,4,8)}
        for event in named:
            pointer,raw,rounded,_,count,action=event['value']
            check(rounded==(raw+15)//16*16 and any(raw==count*width for width in site_widths.get(event['site'],())),
                'named Arena exact item count and alignment',tick,event)
        used=set();stack=[];usage=0;joint=0;shared=0;peak=0;jpeak=0;speak=0;calls=0;requested=0;aligned=0;releases=0
        for event in actual:
            pointer,raw,rounded,after,count,action=event['value']
            check(count==1 and action in (0,1) and rounded==(raw+15)//16*16,'actual Arena request alignment',tick)
            matches=[i for i,e in enumerate(named) if i not in used and e['value'][0:3]==event['value'][0:3] and
                e['value'][-1]==action and e['substep']==event['substep'] and e['worker']==event['worker']]
            # Reused pointers can have repeated equal sizes. Consume the first
            # matching site in recorded order; its matching release must agree.
            if not matches:
                check(False,'unattributed Arena operation',tick,event);site='Unknown'
            else:
                index=matches[0];used.add(index);site=named[index]['site']
                check(site in allowed_sites,'source-admitted Arena site',tick,site)
            if raw:
                check(pointer!=0 and pointer%16==0,'Arena pointer alignment',tick)
                if action==0:
                    check(all(x[0]!=pointer for x in stack),'Arena concurrent ownership',tick)
                    stack.append((pointer,raw,rounded,site));usage+=rounded
                    if site in joint_sites:joint+=rounded;calls+=1;requested+=raw;aligned+=rounded
                    if site in shared_sites:shared+=rounded
                elif action==1:
                    check(bool(stack) and stack[-1]==(pointer,raw,rounded,site),'Arena exact LIFO release lifetime',tick)
                    if stack:stack.pop()
                    usage-=rounded
                    if site in joint_sites:joint-=rounded;releases+=1
                    if site in shared_sites:shared-=rounded
            else:check(pointer==0,'zero-size Arena operation',tick)
            peak=max(peak,usage);jpeak=max(jpeak,joint);speak=max(speak,shared)
            check(usage==after and usage>=0 and joint>=0 and shared>=0,'Arena observed outstanding agreement',tick)
            check(usage<=cap and usage<=33554432,'source-derived combined Arena bound',tick)
        check(not stack and usage==joint==shared==0,'zero end-update Arena outstanding',tick)
        check(all(i in used for i,e in enumerate(named)),'Arena no duplicate or unmatched site event',tick)
        check(calls<=1+3*s and releases==calls,'joint Arena reservation/release source bound',tick)
        joint_bound=bounds['joint_owned']['canonical_one_substep_peak_and_total_bytes'][case]
        check(jpeak<=joint_bound and aligned<=joint_bound,'joint Arena byte source bound',tick)
        check(speak<=bounds['joint_or_contact_shared_sort']['canonical_bytes'][case],'shared sort source bound',tick)
        peaks['combined']=max(peaks['combined'],peak);peaks['joint']=max(peaks['joint'],jpeak);peaks['shared_sort']=max(peaks['shared_sort'],speak)
        for name,value in [('joint_reservations',calls),('joint_requested',requested),('joint_aligned',aligned),('joint_releases',releases)]:totals[name]+=value
        heap_calls=heap_bytes=joint_calls=joint_bytes=unresolved=0
        for event in frame['heap']:
            pointer,old,amount,old_bytes,action=event['value'];check(action in enums['HeapAction'],'heap action identifier',tick)
            if action in (0,1,2):
                heap_calls+=1;heap_bytes+=amount
                if event['phase'].startswith('Joint') or event['site'] in joint_sites|{'JointContainer','AdapterJointStorage','AdapterFilterStorage'}:
                    joint_calls+=1;joint_bytes+=amount
                elif not event['phase'].startswith('Contact') and event['phase'] not in ('CCDPreparation','CCDRejection','CCDAcceptance') and event['site']!='ContactConstraints' and id(event) not in nonjoint:
                    unresolved+=1
        check(joint_calls==0 and joint_bytes==0,'zero joint-induced steady HEAP calls and bytes',tick)
        check(unresolved==0,'no shared potentially joint-induced unresolved heap',tick)
        totals['all_heap_calls']+=heap_calls;totals['all_heap_bytes']+=heap_bytes;totals['joint_heap_calls']+=joint_calls
        totals['joint_heap_bytes']+=joint_bytes;totals['shared_unattributed_heap_calls']+=unresolved
        candidates={};accepted={};points=[]
        for event in frame['ccd']:
            body1,sub1,body2,sub2,*rest=event['value'];key=(event['substep'],body1)
            check(all(math.isfinite(x) for x in rest[:-1]),'finite CCD record',tick)
            check(body1!=body2 and rest[-1]>0,'CCD endpoint and manifold identity',tick)
            target=candidates if event['event']=='CCDCandidate' else accepted if event['event']=='CCDAccepted' else None
            check(target is not None,'CCD event class',tick)
            if target is not None:check(key not in target,'unique candidate or accepted CCD identity',tick);target[key]=event
        for key,event in accepted.items():
            check(key in candidates,'accepted CCD has prepared candidate',tick)
            if key in candidates:check(event['value'][:12]==candidates[key]['value'][:12] and event['value'][13]==candidates[key]['value'][13],'accepted CCD candidate geometry match',tick)
        seen=set()
        actual_witnesses=[];body_handles={b['handle']:i for i,b in enumerate(samples[tick]['bodies'])}
        def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
        def transform(body,local):
            raw=body['orientation'];q={key:raw[offset//4] for key,offset in meta['quaternion_layout'].items()}
            xyz=(q['x'],q['y'],q['z']);uv=cross(xyz,local);uuv=cross(xyz,uv)
            return tuple(body['center_of_mass'][j]+(local[j]+(uv[j]*q['w']+uuv[j])*2.) for j in range(3))
        candidate_ordinals={};response_counts={};discrete_ordinals={}
        for event in frame['point']:
            b1,s1,b2,s2,ordinal,space,*values=event['value'];key=(event['substep'],b1,s1,b2,s2,ordinal,event['event'])
            check(key not in seen,'unique solved point identity',tick);seen.add(key)
            check(b1!=b2 and space in enums['AnchorSpace'] and all(math.isfinite(v) for v in values),'finite point and endpoint identity',tick)
            if event['event']=='Point':
                check(values[-1]>=0,'nonnegative actual discrete normal lambda',tick)
                totals['accepted_discrete_points']+=1
                check(space==0,'discrete COM-local anchor space',tick)
                discrete_ordinals.setdefault((event['substep'],b1,s1,b2,s2),[]).append(ordinal)
            else:
                ckey=(event['substep'],b1);candidate=candidates.get(ckey)
                check(event['event']=='CCDManifoldPoint' and candidate is not None,'CCD candidate manifold provenance',tick)
                if candidate is not None:
                    check((b1,s1,b2,s2)==candidate['value'][:4],'CCD manifold exact subshape endpoints',tick)
                    if space==1:
                        check(ordinal<candidate['value'][13],'CCD prepared manifold ordinal',tick)
                        candidate_ordinals.setdefault(ckey,[]).append(ordinal)
                    else:
                        check(space==0 and ordinal==0xffffffff and ckey in accepted,'CCD accepted impact-local response identity',tick)
                        response_counts[ckey]=response_counts.get(ckey,0)+1
                        if ckey in accepted:check(tuple(values[9:12])==accepted[ckey]['value'][9:12] and values[-1]==accepted[ckey]['value'][12],
                            'CCD actual response normal and lambda correspondence',tick)
            solved=event['event']=='Point' or event['event']=='CCDManifoldPoint' and space==0 and ordinal==0xffffffff
            if solved:
                if event['event']=='CCDManifoldPoint':check((event['substep'],b1) in accepted,'CCD impact-local response requires actual acceptance',tick)
                mapping=identity_at_update[tick];ha=mapping.get((event['world'],b1));hb=mapping.get((event['world'],b2))
                check(ha in body_handles and hb in body_handles,'accepted witness authoritative managed endpoints',tick)
                if ha in body_handles and hb in body_handles:
                    ia,ib=body_handles[ha],body_handles[hb];a=transform(samples[tick]['bodies'][ia],values[:3]);b=transform(samples[tick]['bodies'][ib],values[3:6])
                    n=values[9:12];depth=((a[0]-b[0])*n[0]+(a[1]-b[1])*n[1])+(a[2]-b[2])*n[2]
                    actual_witnesses.append((depth,ia,ib))
            points.append((event['substep'],event['event'],*event['value']))
        for key,candidate in candidates.items():check(sorted(candidate_ordinals.get(key,[]))==list(range(candidate['value'][13])),'all prepared CCD manifold points retained',tick)
        for key in accepted:check(response_counts.get(key)==1,'exactly one accepted CCD impact-local response',tick)
        for ordinals in discrete_ordinals.values():check(sorted(ordinals)==list(range(len(ordinals))),'all discrete manifold ordinals retained',tick)
        life_seen=set();retired_count=0
        # Cache events are source facts; an unobserved sleeping pair is never
        # fabricated from this history. Keep last-seen provenance separately.
        for event in frame['lifetime']:
            b1,s1,b2,s2,point_count,flags,kind=event['value'];key=(b1,s1,b2,s2)
            check(kind in enums['LifeKind'] and b1!=b2 and point_count<=4,'legal contact cache lifecycle record',tick)
            name=enums['LifeKind'].get(kind,'INVALID');unique=(event['substep'],key,name)
            check(unique not in life_seen,'unique cache lifecycle event',tick);life_seen.add(unique)
            if name in ('ManifoldAdded','ManifoldPersisted','CCDCallbackOnly'):cache_history.add(key)
            elif name=='RetiredUnpersisted':
                retired_count+=1;check(key in cache_history,'retired cache has observed provenance',tick)
                endpoints=[identity_at_update[tick].get((event['world'],sdk)) for sdk in (b1,b2)]
                bodies=[samples[tick]['bodies'][body_handles[h]] for h in endpoints if h in body_handles]
                classification='invalid_endpoint' if len(bodies)!=2 else 'live_inactive_endpoints' if all(b['type']==meta['identity_layout']['static_type'] or b['sleeping'][0] for b in bodies) else 'live_active_endpoint'
                retirements.append({'tick':tick,'substep':event['substep'],'key':key,'cached_points':point_count,'source_reason':'RetiredUnpersisted','completed_endpoint_classification':classification})
        check(named_work.get('ContactRetirement',[0])[0]==retired_count,'actual retirement work and lifetime record agreement',tick)
        actual_witnesses.sort(key=lambda item:(item[1],item[2],item[0]))
        check(actual_witnesses==samples[tick]['witnesses'],'raw solved anchors completed-pose witness exact replay',tick)
        totals['ccd_candidates']+=len(candidates);totals['ccd_accepted']+=len(accepted);totals['ccd_rejected']+=len(candidates.keys()-accepted.keys())
        # Only scheduling batches are excluded. Every semantic operation,
        # result and iteration count remains an exact parity requirement.
        semantics={name:value for name,value in named_work.items() if name!='SolverBatch'}
        parity.append({'tick':tick,'points':sorted(points),'ccd':sorted((e['substep'],e['event'],*e['value']) for e in frame['ccd']),
            'lifetime':sorted((e['substep'],*e['value']) for e in frame['lifetime']),'work':semantics})
        per_update.append({'tick':tick,'joint_requested_bytes':requested,'joint_aligned_bytes':aligned,'joint_reservations':calls,
            'joint_releases':releases,'joint_outstanding_bytes':joint,'joint_high_water_bytes':jpeak,'combined_arena_peak_bytes':peak,
            'shared_sort_peak_bytes':speak,'heap_calls':heap_calls,'heap_bytes':heap_bytes,'joint_heap_calls':joint_calls,'joint_heap_bytes':joint_bytes,
            'heap_lifetime':heap_at_update[tick],'phase_worker_totals':phases})
    check(len(worlds)==1,'single canonical SDK world identity')
    check(not meta['contact'] or totals['accepted_discrete_points']>0,'G8 actual accepted contact witnesses')
    check(work_totals.get('JointSetupDispatch',[0])[0]>0 and work_totals.get('JointVelocityDispatch',[0])[0]>0,'actual joint solving observed')
    check(all(peaks[k]>=0 for k in peaks),'bounded occupancy accounting')
    result={'status':'PASS' if not failures else 'FAIL','case':case,'mode':mode,'failures':failures,'ticks':len(updates),
        'allocation_policy':'D26-JOLT-ARENA-01; joint HEAP calls/bytes zero, source-bounded 32MiB Arena reservations permitted',
        'totals':totals,'peaks_bytes':peaks,'source_derived_combined_bound_bytes':cap,'joint_bound_bytes':bounds['joint_owned']['canonical_one_substep_peak_and_total_bytes'][case],
        'actual_worker_count':len(workers),'work_totals':work_totals,'phase_worker_totals':phase_totals,'nonjoint_heap_ownership':heap_ownership,
        'construction_capacities':capacity,'heap_lifetime':{'live_bytes':live_bytes,'peak_bytes':heap_peak,
            'allocation_calls':life_heap_calls,'allocation_requested_bytes':heap_requests,'free_calls':free_calls,'freed_bytes':free_bytes},
        'timing_semantics':'Nested worker elapsed totals; not disjoint wall time. All probes and Arena management remain in inclusive Update.',
        'inputs':{p.relative_to(ROOT).as_posix():sha(p) for p in (directory/'data/result.json',directory/'data/samples.bin',directory/'data/jolt-observation.bin',directory/'audit-01.json',Path(__file__))}}
    write(directory/'jolt-per-update-01.json',per_update);write(directory/'jolt-parity-01.json',parity);write(directory/'jolt-retirements-01.json',retirements)
    result['exported_evidence_bytes']=sum(p.stat().st_size for p in directory.rglob('*') if p.is_file())
    check(result['exported_evidence_bytes']<=67108864,'per-process exported evidence cap')
    result['status']='PASS' if not failures else 'FAIL';write(directory/'jolt-audit-01.json',result)
    print(json.dumps(result),flush=True);return int(bool(failures))

def observation_summary():
    observation_authority();order=[('C4','full'),('C8','full'),('C16','full'),('G8','full'),('C16','minimal'),('G8','minimal')]
    audits={};numeric={};bindings={}
    for case,mode in order:
        directory=OBS/f'run-Release-{case}-{mode}-01'
        assert json.loads((directory/'result.json').read_text())['status']=='PASS'
        audits[case,mode]=json.loads((directory/'jolt-audit-01.json').read_text());assert audits[case,mode]['status']=='PASS'
        numeric[case,mode]=json.loads((directory/'audit-01.json').read_text());assert numeric[case,mode]['status']=='PASS'
        for name in ('result.json','audit-01.json','jolt-audit-01.json','jolt-parity-01.json'):bindings[(directory/name).relative_to(ROOT).as_posix()]=sha(directory/name)
    quantum=json.loads((OBS/'reference-hardware.json').read_text())['clock_quantum_ns'];assert 0<quantum<=1000
    comparisons={};failures=[]
    for case in ('C16','G8'):
        directories=[OBS/f'run-Release-{case}-{mode}-01' for mode in ('full','minimal')]
        invocations=[json.loads((d/'invocation.json').read_text()) for d in directories]
        assert invocations[0]['executable_sha256']==invocations[1]['executable_sha256']
        decoded=[observation_decoded(d) for d in directories];mismatches=[]
        for tick,(a,b) in enumerate(zip(decoded[0][1],decoded[1][1])):
            for key in ('bodies','errors','impulses','energy','fidelity','geometry','witness','error','asleep','contacts','non_ground_contacts','witnesses'):
                if a[key]!=b[key]:mismatches.append((tick,key))
        semantic=[json.loads((d/'jolt-parity-01.json').read_text()) for d in directories]
        for a,b in zip(*semantic):
            if a!=b:mismatches.append((a['tick'],'actual point/CCD/lifetime/semantic-work records'))
        costs=[numeric[case,mode]['costs']['inclusive'] for mode in ('full','minimal')]
        added={key:costs[0][key]-costs[1][key]+2*quantum for key in ('mean_ns','median_ns','p95_ns','max_rolling60_ns')}
        passed=not mismatches and all(added[k]<=1e9/6000 for k in ('mean_ns','median_ns')) and all(added[k]<=1e9/3000 for k in ('p95_ns','max_rolling60_ns'))
        if not passed:failures.append(case+' full/minimal parity or unchanged observer overhead gate')
        comparisons[case]={'status':'PASS' if passed else 'FAIL','semantic_mismatches':mismatches[:20],
            'full_minus_minimal_plus_2q_ns':added,'inclusive_statistics':costs,'same_executable_sha256':invocations[0]['executable_sha256']}
    result={'status':'PASS' if not failures else 'FAIL','failures':failures,'processes':6,'ticks':7200,'retries':0,
        'comparisons':comparisons,'clock_quantum_ns':quantum,'evidence':bindings,
        'D26_CPU_01':'OWNER_ACCEPTED +29.0 us paired microbenchmark residual; not repeated or relabeled',
        'qualified_reference_performance':'INCONCLUSIVE; goal-only','phase27_started':False}
    write(OBS/'qualification-summary-01.json',result);print(json.dumps(result),flush=True);return int(bool(failures))

RECOVERY_ROOT=ROOT/'logs/pre_editor/phase-26/qualification-observation-01/autonomous-recovery-01'
RECOVERY_ACTIVE=False
RECOVERY_PROVIDERS={}

def recovery_configure(candidate):
    """Select a fresh evidence namespace; this grants no additional runtime budget."""
    import re
    global OBS,OBS_RUNTIME,RECOVERY_ACTIVE,RECOVERY_PROVIDERS
    assert re.fullmatch(r'[0-9]{2}',str(candidate)) and int(candidate)>0
    authorization=json.loads((RECOVERY_ROOT/'owner-authorization.json').read_text())
    assert authorization['status']=='OWNER_ACCEPTED_AUTONOMOUS_PHASE26_RECOVERY'
    budget=authorization['budgets']
    assert budget['additional_Debug_build_attempts']==6 and budget['additional_Release_build_attempts']==4
    assert budget['prior_unused_Release_build_attempts']==1 and budget['build_seconds_each']==1200
    OBS=safe(RECOVERY_ROOT/('candidate-'+str(candidate)))
    OBS_RUNTIME=safe(ROOT/'runtime/tmp/phase26-jolt-observation-01-autonomous'/('candidate-'+str(candidate)))
    RECOVERY_ACTIVE=True;RECOVERY_PROVIDERS={}
    return authorization

def recovery_release_glad():
    """Consume the independently captured current provider; preserve older hashes."""
    directory=ROOT/'logs/pre_editor/phase-26/qualification-observation-01/debug-recovery-01/release-provider-admission-01'
    record=directory/'admission.json'
    assert sha(record)=='38268fb9e100a4140553da834f0206af47cc98f45790b0049c93f73eaf350406'
    admission=json.loads(record.read_text());assert admission['status']=='PASS_CURRENT_PROVIDER_PROVENANCE'
    for name,digest in admission['bindings'].items():assert sha(directory/name)==digest,name
    actual=json.loads((directory/'actual-inputs.json').read_text())['files']
    producing=json.loads((directory/'producing-records.json').read_text())['glad']
    required=set(producing['actual_read_inputs']+producing['librarian_or_linker_read_inputs'])
    required.update(admission['owned_glad_inputs']);required.update(admission['additional_actual_system_libraries'])
    folded={Path(p).as_posix().casefold():value['sha256'] for p,value in actual.items()}
    inputs={}
    for name in required:
        p=Path(name);digest=folded[p.as_posix().casefold()];assert sha(p)==digest,name;inputs[p.as_posix()]=digest
    for key in ('library','object'):
        item=admission[key];assert sha(Path(item['path']))==item['sha256'],key
    for name,item in json.loads((directory/'snapshot-manifest.json').read_text())['files'].items():
        assert sha(directory/name)==item['sha256'],name
    assert admission['one_TU_one_object_one_library_provenance'] and admission['all_actual_source_header_inputs_match_frozen_hashes']
    return {'status':'PASS_MATCHED_CURRENT_PROVIDER_REUSE','source_record':record.as_posix(),
        'source_record_sha256':sha(record),'inputs':inputs,'library':admission['library']['path'],
        'sha256':admission['library']['sha256'],'object':admission['object'],
        'historical_mismatch_preserved':admission['historical_mismatch_record'],
        'limits':admission['limits']}

def recovery_freeze(candidate,reason,predecessor='debug-recovery-01'):
    """Freeze only an explained correction; never refresh a used candidate."""
    import difflib
    recovery_configure(candidate);assert reason.strip(),'A demonstrated correction is required'
    assert not (OBS/'frozen-inputs.json').exists(),'Used candidate cannot be refreshed'
    first=ROOT/'logs/pre_editor/phase-26/qualification-observation-01/implementation-01'
    previous=ROOT/'logs/pre_editor/phase-26/qualification-observation-01/debug-recovery-01' if predecessor=='debug-recovery-01' else safe(RECOVERY_ROOT/predecessor)
    assert previous!=OBS and (previous/'frozen-inputs.json').is_file()
    OBS.mkdir(parents=True,exist_ok=True)
    for name in ('reproduction.json','observer-cardinality-02.json'):
        target=OBS/name
        if target.exists():assert target.read_bytes()==(first/name).read_bytes()
        else:
            with target.open('xb') as stream:stream.write((first/name).read_bytes())
    observation_authority()
    before=json.loads((previous/'frozen-inputs.json').read_text())
    allowed={str((ROOT/p).resolve()).casefold() for p in OBS_OWNED}|{str((OBS_SDK/p).resolve()).casefold() for p in OBS_SDK_MEMBERS}
    deltas=[]
    for name,digest in before['hashes'].items():
        p=Path(name);current=sha(p)
        if current!=digest:
            assert str(p.resolve()).casefold() in allowed,('Unclassified input drift',name)
            deltas.append({'path':name,'before':digest,'after':current})
    assert deltas,'Unchanged candidate is not an authorized retry'
    reproduction=json.loads((OBS/'reproduction.json').read_text());originals={x['path']:x['sha256'] for x in reproduction['source_members']}
    assert all(sha(ROOT/'external/jolt'/name)==digest for name,digest in originals.items())
    current_sdk={p.relative_to(OBS_SDK).as_posix():sha(p) for p in OBS_SDK.rglob('*') if p.is_file()}
    assert {name for name in set(originals)|set(current_sdk) if originals.get(name)!=current_sdk.get(name)}==set(OBS_SDK_MEMBERS)
    for name in OBS_OWNED:
        target=safe(OBS/'source-before'/name);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((first/'source-before'/name).read_bytes())
        target=safe(OBS/'source-frozen'/name);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((ROOT/name).read_bytes())
    for name in OBS_SDK_MEMBERS:
        target=safe(OBS/'source-frozen/diagnostic-sdk'/name);target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write((OBS_SDK/name).read_bytes())
    # Capture the exact admitted SDK overlay for this candidate without replacing its predecessors.
    patch=[];preimages={}
    for name in OBS_SDK_MEMBERS:
        old=ROOT/'external/jolt'/name;new=OBS_SDK/name
        a=old.read_bytes().replace(b'\r\n',b'\n') if old.exists() else None;b=new.read_bytes().replace(b'\r\n',b'\n');preimages[name]=a
        patch.append(f'diff --git a/{name} b/{name}\n')
        if a is None:patch.append('new file mode 100644\n')
        patch.extend(difflib.unified_diff((a or b'').decode().splitlines(keepends=True),b.decode().splitlines(keepends=True),fromfile='a/'+name if a is not None else '/dev/null',tofile='b/'+name))
    patch_bytes=''.join(patch).encode();spec=importlib.util.spec_from_file_location('recovery_patch',ROOT/'tools/setup_jolt.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    rebuilt=module.apply_text_patch(patch_bytes,preimages);assert all(rebuilt[name]==(OBS_SDK/name).read_bytes().replace(b'\r\n',b'\n') for name in OBS_SDK_MEMBERS)
    write(OBS/'diagnostic-overlay.patch',patch_bytes.decode())
    inputs=observation_inputs();inputs['hashes'][(RECOVERY_ROOT/'owner-authorization.json').as_posix()]=sha(RECOVERY_ROOT/'owner-authorization.json')
    inputs['hashes'][(OBS/'diagnostic-overlay.patch').as_posix()]=sha(OBS/'diagnostic-overlay.patch')
    glad=recovery_release_glad();inputs['hashes'].update(glad['inputs']);inputs['hashes'][glad['source_record']]=glad['source_record_sha256']
    # Bind actual default library providers already named by the ordinary producing linker records.
    for config in ('Debug','Release'):
        for log in (ROOT/f'bin-int/{config}/RigidBodySimulation').rglob('link.read.*.tlog'):
            for line in log.read_text(encoding='utf-16').splitlines():
                for name in line.lstrip('^').split('|'):
                    if len(name)>2 and name[1]==':' and name.lower().endswith('.lib'):
                        p=Path(name)
                        if not p.resolve().is_relative_to(ROOT/'bin'):inputs['hashes'][p.as_posix()]=sha(p)
    write(OBS/'frozen-inputs.json',inputs)
    write(OBS/'freeze.json',{'status':'FROZEN_CORRECTED_DIAGNOSTIC_SOURCE','reason':reason,'predecessor':previous.relative_to(ROOT).as_posix(),
        'predecessor_inputs_sha256':sha(previous/'frozen-inputs.json'),'inputs_sha256':sha(OBS/'frozen-inputs.json'),
        'owner_authorization_sha256':sha(RECOVERY_ROOT/'owner-authorization.json'),'source_deltas':deltas,
        'candidate_overlay_sha256':sha(OBS/'diagnostic-overlay.patch'),'source_bounds':'Original bounds and cardinality evidence retained; each correction must preserve the admitted meaning and limits.'})
    observation_token_proof()
    return 0

def recovery_capture_partial_provider(candidate,config,name):
    """Admit a completed library project without relabeling its failed parent build."""
    import re
    recovery_configure(candidate);assert name in ('Jolt','GEngine')
    build=OBS/f'build-{config}-01';result_path=build/'result.json';result=json.loads(result_path.read_text())
    assert result['status']=='FAIL' and result['inputs_unchanged'] and not result['timeout']
    inputs_path=build/'inputs.json';inputs=json.loads(inputs_path.read_text());known={Path(p).as_posix().casefold():h for p,h in inputs['hashes'].items()}
    library=OBS_RUNTIME/f'bin/{config}/{name}/{name}.lib';assert result['products'].get(library.as_posix())==sha(library)
    destination=safe(build/('partial-provider-'+name));assert not destination.exists()
    project=ROOT/'GEngine/GEngine.vcxproj' if name=='GEngine' else build/'Jolt.vcxproj'
    log=(build/'build.log').read_text(errors='replace');success=[line for line in log.splitlines() if 'Done Building Project "'+str(project)+'"' in line and '(Build target(s)).' in line and 'FAILED' not in line]
    assert success,('Library project did not complete',name)
    assert not any(w['owner']=='first_party_or_link' and (str(project).lower() in w['line'].lower()) for w in result['warnings']),('Project warning gate',name)
    object_root=OBS_RUNTIME/f'bin-int/{config}/{name}';commands=list(object_root.rglob('CL.command.1.tlog'));assert len(commands)==1
    command=commands[0].read_text(encoding='utf-16');upper=command.upper()
    units={p.replace('\\','/').casefold() for line in command.splitlines() if line.startswith('^') for p in line[1:].split('|')}
    assert units=={p.casefold() for p in inputs['compilation_sources'][name]}
    assert '/D '+OBS_MACRO in upper and '/STD:C++23' in upper and '/W3' in upper
    assert ('/D JPH_NO_DEBUG' in upper)==(config=='Release')
    assert '/MTD' in upper if config=='Debug' else '/MT ' in upper and '/MTD' not in upper
    assert '/D GENGINE_JOLT_FINAL_AUDIT' not in upper and '/D GENGINE_G8_RESISTANCE_CHECKS' not in upper
    if name=='GEngine':assert '/D GENGINE_PHASE26_QUALIFICATION' in upper and '/D GE_ENABLE_PHYSICS_PROFILING' in upper
    dependencies={p.replace('\\','/').casefold() for path in commands[0].parent.glob('*.read.*.tlog') for line in path.read_text(encoding='utf-16').splitlines() for p in line.lstrip('^').split('|') if len(p)>2 and p[1]==':'}
    sources={p:known[p] for p in dependencies if Path(p).suffix.lower() in ('.h','.hpp','.hxx','.inl','.c','.cpp','.cxx')}
    assert all(sha(Path(p))==digest for p,digest in sources.items())
    assert not any(p.startswith((ROOT/'external/jolt').as_posix().casefold()+'/') for p in sources)
    objects={p.as_posix():sha(p) for p in object_root.rglob('*.obj')}
    data=library.read_bytes();assert data[:8]==b'!<arch>\n';cursor=8;members=[]
    while cursor<len(data):
        header=data[cursor:cursor+60];assert header[58:60]==b'`\n';label=header[:16].decode().strip();size=int(header[48:58]);body=data[cursor+60:cursor+60+size];cursor+=60+size+(size%2)
        if label not in ('/','//'):members.append(hashlib.sha256(body).hexdigest())
    assert len(objects)==len(units)==len(members) and sorted(objects.values())==sorted(members)
    tools={p:h for p,h in inputs['hashes'].items() if Path(p).name.lower() in ('cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','msbuild.exe')}
    assert tools and all(sha(Path(p))==h for p,h in tools.items())
    destination.mkdir();archive=safe(destination/'compiler-records');archive.mkdir()
    for p in commands[0].parent.glob('*.tlog'):
        with (archive/p.name).open('xb') as stream:stream.write(p.read_bytes())
    provider={'configuration':config,'library':library.as_posix(),'library_sha256':sha(library),'object_root':object_root.as_posix(),
        'compiler_records_directory':archive.as_posix(),'compiler_record_hashes':{p.as_posix():sha(p) for p in archive.glob('*.tlog')},
        'source_inputs':sources,'compilation_sources':inputs['compilation_sources'][name],'object_hashes':objects,
        'source_frozen_inputs':inputs_path.as_posix(),'source_frozen_inputs_sha256':sha(inputs_path),'toolchain_inputs':tools}
    if name=='Jolt':
        layout=next(p for p in object_root.rglob('*.obj') if p.stem.casefold()=='physicssystem');provider['layout_object']={'path':layout.as_posix(),'sha256':sha(layout)}
    record={'status':'PASS_PARTIAL_PROJECT_PROVENANCE','project':name,'configuration':config,
        'overall_build_status':'FAIL_PRESERVED','overall_build_result':{'path':result_path.as_posix(),'sha256':sha(result_path)},
        'build_log':{'path':(build/'build.log').as_posix(),'sha256':sha(build/'build.log')},'project_completion':success,
        'archive_members_equal_actual_objects':len(members),'actual_source_dependency_count':len(sources),'provider':provider,
        'scope':'This completed library may be reused only while its actual source/compiler/object/archive identities match. No RBS, runtime, overall-build or publication PASS is implied.'}
    assert sha(library)==provider['library_sha256'] and all(sha(Path(p))==h for p,h in sources.items()) and all(sha(Path(p))==h for p,h in objects.items())
    write(destination/'identity.json',record);print(json.dumps({'status':record['status'],'project':name,'members':len(members),'identity_sha256':sha(destination/'identity.json')}))
    return record

def recovery_find_provider(config,name,inputs):
    """Reuse a compiled library only against its actual source and compiler closure."""
    possible=[]
    for path in sorted(RECOVERY_ROOT.glob('candidate-*/build-'+config+'-01/identity.json'),reverse=True):
        identity=json.loads(path.read_text())
        if identity.get('status')!='PASS' or name not in identity.get('project_providers',{}):continue
        provider=dict(identity['project_providers'][name]);provider['identity_record']=path.as_posix();provider['identity_sha256']=sha(path)
        provider['source_frozen_inputs']=(path.parent/'inputs.json').as_posix()
        provider['source_frozen_inputs_sha256']=identity['frozen_inputs_sha256']
        if name=='Jolt':provider['layout_object']=identity['layout_object']
        possible.append(provider)
    for path in sorted(RECOVERY_ROOT.glob('candidate-*/build-'+config+'-01/partial-provider-'+name+'/identity.json'),reverse=True):
        identity=json.loads(path.read_text());assert identity['status']=='PASS_PARTIAL_PROJECT_PROVENANCE' and identity['overall_build_status']=='FAIL_PRESERVED'
        parent=identity['overall_build_result'];assert sha(Path(parent['path']))==parent['sha256'] and json.loads(Path(parent['path']).read_text())['status']=='FAIL'
        provider=dict(identity['provider']);provider['identity_record']=path.as_posix();provider['identity_sha256']=sha(path);possible.append(provider)
    if config=='Debug' and name=='Jolt':
        path=ROOT/'logs/pre_editor/phase-26/qualification-observation-01/debug-recovery-01/compile-failure-01/partial-sdk-identity.json'
        assert sha(path)=='e68e0dc0a65af5cb156f7fda3dbde6b5d2ddd4a9a9f2f9442a4014df0bb51451'
        identity=json.loads(path.read_text());object_root=(ROOT/identity['object']['path']).parent
        archived=path.parent/'compiler-records/Jolt/Jolt.tlog'
        assert archived.is_dir()
        expected_records={str(object_root/'Jolt.tlog'/p.name):sha(p) for p in archived.glob('*.tlog')}
        assert sha(ROOT/identity['compiler_command']['path'])==identity['compiler_command']['sha256']
        possible.append({'identity_record':path.as_posix(),'identity_sha256':sha(path),'configuration':'Debug',
            'library':(ROOT/identity['library']['path']).as_posix(),'library_sha256':identity['library']['sha256'],
            'compiler_records_directory':(object_root/'Jolt.tlog').as_posix(),'object_root':object_root.as_posix(),
            'source_inputs':identity['actual_source_dependencies'],'compilation_sources':json.loads((path.parents[1]/'frozen-inputs.json').read_text())['compilation_sources']['Jolt'],
            'source_frozen_inputs':(path.parents[1]/'frozen-inputs.json').as_posix(),
            'source_frozen_inputs_sha256':sha(path.parents[1]/'frozen-inputs.json'),
            'compiler_record_hashes':expected_records,'compiler_record_archive':archived.as_posix(),
            'object_hashes':identity['object_hashes'],'layout_object':{'path':(ROOT/identity['object']['path']).as_posix(),'sha256':identity['object']['sha256']}})
    current={Path(p).as_posix().casefold():h for p,h in inputs['hashes'].items()}
    for provider in possible:
        recovery_validate_provider(provider)
        if provider['configuration']!=config:continue
        if set(provider['compilation_sources'])!=set(inputs['compilation_sources'][name]):continue
        if any(current.get(Path(p).as_posix().casefold())!=digest or sha(Path(p))!=digest for p,digest in provider['source_inputs'].items()):continue
        before=json.loads(Path(provider['source_frozen_inputs']).read_text())['hashes']
        compiler_inputs={p:h for p,h in before.items() if Path(p).name.lower() in ('cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe','msbuild.exe')}
        if not compiler_inputs or any(current.get(Path(p).as_posix().casefold())!=h or sha(Path(p))!=h for p,h in compiler_inputs.items()):continue
        if sha(Path(provider['library']))!=provider['library_sha256']:continue
        commands=list(Path(provider['compiler_records_directory']).rglob('CL.command.1.tlog'));assert len(commands)==1
        text=commands[0].read_text(encoding='utf-16').upper()
        if '/D '+OBS_MACRO not in text or ('/D JPH_NO_DEBUG' in text)!=(config=='Release'):continue
        if '/STD:C++23' not in text or '/W3' not in text or ('/MTD' not in text if config=='Debug' else '/MT ' not in text or '/MTD' in text):continue
        if name=='GEngine' and ('/D GENGINE_PHASE26_QUALIFICATION' not in text or '/D GE_ENABLE_PHYSICS_PROFILING' not in text):continue
        assert {p.name for p in commands[0].parent.glob('*.tlog')}=={Path(p).name for p in provider['compiler_record_hashes']}
        provider['toolchain_inputs']=compiler_inputs
        return provider
    return None

def recovery_validate_provider(provider):
    assert sha(Path(provider['identity_record']))==provider['identity_sha256']
    assert sha(Path(provider['source_frozen_inputs']))==provider['source_frozen_inputs_sha256']
    assert sha(Path(provider['library']))==provider['library_sha256']
    for path,digest in provider['compiler_record_hashes'].items():
        assert sha(Path(path))==digest,('Reused compiler record drift',path)
        if 'compiler_record_archive' in provider:assert sha(Path(provider['compiler_record_archive'])/Path(path).name)==digest
    for path,digest in provider.get('object_hashes',{}).items():assert sha(Path(path))==digest,('Reused SDK object drift',path)
    if 'layout_object' in provider:
        item=provider['layout_object'];assert sha(Path(item['path']))==item['sha256'],'Reused SDK layout object drift'
    return True

def recovery_preflight_projects(config,d,inputs):
    import xml.etree.ElementTree as ET
    ns={'m':'http://schemas.microsoft.com/developer/msbuild/2003'};documents={name:ET.parse(d/name) for name in ('Jolt.vcxproj','isolated.props','projects.proj')}
    for name,path in [('Jolt',d/'Jolt.vcxproj'),('GEngine',ROOT/'GEngine/GEngine.vcxproj'),('RigidBodySimulation',ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj')]:
        tree=ET.parse(path);assert {x.attrib['Include'] for x in tree.findall('.//m:ProjectConfiguration',ns)}=={'Debug|x64','Release|x64'}
        units=sorted((path.parent/x.attrib['Include']).resolve().as_posix() for x in tree.findall('.//m:ClCompile',ns) if 'Include' in x.attrib)
        assert units==inputs['compilation_sources'][name] and all(Path(p).is_file() for p in units),name
        group=next(x for x in tree.findall('m:ItemDefinitionGroup',ns) if x.attrib.get('Condition')==f"'$(Configuration)|$(Platform)'=='{config}|x64'")
        assert group.findtext('m:ClCompile/m:LanguageStandard',namespaces=ns)=='stdcpp23'
        assert group.findtext('m:ClCompile/m:RuntimeLibrary',namespaces=ns)==('MultiThreadedDebug' if config=='Debug' else 'MultiThreaded')
    props=documents['isolated.props'];assert props.find('.//m:ProjectReference',ns).attrib=={'Remove':'@(ProjectReference)'}
    assert props.findtext('.//m:AdditionalIncludeDirectories',namespaces=ns)==str(OBS_SDK)
    assert props.findtext('.//m:OutDir',namespaces=ns)==str(OBS_RUNTIME/f'bin/{config}')+'/$(MSBuildProjectName)/'
    assert props.findtext('.//m:IntDir',namespaces=ns)==str(OBS_RUNTIME/f'bin-int/{config}')+'/$(MSBuildProjectName)/'
    assert props.findtext('.//m:PreBuildEventUseInBuild',namespaces=ns)=='false' and props.findtext('.//m:PostBuildEventUseInBuild',namespaces=ns)=='false'
    text=(d/'isolated.props').read_text();assert OBS_MACRO in text and 'GENGINE_PHASE26_QUALIFICATION' in text and 'GE_ENABLE_PHYSICS_PROFILING' in text
    assert ('GENGINE_G8_RESISTANCE_CHECKS' in text)==(config=='Debug') and ('JPH_NO_DEBUG' in text)==(config=='Release')
    projects=documents['projects.proj'];assert projects.find('.//m:MSBuild',ns).attrib['StopOnFirstFailure']=='true'
    expected=[str(path) for name,path in [('Jolt',d/'Jolt.vcxproj'),('GEngine',ROOT/'GEngine/GEngine.vcxproj'),('RigidBodySimulation',ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj')] if name not in RECOVERY_PROVIDERS]
    assert [x.attrib['Include'] for x in projects.findall('.//m:CurrentProject',ns)]==expected
    return {'status':'PASS_STATIC_PROJECT_PREFLIGHT','generated_documents':{name:sha(d/name) for name in documents},
        'project_order':expected,'reused_projects':sorted(RECOVERY_PROVIDERS),'inputs_sha256':sha(OBS/'frozen-inputs.json')}

def recovery_assert_containment():
    """An unresolved timed-out child cannot be hidden by a fresh candidate."""
    for candidate in RECOVERY_ROOT.glob('candidate-*'):
        for timeout in candidate.glob('*/timeout.json'):
            if not timeout.parent.name.startswith(('build-','run-')):continue
            termination=timeout.parent/'termination.json'
            assert termination.is_file(),('Timeout containment evidence missing; no further process admitted',str(timeout))
            record=json.loads(termination.read_text())
            assert record.get('timeout_sha256')==sha(timeout) and record.get('parent_reaped') is True and record.get('contained') is True,('Timeout containment unproven; no further process admitted',str(termination))
            if record.get('tree_termination_required'):assert record.get('taskkill_exit_code')==0,('Build worker containment unproven',str(termination))


def recovery_build(candidate,config):
    import re
    global RECOVERY_PROVIDERS
    authority=recovery_configure(candidate);recovery_assert_containment();observation_authority();inputs=json.loads((OBS/'frozen-inputs.json').read_text());observation_assert_inputs(inputs)
    proof=json.loads((OBS/'ordinary-token-proof.json').read_text());assert proof['status']=='PASS' and proof['frozen_inputs_sha256']==sha(OBS/'frozen-inputs.json')
    if config=='Release':assert json.loads((OBS/'build-Debug-01/identity.json').read_text())['status']=='PASS'
    d=safe(OBS/f'build-{config}-01');assert not d.exists(),'Each candidate/configuration is create-only; explain and freeze a new correction'
    previous_attempts=list(RECOVERY_ROOT.glob('candidate-*/build-'+config+'-01/process-started.json'))
    limit=authority['budgets']['additional_Debug_build_attempts'] if config=='Debug' else authority['budgets']['prior_unused_Release_build_attempts']+authority['budgets']['additional_Release_build_attempts']
    assert len(previous_attempts)<limit,('Authorized infrastructure build ceiling exhausted',config,limit)
    RECOVERY_PROVIDERS={name:provider for name in ('Jolt','GEngine') if (provider:=recovery_find_provider(config,name,inputs)) is not None}
    d.mkdir();out,inter=observation_projects(config,d);preflight=recovery_preflight_projects(config,d,inputs)
    write(d/'preflight.json',preflight);write(d/'inputs.json',inputs);write(d/'provider-reuse.json',{'providers':RECOVERY_PROVIDERS})
    for name,provider in RECOVERY_PROVIDERS.items():
        target=safe(out/name/(name+'.lib'));target.parent.mkdir(parents=True,exist_ok=True)
        with target.open('xb') as stream:stream.write(Path(provider['library']).read_bytes())
        assert sha(target)==provider['library_sha256']
    command=[str(MSBUILD),str(d/'projects.proj'),'/t:Build',f'/p:Configuration={config}','/p:Platform=x64',
        '/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',
        f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}','/p:BuildProjectReferences=false','/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);write(d/'budget-before.json',{'attempt_under_standing_authority':len(previous_attempts)+1,
        'maximum_attempts_under_standing_authority':limit,'prior_historical_Debug_attempts':2,'seconds_max':1200,
        'reason':json.loads((OBS/'freeze.json').read_text())['reason'],'owner_authorization_sha256':sha(RECOVERY_ROOT/'owner-authorization.json')})
    observation_assert_inputs(inputs);start=time.monotonic();timed_out=False;termination=None
    print(f'RECOVERY {config} BUILD START candidate={candidate} reuse={sorted(RECOVERY_PROVIDERS)}',flush=True)
    with (d/'build.log').open('xb') as log:
        process=subprocess.Popen(command,cwd=ROOT,env=observation_environment(),stdout=log,stderr=subprocess.STDOUT)
        write(d/'process-started.json',{'pid':process.pid,'utc':datetime.datetime.now(datetime.timezone.utc).isoformat()})
        try:code=process.wait(timeout=max(0,start+1200-time.monotonic()))
        except subprocess.TimeoutExpired:
            timed_out=True;code,termination=observation_timeout_termination(process,d,1200,start,log,tree=True)
    elapsed=time.monotonic()-start;unchanged=all(Path(p).is_file() and sha(Path(p))==h for p,h in inputs['hashes'].items())
    reuse_violations=[]
    for name,provider in RECOVERY_PROVIDERS.items():
        try:recovery_validate_provider(provider)
        except (AssertionError,OSError) as error:reuse_violations.append({'project':name,'error':str(error)})
    warnings=[]
    for line in (d/'build.log').read_text(errors='replace').splitlines():
        if not re.search(r'\bwarning (C\d+|LNK\d+|MSB\d+)',line):continue
        normalized=line.replace('\\','/').lower();vendor=any(part in normalized for part in ('/include/external/','/external/','/vc/tools/msvc/','/windows kits/'))
        if any((OBS_SDK/name).as_posix().lower() in normalized for name in OBS_SDK_MEMBERS):vendor=False
        warnings.append({'line':line,'owner':'vendor_or_system' if vendor else 'first_party_or_link'})
    products={p.as_posix():sha(p) for p in out.rglob('*') if p.suffix.lower() in ('.lib','.exe')}
    expected=[out/'Jolt/Jolt.lib',out/'GEngine/GEngine.lib',out/'RigidBodySimulation/RigidBodySimulation.exe']
    passed=code==0 and not timed_out and elapsed<=1200 and unchanged and not reuse_violations and all(p.is_file() for p in expected) and not any(x['owner']=='first_party_or_link' for x in warnings)
    result={'status':'PASS' if passed else 'FAIL','configuration':config,'exit_code':code,'seconds':elapsed,'timeout':timed_out,
        'inputs_unchanged':unchanged,'products':products,'warnings':warnings,'attempt_under_standing_authority':len(previous_attempts)+1,
        'reused_projects':sorted(RECOVERY_PROVIDERS),'reuse_input_violations':reuse_violations,'generated_build_inputs':preflight['generated_documents'],'termination':termination}
    write(d/'result.json',result);print(json.dumps({k:v for k,v in result.items() if k!='warnings'}),flush=True)
    if passed:
        try:observation_identity(config)
        except Exception as error:
            import traceback
            write(d/'identity-error.json',{'status':'BLOCKED_IDENTITY_ADMISSION','type':type(error).__name__,'message':str(error),'traceback':traceback.format_exc()})
            raise
    return 0 if passed else 1

def recovery_assert_runtime_budget(config,case,mode):
    """Build recovery does not reset the separately admitted simulation envelope."""
    root=ROOT/'logs/pre_editor/phase-26/qualification-observation-01'
    starts=list(root.glob('**/run-*/process-started.json'))
    if config=='Debug':
        assert (case,mode)==('observer','correctness')
        assert not any(p.parent.name=='run-Debug-observer-correctness-01' for p in starts),'Observer process allowance already consumed'
    else:
        order=[('C4','full'),('C8','full'),('C16','full'),('G8','full'),('C16','minimal'),('G8','minimal')]
        assert (case,mode) in order
        assert not any(p.parent.name==f'run-Release-{case}-{mode}-01' for p in starts),'This qualification process already ran'
        assert sum(p.parent.name.startswith('run-Release-') for p in starts)<6,'Six-process qualification ceiling exhausted'

if __name__=='__main__':
    import sys
    if len(sys.argv)>1 and sys.argv[1].startswith('recovery-'):
        p=argparse.ArgumentParser();p.add_argument('action',choices=['recovery-freeze','recovery-build','recovery-identity','recovery-run','recovery-numeric-audit','recovery-jolt-audit','recovery-summary'])
        p.add_argument('--candidate',required=True);p.add_argument('--config',choices=['Debug','Release'],default='Debug')
        p.add_argument('--reason',default='');p.add_argument('--predecessor',default='debug-recovery-01');p.add_argument('--case',default='observer');p.add_argument('--mode',default='correctness');args=p.parse_args()
        if args.action=='recovery-freeze':raise SystemExit(recovery_freeze(args.candidate,args.reason,args.predecessor))
        if args.action=='recovery-build':raise SystemExit(recovery_build(args.candidate,args.config))
        recovery_configure(args.candidate)
        if args.action=='recovery-identity':observation_identity(args.config);raise SystemExit(0)
        if args.action=='recovery-run':
            recovery_assert_runtime_budget(args.config,args.case,args.mode);raise SystemExit(observation_run(args.config,args.case,args.mode))
        if args.action=='recovery-numeric-audit':raise SystemExit(observation_numeric_audit(args.config,args.mode,args.case))
        if args.action=='recovery-jolt-audit':raise SystemExit(observation_jolt_audit(args.config,args.mode,args.case))
        raise SystemExit(observation_summary())
    p=argparse.ArgumentParser();p.add_argument('action',choices=['build','run','audit','summarize','witness-compare','startup-build','startup-run','startup-audit','impulse-build','impulse-run','impulse-audit','g8-impulse-build','g8-impulse-run','sandbox-build','sandbox-run','material-build','material-run','visual-build','visual-run','jitter-build','jitter-run','contact-build','contact-run','contact-audit','contact-summary','observation-freeze','observation-token-proof','observation-build','observation-identity','observation-run','observation-numeric-audit','observation-jolt-audit','observation-summary']);p.add_argument('--config',choices=['Debug','Release'],required=True);p.add_argument('--attempt',default='01');p.add_argument('--case',default='C4');p.add_argument('--mode',default='debug');args=p.parse_args()
    if args.action.startswith('observation-'):
        assert args.attempt=='01','No observation retry authorized'
        if args.action=='observation-freeze':raise SystemExit(observation_freeze())
        if args.action=='observation-token-proof':raise SystemExit(observation_token_proof())
        if args.action=='observation-build':raise SystemExit(observation_build(args.config))
        if args.action=='observation-identity':observation_identity(args.config);raise SystemExit(0)
        if args.action=='observation-run':raise SystemExit(observation_run(args.config,args.case,args.mode))
        if args.action=='observation-jolt-audit':raise SystemExit(observation_jolt_audit(args.config,args.mode,args.case))
        if args.action=='observation-summary':raise SystemExit(observation_summary())
        raise SystemExit(observation_numeric_audit(args.config,args.mode,args.case))
    if args.action.startswith('contact-'):
        raise SystemExit(contact_build(args.config,args.mode,args.attempt) if args.action=='contact-build' else contact_run(args.config,args.case,args.attempt) if args.action=='contact-run' else contact_audit(args.config,args.mode,args.case,args.attempt) if args.action=='contact-audit' else contact_summary(args.attempt))
    if args.action.startswith('jitter-'):
        raise SystemExit(jitter_build(args.config,args.attempt) if args.action=='jitter-build' else jitter_run(args.config,args.case,args.attempt))
    if args.action.startswith('visual-'):
        raise SystemExit({'visual-build':visual_build,'visual-run':visual_run}[args.action](args.config,args.mode,args.attempt))
    if args.action.startswith('material-'):
        raise SystemExit({'material-build':material_build,'material-run':material_run}[args.action](args.config,args.attempt))
    if args.action.startswith('sandbox-'):
        raise SystemExit({'sandbox-build':sandbox_build,'sandbox-run':sandbox_run}[args.action](args.config,args.mode,args.attempt))
    if args.action.startswith('g8-impulse-'):
        raise SystemExit({'g8-impulse-build':g8_impulse_build,'g8-impulse-run':g8_impulse_run}[args.action](args.config))
    if args.action.startswith('impulse-'):
        raise SystemExit({'impulse-build':impulse_build,'impulse-run':impulse_run,'impulse-audit':impulse_audit}[args.action](args.config))
    if args.action.startswith('startup-'):
        raise SystemExit({'startup-build':startup_build,'startup-run':startup_run,'startup-audit':startup_audit}[args.action](args.config))
    raise SystemExit(witness_compare() if args.action=='witness-compare' else summarize() if args.action=='summarize' else build(args.config,args.attempt) if args.action=='build' else (run if args.action=='run' else audit)(args.config,args.mode,args.case,args.attempt))
