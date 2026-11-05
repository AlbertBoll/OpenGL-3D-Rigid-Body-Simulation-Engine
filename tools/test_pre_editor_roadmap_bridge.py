#!/usr/bin/env python3
"""Offline governance-bridge regressions using disposable, network-disabled Git.

Every Git invocation is confined to this run's directory below
logs/pre_editor/governance/milestone-c-publication-bridge-01/tests/. Repositories
and failed candidates are retained. Named production pins are patched only by
test contexts and restored on exit; no production bypass is introduced.
"""
from __future__ import annotations

import argparse
import ast
import copy
from contextlib import ExitStack, contextmanager
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


sys.dont_write_bytecode = True
WORKSPACE = Path(__file__).resolve().parents[1]
TEST_ROOT = WORKSPACE / "logs/pre_editor/governance/milestone-c-publication-bridge-01/tests"
PARSER = argparse.ArgumentParser(add_help=False)
PARSER.add_argument("--helper", type=Path, default=WORKSPACE / ".codex/tools/rendering_checkpoint.py")
PARSER.add_argument("--bridge", type=Path, default=WORKSPACE / "tools/pre_editor_roadmap_bridge.py")
OPTIONS, UNITTEST_ARGS = PARSER.parse_known_args()
REAL_RUN = subprocess.run
TEST_SOURCE_BYTES = Path(__file__).read_bytes()
DESTINATION = "https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git"
REPOSITORY = "AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine"
BRANCH = "pre-editor/refactor"
TAG25 = "pre-editor-phase-25-approved"
BRIDGE_TAG = "pre-editor-milestone-c-roadmap-01-approved"
CANONICAL = "RigidBodySimulation/src/RigidBodySimulation.cpp"
PRODUCTION = "GEngine/src/fixture.cpp"


def load_module(path, name, source=None):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    exec(compile(path.read_bytes() if source is None else source, str(path), "exec"), module.__dict__)
    return module


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Interrupted(RuntimeError):
    """A simulated process interruption after a real durable operation."""


class Fixture:
    def __init__(self, case, label):
        self.case = case
        self.c, self.b = case.c, case.b
        self.directory = Path(tempfile.mkdtemp(prefix=label + "-", dir=case.run_root))
        self.root = self.directory / "work"
        self.origin = self.directory / "origin.git"
        self.destination = self.origin.as_posix()
        self.root.mkdir()
        self.origin.mkdir()
        self.git("init", "--initial-branch=" + BRANCH)
        self.git("config", "user.name", "Offline Bridge Test")
        self.git("config", "user.email", "offline@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "tag.gpgsign", "false")
        self.git("config", "core.autocrlf", "false")
        self.git("config", "core.filemode", "false")
        self.git("remote", "add", "origin", self.destination)
        self.git("init", "--bare", at=self.origin)
        self.write(".gitignore", b"/docs/pre_editor/active/\n/.codex/\n/.agents/\n/logs/\n")
        self.write(CANONICAL, b"// historical RBS source\n")
        self.write(PRODUCTION, b"// historical engine source\n")
        self.write("GEngine/include/GEngine/Physics/fixture.h", b"// historical physics\n")
        self.write("owner.txt", b"approved owner content\n")
        self.write("docs/rendering/REFACTOR_STATE.md", b"historical rendering state\n")
        self.write("docs/physics/REFACTOR_STATE.md", b"historical physics state\n")
        self.git("add", "--", ".gitignore", CANONICAL, PRODUCTION, "owner.txt",
                 "GEngine/include/GEngine/Physics/fixture.h",
                 "docs/rendering/REFACTOR_STATE.md", "docs/physics/REFACTOR_STATE.md")
        self.git("commit", "-m", "offline predecessor")
        self.earlier = self.text("rev-parse", "HEAD")
        self.parent_receipt = "pre-editor-checkpoints/phase-25.json"
        self.write(self.parent_receipt, encoded({"schema": 2, "phase": "25", "baseline": self.earlier,
                                                "summary": "Historical fixture approval"}))
        self.git("add", "--", self.parent_receipt)
        self.git("commit", "-m", "offline approved phase 25")
        self.parent = self.text("rev-parse", "HEAD")
        self.git("tag", "-a", TAG25, "-m", "Historical fixture phase 25")
        self.parent_tag_object = self.text("rev-parse", "refs/tags/" + TAG25)
        self.git("push", "origin", BRANCH, "refs/tags/" + TAG25)
        self.repo = self.c.Repo(self.root)
        self.state = {
            "Refactor branch": BRANCH, "Configured remote": "origin",
            "Current approved phase": "25", "Current approved tag": TAG25,
            "Next expected phase": "26", "Last approved review": "historical-phase25.md",
        }
        self.write(self.c.STATE, self.state_bytes())
        self.write("docs/pre_editor/active/ACTIVATION.json",
                   encoded({"destination_worktree": self.root.as_posix()}))
        self.write(self.c.PUBLICATION_POLICY, encoded({"schema": 1, "enabled": True,
                   "repository": REPOSITORY, "branch": BRANCH,
                   "tag_template": "pre-editor-phase-{phase}-approved"}))
        self.previous_journal = {"schema": 2, "phase": "25", "status": "COMPLETE",
            "transaction": {"commit": self.parent, "branch_pushed": True, "tag_pushed": True},
            "push_destination": self.destination, "receipt": self.parent_receipt,
            "hashes": {self.parent_receipt: self.repo.fingerprint(self.parent_receipt)}}
        self.repo.save(self.previous_journal)
        self.write(".git/rendering-approval/PHASE_25.json", b"historical rendering journal\n")
        self.write(".git/physics-approval/PHASE_25.json", b"historical physics journal\n")
        self.pins = copy.deepcopy(self.b.ROADMAP_PUBLICATION)
        self.pins.update(worktree=self.root.as_posix(), repository=REPOSITORY, branch=BRANCH,
                         remote="origin", push_destination=self.destination, parent=self.parent,
                         parent_tag=TAG25, parent_tag_object=self.parent_tag_object,
                         parent_journal_sha256=sha(self.repo.journal("25").read_bytes()),
                         parent_receipt=self.parent_receipt,
                         parent_receipt_sha256=sha(self.read(self.parent_receipt)),
                         parent_receipt_blob=self.text("rev-parse", self.parent + ":" + self.parent_receipt),
                         physics_trees={"GEngine/include/GEngine/Physics": self.text(
                             "rev-parse", self.parent + ":GEngine/include/GEngine/Physics")})
        self.prepare_inputs()

    def git(self, *args, at=None, input=None, allow_failure=False):
        result = subprocess.run(["git", "--no-optional-locks", "-C", str(at or self.root), *args],
                                input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode and not allow_failure:
            raise AssertionError(result.stderr.decode(errors="replace"))
        return result if allow_failure else result.stdout

    def text(self, *args, **kwargs):
        return self.git(*args, **kwargs).decode().strip()

    def write(self, rel, data):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def read(self, rel):
        return (self.root / rel).read_bytes()

    def state_bytes(self):
        return ("# PRE_EDITOR fixture state\n" + "".join(
            f"- {name}: `{value}`\n" for name, value in self.state.items())).encode()

    def prepare_inputs(self):
        """Create a small structurally real governance snapshot, never production."""
        self.snapshot_root = self.pins["snapshot_root"]
        self.spec_path = self.pins["spec_path"]
        self.manifest_path = self.pins["manifest_path"]
        self.authorization_path = self.pins["authorization_path"]
        self.receipt_path = self.snapshot_root + "/receipt.json"
        self.source_path = "docs/pre_editor/active/roadmap/MILESTONE_C_01/phases/PHASE_26.md"
        self.snapshot_path = self.snapshot_root + "/roadmap/phases/PHASE_26.md"
        self.write(self.source_path, b"# PRE_EDITOR Phase 26 fixture\nAccepted prospective scope.\n")
        self.write(self.snapshot_path, self.read(self.source_path))
        self.helper_path = "tools/pre_editor_roadmap_bridge.py"
        self.write(self.helper_path, self.case.tested_bytes["bridge"])
        self.write("tools/test_pre_editor_roadmap_bridge.py", self.case.tested_bytes["tests"])
        checkpoint_snapshot = self.snapshot_root + "/tooling/rendering_checkpoint.py"
        checkpoint_source = ".codex/tools/rendering_checkpoint.py"
        self.write(checkpoint_source, self.case.tested_bytes["helper"])
        self.write(checkpoint_snapshot, self.read(checkpoint_source))
        self.activation_path = self.pins["activation_path"]
        self.activation_snapshot = self.snapshot_root + "/governance/activation.json"
        self.activation = {"schema": 1, "workflow": "pre-editor",
            "status": "ACTIVE_PROSPECTIVE_FOR_PHASES_26_58", "effective_phases": {"first": "26", "last": "58"},
            "roadmap_root": "docs/pre_editor/active/roadmap/MILESTONE_C_01",
            "artifact_hashes": {self.source_path: sha(self.read(self.source_path))}}
        self.write(self.activation_path, encoded(self.activation))
        self.write(self.activation_snapshot, self.read(self.activation_path))
        self.pins["activation_sha256"] = sha(self.read(self.activation_path))
        self.pins["snapshot_source_paths"] = [checkpoint_source, self.activation_path]
        self.local_bindings = {self.source_path: sha(self.read(self.source_path)),
                               self.c.STATE: sha(self.read(self.c.STATE)),
                               checkpoint_source: sha(self.read(checkpoint_source)),
                               self.activation_path: sha(self.read(self.activation_path))}
        self.files = [
            {"path": self.snapshot_path, "sha256": sha(self.read(self.snapshot_path)),
             "mode": "100644", "role": "snapshot", "source": self.source_path},
            {"path": self.helper_path, "sha256": sha(self.read(self.helper_path)),
             "mode": "100644", "role": "helper"},
            {"path": "tools/test_pre_editor_roadmap_bridge.py",
             "sha256": sha(self.read("tools/test_pre_editor_roadmap_bridge.py")),
             "mode": "100644", "role": "helper"},
            {"path": checkpoint_snapshot, "sha256": sha(self.read(checkpoint_snapshot)),
             "mode": "100644", "role": "snapshot", "source": checkpoint_source},
            {"path": self.activation_snapshot, "sha256": sha(self.read(self.activation_snapshot)),
             "mode": "100644", "role": "snapshot", "source": self.activation_path},
        ]
        payload = {"schema": 1, "identity": self.pins["identity"],
                   "files": self.files, "local_bindings": self.local_bindings}
        self.receipt = {"schema": 1, "identity": self.pins["identity"], "parent": self.parent,
            "parent_tag": TAG25, "parent_tag_object": self.parent_tag_object,
            "parent_journal_sha256": self.pins["parent_journal_sha256"],
            "parent_receipt_sha256": self.pins["parent_receipt_sha256"],
            "tag": self.pins["tag"], "repository": REPOSITORY, "branch": BRANCH,
            "remote": "origin", "push_destination": self.destination,
            "manifest_payload_sha256": sha(encoded(payload))}
        self.write(self.receipt_path, encoded(self.receipt))
        self.files.append({"path": self.receipt_path, "sha256": sha(self.read(self.receipt_path)),
                           "mode": "100644", "role": "snapshot"})
        self.manifest = {"schema": 1, "identity": self.pins["identity"], "files": self.files,
                         "local_bindings": self.local_bindings}
        self.write(self.manifest_path, encoded(self.manifest))
        self.spec = {"schema": 1, "identity": self.pins["identity"], "parent": self.parent,
                     "tag": self.pins["tag"], "repository": REPOSITORY, "branch": BRANCH,
                     "remote": "origin", "push_destination": self.destination,
                     "manifest": self.manifest_path, "manifest_sha256": sha(self.read(self.manifest_path))}
        self.write(self.spec_path, encoded(self.spec))
        self.authorization = {"schema": 1, "status": "OWNER_AUTHORIZED_PUBLICATION",
            "identity": self.pins["identity"], "spec_sha256": sha(self.read(self.spec_path)),
            "parent": self.parent, "tag": self.pins["tag"], "repository": REPOSITORY,
            "branch": BRANCH, "remote": "origin", "push_destination": self.destination}
        self.write(self.authorization_path, encoded(self.authorization))

    @contextmanager
    def pinned(self):
        original = copy.deepcopy(self.b.ROADMAP_PUBLICATION)
        with patch.object(self.b, "ROADMAP_PUBLICATION", self.pins), \
             patch.object(self.c, "_ROADMAP_BRIDGE", self.b):
            yield self
        self.case.assertEqual(original, self.b.ROADMAP_PUBLICATION)

    def check(self):
        return self.b.check(self.c, self.repo, self.spec_path)

    def publish(self):
        with self.c.lock(self.repo):
            return self.b.publish(self.c, self.repo, self.spec_path, self.authorization_path)

    def identities(self):
        return {"index": self.git("ls-files", "--stage", "-z") + self.git("ls-files", "-v", "-z"),
                "index_raw": self.read(".git/index"), "refs": self.git("show-ref"),
                "head": self.text("rev-parse", "HEAD"),
                "state": self.read(self.c.STATE), "production": self.read(PRODUCTION),
                "rbs": self.read(CANONICAL), "previous_journal": self.repo.journal("25").read_bytes(),
                "previous_receipt": self.read(self.parent_receipt),
                "rendering": self.read(".git/rendering-approval/PHASE_25.json"),
                "physics": self.read(".git/physics-approval/PHASE_25.json")}

    def journal_path(self):
        relative = self.pins["journal_namespace"] + "/" + self.pins["identity"] + ".json"
        raw = Path(self.text("rev-parse", "--git-path", relative))
        return raw if raw.is_absolute() else self.root / raw

    def journal(self):
        return json.loads(self.journal_path().read_bytes())

    def rebind(self):
        """Rebind test-generated inputs, for structural forgery tests only."""
        without_receipt = [row for row in self.manifest["files"] if row["path"] != self.receipt_path]
        payload = {"schema": 1, "identity": self.pins["identity"],
                   "files": without_receipt, "local_bindings": self.manifest["local_bindings"]}
        self.receipt["manifest_payload_sha256"] = sha(encoded(payload))
        self.write(self.receipt_path, encoded(self.receipt))
        for row in self.manifest["files"]:
            if row["path"] == self.receipt_path:
                row["sha256"] = sha(self.read(self.receipt_path))
        self.write(self.manifest_path, encoded(self.manifest))
        self.spec["manifest_sha256"] = sha(self.read(self.manifest_path))
        self.write(self.spec_path, encoded(self.spec))
        self.authorization["spec_sha256"] = sha(self.read(self.spec_path))
        self.write(self.authorization_path, encoded(self.authorization))

    def replace_accepted_source(self, data):
        self.write(self.source_path, data)
        self.write(self.snapshot_path, data)
        self.activation["artifact_hashes"][self.source_path] = sha(data)
        self.write(self.activation_path, encoded(self.activation))
        self.write(self.activation_snapshot, self.read(self.activation_path))
        self.pins["activation_sha256"] = sha(self.read(self.activation_path))
        for source in (self.source_path, self.activation_path):
            self.manifest["local_bindings"][source] = sha(self.read(source))
        for entry in self.manifest["files"]:
            if entry.get("source") in (self.source_path, self.activation_path):
                entry["sha256"] = sha(self.read(entry["source"]))
        self.rebind()


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        TEST_ROOT.mkdir(parents=True, exist_ok=True)
        cls.run_root = Path(tempfile.mkdtemp(prefix="offline-run-", dir=TEST_ROOT)).resolve()
        config = cls.run_root / "empty.gitconfig"
        config.write_bytes(b"")
        env = {key: value for key, value in os.environ.items() if not key.upper().startswith("GIT_")}
        env.update(GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(config), GIT_CONFIG_SYSTEM=str(config),
                   GIT_TERMINAL_PROMPT="0", GIT_ALLOW_PROTOCOL="file", GIT_ATTR_NOSYSTEM="1")
        cls.stack = ExitStack()
        cls.addClassCleanup(cls.stack.close)
        cls.stack.enter_context(patch.dict(os.environ, env, clear=True))

        def guarded_run(command, *args, **kwargs):
            if not isinstance(command, (list, tuple)) or command[0] != "git" or "-C" not in command:
                raise AssertionError("Offline suite permits only explicitly rooted Git subprocesses")
            target = Path(command[command.index("-C") + 1]).resolve()
            if not target.is_relative_to(cls.run_root):
                raise AssertionError("Attempted Git access outside disposable fixture root: " + str(target))
            if os.environ.get("GIT_ALLOW_PROTOCOL") != "file":
                raise AssertionError("Network transport must remain disabled")
            return REAL_RUN(command, *args, **kwargs)

        cls.stack.enter_context(patch.object(subprocess, "run", guarded_run))
        cls.tested_bytes = {"helper": OPTIONS.helper.read_bytes(), "bridge": OPTIONS.bridge.read_bytes(),
                            "tests": TEST_SOURCE_BYTES}
        cls.c = load_module(OPTIONS.helper, "offline_roadmap_checkpoint", cls.tested_bytes["helper"])
        cls.b = load_module(OPTIONS.bridge, "offline_roadmap_bridge", cls.tested_bytes["bridge"])
        cls.c.configure_workflow("pre-editor")
        print("Offline disposable repositories: " + str(cls.run_root), flush=True)

    def setUp(self):
        self.c.configure_workflow("pre-editor")

    def fixture(self, label):
        return Fixture(self, label)

    def blocked(self, callback, message=None):
        with self.assertRaises(self.c.Blocked) as captured:
            callback()
        if message:
            self.assertIn(message, str(captured.exception))
        return str(captured.exception)

    def assert_preserved(self, fixture, before, include_index=True):
        after = fixture.identities()
        keys = set(before) - {"head", "refs", "index", "index_raw"}
        if include_index:
            keys |= {"index", "index_raw"}
        for key in sorted(keys):
            self.assertEqual(before[key], after[key], key)

    def assert_complete(self, fixture, before):
        head = fixture.text("rev-parse", "HEAD")
        self.assertNotEqual(fixture.parent, head)
        self.assertEqual(fixture.parent, fixture.text("show", "-s", "--format=%P", head))
        self.assertEqual("1", fixture.text("rev-list", "--count", fixture.parent + "..HEAD"))
        self.assertEqual("tag", fixture.text("cat-file", "-t", "refs/tags/" + fixture.pins["tag"]))
        self.assertEqual(head, fixture.text("rev-parse", fixture.pins["tag"] + "^{commit}"))
        remote = fixture.text("ls-remote", "--", fixture.destination, "refs/heads/" + BRANCH,
                              "refs/tags/" + fixture.pins["tag"], "refs/tags/" + fixture.pins["tag"] + "^{}")
        remote_refs = dict(line.split("\t")[::-1] for line in remote.splitlines())
        self.assertEqual(head, remote_refs["refs/heads/" + BRANCH])
        self.assertEqual(head, remote_refs["refs/tags/" + fixture.pins["tag"] + "^{}"])
        self.assertEqual("COMPLETE", fixture.journal()["status"])
        expected = {row["path"] for row in fixture.manifest["files"]} | {
            fixture.spec_path, fixture.manifest_path}
        changed = set(fixture.git("diff-tree", "--no-commit-id", "--name-only", "-r", head).decode().splitlines())
        self.assertEqual(expected, changed)
        self.assertEqual(fixture.text("rev-parse", fixture.parent + ":GEngine"),
                         fixture.text("rev-parse", head + ":GEngine"))
        self.assertEqual(fixture.text("rev-parse", fixture.parent + ":RigidBodySimulation"),
                         fixture.text("rev-parse", head + ":RigidBodySimulation"))
        self.assertFalse(fixture.repo.staged())
        self.assert_preserved(fixture, before, include_index=False)

    def test_valid_bridge_check_is_read_only_and_publish_is_exact_and_idempotent(self):
        f = self.fixture("valid")
        with f.pinned():
            before = f.identities()
            result = f.check()
            self.assertIsInstance(result, dict)
            self.assertEqual(before, f.identities())
            self.assertFalse(f.journal_path().exists())
            f.publish()
            self.assert_complete(f, before)
            completed = f.identities()
            saved_journal = f.journal_path().read_bytes()
            f.publish()
            self.assertEqual(completed, f.identities())
            self.assertEqual(saved_journal, f.journal_path().read_bytes())

    def test_authorization_required_and_bound_to_exact_spec_and_destination(self):
        f = self.fixture("authorization")
        with f.pinned():
            for field, value in (("status", "IMPLEMENTATION_ONLY"), ("spec_sha256", "0" * 64),
                                 ("parent", f.earlier), ("identity", "other-operation"),
                                 ("tag", "pre-editor-phase-26-approved"), ("branch", "other"),
                                 ("repository", "other/repo"), ("remote", "elsewhere"),
                                 ("push_destination", "https://github.com/other/repo.git")):
                with self.subTest(field=field):
                    wrong = dict(f.authorization, **{field: value})
                    f.write(f.authorization_path, encoded(wrong))
                    before = f.identities()
                    self.blocked(f.publish)
                    self.assertEqual(before, f.identities())
                    self.assertFalse(f.journal_path().exists())
            f.write(f.authorization_path, encoded(f.authorization))
            original = f.read(f.authorization_path)
            (f.root / f.authorization_path).unlink()
            before = f.identities()
            self.blocked(f.publish)
            self.assertEqual(before, f.identities())
            self.assertFalse(f.journal_path().exists())
            f.write(f.authorization_path, original)

    def test_bound_files_cannot_be_missing_or_modified(self):
        f = self.fixture("bound-inputs")
        with f.pinned():
            paths = [f.spec_path, f.manifest_path, f.receipt_path, f.snapshot_path,
                     f.source_path, f.helper_path, f.parent_receipt]
            for rel in paths:
                original = f.read(rel)
                with self.subTest(path=rel, fault="modified"):
                    f.write(rel, original + b"\n")
                    before = f.identities()
                    self.blocked(f.publish)
                    self.assertEqual(before, f.identities())
                    self.assertFalse(f.journal_path().exists())
                f.write(rel, original)
            original = f.read(f.snapshot_path)
            (f.root / f.snapshot_path).unlink()
            self.blocked(f.publish)
            f.write(f.snapshot_path, original)

    def test_predecessor_tag_journal_receipt_and_head_are_exact(self):
        f = self.fixture("predecessor")
        with f.pinned():
            for field, value in (("parent", f.earlier), ("parent_tag_object", "0" * 40),
                                 ("parent_journal_sha256", "0" * 64),
                                 ("parent_receipt_sha256", "0" * 64)):
                with self.subTest(pin=field):
                    original = f.pins[field]
                    f.pins[field] = value
                    before = f.identities()
                    self.blocked(f.check)
                    self.assertEqual(before, f.identities())
                    f.pins[field] = original
            saved = f.repo.journal("25").read_bytes()
            bad = copy.deepcopy(f.previous_journal)
            bad["status"] = "PENDING"
            f.repo.journal("25").write_bytes(encoded(bad))
            self.blocked(f.check)
            f.repo.journal("25").write_bytes(saved)
            f.git("update-ref", "refs/tags/" + TAG25, f.parent, f.parent_tag_object)
            self.blocked(f.check)

    def test_manifest_cannot_smuggle_production_or_historical_paths(self):
        for path in (PRODUCTION, CANONICAL, "docs/rendering/REFACTOR_STATE.md",
                     "pre-editor-checkpoints/phase-25.json", "../escape", ".git/config"):
            with self.subTest(path=path):
                f = self.fixture("manifest-scope")
                with f.pinned():
                    f.manifest["files"].append({"path": path, "sha256": "0" * 64,
                                                 "mode": "100644", "role": "snapshot"})
                    f.rebind()
                    before = f.identities()
                    self.blocked(f.publish)
                    self.assertEqual(before, f.identities())
                    self.assertFalse(f.journal_path().exists())

    def test_ignored_public_destination_duplicate_entry_and_wrong_mode_reject(self):
        f = self.fixture("manifest-policy")
        with f.pinned():
            original_ignore = f.read(".gitignore")
            f.write(".gitignore", original_ignore + ("/" + f.snapshot_root + "/\n").encode())
            self.blocked(f.check)
            f.write(".gitignore", original_ignore)
            original_manifest = copy.deepcopy(f.manifest)
            f.manifest["files"].append(copy.deepcopy(f.manifest["files"][0]))
            f.rebind()
            self.blocked(f.check)
            f.manifest = copy.deepcopy(original_manifest)
            f.manifest["files"][0]["mode"] = "120000"
            f.rebind()
            self.blocked(f.check)

    def test_preexisting_staged_and_unmerged_work_block_without_index_changes(self):
        f = self.fixture("staged-owner")
        with f.pinned():
            f.write("owner.txt", b"owner staged work\n")
            f.git("add", "--", "owner.txt")
            f.write("owner.txt", b"owner additional unstaged work\n")
            before = f.identities()
            self.blocked(f.publish)
            self.assertEqual(before, f.identities())
            self.assertEqual(b"owner additional unstaged work\n", f.read("owner.txt"))
            self.assertFalse(f.journal_path().exists())
        g = self.fixture("unmerged-owner")
        with g.pinned():
            blob = g.text("rev-parse", g.parent + ":owner.txt")
            zero = "0" * len(blob)
            stages = f"0 {zero}\towner.txt\n" + "".join(
                f"100644 {blob} {stage}\towner.txt\n" for stage in (1, 2, 3))
            g.git("update-index", "--index-info", input=stages.encode())
            self.assertTrue(g.git("ls-files", "-u"))
            before = g.identities()
            self.blocked(g.publish)
            self.assertEqual(before, g.identities())
            self.assertFalse(g.journal_path().exists())

    def test_unrelated_unstaged_and_untracked_work_and_old_namespaces_are_preserved(self):
        f = self.fixture("owner-preservation")
        with f.pinned():
            f.write("owner.txt", b"owner unstaged work\n")
            f.write("owner-new.txt", b"owner untracked work\n")
            before = f.identities()
            f.publish()
            self.assert_complete(f, before)
            self.assertEqual(b"owner unstaged work\n", f.read("owner.txt"))
            self.assertEqual(b"owner untracked work\n", f.read("owner-new.txt"))
            self.assertEqual({"owner.txt", "owner-new.txt"}, f.repo.changed())

    def test_pending_and_arbitrary_same_parent_commits_cannot_admit_phase26(self):
        f = self.fixture("phase26-guard")
        with f.pinned():
            tree = f.text("rev-parse", f.parent + "^{tree}")
            impostor = f.text("commit-tree", tree, "-p", f.parent, "-m", "unapproved impostor")
            self.blocked(lambda: self.b.phase26_gate(self.c, f.repo, impostor, f.state))
            self.assertEqual(f.parent, f.text("rev-parse", "HEAD"))
            f.publish()
            head = f.text("rev-parse", "HEAD")
            self.b.phase26_gate(self.c, f.repo, head, f.state)
            self.c.baseline_gate(f.repo, "26", head, f.state)
            self.assertEqual({f.spec_path, f.manifest_path, f.receipt_path, f.authorization_path},
                             self.b.phase26_contract_inputs(self.c, f.repo))
            saved = f.journal_path().read_bytes()
            pending = f.journal()
            pending["status"] = "PENDING"
            f.journal_path().write_bytes(encoded(pending))
            self.blocked(lambda: self.b.phase26_gate(self.c, f.repo, head, f.state))
            f.journal_path().write_bytes(saved)
            original = f.read(f.receipt_path)
            f.write(f.receipt_path, original + b"tamper")
            self.blocked(lambda: self.b.phase26_gate(self.c, f.repo, head, f.state))
            f.write(f.receipt_path, original)
            authorization = f.read(f.authorization_path)
            f.write(f.authorization_path, authorization + b"\n")
            self.blocked(lambda: self.b.phase26_gate(self.c, f.repo, head, f.state))
            f.write(f.authorization_path, authorization)
            f.write("GEngine/include/GEngine/Physics/fixture.h", b"authorized future Phase26 candidate\n")
            self.b.phase26_gate(self.c, f.repo, head, f.state)

    def test_contract_routing_and_rendering_isolation(self):
        f = self.fixture("namespace-isolation")
        activation_path = "docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json"
        f.write(activation_path, encoded({"workflow": "pre-editor",
            "status": "ACTIVE_PROSPECTIVE_FOR_PHASES_26_58", "effective_phases": {"first": "26", "last": "58"},
            "roadmap_root": "docs/pre_editor/active/roadmap/MILESTONE_C_01"}))
        with f.pinned():
            for phase in ("00", "04", "17", "24", "25"):
                contract, required = self.c.phase_contract_inputs(f.repo, phase)
                self.assertEqual("docs/pre_editor/phases/PHASE_" + phase + ".md", contract)
                self.assertNotIn(activation_path, required)
            for phase in ("26", "30", "55", "58"):
                contract, required = self.c.phase_contract_inputs(f.repo, phase)
                self.assertEqual("docs/pre_editor/active/roadmap/MILESTONE_C_01/phases/PHASE_" + phase + ".md", contract)
                self.assertIn(activation_path, required)
            self.assertEqual("58", self.c.phase_id("58"))
            self.blocked(lambda: self.c.phase_id("59"))
            self.c.configure_workflow("rendering")
            before = f.git("show-ref")
            self.blocked(f.check)
            self.blocked(f.publish)
            self.assertEqual(before, f.git("show-ref"))
            contract, required = self.c.phase_contract_inputs(f.repo, "26")
            self.assertEqual("docs/rendering/phases/PHASE_26.md", contract)
            self.assertEqual("68", self.c.phase_id("68"))
            self.c.configure_workflow("pre-editor")

    def test_old_completed_phase25_finish_never_reopens_bridge_or_validation(self):
        f = self.fixture("completed-legacy")
        with f.pinned():
            f.git("remote", "set-url", "origin", DESTINATION)
            journal = dict(f.previous_journal, state=f.state, push_destination=DESTINATION)
            before = f.identities()
            with patch.object(self.c, "check", side_effect=AssertionError("Historical check reopened")), \
                 patch.object(self.c, "seal", side_effect=AssertionError("Historical seal reopened")), \
                 patch.object(self.b, "check", side_effect=AssertionError("Bridge check reopened")), \
                 patch.object(self.b, "phase26_gate", side_effect=AssertionError("Bridge gate reopened")):
                self.assertEqual("ALREADY_COMPLETE", self.c.finish(f.repo, journal)["status"])
            self.assertEqual(before, f.identities())

    def test_recovery_after_each_durable_journal_boundary(self):
        for boundary in ("PREPARED", "STAGED", "COMMIT_PENDING", "COMMITTED", "TAG_PENDING",
                         "TAGGED", "PUSH_PENDING", "COMPLETE"):
            with self.subTest(boundary=boundary):
                f = self.fixture("recover-" + boundary.lower())
                with f.pinned():
                    before = f.identities()
                    original_atomic = self.c.atomic
                    fired = []
                    journal_path = f.journal_path().resolve()

                    def interrupt_after_save(path, data):
                        result = original_atomic(path, data)
                        if Path(path).resolve() == journal_path and json.loads(data).get("status") == boundary and not fired:
                            fired.append(boundary)
                            raise Interrupted(boundary)
                        return result

                    with patch.object(self.c, "atomic", interrupt_after_save):
                        with self.assertRaises(Interrupted):
                            f.publish()
                    self.assertEqual([boundary], fired)
                    f.publish()
                    self.assert_complete(f, before)

    def test_recovery_after_commit_tag_and_push_before_journal_update(self):
        for command in ("commit", "tag", "push"):
            with self.subTest(command=command):
                f = self.fixture("recover-git-" + command)
                with f.pinned():
                    before = f.identities()
                    original_git = f.repo.git
                    fired = []

                    def interrupt_after_git(*args, **kwargs):
                        result = original_git(*args, **kwargs)
                        if command in args and not fired:
                            fired.append(command)
                            raise Interrupted(command)
                        return result

                    with patch.object(f.repo, "git", interrupt_after_git):
                        with self.assertRaises(Interrupted):
                            f.publish()
                    self.assertEqual([command], fired)
                    f.publish()
                    self.assert_complete(f, before)

    def test_commit_hook_failure_is_not_bypassed_and_retry_preserves_checkpoint(self):
        f = self.fixture("commit-hook")
        with f.pinned():
            f.write(".git/hooks/pre-commit", b"#!/bin/sh\nexit 71\n")
            (f.root / ".git/hooks/pre-commit").chmod(0o755)
            before = f.identities()
            self.blocked(f.publish)
            self.assertEqual(f.parent, f.text("rev-parse", "HEAD"))
            self.assert_preserved(f, before, include_index=False)
            (f.root / ".git/hooks/pre-commit").unlink()
            f.publish()
            self.assert_complete(f, before)

    def test_existing_tag_and_stale_remote_head_block_without_publication(self):
        f = self.fixture("tag-conflict")
        with f.pinned():
            f.git("tag", f.pins["tag"], f.parent)
            before = f.identities()
            self.blocked(f.publish)
            self.assertEqual(before, f.identities())
            self.assertFalse(f.journal_path().exists())
        g = self.fixture("stale-remote")
        with g.pinned():
            tree = g.text("rev-parse", g.parent + "^{tree}")
            competing = g.text("commit-tree", tree, "-p", g.parent, "-m", "other publisher")
            g.git("push", "origin", competing + ":refs/heads/" + BRANCH)
            before = g.identities()
            self.blocked(g.publish)
            self.assertEqual(before, g.identities())
            self.assertFalse(g.journal_path().exists())

    def test_operation_lock_blocks_a_concurrent_publication(self):
        f = self.fixture("operation-lock")
        with f.pinned():
            before = f.identities()
            with self.c.lock(f.repo):
                # Windows may reject the existing lock-byte read before the
                # inherited helper translates lock acquisition to Blocked.
                with self.assertRaises((self.c.Blocked, PermissionError)):
                    f.publish()
            self.assertEqual(before, f.identities())
            self.assertFalse(f.journal_path().exists())

    def test_pending_recovery_preserves_concurrent_index_and_head_changes(self):
        for fault in ("index", "head"):
            with self.subTest(fault=fault):
                f = self.fixture("pending-concurrent-" + fault)
                with f.pinned():
                    original_atomic = self.c.atomic
                    journal_path = f.journal_path().resolve()

                    def interrupt_prepared(path, data):
                        result = original_atomic(path, data)
                        if Path(path).resolve() == journal_path and json.loads(data).get("status") == "PREPARED":
                            raise Interrupted("PREPARED")
                        return result

                    with patch.object(self.c, "atomic", interrupt_prepared):
                        with self.assertRaises(Interrupted):
                            f.publish()
                    if fault == "index":
                        f.write("owner.txt", b"concurrent staged owner work\n")
                        f.git("add", "--", "owner.txt")
                    else:
                        tree = f.text("rev-parse", f.parent + "^{tree}")
                        other = f.text("commit-tree", tree, "-p", f.parent, "-m", "concurrent owner commit")
                        f.git("update-ref", "refs/heads/" + BRANCH, other, f.parent)
                    before = f.identities()
                    journal = f.journal_path().read_bytes()
                    self.blocked(f.publish)
                    self.assertEqual(before, f.identities())
                    self.assertEqual(journal, f.journal_path().read_bytes())

    def test_concurrent_remote_update_rejects_atomic_push_and_keeps_pending_evidence(self):
        f = self.fixture("remote-race")
        with f.pinned():
            before = f.identities()
            tree = f.text("rev-parse", f.parent + "^{tree}")
            competing = f.text("commit-tree", tree, "-p", f.parent, "-m", "concurrent remote owner")
            f.git("push", "origin", competing + ":refs/heads/fixture-concurrent")
            original_git = f.repo.git
            raced = []

            def race_before_push(*args, **kwargs):
                if "push" in args and not raced:
                    raced.append(True)
                    f.git("update-ref", "refs/heads/" + BRANCH, competing, f.parent, at=f.origin)
                return original_git(*args, **kwargs)

            with patch.object(f.repo, "git", race_before_push):
                self.blocked(f.publish)
            self.assertEqual([True], raced)
            self.assertEqual("PUSH_PENDING", f.journal()["status"])
            self.assertEqual(competing, f.text("rev-parse", "refs/heads/" + BRANCH, at=f.origin))
            self.assertNotEqual(0, f.git("show-ref", "--verify", "refs/tags/" + f.pins["tag"],
                                        at=f.origin, allow_failure=True).returncode)
            pending = f.identities()
            saved = f.journal_path().read_bytes()
            self.blocked(f.publish)
            self.assertEqual(pending, f.identities())
            self.assertEqual(saved, f.journal_path().read_bytes())
            self.assert_preserved(f, before, include_index=False)

    def test_physics_changes_and_worktree_or_destination_mismatch_fail_closed(self):
        f = self.fixture("identity-physics")
        with f.pinned():
            physics = "GEngine/include/GEngine/Physics/fixture.h"
            original = f.read(physics)
            f.write(physics, b"unapproved Physics behavior\n")
            before = f.identities()
            self.blocked(f.check)
            self.assertEqual(before, f.identities())
            f.write(physics, original)
            for field, value in (("worktree", f.directory.as_posix()), ("branch", "other"),
                                 ("push_destination", (f.directory / "wrong.git").as_posix())):
                with self.subTest(field=field):
                    original = f.pins[field]
                    f.pins[field] = value
                    before = f.identities()
                    self.blocked(f.check)
                    self.assertEqual(before, f.identities())
                    f.pins[field] = original

    def test_active_phase_journal_prevents_governance_transaction(self):
        f = self.fixture("active-phase")
        with f.pinned():
            f.repo.save({"phase": "26", "status": "SEALED", "transaction": None})
            before = f.identities()
            phase26_bytes = f.repo.journal("26").read_bytes()
            self.blocked(f.publish)
            self.assertEqual(before, f.identities())
            self.assertEqual(phase26_bytes, f.repo.journal("26").read_bytes())
            self.assertFalse(f.journal_path().exists())

    def test_unmodified_checkpoint_primitives_match_the_preserved_preimage(self):
        before = WORKSPACE / "logs/pre_editor/governance/milestone-c-publication-bridge-01/before/.codex/tools/rendering_checkpoint.py"
        old = ast.parse(before.read_bytes())
        new = ast.parse(self.tested_bytes["helper"])
        old_defs = {node.name: node for node in old.body if isinstance(node, (ast.FunctionDef, ast.ClassDef))}
        new_defs = {node.name: node for node in new.body if isinstance(node, (ast.FunctionDef, ast.ClassDef))}
        changed_routes = {"baseline_gate", "phase_contract_inputs", "seal", "check", "main"}
        for name in sorted(set(old_defs) - changed_routes):
            with self.subTest(primitive=name):
                self.assertEqual(ast.dump(old_defs[name], include_attributes=False),
                                 ast.dump(new_defs[name], include_attributes=False))

    def test_missing_accepted_artifact_or_required_helper_cannot_be_reauthorized_away(self):
        f = self.fixture("required-closure")
        with f.pinned():
            original_manifest = copy.deepcopy(f.manifest)
            for missing in (f.snapshot_path, f.helper_path, f.activation_snapshot):
                with self.subTest(missing=missing):
                    f.manifest = copy.deepcopy(original_manifest)
                    f.manifest["files"] = [row for row in f.manifest["files"] if row["path"] != missing]
                    f.rebind()
                    before = f.identities()
                    self.blocked(f.publish)
                    self.assertEqual(before, f.identities())
                    self.assertFalse(f.journal_path().exists())

    def test_autocrlf_preserves_raw_archived_bytes_with_bounded_attributes(self):
        f = self.fixture("autocrlf-archive")
        with f.pinned():
            f.git("config", "core.autocrlf", "true")
            raw = b"# PRE_EDITOR Phase 26 fixture\r\nAccepted exact CRLF provenance.\r\n"
            f.replace_accepted_source(raw)
            attributes = f.snapshot_root + "/.gitattributes"
            f.write(attributes, b"* -text\n")
            f.manifest["files"].append({"path": attributes, "sha256": sha(f.read(attributes)),
                                         "mode": "100644", "role": "snapshot"})
            f.rebind()
            before = f.identities()
            f.publish()
            self.assert_complete(f, before)
            self.assertEqual(raw, f.git("show", "HEAD:" + f.snapshot_path))
            self.assertEqual(raw, f.read(f.source_path))

    def test_hooks_cannot_mutate_predecessor_provenance_and_still_complete(self):
        for hook in ("pre-commit", "pre-push"):
            with self.subTest(hook=hook):
                f = self.fixture("predecessor-" + hook)
                with f.pinned():
                    old_journal = f.repo.journal("25").read_bytes()
                    old_receipt = f.read(f.parent_receipt)
                    old_tag = f.text("rev-parse", "refs/tags/" + TAG25)
                    hook_path = ".git/hooks/" + hook
                    f.write(hook_path, b"#!/bin/sh\nprintf '\\n' >> .git/pre-editor-approval/PHASE_25.json\n")
                    (f.root / hook_path).chmod(0o755)
                    self.blocked(f.publish)
                    self.assertNotEqual("COMPLETE", f.journal()["status"])
                    self.assertEqual(old_journal + b"\n", f.repo.journal("25").read_bytes())
                    self.assertEqual(old_receipt, f.read(f.parent_receipt))
                    self.assertEqual(old_tag, f.text("rev-parse", "refs/tags/" + TAG25))
                    if hook == "pre-commit":
                        self.assertEqual(f.parent, f.text("rev-parse", "refs/heads/" + BRANCH, at=f.origin))
                    saved = f.journal_path().read_bytes()
                    self.blocked(f.publish)
                    self.assertEqual(saved, f.journal_path().read_bytes())

    def test_predecessor_tamper_after_tag_creation_blocks_the_push(self):
        f = self.fixture("tag-boundary-predecessor")
        with f.pinned():
            old_journal = f.repo.journal("25").read_bytes()
            original_git = f.repo.git
            pushed = []

            def mutate_after_tag(*args, **kwargs):
                if "push" in args:
                    pushed.append(args)
                result = original_git(*args, **kwargs)
                if args[0] == "tag":
                    f.repo.journal("25").write_bytes(old_journal + b"\n")
                return result

            with patch.object(f.repo, "git", mutate_after_tag):
                self.blocked(f.publish)
            self.assertFalse(pushed)
            self.assertNotEqual("COMPLETE", f.journal()["status"])
            self.assertEqual(f.parent, f.text("rev-parse", "refs/heads/" + BRANCH, at=f.origin))
            self.assertEqual(old_journal + b"\n", f.repo.journal("25").read_bytes())


if __name__ == "__main__":
    program = unittest.main(argv=[sys.argv[0], *UNITTEST_ARGS], verbosity=2, exit=False)
    result = program.result
    if hasattr(BridgeTests, "run_root"):
        (BridgeTests.run_root / "result.json").write_bytes(encoded({
            "schema": 1, "offline": True, "git_protocols": ["file"],
            "test_count": result.testsRun, "successful": result.wasSuccessful(),
            "failures": [{"test": str(test), "traceback": trace} for test, trace in result.failures],
            "errors": [{"test": str(test), "traceback": trace} for test, trace in result.errors],
            "helper_sha256": sha(BridgeTests.tested_bytes["helper"]),
            "bridge_sha256": sha(BridgeTests.tested_bytes["bridge"]),
            "tests_sha256": sha(BridgeTests.tested_bytes["tests"]),
            "current_inputs_match_tested": {
                "helper": OPTIONS.helper.read_bytes() == BridgeTests.tested_bytes["helper"],
                "bridge": OPTIONS.bridge.read_bytes() == BridgeTests.tested_bytes["bridge"],
                "tests": Path(__file__).read_bytes() == BridgeTests.tested_bytes["tests"],
            },
        }))
    sys.exit(0 if result.wasSuccessful() else 1)
