# Generator for translation/en/mdroom.json: Ayase's MD collection (hidden; unlocked by the magic trick)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
S = {"_movie": "mdroom.dxr: Ayase's MD player (BGM / S.E discs); STOP/BACK/SKIP/PLAY and 'PERSONA' are English"}
S.update(yesno('mdroom', 11, 12, 13, 'Ayase: "Wanna listen to Ayase\'s MDs~?"', ja='…:…MD…?…'))
# handwritten ペルソナ on the disc labels -> PERSONA (keeping "BGM!" / "S.E" and "[AYASE]")
RED, BLUE = [214, 16, 24], [33, 49, 189]
INK = {'red': lambda c: c[0] > 90 and c[0] > c[1] + 70 and c[0] > c[2] + 40, 'blue': lambda c: c[2] > 80 and c[2] > c[0] + 45 and c[2] > c[1] + 25}
def strokes(n, box, ink):
    im = load('mdroom', n); px = im.load(); l, t, r, b = box
    return {(x, y) for y in range(t, b) for x in range(l, r) if INK[ink](px[x, y])}
def offset(base, n, box, ink, dys=range(-30, 40)):
    P = strokes(base, box, ink); im = load('mdroom', n); w, h = im.size; px = im.load()
    best = (-1, 0, 0)
    for dy in dys:
        for dx in range(-40, 40):
            m = sum(1 for (x, y) in P if 0 <= x + dx < w and 0 <= y + dy < h and INK[ink](px[x + dx, y + dy]))
            if m > best[0]: best = (m, dx, dy)
    return best[1], best[2]
def label(b, ink, col):
    return [{"erase_sat": b, "ink": ink},
            {"text": "PERSONA", "box": [b[0] + 2, b[1] + 5, b[2] - 4, b[3] + 2], "angle": -8, "font": "MarkerFelt.ttc", "size": 20, "color": col}]
for base, variants, box, col, ink in ((15, [16, 17, 18], [48, 106, 128, 149], RED, 'red'), (19, [20, 21, 22], [47, 104, 134, 148], BLUE, 'blue')):
    for n in [base] + variants:
        dx, dy = (0, 0) if n == base else offset(base, n, box, ink)
        S[str(n)] = {"ja": "ペルソナ (handwritten disc label)", "ops": label([box[0] + dx, box[1] + dy, box[2] + dx, box[3] + dy], ink, col)}
# discs 17/18 also show the top of the S.E disc's label at their upper edge
SE = [47, 104, 134, 148]
for n in (17, 18):
    dx, dy = offset(19, n, SE, 'blue', range(-140, -60))
    # the discs are drawn with background-transparent ink: the S.E disc underneath shows through this strip
    # and supplies its own (translated) label, so the copy here is simply removed
    S[str(n)]["ops"] = [{"erase_sat": [SE[0] + dx, max(0, SE[1] + dy), SE[2] + dx + 12, SE[3] + dy], "ink": "blue"}] + S[str(n)]["ops"]
    print('S.E label in', n, 'at offset', dx, dy)
# the player's LCD title (dot-matrix ペルソナ)
S["14"] = {"ja": "ペルソナ (LCD title)", "ops": [{"fill": [163, 244, 226, 264], "color": [173, 189, 173]},
           {"text": "PERSONA", "box": [165, 247, 240, 263], "font": "Arial Bold.ttf", "size": 11, "color": [49, 57, 49], "nowrap": True}]}
json.dump(S, open('translation/en/mdroom.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
