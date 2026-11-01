"""Independent ordinary RBS final-frame Gold reference oracle. Create-only reports."""
import array, hashlib, json, math, struct, sys, zlib
from pathlib import Path

folder, output, prior = map(Path, sys.argv[1:])
records = {}
for line in (folder/'widget-smoke.tsv').read_text().splitlines():
    row = line.split('\t')
    if row[0] in ('SAME_FRAME','PRESENTED','EXTRACTED','DESCRIPTOR','SUBMITTED'):
        records.setdefault(int(row[1]), {}).setdefault(row[0], []).append(row[2:])
checks = []
def check(ok, label, **detail):
    checks.append(dict(passed=bool(ok), label=label, **detail))

def png(path, data, w, h):
    def chunk(k, d):
        return struct.pack('>I',len(d))+k+d+struct.pack('>I',zlib.crc32(k+d)&0xffffffff)
    pixels = b''.join(b'\0'+data[y*w*4:(y+1)*w*4] for y in range(h-1,-1,-1))
    encoded = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
    if path.exists(): assert path.read_bytes()==encoded, "existing PNG evidence changed"
    else: path.write_bytes(encoded)

frames, masks, metadata = {}, {}, {}
check(set(records)==set(range(20)), 'all20 ordinary frame cases captured', actual=sorted(records))
for stage, rec in records.items():
    v = rec['PRESENTED'][0]
    fw,fh,tw,th = map(int,v[:4]); x0,y0,x1,y1 = map(float,v[4:8]); lw,lh = map(int,v[8:])
    p = folder/f'same-frame-{stage}'
    front = Path(str(p)+'-presented.rgba').read_bytes()
    back = Path(str(p)+'-pre-present.rgba').read_bytes()
    check(len(front)==fw*fh*4 and front==back, f'{stage}: complete ordinary UI backbuffer reaches presented framebuffer unchanged')
    # Sample the actual final viewport, not the offscreen render-target texture.
    final = bytearray(tw*th*4)
    for y in range(th):
        fy = fh-1-int((y1-(y+.5)*(y1-y0)/th)*fh/lh)
        for x in range(tw):
            fx = int((x0+(x+.5)*(x1-x0)/tw)*fw/lw)
            fi = (fy*fw+fx)*4; ti = (y*tw+x)*4
            final[ti:ti+4] = front[fi:fi+4]
    frames[stage] = final
    metadata[stage] = dict(target=[tw,th],window=[fw,fh],viewport=[x0,y0,x1,y1])
    if stage in (0,1,3,16,17,19): png(folder/f'uniform-gold-presented-{stage}.png',front,fw,fh)
    if 'SAME_FRAME' in rec:
        sx,sy,rw,rh,mw,mh,fmt,bits = map(int,rec['SAME_FRAME'][0][:8])
        data = array.array('f'); data.frombytes(Path(str(p)+'-mask.f32').read_bytes())
        check(len(data)==mw*mh*4 and bits>=2, f'{stage}: actual mask extent and stencil bits')
        masks[stage]=(sx,sy,rw,rh,mw,mh,fmt,data)

def cov(stage,x,y):
    if stage not in masks:return (0.,0.)
    sx,sy,rw,rh,mw,mh,fmt,data=masks[stage]
    x-=sx;y-=sy
    if not(0<=x<rw and 0<=y<rh):return (0.,0.)
    i=(y*mw+x)*4
    return (data[i]+(data[i+2] if fmt==34836 else 0.),data[i+1]+(data[i+3] if fmt==34836 else 0.))

tw,th=metadata[0]['target']
check(all(m==metadata[0] for m in metadata.values()),'ordinary viewport mapping stable across reference cases')
references={0:1,1:13,2:14,3:15}
rois={}; contours={}; mesh_points={}
for obj,stage in references.items():
    sx,sy,rw,rh,mw,mh,fmt,data=masks[stage]
    points=[(sx+x,sy+y) for y in range(rh) for x in range(rw) if cov(stage,sx+x,sy+y)[0]>0]
    check(len(points)>40,f'legacy single reference {obj}: actual visible mesh coverage',pixels=len(points))
    x0=max(8,min(x for x,y in points)-8); x1=min(tw-8,max(x for x,y in points)+9)
    y0=max(8,min(y for x,y in points)-8); y1=min(th-8,max(y for x,y in points)+9)
    rois[obj]=(x0,y0,x1,y1)
    pts={(x,y) for x,y in points if cov(stage,x,y)[0]>.25};mesh_points[obj]=pts
    contours[obj]=[(x,y) for x,y in pts if any((x+dx,y+dy) not in pts for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)))]

def compare_roi(a,b,roi):
    maximum=mismatch=changed=gold=0
    x0,y0,x1,y1=roi
    for y in range(y0,y1):
        for x in range(x0,x1):
            i=(y*tw+x)*4
            d=max(abs(a[i+c]-b[i+c]) for c in range(3))
            maximum=max(maximum,d);mismatch+=d>1;changed+=d>=30
            gold+=a[i]>=250 and 178<=a[i+1]<=190 and 16<=a[i+2]<=28
    return dict(max_channel_error=maximum,pixels_outside_1=mismatch,changed_by30=changed,gold_pixels=gold,pixels=(x1-x0)*(y1-y0))

objects={}
for stage,rec in records.items():
    classes={v[0]:int(v[1].split('=')[1]) for v in rec.get('SUBMITTED',[])}
    mesh_ids={int(v[0]):v[1] for v in rec['EXTRACTED']}
    objects[stage]=[]
    for obj,reference in references.items():
        cls=classes.get(mesh_ids[obj],0); expected=reference if cls else 0
        diff=compare_roi(frames[stage],frames[expected],rois[obj])
        visible=compare_roi(frames[stage],frames[0],rois[obj])
        # Collider-on case has independent cyan geometry beneath Gold. Its native
        # coverage must be identical, while final Gold pixels remain observable.
        if stage!=7:
            check(diff['pixels_outside_1']==0,f'{stage}/{obj}: final viewport equals '+('its legacy single Gold reference' if cls else 'unselected reference'),**diff)
        if cls:
            contour_visible=0
            for x,y in contours[obj]:
                best=0
                for dy in range(-4,5):
                    for dx in range(-4,5):
                        xx,yy=x+dx,y+dy
                        if dx*dx+dy*dy>16 or (xx,yy) in mesh_points[obj] or not(0<=xx<tw and 0<=yy<th):continue
                        i=(yy*tw+xx)*4
                        best=max(best,max(abs(frames[stage][i+c]-frames[0][i+c]) for c in range(3)))
                contour_visible+=best>=30
            visible['contour_delta30_fraction']=contour_visible/len(contours[obj])
            # Legacy Gold is alpha/MSAA filtered by ordinary UI presentation. Its
            # raw shader RGB need not occur in the final window. Exact per-mesh
            # legacy reference equality above controls color; retain the previous
            # Gold visibility criterion on the actual presented contour.
            check(visible['changed_by30']>=40 and visible['contour_delta30_fraction']>=.9,f'{stage}/{obj}: clearly present final legacy Gold silhouette',**visible)
        else:
            check(not any(cov(stage,x,y)[0]>0 for y in range(rois[obj][1],rois[obj][3]) for x in range(rois[obj][0],rois[obj][2])),f'{stage}/{obj}: unselected mesh has no mask coverage')
        max_cov=max_depth=0.
        for y in range(rois[obj][1],rois[obj][3]):
            for x in range(rois[obj][0],rois[obj][2]):
                actual=cov(stage,x,y); ref=cov(expected,x,y)
                max_cov=max(max_cov,abs(actual[0]-ref[0])); max_depth=max(max_depth,abs(actual[1]-ref[1]))
        check(max_cov<=1e-6 and max_depth<=3e-6,f'{stage}/{obj}: selected-union coverage and depth equal reference',coverage_error=max_cov,depth_error=max_depth)
        objects[stage].append(dict(object=obj,identity=mesh_ids[obj],classification=cls,reference=expected,final_comparison=diff,visible=visible))

for a,b in [(9,10),(16,17),(17,18),(4,5),(5,6),(6,9),(0,8),(0,12),(0,19)]:
    d=compare_roi(frames[a],frames[b],(8,8,tw-8,th-8))
    check(d['pixels_outside_1']==0,f'{a}/{b}: Active/scope/Esc lifecycle leaves correct viewport presentation',**d)

single=(folder/'same-frame-1-compiled-composite.frag').read_bytes()
multi=(folder/'same-frame-2-compiled-composite.frag').read_bytes()
# The legacy shader reference is an explicit, separately hashed input.
check(single==prior.read_bytes(),'legacy single Gold compiled GLSL byte-identical to Revision04')
merged=multi.replace(b'vec4 selected=texelFetch(selectedMask,p,0).rgba; return selected.rg+selected.ba;',b'return texelFetch(selectedMask,p,0).rg;')
check(merged==single,'multi composite differs from legacy Gold only by union coverage fetch')
report=dict(passed=all(c['passed'] for c in checks),scope='Ordinary RBS FrameSubmission through actual final presented framebuffer. Programmatic selection stimulus; NOT Owner physical-input or visual acceptance.',policy='Exact existing Phase24 Gold for all selected; previous alternate-secondary contrast policy superseded by Owner directive, historical reports untouched.',checks=checks,objects=objects,metadata=metadata,inputs={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.glob('same-frame-*')})
report['inputs'][str(prior)]=hashlib.sha256(prior.read_bytes()).hexdigest()
report['oracle_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
with output.open('x') as f:json.dump(report,f,indent=2)
print(json.dumps(dict(passed=report['passed'],checks=len(checks),failed=[c for c in checks if not c['passed']]),indent=2))
sys.exit(0 if report['passed'] else 1)
