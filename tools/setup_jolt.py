"""Fetch/verify the pinned SDK; reproduce its declared extension without writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EXTENSION_ID = "gengine-contact-resistance-01"
EXTENSION_FILES = frozenset((
    "Jolt/Physics/Collision/ContactListener.h",
    "Jolt/Physics/Constraints/ContactConstraintManager.h",
    "Jolt/Physics/Constraints/ContactConstraintManager.cpp",
    "Jolt/Physics/PhysicsSystem.h",
    "Jolt/Physics/PhysicsSystem.cpp",
    "Jolt/Physics/EPhysicsUpdateError.h",
    "Jolt/Physics/Constraints/ConstraintPart/ContactResistanceConstraintPart.h",
))


class VerificationError(RuntimeError):
    """The checkout is not the exact selected source configuration."""


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def local_path(root, name):
    require(isinstance(name, str) and "\\" not in name, "Invalid source path")
    relative = PurePosixPath(name)
    require(name and not relative.is_absolute() and ".." not in relative.parts,
            "Source path escapes its root")
    path = root.joinpath(*relative.parts)
    require(path.resolve().is_relative_to(root.resolve()), "Resolved path escapes its root")
    for part in (path, *path.parents):
        if part == root:
            break
        require(not part.is_symlink(), "Symbolic source path is not admitted")
        if part.exists():
            require(not (getattr(part.lstat(), "st_file_attributes", 0)
                         & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)),
                    "Reparse source path is not admitted")
    return path


def git(dest, *args):
    environment = os.environ.copy()
    environment.update(GIT_OPTIONAL_LOCKS="0", GIT_NO_LAZY_FETCH="1", GIT_NO_REPLACE_OBJECTS="1")
    return subprocess.check_output(
        ["git", "-c", "safe.directory=" + dest.as_posix(), "-C", str(dest), *args],
        env=environment)


def names(raw):
    return {name.decode("utf-8") for name in raw.split(b"\0") if name}


def checkout_bytes(canonical, ending):
    require(b"\r" not in canonical, "Canonical patch source must use LF line endings")
    require(ending in ("LF", "CRLF"), "Unsupported checkout newline policy")
    return canonical.replace(b"\n", b"\r\n") if ending == "CRLF" else canonical


def apply_text_patch(patch, originals):
    """Reproduce this versioned text-only patch against upstream blobs in memory."""
    require(b"\r" not in patch, "Patch must have canonical LF line endings")
    lines = patch.splitlines(keepends=True)
    result = {}
    index = 0
    hunk_header = re.compile(rb"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(?:[^\n]*)\n")
    while index < len(lines):
        header = lines[index]
        require(header.startswith(b"diff --git a/"), "Unexpected patch section")
        parts = header.rstrip(b"\n").split(b" ")
        require(len(parts) == 4 and parts[2][2:] == parts[3][2:], "Patch rename/path mismatch")
        name = parts[2][2:].decode("utf-8")
        require(name in originals and name not in result, "Unexpected or duplicate patch path")
        original = originals[name]
        index += 1
        metadata = []
        while index < len(lines) and not lines[index].startswith(b"--- "):
            require(lines[index].startswith((b"index ", b"new file mode ")), "Unsupported patch metadata")
            metadata.append(lines[index])
            index += 1
        expected_old = b"/dev/null" if original is None else b"a/" + name.encode("utf-8")
        require(index + 1 < len(lines) and lines[index] == b"--- " + expected_old + b"\n"
                and lines[index + 1] == b"+++ b/" + name.encode("utf-8") + b"\n",
                "Patch file header mismatch")
        require((b"new file mode 100644\n" in metadata) == (original is None), "Patch mode mismatch")
        index += 2
        source = (original or b"").splitlines(keepends=True)
        output = []
        cursor = 0
        hunks = 0
        while index < len(lines) and not lines[index].startswith(b"diff --git "):
            match = hunk_header.fullmatch(lines[index])
            require(match is not None, "Malformed patch hunk")
            old_start, old_count, new_start, new_count = (
                int(match[1]), int(match[2]) if match[2] is not None else 1,
                int(match[3]), int(match[4]) if match[4] is not None else 1)
            old_position = old_start - 1 if old_count else old_start
            require(cursor <= old_position <= len(source), "Patch hunk position mismatch")
            output.extend(source[cursor:old_position])
            cursor = old_position
            require(len(output) == (new_start - 1 if new_count else new_start), "Patch output position mismatch")
            index += 1
            operations = []
            while index < len(lines) and not lines[index].startswith((b"@@ ", b"diff --git ")):
                line = lines[index]
                if line == b"\\ No newline at end of file\n":
                    require(operations and operations[-1].endswith(b"\n"), "Invalid missing-newline marker")
                    operations[-1] = operations[-1][:-1]
                else:
                    require(line[:1] in (b" ", b"+", b"-"), "Invalid patch operation")
                    operations.append(line)
                index += 1
            consumed = produced = 0
            for line in operations:
                operation, payload = line[:1], line[1:]
                if operation in (b" ", b"-"):
                    require(cursor < len(source) and source[cursor] == payload, "Patch does not match upstream source")
                    cursor += 1
                    consumed += 1
                if operation in (b" ", b"+"):
                    output.append(payload)
                    produced += 1
            require(consumed == old_count and produced == new_count, "Patch hunk length mismatch")
            hunks += 1
        require(hunks > 0, "Patch section has no content change")
        output.extend(source[cursor:])
        result[name] = b"".join(output)
    require(set(result) == set(originals), "Patch membership mismatch")
    return result


def verify(root=ROOT, profile="configured"):
    require(profile in ("configured", "pristine"), "Unknown SDK verification profile")
    pin = read_json(root / "external/jolt-pin.json")
    dest = local_path(root, pin["source"])
    require(dest.is_dir(), "Pinned Jolt source is missing; run tools/setup_jolt.py")
    require(git(dest, "rev-parse", "HEAD").decode().strip() == pin["commit"], "Jolt upstream commit differs from pin")
    require(not git(dest, "diff", "--cached", "--name-only", "HEAD", "-z"), "Staged SDK changes are not admitted")
    changed = names(git(dest, "diff", "--no-ext-diff", "--name-only", "HEAD", "-z"))
    untracked = names(git(dest, "ls-files", "--others", "--exclude-standard", "-z"))
    require((dest / "Jolt/Jolt.cmake").is_file(), "Pinned Jolt source is not populated")
    extension = pin.get("gengine_extension") if profile == "configured" else None
    if extension is None:
        require(not changed and not untracked, "Jolt checkout differs from pristine pin; preserve it and resolve manually")
        return "Jolt pin verified: " + pin["commit"]

    require(extension["id"] == EXTENSION_ID, "Unsupported SDK extension")
    manifest_bytes = local_path(root, extension["manifest"]).read_bytes()
    require(digest(manifest_bytes) == extension["manifest_sha256"], "SDK extension manifest hash mismatch")
    manifest = json.loads(manifest_bytes)
    require(manifest["schema"] == 1 and manifest["id"] == EXTENSION_ID
            and manifest["upstream_commit"] == pin["commit"], "SDK extension version mismatch")
    records = manifest["files"]
    require(len(records) == len(EXTENSION_FILES) and {item["path"] for item in records} == EXTENSION_FILES,
            "SDK extension source boundary mismatch")
    existing = {item["path"] for item in records if item["base"] is not None}
    require(changed == existing and untracked == EXTENSION_FILES - existing, "SDK extension changed-path membership mismatch")
    patch = local_path(root, manifest["patch"]).read_bytes()
    require(digest(patch) == manifest["patch_sha256"], "SDK extension patch hash mismatch")
    originals = {}
    for item in records:
        name, base, target = item["path"], item["base"], item["result"]
        current = local_path(dest, name)
        require(current.is_file(), "SDK extension source missing: " + name)
        require(target["mode"] == "100644", "Unsupported extension file mode")
        require(digest(current.read_bytes()) == target["sha256"], "SDK extension result hash mismatch: " + name)
        tree = git(dest, "ls-tree", "HEAD", "--", name).decode().strip()
        if base is None:
            require(not tree, "New extension source already exists upstream")
            originals[name] = None
        else:
            require(tree.startswith(base["mode"] + " blob ") and tree.endswith("\t" + name)
                    and base["mode"] == target["mode"], "SDK extension base/mode mismatch: " + name)
            canonical = git(dest, "show", "HEAD:" + name)
            require(digest(canonical) == base["canonical_sha256"], "SDK extension upstream blob mismatch: " + name)
            require(digest(checkout_bytes(canonical, base["line_endings"])) == base["sha256"],
                    "SDK extension original checkout hash mismatch: " + name)
            originals[name] = canonical
    reconstructed = apply_text_patch(patch, originals)
    for item in records:
        name, target = item["path"], item["result"]
        canonical = reconstructed[name]
        require(digest(canonical) == target["canonical_sha256"]
                and digest(checkout_bytes(canonical, target["line_endings"])) == target["sha256"],
                "SDK extension patch reproduction mismatch: " + name)
    return "Jolt pin verified: " + pin["commit"] + " + " + EXTENSION_ID + " " + manifest["patch_sha256"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Read-only verification; never fetch or apply an extension")
    parser.add_argument("--profile", choices=("configured", "pristine"), default="configured")
    args = parser.parse_args()
    try:
        pin = read_json(ROOT / "external/jolt-pin.json")
        dest = local_path(ROOT, pin["source"])
        if not dest.exists() and not args.verify_only:
            subprocess.run(["git", "clone", "--depth", "1", "--branch", "v" + pin["version"],
                            "--filter=blob:none", "--sparse", pin["repository"], str(dest)], check=True)
            subprocess.run(["git", "-C", str(dest), "sparse-checkout", "set", "Jolt", "Build", "Docs"], check=True)
        print(verify(ROOT, args.profile))
        return 0
    except (VerificationError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print("Jolt verification failed: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
