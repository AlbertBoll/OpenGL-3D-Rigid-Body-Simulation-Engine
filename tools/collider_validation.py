"""Bounded opt-in Collider qualification. Never promotes or grants Owner acceptance."""
import argparse,contextlib,datetime,hashlib,importlib.util,json,os,shutil,subprocess,sys,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT
OUT=ROOT/'logs/collider-validation/local'
AUDIT=OUT
BASE='118e4e1e544ef2e26ec5930cd9c7f56a8cf6b0d3'
sys.dont_write_bytecode=True
sys.path.insert(0,str(SOURCE/'tools'))
import pre_editor_resting_contact_diagnosis as owned
from pre_editor_phase20_comprehensive import NativeOwner
now=lambda:datetime.datetime.now(datetime.timezone.utc).isoformat()
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(path,value):
    with Path(path).open('x',encoding='utf-8') as f:json.dump(value,f,indent=2);f.write('\n')
def git(*args):
    return subprocess.run(['git','--no-optional-locks','-C',str(ROOT),*args],capture_output=True,check=True).stdout
def guard():
    assert OUT.resolve().is_relative_to((ROOT/'logs/collider-validation').resolve())
    assert git('rev-parse','--is-inside-work-tree').decode().strip()=='true'

def inputs():
    names=[]
    for tree in ('GEngine/include','GEngine/src','RigidBodySimulation/include','RigidBodySimulation/src','RigidBodySimulation/tests','external/glad/include'):
        names += [p.relative_to(ROOT) for p in (ROOT/tree).rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.cpp','.c','.inl')]
    names += [Path(p) for p in ('GEngine/GEngine.vcxproj','RigidBodySimulation/RigidBodySimulation.vcxproj','external/glad/glad.vcxproj','RigidBodySimulation/imgui.ini','runtime/tmp/gpu-capture-tools/RenderDoc_1.46_64/RenderDoc_1.46_64/renderdoc_app.h')]
    return {str(ROOT/p):sha(ROOT/p) for p in sorted(set(names))}

def init():
    guard();OUT.mkdir(parents=True,exist_ok=False)
    fixture=ROOT/'RigidBodySimulation/tests/fixtures/collider_c04'
    reference=OUT/'reference';reference.mkdir()
    data=(fixture/'variant-0.original.txt').read_bytes()
    for name in ('observer-cache-initial.bin','observer-cache-command.bin'):
        old=('C:/dev/GEngine-collider-replacement/campaign/native/Debug-selection-03/data/'+name).encode()
        assert data.count(old)==1
        target=reference/name;shutil.copyfile(fixture/name,target)
        data=data.replace(old,target.as_posix().encode())
    (reference/'variant-0.txt').write_bytes(data)
    shutil.copyfile(fixture/'phase24-single-gold.frag',reference/'phase24-single-gold.frag')
    write(OUT/'ENTRY.json',dict(utc=now(),head=git('rev-parse','HEAD').decode().strip(),runner_sha256=sha(Path(__file__)),reference_sha256={p.name:sha(p) for p in reference.iterdir()},acceptance='NOT implied; independent oracle, all required gate review, and Owner acceptance remain separate.'))

def event(event_type,**data):
    rows=sorted(OUT.glob('EVENT_*.json'));write(OUT/f'EVENT_{len(rows)+1:03d}_{event_type}.json',dict(recorded_utc=now(),previous_sha256=sha(rows[-1]) if rows else None,**data))
def usage():
    rows=[json.loads(p.read_text())for p in OUT.glob('attempt-*/result.json')]
    assert len(rows)==len(list(OUT.glob('attempt-*/invocation.json'))),'Unreconciled attempt'
    return dict(builds=sum(r['kind']=='build' for r in rows),launches=sum(r['kind']=='native' for r in rows),build_seconds=sum(r['seconds'] for r in rows if r['kind']=='build'),native_seconds=sum(r['seconds'] for r in rows if r['kind']=='native'))
def env():
    e={k:v for k,v in os.environ.items() if not k.startswith(('GENGINE_','PRE_EDITOR_')) and k not in ('CL','_CL_')}
    temp=OUT/'tmp';temp.mkdir(exist_ok=True);e.update(TMP=str(temp),TEMP=str(temp),PYTHONDONTWRITEBYTECODE='1');return e
def execute(folder,kind,cmd,environment,cwd,watchdog,metadata):
    write(folder/'invocation.json',dict(utc=now(),kind=kind,command=cmd,cwd=str(cwd),environment={k:v for k,v in environment.items() if k.startswith(('GENGINE_','PRE_EDITOR_'))},watchdog_seconds=watchdog,prior_integration_usage=usage(),**metadata))
    began=time.monotonic()
    with (folder/'console.log').open('xb') as log:
        result=owned.remediation_owned_build(cmd,environment,log,watchdog,_factory=lambda audit:NativeOwner(audit,cwd))
    return dict(kind=kind,utc=now(),seconds=time.monotonic()-began,execution=result,passed=result['exit_code']==0 and not result['error'] and not result['timed_out'])
def build(configuration='Release',ordinary=False,control=False):
    assert not control, 'Historical controls are not replayed by this portable runner'
    guard();current=inputs();prior=sorted(OUT.glob('attempt-build-*/result.json'))
    if prior:
        old=json.loads(prior[-1].read_text());assert old['passed'] or old['inputs']!=current,'Unchanged failed candidate retry forbidden'
    if ordinary:
        gate=json.loads((OUT/'FOCUSED_GATE_DISPOSITION.json').read_text());assert gate['passed'] and gate['inputs']==current
    folder=OUT/f'attempt-build-{usage()["builds"]+1:02d}';folder.mkdir()
    props=folder/'qualification.props'
    includes=str(ROOT/'external/sdl2/include')+';'+str(ROOT/'external/glad/include')+';%(AdditionalIncludeDirectories)'
    extra=''
    if not ordinary:
        macros='GENGINE_COLLIDER_REPLACEMENT_DIAGNOSTIC;GE_ENABLE_COUPLED_COLLISION_OBSERVATION;GENGINE_COLLIDER_ADMISSION_VALIDATION;GENGINE_COLLIDER_ADMISSION_ISOLATED_BUILD;GENGINE_COLLIDER_PASSIVE_OBSERVATION;'+('GE_ENABLE_PHYSICS_PROFILING;' if configuration=='Release' else '')
        extra='<ItemDefinitionGroup><ClCompile><PreprocessorDefinitions>'+macros+'%(PreprocessorDefinitions)</PreprocessorDefinitions><DebugInformationFormat>ProgramDatabase</DebugInformationFormat></ClCompile></ItemDefinitionGroup>'
        extra+='<ItemDefinitionGroup Condition="\'$(ProjectName)\'==\'RigidBodySimulation\'"><ClCompile><PreprocessorDefinitions>GENGINE_COLLIDER_EDITOR_SMOKE;GENGINE_MESH_HIGHLIGHT_AUDIT;GENGINE_COLLIDER_VALIDATION_ROOT=&quot;'+ROOT.as_posix()+'/&quot;;%(PreprocessorDefinitions)</PreprocessorDefinitions></ClCompile></ItemDefinitionGroup>'
    props.write_text('<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><ItemDefinitionGroup><ClCompile><AdditionalIncludeDirectories>'+includes+'</AdditionalIncludeDirectories></ClCompile></ItemDefinitionGroup>'+extra+'</Project>')
    for p in current:
        target=folder/'source'/Path(p).relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
    cmd=['C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe',str(ROOT/'RigidBodySimulation/RigidBodySimulation.vcxproj'),'/t:Build','/p:Configuration='+configuration,'/p:Platform=x64','/p:VCToolsVersion=14.44.35207','/p:WindowsTargetPlatformVersion=10.0.26100.0','/p:ForceImportBeforeCppTargets='+str(props),'/m:2','/nr:false','/nologo','/v:minimal']
    result=execute(folder,'build',cmd,env(),ROOT,1200,dict(configuration=configuration,ordinary=ordinary,control=control,inputs=current))
    result.update(configuration=configuration,ordinary=ordinary,control=control,inputs=current)
    if result['passed']:
        exe=ROOT/f'bin/{configuration}/RigidBodySimulation/RigidBodySimulation.exe';result['exe_sha256']=sha(exe);result['library_sha256']=sha(ROOT/f'bin/{configuration}/GEngine/GEngine.lib')
        result['linked_gold_verified']=b'return selected.rg+selected.ba;' in exe.read_bytes();result['passed'] &=result['linked_gold_verified'];shutil.copyfile(exe,folder/exe.name)
    write(folder/'result.json',result);event('BUILD_RESULT',attempt=folder.name,**result);print(json.dumps({k:result.get(k) for k in ('passed','seconds','configuration','ordinary','control','exe_sha256')}),flush=True)
    if not result['passed']:raise SystemExit(1)
def native(mode,preset=None,control=False):
    assert not control, 'Historical controls are not replayed by this portable runner'
    guard();build_path=sorted(OUT.glob('attempt-build-*/result.json'))[-1];built=json.loads(build_path.read_text());assert built['passed'] and built['inputs']==inputs()
    exe=ROOT/f'bin/{built["configuration"]}/RigidBodySimulation/RigidBodySimulation.exe';assert sha(exe)==built['exe_sha256']
    folder=OUT/f'attempt-native-{usage()["launches"]+1:02d}';folder.mkdir();runtime=folder/'runtime';runtime.mkdir();shutil.copyfile(ROOT/'RigidBodySimulation/imgui.ini',runtime/'imgui.ini')
    e=env();data=folder/'data'
    if mode in ('admission','regression'):
        data=(AUDIT/f'accepted-control-{preset}') if control else data;data.mkdir()
        e['GENGINE_COLLIDER_CAPTURE_OUTPUT']=str(data)
        if mode=='admission':
            manifest=OUT/'reference/variant-0.txt';e.update(GENGINE_COLLIDER_ADMISSION_OUTPUT=str(data),GENGINE_COLLIDER_REPLAY_REQUEST=str(manifest),GENGINE_COLLIDER_ADMISSION_INPUT_SHA256=sha(manifest))
        else:e.update(GENGINE_COLLIDER_PASSIVE_MODE='regression',GENGINE_COLLIDER_PASSIVE_PRESET=preset)
    else:
        variable={'audit':'GENGINE_ORDINARY_HIGHLIGHT_OUTPUT','mesh':'GENGINE_MESH_HIGHLIGHT_OUTPUT','inspector':'GENGINE_COLLIDER_EDITOR_SMOKE_OUTPUT'}[mode];e[variable]=str(folder/'checks.tsv')
        if mode=='mesh':e['GENGINE_COLLIDER_SELECTION_SMOKE_OUTPUT']=str(folder/'checks.tsv')
    result=execute(folder,'native',[str(exe)],e,runtime,900 if mode in ('admission','regression') else 300,dict(build=str(build_path),exe_sha256=sha(exe),mode=mode,preset=preset,control=control,data=str(data)))
    if mode in ('admission','regression'):
        path=data/('admission-result.json' if mode=='admission' else 'passive-status.json');summary=json.loads(path.read_text()) if path.exists() else {}
        result['native_summary']=summary;result['passed'] &=summary.get('status')==('SCOPED_CHECKS_COMPLETE' if mode=='admission' else 'CHARACTERIZATION_COMPLETE') and not summary.get('failure')
    else:
        text=(folder/'checks.tsv').read_text() if (folder/'checks.tsv').exists() else '';result['passed'] &= '\nSUMMARY\tPASS\t' in text
        if (folder/'checks.tsv').exists():shutil.copyfile(folder/'checks.tsv',folder/'widget-smoke.tsv')
    result.update(mode=mode,preset=preset,control=control,data=str(data),configuration=built['configuration'],build=str(build_path))
    write(folder/'result.json',result);event('NATIVE_RESULT',attempt=folder.name,**result);print(json.dumps({k:result.get(k) for k in ('passed','seconds','mode','preset','control')}),flush=True)
    if not result['passed']:raise SystemExit(1)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=('build','audit','mesh','inspector','admission','regression','usage','init'));p.add_argument('--configuration',default='Release',choices=('Release','Debug'));p.add_argument('--ordinary',action='store_true');p.add_argument('--control',action='store_true');p.add_argument('--preset',choices=('SphereLattice','BoxStack','SphereBoxStack'));p.add_argument('--output',type=Path,default=Path('logs/collider-validation/local'));a=p.parse_args();OUT=(ROOT/a.output).resolve();AUDIT=OUT
    if a.action=='init':init()
    elif a.action=='build':build(a.configuration,a.ordinary,a.control)
    elif a.action=='usage':print(json.dumps(usage()))
    else:native(a.action,a.preset,a.control)
