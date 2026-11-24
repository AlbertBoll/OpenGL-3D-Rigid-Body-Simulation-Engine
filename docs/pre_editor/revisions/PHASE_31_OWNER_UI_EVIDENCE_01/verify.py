"""Verify the single Phase 31 Editor evidence disposition using retained records."""

import argparse
import hashlib
import json
import runpy
from collections import Counter
from pathlib import Path


PACKAGE = Path(__file__).resolve().parent
ROOT = PACKAGE.parents[3]
RELEASE = "9d5d56a6657cdf0bd96faf8d6854d139fc4f099443e5c67a78676253ca1b1e53"
V01 = "docs/pre_editor/active/EDITOR_COMPONENT_EXPOSURE_AUTHORING_01/verify.py"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def verify(spec=None):
    activation = json.loads((PACKAGE / "ACTIVATION.json").read_text(encoding="utf-8"))
    refs = activation["records"]
    require(activation["phase"] == "31" and activation["candidate"] == "gizmo-keyboard-01"
            and activation["scope"] == ["D_EDITOR", "E_INTERACTION"], "Unexpected amendment scope")
    require(refs["v01"]["path"] == V01 and
            hashlib.sha256((ROOT / V01).read_bytes()).hexdigest() == refs["v01"]["sha256"],
            "V01 verifier identity mismatch")
    checker = runpy.run_path(str(ROOT / V01))["Verifier"](ROOT)
    policy = checker.discover("31")
    for ref in refs.values():
        checker.reference(ref)
    for path in ("ACTIVATION.json", "AMENDMENT.md", "component-dispositions.json", "verify.py"):
        name = (PACKAGE / path).relative_to(ROOT).as_posix()
        checker.bind(name)
        checker.contract_records.add(name)

    def record(key):
        ref = refs[key]
        return checker.load(ref["path"], ref["sha256"])

    assessment = refs["assessment"]["path"]
    data, inventory = checker.assessment(assessment, "31")
    checker.matrix(refs["matrix"]["path"], assessment, data, inventory, policy, "31", ready=False)
    candidate, owner = record("candidate"), record("owner")
    require(candidate["installed_release"] == owner["last_handoff_executable"] and
            candidate["installed_release"]["sha256"] == RELEASE and
            candidate["candidate"] == activation["candidate"] and owner["F_OWNER"] == "PASS" and
            owner["candidate_binding"] == refs["candidate"] and
            record("build")["executable"] == candidate["installed_release"], "Candidate or Owner mismatch")
    checker.reference(candidate["installed_release"])
    for member in candidate["matched_members"]:
        checker.bind(member["path"], member["sha256"])

    original = {row["row_id"]: row for row in record("previous_matrix")["rows"]}
    rows = {row["row_id"]: row for row in record("matrix")["rows"]}
    previous_ready = record("previous_ready")
    require(previous_ready["result"] == "PASS" and previous_ready["stage"] == "ready" and
            previous_ready["validation_input_hashes"][refs["previous_matrix"]["path"]] ==
            refs["previous_matrix"]["sha256"], "Previous verified matrix mismatch")
    counts = Counter(row["status"] for row in rows.values())
    require(len(original) == 485 and set(original) <= set(rows) and
            counts == {"PASS": 323, "NOT_APPLICABLE": 37, "PENDING": 158}, "Frozen population mismatch")
    pending = {key for key, row in rows.items() if row["status"] == "PENDING"}
    for key in rows.keys() - pending:
        require(rows[key] == original[key], f"Historical row changed: {key}")

    overlay = record("dispositions")
    require(overlay["row_disposition"] == "OWNER_ACCEPTED_UI" and
            overlay["new_automated_ui_observations"] == 0, "Invalid UI evidence method")
    closed = set()
    for group in overlay["groups"].values():
        for key in group["technical_evidence"]:
            require(key in refs, f"Missing technical basis: {key}")
        for kind in ("previous_PASS", "added"):
            for key in group[kind]:
                require(key in pending and key not in closed, f"Duplicate or ineligible row: {key}")
                require((kind == "added" and key not in original) or
                        (kind == "previous_PASS" and original.get(key, {}).get("status") == "PASS"),
                        f"Incorrect historical disposition: {key}")
                closed.add(key)
    require(closed == pending and len(closed.intersection(original)) == 125, "Incomplete UI reconciliation")

    for key in overlay["source_and_math_checks"]:
        checks = record(key)["checks"]
        require(checks and all(value is True for value in checks.values()), f"Failed source check: {key}")
    gates = record("technical_gates")["gate_dispositions"]
    require(gates["B_SAFETY"]["Native_experimental_production_contact"]["actual_outcome"] == "FAIL" and
            gates["C_COST"]["Native_experimental_incremental_heap"]["actual_outcome"] == "INCOMPLETE",
            "Native scope disposition changed")
    deferred = {f"s{replay}_{name}" for replay in (1, 2)
                for name in ("peak_contact", "contact_recovery", "zero_steady_cpp_or_attributed")}
    for backend, ref in gates["A_PHYSICS"]["assessments"].items():
        result = checker.load(*checker.reference(ref))
        failed = {name for name, check in result["checks"].items() if check["pass"] is not True}
        require(failed == (deferred if backend == "native" else set()) and
                result["status"] == ("FAIL" if backend == "native" else "PASS"),
                f"Unexpected technical qualification result: {backend}")
    if spec:
        checker.spec(spec, "31")
    return {"phase": "31", "candidate": RELEASE, "result": "PASS",
            "inventory": len(rows), "retained_PASS": counts["PASS"], "NOT_APPLICABLE": counts["NOT_APPLICABLE"],
            "OWNER_ACCEPTED_UI": len(closed), "previously_passing": len(closed.intersection(original)),
            "added": len(closed.difference(original)), "new_automated_ui_observations": 0,
            "D_EDITOR": "PASS_UNDER_AMENDED_METHOD", "E_INTERACTION": "PASS_UNDER_AMENDED_METHOD",
            "F_OWNER": owner["F_OWNER"], "spec_bindings_checked": bool(spec)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec", help="Check the final PRE_EDITOR checkpoint input bindings")
    print(json.dumps(verify(parser.parse_args().spec), indent=2))
