"""Bounded diagnostic continuation of jolt-integration-01. Never qualifies or publishes."""
import argparse,hashlib,json,os,shutil,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'logs/pre_editor/phase-26/jolt-integration-01/final-audit-01'
MSBUILD=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def write(p,obj):p.write_text(json.dumps(obj,indent=2)+'\n')
def create(p):
    assert p.resolve().is_relative_to(BASE.resolve());p.mkdir(parents=True,exist_ok=False);return p
def build(config,attempt):
    d=create(BASE/f'build-{config}-{attempt}')
    # Only GEngine/RBS is instrumented; dependency builds retain their own configuration.
    out=ROOT/f'bin/{config}/Phase26FinalAudit';inter=ROOT/f'bin-int/{config}/Phase26FinalAudit'
    props=f'''<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
<PropertyGroup Condition="'$(MSBuildProjectName)'=='GEngine' Or '$(MSBuildProjectName)'=='RigidBodySimulation'">
<OutDir>{out.as_posix()}/$(MSBuildProjectName)/</OutDir><IntDir>{inter.as_posix()}/$(MSBuildProjectName)/</IntDir><PostBuildEventUseInBuild>false</PostBuildEventUseInBuild>
</PropertyGroup>
<ItemDefinitionGroup Condition="'$(MSBuildProjectName)'=='GEngine' Or '$(MSBuildProjectName)'=='RigidBodySimulation'"><ClCompile><PreprocessorDefinitions>GENGINE_JOLT_FINAL_AUDIT;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>
</Project>'''
    (d/'isolated.props').write_text(props)
    command=[str(MSBUILD),str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build',f'/p:Configuration={config}','/p:Platform=x64','/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0',f'/p:ForceImportBeforeCppTargets={d/"isolated.props"}','/nr:false','/m:2','/nologo','/v:normal']
    write(d/'command.json',command);start=time.monotonic()
    with (d/'build.log').open('wb') as log:r=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=1200)
    result=dict(exit_code=r.returncode,seconds=time.monotonic()-start)
    if not r.returncode:
        exe=out/'RigidBodySimulation/RigidBodySimulation.exe';result['sha256']=sha(exe)
        for dll in (ROOT/f'bin/{config}/RigidBodySimulation').glob('*.dll'):shutil.copy2(dll,exe.parent/dll.name)
    write(d/'result.json',result);print(result,flush=True);return r.returncode
CASES={'SphereBoxStack':('sphere-box-stack',None),'BoxStack':('box-stack',None),'SphereLattice':('sphere-lattice',None),'DistancePendulum':('distance-pendulum',None),'C4':('distance-chain',4),'C8':('distance-chain',8),'C16':('distance-chain',16),'G8':('distance-chain-contact',8)}
def run(config,case,backend,attempt,observed):
    d=create(BASE/f'run-{config}-{case}-{backend}-{attempt}')
    exe=ROOT/f'bin/{config}/Phase26FinalAudit/RigidBodySimulation/RigidBodySimulation.exe'
    env=os.environ.copy();env['GENGINE_ASSET_ROOT']=str(ROOT/f'bin/{config}/assets');env['GENGINE_FINAL_AUDIT_OUTPUT']=str(d)
    env['GENGINE_PASS_TIMING_OUTPUT']=str(d)
    if observed:env['GENGINE_FINAL_AUDIT_OBSERVED']='1'
    else:env.pop('GENGINE_FINAL_AUDIT_OBSERVED',None)
    scene,links=CASES[case];cmd=[str(exe),'--scene',scene,'--backend',backend]
    if links:cmd+=['--links',str(links)]
    start=time.monotonic();write(d/'started.json',dict(command=cmd,exe_sha256=sha(exe),observed=observed,configuration=config,deadline=180,asset_root=env['GENGINE_ASSET_ROOT']))
    with (d/'stdout.log').open('wb') as log:
        try:r=subprocess.run(cmd,cwd=d,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180);code=r.returncode
        except subprocess.TimeoutExpired:code=-999
    result=dict(exit_code=code,seconds=time.monotonic()-start,complete=(d/'complete.txt').exists());write(d/'result.json',result);print(d.name,result,flush=True)
    return 0 if code==0 and result['complete'] else 1
def checks(config):
    d=create(BASE/f'checks-{config}');exe=ROOT/f'bin/{config}/Phase26FinalAudit/RigidBodySimulation/RigidBodySimulation.exe';results=[]
    for check in ['backend','jolt-parameter','impulse-boundary']:
        start=time.monotonic()
        with (d/(check+'.log')).open('wb') as log:r=subprocess.run([str(exe),'--'+check+'-checks'],cwd=d,stdout=log,stderr=subprocess.STDOUT,timeout=180)
        results.append(dict(check=check,exit_code=r.returncode,seconds=time.monotonic()-start,exe_sha256=sha(exe)))
        write(d/'result.json',results);print(results[-1],flush=True)
        if r.returncode:return r.returncode
    return 0
def lifecycle(config):
    d=create(BASE/f'lifecycle-{config}');exe=ROOT/f'bin/{config}/Phase26FinalAudit/RigidBodySimulation/RigidBodySimulation.exe'
    cmd=[str(exe),'--scene','distance-chain','--links','8','--backend','native','--physics-smoke-ticks','120','--backend-switch-checks','true']
    env=os.environ.copy();env.pop('GENGINE_FINAL_AUDIT_OUTPUT',None);env.pop('GENGINE_FINAL_AUDIT_OBSERVED',None)
    start=time.monotonic();write(d/'started.json',dict(command=cmd,exe_sha256=sha(exe),configuration=config))
    with (d/'stdout.log').open('wb') as log:
        try:r=subprocess.run(cmd,cwd=d,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180);code=r.returncode
        except subprocess.TimeoutExpired:code=-999
    text=(d/'stdout.log').read_text(errors='replace');passed=code==0 and 'BACKEND_SWITCH_CHECKS_PASS' in text
    result=dict(exit_code=code,seconds=time.monotonic()-start,passed=passed);write(d/'result.json',result);print(result,flush=True);return 0 if passed else 1
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['build','run','checks','matrix','lifecycle']);p.add_argument('--config',choices=['Debug','Release'],default='Release');p.add_argument('--attempt',default='01');p.add_argument('--case',choices=CASES,default='SphereBoxStack');p.add_argument('--backend',choices=['native','jolt'],default='jolt');p.add_argument('--observed',action='store_true');a=p.parse_args()
    if a.action=='build':return build(a.config,a.attempt)
    if a.action=='checks':return checks(a.config)
    if a.action=='lifecycle':return lifecycle(a.config)
    if a.action=='run':return run(a.config,a.case,a.backend,a.attempt,a.observed)
    for attempt,observed in [('timing-a',False),('timing-b',False),('observed',True)]:
        for case in CASES:
            for backend in (['native','jolt'] if attempt!='timing-b' else ['jolt','native']):
                if run(a.config,case,backend,attempt,observed):return 1
    return 0
if __name__=='__main__':raise SystemExit(main())
