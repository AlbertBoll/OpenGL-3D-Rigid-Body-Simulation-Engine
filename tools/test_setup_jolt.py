"""Focused, filesystem-isolated checks for the pinned Jolt extension verifier."""
import difflib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("setup_jolt_checked", ROOT / "tools/setup_jolt.py")
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
HEAD = "e77f175595e64cb44218cc9d9d56fc365ad0e36a"
NEW = "Jolt/Physics/Constraints/ConstraintPart/ContactResistanceConstraintPart.h"


class Fixture:
    def __init__(self, patched=True):
        directory = ROOT / "logs/pre_editor/phase-26/jolt-integration-01/g8-contact-resistance-01"
        if not directory.is_dir():
            raise RuntimeError("Admitted evidence directory is missing")
        setup.local_path(ROOT, directory.relative_to(ROOT).as_posix())
        self.root = Path(tempfile.mkdtemp(prefix="pin-unit-", dir=directory))
        self.dest = self.root / "external/jolt"
        self.base = {name: b"// " + name.encode() + b"\nfirst\nlast\n"
                     for name in setup.EXTENSION_FILES if name != NEW}
        self.result = {name: (self.base[name].replace(b"first\n", b"first changed\n")
                             if name in self.base else b"// new helper\nsecond\n")
                       for name in setup.EXTENSION_FILES}
        self.changed = set(self.base) if patched else set()
        self.untracked = {NEW} if patched else set()
        self.staged = set()
        self.commit = HEAD
        self.pin = dict(version="5.6.0", commit=HEAD, source="external/jolt")
        self.manifest_path = self.root / "external/jolt-patches/gengine-contact-resistance-01.json"
        self.patch_path = self.root / "external/jolt-patches/gengine-contact-resistance-01.patch"
        for name, content in (self.result if patched else self.base).items():
            self.write(self.dest / name, setup.checkout_bytes(content, "CRLF"))
        self.write(self.dest / "Jolt/Jolt.cmake", b"# pinned fixture\n")
        if patched:
            records = []
            chunks = []
            for name in sorted(setup.EXTENSION_FILES):
                original = self.base.get(name)
                target = self.result[name]
                def identity(value):
                    return dict(sha256=setup.digest(setup.checkout_bytes(value, "CRLF")),
                                canonical_sha256=setup.digest(value), mode="100644", line_endings="CRLF")
                records.append(dict(path=name, base=identity(original) if original is not None else None,
                                    result=identity(target)))
                chunks.append(b"diff --git a/" + name.encode() + b" b/" + name.encode() + b"\n")
                if original is None:
                    chunks.append(b"new file mode 100644\n")
                chunks.append(b"index 0000000..1111111 100644\n")
                chunks.extend(difflib.diff_bytes(difflib.unified_diff,
                    (original or b"").splitlines(keepends=True), target.splitlines(keepends=True),
                    fromfile=b"a/" + name.encode() if original is not None else b"/dev/null",
                    tofile=b"b/" + name.encode()))
            self.write(self.patch_path, b"".join(chunks))
            self.manifest = dict(schema=1, id=setup.EXTENSION_ID, upstream_commit=HEAD,
                patch=self.patch_path.relative_to(self.root).as_posix(), files=records)
            self.seal()
        else:
            self.save_pin()

    @staticmethod
    def write(path, content):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)

    def save_pin(self):
        self.write(self.root / "external/jolt-pin.json", json.dumps(self.pin, indent=2).encode())

    def seal(self):
        self.manifest["patch_sha256"] = setup.digest(self.patch_path.read_bytes())
        self.write(self.manifest_path, json.dumps(self.manifest, indent=2).encode())
        self.pin["gengine_extension"] = dict(id=setup.EXTENSION_ID,
            manifest=self.manifest_path.relative_to(self.root).as_posix(),
            manifest_sha256=setup.digest(self.manifest_path.read_bytes()))
        self.save_pin()

    def git(self, dest, *args):
        if dest != self.dest:
            raise AssertionError("Unexpected repository")
        def nul(values):
            return b"".join(value.encode() + b"\0" for value in sorted(values))
        if args == ("rev-parse", "HEAD"):
            return self.commit.encode() + b"\n"
        if args == ("diff", "--cached", "--name-only", "HEAD", "-z"):
            return nul(self.staged)
        if args == ("diff", "--no-ext-diff", "--name-only", "HEAD", "-z"):
            return nul(self.changed)
        if args == ("ls-files", "--others", "--exclude-standard", "-z"):
            return nul(self.untracked)
        if args[:3] == ("ls-tree", "HEAD", "--"):
            name = args[3]
            return (b"100644 blob deadbeef\t" + name.encode() + b"\n") if name in self.base else b""
        if args[0] == "show" and args[1].startswith("HEAD:"):
            return self.base[args[1][5:]]
        raise AssertionError("Unexpected Git command: " + repr(args))

    def verify(self, profile="configured"):
        with patch.object(setup, "git", side_effect=self.git):
            return setup.verify(self.root, profile)

    def snapshot(self):
        return {str(path.relative_to(self.root)): (path.read_bytes(), path.stat().st_mtime_ns)
                for path in self.root.rglob("*") if path.is_file()}


class VerificationTests(unittest.TestCase):
    def test_pristine_pin(self):
        fixture = Fixture(False)
        self.assertEqual(fixture.verify(), "Jolt pin verified: " + HEAD)

    def test_patched_exact_and_read_only(self):
        fixture = Fixture()
        before = fixture.snapshot()
        self.assertIn(setup.EXTENSION_ID, fixture.verify())
        self.assertEqual(fixture.snapshot(), before)

    def test_explicit_pristine_rejects_patch(self):
        with self.assertRaisesRegex(setup.VerificationError, "pristine"):
            Fixture().verify("pristine")

    def test_unknown_profile_rejected(self):
        with self.assertRaisesRegex(setup.VerificationError, "profile"):
            Fixture(False).verify("unknown")

    def test_wrong_upstream(self):
        fixture = Fixture()
        fixture.commit = "0" * 40
        with self.assertRaisesRegex(setup.VerificationError, "commit"):
            fixture.verify()

    def test_extra_tracked_change(self):
        fixture = Fixture()
        fixture.changed.add("Jolt/extra.cpp")
        with self.assertRaisesRegex(setup.VerificationError, "membership"):
            fixture.verify()

    def test_extra_untracked_header(self):
        fixture = Fixture()
        fixture.untracked.add("Jolt/extra.h")
        with self.assertRaisesRegex(setup.VerificationError, "membership"):
            fixture.verify()

    def test_staged_changes(self):
        fixture = Fixture()
        fixture.staged.add(next(iter(fixture.base)))
        with self.assertRaisesRegex(setup.VerificationError, "Staged"):
            fixture.verify()

    def test_missing_helper(self):
        fixture = Fixture()
        (fixture.dest / NEW).unlink()
        with self.assertRaisesRegex(setup.VerificationError, "missing"):
            fixture.verify()

    def test_partial_patch(self):
        fixture = Fixture()
        name = sorted(fixture.base)[0]
        fixture.write(fixture.dest / name, setup.checkout_bytes(fixture.base[name], "CRLF"))
        with self.assertRaisesRegex(setup.VerificationError, "result hash"):
            fixture.verify()

    def test_wrong_patch_bytes(self):
        fixture = Fixture()
        fixture.write(fixture.patch_path, fixture.patch_path.read_bytes() + b"\n")
        with self.assertRaisesRegex(setup.VerificationError, "patch hash"):
            fixture.verify()

    def test_wrong_manifest_bytes(self):
        fixture = Fixture()
        fixture.write(fixture.manifest_path, fixture.manifest_path.read_bytes() + b"\n")
        with self.assertRaisesRegex(setup.VerificationError, "manifest hash"):
            fixture.verify()

    def test_pinned_patch_must_reproduce_source(self):
        fixture = Fixture()
        fixture.write(fixture.patch_path, fixture.patch_path.read_bytes().replace(b"+first changed", b"+wrong content"))
        fixture.seal()
        with self.assertRaisesRegex(setup.VerificationError, "reproduction"):
            fixture.verify()

    def test_base_hash_cannot_be_invented(self):
        fixture = Fixture()
        next(item for item in fixture.manifest["files"] if item["base"])["base"]["canonical_sha256"] = "0" * 64
        fixture.seal()
        with self.assertRaisesRegex(setup.VerificationError, "upstream blob"):
            fixture.verify()

    def test_mode_mismatch(self):
        fixture = Fixture()
        fixture.manifest["files"][0]["result"]["mode"] = "100755"
        fixture.seal()
        with self.assertRaisesRegex(setup.VerificationError, "mode"):
            fixture.verify()

    def test_boundary_cannot_expand(self):
        fixture = Fixture()
        fixture.manifest["files"][0]["path"] = "Jolt/Other.h"
        fixture.seal()
        with self.assertRaisesRegex(setup.VerificationError, "boundary"):
            fixture.verify()

    def test_path_escape(self):
        fixture = Fixture()
        fixture.manifest["patch"] = "../outside.patch"
        fixture.seal()
        with self.assertRaisesRegex(setup.VerificationError, "escapes"):
            fixture.verify()

    def test_invalid_hunk_does_not_write(self):
        fixture = Fixture()
        fixture.write(fixture.patch_path, fixture.patch_path.read_bytes().replace(b"@@ -1,3 +1,3 @@", b"@@ -99,3 +1,3 @@", 1))
        fixture.seal()
        before = fixture.snapshot()
        with self.assertRaisesRegex(setup.VerificationError, "position"):
            fixture.verify()
        self.assertEqual(fixture.snapshot(), before)


if __name__ == "__main__":
    unittest.main(verbosity=2)
