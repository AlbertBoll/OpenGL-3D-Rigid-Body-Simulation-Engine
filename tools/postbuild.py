"""Copy optional application overlays and verify the checked-in Windows runtime."""

from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import shutil
import subprocess
import sys


# The bundled import library names this DLL in both configurations. Keep the
# existing tracked Editor copy as the source; never discover it through PATH.
ASSIMP_RUNTIME_SOURCE = "bin/Release/GEngineEditor/assimp-vc140-mt.dll"
ASSIMP_RUNTIME_SHA256 = "eb1afc667fc94b961be3a3c058960cbbedbcdf6d850fbb9395b22771e3f7411b"
ASSIMP_IMPORT_LIBRARY_SHA256 = "d6e82cad3f705ce86bf3da2f472461d183c39d1e4a86559df587143a10d33d0a"


def stage_assimp_runtime(repo, destination):
    """Repair the selected consumer's app-local Assimp closure, or fail build."""
    repo, destination = Path(repo).resolve(), Path(destination).resolve()
    source = repo / ASSIMP_RUNTIME_SOURCE
    library = repo / "external/assimp/lib/assimp.lib"
    target = destination / source.name
    temporary = target.with_name(target.name + f".{os.getpid()}.stage-tmp")
    try:
        for path, expected in ((source, ASSIMP_RUNTIME_SHA256), (library, ASSIMP_IMPORT_LIBRARY_SHA256)):
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise RuntimeError(f"Bundled Assimp identity mismatch: {path}. Restore the selected dependency and rebuild.")
        if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != ASSIMP_RUNTIME_SHA256:
            destination.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, temporary)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != ASSIMP_RUNTIME_SHA256:
                raise RuntimeError(f"Assimp staging verification failed: {source} -> {target}")
            os.replace(temporary, target)
    except OSError as error:
        raise RuntimeError(f"Cannot stage bundled Assimp: {source} -> {target}: {error}. Restore the selected dependency or fix destination access and rebuild.") from error
    finally:
        if temporary.is_file():
            temporary.unlink()
    print(f"Assimp runtime verified: {source} -> {target}; SHA-256 {ASSIMP_RUNTIME_SHA256}", flush=True)


def runtime_dlls(config, project):
    names = [
        "SDL2.dll", "SDL2_ttf.dll", "assimp.dll",
        "fmod64.dll", "fmodL64.dll", "fmodstudio64.dll", "fmodstudioL64.dll",
        "libfreetype-6.dll", "zlib1.dll",
    ]
    if config == "Debug":
        names.append("assimp-vc140-mt.dll")
    else:
        names.extend(["assimp-vc143-mt.dll", "assimp-vc143-mtd.dll"])
        if project in ("GEngineEditor", "RigidBodySimulation"):
            names.append("assimp-vc140-mt.dll")
    return names



def stage_runtime_assets(repo, destination):
    """Stage the declared package; source or copy errors fail the postbuild."""
    repo, destination = Path(repo).resolve(), Path(destination).resolve()
    manifest = repo / "tools/runtime_assets.json"
    try:
        package = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError(f"Cannot read runtime asset manifest {manifest}: {error}") from error

    def relative(value):
        path = PurePosixPath(value)
        if not value or path.is_absolute() or ".." in path.parts or ":" in value or "\\" in value or path.as_posix() != value:
            raise RuntimeError(f"Invalid runtime asset manifest path: {value!r}")
        return path

    if package.get("schema") != 1 or set(package.get("startup", {})) != {"GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing"}:
        raise RuntimeError(f"Invalid runtime asset package schema/profiles: {manifest}")
    files = package["files"]
    if not files:
        raise RuntimeError(f"Empty runtime asset manifest: {manifest}")
    for name, source_name in files.items():
        relative(name)
        source = repo / relative(source_name)
        if not source.resolve().is_relative_to(repo) or not source.is_file():
            raise RuntimeError(f"Missing runtime staging source: {source}; resource {name}; destination {destination}. Restore that tracked asset and rebuild.")
    for application, required in package["startup"].items():
        if not required or len(set(required)) != len(required) or not set(required) <= files.keys():
            raise RuntimeError(f"Invalid startup asset list for {application}: {manifest}")

    copied = 0
    for name, source_name in files.items():
        source, target = repo / source_name, destination / name
        temporary = target.with_name(target.name + f".{os.getpid()}.stage-tmp")
        try:
            info = source.stat()
            if target.is_file() and target.stat().st_size == info.st_size and target.stat().st_mtime_ns == info.st_mtime_ns:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, temporary)
            os.replace(temporary, target)
            copied += 1
        except OSError as error:
            raise RuntimeError(f"Runtime asset copy failed: {source} -> {target}: {error}. Fix access/disk space and rebuild.") from error
        finally:
            if temporary.is_file():
                temporary.unlink()
    for application, required in package["startup"].items():
        target = destination / "startup" / (application + ".txt")
        temporary = target.with_name(target.name + f".{os.getpid()}.stage-tmp")
        data = ("GENGINE_STARTUP_ASSETS_V1 " + application + "\n" + "\n".join(required) + "\n").encode("utf-8")
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_bytes(data)
            os.replace(temporary, target)
        except OSError as error:
            raise RuntimeError(f"Cannot publish startup asset list for {application} at {target}: {error}") from error
        finally:
            if temporary.is_file():
                temporary.unlink()
    print(f"Runtime assets staged: {destination} ({len(files)} files; {copied} copied)", flush=True)


def main(project_dir, arguments):
    project_dir = Path(project_dir).resolve()
    args = dict(arg.split("=", 1) for arg in arguments)
    config = args.get("config", "Debug")
    project = args.get("prj", project_dir.name)
    if config not in ("Debug", "Release") or project != project_dir.name:
        raise ValueError("Expected config=Debug|Release and prj=" + project_dir.name)

    dest = project_dir.parent / "bin" / config / project
    sources = [project_dir / "PostBuildCopy"]
    windows = sys.platform == "win32"
    if windows:
        sources.append(project_dir / "PostBuildCopy_windows")

    for source in sources:
        if not source.exists():
            print("Optional post-build directory absent: " + str(source), flush=True)
            continue
        if windows:
            result = subprocess.run(["robocopy", str(source), str(dest), "/E", "/R:0", "/W:0"])
            # Robocopy codes 0-7 are successful; 8 and above indicate failure.
            if result.returncode >= 8:
                raise RuntimeError(f"robocopy failed ({result.returncode}): {source} -> {dest}")
        else:
            shutil.copytree(source, dest, dirs_exist_ok=True)

    stage_runtime_assets(project_dir.parent, dest.parent / "assets")

    if windows:
        if project == "RigidBodySimulation":
            stage_assimp_runtime(project_dir.parent, dest)
        missing = [name for name in runtime_dlls(config, project) if not (dest / name).is_file()]
        if missing:
            raise RuntimeError(
                f"Missing tracked runtime DLLs in {dest}: {', '.join(missing)}. "
                "Restore the repository baseline from its root with: "
                f"git restore --source=HEAD --worktree -- bin/{config}/{project}"
            )
        print(f"Runtime baseline verified: {config}/{project}", flush=True)
