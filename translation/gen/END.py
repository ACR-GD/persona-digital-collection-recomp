# Generator for translation/en/END.json: the staff roll shown after QUIT.
# Names: Western order (given name first). Readings of a few names are best guesses (see NOTES.md).
# Several cards exist in 2-4 animation variants with identical text: the primary card's text lines
# are found once, and each variant gets them at an offset found by matching the text pixels.
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import load
from textpanel import is_text
S = {"_movie": "END.dxr: staff roll; the Atlus / ASCII / QuickTime / Macromedia logos and the official Persona logo are kept"}
FONT, SIZE = 'Arial.ttf', 15

def lines_in(im, box):
    px = im.load(); l, t, r, b = box
    rows = [y for y in range(t, b) if sum(1 for x in range(l, r) if is_text(px[x, y], 170, 40)) >= 2]
    runs = []
    for y in rows:
        if runs and y - runs[-1][1] <= 2: runs[-1][1] = y
        else: runs.append([y, y])
    out = []
    for a, c in runs:
        xs = [x for y in range(a, c + 1) for x in range(l, r) if is_text(px[x, y], 170, 40)]
        out.append((a, c, min(xs), max(xs)))
    return out

def offset(prim, var, box):
    P = prim.load(); V = var.load(); l, t, r, b = box
    pts = [(x, y) for y in range(t, b, 2) for x in range(l, r, 2) if is_text(P[x, y], 170, 40)]
    best = (-1, 0, 0)
    for dy in range(-14, 15):
        for dx in range(-14, 15):
            m = 0
            for (x, y) in pts:
                xx, yy = x + dx, y + dy
                if 0 <= xx < var.size[0] and 0 <= yy < var.size[1] and is_text(V[xx, yy], 170, 40): m += 1
            if m > best[0]: best = (m, dx, dy)
    return best[1], best[2]

def card(primary, variants, blocks):
    """blocks: [(box, align, [english lines...])] with one English line per detected Japanese line."""
    P = load('END', primary)
    plan = []
    for blk in blocks:
        box, align, texts = blk[:3]
        limit = blk[3] if len(blk) > 3 else None     # how far English may extend (right edge if left-aligned, left edge if right-aligned)
        found = lines_in(P, box)
        assert len(found) == len(texts), (primary, box, len(found), len(texts))
        plan.append((box, align, found, texts, limit))
    for n in [primary] + variants:
        dx, dy = (0, 0) if n == primary else offset(P, load('END', n), plan[0][0])
        ops = []
        for box, align, found, texts, limit in plan:
            l, t, r, b = box
            if limit is not None:
                if align == 'left': r = limit
                else: l = limit
            for (a, c, x0, x1), text in zip(found, texts):
                ops.append({"erase": [x0 - 3 + dx, a - 2 + dy, x1 + 4 + dx, c + 3 + dy], "vertical": True})
                tb = [x0 + dx, a - 1 + dy, r + dx, c + 3 + dy] if align == 'left' else [l + dx, a - 1 + dy, x1 + 1 + dx, c + 3 + dy]
                ops.append({"text": text, "box": tb, "align": align, "valign": "middle", "font": FONT, "size": SIZE, "nowrap": True})
        S[str(n)] = {"ja": "staff card %d%s" % (primary, "" if n == primary else " (variant)"), "ops": ops}

card(273, [], [((10, 150, 235, 175), 'center', ["CD-ROM Production Staff"])])
S["273"]["ops"][1]["box"] = [10, 152, 235, 174]
card(274, [275], [((4, 0, 290, 192), 'left', [
    "Publisher & Editor  Hirokazu Hamamura", "Editor-in-Chief  Kenji Sugiuchi", "Production Purchasing  Hiroshi Hamahira",
    "Production Manager  Rei Sato", "Editorial Cooperation  Famitsu Books Editorial Dept."], 385)])
card(276, [277, 278, 279], [((290, 0, 537, 268), 'right', [
    "CD-ROM Planning & Composition", "Sonoko Mikami", "Director Authoring", "Kaori Harukawa / Momoyo Saito",
    "Application Programming", "Masahiro Sano", "Title & Browser Design", "Nendo grafixxx  Kentaro Fujimoto",
    "Icon Design", "ORTAS  Yosuke Ota"])])
card(280, [281], [((0, 0, 360, 275), 'left', [
    "Satomi Tadashi Pharmacy Filming", "Michiko Sato", "Opening Movie & Remix Movies", "Ai Ishihama",
    "SD Character Design", "ORTAS  Yosuke Ota / Makoto Miyauchi", "Dress-up Costume Design", "Hiroyo Ito / Hiroko Onose",
    "Data Editing", "Sonoko Mikami / Mayu Komura"])])
card(282, [283], [((215, 0, 495, 245), 'right', [
    "Screen Savers", "SD Character Design", "ORTAS  Yosuke Ota", "Evilphone Modeling", "Nendo grafixxx  Kentaro Fujimoto",
    "Design", "Tsuyoshi Kusano", "Animation", "Kaori Harukawa / Momoyo Saito / Hiroko Onose"], 125)])
card(284, [285, 286], [((0, 0, 265, 176), 'left', [
    "Wallpapers", "PEPPER SHOP  Manabu Koga", "K'LIMITED E  Koji Hanihara", "neuron  Miho Fukuda", "Tsuyoshi Kusano / Sonoko Mikami"])])
card(287, [288], [((240, 0, 503, 212), 'right', [
    "Special Thanks", "ATLUS Co., Ltd.", "Development Division 1, Development Section", "Kouji Okada / Kazuma Kaneko",
    "Tadashi Satomi / Takayuki Ide / Hidehito Aoki", "Consumer Sales Dept.", "Jun Morita / Rintaro Murata"], 135)])
card(289, [290], [((88, 0, 340, 150), 'left', [
    "Special Thanks", "Arts Vision Inc.", "Masayoshi Kinouchi", "Ken Ishizuka / Tomoe Hanba / Jun Mizusawa", "Masayo Kurata / Toranori Yoshikawa"], 480),
    ((88, 200, 330, 250), 'left', ["Chapter One Inc.", "Cosplex  Yuko Kobayashi"])])

# the little sign held up on the ATLUS card: ※…
for n in (287, 288):
    im = load('END', n); px = im.load(); w, h = im.size
    g = [(x, y) for y in range(230, h) for x in range(w // 2, w) if px[x, y][1] > px[x, y][0] + 25 and px[x, y][1] > px[x, y][2] + 25 and px[x, y][0] < 110]
    xs = [p[0] for p in g]; ys = [p[1] for p in g]
    l, t, r, b = min(xs), min(ys), max(xs) + 1, max(ys) + 1
    S[str(n)]["ops"] += [{"erase": [l + 2, t + 2, r - 2, b - 2], "thr": 120, "sat": 60},
        {"text": "*Complaints: Satomi Tadashi,\nMikage Sun Mall branch.", "box": [l + 3, t + 2, r - 2, b - 1], "align": "center",
         "valign": "middle", "font": "Arial.ttf", "size": 8, "leading": 1.05, "nowrap": True}]
json.dump(S, open('translation/en/END.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
