"""make_subs.py DATA SCRIPT OUTDIR: pre-render English subtitles for burned-in Japanese text in QuickTime movies.

For each card: frames in the search window are decoded with ffmpeg (8 fps); text pixels are white
pixels next to dark ones. The card's area is the union of its text pixels; its on-screen interval
is where the text pixel count exceeds a quarter of its peak (padded for the fades). The subtitle is
an RGBA image (dark feathered band over the area + outlined white English text).

Output OUTDIR/dc/mov/<movie>.sub:  'PSUB' u32 n, then per card: f32 t0, f32 t1, u16 x, y, w, h, w*h*4 RGBA.
"""
import sys, os, json, struct, subprocess, tempfile, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFilter
from textpanel import find_font

FPS = 8
def frames(path, t0, t1):
    d = tempfile.mkdtemp()
    subprocess.run(['ffmpeg', '-v', 'error', '-ss', str(t0), '-to', str(t1), '-i', path, '-vf', 'fps=%d' % FPS, d + '/f_%04d.png'], check=True)
    fs = sorted(glob.glob(d + '/f_*.png'))
    return [(t0 + (i + 0.5) / FPS, Image.open(f).convert('RGB')) for i, f in enumerate(fs)]

def text_pixels(im):
    px = im.load(); w, h = im.size; out = []
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            c = px[x, y]
            if min(c) > 215 and max(c) - min(c) < 40:
                if any(sum(px[x + dx, y + dy]) < 260 for dx, dy in ((2, 0), (-2, 0), (0, 2), (0, -2)) if 0 <= x + dx < w and 0 <= y + dy < h):
                    out.append((x, y))
    return out

def render(w, h, box, text):
    l, t, r, b = box
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    band = Image.new('L', (w, h), 0)
    ImageDraw.Draw(band).rounded_rectangle((l, t, r, b), radius=5, fill=248)
    band = band.filter(ImageFilter.GaussianBlur(1.5))
    img.paste(Image.new('RGBA', (w, h), (8, 8, 16, 255)), (0, 0), band)
    lines = text.split('\n')
    tmp = ImageDraw.Draw(Image.new('L', (1, 1)))
    for size in range(14, 6, -1):
        f = __import__('PIL.ImageFont', fromlist=['x']).truetype(find_font('Arial Bold.ttf'), size)
        lh = int(size * 1.18)
        if max(tmp.textlength(x, font=f) for x in lines) <= (r - l) - 4 and lh * len(lines) <= (b - t) + 2: break
    d = ImageDraw.Draw(img)
    y = t + ((b - t) - lh * len(lines)) // 2
    for ln in lines:
        x = l + ((r - l) - d.textlength(ln, font=f)) / 2
        d.text((x, y), ln, font=f, fill=(255, 255, 255, 255), stroke_width=1, stroke_fill=(0, 0, 0, 255))
        y += lh
    return img

def main():
    data, script, out = sys.argv[1:4]
    S = json.load(open(script, encoding='utf-8'))
    os.makedirs(os.path.join(out, 'dc', 'mov'), exist_ok=True)
    prev = os.path.join('work', 'preview', 'subs'); os.makedirs(prev, exist_ok=True)
    for mv, cards in S.items():
        if mv.startswith('_'): continue
        path = os.path.join(data, 'dc', 'mov', mv + '.mov')
        blob = bytearray(b'PSUB' + struct.pack('<I', len(cards)))
        for ci, c in enumerate(cards):
            fr = frames(path, *c['win'])
            counts = [(t, text_pixels(im)) for t, im in fr]
            if 'box' in c:
                bl, bt, br, bb = c['box']
                counts = [(t, [q for q in p if bl <= q[0] < br and bt <= q[1] < bb]) for t, p in counts]
            peak = max(len(p) for _, p in counts)
            on = [(t, p) for t, p in counts if len(p) > peak / 4]
            t0, t1 = on[0][0] - 0.35, on[-1][0] + 0.35
            pts = [q for _, p in on for q in p]
            xs = sorted(q[0] for q in pts); ys = sorted(q[1] for q in pts)
            k = len(xs) // 400   # drop stray outliers
            l, r = xs[k], xs[-1 - k]; t, b = ys[k], ys[-1 - k]
            w, h = fr[0][1].size
            # room for the English: at least 92% of the width, centred on the Japanese
            cx = (l + r) // 2; half = max((r - l) // 2 + 6, int(w * 0.46))
            l, r = max(0, cx - half), min(w, cx + half)
            nl = c['en'].count('\n') + 1
            t, b = max(0, t - 6), min(h, b + 7)
            need = int(nl * 11.5) + 6
            if b - t < need: grow = need - (b - t); t = max(0, t - grow // 2); b = min(h, b + grow - grow // 2)
            img = render(w, h, (l, t, r, b), c['en']).crop((l, t, r, b))
            blob += struct.pack('<ffHHHH', t0, t1, l, t, r - l, b - t) + img.tobytes()
            # preview: the peak frame with the subtitle
            pk = max(counts, key=lambda q: len(q[1]))
            p = Image.alpha_composite(fr[counts.index(pk)][1].convert('RGBA'), Image.new('RGBA', (w, h)))
            p.alpha_composite(img, (l, t)); p.convert('RGB').resize((w * 2, h * 2), Image.LANCZOS).save(os.path.join(prev, '%s_%d.png' % (mv, ci)))
            print('%s card %d: %.2f-%.2f s, area (%d,%d)-(%d,%d)' % (mv, ci, t0, t1, l, t, r, b))
        open(os.path.join(out, 'dc', 'mov', mv + '.sub'), 'wb').write(blob)

if __name__ == '__main__':
    main()
