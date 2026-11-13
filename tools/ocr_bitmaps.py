"""OCR the exported bitmaps (work/png/<movie>/NNNN.png) with tesseract jpn -> work/ocr/<movie>.json.
usage: ocr_bitmaps.py work/png work/ocr [movie ...]"""
import sys, os, json, subprocess, tempfile, glob
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageOps
src, out = sys.argv[1], sys.argv[2]
movies = sys.argv[3:] or sorted(d for d in os.listdir(src) if os.path.isdir(os.path.join(src, d)))
os.makedirs(out, exist_ok=True)
tmpd = tempfile.mkdtemp()
def has_jp(s): return any(0x3040 <= ord(c) <= 0x30ff or 0x4e00 <= ord(c) <= 0x9fff for c in s)
def ocr(path):
    im = Image.open(path).convert('L')
    w, h = im.size
    if w < 24 or h < 12: return ''
    if sum(im.get_flattened_data() if hasattr(im, "get_flattened_data") else im.getdata()) / (w * h) < 110: im = ImageOps.invert(im)
    im = im.resize((w * 3, h * 3), Image.LANCZOS)
    p = os.path.join(tmpd, os.path.basename(os.path.dirname(path)) + '_' + os.path.basename(path))
    im.save(p)
    r = subprocess.run(['tesseract', p, '-', '-l', 'jpn', '--psm', '6'], capture_output=True, text=True)
    return r.stdout.strip()
for mv in movies:
    files = sorted(glob.glob(os.path.join(src, mv, '*.png')))
    with ThreadPoolExecutor(8) as ex: texts = list(ex.map(ocr, files))
    res = {os.path.basename(f)[:-4]: t for f, t in zip(files, texts) if t and has_jp(t)}
    json.dump(res, open(os.path.join(out, mv + '.json'), 'w'), ensure_ascii=False, indent=1)
    print(mv, len(files), 'bitmaps,', len(res), 'with Japanese text')
