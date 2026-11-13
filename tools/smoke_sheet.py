"""smoke_sheet.py T [out.png]: grid of build/smoke/<n>/ screenshots nearest to T seconds."""
import sys, glob, os
from PIL import Image, ImageDraw
t = int(float(sys.argv[1]) * 10); out = sys.argv[2] if len(sys.argv) > 2 else 'build/smoke.png'
cells = []
for n in range(1, 13):
    fs = glob.glob('build/smoke/%d/s*.ppm' % n)
    if not fs: cells.append(None); continue
    f = min(fs, key=lambda f: abs(int(os.path.basename(f)[1:5]) - t))
    cells.append(Image.open(f).resize((320, 240)))
s = Image.new('RGB', (1280, 720)); d = ImageDraw.Draw(s)
for i, c in enumerate(cells):
    if c: s.paste(c, ((i % 4) * 320, (i // 4) * 240))
    d.text(((i % 4) * 320 + 4, (i // 4) * 240 + 4), str(i + 1), fill=(255, 255, 0))
s.save(out)
