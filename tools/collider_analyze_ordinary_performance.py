import csv,json,math,statistics,sys
from pathlib import Path
folder=Path(sys.argv[1])
labels=['none','single-A','two-A-B','three-A-B-C','toggle-remove-B','Selected-zero-layers','All-zero-layers','collider-restored','Esc-empty','Scope-Off-two','active-change','widely-separated-A-D','cleared-control','single-B','single-C','single-D','four-active-D','four-active-B','four-active-A','final-cleared']
rows={i:{} for i in range(len(labels))}
def add(k,name,x):rows[k].setdefault(name,[]).append(x)
for r in csv.reader((folder/'highlight-frames.csv').open()):
 k=int(r[1]);tick=int(r[2])
 if not 21<=tick<=84:continue
 if r[0]=='GPU':add(k,'render_interval_gpu_ms',int(r[3])/1e6)
 if r[0]=='CPU':
  names=['render_interval_cpu_ms','preparation_ms','extraction_ms','draw_calls','instanced_draw_calls','outline_draw_calls','outline_passes','current_scratch_bytes','retained_scratch_bytes','scratch_allocations','selected_mesh_draws','region_width','region_height']
  for i,n in enumerate(names):
   v=float(r[3+i]);v=v/1e6 if i==0 else v/1000 if i in (1,2) else v;add(k,n,v)
seen=set()
for r in csv.DictReader((folder/'highlight-passes.csv').open()):
 f=int(r['frame']);k=(f-1)//90;tick=(f-1)%90+1
 if k>=len(labels) or not 21<=tick<=84:continue
 if r['pass'] not in ('selection-total','qualification-cpu-submission'):continue
 tag='outline' if r['pass']=='selection-total' else 'submission'
 key=(f,r['pass'],'cpu')
 if key not in seen:add(k,tag+'_cpu_ms',int(r['cpu_ns'])/1e6);seen.add(key)
 if r['gpu_status']=='available' and r['gpu_ns']:
  key=(f,r['pass'],'gpu')
  if key not in seen:add(k,tag+'_gpu_ms',int(r['gpu_ns'])/1e6);seen.add(key)
def stats(a):
 a=sorted(a);return {'n':len(a),'median':statistics.median(a),'p95':a[max(0,math.ceil(.95*len(a))-1)],'p99':a[max(0,math.ceil(.99*len(a))-1)]}
report={'protocol':'20 warmup +64 measured frames per case; descriptive nearest-rank p95/p99, not full Phase24 qualification. Ordinary docked RBS no/single/multi comparisons; existing Phase24 performance INCONCLUSIVE remains unchanged.','cases':{labels[k]:{name:stats(v) for name,v in r.items()} for k,r in rows.items()},'gpu_supported':all(len(rows[k].get('render_interval_gpu_ms',[]))==64 for k in range(len(labels))),'limitations':['Render interval covers Update through ordinary FrameScheduler including UI and Swap. Timing samples21-84 exclude readbacks at86. Cross-binary style comparison is scoped; no full Phase24 performance qualification','Owner visual acceptance outstanding','64-sample tails do not establish long-run p99']}
with (folder/'ORDINARY_MEASUREMENTS.json').open('x') as f:json.dump(report,f,indent=2)
print(json.dumps({'folder':str(folder),'gpu_supported':report['gpu_supported'],'cases':len(report['cases'])}))
