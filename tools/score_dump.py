"""Dump the main channel (tempo / sounds / script) of each frame of a D5 movie score (VWSC)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import DirFile
f = DirFile(sys.argv[1])
d = f.first('VWSC').data
total, f1off, nframes, fver, rec, nch, ndisp = struct.unpack_from('>IIIHHHH', d, 0)
nframes = 1 << 30
print('record', rec, 'channels', nch, 'header->', f1off)
p = 20 if f1off == 0x14 else f1off
size = rec * (nch + 0)
buf = bytearray(rec * (nch + 6))
frame = 0
want = set(int(x) for x in sys.argv[2:]) if len(sys.argv) > 2 else None
while p < min(total, len(d)) and frame < nframes:
    fs, = struct.unpack_from('>H', d, p); q = p + 2; end = p + fs
    while q < end:
        cs, co = struct.unpack_from('>HH', d, q); q += 4
        buf[co:co + cs] = d[q:q + cs]; q += cs
    frame += 1
    if want is None or frame in want:
        m = bytes(buf[:rec])
        print('%4d main %s | tempo %d snd1 %d snd2 %d script %d' % (frame, m.hex(' '), m[5], struct.unpack_from('>H', m, 7)[0] if False else int.from_bytes(m[6:8],'big'), int.from_bytes(m[8:10],'big'), int.from_bytes(m[0:2],'big')))
    p = end
