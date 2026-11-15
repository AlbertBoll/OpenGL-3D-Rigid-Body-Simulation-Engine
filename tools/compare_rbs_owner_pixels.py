"""Compare exact rendered RGBA captures and encode their original pixels as PNG."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

def png(file,width,height,data):
    def chunk(kind,body):
        return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
    rows=b''.join(b'\0'+data[y*width*4:(y+1)*width*4] for y in reversed(range(height)))
    file.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+
                     chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('reference',type=Path);parser.add_argument('candidate',type=Path)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args();rows=[]
    for original in sorted(args.reference.glob('*.rgba')):
        candidate=args.candidate/original.name
        before,after=original.read_bytes(),candidate.read_bytes()
        case,size=original.stem.rsplit('-',1);width,height=map(int,size.split('x'))
        assert len(before)==len(after)==width*height*4
        for file,data in ((original,before),(candidate,after)):png(file.with_suffix('.png'),width,height,data)
        rows.append(dict(case=case,channels_changed=sum(x!=y for x,y in zip(before,after)),
                         reference=hashlib.sha256(before).hexdigest(),candidate=hashlib.sha256(after).hexdigest()))
    assert len(rows)==14
    args.output.write_text(json.dumps(rows,indent=2)+'\n')
    print(f'{sum(r["channels_changed"]==0 for r in rows)}/{len(rows)} bit-exact viewport captures')
    return int(any(r['channels_changed'] for r in rows))

if __name__=='__main__':raise SystemExit(main())
