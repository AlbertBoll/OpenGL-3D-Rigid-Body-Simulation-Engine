"""Selection correction timing summary, including one-shot rectangle release frames."""
import argparse
import csv
import json
from pathlib import Path
from summarize_rbs_owner_rendering import distribution, summarize


def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);args=parser.parse_args()
    result=summarize(args.folder)
    frames=list(csv.DictReader((args.folder/'owner-review-frames.csv').open()))
    passes=list(csv.DictReader((args.folder/'passes-1.csv').open()))
    present={r['frame']:int(r['cpu_ns']) for r in passes if r['pass']=='present'}
    for case,data in result['cases'].items():
        rows=[r for r in frames if r['case']==case and r['warm']=='0']
        for field in ('shadow_draws','color_draws','visible','culled','instances','imgui_fps','imgui_samples','swap_interval','sync_allocs','draft_allocs','selected_entities','instanced_draws','rectangle_draws','rectangle_allocations','buffer_updates','upload_bytes','framebuffer_binds','render_passes'):
            data[field]=distribution([float(r[field]) for r in rows])
        data['rectangle_release_frames']=[r for r in frames if r['case']==case and int(r['rectangle_draws'])]
        data['raw_delta_ms']=distribution([float(r['raw_delta_ns'])/1e6 for r in rows])
        data['cpu_without_present_ms']=distribution([(int(r['cpu_ns'])-present.get(r['frame'],0))/1e6 for r in rows])
    (args.folder/'summary-selection.json').write_text(json.dumps(result,indent=2)+'\n')
    for case,d in result['cases'].items():
        def value(name):
            v=d[name];return f"{v['median']:.3f}/{v['p95']:.3f}/{v['p99']:.3f}"
        p=d['passes'].get('present',{}).get('cpu_ms')
        print(case,'wall',value('wall_ms'),'CPU excl present',value('cpu_without_present_ms'),
              'step',value('physics_ms'),'present',round(p['median'],3) if p else None,
              'shadow/color/visible',d['shadow_draws']['median'],d['color_draws']['median'],d['visible']['median'])


if __name__=='__main__':main()
