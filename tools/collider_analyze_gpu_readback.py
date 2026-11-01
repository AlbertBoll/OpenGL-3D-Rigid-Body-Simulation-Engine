import array,hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]); result={'cases':{},'checks':[]}
def check(ok,label):
 result['checks'].append({'passed':bool(ok),'label':label})
 print('PASS' if ok else 'FAIL',label)
for line in (p/'widget-smoke.tsv').read_text().splitlines():
 r=line.split('\t')
 if r[0]!='SAME_FRAME':continue
 k,ox,oy,w,h,mw,mh,fmt,stencil=map(int,r[1:10]); dx,dy=map(float,r[10:12])
 f=p/f'same-frame-{k}'; mask=array.array('f');mask.frombytes(Path(str(f)+'-mask.f32').read_bytes())
 before=Path(str(f)+'-before.rgba').read_bytes();after=Path(str(f)+'-after.rgba').read_bytes()
 changed=[];solid=0;solid_changed=0;active=secondary=0;gold=secondary_visible=0
 for y in range(h):
  for x in range(w):
   q=(y*mw+x)*4;i=(y*w+x)*4;a=mask[q];s=mask[q+2] if fmt==34836 else 0
   active+=a>0;secondary+=s>0
   diff=before[i:i+3]!=after[i:i+3]
   if a+s>=.9999:solid+=1;solid_changed+=diff
   if diff:changed.append((x,y));gold+=after[i]>=250 and 178<=after[i+1]<=190 and 16<=after[i+2]<=28
 radius=int(6*max(dx,dy)+1)
 for x,y in changed:
  near_a=near_s=False
  for yy in range(max(0,y-radius),min(h,y+radius+1)):
   for xx in range(max(0,x-radius),min(w,x+radius+1)):
    q=(yy*mw+xx)*4
    near_a|=mask[q]>0
    if fmt==34836 and ((x-xx)/dx)**2+((y-yy)/dy)**2<=4.01:near_s|=mask[q+2]>0
  secondary_visible+=near_s and not near_a
 result['cases'][str(k)]=dict(region=[ox,oy,w,h],mask_extent=[mw,mh],format=fmt,stencil_bits=stencil,dpi=[dx,dy],active_pixels=active,secondary_pixels=secondary,changed_pixels=len(changed),solid_pixels=solid,solid_changed=solid_changed,gold_pixels=gold,secondary_visible_pixels=secondary_visible,mask_sha256=hashlib.sha256(mask.tobytes()).hexdigest())
 check(stencil>=2,f'{k}: two stencil bits available')
 check(solid_changed==0,f'{k}: fully covered mesh interior unchanged')
 if k==15:check(active==secondary==len(changed)==0,'15: occluded Active/Secondary cause no visible outline')
 else:
  check(active>0 and gold>0,f'{k}: actual Active coverage and gold pixels')
  if k in (3,4,5,6,7,8,9,10,16,17,18,20,21):check(secondary>0 and secondary_visible>0,f'{k}: actual distinct Secondary silhouette')
c=result['cases']
check(c['7']['active_pixels']<c['2']['active_pixels']*.8,'masked alpha holes exclude actual coverage')
check(c['8']['active_pixels']<c['2']['active_pixels']*.8,'transparent alpha-zero holes exclude actual fallback coverage')
check(c['16']['mask_sha256']==c['17']['mask_sha256']==c['18']['mask_sha256'],'Scope Selected/All zero layers and re-enabled Collider layer preserve mesh coverage')
check(c['9']['dpi']==[2.,2.],'DPI2 composite uses correct pixel ratio')
check('IMAGE\t10\t720\t480' in (p/'widget-smoke.tsv').read_text(),'resize reaches actual720x480 physical target')
check(c['20']['mask_sha256']==c['21']['mask_sha256'],'instanced and serial references produce identical Active/Secondary coverage')
check(c['25']['mask_sha256']==c['26']['mask_sha256'],'legacy single identity and one-member selected set produce identical gold coverage')
check('INSTANCED\t20\t2\t' in (p/'widget-smoke.tsv').read_text(),'Secondary instanced batch observed in actual GL submission')
result['passed']=all(r['passed'] for r in result['checks'])
with (p/'SAME_FRAME_ANALYSIS.json').open('x') as out:json.dump(result,out,indent=2)
sys.exit(0 if result['passed'] else 1)
