# Generator for translation/en/puzzle.json: Maki's sliding puzzle (art room)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "puzzle.dxr: Maki's 15-puzzle; GIVE UP button already English"}
S.update(yesno('puzzle', 11, 12, 13,
    'Maki: "You came to see my paintings? Thank you! ...is what I\'d like to say, but a girl in black came and scattered my picture to pieces. Could you help me fix it?"',
    ja='…:…? …! …?…', size=12))
y25 = yesno('puzzle', 25, 12, 13, 'Maki: "Ugh, the girl in black got me again... Will you help me one more time? Please!"',
            ja='…:…1…? …!…')
S["25"] = y25["25"]
def panel(n, en, ja, size=13):
    im = load('puzzle', n); l, t, r, b = find_interior(im)
    S[str(n)] = {"ja": ja, "box": [l, t, r, b], "text_box": [l + 6, t + 3, r - 4, b], "en": en, "size": size}
panel(26, 'Maki: "Thank you! ...Huh? Yuko? She wanted me to go see her? Got it. Here, take this ticket."', '…:…! …? …? …? …')
panel(67, 'Maki: "Finish the puzzle within the time limit. If all the timer lamps go out, it\'s game over and the girl in black scatters my picture again. Click the screen when you\'re ready!"',
      '…:…', 12)
panel(68, 'Maki: "You know the 15-puzzle, right? Slide the pieces one at a time to finish the picture. If you just can\'t do it, click the "GIVE UP" button below to start over, even with a different picture. Click the screen when you\'re ready!"',
      '…:…15…? …"GIVE UP…"…', 12)
for n, en, ja, col in ((47, "Start!!", "すたーと!!", [239, 156, 49]), (48, "Game over~", "げ~むお~ば~", [49, 49, 206]), (49, "Well done!", "おめでとう♪", [214, 16, 33])):
    w, h = load('puzzle', n).size
    S[str(n)] = {"ja": ja, "ops": [{"fill": [0, 0, w, h], "color": [255, 255, 255]},
        {"text": en, "box": [0, 0, w, h], "align": "center", "valign": "middle", "font": "Arial Rounded Bold.ttf", "size": 52, "color": col, "nowrap": True}]}
S["24"] = {"ja": "制限時間 (timer frame)", "ops": [{"erase": [4, 8, 50, 30]},
    {"text": "TIME", "box": [4, 9, 50, 29], "align": "center", "valign": "middle", "font": "Arial Bold Italic.ttf", "size": 12}]}
json.dump(S, open('translation/en/puzzle.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
