"""Focused scalable Transform checks; no qualification or authored fixture mutation."""
import argparse,hashlib,json,os,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--config',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--max-size',type=int,default=4096)
    parser.add_argument('--checks',nargs='+',default=['hierarchy','selection-batch','transform-shortcut','editor-views','owner05','snap','continuous-transform','scalable-transform'])
    args=parser.parse_args();out=args.output.resolve();assert out.is_relative_to(ROOT/'logs/ui/scalable-transform-01');out.mkdir(parents=True,exist_ok=False)
    exe=ROOT/f'bin/{args.config}/RigidBodySimulation/RigidBodySimulation.exe';identity=hashlib.sha256(exe.read_bytes()).hexdigest();results=[]
    env=os.environ.copy();env['GENGINE_SCALABLE_MAX']=str(args.max_size)
    for check in args.checks:
        start=time.monotonic()
        with (out/(check+'.log')).open('w') as log:
            result=subprocess.run([str(exe),'--'+check+'-checks'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=600)
        entry=dict(check=check,exit_code=result.returncode,seconds=time.monotonic()-start,binary_sha256=identity,configuration=args.config,max_size=args.max_size,drag_samples=int(env.get('GENGINE_SCALABLE_DRAG_SAMPLES','0')),external_body=bool(env.get('GENGINE_SCALABLE_EXTERNAL')))
        results.append(entry);(out/'result.json').write_text(json.dumps(results,indent=2)+'\n');print(json.dumps(entry),flush=True)
        if result.returncode:return 1
    return 0
if __name__=='__main__':raise SystemExit(main())
