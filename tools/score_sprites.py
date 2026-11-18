"""List the sprites of a D5 movie score with their member, attached script (Lscr chunk index, as printed by
lingo_dis.py) and position, frame by frame (only frames where something changes are printed).
usage: score_sprites.py movie.dxr [LSCR ...]   -> with LSCR numbers, only the sprites using those scripts
Sprite record (24 bytes): type, ink, castLib(2), member(2), scriptLib(2), script(2), ?, ?, locV(2), locH(2),
height(2), width(2), ... ; loc is the registration point (the bitmap's centre unless set otherwise)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import DirFile, Member

f = DirFile(sys.argv[1])
want = set(int(x) for x in sys.argv[2:])
d = f.first('VWSC').data
ctx = (f.first('LctX') or f.first('Lctx')).data
nent, = struct.unpack_from('>I', ctx, 8)
eoff, = struct.unpack_from('>H', ctx, 16)
sid2l = {i + 1: struct.unpack_from('>i', ctx, eoff + 12 * i + 4)[0] for i in range(nent)}
first = f.min_member()
mem2l = {}   # score member number (1-based slot in this cast) -> Lscr chunk index
for n, c in f.cast_list():
    m = Member(n, c)
    if m.tname == 'script' and len(m.info_raw) >= 20:
        mem2l[n - first + 1] = sid2l.get(struct.unpack_from('>I', m.info_raw, 16)[0])
labels = {}
lb = f.first('VWLB')
if lb:
    b = lb.data; cnt, = struct.unpack_from('>H', b, 0)
    ents = [struct.unpack_from('>HH', b, 2 + 4 * i) for i in range(cnt + 1)]
    base = 2 + 4 * (cnt + 1)
    for i in range(cnt):
        labels[ents[i][0]] = b[base + ents[i][1]:base + ents[i + 1][1]].decode('latin1')
total, f1off, nframes, fver, rec, nch, ndisp = struct.unpack_from('>IIIHHHH', d, 0)
p = 20; buf = bytearray(rec * (nch + 6)); frame = 0; prev = None
while p < min(total, len(d)):
    fs, = struct.unpack_from('>H', d, p); q = p + 2; end = p + fs
    while q < end:
        cs, co = struct.unpack_from('>HH', d, q); q += 4; buf[co:co + cs] = d[q:q + cs]; q += cs
    frame += 1; p = end
    sp = []
    for ch in range(1, nch):
        r = buf[rec * (ch + 1):rec * (ch + 2)]
        mem = int.from_bytes(r[4:6], 'big'); sc = int.from_bytes(r[8:10], 'big')
        if not mem:
            continue
        lscr = mem2l.get(sc) if sc else None
        if want and lscr not in want:
            continue
        y, x, h, w = (int.from_bytes(r[i:i + 2], 'big', signed=True) for i in (12, 14, 16, 18))
        sp.append('ch%d m%d%s @%d,%d %dx%d' % (ch, mem, ' L%s' % lscr if sc else '', x, y, w, h))
    fsn = int.from_bytes(buf[2:4], 'big')   # main channel: script castLib(2), member(2)
    if fsn and (not want or mem2l.get(fsn) in want):
        sp.insert(0, 'frame script L%s' % mem2l.get(fsn))
    line = ' | '.join(sp)
    if line != prev or frame in labels:
        if line or frame in labels:
            print(frame, labels.get(frame, ''), '|', line)
        prev = line
