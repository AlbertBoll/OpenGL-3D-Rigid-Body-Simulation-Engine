"""Pinned Phase66 validation: RigidBodySimulation is the sole runtime workload."""
import argparse, hashlib, json, sys
from pathlib import Path
from test_runtime_failure import Validation, ROOT

class RbsValidation(Validation):
    def run_rbs(self, name, executable, mode='success', expected=0, markers=()):
        executable=Path(executable).resolve()
        if executable.name!='RigidBodySimulation.exe':
            raise ValueError('Phase66 runtime acceptance requires RigidBodySimulation.exe')
        return self.invoke(name,[executable,mode],expected=expected,markers=markers,
            timeout=240,cwd=self.out/name)
    def smoke(self,*args,**kwargs):
        raise ValueError('Generic application smokes are forbidden by the RBS-only amendment')
    def build_rbs(self):
        self.build(['RigidBodySimulation'])
    def instrumented_rbs(self):
        loader=ROOT/'logs/rendering/phase66/ui-failure-retirement'/f'{self.config}-before/glad-renamed.obj'
        self.report['loader_identity']={'path':str(loader),'sha256':hashlib.sha256(loader.read_bytes()).hexdigest()}
        return self.compile('RigidBodySimulation',[ROOT/'tools/rbs_phase66_validation.cpp',
            ROOT/'RigidBodySimulation/src/main.cpp',loader],['/I'+str(ROOT/'RigidBodySimulation/include')])

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration',choices=['Debug','Release'],required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();v=RbsValidation(args.configuration,args.output)
    try:
        v.build_rbs();exe=v.instrumented_rbs()
        log=v.run_rbs('success',exe,markers=['[PASS] RBS success','owner=1 root=0 contexts=0 SDL=0 TTF=0'])
        if 'GEngine application failed:' in log:raise RuntimeError('Unexpected legacy catch')
        v.report.update(result='PASS',runtime_workload='Maintained RigidBodySimulation only',
            instrumentation='Test-only; real Initialize/Run/ProcessInput/Update/Render/UI; stop after2 completed frames; no production project inclusion')
        return 0
    except Exception as error:
        v.report.update(result='FAIL',reason=str(error));print(error,flush=True);return 1
    finally:v.save()
if __name__=='__main__':sys.exit(main())
