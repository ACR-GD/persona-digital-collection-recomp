"""sheet.py movie out.png num [num...]: paste bitmaps 1:1 with labels (wraps at 1040 px)."""
import sys
from PIL import Image, ImageDraw
mv, out, nums = sys.argv[1], sys.argv[2], sys.argv[3:]
ims = [(n, Image.open('work/png/%s/%04d.png' % (mv, int(n))).convert('RGB')) for n in nums]
W = 1040; x = y = rowh = 0; pos = []
for n, im in ims:
    if x + im.size[0] > W: x = 0; y += rowh + 14; rowh = 0
    pos.append((x, y)); x += im.size[0] + 6; rowh = max(rowh, im.size[1])
s = Image.new('RGB', (W, y + rowh + 14), (60, 60, 60)); d = ImageDraw.Draw(s)
for (n, im), (x, y) in zip(ims, pos): s.paste(im, (x, y + 12)); d.text((x, y), str(n), fill=(255, 255, 0))
s.save(out)
