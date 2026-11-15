"""Bounded campaign helpers; no solution generation or qualification campaign."""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
VC=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207')
SDK=Path('C:/Program Files (x86)/Windows Kits/10')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['bake','probe']);parser.add_argument('--config',default='Release');parser.add_argument('--label',default='01');a=parser.parse_args()
    out=ROOT/f'logs/ui/editor-views-01/{a.mode}-{a.config}-{a.label}';out.mkdir(parents=True,exist_ok=False)
    includes=[VC/'include',*[SDK/'Include/10.0.26100.0'/x for x in ['ucrt','shared','um']]]
    libs=[VC/'lib/x64',*[SDK/'Lib/10.0.26100.0'/x for x in ['ucrt/x64','um/x64']]]
    exe=out/(a.mode+'.exe')
    command=[VC/'bin/Hostx64/x64/cl.exe','/nologo','/std:c++23preview','/EHsc','/W3','/MTd' if a.config=='Debug' else '/MT','/Od' if a.config=='Debug' else '/O2']
    if a.mode=='probe':
        includes += [ROOT/'GEngine/include',ROOT/'GEngine/include/GEngine',ROOT/'GEngine/include/external',ROOT/'RigidBodySimulation/include',*[ROOT/'external'/x/'include' for x in ['sdl2','spdlog','glad','assimp','entt','tbb','fmod']]]
        libs += [ROOT/'external'/x/'lib' for x in ['sdl2','assimp','tbb','fmod']]
        command+=['/DSDL_MAIN_HANDLED','/DGENGINE_PLATFORM_WINDOWS','/DGENGINE_CONFIG_'+a.config.upper(),'/DGENGINE_RENDER_COUNTERS=1','/DGE_ENABLE_PHYSICS_PROFILING']
    command += ['/I'+str(p) for p in includes]
    command += [ROOT/('tools/bake_rbs_icons.cpp' if a.mode=='bake' else 'tools/editor_views_probe.cpp')]
    command += ['/Fo'+str(out)+os.sep,'/Fe'+str(exe),'/link','/SUBSYSTEM:CONSOLE',*['/LIBPATH:'+str(p) for p in libs]]
    if a.mode=='probe':command += [ROOT/f'bin/{a.config}/GEngine/GEngine.lib',ROOT/f'bin/{a.config}/Jolt/Jolt.lib',ROOT/f'external/glad/bin/{a.config}/glad/glad.lib','SDL2.lib','SDL2_ttf.lib','tbb12.lib','tbb12_debug.lib','tbb.lib','tbb_debug.lib','assimp.lib','fmod64_vc.lib','fmodL64_vc.lib','fmodstudio64_vc.lib','fmodstudioL64_vc.lib']
    command=list(map(str,command));record={'command':command}
    result=subprocess.run(command,cwd=out,capture_output=True);(out/'compile.log').write_bytes(result.stdout+result.stderr);record['compile_exit']=result.returncode
    if result.returncode==0:
        env=os.environ.copy();env['Path']=str(ROOT/f'bin/{a.config}/RigidBodySimulation')+os.pathsep+env.get('Path',env.get('PATH',''))
        env['GENGINE_ASSET_ROOT']=str(ROOT/f'bin/{a.config}/assets');env['GENGINE_SHADOW_RESOLUTION']='256'
        if a.mode=='bake':
            icons=ROOT/'GEngine/include/GEngine/Assets/Images/Editor/Icons'
            for p in icons.glob('viewport-*.png'):
                dest=ROOT/'logs/ui/editor-views-01/before/source'/p.relative_to(ROOT)
                if not dest.exists():dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dest)
            args=[str(exe),str(icons)]
        else:args=[str(exe)]
        try:
            with (out/'run.log').open('wb') as stream:
                run=subprocess.run(args,cwd=out,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
            record['run_exit']=run.returncode
        except subprocess.TimeoutExpired:
            record['run_exit']='TIMEOUT at 120 s'
        record['binary_sha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
    (out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record));return 0 if record.get('run_exit')==0 else 1
if __name__=='__main__':raise SystemExit(main())
