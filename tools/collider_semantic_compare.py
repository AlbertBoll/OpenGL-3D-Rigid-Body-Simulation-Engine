"""Exact ordered semantic field-bit comparison of completed diagnostic captures. No Physics calls."""
import argparse,ctypes as C,hashlib,json,struct
from pathlib import Path
import collider_capture_analysis as a
EXCLUDED={'event.a','event.b','event.constraintToken','contact.m_BodyA','contact.m_BodyB','event.context.a','event.context.b','event.contact.m_BodyA','event.contact.m_BodyB'}

def leaves(typ,offset=0,path=''):
    if issubclass(typ,C.Array):
        if issubclass(typ._type_,C._SimpleCData):return [(offset,C.sizeof(typ),path,typ)]
        return [x for i in range(typ._length_) for x in leaves(typ._type_,offset+i*C.sizeof(typ._type_),f'{path}[{i}]')]
    if issubclass(typ,C.Structure):
        out=[]
        for name,t in typ._fields_:
            key=f'{path}.{name}' if path else name
            if key in EXCLUDED or key.startswith('event.patch[') and key.endswith(('.m_BodyA','.m_BodyB')):continue
            out.extend(leaves(t,offset+getattr(typ,name).offset,key))
        return out
    return [(offset,C.sizeof(typ),path,typ)]

def fields(typ):
    raw=leaves(typ);merged=[]
    for off,size,path,t in raw:
        if merged and merged[-1][0]+merged[-1][1]==off:merged[-1]=(merged[-1][0],merged[-1][1]+size)
        else:merged.append((off,size))
    return raw,merged

def records(path,size,rawhash):
    assert path.stat().st_size%size==0,f'truncated {path}'
    with path.open('rb') as f:
        while block:=f.read(size*1024):
            rawhash.update(block)
            for off in range(0,len(block),size):yield block[off:off+size]

def decoded(data,typ):
    if issubclass(typ,C.Array):return list(typ.from_buffer_copy(data))
    return typ.from_buffer_copy(data).value

def compare(left,right,typ):
    fieldlist,spans=fields(typ);size=C.sizeof(typ);lh=hashlib.sha256();rh=hashlib.sha256();ls=hashlib.sha256();rs=hashlib.sha256();li=records(left,size,lh);ri=records(right,size,rh);count=0;diff=0;examples=[]
    while True:
        x=next(li,None);y=next(ri,None)
        if x is None and y is None:break
        xx=b''.join(x[o:o+n] for o,n in spans) if x is not None else None;yy=b''.join(y[o:o+n] for o,n in spans) if y is not None else None
        if xx is not None:ls.update(xx)
        if yy is not None:rs.update(yy)
        if xx!=yy:
            diff+=1
            if len(examples)<8:
                changes=[]
                if x is not None and y is not None:
                    for off,n,path,t in fieldlist:
                        if x[off:off+n]!=y[off:off+n]:changes.append({'field':path,'left':decoded(x[off:off+n],t),'right':decoded(y[off:off+n],t),'left_hex':x[off:off+n].hex(),'right_hex':y[off:off+n].hex()})
                examples.append({'row':count,'left_tick':struct.unpack_from('<Q',x)[0] if x else None,'right_tick':struct.unpack_from('<Q',y)[0] if y else None,'left_missing':x is None,'right_missing':y is None,'changed_fields':changes})
        count+=1
    return {'left_file':str(left),'right_file':str(right),'raw_row_bytes':size,'compared_semantic_bytes_per_row':sum(n for _,n in spans),'left_rows':left.stat().st_size//size,'right_rows':right.stat().st_size//size,'rows_different':diff,'semantic_match':diff==0,'left_raw_sha256':lh.hexdigest(),'right_raw_sha256':rh.hexdigest(),'left_semantic_sha256':ls.hexdigest(),'right_semantic_sha256':rs.hexdigest(),'first_differences':examples}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('original',type=Path);ap.add_argument('replay',type=Path);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();root=Path(__file__).resolve().parents[1];target=args.output.resolve();assert target.is_relative_to(root/'campaign') and not target.exists()
    for p in (args.original,args.replay):
        assert a.load(p/'observer-status.json')['status']=='CAPTURE_COMPLETE';a.verify_layout(a.load(p/'observer-layout.json'));a.verify_queries(a.load(p/'observer-query-layout.json'))
    results={name:compare(args.original/name,args.replay/name,typ) for name,typ in [('observer-states.bin',a.SR),('observer-contacts.bin',a.CR),('observer-events.bin',a.ER),('observer-queries.bin',a.QR)]}
    report={'status':'SEMANTIC_FIELD_MATCH' if all(x['semantic_match'] for x in results.values()) else 'SEMANTIC_FIELD_DIFFERENCE','scope':'Ordered recorded Physics states, contacts, events and completed query events; every included scalar/array bit is compared, including signed zero.','excluded_fields':sorted(EXCLUDED)+['event.patch[*].m_BodyA','event.patch[*].m_BodyB','all native struct padding','observer wall/timing/elapsed/render logs (not in these four files)'],'identity_policy':'Body ordinals and typed query identity slots/generations are compared. Native pointers/constraintToken are excluded; within-run lifecycle evidence remains in original events.','files':results,'script_sha256':a.sha(Path(__file__)),'parser_sha256':a.sha(Path(a.__file__)),'limits':['This establishes recorded-path semantic parity only, not whole-program memory equivalence or uninstrumented behavior.','A match is not a correctness/performance qualification PASS or human visual acceptance.']};a.write(target,report);print(json.dumps({'output':str(target),'status':report['status'],'files':{k:{x:v[x] for x in ('left_rows','right_rows','rows_different')} for k,v in results.items()}},indent=2))
if __name__=='__main__':main()
