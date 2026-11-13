# Generator for translation/en/yamaoka.json: Butler Yamaoka's secret VTR collection (hidden; unlocked by the shooting gallery)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import load
from textpanel import find_interior
S = {"_movie": "yamaoka.dxr: Yamaoka's VTRs: 4 remix movies (titles already English) + the unused Snow Queen ending; red LED menu and subtitles"}
from tl_gen import yesno
S.update(yesno('yamaoka', 9, 12, 13,
    'Butler Yamaoka: "It has been a long time. I, Yamaoka, have returned, wishing to see you all once more! Would you care to view the secret VTRs I have recorded?"',
    ja='…:…! …VTR…?…', size=12))
def led(n, text, ja, arrow=False):
    im = load('yamaoka', n); px = im.load(); w, h = im.size
    # grid origin: first column / row that is mostly grid colour
    gx = next(x for x in range(8) if sum(1 for y in range(h) if px[x, y] == (85, 0, 0)) > h * 0.5)
    gy = next(y for y in range(8) if sum(1 for x in range(w) if px[x, y] == (85, 0, 0)) > w * 0.5)
    if arrow:   # keep the arrow and the underline: erase only the title text
        box = [18, 4, w - 3, 17]
    else:
        box = [0, 0, w, h]
    S[str(n)] = {"ja": ja, "ops": [{"led_grid": box, "origin": [gx, gy]},
                 {"pixel_text": text, "box": [box[0] + 2, box[1], box[2], box[3]] if arrow else [3, 1, w - 3, h - 2], "size": 13}]}
led(19, "Phantom Ending", "まぼろしのEnding", arrow=True)
led(24, '"Once, I dreamed I was a butterfly."', "…")
led(25, '"I fluttered about, a butterfly, content. Then I woke,\nand there I was, unmistakably myself."',
    "…")
led(26, '"Was I dreaming I was a butterfly, or is the butterfly dreaming it is me?"',
    "…")
led(27, '"Surely there is a difference between me and the butterfly,\nyet no absolute one. Cause and effect do not hold there."',
    "…")
led(28, "The Snow Queen ending movie, cut for disc space.", "…")
# the same entry baked into the VTR screen background (member 14)
S["14"] = {"ja": "まぼろしのEnding (menu)", "ops": [{"led_grid": [108, 217, 236, 234], "origin": [7, 5]},
           {"pixel_text": "Phantom Ending", "box": [110, 218, 236, 233], "size": 13}]}
json.dump(S, open('translation/en/yamaoka.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
