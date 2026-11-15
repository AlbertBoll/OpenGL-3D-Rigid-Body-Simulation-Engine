"""Offline analysis of final-audit-01. No reruns, threshold changes or gate promotion."""
import csv,hashlib,json,math,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'logs/pre_editor/phase-26/jolt-integration-01/final-audit-01'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def stats(values):
    a=sorted(values);n=len(a)
    if not n:return None
    return dict(n=n,mean=statistics.fmean(a),median=statistics.median(a),p95=a[math.ceil(.95*n)-1],p99=a[math.ceil(.99*n)-1],max=a[-1])
def summarize(d):
    rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((d/'samples.csv').open())]
    assert len(rows)==600 and [int(r['tick']) for r in rows]==list(range(1,601))
    assert all(math.isfinite(v) for r in rows for v in r.values())
    for r in rows:
        assert r['scene_ns']>=r['physics_ns']
        assert r['physics_ns']>=sum(r[k] for k in ['adapter_pre_ns','jolt_update_ns','adapter_post_ns'])
        r['scene_sync_ns']=r['scene_ns']-r['physics_ns'] # Paired same-tick residual, not percentile subtraction.
    clocks=[k for k in rows[0] if k.endswith('_ns')]
    result={'source':d.name,'started':json.loads((d/'started.json').read_text()),'result':json.loads((d/'result.json').read_text()),'settings':(d/'settings.txt').read_text(),'initial_sha256':sha(d/'initial-bodies.csv'),'final_sha256':sha(d/'final-bodies.csv'),
        'ms':{k:stats([r[k]/1e6 for r in rows if k!='frame_interval_ns' or r[k]>0]) for k in clocks},
        'allocation_totals':{k:sum(r[k] for r in rows) for k in ['cpp_calls','cpp_bytes','jolt_calls','jolt_bytes','temp_calls','temp_bytes']},
        'temp_peak_bytes':max(r['temp_peak'] for r in rows),'peak_joint_error_m':max(r['joint_error'] for r in rows),'sampled_contact_depth_m':max(r['contact_depth'] for r in rows),'contact_count':stats([r['contacts'] for r in rows]),
        'active':stats([r['active'] for r in rows]),'sleeping_last':rows[-1]['sleeping'],'kinetic':stats([r['kinetic'] for r in rows]),'kinetic_last':rows[-1]['kinetic'],'memory_start':(d/'memory-start.txt').read_text(),'memory_end':(d/'memory-end.txt').read_text()}
    for label,a,b in [('startup',1,120),('pre_kick',121,300),('post_kick',301,600)]:
        selected=[r for r in rows if a<=r['tick']<=b];result[label]={k:stats([r[k]/1e6 for r in selected]) for k in clocks if k!='frame_interval_ns'}
    result['sleep_windows']={str(active):stats([r['physics_ns']/1e6 for r in rows if bool(r['active'])==active]) for active in [False,True]}
    result['max_rolling60_physics_ms']=max(statistics.fmean([r['physics_ns']/1e6 for r in rows[i:i+60]]) for i in range(541))
    result['render_passes']={}
    for p in d.glob('passes-*.csv'):
        passes={}
        for row in csv.DictReader(p.open()):passes.setdefault(row['pass'],[]).append(row)
        for name,records in passes.items():
            # Preserve full records including startup; no pooled CPU/GPU quantile arithmetic.
            result['render_passes'][name]={'cpu_ms':stats([int(x['cpu_ns'])/1e6 for x in records]),'gpu_ms':stats([int(x['gpu_ns'])/1e6 for x in records if x['gpu_status']=='available' and x['gpu_ns']]),'draws':stats([int(x['draws']) for x in records if x['draws_known']=='1'])}
    return result
def ordinary_text(raw):
    # Remove only this campaign's compile-time observer, preserving every unrelated conditional.
    lines=raw.decode('utf-8').splitlines(keepends=True);out=[];i=0
    while i<len(lines):
        line=lines[i];s=line.strip()
        if s in ['#include "Physics/JoltAuditObservation.h"','#include "../tests/JoltFinalAudit.h"']:i+=1;continue
        if s=='#ifdef GENGINE_JOLT_FINAL_AUDIT':
            depth=1;emit=False;i+=1
            while i<len(lines) and depth:
                sub=lines[i].strip()
                if sub.startswith(('#ifdef ','#ifndef ','#if ')):depth+=1
                if sub=='#else' and depth==1:emit=True;i+=1;continue
                if sub=='#endif':depth-=1
                if depth and emit:out.append(lines[i])
                i+=1
            assert depth==0
        else:out.append(line);i+=1
    return ''.join(out).replace('\r\n','\n')
def proof():
    entry=json.loads((BASE/'entry.json').read_text());changes={};snapshot=ROOT/'logs/ui/properties-organization-01/final/source'
    for p,h in entry['source_inputs'].items():
        current=ROOT/p
        if sha(current)==h:continue
        original=snapshot/p
        assert original.exists() and sha(original)==h,p
        same=ordinary_text(current.read_bytes())==original.read_text(encoding='utf-8').replace('\r\n','\n')
        changes[p]={'entry_sha256':h,'audit_sha256':sha(current),'ordinary_text_identical':same}
        assert same,p
    (BASE/'ordinary-source-proof.json').write_text(json.dumps({'accepted_input_count':len(entry['source_inputs']),'unchanged_count':len(entry['source_inputs'])-len(changes),'audit_only_changes':changes,'method':'Remove only GENGINE_JOLT_FINAL_AUDIT branches and empty observer includes; compare complete remaining text with preserved accepted source, normalizing line endings only.'},indent=2)+'\n')
def main():
    results={d.name:summarize(d) for d in sorted(BASE.glob('run-Release-*')) if (d/'complete.txt').exists()}
    assert len(results)==48,len(results)
    (BASE/'measurements.json').write_text(json.dumps(results,indent=2)+'\n')
    initial={};parity={}
    for name,v in results.items():
        stem=name.removeprefix('run-Release-');case=stem.split('-')[0];backend=stem.split('-')[1]
        initial.setdefault(case,set()).add(v['initial_sha256']);parity.setdefault(case+'-'+backend,set()).add(v['final_sha256'])
    (BASE/'parity.json').write_text(json.dumps({'initial_equal_within_case':{k:len(v)==1 for k,v in initial.items()},'final_equal_repeats_and_observation':{k:len(v)==1 for k,v in parity.items()},'initial_hashes':{k:sorted(v) for k,v in initial.items()},'final_hashes':{k:sorted(v) for k,v in parity.items()}},indent=2)+'\n')
    columns=['case','backend','attempt','scene_median_ms','physics_median_ms','physics_p95_ms','physics_p99_ms','physics_max_ms','jolt_update_median_ms','adapter_pre_median_ms','adapter_post_median_ms','scene_sync_median_ms','frame_median_ms','frame_p95_ms','frame_p99_ms','frame_max_ms','joint_error_m','sleeping_final','kinetic_final_J','cpp_calls','jolt_heap_calls','temp_peak_bytes']
    with (BASE/'comparison.csv').open('w',newline='') as f:
        w=csv.writer(f);w.writerow(columns)
        for name,v in results.items():
            stem=name.removeprefix('run-Release-').split('-');m=v['ms'];w.writerow([stem[0],stem[1],'-'.join(stem[2:]),m['scene_ns']['median'],*[m['physics_ns'][s] for s in ['median','p95','p99','max']],m['jolt_update_ns']['median'],m['adapter_pre_ns']['median'],m['adapter_post_ns']['median'],m['scene_sync_ns']['median'],*[m['frame_interval_ns'][s] for s in ['median','p95','p99','max']],v['peak_joint_error_m'],v['sleeping_last'],v['kinetic_last'],v['allocation_totals']['cpp_calls'],v['allocation_totals']['jolt_calls'],v['temp_peak_bytes']])
    proof();print('48 completed runs analyzed; independent distributions; ordinary source proof PASS')
if __name__=='__main__':main()
