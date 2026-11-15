"""Snapshot a bounded settled tail of existing delayed pass timings; no app input."""
import argparse,csv,io,json,statistics,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('session',type=Path);p.add_argument('case');p.add_argument('--mark',action='store_true');a=p.parse_args()
folder=a.session.resolve();assert folder.is_relative_to(ROOT/'logs/ui/impulse-editor-01')
source=folder/'passes-1.csv';dest=folder/(a.case+'-timing.json');assert not dest.exists()
marker=folder/(a.case+'-marker.json')
if a.mark:
    assert not marker.exists();marker.write_text(json.dumps(dict(bytes=source.stat().st_size,utc_seconds=time.time()),indent=2)+'\n');print(marker);raise SystemExit(0)
# The live CRT writer denies readers. Mark its flushed byte length and analyze only
# that prefix after clean shutdown; do not change sharing or synchronize the GPU.
data=source.read_bytes()[:json.loads(marker.read_text())['bytes']].decode();lines=data.splitlines();header=lines[0]
rows=list(csv.DictReader(io.StringIO(header+'\n'+'\n'.join(lines[-30000:-1]))))
last=max(int(r['frame']) for r in rows);lo,hi=last-360,last-60
rows=[r for r in rows if lo<=int(r['frame'])<hi and r['completed']=='1']
passes={}
def stats(v):
    v=sorted(v);return dict(n=len(v),median=statistics.median(v),p95=v[int(.95*(len(v)-1))],p99=v[int(.99*(len(v)-1))]) if v else None
for name in sorted({r['pass'] for r in rows}):
    values=[r for r in rows if r['pass']==name]
    passes[name]=dict(cpu_ms=stats([int(r['cpu_ns'])/1e6 for r in values]),gpu_ms=stats([int(r['gpu_ns'])/1e6 for r in values if r['gpu_status']=='available' and r['gpu_ns']]),draws=stats([int(r['draws']) for r in values if r['draws_known']=='1']))
(folder/(a.case+'-samples.csv')).write_text(header+'\n'+'\n'.join(','.join(r[h] for h in header.split(',')) for r in rows)+'\n')
dest.write_text(json.dumps(dict(case=a.case,source=str(source.relative_to(ROOT)),frames=[lo,hi],passes=passes),indent=2)+'\n')
print(dest)
