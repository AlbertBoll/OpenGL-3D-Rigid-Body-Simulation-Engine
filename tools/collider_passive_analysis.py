"""Passive completed-frame end-state and raw timing characterization. No acceptance thresholds."""
import argparse,collections,csv,ctypes as C,hashlib,math
from pathlib import Path
import collider_capture_analysis as a
import collider_semantic_compare as sem

def quantiles(values):
    if not values:return {'samples':0}
    x=sorted(values);n=len(x);at=lambda p:x[max(0,math.ceil(p*n)-1)]
    return {'samples':n,'min':x[0],'median':(x[n//2]+x[(n-1)//2])/2,'p95':at(.95),'p99':at(.99) if n>=1000 else None,'max':x[-1],'mean':sum(x)/n}

def verify_layout(layout):
    for k,v in [('row_size',C.sizeof(a.SR)),('tick_offset',a.SR.tick.offset),('stage_offset',a.SR.stage.offset),('body_offset',a.SR.body.offset),('state_offset',a.SR.state.offset),('state_size',C.sizeof(a.BS)),('end_stage',11)]:assert layout[k]==v,(k,layout[k],v)
    for k,v in layout['state_fields'].items():assert getattr(a.BS,k).offset==v

def profile_report(frames):
    columns=('physics_ns','broadphase_ns','narrowphase_ns','contacts_ns','islands_ns','solver_ns','resolution_ns','gravity_ns','integration_ns')
    valid=[r for r in frames if r['post_steps']<=1800];overshoot=[r for r in frames if r['post_steps']>1800];enabled=all(r['profile_enabled']==1 for r in valid)
    for r in valid:
        if r['profile_enabled']:assert r['profile_steps']==r['steps_last_update'],'profile/scheduler sample mismatch'
    one=[r for r in valid if r['steps_last_update']==1];two=[r for r in valid if r['steps_last_update']==2];steps=sum(r['steps_last_update'] for r in valid)
    return {'profiling_available_for_all_primary_rows':enabled,'primary_frame_rows':len(valid),'zero_step_rows':sum(r['steps_last_update']==0 for r in valid),'one_step_rows':len(one),'two_step_rows':len(two),'other_step_count_rows':sum(r['steps_last_update'] not in (0,1,2) for r in valid),'primary_physics_steps':steps,'terminal_overshoot_rows':len(overshoot),'terminal_overshoot_physics_steps':sum(r['steps_last_update'] for r in overshoot),'single_physics_step_raw_ns':{c:quantiles([r[c] for r in one]) for c in columns} if enabled else {},'two_step_batch_mean_ns_per_step_diagnostic':{c:quantiles([r[c]/2 for r in two]) for c in columns} if enabled else {},'weighted_raw_ns_per_step_all_primary_batches':{c:sum(r[c] for r in valid)/steps for c in columns} if enabled and steps else {},'scene_update_raw_ns_by_step_count':{str(k):quantiles([r['scene_update_ns'] for r in valid if r['steps_last_update']==k]) for k in (0,1,2)},'quantile_method':'Median averages central pair; p95 and p99 nearest-rank ceil(p*n). p99 only when n>=1000. Maximum is descriptive, not a new gate.','limits':['Raw diagnostic elapsed nanoseconds, including existing profiler cost; no observer-overhead subtraction.','Only one-step updates identify individual Physics-step samples. Two-step sums are reported separately as batch means, not individual-step p95.','Scene.Update timing includes non-Physics Scene synchronization, while PhysicsProfile stages are separate; rendering is outside this timer.','Stage timers can overlap/nest; do not sum them to reconstruct total.','No controlled CPU qualification, matched performance budget, or Phase25 gate is inferred. Terminal overshoot rows are disclosed separately.']}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('capture',type=Path);ap.add_argument('--reference',type=Path);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();source=args.capture.resolve();out=args.output.resolve();root=Path(__file__).resolve().parents[1];assert out.is_relative_to(root/'campaign') and not out.exists();status=a.load(source/'passive-status.json');ready=a.load(source/'passive-ready.json');layout=a.load(source/'passive-layout.json');verify_layout(layout);initial=a.load(source/'passive-initial-scene.json');bodies={b['ordinal']:b for b in initial['bodies']};start=status['start_steps'];limit=start+1800;assert status['status']=='CHARACTERIZATION_COMPLETE';hashes={p.name:a.sha(p) for p in source.iterdir() if p.is_file() and p.name.startswith('passive-')};frames=[]
    with (source/'passive-frames.csv').open(newline='',encoding='utf-8') as f:
        for row in csv.DictReader(f):frames.append({k:float(v) if k in ('elapsed_seconds','pending_seconds','discarded_seconds','total_discarded_seconds','max_linear','max_angular') else int(v) for k,v in row.items()})
    expected_ticks={r['total_steps']-1 for r in frames if r['steps_last_update'] and r['post_steps']<=1800};overshoot_ticks={r['total_steps']-1 for r in frames if r['steps_last_update'] and r['post_steps']>1800};raw=list(a.rows(source/'passive-states.bin',a.SR));candidates={'wxyz','xyzw'}
    for r in raw:
        if bodies[r.body]['inverse_mass']==0:candidates={o for o in candidates if a.quat(r.state.orientation,o)==bodies[r.body]['orientation_wxyz']}
    assert len(candidates)==1,'ambiguous passive quaternion memory order';order=candidates.pop();stats={};seen=set();finite=True;encoded={};_,spans=sem.fields(a.SR)
    for r in raw:
        assert r.stage==11 and r.body in bodies and (r.tick,r.body) not in seen;seen.add((r.tick,r.body));assert r.tick in expected_ticks|overshoot_ticks
        if r.tick not in expected_ticks:continue
        s=r.state;v=a.norm(a.vec(s.linear));w=a.norm(a.vec(s.angular));values=a.vec(s.position)+a.vec(s.linear)+a.vec(s.angular)+list(s.orientation)+[s.inactive];finite&=all(math.isfinite(x) for x in values)
        q=stats.setdefault(r.body,{'observed_End_samples':0,'sleeping_observations':0,'max_linear_speed':0.,'max_angular_speed':0.,'observed_sleep_suffix_start':None,'last':None,'tail':[]});q['observed_End_samples']+=1;q['sleeping_observations']+=bool(s.sleeping);q['max_linear_speed']=max(q['max_linear_speed'],v);q['max_angular_speed']=max(q['max_angular_speed'],w)
        if not s.sleeping:q['observed_sleep_suffix_start']=None
        elif q['observed_sleep_suffix_start'] is None:q['observed_sleep_suffix_start']=r.tick
        q['last']=a.state(s,order)
        if r.tick>=start+1680:q['tail'].append((v,w,bool(s.sleeping),a.vec(s.position),a.quat(s.orientation,order),s.inactive))
        data=C.string_at(C.addressof(r),C.sizeof(r));encoded[(r.tick,r.body)]=b''.join(data[o:o+n] for o,n in spans)
    assert {tick for tick,body in encoded}==expected_ticks and len(encoded)==len(expected_ticks)*len(bodies),'incomplete passive batch-end body rows'
    for q in stats.values():
        tail=q.pop('tail');q['tail_120_physics_steps_observed']={'samples':len(tail),'sleeping_observations':sum(x[2] for x in tail),'max_linear_speed':max((x[0] for x in tail),default=None),'max_angular_speed':max((x[1] for x in tail),default=None),'position_ranges':[max(x[3][i] for x in tail)-min(x[3][i] for x in tail) for i in range(3)] if tail else [],'quaternion_ranges':[max(x[4][i] for x in tail)-min(x[4][i] for x in tail) for i in range(4)] if tail else [],'inactive_range':[min(x[5] for x in tail),max(x[5] for x in tail)] if tail else []}
    parity={'requested':False}
    if args.reference:
        ref=args.reference.resolve();a.verify_layout(a.load(ref/'observer-layout.json'));reference={}
        for r in a.rows(ref/'observer-states.bin',a.SR):
            if r.stage==11 and start<=r.tick<limit:
                key=(r.tick,r.body);assert key not in reference;data=C.string_at(C.addressof(r),C.sizeof(r));reference[key]=b''.join(data[o:o+n] for o,n in spans)
        missing=[k for k in encoded if k not in reference];different=[k for k,v in encoded.items() if k in reference and reference[k]!=v];parity={'requested':True,'reference':str(ref),'reference_states_sha256':a.sha(ref/'observer-states.bin'),'compared_observed_End_rows':len(encoded),'reference_full_window_End_rows':len(reference),'unobserved_reference_rows_due_to_batch_sampling':len(reference)-len(encoded),'terminal_overshoot_rows_excluded':len(seen)-len(encoded),'missing_reference_rows':missing[:20],'different_rows':len(different),'first_different_tick_body':different[:20],'exact_observed_semantic_field_match':not missing and not different,'comparison':'Exact recorded tick/stage/body/BodyState scalar bits, including signed zero; native padding excluded. Only completed Scene.Update end states are available; intermediate steps in a two-step batch are unobserved.'}
    assert all(a.sha(source/name)==digest for name,digest in hashes.items()),'passive evidence changed during analysis';out.mkdir(parents=True);report={'status':'PASSIVE_CHARACTERIZATION_NO_QUALIFICATION_PASS','source':str(source),'native_status':status,'ready':ready,'layout_verified':True,'quaternion_order':order,'finite_raw_state_fields':finite,'observed_End_ticks':len(expected_ticks),'body_stats':stats,'parity':parity,'raw_timing':profile_report(frames),'input_sha256':hashes,'script_sha256':a.sha(Path(__file__)),'parser_sha256':a.sha(Path(a.__file__)),'limits':['No Physics/query sinks operate in the passive window, but existing profiler and post-update copied-state/inertia sampling remain.','No contacts or intermediate batched-step states are recorded passively.','Observer parity at sampled endpoints does not establish full whole-program equivalence.','Diagnostic CPU and presentation findings do not constitute performance qualification or final Owner visual acceptance.']};a.write(out/'passive-analysis.json',report);print({'output':str(out/'passive-analysis.json'),'observed_ticks':len(expected_ticks),'finite':finite,'parity':parity,'one_step_cpu_samples':report['raw_timing']['one_step_rows']})
if __name__=='__main__':main()
