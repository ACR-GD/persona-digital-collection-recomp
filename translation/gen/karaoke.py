# Generator for translation/en/karaoke.json: Tadashi & Tamaki's karaoke ("The Satomi Tadashi Pharmacy Song")
# Lyric bitmaps are crops of the backdrop (member 286): the background is restored from it and the
# English lines are drawn at the same screen position in the base bitmap and in its highlight sprites.
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import load, yesno
BG = 286
B = load('karaoke', BG); Bp = B.load()
def locate(n):
    S = load('karaoke', n); Sp = S.load(); w, h = S.size
    pts = [(x, y) for y in range(0, h, 3) for x in range(0, w, 3) if not (Sp[x, y][0] > 200 and Sp[x, y][1] < 120) and not (Sp[x, y][2] > 180 and Sp[x, y][0] < 120) and sum(Sp[x, y]) < 700][:80]
    best = (0, 0, 0)
    for y in range(0, 480 - h + 1):
        for x in range(0, 640 - w + 1):
            m = sum(1 for (xx, yy) in pts if Bp[x + xx, y + yy] == Sp[xx, yy])
            if m > best[0]:
                best = (m, x, y)
                if m == len(pts): return x, y, w, h
    return best[1], best[2], w, h
RED, BLUE = [255, 0, 49], [49, 57, 239]
FONT = 'Arial Rounded Bold.ttf'
lyrics = [
 (289, 290, 291, '1', 'To restore your hit points,', 'Medicine, Beads!', '… / …'),
 (292, 293, 294, '', 'Near death? Help your friends!', 'Revival Bead and Incense', '… / …'),
 (295, 296, 297, '', 'Turned to stone? Dis-Stone!', 'Poison hurts? Dis-Poison!', '… / …'),
 (298, 299, 300, '', 'When you\'re paralyzed, Dis-Paralyze,', 'to cure your illness, Dis-Sick!', '… / …'),
 (301, 302, 303, '', 'Always on your side when you fight:', 'the pharmacy in our town!', '… / …'),
 (304, 305, 306, '2', 'Use them when you save:', 'Medicine, Beads!', '… / …'),
 (307, 308, 309, '', 'Fainted? No need for tears:', 'Revival Bead and Incense', '… / …'),
 (310, 311, 312, '', 'Too heavy to carry? Dis-Stone.', 'Forget it, you\'re dying: Dis-Poison!', '… / …'),
 (313, 314, 315, '', 'You want them when you\'re out:', 'Dis-Paralyze and Dis-Sick!', '… / …'),
 (316, 317, 318, '', 'Always there till they\'re gone,', 'that\'s why we love our pharmacy!', '… / …'),
 (319, 320, 321, '3', 'Toothache, tummy, head or back,', 'we\'ve got the cure!', '… / …'),
 (322, 323, 324, '', 'Ulcers, eye drops, herbs to go,', 'whatever ails you, leave it to us!', '… / …'),
 (325, 326, 327, '', 'What a handy world: Revival Incense!', 'Swallowed poison? Dis-Poison!', '… / …'),
 (328, 329, 330, '', 'At this rate, the average age', 'will be 200, even 300!', '… / 200… 300…'),
 (331, 332, 333, '', 'Supporting everybody\'s heart:', 'Satomi Tadashi Pharmacy!', '… / …'),
]
S = {"_movie": "karaoke.dxr: Tadashi & Tamaki's karaoke room, 'The Satomi Tadashi Pharmacy Song' (lyrics over the video)"}
S.update(yesno('karaoke', 283, 284, 285,
    'Tadashi: "It\'s a video my old man made... Wanna watch it?"',
    ja='…:…?… > … / …'))
def line(text, box, color, size=21):
    return {"text": text, "box": box, "align": "center", "valign": "middle", "font": FONT, "size": size, "stroke": 2, "stroke_color": color, "nowrap": True}
def marker(n, box, color):
    return {"text": "♪" + n, "box": box, "font": "Arial Unicode.ttf", "size": 17, "stroke": 2, "stroke_color": color}
for base, h1, h2, verse, l1, l2, ja in lyrics:
    bx, by, bw, bh = locate(base); x1, y1, w1, hh1 = locate(h1); x2, y2, w2, hh2 = locate(h2)
    # line areas in screen coordinates: full width of the base, height of each highlight sprite
    # each line is centred in its highlight sprite's rectangle (same screen position in base and highlight)
    a1 = [x1 + 2, y1, x1 + w1 - 2, y1 + hh1]; a2 = [x2 + 2, y2, x2 + w2 - 2, y2 + hh2]
    if verse: a1[0] = max(a1[0], bx + 40)   # leave room for the verse marker
    mk = [bx + 4, by, bx + 60, by + 22]
    def rel(a, ox, oy): return [a[0] - ox, a[1] - oy, a[2] - ox, a[3] - oy]
    ops = [{"bg_from": [BG, bx, by]}, line(l1, rel(a1, bx, by), RED), line(l2, rel(a2, bx, by), RED)]
    if verse: ops.append(marker(verse, rel(mk, bx, by), RED))
    S[str(base)] = {"ja": ja, "ops": ops}
    ops = [{"bg_from": [BG, x1, y1]}, line(l1, rel(a1, x1, y1), BLUE)]
    if verse: ops.append(marker(verse, rel(mk, x1, y1), BLUE))
    S[str(h1)] = {"ja": ja.split(' / ')[0] + ' (highlight)', "ops": ops}
    S[str(h2)] = {"ja": ja.split(' / ')[1] + ' (highlight)', "ops": [{"bg_from": [BG, x2, y2]}, line(l2, rel(a2, x2, y2), BLUE)]}
tx, ty, tw, th = locate(288)
S["288"] = {"ja": "…(logo) “…” …:…:…", "ops": [
    {"restore": [BG, tx, ty, [0, 100, tw, th]]},
    {"text": '"The Satomi Tadashi Pharmacy Song"', "box": [0, 104, tw, 128], "align": "center", "font": FONT, "size": 15, "stroke": 2, "stroke_color": [239, 0, 0]},
    {"text": "Lyrics: Misaki Okibe   Music: Hidehito Aoki", "box": [0, 131, tw, th], "align": "center", "font": FONT, "size": 12, "stroke": 2, "stroke_color": [239, 0, 0]}]}
lx, ly, lw, lh = locate(334)
S["334"] = {"ja": "…:…", "ops": [{"bg_from": [BG, lx, ly]},
    {"text": "Filmed in Mikage-cho", "box": [0, 0, lw, lh], "align": "center", "valign": "middle", "font": FONT, "size": 15, "stroke": 2, "stroke_color": [0, 0, 0]}]}
json.dump(S, open('translation/en/karaoke.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
