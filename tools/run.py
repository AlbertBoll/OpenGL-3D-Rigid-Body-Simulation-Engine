from pathlib import Path
import subprocess
import sys
import globals

# Resolve the program from this launcher, independently of the invoking shell.
root = Path(__file__).resolve().parent.parent
args = globals.ProcessArguments(sys.argv)
config = globals.GetArgumentValue(args, "config", "Debug")
project = globals.GetArgumentValue(args, "prj", globals.PROJECT_NAME)
if config not in ("Debug", "Release") or project not in (
        "GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing", "PhysicsTests", "PhysicsBenchmark"):
    raise SystemExit("Expected config=Debug|Release and a maintained prj=<Application> target")
directory = root / "bin" / config / project
executable = directory / (project + (".exe" if globals.IsWindows() else ""))
if not executable.is_file():
    raise SystemExit(f"Executable missing: {executable}. Build {project} ({config}) first.")
sys.exit(subprocess.call([str(executable)], cwd=directory))
