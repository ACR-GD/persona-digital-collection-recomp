"""Test only: write a copy of contents.dxr whose startMovie also gives the player all 5 tickets, so the
principal's office ticket menu and the hidden attractions can be checked without winning the mini-games.
usage: qa_tickets.py data_en/dc/contents.dxr OUT.dxr
The handler's code is copied to the end of the script chunk with the 5 `set <x>ticket = 1` inserted before
its final ret, and the handler record is repointed (jumps are relative, so the copy runs unchanged)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import parse_lnam
from dxrpatch import Patcher

src, out = sys.argv[1:3]
p = Patcher(src)
names = parse_lnam(p.f.first('Lnam').data)
ch = next(c for c in p.f.by_tag['Lscr'] if c.index == 27)
d = bytearray(ch.data)
hcount, hoff = struct.unpack_from('>HI', d, 72)
coff = struct.unpack_from('>I', d, hoff + 8)[0]
assert names[struct.unpack_from('>h', d, hoff)[0]] == 'startMovie'
code = b''
for g in ('magicticket', 'raceticket', 'hockeyticket', 'shootticket', 'puzzleticket'):
    i = names.index(g)
    code += b'\x41\x01' + (bytes([0x4f, i]) if i < 256 else b'\x8f' + struct.pack('>H', i))
clen = struct.unpack_from('>I', d, hoff + 4)[0]
orig = bytes(d[coff:coff + clen])
assert orig[-1] == 0x01, 'unexpected startMovie layout'
new = orig[:-1] + code + b'\x01'
if len(d) & 1: d += b'\0'
struct.pack_into('>II', d, hoff + 4, len(new), len(d))
d += new
p._append_chunk(27, 'Lscr', bytes(d))
p.save(out)
print('wrote', out)
