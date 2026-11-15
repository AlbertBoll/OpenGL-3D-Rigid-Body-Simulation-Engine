"""Record this uncommitted Owner handoff; no publication or historical rewrites."""
from pathlib import Path
import datetime,difflib,hashlib,json,shutil,subprocess

ROOT=Path(__file__).resolve().parents[1]
DOC=ROOT/'docs/ui/candidates/impulse-editor-01'
LOG=ROOT/'logs/ui/impulse-editor-01'
def digest(path):
    b=path.read_bytes();return dict(sha256=hashlib.sha256(b).hexdigest(),bytes=len(b))
def write(name,value):
    p=DOC/name
    if p.exists():raise RuntimeError('Preserve existing handoff: '+str(p))
    p.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def git(*args):
    return subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),*args],cwd=ROOT,text=True).strip()

entry=json.loads((DOC/'preservation.json').read_text())
previous=ROOT/entry['previous_manifest']['path']
assert digest(previous)=={k:entry['previous_manifest'][k] for k in ['sha256','bytes']}
assert git('rev-parse','HEAD')==entry['head']
assert git('ls-files','--stage','--','RigidBodySimulation/src/RigidBodySimulation.cpp')==entry['canonical_index']
assert not git('diff','--cached','--name-only')
for name,identity in entry['source_inputs'].items():
    assert digest(LOG/'before/source'/name)==identity,name

added=[
 'GEngine/src/Physics/ImpulseEditor.inl','GEngine/src/Physics/JoltImpulseEditor.inl',
 'RigidBodySimulation/include/RbsImpulseEditor.h','RigidBodySimulation/src/RbsImpulseEditor.inl',
 'RigidBodySimulation/tests/ImpulseEditorChecks.h','RigidBodySimulation/tests/ImpulseBoundaryChecks.h',
 'docs/physics/RBS_IMPULSE_DEBUGGING.md','docs/ui/OWNER_REVIEW_STATUS.md',
 'tools/test_rbs_impulse_editor.py','tools/summarize_rbs_impulse_overlay.py','tools/record_rbs_impulse_candidate.py']
inputs={p:digest(ROOT/p) for p in sorted(set(entry['source_inputs'])|set(added))}
changed={p:dict(before=entry['source_inputs'].get(p),after=m) for p,m in inputs.items() if entry['source_inputs'].get(p)!=m}
write('changed-inputs.json',changed)
for p in ['RigidBodySimulation/src/RigidBodySimulation.cpp','RigidBodySimulation/src/RbsScene.cpp',
          'GEngine/src/Renderer/FrameSubmission.cpp']:
    assert inputs[p]==entry['source_inputs'][p]
for p in inputs:
    if p.endswith(('.glsl','.vert','.frag')):assert inputs[p]==entry['source_inputs'][p]
patch=[]
for p in changed:
    prior=LOG/'before/source'/p
    old=prior.read_text(encoding='utf-8-sig').splitlines(True) if prior.exists() else []
    patch.extend(difflib.unified_diff(old,(ROOT/p).read_text(encoding='utf-8-sig').splitlines(True),fromfile='entry/'+p,tofile='candidate/'+p))
# This is the newly prepared current candidate diff, never an old candidate's file.
(DOC/'entry-delta.patch').write_text(''.join(patch),encoding='utf-8')
source=LOG/'final/source';source.mkdir(parents=True,exist_ok=False)
for name in inputs:
    dst=source/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,dst)
products={}
for config in ['Debug','Release']:
    src=ROOT/f'bin/{config}/RigidBodySimulation/RigidBodySimulation.exe'
    dst=LOG/f'final/{config}.exe';shutil.copy2(src,dst);products[config]=digest(dst)

evidence={p.relative_to(ROOT).as_posix():digest(p) for p in sorted(LOG.rglob('*')) if p.is_file() and
          not p.is_relative_to(LOG/'before') and not p.is_relative_to(LOG/'final/source')}
write('evidence-index.json',evidence)
reports={p.name:digest(p) for p in DOC.iterdir() if p.is_file()}
write('candidate-inputs.json',dict(candidate='impulse-editor-01',utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
 status='AWAITING OWNER VISUAL STUDIO RELEASE|x64 REBUILD SOLUTION AND MANUAL VISUAL ACCEPTANCE',
 phase26='HUMAN ACCEPTANCE BLOCKED',sphere_lattice='HUMAN PHYSICS VISUAL FAIL UNCHANGED',
 head=entry['head'],canonical_index=entry['canonical_index'],source_inputs=inputs,products=products,
 preservation_verification=dict(entry_source_count=len(entry['source_inputs']),entry_copy_drift=[],previous_manifest_unchanged=True,
 head_index_unchanged=True,canonical_source_unchanged=True,authored_scene_unchanged=True,highlight_renderer_shaders_unchanged=True),
 evidence=dict(final_builds=['build-debug-caption.log','build-release-caption.log'],
 complete_suites=['checks-debug-final/results.json','checks-release-final/results.json'],
 caption_checks=['checks-debug-caption/results.json','checks-release-caption/results.json'],
 desktop=['release-jolt','release-final','release-native-200'],reuse='REVIEW.md distinguishes the final caption-only amendment from unchanged core/preset coverage'),
 limits=['Dynamic only; mixed unsupported targets reject the whole batch','4096 API ceiling, impulse validation/performance through 512',
 'BallSocket/Hinge not present in this checkout','No physics-time Undo; session-only experiments','No full qualification or human acceptance'],reports=reports))
print(json.dumps(dict(source_inputs=len(inputs),changed_inputs=len(changed),evidence_files=len(evidence),products=products),indent=2))
