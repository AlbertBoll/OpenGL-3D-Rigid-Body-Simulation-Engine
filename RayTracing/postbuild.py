from pathlib import Path
import sys

project_dir = Path(__file__).resolve().parent
sys.path.insert(0, str(project_dir.parent / "tools"))
from postbuild import main

if __name__ == "__main__":
    main(project_dir, sys.argv[1:])
