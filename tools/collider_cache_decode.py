"""Read-only decoder for diagnostic CRCACH01 canonical semantic images. No PASS inference."""
import hashlib,math,struct
from pathlib import Path
class Wire:
    def __init__(self,data):self.data=data;self.at=0
    def get(self,fmt):
        fmt='<'+fmt;size=struct.calcsize(fmt)
        if self.at+size>len(self.data):raise ValueError('truncated canonical wire')
        x=struct.unpack_from(fmt,self.data,self.at);self.at+=size
        return x[0] if len(x)==1 else list(x)
    def flag(self):
        x=self.get('B')
        if x not in (0,1):raise ValueError('non-boolean canonical flag')
        return bool(x)
    def end(self):
        if self.at!=len(self.data):raise ValueError(f'unconsumed wire bytes {self.at}/{len(self.data)}')
    def vec(self):return self.get('3f')
    def matrix(self):return [self.vec() for _ in range(3)]
    def state(self):return {'position':self.vec(),'orientation_wxyz':self.get('4f'),'linear':self.vec(),'angular':self.vec(),'inactive':self.get('d'),'sleeping':self.flag()}
    def contact(self):
        d={k:self.vec() for k in ('world_a','world_b','local_a','local_b','normal_b_to_a')}
        d.update(separation=self.get('f'),toi=self.get('f'),feature_a=self.get('Q'),feature_b=self.get('Q'));return d
    def record(self):
        n=self.get('I')
        if n>4096 or self.at+n>len(self.data):raise ValueError('invalid canonical record length')
        b=self.data[self.at:self.at+n];self.at+=n;return b

def physical(w):
    d={'ordinal':w.get('I'),'state':w.state(),'type':w.get('i')}
    for k in ('inverse_mass','restitution','friction'):d[k]=w.get('f')
    for k in ('layer','mask','shape'):d[k]=w.get('I')
    d.update(shape_revision=w.get('Q'),shape_type=w.get('i'),spin_length=w.get('f'),rolling_length=w.get('f'),sleep_linear=w.get('f'),sleep_angular=w.get('f'),sleep_dwell=w.get('d'))
    q={k:w.vec() for k in ('position','linear','angular')};q['orientation_wxyz']=w.get('4f')
    for k in ('inverse_mass','restitution','friction'):q[k]=w.get('f')
    for k in ('layer','mask','shape'):q[k]=w.get('I')
    q.update(shape_revision=w.get('Q'),type=w.get('i'));d['sleep_source']=q
    for k in ('sleep_source_valid','sleep_shape_valid','wake_requested','external_mutation','in_physics_step'):d[k]=w.flag()
    d['activation_island']=w.get('Q');return d

def body(w):
    d={'physical':physical(w)};c={};d['derived']=c
    c.update(rotation_source_orientation=w.get('4f'),body_to_world=w.matrix(),world_to_body=w.matrix(),rotation_revision=w.get('Q'),rotation_valid=w.flag())
    c.update(center_source_position=w.vec(),center_source_shape=w.get('I'),center_source_shape_revision=w.get('Q'),center_source_rotation_revision=w.get('Q'),center_of_mass_world=w.vec(),center_valid=w.flag())
    c.update(inertia_source_inverse_mass=w.get('f'),inertia_source_shape=w.get('I'),inertia_source_shape_revision=w.get('Q'),inertia_body=w.matrix(),inverse_inertia_body=w.matrix(),body_inertia_revision=w.get('Q'),body_inertia_valid=w.flag())
    c.update(world_inertia_source_body_revision=w.get('Q'),world_inertia_source_rotation_revision=w.get('Q'),inverse_inertia_world=w.matrix(),inertia_world=w.matrix(),world_inertia_valid=w.flag())
    c.update(bounds_source_position=w.vec(),bounds_source_shape=w.get('I'),bounds_source_shape_revision=w.get('Q'),bounds_source_rotation_revision=w.get('Q'),bounds_min=w.vec(),bounds_max=w.vec(),bounds_valid=w.flag())
    d['copied_inertia_check']={'cached_body_equals_fresh':w.flag(),'cached_world_equals_fresh':w.flag(),'all_finite':w.flag(),'cached_body':w.matrix(),'fresh_body':w.matrix(),'cached_world':w.matrix(),'fresh_world':w.matrix()};return d

def manifold(w):
    d={'a':w.get('I'),'b':w.get('I'),'count':w.get('I')}
    if d['count']>4:raise ValueError('invalid manifold contact count')
    for k in ('stamp_a','stamp_b'):d[k]={'identity':w.get('I'),'shape':w.get('I'),'revision':w.get('Q')}
    d['contacts']=[]
    for slot in range(d['count']):
        q={'slot':slot,'a':w.get('I'),'b':w.get('I'),'contact':w.contact(),'normal_b_local':w.vec(),'constraint_a':w.get('I'),'constraint_b':w.get('I'),'anchor_a':w.vec(),'anchor_b':w.vec(),'normal_a_local':w.vec(),'friction':w.get('f'),'jacobian_float32_rows':[w.get('12f') for _ in range(3)],'cached_lambda_float32':w.get('3f'),'position_correction_enabled':w.flag(),'spin_impulse':w.get('d'),'spin_length':w.get('f'),'rolling_impulse':w.get('2d'),'rolling_length':w.get('f')}
        d['contacts'].append(q)
    return d

def changes(a,b,path=''):
    if type(a)!=type(b):return [path]
    if isinstance(a,dict):return [v for k in a.keys()|b.keys() for v in (changes(a[k],b[k],path+'.'+k) if k in a and k in b else [path+'.'+k])]
    if isinstance(a,list):return [path] if len(a)!=len(b) else [v for i,(x,y) in enumerate(zip(a,b)) for v in changes(x,y,f'{path}[{i}]')]
    return [] if a==b else [path]

def nonfinite(d,path=''):
    if isinstance(d,dict):return [v for k,x in d.items() for v in nonfinite(x,path+'.'+k)]
    if isinstance(d,list):return [v for i,x in enumerate(d) for v in nonfinite(x,f'{path}[{i}]')]
    return [path] if isinstance(d,float) and not math.isfinite(d) else []

def decode(path):
    data=Path(path).read_bytes()
    if len(data)>4*1024*1024 or data[:8]!=b'CRCACH01':raise ValueError('invalid canonical image magic/size')
    w=Wire(data);w.at=8;schema,stage,nb,nm,ns=w.get('5I');tick,frame=w.get('2Q')
    if schema!=1 or nb>192 or nm>256 or ns>576:raise ValueError('invalid canonical header')
    p=Wire(w.record());policy={'timestep':p.get('d'),'gravity':p.vec(),'passes':p.get('i'),'sleeping_enabled':p.flag(),'paused':p.flag()};p.end()
    out={'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'schema':schema,'stage':stage,'body_count':nb,'manifold_count':nm,'shape_count':ns,'completed_physics_steps':tick,'frame':frame,'policy':policy,'bodies':[],'manifolds':[]}
    for i in range(nb):
        rawp=w.record();rawb=w.record();p=Wire(rawp);d=physical(p);p.end();p=Wire(rawb);b=body(p);p.end()
        if len(rawp)!=230 or len(rawb)!=762 or rawb[:len(rawp)]!=rawp or d['ordinal']!=i:raise ValueError('canonical body prefix/ordinal/size mismatch')
        b['wire_sha256']=hashlib.sha256(rawb).hexdigest();out['bodies'].append(b)
    for i in range(nm):
        raw=w.record();p=Wire(raw);m=manifold(p);p.end()
        if len(raw)!=44+341*m['count']:raise ValueError('canonical manifold size mismatch')
        m['order']=i;m['wire_sha256']=hashlib.sha256(raw).hexdigest();out['manifolds'].append(m)
    w.end();out['nonfinite_fields']=nonfinite(out)
    for m in out['manifolds']:
        if any(x>=nb for x in [m['a'],m['b'],m['stamp_a']['identity'],m['stamp_b']['identity']]):raise ValueError('unbound canonical manifold identity')
        for q in m['contacts']:
            if (q['a'],q['b'],q['constraint_a'],q['constraint_b'])!=(m['a'],m['b'],m['a'],m['b']):raise ValueError('canonical constraint/contact body mismatch')
    return out

def evidence(source,selected):
    names=('observer-cache-initial.bin','observer-cache-pre-operation.bin','observer-cache-command.bin')
    images={n:decode(Path(source)/n) for n in names if (Path(source)/n).exists()}
    summary={'available_images':list(images),'complete_three_image_set':len(images)==3,'decoder':'CRCACH01 explicit packed Wire; all record lengths consumed','equivalence':'Evidence only; no cross-run match or whole-program equivalence is inferred.'}
    if names[1] in images and names[2] in images:
        a,b=images[names[1]],images[names[2]]
        summary.update(last_end_to_command_physical_exact=all(x['physical']==y['physical'] for x,y in zip(a['bodies'],b['bodies'])) and len(a['bodies'])==len(b['bodies']),last_end_to_command_manifolds_exact=a['manifolds']==b['manifolds'],policy_changed_fields=changes(a['policy'],b['policy']),body_changed_fields={str(i):changes(x,y) for i,(x,y) in enumerate(zip(a['bodies'],b['bodies'])) if x!=y},selected_command_body=b['bodies'][selected],selected_command_manifolds=[m for m in b['manifolds'] if selected in (m['a'],m['b'])])
    return summary,images
