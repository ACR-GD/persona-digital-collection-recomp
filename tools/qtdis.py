"""qtdis.py ADDR [N]: disassemble a function of PS.exe, annotating QTIM32/CMGR32 thunk calls as QT#n / CM#n."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import PE
from capstone import *
p = PE('data/dc/PS.exe'); a = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 120
iat = {x: name for x, d, name in p.imports}
md = Cs(CS_ARCH_X86, CS_MODE_32)
for i in md.disasm(p.bytes_at(a, n * 8), a):
    s = i.op_str
    for x, nm in iat.items():
        if hex(x) in s: s = s.replace(hex(x), nm)
    if i.mnemonic == 'call' and s.startswith('0x'):
        t = int(s, 16)
        if 0x4f4092 <= t < 0x4f45ca: s += '   ; QT#%d' % ((t - 0x4f4092) // 13)
        elif 0x4f45ca <= t < 0x4f4780: s += '   ; CM#%d' % ((t - 0x4f45ca) // 18)
    print('%08x  %-6s %s' % (i.address, i.mnemonic, s)); n -= 1
    if n <= 0 or (i.mnemonic == 'ret' and n < 1000 and len(sys.argv) > 3): break
