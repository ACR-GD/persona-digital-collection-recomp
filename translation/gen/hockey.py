# Generator for translation/en/hockey.json: Toro's turtle air hockey (cafeteria); GO!/Win!!/Game Over are English
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "hockey.dxr: Yokouchi (Toro) plays air hockey with his pet turtle"}
S.update(yesno('hockey', 321, 330, 331, 'Toro: "W-w-will y-you p-play with m-me?"', ja='…:…?…'))
S["322"] = yesno('hockey', 322, 330, 331, 'Toro: "P-p-play for r-real this time! W-wanna go a-again?"',
                 ja='…:…! …1…?…')["322"]
im = load('hockey', 323); l, t, r, b = find_interior(im)
S["323"] = {"ja": "…:…! …", "box": [l, t, r, b], "text_box": [l + 6, t + 3, r - 4, b],
            "en": 'Toro: "Y-you\'re a-amazing! H-here, h-have a t-ticket!"'}
json.dump(S, open('translation/en/hockey.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
