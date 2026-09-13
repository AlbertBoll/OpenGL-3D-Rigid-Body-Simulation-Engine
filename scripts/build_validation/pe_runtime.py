"""Read PE runtime dependencies using the pinned VS2022 inspection tool."""

import hashlib
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DUMPBIN = Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe")


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def machine(path):
    with Path(path).open("rb") as stream:
        assert stream.read(2) == b"MZ", path
        stream.seek(0x3c)
        pe = struct.unpack("<I", stream.read(4))[0]
        stream.seek(pe)
        assert stream.read(4) == b"PE\0\0"
        return hex(struct.unpack("<H", stream.read(2))[0])


def imports(path):
    result = subprocess.run([str(DUMPBIN), "/imports", str(path)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    return {"path": str(path), "sha256": sha(path), "machine": machine(path),
            "dll_imports": sorted(set(re.findall(r"^    (\S+\.dll)\s*$", result.stdout, re.M | re.I)), key=str.lower),
            "dumpbin_command": [str(DUMPBIN), "/imports", str(path)], "dumpbin_stdout": result.stdout}


def resolve_closure(exe, extra_sources=None):
    """Report direct/non-system transitive imports without treating PATH as an origin."""
    exe = Path(exe).resolve()
    system = Path("C:/Windows/System32")
    pending = [exe]
    seen = set()
    nodes = []
    edges = []
    while pending:
        path = pending.pop()
        if str(path).casefold() in seen:
            continue
        seen.add(str(path).casefold())
        node = imports(path)
        nodes.append(node)
        for name in node["dll_imports"]:
            candidates = [exe.parent / name, system / name]
            found = next((p for p in candidates if p.is_file()), None)
            supplied = (extra_sources or {}).get(name.lower())
            api_set = name.lower().startswith(("api-ms-", "ext-ms-"))
            edge = {"from": str(path), "dll": name, "search_candidates": [str(p) for p in candidates],
                    "resolved": str(found) if found else "WINDOWS_API_SET" if api_set else "MISSING",
                    "proposed_source": str(supplied) if not found and supplied else None}
            edges.append(edge)
            if found and found.parent == exe.parent:
                pending.append(found)
            elif not found and supplied:
                pending.append(Path(supplied))
            elif found:
                edge.update({"system_sha256": sha(found), "machine": machine(found)})
    return {"exe": str(exe), "nodes": nodes, "edges": edges,
            "missing": [e for e in edges if e["resolved"] == "MISSING"],
            "system_boundary": "Windows System32 and API-set forwarding are declared OS dependencies; loaded module paths/hashes are captured during startup. No machine/global PATH origin is accepted."}
