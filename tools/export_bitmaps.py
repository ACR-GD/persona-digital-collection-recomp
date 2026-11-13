"""Export every bitmap member of every Director file to work/png/<file>/<num>.png + a contact sheet."""
import sys, os, glob, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from director import *
from PIL import Image, ImageDraw, ImageFont
src, out = sys.argv[1], sys.argv[2]
index = {}
for p in sorted(glob.glob(src + '/*.dxr')):
    name = os.path.splitext(os.path.basename(p))[0]
    f = DirFile(p)
    clut = f.first('CLUT')
    pal = clut_rgb(clut.data) if clut else list(range(256))*3
    od = os.path.join(out, name); os.makedirs(od, exist_ok=True)
    entries = []
    for num, ch in f.cast_list():
        m = Member(num, ch)
        if m.tname != 'bitmap': continue
        im, bi = bitmap_image(f, m, pal)
        if im is None: continue
        fn = '%04d.png' % num
        im.save(os.path.join(od, fn))
        entries.append(dict(num=num, name=sjis(m.name), file=fn, **bi))
    index[name] = entries
    # contact sheets: thumbnails max 200x150, 6 per row
    TW, TH, COLS = 210, 170, 6
    for page in range(0, len(entries), 36):
        chunk = entries[page:page + 36]
        rows = (len(chunk) + COLS - 1) // COLS
        sheet = Image.new('RGB', (TW * COLS, TH * rows), (60, 60, 60))
        dr = ImageDraw.Draw(sheet)
        for i, e in enumerate(chunk):
            im = Image.open(os.path.join(od, e['file'])).convert('RGB')
            im.thumbnail((TW - 10, TH - 22))
            x, y = (i % COLS) * TW, (i // COLS) * TH
            sheet.paste(im, (x + 5, y + 18))
            dr.text((x + 4, y + 3), '%d %dx%d' % (e['num'], e['w'], e['h']), fill=(255, 255, 0))
        sheet.save(os.path.join(out, 'sheet_%s_%02d.png' % (name, page // 36)))
json.dump(index, open(os.path.join(out, 'index.json'), 'w'), ensure_ascii=False, indent=1)
print({k: len(v) for k, v in index.items()})
