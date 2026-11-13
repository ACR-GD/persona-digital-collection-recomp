"""Reader/writer for Director 5 (Windows, little-endian "XFIR"/MV93 and MC95) movie and cast files.

Layout (Director 5):
  RIFX header -> imap -> mmap (table of every chunk: tag, length, file offset)
  KEY*  maps (resource id -> owner cast member id, tag)  e.g. a CASt owns its BITD / STXT / snd
  CAS*  list of mmap indices of the CASt chunks of the internal cast, in member order
  CASt  member header: type, info list (name, script text...), type-specific data (big-endian)
  STXT  field / text member contents (big-endian header, text, style runs)
  Lctx/Lnam/Lscr  compiled Lingo: context, symbol names, per-script bytecode + literals
  FXmp  Mac->Windows font / charset mapping table (plain text)
  VWFM  font map of the movie (font numbers -> names)

Tags are stored as little-endian u32 in Windows files, so bytes on disk are reversed.
Chunk payloads keep Mac (big-endian) byte order for most Director structures (CASt, STXT, Lscr...).
"""
import struct, sys

MEMBER_TYPES = {1: 'bitmap', 2: 'filmloop', 3: 'text', 4: 'palette', 5: 'picture', 6: 'sound',
                7: 'button', 8: 'shape', 9: 'movie', 10: 'digitalVideo', 11: 'script', 12: 'RTE',
                15: 'xtra'}


def rtag(b):
    return b[::-1].decode('latin1')


def wtag(s):
    return s.encode('latin1')[::-1]


class Chunk:
    __slots__ = ('index', 'tag', 'length', 'offset', 'flags', 'data')

    def __init__(self, index, tag, length, offset, flags, data):
        self.index, self.tag, self.length, self.offset, self.flags, self.data = index, tag, length, offset, flags, data

    def __repr__(self):
        return '<%s #%d len=%d @%#x>' % (self.tag, self.index, self.length, self.offset)


class DirFile:
    def __init__(self, path=None, data=None, base=0):
        """base: file offset of the RIFX inside its container (offsets in an embedded RIFX are absolute)."""
        self.base = base
        self.path = path
        self.raw = data if data is not None else open(path, 'rb').read()
        d = self.raw
        if d[:4] != b'XFIR':
            raise ValueError('%s: not a little-endian RIFX file (%r)' % (path, d[:4]))
        self.kind = rtag(d[8:12])                   # MV93 (movie) / MC95 (cast)
        imap = 12
        assert rtag(d[imap:imap + 4]) == 'imap'
        self.mmap_off = struct.unpack_from('<I', d, imap + 12)[0] - base
        mo = self.mmap_off
        assert rtag(d[mo:mo + 4]) == 'mmap'
        hs, es, cmax, cused, jh, jh2, fh = struct.unpack_from('<HHiiiii', d, mo + 8)
        self.mmap_hdr = (hs, es, cmax, cused, jh, jh2, fh)
        self.chunks = []
        for i in range(cused):
            p = mo + 8 + hs + i * es
            t = rtag(d[p:p + 4])
            ln, off, fl = struct.unpack_from('<IIH', d, p + 4)
            data = None
            off -= base
            if t not in ('free', 'junk') and off + 8 + ln <= len(d) and off:
                data = d[off + 8: off + 8 + ln]
            self.chunks.append(Chunk(i, t, ln, off, fl, data))
        self.by_tag = {}
        for c in self.chunks:
            self.by_tag.setdefault(c.tag, []).append(c)
        self._parse_keys()

    def first(self, tag):
        l = self.by_tag.get(tag)
        return l[0] if l else None

    # ------------------------------------------------------------ KEY* / CAS*
    def _parse_keys(self):
        self.owned = {}          # owner chunk index -> {tag: chunk index}
        k = self.first('KEY*')
        if not k:
            return
        d = k.data
        ps, ks, mx, used = struct.unpack_from('<HHII', d, 0)
        for i in range(used):
            sec, cast, t = struct.unpack_from('<II4s', d, ps + i * ks)
            self.owned.setdefault(cast, {})[rtag(t)] = sec

    def child(self, owner_index, tag):
        i = self.owned.get(owner_index, {}).get(tag)
        return self.chunks[i] if i is not None else None

    def cast_list(self):
        """[(member_number, CASt chunk)] for the cast in this file."""
        c = self.first('CAS*')
        if not c:
            return []
        n = len(c.data) // 4
        idx = struct.unpack('>%dI' % n, c.data)
        first = self.min_member()
        return [(first + i, self.chunks[x]) for i, x in enumerate(idx) if x]

    def min_member(self):
        cf = self.first('VWCF') or self.first('DRCF')
        if cf and self.kind == 'MV93':
            return struct.unpack_from('>H', cf.data, 4)[0]   # castArrayStart
        return 1


# ---------------------------------------------------------------- CASt
class Member:
    def __init__(self, num, chunk):
        self.num, self.chunk = num, chunk
        d = chunk.data
        self.type, info_len, spec_len = struct.unpack_from('>III', d, 0)
        self.info_raw = d[12:12 + info_len]
        self.spec = d[12 + info_len:12 + info_len + spec_len]
        self.info = parse_info(self.info_raw)
        self.name = self.info[1] if len(self.info) > 1 else b''
        self.script_text = self.info[0] if self.info else b''

    @property
    def tname(self):
        return MEMBER_TYPES.get(self.type, str(self.type))


def parse_info(b):
    """CASt info list: header (dataOffset, unk1, unk2, flags, scriptId), offset table, items."""
    if len(b) < 20:
        return []
    data_off = struct.unpack_from('>I', b, 0)[0]
    p = data_off
    if p + 2 > len(b):
        return []
    n = struct.unpack_from('>H', b, p)[0]
    offs = struct.unpack_from('>%dI' % (n + 1), b, p + 2)
    base = p + 2 + 4 * (n + 1)
    items = []
    for i in range(n):
        it = b[base + offs[i]: base + offs[i + 1]]
        items.append(it)
    # item 1 is a Pascal string name
    if len(items) > 1 and items[1]:
        items[1] = items[1][1:1 + items[1][0]]
    return items


def bitmap_info(spec):
    """Director 5 bitmap specific data: pitch, rect, regs, bitdepth, palette."""
    pitch = struct.unpack_from('>H', spec, 0)[0]
    flags = pitch & 0x8000
    pitch &= 0x3fff
    top, left, bottom, right = struct.unpack_from('>hhhh', spec, 2)
    regy, regx = struct.unpack_from('>hh', spec, 18) if len(spec) >= 22 else (0, 0)
    depth = 1
    pal = 0
    if flags and len(spec) >= 26:
        depth = spec[23]
        pal = struct.unpack_from('>h', spec, 26)[0] if len(spec) >= 28 else 0
    return dict(pitch=pitch, w=right - left, h=bottom - top, top=top, left=left, regx=regx, regy=regy,
                depth=depth, palette=pal)


def decode_bitd(data, w, h, pitch, depth):
    """BITD: PackBits-style RLE unless the payload already has the full size."""
    need = pitch * h
    if len(data) >= need:
        return data[:need]
    out = bytearray()
    i = 0
    while i < len(data) and len(out) < need:
        c = data[i]; i += 1
        if c >= 0x80:
            n = 257 - c
            out += bytes([data[i]]) * n; i += 1
        else:
            n = c + 1
            out += data[i:i + n]; i += n
    out += bytes(need - len(out))
    return bytes(out[:need])


# ---------------------------------------------------------------- STXT
def parse_stxt(d):
    hl, tl, fl = struct.unpack_from('>III', d, 0)
    text = d[hl:hl + tl]
    fmt = d[hl + tl:hl + tl + fl]
    runs = []
    if len(fmt) >= 2:
        n = struct.unpack_from('>H', fmt, 0)[0]
        for i in range(n):
            o = 2 + 20 * i
            if o + 20 > len(fmt):
                break
            start, height, ascent, font, style, _pad, size, r, g, b = struct.unpack_from('>IHHHBBHHHH', fmt, o)
            runs.append(dict(start=start, height=height, ascent=ascent, font=font, style=style, size=size,
                             color=(r >> 8, g >> 8, b >> 8)))
    return text, runs, fmt


def build_stxt(text, fmt):
    return struct.pack('>III', 12, len(text), len(fmt)) + text + fmt


# ---------------------------------------------------------------- Lingo
def parse_lnam(d):
    u1, u2, ln, ln2, off, cnt = struct.unpack_from('>IIIIHH', d, 0)
    names = []
    p = off
    for _ in range(cnt):
        n = d[p]; names.append(d[p + 1:p + 1 + n].decode('latin1')); p += 1 + n
    return names


def parse_lscr_literals(d):
    """[(type, value, abs_offset_of_value)] from a D5 Lscr literal table (ProjectorRays layout).
    type 1 = string, 4 = int, 9 = float."""
    if len(d) < 0x5c:
        return []
    lit_count = struct.unpack_from('>H', d, 0x4e)[0]
    lit_off = struct.unpack_from('>I', d, 0x50)[0]
    data_off = struct.unpack_from('>I', d, 0x58)[0]
    out = []
    for i in range(lit_count):
        t, o = struct.unpack_from('>II', d, lit_off + 8 * i)
        p = data_off + o
        if t in (1, 9):
            n = struct.unpack_from('>I', d, p)[0]
            out.append((t, d[p + 4:p + 4 + n], p))
        else:
            out.append((t, o, None))
    return out


def sjis(b):
    b = b.rstrip(b'\0')
    try:
        return b.decode('cp932')
    except UnicodeDecodeError:
        return b.decode('cp932', 'replace')


if __name__ == '__main__':
    f = DirFile(sys.argv[1])
    from collections import Counter
    print(f.kind, Counter(c.tag for c in f.chunks))
    for num, ch in f.cast_list():
        m = Member(num, ch)
        print(num, m.tname, sjis(m.name))


def clut_rgb(d):
    """CLUT chunk -> flat [r,g,b]*n list (entries are 3 x u16 big-endian)."""
    out = []
    for i in range(len(d) // 6):
        r, g, b = struct.unpack_from('>HHH', d, 6 * i)
        out += [r >> 8, g >> 8, b >> 8]
    return out


def bitmap_image(f, member, palette):
    """Decode a bitmap member to a PIL image (mode P for 8-bit, 1 for 1-bit)."""
    from PIL import Image
    bi = bitmap_info(member.spec)
    b = f.child(member.chunk.index, 'BITD')
    if not b or bi['w'] <= 0 or bi['h'] <= 0:
        return None, bi
    w, h, depth = bi['w'], bi['h'], bi['depth']
    pitch = bi['pitch']
    if depth == 1 and pitch == 0:
        pitch = ((w + 15) // 16) * 2
    px = decode_bitd(b.data, w, h, pitch, depth)
    if depth == 8:
        rows = b''.join(px[y * pitch:y * pitch + w] for y in range(h))
        im = Image.frombytes('P', (w, h), rows)
        im.putpalette(palette[:768] + [0] * (768 - len(palette[:768])))
    elif depth == 1:
        im = Image.frombytes('1', (pitch * 8, h), px).crop((0, 0, w, h))
        im = Image.eval(im.convert('L'), lambda v: 255 - v)
    else:
        return None, bi
    return im, bi
