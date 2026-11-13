"""Erase baked-in Japanese text from a bitmap and draw English text in its place.

Text pixels are found inside a box as bright, low-saturation pixels (white anti-aliased
Mincho) plus a 1-pixel halo of their anti-aliased edges. They are replaced from a background
model built per row and per dither phase (the panels use an ordered-dither gradient), so the
cleaned area keeps the original texture. English text is then rendered anti-aliased and the
result is mapped back to the movie palette (only pixels that changed are re-quantized).
"""
import os
from PIL import Image, ImageDraw, ImageFont

FONT_DIRS = [os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fonts'),
             '/System/Library/Fonts/Supplemental', '/System/Library/Fonts', '/usr/share/fonts/truetype/dejavu', 'C:/Windows/Fonts']


def find_font(name):
    for d in FONT_DIRS:
        p = os.path.join(d, name)
        if os.path.exists(p):
            return p
    raise FileNotFoundError(name)


def lum(c):
    return (c[0] * 299 + c[1] * 587 + c[2] * 114) // 1000


def is_text(c, thr=100, sat=80):
    return lum(c) >= thr and max(c) - min(c) <= sat


def text_mask(px, box, thr=100, sat=80, halo=2, max_run=90):
    """Bright low-saturation pixels in box (minus long straight runs: frame lines), dilated by `halo`."""
    l, t, r, b = box
    hit = {(x, y) for y in range(t, b) for x in range(l, r) if is_text(px[x, y], thr, sat)}
    drop = set()
    for y in range(t, b):                     # horizontal runs
        x = l
        while x < r:
            if (x, y) in hit:
                x0 = x
                while x < r and (x, y) in hit: x += 1
                if x - x0 > max_run: drop.update((k, y) for k in range(x0, x))
            else:
                x += 1
    for x in range(l, r):                     # vertical runs
        y = t
        while y < b:
            if (x, y) in hit:
                y0 = y
                while y < b and (x, y) in hit: y += 1
                if y - y0 > max_run: drop.update((x, k) for k in range(y0, y))
            else:
                y += 1
    core = [q for q in hit if q not in drop]
    mask = set()
    for (x, y) in core:
        for dy in range(-halo, halo + 1):
            for dx in range(-halo, halo + 1):
                if l <= x + dx < r and t <= y + dy < b:
                    mask.add((x + dx, y + dy))
    return mask, core


def text_bbox(img, box, **kw):
    mask, core = text_mask(img.load(), box, **kw)
    if not core:
        return None
    xs = [x for x, _ in core]; ys = [y for _, y in core]
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1)


def erase_vertical(img, box, **kw):
    """For photo backgrounds: masked pixels copy the clean strip just above the box (below if no room),
    which keeps the real texture instead of smearing a row."""
    px = img.load()
    mask, core = text_mask(px, box, **kw)
    l, t, r, b = box
    H = b - t
    w, h = img.size
    src = {}
    for (x, y) in mask:
        for yy in (y - H, y + H, y - 2 * H, y + 2 * H):
            if 0 <= yy < h and (x, yy) not in mask and not is_text(px[x, yy], kw.get('thr', 100), kw.get('sat', 80)):
                src[(x, y)] = px[x, yy]; break
    for q, c in src.items(): px[q] = c
    return mask


def erase(img, box, period=4, **kw):
    if kw.pop('vertical', False):
        return erase_vertical(img, box, **kw)
    """img: RGB image (modified in place). Masked pixels get the most common clean colour of the
    same row and dither phase (falling back to nearby rows). Returns the mask."""
    from collections import Counter
    px = img.load()
    mask, core = text_mask(px, box, **kw)
    guard, _ = text_mask(px, box, halo=kw.get('halo', 2) + 2, **{k: v for k, v in kw.items() if k != 'halo'})
    l, t, r, b = box
    model = {}
    for y in range(t, b):
        for ph in range(period):
            c = Counter(px[x, y] for x in range(l + ph, r, period) if (x, y) not in guard and lum(px[x, y]) < kw.get('thr', 100))
            if c: model[(y, ph)] = c.most_common(1)[0][0]
    # clean samples per (row, phase), for a local choice: the panel gradients also vary horizontally
    samples = {}
    for y in range(t, b):
        for x in range(l, r):
            if (x, y) not in guard and lum(px[x, y]) < kw.get('thr', 100):
                samples.setdefault((y, (x - l) % period), []).append((x, px[x, y]))
    orig = {q: px[q] for q in mask}
    for (x, y) in mask:
        ph = (x - l) % period
        col = None
        # copy the texture from the nearest clean pixel of the same row and dither phase, alternating
        # sides so long gaps don't smear one column (the panels use a noisy dither)
        near = [c for (sx, c) in samples.get((y, ph), ()) if abs(sx - x) <= 40]
        mode = Counter(near).most_common(1)[0][0] if len(near) >= 3 else model.get((y, ph))
        cand = [(abs(sx - x), sx, c) for (sx, c) in samples.get((y, ph), ()) if abs(sx - x) <= 60]
        if mode is not None:   # only copy texture that matches the local background tone (no frame shading)
            cand = [q for q in cand if abs(lum(q[2]) - lum(mode)) <= 12]
        if len(cand) >= 4:     # a pseudo-random pick keeps the noise statistics without visible structure
            col = cand[((x * 73856093) ^ (y * 19349663)) % len(cand)][2]
        else:
            col = mode
        col = col or model.get((y, ph))
        k = 1
        while col is None and k < 8:
            col = model.get((y - k, ph)) or model.get((y + k, ph)); k += 1
        if col is None:   # dense text: any phase of this row, then the nearest rows
            k = 0
            while col is None and k < (b - t):
                for q in range(period):
                    col = col or model.get((y - k, q)) or model.get((y + k, q))
                k += 1
        if col: px[x, y] = col
    return mask


def is_frame(c):
    return c[2] >= 150 and c[2] > c[0] + 60


def find_interior(img, pad=1):
    """Inner box of a framed panel: scan outward from the centre until the saturated-blue frame."""
    px = img.load(); w, h = img.size
    cx, cy = w // 2, h // 2
    def scan(x0, y0, dx, dy, n):
        x, y = x0, y0
        while 0 <= x + dx < w and 0 <= y + dy < h and n > 0:
            x += dx; y += dy; n -= 1
            if is_frame(px[x, y]) and (0 <= x + dx < w and 0 <= y + dy < h) and is_frame(px[x + dx, y + dy]):
                return x
            if dy and is_frame(px[x, y]) and (0 <= y + dy < h) and is_frame(px[x, y + dy]):
                return y
        return x if dx else y
    import statistics
    ls = [scan(cx, y, -1, 0, w) for y in range(h // 4, 3 * h // 4, 3)]
    rs = [scan(cx, y, 1, 0, w) for y in range(h // 4, 3 * h // 4, 3)]
    ts, bs = [], []
    for x in range(w // 4, 3 * w // 4, 5):
        y = cy
        while y > 0 and not (is_frame(px[x, y]) and is_frame(px[x, y - 1])): y -= 1
        ts.append(y)
        y = cy
        while y < h - 1 and not (is_frame(px[x, y]) and is_frame(px[x, y + 1])): y += 1
        bs.append(y)
    l, r = int(statistics.median(ls)), int(statistics.median(rs))
    t, b = int(statistics.median(ts)), int(statistics.median(bs))
    return (l + 1 + pad, t + 1 + pad, r - pad, b - pad)


def wrap(draw, text, font, width):
    lines = []
    for para in text.split('\n'):
        words = para.split(' ')
        cur = ''
        for w in words:
            cand = (cur + ' ' + w) if cur else w
            if draw.textlength(cand, font=font) <= width or not cur:
                cur = cand
            else:
                lines.append(cur)
                cur = w
        lines.append(cur)
    return lines


def fit_text(text, box, font_name, size, min_size=9, leading=1.3, nowrap=False):
    """Pick the largest size <= `size` whose wrapped text fits in the box (one line per paragraph if nowrap)."""
    l, t, r, b = box
    tmp = ImageDraw.Draw(Image.new('L', (1, 1)))
    for s in range(size, min_size - 1, -1):
        f = ImageFont.truetype(find_font(font_name), s)
        if nowrap:
            lines = text.split('\n')
            if max(tmp.textlength(x, font=f) for x in lines) > r - l: continue
        else:
            lines = wrap(tmp, text, f, r - l)
        lh = int(round(s * leading))
        if len(lines) * lh <= b - t + (lh - s):
            return f, lines, lh
    f = ImageFont.truetype(find_font(font_name), min_size)
    return f, wrap(tmp, text, f, r - l), int(round(min_size * leading))


def draw_text(img, box, text, font_name='Georgia Bold.ttf', size=13, color=(255, 255, 255), shadow=None,
              align='left', valign='top', leading=1.3, stroke=0, stroke_color=(0, 0, 0), nowrap=False):
    font, lines, lh = fit_text(text, box, font_name, size, leading=leading, nowrap=nowrap)
    if stroke:
        st = Image.new('L', img.size, 0); dst = ImageDraw.Draw(st)
    l, t, r, b = box
    layer = Image.new('L', img.size, 0)
    sh = Image.new('L', img.size, 0) if shadow else None
    d = ImageDraw.Draw(layer)
    ds = ImageDraw.Draw(sh) if shadow else None
    total = len(lines) * lh
    y = t if valign == 'top' else (t + (b - t - total) // 2 if valign == 'middle' else b - total)
    for ln in lines:
        w = d.textlength(ln, font=font)
        x = l if align == 'left' else (l + (r - l - w) / 2 if align == 'center' else r - w)
        if ds:
            ds.text((x + 1, y + 1), ln, font=font, fill=255)
        if stroke:
            dst.text((x, y), ln, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
        d.text((x, y), ln, font=font, fill=255)
        y += lh
    if stroke:
        img.paste(Image.new('RGB', img.size, tuple(stroke_color)), (0, 0), st)
    if sh:
        img.paste(Image.new('RGB', img.size, shadow), (0, 0), sh)
    img.paste(Image.new('RGB', img.size, color), (0, 0), layer)
    return font.size, len(lines)


def quantize_changed(orig_p, new_rgb, palette):
    """Map new_rgb back to palette indices; pixels whose colour did not change keep their index."""
    pal = [tuple(palette[3 * i:3 * i + 3]) for i in range(256)]
    cache = {}
    out = orig_p.copy()
    op = orig_p.load(); np_ = new_rgb.load(); oo = out.load()
    w, h = orig_p.size
    for y in range(h):
        for x in range(w):
            c = np_[x, y]
            if pal[op[x, y]] == c:
                continue
            if c not in cache:
                best = min(range(256), key=lambda i: 3 * (pal[i][0] - c[0]) ** 2 + 4 * (pal[i][1] - c[1]) ** 2 + 2 * (pal[i][2] - c[2]) ** 2)
                cache[c] = best
            oo[x, y] = cache[c]
    return out
