"""Losslessly encode separate review ReadColor captures and compare fixed sphere interiors.

No synthesis or screenshots of generated UI: these pixels come from the actual GL
render target. Desktop evidence is captured separately by rbs_editor_capture.py.
"""
import json
from pathlib import Path
import re
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1] / 'logs/ui/selection-highlight-02'


def read(path):
    w, h = map(int, re.search(r'-(\d+)x(\d+)\.rgba$', path.name).groups())
    raw = path.read_bytes()
    assert len(raw) == w * h * 4
    return w, h, b''.join(raw[y*w*4:(y+1)*w*4] for y in reversed(range(h)))


def encode(path):
    w, h, raw = read(path)
    rgb = bytearray(w*h*3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[0::4], raw[1::4], raw[2::4]
    rows = b''.join(b'\0' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind+data) & 0xffffffff)
    path.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    result = {'note': 'Actual target RGB, not timing runs. Interior disks are fixed, inspected foreground-sphere regions in the original orthographic Camera B view, away from silhouettes. No global artifact-free claim for arbitrary scenes.'}
    for stage in ('before', 'after'):
        folder = ROOT / f'images-{stage}-jolt'
        for path in folder.glob('*.rgba'):
            encode(path)
        (folder/'CAPTURE_ONLY.md').write_text(
            'Explicit synchronous ReadColor capture run; exclude from performance analysis. '
            'The inherited result.json timing protocol does not qualify capture timings. '
            'PNGs losslessly encode RGB with OpenGL rows presented top-down. '
            'Actual desktop evidence is recorded separately.\n')
        w, h, on = read(next(folder.glob('ortho-181-helper-*.rgba')))
        rw, rh, off = read(next(folder.glob('ortho-181-off-*.rgba')))
        assert (w, h) == (rw, rh) == (1072, 763)
        tested = changed = max_delta = 0
        for cy in (52, 109, 165, 220, 276, 332):
            for cx in (459, 540, 619, 700, 780, 859):
                for dy in range(-18, 19):
                    for dx in range(-18, 19):
                        if dx*dx+dy*dy > 18*18:
                            continue
                        p = ((cy+dy)*w+cx+dx)*4
                        delta = max(abs(on[p+i]-off[p+i]) for i in range(3))
                        tested += 1
                        changed += delta > 0
                        max_delta = max(max_delta, delta)
        result[stage] = dict(interior_pixels=tested, changed_rgb_pixels=changed, max_channel_delta=max_delta)
    result['unselected_render_byte_identical'] = all(
        (ROOT/'images-before-jolt'/p.name).read_bytes() == p.read_bytes()
        for p in (ROOT/'images-after-jolt').glob('none-*.rgba'))
    (ROOT/'image-comparison.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
