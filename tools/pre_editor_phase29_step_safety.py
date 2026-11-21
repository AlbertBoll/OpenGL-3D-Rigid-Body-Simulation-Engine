"""Guard the Phase 29 Step safety builds and its single separately released Debug role."""
from __future__ import annotations

import collections
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
PHASE = ROOT / "logs/pre_editor/phase-29"
EVIDENCE = PHASE / "step-safety-01"
sys.path.insert(0, str(PHASE / "resume-01"))
import build as prior
import pre_editor_phase29_qualification as motion

LIMITS = {"Debug": 975.0, "Release": 676.751}
PROFILES = {"safety": "Debug", "ordinary": "Release", "qualification": "Release"}
NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
ENVELOPE = ROOT / "docs/pre_editor/active/PHASE_29_STEP_SAFETY_AMENDMENT_01.json"
OUTPUT = ROOT / "bin/phase29-step-safety-01"
INTERMEDIATE = ROOT / "bin-int/phase29-step-safety-01"
MARKER = "g_Phase29StepSafetyFixture"
SOURCE_EXTENSIONS = {".h", ".hh", ".hpp", ".inl", ".cpp", ".c", ".cxx", ".inc", ".ipp", ".tpp", ".def"}


def require(ok, reason):
    if not ok:
        raise ValueError(reason)


def read(path):
    return json.loads(path.read_bytes())


def sha(path):
    return prior.digest(path)


def ident(path):
    return {"path": str(path), "sha256": sha(path)}


def groups(path):
    result = {}
    group = None
    for line in path.read_text("utf-16").splitlines():
        if line.startswith("^"):
            group = Path(line[1:])
            result[group] = set()
        elif line:
            require(group is not None, path)
            result[group].add(Path(line))
    return result


def archive_members(path):
    hashes = collections.Counter()
    with path.open("rb") as file:
        require(file.read(8) == b"!<arch>\n", path)
        while header := file.read(60):
            require(len(header) == 60 and header[58:] == b"`\n", path)
            size = int(header[48:58])
            member = file.read(size)
            require(len(member) == size, path)
            if header[:16].strip() not in (b"/", b"//"):
                hashes[hashlib.sha256(member).hexdigest()] += 1
            if size % 2:
                file.read(1)
    return hashes


def closure(profile, project):
    config = PROFILES[profile]
    ordinary = profile == "ordinary"
    targets = prior.SOURCE[project]
    definitions = ET.parse(targets).findall(".//m:ClCompile[@Include]", NS)
    project_sources = {(targets.parent / x.attrib["Include"]).resolve():x.attrib["Include"] for x in definitions}
    configuration = Path(config) if ordinary else Path("phase28-qualification") / config
    if project == "RigidBodySimulation" and not ordinary:
        configuration = Path("phase29-qualification") / config
    tlog = next((ROOT / "bin-int" / configuration / project).rglob("CL.read.1.tlog")).parent
    items = {Path(s):Path(o) for s,o in (l.split(";") for l in (tlog/"Cl.items.tlog").read_text().splitlines())}
    deps = groups(tlog/"CL.read.1.tlog")
    previous = read(PHASE/"runtime-performance-02/builds/object-and-link-closure.json")
    ordinary_inputs = {Path(k):v for k,v in previous["consumed_workspace_input_sha256"].items()}
    phase28 = ROOT / "logs/pre_editor/phase-28"
    old = read(phase28 / prior.OLD_BUILD[config] / "inputs-before.json")
    pins = dict(ordinary_inputs)
    if not ordinary:
        pins.update({ROOT / k:v for k,v in old.items()})
    inputs, selected, reuse = {}, {}, {}
    pch = next(s for s in project_sources if s.stem.lower() in ("gepch","rbspch"))
    inherited = deps.get(pch, set())
    changed = {ROOT / k for k in read(ENVELOPE)["admitted_edit_paths"]}
    changed.add(ROOT / "RigidBodySimulation/tests/PassiveRagdollChecks.h")
    # Relative includes are tracked by the historic MSVC file tracker. The PCH
    # producer's include closure also applies to each PCH consumer.
    for src, spelling in project_sources.items():
        dependencies = {src} | deps.get(src, set()) | inherited
        local = {p for p in dependencies if p.is_relative_to(ROOT) and p.suffix.lower() in SOURCE_EXTENSIONS}
        reasons = []
        if src == pch or src not in items or src not in deps:
            reasons.append("PCH or new translation unit")
        for p in local:
            if not p.is_file():
                reasons.append("historic input removed: "+str(p))
                continue
            if p not in inputs:
                inputs[p] = sha(p)
            if p in changed or pins.get(p) != inputs[p]:
                reasons.append(str(p))
        if src.stem == "PassiveRagdoll":
            reasons.append("current provider uses changed world/rigid body ABI")
        if reasons:
            selected[spelling] = sorted(set(reasons))
        else:
            require(items[src].is_file(), items[src])
            reuse[spelling] = ident(items[src])
    archive_name = "bin/"+configuration.as_posix()+"/GEngine/GEngine.lib"
    libs = {}
    if ordinary:
        archive_name = "bin/Release/GEngine/GEngine.lib"
        provenance = read(PHASE/"runtime-performance-02/builds/pair.json")["normal_source_object_proof"]["archive_details"]
        for name,rel in {"GEngine":archive_name,"Jolt":"bin/Release/Jolt/Jolt.lib",
                         "glad":"external/glad/bin/Release/glad/glad.lib"}.items():
            path=ROOT/rel
            require(sha(path)==provenance[name]["normal_library_sha256"],path)
            libs[name]=ident(path)
    else:
        pin=read(PHASE/"resume-01/build-preparation.json")["archive_reuse"][config]["archives"]
        for rel, value in pin.items():
            path=ROOT/rel
            require(sha(path)==value,path)
            libs[path.stem]=ident(path)
    pch_companion=None
    if project=="GEngine":
        members=archive_members(Path(libs["GEngine"]["path"]))
        require(not (collections.Counter(v["sha256"] for v in reuse.values())-members),
                "retained object bytes absent from bound GEngine archive")
        require(selected[project_sources[pch]]==["PCH or new translation unit"],"original PCH has changed inputs")
        pch_companion=ident(items[pch])
        require(members[pch_companion["sha256"]]>0,"PCH companion absent from the original archive")
    elif ordinary:
        known={Path(k):v for k,v in previous["normal_objects"][project].items()}
        for name, value in reuse.items():
            require(known[(targets.parent/name).resolve()]["sha256"]==value["sha256"],name)
    elif reuse:
        # Only the small application closure: compile any object lacking a
        # previously checked standalone application object identity.
        for name in reuse:
            selected[name]=["application object identity requires recompilation"]
        reuse={}
    return {"selected":selected, "reused":reuse,"dependencies":{str(k):v for k,v in inputs.items()},
            "libraries":libs,"project":ident(targets),"archive_pch_companion":pch_companion,"tracker":[ident(tlog/k) for k in
            ("CL.read.1.tlog","Cl.items.tlog","CL.command.1.tlog")],"profile":profile}


def props(profile, project, info, work, output):
    root = ET.Element("Project", xmlns=NS["m"])
    pg=ET.SubElement(root,"PropertyGroup")
    for name,value in {"OutDir":str(output)+"\\","IntDir":str(work)+"\\",
                       "PostBuildEventUseInBuild":"false","LinkIncremental":"false"}.items():
        ET.SubElement(pg,name).text=value
    group=ET.SubElement(root,"ItemGroup")
    ET.SubElement(group,"ProjectReference",Remove="@(ProjectReference)")
    for source in info["reused"]:
        ET.SubElement(group,"ClCompile",Remove=source)
    definitions=ET.SubElement(root,"ItemDefinitionGroup")
    compile=ET.SubElement(definitions,"ClCompile")
    for key,name in (("ProgramDataBaseFileName","compile.pdb"),("PrecompiledHeaderOutputFile",project+".pch")):
        ET.SubElement(compile,key).text=str(work/name)
    ET.SubElement(compile,"ObjectFileName").text=str(work)+"\\"
    ET.SubElement(compile,"AssemblerListingLocation").text=str(work)+"\\"
    ET.SubElement(compile,"ShowIncludes").text="true"
    if profile=="safety" and project=="RigidBodySimulation":
        # Preserve Debug symbols, /Od, CRT, and assertions. This noninteractive
        # fixture never uses live Visual Studio Edit-and-Continue patching.
        ET.SubElement(compile,"DebugInformationFormat").text="ProgramDatabase"
    if profile!="ordinary":
        flags=["GENGINE_PHASE29_QUALIFICATION","GENGINE_PHASE28_QUALIFICATION","GENGINE_JOLT_FINAL_AUDIT",
               "GE_ENABLE_PHYSICS_PROFILING","JPH_EXTERNAL_PROFILE"]
        if profile=="safety":
            flags.append("GENGINE_PHASE29_STEP_SAFETY")
        if project=="RigidBodySimulation":
            flags+=["GENGINE_WITH_JOLT","JPH_CROSS_PLATFORM_DETERMINISTIC","JPH_OBJECT_LAYER_BITS=16"]
            if profile=="qualification":
                flags.append("JPH_NO_DEBUG")
            ET.SubElement(compile,"AdditionalIncludeDirectories").text=str(ROOT/"external/jolt")+";%(AdditionalIncludeDirectories)"
        ET.SubElement(compile,"PreprocessorDefinitions").text=";".join(flags+["%(PreprocessorDefinitions)"])
    retained=[v["path"] for v in info["reused"].values()]
    if project=="GEngine":
        lib=ET.SubElement(definitions,"Lib")
        ET.SubElement(lib,"AdditionalDependencies").text=";".join(retained+["%(AdditionalDependencies)"])
    else:
        link=ET.SubElement(definitions,"Link")
        libs=[info["archive_pch_companion"]["path"],str(OUTPUT/profile/"GEngine/GEngine.lib"),info["libraries"]["Jolt"]["path"],info["libraries"]["glad"]["path"]]
        ET.SubElement(link,"AdditionalDependencies").text=";".join(retained+libs+["%(AdditionalDependencies)"])
        ET.SubElement(link,"GenerateMapFile").text="true"
        ET.SubElement(link,"MapFileName").text=str(output/"RigidBodySimulation.map")
    ET.indent(root)
    return ET.tostring(root,encoding="unicode")


def build(profile,project,attempt):
    config=PROFILES[profile]
    require(project in prior.SOURCE and re.fullmatch(r"[0-9]{2}",attempt),"invalid build role")
    work=prior.safe(INTERMEDIATE/profile/project)
    out=prior.safe(OUTPUT/profile/project)
    rec=prior.safe(EVIDENCE/"builds"/(profile+"-"+project+"-"+attempt))
    # Each attempt is retained, including a tool crash without a final debit.
    used=0.0
    for invocation in (EVIDENCE/"builds").glob("*/invocation.json"):
        data=read(invocation)
        if data["configuration"]==config:
            result=invocation.with_name("result.json")
            used+=read(result)["seconds"] if result.is_file() else data["remaining_seconds"]
    remaining=LIMITS[config]-used
    require(remaining>0,"compiler time exhausted")
    rec.mkdir(parents=True,exist_ok=False)
    info=closure(profile,project)
    if project=="RigidBodySimulation":
        candidates=sorted((EVIDENCE/"builds").glob(profile+"-GEngine-*/result.json"))
        require(candidates and read(candidates[-1])["status"]=="PASS","missing engine closure")
        require(sha(OUTPUT/profile/"GEngine/GEngine.lib")==read(candidates[-1])["product"]["sha256"],"engine drift")
        engine=read(candidates[-1].with_name("closure.json"))
        companion=engine["archive_pch_companion"]
        require(ident(Path(companion["path"]))==companion,"archive PCH companion drift")
        info["archive_pch_companion"]=companion
    prior.write(rec/"closure.json",info)
    work.mkdir(parents=True,exist_ok=True)
    out.mkdir(parents=True,exist_ok=True)
    profile_path=rec/"build.props"
    prior.write(profile_path,props(profile,project,info,work,out))
    command=[str(prior.MSBUILD),str(prior.SOURCE[project]),"/t:Build","/p:Configuration="+config,"/p:Platform=x64",
             "/p:VCToolsVersion=14.44.35207","/p:WindowsTargetPlatformVersion=10.0.26100.0","/p:PreferredToolArchitecture=x64",
             "/p:BuildProjectReferences=false","/p:MultiProcessorCompilation=false","/p:ForceImportBeforeCppTargets="+str(profile_path),
             "/p:UseMultiToolTask=true","/p:MultiProcMaxCount=1","/p:CL_MPCount=1",
             "/p:EnforceProcessCountAcrossBuilds=true","/m:1","/nr:false","/nologo","/v:minimal"]
    prior.write(rec/"invocation.json",{"configuration":config,"remaining_seconds":remaining,"prior_new_seconds":used,
        "command":command,"source_envelope":ident(ENVELOPE),"driver":ident(Path(__file__)),"profile":ident(profile_path),
        "guard":ident(PHASE/"resume-01/build.py"),"toolchain":read(ROOT/"logs/pre_editor/phase-28/design-02/platform-inputs.json").get("tool_sha256")})
    result=prior.monitor(command,rec,remaining)
    result["inputs_unchanged"]=all(sha(Path(k))==v for k,v in info["dependencies"].items())
    product=out/(project+(".lib" if project=="GEngine" else ".exe"))
    result["status"]="FAIL"
    if result["exit_code"]==0 and not result["guard"] and result["inputs_unchanged"] and product.is_file():
        result["product"]=ident(product)
        if project=="RigidBodySimulation":
            linkmap=out/"RigidBodySimulation.map"
            result["map"]=ident(linkmap)
            result["debug_marker_in_map"]=MARKER in linkmap.read_text(errors="replace")
            if result["debug_marker_in_map"]!=(profile=="safety"):
                result["identity_error"]="fixture marker/profile mismatch"
        if "identity_error" not in result:
            result["status"]="PASS"
    prior.write(rec/"result.json",result)
    print(json.dumps(result))
    return int(result["status"]!="PASS")


def candidate(profile):
    """Bind the last successful build of both layers, their inputs and the actual PE."""
    records = {}
    for project in ("GEngine", "RigidBodySimulation"):
        invocations = sorted((EVIDENCE / "builds").glob(f"{profile}-{project}-*/invocation.json"))
        require(invocations, "missing " + project + " build")
        last = invocations[-1].parent
        result = read(last / "result.json")
        info = read(last / "closure.json")
        require(result["status"] == "PASS" and not result["guard"] and result["inputs_unchanged"],
                "unsuccessful or stale build " + str(last))
        for name, value in info["dependencies"].items():
            require(sha(Path(name)) == value, "source drift " + name)
        for item in [info["project"], *info["libraries"].values(), *info["reused"].values(),
                     info["archive_pch_companion"], result["product"]]:
            require(ident(Path(item["path"])) == item, "build input/product drift " + item["path"])
        require(ident(last / "build.props") == read(last / "invocation.json")["profile"], "build profile drift")
        records[project] = {"build": motion.identity(last / "result.json"), "product": result["product"]}
    image = Path(result["product"]["path"])
    map_file = Path(result["map"]["path"])
    require(ident(map_file) == result["map"], "link map drift")
    linkmap = map_file.read_text(errors="replace")
    with image.open("rb") as file:
        file.seek(0x3c)
        offset = struct.unpack("<I", file.read(4))[0]
        file.seek(offset)
        require(file.read(6) == b"PE\0\0\x64\x86", "expected PE x64")
    safety = profile == "safety"
    for symbol in (MARKER, "Phase29ReserveSafetyAttempt", "PublicationFingerprintForTest"):
        require((symbol in linkmap) == safety, "unexpected Debug symbol profile " + symbol)
    diagnostic = profile != "ordinary"
    require(("g_Phase29CaptureConfiguration" in linkmap) == diagnostic, "qualification marker isolation")
    info = {"profile": profile, "builds": records, "link_map": motion.identity(map_file)}
    if safety:
        _, words = motion.pe_words(image, linkmap, MARKER, 7)
        require(words[:2] == [1, 8] and all(words[2:]), "invalid safety role marker")
        info["fixture_version_attempt_cap_and_layout"] = words
    if diagnostic:
        slot = "attempt-04-D-N" if safety else "attempt-03-R-Jolt-R29_DROP_WAKE_V01"
        baseline = read(motion.RUNS / slot / "reservation.json")["compiled"]
        _, words = motion.pe_words(image, linkmap, "g_Phase29CaptureConfiguration", 16)
        require(words[:11] == [*baseline["target_sizes"], baseline["contact_sample_bytes"],
                          int(baseline["JPH_VALIDATE_ISLAND_BUILDER"]), int(baseline["JPH_TRACK_SIMULATION_STATS"])],
                "SDK layout or flag mismatch")
        info["sdk_layout"] = words[:11]
        info["engine_layout_world_system_scene_error_failure"] = words[11:]
        if safety:
            require(words[11:] == info["fixture_version_attempt_cap_and_layout"][2:], "Debug layout disagreement")
    return info


HOLD = EVIDENCE / "fixture-hold.json"
CUTOFF = EVIDENCE / "owner-witnessed-cutoff-01.json"
RESERVATION = EVIDENCE / "safety-role-01"
ORDINARY_LOG = ROOT / "RigidBodySimulation/GEngine.log"


def release():
    """Only the named successor can dispose the Owner's open historical accounting hold."""
    require(read(HOLD)["release"] is False, "historical hold changed")
    require(CUTOFF.is_file(), "unbounded ordinary history, no new witnessed cutoff")
    require(not RESERVATION.exists(), "supervised role already reserved, zero retries")
    cutoff = read(CUTOFF)
    require(cutoff["decision"] == "RELEASE_STEP_SAFETY_ONLY" and cutoff["supersedes"] == motion.identity(HOLD)
            and cutoff["role_limits"] == {"starts": 1, "attempts": 8, "wall_seconds": 180, "retries": 0,
                                          "replacements": 0}, "wrong role authorization")
    owner = cutoff["owner_disposition"]
    require(motion.identity(owner["path"]) == owner and owner["path"] not in {
        motion.identity(HOLD)["path"], motion.identity(CUTOFF)["path"]}, "missing separate Owner disposition")
    decision = read(motion.local(owner["path"]))
    require(decision["role"] == "PHASE_29_STEP_SAFETY" and decision["owner_statement"].strip()
            and decision["disposition"] in ("CONFIRMED_UPPER_BOUND","EXPLICIT_ACCOUNTING_DISPOSITION")
            and decision["covers_all_later_ordinary_starts"] is True, "unresolved historical sessions")
    witnessed = datetime.fromisoformat(cutoff["witnessed_utc"])
    disposition_time = datetime.fromisoformat(decision["recorded_utc"])
    require(witnessed.utcoffset() is not None and disposition_time.utcoffset() is not None
            and disposition_time <= witnessed <= datetime.now(timezone.utc)
            and witnessed.timestamp() >= ORDINARY_LOG.stat().st_mtime, "invalid or stale witnessed cutoff")
    require(cutoff["all_ordinary_starts_reconciled"] is True and cutoff["exclusive_window_witnessed"] is True
            and cutoff["ordinary_log"] == motion.identity(ORDINARY_LOG)
            and cutoff["ordinary_log_mtime_ns"] == ORDINARY_LOG.stat().st_mtime_ns, "ordinary epoch lacks a current witness")
    history = {p.name: {"reservation": motion.identity(p / "reservation.json"),
                        "process": motion.identity(p / "process.json"), "ticks": row["debit"]}
               for p, row in motion.ledger()}
    require(cutoff["fixture_history"] == history and len(history) == 6, "fixture ledger changed")
    hold = read(HOLD)
    additional = decision["later_ordinary_tick_debit"]
    require(type(additional) is int and additional>=0, "ordinary tick debit must be bounded")
    if additional < sum(hold["additional_provisional_holds"]):
        require(decision.get("explicitly_reconciles_both_provisional_holds") is True, "open provisional holds")
    booked = cutoff["reconciled_total_ticks_before_role"]
    require(type(booked) is int and booked==hold["historical_booked"]+additional and booked+8<=8400,
            "actual reconciled headroom insufficient")
    binary = candidate("safety")
    require(binary == cutoff["candidate"] and cutoff["supervisor"] == motion.identity(__file__),
            "cutoff does not bind this candidate and supervisor")
    return cutoff, binary


def runtime_inputs():
    stage = PHASE / "resume-01/staging-Debug-02.json"
    pinned = read(stage)
    assets = motion.local(pinned["asset_root"])
    require(pinned["manifest"] == sha(ROOT / "tools/runtime_assets.json"), "runtime asset manifest drift")
    for name, digest in pinned["resource_files"].items():
        require(sha(assets / name) == digest, "runtime asset drift " + name)
    dlls = assets.parent / "RigidBodySimulation"
    for name, digest in pinned["runtime_dlls"].items():
        require(Path(name).name == name and sha(dlls / name) == digest, "runtime DLL drift " + name)
    return assets, dlls, pinned, motion.identity(stage)


def reconcile(data, engine_log):
    rows = [line.split("\t") for line in (data / "cases.tsv").read_text().splitlines()]
    expected = [("prepare",1,0,0,10,0), ("two-tick",2,1,2,16,0), ("capacity",1,0,1,21,1),
                ("native",2,1,3,25,0), ("fresh",1,1,-1,-1,0), ("fatal",1,0,2,16,0)]
    require(len(rows) == len(expected), "missing/extra subcase")
    domains = set()
    for row, case in zip(rows, expected):
        name, attempts, completions, domain, stage, code, mask = row
        require((name, *map(int,(attempts,completions,stage,code,mask))) == case, "case mismatch " + name)
        require(int(domain)>0 and domain not in domains, "world domain reused")
        domains.add(domain)
    final_domain = rows[-1][3]
    domain, stage, code, slot, generation, operation = (data / "fatal.tsv").read_text().strip().split("\t")
    require((domain,stage,code,operation)==(final_domain,"2","16","Jolt body mirror publication")
            and 0<=int(slot)<4096 and int(generation)>0, "wrong original fatal cause")
    require((data / "shutdown.tsv").read_text().split() == [domain,"8","3","0","1"],
            "shutdown, render or process counter mismatch")
    log = engine_log.read_text(encoding="utf-8-sig", errors="replace")
    require("code=BackendStep operation="+operation in log and "attempted=1 completed=0" in log
            and "world="+domain+" body="+slot+"/"+generation in log, "ordinary fatal dispatch missing")
    return {"attempted": sum(int(row[1]) for row in rows), "completed": sum(int(row[2]) for row in rows),
            "world_domains": [row[3] for row in rows], "observed_fatal": operation}


def run_safety():
    # Check the historical hold before creating even a campaign lock.
    release()
    with motion.lock():
        cutoff, binary = release()
        assets, dlls, pinned, staging = runtime_inputs()
        census = subprocess.run(["powershell.exe","-NoProfile","-NonInteractive","-Command",
            "[Diagnostics.Process]::GetProcessesByName('RigidBodySimulation').Length"],
            capture_output=True, timeout=15, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
        require(not census.stderr.strip() and census.stdout.strip()==b"0", "ordinary session active")
        parent = prior.safe(ROOT / "runtime/RigidBodySimulation/Debug")
        cwd = Path(tempfile.mkdtemp(prefix="phase29-step-safety-01-", dir=parent))
        exe = cwd / "RigidBodySimulation.exe"
        built = binary["builds"]["RigidBodySimulation"]["product"]
        shutil.copyfile(built["path"],exe)
        require(sha(exe)==built["sha256"], "staged executable drift")
        for name, digest in pinned["runtime_dlls"].items():
            shutil.copyfile(dlls/name,cwd/name)
            require(sha(cwd/name)==digest, "staged runtime dependency drift")
        command = [str(exe),"--scene","passive-ragdoll","--backend","jolt"]
        require(motion.identity(ORDINARY_LOG)==cutoff["ordinary_log"] and
                ORDINARY_LOG.stat().st_mtime_ns==cutoff["ordinary_log_mtime_ns"], "ordinary log changed during staging")
        RESERVATION.mkdir(exist_ok=False)
        data = RESERVATION / "data"
        data.mkdir()
        motion.write(RESERVATION/"reservation.json", {"utc":motion.utc(),"cutoff":motion.identity(CUTOFF),
            "candidate":binary,"staging":staging,"command":command,"cwd":str(cwd),
            "reserved_starts":1,"tick_debit":8,"max_seconds":180,"numerical_replacements":0})
        env = prior.environment()
        env.update(GENGINE_ASSET_ROOT=str(assets),GENGINE_PHASE29_STEP_SAFETY_OUTPUT=str(data))
        result = {"status":"FAIL","exit":None,"debited_ticks":8,"attempted":None,"completed":None,"timeout":False}
        try:
            with (RESERVATION/"stdout.log").open("xb") as log:
                process = subprocess.run(command,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
            result["exit"]=process.returncode
            require(process.returncode==1, "expected the application's typed fatal exit")
            require(binary==candidate("safety") and sha(exe)==built["sha256"]
                    and motion.identity(ORDINARY_LOG)==cutoff["ordinary_log"]
                    and ORDINARY_LOG.stat().st_mtime_ns==cutoff["ordinary_log_mtime_ns"], "candidate or ordinary epoch changed")
            result.update(reconcile(data,cwd/"GEngine.log"))
            result["status"]="PASS"
        except subprocess.TimeoutExpired:
            result["timeout"]=True
        except (OSError,ValueError,KeyError) as err:
            result["error"]=str(err)
        finally:
            if (data/"cases.tsv").is_file():
                result["partial_case_evidence"]=motion.identity(data/"cases.tsv")
            motion.write(RESERVATION/"process.json",result)
        return int(result["status"]!="PASS")


if __name__=="__main__":
    if len(sys.argv)==5 and sys.argv[1]=="build":
        raise SystemExit(build(sys.argv[2],sys.argv[3],sys.argv[4]))
    if sys.argv[1:]==["preflight"]:
        try:
            release()
            runtime_inputs()
        except (OSError, ValueError, KeyError) as err:
            print("HOLD:",err)
            raise SystemExit(2)
        print("Candidate and witnessed cutoff match.")
    elif sys.argv[1:]==["run-safety"]:
        raise SystemExit(run_safety())
    else:
        raise SystemExit("usage: build PROFILE PROJECT NN | preflight | run-safety")

