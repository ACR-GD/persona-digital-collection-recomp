"""Helpers to write translation scripts for recurring layouts (used from small per-movie generators).

yesno(movie, panel, yes, no, text) -> entries for a dialogue panel ending in "> はい / いいえ" and
its two hover sprites (はい / いいえ in lavender), which sit on top of the panel at offsets found by
template matching.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
from textpanel import find_interior

LAVENDER = [107, 107, 222]


def load(movie, num):
    return Image.open('work/png/%s/%04d.png' % (movie, num)).convert('RGB')


def find_offset(big, small, y0=0):
    """Best position of `small` in `big`, comparing only the small image's dark (background) pixels."""
    B = big.load(); S = small.load(); w, h = small.size
    pts = [(xx, yy) for yy in range(h) for xx in range(w) if sum(S[xx, yy]) < 200]
    glyph = [(xx, yy) for yy in range(h) for xx in range(w) if sum(S[xx, yy]) > 330]   # the hover text
    best = None
    for y in range(y0, big.size[1] - h + 1):
        for x in range(0, big.size[0] - w + 1):
            m = sum(1 for (xx, yy) in pts[::2] if B[x + xx, y + yy] == S[xx, yy])
            m += sum(1 for (xx, yy) in glyph if sum(B[x + xx, y + yy]) > 330)
            if best is None or m > best[0]: best = (m, x, y)
    return best[1], best[2]


def hover_color(movie, num):
    from collections import Counter
    im = load(movie, num)
    c = Counter(im.getdata())
    for col, _ in c.most_common():
        if col[2] > 150 and col[2] > col[0] + 40: return list(col)
    return LAVENDER


def yesno(movie, panel, yes, no, text, ja='', yes_text='Yes', no_text='No', size=13):
    P = load(movie, panel)
    Y, N = load(movie, yes), load(movie, no)
    yx, yy = find_offset(P, Y, P.size[1] // 2); nx, ny = find_offset(P, N, P.size[1] // 2)
    l, t, r, b = find_interior(P)
    row_t = min(yy, ny) - 2; row_b = max(yy + Y.size[1], ny + N.size[1])
    row = [yx - 3, row_t, nx + N.size[0] + 3, row_b]
    col = hover_color(movie, yes)
    out = {
        str(panel): {"ja": ja, "ops": [
            {"erase": [l, t, r, row_t]},
            {"text": text, "box": [l + 6, t + 4, r - 4, row_t], "size": size},
            {"erase": row, "thr": 60, "sat": 255},
            {"text": yes_text, "box": [yx, yy + 3, yx + Y.size[0], yy + Y.size[1]], "align": "center"},
            {"text": "/", "box": [yx + Y.size[0], yy + 3, nx, yy + Y.size[1]], "align": "center", "color": [90, 90, 222]},
            {"text": no_text, "box": [nx, ny + 3, nx + N.size[0], ny + N.size[1]], "align": "center"}]},
        str(yes): {"ja": "はい (hover)", "ops": [{"bg_from": [panel, yx, yy, row + [60, 255]]},
                   {"text": yes_text, "box": [1, 3, Y.size[0] + 1, Y.size[1]], "align": "center", "color": col}]},
        str(no): {"ja": "いいえ (hover)", "ops": [{"bg_from": [panel, nx, ny, row + [60, 255]]},
                  {"text": no_text, "box": [2, 3, N.size[0] + 2, N.size[1]], "align": "center", "color": col}]},
    }
    return out


def button(w, h, text, box, color=None, size=11, font='Arial Bold Italic.ttf', ja=''):
    """A bevelled or flat button label: erase `box`, centre `text` in it."""
    op = {"text": text, "box": box, "align": "center", "valign": "middle", "font": font, "size": size}
    if color: op["color"] = color
    return {"ja": ja, "ops": [{"erase": box}, op]}
