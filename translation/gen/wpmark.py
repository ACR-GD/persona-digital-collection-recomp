# Generator for translation/en/wpmark.json (Mark's wallpaper exhibit)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno
S = {"_movie": "wpmark.dxr: Mark's wallpaper (graffiti art) gallery; INSTALL / AUTO PLAY / STOP buttons are already English"}
S.update(yesno('wpmark', 281, 282, 283,
    'Mark: "Whaddaya think? It\'s all about the shout of the soul, y\'know. Come check out my graffiti art!"',
    ja='…:…? …!… > … / …'))
# INSTALL ran the disc's Windows wallpaper changer (open "<cd>\\acce\\Wallchg.exe"), which cannot work
# here: the button is removed. Its frame and shadow are painted into the three gallery backgrounds (at
# 186-294 x 393-438): refill it with the photo texture just above (and, past the arrow buttons'
# column, from the right); the label sprite (member 291 at 204,404, copy ink) becomes that same patch.
BTN = [186, 393, 294, 438]; SPLIT = 279; UP = BTN[3] - BTN[1]   # button + its drop shadow
def unbutton(num):
    return {"ja": "INSTALL button (removed)", "ops": [
        {"restore": [num, 0, -UP, [BTN[0], BTN[1], SPLIT, BTN[3]]]},
        {"restore": [num, BTN[2] - SPLIT, 0, [SPLIT, BTN[1], BTN[2], BTN[3]]]}]}
for bg in (290, 301, 302): S[str(bg)] = unbutton(bg)
S["291"] = {"ja": "INSTALL label (removed)", "ops": [{"restore": [290, 204, 404 - UP, [0, 0, 75, 21]]}]}
S["_disable"] = [{"lscr": 188, "handler": 0, "literal": "Wallchg.exe"}]
json.dump(S, open('translation/en/wpmark.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
