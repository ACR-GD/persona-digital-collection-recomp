"""Disassemble the compiled Lingo (Lscr) of a Director 5 movie.
usage: lingo_dis.py movie.dxr [grep-word]
Layout and opcodes follow ProjectorRays (D5: operand multiplier 8)."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import DirFile, parse_lnam, parse_lscr_literals, sjis

OPS = {0x01: 'ret', 0x02: 'retfactory', 0x03: 'pushzero', 0x04: 'mul', 0x05: 'add', 0x06: 'sub', 0x07: 'div', 0x08: 'mod',
       0x09: 'inv', 0x0a: 'joinstr', 0x0b: 'joinpadstr', 0x0c: 'lt', 0x0d: 'lteq', 0x0e: 'nteq', 0x0f: 'eq', 0x10: 'gt',
       0x11: 'gteq', 0x12: 'and', 0x13: 'or', 0x14: 'not', 0x15: 'containsstr', 0x16: 'contains0str', 0x17: 'getchunk',
       0x18: 'hilitechunk', 0x19: 'ontospr', 0x1a: 'intospr', 0x1b: 'getfield', 0x1c: 'starttell', 0x1d: 'endtell',
       0x1e: 'pushlist', 0x1f: 'pushproplist', 0x21: 'swap',
       0x41: 'pushint8', 0x42: 'pusharglistnoret', 0x43: 'pusharglist', 0x44: 'pushcons', 0x45: 'pushsymb', 0x46: 'pushvarref',
       0x48: 'getglobal2', 0x49: 'getglobal', 0x4a: 'getprop', 0x4b: 'getparam', 0x4c: 'getlocal', 0x4e: 'setglobal2',
       0x4f: 'setglobal', 0x50: 'setprop', 0x51: 'setparam', 0x52: 'setlocal', 0x53: 'jmp', 0x54: 'endrepeat', 0x55: 'jmpifz',
       0x56: 'localcall', 0x57: 'extcall', 0x58: 'objcallv4', 0x59: 'put', 0x5a: 'putchunk', 0x5b: 'deletechunk', 0x5c: 'get',
       0x5d: 'set', 0x5f: 'getmovieprop', 0x60: 'setmovieprop', 0x61: 'getobjprop', 0x62: 'setobjprop', 0x63: 'tellcall',
       0x64: 'peek', 0x65: 'pop', 0x66: 'thebuiltin', 0x67: 'objcall', 0x6d: 'pushchunkvarref', 0x6e: 'pushint16',
       0x6f: 'pushint32', 0x70: 'getchainedprop', 0x71: 'pushfloat32', 0x72: 'gettoplevelprop', 0x73: 'newobj'}
NAMED = {'pushsymb', 'getglobal', 'setglobal', 'getglobal2', 'setglobal2', 'extcall', 'getprop', 'setprop', 'getmovieprop',
         'setmovieprop', 'getobjprop', 'setobjprop', 'thebuiltin', 'objcall', 'tellcall', 'pushvarref', 'getchainedprop',
         'gettoplevelprop', 'newobj'}


def dis_script(d, names, out):
    hcount, hoff = struct.unpack_from('>HI', d, 72)
    lits = parse_lscr_literals(d)
    for h in range(hcount):
        r = hoff + 42 * h
        name_id, vpos, clen, coff, argc, argoff, locc, locoff = struct.unpack_from('>hHIIHIHI', d, r)
        args = [names[struct.unpack_from('>H', d, argoff + 2 * i)[0]] for i in range(argc)] if argc else []
        locs = [names[struct.unpack_from('>H', d, locoff + 2 * i)[0]] for i in range(locc)] if locc else []
        out.append('on %s %s   -- locals %s' % (names[name_id] if 0 <= name_id < len(names) else name_id, ', '.join(args), ', '.join(locs)))
        p = coff; end = coff + clen
        while p < end:
            op = d[p]; at = p - coff; p += 1
            arg = None
            if op >= 0xc0: arg = struct.unpack_from('>i', d, p)[0]; p += 4; base = op - 0x80
            elif op >= 0x80: arg = struct.unpack_from('>H', d, p)[0]; p += 2; base = op - 0x40
            elif op >= 0x40: arg = d[p]; p += 1; base = op
            else: base = op
            mn = OPS.get(base, 'op_%02x' % base)
            s = mn
            if arg is not None:
                if mn in NAMED and 0 <= arg < len(names): s += ' ' + names[arg]
                elif mn == 'pushcons':
                    k = arg // 8
                    if k < len(lits):
                        t, v, _ = lits[k]
                        s += ' ' + (repr(sjis(v)) if t == 1 else (str(v) if t == 4 else repr(v)))
                elif mn in ('getlocal', 'setlocal'): k = arg // 8; s += ' ' + (locs[k] if k < len(locs) else str(arg))
                elif mn in ('getparam', 'setparam'): k = arg // 8; s += ' ' + (args[k] if k < len(args) else str(arg))
                elif mn in ('jmp', 'jmpifz'): s += ' -> %d' % (at + arg)
                elif mn == 'endrepeat': s += ' -> %d' % (at - arg)
                else: s += ' %d' % arg
            out.append('  %4d  %s' % (at, s))
        out.append('end')


def main():
    f = DirFile(sys.argv[1])
    names = parse_lnam(f.first('Lnam').data)
    out = []
    for ch in f.by_tag.get('Lscr', []):
        if not ch.data or len(ch.data) < 92: continue
        out.append('---- Lscr #%d' % ch.index)
        dis_script(ch.data, names, out)
    txt = '\n'.join(out)
    if len(sys.argv) > 2:
        blocks = txt.split('\non ')
        print('\n\non '.join(b for b in blocks if sys.argv[2] in b))
    else:
        print(txt)


if __name__ == '__main__':
    main()
