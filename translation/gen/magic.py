# Generator for translation/en/magic.json: Reiji & Brown's magic trick (find Brown under one of three toilets)
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "magic.dxr: Reiji's shell game: Brown hides in one of three toilets, the player picks one"}
S.update(yesno('magic', 14, 15, 16, 'Reiji: "Wanna see my magic trick?"', ja='…:…?…'))
S.update(yesno('magic', 53, 54, 55,
    'Reiji: "...Wrong."\nBrown: "Too bad! I was over here! Wanna go again?"',
    ja='…:…:…! …! …?…'))
def panel(n, en, ja):
    im = load('magic', n); l, t, r, b = find_interior(im)
    S[str(n)] = {"ja": ja, "box": [l, t, r, b], "text_box": [l + 6, t + 3, r - 4, b], "en": en}
panel(19, 'Reiji: "Brown\'s hiding in one of these toilets. I\'ll shuffle \'em with my magic so you can\'t tell, and you guess which one. Got it? Watch close."',
      '…:…? …')
panel(20, 'Brown: "Okay, the great me is gonna hide now! Go easy on me, big bro Reiji~"',
      '…:…~…')
panel(52, 'Reiji: "......"\nBrown: "Bravo! A handshake with the great me, inside a toilet! Ha ha. Here, your ticket."',
      '…:…:…! …! …')
json.dump(S, open('translation/en/magic.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
