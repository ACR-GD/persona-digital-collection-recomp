# Generator for translation/en/race.json: Ms. Saeko's floor-mopping race (gym); Start!/Win!!/Game Over/Click!! are English
import sys, json; sys.path.insert(0, 'tools')
from tl_gen import yesno, load
from textpanel import find_interior
S = {"_movie": "race.dxr: Ms. Saeko makes everyone mop the gym floor as a race; pick a character, then click fast"}
S.update(yesno('race', 13, 14, 15, 'Ms. Saeko: "Hey, you! Look at this mess! As punishment, everyone\'s mopping the floor!"',
               ja='…:…! …! …'))
S["91"] = yesno('race', 91, 14, 15, 'Ms. Saeko: "Oh dear, it\'s still not clean. Quicker, and put your back into it! Okay, from the top."',
                ja='…:…')["91"]
im = load('race', 92); l, t, r, b = find_interior(im)
S["92"] = {"ja": "…:…!…", "box": [l, t, r, b],
           "text_box": [l + 6, t + 3, r - 4, b], "en": 'Ms. Saeko: "Nice, squeaky clean. Good work! Okay, here\'s a ticket as your reward!"'}
S["36"] = {"ja": "> …", "box": [36, 305, 445, 385], "en": "> Choose your favorite character."}
json.dump(S, open('translation/en/race.json', 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
print(len(S))
