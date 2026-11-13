"""Replace bitmap pixels in a Director 5 (Windows "XFIR") movie.

New BITD chunks are appended at the end of the file and the memory map entry of the old chunk
is pointed at them; everything else (including the old data) stays byte-identical. Bitmaps keep
their size, depth and palette, so the score and Lingo see the same cast member.
"""
import struct
from director import DirFile, Member, bitmap_info, wtag


def rle_encode(data):
    """Director's PackBits flavour: n<0x80 -> n+1 literal bytes; n>=0x80 -> byte repeated 257-n times."""
    out = bytearray(); i = 0; n = len(data)
    while i < n:
        run = 1
        while i + run < n and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes([257 - run, data[i]]); i += run
            continue
        j = i; lit = bytearray()
        while j < n and len(lit) < 128:
            if j + 2 < n and data[j] == data[j + 1] == data[j + 2]:
                break
            lit.append(data[j]); j += 1
        out += bytes([len(lit) - 1]) + lit; i = j
    return bytes(out)


def bitd_from_image(im, bi):
    """im: PIL 'P' image of the member's size -> BITD payload (pitch-padded rows, RLE per row)."""
    w, h, pitch = bi['w'], bi['h'], bi['pitch']
    assert im.size == (w, h), (im.size, w, h)
    raw = im.tobytes()
    out = bytearray(); plain = bytearray()
    for y in range(h):
        row = raw[y * w:(y + 1) * w] + bytes(pitch - w)
        plain += row
        out += rle_encode(row)
    # a payload of exactly pitch*h bytes is read as uncompressed, so never emit RLE that long
    return bytes(out) if len(out) < len(plain) else bytes(plain)


class Patcher:
    def __init__(self, path):
        self.f = DirFile(path)
        self.buf = bytearray(self.f.raw)
        self.members = {num: Member(num, ch) for num, ch in self.f.cast_list()}

    def replace_bitmap(self, num, im):
        m = self.members[num]
        bi = bitmap_info(m.spec)
        if bi['depth'] != 8:
            raise ValueError('member %d: only 8-bit bitmaps are supported (%d)' % (num, bi['depth']))
        old = self.f.child(m.chunk.index, 'BITD')
        payload = bitd_from_image(im, bi)
        self._append_chunk(old.index, 'BITD', payload)

    def disable_handler(self, lscr_index, handler, must_contain=None):
        """Make handler number `handler` of script chunk Lscr #lscr_index return at once (its first
        opcode becomes ret). must_contain: a literal the script must hold (guards against a wrong index)."""
        ch = next(c for c in self.f.by_tag['Lscr'] if c.index == lscr_index)
        d = bytearray(ch.data)
        if must_contain and must_contain.encode('latin-1') not in d:
            raise ValueError('Lscr #%d does not contain %r' % (lscr_index, must_contain))
        hcount, hoff = struct.unpack_from('>HI', d, 72)
        if handler >= hcount: raise ValueError('Lscr #%d has %d handlers' % (lscr_index, hcount))
        coff = struct.unpack_from('>I', d, hoff + 42 * handler + 8)[0]
        d[coff] = 0x01   # ret
        self._append_chunk(lscr_index, 'Lscr', bytes(d))

    def _append_chunk(self, index, tag, payload):
        if len(self.buf) & 1:
            self.buf += b'\0'
        off = len(self.buf)
        self.buf += wtag(tag) + struct.pack('<I', len(payload)) + payload
        if len(payload) & 1:
            self.buf += b'\0'
        hs, es = self.f.mmap_hdr[0], self.f.mmap_hdr[1]
        p = self.f.mmap_off + 8 + hs + index * es
        struct.pack_into('<II', self.buf, p + 4, len(payload), off)

    def save(self, path):
        struct.pack_into('<I', self.buf, 4, len(self.buf) - 8)
        # mmap entry 0 describes the RIFX container itself
        hs = self.f.mmap_hdr[0]
        p = self.f.mmap_off + 8 + hs
        if self.buf[p:p + 4] == wtag('RIFX'):
            struct.pack_into('<I', self.buf, p + 4, len(self.buf) - 8)
        open(path, 'wb').write(self.buf)
