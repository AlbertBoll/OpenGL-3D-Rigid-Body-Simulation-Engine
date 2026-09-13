"""Supply only the two absent oneTBB runtime outputs for the existing Premake graph.

Run after the existing Premake build. The source is the official checksum-verified
oneTBB 2021.5.0 desktop x64 distribution, whose import library exactly matches the
repository. No tracked input or pre-existing output is overwritten.
"""

import argparse
import json
from pathlib import Path
import subprocess
import zipfile

from pe_runtime import ROOT, machine, resolve_closure, sha

ARCHIVE_SHA = "096c004c7079af89fe990bb259d58983b0ee272afa3a7ef0733875bfe09fcd8e"
IMPORT_SHA = "bb72fee5dfebcd6d17448751647a5027027ca4d9309fdae0d4b099c72873ba72"
DLL_SHA = "3bda7c5458d7f43bcd49f0dead5114eac1ea14f573aafc736f833089b1d2cd79"
ORIGIN = ROOT / "bin-int/build-phase-00-revision-01/tbb-origin"
ARCHIVE = ORIGIN / "oneapi-tbb-2021.5.0-win.zip"
MEMBER = "oneapi-tbb-2021.5.0/redist/intel64/vc14/tbb12.dll"
SOURCE = ORIGIN / MEMBER
APPS = ["Breakout", "GEngineEditor", "RayTracing", "RigidBodySimulation"]


def verified_source():
    assert sha(ARCHIVE) == ARCHIVE_SHA, "Wrong upstream archive"
    assert sha(ROOT / "external/tbb/lib/tbb12.lib") == IMPORT_SHA, "Consumed import library changed"
    with zipfile.ZipFile(ARCHIVE) as archive:
        assert archive.read("oneapi-tbb-2021.5.0/lib/intel64/vc14/tbb12.lib") == (ROOT / "external/tbb/lib/tbb12.lib").read_bytes()
        assert archive.read(MEMBER) == SOURCE.read_bytes(), "DLL is not the selected distribution member"
    assert sha(SOURCE) == DLL_SHA and machine(SOURCE) == "0x8664", "Wrong runtime identity/architecture"
    return SOURCE


def checked_output_root(output_root):
    output_root = Path(output_root).resolve()
    copied = (ROOT / "bin-int/build-phase-00-revision-01/safeguard-copy/bin").resolve()
    assert output_root in [(ROOT / "bin").resolve(), copied], "Output root not authorized"
    return output_root


def supply(source, destination, output_root):
    source = Path(source).resolve()
    output_root = checked_output_root(output_root)
    destination = Path(destination).resolve()
    assert destination in [output_root / c / "RayTracing/tbb12.dll" for c in ["Debug", "Release"]], "Destination not one of the two runtime outputs"
    tracked = subprocess.run(["git", "-c", "safe.directory=C:/dev/GEngine-build", "ls-files", "--", destination.relative_to(ROOT).as_posix()],
                             cwd=ROOT, capture_output=True, text=True, check=True).stdout
    assert not tracked, "Refusing a Git-tracked destination"
    assert sha(source) == DLL_SHA and machine(source) == "0x8664", "Wrong source identity"
    if destination.exists():
        assert destination.is_file() and sha(destination) == DLL_SHA, "Refusing to overwrite a pre-existing output"
        return {"destination": str(destination), "action": "verified-existing; no write", "sha256": DLL_SHA}
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open("xb") as stream:
        stream.write(source.read_bytes())
    assert sha(destination) == DLL_SHA
    return {"source": str(source), "destination": str(destination), "action": "created-previously-absent", "sha256": DLL_SHA}


def stage(output_root=ROOT / "bin"):
    source = verified_source()
    output_root = checked_output_root(output_root)
    return [supply(source, output_root / c / "RayTracing/tbb12.dll", output_root) for c in ["Debug", "Release"]]


def check(output_root=ROOT / "bin"):
    output_root = checked_output_root(output_root)
    for config in ["Debug", "Release"]:
        path = output_root / config / "RayTracing/tbb12.dll"
        assert path.is_file() and sha(path) == DLL_SHA, "Missing or mismatched staged runtime: " + str(path)
    audits = [resolve_closure(output_root / c / a / (a + ".exe")) for c in ["Debug", "Release"] for a in APPS]
    assert all(not a["missing"] for a in audits), "Runtime dependency closure incomplete"
    assert all(n["machine"] == "0x8664" for a in audits for n in a["nodes"]), "Non-x64 runtime"
    return audits


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["stage", "check"])
    parser.add_argument("--output-root", type=Path, default=ROOT / "bin")
    args = parser.parse_args()
    print(json.dumps(stage(args.output_root) if args.action == "stage" else check(args.output_root), indent=2))
