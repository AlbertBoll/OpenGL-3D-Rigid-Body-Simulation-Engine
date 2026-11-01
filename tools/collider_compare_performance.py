"""Same-fixture measured comparison. Preserves Phase24 INCONCLUSIVE disposition."""
import hashlib,json,sys
from pathlib import Path

baseline,candidate,output=map(Path,sys.argv[1:4])
a=json.loads((baseline/'ORDINARY_MEASUREMENTS.json').read_text())
b=json.loads((candidate/'ORDINARY_MEASUREMENTS.json').read_text())
checks=[]
def check(ok,label):checks.append(dict(passed=bool(ok),label=label))
check(a['gpu_supported'] and b['gpu_supported'],'GPU interval measurements available:64 samples per case')
cases={}
resources=['draw_calls','instanced_draw_calls','outline_draw_calls','outline_passes','current_scratch_bytes','retained_scratch_bytes','scratch_allocations','selected_mesh_draws','region_width','region_height']
for name,bc in a['cases'].items():
    cc=b['cases'][name]
    check(all(bc.get(k)==cc.get(k) for k in resources),name+': identical measured draw/pass/resource/capacity statistics')
    check(all(s['n']==64 for s in cc.values()),name+': complete64sample protocol')
    for side,c in [('baseline',bc),('candidate',cc)]:
        check(c['scratch_allocations']['median']==c['scratch_allocations']['p99'],name+': '+side+' no steady-state scratch allocations')
    if name in ('none','Esc-empty','cleared-control','final-cleared'):
        check(all(cc[k]['p99']==0 for k in ('outline_draw_calls','outline_passes','current_scratch_bytes')),name+': no-selection zero outline submissions/passes/current scratch')
    if name=='none':check(cc['scratch_allocations']['p99']==cc['retained_scratch_bytes']['p99']==0,'initial no-selection allocates no outline scratch')
    metric={}
    for k in ('outline_gpu_ms','outline_cpu_ms','submission_cpu_ms','render_interval_gpu_ms','render_interval_cpu_ms','preparation_ms','extraction_ms'):
        if k in bc and k in cc:
            metric[k]={p:cc[k][p]-bc[k][p] for p in ('median','p95','p99')}
    cases[name]=dict(baseline=bc,candidate=cc,delta=metric,draw_call_delta_from_no_selection=cc['draw_calls']['median']-b['cases']['none']['draw_calls']['median'])
single='same-frame-1-compiled-composite.frag'
check((baseline/single).read_bytes()==(candidate/single).read_bytes(),'actual single-selection GLSL byte-identical in A/B executables')
report=dict(resource_gates_passed=all(c['passed'] for c in checks),checks=checks,cases=cases,
    baseline=str(baseline),candidate=str(candidate),protocol=b['protocol'],
    performance_disposition='Measured scoped A/B; Phase24 accepted performance INCONCLUSIVE remains unchanged. No formal performance non-regression PASS inferred from pass counts. GPU/CPU median,p95,p99 are descriptive64sample statistics.',
    limitations=['Programmatic ordinary RBS selection; final Owner visual inspection required','Render interval spans application Update through UI/Swap; outline GPU timing isolates existing selection pipeline','No-selection/single production paths and Gold GLSL unchanged; no shader-compilation/readback samples included in timing'],
    inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for d in (baseline,candidate) for p in (d/'ORDINARY_MEASUREMENTS.json',d/single)})
with output.open('x') as f:json.dump(report,f,indent=2)
print(json.dumps(dict(resource_gates_passed=report['resource_gates_passed'],failed=[c for c in checks if not c['passed']],outline_gpu_medians={k:{'before':v['baseline'].get('outline_gpu_ms'),'after':v['candidate'].get('outline_gpu_ms')}for k,v in cases.items() if k in ('none','single-A','two-A-B','three-A-B-C','widely-separated-A-D','four-active-D')}),indent=2))
sys.exit(0 if report['resource_gates_passed'] else 1)
