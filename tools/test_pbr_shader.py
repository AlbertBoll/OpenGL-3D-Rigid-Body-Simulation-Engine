"""Phase 61: real PBR pixels, picking outputs, and maintained milestone integration."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import shutil
import struct
import zlib
import winreg
import re

from rendering_validation import ROOT, toolchain
from postbuild import stage_runtime_assets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configuration', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--focused-only', action='store_true', help='Development only: reuse built library and skip integration/milestone coverage')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    config = args.configuration
    report = {'configuration': config, 'focused_only': args.focused_only, 'result': 'FAIL', 'steps': []}
    env = {k: v for k, v in os.environ.items() if k.lower() != 'path'}
    env['Path'] = os.environ.get('PATH', os.environ.get('Path', ''))
    env.pop('SDL_VIDEODRIVER', None)
    env['CL'] = env.get('CL', '') + ' /we4715 /we4716'

    def invoke(name, command, timeout=120, child_env=None, failure_marker=None, cwd=ROOT):
        command = [str(item) for item in command]
        log = out / (name + '.log')
        step = {'name': name, 'command': command, 'cwd': str(cwd), 'log': str(log), 'expected_failure': failure_marker}
        report['steps'].append(step)
        with log.open('wb') as stream:
            try:
                result = subprocess.run(command, cwd=cwd, env=child_env or env, stdout=stream, stderr=subprocess.STDOUT,
                                        timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
                step['exit'] = result.returncode
                text = log.read_text(errors='replace')
                passed = result.returncode == 0 if failure_marker is None else result.returncode != 0 and failure_marker in text
                step['result'] = 'PASS' if passed else 'FAIL'
            except subprocess.TimeoutExpired:
                step.update(exit=None, result='FAIL', reason='Timeout; owned child terminated')
        print(f"[{step['result']}] {name}: exit={step['exit']}", flush=True)
        return step['result'] == 'PASS'

    try:
        if not args.focused_only:
            if not invoke('integration', [sys.executable, ROOT/'tools/test_frame_submission.py', '--configuration', config,
                                         '--output', out/'integration', '--smoke'], 1800):
                return 1
            report['integration_evidence'] = str(out/'integration/results.json')
        msbuild, vc = toolchain()
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows Kits\Installed Roots') as key:
            sdk = Path(winreg.QueryValueEx(key, 'KitsRoot10')[0])
        version = '10.0.26100.0'
        if vc.name != '14.44.35207' or not (sdk/'Lib'/version/'um/x64/kernel32.lib').is_file():
            report['reason'] = 'Selected compiler/SDK unavailable; no toolchain migration authorized'
            return 1
        report['toolchain'] = {'msbuild': str(msbuild), 'msvc': str(vc), 'sdk': str(sdk), 'sdk_version': version,
                               'language': 'c++23preview', 'CRT': '/MTd' if config == 'Debug' else '/MT'}
        includes = [vc/'include', *(sdk/'Include'/version/part for part in ('ucrt', 'shared', 'um')),
                    ROOT/'GEngine/include', ROOT/'GEngine/include/GEngine', ROOT/'GEngine/include/external',
                    *(ROOT/'external'/part/'include' for part in ('sdl2', 'spdlog', 'glad', 'assimp', 'entt', 'tbb', 'fmod'))]
        libraries = [vc/'lib/x64', sdk/'Lib'/version/'ucrt/x64', sdk/'Lib'/version/'um/x64',
                     *(ROOT/'external'/part/'lib' for part in ('sdl2', 'tbb', 'assimp', 'fmod'))]
        executable = out/'pbr-shader-probe.exe'
        library = ROOT/'bin'/config/'GEngine/GEngine.lib'
        command = [vc/'bin/Hostx64/x64/cl.exe', '/nologo', '/std:c++23preview', '/EHsc', '/W3',
                   '/MTd' if config == 'Debug' else '/MT', '/Od' if config == 'Debug' else '/O2',
                   '/DSDL_MAIN_HANDLED', '/DGENGINE_PLATFORM_WINDOWS', '/DGENGINE_CONFIG_'+config.upper(),
                   *['/I'+str(p) for p in includes], '/external:W0', '/external:templates-',
                   *['/external:I'+str(p) for p in (ROOT/'external', ROOT/'GEngine/include/external', vc/'include')],
                   ROOT/'tools/pbr_shader_probe.cpp', '/Fo'+str(out)+os.sep, '/Fe'+str(executable), '/link', '/SUBSYSTEM:CONSOLE',
                   *['/LIBPATH:'+str(p) for p in libraries], library, ROOT/'external/glad/bin'/config/'glad/glad.lib',
                   'SDL2.lib', 'SDL2_ttf.lib', 'tbb12.lib', 'tbb12_debug.lib', 'tbb.lib', 'tbb_debug.lib', 'assimp.lib',
                   'fmod64_vc.lib', 'fmodL64_vc.lib', 'fmodstudio64_vc.lib', 'fmodstudioL64_vc.lib']
        if not invoke('compile-pbr-probe', command):
            return 1
        assets = ROOT/'bin'/config/'assets'
        if args.focused_only:
            stage_runtime_assets(ROOT, assets)
        env['Path'] = str(ROOT/'bin'/config/'GEngineEditor') + os.pathsep + env['Path']
        env['GENGINE_ASSET_ROOT'] = str(assets)
        env['GENGINE_SHADOW_RESOLUTION'] = '256'
        shaders = assets/'Shaders'
        names = ('pbr_cascade_shadow.frag', 'point_light_sphere_visual.frag', 'mouse_pick.frag')
        report['inputs'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (executable, library, *(shaders/n for n in names))}
        for name in names:
            if (shaders/name).read_bytes() != (ROOT/'GEngine/include/GEngine/Assets/Shaders'/name).read_bytes():
                report['reason'] = 'Staged shader differs from candidate: '+name
                return 1
        if not invoke('pbr-pixels', [executable, shaders]):
            return 1
        if '[PASS] pbr-shader' not in (out/'pbr-pixels.log').read_text(errors='replace'):
            return 1
        # Controls alter private copies only. Each old defect must fail its own GPU oracle.
        source = (shaders/names[0]).read_text()
        controls = [
            ('decode', 'normals', source.replace('texture(normalMap, uvs).xyz * 2.0 - 1.0', 'texture(normalMap, uvs).xyz'),
             '[FAIL] flat/signed tangent normal direction'),
            ('untiled-normal', 'tiling', source.replace('texture(normalMap, uvs)', 'texture(normalMap, fs_in.TexCoords)'),
             '[FAIL] tiled PBR output equals independently selected constant texel'),
            ('bitangent-sign', 'normals', source.replace('-normalize(cross(N, T))', 'normalize(cross(N, T))'),
             '[FAIL] flat/signed tangent normal direction'),
            ('green-flip', 'normals', source.replace('    vec3 Q1 =', '    tangentNormal.y = -tangentNormal.y;\n    vec3 Q1 ='),
             '[FAIL] flat/signed tangent normal direction'),
            ('roughness-scale', 'roughness', source.replace('texture(roughnessMap, uvs).r * roughnessScale', 'texture(roughnessMap, uvs).r'),
             '[FAIL] authored roughness scale equals linear map scaling'),
            ('constant-id', 'entity', source.replace('layout(location = 0) out vec4 FragColor;',
             'layout(location = 0) out vec4 FragColor;\nlayout(location = 1) out int o_EntityID;').replace(
             'FragColor = vec4(color, 1.0);', 'FragColor = vec4(color, 1.0); o_EntityID = 50;'),
             '[FAIL] color program has exactly one output at zero')]
        for label, mode, mutated, marker in controls:
            if mutated == source:
                report['reason'] = 'Negative control did not change source: '+label
                return 1
            directory = out/'controls'/label
            directory.mkdir(parents=True, exist_ok=True)
            for name in names:
                (directory/name).write_bytes((shaders/name).read_bytes())
            (directory/names[0]).write_text(mutated)
            if not invoke('control-'+label, [executable, directory, mode], failure_marker=marker):
                return 1
        # This regression exercises the real application authoring calls. A
        # standalone linear texture fixture cannot catch the SRGB-default bug.
        app_dir = out/'application-normal'
        app_dir.mkdir(parents=True, exist_ok=True)
        package = app_dir/'assets'
        shutil.copytree(assets, package)
        def chunk(kind, payload):
            return struct.pack('>I', len(payload))+kind+payload+struct.pack('>I', zlib.crc32(kind+payload) & 0xffffffff)
        def constant_png(rgb):
            scan=b''.join(b'\0'+bytes((*rgb,255))*4 for _ in range(4))
            return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',4,4,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
        flat=constant_png((128,128,255))
        normal_paths=('Images/PBR/rustediron/rustediron2_normal.png',
                      'Images/PBR/base_white_tile/base-white-tile_normal-dx.png')
        scalar_paths=('Images/PBR/rustediron/rustediron2_metallic.png',
                      'Images/PBR/rustediron/rustediron2_roughness.png',
                      'Images/PBR/subtle_black_granite/subtle-black-granite_ao.png',
                      'Images/PBR/base_white_tile/base-white-tile_metallic.png',
                      'Images/PBR/base_white_tile/base-white-tile_roughness.png',
                      'Images/PBR/base_white_tile/base-white-tile_ao.png')
        for path in (*normal_paths,*scalar_paths):
            target=package/path
            if not target.is_file():
                report['reason']='Missing actual PBR package path: '+path
                return 1
            value = 64 * (scalar_paths.index(path) % 3 + 1) if path in scalar_paths else 0
            target.write_bytes(flat if path in normal_paths else constant_png((value,value,value)))
        app_source=ROOT/'RigidBodySimulation/src/RigidBodySimulation.cpp'
        authored=app_source.read_text()
        normal_bad,count=re.subn(r'("normalMap"), "\.png", dataMapDesc',r'\1',authored)
        scalar_bad,scalars=re.subn(r'("(?:metallicMap|roughnessMap|aoMap)"), "\.png", dataMapDesc',r'\1',authored)
        color_bad,colors=re.subn(r'("albedoMap")\);',r'\1, ".png", dataMapDesc);',authored,count=2)
        if (count,scalars,colors)!=(2,6,2):
            report['reason']='Application controls must identify exactly two normal, six scalar and two color requests'
            return 1
        binding_bad=authored.replace('base-white-tile_metallic", "metallicMap"', 'base-white-tile_roughness", "metallicMap"').replace(
            'base-white-tile_roughness", "roughnessMap"', 'base-white-tile_metallic", "roughnessMap"')
        scale_bad=authored.replace('{"roughnessScale", MaterialParameterType::Float, .5f}',
                                   '{"roughnessScale", MaterialParameterType::Float, 1.f}',1)
        if binding_bad==authored or scale_bad==authored:
            report['reason']='Floor binding/roughness control did not match material authoring'
            return 1
        sphere_bad=authored.replace('{"roughnessScale", MaterialParameterType::Float, .28f}',
                                    '{"roughnessScale", MaterialParameterType::Float, .9f}')
        if sphere_bad==authored:
            report['reason']='Sphere roughness control did not match material authoring'
            return 1
        bad_sources={}
        for label,text in (('bad',normal_bad),('bad-scalar',scalar_bad),('bad-color',color_bad),('bad-binding',binding_bad),('bad-floor-scale',scale_bad),('bad-sphere-scale',sphere_bad)):
            bad_sources[label]=app_dir/(label+'.cpp')
            bad_sources[label].write_text(text)
        split=command.index('/link')
        compile_base=[x for x in command[:split] if not str(x).endswith('.cpp') and not str(x).startswith(('/Fo','/Fe'))]
        compile_base += ['/I'+str(ROOT/'RigidBodySimulation/include'), '/c']
        objects={}
        for label, source_path, definitions in (
            ('good',app_source,['/DCreateApp=OriginalNormalFactory','/Dmain=OriginalNormalMain']),
            *((label,path,['/DCreateApp=OriginalNormalFactory','/Dmain=OriginalNormalMain']) for label,path in bad_sources.items()),
            ('main',ROOT/'RigidBodySimulation/src/main.cpp',[]),
            ('observer',ROOT/'tools/pbr_shader_probe.cpp',['/DPBR_APPLICATION_PROBE'])):
            obj=app_dir/(label+'.obj');objects[label]=obj
            if not invoke('compile-application-normal-'+label, [*compile_base,*definitions,source_path,'/Fo'+str(obj)]):
                return 1
        for label in ('good',*bad_sources):
            run_dir=app_dir/label;run_dir.mkdir()
            app=run_dir/'RigidBodySimulation.exe'
            link=[Path(command[0]).with_name('link.exe'), '/NOLOGO', '/INCREMENTAL:NO', '/OUT:'+str(app),
                  objects[label],objects['main'],objects['observer'],*command[split+1:]]
            if not invoke('link-application-normal-'+label,link):
                return 1
            shutil.copy2(ROOT/'RigidBodySimulation/imgui.ini',run_dir/'imgui.ini')
            app_env=dict(env,GENGINE_ASSET_ROOT=str(package))
            marker={'bad':'[FAIL] production normal map sampled as non-linear vector data',
                    'bad-scalar':'[FAIL] production scalar map sampled as non-linear data',
                    'bad-color':'[FAIL] production albedo lost sRGB color sampling',
                    'bad-binding':'[FAIL] production scalar channel bound to the wrong material map',
                    'bad-floor-scale':'[FAIL] floor roughness scale/adjacent packed material lanes',
                    'bad-sphere-scale':'[FAIL] sphere roughness scale/adjacent packed material lanes'}.get(label)
            if not invoke('application-normal-'+label,[app],180,child_env=app_env,failure_marker=marker,cwd=run_dir):
                return 1
            if label=='good' and '[PASS] production normal-map contract' not in (out/'application-normal-good.log').read_text(errors='replace'):
                report['reason']='Actual normal-map application paths were not both exercised'
                return 1
        report['application_normal_regression']={'source':str(app_source),
            'source_sha256':hashlib.sha256(app_source.read_bytes()).hexdigest(),
            'control_source_sha256':{k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in bad_sources.items()},
            'flat_png_sha256':hashlib.sha256(flat).hexdigest(),'normal_paths':normal_paths,'scalar_paths':scalar_paths,
            'scalar_values':{'metallic':64,'roughness':128,'ao':192},
            'expected_flat_decoded':'abs(x/y)<0.005 and z>0.999 with 8-bit neutral quantization',
            'control':'Restore SRGB normal/scalar requests or force linear albedo; each must fail the corresponding actual sampling gate'}
        report['result'] = 'PASS'
        return 0
    finally:
        (out/'results.json').write_text(json.dumps(report, indent=2)+'\n')
        print(f"[{report['result']}] Phase 61 {config}: {out / 'results.json'}", flush=True)


if __name__ == '__main__':
    sys.exit(main())
