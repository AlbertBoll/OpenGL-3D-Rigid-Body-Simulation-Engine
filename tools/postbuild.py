"""Copy optional application overlays and verify the checked-in Windows runtime."""

from pathlib import Path
import shutil
import subprocess
import sys


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
        if project == "GEngineEditor":
            names.append("assimp-vc140-mt.dll")
    return names


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

    if windows:
        missing = [name for name in runtime_dlls(config, project) if not (dest / name).is_file()]
        if missing:
            raise RuntimeError(
                f"Missing tracked runtime DLLs in {dest}: {', '.join(missing)}. "
                "Restore the repository baseline from its root with: "
                f"git restore --source=HEAD --worktree -- bin/{config}/{project}"
            )
        print(f"Runtime baseline verified: {config}/{project}", flush=True)
