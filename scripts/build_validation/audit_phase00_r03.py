"""Fresh Revision 03 resource, compiler and copied-output validation."""

import collections
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

from revise_phase00_r03 import ROOT, WORK, EVIDENCE, APPS, run, sha, write_json
from pe_runtime import resolve_closure


def compilers():
    import finalize_phase00 as prior
    entry = json.loads((ROOT / ".codex/build/phase-00-entry.json").read_text())
    result = {}
    for mode in ["normal", "profile", "editor-final"]:
        copied = WORK / ("compiler-" + mode)
        copied.mkdir(exist_ok=True)
        for config in ["Debug", "Release"]:
            source = WORK / (f"build-editor-{config}-final.txt" if mode == "editor-final" else f"build-{mode}-{config}.txt")
            destination = copied / f"build-{config}.txt"
            if not destination.exists():
                shutil.copyfile(source, destination)
            assert sha(source) == sha(destination)
        prior.EVIDENCE = copied
        audit = prior.audit_compilers(entry)
        for config, data in audit["configurations"].items():
            assert data["flag_gate"] == "PASS"
            assert data["warning_counts"]["first_party"] == data["warning_counts"]["linker"] == 0
            if mode == "normal":
                assert data["coverage_gate"] == "PASS"
                assert all("GE_ENABLE_PHYSICS_PROFILING" not in c["command"] for c in data["compiler_calls"])
            elif mode == "profile":
                for call in data["compiler_calls"]:
                    assert ("GE_ENABLE_PHYSICS_PROFILING" in call["command"]) == (call["target"] in ["GEngine", "PhysicsBenchmark"])
                for row in data["coverage"]:
                    if row["target"] in ["GEngine", "PhysicsBenchmark"]:
                        assert not row["missing"] and not row["unexpected"]
            else:
                assert len(data["compiler_calls"]) == 1
                assert data["compiler_calls"][0]["sources"] == ["GEngineEditor/src/SceneApp.cpp"]
                assert "GE_ENABLE_PHYSICS_PROFILING" not in data["compiler_calls"][0]["command"]
            data["canonical_capture"] = f"commands/" + (f"build-editor-{config}-final.json" if mode == "editor-final" else f"build-{mode}-{config}.json")
        result[mode] = audit
    write_json(EVIDENCE / "compiler-audit.json", result)
    print(json.dumps({m: {c: {"TUs": a["translation_units"], "warnings": a["warning_counts"]} for c, a in d["configurations"].items()} for m, d in result.items()}))


def resources():
    audits = []
    for config in ["Debug", "Release"]:
        for app in APPS:
            name = f"resources-{config}-{app}"
            if config == "Debug" and app == "Breakout":
                name += "-final"
            log = WORK / (name + "-debugger-output.txt")
            if not (EVIDENCE / "commands" / (name + ".json")).exists():
                audits.append({"configuration": config, "app": app, "result": "NOT RUN"})
                continue
            stacks = collections.defaultdict(list)
            opened, unmatched = [], []
            pending_prefix = ""
            for number, line in enumerate(log.read_text(errors="replace").splitlines(), 1):
                if pending_prefix:
                    line = pending_prefix + line
                    pending_prefix = ""
                if line.startswith(("RESOURCE_BEGIN tid=*** WARNING:", "RESOURCE_END tid=*** WARNING:")):
                    pending_prefix = line.split("*** WARNING:")[0]
                    continue
                start = re.search(r"RESOURCE_BEGIN tid=([0-9a-f]+) access=([0-9a-f]+) path=(.*)", line)
                end = re.search(r"RESOURCE_END tid=([0-9a-f]+) handle=([0-9a-f`]+)", line)
                if start:
                    tid, access, path = start.groups()
                    stacks[tid].append({"requested": path, "access": access, "log_line": number})
                elif end:
                    tid, handle = end.groups()
                    if not stacks[tid]:
                        unmatched.append(number)
                        continue
                    row = stacks[tid].pop()
                    row.update(handle=handle, returned_line=number, success=int(handle.replace("`", ""), 16) not in [0, 0xffffffffffffffff])
                    path = Path(row["requested"].removeprefix("\\\\?\\"))
                    path = (ROOT / app / path).resolve()
                    if path.is_relative_to(ROOT) and ("/include/" in path.as_posix().lower() or path.name == "imgui.ini"):
                        row.update(resolved=path.relative_to(ROOT).as_posix(), sha256=sha(path) if path.is_file() else "ABSENT")
                        opened.append(row)
            relevant_pending = [r for s in stacks.values() for r in s if "GEngine" in r["requested"] or r["requested"].startswith("..")]
            assert not relevant_pending, relevant_pending
            reads = [r for r in opened if int(r["access"], 16) & 0x80000000]
            failures = [r for r in opened if not r["success"] or r["sha256"] == "ABSENT"]
            assert reads, name
            assert not failures, (name, failures)
            assert any(r["resolved"].endswith(".ttf") for r in reads), name
            assert any(r["resolved"].endswith((".png", ".hdr", ".jpg")) for r in reads), name
            startup = WORK / f"startup-{config}-{app}.txt"
            objects = []
            for line in startup.read_text(errors="replace").splitlines():
                try:
                    objects.append(json.loads(line))
                except json.JSONDecodeError:
                    pass
            final = next(o for o in objects if "application_log" in o)
            startup_ok = final["exit_code"] == 0 and final["alive_for_smoke"] and not final["forced_termination"]
            messages = (final["application_log"] or {}).get("text", "")
            errors = [line for line in messages.splitlines() if re.search(r"Unable to load|not found|\[error\]|Framebuffer status error|failed to load", line, re.I)]
            assert not errors, (name, errors)
            audits.append({"configuration": config, "app": app, "cwd": str(ROOT / app), "result": "PASS" if startup_ok else "BLOCKED_STARTUP", "file_open_trace": str(log), "trace_sha256": sha(log), "resource_reads": reads, "other_repository_opens": opened, "unmatched_nonresource_return_lines": unmatched, "startup": final, "layout_guards": [o for o in objects if "protected_layout_after" in o]})
    write_json(EVIDENCE / "resource-audit.json", {"result": "PASS" if all(a["result"] == "PASS" for a in audits) else "BLOCKED", "method": "Debugger traces actual CreateFileA/W entry paths and one-shot return handles, paired per thread; interleaved symbol warnings are joined back to their interrupted event line. Required repository resource reads must exist and return valid handles. OS/driver compatibility probes in bin are retained in raw traces, not classified as required resources. Fresh ordinary source-CWD startup and logs independently check assertions/fallbacks and clean close. Release paths are traced despite compiled-out logging. Protected resource hashes are checked against entry in final integrity.", "audits": audits})


def safeguards():
    fixture = (WORK / "safeguard-copy").resolve()
    assert fixture.is_relative_to(WORK.resolve()) and not fixture.exists()
    fixture.mkdir()
    copied = []
    def copy(source, destination):
        destination.parent.mkdir(parents=True, exist_ok=True)
        assert not destination.exists()
        shutil.copyfile(source, destination)
        assert sha(source) == sha(destination)
        copied.append({"source": str(source), "destination": str(destination), "sha256": sha(source)})
    for name in ["pe_runtime.py", "stage_premake_runtime.py"]:
        copy(ROOT / "scripts/build_validation" / name, fixture / "scripts/build_validation" / name)
    copy(ROOT / "tools/postbuild.py", fixture / "tools/postbuild.py")
    for app in APPS:
        copy(ROOT / app / "postbuild.py", fixture / app / "postbuild.py")
        for config in ["Debug", "Release"]:
            for source in (ROOT / "bin" / config / app).iterdir():
                if source.suffix.lower() in [".exe", ".dll"] and source.name != "tbb12.dll":
                    copy(source, fixture / "bin" / config / app / source.name)
    import stage_premake_runtime as original
    for source in [original.ARCHIVE, original.SOURCE, ROOT / "external/tbb/lib/tbb12.lib"]:
        copy(source, fixture / source.relative_to(ROOT))
    records = []
    def command(test, argv, expected=0, signature=None):
        p = subprocess.run([str(a) for a in argv], cwd=fixture, capture_output=True, text=True)
        row = {"test": test, "argv": [str(a) for a in argv], "cwd": str(fixture), "exit_code": p.returncode, "stdout": p.stdout, "stderr": p.stderr}
        records.append(row)
        assert (p.returncode == 0) == (expected == 0), row
        if signature:
            assert signature in p.stdout + p.stderr, row
    for config in ["Debug", "Release"]:
        for app in APPS:
            command("original-postbuild", [sys.executable, fixture / app / "postbuild.py", "config=" + config, "prj=" + app])
    missing = fixture / "bin/Debug/Breakout/SDL2.dll"
    retained = missing.with_suffix(".dll.retained")
    assert missing.resolve().is_relative_to(fixture)
    missing.rename(retained)
    command("missing-SDL-guard", [sys.executable, fixture / "Breakout/postbuild.py", "config=Debug", "prj=Breakout"], 1, "Missing tracked runtime DLLs")
    retained.rename(missing)
    stager = fixture / "scripts/build_validation/stage_premake_runtime.py"
    command("missing-TBB-check", [sys.executable, stager, "check"], 1, "Missing or mismatched staged runtime")
    command("stage-absent-matching-TBB", [sys.executable, stager, "stage"], signature="created-previously-absent")
    command("idempotent-stage", [sys.executable, stager, "stage"], signature="verified-existing; no write")
    command("all-eight-copied-runtime-closures", [sys.executable, stager, "check"])
    dest = fixture / "bin/Debug/RayTracing/tbb12.dll"
    before = dest.read_bytes()
    dest.write_bytes(b"deliberately mismatched disposable output")
    command("refuse-mismatched-existing-output", [sys.executable, stager, "stage"], 1, "Refusing to overwrite")
    assert dest.read_bytes() == b"deliberately mismatched disposable output"
    dest.write_bytes(before)
    for label, destination, output_root in [("protected-runtime-destination", ROOT / "bin/Debug/Breakout/SDL2.dll", ROOT / "bin"), ("unauthorized-output-root", ROOT / "GEngineEditor/tbb12.dll", ROOT / "GEngineEditor")]:
        old = sha(destination) if destination.is_file() else "ABSENT"
        try:
            original.supply(original.SOURCE, destination, output_root)
            raise RuntimeError("Expected staging refusal")
        except AssertionError as error:
            records.append({"test": label, "expected_refusal": str(error)})
        assert (sha(destination) if destination.is_file() else "ABSENT") == old
    try:
        original.supply(ROOT / "bin/Debug/Breakout/SDL2.dll", ROOT / "bin/Debug/RayTracing/tbb12.dll", ROOT / "bin")
        raise RuntimeError("Expected wrong-source refusal")
    except AssertionError as error:
        records.append({"test": "wrong-source-identity", "expected_refusal": str(error)})
    result = {"result": "PASS", "fixture": str(fixture), "method": "Byte-identical accepted helpers and current outputs relocated into a new disposable fixture; ROOT follows copied helper location. Original Revision 01 fixture and protected runtime files are untouched. No script/version/graph change.", "copied_files": copied, "records": records}
    write_json(EVIDENCE / "safeguard-results.json", result)
    print(json.dumps(result))


if __name__ == "__main__":
    action = sys.argv[1]
    if action == "safeguards":
        sys.exit(run("safeguards", [sys.executable, Path(__file__), "_safeguards"], timeout=600))
    {"compilers": compilers, "resources": resources, "_safeguards": safeguards}[action]()
