"""Exact passive state regression against an explicitly supplied accepted control."""
import argparse,csv,json,math
from pathlib import Path
import collider_capture_analysis as a
import collider_semantic_compare as sem
import collider_passive_analysis as passive

def characterize(path,count):
    status=a.load(path/'passive-status.json');ready=a.load(path/'passive-ready.json')
    passive.verify_layout(a.load(path/'passive-layout.json'))
    assert status['status']=='CHARACTERIZATION_COMPLETE' and not status['failure']
    assert status['start_steps']==0 and status['end_steps']==1800
    assert ready['body_count']==count and ready['physics_query_sinks']==False
    bodies=a.load(path/'passive-initial-scene.json')['bodies']
    dynamic={b['ordinal'] for b in bodies if b['inverse_mass']>0}
    tail={i:dict(position=[],linear=[],angular=[],sleeping=0) for i in dynamic}
    finite=True;rows=0;seen=set();final_sleeping=0
    for r in a.rows(path/'passive-states.bin',a.SR):
        rows+=1;assert r.stage==11 and 0<=r.tick<1800 and 0<=r.body<count
        assert (r.tick,r.body) not in seen;seen.add((r.tick,r.body))
        s=r.state;finite&=all(math.isfinite(x) for x in a.vec(s.position)+a.vec(s.linear)+a.vec(s.angular)+list(s.orientation)+[s.inactive])
        if r.tick==1799 and r.body in dynamic:final_sleeping+=bool(s.sleeping)
        if r.tick>=1680 and r.body in dynamic:
            t=tail[r.body];t['position'].append(a.vec(s.position));t['linear'].append(a.norm(a.vec(s.linear)));t['angular'].append(a.norm(a.vec(s.angular)));t['sleeping']+=bool(s.sleeping)
    assert rows==count*1800 and finite
    for i,t in tail.items():
        t['position_ranges']=[max(v[k] for v in t['position'])-min(v[k] for v in t['position']) for k in range(3)]
        t['max_linear_speed']=max(t['linear']);t['max_angular_speed']=max(t['angular']);t['samples']=len(t['linear'])
        del t['position'],t['linear'],t['angular']
    with (path/'passive-frames.csv').open(newline='') as f:
        frames=[{k:float(v) if k in ('elapsed_seconds','pending_seconds','discarded_seconds','total_discarded_seconds','max_linear','max_angular') else int(v) for k,v in r.items()}for r in csv.DictReader(f)]
    assert len(frames)==1800 and all(r['steps_last_update']==1 and not r['paused'] and not r['total_discarded_seconds'] for r in frames)
    return dict(status=status,ready=ready,finite=True,body_count=count,dynamic_count=len(dynamic),final_sleeping=final_sleeping,rows=rows,tail_120=tail,raw_timing=passive.profile_report(frames),hashes={p.name:a.sha(p) for p in path.glob('passive-*')})

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('control',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--preset',required=True,choices=('SphereLattice','BoxStack','SphereBoxStack'));p.add_argument('--output',required=True,type=Path);args=p.parse_args()
    count={'SphereLattice':185,'BoxStack':21,'SphereBoxStack':22}[args.preset]
    before=characterize(args.control,count);after=characterize(args.candidate,count)
    comparison=sem.compare(args.control/'passive-states.bin',args.candidate/'passive-states.bin',a.SR)
    report=dict(passed=comparison['semantic_match'],preset=args.preset,state_comparison=comparison,control=before,candidate=after,oracle_sha256=a.sha(Path(__file__)),limits=['Exact comparison proves observed integration non-regression only. Baseline residual motion and outstanding contact findings are not repaired or reclassified.','Raw timing is descriptive, not Phase25 qualification. Owner visual acceptance remains mandatory.'])
    a.write(args.output,report)
    print(json.dumps(dict(passed=report['passed'],rows=comparison['left_rows'],differences=comparison['rows_different'])))
    raise SystemExit(0 if report['passed'] else 1)
