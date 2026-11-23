"""Phase 30 Motors. Reuse the existing compiler guard and the frozen per-role limits."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "logs/pre_editor/phase-30/candidate-03/arena-review-01"
PACKET = ROOT / "logs/pre_editor/phase-30/c-motor-01/admission.json"
sys.path.insert(0, str(ROOT / "logs/pre_editor/phase-29/resume-01"))
import build as guarded


def sha(path):
    return guarded.digest(path)


def record(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as out:
        json.dump(value, out, indent=2)
        out.write(chr(10))


def environment():
    result = {k:v for k,v in os.environ.items() if not k.upper().startswith(("GENGINE_","JPH_"))
              and k.upper() not in {"CL","_CL_","LINK","_LINK_","LIB","INCLUDE","LIBPATH","MSBUILD_EXE_PATH",
                                    "FORCEIMPORTBEFORECPPTARGETS","FORCEIMPORTAFTERCPPTARGETS","OUTDIR","INTDIR"}}
    tmp = ROOT / "runtime/tmp/phase30"
    tmp.mkdir(parents=True, exist_ok=True)
    return dict(result, TMP=str(tmp), TEMP=str(tmp), MSBUILDDISABLENODEREUSE="1")


def source_identities(*, include_runtime_assets=False):
    activation = read(ROOT / "docs/pre_editor/active/PHASE_30_ENTRY_ACCEPTANCE_01.json")
    return {entry["path"]: sha(ROOT/entry["path"]) if (ROOT/entry["path"]).is_file() else None
            for entry in activation["source_envelope"] + activation["support"]
            if include_runtime_assets or Path(entry["path"]).suffix in (".h", ".cpp", ".py")}


def ordinary_build(config):
    from pre_editor_phase28_qualification import MSBUILD
    if config not in ("Debug","Release"):
        raise ValueError("Unsupported build configuration")
    dest = EVIDENCE / ("ordinary-" + config)
    dest.mkdir(parents=True, exist_ok=False)
    guarded.environment = environment
    source = source_identities()
    record(dest/"source-inputs.json", source)
    start = time.monotonic()
    status = {}
    for project in ("GEngine","RigidBodySimulation"):
        directory = dest / project
        directory.mkdir()
        command = [str(MSBUILD), str(ROOT/project/(project+".vcxproj")), "/t:Build", "/m:1", "/nr:false",
                   "/p:Configuration="+config, "/p:Platform=x64", "/p:BuildProjectReferences=false",
                   "/p:CL_MPCount=1", "/p:UseMultiToolTask=false", "/verbosity:minimal"]
        record(directory/"command.json",command)
        result = guarded.monitor(command, directory, max(0,1200-(time.monotonic()-start)))
        record(directory/"result.json",result)
        status[project] = result
        if result["guard"] or result["exit_code"]: break
    passed = len(status)==2 and all(not r["guard"] and not r["exit_code"] for r in status.values()) and source == source_identities()
    exe = ROOT / "bin" / config / "RigidBodySimulation/RigidBodySimulation.exe"
    record(dest/"result.json",{"pass":passed,"projects":status,"binary":str(exe) if passed else None,
                              "sha256":sha(exe) if passed else None})
    return 0 if passed else 1


def isolated_build(config):
    import xml.etree.ElementTree as ET
    import pre_editor_phase29_step_safety as prior
    key = {"Debug":"safety","Release":"qualification"}[config]
    dest = EVIDENCE / ("isolated-" + config)
    output = ROOT / "bin/phase30-qualification" / config
    intermediate = ROOT / "bin-int/phase30-qualification" / config
    if dest.exists():
        raise ValueError("The isolated build role already has a record")
    inputs = {project:prior.closure(key,project) for project in guarded.SOURCE}
    inputs["RigidBodySimulation"]["archive_pch_companion"] = inputs["GEngine"]["archive_pch_companion"]
    guarded.environment = environment
    dest.mkdir(parents=True)
    source = source_identities()
    record(dest/"source-inputs.json", source)
    started = time.monotonic()
    passed = False
    for project, info in inputs.items():
        rec = dest / project
        products = output / project
        obj = intermediate / project
        for directory in (rec, products, obj):
            guarded.safe(directory).mkdir(parents=True, exist_ok=False)
        info["dependencies"][str(ROOT/"RigidBodySimulation/tests/MotorChecks.h")] = sha(ROOT/"RigidBodySimulation/tests/MotorChecks.h")
        record(rec/"closure.json", info)
        element = ET.fromstring(prior.props(key,project,info,obj,products))
        ns=prior.NS
        flags = element.find(".//m:PreprocessorDefinitions",ns)
        flags.text = flags.text.replace("GENGINE_PHASE29_STEP_SAFETY;","")
        if project=="RigidBodySimulation":
            flags.text = "GENGINE_PHASE30_QUALIFICATION;"+flags.text
            link = element.find(".//m:Link/m:AdditionalDependencies",ns)
            link.text = link.text.replace(str(prior.OUTPUT/key/"GEngine/GEngine.lib"),str(output/"GEngine/GEngine.lib"))
        ET.register_namespace("",ns["m"])
        props = rec/"build.props"
        element = ET.ElementTree(element)
        element.write(props,encoding="utf-8",xml_declaration=True)
        cmd=[str(guarded.MSBUILD),str(guarded.SOURCE[project]),"/t:Build","/m:1","/nr:false","/v:minimal",
             "/p:Configuration="+config,"/p:Platform=x64","/p:BuildProjectReferences=false",
             "/p:VCToolsVersion=14.44.35207","/p:WindowsTargetPlatformVersion=10.0.26100.0",
             "/p:PreferredToolArchitecture=x64","/p:ForceImportBeforeCppTargets="+str(props),
             "/p:UseMultiToolTask=true","/p:MultiProcMaxCount=1","/p:CL_MPCount=1",
             "/p:EnforceProcessCountAcrossBuilds=true","/p:MultiProcessorCompilation=false"]
        record(rec/"command.json",cmd)
        res=guarded.monitor(cmd,rec,max(0,1200-(time.monotonic()-started)))
        res["inputs_unchanged"]=all(sha(k)==v for k,v in info["dependencies"].items())
        passed=not res["guard"] and not res["exit_code"] and res["inputs_unchanged"]
        record(rec/"result.json",res)
        if not passed: break
    passed = passed and source == source_identities()
    exe=output/"RigidBodySimulation/RigidBodySimulation.exe"
    linkmap=exe.with_suffix(".map")
    record(dest/"result.json",{"pass":passed,"binary":str(exe) if passed else None,
        "sha256":sha(exe) if passed else None,"map_sha256":sha(linkmap) if passed else None})
    return 0 if passed else 1

def read(path):
    return json.loads(Path(path).read_bytes())


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def compiled_inputs(config, case):
    from pre_editor_phase29_qualification import pe_words, SIZES
    build = EVIDENCE / ("isolated-" + config)
    result=read(build/"result.json")
    require(result["pass"],"The isolated build has no successful link")
    manifest = build/"source-inputs.json"
    expected_inputs = read(manifest)
    if config == "Release":
        # Only the Python supervisor changed after this exact binary was linked.
        reuse = read(PACKET.parent.parent/"native-contact-exception-01/runner-reuse.json")
        require(sha(manifest) == reuse["original_source_manifest_sha256"] and
                result["sha256"] == reuse["binary_sha256"], "Release runner reuse belongs to another build")
        expected_inputs["tools/pre_editor_phase30_qualification.py"] = reuse["runner_sha256"]
    require(expected_inputs == source_identities(), "Compiled Phase 30 inputs or bound runner changed")
    exe=Path(result["binary"])
    map_path=exe.with_suffix(".map")
    require(sha(exe)==result["sha256"] and sha(map_path)==result["map_sha256"],"Fixture PE or map drift")
    for project in guarded.SOURCE:
        rec=build/project
        require(all(sha(k)==v for k,v in read(rec/"closure.json")["dependencies"].items()),"Compiled input drift")
    linkmap=map_path.read_text()
    address,sizes=pe_words(exe,linkmap,"g_Phase27TargetSizes",8)
    _,abi=pe_words(exe,linkmap,"g_Phase29CaptureConfiguration",11)
    require(sizes==abi[:8]==SIZES and all(x in (0,1) for x in abi[9:]),"Compiled Arena ABI")
    arena=read(PACKET)["cost"]["arena"]
    require(all(sha(ROOT/k)==v for k,v in arena["source_pins"].items()),"Arena source drift")
    sites=arena["contact" if case=="contact" else "free_limit"]
    enabled=dict(sites["aligned_site_bytes"])
    for flag,on in zip(("JPH_VALIDATE_ISLAND_BUILDER","JPH_TRACK_SIMULATION_STATS"),abi[9:]):
        if on: enabled[flag]=sites["conditional_site_bytes"][flag]
    return exe,{"linker_map":{"path":str(map_path),"sha256":sha(map_path)},"preferred_sizes_va":address,
                "abi_sizes":sizes,"contact_sample_bytes":abi[8],"arena_positive_sites":len(enabled),
                "arena_sum_bytes":sum(enabled.values()),"sites":enabled,"arena_capacity":arena["capacity_bytes"]}


def require_predecessor(role, previous, executable):
    bindings = [(previous, "assessment"), (executable, "executable"), (PACKET, "workload")]
    if role == "native-limit":
        path = ROOT/"docs/pre_editor/active/PHASE_30_NATIVE_CONTACT_OBSERVER_ACCEPTANCE_01.json"
        decision_hash = "18d1617f3940750dfae83cae0c3b4d44df257669f5f0624c4d2d9bf92860e738"
        expected = "INCOMPLETE"
        bindings.extend([(previous.parent/"data/cost.csv", "cost_series"),
                         (PACKET.parent.parent/"jolt-contact-exception-01/native-contact-observer.json", "cost_review")])
    elif role == "jolt-limit":
        path = ROOT/"docs/pre_editor/active/PHASE_30_JOLT_CONTACT_VISUAL_ACCEPTANCE_01.json"
        decision_hash = "7d1aed5cd057991d76425fd241e9fcf61ce7202ede64f7cdd49cdd44cab9543a"
        expected = "FAIL"
    else:
        require(previous.is_file() and read(previous)["status"] == "PASS", "Previous role requires disposition")
        return
    require(sha(path) == decision_hash, "Contact Owner disposition changed")
    decision = read(path)
    for actual, key in bindings:
        reference = decision[key]
        require(actual == ROOT/reference["path"] and sha(actual) == reference["sha256"],
                "Contact exception identity mismatch: " + key)
    assessment = read(previous)
    require(assessment["status"] == decision["actual_outcome"] == expected and
            decision["disposition"] == "ACCEPTED_EXCEPTION", "Expected the original disposed Contact assessment")
    if role == "native-limit":
        checks = assessment["checks"]
        require(checks["observer_upper_ns"]["pass"] is False and
                all(check["pass"] is True for key, check in checks.items() if key != "observer_upper_ns"),
                "Native Contact exception covers only the observer upper bound")


def supervisor(role):
    import platform
    import postbuild
    from datetime import datetime, timezone
    require(sha(PACKET)=="d6f6ba9e195d56269b6d310af2482230b86d30b11b38b9bafa6627e55b721e91","Frozen packet drift")
    roles=["debug"]+[b+"-"+c for b in ("jolt","native") for c in ("free","contact","limit")]
    require(role in roles,"Invalid Physics role")
    index=roles.index(role)
    dest=EVIDENCE/"runs"/role
    require(not dest.exists(),"Role already reserved; zero automatic retries")
    config="Debug" if role=="debug" else "Release"
    backend,case=("jolt","free") if role=="debug" else role.split("-")
    exe,compiled=compiled_inputs(config,case)
    if index:
        require_predecessor(role, EVIDENCE/"runs"/roles[index-1]/"assessment.json", exe)
    package=exe.parent.parent
    postbuild.stage_profile_runtime(ROOT,config,exe.parent)
    postbuild.stage_runtime_assets(ROOT,package/"assets")
    assetroot=package/"assets"
    startup=assetroot/"startup/RigidBodySimulation.txt"
    lines=startup.read_text().splitlines()
    require(bool(lines) and all((assetroot/rel).is_file() for rel in lines[1:] if rel),"Incomplete executable-relative package")
    census=subprocess.check_output(["powershell.exe","-NoProfile","-Command",
        "[Diagnostics.Process]::GetProcessesByName('RigidBodySimulation').Length"],timeout=15).strip()
    require(census==b"0","Another RBS process is active")
    cwd=guarded.safe(ROOT/"runtime/RigidBodySimulation/phase30"/EVIDENCE.name/role)
    cwd.mkdir(parents=True,exist_ok=True)
    dest.mkdir(parents=True,exist_ok=False)
    data=dest/"data";data.mkdir()
    args=[str(exe),"--scene","motors","--motor-case",case,"--backend",backend]
    cap=120 if config=="Debug" else 1200
    record(dest/"reservation.json",{"role":role,"backend":backend.title(),"case":case,"max_attempted":cap,
        "wall_seconds":180,"utc":datetime.now(timezone.utc).isoformat(),"command":args,"cwd":str(cwd),"compiled":compiled,
        "binary_sha256":sha(exe),"packet_sha256":sha(PACKET),"host":platform.platform(),"cpu":platform.processor(),
        "power":subprocess.check_output(["powercfg","/getactivescheme"],text=True).strip(),
        "startup_sha256":sha(startup),"assets":{rel:sha(assetroot/rel) for rel in lines[1:] if rel},
        "dlls":{p.name:sha(p) for p in exe.parent.glob("*.dll")}})
    env=environment()
    env["GENGINE_PHASE30_OUTPUT"]=str(data)
    started=time.monotonic()
    with (dest/"process.log").open("xb") as stream:
        process=subprocess.Popen(args,cwd=cwd,env=env,stdout=stream,stderr=subprocess.STDOUT)
        timed_out=False
        try: code=process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            timed_out=True
            process.kill()
            code=process.wait()
    record(dest/"exit.json",{"pid":process.pid,"exit_code":code,"seconds":time.monotonic()-started,"timeout":timed_out})
    return assess(role)


def measure_numeric(data,reservation,checks,metrics):
    import csv
    import math
    import statistics
    from collections import defaultdict
    import pre_editor_phase29_qualification as g
    case=reservation["case"]
    contract=read(PACKET)
    rules=contract["physics_checks"]
    dt=1/60
    inertia=rules["mass_oracle"]["I_axis_kg_m2"]
    resolution=rules["mass_oracle"]["free_speed_increment_rad_s"]
    def rows(file):
        with (data/file).open() as stream: return list(csv.DictReader(stream))
    def check(name,ok,details=None): checks[name]={"pass":bool(ok),"detail":details}
    def norm(v): return math.sqrt(g.dot(v,v))
    def signed_depth(a,b):
        if not a["sphere"] and not b["sphere"]: return g.overlap(a,b)
        ball,box=(a,b) if a["sphere"] else (b,a)
        local=[abs(g.dot(g.sub(ball["pos"],box["pos"]),axis))-half for axis,half in zip(box["axes"],box["half"])]
        distance=norm([max(v,0) for v in local])+min(max(local),0)
        return max(0.,ball["half"][0]-distance)
    def energy(b):
        if not b["im"]: return 0.
        mass=1/b["im"]
        if b["sphere"]: inertia_axes=[.4*mass*b["half"][0]**2]*3
        else:
            h=b["half"]
            inertia_axes=[mass*(h[(i+1)%3]**2+h[(i+2)%3]**2)/3 for i in range(3)]
        return .5*(mass*g.dot(b["v"],b["v"])+sum(i*g.dot(axis,b["w"])**2 for i,axis in zip(inertia_axes,b["axes"])))+mass*10*(b["pos"][1]-.5)
    history=defaultdict(dict)
    for r in rows("bodies.csv"):
        tick,i=int(r["tick"]),int(r["bone"])
        require(i not in history[tick],"duplicate body at a tick")
        quat=g.vec(r,"qw qx qy qz")
        require(abs(g.dot(quat,quat)-1)<1e-4,"invalid orientation")
        history[tick][i]={"pos":g.vec(r,"x y z"),"q":quat,"axes":g.axes(quat),"v":g.vec(r,"vx vy vz"),"w":g.vec(r,"wx wy wz"),
            "half":g.vec(r,"hx hy hz"),"im":g.vec(r,"im")[0],"sleep":int(r["sleep"]),"sphere":case=="contact" and i==2,
            "id":(r["id"],r["gen"],r["world"])}
    expected_count=8 if case=="contact" else 7
    require(set(history)==set(range(1201)) and all(set(b)==set(range(expected_count)) for b in history.values()),"incomplete body history")
    baseline=history[0]
    require(all(b["id"]==baseline[i]["id"] for states in history.values() for i,b in states.items()),"body identity changed")
    require(abs(baseline[1]["im"]-.01)<1e-8 and baseline[1]["half"]==(3.,.25,.25) and
            (case!="contact" or baseline[2]["im"]==1 and baseline[2]["half"]==(1.,1.,1.)),"mass or geometry differs from frozen run")
    motor=rows("motor.csv")
    require(len(motor)==1201 and all(int(r["tick"])==i for i,r in enumerate(motor)),"incomplete motor series")
    from struct import pack,unpack
    def f32(x): return unpack("f",pack("f",x))[0]
    cap=f32(100*f32(dt))
    loads={int(r["tick"]):r for r in rows("load.csv")}
    require(set(loads)==(set(range(1,31)) if case=="free" else set()),"opposing load schedule")
    work=load_work=oracle=0.
    initial=sum(energy(b) for b in baseline.values())
    residual=[0.]
    peaks=defaultdict(float); violations=defaultdict(list); episodes={}; quiet=defaultdict(int); quiet_series=defaultdict(list)
    hits=[False]*3; holds=[0]*3; hold_runs=[0]*3; release=[False,False]
    schedule=contract["workload"]["commands_before_completed_tick_from_fresh_world"][case]
    angles=rules["stability"]["axis_and_limit_quiet_transient_max_rad"]
    impact=[]
    responses=rows("contacts.csv")
    for t in range(1,1201):
        r=motor[t]
        s={k:float(v) for k,v in r.items()}
        require(all(math.isfinite(v) for v in s.values()),"nonfinite drive sample")
        seg=next(x for x in schedule if x[0]<=t<=x[1])
        want=(seg[2],seg[3])
        enabled,target=want
        states=history[t]
        jcap=cap if enabled else 0.
        axis=states[0]["axes"][1]
        speed=g.dot(g.sub(states[1]["w"],states[0]["w"]),axis)
        def bound(name,value,limit):
            peaks[name]=max(peaks[name],value)
            if value>limit: violations[name].append(t)
        if (s["enabled"],s["target"],s["torque"],s["valid"],s["cap"])!=(float(enabled),target,100.,1.,jcap): violations["command_or_validity"].append(t)
        bound("per_pass_cap_excess",max(abs(s["impulse"]),s["peak"])-jcap,0)
        if abs(s["impulse"])>s["peak"] or bool(s["saturated"])!=(jcap>0 and abs(s["impulse"])>=jcap): violations["effort_readback"].append(t)
        bound("speed_readback",abs(s["speed"]-speed),resolution)
        bound("tracking_error_readback",abs(s["error"]-(target-speed)),resolution)
        if not enabled and (s["impulse"] or s["work"]): violations["disabled_work"].append(t)
        if t in loads:
            row=loads[t]; j=float(row["impulse"]); k=float(row["inverse_inertia"]); before=float(row["pre_speed"])
            require(j==-cap and abs(k-1/inertia)<1e-7 and abs(before-oracle)<=resolution,"invalid external load measurement")
            load_work+=j*(before+.5*j*k)
            oracle+=j/inertia
        if case=="free":
            start_speed=oracle
            oracle+=max(-jcap/inertia,min(jcap/inertia,target-oracle)) if enabled else 0.
            bound("free_oracle_rad_s",abs(speed-oracle),resolution)
            if t<=30 and not (s["impulse"]==jcap and s["saturated"] and abs(speed)<=resolution): violations["stall"].append(t)
            if enabled and target==0 and start_speed>resolution and s["impulse"]>=0: violations["braking_polarity"].append(t)
        work+=s["work"]
        residual.append(sum(energy(b) for b in states.values())-initial-work-load_work)
        rest=rules["energy"]["rest_allowance_J"]["contact" if case=="contact" else "free_or_limit"]
        bound("energy_excess_J",residual[-1],max(rest,rules["energy"]["loaded_allowance_J"]))
        dynamic=[i for i,b in states.items() if b["im"]]
        for i in dynamic:
            b=states[i]
            is_quiet=b["sleep"] or norm(b["v"])<=.05 and norm(b["w"])<=.02
            quiet[i]=quiet[i]+1 if is_quiet else 0
            quiet_series[i].append(bool(is_quiet))
            if quiet[i]>=60:
                bodies=[history[j][i] for j in range(t-59,t+1)]
                length=2 if b["sphere"] else .5
                for j,b1 in enumerate(bodies):
                    for b2 in bodies[j+1:]:
                        bound("quiet_position_pp_"+str(i),math.dist(b1["pos"],b2["pos"]),.001*length)
                        bound("quiet_angle_pp_"+str(i),g.arc(b1["q"],b2["q"]),.002)
                    if 0<j<59:
                        midpoint=g.scale(g.add(bodies[j-1]["pos"],bodies[j+1]["pos"]),.5)
                        bound("quiet_position_hf_"+str(i),math.dist(b1["pos"],midpoint),.0005*length)
                        q0,q1=bodies[j-1]["q"],bodies[j+1]["q"]
                        if g.dot(q0,q1)<0:q1=g.scale(q1,-1)
                        bound("quiet_angle_hf_"+str(i),g.arc(b1["q"],g.unit(g.add(q0,q1))),.001)
        qbar=quiet[1]>=30 or states[1]["sleep"]
        delta=g.sub(g.add(states[1]["pos"],states[1]["axes"][1]),states[0]["pos"])
        bound("anchor_m",norm(delta),.005 if qbar else .01)
        independent_axis=math.acos(max(-1.,min(1.,g.dot(states[0]["axes"][1],states[1]["axes"][1]))))
        bound("axis_rad",max(s["axis"],independent_axis),angles[0 if qbar else 1])
        if t>=2:
            def v(index): return tuple(float(motor[index][k]) for k in ("dx","dy","dz"))
            bound("anchor_hf_m",math.dist(v(t-1),g.scale(g.add(v(t-2),v(t)),.5)),.001)
        if case=="limit":
            bound("stop_penetration_rad",max(0.,abs(s["angle"])-math.pi/4),angles[0 if qbar else 1])
            if t<=1080:
                n=(t-1)//360
                at=abs(s["angle"]-math.copysign(math.pi/4,target))<=angles[1]
                hits[n]|=at
                if n and abs(s["angle"]+math.copysign(math.pi/4,target))>angles[1]:release[n-1]=True
                hold_runs[n]=hold_runs[n]+1 if at and abs(speed)<=.02 else 0
                holds[n]=max(holds[n],hold_runs[n])
        for i in dynamic:
            for j,b in states.items():
                if i==j or (j in dynamic and i>j) or {i,j}=={0,1}:continue
                depth=signed_depth(states[i],b)
                pair=(i,j)
                bound("depth_m",depth,.005 if (quiet[i]>=30 and (j not in dynamic or quiet[j]>=30)) else .01)
                if depth>.005:
                    if pair not in episodes: episodes[pair]=[t,0]
                    episodes[pair][1]=0
                elif pair in episodes:
                    episodes[pair][1]+=1
                    if episodes[pair][1]==3:del episodes[pair]
                if pair in episodes:
                    first,below=episodes[pair]
                    if t-below-first>=4:violations["contact_recovery"].append(t)
        if case=="contact" and t<=47:
            pair=[c for c in responses if int(c["tick"])==t and {int(c["a"]),int(c["b"])}=={1,2}]
            if reservation["backend"]=="Jolt":
                if any(float(c["normal_after"])>float(c["normal_before"]) and float(c["normal_before"])<0 for c in pair):impact.append(t)
            else:
                old=history[t-1][2]
                # A free sphere can change vertical momentum only through gravity
                # and physical contact. Exclude every other possible body across
                # this interval before attributing a positive impulse to the bar.
                margin=dt*(max(norm(old["v"]),norm(states[2]["v"]))+10*dt)
                def sphere_gap(box):
                    offset=g.sub(old["pos"],box["pos"])
                    return norm([max(0,abs(g.dot(offset,n))-h) for n,h in zip(box["axes"],box["half"])])-old["half"][0]
                others_clear=all(sphere_gap(box)>margin for j,box in states.items() if j not in (1,2))
                before=old["v"][1]-10*dt
                roundoff=4*2**-23*(abs(old["v"][1])+abs(states[2]["v"][1])+10*dt)
                if others_clear and states[2]["v"][1]>before+roundoff and (pair or sphere_gap(history[t-1][1])<=margin+dt*norm(history[t-1][1]["w"])*norm(history[t-1][1]["half"])):
                    impact.append(t)
    if episodes:violations["open_depth_episode"].append(1200)
    if case=="contact":check("beam_sphere_normal_response",bool(impact),impact)
    if case=="limit":check("both_stops_and_release",all(hits) and min(holds)>=30 and all(release),[hits,holds,release])
    for start,end,*_ in schedule:
        bins=[statistics.fmean(residual[t:t+60]) for t in range(start,end-58,60)]
        if len(bins)>=2:
            x=range(len(bins)); xm=statistics.mean(x);ym=statistics.mean(bins)
            trend=sum((i-xm)*(y-ym) for i,y in zip(x,bins))/sum((i-xm)**2 for i in x)
            if trend*(len(bins)-1)>rest:violations["energy_bin_trend"].append(start)
    for segment_start,end,*_ in schedule:
        quiet_start=None
        for t in range(segment_start,end+1):
            if t in loads or not all(flags[t-1] for flags in quiet_series.values()):
                quiet_start=None
                continue
            if quiet_start is None: quiet_start=t
            if t-quiet_start>=29 and max(residual[quiet_start:t+1])-residual[quiet_start]>rest:
                violations["quiet_energy_growth"].append(t)
    metrics["numerical_peaks"]=dict(peaks)
    metrics["violations"]={name:{"first":v[0],"last":v[-1],"samples":len(set(v))} for name,v in violations.items() if v}
    check("frozen_numerical_envelope",not any(violations.values()))
    cost=rows("cost.csv")
    require([int(r["tick"]) for r in cost]==list(range(1,1201)),"incomplete CPU series")
    for column,qfactor,mean_limit,tail_limit in (("inclusive_ns",1,dt/2,dt*.75),("observer_upper_ns",2,dt/100,dt/50)):
        values=[int(r[column]) for r in cost]
        q=max(int(r["q"]) for r in cost)
        stats=g.statistics_for(values)
        metrics[column]=stats
        check(column,0<q<=1000 and min(values)>0 and max(stats["mean"],stats["median"])+q*qfactor<=mean_limit*1e9 and
              max(stats["p95"],stats["max_sliding60"])+q*qfactor<=tail_limit*1e9)

def validate_sites(data,reservation,checks,metrics):
    """Classify observed return addresses in the bound executable. Unknowns stay open."""
    import bisect
    import csv
    import re
    from collections import defaultdict
    events=list(csv.DictReader((data/"allocations.csv").read_text().splitlines()))
    if reservation["backend"]=="Native":
        checks["arena_sites"]={"pass":not any(e["allocator"]=="arena" for e in events)}
        return
    compiled=reservation["compiled"]
    site=compiled["sites"]
    reviewed = {}
    review_ref = read(EVIDENCE/"isolated-Release/result.json").get("arena_site_review")
    if review_ref:
        review_path = ROOT/review_ref["path"]
        require(sha(review_path) == review_ref["sha256"], "Arena return-address review changed")
        review = read(review_path)
        require(review["binary_sha256"] == reservation["binary_sha256"] and
                review["map_sha256"] == compiled["linker_map"]["sha256"] and
                review["compiled_sizes"] == compiled["abi_sizes"], "Arena return-address review targets another build")
        for entry in review["disassembly"].values():
            evidence = entry["evidence"]
            require(sha(ROOT/evidence["path"]) == evidence["sha256"], "Arena disassembly evidence changed")
        reviewed = review["addresses"]
    require(sha(compiled["linker_map"]["path"])==compiled["linker_map"]["sha256"],"Arena linker map drift")
    matches=re.findall(r"^\s*[\da-fA-F]+:[\da-fA-F]+\s+(\S+)\s+([\da-fA-F]{12,16})\b",
                       Path(compiled["linker_map"]["path"]).read_text(),re.M)
    symbols=sorted((int(address,16),name) for name,address in matches)
    addresses=[x[0] for x in symbols]
    entries=defaultdict(list)
    unknown=[]
    delta=int((data/"address.txt").read_text())-compiled["preferred_sizes_va"]
    # Each group corresponds to a source function. Finalize owns both the
    # constraint and contact bucket ranges when their helpers have been inlined.
    groups={
        "Update@PhysicsSystem":["step_frames","body_pairs","active_constraints"],
        "STLTempAllocator":["step_frames"],
        "PrepareConstraintBuffer@ContactConstraintManager":["contact_buffer","contact_offsets"],
        "PrepareContactConstraints@IslandBuilder":["contact_links","JPH_VALIDATE_ISLAND_BUILDER"],
        "PrepareNonContactConstraints@IslandBuilder":["constraint_links"],
        "BuildBodyIslands@IslandBuilder":["body_islands","body_starts"],
        "BuildConstraintIslands@IslandBuilder":["constraint_islands","constraint_ends","contact_islands","contact_ends"],
        "SortIslands@IslandBuilder":["sorted_islands","sort_counts_transient"],
        "Finalize@IslandBuilder":["body_islands","body_starts","constraint_islands","constraint_ends","contact_islands",
                                 "contact_ends","sorted_islands","sort_counts_transient","position_steps","JPH_TRACK_SIMULATION_STATS"],
        "JobPreIntegrateVelocity@PhysicsSystem":["ccd_bodies","active_ccd_mapping"],
        "JobResolveCCDContacts@PhysicsSystem":["sorted_ccd_transient"]}
    for e in events:
        if e["allocator"]!="arena" or e["reserve"]!="1" or int(e["requested"])==0:continue
        address=int(e["caller"])-delta
        i=bisect.bisect_right(addresses,address)-1
        name=symbols[i][1] if i>=0 else "unmapped"
        n=int(e["requested"])
        bodies=2 if reservation["case"]=="contact" else 1
        active=range(1,bodies+1)
        pointer_size,_,_,step_size,ccd_size,contact_size,pair_size,_=compiled["abi_sizes"]
        exact={"step_frames":{step_size}, "body_pairs":{16384*pair_size}, "active_constraints":{pointer_size},
               "contact_buffer":{16384*contact_size},"contact_offsets":{65536},"contact_links":{65536},
               "JPH_VALIDATE_ISLAND_BUILDER":{131072},"constraint_links":{4},"constraint_islands":{4},
               "body_islands":{4*i for i in active},"body_starts":{4*(i+1) for i in range(bodies+1)},
               "constraint_ends":{4*(i+1) for i in range(bodies+1)},"contact_ends":{4*(i+1) for i in range(bodies+1)},
               "sorted_islands":{4*i for i in active},"sort_counts_transient":{4*i for i in active},
               "position_steps":set(active),"ccd_bodies":{ccd_size*i for i in active},"active_ccd_mapping":{4*i for i in active},
               "sorted_ccd_transient":{8*i for i in active}}
        binding = reviewed.get(hex(address))
        if binding:
            require(i >= 0 and name == binding["symbol"] and address-symbols[i][0] == binding["offset"] and
                    binding["site"] in exact, "Arena call instruction does not match the reviewed source site")
            candidates = (binding["site"],)
        else:
            candidates = (s for f,members in groups.items() if f in name for s in members)
        choices={s for s in candidates if s in site and int(e["aligned"])<=site[s] and
                 (s not in exact or n in exact[s])}
        if not choices:unknown.append([e["tick"],hex(address),name,e["requested"]])
        entries[int(e["tick"])].append((address,choices,name))
    # The two allocations within each of these pinned functions are unconditional
    # and ordered. For the shared constraint bucket helper, Finalize calls joints
    # first and contacts second. Use the measured non-contact link reservation.
    ordered={"SortIslands@IslandBuilder":("sorted_islands","sort_counts_transient"),
             "BuildBodyIslands@IslandBuilder":("body_islands","body_starts"),
             "JobPreIntegrateVelocity@PhysicsSystem":("ccd_bodies","active_ccd_mapping")}
    # Find a distinct source slot for each actual positive reserve in a single Update.
    # Ambiguous multiple source sites are separately recorded for provenance review.
    mismatch=[];ambiguous={}
    for t,group in entries.items():
        used={}
        for symbol,names in ordered.items():
            members=[i for i,(_,_,f) in enumerate(group) if symbol in f]
            if len(members)==len(names):
                for i,site_name in zip(members,names):
                    group[i][1].intersection_update({site_name})
        buckets=[i for i,(_,_,f) in enumerate(group) if "BuildConstraintIslands@IslandBuilder" in f]
        have_joint=any(choices=={"constraint_links"} for _,choices,_ in group)
        names=(["constraint_islands","constraint_ends"] if have_joint else [])+["contact_islands","contact_ends"]
        for i,site_name in zip(buckets,names):
            group[i][1].intersection_update({site_name})
        def place(i,seen):
            for s in sorted(group[i][1]):
                if s in seen:continue
                seen.add(s)
                if s not in used or place(used[s],seen):
                    used[s]=i
                    return True
            return False
        if not all(place(i,set()) for i in range(len(group))):mismatch.append(t)
        for addr,choices,_ in group:
            if len(choices)>1: ambiguous[hex(addr)]=sorted(choices)
    metrics["arena_sites"]={"ticks_with_reservations":len(entries),"unknown_first":unknown[:8],
                           "unmatched_ticks":mismatch,"ambiguous_addresses":ambiguous}
    checks["arena_site_coverage"]={"pass":bool(entries) and not unknown and not mismatch and not ambiguous,
        "detail":"Ambiguous symbols need offline return-address/disassembly attribution against this PE before PASS."}

def assess(role):
    import csv
    from pre_editor_phase29_qualification import arena_audit
    dest=EVIDENCE/"runs"/role
    data=dest/"data"
    checks={}
    metrics={}
    reservation=read(dest/"reservation.json")
    def check(name,ok,detail=None):
        checks[name]={"pass":bool(ok),"detail":detail}
    try:
        outcome=(data/"outcome.txt").read_text().splitlines()
        attempted,completed=map(int,outcome[1].split())
        result=read(dest/"exit.json")
        check("process_exit",not result["timeout"] and result["exit_code"]==0 and outcome[0]=="CAPTURE_COMPLETE",outcome)
        check("tick_account",attempted==completed and 0<attempted<=reservation["max_attempted"],[attempted,completed])
        if role=="debug":
            rows=list(csv.DictReader((data/"safety.csv").read_text().splitlines()))
            check("both_providers_and_reconstruction",[r["backend"] for r in rows]==["Jolt","Native","scene_reset_switch"] and
                  all(r["result"]=="PASS" and r["domain"]!=r["fresh_domain"] for r in rows) and
                  sum(int(r["attempted"]) for r in rows)==attempted and 10<attempted<=120)
            check("viewport_capture",(data/"ordinary-viewport.png").is_file())
        else:
            label, requests = outcome[2].split()
            require(label == "driver_requests", "Missing Release driver request count")
            requests = int(requests)
            check("driver_requests", requests == attempted, {"requests": requests, "physics_attempted": attempted,
                                                             "physics_completed": completed})
            check("full_workload",attempted==1200)
            measure_numeric(data,reservation,checks,metrics)
            cost=list(csv.DictReader((data/"cost.csv").read_text().splitlines()))
            info=arena_audit(data,cost,reservation,[None,None,None,int((data/"address.txt").read_text())])
            metrics["allocations"]=info
            check("job_and_heap_sites",not info["unclassified_heap_site_pairs"] and
                  (reservation["backend"]!="Jolt" or info["job_pool_teardown"]["status"]=="MATCHED"))
            check("joint_heap",all(int(r["joint_calls"])==0 and int(r["joint_bytes"])==0 for r in cost))
            validate_sites(data,reservation,checks,metrics)
    except (ValueError, OSError, KeyError, IndexError, TypeError, ArithmeticError) as error:
        check("complete_evidence",False,str(error))
    passed=bool(checks) and all(item["pass"] for item in checks.values())
    pending_only=all(item["pass"] or name in ("complete_evidence","arena_site_coverage","observer_upper_ns") for name,item in checks.items())
    verdict="PASS" if passed else "INCOMPLETE" if pending_only else "FAIL"
    record(dest/"assessment.json",{"role":role,"status":verdict,"checks":checks,"metrics":metrics})
    return 0 if passed else 1

def offline():
    """Check the actual input pins and deterministic math without starting RBS."""
    import ast
    import math
    import struct
    from collections import Counter
    admission=read(PACKET)
    activation=read(ROOT/"docs/pre_editor/active/PHASE_30_ENTRY_ACCEPTANCE_01.json")
    require(all(sha(ROOT/path)==digest for path,digest in activation["inputs"].items()),"Admitted input changed")
    require(all(sha(ROOT/path)==digest for path,digest in admission["cost"]["arena"]["source_pins"].items()),"Pinned SDK drift")
    common=admission["workload"]["common"]
    half=common["bar"]["half_extents_m"]
    inertia=common["bar"]["mass_kg"]*(half[0]**2+half[2]**2)/3
    dt=1/60
    cap=100*dt
    oracle=admission["physics_checks"]["mass_oracle"]
    require(math.isclose(inertia,oracle["I_axis_kg_m2"]) and math.ceil(2*inertia/cap)==363,"free-bar inertia or deadline")
    f32=lambda x:struct.unpack("f",struct.pack("f",x))[0]
    require(f32(100*f32(dt))==oracle["representable_solver_cap_Nms"],"binary32 impulse cap")
    schedules=admission["workload"]["commands_before_completed_tick_from_fresh_world"]
    for case,commands in schedules.items():
        require([t for lo,hi,*_ in commands for t in range(lo,hi+1)]==list(range(1,1201)),case+" schedule coverage")
    speed=work=external=0.
    endpoints={}
    for lo,hi,enabled,target,_ in schedules["free"]:
        for t in range(lo,hi+1):
            j=-cap if t<=30 else 0.
            external+=j*(speed+.5*j/inertia)
            speed+=j/inertia
            j=max(-cap,min(cap,(target-speed)*inertia)) if enabled else 0.
            work+=j*(speed+.5*j/inertia)
            speed+=j/inertia
            require(abs(.5*inertia*speed**2-work-external)<1e-8,"independent analytic work identity")
        endpoints[hi]=speed
    require(all(abs(endpoints[t]-v)<1e-12 for t,v in [(30,0),(393,2),(423,2),(786,0),(816,0),(1179,-2),(1200,-2)]),"command endpoints")
    components=read(PACKET.parent/"component-assessment.json")["inventory"]
    ids={r["row_id"] for r in components}
    require(len(ids)==len(components)==1011,"frozen component membership")
    require({r["row_id"] for r in read(PACKET.parent/"component-matrix.json")["rows"]}==ids,"matrix/inventory join")
    ast.parse(Path(__file__).read_bytes())
    scope=source_identities(include_runtime_assets=True)
    return {"status":"PASS_STATIC_ONLY","inputs":activation["inputs"],"current_source":scope,"gate_effect":"No runtime or UI gate closed",
            "reference_inertia_kg_m2":inertia,"representable_cap_Nms":f32(100*f32(dt)),"free_oracle_segment_end_rad_s":endpoints,
            "motor_schedules":list(schedules),"rows":len(ids),"capabilities":dict(Counter(r["expected_capability"] for r in components)),
            "missing_publication_member":[path for path in scope if scope[path] is None]}

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("action",choices=["build","isolated-build","run","offline"])
    parser.add_argument("role",nargs="?",help="Build configuration or one named fixture role")
    args=parser.parse_args()
    if args.action=="offline":
        print(json.dumps(offline(),indent=2))
        sys.exit(0)
    sys.exit({"build":ordinary_build,"isolated-build":isolated_build,"run":supervisor}[args.action](args.role))
