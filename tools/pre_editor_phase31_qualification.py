"""Build and assess the admitted Phase 31 Movers workload in the existing RBS harness."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

import pre_editor_phase29_step_safety as build_inputs
import pre_editor_phase30_qualification as motor
from pre_editor_phase28_qualification import safe


ROOT = motor.ROOT
EVIDENCE = ROOT / 'logs/pre_editor/phase-31/candidate-01'
ADMISSION = ROOT / 'logs/pre_editor/phase-31/entry-01/admission.json'
DECISION = ROOT / 'docs/pre_editor/active/PHASE_31_ENTRY_ACCEPTANCE_01.json'
OUTPUT = ROOT / 'bin/phase31-qualification'
INTERMEDIATE = ROOT / 'bin-int/phase31-qualification'
read, record, sha, require = motor.read, motor.record, motor.sha, motor.require


def environment():
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('GENGINE_', 'JPH_')) and
           k.upper() not in {'CL', '_CL_', 'LINK', '_LINK_', 'LIB', 'INCLUDE', 'LIBPATH', 'MSBUILD_EXE_PATH',
                             'FORCEIMPORTBEFORECPPTARGETS', 'FORCEIMPORTAFTERCPPTARGETS', 'OUTDIR', 'INTDIR'}}
    temp = safe(ROOT / 'runtime/tmp/phase31')
    temp.mkdir(parents=True, exist_ok=True)
    return dict(env, TMP=str(temp), TEMP=str(temp), MSBUILDDISABLENODEREUSE='1')


def admission():
    decision = read(DECISION)
    require(decision['status'] == 'ACTIVE_OWNER_ACCEPTED' and
            decision['packet']['sha256'] == sha(ADMISSION) == '43821614b5b6353997c74a47babe126c0ff23e7cc3b8408c88ac0851a244deb4',
            'Phase 31 execution decision or workload identity differs')
    return read(ADMISSION)


def build(config, project):
    """One dependent project at a time under the existing compiler resource guard."""
    packet = admission()
    profile = {'Debug': 'safety', 'Release': 'qualification'}[config]
    guarded = motor.guarded
    guarded.environment = environment
    root = safe(EVIDENCE / ('isolated-' + config))
    root.mkdir(parents=True, exist_ok=True)
    prior_seconds = sum(read(p)['seconds'] for p in root.glob('*-*/result.json'))
    require(prior_seconds < 1200, 'Profile compiler time exhausted')
    project_file = guarded.SOURCE[project]
    build_number = 1 + len(list(root.glob(project + '-*')))
    directory = safe(root / f'{project}-{build_number:02d}')
    directory.mkdir()
    info = build_inputs.closure(profile, project)
    product = safe(OUTPUT / config / project)
    obj = safe(INTERMEDIATE / config / project)
    for path in (product, obj):
        path.mkdir(parents=True, exist_ok=True)
    if project == 'RigidBodySimulation':
        engine = read(root / 'GEngine.json')
        require(engine['pass'] and sha(engine['product']) == engine['sha256'], 'Missing qualified engine library')
        info['archive_pch_companion'] = read(root / engine['attempt'] / 'closure.json')['archive_pch_companion']
    for member in packet['scope']['proposed_production_runtime'] + packet['scope']['proposed_supporting']:
        path = ROOT / member['path']
        if path.parts and path.is_relative_to(ROOT / project) and path.suffix in {'.cpp', '.h'}:
            info['dependencies'][str(path)] = sha(path)
    record(directory / 'closure.json', info)
    props = ET.fromstring(build_inputs.props(profile, project, info, obj, product))
    ns = build_inputs.NS
    flags = props.find('.//m:PreprocessorDefinitions', ns)
    flags.text = flags.text.replace('GENGINE_PHASE29_STEP_SAFETY;', '')
    if project == 'RigidBodySimulation':
        flags.text = 'GENGINE_PHASE31_QUALIFICATION;' + flags.text
        link = props.find('.//m:Link/m:AdditionalDependencies', ns)
        link.text = link.text.replace(str(build_inputs.OUTPUT/profile/'GEngine/GEngine.lib'), str(OUTPUT/config/'GEngine/GEngine.lib'))
    ET.register_namespace('', ns['m'])
    props_path = directory / 'build.props'
    ET.ElementTree(props).write(props_path, encoding='utf-8', xml_declaration=True)
    for old in sorted(root.glob(project+'-*/build.props')):
        if old != props_path and old.read_bytes() == props_path.read_bytes():
            props_path=old
            break
    args = [str(guarded.MSBUILD), str(project_file), '/t:Build', '/m:1', '/nr:false', '/v:minimal',
            '/p:Configuration=' + config, '/p:Platform=x64', '/p:BuildProjectReferences=false',
            '/p:VCToolsVersion=14.44.35207', '/p:WindowsTargetPlatformVersion=10.0.26100.0',
            '/p:PreferredToolArchitecture=x64', '/p:ForceImportBeforeCppTargets=' + str(props_path),
            '/p:UseMultiToolTask=true', '/p:MultiProcMaxCount=1', '/p:CL_MPCount=1',
            '/p:EnforceProcessCountAcrossBuilds=true', '/p:MultiProcessorCompilation=false']
    record(directory / 'command.json', args)
    result = guarded.monitor(args, directory, 1200 - prior_seconds)
    result['inputs_unchanged'] = all(sha(k)==v for k,v in info['dependencies'].items())
    record(directory/'result.json', result)
    if result['exit_code'] or result['guard'] or not result['inputs_unchanged']:
        return 1
    output = product / ('GEngine.lib' if project == 'GEngine' else 'RigidBodySimulation.exe')
    identity={'pass':True,'attempt':directory.name,'product':str(output),'sha256':sha(output)}
    record(directory/'product.json',identity)
    current=root/(project+'.json')
    if not current.exists():
        record(current,identity)
    else:
        # Keep earlier, unexecuted build records. Select the most recent successful link for this project.
        record(root/(directory.name+'.json'),identity)
    return 0


def product_inputs(config):
    from pre_editor_phase29_qualification import pe_words, SIZES
    base = EVIDENCE / ('isolated-' + config)
    for project in ('GEngine', 'RigidBodySimulation'):
        newer=sorted(base.glob(project+'-*.json'))
        product=read(newer[-1] if newer else base/(project+'.json'))
        require(product['pass'] and sha(product['product']) == product['sha256'], 'Linked artifact changed: ' + project)
        closure = read(base / product['attempt'] / 'closure.json')
        require(all(sha(path) == digest for path, digest in closure['dependencies'].items()), 'Compiled source drift: ' + project)
        require(all(sha(lib['path']) == lib['sha256'] for lib in closure['libraries'].values()), 'Build dependency drift')
    exe = Path(product['product'])
    mapfile = exe.with_suffix('.map')
    symbols = mapfile.read_text()
    address, sizes = pe_words(exe, symbols, 'g_Phase27TargetSizes', 8)
    _, abi = pe_words(exe, symbols, 'g_Phase29CaptureConfiguration', 11)
    require(sizes == abi[:8] == SIZES and all(flag in (0,1) for flag in abi[9:]), 'Arena compiled ABI or switches changed')
    policy = admission()['cost']['arena']
    require(all(sha(ROOT/path)==digest for path,digest in policy['site_source_sha256'].items()), 'Pinned Arena source drift')
    sites = {name:n for name,n in policy['aligned_sites_bytes'].items() if n}
    for flag,(name,n) in zip(abi[9:],policy['conditional_sites_bytes'].items()):
        if flag: sites[name] = n
    require(len(sites)==policy['positive_site_call_limit']+sum(abi[9:]), 'Enabled reservation count')
    return exe, {'preferred_sizes_va':address, 'abi_sizes':sizes, 'configuration':abi, 'sites':sites,
                 'arena_positive_sites':len(sites),'arena_sum_bytes':sum(sites.values()),'capacity':policy['capacity_bytes'],
                 'linker_map':{'path':str(mapfile),'sha256':sha(mapfile)},'source_closure':sha(base/product['attempt']/'closure.json')}


def run(role):
    import platform
    import postbuild
    stages = ['debug','jolt','native']
    index = stages.index(role)
    dest = safe(EVIDENCE/'runs'/role)
    require(not dest.exists(), 'Reservation already consumed; zero retries')
    config = 'Debug' if not index else 'Release'
    if index:
        previous_dir=EVIDENCE/'runs'/stages[index-1]
        reviewed=previous_dir/'assessment-02.json'
        previous=reviewed if reviewed.is_file() else previous_dir/'assessment.json'
        verdict=read(previous)
        require(verdict['status']=='PASS' and verdict['packet_sha256']==sha(ADMISSION) and
                verdict['binary_sha256']==read(previous_dir/'reservation.json')['binary_sha256'],
                'Required preceding role has not passed against the frozen evidence')
        if role=='native':
            require(verdict['binary_sha256']==sha(product_inputs(config)[0]),'Release provider candidates differ')
    exe, compiled = product_inputs(config)
    backend = 'native' if role=='native' else 'jolt'
    package = exe.parent.parent / 'assets'
    postbuild.stage_profile_runtime(ROOT,config,exe.parent)
    postbuild.stage_runtime_assets(ROOT,package)
    startup = package/'startup/RigidBodySimulation.txt'
    lines = startup.read_text().splitlines()
    require(lines and all((package/name).is_file() for name in lines[1:] if name),'Incomplete executable-relative startup package')
    census = subprocess.check_output(['powershell.exe','-NoProfile','-Command',
        "[Diagnostics.Process]::GetProcessesByName('RigidBodySimulation').Length"], timeout=15)
    require(census.strip()==b'0','Another RBS process is active')
    cwd=safe(ROOT/'runtime/RigidBodySimulation/phase31'/EVIDENCE.name/role)
    cwd.mkdir(parents=True,exist_ok=True)
    limit=24 if not index else 3240
    data=dest/'data'
    args=[str(exe),'--scene','movers','--backend',backend]
    reservation={'role':role,'workload':admission()['identity'],'backend':backend.title(),'configuration':config,
        'max_attempted':limit,'wall_seconds':180,'automatic_retries':0,'command':args,'cwd':str(cwd),'compiled':compiled,
        'binary_sha256':sha(exe),'packet_sha256':sha(ADMISSION),'decision_sha256':sha(DECISION),'supervisor_sha256':sha(__file__),
        'host':platform.platform(),'power':subprocess.check_output(['powercfg','/getactivescheme'],text=True).strip(),
        'startup_sha256':sha(startup),'assets':{name:sha(package/name) for name in lines[1:] if name},
        'dlls':{dll.name:sha(dll) for dll in exe.parent.glob('*.dll')},'utc':datetime.now(timezone.utc).isoformat()}
    dest.mkdir(parents=True,exist_ok=False); data.mkdir()
    record(dest/'reservation.json',reservation)
    env=environment();env['GENGINE_PHASE31_OUTPUT']=str(data)
    started=time.monotonic()
    with (dest/'process.log').open('xb') as stream:
        process=subprocess.Popen(args,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT)
        timeout=False
        try: code=process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            timeout=True;process.kill();code=process.wait()
    record(dest/'exit.json',{'pid':process.pid,'exit_code':code,'timeout':timeout,'seconds':time.monotonic()-started})
    return assess(role)


def trajectory_checks(directory, check, metrics):
    import csv
    import math
    import statistics
    import struct
    import pre_editor_phase29_qualification as geom
    from collections import defaultdict
    ref=admission()['reference']
    h=1/60
    u=2**-24
    gamma=8*u/(1-8*u)
    f32=lambda x:struct.unpack('f',struct.pack('f',x))[0]
    history=defaultdict(dict)
    for row in geom.rows(directory,'bodies.csv'):
        t,i=int(row['tick']),int(row['bone'])
        q=geom.vec(row,'qw qx qy qz')
        require(i not in history[t] and abs(geom.dot(q,q)-1)<=gamma*8,'invalid captured orientation or duplicate body')
        body={'pos':geom.vec(row,'x y z'),'q':q,'axes':geom.axes(q),'v':geom.vec(row,'vx vy vz'),
              'w':geom.vec(row,'wx wy wz'),'half':geom.vec(row,'hx hy hz'),'sleep':int(row['sleep']),
              'im':float(row['im']),'id':tuple(int(row[k]) for k in ('id','gen','world'))}
        require(all(math.isfinite(x) for key in ('pos','q','v','w','half') for x in body[key]),'nonfinite body')
        history[t][i]=body
    rows=geom.rows(directory,'mover.csv')
    require(set(history)==set(range(1621)) and all(set(bs)==set(range(7)) for bs in history.values()) and
            [int(r['tick']) for r in rows]==list(range(1621)),'two-body and room history incomplete')
    initial=history[0]
    check('identities',len({b['id'] for b in initial.values()})==7 and all(
        b['id']==initial[i]['id'] for bodies in history.values() for i,b in bodies.items()))
    check('reference',initial[0]['pos']==tuple(ref['box_platform']['center_m']) and
          math.dist(initial[1]['pos'],ref['box_rider']['center_m'])<=gamma*7 and
          initial[0]['im']==0 and initial[1]['im']==1 and all(initial[i]['im']==0 for i in range(2,7)) and
          initial[0]['half']==tuple(ref['box_platform']['half_m']) and initial[1]['half']==tuple(ref['box_rider']['half_m']))
    def energy(body):
        return .5*geom.dot(body['v'],body['v'])+geom.dot(body['w'],body['w'])/3+10*(body['pos'][1]-.5)
    origin=energy(initial[1]);physical=[origin];residual=[0.];work=0.;phase=0
    maximum=defaultdict(float);bad=defaultdict(list)
    def mark(name,t,condition):
        if not condition:bad[name].append(t)
    contacts=[];depths=[];slip=[];relative=[];pair_flags=[];event_flags=[]
    first=None;response=None;room_depth=0.;worst=(0.,0);max_chord=0.;max_derivative_difference=0.
    def horizontal(v):return (v[0],v[2])
    previous_offset=None
    for t,row in enumerate(rows):
        a,r=history[t][0],history[t][1];prev=history[max(0,t-1)]
        direction=0 if not t or 361<=t<=540 or t>=1441 else -1 if t>=601 else 1
        phase+=direction
        target=(10.,5.5,f32(-16*math.sin(.25*phase*h)))
        prev_p=prev[0]['pos'];flag=bool(int(row['contact']))
        pair_flags.append(flag)
        events=tuple(int(row[k]) for k in ('phase_tick','enabled','reversed','contact','slip_valid'))
        event_flags.append(events)
        mark('command',t,events[:3]==(phase,int(t==0 or direction!=0),int(t>=601)) and
             math.isclose(float(row['phase_time']),phase*h,rel_tol=1e-14,abs_tol=1e-14))
        achieved=geom.vec(row,'vx vy vz')
        for i,scale in enumerate((10.,5.5,16.)):
            scale=max(scale,abs(prev_p[i]),1)
            poserr=abs(a['pos'][i]-target[i]); maximum['position_m']=max(maximum['position_m'],poserr)
            mark('trajectory',t,poserr<=gamma*scale and abs(geom.vec(row,'tx ty tz')[i]-target[i])<=gamma*scale)
            chord=(target[i]-prev_p[i])/h if t and direction else 0.
            err=abs(a['v'][i]-chord);max_chord=max(max_chord,err)
            mark('chord_velocity',t,err<=gamma*(scale/h+max(1,abs(chord))) and achieved[i]==a['v'][i])
            if i==2 and t and direction:
                max_derivative_difference=max(max_derivative_difference,abs(chord+4*direction*math.cos(.25*phase*h)))
        mark('no_platform_rotation',t,geom.dot(a['w'],a['w'])==0 and geom.arc(a['q'],initial[0]['q'])<=gamma)
        depth=geom.overlap(a,r);depths.append(depth)
        if depth>worst[0]:worst=(depth,t)
        for i in range(2,7):
            room=history[t][i]
            mark('room_isolation',t,room['pos']==initial[i]['pos'] and room['q']==initial[i]['q'])
            # Each room collider is fixed and axis aligned. An AABB separation proves OBB separation.
            for b in (a,r):
                extent=[sum(abs(axis[k])*half for axis,half in zip(b['axes'],b['half'])) for k in range(3)]
                if all(abs(b['pos'][k]-room['pos'][k])<=extent[k]+room['half'][k] for k in range(3)):
                    room_depth=max(room_depth,geom.overlap(b,room));mark('room_isolation',t,False)
        if flag:
            contacts.append(t)
            if first is None:first=t;previous_offset=geom.sub(r['pos'],a['pos'])
        if t and flag and r['v'][1]-prev[1]['v'][1]+10*h>gamma*10 and response is None:response=t
        if first is not None:
            offset=geom.sub(r['pos'],a['pos']);local=[geom.dot(offset,x) for x in a['axes']]
            slip.append(math.dist(horizontal(offset),horizontal(previous_offset)))
            mark('support_footprint',t,abs(local[0])<=3 and abs(local[2])<=3 and local[1]>0)
        if flag:
            # Bound actual relative velocity across the four lower rider corners using rigid point velocities.
            vertex_values=[]
            for sx in (-1,1):
                for sz in (-1,1):
                    arm=geom.rot(r['q'],(sx*r['half'][0],-r['half'][1],sz*r['half'][2]))
                    v=geom.sub(geom.add(r['v'],geom.cross(r['w'],arm)),a['v'])
                    vertex_values.append(math.hypot(v[0],v[2]))
            relative.append(max(vertex_values))
        if t:
            dv=geom.sub(r['v'],prev[1]['v'])
            work+=a['v'][0]*dv[0]+a['v'][2]*dv[2]
            E=energy(r);physical.append(E);residual.append(E-origin-work)
    check('first_support',first is not None and response is not None and max(first,response)<=7,{'pair_tick':first,'response_tick':response})
    check('peak_contact',worst[0]<=.01,{'meters':worst[0],'tick':worst[1]})
    episodes=[];t=1
    while t<=1620:
        if depths[t]<=.005:t+=1;continue
        first_bad=t;recover=None
        while t<1620:
            t+=1
            if depths[t]<=.005 and t+1<=1620 and depths[t+1]<=.005:recover=t;break
        episodes.append([first_bad,recover]);t+=1
    check('contact_recovery',all(end is not None and end<=start+4 for start,end in episodes),episodes[:24])
    check('settled_depth',max(depths[t] for t in (*range(481,541),*range(1561,1621)))<=.005)
    check('no_room_contact_or_fallthrough',not bad['room_isolation'] and not bad['support_footprint'],room_depth)
    carry=[initial[1]['pos'][2]-history[360][1]['pos'][2],history[1440][1]['pos'][2]-history[600][1]['pos'][2]]
    check('rider_carried_both_directions',all(x>2 for x in carry),carry)
    bins=[]
    for lo,hi in ((1,360),(361,540),(541,600),(601,1440),(1441,1620)):
        means=[statistics.fmean(residual[i:i+60]) for i in range(lo,hi-58,60)]
        if len(means)<2: bins.append([lo,hi,None]);continue
        middle=(len(means)-1)/2
        slope=sum((i-middle)*v for i,v in enumerate(means))/sum((i-middle)**2 for i in range(len(means)))
        bins.append([lo,hi,slope*(len(means)-1)])
    check('energy_upper',max(residual)<=.82,{'joules':max(residual),'tick':residual.index(max(residual))})
    check('energy_trends',all(rise is None or rise<=.00125 for _,_,rise in bins),bins)
    for stop,deadline,end in ((360,480,540),(1440,1560,1620)):
        rise=max(physical[t]-physical[stop] for t in range(stop+1,end+1))
        check('stopped_energy_'+str(stop),rise<=.00125,rise)
        window=[history[t][1] for t in range(deadline,end+1)]
        check('sleep_'+str(stop),all(b['sleep'] for b in window),
              {'first_sleep':next((t for t in range(stop+1,end+1) if history[t][1]['sleep']),None),
               'awake_after_deadline':sum(not b['sleep'] for b in window)})
        pos=max(math.dist(a['pos'],b['pos']) for a in window for b in window)
        orient=max(geom.arc(a['q'],b['q']) for a in window for b in window)
        hf=max(math.dist(b['pos'],geom.scale(geom.add(a['pos'],c['pos']),.5)) for a,b,c in zip(window,window[1:],window[2:]))
        def midpoint(q,s):
            if geom.dot(q,s)<0:s=tuple(-x for x in s)
            v=tuple(x+y for x,y in zip(q,s))
            return tuple(x/math.sqrt(geom.dot(v,v)) for x in v)
        hf_q=max(geom.arc(b['q'],midpoint(a['q'],c['q'])) for a,b,c in zip(window,window[1:],window[2:]))
        check('quiet_motion_'+str(stop),pos<=.002 and orient<=.002 and hf<=.001 and hf_q<=.001,
              {'position_m':pos,'position_hf_m':hf,'orientation_rad':orient,'orientation_hf_rad':hf_q})
    check('restart_wake',not history[541][1]['sleep'] and pair_flags[541],
          {'first_awake_after_restart':next((t for t in range(541,601) if not history[t][1]['sleep']),None)})
    for name in ('command','trajectory','chord_velocity','no_platform_rotation'):
        check(name,not bad[name],{'first_ticks':sorted(set(bad[name]))[:8],'count':len(set(bad[name]))})
    metrics.update(max_position_m=maximum['position_m'],max_chord_error_m_s=max_chord,
        max_endpoint_derivative_minus_chord_m_s=max_derivative_difference,provider_pair_ticks=len(contacts),
        peak_penetration_m=worst[0],peak_penetration_tick=worst[1],support_tick=first,recovery_episodes=episodes,
        energy_residual_peak_J=max(residual),total_platform_work_J=work,energy_residual_final_J=residual[-1],
        rider_slip_peak_m=max(slip,default=0),rider_slip_rms_m=math.sqrt(statistics.fmean(x*x for x in slip)) if slip else None,
        lower_face_relative_point_speed_upper_peak_m_s=max(relative,default=0),
        lower_face_relative_point_speed_upper_rms_m_s=math.sqrt(statistics.fmean(x*x for x in relative)) if relative else None)
    return history,event_flags


def allocation_checks(data, directory, reservation, check, metrics):
    import bisect
    import re
    import struct
    from collections import defaultdict,Counter
    import pre_editor_phase29_qualification as geom
    cfg=reservation['compiled'];sites=cfg['sites'];raw=geom.rows(directory,'allocations.csv')
    cost=geom.rows(directory,'cost.csv')
    require([int(row['tick']) for row in cost]==list(range(1,1621)),'Cost rows missing or reordered')
    active=reservation['backend']=='Jolt'
    for field in ('inclusive_ns','observer_upper_ns'):
        values=[int(r[field]) for r in cost]
        quantum=max(int(r['q']) for r in cost)
        stats=geom.statistics_for(values)
        q=quantum*(2 if field=='observer_upper_ns' else 1)
        center=1e9/(60*(100 if field=='observer_upper_ns' else 2))
        tail=1e9/60*(1/50 if field=='observer_upper_ns' else .75)
        check(field,0<quantum<=1000 and max(stats['mean'],stats['median'])+q<=center and
              max(stats['p95'],stats['max_sliding60'])+q<=tail,stats)
        metrics[field]=stats
    for field in ('wrapper_ns','profile_hooks_ns','boundary_ns','capture_ns'):
        metrics[field]=geom.statistics_for([int(r[field]) for r in cost])
    cpp=Counter((int(r['cpp_calls']),int(r['cpp_bytes'])) for r in cost)
    metrics['cpp_heap_call_byte_histogram']=[[list(key),n] for key,n in sorted(cpp.items())]
    check('zero_steady_cpp_or_attributed',all(int(r['cpp_calls'])==0 and int(r['cpp_bytes'])==0 for r in cost[1:]),
          'Nonzero whole-Update C++ calls require allocation-site attribution before incremental mover cost can pass.')
    by_tick=defaultdict(list)
    for e in raw:
        t=int(e['tick']);require(1<=t<=1620 and e['allocator'] in ('arena','heap') and e['reserve'] in ('0','1'),'invalid event')
        by_tick[t].append(e)
    if not active:
        check('no_jolt_allocator_on_native',not raw);return
    linkmap=Path(cfg['linker_map']['path'])
    require(sha(linkmap)==cfg['linker_map']['sha256'],'Linker map changed')
    symbols=sorted((int(a,16),s) for s,a in re.findall(r'^\s*[\da-fA-F]+:[\da-fA-F]+\s+(\S+)\s+([\da-fA-F]{12,16})\b',linkmap.read_text(),re.M))
    addresses=[a for a,_ in symbols];base=int((directory/'address.txt').read_text())-cfg['preferred_sizes_va']
    def symbol(pointer):
        addr=int(pointer)-base;i=bisect.bisect_right(addresses,addr)-1
        return (symbols[i][1],addr-symbols[i][0]) if i>=0 else ('unmapped',0)
    # Reuse prior instruction attribution only after verifying the same compiled code bytes at that return offset.
    prev=read(motor.EVIDENCE/'isolated-Release/result.json')
    binding=prev['arena_site_review'];require(sha(ROOT/binding['path'])==binding['sha256'],'Historical attribution drift')
    reviewed=read(ROOT/binding['path'])
    previous=Path(prev['binary'])
    def image_reader(path):
        content=path.read_bytes();pe=struct.unpack_from('<I',content,60)[0];opt=pe+24
        address=struct.unpack_from('<Q',content,opt+24)[0];count=struct.unpack_from('<H',content,pe+6)[0]
        offset=opt+struct.unpack_from('<H',content,pe+20)[0]
        sections=[struct.unpack_from('<IIII',content,offset+40*i+8) for i in range(count)]
        def window(va):
            rva=va-address-12
            for _,start,size,at in sections:
                if start<=rva and rva+20<=start+size:return content[at+rva-start:at+rva-start+20]
            return None
        return window
    matched={}
    if previous.is_file() and sha(previous)==reviewed['binary_sha256']:
        old=image_reader(previous);current=image_reader(Path(reservation['command'][0]));where={s:a for a,s in symbols}
        for va,item in reviewed['addresses'].items():
            name=item['symbol']
            if name in where:
                target=where[name]+item['offset'];a=old(int(va,16));b=current(target)
                if a and a==b:matched[(name,item['offset'])]=item['site']
    site_review=EVIDENCE/'arena-source-review-01/verified-sites.json'
    if site_review.is_file():
        review=read(site_review)
        require(sha(site_review)=='bfbd47183875193533d2f291660e4f246556901ac44581d7037e9a6ea5c26d97' and
                review['binary_sha256']==reservation['binary_sha256'] and review['map_sha256']==sha(linkmap) and
                review['source_sha256']==sha(ROOT/review['source']) and review['packet_sha256']==sha(ADMISSION),'Site attribution belongs to another candidate')
        for m in review['mappings']:
            require(sha(ROOT/m['disassembly'])==m['disassembly_sha256'] and m['site'] in sites,'Site proof drift')
            matched[(m['symbol'],m['offset'])]=m['site']
    groups={
        'Update@PhysicsSystem':('step_frames','body_pairs'),
        'STLTempAllocator':('step_frames',),
        'PrepareConstraintBuffer@ContactConstraintManager':('contact_buffer','contact_offsets'),
        'PrepareContactConstraints@IslandBuilder':('contact_links','JPH_VALIDATE_ISLAND_BUILDER'),
        'BuildBodyIslands@IslandBuilder':('body_islands','body_starts'),
        'BuildConstraintIslands@IslandBuilder':('contact_islands','contact_ends'),
        'SortIslands@IslandBuilder':('sorted_islands','sort_counts_transient'),
        'Finalize@IslandBuilder':('body_islands','body_starts','contact_islands','contact_ends','sorted_islands','sort_counts_transient','position_steps','JPH_TRACK_SIMULATION_STATS'),
        'JobPreIntegrateVelocity@PhysicsSystem':('ccd_bodies','active_ccd_mapping'),
        'JobResolveCCDContacts@PhysicsSystem':('sorted_ccd_transient',)}
    requests={'step_frames':{7344},'body_pairs':{131072},'contact_buffer':{12189696},'contact_offsets':{65536},
              'contact_links':{65536},'contact_islands':{4*n for n in range(1,16385)},'contact_ends':{4,8,12},
              'body_islands':{4,8},'body_starts':{4,8,12},'sorted_islands':{4,8},'sort_counts_transient':{4,8},
              'position_steps':{1,2},'ccd_bodies':{160,320},'active_ccd_mapping':{4,8},'sorted_ccd_transient':{8,16},
              'JPH_VALIDATE_ISLAND_BUILDER':{131072},'JPH_TRACK_SIMULATION_STATS':{64}}
    peak=count_peak=total_peak=reservations=releases=0;shape_errors=[];unknown={};site_census=Counter()
    live={};job=None;unclassified=set();heap_balanced=True
    for tick in range(1,1621):
        used=aligned_sum=calls=heap_calls=heap_bytes=0;stack=[];sources=set()
        for e in by_tick[tick]:
            n=int(e['requested']);address=int(e['pointer']);allocate=e['reserve']=='1'
            name,offset=symbol(e['caller'])
            if e['allocator']=='heap':
                if allocate:
                    if address in live:heap_balanced=False
                    live[address]=(name,n);heap_calls+=1;heap_bytes+=n
                    if tick==1 and n==229376 and 'JobSystem@JPH' in name:job=address
                else:
                    old=live.pop(address,None)
                    if not old:heap_balanced=False
                    elif 'QuadTree@' not in old[0] or 'QuadTree@' not in name:unclassified.add((old[0],name,old[1]))
                continue
            aligned=int(e['aligned']);require(aligned==(n+15)//16*16,'Arena alignment')
            if n and allocate:
                stack.append((address,n));used+=aligned;calls+=1;aligned_sum+=aligned;reservations+=1
                choices=[matched[(name,offset)]] if (name,offset) in matched else [
                    key for token,names in groups.items() if token in name for key in names if key in sites and n in requests[key]]
                if len(set(choices))==1 and choices[0] in sites and n in requests[choices[0]] and aligned<=sites[choices[0]] and choices[0] not in sources:
                    sources.add(choices[0]);site_census[choices[0]]+=1
                else:unknown[(name,offset,n)]=sorted(set(choices))
            if n and not allocate:
                require(stack and stack.pop()==(address,n),'Arena release mismatch')
                used-=aligned;releases+=1
            require(int(e['used'])==used and 0<=used<=cfg['capacity'],'Arena occupancy disagreement')
            peak=max(peak,used)
        require(not stack and not used,'Outstanding Arena bytes at Update end')
        if calls>cfg['arena_positive_sites'] or aligned_sum>cfg['arena_sum_bytes']:shape_errors.append(tick)
        count_peak=max(count_peak,calls);total_peak=max(total_peak,aligned_sum)
        heap_balanced &= (set(live)==({job} if job else set()) and heap_calls==int(cost[tick-1]['jph_calls']) and heap_bytes==int(cost[tick-1]['jph_bytes']))
    teardown=directory if directory.name=='1' else data
    release=geom.job_page_teardown(teardown,job,lambda a:symbol(a)[0]) if job else {'status':'NO_JOB_PAGE_OBSERVED'}
    check('balanced_arena',peak>0 and reservations==releases and not shape_errors,
          {'peak_bytes':peak,'max_positive_reservations':count_peak,'max_positive_aligned_bytes':total_peak,
           'positive_reserves':reservations,'matching_releases':releases,'violations':shape_errors})
    check('source_site_attribution',not unknown,[[str(k),v] for k,v in unknown.items()][:15])
    check('jolt_heap_lifetime',heap_balanced and not unclassified and (not job or release['status']=='MATCHED'),release)
    metrics.update(arena_peak_bytes=peak,positive_reserves=reservations,releases=releases,
                   max_sites=count_peak,max_aligned_bytes=total_peak,site_counts=site_census,unclassified_heap=list(unclassified),
                   job_page=release)


def assess_replays(data,reservation,check,metrics):
    import pre_editor_phase29_qualification as geom
    runs=[]
    for schedule in ('1','2'):
        directory=data/schedule
        info=metrics['schedule_'+schedule]={}
        def scoped(name,ok,detail=None):
            check('s'+schedule+'_'+name,ok,detail)
        for name,operation in (('physics',lambda:trajectory_checks(directory,scoped,info)),
                               ('cost',lambda:allocation_checks(data,directory,reservation,scoped,info))):
            try:
                result=operation()
                if name=='physics':runs.append(result)
            except (ValueError,OSError,RuntimeError,ArithmeticError,KeyError,IndexError,TypeError) as err:
                scoped(name+'_complete_evidence',False,str(err))
    if len(runs)!=2:
        check('replay',False,'One or both per-tick histories unavailable');return
    (first,events1),(second,events2)=runs
    bad=[];gamma=8*2**-24/(1-8*2**-24);peak=0.
    for t in range(1621):
        if events1[t]!=events2[t]:bad.append([t,'event'])
        for i in range(7):
            a,b=first[t][i],second[t][i]
            equal=a['sleep']==b['sleep'] and a['id'][2]!=b['id'][2]
            for key in ('pos','q','v','w'):
                sign=-1 if key=='q' and geom.dot(a[key],b[key])<0 else 1
                for x,y in zip(a[key],b[key]):
                    delta=abs(x-sign*y);peak=max(peak,delta)
                    equal &= delta<=gamma*max(1,abs(x),abs(y))
            if not equal:bad.append([t,i])
    check('within_provider_replay',not bad,{'mismatches':len(bad),'first':bad[:10],'peak_component_delta':peak})


def assess(role):
    import csv
    dest=EVIDENCE/'runs'/role
    data=dest/'data'
    reservation=read(dest/'reservation.json')
    require(sha(ADMISSION)==reservation['packet_sha256'],'Wrong assessment packet')
    checks={};metrics={}
    def check(name,ok,detail=None):
        checks[name]={'pass':bool(ok),'detail':detail}
    exit_record=read(dest/'exit.json')
    check('process_exit',exit_record['exit_code']==0 and not exit_record['timeout'],exit_record)
    try:
        outcome=(data/'outcome.txt').read_text().splitlines()
        attempted,completed=map(int,outcome[1].split())
        check('capture',outcome[0]=='CAPTURE_COMPLETE',outcome[0])
        metrics.update(attempted=attempted,completed=completed)
        check('attempt_reservation',0<=completed<=attempted<=reservation['max_attempted'],[attempted,completed])
        domains=list(csv.reader((data/'domains.csv').read_text().splitlines()))
        debited=[r for r in domains if r[0] in ('invalid_duration','retiring','exit')]
        check('world_accounting',len({r[1] for r in debited})==len(debited) and
              sum(int(r[2]) for r in debited)==attempted and sum(int(r[3]) for r in debited)==completed,domains)
        teardown=(data/'teardown-heap.csv').read_text().splitlines()
        check('final_world_destroyed',teardown[-1].startswith('complete,'),teardown[-1])
        if role=='debug':
            cases=list(csv.reader((data/'safety.csv').read_text().splitlines()))
            check('two_backend_safety',[r[0].lower() for r in cases]==['jolt','native'] and all(r[-1]=='PASS' for r in cases),cases)
            check('rejected_duration_and_successful_clocks',attempted==12 and completed==10 and
                  sum(r[0]=='invalid_duration' and r[2:4]==['2','1'] for r in domains)==2,[attempted,completed])
            image=data/'ordinary-viewport.png'
            check('ordinary_viewport',image.is_file() and image.read_bytes()[:8]==b'\x89PNG\r\n\x1a\n',
                  sha(image) if image.is_file() else None)
        else:
            check('two_fresh_replays',attempted==completed==3240 and len(debited)==2,debited)
            assess_replays(data,reservation,check,metrics)
    except (OSError,ValueError,KeyError,IndexError,TypeError,ArithmeticError,RuntimeError) as err:
        check('complete_evidence',False,str(err))
    result={'role':role,'status':'PASS' if all(c['pass'] for c in checks.values()) else 'FAIL',
            'packet_sha256':sha(ADMISSION),'binary_sha256':reservation['binary_sha256'],'checks':checks,'metrics':metrics}
    original=dest/'assessment.json'
    if original.exists():
        require(role=='jolt' and (EVIDENCE/'arena-source-review-01/verified-sites.json').is_file(),'Unexpected repeat assessment')
        result.update(original_assessment_sha256=sha(original),offline_reassessment=True,
                      evaluator_sha256=sha(__file__),source_review_sha256=sha(EVIDENCE/'arena-source-review-01/verified-sites.json'),
                      retained_raw={p.relative_to(data).as_posix():sha(p) for p in data.rglob('*') if p.is_file()})
        record(dest/'assessment-02.json',result)
    else:
        record(original,result)
    print(json.dumps({'role':role,'status':result['status'],'failed':[k for k,v in checks.items() if not v['pass']],
                      'attempted':metrics.get('attempted'),'completed':metrics.get('completed')}))
    return int(result['status']!='PASS')


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    subs=parser.add_subparsers(dest='action',required=True)
    compile_cmd=subs.add_parser('build')
    compile_cmd.add_argument('config',choices=['Debug','Release'])
    compile_cmd.add_argument('project',choices=['GEngine','RigidBodySimulation'])
    for action in ('run','assess'):
        subs.add_parser(action).add_argument('role',choices=['debug','jolt','native'])
    args=parser.parse_args()
    sys.exit(build(args.config,args.project) if args.action=='build' else (run if args.action=='run' else assess)(args.role))
