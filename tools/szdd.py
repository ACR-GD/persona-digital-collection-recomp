"""szdd.py FILE OUTDIR: extract every SZDD (MS COMPRESS.EXE LZSS) blob embedded in FILE as OUTDIR/blob_<offset>.bin."""
import sys, os, struct
def unszdd(d, p):
    assert d[p:p + 8] == b'SZDD\x88\xf0\x27\x33'
    size = struct.unpack_from('<I', d, p + 10)[0]
    win = bytearray(b' ' * 4096); wp = 4096 - 16
    out = bytearray(); i = p + 14
    while len(out) < size and i < len(d):
        flags = d[i]; i += 1
        for bit in range(8):
            if len(out) >= size or i >= len(d): break
            if flags & (1 << bit):
                c = d[i]; i += 1; out.append(c); win[wp] = c; wp = (wp + 1) & 4095
            else:
                a, b = d[i], d[i + 1]; i += 2
                off = a | ((b & 0xf0) << 4); ln = (b & 0x0f) + 3
                for k in range(ln):
                    c = win[(off + k) & 4095]; out.append(c); win[wp] = c; wp = (wp + 1) & 4095
    return bytes(out[:size])
d = open(sys.argv[1], 'rb').read(); os.makedirs(sys.argv[2], exist_ok=True)
p = d.find(b'SZDD\x88\xf0\x27\x33')
while p >= 0:
    try:
        b = unszdd(d, p); open(os.path.join(sys.argv[2], 'blob_%06x.bin' % p), 'wb').write(b)
        print(hex(p), len(b), b[:2], b'QTIM32' in b, b'_EntryPoint' in b)
    except Exception as e: print(hex(p), 'error', e)
    p = d.find(b'SZDD\x88\xf0\x27\x33', p + 8)
