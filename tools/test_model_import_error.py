"""Validate the bounded typed model startup-error transport prerequisite."""
import argparse
import hashlib
from pathlib import Path
import sys
from test_runtime_failure import ROOT, Validation
from test_vertex_buffer import startup_lifetimes


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    v=Validation(args.configuration,args.output)
    try:
        v.build(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        source=ROOT/'tools/model_import_error_probe.cpp'
        v.compile('model-error-header',[source],['/DMODEL_IMPORT_SCHEMA_ONLY'],boundary=True)
        app=v.compile('GEngineEditor',[source])
        for mode in ('failure','success'):
            markers=['[PASS] model startup '+mode]
            if mode=='failure':markers+=['Application model import operation=ModelImportFixture::Create code=1 source=Models/fixture.obj: Importer rejected model; detail=invalid face 37']
            log=v.invoke('model-'+mode,[app,mode],expected=1 if mode=='failure' else 0,markers=markers,cwd=v.out/('model-'+mode))
            if 'GEngine application failed:' in log:raise RuntimeError('Model startup failure used legacy catch transport')
        startup_lifetimes(v)  # Real UBO error alternative and exact buffer/root teardown against the extended variant.
        v.smoke(['GEngineEditor','Breakout','RayTracing','RigidBodySimulation'])
        v.report['result']='PASS'
        v.report['engine_library_sha256']=hashlib.sha256((ROOT/'bin'/args.configuration/'GEngine/GEngine.lib').read_bytes()).hexdigest()
        return 0
    except(OSError,RuntimeError) as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()

if __name__=='__main__':sys.exit(main())
