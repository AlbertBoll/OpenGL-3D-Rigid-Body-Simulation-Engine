"""Validate the bounded Point3D coordinate-error and private KDTree caller slice."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from test_runtime_failure import ROOT,Validation


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--reuse-build',type=Path)
    args=parser.parse_args();v=Validation(args.configuration,args.output)
    sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    path=ROOT/'logs/rendering/phase66/spatial-point/validation-inputs.json';inputs=json.loads(path.read_text())
    build_inputs={p:r['sha256'] for p,r in inputs.items() if not p.startswith('tools/')}
    try:
        if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift before validation')
        v.report['build_inputs']=build_inputs
        if args.reuse_build:
            prior=json.loads(args.reuse_build.read_text());build=next(s for s in prior['steps'] if s['name']=='build')
            if build['result']!='PASS' or prior['configuration']!=args.configuration or prior['build_inputs']!=build_inputs:
                raise RuntimeError('Build reuse input mismatch')
            if prior['engine_library_sha256']!=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib'):raise RuntimeError('Library mismatch')
            v.report['steps'].append(dict(build,reuse=str(args.reuse_build.resolve())))
        else:v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report['engine_library_sha256']=sha(ROOT/'bin'/args.configuration/'GEngine/GEngine.lib');v.save()
        source=ROOT/'tools/spatial_point_probe.cpp'
        v.compile('point-header',[source],['/DSPATIAL_POINT_SCHEMA_ONLY'],boundary=True)
        probe=v.compile('point',[source]);v.invoke('point',[probe],markers=['[PASS] coordinate range/payload golden-tree nodes=7 cycles=2'])
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        if any(sha(ROOT/p)!=r['sha256'] for p,r in inputs.items()):raise RuntimeError('Input drift during validation')
        v.report.update(result='PASS',validation_input_sha256=sha(path));return 0
    except(OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
