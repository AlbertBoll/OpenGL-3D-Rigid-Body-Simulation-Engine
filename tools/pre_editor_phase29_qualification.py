"""Bounded Phase 29 RBS fixtures, immutable captures and independent numerical evaluation."""
import argparse
import csv
import hashlib
import itertools
import json
import math
import os
import re
import statistics
import struct
import subprocess
import xml.etree.ElementTree as ET
from collections import Counter, defaultdict
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PHASE = ROOT / "logs/pre_editor/phase-29"
ADMISSION = PHASE / "admission-02"
RUNS = PHASE / "resume-01/qualification"
RESUMPTION = PHASE / "ragdoll-resume-02"
NATIVE_FIRST = RESUMPTION / "native-first-decision.json"
MANUAL_HEADROOM = RESUMPTION / "native-manual-headroom.json"
NATIVE_CUTOFF = RESUMPTION / "native-three-cutoff.json"
SIZES = [8, 4, 16, 7344, 160, 744, 8, 4]
ORDER = ["D-J", "R-Jolt-R29_DROP_WAKE_V01", "R-Jolt-R29_TILT_LIMIT_WAKE_V01",
         "D-N", "R-Native-R29_DROP_WAKE_V01", "R-Native-R29_TILT_LIMIT_WAKE_V01"]
PREREQUISITES = {ORDER[0]:[],ORDER[1]:[ORDER[0]],ORDER[2]:[ORDER[0]],ORDER[3]:ORDER[:3],
                 ORDER[4]:[ORDER[3]],ORDER[5]:[ORDER[3]]}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def local(path):
    raw = Path(path)
    path = (ROOT / raw).resolve()
    require(path.is_relative_to(ROOT), f"Output/input escapes worktree {raw}")
    return path


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, value):
    path = local(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8", newline="\n") as f:
        json.dump(value, f, indent=2, allow_nan=False)
        f.write("\n")


def sha(path):
    digest=hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):
            digest.update(block)
    return digest.hexdigest()


def utc():
    return datetime.now(timezone.utc).isoformat()


def identity(path):
    path = local(path)
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path)}


def source_inputs():
    envelope = read(ADMISSION / "source-envelope.json")
    names = [entry["path"] for k in ("production_runtime", "supporting") for entry in envelope[k]]
    # Documentation and the as-yet uncaptured thumbnail are outside fixture behavior.
    names = [n for n in names if not n.endswith((".md", ".png"))]
    closure = read(ADMISSION / "input-closure.json")["source_and_contract_hashes"]
    names.extend(n for n in closure if n.startswith(("GEngine/", "external/", "RigidBodySimulation/", "tools/")))
    names.extend(read(ADMISSION / "arena-proposal.json")["current_sources"])
    names.extend(str(ADMISSION / name) for name in
                 ["reference-packet.json", "criteria.json", "workloads.json", "execution-plan.json", "arena-proposal.json"])
    names.extend([__file__, str(PHASE / "resume-01/owner-decisions.json")])
    return {str(p.relative_to(ROOT).as_posix()): sha(p) for p in sorted({local(n) for n in names})}


def pe_words(exe, linkmap, symbol, count):
    contents = exe.read_bytes()
    pe = struct.unpack_from("<I", contents, 0x3c)[0]
    require(contents[pe:pe+4] == b"PE\x00\x00", "Invalid PE image")
    sections = struct.unpack_from("<H", contents, pe+6)[0]
    optsize = struct.unpack_from("<H", contents, pe+20)[0]
    opt = pe+24
    require(struct.unpack_from("<H", contents, opt)[0] == 0x20b, "Expected x64 PE32+")
    base = struct.unpack_from("<Q", contents, opt+24)[0]
    matches = re.findall(r"^\s*[0-9a-fA-F]+:[0-9a-fA-F]+\s+"+re.escape(symbol)+
                         r"\s+([0-9a-fA-F]+)\b", linkmap, re.M)
    require(len(matches) == 1, f"Missing or ambiguous compiler identity {symbol}")
    va = int(matches[0], 16)
    rva = va-base
    for i in range(sections):
        section = opt+optsize+i*40
        _, start, length, raw = struct.unpack_from("<IIII", contents, section+8)
        if start <= rva and rva+count*8 <= start+length:
            return va, list(struct.unpack_from("<"+"Q"*count, contents, raw+rva-start))
    raise ValueError(f"{symbol} outside file-backed PE sections")


def compiled(exe, map_path, build):
    built=read(build); products=built.get("product",{})
    require(built.get("status")=="PASS" and built.get("inputs_unchanged") and not built.get("guard") and
            built.get("exit_code")==0 and products.get(exe.relative_to(ROOT).as_posix())==sha(exe) and
            products.get(map_path.relative_to(ROOT).as_posix())==sha(map_path),
            "Exact executable/map absent from successful guarded build")
    invocation=read(build.parent/"invocation.json")
    for source,digest in invocation["before"].items():
        require(sha(local(source))==digest, f"Compiled source or build input drift {source}")
    options=invocation["command"]
    profile=next(local(arg.partition("=")[2]) for arg in options if arg.startswith("/p:ForceImportBeforeCppTargets="))
    require(sha(profile)==invocation["profile"],"Imported build profile changed")
    profiles=[profile]
    for node in ET.parse(profile).getroot().findall(".//{*}Import"):
        name=node.attrib["Project"].replace("$(MSBuildThisFileDirectory)",str(profile.parent)+os.sep)
        require("$(" not in name,"Unresolved MSBuild profile import")
        imported=local(profile.parent/name)
        profiles.append(imported)
    flags="\n".join(p.read_text(encoding="utf-8-sig") for p in profiles)
    for flag in ("GENGINE_PHASE28_QUALIFICATION","GENGINE_PHASE29_QUALIFICATION","GENGINE_JOLT_FINAL_AUDIT","JPH_EXTERNAL_PROFILE"):
        require(flag in flags,"Missing qualification build flag "+flag)
    map_text = map_path.read_text(errors="strict")
    base, sizes = pe_words(exe, map_text, "g_Phase27TargetSizes", 8)
    _, config = pe_words(exe, map_text, "g_Phase29CaptureConfiguration", 11)
    require(sizes == config[:8] == SIZES, f"Target disagrees with frozen arena size basis {sizes}, {config}")
    require(config[8] > 0 and config[9:] == [0, 0], f"Unexpected contact/conditional compilation profile {config}")
    arena = read(ADMISSION / "arena-proposal.json")
    for path, digest in arena["current_sources"].items():
        require(sha(local(path)) == digest, f"Arena source drift {path}")
    sites = list(arena["base_sites"].values())
    require(len(sites) == 19 and sum(s["aligned_max_bytes"] for s in sites) == 12526080 and
            arena["capacity_bytes"] == 33554432, "Arena bound changed")
    root = "external/jolt/Jolt/"
    listener_sources = ["GEngine/src/Physics/JoltParameterEditor.inl", "GEngine/include/GEngine/Physics/PhysicsJoltSettings.h",
                        root+"Core/LockFreeHashMap.inl", root+"Core/StaticArray.h", root+"Physics/Collision/ContactListener.h",
                        root+"Physics/Constraints/ContactConstraintManager.h", root+"Physics/Constraints/ContactConstraintManager.cpp",
                        root+"Physics/PhysicsSystem.cpp"]
    return {"exe": identity(exe), "linker_map": identity(map_path), "build_record": identity(build),
            "profile_closure":[identity(p) for p in profiles],"invocation":identity(build.parent/"invocation.json"),
            "target_sizes": sizes, "preferred_sizes_va": base, "contact_sample_bytes": config[8],
            "contact_capacity": 8192, "JPH_VALIDATE_ISLAND_BUILDER": False, "JPH_TRACK_SIMULATION_STATS": False,
            "arena_positive_sites": len(sites), "arena_sum_bytes": 12526080, "capacity_bytes": 33554432,
            "listener_sources": [identity(n) for n in listener_sources],
            "source_review": identity(PHASE / "resume-01/listener-bound.md")}


@contextmanager
def lock():
    import msvcrt
    local(RUNS).mkdir(parents=True, exist_ok=True)
    with (RUNS / "campaign.lock").open("a+b") as f:
        if not f.tell():
            f.write(b"0"); f.flush()
        f.seek(0); msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
        try:
            yield
        finally:
            f.seek(0); msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)


def action_values(case):
    packet = read(ADMISSION / "reference-packet.json")
    key = [b["key"] for b in packet["bodies"]]
    joints = {j["key"]: j for j in packet["joints"]}
    workload = next(c for c in read(ADMISSION / "workloads.json")["cases"] if c["id"] == case)
    for a in workload["actions"]:
        t = a["before_tick"]
        if "joint" in a:
            joint = joints[a["joint"]]
            for end in "AB":
                angular = [v*a["sign"] for v in a["angular_impulse_"+end+"_world"]]
                yield [t, key.index(joint["body_"+end.lower()]), 0, 0, 0, *angular]
        else:
            yield [t, key.index(a["body"]), *a["linear_impulse_world"], *a["angular_impulse_world"]]


def ledger():
    result = []
    for path in sorted(local(RUNS).glob("attempt-*/reservation.json")):
        record = read(path)
        finished = path.parent/"process.json"
        record["debit"] = read(finished)["debited_ticks"] if finished.exists() else record["tick_cap"]
        result.append((path.parent, record))
    return result


def prospective_budget(cutoff, future, record):
    """Apply only the three named first attempts at the new Native cutoff."""
    sequence = cutoff["sequence"]
    require(len(future) < len(sequence) and
            [r["slot"] for r in future] == [s["slot"] for s in sequence[:len(future)]] and
            all(r["tick_cap"] == s["max_ticks"] and not r["correction"] for r,s in zip(future,sequence)),
            "Native cutoff has an unexpected historical reservation or is exhausted")
    expected = sequence[len(future)]
    require(record["id"] == expected["slot"] and record["max_ticks"] == expected["max_ticks"],
            "Native cutoff only admits the next named first attempt")
    require(all(type(r["debit"]) is int and 0 <= r["debit"] <= r["tick_cap"] for r in future),
            "Invalid prior fixture debit")
    native_ticks = sum(r["debit"] for r in future) + record["max_ticks"]
    total = cutoff["booked_before_new_reservations"] + native_ticks
    require(native_ticks <= cutoff["post_cutoff_limits"]["fixture_ticks_total"] and
            total <= cutoff["aggregate_tick_ceiling"], "Native or aggregate tick ceiling exceeded")
    return {"post_cutoff_Editor_starts":0, "post_cutoff_fixture_count_including_slot":len(future)+1,
            "native_tick_debit_including_reservation":native_ticks,"aggregate_tick_debit_including_reservation":total,
            "reserved_ticks":record["max_ticks"],"prior_11_start_6_editor_compliance":"UNKNOWN"}


def check_prospective_entry(previous, record):
    require(sha(NATIVE_FIRST) == "2b8e018020c5d2f8987068cc0aef688ea6a63658e50a07e3c062b984081d51c4",
            "Native-first Owner decision changed")
    decision = read(NATIVE_FIRST)
    manual = read(MANUAL_HEADROOM)
    require(manual["decision_sha256"] == sha(NATIVE_FIRST) and manual["cutoff_utc"] == decision["cutoff_utc"],
            "Manual process reconciliation belongs to a different decision or cutoff")
    require(sha(NATIVE_CUTOFF) == "f0ddbe30d5ca3296cdfe6087a44b5bcd530f33152a209241886940b3003752e5",
            "Three-fixture Owner cutoff is missing or changed")
    cutoff = read(NATIVE_CUTOFF)
    require(sha(ROOT / "RigidBodySimulation/GEngine.log") == cutoff["project_log_sha256"],
            "Ordinary project log changed; reconcile the current epoch before reservation")
    history = PHASE / "runtime-performance-02/launch-history.json"
    performance = PHASE / "runtime-performance-03"
    require(sha(history) == "537a43e0a28580f423320c03eedfdbbee6be64c5dd96d8771bf2e348de5dc113" and
            sha(performance / "decision.json") == "b000fed5769a0546c28de95c6aac7510a736b1e41fc39993e9d40caf8c1a2c39" and
            sorted(p.parent.name for p in performance.glob("runs/*/reservation.json")) == ["A1"] and
            sha(performance / "runs/A1/reservation.json") == "3f15ea78e857409cde5cde96fc1fd5d68ab60bbc4d68086be7de8581f79e2fbe" and
            sha(performance / "runs/A1/result.json") == "815185ee5c9d52c56a009a45b6138fe127d041f68409e6ee54c981079b880a9d",
            "Performance or historical reservations changed; reconcile first")
    historical = read(history)["automated"]
    require(len(previous) >= len(historical), "Missing historical fixture reservation")
    for (path, _), receipt in zip(previous, historical):
        require(sha(path / "reservation.json") == receipt["reservation_sha256"] and
                sha(path / "process.json") == receipt["process_sha256"], "Historical fixture ledger changed")
    require(sha(previous[1][0] / "assessment.json") ==
            "09569eabdfd944720c5874b3af974d3fb745c20ed795c7089f968234a60147e5",
            "Historical passing Jolt Debug assessment changed")
    require(all((path / "process.json").is_file() and (path / "assessment.json").is_file()
                for path, _ in previous), "Prior fixture has outstanding process or assessment evidence")
    for path, row in previous[len(historical):]:
        require(row["utc"] >= cutoff["cutoff_utc"] and row["prospective_budget"]["owner"] == identity(NATIVE_CUTOFF),
                "A fixture reservation is outside the three-slot cutoff")
        if row["debit"] != row["tick_cap"]:
            process = read(path / "process.json")
            require(row["category"] == "Debug" and row["debit"] == 0 and process["exit"] == 0
                    and not process["timeout"] and (path / "data/result.txt").read_text().splitlines()[:2]
                    == ["CAPTURE_COMPLETE","0"], "Reduced fixture debit lacks its completed-step witness")
    budget = prospective_budget(cutoff, [r for _, r in previous[len(historical):]], record)
    budget.update(owner=identity(NATIVE_CUTOFF), sequence_decision=identity(NATIVE_FIRST), manual=identity(MANUAL_HEADROOM),
                  cutoff=cutoff["cutoff_utc"], project_log_sha256=cutoff["project_log_sha256"])
    return budget


def native_release_prerequisites(previous, slot, inputs):
    debug = next(((p,r) for p,r in reversed(previous) if r["slot"] == "D-N"), None)
    require(debug is not None, "Native Debug lifetime result is missing")
    path, record = debug
    result = read(path / "assessment.json")
    process = read(path / "process.json")
    require(record["inputs"] == inputs and result["slot"] == "D-N" and result["status"] == "PASS"
            and result["checks"].get("typed_rejection_rollback_stale_members_reset") is True
            and result["checks"].get("effective_native_backend") is True
            and process["exit"] == 0 and not process["timeout"] and process["inputs_match"]
            and (path / "data/result.txt").read_text().splitlines()[:2] == ["CAPTURE_COMPLETE", "0"],
            "Current Native Debug lifetime evidence did not pass")
    if slot == ORDER[5]:
        drop = next(((p,r) for p,r in reversed(previous) if r["slot"] == ORDER[4]), None)
        require(drop is not None, "Evaluate Native DROP before TILT")
        path, record = drop
        result = read(path / "assessment.json")
        require(record["inputs"] == inputs and result["slot"] == ORDER[4], "Native DROP input or slot mismatch")
        if result["status"] != "PASS":
            review_path = RESUMPTION / "native-drop-impact.json"
            review = read(review_path)
            require(result["status"] in ("FAIL", "INCOMPLETE") and
                    review["reservation"] == identity(path / "reservation.json") and
                    review["process"] == identity(path / "process.json") and
                    review["assessment"] == identity(path / "assessment.json") and
                    review["shared_capture_prerequisites_valid"] is True and
                    review["rationale"].strip() and review["evidence"] and
                    all(identity(row["path"]) == row for row in review["evidence"]),
                    "Review the completed DROP evidence and shared capture prerequisites before Native TILT")
            return identity(review_path)
    return None


def run(args):
    exe, map_path, build, assets, cwd = map(local, (args.exe,args.map,args.build,args.assets,args.cwd))
    require(cwd.is_relative_to(ROOT/"runtime/RigidBodySimulation") and assets.is_dir(),
            "Isolated runtime or assets directory missing")
    budget = read(ADMISSION/"execution-plan.json")
    record = next(p for p in budget["planned"] if p["id"] == args.slot)
    with lock():
        previous = ledger()
        current_budget = check_prospective_entry(previous, record)
        sealed = compiled(exe,map_path,build)
        inputs = source_inputs()
        independence = None
        if args.slot in ORDER[4:]:
            independence = native_release_prerequisites(previous, args.slot, inputs)
        prior = [p for p,r in previous if r["slot"]==args.slot]
        if prior:
            require(args.correction and sum(bool(r.get("correction")) for _,r in previous)<2,
                    "Replacement requires a bounded correction record and a remaining reserve")
            correction=identity(args.correction)
            require(all(read(p/"reservation.json")["inputs"]!=inputs or
                    read(p/"reservation.json")["binary"]!=sealed["exe"] for p in prior),
                    "Unchanged retry")
        else:
            require(args.correction is None,"Correction must replace an actual failed attempt")
            correction=None
        # The pinned Owner decision releases D-N before Jolt completes.
        for necessary in ([ORDER[0]] if args.slot == "D-N" else PREREQUISITES[args.slot]):
            require(any(r["slot"]==necessary and (p/"assessment.json").exists() and
                        read(p/"assessment.json")["status"]=="PASS" for p,r in previous),
                    f"Prerequisite has no passing assessment {necessary}")
        require(len(previous)<10 and sum(r["debit"] for _,r in previous)+record["max_ticks"]<=8400 and
                sum(r["category"]==record["category"] for _,r in previous)<(4 if record["category"]=="Debug" else 6),
                "Cumulative run or tick limit")
        package = ROOT / "bin/phase29-qualification" / record["category"]
        require(exe == package / "RigidBodySimulation/RigidBodySimulation.exe" and assets == package / "assets"
                and (assets / "startup/RigidBodySimulation.txt").is_file(), "Wrong fixture configuration or physical package")
        census = subprocess.run(["powershell.exe","-NoProfile","-NonInteractive","-Command",
            "[Diagnostics.Process]::GetProcessesByName('RigidBodySimulation').Length"],
            capture_output=True,timeout=15,check=True,creationflags=subprocess.CREATE_NO_WINDOW)
        require(not census.stderr.strip() and census.stdout.strip()==b"0", "Active RBS process or unavailable census")
        cwd.mkdir(parents=True,exist_ok=True)
        dest=local(RUNS/f"attempt-{len(previous)+1:02d}-{args.slot}")
        dest.mkdir(exist_ok=False); data=dest/"data";data.mkdir()
        case=record.get("case","LIFETIME")
        if case!="LIFETIME":
            (dest/"actions.txt").write_text("".join(" ".join(map(str,row))+"\n" for row in action_values(case)),encoding="ascii")
        cmd=[str(exe),"--scene","passive-ragdoll","--backend",record["backend"].lower()]
        reservation={"slot":args.slot,"case":case,"category":record["category"],"backend":record["backend"],
                     "tick_cap":record["max_ticks"],"utc":utc(),"binary":sealed["exe"],"compiled":sealed,
                     "inputs":inputs,"command":cmd,"cwd":str(cwd),"assets":str(assets),
                     "build":identity(build),"runtime_dlls":[identity(p) for p in sorted(exe.parent.glob("*.dll"))],
                     "script":identity(dest/"actions.txt") if case!="LIFETIME" else None,"correction":correction,
                     "prospective_budget":current_budget,"independence":independence}
        write(dest/"reservation.json",reservation)
        env={k:v for k,v in os.environ.items() if not k.upper().startswith("GENGINE_")}
        env.update(GENGINE_ASSET_ROOT=str(assets),GENGINE_PHASE29_CASE=case,GENGINE_PHASE29_OUTPUT=str(data),
                   TMP=str(local("runtime/tmp")),TEMP=str(local("runtime/tmp")))
        status={"exit":None,"timeout":False,"debited_ticks":record["max_ticks"]}
        try:
            with (dest/"stdout.log").open("xb") as out:
                res=subprocess.run(cmd,cwd=cwd,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=180,check=False)
            status["exit"]=res.returncode
        except subprocess.TimeoutExpired:
            status["timeout"]=True
        finally:
            result=data/"result.txt"
            if status["exit"]==0 and result.is_file():
                lines=result.read_text().splitlines()
                expected_ticks = 0 if case=="LIFETIME" else 1200
                if lines[:2]==["CAPTURE_COMPLETE",str(expected_ticks)]:
                    status["debited_ticks"]=expected_ticks
            status["inputs_match"]=inputs==source_inputs() and sha(exe)==sealed["exe"]["sha256"]
            status["ordinary_epoch_unchanged"]=(sha(ROOT/"RigidBodySimulation/GEngine.log")==current_budget["project_log_sha256"]
                                                and identity(MANUAL_HEADROOM)==current_budget["manual"])
            write(dest/"process.json",status)
        print(dest.relative_to(ROOT))
        return assess(dest)


def rows(data,name):
    with (data/name).open(newline="",encoding="utf-8") as f:
        reader=csv.DictReader(f)
        result=list(reader)
        require(reader.fieldnames and len(set(reader.fieldnames))==len(reader.fieldnames) and
                all(None not in r and None not in r.values() for r in result),"malformed "+name)
        return result


def vec(r, names):
    values=tuple(float(r[n]) for n in names.split())
    require(all(math.isfinite(v) for v in values),"nonfinite vector")
    return values


def dot(a,b):
    return sum(x*y for x,y in zip(a,b))


def add(a,b):
    return tuple(x+y for x,y in zip(a,b))


def sub(a,b):
    return tuple(x-y for x,y in zip(a,b))


def scale(a,s):
    return tuple(x*s for x in a)


def cross(a,b):
    return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])


def unit(a):
    return scale(a,1/math.sqrt(dot(a,a)))


def conj(q):
    return (q[0],-q[1],-q[2],-q[3])


def mul(q,p):
    return (q[0]*p[0]-dot(q[1:],p[1:]),*add(cross(q[1:],p[1:]),add(scale(q[1:],p[0]),scale(p[1:],q[0]))))


def rot(q,v):
    return mul(mul(q,(0,*v)),conj(q))[1:]


def arc(q,p):
    return 2*math.acos(min(1,abs(dot(unit(q),unit(p)))))


def axes(q):
    return tuple(rot(unit(q),v) for v in ((1,0,0),(0,1,0),(0,0,1)))


def overlap(a,b):
    delta=sub(b["pos"],a["pos"])
    axes_a,axes_b=a["axes"],b["axes"]
    best=math.inf
    for candidate in (*axes_a,*axes_b,*(cross(x,y) for x in axes_a for y in axes_b)):
        length=math.sqrt(dot(candidate,candidate))
        if length<1e-10:
            continue
        axis=scale(candidate,1/length)
        extent=sum(h*abs(dot(n,axis)) for obj in (a,b) for h,n in zip(obj["half"],obj["axes"]))
        best=min(best,extent-abs(dot(delta,axis)))
    return max(0.,best)


def matrix(r):
    return tuple(vec(r," ".join(f"i{i}{j}" for j in range(3))) for i in range(3))


def transform(m,v):
    return tuple(dot(row,v) for row in m)


def body_state(r):
    q=vec(r,"qw qx qy qz")
    im,=vec(r,"im")
    require(r["sleep"] in ("0","1"),"invalid provider sleep flag")
    return {"pos":vec(r,"x y z"),"q":q,"axes":axes(q),"half":vec(r,"hx hy hz"),
            "v":vec(r,"vx vy vz"),"w":vec(r,"wx wy wz"),"im":im,
            "render":vec(r,"rx ry rz"),"rq":vec(r,"rqw rqx rqy rqz"),"inverse":matrix(r),"sleep":int(r["sleep"]),
            "id":(int(r["id"]),int(r["gen"]),int(r["world"]))}


def native_possible_contacts(previous_bones, environment, retained, suppressed):
    """Enclose the Native SAP candidates, including ballistic contacts absent at Stage::End."""
    def swept(body, dynamic):
        radius = math.sqrt(dot(body["half"], body["half"]))
        bounds = []
        for axis, start in enumerate(body["pos"]):
            extent = sum(h * abs(n[axis]) for h, n in zip(body["half"], body["axes"]))
            if dynamic and any(body["w"]):
                extent = max(extent, radius)
            speed = body["v"][axis] - (10 / 60 if dynamic and axis == 1 else 0)
            end = start + speed / 60
            # SAP adds 10 mm on each box. Widen outwards for the float pose, transformed corners and displacement.
            margin = .01 + 32 * 2**-23 * max(1., abs(start), abs(end), extent)
            bounds.append((min(start,end)-extent-margin, max(start,end)+extent+margin))
        return bounds
    bounds = [swept(body,True) for body in previous_bones] + [swept(body,False) for body in environment]
    candidates = set(retained)
    for i,moving in enumerate(bounds[:6]):
        for j,other in enumerate(bounds[i+1:],i+1):
            if (i,j) not in suppressed and all(a[0] <= b[1] and b[0] <= a[1] for a,b in zip(moving,other)):
                candidates.add((i,j))
    return candidates


def native_floor_impulse(bone, before, after, joints_before, joints_after, mass):
    # With one Native solve pass, each incident joint applies its prior warm-start
    # point impulse then the change to its current impulse. Angular rows have no linear term.
    current = previous_abs = current_abs = 0.
    for old,new in zip(joints_before,joints_after):
        if bone not in (int(new["a"]),int(new["b"])):
            continue
        current += (-1 if bone==int(new["a"]) else 1) * float(new["iy"])
        previous_abs += abs(float(old["iy"]))
        current_abs += abs(float(new["iy"]))
    impulse = mass * (after["v"][1]-before["v"][1]+10/60) - current
    # Six bodies, five joints, at most eleven float velocity additions including gravity.
    # Bound rounding of the point impulses, gravity and float velocity increments.
    uncertainty = 64 * 2**-23 * max(1., mass*(abs(before["v"][1])+10/60)+2*previous_abs+current_abs,
                                    mass*abs(after["v"][1]))
    return impulse, uncertainty


def statistics_for(values):
    ordered=sorted(values);n=len(ordered)
    return dict(mean=statistics.fmean(ordered),median=statistics.median(ordered),
                p95=ordered[math.ceil(.95*n)-1],p99=ordered[math.ceil(.99*n)-1],maximum=ordered[-1],
                max_sliding60=max(statistics.fmean(values[i:i+60]) for i in range(n-59)))


def work_schedule(data,case,reference):
    raw=rows(data,"actions.csv")
    actions=list(action_values(case))
    expected={(r[0],r[1]):r for r in actions}
    require(len(expected)==len(raw),"missing/extra action")
    work=defaultdict(float);nominal=defaultdict(float)
    for r in raw:
        t,i=int(r["tick"]),int(r["bone"]);a=expected.pop((t,i))
        vec(r,"x y z")
        pre_q=vec(r,"qw qx qy qz")
        require(abs(math.sqrt(dot(pre_q,pre_q))-1)<=1e-5,"impulse pose quaternion is not normalized")
        j,l=vec(r,"jx jy jz"),vec(r,"lx ly lz")
        require(math.dist(a[2:5],j)<1e-6 and math.dist(a[5:8],l)<1e-6,"wrong applied impulse")
        v,w=vec(r,"vx vy vz"),vec(r,"wx wy wz")
        inv_mass,=vec(r,"im")
        dv=scale(j,inv_mass);dw=transform(matrix(r),l)
        require(math.dist(add(v,dv),vec(r,"after_vx after_vy after_vz"))<2e-5 and
                math.dist(add(w,dw),vec(r,"after_wx after_wy after_wz"))<2e-5,"unreported impulse clamp")
        work[t]+=dot(j,v)+.5*dot(j,dv)+dot(l,w)+.5*dot(l,dw)
        body=reference["bodies"][i]["engine"]
        initial_q=body["orientation_wxyz"]
        if "TILT" in case:
            tilt=read(ADMISSION/"workloads.json")["cases"][1]["pose_transform"]["rotation_wxyz"]
            initial_q=mul(tilt,initial_q)
        local_l=rot(conj(unit(initial_q)),l)
        nominal[t]+=.5*(dot(j,j)/body["mass_kg"]+sum(a*a/I for a,I in zip(local_l,body["inertia_diagonal"])))
    require(not expected,"missing scheduled impulse")
    return work,nominal


def job_page_teardown(data, pointer, resolve):
    path=data/"teardown-heap.csv"
    if not path.is_file():
        return {"status":"MISSING","pointer":pointer}
    events=rows(data,path.name)
    require(events and events[-1]["kind"]=="complete" and
            int(events[-1]["event_count"])==len(events)-1,"incomplete or overflowing world teardown capture")
    free_site=None
    for event in events[:-1]:
        kind=event["kind"]
        require(kind in ("reserve","free") and not event["event_count"] and int(event["caller"])>0 and
                (int(event["requested"])>0 if kind=="reserve" else int(event["requested"])==0),
                "invalid teardown heap event")
        if int(event["pointer"])!=pointer:
            continue  # Other world resources include allocations made before Update observation.
        require(kind=="free" and free_site is None,"reused or multiply released job pool address at teardown")
        free_site=resolve(event["caller"])
        require(free_site.startswith(("??1JobSystemThreadPool@JPH@@","??1?$FixedSizeFreeList@VJob@JobSystem@JPH@@")),
                "job pool page freed outside its owning destructor")
    require(free_site is not None,"job pool page free missing from world teardown")
    return {"status":"MATCHED","pointer":pointer,"free_symbol":free_site,"event_count":len(events)-1}


def arena_audit(data, cost, reservation, meta):
    events=rows(data,"allocations.csv");by_tick=defaultdict(list);heap=defaultdict(list)
    for e in events:
        require(1<=int(e["tick"])<=1200 and e["allocator"] in ("arena","heap") and
                e["reserve"] in ("0","1") and all(int(e[k])>=0 for k in ("requested","pointer","caller")),
                "invalid allocation event")
        (by_tick if e["allocator"]=="arena" else heap)[int(e["tick"])].append(e)
    compiled_profile=reservation["compiled"]
    limit=compiled_profile["arena_positive_sites"]
    maximum=0
    for tick,items in by_tick.items():
        require(1<=tick<=1200 and reservation["backend"]=="Jolt","unexpected arena event")
        stack=[];used=0;positive=0
        for e in items:
            n=int(e["requested"]);a=int(e["aligned"]);p=int(e["pointer"])
            require(a==(n+15)//16*16,"arena alignment")
            if n:
                require(p!=0,"null positive allocation")
                if e["reserve"]=="1":
                    require(all(ptr!=p for ptr,_ in stack),"overlapping live allocation")
                    stack.append((p,n));used+=a;positive+=1
                else:
                    require(stack and stack.pop()==(p,n),"non-LIFO or mismatched arena release")
                    used-=a
            require(used==int(e["used"]) and 0<=used<=compiled_profile["arena_sum_bytes"],"bad arena occupancy")
            maximum=max(maximum,used)
        require(not stack and not used and positive<=limit,"unbalanced arena or too many sites")
    if reservation["backend"]=="Jolt":
        require(maximum>0,"no Jolt arena observations")
        calls,allocated,sample,capacity=map(int,(data/"listener-construction.txt").read_text().split())
        require(calls==1 and capacity==8192 and sample==compiled_profile["contact_sample_bytes"] and
                capacity*sample<=allocated<=capacity*sample+64,"unaccounted listener construction allocation")
    # Return addresses can only be attributed to the actual linked binary.
    map_path=local(compiled_profile["linker_map"]["path"])
    require(sha(map_path)==compiled_profile["linker_map"]["sha256"],"allocation linker-map identity mismatch")
    linkmap=map_path.read_text(errors="strict")
    symbols=sorted((int(va,16),symbol) for symbol,va in re.findall(
        r"^\s*[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+(\S+)\s+([0-9A-Fa-f]{12,16})\b",linkmap,re.M))
    from bisect import bisect_right
    addresses=[k for k,_ in symbols]
    delta=int(meta[3])-compiled_profile["preferred_sizes_va"]
    require(delta%65536==0,"invalid map relocation")
    def resolve(caller):
        i=bisect_right(addresses,int(caller)-delta)-1
        return symbols[i][1] if i>=0 else "unmapped"

    unseen=Counter();recorded=Counter();live={};job_page=None
    for tick,counters in enumerate(cost,1):
        calls=0;heap_bytes=0
        for e in heap[tick]:
            require(reservation["backend"]=="Jolt","unexpected Jolt heap event on Native")
            symbol=resolve(e["caller"])
            ptr=int(e["pointer"]);n=int(e["requested"]);reserve=e["reserve"]=="1"
            if reserve:
                require(ptr and ptr not in live and n>0 and symbol!="unmapped","unresolved Jolt heap allocation")
                if symbol.startswith("??$ConstructObject@") and "?$FixedSizeFreeList@VJob@JobSystem@JPH@@" in symbol:
                    require(tick==1 and n==229376 and job_page is None,"unexpected Jolt job pool page")
                    job_page=ptr
                live[ptr]=(symbol,n);calls+=1;heap_bytes+=n
            else:
                require(n==0 and ptr in live,"unmatched heap release")
                require(ptr!=job_page,"job pool page released before world teardown")
                create,size=live.pop(ptr);recorded[(create,symbol,size)]+=1
                if create!=symbol or not any(site in create for site in ("UpdatePrepare@QuadTree@","BuildTree@QuadTree@")):
                    unseen[(create,symbol,size)]+=1
        require(int(counters["tick"])==tick and int(counters["jph_calls"])==calls and int(counters["jph_bytes"])==heap_bytes,
                "heap callback and global JPH counters disagree")
        require(len(live)==(1 if job_page else 0),"heap allocation survived an Update outside the job pool")
    teardown=None
    if reservation["backend"]=="Jolt":
        require(job_page in live,"missing first-Update Jolt job pool page")
        teardown=job_page_teardown(data,job_page,resolve)
    return {"arena_peak_bytes":maximum,"heap_site_pairs":[[list(k),v] for k,v in sorted(recorded.items())],
            "job_pool_teardown":teardown,
            "unclassified_heap_site_pairs":[[list(k),v] for k,v in sorted(unseen.items())]}


def assess(destination):
    destination=local(destination);data=destination/"data";r=read(destination/"reservation.json")
    process=read(destination/"process.json");checks={};failures=[];incomplete=[];metrics={}
    def check(name,ok,details=None):
        if name not in checks:checks[name]=True
        if not ok:
            checks[name]=False
            if len(failures)<60:failures.append([name,details])
    try:
        require(process["exit"]==0 and not process["timeout"] and process["inputs_match"],"process or input integrity failure")
        require(process.get("ordinary_epoch_unchanged",True),"Ordinary run evidence changed during qualification")
        meta=(data/"result.txt").read_text().splitlines();require(meta[0]=="CAPTURE_COMPLETE",str(meta[:3]))
        require([int(i) for i in meta[4].split()]==SIZES,"runtime ABI identity differs")
        if r["category"]=="Debug":
            require(int(meta[1])==0,"unexpected lifetime ticks")
            if r["slot"]=="D-J":require((data/"ordinary-viewport.png").is_file(),"missing ordinary viewport capture")
            if r["slot"]=="D-N":
                backend=re.findall(r"Physics backend: (\w+)\.",(destination/"stdout.log").read_text(errors="replace"))
                check("effective_native_backend",bool(backend) and set(backend)=={"Native"},backend)
            check("typed_rejection_rollback_stale_members_reset",True)
        else:
            require(int(meta[1])==1200,"incomplete episode")
            evaluate(data,r,meta,checks,check,incomplete,metrics)
    except (OSError,ValueError,KeyError,IndexError,ArithmeticError) as exc:
        checks["complete_evidence"]=False
        failures.append(["complete_evidence",str(exc)])
    outcome="FAIL" if any(not v for v in checks.values()) else "INCOMPLETE" if incomplete else "PASS"
    result={"slot":r["slot"],"status":outcome,"checks":checks,"failures":failures,
            "incomplete":incomplete,"metrics":metrics,"F_OWNER":"PENDING"}
    write(destination/"assessment.json",result)
    print(outcome, json.dumps(failures[:5]))
    return outcome


def evaluate(data,r,meta,checks,check,incomplete,metrics):
    case=r["case"];reference=read(ADMISSION/"reference-packet.json");criteria=read(ADMISSION/"criteria.json")
    names=[b["key"] for b in reference["bodies"]]
    env=reference["isolated_environment"]
    pairs=[tuple(sorted((names.index(j["body_a"]),names.index(j["body_b"])))) for j in reference["joints"]]
    active_pairs=sorted(set(itertools.combinations(range(6),2))-set(pairs))+[(i,6) for i in range(6)]
    require(len(active_pairs)==16,"incomplete contact inventory")
    work,nominal=work_schedule(data,case,reference)
    action_ticks=set(work)
    all_bodies=rows(data,"bodies.csv");all_joints=rows(data,"joints.csv");cost=rows(data,"cost.csv")
    require(len(all_bodies)==1201*11 and len(all_joints)==1201*5 and len(cost)==1200,"incomplete full-tick capture")
    raw_contacts=defaultdict(list)
    for row in rows(data,"contacts.csv"):
        t,a,b=(int(row[k]) for k in ("tick","a","b"))
        require(1<=t<=1200 and 0<=a<11 and 0<=b<11 and a!=b,"invalid actual contact ID")
        pair=tuple(sorted((a,b)))
        fields="nx ny nz depth normal_before normal_after"
        if r["backend"]=="Jolt":
            vec(row,fields)
        else:
            require(all(row[k]=="" for k in fields.split()),"unexpected Native contact channel")
        check("physical_filter_and_no_wall",pair in active_pairs,[t,pair])
        raw_contacts[t].append((pair,row))
    lengths=[j["characteristic_length_m"] for j in reference["joints"]]
    energy=[];corrected=[];cumulative_work=0;credit=0;max_excess=-math.inf;max_pen=0
    wake=[];series=[];joint_series=[];angles=[[] for _ in range(7)]
    windows=[range(901,961),range(1081,1201)]
    episodes={};contacts_with_response=[];depth_series=defaultdict(list);momentum_prev=None;ids=None
    ambiguous_native_contacts=[];native_impulses={}
    initial_depths={}
    tilt=read(ADMISSION/"workloads.json")["cases"][1]["pose_transform"]
    for t in range(1201):
        group=all_bodies[t*11:(t+1)*11];jgroup=all_joints[t*5:(t+1)*5]
        require(all(int(v["tick"])==t and int(v["bone"])==i for i,v in enumerate(group)) and
                all(int(v["tick"])==t and int(v["joint"])==i for i,v in enumerate(jgroup)),"tick/member order")
        state=[body_state(row) for row in group];series.append(state[:6]);wake.append([v["sleep"] for v in state[:6]])
        current_ids=[v["id"] for v in state]
        if ids is None:ids=current_ids
        require(current_ids==ids and len(set(ids))==11 and len({x[2] for x in ids})==1,"unstable identity mapping")
        E=0;momentum=(0,0,0)
        for i,s in enumerate(state):
            item=reference["bodies"][i]["engine"] if i<6 else env[i-6]
            initial_pos=item["position"];initial_q=item["orientation_wxyz"]
            if i<6 and "TILT" in case:
                initial_pos=add(add(tilt["pivot"],rot(tilt["rotation_wxyz"],sub(initial_pos,tilt["pivot"]))),tilt["translation"])
                initial_q=mul(tilt["rotation_wxyz"],initial_q)
            check("actual_shape",math.dist(s["half"],item["half_extents"])<=1e-6,[t,i,s["half"]])
            check("normalized_orientation",abs(math.sqrt(dot(s["q"],s["q"]))-1)<=1e-5,[t,i])
            check("body_render_translation",max(map(abs,sub(s["pos"],s["render"])))<=1e-5,[t,i])
            check("body_render_quaternion",min(max(map(abs,sub(s["q"],s["rq"]))),max(map(abs,add(s["q"],s["rq"]))))<=1e-5,[t,i])
            if t==0 or i>=6:
                check("reference_pose",math.dist(s["pos"],initial_pos)<=1e-6 and
                      min(max(map(abs,sub(s["q"],initial_q))),max(map(abs,add(s["q"],initial_q))))<=1e-6,[t,i,s["pos"]])
            if t==0:check("initial_velocities",max(map(abs,s["v"]+s["w"]))<=1e-6,[i])
            if i<6:
                m=item["mass_kg"];check("effective_mass",abs(s["im"]-1/m)<=1e-6,[t,i,s["im"]])
                local_w=rot(conj(unit(s["q"])),s["w"])
                E+=.5*m*dot(s["v"],s["v"])+.5*sum(I*w*w for I,w in zip(item["inertia_diagonal"],local_w))+m*10*s["pos"][1]
                # Confirm the measured inertia agrees with the independently rotated reference tensor.
                for axis,I in zip(s["axes"],item["inertia_diagonal"]):
                    check("effective_inertia",math.dist(transform(s["inverse"],axis),scale(axis,1/I))<=1e-4,[t,i])
                momentum=add(momentum,scale(s["v"],m))
                x,y,z=s["pos"];check("central_domain",math.hypot(x,z)<=10 and y<15,[t,i])
        energy.append(E);cumulative_work+=work[t];credit+=nominal[t]
        excess=E-energy[0]-cumulative_work;corrected.append(excess);max_excess=max(max_excess,excess)
        check("energy_work",excess<=max(.000625,.1025*credit),[t,excess,credit])
        quiet=any(t in w for w in windows);quiet_or_rest=quiet
        # Use the frozen instantaneous quiet predicate after 30 consecutive samples.
        if t>=30:
            quiet_or_rest|=all(all(b["sleep"] or (math.sqrt(dot(b["v"],b["v"]))<=.05 and
                math.sqrt(dot(b["w"],b["w"]))<=.02) for b in prev) for prev in series[t-29:t+1])
        angular_limit=.005 if quiet_or_rest else .010
        errors=[];axial=[]
        for i,row in enumerate(jgroup):
            j=reference["joints"][i];p=j["engine"];a=names.index(j["body_a"]);b=names.index(j["body_b"])
            require(int(row["a"])==a and int(row["b"])==b and int(row["collide"])==0 and
                    int(row["kind"])==(2 if j["kind"]=="Hinge" else 4),"joint mapping or effective filtering")
            wa=add(state[a]["pos"],rot(unit(state[a]["q"]),p["local_anchor_a"]))
            wb=add(state[b]["pos"],rot(unit(state[b]["q"]),p["local_anchor_b"]))
            check("joint_frame_readback",math.dist(wa,vec(row,"ax ay az"))<=1e-5 and math.dist(wb,vec(row,"bx by bz"))<=1e-5,[t,i])
            qa=mul(state[a]["q"],p["local_frame_a_wxyz"]);qb=mul(state[b]["q"],p["local_frame_b_wxyz"])
            q=unit(mul(conj(qa),qb))
            if next(x for x in q if x!=0)<0:q=scale(q,-1)
            asin=lambda v: math.asin(max(-1,min(1,v)))
            signed=math.remainder(2*math.atan2(q[1],q[0]),2*math.pi);u=2*asin(q[2]);v=2*asin(q[3])
            hinge=j["kind"]=="Hinge"
            axis_error=2*asin(math.hypot(q[2],q[3]));cv_error=2*asin(abs(q[1]))
            residual=axis_error if hinge else cv_error
            actual=[signed] if hinge else [u,v]
            vu=max(0,p["lower_radians"]-signed,signed-p["upper_radians"]) if hinge else max(0,abs(u)-p["half_angle_u_radians"])
            vv=0 if hinge else max(0,abs(v)-p["half_angle_v_radians"])
            violation=max(vu,vv)
            vec(row,"ix iy iz")
            observed=vec(row,"axis cv angle u v vu vv")
            independent=(axis_error,cv_error,signed,u,v,vu,vv)
            check("angular_provider_parity",all(abs(x-y)<=1e-5 for x,y in zip(observed,independent)),
                  [t,i,observed,independent])
            check("angular_residual_limit",max(residual,violation)<=angular_limit,[t,i,residual,violation])
            axial.extend(actual)
            error=math.dist(wa,wb);errors.append(error)
            check("individual_anchor_bound",error<=angular_limit*lengths[i],[t,i,error])
        check("summed_anchor_bound",sum(errors)<=angular_limit*sum(lengths),[t,errors])
        joint_series.append(errors)
        for s,value in zip(angles,axial):s.append(value)
        if t==0:
            initial_depths={p:overlap(state[p[0]],state[p[1]]) for p in active_pairs+pairs}
            momentum_prev=momentum;continue
        d_by_pair={}
        for pair in active_pairs+pairs:
            d=overlap(state[pair[0]],state[pair[1]])
            depth_series[pair].append(d)
            if pair in pairs:continue
            d_by_pair[pair]=d
            max_pen=max(max_pen,d);check("maximum_penetration",d<=(.005 if quiet_or_rest else .010),[t,pair,d])
            if d>.005 and pair not in episodes:episodes[pair]=[t,0]
            if pair in episodes:
                start,confirmed=episodes[pair]
                confirmed=confirmed+1 if d<=.005 else 0
                check("penetration_recovery",t-start<4 or confirmed>0,[t,pair,start,d])
                if confirmed==3:episodes.pop(pair)
                else:episodes[pair][1]=confirmed
        # Isolate the whole-assembly vertical momentum balance. All members were
        # awake at both boundaries and no wall or commanded impulse participated.
        # Multiple simultaneous floor callbacks establish only a set of candidates.
        residual_y=momentum[1]-momentum_prev[1]+sum(b["engine"]["mass_kg"] for b in reference["bodies"])*10/60
        floor_rows=[(pair,c) for pair,c in raw_contacts[t] if 6 in pair]
        floor_pairs=sorted({pair for pair,c in floor_rows})
        if (t not in action_ticks and not any(wake[t-1]) and not any(wake[t]) and residual_y>0 and floor_pairs
                and all(max(p)<=6 for p,_ in raw_contacts[t]) and all(d_by_pair.get(p,0)>0 for p in floor_pairs)):
            if r["backend"]=="Native":
                retained=[p for step in (t-1,t) for p,_ in raw_contacts[step]]
                possible=native_possible_contacts(series[t-1],state[6:],retained,pairs)
                confirmed=False
                for pair in floor_pairs:
                    i=pair[0]
                    if any(i in other and pair!=other for other in possible):
                        continue
                    impulse,error=native_floor_impulse(i,series[t-1][i],state[i],all_joints[(t-1)*5:t*5],jgroup,
                                                       reference["bodies"][i]["engine"]["mass_kg"])
                    if impulse>error:
                        native_impulses[t,pair]={"normal_impulse_Ns":impulse,"roundoff_bound_Ns":error}
                        contacts_with_response.append((t,[pair],residual_y))
                        confirmed=True
                if not confirmed:ambiguous_native_contacts.append((t,floor_pairs,residual_y))
            elif any(float(c["normal_after"])>float(c["normal_before"]) for _,c in floor_rows):
                contacts_with_response.append((t,floor_pairs,residual_y))
        momentum_prev=momentum
    check("closed_contact_episodes",not episodes,list(episodes.items()))
    def recovers_after(t,pair):
        depths=depth_series[pair]
        peak=depths[t-1]
        for index in range(t,len(depths)):
            d=depths[index]
            if d<peak and d<=.005:
                if peak<=.005 or (index+2<len(depths) and max(depths[index:index+3])<=.005):
                    return index+1
            peak=max(peak,d)
        return None
    recovered=[]
    for t,floor_pairs,impulse in contacts_with_response:
        later=[recovers_after(t,pair) for pair in floor_pairs]
        if all(s is not None for s in later):
            recovered.append({"tick":t,"actual_floor_pairs":floor_pairs,"overlap_at_tick_m":[depth_series[p][t-1] for p in floor_pairs],
                              "recovery_ticks":later,"whole_assembly_vertical_contact_impulse_Ns":impulse})
            if r["backend"]=="Native":
                recovered[-1]["isolated_bone_floor_impulse"]=native_impulses[t,floor_pairs[0]]
            break
    if not recovered and any(all(recovers_after(t,p) is not None for p in pairs)
                             for t,pairs,_ in ambiguous_native_contacts):
        incomplete.append("Native captured floor response requires attribution beyond measured joint impulses and swept contact candidates")
    else:
        check("actual_floor_response_and_recovery",bool(recovered))
    metrics["floor_witness"]=recovered[0] if recovered else None
    metrics["max_penetration_m"]=max_pen;metrics["peak_energy_excess_J"]=max_excess
    metrics["peak_raw_energy_J"]=max(energy)
    # Preserve raw all-pair SAT and aggregate work/energy for independent inspection.
    with (data.parent/"geometry-energy.csv").open("x",newline="") as f:
        out=csv.writer(f);out.writerow(["tick","energy","work_adjusted_excess",*[f"depth_{a}_{b}" for a,b in active_pairs+pairs]])
        out.writerow([0,energy[0],corrected[0],*(initial_depths[pair] for pair in active_pairs+pairs)])
        for t in range(1,1201):
            out.writerow([t,energy[t],corrected[t],*(depth_series[pair][t-1] for pair in active_pairs+pairs)])
    check("initial_sleep_by_900",all(wake[900]))
    check("connected_wake_961",not any(wake[961]))
    check("returned_sleep_1080",all(wake[1080]))
    for window in windows:
        check("settled_windows",all(all(wake[t]) for t in window))
        check("settled_energy",max(energy[t]-energy[window.start-1] for t in window)<=.000625)
        for i in range(6):
            for first in range(window.start,window.stop-59):
                chunk=[series[t][i] for t in range(first,first+60)]
                max_p=max(math.dist(a["pos"],b["pos"]) for a,b in itertools.combinations(chunk,2))
                max_q=max(arc(a["q"],b["q"]) for a,b in itertools.combinations(chunk,2))
                check("settled_pose_jitter",max_p<=.0005 and max_q<=.002,[first,i,max_p,max_q])
            for t in range(window.start+1,window.stop-1):
                a,m,b=[series[k][i] for k in (t-1,t,t+1)]
                q2=scale(b["q"],-1) if dot(a["q"],b["q"])<0 else b["q"]
                check("settled_high_frequency",math.dist(m["pos"],scale(add(a["pos"],b["pos"]),.5))<=.00025 and
                      arc(m["q"],unit(add(a["q"],q2)))<=.001,[t,i])
    for t in range(1,1200):
        for i,L in enumerate(lengths):
            h=abs(joint_series[t][i]-(joint_series[t-1][i]+joint_series[t+1][i])*.5)
            check("loaded_joint_high_frequency",h<=.001*L,[t,i,h])
    if "TILT" in case:
        for i,s in enumerate(angles):
            for sign in (-1,1):
                visits=[t for t in range(1,1201) if abs(sign*s[t]-math.pi/4)<=.05]
                ok=bool(visits) and any(sign*x<=math.pi/4-.10 for x in s[visits[0]+1:])
                check("dynamic_bound_visit_and_exit",ok,[i,sign,visits[:1]])
    def slope(first):
        y=[statistics.fmean(corrected[t:t+60]) for t in range(first,1201,60)]
        x=[i+.5 for i in range(len(y))];mx=statistics.fmean(x);my=statistics.fmean(y)
        return sum((a-mx)*(b-my) for a,b in zip(x,y))/sum((a-mx)**2 for a in x)
    metrics["post_420_slope_J_per_s"]=slope(421);metrics["full_slope_J_per_s"]=slope(1)
    check("energy_trend",slope(421)<=.000625/12)
    require(all(int(row["tick"])==t and 0<int(row["q"])<=1000 and all(int(value)>=0 for value in row.values())
                for t,row in enumerate(cost,1)),"invalid timing or allocation sample")
    q=max(float(row["q"]) for row in cost);timing={}
    for field in ["inclusive_ns","observer_upper_ns","joint_ns","contact_ns","adapter_pre_ns","sdk_ns","adapter_post_ns",
                  *[f"stage{i}_ns" for i in range(8)]]:
        values=[int(row[field]) for row in cost];require(min(values)>=0,"negative elapsed time")
        timing[field]=statistics_for(values)
    metrics["cost_ns"]=timing
    U=timing["inclusive_ns"];O=timing["observer_upper_ns"];dt=1e9/60
    check("inclusive_cpu",max(U["mean"],U["median"])+q<=.5*dt and max(U["p95"],U["max_sliding60"])+q<=.75*dt)
    if max(O["mean"],O["median"])+2*q>dt/100 or max(O["p95"],O["max_sliding60"])+2*q>dt/50:
        incomplete.append("Measured same-tick conservative upper bound exceeds observer-cost allowance")
    metrics["p95_goal_only_ns"]=dt*.1
    alloc=arena_audit(data,cost,r,meta);metrics["allocation"]=alloc
    if r["backend"]=="Jolt":
        check("job_pool_shutdown_free",alloc["job_pool_teardown"]["status"]=="MATCHED",alloc["job_pool_teardown"])
    if alloc["unclassified_heap_site_pairs"]:incomplete.append("Unattributed JPH allocator events")
    check("joint_allocation",all(int(v[k])==0 for v in cost for k in ("joint_calls","joint_bytes","graph_calls","graph_bytes")))
    if r["backend"]=="Jolt":
        check("cpp_heap_cost",all(int(v["cpp_calls"])==int(v["cpp_bytes"])==0 for v in cost))
    else:
        stages=rows(data,"native-stages.csv")
        require(len(stages)==12*1200 and all(int(v)>=0 for row in stages for v in row.values()),
                "missing or invalid Native allocation-stage observations")
        unknown=[]
        for t in range(1200):
            s=stages[12*t:12*t+12]
            require(all(int(v["tick"])==t+1 and int(v["stage"])==i for i,v in enumerate(s)),"Native stage order")
            # Attribute intervals from Update-entry zero through Stage::Begin and
            # beyond Stage::End to the same whole-Update C++ counters.
            samples=[(0,0), *[(int(row["calls"]),int(row["bytes"])) for row in s],
                     (int(cost[t]["cpp_calls"]),int(cost[t]["cpp_bytes"]))]
            for i,(before,after) in enumerate(zip(samples,samples[1:])):
                calls,size=sub(after,before)
                require(calls>=0 and size>=0,"Native allocation counters decrease within Update")
                if calls or size:unknown.append([t+1,i,calls,size])
        if unknown:
            with (data.parent/"native-allocation-deltas.json").open("x") as f:json.dump(unknown,f)
            incomplete.append("Native allocation deltas require per-site causal assignment")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest="operation",required=True)
    for op in ("preflight","run"):
        command=commands.add_parser(op)
        for arg in ("exe","map","build"):command.add_argument("--"+arg,required=True)
        if op=="run":
            command.add_argument("--slot",choices=ORDER,required=True)
            command.add_argument("--assets",required=True);command.add_argument("--cwd",required=True)
            command.add_argument("--correction")
    command=commands.add_parser("assess");command.add_argument("directory")
    args=parser.parse_args()
    if args.operation=="preflight":
        print(json.dumps(compiled(local(args.exe),local(args.map),local(args.build)),indent=2))
    elif args.operation=="run":
        return int(run(args)!="PASS")
    else:
        return int(assess(args.directory)!="PASS")


if __name__=="__main__":
    raise SystemExit(main())
