"""timeline.py out.png T1 T2 ...: grid (3 per row, 320x240) of build/shots frames nearest to the given seconds."""
import sys, glob, os
from PIL import Image
fs = sorted(glob.glob('build/shots/*.ppm'))
ts = [float(t) for t in sys.argv[2:]]
s = Image.new('RGB', (960, 240 * ((len(ts) + 2) // 3)))
for i, t in enumerate(ts):
    f = min(fs, key=lambda f: abs(int(os.path.basename(f)[1:5]) - t * 10))
    s.paste(Image.open(f).resize((320, 240)), ((i % 3) * 320, (i // 3) * 240))
s.save(sys.argv[1])
