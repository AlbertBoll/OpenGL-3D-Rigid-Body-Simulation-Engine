#!/usr/bin/env python3
"""Seal reviewed evidence and finish/resume exact rendering Git checkpoints.

Python 3.10+, standard library only. Never runs builds/tests or bypasses hooks.
"""
import argparse
import base64
from contextlib import contextmanager
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import sys
from types import SimpleNamespace

STATE = 'docs/rendering/REFACTOR_STATE.md'
FIELDS = ('Current approved phase', 'Current approved tag', 'Next expected phase', 'Last approved review')

# Owner-authorized local handoff; not a general ancestor-baseline option.
PHASE02_HANDOFF = {
    'phase': '02',
    'previous_tag': 'render-refactor-phase-01-approved',
    'previous_commit': '0fe193ab029acc159fe6dfe107ea7bea52a5dc1e',
    'previous_tag_object': '0687644ee0a838a67df4d02c2969bac5fd766297',
    'baseline': '9b625e5db9aa54dc86e770a916c6b7739d493438',
    'authorization': 'docs/rendering/phases/PHASE_02.md',
}


# Exact Owner-accepted Phase25 bridge; never a general ancestor admission.
PHASE25_HANDOFF = {'baseline': '871e065c53cb7364d2e8091dfff7e0080db14869',
 'baseline_tree': '48132991293d5a840089236c2b177195f74bf872',
 'branch': 'pre-editor/refactor',
 'collider_acceptance': 'logs/pre_editor/collider-c04-r05-closure-20261102-01/OWNER_FINAL_ACCEPTANCE.json',
 'collider_acceptance_sha256': '1e3990e53981b87224cdac90fe772961e893dcb517a6d14c6e5f45e042085653',
 'integration_identity': 'logs/pre_editor/collider-c04-r05-closure-20261102-01/INTEGRATED_51_FILES.json',
 'integration_identity_sha256': '746689c156b73f62c228571ec5cf3f8340fe11f42e2a8eaaa677efbc7b337bd9',
 'integration_tag': 'collider-c04-r05-integrated-owner-accepted',
 'integration_tag_object': 'd8dac4e04891e24fbdd715fcd545d003aad657ce',
 'phase': '25',
 'previous_commit': '118e4e1e544ef2e26ec5930cd9c7f56a8cf6b0d3',
 'previous_journal_sha256': '2d22af2d19adcc3866fda785ec2f8822b75e899cfac62daec7f85d7c0149e517',
 'previous_phase': '24',
 'previous_receipt': 'pre-editor-checkpoints/phase-24.json',
 'previous_receipt_sha256': 'c181d6970c35042768b91a4e8cecaf348be43d45b6dde1f9a8da99ee25d2721c',
 'previous_tag': 'pre-editor-phase-24-approved',
 'previous_tag_object': 'b84e3ee2dfef7b4c60f7d5ddfa749dbaa4e1ec0f',
 'push_destination': 'https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git',
 'remote': 'origin',
 'workflow': 'pre-editor',
 'worktree': 'C:/dev/GEngine-pre-editor',
 'authorization': 'docs/pre_editor/active/PHASE_25_PUBLICATION_GUARD_AUTHORIZATION_01.json',
 'authorization_sha256': '86122ea13fa07ff07031d26c251a2df3475d79baeb2963ff957f9d1a90b13fd5',
 'recovery_copy': 'docs/pre_editor/revisions/PHASE_25_PUBLICATION_GUARD_01/authorization.json',
 'proposal_bindings': {'docs/pre_editor/proposals/PHASE_25_FINAL_PUBLICATION_GUARD_RESOLUTION_01/PROPOSAL.md': '86be0875480671a82c530c7781a166876dd9cccd42e5671b31706c8e8ef9fc0e',
                       'docs/pre_editor/proposals/PHASE_25_FINAL_PUBLICATION_GUARD_RESOLUTION_01/audit-closure.json': 'c47bf18460d0531995d8615866b2cc6d45e02a0b8eaf21e747b511a9e0f0e6d9',
                       'docs/pre_editor/proposals/PHASE_25_FINAL_PUBLICATION_GUARD_RESOLUTION_01/path-classification.json': '023ffd91e9d5215094651a19a706c2c7fe437ed7b6e0779665b4eb99b6ca7ca5',
                       'docs/pre_editor/proposals/PHASE_25_FINAL_PUBLICATION_GUARD_RESOLUTION_01/provenance.json': 'fe00001cdf347c272d34ac2743c0be5fa9cec593e0cb5e0327b0741fc9c9fc58'}}


# Canonical rendering-wide policy, shared by seal, receipt checks and governance
# validation. No per-phase opt-in; approved historical receipts are not rewritten.
OWNER_MUTABLE_PATHS = frozenset({'RigidBodySimulation/src/RigidBodySimulation.cpp'})

# Project-owned standing consent, separate from Git discovery and reusable skills.
PUBLICATION_POLICY = 'docs/rendering/PUBLICATION_POLICY.json'
WORKFLOW = 'rendering'
DOC_ROOT = 'docs/rendering'
VALIDATION_POLICY = 'docs/rendering/VALIDATION_POLICY.md'
CHECKPOINT_DIR = 'rendering-checkpoints'
JOURNAL_NAMESPACE = 'rendering-approval'
SEAL_NAMESPACE = 'rendering-seals'
TAG_PREFIX = 'render-refactor-phase-'
COMMIT_PREFIX = 'render-refactor'
PHASE_LIMIT = 68
PRE_EDITOR_BASELINE = '9995074e7af161db76c5bf79a5ede030702fb4ca'
PRE_EDITOR_BASE_TAG = 'render-refactor-phase-68-approved'
PRE_EDITOR_BASE_TAG_OBJECT = '48dbfd3430413492120eb551e761028b94c6f9c6'
PRE_EDITOR_PACKAGE_SHA256 = '5358f168c2b48252d1534d953abdf2d43fbfa7283b14f6a2c3c1accf021d4f0b'


def configure_workflow(workflow):
    # Explicit namespace selection; the original Rendering defaults stay intact.
    global WORKFLOW, DOC_ROOT, STATE, VALIDATION_POLICY, PUBLICATION_POLICY
    global CHECKPOINT_DIR, JOURNAL_NAMESPACE, SEAL_NAMESPACE, TAG_PREFIX, COMMIT_PREFIX, PHASE_LIMIT
    require(workflow in ('rendering', 'pre-editor'), 'Unknown workflow')
    WORKFLOW = workflow
    pre = workflow == 'pre-editor'
    DOC_ROOT = 'docs/pre_editor' if pre else 'docs/rendering'
    STATE = DOC_ROOT + ('/active/REFACTOR_STATE.md' if pre else '/REFACTOR_STATE.md')
    VALIDATION_POLICY = DOC_ROOT + ('/VALIDATION_CONTRACTS.md' if pre else '/VALIDATION_POLICY.md')
    PUBLICATION_POLICY = DOC_ROOT + ('/active/PUBLICATION_POLICY.json' if pre else '/PUBLICATION_POLICY.json')
    CHECKPOINT_DIR = workflow + '-checkpoints'
    JOURNAL_NAMESPACE = workflow + '-approval'
    SEAL_NAMESPACE = workflow + '-seals'
    TAG_PREFIX = 'pre-editor-phase-' if pre else 'render-refactor-phase-'
    COMMIT_PREFIX = 'pre-editor' if pre else 'render-refactor'
    PHASE_LIMIT = 58 if pre else 68



class Blocked(Exception):
    pass


def require(ok, message):
    if not ok:
        raise Blocked(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def encode_json(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n').encode('utf-8')


def atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f'.{os.getpid()}.tmp')
    try:
        tmp.write_bytes(data)
        os.replace(tmp, path)
    finally:
        if tmp.exists():
            tmp.unlink()


def phase_id(value):
    require(bool(re.fullmatch(r'[0-6][0-9]', value)) and int(value) <= PHASE_LIMIT, f'phase must be 00 through {PHASE_LIMIT}')
    return value


class Repo:
    def __init__(self, root):
        self.root = Path(root).resolve()
        top = self.git('rev-parse', '--show-toplevel').decode().strip()
        require(Path(top).resolve() == self.root, 'Run from the repository root or use --repo ROOT')

    def git(self, *args, input=None, allow_failure=False):
        p = subprocess.run(['git', '--no-optional-locks', '-C', str(self.root), *args], input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if p.returncode and not allow_failure:
            msg = p.stderr.decode('utf-8', 'replace').strip() or p.stdout.decode('utf-8', 'replace').strip()
            raise Blocked(f'git {args[0]} failed ({p.returncode}): {msg[-2400:]}')
        return p if allow_failure else p.stdout

    def text(self, *args):
        return self.git(*args).decode('utf-8', 'replace').strip()

    def names(self, *args):
        return {os.fsdecode(x) for x in self.git(*args).split(b'\0') if x}

    def path(self, rel):
        require(isinstance(rel, str) and rel and '\\' not in rel and '\x00' not in rel, f'invalid relative path: {rel!r}')
        pp = PurePosixPath(rel)
        require(not pp.is_absolute() and '..' not in pp.parts and ':' not in rel and pp.parts[0] != '.git', f'unsafe path: {rel}')
        require(pp.as_posix() == rel and not rel.endswith('/'), f'noncanonical path: {rel}')
        p = self.root / rel
        require(p.resolve().is_relative_to(self.root), f'path leaves repository: {rel}')
        for q in [p, *p.parents]:
            if q == self.root:
                break
            require(not q.is_symlink(), f'symlink needs explicit reviewed support: {rel}')
        require(not p.exists() or p.is_file(), f'expected file, not directory: {rel}')
        return p

    def fingerprint(self, rel):
        p = self.path(rel)
        if not p.exists():
            return {'absent': True}
        # Windows file modes do not represent the Git executable bit.
        executable = bool(p.stat().st_mode & 0o111) if os.name != 'nt' else None
        return {'sha256': digest(p.read_bytes()), 'executable': executable}

    def tracked(self, rel):
        return rel in self.names('ls-files', '-z', '--', rel)

    def ignored(self, rel):
        return self.git('check-ignore', '-q', '--', rel, allow_failure=True).returncode == 0

    def push_destination(self, remote):
        require(bool(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._/-]*', remote)), 'Invalid configured remote name')
        destinations = self.text('remote', 'get-url', '--push', '--all', remote).splitlines()
        require(len(destinations) == 1 and destinations[0], 'Lightweight finish requires one explicit push destination; review multi-destination publication separately')
        return destinations[0]

    def changed(self):
        return self.names('diff', '--name-only', '--no-renames', '-z', 'HEAD') | self.names('ls-files', '--others', '--exclude-standard', '-z')

    def staged(self):
        return self.names('diff', '--cached', '--name-only', '--no-renames', '-z', 'HEAD')

    def state(self):
        data = self.path(STATE).read_bytes()
        text = data.decode('utf-8')
        keys = ('Refactor branch', 'Configured remote', *FIELDS)
        result = {}
        for key in keys:
            m = re.findall(r'^- ' + re.escape(key) + r': `([^`]+)`\s*$', text, re.M)
            require(len(m) == 1, f'State requires exactly one {key} field')
            result[key] = m[0]
        return data, result

    def journal(self, phase):
        raw = self.text('rev-parse', '--git-path', f'{JOURNAL_NAMESPACE}/PHASE_{phase}.json')
        p = Path(raw)
        return p if p.is_absolute() else self.root / p

    def load(self, phase):
        p = self.journal(phase)
        require(p.is_file(), 'Missing sealed receipt/journal: use a metadata-only revision, not automatic revalidation')
        return json.loads(p.read_text(encoding='utf-8'))

    def save(self, m):
        atomic(self.journal(m['phase']), encode_json(m))


@contextmanager
def lock(repo):
    p = repo.journal('00').parent / 'operation.lock'
    p.parent.mkdir(parents=True, exist_ok=True)
    with p.open('a+b') as f:
        f.seek(0)
        if not f.read(1):
            f.write(b'0'); f.flush()
        f.seek(0)
        acquired = False
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(f.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(f.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            acquired = True
            yield
        except (BlockingIOError, PermissionError) as e:
            raise Blocked('Another checkpoint operation is running, or its files are not writable') from e
        finally:
            if acquired:
                f.seek(0)
                if os.name == 'nt':
                    msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
                else:
                    fcntl.flock(f.fileno(), fcntl.LOCK_UN)


def check_hashes(repo, mapping, sealed_fingerprints=None):
    sealed_fingerprints = sealed_fingerprints or {}
    mismatches = [p for p, fp in mapping.items()
                  if (sealed_fingerprints[p] if p in sealed_fingerprints else repo.fingerprint(p)) != fp]
    require(not mismatches, 'Snapshot mismatch; return to revision: ' + ', '.join(mismatches))


def index_identity(repo):
    # Logical contents, stages, modes and assume-unchanged/skip-worktree flags;
    # Git's harmless stat-cache refreshes are not staged content changes.
    return digest(repo.git('ls-files', '--stage', '-z') + b'\0' + repo.git('ls-files', '-v', '-z'))


def mutable_authorized(phase, paths):
    phase_id(phase)
    require(isinstance(paths, dict) and set(paths) <= OWNER_MUTABLE_PATHS,
            'Unauthorized owner-mutable-after-seal path')


def inherited_mutable(phase, declarations):
    mutable_authorized(phase, declarations)
    # Source overrides remain explicit. Omitted declarations inherit current
    # validated bytes; required checkpoint membership/fingerprints never default.
    return {p: declarations.get(p, {}) for p in sorted(OWNER_MUTABLE_PATHS)}


def validate_mutable_receipt(public):
    records = public.get('owner_mutable_after_seal', {})
    mutable_authorized(public['phase'], records)
    require(set(records) == OWNER_MUTABLE_PATHS, 'Receipt omits canonical owner-mutable path')
    require(OWNER_MUTABLE_PATHS <= set(public['checkpoint_files']), 'Mutable path missing from checkpoint')
    require(not (OWNER_MUTABLE_PATHS & set(public.get('protected_files', []))), 'Canonical owner-mutable path cannot be protected-only')
    for path, record in records.items():
        require(isinstance(record, dict) and {'fingerprint', 'mode', 'validated_blob', 'validated_blob_ref', 'blob', 'blob_ref'} <= set(record),
                'Incomplete canonical owner-mutable receipt metadata')
        require(record['fingerprint'] == public.get('validation_input_hashes', {}).get(path)
                and 'sha256' in record['fingerprint'], 'Canonical owner-mutable validation fingerprint missing or mismatched')
        require(record['mode'] in ('100644', '100755'), 'Invalid owner-mutable receipt mode')
        for field in ('validated_blob', 'blob'):
            oid, ref = record[field], record[field+'_ref']
            require(isinstance(oid, str) and re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', oid), 'Invalid sealed blob identity')
            require(isinstance(ref, str) and ref.startswith('refs/'+SEAL_NAMESPACE+'/') and ref.endswith('/'+oid),
                    'Invalid sealed blob retention ref')


def mutable_file(repo, path, fingerprint, mode):
    file = repo.path(path)
    require(file.is_file() and stat.S_ISREG(file.lstat().st_mode)
            and not (getattr(file.lstat(), 'st_file_attributes', 0) & 0x400),
            f'Owner-mutable path is missing or changed type: {path}')
    require(mode in ('100644', '100755') and repo.fingerprint(path)['executable'] == fingerprint['executable'],
            f'Owner-mutable file mode changed: {path}')
    if os.name != 'nt':
        require(mode == ('100755' if fingerprint['executable'] else '100644'),
                f'Owner-mutable executable mode differs from baseline: {path}')


def prepare_mutable(repo, phase, baseline, declarations, owned, inputs, previous):
    mutable_authorized(phase, declarations)
    require(set(declarations) == OWNER_MUTABLE_PATHS, 'Seal omits canonical owner-mutable path')
    require(set(declarations) <= owned, 'Owner-mutable path must remain a checkpoint file')
    records, sources = {}, {}
    for path, options in declarations.items():
        require(isinstance(options, dict) and set(options) <= {'validated_source'}, 'Invalid owner-mutable source options')
        source = options.get('validated_source', path)
        fp = repo.fingerprint(source)
        require('sha256' in fp and fp == inputs.get(path), f'Validated owner-mutable fingerprint mismatch: {path}')
        if source != path:
            require(previous is not None and previous['hashes'].get(path) == fp,
                    f'Imported owner-mutable source must match the prior seal exactly: {path}')
        entries = repo.git('ls-tree', '-z', baseline, '--', path).split(b'\0')
        fields = entries[0].split(b'\t', 1)[0].split()
        require(len(fields) == 3 and fields[1] == b'blob', f'Owner-mutable path must be a baseline regular file: {path}')
        mode = fields[0].decode()
        mutable_file(repo, path, fp, mode)
        records[path] = {'fingerprint': fp, 'mode': mode}
        sources[path] = repo.path(source).read_bytes()
        require(digest(sources[path]) == fp['sha256'], f'Owner-mutable source changed while reading: {path}')
    return records, sources


def blob_ref(repo, phase, path, oid):
    # Shared ordinary refs are GC roots even in a linked worktree. They are local,
    # content-addressed, never moved/deleted by approval, and never pushed by finish.
    workspace = digest(str(repo.root).encode('utf-8'))[:16]
    return f'refs/{SEAL_NAMESPACE}/{workspace}/phase-{phase}/{digest(path.encode("utf-8"))}/{oid}'


def pin_mutable(repo, phase, records, sources):
    for path, record in records.items():
        for field, args in [('validated_blob', ()), ('blob', ('--path='+path,))]:
            oid = repo.git('hash-object', '-w', *args, '--stdin', input=sources[path]).decode().strip()
            ref = blob_ref(repo, phase, path, oid)
            prior = repo.git('show-ref', '--verify', '--hash', ref, allow_failure=True)
            if prior.returncode:
                repo.git('update-ref', ref, oid, '0'*len(oid))
            else:
                require(prior.stdout.decode().strip() == oid, f'Sealed retention ref changed: {ref}')
            record[field], record[field+'_ref'] = oid, ref
        # An unchanged canonical member still needs its own validated snapshot
        # and retention metadata in this phase's receipt.
        # Check the sealed Git representation, never the newer owner's worktree.
        repo.git('diff', '--check', 'HEAD:'+path, record['blob'])


def verify_mutable(repo, m):
    records = m.get('owner_mutable_after_seal', {})
    mutable_authorized(m['phase'], records)
    require(set(records) == OWNER_MUTABLE_PATHS, 'Seal omits canonical owner-mutable path')
    require(set(records) <= set(m['checkpoint_files']), 'Mutable path missing from checkpoint')
    verified = {}
    for path, record in records.items():
        fp = record['fingerprint']
        require(fp == m['hashes'].get(path), f'Owner-mutable sealed fingerprint changed: {path}')
        mutable_file(repo, path, fp, record['mode'])
        for field in ('validated_blob', 'blob'):
            oid, ref = record[field], record[field+'_ref']
            require(isinstance(oid, str) and re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', oid), 'Invalid sealed blob identity')
            require(ref == blob_ref(repo, m['phase'], path, oid), 'Invalid sealed blob retention ref')
            target = repo.git('show-ref', '--verify', '--hash', ref, allow_failure=True)
            require(target.returncode == 0 and target.stdout.decode().strip() == oid, f'Missing/changed sealed blob retention ref: {ref}')
            require(repo.text('cat-file', '-t', oid) == 'blob', f'Sealed object is not a blob: {path}')
            data = repo.git('cat-file', 'blob', oid)
            require(repo.git('hash-object', '--stdin', input=data).decode().strip() == oid, f'Corrupt sealed blob: {path}')
            if field == 'validated_blob':
                require(digest(data) == fp['sha256'], f'Sealed blob differs from validated fingerprint: {path}')
        verified[path] = fp
    if records:
        tx = m.get('transaction')
        expected = tx['index_identity'] if tx else m['index_at_seal']
        require(index_identity(repo) == expected, 'Unexpected staged/index changes after seal; preserve index and return to revision/recovery')
    return verified


def clean_index(repo):
    require(not repo.staged() and not repo.git('ls-files', '-u'), 'Pre-existing staged or unmerged work: preserve it and resolve before sealing/approval')


def validate_results(spec):
    require(isinstance(spec.get('summary'), str) and spec['summary'].strip(), 'Nonempty current result summary required')
    require(isinstance(spec.get('validation'), list) and spec['validation'], 'Actual phase validation coverage is required')
    for v in spec['validation']:
        require(all(isinstance(v.get(k), str) and v[k].strip() for k in ('check', 'result', 'command', 'evidence')), 'Each validation needs check/result/command/evidence')
        require(v['result'] in ('PASS', 'NOT_APPLICABLE', 'ACCEPTED_EXCEPTION'), f'Unresolved validation: {v["check"]}')
        if v['result'] == 'NOT_APPLICABLE':
            require(v.get('reason'), f'NOT_APPLICABLE needs reason: {v["check"]}')
        if v['result'] == 'ACCEPTED_EXCEPTION':
            require(v.get('actual_outcome') and v.get('owner_acceptance'), f'Specific exception acceptance missing: {v["check"]}')
    for f in spec.get('findings', []):
        require(f.get('status') in ('RESOLVED', 'ACCEPTED_OUT_OF_SCOPE'), 'Unresolved implementation/review finding')
        require(f.get('finding') and f.get('disposition'), 'Finding requires evidence/disposition')


def phase02_handoff_gate(repo, phase, baseline, state):
    h = PHASE02_HANDOFF
    require(phase == h['phase'] and baseline == h['baseline'], 'Unauthorized phase or stabilization baseline handoff')
    require(state['Current approved phase'] == '01' and state['Current approved tag'] == h['previous_tag'], 'Handoff predecessor tag/state mismatch')
    tag = 'refs/tags/' + h['previous_tag']
    require(repo.text('rev-parse', '--verify', tag+'^{commit}') == h['previous_commit'], 'Handoff predecessor approved commit changed')
    require(repo.text('cat-file', '-t', tag) == 'tag' and repo.text('rev-parse', tag) == h['previous_tag_object'], 'Handoff predecessor tag object changed')
    require(repo.text('show', '-s', '--format=%P', baseline) == h['previous_commit'], 'Handoff must have exactly the approved predecessor as its single parent')
    previous = repo.load('01')
    tx = previous.get('transaction') or {}
    require(previous.get('status') == 'COMPLETE' and tx.get('commit') == h['previous_commit']
            and tx.get('branch_pushed') is True and tx.get('tag_pushed') is True,
            'Handoff predecessor publication is not complete')
    require(previous.get('push_destination') == repo.push_destination(state['Configured remote']), 'Handoff predecessor publication destination changed')
    receipt = 'rendering-checkpoints/phase-01.json'
    require(previous.get('receipt') == receipt and repo.tracked(receipt), 'Handoff predecessor receipt identity changed')
    check_hashes(repo, {receipt: previous['hashes'][receipt]})


def phase22_frozen_predecessor_gate(repo, baseline, state):
    # Exact Owner-authorized Phase22 exception; never a general skip/ancestor flag.
    authorization = repo.path('docs/pre_editor/evidence/phase-22-publication-exception.json')
    require(digest(authorization.read_bytes()) == 'ac79d43535dece228dd56c58df156bbd6a90c9b4a5c15f97f7ef5a3d798ad848',
            'Phase22 publication exception authorization changed')
    require(WORKFLOW == 'pre-editor' and baseline == '3ce1f6cbd9245893876c34441055e3a63ed1f7b6',
            'Phase22 frozen predecessor identity mismatch')
    require(state['Current approved phase'] == '20' and
            state['Current approved tag'] == 'pre-editor-phase-20-approved' and
            state['Next expected phase'] == '21', 'Phase22 exception requires truthful Phase20/next21 state')
    frozen = 'refs/tags/pre-editor-phase-21-frozen-unapproved'
    require(repo.text('cat-file', '-t', frozen) == 'tag' and
            repo.text('rev-parse', frozen) == '86ab5b8373c1cbc37357d084509f828a939d7a2a' and
            repo.text('rev-parse', frozen+'^{commit}') == baseline,
            'Phase22 frozen annotated tag identity changed')
    approved = '6263b3719bfd1b20d3ed8c40abf803580857a82c'
    require(repo.text('rev-parse', 'pre-editor-phase-20-approved^{commit}') == approved and
            repo.text('show', '-s', '--format=%P', baseline) == approved,
            'Phase22 frozen baseline must directly descend from approved Phase20')
    previous = repo.load('20')
    tx = previous.get('transaction') or {}
    require(previous['status'] == 'COMPLETE' and tx.get('commit') == approved and
            tx.get('branch_pushed') is True and tx.get('tag_pushed') is True,
            'Phase22 approved Phase20 publication is incomplete')
    freeze = repo.path('docs/pre_editor/evidence/phase-21-freeze.json')
    require(digest(freeze.read_bytes()) == '79cb382c21e937d0af7be5b1e36da5c2730ad1cffdacfe4acdbf0ec5e3d9844a', 'Phase21 frozen disposition changed')
    require(repo.git('show-ref', '--verify', '--quiet', 'refs/tags/pre-editor-phase-21-approved',
                     allow_failure=True).returncode == 1, 'Phase21 must remain unapproved')



def phase25_handoff_inputs():
    h = PHASE25_HANDOFF
    return {
        h['authorization']: h['authorization_sha256'],
        h['recovery_copy']: h['authorization_sha256'],
        h['previous_receipt']: h['previous_receipt_sha256'],
        h['collider_acceptance']: h['collider_acceptance_sha256'],
        h['integration_identity']: h['integration_identity_sha256'],
        **h['proposal_bindings'],
    }


def phase25_accepted_collider_gate(repo, phase, baseline, state):
    # Independent of current HEAD: pending recovery uses the sealed original state.
    # baseline_gate still checks HEAD before seal; finish checks the exact new parent/tree.
    h = PHASE25_HANDOFF
    require(WORKFLOW == h['workflow'] and phase == h['phase'] and baseline == h['baseline'],
            'Unauthorized Phase25 integration handoff')
    require(repo.root == Path(h['worktree']).resolve(), 'Phase25 handoff worktree mismatch')
    require(state['Refactor branch'] == h['branch'] and
            repo.text('branch', '--show-current') == h['branch'] and
            state['Configured remote'] == h['remote'] and
            state['Current approved phase'] == h['previous_phase'] and
            state['Current approved tag'] == h['previous_tag'] and
            state['Next expected phase'] == h['phase'], 'Phase25 handoff state mismatch')
    require(repo.push_destination(h['remote']) == h['push_destination'],
            'Phase25 handoff publication destination changed')
    for name, tag_object, commit in (
            (h['previous_tag'], h['previous_tag_object'], h['previous_commit']),
            (h['integration_tag'], h['integration_tag_object'], baseline)):
        tag = 'refs/tags/' + name
        require(repo.text('cat-file', '-t', tag) == 'tag' and
                repo.text('rev-parse', tag) == tag_object and
                repo.text('rev-parse', tag+'^{commit}') == commit,
                'Phase25 handoff annotated tag identity changed: ' + name)
    require(repo.text('show', '-s', '--format=%P', baseline) == h['previous_commit'] and
            repo.text('rev-parse', baseline+'^{tree}') == h['baseline_tree'],
            'Phase25 integration must have the exact tree and single approved parent')
    for path, expected in phase25_handoff_inputs().items():
        require(digest(repo.path(path).read_bytes()) == expected,
                'Phase25 handoff authority/evidence changed: ' + path)
    previous_bytes = repo.journal(h['previous_phase']).read_bytes()
    require(digest(previous_bytes) == h['previous_journal_sha256'],
            'Phase25 predecessor journal identity changed')
    previous = json.loads(previous_bytes.decode('utf-8'))
    tx = previous.get('transaction') or {}
    require(previous.get('status') == 'COMPLETE' and
            tx.get('commit') == h['previous_commit'] and
            tx.get('branch_pushed') is True and tx.get('tag_pushed') is True and
            previous.get('push_destination') == h['push_destination'] and
            previous.get('receipt') == h['previous_receipt'] and
            repo.tracked(h['previous_receipt']) and
            previous.get('hashes', {}).get(h['previous_receipt'], {}).get('sha256') == h['previous_receipt_sha256'],
            'Phase25 approved predecessor publication/receipt mismatch')


def baseline_gate(repo, phase, baseline, state):
    require(repo.text('rev-parse', 'HEAD') == baseline, 'HEAD differs from validated baseline')
    require(repo.text('branch', '--show-current') == state['Refactor branch'], 'Branch differs from workflow state')
    require(state['Configured remote'] in repo.text('remote').splitlines(), 'Configured remote missing/uninitialized')
    frozen22 = WORKFLOW == 'pre-editor' and phase == '22' and baseline == '3ce1f6cbd9245893876c34441055e3a63ed1f7b6'
    require(state['Next expected phase'] == phase or (frozen22 and state['Next expected phase'] == '21'),
            'Requested phase is not next expected phase')
    if WORKFLOW == 'pre-editor':
        require(state['Refactor branch'] == 'pre-editor/refactor', 'PRE_EDITOR branch mismatch')
        activation = json.loads(repo.path(DOC_ROOT+'/active/ACTIVATION.json').read_text(encoding='utf-8'))
        require(repo.root == Path(activation['destination_worktree']).resolve(), 'PRE_EDITOR worktree mismatch')
        if frozen22:
            phase22_frozen_predecessor_gate(repo, baseline, state)
        elif phase == '25':
            phase25_accepted_collider_gate(repo, phase, baseline, state)
        elif phase == '26':
            _roadmap('phase26_gate', repo, baseline, state)
        elif phase == '00':
            require(baseline == PRE_EDITOR_BASELINE and state['Current approved phase'] == 'NONE'
                    and state['Current approved tag'] == PRE_EDITOR_BASE_TAG, 'PRE_EDITOR bootstrap baseline/state mismatch')
            require(repo.text('rev-parse', PRE_EDITOR_BASE_TAG+'^{commit}') == baseline
                    and repo.text('rev-parse', PRE_EDITOR_BASE_TAG) == PRE_EDITOR_BASE_TAG_OBJECT,
                    'Rendering Phase 68 predecessor changed')
        else:
            prev = f'{int(phase)-1:02d}'
            tag = TAG_PREFIX+prev+'-approved'
            require(state['Current approved phase'] == prev and state['Current approved tag'] == tag,
                    'PRE_EDITOR predecessor/state mismatch')
            require(repo.text('rev-parse', tag+'^{commit}') == baseline, 'PRE_EDITOR predecessor commit mismatch')
            previous = repo.load(prev)
            tx = previous.get('transaction') or {}
            require(previous['status'] == 'COMPLETE' and tx.get('commit') == baseline
                    and tx.get('branch_pushed') is True and tx.get('tag_pushed') is True,
                    'PRE_EDITOR predecessor publication is incomplete')
        return
    prev = 'NONE' if phase == '00' else f'{int(phase)-1:02d}'
    tag = 'NONE' if phase == '00' else f'render-refactor-phase-{prev}-approved'
    require(state['Current approved phase'] == prev and state['Current approved tag'] == tag, 'Predecessor/state mismatch; reconcile actual approved Git history first')
    if phase == PHASE02_HANDOFF['phase']:
        phase02_handoff_gate(repo, phase, baseline, state)
    elif tag != 'NONE':
        require(repo.text('rev-parse', '--verify', f'refs/tags/{tag}^{{commit}}') == baseline, 'Prior approved tag does not resolve to baseline')



_ROADMAP_BRIDGE = None


def _roadmap(operation, *args):
    """The one named governance bridge shares this helper's integrity primitives."""
    global _ROADMAP_BRIDGE
    require(WORKFLOW == 'pre-editor', 'Roadmap operations require explicit --workflow pre-editor')
    if _ROADMAP_BRIDGE is None:
        path = Path(__file__).resolve().parents[2] / 'tools/pre_editor_roadmap_bridge.py'
        spec = importlib.util.spec_from_file_location('pre_editor_roadmap_bridge', path)
        require(spec is not None and spec.loader is not None, 'Roadmap bridge implementation is unavailable')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        _ROADMAP_BRIDGE = module
    return getattr(_ROADMAP_BRIDGE, operation)(SimpleNamespace(**globals()), *args)


def phase_contract_inputs(repo, phase):
    """Resolve only the accepted prospective PRE_EDITOR contract namespace."""
    contract = f'{DOC_ROOT}/phases/PHASE_{phase}.md'
    validation = VALIDATION_POLICY
    required = {contract, validation}
    if WORKFLOW == 'pre-editor' and int(phase) >= 26:
        activation = 'docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json'
        roadmap = 'docs/pre_editor/active/roadmap/MILESTONE_C_01'
        record = json.loads(repo.path(activation).read_text(encoding='utf-8-sig'))
        require(record.get('workflow') == 'pre-editor'
                and record.get('status') == 'ACTIVE_PROSPECTIVE_FOR_PHASES_26_58'
                and record.get('effective_phases') == {'first': '26', 'last': '58'}
                and record.get('roadmap_root') == roadmap,
                'Accepted Milestone C roadmap activation is missing or mismatched')
        contract = f'{roadmap}/phases/PHASE_{phase}.md'
        validation = f'{roadmap}/VALIDATION_CONTRACTS.md'
        required = {contract, validation, activation}
        if phase == '26':
            required.update(_roadmap('phase26_contract_inputs', repo))
    return contract, required


def seal(repo, spec):
    phase = phase_id(spec['phase'])
    validate_results(spec)
    clean_index(repo)
    state_bytes, state = repo.state()
    baseline_gate(repo, phase, spec['baseline'], state)
    old = None
    j = repo.journal(phase)
    if j.exists():
        old = json.loads(j.read_text())
        require(old.get('transaction') is None and old['status'] == 'SEALED', 'Cannot overwrite an active/completed approval journal')
    for other in j.parent.glob('PHASE_*.json'):
        o = json.loads(other.read_text())
        require(not o.get('transaction') or o['status'] == 'COMPLETE', f'Unfinished approval: {o["phase"]}')
    receipt = f'{CHECKPOINT_DIR}/phase-{phase}.json'
    require(not repo.tracked(receipt), 'This phase receipt already belongs to history; do not reseal approved work')
    sets = {k: spec.get(k, []) for k in ('checkpoint_files', 'local_governance', 'protected_files', 'contract_files')}
    for key, paths in sets.items():
        require(isinstance(paths, list) and len(paths) == len(set(paths)), f'Duplicate/invalid {key}')
        for p in paths:
            repo.path(p)
    owned, local, protected = (set(sets[k]) for k in ('checkpoint_files', 'local_governance', 'protected_files'))
    require(not (owned & local or owned & protected or local & protected), 'File ownership categories overlap')
    require(STATE not in owned | local | protected and receipt not in owned | local | protected, 'State and generated receipt are managed separately')
    review = spec['review']
    require(review in owned | local and repo.path(review).is_file(), 'Current review must exist and be classified')
    contract, required_contracts = phase_contract_inputs(repo, phase)
    require(required_contracts <= set(sets['contract_files']), 'Active phase, validation policy and applicable roadmap activation must be sealed contract inputs')
    contract_text = repo.path(contract).read_text(encoding='utf-8-sig' if WORKFLOW == 'pre-editor' else 'utf-8')
    nxt = re.findall(r'^next_phase: "([^\"]+)"$', contract_text, re.M)
    expected_next = 'COMPLETE' if int(phase) == PHASE_LIMIT else f'{int(phase)+1:02d}'
    if WORKFLOW == 'pre-editor':
        # Accepted contracts stay byte-exact; numeric ordering is defined by C0.
        require(contract_text.startswith(f'# PRE_EDITOR Phase {phase} '), 'PRE_EDITOR phase contract mismatch')
    else:
        require(nxt == [expected_next], 'Phase next_phase contract mismatch')
    for p in local:
        require(not repo.tracked(p) and repo.ignored(p), f'Local governance must be intentionally ignored and untracked: {p}')
    for p in owned | {receipt}:
        require(repo.tracked(p) or not repo.ignored(p), f'Intended checkpoint path is ignored; resolve policy during execution: {p}')
    actual = repo.changed()
    unknown = actual - owned - local - protected - {receipt}
    require(not unknown, 'Unclassified changes: ' + ', '.join(sorted(unknown)))
    inputs = spec.get('validation_input_hashes')
    require(isinstance(inputs, dict), 'validation_input_hashes mapping required (empty only for verified docs-only work)')
    if WORKFLOW == 'pre-editor' and phase == '25':
        bound_inputs = owned | local | protected | set(sets['contract_files']) | set(inputs)
        require(set(phase25_handoff_inputs()) <= bound_inputs, 'Phase25 handoff authority/evidence missing from snapshot')
        require(PHASE25_HANDOFF['authorization'] in local, 'Phase25 active authorization must be local governance')
    for p in inputs:
        repo.path(p)
    declarations = inherited_mutable(phase, spec.get('owner_mutable_after_seal', {}))
    records, sources = prepare_mutable(repo, phase, spec['baseline'], declarations, owned, inputs, old)
    require(owned <= actual | set(records), 'Checkpoint list includes unchanged paths: ' + ', '.join(sorted(owned - actual - set(records))))
    check_hashes(repo, inputs, {p: r['fingerprint'] for p, r in records.items()})
    preserved = set()
    if WORKFLOW == 'pre-editor' and phase == '00':
        # The owner requires byte-exact transfer, including historic whitespace.
        # Exempt only the hash-pinned reviewed package from new-file style checks.
        inventory_path = 'docs/pre_editor/evidence/package-files.json'
        inventory_bytes = repo.path(inventory_path).read_bytes()
        require(digest(inventory_bytes) == PRE_EDITOR_PACKAGE_SHA256, 'Reviewed transfer inventory changed')
        for entry in json.loads(inventory_bytes.decode('utf-8-sig'))['files']:
            path = entry['path']
            expected = PRE_EDITOR_PACKAGE_SHA256 if path == inventory_path else entry['sha256']
            require(path in owned and repo.fingerprint(path).get('sha256') == expected,
                    f'Reviewed transfer bytes or membership changed: {path}')
            preserved.add(path)
    ordinary = owned - set(records) - preserved
    if ordinary:
        repo.git('diff', '--check', 'HEAD', '--', *sorted(ordinary))
        for p in ordinary:
            if not repo.tracked(p) and repo.path(p).exists():
                result = repo.git('diff', '--no-index', '--check', '--', os.devnull, str(repo.path(p)), allow_failure=True)
                require(result.returncode in (0, 1) and not result.stdout.strip(), f'New-file whitespace failure: {p}')
    pin_mutable(repo, phase, records, sources)
    public = {
        'schema': 2, 'phase': phase, 'baseline': spec['baseline'], 'previous_approved_tag': state['Current approved tag'],
        'branch': state['Refactor branch'], 'remote': state['Configured remote'], 'next_phase': expected_next,
        'summary': spec['summary'], 'eligible': True, 'review': review,
        'checkpoint_files': sorted(owned | {receipt}), 'local_governance': sorted(local), 'protected_files': sorted(protected),
        'findings': spec.get('findings', []), 'validation': spec['validation'], 'validation_input_hashes': inputs,
    }
    if WORKFLOW == 'rendering' and phase == '02' and spec['baseline'] == PHASE02_HANDOFF['baseline']:
        public['baseline_handoff'] = dict(PHASE02_HANDOFF)
    elif WORKFLOW == 'pre-editor' and phase == '25':
        public['baseline_handoff'] = dict(PHASE25_HANDOFF)
    elif WORKFLOW == 'pre-editor' and phase == '26':
        public['governance_handoff'] = _roadmap('phase26_gate', repo, spec['baseline'], state)
    if records:
        public['owner_mutable_after_seal'] = records
    validate_mutable_receipt(public)
    atomic(repo.path(receipt), encode_json(public))
    bound = owned | local | protected | set(sets['contract_files']) | set(inputs) | {receipt}
    m = {'schema': 2, 'phase': phase, 'status': 'SEALED', 'baseline': spec['baseline'], 'receipt': receipt,
         'push_destination': repo.push_destination(state['Configured remote']),
         'state_before': base64.b64encode(state_bytes).decode(), 'state': state,
         'checkpoint_files': public['checkpoint_files'], 'local_governance': sorted(local), 'protected_files': sorted(protected),
         'hashes': {p: records[p]['fingerprint'] if p in records else repo.fingerprint(p) for p in sorted(bound)}, 'transaction': None}
    if records:
        m.update(owner_mutable_after_seal=records, index_at_seal=index_identity(repo))
    repo.save(m)
    return {'status': 'SEALED', 'phase': phase, 'receipt': receipt, 'checkpoint_files': len(public['checkpoint_files']), 'validation_executed_by_helper': False}


def check(repo, m, pending=False):
    require(m.get('schema') == 2 and m['status'] in ('SEALED', 'PENDING'), 'Invalid or completed seal for this operation')
    check_hashes(repo, m['hashes'], verify_mutable(repo, m))
    current_state, state = repo.state()
    require(repo.text('branch', '--show-current') == m['state']['Refactor branch'], 'Branch changed since seal')
    require(state['Configured remote'] == m['state']['Configured remote'], 'Remote changed since seal')
    require(repo.push_destination(state['Configured remote']) == m['push_destination'], 'Remote push destination changed since seal; preserve checkpoint and reconcile authorization')
    original = base64.b64decode(m['state_before'])
    allowed = [original]
    if pending and m.get('transaction'):
        allowed.append(base64.b64decode(m['transaction']['state_after']))
    require(current_state in allowed, 'Workflow state changed outside deterministic approval update')
    if not pending:
        clean_index(repo)
        baseline_gate(repo, m['phase'], m['baseline'], state)
    elif WORKFLOW == 'rendering' and m['phase'] == '02' and m['baseline'] == PHASE02_HANDOFF['baseline']:
        phase02_handoff_gate(repo, m['phase'], m['baseline'], m['state'])
    elif WORKFLOW == 'pre-editor' and m['phase'] == '25':
        phase25_accepted_collider_gate(repo, m['phase'], m['baseline'], m['state'])
    elif WORKFLOW == 'pre-editor' and m['phase'] == '26':
        _roadmap('phase26_gate', repo, m['baseline'], m['state'])
    unknown = repo.changed() - set(m['checkpoint_files']) - set(m['protected_files']) - set(m['local_governance']) - ({STATE} if pending else set())
    require(not unknown, 'Changes appeared after seal: ' + ', '.join(sorted(unknown)))
    public = json.loads(repo.path(m['receipt']).read_text(encoding='utf-8'))
    if WORKFLOW == 'pre-editor' and m['phase'] == '25':
        require(public.get('baseline_handoff') == PHASE25_HANDOFF, 'Phase25 receipt handoff identity mismatch')
        require(PHASE25_HANDOFF['authorization'] in m.get('local_governance', []),
                'Phase25 active authorization missing from sealed governance')
        require(all(m['hashes'].get(p, {}).get('sha256') == expected
                    for p, expected in phase25_handoff_inputs().items()),
                'Phase25 sealed authority/evidence binding mismatch')
    if WORKFLOW == 'pre-editor' and m['phase'] == '26':
        require(public.get('governance_handoff') == _roadmap('phase26_gate', repo, m['baseline'], m['state']),
                'Phase26 sealed governance bridge identity changed')
    validate_mutable_receipt(public)
    require(public.get('eligible') is True and public['phase'] == m['phase'] and public['baseline'] == m['baseline'], 'Receipt identity/eligibility mismatch')
    require(public.get('owner_mutable_after_seal', {}) == m.get('owner_mutable_after_seal', {}), 'Owner-mutable receipt/journal mismatch')
    validate_results(public)
    return public


def state_after(m, public):
    text = base64.b64decode(m['state_before']).decode('utf-8')
    values = (m['phase'], f'{TAG_PREFIX}{m["phase"]}-approved', public['next_phase'], public['review'])
    for key, value in zip(FIELDS, values):
        # Keep original line endings and every unrelated byte/field.
        pattern = r'(^- ' + re.escape(key) + r': )`[^`]+`'
        text, count = re.subn(pattern, lambda z: z[1] + '`' + value + '`', text, flags=re.M)
        require(count == 1, f'Ambiguous state field: {key}')
    return text.encode('utf-8')


def staged_matches_worktree(repo, paths, owner_mutable=None):
    owner_mutable = owner_mutable or {}
    for p in paths:
        path = repo.path(p)
        entries = repo.git('ls-files', '--stage', '-z', '--', p).split(b'\0')
        entries = [e for e in entries if e]
        if p in owner_mutable:
            record = owner_mutable[p]
            require(len(entries) == 1 and entries[0].split(b'\t', 1)[0].split() ==
                    [record['mode'].encode(), record['blob'].encode(), b'0'],
                    f'Index differs from exact sealed blob/mode: {p}')
            continue
        if not path.exists():
            require(not entries, f'Staged deletion differs: {p}')
            continue
        require(len(entries) == 1, f'Unexpected index entries: {p}')
        fields = entries[0].split(b'\t', 1)[0].split()
        require(fields[2] == b'0' and fields[0] in (b'100644', b'100755'), f'Unsupported staged mode: {p}')
        expected = repo.git('hash-object', '--path='+p, '--stdin', input=path.read_bytes()).strip()
        require(fields[1] == expected, f'Index content differs from sealed worktree: {p}')
        filemode = repo.git('config', '--bool', '--get', 'core.filemode', allow_failure=True)
        if os.name != 'nt' and filemode.stdout.strip() != b'false':
            mode = b'100755' if path.stat().st_mode & 0o111 else b'100644'
            require(fields[0] == mode, f'Index executable bit differs: {p}')


def policy_target(policy, phase):
    """Validate the project policy and expand only its two-digit phase field."""
    phase = phase_id(phase)
    require(isinstance(policy, dict) and type(policy.get('schema')) is int and policy.get('schema') == 1
            and type(policy.get('enabled')) is bool, 'Publication policy requires schema 1 and boolean enabled')
    repository = policy.get('repository')
    require(isinstance(repository, str) and re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository)
            and all(p not in ('.', '..') for p in repository.split('/')), 'Publication policy requires an exact OWNER/REPO')
    branch, template = policy.get('branch'), policy.get('tag_template')
    require(isinstance(branch, str) and branch, 'Publication policy requires an exact branch')
    require(isinstance(template, str) and template.count('{phase}') == 1
            and not re.search(r'[{}]', template.replace('{phase}', '')),
            'Publication policy tag_template requires exactly one {phase} field')
    return {'repository': repository, 'branch': branch, 'tag': template.replace('{phase}', phase)}


def publication_target(repo, m):
    phase = phase_id(m['phase'])
    branch = m['state']['Refactor branch']
    require(repo.text('branch', '--show-current') == branch, 'Publication branch differs from sealed branch')
    destination = repo.push_destination(m['state']['Configured remote'])
    require(destination == m['push_destination'], 'Remote push destination changed since seal; preserve the checkpoint')
    match = re.fullmatch(r'(?:https://github\.com/|git@github\.com:|ssh://git@github\.com/)([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+?)(?:\.git)?/?', destination)
    require(match is not None and all(p not in ('.', '..') for p in match[1].split('/')),
            'Publication requires one exact supported GitHub repository URL; do not infer another destination')
    repo.git('check-ref-format', 'refs/heads/'+branch)
    target = {'repository': match[1], 'branch': branch, 'tag': f'{TAG_PREFIX}{phase}-approved'}
    repo.git('check-ref-format', 'refs/tags/'+target['tag'])
    return target


def publication_policy(repo, phase, target):
    """Discovery alone never grants consent. Missing/invalid policy fails closed."""
    result = {'path': PUBLICATION_POLICY, 'status': 'INVALID', 'matches': False}
    try:
        path = repo.path(PUBLICATION_POLICY)
        if not path.is_file():
            return dict(result, status='MISSING', reason='Project standing publication policy is missing')
        policy = json.loads(path.read_text(encoding='utf-8'))
        expected = policy_target(policy, phase)
        repo.git('check-ref-format', 'refs/heads/'+expected['branch'])
        repo.git('check-ref-format', 'refs/tags/'+expected['tag'])
        result['expected_target'] = expected
        if not policy['enabled']:
            return dict(result, status='DISABLED', reason='Standing publication authorization is disabled')
        if expected != target:
            return dict(result, status='MISMATCH', reason='Resolved target differs from project standing publication policy')
        return dict(result, status='MATCH', matches=True)
    except (Blocked, OSError, ValueError, KeyError, TypeError) as error:
        return dict(result, reason=str(error))


def publication_authorized(repo, m, authorization=None):
    target = publication_target(repo, m)
    if authorization is not None:
        # Supply these only for an exact owner-authorized destination. They
        # authorize discovery's result; they cannot change any sealed target.
        require(authorization == target, f'Publication arguments differ from resolved sealed target; expected {target}')
        return target
    policy = publication_policy(repo, m['phase'], target)
    require(policy['matches'], f'Explicit publication authorization required for {target}; '
            f'{PUBLICATION_POLICY}: {policy["status"]}. STOP before finish and request owner authorization. '
            'Git configuration is discovery only; no staging, commit, tag or push is authorized.')
    return target


def finish(repo, m, authorization=None):
    publication_authorized(repo, m, authorization)  # Before any approval mutation.
    phase = m['phase']; tag = f'{TAG_PREFIX}{phase}-approved'
    if m['status'] == 'COMPLETE':
        commit = m['transaction']['commit']
        require(repo.text('cat-file', '-t', 'refs/tags/'+tag) == 'tag', 'Completed tag is not annotated')
        require(repo.text('rev-parse', 'refs/tags/'+tag+'^{commit}') == commit, 'Completed approved tag changed')
        return {'status': 'ALREADY_COMPLETE', 'phase': phase, 'commit': commit, 'tag': tag, 'publication': 'previously verified complete'}
    public = check(repo, m, pending=m.get('transaction') is not None)
    if m.get('transaction') is None:
        after = state_after(m, public)
        files = sorted(set(m['checkpoint_files']) | ({STATE} if repo.tracked(STATE) else set()))
        m['transaction'] = {'state_after': base64.b64encode(after).decode(), 'files': files, 'tree': None,
                            'commit': None, 'branch_pushed': False, 'tag_pushed': False}
        if m.get('owner_mutable_after_seal'):
            m['transaction']['index_identity'] = index_identity(repo)
        m['status'] = 'PENDING'
        repo.save(m)
    tx = m['transaction']; files = tx['files']
    mutable = m.get('owner_mutable_after_seal', {})
    require(repo.tracked(STATE) == (STATE in files), 'State tracking policy changed during approval')
    head = repo.text('rev-parse', 'HEAD')
    if head == m['baseline']:
        require(tx['commit'] is None, 'HEAD moved back after approval commit; preserve history')
        staged = repo.staged()
        require(staged <= set(files) and not repo.git('ls-files', '-u'), 'Unexpected staged/unmerged work during recovery')
        staged_matches_worktree(repo, staged, mutable)
        atomic(repo.path(STATE), base64.b64decode(tx['state_after']))
        ordinary = sorted(set(files) - set(mutable))
        if ordinary:
            repo.git('add', '--', *ordinary)
        for path, record in mutable.items():
            repo.git('update-index', '--add', '--cacheinfo', record['mode'], record['blob'], path)
        unchanged_mutable = {p for p, record in mutable.items()
                             if repo.text('rev-parse', m['baseline']+':'+p) == record['blob']}
        require(repo.staged() == set(files) - unchanged_mutable, 'Staged path set does not exactly match approved checkpoint')
        staged_matches_worktree(repo, files, mutable)
        tx['tree'] = repo.text('write-tree')
        if mutable:
            tx['index_identity'] = index_identity(repo)
        repo.save(m)
        repo.git('commit', '-m', f'{COMMIT_PREFIX}: complete phase {phase} '+public['summary'].splitlines()[0][:90])
        head = repo.text('rev-parse', 'HEAD')
    require(head != m['baseline'], 'No new commit was created')
    require(tx['tree'] and repo.text('rev-parse', head+'^{tree}') == tx['tree'], 'Approval commit tree differs from exact staged checkpoint')
    require(repo.text('show', '-s', '--format=%P', head) == m['baseline'], 'Approval commit must have exactly the original baseline parent')
    require(tx['commit'] in (None, head), 'HEAD differs from recorded approval commit')
    tx['commit'] = head; repo.save(m)
    check(repo, m, pending=True)
    require(repo.path(STATE).read_bytes() == base64.b64decode(tx['state_after']), 'Pending state advancement is missing')
    clean_index(repo)
    exists = repo.git('show-ref', '--verify', '--quiet', 'refs/tags/'+tag, allow_failure=True).returncode == 0
    if not exists:
        repo.git('tag', '-a', tag, head, '-m', f'{"PRE_EDITOR" if WORKFLOW == "pre-editor" else "Rendering"} phase {phase} approved; reviewed receipt {m["receipt"]}')
    require(repo.text('cat-file', '-t', 'refs/tags/'+tag) == 'tag', 'Approved tag must be annotated; never overwrite it')
    require(repo.text('rev-parse', 'refs/tags/'+tag+'^{commit}') == head, 'Approved tag points elsewhere; never overwrite it')
    remote, branch = public['remote'], public['branch']
    if not tx['branch_pushed']:
        repo.git('push', remote, head+':refs/heads/'+branch)
        tx['branch_pushed'] = True; repo.save(m)
    if not tx['tag_pushed']:
        repo.git('push', remote, 'refs/tags/'+tag+':refs/tags/'+tag)
        tx['tag_pushed'] = True; repo.save(m)
    refs = {}
    for line in repo.text('ls-remote', '--', m['push_destination'], 'refs/heads/'+branch, 'refs/tags/'+tag, 'refs/tags/'+tag+'^{}').splitlines():
        oid, name = line.split('\t'); refs[name] = oid
    require(refs.get('refs/heads/'+branch) == head and refs.get('refs/tags/'+tag+'^{}') == head and refs.get('refs/tags/'+tag) == repo.text('rev-parse', 'refs/tags/'+tag), 'Remote checkpoint is incomplete or changed concurrently; no force operation')
    check(repo, m, pending=True)
    require(repo.text('rev-parse', 'HEAD') == head, 'HEAD changed during publication')
    clean_index(repo)
    m['status'] = 'COMPLETE'; repo.save(m)
    return {'status': 'COMPLETE', 'phase': phase, 'commit': head, 'tag': tag, 'remote': remote,
            'branch_pushed': True, 'tag_pushed': True, 'committed_files': files,
            'protected_files': m['protected_files'], 'remaining_changed_paths': sorted(repo.changed()),
            'validation_rerun': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', default='.')
    parser.add_argument('--workflow', choices=('rendering', 'pre-editor'), default='rendering')
    sub = parser.add_subparsers(dest='command', required=True)
    f = sub.add_parser('fingerprint'); f.add_argument('--paths', nargs='+', required=True)
    s = sub.add_parser('seal'); s.add_argument('--spec', required=True)
    for name in ('roadmap-check', 'roadmap-finish'):
        g = sub.add_parser(name); g.add_argument('--spec', required=True)
        if name == 'roadmap-finish':
            g.add_argument('--authorization', required=True)
    for cmd in ('check', 'finish', 'publication-target'):
        c = sub.add_parser(cmd); c.add_argument('--phase', required=True)
        if cmd == 'finish':
            c.add_argument('--authorize-repository')
            c.add_argument('--authorize-branch')
            c.add_argument('--authorize-tag')
    args = parser.parse_args()
    try:
        configure_workflow(args.workflow)
        repo = Repo(args.repo)
        if args.command == 'fingerprint':
            result = {p: repo.fingerprint(p) for p in sorted(set(args.paths) | OWNER_MUTABLE_PATHS)}
        elif args.command == 'roadmap-check':
            result = _roadmap('check', repo, args.spec)
        elif args.command == 'roadmap-finish':
            require(WORKFLOW == 'pre-editor', 'Roadmap publication is PRE_EDITOR only')
            with lock(repo):
                result = _roadmap('publish', repo, args.spec, args.authorization)
        elif args.command == 'publication-target':
            phase = phase_id(args.phase)
            target = publication_target(repo, repo.load(phase))
            policy = publication_policy(repo, phase, target)
            result = {'status': 'TARGET_RESOLVED', **target,
                      'standing_owner_authorization': policy['matches'], 'standing_policy': policy}
            if not policy['matches']:
                result['authorization_request'] = 'STOP before finish and request explicit owner authorization for this exact resolved repository, branch and tag.'
        else:
            with lock(repo):
                if args.command == 'seal':
                    result = seal(repo, json.loads(Path(args.spec).read_text(encoding='utf-8')))
                else:
                    m = repo.load(phase_id(args.phase))
                    if args.command == 'finish':
                        authorization = {'repository': args.authorize_repository, 'branch': args.authorize_branch, 'tag': args.authorize_tag}
                        if all(value is None for value in authorization.values()):
                            authorization = None
                        result = finish(repo, m, authorization)
                    else:
                        check(repo, m, pending=m.get('transaction') is not None)
                        result = {'status': 'MATCH', 'phase': args.phase, 'validation_rerun': False}
        print(json.dumps(result, ensure_ascii=False, indent=2))
    except (Blocked, OSError, ValueError, KeyError, TypeError) as e:
        print(json.dumps({'status': 'BLOCKED', 'reason': str(e), 'action': 'Preserve work; resolve only this condition. No automatic revalidation or forced Git operation.'}, ensure_ascii=False, indent=2))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
