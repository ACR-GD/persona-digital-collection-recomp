"""Build the English patch applied by the launcher: data/ + data_en/ -> persona_en.patch.

    make_patch.py data data_en out.patch

The patch holds, for every file of the overlay, the byte runs that differ from the original file on
the disc (translated movies: the appended bitmaps and the repointed memory map), or the whole file
when it has no original (subtitles). Original files are identified by size and CRC-32, so the patch
applies only to the disc it was made from. Format (little-endian):

    "PDCPAT1\\0" u32 nfiles
    per file: u16 len, path (relative, '/'), u32 src_size, u32 src_crc (0, 0 = new file),
              u32 dst_size, u32 dst_crc, u32 nruns, nruns x (u32 offset, u32 length, bytes)
"""
import os, struct, sys, zlib

GAP = 32   # identical runs shorter than this are folded into the surrounding difference


def runs(src, dst):
    """(offset, bytes) runs of dst that differ from src; blocks are compared first, then bytes."""
    n = len(dst); common = min(len(src), n); B = 4096
    diff = []          # [start, end) byte ranges that differ
    for o in range(0, common, B):
        e = min(o + B, common)
        if src[o:e] == dst[o:e]: continue
        k = o
        while k < e:
            if src[k] != dst[k]:
                s0 = k
                while k < e and src[k] != dst[k]: k += 1
                diff.append([s0, k])
            else: k += 1
    if n > common: diff.append([common, n])
    merged = []
    for d in diff:
        if merged and d[0] - merged[-1][1] < GAP: merged[-1][1] = d[1]
        else: merged.append(d)
    return [(s0, dst[s0:e0]) for s0, e0 in merged]


def main(data, overlay, out):
    files = []
    for root, _, names in os.walk(overlay):
        for nm in sorted(names):
            p = os.path.join(root, nm); rel = os.path.relpath(p, overlay).replace(os.sep, '/')
            if nm.startswith('.'): continue
            files.append(rel)
    files.sort()
    blob = bytearray(b'PDCPAT1\0' + struct.pack('<I', len(files)))
    for rel in files:
        dst = open(os.path.join(overlay, rel), 'rb').read()
        sp = os.path.join(data, rel)
        src = open(sp, 'rb').read() if os.path.exists(sp) else b''
        rr = runs(src, dst) if src else [(0, dst)]
        blob += struct.pack('<H', len(rel.encode())) + rel.encode()
        blob += struct.pack('<IIIII', len(src), zlib.crc32(src) if src else 0, len(dst), zlib.crc32(dst), len(rr))
        for off, b in rr: blob += struct.pack('<II', off, len(b)) + b
        print('%-24s %9d -> %9d  %4d runs %8d bytes' % (rel, len(src), len(dst), len(rr), sum(len(b) for _, b in rr)))
    open(out, 'wb').write(blob)
    print('%s: %d files, %d bytes' % (out, len(files), len(blob)))


if __name__ == '__main__':
    main(*sys.argv[1:4])
