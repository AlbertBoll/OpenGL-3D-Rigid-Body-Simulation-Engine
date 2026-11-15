"""Bounded opt-in group Transform render probe; uses existing delayed GPU timings."""
import argparse,csv,hashlib,json,os,shutil,subprocess,time,statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--backend',choices=['native','jolt'],default='jolt');a=p.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'logs/ui/scalable-transform-01');out.mkdir(parents=True,exist_ok=False)
    seed=ROOT/'logs/ui/selection-highlight-02/before-ui/runtime/imgui.ini';shutil.copy2(seed,out/'imgui.ini')
    exe=ROOT/'bin/Release/RigidBodySimulation/RigidBodySimulation.exe'
    env=os.environ.copy();env['GENGINE_PASS_TIMING_OUTPUT']=str(out);env['OWNER_REVIEW_TRANSFORM_SUITE']='1';env['OWNER_REVIEW_NO_VSYNC']='1'
    command=[str(exe),'--scene','sphere-lattice','--backend',a.backend]
    identity=dict(command=command,binary_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),settings_sha256=hashlib.sha256(seed.read_bytes()).hexdigest(),
        protocol='Paused authored SphereLattice tick0, same focus(3,8,3), pitch1.30 yaw-.2 distance55; no physics or authored changes. 24 warm+96 measured frames/case. 0,180 selected,180 idle Gizmo,180 T/R/uniform-S previews. Preview cancelled between cases. Existing delayed GPU timestamps; no capture/readback in measurement. Ordinary physical desktop input checked separately. VSync off.')
    (out/'started.json').write_text(json.dumps(identity,indent=2)+'\n');start=time.monotonic()
    with (out/'stdout.log').open('wb') as log:result=subprocess.run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
    frames=list(csv.DictReader((out/'owner-review-frames.csv').open()));cases={r['case'] for r in frames}
    valid=result.returncode==0 and len(cases)==6 and len(frames)==720 and all(r['tick']=='0' and r['physics_ns']=='0' and None not in r for r in frames)
    identity.update(exit_code=result.returncode,seconds=time.monotonic()-start,frames=len(frames),cases=len(cases),protocol_valid=valid)
    gpu=list(csv.DictReader((out/'owner-review-gpu-frame.csv').open()))
    def stats(values):
        values=sorted(values)
        return dict(median=statistics.median(values),p95=values[min(len(values)-1,int(.95*len(values)))],p99=values[min(len(values)-1,int(.99*len(values)))]) if values else None
    summary={}
    for case in sorted(cases):
        rows=[r for r in frames if r['case']==case and r['warm']=='0']
        summary[case]={key:stats([float(r[key])/(1e6 if key.endswith('_ns') else 1) for r in rows]) for key in ['wall_ns','cpu_ns','physics_ns','ui_ns','scene_ns','allocs','bytes','draws','outline_draws','outline_passes','scratch_allocs','instanced_draws','rectangle_draws','rectangle_allocations','buffer_updates','upload_bytes','selected_entities']}
        summary[case]['gpu_ms']=stats([float(r['gpu_frame_ns'])/1e6 for r in gpu if r['case']==case and int(r['local'])>=24])
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (out/'result.json').write_text(json.dumps(identity,indent=2)+'\n');print(json.dumps(identity,indent=2));return 0 if valid else 1
if __name__=='__main__':raise SystemExit(main())
