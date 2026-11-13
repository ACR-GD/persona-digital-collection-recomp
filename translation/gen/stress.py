# Generator for translation/en/stress.json: Ms. Natsumi's stress check (nurse's office)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
S = {"_movie": "stress.dxr: Ms. Natsumi's stress check: 10 yes/no questions on a clipboard, then a diagnosis"}
S.update(yesno('stress', 13, 14, 15, 'Ms. Natsumi: "Oh my? Not feeling well? Shall I have a look at you?"',
               ja='…:…? …? …?…'))
BLUE = [49, 49, 206]
qs = {
 19: ("I am currently playing Megami Ibunroku Persona.", "…"),
 20: ("When I enter a new dungeon, I can't rest until every corner is mapped.", "…"),
 21: ("I'm struggling to get a spell card out of Kuchisake.", "“…”…"),
 22: ("I'm bad at poker, so I save battle money to buy coins and collect relics.", "…"),
 23: ("This is not my first time playing Persona.", "…1…"),
 24: ("I'm lost in a dungeon and haven't seen an Agastya Tree in three days.", "…3…"),
 25: ("I like the Maki in the hospital bed more than the lively Maki of the other world.", "…"),
 26: ("When something goes wrong, I end up yelling \"Peru-peru-peru-peru...\"", "…"),
 27: ("Lately, when I sit on a Western toilet, I often see a red hand reaching out of the bowl.", "…"),
 28: ("I think I might really be a butterfly.", "…"),
}
for i, (n, (en, ja)) in enumerate(qs.items()):
    w, h = load('stress', n).size
    S[str(n)] = {"ja": "質問%d: %s" % (i + 1, ja), "ops": [
        {"fill": [0, 0, w, h], "color": [255, 255, 255]},
        {"text": "Question %d:" % (i + 1), "box": [0, 0, w, 20], "font": "Arial.ttf", "size": 16, "color": BLUE},
        {"text": en, "box": [0, 34, w, h], "font": "Arial.ttf", "size": 15, "color": BLUE, "leading": 1.25}]}
# clipboard (18): red はい / いいえ under the paper; hover sprites 29/30 are blue versions at the same place
P = load('stress', 18).load()
def place(n):
    H = load('stress', n); Hp = H.load(); w, h = H.size
    pts = [(x, y) for y in range(h) for x in range(w) if Hp[x, y][2] > 150 and Hp[x, y][0] < 120]
    best = None
    for y in range(300, 345):
        for x in range(230, 400):
            m = sum(1 for (xx, yy) in pts if P[x + xx, y + yy][0] > 150 and P[x + xx, y + yy][1] < 90)
            if best is None or m > best[0]: best = (m, x, y)
    return best[1], best[2], w, h
yx, yy, yw, yh = place(29); nx, ny, nw, nh = place(30)
RED = [198, 0, 33]
S["18"] = {"ja": "はい / いいえ (clipboard)", "ops": [
    {"fill": [yx - 2, yy, yx + yw + 1, yy + yh], "color": [255, 255, 255]},
    {"fill": [nx - 1, ny, nx + nw + 2, ny + nh], "color": [255, 255, 255]},
    {"text": "Yes", "box": [yx, yy, yx + yw, yy + yh], "align": "center", "valign": "middle", "font": "Arial.ttf", "size": 17, "color": RED},
    {"text": "No", "box": [nx, ny, nx + nw, ny + nh], "align": "center", "valign": "middle", "font": "Arial.ttf", "size": 17, "color": RED}]}
for n, (w, h), t in ((29, (yw, yh), "Yes"), (30, (nw, nh), "No")):
    S[str(n)] = {"ja": "はい / いいえ (hover)", "ops": [{"fill": [0, 0, w, h], "color": [255, 255, 255]},
        {"text": t, "box": [0, 0, w, h], "align": "center", "valign": "middle", "font": "Arial.ttf", "size": 17, "color": BLUE}]}
res = {
 37: ('Ms. Natsumi: "Aren\'t you pushing your body a little too hard? I\'ll prescribe you some Medicine."', '…:…? "…"…'),
 39: ('Ms. Natsumi: "Looks like some stress has built up. I\'ll give you a Chewing Soul."', '…:…'),
 41: ('Ms. Natsumi: "You look quite worn out. This Hiranya should help."', '…:…'),
 43: ('Ms. Natsumi: "Goodness! How could you let it get this bad?! I\'ll use Revival Incense for first aid, but make sure you get plenty of rest afterwards."', '…:…! …?! …"…"…'),
 45: ('Ms. Natsumi: "You\'re fine. Nothing seems wrong at all. Come by any time you don\'t feel well."', '…:…'),
}
from textpanel import find_interior
for n, (en, ja) in res.items():
    im = load('stress', n); l, t, r, b = find_interior(im.crop((130, 0) + im.size))
    S[str(n)] = {"ja": ja, "box": [l + 131, t, r + 131, b + 2], "text_box": [l + 134, t + 3, r + 128, b + 2], "en": en}
json.dump(S, open('translation/en/stress.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S), (yx, yy), (nx, ny))
