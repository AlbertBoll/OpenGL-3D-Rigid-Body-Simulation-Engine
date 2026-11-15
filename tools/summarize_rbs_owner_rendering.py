"""Summarize recorded paused rendering cases; does not invent a performance gate."""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path


def distribution(values):
    values=sorted(values)
    if not values:
        return None
    return dict(n=len(values),median=statistics.median(values),
                p95=values[math.ceil(.95*len(values))-1],p99=values[math.ceil(.99*len(values))-1],max=max(values))


def summarize(folder):
    frames=list(csv.DictReader((folder/'owner-review-frames.csv').open()))
    passes=list(csv.DictReader((folder/'passes-1.csv').open()))
    gpu=list(csv.DictReader((folder/'owner-review-gpu-frame.csv').open()))
    by_frame={r['frame']:r for r in frames}
    result={'identity':json.loads((folder/'result.json').read_text()),'cases':{},
            'note':'ms; nearest-rank p95/p99; 96 steady frames/case. GPU frame span includes idle/present pacing. C++ allocation counts cover current thread only.'}
    for case in dict.fromkeys(r['case'] for r in frames):
        all_rows=[r for r in frames if r['case']==case]
        rows=[r for r in all_rows if r['warm']=='0']
        data={}
        for field in ('wall_ns','cpu_ns','physics_ns','ui_ns','sync_ns','draft_ns','pick_read_ns','scene_ns'):
            data[field.replace('_ns','_ms')]=distribution([int(r[field])/1e6 for r in rows])
        for field in ('allocs','bytes','ui_allocs','draft_calls','draws','pick_draws','outline_draws','outline_passes'):
            data[field]=distribution([int(r[field]) for r in rows])
        data['scratch_allocations_in_steady']=int(rows[-1]['scratch_allocs'])-int(rows[0]['scratch_allocs'])
        data['viewport_sizes']=sorted(set((r['viewport_w'],r['viewport_h']) for r in rows))
        data['regions']=sorted(set((r['region_x'],r['region_y'],r['region_w'],r['region_h']) for r in rows))
        data['ticks']=sorted(set(r['tick'] for r in rows))
        data['gpu_frame_span_ms']=distribution([int(r['gpu_frame_ns'])/1e6 for r in gpu if r['case']==case and int(r['local'])>=24])
        data['passes']={}
        for p in passes:
            f=by_frame.get(p['frame'])
            if not f or f['case']!=case or f['warm']!='0':continue
            data['passes'].setdefault(p['pass'],[]).append(p)
        for name,ps in data['passes'].items():
            data['passes'][name]={'cpu_ms':distribution([int(p['cpu_ns'])/1e6 for p in ps]),
                                 'gpu_ms':distribution([int(p['gpu_ns'])/1e6 for p in ps if p['gpu_status']=='available']),
                                 'draws':distribution([int(p['draws']) for p in ps]),
                                 'gpu_status':{s:sum(p['gpu_status']==s for p in ps) for s in set(p['gpu_status'] for p in ps)}}
        data['one_shot_pick_frames']=[r for r in all_rows if int(r['pick_draws'])]
        result['cases'][case]=data
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);args=parser.parse_args()
    result=summarize(args.folder)
    (args.folder/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    for case,data in result['cases'].items():
        selection=data['passes'].get('selection-total',{}).get('gpu_ms')
        print(case,'CPU',round(data['cpu_ms']['median'],3),'selection GPU',round(selection['median'],3) if selection else 0,
              'picks',len(data['one_shot_pick_frames']))
