"""Apply a translation script to the Director movies.

usage: apply_translation.py DATA_DIR SCRIPT_DIR OUT_DIR [movie ...]
  DATA_DIR    the CD contents (data/), read only
  SCRIPT_DIR  translation/en: one <movie>.json per movie
  OUT_DIR     overlay directory (data_en/): patched movies are written to OUT_DIR/dc/<movie>.dxr,
              previews of every changed bitmap to work/preview/<movie>/

Script entries are keyed by cast member number:
  "281": {"ja": "...", "en": "..."}
      erase the text inside the panel interior (auto-detected) and draw the English text,
      starting where the Japanese text started.
  options: "box": [l,t,r,b] text area / erase area override, "size", "font", "color", "align",
           "valign", "leading", "thr" (text luminance threshold), "sat" (max saturation of text)
  "286": {"ja": "...", "ops": [{"erase": [l,t,r,b]}, {"text": "Yes", "box": [l,t,r,b], "align": "center"}]}
      explicit operations, applied in order.
  "en": null or a missing entry leaves the bitmap untouched.
  "_disable": [{"lscr": 188, "handler": 0, "literal": "..."}]
      Lingo handlers to switch off (they return at once), e.g. buttons that start Windows programs.
"""
import sys, os, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import DirFile, Member, bitmap_image, clut_rgb, bitmap_info
from dxrpatch import Patcher
from textpanel import erase, draw_text, find_interior, text_bbox, quantize_changed

DEFAULT_FONT = 'Georgia Bold.ttf'


def process(im_p, entry, load=None):
    rgb = im_p.convert('RGB')
    kw = {k: entry[k] for k in ('thr', 'sat', 'halo') if k in entry}
    if 'ops' in entry:
        for op in entry['ops']:
            if 'bg_from' in op:      # [member, x, y]: background = that member's original pixels at (x, y)
                src = load(op['bg_from'][0]).convert('RGB')
                x0, y0 = op['bg_from'][1], op['bg_from'][2]
                if len(op['bg_from']) > 3:   # erase this box of the source first (in its coordinates)
                    eb = op['bg_from'][3]
                    erase(src, tuple(eb[:4]), **({'thr': eb[4], 'sat': eb[5]} if len(eb) > 4 else {}))
                rgb.paste(src.crop((x0, y0, x0 + rgb.size[0], y0 + rgb.size[1])), (0, 0))
            if 'restore' in op:      # [member, x, y, [l,t,r,b]]: copy that member's pixels at (x+l, y+t) into box l,t,r,b
                src = load(op['restore'][0]).convert('RGB')
                x0, y0 = op['restore'][1], op['restore'][2]; l, t, r, b = op['restore'][3]
                rgb.paste(src.crop((x0 + l, y0 + t, x0 + r, y0 + b)), (l, t))
            if 'led_grid' in op:     # rebuild an LED-panel background: dark-red grid lines every 8 px on black
                px = rgb.load(); l, t, r, b = op['led_grid']; ox, oy = op.get('origin', (1, 5))
                for y in range(t, b):
                    for x in range(l, r):
                        if px[x, y] == (255, 0, 0):
                            px[x, y] = (85, 0, 0) if (x - ox) % 8 == 0 or (y - oy) % 8 == 0 else (0, 0, 0)
            if 'pixel_text' in op:   # crisp (non anti-aliased) text, e.g. on the LED panels
                from PIL import Image, ImageDraw
                from textpanel import fit_text
                l, t, r, b = op['box']
                font, lines, lh = fit_text(op['pixel_text'], (l, t, r, b), op.get('font', 'Arial.ttf'), op.get('size', 12), nowrap=True)
                m = Image.new('1', rgb.size, 0); d = ImageDraw.Draw(m); d.fontmode = '1'
                y = t + (b - t - lh * len(lines)) // 2
                for ln in lines:
                    d.text((l, y), ln, font=font, fill=1); y += lh
                rgb.paste(Image.new('RGB', rgb.size, tuple(op.get('color', (255, 0, 0)))), (0, 0), m)
            if 'erase_dark' in op or 'erase_sat' in op:
                # strokes on a light texture: dark pixels (erase_dark, sum < thr) or coloured ones (erase_sat,
                # max-min > sat) are replaced from the nearest clean pixel of the row
                from collections import Counter
                px = rgb.load()
                if 'erase_dark' in op:
                    l, t, r, b = op['erase_dark']; th = op.get('thr', 600)
                    pick = lambda c: sum(c) < th
                else:
                    l, t, r, b = op['erase_sat']; th = op.get('sat', 70); ink = op.get('ink')
                    if ink == 'red': pick = lambda c: c[0] > 90 and c[0] > c[1] + 70 and c[0] > c[2] + 40
                    elif ink == 'blue': pick = lambda c: c[2] > 80 and c[2] > c[0] + 45 and c[2] > c[1] + 25
                    else: pick = lambda c: max(c) - min(c) > th
                dark = {(x, y) for y in range(t, b) for x in range(l, r) if pick(px[x, y])}
                halo = {(x + dx, y + dy) for (x, y) in dark for dx in (-1, 0, 1) for dy in (-1, 0, 1) if l <= x + dx < r and t <= y + dy < b}
                for y in range(t, b):
                    row = [px[x, y] for x in range(l, r) if (x, y) not in halo]
                    if not row: continue
                    c = Counter(row).most_common(1)[0][0]
                    for x in range(l, r):
                        if (x, y) in halo:
                            # nearest clean pixel on the row keeps the marble texture
                            for k in range(1, r - l):
                                if x - k >= l and (x - k, y) not in halo: px[x, y] = px[x - k, y]; break
                                if x + k < r and (x + k, y) not in halo: px[x, y] = px[x + k, y]; break
                            else: px[x, y] = c
            if 'erase' in op:
                erase(rgb, tuple(op['erase']), **({k: op[k] for k in ('thr', 'sat', 'period', 'vertical', 'halo') if k in op} or kw))
            if 'fill' in op:
                l, t, r, b = op['fill']
                rgb.paste(tuple(op.get('color', (0, 0, 0))), (l, t, r, b))
            if 'text' in op and 'angle' in op:   # rotated text (e.g. handwriting on a tilted disc)
                from PIL import Image, ImageDraw
                from textpanel import fit_text
                l, t, r, b = op['box']
                font, lines, lh = fit_text(op['text'], (0, 0, r - l, b - t), op.get('font', entry.get('font', DEFAULT_FONT)),
                                           op.get('size', 13), nowrap=True)
                pad = 20
                layer = Image.new('L', (r - l + 2 * pad, b - t + 2 * pad), 0); d = ImageDraw.Draw(layer)
                y = pad + (b - t - len(lines) * lh) // 2
                for ln in lines:
                    d.text((pad, y), ln, font=font, fill=255); y += lh
                layer = layer.rotate(op['angle'], resample=Image.BICUBIC)
                rgb.paste(Image.new('RGB', layer.size, tuple(op.get('color', (255, 255, 255)))), (l - pad, t - pad), layer)
            elif 'text' in op:
                draw_text(rgb, tuple(op['box']), op['text'], font_name=op.get('font', entry.get('font', DEFAULT_FONT)),
                          size=op.get('size', entry.get('size', 13)), color=tuple(op.get('color', entry.get('color', (255, 255, 255)))),
                          align=op.get('align', 'left'), valign=op.get('valign', 'top'), leading=op.get('leading', 1.3),
                          shadow=tuple(op['shadow']) if 'shadow' in op else None,
                          stroke=op.get('stroke', 0), stroke_color=tuple(op.get('stroke_color', (0, 0, 0))), nowrap=op.get('nowrap', False))
    else:
        box = tuple(entry['box']) if 'box' in entry else find_interior(rgb)
        bb = text_bbox(rgb, box, **kw)
        erase(rgb, box, **kw)
        if 'text_box' in entry:
            tb = tuple(entry['text_box'])
        elif bb:
            tb = (bb[0], max(box[1], bb[1] - 1), box[2], box[3])
        else:
            tb = box
        draw_text(rgb, tb, entry['en'], font_name=entry.get('font', DEFAULT_FONT), size=entry.get('size', 13),
                  color=tuple(entry.get('color', (255, 255, 255))), align=entry.get('align', 'left'),
                  valign=entry.get('valign', 'top'), leading=entry.get('leading', 1.3), nowrap=entry.get('nowrap', False))
    return quantize_changed(im_p, rgb, im_p.getpalette())


def main():
    data, scripts, out = sys.argv[1:4]
    movies = sys.argv[4:] or sorted(f[:-5] for f in os.listdir(scripts) if f.endswith('.json'))
    os.makedirs(os.path.join(out, 'dc'), exist_ok=True)
    for mv in movies:
        script = json.load(open(os.path.join(scripts, mv + '.json'), encoding='utf-8'))
        src = os.path.join(data, 'dc', mv + '.dxr')
        P = Patcher(src)
        clut = clut_rgb(P.f.first('CLUT').data)
        prev = os.path.join('work', 'preview', mv); os.makedirs(prev, exist_ok=True)
        n = 0
        for key, entry in script.items():
            if key.startswith('_') or not entry or (entry.get('en') is None and 'ops' not in entry):
                continue
            num = int(key)
            im, bi = bitmap_image(P.f, P.members[num], clut)
            new = process(im, entry, lambda k: bitmap_image(P.f, P.members[k], clut)[0])
            P.replace_bitmap(num, new)
            new.convert('RGB').save(os.path.join(prev, '%04d.png' % num))
            n += 1
        for dis in script.get('_disable', []):
            P.disable_handler(dis['lscr'], dis['handler'], dis.get('literal'))
        dst = os.path.join(out, 'dc', mv + '.dxr')
        P.save(dst)
        print('%s: %d bitmaps translated -> %s' % (mv, n, dst))


if __name__ == '__main__':
    main()
