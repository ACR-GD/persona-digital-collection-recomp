# Generator for translation/en/clothes.json: Trish's dress-up (hidden; unlocked by the puzzle)
# The UI (character tabs, BG:1-9, GO TO STUDIO!, SCREEN SHOT!) is already English.
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno
S = {"_movie": "clothes.dxr: Trish dresses up the party; only Trish's prompt and the BG 1 ceremony banner are Japanese"}
S.update(yesno('clothes', 283, 284, 285, 'Trish: "Ha-i! I\'m Trish. I\'ll use my magic to transform everyone! Ready?"',
               ja='…:…Ha-i! …! …?…'))
# background 1: graduation ceremony banner …
S["313"] = {"ja": "…", "ops": [
    {"erase_dark": [148, 76, 492, 143], "thr": 690},
    {"text": "32nd Graduation Ceremony", "box": [150, 78, 490, 141], "align": "center", "valign": "middle",
     "font": "Times New Roman.ttf", "size": 34, "color": [40, 40, 40], "nowrap": True}]}
json.dump(S, open('translation/en/clothes.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
