"""One exact Milestone C governance publication, using checkpoint primitives.

This is deliberately not a phase approval or a general ancestor admission. The
caller supplies the existing checkpoint module, Repo, and operation lock. All
Git mutation requires a separate, exact owner publication authorization.
"""
import json
import os
from pathlib import Path
import re
import stat


ROADMAP_PUBLICATION = {
    'identity': 'milestone-c-roadmap-01',
    'worktree': 'C:/dev/GEngine-pre-editor',
    'repository': 'AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine',
    'branch': 'pre-editor/refactor',
    'remote': 'origin',
    'push_destination': 'https://github.com/AlbertBoll/OpenGL-3D-Rigid-Body-Simulation-Engine.git',
    'parent': '1a2adf61c1571d9f4604f2ca185fea3f95682b4c',
    'parent_tag': 'pre-editor-phase-25-approved',
    'parent_tag_object': '9d68bbfeebcdd42fa69fd1dc9ac8757176a317c4',
    'parent_journal_sha256': '7d4a72b21bc214137d3b7d80cf9d6c7a6263560376ea29899c0caa388643e097',
    'parent_receipt': 'pre-editor-checkpoints/phase-25.json',
    'parent_receipt_sha256': 'bcb60fc891fe456d1ae066b251078a01afc938480d25c6a1c4f402f364c07afe',
    'parent_receipt_blob': '382cee2ab5201f310518b1c639b0d643b72e350d',
    'tag': 'pre-editor-milestone-c-roadmap-01-approved',
    'snapshot_root': 'docs/pre_editor/revisions/MILESTONE_C_ROADMAP_PUBLICATION_01',
    'spec_path': 'docs/pre_editor/revisions/MILESTONE_C_ROADMAP_PUBLICATION_01/publication-spec.json',
    'manifest_path': 'docs/pre_editor/revisions/MILESTONE_C_ROADMAP_PUBLICATION_01/publication-manifest.json',
    'authorization_path': 'docs/pre_editor/active/MILESTONE_C_ROADMAP_PUBLICATION_AUTHORIZATION_01.json',
    'journal_namespace': 'pre-editor-governance',
    'helper_paths': ['tools/pre_editor_roadmap_bridge.py', 'tools/test_pre_editor_roadmap_bridge.py'],
    'physics_trees': {'GEngine/include/GEngine/Physics': 'f8f95c7d63ca5126f3995d48ae8bfb35e198c7ec'},
    'snapshot_source_prefix': 'docs/pre_editor/active/roadmap/MILESTONE_C_01/',
    'activation_path': 'docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json',
    'activation_sha256': '4b04cefc2c00e55973db0785da16c95fe62162f81c61bc8e893d7ded9e8e9447',
    'generated_snapshot_names': ['.gitattributes', 'README.md', 'REVIEW.md', 'receipt.json', 'PUBLICATION_PROCEDURE.md'],
    'snapshot_source_paths': [
        'AGENTS.md', '.codex/tools/rendering_checkpoint.py',
        '.agents/skills/pre-editor-phase-execution/SKILL.md',
        '.agents/skills/pre-editor-phase-approval/SKILL.md',
        '.agents/skills/pre-editor-phase-rejection/SKILL.md',
        'docs/pre_editor/active/MILESTONE_C_ROADMAP_ACTIVATION_01.json',
        'docs/pre_editor/active/PUBLICATION_GRANT_MILESTONE_C_01.json',
        'docs/pre_editor/active/PUBLICATION_POLICY.json',
        'docs/pre_editor/active/PHASE_18_PLUS_MINIMUM_SUFFICIENT_VALIDATION.md',
        'docs/pre_editor/active/PHASE_18_PLUS_ARTIFACT_RETENTION.json',
        'docs/pre_editor/active/PHASE_18_PLUS_MSV_ACTIVATION.json',
        'docs/pre_editor/active/MILESTONE_C_ROADMAP_BRIDGE_AUTHORIZATION_01.json',
        'docs/pre_editor/active/MILESTONE_C_ROADMAP_BRIDGE_REMEDIATION_01.json',
    ],
}

_TARGET = ('identity', 'parent', 'tag', 'repository', 'branch', 'remote', 'push_destination')
_STATUSES = ('PREPARED', 'STAGED', 'COMMIT_PENDING', 'COMMITTED',
             'TAG_PENDING', 'TAGGED', 'PUSH_PENDING', 'COMPLETE')


def _journal(repo):
    h = ROADMAP_PUBLICATION
    path = Path(repo.text('rev-parse', '--git-path', h['journal_namespace'] + '/' + h['identity'] + '.json'))
    return path if path.is_absolute() else repo.root / path


def _safe_private(c, path, required=True):
    for part in (path, *path.parents):
        c.require(not part.is_symlink(), 'Roadmap private journal uses a symlink')
        if part.exists():
            attrs = getattr(part.lstat(), 'st_file_attributes', 0)
            c.require(not attrs & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400),
                      'Roadmap private journal uses a Windows reparse point')
    c.require(path.is_file() if required else not path.exists() or path.is_file(),
              'Missing or nonregular roadmap private journal')
    return path


def _read(c, repo, path):
    p = repo.path(path)
    for part in (p, *p.parents):
        if part.exists():
            attrs = getattr(part.lstat(), 'st_file_attributes', 0)
            c.require(not attrs & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400),
                      'Roadmap input uses a Windows reparse point: ' + path)
        if part == repo.root:
            break
    c.require(p.is_file(), 'Missing roadmap publication input: ' + path)
    return p.read_bytes()


def _json(c, data, label):
    def object_pairs(items):
        value = {}
        for key, item in items:
            c.require(key not in value, 'Duplicate roadmap JSON key: ' + label + ': ' + key)
            value[key] = item
        return value
    try:
        value = json.loads(data.decode('utf-8'), object_pairs_hook=object_pairs)
    except (ValueError, UnicodeError) as error:
        raise c.Blocked('Invalid roadmap JSON: ' + label) from error
    c.require(isinstance(value, dict), 'Roadmap JSON must be an object: ' + label)
    return value


def _environment(c, repo):
    h = ROADMAP_PUBLICATION
    for name in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_COMMON_DIR',
                 'GIT_OBJECT_DIRECTORY', 'GIT_ALTERNATE_OBJECT_DIRECTORIES', 'GIT_NAMESPACE'):
        c.require(not os.environ.get(name), 'Roadmap publication forbids Git environment redirection: ' + name)
    c.require(c.WORKFLOW == 'pre-editor', 'Roadmap bridge requires the PRE_EDITOR namespace')
    c.require(repo.root == Path(h['worktree']).resolve(), 'Roadmap publication worktree mismatch')
    c.require(repo.text('branch', '--show-current') == h['branch'], 'Roadmap publication branch mismatch')
    c.require(repo.push_destination(h['remote']) == h['push_destination'], 'Roadmap push destination changed')
    mirror = repo.git('config', '--bool', '--get', 'remote.' + h['remote'] + '.mirror', allow_failure=True)
    c.require(mirror.returncode in (0, 1) and mirror.stdout.strip() != b'true',
              'Roadmap publication forbids mirror pushes')


def _predecessor(c, repo, state):
    h = ROADMAP_PUBLICATION
    c.require(state.get('Refactor branch') == h['branch'] and state.get('Configured remote') == h['remote']
              and state.get('Current approved phase') == '25' and state.get('Current approved tag') == h['parent_tag']
              and state.get('Next expected phase') == '26', 'Roadmap bridge requires unchanged approved Phase25/next26 state')
    ref = 'refs/tags/' + h['parent_tag']
    c.require(repo.text('cat-file', '-t', ref) == 'tag'
              and repo.text('rev-parse', ref) == h['parent_tag_object']
              and repo.text('rev-parse', ref + '^{commit}') == h['parent'], 'Approved Phase25 tag identity changed')
    raw = _safe_private(c, repo.journal('25')).read_bytes()
    c.require(c.digest(raw) == h['parent_journal_sha256'], 'Approved Phase25 journal identity changed')
    previous = _json(c, raw, 'Phase25 journal')
    tx = previous.get('transaction') or {}
    c.require(previous.get('status') == 'COMPLETE' and tx.get('commit') == h['parent']
              and tx.get('branch_pushed') is True and tx.get('tag_pushed') is True
              and previous.get('push_destination') == h['push_destination']
              and previous.get('receipt') == h['parent_receipt']
              and previous.get('hashes', {}).get(h['parent_receipt'], {}).get('sha256') == h['parent_receipt_sha256'],
              'Approved Phase25 publication receipt is incomplete or changed')
    c.require(c.digest(_read(c, repo, h['parent_receipt'])) == h['parent_receipt_sha256'],
              'Approved Phase25 working receipt bytes changed')
    # The historical working receipt is CRLF; its approved Git blob is LF.
    # Preserve both original identities instead of relabeling either fingerprint.
    c.require(repo.text('rev-parse', h['parent'] + ':' + h['parent_receipt']) == h['parent_receipt_blob'],
              'Approved Phase25 committed receipt blob changed')
    for path, tree in h['physics_trees'].items():
        c.require(repo.text('rev-parse', h['parent'] + ':' + path) == tree,
                  'Approved Phase25 Physics tree changed: ' + path)


def _entry(c, repo, revision, path):
    rows = [x for x in repo.git('ls-tree', '-z', revision, '--', path).split(b'\0') if x]
    if not rows:
        return None
    c.require(len(rows) == 1, 'Ambiguous roadmap tree entry: ' + path)
    fields, actual = rows[0].split(b'\t', 1)
    mode, kind, oid = fields.decode().split()
    c.require(os.fsdecode(actual) == path and kind == 'blob' and mode in ('100644', '100755'),
              'Unsupported roadmap tree entry: ' + path)
    return {'mode': mode, 'sha256': c.digest(repo.git('cat-file', 'blob', oid))}


def _receipt_payload(c, manifest):
    receipt = ROADMAP_PUBLICATION['snapshot_root'] + '/receipt.json'
    return c.digest(c.encode_json({'schema': 1, 'identity': manifest['identity'],
                                 'files': [f for f in manifest['files'] if f['path'] != receipt],
                                 'local_bindings': manifest['local_bindings']}))


def _accepted_closure(c, repo, manifest):
    h = ROADMAP_PUBLICATION
    raw = _read(c, repo, h['activation_path'])
    c.require(c.digest(raw) == h['activation_sha256'], 'Accepted roadmap activation identity changed')
    activation = _json(c, raw, h['activation_path'])
    artifacts = activation.get('artifact_hashes')
    c.require(isinstance(artifacts, dict) and artifacts, 'Accepted roadmap artifact closure is missing')
    sources = {}
    for entry in manifest['files']:
        if 'source' in entry:
            c.require(entry['source'].casefold() not in {s.casefold() for s in sources},
                      'A governance source is copied more than once in the publication')
            sources[entry['source']] = entry
    c.require(set(sources) == set(artifacts) | set(h['snapshot_source_paths']),
              'Publication must contain the exact accepted roadmap and required governance sources')
    c.require(set(h['helper_paths']) <= {f['path'] for f in manifest['files'] if f['role'] == 'helper'},
              'Publication is missing its normal tracked helper or offline tests')
    for source, expected in artifacts.items():
        c.require(source.startswith(h['snapshot_source_prefix']), 'Accepted activation has an unexpected roadmap source')
        entry = sources[source]
        destination = h['snapshot_root'] + '/roadmap/' + source[len(h['snapshot_source_prefix']):]
        c.require(entry['path'] == destination and entry['sha256'] == expected
                  and manifest['local_bindings'].get(source) == expected,
                  'Accepted roadmap source-to-public mapping changed: ' + source)


def _inputs(c, repo, spec_path, current=True):
    h = ROADMAP_PUBLICATION
    c.require(spec_path == h['spec_path'], 'Roadmap publication requires its fixed spec path')
    spec_raw = _read(c, repo, spec_path)
    spec = _json(c, spec_raw, spec_path)
    c.require(set(spec) == {'schema', *_TARGET, 'manifest', 'manifest_sha256'} and type(spec.get('schema')) is int and spec['schema'] == 1,
              'Unexpected roadmap spec schema')
    c.require(all(spec.get(k) == h[k] for k in _TARGET) and spec.get('manifest') == h['manifest_path'],
              'Roadmap spec differs from the fixed publication identity')
    manifest_raw = _read(c, repo, h['manifest_path'])
    c.require(c.digest(manifest_raw) == spec['manifest_sha256'], 'Roadmap manifest identity changed')
    manifest = _json(c, manifest_raw, h['manifest_path'])
    c.require(set(manifest) == {'schema', 'identity', 'files', 'local_bindings'} and type(manifest.get('schema')) is int and manifest['schema'] == 1
              and manifest.get('identity') == h['identity'] and isinstance(manifest.get('files'), list)
              and isinstance(manifest.get('local_bindings'), dict), 'Unexpected roadmap manifest schema')
    files = {}
    casefold_paths = {spec_path.casefold(), h['manifest_path'].casefold()}
    for entry in manifest['files']:
        c.require(isinstance(entry, dict) and set(entry) in ({'path', 'sha256', 'mode', 'role'},
                  {'path', 'sha256', 'mode', 'role', 'source'}), 'Unexpected roadmap manifest entry schema')
        path = entry['path']
        repo.path(path)
        c.require(path.casefold() not in casefold_paths, 'Duplicate, case-colliding or circular roadmap manifest member')
        casefold_paths.add(path.casefold())
        c.require(entry['mode'] in ('100644', '100755') and isinstance(entry['sha256'], str)
                  and re.fullmatch(r'[0-9a-f]{64}', entry['sha256']), 'Invalid roadmap file identity: ' + path)
        allowed = (entry['role'] == 'snapshot' and path.startswith(h['snapshot_root'] + '/')) or (
            entry['role'] == 'helper' and path in h['helper_paths'])
        c.require(allowed, 'File is outside the bounded roadmap publication scope: ' + path)
        if 'source' in entry:
            repo.path(entry['source'])
            c.require(entry['source'] in h['snapshot_source_paths'] or entry['source'].startswith(h['snapshot_source_prefix']),
                      'Snapshot source is outside the reviewed governance allowlist: ' + entry['source'])
            c.require(manifest['local_bindings'].get(entry['source']) == entry['sha256'],
                      'Snapshot source is not bound to identical raw bytes: ' + path)
        elif entry['role'] == 'snapshot':
            c.require(path in {h['snapshot_root'] + '/' + name for name in h['generated_snapshot_names']},
                      'Unreviewed generated roadmap snapshot path: ' + path)
        files[path] = {'sha256': entry['sha256'], 'mode': entry['mode']}
    _accepted_closure(c, repo, manifest)
    c.require(len({p.casefold() for p in manifest['local_bindings']}) == len(manifest['local_bindings']),
              'Case-colliding local governance bindings')
    for path, value in manifest['local_bindings'].items():
        repo.path(path)
        c.require(isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value), 'Invalid local governance binding: ' + path)
        if current:
            c.require(c.digest(_read(c, repo, path)) == value, 'Local governance binding changed: ' + path)
    receipt_path = h['snapshot_root'] + '/receipt.json'
    c.require(receipt_path in files, 'Roadmap public receipt is missing from the bounded snapshot')
    receipt = _json(c, _read(c, repo, receipt_path), receipt_path)
    receipt_keys = {'schema', *_TARGET, 'parent_tag', 'parent_tag_object', 'parent_journal_sha256',
                    'parent_receipt_sha256', 'manifest_payload_sha256'}
    c.require(set(receipt) == receipt_keys and type(receipt.get('schema')) is int and receipt['schema'] == 1
              and all(receipt.get(k) == h[k] for k in receipt_keys - {'schema', 'manifest_payload_sha256'})
              and receipt.get('manifest_payload_sha256') == _receipt_payload(c, manifest),
              'Roadmap public receipt differs from its exact noncircular payload')
    files[spec_path] = {'sha256': c.digest(spec_raw), 'mode': '100644'}
    files[h['manifest_path']] = {'sha256': c.digest(manifest_raw), 'mode': '100644'}
    if current:
        for path, entry in files.items():
            c.require(not repo.ignored(path), 'Roadmap publication cannot force-add an ignored path: ' + path)
            c.require(c.digest(_read(c, repo, path)) == entry['sha256'], 'Roadmap publication bytes changed: ' + path)
            if os.name != 'nt':
                mode = '100755' if repo.path(path).stat().st_mode & 0o111 else '100644'
                c.require(mode == entry['mode'], 'Roadmap publication file mode changed: ' + path)
    return {'spec': spec, 'manifest': manifest, 'files': files,
            'spec_sha256': c.digest(spec_raw), 'manifest_sha256': c.digest(manifest_raw)}


def _outside_index(c, repo, paths):
    # Protect flags, modes, stages and content outside the explicitly owned set.
    chunks = []
    for args in (('ls-files', '--stage', '-z'), ('ls-files', '-v', '-z')):
        rows = repo.git(*args).split(b'\0')
        kept = [row for row in rows if row and os.fsdecode(row.split(b'\t', 1)[1] if b'\t' in row else row[2:]) not in paths]
        chunks.append(b'\0'.join(kept))
    return c.digest(b'\0\0'.join(chunks))


def _index(c, repo, inputs, journal, staged=False):
    paths = set(inputs['files'])
    c.require(not repo.git('ls-files', '-u'), 'Unmerged index during roadmap publication; preserve and resolve it')
    c.require(_outside_index(c, repo, paths) == journal['index_outside'],
              'Unexpected index changes outside the roadmap transaction; preserve them for recovery')
    actual = repo.staged()
    expected = set(journal['changed_files'])
    c.require(actual == expected if staged else actual <= expected,
              'Unexpected staged path set during roadmap publication; preserve it for recovery')
    c.staged_matches_worktree(repo, actual)
    for row in repo.git('ls-files', '-v', '-z', '--', *sorted(paths)).split(b'\0'):
        if row:
            c.require(row[:1] == b'H', 'Special index flags on roadmap transaction files require recovery')
    for path in actual:
        data = repo.git('ls-files', '--stage', '-z', '--', path).split(b'\0')[0]
        mode, oid, stage = data.split(b'\t', 1)[0].decode().split()
        c.require(stage == '0' and mode == inputs['files'][path]['mode']
                  and c.digest(repo.git('cat-file', 'blob', oid)) == inputs['files'][path]['sha256'],
                  'Staged roadmap raw bytes/mode differ from the manifest: ' + path)


def _physics_worktree(c, repo):
    roots = tuple(ROADMAP_PUBLICATION['physics_trees'])
    dirty = repo.names('diff', '--name-only', '--no-renames', '-z', ROADMAP_PUBLICATION['parent'], '--', *roots)
    dirty |= repo.names('ls-files', '--others', '--exclude-standard', '-z', '--', *roots)
    c.require(not dirty, 'Unapproved Physics changes block governance publication: ' + ', '.join(sorted(dirty)))
    # Also inspect bytes hidden by assume-unchanged/skip-worktree index flags.
    c.staged_matches_worktree(repo, repo.names('ls-files', '-z', '--', *roots))


def _tag(c, repo, commit=None, expected_object=None):
    ref = 'refs/tags/' + ROADMAP_PUBLICATION['tag']
    exists = repo.git('show-ref', '--verify', '--quiet', ref, allow_failure=True)
    c.require(exists.returncode in (0, 1), 'Cannot inspect roadmap tag')
    if exists.returncode == 1:
        c.require(expected_object is None, 'Recorded roadmap annotated tag is missing')
        return None
    c.require(commit is not None and repo.text('cat-file', '-t', ref) == 'tag'
              and repo.text('rev-parse', ref + '^{commit}') == commit,
              'Roadmap tag collision; preserve the existing ref')
    oid = repo.text('rev-parse', ref)
    c.require(expected_object is None or oid == expected_object, 'Recorded roadmap tag object changed')
    return oid


def _remote(c, repo, commit=None, tag_object=None, complete=False):
    h = ROADMAP_PUBLICATION
    branch, tag = 'refs/heads/' + h['branch'], 'refs/tags/' + h['tag']
    refs = {}
    for line in repo.text('ls-remote', '--', h['push_destination'], branch, tag, tag + '^{}').splitlines():
        oid, name = line.split('\t')
        c.require(name not in refs and name in (branch, tag, tag + '^{}'), 'Unexpected duplicate remote ref')
        refs[name] = oid
    c.require(refs.get(branch) in ({commit} if complete else {h['parent'], commit} - {None}),
              'Remote roadmap branch diverged; no force publication is permitted')
    if tag in refs or tag + '^{}' in refs or complete:
        c.require(tag_object is not None and refs.get(tag) == tag_object and refs.get(tag + '^{}') == commit,
                  'Remote roadmap tag collision or incomplete annotated tag')
    return refs


def _commit(c, repo, commit, inputs, expected_tree):
    h = ROADMAP_PUBLICATION
    c.require(expected_tree and repo.text('rev-parse', commit + '^{tree}') == expected_tree
              and repo.text('show', '-s', '--format=%P', commit) == h['parent'],
              'Roadmap commit is not the exact recorded tree with sole approved Phase25 parent')
    changed = repo.names('diff-tree', '--no-commit-id', '--name-only', '--no-renames', '-r', '-z', h['parent'], commit)
    expected_changed = {path for path, entry in inputs['files'].items() if _entry(c, repo, h['parent'], path) != entry}
    c.require(changed == expected_changed, 'Roadmap commit differs from the exact manifest change set')
    for path, entry in inputs['files'].items():
        c.require(_entry(c, repo, commit, path) == entry, 'Committed roadmap bytes/mode changed: ' + path)
    for path, tree in h['physics_trees'].items():
        c.require(repo.text('rev-parse', commit + ':' + path) == tree, 'Roadmap commit changed the approved Physics tree')


def _load(c, repo, inputs):
    path = _safe_private(c, _journal(repo), required=False)
    if not path.exists():
        return None
    record = _json(c, path.read_bytes(), 'roadmap journal')
    c.require(type(record.get('schema')) is int and record['schema'] == 1 and record.get('identity') == ROADMAP_PUBLICATION['identity']
              and record.get('status') in _STATUSES and record.get('spec_sha256') == inputs['spec_sha256']
              and record.get('manifest_sha256') == inputs['manifest_sha256']
              and record.get('files') == inputs['files'], 'Roadmap recovery journal identity differs; preserve it')
    c.require(isinstance(record.get('transaction'), dict), 'Roadmap transaction intent is missing')
    c.require({'expected_tree', 'commit', 'tag_object', 'branch_pushed', 'tag_pushed'} <= set(record['transaction'])
              and isinstance(record.get('changed_files'), list) and isinstance(record.get('index_outside'), str)
              and isinstance(record.get('state_before_sha256'), str), 'Incomplete roadmap recovery journal')
    return record


def _save(c, repo, record, status):
    record['status'] = status
    path = _safe_private(c, _journal(repo), required=False)
    c.atomic(path, c.encode_json(record))


def _authorization(c, repo, inputs, authorization_path):
    c.require(authorization_path == ROADMAP_PUBLICATION['authorization_path'],
              'Roadmap publication requires its fixed separate owner authorization path')
    raw = _read(c, repo, authorization_path)
    auth = _json(c, raw, authorization_path)
    c.require(set(auth) == {'schema', 'status', *_TARGET, 'spec_sha256'} and type(auth.get('schema')) is int and auth['schema'] == 1
              and auth.get('status') == 'OWNER_AUTHORIZED_PUBLICATION'
              and auth.get('spec_sha256') == inputs['spec_sha256']
              and all(auth.get(k) == ROADMAP_PUBLICATION[k] for k in _TARGET),
              'Separate exact owner publication authorization is required before any transaction mutation')
    return c.digest(raw)


def _no_active_phase(c, repo):
    for path in repo.journal('25').parent.glob('PHASE_*.json'):
        prior = _json(c, _safe_private(c, path).read_bytes(), path.name)
        c.require(prior.get('status') == 'COMPLETE', 'An unfinished PRE_EDITOR phase blocks roadmap publication: ' + path.name)


def _bound_authorization(c, repo, inputs, record):
    c.require(_authorization(c, repo, inputs, record['authorization_path']) == record['authorization_sha256'],
              'Owner publication authorization changed during the roadmap transaction')


def check(c, repo, spec_path):
    """Read-only local readiness/recovery check; this grants no publication."""
    _environment(c, repo)
    _, state = repo.state()
    _predecessor(c, repo, state)
    _no_active_phase(c, repo)
    inputs = _inputs(c, repo, spec_path)
    record = _load(c, repo, inputs)
    head = repo.text('rev-parse', 'HEAD')
    if record is None:
        c.clean_index(repo)
        c.require(head == ROADMAP_PUBLICATION['parent'], 'Roadmap publication must begin at approved Phase25')
        _tag(c, repo)
    else:
        c.require(c.digest(repo.state()[0]) == record['state_before_sha256'],
                  'Workflow state changed during roadmap publication; preserve it for recovery')
        tx = record['transaction']
        if head == ROADMAP_PUBLICATION['parent']:
            c.require(record['status'] in ('PREPARED', 'STAGED', 'COMMIT_PENDING') and not tx.get('commit'),
                      'Roadmap HEAD moved backward after commit; preserve history')
            _index(c, repo, inputs, record)
            _tag(c, repo)
        else:
            c.require(record['status'] in _STATUSES[2:] and tx.get('commit') in (None, head),
                      'Unexpected roadmap commit or missing pending commit intent')
            _commit(c, repo, head, inputs, tx.get('expected_tree'))
            c.clean_index(repo)
            _index(c, repo, inputs, record)
            tag_object = _tag(c, repo, head, tx.get('tag_object'))
            c.require(tag_object is None or tx.get('tag_object') == tag_object or record['status'] == 'TAG_PENDING',
                      'Unrecorded roadmap tag lacks a pending creation intent')
    _physics_worktree(c, repo)
    return {'status': 'READY_FOR_OWNER_PUBLICATION_AUTHORIZATION' if record is None else record['status'],
            'identity': ROADMAP_PUBLICATION['identity'], 'spec_sha256': inputs['spec_sha256'],
            'publication_authorized': False, 'files': sorted(inputs['files'])}


def publish(c, repo, spec_path, authorization_path):
    """Resume only this exact owner-authorized governance commit/tag transaction."""
    _environment(c, repo)
    inputs = _inputs(c, repo, spec_path)
    auth_sha256 = _authorization(c, repo, inputs, authorization_path)
    check(c, repo, spec_path)
    h = ROADMAP_PUBLICATION
    record = _load(c, repo, inputs)
    if record is None:
        _remote(c, repo)
        changed = sorted(path for path, entry in inputs['files'].items()
                         if _entry(c, repo, h['parent'], path) != entry)
        c.require(changed, 'Roadmap publication has no changes')
        record = {'schema': 1, 'identity': h['identity'], 'spec_sha256': inputs['spec_sha256'],
                  'manifest_sha256': inputs['manifest_sha256'], 'files': inputs['files'],
                  'authorization_sha256': auth_sha256, 'authorization_path': authorization_path,
                  'state_before_sha256': c.digest(repo.state()[0]),
                  'index_at_start': c.index_identity(repo), 'index_outside': _outside_index(c, repo, set(inputs['files'])),
                  'changed_files': changed, 'transaction': {'expected_tree': None, 'commit': None,
                  'tag_object': None, 'branch_pushed': False, 'tag_pushed': False}}
        _save(c, repo, record, 'PREPARED')
    c.require(record.get('authorization_sha256') == auth_sha256
              and record.get('authorization_path') == authorization_path, 'Roadmap publication authorization changed during recovery')
    tx = record['transaction']
    if record['status'] == 'COMPLETE':
        _remote(c, repo, tx['commit'], tx['tag_object'], complete=True)
        return {'status': 'ALREADY_COMPLETE', 'identity': h['identity'], 'commit': tx['commit'], 'tag': h['tag']}
    head = repo.text('rev-parse', 'HEAD')
    if head == h['parent']:
        _index(c, repo, inputs, record)
        _remote(c, repo)
        _bound_authorization(c, repo, inputs, record)
        # Literal pathspecs and a closed path list; ignored inputs already failed.
        repo.git('--literal-pathspecs', 'add', '--', *sorted(inputs['files']))
        _index(c, repo, inputs, record, staged=True)
        tree = repo.text('write-tree')
        c.require(tx['expected_tree'] in (None, tree), 'Roadmap staged tree differs from recorded commit intent')
        tx['expected_tree'] = tree
        tx['staged_index'] = c.index_identity(repo)
        _save(c, repo, record, 'STAGED')
        _save(c, repo, record, 'COMMIT_PENDING')
        _bound_authorization(c, repo, inputs, record)
        repo.git('commit', '-m', 'pre-editor: publish accepted Milestone C roadmap governance')
        head = repo.text('rev-parse', 'HEAD')
    _commit(c, repo, head, inputs, tx['expected_tree'])
    c.clean_index(repo)
    _index(c, repo, inputs, record)
    c.require(tx['commit'] in (None, head), 'Roadmap HEAD differs from the recorded commit')
    tx['commit'] = head
    if record['status'] in _STATUSES[:3]:
        _save(c, repo, record, 'COMMITTED')
    # Preserve unrelated work and verify no commit hook changed publication bytes.
    _environment(c, repo)
    _inputs(c, repo, spec_path)
    _predecessor(c, repo, repo.state()[1])
    _physics_worktree(c, repo)
    c.require(c.digest(repo.state()[0]) == record['state_before_sha256'], 'Workflow state changed during roadmap commit')
    tag_object = _tag(c, repo, head, tx['tag_object'])
    if tag_object is None:
        c.require(record['status'] in ('COMMITTED', 'TAG_PENDING'), 'Roadmap annotated tag disappeared after creation')
        _remote(c, repo, head)
        _save(c, repo, record, 'TAG_PENDING')
        _bound_authorization(c, repo, inputs, record)
        repo.git('tag', '-a', h['tag'], head, '-m', 'Owner-approved Milestone C roadmap governance; ' + h['snapshot_root'] + '/receipt.json')
        tag_object = _tag(c, repo, head)
    else:
        c.require(tx['tag_object'] == tag_object or record['status'] == 'TAG_PENDING',
                  'Unrecorded roadmap tag lacks a pending creation intent')
    tx['tag_object'] = tag_object
    if record['status'] in ('COMMITTED', 'TAG_PENDING'):
        _save(c, repo, record, 'TAGGED')
    _remote(c, repo, head, tag_object)
    _save(c, repo, record, 'PUSH_PENDING')
    _environment(c, repo)
    _predecessor(c, repo, repo.state()[1])
    _bound_authorization(c, repo, inputs, record)
    repo.git('-c', 'push.followTags=false', 'push', '--atomic', '--no-follow-tags', h['push_destination'],
             head + ':refs/heads/' + h['branch'], 'refs/tags/' + h['tag'] + ':refs/tags/' + h['tag'])
    _remote(c, repo, head, tag_object, complete=True)
    c.require(repo.text('rev-parse', 'HEAD') == head, 'Roadmap HEAD changed during publication')
    c.clean_index(repo)
    _index(c, repo, inputs, record)
    _inputs(c, repo, spec_path)
    _tag(c, repo, head, tag_object)
    _physics_worktree(c, repo)
    _environment(c, repo)
    _bound_authorization(c, repo, inputs, record)
    _predecessor(c, repo, repo.state()[1])
    c.require(c.digest(repo.state()[0]) == record['state_before_sha256'], 'Workflow state changed during roadmap publication')
    tx['branch_pushed'] = tx['tag_pushed'] = True
    _save(c, repo, record, 'COMPLETE')
    return {'status': 'COMPLETE', 'identity': h['identity'], 'commit': head, 'tag': h['tag'],
            'branch_pushed': True, 'tag_pushed': True, 'committed_files': sorted(inputs['files'])}


def phase26_gate(c, repo, baseline, state):
    """Admit only the durable, fully published governance bridge above Phase25.

    Prospective Phase26 Physics edits are validated by that phase's own contract;
    this gate compares the immutable bridge tree, never the current Physics work.
    """
    _environment(c, repo)
    _predecessor(c, repo, state)
    inputs = _inputs(c, repo, ROADMAP_PUBLICATION['spec_path'])
    record = _load(c, repo, inputs)
    c.require(record is not None and record['status'] == 'COMPLETE', 'Phase26 requires a completed roadmap governance publication')
    tx = record['transaction']
    c.require(tx.get('commit') == baseline and tx.get('branch_pushed') is True and tx.get('tag_pushed') is True
              and tx.get('tag_object'), 'Phase26 baseline differs from the exact published roadmap bridge')
    _commit(c, repo, baseline, inputs, tx.get('expected_tree'))
    _tag(c, repo, baseline, tx['tag_object'])
    auth_sha256 = _authorization(c, repo, inputs, ROADMAP_PUBLICATION['authorization_path'])
    c.require(record.get('authorization_path') == ROADMAP_PUBLICATION['authorization_path']
              and record.get('authorization_sha256') == auth_sha256, 'Completed roadmap owner authorization changed')
    return {'identity': ROADMAP_PUBLICATION['identity'], 'commit': baseline, 'tag': ROADMAP_PUBLICATION['tag'],
            'tag_object': tx['tag_object'], 'journal_sha256': c.digest(_safe_private(c, _journal(repo)).read_bytes()),
            'spec_sha256': inputs['spec_sha256'], 'manifest_sha256': inputs['manifest_sha256'],
            'authorization_sha256': auth_sha256}


def phase26_contract_inputs(c, repo):
    """Exact repository inputs the existing Phase26 seal must retain."""
    h = ROADMAP_PUBLICATION
    return {h['spec_path'], h['manifest_path'], h['snapshot_root'] + '/receipt.json', h['authorization_path']}
