"""Focused postbuild/launcher contract tests; all mutations use disposable fixtures."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("asset_postbuild", HERE / "postbuild.py")
postbuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(postbuild)


class RuntimeStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="runtime-assets-")
        self.addCleanup(self.temp.cleanup)
        self.repo = Path(self.temp.name) / "checkout with spaces"
        self.repo.mkdir()
        self.package = {
            "schema": 1,
            "files": {"Fonts/ui.ttf": "source/ui.ttf", "Breakout/levels/one.lvl": "source/one.lvl"},
            "startup": {app: ["Fonts/ui.ttf"] for app in
                        ["GEngineEditor", "RigidBodySimulation", "Breakout", "RayTracing"]},
        }
        self.package["startup"]["Breakout"].append("Breakout/levels/one.lvl")
        self.write("tools/runtime_assets.json", json.dumps(self.package))
        self.write("source/ui.ttf", "fixture font")
        self.write("source/one.lvl", "1 2 3\n")
        self.project = self.repo / "RayTracing"
        self.project.mkdir()
        self.destination = self.repo / "bin/Debug/assets"
        for name in postbuild.runtime_dlls("Debug", "RayTracing"):
            self.write("bin/Debug/RayTracing/" + name, "fixture dll")

    def write(self, path, contents):
        p = self.repo / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(contents, encoding="utf-8")

    def invoke_postbuild(self):
        code = ("import sys; sys.path.insert(0, sys.argv[1]); from postbuild import main; "
                "main(sys.argv[2], ['config=Debug', 'prj=RayTracing'])")
        return subprocess.run([sys.executable, "-c", code, str(HERE), str(self.project)],
                              cwd=self.temp.name, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, timeout=30)

    def test_real_postbuild_stages_from_unrelated_cwd_with_scoped_lists(self):
        result = self.invoke_postbuild()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual((self.destination / "Fonts/ui.ttf").read_text(), "fixture font")
        ray = (self.destination / "startup/RayTracing.txt").read_bytes()
        breakout = (self.destination / "startup/Breakout.txt").read_bytes()
        self.assertNotIn(b"Breakout/levels", ray)
        self.assertIn(b"Breakout/levels/one.lvl", breakout)
        self.assertNotIn(b"\r", ray)
        first = (self.destination / "Fonts/ui.ttf").stat().st_mtime_ns
        self.assertEqual(self.invoke_postbuild().returncode, 0)
        self.assertEqual((self.destination / "Fonts/ui.ttf").stat().st_mtime_ns, first)

    def test_missing_staging_source_is_nonzero_and_actionable(self):
        (self.repo / "source/ui.ttf").unlink()
        result = self.invoke_postbuild()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing runtime staging source", result.stdout)
        self.assertIn("source", result.stdout)
        self.assertIn("ui.ttf", result.stdout)
        self.assertIn("rebuild", result.stdout)
        self.assertFalse((self.destination / "startup/RayTracing.txt").exists())

    def test_real_copy_failure_propagates_from_postbuild(self):
        self.write("bin/Debug/assets/Fonts", "file obstructs destination directory")
        result = self.invoke_postbuild()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Runtime asset copy failed", result.stdout)
        self.assertIn("ui.ttf", result.stdout)
        self.assertFalse((self.destination / "startup/RayTracing.txt").exists())

    def test_copy_failure_does_not_publish_success_manifest(self):
        with mock.patch.object(postbuild.shutil, "copy2", side_effect=PermissionError("fixture denied")):
            with self.assertRaisesRegex(RuntimeError, "copy failed.*fixture denied"):
                postbuild.stage_runtime_assets(self.repo, self.destination)
        self.assertFalse((self.destination / "startup/RayTracing.txt").exists())

    def test_manifest_escape_is_rejected(self):
        self.package["files"]["../escape"] = "source/ui.ttf"
        self.write("tools/runtime_assets.json", json.dumps(self.package))
        with self.assertRaisesRegex(RuntimeError, "Invalid runtime asset manifest path"):
            postbuild.stage_runtime_assets(self.repo, self.destination)

    def test_unknown_required_asset_is_rejected(self):
        self.package["startup"]["RayTracing"].append("not-declared.ttf")
        self.write("tools/runtime_assets.json", json.dumps(self.package))
        with self.assertRaisesRegex(RuntimeError, "Invalid startup asset list for RayTracing"):
            postbuild.stage_runtime_assets(self.repo, self.destination)

    def test_real_package_keeps_application_requirements_separate(self):
        package = json.loads((HERE / "runtime_assets.json").read_text())
        self.assertIn("Images/heightmap.png", package["startup"]["RayTracing"])
        for app in ["GEngineEditor", "RigidBodySimulation", "RayTracing"]:
            self.assertFalse(any(name.startswith("Breakout/") for name in package["startup"][app]))
        for name, source in package["files"].items():
            self.assertTrue((HERE.parent / source).is_file(), name)


if __name__ == "__main__":
    unittest.main(verbosity=2)
