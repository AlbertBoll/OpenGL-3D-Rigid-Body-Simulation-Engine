"""Offline, create-only diagnostic decoder. Never calls Physics or assigns qualification PASS."""
import argparse, collections, csv, ctypes as C, hashlib, json, math, sys
from pathlib import Path
from collider_cache_decode import evidence as cache_evidence
F=C.c_float; D=C.c_double; U=C.c_uint32; I=C.c_int32; Q=C.c_uint64; B=C.c_bool
class V(C.LittleEndianStructure): _fields_=[('x',F),('y',F),('z',F)]
class BS(C.LittleEndianStructure):
    _fields_=[('position',V),('linear',V),('angular',V),('orientation',F*4),('inactive',D),('sleeping',B)]
class Ev(C.LittleEndianStructure):
    _fields_=[('operation',I),('a',Q),('b',Q),('beforeA',BS),('beforeB',BS),('afterA',BS),('afterB',BS),('normal',V),('anchorA',V),('anchorB',V),('linearImpulseB',V),('angularImpulseB',V),('accumulatedBefore',D*3),('accumulatedAfter',D*3),('restitution',D),('closingSpeed',D),('penetration',D),('linearImpulseA',V),('angularImpulseA',V),('localA',V),('localB',V),('tangentU',V),('tangentV',V),('normalRow',D*12),('tangentRowU',D*12),('tangentRowV',D*12),('featureA',Q),('featureB',Q),('slot',I),('previousSlot',I),('reason',I),('time',D),('duration',D),('bias',D),('constraintToken',Q),('appliedImpulsesKnown',B)]
class Ct(C.LittleEndianStructure):
    _fields_=[('ptOnA_WorldSpace',V),('ptOnB_WorldSpace',V),('ptOnA_LocalSpace',V),('ptOnB_LocalSpace',V),('normal',V),('separationDistance',F),('timeOfImpact',F),('featureA',U),('featureB',U),('m_BodyA',Q),('m_BodyB',Q)]
class SR(C.LittleEndianStructure): _fields_=[('tick',Q),('stage',U),('body',U),('state',BS)]
class ER(C.LittleEndianStructure): _fields_=[('tick',Q),('stage',U),('a',U),('b',U),('event',Ev)]
class CR(C.LittleEndianStructure): _fields_=[('tick',Q),('stage',U),('a',U),('b',U),('slot',U),('contact',Ct),('actualA',V),('actualB',V),('projectedDepth',D)]
class GD(C.LittleEndianStructure): _fields_=[('termination',I),('iterations',U),('epaTermination',I),('epaIterations',U)]
class QC(C.LittleEndianStructure): _fields_=[('query',Q),('a',Q),('b',Q),('slotA',Q),('generationA',Q),('slotB',Q),('generationB',Q),('role',I),('stage',I),('dt',F),('accumulatedTime',F)]
class QE(C.LittleEndianStructure): _fields_=[('kind',I),('context',QC),('beforeA',BS),('beforeB',BS),('afterA',BS),('afterB',BS),('outcome',I),('reason',I),('diagnostics',GD),('contact',Ct),('patch',Ct*4),('patchCount',U),('admission',I)]
class QR(C.LittleEndianStructure): _fields_=[('tick',Q),('a',U),('b',U),('event',QE)]
STAGES=['Begin','Wake','Gravity','Broadphase','Narrowphase','WarmStart','Islands','Solve','Integration','Correction','Sleep','End']
OPS=['WarmStart','NormalSliding','Spin','Rolling','Position','BlockNormal','ToiNormal','ToiFriction','ToiPosition','ContactNew','ContactRefresh','ContactRemove','PairBegin','PairEnd','PairReset','PatchBegin','PatchEnd','PointMap','ContactSnapshot','SleepDecision','GravityImpulse','Integrate','ToiQuery']
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for data in iter(lambda:f.read(1048576),b''):h.update(data)
    return h.hexdigest()
def write(p,obj):
    with p.open('x',encoding='utf-8',newline='\n') as f:json.dump(obj,f,indent=2,allow_nan=False);f.write('\n')
def vec(v):return [float(v.x),float(v.y),float(v.z)]
def sub(a,b):return [x-y for x,y in zip(a,b)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def norm(a):return math.sqrt(dot(a,a))
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def quat(raw,order):return list(raw) if order=='wxyz' else [raw[3],raw[0],raw[1],raw[2]]
def rotate(q,v):
    length=norm(q);assert length>0 and math.isfinite(length),'invalid quaternion'
    w,x,y,z=[a/length for a in q];u=[x,y,z];uv=cross(u,v);uuv=cross(u,uv)
    return [v[i]+2*(w*uv[i]+uuv[i]) for i in range(3)]
def inverse(q):return [q[0],-q[1],-q[2],-q[3]]
def state(s,order):return {'position':vec(s.position),'linear':vec(s.linear),'angular':vec(s.angular),'orientation_wxyz':quat(s.orientation,order),'inactive':s.inactive,'sleeping':bool(s.sleeping)}
def rows(p,typ):
    size=C.sizeof(typ);assert p.stat().st_size%size==0,f'truncated {p.name}'
    with p.open('rb') as f:
        while data:=f.read(size*1024):
            assert len(data)%size==0
            for at in range(0,len(data),size):yield typ.from_buffer_copy(data,at)
def verify_layout(layout):
    assert layout['version']==1 and layout['endian']=='little'
    for key,typ in [('state_row',SR),('event_row',ER),('contact_row',CR),('body_state',BS),('event',Ev),('contact',Ct)]:assert layout[key]==C.sizeof(typ),(key,layout[key],C.sizeof(typ))
    for key,typ in [('state_fields',BS),('state_row_fields',SR),('event_row_fields',ER),('event_fields',Ev),('contact_row_fields',CR),('contact_fields',Ct)]:
        for name,off in layout[key].items():assert getattr(typ,name).offset==off,(key,name,off,getattr(typ,name).offset)
def verify_queries(layout):
    for key,size in [('row_size',C.sizeof(QR)),('event_size',C.sizeof(QE)),('context_size',C.sizeof(QC)),('event_offset',QR.event.offset)]:assert layout[key]==size,(key,layout[key],size)
    mapping={'beforeA':'beforeA','beforeB':'beforeB','afterA':'afterA','afterB':'afterB','patch_count':'patchCount'}
    for name in ['kind','context','beforeA','beforeB','afterA','afterB','outcome','reason','diagnostics','contact','patch','patch_count','admission']:
        assert layout[name+'_offset']==getattr(QE,mapping.get(name,name)).offset,name

def analyze(source,out):
    source=source.resolve();out=out.resolve();root=Path(__file__).resolve().parents[1]
    assert out.is_relative_to(root/'campaign'),'output must be isolated campaign path'
    assert not out.exists(),'create-only output directory already exists'
    assert source.is_dir() and out!=source
    layout=load(source/'observer-layout.json');verify_layout(layout)
    before=load(source/'replacement-before.json');after=load(source/'replacement-after.json');status=load(source/'observer-status.json')
    identity=list(csv.DictReader((source/'observer-bodies.csv').open(encoding='utf-8')))
    uuid=int(before['selected_uuid']);selected=[int(x['ordinal']) for x in identity if int(x['uuid'])==uuid];assert len(selected)==1;selected=selected[0]
    initial={int(b['ordinal']):b for b in before['world']['bodies']};afterb={int(b['ordinal']):b for b in after['world']['bodies']}
    candidates={'wxyz','xyzw'};first={};post={}
    for r in rows(source/'observer-states.bin',SR):
        if r.stage==100:
            expected=initial[r.body]['orientation_wxyz'];raw=list(r.state.orientation)
            candidates={o for o in candidates if quat(raw,o)==expected};first[r.body]=r
        elif r.stage==101:post[r.body]=r
    assert len(candidates)==1,'ambiguous quaternion layout';order=candidates.pop()
    assert set(first)==set(initial)==set(post),'incomplete before/after states'
    for ordinal,r in first.items():
        assert vec(r.state.position)==initial[ordinal]['position'] and vec(r.state.linear)==initial[ordinal]['linear_velocity'] and vec(r.state.angular)==initial[ordinal]['angular_velocity'],'before JSON/raw state mismatch'
    files=[p for p in source.iterdir() if p.is_file() and (p.name.startswith('observer-') or p.name.startswith('replacement-') or p.name.startswith('selected-mesh-'))]
    hashes={p.name:sha(p) for p in files}
    cache_summary,cache_images=cache_evidence(source,selected)
    out.mkdir(parents=True)
    write(out/'canonical-caches.json',cache_images)
    counts=collections.Counter();finite=True;body_stats={};solveq={};previous_end={};sleep_changes=[];pre_sleep=collections.Counter();seen=set()
    with (out/'states.csv').open('x',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(['tick','stage','body','uuid','px','py','pz','vx','vy','vz','wx','wy','wz','qw','qx','qy','qz','inactive','sleeping','linear_speed','angular_speed','speed_eligible'])
        for r in rows(source/'observer-states.bin',SR):
            key=(r.tick,r.stage,r.body);assert key not in seen,'duplicate state record';seen.add(key)
            s=r.state;v=vec(s.linear);a=vec(s.angular);linear=norm(v);angular=norm(a);values=vec(s.position)+v+a+quat(s.orientation,order)+[s.inactive]
            finite &= all(math.isfinite(x) for x in values);threshold=initial[r.body];eligible=threshold['inverse_mass']>0 and all(math.isfinite(x) for x in values) and linear<=threshold['sleep_linear_threshold'] and angular<=threshold['sleep_angular_threshold']
            w.writerow([r.tick,r.stage,r.body,threshold.get('entity_uuid'),*values,int(s.sleeping),linear,angular,int(eligible)]);counts[str(r.stage)]+=1
            if r.stage==7:solveq.setdefault(r.tick,{})[r.body]=quat(s.orientation,order)
            if r.stage==99:pre_sleep[(r.body,bool(s.sleeping))]+=1
            if r.stage==11:
                b=body_stats.setdefault(r.body,{'samples':0,'sleeping_steps':0,'eligible_steps':0,'max_linear_speed':0,'max_angular_speed':0,'min_inactivity':s.inactive,'max_inactivity':s.inactive,'last':None,'tail_120':collections.deque(maxlen=120)})
                b['samples']+=1;b['sleeping_steps']+=bool(s.sleeping);b['eligible_steps']+=eligible;b['max_linear_speed']=max(b['max_linear_speed'],linear);b['max_angular_speed']=max(b['max_angular_speed'],angular);b['min_inactivity']=min(b['min_inactivity'],s.inactive);b['max_inactivity']=max(b['max_inactivity'],s.inactive);b['last']=state(s,order);b['tail_120'].append((linear,angular,bool(s.sleeping),s.inactive,vec(s.position),quat(s.orientation,order)))
                if r.body in previous_end and previous_end[r.body]!=bool(s.sleeping):sleep_changes.append({'tick':r.tick,'body':r.body,'from_sleeping':previous_end[r.body],'to_sleeping':bool(s.sleeping)})
                previous_end[r.body]=bool(s.sleeping)
    pair_stats={};contact_at_solve={};contact_at_correction={};neighbors=set();contact_rows=0
    with (out/'contacts.csv').open('x',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(['tick','stage','a','b','slot','feature_a','feature_b','projected_depth','separation','toi','normal_x','normal_y','normal_z','actual_ax','actual_ay','actual_az','actual_bx','actual_by','actual_bz'])
        for r in rows(source/'observer-contacts.bin',CR):
            c=r.contact;p=tuple(sorted((r.a,r.b)));key=f'{p[0]}:{p[1]}';contact_rows+=1
            ps=pair_stats.setdefault(key,{'rows':0,'max_projected_depth':-1e100,'min_projected_depth':1e100,'stage_counts':collections.Counter(),'tick_min':r.tick,'tick_max':r.tick})
            ps['rows']+=1;ps['max_projected_depth']=max(ps['max_projected_depth'],r.projectedDepth);ps['min_projected_depth']=min(ps['min_projected_depth'],r.projectedDepth);ps['stage_counts'][str(r.stage)]+=1;ps['tick_max']=max(ps['tick_max'],r.tick)
            finite &= all(math.isfinite(x) for x in [r.projectedDepth,c.separationDistance,c.timeOfImpact,*vec(c.normal),*vec(r.actualA),*vec(r.actualB)])
            w.writerow([r.tick,r.stage,r.a,r.b,r.slot,c.featureA,c.featureB,r.projectedDepth,c.separationDistance,c.timeOfImpact,*vec(c.normal),*vec(r.actualA),*vec(r.actualB)])
            if selected in p:
                neighbors.update(p)
                k=(r.a,r.b,r.slot,c.featureA,c.featureB)
                if r.stage==7:contact_at_solve.setdefault(r.tick,{})[k]={'normal':vec(c.normal),'localA':vec(c.ptOnA_LocalSpace),'localB':vec(c.ptOnB_LocalSpace),'depth':r.projectedDepth}
                if r.stage==9:contact_at_correction.setdefault(r.tick,{})[k]=r.projectedDepth
    operations=collections.Counter();reasons=collections.Counter();event_pairs={};correction_summary={};correction_rows=0;max_residual=0.;checked_residuals=0
    current_tick=None;latest={};normal_cache={};totals={};first_depth={}
    def worldpoint(body,local,s):
        rotated=rotate(s['orientation_wxyz'],[local[i]+afterb[body]['geometry']['center_of_mass'][i] for i in range(3)])
        return [s['position'][i]+rotated[i] for i in range(3)]
    def finish_tick(tick):
        nonlocal max_residual,checked_residuals
        if tick is None:return
        for k,initial_depth in first_depth.items():
            if k in contact_at_correction.get(tick,{}):
                residual=abs(initial_depth+totals.get(k,0)-contact_at_correction[tick][k]);max_residual=max(max_residual,residual);checked_residuals+=1
    with (out/'events.csv').open('x',newline='',encoding='utf-8') as ef,(out/'selected-pair-position-effects.csv').open('x',newline='',encoding='utf-8') as pf:
        ew=csv.writer(ef);pw=csv.writer(pf)
        ew.writerow(['sequence','tick','stage','a','b','operation','reason','slot','previous_slot','feature_a','feature_b','normal_x','normal_y','normal_z','penetration','lambda_before_n','lambda_before_u','lambda_before_v','lambda_after_n','lambda_after_u','lambda_after_v','known_impulses','impulse_ax','impulse_ay','impulse_az','impulse_bx','impulse_by','impulse_bz','dpa_x','dpa_y','dpa_z','dpb_x','dpb_y','dpb_z'])
        pw.writerow(['sequence','tick','observed_pair_a','observed_pair_b','slot','feature_a','feature_b','operation_pair_a','operation_pair_b','own_pair','delta_depth','separation_gain','reverses_separation'])
        for seq,r in enumerate(rows(source/'observer-events.bin',ER)):
            e=r.event;assert 0<=e.operation<len(OPS),'unknown operation enum'
            if current_tick!=r.tick:finish_tick(current_tick);current_tick=r.tick;latest={};normal_cache={};totals={};first_depth={}
            operations[OPS[e.operation]]+=1
            key=':'.join(map(str,sorted((r.a,r.b))));event_pairs.setdefault(key,collections.Counter())[OPS[e.operation]]+=1
            if e.operation==19:reasons[f'{r.a}:{e.reason}']+=1
            dpa=sub(vec(e.afterA.position),vec(e.beforeA.position));dpb=sub(vec(e.afterB.position),vec(e.beforeB.position))
            ew.writerow([seq,r.tick,r.stage,r.a,r.b,OPS[e.operation],e.reason,e.slot,e.previousSlot,e.featureA,e.featureB,*vec(e.normal),e.penetration,*e.accumulatedBefore,*e.accumulatedAfter,int(e.appliedImpulsesKnown),*vec(e.linearImpulseA),*vec(e.linearImpulseB),*dpa,*dpb])
            latest[r.a]=state(e.beforeA,order);latest[r.b]=state(e.beforeB,order)
            if e.operation==4:
                assert quat(e.beforeA.orientation,order)==quat(e.afterA.orientation,order) and quat(e.beforeB.orientation,order)==quat(e.afterB.orientation,order),'Position operation rotated body'
                for k,c in contact_at_solve.get(r.tick,{}).items():
                    a,b,slot,fa,fb=k
                    if not ({a,b}&{r.a,r.b}):continue
                    if a not in latest or b not in latest:raise ValueError('missing incident body state for position reconstruction')
                    if k not in normal_cache:
                        normal_local=rotate(inverse(solveq[r.tick][a]),c['normal']);normal_cache[k]=rotate(latest[a]['orientation_wxyz'],normal_local)
                    normal=normal_cache[k];wa=worldpoint(a,c['localA'],latest[a]);wb=worldpoint(b,c['localB'],latest[b]);depth=-dot(sub(wa,wb),normal)
                    if k not in first_depth:first_depth[k]=depth
                    delta={r.a:dpa,r.b:dpb};effect=-dot(sub(delta.get(a,[0,0,0]),delta.get(b,[0,0,0])),normal)
                    totals[k]=totals.get(k,0)+effect;own={r.a,r.b}=={a,b};pairkey=':'.join(map(str,sorted((a,b))))
                    ps=correction_summary.setdefault(pairkey,{'incident_position_operations':0,'own_separation_gain_sum':0.,'other_separation_gain_sum':0.,'reversing_operations':0,'max_single_depth_increase':0.})
                    ps['incident_position_operations']+=1;ps['own_separation_gain_sum' if own else 'other_separation_gain_sum']-=effect;ps['reversing_operations']+=effect>1e-9;ps['max_single_depth_increase']=max(ps['max_single_depth_increase'],effect)
                    pw.writerow([seq,r.tick,a,b,slot,fa,fb,r.a,r.b,int(own),effect,-effect,int(effect>1e-9)]);correction_rows+=1
            latest[r.a]=state(e.afterA,order);latest[r.b]=state(e.afterB,order)
        finish_tick(current_tick)
    query={'available':False}
    if (source/'observer-query-layout.json').exists():
        verify_queries(load(source/'observer-query-layout.json'));query={'available':True,'finish_outcomes':collections.Counter(),'finish_reasons':collections.Counter(),'gjk_terminations':collections.Counter(),'epa_terminations':collections.Counter(),'admissions':collections.Counter(),'selected_pair_outcomes':collections.Counter(),'rows':0}
        with (out/'queries.csv').open('x',newline='',encoding='utf-8') as f:
            w=csv.writer(f);w.writerow(['tick','a','b','kind','query','role','outcome','reason','gjk_termination','gjk_iterations','epa_termination','epa_iterations','separation','toi','patch_count','admission'])
            for r in rows(source/'observer-queries.bin',QR):
                e=r.event;query['rows']+=1;w.writerow([r.tick,r.a,r.b,e.kind,e.context.query,e.context.role,e.outcome,e.reason,e.diagnostics.termination,e.diagnostics.iterations,e.diagnostics.epaTermination,e.diagnostics.epaIterations,e.contact.separationDistance,e.contact.timeOfImpact,e.patchCount,e.admission])
                if e.kind==1:
                    query['finish_outcomes'][str(e.outcome)]+=1;query['finish_reasons'][str(e.reason)]+=1;query['gjk_terminations'][str(e.diagnostics.termination)]+=1;query['epa_terminations'][str(e.diagnostics.epaTermination)]+=1
                    if selected in (r.a,r.b):query['selected_pair_outcomes'][f'{min(r.a,r.b)}:{max(r.a,r.b)}:{e.outcome}']+=1
                elif e.kind==2:query['admissions'][str(e.admission)]+=1
    for b in body_stats.values():
        tail=list(b.pop('tail_120'));b['tail_120']={'count':len(tail),'sleeping_steps':sum(x[2] for x in tail),'max_linear_speed':max(x[0] for x in tail),'max_angular_speed':max(x[1] for x in tail),'min_inactivity':min(x[3] for x in tail),'max_inactivity':max(x[3] for x in tail),'position_component_ranges':[max(x[4][i] for x in tail)-min(x[4][i] for x in tail) for i in range(3)],'quaternion_component_ranges':[max(x[5][i] for x in tail)-min(x[5][i] for x in tail) for i in range(4)]}
    presentation={'available':False}
    if (source/'observer-render.csv').exists():
        presentation={'available':True,'rows':0,'maximum_render_translation_delta_from_authoritative':0.,'per_body_maximum_delta':{},'interpretation':'Render matrix translation versus current raw body position. Nonzero differences can be expected interpolation; not evidence of a presentation bug by itself.'}
        with (source/'observer-render.csv').open(newline='',encoding='utf-8') as f:
            for r in csv.DictReader(f):
                delta=norm([float(r['matrix_'+str(12+i)])-float(r['authoritative_p'+axis]) for i,axis in enumerate('xyz')]);ordinal=r['ordinal'];presentation['rows']+=1
                presentation['maximum_render_translation_delta_from_authoritative']=max(presentation['maximum_render_translation_delta_from_authoritative'],delta)
                presentation['per_body_maximum_delta'][ordinal]=max(presentation['per_body_maximum_delta'].get(ordinal,0),delta)
    report={'status':'DIAGNOSTIC_ANALYSIS_ONLY_NO_QUALIFICATION_PASS','source':str(source),'capture_status':status,'layout_verified':True,'canonical_cache_evidence':cache_summary,'quaternion_memory_order':order,'all_decoded_state_contact_numeric_fields_finite':finite,'selection':{'uuid':uuid,'ordinal':selected,'historical_owner_uuid_recovered':before.get('historical_owner_uuid_recovered'),'pick_matches_selected':before.get('pick_matches_selected'),'pick_readback_pixel':before.get('pick_readback_pixel'),'cursor_screen_at_readback':before.get('cursor_screen_at_readback'),'pick_camera':before.get('pick_camera'),'command_camera':before.get('command_camera'),'fitted_capsule':before.get('fitted_capsule'),'before':initial[selected],'after':afterb[selected]},'state_counts_by_stage':counts,'body_stats':body_stats,'pre_sleep_counts':{str(k):v for k,v in pre_sleep.items()},'sleep_transitions':sleep_changes,'selected_contact_neighbors':sorted(neighbors-{selected}),'contact_rows':contact_rows,'pairs':pair_stats,'event_operations':operations,'event_pair_operations':event_pairs,'sleep_reason_counts_body_reason':reasons,'queries':query,'query_outcome_names':{'0':'Contact','1':'SeparatedValid','2':'Failed'},'presentation':presentation,'position_attribution':{'rows':correction_rows,'selected_pairs':correction_summary,'end_depth_checks':checked_residuals,'maximum_absolute_reconstruction_residual':max_residual,'interpretation':'Double-precision source reconstruction, not bit parity. Positive delta_depth means an incident translation increases selected-contact penetration. Residual includes transport/rounding or unmodeled changes; inspect before causal conclusion.'},'limits':['New captured operation identity is not an archived historical UUID unless independently proven.','Stored-anchor penetration is not independent geometric overlap or certified minimum translation distance.','No threshold changes, remedy validation or performance PASS inferred.','Sleep reason counts are calls, not necessarily actual state transitions.','The pre-replacement ring contains body states; separate canonical initial/last-End/command images include private sleep, derived caches, manifold order and warm-start state. Image presence does not establish cross-run parity; examine strict replay match evidence.','Detailed observation has timing overhead; M/P parity and full snapshot restoration are not established.']}
    assert all(sha(source/name)==digest for name,digest in hashes.items()),'input changed during analysis'
    report['input_sha256']=hashes;report['analyzer_sha256']=sha(Path(__file__));report['cache_decoder_sha256']=sha(Path(__file__).with_name('collider_cache_decode.py'))
    write(out/'summary.json',report)
    print(json.dumps({'output':str(out/'summary.json'),'status':report['status'],'selected_uuid':uuid,'selected_ordinal':selected,'post_steps':status.get('post_steps'),'query_outcomes':query.get('finish_outcomes'),'reconstruction_max_error':max_residual},indent=2))

def main():
    p=argparse.ArgumentParser(__doc__);p.add_argument('capture',type=Path);p.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[1]/'campaign/analysis-01');args=p.parse_args();analyze(args.capture,args.output)
if __name__=='__main__':main()
