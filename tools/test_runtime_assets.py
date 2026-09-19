"""Focused postbuild/launcher contract tests; all mutations use disposable fixtures."""
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import os
import shutil
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


@unittest.skipUnless(sys.platform == "win32", "Windows Assimp runtime closure")
class AssimpClosureTests(unittest.TestCase):
    write = RuntimeStagingTests.write

    def setUp(self):
        RuntimeStagingTests.setUp(self)
        self.project = self.repo / "RigidBodySimulation"
        self.project.mkdir()
        self.config = "Release"
        for relative in (postbuild.ASSIMP_RUNTIME_SOURCE, "external/assimp/lib/assimp.lib"):
            target = self.repo / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(HERE.parent / relative, target)
        for config in ("Debug", "Release"):
            for name in postbuild.runtime_dlls(config, self.project.name):
                if name != "assimp-vc140-mt.dll":
                    self.write(f"bin/{config}/RigidBodySimulation/" + name, "unchanged fixture dependency")

    def invoke_postbuild(self):
        code = ("import sys; sys.path.insert(0, sys.argv[1]); from postbuild import main; "
                "main(sys.argv[2], ['config=' + sys.argv[3], 'prj=RigidBodySimulation'])")
        return subprocess.run([sys.executable, "-c", code, str(HERE), str(self.project), self.config],
                              cwd=self.temp.name, capture_output=True, text=True, timeout=30)

    def test_assimp_clean_output_and_same_size_corruption_are_repaired(self):
        for config in ("Release", "Debug"):
            self.config = config
            target = self.repo / f"bin/{config}/RigidBodySimulation/assimp-vc140-mt.dll"
            self.assertFalse(target.exists())
            result = self.invoke_postbuild()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(), postbuild.ASSIMP_RUNTIME_SHA256)
            info = target.stat()
            with target.open("r+b") as handle:
                handle.write(b"XX")
            os.utime(target, ns=(info.st_atime_ns, info.st_mtime_ns))
            self.assertEqual(self.invoke_postbuild().returncode, 0)
            self.assertEqual(hashlib.sha256(target.read_bytes()).hexdigest(), postbuild.ASSIMP_RUNTIME_SHA256)

    def test_assimp_source_and_library_identity_required_even_when_destination_exists(self):
        self.assertEqual(self.invoke_postbuild().returncode, 0)
        for relative in (postbuild.ASSIMP_RUNTIME_SOURCE, "external/assimp/lib/assimp.lib"):
            path = self.repo / relative
            original = path.read_bytes()
            path.write_bytes(b"incidental or wrong dependency")
            result = self.invoke_postbuild()
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Bundled Assimp identity mismatch", result.stderr)
            path.write_bytes(original)
        (self.repo / postbuild.ASSIMP_RUNTIME_SOURCE).unlink()
        result = self.invoke_postbuild()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot stage bundled Assimp", result.stderr)

    def test_assimp_copy_failure_is_nonzero(self):
        self.write("bin/Release/RigidBodySimulation/assimp-vc140-mt.dll", "wrong dll")
        with mock.patch.object(postbuild.shutil, "copy2", side_effect=PermissionError("fixture denied")):
            with self.assertRaisesRegex(RuntimeError, "Cannot stage bundled Assimp.*fixture denied"):
                postbuild.stage_assimp_runtime(self.repo, self.repo / "bin/Release/RigidBodySimulation")

    def test_smoke_rejects_missing_or_wrong_output_despite_editor_path(self):
        import test_async_mesh
        target = self.repo / "bin/Release/RigidBodySimulation/assimp-vc140-mt.dll"
        with mock.patch.object(test_async_mesh, "ROOT", self.repo), mock.patch.dict(
                os.environ, {"Path": str((self.repo / postbuild.ASSIMP_RUNTIME_SOURCE).parent)}):
            for exists in (False, True):
                if exists:
                    target.write_bytes(b"wrong installed dependency")
                result = test_async_mesh.standalone_smoke("Release", self.repo / "smoke")
                self.assertEqual(result["result"], "FAIL")
                self.assertIn(str(target), result["reason"])
                self.assertEqual(target.exists(), exists)  # Validation never repairs staging.


if __name__ == "__main__":
    unittest.main(verbosity=2)
